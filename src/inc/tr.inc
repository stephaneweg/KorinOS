; ============================================================================
;  inc/tr.inc - lang_tr (AppKit) la ou les arguments sont deja en place : on
;  peut l'inserer entre deux lignes d'un appel. Tous les registres de travail
;  (x0 a x17 sauf le registre traduit) et les drapeaux sont gardes.
;
;	adr	x1,s_score
;	bl	tr_x1
;	bl	str_cat
;
;  Le programme a kit_use appkit ; x30 change : pas dans une feuille sans
;  prologue. A inclure dans le code (avant « segment readable writeable »).
; ============================================================================
	align 4

; x0 = texte -> x0 = sa traduction
tr_x0:
	prologue
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	stp	x5,x6,[sp,-16]!
	stp	x7,x8,[sp,-16]!
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	stp	x13,x14,[sp,-16]!
	stp	x15,x16,[sp,-16]!
	mrs	x1,nzcv
	stp	x1,x17,[sp,-16]!
	bl	lang_tr
	ldp	x1,x17,[sp],16
	msr	nzcv,x1
	ldp	x15,x16,[sp],16
	ldp	x13,x14,[sp],16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	ldp	x7,x8,[sp],16
	ldp	x5,x6,[sp],16
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	epilogue

macro tr_reg r {
tr_#r:
	stp	x0,x30,[sp,-16]!
	mov	x0,r
	bl	tr_x0
	mov	r,x0
	ldp	x0,x30,[sp],16
	ret
}
tr_reg x1
tr_reg x2
tr_reg x3
tr_reg x4
tr_reg x5

; x0 = chaines a la suite (un 0 de plus a la fin : menus de ui_popup), x1 = tampon :
; leurs traductions, de meme (la traduction d'une chaine seule n'a pas ce 0 de plus)
tr_block:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.l:	ldrb	w0,[x19]
	cbz	w0,.e
	mov	x0,x19
	bl	lang_tr
.c:	ldrb	w1,[x0],1
	strb	w1,[x20],1
	cbnz	w1,.c
.s:	ldrb	w0,[x19],1
	cbnz	w0,.s
	b	.l
.e:	strb	wzr,[x20]
	ldp	x19,x20,[sp],16
	epilogue
