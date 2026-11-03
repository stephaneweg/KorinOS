; ============================================================================
;  kits/audiokit/ak_bits.inc - lecteur de bits, poids fort d'abord (FLAC, MP3)
;
;  Etat (BR_*, 48 octets) : les bits a lire sont cadres a gauche dans un
;  registre de 64 bits (BR_CACHE), BR_N bits valides (les autres a zero).
;  Au-dela de la fin des donnees, on lit des zeros.
; ============================================================================

BR_PTR		= 0		; prochain octet a charger
BR_END		= 8
BR_CACHE	= 16
BR_N		= 24
BR_START	= 32
BR_SIZE		= 48

; x0 = lecteur, x1 = donnees, x2 = octets
br_init:
	str	x1,[x0,BR_PTR]
	str	x1,[x0,BR_START]
	add	x2,x1,x2
	str	x2,[x0,BR_END]
	str	xzr,[x0,BR_CACHE]
	str	xzr,[x0,BR_N]
	ret

; x0 = lecteur ; x2 = cache, x3 = bits valides (lus et rendus a jour en
; memoire) : remplit le cache (au moins 57 bits). Ne touche que x2-x7.
br_fill:
	ldr	x4,[x0,BR_PTR]
	ldr	x5,[x0,BR_END]
	add	x6,x4,8
	cmp	x6,x5
	b.hi	.bytes
	ldr	x6,[x4]				; 8 octets d'un coup
	rev	x6,x6
	mov	x7,64
	sub	x7,x7,x3
	lsr	x7,x7,3				; octets entiers qui tiennent
	add	x4,x4,x7
	lsr	x6,x6,x3
	lsl	x7,x7,3
	add	x3,x3,x7			; bits valides
	mov	x7,64
	sub	x7,x7,x3			; bits partiels a effacer
	mov	x5,-1
	lsl	x5,x5,x7
	cmp	x3,64
	csinv	x5,x5,xzr,ne			; (64 bits pleins : rien a effacer)
	and	x6,x6,x5
	orr	x2,x2,x6
	b	.st
.bytes:	cmp	x3,56				; un octet a la fois (fin des donnees)
	b.hi	.st
	mov	x6,0
	cmp	x4,x5
	b.hs	.z
	ldrb	w6,[x4]
.z:	add	x4,x4,1
	mov	x7,56
	sub	x7,x7,x3
	lsl	x6,x6,x7
	orr	x2,x2,x6
	add	x3,x3,8
	b	.bytes
.st:	str	x4,[x0,BR_PTR]
	str	x2,[x0,BR_CACHE]
	str	x3,[x0,BR_N]
	ret

; x0 = lecteur, x1 = bits (0..32) -> x0 = valeur ; ne touche que x0-x7
br_bits:
	ldr	x2,[x0,BR_CACHE]
	ldr	x3,[x0,BR_N]
	cmp	x3,x1
	b.hs	.ok
	stp	x29,x30,[sp,-16]!
	bl	br_fill
	ldp	x29,x30,[sp],16
.ok:	cbz	x1,.zero
	mov	x4,64
	sub	x4,x4,x1
	lsr	x4,x2,x4
	lsl	x2,x2,x1
	sub	x3,x3,x1
	str	x2,[x0,BR_CACHE]
	str	x3,[x0,BR_N]
	mov	x0,x4
	ret
.zero:	mov	x0,0
	ret

; x0 = lecteur, x1 = bits (1..32) -> x0 = valeur signee (complement a 2)
br_sbits:
	mov	x8,x30
	mov	x9,x1
	bl	br_bits
	mov	x30,x8
	mov	x1,64
	sub	x1,x1,x9
	lsl	x0,x0,x1
	asr	x0,x0,x1
	ret

; x0 = lecteur -> x0 = nombre de 0 avant le prochain 1 (le 1 est lu)
; ne touche que x0-x9
br_unary:
	mov	x8,x30
	mov	x9,0
	ldr	x2,[x0,BR_CACHE]
	ldr	x3,[x0,BR_N]
.l:	cbnz	x2,.one
	add	x9,x9,x3			; que des 0 : tout prendre
	mov	x3,0
	ldr	x4,[x0,BR_PTR]
	ldr	x5,[x0,BR_END]
	add	x5,x5,8
	cmp	x4,x5
	b.hi	.end				; (au-dela de la fin : arret)
	bl	br_fill
	b	.l
.one:	clz	x4,x2
	add	x9,x9,x4
	lsl	x2,x2,x4
	lsl	x2,x2,1
	add	x4,x4,1
	sub	x3,x3,x4
	str	x2,[x0,BR_CACHE]
	str	x3,[x0,BR_N]
.end:	mov	x0,x9
	mov	x30,x8
	ret

; x0 = lecteur, x1 = bits a sauter (quelconque)
br_skip:
	mov	x10,x30
	mov	x11,x0
	mov	x12,x1
.l:	cbz	x12,.ret
	mov	x1,32
	cmp	x12,x1
	csel	x1,x12,x1,lo
	sub	x12,x12,x1
	mov	x0,x11
	bl	br_bits
	b	.l
.ret:	mov	x30,x10
	ret

; x0 = lecteur : jusqu'au prochain octet
br_align:
	ldr	x3,[x0,BR_N]
	and	x1,x3,7
	ldr	x2,[x0,BR_CACHE]
	lsl	x2,x2,x1
	sub	x3,x3,x1
	str	x2,[x0,BR_CACHE]
	str	x3,[x0,BR_N]
	ret

; x0 = lecteur -> x0 = bits lus depuis le debut
br_tell:
	ldr	x1,[x0,BR_PTR]
	ldr	x2,[x0,BR_START]
	sub	x1,x1,x2
	ldr	x3,[x0,BR_N]
	lsl	x1,x1,3
	sub	x0,x1,x3
	ret

; ---------------------------------------------------------------------------
;  Lecteur en registres (boucles chaudes : FLAC, MP3) : x9 = cache (bits a
;  lire cadres a gauche, le reste a zero), x10 = bits valides, x11 =
;  prochain octet, x12 = fin des donnees. Les fonctions rb_* ne touchent
;  que x0, x1, x8, x16, x17 (et l'etat).
; ---------------------------------------------------------------------------

; x0 = debut, x1 = octets : etat du lecteur en registres
macro rb_init {
	mov	x11,x0
	add	x12,x0,x1
	mov	x9,0
	mov	x10,0
}

; remplit le cache (au moins 57 bits, sauf a la fin) ; ne touche que x16, x17
rb_fill:
	add	x16,x11,8
	cmp	x16,x12
	b.hi	.bytes
	ldr	x16,[x11]			; 8 octets d'un coup
	rev	x16,x16
	mov	x17,64
	sub	x17,x17,x10
	lsr	x17,x17,3			; octets entiers qui tiennent
	add	x11,x11,x17
	lsr	x16,x16,x10
	add	x10,x10,x17,lsl 3
	mov	x17,64
	sub	x17,x17,x10			; bits partiels a effacer (0..7)
	lsr	x16,x16,x17
	lsl	x16,x16,x17
	orr	x9,x9,x16
	ret
.bytes:	cmp	x10,56
	b.hi	.ret
	mov	x16,0
	cmp	x11,x12
	b.hs	.z
	ldrb	w16,[x11]
.z:	add	x11,x11,1
	mov	x17,56
	sub	x17,x17,x10
	lsl	x16,x16,x17
	orr	x9,x9,x16
	add	x10,x10,8
	b	.bytes
.ret:	ret

; x1 = bits (1..32) -> x0 = valeur
rb_get:
	cmp	x10,x1
	b.hs	.ok
	mov	x8,x30
	bl	rb_fill
	mov	x30,x8
.ok:	mov	x17,64
	sub	x17,x17,x1
	lsr	x0,x9,x17
	lsl	x9,x9,x1
	sub	x10,x10,x1
	ret

; x1 = bits (0..32) -> x0 = valeur signee
rb_sget:
	cbz	x1,.zero
	cmp	x10,x1
	b.hs	.ok
	mov	x8,x30
	bl	rb_fill
	mov	x30,x8
.ok:	mov	x17,64
	sub	x17,x17,x1
	asr	x0,x9,x17
	lsl	x9,x9,x1
	sub	x10,x10,x1
	ret
.zero:	mov	x0,0
	ret

; -> x0 = nombre de 0 avant le prochain 1 (le 1 est lu)
rb_unary:
	mov	x8,x30
	mov	x0,0
.l:	clz	x16,x9				; (au-dela des bits valides : des 0)
	cmp	x16,x10
	b.lo	.one
	add	x0,x0,x10
	mov	x9,0
	mov	x10,0
	add	x16,x12,8
	cmp	x11,x16
	b.hi	.end				; au-dela de la fin : arret
	bl	rb_fill
	b	.l
.one:	add	x0,x0,x16
	lsl	x9,x9,x16			; (en deux fois : un decalage de 64
	lsl	x9,x9,1				;  ne ferait rien)
	sub	x10,x10,x16
	sub	x10,x10,1
.end:	mov	x30,x8
	ret

; jusqu'au prochain octet
macro rb_align {
	and	x16,x10,7
	lsl	x9,x9,x16
	sub	x10,x10,x16
}

; -> x0 = adresse de l'octet suivant les bits lus (apres rb_align)
macro rb_byteptr {
	sub	x0,x11,x10,lsr 3
}
