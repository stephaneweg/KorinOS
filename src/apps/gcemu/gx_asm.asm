; ============================================================================
;  gx_asm.asm - les boucles chaudes du GX de gcemu, en assembleur (docs/EMULATEURS.md)
;  -> un objet ELF, relie au C++ : fonctions feuilles, sans relocation, l'AAPCS64.
;
;    gx_decode     les sommets de la FIFO -> GxVertex (gc_gxdraw.cpp)
;    gx_positions  position -> oeil -> clip (gxes_rec.cpp)
;    gx_expand     les sommets de chaque primitive, a la suite (gxes_rec.cpp)
;    gx_yuv        une ligne du XFB, YUV 4:2:2 -> 0x00RRGGBB (gc_hw.cpp)
;
;  Chaque fonction donne au bit pres ce que donne son C++ (tools/emu/check_gc.sh,
;  avec -ffp-contract=off : ici fmul puis fadd, jamais fmla).
; ============================================================================
format ELF64
processor cpu64_v8 +cpu64_fp +cpu64_simd
public gx_decode
public gx_positions
public gx_expand
public gx_yuv

; struct GxDecode (gc.h)
DC_PLAN		= 0
DC_DATA		= 8
DC_VX		= 16
DC_MEM		= 24
DC_T		= 32
DC_TMK		= 40
DC_NP		= 48
DC_COUNT	= 52
DC_VSZ		= 56
DC_PNIDX	= 60
DC_NTM		= 64

; struct Att (gc_gxdraw.cpp)
AT_KIND		= 0
AT_TYPE		= 4
AT_FMT		= 8
AT_N		= 12
AT_CS		= 16
AT_ADV		= 20
AT_EXTRA	= 24
AT_SC		= 28
AT_BASE		= 32
AT_STRIDE	= 36
AT_SIZE		= 40

; struct GxVertex
GV_POS		= 0
GV_NRM		= 12
GV_C0		= 24
GV_C1		= 28
GV_TC		= 32
GV_MTX		= 96
GV_SIZE		= 108

MEM1_LIMIT	= 24*1024*1024-64

; struct GxPos (gxes.h)
GP_VX		= 0
GP_XF		= 8
GP_OUT		= 16
GP_OC		= 24
GP_EYE		= 32
GP_NV		= 40
GP_STRIDE	= 44
GP_PROJ		= 48
GP_LASTPM	= 52
GP_AX		= 56
GP_BX		= 60
GP_AY		= 64
GP_BY		= 68
GP_AZ		= 72
GP_BZ		= 76
GP_P0		= 80		; P0..P5
GP_VP0		= 104
GP_VP1		= 108
GP_COLS		= 112		; les 4 colonnes de la matrice du dernier indice

section '.text' executable

; ---------------------------------------------------------------------------
;  x0 = GxDecode
; ---------------------------------------------------------------------------
gx_decode:
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	ldr	x19,[x0,DC_PLAN]
	ldr	x21,[x0,DC_DATA]
	ldr	x24,[x0,DC_VX]
	ldr	x25,[x0,DC_MEM]
	ldr	x26,[x0,DC_T]
	ldr	x9,[x0,DC_TMK]
	ldr	w20,[x0,DC_NP]
	ldr	w22,[x0,DC_COUNT]
	ldr	w23,[x0,DC_VSZ]
	ldr	w10,[x0,DC_PNIDX]
	ldr	w11,[x0,DC_NTM]
	movz	x17,MEM1_LIMIT and 0xFFFF
	movk	x17,MEM1_LIMIT shr 16,lsl 16
	ldp	q16,q17,[x26]			; le sommet par defaut
	ldp	q18,q19,[x26,32]
	ldp	q20,q21,[x26,64]
	ldur	q22,[x26,GV_SIZE-16]
.vtx:	cbz	w22,.done
	stp	q16,q17,[x24]
	stp	q18,q19,[x24,32]
	stp	q20,q21,[x24,64]
	stur	q22,[x24,GV_SIZE-16]
	mov	x12,x21				; q
	cbz	w10,.tm
	ldrb	w0,[x12],1
	and	w0,w0,63
	strb	w0,[x24,GV_MTX]
.tm:	mov	w13,0
.tml:	cmp	w13,w11
	b.hs	.att
	ldrb	w0,[x12],1
	and	w0,w0,63
	ldrsw	x1,[x9,x13,lsl 2]
	add	x1,x1,x24
	strb	w0,[x1,GV_MTX+1]
	add	w13,w13,1
	b	.tml
.att:	mov	x14,x19				; le plan
	mov	w15,w20
.al:	cbz	w15,.next
	ldr	w2,[x14,AT_TYPE]
	cmp	w2,1
	b.ne	.idx
	mov	x1,x12				; direct
	ldr	w0,[x14,AT_ADV]
	add	x12,x12,x0
	b	.src
.idx:	cmp	w2,2
	b.ne	.i16
	ldrb	w0,[x12],1
	b	.ia
.i16:	ldrh	w0,[x12],2
	rev16	w0,w0
.ia:	ldr	w2,[x14,AT_EXTRA]
	add	x12,x12,x2
	ldp	w2,w3,[x14,AT_BASE]
	mul	w3,w0,w3
	add	w3,w3,w2
	and	w3,w3,0x01FFFFFF
	cmp	x3,x17
	csel	x3,xzr,x3,hs
	add	x1,x25,x3
.src:	ldr	w2,[x14,AT_KIND]
	cmp	w2,1
	b.ls	.pn
	cmp	w2,4
	b.hs	.tc
	add	x5,x24,GV_C0			; une couleur
	cmp	w2,2
	add	x6,x5,4
	csel	x5,x5,x6,eq
	ldr	w2,[x14,AT_FMT]
	cmp	w2,1
	b.eq	.rgb
	cmp	w2,2
	b.eq	.rgb
	cbz	w2,.c565
	cmp	w2,3
	b.eq	.c4444
	cmp	w2,4
	b.eq	.c6666
	ldr	w0,[x1]
	str	w0,[x5]
	b	.anext
.rgb:	ldrb	w0,[x1]
	ldrb	w2,[x1,1]
	ldrb	w3,[x1,2]
	strb	w0,[x5]
	strb	w2,[x5,1]
	strb	w3,[x5,2]
	mov	w0,255
	strb	w0,[x5,3]
	b	.anext
.c565:	ldrh	w0,[x1]
	rev16	w0,w0
	ubfx	w2,w0,11,5
	lsl	w3,w2,3
	orr	w3,w3,w2,lsr 2
	strb	w3,[x5]
	ubfx	w2,w0,5,6
	lsl	w3,w2,2
	orr	w3,w3,w2,lsr 4
	strb	w3,[x5,1]
	and	w2,w0,31
	lsl	w3,w2,3
	orr	w3,w3,w2,lsr 2
	strb	w3,[x5,2]
	mov	w0,255
	strb	w0,[x5,3]
	b	.anext
.c4444:	ldrh	w0,[x1]
	rev16	w0,w0
	ubfx	w2,w0,12,4
	orr	w2,w2,w2,lsl 4
	strb	w2,[x5]
	ubfx	w2,w0,8,4
	orr	w2,w2,w2,lsl 4
	strb	w2,[x5,1]
	ubfx	w2,w0,4,4
	orr	w2,w2,w2,lsl 4
	strb	w2,[x5,2]
	and	w2,w0,15
	orr	w2,w2,w2,lsl 4
	strb	w2,[x5,3]
	b	.anext
.c6666:	ldrb	w0,[x1]
	ldrb	w2,[x1,1]
	ldrb	w3,[x1,2]
	lsl	w0,w0,16
	orr	w0,w0,w2,lsl 8
	orr	w0,w0,w3
	ubfx	w2,w0,18,6
	lsl	w3,w2,2
	orr	w3,w3,w2,lsr 4
	strb	w3,[x5]
	ubfx	w2,w0,12,6
	lsl	w3,w2,2
	orr	w3,w3,w2,lsr 4
	strb	w3,[x5,1]
	ubfx	w2,w0,6,6
	lsl	w3,w2,2
	orr	w3,w3,w2,lsr 4
	strb	w3,[x5,2]
	and	w2,w0,63
	lsl	w3,w2,2
	orr	w3,w3,w2,lsr 4
	strb	w3,[x5,3]
	b	.anext
.pn:	add	x5,x24,GV_POS
	ldr	w3,[x14,AT_N]
	cbz	w2,.comp
	add	x5,x24,GV_NRM
	mov	w3,3
	b	.comp
.tc:	sub	w2,w2,4
	add	x5,x24,GV_TC
	add	x5,x5,x2,lsl 3
	ldr	w3,[x14,AT_N]
.comp:	; x1 = source, x5 = destination, w3 = composantes
	ldr	w2,[x14,AT_FMT]
	ldr	s0,[x14,AT_SC]
	ldr	w4,[x14,AT_CS]
	cbz	w2,.u8
	cmp	w2,1
	b.eq	.s8
	cmp	w2,2
	b.eq	.u16
	cmp	w2,3
	b.eq	.s16
.f32:	ldr	w0,[x1],4
	rev	w0,w0
	str	w0,[x5],4
	subs	w3,w3,1
	b.ne	.f32
	b	.anext
.u8:	ldrb	w0,[x1],1
	ucvtf	s1,w0
	fmul	s1,s1,s0
	str	s1,[x5],4
	subs	w3,w3,1
	b.ne	.u8
	b	.anext
.s8:	ldrsb	w0,[x1],1
	scvtf	s1,w0
	fmul	s1,s1,s0
	str	s1,[x5],4
	subs	w3,w3,1
	b.ne	.s8
	b	.anext
.u16:	ldrh	w0,[x1],2
	rev16	w0,w0
	ucvtf	s1,w0
	fmul	s1,s1,s0
	str	s1,[x5],4
	subs	w3,w3,1
	b.ne	.u16
	b	.anext
.s16:	ldrh	w0,[x1],2
	rev16	w0,w0
	sxth	w0,w0
	scvtf	s1,w0
	fmul	s1,s1,s0
	str	s1,[x5],4
	subs	w3,w3,1
	b.ne	.s16
.anext:	add	x14,x14,AT_SIZE
	sub	w15,w15,1
	b	.al
.next:	add	x21,x21,x23
	add	x24,x24,GV_SIZE
	sub	w22,w22,1
	b	.vtx
.done:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ret

; ---------------------------------------------------------------------------
;  x0 = GxPos : pour chaque sommet, l'oeil (4 flottants, le dernier sans valeur)
;  dans eye[], le clip dans out (pas : stride octets), les plans dans oc[]
; ---------------------------------------------------------------------------
gx_positions:
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	stp	d12,d13,[sp,-16]!
	stp	d14,d15,[sp,-16]!
	ldp	x1,x2,[x0,GP_VX]		; sommets, memoire XF
	ldp	x3,x4,[x0,GP_OUT]		; sortie, plans
	ldr	x5,[x0,GP_EYE]
	ldp	w6,w7,[x0,GP_NV]		; nombre, pas
	ldp	w8,w9,[x0,GP_PROJ]		; projection orthogonale, dernier indice
	ldp	q20,q21,[x0,GP_COLS]
	ldp	q22,q23,[x0,GP_COLS+32]
	ldp	s24,s25,[x0,GP_AX]
	ldp	s26,s27,[x0,GP_AY]
	ldp	s28,s29,[x0,GP_AZ]
	ldp	s8,s9,[x0,GP_P0]
	ldp	s10,s11,[x0,GP_P0+8]
	ldp	s12,s13,[x0,GP_P0+16]
	ldp	s14,s15,[x0,GP_VP0]
	fmov	s30,1.0
	movi	v31.4s,0
.v:	cbz	w6,.end
	ldrb	w10,[x1,GV_MTX]
	cmp	w10,w9
	b.eq	.xf
	mov	w9,w10				; une autre matrice : ses trois lignes, en colonnes
	lsl	w12,w10,2
	add	x13,x2,x12,lsl 2
	ldr	q0,[x13]
	add	w12,w10,1
	and	w12,w12,63
	add	x13,x2,x12,lsl 4
	ldr	q1,[x13]
	add	w12,w10,2
	and	w12,w12,63
	add	x13,x2,x12,lsl 4
	ldr	q2,[x13]
	trn1	v4.4s,v0.4s,v1.4s
	trn2	v5.4s,v0.4s,v1.4s
	trn1	v6.4s,v2.4s,v31.4s
	trn2	v7.4s,v2.4s,v31.4s
	zip1	v20.2d,v4.2d,v6.2d
	zip1	v21.2d,v5.2d,v7.2d
	zip2	v22.2d,v4.2d,v6.2d
	zip2	v23.2d,v5.2d,v7.2d
.xf:	ldp	s0,s1,[x1,GV_POS]
	ldr	s2,[x1,GV_POS+8]
	fmul	v3.4s,v20.4s,v0.s[0]		; oeil = c0 x + c1 y + c2 z + c3, dans cet ordre
	fmul	v4.4s,v21.4s,v1.s[0]
	fadd	v3.4s,v3.4s,v4.4s
	fmul	v4.4s,v22.4s,v2.s[0]
	fadd	v3.4s,v3.4s,v4.4s
	fadd	v3.4s,v3.4s,v23.4s
	str	q3,[x5],16
	mov	s5,v3.s[1]
	mov	s6,v3.s[2]
	cbz	w8,.persp
	fmul	s0,s8,s3			; X, Y, Z ; W = 1
	fadd	s0,s0,s9
	fmul	s1,s10,s5
	fadd	s1,s1,s11
	fmul	s2,s12,s6
	fadd	s2,s2,s13
	fmov	s7,s30
	b	.vp
.persp:	fmul	s0,s8,s3
	fmul	s4,s9,s6
	fadd	s0,s0,s4
	fmul	s1,s10,s5
	fmul	s4,s11,s6
	fadd	s1,s1,s4
	fmul	s2,s12,s6
	fadd	s2,s2,s13
	fneg	s7,s6
.vp:	fmul	s0,s0,s14
	fmul	s1,s1,s15
	fadd	s2,s2,s2			; Z = 2 Z + W
	fadd	s2,s2,s7
	fadd	s0,s0,s7			; x' = ax W + bx (X + W), et ainsi de suite
	fmul	s0,s25,s0
	fmul	s3,s24,s7
	fadd	s0,s3,s0
	fadd	s1,s1,s7
	fmul	s1,s27,s1
	fmul	s3,s26,s7
	fadd	s1,s3,s1
	fadd	s2,s2,s7
	fmul	s2,s29,s2
	fmul	s3,s28,s7
	fadd	s2,s3,s2
	stp	s0,s1,[x3]
	stp	s2,s7,[x3,8]
	fadd	s4,s2,s7
	mov	w12,1
	fcmp	s4,0.0
	csel	w13,wzr,w12,ge
	mov	w12,2
	fcmp	s7,0.0
	csel	w14,wzr,w12,gt
	orr	w13,w13,w14
	strb	w13,[x4],1
	add	x3,x3,x7
	add	x1,x1,GV_SIZE
	sub	w6,w6,1
	b	.v
.end:	str	w9,[x0,GP_LASTPM]
	stp	q20,q21,[x0,GP_COLS]
	stp	q22,q23,[x0,GP_COLS+32]
	ldp	d14,d15,[sp],16
	ldp	d12,d13,[sp],16
	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ret

; ---------------------------------------------------------------------------
;  (x0 = destination, x1 = sommets, x2 = indices, w3 = primitives, w4 = sommets par
;  primitive, w5 = pas en octets, multiple de 16, w6 = sommets, x7 = plans) -> primitives gardees
; ---------------------------------------------------------------------------
gx_expand:
	mov	w9,0				; gardees
	mov	w16,w5
.p:	cbz	w3,.out
	mov	w10,w4
	mov	w11,0xFF
	mov	x12,x2				; les plans : tous dehors du meme cote, laissee
.o:	ldr	w13,[x12],4
	cmp	w13,w6
	csel	w13,wzr,w13,hs
	ldrb	w14,[x7,x13]
	and	w11,w11,w14
	subs	w10,w10,1
	b.ne	.o
	cbnz	w11,.skip
	mov	w10,w4
	mov	x12,x2
.c:	ldr	w13,[x12],4
	cmp	w13,w6
	csel	w13,wzr,w13,hs
	mul	x14,x13,x16
	add	x14,x1,x14
	mov	w15,w5
.q:	ldr	q0,[x14],16
	str	q0,[x0],16
	subs	w15,w15,16
	b.ne	.q
	subs	w10,w10,1
	b.ne	.c
	add	w9,w9,1
.skip:	add	x2,x2,x4,lsl 2
	sub	w3,w3,1
	b	.p
.out:	mov	w0,w9
	ret

; ---------------------------------------------------------------------------
;  (x0 = source YUYV, x1 = destination, w2 = largeur, paire) : 4 points a la fois
;  r = (298 y' + 409 v' + 128) >> 8, g = (298 y' - 100 u' - 208 v' + 128) >> 8,
;  b = (298 y' + 516 u' + 128) >> 8, chaque valeur bornee a 0..255
; ---------------------------------------------------------------------------
gx_yuv:
	movz	w3,298
	dup	v16.4s,w3
	movz	w3,409
	dup	v17.4s,w3
	movz	w3,100
	dup	v18.4s,w3
	movz	w3,208
	dup	v19.4s,w3
	movz	w3,516
	dup	v20.4s,w3
	movz	w3,128
	dup	v21.4s,w3
	movz	w3,255
	dup	v22.4s,w3
	movi	v23.4s,0
	movz	w3,16
	dup	v24.4s,w3
.l:	cmp	w2,4
	b.lo	.tail
	ld1	{v0.8b},[x0],8			; y0 u0 y1 v0 y2 u1 y3 v1
	uxtl	v1.8h,v0.8b
	uxtl	v2.4s,v1.4h			; y0 u0 y1 v0
	uxtl2	v3.4s,v1.8h			; y2 u1 y3 v1
	uzp1	v4.4s,v2.4s,v3.4s		; y0 y1 y2 y3
	uzp2	v5.4s,v2.4s,v3.4s		; u0 v0 u1 v1
	sub	v4.4s,v4.4s,v24.4s
	movz	w3,128
	dup	v6.4s,w3
	sub	v5.4s,v5.4s,v6.4s
	trn1	v6.4s,v5.4s,v5.4s		; u0 u0 u1 u1
	trn2	v7.4s,v5.4s,v5.4s		; v0 v0 v1 v1
	mul	v4.4s,v4.4s,v16.4s
	add	v4.4s,v4.4s,v21.4s
	mul	v25.4s,v7.4s,v17.4s
	add	v25.4s,v25.4s,v4.4s
	mul	v26.4s,v6.4s,v18.4s
	mul	v27.4s,v7.4s,v19.4s
	add	v26.4s,v26.4s,v27.4s
	sub	v26.4s,v4.4s,v26.4s
	mul	v27.4s,v6.4s,v20.4s
	add	v27.4s,v27.4s,v4.4s
	sshr	v25.4s,v25.4s,8
	sshr	v26.4s,v26.4s,8
	sshr	v27.4s,v27.4s,8
	smin	v25.4s,v25.4s,v22.4s
	smin	v26.4s,v26.4s,v22.4s
	smin	v27.4s,v27.4s,v22.4s
	smax	v25.4s,v25.4s,v23.4s
	smax	v26.4s,v26.4s,v23.4s
	smax	v27.4s,v27.4s,v23.4s
	shl	v25.4s,v25.4s,16
	shl	v26.4s,v26.4s,8
	orr	v25.16b,v25.16b,v26.16b
	orr	v25.16b,v25.16b,v27.16b
	st1	{v25.4s},[x1],16
	sub	w2,w2,4
	b	.l
.tail:	ret
