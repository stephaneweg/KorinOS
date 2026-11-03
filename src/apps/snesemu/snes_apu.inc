; ============================================================================
;  snes_apu.inc - l'unite de son : le SPC700 (tous ses codes, ses cycles, la
;  ROM de demarrage de 64 octets), ses trois minuteurs, et le S-DSP echantillon
;  par echantillon (32 kHz) : 8 voix BRR, interpolation gaussienne, ADSR / GAIN,
;  bruit, modulation de hauteur, echo (FIR a 8 prises). La sortie est
;  reechantillonnee au debit de l'hote. Le SPC700 court derriere le 5A22 et le
;  rattrape quand celui-ci touche aux ports, et toutes les 16 lignes (apu_sync).
;
;  Dans le SPC700 : w21 A, w22 X, w23 Y, w24 S, w25 PC, w26 P, x27 aram,
;  w28 cycles du code en cours. spc_rd et spc_wr gardent x9-x15.
; ============================================================================

FS_C		= 0
FS_Z		= 1
FS_I		= 2
FS_H		= 3
FS_B		= 4
FS_P		= 5
FS_V		= 6
FS_N		= 7

macro knxt {
	b	spc_next
}
macro k_dpa reg {			; reg |= la page directe, perd w1
	and	w1,w26,0x20
	orr	reg,reg,w1,lsl 3
}
macro clamp16 reg {
	mov	w16,32767
	cmp	reg,w16
	csel	reg,w16,reg,gt
	mov	w17,-32768
	cmp	reg,w17
	csel	reg,w17,reg,lt
}

; -> x0 = cycles du SPC700 a l'instant x20
macro spc_target {
	movl	x0,1024000
	mul	x0,x20,x0
	movl	x1,21477272
	ldrb	w2,[x19,s_pal]
	cbz	w2,@f
	movl	x1,21281370
@@:	udiv	x0,x0,x1
}

apu_reset:
	add	x0,x19,S_ARAM
	mov	w1,0x10000
.a:	strb	wzr,[x0],1
	subs	w1,w1,1
	b.ne	.a
	str	wzr,[x19,s_spcA]
	str	wzr,[x19,s_spcX]
	str	wzr,[x19,s_spcY]
	mov	w0,0xEF
	str	w0,[x19,s_spcS]
	str	wzr,[x19,s_spcP]
	mov	w0,0xFFC0
	str	w0,[x19,s_spcPC]
	add	x0,x19,s_cpuToApu
	strb	wzr,[x0]
	strb	wzr,[x0,1]
	strb	wzr,[x0,2]
	strb	wzr,[x0,3]
	strb	wzr,[x0,4]
	strb	wzr,[x0,5]
	strb	wzr,[x0,6]
	strb	wzr,[x0,7]
	mov	w0,0x80
	strb	w0,[x19,s_spcCtrl]
	strb	wzr,[x19,s_dspAddr]
	strb	wzr,[x19,s_spcF8]
	strb	wzr,[x19,s_spcF9]
	mov	x0,0
.t:	add	x1,x19,s_spcTimerTarget
	strb	wzr,[x1,x0]
	add	x1,x19,s_spcTimerOut
	strb	wzr,[x1,x0]
	add	x1,x19,s_spcTimerCnt
	strb	wzr,[x1,x0]
	add	x1,x19,s_spcTimerDiv
	strh	wzr,[x1,x0,lsl 1]
	add	x0,x0,1
	cmp	x0,3
	b.lo	.t
	strb	wzr,[x19,s_spcSleep]
	spc_target
	str	x0,[x19,s_spcCycles]
	mov	w0,32
	str	w0,[x19,s_dspClock]
	add	x0,x19,S_DSP
	mov	w1,128
.d:	strb	wzr,[x0],1
	subs	w1,w1,1
	b.ne	.d
	mov	w0,0xE0
	strb	w0,[x19,S_DSP+0x6C]
	add	x0,x19,S_VOICE
	mov	w1,8
.v:	str	wzr,[x0,V_bufPos]
	add	x2,x0,V_buf
	mov	w3,24
.vb:	strh	wzr,[x2],2
	subs	w3,w3,1
	b.ne	.vb
	str	wzr,[x0,V_interpPos]
	str	wzr,[x0,V_brrAddr]
	mov	w2,1
	str	w2,[x0,V_brrOffset]
	str	wzr,[x0,V_konDelay]
	mov	w2,3
	str	w2,[x0,V_envMode]
	str	wzr,[x0,V_env]
	str	wzr,[x0,V_hiddenEnv]
	str	wzr,[x0,V_out]
	add	x0,x0,V_SIZE
	subs	w1,w1,1
	b.ne	.v
	strb	wzr,[x19,s_konPending]
	strb	wzr,[x19,s_endx]
	str	wzr,[x19,s_dspCounter]
	mov	w0,0x4000
	strh	w0,[x19,s_noise]
	str	wzr,[x19,s_echoOffset]
	str	wzr,[x19,s_echoLength]
	str	wzr,[x19,s_echoHistPos]
	strb	wzr,[x19,s_everyOther]
	add	x0,x19,s_echoHist
	mov	w1,16
.h:	strh	wzr,[x0],2
	subs	w1,w1,1
	b.ne	.h
	str	wzr,[x19,s_resAcc]
	strh	wzr,[x19,s_lastL]
	strh	wzr,[x19,s_lastR]
	str	wzr,[x19,s_ahead]
	str	wzr,[x19,s_atail]
	ret

; le son jusqu'a maintenant ; garde tout sauf x0-x8, x16, x17
apu_sync:
	spc_target
	ldr	x1,[x19,s_spcCycles]
	cmp	x1,x0
	b.lo	.go
	ret
.go:	prologue
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	stp	x13,x14,[sp,-16]!
	stp	x15,x0,[sp,-16]!		; la cible
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	ldr	w21,[x19,s_spcA]
	ldr	w22,[x19,s_spcX]
	ldr	w23,[x19,s_spcY]
	ldr	w24,[x19,s_spcS]
	ldr	w25,[x19,s_spcPC]
	ldr	w26,[x19,s_spcP]
	add	x27,x19,S_ARAM
spc_loop:
	ldr	x0,[sp,72]			; cible
	ldr	x1,[x19,s_spcCycles]
	cmp	x1,x0
	b.hs	spc_out
	ldrb	w2,[x19,s_spcSleep]
	cbz	w2,.run
	sub	x0,x0,x1
	mov	x2,64
	cmp	x0,x2
	csel	x28,x2,x0,hi
	b	spc_next
.run:
  if defined SNES_TRACE_SPC
	bl	spc_trace
  end if
	mov	w0,w25
	add	w25,w25,1
	and	w25,w25,0xFFFF
	bl	spc_rd
	mov	w13,w0				; op
	adr	x1,spc_cyc
	ldrb	w28,[x1,x0]
	jtab	x0,spc_tab
spc_next:
	ldr	x1,[x19,s_spcCycles]
	add	x1,x1,x28
	str	x1,[x19,s_spcCycles]
	; les minuteurs
	ldrb	w8,[x19,s_spcCtrl]
	mov	x0,0
.tm:	lsr	w1,w8,w0
	tbz	w1,0,.tn
	mov	w2,128
	cmp	x0,2
	b.ne	.tp
	mov	w2,16
.tp:	add	x3,x19,s_spcTimerDiv
	ldrh	w4,[x3,x0,lsl 1]
	add	w4,w4,w28
	and	w4,w4,0xFFFF
.tw:	cmp	w4,w2
	b.lo	.ts
	sub	w4,w4,w2
	add	x5,x19,s_spcTimerCnt
	ldrb	w6,[x5,x0]
	add	w6,w6,1
	and	w6,w6,0xFF
	add	x7,x19,s_spcTimerTarget
	ldrb	w7,[x7,x0]
	cmp	w6,w7
	b.ne	.tc
	mov	w6,0
	add	x7,x19,s_spcTimerOut
	ldrb	w1,[x7,x0]
	add	w1,w1,1
	and	w1,w1,15
	strb	w1,[x7,x0]
.tc:	strb	w6,[x5,x0]
	b	.tw
.ts:	strh	w4,[x3,x0,lsl 1]
.tn:	add	x0,x0,1
	cmp	x0,3
	b.lo	.tm
	ldr	w0,[x19,s_dspClock]
	sub	w0,w0,w28
.ds:	cmp	w0,0
	b.gt	.df
	add	w0,w0,32
	str	w0,[x19,s_dspClock]
	bl	dsp_sample
	ldr	w0,[x19,s_dspClock]
	b	.ds
.df:	str	w0,[x19,s_dspClock]
	b	spc_loop
spc_out:
	str	w21,[x19,s_spcA]
	str	w22,[x19,s_spcX]
	str	w23,[x19,s_spcY]
	str	w24,[x19,s_spcS]
	str	w25,[x19,s_spcPC]
	str	w26,[x19,s_spcP]
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x15,x0,[sp],16
	ldp	x13,x14,[sp],16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	epilogue

; ---- le bus du SPC700 ----

spc_ipl:
	db	0xCD,0xEF,0xBD,0xE8,0x00,0xC6,0x1D,0xD0,0xFC,0x8F,0xAA,0xF4,0x8F,0xBB,0xF5,0x78
	db	0xCC,0xF4,0xD0,0xFB,0x2F,0x19,0xEB,0xF4,0xD0,0xFC,0x7E,0xF4,0xD0,0x0B,0xE4,0xF5
	db	0xCB,0xF4,0xD7,0x00,0xFC,0xD0,0xF3,0xAB,0x01,0x10,0xEF,0x7E,0xF4,0x10,0xEB,0xBA
	db	0xF6,0xDA,0x00,0xBA,0xF4,0xC4,0xF4,0xDD,0x5D,0xD0,0xDB,0x1F,0x00,0x00,0xC0,0xFF
spc_cyc:
	db	2,8,4,5,3,4,3,6,2,6,5,4,5,4,6,8,  2,8,4,5,4,5,5,6,5,5,6,5,2,2,4,6
	db	2,8,4,5,3,4,3,6,2,6,5,4,5,4,5,4,  2,8,4,5,4,5,5,6,5,5,6,5,2,2,3,8
	db	2,8,4,5,3,4,3,6,2,6,4,4,5,4,6,6,  2,8,4,5,4,5,5,6,5,5,4,5,2,2,4,3
	db	2,8,4,5,3,4,3,6,2,6,4,4,5,4,5,5,  2,8,4,5,4,5,5,6,5,5,5,5,2,2,3,6
	db	2,8,4,5,3,4,3,6,2,6,5,4,5,2,4,5,  2,8,4,5,4,5,5,6,5,5,5,5,2,2,12,5
	db	3,8,4,5,3,4,3,6,2,6,4,4,5,2,4,4,  2,8,4,5,4,5,5,6,5,5,5,5,2,2,3,4
	db	3,8,4,5,4,5,4,7,2,5,6,4,5,2,4,9,  2,8,4,5,5,6,6,7,4,5,5,5,2,2,6,3
	db	2,8,4,5,3,4,3,6,2,4,5,3,4,3,4,3,  2,8,4,5,4,5,5,6,3,4,5,4,2,2,4,3

; w0 = a -> w0
spc_rd:
	sub	w1,w0,0xF0
	cmp	w1,0x10
	b.lo	.io
	lsr	w1,w0,6
	cmp	w1,0x3FF
	b.eq	.hi
	ldrb	w0,[x27,x0]
	ret
.hi:	ldrb	w1,[x19,s_spcCtrl]
	tbz	w1,7,.ram
	and	w0,w0,0x3F
	adr	x1,spc_ipl
	ldrb	w0,[x1,x0]
	ret
.ram:	ldrb	w0,[x27,x0]
	ret
.io:	cmp	w1,0x2
	b.ne	.a
.none:	ldrb	w0,[x19,s_dspAddr]
	ret
.a:	cmp	w1,3
	b.ne	.b
	ldrb	w1,[x19,s_dspAddr]
	and	w1,w1,0x7F
	cmp	w1,0x7C
	b.eq	.ex
	add	x2,x19,S_DSP
	ldrb	w0,[x2,x1]
	ret
.ex:	ldrb	w0,[x19,s_endx]
	ret
.b:	cmp	w1,4
	b.lo	.z
	cmp	w1,8
	b.hs	.c
	sub	w1,w1,4
	add	x2,x19,s_cpuToApu
	ldrb	w0,[x2,x1]
	ret
.c:	cmp	w1,8
	b.ne	.d
	ldrb	w0,[x19,s_spcF8]
	ret
.d:	cmp	w1,9
	b.ne	.e
	ldrb	w0,[x19,s_spcF9]
	ret
.e:	cmp	w1,0xD
	b.lo	.z
	sub	w1,w1,0xD
	add	x2,x19,s_spcTimerOut
	ldrb	w0,[x2,x1]
	strb	wzr,[x2,x1]
	ret
.z:	mov	w0,0
	ret

; w0 = a, w1 = v
spc_wr:
	sub	w2,w0,0xF0
	cmp	w2,0x10
	b.lo	.io
	strb	w1,[x27,x0]
	ret
.io:	strb	w1,[x27,x0]
	cmp	w2,1
	b.ne	.n2
	; F1 : minuteurs, ports
	ldrb	w3,[x19,s_spcCtrl]
	mov	x4,0
.t:	lsr	w5,w3,w4
	tbnz	w5,0,.tn
	lsr	w5,w1,w4
	tbz	w5,0,.tn
	add	x5,x19,s_spcTimerCnt
	strb	wzr,[x5,x4]
	add	x5,x19,s_spcTimerOut
	strb	wzr,[x5,x4]
.tn:	add	x4,x4,1
	cmp	x4,3
	b.lo	.t
	add	x5,x19,s_cpuToApu
	tbz	w1,4,.p1
	strb	wzr,[x5]
	strb	wzr,[x5,1]
.p1:	tbz	w1,5,.p2
	strb	wzr,[x5,2]
	strb	wzr,[x5,3]
.p2:	strb	w1,[x19,s_spcCtrl]
	ret
.n2:	cmp	w2,2
	b.ne	.n3
	strb	w1,[x19,s_dspAddr]
	ret
.n3:	cmp	w2,3
	b.ne	.n4
	ldrb	w3,[x19,s_dspAddr]
	tbnz	w3,7,.r
	add	x4,x19,S_DSP
	strb	w1,[x4,x3]
	cmp	w3,0x4C
	b.ne	.n31
	strb	w1,[x19,s_konPending]
	ret
.n31:	cmp	w3,0x7C
	b.ne	.r
	strb	wzr,[x19,s_endx]
.r:	ret
.n4:	cmp	w2,4
	b.lo	.r
	cmp	w2,8
	b.hs	.n5
	sub	w2,w2,4
	add	x3,x19,s_apuToCpu
	strb	w1,[x3,x2]
	ret
.n5:	cmp	w2,8
	b.ne	.n6
	strb	w1,[x19,s_spcF8]
	ret
.n6:	cmp	w2,9
	b.ne	.n7
	strb	w1,[x19,s_spcF9]
	ret
.n7:	cmp	w2,0xA
	b.lo	.r
	cmp	w2,0xD
	b.hs	.r
	sub	w2,w2,0xA
	add	x3,x19,s_spcTimerTarget
	strb	w1,[x3,x2]
	ret

k_imm:					; -> w0
	mov	w0,w25
	add	w25,w25,1
	and	w25,w25,0xFFFF
	b	spc_rd

k_imm16:
	str	x30,[sp,-16]!
	bl	k_imm
	str	w0,[sp,8]
	bl	k_imm
	ldr	w1,[sp,8]
	orr	w0,w1,w0,lsl 8
	ldr	x30,[sp],16
	ret

k_rd16dp:				; w0 = o -> w0
	str	x30,[sp,-16]!
	str	w0,[sp,8]
	k_dpa	w0
	bl	spc_rd
	ldr	w1,[sp,8]
	str	w0,[sp,8]
	add	w0,w1,1
	and	w0,w0,0xFF
	k_dpa	w0
	bl	spc_rd
	ldr	w1,[sp,8]
	orr	w0,w1,w0,lsl 8
	ldr	x30,[sp],16
	ret

k_rd_dp:				; w0 = o -> w0
	k_dpa	w0
	b	spc_rd

k_push:					; w0 = v
	mov	w1,w0
	orr	w0,w24,0x100
	sub	w24,w24,1
	and	w24,w24,0xFF
	b	spc_wr

k_pop:
	add	w24,w24,1
	and	w24,w24,0xFF
	orr	w0,w24,0x100
	b	spc_rd

k_push_pc:
	str	x30,[sp,-16]!
	ubfx	w0,w25,8,8
	bl	k_push
	and	w0,w25,0xFF
	bl	k_push
	ldr	x30,[sp],16
	ret

k_adc:					; w0 = a, w1 = b -> w0
	and	w2,w26,1
	add	w2,w2,w0
	add	w2,w2,w1			; r
	cmp	w2,0xFF
	cset	w4,hi
	bfxil	w26,w4,0,1
	eor	w4,w0,w1
	eor	w4,w4,w2
	ubfx	w4,w4,4,1
	bfi	w26,w4,FS_H,1
	eor	w4,w0,w1
	mvn	w4,w4
	eor	w5,w0,w2
	and	w4,w4,w5
	ubfx	w4,w4,7,1
	bfi	w26,w4,FS_V,1
	set_zn8	w2
	and	w0,w2,0xFF
	ret

k_cmp:					; w0 = a, w1 = b -> w0 = a
	cmp	w0,w1
	cset	w2,hs
	set_c	w2
	sub	w2,w0,w1
	set_zn8	w2
	ret

; w12 = k, w0 = a, w1 = b -> w0
k_alu:
	cbnz	w12,.a
	orr	w0,w0,w1
	set_zn8	w0
	ret
.a:	cmp	w12,1
	b.ne	.b
	and	w0,w0,w1
	set_zn8	w0
	ret
.b:	cmp	w12,2
	b.ne	.c
	eor	w0,w0,w1
	set_zn8	w0
	ret
.c:	cmp	w12,3
	b.eq	k_cmp
	cmp	w12,4
	b.eq	k_adc
	mvn	w1,w1
	and	w1,w1,0xFF
	b	k_adc

; w12 = k, w0 = v -> w0 ; 0 ASL 1 ROL 2 LSR 3 ROR 4 DEC 5 INC
k_shift:
	and	w2,w26,1
	cmp	w12,1
	b.hi	.r
	lsr	w1,w0,7
	set_c	w1
	lsl	w0,w0,1
	cbz	w12,.m
	orr	w0,w0,w2
	b	.m
.r:	cmp	w12,3
	b.hi	.i
	and	w1,w0,1
	set_c	w1
	lsr	w0,w0,1
	cmp	w12,2
	b.eq	.m
	orr	w0,w0,w2,lsl 7
	b	.m
.i:	cmp	w12,4
	b.ne	.p
	sub	w0,w0,1
	b	.m
.p:	add	w0,w0,1
.m:	and	w0,w0,0xFF
	set_zn8	w0
	ret

k_branch:				; w0 <> 0 : pris
	mov	w9,w0
	str	x30,[sp,-16]!
	bl	k_imm
	ldr	x30,[sp],16
	cbz	w9,.n
	sxtb	w0,w0
	add	w25,w25,w0
	and	w25,w25,0xFFFF
	add	w28,w28,2
.n:	ret

; ---- le bloc UAL ----

k_block:
	lsr	w9,w13,4			; hi
	and	w10,w13,15			; lo
	lsr	w12,w9,1			; k
	and	w9,w9,1				; odd
	cmp	w10,8
	b.ne	.n8
	cbnz	w9,.m8
	bl	k_imm				; A op #
	mov	w1,w0
	mov	w0,w21
	bl	k_alu
	mov	w21,w0
	knxt
.m8:	bl	k_imm				; d, #
	mov	w10,w0				; v
	bl	k_imm
	k_dpa	w0
	mov	w11,w0				; d
	b	.mem
.n8:	cmp	w10,9
	b.ne	.n9
	cbnz	w9,.m9
	bl	k_imm				; d, d
	bl	k_rd_dp
	mov	w10,w0
	bl	k_imm
	k_dpa	w0
	mov	w11,w0
	b	.mem
.m9:	mov	w0,w23				; (X), (Y)
	bl	k_rd_dp
	mov	w10,w0
	mov	w0,w22
	k_dpa	w0
	mov	w11,w0
.mem:	mov	w0,w11
	bl	spc_rd
	mov	w1,w10
	bl	k_alu
	cmp	w12,3
	b.eq	.f
	mov	w1,w0
	mov	w0,w11
	bl	spc_wr
.f:	knxt
.n9:	orr	w10,w10,w9,lsl 4
	cmp	w10,0x04
	b.ne	.a14
	bl	k_imm
	k_dpa	w0
	b	.ld
.a14:	cmp	w10,0x14
	b.ne	.a05
	bl	k_imm
	add	w0,w0,w22
	and	w0,w0,0xFF
	k_dpa	w0
	b	.ld
.a05:	cmp	w10,0x05
	b.ne	.a15
	bl	k_imm16
	b	.ld
.a15:	cmp	w10,0x15
	b.ne	.a06
	bl	k_imm16
	add	w0,w0,w22
	and	w0,w0,0xFFFF
	b	.ld
.a06:	cmp	w10,0x06
	b.ne	.a16
	mov	w0,w22
	k_dpa	w0
	b	.ld
.a16:	cmp	w10,0x16
	b.ne	.a07
	bl	k_imm16
	add	w0,w0,w23
	and	w0,w0,0xFFFF
	b	.ld
.a07:	cmp	w10,0x07
	b.ne	.a17
	bl	k_imm
	add	w0,w0,w22
	and	w0,w0,0xFF
	bl	k_rd16dp
	b	.ld
.a17:	bl	k_imm
	bl	k_rd16dp
	add	w0,w0,w23
	and	w0,w0,0xFFFF
.ld:	bl	spc_rd
	mov	w1,w0
	mov	w0,w21
	bl	k_alu
	mov	w21,w0
	knxt

; SET1 / CLR1
k_bit:	bl	k_imm
	k_dpa	w0
	mov	w10,w0
	bl	spc_rd
	lsr	w9,w13,4
	lsr	w1,w9,1
	mov	w2,1
	lsl	w1,w2,w1
	tbz	w9,0,.s
	bic	w1,w0,w1
	b	.w
.s:	orr	w1,w0,w1
.w:	mov	w0,w10
	bl	spc_wr
	knxt

; BBS / BBC
k_bbs:	bl	k_imm
	bl	k_rd_dp
	lsr	w9,w13,4
	lsr	w1,w9,1
	lsr	w0,w0,w1
	and	w0,w0,1
	and	w1,w9,1
	eor	w0,w0,w1
	bl	k_branch
	knxt

; TCALL
k_tcall:
	bl	k_push_pc
	lsr	w9,w13,4
	mov	w0,0xFFDE
	sub	w9,w0,w9,lsl 1
	mov	w0,w9
	bl	spc_rd
	mov	w10,w0
	add	w0,w9,1
	and	w0,w0,0xFFFF
	bl	spc_rd
	orr	w25,w10,w0,lsl 8
	knxt

k00:	knxt

macro kbr op, inv, bit {
k#op:	ubfx	w0,w26,bit,1
  if inv
	eor	w0,w0,1
  end if
	bl	k_branch
	knxt
}
kbr 10,1,FS_N
kbr 30,0,FS_N
kbr 50,1,FS_V
kbr 70,0,FS_V
kbr 90,1,FS_C
kbr B0,0,FS_C
kbr D0,1,FS_Z
kbr F0,0,FS_Z
k2F:	mov	w0,1
	bl	k_branch
	knxt
k20:	bic	w26,w26,0x20
	knxt
k40:	orr	w26,w26,0x20
	knxt
k60:	bic	w26,w26,1
	knxt
k80:	orr	w26,w26,1
	knxt
kA0:	orr	w26,w26,4
	knxt
kC0:	bic	w26,w26,4
	knxt
kE0:	bic	w26,w26,0x40
	bic	w26,w26,8
	knxt
kED:	eor	w26,w26,1
	knxt

; ---- MOV ----

; les adresses : -> w0
ka_dp:	str	x30,[sp,-16]!
	bl	k_imm
	k_dpa	w0
	ldr	x30,[sp],16
	ret
ka_dpx:	str	x30,[sp,-16]!
	bl	k_imm
	add	w0,w0,w22
	and	w0,w0,0xFF
	k_dpa	w0
	ldr	x30,[sp],16
	ret
ka_dpy:	str	x30,[sp,-16]!
	bl	k_imm
	add	w0,w0,w23
	and	w0,w0,0xFF
	k_dpa	w0
	ldr	x30,[sp],16
	ret
ka_absx: str	x30,[sp,-16]!
	bl	k_imm16
	add	w0,w0,w22
	and	w0,w0,0xFFFF
	ldr	x30,[sp],16
	ret
ka_absy: str	x30,[sp,-16]!
	bl	k_imm16
	add	w0,w0,w23
	and	w0,w0,0xFFFF
	ldr	x30,[sp],16
	ret
ka_idx:	str	x30,[sp,-16]!			; [d+X]
	bl	k_imm
	add	w0,w0,w22
	and	w0,w0,0xFF
	bl	k_rd16dp
	ldr	x30,[sp],16
	ret
ka_idy:	str	x30,[sp,-16]!			; [d]+Y
	bl	k_imm
	bl	k_rd16dp
	add	w0,w0,w23
	and	w0,w0,0xFFFF
	ldr	x30,[sp],16
	ret
ka_x:	mov	w0,w22
	k_dpa	w0
	ret

k_sta:	mov	w1,w21
	bl	spc_wr
	knxt
k_stx:	mov	w1,w22
	bl	spc_wr
	knxt
k_sty:	mov	w1,w23
	bl	spc_wr
	knxt
k_lda:	bl	spc_rd
k_seta:	mov	w21,w0
	set_zn8	w21
	knxt
k_ldx:	bl	spc_rd
k_setx:	mov	w22,w0
	set_zn8	w22
	knxt
k_ldy:	bl	spc_rd
k_sety:	mov	w23,w0
	set_zn8	w23
	knxt

kC4:	bl	ka_dp
	b	k_sta
kD4:	bl	ka_dpx
	b	k_sta
kC5:	bl	k_imm16
	b	k_sta
kD5:	bl	ka_absx
	b	k_sta
kC6:	bl	ka_x
	b	k_sta
kD6:	bl	ka_absy
	b	k_sta
kC7:	bl	ka_idx
	b	k_sta
kD7:	bl	ka_idy
	b	k_sta
kE4:	bl	ka_dp
	b	k_lda
kF4:	bl	ka_dpx
	b	k_lda
kE5:	bl	k_imm16
	b	k_lda
kF5:	bl	ka_absx
	b	k_lda
kE6:	bl	ka_x
	b	k_lda
kF6:	bl	ka_absy
	b	k_lda
kE7:	bl	ka_idx
	b	k_lda
kF7:	bl	ka_idy
	b	k_lda
kE8:	bl	k_imm
	b	k_seta
kCD:	bl	k_imm
	b	k_setx
k8D:	bl	k_imm
	b	k_sety
kF8:	bl	ka_dp
	b	k_ldx
kF9:	bl	ka_dpy
	b	k_ldx
kE9:	bl	k_imm16
	b	k_ldx
kEB:	bl	ka_dp
	b	k_ldy
kFB:	bl	ka_dpx
	b	k_ldy
kEC:	bl	k_imm16
	b	k_ldy
kD8:	bl	ka_dp
	b	k_stx
kD9:	bl	ka_dpy
	b	k_stx
kC9:	bl	k_imm16
	b	k_stx
kCB:	bl	ka_dp
	b	k_sty
kDB:	bl	ka_dpx
	b	k_sty
kCC:	bl	k_imm16
	b	k_sty
k5D:	mov	w22,w21
	set_zn8	w22
	knxt
k7D:	mov	w21,w22
	set_zn8	w21
	knxt
kFD:	mov	w23,w21
	set_zn8	w23
	knxt
kDD:	mov	w21,w23
	set_zn8	w21
	knxt
k9D:	mov	w22,w24
	set_zn8	w22
	knxt
kBD:	mov	w24,w22
	knxt
kAF:	bl	ka_x
	mov	w1,w21
	bl	spc_wr
	add	w22,w22,1
	and	w22,w22,0xFF
	knxt
kBF:	bl	ka_x
	bl	spc_rd
	mov	w21,w0
	add	w22,w22,1
	and	w22,w22,0xFF
	set_zn8	w21
	knxt
kFA:	bl	k_imm
	bl	k_rd_dp
	mov	w9,w0
	bl	ka_dp
	mov	w1,w9
	bl	spc_wr
	knxt
k8F:	bl	k_imm
	mov	w9,w0
	bl	ka_dp
	mov	w1,w9
	bl	spc_wr
	knxt

; ---- CMP X / Y ----

kC8:	bl	k_imm
	b	k_cpx
k3E:	bl	ka_dp
	b	k_cpxm
k1E:	bl	k_imm16
k_cpxm:	bl	spc_rd
k_cpx:	mov	w1,w0
	mov	w0,w22
	bl	k_cmp
	knxt
kAD:	bl	k_imm
	b	k_cpy
k7E:	bl	ka_dp
	b	k_cpym
k5E:	bl	k_imm16
k_cpym:	bl	spc_rd
k_cpy:	mov	w1,w0
	mov	w0,w23
	bl	k_cmp
	knxt

; ---- decalages, INC, DEC ----

k_rmw:	mov	w10,w0				; w0 = d, w13 = op
	bl	spc_rd
	lsr	w12,w13,5
	bl	k_shift
	mov	w1,w0
	mov	w0,w10
	bl	spc_wr
	knxt
k0B:
k2B:
k4B:
k6B:
k8B:
kAB:	bl	ka_dp
	b	k_rmw
k1B:
k3B:
k5B:
k7B:
k9B:
kBB:	bl	ka_dpx
	b	k_rmw
k0C:
k2C:
k4C:
k6C:
k8C:
kAC:	bl	k_imm16
	b	k_rmw
macro kshift op, reg, n {
k#op:	mov	w0,reg
	mov	w12,n
	bl	k_shift
	mov	reg,w0
	knxt
}
kshift 1C, w21, 0
kshift 3C, w21, 1
kshift 5C, w21, 2
kshift 7C, w21, 3
kshift 9C, w21, 4
kshift BC, w21, 5
kshift 1D, w22, 4
kshift 3D, w22, 5
kshift DC, w23, 4
kshift FC, w23, 5

; ---- 16 bits ----

k1A:	mov	w9,-1				; DECW
	b	k_incw
k3A:	mov	w9,1				; INCW
k_incw:	bl	k_imm
	mov	w10,w0
	bl	k_rd16dp
	add	w9,w0,w9
	and	w9,w9,0xFFFF
	mov	w0,w10
	k_dpa	w0
	and	w1,w9,0xFF
	bl	spc_wr
	add	w0,w10,1
	and	w0,w0,0xFF
	k_dpa	w0
	lsr	w1,w9,8
	bl	spc_wr
	set_zn16 w9
	knxt
k5A:	bl	k_imm				; CMPW
	bl	k_rd16dp
	orr	w1,w21,w23,lsl 8
	cmp	w1,w0
	cset	w2,hs
	set_c	w2
	sub	w1,w1,w0
	set_zn16 w1
	knxt
k9A:	mov	w9,1				; SUBW
	b	k_addw
k7A:	mov	w9,0				; ADDW
k_addw:	bl	k_imm
	bl	k_rd16dp
	orr	w1,w21,w23,lsl 8		; ya
	mov	w2,w9				; (retenue)
	cbz	w9,.a
	mvn	w0,w0
	and	w0,w0,0xFFFF			; w
.a:	add	w2,w2,w1
	add	w2,w2,w0			; r
	movl	w3,0xFFFF
	cmp	w2,w3
	cset	w3,hi
	bfxil	w26,w3,0,1
	eor	w3,w1,w0
	mvn	w3,w3
	eor	w4,w1,w2
	and	w3,w3,w4
	ubfx	w3,w3,15,1
	bfi	w26,w3,FS_V,1
	eor	w3,w1,w0
	eor	w3,w3,w2
	ubfx	w3,w3,12,1
	bfi	w26,w3,FS_H,1
	and	w21,w2,0xFF
	ubfx	w23,w2,8,8
	set_zn16 w2
	knxt
kBA:	bl	k_imm				; MOVW YA, d
	bl	k_rd16dp
	and	w21,w0,0xFF
	lsr	w23,w0,8
	set_zn16 w0
	knxt
kDA:	bl	k_imm				; MOVW d, YA
	mov	w9,w0
	k_dpa	w0
	mov	w1,w21
	bl	spc_wr
	add	w0,w9,1
	and	w0,w0,0xFF
	k_dpa	w0
	mov	w1,w23
	bl	spc_wr
	knxt
kCF:	mul	w0,w23,w21			; MUL
	and	w21,w0,0xFF
	ubfx	w23,w0,8,8
	set_zn8	w23
	knxt
k9E:	orr	w0,w21,w23,lsl 8		; DIV : ya
	bic	w26,w26,0x40
	bic	w26,w26,8
	and	w1,w23,15
	and	w2,w22,15
	cmp	w1,w2
	b.lo	.a
	orr	w26,w26,8
.a:	cmp	w23,w22
	b.lo	.b
	orr	w26,w26,0x40
.b:	cmp	w23,w22,lsl 1
	b.hs	.c
	cbz	w22,.z
	udiv	w1,w0,w22
	msub	w2,w1,w22,w0
	and	w21,w1,0xFF
	and	w23,w2,0xFF
	b	.f
.z:	mov	w21,0xFF
	and	w23,w0,0xFF
	b	.f
.c:	sub	w0,w0,w22,lsl 9
	mov	w1,256
	sub	w1,w1,w22
	udiv	w2,w0,w1
	msub	w3,w2,w1,w0
	mov	w4,255
	sub	w4,w4,w2
	and	w21,w4,0xFF
	add	w3,w3,w22
	and	w23,w3,0xFF
.f:	set_zn8	w21
	knxt
kDF:	tbnz	w26,FS_C,.c			; DAA
	cmp	w21,0x99
	b.ls	.d
.c:	add	w21,w21,0x60
	and	w21,w21,0xFF
	orr	w26,w26,1
.d:	tbnz	w26,FS_H,.e
	and	w0,w21,15
	cmp	w0,9
	b.ls	.f
.e:	add	w21,w21,6
	and	w21,w21,0xFF
.f:	set_zn8	w21
	knxt
kBE:	tbz	w26,FS_C,.c			; DAS
	cmp	w21,0x99
	b.ls	.d
.c:	sub	w21,w21,0x60
	and	w21,w21,0xFF
	bic	w26,w26,1
.d:	tbz	w26,FS_H,.e
	and	w0,w21,15
	cmp	w0,9
	b.ls	.f
.e:	sub	w21,w21,6
	and	w21,w21,0xFF
.f:	set_zn8	w21
	knxt
k9F:	lsr	w0,w21,4			; XCN
	lsl	w1,w21,4
	orr	w21,w0,w1
	and	w21,w21,0xFF
	set_zn8	w21
	knxt

; ---- bits ----

k0A:
k2A:
k4A:
k6A:
k8A:
kAA:
kCA:
kEA:	bl	k_imm16
	and	w9,w0,0x1FFF			; a
	lsr	w10,w0,13			; b
	mov	w0,w9
	bl	spc_rd
	mov	w11,w0
	lsr	w1,w0,w10
	and	w1,w1,1				; m
	and	w2,w26,1			; c
	cmp	w13,0x0A
	b.ne	.a
	orr	w2,w2,w1
	b	.setc
.a:	cmp	w13,0x2A
	b.ne	.b
	eor	w1,w1,1
	orr	w2,w2,w1
	b	.setc
.b:	cmp	w13,0x4A
	b.ne	.c
	and	w2,w2,w1
	b	.setc
.c:	cmp	w13,0x6A
	b.ne	.d
	eor	w1,w1,1
	and	w2,w2,w1
	b	.setc
.d:	cmp	w13,0x8A
	b.ne	.e
	eor	w2,w2,w1
	b	.setc
.e:	cmp	w13,0xAA
	b.ne	.f
	mov	w2,w1
.setc:	set_c	w2
	knxt
.f:	cmp	w13,0xCA
	b.ne	.g
	mov	w3,1
	lsl	w3,w3,w10
	bic	w11,w11,w3
	lsl	w2,w2,w10
	orr	w1,w11,w2
	mov	w0,w9
	bl	spc_wr
	knxt
.g:	mov	w3,1				; 0xEA
	lsl	w3,w3,w10
	eor	w1,w11,w3
	mov	w0,w9
	bl	spc_wr
	knxt
k0E:
k4E:	bl	k_imm16
	mov	w9,w0
	bl	spc_rd
	sub	w1,w21,w0
	set_zn8	w1
	cmp	w13,0x0E
	b.ne	.c
	orr	w1,w0,w21
	b	.w
.c:	bic	w1,w0,w21
.w:	mov	w0,w9
	bl	spc_wr
	knxt

; ---- sauts, appels, pile ----

k2E:	bl	ka_dp
	b	k_cbne
kDE:	bl	ka_dpx
k_cbne:	bl	spc_rd
	sub	w0,w21,w0
	bl	k_branch
	knxt
k6E:	bl	ka_dp				; DBNZ d
	mov	w10,w0
	bl	spc_rd
	sub	w9,w0,1
	and	w9,w9,0xFF
	mov	w0,w10
	mov	w1,w9
	bl	spc_wr
	mov	w0,w9
	bl	k_branch
	knxt
kFE:	sub	w23,w23,1
	and	w23,w23,0xFF
	mov	w0,w23
	bl	k_branch
	knxt
k5F:	bl	k_imm16
	mov	w25,w0
	knxt
k1F:	bl	ka_absx
	mov	w9,w0
	bl	spc_rd
	mov	w10,w0
	add	w0,w9,1
	and	w0,w0,0xFFFF
	bl	spc_rd
	orr	w25,w10,w0,lsl 8
	knxt
k3F:	bl	k_imm16
	mov	w9,w0
	bl	k_push_pc
	mov	w25,w9
	knxt
k4F:	bl	k_imm
	mov	w9,w0
	bl	k_push_pc
	mov	w0,0xFF00
	orr	w25,w9,w0
	knxt
k6F:	bl	k_pop
	mov	w9,w0
	bl	k_pop
	orr	w25,w9,w0,lsl 8
	knxt
k7F:	bl	k_pop
	mov	w26,w0
	bl	k_pop
	mov	w9,w0
	bl	k_pop
	orr	w25,w9,w0,lsl 8
	knxt
k0F:	bl	k_push_pc
	mov	w0,w26
	bl	k_push
	orr	w26,w26,0x10
	bic	w26,w26,4
	mov	w0,0xFFDE
	bl	spc_rd
	mov	w9,w0
	mov	w0,0xFFDF
	bl	spc_rd
	orr	w25,w9,w0,lsl 8
	knxt
k0D:	mov	w0,w26
	bl	k_push
	knxt
k2D:	mov	w0,w21
	bl	k_push
	knxt
k4D:	mov	w0,w22
	bl	k_push
	knxt
k6D:	mov	w0,w23
	bl	k_push
	knxt
k8E:	bl	k_pop
	mov	w26,w0
	knxt
kAE:	bl	k_pop
	mov	w21,w0
	knxt
kCE:	bl	k_pop
	mov	w22,w0
	knxt
kEE:	bl	k_pop
	mov	w23,w0
	knxt
kEF:
kFF:	mov	w0,1
	strb	w0,[x19,s_spcSleep]
	knxt

spc_tab:
irp a, 0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F {
  irp b, 0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F \{
    if (0x#a < 0xC) & (0x\#b >= 4) & (0x\#b <= 9)
	dw	k_block-spc_tab
    else if 0x\#b = 1
	dw	k_tcall-spc_tab
    else if 0x\#b = 2
	dw	k_bit-spc_tab
    else if 0x\#b = 3
	dw	k_bbs-spc_tab
    else
	dw	k#a\#b-spc_tab
    end if
  \}
}

; ---- le S-DSP --------------------------------------------------------------

dsp_rates:
	dh	0,2048,1536,1280,1024,768,640,512,384,320,256,192,160,128,96,80
	dh	64,48,40,32,24,20,16,12,10,8,6,5,4,3,2,1
dsp_offsets:
	dh	1,0,1040,536,0,1040,536,0,1040,536,0,1040,536,0,1040,536
	dh	0,1040,536,0,1040,536,0,1040,536,0,1040,536,0,1040,0,0

ENV_RELEASE	= 0
ENV_ATTACK	= 1
ENV_DECAY	= 2
ENV_SUSTAIN	= 3

; w0 = rate -> w0 = 0 / 1 ; perd x1-x3
counter_tick:
	cbz	w0,.r
	adr	x1,dsp_rates
	ldrh	w2,[x1,x0,lsl 1]
	adr	x1,dsp_offsets
	ldrh	w0,[x1,x0,lsl 1]
	ldr	w1,[x19,s_dspCounter]
	add	w0,w0,w1
	udiv	w1,w0,w2
	msub	w0,w1,w2,w0
	cmp	w0,0
	cset	w0,eq
.r:	ret

; x21 = la voix ; perd x0-x8, x16, x17, garde x9-x15
decode_brr:
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	ldr	w0,[x21,V_brrAddr]
	ldrb	w1,[x27,x0]			; header
	lsr	w2,w1,4				; shift
	ubfx	w3,w1,2,2			; filter
	mov	w9,0				; k
.k:	ldr	w4,[x21,V_brrAddr]
	ldr	w5,[x21,V_brrOffset]
	add	w4,w4,w5
	add	w4,w4,w9
	and	w4,w4,0xFFFF
	ldrb	w10,[x27,x4]			; byte
	mov	w11,0				; nib
.n:	lsr	w4,w10,4
	cbz	w11,.a
	and	w4,w10,15
.a:	eor	w4,w4,8
	sub	w4,w4,8				; s
	lsl	w4,w4,w2
	asr	w4,w4,1
	cmp	w2,0xD
	b.lo	.b
	asr	w4,w4,31
	mov	w5,-2048
	and	w4,w4,w5
.b:	ldr	w5,[x21,V_bufPos]		; pos
	add	x6,x21,V_buf
	add	w7,w5,11
	mov	w8,12
	udiv	w16,w7,w8
	msub	w7,w16,w8,w7
	ldrsh	w7,[x6,x7,lsl 1]		; p1
	add	w12,w5,10
	udiv	w16,w12,w8
	msub	w12,w16,w8,w12
	ldrsh	w12,[x6,x12,lsl 1]
	asr	w12,w12,1			; p2
	cmp	w3,2
	b.lo	.f1
	add	w4,w4,w7
	sub	w4,w4,w12
	b.ne	.f3
	add	w4,w4,w12,asr 4
	add	w8,w7,w7,lsl 1
	neg	w8,w8
	add	w4,w4,w8,asr 6
	b	.fe
.f3:	mov	w8,-13
	mul	w8,w8,w7
	add	w4,w4,w8,asr 7
	add	w8,w12,w12,lsl 1
	add	w4,w4,w8,asr 4
	b	.fe
.f1:	cbz	w3,.fe
	add	w4,w4,w7,asr 1
	neg	w8,w7
	add	w4,w4,w8,asr 5
.fe:	clamp16	w4
	lsl	w4,w4,1
	strh	w4,[x6,x5,lsl 1]
	add	w7,w5,12
	strh	w4,[x6,x7,lsl 1]
	add	w5,w5,1
	cmp	w5,12
	csel	w5,wzr,w5,eq
	str	w5,[x21,V_bufPos]
	add	w11,w11,1
	cmp	w11,2
	b.lo	.n
	add	w9,w9,1
	cmp	w9,2
	b.lo	.k
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	ret

; x21 = la voix, x22 = ses registres
run_envelope:
	ldr	w4,[x21,V_env]			; env
	ldr	w5,[x21,V_envMode]
	cbnz	w5,.na
	subs	w4,w4,8
	csel	w4,wzr,w4,lt
	str	w4,[x21,V_env]
	ret
.na:	ldrb	w6,[x22,5]			; adsr0
	ldrb	w7,[x22,6]			; envData
	tbz	w6,7,.gain
	cmp	w5,ENV_DECAY
	b.lo	.att
	sub	w4,w4,1
	sub	w4,w4,w4,asr 8
	and	w8,w7,0x1F			; rateIdx
	cmp	w5,ENV_DECAY
	b.ne	.e
	ubfx	w8,w6,3,4
	and	w8,w8,0x0E
	add	w8,w8,0x10
	b	.e
.att:	and	w8,w6,0x0F
	lsl	w8,w8,1
	add	w8,w8,1
	mov	w1,0x20
	mov	w2,0x400
	cmp	w8,31
	csel	w1,w2,w1,hs
	add	w4,w4,w1
	b	.e
.gain:	ldrb	w7,[x22,7]
	lsr	w1,w7,5				; mode
	cmp	w1,4
	b.hs	.g2
	lsl	w4,w7,4
	mov	w8,31
	b	.e
.g2:	and	w8,w7,0x1F
	b.ne	.g3
	sub	w4,w4,0x20
	b	.e
.g3:	cmp	w1,6
	b.hs	.g4
	sub	w4,w4,1
	sub	w4,w4,w4,asr 8
	b	.e
.g4:	add	w4,w4,0x20
	b.ls	.e
	ldr	w2,[x21,V_hiddenEnv]
	cmp	w2,0x600
	b.lo	.e
	sub	w4,w4,0x18
.e:	cmp	w5,ENV_DECAY
	b.ne	.f
	asr	w1,w4,8
	lsr	w2,w7,5
	cmp	w1,w2
	b.ne	.f
	mov	w5,ENV_SUSTAIN
	str	w5,[x21,V_envMode]
.f:	str	w4,[x21,V_hiddenEnv]
	mov	w1,0x7FF
	cmp	w4,w1
	b.ls	.t
	cmp	w4,0
	csel	w4,wzr,w1,lt
	cmp	w5,ENV_ATTACK
	b.ne	.t
	mov	w1,ENV_DECAY
	str	w1,[x21,V_envMode]
.t:	mov	w0,w8
	mov	x8,x30
	bl	counter_tick
	mov	x30,x8
	cbz	w0,.r
	str	w4,[x21,V_env]
.r:	ret

; w0 = l, w1 = r : 32 kHz -> le debit de l'hote, lineairement
dsp_out:
	ldr	w2,[x19,s_rate]			; k
	ldr	w3,[x19,s_resAcc]
	add	w3,w3,w2
	ldrsh	w4,[x19,s_lastL]
	ldrsh	w5,[x19,s_lastR]
	sub	w4,w4,w0
	sub	w5,w5,w1
	movl	w8,32000
.l:	cmp	w3,w8
	b.lo	.f
	sub	w3,w3,w8
	mul	w6,w4,w3
	sdiv	w6,w6,w2
	add	w6,w6,w0			; ol
	mul	w7,w5,w3
	sdiv	w7,w7,w2
	add	w7,w7,w1			; orr
	ldr	w16,[x19,s_ahead]
	add	w17,w16,1
	and	w17,w17,SNES_ABUF-1
	stp	x9,x10,[sp,-16]!
	ldr	w9,[x19,s_atail]
	cmp	w17,w9
	b.ne	.p
	add	w9,w9,1
	and	w9,w9,SNES_ABUF-1
	str	w9,[x19,s_atail]
.p:	add	x10,x19,S_ABUF
	add	x10,x10,x16,lsl 2
	strh	w6,[x10]
	strh	w7,[x10,2]
	str	w17,[x19,s_ahead]
	ldp	x9,x10,[sp],16
	b	.l
.f:	str	w3,[x19,s_resAcc]
	strh	w0,[x19,s_lastL]
	strh	w1,[x19,s_lastR]
	ret

dsp_gauss:
include 'snes_gauss.inc'

dsp_sample:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,32
	add	x27,x19,S_ARAM
	ldr	w0,[x19,s_dspCounter]
	subs	w0,w0,1
	b.pl	.c1
	movl	w0,30719
.c1:	str	w0,[x19,s_dspCounter]
	ldrb	w28,[x19,S_DSP+0x6C]		; flg
	and	w0,w28,0x1F
	bl	counter_tick
	cbz	w0,.nn
	ldrh	w1,[x19,s_noise]
	lsl	w2,w1,13
	eor	w2,w2,w1,lsl 14
	and	w2,w2,0x4000
	eor	w2,w2,w1,lsr 1
	strh	w2,[x19,s_noise]
.nn:	ldrb	w0,[x19,s_everyOther]
	eor	w0,w0,1
	strb	w0,[x19,s_everyOther]
	str	wzr,[sp]			; kon
	ldrb	w1,[x19,S_DSP+0x5C]
	str	w1,[sp,4]			; koff
	cbz	w0,.ke
	ldrb	w1,[x19,s_konPending]
	str	w1,[sp]
	strb	wzr,[x19,s_konPending]
	ldrb	w2,[x19,s_endx]
	bic	w2,w2,w1
	strb	w2,[x19,s_endx]
.ke:	mov	w11,0				; mainL
	mov	w12,0				; mainR
	mov	w13,0				; echoL
	mov	w14,0				; echoR
	mov	w15,0				; prevOut
	mov	w23,0				; n
	add	x21,x19,S_VOICE
	add	x22,x19,S_DSP
.v:	mov	w24,1
	lsl	w24,w24,w23			; bit
	ldrb	w0,[x22,2]
	ldrb	w1,[x22,3]
	orr	w25,w0,w1,lsl 8
	and	w25,w25,0x3FFF			; pitch
	cbz	w23,.np
	ldrb	w0,[x19,S_DSP+0x2D]
	tst	w0,w24
	b.eq	.np
	asr	w0,w15,5
	mul	w0,w0,w25
	add	w25,w25,w0,asr 10
.np:	ldr	w0,[x21,V_brrAddr]
	ldrb	w29,[x27,x0]			; header
	ldr	w5,[x21,V_konDelay]
	cbz	w5,.nk
	cmp	w5,5
	b.ne	.k4
	bl	.src				; w0 = srcAddr
	ldrb	w1,[x27,x0]
	add	w0,w0,1
	and	w0,w0,0xFFFF
	ldrb	w0,[x27,x0]
	orr	w1,w1,w0,lsl 8
	str	w1,[x21,V_brrAddr]
	mov	w0,1
	str	w0,[x21,V_brrOffset]
	str	wzr,[x21,V_bufPos]
	mov	w29,0
	ldr	w5,[x21,V_konDelay]
.k4:	str	wzr,[x21,V_env]
	str	wzr,[x21,V_hiddenEnv]
	sub	w5,w5,1
	str	w5,[x21,V_konDelay]
	mov	w0,0
	tst	w5,3
	b.eq	.k5
	mov	w0,0x4000
.k5:	str	w0,[x21,V_interpPos]
	mov	w25,0
.nk:	ldrb	w0,[x19,S_DSP+0x3D]
	tst	w0,w24
	b.eq	.gs
	ldrh	w26,[x19,s_noise]
	lsl	w26,w26,1
	sxth	w26,w26
	b	.ot
.gs:	ldr	w0,[x21,V_interpPos]
	ubfx	w1,w0,4,8			; off
	lsr	w0,w0,12
	ldr	w2,[x21,V_bufPos]
	add	w0,w0,w2
	add	x2,x21,V_buf
	add	x2,x2,x0,lsl 1			; in
	adr	x3,dsp_gauss
	mov	w4,255
	sub	w4,w4,w1
	ldrsh	w4,[x3,x4,lsl 1]
	ldrsh	w5,[x2]
	mul	w4,w4,w5
	asr	w26,w4,11
	mov	w4,511
	sub	w4,w4,w1
	ldrsh	w4,[x3,x4,lsl 1]
	ldrsh	w5,[x2,2]
	mul	w4,w4,w5
	add	w26,w26,w4,asr 11
	add	w4,w1,256
	ldrsh	w4,[x3,x4,lsl 1]
	ldrsh	w5,[x2,4]
	mul	w4,w4,w5
	add	w26,w26,w4,asr 11
	sxth	w26,w26
	ldrsh	w4,[x3,x1,lsl 1]
	ldrsh	w5,[x2,6]
	mul	w4,w4,w5
	add	w26,w26,w4,asr 11
	clamp16	w26
	and	w26,w26,0xFFFFFFFE
.ot:	ldr	w0,[x21,V_env]
	mul	w26,w26,w0
	asr	w26,w26,11
	and	w26,w26,0xFFFFFFFE
	lsr	w1,w0,4
	strb	w1,[x22,8]
	lsr	w1,w26,8
	strb	w1,[x22,9]
	tbnz	w28,7,.kl
	and	w0,w29,3
	cmp	w0,1
	b.ne	.nkl
.kl:	str	wzr,[x21,V_envMode]
	str	wzr,[x21,V_env]
.nkl:	ldrb	w0,[x19,s_everyOther]
	cbz	w0,.ne
	ldr	w0,[sp,4]
	tst	w0,w24
	b.eq	.nf
	str	wzr,[x21,V_envMode]
.nf:	ldr	w0,[sp]
	tst	w0,w24
	b.eq	.ne
	mov	w0,5
	str	w0,[x21,V_konDelay]
	mov	w0,ENV_ATTACK
	str	w0,[x21,V_envMode]
.ne:	ldr	w0,[x21,V_konDelay]
	cbnz	w0,.nr
	bl	run_envelope
.nr:	mov	w15,w26				; prevOut
	ldrsb	w0,[x22,0]
	mul	w0,w0,w26
	asr	w0,w0,7				; l
	ldrsb	w1,[x22,1]
	mul	w1,w1,w26
	asr	w1,w1,7				; rr
	add	w11,w11,w0
	clamp16	w11
	add	w12,w12,w1
	clamp16	w12
	ldrb	w2,[x19,S_DSP+0x4D]
	tst	w2,w24
	b.eq	.ne2
	add	w13,w13,w0
	clamp16	w13
	add	w14,w14,w1
	clamp16	w14
.ne2:	ldr	w0,[x21,V_interpPos]
	cmp	w0,0x4000
	b.lo	.ni
	bl	decode_brr
	ldr	w0,[x21,V_brrOffset]
	add	w0,w0,2
	cmp	w0,9
	b.lt	.ns
	ldr	w0,[x21,V_brrAddr]
	add	w0,w0,9
	and	w0,w0,0xFFFF
	str	w0,[x21,V_brrAddr]
	tbz	w29,0,.nl
	bl	.src
	add	w1,w0,3
	and	w1,w1,0xFFFF
	add	w0,w0,2
	and	w0,w0,0xFFFF
	ldrb	w0,[x27,x0]
	ldrb	w1,[x27,x1]
	orr	w0,w0,w1,lsl 8
	str	w0,[x21,V_brrAddr]
	ldrb	w0,[x19,s_endx]
	orr	w0,w0,w24
	strb	w0,[x19,s_endx]
.nl:	mov	w0,1
.ns:	str	w0,[x21,V_brrOffset]
.ni:	ldr	w0,[x21,V_interpPos]
	and	w0,w0,0x3FFF
	add	w0,w0,w25
	mov	w1,0x7FFF
	cmp	w0,w1
	csel	w0,w1,w0,gt
	str	w0,[x21,V_interpPos]
	add	x21,x21,V_SIZE
	add	x22,x22,16
	add	w23,w23,1
	cmp	w23,8
	b.lo	.v
	; l'echo
	ldrb	w0,[x19,S_DSP+0x6D]
	lsl	w0,w0,8
	ldr	w1,[x19,s_echoOffset]
	add	w0,w0,w1
	and	w26,w0,0xFFFF			; ea
	ldr	w0,[x19,s_echoHistPos]
	add	w0,w0,1
	and	w0,w0,7
	str	w0,[x19,s_echoHistPos]
	mov	w23,w0				; pos
	add	x22,x19,s_echoHist
	mov	w1,w26
	bl	.ram16
	asr	w0,w0,1
	add	x1,x22,x23,lsl 2
	strh	w0,[x1]
	add	w1,w26,2
	and	w1,w1,0xFFFF
	bl	.ram16
	asr	w0,w0,1
	add	x1,x22,x23,lsl 2
	strh	w0,[x1,2]
	; le FIR
	mov	w24,0				; c
.fc:	mov	w0,0				; s
	mov	w1,0				; i
	add	x4,x19,S_DSP+0x0F
.fi:	add	w2,w23,1
	add	w2,w2,w1
	and	w2,w2,7
	add	x3,x22,x2,lsl 2
	ldrsh	w2,[x3,x24,lsl 1]
	ldrsb	w3,[x4]
	mul	w2,w2,w3
	add	w0,w0,w2,asr 6
	add	x4,x4,16
	add	w1,w1,1
	cmp	w1,7
	b.lo	.fi
	sxth	w0,w0
	add	x3,x22,x23,lsl 2
	ldrsh	w2,[x3,x24,lsl 1]
	ldrsb	w3,[x19,S_DSP+0x7F]
	mul	w2,w2,w3
	add	w0,w0,w2,asr 6
	clamp16	w0
	and	w0,w0,0xFFFFFFFE
	add	x1,sp,16
	str	w0,[x1,x24,lsl 2]
	add	w24,w24,1
	cmp	w24,2
	b.lo	.fc
	ldp	w5,w6,[sp,16]			; fir [0], fir [1]
	ldrsb	w0,[x19,S_DSP+0x0C]
	mul	w0,w0,w11
	asr	w0,w0,7
	ldrsb	w1,[x19,S_DSP+0x2C]
	mul	w1,w1,w5
	add	w21,w0,w1,asr 7
	clamp16	w21				; outL
	ldrsb	w0,[x19,S_DSP+0x1C]
	mul	w0,w0,w12
	asr	w0,w0,7
	ldrsb	w1,[x19,S_DSP+0x3C]
	mul	w1,w1,w6
	add	w22,w0,w1,asr 7
	clamp16	w22				; outR
	tbnz	w28,5,.nw
	ldrsb	w2,[x19,S_DSP+0x0D]
	mul	w0,w5,w2
	asr	w0,w0,7
	sxth	w0,w0
	add	w0,w0,w13
	clamp16	w0
	and	w0,w0,0xFFFFFFFE		; wl
	mul	w1,w6,w2
	asr	w1,w1,7
	sxth	w1,w1
	add	w1,w1,w14
	clamp16	w1
	and	w1,w1,0xFFFFFFFE		; wr
	strb	w0,[x27,x26]
	add	w2,w26,1
	and	w2,w2,0xFFFF
	lsr	w0,w0,8
	strb	w0,[x27,x2]
	add	w2,w26,2
	and	w2,w2,0xFFFF
	strb	w1,[x27,x2]
	add	w2,w26,3
	and	w2,w2,0xFFFF
	lsr	w1,w1,8
	strb	w1,[x27,x2]
.nw:	ldr	w0,[x19,s_echoOffset]
	cbnz	w0,.eo
	ldrb	w1,[x19,S_DSP+0x7D]
	and	w1,w1,15
	lsl	w1,w1,11
	str	w1,[x19,s_echoLength]
.eo:	add	w0,w0,4
	ldr	w1,[x19,s_echoLength]
	cmp	w0,w1
	csel	w0,wzr,w0,ge
	str	w0,[x19,s_echoOffset]
	mov	w0,w21
	mov	w1,w22
	tbz	w28,6,.o
	mov	w0,0
	mov	w1,0
.o:	bl	dsp_out
	add	sp,sp,32
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue
; -> w0 = srcAddr de la voix (x22)
.src:	ldrb	w0,[x19,S_DSP+0x5D]
	lsl	w0,w0,8
	ldrb	w1,[x22,4]
	add	w0,w0,w1,lsl 2
	and	w0,w0,0xFFFF
	ret
; w1 = ea -> w0 : le mot signe en aram
.ram16:	ldrb	w0,[x27,x1]
	add	w1,w1,1
	and	w1,w1,0xFFFF
	ldrb	w1,[x27,x1]
	orr	w0,w0,w1,lsl 8
	sxth	w0,w0
	ret
