; mv - deplace ou renomme : mv [-f] source... destination (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	ldr	x0,[x20,8]
	cbz	x0,.us
	bl	t_est_opt
	cbz	x0,.d
	mov	x21,2
.d:	sub	x22,x19,1			; la destination
	cmp	x21,x22
	b.hs	.us
	ldr	x22,[x20,x22,lsl 3]
	mov	x23,0				; code
.s:	sub	x0,x19,1
	cmp	x21,x0
	b.hs	.fin
	ldr	x24,[x20,x21,lsl 3]
	add	x21,x21,1
	mov	x0,x24
	bl	deplace
	orr	x23,x23,x0
	b	.s
.fin:	mov	x0,x23
	bl	t_fin
.us:	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,1
	bl	t_fin

; x0 = source (x22 : destination) -> x0 = 0, 1
deplace:
	prologue
	stp	x24,x25,[sp,-16]!
	sub	sp,sp,320
	mov	x24,x0
	mov	x25,x22
	mov	x0,x22
	bl	t_type
	cmp	x0,1
	b.ne	.tel
	mov	x0,x24
	bl	t_base
	mov	x2,x0
	mov	x0,sp
	mov	x1,x22
	bl	t_join
	mov	x25,sp
.tel:	mov	x0,x24
	mov	x1,x25
	bl	t_rename
	cbz	x0,.out
	cmn	x0,18				; deux volumes : recopie (un fichier)
	b.ne	.err
	mov	x0,x24
	mov	x1,x25
	bl	t_copier
	cbnz	x0,.err
	mov	x0,x24
	sys	SYS_UNLINK
	mov	x0,0
	b	.out
.err:	mov	x0,x24
	bl	t_err_fic
	mov	x0,1
.out:	add	sp,sp,320
	ldp	x24,x25,[sp],16
	epilogue

include '../../inc/tool.inc'
s_usage	db 'usage : mv source... destination',0
	align 4

segment readable writeable
kit_data
tool_data
