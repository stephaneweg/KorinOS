; sort - trie des lignes : sort [-r] [-n] [-u] [fichiers] (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,0				; bits : 1 -r, 2 -n, 4 -u
.o:	cmp	x21,x19
	b.hs	.go
	ldr	x24,[x20,x21,lsl 3]
	mov	x0,x24
	bl	t_est_opt
	cbz	x0,.go
	add	x21,x21,1
.l:	ldrb	w0,[x24,1]!
	cbz	w0,.o
	cmp	w0,'r'
	b.ne	.l2
	orr	x22,x22,1
	b	.l
.l2:	cmp	w0,'n'
	b.ne	.l3
	orr	x22,x22,2
	b	.l
.l3:	cmp	w0,'u'
	b.ne	.l
	orr	x22,x22,4
	b	.l
.go:	mov	x0,0				; textes et pointeurs : regions a la demande
	movl	x1,1 shl 30
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x23,x0				; ecriture
	mov	x0,0
	movl	x1,1 shl 28
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x24,x0				; lignes
	mov	x25,0				; nombre
	cmp	x21,x19
	b.lo	.a
	mov	x0,0
	bl	lire
	b	.tri
.a:	cmp	x21,x19
	b.hs	.tri
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	bl	lire
	b	.a
.tri:	mov	x0,x24
	mov	x1,x25
	add	x2,x24,x25,lsl 3		; (tampon de fusion, juste apres)
	bl	fusion
	mov	x26,0
.e:	cmp	x26,x25
	b.hs	.fin
	ldr	x27,[x24,x26,lsl 3]
	cbz	x26,.ec
	tbz	x22,2,.ec
	sub	x0,x26,1
	ldr	x0,[x24,x0,lsl 3]
	mov	x1,x27
	bl	compare
	cbz	x0,.nx
.ec:	mov	x0,x27
	bl	t_puts
	bl	t_nl
.nx:	add	x26,x26,1
	b	.e
.fin:	mov	x0,0
	bl	t_fin

; x0 = chemin
lire:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	t_ouvrir
	tbnz	x0,63,.err
	stp	x0,xzr,[sp,-16]!
	bl	t_lire_debut
.c:	bl	t_lire_ligne
	cbz	x0,.f
	str	x23,[x24,x25,lsl 3]
	add	x25,x25,1
.cp:	ldrb	w1,[x0],1
	strb	w1,[x23],1
	cbnz	w1,.cp
	b	.c
.f:	ldp	x0,xzr,[sp],16
	bl	t_fermer
	b	.out
.err:	mov	x0,x19
	bl	t_err_fic
.out:	ldr	x19,[sp],16
	epilogue

; x0, x1 = lignes -> x0 < 0, 0, > 0
compare:
	prologue
	tbz	x22,1,.txt
	stp	x0,x1,[sp,-16]!
	bl	nb
	mov	x2,x0
	ldp	x0,x1,[sp],16
	stp	x0,x1,[sp,-16]!
	mov	x0,x1
	str	x2,[sp,-16]!
	bl	nb
	mov	x3,x0
	ldr	x2,[sp],16
	ldp	x0,x1,[sp],16
	subs	x4,x2,x3
	b.eq	.txt
	mov	x0,x4
	b	.sens
.txt:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.ne	.d
	cbnz	w2,.txt
	mov	x0,0
	epilogue
.d:	sub	x0,x2,x3
.sens:	tbz	x22,0,.out
	neg	x0,x0
.out:	epilogue

; x0 = ligne -> x0 = son nombre au debut
nb:
	mov	x1,0
	mov	x2,0
.b:	ldrb	w3,[x0]
	cmp	w3,' '
	b.ne	.s
	add	x0,x0,1
	b	.b
.s:	cmp	w3,'-'
	b.ne	.c
	mov	x2,1
	add	x0,x0,1
.c:	ldrb	w3,[x0],1
	sub	w3,w3,'0'
	cmp	w3,9
	b.hi	.f
	mov	x4,10
	mul	x1,x1,x4
	add	x1,x1,x3
	b	.c
.f:	mov	x0,x1
	cbz	x2,.r
	neg	x0,x0
.r:	ret

; x0 = tableau, x1 = nombre, x2 = tampon de meme taille : tri fusion, stable
fusion:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x26,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	cmp	x1,2
	b.lo	.out
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	lsr	x23,x20,1			; gauche
	mov	x1,x23
	bl	fusion
	add	x0,x19,x23,lsl 3
	sub	x1,x20,x23
	mov	x2,x21
	bl	fusion
	mov	x0,0				; gauche dans le tampon
.cp:	cmp	x0,x23
	b.hs	.m
	ldr	x1,[x19,x0,lsl 3]
	str	x1,[x21,x0,lsl 3]
	add	x0,x0,1
	b	.cp
.m:	mov	x24,0				; i gauche
	mov	x25,x23				; j droite
	mov	x26,0				; k
.b:	cmp	x24,x23
	b.hs	.out
	cmp	x25,x20
	b.hs	.g
	ldr	x0,[x21,x24,lsl 3]
	ldr	x1,[x19,x25,lsl 3]
	bl	compare
	cmp	x0,0
	b.gt	.d
.g:	ldr	x0,[x21,x24,lsl 3]
	add	x24,x24,1
	b	.p
.d:	ldr	x0,[x19,x25,lsl 3]
	add	x25,x25,1
.p:	str	x0,[x19,x26,lsl 3]
	add	x26,x26,1
	b	.b
.out:	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x26,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
