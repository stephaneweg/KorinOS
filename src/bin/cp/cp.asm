; cp - copie : cp [-r] [-f] source... destination (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; 1 : -r
.o:	cmp	x21,x19
	b.hs	.us
	ldr	x24,[x20,x21,lsl 3]
	mov	x0,x24
	bl	t_est_opt
	cbz	x0,.a
	add	x21,x21,1
.l:	ldrb	w0,[x24,1]!
	cbz	w0,.o
	cmp	w0,'r'
	b.eq	.r
	cmp	w0,'R'
	b.eq	.r
	cmp	w0,'a'
	b.ne	.l
.r:	mov	x22,1
	b	.l
.a:	sub	x23,x19,1			; la destination
	cmp	x21,x23
	b.hs	.us
	ldr	x23,[x20,x23,lsl 3]
	mov	x25,0				; code
.s:	sub	x0,x19,1
	cmp	x21,x0
	b.hs	.fin
	ldr	x24,[x20,x21,lsl 3]
	add	x21,x21,1
	mov	x0,x24
	mov	x1,x23
	bl	copie_vers
	orr	x25,x25,x0
	b	.s
.fin:	mov	x0,x25
	bl	t_fin
.us:	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,1
	bl	t_fin

; x0 = source, x1 = destination (si c'est un dossier : dedans) -> x0 = 0, 1
copie_vers:
	prologue
	stp	x24,x25,[sp,-16]!
	sub	sp,sp,320
	mov	x24,x0
	mov	x25,x1
	mov	x0,x25
	bl	t_type
	cmp	x0,1
	b.ne	.tel
	mov	x0,x24
	bl	t_base
	mov	x2,x0
	mov	x0,sp
	mov	x1,x25
	bl	t_join
	mov	x25,sp
.tel:	mov	x0,x24
	mov	x1,x25
	bl	copie_arbre
	add	sp,sp,320
	ldp	x24,x25,[sp],16
	epilogue

; x0 = source, x1 = destination exacte -> x0 = 0, 1
copie_arbre:
	prologue
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	sub	sp,sp,DIRENT_BYTES+640
	mov	x24,x0
	mov	x25,x1
	bl	t_type
	tbnz	x0,63,.err
	cbz	x0,.fic
	cbz	x22,.nonr
	mov	x0,x25
	sys	SYS_MKDIR
	mov	x0,x24
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x26,x0
	mov	x27,0
.l:	mov	x0,x26
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
	mov	x1,x24
	mov	x2,sp
	bl	t_join
	add	x0,sp,DIRENT_BYTES+320
	mov	x1,x25
	mov	x2,sp
	bl	t_join
	add	x0,sp,DIRENT_BYTES
	add	x1,sp,DIRENT_BYTES+320
	bl	copie_arbre
	orr	x27,x27,x0
	b	.l
.f:	mov	x0,x26
	sys	SYS_CLOSE
	mov	x0,x27
	b	.out
.nonr:	adr	x0,s_dossier
	bl	t_err_msg
	mov	x0,1
	b	.out
.fic:	mov	x0,x24
	mov	x1,x25
	bl	t_copier
	cbz	x0,.out
.err:	mov	x0,x24
	bl	t_err_fic
	mov	x0,1
.out:	add	sp,sp,DIRENT_BYTES+640
	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	epilogue

include '../../inc/tool.inc'
s_usage	db 'usage : cp [-r] source... destination',0
s_dossier	db 'un dossier demande -r',0
	align 4

segment readable writeable
kit_data
tool_data
