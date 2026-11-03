; ============================================================================
;  gba_ppu.inc - l'image, une ligne a la fois (au debut du H-blank de chaque
;  ligne) : plans de texte et affines, modes bitmap, sprites (normaux et
;  affines), fenetres (0, 1, sprites, dehors), priorites, effets (melange,
;  plus clair, plus sombre, sprites semi-transparents), mosaique.
; ============================================================================

; x1 = line, w0 = mosH (> 1) ; perd x2-x4
mos_apply:
	mov	x2,0				; x
.l:	udiv	w3,w2,w0
	msub	w3,w3,w0,w2
	sub	w3,w2,w3
	ldr	w4,[x1,x3,lsl 2]
	str	w4,[x1,x2,lsl 2]
	add	x2,x2,1
	cmp	x2,GBA_W
	b.lo	.l
	ret

; w0 = bg, x1 = line
render_text:
	prologue
	mov	x21,x1				; line
	add	x2,x19,A_IO
	add	x3,x2,0x08
	ldrh	w4,[x3,x0,lsl 1]		; cnt
	add	x3,x2,0x10
	add	x3,x3,x0,lsl 2
	ldrh	w9,[x3]
	and	w9,w9,511			; hofs
	ldrh	w5,[x3,2]
	and	w5,w5,511			; vofs
	ubfx	w24,w4,2,2
	lsl	w24,w24,14			; charBase
	ubfx	w26,w4,8,5
	lsl	w26,w26,11			; screenBase
	ubfx	w14,w4,7,1			; bpp8
	lsr	w11,w4,14			; size
	mov	w10,255
	tbz	w11,0,.a
	mov	w10,511				; wmask
.a:	mov	w6,255
	tbz	w11,1,.b
	mov	w6,511				; hmask
.b:	ldr	w7,[x19,a_vcount]		; ly
	mov	w15,1				; mosH
	tbz	w4,6,.c
	io_h	w0,0x4C
	ubfx	w1,w0,4,4
	add	w1,w1,1				; mv
	and	w0,w0,15
	add	w15,w0,1
	udiv	w0,w7,w1
	msub	w0,w0,w1,w7
	sub	w7,w7,w0
.c:	add	w7,w7,w5
	and	w7,w7,w6			; y
	ubfx	w12,w7,8,1			; sby
	ubfx	w0,w7,3,5
	add	w26,w26,w0,lsl 6		; rowBase
	and	w13,w7,7			; ty0
	add	x22,x19,A_VRAM
	add	x23,x19,A_PAL
	mov	w25,0				; x
.tile:	add	w0,w25,w9
	and	w0,w0,w10			; px
	ubfx	w1,w0,8,1			; sbx
	mov	w2,0				; block
	cbz	w11,.bk
	mov	w2,w1
	cmp	w11,1
	b.eq	.bk
	mov	w2,w12
	cmp	w11,2
	b.eq	.bk
	add	w2,w1,w12,lsl 1
.bk:	lsl	w2,w2,11
	add	w2,w2,w26
	ubfx	w1,w0,3,5
	add	w2,w2,w1,lsl 1
	and	w2,w2,0xFFFF
	ldrh	w8,[x22,x2]			; e
	and	w1,w8,0x3FF			; tile
	mov	w2,w13				; ty
	tbz	w8,11,.ny
	mov	w2,7
	sub	w2,w2,w13
.ny:	mov	w7,0				; flip
	tbz	w8,10,.nx
	mov	w7,7
.nx:	and	w3,w0,7				; tx
	mov	w4,8
	sub	w4,w4,w3			; n
	mov	w5,GBA_W
	sub	w5,w5,w25
	cmp	w4,w5
	csel	w4,w5,w4,gt
	add	x5,x21,x25,lsl 2		; sortie
	add	w25,w25,w4
	cbz	w14,.b4
	; 8 bits
	lsl	w1,w1,6
	add	w1,w1,w24
	add	w1,w1,w2,lsl 3			; a
	mov	x6,0				; row
	cmp	w1,0x10000
	b.hs	.r8
	ldr	x6,[x22,x1]
.r8:	eor	w2,w3,w7
	lsl	w2,w2,3
	lsr	x2,x6,x2
	and	w2,w2,0xFF			; idx
	cbz	w2,.t8
	ldrh	w2,[x23,x2,lsl 1]
	str	w2,[x5],4
	b	.n8
.t8:	mov	w2,0x80000000
	str	w2,[x5],4
.n8:	add	w3,w3,1
	subs	w4,w4,1
	b.ne	.r8
	b	.nt
.b4:	lsl	w1,w1,5
	add	w1,w1,w24
	add	w1,w1,w2,lsl 2			; a
	mov	w6,0
	cmp	w1,0x10000
	b.hs	.r4
	ldr	w6,[x22,x1]
.r4:	lsr	w1,w8,12
	add	x1,x23,x1,lsl 5			; pb
.p4:	eor	w2,w3,w7
	lsl	w2,w2,2
	lsr	w2,w6,w2
	and	w2,w2,15
	cbz	w2,.t4
	ldrh	w2,[x1,x2,lsl 1]
	str	w2,[x5],4
	b	.n4
.t4:	mov	w2,0x80000000
	str	w2,[x5],4
.n4:	add	w3,w3,1
	subs	w4,w4,1
	b.ne	.p4
.nt:	cmp	w25,GBA_W
	b.lo	.tile
	cmp	w15,1
	b.ls	.f
	mov	x1,x21
	mov	w0,w15
	bl	mos_apply
.f:	epilogue

; w0 = bg, x1 = line
render_affine:
	prologue
	mov	x21,x1
	sub	w9,w0,2				; k
	add	x2,x19,A_IO+8
	ldrh	w4,[x2,x0,lsl 1]		; cnt
	ubfx	w24,w4,2,2
	lsl	w24,w24,14			; charBase
	ubfx	w26,w4,8,5
	lsl	w26,w26,11			; screenBase
	lsr	w0,w4,14
	mov	w10,128
	lsl	w10,w10,w0			; size
	lsr	w11,w10,3			; tiles
	ubfx	w12,w4,13,1			; wrap
	ubfx	w15,w4,6,1			; mosaique
	lsl	w0,w9,4
	add	x2,x19,A_IO+0x20
	ldrsh	w13,[x2,x0]			; pa
	add	x2,x2,4
	ldrsh	w14,[x2,x0]			; pc
	add	x2,x19,a_affX
	ldr	w5,[x2,x9,lsl 2]		; fx
	add	x2,x19,a_affY
	ldr	w6,[x2,x9,lsl 2]		; fy
	add	x22,x19,A_VRAM
	add	x23,x19,A_PAL
	sub	w7,w10,1			; size - 1
	mov	x8,0				; x
.x:	asr	w0,w5,8				; tx
	asr	w1,w6,8				; ty
	cbz	w12,.nw
	and	w0,w0,w7
	and	w1,w1,w7
	b	.in
.nw:	orr	w2,w0,w1
	tbnz	w2,31,.out
	cmp	w0,w10
	b.ge	.out
	cmp	w1,w10
	b.ge	.out
.in:	asr	w2,w1,3
	mul	w2,w2,w11
	add	w2,w2,w0,asr 3
	add	w2,w2,w26
	and	w2,w2,0xFFFF
	ldrb	w2,[x22,x2]			; tile
	lsl	w2,w2,6
	add	w2,w2,w24
	and	w3,w1,7
	add	w2,w2,w3,lsl 3
	and	w3,w0,7
	add	w2,w2,w3
	and	w2,w2,0xFFFF
	ldrb	w2,[x22,x2]			; idx
	cbz	w2,.out
	ldrh	w2,[x23,x2,lsl 1]
	str	w2,[x21,x8,lsl 2]
	b	.n
.out:	mov	w2,0x80000000
	str	w2,[x21,x8,lsl 2]
.n:	add	w5,w5,w13
	add	w6,w6,w14
	add	x8,x8,1
	cmp	x8,GBA_W
	b.lo	.x
	cbz	w15,.f
	io_h	w0,0x4C
	and	w0,w0,15
	add	w0,w0,1
	cmp	w0,1
	b.ls	.f
	mov	x1,x21
	bl	mos_apply
.f:	epilogue

; w0 = mode, x1 = line
render_bitmap:
	prologue
	mov	x21,x1
	mov	w9,w0				; mode
	ldrsh	w13,[x19,A_IO+0x20]
	ldrsh	w14,[x19,A_IO+0x24]
	ldr	w5,[x19,a_affX]
	ldr	w6,[x19,a_affY]
	io_b	w0,0
	mov	w12,0				; page
	tbz	w0,4,.a
	mov	w12,0xA000
.a:	mov	w10,240				; bw
	mov	w11,160				; bh
	cmp	w9,5
	b.ne	.b
	mov	w10,160
	mov	w11,128
.b:	add	x22,x19,A_VRAM
	add	x23,x19,A_PAL
	mov	x8,0
.x:	asr	w0,w5,8
	asr	w1,w6,8
	orr	w2,w0,w1
	tbnz	w2,31,.out
	cmp	w0,w10
	b.ge	.out
	cmp	w1,w11
	b.ge	.out
	cmp	w9,3
	b.ne	.m5
	mov	w2,240
	mul	w2,w2,w1
	add	w2,w2,w0
	ldrh	w2,[x22,x2,lsl 1]
	and	w2,w2,0x7FFF
	b	.p
.m5:	cmp	w9,5
	b.ne	.m4
	mov	w2,160
	mul	w2,w2,w1
	add	w2,w2,w0
	lsl	w2,w2,1
	add	w2,w2,w12
	ldrh	w2,[x22,x2]
	and	w2,w2,0x7FFF
	b	.p
.m4:	mov	w2,240
	mul	w2,w2,w1
	add	w2,w2,w0
	add	w2,w2,w12
	ldrb	w2,[x22,x2]
	cbz	w2,.out
	ldrh	w2,[x23,x2,lsl 1]
.p:	str	w2,[x21,x8,lsl 2]
	b	.n
.out:	mov	w2,0x80000000
	str	w2,[x21,x8,lsl 2]
.n:	add	w5,w5,w13
	add	w6,w6,w14
	add	x8,x8,1
	cmp	x8,GBA_W
	b.lo	.x
	io_h	w0,0x0C
	tbz	w0,6,.f
	io_h	w0,0x4C
	and	w0,w0,15
	add	w0,w0,1
	cmp	w0,1
	b.ls	.f
	mov	x1,x21
	bl	mos_apply
.f:	epilogue

obj_w:	db	8,16,32,64, 16,32,32,64, 8,8,16,32, 0,0,0,0
obj_h:	db	8,16,32,64, 8,8,16,32, 16,32,32,64, 0,0,0,0

; les sprites : variables sur la pile
RO_i		= 0
RO_a0		= 4
RO_bh		= 8
RO_map1D	= 12
RO_bitmap	= 16
RO_pa		= 20
RO_pb		= 24
RO_pc		= 28
RO_pd		= 32
RO_dy		= 36
RO_ly		= 40
RO_SIZE		= 48

render_objs:
	prologue
	sub	sp,sp,RO_SIZE
	io_h	w0,0
	ubfx	w1,w0,6,1
	str	w1,[sp,RO_map1D]
	and	w1,w0,7
	cmp	w1,3
	cset	w1,hs
	str	w1,[sp,RO_bitmap]
	ldr	w0,[x19,a_vcount]
	str	w0,[sp,RO_ly]
	add	x27,x19,A_VRAM
	add	x28,x19,A_PAL
	str	wzr,[sp,RO_i]
.obj:	ldr	w0,[sp,RO_i]
	add	x1,x19,A_OAM
	add	x1,x1,x0,lsl 3
	ldrh	w2,[x1]				; a0
	ldrh	w9,[x1,2]			; a1
	ldrh	w4,[x1,4]			; a2
	str	w2,[sp,RO_a0]
	ubfx	w23,w2,8,1			; affine
	cbnz	w23,.a1
	tbnz	w2,9,.next
.a1:	ubfx	w26,w2,10,2			; mode
	cmp	w26,3
	b.eq	.next
	lsr	w0,w2,14			; shape
	cmp	w0,3
	b.eq	.next
	lsl	w0,w0,2
	lsr	w1,w9,14
	add	w0,w0,w1
	adr	x1,obj_w
	ldrb	w13,[x1,x0]			; w
	adr	x1,obj_h
	ldrb	w14,[x1,x0]			; h
	and	w5,w2,0xFF			; y
	cmp	w5,160
	b.lt	.y1
	sub	w5,w5,256
.y1:	and	w12,w9,0x1FF			; x
	cmp	w12,240
	b.lt	.x1
	sub	w12,w12,512
.x1:	mov	w10,w13				; bw
	mov	w6,w14				; bh
	cbz	w23,.s1
	tbz	w2,9,.s1
	lsl	w10,w10,1
	lsl	w6,w6,1
.s1:	str	w6,[sp,RO_bh]
	ldr	w7,[sp,RO_ly]
	sub	w11,w7,w5			; row
	tbnz	w11,31,.wrap
	cmp	w11,w6
	b.lt	.ok
.wrap:	add	w8,w5,w6
	cmp	w8,256
	b.le	.next
	and	w8,w8,255
	cmp	w7,w8
	b.ge	.next
	add	w11,w7,256
	sub	w11,w11,w5
.ok:	tbnz	w11,31,.next
	cmp	w11,w6
	b.ge	.next
	and	w15,w4,0x3FF			; tile
	ldr	w0,[sp,RO_bitmap]
	cbz	w0,.t1
	cmp	w15,512
	b.lo	.next
.t1:	ubfx	w24,w4,10,2			; pr
	lsr	w25,w4,12			; palb
	tbz	w2,12,.nm
	io_h	w0,0x4C
	ubfx	w0,w0,12,4
	add	w0,w0,1
	udiv	w1,w11,w0
	msub	w1,w1,w0,w11
	sub	w11,w11,w1
.nm:	mov	w0,256
	str	w0,[sp,RO_pa]
	str	wzr,[sp,RO_pb]
	str	wzr,[sp,RO_pc]
	str	w0,[sp,RO_pd]
	cbz	w23,.np
	ubfx	w0,w9,9,5
	lsl	w0,w0,5
	add	x1,x19,A_OAM
	add	x1,x1,x0
	ldrsh	w0,[x1,6]
	str	w0,[sp,RO_pa]
	ldrsh	w0,[x1,14]
	str	w0,[sp,RO_pb]
	ldrsh	w0,[x1,22]
	str	w0,[sp,RO_pc]
	ldrsh	w0,[x1,30]
	str	w0,[sp,RO_pd]
.np:	mov	w21,32				; rowTiles
	ldr	w0,[sp,RO_map1D]
	cbz	w0,.rt
	lsr	w21,w13,3
	tbz	w2,13,.rt
	lsr	w21,w13,2
.rt:	ldr	w0,[sp,RO_bh]
	lsr	w0,w0,1
	sub	w0,w11,w0
	str	w0,[sp,RO_dy]
	mov	w22,0				; sx
.px:	add	w8,w12,w22			; px
	cmp	w8,GBA_W
	b.hs	.pn
	cbz	w23,.nor
	lsr	w0,w10,1
	sub	w0,w22,w0			; dx
	ldr	w5,[sp,RO_dy]
	ldr	w1,[sp,RO_pa]
	mul	w1,w1,w0
	ldr	w2,[sp,RO_pb]
	mul	w2,w2,w5
	add	w1,w1,w2
	asr	w1,w1,8
	add	w1,w1,w13,lsr 1			; tx
	ldr	w2,[sp,RO_pc]
	mul	w2,w2,w0
	ldr	w3,[sp,RO_pd]
	mul	w3,w3,w5
	add	w2,w2,w3
	asr	w2,w2,8
	add	w2,w2,w14,lsr 1			; ty
	orr	w0,w1,w2
	tbnz	w0,31,.pn
	cmp	w1,w13
	b.ge	.pn
	cmp	w2,w14
	b.ge	.pn
	b	.col
.nor:	mov	w1,w22
	tbz	w9,12,.n1
	sub	w1,w13,1
	sub	w1,w1,w22
.n1:	mov	w2,w11
	tbz	w9,13,.col
	sub	w2,w14,1
	sub	w2,w2,w11
.col:	asr	w3,w2,3
	mul	w3,w3,w21
	add	w3,w3,w15			; t
	ldr	w0,[sp,RO_a0]
	tbz	w0,13,.b4
	add	w3,w3,w1,asr 3
	add	w3,w3,w1,asr 3
	and	w3,w3,0x3FF
	lsl	w3,w3,5
	and	w4,w2,7
	add	w3,w3,w4,lsl 3
	and	w4,w1,7
	add	w3,w3,w4
	add	w3,w3,0x10000
	ldrb	w3,[x27,x3]			; idx
	cbz	w3,.pn
	add	w3,w3,0x100
	b	.got
.b4:	add	w3,w3,w1,asr 3
	and	w3,w3,0x3FF
	lsl	w3,w3,5
	and	w4,w2,7
	add	w3,w3,w4,lsl 2
	ubfx	w4,w1,1,2
	add	w3,w3,w4
	add	w3,w3,0x10000
	ldrb	w3,[x27,x3]
	and	w4,w1,1
	lsl	w4,w4,2
	lsr	w3,w3,w4
	ands	w3,w3,15
	b.eq	.pn
	add	w3,w3,w25,lsl 4
	add	w3,w3,0x100
.got:	ldrh	w3,[x28,x3,lsl 1]		; color
	cmp	w26,2
	b.ne	.nw
	adm	x0,A_OBJW
	mov	w1,1
	strb	w1,[x0,x8]
	b	.pn
.nw:	add	x0,x19,A_OBJL
	adm	x1,A_OBJP
	ldr	w2,[x0,x8,lsl 2]
	tbnz	w2,31,.pp
	ldrb	w2,[x1,x8]
	cmp	w24,w2
	b.ge	.pn
.pp:	and	w3,w3,0x7FFF
	cmp	w26,1
	b.ne	.ps
	orr	w3,w3,0x40000000
.ps:	str	w3,[x0,x8,lsl 2]
	strb	w24,[x1,x8]
.pn:	add	w22,w22,1
	cmp	w22,w10
	b.lo	.px
.next:	ldr	w0,[sp,RO_i]
	add	w0,w0,1
	str	w0,[sp,RO_i]
	cmp	w0,128
	b.lo	.obj
	add	sp,sp,RO_SIZE
	epilogue

; w0 = c1, w1 = c2, w21 = eva, w22 = evb -> w0 ; perd w1-w7
blend:
	and	w2,w0,31
	mul	w2,w2,w21
	and	w3,w1,31
	mul	w3,w3,w22
	add	w2,w2,w3
	lsr	w2,w2,4				; r
	ubfx	w3,w0,5,5
	mul	w3,w3,w21
	ubfx	w4,w1,5,5
	mul	w4,w4,w22
	add	w3,w3,w4
	lsr	w3,w3,4				; g
	ubfx	w4,w0,10,5
	mul	w4,w4,w21
	ubfx	w5,w1,10,5
	mul	w5,w5,w22
	add	w4,w4,w5
	lsr	w4,w4,4				; b
	mov	w5,31
	cmp	w2,31
	csel	w2,w5,w2,hi
	cmp	w3,31
	csel	w3,w5,w3,hi
	cmp	w4,31
	csel	w4,w5,w4,hi
	orr	w0,w2,w3,lsl 5
	orr	w0,w0,w4,lsl 10
	ret

; pile de la ligne
RL_order	= 0			; 4 octets
RL_prio		= 4			; 4 octets
RL_on		= 8
RL_no		= 12
RL_mode		= 16
RL_out		= 24
RL_SIZE		= 32

render_line:
	prologue
	save_all
	sub	sp,sp,RL_SIZE
	ldr	w0,[x19,a_vcount]
	mov	w1,GBA_W*4
	mul	w0,w0,w1
	add	x9,x19,A_FB
	add	x9,x9,x0			; out
	str	x9,[sp,RL_out]
	io_h	w10,0				; disp
	tbz	w10,7,.on
	mov	w0,0xFFFFFF
	mov	w1,GBA_W
.fb:	str	w0,[x9],4
	subs	w1,w1,1
	b.ne	.fb
	b	.end
.on:	and	w11,w10,7			; mode
	str	w11,[sp,RL_mode]
	mov	w12,0				; bgOn
	mov	w13,0				; b
.bo:	mov	w0,0x100
	lsl	w0,w0,w13
	tst	w10,w0
	b.eq	.bn
	cbz	w11,.be
	cmp	w11,1
	b.ne	.b2
	cmp	w13,3
	b.lo	.be
	b	.bn
.b2:	cmp	w11,2
	b.ne	.b3
	cmp	w13,2
	b.hs	.be
	b	.bn
.b3:	cmp	w13,2
	b.ne	.bn
.be:	mov	w0,1
	lsl	w0,w0,w13
	orr	w12,w12,w0
.bn:	add	w13,w13,1
	cmp	w13,4
	b.lo	.bo
	str	w12,[sp,RL_on]
	mov	w13,0
.br:	ldr	w12,[sp,RL_on]
	lsr	w0,w12,w13
	tbz	w0,0,.rn
	ldr	w11,[sp,RL_mode]
	add	x1,x19,A_BGL
	mov	w0,GBA_W*4
	mul	w0,w0,w13
	add	x1,x1,x0
	mov	w0,w13
	str	x13,[sp,-16]!
	cbz	w11,.rt
	cmp	w11,1
	b.ne	.r2
	cmp	w13,2
	b.hs	.ra
.rt:	bl	render_text
	b	.rd
.r2:	cmp	w11,2
	b.ls	.ra
	mov	w0,w11
	bl	render_bitmap
	b	.rd
.ra:	bl	render_affine
.rd:	ldr	x13,[sp],16
.rn:	add	w13,w13,1
	cmp	w13,4
	b.lo	.br
	add	x0,x19,A_OBJL
	adm	x1,A_OBJP
	adm	x2,A_OBJW
	mov	w3,0x80000000
	mov	w4,4
	mov	x5,0
.ob:	str	w3,[x0,x5,lsl 2]
	strb	w4,[x1,x5]
	strb	wzr,[x2,x5]
	add	x5,x5,1
	cmp	x5,GBA_W
	b.lo	.ob
	io_h	w10,0
	tbz	w10,12,.w
	bl	render_objs
.w:	io_h	w10,0
	; les fenetres
	adm	x9,A_WINL
	ubfx	w11,w10,13,1			; w0
	ubfx	w12,w10,14,1			; w1
	ubfx	w13,w10,15,1
	ubfx	w0,w10,12,1
	and	w13,w13,w0			; wo
	orr	w0,w11,w12
	orr	w0,w0,w13
	cbnz	w0,.ws
	mov	w0,0x3F
	mov	x1,0
.w0a:	strb	w0,[x9,x1]
	add	x1,x1,1
	cmp	x1,GBA_W
	b.lo	.w0a
	b	.bgo
.ws:	io_b	w0,0x4A
	and	w0,w0,0x3F			; outside
	mov	x1,0
.w1a:	strb	w0,[x9,x1]
	add	x1,x1,1
	cmp	x1,GBA_W
	b.lo	.w1a
	cbz	w13,.wk
	io_b	w0,0x4B
	and	w0,w0,0x3F			; objIn
	adm	x2,A_OBJW
	mov	x1,0
.w2a:	ldrb	w3,[x2,x1]
	cbz	w3,.w2b
	strb	w0,[x9,x1]
.w2b:	add	x1,x1,1
	cmp	x1,GBA_W
	b.lo	.w2a
.wk:	mov	w14,1				; k
.wl:	cmp	w14,0
	csel	w0,w12,w11,ne
	cbz	w0,.wn
	lsl	w0,w14,1
	add	x1,x19,A_IO
	add	x1,x1,x0
	ldrb	w2,[x1,0x41]			; x1
	ldrb	w3,[x1,0x40]			; x2
	ldrb	w4,[x1,0x45]			; y1
	ldrb	w5,[x1,0x44]			; y2
	mov	w6,GBA_W
	cmp	w3,w6
	csel	w7,w6,w3,hi
	cmp	w2,w3
	csel	w3,w6,w7,hi
	mov	w6,228
	cmp	w5,w6
	csel	w7,w6,w5,hi
	cmp	w4,w5
	csel	w5,w6,w7,hi
	ldr	w6,[x19,a_vcount]
	cmp	w6,w4
	b.lt	.wn
	cmp	w6,w5
	b.ge	.wn
	add	x1,x19,A_IO+0x48
	ldrb	w1,[x1,x14]
	and	w1,w1,0x3F
.wf:	cmp	w2,w3
	b.ge	.wn
	strb	w1,[x9,x2]
	add	w2,w2,1
	b	.wf
.wn:	subs	w14,w14,1
	b.pl	.wl
.bgo:	; les plans par priorite (puis numero)
	add	x1,x19,A_IO+8
	add	x2,sp,RL_prio
	mov	x3,0
.pr:	ldrh	w0,[x1,x3,lsl 1]
	and	w0,w0,3
	strb	w0,[x2,x3]
	add	x3,x3,1
	cmp	x3,4
	b.lo	.pr
	ldr	w4,[sp,RL_on]
	mov	w5,0				; no
	mov	w6,0				; p
.op:	mov	x3,0
.oq:	lsr	w0,w4,w3
	tbz	w0,0,.or
	ldrb	w0,[x2,x3]
	cmp	w0,w6
	b.ne	.or
	add	x0,sp,RL_order
	strb	w3,[x0,x5]
	add	w5,w5,1
.or:	add	x3,x3,1
	cmp	x3,4
	b.lo	.oq
	add	w6,w6,1
	cmp	w6,4
	b.lo	.op
	str	w5,[sp,RL_no]
	io_h	w25,0x50			; bld
	mov	w3,16
	io_b	w21,0x52
	and	w21,w21,31
	cmp	w21,w3
	csel	w21,w3,w21,hi			; eva
	io_b	w22,0x53
	and	w22,w22,31
	cmp	w22,w3
	csel	w22,w3,w22,hi			; evb
	io_b	w23,0x54
	and	w23,w23,31
	cmp	w23,w3
	csel	w23,w3,w23,hi			; evy
	add	x0,x19,A_PAL
	ldrh	w24,[x0]
	and	w24,w24,0x7FFF			; backdrop
	ldr	x26,[sp,RL_out]
	adm	x27,A_RGB
	adm	x28,A_WINL
	add	x15,x19,A_BGL
	mov	x9,0				; x
	tst	w25,0xC0
	b.ne	.fx
.nl:	ldrb	w10,[x28,x9]			; m
	mov	w11,-1				; b1
	ldr	w4,[sp,RL_no]
	mov	x5,0
.nk:	cmp	w5,w4
	b.ge	.nf
	add	x0,sp,RL_order
	ldrb	w6,[x0,x5]			; b
	lsr	w0,w10,w6
	tbz	w0,0,.nc
	mov	w0,GBA_W*4
	mul	w0,w0,w6
	add	x1,x15,x0
	ldr	w1,[x1,x9,lsl 2]
	tbnz	w1,31,.nc
	mov	w11,w6
	mov	w12,w1				; bgl [b1][x]
	b	.nf
.nc:	add	x5,x5,1
	b	.nk
.nf:	add	x0,x19,A_OBJL
	ldr	w13,[x0,x9,lsl 2]		; ol
	tbnz	w13,31,.nb
	tbz	w10,4,.nb
	tbnz	w11,31,.no
	adm	x0,A_OBJP
	ldrb	w0,[x0,x9]
	add	x1,sp,RL_prio
	ldrb	w1,[x1,x11]
	cmp	w0,w1
	b.gt	.nb
.no:	and	w0,w13,0x7FFF			; c1
	tbz	w13,30,.np
	mov	w1,5
	tbnz	w11,31,.nt
	mov	w1,w11
.nt:	add	w1,w1,8
	lsr	w1,w25,w1
	tbz	w1,0,.np
	mov	w1,w24
	tbnz	w11,31,.nu
	mov	w1,w12
.nu:	bl	blend
.np:	and	w0,w0,0x7FFF
	b	.nv
.nb:	mov	w0,w24
	tbnz	w11,31,.nd
	mov	w0,w12
.nd:	and	w0,w0,0x7FFF
.nv:	ldr	w0,[x27,x0,lsl 2]
	str	w0,[x26,x9,lsl 2]
	add	x9,x9,1
	cmp	x9,GBA_W
	b.lo	.nl
	b	.end
.fx:	ldrb	w10,[x28,x9]			; m
	mov	w11,-1				; b1
	mov	w12,-1				; b2
	ldr	w4,[sp,RL_no]
	mov	x5,0
.fk:	cmp	w5,w4
	b.ge	.ff
	add	x0,sp,RL_order
	ldrb	w6,[x0,x5]
	lsr	w0,w10,w6
	tbz	w0,0,.fc
	mov	w0,GBA_W*4
	mul	w0,w0,w6
	add	x1,x15,x0
	ldr	w1,[x1,x9,lsl 2]
	tbnz	w1,31,.fc
	tbz	w11,31,.fb2
	mov	w11,w6
	mov	w13,w1				; bgl [b1][x]
	b	.fc
.fb2:	mov	w12,w6
	mov	w14,w1				; bgl [b2][x]
	b	.ff
.fc:	add	x5,x5,1
	b	.fk
.ff:	add	x0,x19,A_OBJL
	ldr	w4,[x0,x9,lsl 2]		; objl [x]
	mov	w5,0				; objHere
	tbnz	w4,31,.fh
	ubfx	w5,w10,4,1
.fh:	adm	x0,A_OBJP
	ldrb	w6,[x0,x9]			; op
	mov	w0,w24				; c1
	mov	w1,w24				; c2
	mov	w2,5				; l1
	mov	w3,5				; l2
	mov	w7,0				; semi
	cbz	w5,.fl
	tbnz	w11,31,.fo
	add	x8,sp,RL_prio
	ldrb	w8,[x8,x11]
	cmp	w6,w8
	b.gt	.fl
.fo:	and	w0,w4,0x7FFF
	mov	w2,4
	ubfx	w7,w4,30,1
	tbnz	w11,31,.fm
	mov	w1,w13
	mov	w3,w11
	b	.fm
.fl:	tbnz	w11,31,.f1
	mov	w0,w13
	mov	w2,w11
.f1:	cbz	w5,.f2
	tbnz	w12,31,.f3
	add	x8,sp,RL_prio
	ldrb	w8,[x8,x12]
	cmp	w6,w8
	b.gt	.f2
.f3:	and	w1,w4,0x7FFF
	mov	w3,4
	b	.fm
.f2:	tbnz	w12,31,.fm
	mov	w1,w14
	mov	w3,w12
.fm:	lsr	w4,w25,w2
	and	w4,w4,1				; top1
	add	w5,w3,8
	lsr	w5,w25,w5
	and	w5,w5,1				; bot2
	mov	w6,1				; e
	cbz	w7,.e0
	cbnz	w5,.e1
.e0:	mov	w6,0
	tbz	w10,5,.e1
	cbz	w4,.e1
	ubfx	w6,w25,6,2
.e1:	cmp	w6,1
	b.ne	.e2
	cbz	w5,.e2
	stp	x9,x10,[sp,-16]!
	bl	blend
	ldp	x9,x10,[sp],16
	b	.fw
.e2:	cmp	w6,2
	b.ne	.e3
	and	w1,w0,31
	ubfx	w2,w0,5,5
	ubfx	w3,w0,10,5
	mov	w4,31
	sub	w5,w4,w1
	mul	w5,w5,w23
	add	w1,w1,w5,lsr 4
	sub	w5,w4,w2
	mul	w5,w5,w23
	add	w2,w2,w5,lsr 4
	sub	w5,w4,w3
	mul	w5,w5,w23
	add	w3,w3,w5,lsr 4
	b	.e4
.e3:	cmp	w6,3
	b.ne	.fw
	and	w1,w0,31
	ubfx	w2,w0,5,5
	ubfx	w3,w0,10,5
	mul	w5,w1,w23
	sub	w1,w1,w5,lsr 4
	mul	w5,w2,w23
	sub	w2,w2,w5,lsr 4
	mul	w5,w3,w23
	sub	w3,w3,w5,lsr 4
.e4:	orr	w0,w1,w2,lsl 5
	orr	w0,w0,w3,lsl 10
.fw:	and	w0,w0,0x7FFF
	ldr	w0,[x27,x0,lsl 2]
	str	w0,[x26,x9,lsl 2]
	add	x9,x9,1
	cmp	x9,GBA_W
	b.lo	.fx
.end:	add	sp,sp,RL_SIZE
	rest_all
	epilogue
