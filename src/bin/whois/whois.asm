; whois - la base WHOIS (TCP, port 43) (docs/OUTILS.md ; d'apres whois d'Onyx) :
;   whois [-h SERVEUR] [-p PORT] DOMAINE [SERVEUR]
; Sans serveur : whois.iana.org d'abord, puis le serveur de son « refer: » (celui du registre du
; domaine, whois.verisign-grs.com pour .com). -p : un autre port que 43 (essais).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../kits/netkit/netkit.inc'

REP_MAX		= 65536

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	t_debut
	mov	x21,0				; domaine
	mov	x22,0				; serveur
	mov	x23,43				; port
	mov	x24,1
.arg:	cmp	x24,x19
	b.hs	.go
	ldr	x25,[x20,x24,lsl 3]
	add	x24,x24,1
	ldrb	w0,[x25]
	cmp	w0,'-'
	b.ne	.mot
	ldrb	w1,[x25,2]
	cbnz	w1,.usage
	ldrb	w0,[x25,1]
	cmp	x24,x19				; une valeur apres l'option
	b.hs	.usage
	ldr	x1,[x20,x24,lsl 3]
	add	x24,x24,1
	cmp	w0,'h'
	b.ne	.op
	mov	x22,x1
	b	.arg
.op:	cmp	w0,'p'
	b.ne	.usage
	mov	x0,x1
	bl	parse_dec
	cbz	x0,.usage
	mov	x23,x0
	b	.arg
.mot:	cbnz	x21,.serv
	mov	x21,x25
	b	.arg
.serv:	cbnz	x22,.usage
	mov	x22,x25
	b	.arg
.go:	cbz	x21,.usage
	bl	nt_reseau
	cbz	x0,.nonet
	cbnz	x22,.direct
	adr	x22,s_iana			; 1) l'IANA
	mov	x0,x22
	bl	titre
	mov	x0,x22
	bl	question
	tbnz	x0,63,.injoignable
	bl	chercher_refer
	cbnz	x0,.refer
	bl	afficher			; l'IANA tient la reponse
	b	.ok
.refer:	adr	x22,serveur			; 2) le registre
.direct:
	mov	x0,x22
	bl	titre
	mov	x0,x22
	bl	question
	tbnz	x0,63,.injoignable
	bl	afficher
.ok:	mov	x0,0
	bl	t_fin
.injoignable:
	mov	x25,x0
	adr	x0,m_injoignable
	bl	t_derr
	mov	x0,x22
	bl	t_err
	adr	x0,m_deux_points
	bl	t_err
	mov	x0,x25
	bl	nt_err_texte
	adr	x0,m_nl
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

; x0 = serveur : « [serveur] »
titre:
	prologue
	str	x0,[sp,-16]!
	mov	w0,'['
	bl	t_putc
	ldr	x0,[sp],16
	bl	t_puts
	mov	w0,']'
	bl	t_putc
	bl	t_nl
	bl	t_flush
	epilogue

; x0 = serveur : « domaine CR LF » au port x23, toute la reponse dans rep -> x0 = octets, < 0
question:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x1,x23
	bl	nt_tcp
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x1,x21
	bl	nt_envoyer
	mov	x0,x19
	adr	x1,s_crlf
	bl	nt_envoyer
	mov	x20,0				; octets recus
.r:	movl	x2,REP_MAX-1
	subs	x2,x2,x20
	b.ls	.f
	mov	x0,x19
	adr	x1,rep
	add	x1,x1,x20
	movl	x3,10000			; 10 s de silence : la fin
	bl	net_recv
	cmp	x0,0
	b.le	.f
	add	x20,x20,x0
	b	.r
.f:	mov	x0,x19
	bl	net_close
	adr	x0,rep
	strb	wzr,[x0,x20]
	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; la ligne « refer: » ou « whois: » de la reponse -> serveur, x0 = 1 si trouvee
chercher_refer:
	prologue
	adr	x0,rep
.l:	ldrb	w1,[x0]
	cbz	w1,.non
	adr	x2,s_refer			; « refer: »
	bl	.prefixe
	cbnz	x3,.val
	adr	x2,s_whois			; « whois: »
	bl	.prefixe
	cbnz	x3,.val
.saut:	ldrb	w1,[x0],1			; ligne suivante
	cbz	w1,.non
	cmp	w1,10
	b.ne	.saut
	b	.l
.val:	ldrb	w1,[x3]				; blancs
	cmp	w1,' '
	b.eq	.b
	cmp	w1,9
	b.ne	.v
.b:	add	x3,x3,1
	b	.val
.v:	adr	x4,serveur
	mov	x5,0
.c:	ldrb	w1,[x3,x5]
	cmp	w1,' '
	b.ls	.cf				; (fin de ligne, CR, blanc, 0)
	strb	w1,[x4,x5]
	add	x5,x5,1
	cmp	x5,95
	b.lo	.c
.cf:	strb	wzr,[x4,x5]
	cbz	x5,.saut
	mov	x0,1
	epilogue
.non:	mov	x0,0
	epilogue
; (interne) x0 = ligne, x2 = prefixe -> x3 = apres lui, 0 s'il n'y est pas (x1, x4, x5 changes)
.prefixe:
	mov	x4,0
.pc:	ldrb	w5,[x2,x4]
	cbz	w5,.oui
	ldrb	w1,[x0,x4]
	cmp	w1,w5
	b.ne	.pas
	add	x4,x4,1
	b	.pc
.oui:	add	x3,x0,x4
	ret
.pas:	mov	x3,0
	ret

; la reponse sur la sortie, sans les CR
afficher:
	prologue
	str	x19,[sp,-16]!
	adr	x19,rep
.c:	ldrb	w0,[x19],1
	cbz	w0,.f
	cmp	w0,13
	b.eq	.c
	bl	t_putc
	b	.c
.f:	bl	t_nl
	ldr	x19,[sp],16
	epilogue

include '../../inc/tool.inc'
include '../../inc/nettool.inc'

m_usage		db 'usage : whois [-h serveur] [-p port] domaine [serveur]',10,0
m_nonet		db 'whois : pas de r',0xC3,0xA9,'seau',10,0
m_injoignable	db 'whois : injoignable : ',0
m_deux_points	db ' : ',0
m_nl		db 10,0
s_iana		db 'whois.iana.org',0
s_crlf		db 13,10,0
s_refer		db 'refer:',0
s_whois		db 'whois:',0
		align 4

segment readable writeable
kit_data
tool_data
nettool_data
		align 8
serveur		rb 96
rep		rb REP_MAX
