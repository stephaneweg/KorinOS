; grep - cherche un motif : grep [-EivnclqsF] [-e motif] motif [fichiers] (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; bits : 1 -v, 2 -n, 4 -c, 8 -l, 16 -q, 32 -H
	mov	x23,0				; drapeaux du motif
	mov	x24,0				; motif
.o:	cmp	x21,x19
	b.hs	.mot
	ldr	x25,[x20,x21,lsl 3]
	mov	x0,x25
	bl	t_est_opt
	cbz	x0,.mot
	add	x21,x21,1
.l:	ldrb	w0,[x25,1]!
	cbz	w0,.o
	cmp	w0,'E'
	b.ne	.a1
	orr	x23,x23,RE_E
	b	.l
.a1:	cmp	w0,'i'
	b.ne	.a2
	orr	x23,x23,RE_I
	b	.l
.a2:	cmp	w0,'v'
	b.ne	.a3
	orr	x22,x22,1
	b	.l
.a3:	cmp	w0,'n'
	b.ne	.a4
	orr	x22,x22,2
	b	.l
.a4:	cmp	w0,'c'
	b.ne	.a5
	orr	x22,x22,4
	b	.l
.a5:	cmp	w0,'l'
	b.ne	.a6
	orr	x22,x22,8
	b	.l
.a6:	cmp	w0,'q'
	b.ne	.a7
	orr	x22,x22,16
	b	.l
.a7:	cmp	w0,'e'
	b.ne	.l
	ldr	x24,[x20,x21,lsl 3]
	add	x21,x21,1
	b	.o
.mot:	cbnz	x24,.comp
	cmp	x21,x19
	b.hs	.us
	ldr	x24,[x20,x21,lsl 3]
	add	x21,x21,1
.comp:	movl	x0,RE_TAILLE
	bl	malloc
	mov	x2,x0
	mov	x26,x0
	mov	x0,x24
	mov	x1,x23
	bl	re_compile
	sub	x0,x19,x21			; plusieurs fichiers : leur nom devant
	cmp	x0,1
	b.ls	.nb
	orr	x22,x22,32
.nb:	mov	x25,1				; code : 1 si rien trouve
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
	b	.a
.fin:	mov	x0,x25
	bl	t_fin
.us:	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,2
	bl	t_fin

; x0 = chemin (0 : l'entree standard) : traite par traite_fichier -> x0 = 0, 1
un_fichier:
	prologue
	str	x24,[sp,-16]!
	mov	x24,x0
	bl	t_ouvrir
	tbnz	x0,63,.err
	bl	t_lire_debut
	mov	x0,x24
	bl	traite_fichier
	adr	x0,t_fd
	ldr	x0,[x0]
	bl	t_fermer
	mov	x0,0
	b	.out
.err:	mov	x0,x24
	bl	t_err_fic
	mov	x25,2
	mov	x0,1
.out:	ldr	x24,[sp],16
	epilogue

; x0 = nom du fichier (0 : entree standard)
traite_fichier:
	prologue
	stp	x27,x28,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x28,x0
	mov	x27,0				; numero de ligne
	mov	x24,0				; lignes trouvees
.l:	bl	t_lire_ligne
	cbz	x0,.f
	mov	x23,x0
	add	x27,x27,1
	mov	x0,x26
	mov	x1,x23
	mov	x2,x23
	bl	re_cherche
	tbz	x22,0,.pv
	eor	x0,x0,1
.pv:	cbz	x0,.l
	add	x24,x24,1
	mov	x25,0
	tbnz	x22,4,.q
	tbnz	x22,3,.f
	tbnz	x22,2,.l
	tbz	x22,5,.pn
	mov	x0,x28
	bl	t_puts
	mov	w0,':'
	bl	t_putc
.pn:	tbz	x22,1,.pl
	mov	x0,x27
	bl	t_putn
	mov	w0,':'
	bl	t_putc
.pl:	mov	x0,x23
	bl	t_puts
	bl	t_nl
	b	.l
.q:	mov	x0,0
	bl	t_fin
.f:	tbz	x22,3,.pc
	cbz	x24,.out
	cbz	x28,.out
	mov	x0,x28
	bl	t_puts
	bl	t_nl
	b	.out
.pc:	tbz	x22,2,.out
	tbz	x22,5,.pc2
	mov	x0,x28
	bl	t_puts
	mov	w0,':'
	bl	t_putc
.pc2:	mov	x0,x24
	bl	t_putn
	bl	t_nl
.out:	ldp	x23,x24,[sp],16
	ldp	x27,x28,[sp],16
	epilogue
include '../../inc/regex.inc'

include '../../inc/tool.inc'
s_usage	db 'usage : grep [-Eivnclq] [-e motif] motif [fichiers]',0
	align 4

segment readable writeable
kit_data
tool_data
regex_data
