; unload - retire les images d'un programme (docs/ABI.md, « Images des
; executables ») :
;   unload <chemin>...       le prochain lancement relit le fichier ; les
;                            process deja lances gardent la leur
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	mov	x21,0				; code de sortie
	cbz	x19,.usage
	ldrb	w0,[x19]
	cbz	w0,.usage
.mot:	ldrb	w0,[x19]			; chaque mot de la ligne
	cmp	w0,' '
	b.ne	.m1
	add	x19,x19,1
	b	.mot
.m1:	cbz	w0,.fin
	mov	x20,x19
.m2:	ldrb	w0,[x19]
	cbz	w0,.m3
	cmp	w0,' '
	b.eq	.m3
	add	x19,x19,1
	b	.m2
.m3:	mov	w22,w0
	strb	wzr,[x19]
	mov	x0,x20
	sys	SYS_UNLOAD
	mov	x23,x0
	adr	x0,m_un
	bl	lang_puts
	mov	x0,x20
	bl	puts
	tbnz	x23,63,.err
	adr	x0,m_sep
	bl	lang_puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_ok
	bl	lang_puts
	b	.suite
.err:	adr	x0,m_err
	bl	lang_puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x21,1
.suite:	cbz	w22,.fin
	add	x19,x19,1
	b	.mot
.usage:	adr	x0,m_usage
	bl	lang_puts
	mov	x21,1
.fin:	mov	x0,x21
	sys	SYS_EXIT

m_un		db 'unload : ',0
m_sep		db ' : ',0
m_ok		db ' image(s) oubli',0xC3,0xA9,'e(s)',10,0
m_err		db ' : erreur ',0
m_nl		db 10,0
m_usage		db 'usage : unload <chemin>...',10,0
		align 8

segment readable writeable
kit_data
