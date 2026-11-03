; mix.inc - le son : SND_PaintChannelFrom8, SND_PaintChannelFrom16, Snd_WriteLinearBlastStereo16 (snd_mix.c),
; et la conversion de l'image (vid.c)

CH_LEFTVOL	= 8			; channel_t
CH_RIGHTVOL	= 12
CH_POS		= 20
SC_DATA		= 20			; sfxcache_t

; x0 = ch, x1 = sc, w2 = count
public SND_PaintChannelFrom8
SND_PaintChannelFrom8:
	ldp	w3,w4,[x0,CH_LEFTVOL]
	cmp	w3,255
	mov	w5,255
	csel	w3,w5,w3,gt
	cmp	w4,255
	csel	w4,w5,w4,gt
	stp	w3,w4,[x0,CH_LEFTVOL]
	asr	w3,w3,3				; (snd_scaletable : i = vol >> 3, data * i * 8)
	asr	w4,w4,3
	lsl	w3,w3,3
	lsl	w4,w4,3
	ldrsw	x5,[x0,CH_POS]
	add	x1,x1,SC_DATA
	add	x1,x1,x5
	la	x6,paintbuffer
	mov	w7,w2
	dup	v4.4s,w3
	dup	v5.4s,w4
	subs	w7,w7,4
	b.lt	.t
.q:	ldr	s0,[x1],4			; 4 echantillons signes
	sxtl	v0.8h,v0.8b
	sxtl	v0.4s,v0.4h
	mul	v1.4s,v0.4s,v4.4s
	mul	v2.4s,v0.4s,v5.4s
	ld2	{v6.4s,v7.4s},[x6]
	add	v6.4s,v6.4s,v1.4s
	add	v7.4s,v7.4s,v2.4s
	st2	{v6.4s,v7.4s},[x6],32
	subs	w7,w7,4
	b.ge	.q
.t:	adds	w7,w7,4
	b.eq	.end
.s:	ldrsb	w8,[x1],1
	mul	w9,w8,w3
	mul	w8,w8,w4
	ldp	w10,w11,[x6]
	add	w10,w10,w9
	add	w11,w11,w8
	stp	w10,w11,[x6],8
	subs	w7,w7,1
	b.ne	.s
.end:	add	w5,w5,w2
	str	w5,[x0,CH_POS]
	ret

; x0 = ch, x1 = sc, w2 = count
public SND_PaintChannelFrom16
SND_PaintChannelFrom16:
	ldp	w3,w4,[x0,CH_LEFTVOL]
	ldrsw	x5,[x0,CH_POS]
	add	x1,x1,SC_DATA
	add	x1,x1,x5,lsl 1
	la	x6,paintbuffer
	mov	w7,w2
	dup	v4.4s,w3
	dup	v5.4s,w4
	subs	w7,w7,4
	b.lt	.t
.q:	ldr	d0,[x1],8
	sxtl	v0.4s,v0.4h
	mul	v1.4s,v0.4s,v4.4s
	mul	v2.4s,v0.4s,v5.4s
	sshr	v1.4s,v1.4s,8
	sshr	v2.4s,v2.4s,8
	ld2	{v6.4s,v7.4s},[x6]
	add	v6.4s,v6.4s,v1.4s
	add	v7.4s,v7.4s,v2.4s
	st2	{v6.4s,v7.4s},[x6],32
	subs	w7,w7,4
	b.ge	.q
.t:	adds	w7,w7,4
	b.eq	.end
.s:	ldrsh	w8,[x1],2
	mul	w9,w8,w3
	mul	w8,w8,w4
	asr	w9,w9,8
	asr	w8,w8,8
	ldp	w10,w11,[x6]
	add	w10,w10,w9
	add	w11,w11,w8
	stp	w10,w11,[x6],8
	subs	w7,w7,1
	b.ne	.s
.end:	add	w5,w5,w2
	str	w5,[x0,CH_POS]
	ret

; snd_p, snd_out, snd_linear_count, snd_vol
public Snd_WriteLinearBlastStereo16
Snd_WriteLinearBlastStereo16:
	la	x0,snd_p
	ldr	x0,[x0]
	la	x1,snd_out
	ldr	x1,[x1]
	lw	w2,x9,snd_linear_count
	lw	w3,x9,snd_vol
	dup	v4.4s,w3
	subs	w2,w2,4
	b.lt	.t
.q:	ldr	q0,[x0],16
	mul	v0.4s,v0.4s,v4.4s
	sshr	v0.4s,v0.4s,8
	sqxtn	v0.4h,v0.4s
	str	d0,[x1],8
	subs	w2,w2,4
	b.ge	.q
.t:	adds	w2,w2,4
	b.le	.end
.s:	ldr	s0,[x0],4
	mul	v0.2s,v0.2s,v4.2s
	sshr	v0.2s,v0.2s,8
	sqxtn	v0.4h,v0.4s
	str	h0,[x1],2
	subs	w2,w2,1
	b.gt	.s
.end:	ret

; x0 = destination 0x00RRGGBB, x1 = son pas (octets), x2 = source 8 bits, w3 = son pas, w4 = largeur, w5 = hauteur,
; x6 = palette
public qa_present
qa_present:
	sxtw	x3,w3
.y:	mov	x7,x0
	mov	x8,x2
	mov	w9,w4
	subs	w9,w9,4
	b.lt	.t
.q:	ldrb	w10,[x8]
	ldrb	w11,[x8,1]
	ldrb	w12,[x8,2]
	ldrb	w13,[x8,3]
	add	x8,x8,4
	ldr	w10,[x6,x10,lsl 2]
	ldr	w11,[x6,x11,lsl 2]
	ldr	w12,[x6,x12,lsl 2]
	ldr	w13,[x6,x13,lsl 2]
	stp	w10,w11,[x7]
	stp	w12,w13,[x7,8]
	add	x7,x7,16
	subs	w9,w9,4
	b.ge	.q
.t:	adds	w9,w9,4
	b.eq	.n
.s:	ldrb	w10,[x8],1
	ldr	w10,[x6,x10,lsl 2]
	str	w10,[x7],4
	subs	w9,w9,1
	b.ne	.s
.n:	add	x0,x0,x1
	add	x2,x2,x3
	subs	w5,w5,1
	b.ne	.y
	ret
