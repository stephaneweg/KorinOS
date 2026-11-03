; ============================================================================
;  snes_cpu.inc - le 65C816 : modes natif et emulation, A 8 / 16 bits, X Y
;  8 / 16 bits, tous les codes et modes d'adressage, ADC / SBC decimaux, MVN /
;  MVP, WAI / STP, NMI / IRQ / BRK / COP. Chaque acces coute la vitesse de son
;  adresse, chaque cycle interne 6 horloges.
;
;  Un mode d'adressage rend w10 = adresse, w11 <> 0 : le deuxieme octet tourne
;  dans la banque 0. Les routines d'un code gardent x9-x15 a travers le bus.
; ============================================================================

FP_C		= 0
FP_Z		= 1
FP_I		= 2
FP_D		= 3
FP_X		= 4
FP_M		= 5
FP_V		= 6
FP_N		= 7
FP_E		= 8

macro nxt {
	b	cpu_next
}
macro set_pc reg {
	bfxil	w25,reg,0,16
}
macro set_pb reg {
	bfi	w25,reg,16,8
}
macro set_zn8 reg {
	ands	w16,reg,0xFF
	cset	w17,eq
	bfi	w26,w17,1,1
	lsr	w16,w16,7
	bfi	w26,w16,7,1
}
macro set_zn16 reg {
	ands	w16,reg,0xFFFF
	cset	w17,eq
	bfi	w26,w17,1,1
	lsr	w16,w16,15
	bfi	w26,w16,7,1
}
macro set_c reg {			; reg = 0 / 1
	bfxil	w26,reg,0,1
}
macro dp_io {
	tst	w27,0xFF
	b.eq	@f
	io_cycle
@@:
}
macro next_addr reg {			; le deuxieme octet de (w10, w11)
	add	reg,w10,1
	and	w1,reg,0xFFFF
	and	reg,reg,0xFFFFFF
	cmp	w11,0
	csel	reg,w1,reg,ne
}
macro db_shl16 reg {
	and	reg,w27,0xFF0000
}

cpu_fetch:				; -> w0
	mov	w0,w25
	add	w1,w25,1
	bfxil	w25,w1,0,16
	b	cpu_read

cpu_fetch16:
	str	x30,[sp,-16]!
	bl	cpu_fetch
	str	w0,[sp,8]
	bl	cpu_fetch
	ldr	w1,[sp,8]
	orr	w0,w1,w0,lsl 8
	ldr	x30,[sp],16
	ret

cpu_push:				; w0 = v
	str	x30,[sp,-16]!
	mov	w1,w0
	mov	w0,w24
	bl	cpu_write
	sub	w0,w24,1
	tbnz	w26,FP_E,.e
	and	w24,w0,0xFFFF
	ldr	x30,[sp],16
	ret
.e:	and	w0,w0,0xFF
	orr	w24,w0,0x100
	ldr	x30,[sp],16
	ret

cpu_pull:				; -> w0
	add	w0,w24,1
	tbnz	w26,FP_E,.e
	and	w24,w0,0xFFFF
	mov	w0,w24
	b	cpu_read
.e:	and	w0,w0,0xFF
	orr	w24,w0,0x100
	mov	w0,w24
	b	cpu_read

cpu_push_n:				; w0 = v : S ne reste pas en page 1
	str	x30,[sp,-16]!
	mov	w1,w0
	mov	w0,w24
	bl	cpu_write
	sub	w24,w24,1
	and	w24,w24,0xFFFF
	ldr	x30,[sp],16
	ret

cpu_pull_n:
	add	w24,w24,1
	and	w24,w24,0xFFFF
	mov	w0,w24
	b	cpu_read

macro fix_e {
	tbz	w26,FP_E,@f
	and	w24,w24,0xFF
	orr	w24,w24,0x100
	and	w22,w22,0xFF
	and	w23,w23,0xFF
	orr	w26,w26,0x30
@@:
}

; w0 = v (8 bits)
set_p:
	bfxil	w26,w0,0,8
	tbz	w26,FP_E,.a
	orr	w26,w26,0x30
.a:	tbz	w26,FP_X,.b
	and	w22,w22,0xFF
	and	w23,w23,0xFF
.b:	ret

; w9 = vecteur natif, w10 = vecteur emulation, w11 = BRK / COP
cpu_interrupt:
	str	x30,[sp,-16]!
	add	x20,x20,12
	tbnz	w26,FP_E,.e
	lsr	w0,w25,16
	bl	cpu_push
.e:	ubfx	w0,w25,8,8
	bl	cpu_push
	and	w0,w25,0xFF
	bl	cpu_push
	and	w0,w26,0xFF
	tbz	w26,FP_E,.p
	orr	w1,w0,0x10
	bic	w0,w0,0x10
	cmp	w11,0
	csel	w0,w1,w0,ne
.p:	bl	cpu_push
	orr	w26,w26,4
	bic	w26,w26,8
	and	w25,w25,0xFFFF
	tst	w26,0x100
	csel	w9,w10,w9,ne
	mov	w0,w9
	bl	cpu_read
	mov	w10,w0
	add	w0,w9,1
	and	w0,w0,0xFFFF
	bl	cpu_read
	orr	w0,w10,w0,lsl 8
	set_pc	w0
	strb	wzr,[x19,s_waiting]
	ldr	x30,[sp],16
	ret

; ---- memoire ----

rd16:					; (w10, w11) -> w0
	str	x30,[sp,-16]!
	mov	w0,w10
	bl	cpu_read
	str	w0,[sp,8]
	next_addr w0
	bl	cpu_read
	ldr	w1,[sp,8]
	orr	w0,w1,w0,lsl 8
	ldr	x30,[sp],16
	ret

wr16:					; (w10, w11), w0 = v
	str	x30,[sp,-16]!
	str	w0,[sp,8]
	mov	w1,w0
	mov	w0,w10
	bl	cpu_write
	next_addr w0
	ldr	w1,[sp,8]
	lsr	w1,w1,8
	bl	cpu_write
	ldr	x30,[sp],16
	ret

load_m:
	tbz	w26,FP_M,rd16
	mov	w0,w10
	b	cpu_read
load_x:
	tbz	w26,FP_X,rd16
	mov	w0,w10
	b	cpu_read
store_m:
	tbz	w26,FP_M,wr16
	mov	w1,w0
	mov	w0,w10
	b	cpu_write
store_x:
	tbz	w26,FP_X,wr16
	mov	w1,w0
	mov	w0,w10
	b	cpu_write
fetch_m:
	tbz	w26,FP_M,cpu_fetch16
	b	cpu_fetch
fetch_x:
	tbz	w26,FP_X,cpu_fetch16
	b	cpu_fetch

; ---- la page directe ----

; w0 = off -> w0 = dpAddr (off)
dp_addr:
	uxth	w1,w27
	tbz	w26,FP_E,.n
	tst	w1,0xFF
	b.ne	.n
	and	w0,w0,0xFF
	orr	w0,w0,w1
	ret
.n:	add	w0,w1,w0
	and	w0,w0,0xFFFF
	ret

; w0 = off -> w0 = dpPtr (off) (perd w9)
dp_ptr:
	str	x30,[sp,-16]!
	mov	w9,w0
	bl	dp_addr
	bl	cpu_read
	str	w0,[sp,8]
	add	w0,w9,1
	bl	dp_addr
	bl	cpu_read
	ldr	w1,[sp,8]
	orr	w0,w1,w0,lsl 8
	ldr	x30,[sp],16
	ret

; w0 = off -> w0 = dpPtrN (off), w9 = off (la banque est en off + 2)
dp_ptr_n:
	str	x30,[sp,-16]!
	mov	w9,w0
	uxth	w1,w27
	add	w0,w1,w9
	and	w0,w0,0xFFFF
	bl	cpu_read
	str	w0,[sp,8]
	uxth	w1,w27
	add	w0,w1,w9
	add	w0,w0,1
	and	w0,w0,0xFFFF
	bl	cpu_read
	ldr	w1,[sp,8]
	orr	w0,w1,w0,lsl 8
	ldr	x30,[sp],16
	ret

; ---- les modes d'adressage ----

m_01:					; (dp,X)
	str	x30,[sp,-16]!
	bl	cpu_fetch
	mov	w9,w0
	dp_io
	io_cycle
	uxth	w1,w27
	tbz	w26,FP_E,.n
	tst	w1,0xFF
	b.ne	.n
	add	w0,w9,w22
	and	w0,w0,0xFF
	and	w1,w1,0xFF00
	orr	w9,w1,w0
	b	.e
.n:	add	w0,w1,w9
	add	w0,w0,w22
	and	w9,w0,0xFFFF
.e:	mov	w0,w9
	bl	cpu_read
	mov	w10,w0
	tbz	w26,FP_E,.n2
	and	w1,w9,0xFF00
	add	w0,w9,1
	and	w0,w0,0xFF
	orr	w0,w0,w1
	b	.h
.n2:	add	w0,w9,1
	and	w0,w0,0xFFFF
.h:	bl	cpu_read
	orr	w10,w10,w0,lsl 8
	db_shl16 w1
	orr	w10,w10,w1
	mov	w11,0
	ldr	x30,[sp],16
	ret

m_03:					; sr,S
	str	x30,[sp,-16]!
	bl	cpu_fetch
	io_cycle
	add	w0,w0,w24
	and	w10,w0,0xFFFF
	mov	w11,1
	ldr	x30,[sp],16
	ret

m_05:					; dp
	str	x30,[sp,-16]!
	bl	cpu_fetch
	dp_io
	uxth	w1,w27
	add	w0,w0,w1
	and	w10,w0,0xFFFF
	mov	w11,1
	ldr	x30,[sp],16
	ret

m_07:					; [dp]
	str	x30,[sp,-16]!
	bl	cpu_fetch
	dp_io
	bl	dp_ptr_n
	mov	w10,w0
	uxth	w1,w27
	add	w0,w1,w9
	add	w0,w0,2
	and	w0,w0,0xFFFF
	bl	cpu_read
	orr	w10,w10,w0,lsl 16
	mov	w11,0
	ldr	x30,[sp],16
	ret

m_0D:					; abs
	str	x30,[sp,-16]!
	bl	cpu_fetch16
	db_shl16 w1
	orr	w10,w0,w1
	mov	w11,0
	ldr	x30,[sp],16
	ret

m_0F:					; long
	str	x30,[sp,-16]!
	bl	cpu_fetch16
	mov	w10,w0
	bl	cpu_fetch
	orr	w10,w10,w0,lsl 16
	mov	w11,0
	ldr	x30,[sp],16
	ret

; w10 = base, w12 = index, wr dans w13 -> w10 = a, w11 = 0
idx_end:
	add	w0,w10,w12
	and	w0,w0,0xFFFFFF
	cbnz	w13,.pen
	tbz	w26,FP_X,.pen
	eor	w1,w10,w0
	tst	w1,0xFF00
	b.eq	.np
.pen:	io_cycle
.np:	mov	w10,w0
	mov	w11,0
	ret

m_11w:	mov	w13,1				; (dp),Y
	b	m_11x
m_11:	mov	w13,0
m_11x:	str	x30,[sp,-16]!
	bl	cpu_fetch
	dp_io
	bl	dp_ptr
	db_shl16 w1
	orr	w10,w0,w1
	mov	w12,w23
	bl	idx_end
	ldr	x30,[sp],16
	ret

m_12:					; (dp)
	str	x30,[sp,-16]!
	bl	cpu_fetch
	dp_io
	bl	dp_ptr
	db_shl16 w1
	orr	w10,w0,w1
	mov	w11,0
	ldr	x30,[sp],16
	ret

m_13:					; (sr,S),Y
	str	x30,[sp,-16]!
	bl	cpu_fetch
	io_cycle
	add	w0,w0,w24
	and	w9,w0,0xFFFF
	mov	w0,w9
	bl	cpu_read
	mov	w10,w0
	add	w0,w9,1
	and	w0,w0,0xFFFF
	bl	cpu_read
	orr	w10,w10,w0,lsl 8
	io_cycle
	db_shl16 w1
	orr	w10,w10,w1
	add	w10,w10,w23
	and	w10,w10,0xFFFFFF
	mov	w11,0
	ldr	x30,[sp],16
	ret

m_15:					; dp,X
	str	x30,[sp,-16]!
	bl	cpu_fetch
	dp_io
	io_cycle
	add	w0,w0,w22
	bl	dp_addr
	mov	w10,w0
	mov	w11,1
	tbz	w26,FP_E,.r
	and	w11,w27,0xFF
.r:	ldr	x30,[sp],16
	ret

m_dpx:					; dp,X : le deuxieme octet toujours en banque 0
	str	x30,[sp,-16]!
	bl	cpu_fetch
	dp_io
	io_cycle
	add	w0,w0,w22
	bl	dp_addr
	mov	w10,w0
	mov	w11,1
	ldr	x30,[sp],16
	ret

m_dpy:
	str	x30,[sp,-16]!
	bl	cpu_fetch
	dp_io
	io_cycle
	add	w0,w0,w23
	bl	dp_addr
	mov	w10,w0
	mov	w11,1
	ldr	x30,[sp],16
	ret

m_17:					; [dp],Y
	str	x30,[sp,-16]!
	bl	cpu_fetch
	dp_io
	bl	dp_ptr_n
	mov	w10,w0
	uxth	w1,w27
	add	w0,w1,w9
	add	w0,w0,2
	and	w0,w0,0xFFFF
	bl	cpu_read
	orr	w10,w10,w0,lsl 16
	add	w10,w10,w23
	and	w10,w10,0xFFFFFF
	mov	w11,0
	ldr	x30,[sp],16
	ret

m_19w:	mov	w13,1				; abs,Y
	b	m_19x
m_19:	mov	w13,0
m_19x:	str	x30,[sp,-16]!
	bl	cpu_fetch16
	db_shl16 w1
	orr	w10,w0,w1
	mov	w12,w23
	bl	idx_end
	ldr	x30,[sp],16
	ret

m_1Dw:	mov	w13,1				; abs,X
	b	m_1Dx
m_1D:	mov	w13,0
m_1Dx:	str	x30,[sp,-16]!
	bl	cpu_fetch16
	db_shl16 w1
	orr	w10,w0,w1
	mov	w12,w22
	bl	idx_end
	ldr	x30,[sp],16
	ret

m_1F:					; long,X
	str	x30,[sp,-16]!
	bl	cpu_fetch16
	mov	w10,w0
	bl	cpu_fetch
	orr	w10,w10,w0,lsl 16
	add	w10,w10,w22
	and	w10,w10,0xFFFFFF
	mov	w11,0
	ldr	x30,[sp],16
	ret

; ---- UAL ----

do_ora:	bl	load_m
ora_v:	orr	w0,w21,w0
	b	set_a
do_and:	bl	load_m
and_v:	and	w0,w21,w0
	b	set_a
do_eor:	bl	load_m
eor_v:	eor	w0,w21,w0
	b	set_a
do_lda:	bl	load_m
set_a:	tbz	w26,FP_M,.w
	bfxil	w21,w0,0,8
	set_zn8	w0
	nxt
.w:	and	w21,w0,0xFFFF
	set_zn16 w21
	nxt
do_sta:	mov	w0,w21
	bl	store_m
	nxt
do_cmp:	bl	load_m
cmp_v:	mov	w1,w0
	mov	w0,w21
	tbnz	w26,FP_M,cmp8
cmp16:	cmp	w0,w1				; w0 = r, w1 = v
	cset	w3,hs
	set_c	w3
	sub	w0,w0,w1
	set_zn16 w0
	nxt
cmp8:	and	w0,w0,0xFF
	and	w1,w1,0xFF
	cmp	w0,w1
	cset	w3,hs
	set_c	w3
	sub	w0,w0,w1
	set_zn8	w0
	nxt
cmp_idx:				; w0 = r, w1 = v, selon x8
	tbnz	w26,FP_X,cmp8
	b	cmp16

do_adc:	bl	load_m
adc_v:	and	w1,w26,1			; c
	tbnz	w26,FP_M,.b8
	uxth	w2,w21				; a
	tbnz	w26,FP_D,.d16
	add	w3,w2,w0
	add	w3,w3,w1
.f16:	eor	w4,w2,w0
	mvn	w4,w4
	eor	w5,w2,w3
	and	w4,w4,w5
	ubfx	w4,w4,15,1
	bfi	w26,w4,6,1
	tbz	w26,FP_D,.c16
	movl	w5,0x9FFF
	cmp	w3,w5
	b.ls	.c16
	add	w3,w3,0x6000
.c16:	movl	w5,0xFFFF
	cmp	w3,w5
	cset	w4,hi
	set_c	w4
	and	w21,w3,0xFFFF
	set_zn16 w21
	nxt
.d16:	and	w3,w2,0xF			; r
	and	w4,w0,0xF
	add	w3,w3,w4
	add	w3,w3,w1
	cmp	w3,9
	b.ls	.a1
	add	w3,w3,6
.a1:	cmp	w3,0xF
	cset	w1,hi
	and	w3,w3,0xF
	and	w4,w2,0xF0
	and	w5,w0,0xF0
	add	w4,w4,w5
	add	w4,w4,w1,lsl 4
	add	w3,w3,w4
	cmp	w3,0x9F
	b.ls	.a2
	add	w3,w3,0x60
.a2:	cmp	w3,0xFF
	cset	w1,hi
	and	w3,w3,0xFF
	and	w4,w2,0xF00
	and	w5,w0,0xF00
	add	w4,w4,w5
	add	w4,w4,w1,lsl 8
	add	w3,w3,w4
	movl	w5,0x9FF
	cmp	w3,w5
	b.ls	.a3
	add	w3,w3,0x600
.a3:	mov	w5,0xFFF
	cmp	w3,w5
	cset	w1,hi
	and	w3,w3,0xFFF
	and	w4,w2,0xF000
	and	w5,w0,0xF000
	add	w4,w4,w5
	add	w4,w4,w1,lsl 12
	add	w3,w3,w4
	b	.f16
.b8:	and	w2,w21,0xFF			; a
	and	w0,w0,0xFF			; v
	tbnz	w26,FP_D,.d8
	add	w3,w2,w0
	add	w3,w3,w1
.f8:	eor	w4,w2,w0
	mvn	w4,w4
	eor	w5,w2,w3
	and	w4,w4,w5
	ubfx	w4,w4,7,1
	bfi	w26,w4,6,1
	tbz	w26,FP_D,.c8
	cmp	w3,0x9F
	b.ls	.c8
	add	w3,w3,0x60
.c8:	cmp	w3,0xFF
	cset	w4,hi
	set_c	w4
	bfxil	w21,w3,0,8
	set_zn8	w3
	nxt
.d8:	and	w3,w2,0xF
	and	w4,w0,0xF
	add	w3,w3,w4
	add	w3,w3,w1
	cmp	w3,9
	b.ls	.b1
	add	w3,w3,6
.b1:	cmp	w3,0xF
	cset	w1,hi
	and	w3,w3,0xF
	and	w4,w2,0xF0
	and	w5,w0,0xF0
	add	w4,w4,w5
	add	w4,w4,w1,lsl 4
	add	w3,w3,w4
	b	.f8

do_sbc:	bl	load_m
sbc_v:	and	w1,w26,1			; c
	tbnz	w26,FP_M,.b8
	uxth	w2,w21				; a
	mvn	w0,w0
	and	w0,w0,0xFFFF			; v
	tbnz	w26,FP_D,.d16
	add	w3,w2,w0
	add	w3,w3,w1
.f16:	eor	w4,w2,w0
	mvn	w4,w4
	eor	w5,w2,w3
	and	w4,w4,w5
	ubfx	w4,w4,15,1
	bfi	w26,w4,6,1
	tbz	w26,FP_D,.c16
	movl	w5,0xFFFF
	cmp	w3,w5
	b.gt	.c16
	sub	w3,w3,0x6000
.c16:	movl	w5,0xFFFF
	cmp	w3,w5
	cset	w4,gt
	set_c	w4
	and	w21,w3,0xFFFF
	set_zn16 w21
	nxt
.d16:	and	w3,w2,0xF			; r
	and	w4,w0,0xF
	add	w3,w3,w4
	add	w3,w3,w1
	cmp	w3,0xF
	b.gt	.a1
	sub	w3,w3,6
.a1:	cmp	w3,0xF
	cset	w1,gt
	and	w3,w3,0xF
	and	w4,w2,0xF0
	and	w5,w0,0xF0
	add	w4,w4,w5
	add	w4,w4,w1,lsl 4
	add	w3,w3,w4
	cmp	w3,0xFF
	b.gt	.a2
	sub	w3,w3,0x60
.a2:	cmp	w3,0xFF
	cset	w1,gt
	and	w3,w3,0xFF
	and	w4,w2,0xF00
	and	w5,w0,0xF00
	add	w4,w4,w5
	add	w4,w4,w1,lsl 8
	add	w3,w3,w4
	mov	w5,0xFFF
	cmp	w3,w5
	b.gt	.a3
	sub	w3,w3,0x600
.a3:	mov	w5,0xFFF
	cmp	w3,w5
	cset	w1,gt
	and	w3,w3,0xFFF
	and	w4,w2,0xF000
	and	w5,w0,0xF000
	add	w4,w4,w5
	add	w4,w4,w1,lsl 12
	add	w3,w3,w4
	b	.f16
.b8:	and	w2,w21,0xFF			; a
	mvn	w0,w0
	and	w0,w0,0xFF			; v
	tbnz	w26,FP_D,.d8
	add	w3,w2,w0
	add	w3,w3,w1
.f8:	eor	w4,w2,w0
	mvn	w4,w4
	eor	w5,w2,w3
	and	w4,w4,w5
	ubfx	w4,w4,7,1
	bfi	w26,w4,6,1
	tbz	w26,FP_D,.c8
	cmp	w3,0xFF
	b.gt	.c8
	sub	w3,w3,0x60
.c8:	cmp	w3,0xFF
	cset	w4,gt
	set_c	w4
	bfxil	w21,w3,0,8
	set_zn8	w3
	nxt
.d8:	and	w3,w2,0xF
	and	w4,w0,0xF
	add	w3,w3,w4
	add	w3,w3,w1
	cmp	w3,0xF
	b.gt	.b1
	sub	w3,w3,6
.b1:	cmp	w3,0xF
	cset	w1,gt
	and	w3,w3,0xF
	and	w4,w2,0xF0
	and	w5,w0,0xF0
	add	w4,w4,w5
	add	w4,w4,w1,lsl 4
	add	w3,w3,w4
	b	.f8

; w0 = v : BIT (memoire), puis Z
bit_mem:
	tbnz	w26,FP_M,.b8
	ubfx	w1,w0,14,2
	b	.s
.b8:	ubfx	w1,w0,6,2
.s:	bfi	w26,w1,6,2
bit_imm:
	tbnz	w26,FP_M,.b8
	tst	w21,w0
	b	.z
.b8:	and	w1,w21,0xFF
	tst	w1,w0
.z:	cset	w1,eq
	bfi	w26,w1,1,1
	nxt

; ---- lecture-modification-ecriture : w0 -> w0, perd w1-w3, w16, w17 ----

alt_asl:
	tbnz	w26,FP_M,.b8
	ubfx	w1,w0,15,1
	set_c	w1
	lsl	w0,w0,1
	and	w0,w0,0xFFFF
	b	alt_zn
.b8:	ubfx	w1,w0,7,1
	set_c	w1
	lsl	w0,w0,1
	and	w0,w0,0xFF
	b	alt_zn
alt_rol:
	and	w2,w26,1
	tbnz	w26,FP_M,.b8
	ubfx	w1,w0,15,1
	set_c	w1
	orr	w0,w2,w0,lsl 1
	and	w0,w0,0xFFFF
	b	alt_zn
.b8:	ubfx	w1,w0,7,1
	set_c	w1
	orr	w0,w2,w0,lsl 1
	and	w0,w0,0xFF
	b	alt_zn
alt_lsr:
	and	w1,w0,1
	set_c	w1
	lsr	w0,w0,1
	b	alt_zn
alt_ror:
	and	w2,w26,1
	and	w1,w0,1
	set_c	w1
	lsr	w0,w0,1
	tbnz	w26,FP_M,.b8
	orr	w0,w0,w2,lsl 15
	b	alt_zn
.b8:	orr	w0,w0,w2,lsl 7
	b	alt_zn
alt_inc:
	add	w0,w0,1
	b	alt_mask
alt_dec:
	sub	w0,w0,1
alt_mask:
	tbnz	w26,FP_M,.b8
	and	w0,w0,0xFFFF
	b	alt_zn
.b8:	and	w0,w0,0xFF
alt_zn:	tbnz	w26,FP_M,.b8
	set_zn16 w0
	ret
.b8:	set_zn8	w0
	ret
alt_tsb:
	tbnz	w26,FP_M,.b8
	tst	w21,w0
	cset	w1,eq
	bfi	w26,w1,1,1
	orr	w0,w0,w21
	ret
.b8:	and	w2,w21,0xFF
	tst	w2,w0
	cset	w1,eq
	bfi	w26,w1,1,1
	orr	w0,w0,w2
	ret
alt_trb:
	tbnz	w26,FP_M,.b8
	tst	w21,w0
	cset	w1,eq
	bfi	w26,w1,1,1
	bic	w0,w0,w21
	ret
.b8:	and	w2,w21,0xFF
	tst	w2,w0
	cset	w1,eq
	bfi	w26,w1,1,1
	bic	w0,w0,w2
	ret

; (w10, w11), x13 = l'operation
do_rmw:
	tbz	w26,FP_M,.w
	mov	w0,w10
	bl	cpu_read
	io_cycle
	blr	x13
	mov	w1,w0
	mov	w0,w10
	bl	cpu_write
	nxt
.w:	bl	rd16
	io_cycle
	blr	x13
	mov	w12,w0
	next_addr w0
	lsr	w1,w12,8
	bl	cpu_write
	mov	w0,w10
	mov	w1,w12
	bl	cpu_write
	nxt

do_rmwa:
	io_cycle
	tbz	w26,FP_M,.w
	and	w0,w21,0xFF
	blr	x13
	bfxil	w21,w0,0,8
	nxt
.w:	mov	w0,w21
	blr	x13
	mov	w21,w0
	nxt

macro rmw4 p0,p1,p2,p3, fn {
o#p0:	bl	m_05
	adr	x13,fn
	b	do_rmw
o#p1:	bl	m_0D
	adr	x13,fn
	b	do_rmw
o#p2:	bl	m_dpx
	adr	x13,fn
	b	do_rmw
o#p3:	bl	m_1Dw
	adr	x13,fn
	b	do_rmw
}
macro rmwa_op a, fn {
o#a:	adr	x13,fn
	b	do_rmwa
}

; ---- les codes de la UAL ----

macro alu_row r0,r1, fn, imm {
o#r0#1:	bl	m_01
	b	fn
o#r0#3:	bl	m_03
	b	fn
o#r0#5:	bl	m_05
	b	fn
o#r0#7:	bl	m_07
	b	fn
o#r0#9:	bl	fetch_m
	b	imm
o#r0#D:	bl	m_0D
	b	fn
o#r0#F:	bl	m_0F
	b	fn
o#r1#1:	bl	m_11
	b	fn
o#r1#2:	bl	m_12
	b	fn
o#r1#3:	bl	m_13
	b	fn
o#r1#5:	bl	m_15
	b	fn
o#r1#7:	bl	m_17
	b	fn
o#r1#9:	bl	m_19
	b	fn
o#r1#D:	bl	m_1D
	b	fn
o#r1#F:	bl	m_1F
	b	fn
}

alu_row 0,1, do_ora, ora_v
alu_row 2,3, do_and, and_v
alu_row 4,5, do_eor, eor_v
alu_row 6,7, do_adc, adc_v
alu_row A,B, do_lda, set_a
alu_row C,D, do_cmp, cmp_v
alu_row E,F, do_sbc, sbc_v

o81:	bl	m_01
	b	do_sta
o83:	bl	m_03
	b	do_sta
o85:	bl	m_05
	b	do_sta
o87:	bl	m_07
	b	do_sta
o89:	bl	fetch_m				; BIT #
	b	bit_imm
o8D:	bl	m_0D
	b	do_sta
o8F:	bl	m_0F
	b	do_sta
o91:	bl	m_11w
	b	do_sta
o92:	bl	m_12
	b	do_sta
o93:	bl	m_13
	b	do_sta
o95:	bl	m_15
	b	do_sta
o97:	bl	m_17
	b	do_sta
o99:	bl	m_19w
	b	do_sta
o9D:	bl	m_1Dw
	b	do_sta
o9F:	bl	m_1F
	b	do_sta

; ---- RMW ----

rmw4 06,0E,16,1E, alt_asl
rmw4 26,2E,36,3E, alt_rol
rmw4 46,4E,56,5E, alt_lsr
rmw4 66,6E,76,7E, alt_ror
rmw4 E6,EE,F6,FE, alt_inc
rmw4 C6,CE,D6,DE, alt_dec
o04:	bl	m_05
	adr	x13,alt_tsb
	b	do_rmw
o0C:	bl	m_0D
	adr	x13,alt_tsb
	b	do_rmw
o14:	bl	m_05
	adr	x13,alt_trb
	b	do_rmw
o1C:	bl	m_0D
	adr	x13,alt_trb
	b	do_rmw
rmwa_op 0A, alt_asl
rmwa_op 2A, alt_rol
rmwa_op 4A, alt_lsr
rmwa_op 6A, alt_ror
rmwa_op 1A, alt_inc
rmwa_op 3A, alt_dec

set_x:	tbz	w26,FP_X,.w			; w0 = v
	and	w22,w0,0xFF
	set_zn8	w22
	nxt
.w:	and	w22,w0,0xFFFF
	set_zn16 w22
	nxt
set_y:	tbz	w26,FP_X,.w
	and	w23,w0,0xFF
	set_zn8	w23
	nxt
.w:	and	w23,w0,0xFFFF
	set_zn16 w23
	nxt

oE8:	io_cycle
	add	w0,w22,1
	b	set_x
oC8:	io_cycle
	add	w0,w23,1
	b	set_y
oCA:	io_cycle
	sub	w0,w22,1
	b	set_x
o88:	io_cycle
	sub	w0,w23,1
	b	set_y

; ---- index, STZ, BIT ----

oA2:	bl	fetch_x
	b	set_x
oA6:	bl	m_05
	bl	load_x
	b	set_x
oB6:	bl	m_dpy
	bl	load_x
	b	set_x
oAE:	bl	m_0D
	bl	load_x
	b	set_x
oBE:	bl	m_19
	bl	load_x
	b	set_x
oA0:	bl	fetch_x
	b	set_y
oA4:	bl	m_05
	bl	load_x
	b	set_y
oB4:	bl	m_dpx
	bl	load_x
	b	set_y
oAC:	bl	m_0D
	bl	load_x
	b	set_y
oBC:	bl	m_1D
	bl	load_x
	b	set_y
o86:	bl	m_05
	b	stx_op
o96:	bl	m_dpy
	b	stx_op
o8E:	bl	m_0D
stx_op:	mov	w0,w22
	bl	store_x
	nxt
o84:	bl	m_05
	b	sty_op
o94:	bl	m_dpx
	b	sty_op
o8C:	bl	m_0D
sty_op:	mov	w0,w23
	bl	store_x
	nxt
oE0:	bl	fetch_x
	b	cpx_op
oE4:	bl	m_05
	b	cpx_m
oEC:	bl	m_0D
cpx_m:	bl	load_x
cpx_op:	mov	w1,w0
	mov	w0,w22
	b	cmp_idx
oC0:	bl	fetch_x
	b	cpy_op
oC4:	bl	m_05
	b	cpy_m
oCC:	bl	m_0D
cpy_m:	bl	load_x
cpy_op:	mov	w1,w0
	mov	w0,w23
	b	cmp_idx
o64:	bl	m_05
	b	stz_op
o74:	bl	m_dpx
	b	stz_op
o9C:	bl	m_0D
	b	stz_op
o9E:	bl	m_1Dw
stz_op:	mov	w0,0
	bl	store_m
	nxt
o24:	bl	m_05
	b	bit_op
o2C:	bl	m_0D
	b	bit_op
o34:	bl	m_dpx
	b	bit_op
o3C:	bl	m_1D
bit_op:	bl	load_m
	b	bit_mem

; ---- sauts ----

br_not:	bl	cpu_fetch
	nxt
br_take:
	bl	cpu_fetch
	sxtb	w0,w0
	add	w1,w25,w0
	and	w1,w1,0xFFFF			; t
	io_cycle
	tbz	w26,FP_E,.s
	eor	w2,w1,w25
	tst	w2,0xFF00
	b.eq	.s
	io_cycle
.s:	set_pc	w1
	nxt

o10:	tbnz	w26,FP_N,br_not
	b	br_take
o30:	tbz	w26,FP_N,br_not
	b	br_take
o50:	tbnz	w26,FP_V,br_not
	b	br_take
o70:	tbz	w26,FP_V,br_not
	b	br_take
o90:	tbnz	w26,FP_C,br_not
	b	br_take
oB0:	tbz	w26,FP_C,br_not
	b	br_take
oD0:	tbnz	w26,FP_Z,br_not
	b	br_take
oF0:	tbz	w26,FP_Z,br_not
	b	br_take
o80:	b	br_take
o82:	bl	cpu_fetch16
	io_cycle
	add	w0,w25,w0
	set_pc	w0
	nxt
o4C:	bl	cpu_fetch16
	set_pc	w0
	nxt
o5C:	bl	cpu_fetch16
	mov	w10,w0
	bl	cpu_fetch
	set_pb	w0
	set_pc	w10
	nxt

; w10 = a (banque comprise), w11 = l'adresse + 1 -> w0 = le mot
rd_pair:
	str	x30,[sp,-16]!
	mov	w0,w10
	bl	cpu_read
	mov	w10,w0
	mov	w0,w11
	bl	cpu_read
	orr	w0,w10,w0,lsl 8
	ldr	x30,[sp],16
	ret

o6C:	bl	cpu_fetch16
	mov	w10,w0
	add	w11,w0,1
	and	w11,w11,0xFFFF
	bl	rd_pair
	set_pc	w0
	nxt
o7C:	bl	cpu_fetch16
	io_cycle
	add	w0,w0,w22
	and	w0,w0,0xFFFF			; p
pc_tbl:	and	w1,w25,0xFF0000
	orr	w10,w1,w0
	add	w0,w0,1
	and	w0,w0,0xFFFF
	orr	w11,w1,w0
	bl	rd_pair
	set_pc	w0
	nxt
oDC:	bl	cpu_fetch16
	mov	w9,w0
	mov	w10,w0
	add	w11,w0,1
	and	w11,w11,0xFFFF
	bl	rd_pair
	mov	w12,w0
	add	w0,w9,2
	and	w0,w0,0xFFFF
	bl	cpu_read
	set_pb	w0
	set_pc	w12
	nxt
o20:	bl	cpu_fetch16
	mov	w9,w0
	io_cycle
	sub	w10,w25,1
	ubfx	w0,w10,8,8
	bl	cpu_push
	and	w0,w10,0xFF
	bl	cpu_push
	set_pc	w9
	nxt
oFC:	bl	cpu_fetch
	mov	w9,w0				; l
	ubfx	w0,w25,8,8
	bl	cpu_push_n
	and	w0,w25,0xFF
	bl	cpu_push_n
	bl	cpu_fetch			; h
	io_cycle
	orr	w0,w9,w0,lsl 8
	add	w0,w0,w22
	and	w0,w0,0xFFFF
	and	w1,w25,0xFF0000
	orr	w10,w1,w0
	add	w0,w0,1
	and	w0,w0,0xFFFF
	orr	w11,w1,w0
	bl	rd_pair
	set_pc	w0
	fix_e
	nxt
o22:	bl	cpu_fetch16
	mov	w9,w0				; a
	lsr	w0,w25,16
	bl	cpu_push_n
	io_cycle
	bl	cpu_fetch
	mov	w10,w0				; b
	sub	w11,w25,1
	ubfx	w0,w11,8,8
	bl	cpu_push_n
	and	w0,w11,0xFF
	bl	cpu_push_n
	set_pb	w10
	set_pc	w9
	fix_e
	nxt
o60:	add	x20,x20,12
	bl	cpu_pull
	mov	w9,w0
	bl	cpu_pull
	orr	w0,w9,w0,lsl 8
	add	w0,w0,1
	set_pc	w0
	io_cycle
	nxt
o6B:	add	x20,x20,12
	bl	cpu_pull_n
	mov	w9,w0
	bl	cpu_pull_n
	orr	w9,w9,w0,lsl 8
	bl	cpu_pull_n
	set_pb	w0
	add	w0,w9,1
	set_pc	w0
	fix_e
	nxt
o40:	add	x20,x20,12
	bl	cpu_pull
	bl	set_p
	bl	cpu_pull
	mov	w9,w0
	bl	cpu_pull
	orr	w0,w9,w0,lsl 8
	set_pc	w0
	tbnz	w26,FP_E,.c
	bl	cpu_pull
	set_pb	w0
.c:	nxt
o00:	bl	cpu_fetch
	mov	w9,0xFFE6
	mov	w10,0xFFFE
	mov	w11,1
	bl	cpu_interrupt
	nxt
o02:	bl	cpu_fetch
	mov	w9,0xFFE4
	mov	w10,0xFFF4
	mov	w11,1
	bl	cpu_interrupt
	nxt

; ---- la pile ----

o08:	io_cycle
	and	w0,w26,0xFF
	bl	cpu_push
	nxt
o28:	add	x20,x20,12
	bl	cpu_pull
	bl	set_p
	nxt
; w0 = v, b8 : le bit de P
push_m:	str	x30,[sp,-16]!
	mov	w9,w0
	tbnz	w26,FP_M,.l
	ubfx	w0,w9,8,8
	bl	cpu_push
.l:	and	w0,w9,0xFF
	bl	cpu_push
	ldr	x30,[sp],16
	ret
push_x:	str	x30,[sp,-16]!
	mov	w9,w0
	tbnz	w26,FP_X,.l
	ubfx	w0,w9,8,8
	bl	cpu_push
.l:	and	w0,w9,0xFF
	bl	cpu_push
	ldr	x30,[sp],16
	ret
pull_w:	str	x30,[sp,-16]!
	bl	cpu_pull
	mov	w9,w0
	bl	cpu_pull
	orr	w0,w9,w0,lsl 8
	ldr	x30,[sp],16
	ret
o48:	io_cycle
	mov	w0,w21
	bl	push_m
	nxt
o68:	add	x20,x20,12
	tbz	w26,FP_M,.w
	bl	cpu_pull
	b	set_a
.w:	bl	pull_w
	b	set_a
oDA:	io_cycle
	mov	w0,w22
	bl	push_x
	nxt
oFA:	add	x20,x20,12
	tbz	w26,FP_X,.w
	bl	cpu_pull
	b	set_x
.w:	bl	pull_w
	b	set_x
o5A:	io_cycle
	mov	w0,w23
	bl	push_x
	nxt
o7A:	add	x20,x20,12
	tbz	w26,FP_X,.w
	bl	cpu_pull
	b	set_y
.w:	bl	pull_w
	b	set_y
o8B:	io_cycle
	ubfx	w0,w27,16,8
	bl	cpu_push
	nxt
oAB:	add	x20,x20,12
	bl	cpu_pull_n
	bfi	w27,w0,16,8
	set_zn8	w0
	fix_e
	nxt
o4B:	io_cycle
	lsr	w0,w25,16
	bl	cpu_push
	nxt
o0B:	io_cycle
	ubfx	w0,w27,8,8
	bl	cpu_push_n
	and	w0,w27,0xFF
	bl	cpu_push_n
	fix_e
	nxt
o2B:	add	x20,x20,12
	bl	cpu_pull_n
	mov	w9,w0
	bl	cpu_pull_n
	orr	w0,w9,w0,lsl 8
	bfxil	w27,w0,0,16
	set_zn16 w0
	fix_e
	nxt
oF4:	bl	cpu_fetch16
push_w:	mov	w9,w0
	ubfx	w0,w9,8,8
	bl	cpu_push_n
	and	w0,w9,0xFF
	bl	cpu_push_n
	fix_e
	nxt
oD4:	bl	cpu_fetch
	dp_io
	bl	dp_ptr_n
	b	push_w
o62:	bl	cpu_fetch16
	io_cycle
	add	w0,w25,w0
	and	w0,w0,0xFFFF
	b	push_w

; ---- transferts ----

oAA:	io_cycle
	mov	w0,w21
	b	set_x
oA8:	io_cycle
	mov	w0,w21
	b	set_y
o8A:	io_cycle
	mov	w0,w22
	b	set_a
o98:	io_cycle
	mov	w0,w23
	b	set_a
o9B:	io_cycle
	mov	w0,w22
	b	set_y
oBB:	io_cycle
	mov	w0,w23
	b	set_x
oBA:	io_cycle
	mov	w0,w24
	b	set_x
o9A:	io_cycle
	mov	w0,w22
	b	tcs_op
o1B:	io_cycle
	mov	w0,w21
tcs_op:	mov	w24,w0
	tbz	w26,FP_E,.n
	and	w24,w0,0xFF
	orr	w24,w24,0x100
.n:	nxt
o3B:	io_cycle
	mov	w21,w24
	set_zn16 w21
	nxt
o5B:	io_cycle
	bfxil	w27,w21,0,16
	set_zn16 w21
	nxt
o7B:	io_cycle
	uxth	w21,w27
	set_zn16 w21
	nxt
oEB:	add	x20,x20,12
	rev16	w21,w21
	set_zn8	w21
	nxt

; ---- drapeaux, modes ----

o18:	io_cycle
	bic	w26,w26,1
	nxt
o38:	io_cycle
	orr	w26,w26,1
	nxt
o58:	io_cycle
	bic	w26,w26,4
	nxt
o78:	io_cycle
	orr	w26,w26,4
	nxt
oD8:	io_cycle
	bic	w26,w26,8
	nxt
oF8:	io_cycle
	orr	w26,w26,8
	nxt
oB8:	io_cycle
	bic	w26,w26,0x40
	nxt
oC2:	bl	cpu_fetch
	io_cycle
	bic	w0,w26,w0
	and	w0,w0,0xFF
	bl	set_p
	nxt
oE2:	bl	cpu_fetch
	io_cycle
	orr	w0,w26,w0
	and	w0,w0,0xFF
	bl	set_p
	nxt
oFB:	io_cycle
	and	w0,w26,1			; c
	ubfx	w1,w26,FP_E,1
	bfi	w26,w0,FP_E,1
	set_c	w1
	cbz	w0,.n
	orr	w26,w26,0x30
	and	w22,w22,0xFF
	and	w23,w23,0xFF
	and	w24,w24,0xFF
	orr	w24,w24,0x100
.n:	nxt

; ---- blocs ----

o54:	mov	w12,1
	b	blk_op
o44:	mov	w12,-1
blk_op:	bl	cpu_fetch
	mov	w9,w0				; dst
	bl	cpu_fetch			; src
	bfi	w27,w9,16,8
	orr	w0,w22,w0,lsl 16
	bl	cpu_read
	mov	w1,w0
	orr	w0,w23,w9,lsl 16
	bl	cpu_write
	add	x20,x20,12
	add	w22,w22,w12
	add	w23,w23,w12
	and	w22,w22,0xFFFF
	and	w23,w23,0xFFFF
	tbz	w26,FP_X,.a
	and	w22,w22,0xFF
	and	w23,w23,0xFF
.a:	sub	w21,w21,1
	and	w21,w21,0xFFFF
	movl	w0,0xFFFF
	cmp	w21,w0
	b.eq	.f
	sub	w0,w25,3
	set_pc	w0
.f:	nxt

; ---- le reste ----

oEA:	io_cycle
	nxt
o42:	bl	cpu_fetch
	nxt
oCB:	add	x20,x20,12
	mov	w0,1
	strb	w0,[x19,s_waiting]
	nxt
oDB:	add	x20,x20,12
	mov	w0,1
	strb	w0,[x19,s_stopped]
	nxt

cpu_optab:
irp a, 0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F {
  irp b, 0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F \{
	dw	o#a\#b-cpu_optab
  \}
}
