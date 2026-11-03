; ping - attend l'adresse DHCP, puis :
;   ping HOTE [N]	N pings (4 par defaut) vers un nom ou une adresse, et le bilan
;   ping -t HOTE	jusqu'a Ctrl+C
;   ping		l'essai du reseau : la passerelle, puis un nom resolu par DNS
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
	mov	x21,0				; hote
	mov	x22,4				; nombre
	mov	x23,1
	mov	x25,0				; -t
.arg:	ldr	x24,[x20,x23,lsl 3]
	cbz	x24,.net
	add	x23,x23,1
	ldrb	w0,[x24]
	cmp	w0,'-'
	b.ne	.val
	ldrb	w0,[x24,1]
	cmp	w0,'t'
	b.ne	.val
	ldrb	w0,[x24,2]
	cbnz	w0,.val
	mov	x25,1
	b	.arg
.val:	cbnz	x21,.nb
	mov	x21,x24
	b	.arg
.nb:	mov	x0,x24
	bl	parse_dec
	cbz	x0,.arg
	mov	x22,x0
	b	.arg
.net:	cbz	x25,.dhcp
	mov	x22,0				; (0 : sans fin)
.dhcp:	adr	x0,ni
	bl	wait_dhcp
	cbz	x0,.nonet
	cbnz	x21,.hote
	adr	x0,m_ip
	bl	lang_puts
	adr	x0,ni
	ldr	w0,[x0,NI_IP]
	bl	put_ip
	adr	x0,m_gw
	bl	lang_puts
	adr	x0,ni
	ldr	w0,[x0,NI_GW]
	bl	put_ip
	adr	x0,m_nl
	bl	puts

	adr	x0,ni				; la passerelle
	ldr	w0,[x0,NI_GW]
	mov	x1,4
	bl	ping_n

	adr	x0,m_res			; un nom
	bl	lang_puts
	adr	x0,host
	bl	puts
	adr	x0,m_arrow
	bl	puts
	adr	x0,host
	netcall	net_resolve
	tbnz	x0,63,.nores
	mov	w19,w0
	bl	put_ip
	adr	x0,m_nl
	bl	puts
	mov	w0,w19
	mov	x1,3
	bl	ping_n
	mov	x0,0
	sys	SYS_EXIT
.hote:	mov	x0,x21
	netcall	net_resolve
	tbnz	x0,63,.inconnu
	mov	w19,w0
	mov	x1,x22
	bl	ping_n
	mov	x19,x0				; le bilan
	adr	x0,m_bilan
	bl	lang_puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_sur
	bl	lang_puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_recus
	bl	lang_puts
	cmp	x19,0
	cset	x0,eq
	sys	SYS_EXIT
.nores:	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,2
	sys	SYS_EXIT
.inconnu:
	adr	x0,m_inconnu
	bl	lang_puts
	mov	x0,x21
	bl	puts
	adr	x0,m_nl
	bl	puts
	mov	x0,2
	sys	SYS_EXIT
.nonet:	adr	x0,m_nonet
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

; w0 = adresse, x1 = nombre de pings (0 : sans fin) -> x0 = reponses recues
ping_n:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	w19,w0
	mov	x20,x1
	mov	x21,0
.p:	adr	x0,m_ping
	bl	lang_puts
	mov	w0,w19
	bl	put_ip
	adr	x0,m_colon
	bl	lang_puts
	mov	w0,w19
	mov	x1,1000
	netcall	net_ping
	tbnz	x0,63,.err
	add	x21,x21,1
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	b	.next
.err:	bl	print_dec
	adr	x0,m_fail
	bl	lang_puts
.next:	mov	x0,300
	sys	SYS_SLEEP
	cbz	x20,.p
	subs	x20,x20,1
	b.ne	.p
	mov	x0,x21
	ldr	x21,[sp],16
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

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data

host		db 'example.com',0
m_ip		db 'ping : adresse ',0
m_gw		db '  passerelle ',0
m_res		db 'ping : r',0xC3,0xA9,'solution DNS ',0
m_arrow		db ' -> ',0
m_ping		db 'ping : ',0
m_colon		db ' : ',0
m_ms		db ' ms',10,0
m_bilan		db 'ping : ',0
m_sur		db ' sur ',0
m_recus		db ' re',0xC3,0xA7,'us',10,0
m_fail		db ' (erreur)',10,0
m_nonet		db 'ping : pas de r',0xC3,0xA9,'seau',10,0
m_inconnu	db 'ping : h',0xC3,0xB4,'te inconnu : ',0
m_nl		db 10,0
		align 8
ni		rb 32
