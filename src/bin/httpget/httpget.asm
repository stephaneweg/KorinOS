; httpget - client HTTP/1.0 : GET http://<hote>/ (example.com, sinon un serveur
; local sur l'hote QEMU 10.0.2.2:8000), affiche la ligne d'etat et les en-tetes
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
	cbz	x0,.nonet
	adr	x0,host				; 1) example.com:80
	netcall	net_resolve
	tbnz	x0,63,.local
	mov	w1,w0
	mov	x2,80
	adr	x3,host
	bl	http_get
	cbz	x0,.ok
.local:	adr	x0,m_local			; 2) serveur local de l'hote
	bl	lang_puts
	movl	w1,0x0A000202
	movl	x2,8000
	adr	x3,host_local
	bl	http_get
	cbnz	x0,.fail
.ok:	mov	x0,0
	sys	SYS_EXIT
.fail:	mov	x0,1
	sys	SYS_EXIT
.nonet:	adr	x0,m_nonet
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

; w1 = IP, x2 = port, x3 = nom d'hote -> x0 = 0 si reponse recue
http_get:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	w20,w1
	mov	x21,x2
	mov	x22,x3
	adr	x0,m_conn
	bl	lang_puts
	mov	w0,w20
	bl	put_ip
	adr	x0,m_colon
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,SOCK_TCP
	netcall	net_socket
	tbnz	x0,63,.err
	mov	x19,x0
	mov	w1,w20
	mov	x2,x21
	netcall	net_connect
	tbnz	x0,63,.err_close
	adr	x0,m_connected
	bl	lang_puts
	adr	x0,req				; requete
	adr	x1,req1
	bl	str_cat
	mov	x1,x22
	bl	str_cat
	adr	x1,req2
	bl	str_cat
	adr	x1,req
	sub	x2,x0,x1
	mov	x0,x19
	netcall	net_send
	tbnz	x0,63,.err_close
	mov	x23,0				; octets recus
	mov	x24,0				; en-tetes deja affiches
.rx:	mov	x0,x19
	adr	x1,buf
	mov	x2,1024
	movl	x3,10000
	netcall	net_recv
	cmp	x0,0
	b.le	.end
	add	x23,x23,x0
	cbnz	x24,.rx
	mov	x24,1				; afficher la tete de la reponse (en-tetes)
	adr	x1,buf
	mov	x2,0				; position de la ligne vide (sinon : tout)
	sub	x5,x0,3
.hd:	cmp	x2,x5
	b.ge	.hd_all
	ldr	w3,[x1,x2]
	movl	w4,0x0A0D0A0D
	cmp	w3,w4
	b.eq	.hd_end
	add	x2,x2,1
	b	.hd
.hd_all:
	mov	x2,x0
.hd_end:
	strb	wzr,[x1,x2]
	adr	x0,m_hdr
	bl	lang_puts
	adr	x0,buf
	bl	puts
	adr	x0,m_nl
	bl	puts
	b	.rx
.end:	mov	x24,x0
	adr	x0,m_total
	bl	lang_puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_end
	bl	lang_puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,x19
	netcall	net_close
	mov	x0,1
	cbz	x23,.out
	mov	x0,0
	b	.out
.err_close:
	mov	x20,x0
	mov	x0,x19
	netcall	net_close
	mov	x0,x20
.err:	mov	x20,x0
	adr	x0,m_err
	bl	lang_puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue


; w0 = adresse IPv4 -> sortie standard (le print_ip de NetKit ecrit sur la
; console du noyau, donc dans le journal, pas dans le tube du terminal)
put_ip:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_ip
	mov	x0,sp
	bl	puts
	add	sp,sp,32
	epilogue

segment readable writeable
kit_data

host		db 'example.com',0
host_local	db '10.0.2.2',0
req1		db 'GET / HTTP/1.0',13,10,'Host: ',0
req2		db 13,10,'User-Agent: Korin-httpget/0.1',13,10,13,10,0
m_conn		db 'httpget : connexion ',0xC3,0xA0,' ',0
m_colon		db ':',0
m_connected	db 'httpget : connect',0xC3,0xA9,', envoi de la requ',0xC3,0xAA,'te',10,0
m_hdr		db 'httpget : en-t',0xC3,0xAA,'tes re',0xC3,0xA7,'us :',10,0
m_total		db 'httpget : total ',0
m_end		db ' octets, fin de lecture ',0
m_err		db 'httpget : erreur ',0
m_local		db 'httpget : essai du serveur local de l''h',0xC3,0xB4,'te',10,0
m_nonet		db 'httpget : pas de r',0xC3,0xA9,'seau',10,0
m_nl		db 10,0
		align 8
ni		rb 32
req		rb 512
buf		rb 1040
