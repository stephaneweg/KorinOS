; poly.inc - les modeles et les particules : D_PolysetDrawSpans8 (d_polyse.c), D_DrawParticle (d_part.c)

; spanpackage_t
SP_PDEST	= 0
SP_PZ		= 8
SP_COUNT	= 16
SP_PTEX		= 24
SP_SFRAC	= 32
SP_TFRAC	= 36
SP_LIGHT	= 40
SP_ZI		= 44
SP_SIZE		= 48
SP_END		= -999999
AT_SKINWIDTH	= 16			; affinetridesc_t

; x0 = pspanpackage
public D_PolysetDrawSpans8
D_PolysetDrawSpans8:
	stp	x29,x30,[sp,-80]!
	stp	x19,x20,[sp,16]
	stp	x21,x22,[sp,32]
	stp	x23,x24,[sp,48]
	stp	x25,x26,[sp,64]
	la	x19,d_aspancount
	ldr	w1,[x19]
	la	x20,errorterm
	ldr	w2,[x20]
	lw	w3,x9,erroradjustup
	lw	w4,x9,erroradjustdown
	lw	w5,x9,d_countextrastep
	lw	w6,x9,ubasestep
	lw	w7,x9,r_zistepx
	lw	w8,x9,r_lstepx
	lw	w21,x9,a_ststepxwhole
	sxtw	x21,w21
	lw	w22,x9,a_sstepxfrac
	lw	w23,x9,a_tstepxfrac
	la	x24,acolormap
	ldr	x24,[x24]
	la	x9,r_affinetridesc
	ldrsw	x25,[x9,AT_SKINWIDTH]
	movz	w26,0xFFFF
	movi32	w17,SP_END and 0xFFFFFFFF
.span:	ldr	w10,[x0,SP_COUNT]
	subs	w10,w1,w10			; lcount
	adds	w2,w2,w3
	b.mi	.base
	add	w1,w1,w5
	sub	w2,w2,w4
	b	.go
.base:	add	w1,w1,w6
.go:	cbz	w10,.next
	ldp	x11,x12,[x0,SP_PDEST]
	ldr	x13,[x0,SP_PTEX]
	ldp	w14,w15,[x0,SP_SFRAC]
	ldp	w16,w9,[x0,SP_LIGHT]		; w16 light, w9 zi
.px:	asr	w30,w9,16
	ldrsh	w29,[x12]
	cmp	w30,w29
	b.lt	.skip
	strh	w30,[x12]
	ldrb	w29,[x13]
	and	w30,w16,0xFF00
	add	w29,w29,w30
	ldrb	w29,[x24,x29]
	strb	w29,[x11]
.skip:	add	x11,x11,1
	add	w9,w9,w7
	add	x12,x12,2
	add	w16,w16,w8
	add	x13,x13,x21
	add	w14,w14,w22
	asr	w29,w14,16
	add	x13,x13,w29,sxtw
	and	w14,w14,w26
	add	w15,w15,w23
	tbz	w15,16,.nt
	add	x13,x13,x25
	and	w15,w15,w26
.nt:	subs	w10,w10,1
	b.ne	.px
.next:	add	x0,x0,SP_SIZE
	ldr	w9,[x0,SP_COUNT]
	cmp	w9,w17
	b.ne	.span
	str	w1,[x19]
	str	w2,[x20]
	ldp	x19,x20,[sp,16]
	ldp	x21,x22,[sp,32]
	ldp	x23,x24,[sp,48]
	ldp	x25,x26,[sp,64]
	ldp	x29,x30,[sp],80
	ret

; x0 = pparticle
public D_DrawParticle
D_DrawParticle:
	la	x9,r_origin
	ldp	s0,s1,[x0]
	ldr	s2,[x0,8]
	ldp	s3,s4,[x9]
	ldr	s5,[x9,8]
	fsub	s0,s0,s3			; local
	fsub	s1,s1,s4
	fsub	s2,s2,s5
	la	x9,r_pright
	ldp	s3,s4,[x9]
	ldr	s5,[x9,8]
	fmul	s3,s0,s3
	fmul	s4,s1,s4
	fadd	s3,s3,s4
	fmul	s5,s2,s5
	fadd	s3,s3,s5			; transformed[0]
	la	x9,r_pup
	ldp	s4,s5,[x9]
	ldr	s6,[x9,8]
	fmul	s4,s0,s4
	fmul	s5,s1,s5
	fadd	s4,s4,s5
	fmul	s6,s2,s6
	fadd	s4,s4,s6			; transformed[1]
	la	x9,r_ppn
	ldp	s5,s6,[x9]
	ldr	s7,[x9,8]
	fmul	s5,s0,s5
	fmul	s6,s1,s6
	fadd	s5,s5,s6
	fmul	s7,s2,s7
	fadd	s5,s5,s7			; transformed[2]
	fmov	s6,8.0
	fcmp	s5,s6
	b.mi	.ret				; (PARTICLE_Z_CLIP ; NaN : continue, comme le C)
	fmov	d6,1.0
	fcvt	d5,s5
	fdiv	d5,d6,d5
	fcvt	s5,d5				; zi
	la	x9,xcenter
	ldr	s6,[x9]
	fmul	s0,s5,s3
	fadd	s0,s6,s0
	fcvt	d0,s0
	fmov	d7,0.5
	fadd	d0,d0,d7
	fcvtzs	w1,d0				; u
	la	x9,ycenter
	ldr	s6,[x9]
	fmul	s0,s5,s4
	fsub	s0,s6,s0
	fcvt	d0,s0
	fadd	d0,d0,d7
	fcvtzs	w2,d0				; v
	lw	w9,x10,d_vrectbottom_particle
	cmp	w2,w9
	b.gt	.ret
	lw	w9,x10,d_vrectright_particle
	cmp	w1,w9
	b.gt	.ret
	lw	w9,x10,d_vrecty
	cmp	w2,w9
	b.lt	.ret
	lw	w9,x10,d_vrectx
	cmp	w1,w9
	b.lt	.ret
	lw	w3,x10,d_zwidth			; pas de z
	mul	w9,w3,w2
	add	w9,w9,w1
	la	x4,d_pzbuffer
	ldr	x4,[x4]
	add	x4,x4,w9,sxtw 1			; pz
	la	x5,d_viewbuffer
	ldr	x5,[x5]
	la	x9,d_scantable
	ldr	w9,[x9,w2,sxtw 2]
	add	x5,x5,w9,sxtw
	add	x5,x5,w1,sxtw			; pdest
	movi32	w9,0x47000000			; 0x8000
	fmov	s6,w9
	fmul	s6,s5,s6
	fcvtzs	w6,s6				; izi
	lw	w9,x10,d_pix_shift
	asr	w7,w6,w9			; pix
	lw	w9,x10,d_pix_min
	lw	w10,x11,d_pix_max
	cmp	w7,w9
	csel	w7,w9,w7,lt
	cmp	w7,w10
	csel	w7,w10,w7,gt
	lw	w9,x10,screenwidth		; pas de l'image
	ldr	s0,[x0,12]
	fcvtzs	w10,s0
	sxtw	x3,w3
	sxtw	x9,w9
	lw	w11,x12,d_y_aspect_shift
	lsl	w12,w7,w11			; lignes : pix de large, pix shl aspect de haut
.y:	mov	w13,0
.x:	ldrsh	w14,[x4,w13,sxtw 1]
	cmp	w14,w6
	b.gt	.n
	strh	w6,[x4,w13,sxtw 1]
	strb	w10,[x5,w13,sxtw]
.n:	add	w13,w13,1
	cmp	w13,w7
	b.lt	.x
	add	x4,x4,x3,lsl 1
	add	x5,x5,x9
	subs	w12,w12,1
	b.ne	.y
.ret:	ret
