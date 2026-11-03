; span.inc - les spans du monde : D_DrawSpans8, D_DrawZSpans, D_DrawTurbulent8Span (d_scan.c)

; espan_t
ES_U		= 0
ES_V		= 4
ES_COUNT	= 8
ES_NEXT		= 16

; un point : w14 s, w15 t, w16 pas de s, w17 pas de t, x1 texture, w4 largeur, x12 destination
macro span_pix {
	asr	w9,w14,16
	asr	w10,w15,16
	mul	w10,w10,w4
	add	w9,w9,w10
	ldrb	w9,[x1,w9,sxtw]
	strb	w9,[x12],1
	add	w14,w14,w16
	add	w15,w15,w17
}

; (u, v) -> s2 sdivz, s3 tdivz, s4 zi : l'ordre du C
macro span_start {
	ldp	w9,w10,[x0,ES_U]
	scvtf	s0,w9
	scvtf	s1,w10
	fmul	s2,s1,s19
	fadd	s2,s22,s2
	fmul	s3,s0,s16
	fadd	s2,s2,s3
	fmul	s3,s1,s20
	fadd	s3,s23,s3
	fmul	s4,s0,s17
	fadd	s3,s3,s4
	fmul	s4,s1,s21
	fadd	s4,s24,s4
	fmul	s5,s0,s18
	fadd	s4,s4,s5
}

; x0 = pspan
public D_DrawSpans8
D_DrawSpans8:
	stp	x19,x20,[sp,-16]!
	la	x1,cacheblock
	ldr	x1,[x1]
	la	x2,d_viewbuffer
	ldr	x2,[x2]
	lw	w3,x9,screenwidth
	lw	w4,x9,cachewidth
	lw	w5,x9,sadjust
	lw	w6,x9,tadjust
	lw	w7,x9,bbextents
	lw	w8,x9,bbextentt
	la	x9,d_sdivzstepu
	ldr	s16,[x9]
	la	x9,d_tdivzstepu
	ldr	s17,[x9]
	la	x9,d_zistepu
	ldr	s18,[x9]
	la	x9,d_sdivzstepv
	ldr	s19,[x9]
	la	x9,d_tdivzstepv
	ldr	s20,[x9]
	la	x9,d_zistepv
	ldr	s21,[x9]
	la	x9,d_sdivzorigin
	ldr	s22,[x9]
	la	x9,d_tdivzorigin
	ldr	s23,[x9]
	la	x9,d_ziorigin
	ldr	s24,[x9]
	fmov	s25,8.0
	fmul	s26,s16,s25			; sdivz8stepu
	fmul	s27,s17,s25
	fmul	s25,s18,s25
	movi32	w9,0x47800000			; 65536.0
	fmov	s28,w9
.span:	ldr	w10,[x0,ES_V]
	mul	w10,w10,w3
	ldr	w9,[x0,ES_U]
	add	w10,w10,w9
	add	x12,x2,w10,sxtw
	ldr	w13,[x0,ES_COUNT]
	span_start
	fdiv	s5,s28,s4
	fmul	s0,s2,s5
	fcvtzs	w14,s0
	add	w14,w14,w5
	clamp	w14,wzr,w7,w9
	fmul	s0,s3,s5
	fcvtzs	w15,s0
	add	w15,w15,w6
	clamp	w15,wzr,w8,w9
.sub:	cmp	w13,8
	mov	w19,8
	csel	w19,w19,w13,ge			; spancount
	subs	w13,w13,w19
	b.eq	.last
	fadd	s2,s2,s26
	fadd	s3,s3,s27
	fadd	s4,s4,s25
	fdiv	s5,s28,s4
	mov	w9,8
	fmul	s0,s2,s5
	fcvtzs	w20,s0
	add	w20,w20,w5
	clamp	w20,w9,w7,w10
	fmul	s0,s3,s5
	fcvtzs	w11,s0
	add	w11,w11,w6
	clamp	w11,w9,w8,w10
	sub	w16,w20,w14
	asr	w16,w16,3
	sub	w17,w11,w15
	asr	w17,w17,3
	span_pix				; huit points
	span_pix
	span_pix
	span_pix
	span_pix
	span_pix
	span_pix
	span_pix
	mov	w14,w20
	mov	w15,w11
	b	.sub
.last:	sub	w10,w19,1
	scvtf	s1,w10
	fmul	s0,s16,s1
	fadd	s2,s2,s0
	fmul	s0,s17,s1
	fadd	s3,s3,s0
	fmul	s0,s18,s1
	fadd	s4,s4,s0
	fdiv	s5,s28,s4
	mov	w9,8
	fmul	s0,s2,s5
	fcvtzs	w20,s0
	add	w20,w20,w5
	clamp	w20,w9,w7,w11
	fmul	s0,s3,s5
	fcvtzs	w11,s0
	add	w11,w11,w6
	clamp	w11,w9,w8,w13
	cmp	w10,0
	b.le	.px
	sub	w16,w20,w14
	sdiv	w16,w16,w10
	sub	w17,w11,w15
	sdiv	w17,w17,w10
.px:	span_pix
	subs	w19,w19,1
	b.gt	.px
	ldr	x0,[x0,ES_NEXT]
	cbnz	x0,.span
	ldp	x19,x20,[sp],16
	ret

; x0 = pspan
public D_DrawZSpans
D_DrawZSpans:
	la	x2,d_pzbuffer
	ldr	x2,[x2]
	lw	w3,x9,d_zwidth
	la	x9,d_zistepu
	ldr	s18,[x9]
	la	x9,d_zistepv
	ldr	s21,[x9]
	la	x9,d_ziorigin
	ldr	s24,[x9]
	movi32	w9,0x4F000000			; 0x8000 * 0x10000
	fmov	s6,w9
	fmul	s7,s18,s6
	fcvtzs	w4,s7				; izistep
	fcvt	d6,s6
	dup	v25.4s,w4			; pas de 8 points, et les 8 premiers ecarts
	shl	v25.4s,v25.4s,3
	mov	w9,0
	ins	v26.s[0],w9
	add	w9,w9,w4
	ins	v26.s[1],w9
	add	w9,w9,w4
	ins	v26.s[2],w9
	add	w9,w9,w4
	ins	v26.s[3],w9
	add	w9,w9,w4
	dup	v27.4s,w9
	add	v27.4s,v27.4s,v26.4s		; 4..7
.span:	ldp	w9,w10,[x0,ES_U]
	ldr	w5,[x0,ES_COUNT]
	mul	w11,w3,w10
	add	w11,w11,w9
	add	x6,x2,w11,sxtw 1		; pdest
	scvtf	s0,w9
	scvtf	s1,w10
	fmul	s2,s1,s21
	fadd	s2,s24,s2
	fmul	s3,s0,s18
	fadd	s2,s2,s3
	fcvt	d2,s2
	fmul	d2,d2,d6
	fcvtzs	w7,d2				; izi
	subs	w5,w5,8
	b.lt	.tail
	dup	v0.4s,w7
	add	v1.4s,v0.4s,v27.4s
	add	v0.4s,v0.4s,v26.4s
.q:	uzp2	v2.8h,v0.8h,v1.8h		; (les 16 bits du haut)
	str	q2,[x6],16
	add	v0.4s,v0.4s,v25.4s
	add	v1.4s,v1.4s,v25.4s
	subs	w5,w5,8
	b.ge	.q
	umov	w7,v0.s[0]
.tail:	adds	w5,w5,8
	b.eq	.next
.t1:	lsr	w9,w7,16
	strh	w9,[x6],2
	add	w7,w7,w4
	subs	w5,w5,1
	b.ne	.t1
.next:	ldr	x0,[x0,ES_NEXT]
	cbnz	x0,.span
	ret

; les variables r_turb_* ; r_turb_pdest et r_turb_spancount a jour en sortie
public D_DrawTurbulent8Span
D_DrawTurbulent8Span:
	la	x0,r_turb_pbase
	ldr	x0,[x0]
	la	x8,r_turb_pdest
	ldr	x1,[x8]
	la	x2,r_turb_turb
	ldr	x2,[x2]
	lw	w3,x9,r_turb_s
	lw	w4,x9,r_turb_t
	lw	w5,x9,r_turb_sstep
	lw	w6,x9,r_turb_tstep
	la	x10,r_turb_spancount
	ldr	w7,[x10]
.l:	ubfx	w9,w4,16,7			; CYCLE = 128
	ldr	w9,[x2,w9,uxtw 2]
	add	w9,w9,w3
	ubfx	w9,w9,16,6
	ubfx	w11,w3,16,7
	ldr	w11,[x2,w11,uxtw 2]
	add	w11,w11,w4
	ubfx	w11,w11,16,6
	add	w9,w9,w11,lsl 6
	ldrb	w9,[x0,x9]
	strb	w9,[x1],1
	add	w3,w3,w5
	add	w4,w4,w6
	subs	w7,w7,1
	b.gt	.l
	str	x1,[x8]
	str	w7,[x10]
	la	x9,r_turb_s
	str	w3,[x9]
	la	x9,r_turb_t
	str	w4,[x9]
	ret
