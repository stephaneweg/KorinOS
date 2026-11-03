; find - parcourt des dossiers : find chemins [-name motif] [-type f|d] [-maxdepth n] [-print] (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1				; les chemins, jusqu'a la premiere option
.c:	cmp	x21,x19
	b.hs	.op
	ldr	x0,[x20,x21,lsl 3]
	ldrb	w0,[x0]
	cmp	w0,'-'
	b.eq	.op
	add	x21,x21,1
	b	.c
.op:	mov	x22,x21				; options
	mov	x23,0				; motif
	mov	x24,0				; 1 : fichiers, 2 : dossiers
	movl	x25,0x7FFFFFFF			; profondeur
.o:	cmp	x22,x19
	b.hs	.go
	ldr	x26,[x20,x22,lsl 3]
	add	x22,x22,1
	mov	x0,x26
	adr	x1,s_name
	bl	str_eq
	cbz	x0,.t
	ldr	x23,[x20,x22,lsl 3]
	add	x22,x22,1
	b	.o
.t:	mov	x0,x26
	adr	x1,s_type
	bl	str_eq
	cbz	x0,.m
	ldr	x0,[x20,x22,lsl 3]
	add	x22,x22,1
	ldrb	w0,[x0]
	cmp	w0,'f'
	mov	x24,1
	b.eq	.o
	mov	x24,2
	b	.o
.m:	mov	x0,x26
	adr	x1,s_max
	bl	str_eq
	cbz	x0,.o
	ldr	x0,[x20,x22,lsl 3]
	add	x22,x22,1
	bl	parse_dec
	mov	x25,x0
	b	.o
.go:	mov	x26,1
	cmp	x21,1
	b.ne	.p
	adr	x0,s_point
	mov	x1,0
	bl	visite
	b	.fin
.p:	cmp	x26,x21
	b.hs	.fin
	ldr	x0,[x20,x26,lsl 3]
	add	x26,x26,1
	mov	x1,0
	bl	visite
	b	.p
.fin:	mov	x0,0
	bl	t_fin

; x0 = chemin, x1 = profondeur
visite:
	prologue
	stp	x27,x28,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,DIRENT_BYTES+320
	mov	x27,x0
	mov	x28,x1
	bl	t_type
	tbz	x0,63,.ex
	mov	x0,x27
	bl	t_err_fic
	b	.out
.ex:	mov	x21,x0				; 1 : dossier
	cbz	x24,.nom			; -type
	cmp	x24,2
	cset	x1,eq
	cmp	x1,x21
	b.ne	.des
.nom:	cbz	x23,.ecr			; -name
	mov	x0,x27
	bl	t_base
	mov	x1,x0
	mov	x0,x23
	bl	motif
	cbz	x0,.des
.ecr:	mov	x0,x27
	bl	t_puts
	bl	t_nl
.des:	cbz	x21,.out
	cmp	x28,x25
	b.hs	.out
	mov	x0,x27
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x22,x0
.l:	mov	x0,x22
	mov	x1,sp
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.f
	ldrb	w0,[sp]
	cmp	w0,'.'
	b.ne	.p
	ldrb	w0,[sp,1]
	cbz	w0,.l
	cmp	w0,'.'
	b.ne	.p
	ldrb	w0,[sp,2]
	cbz	w0,.l
.p:	add	x0,sp,DIRENT_BYTES
	mov	x1,x27
	mov	x2,sp
	bl	t_join
	add	x0,sp,DIRENT_BYTES
	add	x1,x28,1
	bl	visite
	b	.l
.f:	mov	x0,x22
	sys	SYS_CLOSE
.out:	add	sp,sp,DIRENT_BYTES+320
	ldp	x21,x22,[sp],16
	ldp	x27,x28,[sp],16
	epilogue

; x0 = motif (* ?), x1 = nom -> x0 = 1
motif:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
.c:	ldrb	w0,[x21]
	cbz	w0,.fin
	cmp	w0,'*'
	b.ne	.q
	add	x21,x21,1
.et:	mov	x0,x21
	mov	x1,x22
	bl	motif
	cbnz	x0,.oui
	ldrb	w0,[x22],1
	cbnz	w0,.et
	b	.non
.q:	ldrb	w1,[x22]
	cbz	w1,.non
	cmp	w0,'?'
	b.eq	.av
	cmp	w0,w1
	b.ne	.non
.av:	add	x21,x21,1
	add	x22,x22,1
	b	.c
.fin:	ldrb	w0,[x22]
	cbnz	w0,.non
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	epilogue

include '../../inc/tool.inc'
s_name	db '-name',0
s_type	db '-type',0
s_max	db '-maxdepth',0
s_point	db '.',0
	align 4

segment readable writeable
kit_data
tool_data
