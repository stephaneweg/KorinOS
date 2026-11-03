; rm - efface des fichiers : rm [-f] [-r] chemins (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; bit 0 : -f, bit 1 : -r
	mov	x23,0				; code
.o:	cmp	x21,x19
	b.hs	.fin
	ldr	x24,[x20,x21,lsl 3]
	mov	x0,x24
	bl	t_est_opt
	cbz	x0,.a
	add	x21,x21,1
.l:	ldrb	w0,[x24,1]!
	cbz	w0,.o
	cmp	w0,'f'
	b.ne	.r
	orr	x22,x22,1
	b	.l
.r:	cmp	w0,'r'
	b.eq	.r2
	cmp	w0,'R'
	b.ne	.l
.r2:	orr	x22,x22,2
	b	.l
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x24,[x20,x21,lsl 3]
	add	x21,x21,1
	mov	x0,x24
	bl	efface
	orr	x23,x23,x0
	b	.a
.fin:	mov	x0,x23
	bl	t_fin

; x0 = chemin -> x0 = 0, 1 : erreur
efface:
	prologue
	stp	x24,x25,[sp,-16]!
	mov	x24,x0
	tbz	x22,1,.u
	bl	t_type
	cmp	x0,1
	b.ne	.u
	mov	x0,x24
	bl	vider
.u:	mov	x0,x24
	sys	SYS_UNLINK
	mov	x1,x0
	mov	x0,0
	cbz	x1,.out
	tbnz	x22,0,.out
	mov	x0,x24
	bl	t_err_fic
	mov	x0,1
.out:	ldp	x24,x25,[sp],16
	epilogue

; x0 = dossier : tout ce qu'il contient, efface
vider:
	prologue
	stp	x24,x25,[sp,-16]!
	sub	sp,sp,DIRENT_BYTES+320
	mov	x24,x0
.ouv:	mov	x0,x24				; (rouvert apres chaque effacement)
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x25,x0
.l:	mov	x0,x25
	mov	x1,sp
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.f
	ldrb	w0,[sp]				; ni . ni ..
	cmp	w0,'.'
	b.ne	.pris
	ldrb	w0,[sp,1]
	cbz	w0,.l
	cmp	w0,'.'
	b.ne	.pris
	ldrb	w0,[sp,2]
	cbz	w0,.l
.pris:	mov	x0,x25
	sys	SYS_CLOSE
	add	x0,sp,DIRENT_BYTES
	mov	x1,x24
	mov	x2,sp
	bl	t_join
	add	x0,sp,DIRENT_BYTES
	bl	efface
	cbz	x0,.ouv
	b	.out
.f:	mov	x0,x25
	sys	SYS_CLOSE
.out:	add	sp,sp,DIRENT_BYTES+320
	ldp	x24,x25,[sp],16
	epilogue

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
