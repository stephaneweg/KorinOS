; ============================================================================
;  gba_cpu.inc - l'ARM7TDMI : ARM (32 bits) et Thumb (16 bits), les registres
;  en banques, les interruptions. r [15] tient l'adresse de l'instruction
;  suivante entre deux codes ; pendant un code il vaut celui-ci + 8 (ARM) ou
;  + 4 (Thumb). Les drapeaux N Z C V de l'ARM sont ceux de l'A64.
; ============================================================================

FT_BIT		= 5

macro nz_of reg {			; N, Z
	cmp	reg,0
	cset	w16,eq
	lsr	w17,reg,31
	bfi	w22,w17,31,1
	bfi	w22,w16,30,1
}
macro nzcv_get {			; apres adds / subs / adcs / sbcs
	mrs	x16,nzcv
	lsr	w16,w16,28
	bfi	w22,w16,28,4
}
macro carry_in {			; C de l'ARM -> C de l'A64
	ubfx	w16,w22,29,1
	cmp	w16,1
}
macro set_cf reg {
	bfi	w22,reg,29,1
}

cond_tab:
	dh	0xF0F0,0x0F0F,0xCCCC,0x3333,0xFF00,0x00FF,0xAAAA,0x5555
	dh	0x0C0C,0xF3F3,0xAA55,0x55AA,0x0A05,0xF5FA,0xFFFF,0x0000

; w0 = mode -> w0 = banque
mode_bank:
	and	w0,w0,0x1F
	cmp	w0,0x11
	b.lo	.z
	cmp	w0,0x13
	b.hi	.a
	sub	w0,w0,0x10
	ret
.a:	cmp	w0,0x17
	b.ne	.b
	mov	w0,4
	ret
.b:	cmp	w0,0x1B
	b.ne	.z
	mov	w0,5
	ret
.z:	mov	w0,0
	ret

; w0 = m (perd x0-x7)
set_mode:
	mov	x7,x30
	mov	w5,w0				; m
	mov	w0,w22
	bl	mode_bank
	mov	w1,w0				; ob
	mov	w0,w5
	bl	mode_bank
	mov	w2,w0				; nb
	cmp	w1,w2
	b.eq	.s
	add	x3,x19,a_bankR13
	ldr	w0,[x19,a_r+13*4]
	str	w0,[x3,x1,lsl 2]
	ldr	w0,[x19,a_r+14*4]
	add	x3,x3,24
	str	w0,[x3,x1,lsl 2]
	ldr	w0,[x19,a_spsr]
	add	x3,x3,24
	str	w0,[x3,x1,lsl 2]
	cmp	w1,1
	b.ne	.n1
	mov	x3,0
.f1:	add	x4,x19,a_r+8*4
	ldr	w0,[x4,x3,lsl 2]
	add	x4,x19,a_bankFiq
	str	w0,[x4,x3,lsl 2]
	add	x4,x19,a_bankUsr
	ldr	w0,[x4,x3,lsl 2]
	add	x4,x19,a_r+8*4
	str	w0,[x4,x3,lsl 2]
	add	x3,x3,1
	cmp	x3,5
	b.lo	.f1
.n1:	cmp	w2,1
	b.ne	.n2
	mov	x3,0
.f2:	add	x4,x19,a_r+8*4
	ldr	w0,[x4,x3,lsl 2]
	add	x4,x19,a_bankUsr
	str	w0,[x4,x3,lsl 2]
	add	x4,x19,a_bankFiq
	ldr	w0,[x4,x3,lsl 2]
	add	x4,x19,a_r+8*4
	str	w0,[x4,x3,lsl 2]
	add	x3,x3,1
	cmp	x3,5
	b.lo	.f2
.n2:	add	x3,x19,a_bankR13
	ldr	w0,[x3,x2,lsl 2]
	str	w0,[x19,a_r+13*4]
	add	x3,x3,24
	ldr	w0,[x3,x2,lsl 2]
	str	w0,[x19,a_r+14*4]
	add	x3,x3,24
	ldr	w0,[x3,x2,lsl 2]
	str	w0,[x19,a_spsr]
.s:	bfxil	w22,w5,0,5
	br	x7

; w0 = v (garde x9-x15, perd x0-x8)
set_cpsr:
	mov	x6,x30
	mov	w8,w0
	orr	w0,w8,0x10
	stp	x6,x8,[sp,-16]!
	bl	set_mode
	ldp	x6,x8,[sp],16
	eor	w1,w22,w8
	tbz	w1,FT_BIT,.a
	strb	wzr,[x19,a_pipeOk]
.a:	orr	w22,w8,0x10
	br	x6

set_pc:					; w0 = v
	and	w1,w0,0xFFFFFFFC
	tbz	w22,FT_BIT,.a
	and	w1,w0,0xFFFFFFFE
.a:	str	w1,[x19,a_r+15*4]
	mov	w26,1
	strb	wzr,[x19,a_pipeOk]
	ret

cpu_irq:
	str	x30,[sp,-16]!
	mov	w8,w22				; old
	mov	w0,0x12
	stp	x8,xzr,[sp,-16]!
	bl	set_mode
	ldp	x8,x1,[sp],16
	str	w8,[x19,a_spsr]
	orr	w22,w22,0x80
	bic	w22,w22,0x20
	ldr	w0,[x19,a_r+15*4]
	add	w0,w0,4
	str	w0,[x19,a_r+14*4]
	mov	w0,0x18
	str	w0,[x19,a_r+15*4]
	strb	wzr,[x19,a_pipeOk]
	strb	wzr,[x19,a_halted]
	ldr	x30,[sp],16
	ret

; ---- le decaleur : w1 = c entrant -> w0 = v, w1 = c ----

shift_imm:
	and	w2,w23,15
	ldr	w0,[x19,x2,lsl 2]		; v
	ubfx	w2,w23,7,5			; amt
	ubfx	w3,w23,5,2
	cbz	w3,.lsl
	cmp	w3,2
	b.lo	.lsr
	b.eq	.asr
	cbz	w2,.rrx
	sub	w3,w2,1
	lsr	w1,w0,w3
	and	w1,w1,1
	ror	w0,w0,w2
	ret
.rrx:	ubfx	w2,w22,29,1
	and	w1,w0,1
	lsr	w0,w0,1
	orr	w0,w0,w2,lsl 31
	ret
.lsl:	cbz	w2,.r
	mov	w3,32
	sub	w3,w3,w2
	lsr	w1,w0,w3
	and	w1,w1,1
	lsl	w0,w0,w2
.r:	ret
.lsr:	cbz	w2,.lz
	sub	w3,w2,1
	lsr	w1,w0,w3
	and	w1,w1,1
	lsr	w0,w0,w2
	ret
.lz:	lsr	w1,w0,31
	mov	w0,0
	ret
.asr:	cbz	w2,.az
	sub	w3,w2,1
	asr	w1,w0,w3
	and	w1,w1,1
	asr	w0,w0,w2
	ret
.az:	lsr	w1,w0,31
	asr	w0,w0,31
	ret

shift_reg:
	and	w2,w23,15
	ldr	w0,[x19,x2,lsl 2]
	cmp	w2,15
	b.ne	.a
	add	w0,w0,4
.a:	ubfx	w2,w23,8,4
	ldr	w2,[x19,x2,lsl 2]
	and	w2,w2,0xFF			; amt
	cbz	w2,.r
	ubfx	w3,w23,5,2
	cbz	w3,.lsl
	cmp	w3,2
	b.lo	.lsr
	b.eq	.asr
	ands	w2,w2,31
	b.ne	.ro
	lsr	w1,w0,31
	ret
.ro:	sub	w3,w2,1
	lsr	w1,w0,w3
	and	w1,w1,1
	ror	w0,w0,w2
.r:	ret
.lsl:	cmp	w2,32
	b.hs	.l2
	mov	w3,32
	sub	w3,w3,w2
	lsr	w1,w0,w3
	and	w1,w1,1
	lsl	w0,w0,w2
	ret
.l2:	and	w1,w0,1
	csel	w1,w1,wzr,eq
	mov	w0,0
	ret
.lsr:	cmp	w2,32
	b.hs	.r2
	sub	w3,w2,1
	lsr	w1,w0,w3
	and	w1,w1,1
	lsr	w0,w0,w2
	ret
.r2:	lsr	w1,w0,31
	csel	w1,w1,wzr,eq
	mov	w0,0
	ret
.asr:	cmp	w2,32
	b.hs	.a2
	sub	w3,w2,1
	asr	w1,w0,w3
	and	w1,w1,1
	asr	w0,w0,w2
	ret
.a2:	lsr	w1,w0,31
	asr	w0,w0,31
	ret

; ---- ARM ----

arm_step:
	ldr	w24,[x19,a_r+15*4]		; pc
	str	w24,[x19,a_fetchPC]
	ubfx	w25,w24,24,4			; region
	ldrb	w0,[x19,a_pipeOk]
	cbnz	w0,.ok
	mov	w0,w24
	bl	gba_read32
	str	w0,[x19,a_pipe]
	add	w0,w24,4
	bl	gba_read32
	str	w0,[x19,a_pipe+4]
	mov	w0,1
	strb	w0,[x19,a_pipeOk]
.ok:	ldp	w23,w1,[x19,a_pipe]		; op, pipe [1]
	str	w1,[x19,a_pipe]
	add	w0,w24,8
	bl	gba_read32
	str	w0,[x19,a_pipe+4]
	cbnz	w25,.nb
	str	w0,[x19,a_biosLatch]
.nb:	add	x0,x19,a_waitS32
	ldr	w21,[x0,x25,lsl 2]
	ldrb	w0,[x19,a_prefetch]
	cbz	w0,.np
	sub	w0,w25,8
	cmp	w0,5
	b.hi	.np
	mov	w21,2
.np:	add	w0,w24,8
	str	w0,[x19,a_r+15*4]
	mov	w26,0
	lsr	w0,w23,28
	adr	x1,cond_tab
	ldrh	w1,[x1,x0,lsl 1]
	lsr	w0,w22,28
	lsr	w1,w1,w0
	tbz	w1,0,arm_end
	ubfx	w0,w23,25,3
	jtab	x0,.t
.t:	dw	arm_c0-.t,arm_c1-.t,arm_single-.t,arm_c3-.t,arm_block-.t,arm_b-.t,arm_end-.t,arm_c7-.t

arm_end:
	cbnz	w26,.b
	add	w0,w24,4
	str	w0,[x19,a_r+15*4]
	b	step_done
.b:	ldr	w0,[x19,a_r+15*4]
	ubfx	w0,w0,24,4
	add	x1,x19,a_waitN32
	ldr	w2,[x1,x0,lsl 2]
	add	w21,w21,w2
	add	x1,x19,a_waitS32
	ldr	w2,[x1,x0,lsl 2]
	add	w21,w21,w2
	b	step_done

arm_c0:
	tbz	w23,4,.psr
	tbz	w23,7,.bx
	movl	w0,0x0FC000F0
	and	w0,w23,w0
	cmp	w0,0x90
	b.eq	arm_mul
	movl	w0,0x0F8000F0
	and	w0,w23,w0
	movl	w1,0x00800090
	cmp	w0,w1
	b.eq	arm_mull
	movl	w0,0x0FB00FF0
	and	w0,w23,w0
	movl	w1,0x01000090
	cmp	w0,w1
	b.eq	arm_swp
	tst	w23,0x60
	b.ne	arm_half
	b	.psr
.bx:	and	w0,w23,0x0FFFFFF0
	movl	w1,0x012FFF10
	cmp	w0,w1
	b.ne	.psr
	and	w0,w23,15
	ldr	w0,[x19,x0,lsl 2]
	bfi	w22,w0,FT_BIT,1
	bl	set_pc
	b	arm_end
.psr:	movl	w0,0x0D900000
	and	w0,w23,w0
	mov	w1,0x01000000
	cmp	w0,w1
	b.eq	arm_psr
	b	arm_dp

arm_c1:
	movl	w0,0x0DB00000
	and	w0,w23,w0
	movl	w1,0x01200000
	cmp	w0,w1
	b.eq	arm_psr
	movl	w0,0x0D900000
	and	w0,w23,w0
	mov	w1,0x01000000
	cmp	w0,w1
	b.eq	arm_end
	b	arm_dp

arm_c3:
	tbnz	w23,4,arm_end
	b	arm_single

arm_b:
	sbfx	w0,w23,0,24
	lsl	w0,w0,2
	tbz	w23,24,.j
	add	w1,w24,4
	str	w1,[x19,a_r+14*4]
.j:	ldr	w1,[x19,a_r+15*4]
	add	w0,w0,w1
	bl	set_pc
	b	arm_end

arm_c7:
	tbz	w23,24,arm_end
	ubfx	w0,w23,16,8
	bl	gba_swi
	b	arm_end

arm_dp:
	ubfx	w1,w22,29,1			; c
	tbz	w23,25,.reg
	ubfx	w0,w23,8,4
	lsl	w0,w0,1				; rot
	and	w3,w23,0xFF
	ror	w3,w3,w0
	cbz	w0,.got
	lsr	w1,w3,31
	b	.got
.reg:	tbz	w23,4,.si
	bl	shift_reg
	mov	w3,w0
	add	w21,w21,1
	b	.got
.si:	bl	shift_imm
	mov	w3,w0
.got:	ubfx	w4,w23,16,4			; rn
	ldr	w2,[x19,x4,lsl 2]		; a
	cmp	w4,15
	b.ne	.o
	tbnz	w23,25,.o
	tbz	w23,4,.o
	add	w2,w2,4
.o:	ubfx	w4,w23,21,4			; opc
	mov	w6,2				; 2 : pas de V
	jtab	x4,.t
.t:	dw	.and-.t,.eor-.t,.sub-.t,.rsb-.t,.add-.t,.adc-.t,.sbc-.t,.rsc-.t
	dw	.and-.t,.eor-.t,.sub-.t,.add-.t,.orr-.t,.mov-.t,.bic-.t,.mvn-.t
.and:	and	w5,w2,w3
	b	.e
.eor:	eor	w5,w2,w3
	b	.e
.orr:	orr	w5,w2,w3
	b	.e
.mov:	mov	w5,w3
	b	.e
.bic:	bic	w5,w2,w3
	b	.e
.mvn:	mvn	w5,w3
	b	.e
.sub:	subs	w5,w2,w3
	b	.fl
.rsb:	subs	w5,w3,w2
	b	.fl
.add:	adds	w5,w2,w3
	b	.fl
.adc:	carry_in
	adcs	w5,w2,w3
	b	.fl
.sbc:	carry_in
	sbcs	w5,w2,w3
	b	.fl
.rsc:	carry_in
	sbcs	w5,w3,w2
.fl:	cset	w1,cs
	cset	w6,vs
.e:	ubfx	w8,w23,12,4			; rd
	sub	w7,w4,8
	cmp	w7,3				; test ?
	cset	w7,ls
	cmp	w8,15
	b.ne	.s
	tbz	w23,20,.ns
	mov	w25,w5
	mov	w0,w22
	bl	mode_bank
	cbnz	w7,.tp
	cbz	w0,.nsp
	ldr	w0,[x19,a_spsr]
	bl	set_cpsr
.nsp:	mov	w0,w25
	bl	set_pc
	b	arm_end
.tp:	mov	w1,w0
	mov	w0,w22
	cbz	w1,.tq
	ldr	w0,[x19,a_spsr]
.tq:	bl	set_cpsr
	b	arm_end
.s:	tbz	w23,20,.ns
	nz_of	w5
	set_cf	w1
	cmp	w6,2
	b.eq	.ns
	bfi	w22,w6,28,1
.ns:	cbnz	w7,arm_end
	cmp	w8,15
	b.eq	.pc
	str	w5,[x19,x8,lsl 2]
	b	arm_end
.pc:	mov	w0,w5
	bl	set_pc
	b	arm_end

; w0 = m -> w21 += le temps de la multiplication
macro mul_time reg {
	mov	w0,1
	lsr	w1,reg,8
	movl	w2,0xFFFFFF
	cbz	w1,@f
	cmp	w1,w2
	b.eq	@f
	mov	w0,2
	lsr	w1,reg,16
	cbz	w1,@f
	mov	w2,0xFFFF
	cmp	w1,w2
	b.eq	@f
	mov	w0,3
	lsr	w1,reg,24
	cbz	w1,@f
	cmp	w1,0xFF
	b.eq	@f
	mov	w0,4
@@:	add	w21,w21,w0
}

arm_mul:
	and	w0,w23,15
	ldr	w0,[x19,x0,lsl 2]
	ubfx	w1,w23,8,4
	ldr	w3,[x19,x1,lsl 2]		; m
	mul	w4,w0,w3			; res
	tbz	w23,21,.a
	ubfx	w0,w23,12,4
	ldr	w0,[x19,x0,lsl 2]
	add	w4,w4,w0
	add	w21,w21,1
.a:	mul_time w3
	ubfx	w0,w23,16,4
	str	w4,[x19,x0,lsl 2]
	tbz	w23,20,arm_end
	nz_of	w4
	b	arm_end

arm_mull:
	ubfx	w5,w23,16,4			; hi
	ubfx	w6,w23,12,4			; lo
	and	w0,w23,15
	ldr	w0,[x19,x0,lsl 2]
	ubfx	w1,w23,8,4
	ldr	w1,[x19,x1,lsl 2]		; (rs)
	tbz	w23,22,.u
	smull	x4,w0,w1
	b	.a
.u:	umull	x4,w0,w1
.a:	tbz	w23,21,.b
	ldr	w2,[x19,x5,lsl 2]
	ldr	w3,[x19,x6,lsl 2]
	orr	x2,x3,x2,lsl 32
	add	x4,x4,x2
	add	w21,w21,1
.b:	lsr	w1,w1,16
	mov	w0,3
	mov	w2,4
	cmp	w1,0
	csel	w0,w2,w0,ne
	add	w21,w21,w0
	str	w4,[x19,x6,lsl 2]
	lsr	x0,x4,32
	str	w0,[x19,x5,lsl 2]
	tbz	w23,20,arm_end
	lsr	x0,x4,63
	bfi	w22,w0,31,1
	cmp	x4,0
	cset	w0,eq
	bfi	w22,w0,30,1
	b	arm_end

arm_swp:
	ubfx	w0,w23,16,4
	ldr	w25,[x19,x0,lsl 2]		; a
	and	w27,w23,15
	ldr	w27,[x19,x27,lsl 2]		; r [rm]
	mov	w0,w25
	tbz	w23,22,.w
	bl	gba_read8
	mov	w28,w0
	mov	w0,w25
	mov	w1,w27
	bl	gba_write8
	b	.d
.w:	bl	gba_read32
	and	w1,w25,3
	lsl	w1,w1,3
	ror	w28,w0,w1
	mov	w0,w25
	mov	w1,w27
	bl	gba_write32
.d:	ubfx	w0,w23,12,4
	str	w28,[x19,x0,lsl 2]
	ubfx	w0,w25,24,4
	add	x1,x19,a_waitN32
	ldr	w0,[x1,x0,lsl 2]
	lsl	w0,w0,1
	add	w0,w0,1
	add	w21,w21,w0
	b	arm_end

macro wait_of tab, reg {		; w21 += tab [reg >> 24 & 15], perd w16, x17
	ubfx	w16,reg,24,4
	add	x17,x19,tab
	ldr	w16,[x17,x16,lsl 2]
	add	w21,w21,w16
}

; w0 = a -> w0 : ce que LDRSH lit
ld_sh:	tbz	w0,0,.h
	str	x30,[sp,-16]!
	bl	gba_read8
	ldr	x30,[sp],16
	sxtb	w0,w0
	ret
.h:	str	x30,[sp,-16]!
	bl	gba_read16
	ldr	x30,[sp],16
	sxth	w0,w0
	ret

arm_half:
	ubfx	w9,w23,16,4			; rn
	ubfx	w10,w23,12,4			; rd
	ubfx	w11,w23,5,2			; sh
	tbz	w23,22,.ro
	ubfx	w0,w23,8,4
	lsl	w0,w0,4
	and	w1,w23,15
	orr	w0,w0,w1			; off
	b	.b
.ro:	and	w0,w23,15
	ldr	w0,[x19,x0,lsl 2]
.b:	ldr	w12,[x19,x9,lsl 2]		; base
	add	w25,w12,w0			; eff
	tbnz	w23,23,.u
	sub	w25,w12,w0
.u:	mov	w27,w12				; at
	tbz	w23,24,.at
	mov	w27,w25
.at:	wait_of	a_waitN,w27
	tbz	w23,20,.st
	mov	w0,w27
	cmp	w11,1
	b.ne	.s2
	bl	gba_read16
	tbz	w27,0,.v
	ror	w0,w0,8
	b	.v
.s2:	cmp	w11,2
	b.ne	.s3
	bl	gba_read8
	sxtb	w0,w0
	b	.v
.s3:	bl	ld_sh
.v:	tbz	w23,24,.wb
	tbz	w23,21,.nw
.wb:	cmp	w9,w10
	b.eq	.nw
	str	w25,[x19,x9,lsl 2]
.nw:	add	w21,w21,1
	cmp	w10,15
	b.eq	.p
	str	w0,[x19,x10,lsl 2]
	b	arm_end
.p:	bl	set_pc
	b	arm_end
.st:	cmp	w11,1
	b.ne	.st2
	ldr	w1,[x19,x10,lsl 2]
	cmp	w10,15
	b.ne	.st1
	add	w1,w1,4
.st1:	mov	w0,w27
	bl	gba_write16
.st2:	tbz	w23,24,.sw
	tbz	w23,21,arm_end
.sw:	str	w25,[x19,x9,lsl 2]
	b	arm_end

arm_single:
	ubfx	w9,w23,16,4			; rn
	ubfx	w10,w23,12,4			; rd
	tbz	w23,25,.imm
	ubfx	w1,w22,29,1
	bl	shift_imm
	b	.b
.imm:	and	w0,w23,0xFFF
.b:	ldr	w12,[x19,x9,lsl 2]
	add	w25,w12,w0
	tbnz	w23,23,.u
	sub	w25,w12,w0
.u:	mov	w27,w12
	tbz	w23,24,.at
	mov	w27,w25
.at:	mov	w0,w27
	tbz	w23,20,.st
	tbz	w23,22,.lw
	bl	gba_read8
	wait_of	a_waitN,w27
	b	.v
.lw:	bl	gba_read32
	and	w1,w27,3
	lsl	w1,w1,3
	ror	w0,w0,w1
	wait_of	a_waitN32,w27
.v:	tbz	w23,24,.wb
	tbz	w23,21,.nw
.wb:	cmp	w9,w10
	b.eq	.nw
	str	w25,[x19,x9,lsl 2]
.nw:	add	w21,w21,1
	cmp	w10,15
	b.eq	.p
	str	w0,[x19,x10,lsl 2]
	b	arm_end
.p:	bl	set_pc
	b	arm_end
.st:	ldr	w1,[x19,x10,lsl 2]
	cmp	w10,15
	b.ne	.s1
	add	w1,w1,4
.s1:	tbz	w23,22,.sw
	bl	gba_write8
	wait_of	a_waitN,w27
	b	.w
.sw:	bl	gba_write32
	wait_of	a_waitN32,w27
.w:	tbz	w23,24,.wb2
	tbz	w23,21,arm_end
.wb2:	str	w25,[x19,x9,lsl 2]
	b	arm_end

; w1 = a, w2 = n : le temps d'un transfert de bloc
block_time:
	ubfx	w3,w1,24,4
	add	x4,x19,a_waitN32
	ldr	w4,[x4,x3,lsl 2]
	add	w21,w21,w4
	cmp	w2,1
	b.ls	.r
	add	x4,x19,a_waitS32
	ldr	w4,[x4,x3,lsl 2]
	sub	w5,w2,1
	mul	w4,w4,w5
	add	w21,w21,w4
.r:	ret

arm_block:
	ubfx	w9,w23,16,4			; rn
	and	w10,w23,0xFFFF			; list
	mov	w11,0				; n
	mov	w0,w10
.c:	cbz	w0,.cf
	add	w11,w11,1
	sub	w1,w0,1
	and	w0,w0,w1
	b	.c
.cf:	ldr	w12,[x19,x9,lsl 2]		; base
	mov	w13,0x40			; bytes
	cbz	w10,.e
	lsl	w13,w11,2
.e:	cbnz	w10,.ne
	mov	w10,0x8000
.ne:	tbz	w23,23,.dn
	mov	w25,w12				; start
	tbz	w23,24,.u
	add	w25,w25,4
.u:	add	w27,w12,w13			; newBase
	b	.sb
.dn:	sub	w25,w12,w13
	tbnz	w23,24,.d2
	add	w25,w25,4
.d2:	sub	w27,w12,w13
.sb:	mov	w28,0				; userBank
	tbz	w23,22,.m
	tbz	w23,20,.ub
	tbnz	w10,15,.m
.ub:	mov	w28,1
.m:	and	w14,w22,0x1F			; oldMode
	cbz	w28,.nm
	mov	w0,0x1F
	bl	set_mode
.nm:	mov	w1,w25
	mov	w2,w11
	bl	block_time
	mov	w15,0				; i
	tbz	w23,20,.st
	tbz	w23,21,.l1
	lsr	w0,w10,w9
	tbnz	w0,0,.l1
	str	w27,[x19,x9,lsl 2]
.l1:	lsr	w0,w10,w15
	tbz	w0,0,.l2
	mov	w0,w25
	bl	gba_read32
	add	w25,w25,4
	cmp	w15,15
	b.eq	.lp
	str	w0,[x19,x15,lsl 2]
	b	.l2
.lp:	mov	w27,w0
	tbz	w23,22,.lq
	mov	w0,w22
	bl	mode_bank
	cbz	w0,.lq
	ldr	w0,[x19,a_spsr]
	bl	set_cpsr
.lq:	mov	w0,w27
	bl	set_pc
.l2:	add	w15,w15,1
	cmp	w15,16
	b.lo	.l1
	add	w21,w21,1
	b	.end
.st:	mov	w11,1				; first
.s1:	lsr	w0,w10,w15
	tbz	w0,0,.s2
	ldr	w1,[x19,x15,lsl 2]
	cmp	w15,15
	b.ne	.s3
	add	w1,w1,4
.s3:	mov	w0,w25
	bl	gba_write32
	add	w25,w25,4
	cbz	w11,.s4
	tbz	w23,21,.s4
	str	w27,[x19,x9,lsl 2]
.s4:	mov	w11,0
.s2:	add	w15,w15,1
	cmp	w15,16
	b.lo	.s1
.end:	cbz	w28,arm_end
	mov	w0,w14
	bl	set_mode
	b	arm_end

arm_psr:
	mov	w0,w22
	bl	mode_bank
	mov	w9,w0				; banque
	ubfx	w10,w23,22,1			; toSpsr
	tbnz	w23,21,.msr
	mov	w1,w22
	cbz	w10,.m
	cbz	w9,.m
	ldr	w1,[x19,a_spsr]
.m:	ubfx	w0,w23,12,4
	str	w1,[x19,x0,lsl 2]
	b	arm_end
.msr:	tbz	w23,25,.r
	and	w0,w23,0xFF
	ubfx	w1,w23,8,4
	lsl	w1,w1,1
	ror	w0,w0,w1
	b	.v
.r:	and	w0,w23,15
	ldr	w0,[x19,x0,lsl 2]
.v:	mov	w1,0				; mask
	tbz	w23,19,.a
	orr	w1,w1,0xFF000000
.a:	tbz	w23,18,.b
	orr	w1,w1,0x00FF0000
.b:	tbz	w23,17,.c
	orr	w1,w1,0x0000FF00
.c:	tbz	w23,16,.d
	orr	w1,w1,0x000000FF
.d:	cbz	w10,.cp
	cbz	w9,arm_end
	ldr	w2,[x19,a_spsr]
	bic	w2,w2,w1
	and	w0,w0,w1
	orr	w2,w2,w0
	str	w2,[x19,a_spsr]
	b	arm_end
.cp:	and	w2,w22,0x1F
	cmp	w2,0x10
	b.ne	.nu
	and	w1,w1,0xFF000000
.nu:	bic	w2,w22,w1
	and	w0,w0,w1
	orr	w0,w2,w0
	lsr	w1,w22,FT_BIT
	bfi	w0,w1,FT_BIT,1
	bl	set_cpsr
	b	arm_end

; ---- Thumb ----

thumb_step:
	ldr	w24,[x19,a_r+15*4]		; pc
	str	w24,[x19,a_fetchPC]
	ubfx	w25,w24,24,4			; region
	ldrb	w0,[x19,a_pipeOk]
	cbnz	w0,.ok
	mov	w0,w24
	bl	gba_read16
	str	w0,[x19,a_pipe]
	add	w0,w24,2
	bl	gba_read16
	str	w0,[x19,a_pipe+4]
	mov	w0,1
	strb	w0,[x19,a_pipeOk]
.ok:	ldp	w23,w1,[x19,a_pipe]
	str	w1,[x19,a_pipe]
	add	w0,w24,4
	bl	gba_read16
	str	w0,[x19,a_pipe+4]
	add	x0,x19,a_waitS
	ldr	w21,[x0,x25,lsl 2]
	ldrb	w0,[x19,a_prefetch]
	cbz	w0,.np
	sub	w0,w25,8
	cmp	w0,5
	b.hi	.np
	mov	w21,1
.np:	add	w0,w24,4
	str	w0,[x19,a_r+15*4]
	mov	w26,0
	lsr	w0,w23,13
	jtab	x0,.t
.t:	dw	th_0-.t,th_1-.t,th_2-.t,th_3-.t,th_4-.t,th_5-.t,th_6-.t,th_7-.t

th_end:
	cbnz	w26,.b
	add	w0,w24,2
	str	w0,[x19,a_r+15*4]
	b	step_done
.b:	ldr	w0,[x19,a_r+15*4]
	ubfx	w0,w0,24,4
	add	x1,x19,a_waitN
	ldr	w2,[x1,x0,lsl 2]
	add	w21,w21,w2
	add	x1,x19,a_waitS
	ldr	w2,[x1,x0,lsl 2]
	add	w21,w21,w2
	b	step_done

th_0:
	and	w4,w23,7			; rd
	ubfx	w5,w23,3,3
	ldr	w5,[x19,x5,lsl 2]		; r [rs] / v
	lsr	w0,w23,11
	cmp	w0,3
	b.ne	.sh
	ubfx	w1,w23,6,3
	tbnz	w23,10,.i
	ldr	w1,[x19,x1,lsl 2]
.i:	tbz	w23,9,.a
	subs	w3,w5,w1
	b	.f
.a:	adds	w3,w5,w1
.f:	nzcv_get
	str	w3,[x19,x4,lsl 2]
	b	th_end
.sh:	ubfx	w2,w23,6,5			; amt
	ubfx	w1,w22,29,1			; c
	cbz	w0,.lsl
	cmp	w0,1
	b.eq	.lsr
	cbz	w2,.az
	sub	w3,w2,1
	asr	w1,w5,w3
	and	w1,w1,1
	asr	w5,w5,w2
	b	.e
.az:	lsr	w1,w5,31
	asr	w5,w5,31
	b	.e
.lsl:	cbz	w2,.e
	mov	w3,32
	sub	w3,w3,w2
	lsr	w1,w5,w3
	and	w1,w1,1
	lsl	w5,w5,w2
	b	.e
.lsr:	cbz	w2,.lz
	sub	w3,w2,1
	lsr	w1,w5,w3
	and	w1,w1,1
	lsr	w5,w5,w2
	b	.e
.lz:	lsr	w1,w5,31
	mov	w5,0
.e:	str	w5,[x19,x4,lsl 2]
	nz_of	w5
	set_cf	w1
	b	th_end

th_1:
	ubfx	w4,w23,8,3			; rd
	and	w1,w23,0xFF			; imm
	ldr	w2,[x19,x4,lsl 2]		; a
	ubfx	w0,w23,11,2
	cbnz	w0,.a
	str	w1,[x19,x4,lsl 2]
	nz_of	w1
	b	th_end
.a:	cmp	w0,2
	b.eq	.add
	subs	w3,w2,w1
	nzcv_get
	cmp	w0,1
	b.eq	th_end
	str	w3,[x19,x4,lsl 2]
	b	th_end
.add:	adds	w3,w2,w1
	nzcv_get
	str	w3,[x19,x4,lsl 2]
	b	th_end

th_2:
	lsr	w0,w23,10
	cmp	w0,0x10
	b.eq	th_alu
	cmp	w0,0x11
	b.eq	th_hi
	lsr	w0,w23,11
	cmp	w0,9
	b.eq	th_ldrpc
	; charge / range, registre
	and	w9,w23,7			; rd
	ubfx	w0,w23,3,3
	ldr	w0,[x19,x0,lsl 2]
	ubfx	w1,w23,6,3
	ldr	w1,[x19,x1,lsl 2]
	add	w25,w0,w1			; a
	mov	w0,w25
	ubfx	w2,w23,9,3
	jtab	x2,.t
.t:	dw	.s0-.t,.s1-.t,.s2-.t,.l3-.t,.l4-.t,.l5-.t,.l6-.t,.l7-.t
.s0:	ldr	w1,[x19,x9,lsl 2]
	bl	gba_write32
	wait_of	a_waitN32,w25
	b	th_end
.s1:	ldr	w1,[x19,x9,lsl 2]
	bl	gba_write16
	wait_of	a_waitN,w25
	b	th_end
.s2:	ldr	w1,[x19,x9,lsl 2]
	bl	gba_write8
	wait_of	a_waitN,w25
	b	th_end
.l3:	bl	gba_read8
	sxtb	w0,w0
	b	.ln
.l4:	bl	gba_read32
	and	w1,w25,3
	lsl	w1,w1,3
	ror	w0,w0,w1
	str	w0,[x19,x9,lsl 2]
	wait_of	a_waitN32,w25
	add	w21,w21,1
	b	th_end
.l5:	bl	gba_read16
	tbz	w25,0,.ln
	ror	w0,w0,8
	b	.ln
.l6:	bl	gba_read8
	b	.ln
.l7:	bl	ld_sh
.ln:	str	w0,[x19,x9,lsl 2]
	wait_of	a_waitN,w25
	add	w21,w21,1
	b	th_end

th_alu:
	and	w4,w23,7			; rd
	ldr	w2,[x19,x4,lsl 2]		; a
	ubfx	w3,w23,3,3
	ldr	w3,[x19,x3,lsl 2]		; b
	ubfx	w0,w23,6,4
	jtab	x0,.t
.t:	dw	.and-.t,.eor-.t,.lsl-.t,.lsr-.t,.asr-.t,.adc-.t,.sbc-.t,.ror-.t
	dw	.tst-.t,.neg-.t,.cmp-.t,.cmn-.t,.orr-.t,.mul-.t,.bic-.t,.mvn-.t
.and:	and	w5,w2,w3
.st:	str	w5,[x19,x4,lsl 2]
	nz_of	w5
	b	th_end
.eor:	eor	w5,w2,w3
	b	.st
.orr:	orr	w5,w2,w3
	b	.st
.bic:	bic	w5,w2,w3
	b	.st
.mvn:	mvn	w5,w3
	b	.st
.mul:	mul	w5,w2,w3
	add	w21,w21,2
	b	.st
.tst:	and	w5,w2,w3
	nz_of	w5
	b	th_end
.cmp:	subs	w5,w2,w3
	nzcv_get
	b	th_end
.cmn:	adds	w5,w2,w3
	nzcv_get
	b	th_end
.neg:	subs	w5,wzr,w3
	nzcv_get
	str	w5,[x19,x4,lsl 2]
	b	th_end
.adc:	carry_in
	adcs	w5,w2,w3
	nzcv_get
	str	w5,[x19,x4,lsl 2]
	b	th_end
.sbc:	carry_in
	sbcs	w5,w2,w3
	nzcv_get
	str	w5,[x19,x4,lsl 2]
	b	th_end
.lsl:	and	w3,w3,0xFF
	ubfx	w1,w22,29,1
	cbz	w3,.sh
	cmp	w3,32
	b.hs	.l2
	mov	w5,32
	sub	w5,w5,w3
	lsr	w1,w2,w5
	and	w1,w1,1
	lsl	w2,w2,w3
	b	.sh
.l2:	and	w1,w2,1
	csel	w1,w1,wzr,eq
	mov	w2,0
	b	.sh
.lsr:	and	w3,w3,0xFF
	ubfx	w1,w22,29,1
	cbz	w3,.sh
	cmp	w3,32
	b.hs	.r2
	sub	w5,w3,1
	lsr	w1,w2,w5
	and	w1,w1,1
	lsr	w2,w2,w3
	b	.sh
.r2:	lsr	w1,w2,31
	csel	w1,w1,wzr,eq
	mov	w2,0
	b	.sh
.asr:	and	w3,w3,0xFF
	ubfx	w1,w22,29,1
	cbz	w3,.sh
	cmp	w3,32
	b.hs	.a2
	sub	w5,w3,1
	asr	w1,w2,w5
	and	w1,w1,1
	asr	w2,w2,w3
	b	.sh
.a2:	lsr	w1,w2,31
	asr	w2,w2,31
	b	.sh
.ror:	and	w3,w3,0xFF
	ubfx	w1,w22,29,1
	cbz	w3,.sh
	ands	w3,w3,31
	b.ne	.ro
	lsr	w1,w2,31
	b	.sh
.ro:	sub	w5,w3,1
	lsr	w1,w2,w5
	and	w1,w1,1
	ror	w2,w2,w3
.sh:	str	w2,[x19,x4,lsl 2]
	nz_of	w2
	set_cf	w1
	add	w21,w21,1
	b	th_end

th_hi:
	and	w4,w23,7
	ubfx	w0,w23,4,4
	and	w0,w0,8
	orr	w4,w4,w0			; rd
	ubfx	w5,w23,3,4
	ldr	w5,[x19,x5,lsl 2]		; b
	ubfx	w0,w23,8,2
	cbnz	w0,.c1
	cmp	w4,15
	b.ne	.a
	ldr	w0,[x19,a_r+15*4]
	add	w0,w0,w5
	bl	set_pc
	b	th_end
.a:	ldr	w0,[x19,x4,lsl 2]
	add	w0,w0,w5
	str	w0,[x19,x4,lsl 2]
	b	th_end
.c1:	cmp	w0,1
	b.ne	.c2
	ldr	w0,[x19,x4,lsl 2]
	subs	w0,w0,w5
	nzcv_get
	b	th_end
.c2:	cmp	w0,2
	b.ne	.c3
	cmp	w4,15
	b.ne	.m
	mov	w0,w5
	bl	set_pc
	b	th_end
.m:	str	w5,[x19,x4,lsl 2]
	b	th_end
.c3:	bfi	w22,w5,FT_BIT,1
	mov	w0,w5
	bl	set_pc
	b	th_end

th_ldrpc:
	ldr	w0,[x19,a_r+15*4]
	and	w0,w0,0xFFFFFFFC
	and	w1,w23,0xFF
	add	w25,w0,w1,lsl 2
	mov	w0,w25
	bl	gba_read32
	ubfx	w1,w23,8,3
	str	w0,[x19,x1,lsl 2]
	wait_of	a_waitN32,w25
	add	w21,w21,1
	b	th_end

th_3:
	and	w9,w23,7			; rd
	ubfx	w1,w23,6,5			; imm
	ubfx	w0,w23,3,3
	ldr	w0,[x19,x0,lsl 2]
	tbnz	w23,12,.by
	add	w25,w0,w1,lsl 2			; a
	mov	w0,w25
	tbz	w23,11,.sw
	bl	gba_read32
	and	w1,w25,3
	lsl	w1,w1,3
	ror	w0,w0,w1
	str	w0,[x19,x9,lsl 2]
	wait_of	a_waitN32,w25
	add	w21,w21,1
	b	th_end
.sw:	ldr	w1,[x19,x9,lsl 2]
	bl	gba_write32
	wait_of	a_waitN32,w25
	b	th_end
.by:	add	w25,w0,w1
	mov	w0,w25
	tbz	w23,11,.sb
	bl	gba_read8
	str	w0,[x19,x9,lsl 2]
	wait_of	a_waitN,w25
	add	w21,w21,1
	b	th_end
.sb:	ldr	w1,[x19,x9,lsl 2]
	bl	gba_write8
	wait_of	a_waitN,w25
	b	th_end

th_4:
	tbnz	w23,12,.sp
	and	w9,w23,7
	ubfx	w0,w23,3,3
	ldr	w0,[x19,x0,lsl 2]
	ubfx	w1,w23,6,5
	add	w25,w0,w1,lsl 1
	mov	w0,w25
	tbz	w23,11,.st
	bl	gba_read16
	tbz	w25,0,.a
	ror	w0,w0,8
.a:	str	w0,[x19,x9,lsl 2]
	wait_of	a_waitN,w25
	add	w21,w21,1
	b	th_end
.st:	ldr	w1,[x19,x9,lsl 2]
	bl	gba_write16
	wait_of	a_waitN,w25
	b	th_end
.sp:	ubfx	w9,w23,8,3
	ldr	w0,[x19,a_r+13*4]
	and	w1,w23,0xFF
	add	w25,w0,w1,lsl 2
	mov	w0,w25
	tbz	w23,11,.ss
	bl	gba_read32
	and	w1,w25,3
	lsl	w1,w1,3
	ror	w0,w0,w1
	str	w0,[x19,x9,lsl 2]
	wait_of	a_waitN32,w25
	add	w21,w21,1
	b	th_end
.ss:	ldr	w1,[x19,x9,lsl 2]
	bl	gba_write32
	wait_of	a_waitN32,w25
	b	th_end

th_5:
	tbnz	w23,12,.b
	ldr	w0,[x19,a_r+15*4]
	and	w0,w0,0xFFFFFFFC
	tbz	w23,11,.a
	ldr	w0,[x19,a_r+13*4]
.a:	and	w1,w23,0xFF
	add	w0,w0,w1,lsl 2
	ubfx	w1,w23,8,3
	str	w0,[x19,x1,lsl 2]
	b	th_end
.b:	ubfx	w0,w23,8,4
	cbnz	w0,.c
	and	w0,w23,0x7F
	lsl	w0,w0,2
	ldr	w1,[x19,a_r+13*4]
	tbz	w23,7,.p
	sub	w1,w1,w0
	b	.s
.p:	add	w1,w1,w0
.s:	str	w1,[x19,a_r+13*4]
	b	th_end
.c:	and	w0,w23,0x600
	cmp	w0,0x400
	b.ne	th_end
	and	w9,w23,0xFF			; list
	ubfx	w10,w23,8,1			; extra
	mov	w2,w10				; n
	mov	w0,w9
.n:	cbz	w0,.nf
	add	w2,w2,1
	sub	w1,w0,1
	and	w0,w0,w1
	b	.n
.nf:	mov	w11,0				; i
	tbz	w23,11,.push
	ldr	w25,[x19,a_r+13*4]		; a
	mov	w1,w25
	bl	block_time
	add	w21,w21,1
.l1:	lsr	w0,w9,w11
	tbz	w0,0,.l2
	mov	w0,w25
	bl	gba_read32
	str	w0,[x19,x11,lsl 2]
	add	w25,w25,4
.l2:	add	w11,w11,1
	cmp	w11,8
	b.lo	.l1
	cbz	w10,.l3
	mov	w0,w25
	bl	gba_read32
	bl	set_pc
	add	w25,w25,4
.l3:	str	w25,[x19,a_r+13*4]
	b	th_end
.push:	ldr	w25,[x19,a_r+13*4]
	sub	w25,w25,w2,lsl 2
	str	w25,[x19,a_r+13*4]
	mov	w1,w25
	bl	block_time
.s1:	lsr	w0,w9,w11
	tbz	w0,0,.s2
	mov	w0,w25
	ldr	w1,[x19,x11,lsl 2]
	bl	gba_write32
	add	w25,w25,4
.s2:	add	w11,w11,1
	cmp	w11,8
	b.lo	.s1
	cbz	w10,th_end
	mov	w0,w25
	ldr	w1,[x19,a_r+14*4]
	bl	gba_write32
	b	th_end

th_6:
	tbnz	w23,12,.bc
	ubfx	w9,w23,8,3			; rb
	and	w10,w23,0xFF			; list
	ldr	w25,[x19,x9,lsl 2]		; a
	cbnz	w10,.nz
	tbz	w23,11,.es
	mov	w0,w25
	bl	gba_read32
	bl	set_pc
	b	.e2
.es:	ldr	w1,[x19,a_r+15*4]
	add	w1,w1,2
	mov	w0,w25
	bl	gba_write32
.e2:	add	w0,w25,0x40
	str	w0,[x19,x9,lsl 2]
	b	th_end
.nz:	mov	w2,0
	mov	w0,w10
.n:	cbz	w0,.nf
	add	w2,w2,1
	sub	w1,w0,1
	and	w0,w0,w1
	b	.n
.nf:	add	w27,w25,w2,lsl 2		; end
	mov	w1,w25
	bl	block_time
	mov	w11,0
	tbz	w23,11,.st
.l1:	lsr	w0,w10,w11
	tbz	w0,0,.l2
	mov	w0,w25
	bl	gba_read32
	str	w0,[x19,x11,lsl 2]
	add	w25,w25,4
.l2:	add	w11,w11,1
	cmp	w11,8
	b.lo	.l1
	lsr	w0,w10,w9
	tbnz	w0,0,.l3
	str	w27,[x19,x9,lsl 2]
.l3:	add	w21,w21,1
	b	th_end
.st:	mov	w12,1				; first
.s1:	lsr	w0,w10,w11
	tbz	w0,0,.s2
	mov	w0,w25
	ldr	w1,[x19,x11,lsl 2]
	bl	gba_write32
	add	w25,w25,4
	cbz	w12,.s3
	str	w27,[x19,x9,lsl 2]
.s3:	mov	w12,0
.s2:	add	w11,w11,1
	cmp	w11,8
	b.lo	.s1
	b	th_end
.bc:	ubfx	w0,w23,8,4			; cond
	cmp	w0,15
	b.ne	.b1
	and	w0,w23,0xFF
	bl	gba_swi
	b	th_end
.b1:	cmp	w0,14
	b.eq	th_end
	adr	x1,cond_tab
	ldrh	w1,[x1,x0,lsl 1]
	lsr	w0,w22,28
	lsr	w1,w1,w0
	tbz	w1,0,th_end
	sxtb	w0,w23
	lsl	w0,w0,1
	ldr	w1,[x19,a_r+15*4]
	add	w0,w0,w1
	bl	set_pc
	b	th_end

th_7:
	tbnz	w23,12,.bl
	tbnz	w23,11,th_end
	sbfx	w0,w23,0,11
	lsl	w0,w0,1
	ldr	w1,[x19,a_r+15*4]
	add	w0,w0,w1
	bl	set_pc
	b	th_end
.bl:	tbnz	w23,11,.b2
	sbfx	w0,w23,0,11
	lsl	w0,w0,12
	ldr	w1,[x19,a_r+15*4]
	add	w0,w0,w1
	str	w0,[x19,a_r+14*4]
	b	th_end
.b2:	ldr	w0,[x19,a_r+14*4]
	and	w1,w23,0x7FF
	add	w0,w0,w1,lsl 1
	bl	set_pc
	add	w0,w24,2
	orr	w0,w0,1
	str	w0,[x19,a_r+14*4]
	b	th_end
