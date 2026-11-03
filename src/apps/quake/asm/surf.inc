; surf.inc - la lumiere sur la texture, dans le cache des surfaces : R_DrawSurfaceBlock8_mip0..3 (r_surf.c)

; un bloc de n points de cote (n = 1 shl sh)
;   x0 source, x1 destination, x2 r_lightptr, w3 r_lightwidth, x4 colormap, w5 r_numvblocks, x6 r_sourcemax,
;   w7 sourcetstep, w8 surfrowbytes, w9 r_stepback
macro surf_block name, sh, n {
public name
name:
	stp	x19,x20,[sp,-32]!
	stp	x21,x22,[sp,16]
	la	x10,pbasesource
	ldr	x0,[x10]
	la	x10,prowdestbase
	ldr	x1,[x10]
	la	x21,r_lightptr
	ldr	x2,[x21]
	lw	w3,x10,r_lightwidth
	la	x10,vid
	ldr	x4,[x10,8]			; vid.colormap
	lw	w5,x10,r_numvblocks
	la	x6,r_sourcemax
	ldr	x6,[x6]
	lw	w7,x10,sourcetstep
	lw	w8,x10,surfrowbytes
	lw	w9,x10,r_stepback
	cmp	w5,0
	b.le	.out
.v:	ldp	w10,w11,[x2]			; lightleft, lightright
	add	x2,x2,w3,sxtw 2			; (r_lightwidth : entier)
	ldp	w12,w13,[x2]
	sub	w12,w12,w10
	lsr	w12,w12,sh			; (non signe dans le C : r_lightptr)
	sub	w13,w13,w11
	lsr	w13,w13,sh
	mov	w14,n
.i:	sub	w15,w10,w11
	asr	w15,w15,sh			; lightstep
	mov	w16,w11				; light
	mov	x17,n-1
.b:	ldrb	w19,[x0,x17]
	and	w20,w16,0xFF00
	add	w19,w19,w20
	ldrb	w19,[x4,x19]
	strb	w19,[x1,x17]
	add	w16,w16,w15
	subs	x17,x17,1
	b.ge	.b
	add	x0,x0,w7,sxtw
	add	w11,w11,w13
	add	w10,w10,w12
	add	x1,x1,w8,sxtw
	subs	w14,w14,1
	b.ne	.i
	cmp	x0,x6
	b.lo	.nx
	sub	x0,x0,w9,sxtw
.nx:	subs	w5,w5,1
	b.ne	.v
.out:	str	x2,[x21]
	ldp	x21,x22,[sp,16]
	ldp	x19,x20,[sp],32
	ret
}

surf_block R_DrawSurfaceBlock8_mip0, 4, 16
surf_block R_DrawSurfaceBlock8_mip1, 3, 8
surf_block R_DrawSurfaceBlock8_mip2, 2, 4
surf_block R_DrawSurfaceBlock8_mip3, 1, 2
