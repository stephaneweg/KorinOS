; ============================================================================
;  gb_core.inc - le Game Boy et le Game Boy Color : le SM83, le LCD (une ligne a
;  la fois), le minuteur, la manette, OAM / HDMA, les cartouches MBC1, MBC2,
;  MBC3 (horloge) et MBC5, les quatre voix. Ecrit d'apres le coeur C++ d'Onyx
;  (user/Emulators/gb), image pour image : tools/emu/ (docs/EMULATEURS.md).
;
;	gb_load (m, rom, octets)	-> 1 ; 0 : trop courte
;	gb_reset (m)
;	gb_frame (m, pixels, pas, boutons)	une image, 0x00RRGGBB, 160 x 144
;	gb_audio_rate (m, hz), gb_audio_read (m, lr, trames) -> trames
;	gb_set_save (m, donnees, octets), gb_save_poll (m, &pointeur) -> octets
;	m = GB_SIZE octets a zero
;
;  Registres dans le coeur : x19 = m, w20 A, w21 F, w22 BC, w23 DE, w24 HL,
;  w25 PC, w26 SP, w27 cycles, x28 table des codes.
;  gb_read et gb_write gardent x9 et x10 ; le reste de x0-x15 est perdu.
; ============================================================================

FZ		= 0x80
FN		= 0x40
FH		= 0x20
FC		= 0x10
GB_CPU_HZ	= 4194304
GB_W		= 160
GB_H		= 144
GB_ABUF		= 8192

virtual at 0
g_rom:		rb 8
g_romLo:	rb 8
g_romHi:	rb 8
g_rtcCycles:	rb 8
g_sampleAcc:	rb 8
g_romSize:	rb 4
g_romBanks:	rb 4
g_cgb:		rb 4
g_sramSize:	rb 4
g_battery:	rb 4
g_sramDirty:	rb 4
g_mbc:		rb 4
g_romBank:	rb 4
g_ramBank:	rb 4
g_mbc1Mode:	rb 4
g_ramOn:	rb 4
g_rtcReg:	rb 20
g_rtcLatched:	rb 20
g_rtcSel:	rb 4
g_rtcPrimed:	rb 4
g_ie:		rb 4
g_vbank:	rb 4
g_wbank:	rb 4
g_ime:		rb 4
g_halted:	rb 4
g_haltBug:	rb 4
g_eiDelay:	rb 4
g_double:	rb 4
g_div:		rb 4
g_lineDots:	rb 4
g_ly:		rb 4
g_winLine:	rb 4
g_frameDone:	rb 4
g_statLine:	rb 4
g_hdmaActive:	rb 4
g_hdmaLen:	rb 4
g_hdmaSrc:	rb 4
g_hdmaDst:	rb 4
g_buttons:	rb 4
g_frameSeq:	rb 4
g_fsTimer:	rb 4
g_apuPending:	rb 4
g_rate:		rb 4
g_ahead:	rb 4
g_atail:	rb 4
g_regA:		rb 4
g_regF:		rb 4
g_regBC:	rb 4
g_regDE:	rb 4
g_regHL:	rb 4
g_regPC:	rb 4
g_regSP:	rb 4
g_sq:		rb 2*SQ_SIZE
g_wv:		rb WV_SIZE
g_ns:		rb NS_SIZE
g_end:
end virtual

SQ_on		= 0
SQ_dac		= 4
SQ_len		= 8
SQ_duty		= 12
SQ_dutyPos	= 16
SQ_vol		= 20
SQ_envPeriod	= 24
SQ_envTimer	= 28
SQ_envDir	= 32
SQ_freq		= 36
SQ_timer	= 40
SQ_lenOn	= 44
SQ_sweepPeriod	= 48
SQ_sweepTimer	= 52
SQ_sweepShift	= 56
SQ_sweepDir	= 60
SQ_shadow	= 64
SQ_sweepOn	= 68
SQ_SIZE		= 72
WV_on		= 0
WV_dac		= 4
WV_len		= 8
WV_vol		= 12
WV_freq		= 16
WV_timer	= 20
WV_pos		= 24
WV_lenOn	= 28
WV_sample	= 32
WV_SIZE		= 36
NS_on		= 0
NS_dac		= 4
NS_len		= 8
NS_vol		= 12
NS_envPeriod	= 16
NS_envTimer	= 20
NS_envDir	= 24
NS_shift	= 28
NS_width	= 32
NS_div		= 36
NS_timer	= 40
NS_lfsr		= 44
NS_lenOn	= 48
NS_SIZE		= 52

G_MAPR		= 0x300			; 16 pointeurs, par tranche de 4 Ko : adresse + pointeur ; 0 : lent
G_MAPW		= 0x380
G_IO		= 0x400
G_HRAM		= 0x480
G_OAM		= 0x500
G_BGPAL		= 0x5A0
G_OBPAL		= 0x5E0
G_BGRGB		= 0x620
G_OBRGB		= 0x6A0
G_DMG		= 0x720
G_DMGPAL	= 0x730
G_BGIDX		= 0x740
G_BGATTR	= 0x7E0
G_VRAM		= 0x1000
G_WRAM		= 0x5000
G_ABUF		= 0xD000
G_FB		= 0x15000
G_SRAM		= 0x2C000
GB_SIZE		= G_SRAM + 0x20000
assert g_end <= 0x300

macro io_get reg, n {
	ldrb	reg,[x19,G_IO+(n)]
}
macro io_put reg, n {
	strb	reg,[x19,G_IO+(n)]
}

; ---------------------------------------------------------------------------
;  Une image
; ---------------------------------------------------------------------------

; x0 = m, x1 = pixels, x2 = pas, x3 = boutons
gb_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	x1,x2,[sp,-16]!
	mov	x19,x0
	str	w3,[x19,g_buttons]
	ldr	x0,[x19,g_rom]
	cbz	x0,gf_out
	ldr	w20,[x19,g_regA]
	ldr	w21,[x19,g_regF]
	ldr	w22,[x19,g_regBC]
	ldr	w23,[x19,g_regDE]
	ldr	w24,[x19,g_regHL]
	ldr	w25,[x19,g_regPC]
	ldr	w26,[x19,g_regSP]
	adr	x28,gb_optab
	str	wzr,[x19,g_frameDone]
	movl	w15,200000
	str	w15,[sp,-16]!
gb_step:
	ldr	w0,[x19,g_frameDone]
	cbnz	w0,gf_fin
	ldr	w15,[sp]
	subs	w15,w15,1
	b.lo	gf_fin
	str	w15,[sp]
	ldr	w0,[x19,g_ie]
	io_get	w1,0x0F
	and	w0,w0,w1
	ands	w0,w0,0x1F
	b.eq	.nopend
	str	wzr,[x19,g_halted]
	ldr	w1,[x19,g_ime]
	cbz	w1,.nopend
	rbit	w0,w0
	clz	w0,w0				; bit
	mov	w2,1
	lsl	w2,w2,w0
	io_get	w1,0x0F
	bic	w1,w1,w2
	io_put	w1,0x0F
	str	wzr,[x19,g_ime]
	mov	w10,w0
	mov	w0,w25
	bl	gb_push
	lsl	w25,w10,3
	add	w25,w25,0x40
	mov	w27,20
	b	gb_done
.nopend:
	ldr	w0,[x19,g_halted]
	cbz	w0,.run
	bl	halt_skip
	mov	w27,w0
	b	gb_done
.run:	ldr	w0,[x19,g_eiDelay]
	cbz	w0,.go
	subs	w0,w0,1
	str	w0,[x19,g_eiDelay]
	b.ne	.go
	mov	w0,1
	str	w0,[x19,g_ime]
.go:
  if defined GB_TRACE
	bl	gb_trace
  end if
	bl	gb_fetch
	ldrsw	x1,[x28,x0,lsl 2]
	add	x1,x28,x1
	br	x1
gb_done:
	mov	w0,w27
	bl	gb_tick
	b	gb_step
gf_fin:	add	sp,sp,16
	str	w20,[x19,g_regA]
	str	w21,[x19,g_regF]
	str	w22,[x19,g_regBC]
	str	w23,[x19,g_regDE]
	str	w24,[x19,g_regHL]
	str	w25,[x19,g_regPC]
	str	w26,[x19,g_regSP]
	; l'image vers l'appelant
	ldp	x1,x2,[sp]
	add	x0,x19,G_FB
	mov	x3,GB_H
.y:	mov	x4,x1
	mov	x5,GB_W
.x:	ldr	w6,[x0],4
	str	w6,[x4],4
	subs	x5,x5,1
	b.ne	.x
	add	x1,x1,x2
	subs	x3,x3,1
	b.ne	.y
gf_out:	add	sp,sp,16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> w0 = l'octet en PC
gb_fetch:
	mov	w0,w25
	ldr	w1,[x19,g_haltBug]
	cbnz	w1,.b
	add	w25,w25,1
	and	w25,w25,0xFFFF
	b	gb_read
.b:	str	wzr,[x19,g_haltBug]
	b	gb_read

gb_fetch16:
	str	x30,[sp,-16]!
	bl	gb_fetch
	mov	w9,w0
	bl	gb_fetch
	orr	w0,w9,w0,lsl 8
	ldr	x30,[sp],16
	ret

gb_push:				; w0 = mot
	str	x30,[sp,-16]!
	mov	w9,w0
	sub	w26,w26,1
	and	w26,w26,0xFFFF
	mov	w0,w26
	lsr	w1,w9,8
	bl	gb_write
	sub	w26,w26,1
	and	w26,w26,0xFFFF
	mov	w0,w26
	and	w1,w9,0xFF
	bl	gb_write
	ldr	x30,[sp],16
	ret

gb_pop:					; -> w0
	str	x30,[sp,-16]!
	mov	w0,w26
	add	w26,w26,1
	and	w26,w26,0xFFFF
	bl	gb_read
	mov	w9,w0
	mov	w0,w26
	add	w26,w26,1
	and	w26,w26,0xFFFF
	bl	gb_read
	orr	w0,w9,w0,lsl 8
	ldr	x30,[sp],16
	ret

; -> w0 = fin du mode en cours (dots), perd w3, w4
macro bl_next lab {
	ldr	w3,[x19,g_ly]
	mov	w0,456
	cmp	w3,144
	b.hs	lab
	io_get	w3,0x41
	and	w3,w3,3
	mov	w4,80
	cmp	w3,2
	csel	w0,w4,w0,eq
	mov	w4,252
	cmp	w3,3
	csel	w0,w4,w0,eq
}

; HALT : on saute au prochain moment possible d'une interruption -> w0 = cycles
halt_skip:
	ldr	w1,[x19,g_double]
	add	w1,w1,1				; ds
	io_get	w2,0x40
	tbz	w2,7,.off
	bl_next	.a
.a:	ldr	w3,[x19,g_lineDots]
	sub	w0,w0,w3
	mul	w0,w0,w1
	b	.t
.off:	ldr	w3,[x19,g_lineDots]
	movl	w0,70224
	sub	w0,w0,w3
	mul	w0,w0,w1
.t:	io_get	w2,0x07
	tbz	w2,2,.c
	and	w2,w2,3
	adr	x3,tima_bits
	ldrb	w2,[x3,x2]
	mov	w3,2
	lsl	w3,w3,w2			; period
	ldr	w4,[x19,g_div]
	sub	w5,w3,1
	and	w4,w4,w5
	sub	w4,w3,w4
	io_get	w5,0x05
	mov	w6,255
	sub	w5,w6,w5
	mul	w5,w5,w3
	add	w4,w4,w5
	cmp	w4,w0
	csel	w0,w4,w0,lt
.c:	mov	w1,912
	cmp	w0,w1
	csel	w0,w1,w0,gt
	and	w0,w0,0xFFFFFFFC
	mov	w1,4
	cmp	w0,4
	csel	w0,w1,w0,lt
	ret
tima_bits:
	db	9,3,5,7

; ---------------------------------------------------------------------------
;  Le bus
; ---------------------------------------------------------------------------

; w0 = adresse -> w0 (garde x9, x10)
gb_read:
	lsr	w1,w0,12
	add	x2,x19,G_MAPR
	ldr	x1,[x2,x1,lsl 3]
	cbz	x1,.slow
	ldrb	w0,[x1,x0]
	ret
.slow:	stp	x9,x10,[sp,-32]!
	str	x30,[sp,16]
	movl	w1,0xC000
	cmp	w0,w1
	b.hs	.hi
	bl	read_sram
	b	.r
.hi:	movl	w1,0xFE00
	cmp	w0,w1
	b.hs	.h2
	sub	w0,w0,0x2000
	bl	gb_read
	b	.r
.h2:	movl	w1,0xFEA0
	cmp	w0,w1
	b.hs	.h3
	sub	w0,w0,0xE00
	and	w0,w0,0xFF
	add	x1,x19,G_OAM
	ldrb	w0,[x1,x0]
	b	.r
.h3:	movl	w1,0xFF00
	cmp	w0,w1
	b.hs	.h4
	mov	w0,0xFF
	b	.r
.h4:	movl	w1,0xFF80
	cmp	w0,w1
	b.hs	.h5
	and	w0,w0,0x7F
	bl	read_io
	b	.r
.h5:	movl	w1,0xFFFF
	cmp	w0,w1
	b.hs	.ie
	and	w0,w0,0x7F
	add	x1,x19,G_HRAM
	ldrb	w0,[x1,x0]
	b	.r
.ie:	ldr	w0,[x19,g_ie]
.r:	ldr	x30,[sp,16]
	ldp	x9,x10,[sp],32
	ret

; w0 = adresse, w1 = valeur (garde x9, x10)
gb_write:
	lsr	w2,w0,12
	add	x3,x19,G_MAPW
	ldr	x2,[x3,x2,lsl 3]
	cbz	x2,.slow
	strb	w1,[x2,x0]
	ret
.slow:	stp	x9,x10,[sp,-32]!
	str	x30,[sp,16]
	tbnz	w0,15,.a
	bl	write_mbc
	b	.r
.a:	movl	w2,0xC000
	cmp	w0,w2
	b.hs	.hi
	bl	write_sram
	b	.r
.hi:	movl	w2,0xFE00
	cmp	w0,w2
	b.hs	.h2
	sub	w0,w0,0x2000
	bl	gb_write
	b	.r
.h2:	movl	w2,0xFEA0
	cmp	w0,w2
	b.hs	.h3
	and	w0,w0,0xFF
	add	x2,x19,G_OAM
	strb	w1,[x2,x0]
	b	.r
.h3:	movl	w2,0xFF00
	cmp	w0,w2
	b.lo	.r
	movl	w2,0xFF80
	cmp	w0,w2
	b.hs	.h5
	and	w0,w0,0x7F
	bl	write_io
	b	.r
.h5:	movl	w2,0xFFFF
	cmp	w0,w2
	b.hs	.ie
	and	w0,w0,0x7F
	add	x2,x19,G_HRAM
	strb	w1,[x2,x0]
	b	.r
.ie:	str	w1,[x19,g_ie]
.r:	ldr	x30,[sp,16]
	ldp	x9,x10,[sp],32
	ret

; les tranches de 4 Ko directes, d'apres les banques
map_update:
	add	x2,x19,G_MAPR
	add	x3,x19,G_MAPW
	ldr	x0,[x19,g_romLo]
	str	x0,[x2]
	str	x0,[x2,8]
	str	x0,[x2,16]
	str	x0,[x2,24]
	ldr	x0,[x19,g_romHi]
	movl	x1,0x4000
	sub	x0,x0,x1
	str	x0,[x2,32]
	str	x0,[x2,40]
	str	x0,[x2,48]
	str	x0,[x2,56]
	ldr	w0,[x19,g_vbank]
	lsl	x0,x0,13
	add	x0,x0,x19
	add	x0,x0,G_VRAM
	movl	x1,0x8000
	sub	x0,x0,x1
	str	x0,[x2,64]
	str	x0,[x2,72]
	str	x0,[x3,64]
	str	x0,[x3,72]
	str	xzr,[x2,80]			; sauvegarde : lente
	str	xzr,[x2,88]
	add	x0,x19,G_WRAM
	movl	x1,0xC000
	sub	x0,x0,x1
	str	x0,[x2,96]
	str	x0,[x3,96]
	movl	x1,0xE000-0xC000
	sub	x1,x0,x1
	str	x1,[x2,112]			; echo
	str	x1,[x3,112]
	ldr	w0,[x19,g_wbank]
	lsl	x0,x0,12
	add	x0,x0,x19
	add	x0,x0,G_WRAM
	movl	x1,0xD000
	sub	x0,x0,x1
	str	x0,[x2,104]
	str	x0,[x3,104]
	str	xzr,[x2,120]
	str	xzr,[x3,120]
	ret

map_rom:
	ldr	w1,[x19,g_romBanks]
	mov	w3,1
	cmp	w1,0
	csel	w1,w3,w1,eq
	mov	x4,0
	ldr	w2,[x19,g_mbc]
	cmp	w2,1
	b.ne	.lo
	ldr	w2,[x19,g_mbc1Mode]
	cbz	w2,.lo
	ldr	w2,[x19,g_romBank]
	and	w2,w2,0x60
	udiv	w3,w2,w1
	msub	w2,w3,w1,w2
	lsl	x4,x2,14
.lo:	ldr	x0,[x19,g_rom]
	add	x4,x4,x0
	str	x4,[x19,g_romLo]
	ldr	w2,[x19,g_romBank]
	udiv	w3,w2,w1
	msub	w2,w3,w1,w2
	lsl	x4,x2,14
	add	x4,x4,x0
	str	x4,[x19,g_romHi]
	b	map_update

; -> x1 = decalage dans la sauvegarde de l'adresse w0 (A000-BFFF)
macro sram_off {
	ldr	w2,[x19,g_mbc]
	ldr	w3,[x19,g_ramBank]
	cmp	w2,1
	b.ne	.bk
	ldr	w2,[x19,g_mbc1Mode]
	and	w3,w3,3
	cmp	w2,0
	csel	w3,wzr,w3,eq
.bk:	lsl	x3,x3,13
	and	x1,x0,0x1FFF
	add	x1,x1,x3
	ldr	w2,[x19,g_sramSize]
	udiv	x3,x1,x2
	msub	x1,x3,x2,x1
}

read_sram:
	ldr	w1,[x19,g_ramOn]
	cbz	w1,.ff
	ldr	w1,[x19,g_mbc]
	cmp	w1,3
	b.ne	.a
	ldr	w2,[x19,g_rtcSel]
	cbz	w2,.a
	sub	w2,w2,8
	add	x3,x19,g_rtcLatched
	ldr	w0,[x3,x2,lsl 2]
	and	w0,w0,0xFF
	ret
.a:	ldr	w2,[x19,g_sramSize]
	cbz	w2,.ff
	add	x4,x19,G_SRAM
	cmp	w1,2
	b.ne	.b
	and	w0,w0,0x1FF
	ldrb	w0,[x4,x0]
	orr	w0,w0,0xF0
	ret
.b:	sram_off
	ldrb	w0,[x4,x1]
	ret
.ff:	mov	w0,0xFF
	ret

write_sram:				; w0 = adresse, w1 = valeur
	ldr	w2,[x19,g_ramOn]
	cbz	w2,.r
	mov	w5,w1
	ldr	w1,[x19,g_mbc]
	cmp	w1,3
	b.ne	.a
	ldr	w2,[x19,g_rtcSel]
	cbz	w2,.a
	sub	w3,w2,8
	add	x4,x19,g_rtcReg
	str	w5,[x4,x3,lsl 2]
	cmp	w2,8
	b.ne	.r
	str	xzr,[x19,g_rtcCycles]
	ret
.a:	ldr	w2,[x19,g_sramSize]
	cbz	w2,.r
	add	x4,x19,G_SRAM
	cmp	w1,2
	b.ne	.b
	and	w0,w0,0x1FF
	and	w5,w5,0x0F
	strb	w5,[x4,x0]
	b	.d
.b:	sram_off
	strb	w5,[x4,x1]
.d:	mov	w0,1
	str	w0,[x19,g_sramDirty]
.r:	ret

; w0 = adresse, w1 = valeur
write_mbc:
	ldr	w2,[x19,g_mbc]
	cmp	w2,1
	b.eq	.m1
	cmp	w2,2
	b.eq	.m2
	cmp	w2,3
	b.eq	.m3
	cmp	w2,5
	b.eq	.m5
	b	map_rom
.ramen:	and	w1,w1,0x0F
	cmp	w1,0x0A
	cset	w1,eq
	str	w1,[x19,g_ramOn]
	b	map_rom
.m1:	lsr	w3,w0,13			; 0..3
	cbz	w3,.ramen
	cmp	w3,1
	b.ne	.m1a
	ldr	w2,[x19,g_romBank]
	and	w2,w2,0x60
	and	w3,w1,0x1F
	orr	w2,w2,w3
	cbnz	w3,.m1s
	orr	w2,w2,1
.m1s:	str	w2,[x19,g_romBank]
	b	map_rom
.m1a:	cmp	w3,2
	b.ne	.m1b
	and	w3,w1,3
	str	w3,[x19,g_ramBank]
	ldr	w2,[x19,g_romBank]
	and	w2,w2,0x1F
	orr	w2,w2,w3,lsl 5
	str	w2,[x19,g_romBank]
	b	map_rom
.m1b:	and	w1,w1,1
	str	w1,[x19,g_mbc1Mode]
	b	map_rom
.m2:	lsr	w3,w0,14
	cbnz	w3,map_rom
	tbz	w0,8,.ramen
	and	w1,w1,0x0F
	mov	w2,1
	cmp	w1,0
	csel	w1,w2,w1,eq
	str	w1,[x19,g_romBank]
	b	map_rom
.m3:	lsr	w3,w0,13
	cbz	w3,.ramen
	cmp	w3,1
	b.ne	.m3a
	and	w1,w1,0x7F
	mov	w2,1
	cmp	w1,0
	csel	w1,w2,w1,eq
	str	w1,[x19,g_romBank]
	b	map_rom
.m3a:	cmp	w3,2
	b.ne	.m3b
	cmp	w1,3
	b.hi	.m3c
	str	w1,[x19,g_ramBank]
	str	wzr,[x19,g_rtcSel]
	b	map_rom
.m3c:	cmp	w1,8
	b.lo	map_rom
	cmp	w1,12
	b.hi	map_rom
	str	w1,[x19,g_rtcSel]
	b	map_rom
.m3b:	cbnz	w1,.m3d
	mov	w1,1
	str	w1,[x19,g_rtcPrimed]
	b	map_rom
.m3d:	cmp	w1,1
	b.ne	map_rom
	ldr	w2,[x19,g_rtcPrimed]
	cbz	w2,map_rom
	add	x2,x19,g_rtcReg
	add	x3,x19,g_rtcLatched
	mov	x4,0
.m3e:	ldr	w5,[x2,x4,lsl 2]
	str	w5,[x3,x4,lsl 2]
	add	x4,x4,1
	cmp	x4,5
	b.lo	.m3e
	str	wzr,[x19,g_rtcPrimed]
	b	map_rom
.m5:	lsr	w3,w0,13
	cbz	w3,.ramen
	lsr	w3,w0,12
	cmp	w3,2
	b.ne	.m5a
	ldr	w2,[x19,g_romBank]
	and	w2,w2,0x100
	orr	w2,w2,w1
	str	w2,[x19,g_romBank]
	b	map_rom
.m5a:	cmp	w3,3
	b.ne	.m5b
	ldr	w2,[x19,g_romBank]
	and	w2,w2,0xFF
	and	w1,w1,1
	orr	w2,w2,w1,lsl 8
	str	w2,[x19,g_romBank]
	b	map_rom
.m5b:	cmp	w3,6
	b.hs	map_rom
	and	w1,w1,0x0F
	str	w1,[x19,g_ramBank]
	b	map_rom

; w0 = registre (0..0x7F) -> w0
read_io:
	cmp	w0,0x10
	b.lo	.lo
	cmp	w0,0x40
	b.hs	.hi
	str	x30,[sp,-16]!
	mov	w9,w0
	bl	apu_flush
	mov	w0,w9
	bl	apu_read
	ldr	x30,[sp],16
	ret
.lo:	cbz	w0,.joy
	cmp	w0,2
	b.eq	.r2
	cmp	w0,4
	b.eq	.r4
	cmp	w0,7
	b.eq	.r7
	cmp	w0,0x0F
	b.eq	.rF
	b	.dflt
.joy:	io_get	w0,0
	and	w0,w0,0x30			; sel
	mov	w1,0x0F				; lo
	ldr	w2,[x19,g_buttons]
	tbnz	w0,4,.j1
	bic	w1,w1,w2
.j1:	tbnz	w0,5,.j2
	lsr	w2,w2,4
	and	w2,w2,0x0F
	bic	w1,w1,w2
.j2:	and	w1,w1,0x0F
	orr	w0,w0,w1
	orr	w0,w0,0xC0
	ret
.r2:	io_get	w0,2
	mov	w1,0x7E
	orr	w0,w0,w1
	ret
.r4:	ldr	w0,[x19,g_div]
	ubfx	w0,w0,8,8
	ret
.r7:	io_get	w0,7
	orr	w0,w0,0xF8
	ret
.rF:	io_get	w0,0x0F
	orr	w0,w0,0xE0
	ret
.hi:	ldr	w2,[x19,g_cgb]
	cmp	w0,0x41
	b.eq	.r41
	cmp	w0,0x44
	b.eq	.r44
	cmp	w0,0x4D
	b.eq	.r4D
	cmp	w0,0x4F
	b.eq	.r4F
	cmp	w0,0x55
	b.eq	.r55
	cmp	w0,0x68
	b.eq	.r68
	cmp	w0,0x6A
	b.eq	.r68
	cmp	w0,0x69
	b.eq	.r69
	cmp	w0,0x6B
	b.eq	.r6B
	cmp	w0,0x70
	b.eq	.r70
.dflt:	add	x1,x19,G_IO
	ldrb	w0,[x1,x0]
	ret
.r41:	io_get	w0,0x41
	orr	w0,w0,0x80
	ret
.r44:	ldr	w0,[x19,g_ly]
	and	w0,w0,0xFF
	ret
.ff:	mov	w0,0xFF
	ret
.r4D:	cbz	w2,.ff
	ldr	w0,[x19,g_double]
	lsl	w0,w0,7
	io_get	w1,0x4D
	and	w1,w1,1
	orr	w0,w0,w1
	mov	w1,0x7E
	orr	w0,w0,w1
	ret
.r4F:	cbz	w2,.ff
	ldr	w0,[x19,g_vbank]
	orr	w0,w0,0xFE
	ret
.r55:	cbz	w2,.ff
	ldr	w1,[x19,g_hdmaActive]
	cbz	w1,.ff
	ldr	w0,[x19,g_hdmaLen]
	mov	w1,16
	sdiv	w0,w0,w1
	sub	w0,w0,1
	and	w0,w0,0x7F
	ret
.r68:	cbz	w2,.ff
	add	x1,x19,G_IO
	ldrb	w0,[x1,x0]
	orr	w0,w0,0x40
	ret
.r69:	cbz	w2,.ff
	io_get	w0,0x68
	and	w0,w0,0x3F
	add	x1,x19,G_BGPAL
	ldrb	w0,[x1,x0]
	ret
.r6B:	cbz	w2,.ff
	io_get	w0,0x6A
	and	w0,w0,0x3F
	add	x1,x19,G_OBPAL
	ldrb	w0,[x1,x0]
	ret
.r70:	cbz	w2,.ff
	ldr	w0,[x19,g_wbank]
	orr	w0,w0,0xF8
	ret

; x1 = palette, w2 = couleur -> w0 = 0x00RRGGBB (perd w3, w4, w5)
cgb_color:
	add	x3,x1,x2,lsl 1
	ldrh	w3,[x3]
	and	w0,w3,31
	ubfx	w4,w3,5,5
	ubfx	w3,w3,10,5
	lsl	w5,w0,3
	orr	w0,w5,w0,lsr 2
	lsl	w5,w4,3
	orr	w4,w5,w4,lsr 2
	lsl	w5,w3,3
	orr	w3,w5,w3,lsr 2
	lsl	w0,w0,16
	orr	w0,w0,w4,lsl 8
	orr	w0,w0,w3
	ret

; w0 = registre, w1 = valeur
write_io:
	cmp	w0,0x10
	b.lo	.lo
	cmp	w0,0x40
	b.hs	.hi
	str	x30,[sp,-16]!
	mov	w9,w0
	mov	w10,w1
	bl	apu_flush
	mov	w0,w9
	mov	w1,w10
	bl	apu_write
	ldr	x30,[sp],16
	ret
.lo:	cbnz	w0,.l1
	and	w1,w1,0x30
	io_put	w1,0
	ret
.l1:	cmp	w0,1
	b.ne	.l2
	io_put	w1,1
	ret
.l2:	cmp	w0,2
	b.ne	.l4
	io_put	w1,2
	mov	w2,0x81
	and	w2,w1,w2
	cmp	w2,0x81
	b.ne	.r
	mov	w2,0xFF
	io_put	w2,1
	and	w1,w1,0x7F
	io_put	w1,2
	mov	w0,3
	b	gb_irq
.l4:	cmp	w0,4
	b.ne	.lF
	str	wzr,[x19,g_div]
	ret
.lF:	cmp	w0,0x0F
	b.ne	.dflt
	and	w1,w1,0x1F
	io_put	w1,0x0F
	ret
.r:	ret
.dflt:	add	x2,x19,G_IO
	strb	w1,[x2,x0]
	ret
.hi:	cmp	w0,0x40
	b.eq	.w40
	cmp	w0,0x41
	b.eq	.w41
	cmp	w0,0x44
	b.eq	.r
	cmp	w0,0x45
	b.eq	.w45
	cmp	w0,0x46
	b.eq	.w46
	ldr	w2,[x19,g_cgb]
	cmp	w0,0x4D
	b.eq	.w4D
	cmp	w0,0x4F
	b.eq	.w4F
	cmp	w0,0x55
	b.eq	.w55
	cmp	w0,0x69
	b.eq	.w69
	cmp	w0,0x6B
	b.eq	.w6B
	cmp	w0,0x70
	b.eq	.w70
	b	.dflt
.w40:	str	x30,[sp,-16]!
	mov	w9,w1
	io_get	w2,0x40
	tbz	w2,7,.w40a
	tbnz	w1,7,.w40a
	str	wzr,[x19,g_ly]
	str	wzr,[x19,g_lineDots]
	io_get	w3,0x41
	and	w3,w3,0xFC
	io_put	w3,0x41
	str	wzr,[x19,g_winLine]
.w40a:	tbnz	w2,7,.w40b
	tbz	w1,7,.w40b
	str	wzr,[x19,g_ly]
	str	wzr,[x19,g_lineDots]
	mov	w0,2
	bl	set_mode
.w40b:	io_put	w9,0x40
	ldr	x30,[sp],16
	ret
.w41:	io_get	w2,0x41
	and	w2,w2,7
	and	w1,w1,0x78
	orr	w2,w2,w1
	io_put	w2,0x41
	b	check_stat
.w45:	io_put	w1,0x45
	b	check_stat
.w46:	str	x30,[sp,-16]!
	mov	w9,w1
	lsl	w9,w9,8
	mov	w10,0
.dma:	add	w0,w9,w10
	and	w0,w0,0xFFFF
	bl	gb_read
	add	x1,x19,G_OAM
	strb	w0,[x1,x10]
	add	w10,w10,1
	cmp	w10,0xA0
	b.lo	.dma
	lsr	w9,w9,8
	io_put	w9,0x46
	ldr	x30,[sp],16
	ret
.w4D:	cbz	w2,.r
	and	w1,w1,1
	io_put	w1,0x4D
	ret
.w4F:	cbz	w2,.r
	and	w1,w1,1
	str	w1,[x19,g_vbank]
	b	map_update
.w55:	cbz	w2,.r
	ldr	w2,[x19,g_hdmaActive]
	cbz	w2,.w55a
	tbnz	w1,7,.w55a
	str	wzr,[x19,g_hdmaActive]
	ret
.w55a:	io_get	w2,0x51
	io_get	w3,0x52
	orr	w2,w3,w2,lsl 8
	and	w2,w2,0xFFF0
	str	w2,[x19,g_hdmaSrc]
	io_get	w2,0x53
	io_get	w3,0x54
	orr	w2,w3,w2,lsl 8
	and	w2,w2,0x1FF0
	orr	w2,w2,0x8000
	str	w2,[x19,g_hdmaDst]
	and	w2,w1,0x7F
	add	w2,w2,1
	lsl	w2,w2,4
	str	w2,[x19,g_hdmaLen]
	tbz	w1,7,.w55g
	mov	w2,1
	str	w2,[x19,g_hdmaActive]
	ret
.w55g:	str	x30,[sp,-16]!
.w55l:	ldr	w0,[x19,g_hdmaLen]
	cmp	w0,0
	b.le	.w55e
	bl	hdma_block
	b	.w55l
.w55e:	ldr	x30,[sp],16
	ret
.w69:	cbz	w2,.r
	add	x8,x19,G_BGPAL
	add	x7,x19,G_BGRGB
	mov	w6,0x68
	b	.pal
.w6B:	cbz	w2,.r
	add	x8,x19,G_OBPAL
	add	x7,x19,G_OBRGB
	mov	w6,0x6A
.pal:	add	x5,x19,G_IO
	ldrb	w9,[x5,x6]
	and	w4,w9,0x3F			; i
	strb	w1,[x8,x4]
	lsr	w2,w4,1
	mov	x11,x1
	mov	x1,x8
	mov	x12,x30
	mov	x13,x4
	mov	x14,x5
	bl	cgb_color
	mov	x30,x12
	lsr	w2,w13,1
	str	w0,[x7,x2,lsl 2]
	tbz	w9,7,.r
	add	w4,w13,1
	and	w4,w4,0x3F
	orr	w4,w4,0x80
	strb	w4,[x14,x6]
	ret
.w70:	cbz	w2,.r
	and	w1,w1,7
	mov	w2,1
	cmp	w1,0
	csel	w1,w2,w1,eq
	str	w1,[x19,g_wbank]
	b	map_update

gb_irq:					; w0 = bit
	mov	w1,1
	lsl	w1,w1,w0
	io_get	w0,0x0F
	orr	w0,w0,w1
	io_put	w0,0x0F
	ret

hdma_block:				; 16 octets
	stp	x30,x11,[sp,-16]!
	mov	w11,16
.l:	ldr	w0,[x19,g_hdmaSrc]
	bl	gb_read
	ldr	w1,[x19,g_hdmaDst]
	and	w1,w1,0x1FFF
	ldr	w2,[x19,g_vbank]
	lsl	x2,x2,13
	add	x2,x2,x19
	add	x2,x2,G_VRAM
	strb	w0,[x2,x1]
	ldr	w0,[x19,g_hdmaSrc]
	add	w0,w0,1
	and	w0,w0,0xFFFF
	str	w0,[x19,g_hdmaSrc]
	ldr	w0,[x19,g_hdmaDst]
	add	w0,w0,1
	and	w0,w0,0x1FFF
	orr	w0,w0,0x8000
	str	w0,[x19,g_hdmaDst]
	subs	w11,w11,1
	b.ne	.l
	ldr	w0,[x19,g_hdmaLen]
	subs	w0,w0,16
	b.gt	.s
	mov	w0,0
	str	wzr,[x19,g_hdmaActive]
.s:	str	w0,[x19,g_hdmaLen]
	ldp	x30,x11,[sp],16
	ret

; ---------------------------------------------------------------------------
;  Le temps
; ---------------------------------------------------------------------------

; w0 = cycles
gb_tick:
	str	x30,[sp,-16]!
	mov	w13,w0
	; le minuteur
	ldr	w1,[x19,g_div]
	add	w2,w1,w13
	and	w3,w2,0xFFFF
	str	w3,[x19,g_div]
	io_get	w3,0x07
	tbz	w3,2,.dots
	and	w3,w3,3
	adr	x4,tima_bits
	ldrb	w3,[x4,x3]
	add	w3,w3,1
	lsr	w2,w2,w3
	lsr	w1,w1,w3
	subs	w2,w2,w1
	b.eq	.dots
	io_get	w1,0x05
.t:	add	w1,w1,1
	ands	w1,w1,0xFF
	b.ne	.t1
	io_get	w1,0x06
	mov	w0,2
	mov	w3,w1
	mov	w4,w2
	bl	gb_irq
	mov	w1,w3
	mov	w2,w4
.t1:	subs	w2,w2,1
	b.ne	.t
	io_put	w1,0x05
.dots:	ldr	w0,[x19,g_double]
	lsr	w13,w13,w0
	str	x13,[sp,8]
	mov	w0,w13
	bl	ppu_tick
	ldr	x13,[sp,8]
	ldr	w0,[x19,g_apuPending]
	add	w0,w0,w13
	str	w0,[x19,g_apuPending]
	cmp	w0,64
	b.lt	.r
	bl	apu_flush
.r:	ldr	w0,[x19,g_mbc]
	cmp	w0,3
	b.ne	.f
	ldr	x0,[sp,8]
	bl	rtc_tick
.f:	ldr	x30,[sp],16
	ret

rtc_tick:				; w0 = cycles
	ldr	w1,[x19,g_rtcReg+16]
	tbnz	w1,6,.r
	ldr	x1,[x19,g_rtcCycles]
	add	x1,x1,x0
	movl	x2,GB_CPU_HZ
.l:	cmp	x1,x2
	b.lt	.f
	sub	x1,x1,x2
	add	x3,x19,g_rtcReg
	ldr	w4,[x3]
	add	w4,w4,1
	str	w4,[x3]
	cmp	w4,60
	b.lt	.l
	str	wzr,[x3]
	ldr	w4,[x3,4]
	add	w4,w4,1
	str	w4,[x3,4]
	cmp	w4,60
	b.lt	.l
	str	wzr,[x3,4]
	ldr	w4,[x3,8]
	add	w4,w4,1
	str	w4,[x3,8]
	cmp	w4,24
	b.lt	.l
	str	wzr,[x3,8]
	ldr	w4,[x3,12]
	ldr	w5,[x3,16]
	and	w6,w5,1
	orr	w4,w4,w6,lsl 8
	add	w4,w4,1
	cmp	w4,511
	b.le	.d
	mov	w4,0
	orr	w5,w5,0x80
.d:	and	w6,w4,0xFF
	str	w6,[x3,12]
	and	w5,w5,0xFFFFFFFE
	orr	w5,w5,w4,lsr 8
	str	w5,[x3,16]
	b	.l
.f:	str	x1,[x19,g_rtcCycles]
.r:	ret

set_mode:				; w0 = mode
	io_get	w1,0x41
	and	w1,w1,0xFC
	orr	w1,w1,w0
	io_put	w1,0x41
check_stat:
	io_get	w1,0x41
	ldr	w0,[x19,g_ly]
	io_get	w2,0x45
	cmp	w0,w2
	cset	w3,eq				; coinc
	and	w1,w1,0xFFFFFFFB
	orr	w1,w1,w3,lsl 2
	io_put	w1,0x41
	io_get	w0,0x40
	tbnz	w0,7,.on
	str	wzr,[x19,g_statLine]
	ret
.on:	and	w2,w1,3				; mode
	mov	w4,0				; line
	tbz	w1,6,.a
	orr	w4,w4,w3
.a:	tbz	w1,3,.b
	cmp	w2,0
	cset	w5,eq
	orr	w4,w4,w5
.b:	tbz	w1,4,.c
	cmp	w2,1
	cset	w5,eq
	orr	w4,w4,w5
.c:	tbz	w1,5,.d
	cmp	w2,2
	cset	w5,eq
	orr	w4,w4,w5
.d:	ldr	w5,[x19,g_statLine]
	str	w4,[x19,g_statLine]
	cbz	w4,.r
	cbnz	w5,.r
	mov	w0,1
	b	gb_irq
.r:	ret

; w0 = dots
ppu_tick:
	stp	x30,x14,[sp,-16]!
	mov	w14,w0
	io_get	w1,0x40
	tbnz	w1,7,.l
	ldr	w1,[x19,g_lineDots]
	add	w1,w1,w14
	movl	w2,70224
	cmp	w1,w2
	b.lt	.s
	sub	w1,w1,w2
	mov	w2,1
	str	w2,[x19,g_frameDone]
.s:	str	w1,[x19,g_lineDots]
	b	.out
.l:	cmp	w14,0
	b.le	.out
	bl_next	.n1
.n1:	mov	w5,w0				; next
	ldr	w1,[x19,g_lineDots]
	sub	w2,w5,w1			; n
	cmp	w2,w14
	csel	w2,w14,w2,gt
	add	w1,w1,w2
	str	w1,[x19,g_lineDots]
	sub	w14,w14,w2
	cmp	w1,w5
	b.lt	.out
	ldr	w3,[x19,g_ly]
	cmp	w3,144
	b.lo	.vis
	str	wzr,[x19,g_lineDots]
	add	w3,w3,1
	str	w3,[x19,g_ly]
	cmp	w3,153
	b.ls	.st
	str	wzr,[x19,g_ly]
	str	wzr,[x19,g_winLine]
	mov	w0,2
	bl	set_mode
	b	.l
.st:	bl	check_stat
	b	.l
.vis:	io_get	w4,0x41
	and	w4,w4,3
	cmp	w4,2
	b.ne	.m3
	mov	w0,3
	bl	set_mode
	b	.l
.m3:	cmp	w4,3
	b.ne	.m0
	str	x14,[sp,-16]!
	bl	render_line
	ldr	x14,[sp],16
	mov	w0,0
	bl	set_mode
	ldr	w0,[x19,g_hdmaActive]
	cbz	w0,.l
	bl	hdma_block
	b	.l
.m0:	str	wzr,[x19,g_lineDots]
	add	w3,w3,1
	str	w3,[x19,g_ly]
	cmp	w3,144
	b.ne	.m2
	mov	w0,1
	bl	set_mode
	mov	w0,0
	bl	gb_irq
	mov	w0,1
	str	w0,[x19,g_frameDone]
	b	.l
.m2:	mov	w0,2
	bl	set_mode
	b	.l
.out:	ldp	x30,x14,[sp],16
	ret

; ---------------------------------------------------------------------------
;  Une ligne de l'image
; ---------------------------------------------------------------------------

; x0 = sortie, w1 = x0, w2 = x1, w3 = mapBase, w4 = px, w5 = py, w6 = lcdc
draw_bg:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	mov	x20,x0
	mov	w21,w1				; x
	mov	w22,w2				; x1
	lsr	w0,w5,3
	add	w23,w3,w0,lsl 5			; rowBase
	mov	w24,w4				; px
	mov	w25,w5				; py
	mov	w26,w6				; lcdc
	add	x27,x19,G_VRAM
	ldr	w15,[x19,g_cgb]
	add	x13,x19,G_BGIDX
	add	x14,x19,G_BGATTR
.t:	cmp	w21,w22
	b.ge	.out
	and	w24,w24,255
	add	w0,w23,w24,lsr 3		; mi
	ldrb	w1,[x27,x0]			; tile
	mov	w2,0				; attr
	cbz	w15,.a
	add	x3,x27,0x2000
	ldrb	w2,[x3,x0]
.a:	and	w3,w25,7			; row
	tbz	w2,6,.b
	mov	w4,7
	sub	w3,w4,w3
.b:	tbz	w26,4,.s
	lsl	w4,w1,4
	b	.c
.s:	sxtb	w4,w1
	lsl	w4,w4,4
	add	w4,w4,0x1000
.c:	add	w4,w4,w3,lsl 1			; addr
	mov	x5,x27
	tbz	w2,3,.d
	add	x5,x5,0x2000
.d:	add	x5,x5,w4,sxtw
	ldrb	w6,[x5]				; lo
	ldrb	w7,[x5,1]			; hi
	tbz	w2,5,.e
	rbit	w6,w6
	lsr	w6,w6,24
	rbit	w7,w7
	lsr	w7,w7,24
.e:	cbz	w15,.dp
	and	w3,w2,7
	add	x8,x19,G_BGRGB
	add	x8,x8,x3,lsl 4			; pal
	b	.p
.dp:	add	x8,x19,G_DMGPAL
.p:	and	w3,w24,7			; col
	mov	w9,8
	sub	w9,w9,w3			; n
	sub	w10,w22,w21
	cmp	w9,w10
	csel	w9,w10,w9,gt
	mov	w10,0				; k
.k:	add	w11,w3,w10
	mov	w12,7
	sub	w11,w12,w11			; bit
	lsr	w12,w6,w11
	and	w12,w12,1
	lsr	w0,w7,w11
	and	w0,w0,1
	orr	w12,w12,w0,lsl 1		; ci
	add	w11,w21,w10
	strb	w12,[x13,x11]
	strb	w2,[x14,x11]
	ldr	w12,[x8,x12,lsl 2]
	str	w12,[x20,x11,lsl 2]
	add	w10,w10,1
	cmp	w10,w9
	b.lt	.k
	add	w21,w21,w9
	add	w24,w24,w9
	b	.t
.out:	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

render_line:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	sub	sp,sp,64			; sel, order
	io_get	w20,0x40			; lcdc
	ldr	w21,[x19,g_ly]
	ldr	w22,[x19,g_cgb]
	mov	w0,GB_W
	mul	w0,w0,w21
	add	x23,x19,G_FB
	add	x23,x23,x0,lsl 2		; out
	cbnz	w22,.bg
	tbnz	w20,0,.bg
	ldr	w2,[x19,G_DMG]
	add	x3,x19,G_BGIDX
	add	x4,x19,G_BGATTR
	mov	x1,0
.b0:	strb	wzr,[x3,x1]
	strb	wzr,[x4,x1]
	str	w2,[x23,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,GB_W
	b.lo	.b0
	b	.spr
.bg:	mov	w26,0				; winOn
	tbz	w20,5,.w1
	io_get	w0,0x4A
	cmp	w0,w21
	b.hi	.w1
	io_get	w0,0x4B
	cmp	w0,166
	b.hi	.w1
	cbnz	w22,.w0
	tbz	w20,0,.w1
.w0:	mov	w26,1
.w1:	io_get	w25,0x4B
	sub	w25,w25,7			; wx
	io_get	w0,0x47
	add	x1,x19,G_DMG
	add	x2,x19,G_DMGPAL
	mov	x3,0
.dp:	and	w4,w0,3
	ldr	w4,[x1,x4,lsl 2]
	str	w4,[x2,x3,lsl 2]
	lsr	w0,w0,2
	add	x3,x3,1
	cmp	x3,4
	b.lo	.dp
	mov	w24,GB_W			; split
	cbz	w26,.sp
	cmp	w25,GB_W
	b.ge	.sp
	cmp	w25,0
	csel	w24,w25,wzr,gt
.sp:	cmp	w24,0
	b.le	.nosplit
	mov	x0,x23
	mov	w1,0
	mov	w2,w24
	mov	w3,0x1800
	tbz	w20,3,.m1
	mov	w3,0x1C00
.m1:	io_get	w4,0x43
	io_get	w5,0x42
	add	w5,w5,w21
	and	w5,w5,255
	mov	w6,w20
	bl	draw_bg
.nosplit:
	cmp	w24,GB_W
	b.ge	.spr
	mov	x0,x23
	mov	w1,w24
	mov	w2,GB_W
	mov	w3,0x1800
	tbz	w20,6,.m2
	mov	w3,0x1C00
.m2:	sub	w4,w24,w25
	ldr	w5,[x19,g_winLine]
	mov	w6,w20
	bl	draw_bg
	ldr	w0,[x19,g_winLine]
	add	w0,w0,1
	str	w0,[x19,g_winLine]
.spr:	tbz	w20,1,.out
	mov	w24,8				; hgt
	tbz	w20,2,.h
	mov	w24,16
.h:	add	x25,x19,G_OAM
	mov	w26,0				; ns
	mov	w0,0
.sel:	add	x1,x25,x0,lsl 2
	ldrb	w1,[x1]
	sub	w1,w1,16
	cmp	w21,w1
	b.lt	.seln
	add	w1,w1,w24
	cmp	w21,w1
	b.ge	.seln
	str	w0,[sp,x26,lsl 2]
	add	w26,w26,1
.seln:	add	w0,w0,1
	cmp	w0,40
	b.hs	.ord
	cmp	w26,10
	b.lo	.sel
.ord:	cbnz	w22,.draw
	mov	w0,1				; DMG : X croissant, puis l'ordre de l'OAM
.is:	cmp	w0,w26
	b.hs	.draw
	ldr	w1,[sp,x0,lsl 2]		; k
	add	x2,x25,x1,lsl 2
	ldrb	w2,[x2,1]			; x de k
	mov	w3,w0				; j
.is1:	cbz	w3,.is2
	sub	w4,w3,1
	ldr	w5,[sp,x4,lsl 2]
	add	x6,x25,x5,lsl 2
	ldrb	w6,[x6,1]
	cmp	w6,w2
	b.ls	.is2
	str	w5,[sp,x3,lsl 2]
	mov	w3,w4
	b	.is1
.is2:	str	w1,[sp,x3,lsl 2]
	add	w0,w0,1
	b	.is
.draw:	sub	w27,w26,1
.n:	tbnz	w27,31,.out
	ldr	w0,[sp,x27,lsl 2]
	add	x0,x25,x0,lsl 2
	ldrb	w1,[x0]
	sub	w1,w1,16			; sy
	ldrb	w2,[x0,1]
	sub	w2,w2,8				; sx
	ldrb	w3,[x0,2]			; tile
	ldrb	w4,[x0,3]			; at
	cmp	w24,16
	b.ne	.t8
	and	w3,w3,0xFE
.t8:	sub	w5,w21,w1			; row
	tbz	w4,6,.nf
	sub	w5,w24,w5
	sub	w5,w5,1
.nf:	add	x6,x19,G_VRAM
	cbz	w22,.b1
	tbz	w4,3,.b1
	add	x6,x6,0x2000
.b1:	lsl	w7,w3,4
	add	w7,w7,w5,lsl 1
	add	x6,x6,x7
	ldrb	w7,[x6]				; lo
	ldrb	w8,[x6,1]			; hi
	mov	w9,0				; col
.c:	add	w10,w2,w9			; x
	cmp	w10,GB_W
	b.hs	.cn				; (< 0 aussi)
	mov	w11,7
	sub	w11,w11,w9
	tst	w4,0x20
	csel	w11,w9,w11,ne			; bit
	lsr	w12,w7,w11
	and	w12,w12,1
	lsr	w13,w8,w11
	and	w13,w13,1
	orr	w12,w12,w13,lsl 1		; ci
	cbz	w12,.cn
	add	x13,x19,G_BGIDX
	ldrb	w13,[x13,x10]
	cbz	w22,.dmg
	tbz	w20,0,.cg
	cbz	w13,.cg
	tbnz	w4,7,.cn
	add	x14,x19,G_BGATTR
	ldrb	w14,[x14,x10]
	tbnz	w14,7,.cn
.cg:	and	w14,w4,7
	add	w14,w12,w14,lsl 2
	add	x13,x19,G_OBRGB
	ldr	w14,[x13,x14,lsl 2]
	str	w14,[x23,x10,lsl 2]
	b	.cn
.dmg:	tbz	w4,7,.d1
	cbnz	w13,.cn
.d1:	io_get	w13,0x48
	tbz	w4,4,.d2
	io_get	w13,0x49
.d2:	lsl	w12,w12,1
	lsr	w13,w13,w12
	and	w13,w13,3
	add	x14,x19,G_DMG
	ldr	w13,[x14,x13,lsl 2]
	str	w13,[x23,x10,lsl 2]
.cn:	add	w9,w9,1
	cmp	w9,8
	b.lo	.c
	sub	w27,w27,1
	b	.n
.out:	add	sp,sp,64
	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Le son
; ---------------------------------------------------------------------------

apu_mask:
	db 0x80,0x3F,0x00,0xFF,0xBF,0xFF,0x3F,0x00,0xFF,0xBF,0x7F,0xFF,0x9F,0xFF,0xBF,0xFF
	db 0xFF,0x00,0x00,0xBF,0x00,0x00,0x70,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
	db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
duty_tab:
	db 0x01,0x81,0x87,0x7E

apu_read:				; w0 = registre -> w0
	cmp	w0,0x26
	b.ne	.m
	io_get	w0,0x26
	and	w0,w0,0x80
	orr	w0,w0,0x70
	ldr	w1,[x19,g_sq+SQ_on]
	orr	w0,w0,w1
	ldr	w1,[x19,g_sq+SQ_SIZE+SQ_on]
	orr	w0,w0,w1,lsl 1
	ldr	w1,[x19,g_wv+WV_on]
	orr	w0,w0,w1,lsl 2
	ldr	w1,[x19,g_ns+NS_on]
	orr	w0,w0,w1,lsl 3
	ret
.m:	add	x1,x19,G_IO
	ldrb	w1,[x1,x0]
	sub	w0,w0,0x10
	adr	x2,apu_mask
	ldrb	w0,[x2,x0]
	orr	w0,w0,w1
	ret

; x1 = bloc, w2 = NRx2 : volume et enveloppe (decalages Vol, EnvPeriod, EnvDir, EnvTimer)
macro env_load vl, ep, ed, et {
	lsr	w3,w2,4
	str	w3,[x1,vl]
	and	w3,w2,7
	str	w3,[x1,ep]
	str	w3,[x1,et]
	mov	w4,1
	mov	w5,-1
	tst	w2,8
	csel	w4,w4,w5,ne
	str	w4,[x1,ed]
}

trigger:				; w0 = voie
	cmp	w0,2
	b.hs	.hi
	mov	w3,SQ_SIZE
	mul	w3,w3,w0
	add	x1,x19,g_sq
	add	x1,x1,x3
	mov	w4,0x12
	mov	w3,0x17
	cmp	w0,0
	csel	w4,w3,w4,ne			; NRx2
	mov	w6,w0
	ldr	w3,[x1,SQ_dac]
	str	w3,[x1,SQ_on]
	ldr	w3,[x1,SQ_len]
	cbnz	w3,.l1
	mov	w3,64
	str	w3,[x1,SQ_len]
.l1:	ldr	w3,[x1,SQ_freq]
	mov	w5,2048
	sub	w3,w5,w3
	lsl	w3,w3,2
	str	w3,[x1,SQ_timer]
	add	x2,x19,G_IO
	ldrb	w2,[x2,x4]
	env_load SQ_vol,SQ_envPeriod,SQ_envDir,SQ_envTimer
	cbnz	w6,.r
	ldr	w3,[x1,SQ_freq]
	str	w3,[x1,SQ_shadow]
	io_get	w2,0x10
	ubfx	w3,w2,4,3
	str	w3,[x1,SQ_sweepPeriod]
	mov	w4,1
	mov	w5,-1
	tst	w2,8
	csel	w4,w5,w4,ne
	str	w4,[x1,SQ_sweepDir]
	and	w5,w2,7
	str	w5,[x1,SQ_sweepShift]
	mov	w6,8
	cmp	w3,0
	csel	w6,w3,w6,ne
	str	w6,[x1,SQ_sweepTimer]
	orr	w6,w3,w5
	cmp	w6,0
	cset	w6,ne
	str	w6,[x1,SQ_sweepOn]
	cbz	w5,.r
	ldr	w3,[x1,SQ_shadow]
	lsr	w6,w3,w5
	mul	w6,w6,w4
	add	w3,w3,w6
	cmp	w3,2047
	b.le	.r
	str	wzr,[x1,SQ_on]
.r:	ret
.hi:	b.ne	.n
	add	x1,x19,g_wv
	ldr	w3,[x1,WV_dac]
	str	w3,[x1,WV_on]
	ldr	w3,[x1,WV_len]
	cbnz	w3,.w1
	mov	w3,256
	str	w3,[x1,WV_len]
.w1:	ldr	w3,[x1,WV_freq]
	mov	w5,2048
	sub	w3,w5,w3
	lsl	w3,w3,1
	str	w3,[x1,WV_timer]
	str	wzr,[x1,WV_pos]
	ret
.n:	add	x1,x19,g_ns
	ldr	w3,[x1,NS_dac]
	str	w3,[x1,NS_on]
	ldr	w3,[x1,NS_len]
	cbnz	w3,.n1
	mov	w3,64
	str	w3,[x1,NS_len]
.n1:	mov	w3,0x7FFF
	str	w3,[x1,NS_lfsr]
	io_get	w2,0x21
	env_load NS_vol,NS_envPeriod,NS_envDir,NS_envTimer
	ret

; x0 = debut, x1 = octets
gb_zero:
	add	x1,x0,x1
.l:	cmp	x0,x1
	b.hs	.f
	strb	wzr,[x0],1
	b	.l
.f:	ret

; w0 = registre, w1 = valeur
apu_write:
	add	x2,x19,G_IO
	cmp	w0,0x30
	b.lo	.a
	strb	w1,[x2,x0]
	ret
.a:	cmp	w0,0x26
	b.ne	.b
	tbnz	w1,7,.on
	stp	x30,x1,[sp,-16]!
	add	x0,x2,0x10
	mov	x1,0x16
	bl	gb_zero
	add	x0,x19,g_sq
	mov	x1,2*SQ_SIZE+WV_SIZE+NS_SIZE
	bl	gb_zero
	mov	w0,0x7FFF
	str	w0,[x19,g_ns+NS_lfsr]
	ldp	x30,x1,[sp],16
.on:	and	w1,w1,0x80
	io_put	w1,0x26
	ret
.b:	io_get	w3,0x26
	tbz	w3,7,.r
	strb	w1,[x2,x0]
	add	x4,x19,g_sq
	add	x5,x4,SQ_SIZE
	add	x6,x19,g_wv
	add	x7,x19,g_ns
	cmp	w0,0x11
	b.ne	.c16
	mov	x3,x4
.nr1:	lsr	w2,w1,6
	str	w2,[x3,SQ_duty]
	and	w2,w1,63
	mov	w8,64
	sub	w2,w8,w2
	str	w2,[x3,SQ_len]
	ret
.c16:	cmp	w0,0x16
	b.ne	.c12
	mov	x3,x5
	b	.nr1
.c12:	cmp	w0,0x12
	b.ne	.c17
	mov	x3,x4
.nr2:	tst	w1,0xF8
	cset	w2,ne
	str	w2,[x3,SQ_dac]
	cbnz	w2,.r
	str	wzr,[x3,SQ_on]
.r:	ret
.c17:	cmp	w0,0x17
	b.ne	.c13
	mov	x3,x5
	b	.nr2
.c13:	cmp	w0,0x13
	b.ne	.c18
	mov	x3,x4
.nr3:	ldr	w2,[x3,SQ_freq]
	and	w2,w2,0x700
	orr	w2,w2,w1
	str	w2,[x3,SQ_freq]
	ret
.c18:	cmp	w0,0x18
	b.ne	.c14
	mov	x3,x5
	b	.nr3
.c14:	cmp	w0,0x14
	b.ne	.c19
	mov	x3,x4
	mov	w0,0
.nr4:	ldr	w2,[x3,SQ_freq]
	and	w2,w2,0xFF
	and	w8,w1,7
	orr	w2,w2,w8,lsl 8
	str	w2,[x3,SQ_freq]
	ubfx	w2,w1,6,1
	str	w2,[x3,SQ_lenOn]
	tbz	w1,7,.r
	b	trigger
.c19:	cmp	w0,0x19
	b.ne	.c1A
	mov	x3,x5
	mov	w0,1
	b	.nr4
.c1A:	cmp	w0,0x1A
	b.ne	.c1B
	ubfx	w2,w1,7,1
	str	w2,[x6,WV_dac]
	cbnz	w2,.r
	str	wzr,[x6,WV_on]
	ret
.c1B:	cmp	w0,0x1B
	b.ne	.c1C
	mov	w2,256
	sub	w2,w2,w1
	str	w2,[x6,WV_len]
	ret
.c1C:	cmp	w0,0x1C
	b.ne	.c1D
	ubfx	w2,w1,5,2
	str	w2,[x6,WV_vol]
	ret
.c1D:	cmp	w0,0x1D
	b.ne	.c1E
	ldr	w2,[x6,WV_freq]
	and	w2,w2,0x700
	orr	w2,w2,w1
	str	w2,[x6,WV_freq]
	ret
.c1E:	cmp	w0,0x1E
	b.ne	.c20
	ldr	w2,[x6,WV_freq]
	and	w2,w2,0xFF
	and	w8,w1,7
	orr	w2,w2,w8,lsl 8
	str	w2,[x6,WV_freq]
	ubfx	w2,w1,6,1
	str	w2,[x6,WV_lenOn]
	tbz	w1,7,.r
	mov	w0,2
	b	trigger
.c20:	cmp	w0,0x20
	b.ne	.c21
	and	w2,w1,63
	mov	w8,64
	sub	w2,w8,w2
	str	w2,[x7,NS_len]
	ret
.c21:	cmp	w0,0x21
	b.ne	.c22
	tst	w1,0xF8
	cset	w2,ne
	str	w2,[x7,NS_dac]
	cbnz	w2,.r
	str	wzr,[x7,NS_on]
	ret
.c22:	cmp	w0,0x22
	b.ne	.c23
	lsr	w2,w1,4
	str	w2,[x7,NS_shift]
	ubfx	w2,w1,3,1
	str	w2,[x7,NS_width]
	and	w2,w1,7
	str	w2,[x7,NS_div]
	ret
.c23:	cmp	w0,0x23
	b.ne	.r
	ubfx	w2,w1,6,1
	str	w2,[x7,NS_lenOn]
	tbz	w1,7,.r
	mov	w0,3
	b	trigger

; w1 = bloc : decompte de la duree (decalages)
macro len_step ln, lo, on {
	local z
	ldr	w2,[x1,lo]
	cbz	w2,z
	ldr	w2,[x1,ln]
	cmp	w2,0
	b.le	z
	subs	w2,w2,1
	str	w2,[x1,ln]
	b.ne	z
	str	wzr,[x1,on]
z:
}
macro env_step ep, et, vl, ed {
	local z
	ldr	w2,[x1,ep]
	cbz	w2,z
	ldr	w3,[x1,et]
	subs	w3,w3,1
	str	w3,[x1,et]
	b.gt	z
	str	w2,[x1,et]
	ldr	w3,[x1,vl]
	ldr	w4,[x1,ed]
	add	w3,w3,w4
	cmp	w3,15
	b.hi	z
	str	w3,[x1,vl]
z:
}

frame_sequencer:
	ldr	w6,[x19,g_frameSeq]
	add	w0,w6,1
	and	w0,w0,7
	str	w0,[x19,g_frameSeq]
	tbnz	w6,0,fsq_a
	add	x1,x19,g_sq
	len_step SQ_len,SQ_lenOn,SQ_on
	add	x1,x1,SQ_SIZE
	len_step SQ_len,SQ_lenOn,SQ_on
	add	x1,x19,g_wv
	len_step WV_len,WV_lenOn,WV_on
	add	x1,x19,g_ns
	len_step NS_len,NS_lenOn,NS_on
fsq_a:	cmp	w6,2
	b.eq	fsq_sw
	cmp	w6,6
	b.ne	fsq_e
fsq_sw:	add	x1,x19,g_sq
	ldr	w2,[x1,SQ_sweepOn]
	cbz	w2,fsq_e
	ldr	w2,[x1,SQ_sweepTimer]
	subs	w2,w2,1
	str	w2,[x1,SQ_sweepTimer]
	b.gt	fsq_e
	ldr	w3,[x1,SQ_sweepPeriod]
	mov	w2,8
	cmp	w3,0
	csel	w2,w3,w2,ne
	str	w2,[x1,SQ_sweepTimer]
	cbz	w3,fsq_e
	ldr	w2,[x1,SQ_shadow]
	ldr	w3,[x1,SQ_sweepShift]
	ldr	w4,[x1,SQ_sweepDir]
	lsr	w5,w2,w3
	mul	w5,w5,w4
	add	w2,w2,w5			; nf
	cmp	w2,2047
	b.le	fsq_s1
	str	wzr,[x1,SQ_on]
	b	fsq_e
fsq_s1:	cbz	w3,fsq_e
	str	w2,[x1,SQ_shadow]
	str	w2,[x1,SQ_freq]
	lsr	w5,w2,w3
	add	w5,w5,w2
	cmp	w5,2047
	b.le	fsq_e
	cmp	w4,0
	b.le	fsq_e
	str	wzr,[x1,SQ_on]
fsq_e:	cmp	w6,7
	b.ne	fsq_r
	add	x1,x19,g_sq
	env_step SQ_envPeriod,SQ_envTimer,SQ_vol,SQ_envDir
	add	x1,x1,SQ_SIZE
	env_step SQ_envPeriod,SQ_envTimer,SQ_vol,SQ_envDir
	add	x1,x19,g_ns
	env_step NS_envPeriod,NS_envTimer,NS_vol,NS_envDir
fsq_r:	ret

; w8 = 1 si la voie sonne (DAC) ; le melange d'un echantillon
mix_sample:
	add	x1,x19,g_sq
	mov	x6,0				; voie
	mov	w7,0				; gauche
	mov	w8,0				; droite
	io_get	w9,0x25				; pan
	adr	x10,duty_tab
.sq:	ldr	w2,[x1,SQ_dac]
	mov	w3,0
	cbz	w2,.sq1
	ldr	w4,[x1,SQ_on]
	cbz	w4,.sq1
	ldr	w4,[x1,SQ_duty]
	ldrb	w4,[x10,x4]
	ldr	w5,[x1,SQ_dutyPos]
	mov	w3,7
	sub	w5,w3,w5
	lsr	w4,w4,w5
	mov	w3,0
	tbz	w4,0,.sq1
	ldr	w3,[x1,SQ_vol]
.sq1:	bl	mix_one
	add	x1,x1,SQ_SIZE
	add	x6,x6,1
	cmp	x6,2
	b.lo	.sq
	add	x1,x19,g_wv
	ldr	w2,[x1,WV_dac]
	mov	w3,0
	cbz	w2,.w
	ldr	w4,[x1,WV_on]
	cbz	w4,.w
	ldr	w4,[x1,WV_vol]
	cbz	w4,.w
	ldr	w3,[x1,WV_sample]
	sub	w4,w4,1
	lsr	w3,w3,w4
.w:	bl	mix_one
	add	x6,x6,1
	add	x1,x19,g_ns
	ldr	w2,[x1,NS_dac]
	mov	w3,0
	cbz	w2,.n
	ldr	w4,[x1,NS_on]
	cbz	w4,.n
	ldr	w4,[x1,NS_lfsr]
	tbnz	w4,0,.n
	ldr	w3,[x1,NS_vol]
.n:	bl	mix_one
	io_get	w0,0x24
	ubfx	w1,w0,4,3
	add	w1,w1,1
	mul	w7,w7,w1
	and	w1,w0,7
	add	w1,w1,1
	mul	w8,w8,w1
	ldr	w1,[x19,g_atail]
	add	w2,w1,1
	and	w2,w2,GB_ABUF-1
	ldr	w3,[x19,g_ahead]
	cmp	w2,w3
	b.eq	.f
	add	x3,x19,G_ABUF
	add	x3,x3,x1,lsl 2
	lsl	w7,w7,6
	lsl	w8,w8,6
	strh	w7,[x3]
	strh	w8,[x3,2]
	str	w2,[x19,g_atail]
.f:	ldp	x30,x10,[sp],16
	ret

; w3 = sortie de la voie x6, w2 = son DAC
mix_one:
	lsl	w3,w3,1
	sub	w3,w3,15
	cmp	w2,0
	csel	w3,wzr,w3,eq
	mov	w4,0x10
	lsl	w4,w4,w6
	tst	w9,w4
	b.eq	.a
	add	w7,w7,w3
.a:	lsr	w4,w4,4
	tst	w9,w4
	b.eq	.b
	add	w8,w8,w3
.b:	ret

apu_flush:
	stp	x30,x10,[sp,-16]!
	stp	x9,x11,[sp,-16]!
.l:	ldr	w0,[x19,g_apuPending]
	cmp	w0,0
	b.le	.f
	ldr	x1,[x19,g_sampleAcc]
	ldr	w2,[x19,g_rate]
	movl	x3,GB_CPU_HZ
	sub	x1,x3,x1
	add	x1,x1,x2
	sub	x1,x1,1
	sdiv	x1,x1,x2			; need
	cmp	x1,1
	b.ge	.a
	mov	w1,1
	b	.b
.a:	cmp	x1,x0
	csel	x1,x0,x1,ge
.b:	sub	w0,w0,w1
	str	w0,[x19,g_apuPending]
	mov	w0,w1
	bl	apu_tick
	b	.l
.f:	ldp	x9,x11,[sp],16
	ldp	x30,x10,[sp],16
	ret

; w0 = cycles
apu_tick:
	stp	x30,x14,[sp,-16]!
	mov	w14,w0
	io_get	w1,0x26
	tbz	w1,7,.smp
	add	x1,x19,g_sq
	mov	x6,2
.sq:	ldr	w2,[x1,SQ_timer]
	subs	w2,w2,w14
	b.gt	.s1
	ldr	w3,[x1,SQ_freq]
	mov	w4,2048
	sub	w3,w4,w3
	lsl	w3,w3,2
	ldr	w4,[x1,SQ_dutyPos]
.s0:	add	w2,w2,w3
	add	w4,w4,1
	and	w4,w4,7
	cmp	w2,0
	b.le	.s0
	str	w4,[x1,SQ_dutyPos]
.s1:	str	w2,[x1,SQ_timer]
	add	x1,x1,SQ_SIZE
	subs	x6,x6,1
	b.ne	.sq
	add	x1,x19,g_wv
	ldr	w2,[x1,WV_timer]
	subs	w2,w2,w14
	b.gt	.w1
	ldr	w3,[x1,WV_freq]
	mov	w4,2048
	sub	w3,w4,w3
	lsl	w3,w3,1
	ldr	w4,[x1,WV_pos]
.w0:	add	w2,w2,w3
	add	w4,w4,1
	and	w4,w4,31
	lsr	w5,w4,1
	add	x6,x19,G_IO+0x30
	ldrb	w5,[x6,x5]
	tst	w4,1
	and	w6,w5,0x0F
	lsr	w5,w5,4
	csel	w5,w6,w5,ne
	str	w5,[x1,WV_sample]
	cmp	w2,0
	b.le	.w0
	str	w4,[x1,WV_pos]
.w1:	str	w2,[x1,WV_timer]
	add	x1,x19,g_ns
	ldr	w2,[x1,NS_timer]
	subs	w2,w2,w14
	b.gt	.n1
	ldr	w3,[x1,NS_div]
	mov	w4,8
	lsl	w3,w3,4
	cmp	w3,0
	csel	w3,w4,w3,eq
	ldr	w4,[x1,NS_shift]
	lsl	w3,w3,w4
	ldr	w4,[x1,NS_lfsr]
	ldr	w5,[x1,NS_width]
.n0:	add	w2,w2,w3
	eor	w6,w4,w4,lsr 1
	and	w6,w6,1
	lsr	w4,w4,1
	orr	w4,w4,w6,lsl 14
	cbz	w5,.n2
	and	w4,w4,0xFFFFFFBF
	orr	w4,w4,w6,lsl 6
.n2:	cmp	w2,0
	b.le	.n0
	str	w4,[x1,NS_lfsr]
.n1:	str	w2,[x1,NS_timer]
	ldr	w0,[x19,g_fsTimer]
	add	w0,w0,w14
.fs:	cmp	w0,8192
	b.lt	.fs1
	sub	w0,w0,8192
	str	w0,[x19,g_fsTimer]
	bl	frame_sequencer
	ldr	w0,[x19,g_fsTimer]
	b	.fs
.fs1:	str	w0,[x19,g_fsTimer]
.smp:	ldr	x0,[x19,g_sampleAcc]
	ldr	w1,[x19,g_rate]
	mul	x1,x14,x1
	add	x0,x0,x1
	str	x0,[x19,g_sampleAcc]
.ms:	ldr	x0,[x19,g_sampleAcc]
	movl	x1,GB_CPU_HZ
	cmp	x0,x1
	b.lt	.f
	sub	x0,x0,x1
	str	x0,[x19,g_sampleAcc]
	bl	mix_call
	b	.ms
.f:	ldp	x30,x14,[sp],16
	ret

mix_call:
	stp	x30,x10,[sp,-16]!
	b	mix_sample

; ---------------------------------------------------------------------------
;  Chargement
; ---------------------------------------------------------------------------

gb_ram_sizes:
	dw	0,2048,8192,32768,131072,65536

; x0 = m, x1 = rom, x2 = octets -> 1 / 0
gb_load:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	x2,0x8000
	b.lt	.bad
	cbz	x1,.bad
	str	x1,[x19,g_rom]
	str	w2,[x19,g_romSize]
	lsr	x3,x2,14
	str	w3,[x19,g_romBanks]
	ldrb	w3,[x1,0x143]
	ubfx	w3,w3,7,1
	str	w3,[x19,g_cgb]
	ldrb	w3,[x1,0x147]			; t
	mov	w4,0				; mbc
	sub	w5,w3,1
	cmp	w5,2
	b.hi	.a
	mov	w4,1
	b	.d
.a:	sub	w5,w3,5
	cmp	w5,1
	b.hi	.b
	mov	w4,2
	b	.d
.b:	sub	w5,w3,0x0F
	cmp	w5,4
	b.hi	.c
	mov	w4,3
	b	.d
.c:	sub	w5,w3,0x19
	cmp	w5,5
	b.hi	.d
	mov	w4,5
.d:	str	w4,[x19,g_mbc]
	adr	x5,gb_batt
	mov	w6,0
.e:	ldrb	w7,[x5],1
	cbz	w7,.f
	cmp	w7,w3
	b.ne	.e
	mov	w6,1
.f:	str	w6,[x19,g_battery]
	ldrb	w3,[x1,0x149]
	mov	w5,0
	cmp	w3,6
	b.hs	.g
	adr	x5,gb_ram_sizes
	ldr	w5,[x5,x3,lsl 2]
.g:	cmp	w4,2
	b.ne	.h
	mov	w5,512
.h:	str	w5,[x19,g_sramSize]
	add	x0,x19,G_SRAM
	mov	w1,0xFF
.i:	cbz	w5,.j
	strb	w1,[x0],1
	sub	w5,w5,1
	b	.i
.j:	mov	x0,x19
	bl	gb_reset
	mov	x0,1
	b	.out
.bad:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

gb_batt:
	db	3,6,9,0x0D,0x0F,0x10,0x13,0x1B,0x1E,0
	align	4
gb_wave_init:
	db	0x84,0x40,0x43,0xAA,0x2D,0x78,0x92,0x3C,0x60,0x59,0x59,0xB0,0x34,0xB8,0x2E,0xDA
gb_regs:				; A F BC DE HL : DMG, CGB
	dw	0x01,0xB0,0x0013,0x00D8,0x014D
	dw	0x11,0x80,0x0000,0xFF56,0x000D

gb_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	; les couleurs du DMG (la machine neuve : vert)
	add	x1,x19,G_DMG
	movl	w0,0xE0F8D0
	str	w0,[x1]
	movl	w0,0x88C070
	str	w0,[x1,4]
	movl	w0,0x346856
	str	w0,[x1,8]
	movl	w0,0x081820
	str	w0,[x1,12]
	add	x0,x19,G_VRAM
	movl	x1,0x4000+0x8000
	bl	gb_zero
	add	x0,x19,G_IO
	mov	x1,0x1A0
	bl	gb_zero
	str	wzr,[x19,g_ie]
	str	wzr,[x19,g_vbank]
	mov	w0,1
	str	w0,[x19,g_wbank]
	str	w0,[x19,g_romBank]
	str	wzr,[x19,g_ramBank]
	str	wzr,[x19,g_mbc1Mode]
	str	wzr,[x19,g_ramOn]
	bl	map_rom
	add	x0,x19,g_rtcReg
	mov	x1,48
	bl	gb_zero
	str	xzr,[x19,g_rtcCycles]
	ldr	w0,[x19,g_cgb]
	adr	x1,gb_regs
	mov	w2,20
	mul	w2,w2,w0
	add	x1,x1,x2
	ldp	w2,w3,[x1]
	str	w2,[x19,g_regA]
	str	w3,[x19,g_regF]
	ldp	w2,w3,[x1,8]
	str	w2,[x19,g_regBC]
	str	w3,[x19,g_regDE]
	ldr	w2,[x1,16]
	str	w2,[x19,g_regHL]
	movl	w0,0xFFFE
	str	w0,[x19,g_regSP]
	mov	w0,0x100
	str	w0,[x19,g_regPC]
	add	x0,x19,g_ime
	add	x1,x19,g_div
	sub	x1,x1,x0
	bl	gb_zero
	movl	w0,0xABCC
	str	w0,[x19,g_div]
	add	x0,x19,g_lineDots
	add	x1,x19,g_frameSeq
	sub	x1,x1,x0
	bl	gb_zero
	mov	w1,0xCF
	io_put	w1,0x00
	mov	w1,0x91
	io_put	w1,0x40
	mov	w1,0x85
	io_put	w1,0x41
	mov	w1,0xFC
	io_put	w1,0x47
	mov	w1,0xFF
	io_put	w1,0x48
	io_put	w1,0x49
	mov	w1,0xE1
	io_put	w1,0x0F
	add	x0,x19,G_BGPAL
	mov	x1,0
	mov	w2,0xFF
.p:	strb	w2,[x0,x1]
	add	x1,x1,1
	cmp	x1,128
	b.lo	.p
	add	x0,x19,G_BGRGB
	movl	w2,0xFFFFFF
	mov	x1,0
.q:	str	w2,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,64
	b.lo	.q
	str	wzr,[x19,g_apuPending]
	add	x0,x19,g_sq
	mov	x1,2*SQ_SIZE+WV_SIZE+NS_SIZE
	bl	gb_zero
	mov	w0,0x7FFF
	str	w0,[x19,g_ns+NS_lfsr]
	str	wzr,[x19,g_frameSeq]
	str	wzr,[x19,g_fsTimer]
	str	xzr,[x19,g_sampleAcc]
	mov	w1,0xF1
	io_put	w1,0x26
	mov	w1,0x77
	io_put	w1,0x24
	mov	w1,0xF3
	io_put	w1,0x25
	adr	x0,gb_wave_init
	add	x1,x19,G_IO+0x30
	ldp	x2,x3,[x0]
	stp	x2,x3,[x1]
	ldr	w0,[x19,g_cgb]
	movl	w2,0xFFFFFF
	ldr	w1,[x19,G_DMG]
	cmp	w0,0
	csel	w2,w2,w1,ne
	add	x0,x19,G_FB
	movl	x1,GB_W*GB_H
.f:	str	w2,[x0],4
	subs	x1,x1,1
	b.ne	.f
	ldp	x19,x20,[sp],16
	epilogue

gb_audio_rate:
	movl	w2,48000
	movl	w3,8000
	cmp	w1,w3
	csel	w1,w1,w2,gt
	str	w1,[x0,g_rate]
	ret

gb_audio_read:				; x0 = m, x1 = lr, x2 = trames au plus -> trames
	ldr	w3,[x0,g_ahead]
	ldr	w4,[x0,g_atail]
	mov	x5,0
	add	x6,x0,G_ABUF
.l:	cmp	w3,w4
	b.eq	.f
	cmp	x5,x2
	b.hs	.f
	ldr	w7,[x6,x3,lsl 2]
	str	w7,[x1],4
	add	w3,w3,1
	and	w3,w3,GB_ABUF-1
	add	x5,x5,1
	b	.l
.f:	str	w3,[x0,g_ahead]
	mov	x0,x5
	ret

gb_set_save:				; x0 = m, x1 = donnees, x2 = octets
	ldr	w3,[x0,g_sramSize]
	cmp	x2,x3
	csel	x2,x3,x2,hi
	add	x3,x0,G_SRAM
	mov	x4,0
.l:	cmp	x4,x2
	b.hs	.f
	ldrb	w5,[x1,x4]
	strb	w5,[x3,x4]
	add	x4,x4,1
	b	.l
.f:	str	wzr,[x0,g_sramDirty]
	ret

gb_save_poll:				; x0 = m, x1 = &pointeur -> octets (0 : rien de neuf)
	ldr	w2,[x0,g_sramDirty]
	ldr	w3,[x0,g_battery]
	and	w2,w2,w3
	cbz	w2,.n
	str	wzr,[x0,g_sramDirty]
	add	x2,x0,G_SRAM
	str	x2,[x1]
	ldr	w0,[x0,g_sramSize]
	ret
.n:	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  Les instructions
; ---------------------------------------------------------------------------

macro getr i {			; -> w0
  if (i) = 0
	lsr	w0,w22,8
  else if (i) = 1
	and	w0,w22,0xFF
  else if (i) = 2
	lsr	w0,w23,8
  else if (i) = 3
	and	w0,w23,0xFF
  else if (i) = 4
	lsr	w0,w24,8
  else if (i) = 5
	and	w0,w24,0xFF
  else if (i) = 6
	mov	w0,w24
	bl	gb_read
  else
	mov	w0,w20
  end if
}
macro setr i, reg {
  if (i) = 0
	bfi	w22,reg,8,8
  else if (i) = 1
	bfxil	w22,reg,0,8
  else if (i) = 2
	bfi	w23,reg,8,8
  else if (i) = 3
	bfxil	w23,reg,0,8
  else if (i) = 4
	bfi	w24,reg,8,8
  else if (i) = 5
	bfxil	w24,reg,0,8
  else if (i) = 6
	mov	w1,reg
	mov	w0,w24
	bl	gb_write
  else
	and	w20,reg,0xFF
  end if
}
macro fin n {
	mov	w27,n
	b	gb_done
}
macro alu_call k {
  if (k) = 0
	bl	alu_add
  else if (k) = 1
	bl	alu_adc
  else if (k) = 2
	bl	alu_sub
  else if (k) = 3
	bl	alu_sbc
  else if (k) = 4
	bl	alu_and
  else if (k) = 5
	bl	alu_xor
  else if (k) = 6
	bl	alu_or
  else
	bl	alu_cp
  end if
}

macro flagz reg {
	tst	reg,0xFF
	cset	w21,eq
	lsl	w21,w21,7
}
macro bl_cp {
	subs	w1,w20,w0
	flagz	w1
	orr	w21,w21,FN
	and	w2,w20,0xF
	and	w3,w0,0xF
	cmp	w2,w3
	cset	w3,lo
	orr	w21,w21,w3,lsl 5
	cmp	w1,0
	cset	w3,lt
	orr	w21,w21,w3,lsl 4
}
alu_add:
	and	w2,w20,0xF
	and	w3,w0,0xF
	add	w2,w2,w3
	add	w1,w20,w0
	flagz	w1
	cmp	w2,0xF
	cset	w3,hi
	orr	w21,w21,w3,lsl 5
	cmp	w1,0xFF
	cset	w3,hi
	orr	w21,w21,w3,lsl 4
	and	w20,w1,0xFF
	ret
alu_adc:
	ubfx	w4,w21,4,1
	and	w2,w20,0xF
	and	w3,w0,0xF
	add	w2,w2,w3
	add	w2,w2,w4
	add	w1,w20,w0
	add	w1,w1,w4
	flagz	w1
	cmp	w2,0xF
	cset	w3,hi
	orr	w21,w21,w3,lsl 5
	cmp	w1,0xFF
	cset	w3,hi
	orr	w21,w21,w3,lsl 4
	and	w20,w1,0xFF
	ret
alu_sub:
	bl_cp
	and	w20,w1,0xFF
	ret
alu_cp:
	bl_cp
	ret
alu_sbc:
	ubfx	w4,w21,4,1
	sub	w1,w20,w0
	sub	w1,w1,w4
	flagz	w1
	orr	w21,w21,FN
	and	w2,w20,0xF
	and	w3,w0,0xF
	add	w3,w3,w4
	cmp	w2,w3
	cset	w3,lo
	orr	w21,w21,w3,lsl 5
	cmp	w1,0
	cset	w3,lt
	orr	w21,w21,w3,lsl 4
	and	w20,w1,0xFF
	ret
alu_and:
	and	w20,w20,w0
	flagz	w20
	orr	w21,w21,FH
	ret
alu_xor:
	eor	w20,w20,w0
	flagz	w20
	ret
alu_or:
	orr	w20,w20,w0
	flagz	w20
	ret

do_inc:					; w0 -> w0
	and	w1,w0,0xF
	add	w0,w0,1
	and	w0,w0,0xFF
	and	w21,w21,FC
	tst	w0,0xFF
	cset	w2,eq
	orr	w21,w21,w2,lsl 7
	cmp	w1,0xF
	cset	w2,eq
	orr	w21,w21,w2,lsl 5
	ret
do_dec:
	and	w1,w0,0xF
	sub	w0,w0,1
	and	w0,w0,0xFF
	and	w21,w21,FC
	orr	w21,w21,FN
	tst	w0,0xFF
	cset	w2,eq
	orr	w21,w21,w2,lsl 7
	cmp	w1,0
	cset	w2,eq
	orr	w21,w21,w2,lsl 5
	ret
add_hl:					; w0 = mot
	and	w1,w24,0xFFF
	and	w2,w0,0xFFF
	add	w1,w1,w2
	add	w2,w24,w0
	and	w21,w21,FZ
	cmp	w1,0xFFF
	cset	w1,hi
	orr	w21,w21,w1,lsl 5
	cmp	w2,0x10,lsl 12
	cset	w1,hs
	orr	w21,w21,w1,lsl 4
	and	w24,w2,0xFFFF
	ret
add_sp:					; w0 = e8 -> w0 = sp + e8
	sxtb	w3,w0
	and	w1,w26,0xF
	and	w2,w0,0xF
	add	w1,w1,w2
	cmp	w1,0xF
	cset	w1,hi
	lsl	w21,w1,5
	and	w1,w26,0xFF
	add	w1,w1,w0
	cmp	w1,0xFF
	cset	w1,hi
	orr	w21,w21,w1,lsl 4
	add	w0,w26,w3
	and	w0,w0,0xFFFF
	ret

; cond cc -> eq : la condition est fausse
macro cc_false k, lab {
  if (k) = 0
	tbnz	w21,7,lab
  else if (k) = 1
	tbz	w21,7,lab
  else if (k) = 2
	tbnz	w21,4,lab
  else
	tbz	w21,4,lab
  end if
}

macro op_gen n {
  if (n) >= 0x40 & (n) < 0x80
   if (n) = 0x76
	ldr	w1,[x19,g_ime]
	mov	w2,1
	cbnz	w1,.h
	ldr	w0,[x19,g_ie]
	io_get	w1,0x0F
	and	w0,w0,w1
	tst	w0,0x1F
	b.eq	.h
	str	w2,[x19,g_haltBug]
	fin	4
.h:	str	w2,[x19,g_halted]
	fin	4
   else
	getr	(n) and 7
	setr	((n) shr 3) and 7,w0
    if (((n) shr 3) and 7) = 6 | ((n) and 7) = 6
	fin	8
    else
	fin	4
    end if
   end if
  else if (n) >= 0x80 & (n) < 0xC0
	getr	(n) and 7
	alu_call ((n) shr 3) and 7
   if ((n) and 7) = 6
	fin	8
   else
	fin	4
   end if
  else if (n) = 0x00
	fin	4
  else if (n) = 0x01
	bl	gb_fetch16
	mov	w22,w0
	fin	12
  else if (n) = 0x11
	bl	gb_fetch16
	mov	w23,w0
	fin	12
  else if (n) = 0x21
	bl	gb_fetch16
	mov	w24,w0
	fin	12
  else if (n) = 0x31
	bl	gb_fetch16
	mov	w26,w0
	fin	12
  else if (n) = 0x02
	mov	w0,w22
	mov	w1,w20
	bl	gb_write
	fin	8
  else if (n) = 0x12
	mov	w0,w23
	mov	w1,w20
	bl	gb_write
	fin	8
  else if (n) = 0x22
	mov	w0,w24
	mov	w1,w20
	bl	gb_write
	add	w24,w24,1
	and	w24,w24,0xFFFF
	fin	8
  else if (n) = 0x32
	mov	w0,w24
	mov	w1,w20
	bl	gb_write
	sub	w24,w24,1
	and	w24,w24,0xFFFF
	fin	8
  else if (n) = 0x0A
	mov	w0,w22
	bl	gb_read
	mov	w20,w0
	fin	8
  else if (n) = 0x1A
	mov	w0,w23
	bl	gb_read
	mov	w20,w0
	fin	8
  else if (n) = 0x2A
	mov	w0,w24
	bl	gb_read
	mov	w20,w0
	add	w24,w24,1
	and	w24,w24,0xFFFF
	fin	8
  else if (n) = 0x3A
	mov	w0,w24
	bl	gb_read
	mov	w20,w0
	sub	w24,w24,1
	and	w24,w24,0xFFFF
	fin	8
  else if (n) = 0x03
	add	w22,w22,1
	and	w22,w22,0xFFFF
	fin	8
  else if (n) = 0x13
	add	w23,w23,1
	and	w23,w23,0xFFFF
	fin	8
  else if (n) = 0x23
	add	w24,w24,1
	and	w24,w24,0xFFFF
	fin	8
  else if (n) = 0x33
	add	w26,w26,1
	and	w26,w26,0xFFFF
	fin	8
  else if (n) = 0x0B
	sub	w22,w22,1
	and	w22,w22,0xFFFF
	fin	8
  else if (n) = 0x1B
	sub	w23,w23,1
	and	w23,w23,0xFFFF
	fin	8
  else if (n) = 0x2B
	sub	w24,w24,1
	and	w24,w24,0xFFFF
	fin	8
  else if (n) = 0x3B
	sub	w26,w26,1
	and	w26,w26,0xFFFF
	fin	8
  else if ((n) and 0xC7) = 0x04
	getr	((n) shr 3) and 7
	bl	do_inc
	setr	((n) shr 3) and 7,w0
   if (((n) shr 3) and 7) = 6
	fin	12
   else
	fin	4
   end if
  else if ((n) and 0xC7) = 0x05
	getr	((n) shr 3) and 7
	bl	do_dec
	setr	((n) shr 3) and 7,w0
   if (((n) shr 3) and 7) = 6
	fin	12
   else
	fin	4
   end if
  else if ((n) and 0xC7) = 0x06
	bl	gb_fetch
	setr	((n) shr 3) and 7,w0
   if (((n) shr 3) and 7) = 6
	fin	12
   else
	fin	8
   end if
  else if (n) = 0x07
	ubfx	w1,w20,7,1
	orr	w20,w1,w20,lsl 1
	and	w20,w20,0xFF
	lsl	w21,w1,4
	fin	4
  else if (n) = 0x0F
	and	w1,w20,1
	orr	w20,w20,w1,lsl 8
	lsr	w20,w20,1
	lsl	w21,w1,4
	fin	4
  else if (n) = 0x17
	ubfx	w1,w20,7,1
	ubfx	w2,w21,4,1
	orr	w20,w2,w20,lsl 1
	and	w20,w20,0xFF
	lsl	w21,w1,4
	fin	4
  else if (n) = 0x1F
	and	w1,w20,1
	and	w2,w21,FC
	lsl	w2,w2,3
	orr	w20,w2,w20,lsr 1
	lsl	w21,w1,4
	fin	4
  else if (n) = 0x08
	bl	gb_fetch16
	mov	w9,w0
	and	w1,w26,0xFF
	bl	gb_write
	add	w0,w9,1
	and	w0,w0,0xFFFF
	lsr	w1,w26,8
	bl	gb_write
	fin	20
  else if (n) = 0x09
	mov	w0,w22
	bl	add_hl
	fin	8
  else if (n) = 0x19
	mov	w0,w23
	bl	add_hl
	fin	8
  else if (n) = 0x29
	mov	w0,w24
	bl	add_hl
	fin	8
  else if (n) = 0x39
	mov	w0,w26
	bl	add_hl
	fin	8
  else if (n) = 0x10
	bl	gb_fetch
	ldr	w0,[x19,g_cgb]
	cbz	w0,.s
	io_get	w1,0x4D
	tbz	w1,0,.s
	ldr	w0,[x19,g_double]
	eor	w0,w0,1
	str	w0,[x19,g_double]
	io_put	wzr,0x4D
	str	wzr,[x19,g_div]
.s:	fin	4
  else if (n) = 0x18
	bl	gb_fetch
	sxtb	w0,w0
	add	w25,w25,w0
	and	w25,w25,0xFFFF
	fin	12
  else if ((n) and 0xE7) = 0x20
	bl	gb_fetch
	sxtb	w0,w0
	cc_false ((n) shr 3) and 3,.nt
	add	w25,w25,w0
	and	w25,w25,0xFFFF
	fin	12
.nt:	fin	8
  else if (n) = 0x27
	mov	w0,w20
	tbz	w21,6,.pl
	tbz	w21,5,.m1
	sub	w0,w0,6
	and	w0,w0,0xFF
.m1:	tbz	w21,4,.d
	sub	w0,w0,0x60
	b	.d
.pl:	tbnz	w21,5,.p1
	and	w1,w0,0xF
	cmp	w1,9
	b.ls	.p2
.p1:	add	w0,w0,6
.p2:	tbnz	w21,4,.p3
	cmp	w0,0x9F
	b.le	.d
.p3:	add	w0,w0,0x60
.d:	mov	w1,FN or FC
	and	w1,w21,w1
	cmp	w0,0xFF
	b.le	.nc
	orr	w1,w1,FC
.nc:	and	w20,w0,0xFF
	mov	w21,w1
	tst	w20,0xFF
	cset	w1,eq
	orr	w21,w21,w1,lsl 7
	fin	4
  else if (n) = 0x2F
	mvn	w20,w20
	and	w20,w20,0xFF
	orr	w21,w21,FN or FH
	fin	4
  else if (n) = 0x37
	and	w21,w21,FZ
	orr	w21,w21,FC
	fin	4
  else if (n) = 0x3F
	and	w1,w21,FC
	eor	w1,w1,FC
	and	w21,w21,FZ
	orr	w21,w21,w1
	fin	4
  else if ((n) and 0xE7) = 0xC0
	cc_false ((n) shr 3) and 3,.nt
	bl	gb_pop
	mov	w25,w0
	fin	20
.nt:	fin	8
  else if (n) = 0xC9
	bl	gb_pop
	mov	w25,w0
	fin	16
  else if (n) = 0xD9
	bl	gb_pop
	mov	w25,w0
	mov	w0,1
	str	w0,[x19,g_ime]
	fin	16
  else if ((n) and 0xE7) = 0xC2
	bl	gb_fetch16
	cc_false ((n) shr 3) and 3,.nt
	mov	w25,w0
	fin	16
.nt:	fin	12
  else if (n) = 0xC3
	bl	gb_fetch16
	mov	w25,w0
	fin	16
  else if (n) = 0xE9
	mov	w25,w24
	fin	4
  else if ((n) and 0xE7) = 0xC4
	bl	gb_fetch16
	cc_false ((n) shr 3) and 3,.nt
	mov	w10,w0
	mov	w0,w25
	bl	gb_push
	mov	w25,w10
	fin	24
.nt:	fin	12
  else if (n) = 0xCD
	bl	gb_fetch16
	mov	w10,w0
	mov	w0,w25
	bl	gb_push
	mov	w25,w10
	fin	24
  else if ((n) and 0xC7) = 0xC7
	mov	w0,w25
	bl	gb_push
	mov	w25,(n) and 0x38
	fin	16
  else if (n) = 0xC1
	bl	gb_pop
	mov	w22,w0
	fin	12
  else if (n) = 0xD1
	bl	gb_pop
	mov	w23,w0
	fin	12
  else if (n) = 0xE1
	bl	gb_pop
	mov	w24,w0
	fin	12
  else if (n) = 0xF1
	bl	gb_pop
	lsr	w20,w0,8
	and	w21,w0,0xF0
	fin	12
  else if (n) = 0xC5
	mov	w0,w22
	bl	gb_push
	fin	16
  else if (n) = 0xD5
	mov	w0,w23
	bl	gb_push
	fin	16
  else if (n) = 0xE5
	mov	w0,w24
	bl	gb_push
	fin	16
  else if (n) = 0xF5
	orr	w0,w21,w20,lsl 8
	bl	gb_push
	fin	16
  else if ((n) and 0xC7) = 0xC6
	bl	gb_fetch
	alu_call ((n) shr 3) and 7
	fin	8
  else if (n) = 0xCB
	bl	gb_fetch
	adr	x1,gb_cbtab
	ldrsw	x2,[x1,x0,lsl 2]
	add	x1,x1,x2
	br	x1
  else if (n) = 0xE0
	bl	gb_fetch
	movl	w1,0xFF00
	orr	w0,w0,w1
	mov	w1,w20
	bl	gb_write
	fin	12
  else if (n) = 0xF0
	bl	gb_fetch
	movl	w1,0xFF00
	orr	w0,w0,w1
	bl	gb_read
	mov	w20,w0
	fin	12
  else if (n) = 0xE2
	and	w0,w22,0xFF
	movl	w1,0xFF00
	orr	w0,w0,w1
	mov	w1,w20
	bl	gb_write
	fin	8
  else if (n) = 0xF2
	and	w0,w22,0xFF
	movl	w1,0xFF00
	orr	w0,w0,w1
	bl	gb_read
	mov	w20,w0
	fin	8
  else if (n) = 0xEA
	bl	gb_fetch16
	mov	w1,w20
	bl	gb_write
	fin	16
  else if (n) = 0xFA
	bl	gb_fetch16
	bl	gb_read
	mov	w20,w0
	fin	16
  else if (n) = 0xE8
	bl	gb_fetch
	bl	add_sp
	mov	w26,w0
	fin	16
  else if (n) = 0xF8
	bl	gb_fetch
	bl	add_sp
	mov	w24,w0
	fin	12
  else if (n) = 0xF9
	mov	w26,w24
	fin	8
  else if (n) = 0xF3
	str	wzr,[x19,g_ime]
	str	wzr,[x19,g_eiDelay]
	fin	4
  else if (n) = 0xFB
	ldr	w0,[x19,g_ime]
	cbnz	w0,.e
	ldr	w0,[x19,g_eiDelay]
	cbnz	w0,.e
	mov	w0,2
	str	w0,[x19,g_eiDelay]
.e:	fin	4
  else
	fin	4
  end if
}

; w0 = v -> w9 = res, w10 = retenue (0, 1)
macro cb_rot y {
  if (y) = 0
	ubfx	w10,w0,7,1
	orr	w9,w10,w0,lsl 1
  else if (y) = 1
	and	w10,w0,1
	orr	w9,w0,w10,lsl 8
	lsr	w9,w9,1
  else if (y) = 2
	ubfx	w10,w0,7,1
	ubfx	w1,w21,4,1
	orr	w9,w1,w0,lsl 1
  else if (y) = 3
	and	w10,w0,1
	ubfx	w1,w21,4,1
	orr	w9,w0,w1,lsl 8
	lsr	w9,w9,1
  else if (y) = 4
	ubfx	w10,w0,7,1
	lsl	w9,w0,1
  else if (y) = 5
	and	w10,w0,1
	and	w1,w0,0x80
	orr	w9,w1,w0,lsr 1
  else if (y) = 6
	mov	w10,0
	lsr	w1,w0,4
	orr	w9,w1,w0,lsl 4
  else
	and	w10,w0,1
	lsr	w9,w0,1
  end if
	and	w9,w9,0xFF
	tst	w9,0xFF
	cset	w21,eq
	lsl	w21,w21,7
	orr	w21,w21,w10,lsl 4
}

macro cb_gen n {
  local r, x, y
  r = (n) and 7
  x = (n) shr 6
  y = ((n) shr 3) and 7
	getr	r
  if x = 0
	cb_rot	y
	setr	r,w9
  else if x = 1
	lsr	w1,w0,y
	and	w1,w1,1
	eor	w1,w1,1
	and	w21,w21,FC
	orr	w21,w21,FH
	orr	w21,w21,w1,lsl 7
  else if x = 2
	and	w9,w0,(not (1 shl y)) and 0xFFFFFFFF
	setr	r,w9
  else
	orr	w9,w0,1 shl y
	setr	r,w9
  end if
  if r = 6
   if x = 1
	fin	12
   else
	fin	16
   end if
  else
	fin	8
  end if
}

	align	4
gb_optab:
rept 256 i:0 {
	dw	gop_#i - gb_optab
}
gb_cbtab:
rept 256 i:0 {
	dw	gcb_#i - gb_cbtab
}
rept 256 i:0 {
gop_#i:
	op_gen	i
}
rept 256 i:0 {
gcb_#i:
	cb_gen	i
}
