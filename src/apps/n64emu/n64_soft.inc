; ============================================================================
;  n64_soft.inc - le rendu logiciel des listes d'affichage, quand il n'y a pas
;  de GPU (QEMU) : les triangles coupes au plan proche, couleurs et coordonnees
;  corrigees en perspective, profondeur, faces cachees, melange. D'apres le
;  BASIC 3D d'Onyx (tools/emu/n64ref/bas3d.h), au bit pres : memes operations
;  flottantes dans le meme ordre.
;
;	n64_sw_frame (m, px, l, h, pas en pixels) -> 1 ; 0 : rien a montrer
; ============================================================================

G3_NOMATRIX	= 1 shl 17
G3_ALPHATEST	= 1 shl 18
G3_NOZWRITE	= 1 shl 3
G3_CULL_BACK	= 1 shl 4
G3_CULL_FRONT	= 1 shl 5
F_255_		= 0x437F0000
F_EPS		= 0x3727C5AC		; 1e-5

CL_x		= 0
CL_y		= 4
CL_z		= 8
CL_w		= 12
CL_s		= 16
CL_a2		= 52
CL_SIZE		= 56
G3T_px		= 0
G3T_w		= 8
G3T_h		= 12

RS_X		= 0
RS_Y		= 12
RS_Z		= 24
RS_IW		= 36
RS_A		= 48			; 3 x 10
RS_at		= 168			; 10
RS_c		= 208			; 3 pointeurs
RS_o		= 232			; 3
RS_x0		= 244
RS_y1		= 248
RS_255		= 252
RS_SIZE		= 256

; w0 = i, w1 = n, w2 = mode -> w0 ; perd w2, w3
tex_wrap:
	cmp	w2,1
	b.ne	.a
	cmp	w0,0
	csel	w0,wzr,w0,lt
	sub	w2,w1,1
	cmp	w0,w1
	csel	w0,w2,w0,ge
	ret
.a:	cmp	w2,2
	b.ne	.r
	lsl	w2,w1,1				; p
	sdiv	w3,w0,w2
	msub	w0,w3,w2,w0
	cmp	w0,0
	b.ge	.b
	add	w0,w0,w2
.b:	cmp	w0,w1
	b.lt	.c
	sub	w0,w2,w0
	sub	w0,w0,1
.c:	ret
.r:	sdiv	w3,w0,w1
	msub	w0,w3,w1,w0
	cmp	w0,0
	b.ge	.d
	add	w0,w0,w1
.d:	ret

; w0 = x, w1 = y, s2 = k : s4-s7 += k * le texel. x9 = pixels, w6, w7 = taille,
; w27 = drapeaux. Perd w0-w3, w8, w16, x17, s3
g3_texel:
	mov	x17,x30
	mov	w16,w0
	mov	w0,w1
	mov	w1,w7
	ubfx	w2,w27,15,2
	bl	tex_wrap
	mul	w8,w0,w6
	mov	w0,w16
	mov	w1,w6
	ubfx	w2,w27,13,2
	bl	tex_wrap
	add	w0,w0,w8
	ldr	w0,[x9,x0,lsl 2]
	ubfx	w1,w0,16,8
	ucvtf	s3,w1
	fmul	s3,s2,s3
	fadd	s4,s4,s3
	ubfx	w1,w0,8,8
	ucvtf	s3,w1
	fmul	s3,s2,s3
	fadd	s5,s5,s3
	and	w1,w0,255
	ucvtf	s3,w1
	fmul	s3,s2,s3
	fadd	s6,s6,s3
	lsr	w1,w0,24
	ucvtf	s3,w1
	fmul	s3,s2,s3
	fadd	s7,s7,s3
	br	x17

; s0 = s, s1 = t -> s4-s7 ; x28 = texture, w27 = drapeaux, s15 = 1, s22 = 0.5.
; Perd x0-x9, x16, x17, s0-s3, s30, s31
g3_sample:
	str	x30,[sp,-16]!
	fmov	s4,wzr
	fmov	s5,wzr
	fmov	s6,wzr
	fmov	s7,wzr
	ldr	x9,[x28,G3T_px]
	ldr	w6,[x28,G3T_w]
	ldr	w7,[x28,G3T_h]
	scvtf	s2,w6
	fmul	s0,s0,s2			; u
	scvtf	s2,w7
	fmul	s1,s1,s2			; v
	tbnz	w27,12,.lin
	fcvtzs	w0,s0
	scvtf	s2,w0
	fcmp	s2,s0
	b.le	.a
	sub	w0,w0,1
.a:	fcvtzs	w1,s1
	scvtf	s2,w1
	fcmp	s2,s1
	b.le	.b
	sub	w1,w1,1
.b:	fmov	s2,s15
	bl	g3_texel
	ldr	x30,[sp],16
	ret
.lin:	fsub	s0,s0,s22
	fsub	s1,s1,s22
	fcvtzs	w4,s0
	scvtf	s2,w4
	fcmp	s2,s0
	b.le	.c
	sub	w4,w4,1
.c:	fcvtzs	w5,s1
	scvtf	s2,w5
	fcmp	s2,s1
	b.le	.d
	sub	w5,w5,1
.d:	scvtf	s2,w4
	fsub	s30,s0,s2			; fx
	scvtf	s2,w5
	fsub	s31,s1,s2			; fy
	fsub	s0,s15,s30
	fsub	s1,s15,s31
	fmul	s2,s0,s1
	mov	w0,w4
	mov	w1,w5
	bl	g3_texel
	fsub	s1,s15,s31
	fmul	s2,s30,s1
	add	w0,w4,1
	mov	w1,w5
	bl	g3_texel
	fsub	s0,s15,s30
	fmul	s2,s0,s31
	mov	w0,w4
	add	w1,w5,1
	bl	g3_texel
	fmul	s2,s30,s31
	add	w0,w4,1
	add	w1,w5,1
	bl	g3_texel
	ldr	x30,[sp],16
	ret

; x0, x1, x2 = les sommets, w3 = drapeaux, x4 = texture ou 0, w5, w6 = l, h
g3_raster:
	prologue
	save_callee
	sub	sp,sp,RS_SIZE
	stp	x0,x1,[sp,RS_c]
	str	x2,[sp,RS_c+16]
	mov	w27,w3
	mov	x28,x4
	mov	w22,w5				; W
	mov	w23,w6				; H
	scvtf	s16,w22
	scvtf	s17,w23
	fmov	s18,1.0
	fmov	s19,0.5
	mov	x1,0
	add	x3,sp,RS_c
	mov	x4,sp
.p:	ldr	x2,[x3,x1,lsl 3]
	ldr	s0,[x2,CL_w]
	fdiv	s0,s18,s0
	str	s0,[x4,RS_IW]
	ldr	s1,[x2,CL_x]
	fmul	s1,s1,s0
	fadd	s1,s1,s18
	fmul	s1,s1,s19
	fmul	s1,s1,s16
	str	s1,[x4,RS_X]
	ldr	s1,[x2,CL_y]
	fmul	s1,s1,s0
	fsub	s1,s18,s1
	fmul	s1,s1,s19
	fmul	s1,s1,s17
	str	s1,[x4,RS_Y]
	ldr	s1,[x2,CL_z]
	fmul	s1,s1,s0
	fmul	s1,s1,s19
	fadd	s1,s1,s19
	str	s1,[x4,RS_Z]
	add	x4,x4,4
	add	x1,x1,1
	cmp	x1,3
	b.lo	.p
	ldr	s0,[sp,RS_X]
	ldr	s1,[sp,RS_X+4]
	ldr	s2,[sp,RS_X+8]
	ldr	s3,[sp,RS_Y]
	ldr	s4,[sp,RS_Y+4]
	ldr	s5,[sp,RS_Y+8]
	fsub	s6,s1,s0
	fsub	s7,s5,s3
	fmul	s6,s6,s7
	fsub	s7,s2,s0
	fsub	s20,s4,s3
	fmul	s7,s7,s20
	fsub	s6,s6,s7			; area
	fcmp	s6,0.0
	b.eq	.out
	cset	w0,mi				; front
	tst	w27,G3_CULL_BACK
	b.eq	.c1
	cbz	w0,.out
.c1:	tst	w27,G3_CULL_FRONT
	b.eq	.c2
	cbnz	w0,.out
.c2:	mov	w1,1
	mov	w2,2
	cbz	w0,.c3
	mov	w1,2
	mov	w2,1
	fneg	s6,s6
.c3:	str	wzr,[sp,RS_o]
	str	w1,[sp,RS_o+4]
	str	w2,[sp,RS_o+8]
	; les bornes
	fmov	s20,s0				; minX
	fmov	s21,s0				; maxX
	fmov	s23,s3				; minY
	fmov	s24,s3				; maxY
	fcmp	s1,s20
	fcsel	s20,s1,s20,mi
	fcmp	s1,s21
	fcsel	s21,s1,s21,gt
	fcmp	s4,s23
	fcsel	s23,s4,s23,mi
	fcmp	s4,s24
	fcsel	s24,s4,s24,gt
	fcmp	s2,s20
	fcsel	s20,s2,s20,mi
	fcmp	s2,s21
	fcsel	s21,s2,s21,gt
	fcmp	s5,s23
	fcsel	s23,s5,s23,mi
	fcmp	s5,s24
	fcsel	s24,s5,s24,gt
	fcvtzs	w25,s20				; x0
	fcmp	s20,0.0
	csel	w25,wzr,w25,mi
	fcvtzs	w26,s23				; y0
	fcmp	s23,0.0
	csel	w26,wzr,w26,mi
	fcvtzs	w15,s21				; x1
	fcmp	s21,s16
	sub	w0,w22,1
	csel	w15,w0,w15,ge
	fcvtzs	w1,s24				; y1
	fcmp	s24,s17
	sub	w0,w23,1
	csel	w1,w0,w1,ge
	cmp	w26,w1
	b.gt	.out
	str	w1,[sp,RS_y1]
	str	w25,[sp,RS_x0]
	fdiv	s14,s18,s6			; inv
	and	w12,w27,7			; zf
	mov	w0,1
	cmp	w12,0
	csel	w12,w0,w12,eq
	mov	w13,0				; zw
	tst	w27,G3_NOZWRITE
	b.ne	.c4
	cmp	w12,7
	cset	w13,ne
.c4:	ubfx	w14,w27,8,4			; blend
	; les attributs sur w, sommet par sommet, dans l'ordre des aretes
	add	x5,sp,RS_A
	add	x6,sp,RS_o
	mov	x7,0
.at:	ldr	w1,[x6,x7,lsl 2]
	add	x2,sp,RS_c
	ldr	x2,[x2,x1,lsl 3]		; v
	add	x3,sp,RS_IW
	ldr	s0,[x3,x1,lsl 2]		; iw
	mov	x4,0
.aj:	add	x3,x2,CL_s
	ldr	s1,[x3,x4,lsl 2]
	fmul	s1,s1,s0
	str	s1,[x5],4
	add	x4,x4,1
	cmp	x4,10
	b.lo	.aj
	add	x7,x7,1
	cmp	x7,3
	b.lo	.at
	add	x3,sp,RS_o
	ldr	w0,[x3]
	ldr	w1,[x3,4]
	ldr	w2,[x3,8]
	add	x4,sp,RS_X
	ldr	s8,[x4,x0,lsl 2]
	ldr	s9,[x4,x1,lsl 2]
	ldr	s10,[x4,x2,lsl 2]
	add	x4,sp,RS_Y
	ldr	s11,[x4,x0,lsl 2]
	ldr	s12,[x4,x1,lsl 2]
	ldr	s13,[x4,x2,lsl 2]
	add	x4,sp,RS_Z
	ldr	s16,[x4,x0,lsl 2]
	ldr	s17,[x4,x1,lsl 2]
	ldr	s18,[x4,x2,lsl 2]
	add	x4,sp,RS_IW
	ldr	s19,[x4,x0,lsl 2]
	ldr	s20,[x4,x1,lsl 2]
	ldr	s21,[x4,x2,lsl 2]
	fmov	s15,1.0
	fmov	s22,0.5
	movl	w0,F_255_
	str	w0,[sp,RS_255]
	movl	x21,M_PIC
	add	x21,x19,x21			; dst
	movl	x24,M_ZB
	add	x24,x19,x24
.y:	ldr	w0,[sp,RS_y1]
	cmp	w26,w0
	b.gt	.out
	mul	w0,w26,w22
	add	x10,x24,x0,lsl 2
	add	x11,x21,x0,lsl 2
	scvtf	s0,w26
	fadd	s0,s0,s22			; py
	fsub	s23,s11,s0
	fsub	s24,s12,s0
	fsub	s25,s13,s0
	ldr	w25,[sp,RS_x0]
.x:	cmp	w25,w15
	b.gt	.yn
	scvtf	s0,w25
	fadd	s0,s0,s22			; px
	fsub	s1,s9,s0			; PX[1] - px
	fsub	s2,s10,s0			; PX[2] - px
	fsub	s0,s8,s0			; PX[0] - px
	fmul	s3,s1,s25
	fmul	s4,s2,s24
	fsub	s3,s3,s4
	fmul	s26,s3,s14			; b0
	fcmp	s26,0.0
	b.mi	.xn
	fmul	s3,s2,s23
	fmul	s4,s0,s25
	fsub	s3,s3,s4
	fmul	s27,s3,s14			; b1
	fsub	s28,s15,s26
	fsub	s28,s28,s27			; b2
	fcmp	s27,0.0
	b.mi	.xn
	fcmp	s28,0.0
	b.mi	.xn
	fmul	s0,s26,s16
	fmul	s1,s27,s17
	fadd	s0,s0,s1
	fmul	s1,s28,s18
	fadd	s29,s0,s1			; z
	fcmp	s29,0.0
	b.mi	.xn
	fcmp	s29,s15
	b.gt	.xn
	cmp	w12,7
	b.eq	.zok
	ldr	s0,[x10,x25,lsl 2]
	cmp	w12,3
	b.eq	.zle
	cmp	w12,2
	b.eq	.zeq
	cmp	w12,4
	b.eq	.zgt
	cmp	w12,5
	b.eq	.zne
	cmp	w12,6
	b.eq	.zge
	fcmp	s29,s0
	b.mi	.zok
	b	.xn
.zle:	fcmp	s29,s0
	b.ls	.zok
	b	.xn
.zeq:	fcmp	s29,s0
	b.eq	.zok
	b	.xn
.zgt:	fcmp	s29,s0
	b.gt	.zok
	b	.xn
.zne:	fcmp	s29,s0
	b.ne	.zok
	b	.xn
.zge:	fcmp	s29,s0
	b.lt	.xn
.zok:	fmul	s0,s26,s19
	fmul	s1,s27,s20
	fadd	s0,s0,s1
	fmul	s1,s28,s21
	fadd	s0,s0,s1
	fdiv	s30,s15,s0			; w
	add	x5,sp,RS_A
	add	x6,sp,RS_at
	mov	x7,10
.ak:	ldr	s0,[x5]
	ldr	s1,[x5,40]
	ldr	s2,[x5,80]
	fmul	s0,s26,s0
	fmul	s1,s27,s1
	fadd	s0,s0,s1
	fmul	s1,s28,s2
	fadd	s0,s0,s1
	fmul	s0,s0,s30
	str	s0,[x6],4
	add	x5,x5,4
	subs	x7,x7,1
	b.ne	.ak
	cbz	x28,.nt
	ldr	s0,[sp,RS_at]
	ldr	s1,[sp,RS_at+4]
	bl	g3_sample
	ldr	s30,[sp,RS_255]
	fdiv	s4,s4,s30
	fdiv	s5,s5,s30
	fdiv	s6,s6,s30
	fdiv	s7,s7,s30
	ldr	s0,[sp,RS_at+8]
	ldr	s1,[sp,RS_at+12]
	ldr	s2,[sp,RS_at+16]
	ldr	s3,[sp,RS_at+20]
	fmul	s0,s0,s4
	fmul	s1,s1,s5
	fmul	s2,s2,s6
	fmul	s3,s3,s7
	b	.ad
.nt:	ldr	s0,[sp,RS_at+8]
	ldr	s1,[sp,RS_at+12]
	ldr	s2,[sp,RS_at+16]
	ldr	s3,[sp,RS_at+20]
	ldr	s30,[sp,RS_255]
.ad:	ldr	s4,[sp,RS_at+24]
	fadd	s0,s0,s4
	ldr	s4,[sp,RS_at+28]
	fadd	s1,s1,s4
	ldr	s4,[sp,RS_at+32]
	fadd	s2,s2,s4
	ldr	s4,[sp,RS_at+36]
	fadd	s3,s3,s4
	fcmp	s0,s30
	fcsel	s0,s30,s0,gt
	fcmp	s1,s30
	fcsel	s1,s30,s1,gt
	fcmp	s2,s30
	fcsel	s2,s30,s2,gt
	fcmp	s3,s30
	fcsel	s3,s30,s3,gt
	tbz	w27,18,.nta
	ubfx	w0,w27,19,8
	ucvtf	s4,w0
	fcmp	s3,s4
	b.mi	.xn
.nta:	cbz	w14,.wr
	ldr	w0,[x11,x25,lsl 2]
	ubfx	w1,w0,16,8
	ucvtf	s4,w1				; dr
	ubfx	w1,w0,8,8
	ucvtf	s5,w1				; dg
	and	w1,w0,255
	ucvtf	s6,w1				; db
	fdiv	s7,s3,s30			; fa
	fsub	s31,s15,s7			; 1 - fa
	cmp	w14,1
	b.eq	.b1
	cmp	w14,2
	b.eq	.b2
	cmp	w14,3
	b.eq	.b3
	cmp	w14,4
	b.ne	.wr
	fmul	s4,s4,s31
	fadd	s0,s0,s4
	fmul	s5,s5,s31
	fadd	s1,s1,s5
	fmul	s6,s6,s31
	fadd	s2,s2,s6
	b	.wr
.b1:	fmul	s0,s0,s7
	fmul	s4,s4,s31
	fadd	s0,s0,s4
	fmul	s1,s1,s7
	fmul	s5,s5,s31
	fadd	s1,s1,s5
	fmul	s2,s2,s7
	fmul	s6,s6,s31
	fadd	s2,s2,s6
	b	.wr
.b2:	fmul	s0,s0,s7
	fadd	s0,s0,s4
	fmul	s1,s1,s7
	fadd	s1,s1,s5
	fmul	s2,s2,s7
	fadd	s2,s2,s6
	b	.wr
.b3:	fmul	s0,s0,s4
	fdiv	s0,s0,s30
	fmul	s1,s1,s5
	fdiv	s1,s1,s30
	fmul	s2,s2,s6
	fdiv	s2,s2,s30
.wr:	fadd	s0,s0,s22
	fadd	s1,s1,s22
	fadd	s2,s2,s22
	fcvtzu	w0,s0
	fcvtzu	w1,s1
	fcvtzu	w2,s2
	mov	w3,255
	cmp	w0,w3
	csel	w0,w3,w0,hi
	cmp	w1,w3
	csel	w1,w3,w1,hi
	cmp	w2,w3
	csel	w2,w3,w2,hi
	lsl	w0,w0,16
	orr	w0,w0,w1,lsl 8
	orr	w0,w0,w2
	str	w0,[x11,x25,lsl 2]
	cbz	w13,.xn
	str	s29,[x10,x25,lsl 2]
.xn:	add	w25,w25,1
	b	.x
.yn:	add	w26,w26,1
	b	.y
.out:	add	sp,sp,RS_SIZE
	rest_callee
	epilogue

g3_identity:
	dw	0x3F800000,0,0,0,0,0x3F800000,0,0,0,0,0x3F800000,0,0,0,0,0x3F800000

SW_in		= 0			; 3 sommets
SW_poly		= 168			; 4 sommets
SW_T		= 392			; 16 octets
SW_W		= 408
SW_H		= 412
SW_px		= 416
SW_l		= 424
SW_h		= 428
SW_pas		= 432
SW_SIZE		= 448

; x0 = a, x1 = b, x2 = q, s0 = f : q = a + (b - a) * f, 14 flottants
g3_lerp:
	mov	x3,14
.l:	ldr	s1,[x0],4
	ldr	s2,[x1],4
	fsub	s2,s2,s1
	fmul	s2,s2,s0
	fadd	s1,s1,s2
	str	s1,[x2],4
	subs	x3,x3,1
	b.ne	.l
	ret

; x0 = source, x1 = destination : un sommet
g3_copy:
	ldp	x2,x3,[x0]
	stp	x2,x3,[x1]
	ldp	x2,x3,[x0,16]
	stp	x2,x3,[x1,16]
	ldp	x2,x3,[x0,32]
	stp	x2,x3,[x1,32]
	ldr	x2,[x0,48]
	str	x2,[x1,48]
	ret

n64_sw_frame:
	prologue
	save_all
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	stp	d12,d13,[sp,-16]!
	stp	d14,d15,[sp,-16]!
	sub	sp,sp,SW_SIZE
	mov	x19,x0
	str	x1,[sp,SW_px]
	str	w2,[sp,SW_l]
	str	w3,[sp,SW_h]
	str	w4,[sp,SW_pas]
	ldr	w0,[x19,m_gfxReady]
	tbnz	w0,31,.no
	mov	w1,GF_SIZE
	mul	w0,w0,w1
	movl	x1,m_gfxFrame
	add	x20,x19,x1
	add	x20,x20,x0			; F
	ldr	w5,[x20,GF_width]
	ldr	w6,[x20,GF_height]
	cmp	w5,0
	b.le	.no
	cmp	w6,0
	b.le	.no
	cmp	w5,N64_FBW
	b.gt	.no
	cmp	w6,N64_FBH
	b.gt	.no
	str	w5,[sp,SW_W]
	str	w6,[sp,SW_H]
	mul	w0,w5,w6			; le fond, la profondeur
	movl	x1,M_PIC
	add	x1,x19,x1
	movl	x2,M_ZB
	add	x2,x19,x2
	ldr	w3,[x20,GF_clear]
	mov	w4,0x3F800000
.fl:	str	w3,[x1],4
	str	w4,[x2],4
	subs	w0,w0,1
	b.ne	.fl
	mov	w21,0				; bi
.bt:	ldr	w0,[x20,GF_nb]
	cmp	w21,w0
	b.ge	.sc
	ldr	x22,[x20,GF_b]
	mov	w0,GB_SIZE
	mul	w0,w21,w0
	add	x22,x22,x0			; B
	add	x23,x22,GB_m
	ldr	w0,[x22,GB_flags]
	tbz	w0,17,.m
	adr	x23,g3_identity
.m:	mov	x27,0				; T
	ldr	w0,[x22,GB_tex]
	tbnz	w0,31,.nt
	movl	x1,M_TEXS
	add	x1,x19,x1
	mov	w2,TX_SIZE
	mul	w0,w0,w2
	add	x1,x1,x0
	ldr	x0,[x1,TX_px]
	ldr	w2,[x1,TX_w]
	ldr	w3,[x1,TX_h]
	add	x27,sp,SW_T
	str	x0,[x27,G3T_px]
	mul	w0,w2,w3
	mov	w4,1
	cmp	w2,0
	b.le	.t1
	cmp	w3,0
	b.le	.t1
	cmp	w0,N64_TEX_PIXELS
	b.le	.t2
.t1:	mov	w2,w4
	mov	w3,w4
.t2:	str	w2,[x27,G3T_w]
	str	w3,[x27,G3T_h]
.nt:	mov	w24,0				; t
.tr:	add	w0,w24,3
	ldr	w1,[x22,GB_count]
	cmp	w0,w1
	b.hi	.bn
	ldr	w1,[x22,GB_first]
	add	w0,w0,w1
	ldr	w2,[x20,GF_nv]
	cmp	w0,w2
	b.hi	.bn
	; les trois sommets
	ldr	x0,[x20,GF_v]
	add	w1,w1,w24
	add	x25,x0,x1,lsl 5			; p
	mov	x26,sp				; q
	mov	w28,3
.vx:	ldp	s0,s1,[x25]
	ldp	s2,s3,[x25,8]
	mov	x3,x23
	mov	x4,4
.ro:	ldp	s4,s5,[x3]
	ldp	s6,s7,[x3,8]
	add	x3,x3,16
	fmul	s4,s4,s0
	fmul	s5,s5,s1
	fadd	s4,s4,s5
	fmul	s6,s6,s2
	fadd	s4,s4,s6
	fmul	s7,s7,s3
	fadd	s4,s4,s7
	str	s4,[x26],4
	subs	x4,x4,1
	b.ne	.ro
	ldr	x0,[x25,GV_s]
	str	x0,[x26],8
	mov	x1,0
.cc:	add	x2,x25,GV_r
	ldrb	w0,[x2,x1]
	ucvtf	s0,w0
	str	s0,[x26],4
	add	x1,x1,1
	cmp	x1,8
	b.lo	.cc
	add	x25,x25,GV_SIZE
	subs	w28,w28,1
	b.ne	.vx
	; le plan proche : un polygone de 4 sommets au plus
	mov	w26,0				; n
	mov	x25,0				; k
	movl	w0,F_EPS
	fmov	s31,w0
.pl:	mov	x0,CL_SIZE
	mul	x0,x25,x0
	add	x28,sp,x0			; a
	add	x1,x25,1
	cmp	x1,3
	csel	x1,xzr,x1,eq
	mov	x0,CL_SIZE
	mul	x0,x1,x0
	add	x0,sp,x0			; b
	ldr	s0,[x28,CL_z]
	ldr	s1,[x28,CL_w]
	fadd	s16,s0,s1			; da
	ldr	s0,[x0,CL_z]
	ldr	s2,[x0,CL_w]
	fadd	s17,s0,s2			; db
	mov	w3,0				; ina
	fcmp	s16,0.0
	b.lt	.p1
	fcmp	s1,s31
	cset	w3,gt
.p1:	mov	w4,0				; inb
	fcmp	s17,0.0
	b.lt	.p2
	fcmp	s2,s31
	cset	w4,gt
.p2:	stp	x3,x4,[sp,-16]!
	str	x0,[sp,-16]!
	cbz	w3,.p3
	mov	x0,x28
	mov	w1,CL_SIZE
	mul	w1,w26,w1
	add	x2,sp,32+SW_poly
	add	x1,x2,x1
	bl	g3_copy
	add	w26,w26,1
.p3:	ldr	x1,[sp],16
	ldp	x3,x4,[sp],16
	cmp	w3,w4
	b.eq	.pn
	cmp	w26,4
	b.ge	.pn
	fsub	s0,s16,s17
	fdiv	s0,s16,s0			; f
	mov	x0,x28
	mov	w2,CL_SIZE
	mul	w2,w26,w2
	add	x3,sp,SW_poly
	add	x2,x3,x2
	mov	x4,x2
	bl	g3_lerp
	add	w26,w26,1
	ldr	s0,[x4,CL_w]
	fcmp	s0,s31
	b.hi	.pn
	sub	w26,w26,1
.pn:	add	x25,x25,1
	cmp	x25,3
	b.lo	.pl
	mov	w25,1				; k
.fa:	add	w0,w25,1
	cmp	w0,w26
	b.ge	.tn
	add	x0,sp,SW_poly			; poly[0]
	mov	w1,CL_SIZE
	mul	w1,w25,w1
	add	x1,x0,x1
	add	x2,x1,CL_SIZE
	ldr	w3,[x22,GB_flags]
	mov	x4,x27
	ldr	w5,[sp,SW_W]
	ldr	w6,[sp,SW_H]
	bl	g3_raster
	add	w25,w25,1
	b	.fa
.tn:	add	w24,w24,3
	b	.tr
.bn:	add	w21,w21,1
	b	.bt
.sc:	; a l'echelle, au plus proche
	ldr	w5,[sp,SW_W]
	ldr	w6,[sp,SW_H]
	ldr	w7,[sp,SW_l]
	ldr	w8,[sp,SW_h]
	ldr	x9,[sp,SW_px]
	ldrsw	x10,[sp,SW_pas]
	lsl	x10,x10,2
	movl	x11,M_PIC
	add	x11,x19,x11
	mov	w12,0				; y
.sy:	cmp	w12,w8
	b.ge	.yes
	mul	w0,w12,w6
	sdiv	w0,w0,w8
	mul	w0,w0,w5
	add	x13,x11,x0,lsl 2		; r
	mov	x14,x9				; d
	mov	w15,0				; x
	mov	w16,0				; acc
	mov	x17,0				; x * W / w
.sx:	cmp	w15,w7
	b.ge	.sn
	ldr	w0,[x13,x17,lsl 2]
	str	w0,[x14],4
	add	w16,w16,w5
.sa:	cmp	w16,w7
	b.lt	.sb
	sub	w16,w16,w7
	add	x17,x17,1
	b	.sa
.sb:	add	w15,w15,1
	b	.sx
.sn:	add	x9,x9,x10
	add	w12,w12,1
	b	.sy
.yes:	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	add	sp,sp,SW_SIZE
	ldp	d14,d15,[sp],16
	ldp	d12,d13,[sp],16
	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	rest_all
	epilogue
