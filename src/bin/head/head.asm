; head - les premieres lignes : head [-n N | -N] [fichiers] (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,10
	ldr	x24,[x20,8]
	cbz	x24,.go
	mov	x0,x24
	bl	t_est_opt
	cbz	x0,.go
	ldrb	w0,[x24,1]
	cmp	w0,'n'
	b.ne	.chif
	ldr	x0,[x20,16]
	cbz	x0,.go
	bl	parse_dec
	mov	x22,x0
	mov	x21,3
	b	.go
.chif:	add	x0,x24,1
	bl	parse_dec
	mov	x22,x0
	mov	x21,2
.go:	mov	x25,0
	cmp	x21,x19
	b.lo	.a
	mov	x0,0
	bl	un_fichier
	b	.fin
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	bl	un_fichier
	orr	x25,x25,x0
	b	.a
.fin:	mov	x0,x25
	bl	t_fin

; x0 = chemin (0 : l'entree standard) : traite par traite_fichier -> x0 = 0, 1
un_fichier:
	prologue
	str	x24,[sp,-16]!
	mov	x24,x0
	bl	t_ouvrir
	tbnz	x0,63,.err
	mov	x23,x0
	bl	t_lire_debut
	mov	x0,x24
	bl	traite_fichier
	mov	x0,x23
	bl	t_fermer
	mov	x0,0
	b	.out
.err:	mov	x0,x24
	bl	t_err_fic
	mov	x0,1
.out:	ldr	x24,[sp],16
	epilogue

traite_fichier:
	prologue
	str	x25,[sp,-16]!
	mov	x25,x22
.c:	cbz	x25,.f
	bl	t_getc
	tbnz	x0,63,.f
	cmp	w0,10
	b.ne	.p
	sub	x25,x25,1
.p:	bl	t_putc
	b	.c
.f:	ldr	x25,[sp],16
	epilogue

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
