; ============================================================================
;  n64_jit.inc - le recompilateur R4300 vers AArch64 : le code MIPS en RDRAM,
;  traduit un bloc a la fois (64 instructions au plus), des blocs chaines.
;
;    * Rien de lourd dans un bloc : ce qui sort du chemin rapide (hors kseg0 /
;      kseg1 ou de la RDRAM, mal aligne, debordement, COP0, page de code ecrite,
;      instruction non traduite) est une SORTIE avant l'instruction, avec l'etat
;      exact de l'interprete : step la fait, puis on revient aux blocs.
;    * Les cycles sont ceux de l'interprete, a l'instruction pres : un bloc ne
;      part que s'il tient avant le prochain evenement.
;    * Dans un bloc : x19 = m, x20 = cycles, 15 registres MIPS dans des registres
;      A64 (jit_pin), x7 = RDRAM, x6 = pages de code, x5 = le branchement en
;      cours ; x0-x4, x16, x17 de passage.
;
;  Pendant une traduction : x21 = la prochaine case de code, x28 = l'etat
;  (J_*), w22 = op, w23 rs, w24 rt, w25 rd, w26 sa. emit3 et les petites
;  fonctions perdent x0-x3, x16, x17 et gardent x4-x15.
; ============================================================================

JP		= 8
JIT_CODE_SIZE	= 48 shl 20
MAX_BLOCKS	= 1 shl 19
MAX_INS		= 64
MAX_WORST	= 3000
MAX_LINKS	= 1 shl 20
MAX_FIX		= 24

virtual at 0
J_nst:		rb 4
J_nex:		rb 4
J_cur:		rb 4
J_at:		rb 4
J_sofar:	rb 4
J_slot:		rb 4
J_fpChecked:	rb 4
J_fr:		rb 4
J_extra:	rb 4
J_slotKind:	rb 4
J_taken:	rb 4
J_fall:		rb 4
J_va:		rb 4
J_pa:		rb 4
J_ex:		rb 8*16
J_st:		rb (MAX_INS*2)*ST_SIZE
J_end:
end virtual
ST_at		= 0
ST_sofar	= 4
ST_slot		= 8
ST_nfix		= 12
ST_fix		= 16
ST_SIZE		= 16+MAX_FIX*8
M_JST		= 0x19000
assert M_JST+J_end <= M_SRAM

CC_EQ		= 0
CC_NE		= 1
CC_HS		= 2
CC_LO		= 3
CC_MI		= 4
CC_PL		= 5
CC_VS		= 6
CC_VC		= 7
CC_HI		= 8
CC_LS		= 9
CC_GE		= 10
CC_LT		= 11
CC_GT		= 12
CC_LE		= 13
RZ		= 31

; le registre A64 d'un registre MIPS, 0xFF : en memoire
jit_pin:
	db	0xFF,0xFF,23,24,25,26,9,10,11,12,13,14,15,0xFF,0xFF,0xFF
	db	27,28,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,21,0xFF,22
jit_pins:
	db	2,3,4,5,6,7,8,9,10,11,12,16,17,29,31,0xFF

macro E val {				; un mot constant
	movl	w16,val
	str	w16,[x21],4
}
macro Ew reg {
	str	reg,[x21],4
}

; w0 = base, w1 = d, w2 = n, w3 = m
emit3:	orr	w0,w0,w3,lsl 16
	orr	w0,w0,w2,lsl 5
	orr	w0,w0,w1
	str	w0,[x21],4
	ret

macro op3 base, d, n, m {
	mov	w3,m
	mov	w2,n
	mov	w1,d
	movl	w0,base
	bl	emit3
}
macro a_ADD d,n,m {
	op3	0x8B000000,d,n,m
}
macro a_ADDW d,n,m {
	op3	0x0B000000,d,n,m
}
macro a_SUB d,n,m {
	op3	0xCB000000,d,n,m
}
macro a_SUBW d,n,m {
	op3	0x4B000000,d,n,m
}
macro a_AND d,n,m {
	op3	0x8A000000,d,n,m
}
macro a_ANDW d,n,m {
	op3	0x0A000000,d,n,m
}
macro a_ORR d,n,m {
	op3	0xAA000000,d,n,m
}
macro a_EOR d,n,m {
	op3	0xCA000000,d,n,m
}
macro a_MOV d,m {			; x
	op3	0xAA000000,d,RZ,m
}
macro a_MOVW d,m {
	op3	0x2A000000,d,RZ,m
}
macro a_CMP n,m {
	op3	0xEB000000,RZ,n,m
}
macro a_CMPW n,m {
	op3	0x6B000000,RZ,n,m
}

; w0 = d, w1 = n, w2 = imm (12 bits), w3 = base
emit_i:	orr	w3,w3,w2,lsl 10
	orr	w3,w3,w1,lsl 5
	orr	w3,w3,w0
	str	w3,[x21],4
	ret
macro a_ADDI d,n,imm {
	mov	w2,imm
	mov	w0,d
	mov	w1,n
	movl	w3,0x91000000
	bl	emit_i
}
macro a_ADDIW d,n,imm {
	mov	w2,imm
	mov	w0,d
	mov	w1,n
	movl	w3,0x11000000
	bl	emit_i
}
macro a_SUBIW d,n,imm {
	mov	w2,imm
	mov	w0,d
	mov	w1,n
	movl	w3,0x51000000
	bl	emit_i
}

; UBFM / SBFM : w0 = base, w1 = d, w2 = n, w3 = r | s shl 8
emit_bf:
	and	w16,w3,63
	orr	w0,w0,w16,lsl 16
	lsr	w16,w3,8
	orr	w0,w0,w16,lsl 10
	orr	w0,w0,w2,lsl 5
	orr	w0,w0,w1
	str	w0,[x21],4
	ret
macro a_SXTW d,n {			; sbfm x, x, 0, 31
	movl	w0,0x93400000
	mov	w1,d
	mov	w2,n
	mov	w3,31 shl 8
	bl	emit_bf
}
macro a_ASR d,n,sa {			; sbfm x, x, sa, 63
	movl	w0,0x93400000
	mov	w1,d
	mov	w2,n
	mov	w3,sa
	orr	w3,w3,63 shl 8
	bl	emit_bf
}
macro a_LSR d,n,sa {
	movl	w0,0xD3400000
	mov	w1,d
	mov	w2,n
	mov	w3,sa
	orr	w3,w3,63 shl 8
	bl	emit_bf
}
macro a_LSRW d,n,sa {
	movl	w0,0x53000000
	mov	w1,d
	mov	w2,n
	mov	w3,sa
	orr	w3,w3,31 shl 8
	bl	emit_bf
}

; w0 = d, w1 = la valeur 32 bits
a_mov32:
	and	w2,w1,0xFFFF
	lsl	w2,w2,5
	orr	w2,w2,w0
	movl	w3,0x52800000
	orr	w2,w2,w3
	str	w2,[x21],4
	lsr	w1,w1,16
	cbz	w1,.r
	lsl	w1,w1,5
	orr	w1,w1,w0
	movl	w3,0x72A00000
	orr	w1,w1,w3
	str	w1,[x21],4
.r:	ret

; w0 = d, w1 = la valeur signee -> x d
a_movs:
	tbz	w1,31,a_mov32
	mvn	w2,w1
	and	w2,w2,0xFFFF
	lsl	w2,w2,5
	orr	w2,w2,w0
	movl	w3,0x92800000
	orr	w2,w2,w3
	str	w2,[x21],4
	lsr	w1,w1,16
	mov	w2,0xFFFF
	cmp	w1,w2
	b.eq	.r
	lsl	w1,w1,5
	orr	w1,w1,w0
	movl	w3,0xF2A00000
	orr	w1,w1,w3
	str	w1,[x21],4
.r:	ret

; w0 = t, w1 = n, w2 = off (octets), w3 = base : LDR / STR (x, w, d, s, x sur 8 ou 4)
emit_ls:
	mov	w16,w3
	tbnz	w3,30,.x			; (bit 30 : 64 bits)
	lsr	w2,w2,2
	b	.e
.x:	lsr	w2,w2,3
.e:	orr	w16,w16,w2,lsl 10
	orr	w16,w16,w1,lsl 5
	orr	w16,w16,w0
	str	w16,[x21],4
	ret
macro a_LDRX t,n,off {
	mov	w2,off
	mov	w0,t
	mov	w1,n
	movl	w3,0xF9400000
	bl	emit_ls
}
macro a_STRX t,n,off {
	mov	w2,off
	mov	w0,t
	mov	w1,n
	movl	w3,0xF9000000
	bl	emit_ls
}
macro a_LDRW t,n,off {
	mov	w2,off
	mov	w0,t
	mov	w1,n
	movl	w3,0xB9400000
	bl	emit_ls
}
macro a_STRW t,n,off {
	mov	w2,off
	mov	w0,t
	mov	w1,n
	movl	w3,0xB9000000
	bl	emit_ls
}
macro a_LDRS t,off {
	mov	w2,off
	mov	w0,t
	mov	w1,19
	movl	w3,0xBD400000
	bl	emit_ls
}
macro a_STRS t,off {
	mov	w2,off
	mov	w0,t
	mov	w1,19
	movl	w3,0xBD000000
	bl	emit_ls
}
macro a_LDRD t,off {
	mov	w2,off
	mov	w0,t
	mov	w1,19
	movl	w3,0xFD400000
	bl	emit_ls
}
macro a_STRD t,off {
	mov	w2,off
	mov	w0,t
	mov	w1,19
	movl	w3,0xFD000000
	bl	emit_ls
}
macro a_STRBm t,off {			; strb t, [x19, off]
	mov	w0,t
	lsl	w0,w0,0
	mov	w1,off
	lsl	w1,w1,10
	orr	w0,w0,w1
	movl	w1,0x39000000 or (19 shl 5)
	orr	w0,w0,w1
	str	w0,[x21],4
}
; [xn, xm]
macro a_LSX base,t,n,m {
	mov	w3,m
	mov	w2,n
	mov	w1,t
	movl	w0,base or 0x6800
	bl	emit3
}
R_LDRB		= 0x38600000
R_LDRSBX	= 0x38A00000
R_LDRH		= 0x78600000
R_LDRSHX	= 0x78A00000
R_LDRW		= 0xB8600000
R_LDRSW		= 0xB8A00000
R_LDRX		= 0xF8600000
R_STRB		= 0x38200000
R_STRH		= 0x78200000
R_STRW		= 0xB8200000
R_STRX		= 0xF8200000

; w0 = d, w1 = n, w2 = imm : w d = w n + imm (n peut etre RZ)
add_imm32:
	cmp	w1,RZ
	b.ne	.a
	mov	w1,w2
	b	a_mov32
.a:	mov	w3,4096
	cmp	w2,w3
	b.hs	.b
	movl	w3,0x11000000
	b	emit_i
.b:	neg	w4,w2
	cmp	w2,0
	b.ge	.c
	cmp	w4,w3
	b.hs	.c
	mov	w2,w4
	movl	w3,0x51000000
	b	emit_i
.c:	str	x30,[sp,-16]!
	stp	x0,x1,[sp,-16]!
	mov	w0,1
	mov	w1,w2
	bl	a_mov32
	ldp	x1,x2,[sp],16
	mov	w3,1
	movl	w0,0x0B000000
	ldr	x30,[sp],16
	b	emit3

; le stub de l'instruction en cours -> x0 ; x1 = le site
side_ref:
	ldr	w0,[x28,J_cur]
	tbz	w0,31,.h
	ldr	w0,[x28,J_nst]
	add	w2,w0,1
	str	w2,[x28,J_nst]
	str	w0,[x28,J_cur]
	mov	w2,ST_SIZE
	mul	w2,w2,w0
	add	x2,x28,x2
	add	x2,x2,J_st
	ldr	w3,[x28,J_at]
	str	w3,[x2,ST_at]
	ldr	w3,[x28,J_sofar]
	str	w3,[x2,ST_sofar]
	ldr	w3,[x28,J_slot]
	str	w3,[x2,ST_slot]
	str	wzr,[x2,ST_nfix]
.h:	mov	w2,ST_SIZE
	mul	w2,w2,w0
	add	x2,x28,x2
	add	x2,x2,J_st
	ldr	w3,[x2,ST_nfix]
	cmp	w3,MAX_FIX
	b.hs	.r
	add	x0,x2,ST_fix
	str	x21,[x0,x3,lsl 3]
	add	w3,w3,1
	str	w3,[x2,ST_nfix]
.r:	ret

; b.cc vers la sortie de cette instruction : w0 = cc
side_if:
	mov	w4,w0
	mov	x5,x30
	bl	side_ref
	movl	w0,0x54000000
	orr	w0,w0,w4
	str	w0,[x21],4
	br	x5
side_cbnz:				; w0 = t
	mov	w4,w0
	mov	x5,x30
	bl	side_ref
	movl	w0,0x35000000
	orr	w0,w0,w4
	str	w0,[x21],4
	br	x5
side_always:
	mov	w0,14
	b	side_if

; w0 = g, w1 = tmp -> w0 : le registre A64 qui le tient
j_rd:	cbnz	w0,.a
	mov	w0,RZ
	ret
.a:	adr	x2,jit_pin
	ldrb	w3,[x2,x0]
	cmp	w3,0xFF
	b.eq	.m
	mov	w0,w3
	ret
.m:	mov	w2,w0
	mov	w4,w1
	lsl	w2,w2,3
	mov	w0,w1
	mov	w1,19
	movl	w3,0xF9400000
	str	x30,[sp,-16]!
	bl	emit_ls
	ldr	x30,[sp],16
	mov	w0,w4
	ret

; w0 = g, w1 = tmp -> w0 : la destination
j_dst:	cbz	w0,.t
	adr	x2,jit_pin
	ldrb	w2,[x2,x0]
	cmp	w2,0xFF
	b.eq	.t
	mov	w0,w2
	ret
.t:	mov	w0,w1
	ret

; w0 = g, w1 = h : apres, h tient la valeur de g
j_wr:	cbz	w0,.r
	adr	x2,jit_pin
	ldrb	w2,[x2,x0]
	cmp	w2,0xFF
	b.eq	.m
	cmp	w2,w1
	b.eq	.r
	mov	w3,w1
	mov	w1,w2
	mov	w2,RZ
	movl	w0,0xAA000000
	b	emit3
.m:	mov	w2,w0
	lsl	w2,w2,3
	mov	w0,w1
	mov	w1,19
	movl	w3,0xF9000000
	b	emit_ls
.r:	ret

; w1 = adresse physique de w0 ; hors RDRAM, mal alignee : sortie
; w0 = mask (0, 1, 3, 7), w1 = store
eff_addr:
	str	x30,[sp,-16]!
	stp	x9,x10,[sp,-16]!
	mov	w9,w0
	mov	w10,w1
	mov	w0,w23
	mov	w1,0
	bl	j_rd
	mov	w1,w0
	mov	w0,0
	sxth	w2,w22
	bl	add_imm32
	E	0x52010001			; eor w1, w0, #0x80000000
	E	0x12027821			; and w1, w1, #0xDFFFFFFF
	E	0x7160003F			; cmp w1, #0x800, lsl 12
	mov	w0,CC_HS
	bl	side_if
	cbz	w9,.a
	mov	w2,0
	cmp	w9,1
	b.eq	.m
	mov	w2,1
	cmp	w9,3
	b.eq	.m
	mov	w2,2
.m:	movl	w0,0x7200003F or (1 shl 5)	; tst w1, #masque
	orr	w0,w0,w2,lsl 10
	str	w0,[x21],4
	mov	w0,CC_NE
	bl	side_if
.a:	cbz	w10,.f
	a_LSRW	2,1,JP
	a_LSX	R_LDRB,2,6,2
	mov	w0,2
	bl	side_cbnz
.f:	ldp	x9,x10,[sp],16
	ldr	x30,[sp],16
	ret

check_fp:
	ldr	w0,[x28,J_fpChecked]
	cbz	w0,.a
	ret
.a:	mov	w0,1
	str	w0,[x28,J_fpChecked]
	str	x30,[sp,-16]!
	a_LDRX	0,19,m_cp0+12*8
	E	0x52A48001			; movz w1, #0x2400, lsl 16
	a_ANDW	0,0,1
	ldr	w0,[x28,J_fr]
	movl	w1,0x52A48002
	movl	w2,0x52A40002
	cmp	w0,0
	csel	w1,w1,w2,ne
	str	w1,[x21],4
	a_CMPW	0,2
	mov	w0,CC_NE
	bl	side_if
	ldr	x30,[sp],16
	ret

; w0 = n -> w0 : l'offset du flottant (selon FR a la traduction)
fp32_off:
	ldr	w1,[x28,J_fr]
	cbnz	w1,.fr
	tbz	w0,0,.fr
	and	w0,w0,30
	lsl	w0,w0,3
	add	w0,w0,m_fpr+4
	ret
.fr:	lsl	w0,w0,3
	add	w0,w0,m_fpr
	ret
fp64_off:
	ldr	w1,[x28,J_fr]
	cbnz	w1,.fr
	and	w0,w0,30
.fr:	lsl	w0,w0,3
	add	w0,w0,m_fpr
	ret

; -> w0 = 1 traduite, 0 non
gen_cop1:
	stp	x29,x30,[sp,-16]!
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	ubfx	w9,w22,21,5			; fmt
	ubfx	w10,w22,16,5			; ft
	ubfx	w11,w22,11,5			; fs
	ubfx	w12,w22,6,5			; fd
	cbnz	w9,.n0
	bl	check_fp			; MFC1
	cbz	w10,.yes
	mov	w0,w10
	mov	w1,2
	bl	j_dst
	mov	w8,w0
	mov	w0,w11
	bl	fp32_off
	a_LDRW	2,19,w0
	a_SXTW	w8,2
	mov	w0,w10
	mov	w1,w8
	bl	j_wr
	b	.yes
.n0:	cmp	w9,1
	b.ne	.n1
	bl	check_fp			; DMFC1
	cbz	w10,.yes
	mov	w0,w11
	bl	fp64_off
	a_LDRX	2,19,w0
	mov	w0,w10
	mov	w1,2
	bl	j_wr
	b	.yes
.n1:	cmp	w9,4
	b.ne	.n2
	bl	check_fp			; MTC1
	mov	w0,w10
	mov	w1,2
	bl	j_rd
	mov	w8,w0
	mov	w0,w11
	bl	fp32_off
	a_STRW	w8,19,w0
	b	.yes
.n2:	cmp	w9,5
	b.ne	.n3
	bl	check_fp			; DMTC1
	mov	w0,w10
	mov	w1,2
	bl	j_rd
	mov	w8,w0
	mov	w0,w11
	bl	fp64_off
	a_STRX	w8,19,w0
	b	.yes
.n3:	and	w13,w22,63			; fn
	cmp	w9,0x10
	b.eq	.fl
	cmp	w9,0x11
	b.ne	.w
.fl:	sub	w14,w9,0x10			; dbl
	bl	check_fp
	cmp	w13,8
	b.lo	.ar
	cmp	w13,0x0C
	b.lo	.no
	cmp	w13,0x10
	b.lo	.toi
	cmp	w13,0x24
	b.eq	.toi
	cmp	w13,0x20
	b.eq	.cvs
	cmp	w13,0x21
	b.eq	.cvd
	cmp	w13,0x30
	b.lo	.no
	; C.cond ; un NaN : l'interprete
	mov	w0,0
	mov	w1,w11
	bl	.load
	mov	w0,1
	mov	w1,w10
	bl	.load
	mov	w0,0
	mov	w1,0
	mov	w2,1
	movl	w3,0x1E202000
	bl	.f2
	mov	w0,CC_VS
	bl	side_if
	tst	w13,6
	b.eq	.cz
	and	w0,w13,6
	mov	w1,CC_EQ
	cmp	w0,6
	mov	w2,CC_LS
	csel	w1,w2,w1,eq
	cmp	w0,4
	mov	w2,CC_MI
	csel	w1,w2,w1,eq
	eor	w1,w1,1
	movl	w0,0x1A9F07E0 or 2
	orr	w0,w0,w1,lsl 12
	str	w0,[x21],4
	b	.cs
.cz:	a_MOVW	2,RZ
.cs:	a_LDRW	0,19,m_fcr31
	mov	w0,1
	mov	w1,0x3F000
	orr	w1,w1,1 shl 23
	mvn	w1,w1
	bl	a_mov32
	a_ANDW	0,0,1
	E	0x2A025C00			; orr w0, w0, w2, lsl 23
	a_STRW	0,19,m_fcr31
	b	.yes
.ar:	cmp	w13,5
	b.eq	.no
	cmp	w13,6
	b.eq	.mov
	cmp	w13,7
	b.eq	.neg
	mov	w0,0
	mov	w1,w11
	bl	.load
	cmp	w13,4
	b.eq	.sq
	mov	w0,1
	mov	w1,w10
	bl	.load
	mov	w0,0
	mov	w1,0
	mov	w2,1
	adr	x3,.fops
	ldr	w3,[x3,x13,lsl 2]
	bl	.f2
	mov	w0,0
	adr	x2,.fext
	add	w1,w13,w14,lsl 2
	ldrb	w1,[x2,x1]
	str	w1,[x28,J_extra]
	b	.st0
.fops:	dw	0x1E202800,0x1E203800,0x1E200800,0x1E201800
.fext:	db	0,0,4,56,0,0,7,114
.sq:	movl	w3,0x1E21C000
	bl	.f1
	mov	w0,56
	mov	w1,114
	cmp	w14,0
	csel	w0,w1,w0,ne
	str	w0,[x28,J_extra]
.st0:	mov	w0,0
	mov	w1,w12
	bl	.store
	b	.yes
.neg:	mov	w0,0
	mov	w1,w11
	bl	.load
	movl	w3,0x1E214000
	bl	.f1
	b	.st0
.mov:	cbz	w14,.mvs
	mov	w0,w11
	bl	fp64_off
	a_LDRX	0,19,w0
	mov	w0,w12
	bl	fp64_off
	a_STRX	0,19,w0
	b	.yes
.mvs:	mov	w0,w11
	bl	fp32_off
	a_LDRW	0,19,w0
	mov	w0,w12
	bl	fp32_off
	a_STRW	0,19,w0
	b	.yes
.toi:	cmp	w13,0x24
	b.ne	.ti1
	a_LDRW	0,19,m_fcr31			; CVT.W : FCR31, au plus pres seulement
	E	0x7200041F			; tst w0, #3
	mov	w0,CC_NE
	bl	side_if
.ti1:	mov	w0,0
	mov	w1,w11
	bl	.load
	cmp	w13,0x0D
	b.eq	.ti2				; (FRINTZ : le fcvtzs)
	movl	w3,0x1E244000			; FRINTN
	cmp	w13,0x0E
	b.ne	.ti3
	movl	w3,0x1E24C000
.ti3:	cmp	w13,0x0F
	b.ne	.ti4
	movl	w3,0x1E254000
.ti4:	bl	.f1
.ti2:	movl	w0,0x1E380000
	cbz	w14,.ti5
	movl	w0,0x1E780000
.ti5:	str	w0,[x21],4
	mov	w0,w12
	bl	fp32_off
	a_STRW	0,19,w0
	b	.yes
.cvs:	cbz	w14,.no
	mov	w0,0
	mov	w1,w11
	bl	.load
	E	0x1E624000			; fcvt s0, d0
	mov	w0,w12
	bl	fp32_off
	a_STRS	0,w0
	b	.yes
.cvd:	cbnz	w14,.no
	mov	w0,0
	mov	w1,w11
	bl	.load
	E	0x1E22C000			; fcvt d0, s0
	mov	w0,w12
	bl	fp64_off
	a_STRD	0,w0
	b	.yes
.w:	cmp	w9,0x14
	b.ne	.no
	cmp	w13,0x20
	b.eq	.w1
	cmp	w13,0x21
	b.ne	.no
.w1:	bl	check_fp
	mov	w0,w11
	bl	fp32_off
	a_LDRW	0,19,w0
	cmp	w13,0x20
	b.ne	.w2
	E	0x1E220000
	mov	w0,w12
	bl	fp32_off
	a_STRS	0,w0
	b	.yes
.w2:	E	0x1E620000
	mov	w0,w12
	bl	fp64_off
	a_STRD	0,w0
	b	.yes
.no:	mov	w0,0
	b	.ret
.yes:	mov	w0,1
.ret:	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	ldp	x29,x30,[sp],16
	ret
; w0 = t, w1 = n : le flottant n dans s / d t
.load:	mov	w8,w0
	mov	x7,x30
	mov	w0,w1
	cbz	w14,.ls
	bl	fp64_off
	a_LDRD	w8,w0
	br	x7
.ls:	bl	fp32_off
	a_LDRS	w8,w0
	br	x7
.store:	mov	w8,w0
	mov	x7,x30
	mov	w0,w1
	cbz	w14,.ss
	bl	fp64_off
	a_STRD	w8,w0
	br	x7
.ss:	bl	fp32_off
	a_STRS	w8,w0
	br	x7
; w0 = d, w1 = n, w2 = m, w3 = base (+ dbl)
.f2:	orr	w3,w3,w14,lsl 22
	orr	w3,w3,w2,lsl 16
	orr	w3,w3,w1,lsl 5
	orr	w3,w3,w0
	str	w3,[x21],4
	ret
; s0 = op s0 : w3 = base
.f1:	orr	w3,w3,w14,lsl 22
	str	w3,[x21],4
	ret

; une instruction sans branchement : w22 = op -> w0 = 1 traduite
gen_op:
	stp	x29,x30,[sp,-16]!
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	ubfx	w23,w22,21,5
	ubfx	w24,w22,16,5
	ubfx	w25,w22,11,5
	ubfx	w26,w22,6,5
	lsr	w0,w22,26
	jtab	x0,.t
.t:	dw	.sp-.t,.no-.t,.no-.t,.no-.t,.no-.t,.no-.t,.no-.t,.no-.t
	dw	.addi-.t,.addiu-.t,.slti-.t,.slti-.t,.logi-.t,.logi-.t,.logi-.t,.lui-.t
	dw	.no-.t,.cop1-.t,.no-.t,.no-.t,.no-.t,.no-.t,.no-.t,.no-.t
	dw	.daddi-.t,.daddiu-.t,.no-.t,.no-.t,.no-.t,.no-.t,.no-.t,.no-.t
	dw	.ld-.t,.ld-.t,.no-.t,.ld-.t,.ld-.t,.ld-.t,.no-.t,.ld-.t
	dw	.st-.t,.st-.t,.no-.t,.st-.t,.no-.t,.no-.t,.no-.t,.yes-.t
	dw	.no-.t,.lf-.t,.no-.t,.no-.t,.no-.t,.lf-.t,.no-.t,.ld-.t
	dw	.no-.t,.sf-.t,.no-.t,.no-.t,.no-.t,.sf-.t,.no-.t,.st-.t
.no:	mov	w0,0
	b	.ret
.yes:	mov	w0,1
.ret:	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	ldp	x29,x30,[sp],16
	ret
.cop1:	bl	gen_cop1
	b	.ret
; (rd : w12 = op & 63)
.sp:	and	w12,w22,63
	cmp	w12,0x38
	b.hs	.ds
	cmp	w12,0x30
	b.hs	.no
	jtab	x12,.u
.u:	dw	.sh-.u,.no-.u,.sh-.u,.sh-.u,.shv-.u,.no-.u,.shv-.u,.shv-.u
	dw	.no-.u,.no-.u,.no-.u,.no-.u,.no-.u,.no-.u,.no-.u,.yes-.u
	dw	.mfh-.u,.mth-.u,.mfl-.u,.mtl-.u,.dshv-.u,.no-.u,.dshv-.u,.dshv-.u
	dw	.mul-.u,.mul-.u,.div-.u,.div-.u,.dmul-.u,.dmul-.u,.no-.u,.no-.u
	dw	.add-.u,.addu-.u,.add-.u,.addu-.u,.log-.u,.log-.u,.log-.u,.log-.u
	dw	.no-.u,.no-.u,.slt-.u,.slt-.u,.dadd-.u,.daddu-.u,.dadd-.u,.daddu-.u
; deux sources, une destination (rd) : w9 a, w10 b, w11 d
.src2:	stp	x29,x30,[sp,-16]!
	mov	w0,w23
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	mov	w0,w24
	mov	w1,1
	bl	j_rd
	mov	w10,w0
	mov	w0,w25
	mov	w1,2
	bl	j_dst
	mov	w11,w0
	ldp	x29,x30,[sp],16
	ret
.wr11:	mov	w0,w25
	mov	w1,w11
	bl	j_wr
	b	.yes
.sh:	cbz	w25,.yes
	mov	w0,w24
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	mov	w0,w25
	mov	w1,2
	bl	j_dst
	mov	w11,w0
	cbnz	w12,.sh1
	cbz	w26,.sh0
	; lsl w d, w s, sa
	movl	w0,0x53000000
	mov	w1,w11
	mov	w2,w9
	mov	w3,32
	sub	w3,w3,w26
	and	w3,w3,31
	mov	w4,31
	sub	w4,w4,w26
	orr	w3,w3,w4,lsl 8
	bl	emit_bf
	b	.shs
.sh0:	a_MOVW	w11,w9
	b	.shs
.sh1:	cmp	w12,2
	b.ne	.sh2
	a_LSRW	w11,w9,w26
	b	.shs
.sh2:	a_ASR	w11,w9,w26
.shs:	a_SXTW	w11,w11
	b	.wr11
.shv:	cbz	w25,.yes
	bl	.src2				; (a = n : rs, b = s : rt)
	cmp	w12,7
	b.ne	.shv1
	movl	w0,0x12001000
	orr	w0,w0,w9,lsl 5
	orr	w0,w0,16
	str	w0,[x21],4
	movl	w0,0x9AC02800			; asr x
	mov	w1,w11
	mov	w2,w10
	mov	w3,16
	bl	emit3
	b	.shs
.shv1:	movl	w0,0x1AC02000
	cmp	w12,4
	b.eq	.shv2
	movl	w0,0x1AC02400
.shv2:	mov	w1,w11
	mov	w2,w10
	mov	w3,w9
	bl	emit3
	b	.shs
.mfh:	cbz	w25,.yes
	mov	w0,w25
	mov	w1,2
	bl	j_dst
	mov	w11,w0
	a_LDRX	w11,19,m_hi
	b	.wr11
.mfl:	cbz	w25,.yes
	mov	w0,w25
	mov	w1,2
	bl	j_dst
	mov	w11,w0
	a_LDRX	w11,19,m_lo
	b	.wr11
.mth:	mov	w0,w23
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	a_STRX	w9,19,m_hi
	b	.yes
.mtl:	mov	w0,w23
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	a_STRX	w9,19,m_lo
	b	.yes
.dshv:	cbz	w25,.yes
	bl	.src2
	mov	w0,0x2000
	cmp	w12,0x16
	b.ne	.ds1
	mov	w0,0x2400
.ds1:	cmp	w12,0x17
	b.ne	.ds2
	mov	w0,0x2800
.ds2:	movl	w1,0x9AC00000
	orr	w0,w0,w1
	mov	w1,w11
	mov	w2,w10
	mov	w3,w9
	bl	emit3
	b	.wr11
.mul:	bl	.src2
	movl	w0,0x9B207C00
	cmp	w12,0x18
	b.eq	.mu1
	movl	w0,0x9BA07C00
.mu1:	mov	w1,2
	mov	w2,w9
	mov	w3,w10
	bl	emit3
	a_SXTW	3,2
	a_STRX	3,19,m_lo
	a_ASR	3,2,32
	a_SXTW	3,3
	a_STRX	3,19,m_hi
	mov	w0,8
	str	w0,[x28,J_extra]
	b	.yes
.div:	bl	.src2
	a_CMPW	w10,RZ
	mov	w0,CC_EQ
	bl	side_if
	cmp	w12,0x1A
	b.ne	.du
	E	0x12800002			; movn w2, #0
	a_CMPW	w10,2
	mov	w0,CC_EQ
	bl	side_if
	movl	w0,0x1AC00C00
	b	.d2
.du:	movl	w0,0x1AC00800
.d2:	mov	w1,2
	mov	w2,w9
	mov	w3,w10
	bl	emit3
	; msub w3, w2, b, a
	movl	w0,0x1B008000
	orr	w0,w0,w10,lsl 16
	orr	w0,w0,w9,lsl 10
	orr	w0,w0,2 shl 5
	orr	w0,w0,3
	str	w0,[x21],4
	a_SXTW	2,2
	a_STRX	2,19,m_lo
	a_SXTW	3,3
	a_STRX	3,19,m_hi
	mov	w0,70
	str	w0,[x28,J_extra]
	b	.yes
.dmul:	bl	.src2
	movl	w0,0x9B007C00
	mov	w1,2
	mov	w2,w9
	mov	w3,w10
	bl	emit3
	movl	w0,0x9B407C00
	cmp	w12,0x1C
	b.eq	.dm1
	movl	w0,0x9BC07C00
.dm1:	mov	w1,3
	mov	w2,w9
	mov	w3,w10
	bl	emit3
	a_STRX	2,19,m_lo
	a_STRX	3,19,m_hi
	mov	w0,14
	str	w0,[x28,J_extra]
	b	.yes
.add:	bl	.src2
	movl	w0,0x2B000000
	cmp	w12,0x20
	b.eq	.a1
	movl	w0,0x6B000000
.a1:	mov	w1,2
	mov	w2,w9
	mov	w3,w10
	bl	emit3
	mov	w0,CC_VS
	bl	side_if
	cbz	w25,.yes
	mov	w0,w25
	mov	w1,2
	bl	j_dst
	mov	w11,w0
	a_SXTW	w11,2
	b	.wr11
.addu:	cbz	w25,.yes
	bl	.src2
	movl	w0,0x0B000000
	cmp	w12,0x21
	b.eq	.au1
	movl	w0,0x4B000000
.au1:	mov	w1,w11
	mov	w2,w9
	mov	w3,w10
	bl	emit3
	a_SXTW	w11,w11
	b	.wr11
.log:	cbz	w25,.yes
	bl	.src2
	cmp	w12,0x24
	b.ne	.l1
	a_AND	w11,w9,w10
	b	.wr11
.l1:	cmp	w12,0x26
	b.ne	.l2
	a_EOR	w11,w9,w10
	b	.wr11
.l2:	a_ORR	w11,w9,w10
	cmp	w12,0x25
	b.eq	.wr11
	op3	0xAA200000,w11,RZ,w11		; orn d, xzr, d
	b	.wr11
.slt:	cbz	w25,.yes
	bl	.src2
	a_CMP	w9,w10
	mov	w0,CC_LT
	cmp	w12,0x2A
	b.eq	.s1
	mov	w0,CC_LO
.s1:	mov	w1,w11
	bl	.cset
	b	.wr11
; cset x d (w1), cc (w0)
.cset:	eor	w0,w0,1
	lsl	w0,w0,12
	orr	w0,w0,w1
	movl	w1,0x9A9F07E0
	orr	w0,w0,w1
	str	w0,[x21],4
	ret
.dadd:	bl	.src2
	movl	w0,0xAB000000
	cmp	w12,0x2C
	b.eq	.da1
	movl	w0,0xEB000000
.da1:	mov	w1,2
	mov	w2,w9
	mov	w3,w10
	bl	emit3
	mov	w0,CC_VS
	bl	side_if
	mov	w0,w25
	mov	w1,2
	bl	j_wr
	b	.yes
.daddu:	cbz	w25,.yes
	bl	.src2
	movl	w0,0x8B000000
	cmp	w12,0x2D
	b.eq	.du1
	movl	w0,0xCB000000
.du1:	mov	w1,w11
	mov	w2,w9
	mov	w3,w10
	bl	emit3
	b	.wr11
.ds:	cmp	w12,0x39
	b.eq	.no
	cmp	w12,0x3D
	b.eq	.no
	cbz	w25,.yes
	mov	w0,w24
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	mov	w0,w25
	mov	w1,2
	bl	j_dst
	mov	w11,w0
	mov	w4,w26
	tbz	w12,2,.ds3
	add	w4,w4,32			; n
.ds3:	and	w0,w12,3
	cbnz	w0,.ds5
	cbnz	w4,.ds4
	a_MOV	w11,w9
	b	.wr11
.ds4:	movl	w0,0xD3400000			; lsl x
	mov	w1,w11
	mov	w2,w9
	mov	w3,64
	sub	w3,w3,w4
	and	w3,w3,63
	mov	w5,63
	sub	w5,w5,w4
	orr	w3,w3,w5,lsl 8
	bl	emit_bf
	b	.wr11
.ds5:	cmp	w0,2
	b.ne	.ds6
	a_LSR	w11,w9,w4
	b	.wr11
.ds6:	a_ASR	w11,w9,w4
	b	.wr11
; deux registres et l'immediat : w9 = a (rs), w11 = d (rt)
.src1:	stp	x29,x30,[sp,-16]!
	mov	w0,w23
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	mov	w0,w24
	mov	w1,2
	bl	j_dst
	mov	w11,w0
	ldp	x29,x30,[sp],16
	ret
.wrt:	mov	w0,w24
	mov	w1,w11
	bl	j_wr
	b	.yes
.addi:	mov	w0,w23
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	mov	w0,1
	sxth	w1,w22
	bl	a_mov32
	movl	w0,0x2B000000
	mov	w1,2
	mov	w2,w9
	mov	w3,1
	bl	emit3
	mov	w0,CC_VS
	bl	side_if
	cbz	w24,.yes
	mov	w0,w24
	mov	w1,2
	bl	j_dst
	mov	w11,w0
	a_SXTW	w11,2
	b	.wrt
.addiu:	cbz	w24,.yes
	bl	.src1
	mov	w0,w11
	mov	w1,w9
	sxth	w2,w22
	bl	add_imm32
	a_SXTW	w11,w11
	b	.wrt
.slti:	cbz	w24,.yes
	bl	.src1
	mov	w0,1
	sxth	w1,w22
	bl	a_movs
	a_CMP	w9,1
	lsr	w2,w22,26
	mov	w0,CC_LT
	cmp	w2,0x0A
	b.eq	.sl1
	mov	w0,CC_LO
.sl1:	mov	w1,w11
	bl	.cset
	b	.wrt
.logi:	cbz	w24,.yes
	bl	.src1
	and	w1,w22,0xFFFF
	lsl	w1,w1,5
	movl	w0,0xD2800001
	orr	w0,w0,w1
	str	w0,[x21],4
	lsr	w2,w22,26
	cmp	w2,0x0C
	b.ne	.lg1
	a_AND	w11,w9,1
	b	.wrt
.lg1:	cmp	w2,0x0D
	b.ne	.lg2
	a_ORR	w11,w9,1
	b	.wrt
.lg2:	a_EOR	w11,w9,1
	b	.wrt
.lui:	cbz	w24,.yes
	mov	w0,w24
	mov	w1,2
	bl	j_dst
	mov	w11,w0
	lsl	w1,w22,16
	bl	a_movs
	b	.wrt
.daddi:	mov	w0,w23
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	mov	w0,1
	sxth	w1,w22
	bl	a_movs
	movl	w0,0xAB000000
	mov	w1,2
	mov	w2,w9
	mov	w3,1
	bl	emit3
	mov	w0,CC_VS
	bl	side_if
	mov	w0,w24
	mov	w1,2
	bl	j_wr
	b	.yes
.daddiu: cbz	w24,.yes
	bl	.src1
	mov	w0,1
	sxth	w1,w22
	bl	a_movs
	a_ADD	w11,w9,1
	b	.wrt
.ld:	lsr	w12,w22,26			; o
	mov	w0,3
	cmp	w12,0x20
	b.eq	.ld0
	cmp	w12,0x24
	b.ne	.ld1
.ld0:	mov	w0,0
	b	.ld3
.ld1:	cmp	w12,0x21
	b.eq	.ld2
	cmp	w12,0x25
	b.ne	.ld15
.ld2:	mov	w0,1
	b	.ld3
.ld15:	cmp	w12,0x37
	b.ne	.ld3
	mov	w0,7
.ld3:	mov	w1,0
	bl	eff_addr
	mov	w0,0
	cmp	w12,0x20
	b.eq	.lx3
	cmp	w12,0x24
	b.ne	.lx0
.lx3:	E	0x52000421			; eor w1, w1, #3
	b	.lx1
.lx0:	cmp	w12,0x21
	b.eq	.lx2
	cmp	w12,0x25
	b.ne	.lx1
.lx2:	E	0x521F0021			; eor w1, w1, #2
.lx1:	mov	w11,2
	cbz	w24,.ld4
	mov	w0,w24
	mov	w1,2
	bl	j_dst
	mov	w11,w0
.ld4:	cmp	w12,0x20
	b.ne	.a21
	a_LSX	R_LDRSBX,w11,7,1
	b	.wrt
.a21:	cmp	w12,0x21
	b.ne	.a23
	a_LSX	R_LDRSHX,w11,7,1
	b	.wrt
.a23:	cmp	w12,0x23
	b.ne	.a24
	a_LSX	R_LDRSW,w11,7,1
	b	.wrt
.a24:	cmp	w12,0x24
	b.ne	.a25
	a_LSX	R_LDRB,w11,7,1
	b	.wrt
.a25:	cmp	w12,0x25
	b.ne	.a27
	a_LSX	R_LDRH,w11,7,1
	b	.wrt
.a27:	cmp	w12,0x27
	b.ne	.a37
	a_LSX	R_LDRW,w11,7,1
	b	.wrt
.a37:	a_LSX	R_LDRX,w11,7,1
	mov	w0,w11
	bl	.ror32
	b	.wrt
; ror x, x, 32 (w0)
.ror32:	movl	w1,0x93C08000
	orr	w1,w1,w0,lsl 16
	orr	w1,w1,w0,lsl 5
	orr	w1,w1,w0
	str	w1,[x21],4
	ret
.st:	lsr	w12,w22,26
	mov	w0,3
	cmp	w12,0x28
	b.ne	.st1
	mov	w0,0
	b	.st3
.st1:	cmp	w12,0x29
	b.ne	.st2
	mov	w0,1
	b	.st3
.st2:	cmp	w12,0x3F
	b.ne	.st3
	mov	w0,7
.st3:	mov	w1,1
	bl	eff_addr
	cmp	w12,0x28
	b.ne	.st4
	E	0x52000421
	b	.st5
.st4:	cmp	w12,0x29
	b.ne	.st5
	E	0x521F0021
.st5:	mov	w0,w24
	mov	w1,3
	bl	j_rd
	mov	w9,w0
	cmp	w12,0x28
	b.ne	.st6
	a_LSX	R_STRB,w9,7,1
	b	.yes
.st6:	cmp	w12,0x29
	b.ne	.st7
	a_LSX	R_STRH,w9,7,1
	b	.yes
.st7:	cmp	w12,0x2B
	b.ne	.st8
	a_LSX	R_STRW,w9,7,1
	b	.yes
.st8:	movl	w0,0x93C08003			; ror x3, v, 32
	orr	w0,w0,w9,lsl 16
	orr	w0,w0,w9,lsl 5
	str	w0,[x21],4
	a_LSX	R_STRX,3,7,1
	b	.yes
.lf:	bl	check_fp
	lsr	w12,w22,26
	mov	w0,3
	cmp	w12,0x31
	b.eq	.lf1
	mov	w0,7
.lf1:	mov	w1,0
	bl	eff_addr
	cmp	w12,0x31
	b.ne	.lf2
	a_LSX	R_LDRW,2,7,1
	mov	w0,w24
	bl	fp32_off
	a_STRW	2,19,w0
	b	.yes
.lf2:	a_LSX	R_LDRX,2,7,1
	mov	w0,2
	bl	.ror32
	mov	w0,w24
	bl	fp64_off
	a_STRX	2,19,w0
	b	.yes
.sf:	bl	check_fp
	lsr	w12,w22,26
	mov	w0,3
	cmp	w12,0x39
	b.eq	.sf1
	mov	w0,7
.sf1:	mov	w1,1
	bl	eff_addr
	cmp	w12,0x39
	b.ne	.sf2
	mov	w0,w24
	bl	fp32_off
	a_LDRW	2,19,w0
	a_LSX	R_STRW,2,7,1
	b	.yes
.sf2:	mov	w0,w24
	bl	fp64_off
	a_LDRX	2,19,w0
	mov	w0,2
	bl	.ror32
	a_LSX	R_STRX,2,7,1
	b	.yes

; w0 = op -> w0 : 1 relatif, 2 J / JAL, 3 JR / JALR, 0
branch_kind:
	lsr	w1,w0,26
	sub	w2,w1,2
	cmp	w2,2
	b.hs	.a
	mov	w0,2
	ret
.a:	cbnz	w1,.b
	and	w2,w0,63
	sub	w2,w2,8
	cmp	w2,2
	b.hs	.z
	mov	w0,3
	ret
.b:	sub	w2,w1,4
	cmp	w2,4
	b.lo	.y
	sub	w2,w1,0x14
	cmp	w2,4
	b.lo	.y
	cmp	w1,1
	b.ne	.c
	ubfx	w2,w0,16,5
	cmp	w2,3
	b.ls	.y
	sub	w2,w2,0x10
	cmp	w2,3
	b.ls	.y
	b	.z
.c:	cmp	w1,0x11
	b.ne	.z
	ubfx	w2,w0,21,5
	cmp	w2,8
	b.ne	.z
.y:	mov	w0,1
	ret
.z:	mov	w0,0
	ret

; w0 = target, w1 = cyc
exit_to:
	str	x30,[sp,-16]!
	mov	w4,w0
	cbz	w1,.a
	mov	w2,w1
	mov	w0,20
	mov	w1,20
	movl	w3,0x91000000
	bl	emit_i
.a:	ldr	w0,[x28,J_nex]
	add	w1,w0,1
	str	w1,[x28,J_nex]
	add	x1,x28,J_ex
	add	x1,x1,x0,lsl 4
	str	x21,[x1]
	str	w4,[x1,8]
	str	wzr,[x21],4
	ldr	x30,[sp],16
	ret

; w0 = op : w5 = la condition du branchement relatif
gen_cond:
	stp	x29,x30,[sp,-16]!
	stp	x9,x10,[sp,-16]!
	lsr	w9,w0,26			; o
	ubfx	w23,w0,21,5
	ubfx	w24,w0,16,5
	cmp	w9,0x11
	b.ne	.a
	bl	check_fp
	a_LDRW	0,19,m_fcr31
	E	0x53175C05			; ubfx w5, w0, 23, 1
	tbnz	w24,0,.r
	E	0x520000A5			; eor w5, w5, #1
	b	.r
.a:	cmp	w9,1
	b.ne	.b
	mov	w0,w23
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	a_CMP	w9,RZ
	mov	w0,CC_LT
	tbz	w24,0,.c
	mov	w0,CC_GE
	b	.c
.b:	and	w10,w9,3			; k
	cmp	w10,1
	b.hi	.d
	mov	w0,w23
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	mov	w0,w24
	mov	w1,1
	bl	j_rd
	a_CMP	w9,w0
	mov	w0,w10
	b	.c				; (0 eq, 1 ne)
.d:	mov	w0,w23
	mov	w1,0
	bl	j_rd
	mov	w9,w0
	a_CMP	w9,RZ
	mov	w0,CC_LE
	cmp	w10,2
	b.eq	.c
	mov	w0,CC_GT
.c:	eor	w0,w0,1				; cset w5, cc
	lsl	w0,w0,12
	movl	w1,0x1A9F07E5
	orr	w0,w0,w1
	str	w0,[x21],4
.r:	ldp	x9,x10,[sp],16
	ldp	x29,x30,[sp],16
	ret

; x0 = debut, x1 = fin du code a rendre executable
flush_code:
	mov	x4,x0
	dw	0xD53B0022			; mrs x2, ctr_el0
	ubfx	x3,x2,16,4
	mov	x5,4
	lsl	x3,x5,x3			; dl
	and	x2,x2,15
	lsl	x2,x5,x2			; il
	sub	x5,x3,1
	bic	x5,x0,x5
.d:	cmp	x5,x1
	b.hs	.de
	dw	0xD50B7B25			; dc cvau, x5
	add	x5,x5,x3
	b	.d
.de:	dw	0xD5033B9F			; dsb ish
	sub	x5,x2,1
	bic	x5,x4,x5
.i:	cmp	x5,x1
	b.hs	.ie
	dw	0xD50B7525			; ic ivau, x5
	add	x5,x5,x2
	b	.i
.ie:	dw	0xD5033B9F
	dw	0xD5033FDF			; isb
	ret

jit_flush_all:
	ldr	x0,[x19,m_jitStart]
	ldr	x1,[x19,m_jitCode]
	add	x0,x1,x0
	str	x0,[x19,m_jitCp]
	mov	w0,1
	str	w0,[x19,m_jitBlocks]
	str	wzr,[x19,m_jitNlinks]
	ldr	x0,[x19,m_jitFlushes]
	add	x0,x0,1
	str	x0,[x19,m_jitFlushes]
	str	x30,[sp,-16]!
	ldr	x0,[x19,m_jitTab]
	mov	x1,N64_RDRAM_SIZE
	bl	mem_zero
	ldr	x0,[x19,m_jitPage]
	mov	x1,N64_RDRAM_SIZE shr JP
	bl	mem_zero
	ldr	x30,[sp],16
	ret

jit_flush:
	ldr	x0,[x19,m_jitPage]
	cbnz	x0,jit_flush_all
	ret

; patchBcc : x0 = at, x1 = vers
patch_bcc:
	sub	x2,x1,x0
	asr	x2,x2,2
	and	w2,w2,0x7FFFF
	ldr	w3,[x0]
	movl	w4,0xFF00001F
	and	w3,w3,w4
	orr	w3,w3,w2,lsl 5
	str	w3,[x0]
	ret
patch_b:
	sub	x2,x1,x0
	asr	x2,x2,2
	and	w2,w2,0x3FFFFFF
	movl	w3,0x14000000
	orr	w2,w2,w3
	str	w2,[x0]
	ret

; x1 = trampoline : b depuis x21
emit_b:	sub	x2,x1,x21
	asr	x2,x2,2
	and	w2,w2,0x3FFFFFF
	movl	w3,0x14000000
	orr	w2,w2,w3
	str	w2,[x21],4
	ret

; w0 = va, w1 = pa -> w0 = le bloc
jit_compile:
	prologue
	save_callee
	sub	sp,sp,64
	movl	x28,M_JST
	add	x28,x19,x28
	str	w0,[x28,J_va]
	str	w1,[x28,J_pa]
	ldr	x21,[x19,m_jitCp]
	ldr	x0,[x19,m_jitCode]
	sub	x0,x21,x0
	movl	x1,JIT_CODE_SIZE-0x10000
	cmp	x0,x1
	b.gt	.fl
	ldr	w0,[x19,m_jitBlocks]
	movl	w1,MAX_BLOCKS-2
	cmp	w0,w1
	b.ge	.fl
	ldr	w0,[x19,m_jitNlinks]
	movl	w1,MAX_LINKS-16
	cmp	w0,w1
	b.lt	.go
.fl:	bl	jit_flush_all
	ldr	x21,[x19,m_jitCp]
.go:	ldr	w0,[x19,m_jitBlocks]
	str	w0,[sp]				; id
	str	x21,[sp,8]			; start
	str	wzr,[x28,J_nst]
	str	wzr,[x28,J_nex]
	str	wzr,[x28,J_fpChecked]
	ldr	x0,[x19,m_cp0+12*8]
	ubfx	x0,x0,26,1
	str	w0,[x28,J_fr]
	str	x21,[sp,16]			; entryAdd
	str	wzr,[x21],4
	a_LDRX	1,19,m_nextEvent
	a_CMP	0,1
	str	x21,[sp,24]			; entryFix
	E	0x54000000+CC_HI
	mov	w9,0				; sofar
	mov	w10,0				; worst
	mov	w11,0				; ins
	ldr	w12,[x28,J_va]			; at
	ldr	w13,[x28,J_pa]			; p
.ins:	cmp	w11,MAX_INS
	b.ge	.lim
	cmp	w10,MAX_WORST
	b.le	.nl
.lim:	mov	w0,w12
	mov	w1,w9
	bl	exit_to
	b	.fin
.nl:	add	w0,w13,8
	cmp	w0,N64_RDRAM_SIZE
	b.ls	.nl2
	bl	.mark
	bl	side_always
	b	.fin
.nl2:	add	x0,x19,M_RDRAM
	ldr	w22,[x0,x13]			; op
	mov	w0,w22
	bl	branch_kind
	mov	w14,w0				; bk
	bl	.mark
	str	wzr,[x28,J_extra]
	movl	w0,0x1000FFFF
	cmp	w22,w0
	b.ne	.nb
	bl	side_always
	b	.fin
.nb:	cbnz	w14,.br
	cbz	w22,.nop
	str	x21,[sp,32]
	ldr	w0,[x28,J_nst]
	str	w0,[sp,40]
	ldr	w0,[x28,J_fpChecked]
	str	w0,[sp,44]
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	stp	x13,x14,[sp,-16]!
	bl	gen_op
	ldp	x13,x14,[sp],16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	cbnz	w0,.nop
	ldr	x21,[sp,32]
	ldr	w0,[sp,40]
	str	w0,[x28,J_nst]
	ldr	w0,[sp,44]
	str	w0,[x28,J_fpChecked]
	mov	w0,-1
	str	w0,[x28,J_cur]
	bl	side_always
	b	.fin
.nop:	ldr	w0,[x28,J_extra]
	add	w0,w0,2
	add	w9,w9,w0
	add	w10,w10,w0
	add	w12,w12,4
	add	w13,w13,4
	add	w11,w11,1
	b	.ins
.br:	add	x0,x19,M_RDRAM
	add	w1,w13,4
	ldr	w15,[x0,x1]			; slotOp
	str	x21,[sp,32]
	ldr	w0,[x28,J_nst]
	str	w0,[sp,40]
	ldr	w0,[x28,J_fpChecked]
	str	w0,[sp,44]
	ldr	w0,[x28,J_nex]
	str	w0,[sp,48]
	mov	w0,w15
	bl	branch_kind
	cbz	w0,.br1
	bl	side_always
	b	.fin
.br1:	lsr	w8,w22,26			; o
	mov	w26,0				; target
	mov	w27,0				; likely | link << 1
	cmp	w14,1
	b.ne	.b2
	sxth	w0,w22
	add	w26,w12,4
	add	w26,w26,w0,lsl 2
	sub	w0,w8,0x14
	cmp	w0,4
	b.hs	.b11
	mov	w27,1
.b11:	cmp	w8,1
	b.ne	.b12
	ubfx	w0,w22,16,5
	ubfx	w1,w0,1,1
	orr	w27,w27,w1
	cmp	w0,0x10
	b.lo	.b12
	orr	w27,w27,2
.b12:	cmp	w8,0x11
	b.ne	.b13
	ubfx	w0,w22,17,1
	orr	w27,w27,w0
.b13:	stp	x8,x9,[sp,-16]!
	stp	x10,x11,[sp,-16]!
	stp	x12,x13,[sp,-16]!
	stp	x14,x15,[sp,-16]!
	mov	w0,w22
	bl	gen_cond
	ldp	x14,x15,[sp],16
	ldp	x12,x13,[sp],16
	ldp	x10,x11,[sp],16
	ldp	x8,x9,[sp],16
	mov	w0,1
	str	w0,[x28,J_slotKind]
	b	.lk
.b2:	cmp	w14,2
	b.ne	.b3
	and	w26,w12,0xF0000000
	and	w0,w22,0x3FFFFFF
	orr	w26,w26,w0,lsl 2
	cmp	w8,3
	b.ne	.b21
	orr	w27,w27,2
.b21:	str	wzr,[x28,J_slotKind]
	b	.lk
.b3:	ubfx	w0,w22,21,5
	mov	w1,0
	bl	j_rd
	a_MOVW	5,w0
	mov	w0,2
	str	w0,[x28,J_slotKind]
.lk:	tbz	w27,1,.lk2
	mov	w0,31
	mov	w1,2
	bl	j_dst
	mov	w4,w0
	add	w1,w12,8
	bl	a_movs
	mov	w0,31
	mov	w1,w4
	bl	j_wr
.lk2:	cmp	w14,3
	b.ne	.lk3
	and	w0,w22,63
	cmp	w0,9
	b.ne	.lk3
	ubfx	w8,w22,11,5
	cbz	w8,.lk3
	mov	w0,w8
	mov	w1,2
	bl	j_dst
	mov	w4,w0
	add	w1,w12,8
	bl	a_movs
	mov	w0,w8
	mov	w1,w4
	bl	j_wr
.lk3:	str	w26,[x28,J_taken]
	add	w0,w12,8
	str	w0,[x28,J_fall]
	add	w9,w9,2
	add	w10,w10,2
	mov	x8,0				; likelyFix
	tbz	w27,0,.sl
	mov	x8,x21
	E	0x35000000+5			; cbnz w5, .pris
	add	w0,w12,8
	mov	w1,w9
	stp	x8,xzr,[sp,-16]!
	bl	exit_to
	ldp	x0,x1,[sp],16
	mov	x1,x21
	bl	patch_bcc
.sl:	; la case
	add	w0,w12,4
	str	w0,[x28,J_at]
	str	w9,[x28,J_sofar]
	mov	w0,1
	str	w0,[x28,J_slot]
	mov	w0,-1
	str	w0,[x28,J_cur]
	str	wzr,[x28,J_extra]
	cbz	w15,.sok
	mov	w22,w15
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	stp	x13,x14,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	bl	gen_op
	ldp	x26,x27,[sp],16
	ldp	x13,x14,[sp],16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	cbnz	w0,.sok
	ldr	x21,[sp,32]
	ldr	w0,[sp,40]
	str	w0,[x28,J_nst]
	ldr	w0,[sp,44]
	str	w0,[x28,J_fpChecked]
	ldr	w0,[sp,48]
	str	w0,[x28,J_nex]
	sub	w9,w9,2
	bl	.mark
	bl	side_always
	b	.fin
.sok:	ldr	w0,[x28,J_extra]
	add	w0,w0,2
	add	w9,w9,w0
	add	w10,w10,w0
	cmp	w14,2
	b.eq	.ex1
	tbz	w27,0,.ex2
.ex1:	mov	w0,w26
	mov	w1,w9
	bl	exit_to
	b	.fin
.ex2:	cmp	w14,1
	b.ne	.ex3
	mov	x8,x21
	E	0x34000000+5			; cbz w5, .non_pris
	mov	w0,w26
	mov	w1,w9
	str	x8,[sp,-16]!
	bl	exit_to
	ldr	x0,[sp],16
	mov	x1,x21
	bl	patch_bcc
	add	w0,w12,8
	mov	w1,w9
	bl	exit_to
	b	.fin
.ex3:	cbz	w9,.ex4
	a_ADDI	20,20,w9
.ex4:	a_MOVW	0,5
	ldr	x1,[x19,m_jitJr]
	bl	emit_b
.fin:	; les sorties
	mov	w0,w10
	mov	w1,2
	cmp	w0,0
	csel	w10,w1,w0,eq			; worst
	ldr	x0,[sp,16]
	movl	w1,0x91000000 or (20 shl 5)
	orr	w1,w1,w10,lsl 10
	str	w1,[x0]
	ldr	x0,[sp,24]
	mov	x1,x21
	bl	patch_bcc
	mov	w0,0
	ldr	w1,[x28,J_va]
	bl	a_mov32
	ldr	x1,[x19,m_jitNorm]
	bl	emit_b
	; les stubs
	add	x14,x28,J_st
	ldr	w15,[x28,J_nst]
.st:	cbz	w15,.sx
	sub	w15,w15,1
	ldr	w8,[x14,ST_nfix]
	cbz	w8,.stn
	mov	x9,0
.stf:	add	x0,x14,ST_fix
	ldr	x0,[x0,x9,lsl 3]
	mov	x1,x21
	bl	patch_bcc
	add	x9,x9,1
	cmp	x9,x8
	b.lo	.stf
	ldr	w0,[x14,ST_sofar]
	cbz	w0,.st1
	a_ADDI	20,20,w0
.st1:	mov	w0,0
	ldr	w1,[x14,ST_at]
	bl	a_mov32
	ldr	w0,[x14,ST_slot]
	cbz	w0,.st5
	ldr	w0,[x28,J_slotKind]
	cbnz	w0,.st2
	mov	w0,1
	ldr	w1,[x28,J_taken]
	bl	a_mov32
	b	.st4
.st2:	cmp	w0,2
	b.ne	.st3
	a_MOVW	1,5
	b	.st4
.st3:	mov	w0,1
	ldr	w1,[x28,J_taken]
	bl	a_mov32
	mov	w0,2
	ldr	w1,[x28,J_fall]
	bl	a_mov32
	E	0x710000BF			; cmp w5, 0
	E	0x1A821021			; csel w1, w1, w2, ne
.st4:	ldr	x1,[x19,m_jitSlot]
	bl	emit_b
	b	.stn
.st5:	ldr	x1,[x19,m_jitSide]
	bl	emit_b
.stn:	add	x14,x14,ST_SIZE
	b	.st
.sx:	; les chainages
	add	x14,x28,J_ex
	ldr	w15,[x28,J_nex]
.xl:	cbz	w15,.xe
	sub	w15,w15,1
	ldr	x0,[x14]
	mov	x1,x21
	bl	patch_b
	mov	w0,0
	ldr	w1,[x14,8]
	bl	a_mov32
	; adr x1, site
	ldr	x0,[x14]
	sub	x0,x0,x21
	and	w1,w0,3
	lsl	w1,w1,29
	ubfx	w0,w0,2,19
	orr	w1,w1,w0,lsl 5
	movl	w0,0x10000001
	orr	w0,w0,w1
	str	w0,[x21],4
	ldr	x1,[x19,m_jitChain]
	bl	emit_b
	add	x14,x14,16
	b	.xl
.xe:	ldr	x0,[sp,8]
	mov	x1,x21
	bl	flush_code
	ldr	w8,[sp]				; id
	ldr	x0,[x19,m_jitRec]
	add	x0,x0,x8,lsl 3
	ldr	w1,[x28,J_va]
	str	w1,[x0]
	ldr	x1,[sp,8]
	ldr	x2,[x19,m_jitCode]
	sub	x1,x1,x2
	str	w1,[x0,4]
	ldr	x0,[x19,m_jitInfo]
	mov	x1,12
	mul	x1,x1,x8
	add	x0,x0,x1
	ldr	w1,[x28,J_pa]
	str	w1,[x0]
	str	w10,[x0,4]
	mov	w1,-1
	str	w1,[x0,8]
	add	w0,w8,1
	str	w0,[x19,m_jitBlocks]
	ldr	x0,[x19,m_jitCompiled]
	add	x0,x0,1
	str	x0,[x19,m_jitCompiled]
	ldr	w1,[x28,J_pa]
	ldr	x0,[x19,m_jitTab]
	and	x2,x1,0xFFFFFFFC
	str	w8,[x0,x2]
	lsr	w2,w1,JP			; q
	add	w3,w13,8
	lsr	w3,w3,JP
	ldr	x0,[x19,m_jitPage]
	mov	w4,1
	mov	w5,N64_RDRAM_SIZE shr JP
.pg:	cmp	w2,w3
	b.hi	.pge
	cmp	w2,w5
	b.hs	.pge
	strb	w4,[x0,x2]
	add	w2,w2,1
	b	.pg
.pge:	str	x21,[x19,m_jitCp]
	mov	w0,w8
	add	sp,sp,64
	rest_callee
	epilogue
; l'etat de l'instruction en cours
.mark:	str	w12,[x28,J_at]
	str	w9,[x28,J_sofar]
	str	wzr,[x28,J_slot]
	mov	w0,-1
	str	w0,[x28,J_cur]
	ret

; les trampolines, au debut de la memoire de code
jit_stubs:
	prologue
	stp	x21,x22,[sp,-16]!
	ldr	x21,[x19,m_jitCode]
	str	x21,[x19,m_jitEnter]		; (x0 = code)
	E	0xA9BA7BFD			; stp x29, x30, [sp, -96]!
	E	0xA90153F3
	E	0xA9025BF5
	E	0xA90363F7
	E	0xA9046BF9
	E	0xA90573FB
	adr	x22,jit_pins
.e1:	ldrb	w2,[x22],1
	cmp	w2,0xFF
	b.eq	.e2
	adr	x0,jit_pin
	ldrb	w0,[x0,x2]
	mov	w1,19
	lsl	w2,w2,3
	movl	w3,0xF9400000
	bl	emit_ls
	b	.e1
.e2:	a_LDRX	20,19,m_cycles
	; x7 = rdram, x6 = pages
	movl	w0,0x91440267			; add x7, x19, #0x100, lsl 12
	str	w0,[x21],4
	a_LDRX	6,19,m_jitPage
	E	0xD61F0000			; br x0
	mov	x22,x21				; leave
	adr	x8,jit_pins
.l1:	ldrb	w2,[x8],1
	cmp	w2,0xFF
	b.eq	.l2
	adr	x0,jit_pin
	ldrb	w0,[x0,x2]
	mov	w1,19
	lsl	w2,w2,3
	movl	w3,0xF9000000
	bl	emit_ls
	b	.l1
.l2:	a_STRX	20,19,m_cycles
	E	0xA94153F3
	E	0xA9425BF5
	E	0xA94363F7
	E	0xA9446BF9
	E	0xA94573FB
	E	0xA8C67BFD
	E	0xD65F03C0
	str	x21,[x19,m_jitChain]
	a_STRX	1,19,m_jitSite
	str	x21,[x19,m_jitNorm]
	a_STRW	0,19,m_pc
	a_ADDIW	2,0,4
	a_STRW	2,19,m_npc
	a_STRBm	RZ,m_nextDelay
	mov	x1,x22
	bl	emit_b
	str	x21,[x19,m_jitSide]
	a_STRW	0,19,m_pc
	a_ADDIW	2,0,4
	a_STRW	2,19,m_npc
	a_STRBm	RZ,m_nextDelay
	E	0x52800022			; movz w2, 1
	a_STRW	2,19,m_jitStep
	mov	x1,x22
	bl	emit_b
	str	x21,[x19,m_jitSlot]
	a_STRW	0,19,m_pc
	a_STRW	1,19,m_npc
	E	0x52800022
	a_STRBm	2,m_nextDelay
	a_STRW	2,19,m_jitStep
	mov	x1,x22
	bl	emit_b
	str	x21,[x19,m_jitJr]
	E	0x52010001			; eor w1, w0, #0x80000000
	E	0x12027821			; and w1, w1, #0xDFFFFFFF
	E	0x7160003F			; cmp w1, #0x800, lsl 12
	mov	x4,x21				; f1
	E	0x54000000+CC_HS
	E	0x7200043F			; tst w1, #3
	mov	x5,x21				; f2
	E	0x54000000+CC_NE
	a_LDRX	2,19,m_jitTab
	a_LSX	R_LDRW,2,2,1
	mov	x8,x21				; f3
	E	0x34000000+2
	a_LDRX	3,19,m_jitRec
	E	0x8B020C63			; add x3, x3, x2, lsl 3
	a_LDRW	16,3,0
	a_LDRW	17,3,4
	a_CMPW	16,0
	mov	x9,x21				; f4
	E	0x54000000+CC_NE
	a_LDRX	3,19,m_jitCode
	a_ADD	3,3,17
	E	0xD61F0060			; br x3
	mov	x0,x4
	mov	x1,x21
	bl	patch_bcc
	mov	x0,x5
	mov	x1,x21
	bl	patch_bcc
	mov	x0,x8
	mov	x1,x21
	bl	patch_bcc
	mov	x0,x9
	mov	x1,x21
	bl	patch_bcc
	ldr	x1,[x19,m_jitNorm]
	bl	emit_b
	ldr	x0,[x19,m_jitCode]
	sub	x1,x21,x0
	str	x1,[x19,m_jitStart]
	mov	x1,x21
	bl	flush_code
	ldp	x21,x22,[sp],16
	epilogue

; (m, code : JIT_CODE_SIZE octets, lecture, ecriture, execution)
n64_jit_enable:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	str	x1,[x19,m_jitCode]
	movl	x0,MAX_BLOCKS*8
	bl	core_alloc
	str	x0,[x19,m_jitRec]
	movl	x0,MAX_BLOCKS*12
	bl	core_alloc
	str	x0,[x19,m_jitInfo]
	movl	x0,MAX_LINKS*12
	bl	core_alloc
	str	x0,[x19,m_jitLinks]
	mov	x0,N64_RDRAM_SIZE
	bl	core_alloc
	str	x0,[x19,m_jitTab]
	mov	x0,N64_RDRAM_SIZE shr JP
	bl	core_alloc
	str	x0,[x19,m_jitPage]
	bl	jit_stubs
	bl	jit_flush_all
	str	xzr,[x19,m_jitFlushes]
	mov	w0,1
	strb	w0,[x19,m_useJit]
	ldp	x19,x20,[sp],16
	epilogue

; w0 = page : ses blocs retires, leurs chainages defaits
jit_drop_page:
	stp	x29,x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldr	x1,[x19,m_jitPage]
	strb	wzr,[x1,x0]
	lsl	w21,w0,JP-2			; from
	add	w22,w21,1 shl (JP-2)		; to
	mov	w2,MAX_INS+2
	cmp	w21,w2
	csel	w0,w21,w2,lo
	sub	w21,w21,w0
	mov	w23,N64_RDRAM_SIZE/4
.l:	cmp	w21,w22
	b.hs	.f
	cmp	w21,w23
	b.hs	.f
	ldr	x0,[x19,m_jitTab]
	ldr	w24,[x0,x21,lsl 2]		; id
	cbz	w24,.n
	str	wzr,[x0,x21,lsl 2]
	mov	x1,12
	mul	x1,x24,x1
	ldr	x2,[x19,m_jitInfo]
	add	x2,x2,x1			; info
	ldr	w3,[x2,8]			; l
.c:	tbnz	w3,31,.ce
	ldr	x4,[x19,m_jitLinks]
	mov	x5,12
	sxtw	x3,w3
	mul	x5,x3,x5
	add	x4,x4,x5
	ldr	w5,[x4]				; site
	ldr	x0,[x19,m_jitCode]
	add	x0,x0,x5,lsl 2
	ldr	w6,[x4,4]
	str	w6,[x0]
	ldr	w3,[x4,8]
	stp	x2,x3,[sp,-16]!
	add	x1,x0,4
	bl	flush_code
	ldp	x2,x3,[sp],16
	b	.c
.ce:	mov	w0,-1
	str	w0,[x2,8]
	ldr	x0,[x19,m_jitRec]
	add	x0,x0,x24,lsl 3
	str	wzr,[x0]
.n:	add	w21,w21,1
	b	.l
.f:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
	ret

; w0 = pa, w1 = len
jit_wrote:
	ldr	x2,[x19,m_jitPage]
	cbz	x2,.r
	add	w1,w0,w1
	sub	w1,w1,1				; last
	mov	w3,N64_RDRAM_SIZE-1
	cmp	w1,w3
	csel	w1,w3,w1,hs
	lsr	w0,w0,JP
	lsr	w1,w1,JP
.l:	cmp	w0,w1
	b.hi	.r
	ldrb	w3,[x2,x0]
	cbnz	w3,.d
.n:	add	w0,w0,1
	b	.l
.d:	stp	x0,x1,[sp,-16]!
	stp	x2,x30,[sp,-16]!
	bl	jit_drop_page
	ldp	x2,x30,[sp],16
	ldp	x0,x1,[sp],16
	b	.n
.r:	ret

jit_run:
	prologue
	stp	x21,x22,[sp,-16]!
.l:
if defined N64_TRACE
	bl	n64_trace
end if
	ldr	x0,[x19,m_nextEvent]
	cmp	x20,x0
	b.hs	.f
	ldrb	w0,[x19,m_excPending]
	ldrb	w1,[x19,m_nextDelay]
	orr	w0,w0,w1
	ldr	w1,[x19,m_jitStep]
	orr	w0,w0,w1
	cbz	w0,.go
	str	wzr,[x19,m_jitStep]
.st:	bl	step
	b	.l
.go:	ldr	w0,[x19,m_pc]			; va
	and	w1,w0,0x1FFFFFFF		; pa
	lsr	w2,w0,30
	cmp	w2,2
	b.ne	.st
	cmp	w1,N64_RDRAM_SIZE
	b.hs	.st
	tst	w0,3
	b.ne	.st
	ldr	x2,[x19,m_jitTab]
	ldr	w21,[x2,x1]			; id
	cbz	w21,.cp
	ldr	x3,[x19,m_jitRec]
	ldr	x4,[x3,x21,lsl 3]
	cmp	w4,w0
	b.eq	.hv
	str	wzr,[x2,x1]
.cp:	bl	jit_compile
	mov	w21,w0
.hv:	mov	x0,12
	mul	x0,x21,x0
	ldr	x1,[x19,m_jitInfo]
	add	x1,x1,x0
	ldr	w1,[x1,4]			; worst
	add	x1,x20,x1
	ldr	x0,[x19,m_nextEvent]
	cmp	x1,x0
	b.hi	.st
	str	xzr,[x19,m_jitSite]
	ldr	x1,[x19,m_jitRec]
	add	x1,x1,x21,lsl 3
	ldr	w1,[x1,4]
	ldr	x0,[x19,m_jitCode]
	add	x0,x0,x1
	ldr	x1,[x19,m_jitEnter]
	str	x20,[x19,m_cycles]
	blr	x1
	ldr	x20,[x19,m_cycles]
if defined N64_TRACE
	b	.l
end if
	; chainer ce site a sa cible
	ldr	x21,[x19,m_jitSite]
	cbz	x21,.l
	ldr	w0,[x19,m_jitStep]
	cbnz	w0,.l
	ldr	x0,[x19,m_nextEvent]
	cmp	x20,x0
	b.hs	.l
	ldr	w0,[x19,m_pc]
	and	w1,w0,0x1FFFFFFF
	lsr	w2,w0,30
	cmp	w2,2
	b.ne	.l
	cmp	w1,N64_RDRAM_SIZE
	b.hs	.l
	tst	w0,3
	b.ne	.l
	ldr	x2,[x19,m_jitTab]
	ldr	w22,[x2,x1]			; t
	cbz	w22,.c1
	ldr	x3,[x19,m_jitRec]
	ldr	x4,[x3,x22,lsl 3]
	cmp	w4,w0
	csel	w22,wzr,w22,ne
.c1:	cbnz	w22,.c2
	ldr	x2,[x19,m_jitCp]
	ldr	x3,[x19,m_jitCode]
	sub	x2,x2,x3
	movl	x3,JIT_CODE_SIZE-0x20000
	cmp	x2,x3
	b.ge	.c2
	ldr	x2,[x19,m_jitFlushes]
	str	x2,[sp,-16]!
	bl	jit_compile
	mov	w22,w0
	ldr	x2,[sp],16
	ldr	x3,[x19,m_jitFlushes]
	cmp	x2,x3
	csel	x21,xzr,x21,ne
.c2:	cbz	w22,.l
	cbz	x21,.l
	ldr	w0,[x19,m_jitNlinks]
	movl	w1,MAX_LINKS-1
	cmp	w0,w1
	b.ge	.l
	ldr	x1,[x19,m_jitLinks]
	mov	x2,12
	mul	x2,x0,x2
	add	x1,x1,x2			; l
	ldr	x2,[x19,m_jitCode]
	sub	x3,x21,x2
	lsr	x3,x3,2
	str	w3,[x1]
	ldr	w3,[x21]
	str	w3,[x1,4]
	ldr	x3,[x19,m_jitInfo]
	mov	x4,12
	mul	x4,x22,x4
	add	x3,x3,x4
	ldr	w4,[x3,8]
	str	w4,[x1,8]
	str	w0,[x3,8]
	add	w0,w0,1
	str	w0,[x19,m_jitNlinks]
	ldr	x3,[x19,m_jitRec]
	add	x3,x3,x22,lsl 3
	ldr	w3,[x3,4]
	add	x1,x2,x3
	mov	x0,x21
	bl	patch_b
	mov	x0,x21
	add	x1,x21,4
	bl	flush_code
	b	.l
.f:	ldp	x21,x22,[sp],16
	epilogue
