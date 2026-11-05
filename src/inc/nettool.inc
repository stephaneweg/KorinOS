; nettool.inc - ce que partagent les commandes reseau de SD0:/bin (wget, whois, ftp, nslookup,
; netstat ; docs/OUTILS.md) : le reseau pret, une connexion TCP a un nom, l'envoi d'une chaine,
; une reponse lue par lignes, une adresse et une erreur en texte. Sur tool.inc (t_*) et NetKit.
;
;	include '../../inc/tool.inc'
;	include '../../inc/nettool.inc'
;	segment readable writeable	...	tool_data	nettool_data

NT_LIGNE	= 1024			; une ligne de reponse (FTP, HTTP, WHOIS)
NT_RX		= 4096			; tampon de reception d'une connexion

E_NT_CONNREFUSED = -111
E_NT_HOSTUNREACH = -113
E_NT_TIMEDOUT	= -110
E_NT_CONNRESET	= -104

; -> x0 = 1 si le reseau a une adresse (attend le DHCP, 20 s au plus)
nt_reseau:
	prologue
	sub	sp,sp,48
	mov	x0,sp
	bl	net_info
	ldr	w0,[sp,NI_IP]
	cbnz	w0,.ok
	mov	x0,sp
	bl	wait_dhcp
	b	.out
.ok:	mov	x0,1
.out:	add	sp,sp,48
	epilogue

; x0 = nom ou « a.b.c.d », x1 = port -> x0 = socket TCP connecte, < 0 si erreur ; w1 = adresse
nt_tcp:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	bl	net_resolve
	tbnz	x0,63,.out
	mov	w21,w0
	mov	x0,SOCK_TCP
	bl	net_socket
	tbnz	x0,63,.out
	mov	x19,x0
	mov	w1,w21
	mov	x2,x20
	bl	net_connect
	tbnz	x0,63,.ferme
	mov	x0,x19
	mov	w1,w21
	b	.out
.ferme:	mov	x20,x0
	mov	x0,x19
	bl	net_close
	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = adresse IPv4, x1 = port -> x0 = socket TCP connecte, < 0 si erreur
nt_tcp_ip:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w21,w0
	mov	x20,x1
	mov	x0,SOCK_TCP
	bl	net_socket
	tbnz	x0,63,.out
	mov	x19,x0
	mov	w1,w21
	mov	x2,x20
	bl	net_connect
	tbnz	x0,63,.ferme
	mov	x0,x19
	b	.out
.ferme:	mov	x20,x0
	mov	x0,x19
	bl	net_close
	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = socket, x1 = chaine -> x0 = octets envoyes, < 0 si erreur
nt_envoyer:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x1
	bl	str_len
	mov	x2,x0
	mov	x0,x19
	mov	x1,x20
	bl	net_send
	ldp	x19,x20,[sp],16
	epilogue

; x0 = socket : une nouvelle lecture par lignes (nt_ligne)
nt_lire_debut:
	adr	x1,nt_sd
	str	x0,[x1]
	adr	x1,nt_rx_n
	str	xzr,[x1]
	adr	x1,nt_rx_i
	str	xzr,[x1]
	ret

; -> w0 = octet suivant de la connexion, -1 a la fin (fermee, erreur ou 30 s sans rien)
nt_getc:
	prologue
	adr	x2,nt_rx_i
	ldr	x3,[x2]
	adr	x4,nt_rx_n
	ldr	x5,[x4]
	cmp	x3,x5
	b.lo	.o
	adr	x0,nt_sd
	ldr	x0,[x0]
	adr	x1,nt_rx
	mov	x2,NT_RX
	movl	x3,30000
	bl	net_recv
	cmp	x0,0
	b.le	.fin
	adr	x4,nt_rx_n
	str	x0,[x4]
	mov	x3,0
	adr	x2,nt_rx_i
.o:	adr	x1,nt_rx
	ldrb	w0,[x1,x3]
	add	x3,x3,1
	str	x3,[x2]
	b	.out
.fin:	mov	x0,-1
.out:	epilogue

; -> x0 = ligne (sans CR LF, terminee par 0), 0 a la fin de la connexion
nt_ligne:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
	adr	x20,nt_l
.c:	bl	nt_getc
	tbnz	x0,63,.fin
	cmp	w0,10
	b.eq	.ok
	cmp	w0,13
	b.eq	.c
	cmp	x19,NT_LIGNE-2
	b.hs	.c
	strb	w0,[x20,x19]
	add	x19,x19,1
	b	.c
.fin:	cbnz	x19,.ok
	mov	x0,0
	b	.out
.ok:	strb	wzr,[x20,x19]
	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

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

; w0 = adresse IPv4 : sur la sortie
nt_put_ip:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_ip
	mov	x0,sp
	bl	t_puts
	add	sp,sp,32
	epilogue

; x0 = code d'erreur (< 0) : son texte, traduit, sur la sortie d'erreur (sans fin de ligne)
nt_err_texte:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,s_nt_inconnu
	cmn	x19,2				; E_NOENT : le DNS ne connait pas ce nom
	b.eq	.dit
	adr	x0,s_nt_refus
	cmn	x19,111
	b.eq	.dit
	adr	x0,s_nt_injoignable
	cmn	x19,113
	b.eq	.dit
	adr	x0,s_nt_delai
	cmn	x19,110
	b.eq	.dit
	adr	x0,s_nt_coupe
	cmn	x19,104
	b.eq	.dit
	adr	x0,s_nt_erreur
	bl	t_derr
	sub	sp,sp,32
	neg	x0,x19
	mov	x1,sp
	bl	fmt_dec
	adr	x0,s_nt_moins			; (le code, negatif)
	bl	t_err
	mov	x0,sp
	bl	t_err
	add	sp,sp,32
	b	.out
.dit:	bl	t_derr
.out:	ldr	x19,[sp],16
	epilogue

s_nt_inconnu	db 'nom inconnu',0
s_nt_refus	db 'connexion refus',0xC3,0xA9,'e',0
s_nt_injoignable db 'h',0xC3,0xB4,'te injoignable',0
s_nt_delai	db 'pas de r',0xC3,0xA9,'ponse (d',0xC3,0xA9,'lai d',0xC3,0xA9,'pass',0xC3,0xA9,')',0
s_nt_coupe	db 'connexion coup',0xC3,0xA9,'e',0
s_nt_erreur	db 'erreur ',0
s_nt_moins	db '-',0
		align 4

macro nettool_data {
		align 8
nt_sd		rb 8
nt_rx_n		rb 8
nt_rx_i		rb 8
nt_rx		rb NT_RX
nt_l		rb NT_LIGNE
}
