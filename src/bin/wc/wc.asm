; wc - compte : wc [-l] [-w] [-c] [fichiers] (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; bits : 1 lignes, 2 mots, 4 octets
.o:	cmp	x21,x19
	b.hs	.dep
	ldr	x24,[x20,x21,lsl 3]
	mov	x0,x24
	bl	t_est_opt
	cbz	x0,.dep
	add	x21,x21,1
.l:	ldrb	w0,[x24,1]!
	cbz	w0,.o
	cmp	w0,'l'
	b.ne	.l2
	orr	x22,x22,1
	b	.l
.l2:	cmp	w0,'w'
	b.ne	.l3
	orr	x22,x22,2
	b	.l
.l3:	orr	x22,x22,4
	b	.l
.dep:	cbnz	x22,.go
	mov	x22,7
.go:	cmp	x21,x19
	b.lo	.a
	mov	x0,0
	bl	un_fichier
	mov	x0,0
	bl	t_fin
.a:	mov	x25,0
.b:	cmp	x21,x19
	b.hs	.fin
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	bl	un_fichier
	orr	x25,x25,x0
	b	.b
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

; x0 = nom affiche (0 : aucun)
traite_fichier:
	prologue
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x28,x0
	mov	x25,0				; lignes
	mov	x26,0				; mots
	mov	x27,0				; octets
	mov	x24,0				; 1 : dans un mot
.c:	bl	t_getc
	tbnz	x0,63,.f
	add	x27,x27,1
	cmp	w0,10
	b.ne	.m
	add	x25,x25,1
.m:	cmp	w0,' '
	b.eq	.bl
	cmp	w0,9
	b.eq	.bl
	cmp	w0,10
	b.eq	.bl
	cbnz	x24,.c
	mov	x24,1
	add	x26,x26,1
	b	.c
.bl:	mov	x24,0
	b	.c
.f:	tbz	x22,0,.n2
	mov	x0,x25
	bl	val
.n2:	tbz	x22,1,.n3
	mov	x0,x26
	bl	val
.n3:	tbz	x22,2,.n4
	mov	x0,x27
	bl	val
.n4:	cbz	x28,.nl
	mov	x0,x28
	bl	t_puts
.nl:	bl	t_nl
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	epilogue

; x0 = valeur, suivie d'un espace si d'autres viennent
val:
	prologue
	bl	t_putn
	cbnz	x28,.e
	sub	x0,x22,1
	tst	x22,x0				; un seul compte demande : rien apres
	b.eq	.out
.e:	mov	w0,' '
	bl	t_putc
.out:	epilogue

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
