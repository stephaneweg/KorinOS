; ============================================================================
;  gba_bios.inc - le BIOS de remplacement : les appels SWI faits ici, aucune
;  image du BIOS de Nintendo. Seule l'entree des interruptions, en 0x18, est du
;  code ARM (bios_irq, gba_core.inc).
; ============================================================================

include 'gba_tables.inc'

; w0 = i -> w0 (perd w1-w3)
arc_tan:
	mul	w1,w0,w0
	asr	w1,w1,14
	neg	w1,w1				; a
	mov	w2,0xA9
	mul	w2,w2,w1
	asr	w2,w2,14
	add	w2,w2,0x390			; b
macro at_step k {
	mul	w2,w2,w1
	asr	w2,w2,14
	movl	w3,k
	add	w2,w2,w3
}
	at_step	0x91C
	at_step	0xFB6
	at_step	0x16AA
	at_step	0x2081
	at_step	0x3651
	at_step	0xA2F9
	mul	w0,w0,w2
	asr	w0,w0,16
	ret

; w0 = x, w1 = y -> w0 (perd x1-x5)
arc_tan2:
	mov	x5,x30
	mov	w4,w0
	cbnz	w1,.a
	mov	w0,0x8000
	tbnz	w4,31,.r
	mov	w0,0
	b	.r
.a:	cbnz	w4,.b
	mov	w0,0xC000
	tbnz	w1,31,.r
	mov	w0,0x4000
	b	.r
.b:	tbnz	w1,31,.neg
	tbnz	w4,31,.xn
	cmp	w4,w1
	b.lt	.q1
	lsl	w0,w1,14
	sdiv	w0,w0,w4
	bl	arc_tan
	b	.m
.xn:	neg	w0,w4
	cmp	w0,w1
	b.lt	.q1
	lsl	w0,w1,14
	sdiv	w0,w0,w4
	bl	arc_tan
	add	w0,w0,0x8000
	b	.m
.q1:	lsl	w0,w4,14
	sdiv	w0,w0,w1
	bl	arc_tan
	mov	w1,0x4000
	sub	w0,w1,w0
	b	.m
.neg:	cmp	w4,0
	b.gt	.xp
	neg	w0,w4
	neg	w2,w1
	cmp	w0,w2
	b.le	.q3
	lsl	w0,w1,14
	sdiv	w0,w0,w4
	bl	arc_tan
	add	w0,w0,0x8000
	b	.m
.xp:	neg	w2,w1
	cmp	w4,w2
	b.lt	.q3
	lsl	w0,w1,14
	sdiv	w0,w0,w4
	bl	arc_tan
	add	w0,w0,0x10000
	b	.m
.q3:	lsl	w0,w4,14
	sdiv	w0,w0,w1
	bl	arc_tan
	mov	w1,0xC000
	sub	w0,w1,w0
.m:	and	w0,w0,0xFFFF
.r:	br	x5

; w0 = n -> w0
isqrt:
	mov	w1,0				; res
	mov	w2,0x40000000			; bit
.a:	cmp	w2,w0
	b.ls	.l
	lsr	w2,w2,2
	b	.a
.l:	cbz	w2,.f
	add	w3,w1,w2
	cmp	w0,w3
	b.lo	.n
	sub	w0,w0,w3
	add	w1,w2,w1,lsr 1
	b	.s
.n:	lsr	w1,w1,1
.s:	lsr	w2,w2,2
	b	.l
.f:	mov	w0,w1
	ret

; w0 = f -> w0 (perd w1-w4, x5)
pow2neg:
	lsl	w0,w0,6				; x
	mov	w1,3072
	udiv	w2,w0,w1			; k
	msub	w3,w2,w1,w0			; fr
	adr	x5,gba_pow
	ldr	w4,[x5,x2,lsl 2]
	add	x5,x5,4
	ldr	w5,[x5,x2,lsl 2]
	sub	w5,w4,w5
	mul	w5,w5,w3
	udiv	w5,w5,w1
	sub	w0,w4,w5
	ret

; x13 = half, x25 = vr, w10 = d, w11 = done : l'octet w0 de la sortie, done ++
put_b:
	str	x30,[sp,-16]!
	add	w1,w10,w11
	add	w11,w11,1
	cbz	w25,.b8
	tbnz	w1,0,.h
	mov	w13,w0
	b	.r
.h:	orr	w2,w13,w0,lsl 8
	and	w0,w1,0xFFFFFFFE
	mov	w1,w2
	bl	gba_write16
	b	.r
.b8:	mov	w2,w0
	mov	w0,w1
	mov	w1,w2
	bl	gba_write8
.r:	ldr	x30,[sp],16
	ret

; w0 = n
gba_swi:
	prologue
	sub	sp,sp,32
	mov	w15,w0				; n
	add	w21,w21,3
	movl	w0,0xE3A02004
	str	w0,[x19,a_biosLatch]
	cmp	w15,0x20
	b.hs	swi_end
	jtab	x15,.t
.t:	dw	sw_reset-.t,sw_ramreset-.t,sw_halt-.t,sw_halt-.t,sw_wait-.t,sw_wait-.t,sw_div-.t,sw_div-.t
	dw	sw_sqrt-.t,sw_atan-.t,sw_atan2-.t,sw_cpuset-.t,sw_fastset-.t,sw_sum-.t,sw_bgaff-.t,sw_objaff-.t
	dw	sw_bitun-.t,sw_lz77-.t,sw_lz77-.t,sw_huff-.t,sw_rl-.t,sw_rl-.t,sw_diff-.t,sw_diff-.t
	dw	sw_diff-.t,swi_end-.t,swi_end-.t,swi_end-.t,swi_end-.t,swi_end-.t,swi_end-.t,sw_midi-.t
swi_end:
	add	sp,sp,32
	epilogue

sw_reset:
	adm	x1,A_IWRAM
	movl	x2,0x7FFA
	ldrb	w9,[x1,x2]			; toRam
	movl	x2,0x7E00
	add	x0,x1,x2
	mov	x1,0x200
	bl	mem_zero
	mov	w0,0x13
	bl	set_cpsr
	movl	w0,0x03007FE0
	str	w0,[x19,a_r+13*4]
	str	wzr,[x19,a_r+14*4]
	str	wzr,[x19,a_spsr]
	mov	w0,0x12
	bl	set_cpsr
	movl	w0,0x03007FA0
	str	w0,[x19,a_r+13*4]
	str	wzr,[x19,a_r+14*4]
	str	wzr,[x19,a_spsr]
	mov	w0,0x1F
	bl	set_cpsr
	movl	w0,0x03007F00
	str	w0,[x19,a_r+13*4]
	mov	x1,0
.z:	str	wzr,[x19,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,13
	b.lo	.z
	str	wzr,[x19,a_r+14*4]
	bic	w22,w22,0x20
	mov	w0,0x08000000
	cbz	w9,.j
	mov	w0,0x02000000
.j:	bl	set_pc
	b	swi_end

sw_ramreset:
	ldr	w9,[x19,a_r]
	tbz	w9,0,.a
	adm	x0,A_EWRAM
	mov	x1,0x40000
	bl	mem_zero
.a:	tbz	w9,1,.b
	adm	x0,A_IWRAM
	mov	x1,0x7E00
	bl	mem_zero
.b:	tbz	w9,2,.c
	add	x0,x19,A_PAL
	mov	x1,0x400
	bl	mem_zero
.c:	tbz	w9,3,.d
	adm	x0,A_VRAM
	mov	x1,0x18000
	bl	mem_zero
.d:	tbz	w9,4,.e
	add	x0,x19,A_OAM
	mov	x1,0x400
	bl	mem_zero
.e:	tbz	w9,7,.f
	add	x0,x19,A_IO
	mov	x1,0x60
	bl	mem_zero
	mov	w0,0x100
	strh	w0,[x19,A_IO+0x20]
	strh	w0,[x19,A_IO+0x26]
	strh	w0,[x19,A_IO+0x30]
	strh	w0,[x19,A_IO+0x36]
.f:	mov	w0,0x80
	strb	w0,[x19,A_IO]
	strb	wzr,[x19,A_IO+1]
	b	swi_end

sw_halt:
	mov	w0,1
	strb	w0,[x19,a_halted]
	b	swi_end

sw_wait:
	ldr	w9,[x19,a_r]			; discard
	ldr	w10,[x19,a_r+4]			; mask
	cmp	w15,5
	b.ne	.a
	mov	w9,1
	mov	w10,1
	str	w9,[x19,a_r]
	str	w9,[x19,a_r+4]
.a:	mov	w0,1
	strb	w0,[x19,A_IO+0x208]
	bl	check_irq
	adm	x11,A_IWRAM
	add	x11,x11,0x7000
	ldrh	w12,[x11,0xFF8]			; bif
	ldr	w0,[x19,a_intrWait]
	cbnz	w0,.b
	mov	w0,1
	str	w0,[x19,a_intrWait]
	cbz	w9,.b
	bic	w12,w12,w10
.b:	tst	w12,w10
	b.eq	.h
	bic	w12,w12,w10
	str	wzr,[x19,a_intrWait]
	b	.s
.h:	mov	w0,1
	strb	w0,[x19,a_halted]
	ldr	w0,[x19,a_fetchPC]
	bl	set_pc
.s:	strh	w12,[x11,0xFF8]
	b	swi_end

sw_div:
	ldr	w0,[x19,a_r]			; num
	ldr	w1,[x19,a_r+4]			; den
	cmp	w15,6
	b.eq	.a
	mov	w2,w0
	mov	w0,w1
	mov	w1,w2
.a:	cbnz	w1,.b
	mov	w2,1
	mov	w3,-1
	cmp	w0,0
	csel	w2,w3,w2,lt
	str	w2,[x19,a_r]
	str	w0,[x19,a_r+4]
	mov	w2,1
	str	w2,[x19,a_r+12]
	b	swi_end
.b:	mov	w2,0x80000000
	cmp	w0,w2
	b.ne	.c
	cmn	w1,1
	b.ne	.c
	str	w2,[x19,a_r]
	str	wzr,[x19,a_r+4]
	str	w2,[x19,a_r+12]
	b	swi_end
.c:	sdiv	w2,w0,w1
	msub	w3,w2,w1,w0
	str	w2,[x19,a_r]
	str	w3,[x19,a_r+4]
	cmp	w2,0
	cneg	w2,w2,lt
	str	w2,[x19,a_r+12]
	add	w21,w21,20
	b	swi_end

sw_sqrt:
	ldr	w0,[x19,a_r]
	bl	isqrt
	str	w0,[x19,a_r]
	add	w21,w21,20
	b	swi_end

sw_atan:
	ldrsh	w0,[x19,a_r]
	bl	arc_tan
	and	w0,w0,0xFFFF
	str	w0,[x19,a_r]
	b	swi_end

sw_atan2:
	ldrsh	w0,[x19,a_r]
	ldrsh	w1,[x19,a_r+4]
	bl	arc_tan2
	str	w0,[x19,a_r]
	b	swi_end

sw_cpuset:
	ldr	w9,[x19,a_r]			; s
	ldr	w10,[x19,a_r+4]			; d
	ldr	w11,[x19,a_r+8]			; c
	and	w12,w11,0x1FFFFF		; cnt
	lsr	w0,w9,24
	cbnz	w0,.ok
	ldr	w0,[x19,a_fetchPC]
	cmp	w0,0x4000
	b.hs	swi_end
.ok:	mov	w13,0				; i
	tbz	w11,26,.h
	and	w9,w9,0xFFFFFFFC
	and	w10,w10,0xFFFFFFFC
	mov	w0,w9
	bl	gba_read32
	mov	w14,w0				; v
.w:	cmp	w13,w12
	b.hs	.f
	mov	w0,w14
	tbnz	w11,24,.w1
	mov	w0,w9
	bl	gba_read32
.w1:	mov	w1,w0
	mov	w0,w10
	bl	gba_write32
	add	w10,w10,4
	tbnz	w11,24,.w2
	add	w9,w9,4
.w2:	add	w13,w13,1
	b	.w
.h:	and	w9,w9,0xFFFFFFFE
	and	w10,w10,0xFFFFFFFE
	mov	w0,w9
	bl	gba_read16
	mov	w14,w0
.h0:	cmp	w13,w12
	b.hs	.f
	mov	w0,w14
	tbnz	w11,24,.h1
	mov	w0,w9
	bl	gba_read16
.h1:	mov	w1,w0
	mov	w0,w10
	bl	gba_write16
	add	w10,w10,2
	tbnz	w11,24,.h2
	add	w9,w9,2
.h2:	add	w13,w13,1
	b	.h0
.f:	add	w21,w21,w12,lsl 1
	b	swi_end

sw_fastset:
	ldr	w9,[x19,a_r]
	and	w9,w9,0xFFFFFFFC
	ldr	w10,[x19,a_r+4]
	and	w10,w10,0xFFFFFFFC
	ldr	w11,[x19,a_r+8]
	and	w12,w11,0x1FFFFF
	add	w12,w12,7
	and	w12,w12,0xFFFFFFF8
	mov	w0,w9
	bl	gba_read32
	mov	w14,w0
	mov	w13,0
.w:	cmp	w13,w12
	b.hs	.f
	mov	w0,w14
	tbnz	w11,24,.w1
	mov	w0,w9
	bl	gba_read32
.w1:	mov	w1,w0
	mov	w0,w10
	bl	gba_write32
	add	w10,w10,4
	tbnz	w11,24,.w2
	add	w9,w9,4
.w2:	add	w13,w13,1
	b	.w
.f:	add	w21,w21,w12
	b	swi_end

sw_sum:
	movl	w0,0xBAAE187F
	str	w0,[x19,a_r]
	b	swi_end

; w0 = a -> w0 : read16 signe
rd_s16:	str	x30,[sp,-16]!
	bl	gba_read16
	ldr	x30,[sp],16
	sxth	w0,w0
	ret

; w0 = th -> w1 = sin, w2 = cos (perd x3)
sin_cos:
	adr	x3,gba_sin
	ldrsh	w1,[x3,x0,lsl 1]
	add	w0,w0,64
	and	w0,w0,255
	ldrsh	w2,[x3,x0,lsl 1]
	ret

sw_bgaff:
	ldr	w9,[x19,a_r]			; s
	ldr	w10,[x19,a_r+4]			; d
	mov	w11,0				; k
.k:	ldr	w0,[x19,a_r+8]
	cmp	w11,w0
	b.hs	swi_end
	mov	w0,w9
	bl	gba_read32
	str	w0,[sp]				; ox
	add	w0,w9,4
	bl	gba_read32
	str	w0,[sp,4]			; oy
	add	w0,w9,8
	bl	rd_s16
	mov	w12,w0				; cx
	add	w0,w9,10
	bl	rd_s16
	mov	w13,w0				; cy
	add	w0,w9,12
	bl	rd_s16
	mov	w14,w0				; sx
	add	w0,w9,14
	bl	rd_s16
	mov	w15,w0				; sy
	add	w0,w9,16
	bl	gba_read16
	lsr	w0,w0,8
	bl	sin_cos
	mul	w25,w2,w14
	asr	w25,w25,14			; a
	mul	w27,w1,w14
	asr	w27,w27,14
	neg	w27,w27				; b
	mul	w28,w1,w15
	asr	w28,w28,14			; c
	mul	w14,w2,w15
	asr	w14,w14,14			; dd
	mov	w0,w10
	mov	w1,w25
	bl	gba_write16
	add	w0,w10,2
	mov	w1,w27
	bl	gba_write16
	add	w0,w10,4
	mov	w1,w28
	bl	gba_write16
	add	w0,w10,6
	mov	w1,w14
	bl	gba_write16
	mul	w0,w25,w12
	mul	w1,w27,w13
	add	w0,w0,w1
	ldr	w1,[sp]
	sub	w1,w1,w0
	add	w0,w10,8
	bl	gba_write32
	mul	w0,w28,w12
	mul	w1,w14,w13
	add	w0,w0,w1
	ldr	w1,[sp,4]
	sub	w1,w1,w0
	add	w0,w10,12
	bl	gba_write32
	add	w9,w9,20
	add	w10,w10,16
	add	w11,w11,1
	b	.k

sw_objaff:
	ldr	w9,[x19,a_r]
	ldr	w10,[x19,a_r+4]
	ldr	w12,[x19,a_r+12]		; stride
	mov	w11,0
.k:	ldr	w0,[x19,a_r+8]
	cmp	w11,w0
	b.hs	swi_end
	mov	w0,w9
	bl	rd_s16
	mov	w13,w0				; sx
	add	w0,w9,2
	bl	rd_s16
	mov	w14,w0				; sy
	add	w0,w9,4
	bl	gba_read16
	lsr	w0,w0,8
	bl	sin_cos
	mov	w25,w1				; sn
	mov	w27,w2				; cs
	mul	w1,w27,w13
	asr	w1,w1,14
	mov	w0,w10
	bl	gba_write16
	add	w10,w10,w12
	mul	w1,w25,w13
	asr	w1,w1,14
	neg	w1,w1
	mov	w0,w10
	bl	gba_write16
	add	w10,w10,w12
	mul	w1,w25,w14
	asr	w1,w1,14
	mov	w0,w10
	bl	gba_write16
	add	w10,w10,w12
	mul	w1,w27,w14
	asr	w1,w1,14
	mov	w0,w10
	bl	gba_write16
	add	w10,w10,w12
	add	w9,w9,8
	add	w11,w11,1
	b	.k

sw_bitun:
	ldr	w9,[x19,a_r]			; s
	ldr	w10,[x19,a_r+4]			; d
	ldr	w11,[x19,a_r+8]			; info
	mov	w0,w11
	bl	gba_read16
	mov	w12,w0				; len
	add	w0,w11,2
	bl	gba_read8
	mov	w13,w0				; sw
	add	w0,w11,3
	bl	gba_read8
	mov	w14,w0				; dw
	add	w0,w11,4
	bl	gba_read32
	lsr	w25,w0,31			; zero
	and	w27,w0,0x7FFFFFFF		; off
	cbz	w13,swi_end
	cbz	w14,swi_end
	cmp	w14,32
	b.hi	swi_end
	mov	w11,0				; i
	mov	w15,0				; out
	mov	w28,0				; outBits
.i:	cmp	w11,w12
	b.hs	swi_end
	add	w0,w9,w11
	bl	gba_read8
	str	w0,[sp]				; byte
	str	wzr,[sp,4]			; b
.b:	ldr	w2,[sp,4]
	cmp	w2,8
	b.hs	.n
	ldr	w0,[sp]
	lsr	w0,w0,w2
	mov	w1,1
	lsl	w1,w1,w13
	sub	w1,w1,1
	and	w0,w0,w1			; v
	orr	w1,w0,w25
	cbz	w1,.z
	add	w0,w0,w27
.z:	lsl	w0,w0,w28
	orr	w15,w15,w0
	add	w28,w28,w14
	cmp	w28,32
	b.lt	.c
	mov	w0,w10
	mov	w1,w15
	bl	gba_write32
	add	w10,w10,4
	mov	w15,0
	mov	w28,0
.c:	ldr	w2,[sp,4]
	add	w2,w2,w13
	str	w2,[sp,4]
	b	.b
.n:	add	w11,w11,1
	b	.i

sw_lz77:
	ldr	w9,[x19,a_r]			; s
	ldr	w10,[x19,a_r+4]			; d
	mov	w0,w9
	bl	gba_read32
	add	w9,w9,4
	lsr	w12,w0,8			; size
	str	w12,[sp]
	mov	w11,0				; done
	mov	w13,0				; half
	cmp	w15,0x12
	cset	w25,eq				; vr
.o:	cmp	w11,w12
	b.hs	.f
	mov	w0,w9
	add	w9,w9,1
	bl	gba_read8
	mov	w14,w0				; flags
	mov	w15,0				; k
.k:	cmp	w15,8
	b.hs	.o
	cmp	w11,w12
	b.hs	.o
	tbz	w14,7,.lit
	mov	w0,w9
	bl	gba_read8
	mov	w27,w0
	add	w0,w9,1
	bl	gba_read8
	add	w9,w9,2
	orr	w0,w0,w27,lsl 8			; v
	lsr	w27,w0,12
	add	w27,w27,3			; len
	and	w0,w0,0xFFF
	add	w0,w0,1
	str	w0,[sp,4]			; disp
.j:	cbz	w27,.nk
	cmp	w11,w12
	b.hs	.nk
	sub	w27,w27,1
	add	w0,w10,w11
	ldr	w1,[sp,4]
	sub	w28,w0,w1			; from
	cbz	w25,.b8
	and	w0,w28,0xFFFFFFFE
	bl	gba_read16
	and	w1,w28,1
	lsl	w1,w1,3
	lsr	w0,w0,w1
	and	w0,w0,0xFF
	and	w1,w28,0xFFFFFFFE
	add	w2,w10,w11
	and	w2,w2,0xFFFFFFFE
	cmp	w1,w2
	b.ne	.p
	and	w0,w13,0xFF
	b	.p
.b8:	mov	w0,w28
	bl	gba_read8
.p:	bl	put_b
	b	.j
.lit:	mov	w0,w9
	add	w9,w9,1
	bl	gba_read8
	bl	put_b
.nk:	add	w15,w15,1
	lsl	w14,w14,1
	b	.k
.f:	add	w21,w21,w12
	b	swi_end

sw_huff:
	ldr	w9,[x19,a_r+4]			; d
	ldr	w10,[x19,a_r]			; s
	mov	w0,w10
	bl	gba_read32
	and	w1,w0,15
	str	w1,[sp]				; bits
	lsr	w11,w0,8			; size
	add	w10,w10,4			; tree
	str	w10,[sp,4]
	mov	w0,w10
	bl	gba_read8
	add	w0,w0,1
	add	w10,w10,w0,lsl 1		; data
	mov	w12,0				; out
	mov	w13,0				; outBits
	mov	w14,0				; done
	ldr	w15,[sp,4]
	add	w15,w15,1			; node
	mov	w0,w15
	bl	gba_read8
	mov	w25,w0				; nv
.o:	cmp	w14,w11
	b.hs	swi_end
	mov	w0,w10
	bl	gba_read32
	mov	w27,w0				; word
	add	w10,w10,4
	mov	w28,31				; b
.b:	tbnz	w28,31,.o
	cmp	w14,w11
	b.hs	.o
	lsr	w0,w27,w28
	and	w0,w0,1				; dir
	and	w1,w15,0xFFFFFFFE
	and	w2,w25,0x3F
	add	w1,w1,w2,lsl 1
	add	w1,w1,2
	add	w1,w1,w0			; next
	cbz	w0,.d0
	tst	w25,0x40
	b	.t
.d0:	tst	w25,0x80
.t:	b.eq	.nl
	mov	w0,w1
	bl	gba_read8
	ldr	w1,[sp]
	mov	w2,1
	lsl	w2,w2,w1
	sub	w2,w2,1
	and	w0,w0,w2
	lsl	w0,w0,w13
	orr	w12,w12,w0
	add	w13,w13,w1
	cmp	w13,32
	b.ne	.nw
	mov	w0,w9
	mov	w1,w12
	bl	gba_write32
	add	w9,w9,4
	add	w14,w14,4
	mov	w12,0
	mov	w13,0
.nw:	ldr	w15,[sp,4]
	add	w15,w15,1
	b	.rd
.nl:	mov	w15,w1
.rd:	mov	w0,w15
	bl	gba_read8
	mov	w25,w0
	sub	w28,w28,1
	b	.b

sw_rl:
	ldr	w9,[x19,a_r]			; s
	ldr	w10,[x19,a_r+4]			; d
	mov	w0,w9
	bl	gba_read32
	add	w9,w9,4
	lsr	w12,w0,8			; size
	mov	w11,0				; done
	mov	w13,0				; half
	cmp	w15,0x15
	cset	w25,eq
.o:	cmp	w11,w12
	b.hs	swi_end
	mov	w0,w9
	add	w9,w9,1
	bl	gba_read8
	mov	w14,w0				; f
	and	w27,w0,0x7F
	add	w27,w27,1			; len
	mov	w28,0
	tbz	w14,7,.j
	add	w27,w27,2
	mov	w0,w9
	add	w9,w9,1
	bl	gba_read8
	mov	w28,w0				; rep
.j:	cbz	w27,.o
	cmp	w11,w12
	b.hs	.o
	sub	w27,w27,1
	mov	w0,w28
	tbnz	w14,7,.p
	mov	w0,w9
	add	w9,w9,1
	bl	gba_read8
.p:	bl	put_b
	b	.j

sw_diff:
	ldr	w9,[x19,a_r]			; s
	ldr	w10,[x19,a_r+4]			; d
	mov	w0,w9
	bl	gba_read32
	add	w9,w9,4
	lsr	w12,w0,8			; size
	mov	w11,0				; i
	mov	w13,0				; acc
	mov	w14,0				; half
	cmp	w15,0x18
	b.ne	.b8
.w:	cmp	w11,w12
	b.hs	swi_end
	add	w0,w9,w11
	bl	gba_read16
	add	w13,w13,w0
	and	w13,w13,0xFFFF
	add	w0,w10,w11
	mov	w1,w13
	bl	gba_write16
	add	w11,w11,2
	b	.w
.b8:	cmp	w11,w12
	b.hs	swi_end
	add	w0,w9,w11
	bl	gba_read8
	add	w13,w13,w0
	and	w13,w13,0xFF
	add	w0,w10,w11
	cmp	w15,0x16
	b.ne	.v
	mov	w1,w13
	bl	gba_write8
	b	.n
.v:	tbnz	w11,0,.h
	mov	w14,w13
	b	.n
.h:	and	w0,w0,0xFFFFFFFE
	orr	w1,w14,w13,lsl 8
	bl	gba_write16
.n:	add	w11,w11,1
	b	.b8

sw_midi:
	ldr	w0,[x19,a_r]
	add	w0,w0,4
	bl	gba_read32
	mov	w9,w0				; base
	ldr	w0,[x19,a_r+4]
	and	w0,w0,0xFF
	mov	w1,180
	sub	w0,w1,w0
	lsl	w0,w0,8
	ldr	w1,[x19,a_r+8]
	and	w1,w1,0xFF
	sub	w0,w0,w1			; e
	mov	w1,3072
	udiv	w10,w0,w1			; oct
	msub	w0,w10,w1,w0			; frac
	bl	pow2neg
	mov	w1,0
	cmp	w10,32
	b.hs	.z
	lsr	w1,w9,w10
.z:	umull	x0,w1,w0
	lsr	x0,x0,16
	str	w0,[x19,a_r]
	b	swi_end
