; ============================================================================
;  snes_core.inc - la Super Nintendo : le 5A22 (65C816, DMA, HDMA, multiplieur,
;  diviseur, minuteur H/V, NMI, IRQ, manette automatique), le PPU une ligne a
;  la fois, le SPC700, le S-DSP, les cartouches LoROM et HiROM. NTSC et PAL.
;  Ecrit d'apres le coeur C++ d'Onyx (user/Emulators/snes), image pour image :
;  tools/emu/ (docs/EMULATEURS.md).
;
;	snes_load (m, rom, octets)	-> 1 ; 0 : pas une ROM, ou une puce absente
;	snes_reset (m)
;	snes_frame (m, pixels, pas, boutons)	une image, 0x00RRGGBB, 256 x 239
;	snes_audio_rate (m, hz), snes_audio_read (m, lr, trames) -> trames
;	snes_set_save (m, donnees, octets), snes_save_poll (m, &pointeur) -> octets
;	m = SNES_SIZE octets a zero
;
;  Dans le coeur : x19 = m, x20 = horloge maitre, et pour le 65C816 w21 A,
;  w22 X, w23 Y, w24 S, w25 PB:PC, w26 E:P (E au bit 8), w27 DB:D, x28 = rdPage.
;  Le bus (lecture, ecriture, registres, DMA) perd x0-x8, x16, x17 et garde
;  x9-x15. apu_sync et les fonctions de ligne gardent tout ce qu'ils touchent.
; ============================================================================

SNES_W		= 256
SNES_H		= 239
SNES_ABUF	= 8192
SNES_SRAM_MAX	= 0x20000

virtual at 0
s_now:		rb 8
s_lineStart:	rb 8
s_irqAt:	rb 8
s_ev:		rb 8
s_spcCycles:	rb 8
s_rom:		rb 8
s_romSize:	rb 4
s_sramSize:	rb 4
s_height:	rb 4
s_frames:	rb 4
s_line:		rb 4
s_evStep:	rb 4
s_rate:		rb 4
s_resAcc:	rb 4
s_ahead:	rb 4
s_atail:	rb 4
s_dspClock:	rb 4
s_dspCounter:	rb 4
s_echoOffset:	rb 4
s_echoLength:	rb 4
s_echoHistPos:	rb 4
s_wmAddr:	rb 4
s_A:		rb 4
s_X:		rb 4
s_Y:		rb 4
s_S:		rb 4
s_PC:		rb 4			; PB:PC
s_P:		rb 4			; E:P
s_D:		rb 4			; DB:D
s_spcA:		rb 4
s_spcX:		rb 4
s_spcY:		rb 4
s_spcS:		rb 4
s_spcPC:	rb 4
s_spcP:		rb 4
s_nmiPending:	rb 1			; ces quatre : un seul mot, lu a chaque instruction
s_irqLine:	rb 1
s_waiting:	rb 1
s_stopped:	rb 1
s_htime:	rb 2
s_vtime:	rb 2
s_rddiv:	rb 2
s_rdmpy:	rb 2
s_joy1:		rb 2
s_pad:		rb 2
s_padShift:	rb 2
s_bghofs:	rb 8
s_bgvofs:	rb 8
s_m7a:		rb 2
s_m7b:		rb 2
s_m7c:		rb 2
s_m7d:		rb 2
s_m7x:		rb 2
s_m7y:		rb 2
s_m7hofs:	rb 2
s_m7vofs:	rb 2
s_vramAddr:	rb 2
s_vramBuf:	rb 2
s_oamAddr:	rb 2
s_oamReload:	rb 2
s_cgAddr:	rb 2
s_fixedColor:	rb 2
s_hLatch:	rb 2
s_vLatch:	rb 2
s_noise:	rb 2
s_lastL:	rb 2
s_lastR:	rb 2
s_spcTimerDiv:	rb 6
s_echoHist:	rb 32
s_pal:		rb 1
s_hirom:	rb 1
s_sramDirty:	rb 1
s_chip:		rb 1
s_mdr:		rb 1
s_fastRom:	rb 1
s_nmitimen:	rb 1
s_wrio:		rb 1
s_wrmpya:	rb 1
s_wrdivl:	rb 1
s_wrdivh:	rb 1
s_nmiFlag:	rb 1
s_timeUp:	rb 1
s_autoJoyBusy:	rb 1
s_padStrobe:	rb 1
s_hdmaen:	rb 1
s_frameDone:	rb 1
s_inidisp:	rb 1
s_obsel:	rb 1
s_bgmode:	rb 1
s_mosaic:	rb 1
s_bgsc:		rb 4
s_bgnba:	rb 2
s_vmain:	rb 1
s_m7sel:	rb 1
s_bgLatch:	rb 1
s_bgLatchH:	rb 1
s_m7Latch:	rb 1
s_oamPrioRot:	rb 1
s_oamLatch:	rb 1
s_cgLatch:	rb 1
s_w12sel:	rb 1
s_w34sel:	rb 1
s_wobjsel:	rb 1
s_wh:		rb 4
s_wbglog:	rb 1
s_wobjlog:	rb 1
s_tm:		rb 1
s_ts:		rb 1
s_tmw:		rb 1
s_tsw:		rb 1
s_cgwsel:	rb 1
s_cgadsub:	rb 1
s_setini:	rb 1
s_overscan:	rb 1
s_hFlip:	rb 1
s_vFlip:	rb 1
s_counterLatched: rb 1
s_stat77:	rb 1
s_ppu1Bus:	rb 1
s_ppu2Bus:	rb 1
s_cpuToApu:	rb 4
s_apuToCpu:	rb 4
s_spcCtrl:	rb 1
s_dspAddr:	rb 1
s_spcTimerTarget: rb 3
s_spcTimerOut:	rb 3
s_spcTimerCnt:	rb 3
s_spcF8:	rb 1
s_spcF9:	rb 1
s_spcSleep:	rb 1
s_konPending:	rb 1
s_endx:		rb 1
s_everyOther:	rb 1
s_title:	rb 22
s_end:
end virtual

; un canal de DMA
D_ctrl		= 0
D_bbad		= 1
D_a1b		= 2
D_dasb		= 3
D_ntrl		= 4
D_doTransfer	= 5
D_done		= 6
D_unused	= 7
D_a1t		= 8
D_das		= 10
D_a2a		= 12
D_SIZE		= 16
; une voix du S-DSP
V_bufPos	= 0
V_buf		= 4
V_interpPos	= 52
V_brrAddr	= 56
V_brrOffset	= 60
V_konDelay	= 64
V_envMode	= 68
V_env		= 72
V_hiddenEnv	= 76
V_out		= 80
V_SIZE		= 96

S_DMA		= 0x300
S_DSP		= 0x380
S_VOICE		= 0x400
S_OAM		= 0x700
S_CGRAM		= 0x920
S_LUT		= 0x1000		; 16 x 32 mots
S_SPREAD	= 0x2000		; octet -> ses 8 bits sur 8 octets
S_BGCOL		= 0x3000
S_BGPRI		= 0x3800
S_OBJCOL	= 0x3C00
S_OBJPRI	= 0x3E00
S_OBJMATH	= 0x3F00
S_WIN		= 0x4000
S_WINVALID	= 0x4600
S_DBG		= 0x4608
S_DOBJ		= 0x4610
S_MAINC		= 0x5000
S_SUBC		= 0x5200
S_MAINZ		= 0x5400
S_SUBZ		= 0x5500
S_MAINL		= 0x5600
S_SUBL		= 0x5700
S_RD		= 0x6000		; 4096 pointeurs : pointeur or (vitesse shl 56) ; 0 : lent
S_WR		= 0xE000
S_ABUF		= 0x16000
S_FB		= 0x1E000
S_SRAM		= 0x5A000
S_WRAM		= 0x7A000
S_ARAM		= 0x9A000
S_VRAM		= 0xAA000
SNES_SIZE	= 0xBA000
assert s_end <= S_DMA

; une adresse dans la machine, quelle qu'elle soit
macro adm reg, ofs {
	add	reg,x19,(ofs) and 0xFFF000
	add	reg,reg,(ofs) and 0xFFF
}

; saut par une table de mots relatifs ; idx : registre x
macro jtab idx, tab {
	adr	x16,tab
	ldrsw	x17,[x16,idx,lsl 2]
	add	x16,x16,x17
	br	x16
}

macro io_cycle {
	add	x20,x20,6
}

; ---------------------------------------------------------------------------
;  Entrees
; ---------------------------------------------------------------------------

macro cpu_load {
	ldr	x20,[x19,s_now]
	ldr	w21,[x19,s_A]
	ldr	w22,[x19,s_X]
	ldr	w23,[x19,s_Y]
	ldr	w24,[x19,s_S]
	ldr	w25,[x19,s_PC]
	ldr	w26,[x19,s_P]
	ldr	w27,[x19,s_D]
	add	x28,x19,S_RD
}
macro cpu_store {
	str	x20,[x19,s_now]
	str	w21,[x19,s_A]
	str	w22,[x19,s_X]
	str	w23,[x19,s_Y]
	str	w24,[x19,s_S]
	str	w25,[x19,s_PC]
	str	w26,[x19,s_P]
	str	w27,[x19,s_D]
}
macro save_all {
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
}
macro rest_all {
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
}

snes_audio_rate:			; (m, hz)
	mov	w2,48000
	cmp	w1,0
	csel	w1,w1,w2,gt
	str	w1,[x0,s_rate]
	ret

; (m, donnees, octets)
snes_set_save:
	movl	x3,SNES_SRAM_MAX
	cmp	x2,x3
	csel	x2,x2,x3,lt
	add	x3,x0,S_SRAM
	mov	x4,0
.l:	cmp	x4,x2
	b.ge	.f
	ldrb	w5,[x1,x4]
	strb	w5,[x3,x4]
	add	x4,x4,1
	b	.l
.f:	strb	wzr,[x0,s_sramDirty]
	ret

; (m, &pointeur) -> octets ; 0 : rien de neuf
snes_save_poll:
	ldr	w2,[x0,s_sramSize]
	cbz	w2,.non
	ldrb	w3,[x0,s_sramDirty]
	cbz	w3,.non
	strb	wzr,[x0,s_sramDirty]
	add	x3,x0,S_SRAM
	str	x3,[x1]
	mov	x0,x2
	ret
.non:	mov	x0,0
	ret

; (m, lr, trames max) -> trames
snes_audio_read:
	add	x3,x0,S_ABUF
	ldr	w4,[x0,s_atail]
	ldr	w5,[x0,s_ahead]
	mov	x6,0
.l:	cmp	x6,x2
	b.hs	.f
	cmp	w4,w5
	b.eq	.f
	ldr	w7,[x3,x4,lsl 2]
	str	w7,[x1],4
	add	w4,w4,1
	and	w4,w4,SNES_ABUF-1
	add	x6,x6,1
	b	.l
.f:	str	w4,[x0,s_atail]
	mov	x0,x6
	ret

; x0 = m, x1 = pixels, x2 = pas, x3 = boutons
snes_frame:
	prologue
	save_all
	stp	x1,x2,[sp,-16]!
	mov	x19,x0
	strh	w3,[x19,s_pad]
	ldr	x0,[x19,s_rom]
	cbz	x0,.out
	cpu_load
	bl	run_frame
	cpu_store
	ldp	x1,x2,[sp]
	add	x0,x19,S_FB
	mov	x3,SNES_H
.y:	mov	x4,x1
	mov	x5,SNES_W/4
.x:	ldp	x6,x7,[x0],16
	stp	x6,x7,[x4],16
	subs	x5,x5,1
	b.ne	.x
	add	x1,x1,x2
	subs	x3,x3,1
	b.ne	.y
.out:	add	sp,sp,16
	rest_all
	epilogue

; x0 = m
snes_reset:
	prologue
	save_all
	mov	x19,x0
	ldr	x20,[x19,s_now]
	add	x28,x19,S_RD
	bl	do_reset
	cpu_store
	rest_all
	epilogue

; ---------------------------------------------------------------------------
;  La cartouche
; ---------------------------------------------------------------------------

; x0 = d, w1 = size, w2 = at, w3 = hi -> w0 = points (perd x4-x8)
header_score:
	add	w4,w2,0x40
	cmp	w4,w1
	b.ls	.ok
	mov	w0,-100
	ret
.ok:	add	x4,x0,x2			; h
	mov	w0,0				; s
	ldrh	w5,[x4,0x1E]
	ldrh	w6,[x4,0x1C]
	add	w5,w5,w6
	and	w5,w5,0xFFFF
	movl	w6,0xFFFF
	cmp	w5,w6
	b.ne	.a
	add	w0,w0,8
.a:	ldrb	w5,[x4,0x15]
	bic	w6,w5,0x10
	cbz	w3,.lo
	cmp	w6,0x21
	b.eq	.m
	cmp	w6,0x25
	b.eq	.m
	b	.b
.lo:	cmp	w6,0x20
	b.ne	.b
.m:	add	w0,w0,4
.b:	ldrh	w5,[x4,0x3C]
	mov	w6,0x8000
	cmp	w5,w6
	b.hs	.c
	sub	w0,w0,8
	b	.d
.c:	add	w0,w0,3
.d:	mov	x5,0
.t:	ldrb	w6,[x4,x5]
	cmp	w6,0x20
	b.lo	.t1
	cmp	w6,0x7E
	b.ls	.t2
.t1:	sub	w0,w0,1
	b	.t3
.t2:	add	x5,x5,1
	cmp	x5,21
	b.lo	.t
.t3:	ldrb	w5,[x4,0x19]
	cmp	w5,0x14
	b.hs	.e
	add	w0,w0,1
.e:	ldrb	w5,[x4,0x17]
	cmp	w5,0x0E
	b.hs	.f
	add	w0,w0,1
.f:	ret

; x0 = m, x1 = rom, x2 = octets -> 1 / 0
snes_load:
	prologue
	save_all
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	; les tables : couleur, bits
	add	x0,x19,S_LUT
	mov	w1,0				; b
.lb:	mov	w2,0				; c
.lc:	mov	w3,255
	mul	w3,w3,w2
	mov	w4,31
	udiv	w3,w3,w4
	mul	w3,w3,w1
	mov	w4,15
	udiv	w3,w3,w4
	str	w3,[x0],4
	add	w2,w2,1
	cmp	w2,32
	b.lo	.lc
	add	w1,w1,1
	cmp	w1,16
	b.lo	.lb
	add	x0,x19,S_SPREAD
	mov	x1,0
.sp:	mov	x2,0
	mov	x3,0
.sb:	lsr	x4,x1,x3
	and	x4,x4,1
	mov	x5,7
	sub	x5,x5,x3
	lsl	x5,x5,3
	lsl	x4,x4,x5
	orr	x2,x2,x4
	add	x3,x3,1
	cmp	x3,8
	b.lo	.sb
	str	x2,[x0,x1,lsl 3]
	add	x1,x1,1
	cmp	x1,256
	b.lo	.sp
	mov	w0,224
	str	w0,[x19,s_height]
	cbz	x21,.no
	cmp	x22,0x8000
	b.lt	.no
	and	x0,x22,0x3FF
	cmp	x0,0x200
	b.ne	.h
	add	x21,x21,0x200			; en-tete de copieur
	sub	x22,x22,0x200
.h:	mov	x0,x21
	mov	w1,w22
	mov	w2,0x7FC0
	mov	w3,0
	bl	header_score
	mov	w23,w0
	mov	x0,x21
	mov	w1,w22
	mov	w2,0xFFC0
	mov	w3,1
	bl	header_score
	cmp	w0,w23
	cset	w24,gt				; hirom
	strb	w24,[x19,s_hirom]
	csel	w0,w0,w23,gt
	tbnz	w0,31,.no
	mov	w2,0x7FC0
	mov	w3,0xFFC0
	cmp	w24,0
	csel	w2,w3,w2,ne
	add	x25,x21,x2			; h
	str	x21,[x19,s_rom]
	str	w22,[x19,s_romSize]
	; le titre
	add	x0,x19,s_title
	mov	x1,0
.tt:	ldrb	w2,[x25,x1]
	mov	w3,' '
	cmp	w2,0x20
	csel	w2,w3,w2,lo
	cmp	w2,0x7F
	csel	w2,w3,w2,hs
	strb	w2,[x0,x1]
	add	x1,x1,1
	cmp	x1,21
	b.lo	.tt
	mov	x1,21
.tz:	cbz	x1,.t0
	sub	x2,x1,1
	ldrb	w3,[x0,x2]
	cmp	w3,' '
	b.ne	.t0
	mov	x1,x2
	b	.tz
.t0:	strb	wzr,[x0,x1]
	ldrb	w0,[x25,0x16]
	strb	w0,[x19,s_chip]
	ldrb	w1,[x25,0x18]			; la RAM
	mov	w2,0
	cbz	w1,.sr
	cmp	w1,7
	mov	w3,7
	csel	w1,w3,w1,hi
	mov	w2,1024
	lsl	w2,w2,w1
.sr:	mov	w3,SNES_SRAM_MAX
	cmp	w2,w3
	csel	w2,w3,w2,hi
	str	w2,[x19,s_sramSize]
	ldrb	w1,[x25,0x19]			; la region
	sub	w1,w1,2
	cmp	w1,10
	cset	w1,ls
	strb	w1,[x19,s_pal]
	and	w1,w0,0x0F			; les puces absentes
	cmp	w1,3
	b.hs	.no
	adm	x0,S_SRAM
	mov	w1,SNES_SRAM_MAX
	mov	w2,0xFF
.ff:	strb	w2,[x0],1
	subs	w1,w1,1
	b.ne	.ff
	ldr	x20,[x19,s_now]
	add	x28,x19,S_RD
	bl	do_reset
	cpu_store
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	rest_all
	epilogue

; w0 = a -> w0 ; garde x9-x15
rom_mirror:
	ldr	w1,[x19,s_romSize]		; size
	cmp	w0,w1
	b.hs	.m
	ret
.m:	mov	w2,0				; base
	mov	w3,0x800000			; mask
.w:	cmp	w0,w1
	b.lo	.f
.a:	tst	w0,w3
	b.ne	.b
	lsr	w3,w3,1
	b	.a
.b:	sub	w0,w0,w3
	cmp	w1,w3
	b.ls	.c
	sub	w1,w1,w3
	add	w2,w2,w3
.c:	lsr	w3,w3,1
	b	.w
.f:	add	w0,w0,w2
	ret

map_pages:
	prologue
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	stp	x13,x14,[sp,-16]!
	mov	w9,0				; p
	ldrb	w10,[x19,s_hirom]
	ldr	w11,[x19,s_sramSize]
.page:	lsr	w12,w9,4			; bank
	and	w13,w9,15
	lsl	w13,w13,12			; addr
	mov	x4,0				; r
	mov	x5,0				; w
	mov	w6,8				; sp
	and	w7,w12,0x7F			; bank & 0x7F
	cmp	w12,0x7E
	b.lo	.nw
	cmp	w12,0x7F
	b.hi	.nw
	adm	x4,S_WRAM
	and	w0,w12,1
	lsl	w0,w0,16
	add	x4,x4,x0
	add	x4,x4,x13
	mov	x5,x4
	b	.fin
.nw:	tbnz	w12,6,.cart
	cmp	w13,0x2000
	b.hs	.n2
	adm	x4,S_WRAM
	add	x4,x4,x13
	mov	x5,x4
	b	.fin
.n2:	cmp	w13,0x8000
	b.hs	.cart
	mov	w6,0				; registres et le reste : la voie lente
	cbz	w10,.fin
	cmp	w13,0x6000
	b.lo	.fin
	cmp	w7,0x20
	b.lo	.fin
	cmp	w11,0x1000
	b.lo	.fin
	and	w0,w12,0x1F
	lsl	w0,w0,13
	sub	w1,w13,0x6000
	orr	w0,w0,w1
	sub	w1,w11,1
	and	w0,w0,w1
	adm	x4,S_SRAM
	add	x4,x4,x0
	mov	w6,8
	b	.fin
.cart:	cbnz	w10,.hi
	; LoROM
	cmp	w13,0x8000
	b.lo	.l1
	lsl	w0,w7,15
	and	w1,w13,0x7FFF
	orr	w0,w0,w1
	bl	.rom
	b	.l3
.l1:	cmp	w7,0x70
	b.lo	.l2
	cmp	w11,0x1000
	b.lo	.l3
	and	w0,w12,0x0F
	lsl	w0,w0,15
	orr	w0,w0,w13
	sub	w1,w11,1
	and	w0,w0,w1
	adm	x4,S_SRAM
	add	x4,x4,x0
	b	.l3
.l2:	cmp	w7,0x40
	b.lo	.l3
	and	w0,w12,0x3F
	lsl	w0,w0,15
	and	w1,w13,0x7FFF
	orr	w0,w0,w1
	bl	.rom
.l3:	cmp	w13,0x8000
	b.hs	.spd
	cmp	w7,0x70
	b.hs	.fin				; sp = 8
	b	.spd
.hi:	and	w0,w12,0x3F
	lsl	w0,w0,16
	orr	w0,w0,w13
	bl	.rom
.spd:	tbz	w12,7,.fin
	ldrb	w0,[x19,s_fastRom]
	cbz	w0,.fin
	mov	w6,6
.fin:	cbnz	x4,.put
	mov	w6,0
.put:	lsl	x6,x6,56
	orr	x4,x4,x6
	str	x4,[x28,x9,lsl 3]
	add	x0,x28,S_WR-S_RD
	str	x5,[x0,x9,lsl 3]
	add	w9,w9,1
	cmp	w9,4096
	b.lo	.page
	ldp	x13,x14,[sp],16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	epilogue
.rom:	mov	x14,x30
	bl	rom_mirror
	ldr	x4,[x19,s_rom]
	add	x4,x4,x0
	br	x14

; ---------------------------------------------------------------------------
;  Le bus
; ---------------------------------------------------------------------------

; w0 = a -> w1 = vitesse des registres ; garde w0
io_speed:
	and	w2,w0,0xFFFF
	lsr	w3,w2,9
	mov	w1,12
	cmp	w3,0x20				; 4000-41FF
	b.eq	.r
	lsr	w3,w2,13
	sub	w3,w3,1				; 2000-5FFF
	cmp	w3,2
	mov	w1,6
	mov	w3,8
	csel	w1,w1,w3,lo
.r:	ret

; w0 = a -> w0
cpu_read:
	lsr	w1,w0,12
	ldr	x2,[x28,x1,lsl 3]
	cbz	x2,.slow
	add	x20,x20,x2,lsr 56
	and	x2,x2,0x00FFFFFFFFFFFFFF
	and	w1,w0,0xFFF
	ldrb	w0,[x2,x1]
	strb	w0,[x19,s_mdr]
	ret
.slow:	str	x30,[sp,-16]!
	bl	io_speed
	add	x20,x20,x1
	bl	io_read
	strb	w0,[x19,s_mdr]
	ldr	x30,[sp],16
	ret

; w0 = a, w1 = v
cpu_write:
	and	w1,w1,0xFF
	strb	w1,[x19,s_mdr]
	lsr	w2,w0,12
	add	x3,x28,S_WR-S_RD
	ldr	x3,[x3,x2,lsl 3]
	cbz	x3,.slow
	add	x20,x20,8
	and	w2,w0,0xFFF
	strb	w1,[x3,x2]
	ret
.slow:	str	x30,[sp,-16]!
	ldr	x3,[x28,x2,lsl 3]
	lsr	x3,x3,56
	mov	w4,w1
	cbnz	x3,.sp
	bl	io_speed
	mov	x3,x1
.sp:	add	x20,x20,x3
	mov	w1,w4
	bl	io_write
	ldr	x30,[sp],16
	ret

; le numero de registre $43xx -> l'octet du canal ; 0xFF : aucun
dma_field:
	db	D_ctrl, D_bbad, D_a1t, D_a1t+1, D_a1b, D_das, D_das+1, D_dasb
	db	D_a2a, D_a2a+1, D_ntrl, D_unused, 0xFF, 0xFF, 0xFF, D_unused
	align 4

; w0 = a -> w0
io_read:
	lsr	w1,w0,16			; bank
	and	w2,w0,0xFFFF			; addr
	tbnz	w1,6,.hi
	tbnz	w2,15,.hi
	lsr	w3,w2,8
	cmp	w3,0x21
	b.ne	.n1
	and	w0,w2,0xFF
	b	read_b
.n1:	movl	w3,0x4016
	cmp	w2,w3
	b.ne	.n2
	ldrb	w0,[x19,s_mdr]			; $4016
	and	w0,w0,0xFC
	ldrh	w4,[x19,s_padShift]
	orr	w0,w0,w4,lsr 15
	lsl	w4,w4,1
	orr	w4,w4,1
	strh	w4,[x19,s_padShift]
	ret
.n2:	add	w3,w3,1
	cmp	w2,w3
	b.ne	.n3
	ldrb	w0,[x19,s_mdr]			; $4017
	and	w0,w0,0xE0
	orr	w0,w0,0x1C
	ret
.n3:	movl	w3,0x4300
	sub	w3,w2,w3
	cmp	w3,0x80
	b.hs	.n4
	lsr	w4,w3,4
	and	w3,w3,15
	adr	x5,dma_field
	ldrb	w5,[x5,x3]
	cmp	w5,0xFF
	b.eq	.mdr
	add	x6,x19,S_DMA
	add	x6,x6,x4,lsl 4
	ldrb	w0,[x6,x5]
	ret
.n4:	movl	w3,0x4210
	sub	w3,w2,w3
	cmp	w3,16
	b.hs	.sr
	jtab	x3,.t10
.t10:	dw	.r10-.t10,.r11-.t10,.r12-.t10,.r13-.t10,.r14-.t10,.r15-.t10,.r16-.t10,.r17-.t10
	dw	.r18-.t10,.r19-.t10,.r1a-.t10,.r1a-.t10,.r1a-.t10,.r1a-.t10,.r1a-.t10,.r1a-.t10
.r10:	ldrb	w0,[x19,s_mdr]
	and	w0,w0,0x70
	ldrb	w4,[x19,s_nmiFlag]
	orr	w0,w0,w4,lsl 7
	orr	w0,w0,2
	strb	wzr,[x19,s_nmiFlag]
	ret
.r11:	ldrb	w0,[x19,s_mdr]
	and	w0,w0,0x7F
	ldrb	w4,[x19,s_timeUp]
	orr	w0,w0,w4,lsl 7
	strb	wzr,[x19,s_timeUp]
	strb	wzr,[x19,s_irqLine]
	ret
.r12:	ldrb	w0,[x19,s_mdr]
	and	w0,w0,0x3E
	ldr	w4,[x19,s_line]
	mov	w5,225
	ldrb	w6,[x19,s_overscan]
	mov	w7,240
	cmp	w6,0
	csel	w5,w7,w5,ne			; vdisp
	cmp	w4,w5
	b.lt	.a
	orr	w0,w0,0x80
.a:	ldr	x6,[x19,s_lineStart]
	sub	x6,x20,x6
	ubfx	x6,x6,2,16			; h
	cmp	w6,1
	b.lo	.b
	cmp	w6,274
	b.lo	.c
.b:	orr	w0,w0,0x40
.c:	ldrb	w6,[x19,s_autoJoyBusy]
	cbz	w6,.d
	cmp	w4,w5
	b.lt	.d
	add	w5,w5,3
	cmp	w4,w5
	b.ge	.d
	orr	w0,w0,1
.d:	ret
.r13:	ldrb	w0,[x19,s_wrio]
	ret
.r14:	ldrb	w0,[x19,s_rddiv]
	ret
.r15:	ldrb	w0,[x19,s_rddiv+1]
	ret
.r16:	ldrb	w0,[x19,s_rdmpy]
	ret
.r17:	ldrb	w0,[x19,s_rdmpy+1]
	ret
.r18:	ldrb	w0,[x19,s_joy1]
	ret
.r19:	ldrb	w0,[x19,s_joy1+1]
	ret
.r1a:	mov	w0,0
	ret
.sr:	ldrb	w3,[x19,s_hirom]		; HiROM : la RAM en 6000-7FFF
	cbz	w3,.mdr
	cmp	w2,0x6000
	b.lo	.mdr
	and	w3,w1,0x7F
	cmp	w3,0x20
	b.lo	.mdr
	ldr	w3,[x19,s_sramSize]
	cbz	w3,.mdr
	and	w1,w1,0x1F
	lsl	w1,w1,13
	sub	w2,w2,0x6000
	orr	w1,w1,w2
	sub	w3,w3,1
	and	w1,w1,w3
	adm	x2,S_SRAM
	ldrb	w0,[x2,x1]
	ret
.hi:	ldrb	w3,[x19,s_hirom]		; LoROM : la RAM en 70-7D
	cbnz	w3,.mdr
	and	w3,w1,0x7F
	cmp	w3,0x70
	b.lo	.mdr
	cmp	w3,0x7E
	b.hs	.mdr
	tbnz	w2,15,.mdr
	ldr	w3,[x19,s_sramSize]
	cbz	w3,.mdr
	and	w1,w1,0x0F
	lsl	w1,w1,15
	orr	w1,w1,w2
	sub	w3,w3,1
	and	w1,w1,w3
	adm	x2,S_SRAM
	ldrb	w0,[x2,x1]
	ret
.mdr:	ldrb	w0,[x19,s_mdr]
	ret

; w0 = a, w1 = v
io_write:
	lsr	w2,w0,16			; bank
	and	w3,w0,0xFFFF			; addr
	tbnz	w2,6,.hi
	tbnz	w3,15,.hi
	lsr	w4,w3,8
	cmp	w4,0x21
	b.ne	.n1
	and	w0,w3,0xFF
	b	write_b
.n1:	movl	w4,0x4016
	cmp	w3,w4
	b.ne	.n2
	and	w1,w1,1				; s
	ldrb	w5,[x19,s_padStrobe]
	ldrh	w6,[x19,s_pad]
	ldrh	w7,[x19,s_padShift]
	cbz	w5,.a
	cbnz	w1,.a
	mov	w7,w6
.a:	cbz	w1,.b
	mov	w7,w6
.b:	strh	w7,[x19,s_padShift]
	strb	w1,[x19,s_padStrobe]
	ret
.n2:	movl	w4,0x4300
	sub	w4,w3,w4
	cmp	w4,0x80
	b.hs	.n3
	lsr	w5,w4,4
	and	w4,w4,15
	adr	x6,dma_field
	ldrb	w6,[x6,x4]
	cmp	w6,0xFF
	b.eq	.r
	add	x7,x19,S_DMA
	add	x7,x7,x5,lsl 4
	strb	w1,[x7,x6]
.r:	ret
.n3:	movl	w4,0x4200
	sub	w4,w3,w4
	cmp	w4,14
	b.hs	.sr
	jtab	x4,.t00
.t00:	dw	.w00-.t00,.w01-.t00,.w02-.t00,.w03-.t00,.w04-.t00,.w05-.t00,.w06-.t00,.w07-.t00
	dw	.w08-.t00,.w09-.t00,.w0a-.t00,.w0b-.t00,.w0c-.t00,.w0d-.t00
.w00:	ldrb	w4,[x19,s_nmitimen]
	strb	w1,[x19,s_nmitimen]
	tbnz	w4,7,.w0a1
	tbz	w1,7,.w0a1
	ldrb	w4,[x19,s_nmiFlag]
	cbz	w4,.w0a1
	mov	w4,1
	strb	w4,[x19,s_nmiPending]
.w0a1:	tst	w1,0x30
	b.ne	compute_irq
	strb	wzr,[x19,s_timeUp]
	strb	wzr,[x19,s_irqLine]
	b	compute_irq
.w01:	ldrb	w4,[x19,s_wrio]
	strb	w1,[x19,s_wrio]
	tbz	w4,7,.r
	tbnz	w1,7,.r
	b	latch_counters
.w02:	strb	w1,[x19,s_wrmpya]
	ret
.w03:	ldrb	w4,[x19,s_wrmpya]
	mul	w4,w4,w1
	strh	w4,[x19,s_rdmpy]
	strh	w1,[x19,s_rddiv]
	ret
.w04:	strb	w1,[x19,s_wrdivl]
	ret
.w05:	strb	w1,[x19,s_wrdivh]
	ret
.w06:	ldrb	w4,[x19,s_wrdivl]
	ldrb	w5,[x19,s_wrdivh]
	orr	w4,w4,w5,lsl 8
	cbz	w1,.dz
	udiv	w5,w4,w1
	msub	w6,w5,w1,w4
	strh	w5,[x19,s_rddiv]
	strh	w6,[x19,s_rdmpy]
	ret
.dz:	mov	w5,0xFFFF
	strh	w5,[x19,s_rddiv]
	strh	w4,[x19,s_rdmpy]
	ret
.w07:	strb	w1,[x19,s_htime]
	b	compute_irq
.w08:	and	w1,w1,1
	strb	w1,[x19,s_htime+1]
	b	compute_irq
.w09:	strb	w1,[x19,s_vtime]
	b	compute_irq
.w0a:	and	w1,w1,1
	strb	w1,[x19,s_vtime+1]
	b	compute_irq
.w0b:	mov	w0,w1
	b	dma_run
.w0c:	strb	w1,[x19,s_hdmaen]
	ret
.w0d:	and	w1,w1,1
	ldrb	w4,[x19,s_fastRom]
	cmp	w4,w1
	b.eq	.r
	strb	w1,[x19,s_fastRom]
	b	map_pages
.sr:	ldrb	w4,[x19,s_hirom]
	cbz	w4,.r
	cmp	w3,0x6000
	b.lo	.r
	and	w4,w2,0x7F
	cmp	w4,0x20
	b.lo	.r
	ldr	w4,[x19,s_sramSize]
	cbz	w4,.r
	and	w2,w2,0x1F
	lsl	w2,w2,13
	sub	w3,w3,0x6000
	orr	w2,w2,w3
	sub	w4,w4,1
	and	w2,w2,w4
.ps:	adm	x3,S_SRAM
	ldrb	w4,[x3,x2]
	cmp	w4,w1
	b.eq	.r
	strb	w1,[x3,x2]
	mov	w4,1
	strb	w4,[x19,s_sramDirty]
	ret
.hi:	ldrb	w4,[x19,s_hirom]
	cbnz	w4,.r
	and	w4,w2,0x7F
	cmp	w4,0x70
	b.lo	.r
	cmp	w4,0x7E
	b.hs	.r
	tbnz	w3,15,.r
	ldr	w4,[x19,s_sramSize]
	cbz	w4,.r
	and	w2,w2,0x0F
	lsl	w2,w2,15
	orr	w2,w2,w3
	sub	w4,w4,1
	and	w2,w2,w4
	b	.ps

; le bus B : w0 = a -> w0
read_b:
	cmp	w0,0x40
	b.lo	ppu_read
	cmp	w0,0x80
	b.hi	ppu_read
	b.eq	.wm
	stp	x0,x30,[sp,-16]!
	bl	apu_sync
	ldp	x0,x30,[sp],16
	and	w0,w0,3
	add	x1,x19,s_apuToCpu
	ldrb	w0,[x1,x0]
	ret
.wm:	ldr	w1,[x19,s_wmAddr]
	adm	x2,S_WRAM
	ldrb	w0,[x2,x1]
	add	w1,w1,1
	and	w1,w1,0x1FFFF
	str	w1,[x19,s_wmAddr]
	ret

; w0 = a, w1 = v
write_b:
	and	w1,w1,0xFF
	cmp	w0,0x40
	b.lo	ppu_write
	cmp	w0,0x80
	b.lo	.apu
	cmp	w0,0x83
	b.hi	ppu_write
	ldr	w2,[x19,s_wmAddr]
	sub	w0,w0,0x80
	cbnz	w0,.a1
	adm	x3,S_WRAM
	strb	w1,[x3,x2]
	add	w2,w2,1
	and	w2,w2,0x1FFFF
	b	.s
.a1:	cmp	w0,1
	b.ne	.a2
	bfxil	w2,w1,0,8
	b	.s
.a2:	cmp	w0,2
	b.ne	.a3
	bfi	w2,w1,8,8
	b	.s
.a3:	bfi	w2,w1,16,1
.s:	str	w2,[x19,s_wmAddr]
	ret
.apu:	stp	x0,x30,[sp,-16]!
	str	x1,[sp,-16]!
	bl	apu_sync
	ldr	x1,[sp],16
	ldp	x0,x30,[sp],16
	and	w0,w0,3
	add	x2,x19,s_cpuToApu
	strb	w1,[x2,x0]
	ret

; w0 = a -> Z mis si le bus A n'y va pas (perd w1, w2)
a_blocked:
	tst	w0,0x400000
	b.ne	.non
	and	w1,w0,0xFFFF
	lsr	w2,w1,8
	cmp	w2,0x21
	b.eq	.oui
	lsr	w2,w1,10
	cmp	w2,0x10				; 4000-43FF
	b.eq	.oui
.non:	mov	w1,1
	cmp	w1,0
	ret
.oui:	cmp	w1,w1
	ret

dma_read_a:				; w0 = a -> w0
	str	x30,[sp,-16]!
	bl	a_blocked
	b.ne	.ok
	ldrb	w0,[x19,s_mdr]
	b	.r
.ok:	lsr	w1,w0,12
	ldr	x2,[x28,x1,lsl 3]
	cbz	x2,.io
	and	x2,x2,0x00FFFFFFFFFFFFFF
	and	w1,w0,0xFFF
	ldrb	w0,[x2,x1]
	strb	w0,[x19,s_mdr]
	b	.r
.io:	bl	io_read
	strb	w0,[x19,s_mdr]
.r:	ldr	x30,[sp],16
	ret

dma_write_a:				; w0 = a, w1 = v
	str	x30,[sp,-16]!
	mov	w3,w1
	bl	a_blocked
	b.eq	.r
	lsr	w2,w0,12
	add	x1,x28,S_WR-S_RD
	ldr	x2,[x1,x2,lsl 3]
	mov	w1,w3
	cbz	x2,.io
	and	w4,w0,0xFFF
	strb	w1,[x2,x4]
	b	.r
.io:	bl	io_write
.r:	ldr	x30,[sp],16
	ret

; ---------------------------------------------------------------------------
;  DMA et HDMA
; ---------------------------------------------------------------------------

dma_pattern:
	db	0,0,0,0, 0,1,0,1, 0,0,0,0, 0,0,1,1, 0,1,2,3, 0,1,0,1, 0,0,0,0, 0,0,1,1
dma_len:
	db	1,2,2,4,4,4,2,4
	align 4

; w0 = masque
dma_run:
	cbz	w0,.nil
	prologue
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	stp	x13,x14,[sp,-16]!
	stp	x15,x16,[sp,-16]!
	mov	w9,w0				; masque
	add	x20,x20,8
	mov	w10,0				; i
.chan:	lsr	w0,w9,w10
	tbz	w0,0,.next
	add	x11,x19,S_DMA
	add	x11,x11,x10,lsl 4		; c
	add	x20,x20,8
	ldrh	w12,[x11,D_das]			; count
	cbnz	w12,.c
	mov	w12,0x10000
.c:	mov	w13,0				; k
	ldrb	w0,[x11,D_ctrl]
	mov	w14,1				; step
	tbz	w0,4,.c1
	mov	w14,-1
.c1:	tbz	w0,3,.c2
	mov	w14,0
.c2:	and	w15,w0,7			; motif
.xfer:	cbz	w12,.endc
	sub	w12,w12,1
	adr	x0,dma_pattern
	add	x0,x0,x15,lsl 2
	and	w1,w13,3
	ldrb	w0,[x0,x1]
	ldrb	w1,[x11,D_bbad]
	add	w0,w0,w1
	and	w0,w0,0xFF			; b
	ldrb	w1,[x11,D_a1b]
	ldrh	w2,[x11,D_a1t]
	orr	w16,w2,w1,lsl 16		; aa
	ldrb	w1,[x11,D_ctrl]
	tbz	w1,7,.a2b
	str	x16,[sp,-16]!
	bl	read_b
	mov	w1,w0
	ldr	x0,[sp],16
	bl	dma_write_a
	b	.step
.a2b:	stp	x0,x16,[sp,-16]!
	mov	w0,w16
	bl	dma_read_a
	mov	w1,w0
	ldp	x0,x16,[sp],16
	cmp	w0,0x80
	b.ne	.wr
	and	w2,w16,0xFE0000
	cmp	w2,0x7E0000
	b.eq	.step
.wr:	bl	write_b
.step:	ldrh	w0,[x11,D_a1t]
	add	w0,w0,w14
	strh	w0,[x11,D_a1t]
	add	x20,x20,8
	add	w13,w13,1
	b	.xfer
.endc:	strh	wzr,[x11,D_das]
.next:	add	w10,w10,1
	cmp	w10,8
	b.lo	.chan
	ldp	x15,x16,[sp],16
	ldp	x13,x14,[sp],16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
	epilogue
.nil:	ret

; x11 = canal : l'octet suivant de la table de HDMA -> w0
hdma_byte:
	str	x30,[sp,-16]!
	ldrb	w0,[x11,D_a1b]
	ldrh	w1,[x11,D_a2a]
	orr	w0,w1,w0,lsl 16
	add	w1,w1,1
	strh	w1,[x11,D_a2a]
	bl	dma_read_a
	ldr	x30,[sp],16
	ret

; x11 = canal : ntrl (et l'adresse indirecte) depuis la table
hdma_head:
	str	x30,[sp,-16]!
	bl	hdma_byte
	strb	w0,[x11,D_ntrl]
	add	x20,x20,8
	ldrb	w0,[x11,D_ctrl]
	tbz	w0,6,.d
	bl	hdma_byte
	mov	w12,w0
	bl	hdma_byte
	orr	w0,w12,w0,lsl 8
	strh	w0,[x11,D_das]
	add	x20,x20,16
.d:	ldrb	w0,[x11,D_ntrl]
	cbnz	w0,.t
	mov	w0,1
	strb	w0,[x11,D_done]
.t:	mov	w0,1
	strb	w0,[x11,D_doTransfer]
	ldr	x30,[sp],16
	ret

hdma_init:
	prologue
	mov	w10,0
.l:	add	x11,x19,S_DMA
	add	x11,x11,x10,lsl 4
	ldrb	w0,[x19,s_hdmaen]
	lsr	w0,w0,w10
	and	w0,w0,1
	eor	w0,w0,1
	strb	w0,[x11,D_done]
	strb	wzr,[x11,D_doTransfer]
	cbnz	w0,.n
	ldrh	w0,[x11,D_a1t]
	strh	w0,[x11,D_a2a]
	bl	hdma_head
.n:	add	w10,w10,1
	cmp	w10,8
	b.lo	.l
	epilogue

hdma_run:
	prologue
	mov	w9,0				; any
	mov	w10,0
.l:	add	x11,x19,S_DMA
	add	x11,x11,x10,lsl 4
	ldrb	w0,[x11,D_done]
	cbnz	w0,.n
	ldrb	w0,[x19,s_hdmaen]
	lsr	w0,w0,w10
	tbz	w0,0,.n
	mov	w9,1
	add	x20,x20,8
	ldrb	w0,[x11,D_doTransfer]
	cbz	w0,.after
	ldrb	w0,[x11,D_ctrl]
	and	w0,w0,7
	adr	x1,dma_len
	ldrb	w13,[x1,x0]			; len
	mov	w14,w0				; motif
	mov	w12,0				; k
.k:	ldrb	w0,[x11,D_ctrl]
	tbz	w0,6,.dir
	ldrb	w1,[x11,D_dasb]
	ldrh	w2,[x11,D_das]
	orr	w15,w2,w1,lsl 16
	add	w2,w2,1
	strh	w2,[x11,D_das]
	b	.ad
.dir:	ldrb	w1,[x11,D_a1b]
	ldrh	w2,[x11,D_a2a]
	orr	w15,w2,w1,lsl 16
	add	w2,w2,1
	strh	w2,[x11,D_a2a]
.ad:	adr	x1,dma_pattern
	add	x1,x1,x14,lsl 2
	ldrb	w1,[x1,x12]
	ldrb	w2,[x11,D_bbad]
	add	w1,w1,w2
	and	w8,w1,0xFF			; b
	tbz	w0,7,.a2b
	mov	w0,w8
	bl	read_b
	mov	w1,w0
	mov	w0,w15
	bl	dma_write_a
	b	.kn
.a2b:	mov	w0,w15
	str	x8,[sp,-16]!
	bl	dma_read_a
	mov	w1,w0
	ldr	x0,[sp],16
	bl	write_b
.kn:	add	x20,x20,8
	add	w12,w12,1
	cmp	w12,w13
	b.lo	.k
.after:	ldrb	w0,[x11,D_ntrl]
	sub	w0,w0,1
	and	w0,w0,0xFF
	strb	w0,[x11,D_ntrl]
	lsr	w1,w0,7
	strb	w1,[x11,D_doTransfer]
	tst	w0,0x7F
	b.ne	.n
	bl	hdma_head
.n:	add	w10,w10,1
	cmp	w10,8
	b.lo	.l
	cbz	w9,.r
	add	x20,x20,18
.r:	epilogue

; ---------------------------------------------------------------------------
;  Remise a zero, l'image
; ---------------------------------------------------------------------------

do_reset:
	prologue
	strb	wzr,[x19,s_fastRom]
	bl	map_pages
	strb	wzr,[x19,s_mdr]
	mov	w26,0x134			; E, P = 0x34
	mov	w21,0
	mov	w22,0
	mov	w23,0
	mov	w24,0x1FF
	mov	w27,0
	mov	w25,0
	str	wzr,[x19,s_nmiPending]		; et irqLine, waiting, stopped
	strb	wzr,[x19,s_nmitimen]
	mov	w0,0xFF
	strb	w0,[x19,s_wrio]
	strb	w0,[x19,s_wrmpya]
	strb	w0,[x19,s_wrdivl]
	strb	w0,[x19,s_wrdivh]
	strh	wzr,[x19,s_rddiv]
	strh	wzr,[x19,s_rdmpy]
	mov	w0,0x1FF
	strh	w0,[x19,s_htime]
	strh	w0,[x19,s_vtime]
	strb	wzr,[x19,s_nmiFlag]
	strb	wzr,[x19,s_timeUp]
	strb	wzr,[x19,s_autoJoyBusy]
	str	wzr,[x19,s_wmAddr]
	strh	wzr,[x19,s_joy1]
	strh	wzr,[x19,s_padShift]
	strb	wzr,[x19,s_padStrobe]
	strb	wzr,[x19,s_hdmaen]
	add	x1,x19,S_DMA
	mov	w2,8
	mov	w0,0xFF
	movl	w3,0xFFFF
.d:	strb	w0,[x1,D_ctrl]
	strb	w0,[x1,D_bbad]
	strb	w0,[x1,D_a1b]
	strb	w0,[x1,D_dasb]
	strb	w0,[x1,D_ntrl]
	strb	w0,[x1,D_unused]
	strh	w3,[x1,D_a1t]
	strh	w3,[x1,D_das]
	strh	w3,[x1,D_a2a]
	mov	w4,1
	strb	w4,[x1,D_done]
	strb	wzr,[x1,D_doTransfer]
	add	x1,x1,D_SIZE
	subs	w2,w2,1
	b.ne	.d
	bl	ppu_reset
	bl	apu_reset
	str	wzr,[x19,s_line]
	str	x20,[x19,s_lineStart]
	str	wzr,[x19,s_evStep]
	mov	x0,-1
	str	x0,[x19,s_irqAt]
	strb	wzr,[x19,s_frameDone]
	mov	w0,0xFFFC
	bl	cpu_read
	mov	w9,w0
	mov	w0,0xFFFD
	bl	cpu_read
	orr	w25,w9,w0,lsl 8
	bl	compute_irq
	epilogue

; garde x9-x15
compute_irq:
	mov	x0,-1
	str	x0,[x19,s_irqAt]
	ldrb	w0,[x19,s_nmitimen]
	ubfx	w0,w0,4,2			; en
	cbz	w0,.r
	ldr	w1,[x19,s_line]
	ldrh	w2,[x19,s_vtime]
	ldr	x3,[x19,s_lineStart]
	cmp	w0,2
	b.ne	.h
	cmp	w1,w2
	b.ne	.r
	add	x4,x3,10			; at
	b	.chk
.h:	cmp	w0,3
	b.ne	.h1
	cmp	w1,w2
	b.ne	.r
.h1:	ldrh	w5,[x19,s_htime]
	cmp	w5,339
	b.hi	.r
	add	x4,x3,x5,lsl 2
	add	x4,x4,14
.chk:	cmp	x4,x20
	b.hs	.ok
	cmp	w0,2
	b.eq	.v
	sub	x5,x20,x4
	cmp	x5,8
	b.hi	.r
	b	.ok
.v:	sub	x5,x20,x3
	cmp	x5,40
	b.hi	.r
.ok:	str	x4,[x19,s_irqAt]
.r:	ret

ev_pos:	dw	12,536,1096,1364

line_event:
	prologue
	ldr	w0,[x19,s_evStep]
	jtab	x0,.t
.t:	dw	.e0-.t,.e1-.t,.e2-.t,.e3-.t
.e0:	ldr	w0,[x19,s_line]
	cbnz	w0,.next
	bl	hdma_init
	b	.next
.e1:	add	x20,x20,40
	b	.next
.e2:	ldr	w0,[x19,s_line]
	bl	vdisp_of
	cmp	w0,w1
	b.ge	.next
	cmp	w0,1
	b.lt	.e2b
	bl	render_line
.e2b:	bl	hdma_run
.next:	ldr	w0,[x19,s_evStep]
	add	w0,w0,1
	str	w0,[x19,s_evStep]
	epilogue
.e3:	ldr	w9,[x19,s_line]
	add	w9,w9,1
	ldr	x0,[x19,s_lineStart]
	add	x0,x0,1364
	str	x0,[x19,s_lineStart]
	mov	w0,262
	mov	w1,312
	ldrb	w2,[x19,s_pal]
	cmp	w2,0
	csel	w0,w1,w0,ne			; lines ()
	cmp	w9,w0
	b.lt	.e31
	mov	w9,0
	strb	wzr,[x19,s_nmiFlag]
	ldrb	w0,[x19,s_setini]
	ubfx	w0,w0,2,1
	strb	w0,[x19,s_overscan]
	ldr	w0,[x19,s_frames]
	add	w0,w0,1
	str	w0,[x19,s_frames]
.e31:	str	w9,[x19,s_line]
	mov	w0,w9
	bl	vdisp_of
	cmp	w0,w1
	b.ne	.e32
	ldrb	w0,[x19,s_overscan]
	mov	w1,224
	mov	w2,239
	cmp	w0,0
	csel	w1,w2,w1,ne
	str	w1,[x19,s_height]
	mov	w0,1
	strb	w0,[x19,s_nmiFlag]
	ldrb	w1,[x19,s_nmitimen]
	tbz	w1,7,.e3a
	strb	w0,[x19,s_nmiPending]
.e3a:	ldrb	w2,[x19,s_inidisp]
	tbnz	w2,7,.e3b
	ldrh	w2,[x19,s_oamReload]
	strh	w2,[x19,s_oamAddr]
.e3b:	tbz	w1,0,.e3c
	ldrh	w2,[x19,s_pad]
	strh	w2,[x19,s_joy1]
	strb	w0,[x19,s_autoJoyBusy]
	mov	w2,0xFFFF
	strh	w2,[x19,s_padShift]
	b	.e3d
.e3c:	strb	wzr,[x19,s_autoJoyBusy]
.e3d:	strb	w0,[x19,s_frameDone]
.e32:	tst	w9,15
	b.ne	.e33
	bl	apu_sync
.e33:	str	wzr,[x19,s_evStep]
	bl	compute_irq
	epilogue

; w0 garde -> w1 = vdisp ()
vdisp_of:
	ldrb	w2,[x19,s_overscan]
	mov	w1,225
	mov	w3,240
	cmp	w2,0
	csel	w1,w3,w1,ne
	ret

macro irq_check {
	mov	w2,1
	strb	w2,[x19,s_timeUp]
	strb	w2,[x19,s_irqLine]
	mov	x2,-1
	str	x2,[x19,s_irqAt]
}

; une image ; garde le contexte du 65C816 dans ses registres
run_frame:
	prologue
	strb	wzr,[x19,s_frameDone]
rf_loop:
	ldrb	w0,[x19,s_frameDone]
	cbnz	w0,rf_fin
	ldr	w0,[x19,s_evStep]
	adr	x1,ev_pos
	ldr	w1,[x1,x0,lsl 2]
	ldr	x0,[x19,s_lineStart]
	add	x0,x0,x1
	str	x0,[x19,s_ev]
cpu_next:
	ldr	x0,[x19,s_ev]
	cmp	x20,x0
	b.hs	rf_event
	ldr	x1,[x19,s_irqAt]
	cmp	x20,x1
	b.lo	.chk
	irq_check
.chk:	ldr	w2,[x19,s_nmiPending]
	cbnz	w2,cpu_special
cpu_fetch_op:
  if defined SNES_TRACE
	bl	snes_trace
  end if
	mov	w0,w25
	add	w1,w25,1
	bfxil	w25,w1,0,16
	bl	cpu_read
	jtab	x0,cpu_optab

; NMI, IRQ, WAI, STP
cpu_special:
	ldr	x0,[x19,s_ev]
	ldr	x1,[x19,s_irqAt]
	cmp	x1,x0
	csel	x3,x1,x0,lo			; stop
	ldrb	w2,[x19,s_stopped]
	cbz	w2,.w
	mov	x20,x0
	b	rf_event
.w:	ldrb	w2,[x19,s_waiting]
	cbz	w2,.step
	ldrb	w2,[x19,s_nmiPending]
	ldrb	w4,[x19,s_irqLine]
	orr	w2,w2,w4
	cbnz	w2,.wake
	mov	x20,x3
	b	cpu_next
.wake:	strb	wzr,[x19,s_waiting]
.step:	ldrb	w2,[x19,s_nmiPending]
	cbz	w2,.irq
	strb	wzr,[x19,s_nmiPending]
	mov	w9,0xFFEA
	mov	w10,0xFFFA
	mov	w11,0
	bl	cpu_interrupt
	b	cpu_next
.irq:	ldrb	w2,[x19,s_irqLine]
	cbz	w2,cpu_fetch_op
	tbnz	w26,2,cpu_fetch_op
	mov	w9,0xFFEE
	mov	w10,0xFFFE
	mov	w11,0
	bl	cpu_interrupt
	b	cpu_next

rf_event:
	ldr	x1,[x19,s_irqAt]
	cmp	x20,x1
	b.lo	.e
	irq_check
.e:	bl	line_event
	b	rf_loop
rf_fin:	bl	apu_sync
	epilogue

include 'snes_cpu.inc'
include 'snes_ppu.inc'
include 'snes_apu.inc'
