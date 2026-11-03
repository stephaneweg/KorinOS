; mkdir - cree des dossiers : mkdir [-p] dossiers (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; 1 : -p
	mov	x23,0				; code
	ldr	x0,[x20,8]
	cbz	x0,.fin
	bl	t_est_opt
	cbz	x0,.a
	mov	x22,1
	mov	x21,2
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x24,[x20,x21,lsl 3]
	add	x21,x21,1
	cbz	x22,.un
	mov	x0,x24
	bl	parents
.un:	mov	x0,x24
	sys	SYS_MKDIR
	cbz	x0,.a
	cbz	x22,.err
	mov	x0,x24				; -p : deja la, c'est bon
	bl	t_type
	cmp	x0,1
	b.eq	.a
.err:	mov	x0,x24
	bl	t_err_fic
	mov	x23,1
	b	.a
.fin:	mov	x0,x23
	bl	t_fin

; x0 = chemin : ses dossiers parents crees
parents:
	prologue
	stp	x24,x25,[sp,-16]!
	sub	sp,sp,304
	mov	x1,x0
	mov	x0,sp
	mov	x2,0
.cp:	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	cbz	w3,.go
	add	x2,x2,1
	cmp	x2,300
	b.lo	.cp
	b	.out
.go:	add	x24,sp,1
.c:	ldrb	w0,[x24]
	cbz	w0,.out
	cmp	w0,'/'
	b.ne	.n
	ldurb	w0,[x24,-1]			; pas la racine d'un volume
	cmp	w0,':'
	b.eq	.n
	strb	wzr,[x24]
	mov	x0,sp
	sys	SYS_MKDIR
	mov	w0,'/'
	strb	w0,[x24]
.n:	add	x24,x24,1
	b	.c
.out:	add	sp,sp,304
	ldp	x24,x25,[sp],16
	epilogue

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
