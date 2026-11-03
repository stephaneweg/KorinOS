; ============================================================================
;  appsA_util.inc - utilitaires communs a terminal, editeur et fichiers
;  (chaines, UTF-8, chemins). A inclure dans le segment de code, apres lib.inc.
; ============================================================================

; x0 = chaine -> x0 = longueur en octets
str_len:
	mov	x1,x0
.c:	ldrb	w2,[x1],1
	cbnz	w2,.c
	sub	x0,x1,x0
	sub	x0,x0,1
	ret

; x0 = chaine 1, x1 = chaine 2 -> x0 = 1 si egales
str_eq:
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.ne	.no
	cbnz	w2,.c
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = destination, x1 = source, x2 = octets
mem_move:
	cbz	x2,.done
	cmp	x0,x1
	b.hi	.back
.f:	cmp	x2,32				; grands blocs : par 16 octets
	b.lo	.fb
	sub	x3,x1,x0
	cmp	x3,16
	b.lo	.fb
.f16:	ldp	x3,x16,[x1],16
	stp	x3,x16,[x0],16
	sub	x2,x2,16
	cmp	x2,16
	b.hs	.f16
	cbz	x2,.done
.fb:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.fb
	ret
.back:	add	x0,x0,x2
	add	x1,x1,x2
.b:	ldrb	w3,[x1,-1]!
	strb	w3,[x0,-1]!
	subs	x2,x2,1
	b.ne	.b
.done:	ret

; w0 = point de code, x1 = destination -> x0 = octets ecrits (1..3)
utf8_put:
	cmp	w0,0x80
	b.hs	.two
	strb	w0,[x1]
	mov	x0,1
	ret
.two:	cmp	w0,0x800
	b.hs	.three
	lsr	w2,w0,6
	orr	w2,w2,0xC0
	strb	w2,[x1]
	and	w2,w0,0x3F
	orr	w2,w2,0x80
	strb	w2,[x1,1]
	mov	x0,2
	ret
.three:	lsr	w2,w0,12
	orr	w2,w2,0xE0
	strb	w2,[x1]
	ubfx	w2,w0,6,6
	orr	w2,w2,0x80
	strb	w2,[x1,1]
	and	w2,w0,0x3F
	orr	w2,w2,0x80
	strb	w2,[x1,2]
	mov	x0,3
	ret

; x0 = destination, x1 = repertoire, x2 = nom : "rep/nom" (racine "/" geree)
path_join:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x2
	bl	str_cat
	ldurb	w1,[x0,-1]
	cmp	w1,'/'
	b.eq	.sep
	mov	w1,'/'
	strb	w1,[x0],1
.sep:	mov	x1,x19
	bl	str_cat
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin (modifie en place) : retire le dernier composant ("SD0:/a" -> "/sd",
; "/sd" -> "/")
path_parent:
	mov	x1,x0
	mov	x2,x0				; dernier '/'
.c:	ldrb	w3,[x1]
	cbz	w3,.end
	cmp	w3,'/'
	b.ne	.n
	mov	x2,x1
.n:	add	x1,x1,1
	b	.c
.end:	cmp	x2,x0
	b.eq	.ret				; (pas de « / »)
	ldurb	w3,[x2,-1]			; « VOL:/x » -> « VOL:/ » : on garde le
	cmp	w3,':'				; « / » de la racine du volume
	b.ne	.cut
	add	x2,x2,1
.cut:	strb	wzr,[x2]
.ret:	ret

; x0 = chaine -> x0 = valeur decimale (non signee), 0 si rien
parse_dec:
	mov	x1,0
	mov	x3,10
.c:	ldrb	w2,[x0],1
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.end
	madd	x1,x1,x3,x2
	b	.c
.end:	mov	x0,x1
	ret

; x0 = chaine, x1 = suffixe -> x0 = 1 si la chaine se termine par le suffixe
; (insensible a la casse ASCII)
str_ends:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	str_len
	mov	x21,x0
	mov	x0,x20
	bl	str_len
	cmp	x0,x21
	b.hi	.no
	sub	x1,x21,x0
	add	x1,x19,x1
	mov	x2,x20
.c:	ldrb	w3,[x1],1
	ldrb	w4,[x2],1
	cbz	w4,.yes
	orr	w3,w3,0x20
	orr	w4,w4,0x20
	cmp	w3,w4
	b.eq	.c
.no:	mov	x0,0
	b	.out
.yes:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
