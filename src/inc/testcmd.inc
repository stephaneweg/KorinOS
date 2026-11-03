; testcmd.inc - test et [ : la commande integree de sh et le programme /bin/test
; x0 = argv -> x0 = 0 si vrai

; test, [ : -e -f -d -s -z -n, = != == , -eq -ne -lt -le -gt -ge, ! , un mot seul
cmd_test:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,0				; nombre d'arguments
.n:	add	x1,x20,1
	ldr	x2,[x19,x1,lsl 3]
	cbz	x2,.nf
	mov	x20,x1
	b	.n
.nf:	ldr	x0,[x19]			; [ ... ]
	ldrb	w1,[x0]
	cmp	w1,'['
	b.ne	.ev
	cbz	x20,.faux
	ldr	x0,[x19,x20,lsl 3]
	adr	x1,s_crochet
	bl	str_eq
	cbz	x0,.faux
	sub	x20,x20,1
.ev:	add	x19,x19,8			; x19 : le premier argument
	mov	x21,0				; 1 : « ! »
.neg:	cbz	x20,.vide
	ldr	x0,[x19]
	adr	x1,s_bang
	bl	str_eq
	cbz	x0,.exp
	eor	x21,x21,1
	add	x19,x19,8
	sub	x20,x20,1
	b	.neg
.vide:	mov	x0,1
	b	.res
.exp:	cmp	x20,1
	b.ne	.deux
	ldr	x0,[x19]
	ldrb	w0,[x0]
	cmp	w0,0
	cset	x0,eq
	b	.res
.deux:	cmp	x20,2
	b.ne	.trois
	ldr	x22,[x19]			; -f chemin
	ldrb	w0,[x22,1]
	ldr	x1,[x19,8]
	bl	test_unaire
	b	.res
.trois:	cmp	x20,3
	b.ne	.faux
	ldr	x0,[x19]
	ldr	x1,[x19,8]
	ldr	x2,[x19,16]
	bl	test_binaire
.res:	eor	x0,x0,x21
	b	.out
.faux:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = lettre, x1 = texte -> x0 = 0 si vrai
test_unaire:
	prologue
	sub	sp,sp,16
	cmp	w0,'z'
	b.ne	.n
	ldrb	w0,[x1]
	cmp	w0,0
	cset	x0,ne
	b	.out
.n:	cmp	w0,'n'
	b.ne	.st
	ldrb	w0,[x1]
	cmp	w0,0
	cset	x0,eq
	b	.out
.st:	str	x0,[sp,8]
	mov	x0,x1
	mov	x1,sp
	str	x0,[sp,-16]!
	sub	sp,sp,16
	add	x1,sp,16
	ldr	x0,[sp,16]
	mov	x1,sp
	sys	SYS_STAT
	mov	x3,x0
	ldp	x1,x2,[sp]			; taille, attributs
	add	sp,sp,32
	ldr	x0,[sp,8]
	tbnz	x3,63,.nonv
	cmp	w0,'d'
	b.ne	.f
	tst	x2,ATTR_DIR
	cset	x0,eq
	b	.out
.f:	cmp	w0,'f'
	b.ne	.s
	tst	x2,ATTR_DIR
	cset	x0,ne
	b	.out
.s:	cmp	w0,'s'
	b.ne	.oui
	cmp	x1,0
	cset	x0,eq
	b	.out
.oui:	mov	x0,0				; -e, -r, -w, -x
	b	.out
.nonv:	mov	x0,1
.out:	add	sp,sp,16
	epilogue

; x0 = gauche, x1 = operateur, x2 = droite -> x0 = 0 si vrai
test_binaire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldrb	w0,[x20]
	cmp	w0,'='
	b.eq	.eg
	cmp	w0,'!'
	b.ne	.num
	mov	x0,x19
	mov	x1,x21
	bl	str_eq
	eor	x0,x0,1
	eor	x0,x0,1
	b	.out
.eg:	mov	x0,x19
	mov	x1,x21
	bl	str_eq
	eor	x0,x0,1
	b	.out
.num:	mov	x0,x19
	bl	dec_signe
	mov	x22,x0
	mov	x0,x21
	bl	dec_signe
	cmp	x22,x0
	ldrb	w1,[x20,1]
	ldrb	w2,[x20,2]
	cmp	w1,'e'				; -eq
	b.ne	.ne
	cmp	x22,x0
	cset	x0,ne
	b	.out
.ne:	cmp	w1,'n'
	b.ne	.lt
	cmp	x22,x0
	cset	x0,eq
	b	.out
.lt:	cmp	w1,'l'
	b.ne	.gt
	cmp	w2,'t'
	b.ne	.le
	cmp	x22,x0
	cset	x0,ge
	b	.out
.le:	cmp	x22,x0
	cset	x0,gt
	b	.out
.gt:	cmp	w2,'t'
	b.ne	.ge
	cmp	x22,x0
	cset	x0,le
	b	.out
.ge:	cmp	x22,x0
	cset	x0,lt
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> x0 = nombre signe
dec_signe:
	mov	x1,0
	mov	x2,0
	ldrb	w3,[x0]
	cmp	w3,'-'
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

