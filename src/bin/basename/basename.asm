; basename - dernier composant : basename chemin [suffixe] (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	ldr	x21,[x20,8]
	cbz	x21,.fin
	mov	x0,x21
	bl	t_base
	mov	x21,x0
	ldr	x22,[x20,16]
	cbz	x22,.ecr
	mov	x0,x21				; sans son suffixe
	bl	str_len
	mov	x23,x0
	mov	x0,x22
	bl	str_len
	cmp	x0,x23
	b.hs	.ecr
	sub	x1,x23,x0
	add	x1,x21,x1
	mov	x2,x1
	mov	x3,x22
.c:	ldrb	w4,[x2],1
	ldrb	w5,[x3],1
	cmp	w4,w5
	b.ne	.ecr
	cbnz	w4,.c
	strb	wzr,[x1]
.ecr:	mov	x0,x21
	bl	t_puts
	bl	t_nl
.fin:	mov	x0,0
	bl	t_fin

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
