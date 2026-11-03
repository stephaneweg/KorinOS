; ============================================================================
;  n64_gfx.inc - les taches graphiques a haut niveau : ce que font le microcode
;  F3DEX2 (F3DZEX2 d'OoT) d'une liste d'affichage et le RDP de ses commandes,
;  change en triangles d'une image pour le rendu (GPU, ou rendu logiciel).
;  Les segments, les matrices, les sommets (lumieres), TMEM et les tuiles, le
;  cache de textures, le combineur evalue par sommet, les rectangles. Avec Mesa
;  (m_glOn), ni combineur ni melange ici : n64_gl.inc garde les donnees brutes.
;
;  Convention ordinaire. x22 = rdram dans les fonctions qui lisent la RDRAM.
; ============================================================================

G_ZBUFFER	= 0x1
G_SHADE		= 0x4
G_CULL_FRONT	= 0x200
G_CULL_BACK	= 0x400
G_LIGHTING	= 0x20000
G_TEXTURE_GEN	= 0x40000
OM_ZCMP		= 0x10
OM_ZUPD		= 0x20
OM_ZMODE	= 0xC00
OM_FORCEBL	= 0x4000
GF_ZALWAYS	= 7
GF_ZLEQUAL	= 3
GF_NOZWRITE	= 1 shl 3
GF_CULL_BACK	= 1 shl 4
GF_CULL_FRONT	= 1 shl 5
GF_BLEND_ALPHA	= 1 shl 8
GF_LINEAR	= 1 shl 12
GF_WRAP_S	= 13
GF_WRAP_T	= 15
GF_NOMATRIX	= 1 shl 17
GF_ALPHATEST	= 1 shl 18

F_255		= 0x437F0000
F_31		= 0x41F80000
F_4		= 0x40800000
F_32		= 0x42000000
F_65536		= 0x47800000
F_1024		= 0x44800000
F_0_5		= 0x3F000000
F_0_01		= 0x3C23D70A
F_2E_4		= 0x3951B717

macro fconst sreg, wtmp, val {
	movl	wtmp,val
	fmov	sreg,wtmp
}
; rdH / rdB / rdW : les macros de n64_audio.inc (x22 = rdram)

; s0 -> s0 borne a 0..1 (perd s1)
macro f_clamp sreg {
	fmov	s1,wzr
	fcmp	sreg,s1
	fcsel	sreg,s1,sreg,mi
	fmov	s1,1.0
	fcmp	sreg,s1
	fcsel	sreg,s1,sreg,gt
}

; s0 -> w0 : c8 (perd s1)
f_c8:	fcmp	s0,0.0
	b.ls	.z
	fmov	s1,1.0
	fcmp	s0,s1
	b.ge	.m
	fconst	s1,w0,F_255
	fmul	s0,s0,s1
	fmov	s1,0.5
	fadd	s0,s0,s1
	fcvtzs	w0,s0
	and	w0,w0,0xFF
	ret
.z:	mov	w0,0
	ret
.m:	mov	w0,255
	ret

gfx_init:
	prologue
	add	x0,x19,m_gfxFrame
	mov	w1,2
.f:	str	wzr,[x0,GF_nv]
	str	wzr,[x0,GF_nb]
	str	wzr,[x0,GF_clear]
	mov	w2,320
	str	w2,[x0,GF_width]
	mov	w2,240
	str	w2,[x0,GF_height]
	str	wzr,[x0,GF_cimg]
	add	x0,x0,GF_SIZE
	subs	w1,w1,1
	b.ne	.f
	str	wzr,[x19,m_gfxBuild]
	mov	w0,-1
	str	w0,[x19,m_gfxReady]
	str	wzr,[x19,m_gfxSerial]
	str	wzr,[x19,m_texClock]
	adm	x0,m_texMemo
	mov	x1,8*TM_SIZE
	bl	mem_zero
	adm	x0,M_TEXS
	mov	w1,N64_MAX_TEX
.t:	stp	wzr,wzr,[x0,TX_w]
	str	xzr,[x0,TX_key]
	str	wzr,[x0,TX_lastUse]
	strb	wzr,[x0,TX_dirty]
	add	x0,x0,TX_SIZE
	subs	w1,w1,1
	b.ne	.t
	add	x0,x19,m_segment
	mov	x1,64
	bl	mem_zero
	mov	x0,0
	fmov	s0,1.0
	add	x1,x19,m_proj
	add	x2,x19,m_mtxStack
.i:	str	wzr,[x1,x0,lsl 2]
	str	wzr,[x2,x0,lsl 2]
	tst	w0,3
	and	w3,w0,15
	mov	w4,5
	udiv	w5,w3,w4
	msub	w5,w5,w4,w3
	cbnz	w5,.in
	str	s0,[x1,x0,lsl 2]
	str	s0,[x2,x0,lsl 2]
.in:	add	x0,x0,1
	cmp	x0,16
	b.lo	.i
	str	wzr,[x19,m_mtxSp]
	mov	w0,1
	strb	w0,[x19,m_mvpDirty]
	movl	x17,m_vtx
	add	x0,x19,x17
	mov	x1,64*VX_SIZE
	bl	mem_zero
	str	wzr,[x19,m_geom]
	fmov	s0,1.0
	str	s0,[x19,m_texScaleS]
	str	s0,[x19,m_texScaleT]
	str	wzr,[x19,m_texTile]
	str	wzr,[x19,m_texOn]
	add	x0,x19,m_lights
	mov	x1,8*LI_SIZE
	bl	mem_zero
	str	wzr,[x19,m_numLights]
	mov	w0,640
	strh	w0,[x19,m_vpScale]
	strh	w0,[x19,m_vpTrans]
	mov	w0,480
	strh	w0,[x19,m_vpScale+2]
	strh	w0,[x19,m_vpTrans+2]
	mov	w0,511
	strh	w0,[x19,m_vpScale+4]
	strh	w0,[x19,m_vpTrans+4]
	strh	wzr,[x19,m_vpScale+6]
	strh	wzr,[x19,m_vpTrans+6]
	str	wzr,[x19,m_rdpHalf1]
	str	wzr,[x19,m_rdpHalf2]
	str	wzr,[x19,m_fogMul]
	str	wzr,[x19,m_fogOff]
	str	wzr,[x19,m_omH]
	str	wzr,[x19,m_omL]
	str	wzr,[x19,m_combH]
	str	wzr,[x19,m_combL]
	add	x0,x19,m_primC
	mov	x1,64
	bl	mem_zero
	str	wzr,[x19,m_fillColor]
	str	wzr,[x19,m_primDepth]
	str	wzr,[x19,m_cimg]
	str	wzr,[x19,m_zimg]
	str	wzr,[x19,m_timg]
	mov	w0,320
	str	w0,[x19,m_cimgW]
	str	wzr,[x19,m_timgW]
	str	wzr,[x19,m_timgSiz]
	str	wzr,[x19,m_timgFmt]
	add	x0,x19,m_tiles
	mov	x1,8*TI_SIZE
	bl	mem_zero
	add	x0,x19,M_TMEM
	mov	x1,4096
	bl	mem_zero
	str	wzr,[x19,m_tmemSerial]
	str	wzr,[x19,m_scissor]
	str	wzr,[x19,m_scissor+4]
	mov	w0,320
	str	w0,[x19,m_scissor+8]
	mov	w0,240
	str	w0,[x19,m_scissor+12]
	mov	w0,2
	str	w0,[x19,m_cimgSiz]
	str	wzr,[x19,m_rectTile]
	strb	wzr,[x19,m_drawMain]
	strb	wzr,[x19,m_dlEnd]
	add	x0,x19,m_fbRec
	mov	x1,64
	bl	mem_zero
	str	wzr,[x19,m_fbRecNext]
	epilogue

; ---- les images du rendu, ecrites en RDRAM a la demande ----

; (m, px, w, h, stride)
n64_snapshot:
	ldr	w5,[x0,m_gfxReady]
	tbnz	w5,31,.r
	cbz	x1,.r
	cmp	w2,0
	b.le	.r
	cmp	w3,0
	b.le	.r
	mov	w6,GF_SIZE
	mul	w5,w5,w6
	add	x5,x0,x5
	add	x5,x5,m_gfxFrame
	ldr	w6,[x5,GF_width]		; W
	ldr	w7,[x5,GF_height]		; H
	cmp	w6,0
	b.le	.r
	cmp	w7,0
	b.le	.r
	cmp	w6,N64_FBW
	b.gt	.r
	cmp	w7,N64_FBH
	b.gt	.r
	movl	x8,M_SNAP
	add	x8,x0,x8			; d
	mov	w9,0				; y
.y:	mul	w10,w9,w3
	sdiv	w10,w10,w7
	sxtw	x10,w10
	mul	x10,x10,x4
	add	x10,x1,x10,lsl 2		; r
	mov	w11,0				; x
.x:	mul	w12,w11,w2
	sdiv	w12,w12,w6
	ldr	w13,[x10,w12,sxtw 2]
	str	w13,[x8],4
	add	w11,w11,1
	cmp	w11,w6
	b.lt	.x
	add	w9,w9,1
	cmp	w9,w7
	b.lt	.y
	str	w6,[x0,m_fbSnapW]
	str	w7,[x0,m_fbSnapH]
	ldr	w1,[x0,m_fbSnapSerial]
	add	w1,w1,1
	str	w1,[x0,m_fbSnapSerial]
.r:	ret

; w0 = addr, w1 = w
fb_note:
	add	x2,x19,m_fbRec
	mov	w3,4
.l:	ldp	w4,w5,[x2]
	cmp	w4,w0
	b.ne	.n
	cmp	w5,w1
	b.ne	.n
	ret
.n:	add	x2,x2,16
	subs	w3,w3,1
	b.ne	.l
	ldr	w3,[x19,m_fbRecNext]
	add	x2,x19,m_fbRec
	add	x2,x2,x3,lsl 4
	add	w3,w3,1
	and	w3,w3,3
	str	w3,[x19,m_fbRecNext]
	stp	w0,w1,[x2]
	add	w3,w1,w1,lsl 1
	lsr	w3,w3,2
	str	w3,[x2,8]
	str	wzr,[x2,12]
	ret

; w0 = addr, w1 = len
fb_writeback:
	ldr	w2,[x19,m_fbSnapSerial]
	cbz	w2,.r
	mov	w3,0				; i
.l:	add	x4,x19,m_fbRec
	add	x4,x4,x3,lsl 4			; R
	ldr	w5,[x4,4]			; w
	cbz	w5,.n
	ldr	w6,[x4,12]
	cmp	w6,w2
	b.eq	.n
	ldr	w6,[x4,8]			; h
	mul	w7,w5,w6
	lsl	w7,w7,1				; size
	ldr	w8,[x4]				; R.addr
	add	w9,w0,w1
	cmp	w9,w8
	b.ls	.n
	add	w9,w8,w7
	cmp	w0,w9
	b.hs	.n
	str	w2,[x4,12]
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	add	x22,x19,M_RDRAM
	ldr	w10,[x19,m_fbSnapW]
	ldr	w11,[x19,m_fbSnapH]
	adm	x12,M_SNAP
	mov	w13,0				; y
.y:	mul	w14,w13,w11
	udiv	w14,w14,w6
	mul	w14,w14,w10
	add	x14,x12,x14,lsl 2		; src
	mov	w15,0				; x
.x:	mul	w0,w15,w10
	udiv	w0,w0,w5
	ldr	w0,[x14,x0,lsl 2]		; c
	ubfx	w1,w0,19,5
	lsl	w1,w1,11
	ubfx	w2,w0,11,5
	orr	w1,w1,w2,lsl 6
	ubfx	w2,w0,3,5
	orr	w1,w1,w2,lsl 1
	orr	w1,w1,1
	mul	w0,w13,w5
	add	w0,w0,w15
	add	w0,w8,w0,lsl 1
	wr_s16	w1,w0
	add	w15,w15,1
	cmp	w15,w5
	b.lo	.x
	add	w13,w13,1
	cmp	w13,w6
	b.lo	.y
	ldp	x21,x22,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
.n:	add	w3,w3,1
	cmp	w3,4
	b.lo	.l
.r:	ret

; Un rectangle vers une image que le rendu ne dessine pas : par le CPU, en RDRAM.
; s0-s7 = x0 y0 x1 y1 s0 t0 s1 t1, w0 = texId, x1 = col, x2 = add, w3 = fill
RC_x0		= 0
RC_y0		= 4
RC_x1		= 8
RC_y1		= 12
RC_s0		= 16
RC_t0		= 20
RC_s1		= 24
RC_t1		= 28
RC_col		= 32
RC_add		= 40
RC_fw		= 48
RC_fh		= 52
RC_SIZE		= 64

rect_cpu:
	prologue
	save_callee
	sub	sp,sp,RC_SIZE
	ldr	w4,[x19,m_cimgSiz]
	cmp	w4,1
	b.lo	.out
	ldr	w5,[x19,m_cimgW]		; cimgW
	cbz	w5,.out
	cmp	w5,2048
	b.hi	.out
	stp	s0,s1,[sp,RC_x0]
	stp	s2,s3,[sp,RC_x1]
	stp	s4,s5,[sp,RC_s0]
	stp	s6,s7,[sp,RC_s1]
	str	x1,[sp,RC_col]
	str	x2,[sp,RC_add]
	fsub	s30,s2,s0
	str	s30,[sp,RC_fw]
	fsub	s30,s3,s1
	str	s30,[sp,RC_fh]
	fcvtzs	w21,s0				; X0
	fcvtzs	w22,s1				; Y0
	fcvtzs	w23,s2				; X1
	fcvtzs	w24,s3				; Y1
	ldr	w6,[x19,m_scissor]
	cmp	w21,w6
	csel	w21,w6,w21,lt
	ldr	w6,[x19,m_scissor+4]
	cmp	w22,w6
	csel	w22,w6,w22,lt
	ldr	w6,[x19,m_scissor+8]
	cmp	w23,w6
	csel	w23,w6,w23,gt
	ldr	w6,[x19,m_scissor+12]
	cmp	w24,w6
	csel	w24,w6,w24,gt
	cmp	w23,w5
	csel	w23,w5,w23,gt
	cmp	w21,w23
	b.ge	.out
	cmp	w22,w24
	b.ge	.out
	cmp	w24,1024
	b.gt	.out
	mov	x25,0				; T
	tbnz	w0,31,.nt
	adm	x25,M_TEXS
	add	x25,x25,x0,lsl 5
.nt:	mov	w26,w3				; fill
	add	x27,x19,M_RDRAM
	ldr	w28,[x19,m_cimg]
	cmp	w4,1
	b.ne	.c16
	; une image de 8 bits : la couverture
	mov	w6,0xFF
	cbz	w26,.y8
	ldrb	w6,[x19,m_fillColor+3]
.y8:	mov	w7,w21
.x8:	mul	w8,w22,w5
	add	w8,w8,w7
	add	w8,w8,w28
	and	w8,w8,0x7FFFFF
	eor	w8,w8,3
	strb	w6,[x27,x8]
	add	w7,w7,1
	cmp	w7,w23
	b.lt	.x8
	add	w22,w22,1
	cmp	w22,w24
	b.lt	.y8
	b	.out
.c16:	mov	w9,w21				; (x0)
.yl:	mov	w21,w9
.xl:	cbz	w26,.nf
	ldr	w0,[x19,m_fillColor]
	ldr	w4,[x19,m_cimgSiz]
	cmp	w4,3
	b.eq	.pu
	tbnz	w21,0,.fo
	lsr	w0,w0,16
	b	.pu
.fo:	and	w0,w0,0xFFFF
	b	.pu
.nf:	cbz	x25,.nt2
	; le texel
	scvtf	s0,w21
	fmov	s1,0.5
	fadd	s0,s0,s1
	ldr	s2,[sp,RC_x0]
	fsub	s0,s0,s2			; x + 0.5 - x0
	ldp	s2,s3,[sp,RC_s0]		; s0, t0
	ldp	s4,s5,[sp,RC_s1]
	fsub	s4,s4,s2
	fmul	s4,s4,s0
	ldr	s6,[sp,RC_fw]
	fmov	s7,1.0
	fcmp	s6,0.0
	fcsel	s6,s6,s7,gt
	fdiv	s4,s4,s6
	fadd	s2,s2,s4			; s
	scvtf	s0,w22
	fadd	s0,s0,s1
	ldr	s4,[sp,RC_y0]
	fsub	s0,s0,s4
	fsub	s5,s5,s3
	fmul	s5,s5,s0
	ldr	s6,[sp,RC_fh]
	fcmp	s6,0.0
	fcsel	s6,s6,s7,gt
	fdiv	s5,s5,s6
	fadd	s3,s3,s5			; t
	fcvtzs	w0,s2
	ldr	w1,[x25,TX_w]
	sdiv	w2,w0,w1
	msub	w0,w2,w1,w0
	cmp	w0,0
	b.ge	.ix
	add	w0,w0,w1
.ix:	fcvtzs	w2,s3
	ldr	w3,[x25,TX_h]
	sdiv	w4,w2,w3
	msub	w2,w4,w3,w2
	cmp	w2,0
	b.ge	.iy
	add	w2,w2,w3
.iy:	mul	w2,w2,w1
	add	w2,w2,w0
	ldr	x3,[x25,TX_px]
	ldr	w2,[x3,x2,lsl 2]		; texel
	ldr	w3,[x19,m_omL]
	and	w3,w3,3
	cmp	w3,1
	b.ne	.ca
	lsr	w3,w2,24
	cbz	w3,.nx
.ca:	fconst	s4,w3,F_255
	ubfx	w3,w2,16,8
	ucvtf	s0,w3
	fdiv	s0,s0,s4
	ubfx	w3,w2,8,8
	ucvtf	s1,w3
	fdiv	s1,s1,s4
	and	w3,w2,255
	ucvtf	s2,w3
	fdiv	s2,s2,s4
	lsr	w3,w2,24
	ucvtf	s3,w3
	fdiv	s3,s3,s4
	ldr	x0,[sp,RC_col]
	ldr	x1,[sp,RC_add]
	fmov	s6,1.0
	mov	x2,0
.cj:	ldr	s4,[x0,x2]
	ldr	s5,[x1,x2]
	cmp	x2,4
	b.eq	.c1
	cmp	x2,8
	b.eq	.c2
	cmp	x2,12
	b.eq	.c3
	fmul	s0,s0,s4
	fadd	s0,s0,s5
	fcmp	s0,s6
	fcsel	s0,s6,s0,gt
	b	.cn
.c1:	fmul	s1,s1,s4
	fadd	s1,s1,s5
	fcmp	s1,s6
	fcsel	s1,s6,s1,gt
	b	.cn
.c2:	fmul	s2,s2,s4
	fadd	s2,s2,s5
	fcmp	s2,s6
	fcsel	s2,s6,s2,gt
	b	.cn
.c3:	fmul	s3,s3,s4
	fadd	s3,s3,s5
	fcmp	s3,s6
	fcsel	s3,s6,s3,gt
.cn:	add	x2,x2,4
	cmp	x2,16
	b.lo	.cj
	b	.en
.nt2:	ldr	x0,[sp,RC_col]
	ldp	s0,s1,[x0]
	ldp	s2,s3,[x0,8]
.en:	fconst	s4,w0,F_255
	fmov	s5,0.5
	fmul	s0,s0,s4
	fadd	s0,s0,s5
	fcvtzu	w0,s0				; r
	fmul	s1,s1,s4
	fadd	s1,s1,s5
	fcvtzu	w1,s1				; g
	fmul	s2,s2,s4
	fadd	s2,s2,s5
	fcvtzu	w2,s2				; b
	fmul	s3,s3,s4
	fadd	s3,s3,s5
	fcvtzu	w3,s3				; a
	ldr	w4,[x19,m_cimgSiz]
	cmp	w4,3
	b.ne	.o16
	lsl	w0,w0,24
	orr	w0,w0,w1,lsl 16
	orr	w0,w0,w2,lsl 8
	orr	w0,w0,w3
	b	.pu
.o16:	lsr	w0,w0,3
	lsl	w0,w0,11
	lsr	w1,w1,3
	orr	w0,w0,w1,lsl 6
	lsr	w2,w2,3
	orr	w0,w0,w2,lsl 1
	cmp	w3,128
	cset	w3,hs
	orr	w0,w0,w3
.pu:	ldr	w5,[x19,m_cimgW]
	mul	w1,w22,w5
	add	w1,w1,w21
	ldr	w4,[x19,m_cimgSiz]
	cmp	w4,3
	b.ne	.p16
	add	w1,w28,w1,lsl 2
	and	w1,w1,0x7FFFFC
	str	w0,[x27,x1]
	b	.nx
.p16:	add	w1,w28,w1,lsl 1
	and	w1,w1,0x7FFFFE
	eor	w1,w1,2
	strh	w0,[x27,x1]
.nx:	add	w21,w21,1
	cmp	w21,w23
	b.lt	.xl
	add	w22,w22,1
	cmp	w22,w24
	b.lt	.yl
.out:	add	sp,sp,RC_SIZE
	rest_callee
	epilogue

; ---- la tache ----

gfx_task:
	add	x0,x19,M_DMEM
	ldr	w0,[x0,0xFF0]
	and	w0,w0,0x7FFFFF

; w0 = addr
gfx_dl:
	prologue
	save_callee
	sub	sp,sp,80			; stack [18]
	add	x22,x19,M_RDRAM
	str	w0,[x19,m_pcDl]
	mov	w21,0				; sp
	movl	w23,2000000			; n
.l:	cbz	w23,.out
	sub	w23,w23,1
	ldr	w0,[x19,m_pcDl]
	rd_w	w24,w0				; w0
	add	w0,w0,4
	rd_w	w25,w0				; w1
	add	w0,w0,4
	str	w0,[x19,m_pcDl]
	lsr	w0,w24,24
	cmp	w0,0xDF
	b.ne	.a
.pop:	cbz	w21,.out
	sub	w21,w21,1
	ldr	w0,[sp,x21,lsl 2]
	str	w0,[x19,m_pcDl]
	b	.l
.a:	cmp	w0,0xDE
	b.ne	.b
	ubfx	w0,w24,16,8
	cbnz	w0,.j
	cmp	w21,18
	b.ge	.j
	ldr	w0,[x19,m_pcDl]
	str	w0,[sp,x21,lsl 2]
	add	w21,w21,1
.j:	mov	w0,w25
	bl	gfx_seg
	str	w0,[x19,m_pcDl]
	b	.l
.b:	cmp	w0,0xE9
	b.ne	.c
	mov	w0,w24
	mov	w1,w25
	bl	gfx_cmd
	b	.out
.c:	strb	wzr,[x19,m_dlEnd]
	mov	w0,w24
	mov	w1,w25
	bl	gfx_cmd
	ldrb	w0,[x19,m_dlEnd]
	cbnz	w0,.pop
	b	.l
.out:	add	sp,sp,80
	rest_callee
	epilogue

; w0 = a -> w0 = seg (a)
gfx_seg:
	ubfx	w1,w0,24,4
	add	x2,x19,m_segment
	ldr	w1,[x2,x1,lsl 2]
	and	w0,w0,0xFFFFFF
	add	w0,w0,w1
	and	w0,w0,0x7FFFFF
	ret

; ---- les matrices ----

; x0 = a, x1 = b, x2 = out = a x b
mtx_mul:
	sub	sp,sp,64
	mov	x3,0				; i
.i:	add	x4,x0,x3,lsl 4
	ldp	s0,s1,[x4]
	ldp	s2,s3,[x4,8]
	mov	x5,0				; j
.j:	add	x6,x1,x5,lsl 2
	ldr	s4,[x6]
	fmul	s4,s0,s4
	ldr	s5,[x6,16]
	fmul	s5,s1,s5
	fadd	s4,s4,s5
	ldr	s5,[x6,32]
	fmul	s5,s2,s5
	fadd	s4,s4,s5
	ldr	s5,[x6,48]
	fmul	s5,s3,s5
	fadd	s4,s4,s5
	add	x6,x5,x3,lsl 2
	str	s4,[sp,x6,lsl 2]
	add	x5,x5,1
	cmp	x5,4
	b.lo	.j
	add	x3,x3,1
	cmp	x3,4
	b.lo	.i
	mov	x3,0
.c:	ldr	w4,[sp,x3]
	str	w4,[x2,x3]
	add	x3,x3,4
	cmp	x3,64
	b.lo	.c
	add	sp,sp,64
	ret

; ---- les sommets ----

; w0 = addr, w1 = n, w2 = v0
gfx_vtx:
	prologue
	save_callee
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	mov	w21,w0
	mov	w23,w1
	mov	w24,w2
	add	x22,x19,M_RDRAM
	ldr	w0,[x19,m_mtxSp]
	lsl	w0,w0,6
	add	x25,x19,m_mtxStack
	add	x25,x25,x0			; mv
	add	x26,x19,m_mvp
	ldrb	w0,[x19,m_mvpDirty]
	cbz	w0,.g
	mov	x0,x25
	add	x1,x19,m_proj
	mov	x2,x26
	bl	mtx_mul
	strb	wzr,[x19,m_mvpDirty]
.g:	mov	w27,0				; i
.l:	cmp	w27,w23
	b.ge	.out
	add	w0,w24,w27			; k
	cmp	w0,64
	b.hs	.out
	mov	w1,VX_SIZE
	mul	w0,w0,w1
	movl	x17,m_vtx
	add	x28,x19,x17
	add	x28,x28,x0			; v
	add	w9,w21,w27,lsl 4		; a
	rd_s16	w0,w9
	scvtf	s8,w0				; x
	add	w0,w9,2
	rd_s16	w0,w0
	scvtf	s9,w0				; y
	add	w0,w9,4
	rd_s16	w0,w0
	scvtf	s10,w0				; z
	mov	x0,0
.xf:	add	x1,x26,x0,lsl 2
	ldr	s0,[x1]
	fmul	s0,s8,s0
	ldr	s1,[x1,16]
	fmul	s1,s9,s1
	fadd	s0,s0,s1
	ldr	s1,[x1,32]
	fmul	s1,s10,s1
	fadd	s0,s0,s1
	ldr	s1,[x1,48]
	fadd	s0,s0,s1
	str	s0,[x28,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,4
	b.lo	.xf
	add	w0,w9,8
	rd_s16	w0,w0
	scvtf	s0,w0
	ldr	s1,[x19,m_texScaleS]
	fmul	s0,s0,s1
	str	s0,[x28,VX_s]
	add	w0,w9,10
	rd_s16	w0,w0
	scvtf	s0,w0
	ldr	s1,[x19,m_texScaleT]
	fmul	s0,s0,s1
	str	s0,[x28,VX_t]
	add	w0,w9,12
	rd_b	w10,w0				; c0
	add	w0,w9,13
	rd_b	w11,w0
	add	w0,w9,14
	rd_b	w12,w0
	add	w0,w9,15
	rd_b	w13,w0
	ldr	w0,[x19,m_geom]
	tst	w0,G_LIGHTING
	b.eq	.nl
	sxtb	w0,w10
	scvtf	s8,w0				; nx
	sxtb	w0,w11
	scvtf	s9,w0				; ny
	sxtb	w0,w12
	scvtf	s10,w0				; nz
	mov	x0,0
	sub	sp,sp,32
	mov	x14,sp
.tn:	add	x1,x25,x0,lsl 2
	ldr	s0,[x1]
	fmul	s0,s8,s0
	ldr	s1,[x1,16]
	fmul	s1,s9,s1
	fadd	s0,s0,s1
	ldr	s1,[x1,32]
	fmul	s1,s10,s1
	fadd	s0,s0,s1
	str	s0,[x14,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,3
	b.lo	.tn
	ldp	s8,s9,[sp]
	ldr	s10,[sp,8]			; tx ty tz
	add	sp,sp,32
	fmul	s0,s8,s8
	fmul	s1,s9,s9
	fadd	s0,s0,s1
	fmul	s1,s10,s10
	fadd	s0,s0,s1
	fsqrt	s0,s0				; l
	fcmp	s0,0.0
	b.le	.ln
	b.vs	.ln
	fdiv	s8,s8,s0
	fdiv	s9,s9,s0
	fdiv	s10,s10,s0
.ln:	ldr	w0,[x19,m_numLights]
	mov	w1,7
	cmp	w0,8
	csel	w1,w0,w1,lt
	cmp	w1,0
	mov	w3,LI_SIZE
	smull	x1,w1,w3
	add	x2,x19,m_lights
	add	x1,x2,x1			; amb
	ldp	s2,s3,[x1]			; r g
	ldr	s4,[x1,8]			; b
	mov	w3,7
	cmp	w0,7
	csel	w3,w0,w3,lt			; (j < numLights && j < 7)
.lj:	cmp	w3,0
	b.le	.le
	ldp	s5,s6,[x2,LI_x]
	ldr	s7,[x2,LI_x+8]
	fmul	s5,s8,s5
	fmul	s6,s9,s6
	fadd	s5,s5,s6
	fmul	s7,s10,s7
	fadd	s5,s5,s7			; d
	fcmp	s5,0.0
	b.le	.ls
	b.vs	.ls
	ldp	s0,s1,[x2]
	fmul	s0,s0,s5
	fadd	s2,s2,s0
	fmul	s1,s1,s5
	fadd	s3,s3,s1
	ldr	s0,[x2,8]
	fmul	s0,s0,s5
	fadd	s4,s4,s0
.ls:	add	x2,x2,LI_SIZE
	sub	w3,w3,1
	b	.lj
.le:	fmov	s0,s2
	f_clamp	s0
	str	s0,[x28,VX_r]
	fmov	s0,s3
	f_clamp	s0
	str	s0,[x28,VX_g]
	fmov	s0,s4
	f_clamp	s0
	str	s0,[x28,VX_b]
	ldr	w0,[x19,m_geom]
	tst	w0,G_TEXTURE_GEN
	b.eq	.co
	fmov	s1,1.0
	fmov	s2,0.5
	fconst	s3,w0,F_1024
	fadd	s0,s8,s1
	fmul	s0,s0,s2
	fmul	s0,s0,s3
	ldr	s4,[x19,m_texScaleS]
	fmul	s0,s0,s4
	str	s0,[x28,VX_s]
	fadd	s0,s9,s1
	fmul	s0,s0,s2
	fmul	s0,s0,s3
	ldr	s4,[x19,m_texScaleT]
	fmul	s0,s0,s4
	str	s0,[x28,VX_t]
	b	.co
.nl:	fconst	s1,w0,F_255
	ucvtf	s0,w10
	fdiv	s0,s0,s1
	str	s0,[x28,VX_r]
	ucvtf	s0,w11
	fdiv	s0,s0,s1
	str	s0,[x28,VX_g]
	ucvtf	s0,w12
	fdiv	s0,s0,s1
	str	s0,[x28,VX_b]
.co:	fconst	s1,w0,F_255
	ucvtf	s0,w13
	fdiv	s0,s0,s1
	str	s0,[x28,VX_a]
	ldp	s0,s1,[x28,VX_x]		; x y
	ldr	s2,[x28,VX_w]
	fneg	s3,s2
	mov	w0,0
	fcmp	s0,s3
	b.pl	.c1
	b.vs	.c1
	orr	w0,w0,1
.c1:	fcmp	s0,s2
	b.le	.c2
	b.vs	.c2
	orr	w0,w0,2
.c2:	fcmp	s1,s3
	b.pl	.c3
	b.vs	.c3
	orr	w0,w0,4
.c3:	fcmp	s1,s2
	b.le	.c4
	b.vs	.c4
	orr	w0,w0,8
.c4:	fconst	s3,w1,F_0_01
	fcmp	s2,s3
	b.pl	.c5
	b.vs	.c5
	orr	w0,w0,16
.c5:	str	w0,[x28,VX_clip]
	add	w27,w27,1
	b	.l
.out:	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	rest_callee
	epilogue

; ---- le combineur ----

f_ones:	dd	0x3F8000003F800000,0x3F8000003F800000
f_zeros: dd	0,0

; w0-w7 = ca cb cc cd aa ab ac ad, x8 = comb, x9 = t0, x10 = shade, x11 = out
combine_cycle:
	sub	sp,sp,64
	stp	x8,x9,[sp]
	stp	x9,xzr,[sp,16]
	add	x12,x19,m_primC
	str	x12,[sp,24]
	stp	x10,xzr,[sp,32]
	add	x12,x19,m_envC
	str	x12,[sp,40]
	adr	x12,f_ones
	str	x12,[sp,48]
	adr	x12,f_zeros
	str	x12,[sp,56]
	; A, B, D
	cmp	w0,7
	mov	w12,7
	csel	w0,w12,w0,hi
	ldr	x13,[sp,x0,lsl 3]		; A
	cmp	w1,6
	csel	w1,w12,w1,hs
	ldr	x14,[sp,x1,lsl 3]		; B
	ldr	x15,[sp,x3,lsl 3]		; D
	; C
	cmp	w2,15
	b.hi	.c0
	jtab	x2,.t
.t:	dw	.cc-.t,.ct-.t,.ct-.t,.cp-.t,.cs-.t,.ce-.t,.c1-.t,.ca-.t
	dw	.at-.t,.at-.t,.ap-.t,.as-.t,.ae-.t,.c5-.t,.c5-.t,.c0-.t
.cc:	ldp	s16,s17,[x8]
	ldr	s18,[x8,8]
	b	.mix
.ct:	ldp	s16,s17,[x9]
	ldr	s18,[x9,8]
	b	.mix
.cp:	movl	x17,m_primC
	add	x17,x19,x17
	ldp	s16,s17,[x17]
	ldr	s18,[x19,m_primC+8]
	b	.mix
.cs:	ldp	s16,s17,[x10]
	ldr	s18,[x10,8]
	b	.mix
.ce:	movl	x17,m_envC
	add	x17,x19,x17
	ldp	s16,s17,[x17]
	ldr	s18,[x19,m_envC+8]
	b	.mix
.c1:	fmov	s16,1.0
	b	.dup
.ca:	ldr	s16,[x8,12]
	b	.dup
.at:	ldr	s16,[x9,12]
	b	.dup
.ap:	ldr	s16,[x19,m_primC+12]
	b	.dup
.as:	ldr	s16,[x10,12]
	b	.dup
.ae:	ldr	s16,[x19,m_envC+12]
	b	.dup
.c5:	fmov	s16,0.5
	b	.dup
.c0:	fmov	s16,wzr
.dup:	fmov	s17,s16
	fmov	s18,s16
.mix:	ldp	s0,s1,[x13]
	ldp	s2,s3,[x14]
	ldp	s20,s21,[x15]
	fsub	s0,s0,s2
	fmul	s0,s0,s16
	fadd	s0,s0,s20
	str	s0,[x11]
	fsub	s1,s1,s3
	fmul	s1,s1,s17
	fadd	s1,s1,s21
	str	s1,[x11,4]
	ldr	s0,[x13,8]
	ldr	s2,[x14,8]
	ldr	s20,[x15,8]
	fsub	s0,s0,s2
	fmul	s0,s0,s18
	fadd	s0,s0,s20
	str	s0,[x11,8]
	; l'alpha
	mov	w12,0
	cmp	w6,6
	b.hi	.az
	b.eq	.ah
	cbz	w6,.ah
	ldr	x12,[sp,x6,lsl 3]
	ldr	s16,[x12,12]
	b	.am
.ah:	fmov	s16,0.5
	b	.am
.az:	fmov	s16,wzr
.am:	ldr	x12,[sp,x4,lsl 3]
	ldr	s0,[x12,12]
	ldr	x12,[sp,x5,lsl 3]
	ldr	s1,[x12,12]
	ldr	x12,[sp,x7,lsl 3]
	ldr	s2,[x12,12]
	fsub	s0,s0,s1
	fmul	s0,s0,s16
	fadd	s0,s0,s2
	str	s0,[x11,12]
	add	sp,sp,64
	ret

; x0 = shade, x1 = texel, x2 = out
gfx_combine:
	prologue
	save_callee
	sub	sp,sp,32			; c, r
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
	stp	xzr,xzr,[sp]
	ldr	w24,[x19,m_combH]
	ldr	w25,[x19,m_combL]
	ubfx	w0,w24,20,4			; a0
	ubfx	w1,w25,28,4			; b0
	ubfx	w2,w24,15,5			; c0
	ubfx	w3,w25,15,3			; d0
	ubfx	w4,w24,12,3			; Aa0
	ubfx	w5,w25,12,3			; Ab0
	ubfx	w6,w24,9,3			; Ac0
	ubfx	w7,w25,9,3			; Ad0
	mov	x8,sp
	mov	x9,x22
	mov	x10,x21
	add	x11,sp,16
	bl	combine_cycle
	ldr	w0,[x19,m_omH]
	ubfx	w0,w0,20,2
	cmp	w0,1
	b.ne	.f
	mov	x0,0
.c:	add	x1,sp,16
	ldr	s2,[x1,x0,lsl 2]
	fmov	s0,s2
	f_clamp	s0
	str	s0,[sp,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,4
	b.lo	.c
	ubfx	w0,w24,5,4			; a1
	ubfx	w1,w25,24,4			; b1
	and	w2,w24,31			; c1
	ubfx	w3,w25,6,3			; d1
	ubfx	w4,w25,21,3			; Aa1
	ubfx	w5,w25,3,3			; Ab1
	ubfx	w6,w25,18,3			; Ac1
	and	w7,w25,7			; Ad1
	mov	x8,sp
	mov	x9,x22
	mov	x10,x21
	add	x11,sp,16
	bl	combine_cycle
.f:	mov	x0,0
.o:	add	x1,sp,16
	ldr	s0,[x1,x0,lsl 2]
	f_clamp	s0
	str	s0,[x23,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,4
	b.lo	.o
	add	sp,sp,32
	rest_callee
	epilogue

macro is_t reg, val {			; v == 1 || v == 2 -> reg |= ...
	sub	w3,val,1
	cmp	w3,2
	cinc	reg,reg,lo
}

; -> w0 <> 0 : le combineur lit un texel
uses_texel:
	ldr	w4,[x19,m_combH]
	ldr	w5,[x19,m_combL]
	mov	w0,0
	ubfx	w1,w4,20,4
	is_t	w0,w1
	ubfx	w1,w5,28,4
	is_t	w0,w1
	ubfx	w1,w4,15,5
	is_t	w0,w1
	sub	w3,w1,8
	cmp	w3,2
	cinc	w0,w0,lo
	ubfx	w1,w5,15,3
	is_t	w0,w1
	ubfx	w1,w4,12,3
	is_t	w0,w1
	ubfx	w1,w5,12,3
	is_t	w0,w1
	ubfx	w1,w4,9,3
	is_t	w0,w1
	ubfx	w1,w5,9,3
	is_t	w0,w1
	ldr	w2,[x19,m_omH]
	ubfx	w2,w2,20,2
	cmp	w2,1
	b.ne	.r
	ubfx	w1,w4,5,4
	is_t	w0,w1
	ubfx	w1,w5,24,4
	is_t	w0,w1
	and	w1,w4,31
	is_t	w0,w1
	sub	w3,w1,8
	cmp	w3,2
	cinc	w0,w0,lo
	ubfx	w1,w5,6,3
	is_t	w0,w1
	ubfx	w1,w5,21,3
	is_t	w0,w1
	ubfx	w1,w5,3,3
	is_t	w0,w1
	ubfx	w1,w5,18,3
	is_t	w0,w1
	and	w1,w5,7
	is_t	w0,w1
.r:	ret

; -> w0 = les drapeaux du lot
gfx_flags:
	ldr	w1,[x19,m_geom]
	ldr	w2,[x19,m_omL]
	ldr	w3,[x19,m_omH]
	mov	w0,GF_ZALWAYS
	tbz	w1,0,.a
	tbz	w2,4,.a
	mov	w0,GF_ZLEQUAL
.a:	and	w4,w2,OM_ZMODE
	tbz	w1,0,.nw
	tbz	w2,5,.nw
	cmp	w4,0xC00
	b.ne	.b
.nw:	orr	w0,w0,GF_NOZWRITE
.b:	tst	w1,G_CULL_BACK
	b.eq	.c
	orr	w0,w0,GF_CULL_BACK
.c:	tst	w1,G_CULL_FRONT
	b.eq	.d
	orr	w0,w0,GF_CULL_FRONT
.d:	ubfx	w5,w3,20,2
	cmp	w5,1
	mov	w5,30
	mov	w6,28
	csel	w5,w6,w5,eq			; p
	lsr	w6,w2,w5
	and	w6,w6,3
	sub	w5,w5,4
	lsr	w7,w2,w5
	and	w7,w7,3				; a
	sub	w5,w5,4
	lsr	w8,w2,w5
	and	w8,w8,3				; m
	sub	w5,w5,4
	lsr	w9,w2,w5
	and	w9,w9,3				; b
	cmp	w4,0x800
	b.eq	.bl
	tbz	w2,14,.nb
	orr	w6,w6,w7
	orr	w6,w6,w9
	cbnz	w6,.nb
	cmp	w8,1
	b.ne	.nb
.bl:	orr	w0,w0,GF_BLEND_ALPHA
.nb:	and	w4,w2,3
	cmp	w4,1
	b.ne	.e
	ldr	s0,[x19,m_blendC+12]
	fconst	s1,w4,F_255
	fmul	s0,s0,s1
	fmov	s1,0.5
	fadd	s0,s0,s1
	fcvtzu	w4,s0
	mov	w5,1
	cmp	w4,0
	csel	w4,w5,w4,eq
	orr	w0,w0,GF_ALPHATEST
	orr	w0,w0,w4,lsl 19
.e:	ubfx	w3,w3,12,2
	cmp	w3,2
	b.ne	.r
	orr	w0,w0,GF_LINEAR
.r:	ret

; ---- les textures ----

; w0 = c -> w0
tx_rgba16:
	ubfx	w1,w0,11,5
	ubfx	w2,w0,6,5
	ubfx	w3,w0,1,5
	lsl	w4,w1,19
	orr	w4,w4,w1,lsl 14
	and	w4,w4,0xFF0000
	lsl	w1,w2,11
	orr	w1,w1,w2,lsl 6
	and	w1,w1,0xFF00
	orr	w4,w4,w1
	lsl	w1,w3,3
	orr	w1,w1,w3,lsr 2
	orr	w4,w4,w1
	tbz	w0,0,.a
	orr	w4,w4,0xFF000000
.a:	mov	w0,w4
	ret
; w0 = i, w1 = a -> w0
tx_ia:	lsl	w1,w1,24
	orr	w1,w1,w0,lsl 16
	orr	w1,w1,w0,lsl 8
	orr	w0,w1,w0
	ret

; w0 = pa -> w0 : e de la palette en TMEM ; w27 = tlut
tx_pal:	and	w0,w0,0xFFF
	ldrb	w1,[x28,x0]
	add	x0,x28,x0
	ldrb	w2,[x0,1]
	orr	w0,w2,w1,lsl 8
	cmp	w27,3
	b.ne	tx_rgba16
	and	w1,w0,0xFF
	lsr	w0,w0,8
	b	tx_ia

macro key_mix reg {
	eor	x9,x9,reg
	mul	x9,x9,x10
}

; w0 = tuile -> w0 = texture
gfx_texture:
	prologue
	save_callee
	sub	sp,sp,32
	and	w0,w0,7
	str	w0,[sp,24]
	mov	w1,TI_SIZE
	mul	w0,w0,w1
	add	x21,x19,m_tiles
	add	x21,x21,x0			; T
	ldr	w22,[x21,TI_masks]		; w
	cbz	w22,.w0
	mov	w0,1
	lsl	w22,w0,w22
	b	.w1
.w0:	ldr	w0,[x21,TI_sh]
	ldr	w1,[x21,TI_sl]
	sub	w0,w0,w1
	asr	w0,w0,2
	add	w22,w0,1
.w1:	ldr	w23,[x21,TI_maskt]		; h
	cbz	w23,.h0
	mov	w0,1
	lsl	w23,w0,w23
	b	.h1
.h0:	ldr	w0,[x21,TI_th]
	ldr	w1,[x21,TI_tl]
	sub	w0,w0,w1
	asr	w0,w0,2
	add	w23,w0,1
.h1:	mov	w0,1
	cmp	w22,1
	csel	w22,w0,w22,lt
	cmp	w23,1
	csel	w23,w0,w23,lt
.sz:	mul	w0,w22,w23
	cmp	w0,N64_TEX_PIXELS
	b.le	.szk
	cmp	w23,1
	b.le	.szw
	asr	w23,w23,1
	b	.sz
.szw:	asr	w22,w22,1
	b	.sz
.szk:	ldr	w27,[x19,m_omH]
	ubfx	w27,w27,14,2			; tlut
	ldr	w0,[sp,24]			; la meme tuile sur la meme TMEM : la meme texture, sans la hacher
	adm	x12,m_texMemo
	add	x12,x12,x0,lsl 6
	ldr	w0,[x12,TM_id]
	cbz	w0,.hash
	ldr	w1,[x12,TM_serial]
	ldr	w2,[x19,m_tmemSerial]
	cmp	w1,w2
	b.ne	.hash
	ldp	w1,w2,[x12]
	ldp	w3,w4,[x21]
	cmp	w1,w3
	b.ne	.hash
	cmp	w2,w4
	b.ne	.hash
	ldp	w1,w2,[x12,8]
	ldp	w3,w4,[x21,8]
	cmp	w1,w3
	b.ne	.hash
	cmp	w2,w4
	b.ne	.hash
	ldr	w1,[x12,TI_pal]
	ldr	w2,[x21,TI_pal]
	cmp	w1,w2
	b.ne	.hash
	ldp	w1,w2,[x12,TM_w]
	cmp	w1,w22
	b.ne	.hash
	cmp	w2,w23
	b.ne	.hash
	ldr	w1,[x12,TM_tlut]
	cmp	w1,w27
	b.ne	.hash
	sub	w0,w0,1
	adm	x13,M_TEXS
	add	x13,x13,x0,lsl 5
	ldr	x1,[x13,TX_key]
	ldr	x2,[x12,TM_key]
	cmp	x1,x2
	b.ne	.hash
	ldp	w1,w2,[x13,TX_w]
	cmp	w1,w22
	b.ne	.hash
	cmp	w2,w23
	b.ne	.hash
	ldr	w1,[x19,m_texClock]
	add	w1,w1,1
	str	w1,[x19,m_texClock]
	ldr	w1,[x19,m_gfxSerial]
	str	w1,[x13,TX_lastUse]
	b	.out
.hash:	add	x28,x19,M_TMEM
	movl	x9,1469598103934665603
	movl	x10,1099511628211
	ldr	w0,[x21,TI_fmt]
	key_mix	x0
	ldr	w0,[x21,TI_siz]
	key_mix	x0
	ldr	w0,[x21,TI_line]
	key_mix	x0
	ldr	w0,[x21,TI_tmem]
	key_mix	x0
	ldr	w0,[x21,TI_pal]
	key_mix	x0
	mov	w0,w22
	key_mix	x0
	mov	w0,w23
	key_mix	x0
	mov	w0,w27
	key_mix	x0
	ldr	w24,[x21,TI_line]
	lsl	w24,w24,3			; lineBytes
	ldr	w25,[x21,TI_tmem]
	lsl	w25,w25,3			; base
	mul	w26,w24,w23			; span
	cmp	w26,4096
	b.hi	.sp
	cbnz	w26,.spk
.sp:	mov	w26,4096
.spk:	mov	w1,0
.m1:	cmp	w1,w26
	b.hs	.m2
	add	w2,w25,w1
	and	w2,w2,0xFFC
	ldr	w0,[x28,x2]
	rev	w0,w0
	key_mix	x0
	add	w1,w1,4
	b	.m1
.m2:	ldr	w0,[x21,TI_siz]
	cmp	w0,3
	b.ne	.m3
	mov	w1,0
.m2l:	cmp	w1,w26
	b.hs	.m3
	add	w2,w25,w1
	add	w2,w2,0x800
	and	w2,w2,0xFFC
	ldr	w0,[x28,x2]
	rev	w0,w0
	key_mix	x0
	add	w1,w1,4
	b	.m2l
.m3:	ldr	w0,[x21,TI_fmt]
	cmp	w0,2
	b.ne	.m4
	mov	w1,0x800
.m3l:	ldrh	w0,[x28,x1]
	rev16	w0,w0
	key_mix	x0
	add	w1,w1,8
	cmp	w1,0x1000
	b.lo	.m3l
.m4:	ldr	w0,[x19,m_texClock]
	add	w0,w0,1
	str	w0,[x19,m_texClock]
	mov	w11,0				; victim
	mov	w12,-1				; oldest
	adm	x13,M_TEXS
	mov	x14,0				; i
	ldr	w15,[x19,m_gfxSerial]
.f:	ldr	x0,[x13,TX_key]
	ldp	w1,w2,[x13,TX_w]
	cmp	x0,x9
	b.ne	.f1
	cmp	w1,w22
	b.ne	.f1
	cmp	w2,w23
	b.ne	.f1
	str	w15,[x13,TX_lastUse]
	ldr	w0,[sp,24]
	adm	x12,m_texMemo
	add	x12,x12,x0,lsl 6
	ldp	w0,w1,[x21]
	stp	w0,w1,[x12]
	ldp	w0,w1,[x21,8]
	stp	w0,w1,[x12,8]
	ldr	w0,[x21,TI_pal]
	str	w0,[x12,TI_pal]
	stp	w22,w23,[x12,TM_w]
	str	w27,[x12,TM_tlut]
	ldr	w0,[x19,m_tmemSerial]
	str	w0,[x12,TM_serial]
	add	w0,w14,1
	str	w0,[x12,TM_id]
	str	x9,[x12,TM_key]
	mov	w0,w14
	b	.out
.f1:	cbnz	w1,.f2
	mov	w11,w14
	mov	w12,0
	b	.f3
.f2:	cbz	w12,.f3
	ldr	w0,[x13,TX_lastUse]
	cmp	w0,w12
	b.hs	.f3
	mov	w12,w0
	mov	w11,w14
.f3:	add	x13,x13,TX_SIZE
	add	x14,x14,1
	cmp	x14,N64_MAX_TEX
	b.lo	.f
	; depuis TMEM
	str	w11,[sp]
	adm	x13,M_TEXS
	add	x13,x13,x11,lsl 5		; X
	str	x9,[x13,TX_key]
	stp	w22,w23,[x13,TX_w]
	str	w15,[x13,TX_lastUse]
	mov	w0,1
	strb	w0,[x13,TX_dirty]
	ldr	x14,[x13,TX_px]			; sortie
	ldr	w9,[x21,TI_siz]
	ldr	w10,[x21,TI_fmt]
	ldr	w11,[x21,TI_pal]
	mov	w12,0				; y
.y:	mul	w13,w12,w24
	add	w13,w13,w25			; row
	ubfx	w15,w12,0,1
	lsl	w15,w15,2			; sw
	mov	w26,0				; x
.x:	cmp	w9,1
	b.lo	.s4
	b.eq	.s8
	cmp	w9,2
	b.eq	.s16
	; 32 bits
	add	w0,w13,w26,lsl 1
	eor	w0,w0,w15
	and	w0,w0,0x7FE
	add	x1,x28,x0
	ldrb	w2,[x1]
	ldrb	w3,[x1,1]
	add	x1,x1,0x800
	ldrb	w4,[x1]
	ldrb	w5,[x1,1]
	lsl	w0,w5,24
	orr	w0,w0,w2,lsl 16
	orr	w0,w0,w3,lsl 8
	orr	w0,w0,w4
	b	.put
.s4:	add	w0,w13,w26,lsr 1
	eor	w0,w0,w15
	and	w0,w0,0xFFF
	ldrb	w0,[x28,x0]
	lsr	w1,w0,4
	tbz	w26,0,.n4
	and	w1,w0,15			; n
.n4:	cmp	w10,2
	b.ne	.s41
	orr	w0,w1,w11,lsl 4
	lsl	w0,w0,3
	add	w0,w0,0x800
	bl	tx_pal
	b	.put
.s41:	cmp	w10,3
	b.ne	.s42
	lsr	w0,w1,1
	mov	w2,36
	mul	w0,w0,w2
	mov	w2,0
	tbz	w1,0,.s41a
	mov	w2,255
.s41a:	mov	w1,w2
	bl	tx_ia
	b	.put
.s42:	add	w0,w1,w1,lsl 4
	mov	w1,w0
	bl	tx_ia
	b	.put
.s8:	add	w0,w13,w26
	eor	w0,w0,w15
	and	w0,w0,0xFFF
	ldrb	w0,[x28,x0]
	cmp	w10,2
	b.ne	.s81
	lsl	w0,w0,3
	add	w0,w0,0x800
	bl	tx_pal
	b	.put
.s81:	cmp	w10,3
	b.ne	.s82
	lsr	w1,w0,4
	add	w1,w1,w1,lsl 4
	and	w2,w0,15
	add	w2,w2,w2,lsl 4
	mov	w0,w1
	mov	w1,w2
	bl	tx_ia
	b	.put
.s82:	mov	w1,w0
	bl	tx_ia
	b	.put
.s16:	add	w0,w13,w26,lsl 1
	eor	w0,w0,w15
	and	w0,w0,0xFFE
	ldrh	w0,[x28,x0]
	rev16	w0,w0
	cmp	w10,3
	b.ne	.s161
	and	w1,w0,0xFF
	lsr	w0,w0,8
	bl	tx_ia
	b	.put
.s161:	bl	tx_rgba16
.put:	str	w0,[x14],4
	add	w26,w26,1
	cmp	w26,w22
	b.lt	.x
	add	w12,w12,1
	cmp	w12,w23
	b.lt	.y
	ldr	w0,[sp]
.out:	add	sp,sp,32
	rest_callee
	epilogue

; w0, w1 : la commande, w2 = 0 LOADBLOCK, 1 LOADTILE, 2 LOADTLUT
gfx_load:
	prologue
	save_callee
	mov	w21,w0
	mov	w23,w1
	mov	w24,w2
	add	x22,x19,M_RDRAM
	ubfx	w0,w23,24,3
	mov	w1,TI_SIZE
	mul	w0,w0,w1
	add	x25,x19,m_tiles
	add	x25,x25,x0			; T
	ubfx	w26,w21,12,12			; uls
	and	w27,w21,0xFFF			; ult
	ubfx	w28,w23,12,12			; lrs
	and	w23,w23,0xFFF			; lrt
	ldr	w0,[x25,TI_tmem]
	lsl	w10,w0,3			; dst
	ldr	w0,[x19,m_tmemSerial]
	add	w0,w0,1
	str	w0,[x19,m_tmemSerial]
	ldr	w11,[x19,m_timgSiz]
	ldr	w12,[x19,m_timgW]
	ldr	w13,[x19,m_timg]
	mov	w0,1				; bpp2
	cbz	w11,.b1
	sub	w0,w11,1
	mov	w1,1
	lsl	w1,w1,w0
	lsl	w0,w1,1
.b1:	lsr	w1,w27,2
	mul	w1,w1,w12
	mul	w1,w1,w0
	add	w2,w13,w1,lsr 1			; lo
	lsr	w1,w23,2
	add	w1,w1,1
	mul	w1,w1,w12
	mul	w1,w1,w0
	add	w3,w13,w1,lsr 1			; hi
	cbnz	w24,.b2
	mov	w2,w13
	add	w1,w28,1
	mul	w1,w1,w0
	add	w3,w13,w1,lsr 1
	add	w3,w3,8
.b2:	mov	w1,8
	sub	w4,w3,w2
	cmp	w3,w2
	csel	w1,w4,w1,hi
	mov	w0,w2
	stp	x10,x11,[sp,-16]!
	stp	x12,x13,[sp,-16]!
	bl	fb_writeback
	ldp	x12,x13,[sp],16
	ldp	x10,x11,[sp],16
	add	x14,x19,M_TMEM
	cmp	w24,2
	b.ne	.nt
	; la palette : chaque entree 4 fois
	lsr	w0,w28,2
	lsr	w1,w26,2
	sub	w0,w0,w1
	add	w0,w0,1				; n
	lsr	w2,w27,2
	mul	w2,w2,w12
	add	w2,w2,w1
	add	w2,w13,w2,lsl 1			; src
	mov	w3,0				; i
.p:	cmp	w3,w0
	b.ge	.out
	cmp	w3,256
	b.ge	.out
	add	w4,w2,w3,lsl 1
	rd_s16	w4,w4
	mov	w5,0				; k
.pk:	add	w6,w10,w3,lsl 3
	add	w6,w6,w5,lsl 1
	and	w6,w6,0xFFF
	ubfx	w7,w4,8,8
	strb	w7,[x14,x6]
	add	x6,x14,x6
	strb	w4,[x6,1]
	add	w5,w5,1
	cmp	w5,4
	b.lo	.pk
	add	w3,w3,1
	b	.p
.nt:	cbnz	w24,.tile
	; un bloc
	mov	w0,w23				; dxt
	sub	w1,w28,w26
	add	w1,w1,1
	mul	w2,w27,w12
	add	w2,w2,w26
	cbnz	w11,.bs
	lsr	w1,w1,1				; bytes
	lsr	w2,w2,1
	b	.bo
.bs:	sub	w3,w11,1
	lsl	w1,w1,w3
	lsl	w2,w2,w3
.bo:	add	w2,w2,w13			; src
	add	w1,w1,7
	lsr	w1,w1,3				; words
	mov	w3,0				; i
.bl:	cmp	w3,w1
	b.hs	.out
	mul	w4,w3,w0
	ubfx	w4,w4,11,1			; odd
	lsl	w4,w4,2				; 0 / 4
	add	w5,w2,w3,lsl 3			; b
	cmp	w11,3
	b.ne	.b8
	mov	w6,0				; k
.b32:	add	w7,w10,w3,lsl 2
	add	w7,w7,w6,lsl 1
	eor	w7,w7,w4
	and	w7,w7,0x7FE
	add	x8,x14,x7
	add	w9,w5,w6,lsl 2
	rd_b	w15,w9
	strb	w15,[x8]
	add	w9,w9,1
	rd_b	w15,w9
	strb	w15,[x8,1]
	add	x8,x8,0x800
	add	w9,w9,1
	rd_b	w15,w9
	strb	w15,[x8]
	add	w9,w9,1
	rd_b	w15,w9
	strb	w15,[x8,1]
	add	w6,w6,1
	cmp	w6,2
	b.lo	.b32
	b	.bn
.b8:	mov	w6,0
.b8l:	add	w7,w5,w6
	rd_b	w15,w7
	add	w7,w10,w3,lsl 3
	add	w7,w7,w6
	and	w7,w7,0xFFF
	eor	w7,w7,w4
	strb	w15,[x14,x7]
	add	w6,w6,1
	cmp	w6,8
	b.lo	.b8l
.bn:	add	w3,w3,1
	b	.bl
.tile:	asr	w0,w26,2			; s0
	asr	w1,w27,2			; t0
	asr	w2,w28,2			; s1
	asr	w3,w23,2			; t1
	ldr	w4,[x25,TI_line]
	lsl	w4,w4,3				; lineBytes
	mov	w5,w1				; y
.ty:	cmp	w5,w3
	b.gt	.out
	sub	w6,w5,w1
	mul	w7,w6,w4
	add	w7,w7,w10			; row
	and	w6,w6,1
	lsl	w6,w6,2				; sw
	cmp	w11,3
	b.ne	.tb
	mov	w8,w0				; x
.tx:	cmp	w8,w2
	b.gt	.tn
	mul	w9,w5,w12
	add	w9,w9,w8
	add	w9,w13,w9,lsl 2			; src
	sub	w15,w8,w0
	add	w15,w7,w15,lsl 1
	eor	w15,w15,w6
	and	w15,w15,0x7FE
	add	x15,x14,x15
	rd_b	w17,w9
	strb	w17,[x15]
	add	w9,w9,1
	rd_b	w17,w9
	strb	w17,[x15,1]
	add	x15,x15,0x800
	add	w9,w9,1
	rd_b	w17,w9
	strb	w17,[x15]
	add	w9,w9,1
	rd_b	w17,w9
	strb	w17,[x15,1]
	add	w8,w8,1
	b	.tx
.tb:	sub	w8,w2,w0
	add	w8,w8,1				; bytesRow
	mul	w9,w5,w12
	add	w9,w9,w0
	cbnz	w11,.tb1
	lsr	w8,w8,1
	lsr	w9,w9,1
	b	.tb2
.tb1:	sub	w15,w11,1
	lsl	w8,w8,w15
	lsl	w9,w9,w15
.tb2:	add	w9,w9,w13			; src
	mov	w15,0
.tl:	cmp	w15,w8
	b.hs	.tn
	add	w17,w9,w15
	rd_b	w17,w17
	str	w17,[sp,-16]!
	add	w17,w7,w15
	eor	w17,w17,w6
	and	w17,w17,0xFFF
	ldr	w16,[sp],16
	strb	w16,[x14,x17]
	add	w15,w15,1
	b	.tl
.tn:	add	w5,w5,1
	b	.ty
.out:	rest_callee
	epilogue

; ---- la sortie ----

; x0 = v3, w1 = tex, w2 = drapeaux
gfx_emit:
	ldr	w3,[x19,m_gfxBuild]
	mov	w4,GF_SIZE
	mul	w3,w3,w4
	add	x3,x19,x3
	add	x3,x3,m_gfxFrame		; F
	ldr	w4,[x3,GF_nv]
	add	w5,w4,3
	movl	w6,N64_MAXV
	cmp	w5,w6
	b.gt	.r
	mov	w5,GF_NOMATRIX
	orr	w2,w2,w5			; f
	ldr	w5,[x3,GF_nb]
	ldr	x6,[x3,GF_b]
	cbz	w5,.new
	sub	w7,w5,1
	mov	w8,GB_SIZE
	mul	w7,w7,w8
	add	x7,x6,x7			; dernier
	ldr	w8,[x7,GB_tex]
	cmp	w8,w1
	b.ne	.new
	ldr	w8,[x7,GB_flags]
	cmp	w8,w2
	b.ne	.new
	ldr	w8,[x7,GB_first]
	ldr	w9,[x7,GB_count]
	add	w8,w8,w9
	cmp	w8,w4
	b.eq	.copy
.new:	cmp	w5,N64_MAXB
	b.ge	.r
	mov	w8,GB_SIZE
	mul	w8,w5,w8
	add	x7,x6,x8
	add	w5,w5,1
	str	w5,[x3,GF_nb]
	str	w4,[x7,GB_first]
	str	wzr,[x7,GB_count]
	str	w1,[x7,GB_tex]
	str	w2,[x7,GB_flags]
	fmov	s0,1.0
	add	x8,x7,GB_m
	mov	w9,0
.m:	str	wzr,[x8,x9,lsl 2]
	cmp	w9,0
	b.eq	.m1
	cmp	w9,5
	b.eq	.m1
	cmp	w9,10
	b.eq	.m1
	cmp	w9,15
	b.ne	.m2
.m1:	str	s0,[x8,x9,lsl 2]
.m2:	add	w9,w9,1
	cmp	w9,16
	b.lo	.m
.copy:	ldr	x6,[x3,GF_v]
	add	x6,x6,x4,lsl 5
	ldp	x8,x9,[x0]
	stp	x8,x9,[x6]
	ldp	x8,x9,[x0,16]
	stp	x8,x9,[x6,16]
	ldp	x8,x9,[x0,32]
	stp	x8,x9,[x6,32]
	ldp	x8,x9,[x0,48]
	stp	x8,x9,[x6,48]
	ldp	x8,x9,[x0,64]
	stp	x8,x9,[x6,64]
	ldp	x8,x9,[x0,80]
	stp	x8,x9,[x6,80]
	add	w4,w4,3
	str	w4,[x3,GF_nv]
	ldr	w4,[x7,GB_count]
	add	w4,w4,3
	str	w4,[x7,GB_count]
.r:	ret

; TRI : variables sur la pile
GT_V		= 0			; 3 pointeurs
GT_o		= 24			; 3 sommets
GT_shade	= 120
GT_c0		= 136
GT_c1		= 152
GT_kx		= 168			; g.x = v.x * kx + v.w * kxw
GT_kxw		= 172
GT_ky		= 176
GT_kyw		= 180
GT_tw		= 184
GT_th		= 188
GT_zb		= 192
GT_SIZE		= 208

f_black: dd	0,0
; (blanc : f_ones)

; w0, w1, w2 = les sommets
gfx_tri:
	ldrb	w3,[x19,m_drawMain]
	cbnz	w3,.go
	ret
.go:	prologue
	save_callee
	sub	sp,sp,GT_SIZE
	movl	x17,m_vtx
	add	x3,x19,x17
	mov	w4,VX_SIZE
	and	w0,w0,63
	mul	w0,w0,w4
	add	x5,x3,x0
	and	w1,w1,63
	mul	w1,w1,w4
	add	x6,x3,x1
	and	w2,w2,63
	mul	w2,w2,w4
	add	x7,x3,x2
	stp	x5,x6,[sp,GT_V]
	str	x7,[sp,GT_V+16]
	ldr	w0,[x5,VX_clip]
	ldr	w1,[x6,VX_clip]
	and	w0,w0,w1
	ldr	w1,[x7,VX_clip]
	and	w0,w0,w1
	tst	w0,15
	b.ne	.out
	ldr	w0,[x19,m_gfxBuild]
	mov	w1,GF_SIZE
	mul	w0,w0,w1
	add	x25,x19,x0
	add	x25,x25,m_gfxFrame		; F
	ldr	s0,[x25,GF_width]
	scvtf	s0,s0				; W
	ldr	s1,[x25,GF_height]
	scvtf	s1,s1				; H
	fmov	s2,2.0
	fdiv	s3,s0,s2			; W / 2
	fdiv	s4,s1,s2			; H / 2
	fconst	s5,w0,F_4
	ldrsh	w0,[x19,m_vpScale]
	scvtf	s6,w0
	fdiv	s6,s6,s5			; sx
	fdiv	s6,s6,s3
	str	s6,[sp,GT_kx]
	ldrsh	w0,[x19,m_vpScale+2]
	scvtf	s6,w0
	fdiv	s6,s6,s5
	fdiv	s6,s6,s4
	str	s6,[sp,GT_ky]
	fmov	s7,1.0
	ldrsh	w0,[x19,m_vpTrans]
	scvtf	s6,w0
	fdiv	s6,s6,s5
	fdiv	s6,s6,s3
	fsub	s6,s6,s7
	str	s6,[sp,GT_kxw]
	ldrsh	w0,[x19,m_vpTrans+2]
	scvtf	s6,w0
	fdiv	s6,s6,s5
	fdiv	s6,s6,s4
	fsub	s6,s7,s6
	str	s6,[sp,GT_kyw]
	mov	w21,0				; textured
	ldr	w0,[x19,m_texOn]
	cbz	w0,.nt
	bl	uses_texel
	cmp	w0,0
	cset	w21,ne
.nt:	mov	w22,-1				; texId
	fmov	s0,1.0
	str	s0,[sp,GT_tw]
	str	s0,[sp,GT_th]
	mov	x23,0				; T
	bl	gfx_flags
	mov	w24,w0				; flags
	ldr	w0,[x19,m_glOn]
	cbz	w0,.nogl
	ldr	w0,[x19,m_texTile]
	ldr	w1,[x19,m_texOn]
	bl	gl_state
	b	.nt2
.nogl:	cbz	w21,.nt2
	ldr	w0,[x19,m_texTile]
	bl	gfx_texture
	mov	w22,w0
	ldr	w0,[x19,m_texTile]
	and	w0,w0,7
	mov	w1,TI_SIZE
	mul	w0,w0,w1
	add	x23,x19,m_tiles
	add	x23,x23,x0
	adm	x0,M_TEXS
	add	x0,x0,x22,lsl 5
	ldr	s0,[x0,TX_w]
	scvtf	s0,s0
	str	s0,[sp,GT_tw]
	ldr	s0,[x0,TX_h]
	scvtf	s0,s0
	str	s0,[sp,GT_th]
	bl	.wrap
	orr	w24,w24,w0
.nt2:	str	wzr,[sp,GT_zb]
	ldr	w0,[x19,m_omL]
	and	w0,w0,OM_ZMODE
	cmp	w0,0xC00
	b.ne	.zk
	fconst	s0,w0,F_2E_4
	str	s0,[sp,GT_zb]
.zk:	mov	x26,0				; k
.v:	add	x0,sp,GT_V
	ldr	x27,[x0,x26,lsl 3]		; v
	mov	x0,GV_SIZE
	mul	x0,x26,x0
	add	x28,sp,GT_o
	add	x28,x28,x0			; g
	ldp	s0,s1,[x27,VX_x]
	ldp	s2,s3,[x27,VX_z]		; z w
	ldr	s4,[sp,GT_kx]
	fmul	s4,s0,s4
	ldr	s5,[sp,GT_kxw]
	fmul	s5,s3,s5
	fadd	s4,s4,s5
	str	s4,[x28]
	ldr	s4,[sp,GT_ky]
	fmul	s4,s1,s4
	ldr	s5,[sp,GT_kyw]
	fmul	s5,s3,s5
	fadd	s4,s4,s5
	str	s4,[x28,4]
	ldr	s4,[sp,GT_zb]
	fmul	s4,s4,s3
	fsub	s4,s2,s4
	str	s4,[x28,8]
	str	s3,[x28,12]
	ldr	w0,[x19,m_glOn]
	cbz	w0,.nogv
	bl	gl_vertex
	b	.vn
.nogv:	add	x0,sp,GT_shade
	ldr	w1,[x19,m_geom]
	tst	w1,G_SHADE
	b.eq	.sh1
	add	x1,x27,VX_r
	ldp	x2,x3,[x1]
	stp	x2,x3,[x0]
	b	.sh2
.sh1:	adr	x1,f_ones
	ldp	x2,x3,[x1]
	stp	x2,x3,[x0]
.sh2:	adr	x1,f_black
	add	x2,sp,GT_c0
	bl	gfx_combine
	cbz	w21,.nc
	add	x0,sp,GT_shade
	adr	x1,f_ones
	add	x2,sp,GT_c1
	bl	gfx_combine
	mov	x9,0
.c1:	add	x0,sp,GT_c1
	ldr	s0,[x0,x9,lsl 2]
	add	x0,sp,GT_c0
	ldr	s1,[x0,x9,lsl 2]
	fsub	s0,s0,s1
	fmov	s1,wzr
	fcmp	s0,s1
	fcsel	s0,s1,s0,mi
	bl	f_c8
	add	x1,x28,GV_r
	strb	w0,[x1,x9]
	add	x0,sp,GT_c0
	ldr	s0,[x0,x9,lsl 2]
	bl	f_c8
	add	x1,x28,GV_r2
	strb	w0,[x1,x9]
	add	x9,x9,1
	cmp	x9,4
	b.lo	.c1
	; s, t
	ldp	s0,s1,[x27,VX_s]
	fconst	s2,w0,F_32
	fdiv	s0,s0,s2			; u
	fdiv	s1,s1,s2			; w
	ldr	w0,[x23,TI_shifts]
	bl	.shf0
	ldr	w0,[x23,TI_shiftt]
	bl	.shf1
	fconst	s2,w0,F_4
	ldr	w0,[x23,TI_sl]
	scvtf	s3,w0
	fdiv	s3,s3,s2
	fsub	s0,s0,s3
	ldr	w0,[x23,TI_tl]
	scvtf	s3,w0
	fdiv	s3,s3,s2
	fsub	s1,s1,s3
	ldr	s2,[sp,GT_tw]
	fdiv	s0,s0,s2
	ldr	s2,[sp,GT_th]
	fdiv	s1,s1,s2
	stp	s0,s1,[x28,GV_s]
	b	.vn
.nc:	mov	x9,0
.c0:	add	x0,sp,GT_c0
	ldr	s0,[x0,x9,lsl 2]
	bl	f_c8
	add	x1,x28,GV_r
	strb	w0,[x1,x9]
	add	x1,x28,GV_r2
	strb	wzr,[x1,x9]
	add	x9,x9,1
	cmp	x9,4
	b.lo	.c0
	stp	wzr,wzr,[x28,GV_s]
.vn:	add	x26,x26,1
	cmp	x26,3
	b.lo	.v
	add	x0,sp,GT_o
	mov	w1,w22
	mov	w2,w24
	ldr	w3,[x19,m_glOn]
	cbz	w3,.noge
	bl	gl_emit
	b	.out
.noge:	bl	gfx_emit
.out:	add	sp,sp,GT_SIZE
	rest_callee
	epilogue
; w0 = shifts sur s0
.shf0:	cbz	w0,.sr
	cmp	w0,10
	b.gt	.s1
	mov	w1,1
	lsl	w1,w1,w0
	scvtf	s2,w1
	fdiv	s0,s0,s2
	ret
.s1:	mov	w1,16
	sub	w0,w1,w0
	mov	w1,1
	lsl	w1,w1,w0
	scvtf	s2,w1
	fmul	s0,s0,s2
.sr:	ret
.shf1:	cbz	w0,.sr
	cmp	w0,10
	b.gt	.s2
	mov	w1,1
	lsl	w1,w1,w0
	scvtf	s2,w1
	fdiv	s1,s1,s2
	ret
.s2:	mov	w1,16
	sub	w0,w1,w0
	mov	w1,1
	lsl	w1,w1,w0
	scvtf	s2,w1
	fmul	s1,s1,s2
	ret
; x23 = T -> w0 = les bits de repli
.wrap:	mov	w0,0				; ws
	ldr	w1,[x23,TI_cms]
	tbnz	w1,1,.w1
	tbz	w1,0,.w2
	mov	w0,2
	b	.w2
.w1:	mov	w0,1
.w2:	ldr	w1,[x23,TI_masks]
	mov	w2,1
	cmp	w1,0
	csel	w0,w2,w0,eq
	mov	w3,0				; wt
	ldr	w1,[x23,TI_cmt]
	tbnz	w1,1,.w3
	tbz	w1,0,.w4
	mov	w3,2
	b	.w4
.w3:	mov	w3,1
.w4:	ldr	w1,[x23,TI_maskt]
	cmp	w1,0
	csel	w3,w2,w3,eq
	lsl	w0,w0,GF_WRAP_S
	orr	w0,w0,w3,lsl GF_WRAP_T
	ret

; RECT : s0-s7 = x0 y0 x1 y1 s0 t0 s1 t1, w0 = textured, w1 = fill
GR_p		= 0			; s0..s7 : 32 octets
GR_col		= 32
GR_add		= 48
GR_q		= 64			; 4 sommets
GR_tri		= 192			; 3 sommets
GR_tw		= 288
GR_th		= 292
GR_SIZE		= 304

gfx_rect:
	prologue
	save_callee
	sub	sp,sp,GR_SIZE
	stp	s0,s1,[sp,GR_p]
	stp	s2,s3,[sp,GR_p+8]
	stp	s4,s5,[sp,GR_p+16]
	stp	s6,s7,[sp,GR_p+24]
	mov	w21,w0				; textured
	mov	w22,w1				; fill
	mov	w23,-1				; texId
	fmov	s16,1.0
	str	s16,[sp,GR_tw]
	str	s16,[sp,GR_th]
	mov	w24,GF_ZALWAYS or GF_NOZWRITE	; flags
	ldr	w25,[x19,m_omH]
	ubfx	w25,w25,20,2			; cyc
	stp	s16,s16,[sp,GR_col]
	stp	s16,s16,[sp,GR_col+8]
	stp	xzr,xzr,[sp,GR_add]
	cbz	w22,.tx
	; plein
	ldr	w0,[x19,m_fillColor]
	ldr	w1,[x19,m_cimgSiz]
	cmp	w1,3
	b.ne	.f16
	fconst	s1,w1,F_255
	lsr	w2,w0,24
	ucvtf	s0,w2
	fdiv	s0,s0,s1
	str	s0,[sp,GR_col]
	ubfx	w2,w0,16,8
	ucvtf	s0,w2
	fdiv	s0,s0,s1
	str	s0,[sp,GR_col+4]
	ubfx	w2,w0,8,8
	ucvtf	s0,w2
	fdiv	s0,s0,s1
	str	s0,[sp,GR_col+8]
	b	.drw
.f16:	lsr	w0,w0,16
	fconst	s1,w1,F_31
	ubfx	w2,w0,11,5
	ucvtf	s0,w2
	fdiv	s0,s0,s1
	str	s0,[sp,GR_col]
	ubfx	w2,w0,6,5
	ucvtf	s0,w2
	fdiv	s0,s0,s1
	str	s0,[sp,GR_col+4]
	ubfx	w2,w0,1,5
	ucvtf	s0,w2
	fdiv	s0,s0,s1
	str	s0,[sp,GR_col+8]
	b	.drw
.tx:	bl	.glrect
	cbz	w0,.tx0
	stp	xzr,xzr,[sp,GR_col]		; (un rectangle du RDP n'a pas d'ombrage : 0)
	cmp	w25,2
	b.eq	.copy
	b	.zt
.tx0:	cmp	w25,2
	b.eq	.copy
	adr	x0,f_ones
	adr	x1,f_black
	add	x2,sp,GR_add
	bl	gfx_combine
	cbz	w21,.nt
	adr	x0,f_ones
	adr	x1,f_ones
	add	x2,sp,GR_col
	bl	gfx_combine
	mov	x9,0
.cd:	add	x0,sp,GR_col
	ldr	s0,[x0,x9,lsl 2]
	add	x1,sp,GR_add
	ldr	s1,[x1,x9,lsl 2]
	fsub	s0,s0,s1
	fmov	s1,wzr
	fcmp	s0,s1
	fcsel	s0,s1,s0,mi
	str	s0,[x0,x9,lsl 2]
	add	x9,x9,1
	cmp	x9,4
	b.lo	.cd
	b	.zt
.nt:	ldp	x0,x1,[sp,GR_add]
	stp	x0,x1,[sp,GR_col]
	stp	xzr,xzr,[sp,GR_add]
.zt:	ldr	w0,[x19,m_geom]
	tbz	w0,0,.zf
	ldr	w0,[x19,m_omL]
	tbz	w0,4,.zf
	mov	w24,GF_ZLEQUAL or GF_NOZWRITE
.zf:	bl	gfx_flags
	movl	w1,GF_BLEND_ALPHA or GF_LINEAR or GF_ALPHATEST or 0xFF80000
	and	w0,w0,w1
	orr	w24,w24,w0
	b	.txu
.copy:	ldr	w0,[x19,m_omL]
	and	w0,w0,3
	cmp	w0,1
	b.ne	.txu
	movl	w0,GF_ALPHATEST or (1 shl 19)
	orr	w24,w24,w0
.txu:	bl	.glrect
	cbz	w0,.txg
	ldr	w0,[x19,m_rectTile]
	mov	w1,w21
	bl	gl_state
	b	.drw
.txg:	cbz	w21,.drw
	ldr	w0,[x19,m_rectTile]
	bl	gfx_texture
	mov	w23,w0
	adm	x0,M_TEXS
	add	x0,x0,x23,lsl 5
	ldr	s0,[x0,TX_w]
	scvtf	s0,s0
	str	s0,[sp,GR_tw]
	ldr	s0,[x0,TX_h]
	scvtf	s0,s0
	str	s0,[sp,GR_th]
	ldr	w0,[x19,m_rectTile]
	and	w0,w0,7
	mov	w1,TI_SIZE
	mul	w0,w0,w1
	add	x26,x19,m_tiles
	add	x26,x26,x0			; T
	fconst	s1,w0,F_4
	ldr	w0,[x26,TI_sl]
	scvtf	s0,w0
	fdiv	s0,s0,s1
	ldr	s2,[sp,GR_p+16]
	fsub	s2,s2,s0
	str	s2,[sp,GR_p+16]
	ldr	s2,[sp,GR_p+24]
	fsub	s2,s2,s0
	str	s2,[sp,GR_p+24]
	ldr	w0,[x26,TI_tl]
	scvtf	s0,w0
	fdiv	s0,s0,s1
	ldr	s2,[sp,GR_p+20]
	fsub	s2,s2,s0
	str	s2,[sp,GR_p+20]
	ldr	s2,[sp,GR_p+28]
	fsub	s2,s2,s0
	str	s2,[sp,GR_p+28]
	mov	x0,x23
	mov	x23,x26
	stp	x0,x26,[sp,-16]!
	bl	gfx_tri.wrap
	ldp	x23,x26,[sp],16
	orr	w24,w24,w0
.drw:	ldrb	w0,[x19,m_drawMain]
	cbnz	w0,.scr
	; un tampon hors de l'ecran : par le CPU
	ldr	w0,[x19,m_cimg]
	ldr	w1,[x19,m_zimg]
	cmp	w0,w1
	b.ne	.cp
	cbnz	w22,.out
.cp:	ldp	s0,s1,[sp,GR_p]
	ldp	s2,s3,[sp,GR_p+8]
	ldp	s4,s5,[sp,GR_p+16]
	ldp	s6,s7,[sp,GR_p+24]
	mov	w0,w23
	add	x1,sp,GR_col
	add	x2,sp,GR_add
	mov	w3,w22
	bl	rect_cpu
	b	.out
.scr:	cbz	w22,.scr0
	ldr	w0,[x19,m_glOn]
	cbz	w0,.scr0
	mov	w0,0				; (plein : la couleur est dans les sommets)
	mov	w1,0
	bl	gl_state
.scr0:	ldr	w0,[x19,m_gfxBuild]
	mov	w1,GF_SIZE
	mul	w0,w0,w1
	add	x0,x19,x0
	add	x0,x0,m_gfxFrame
	ldr	s0,[x0,GF_width]
	scvtf	s0,s0
	ldr	s1,[x0,GF_height]
	scvtf	s1,s1
	fmov	s2,2.0
	fdiv	s0,s0,s2			; W / 2
	fdiv	s1,s1,s2			; H / 2
	fmov	s2,1.0
	ldp	s3,s4,[sp,GR_p]			; x0 y0
	ldp	s5,s6,[sp,GR_p+8]		; x1 y1
	fdiv	s3,s3,s0
	fsub	s3,s3,s2			; X0
	fdiv	s5,s5,s0
	fsub	s5,s5,s2			; X1
	fdiv	s4,s4,s1
	fsub	s4,s2,s4			; Y0
	fdiv	s6,s6,s1
	fsub	s6,s2,s6			; Y1
	ldp	s16,s17,[sp,GR_p+16]		; s0 t0
	ldp	s18,s19,[sp,GR_p+24]		; s1 t1
	ldr	s0,[sp,GR_tw]
	ldr	s1,[sp,GR_th]
	fdiv	s16,s16,s0
	fdiv	s18,s18,s0
	fdiv	s17,s17,s1
	fdiv	s19,s19,s1
	add	x26,sp,GR_q
	mov	x0,GV_SIZE
	stp	s3,s4,[x26]
	stp	s16,s17,[x26,GV_s]
	stp	s5,s4,[x26,GV_SIZE]
	add	x1,x26,GV_SIZE*1
	stp	s18,s17,[x1,GV_s]
	add	x1,x26,GV_SIZE*2
	stp	s5,s6,[x1]
	stp	s18,s19,[x1,GV_s]
	add	x1,x26,GV_SIZE*3
	stp	s3,s6,[x1]
	stp	s16,s19,[x1,GV_s]
	; les couleurs
	mov	x9,0
.col:	add	x0,sp,GR_col
	ldr	s0,[x0,x9,lsl 2]
	bl	f_c8
	mov	w27,w0
	cmp	x9,3
	b.ne	.ca
	cbz	w22,.ca
	mov	w27,255
.ca:	add	x0,sp,GR_add
	ldr	s0,[x0,x9,lsl 2]
	bl	f_c8
	mov	x1,0
.cq:	mov	x2,GV_SIZE
	mul	x2,x2,x1
	add	x2,x26,x2
	add	x3,x2,GV_r
	strb	w27,[x3,x9]
	add	x3,x2,GV_r2
	strb	w0,[x3,x9]
	add	x1,x1,1
	cmp	x1,4
	b.lo	.cq
	add	x9,x9,1
	cmp	x9,4
	b.lo	.col
	mov	x0,0
	fmov	s1,1.0
	fmov	s0,wzr
.zq:	mov	x2,GV_SIZE
	mul	x2,x2,x0
	add	x2,x26,x2
	stp	s0,s1,[x2,8]
	add	x0,x0,1
	cmp	x0,4
	b.lo	.zq
	; q0 q2 q1, puis q0 q3 q2
	add	x27,sp,GR_tri
	mov	w0,0
	mov	w1,2
	mov	w2,1
	bl	.tri
	mov	w0,0
	mov	w1,3
	mov	w2,2
	bl	.tri
.out:	add	sp,sp,GR_SIZE
	rest_callee
	epilogue
.glrect:				; -> w0 <> 0 : ce rectangle va a n64gl (hors ecran : le CPU, comme sans Mesa)
	ldr	w0,[x19,m_glOn]
	cbz	w0,.glr
	ldrb	w0,[x19,m_drawMain]
.glr:	ret
.tri:	stp	x29,x30,[sp,-16]!
	mov	x3,x27
	mov	w4,w0
	bl	.qcopy
	mov	w4,w1
	bl	.qcopy
	mov	w4,w2
	bl	.qcopy
	mov	x0,x27
	mov	w1,w23
	mov	w2,w24
	ldr	w3,[x19,m_glOn]
	cbz	w3,.nogl
	bl	gl_emit
	ldp	x29,x30,[sp],16
	ret
.nogl:	bl	gfx_emit
	ldp	x29,x30,[sp],16
	ret
.qcopy:	lsl	x4,x4,5
	add	x4,x26,x4
	ldp	x5,x6,[x4]
	stp	x5,x6,[x3],16
	ldp	x5,x6,[x4,16]
	stp	x5,x6,[x3],16
	ret

; ---- une commande ----

; w0 = bits, w1 -> w0 = index / 2 (pour TRI)
macro tri_args reg, sh {
	ubfx	w0,reg,16,8
	lsr	w0,w0,1
	ubfx	w1,reg,8,8
	lsr	w1,w1,1
	and	w2,reg,0xFF
	lsr	w2,w2,1
}

gfx_cmd:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,80
	mov	w21,w0				; w0
	mov	w23,w1				; w1
	add	x22,x19,M_RDRAM
	lsr	w0,w21,24
	cmp	w0,7
	b.hi	.n1
	jtab	x0,.t
.t:	dw	.rdp-.t,.c01-.t,.c02-.t,.c03-.t,.c04-.t,.c05-.t,.c06-.t,.c06-.t
.n1:	sub	w1,w0,0xD7
	cmp	w1,15
	b.hs	.rdp
	jtab	x1,.u
.u:	dw	.d7-.u,.d8-.u,.d9-.u,.da-.u,.db-.u,.dc-.u,.rdp-.u,.rdp-.u
	dw	.rdp-.u,.rdp-.u,.e1-.u,.e2-.u,.e2-.u,.e4-.u,.e4-.u
.rdp:	mov	w0,w21
	mov	w1,w23
	bl	gfx_rdp
	b	.out
.c01:	ubfx	w24,w21,12,8			; n
	ubfx	w1,w21,1,7			; end
	sub	w24,w1,w24
	ubfx	w1,w21,12,8
	mov	w0,w23
	stp	x1,xzr,[sp,-16]!
	bl	gfx_seg
	ldp	x1,x2,[sp],16
	mov	w2,w24
	bl	gfx_vtx
	b	.out
.c02:	ubfx	w0,w21,16,8			; where
	and	w1,w21,0xFFFF
	lsr	w1,w1,1
	and	w1,w1,63
	mov	w2,VX_SIZE
	mul	w1,w1,w2
	movl	x17,m_vtx
	add	x2,x19,x17
	add	x2,x2,x1
	cmp	w0,0x10
	b.ne	.c021
	fconst	s1,w0,F_255
	lsr	w0,w23,24
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	str	s0,[x2,VX_r]
	ubfx	w0,w23,16,8
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	str	s0,[x2,VX_g]
	ubfx	w0,w23,8,8
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	str	s0,[x2,VX_b]
	and	w0,w23,255
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	str	s0,[x2,VX_a]
	b	.out
.c021:	cmp	w0,0x14
	b.ne	.out
	asr	w0,w23,16
	scvtf	s0,w0
	str	s0,[x2,VX_s]
	sxth	w0,w23
	scvtf	s0,w0
	str	s0,[x2,VX_t]
	b	.out
.c03:	and	w0,w21,0xFFFF
	lsr	w0,w0,1
	and	w0,w0,63			; v0
	and	w1,w23,0xFFFF
	lsr	w1,w1,1
	and	w1,w1,63			; v1
	mov	w2,31				; all
.c03l:	cmp	w0,w1
	b.gt	.c03e
	mov	w3,VX_SIZE
	mul	w3,w3,w0
	movl	x17,m_vtx
	add	x4,x19,x17
	add	x4,x4,x3
	ldr	w3,[x4,VX_clip]
	and	w2,w2,w3
	add	w0,w0,1
	b	.c03l
.c03e:	tst	w2,15
	b.eq	.out
	mov	w0,1
	strb	w0,[x19,m_dlEnd]
	b	.out
.c04:	ldr	w0,[x19,m_rdpHalf1]
	bl	gfx_seg
	str	w0,[x19,m_pcDl]
	b	.out
.c05:	tri_args w21,0
	bl	gfx_tri
	b	.out
.c06:	tri_args w21,0
	bl	gfx_tri
	tri_args w23,0
	bl	gfx_tri
	b	.out
.d7:	fconst	s1,w0,F_65536
	fmov	s2,1.0
	lsr	w0,w23,16
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	mov	w1,0xFFFF
	cmp	w0,w1
	fcsel	s0,s2,s0,eq
	str	s0,[x19,m_texScaleS]
	and	w0,w23,0xFFFF
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	cmp	w0,w1
	fcsel	s0,s2,s0,eq
	str	s0,[x19,m_texScaleT]
	ubfx	w0,w21,8,3
	str	w0,[x19,m_texTile]
	ubfx	w0,w21,1,7
	str	w0,[x19,m_texOn]
	b	.out
.d8:	lsr	w0,w23,6			; n
	ldr	w1,[x19,m_mtxSp]
	subs	w1,w1,w0
	csel	w1,wzr,w1,lt
	str	w1,[x19,m_mtxSp]
	mov	w0,1
	strb	w0,[x19,m_mvpDirty]
	b	.out
.d9:	ldr	w0,[x19,m_geom]
	and	w1,w21,0xFFFFFF
	and	w0,w0,w1
	orr	w0,w0,w23
	str	w0,[x19,m_geom]
	b	.out
.da:	mov	w0,w23
	bl	gfx_seg
	mov	w24,w0				; a
	and	w21,w21,0xFF
	eor	w21,w21,1			; param
	mov	x4,0
	fconst	s2,w0,F_65536
.dm:	add	w0,w24,w4,lsl 1
	rd_s16	w0,w0
	scvtf	s0,w0
	add	w0,w24,32
	add	w0,w0,w4,lsl 1
	rd_s16	w0,w0
	and	w0,w0,0xFFFF
	ucvtf	s1,w0
	fdiv	s1,s1,s2
	fadd	s0,s0,s1
	str	s0,[sp,x4,lsl 2]
	add	x4,x4,1
	cmp	x4,16
	b.lo	.dm
	tbz	w21,2,.dmv
	add	x2,x19,m_proj
	tbz	w21,1,.dpm
	mov	x0,sp
	bl	.m_copy
	b	.dme
.dpm:	mov	x0,sp
	mov	x1,x2
	bl	mtx_mul
	b	.dme
.dmv:	ldr	w0,[x19,m_mtxSp]
	tbz	w21,0,.dmn
	cmp	w0,9
	b.ge	.dmn
	add	x1,x19,m_mtxStack
	add	x1,x1,x0,lsl 6
	add	x2,x1,64
	mov	x0,x1
	bl	.m_copy
	ldr	w0,[x19,m_mtxSp]
	add	w0,w0,1
	str	w0,[x19,m_mtxSp]
.dmn:	ldr	w0,[x19,m_mtxSp]
	add	x2,x19,m_mtxStack
	add	x2,x2,x0,lsl 6
	tbz	w21,1,.dmm
	mov	x0,sp
	bl	.m_copy
	b	.dme
.dmm:	mov	x0,sp
	mov	x1,x2
	bl	mtx_mul
.dme:	mov	w0,1
	strb	w0,[x19,m_mvpDirty]
	b	.out
.m_copy:				; x0 -> x2 : 64 octets
	ldp	x3,x4,[x0]
	stp	x3,x4,[x2]
	ldp	x3,x4,[x0,16]
	stp	x3,x4,[x2,16]
	ldp	x3,x4,[x0,32]
	stp	x3,x4,[x2,32]
	ldp	x3,x4,[x0,48]
	stp	x3,x4,[x2,48]
	ret
.db:	ubfx	w0,w21,16,8			; idx
	and	w1,w21,0xFFFF			; off
	cmp	w0,6
	b.ne	.db1
	ubfx	w1,w1,2,4
	and	w2,w23,0xFFFFFF
	add	x3,x19,m_segment
	str	w2,[x3,x1,lsl 2]
	b	.out
.db1:	cmp	w0,2
	b.ne	.db2
	mov	w0,24
	udiv	w0,w23,w0
	str	w0,[x19,m_numLights]
	b	.out
.db2:	cmp	w0,8
	b.ne	.db3
	asr	w0,w23,16
	scvtf	s0,w0
	str	s0,[x19,m_fogMul]
	sxth	w0,w23
	scvtf	s0,w0
	str	s0,[x19,m_fogOff]
	b	.out
.db3:	cmp	w0,10
	b.ne	.out
	mov	w0,24
	udiv	w1,w1,w0
	cmp	w1,8
	b.hs	.out
	mul	w1,w1,w0
	add	x2,x19,m_lights
	add	x2,x2,x1
	fconst	s1,w0,F_255
	lsr	w0,w23,24
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	str	s0,[x2]
	ubfx	w0,w23,16,8
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	str	s0,[x2,4]
	ubfx	w0,w23,8,8
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	str	s0,[x2,8]
	b	.out
.dc:	and	w4,w21,0xFF			; idx
	ubfx	w5,w21,8,8
	lsl	w5,w5,3				; off
	mov	w0,w23
	bl	gfx_seg
	mov	w24,w0				; a
	cmp	w4,8
	b.ne	.dc1
	mov	x1,0
.dcv:	add	w2,w24,w1,lsl 1
	rd_s16	w3,w2
	add	x4,x19,m_vpScale
	strh	w3,[x4,x1,lsl 1]
	add	w2,w2,8
	rd_s16	w3,w2
	add	x4,x19,m_vpTrans
	strh	w3,[x4,x1,lsl 1]
	add	x1,x1,1
	cmp	x1,4
	b.lo	.dcv
	b	.out
.dc1:	cmp	w4,10
	b.ne	.out
	cmp	w5,48
	b.lo	.out
	sub	w5,w5,48
	mov	w0,24
	udiv	w5,w5,w0
	cmp	w5,8
	b.hs	.out
	mul	w5,w5,w0
	add	x2,x19,m_lights
	add	x2,x2,x5			; L
	fconst	s1,w0,F_255
	rd_b	w0,w24
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	str	s0,[x2]
	add	w0,w24,1
	rd_b	w0,w0
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	str	s0,[x2,4]
	add	w0,w24,2
	rd_b	w0,w0
	ucvtf	s0,w0
	fdiv	s0,s0,s1
	str	s0,[x2,8]
	add	w0,w24,8
	rd_b	w0,w0
	sxtb	w0,w0
	scvtf	s2,w0				; x
	add	w0,w24,9
	rd_b	w0,w0
	sxtb	w0,w0
	scvtf	s3,w0				; y
	add	w0,w24,10
	rd_b	w0,w0
	sxtb	w0,w0
	scvtf	s4,w0				; z
	fmul	s0,s2,s2
	fmul	s1,s3,s3
	fadd	s0,s0,s1
	fmul	s1,s4,s4
	fadd	s0,s0,s1
	fsqrt	s0,s0
	fcmp	s0,0.0
	b.le	.dc2
	b.vs	.dc2
	fdiv	s2,s2,s0
	fdiv	s3,s3,s0
	fdiv	s4,s4,s0
.dc2:	stp	s2,s3,[x2,LI_x]
	str	s4,[x2,LI_x+8]
	b	.out
.e1:	str	w23,[x19,m_rdpHalf1]
	b	.out
.e2:	and	w0,w21,0xFF
	add	w0,w0,1				; len
	ubfx	w1,w21,8,8
	mov	w2,32
	sub	w2,w2,w1
	sub	w2,w2,w0			; sft
	mov	w3,-1
	cmp	w0,32
	b.hs	.e21
	mov	w3,1
	lsl	w3,w3,w0
	sub	w3,w3,1
.e21:	lsl	w3,w3,w2			; mask
	lsr	w0,w21,24
	ldr	w1,[x19,m_omH]
	cmp	w0,0xE2
	b.ne	.e22
	ldr	w1,[x19,m_omL]
.e22:	bic	w1,w1,w3
	and	w2,w23,w3
	orr	w1,w1,w2
	cmp	w0,0xE2
	b.ne	.e23
	str	w1,[x19,m_omL]
	b	.out
.e23:	str	w1,[x19,m_omH]
	b	.out
.e4:	ldr	w0,[x19,m_pcDl]
	add	w1,w0,4
	rd_w	w4,w1				; h1
	add	w1,w0,12
	rd_w	w5,w1				; h2
	add	w0,w0,16
	str	w0,[x19,m_pcDl]
	fconst	s16,w0,F_4
	ubfx	w0,w21,12,12
	ucvtf	s2,w0
	fdiv	s2,s2,s16			; x1
	and	w0,w21,0xFFF
	ucvtf	s3,w0
	fdiv	s3,s3,s16			; y1
	ubfx	w0,w23,12,12
	ucvtf	s0,w0
	fdiv	s0,s0,s16			; x0
	and	w0,w23,0xFFF
	ucvtf	s1,w0
	fdiv	s1,s1,s16			; y0
	ubfx	w0,w23,24,3
	str	w0,[x19,m_rectTile]
	fconst	s17,w0,F_32
	asr	w0,w4,16
	scvtf	s4,w0
	fdiv	s4,s4,s17			; s
	sxth	w0,w4
	scvtf	s5,w0
	fdiv	s5,s5,s17			; t
	fconst	s17,w0,F_1024
	asr	w0,w5,16
	scvtf	s18,w0
	fdiv	s18,s18,s17			; dsdx
	sxth	w0,w5
	scvtf	s19,w0
	fdiv	s19,s19,s17			; dtdy
	ldr	w0,[x19,m_omH]
	ubfx	w0,w0,20,2
	cmp	w0,2
	b.ne	.e41
	fdiv	s18,s18,s16
	fmov	s17,1.0
	fadd	s2,s2,s17
	fadd	s3,s3,s17
.e41:	lsr	w0,w21,24
	cmp	w0,0xE5
	b.ne	.e42
	fsub	s6,s3,s1
	fmul	s6,s6,s18
	fadd	s6,s4,s6			; s + (y1 - y0) * dsdx
	fsub	s7,s2,s0
	fmul	s7,s7,s19
	fadd	s7,s5,s7			; t + (x1 - x0) * dtdy
	b	.e43
.e42:	fsub	s6,s2,s0
	fmul	s6,s6,s18
	fadd	s6,s4,s6
	fsub	s7,s3,s1
	fmul	s7,s7,s19
	fadd	s7,s5,s7
.e43:	mov	w0,1
	mov	w1,0
	bl	gfx_rect
.out:	add	sp,sp,80
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; w0 = la couleur, x1 = o[4]
f_col:	fconst	s1,w2,F_255
	lsr	w2,w0,24
	ucvtf	s0,w2
	fdiv	s0,s0,s1
	str	s0,[x1]
	ubfx	w2,w0,16,8
	ucvtf	s0,w2
	fdiv	s0,s0,s1
	str	s0,[x1,4]
	ubfx	w2,w0,8,8
	ucvtf	s0,w2
	fdiv	s0,s0,s1
	str	s0,[x1,8]
	and	w2,w0,255
	ucvtf	s0,w2
	fdiv	s0,s0,s1
	str	s0,[x1,12]
	ret

; -> w0 : drawMain
draw_main:
	ldr	w0,[x19,m_cimg]
	ldr	w1,[x19,m_zimg]
	cmp	w0,w1
	cset	w0,ne
	ldr	w1,[x19,m_cimgW]
	ldr	w2,[x19,m_vi+8]
	and	w2,w2,0xFFF
	cmp	w1,w2
	csel	w0,wzr,w0,ne
	ldr	w1,[x19,m_cimgSiz]
	cmp	w1,2
	csel	w0,wzr,w0,lo
	strb	w0,[x19,m_drawMain]
	ret

gfx_rdp:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	w21,w0
	mov	w22,w1
	lsr	w0,w21,24
	sub	w1,w0,0xE9
	cmp	w1,22
	b.hs	.out
	jtab	x1,.t
.t:	dw	.e9-.t,.out-.t,.out-.t,.out-.t,.ed-.t,.ee-.t,.ef-.t,.f0-.t
	dw	.f1-.t,.f2-.t,.f3-.t,.f4-.t,.f5-.t,.f6-.t,.f7-.t,.f8-.t
	dw	.f9-.t,.fa-.t,.fb-.t,.fc-.t,.fd-.t,.fe-.t
.f1:	str	w22,[x19,m_rdpHalf2]
	b	.out
.e9:	ldr	w0,[x19,m_gfxBuild]
	mov	w1,GF_SIZE
	mul	w0,w0,w1
	add	x2,x19,x0
	add	x2,x2,m_gfxFrame		; le cadre fini
	ldr	w0,[x2,GF_nv]
	cmp	w0,0
	b.gt	.e91
	ldr	w0,[x2,GF_clear]
	cbz	w0,.out
.e91:	ldr	w0,[x19,m_gfxBuild]
	str	w0,[x19,m_gfxReady]
	eor	w0,w0,1
	str	w0,[x19,m_gfxBuild]
	ldr	w1,[x19,m_gfxSerial]
	add	w1,w1,1
	str	w1,[x19,m_gfxSerial]
	mov	w1,GF_SIZE
	mul	w0,w0,w1
	add	x3,x19,x0
	add	x3,x3,m_gfxFrame		; N
	str	wzr,[x3,GF_nv]
	str	wzr,[x3,GF_nb]
	ldr	w0,[x2,GF_clear]
	str	w0,[x3,GF_clear]
	ldr	w0,[x19,m_vi+8]
	and	w0,w0,0xFFF
	mov	w1,320
	cmp	w0,0
	csel	w0,w1,w0,eq
	str	w0,[x3,GF_width]
	add	w0,w0,w0,lsl 1
	lsr	w0,w0,2
	str	w0,[x3,GF_height]
	b	.out
.ed:	ubfx	w0,w21,12,12
	asr	w0,w0,2
	str	w0,[x19,m_scissor]
	and	w0,w21,0xFFF
	asr	w0,w0,2
	str	w0,[x19,m_scissor+4]
	ubfx	w0,w22,12,12
	asr	w0,w0,2
	str	w0,[x19,m_scissor+8]
	and	w0,w22,0xFFF
	asr	w0,w0,2
	str	w0,[x19,m_scissor+12]
	b	.out
.ee:	lsr	w0,w22,16
	ucvtf	s0,w0
	str	s0,[x19,m_primDepth]
	b	.out
.ef:	and	w0,w21,0xFFFFFF
	str	w0,[x19,m_omH]
	str	w22,[x19,m_omL]
	b	.out
.f0:	mov	w0,w21
	mov	w1,w22
	mov	w2,2
	bl	gfx_load
	b	.out
.f3:	mov	w0,w21
	mov	w1,w22
	mov	w2,0
	bl	gfx_load
	b	.out
.f4:	mov	w0,w21
	mov	w1,w22
	mov	w2,1
	bl	gfx_load
	b	.out
.f2:	ubfx	w0,w22,24,3
	mov	w1,TI_SIZE
	mul	w0,w0,w1
	add	x2,x19,m_tiles
	add	x2,x2,x0
	ubfx	w0,w21,12,12
	str	w0,[x2,TI_sl]
	and	w0,w21,0xFFF
	str	w0,[x2,TI_tl]
	ubfx	w0,w22,12,12
	str	w0,[x2,TI_sh]
	and	w0,w22,0xFFF
	str	w0,[x2,TI_th]
	b	.out
.f5:	ubfx	w0,w22,24,3
	mov	w1,TI_SIZE
	mul	w0,w0,w1
	add	x2,x19,m_tiles
	add	x2,x2,x0
	ubfx	w0,w21,21,3
	str	w0,[x2,TI_fmt]
	ubfx	w0,w21,19,2
	str	w0,[x2,TI_siz]
	ubfx	w0,w21,9,9
	str	w0,[x2,TI_line]
	and	w0,w21,0x1FF
	str	w0,[x2,TI_tmem]
	ubfx	w0,w22,20,4
	str	w0,[x2,TI_pal]
	ubfx	w0,w22,18,2
	str	w0,[x2,TI_cmt]
	ubfx	w0,w22,14,4
	str	w0,[x2,TI_maskt]
	ubfx	w0,w22,10,4
	str	w0,[x2,TI_shiftt]
	ubfx	w0,w22,8,2
	str	w0,[x2,TI_cms]
	ubfx	w0,w22,4,4
	str	w0,[x2,TI_masks]
	and	w0,w22,15
	str	w0,[x2,TI_shifts]
	b	.out
.f6:	fconst	s16,w0,F_4
	ubfx	w0,w21,12,12
	ucvtf	s2,w0
	fdiv	s2,s2,s16			; x1
	and	w0,w21,0xFFF
	ucvtf	s3,w0
	fdiv	s3,s3,s16			; y1
	ubfx	w0,w22,12,12
	ucvtf	s0,w0
	fdiv	s0,s0,s16			; x0
	and	w0,w22,0xFFF
	ucvtf	s1,w0
	fdiv	s1,s1,s16			; y0
	ldr	w1,[x19,m_omH]
	ubfx	w1,w1,20,2			; cyc
	cmp	w1,3
	cset	w2,eq				; fill
	b.ne	.f61
	fmov	s17,1.0
	fadd	s2,s2,s17
	fadd	s3,s3,s17
.f61:	fmov	s4,wzr
	fmov	s5,wzr
	fmov	s6,wzr
	fmov	s7,wzr
	ldrb	w0,[x19,m_drawMain]
	cbnz	w0,.f62
	ldr	w0,[x19,m_cimg]
	ldr	w1,[x19,m_zimg]
	cmp	w0,w1
	b.eq	.out
.f6r:	mov	w0,0
	mov	w1,w2
	bl	gfx_rect
	b	.out
.f62:	cbz	w2,.f6r
	ldr	w0,[x19,m_gfxBuild]
	mov	w1,GF_SIZE
	mul	w0,w0,w1
	add	x3,x19,x0
	add	x3,x3,m_gfxFrame		; F
	fcmp	s0,0.0
	b.hi	.f6r
	b.vs	.f6r
	fcmp	s1,0.0
	b.hi	.f6r
	b.vs	.f6r
	fmov	s17,1.0
	ldr	w0,[x3,GF_width]
	scvtf	s16,w0
	fsub	s16,s16,s17
	fcmp	s2,s16
	b.lt	.f6r
	b.vs	.f6r
	ldr	w0,[x3,GF_height]
	scvtf	s16,w0
	fsub	s16,s16,s17
	fcmp	s3,s16
	b.lt	.f6r
	b.vs	.f6r
	; tout l'ecran : un effacement
	ldr	w0,[x19,m_fillColor]
	ldr	w1,[x19,m_cimgSiz]
	cmp	w1,3
	b.ne	.f63
	lsr	w0,w0,8
	b	.f64
.f63:	lsr	w0,w0,16
	mov	w4,255
	mov	w5,31
	ubfx	w1,w0,11,5
	mul	w1,w1,w4
	udiv	w1,w1,w5
	ubfx	w2,w0,6,5
	mul	w2,w2,w4
	udiv	w2,w2,w5
	ubfx	w6,w0,1,5
	mul	w6,w6,w4
	udiv	w6,w6,w5
	lsl	w0,w1,16
	orr	w0,w0,w2,lsl 8
	orr	w0,w0,w6
.f64:	str	w0,[x3,GF_clear]
	str	wzr,[x3,GF_nv]
	str	wzr,[x3,GF_nb]
	b	.out
.f7:	str	w22,[x19,m_fillColor]
	b	.out
.f8:	add	x1,x19,m_fogC
	str	w22,[x19,m_fogW]
	b	.col
.f9:	add	x1,x19,m_blendC
	str	w22,[x19,m_blendW]
	b	.col
.fa:	add	x1,x19,m_primC
	str	w22,[x19,m_primW]
	and	w0,w21,0xFF
	str	w0,[x19,m_primLod]
	b	.col
.fb:	add	x1,x19,m_envC
	str	w22,[x19,m_envW]
.col:	mov	w0,w22
	bl	f_col
	b	.out
.fc:	and	w0,w21,0xFFFFFF
	str	w0,[x19,m_combH]
	str	w22,[x19,m_combL]
	b	.out
.fd:	mov	w0,w22
	bl	gfx_seg
	str	w0,[x19,m_timg]
	and	w0,w21,0xFFF
	add	w0,w0,1
	str	w0,[x19,m_timgW]
	ubfx	w0,w21,19,2
	str	w0,[x19,m_timgSiz]
	ubfx	w0,w21,21,3
	str	w0,[x19,m_timgFmt]
	b	.out
.fe:	mov	w0,w22
	bl	gfx_seg
	str	w0,[x19,m_zimg]
	bl	draw_main
	b	.out
.out:	lsr	w0,w21,24
	cmp	w0,0xFF
	b.ne	.r
	mov	w0,w22
	bl	gfx_seg
	str	w0,[x19,m_cimg]
	and	w0,w21,0xFFF
	add	w0,w0,1
	str	w0,[x19,m_cimgW]
	ubfx	w0,w21,19,2
	str	w0,[x19,m_cimgSiz]
	bl	draw_main
	cbz	w0,.r
	ldr	w0,[x19,m_cimg]
	ldr	w1,[x19,m_cimgW]
	ldr	w2,[x19,m_gfxBuild]
	mov	w3,GF_SIZE
	mul	w2,w2,w3
	add	x2,x19,x2
	add	x2,x2,m_gfxFrame
	str	w0,[x2,GF_cimg]
	bl	fb_note
.r:	ldp	x21,x22,[sp],16
	epilogue

include 'n64_gl.inc'
