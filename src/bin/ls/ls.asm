; ls - liste un dossier : ls [-a] [-l] [-1] [chemins], trie (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; bit 0 : -a, bit 1 : -l
.o:	cmp	x21,x19
	b.hs	.go
	ldr	x24,[x20,x21,lsl 3]
	mov	x0,x24
	bl	t_est_opt
	cbz	x0,.go
	add	x21,x21,1
.l:	ldrb	w0,[x24,1]!
	cbz	w0,.o
	cmp	w0,'a'
	b.ne	.l2
	orr	x22,x22,1
	b	.l
.l2:	cmp	w0,'l'
	b.ne	.l
	orr	x22,x22,2
	b	.l
.go:	mov	x23,0				; code
	cmp	x21,x19
	b.lo	.a
	adr	x0,s_point
	bl	liste
	mov	x23,x0
	b	.fin
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x24,[x20,x21,lsl 3]
	add	x21,x21,1
	mov	x0,x24
	bl	liste
	orr	x23,x23,x0
	b	.a
.fin:	mov	x0,x23
	bl	t_fin

; x0 = chemin -> x0 = 0, 1
liste:
	prologue
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	mov	x24,x0
	bl	t_type
	tbnz	x0,63,.err
	cbnz	x0,.dos
	mov	x0,x24				; un fichier : son nom
	bl	un_nom
	b	.ok
.dos:	mov	x0,x24
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x25,x0
	movl	x0,8*NOMS_MAX
	bl	malloc
	mov	x26,x0
	mov	x27,0
.l:	mov	x0,x25
	adr	x1,ent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.f
	adr	x0,ent
	ldrb	w1,[x0]
	cmp	w1,'.'
	b.ne	.garde
	tbz	x22,0,.l
.garde:	cmp	x27,NOMS_MAX
	b.hs	.l
	adr	x0,ent
	bl	str_len
	add	x0,x0,1
	bl	malloc
	str	x0,[x26,x27,lsl 3]
	add	x27,x27,1
	mov	x1,x0
	adr	x0,ent
.c:	ldrb	w2,[x0],1
	strb	w2,[x1],1
	cbnz	w2,.c
	b	.l
.f:	mov	x0,x25
	sys	SYS_CLOSE
	mov	x0,x26
	add	x1,x26,x27,lsl 3
	bl	trier
	mov	x25,0
.p:	cmp	x25,x27
	b.hs	.ok
	ldr	x0,[x26,x25,lsl 3]
	add	x25,x25,1
	tbnz	x22,1,.long
	bl	t_puts
	bl	t_nl
	b	.p
.long:	str	x0,[sp,-16]!
	sub	sp,sp,320
	mov	x2,x0
	mov	x0,sp
	mov	x1,x24
	bl	t_join
	mov	x0,sp
	ldr	x1,[sp,320]
	bl	ligne_longue
	add	sp,sp,336
	b	.p
.ok:	mov	x0,0
	b	.out
.err:	mov	x0,x24
	bl	t_err_fic
	mov	x0,1
.out:	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	epilogue

; x0 = chemin
un_nom:
	prologue
	tbnz	x22,1,.l
	bl	t_puts
	bl	t_nl
	epilogue
.l:	mov	x1,x0
	bl	ligne_longue
	epilogue

; x0 = chemin, x1 = nom affiche : « d       taille  nom »
ligne_longue:
	prologue
	str	x24,[sp,-16]!
	sub	sp,sp,16
	mov	x24,x1
	mov	x1,sp
	sys	SYS_STAT
	ldr	x0,[sp,8]
	tst	x0,ATTR_DIR
	mov	w0,'-'
	mov	w1,'d'
	csel	w0,w0,w1,eq
	bl	t_putc
	mov	w0,' '
	bl	t_putc
	ldr	x0,[sp]
	bl	t_putn
	mov	w0,' '
	bl	t_putc
	mov	x0,x24
	bl	t_puts
	bl	t_nl
	add	sp,sp,16
	ldr	x24,[sp],16
	epilogue

; x0, x1 = tableau de chaines [debut, fin[
trier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	add	x21,x19,8
.i:	cmp	x21,x20
	b.hs	.f
	ldr	x22,[x21]
	mov	x2,x21
.j:	cmp	x2,x19
	b.ls	.pose
	ldur	x3,[x2,-8]
	mov	x4,0
.c:	ldrb	w5,[x3,x4]
	ldrb	w6,[x22,x4]
	cmp	w5,w6
	b.ne	.d
	cbz	w5,.pose
	add	x4,x4,1
	b	.c
.d:	b.ls	.pose
	str	x3,[x2]
	sub	x2,x2,8
	b	.j
.pose:	str	x22,[x2]
	add	x21,x21,8
	b	.i
.f:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

NOMS_MAX = 16384

include '../../inc/tool.inc'
s_point	db '.',0
	align 4

segment readable writeable
kit_data
tool_data
ent	rb DIRENT_BYTES
