; ============================================================================
;  svc/rdpd/sha256.inc - SHA-256 (FIPS 180-4) et HMAC-SHA256 (RFC 2104), pour
;  le defi de rdpd (le mot de passe de l'acces a distance). Le Cortex-A72 du
;  Pi 4 n'a pas les instructions SHA d'ARMv8 : tout en scalaire. Verifie
;  contre les vecteurs de la RFC 4231 (cles <= 64 octets) et de hashlib.
;
;	adr	x0,donnees
;	mov	x1,octets
;	adr	x2,sortie		; 32 octets
;	bl	sha256
;
;	adr	x0,cle			; 64 octets au plus (le mot de passe : 63)
;	mov	x1,longueur_cle
;	adr	x2,message		; 64 octets au plus
;	mov	x3,longueur_message
;	adr	x4,sortie		; 32 octets
;	bl	hmac_sha256
;
;  Ni tas ni donnees : tout sur la pile (sha256 : 160 octets, hmac : 192).
; ============================================================================

	align 4
; x0 = etat (8 mots), x1 = un bloc de 64 octets
sha_block:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,256			; W[0..63]
	mov	x2,0
.w16:	ldr	w3,[x1,x2,lsl 2]		; W[0..15] : le bloc, gros-boutiste
	rev	w3,w3
	str	w3,[sp,x2,lsl 2]
	add	x2,x2,1
	cmp	x2,16
	b.lo	.w16
.wx:	sub	x3,x2,2				; W[t] = s1(W[t-2]) + W[t-7]
	ldr	w4,[sp,x3,lsl 2]		;        + s0(W[t-15]) + W[t-16]
	ror	w5,w4,17
	eor	w5,w5,w4,ror 19
	eor	w5,w5,w4,lsr 10
	sub	x3,x2,15
	ldr	w4,[sp,x3,lsl 2]
	ror	w6,w4,7
	eor	w6,w6,w4,ror 18
	eor	w6,w6,w4,lsr 3
	add	w5,w5,w6
	sub	x3,x2,7
	ldr	w4,[sp,x3,lsl 2]
	add	w5,w5,w4
	sub	x3,x2,16
	ldr	w4,[sp,x3,lsl 2]
	add	w5,w5,w4
	str	w5,[sp,x2,lsl 2]
	add	x2,x2,1
	cmp	x2,64
	b.lo	.wx
	ldp	w19,w20,[x0]			; a b c d e f g h
	ldp	w21,w22,[x0,8]
	ldp	w23,w24,[x0,16]
	ldp	w25,w26,[x0,24]
	adr	x9,sha_k
	mov	x2,0
.r:	ror	w3,w23,6			; T1 = h + S1(e) + Ch(e,f,g) + K[t] + W[t]
	eor	w3,w3,w23,ror 11
	eor	w3,w3,w23,ror 25
	and	w4,w23,w24
	bic	w5,w25,w23
	orr	w4,w4,w5
	add	w3,w3,w4
	add	w3,w3,w26
	ldr	w4,[x9,x2,lsl 2]
	add	w3,w3,w4
	ldr	w4,[sp,x2,lsl 2]
	add	w3,w3,w4
	ror	w4,w19,2			; T2 = S0(a) + Maj(a,b,c)
	eor	w4,w4,w19,ror 13
	eor	w4,w4,w19,ror 22
	and	w5,w19,w20
	and	w6,w19,w21
	eor	w5,w5,w6
	and	w6,w20,w21
	eor	w5,w5,w6
	add	w4,w4,w5
	mov	w26,w25
	mov	w25,w24
	mov	w24,w23
	add	w23,w22,w3
	mov	w22,w21
	mov	w21,w20
	mov	w20,w19
	add	w19,w3,w4
	add	x2,x2,1
	cmp	x2,64
	b.lo	.r
	ldp	w3,w4,[x0]			; etat += a..h
	add	w3,w3,w19
	add	w4,w4,w20
	stp	w3,w4,[x0]
	ldp	w3,w4,[x0,8]
	add	w3,w3,w21
	add	w4,w4,w22
	stp	w3,w4,[x0,8]
	ldp	w3,w4,[x0,16]
	add	w3,w3,w23
	add	w4,w4,w24
	stp	w3,w4,[x0,16]
	ldp	w3,w4,[x0,24]
	add	w3,w3,w25
	add	w4,w4,w26
	stp	w3,w4,[x0,24]
	add	sp,sp,256
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = donnees, x1 = octets, x2 = sortie (32 octets)
sha256:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,160			; etat (32), puis la fin (2 blocs)
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x3,sha_h0
	ldp	x4,x5,[x3]
	ldp	x6,x7,[x3,16]
	stp	x4,x5,[sp]
	stp	x6,x7,[sp,16]
	mov	x22,x20				; reste
.full:	cmp	x22,64
	b.lo	.tail
	mov	x0,sp
	mov	x1,x19
	bl	sha_block
	add	x19,x19,64
	sub	x22,x22,64
	b	.full
.tail:	add	x23,sp,32
	mov	x3,0
.z:	str	xzr,[x23,x3]
	add	x3,x3,8
	cmp	x3,128
	b.lo	.z
	mov	x3,0
.cp:	cmp	x3,x22
	b.hs	.pad
	ldrb	w4,[x19,x3]
	strb	w4,[x23,x3]
	add	x3,x3,1
	b	.cp
.pad:	mov	w4,0x80
	strb	w4,[x23,x22]
	mov	x24,64				; 1 bloc, ou 2 s'il reste 56 octets ou plus
	cmp	x22,56
	b.lo	.len
	mov	x24,128
.len:	lsl	x3,x20,3			; la longueur en bits, gros-boutiste
	rev	x3,x3
	sub	x4,x24,8
	str	x3,[x23,x4]
	mov	x0,sp
	mov	x1,x23
	bl	sha_block
	cmp	x24,128
	b.ne	.out
	mov	x0,sp
	add	x1,x23,64
	bl	sha_block
.out:	mov	x3,0
.o:	ldr	w4,[sp,x3,lsl 2]
	rev	w4,w4
	str	w4,[x21,x3,lsl 2]
	add	x3,x3,1
	cmp	x3,8
	b.lo	.o
	add	sp,sp,160
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = cle (<= 64 octets), x1 = sa longueur, x2 = message (<= 64 octets),
; x3 = sa longueur, x4 = sortie (32 octets)
hmac_sha256:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,192			; (cle ^ pad) || message, puis le hache interne
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	w0,0x36				; interne : (cle ^ ipad) || message
	bl	.key
	mov	x5,0
.m:	cmp	x5,x22
	b.hs	.m1
	ldrb	w6,[x21,x5]
	add	x7,x5,64
	strb	w6,[sp,x7]
	add	x5,x5,1
	b	.m
.m1:	mov	x0,sp
	add	x1,x22,64
	add	x2,sp,128
	bl	sha256
	mov	w0,0x5C				; externe : (cle ^ opad) || interne
	bl	.key
	ldp	x5,x6,[sp,128]
	stp	x5,x6,[sp,64]
	ldp	x5,x6,[sp,144]
	stp	x5,x6,[sp,80]
	mov	x0,sp
	mov	x1,96
	mov	x2,x23
	bl	sha256
	add	sp,sp,192
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
; w0 = pad : [sp .. sp+63] de hmac_sha256 (meme sp : bl ne pousse rien ; x30
; est deja garde par son prologue) = cle ^ pad, completee de zeros
.key:	mov	x5,0
.k:	mov	w6,0
	cmp	x5,x20
	b.hs	.kp
	ldrb	w6,[x19,x5]
.kp:	eor	w6,w6,w0
	strb	w6,[sp,x5]
	add	x5,x5,1
	cmp	x5,64
	b.lo	.k
	ret

	align 8
sha_h0	dw	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a
	dw	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
sha_k	dw	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
	dw	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
	dw	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
	dw	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
	dw	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
	dw	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
	dw	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
	dw	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	align 4
