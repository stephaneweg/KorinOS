; dltest - telecharge http://10.0.2.2:8000/big.bin (serveur de l'hote QEMU),
; compte les octets du corps et leur somme, mesure le debit
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
	cbz	x0,.fail
	mov	x0,SOCK_TCP
	netcall	net_socket
	tbnz	x0,63,.fail
	mov	x19,x0
	movl	w1,0x0A000202
	movl	x2,8000
	netcall	net_connect
	tbnz	x0,63,.fail
	sys	SYS_TIME_MS
	mov	x25,x0
	mov	x0,x19
	adr	x1,req
	mov	x2,req_len
	netcall	net_send
	mov	x20,0				; octets du corps
	mov	x21,0				; somme
	mov	x22,0				; 1 : en-tetes passes
	mov	w23,0				; 4 derniers octets (detection de \r\n\r\n)
.rx:	mov	x0,x19
	adr	x1,buf
	mov	x2,4096
	movl	x3,10000
	netcall	net_recv
	cmp	x0,0
	b.le	.end
	adr	x1,buf
	mov	x2,0
.b:	ldrb	w3,[x1,x2]
	cbz	x22,.hdr
	add	x20,x20,1
	add	x21,x21,x3
	b	.nb
.hdr:	orr	w23,w3,w23,lsl 8
	movl	w4,0x0D0A0D0A
	cmp	w23,w4
	cset	x22,eq
.nb:	add	x2,x2,1
	cmp	x2,x0
	b.lo	.b
	b	.rx
.end:	sys	SYS_TIME_MS
	sub	x25,x0,x25
	mov	x0,x19
	netcall	net_close
	adr	x0,m_res
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_sum
	bl	puts
	mov	x0,x21
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

req		db 'GET /big.bin HTTP/1.0',13,10,'Host: 10.0.2.2',13,10,13,10
req_len		= $ - req
m_res		db 'dltest : corps ',0
m_sum		db ' octets, somme ',0
m_time		db ', ',0
m_ms		db ' ms',10,0
m_fail		db ' : dltest echec',10,0
		align 8
ni		rb 32
buf		rb 4100
