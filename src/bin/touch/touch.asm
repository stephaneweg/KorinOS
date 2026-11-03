; touch - cree un fichier ou remet sa date a maintenant : touch [-c] fichiers (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; 1 : -c, ne pas creer
	mov	x23,0
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
	mov	x0,x24
	bl	t_type
	tbz	x0,63,.date
	cbnz	x22,.a
	mov	x0,x24
	mov	x1,O_WRITE or O_CREATE
	sys	SYS_OPEN
	tbnz	x0,63,.err
	sys	SYS_CLOSE
	b	.a
.date:	mov	x0,0
	sys	SYS_CLOCK_GET
	mov	x1,x0
	mov	x0,x24
	bl	t_utime
	cbz	x0,.a
.err:	mov	x0,x24
	bl	t_err_fic
	mov	x23,1
	b	.a
.fin:	mov	x0,x23
	bl	t_fin

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
