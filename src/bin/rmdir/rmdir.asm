; rmdir - efface des dossiers vides : rmdir dossiers (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x23,[x20,x21,lsl 3]
	add	x21,x21,1
	mov	x0,x23
	sys	SYS_UNLINK
	cbz	x0,.a
	mov	x0,x23
	bl	t_err_fic
	mov	x22,1
	b	.a
.fin:	mov	x0,x22
	bl	t_fin

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
