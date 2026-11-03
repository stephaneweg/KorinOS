; dirname - dossier d'un chemin : dirname chemin (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	ldr	x21,[x20,8]
	cbz	x21,.point
	mov	x0,x21
	bl	t_base
	cmp	x0,x21
	b.eq	.point
	sub	x0,x0,1				; le separateur
	ldrb	w1,[x0]
	cmp	w1,':'
	b.eq	.vol
	cmp	x0,x21
	b.ne	.c
	add	x0,x0,1				; « /x » : /
	b	.c
.vol:	add	x0,x0,1
.c:	strb	wzr,[x0]
	mov	x0,x21
	bl	t_puts
	b	.nl
.point:	mov	w0,'.'
	bl	t_putc
.nl:	bl	t_nl
	mov	x0,0
	bl	t_fin

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
