; mount - les volumes (docs/OUTILS.md ; d'apres mount d'Onyx) :
;   mount                       chaque volume monte : nom, systeme de fichiers, taille, place
;                               libre, etiquette, genre, puis sa source et son service
;   mount VOLUME:               le remonter (une cle USB ejectee mais encore branchee, un volume
;                               de SD0:/etc/mounts.txt) : sa source vient de mounts.txt, sinon
;                               USBn: -> blk.usbNp1 (ou blk.usbN), service fatfs
;   mount VOLUME: SOURCE [SERVICE]   monter une source donnee (« mount IMG2: blk.img2p1 »)
; Les cles USB se montent d'elles-memes au branchement (USB0:, USB1:...) ; eject les retire.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	cmp	x19,1
	b.eq	.liste
	cmp	x19,4
	b.hi	.usage
	ldr	x0,[x20,8]
	ldrb	w1,[x0]
	cmp	w1,'-'
	b.eq	.usage
	bl	vu_nom
	cbz	x0,.usage
	adr	x0,vu_vol
	bl	vu_trouver
	cbnz	x0,.deja
	cmp	x19,2
	b.eq	.devine
	ldr	x1,[x20,16]			; source donnee
	adr	x0,vu_src
	bl	str_cat
	adr	x0,vu_srv
	adr	x1,vu_s_fatfs
	cmp	x19,4
	b.lo	.srv
	ldr	x1,[x20,24]
.srv:	bl	str_cat
	b	.monte
.devine:
	adr	x0,vu_vol
	bl	vu_source
	cbz	x0,.inconnu
.monte:	adr	x0,vu_aff
	adr	x1,vu_src
	adr	x2,vu_srv
	bl	vu_monter
	mov	x21,x0
	tbnz	x21,63,.err
	adr	x0,vu_aff
	bl	t_puts
	adr	x0,m_monte
	bl	t_dire
	adr	x0,vu_src
	bl	t_puts
	mov	w0,' '
	bl	t_putc
	adr	x0,vu_srv
	bl	t_puts
	mov	w0,')'
	bl	t_putc
	bl	t_nl
	mov	x0,0
	bl	t_fin
.err:	adr	x0,vu_aff
	bl	t_err
	adr	x0,m_pas_monte
	bl	t_derr
	mov	x0,x21
	bl	vu_err
	adr	x0,m_nl
	bl	t_err
	mov	x0,1
	bl	t_fin
.deja:	adr	x0,vu_aff
	bl	t_puts
	adr	x0,m_deja
	bl	t_dire
	mov	x0,0
	bl	t_fin
.inconnu:
	adr	x0,vu_aff
	bl	t_err
	adr	x0,m_inconnu
	bl	t_derr
	mov	x0,1
	bl	t_fin
.liste:	bl	liste
	mov	x0,0
	bl	t_fin
.usage:	adr	x0,m_usage
	bl	t_derr
	mov	x0,2
	bl	t_fin

; chaque volume monte, une ligne
liste:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	vu_liste
	cmp	x0,0
	b.gt	.go
	adr	x0,m_aucun
	bl	t_dire
	b	.out
.go:	mov	x22,x0				; (vu_msg change avec vu_info :
	adr	x0,copie			; la liste est copiee)
	adr	x1,vu_msg
	add	x1,x1,MSG_DATA
	mov	x2,VU_MAX * VU_ENT
	bl	mem_move
	adr	x23,copie
.e:	cbz	x22,.out
	mov	x0,x23				; « USB0: »
	bl	vu_nom
	adr	x0,vu_aff
	mov	x1,8
	bl	t_col
	mov	x0,x23
	bl	vu_info
	tbnz	x0,63,.sans
	adr	x24,vu_rep
	ldr	x0,[x24,MSG_D]
	bl	vu_type_nom
	mov	x1,7
	bl	t_col
	ldr	x0,[x24,MSG_A]
	bl	vu_taille
	ldr	x0,[x24,MSG_B]
	tbnz	x0,63,.lab
	adr	x0,m_libre
	bl	t_dire
	ldr	x0,[x24,MSG_B]
	bl	vu_taille
.lab:	ldrb	w0,[x24,MSG_DATA]
	cbz	w0,.genre
	adr	x0,m_guill1
	bl	t_puts
	add	x0,x24,MSG_DATA
	strb	wzr,[x0,63]
	bl	t_puts
	adr	x0,m_guill2
	bl	t_puts
	b	.genre
.sans:	adr	x0,m_sans_info
	bl	t_dire
.genre:	ldr	w0,[x23]			; « sd0 » : le systeme
	movl	w1,0x00306473
	cmp	w0,w1
	b.ne	.g1
	adr	x0,m_systeme
	bl	t_dire
.g1:	add	x0,x23,VU_SRC
	adr	x1,vu_s_blk_usb
	bl	prefixe
	cbz	x0,.g2
	adr	x0,m_amovible
	bl	t_dire
.g2:	add	x0,x23,VU_SRC
	adr	x1,vu_s_ram
	bl	prefixe
	cbz	x0,.src
	adr	x0,m_memoire
	bl	t_dire
.src:	adr	x0,m_paren
	bl	t_puts
	add	x0,x23,VU_SRC
	bl	t_puts
	mov	w0,' '
	bl	t_putc
	add	x0,x23,VU_FS
	bl	t_puts
	mov	w0,')'
	bl	t_putc
	bl	t_nl
	add	x23,x23,VU_ENT
	sub	x22,x22,1
	b	.e
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = texte, x1 = prefixe -> x0 = 1 si le texte commence par lui
prefixe:
	ldrb	w2,[x1],1
	cbz	w2,.oui
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	prefixe
	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; x0 = texte (brut), x1 = largeur : le texte, puis des espaces jusqu'a la largeur (une au moins)
t_col:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	t_puts
	mov	x0,x19
	bl	str_len
	mov	x19,x0
.p:	mov	w0,' '
	bl	t_putc
	add	x19,x19,1
	cmp	x19,x20
	b.lo	.p
	ldp	x19,x20,[sp],16
	epilogue

include '../../inc/tool.inc'
include '../../inc/volutil.inc'

m_usage		db 'usage : mount [VOLUME: [source [service]]]',10,0
m_aucun		db '(aucun volume mont',0xC3,0xA9,')',10,0
m_monte		db ' mont',0xC3,0xA9,' (',0
m_pas_monte	db ' pas mont',0xC3,0xA9,' : ',0
m_deja		db ' est d',0xC3,0xA9,'j',0xC3,0xA0,' mont',0xC3,0xA9,10,0
m_inconnu	db ' : volume inconnu (ni dans SD0:/etc/mounts.txt, ni une cl',0xC3,0xA9,' USB branch',0xC3,0xA9,'e)',10,0
m_nl		db 10,0
m_libre		db ', libre ',0
m_guill1	db ', « ',0
m_guill2	db ' »',0
m_sans_info	db '(le service ne r',0xC3,0xA9,'pond pas)',0
m_systeme	db ', syst',0xC3,0xA8,'me',0
m_amovible	db ', amovible',0
m_memoire	db ', en m',0xC3,0xA9,'moire',0
m_paren		db '   (',0
vu_s_blk_usb	db 'blk.usb',0
vu_s_ram	db 'ram',0
		align 4

segment readable writeable
kit_data
tool_data
volutil_data
		align 8
copie		rb VU_MAX * VU_ENT
