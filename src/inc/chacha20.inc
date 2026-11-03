; ============================================================================
;  inc/chacha20.inc - le bloc ChaCha20 (RFC 8439, 2.3) : generateur aleatoire
;  du noyau (src/kernel/random.asm) et son essai (src/bin/rngtest).
;
;  chacha20_block : x0 = cle (32 o), w1 = compteur, x2 = nonce (12 o),
;  x3 = sortie (64 o). Garde x19-x28 (pile : 80 o).
; ============================================================================

macro cc_qr a*, b*, c*, d* {
	add	a,a,b
	eor	d,d,a
	ror	d,d,16
	add	c,c,d
	eor	b,b,c
	ror	b,b,20
	add	a,a,b
	eor	d,d,a
	ror	d,d,24
	add	c,c,d
	eor	b,b,c
	ror	b,b,25
}

chacha20_block:
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,64			; etat initial
	movl	w4,0x61707865			; "expand 32-byte k"
	movl	w5,0x3320646E
	movl	w6,0x79622D32
	movl	w7,0x6B206574
	stp	w4,w5,[sp]
	stp	w6,w7,[sp,8]
	ldp	x8,x9,[x0]			; cle
	stp	x8,x9,[sp,16]
	ldp	x8,x9,[x0,16]
	stp	x8,x9,[sp,32]
	str	w1,[sp,48]			; compteur, nonce
	ldr	w8,[x2]
	str	w8,[sp,52]
	ldr	w8,[x2,4]
	str	w8,[sp,56]
	ldr	w8,[x2,8]
	str	w8,[sp,60]
	ldp	w4,w5,[sp]			; s0..s15 -> w4..w19 (sauf w18 : w26)
	ldp	w6,w7,[sp,8]
	ldp	w8,w9,[sp,16]
	ldp	w10,w11,[sp,24]
	ldp	w12,w13,[sp,32]
	ldp	w14,w15,[sp,40]
	ldp	w16,w17,[sp,48]
	ldp	w26,w19,[sp,56]
	mov	x20,10				; 10 doubles tours
.r:	cc_qr	w4,w8,w12,w16			; colonnes
	cc_qr	w5,w9,w13,w17
	cc_qr	w6,w10,w14,w26
	cc_qr	w7,w11,w15,w19
	cc_qr	w4,w9,w14,w19			; diagonales
	cc_qr	w5,w10,w15,w16
	cc_qr	w6,w11,w12,w17
	cc_qr	w7,w8,w13,w26
	subs	x20,x20,1
	b.ne	.r
	ldp	w20,w21,[sp]			; + etat initial
	add	w4,w4,w20
	add	w5,w5,w21
	ldp	w20,w21,[sp,8]
	add	w6,w6,w20
	add	w7,w7,w21
	ldp	w20,w21,[sp,16]
	add	w8,w8,w20
	add	w9,w9,w21
	ldp	w20,w21,[sp,24]
	add	w10,w10,w20
	add	w11,w11,w21
	ldp	w20,w21,[sp,32]
	add	w12,w12,w20
	add	w13,w13,w21
	ldp	w20,w21,[sp,40]
	add	w14,w14,w20
	add	w15,w15,w21
	ldp	w20,w21,[sp,48]
	add	w16,w16,w20
	add	w17,w17,w21
	ldp	w20,w21,[sp,56]
	add	w26,w26,w20
	add	w19,w19,w21
	stp	w4,w5,[x3]
	stp	w6,w7,[x3,8]
	stp	w8,w9,[x3,16]
	stp	w10,w11,[x3,24]
	stp	w12,w13,[x3,32]
	stp	w14,w15,[x3,40]
	stp	w16,w17,[x3,48]
	stp	w26,w19,[x3,56]
	add	sp,sp,64
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ret
