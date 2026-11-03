; httpd - mini serveur HTTP sur le port 80 : repond une page a chaque requete
; (QEMU : hostfwd=tcp::8080-:80, puis http://127.0.0.1:8080/ depuis l'hote)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/netkit/netkit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,ni
	bl	wait_dhcp
	mov	x0,SOCK_TCP
	netcall	net_socket
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x1,80
	netcall	net_bind
	tbnz	x0,63,.fail
	mov	x0,x19
	mov	x1,4
	netcall	net_listen
	tbnz	x0,63,.fail
	adr	x0,m_listen
	bl	lang_puts
	mov	x21,0				; requetes servies
.acc:	mov	x0,x19
	movl	x1,3600000
	netcall	net_accept
	tbnz	x0,63,.acc
	mov	x20,x0
	mov	x22,0				; octets de la requete
.rq:	mov	x0,x20				; lire jusqu'a la ligne vide
	adr	x1,buf
	add	x1,x1,x22
	mov	x2,1000
	sub	x2,x2,x22
	mov	x3,3000
	netcall	net_recv
	cmp	x0,0
	b.le	.reply
	add	x22,x22,x0
	adr	x1,buf
	strb	wzr,[x1,x22]
	bl	has_blank_line
	cbnz	x0,.reply
	cmp	x22,1000
	b.lo	.rq
.reply:	add	x21,x21,1
	adr	x0,m_req			; journal : premiere ligne de la requete
	bl	lang_puts
	adr	x0,buf
	mov	x1,0
.ln:	ldrb	w2,[x0,x1]
	cbz	w2,.ln_end
	cmp	w2,13
	b.eq	.ln_end
	add	x1,x1,1
	b	.ln
.ln_end:
	strb	wzr,[x0,x1]
	bl	puts
	adr	x0,m_nl
	bl	puts
	adr	x0,page				; reponse
	adr	x1,resp1
	bl	str_cat
	mov	x22,x0
	adr	x0,resp_page			; (le texte de la page, traduit)
	bl	lang_tr
	mov	x1,x0
	mov	x0,x22
	bl	str_cat
	mov	x1,x0
	mov	x0,x21
	bl	fmt_dec
	adr	x1,resp2
	bl	str_cat
	adr	x1,page
	sub	x2,x0,x1
	mov	x0,x20
	netcall	net_send
	mov	x0,x20
	netcall	net_close
	b	.acc
.fail:	adr	x0,m_fail
	bl	lang_puts
	bl	print_dec
	mov	x0,1
	sys	SYS_EXIT

; -> x0 = 1 si buf contient "\r\n\r\n"
has_blank_line:
	adr	x1,buf
.c:	ldrb	w2,[x1]
	cbz	w2,.no
	cmp	w2,13
	b.ne	.n
	ldrb	w2,[x1,1]
	cmp	w2,10
	b.ne	.n
	ldrb	w2,[x1,2]
	cmp	w2,13
	b.ne	.n
	ldrb	w2,[x1,3]
	cmp	w2,10
	b.ne	.n
	mov	x0,1
	ret
.n:	add	x1,x1,1
	b	.c
.no:	mov	x0,0
	ret


segment readable writeable
kit_data

m_listen	db 'httpd : en ',0xC3,0xA9,'coute sur le port 80',10,0
m_req		db 'httpd : requ',0xC3,0xAA,'te ',0
m_fail		db 'httpd : erreur ',0
m_nl		db 10,0
resp1		db 'HTTP/1.0 200 OK',13,10
		db 'Content-Type: text/html; charset=utf-8',13,10
		db 'Connection: close',13,10,13,10,0
resp_page	db '<html><body><h1>Bonjour depuis Korin OS</h1>'
		db '<p>Serveur HTTP en assembleur AArch64, pile TCP/IP maison.</p>'
		db '<p>Requ',0xC3,0xAA,'te num',0xC3,0xA9,'ro ',0
resp2		db '</p></body></html>',13,10,0
		align 8
ni		rb 32
buf		rb 1040
page		rb 1024
