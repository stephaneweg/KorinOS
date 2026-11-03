; false - echoue (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x0,1
	bl	t_fin

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
