; tcpbench - banc du reseau, sans disque dans la boucle : serveur TCP sur le port
; 5001, le client est tools/tcpbench.py. Le premier octet d'une connexion choisit :
;	S  (+ un octet : la duree en secondes) le Pi envoie, puis ferme
;	R  le Pi recoit et jette jusqu'a la fermeture
;	E  le Pi renvoie chaque octet (aller-retour)
;	I  le Pi envoie les compteurs de netd (NET_OP_STATS, NST_BYTES octets)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/netkit/netkit.inc'
include '../../inc/netd.inc'

TB_PORT		= 5001
TB_BUF		= 65536
TB_WAIT		= 30000			; ms

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	kits_open
	adr	x0,ni
	bl	wait_dhcp
	mov	x0,SOCK_TCP
	netcall	net_socket
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x1,TB_PORT
	netcall	net_bind
	tbnz	x0,63,.fail
	mov	x0,x19
	mov	x1,4
	netcall	net_listen
	tbnz	x0,63,.fail
	adr	x0,s_net
	movl	x1,5000
	sys	SYS_SVC_OPEN
	adr	x1,canal
	str	x0,[x1]
	adr	x0,m_listen
	bl	puts
.acc:	mov	x0,x19
	movl	x1,3600000
	netcall	net_accept
	tbnz	x0,63,.acc
	mov	x20,x0
	mov	x0,x20				; le choix
	adr	x1,buf
	mov	x2,1
	movl	x3,TB_WAIT
	netcall	net_recv
	cmp	x0,1
	b.ne	.close
	adr	x1,buf
	ldrb	w21,[x1]
	cmp	w21,'S'
	b.eq	.send
	cmp	w21,'R'
	b.eq	.recv
	cmp	w21,'I'
	b.eq	.info
.echo:	mov	x0,x20
	adr	x1,buf
	mov	x2,1024
	movl	x3,TB_WAIT
	netcall	net_recv
	cmp	x0,0
	b.le	.close
	mov	x2,x0
	mov	x0,x20
	adr	x1,buf
	netcall	net_send
	cmp	x0,0
	b.gt	.echo
	b	.close
.send:	mov	x0,x20				; (fin decidee ici : la fermeture du client
	adr	x1,buf				;  se voit par un RST, qui peut se perdre, et
	mov	x2,1				;  la connexion suivante attendrait)
	movl	x3,TB_WAIT
	netcall	net_recv
	cmp	x0,1
	b.ne	.close
	adr	x1,buf
	ldrb	w22,[x1]
	mov	x0,1000
	mul	x22,x22,x0
	sys	SYS_TIME_MS
	add	x22,x22,x0
.sl:	mov	x0,x20
	adr	x1,buf
	movl	x2,TB_BUF
	netcall	net_send
	cmp	x0,0
	b.le	.close
	sys	SYS_TIME_MS
	cmp	x0,x22
	b.lo	.sl
	b	.close
.recv:	mov	x0,x20
	adr	x1,buf
	movl	x2,TB_BUF
	movl	x3,TB_WAIT
	netcall	net_recv
	cmp	x0,0
	b.gt	.recv
	b	.close
.info:	adr	x1,msg
	mov	x0,NET_OP_STATS
	str	x0,[x1]
	adr	x0,canal
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
	mov	x0,x20
	adr	x1,msg+WM_DATA
	mov	x2,NST_BYTES
	netcall	net_send
.close:	mov	x0,x20
	netcall	net_close
	b	.acc
.fail:	adr	x0,m_fail
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

segment readable writeable
kit_data
		align 8
ni		rb 32
canal		dd 0
s_net		db 'net',0
		align 8
msg		rb 512
m_listen	db 'tcpbench : en ecoute sur le port 5001',10,0
m_fail		db 'tcpbench : reseau indisponible',10,0
		align 16
buf		rb TB_BUF
