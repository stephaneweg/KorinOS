; pwd - dossier courant (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	adr	x0,dos
	mov	x1,256
	sys	SYS_GETCWD
	adr	x0,dos
	bl	t_puts
	bl	t_nl
	mov	x0,0
	bl	t_fin

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
dos	rb 256
