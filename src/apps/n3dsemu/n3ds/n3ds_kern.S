//
// n3ds/n3ds_kern.S -- ce qui compte dans le coeur 3DS, en assembleur : les boucles de pixels. Le reste du coeur est du
// C++ : systeme, services et commandes passent leur temps a suivre des structures (docs/EMULATEURS.md).
//
	.text
	.arch armv8-a

// void n3ds_blit (unsigned *dst, long pitch, const unsigned *src, int w, int h) : un ecran dans l'image d'EmuKit
	.global	n3ds_blit
	.type	n3ds_blit, %function
n3ds_blit:
	sxtw	x3, w3
1:	cbz	w4, 9f
	mov	x5, x0
	mov	x6, x3
2:	subs	x6, x6, #16
	b.lo	3f
	ldp	q0, q1, [x2], #32
	ldp	q2, q3, [x2], #32
	stp	q0, q1, [x5], #32
	stp	q2, q3, [x5], #32
	b	2b
3:	adds	x6, x6, #16
	b.eq	5f
4:	ldr	w7, [x2], #4
	str	w7, [x5], #4
	subs	x6, x6, #1
	b.ne	4b
5:	add	x0, x0, x1
	sub	w4, w4, #1
	b	1b
9:	ret
	.size	n3ds_blit, . - n3ds_blit

// void n3ds_xfer_rgb8 (u8 *dst, const u32 *src, long n) : des pixels 0xAARRGGBB en B G R, l'ecran du jeu
	.global	n3ds_xfer_rgb8
	.type	n3ds_xfer_rgb8, %function
n3ds_xfer_rgb8:
1:	subs	x2, x2, #16
	b.lo	2f
	ld4	{v0.16b, v1.16b, v2.16b, v3.16b}, [x1], #64
	st3	{v0.16b, v1.16b, v2.16b}, [x0], #48
	b	1b
2:	adds	x2, x2, #16
	b.eq	9f
3:	ldr	w3, [x1], #4
	strb	w3, [x0], #1
	lsr	w3, w3, #8
	strb	w3, [x0], #1
	lsr	w3, w3, #8
	strb	w3, [x0], #1
	subs	x2, x2, #1
	b.ne	3b
9:	ret
	.size	n3ds_xfer_rgb8, . - n3ds_xfer_rgb8

// void n3ds_xfer_rgba8 (u8 *dst, const u32 *src, long n) : ... en A B G R
	.global	n3ds_xfer_rgba8
	.type	n3ds_xfer_rgba8, %function
n3ds_xfer_rgba8:
1:	subs	x2, x2, #4
	b.lo	2f
	ldr	q0, [x1], #16
	shl	v1.4s, v0.4s, #8
	ushr	v0.4s, v0.4s, #24
	orr	v0.16b, v0.16b, v1.16b
	str	q0, [x0], #16
	b	1b
2:	adds	x2, x2, #4
	b.eq	9f
3:	ldr	w3, [x1], #4
	ror	w3, w3, #24
	str	w3, [x0], #4
	subs	x2, x2, #1
	b.ne	3b
9:	ret
	.size	n3ds_xfer_rgba8, . - n3ds_xfer_rgba8

// void n3ds_tile (u32 *dst, long pitch, const u8 *src, int format) : une tuile 8 x 8, ordre Z, en octets r g b a.
// Les formats 0 a 11 de n3ds_pica.cpp (fetch), au pixel pres ; ETC1 : n3ds_etc1.
	.section .rodata
	.align	4
z_xy:	// n -> x | y << 3
	.byte	0, 1, 8, 9, 2, 3, 10, 11, 16, 17, 24, 25, 18, 19, 26, 27
	.byte	4, 5, 12, 13, 6, 7, 14, 15, 20, 21, 28, 29, 22, 23, 30, 31
	.byte	32, 33, 40, 41, 34, 35, 42, 43, 48, 49, 56, 57, 50, 51, 58, 59
	.byte	36, 37, 44, 45, 38, 39, 46, 47, 52, 53, 60, 61, 54, 55, 62, 63
t5:	// i * 255 / 31
	.byte	0, 8, 16, 24, 32, 41, 49, 57, 65, 74, 82, 90, 98, 106, 115, 123
	.byte	131, 139, 148, 156, 164, 172, 180, 189, 197, 205, 213, 222, 230, 238, 246, 255
t6:	// i * 255 / 63
	.byte	0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60
	.byte	64, 68, 72, 76, 80, 85, 89, 93, 97, 101, 105, 109, 113, 117, 121, 125
	.byte	129, 133, 137, 141, 145, 149, 153, 157, 161, 165, 170, 174, 178, 182, 186, 190
	.byte	194, 198, 202, 206, 210, 214, 218, 222, 226, 230, 234, 238, 242, 246, 250, 255
	.text
	.global	n3ds_tile
	.type	n3ds_tile, %function
n3ds_tile:
	adrp	x4, z_xy
	add	x4, x4, :lo12:z_xy
	adrp	x10, t5
	add	x10, x10, :lo12:t5
	adrp	x11, t6
	add	x11, x11, :lo12:t6
	mov	x5, #0
	adr	x12, 20f
	ldrh	w13, [x12, w3, uxtw #1]
	add	x12, x12, x13
1:	ldrb	w6, [x4, x5]
	and	w7, w6, #7
	lsr	w6, w6, #3
	madd	x8, x6, x1, x0
	add	x8, x8, x7, lsl #2
	br	x12
2:	str	w9, [x8]
	add	x5, x5, #1
	cmp	x5, #64
	b.ne	1b
	ret
	.p2align 1
20:	.hword	f0-20b, f1-20b, f2-20b, f3-20b, f4-20b, f5-20b, f6-20b, f7-20b, f8-20b, f9-20b, f10-20b, f11-20b
	.p2align 2
f0:	ldr	w9, [x2, x5, lsl #2]
	rev	w9, w9
	b	2b
f1:	add	x6, x5, x5, lsl #1
	add	x6, x2, x6
	ldrb	w9, [x6, #2]
	ldrb	w7, [x6, #1]
	ldrb	w6, [x6]
	orr	w9, w9, w7, lsl #8
	orr	w9, w9, w6, lsl #16
	orr	w9, w9, #0xFF000000
	b	2b
f2:	ldrh	w7, [x2, x5, lsl #1]
	ubfx	w6, w7, #11, #5
	ldrb	w9, [x10, x6]
	ubfx	w6, w7, #6, #5
	ldrb	w6, [x10, x6]
	orr	w9, w9, w6, lsl #8
	ubfx	w6, w7, #1, #5
	ldrb	w6, [x10, x6]
	orr	w9, w9, w6, lsl #16
	tbz	w7, #0, 2b
	orr	w9, w9, #0xFF000000
	b	2b
f3:	ldrh	w7, [x2, x5, lsl #1]
	ubfx	w6, w7, #11, #5
	ldrb	w9, [x10, x6]
	ubfx	w6, w7, #5, #6
	ldrb	w6, [x11, x6]
	orr	w9, w9, w6, lsl #8
	and	w6, w7, #31
	ldrb	w6, [x10, x6]
	orr	w9, w9, w6, lsl #16
	orr	w9, w9, #0xFF000000
	b	2b
f4:	ldrh	w7, [x2, x5, lsl #1]
	rev16	w7, w7				// a b g r en quartets -> ...
	and	w9, w7, #0xF0
	lsr	w9, w9, #4			// (r)
	and	w6, w7, #0x0F
	orr	w9, w9, w6, lsl #8		// g
	lsr	w7, w7, #8
	and	w6, w7, #0xF0
	lsr	w6, w6, #4
	orr	w9, w9, w6, lsl #16		// b
	and	w6, w7, #0x0F
	orr	w9, w9, w6, lsl #24		// a
	add	w9, w9, w9, lsl #4
	b	2b
f5:	add	x6, x2, x5, lsl #1
	ldrb	w9, [x6, #1]
	ldrb	w6, [x6]
	orr	w9, w9, w9, lsl #8
	orr	w9, w9, w9, lsl #16
	bfi	w9, w6, #24, #8
	b	2b
f6:	add	x6, x2, x5, lsl #1
	ldrb	w9, [x6, #1]
	ldrb	w6, [x6]
	orr	w9, w9, w6, lsl #8
	orr	w9, w9, #0xFF000000
	b	2b
f7:	ldrb	w9, [x2, x5]
	orr	w9, w9, w9, lsl #8
	orr	w9, w9, w9, lsl #16
	orr	w9, w9, #0xFF000000
	b	2b
f8:	ldrb	w9, [x2, x5]
	lsl	w9, w9, #24
	b	2b
f9:	ldrb	w6, [x2, x5]
	lsr	w9, w6, #4
	add	w9, w9, w9, lsl #4
	orr	w9, w9, w9, lsl #8
	orr	w9, w9, w9, lsl #16
	and	w6, w6, #15
	add	w6, w6, w6, lsl #4
	bfi	w9, w6, #24, #8
	b	2b
f10:	lsr	x6, x5, #1
	ldrb	w6, [x2, x6]
	tbz	x5, #0, 3f
	lsr	w6, w6, #4
3:	and	w9, w6, #15
	add	w9, w9, w9, lsl #4
	orr	w9, w9, w9, lsl #8
	orr	w9, w9, w9, lsl #16
	orr	w9, w9, #0xFF000000
	b	2b
f11:	lsr	x6, x5, #1
	ldrb	w6, [x2, x6]
	tbz	x5, #0, 3f
	lsr	w6, w6, #4
3:	and	w9, w6, #15
	add	w9, w9, w9, lsl #4
	lsl	w9, w9, #24
	b	2b
	.size	n3ds_tile, . - n3ds_tile

// void n3ds_etc1 (u32 *dst, long pitch, const u8 *src, int alpha) : un bloc ETC1 4 x 4 (alpha : ETC1A4, 8 octets
// d'alpha devant), en octets r g b a. Comme etc1Texel de n3ds_pica.cpp.
	.section .rodata
	.align	2
etc_step:
	.word	2, 8, 5, 17, 9, 29, 13, 42, 18, 60, 24, 80, 33, 106, 47, 183
	.text
	.global	n3ds_etc1
	.type	n3ds_etc1, %function
n3ds_etc1:
	stp	x19, x20, [sp, #-48]!
	stp	x21, x22, [sp, #16]
	stp	x23, x24, [sp, #32]
	mov	x4, #0				// les alphas
	cbz	w3, 1f
	ldr	x4, [x2], #8
1:	ldr	x5, [x2]			// c
	adrp	x24, etc_step
	add	x24, x24, :lo12:etc_step
	// les deux couleurs de base : w6, w7, w8 ; w9, w10, w11
	tbz	x5, #33, 2f
	ubfx	x6, x5, #59, #5
	ubfx	x7, x5, #51, #5
	ubfx	x8, x5, #43, #5
	sbfx	x9, x5, #56, #3
	sbfx	x10, x5, #48, #3
	sbfx	x11, x5, #40, #3
	add	w9, w6, w9
	add	w10, w7, w10
	add	w11, w8, w11
	and	w9, w9, #31
	and	w10, w10, #31
	and	w11, w11, #31
	lsl	w12, w6, #3
	orr	w6, w12, w6, lsr #2
	lsl	w12, w7, #3
	orr	w7, w12, w7, lsr #2
	lsl	w12, w8, #3
	orr	w8, w12, w8, lsr #2
	lsl	w12, w9, #3
	orr	w9, w12, w9, lsr #2
	lsl	w12, w10, #3
	orr	w10, w12, w10, lsr #2
	lsl	w12, w11, #3
	orr	w11, w12, w11, lsr #2
	b	3f
2:	ubfx	x6, x5, #60, #4
	ubfx	x9, x5, #56, #4
	ubfx	x7, x5, #52, #4
	ubfx	x10, x5, #48, #4
	ubfx	x8, x5, #44, #4
	ubfx	x11, x5, #40, #4
	add	w6, w6, w6, lsl #4
	add	w7, w7, w7, lsl #4
	add	w8, w8, w8, lsl #4
	add	w9, w9, w9, lsl #4
	add	w10, w10, w10, lsl #4
	add	w11, w11, w11, lsl #4
3:	ubfx	x12, x5, #37, #3		// les tables : le premier, le second
	ubfx	x13, x5, #34, #3
	ubfx	x14, x5, #32, #1		// flip
	mov	x15, #0				// x
4:	mov	x16, #0				// y
5:	add	x17, x16, x15, lsl #2		// texel = x * 4 + y
	cmp	x14, #0
	csel	x19, x15, x16, eq
	cmp	x19, #2
	b.hs	6f
	mov	w19, w6
	mov	w20, w7
	mov	w21, w8
	mov	x22, x12
	b	7f
6:	mov	w19, w9
	mov	w20, w10
	mov	w21, w11
	mov	x22, x13
7:	lsr	x23, x5, x17
	and	x23, x23, #1
	add	x22, x23, x22, lsl #1
	ldr	w22, [x24, x22, lsl #2]		// step
	add	x23, x17, #16
	lsr	x23, x5, x23
	tbz	x23, #0, 8f
	neg	w22, w22
8:	add	w19, w19, w22
	add	w20, w20, w22
	add	w21, w21, w22
	cmp	w19, #0
	csel	w19, wzr, w19, lt
	cmp	w19, #255
	mov	w23, #255
	csel	w19, w23, w19, gt
	cmp	w20, #0
	csel	w20, wzr, w20, lt
	cmp	w20, #255
	csel	w20, w23, w20, gt
	cmp	w21, #0
	csel	w21, wzr, w21, lt
	cmp	w21, #255
	csel	w21, w23, w21, gt
	orr	w19, w19, w20, lsl #8
	orr	w19, w19, w21, lsl #16
	cbz	w3, 9f
	lsl	x23, x17, #2
	lsr	x23, x4, x23
	and	w23, w23, #15
	add	w23, w23, w23, lsl #4
9:	orr	w19, w19, w23, lsl #24
	madd	x20, x16, x1, x0
	str	w19, [x20, x15, lsl #2]
	add	x16, x16, #1
	cmp	x16, #4
	b.ne	5b
	add	x15, x15, #1
	cmp	x15, #4
	b.ne	4b
	ldp	x23, x24, [sp, #32]
	ldp	x21, x22, [sp, #16]
	ldp	x19, x20, [sp], #48
	ret
	.size	n3ds_etc1, . - n3ds_etc1
