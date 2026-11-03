; test - evalue une condition : test expression (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x0,x20
	bl	cmd_test
	bl	t_fin
include '../../inc/testcmd.inc'

include '../../inc/tool.inc'
s_crochet	db ']',0
s_bang	db '!',0
	align 4

segment readable writeable
kit_data
tool_data
