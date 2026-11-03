; tr - remplace ou efface des caracteres : tr [-d] ensemble1 [ensemble2] (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; 1 : -d
	ldr	x0,[x20,8]
	cbz	x0,.us
	adr	x1,s_d
	bl	str_eq
	cbz	x0,.a
	mov	x22,1
	mov	x21,2
.a:	ldr	x0,[x20,x21,lsl 3]
	cbz	x0,.us
	adr	x1,ens1
	bl	developpe
	mov	x23,x0				; sa longueur
	add	x21,x21,1
	mov	x24,0
	cbnz	x22,.t
	ldr	x0,[x20,x21,lsl 3]
	cbz	x0,.us
	adr	x1,ens2
	bl	developpe
	mov	x24,x0
.t:	adr	x25,table			; 0 a 255 ; 256 : efface
	mov	x0,0
.i:	strh	w0,[x25,x0,lsl 1]
	add	x0,x0,1
	cmp	x0,256
	b.lo	.i
	mov	x0,0
.r:	cmp	x0,x23
	b.hs	.go
	adr	x1,ens1
	ldrb	w1,[x1,x0]
	mov	w2,256
	cbnz	x22,.p
	cmp	x0,x24				; ensemble2 plus court : son dernier
	sub	x2,x24,1
	csel	x2,x0,x2,lo
	adr	x3,ens2
	ldrb	w2,[x3,x2]
.p:	strh	w2,[x25,x1,lsl 1]
	add	x0,x0,1
	b	.r
.go:	mov	x0,0
	bl	t_lire_debut
.c:	bl	t_getc
	tbnz	x0,63,.fin
	ldrh	w0,[x25,x0,lsl 1]
	cmp	w0,256
	b.eq	.c
	bl	t_putc
	b	.c
.fin:	mov	x0,0
	bl	t_fin
.us:	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,1
	bl	t_fin

; x0 = texte (a-z, \n, \t), x1 = sortie (256 o) -> x0 = longueur
developpe:
	mov	x2,0
.c:	ldrb	w3,[x0],1
	cbz	w3,.f
	cmp	w3,'\'
	b.ne	.pl
	ldrb	w3,[x0],1
	cbz	w3,.f
	cmp	w3,'n'
	b.ne	.t
	mov	w3,10
	b	.pose
.t:	cmp	w3,'t'
	b.ne	.pose
	mov	w3,9
	b	.pose
.pl:	ldrb	w4,[x0]
	cmp	w4,'-'
	b.ne	.pose
	ldrb	w4,[x0,1]
	cbz	w4,.pose
	add	x0,x0,2
.r:	cmp	w3,w4
	b.hi	.c
	cmp	x2,256
	b.hs	.f
	strb	w3,[x1,x2]
	add	x2,x2,1
	add	w3,w3,1
	b	.r
.pose:	cmp	x2,256
	b.hs	.f
	strb	w3,[x1,x2]
	add	x2,x2,1
	b	.c
.f:	mov	x0,x2
	ret

include '../../inc/tool.inc'
s_d	db '-d',0
s_usage	db 'usage : tr [-d] ensemble1 [ensemble2]',0
	align 4

segment readable writeable
kit_data
tool_data
ens1	rb 256
ens2	rb 256
	align 8
table	rb 512
