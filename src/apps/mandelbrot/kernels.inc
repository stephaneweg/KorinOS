; ============================================================================
;  kernels.inc - les calculs de Fractales, en NEON (deux points a la fois, en
;  double precision) ; chaque coeur en fait tourner un.
;
;  Une ligne : x0 = flottants de sortie (32 bits), x1 = points, x2 = iterations,
;  d0 = x du premier point, d1 = y, d2 = pas. Sortie : la valeur lissee du temps
;  de fuite (-1 : dedans) ; Newton : racine * 1000 + iterations ; Lyapunov :
;  l'exposant.
;
;  esc_row : le patron des fractales a temps de fuite. Registres :
;   v0, v1  z		v4, v5  c		v6  masque des voies finies
;   v7  compteurs	v8, v9  zr2, zi2	v12, v13  nouveau z
;   v16, v17  point	v18  deux pas		v19  seuil
;   v20  1 (entier)	v21, v22  g_cr, g_ci	v23  3.0
;   v24 .25, v25 1.0, v26 1/16
;  Une voie finie garde son z (bif) ; on regarde toutes les 8 iterations si les
;  deux le sont.
; ============================================================================

macro esc_row name, PRE, TEST, STEP, FIN {
name:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	fadd	d3,d2,d2
	dup	v18.2d,v3.d[0]
	fadd	d4,d0,d2
	ins	v0.d[1],v4.d[0]
	mov	v16.16b,v0.16b
	dup	v17.2d,v1.d[0]
	mov	x4,1
	dup	v20.2d,x4
	adr	x4,k_tab
	ldp	q24,q25,[x4]
	ldp	q26,q23,[x4,32]
	ldr	q19,[x4,64]
	ldr	d27,[x4,80]
	adr	x4,g_cr
	ldp	d21,d22,[x4]
	dup	v21.2d,v21.d[0]
	dup	v22.2d,v22.d[0]
.p:	cmp	x20,0
	b.le	.done
	PRE
	movi	v6.2d,0
	movi	v7.2d,0
	mov	x3,x21
.i:	TEST
	orr	v6.16b,v6.16b,v11.16b
	add	v7.2d,v7.2d,v20.2d
	add	v7.2d,v7.2d,v6.2d
	STEP
	bif	v0.16b,v12.16b,v6.16b
	bif	v1.16b,v13.16b,v6.16b
	subs	x3,x3,1
	b.eq	.end
	tst	x3,7
	b.ne	.i
	umov	x4,v6.d[0]
	umov	x5,v6.d[1]
	and	x4,x4,x5
	cbz	x4,.i
.end:	umov	x22,v6.d[1]
	umov	x23,v7.d[1]
	ins	v2.d[0],v0.d[1]
	ins	v3.d[0],v1.d[1]
	umov	x4,v6.d[0]
	umov	x5,v7.d[0]
	bl	FIN
	str	s0,[x19],4
	mov	x4,x22
	mov	x5,x23
	fmov	d0,d2
	fmov	d1,d3
	bl	FIN
	str	s0,[x19],4
.next:	fadd	v16.2d,v16.2d,v18.2d
	sub	x20,x20,2
	b	.p
.skip:	movl	w4,0xBF800000			; -1 : dedans
	str	w4,[x19],4
	str	w4,[x19],4
	b	.next
.done:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
}

macro pre_c {
	movi	v0.2d,0
	movi	v1.2d,0
	mov	v4.16b,v16.16b
	mov	v5.16b,v17.16b
}
; (Mandelbrot) deux points dans la cardioide ou le grand disque : pas de calcul
macro pre_mandel {
	pre_c
	fsub	v8.2d,v16.2d,v24.2d
	fmul	v9.2d,v8.2d,v8.2d
	fmul	v10.2d,v17.2d,v17.2d
	fadd	v9.2d,v9.2d,v10.2d
	fadd	v11.2d,v9.2d,v8.2d
	fmul	v11.2d,v11.2d,v9.2d
	fmul	v12.2d,v10.2d,v24.2d
	fcmgt	v13.2d,v12.2d,v11.2d
	fadd	v8.2d,v16.2d,v25.2d
	fmul	v8.2d,v8.2d,v8.2d
	fadd	v8.2d,v8.2d,v10.2d
	fcmgt	v9.2d,v26.2d,v8.2d
	orr	v13.16b,v13.16b,v9.16b
	umov	x4,v13.d[0]
	umov	x5,v13.d[1]
	and	x4,x4,x5
	cbnz	x4,.skip
}
macro pre_julia {
	mov	v0.16b,v16.16b
	mov	v1.16b,v17.16b
	mov	v4.16b,v21.16b
	mov	v5.16b,v22.16b
}
macro pre_phoenix {
	mov	v0.16b,v17.16b			; (retournee : la forme connue, debout)
	mov	v1.16b,v16.16b
	movi	v2.2d,0
	movi	v3.2d,0
	mov	v4.16b,v21.16b
	mov	v5.16b,v22.16b
}
macro pre_newton {
	mov	v0.16b,v16.16b
	mov	v1.16b,v17.16b
}

macro test_esc {
	fmul	v8.2d,v0.2d,v0.2d
	fmul	v9.2d,v1.2d,v1.2d
	fadd	v10.2d,v8.2d,v9.2d
	fcmgt	v11.2d,v10.2d,v19.2d
}
; Newton sur z^3 - 1 : v14, v15 = f(z), v28, v29 = z^2, fini quand |f|^2 < seuil
macro test_newton {
	fmul	v8.2d,v0.2d,v0.2d
	fmul	v9.2d,v1.2d,v1.2d
	fsub	v28.2d,v8.2d,v9.2d
	fmul	v29.2d,v0.2d,v1.2d
	fadd	v29.2d,v29.2d,v29.2d
	fmul	v14.2d,v28.2d,v0.2d
	fmls	v14.2d,v29.2d,v1.2d
	fsub	v14.2d,v14.2d,v25.2d
	fmul	v15.2d,v28.2d,v1.2d
	fmla	v15.2d,v29.2d,v0.2d
	fmul	v10.2d,v14.2d,v14.2d
	fmla	v10.2d,v15.2d,v15.2d
	dup	v11.2d,v27.d[0]
	fcmgt	v11.2d,v11.2d,v10.2d
}

macro step_mandel {
	fsub	v12.2d,v8.2d,v9.2d
	fadd	v12.2d,v12.2d,v4.2d
	fmul	v13.2d,v0.2d,v1.2d
	fadd	v13.2d,v13.2d,v13.2d
	fadd	v13.2d,v13.2d,v5.2d
}
macro step_ship {
	fsub	v12.2d,v8.2d,v9.2d
	fadd	v12.2d,v12.2d,v4.2d
	fmul	v13.2d,v0.2d,v1.2d
	fabs	v13.2d,v13.2d
	fadd	v13.2d,v13.2d,v13.2d
	fadd	v13.2d,v13.2d,v5.2d
}
macro step_tricorn {
	fsub	v12.2d,v8.2d,v9.2d
	fadd	v12.2d,v12.2d,v4.2d
	fmul	v13.2d,v0.2d,v1.2d
	fadd	v13.2d,v13.2d,v13.2d
	fsub	v13.2d,v5.2d,v13.2d
}
macro step_m3 {
	fmul	v14.2d,v9.2d,v23.2d
	fsub	v14.2d,v8.2d,v14.2d
	fmul	v12.2d,v0.2d,v14.2d
	fadd	v12.2d,v12.2d,v4.2d
	fmul	v14.2d,v8.2d,v23.2d
	fsub	v14.2d,v14.2d,v9.2d
	fmul	v13.2d,v1.2d,v14.2d
	fadd	v13.2d,v13.2d,v5.2d
}
macro step_m4 {
	fsub	v14.2d,v8.2d,v9.2d
	fmul	v15.2d,v0.2d,v1.2d
	fadd	v15.2d,v15.2d,v15.2d
	fmul	v12.2d,v14.2d,v14.2d
	fmls	v12.2d,v15.2d,v15.2d
	fadd	v12.2d,v12.2d,v4.2d
	fmul	v13.2d,v14.2d,v15.2d
	fadd	v13.2d,v13.2d,v13.2d
	fadd	v13.2d,v13.2d,v5.2d
}
macro step_m5 {
	fsub	v14.2d,v8.2d,v9.2d		; z^2
	fmul	v15.2d,v0.2d,v1.2d
	fadd	v15.2d,v15.2d,v15.2d
	fmul	v28.2d,v14.2d,v14.2d		; z^4
	fmls	v28.2d,v15.2d,v15.2d
	fmul	v29.2d,v14.2d,v15.2d
	fadd	v29.2d,v29.2d,v29.2d
	fmul	v12.2d,v28.2d,v0.2d		; z^5
	fmls	v12.2d,v29.2d,v1.2d
	fadd	v12.2d,v12.2d,v4.2d
	fmul	v13.2d,v28.2d,v1.2d
	fmla	v13.2d,v29.2d,v0.2d
	fadd	v13.2d,v13.2d,v5.2d
}
macro step_phoenix {
	fsub	v12.2d,v8.2d,v9.2d
	fadd	v12.2d,v12.2d,v4.2d
	fmla	v12.2d,v5.2d,v2.2d
	fmul	v13.2d,v0.2d,v1.2d
	fadd	v13.2d,v13.2d,v13.2d
	fmla	v13.2d,v5.2d,v3.2d
	bif	v2.16b,v0.16b,v6.16b
	bif	v3.16b,v1.16b,v6.16b
}
; z - f / (3 z^2) : f * conj(z^2) / (3 |z^2|^2)
macro step_newton {
	fmul	v30.2d,v28.2d,v28.2d
	fmla	v30.2d,v29.2d,v29.2d
	fmul	v30.2d,v30.2d,v23.2d
	fmul	v12.2d,v14.2d,v28.2d
	fmla	v12.2d,v15.2d,v29.2d
	fdiv	v12.2d,v12.2d,v30.2d
	fmul	v13.2d,v15.2d,v28.2d
	fmls	v13.2d,v14.2d,v29.2d
	fdiv	v13.2d,v13.2d,v30.2d
	fsub	v12.2d,v0.2d,v12.2d
	fsub	v13.2d,v1.2d,v13.2d
}

esc_row k_mandel, pre_mandel, test_esc, step_mandel, fin_smooth
esc_row k_julia, pre_julia, test_esc, step_mandel, fin_smooth
esc_row k_ship, pre_c, test_esc, step_ship, fin_smooth
esc_row k_tricorn, pre_c, test_esc, step_tricorn, fin_smooth
esc_row k_m3, pre_c, test_esc, step_m3, fin_smooth
esc_row k_m4, pre_c, test_esc, step_m4, fin_smooth
esc_row k_m5, pre_c, test_esc, step_m5, fin_smooth
esc_row k_phoenix, pre_phoenix, test_esc, step_phoenix, fin_smooth
esc_row k_newton, pre_newton, test_newton, step_newton, fin_newton

; d0 > 0 -> d0 = log2 (d0), a 2e-5 pres (le polynome vaut 0 et 1 aux bouts :
; la valeur lissee ne saute pas). Garde d2, d3.
log2d:
	fmov	x4,d0
	ubfx	x5,x4,52,11
	sub	x5,x5,1023
	and	x4,x4,0x000FFFFFFFFFFFFF
	mov	x6,0x3FF
	orr	x4,x4,x6,lsl 52
	fmov	d0,x4
	fmov	d4,1.0
	fsub	d0,d0,d4
	adr	x4,k_log
	ldp	d4,d5,[x4,24]			; a4, a5
	fmul	d6,d0,d5
	fadd	d6,d6,d4
	ldp	d4,d5,[x4,8]			; a2, a3
	fmul	d6,d6,d0
	fadd	d6,d6,d5
	fmul	d6,d6,d0
	fadd	d6,d6,d4
	ldr	d4,[x4]				; a1
	fmul	d6,d6,d0
	fadd	d6,d6,d4
	fmul	d0,d6,d0
	scvtf	d4,x5
	fadd	d0,d0,d4
	ret

; x4 = fini, x5 = iterations, d0, d1 = z -> s0 = n + 1 - log_d (log2 |z|)
fin_smooth:
	cbz	x4,.in
	str	x30,[sp,-16]!
	fmul	d0,d0,d0
	fmul	d1,d1,d1
	fadd	d0,d0,d1
	mov	x7,x5
	bl	log2d				; log2 |z|^2
	fmov	d4,0.5
	fmul	d0,d0,d4
	bl	log2d
	adr	x4,g_ild
	ldr	d4,[x4]
	fmul	d0,d0,d4
	ucvtf	d4,x7
	fmov	d5,1.0
	fadd	d4,d4,d5
	fsub	d0,d4,d0
	fmov	d4,xzr
	fmaxnm	d0,d0,d4
	fcvt	s0,d0
	ldr	x30,[sp],16
	ret
.in:	fmov	s0,-1.0
	ret

; x4 = fini, x5 = iterations, d0, d1 = z -> s0 = racine * 1000 + iterations
fin_newton:
	cbz	x4,.in
	mov	x6,0
	fmov	d4,xzr
	fcmp	d0,d4
	b.gt	.r
	mov	x6,1
	fcmp	d1,d4
	b.gt	.r
	mov	x6,2
.r:	mov	x4,1000
	mul	x6,x6,x4
	add	x6,x6,x5
	ucvtf	s0,x6
	ret
.in:	fmov	s0,-1.0
	ret

; --- Lyapunov ----------------------------------------------------------------
; l'exposant de x -> r x (1 - x), r pris dans (a, b) selon la suite g_seq ;
; a : le x du point, b : son y
macro vlog2 dest {
	ushr	v8.2d,v1.2d,52
	sub	v8.2d,v8.2d,v31.2d
	scvtf	v8.2d,v8.2d
	and	v9.16b,v1.16b,v30.16b
	orr	v9.16b,v9.16b,v29.16b
	fsub	v9.2d,v9.2d,v25.2d
	mov	v10.16b,v28.16b
	fmul	v10.2d,v10.2d,v9.2d
	fadd	v10.2d,v10.2d,v27.2d
	fmul	v10.2d,v10.2d,v9.2d
	fadd	v10.2d,v10.2d,v26.2d
	fmul	v10.2d,v10.2d,v9.2d
	fadd	v10.2d,v10.2d,v24.2d
	fmul	v10.2d,v10.2d,v9.2d
	fadd	v10.2d,v10.2d,v23.2d
	fmul	v10.2d,v10.2d,v9.2d
	fadd	dest,v10.2d,v8.2d
}
k_lyap:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	fadd	d3,d2,d2
	dup	v18.2d,v3.d[0]
	fadd	d4,d0,d2
	ins	v0.d[1],v4.d[0]
	mov	v16.16b,v0.16b
	dup	v17.2d,v1.d[0]
	fmov	v25.2d,1.0
	fmov	v22.2d,2.0
	adr	x4,k_log
	ld1r_d	23,x4
	add	x4,x4,8
	ld1r_d	24,x4
	add	x4,x4,8
	ld1r_d	26,x4
	add	x4,x4,8
	ld1r_d	27,x4
	add	x4,x4,8
	ld1r_d	28,x4
	mov	x4,1023
	dup	v31.2d,x4
	movl	x4,0x000FFFFFFFFFFFFF
	dup	v30.2d,x4
	movl	x4,0x3FF0000000000000
	dup	v29.2d,x4
	adr	x9,g_seq
	adr	x10,g_seqn
	ldr	x10,[x10]
.p:	cmp	x20,0
	b.le	.done
	fmov	v0.2d,0.5
	mov	x4,x21				; mise en route : un quart des iterations
	lsr	x4,x4,2
	mov	x5,0
.w:	ldrb	w6,[x9,x5]
	add	x5,x5,1
	cmp	x5,x10
	csel	x5,xzr,x5,hs
	mov	v2.16b,v16.16b
	cbz	w6,.w1
	mov	v2.16b,v17.16b
.w1:	fsub	v1.2d,v25.2d,v0.2d
	fmul	v0.2d,v0.2d,v1.2d
	fmul	v0.2d,v0.2d,v2.2d
	subs	x4,x4,1
	b.ne	.w
	movi	v3.2d,0				; la somme
	mov	x4,x21
.s:	ldrb	w6,[x9,x5]
	add	x5,x5,1
	cmp	x5,x10
	csel	x5,xzr,x5,hs
	mov	v2.16b,v16.16b
	cbz	w6,.s1
	mov	v2.16b,v17.16b
.s1:	fsub	v1.2d,v25.2d,v0.2d
	fmul	v0.2d,v0.2d,v1.2d
	fmul	v0.2d,v0.2d,v2.2d		; x suivant
	fmul	v1.2d,v0.2d,v22.2d
	fsub	v1.2d,v25.2d,v1.2d
	fmul	v1.2d,v1.2d,v2.2d
	fabs	v1.2d,v1.2d			; |r (1 - 2 x)|
	vlog2	v11.2d
	fadd	v3.2d,v3.2d,v11.2d
	subs	x4,x4,1
	b.ne	.s
	ucvtf	d1,x21
	dup	v1.2d,v1.d[0]
	fdiv	v3.2d,v3.2d,v1.2d
	fcvt	s0,d3
	str	s0,[x19],4
	ins	v3.d[0],v3.d[1]
	fcvt	s0,d3
	str	s0,[x19],4
	fadd	v16.2d,v16.2d,v18.2d
	sub	x20,x20,2
	b	.p
.done:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- attracteurs : Clifford, De Jong -------------------------------------------
; un lot : x0 = numero du coeur. Deux orbites, chaque point compte dans g_mu.
ATTR_N		= 8192
macro vsin dest {
	fmul	v8.2d,v12.2d,v24.2d
	frintn	v8.2d,v8.2d
	mov	v9.16b,v12.16b
	fmls	v9.2d,v8.2d,v25.2d
	fmul	v10.2d,v9.2d,v9.2d
	mov	v8.16b,v30.16b
	fmul	v8.2d,v8.2d,v10.2d
	fadd	v8.2d,v8.2d,v29.2d
	fmul	v8.2d,v8.2d,v10.2d
	fadd	v8.2d,v8.2d,v28.2d
	fmul	v8.2d,v8.2d,v10.2d
	fadd	v8.2d,v8.2d,v27.2d
	fmul	v8.2d,v8.2d,v10.2d
	fadd	v8.2d,v8.2d,v26.2d
	fmul	dest,v8.2d,v9.2d
}
attr_setup:
	adr	x4,k_sin
	ldp	q24,q25,[x4]
	ldp	q26,q27,[x4,32]
	ldp	q28,q29,[x4,64]
	ldp	q30,q31,[x4,96]
	adr	x4,g_pa
	ldp	d2,d3,[x4]
	ldp	d4,d5,[x4,16]
	dup	v2.2d,v2.d[0]
	dup	v3.2d,v3.d[0]
	dup	v4.2d,v4.d[0]
	dup	v5.2d,v5.d[0]
	adr	x4,g_x0
	ldp	d6,d18,[x4]			; g_x0, g_y0
	ldr	d7,[x4,16]			; g_dx
	fmov	d8,1.0
	fdiv	d7,d8,d7
	dup	v6.2d,v6.d[0]
	dup	v18.2d,v18.d[0]
	dup	v7.2d,v7.d[0]
	ret

; (lot) x19 = etat de l'orbite (x, x, y, y), x20 = iterations -> les points
macro attr_loop STEP {
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	ldv	x19,g_work
	mov	x1,ORB_STATE
	madd	x19,x0,x1,x19
	mov	x20,ATTR_N
	bl	attr_setup
	ldp	q0,q1,[x19]
	ldv	x8,g_mu
	ldv	x9,g_vw
	ldv	x10,g_vh
.i:	STEP
	fsub	v12.2d,v0.2d,v6.2d
	fmul	v12.2d,v12.2d,v7.2d
	fcvtzs	v12.2d,v12.2d
	fsub	v13.2d,v1.2d,v18.2d
	fmul	v13.2d,v13.2d,v7.2d
	fcvtzs	v13.2d,v13.2d
	umov	x2,v12.d[0]
	umov	x3,v13.d[0]
	cmp	x2,x9
	b.hs	.n0
	cmp	x3,x10
	b.hs	.n0
	madd	x4,x3,x9,x2
	ldr	w5,[x8,x4,lsl 2]
	add	w5,w5,1
	str	w5,[x8,x4,lsl 2]
.n0:	umov	x2,v12.d[1]
	umov	x3,v13.d[1]
	cmp	x2,x9
	b.hs	.n1
	cmp	x3,x10
	b.hs	.n1
	madd	x4,x3,x9,x2
	ldr	w5,[x8,x4,lsl 2]
	add	w5,w5,1
	str	w5,[x8,x4,lsl 2]
.n1:	subs	x20,x20,1
	b.ne	.i
	stp	q0,q1,[x19]
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
}
; sin (a y) + c cos (a x), sin (b x) + d cos (b y)
macro step_clifford {
	fmul	v12.2d,v2.2d,v1.2d
	vsin	v14.2d
	fmul	v12.2d,v2.2d,v0.2d
	fadd	v12.2d,v12.2d,v31.2d
	vsin	v15.2d
	fmla	v14.2d,v15.2d,v4.2d
	fmul	v12.2d,v3.2d,v0.2d
	vsin	v16.2d
	fmul	v12.2d,v3.2d,v1.2d
	fadd	v12.2d,v12.2d,v31.2d
	vsin	v15.2d
	fmla	v16.2d,v15.2d,v5.2d
	mov	v0.16b,v14.16b
	mov	v1.16b,v16.16b
}
; sin (a y) - cos (b x), sin (c x) - cos (d y)
macro step_dejong {
	fmul	v12.2d,v2.2d,v1.2d
	vsin	v14.2d
	fmul	v12.2d,v3.2d,v0.2d
	fadd	v12.2d,v12.2d,v31.2d
	vsin	v15.2d
	fsub	v14.2d,v14.2d,v15.2d
	fmul	v12.2d,v4.2d,v0.2d
	vsin	v16.2d
	fmul	v12.2d,v5.2d,v1.2d
	fadd	v12.2d,v12.2d,v31.2d
	vsin	v15.2d
	fsub	v16.2d,v16.2d,v15.2d
	mov	v0.16b,v14.16b
	mov	v1.16b,v16.16b
}
k_clifford:
	prologue
	attr_loop step_clifford
	epilogue
k_dejong:
	prologue
	attr_loop step_dejong
	epilogue

; les bornes d'une orbite -> v6, v7 = min, max de x ; v11, v13 = min, max de y
macro attr_bounds STEP {
	bl	attr_setup
	adr	x4,k_orb
	ldp	d0,d1,[x4]
	dup	v0.2d,v0.d[0]
	dup	v1.2d,v1.d[0]
	mov	x20,256
.w:	STEP
	subs	x20,x20,1
	b.ne	.w
	mov	v6.16b,v0.16b
	mov	v7.16b,v0.16b
	mov	v11.16b,v1.16b
	mov	v13.16b,v1.16b
	movl	x20,40000
.b:	STEP
	fmin	v6.2d,v6.2d,v0.2d
	fmax	v7.2d,v7.2d,v0.2d
	fmin	v11.2d,v11.2d,v1.2d
	fmax	v13.2d,v13.2d,v1.2d
	subs	x20,x20,1
	b.ne	.b
}
attr_bounds_c:
	prologue
	str	x20,[sp,-16]!
	attr_bounds step_clifford
	ldr	x20,[sp],16
	epilogue
attr_bounds_d:
	prologue
	str	x20,[sp,-16]!
	attr_bounds step_dejong
	ldr	x20,[sp],16
	epilogue

; (Clifford, De Jong) la vue qui montre tout l'attracteur
attr_fit:
	ldv	x0,g_type
	sub	x1,x0,T_CLIFFORD
	cmp	x1,1
	b.hi	.ret
	prologue
	cbnz	x1,.d
	bl	attr_bounds_c
	b	.f
.d:	bl	attr_bounds_d
.f:	fmov	d4,0.5
	fadd	d0,d6,d7			; centre
	fmul	d0,d0,d4
	fadd	d1,d11,d13
	fmul	d1,d1,d4
	fsub	d2,d7,d6			; largeurs
	fsub	d3,d13,d11
	ldv	x0,g_vw
	ldv	x1,g_vh
	ucvtf	d4,x0
	ucvtf	d5,x1
	fdiv	d4,d4,d5
	fmul	d3,d3,d4			; la hauteur, en largeur
	fcmp	d3,d2
	fcsel	d2,d3,d2,gt
	adr	x0,k_fit
	ldr	d3,[x0]
	fmul	d2,d2,d3
	adr	x0,g_cx
	stp	d0,d1,[x0]
	str	d2,[x0,16]
	adr	x0,g_span0
	str	d2,[x0]
	epilogue
.ret:	ret

; --- Buddhabrot : les orbites qui s'echappent, comptees dans g_mu ---------------
; x0 = numero du coeur : un lot de points c au hasard. Le plan est tourne : Re
; vers le bas, Im en travers ; les deux moities (conjuguees) comptent.
BUD_N		= 256
k_buddha:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldv	x19,g_work
	mov	x1,ORB_STATE
	madd	x20,x0,x1,x19			; l'etat : le hasard (x20)
	movl	x1,ORB_SIZE
	madd	x1,x0,x1,x19
	add	x19,x1,ORB_BASE			; l'orbite
	ldr	x21,[x20,64]			; xorshift
	mov	x22,BUD_N
	ldv	x23,g_maxit
.n:	cbz	x22,.out
	sub	x22,x22,1
	eor	x1,x21,x21,lsl 13
	eor	x1,x1,x1,lsr 7
	eor	x21,x1,x1,lsl 17
	ubfx	x1,x21,0,26
	ubfx	x2,x21,26,26
	ucvtf	d4,x1
	ucvtf	d5,x2
	adr	x1,k_bud
	ldp	d6,d7,[x1]
	fmul	d4,d4,d6			; c : (-2 .. 1, -1.5 .. 1.5)
	fmul	d5,d5,d6
	fsub	d4,d4,d7
	fmov	d7,1.5
	fsub	d5,d5,d7
	fmov	d7,0.25				; cardioide, grand disque : jamais de fuite
	fsub	d8,d4,d7
	fmul	d9,d8,d8
	fmul	d10,d5,d5
	fadd	d9,d9,d10
	fadd	d11,d9,d8
	fmul	d11,d11,d9
	fmul	d12,d10,d7
	fcmp	d12,d11
	b.gt	.n
	fmov	d7,1.0
	fadd	d8,d4,d7
	fmul	d8,d8,d8
	fadd	d8,d8,d10
	ldr	d7,[x1,16]
	fcmp	d8,d7
	b.lt	.n
	fmov	d0,xzr
	fmov	d1,xzr
	mov	x3,0				; points de l'orbite
	mov	x2,x19
	fmov	d7,4.0
.it:	fmul	d8,d0,d0
	fmul	d9,d1,d1
	fadd	d10,d8,d9
	fcmp	d10,d7
	b.gt	.esc
	fmul	d1,d0,d1
	fadd	d1,d1,d1
	fadd	d1,d1,d5
	fsub	d0,d8,d9
	fadd	d0,d0,d4
	stp	d0,d1,[x2],16
	add	x3,x3,1
	cmp	x3,x23
	b.lo	.it
	b	.n				; (dedans : rien)
.esc:	cmp	x3,8				; (les fuites immediates : un fond uni)
	b.lo	.n
	adr	x1,g_x0				; x0 : Im, y0 : Re
	ldp	d4,d5,[x1]
	ldr	d6,[x1,16]
	fmov	d7,1.0
	fdiv	d6,d7,d6
	ldv	x8,g_mu
	ldv	x9,g_vw
	ldv	x10,g_vh
	mov	x2,x19
.pl:	cbz	x3,.n
	sub	x3,x3,1
	ldp	d0,d1,[x2],16
	fsub	d2,d0,d5
	fmul	d2,d2,d6
	fcvtzs	x4,d2				; rang
	cmp	x4,x10
	b.hs	.pl
	fsub	d2,d1,d4
	fmul	d2,d2,d6
	fcvtzs	x5,d2				; colonne
	cmp	x5,x9
	b.hs	.s2
	madd	x6,x4,x9,x5
	ldr	w7,[x8,x6,lsl 2]
	add	w7,w7,1
	str	w7,[x8,x6,lsl 2]
.s2:	fneg	d2,d1
	fsub	d2,d2,d4
	fmul	d2,d2,d6
	fcvtzs	x5,d2
	cmp	x5,x9
	b.hs	.pl
	madd	x6,x4,x9,x5
	ldr	w7,[x8,x6,lsl 2]
	add	w7,w7,1
	str	w7,[x8,x6,lsl 2]
	b	.pl
.out:	str	x21,[x20,64]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
