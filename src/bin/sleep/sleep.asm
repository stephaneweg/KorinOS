; sleep - attend : sleep secondes (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	ldr	x0,[x20,8]
	cbz	x0,.fin
	bl	parse_dec
	mov	x1,1000
	mul	x0,x0,x1
	sys	SYS_SLEEP
.fin:	mov	x0,0
	bl	t_fin

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
