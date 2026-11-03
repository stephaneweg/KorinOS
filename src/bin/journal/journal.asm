; journal - copie le journal du noyau dans SD0:/journal.txt (diagnostic sur
; materiel reel, sans liaison serie). Lance depuis SD0:/etc/startup.txt :
; ecrit apres 20 s, puis de nouveau apres 60 s (messages USB tardifs).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

LOG_MAX		= 65536

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	mov	x0,20000
	sys	SYS_SLEEP
	bl	save
	mov	x0,40000
	sys	SYS_SLEEP
	bl	save
	mov	x0,0
	sys	SYS_EXIT

; journal -> fichier
save:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,logbuf
	movl	x1,LOG_MAX
	sys	SYS_KLOG
	tbnz	x0,63,.out
	mov	x20,x0
	adr	x0,path
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	adr	x1,logbuf
	mov	x2,x20
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,m_ok
	bl	lang_puts
	b	.out
.fail:	adr	x0,m_fail
	bl	lang_puts
.out:	ldp	x19,x20,[sp],16
	epilogue


segment readable writeable
kit_data

path		db 'SD0:/journal.txt',0
m_ok		db 'journal : SD0:/journal.txt ',0xC3,0xA9,'crit',10,0
m_fail		db 'journal : ',0xC3,0xA9,'criture impossible',10,0
		align 16
logbuf		rb LOG_MAX
