; ultest - envoie 300 000 octets (motif (i*7)&255) a 10.0.2.2:9000 puis ferme
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/netkit/netkit.inc'

TOTAL	= 300000

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,ni
	bl	wait_dhcp
	cbz	x0,.fail
	mov	x0,SOCK_TCP
	netcall	net_socket
	tbnz	x0,63,.fail
	mov	x19,x0
	movl	w1,0x0A000202
	movl	x2,9000
	netcall	net_connect
	tbnz	x0,63,.fail
	sys	SYS_TIME_MS
	mov	x25,x0
	mov	x20,0				; octets envoyes
.blk:	adr	x1,buf				; bloc de 4000 octets
	mov	x2,0
.f:	add	x3,x20,x2
	mov	x4,7
	mul	x3,x3,x4
	strb	w3,[x1,x2]
	add	x2,x2,1
	cmp	x2,4000
	b.lo	.f
	mov	x0,x19
	adr	x1,buf
	movl	x3,TOTAL
	sub	x2,x3,x20
	cmp	x2,4000
	mov	x3,4000
	csel	x2,x3,x2,hi
	netcall	net_send
	cmp	x0,0
	b.le	.fail
	add	x20,x20,x0
	movl	x3,TOTAL
	cmp	x20,x3
	b.lo	.blk
	mov	x0,x19
	netcall	net_close
	sys	SYS_TIME_MS
	sub	x25,x0,x25
	adr	x0,m_ok
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_time
	bl	puts
	mov	x0,x25
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.fail:	bl	print_dec
	adr	x0,m_fail
	bl	puts
	mov	x0,1
	sys	SYS_EXIT


segment readable writeable
kit_data

m_ok		db 'ultest : envoy',0xC3,0xA9,' ',0
m_time		db ' octets en ',0
m_ms		db ' ms (mise en file)',10,0
m_fail		db ' : ultest echec',10,0
		align 8
ni		rb 32
buf		rb 4000
