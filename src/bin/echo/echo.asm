; echo - ecrit ses arguments : echo [-n] mots (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; 1 : pas de fin de ligne
	ldr	x0,[x20,8]
	cbz	x0,.a
	adr	x1,s_n
	bl	str_eq
	cbz	x0,.a
	mov	x22,1
	mov	x21,2
.a:	cmp	x21,x19
	b.hs	.f
	ldr	x0,[x20,x21,lsl 3]
	bl	t_puts
	add	x21,x21,1
	cmp	x21,x19
	b.hs	.f
	mov	w0,' '
	bl	t_putc
	b	.a
.f:	cbnz	x22,.fin
	bl	t_nl
.fin:	mov	x0,0
	bl	t_fin

include '../../inc/tool.inc'
s_n	db '-n',0
	align 4

segment readable writeable
kit_data
tool_data
