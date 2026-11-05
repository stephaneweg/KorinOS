; nslookup - l'adresse d'un nom, par le serveur DNS de netd (docs/OUTILS.md ; d'apres nslookup
; d'Onyx) : nslookup NOM. Le serveur est celui du bail DHCP (une adresse donnee en nom est
; rendue telle quelle, « localhost » : 127.0.0.1) ; pas encore d'autre serveur ni de nom inverse.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../kits/netkit/netkit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	t_debut
	cmp	x19,2
	b.ne	.usage
	ldr	x21,[x20,8]			; le nom
	ldrb	w0,[x21]
	cmp	w0,'-'
	b.eq	.usage
	bl	nt_reseau
	cbz	x0,.nonet
	sub	sp,sp,48
	mov	x0,sp
	bl	net_info
	adr	x0,m_serveur
	bl	dire_col
	ldr	w0,[sp,NI_DNS]
	cbz	w0,.sans
	bl	nt_put_ip
	b	.s1
.sans:	adr	x0,m_aucun
	bl	t_dire
.s1:	bl	t_nl
	add	sp,sp,48
	mov	x0,x21
	bl	net_resolve
	tbnz	x0,63,.echec
	mov	x22,x0
	adr	x0,m_nom
	bl	dire_col
	mov	x0,x21
	bl	t_puts
	bl	t_nl
	adr	x0,m_adresse
	bl	dire_col
	mov	w0,w22
	bl	nt_put_ip
	bl	t_nl
	mov	x0,0
	bl	t_fin
.echec:	mov	x22,x0
	adr	x0,m_introuvable
	bl	t_derr
	mov	x0,x21
	bl	t_err
	adr	x0,m_paren
	bl	t_err
	mov	x0,x22
	bl	nt_err_texte
	adr	x0,m_paren2
	bl	t_err
	mov	x0,1
	bl	t_fin
.nonet:	adr	x0,m_nonet
	bl	t_derr
	mov	x0,1
	bl	t_fin
.usage:	adr	x0,m_usage
	bl	t_derr
	mov	x0,2
	bl	t_fin

; x0 = texte : traduit, puis des espaces jusqu'a la colonne 12
dire_col:
	prologue
	str	x19,[sp,-16]!
	bl	lang_tr
	mov	x19,x0
	bl	t_puts
	mov	x0,x19
	bl	str_len
	mov	x19,x0
.p:	cmp	x19,12
	b.hs	.out
	mov	w0,' '
	bl	t_putc
	add	x19,x19,1
	b	.p
.out:	ldr	x19,[sp],16
	epilogue

include '../../inc/tool.inc'
include '../../inc/nettool.inc'

m_usage		db 'usage : nslookup NOM',10,0
m_nonet		db 'nslookup : pas de r',0xC3,0xA9,'seau',10,0
m_serveur	db 'Serveur :',0
m_aucun		db '(aucun)',0
m_nom		db 'Nom :',0
m_adresse	db 'Adresse :',0
m_introuvable	db '** introuvable : ',0
m_paren		db ' (',0
m_paren2	db ')',10,0
		align 4

segment readable writeable
kit_data
tool_data
nettool_data
