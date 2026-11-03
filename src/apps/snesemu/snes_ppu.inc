; ============================================================================
;  snes_ppu.inc - le PPU : les registres $2100-$213F (VRAM, OAM, CGRAM, le
;  defilement, la matrice de Mode 7, les fenetres, le calcul de couleur, le
;  verrou H/V) et une ligne entiere quand le faisceau l'atteint : les plans du
;  mode (2, 4, 8 bpp, tuiles 8x8 ou 16x16, offset-per-tile, mosaique, couleur
;  directe), Mode 7 (EXTBG), les sprites (32 par ligne), ecrans principal et
;  secondaire, fenetres, calcul de couleur, luminosite. Modes 5 / 6 : montres
;  a 256 de large.
; ============================================================================

ppu_reset:
	str	x30,[sp,-16]!
	mov	w0,0x80
	strb	w0,[x19,s_inidisp]
	strb	wzr,[x19,s_obsel]
	strb	wzr,[x19,s_bgmode]
	strb	wzr,[x19,s_mosaic]
	strb	wzr,[x19,s_vmain]
	strb	wzr,[x19,s_m7sel]
	add	x0,x19,s_bgsc
	strb	wzr,[x0]
	strb	wzr,[x0,1]
	strb	wzr,[x0,2]
	strb	wzr,[x0,3]
	add	x0,x19,s_bghofs
	mov	w1,8
.h:	strh	wzr,[x0],2
	subs	w1,w1,1
	b.ne	.h
	strb	wzr,[x19,s_bgnba]
	strb	wzr,[x19,s_bgnba+1]
	strb	wzr,[x19,s_bgLatch]
	strb	wzr,[x19,s_bgLatchH]
	add	x0,x19,s_m7a
	mov	w1,8
.m:	strh	wzr,[x0],2
	subs	w1,w1,1
	b.ne	.m
	strb	wzr,[x19,s_m7Latch]
	strh	wzr,[x19,s_vramAddr]
	strh	wzr,[x19,s_vramBuf]
	strh	wzr,[x19,s_oamAddr]
	strh	wzr,[x19,s_oamReload]
	strb	wzr,[x19,s_oamPrioRot]
	strb	wzr,[x19,s_oamLatch]
	strh	wzr,[x19,s_cgAddr]
	strb	wzr,[x19,s_cgLatch]
	strb	wzr,[x19,s_w12sel]
	strb	wzr,[x19,s_w34sel]
	strb	wzr,[x19,s_wobjsel]
	mov	w0,1
	strb	w0,[x19,s_wh]
	strb	w0,[x19,s_wh+2]
	strb	wzr,[x19,s_wh+1]
	strb	wzr,[x19,s_wh+3]
	strb	wzr,[x19,s_wbglog]
	strb	wzr,[x19,s_wobjlog]
	strb	wzr,[x19,s_tm]
	strb	wzr,[x19,s_ts]
	strb	wzr,[x19,s_tmw]
	strb	wzr,[x19,s_tsw]
	strb	wzr,[x19,s_cgwsel]
	strb	wzr,[x19,s_cgadsub]
	strb	wzr,[x19,s_setini]
	strh	wzr,[x19,s_fixedColor]
	strb	wzr,[x19,s_overscan]
	strh	wzr,[x19,s_hLatch]
	strh	wzr,[x19,s_vLatch]
	strb	wzr,[x19,s_hFlip]
	strb	wzr,[x19,s_vFlip]
	strb	wzr,[x19,s_counterLatched]
	strb	wzr,[x19,s_stat77]
	strb	wzr,[x19,s_ppu1Bus]
	strb	wzr,[x19,s_ppu2Bus]
	add	x0,x19,S_OAM
	mov	w1,544
.o:	strb	wzr,[x0],1
	subs	w1,w1,1
	b.ne	.o
	add	x0,x19,S_CGRAM
	mov	w1,512
.c:	strb	wzr,[x0],1
	subs	w1,w1,1
	b.ne	.c
	ldr	x30,[sp],16
	ret

; -> w0 = vramRemap (), perd w1
vram_remap:
	ldrh	w0,[x19,s_vramAddr]
	ldrb	w1,[x19,s_vmain]
	ubfx	w1,w1,2,2
	cbz	w1,.r
	cmp	w1,2
	b.eq	.m2
	b.hi	.m3
	and	w1,w0,0xFF00			; 1
	and	w2,w0,0x1F
	orr	w1,w1,w2,lsl 3
	ubfx	w2,w0,5,3
	orr	w0,w1,w2
	ret
.m2:	and	w1,w0,0xFE00
	and	w2,w0,0x3F
	orr	w1,w1,w2,lsl 3
	ubfx	w2,w0,6,3
	orr	w0,w1,w2
	ret
.m3:	and	w1,w0,0xFC00
	and	w2,w0,0x7F
	orr	w1,w1,w2,lsl 3
	ubfx	w2,w0,7,3
	orr	w0,w1,w2
.r:	ret

; w0 = 0 / 1 : l'octet ecrit ou lu est le haut
vram_step:
	ldrb	w1,[x19,s_vmain]
	lsr	w2,w1,7
	cmp	w0,w2
	b.ne	.r
	and	w1,w1,3
	adr	x2,.step
	ldrb	w1,[x2,x1]
	ldrh	w2,[x19,s_vramAddr]
	add	w2,w2,w1
	strh	w2,[x19,s_vramAddr]
.r:	ret
.step:	db	1,32,128,128

; vramBuf = vword (vramRemap ())
vram_fill:
	str	x30,[sp,-16]!
	bl	vram_remap
	and	w0,w0,0x7FFF
	add	x1,x19,S_VRAM
	ldrh	w0,[x1,x0,lsl 1]
	strh	w0,[x19,s_vramBuf]
	ldr	x30,[sp],16
	ret

latch_counters:
	ldr	x0,[x19,s_lineStart]
	sub	x0,x20,x0
	ubfx	x0,x0,2,16
	mov	w1,339
	cmp	w0,w1
	csel	w0,w1,w0,hi
	strh	w0,[x19,s_hLatch]
	ldr	w0,[x19,s_line]
	strh	w0,[x19,s_vLatch]
	mov	w0,1
	strb	w0,[x19,s_counterLatched]
	ret

; w0 = r (bas du numero) -> w0
ppu_read:
	sub	w1,w0,0x34
	cmp	w1,12
	b.hs	.mdr
	jtab	x1,.t
.t:	dw	.mp-.t,.mp-.t,.mp-.t,.r37-.t,.r38-.t,.r39-.t,.r3a-.t,.r3b-.t,.r3c-.t,.r3d-.t,.r3e-.t,.r3f-.t
.mdr:	ldrb	w0,[x19,s_mdr]
	ret
.mp:	ldrsh	w2,[x19,s_m7a]
	ldrsb	w3,[x19,s_m7b+1]
	mul	w2,w2,w3
	lsl	w1,w1,3
	asr	w0,w2,w1
	and	w0,w0,0xFF
	strb	w0,[x19,s_ppu1Bus]
	ret
.r37:	ldrb	w1,[x19,s_wrio]
	tbz	w1,7,.mdr
	str	x30,[sp,-16]!
	bl	latch_counters
	ldr	x30,[sp],16
	b	.mdr
.r38:	ldrh	w1,[x19,s_oamAddr]
	add	x2,x19,S_OAM
	cmp	w1,0x200
	b.lo	.o1
	and	w3,w1,0x1F
	orr	w3,w3,0x200
	ldrb	w0,[x2,x3]
	b	.o2
.o1:	ldrb	w0,[x2,x1]
.o2:	add	w1,w1,1
	and	w1,w1,0x3FF
	strh	w1,[x19,s_oamAddr]
	strb	w0,[x19,s_ppu1Bus]
	ret
.r39:	stp	x9,x30,[sp,-16]!
	ldrb	w9,[x19,s_vramBuf]
	ldrb	w1,[x19,s_vmain]
	tbnz	w1,7,.v
	bl	vram_fill
	mov	w0,0
	bl	vram_step
	b	.v
.r3a:	stp	x9,x30,[sp,-16]!
	ldrb	w9,[x19,s_vramBuf+1]
	ldrb	w1,[x19,s_vmain]
	tbz	w1,7,.v
	bl	vram_fill
	mov	w0,1
	bl	vram_step
.v:	strb	w9,[x19,s_ppu1Bus]
	mov	w0,w9
	ldp	x9,x30,[sp],16
	ret
.r3b:	ldrh	w1,[x19,s_cgAddr]
	add	x2,x19,S_CGRAM
	ldrb	w0,[x2,x1]
	tbz	w1,0,.c1
	ldrb	w2,[x19,s_ppu2Bus]
	bfxil	w2,w0,0,7
	mov	w0,w2
.c1:	add	w1,w1,1
	and	w1,w1,0x1FF
	strh	w1,[x19,s_cgAddr]
	strb	w0,[x19,s_ppu2Bus]
	ret
.r3c:	ldrh	w1,[x19,s_hLatch]
	ldrb	w2,[x19,s_hFlip]
	eor	w3,w2,1
	strb	w3,[x19,s_hFlip]
	b	.hv
.r3d:	ldrh	w1,[x19,s_vLatch]
	ldrb	w2,[x19,s_vFlip]
	eor	w3,w2,1
	strb	w3,[x19,s_vFlip]
.hv:	cbnz	w2,.hv1
	and	w0,w1,0xFF
	b	.hv2
.hv1:	ubfx	w0,w1,8,1
	ldrb	w1,[x19,s_ppu2Bus]
	and	w1,w1,0xFE
	orr	w0,w0,w1
.hv2:	strb	w0,[x19,s_ppu2Bus]
	ret
.r3e:	ldrb	w0,[x19,s_stat77]
	orr	w0,w0,1
	ldrb	w1,[x19,s_ppu1Bus]
	and	w1,w1,0x10
	orr	w0,w0,w1
	strb	w0,[x19,s_ppu1Bus]
	ret
.r3f:	ldrb	w0,[x19,s_counterLatched]
	lsl	w0,w0,6
	ldrb	w1,[x19,s_pal]
	orr	w0,w0,w1,lsl 4
	orr	w0,w0,3
	ldrb	w1,[x19,s_ppu2Bus]
	and	w1,w1,0x20
	orr	w0,w0,w1
	ldr	w1,[x19,s_frames]
	and	w1,w1,1
	orr	w0,w0,w1,lsl 7
	strb	wzr,[x19,s_hFlip]
	strb	wzr,[x19,s_vFlip]
	ldrb	w1,[x19,s_wrio]
	tbz	w1,7,.f1
	strb	wzr,[x19,s_counterLatched]
.f1:	strb	w0,[x19,s_ppu2Bus]
	ret

; w0 = r, w1 = v (8 bits)
ppu_write:
	cmp	w0,0x34
	b.hs	.ret
	jtab	x0,.t
.t:	dw	.w00-.t,.w01-.t,.w02-.t,.w03-.t,.w04-.t,.w05-.t,.w06-.t,.w07-.t
	dw	.w07-.t,.w07-.t,.w07-.t,.w0b-.t,.w0c-.t,.w0d-.t,.w0e-.t,.w0f-.t
	dw	.w10-.t,.w0f-.t,.w10-.t,.w0f-.t,.w10-.t,.w15-.t,.w16-.t,.w16-.t
	dw	.w18-.t,.w19-.t,.w1a-.t,.w1b-.t,.w1b-.t,.w1b-.t,.w1b-.t,.w1f-.t
	dw	.w1f-.t,.w21-.t,.w22-.t,.w23-.t,.w24-.t,.w25-.t,.w26-.t,.w26-.t
	dw	.w26-.t,.w26-.t,.w2a-.t,.w2b-.t,.w2c-.t,.w2d-.t,.w2e-.t,.w2f-.t
	dw	.w30-.t,.w31-.t,.w32-.t,.w33-.t
.ret:	ret
.w00:	ldrb	w2,[x19,s_inidisp]
	strb	w1,[x19,s_inidisp]
	tbz	w2,7,.ret
	tbnz	w1,7,.ret
	mov	x8,x30
	bl	vdisp_of
	mov	x30,x8
	ldr	w2,[x19,s_line]
	cmp	w2,w1
	b.ne	.ret
	ldrh	w2,[x19,s_oamReload]
	strh	w2,[x19,s_oamAddr]
	ret
.w01:	strb	w1,[x19,s_obsel]
	ret
.w02:	ldrh	w2,[x19,s_oamReload]
	and	w2,w2,0x200
	orr	w2,w2,w1,lsl 1
	strh	w2,[x19,s_oamReload]
	strh	w2,[x19,s_oamAddr]
	ret
.w03:	ldrh	w2,[x19,s_oamReload]
	and	w2,w2,0x1FE
	and	w3,w1,1
	orr	w2,w2,w3,lsl 9
	strh	w2,[x19,s_oamReload]
	strh	w2,[x19,s_oamAddr]
	lsr	w3,w1,7
	strb	w3,[x19,s_oamPrioRot]
	ret
.w04:	ldrh	w2,[x19,s_oamAddr]
	add	x3,x19,S_OAM
	cmp	w2,0x200
	b.hs	.o2
	tbnz	w2,0,.o1
	strb	w1,[x19,s_oamLatch]
	b	.o3
.o1:	ldrb	w4,[x19,s_oamLatch]
	sub	w5,w2,1
	strb	w4,[x3,x5]
	strb	w1,[x3,x2]
	b	.o3
.o2:	and	w4,w2,0x1F
	orr	w4,w4,0x200
	strb	w1,[x3,x4]
.o3:	add	w2,w2,1
	and	w2,w2,0x3FF
	strh	w2,[x19,s_oamAddr]
	ret
.w05:	strb	w1,[x19,s_bgmode]
	ret
.w06:	strb	w1,[x19,s_mosaic]
	ret
.w07:	add	x2,x19,s_bgsc
	sub	w0,w0,7
	strb	w1,[x2,x0]
	ret
.w0b:	strb	w1,[x19,s_bgnba]
	ret
.w0c:	strb	w1,[x19,s_bgnba+1]
	ret
.w0d:	ldrb	w2,[x19,s_m7Latch]
	orr	w2,w2,w1,lsl 8
	sbfx	w2,w2,0,13
	strh	w2,[x19,s_m7hofs]
	strb	w1,[x19,s_m7Latch]
.w0f:	sub	w0,w0,0x0D
	lsr	w0,w0,1				; n
	ldrb	w2,[x19,s_bgLatch]
	and	w2,w2,0xF8
	ldrb	w3,[x19,s_bgLatchH]
	and	w3,w3,7
	orr	w2,w2,w3
	orr	w2,w2,w1,lsl 8
	and	w2,w2,0x3FF
	add	x3,x19,s_bghofs
	strh	w2,[x3,x0,lsl 1]
	strb	w1,[x19,s_bgLatch]
	strb	w1,[x19,s_bgLatchH]
	ret
.w0e:	ldrb	w2,[x19,s_m7Latch]
	orr	w2,w2,w1,lsl 8
	sbfx	w2,w2,0,13
	strh	w2,[x19,s_m7vofs]
	strb	w1,[x19,s_m7Latch]
.w10:	sub	w0,w0,0x0E
	lsr	w0,w0,1
	ldrb	w2,[x19,s_bgLatch]
	orr	w2,w2,w1,lsl 8
	and	w2,w2,0x3FF
	add	x3,x19,s_bgvofs
	strh	w2,[x3,x0,lsl 1]
	strb	w1,[x19,s_bgLatch]
	ret
.w15:	strb	w1,[x19,s_vmain]
	ret
.w16:	cmp	w0,0x16
	b.ne	.a17
	strb	w1,[x19,s_vramAddr]
	b	vram_fill
.a17:	strb	w1,[x19,s_vramAddr+1]
	b	vram_fill
.w18:	str	x30,[sp,-16]!
	str	w1,[sp,8]
	bl	vram_remap
	and	w0,w0,0x7FFF
	add	x1,x19,S_VRAM
	ldr	w2,[sp,8]
	lsl	w0,w0,1
	strb	w2,[x1,x0]
	mov	w0,0
	bl	vram_step
	ldr	x30,[sp],16
	ret
.w19:	str	x30,[sp,-16]!
	str	w1,[sp,8]
	bl	vram_remap
	and	w0,w0,0x7FFF
	lsl	w0,w0,1
	add	w0,w0,1
	add	x1,x19,S_VRAM
	ldr	w2,[sp,8]
	strb	w2,[x1,x0]
	mov	w0,1
	bl	vram_step
	ldr	x30,[sp],16
	ret
.w1a:	strb	w1,[x19,s_m7sel]
	ret
.w1b:	ldrb	w2,[x19,s_m7Latch]		; m7a, b, c, d
	orr	w2,w2,w1,lsl 8
	sub	w0,w0,0x1B
	add	x3,x19,s_m7a
	strh	w2,[x3,x0,lsl 1]
	strb	w1,[x19,s_m7Latch]
	ret
.w1f:	ldrb	w2,[x19,s_m7Latch]		; m7x, y
	orr	w2,w2,w1,lsl 8
	sbfx	w2,w2,0,13
	sub	w0,w0,0x1F
	add	x3,x19,s_m7x
	strh	w2,[x3,x0,lsl 1]
	strb	w1,[x19,s_m7Latch]
	ret
.w21:	lsl	w1,w1,1
	strh	w1,[x19,s_cgAddr]
	ret
.w22:	ldrh	w2,[x19,s_cgAddr]
	add	x3,x19,S_CGRAM
	tbnz	w2,0,.c1
	strb	w1,[x19,s_cgLatch]
	b	.c2
.c1:	ldrb	w4,[x19,s_cgLatch]
	sub	w5,w2,1
	strb	w4,[x3,x5]
	and	w1,w1,0x7F
	strb	w1,[x3,x2]
.c2:	add	w2,w2,1
	and	w2,w2,0x1FF
	strh	w2,[x19,s_cgAddr]
	ret
.w23:	strb	w1,[x19,s_w12sel]
	ret
.w24:	strb	w1,[x19,s_w34sel]
	ret
.w25:	strb	w1,[x19,s_wobjsel]
	ret
.w26:	add	x2,x19,s_wh
	sub	w0,w0,0x26
	strb	w1,[x2,x0]
	ret
.w2a:	strb	w1,[x19,s_wbglog]
	ret
.w2b:	strb	w1,[x19,s_wobjlog]
	ret
.w2c:	strb	w1,[x19,s_tm]
	ret
.w2d:	strb	w1,[x19,s_ts]
	ret
.w2e:	strb	w1,[x19,s_tmw]
	ret
.w2f:	strb	w1,[x19,s_tsw]
	ret
.w30:	strb	w1,[x19,s_cgwsel]
	ret
.w31:	strb	w1,[x19,s_cgadsub]
	ret
.w32:	ldrh	w2,[x19,s_fixedColor]
	and	w3,w1,0x1F
	tbz	w1,5,.f1
	bfxil	w2,w3,0,5
.f1:	tbz	w1,6,.f2
	bfi	w2,w3,5,5
.f2:	tbz	w1,7,.f3
	bfi	w2,w3,10,5
.f3:	strh	w2,[x19,s_fixedColor]
	ret
.w33:	strb	w1,[x19,s_setini]
	ret

; ---- la ligne --------------------------------------------------------------

; w0 = plan (0-5) -> x0 = son masque ; perd x1-x8
ppu_window:
	mov	x8,x0
	mov	w1,256
	mul	w1,w1,w0
	adm	x0,S_WIN
	add	x0,x0,x1			; m
	adm	x2,S_WINVALID
	ldrb	w3,[x2,x8]
	cbz	w3,.make
	ret
.make:	mov	w3,1
	strb	w3,[x2,x8]
	cmp	w8,4
	b.hs	.ob
	ldrb	w1,[x19,s_w12sel]
	cmp	w8,2
	b.lo	.a
	ldrb	w1,[x19,s_w34sel]
.a:	and	w2,w8,1
	lsl	w2,w2,2
	lsr	w1,w1,w2
	and	w1,w1,0xF			; sel
	ldrb	w2,[x19,s_wbglog]
	lsl	w3,w8,1
	lsr	w2,w2,w3
	and	w2,w2,3				; logic
	b	.go
.ob:	sub	w3,w8,4
	ldrb	w1,[x19,s_wobjsel]
	lsl	w4,w3,2
	lsr	w1,w1,w4
	and	w1,w1,0xF
	ldrb	w2,[x19,s_wobjlog]
	lsl	w4,w3,1
	lsr	w2,w2,w4
	and	w2,w2,3
.go:	mov	w3,0xA				; e1, e2
	tst	w1,w3
	b.ne	.some
	mov	x3,x0
	mov	w1,256/16
.z:	stp	xzr,xzr,[x3],16
	subs	w1,w1,1
	b.ne	.z
	ret
.some:	ubfx	w3,w1,0,1			; i1
	ubfx	w4,w1,2,1			; i2
	ldrb	w5,[x19,s_wh]
	ldrb	w6,[x19,s_wh+1]
	ldrb	w7,[x19,s_wh+2]
	ldrb	w8,[x19,s_wh+3]
	stp	x9,x10,[sp,-16]!
	mov	w9,0				; x
.x:	cmp	w9,w5
	cset	w10,hs
	cmp	w9,w6
	cset	w16,ls
	and	w10,w10,w16
	eor	w10,w10,w3			; a
	cmp	w9,w7
	cset	w17,hs
	cmp	w9,w8
	cset	w16,ls
	and	w17,w17,w16
	eor	w17,w17,w4			; b
	tbnz	w1,3,.e2
	b	.ra				; !e2 : a
.e2:	tbnz	w1,1,.both
	mov	w10,w17				; !e1 : b
	b	.ra
.both:	cbnz	w2,.l1
	orr	w10,w10,w17
	b	.ra
.l1:	cmp	w2,1
	b.ne	.l2
	and	w10,w10,w17
	b	.ra
.l2:	eor	w10,w10,w17
	cmp	w2,2
	b.eq	.ra
	eor	w10,w10,1
.ra:	strb	w10,[x0,x9]
	add	w9,w9,1
	cmp	w9,256
	b.lo	.x
	ldp	x9,x10,[sp],16
	ret

; x23 = vram, w0 = ad -> x0 : les 8 pixels de la rangee (octet 0 : a gauche) ; w25 = bpp
; perd x1-x4, x17
tile_px:
	add	x17,x19,S_SPREAD
	and	w1,w0,0x7FFF
	ldrh	w1,[x23,x1,lsl 1]
	and	w2,w1,0xFF
	lsr	w1,w1,8
	ldr	x3,[x17,x2,lsl 3]
	ldr	x4,[x17,x1,lsl 3]
	orr	x3,x3,x4,lsl 1
	cmp	w25,4
	b.lo	.f
	add	w1,w0,8
	and	w1,w1,0x7FFF
	ldrh	w1,[x23,x1,lsl 1]
	and	w2,w1,0xFF
	lsr	w1,w1,8
	ldr	x4,[x17,x2,lsl 3]
	orr	x3,x3,x4,lsl 2
	ldr	x4,[x17,x1,lsl 3]
	orr	x3,x3,x4,lsl 3
	cmp	w25,8
	b.ne	.f
	add	w1,w0,16
	and	w1,w1,0x7FFF
	ldrh	w1,[x23,x1,lsl 1]
	and	w2,w1,0xFF
	lsr	w1,w1,8
	ldr	x4,[x17,x2,lsl 3]
	orr	x3,x3,x4,lsl 4
	ldr	x4,[x17,x1,lsl 3]
	orr	x3,x3,x4,lsl 5
	add	w1,w0,24
	and	w1,w1,0x7FFF
	ldrh	w1,[x23,x1,lsl 1]
	and	w2,w1,0xFF
	lsr	w1,w1,8
	ldr	x4,[x17,x2,lsl 3]
	orr	x3,x3,x4,lsl 6
	ldr	x4,[x17,x1,lsl 3]
	orr	x3,x3,x4,lsl 7
.f:	mov	x0,x3
	ret

; w0 = p (pixel), w1 = pal -> w0 = couleur directe (perd w2, w3)
direct_color:
	and	w2,w0,7
	lsl	w2,w2,2
	and	w3,w1,1
	orr	w2,w2,w3,lsl 1			; r
	ubfx	w3,w0,3,3
	lsl	w3,w3,2
	and	w4,w1,2
	orr	w3,w3,w4
	orr	w2,w2,w3,lsl 5			; g
	ubfx	w3,w0,6,2
	lsl	w3,w3,3
	and	w4,w1,4
	orr	w3,w3,w4
	orr	w0,w2,w3,lsl 10			; b
	ret

; w0 = n, w1 = y, w2 = bpp
; pile : variables de la voie lente
RB_n		= 0
RB_y		= 4
RB_mode		= 8
RB_hires	= 12
RB_msize	= 16
RB_mos		= 20
RB_opt		= 24
RB_yy		= 28
RB_mapBase	= 32
RB_hofs		= 36
RB_vofs		= 40
RB_lastKey	= 44
RB_cpal		= 48
RB_cpri		= 52
RB_cflip	= 56
RB_pixL		= 64
RB_SIZE		= 80

render_bg:
	prologue
	sub	sp,sp,RB_SIZE
	str	w0,[sp,RB_n]
	str	w1,[sp,RB_y]
	mov	w25,w2				; bpp
	adm	x21,S_BGCOL
	add	x21,x21,x0,lsl 9		; col
	adm	x22,S_BGPRI
	add	x22,x22,x0,lsl 8		; pri
	add	x23,x19,S_VRAM
	add	x24,x19,S_CGRAM
	ldrb	w3,[x19,s_bgmode]
	and	w4,w3,7				; mode
	str	w4,[sp,RB_mode]
	cmp	w4,5
	cset	w5,eq
	cmp	w4,6
	cset	w6,eq
	orr	w5,w5,w6			; hires
	str	w5,[sp,RB_hires]
	mov	w6,0x10
	lsl	w6,w6,w0
	tst	w3,w6				; big
	cset	w6,ne				; big
	orr	w5,w5,w6
	add	w26,w5,3			; tws
	add	w27,w6,3			; ths
	add	x7,x19,s_bgsc
	ldrb	w7,[x7,x0]			; bgsc [n]
	and	w8,w7,0xFC
	lsl	w8,w8,8
	str	w8,[sp,RB_mapBase]
	ubfx	w14,w7,0,1			; wide
	ubfx	w15,w7,1,1			; tall
	lsr	w1,w0,1
	add	x2,x19,s_bgnba
	ldrb	w1,[x2,x1]
	and	w2,w0,1
	lsl	w2,w2,2
	lsr	w1,w1,w2
	and	w1,w1,0xF
	lsl	w28,w1,12			; chrBase
	ldrb	w1,[x19,s_mosaic]
	lsr	w2,w1,4
	add	w2,w2,1				; msize
	str	w2,[sp,RB_msize]
	lsr	w1,w1,w0
	and	w1,w1,1
	cmp	w2,1
	csel	w1,wzr,w1,ls			; mos
	str	w1,[sp,RB_mos]
	ldr	w3,[sp,RB_y]
	mov	w5,w3
	cbz	w1,.yy
	sub	w5,w3,1
	udiv	w6,w5,w2
	msub	w6,w6,w2,w5			; (y - 1) % msize
	sub	w5,w3,w6
.yy:	str	w5,[sp,RB_yy]
	mov	w6,0				; palBase
	cbnz	w4,.pb
	lsl	w6,w0,5
.pb:	mov	w29,0				; direct
	cmp	w25,8
	b.ne	.nd
	ldrb	w7,[x19,s_cgwsel]
	and	w29,w7,1
.nd:	mov	w7,0				; opt
	cmp	w0,2
	b.hs	.op
	cmp	w4,2
	b.eq	.op1
	cmp	w4,4
	b.eq	.op1
	cmp	w4,6
	b.ne	.op
.op1:	mov	w7,1
.op:	str	w7,[sp,RB_opt]
	lsl	w12,w25,2			; wpt
	add	x8,x19,s_bghofs
	ldrh	w10,[x8,x0,lsl 1]		; hofs0
	add	x8,x19,s_bgvofs
	ldrh	w11,[x8,x0,lsl 1]		; vofs0
	str	w10,[sp,RB_hofs]
	str	w11,[sp,RB_vofs]
	ldr	w8,[sp,RB_hires]
	orr	w8,w8,w7
	orr	w8,w8,w1
	cbnz	w8,bg_slow
	; le cas courant : huit pixels a la fois
	; w10 hx, w9 x, w13 rowBase, w8 fy0, w6 palBase
	add	w0,w11,w5
	and	w0,w0,0x3FF			; vy
	and	w10,w10,0x3FF			; hx
	lsr	w1,w0,w27			; ty
	mov	w2,1
	lsl	w2,w2,w27
	sub	w2,w2,1
	and	w8,w0,w2			; fy0
	and	w3,w1,31
	ldr	w13,[sp,RB_mapBase]
	add	w13,w13,w3,lsl 5		; rowBase
	tbz	w1,5,.nt
	cbz	w15,.nt
	mov	w3,0x400
	mov	w4,0x800
	cmp	w14,0
	csel	w3,w4,w3,ne
	add	w13,w13,w3
.nt:	mov	w9,0
.tile:	lsr	w1,w10,w26			; tx
	and	w2,w1,31
	add	w0,w13,w2
	tbz	w1,5,.nw
	cbz	w14,.nw
	add	w0,w0,0x400
.nw:	and	w0,w0,0x7FFF
	ldrh	w7,[x23,x0,lsl 1]		; e
	mov	w1,1
	lsl	w1,w1,w26
	sub	w1,w1,1
	and	w2,w10,w1			; fx
	tbz	w7,14,.hf
	sub	w2,w1,w2
.hf:	mov	w3,w8				; fy
	tbz	w7,15,.vf
	mov	w1,1
	lsl	w1,w1,w27
	sub	w1,w1,1
	sub	w3,w1,w8
.vf:	and	w1,w7,0x3FF
	add	w1,w1,w2,lsr 3
	lsr	w2,w3,3
	add	w1,w1,w2,lsl 4			; ch
	mul	w1,w1,w12
	and	w3,w3,7
	add	w0,w28,w1
	add	w0,w0,w3			; ad
	bl	tile_px
	tbz	w7,14,.nf
	rev	x0,x0
.nf:	and	w1,w10,7			; start
	mov	w2,8
	sub	w2,w2,w1			; cnt
	mov	w3,256
	sub	w3,w3,w9
	cmp	w2,w3
	csel	w2,w3,w2,hi
	lsl	w3,w1,3
	lsr	x0,x0,x3			; px
	ubfx	w3,w7,13,1			; cpri
	ubfx	w4,w7,10,3			; cpal
	add	w10,w10,w2
	and	w10,w10,0x3FF
	add	x5,x21,x9,lsl 1
	add	x16,x22,x9
	add	w9,w9,w2
	cbnz	w29,.dir
	mov	w1,0
	cmp	w25,8
	b.eq	.pl
	lsl	w1,w4,w25
	add	w1,w1,w6			; pbase
.pl:	and	x7,x0,0xFF
	lsr	x0,x0,8
	cbz	w7,.pz
	strb	w3,[x16],1
	add	w7,w7,w1
	ldrh	w7,[x24,x7,lsl 1]
	strh	w7,[x5],2
	subs	w2,w2,1
	b.ne	.pl
	b	.tn
.pz:	mov	w7,0xFF
	strb	w7,[x16],1
	add	x5,x5,2
	subs	w2,w2,1
	b.ne	.pl
	b	.tn
.dir:	mov	w17,w2
	mov	x7,x0
.dl:	and	x0,x7,0xFF
	lsr	x7,x7,8
	cbz	w0,.dz
	strb	w3,[x16],1
	mov	w1,w4
	stp	x3,x4,[sp,-16]!
	bl	direct_color
	ldp	x3,x4,[sp],16
	strh	w0,[x5],2
	b	.dn
.dz:	mov	w0,0xFF
	strb	w0,[x16],1
	add	x5,x5,2
.dn:	subs	w17,w17,1
	b.ne	.dl
.tn:	cmp	w9,256
	b.lo	.tile
	add	sp,sp,RB_SIZE
	epilogue

; un pixel a la fois : mosaique, offset-per-tile, grande resolution
bg_slow:
	mov	w0,-1
	str	w0,[sp,RB_lastKey]
	str	wzr,[sp,RB_cpal]
	str	wzr,[sp,RB_cpri]
	str	wzr,[sp,RB_cflip]
	str	xzr,[sp,RB_pixL]
	str	w6,[sp,RB_pixL+8]		; palBase
	mov	w9,0				; x
.x:	mov	w13,w9				; sx
	ldr	w0,[sp,RB_mos]
	cbz	w0,.nm
	ldr	w1,[sp,RB_msize]
	udiv	w2,w9,w1
	msub	w2,w2,w1,w9
	sub	w13,w9,w2
.nm:	ldr	w10,[sp,RB_hofs]		; ho
	ldr	w11,[sp,RB_vofs]		; vo
	ldr	w0,[sp,RB_opt]
	cbz	w0,.po
	and	w0,w10,7
	add	w0,w13,w0
	lsr	w0,w0,3				; c
	cbz	w0,.po
	ldrh	w1,[x19,s_bghofs+4]		; h3
	ldrh	w2,[x19,s_bgvofs+4]		; v3
	sub	w0,w0,1
	add	w0,w0,w1,lsr 3
	and	w6,w0,63			; tc
	ubfx	w7,w2,3,6			; tr
	mov	w0,w6
	mov	w1,w7
	bl	opt_ent				; hv
	mov	w8,w0
	ldr	w2,[sp,RB_n]
	mov	w3,0x2000
	lsl	w3,w3,w2			; bitv
	ldr	w4,[sp,RB_mode]
	cmp	w4,4
	b.ne	.o2
	tst	w8,w3
	b.eq	.po
	tbz	w8,15,.o1
	and	w11,w8,0x3FF
	b	.po
.o1:	and	w0,w8,0x3F8
	and	w1,w10,7
	orr	w10,w0,w1
	b	.po
.o2:	stp	x3,xzr,[sp,-16]!
	mov	w0,w6
	add	w1,w7,1
	bl	opt_ent				; vv
	ldp	x3,x4,[sp],16
	tst	w8,w3
	b.eq	.o3
	and	w1,w8,0x3F8
	and	w2,w10,7
	orr	w10,w1,w2
.o3:	tst	w0,w3
	b.eq	.po
	and	w11,w0,0x3FF
.po:	ldr	w0,[sp,RB_hires]
	cbz	w0,.lo
	lsl	w1,w10,1
	add	w1,w1,w13,lsl 1
	and	w1,w1,0x7FF			; hx
	b	.hxd
.lo:	add	w1,w10,w13
	and	w1,w1,0x3FF
.hxd:	mov	w10,w1
	ldr	w0,[sp,RB_yy]
	add	w11,w11,w0
	and	w11,w11,0x3FF			; vy
	lsr	w0,w10,3
	orr	w0,w0,w11,lsl 11		; key
	ldr	w1,[sp,RB_lastKey]
	cmp	w0,w1
	b.eq	.same
	str	w0,[sp,RB_lastKey]
	lsr	w0,w10,w26			; tx
	lsr	w1,w11,w27			; ty
	and	w2,w1,31
	ldr	w3,[sp,RB_mapBase]
	add	w3,w3,w2,lsl 5
	and	w2,w0,31
	add	w3,w3,w2
	tbz	w0,5,.w1
	cbz	w14,.w1
	add	w3,w3,0x400
.w1:	tbz	w1,5,.w2
	cbz	w15,.w2
	mov	w2,0x400
	mov	w4,0x800
	cmp	w14,0
	csel	w2,w4,w2,ne
	add	w3,w3,w2
.w2:	and	w3,w3,0x7FFF
	ldrh	w7,[x23,x3,lsl 1]		; e
	mov	w1,1
	lsl	w1,w1,w26
	sub	w1,w1,1
	and	w2,w10,w1			; fx
	ubfx	w3,w7,14,1
	str	w3,[sp,RB_cflip]
	cbz	w3,.a1
	sub	w2,w1,w2
.a1:	mov	w1,1
	lsl	w1,w1,w27
	sub	w1,w1,1
	and	w3,w11,w1			; fy
	tbz	w7,15,.a2
	sub	w3,w1,w3
.a2:	and	w1,w7,0x3FF
	add	w1,w1,w2,lsr 3
	lsr	w2,w3,3
	add	w1,w1,w2,lsl 4
	mul	w1,w1,w12
	and	w3,w3,7
	add	w0,w28,w1
	add	w0,w0,w3
	bl	tile_px
	str	x0,[sp,RB_pixL]
	ubfx	w0,w7,10,3
	str	w0,[sp,RB_cpal]
	ubfx	w0,w7,13,1
	str	w0,[sp,RB_cpri]
.same:	and	w0,w10,7			; fb8
	ldr	w1,[sp,RB_cflip]
	cbz	w1,.b1
	mov	w1,7
	sub	w0,w1,w0
.b1:	add	x1,sp,RB_pixL
	ldrb	w0,[x1,x0]			; p
	cbnz	w0,.vis
	mov	w0,0xFF
	strb	w0,[x22,x9]
	b	.nx
.vis:	ldr	w1,[sp,RB_cpri]
	strb	w1,[x22,x9]
	cbz	w29,.nd
	ldr	w1,[sp,RB_cpal]
	bl	direct_color
	b	.put
.nd:	cmp	w25,8
	b.eq	.c8
	ldr	w1,[sp,RB_cpal]
	lsl	w1,w1,w25
	ldr	w2,[sp,RB_pixL+8]
	add	w1,w1,w2
	add	w0,w0,w1
.c8:	ldrh	w0,[x24,x0,lsl 1]
.put:	strh	w0,[x21,x9,lsl 1]
.nx:	add	w9,w9,1
	cmp	w9,256
	b.lo	.x
	add	sp,sp,RB_SIZE
	epilogue

; w0 = cx, w1 = cy -> w0 : l'entree du plan 3 (perd w2-w5)
opt_ent:
	ldrb	w2,[x19,s_bgsc+2]
	and	w3,w2,0xFC
	lsl	w3,w3,8				; m3
	and	w4,w1,31
	add	w3,w3,w4,lsl 5
	and	w4,w0,31
	add	w3,w3,w4
	tbz	w0,5,.a
	tbz	w2,0,.a
	add	w3,w3,0x400
.a:	tbz	w1,5,.b
	tbz	w2,1,.b
	mov	w4,0x400
	mov	w5,0x800
	tst	w2,1
	csel	w4,w5,w4,ne
	add	w3,w3,w4
.b:	and	w3,w3,0x7FFF
	ldrh	w0,[x23,x3,lsl 1]
	ret

; w0 = y
render_mode7:
	prologue
	add	x23,x19,S_VRAM
	add	x24,x19,S_CGRAM
	ldrb	w1,[x19,s_mosaic]
	lsr	w2,w1,4
	add	w2,w2,1				; msize
	and	w3,w1,3				; mos1 | mos2
	cmp	w2,1
	csel	w3,wzr,w3,ls
	mov	w4,w0				; yy
	cbz	w3,.a
	sub	w5,w0,1
	udiv	w6,w5,w2
	msub	w6,w6,w2,w5
	sub	w4,w0,w6
.a:	mov	w27,w2				; msize
	mov	w28,w3				; mos
	ldrb	w1,[x19,s_m7sel]
	mov	w5,w4
	tbz	w1,1,.b
	mov	w5,255
	sub	w5,w5,w4			; ly
.b:	ldrsh	w21,[x19,s_m7a]			; a
	ldrsh	w6,[x19,s_m7b]			; b
	ldrsh	w22,[x19,s_m7c]			; c
	ldrsh	w7,[x19,s_m7d]			; d
	ldrsh	w8,[x19,s_m7x]			; cx
	ldrsh	w9,[x19,s_m7y]			; cy
	ldrsh	w10,[x19,s_m7hofs]
	ldrsh	w11,[x19,s_m7vofs]
	sub	w10,w10,w8			; ho - cx
	sub	w11,w11,w9			; vo - cy
	tst	w10,0x2000
	b.eq	.c1
	orr	w10,w10,0xFFFFFC00
	b	.c2
.c1:	and	w10,w10,1023
.c2:	tst	w11,0x2000
	b.eq	.c3
	orr	w11,w11,0xFFFFFC00
	b	.c4
.c3:	and	w11,w11,1023
.c4:	mov	w12,0xFFFFFFC0
	mul	w13,w21,w10
	and	w13,w13,w12
	mul	w14,w6,w11
	and	w14,w14,w12
	add	w13,w13,w14
	mul	w14,w6,w5
	and	w14,w14,w12
	add	w13,w13,w14
	add	w25,w13,w8,lsl 8		; psx
	mul	w13,w22,w10
	and	w13,w13,w12
	mul	w14,w7,w11
	and	w14,w14,w12
	add	w13,w13,w14
	mul	w14,w7,w5
	and	w14,w14,w12
	add	w13,w13,w14
	add	w26,w13,w9,lsl 8		; psy
	ldrb	w13,[x19,s_setini]
	ubfx	w13,w13,6,1			; ext
	ldrb	w14,[x19,s_cgwsel]
	and	w14,w14,1			; direct
	lsr	w15,w1,6			; over
	and	w12,w1,1			; m7sel & 1
	mov	w9,0				; x
.x:	mov	w0,w9				; sx
	cbz	w28,.s1
	udiv	w1,w9,w27
	msub	w1,w1,w27,w9
	sub	w0,w9,w1
.s1:	cbz	w12,.s2
	mov	w1,255
	sub	w0,w1,w0			; lx
.s2:	mul	w1,w21,w0
	add	w1,w1,w25
	asr	w1,w1,8				; px
	mul	w2,w22,w0
	add	w2,w2,w26
	asr	w2,w2,8				; py
	orr	w3,w1,w2
	mov	w4,0xFFFFFC00
	tst	w3,w4
	b.eq	.in
	cmp	w15,2
	b.ne	.ov
	adm	x3,S_BGPRI
	mov	w4,0xFF
	strb	w4,[x3,x9]
	add	x3,x3,256
	strb	w4,[x3,x9]
	b	.nx
.ov:	mov	w3,0				; tile
	cmp	w15,3
	b.eq	.tl
.in:	ubfx	w3,w2,3,7
	lsl	w3,w3,7
	ubfx	w4,w1,3,7
	add	w3,w3,w4
	lsl	w3,w3,1
	ldrb	w3,[x23,x3]			; tile
.tl:	lsl	w3,w3,6
	and	w4,w2,7
	add	w3,w3,w4,lsl 3
	and	w4,w1,7
	add	w3,w3,w4
	lsl	w3,w3,1
	add	w3,w3,1
	and	w3,w3,0x1FFFF
	ldrb	w0,[x23,x3]			; p
	adm	x4,S_BGPRI
	cbz	w0,.p0
	strb	wzr,[x4,x9]
	mov	w10,w0
	cbz	w14,.cg
	mov	w1,0
	bl	direct_color
	b	.p1
.cg:	ldrh	w0,[x24,x0,lsl 1]
.p1:	adm	x4,S_BGCOL
	strh	w0,[x4,x9,lsl 1]
	mov	w0,w10
	b	.ext
.p0:	mov	w1,0xFF
	strb	w1,[x4,x9]
.ext:	cbz	w13,.nx
	adm	x4,S_BGPRI
	add	x4,x4,256
	ands	w1,w0,0x7F			; q
	b.eq	.q0
	lsr	w2,w0,7
	strb	w2,[x4,x9]
	ldrh	w1,[x24,x1,lsl 1]
	adm	x4,S_BGCOL
	add	x4,x4,512
	strh	w1,[x4,x9,lsl 1]
	b	.nx
.q0:	mov	w1,0xFF
	strb	w1,[x4,x9]
.nx:	add	w9,w9,1
	cmp	w9,256
	b.lo	.x
	epilogue

obj_sw:	db	8,8,8,16,16,32,16,16
obj_sh:	db	8,8,8,16,16,32,32,32
obj_lw:	db	16,32,64,32,64,64,32,32
obj_lh:	db	16,32,64,32,64,64,64,32

; w0 = n -> w1 = hi (2 bits utiles), w2 = w, w3 = h, x4 = o ; w26 = sz ; perd x5
obj_info:
	add	x4,x19,S_OAM
	lsr	w1,w0,2
	add	w1,w1,0x200
	ldrb	w1,[x4,x1]
	and	w2,w0,3
	lsl	w2,w2,1
	lsr	w1,w1,w2			; hi
	add	x4,x4,x0,lsl 2			; o
	tbnz	w1,1,.big
	adr	x2,obj_sw
	ldrb	w2,[x2,x26]
	adr	x3,obj_sh
	ldrb	w3,[x3,x26]
	ret
.big:	adr	x2,obj_lw
	ldrb	w2,[x2,x26]
	adr	x3,obj_lh
	ldrb	w3,[x3,x26]
	ret

; w0 = y
render_obj:
	prologue
	sub	sp,sp,48
	mov	w27,w0				; y
	add	x23,x19,S_VRAM
	add	x24,x19,S_CGRAM
	adm	x0,S_OBJPRI
	mov	x1,-1
	mov	w2,32
.ff:	str	x1,[x0],8
	subs	w2,w2,1
	b.ne	.ff
	ldrb	w0,[x19,s_obsel]
	lsr	w26,w0,5			; sz
	mov	w21,0				; first
	ldrb	w0,[x19,s_oamPrioRot]
	cbz	w0,.a
	ldrh	w0,[x19,s_oamReload]
	ubfx	w21,w0,2,7
.a:	mov	w22,0				; cnt
	mov	w25,0				; i
.sel:	add	w0,w21,w25
	and	w0,w0,127			; n
	mov	w8,w0
	bl	obj_info
	ldrb	w5,[x4]
	and	w6,w1,1
	orr	w5,w5,w6,lsl 8			; sx
	cmp	w5,256
	b.lo	.s1
	sub	w5,w5,512
.s1:	ldrb	w6,[x4,1]
	sub	w7,w27,1
	sub	w7,w7,w6
	and	w7,w7,0xFF			; row
	cmp	w7,w3
	b.hs	.sn
	neg	w6,w2
	cmp	w5,w6
	b.le	.sn
	cmp	w5,256
	b.ge	.sn
	cmp	w22,32
	b.ne	.s2
	ldrb	w0,[x19,s_stat77]
	orr	w0,w0,0x40
	strb	w0,[x19,s_stat77]
	b	.draw
.s2:	strb	w8,[sp,x22]
	add	w22,w22,1
.sn:	add	w25,w25,1
	cmp	w25,128
	b.lo	.sel
.draw:	ldrb	w0,[x19,s_obsel]
	and	w1,w0,7
	lsl	w12,w1,13			; nameBase
	ubfx	w1,w0,3,2
	add	w1,w1,1
	lsl	w13,w1,12			; gap
.k:	subs	w22,w22,1
	b.lo	.done
	ldrb	w0,[sp,x22]
	bl	obj_info
	ldrb	w5,[x4]
	and	w6,w1,1
	orr	w5,w5,w6,lsl 8
	cmp	w5,256
	b.lo	.k1
	sub	w5,w5,512			; sx
.k1:	ldrb	w6,[x4,1]
	sub	w7,w27,1
	sub	w7,w7,w6
	and	w7,w7,0xFF			; row
	ldrb	w8,[x4,3]			; attr
	tbz	w8,7,.k2
	sub	w7,w3,w7
	sub	w7,w7,1
.k2:	ubfx	w9,w8,4,2			; prio
	ubfx	w10,w8,1,3			; pal
	mov	w11,w12				; base
	tbz	w8,0,.k3
	add	w11,w11,w13
.k3:	lsr	w14,w2,3			; tiles
	ldrb	w15,[x4,2]			; o [2]
	mov	w21,0				; c
.c:	add	w25,w5,w21,lsl 3		; x0
	cmn	w25,8
	b.le	.cn
	cmp	w25,256
	b.ge	.cn
	mov	w0,w21				; tx
	tbz	w8,6,.c1
	sub	w0,w14,1
	sub	w0,w0,w21
.c1:	add	w0,w0,w15
	and	w0,w0,0x0F			; chx
	lsr	w1,w15,4
	add	w1,w1,w7,lsr 3
	and	w1,w1,0x0F			; chy
	add	w0,w0,w1,lsl 4
	lsl	w0,w0,4
	add	w0,w0,w11
	and	w1,w7,7
	add	w0,w0,w1			; ad
	and	w1,w0,0x7FFF
	ldrh	w16,[x23,x1,lsl 1]		; w0
	add	w1,w0,8
	and	w1,w1,0x7FFF
	ldrh	w17,[x23,x1,lsl 1]		; w1
	; le plan de bits en 8 octets
	add	x6,x19,S_SPREAD
	and	w0,w16,0xFF
	ldr	x0,[x6,x0,lsl 3]
	lsr	w1,w16,8
	ldr	x1,[x6,x1,lsl 3]
	orr	x0,x0,x1,lsl 1
	and	w1,w17,0xFF
	ldr	x1,[x6,x1,lsl 3]
	orr	x0,x0,x1,lsl 2
	lsr	w1,w17,8
	ldr	x1,[x6,x1,lsl 3]
	orr	x0,x0,x1,lsl 3
	cbz	x0,.cn				; (rien a dessiner)
	tbz	w8,6,.c2
	rev	x0,x0
.c2:	lsl	w1,w10,4
	add	w1,w1,128			; 128 + pal * 16
	cmp	w10,4
	cset	w17,hs				; math
	mov	w2,w25				; xx
	mov	w6,8
	stp	x7,x8,[sp,32]
	adm	x7,S_OBJPRI
	adm	x8,S_OBJCOL
.b:	and	x3,x0,0xFF
	lsr	x0,x0,8
	cbz	w3,.bn
	cmp	w2,256
	b.hs	.bn
	add	w3,w3,w1
	ldrh	w3,[x24,x3,lsl 1]
	strh	w3,[x8,x2,lsl 1]
	strb	w9,[x7,x2]
	add	x3,x7,S_OBJMATH-S_OBJPRI
	strb	w17,[x3,x2]
.bn:	add	w2,w2,1
	subs	w6,w6,1
	b.ne	.b
	ldp	x7,x8,[sp,32]
.cn:	add	w21,w21,1
	cmp	w21,w14
	b.lo	.c
	b	.k
.done:	add	sp,sp,48
	epilogue

order0:	db	0x13,0x01,0x03,0x12,0x00,0x02,0x11,0x05,0x07,0x10,0x04,0x06
order1h: db	0x05,0x13,0x01,0x03,0x12,0x00,0x02,0x11,0x10,0x04
order1:	db	0x13,0x01,0x03,0x12,0x00,0x02,0x11,0x05,0x10,0x04
order2:	db	0x13,0x01,0x12,0x03,0x11,0x00,0x10,0x02
order6:	db	0x13,0x01,0x12,0x11,0x00,0x10
order7:	db	0x13,0x12,0x03,0x11,0x00,0x10,0x02
bpp_tab: db	2,2,2,2, 4,4,2,0, 4,4,0,0, 8,4,0,0, 8,2,0,0, 4,2,0,0, 4,0,0,0, 0,0,0,0
	align 4

; w0 = y
render_line:
	cmp	w0,SNES_H
	b.hi	.r
	prologue
	save_all
	sub	sp,sp,16
	str	w0,[sp]
	sub	w1,w0,1
	lsl	w1,w1,10
	add	x2,x19,S_FB
	add	x2,x2,x1
	str	x2,[sp,8]			; out
	ldrb	w0,[x19,s_inidisp]
	tbz	w0,7,.on
	mov	x0,x2
	mov	w1,256/2
.b:	str	xzr,[x0],8
	subs	w1,w1,1
	b.ne	.b
	b	.end
.on:	adm	x0,S_WINVALID
	str	xzr,[x0]
	b	rl_main
.end:	add	sp,sp,16
	rest_all
	epilogue
.r:	ret

rl_main:
	ldrb	w0,[x19,s_ts]
	ldrb	w10,[x19,s_tm]
	orr	w10,w10,w0			; used
	ldrb	w11,[x19,s_bgmode]
	and	w11,w11,7			; mode
	mov	w12,0				; have
	cmp	w11,7
	b.ne	.bgs
	tst	w10,3
	b.eq	.obj
	ldr	w0,[sp]
	str	w10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	bl	render_mode7
	ldp	x11,x12,[sp],16
	ldr	w10,[sp],16
	ldrb	w0,[x19,s_setini]
	mov	w12,1
	tbz	w0,6,.obj
	mov	w12,3
	b	.obj
.bgs:	mov	w13,0				; n
.bg:	adr	x0,bpp_tab
	add	x0,x0,x11,lsl 2
	ldrb	w2,[x0,x13]			; bpp
	cbz	w2,.bn
	lsr	w0,w10,w13
	tbz	w0,0,.bn
	mov	w0,w13
	ldr	w1,[sp]
	stp	x10,x11,[sp,-16]!
	stp	x12,x13,[sp,-16]!
	bl	render_bg
	ldp	x12,x13,[sp],16
	ldp	x10,x11,[sp],16
	mov	w0,1
	lsl	w0,w0,w13
	orr	w12,w12,w0
.bn:	add	w13,w13,1
	cmp	w13,4
	b.lo	.bg
.obj:	tbz	w10,4,.dep
	ldr	w0,[sp]
	stp	x11,x12,[sp,-16]!
	bl	render_obj
	ldp	x11,x12,[sp],16
.dep:	; profondeur de chaque (plan, priorite)
	adm	x13,S_DBG
	str	xzr,[x13]
	adm	x14,S_DOBJ
	str	wzr,[x14]
	adr	x0,order2
	mov	w1,8
	cbnz	w11,.m1
	adr	x0,order0
	mov	w1,12
	b	.mz
.m1:	cmp	w11,1
	b.ne	.m6
	ldrb	w2,[x19,s_bgmode]
	adr	x0,order1
	mov	w1,10
	tbz	w2,3,.mz
	adr	x0,order1h
	b	.mz
.m6:	cmp	w11,6
	b.ne	.m7
	adr	x0,order6
	mov	w1,6
	b	.mz
.m7:	cmp	w11,7
	b.ne	.mz
	adr	x0,order7
	mov	w1,7
.mz:	mov	w2,0				; i
.ol:	ldrb	w3,[x0,x2]			; e
	sub	w4,w1,w2			; dd
	tbz	w3,4,.ob
	and	w5,w3,3
	strb	w4,[x14,x5]
	b	.on
.ob:	strb	w4,[x13,x3]			; dBg [e >> 1][e & 1]
.on:	add	w2,w2,1
	cmp	w2,w1
	b.lo	.ol
	cmp	w11,7
	b.ne	.scr
	ldrb	w0,[x13]
	strb	w0,[x13,1]
.scr:	; les deux ecrans
	add	x0,x19,S_CGRAM
	ldrh	w0,[x0]				; back
	ldrh	w1,[x19,s_fixedColor]
	adm	x2,S_MAINC
	adm	x3,S_SUBC
	adm	x4,S_MAINZ
	mov	w5,256
	movl	x6,0x0505050505050505
	add	x7,x4,S_MAINL-S_MAINZ
.z1:	strh	w0,[x2],2
	strh	w1,[x3],2
	subs	w5,w5,1
	b.ne	.z1
	mov	x5,x4
	mov	w8,256/8
.z2:	str	xzr,[x5,0x100]
	str	xzr,[x5],8
	str	x6,[x7,0x100]
	str	x6,[x7],8
	subs	w8,w8,1
	b.ne	.z2
	mov	w15,0				; s
	ldrb	w22,[x19,s_tm]
	ldrb	w23,[x19,s_tmw]
.scr1:	adm	x24,S_MAINC			; cc
	adm	x25,S_MAINZ			; zz
	adm	x26,S_MAINL			; ll
	cbz	w15,.sn
	ldrb	w22,[x19,s_ts]
	ldrb	w23,[x19,s_tsw]
	add	x24,x24,S_SUBC-S_MAINC
	add	x25,x25,S_SUBZ-S_MAINZ
	add	x26,x26,S_SUBL-S_MAINL
.sn:	mov	w9,0				; n
.lay:	and	w0,w22,w12
	lsr	w0,w0,w9
	tbz	w0,0,.ln
	mov	x10,0				; w
	lsr	w0,w23,w9
	tbz	w0,0,.nw
	mov	w0,w9
	bl	ppu_window
	mov	x10,x0
.nw:	adm	x1,S_BGPRI
	add	x1,x1,x9,lsl 8			; pp
	adm	x2,S_BGCOL
	add	x2,x2,x9,lsl 9			; pc
	add	x3,x13,x9,lsl 1
	ldrb	w4,[x3]				; d0
	ldrb	w5,[x3,1]			; d1
	mov	w6,0				; x
.lx:	ldrb	w7,[x1,x6]
	cmp	w7,0xFF
	b.eq	.lz
	cbz	x10,.lw
	ldrb	w0,[x10,x6]
	cbnz	w0,.lz
.lw:	cmp	w7,0
	csel	w7,w5,w4,ne			; d
	ldrb	w8,[x25,x6]
	cmp	w7,w8
	b.ls	.lz
	strb	w7,[x25,x6]
	ldrh	w7,[x2,x6,lsl 1]
	strh	w7,[x24,x6,lsl 1]
	strb	w9,[x26,x6]
.lz:	add	w6,w6,1
	cmp	w6,256
	b.lo	.lx
.ln:	add	w9,w9,1
	cmp	w9,4
	b.lo	.lay
	tbz	w22,4,.se
	mov	x10,0
	tbz	w23,4,.ow
	mov	w0,4
	bl	ppu_window
	mov	x10,x0
.ow:	adm	x1,S_OBJPRI
	adm	x2,S_OBJCOL
	add	x3,x1,S_OBJMATH-S_OBJPRI
	mov	w6,0
.ox:	ldrb	w7,[x1,x6]
	cmp	w7,0xFF
	b.eq	.oz
	cbz	x10,.o1
	ldrb	w0,[x10,x6]
	cbnz	w0,.oz
.o1:	ldrb	w7,[x14,x7]			; d
	ldrb	w8,[x25,x6]
	cmp	w7,w8
	b.ls	.oz
	strb	w7,[x25,x6]
	ldrh	w7,[x2,x6,lsl 1]
	strh	w7,[x24,x6,lsl 1]
	ldrb	w7,[x3,x6]
	mov	w0,6
	mov	w8,4
	cmp	w7,0
	csel	w0,w8,w0,ne
	strb	w0,[x26,x6]
.oz:	add	w6,w6,1
	cmp	w6,256
	b.lo	.ox
.se:	add	w15,w15,1
	cmp	w15,2
	b.lo	.scr1
	; le calcul de couleur
	mov	w0,5
	bl	ppu_window
	mov	x10,x0				; cw
	ldrb	w1,[x19,s_cgwsel]
	lsr	w12,w1,6			; clipMode
	ubfx	w13,w1,4,2			; prevMode
	ubfx	w14,w1,1,1			; useSub
	ldrb	w15,[x19,s_cgadsub]
	ubfx	w22,w15,6,1			; halfOn
	ldrb	w0,[x19,s_inidisp]
	and	w0,w0,15
	add	x23,x19,S_LUT
	add	x23,x23,x0,lsl 7		; lut
	adm	x24,S_MAINC
	ldr	x25,[sp,8]			; out
	mov	w9,0				; x
.cx:	ldrh	w0,[x24,x9,lsl 1]		; c
	ldrb	w1,[x10,x9]			; in
	lsr	w2,w12,w1
	and	w2,w2,1				; clip
	lsr	w3,w13,w1
	and	w3,w3,1				; noMath
	cbz	w2,.cc
	mov	w0,0
.cc:	cbnz	w3,.pix
	add	x4,x24,S_MAINL-S_MAINC
	ldrb	w4,[x4,x9]			; l
	cmp	w4,6
	b.eq	.pix
	lsr	w5,w15,w4
	tbz	w5,0,.pix
	ldrh	w5,[x19,s_fixedColor]		; o
	mov	w6,w22				; half
	cbz	w2,.h1
	mov	w6,0
.h1:	cbz	w14,.h2
	add	x7,x24,S_SUBC-S_MAINC
	ldrh	w5,[x7,x9,lsl 1]
	add	x7,x24,S_SUBL-S_MAINC
	ldrb	w7,[x7,x9]
	cmp	w7,5
	b.ne	.h2
	mov	w6,0
.h2:	and	w1,w0,31			; r
	ubfx	w2,w0,5,5			; g
	lsr	w3,w0,10			; b
	and	w4,w5,31
	ubfx	w7,w5,5,5
	lsr	w8,w5,10
	tbz	w15,7,.add
	subs	w1,w1,w4
	csel	w1,wzr,w1,lt
	subs	w2,w2,w7
	csel	w2,wzr,w2,lt
	subs	w3,w3,w8
	csel	w3,wzr,w3,lt
	cbz	w6,.mix
	lsr	w1,w1,1
	lsr	w2,w2,1
	lsr	w3,w3,1
	b	.mix
.add:	add	w1,w1,w4
	add	w2,w2,w7
	add	w3,w3,w8
	cbz	w6,.ncl
	lsr	w1,w1,1
	lsr	w2,w2,1
	lsr	w3,w3,1
.ncl:	mov	w4,31
	cmp	w1,31
	csel	w1,w4,w1,hi
	cmp	w2,31
	csel	w2,w4,w2,hi
	cmp	w3,31
	csel	w3,w4,w3,hi
.mix:	orr	w0,w1,w2,lsl 5
	orr	w0,w0,w3,lsl 10
.pix:	and	w1,w0,31
	ldr	w1,[x23,x1,lsl 2]
	ubfx	w2,w0,5,5
	ldr	w2,[x23,x2,lsl 2]
	ubfx	w3,w0,10,5
	ldr	w3,[x23,x3,lsl 2]
	orr	w3,w3,w2,lsl 8
	orr	w3,w3,w1,lsl 16
	str	w3,[x25,x9,lsl 2]
	add	w9,w9,1
	cmp	w9,256
	b.lo	.cx
	add	sp,sp,16
	rest_all
	epilogue
