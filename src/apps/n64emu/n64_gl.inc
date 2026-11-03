; ============================================================================
;  n64_gl.inc - le flux pour Mesa (m_glOn) : rien n'est combine ici. Les lots
;  gardent le combineur et le blender du RDP, les sommets l'ombrage et les
;  coordonnees brutes ; gl/n64gl.c en fait des shaders GLSL ES (docs/EMULATEURS.md).
;  Memes tableaux que GF_v et GF_b, autre sens des champs : struct n64gl_batch
;  et n64gl_vert de gl/n64gl.h.
; ============================================================================

G_FOG		= 0x10000
GL_tex0		= 8
GL_flags	= 12
GL_combH	= 16
GL_combL	= 20
GL_omH		= 24
GL_omL		= 28
GL_prim		= 32
GL_env		= 36
GL_fog		= 40
GL_blend	= 44
GL_tex1		= 48
GL_tile0	= 52
GL_wrap0	= 56
GL_tile1	= 60
GL_wrap1	= 64
GL_sc0		= 68
GL_sc1		= 72
GL_primLod	= 76

; w4, w5 = les lettres a b d c d'un cycle, rgb et alpha, un octet chacune ; w2, w3 = les bits de TEXEL0 et
; TEXEL1 pour ce cycle : w0 |= ce qu'il lit. Perd w1, w4-w7
gl_cycle:
	mov	w6,4
.l:	and	w1,w4,255
	cmp	w1,1
	b.eq	.t0
	cmp	w1,2
	b.eq	.t1
	cmp	w6,1				; c rgb : aussi l'alpha des textures
	b.ne	.a
	cmp	w1,8
	b.eq	.t0
	cmp	w1,9
	b.ne	.a
.t1:	orr	w0,w0,w3
	b	.a
.t0:	orr	w0,w0,w2
.a:	and	w1,w5,255
	cmp	w1,1
	b.ne	.a2
	orr	w0,w0,w2
.a2:	cmp	w1,2
	b.ne	.n
	orr	w0,w0,w3
.n:	lsr	w4,w4,8
	lsr	w5,w5,8
	subs	w6,w6,1
	b.ne	.l
	ret

; -> w0 : 1 la tuile de base, 2 la suivante, lues par le combineur. Au 2e cycle
; le RDP a avance : TEXEL0 y est la tuile suivante, TEXEL1 la tuile de base.
gl_texmask:
	mov	x8,x30
	ldr	w9,[x19,m_combH]
	ldr	w10,[x19,m_combL]
	ldr	w11,[x19,m_omH]
	ubfx	w11,w11,20,2
	mov	w0,0
	cmp	w11,2
	b.lo	.c
	cset	w0,eq				; copie : TEXEL0 ; plein : rien
	br	x8
.c:	mov	w2,1
	mov	w3,2
	ubfx	w4,w9,20,4			; a0
	ubfx	w1,w10,28,4			; b0
	orr	w4,w4,w1,lsl 8
	ubfx	w1,w10,15,3			; d0
	orr	w4,w4,w1,lsl 16
	ubfx	w1,w9,15,5			; c0
	orr	w4,w4,w1,lsl 24
	ubfx	w5,w9,12,3			; Aa0
	ubfx	w1,w10,12,3			; Ab0
	orr	w5,w5,w1,lsl 8
	ubfx	w1,w10,9,3			; Ad0
	orr	w5,w5,w1,lsl 16
	ubfx	w1,w9,9,3			; Ac0
	orr	w5,w5,w1,lsl 24
	bl	gl_cycle
	cmp	w11,1
	b.ne	.r
	mov	w2,2
	mov	w3,1
	ubfx	w4,w9,5,4			; a1
	ubfx	w1,w10,24,4			; b1
	orr	w4,w4,w1,lsl 8
	ubfx	w1,w10,6,3			; d1
	orr	w4,w4,w1,lsl 16
	and	w1,w9,31			; c1
	orr	w4,w4,w1,lsl 24
	ubfx	w5,w10,21,3			; Aa1
	ubfx	w1,w10,3,3			; Ab1
	orr	w5,w5,w1,lsl 8
	and	w1,w10,7			; Ad1
	orr	w5,w5,w1,lsl 16
	ubfx	w1,w10,18,3			; Ac1
	orr	w5,w5,w1,lsl 24
	bl	gl_cycle
.r:	br	x8

; x0 = deux mots a remplir, w2 = tuile : ses parametres
gl_tile:
	stp	x29,x30,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x24,x0
	and	w2,w2,7
	mov	w1,TI_SIZE
	mul	w2,w2,w1
	add	x23,x19,m_tiles
	add	x23,x23,x2
	ldr	w2,[x23,TI_sl]
	ldr	w1,[x23,TI_tl]
	orr	w2,w2,w1,lsl 12
	ldr	w1,[x23,TI_shifts]
	orr	w2,w2,w1,lsl 24
	ldr	w1,[x23,TI_shiftt]
	orr	w2,w2,w1,lsl 28
	str	w2,[x24]
	bl	gfx_tri.wrap
	str	w0,[x24,4]
	ldp	x23,x24,[sp],16
	ldp	x29,x30,[sp],16
	ret

; w0 = tuile de base, w1 = 0 : pas de texture -> w0 = texture ; remplit m_glS
gl_state:
	prologue
	save_callee
	mov	w21,w0
	mov	w23,w1
	mov	w24,-1				; tex0
	mov	w25,-1				; tex1
	cbz	w23,.nt
	bl	gl_texmask
	mov	w26,w0
	tbz	w26,0,.t1
	mov	w0,w21
	bl	gfx_texture
	mov	w24,w0
.t1:	tbz	w26,1,.nt
	add	w0,w21,1
	bl	gfx_texture
	mov	w25,w0
.nt:	adm	x22,m_glS
	str	w24,[x22,GL_tex0]
	str	w25,[x22,GL_tex1]
	ldr	w0,[x19,m_combH]
	str	w0,[x22,GL_combH]
	ldr	w0,[x19,m_combL]
	str	w0,[x22,GL_combL]
	ldr	w0,[x19,m_omH]
	str	w0,[x22,GL_omH]
	ldr	w0,[x19,m_omL]
	str	w0,[x22,GL_omL]
	ldr	w0,[x19,m_primW]
	str	w0,[x22,GL_prim]
	ldr	w0,[x19,m_envW]
	str	w0,[x22,GL_env]
	ldr	w0,[x19,m_fogW]
	str	w0,[x22,GL_fog]
	ldr	w0,[x19,m_blendW]
	str	w0,[x22,GL_blend]
	ldr	w0,[x19,m_primLod]
	str	w0,[x22,GL_primLod]
	ldr	w0,[x19,m_scissor]
	ldr	w1,[x19,m_scissor+4]
	orr	w0,w0,w1,lsl 16
	str	w0,[x22,GL_sc0]
	ldr	w0,[x19,m_scissor+8]
	ldr	w1,[x19,m_scissor+12]
	orr	w0,w0,w1,lsl 16
	str	w0,[x22,GL_sc1]
	add	x0,x22,GL_tile0
	mov	w2,w21
	bl	gl_tile
	add	x0,x22,GL_tile1
	add	w2,w21,1
	bl	gl_tile
	mov	w0,w24
	rest_callee
	epilogue

; un sommet de gfx_tri : x27 = VX, x28 = g. Perd x0, x1, x8-x10, s0-s3
gl_vertex:
	mov	x8,x30
	ldp	s0,s1,[x27,VX_s]
	fconst	s2,w0,F_32
	fdiv	s0,s0,s2
	fdiv	s1,s1,s2
	stp	s0,s1,[x28,GV_s]
	str	wzr,[x28,GV_r2]
	ldr	w9,[x19,m_geom]
	mov	w10,-1
	tst	w9,G_SHADE
	b.eq	.sh
	ldr	s0,[x27,VX_r]
	bl	f_c8
	bfxil	w10,w0,0,8
	ldr	s0,[x27,VX_g]
	bl	f_c8
	bfi	w10,w0,8,8
	ldr	s0,[x27,VX_b]
	bl	f_c8
	bfi	w10,w0,16,8
	ldr	s0,[x27,VX_a]
	bl	f_c8
	bfi	w10,w0,24,8
.sh:	tst	w9,G_FOG
	b.eq	.st
	ldp	s2,s3,[x27,VX_z]		; le brouillard du microcode prend l'alpha : z / w * fm + fo, 8 bits
	fconst	s1,w0,F_0_01
	mov	w0,0
	fcmp	s3,s1
	b.mi	.fg
	fdiv	s2,s2,s3
	ldr	s1,[x19,m_fogMul]
	fmul	s2,s2,s1
	ldr	s1,[x19,m_fogOff]
	fadd	s2,s2,s1
	fcvtzs	w0,s2
	cmp	w0,0
	csel	w0,wzr,w0,lt
	mov	w1,255
	cmp	w0,255
	csel	w0,w1,w0,gt
.fg:	bfi	w10,w0,24,8
.st:	str	w10,[x28,GV_r]
	br	x8

; x0 = 3 sommets, w2 = drapeaux GF_ ; l'etat : m_glS
gl_emit:
	adm	x10,m_glS
	str	w2,[x10,GL_flags]
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
	ldr	w5,[x3,GF_nb]
	ldr	x6,[x3,GF_b]
	cbz	w5,.new
	sub	w7,w5,1
	mov	w8,GB_SIZE
	mul	w7,w7,w8
	add	x7,x6,x7			; dernier
	ldr	w8,[x7,GB_first]
	ldr	w9,[x7,GB_count]
	add	w8,w8,w9
	cmp	w8,w4
	b.ne	.new
	mov	x8,8
.cmp:	ldr	x9,[x7,x8]
	ldr	x11,[x10,x8]
	cmp	x9,x11
	b.ne	.new
	add	x8,x8,8
	cmp	x8,GB_SIZE
	b.lo	.cmp
	b	.copy
.new:	cmp	w5,N64_MAXB
	b.ge	.r
	mov	w8,GB_SIZE
	mul	w8,w5,w8
	add	x7,x6,x8
	add	w5,w5,1
	str	w5,[x3,GF_nb]
	str	w4,[x7,GB_first]
	str	wzr,[x7,GB_count]
	mov	x8,8
.st:	ldr	x9,[x10,x8]
	str	x9,[x7,x8]
	add	x8,x8,8
	cmp	x8,GB_SIZE
	b.lo	.st
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
