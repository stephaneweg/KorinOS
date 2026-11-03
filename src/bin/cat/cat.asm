; cat - concatene des fichiers sur la sortie : cat [fichiers] (- : entree standard) (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1				; argument
	mov	x22,0				; code
	cmp	x19,1
	b.ne	.a
	mov	x0,0
	bl	copie
	b	.fin
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	bl	copie
	orr	x22,x22,x0
	b	.a
.fin:	mov	x0,x22
	bl	t_fin

; x0 = chemin -> x0 = 0, 1 si inaccessible
copie:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x22,x0
	bl	t_ouvrir
	tbnz	x0,63,.err
	mov	x21,x0
	bl	t_flush
.c:	mov	x0,x21
	adr	x1,t_lu
	mov	x2,T_LIRE
	sys	SYS_READ
	cmp	x0,0
	b.le	.f
	mov	x2,x0
	mov	x0,1
	adr	x1,t_lu
	sys	SYS_WRITE
	tbz	x0,63,.c
.f:	mov	x0,x21
	bl	t_fermer
	mov	x0,0
	b	.out
.err:	mov	x0,x22
	bl	t_err_fic
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	epilogue

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
