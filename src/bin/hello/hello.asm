; hello - premier process EL0 : affiche, dort, compte, se termine
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; puts, print_dec... (SD0:/lib/appkit.so)

start:
	mov	x20,x0				; ligne d'arguments (ou 0)
	bl	kits_open
	adr	x0,msg_hello
	bl	puts
	sys	SYS_GETPID
	bl	print_dec
	adr	x0,msg_nl
	bl	puts
	cbz	x20,.noargs
	adr	x0,msg_args
	bl	puts
	mov	x0,x20
	bl	puts
	adr	x0,msg_nl
	bl	puts
.noargs:
	mov	x19,1
.loop:	mov	x0,1000
	sys	SYS_SLEEP
	adr	x0,msg_tick
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,msg_nl
	bl	puts
	add	x19,x19,1
	cmp	x19,2
	b.ls	.loop

	mov	x0,0
	sys	SYS_EXIT

segment readable writeable
kit_data

msg_hello	db 'hello : bonjour depuis EL0 ! pid ',0
msg_tick	db 'hello : seconde ',0
msg_nl		db 10,0
msg_args	db 'hello : arguments = ',0
