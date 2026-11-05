; eject - une cle USB prete a etre retiree (docs/OUTILS.md ; d'apres eject d'Onyx) :
;   eject [-f] [USB0: | USB1: ...]
; Sans volume : la seule cle USB montee. Les entrees de ses fichiers ouverts sont d'abord ecrites
; (FS_SYNC de fatfs) ; s'il en reste d'ouverts, rien n'est fait, sauf avec -f (les appels suivants
; des programmes sur ces fichiers echouent). Puis vfs le demonte (son service fatfs s'arrete) :
; « mount USB0: » le remonte tant que la cle est branchee. Seules les cles USB s'ejectent : jamais
; la carte (SD0:, SD1:...).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,0				; -f
	mov	x22,0				; volume donne
	mov	x23,1
.arg:	cmp	x23,x19
	b.hs	.go
	ldr	x24,[x20,x23,lsl 3]
	add	x23,x23,1
	ldrb	w0,[x24]
	cmp	w0,'-'
	b.ne	.vol
	ldrb	w0,[x24,1]
	cmp	w0,'f'
	b.ne	.usage
	ldrb	w0,[x24,2]
	cbnz	w0,.usage
	mov	x21,1
	b	.arg
.vol:	cbnz	x22,.usage
	mov	x22,x24
	b	.arg
.go:	cbz	x22,.seul
	mov	x0,x22
	bl	vu_nom
	cbz	x0,.usage
	adr	x0,vu_vol
	bl	vu_trouver
	cbz	x0,.pas_monte
	b	.trouve
.seul:	bl	vu_liste			; la seule cle USB montee
	cmp	x0,0
	b.le	.aucune
	mov	x23,x0
	adr	x24,vu_msg
	add	x24,x24,MSG_DATA
	mov	x25,0				; trouvee
.s:	cbz	x23,.sf
	add	x0,x24,VU_SRC
	bl	est_usb
	cbz	x0,.sn
	cbnz	x25,.plusieurs
	mov	x25,x24
.sn:	add	x24,x24,VU_ENT
	sub	x23,x23,1
	b	.s
.sf:	cbz	x25,.aucune
	mov	x0,x25
	bl	vu_nom
	mov	x0,x25
.trouve:
	add	x0,x0,VU_SRC
	bl	est_usb
	cbz	x0,.pas_amovible
	adr	x0,vu_vol			; ses fichiers ouverts
	bl	vu_sync
	tbnz	x0,63,.demonte			; (service sans FS_SYNC : on demonte)
	cbz	x0,.demonte
	mov	x24,x0
	cbnz	x21,.force
	adr	x0,vu_aff
	bl	t_err
	adr	x0,m_occupe
	bl	t_derr
	mov	x0,x24
	bl	put_err_dec
	adr	x0,m_ouverts
	bl	t_derr
	mov	x0,1
	bl	t_fin
.force:	adr	x0,m_force
	bl	t_derr
.demonte:
	adr	x0,vu_vol
	bl	vu_demonter
	tbnz	x0,63,.echec
	adr	x0,vu_aff
	bl	t_puts
	adr	x0,m_retirer
	bl	t_dire
	mov	x0,0
	bl	t_fin
.echec:	mov	x24,x0
	adr	x0,vu_aff
	bl	t_err
	adr	x0,m_pas_ejecte
	bl	t_derr
	mov	x0,x24
	bl	vu_err
	adr	x0,m_nl
	bl	t_err
	mov	x0,1
	bl	t_fin
.pas_monte:
	adr	x0,vu_aff
	bl	t_err
	adr	x0,m_pas_monte
	bl	t_derr
	mov	x0,1
	bl	t_fin
.pas_amovible:
	adr	x0,vu_aff
	bl	t_err
	adr	x0,m_pas_amovible
	bl	t_derr
	mov	x0,1
	bl	t_fin
.plusieurs:
	adr	x0,m_plusieurs
	bl	t_derr
	mov	x0,2
	bl	t_fin
.aucune:
	adr	x0,m_aucune
	bl	t_derr
	mov	x0,1
	bl	t_fin
.usage:	adr	x0,m_usage
	bl	t_derr
	mov	x0,2
	bl	t_fin

; x0 = source -> x0 = 1 si c'est une cle USB (« blk.usb... »)
est_usb:
	adr	x1,s_blk_usb
.c:	ldrb	w2,[x1],1
	cbz	w2,.oui
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	.c
	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; x0 = valeur : en decimal sur la sortie d'erreur
put_err_dec:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	bl	t_err
	add	sp,sp,32
	epilogue

include '../../inc/tool.inc'
include '../../inc/volutil.inc'

m_usage		db 'usage : eject [-f] [USB0: | USB1: ...]',10,0
m_aucune	db 'eject : aucune cl',0xC3,0xA9,' USB n',0x27,'est mont',0xC3,0xA9,'e',10,0
m_plusieurs	db 'eject : plusieurs cl',0xC3,0xA9,'s USB sont mont',0xC3,0xA9,'es : laquelle ? (eject USB1:)',10,0
m_pas_monte	db ' n',0x27,'est pas mont',0xC3,0xA9,10,0
m_pas_amovible	db ' n',0x27,'est pas une cl',0xC3,0xA9,' USB : il ne s',0x27,0xC3,0xA9,'jecte pas',10,0
m_occupe	db ' pas ',0xC3,0xA9,'ject',0xC3,0xA9,' : fichiers ouverts : ',0
m_ouverts	db 10,'(leurs entr',0xC3,0xA9,'es sont ',0xC3,0xA9,'crites ; fermez-les, ou eject -f)',10,0
m_force		db 'eject : des fichiers sont encore ouverts : ',0xC3,0xA9,'ject',0xC3,0xA9,' quand m',0xC3,0xAA,'me (-f)',10,0
m_retirer	db ' peut ',0xC3,0xAA,'tre retir',0xC3,0xA9,' sans risque',10,0
m_pas_ejecte	db ' pas ',0xC3,0xA9,'ject',0xC3,0xA9,' : ',0
m_nl		db 10,0
s_blk_usb	db 'blk.usb',0
		align 4

segment readable writeable
kit_data
tool_data
volutil_data
