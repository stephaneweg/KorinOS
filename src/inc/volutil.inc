; volutil.inc - ce que partagent mount, eject et mkfs (src/bin ; comme volutil.h d'Onyx) : le nom
; d'un volume, la liste de vfs (VFS_VOLUMES), les infos et la synchronisation de son service
; (FS_INFO, FS_SYNC), sa source quand il n'est pas monte (SD0:/etc/mounts.txt, ou blk.usbNp1),
; monter et demonter, une taille en texte. Sur tool.inc (t_*).
;
;	include '../../inc/tool.inc'
;	include '../../inc/volutil.inc'
;	segment readable writeable	...	tool_data	volutil_data

VU_MAX		= 8			; volumes d'une reponse de vfs (464 / 56)
VU_ENT		= 56			; entree de VFS_VOLUMES : nom 16, source 24, service 16
VU_SRC		= 16
VU_FS		= 40
VU_MOUNTS	= 4096			; mounts.txt lu au plus

; x0 = argument (« usb0 », « USB0: », « usb0:/x ») -> vu_vol (« usb0 ») et vu_aff (« USB0: ») ;
; x0 = longueur du nom (0 : aucun)
vu_nom:
	adr	x1,vu_vol
	adr	x2,vu_aff
	mov	x3,0
.c:	ldrb	w4,[x0,x3]
	cbz	w4,.f
	cmp	w4,':'
	b.eq	.f
	cmp	w4,'/'
	b.eq	.f
	cmp	x3,14
	b.hs	.f
	sub	w5,w4,'A'			; minuscule
	cmp	w5,25
	b.hi	.min
	add	w4,w4,32
.min:	strb	w4,[x1,x3]
	sub	w5,w4,'a'			; majuscule
	cmp	w5,25
	b.hi	.maj
	sub	w4,w4,32
.maj:	strb	w4,[x2,x3]
	add	x3,x3,1
	b	.c
.f:	strb	wzr,[x1,x3]
	mov	w4,':'
	strb	w4,[x2,x3]
	add	x4,x3,1
	strb	wzr,[x2,x4]
	mov	x0,x3
	ret

; -> x0 = nombre de volumes montes (entrees VU_ENT a vu_msg + MSG_DATA), < 0 si vfs ne repond pas
vu_liste:
	prologue
	adr	x0,vu_msg
	mov	x1,VFS_VOLUMES
	mov	x2,0
	mov	x3,0
	adr	x4,vu_vide
	mov	x5,0
	bl	t_msg
	adr	x0,vu_msg
	bl	t_vfs
	tbnz	x0,63,.out
	adr	x0,vu_msg
	ldr	x0,[x0,MSG_B]
	cmp	x0,VU_MAX
	mov	x1,VU_MAX
	csel	x0,x0,x1,ls
.out:	epilogue

; x0 = nom en minuscules -> x0 = son entree de VFS_VOLUMES, 0 s'il n'est pas monte
vu_trouver:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	bl	vu_liste
	cmp	x0,0
	b.le	.non
	mov	x20,x0
	adr	x21,vu_msg
	add	x21,x21,MSG_DATA
.e:	mov	x0,x21
	mov	x1,x19
	bl	str_eq
	cbnz	x0,.oui
	add	x21,x21,VU_ENT
	subs	x20,x20,1
	b.ne	.e
.non:	mov	x0,0
	b	.out
.oui:	mov	x0,x21
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom en minuscules, x1 = op (FS_INFO, FS_SYNC) : requete au service « fs.<nom> »
; -> x0 = resultat (reponse dans vu_rep)
vu_fs_op:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x1,x0
	adr	x0,vu_tmp			; « fs.<nom> »
	str	x1,[sp,-16]!
	adr	x1,vu_s_fs
	bl	str_cat
	ldr	x1,[sp],16
	bl	str_cat
	adr	x0,vu_tmp
	mov	x1,500
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	mov	x20,x0
	adr	x1,vu_rep
	mov	x2,0
.z:	str	xzr,[x1,x2]
	add	x2,x2,8
	cmp	x2,MSG_DATA
	b.lo	.z
	str	x19,[x1]
	mov	x0,x20
	sys	SYS_SVC_CALL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom en minuscules : FS_SYNC -> x0 = fichiers ouverts, < 0 (E_INVAL : service sans sync)
vu_sync:
	mov	x1,FS_SYNC
	b	vu_fs_op

; x0 = nom en minuscules : FS_INFO -> x0 = 0 ; vu_rep : a octets, b libres (-1 : inconnu),
; c octets par cluster, d FS_T_* ; donnees : etiquette
vu_info:
	mov	x1,FS_INFO
	b	vu_fs_op

; x0 = FS_T_* -> x0 = son nom
vu_type_nom:
	adr	x1,vu_t_fat32
	cmp	x0,FS_T_FAT32
	b.eq	.r
	adr	x1,vu_t_exfat
	cmp	x0,FS_T_EXFAT
	b.eq	.r
	adr	x1,vu_t_ram
	cmp	x0,FS_T_RAM
	b.eq	.r
	adr	x1,vu_t_autre
.r:	mov	x0,x1
	ret

; x0 = octets : « 512 Ko », « 37,5 Mo », « 29,7 Go » sur la sortie
vu_taille:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x21,vu_u_ko
	mov	x20,10				; diviseur : 2^x20
	lsr	x0,x19,40
	cbz	x0,.g
	adr	x21,vu_u_to
	mov	x20,40
	b	.d
.g:	lsr	x0,x19,30
	cbz	x0,.m
	adr	x21,vu_u_go
	mov	x20,30
	b	.d
.m:	lsr	x0,x19,20
	cbz	x0,.d
	adr	x21,vu_u_mo
	mov	x20,20
.d:	lsrv	x0,x19,x20			; entier
	mov	x22,x0
	bl	t_putn
	cmp	x20,10
	b.eq	.u
	cmp	x22,100
	b.hs	.u
	bl	lang_get			; virgule (fr) ou point (en)
	ldrb	w1,[x0]
	mov	w0,','
	cmp	w1,'e'
	mov	w2,'.'
	csel	w0,w2,w0,eq
	bl	t_putc
	mov	x0,1				; dixieme
	lslv	x0,x0,x20
	sub	x0,x0,1
	and	x0,x19,x0
	mov	x1,10
	mul	x0,x0,x1
	lsrv	x0,x0,x20
	bl	t_putn
.u:	mov	x0,x21
	bl	t_dire
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom en minuscules, non monte : sa source et son service -> vu_src, vu_srv ; x0 = 1 si
; trouves (ligne de SD0:/etc/mounts.txt ; sinon usbN : blk.usbNp1, ou blk.usbN sans partition)
vu_source:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,vu_srv			; service par defaut
	adr	x1,vu_s_fatfs
	bl	str_cat
	adr	x0,vu_s_mounts
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.usb
	mov	x20,x0
	adr	x1,vu_mounts
	mov	x2,VU_MOUNTS-1
	sys	SYS_READ
	mov	x21,x0
	mov	x0,x20
	sys	SYS_CLOSE
	cmp	x21,0
	b.le	.usb
	adr	x20,vu_mounts
	strb	wzr,[x20,x21]
.ligne:	ldrb	w0,[x20]			; une ligne : « NOM = source, service »
	cbz	w0,.usb
	cmp	w0,'#'
	b.eq	.suiv
	mov	x1,0				; le nom, sans la casse
.n:	ldrb	w2,[x19,x1]
	ldrb	w3,[x20,x1]
	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.n1
	add	w3,w3,32
.n1:	cbz	w2,.nf
	cmp	w2,w3
	b.ne	.suiv
	add	x1,x1,1
	b	.n
.nf:	add	x0,x20,x1			; puis blancs, « = », blancs
	bl	.blancs
	ldrb	w1,[x0]
	cmp	w1,'='
	b.ne	.suiv
	add	x0,x0,1
	bl	.blancs
	adr	x1,vu_src			; la source, jusqu'a « , »
	mov	x2,0
.s:	ldrb	w3,[x0]
	cmp	w3,','
	b.eq	.sf
	cmp	w3,' '
	b.ls	.sf
	cmp	x2,23
	b.hs	.sf
	strb	w3,[x1,x2]
	add	x2,x2,1
	add	x0,x0,1
	b	.s
.sf:	strb	wzr,[x1,x2]
	cbz	x2,.suiv
.v:	ldrb	w3,[x0]				; le service
	cmp	w3,','
	b.eq	.v1
	cmp	w3,' '
	b.ne	.sv
.v1:	add	x0,x0,1
	b	.v
.sv:	adr	x1,vu_srv
	mov	x2,0
.s2:	ldrb	w3,[x0,x2]
	cmp	w3,' '
	b.ls	.s2f
	cmp	x2,15
	b.hs	.s2f
	strb	w3,[x1,x2]
	add	x2,x2,1
	b	.s2
.s2f:	cbz	x2,.oui				; (pas de service : fatfs)
	strb	wzr,[x1,x2]
	b	.oui
.suiv:	ldrb	w0,[x20],1
	cbz	w0,.usb
	cmp	w0,10
	b.ne	.suiv
	b	.ligne
.usb:	ldrb	w0,[x19]			; usbN : blk.usbNp1, sinon blk.usbN
	cmp	w0,'u'
	b.ne	.non
	ldrb	w0,[x19,1]
	cmp	w0,'s'
	b.ne	.non
	ldrb	w0,[x19,2]
	cmp	w0,'b'
	b.ne	.non
	adr	x0,vu_src
	adr	x1,vu_s_blk
	bl	str_cat
	mov	x1,x19
	bl	str_cat
	mov	x20,x0
	adr	x1,vu_s_p1
	bl	str_cat
	adr	x0,vu_src
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbz	x0,63,.oui
	strb	wzr,[x20]			; sans partition : le disque entier
	adr	x0,vu_src
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbz	x0,63,.oui
.non:	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.blancs:
	ldrb	w1,[x0]
	cmp	w1,' '
	b.eq	.b1
	cmp	w1,9
	b.ne	.b2
.b1:	add	x0,x0,1
	b	.blancs
.b2:	ret

; x0 = nom (vu_aff sans « : » convient), x1 = source, x2 = service : VFS_MONTER -> x0 = 0, erreur
vu_monter:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x0,vu_tmp			; « VOL source service »
	strb	wzr,[x0]
	mov	x1,x19
	bl	str_cat
	sub	x1,x0,1				; (sans le « : » final)
	ldrb	w2,[x1]
	cmp	w2,':'
	b.ne	.e1
	strb	wzr,[x1]
	mov	x0,x1
.e1:	adr	x1,vu_s_esp
	bl	str_cat
	mov	x1,x20
	bl	str_cat
	adr	x1,vu_s_esp
	bl	str_cat
	mov	x1,x21
	bl	str_cat
	adr	x0,vu_msg
	mov	x1,VFS_MONTER
	mov	x2,0
	mov	x3,0
	adr	x4,vu_tmp
	mov	x5,0
	bl	t_msg
	adr	x0,vu_msg
	bl	t_vfs
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom en minuscules : VFS_DEMONTER (son service s'arrete), puis attend que le volume ait
; quitte la liste (5 s au plus) -> x0 = 0, erreur
vu_demonter:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x4,x0
	adr	x0,vu_msg
	mov	x1,VFS_DEMONTER
	mov	x2,0
	mov	x3,0
	mov	x5,0
	bl	t_msg
	adr	x0,vu_msg
	bl	t_vfs
	tbnz	x0,63,.out
	mov	x20,50
.w:	mov	x0,x19
	bl	vu_trouver
	cbz	x0,.ok
	mov	x0,100
	sys	SYS_SLEEP
	subs	x20,x20,1
	b.ne	.w
	mov	x0,E_BUSY_VU
	b	.out
.ok:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

E_BUSY_VU	= -16
E_PERM_VU	= -1
E_IO_VU		= -5

; x0 = code d'erreur : son texte, traduit, sur la sortie d'erreur (sans fin de ligne)
vu_err:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,vu_e_occupe
	cmn	x19,16
	b.eq	.d
	adr	x0,vu_e_perm
	cmn	x19,1
	b.eq	.d
	adr	x0,vu_e_inval
	cmn	x19,22
	b.eq	.d
	adr	x0,vu_e_noent
	cmn	x19,2
	b.eq	.d
	adr	x0,vu_e_existe
	cmn	x19,17
	b.eq	.d
	adr	x0,vu_e_io
	cmn	x19,5
	b.eq	.d
	adr	x0,vu_e_nomem
	cmn	x19,12
	b.eq	.d
	adr	x0,vu_e_delai
	cmn	x19,110
	b.eq	.d
	adr	x0,vu_e_autre
	bl	t_derr
	sub	sp,sp,32
	neg	x0,x19
	mov	x1,sp
	bl	fmt_dec
	adr	x0,vu_moins
	bl	t_err
	mov	x0,sp
	bl	t_err
	add	sp,sp,32
	b	.out
.d:	bl	t_derr
.out:	ldr	x19,[sp],16
	epilogue

vu_vide		db 0
vu_s_fs		db 'fs.',0
vu_s_fatfs	db 'fatfs',0
vu_s_mounts	db 'SD0:/etc/mounts.txt',0
vu_s_blk	db 'blk.',0
vu_s_p1		db 'p1',0
vu_s_esp	db ' ',0
vu_moins	db '-',0
vu_t_fat32	db 'FAT32',0
vu_t_exfat	db 'exFAT',0
vu_t_ram	db 'RAM',0
vu_t_autre	db '?',0
vu_u_ko		db ' Ko',0
vu_u_mo		db ' Mo',0
vu_u_go		db ' Go',0
vu_u_to		db ' To',0
vu_e_occupe	db 'occup',0xC3,0xA9,' (des fichiers y sont ouverts)',0
vu_e_perm	db 'non permis sur ce volume',0
vu_e_inval	db 'invalide',0
vu_e_noent	db 'pas de tel volume, ou pas mont',0xC3,0xA9,0
vu_e_existe	db 'd',0xC3,0xA9,'j',0xC3,0xA0,' mont',0xC3,0xA9,0
vu_e_io		db 'erreur d',0x27,'entr',0xC3,0xA9,'e / sortie',0
vu_e_nomem	db 'plus de m',0xC3,0xA9,'moire',0
vu_e_delai	db 'le service ne r',0xC3,0xA9,'pond pas (pas de syst',0xC3,0xA8,'me de fichiers reconnu ?)',0
vu_e_autre	db 'erreur ',0
		align 4

macro volutil_data {
		align 8
vu_msg		rb MSG_TAILLE
vu_rep		rb MSG_TAILLE
vu_vol		rb 16
vu_aff		rb 20
vu_src		rb 32
vu_srv		rb 16
vu_tmp		rb 128
vu_mounts	rb VU_MOUNTS
}
