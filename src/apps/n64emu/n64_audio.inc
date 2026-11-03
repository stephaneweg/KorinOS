; ============================================================================
;  n64_audio.inc - le son : les taches audio du RSP a haut niveau (l'ABI " nead "
;  de Zelda OoT et MM : VADPCM, reechantillonnage, mixage d'enveloppe...) sur une
;  image de 4 Ko de la DMEM, et la sortie de l'AI, reechantillonnee au debit de
;  l'hote. Dans audio_task : x21 = aDmem, x22 = rdram.
; ============================================================================

macro a_get dst, addr {
	and	w16,addr,0xFFE
	ldrh	dst,[x21,x16]
	rev16	dst,dst
	sxth	dst,dst
}
macro a_put val, addr {
	and	w16,addr,0xFFE
	rev16	w17,val
	strh	w17,[x21,x16]
}
macro clamp_s16 reg {
	mov	w16,32767
	cmp	reg,w16
	csel	reg,w16,reg,gt
	mov	w17,-32768
	cmp	reg,w17
	csel	reg,w17,reg,lt
}
macro rd_s16 dst, addr {			; rdS16 : addr dans w
	and	w16,addr,0x7FFFFE
	eor	w16,w16,2
	ldrsh	dst,[x22,x16]
}
macro wr_s16 val, addr {
	and	w16,addr,0x7FFFFE
	eor	w16,w16,2
	strh	val,[x22,x16]
}
macro rd_b dst, addr {
	and	w16,addr,0x7FFFFF
	eor	w16,w16,3
	ldrb	dst,[x22,x16]
}
macro rd_w dst, addr {
	and	w16,addr,0x7FFFFC
	ldr	dst,[x22,x16]
}

; w0 = adresse -> w0 = le mot (x22 libre : pour les autres)
ram_word:
	and	w0,w0,0x7FFFFC
	add	x1,x19,M_RDRAM
	ldr	w0,[x1,x0]
	ret

; w0 = dmem, w1 = addr, w2 = count
a_load:
	and	w0,w0,0xFFFFFFFC
	and	w1,w1,0xFFFFFFF8
	add	w2,w2,7
	and	w2,w2,0xFFFFFFF8
	mov	w3,0
.l:	cmp	w3,w2
	b.hs	.r
	add	w4,w1,w3
	rd_b	w5,w4
	add	w4,w0,w3
	and	w4,w4,0xFFF
	strb	w5,[x21,x4]
	add	w3,w3,1
	b	.l
.r:	ret

a_save:
	and	w0,w0,0xFFFFFFFC
	and	w1,w1,0xFFFFFFF8
	add	w2,w2,7
	and	w2,w2,0xFFFFFFF8
	mov	w3,0
.l:	cmp	w3,w2
	b.hs	.r
	add	w4,w0,w3
	and	w4,w4,0xFFF
	ldrb	w5,[x21,x4]
	add	w4,w1,w3
	and	w4,w4,0x7FFFFF
	eor	w4,w4,3
	strb	w5,[x22,x4]
	add	w3,w3,1
	b	.l
.r:	ret

; w0 = flags, w1 = state
A_LAST		= 0
A_FRAME		= 32
A_SIZE		= 64
a_adpcm:
	prologue
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,A_SIZE
	mov	w9,w0				; flags
	mov	w10,w1				; state
	ldr	w23,[x19,m_aIn]			; in
	ldr	w24,[x19,m_aOut]		; out
	ldr	w25,[x19,m_aCount]
	add	w25,w25,31
	and	w25,w25,0xFFFFFFE0		; count
	tbz	w9,0,.ni
	stp	xzr,xzr,[sp,A_LAST]
	stp	xzr,xzr,[sp,A_LAST+16]
	b	.c
.ni:	mov	w2,w10
	tbz	w9,1,.st
	ldr	w2,[x19,m_aLoop]
.st:	mov	w3,0
.s1:	add	w4,w2,w3,lsl 1
	rd_s16	w5,w4
	add	x4,sp,A_LAST
	strh	w5,[x4,x3,lsl 1]
	add	w3,w3,1
	cmp	w3,16
	b.lo	.s1
.c:	mov	w3,0
.o0:	add	x4,sp,A_LAST
	ldrh	w5,[x4,x3,lsl 1]
	a_put	w5,w24
	add	w24,w24,2
	add	w3,w3,1
	cmp	w3,16
	b.lo	.o0
.fr:	cbz	w25,.end
	and	w4,w23,0xFFF
	ldrb	w4,[x21,x4]			; code
	add	w23,w23,1
	lsr	w5,w4,4				; scale
	and	w4,w4,15
	lsl	w4,w4,5
	add	x6,x19,M_ABOOK
	add	x6,x6,x4			; book1 (octets ; book2 = +16)
	add	x7,sp,A_FRAME
	tbz	w9,2,.b4
	mov	w8,14
	sub	w8,w8,w5
	cmp	w5,14
	csel	w8,wzr,w8,ge			; rs
	mov	w0,0				; i
.t1:	and	w1,w23,0xFFF
	ldrb	w1,[x21,x1]
	add	w23,w23,1
	mov	w2,0				; k
.t2:	lsl	w3,w2,1
	lsl	w3,w1,w3
	and	w3,w3,0xC0
	lsl	w3,w3,8
	sxth	w3,w3
	asr	w3,w3,w8
	add	w4,w2,w0,lsl 2
	strh	w3,[x7,x4,lsl 1]
	add	w2,w2,1
	cmp	w2,4
	b.lo	.t2
	add	w0,w0,1
	cmp	w0,4
	b.lo	.t1
	b	.pred
.b4:	mov	w8,12
	sub	w8,w8,w5
	cmp	w5,12
	csel	w8,wzr,w8,ge
	mov	w0,0
.f1:	and	w1,w23,0xFFF
	ldrb	w1,[x21,x1]
	add	w23,w23,1
	and	w2,w1,0xF0
	lsl	w2,w2,8
	sxth	w2,w2
	asr	w2,w2,w8
	strh	w2,[x7],2
	lsl	w2,w1,4
	and	w2,w2,0xF0
	lsl	w2,w2,8
	sxth	w2,w2
	asr	w2,w2,w8
	strh	w2,[x7],2
	add	w0,w0,1
	cmp	w0,8
	b.lo	.f1
.pred:	mov	w0,0				; h
.h:	add	x1,sp,A_LAST
	mov	w2,14
	mov	w3,15
	cbz	w0,.h1
	mov	w2,6
	mov	w3,7
.h1:	ldrsh	w26,[x1,x2,lsl 1]		; l1
	ldrsh	w8,[x1,x3,lsl 1]		; l2
	add	x7,sp,A_FRAME
	add	x7,x7,x0,lsl 4			; src
	mov	x1,0				; i
.i:	ldrsh	w2,[x7,x1,lsl 1]
	lsl	w2,w2,11			; acc
	ldrsh	w3,[x6,x1,lsl 1]
	mul	w3,w3,w26
	add	w2,w2,w3
	add	x4,x6,16
	ldrsh	w3,[x4,x1,lsl 1]
	mul	w3,w3,w8
	add	w2,w2,w3
	mov	x3,0				; k
.k:	cmp	x3,x1
	b.hs	.ke
	ldrsh	w5,[x4,x3,lsl 1]
	sub	x11,x1,1
	sub	x11,x11,x3
	ldrsh	w11,[x7,x11,lsl 1]
	mul	w5,w5,w11
	add	w2,w2,w5
	add	x3,x3,1
	b	.k
.ke:	asr	w2,w2,11
	clamp_s16 w2
	add	x3,x1,x0,lsl 3
	add	x4,sp,A_LAST
	strh	w2,[x4,x3,lsl 1]
	add	x1,x1,1
	cmp	x1,8
	b.lo	.i
	add	w0,w0,1
	cmp	w0,2
	b.lo	.h
	mov	w3,0
.o1:	add	x4,sp,A_LAST
	ldrh	w5,[x4,x3,lsl 1]
	a_put	w5,w24
	add	w24,w24,2
	add	w3,w3,1
	cmp	w3,16
	b.lo	.o1
	sub	w25,w25,32
	b	.fr
.end:	mov	w3,0
.e1:	add	x4,sp,A_LAST
	ldrh	w5,[x4,x3,lsl 1]
	add	w4,w10,w3,lsl 1
	wr_s16	w5,w4
	add	w3,w3,1
	cmp	w3,16
	b.lo	.e1
	add	sp,sp,A_SIZE
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	epilogue

; w0 = flags, w1 = pitch, w2 = state
a_resample:
	stp	x23,x24,[sp,-16]!
	mov	w9,w1				; pitch
	mov	w10,w2				; state
	ldr	w3,[x19,m_aIn]
	lsr	w3,w3,1
	sub	w3,w3,4				; ipos
	ldr	w4,[x19,m_aOut]
	lsr	w4,w4,1				; opos
	ldr	w5,[x19,m_aCount]
	add	w5,w5,15
	and	w5,w5,0xFFFFFFF0
	lsr	w5,w5,1				; count
	mov	w6,0				; k
	tbz	w0,0,.st
.z:	add	w7,w3,w6
	lsl	w7,w7,1
	a_put	wzr,w7
	add	w6,w6,1
	cmp	w6,4
	b.lo	.z
	mov	w8,0				; acc
	b	.go
.st:	add	w7,w10,w6,lsl 1
	rd_s16	w8,w7
	add	w7,w3,w6
	lsl	w7,w7,1
	a_put	w8,w7
	add	w6,w6,1
	cmp	w6,4
	b.lo	.st
	add	w7,w10,8
	rd_s16	w8,w7
	and	w8,w8,0xFFFF
.go:	adr	x11,rs_lut
.l:	cbz	w5,.end
	sub	w5,w5,1
	ubfx	w6,w8,10,6
	add	x6,x11,x6,lsl 3			; lut
	mov	w7,0				; v
	mov	x12,0				; k
.t:	add	w13,w3,w12
	lsl	w13,w13,1
	a_get	w13,w13
	ldrsh	w14,[x6,x12,lsl 1]
	mul	w13,w13,w14
	add	w7,w7,w13,asr 15
	add	x12,x12,1
	cmp	x12,4
	b.lo	.t
	clamp_s16 w7
	lsl	w13,w4,1
	a_put	w7,w13
	add	w4,w4,1
	add	w8,w8,w9
	add	w3,w3,w8,lsr 16
	and	w8,w8,0xFFFF
	b	.l
.end:	mov	w6,0
.e1:	add	w7,w3,w6
	lsl	w7,w7,1
	a_get	w7,w7
	add	w12,w10,w6,lsl 1
	wr_s16	w7,w12
	add	w6,w6,1
	cmp	w6,4
	b.lo	.e1
	add	w7,w10,8
	wr_s16	w8,w7
	ldp	x23,x24,[sp],16
	ret

; w0, w1 : la commande
a_env_mix:
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	ubfx	w2,w0,12,12
	and	w2,w2,0xFF0			; in
	ubfx	w3,w0,8,8			; count
	and	w5,w0,8
	lsr	w5,w5,1
	neg	w5,w5
	sxth	w5,w5				; x2
	and	w6,w0,4
	lsr	w6,w6,1
	neg	w6,w6
	sxth	w6,w6				; x3
	and	w7,w0,2
	lsr	w7,w7,1
	neg	w7,w7
	sxth	w7,w7				; x0
	and	w8,w0,1
	neg	w8,w8
	sxth	w8,w8				; x1
	ubfx	w9,w1,20,12
	and	w9,w9,0xFF0			; dl
	ubfx	w10,w1,12,12
	and	w10,w10,0xFF0			; dr
	ubfx	w11,w1,4,12
	and	w11,w11,0xFF0			; wl
	lsl	w12,w1,4
	and	w12,w12,0xFF0			; wr
	tbz	w0,4,.ns
	mov	w13,w11
	mov	w11,w12
	mov	w12,w13
.ns:	add	w3,w3,7
	and	w3,w3,0xFFFFFFF8
	add	x23,x19,m_aEnvV
.c:	cbz	w3,.end
	mov	w13,0				; i
.i:	add	w14,w2,w13,lsl 1
	a_get	w14,w14				; s
	ldrh	w15,[x23]
	mul	w0,w14,w15
	asr	w0,w0,16
	eor	w0,w0,w7
	sxth	w0,w0				; l
	ldrh	w15,[x23,2]
	mul	w1,w14,w15
	asr	w1,w1,16
	eor	w1,w1,w8
	sxth	w1,w1				; r
	ldrh	w15,[x23,4]
	mul	w24,w0,w15
	asr	w24,w24,16
	eor	w24,w24,w5
	sxth	w24,w24				; l2
	mul	w25,w1,w15
	asr	w25,w25,16
	eor	w25,w25,w6
	sxth	w25,w25				; r2
	add	w26,w9,w13,lsl 1
	a_get	w27,w26
	add	w27,w27,w0
	clamp_s16 w27
	a_put	w27,w26
	add	w26,w10,w13,lsl 1
	a_get	w27,w26
	add	w27,w27,w1
	clamp_s16 w27
	a_put	w27,w26
	add	w26,w11,w13,lsl 1
	a_get	w27,w26
	add	w27,w27,w24
	clamp_s16 w27
	a_put	w27,w26
	add	w26,w12,w13,lsl 1
	a_get	w27,w26
	add	w27,w27,w25
	clamp_s16 w27
	a_put	w27,w26
	add	w13,w13,1
	cmp	w13,8
	b.lo	.i
	mov	x13,0
.v:	ldrh	w14,[x23,x13,lsl 1]
	add	x15,x19,m_aEnvS
	ldrh	w15,[x15,x13,lsl 1]
	add	w14,w14,w15
	strh	w14,[x23,x13,lsl 1]
	add	x13,x13,1
	cmp	x13,3
	b.lo	.v
	add	w2,w2,16
	add	w9,w9,16
	add	w10,w10,16
	add	w11,w11,16
	add	w12,w12,16
	sub	w3,w3,8
	b	.c
.end:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ret

; x0 = M_ADMEM base + o : octet n'importe
audio_task:
	prologue
	save_callee
	add	x21,x19,M_ADMEM
	add	x22,x19,M_RDRAM
	add	x0,x19,M_DMEM
	ldr	w23,[x0,0xFD8]
	and	w23,w23,0x7FFFFF		; ucodeData
	add	w0,w23,0x10
	rd_w	w24,w0				; kind
	rd_w	w0,w23
	cmp	w0,1
	b.ne	.unk
	add	w0,w23,0x30
	rd_w	w0,w0
	movl	w1,0xF0000F00
	cmp	w0,w1
	b.eq	.unk
	movl	w0,0x1F681230
	cmp	w24,w0
	b.eq	.go
	movl	w0,0x1F801250
	cmp	w24,w0
	b.eq	.go
	movl	w0,0x109411F8
	cmp	w24,w0
	b.eq	.go
.unk:	ldr	w0,[x19,m_audioUnknown]
	add	w0,w0,1
	str	w0,[x19,m_audioUnknown]
	b	.out
.go:	add	x0,x19,M_DMEM
	ldr	w23,[x0,0xFF0]
	and	w23,w23,0x7FFFFF		; list
	ldr	w24,[x0,0xFF4]			; size
	mov	w0,0x10000
	cmp	w24,w0
	csel	w24,w0,w24,hi
	mov	w25,0				; p
.l:	add	w0,w25,8
	cmp	w0,w24
	b.hi	.done
	add	w0,w23,w25
	rd_w	w26,w0				; w0
	add	w0,w23,w25
	add	w0,w0,4
	rd_w	w27,w0				; w1
	add	w25,w25,8
	lsr	w0,w26,24
	cmp	w0,23
	b.hs	.l
	jtab	x0,.t
.t:	dw	.l-.t,.c1-.t,.c2-.t,.l-.t,.c4-.t,.c5-.t,.c6-.t,.l-.t
	dw	.c8-.t,.c9-.t,.c10-.t,.c11-.t,.c12-.t,.c13-.t,.c14-.t,.c15-.t
	dw	.l-.t,.c17-.t,.c18-.t,.c19-.t,.c20-.t,.c21-.t,.c22-.t
.c1:	ubfx	w0,w26,16,8
	and	w1,w27,0xFFFFFF
	bl	a_adpcm
	b	.l
.c2:	and	w0,w26,0xFFFF
	and	w1,w27,0xFFF
	add	w1,w1,15
	and	w1,w1,0xFFFFFFF0
	mov	w2,0
.c2l:	cmp	w2,w1
	b.hs	.l
	add	w3,w0,w2
	and	w3,w3,0xFFF
	strb	wzr,[x21,x3]
	add	w2,w2,1
	b	.c2l
.c4:	ubfx	w0,w26,12,12
	and	w0,w0,0xFF0			; n
	lsr	w1,w27,16			; i0
	and	w2,w27,0xFFFF			; o
	mov	w3,0
.c4l:	cmp	w3,w0
	b.hs	.l
	add	w4,w2,w3
	a_get	w5,w4
	add	w6,w1,w3
	a_get	w6,w6
	add	w5,w5,w6
	clamp_s16 w5
	a_put	w5,w4
	add	w3,w3,2
	b	.c4l
.c5:	ubfx	w0,w26,16,8
	and	w1,w26,0xFFFF
	lsl	w1,w1,1
	and	w2,w27,0xFFFFFF
	bl	a_resample
	b	.l
.c6:	and	w0,w26,0xFFFF
	lsl	w0,w0,1				; pitch
	and	w1,w27,0xFFFF			; acc
	ldr	w2,[x19,m_aIn]
	lsr	w2,w2,1				; ip
	ldr	w3,[x19,m_aOut]
	lsr	w3,w3,1				; op
	ldr	w4,[x19,m_aCount]
	lsr	w4,w4,1				; n
.c6l:	cbz	w4,.l
	sub	w4,w4,1
	lsl	w5,w2,1
	a_get	w5,w5
	lsl	w6,w3,1
	a_put	w5,w6
	add	w3,w3,1
	add	w1,w1,w0
	add	w2,w2,w1,lsr 16
	and	w1,w1,0xFFFF
	b	.c6l
.c8:	and	w0,w26,0xFFFF
	str	w0,[x19,m_aIn]
	lsr	w0,w27,16
	str	w0,[x19,m_aOut]
	and	w0,w27,0xFFFF
	str	w0,[x19,m_aCount]
	b	.l
.c9:	ubfx	w0,w26,16,8			; n
	and	w1,w26,0xFFFF			; i0
	lsr	w2,w27,16			; o
	sub	sp,sp,128
	mov	x3,0
.c9a:	add	w4,w1,w3
	and	w4,w4,0xFFF
	ldrb	w4,[x21,x4]
	strb	w4,[sp,x3]
	add	x3,x3,1
	cmp	x3,128
	b.lo	.c9a
.c9b:	cbz	w0,.c9e
	sub	w0,w0,1
	mov	x3,0
.c9c:	ldrb	w4,[sp,x3]
	add	w5,w2,w3
	and	w5,w5,0xFFF
	strb	w4,[x21,x5]
	add	x3,x3,1
	cmp	x3,128
	b.lo	.c9c
	add	w2,w2,128
	b	.c9b
.c9e:	add	sp,sp,128
	b	.l
.c10:	and	w0,w26,0xFFFF			; i0
	lsr	w1,w27,16			; o
	and	w2,w27,0xFFFF
	add	w2,w2,3
	and	w2,w2,0xFFFFFFFC		; n
	cmp	w1,w0
	b.ls	.c10f
	add	w3,w0,w2
	cmp	w1,w3
	b.hs	.c10f
.c10b:	cbz	w2,.l
	sub	w2,w2,1
	add	w3,w0,w2
	and	w3,w3,0xFFF
	ldrb	w3,[x21,x3]
	add	w4,w1,w2
	and	w4,w4,0xFFF
	strb	w3,[x21,x4]
	b	.c10b
.c10f:	mov	w5,0
.c10l:	cmp	w5,w2
	b.hs	.l
	add	w3,w0,w5
	and	w3,w3,0xFFF
	ldrb	w3,[x21,x3]
	add	w4,w1,w5
	and	w4,w4,0xFFF
	strb	w3,[x21,x4]
	add	w5,w5,1
	b	.c10l
.c11:	and	w0,w26,0xFFFF
	lsr	w0,w0,1				; n
	mov	w1,256
	cmp	w0,w1
	csel	w0,w1,w0,hi
	and	w1,w27,0xFFFFFF			; a
	add	x3,x19,M_ABOOK
	mov	x2,0
.c11l:	cmp	x2,x0
	b.hs	.l
	add	w4,w1,w2,lsl 1
	rd_s16	w5,w4
	strh	w5,[x3,x2,lsl 1]
	add	x2,x2,1
	b	.c11l
.c12:	ubfx	w0,w26,12,12
	and	w0,w0,0xFF0
	lsr	w1,w27,16
	and	w2,w27,0xFFFF
	sxth	w3,w26				; gain
	mov	w4,0
.c12l:	cmp	w4,w0
	b.hs	.l
	add	w5,w2,w4
	a_get	w6,w5
	add	w7,w1,w4
	a_get	w7,w7
	mul	w7,w7,w3
	add	w6,w6,w7,asr 15
	clamp_s16 w6
	a_put	w6,w5
	add	w4,w4,2
	b	.c12l
.c13:	ubfx	w0,w26,12,12
	and	w0,w0,0xFF0
	lsr	w0,w0,1				; n
	and	w1,w26,0xFFFF			; o
	lsr	w2,w27,16			; l
	and	w3,w27,0xFFFF			; r
	mov	w4,0x400
	cmp	w0,w4
	csel	w0,w4,w0,hi
	sub	sp,sp,0x1000
	mov	x4,0
.c13a:	cmp	w4,w0
	b.hs	.c13b
	add	w5,w2,w4,lsl 1
	a_get	w5,w5
	add	x6,sp,x4,lsl 2
	strh	w5,[x6]
	add	w5,w3,w4,lsl 1
	a_get	w5,w5
	strh	w5,[x6,2]
	add	x4,x4,1
	b	.c13a
.c13b:	mov	x4,0
	lsl	w0,w0,1
.c13c:	cmp	w4,w0
	b.hs	.c13e
	ldrh	w5,[sp,x4,lsl 1]
	add	w6,w1,w4,lsl 1
	a_put	w5,w6
	add	x4,x4,1
	b	.c13c
.c13e:	add	sp,sp,0x1000
	b	.l
.c14:	ubfx	w0,w26,16,8
	sxtb	w0,w0				; gain
	and	w1,w26,0xFFFF			; n
	lsr	w2,w27,16			; d
	mov	w3,0
.c14l:	cmp	w3,w1
	b.hs	.l
	add	w4,w2,w3
	a_get	w5,w4
	mul	w5,w5,w0
	asr	w5,w5,4
	clamp_s16 w5
	a_put	w5,w4
	add	w3,w3,2
	b	.c14l
.c15:	and	w0,w27,0xFFFFFF
	str	w0,[x19,m_aLoop]
	b	.l
.c17:	and	w0,w26,0xFFFF			; n
	lsr	w1,w27,16			; i0
	and	w2,w27,0xFFFF			; o
.c17l:	cbz	w0,.l
	sub	w0,w0,1
	a_get	w3,w1
	a_put	w3,w2
	add	w2,w2,2
	add	w1,w1,4
	b	.c17l
.c18:	lsr	w0,w26,8
	and	w0,w0,0xFF00
	strh	w0,[x19,m_aEnvV+4]
	strh	w26,[x19,m_aEnvS+4]
	lsr	w0,w27,16
	strh	w0,[x19,m_aEnvS]
	strh	w27,[x19,m_aEnvS+2]
	b	.l
.c19:	mov	w0,w26
	mov	w1,w27
	bl	a_env_mix
	b	.l
.c20:	and	w0,w26,0xFFFF
	and	w1,w27,0xFFFFFF
	ubfx	w2,w26,12,12
	and	w2,w2,0xFF0
	bl	a_load
	b	.l
.c21:	and	w0,w26,0xFFFF
	and	w1,w27,0xFFFFFF
	ubfx	w2,w26,12,12
	and	w2,w2,0xFF0
	bl	a_save
	b	.l
.c22:	lsr	w0,w27,16
	strh	w0,[x19,m_aEnvV]
	strh	w27,[x19,m_aEnvV+2]
	b	.l
.done:	ldr	w0,[x19,m_audioTasks]
	add	w0,w0,1
	str	w0,[x19,m_audioTasks]
.out:	rest_callee
	epilogue

; ---- l'AI ----

; -> w0 = le debit de l'AI
ai_rate:
	movl	w0,48681812
	ldrb	w1,[x19,m_pal]
	cbz	w1,.a
	movl	w0,49656530
.a:	ldr	w1,[x19,m_ai+16]
	and	w1,w1,0x3FFF
	add	w1,w1,1
	udiv	w0,w0,w1
	mov	w1,4000
	cmp	w0,w1
	csel	w0,w1,w0,lo
	ret

; w0 = addr, w1 = len, w2 = rate
ai_output:
	ldr	w3,[x19,m_outRate]		; outRate
	cbz	w3,.r
	lsr	w4,w1,2				; n
	ldr	w5,[x19,m_resAcc]
	ldr	w6,[x19,m_lastL]
	ldr	w7,[x19,m_lastR]
	add	x8,x19,M_RDRAM
	movl	x17,M_ABUF
	add	x17,x19,x17
	ldr	w9,[x19,m_aHead]
	ldr	w10,[x19,m_aTail]
.l:	cbz	w4,.f
	sub	w4,w4,1
	and	w11,w0,0x7FFFFC
	ldr	w11,[x8,x11]
	add	w0,w0,4
	asr	w12,w11,16			; l
	sxth	w13,w11				; r
	add	w5,w5,w3
.w:	cmp	w5,w2
	b.lo	.n
	sub	w5,w5,w2
	lsl	w14,w5,8
	udiv	w14,w14,w3			; f
	sub	w15,w6,w12
	mul	w15,w15,w14
	add	w15,w12,w15,asr 8		; ol
	sub	w16,w7,w13
	mul	w16,w16,w14
	add	w16,w13,w16,asr 8		; orr
	add	w14,w9,1
	and	w14,w14,N64_ABUF-1		; nh
	cmp	w14,w10
	b.ne	.p
	add	w10,w10,1
	and	w10,w10,N64_ABUF-1
.p:	add	x1,x17,x9,lsl 2
	strh	w15,[x1]
	strh	w16,[x1,2]
	mov	w9,w14
	b	.w
.n:	mov	w6,w12
	mov	w7,w13
	b	.l
.f:	str	w5,[x19,m_resAcc]
	str	w6,[x19,m_lastL]
	str	w7,[x19,m_lastR]
	str	w9,[x19,m_aHead]
	str	w10,[x19,m_aTail]
.r:	ret

rs_lut:
include 'n64_rslut.inc'
