; crash - teste l'isolation : pointeur noyau refuse par un appel systeme,
; puis lecture d'une adresse invalide -> le process est tue, pas le noyau
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,msg_start
	bl	puts
	mov	x0,0x80000			; adresse du noyau
	mov	x1,16
	sys	SYS_PRINT
	mov	x19,x0
	adr	x0,msg_ret
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,msg_nl
	bl	puts

	mov	x0,2000
	sys	SYS_SLEEP
	adr	x0,msg_boom
	bl	puts
	mov	x0,0x80000
	ldr	x1,[x0]				; lecture interdite en EL0 -> faute
	adr	x0,msg_alive			; jamais atteint
	bl	puts
	mov	x0,0
	sys	SYS_EXIT


segment readable writeable
kit_data

msg_start	db 'crash : print(pointeur noyau) -> ',0
msg_ret		db 'retour ',0
msg_boom	db 'crash : lecture de la memoire du noyau...',10,0
msg_alive	db 'crash : ERREUR, toujours vivant',10,0
msg_nl		db 10,0
