; usbstorage - cles USB et disques (classe 08h, SCSI transparent 06h,
; Bulk-Only 50h), pilote lance par hwmanager (drivers.txt) avec
; « <peripherique> <interface » (docs/USB.md). Repris de
; src/kernel/arch/pi4/usb_msd.asm :
;  * transport Bulk-Only par usbd (op BULK, tampon partage de 68 Ko : la
;    commande en +0, le CSW en +512, les donnees en +4096) ;
;  * SCSI : INQUIRY, TEST UNIT READY, REQUEST SENSE, READ CAPACITY(10),
;    READ(10) / WRITE(10) par paquets de 128 secteurs ; LUN 0, 512 o ;
;  * peripherique bloc « blk.usb0 » (svc/inc/blkserv.inc : partitions MBR,
;    « blk.usb0p1 »...), puis un thread demande a vfs de monter la premiere
;    partition en USB0: (FatFS : FAT32 ou exFAT).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/usb.inc'
include '../../inc/fs.inc'
include '../../inc/blk.inc'

TAMPON		= 65536 + 4096
DONNEES		= 4096			; dans le tampon partage
CSW_OFF		= 512
MAX_BLOCS	= 128			; secteurs par commande
CBW_SIG		= 0x43425355		; « USBC »
CSW_SIG		= 0x53425355		; « USBS »
E_IO		= -5
E_NODEV		= -19

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	mov	x19,x0				; « <peripherique> <interface> »
	bl	kits_open
	cbz	x19,quit
	mov	x0,x19
	bl	parse_dec
	adr	x1,dev
	str	x0,[x1]
.esp:	ldrb	w0,[x19],1
	cbz	w0,quit
	cmp	w0,' '
	b.ne	.esp
	mov	x0,x19
	bl	parse_dec
	adr	x1,iface
	str	x0,[x1]
	adr	x0,s_usb			; usbd
	mov	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	bl	decrire				; extremites bulk
	cbz	x0,.inutile
	adr	x1,msg				; le tampon partage
	mov	x2,USB_OP_BULK_TAMPON
	str	x2,[x1]
	movl	x2,TAMPON
	str	x2,[x1,8]
	adr	x0,canal
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
	tbnz	x0,63,quit
	sys	SYS_BUF_MAP
	tbnz	x0,63,quit
	adr	x1,tampon
	str	x0,[x1]
	bl	preparer			; INQUIRY, pret, capacite
	cbz	x0,.muet
	adr	x0,s_disque			; un nom libre : usb0, usb1...
	mov	x19,0
.nom:	add	w1,w19,'0'
	strb	w1,[x0,3]
	adr	x0,s_blk_disque
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.libre
	add	x19,x19,1
	cmp	x19,9
	b.hs	quit
	adr	x0,s_disque
	b	.nom
.libre:	adr	x0,s_disque			; « USB : blk.usb0, 7600 Mo »
	ldrb	w1,[x0,3]
	adr	x0,s_vol
	strb	w1,[x0,3]
	adr	x0,s_vol_p
	strb	w1,[x0,3]
	adr	x0,s_vol_p1
	strb	w1,[x0,3]
	mov	x0,16384			; le montage, des que la partition
	bl	malloc				; est servie : un thread
	cbz	x0,.serv
	add	x1,x0,16384
	adr	x0,monter
	mov	x2,0
	sys	SYS_THREAD_CREATE
.serv:	adr	x0,lire				; le pilote du serveur de blocs
	adr	x1,blk_lire_fn
	str	x0,[x1]
	adr	x0,ecrire
	adr	x1,blk_ecrire_fn
	str	x0,[x1]
	adr	x0,s_disque
	mov	x1,0				; lecture et ecriture
	adr	x2,blocs
	ldr	x2,[x2]
	b	blk_demarrer			; (ne revient pas)
.muet:	adr	x0,m_muet
	bl	puts
	b	quit
.inutile:
	adr	x0,m_inutile
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
quit:	mov	x0,1
	sys	SYS_EXIT

; (thread) des que « blk.usbNp1 » est servi, vfs le monte en USBN: ; puis
; il veille : la cle retiree, le volume est demonte
monter:
	adr	x0,s_blk_p1
	mov	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.fin
	adr	x0,s_vfs
	mov	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.fin
	mov	x19,x0
	adr	x1,msg_vfs			; « USB0 blk.usb0p1 fatfs »
	mov	x2,VFS_MONTER
	str	x2,[x1]
	add	x0,x1,48
	adr	x1,s_vol
	bl	str_cat
	mov	x0,x19
	adr	x1,msg_vfs
	sys	SYS_SVC_CALL
	str	x0,[sp,-16]!
	adr	x0,m_monte
	bl	puts
	adr	x0,s_vol
	bl	puts
	adr	x0,m_fleche
	bl	puts
	ldr	x0,[sp],16
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,t_pret			; notification : « USB0: est pret »
	bl	noter
.veille:					; la cle est-elle encore la ? (sans
	mov	x0,1000				; transfert en cours, le retrait ne
	sys	SYS_SLEEP			; se verrait pas)
	adr	x1,msg_vfs
	mov	x2,USB_OP_DESCRIPTEURS
	str	x2,[x1]
	adr	x2,dev
	ldr	x2,[x2]
	str	x2,[x1,8]
	adr	x0,canal
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
	cmp	x0,E_NODEV
	b.ne	.veille
	b	retiree
.fin:	sys	SYS_THREAD_EXIT

; descripteurs : les extremites bulk IN et OUT de notre interface
; -> x0 = 1 si trouvees
decrire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x1,msg
	mov	x2,USB_OP_DESCRIPTEURS
	str	x2,[x1]
	adr	x2,dev
	ldr	x2,[x2]
	str	x2,[x1,8]
	adr	x0,canal
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.non
	adr	x1,msg
	ldr	x2,[x1,16]
	add	x19,x1,48
	add	x20,x19,x2
	mov	w21,0				; 1 : dans notre interface
	adr	x0,iface
	ldr	x22,[x0]
.d:	add	x0,x19,2
	cmp	x0,x20
	b.hi	.fini
	ldrb	w0,[x19]
	cmp	w0,2
	b.lo	.fini
	ldrb	w1,[x19,1]
	cmp	w1,USB_DT_INTERFACE
	b.ne	.ep
	ldrb	w2,[x19,2]
	ldrb	w3,[x19,3]
	cmp	x2,x22
	ccmp	w3,0,0,eq
	cset	w21,eq
	b	.n
.ep:	cbz	w21,.n
	cmp	w1,USB_DT_ENDPOINT
	b.ne	.n
	ldrb	w3,[x19,3]			; bulk seulement
	and	w3,w3,3
	cmp	w3,2
	b.ne	.n
	ldrb	w2,[x19,2]
	ldrb	w3,[x19,4]
	ldrb	w4,[x19,5]
	orr	w3,w3,w4,lsl 8
	and	w3,w3,0x7FF
	adr	x4,ep_out
	tbz	w2,7,.e
	adr	x4,ep_in
.e:	stp	x2,x3,[x4]			; extremite, taille max.
.n:	ldrb	w0,[x19]
	add	x19,x19,x0
	b	.d
.fini:	adr	x0,ep_in
	ldr	x0,[x0]
	adr	x1,ep_out
	ldr	x1,[x1]
	cmp	x0,0
	ccmp	x1,0,4,ne
	cset	x0,ne
	b	.out
.non:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = 1 IN / 0 OUT, x1 = decalage dans le tampon, x2 = longueur
; -> x0 = octets transferes, < 0 si erreur
bulk:
	prologue
	adr	x3,ep_out
	cbz	x0,.ep
	adr	x3,ep_in
.ep:	ldp	x4,x5,[x3]			; extremite, taille max.
	adr	x6,msg
	mov	x7,USB_OP_BULK
	str	x7,[x6]
	adr	x7,dev
	ldr	x7,[x7]
	stp	x7,x4,[x6,8]
	stp	x5,x1,[x6,24]
	str	x2,[x6,48]
	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,x6
	sys	SYS_SVC_CALL
	cmp	x0,E_NODEV			; cle retiree
	b.eq	retiree
	epilogue

; la cle a ete retiree : vfs demonte le volume (son service FatFS
; s'arrete), puis le pilote s'arrete
retiree:
	adr	x0,s_vfs
	mov	x1,1000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.fin
	adr	x1,msg_vfs
	mov	x2,VFS_DEMONTER
	str	x2,[x1]
	adr	x2,s_disque			; « usb0 »
	ldr	w3,[x2]
	str	w3,[x1,48]
	strb	wzr,[x1,52]
	sys	SYS_SVC_CALL
	adr	x0,m_retiree
	bl	puts
	adr	x0,t_retiree
	bl	noter
.fin:	mov	x0,0
	sys	SYS_EXIT

; x0 = SETUP -> x0 = octets, < 0 si erreur (sans donnees)
controle:
	prologue
	adr	x2,msg
	mov	x3,USB_OP_CONTROLE
	str	x3,[x2]
	adr	x3,dev
	ldr	x3,[x3]
	stp	x3,x0,[x2,8]
	str	xzr,[x2,24]
	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,x2
	sys	SYS_SVC_CALL
	epilogue

; w0 = 1 IN / 0 OUT : CLEAR_FEATURE(ENDPOINT_HALT) sur cette extremite
debloquer:
	prologue
	adr	x1,ep_out
	cbz	w0,.ep
	adr	x1,ep_in
.ep:	ldr	x1,[x1]
	usb_setup x0, 0x02, USB_REQ_CLEAR_FEATURE, 0, 0, 0
	orr	x0,x0,x1,lsl 32
	bl	controle
	epilogue

; remise a zero du transport : Bulk-Only Mass Storage Reset + deblocage
remettre:
	prologue
	adr	x1,iface
	ldr	x1,[x1]
	usb_setup x0, 0x21, 0xFF, 0, 0, 0
	orr	x0,x0,x1,lsl 32
	bl	controle
	mov	w0,1
	bl	debloquer
	mov	w0,0
	bl	debloquer
	epilogue

; x0 = bloc de commande SCSI (16 o), w1 = sa longueur, x2 = octets de donnees
; (tampon + DONNEES), w3 = 1 donnees entrantes -> x0 = statut CSW (0 bon,
; 1 echec de commande), -1 erreur de transport
commande:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	mov	x21,x2
	mov	w22,w3
	adr	x0,tampon
	ldr	x23,[x0]
	mov	x0,0				; CBW (31 octets)
.z:	str	xzr,[x23,x0]
	add	x0,x0,8
	cmp	x0,32
	b.lo	.z
	movl	w0,CBW_SIG
	str	w0,[x23]
	adr	x24,etiquette
	ldr	w0,[x24]
	add	w0,w0,1
	str	w0,[x24]
	str	w0,[x23,4]
	str	w21,[x23,8]
	mov	w0,0x80
	cmp	w22,0
	csel	w0,w0,wzr,ne
	strb	w0,[x23,12]
	strb	wzr,[x23,13]			; LUN 0
	strb	w20,[x23,14]
	mov	x0,0
.cb:	ldrb	w1,[x19,x0]
	add	x2,x23,15
	strb	w1,[x2,x0]
	add	x0,x0,1
	cmp	x0,16
	b.lo	.cb
	mov	x0,0
	mov	x1,0
	mov	x2,31
	bl	bulk
	cmp	x0,31
	b.ne	.reset
	cbz	x21,.csw			; donnees
	mov	w0,w22
	mov	x1,DONNEES
	mov	x2,x21
	bl	bulk
	tbz	x0,63,.csw
	mov	w0,w22				; echec (point d'acces bloque ?) :
	bl	debloquer			; debloquer, puis lire le CSW
.csw:	mov	x19,2				; essais
.csw_try:
	mov	x0,1
	mov	x1,CSW_OFF
	mov	x2,13
	bl	bulk
	cmp	x0,13
	b.hs	.csw_got
	mov	w0,1
	bl	debloquer
	subs	x19,x19,1
	b.ne	.csw_try
	b	.reset
.csw_got:
	add	x1,x23,CSW_OFF
	ldr	w0,[x1]
	movl	w2,CSW_SIG
	cmp	w0,w2
	b.ne	.reset
	ldr	w0,[x1,4]
	ldr	w2,[x24]
	cmp	w0,w2
	b.ne	.reset
	ldrb	w0,[x1,12]
	cmp	w0,2				; erreur de phase
	b.hs	.reset
	b	.out
.reset:	bl	remettre
	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; REQUEST SENSE (efface la condition en attente) -> x0 = cle, -1 si erreur
sense:
	prologue
	sub	sp,sp,32
	stp	xzr,xzr,[sp]
	stp	xzr,xzr,[sp,16]
	mov	w0,0x03
	strb	w0,[sp]
	mov	w0,18
	strb	w0,[sp,4]
	mov	x0,sp
	mov	w1,6
	mov	x2,18
	mov	w3,1
	bl	commande
	cbnz	x0,.err
	adr	x0,tampon
	ldr	x0,[x0]
	add	x0,x0,DONNEES
	ldrb	w0,[x0,2]
	and	x0,x0,0xF
	b	.out
.err:	mov	x0,-1
.out:	add	sp,sp,32
	epilogue

; INQUIRY, TEST UNIT READY, READ CAPACITY(10) -> x0 = 1 si le support est
; pret (blocs = nombre de secteurs)
preparer:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	sub	sp,sp,32
	adr	x0,tampon
	ldr	x20,[x0]
	add	x20,x20,DONNEES
	stp	xzr,xzr,[sp]			; INQUIRY : fabricant et produit
	stp	xzr,xzr,[sp,16]
	mov	w0,0x12
	strb	w0,[sp]
	mov	w0,36
	strb	w0,[sp,4]
	mov	x0,sp
	mov	w1,6
	mov	x2,36
	mov	w3,1
	bl	commande
	cbnz	x0,.ko
	adr	x0,m_inquiry
	bl	puts
	strb	wzr,[x20,32]			; 8..15 fabricant, 16..31 produit
	add	x0,x20,8
	bl	puts
	adr	x0,m_nl
	bl	puts
	mov	x21,20				; TEST UNIT READY (le support peut
.tur:	stp	xzr,xzr,[sp]			; demander un peu de temps)
	stp	xzr,xzr,[sp,16]
	mov	x0,sp
	mov	w1,6
	mov	x2,0
	mov	w3,0
	bl	commande
	cbz	x0,.pret
	tbnz	x0,63,.ko
	bl	sense
	mov	x0,100
	sys	SYS_SLEEP
	subs	x21,x21,1
	b.ne	.tur
	b	.ko
.pret:	stp	xzr,xzr,[sp]			; READ CAPACITY(10)
	stp	xzr,xzr,[sp,16]
	mov	w0,0x25
	strb	w0,[sp]
	mov	x0,sp
	mov	w1,10
	mov	x2,8
	mov	w3,1
	bl	commande
	cbnz	x0,.ko
	ldr	w0,[x20]			; dernier secteur (gros-boutiste)
	rev	w0,w0
	ldr	w1,[x20,4]			; taille de secteur
	rev	w1,w1
	cmp	w1,512
	b.ne	.taille
	add	x0,x0,1
	adr	x1,blocs
	str	x0,[x1]
	adr	x0,m_taille
	bl	puts
	adr	x0,blocs
	ldr	x0,[x0]
	lsr	x0,x0,11			; Mo
	bl	print_dec
	adr	x0,m_mo
	bl	puts
	mov	x0,1
	b	.out
.taille:
	adr	x0,m_taille_ko
	bl	puts
.ko:	mov	x0,0
.out:	add	sp,sp,32
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = secteur, x1 = nombre, x2 = adresse -> 0 ou erreur (blkserv)
lire:
	mov	x3,0
	b	transfert
ecrire:
	mov	x3,1
transfert:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,32			; bloc de commande
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
.next:	cbz	x20,.ok
	mov	x23,MAX_BLOCS			; paquet
	cmp	x20,x23
	csel	x23,x20,x23,lo
	adr	x0,blocs			; bornes du support
	ldr	x0,[x0]
	add	x1,x19,x23
	cmp	x1,x0
	b.hi	.ko
	cbz	x22,.cmd
	adr	x0,tampon			; ecriture : vers le tampon partage
	ldr	x0,[x0]
	add	x0,x0,DONNEES
	mov	x1,x21
	lsl	x2,x23,9
	bl	mem_move
.cmd:	mov	x24,2				; essais
.try:	stp	xzr,xzr,[sp]			; READ(10) / WRITE(10)
	stp	xzr,xzr,[sp,16]
	mov	w0,0x28
	cbz	x22,.op
	mov	w0,0x2A
.op:	strb	w0,[sp]
	rev	w0,w19
	strb	w0,[sp,2]			; LBA (gros-boutiste, non aligne)
	lsr	w0,w0,8
	strb	w0,[sp,3]
	lsr	w0,w0,8
	strb	w0,[sp,4]
	lsr	w0,w0,8
	strb	w0,[sp,5]
	lsr	w0,w23,8			; nombre (gros-boutiste)
	strb	w0,[sp,7]
	strb	w23,[sp,8]
	mov	x0,sp
	mov	w1,10
	lsl	x2,x23,9
	cmp	x22,0
	cset	w3,eq
	bl	commande
	cbz	x0,.fait
	tbnz	x0,63,.retry
	bl	sense				; echec de commande : effacer, recommencer
.retry:	subs	x24,x24,1
	b.ne	.try
	b	.ko
.fait:	cbnz	x22,.adv
	mov	x0,x21				; lecture : vers l'appelant
	adr	x1,tampon
	ldr	x1,[x1]
	add	x1,x1,DONNEES
	lsl	x2,x23,9
	bl	mem_move
.adv:	add	x19,x19,x23
	sub	x20,x20,x23
	add	x21,x21,x23,lsl 9
	b	.next
.ok:	mov	x0,0
	b	.out
.ko:	mov	x0,E_IO
.out:	add	sp,sp,32
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = suite du texte : notification « Cle USB » : « USB0: <suite> »
noter:
	prologue
	mov	x1,x0
	adr	x0,texte
	adr	x2,s_vol			; « USB0 »
	ldr	w2,[x2]
	str	w2,[x0]
	mov	w2,':'
	strb	w2,[x0,4]
	mov	w2,' '
	strb	w2,[x0,5]
	add	x0,x0,6
	bl	str_cat
	adr	x0,t_cle
	adr	x1,texte
	bl	notifier
	epilogue

include '../../inc/notify.inc'
include '../inc/blkserv.inc'

s_usb		db 'usb',0
s_vfs		db 'vfs',0
m_inquiry	db 'usbstorage : ',0
m_taille	db 'usbstorage : support de ',0
m_mo		db ' Mo',10,0
m_taille_ko	db 'usbstorage : secteurs de taille non geree (512 seulement)',10,0
m_muet		db 'usbstorage : support muet',10,0
m_inutile	db 'usbstorage : pas d''extremites bulk',10,0
m_monte		db 'usbstorage : montage de ',0
m_fleche	db ' -> ',0
m_nl		db 10,0
t_cle		db 'Cl',0xC3,0xA9,' USB',0
t_pret		db 'est pr',0xC3,0xAA,'t',0
t_retiree	db 'retir',0xC3,0xA9,'e, volume d',0xC3,0xA9,'mont',0xC3,0xA9,0
m_retiree	db 'usbstorage : cle retiree, volume demonte',10,0
		align 8

segment readable writeable
kit_data
blkserv_data
s_blk_disque	db 'blk.'
s_disque	db 'usb0',0
s_blk_p1	db 'blk.'
s_vol_p1	db 'usb0p1',0
s_vol		db 'USB0 blk.'
s_vol_p		db 'usb0p1 fatfs',0
		align 8
		align 8
canal		dd -1
dev		dd 0
iface		dd 0
ep_in		dd 0, 0				; extremite, taille max.
ep_out		dd 0, 0
tampon		dd 0
blocs		dd 0
etiquette	dd 0
msg		rb 512
msg_vfs		rb 512
texte		rb 96
