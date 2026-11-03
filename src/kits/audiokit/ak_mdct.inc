; ============================================================================
;  kits/audiokit/ak_mdct.inc - FFT complexe a radix mixtes (2, 3, 5) et IMDCT
;  par une FFT de N/4 points (Vorbis : N = 64..8192 ; Opus/CELT : N = 2 x
;  120..960, facteurs 2, 3, 5) ; sinus et cosinus en double precision pour
;  les tables.
;
;  FFT (decimation temporelle iterative) : l'entree est rangee a la place
;  de ses chiffres inverses (FT_POS), puis chaque etage de radix p combine
;  des sous-transformees de taille m en blocs de taille L = m p :
;	y[b + j + k m] = somme_q (y[b + j + q m] W_L^(j q)) W_p^(q k)
;  IMDCT de N points (N / 2 coefficients X, sortie y[0..N-1], sans fenetre) :
;	y[n] = somme_k X[k] cos(2 pi / N (n + 1/2 + N/4)(k + 1/2))
;  z[k] = (X[2k] + i X[N/2-1-2k]) e^(-i t_k), t_k = 2 pi (k + 1/8) / N,
;  Z = FFT(z) e^(-i t_k), puis y d'apres les quarts de Z (ak_imdct).
; ============================================================================

FT_N		= 0
FT_NF		= 8		; etages
FT_F		= 16		; radix de chaque etage (ordre de calcul), 16 au plus
FT_POS		= 144		; rangs de l'entree (32 bits, n)
FT_W		= 152		; W_n^k = e^(-2 pi i k / n), k < n (complexes)
FT_SIZE		= 160

MD_N		= 0
MD_N4		= 8
MD_TW		= 16		; e^(-i t_k) : (cos, sin) x N/4
MD_Z		= 24		; tampons complexes de N/4 (2)
MD_Z2		= 32
MD_FFT		= 64		; plan de la FFT de N/4 (FT_SIZE)
MD_SIZE		= 64 + FT_SIZE

; d0 = x -> d0 = sin x, d1 = cos x (double ; |x| < 2^20)
ak_sincos:
	adr	x0,.c
	ldr	d2,[x0]				; 2 / pi
	fmul	d3,d0,d2
	frintn	d3,d3				; k
	fcvtzs	x1,d3
	ldr	d2,[x0,8]			; pi/2 en deux parties
	fmsub	d0,d3,d2,d0
	ldr	d2,[x0,16]
	fmsub	d0,d3,d2,d0			; r dans [-pi/4, pi/4]
	fmul	d4,d0,d0			; r^2
	; sin r = r (1 + r^2 (s1 + r^2 (s2 + ...)))
	ldr	d5,[x0,24+6*8]
	mov	x2,5
	add	x3,x0,24+5*8
.s:	ldr	d6,[x3],-8
	fmadd	d5,d5,d4,d6
	subs	x2,x2,1
	b.ne	.s
	fmul	d5,d5,d4
	fmadd	d5,d5,d0,d0			; sin r
	; cos r = 1 + r^2 (c1 + r^2 (c2 + ...))
	ldr	d6,[x0,24+8*8+6*8]
	mov	x2,5
	add	x3,x0,24+8*8+5*8
.co:	ldr	d7,[x3],-8
	fmadd	d6,d6,d4,d7
	subs	x2,x2,1
	b.ne	.co
	fmul	d6,d6,d4
	fmov	d7,1.0
	fadd	d6,d6,d7			; cos r
	; quadrant
	and	x1,x1,3
	cmp	x1,1
	b.eq	.q1
	cmp	x1,2
	b.eq	.q2
	cmp	x1,3
	b.eq	.q3
	fmov	d0,d5
	fmov	d1,d6
	ret
.q1:	fmov	d0,d6				; sin(r + pi/2) = cos r
	fneg	d1,d5
	ret
.q2:	fneg	d0,d5
	fneg	d1,d6
	ret
.q3:	fneg	d0,d6
	fmov	d1,d5
	ret
		align	8
.c:	dd	0.63661977236758134308		; 2 / pi
	dd	1.5707963267341256		; pi/2 (tete)
	dd	6.077100506506192e-11		; pi/2 (reste)
	; sinus : s1..s6 (-1/3!, 1/5!, ...), puis 1/15! (s7)
	dd	0.0, -0.16666666666666666, 0.008333333333333333, -0.0001984126984126984
	dd	2.7557319223985893e-06, -2.505210838544172e-08, 1.6059043836821613e-10
	dd	-7.647163731819816e-13
	; cosinus : c1..c7 (-1/2!, 1/4!, ...)
	dd	0.0, -0.5, 0.041666666666666664, -0.001388888888888889
	dd	2.48015873015873e-05, -2.755731922398589e-07, 2.08767569878681e-09
	dd	-1.1470745597729725e-11

; x0 = decodeur (memoire), x1 = plan (FT_SIZE), x2 = n -> x0 = 0 / < 0
ak_fft_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	str	x21,[x20,FT_N]
	; facteurs (2, 3, 5), ranges a l'envers dans FT_F (ordre de calcul)
	mov	x22,0				; nombre
	mov	x0,x21
	sub	sp,sp,128
.f2:	tst	x0,1
	b.ne	.f3
	cmp	x0,1
	b.eq	.fd
	mov	x1,2
	str	x1,[sp,x22,lsl 3]
	add	x22,x22,1
	lsr	x0,x0,1
	b	.f2
.f3:	mov	x1,3
	udiv	x2,x0,x1
	msub	x3,x2,x1,x0
	cbnz	x3,.f5
	str	x1,[sp,x22,lsl 3]
	add	x22,x22,1
	mov	x0,x2
	b	.f3
.f5:	mov	x1,5
	udiv	x2,x0,x1
	msub	x3,x2,x1,x0
	cbnz	x3,.fe
	str	x1,[sp,x22,lsl 3]
	add	x22,x22,1
	mov	x0,x2
	b	.f5
.fe:	cmp	x0,1
	b.ne	.bad
.fd:	cmp	x22,16
	b.hi	.bad
	str	x22,[x20,FT_NF]
	mov	x0,0				; FT_F[s] = f[nf - 1 - s]
.rv:	cmp	x0,x22
	b.hs	.rvd
	sub	x1,x22,1
	sub	x1,x1,x0
	ldr	x2,[sp,x1,lsl 3]
	add	x3,x20,FT_F
	str	x2,[x3,x0,lsl 3]
	add	x0,x0,1
	b	.rv
.rvd:	; rangs : pos(i) = somme d_s * n / (f_0 ... f_s)
	mov	x0,x19
	lsl	x1,x21,2
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,FT_POS]
	mov	x23,0				; i
.pi:	cmp	x23,x21
	b.hs	.pd
	mov	x0,x23				; x
	mov	x1,x21				; mult
	mov	x2,0				; r
	mov	x3,0				; s
.pd2:	cmp	x3,x22
	b.hs	.pst
	ldr	x4,[sp,x3,lsl 3]		; f_s (ordre d'origine)
	udiv	x5,x0,x4
	msub	x6,x5,x4,x0			; chiffre
	mov	x0,x5
	udiv	x1,x1,x4
	madd	x2,x6,x1,x2
	add	x3,x3,1
	b	.pd2
.pst:	ldr	x4,[x20,FT_POS]
	str	w2,[x4,x23,lsl 2]
	add	x23,x23,1
	b	.pi
.pd:	; W_n^k
	mov	x0,x19
	lsl	x1,x21,3
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,FT_W]
	mov	x23,x0
	scvtf	d8,x21
	adr	x0,ft_2pi
	ldr	d9,[x0]
	fdiv	d9,d9,d8			; 2 pi / n
	mov	x24,0
.w:	cmp	x24,x21
	b.hs	.ok
	scvtf	d0,x24
	fmul	d0,d0,d9
	fneg	d0,d0
	bl	ak_sincos
	fcvt	s1,d1				; cos
	fcvt	s0,d0				; -sin
	stp	s1,s0,[x23],8
	add	x24,x24,1
	b	.w
.ok:	mov	x0,0
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.bad:	mov	x0,AKE_INVAL
.out:	add	sp,sp,128
	ldp	d8,d9,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

		align	8
ft_2pi		dd	6.283185307179586
ft_c3		dw	0xBF000000, 0x3F5DB3D7	; -1/2, sin(pi/3)
ft_c5		dw	0x3E9E3779, 0x3F737871	; cos(2pi/5), sin(2pi/5)
		dw	0xBF4F1BBD, 0x3F167918	; cos(4pi/5), sin(4pi/5)

; x0 = plan, x1 = entree (complexes, n), x2 = sortie (complexes, n ; autre
; tampon) : y = FFT(x), e^(-2 pi i k n / N). Ne garde que x19-x28, v8-v15.
ak_fft:
	stp	x29,x30,[sp,-16]!
	ldr	x3,[x0,FT_N]
	ldr	x4,[x0,FT_POS]
	mov	x5,0
.sc:	ldr	d0,[x1,x5,lsl 3]		; rangement : y[pos(i)] = x[i]
	ldr	w6,[x4,x5,lsl 2]
	str	d0,[x2,x6,lsl 3]
	add	x5,x5,1
	cmp	x5,x3
	b.lo	.sc
	ldr	x7,[x0,FT_NF]
	ldr	x8,[x0,FT_W]
	add	x9,x0,FT_F
	mov	x10,1				; L (taille des sous-transformees)
.stage:	cbz	x7,.ret
	ldr	x11,[x9],8			; p
	mov	x12,x10				; m
	mul	x10,x10,x11			; L
	udiv	x13,x3,x10			; pas de W : n / L
	mov	x14,0				; b
.blk:	cmp	x14,x3
	b.hs	.nst
	mov	x15,0				; j
.j:	cmp	x15,x12
	b.hs	.nblk
	add	x16,x14,x15
	add	x16,x2,x16,lsl 3		; &y[b + j]
	lsl	x17,x12,3			; pas : m complexes
	mul	x6,x15,x13			; indice de W^j
	cmp	x11,2
	b.eq	.r2
	cmp	x11,3
	b.eq	.r3
	; radix 5
	ldr	d0,[x16]			; v0
	add	x1,x16,x17
	ldr	d1,[x1]				; v1..v4 x W^(jq)
	add	x5,x6,x6
	cbz	x6,.r5nw
	ldr	d16,[x8,x6,lsl 3]
	bl	.cmul1
	add	x1,x1,x17
	ldr	d2,[x1]
	ldr	d16,[x8,x5,lsl 3]
	bl	.cmul2
	add	x1,x1,x17
	ldr	d3,[x1]
	add	x4,x5,x6
	ldr	d16,[x8,x4,lsl 3]
	bl	.cmul3
	add	x1,x1,x17
	ldr	d4,[x1]
	add	x4,x4,x6
	ldr	d16,[x8,x4,lsl 3]
	bl	.cmul4
	b	.r5k
.r5nw:	add	x1,x1,x17
	ldr	d2,[x1]
	add	x1,x1,x17
	ldr	d3,[x1]
	add	x1,x1,x17
	ldr	d4,[x1]
.r5k:	; y0 = v0 + v1 + v2 + v3 + v4 ; yk = v0 + sum_q vq e^(-2 pi i q k / 5)
	; a = v1 + v4, b = v1 - v4, c = v2 + v3, d = v2 - v3
	fadd	v20.2s,v1.2s,v4.2s		; a
	fsub	v21.2s,v1.2s,v4.2s		; b
	fadd	v22.2s,v2.2s,v3.2s		; c
	fsub	v23.2s,v2.2s,v3.2s		; d
	fadd	v24.2s,v0.2s,v20.2s
	fadd	v24.2s,v24.2s,v22.2s		; y0
	adr	x4,ft_c5
	ldp	s25,s26,[x4]			; c1, s1
	ldp	s27,s28,[x4,8]			; c2, s2
	; y1 = v0 + c1 a + c2 c - i (s1 b + s2 d)
	; y4 = v0 + c1 a + c2 c + i (s1 b + s2 d)
	; y2 = v0 + c2 a + c1 c - i (s2 b - s1 d)
	; y3 = v0 + c2 a + c1 c + i (s2 b - s1 d)
	fmul	v29.2s,v20.2s,v25.s[0]
	fmla	v29.2s,v22.2s,v27.s[0]
	fadd	v29.2s,v29.2s,v0.2s		; r1
	fmul	v30.2s,v21.2s,v26.s[0]
	fmla	v30.2s,v23.2s,v28.s[0]		; t1 = s1 b + s2 d
	fmul	v31.2s,v20.2s,v27.s[0]
	fmla	v31.2s,v22.2s,v25.s[0]
	fadd	v31.2s,v31.2s,v0.2s		; r2
	fmul	v5.2s,v21.2s,v28.s[0]
	fmls	v5.2s,v23.2s,v26.s[0]		; t2 = s2 b - s1 d
	; -i (x + i y) = y - i x : (t.im, -t.re)
	rev64	v6.2s,v30.2s			; (t1.im, t1.re)
	fneg	v7.2s,v6.2s
	ins	v6.s[1],v7.s[1]			; (t1.im, -t1.re) = -i t1
	rev64	v7.2s,v5.2s
	fneg	v16.2s,v7.2s
	ins	v7.s[1],v16.s[1]		; -i t2
	str	d24,[x16]
	add	x1,x16,x17
	fadd	v16.2s,v29.2s,v6.2s
	str	d16,[x1]			; y1
	add	x1,x1,x17
	fadd	v16.2s,v31.2s,v7.2s
	str	d16,[x1]			; y2
	add	x1,x1,x17
	fsub	v16.2s,v31.2s,v7.2s
	str	d16,[x1]			; y3
	add	x1,x1,x17
	fsub	v16.2s,v29.2s,v6.2s
	str	d16,[x1]			; y4
	b	.nj
.r3:	ldr	d0,[x16]
	add	x1,x16,x17
	ldr	d1,[x1]
	add	x4,x1,x17
	ldr	d2,[x4]
	cbz	x6,.r3k
	ldr	d16,[x8,x6,lsl 3]
	bl	.cmul1
	add	x5,x6,x6
	ldr	d16,[x8,x5,lsl 3]
	bl	.cmul2
.r3k:	; y0 = v0 + v1 + v2 ; y1,2 = v0 - (v1 + v2)/2 -/+ i sin60 (v1 - v2)
	fadd	v20.2s,v1.2s,v2.2s
	fsub	v21.2s,v1.2s,v2.2s
	fadd	v24.2s,v0.2s,v20.2s
	adr	x5,ft_c3
	ldp	s25,s26,[x5]
	fmla	v0.2s,v20.2s,v25.s[0]		; v0 - (v1+v2)/2
	fmul	v21.2s,v21.2s,v26.s[0]
	rev64	v6.2s,v21.2s
	fneg	v7.2s,v6.2s
	ins	v6.s[1],v7.s[1]			; -i t
	str	d24,[x16]
	fadd	v16.2s,v0.2s,v6.2s
	str	d16,[x1]
	fsub	v16.2s,v0.2s,v6.2s
	str	d16,[x4]
	b	.nj
.r2:	ldr	d0,[x16]
	add	x1,x16,x17
	ldr	d1,[x1]
	cbz	x6,.r2k
	ldr	d16,[x8,x6,lsl 3]
	bl	.cmul1
.r2k:	fadd	v2.2s,v0.2s,v1.2s
	fsub	v3.2s,v0.2s,v1.2s
	str	d2,[x16]
	str	d3,[x1]
.nj:	add	x15,x15,1
	b	.j
.nblk:	add	x14,x14,x10
	b	.blk
.nst:	sub	x7,x7,1
	b	.stage
.ret:	ldp	x29,x30,[sp],16
	ret
; v1..v4 *= d16 (complexe) ; ne touche que v17-v19
.cmul1:	fmul	v17.2s,v1.2s,v16.s[0]		; (a + ib)(c + id) = (ac - bd) + i(ad + bc)
	fmul	v18.2s,v1.2s,v16.s[1]		; (ad, bd)
	rev64	v18.2s,v18.2s			; (bd, ad)
	fneg	v19.2s,v18.2s
	ins	v18.s[0],v19.s[0]		; (-bd, ad)
	fadd	v1.2s,v17.2s,v18.2s
	ret
.cmul2:	fmul	v17.2s,v2.2s,v16.s[0]
	fmul	v18.2s,v2.2s,v16.s[1]
	rev64	v18.2s,v18.2s
	fneg	v19.2s,v18.2s
	ins	v18.s[0],v19.s[0]
	fadd	v2.2s,v17.2s,v18.2s
	ret
.cmul3:	fmul	v17.2s,v3.2s,v16.s[0]
	fmul	v18.2s,v3.2s,v16.s[1]
	rev64	v18.2s,v18.2s
	fneg	v19.2s,v18.2s
	ins	v18.s[0],v19.s[0]
	fadd	v3.2s,v17.2s,v18.2s
	ret
.cmul4:	fmul	v17.2s,v4.2s,v16.s[0]
	fmul	v18.2s,v4.2s,v16.s[1]
	rev64	v18.2s,v18.2s
	fneg	v19.2s,v18.2s
	ins	v18.s[0],v19.s[0]
	fadd	v4.2s,v17.2s,v18.2s
	ret

; x0 = decodeur, x1 = plan d'IMDCT (MD_SIZE), x2 = N -> 0 / < 0
ak_imdct_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	str	x2,[x20,MD_N]
	lsr	x21,x2,2
	str	x21,[x20,MD_N4]
	mov	x0,x19
	add	x1,x20,MD_FFT
	mov	x2,x21
	bl	ak_fft_init
	tbnz	x0,63,.out
	mov	x0,x19				; tampons et rotations
	lsl	x1,x21,3
	add	x1,x1,x1,lsl 1			; 3 x N/4 complexes
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,MD_TW]
	add	x1,x0,x21,lsl 3
	str	x1,[x20,MD_Z]
	add	x1,x1,x21,lsl 3
	str	x1,[x20,MD_Z2]
	mov	x22,x0
	ldr	x0,[x20,MD_N]
	scvtf	d8,x0
	adr	x0,ft_2pi
	ldr	d9,[x0]
	fdiv	d9,d9,d8			; 2 pi / N
	mov	x19,0
.t:	cmp	x19,x21
	b.hs	.ok
	scvtf	d0,x19
	fmov	d1,0.125
	fadd	d0,d0,d1
	fmul	d0,d0,d9
	bl	ak_sincos
	fcvt	s1,d1
	fcvt	s0,d0
	stp	s1,s0,[x22],8			; (cos t, sin t)
	add	x19,x19,1
	b	.t
.ok:	mov	x0,0
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	d8,d9,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = plan, x1 = X (N/2 flottants), x2 = y (N flottants) : IMDCT sans fenetre
ak_imdct:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldr	x3,[x19,MD_N4]
	ldr	x4,[x19,MD_TW]
	ldr	x5,[x19,MD_Z]
	ldr	x6,[x19,MD_N]
	lsr	x6,x6,1				; N/2
	; z[k] = (X[2k] + i X[N/2-1-2k]) e^(-i t)
	mov	x7,0
.pre:	lsl	x8,x7,1
	ldr	s0,[x20,x8,lsl 2]		; re
	sub	x9,x6,1
	sub	x9,x9,x8
	ldr	s1,[x20,x9,lsl 2]		; im
	add	x9,x4,x7,lsl 3
	ldp	s2,s3,[x9]			; cos, sin
	fmul	s4,s0,s2			; (re + i im)(c - i s) = re c + im s + i (im c - re s)
	fmadd	s4,s1,s3,s4
	fmul	s5,s1,s2
	fmsub	s5,s0,s3,s5
	add	x9,x5,x7,lsl 3
	stp	s4,s5,[x9]
	add	x7,x7,1
	cmp	x7,x3
	b.lo	.pre
	add	x0,x19,MD_FFT
	mov	x1,x5
	ldr	x2,[x19,MD_Z2]
	bl	ak_fft
	; Z *= e^(-i t)
	ldr	x3,[x19,MD_N4]
	ldr	x4,[x19,MD_TW]
	ldr	x5,[x19,MD_Z2]
	mov	x7,0
.post:	add	x9,x5,x7,lsl 3
	ldp	s0,s1,[x9]
	add	x12,x4,x7,lsl 3
	ldp	s2,s3,[x12]
	fmul	s4,s0,s2
	fmadd	s4,s1,s3,s4
	fmul	s6,s1,s2
	fmsub	s6,s0,s3,s6
	stp	s4,s6,[x9]
	add	x7,x7,1
	cmp	x7,x3
	b.lo	.post
	; sortie : quatre quarts (n4 = N/4, n8 = N/8, m < n8)
	lsr	x8,x3,1				; n8
	mov	x7,0				; m
	lsl	x10,x3,2			; n4 en octets (flottants)
.out:	cmp	x7,x8
	b.hs	.done
	add	x11,x21,x7,lsl 3		; &y[2m]
	add	x12,x8,x7			; n8 + m
	add	x12,x5,x12,lsl 3
	ldp	s0,s1,[x12]			; Z[n8 + m]
	sub	x13,x8,1
	sub	x13,x13,x7			; n8 - 1 - m
	add	x13,x5,x13,lsl 3
	ldp	s2,s3,[x13]			; Z[n8 - 1 - m]
	add	x12,x5,x7,lsl 3
	ldp	s4,s5,[x12]			; Z[m]
	sub	x14,x3,1
	sub	x14,x14,x7			; n4 - 1 - m
	add	x14,x5,x14,lsl 3
	ldp	s6,s7,[x14]			; Z[n4 - 1 - m]
	; quart 0 : re[n8+m], -im[n8-1-m]
	fneg	s16,s3
	stp	s0,s16,[x11]
	; quart 1 : im[m], -re[n4-1-m]
	add	x11,x11,x10
	fneg	s16,s6
	stp	s5,s16,[x11]
	; quart 2 : im[n8+m], -re[n8-1-m]
	add	x11,x11,x10
	fneg	s16,s2
	stp	s1,s16,[x11]
	; quart 3 : -re[m], im[n4-1-m]
	add	x11,x11,x10
	fneg	s16,s4
	stp	s16,s7,[x11]
	add	x7,x7,1
	b	.out
.done:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
