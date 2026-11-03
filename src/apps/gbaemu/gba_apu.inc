; ============================================================================
;  gba_apu.inc - le son : les 4 voix PSG du Game Boy (registres 0x60-0x84, RAM
;  d'onde en deux banques) et Direct Sound A / B (deux FIFO de 32 octets, lues
;  aux debordements d'un minuteur, remplies par DMA 1 / 2). apu_run amene le son
;  au cycle courant, avant chaque acces aux registres et chaque echantillon.
; ============================================================================

; x1 = voix : volume, enveloppe depuis env (w3) ; perd w4
macro bl_env reg, oV, oP, oD, oT {
	lsr	w4,w3,4
	str	w4,[reg,oV]
	and	w4,w3,7
	str	w4,[reg,oP]
	str	w4,[reg,oT]
	mov	w4,1
	tst	w3,8
	csneg	w4,w4,w4,ne
	str	w4,[reg,oD]
}

apu_duty:
	db	0x01,0x81,0x87,0x7E
apu_vsh:
	db	4,0,1,2

; w0 = ch ; perd x0-x8
apu_trigger:
	cmp	w0,2
	b.hs	.nq
	add	x1,x19,a_sq
	mov	w2,SQ_SIZE
	mul	w2,w2,w0
	add	x1,x1,x2			; s
	mov	w3,0x63
	cbz	w0,.e
	mov	w3,0x69
.e:	add	x4,x19,A_IO
	ldrb	w3,[x4,x3]			; env
	tst	w3,0xF8
	cset	w4,ne
	str	w4,[x1,SQ_on]
	ldr	w4,[x1,SQ_len]
	cbnz	w4,.l
	mov	w4,64
	str	w4,[x1,SQ_len]
.l:	ldr	w4,[x1,SQ_freq]
	mov	w5,2048
	sub	w5,w5,w4
	lsl	w5,w5,4
	str	w5,[x1,SQ_timer]
	bl_env	x1,SQ_vol,SQ_envPeriod,SQ_envDir,SQ_envTimer
	cbnz	w0,.r
	io_b	w3,0x60				; sw
	ldr	w4,[x1,SQ_freq]
	str	w4,[x1,SQ_shadow]
	ubfx	w5,w3,4,3
	str	w5,[x1,SQ_sweepPeriod]
	mov	w6,1
	mov	w7,-1
	tst	w3,8
	csel	w6,w7,w6,ne
	str	w6,[x1,SQ_sweepDir]
	and	w7,w3,7
	str	w7,[x1,SQ_sweepShift]
	mov	w8,8
	cmp	w5,0
	csel	w8,w5,w8,ne
	str	w8,[x1,SQ_sweepTimer]
	orr	w8,w5,w7
	cmp	w8,0
	cset	w8,ne
	str	w8,[x1,SQ_sweepOn]
	cbz	w7,.r
	asr	w8,w4,w7
	mul	w8,w8,w6
	add	w8,w8,w4
	cmp	w8,2047
	b.le	.r
	str	wzr,[x1,SQ_on]
.r:	ret
.nq:	b.ne	.ns
	io_b	w3,0x70				; la voie d'onde
	ubfx	w3,w3,7,1
	str	w3,[x19,a_wv+WV_on]
	ldr	w4,[x19,a_wv+WV_len]
	cbnz	w4,.w1
	mov	w4,256
	str	w4,[x19,a_wv+WV_len]
.w1:	ldr	w4,[x19,a_wv+WV_freq]
	mov	w5,2048
	sub	w5,w5,w4
	lsl	w5,w5,3
	str	w5,[x19,a_wv+WV_timer]
	str	wzr,[x19,a_wv+WV_pos]
	ret
.ns:	add	x1,x19,a_ns
	io_b	w3,0x79
	tst	w3,0xF8
	cset	w4,ne
	str	w4,[x1,NS_on]
	ldr	w4,[x1,NS_len]
	cbnz	w4,.n1
	mov	w4,64
	str	w4,[x1,NS_len]
.n1:	mov	w4,0x7FFF
	str	w4,[x1,NS_lfsr]
	bl_env	x1,NS_vol,NS_envPeriod,NS_envDir,NS_envTimer
	ret

; w0 = off -> w0 ; perd x1-x3
apu_read:
	sub	w1,w0,0x90
	cmp	w1,0x10
	b.hs	.a
	io_b	w2,0x70
	add	w3,w1,16
	tst	w2,0x40
	csel	w1,w1,w3,ne
	add	x2,x19,a_waveRam
	ldrb	w0,[x2,x1]
	ret
.a:	cmp	w0,0x84
	b.ne	.b
	io_b	w0,0x84
	and	w0,w0,0x80
	ldr	w1,[x19,a_sq+SQ_on]
	orr	w0,w0,w1
	ldr	w1,[x19,a_sq+SQ_SIZE+SQ_on]
	orr	w0,w0,w1,lsl 1
	ldr	w1,[x19,a_wv+WV_on]
	orr	w0,w0,w1,lsl 2
	ldr	w1,[x19,a_ns+NS_on]
	orr	w0,w0,w1,lsl 3
	ret
.b:	cmp	w0,0x83
	b.ne	.c
	io_b	w0,0x83
	mov	w1,0x77
	and	w0,w0,w1
	ret
.c:	cmp	w0,0xA0
	b.hs	.z
	add	x1,x19,A_IO
	ldrb	w0,[x1,x0]
	ret
.z:	mov	w0,0
	ret

; w0 = off, w1 = v
apu_write:
	and	w1,w1,0xFF
	sub	w2,w0,0x90
	cmp	w2,0x10
	b.hs	.a
	io_b	w3,0x70
	add	w4,w2,16
	tst	w3,0x40
	csel	w2,w2,w4,ne
	add	x3,x19,a_waveRam
	strb	w1,[x3,x2]
	ret
.a:	cmp	w0,0x84
	b.ne	.b
	and	w2,w1,0x80
	strb	w2,[x19,A_IO+0x84]
	tbnz	w1,7,.r
	add	x2,x19,A_IO+0x60
	mov	w3,0x22
.z:	strb	wzr,[x2],1
	subs	w3,w3,1
	b.ne	.z
	str	wzr,[x19,a_sq+SQ_on]
	str	wzr,[x19,a_sq+SQ_SIZE+SQ_on]
	str	wzr,[x19,a_wv+WV_on]
	str	wzr,[x19,a_ns+NS_on]
.r:	ret
.b:	cmp	w0,0x80
	b.hs	.c
	io_b	w2,0x84
	tbz	w2,7,.r
.c:	add	x2,x19,A_IO
	strb	w1,[x2,x0]
	cmp	w0,0x62
	b.eq	.s62
	cmp	w0,0x68
	b.eq	.s68
	cmp	w0,0x63
	b.eq	.s63
	cmp	w0,0x69
	b.eq	.s69
	cmp	w0,0x64
	b.eq	.s64
	cmp	w0,0x6C
	b.eq	.s6c
	cmp	w0,0x65
	b.eq	.s65
	cmp	w0,0x6D
	b.eq	.s6d
	cmp	w0,0x70
	b.eq	.w70
	cmp	w0,0x72
	b.eq	.w72
	cmp	w0,0x73
	b.eq	.w73
	cmp	w0,0x74
	b.eq	.w74
	cmp	w0,0x75
	b.eq	.w75
	cmp	w0,0x78
	b.eq	.n78
	cmp	w0,0x79
	b.eq	.n79
	cmp	w0,0x7C
	b.eq	.n7c
	cmp	w0,0x7D
	b.eq	.n7d
	cmp	w0,0x83
	b.eq	.s83
	ret
.s62:	add	x2,x19,a_sq
.dl:	lsr	w3,w1,6
	str	w3,[x2,SQ_duty]
	and	w3,w1,63
	mov	w4,64
	sub	w3,w4,w3
	str	w3,[x2,SQ_len]
	ret
.s68:	add	x2,x19,a_sq+SQ_SIZE
	b	.dl
.s63:	add	x2,x19,a_sq
.en:	tst	w1,0xF8
	b.ne	.r
	str	wzr,[x2,SQ_on]
	ret
.s69:	add	x2,x19,a_sq+SQ_SIZE
	b	.en
.s64:	add	x2,x19,a_sq
.fl:	ldr	w3,[x2,SQ_freq]
	bfxil	w3,w1,0,8
	and	w3,w3,0x7FF
	str	w3,[x2,SQ_freq]
	ret
.s6c:	add	x2,x19,a_sq+SQ_SIZE
	b	.fl
.s65:	add	x2,x19,a_sq
	mov	w5,0
.fh:	ldr	w3,[x2,SQ_freq]
	and	w3,w3,0xFF
	and	w4,w1,7
	orr	w3,w3,w4,lsl 8
	str	w3,[x2,SQ_freq]
	ubfx	w3,w1,6,1
	str	w3,[x2,SQ_lenOn]
	tbz	w1,7,.r
	mov	w0,w5
	b	apu_trigger
.s6d:	add	x2,x19,a_sq+SQ_SIZE
	mov	w5,1
	b	.fh
.w70:	tbnz	w1,7,.r
	str	wzr,[x19,a_wv+WV_on]
	ret
.w72:	mov	w2,256
	sub	w2,w2,w1
	str	w2,[x19,a_wv+WV_len]
	ret
.w73:	str	w1,[x19,a_wv+WV_vol]
	ret
.w74:	ldr	w2,[x19,a_wv+WV_freq]
	bfxil	w2,w1,0,8
	and	w2,w2,0x7FF
	str	w2,[x19,a_wv+WV_freq]
	ret
.w75:	ldr	w2,[x19,a_wv+WV_freq]
	and	w2,w2,0xFF
	and	w3,w1,7
	orr	w2,w2,w3,lsl 8
	str	w2,[x19,a_wv+WV_freq]
	ubfx	w2,w1,6,1
	str	w2,[x19,a_wv+WV_lenOn]
	tbz	w1,7,.r
	mov	w0,2
	b	apu_trigger
.n78:	and	w2,w1,63
	mov	w3,64
	sub	w2,w3,w2
	str	w2,[x19,a_ns+NS_len]
	ret
.n79:	tst	w1,0xF8
	b.ne	.r
	str	wzr,[x19,a_ns+NS_on]
	ret
.n7c:	lsr	w2,w1,4
	str	w2,[x19,a_ns+NS_shift]
	ubfx	w2,w1,3,1
	str	w2,[x19,a_ns+NS_width]
	and	w2,w1,7
	str	w2,[x19,a_ns+NS_div]
	ret
.n7d:	ubfx	w2,w1,6,1
	str	w2,[x19,a_ns+NS_lenOn]
	tbz	w1,7,.r
	mov	w0,3
	b	apu_trigger
.s83:	tbz	w1,3,.f1
	str	wzr,[x19,a_fifoLen]
	str	wzr,[x19,a_fifoRd]
	str	wzr,[x19,a_fifoWr]
.f1:	tbz	w1,7,.f2
	str	wzr,[x19,a_fifoLen+4]
	str	wzr,[x19,a_fifoRd+4]
	str	wzr,[x19,a_fifoWr+4]
.f2:	mov	w2,0x77
	and	w1,w1,w2
	strb	w1,[x19,A_IO+0x83]
	ret

; x1 = la voix, ofs = son compteur : --len == 0 -> on = 0
macro len_step reg, ofL, ofE, ofO {
	ldr	w2,[reg,ofE]
	cbz	w2,@f
	ldr	w2,[reg,ofL]
	cmp	w2,0
	b.le	@f
	subs	w2,w2,1
	str	w2,[reg,ofL]
	b.ne	@f
	str	wzr,[reg,ofO]
@@:
}
macro env_step reg, ofP, ofT, ofV, ofD {
	ldr	w2,[reg,ofP]
	cbz	w2,@f
	ldr	w3,[reg,ofT]
	subs	w3,w3,1
	str	w3,[reg,ofT]
	b.gt	@f
	str	w2,[reg,ofT]
	ldr	w2,[reg,ofV]
	ldr	w3,[reg,ofD]
	add	w2,w2,w3
	cmp	w2,15
	b.hi	@f
	str	w2,[reg,ofV]
@@:
}

frame_sequencer:
	ldr	w0,[x19,a_frameSeq]		; st
	add	w1,w0,1
	and	w1,w1,7
	str	w1,[x19,a_frameSeq]
	add	x1,x19,a_sq
	add	x4,x1,SQ_SIZE
	add	x5,x19,a_wv
	add	x6,x19,a_ns
	tbnz	w0,0,.a
	len_step x1,SQ_len,SQ_lenOn,SQ_on
	len_step x4,SQ_len,SQ_lenOn,SQ_on
	len_step x5,WV_len,WV_lenOn,WV_on
	len_step x6,NS_len,NS_lenOn,NS_on
.a:	cmp	w0,2
	b.eq	.sw
	cmp	w0,6
	b.ne	.b
.sw:	ldr	w2,[x1,SQ_sweepOn]
	cbz	w2,.b
	ldr	w2,[x1,SQ_sweepTimer]
	subs	w2,w2,1
	str	w2,[x1,SQ_sweepTimer]
	b.gt	.b
	ldr	w3,[x1,SQ_sweepPeriod]
	mov	w2,8
	cmp	w3,0
	csel	w2,w3,w2,ne
	str	w2,[x1,SQ_sweepTimer]
	cbz	w3,.b
	ldr	w2,[x1,SQ_shadow]
	ldr	w3,[x1,SQ_sweepShift]
	ldr	w7,[x1,SQ_sweepDir]
	asr	w8,w2,w3
	mul	w8,w8,w7
	add	w2,w2,w8			; nf
	cmp	w2,2047
	b.le	.s1
	str	wzr,[x1,SQ_on]
	b	.b
.s1:	cbz	w3,.b
	str	w2,[x1,SQ_shadow]
	str	w2,[x1,SQ_freq]
	asr	w8,w2,w3
	add	w8,w8,w2
	cmp	w8,2047
	b.le	.b
	cmp	w7,0
	b.le	.b
	str	wzr,[x1,SQ_on]
.b:	cmp	w0,7
	b.ne	.r
	env_step x1,SQ_envPeriod,SQ_envTimer,SQ_vol,SQ_envDir
	env_step x4,SQ_envPeriod,SQ_envTimer,SQ_vol,SQ_envDir
	env_step x6,NS_envPeriod,NS_envTimer,NS_vol,NS_envDir
.r:	ret

; x1 = la voix carree -> w0 = sa sortie
sq_out:
	mov	w0,0
	ldr	w2,[x1,SQ_on]
	cbz	w2,.r
	ldr	w2,[x1,SQ_duty]
	adr	x3,apu_duty
	ldrb	w2,[x3,x2]
	ldr	w3,[x1,SQ_dutyPos]
	mov	w4,7
	sub	w3,w4,w3
	lsr	w2,w2,w3
	tbz	w2,0,.r
	ldr	w0,[x1,SQ_vol]
.r:	ret

mix_sample:
	mov	w5,0				; left
	mov	w6,0				; right
	io_b	w0,0x84
	tbz	w0,7,.p
	stp	x9,x10,[sp,-16]!
	mov	x10,x30
	add	x1,x19,a_sq
	bl	sq_out
	mov	w8,w0
	add	x1,x1,SQ_SIZE
	bl	sq_out
	mov	w9,w0
	mov	x30,x10
	mov	w7,0				; out [2]
	ldr	w0,[x19,a_wv+WV_on]
	cbz	w0,.w
	ldr	w0,[x19,a_wv+WV_sample]
	ldr	w1,[x19,a_wv+WV_vol]
	tbz	w1,7,.v
	add	w7,w0,w0,lsl 1
	asr	w7,w7,2
	b	.w
.v:	ubfx	w1,w1,5,2
	adr	x2,apu_vsh
	ldrb	w1,[x2,x1]
	asr	w7,w0,w1
.w:	mov	w4,0				; out [3]
	ldr	w0,[x19,a_ns+NS_on]
	cbz	w0,.n
	ldr	w0,[x19,a_ns+NS_lfsr]
	tbnz	w0,0,.n
	ldr	w4,[x19,a_ns+NS_vol]
.n:	io_b	w0,0x81				; pan
	mov	w1,0				; pl
	mov	w2,0				; pr
	mov	w16,15
	lsl	w3,w8,1
	sub	w3,w3,w16
	tbz	w0,4,.a1
	add	w1,w1,w3
.a1:	tbz	w0,0,.a2
	add	w2,w2,w3
.a2:	lsl	w3,w9,1
	sub	w3,w3,w16
	tbz	w0,5,.b1
	add	w1,w1,w3
.b1:	tbz	w0,1,.b2
	add	w2,w2,w3
.b2:	lsl	w3,w7,1
	sub	w3,w3,w16
	tbz	w0,6,.c1
	add	w1,w1,w3
.c1:	tbz	w0,2,.c2
	add	w2,w2,w3
.c2:	lsl	w3,w4,1
	sub	w3,w3,w16
	tbz	w0,7,.d1
	add	w1,w1,w3
.d1:	tbz	w0,3,.d2
	add	w2,w2,w3
.d2:	ldp	x9,x10,[sp],16
	io_b	w0,0x80
	ubfx	w3,w0,4,3
	add	w3,w3,1
	mul	w1,w1,w3
	and	w3,w0,7
	add	w3,w3,1
	mul	w2,w2,w3
	io_h	w0,0x82				; h
	and	w3,w0,3
	mov	w4,0
	cmp	w3,1
	b.hi	.sh
	mov	w4,2
	b.ne	.sh
	mov	w4,1
.sh:	asr	w5,w1,w4
	asr	w6,w2,w4
	ldr	w1,[x19,a_dsOut]
	lsl	w1,w1,1
	tbz	w0,2,.e1
	lsl	w1,w1,1
.e1:	ldr	w2,[x19,a_dsOut+4]
	lsl	w2,w2,1
	tbz	w0,3,.e2
	lsl	w2,w2,1
.e2:	tbz	w0,8,.g1
	add	w6,w6,w1
.g1:	tbz	w0,9,.g2
	add	w5,w5,w1
.g2:	tbz	w0,12,.g3
	add	w6,w6,w2
.g3:	tbz	w0,13,.p
	add	w5,w5,w2
.p:	lsl	w5,w5,5
	lsl	w6,w6,5
	mov	w0,32767
	mov	w1,-32768
	cmp	w5,w0
	csel	w5,w0,w5,gt
	cmp	w5,w1
	csel	w5,w1,w5,lt
	cmp	w6,w0
	csel	w6,w0,w6,gt
	cmp	w6,w1
	csel	w6,w1,w6,lt
	ldr	w0,[x19,a_atail]
	add	w1,w0,1
	and	w1,w1,GBA_ABUF-1
	ldr	w2,[x19,a_ahead]
	cmp	w1,w2
	b.eq	.r
	add	x2,x19,A_ABUF
	add	x2,x2,x0,lsl 2
	strh	w5,[x2]
	strh	w6,[x2,2]
	str	w1,[x19,a_atail]
.r:	ret

; le son jusqu'a maintenant ; garde x9-x15
apu_run:
	ldr	x0,[x19,a_apuLast]
	cmp	x0,x20
	b.lo	.go
	ret
.go:	push_x9
	ldr	w9,[x19,a_rate]			; rate
.l:	ldr	x10,[x19,a_apuLast]
	cmp	x10,x20
	b.hs	.f
	ldr	x0,[x19,a_sampleAcc]
	mov	x1,GBA_CPU_HZ
	sub	x0,x1,x0
	add	x0,x0,x9
	sub	x0,x0,1
	sdiv	x0,x0,x9			; need
	mov	x1,1
	cmp	x0,1
	csel	x0,x1,x0,lt			; n
	sub	x1,x20,x10
	cmp	x0,x1
	csel	x11,x1,x0,hi			; n
	add	x1,x19,a_sq
	mov	w2,2
.sq:	ldr	w3,[x1,SQ_timer]
	sub	w3,w3,w11
	ldr	w4,[x1,SQ_freq]
	mov	w5,2048
	sub	w4,w5,w4
	lsl	w4,w4,4
	ldr	w5,[x1,SQ_dutyPos]
.sw:	cmp	w3,0
	b.gt	.se
	add	w3,w3,w4
	add	w5,w5,1
	and	w5,w5,7
	b	.sw
.se:	str	w3,[x1,SQ_timer]
	str	w5,[x1,SQ_dutyPos]
	add	x1,x1,SQ_SIZE
	subs	w2,w2,1
	b.ne	.sq
	ldr	w3,[x19,a_wv+WV_timer]
	sub	w3,w3,w11
.ww:	cmp	w3,0
	b.gt	.we
	ldr	w4,[x19,a_wv+WV_freq]
	mov	w5,2048
	sub	w4,w5,w4
	add	w3,w3,w4,lsl 3
	io_b	w4,0x70
	ldr	w5,[x19,a_wv+WV_pos]
	add	w5,w5,1
	mov	w6,31
	tbz	w4,5,.w32
	mov	w6,63
.w32:	and	w5,w5,w6
	str	w5,[x19,a_wv+WV_pos]
	mov	w6,0
	tbz	w4,6,.wb
	mov	w6,16
.wb:	add	w6,w6,w5,lsr 1
	and	w6,w6,31
	add	x7,x19,a_waveRam
	ldrb	w6,[x7,x6]
	lsr	w7,w6,4
	tbz	w5,0,.wh
	and	w7,w6,15
.wh:	str	w7,[x19,a_wv+WV_sample]
	b	.ww
.we:	str	w3,[x19,a_wv+WV_timer]
	ldr	w3,[x19,a_ns+NS_timer]
	sub	w3,w3,w11
.nw:	cmp	w3,0
	b.gt	.ne
	ldr	w4,[x19,a_ns+NS_div]
	lsl	w5,w4,4
	mov	w6,8
	cmp	w4,0
	csel	w5,w6,w5,eq
	ldr	w4,[x19,a_ns+NS_shift]
	lsl	w5,w5,w4
	add	w3,w3,w5,lsl 2
	ldr	w4,[x19,a_ns+NS_lfsr]
	eor	w5,w4,w4,lsr 1
	and	w5,w5,1				; bit
	lsr	w4,w4,1
	orr	w4,w4,w5,lsl 14
	ldr	w6,[x19,a_ns+NS_width]
	cbz	w6,.nv
	bfi	w4,w5,6,1
.nv:	str	w4,[x19,a_ns+NS_lfsr]
	b	.nw
.ne:	str	w3,[x19,a_ns+NS_timer]
	ldr	w0,[x19,a_fsTimer]
	add	w0,w0,w11
.fs:	cmp	w0,32768
	b.lt	.fe
	sub	w0,w0,32768
	str	w0,[x19,a_fsTimer]
	bl	frame_sequencer
	ldr	w0,[x19,a_fsTimer]
	b	.fs
.fe:	str	w0,[x19,a_fsTimer]
	add	x10,x10,x11
	str	x10,[x19,a_apuLast]
	ldr	x0,[x19,a_sampleAcc]
	mul	x1,x11,x9
	add	x0,x0,x1
	mov	x1,GBA_CPU_HZ
	cmp	x0,x1
	b.lt	.ns
	sub	x0,x0,x1
	str	x0,[x19,a_sampleAcc]
	bl	mix_sample
	b	.l
.ns:	str	x0,[x19,a_sampleAcc]
	b	.l
.f:	pop_x9
	ret

; w0 = ch, w1 = v
fifo_push:
	add	x2,x19,a_fifoLen
	ldr	w3,[x2,x0,lsl 2]
	cmp	w3,32
	b.ge	.r
	add	w3,w3,1
	str	w3,[x2,x0,lsl 2]
	add	x2,x19,a_fifoWr
	ldr	w3,[x2,x0,lsl 2]
	add	x4,x19,a_fifo
	add	x4,x4,x0,lsl 5
	strb	w1,[x4,x3]
	add	w3,w3,1
	and	w3,w3,31
	str	w3,[x2,x0,lsl 2]
.r:	ret

; w0 = le minuteur qui deborde
fifo_timer:
	push_x9
	mov	w9,w0				; t
	mov	w10,0				; ch
.c:	io_h	w11,0x82			; h
	lsl	w0,w10,2
	add	w1,w0,10
	lsr	w1,w11,w1
	and	w1,w1,1
	cmp	w1,w9
	b.ne	.n
	mov	w1,0x300
	lsl	w1,w1,w0
	tst	w11,w1
	b.eq	.n
	bl	apu_run
	add	x1,x19,a_fifoLen
	ldr	w2,[x1,x10,lsl 2]
	cmp	w2,0
	b.le	.t
	sub	w2,w2,1
	str	w2,[x1,x10,lsl 2]
	add	x1,x19,a_fifoRd
	ldr	w3,[x1,x10,lsl 2]
	add	x4,x19,a_fifo
	add	x4,x4,x10,lsl 5
	ldrsb	w4,[x4,x3]
	add	x5,x19,a_dsOut
	str	w4,[x5,x10,lsl 2]
	add	w3,w3,1
	and	w3,w3,31
	str	w3,[x1,x10,lsl 2]
.t:	cmp	w2,16
	b.gt	.n
	movl	w12,0x040000A0
	cbz	w10,.d
	movl	w12,0x040000A4
.d:	mov	w13,1				; d
.dl:	add	x1,x19,a_dma
	add	x1,x1,x13,lsl 4
	ldrb	w2,[x1,D_on]
	cbz	w2,.dn
	ldrh	w2,[x1,D_ctl]
	ubfx	w2,w2,12,2
	cmp	w2,3
	b.ne	.dn
	ldr	w2,[x1,D_dst]
	cmp	w2,w12
	b.ne	.dn
	mov	w0,w13
	bl	dma_run
	b	.n
.dn:	add	w13,w13,1
	cmp	w13,2
	b.ls	.dl
.n:	add	w10,w10,1
	cmp	w10,2
	b.lo	.c
	pop_x9
	ret
