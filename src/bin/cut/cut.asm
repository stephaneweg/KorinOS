; cut - colonnes : cut -d C -f liste, ou cut -c liste [fichiers] (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,9				; delimiteur
	mov	x23,0				; 1 : -c
	mov	x25,0				; la liste
.o:	cmp	x21,x19
	b.hs	.go
	ldr	x24,[x20,x21,lsl 3]
	mov	x0,x24
	bl	t_est_opt
	cbz	x0,.go
	add	x21,x21,1
	ldrb	w0,[x24,1]
	add	x1,x24,2			; valeur collee, ou argument suivant
	ldrb	w2,[x1]
	cbnz	w2,.v
	ldr	x1,[x20,x21,lsl 3]
	add	x21,x21,1
.v:	cmp	w0,'d'
	b.ne	.f
	ldrb	w22,[x1]
	b	.o
.f:	cmp	w0,'c'
	b.ne	.f2
	mov	x23,1
.f2:	mov	x25,x1
	b	.o
.go:	cbz	x25,.us
	mov	x0,x25
	bl	lire_liste
	mov	x25,0
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
.us:	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,1
	bl	t_fin

; x0 = « 1,3-5,7- » : les rangs voulus (de 1 a 1023) dans « voulu »
lire_liste:
	prologue
	stp	x24,x25,[sp,-16]!
	mov	x24,x0
.c:	ldrb	w0,[x24]
	cbz	w0,.out
	mov	x0,x24
	bl	nombre
	mov	x25,x1				; debut (0 : depuis 1)
	mov	x2,x25
	cbnz	x25,.d
	mov	x25,1
.d:	ldrb	w0,[x24]
	cmp	w0,'-'
	b.ne	.un
	add	x24,x24,1
	mov	x0,x24
	bl	nombre
	mov	x2,x1
	cbnz	x2,.p
	mov	x2,1023
	b	.p
.un:	mov	x2,x25
.p:	adr	x0,voulu
.r:	cmp	x25,x2
	b.hi	.s
	cmp	x25,1023
	b.hi	.s
	mov	w1,1
	strb	w1,[x0,x25]
	add	x25,x25,1
	b	.r
.s:	ldrb	w0,[x24]
	cbz	w0,.out
	add	x24,x24,1
	b	.c
.out:	ldp	x24,x25,[sp],16
	epilogue

; x24 = texte -> x1 = nombre, x24 apres
nombre:
	mov	x1,0
.c:	ldrb	w0,[x24]
	sub	w0,w0,'0'
	cmp	w0,9
	b.hi	.r
	mov	x2,10
	mul	x1,x1,x2
	add	x1,x1,x0
	add	x24,x24,1
	b	.c
.r:	ret

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
	mov	x0,1
.out:	ldr	x24,[sp],16
	epilogue

traite_fichier:
	prologue
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
.l:	bl	t_lire_ligne
	cbz	x0,.out
	mov	x25,x0
	mov	x26,1				; rang
	mov	x27,0				; quelque chose d'ecrit
	cbnz	x23,.car
	mov	x0,x25				; sans delimiteur : la ligne entiere
.t:	ldrb	w1,[x0],1
	cbz	w1,.ent
	cmp	w1,w22
	b.ne	.t
.ch:	mov	x28,x25				; un champ
.e:	ldrb	w1,[x25]
	cbz	w1,.ef
	cmp	w1,w22
	b.eq	.ef
	add	x25,x25,1
	b	.e
.ef:	cmp	x26,1023
	b.hi	.nx
	adr	x0,voulu
	ldrb	w0,[x0,x26]
	cbz	w0,.nx
	cbz	x27,.pas
	mov	w0,w22
	bl	t_putc
.pas:	mov	x27,1
	mov	x0,x28
	sub	x1,x25,x28
	bl	t_write
.nx:	add	x26,x26,1
	ldrb	w1,[x25],1
	cbnz	w1,.ch
	b	.nl
.ent:	mov	x0,x25
	bl	t_puts
	b	.nl
.car:	ldrb	w0,[x25],1
	cbz	w0,.nl
	cmp	x26,1023
	b.hi	.nl
	adr	x1,voulu
	ldrb	w1,[x1,x26]
	add	x26,x26,1
	cbz	w1,.car
	bl	t_putc
	b	.car
.nl:	bl	t_nl
	b	.l
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	epilogue

include '../../inc/tool.inc'
s_usage	db 'usage : cut -d C -f liste, ou cut -c liste',0
	align 4

segment readable writeable
kit_data
tool_data
voulu	rb 1024
