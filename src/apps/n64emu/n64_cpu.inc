; ============================================================================
;  n64_cpu.inc - l'interprete du R4300i : l'unite entiere 64 bits, les
;  branchements et leurs cases de retard, les acces non alignes, LL / SC, COP0
;  (exceptions, interruptions, Count / Compare, TLB de 32 entrees), COP1 (les 16
;  ou 32 registres de Status.FR). Une instruction = 2 cycles.
;
;  Dans un code : w21 = op, w22 rs, w23 rt, w24 rd, x25 = immediat signe ;
;  x26, x27, x28 de travail (x28 : le retour de step).
; ============================================================================

ST_IE		= 1
ST_EXL		= 2
ST_ERL		= 4
EXC_INT		= 0
EXC_MOD		= 1
EXC_TLBL	= 2
EXC_TLBS	= 3
EXC_ADEL	= 4
EXC_ADES	= 5
EXC_SYS		= 8
EXC_BP		= 9
EXC_RI		= 10
EXC_CPU		= 11
EXC_OV		= 12
EXC_TR		= 13
EXC_FPE		= 15
C0_STATUS	= m_cp0+12*8
C0_CAUSE	= m_cp0+13*8

macro r_get dst, idx {
	ldr	dst,[x19,idx,lsl 3]
}
macro r_put val, idx {
	str	val,[x19,idx,lsl 3]
}

; -> w0 = Count
macro count_of {
	ldr	x0,[x19,m_countBase]
	sub	x0,x20,x0
	lsr	x0,x0,1
	and	x0,x0,0xFFFFFFFF
}

schedule_compare:
	count_of
	ldr	w1,[x19,m_cp0+11*8]
	sub	w1,w1,w0
	mov	x2,0x100000000
	cmp	w1,0
	csel	x1,x2,x1,eq
	lsl	x1,x1,1
	mov	w0,EV_COMPARE
	b	schedule

check_interrupts:
	ldr	x0,[x19,C0_CAUSE]
	mov	x2,0xFFFFFFFF
	and	x1,x0,x2
	bic	w1,w1,0x400
	ldr	w2,[x19,m_mi+8]
	ldr	w3,[x19,m_mi+12]
	tst	w2,w3
	b.eq	.a
	orr	w1,w1,0x400
.a:	str	x1,[x19,C0_CAUSE]
	ldr	w2,[x19,C0_STATUS]
	tbz	w2,0,.r
	tst	w2,6
	b.ne	.r
	and	w1,w1,w2
	tst	w1,0xFF00
	b.eq	.r
	mov	w0,1
	strb	w0,[x19,m_excPending]
.r:	ret

; w0 = code, w1 = ce (perd x0-x5)
exception:
	add	x2,x19,m_excCount
	and	x3,x0,31
	ldr	w4,[x2,x3,lsl 2]
	add	w4,w4,1
	str	w4,[x2,x3,lsl 2]
	cbz	w0,.a
	ldr	w2,[x19,m_curPc]
	str	w2,[x19,m_lastExcPc]
	str	w0,[x19,m_lastExcCode]
.a:	ldr	w2,[x19,C0_STATUS]		; st
	mov	w3,0x180			; vec
	tbnz	w2,1,.exl
	ldr	w4,[x19,m_curPc]
	ldrb	w5,[x19,m_inDelay]
	cbz	w5,.b
	sub	w4,w4,4
.b:	sxtw	x4,w4
	str	x4,[x19,m_cp0+14*8]
	ldr	x4,[x19,C0_CAUSE]
	bfi	x4,x5,31,1
	str	x4,[x19,C0_CAUSE]
	cmp	w0,EXC_TLBL
	b.eq	.c
	cmp	w0,EXC_TLBS
	b.ne	.exl
.c:	ldr	w4,[x19,m_tlbMissVector]
	cbnz	w4,.exl
	mov	w3,0
.exl:	ldr	x4,[x19,C0_CAUSE]
	movl	x5,0x3000007C
	bic	x4,x4,x5
	lsl	w0,w0,2
	orr	x4,x4,x0
	lsl	x1,x1,28
	and	x1,x1,0xFFFFFFFF
	orr	x4,x4,x1
	str	x4,[x19,C0_CAUSE]
	orr	w4,w2,ST_EXL
	str	x4,[x19,C0_STATUS]
	movl	w4,0x80000000
	movl	w5,0xBFC00200
	tst	w2,0x400000
	csel	w4,w5,w4,ne
	add	w4,w4,w3
	str	w4,[x19,m_pc]
	add	w4,w4,4
	str	w4,[x19,m_npc]
	strb	wzr,[x19,m_nextDelay]
	str	wzr,[x19,m_tlbMissVector]
	ret

; ---- le TLB ----

; w0 = va, w1 = write -> x0 = 1, w1 = pa ; 0 : exception levee
translate:
	ldr	x2,[x19,m_cp0+10*8]
	and	w2,w2,0xFF			; asid
	mov	w3,0				; k
	ldr	w4,[x19,m_lastTlb]
.l:	add	w5,w4,w3
	and	w5,w5,31			; i
	add	x6,x19,m_tlb
	add	x6,x6,x5,lsl 4			; e
	ldp	w7,w8,[x6]			; mask, hi
	orr	w7,w7,0x1FFF			; m
	eor	w9,w0,w8
	bics	wzr,w9,w7
	b.ne	.n
	ldp	w9,w10,[x6,8]			; lo0, lo1
	and	w11,w9,w10
	tbnz	w11,0,.hit
	and	w11,w8,0xFF
	cmp	w11,w2
	b.ne	.n
.hit:	add	w7,w7,1
	lsr	w7,w7,1				; odd
	tst	w0,w7
	csel	w9,w10,w9,ne			; lo
	tbnz	w9,1,.v
	mov	w2,1
	str	w2,[x19,m_tlbMissVector]
	b	.miss
.v:	cbz	w1,.ok
	tbnz	w9,2,.ok
	; pas sale : TLB modification
	mov	w1,EXC_MOD
	str	x30,[sp,-16]!
	bl	tlb_fault
	ldr	x30,[sp],16
	strb	wzr,[x19,m_excPending]
	mov	x0,0
	ret
.ok:	str	w5,[x19,m_lastTlb]
	ubfx	w9,w9,6,20
	lsl	w9,w9,12
	sub	w10,w7,1
	bic	w9,w9,w10
	and	w0,w0,w10
	orr	w1,w9,w0
	mov	x0,1
	ret
.n:	add	w3,w3,1
	cmp	w3,32
	b.lo	.l
	str	wzr,[x19,m_tlbMissVector]
.miss:	cmp	w1,0
	mov	w2,EXC_TLBL
	mov	w3,EXC_TLBS
	csel	w1,w2,w3,eq
	str	x30,[sp,-16]!
	bl	tlb_fault
	ldr	x30,[sp],16
	mov	x0,0
	ret

; w0 = va, w1 = code
tlb_fault:
	str	w0,[x19,m_badVa]
	sxtw	x2,w0
	str	x2,[x19,m_cp0+8*8]
	ldr	x2,[x19,m_cp0+4*8]
	movl	x3,0x7FFFF0
	bic	x2,x2,x3
	lsr	w3,w0,9
	movl	w4,0x7FFFF0
	and	w3,w3,w4
	orr	x2,x2,x3
	str	x2,[x19,m_cp0+4*8]
	ldr	x2,[x19,m_cp0+10*8]
	and	x2,x2,0xFF
	and	w3,w0,0xFFFFE000
	orr	x2,x2,x3
	str	x2,[x19,m_cp0+10*8]
	mov	w0,w1
	mov	w1,0
	b	exception

; w0 = i
tlb_write:
	and	x0,x0,31
	add	x1,x19,m_tlb
	add	x1,x1,x0,lsl 4			; e
	ldr	x2,[x19,m_cp0+5*8]
	and	w2,w2,0x01FFE000		; mask
	str	w2,[x1]
	ldr	x3,[x19,m_cp0+10*8]
	orr	w4,w2,0x1F00
	bic	w3,w3,w4
	str	w3,[x1,4]
	ldr	x3,[x19,m_cp0+2*8]
	ldr	x4,[x19,m_cp0+3*8]
	and	x5,x3,x4
	and	w5,w5,1				; g
	and	w3,w3,0x3FFFFFFE
	orr	w3,w3,w5
	str	w3,[x1,8]
	and	w4,w4,0x3FFFFFFE
	orr	w4,w4,w5
	str	w4,[x1,12]
	ret

tlb_probe:
	ldr	w0,[x19,m_cp0+10*8]		; hi
	mov	x1,0x80000000
	str	x1,[x19,m_cp0+0*8]
	mov	x1,0
.l:	add	x2,x19,m_tlb
	add	x2,x2,x1,lsl 4
	ldp	w3,w4,[x2]
	orr	w3,w3,0x1FFF
	eor	w5,w0,w4
	bics	wzr,w5,w3
	b.ne	.n
	ldp	w6,w7,[x2,8]
	and	w6,w6,w7
	tbnz	w6,0,.hit
	tst	w5,0xFF
	b.ne	.n
.hit:	str	x1,[x19,m_cp0+0*8]
	ret
.n:	add	x1,x1,1
	cmp	x1,32
	b.lo	.l
	ret

; ---- la memoire virtuelle : x0 = va -> x0 = 1, x1 = valeur ; 0 : exception ----

macro xlate_fast wr, cont, slow {
	lsr	w2,w0,30
	cmp	w2,2
	b.ne	slow
	and	w1,w0,0x1FFFFFFF
}
macro xlate_slow wr, cont {
	str	x30,[sp,-16]!
	mov	w1,wr
	bl	translate
	ldr	x30,[sp],16
	cbnz	x0,cont
	strb	wzr,[x19,m_excPending]
	ret
}
macro addr_err code {
	str	w0,[x19,m_badVa]
	sxtw	x1,w0
	str	x1,[x19,m_cp0+8*8]
	str	x30,[sp,-16]!
	mov	w0,code
	mov	w1,0
	bl	exception
	ldr	x30,[sp],16
	mov	x0,0
	ret
}

rd8:	xlate_fast 0,.p,.t
.p:	cmp	w1,N64_RDRAM_SIZE
	b.hs	.io
	add	x2,x19,M_RDRAM
	eor	w1,w1,3
	ldrb	w1,[x2,x1]
	mov	x0,1
	ret
.t:	xlate_slow 0,.q
.q:	b	.p
.io:	stp	x1,x30,[sp,-16]!
	mov	w0,w1
	bl	read_sub
	ldp	x1,x30,[sp],16
	and	w1,w1,3
	mov	w2,3
	sub	w1,w2,w1
	lsl	w1,w1,3
	lsr	w1,w0,w1
	and	w1,w1,0xFF
	mov	x0,1
	ret

rd16:	tst	w0,1
	b.ne	.e
	xlate_fast 0,.p,.t
.p:	cmp	w1,N64_RDRAM_SIZE
	b.hs	.io
	add	x2,x19,M_RDRAM
	eor	w1,w1,2
	ldrh	w1,[x2,x1]
	mov	x0,1
	ret
.t:	xlate_slow 0,.q
.q:	b	.p
.e:	addr_err EXC_ADEL
.io:	stp	x1,x30,[sp,-16]!
	mov	w0,w1
	bl	read_sub
	ldp	x1,x30,[sp],16
	and	w1,w1,2
	mov	w2,2
	sub	w1,w2,w1
	lsl	w1,w1,3
	lsr	w1,w0,w1
	and	w1,w1,0xFFFF
	mov	x0,1
	ret

rd32:	tst	w0,3
	b.ne	.e
	xlate_fast 0,.p,.t
.p:	cmp	w1,N64_RDRAM_SIZE
	b.hs	.io
	add	x2,x19,M_RDRAM
	ldr	w1,[x2,x1]
	mov	x0,1
	ret
.t:	xlate_slow 0,.q
.q:	b	.p
.e:	addr_err EXC_ADEL
.io:	str	x30,[sp,-16]!
	mov	w0,w1
	bl	read_io
	mov	w1,w0
	mov	x0,1
	ldr	x30,[sp],16
	ret

rd64:	tst	w0,7
	b.ne	.e
	xlate_fast 0,.p,.t
.p:	cmp	w1,N64_RDRAM_SIZE
	b.hs	.io
	add	x2,x19,M_RDRAM
	ldr	x1,[x2,x1]
	ror	x1,x1,32
	mov	x0,1
	ret
.t:	xlate_slow 0,.q
.q:	b	.p
.e:	addr_err EXC_ADEL
.io:	stp	x1,x30,[sp,-16]!
	mov	w0,w1
	bl	read_io
	ldr	x1,[sp]
	str	x0,[sp]
	add	w0,w1,4
	bl	read_io
	ldp	x1,x30,[sp],16
	orr	x1,x0,x1,lsl 32
	mov	x0,1
	ret

; x0 = va, x1 = v -> x0 = 1 / 0
mw8:	mov	x3,x1
	xlate_fast 1,.p,.t
.p:	cmp	w1,N64_RDRAM_SIZE
	b.hs	.io
	add	x2,x19,M_RDRAM
	eor	w0,w1,3
	strb	w3,[x2,x0]
	mov	w0,w1
	mov	w1,1
	b	wrote_ok
.t:	stp	x3,xzr,[sp,-16]!
	xlate_slow2 1,.q
.q:	ldp	x3,x2,[sp],16
	b	.p
.io:	and	w2,w1,3
	mov	w4,3
	sub	w2,w4,w2
	lsl	w2,w2,3				; sh
	and	w3,w3,0xFF
	lsl	w3,w3,w2
	mov	w4,0xFF
	lsl	w2,w4,w2
	mov	w0,w1
	mov	w1,w3
	b	io_wr

mw16:	mov	x3,x1
	tst	w0,1
	b.ne	.e
	xlate_fast 1,.p,.t
.p:	cmp	w1,N64_RDRAM_SIZE
	b.hs	.io
	add	x2,x19,M_RDRAM
	eor	w0,w1,2
	strh	w3,[x2,x0]
	mov	w0,w1
	mov	w1,2
	b	wrote_ok
.t:	stp	x3,xzr,[sp,-16]!
	xlate_slow2 1,.q
.q:	ldp	x3,x2,[sp],16
	b	.p
.e:	addr_err EXC_ADES
.io:	and	w2,w1,2
	mov	w4,2
	sub	w2,w4,w2
	lsl	w2,w2,3
	and	w3,w3,0xFFFF
	lsl	w3,w3,w2
	mov	w4,0xFFFF
	lsl	w2,w4,w2
	mov	w0,w1
	mov	w1,w3
	b	io_wr

mw32:	mov	x3,x1
	tst	w0,3
	b.ne	.e
	xlate_fast 1,.p,.t
.p:	cmp	w1,N64_RDRAM_SIZE
	b.hs	.io
	add	x2,x19,M_RDRAM
	str	w3,[x2,x1]
	mov	w0,w1
	mov	w1,4
	b	wrote_ok
.t:	stp	x3,xzr,[sp,-16]!
	xlate_slow2 1,.q
.q:	ldp	x3,x2,[sp],16
	b	.p
.e:	addr_err EXC_ADES
.io:	mov	w0,w1
	mov	w1,w3
	mov	w2,-1
	b	io_wr

mw64:	mov	x3,x1
	tst	w0,7
	b.ne	.e
	xlate_fast 1,.p,.t
.p:	cmp	w1,N64_RDRAM_SIZE
	b.hs	.io
	add	x2,x19,M_RDRAM
	ror	x0,x3,32
	str	x0,[x2,x1]
	mov	w0,w1
	mov	w1,8
	b	wrote_ok
.t:	stp	x3,xzr,[sp,-16]!
	xlate_slow2 1,.q
.q:	ldp	x3,x2,[sp],16
	b	.p
.e:	addr_err EXC_ADES
.io:	stp	x1,x3,[sp,-16]!
	str	x30,[sp,-16]!
	mov	w0,w1
	lsr	x1,x3,32
	mov	w2,-1
	bl	write_io
	ldr	x30,[sp],16
	ldp	x0,x3,[sp],16
	add	w0,w0,4
	mov	w1,w3
	mov	w2,-1
	b	io_wr

; apres une ecriture en RDRAM : w0 = pa, w1 = octets -> x0 = 1
wrote_ok:
	str	x30,[sp,-16]!
	bl	jit_wrote
	mov	x0,1
	ldr	x30,[sp],16
	ret
io_wr:	str	x30,[sp,-16]!
	bl	write_io
	mov	x0,1
	ldr	x30,[sp],16
	ret

; w0 = va -> x0 = 1, w1 = le mot ; 0
cpu_fetch:
	tst	w0,3
	b.ne	.e
	lsr	w2,w0,30
	cmp	w2,2
	b.ne	.t
	and	w1,w0,0x1FFFFFFF
.p:	cmp	w1,N64_RDRAM_SIZE
	b.hs	.io
	add	x2,x19,M_RDRAM
	ldr	w1,[x2,x1]
	mov	x0,1
	ret
.t:	str	x30,[sp,-16]!
	mov	w1,0
	bl	translate
	ldr	x30,[sp],16
	cbnz	x0,.p
	ret
.e:	str	w0,[x19,m_badVa]
	sxtw	x1,w0
	str	x1,[x19,m_cp0+8*8]
	str	w0,[x19,m_curPc]
	str	x30,[sp,-16]!
	mov	w0,EXC_ADEL
	mov	w1,0
	bl	exception
	ldr	x30,[sp],16
	mov	x0,0
	ret
.io:	str	x30,[sp,-16]!
	mov	w0,w1
	bl	read_io
	mov	w1,w0
	mov	x0,1
	ldr	x30,[sp],16
	ret

; ---- une instruction ----

step:
	mov	x28,x30
if defined N64_TRACE
	bl	n64_trace
end if
	ldrb	w0,[x19,m_excPending]
	cbz	w0,.a
	strb	wzr,[x19,m_excPending]
	ldr	w0,[x19,C0_STATUS]
	tbz	w0,0,.a
	tst	w0,6
	b.ne	.a
	ldr	w1,[x19,C0_CAUSE]
	and	w1,w1,w0
	tst	w1,0xFF00
	b.eq	.a
	ldr	w0,[x19,m_pc]
	str	w0,[x19,m_curPc]
	ldrb	w0,[x19,m_nextDelay]
	strb	w0,[x19,m_inDelay]
	mov	w0,EXC_INT
	mov	w1,0
	bl	exception
.a:	ldr	w0,[x19,m_pc]			; at
	ldrb	w1,[x19,m_nextDelay]
	strb	w1,[x19,m_inDelay]
	strb	wzr,[x19,m_nextDelay]
	str	w0,[x19,m_curPc]
	bl	cpu_fetch
	add	x20,x20,2
	cbnz	x0,.b
	br	x28
.b:	mov	w21,w1				; op
	ldr	w0,[x19,m_npc]
	str	w0,[x19,m_pc]
	add	w0,w0,4
	str	w0,[x19,m_npc]
	ubfx	w22,w21,21,5
	ubfx	w23,w21,16,5
	ubfx	w24,w21,11,5
	sxth	x25,w21
	lsr	w0,w21,26
	jtab	x0,ex_tab

ex_end:
	str	xzr,[x19]
	br	x28

ex_exc:					; w0 = code
	mov	w1,0
	bl	exception
	b	ex_end

; w0 <> 0 : la branche ; le lien est deja la
branch_op:
	mov	w1,1
	strb	w1,[x19,m_nextDelay]
	cbz	w0,ex_end
br_target:
	ldr	w0,[x19,m_curPc]
	add	w0,w0,4
	add	w0,w0,w25,lsl 2
	str	w0,[x19,m_npc]
	b	ex_end
branchl_op:
	cbz	w0,.n
	mov	w1,1
	strb	w1,[x19,m_nextDelay]
	b	br_target
.n:	ldr	w0,[x19,m_npc]
	str	w0,[x19,m_pc]
	add	w0,w0,4
	str	w0,[x19,m_npc]
	b	ex_end

macro link31 {
	ldr	w0,[x19,m_curPc]
	add	w0,w0,8
	sxtw	x0,w0
	str	x0,[x19,31*8]
}

ex_tab:
	dw	ex_special-ex_tab,ex_regimm-ex_tab,ex_j-ex_tab,ex_jal-ex_tab,ex_beq-ex_tab,ex_bne-ex_tab,ex_blez-ex_tab,ex_bgtz-ex_tab
	dw	ex_addi-ex_tab,ex_addiu-ex_tab,ex_slti-ex_tab,ex_sltiu-ex_tab,ex_andi-ex_tab,ex_ori-ex_tab,ex_xori-ex_tab,ex_lui-ex_tab
	dw	ex_cop0-ex_tab,ex_cop1-ex_tab,ex_cop2-ex_tab,ex_cop3-ex_tab,ex_beql-ex_tab,ex_bnel-ex_tab,ex_blezl-ex_tab,ex_bgtzl-ex_tab
	dw	ex_daddi-ex_tab,ex_daddiu-ex_tab,ex_ldl-ex_tab,ex_ldr-ex_tab,ex_ri-ex_tab,ex_ri-ex_tab,ex_ri-ex_tab,ex_ri-ex_tab
	dw	ex_lb-ex_tab,ex_lh-ex_tab,ex_lwl-ex_tab,ex_lw-ex_tab,ex_lbu-ex_tab,ex_lhu-ex_tab,ex_lwr-ex_tab,ex_lwu-ex_tab
	dw	ex_sb-ex_tab,ex_sh-ex_tab,ex_swl-ex_tab,ex_sw-ex_tab,ex_sdl-ex_tab,ex_sdr-ex_tab,ex_swr-ex_tab,ex_end-ex_tab
	dw	ex_ll-ex_tab,ex_lwc1-ex_tab,ex_ri-ex_tab,ex_ri-ex_tab,ex_lld-ex_tab,ex_ldc1-ex_tab,ex_ri-ex_tab,ex_ld-ex_tab
	dw	ex_sc-ex_tab,ex_swc1-ex_tab,ex_ri-ex_tab,ex_ri-ex_tab,ex_scd-ex_tab,ex_sdc1-ex_tab,ex_ri-ex_tab,ex_sd-ex_tab

ex_ri:	mov	w0,EXC_RI
	b	ex_exc
ex_cop2:
	mov	w0,EXC_CPU
	mov	w1,2
	bl	exception
	b	ex_end
ex_cop3:
	mov	w0,EXC_CPU
	mov	w1,3
	bl	exception
	b	ex_end

ex_j:	mov	w0,1
	strb	w0,[x19,m_nextDelay]
j_tgt:	ldr	w0,[x19,m_curPc]
	and	w0,w0,0xF0000000
	and	w1,w21,0x3FFFFFF
	orr	w0,w0,w1,lsl 2
	str	w0,[x19,m_npc]
	b	ex_end
ex_jal:	link31
	mov	w0,1
	strb	w0,[x19,m_nextDelay]
	b	j_tgt

ex_beq:	orr	w0,w22,w23
	cbnz	w0,.g
	cmn	w25,1
	b.ne	.g
	ldr	w0,[x19,m_pc]
	ldr	w1,[x19,m_curPc]
	add	w1,w1,4
	cmp	w0,w1
	b.ne	.g
	bl	cpu_fetch			; « b . » : jusqu'au prochain evenement
	cbz	x0,.g
	cbnz	w1,.g
	ldr	x0,[x19,m_nextEvent]
	cmp	x0,x20
	b.ls	.g
	mov	x20,x0
.g:	r_get	x0,x22
	r_get	x1,x23
	cmp	x0,x1
	cset	w0,eq
	b	branch_op
ex_bne:	r_get	x0,x22
	r_get	x1,x23
	cmp	x0,x1
	cset	w0,ne
	b	branch_op
ex_blez: r_get	x0,x22
	cmp	x0,0
	cset	w0,le
	b	branch_op
ex_bgtz: r_get	x0,x22
	cmp	x0,0
	cset	w0,gt
	b	branch_op
ex_beql: r_get	x0,x22
	r_get	x1,x23
	cmp	x0,x1
	cset	w0,eq
	b	branchl_op
ex_bnel: r_get	x0,x22
	r_get	x1,x23
	cmp	x0,x1
	cset	w0,ne
	b	branchl_op
ex_blezl: r_get	x0,x22
	cmp	x0,0
	cset	w0,le
	b	branchl_op
ex_bgtzl: r_get	x0,x22
	cmp	x0,0
	cset	w0,gt
	b	branchl_op

ex_regimm:
	r_get	x26,x22				; a
	mov	w0,w23
	cmp	w0,0x14
	b.hs	ex_ri
	jtab	x0,.t
.t:	dw	.b0-.t,.b1-.t,.b2-.t,.b3-.t,ex_ri-.t,ex_ri-.t,ex_ri-.t,ex_ri-.t
	dw	.t8-.t,.t9-.t,.ta-.t,.tb-.t,.tc-.t,ex_ri-.t,.te-.t,ex_ri-.t
	dw	.b10-.t,.b11-.t,.b12-.t,.b13-.t
.b0:	lsr	x0,x26,63
	b	branch_op
.b1:	lsr	x0,x26,63
	eor	w0,w0,1
	b	branch_op
.b2:	lsr	x0,x26,63
	b	branchl_op
.b3:	lsr	x0,x26,63
	eor	w0,w0,1
	b	branchl_op
.b10:	link31
	lsr	x0,x26,63
	b	branch_op
.b11:	link31
	lsr	x0,x26,63
	eor	w0,w0,1
	b	branch_op
.b12:	link31
	lsr	x0,x26,63
	b	branchl_op
.b13:	link31
	lsr	x0,x26,63
	eor	w0,w0,1
	b	branchl_op
.t8:	cmp	x26,x25
	b.ge	ex_trap
	b	ex_end
.t9:	cmp	x26,x25
	b.hs	ex_trap
	b	ex_end
.ta:	cmp	x26,x25
	b.lt	ex_trap
	b	ex_end
.tb:	cmp	x26,x25
	b.lo	ex_trap
	b	ex_end
.tc:	cmp	x26,x25
	b.eq	ex_trap
	b	ex_end
.te:	cmp	x26,x25
	b.ne	ex_trap
	b	ex_end
ex_trap:
	mov	w0,EXC_TR
	b	ex_exc

ex_addi:
	r_get	x0,x22
	adds	w0,w0,w25
	b.vs	ex_ov
	sxtw	x0,w0
	r_put	x0,x23
	b	ex_end
ex_ov:	mov	w0,EXC_OV
	b	ex_exc
ex_addiu:
	r_get	x0,x22
	add	w0,w0,w25
	sxtw	x0,w0
	r_put	x0,x23
	b	ex_end
ex_slti:
	r_get	x0,x22
	cmp	x0,x25
	cset	x0,lt
	r_put	x0,x23
	b	ex_end
ex_sltiu:
	r_get	x0,x22
	cmp	x0,x25
	cset	x0,lo
	r_put	x0,x23
	b	ex_end
ex_andi:
	r_get	x0,x22
	and	x1,x21,0xFFFF
	and	x0,x0,x1
	r_put	x0,x23
	b	ex_end
ex_ori:	r_get	x0,x22
	and	x1,x21,0xFFFF
	orr	x0,x0,x1
	r_put	x0,x23
	b	ex_end
ex_xori:
	r_get	x0,x22
	and	x1,x21,0xFFFF
	eor	x0,x0,x1
	r_put	x0,x23
	b	ex_end
ex_lui:	lsl	w0,w21,16
	sxtw	x0,w0
	r_put	x0,x23
	b	ex_end
ex_daddi:
	r_get	x0,x22
	adds	x0,x0,x25
	b.vs	ex_ov
	r_put	x0,x23
	b	ex_end
ex_daddiu:
	r_get	x0,x22
	add	x0,x0,x25
	r_put	x0,x23
	b	ex_end

; -> x26 = EA (32 bits)
macro ea_of {
	r_get	x0,x22
	add	w26,w0,w25
}

ex_lb:	ea_of
	mov	w0,w26
	bl	rd8
	cbz	x0,ex_end
	sxtb	x1,w1
	r_put	x1,x23
	b	ex_end
ex_lbu:	ea_of
	mov	w0,w26
	bl	rd8
	cbz	x0,ex_end
	r_put	x1,x23
	b	ex_end
ex_lh:	ea_of
	mov	w0,w26
	bl	rd16
	cbz	x0,ex_end
	sxth	x1,w1
	r_put	x1,x23
	b	ex_end
ex_lhu:	ea_of
	mov	w0,w26
	bl	rd16
	cbz	x0,ex_end
	r_put	x1,x23
	b	ex_end
ex_lw:	ea_of
	mov	w0,w26
	bl	rd32
	cbz	x0,ex_end
	sxtw	x1,w1
	r_put	x1,x23
	b	ex_end
ex_lwu:	ea_of
	mov	w0,w26
	bl	rd32
	cbz	x0,ex_end
	mov	w1,w1
	r_put	x1,x23
	b	ex_end
ex_ld:	ea_of
	mov	w0,w26
	bl	rd64
	cbz	x0,ex_end
	r_put	x1,x23
	b	ex_end
ex_lwl:	ea_of
	and	w0,w26,0xFFFFFFFC
	bl	rd32
	cbz	x0,ex_end
	and	w2,w26,3
	lsl	w2,w2,3				; sh
	mov	w3,-1
	lsl	w3,w3,w2			; mask
	r_get	x4,x23
	bic	w4,w4,w3
	lsl	w1,w1,w2
	orr	w1,w1,w4
	sxtw	x1,w1
	r_put	x1,x23
	b	ex_end
ex_lwr:	ea_of
	and	w0,w26,0xFFFFFFFC
	bl	rd32
	cbz	x0,ex_end
	and	w2,w26,3
	mov	w3,3
	sub	w2,w3,w2
	lsl	w2,w2,3
	mov	w3,-1
	lsr	w3,w3,w2
	r_get	x4,x23
	bic	w4,w4,w3
	lsr	w1,w1,w2
	orr	w1,w1,w4
	sxtw	x1,w1
	r_put	x1,x23
	b	ex_end
ex_ldl:	ea_of
	and	w0,w26,0xFFFFFFF8
	bl	rd64
	cbz	x0,ex_end
	and	w2,w26,7
	lsl	w2,w2,3
	mov	x3,-1
	lsl	x3,x3,x2
	r_get	x4,x23
	bic	x4,x4,x3
	lsl	x1,x1,x2
	orr	x1,x1,x4
	r_put	x1,x23
	b	ex_end
ex_ldr:	ea_of
	and	w0,w26,0xFFFFFFF8
	bl	rd64
	cbz	x0,ex_end
	and	w2,w26,7
	mov	w3,7
	sub	w2,w3,w2
	lsl	w2,w2,3
	mov	x3,-1
	lsr	x3,x3,x2
	r_get	x4,x23
	bic	x4,x4,x3
	lsr	x1,x1,x2
	orr	x1,x1,x4
	r_put	x1,x23
	b	ex_end
ex_sb:	ea_of
	mov	w0,w26
	r_get	x1,x23
	bl	mw8
	b	ex_end
ex_sh:	ea_of
	mov	w0,w26
	r_get	x1,x23
	bl	mw16
	b	ex_end
ex_sw:	ea_of
	mov	w0,w26
	r_get	x1,x23
	bl	mw32
	b	ex_end
ex_sd:	ea_of
	mov	w0,w26
	r_get	x1,x23
	bl	mw64
	b	ex_end
ex_swl:	ea_of
	and	w0,w26,0xFFFFFFFC
	bl	rd32
	cbz	x0,ex_end
	and	w2,w26,3
	lsl	w2,w2,3
	mov	w3,-1
	lsr	w3,w3,w2
	bic	w1,w1,w3
	r_get	x4,x23
	lsr	w4,w4,w2
	orr	w1,w1,w4
	and	w0,w26,0xFFFFFFFC
	bl	mw32
	b	ex_end
ex_swr:	ea_of
	and	w0,w26,0xFFFFFFFC
	bl	rd32
	cbz	x0,ex_end
	and	w2,w26,3
	mov	w3,3
	sub	w2,w3,w2
	lsl	w2,w2,3
	mov	w3,-1
	lsl	w3,w3,w2
	bic	w1,w1,w3
	r_get	x4,x23
	lsl	w4,w4,w2
	orr	w1,w1,w4
	and	w0,w26,0xFFFFFFFC
	bl	mw32
	b	ex_end
ex_sdl:	ea_of
	and	w0,w26,0xFFFFFFF8
	bl	rd64
	cbz	x0,ex_end
	and	w2,w26,7
	lsl	w2,w2,3
	mov	x3,-1
	lsr	x3,x3,x2
	bic	x1,x1,x3
	r_get	x4,x23
	lsr	x4,x4,x2
	orr	x1,x1,x4
	and	w0,w26,0xFFFFFFF8
	bl	mw64
	b	ex_end
ex_sdr:	ea_of
	and	w0,w26,0xFFFFFFF8
	bl	rd64
	cbz	x0,ex_end
	and	w2,w26,7
	mov	w3,7
	sub	w2,w3,w2
	lsl	w2,w2,3
	mov	x3,-1
	lsl	x3,x3,x2
	bic	x1,x1,x3
	r_get	x4,x23
	lsl	x4,x4,x2
	orr	x1,x1,x4
	and	w0,w26,0xFFFFFFF8
	bl	mw64
	b	ex_end
ex_ll:	ea_of
	mov	w0,w26
	bl	rd32
	cbz	x0,ex_end
	sxtw	x1,w1
	r_put	x1,x23
ll_set:	mov	w0,1
	strb	w0,[x19,m_llbit]
	and	w0,w26,0x1FFFFFFF
	lsr	w0,w0,4
	str	x0,[x19,m_cp0+17*8]
	b	ex_end
ex_lld:	ea_of
	mov	w0,w26
	bl	rd64
	cbz	x0,ex_end
	r_put	x1,x23
	b	ll_set
ex_sc:	ea_of
	ldrb	w0,[x19,m_llbit]
	cbz	w0,sc_no
	mov	w0,w26
	r_get	x1,x23
	bl	mw32
sc_end:	cbz	x0,ex_end
	mov	x0,1
	r_put	x0,x23
	b	ex_end
sc_no:	r_put	xzr,x23
	b	ex_end
ex_scd:	ea_of
	ldrb	w0,[x19,m_llbit]
	cbz	w0,sc_no
	mov	w0,w26
	r_get	x1,x23
	bl	mw64
	b	sc_end

; x0 -> flags Z si CU1 absent
cu1_check:
	ldr	x0,[x19,C0_STATUS]
	tbnz	x0,29,.ok
	mov	w0,EXC_CPU
	mov	w1,1
	bl	exception
	b	ex_end
.ok:	ret

; w0 = n, Status.FR -> x0 = l'adresse du mot 32 bits
fp32_addr:
	ldr	w2,[x19,C0_STATUS]
	tbnz	w2,26,.fr
	tbz	w0,0,.fr
	and	x0,x0,30
	add	x2,x19,m_fpr
	add	x0,x2,x0,lsl 3
	add	x0,x0,4
	ret
.fr:	add	x2,x19,m_fpr
	add	x0,x2,x0,lsl 3
	ret
fp64_addr:
	ldr	w2,[x19,C0_STATUS]
	tbnz	w2,26,.fr
	and	x0,x0,30
.fr:	add	x2,x19,m_fpr
	add	x0,x2,x0,lsl 3
	ret

ex_lwc1:
	bl	cu1_check
	ea_of
	mov	w0,w26
	bl	rd32
	cbz	x0,ex_end
	mov	w26,w1
	mov	w0,w23
	bl	fp32_addr
	str	w26,[x0]
	b	ex_end
ex_ldc1:
	bl	cu1_check
	ea_of
	mov	w0,w26
	bl	rd64
	cbz	x0,ex_end
	mov	x26,x1
	mov	w0,w23
	bl	fp64_addr
	str	x26,[x0]
	b	ex_end
ex_swc1:
	bl	cu1_check
	ea_of
	mov	w0,w23
	bl	fp32_addr
	ldr	w1,[x0]
	mov	w0,w26
	bl	mw32
	b	ex_end
ex_sdc1:
	bl	cu1_check
	ea_of
	mov	w0,w23
	bl	fp64_addr
	ldr	x1,[x0]
	mov	w0,w26
	bl	mw64
	b	ex_end

; ---- SPECIAL ----

ex_special:
	and	w0,w21,63
	ubfx	w26,w21,6,5			; sa
	jtab	x0,.t
.t:	dw	sp_sll-.t,ex_ri-.t,sp_srl-.t,sp_sra-.t,sp_sllv-.t,ex_ri-.t,sp_srlv-.t,sp_srav-.t
	dw	sp_jr-.t,sp_jalr-.t,ex_ri-.t,ex_ri-.t,sp_sys-.t,sp_brk-.t,ex_ri-.t,ex_end-.t
	dw	sp_mfhi-.t,sp_mthi-.t,sp_mflo-.t,sp_mtlo-.t,sp_dsllv-.t,ex_ri-.t,sp_dsrlv-.t,sp_dsrav-.t
	dw	sp_mult-.t,sp_multu-.t,sp_div-.t,sp_divu-.t,sp_dmult-.t,sp_dmultu-.t,sp_ddiv-.t,sp_ddivu-.t
	dw	sp_add-.t,sp_addu-.t,sp_sub-.t,sp_subu-.t,sp_and-.t,sp_or-.t,sp_xor-.t,sp_nor-.t
	dw	ex_ri-.t,ex_ri-.t,sp_slt-.t,sp_sltu-.t,sp_dadd-.t,sp_daddu-.t,sp_dsub-.t,sp_dsubu-.t
	dw	sp_tge-.t,sp_tgeu-.t,sp_tlt-.t,sp_tltu-.t,sp_teq-.t,ex_ri-.t,sp_tne-.t,ex_ri-.t
	dw	sp_dsll-.t,ex_ri-.t,sp_dsrl-.t,sp_dsra-.t,sp_dsll32-.t,ex_ri-.t,sp_dsrl32-.t,sp_dsra32-.t

macro put_rd32 reg {
	sxtw	x0,reg
	r_put	x0,x24
	b	ex_end
}
macro put_rd64 reg {
	r_put	reg,x24
	b	ex_end
}

sp_sll:	r_get	x0,x23
	lsl	w0,w0,w26
	put_rd32 w0
sp_srl:	r_get	x0,x23
	lsr	w0,w0,w26
	put_rd32 w0
sp_sra:	r_get	x0,x23
	asr	x0,x0,x26
	put_rd32 w0
sp_sllv: r_get	x0,x23
	r_get	x1,x22
	lsl	w0,w0,w1
	put_rd32 w0
sp_srlv: r_get	x0,x23
	r_get	x1,x22
	lsr	w0,w0,w1
	put_rd32 w0
sp_srav: r_get	x0,x23
	r_get	x1,x22
	and	x1,x1,31
	asr	x0,x0,x1
	put_rd32 w0
sp_jr:	mov	w0,1
	strb	w0,[x19,m_nextDelay]
	r_get	x0,x22
	str	w0,[x19,m_npc]
	b	ex_end
sp_jalr: r_get	x1,x22
	ldr	w0,[x19,m_curPc]
	add	w0,w0,8
	sxtw	x0,w0
	r_put	x0,x24
	mov	w0,1
	strb	w0,[x19,m_nextDelay]
	str	w1,[x19,m_npc]
	b	ex_end
sp_sys:	mov	w0,EXC_SYS
	b	ex_exc
sp_brk:	mov	w0,EXC_BP
	b	ex_exc
sp_mfhi: ldr	x0,[x19,m_hi]
	put_rd64 x0
sp_mthi: r_get	x0,x22
	str	x0,[x19,m_hi]
	b	ex_end
sp_mflo: ldr	x0,[x19,m_lo]
	put_rd64 x0
sp_mtlo: r_get	x0,x22
	str	x0,[x19,m_lo]
	b	ex_end
sp_dsllv: r_get	x0,x23
	r_get	x1,x22
	lsl	x0,x0,x1
	put_rd64 x0
sp_dsrlv: r_get	x0,x23
	r_get	x1,x22
	lsr	x0,x0,x1
	put_rd64 x0
sp_dsrav: r_get	x0,x23
	r_get	x1,x22
	asr	x0,x0,x1
	put_rd64 x0
sp_mult: r_get	x0,x22
	r_get	x1,x23
	smull	x0,w0,w1
	b	mul_end
sp_multu: r_get	x0,x22
	r_get	x1,x23
	umull	x0,w0,w1
mul_end: sxtw	x1,w0
	str	x1,[x19,m_lo]
	asr	x1,x0,32
	sxtw	x1,w1
	str	x1,[x19,m_hi]
	add	x20,x20,8
	b	ex_end
sp_div:	r_get	x0,x22
	r_get	x1,x23
	cbnz	w1,.a
	sxtw	x2,w0
	str	x2,[x19,m_hi]
	mov	x2,1
	mov	x3,-1
	tst	w0,0x80000000
	csel	x2,x2,x3,ne
	str	x2,[x19,m_lo]
	b	.e
.a:	cmn	w1,1
	b.ne	.b
	mov	w2,0x80000000
	cmp	w0,w2
	b.ne	.b
	sxtw	x2,w2
	str	x2,[x19,m_lo]
	str	xzr,[x19,m_hi]
	b	.e
.b:	sdiv	w2,w0,w1
	msub	w3,w2,w1,w0
	sxtw	x2,w2
	str	x2,[x19,m_lo]
	sxtw	x3,w3
	str	x3,[x19,m_hi]
.e:	add	x20,x20,70
	b	ex_end
sp_divu: r_get	x0,x22
	r_get	x1,x23
	cbnz	w1,.b
	mov	x2,-1
	str	x2,[x19,m_lo]
	sxtw	x2,w0
	str	x2,[x19,m_hi]
	b	.e
.b:	udiv	w2,w0,w1
	msub	w3,w2,w1,w0
	sxtw	x2,w2
	str	x2,[x19,m_lo]
	sxtw	x3,w3
	str	x3,[x19,m_hi]
.e:	add	x20,x20,70
	b	ex_end
sp_dmult: r_get	x0,x22
	r_get	x1,x23
	mul	x2,x0,x1
	smulh	x3,x0,x1
	b	dmul_end
sp_dmultu: r_get x0,x22
	r_get	x1,x23
	mul	x2,x0,x1
	umulh	x3,x0,x1
dmul_end: str	x2,[x19,m_lo]
	str	x3,[x19,m_hi]
	add	x20,x20,14
	b	ex_end
sp_ddiv: r_get	x0,x22
	r_get	x1,x23
	cbnz	x1,.a
	str	x0,[x19,m_hi]
	mov	x2,1
	mov	x3,-1
	cmp	x0,0
	csel	x2,x2,x3,lt
	str	x2,[x19,m_lo]
	b	.e
.a:	cmn	x1,1
	b.ne	.b
	mov	x2,0x8000000000000000
	cmp	x0,x2
	b.ne	.b
	str	x0,[x19,m_lo]
	str	xzr,[x19,m_hi]
	b	.e
.b:	sdiv	x2,x0,x1
	msub	x3,x2,x1,x0
	str	x2,[x19,m_lo]
	str	x3,[x19,m_hi]
.e:	add	x20,x20,138
	b	ex_end
sp_ddivu: r_get	x0,x22
	r_get	x1,x23
	cbnz	x1,.b
	mov	x2,-1
	str	x2,[x19,m_lo]
	str	x0,[x19,m_hi]
	b	.e
.b:	udiv	x2,x0,x1
	msub	x3,x2,x1,x0
	str	x2,[x19,m_lo]
	str	x3,[x19,m_hi]
.e:	add	x20,x20,138
	b	ex_end
sp_add:	r_get	x0,x22
	r_get	x1,x23
	adds	w0,w0,w1
	b.vs	ex_ov
	put_rd32 w0
sp_addu: r_get	x0,x22
	r_get	x1,x23
	add	w0,w0,w1
	put_rd32 w0
sp_sub:	r_get	x0,x22
	r_get	x1,x23
	subs	w0,w0,w1
	b.vs	ex_ov
	put_rd32 w0
sp_subu: r_get	x0,x22
	r_get	x1,x23
	sub	w0,w0,w1
	put_rd32 w0
sp_and:	r_get	x0,x22
	r_get	x1,x23
	and	x0,x0,x1
	put_rd64 x0
sp_or:	r_get	x0,x22
	r_get	x1,x23
	orr	x0,x0,x1
	put_rd64 x0
sp_xor:	r_get	x0,x22
	r_get	x1,x23
	eor	x0,x0,x1
	put_rd64 x0
sp_nor:	r_get	x0,x22
	r_get	x1,x23
	orr	x0,x0,x1
	mvn	x0,x0
	put_rd64 x0
sp_slt:	r_get	x0,x22
	r_get	x1,x23
	cmp	x0,x1
	cset	x0,lt
	put_rd64 x0
sp_sltu: r_get	x0,x22
	r_get	x1,x23
	cmp	x0,x1
	cset	x0,lo
	put_rd64 x0
sp_dadd: r_get	x0,x22
	r_get	x1,x23
	adds	x0,x0,x1
	b.vs	ex_ov
	put_rd64 x0
sp_daddu: r_get	x0,x22
	r_get	x1,x23
	add	x0,x0,x1
	put_rd64 x0
sp_dsub: r_get	x0,x22
	r_get	x1,x23
	subs	x0,x0,x1
	b.vs	ex_ov
	put_rd64 x0
sp_dsubu: r_get	x0,x22
	r_get	x1,x23
	sub	x0,x0,x1
	put_rd64 x0
macro trap_op name, cc {
name:	r_get	x0,x22
	r_get	x1,x23
	cmp	x0,x1
	b.#cc	ex_trap
	b	ex_end
}
trap_op sp_tge, ge
trap_op sp_tgeu, hs
trap_op sp_tlt, lt
trap_op sp_tltu, lo
trap_op sp_teq, eq
trap_op sp_tne, ne
sp_dsll: r_get	x0,x23
	lsl	x0,x0,x26
	put_rd64 x0
sp_dsrl: r_get	x0,x23
	lsr	x0,x0,x26
	put_rd64 x0
sp_dsra: r_get	x0,x23
	asr	x0,x0,x26
	put_rd64 x0
sp_dsll32: r_get x0,x23
	add	w26,w26,32
	lsl	x0,x0,x26
	put_rd64 x0
sp_dsrl32: r_get x0,x23
	add	w26,w26,32
	lsr	x0,x0,x26
	put_rd64 x0
sp_dsra32: r_get x0,x23
	add	w26,w26,32
	asr	x0,x0,x26
	put_rd64 x0

; ---- COP0 ----

; -> w0 = Random
macro random_of {
	ldr	w1,[x19,m_cp0+6*8]
	and	w1,w1,31			; wired
	mov	w0,31
	cmp	w1,31
	b.hs	@f
	mov	w2,32
	sub	w2,w2,w1
	lsr	x3,x20,1
	udiv	x4,x3,x2
	msub	x4,x4,x2,x3
	sub	w0,w0,w4
@@:
}

ex_cop0:
	cmp	w22,2
	b.lo	.mf
	cmp	w22,4
	b.eq	.mt
	cmp	w22,5
	b.eq	.mt
	cmp	w22,0x10
	b.lo	ex_ri
	and	w0,w21,63
	cmp	w0,1
	b.eq	.tlbr
	cmp	w0,2
	b.eq	.tlbwi
	cmp	w0,6
	b.eq	.tlbwr
	cmp	w0,8
	b.eq	.tlbp
	cmp	w0,0x18
	b.ne	ex_end
	ldr	w0,[x19,C0_STATUS]		; ERET
	tbz	w0,2,.ee
	ldr	w1,[x19,m_cp0+30*8]
	bic	w0,w0,ST_ERL
	b	.es
.ee:	ldr	w1,[x19,m_cp0+14*8]
	bic	w0,w0,ST_EXL
.es:	str	w1,[x19,m_pc]
	str	x0,[x19,C0_STATUS]
	add	w1,w1,4
	str	w1,[x19,m_npc]
	strb	wzr,[x19,m_nextDelay]
	strb	wzr,[x19,m_llbit]
	bl	check_interrupts
	b	ex_end
.tlbr:	ldr	x0,[x19,m_cp0]
	and	x0,x0,31
	add	x1,x19,m_tlb
	add	x1,x1,x0,lsl 4
	ldp	w2,w3,[x1]			; mask, hi
	ldp	w4,w5,[x1,8]
	and	w6,w4,w5
	and	w6,w6,1				; g
	str	x2,[x19,m_cp0+5*8]
	bic	w3,w3,w2
	str	x3,[x19,m_cp0+10*8]
	bic	w4,w4,1
	orr	w4,w4,w6
	str	x4,[x19,m_cp0+2*8]
	bic	w5,w5,1
	orr	w5,w5,w6
	str	x5,[x19,m_cp0+3*8]
	b	ex_end
.tlbwi:	ldr	x0,[x19,m_cp0]
	bl	tlb_write
	b	ex_end
.tlbwr:	random_of
	bl	tlb_write
	b	ex_end
.tlbp:	bl	tlb_probe
	b	ex_end
.mf:	cmp	w24,9
	b.ne	.m1
	count_of
	b	.mr
.m1:	cmp	w24,1
	b.ne	.m2
	random_of
	b	.mr
.m2:	add	x1,x19,m_cp0
	ldr	x0,[x1,x24,lsl 3]
.mr:	cbnz	w22,.md
	sxtw	x0,w0
.md:	r_put	x0,x23
	b	ex_end
.mt:	r_get	x0,x23
	cmp	w22,4
	b.ne	.mv
	sxtw	x0,w0				; v
.mv:	add	x1,x19,m_cp0
	mov	x2,x24
	cmp	w24,32
	b.hs	ex_end
	jtab	x2,.t
.t:	dw	.i0-.t,ex_end-.t,.lo-.t,.lo-.t,.ctx-.t,.pm-.t,.wi-.t,.def-.t
	dw	ex_end-.t,.cnt-.t,.ehi-.t,.cmp-.t,.st-.t,.cau-.t,.def-.t,ex_end-.t
	dw	.cfg-.t,.def-.t,.def-.t,.def-.t,.def-.t,.def-.t,.def-.t,.def-.t
	dw	.def-.t,.def-.t,.def-.t,.def-.t,.def-.t,.def-.t,.def-.t,.def-.t
.def:	str	x0,[x1,x24,lsl 3]
	b	ex_end
.i0:	movl	x2,0x8000003F
	and	x0,x0,x2
	b	.def
.lo:	and	x0,x0,0x3FFFFFFF
	b	.def
.ctx:	ldr	x2,[x1,x24,lsl 3]
	and	x2,x2,0x7FFFFF
	and	x0,x0,0xFFFFFFFFFF800000
	orr	x0,x0,x2
	b	.def
.pm:	and	x0,x0,0x01FFE000
	b	.def
.wi:	and	x0,x0,63
	b	.def
.cnt:	mov	w0,w0
	sub	x0,x20,x0,lsl 1
	str	x0,[x19,m_countBase]
	bl	schedule_compare
	b	ex_end
.ehi:	movl	x2,0xC00000FFFFFFE0FF
	and	x0,x0,x2
	b	.def
.cmp:	mov	w0,w0
	str	x0,[x1,x24,lsl 3]
	ldr	x0,[x19,C0_CAUSE]
	bic	x0,x0,0x8000
	str	x0,[x19,C0_CAUSE]
	bl	schedule_compare
	b	ex_end
.st:	movl	x2,0xFF57FFFF
	and	x0,x0,x2
	str	x0,[x19,C0_STATUS]
	bl	check_interrupts
	b	ex_end
.cau:	ldr	x2,[x19,C0_CAUSE]
	bic	x2,x2,0x300
	and	x0,x0,0x300
	orr	x0,x0,x2
	str	x0,[x19,C0_CAUSE]
	bl	check_interrupts
	b	ex_end
.cfg:	ldr	x2,[x19,m_cp0+16*8]
	movl	x3,0x0F00800F
	bic	x2,x2,x3
	and	x0,x0,x3
	orr	x0,x0,x2
	str	x0,[x19,m_cp0+16*8]
	b	ex_end

; ---- COP1 ----

macro f_rd32 sreg, nreg {			; sreg <- get32 (nreg)
	mov	w0,nreg
	bl	fp32_addr
	ldr	sreg,[x0]
}
macro f_wr32 sreg, nreg {
	mov	w0,nreg
	bl	fp32_addr
	str	sreg,[x0]
}
macro f_rd64 dreg, nreg {
	mov	w0,nreg
	bl	fp64_addr
	ldr	dreg,[x0]
}
macro f_wr64 dreg, nreg {
	mov	w0,nreg
	bl	fp64_addr
	str	dreg,[x0]
}

ex_cop1:
	bl	cu1_check
	ubfx	w26,w21,6,5			; fd
	mov	w0,w22
	cmp	w0,0x16
	b.hs	ex_ri
	jtab	x0,.t
.t:	dw	.mfc-.t,.dmf-.t,.cfc-.t,.fx-.t,.mtc-.t,.dmt-.t,.ctc-.t,.fx-.t
	dw	.bc-.t,.fx-.t,.fx-.t,.fx-.t,.fx-.t,.fx-.t,.fx-.t,.fx-.t
	dw	cp_s-.t,cp_d-.t,ex_ri-.t,ex_ri-.t,cp_w-.t,cp_l-.t
.fx:	mov	w0,EXC_RI
	b	ex_exc
.mfc:	f_rd32	w1,w24
	sxtw	x1,w1
	r_put	x1,x23
	b	ex_end
.dmf:	f_rd64	x1,w24
	r_put	x1,x23
	b	ex_end
.cfc:	mov	w1,0
	cmp	w24,31
	b.ne	.cf0
	ldr	w1,[x19,m_fcr31]
	b	.cfs
.cf0:	cbnz	w24,.cfs
	mov	w1,0xA00
.cfs:	sxtw	x1,w1
	r_put	x1,x23
	b	ex_end
.mtc:	r_get	x1,x23
	f_wr32	w1,w24
	b	ex_end
.dmt:	r_get	x1,x23
	f_wr64	x1,w24
	b	ex_end
.ctc:	cmp	w24,31
	b.ne	ex_end
	r_get	x1,x23
	movl	w0,0x0183FFFF
	and	w1,w1,w0
	str	w1,[x19,m_fcr31]
	b	ex_end
.bc:	ldr	w0,[x19,m_fcr31]
	ubfx	w0,w0,23,1			; c
	tbnz	w23,0,.b1
	eor	w0,w0,1
.b1:	tbnz	w23,1,branchl_op
	b	branch_op

; le mode d'arrondi w0 sur d0 -> d0
fp_round:
	cbnz	w0,.a
	frintn	d0,d0
	ret
.a:	cmp	w0,1
	b.ne	.b
	frintz	d0,d0
	ret
.b:	cmp	w0,2
	b.ne	.c
	frintp	d0,d0
	ret
.c:	frintm	d0,d0
	ret

; d0 = a, d1 = b : les C.cond, w27 = fn
fp_cmp:
	mov	x8,x30
	ldr	w2,[x19,m_fcr31]
	movl	w3,0x3F000
	bic	w2,w2,w3
	str	w2,[x19,m_fcr31]
	fcmp	d0,d1
	cset	w3,vs				; un
	cset	w4,mi				; a < b
	cset	w5,eq				; a == b
	cbz	w3,.a
	tbz	w27,3,.a
	movl	w6,0x10040
	orr	w2,w2,w6
	str	w2,[x19,m_fcr31]
	tbz	w2,11,.a
	mov	w0,EXC_FPE
	mov	w1,0
	bl	exception
	br	x8
.a:	mov	w6,0
	cbz	w3,.n
	and	w6,w27,1
	b	.s
.n:	ubfx	w7,w27,2,1
	and	w7,w7,w4
	ubfx	w6,w27,1,1
	and	w6,w6,w5
	orr	w6,w6,w7
.s:	ldr	w2,[x19,m_fcr31]
	bfi	w2,w6,23,1
	str	w2,[x19,m_fcr31]
	br	x8

cp_s:	and	w27,w21,63			; fn
	f_rd32	s8,w24
	f_rd32	s9,w23
	cmp	w27,0x30
	b.hs	.cm
	cmp	w27,0x26
	b.hs	ex_end
	jtab	x27,.t
.t:	dw	.add-.t,.sub-.t,.mul-.t,.div-.t,.sqrt-.t,.abs-.t,.mov-.t,.neg-.t
	dw	.l-.t,.l-.t,.l-.t,.l-.t,.w-.t,.w-.t,.w-.t,.w-.t
	dw	ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t
	dw	ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t
	dw	ex_end-.t,.cvd-.t,ex_end-.t,ex_end-.t,.cw-.t,.cl-.t
.add:	fadd	s0,s8,s9
	b	.st
.sub:	fsub	s0,s8,s9
	b	.st
.mul:	fmul	s0,s8,s9
	add	x20,x20,4
	b	.st
.div:	fdiv	s0,s8,s9
	add	x20,x20,56
	b	.st
.sqrt:	fsqrt	s0,s8
	add	x20,x20,56
	b	.st
.abs:	fcmp	s8,0.0
	b.mi	.absn
	fmov	s0,s8
	b.ne	.st
	fmov	s0,wzr
	b	.st
.absn:	fneg	s0,s8
	b	.st
.mov:	f_rd32	w1,w24
	f_wr32	w1,w26
	b	ex_end
.neg:	f_rd32	w1,w24
	eor	w1,w1,0x80000000
	f_wr32	w1,w26
	b	ex_end
.st:	f_wr32	s0,w26
	b	ex_end
.l:	fcvt	d0,s8
	and	w0,w27,3
	bl	fp_round
.lst:	fcvtzs	x1,d0
	f_wr64	x1,w26
	b	ex_end
.w:	fcvt	d0,s8
	and	w0,w27,3
	bl	fp_round
.wst:	fcvtzs	w1,d0
	f_wr32	w1,w26
	b	ex_end
.cvd:	fcvt	d0,s8
	f_wr64	d0,w26
	b	ex_end
.cw:	fcvt	d0,s8
	ldr	w0,[x19,m_fcr31]
	and	w0,w0,3
	bl	fp_round
	b	.wst
.cl:	fcvt	d0,s8
	ldr	w0,[x19,m_fcr31]
	and	w0,w0,3
	bl	fp_round
	b	.lst
.cm:	fcvt	d0,s8
	fcvt	d1,s9
	bl	fp_cmp
	b	ex_end

cp_d:	and	w27,w21,63
	f_rd64	d8,w24
	f_rd64	d9,w23
	cmp	w27,0x30
	b.hs	.cm
	cmp	w27,0x26
	b.hs	ex_end
	jtab	x27,.t
.t:	dw	.add-.t,.sub-.t,.mul-.t,.div-.t,.sqrt-.t,.abs-.t,.mov-.t,.neg-.t
	dw	.l-.t,.l-.t,.l-.t,.l-.t,.w-.t,.w-.t,.w-.t,.w-.t
	dw	ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t
	dw	ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t,ex_end-.t
	dw	.cvs-.t,ex_end-.t,ex_end-.t,ex_end-.t,.cw-.t,.cl-.t
.add:	fadd	d0,d8,d9
	b	.st
.sub:	fsub	d0,d8,d9
	b	.st
.mul:	fmul	d0,d8,d9
	add	x20,x20,7
	b	.st
.div:	fdiv	d0,d8,d9
	add	x20,x20,114
	b	.st
.sqrt:	fsqrt	d0,d8
	add	x20,x20,114
	b	.st
.abs:	fcmp	d8,0.0
	b.mi	.absn
	fmov	d0,d8
	b.ne	.st
	fmov	d0,xzr
	b	.st
.absn:	fneg	d0,d8
	b	.st
.mov:	f_rd64	x1,w24
	f_wr64	x1,w26
	b	ex_end
.neg:	f_rd64	x1,w24
	eor	x1,x1,0x8000000000000000
	f_wr64	x1,w26
	b	ex_end
.st:	f_wr64	d0,w26
	b	ex_end
.l:	fmov	d0,d8
	and	w0,w27,3
	bl	fp_round
.lst:	fcvtzs	x1,d0
	f_wr64	x1,w26
	b	ex_end
.w:	fmov	d0,d8
	and	w0,w27,3
	bl	fp_round
.wst:	fcvtzs	w1,d0
	f_wr32	w1,w26
	b	ex_end
.cvs:	fcvt	s0,d8
	f_wr32	s0,w26
	b	ex_end
.cw:	fmov	d0,d8
	ldr	w0,[x19,m_fcr31]
	and	w0,w0,3
	bl	fp_round
	b	.wst
.cl:	fmov	d0,d8
	ldr	w0,[x19,m_fcr31]
	and	w0,w0,3
	bl	fp_round
	b	.lst
.cm:	fmov	d0,d8
	fmov	d1,d9
	bl	fp_cmp
	b	ex_end

cp_w:	and	w27,w21,63
	f_rd32	w1,w24
	cmp	w27,0x20
	b.ne	.d
	scvtf	s0,w1
	f_wr32	s0,w26
	b	ex_end
.d:	cmp	w27,0x21
	b.ne	ex_end
	scvtf	d0,w1
	f_wr64	d0,w26
	b	ex_end

cp_l:	and	w27,w21,63
	f_rd64	x1,w24
	cmp	w27,0x20
	b.ne	.d
	scvtf	s0,x1
	f_wr32	s0,w26
	b	ex_end
.d:	cmp	w27,0x21
	b.ne	ex_end
	scvtf	d0,x1
	f_wr64	d0,w26
	b	ex_end
