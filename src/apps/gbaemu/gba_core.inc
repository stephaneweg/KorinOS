; ============================================================================
;  gba_core.inc - la Game Boy Advance : l'ARM7TDMI (ARM et Thumb), la memoire et
;  ses attentes, le PPU une ligne a la fois (modes 0-5, plans affines, sprites,
;  fenetres, melanges, mosaique), DMA, minuteurs, le son (4 voix PSG + Direct
;  Sound A / B), le BIOS de remplacement (les appels SWI faits ici, aucune image
;  de BIOS) et les sauvegardes (SRAM, Flash, EEPROM). Ecrit d'apres le coeur C++
;  d'Onyx (user/Emulators/gba), image pour image : tools/emu/ (docs/EMULATEURS.md).
;
;	gba_load (m, rom, octets)	-> 1 ; 0 : pas une ROM
;	gba_reset (m)
;	gba_frame (m, pixels, pas, boutons)	une image, 0x00RRGGBB, 240 x 160
;	gba_audio_rate (m, hz), gba_audio_read (m, lr, trames) -> trames
;	gba_set_save (m, donnees, octets), gba_save_poll (m, &pointeur) -> octets
;	m = GBA_SIZE octets a zero
;
;  Dans le coeur : x19 = m (r [] au debut), x20 = now, w21 = cyc, w22 = cpsr,
;  w23 = code en cours, w24 = son adresse, w26 = branched. x25, x27, x28 : libres
;  dans un code. Le bus garde x9-x15 et x23-x28, perd x0-x8, x16, x17.
; ============================================================================

GBA_W		= 240
GBA_H		= 160
GBA_ABUF	= 8192
GBA_CPU_HZ	= 16777216
SAVE_NONE	= 0
SAVE_SRAM	= 1
SAVE_FLASH64	= 2
SAVE_FLASH128	= 3
SAVE_EEPROM	= 4
TRANSP		= 0x80000000

virtual at 0
a_r:		rb 64
a_spsr:		rb 4
a_cpsr:		rb 4
a_biosLatch:	rb 4
a_fetchPC:	rb 4
a_pipe:		rb 8
a_bankR13:	rb 24
a_bankR14:	rb 24
a_bankSpsr:	rb 24
a_bankFiq:	rb 20
a_bankUsr:	rb 20
a_waitN:	rb 64
a_waitS:	rb 64
a_waitN32:	rb 64
a_waitS32:	rb 64
a_now:		rb 8
a_lineStart:	rb 8
a_target:	rb 8
a_rom:		rb 8
a_apuLast:	rb 8
a_sampleAcc:	rb 8
a_eeBuf:	rb 8
a_timers:	rb 64
a_dma:		rb 64
a_romSize:	rb 4
a_saveType:	rb 4
a_saveSize:	rb 4
a_keys:		rb 4
a_vcount:	rb 4
a_rate:		rb 4
a_ahead:	rb 4
a_atail:	rb 4
a_intrWait:	rb 4
a_affX:		rb 8
a_affY:		rb 8
a_frameSeq:	rb 4
a_fsTimer:	rb 4
a_fifoLen:	rb 8
a_fifoRd:	rb 8
a_fifoWr:	rb 8
a_dsOut:	rb 8
a_flashState:	rb 4
a_flashBank:	rb 4
a_eeBits:	rb 4
a_eeState:	rb 4
a_eeAddrBits:	rb 4
a_eeCmd:	rb 4
a_eeAddr:	rb 4
a_eeReadPos:	rb 4
a_halted:	rb 1			; ces deux : lus ensemble
a_irqLine:	rb 1
a_pad0:		rb 2
a_sq:		rb 2*SQ_SIZE
a_wv:		rb WV_SIZE
a_ns:		rb NS_SIZE
a_fifo:		rb 64
a_waveRam:	rb 32
a_evDirty:	rb 1
a_inHblank:	rb 1
a_frameDone:	rb 1
a_pipeOk:	rb 1
a_prefetch:	rb 1
a_saveDirty:	rb 1
a_flashId:	rb 1
a_flashErase:	rb 1
a_flashWrite:	rb 1
a_flashBankSel:	rb 1
a_title:	rb 13
a_code:		rb 5
a_end:
end virtual

T_reload	= 0
T_counter	= 2
T_ctl		= 4
T_last		= 8
T_SIZE		= 16
D_src		= 0
D_dst		= 4
D_cnt		= 8
D_ctl		= 12
D_on		= 14
D_SIZE		= 16
SQ_on		= 0
SQ_len		= 4
SQ_duty		= 8
SQ_dutyPos	= 12
SQ_vol		= 16
SQ_envPeriod	= 20
SQ_envTimer	= 24
SQ_envDir	= 28
SQ_freq		= 32
SQ_timer	= 36
SQ_lenOn	= 40
SQ_sweepPeriod	= 44
SQ_sweepTimer	= 48
SQ_sweepShift	= 52
SQ_sweepDir	= 56
SQ_shadow	= 60
SQ_sweepOn	= 64
SQ_SIZE		= 68
WV_on		= 0
WV_len		= 4
WV_vol		= 8
WV_freq		= 12
WV_timer	= 16
WV_pos		= 20
WV_lenOn	= 24
WV_sample	= 28
WV_SIZE		= 32
NS_on		= 0
NS_len		= 4
NS_vol		= 8
NS_envPeriod	= 12
NS_envTimer	= 16
NS_envDir	= 20
NS_shift	= 24
NS_width	= 28
NS_div		= 32
NS_timer	= 36
NS_lfsr		= 40
NS_lenOn	= 44
NS_SIZE		= 48

A_IO		= 0x800
A_OAM		= 0x1000
A_PAL		= 0x2000
A_BGL		= 0x3000		; 4 x 240 mots
A_OBJL		= 0x4000
A_OBJP		= 0x4400
A_OBJW		= 0x4500
A_WINL		= 0x4600
A_IWRAM		= 0x5000
A_BIOS		= 0xD000
A_EWRAM		= 0x11000
A_VRAM		= 0x51000
A_ABUF		= 0x69000
A_SAVE		= 0x71000
A_FB		= 0x91000
A_RGB		= 0xB7000
GBA_SIZE	= 0xD7000
assert a_end <= A_IO

macro adm reg, ofs {
	add	reg,x19,(ofs) and 0xFFF000
	add	reg,reg,(ofs) and 0xFFF
}
macro jtab idx, tab {
	adr	x16,tab
	ldrsw	x17,[x16,idx,lsl 2]
	add	x16,x16,x17
	br	x16
}
macro io_b reg, n {
	ldrb	reg,[x19,A_IO+(n)]
}
macro io_h reg, n {
	ldrh	reg,[x19,A_IO+(n)]
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
macro push_x9 {
	stp	x9,x10,[sp,-16]!
	stp	x11,x12,[sp,-16]!
	stp	x13,x14,[sp,-16]!
	stp	x15,x30,[sp,-16]!
}
macro pop_x9 {
	ldp	x15,x30,[sp],16
	ldp	x13,x14,[sp],16
	ldp	x11,x12,[sp],16
	ldp	x9,x10,[sp],16
}

; ---------------------------------------------------------------------------
;  Entrees
; ---------------------------------------------------------------------------

gba_audio_rate:				; (m, hz)
	mov	w2,32768
	cmp	w1,1000
	csel	w1,w1,w2,gt
	str	w1,[x0,a_rate]
	ret

; (m, &pointeur) -> octets ; 0 : rien de neuf
gba_save_poll:
	ldr	w2,[x0,a_saveType]
	cbz	w2,.non
	ldr	w2,[x0,a_saveSize]
	cbz	w2,.non
	ldrb	w3,[x0,a_saveDirty]
	cbz	w3,.non
	strb	wzr,[x0,a_saveDirty]
	add	x3,x0,A_SAVE
	str	x3,[x1]
	mov	x0,x2
	ret
.non:	mov	x0,0
	ret

; (m, donnees, octets)
gba_set_save:
	cbz	x1,.r
	cmp	x2,0
	b.le	.r
	mov	x3,0x20000
	cmp	x2,x3
	csel	x2,x3,x2,gt
	add	x3,x0,A_SAVE
	mov	x4,0
.l:	ldrb	w5,[x1,x4]
	strb	w5,[x3,x4]
	add	x4,x4,1
	cmp	x4,x2
	b.lo	.l
	ldr	w3,[x0,a_saveType]
	cmp	w3,SAVE_EEPROM
	b.ne	.n1
	cmp	x2,512
	mov	w4,6
	mov	w5,14
	csel	w4,w4,w5,ls
	str	w4,[x0,a_eeAddrBits]
	mov	w4,512
	mov	w5,0x2000
	csel	w4,w4,w5,ls
	str	w4,[x0,a_saveSize]
.n1:	cbnz	w3,.n2
	mov	w3,SAVE_SRAM
	mov	w4,SAVE_EEPROM
	cmp	x2,0x2000
	csel	w3,w4,w3,ls
	mov	w4,SAVE_FLASH64
	cmp	x2,0x10000
	csel	w3,w4,w3,eq
	mov	w4,SAVE_FLASH128
	cmp	x2,0x20000
	csel	w3,w4,w3,eq
	str	w3,[x0,a_saveType]
	str	w2,[x0,a_saveSize]
.n2:	strb	wzr,[x0,a_saveDirty]
.r:	ret

; (m, lr, trames max) -> trames
gba_audio_read:
	add	x3,x0,A_ABUF
	ldr	w4,[x0,a_ahead]
	ldr	w5,[x0,a_atail]
	mov	x6,0
.l:	cmp	w4,w5
	b.eq	.f
	cmp	x6,x2
	b.hs	.f
	ldr	w7,[x3,x4,lsl 2]
	str	w7,[x1],4
	add	w4,w4,1
	and	w4,w4,GBA_ABUF-1
	add	x6,x6,1
	b	.l
.f:	str	w4,[x0,a_ahead]
	mov	x0,x6
	ret

; x0 = m, x1 = pixels, x2 = pas, x3 = boutons
gba_frame:
	prologue
	save_all
	stp	x1,x2,[sp,-16]!
	mov	x19,x0
	str	w3,[x19,a_keys]
	ldr	x0,[x19,a_rom]
	cbz	x0,.out
	ldr	x20,[x19,a_now]
	ldr	w22,[x19,a_cpsr]
	bl	run_frame
	str	x20,[x19,a_now]
	str	w22,[x19,a_cpsr]
	ldp	x1,x2,[sp]
	add	x0,x19,A_FB
	mov	x3,GBA_H
.y:	mov	x4,x1
	mov	x5,GBA_W/4
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

gba_reset:				; (m)
	prologue
	save_all
	mov	x19,x0
	bl	do_reset
	rest_all
	epilogue

; (m, rom, octets) -> 1 / 0
gba_load:
	prologue
	save_all
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	; BGR555 -> 0x00RRGGBB
	add	x0,x19,A_RGB
	mov	w1,0
.c:	and	w2,w1,31
	lsl	w3,w2,3
	orr	w2,w3,w2,lsr 2			; r
	ubfx	w3,w1,5,5
	lsl	w4,w3,3
	orr	w3,w4,w3,lsr 2			; g
	ubfx	w4,w1,10,5
	lsl	w5,w4,3
	orr	w4,w5,w4,lsr 2			; b
	orr	w4,w4,w3,lsl 8
	orr	w4,w4,w2,lsl 16
	str	w4,[x0,x1,lsl 2]
	add	w1,w1,1
	cmp	w1,32768
	b.lo	.c
	ldr	w0,[x19,a_rate]
	cbnz	w0,.ra
	mov	w0,32768
	str	w0,[x19,a_rate]
.ra:	cbz	x21,.no
	cmp	x22,0xC0
	b.lt	.no
	str	x21,[x19,a_rom]
	str	w22,[x19,a_romSize]
	add	x0,x19,a_title
	mov	x1,0
.t:	add	x2,x21,0xA0
	ldrb	w2,[x2,x1]
	cmp	w2,32
	b.lo	.t0
	cmp	w2,127
	b.lo	.t1
.t0:	mov	w2,0
.t1:	strb	w2,[x0,x1]
	add	x1,x1,1
	cmp	x1,12
	b.lo	.t
	strb	wzr,[x0,12]
	add	x0,x19,a_code
	mov	x1,0
.k:	add	x2,x21,0xAC
	ldrb	w2,[x2,x1]
	cmp	w2,32
	b.lo	.k0
	cmp	w2,127
	b.lo	.k1
.k0:	mov	w2,' '
.k1:	strb	w2,[x0,x1]
	add	x1,x1,1
	cmp	x1,4
	b.lo	.k
	strb	wzr,[x0,4]
	bl	detect_save
	bl	do_reset
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	rest_all
	epilogue

save_ids:
	db	'EEPROM_V',0,0,0,0,	SAVE_EEPROM,0,0x20,0
	db	'SRAM_V',0,0,0,0,0,0,	SAVE_SRAM,0,0x80,0
	db	'SRAM_F_V',0,0,0,0,	SAVE_SRAM,0,0x80,0
	db	'FLASH1M_V',0,0,0,	SAVE_FLASH128,0,0,2
	db	'FLASH512_V',0,0,	SAVE_FLASH64,0,0,1
	db	'FLASH_V',0,0,0,0,0,	SAVE_FLASH64,0,0,1

detect_save:
	str	wzr,[x19,a_saveType]
	str	wzr,[x19,a_saveSize]
	ldr	w1,[x19,a_romSize]		; romSize
	ldr	x2,[x19,a_rom]
	mov	x3,0				; i
.i:	add	x0,x3,12
	cmp	x0,x1
	b.hs	.f
	ldrb	w4,[x2,x3]
	cmp	w4,'E'
	b.eq	.try
	cmp	w4,'S'
	b.eq	.try
	cmp	w4,'F'
	b.ne	.n
.try:	adr	x4,save_ids
	mov	x5,6
.k:	mov	x6,0
.j:	ldrb	w7,[x4,x6]
	cbz	w7,.hit
	add	x8,x2,x3
	ldrb	w8,[x8,x6]
	cmp	w7,w8
	b.ne	.nk
	add	x6,x6,1
	b	.j
.hit:	ldrb	w5,[x4,12]
	str	w5,[x19,a_saveType]
	ldrb	w5,[x4,14]
	ldrb	w6,[x4,15]
	lsl	w5,w5,8
	orr	w5,w5,w6,lsl 16
	str	w5,[x19,a_saveSize]
	b	.f
.nk:	add	x4,x4,16
	subs	x5,x5,1
	b.ne	.k
.n:	add	x3,x3,4
	b	.i
.f:	add	x0,x19,A_SAVE
	mov	w1,0x20000
	mov	w2,0xFF
.ff:	strb	w2,[x0],1
	subs	w1,w1,1
	b.ne	.ff
	str	wzr,[x19,a_eeAddrBits]
	ret

; x0 = debut, x1 = octets (multiple de 8)
mem_zero:
	str	xzr,[x0],8
	subs	x1,x1,8
	b.ne	mem_zero
	ret

bios_irq:
	dw	0xE92D500F,0xE3A00301,0xE28FE000,0xE510F004,0xE8BD500F,0xE25EF004

do_reset:
	prologue
	mov	x20,0
	adm	x0,A_BIOS
	mov	x1,0x4000
	bl	mem_zero
	adm	x0,A_EWRAM
	mov	x1,0x40000
	bl	mem_zero
	adm	x0,A_IWRAM
	mov	x1,0x8000
	bl	mem_zero
	add	x0,x19,A_PAL
	mov	x1,0x400
	bl	mem_zero
	adm	x0,A_VRAM
	mov	x1,0x18000
	bl	mem_zero
	add	x0,x19,A_OAM
	mov	x1,0x400
	bl	mem_zero
	add	x0,x19,A_IO
	mov	x1,0x400
	bl	mem_zero
	adm	x1,A_BIOS+0x18
	adr	x2,bios_irq
	mov	x3,6
.b:	ldr	w0,[x2],4
	str	w0,[x1],4
	subs	x3,x3,1
	b.ne	.b
	movl	w0,0xE55EC002
	adm	x1,A_BIOS+0x34
	str	w0,[x1]
	movl	w0,0xE129F000
	str	w0,[x19,a_biosLatch]
	mov	x0,x19
	mov	x1,64
	bl	mem_zero
	add	x0,x19,a_bankR13
	mov	x1,72
	bl	mem_zero
	mov	x1,0
	add	x0,x19,a_bankFiq
.f:	str	wzr,[x0,x1]
	add	x1,x1,4
	cmp	x1,40
	b.lo	.f
	mov	w22,0x1F
	str	wzr,[x19,a_spsr]
	movl	w0,0x03007FE0
	str	w0,[x19,a_bankR13+12]
	movl	w0,0x03007FA0
	str	w0,[x19,a_bankR13+8]
	movl	w0,0x03007F00
	str	w0,[x19,a_r+13*4]
	movl	w0,0x08000000
	str	w0,[x19,a_r+15*4]
	str	w0,[x19,a_fetchPC]
	strb	wzr,[x19,a_halted]
	strb	wzr,[x19,a_irqLine]
	strb	wzr,[x19,a_pipeOk]
	mov	w26,0
	str	wzr,[x19,a_intrWait]
	str	xzr,[x19,a_lineStart]
	strb	wzr,[x19,a_inHblank]
	str	wzr,[x19,a_vcount]
	strb	wzr,[x19,a_frameDone]
	add	x0,x19,a_timers
	mov	x1,128
	bl	mem_zero
	mov	w0,0x100
	strh	w0,[x19,A_IO+0x20]
	strh	w0,[x19,A_IO+0x26]
	strh	w0,[x19,A_IO+0x30]
	strh	w0,[x19,A_IO+0x36]
	str	wzr,[x19,a_affX]
	str	wzr,[x19,a_affX+4]
	str	wzr,[x19,a_affY]
	str	wzr,[x19,a_affY+4]
	mov	w0,0x200
	strh	w0,[x19,A_IO+0x88]
	mov	w0,1
	strb	w0,[x19,A_IO+0x300]
	bl	update_wait
	add	x0,x19,a_sq
	mov	x1,0
.s:	strb	wzr,[x0,x1]
	add	x1,x1,1
	cmp	x1,2*SQ_SIZE+WV_SIZE+NS_SIZE
	b.lo	.s
	mov	w0,0x7FFF
	str	w0,[x19,a_ns+NS_lfsr]
	add	x0,x19,a_waveRam
	mov	x1,32
	bl	mem_zero
	str	wzr,[x19,a_frameSeq]
	str	wzr,[x19,a_fsTimer]
	add	x0,x19,a_fifoLen
	mov	x1,32
	bl	mem_zero
	str	xzr,[x19,a_sampleAcc]
	str	wzr,[x19,a_ahead]
	str	wzr,[x19,a_atail]
	str	xzr,[x19,a_apuLast]
	str	wzr,[x19,a_flashState]
	str	wzr,[x19,a_flashBank]
	strb	wzr,[x19,a_flashId]
	strb	wzr,[x19,a_flashErase]
	strb	wzr,[x19,a_flashWrite]
	strb	wzr,[x19,a_flashBankSel]
	str	wzr,[x19,a_eeBits]
	str	xzr,[x19,a_eeBuf]
	str	wzr,[x19,a_eeState]
	str	wzr,[x19,a_eeCmd]
	str	wzr,[x19,a_eeAddr]
	str	wzr,[x19,a_eeReadPos]
	strb	wzr,[x19,a_evDirty]
	str	x20,[x19,a_now]
	str	w22,[x19,a_cpsr]
	epilogue

wait_ntab:
	db	4,3,2,8

; garde x9-x15
update_wait:
	mov	x17,x30
	io_h	w0,0x204			; w
	add	x1,x19,a_waitN
	mov	x2,0
	mov	w3,1
.a:	str	w3,[x1,x2]
	add	x2,x2,4
	cmp	x2,64*4
	b.lo	.a
	mov	w3,3
	str	w3,[x19,a_waitN+2*4]
	str	w3,[x19,a_waitS+2*4]
	mov	w3,6
	str	w3,[x19,a_waitN32+2*4]
	str	w3,[x19,a_waitS32+2*4]
	mov	w3,2
	str	w3,[x19,a_waitN32+5*4]
	str	w3,[x19,a_waitS32+5*4]
	str	w3,[x19,a_waitN32+6*4]
	str	w3,[x19,a_waitS32+6*4]
	adr	x4,wait_ntab
	ubfx	w5,w0,2,2
	ldrb	w5,[x4,x5]			; N
	mov	w6,2
	mov	w7,1
	tst	w0,0x10
	csel	w6,w7,w6,ne			; S
	mov	x1,8
	bl	.reg
	ubfx	w5,w0,5,2
	ldrb	w5,[x4,x5]
	mov	w6,4
	tst	w0,0x80
	csel	w6,w7,w6,ne
	mov	x1,10
	bl	.reg
	ubfx	w5,w0,8,2
	ldrb	w5,[x4,x5]
	mov	w6,8
	tst	w0,0x400
	csel	w6,w7,w6,ne
	mov	x1,12
	bl	.reg
	and	w5,w0,3
	ldrb	w5,[x4,x5]
	add	w5,w5,1
	str	w5,[x19,a_waitN+14*4]
	str	w5,[x19,a_waitS+14*4]
	str	w5,[x19,a_waitN+15*4]
	str	w5,[x19,a_waitS+15*4]
	str	w5,[x19,a_waitN32+14*4]
	str	w5,[x19,a_waitS32+14*4]
	str	w5,[x19,a_waitN32+15*4]
	str	w5,[x19,a_waitS32+15*4]
	ubfx	w0,w0,14,1
	strb	w0,[x19,a_prefetch]
	br	x17
.reg:	add	w5,w5,1				; x1 = region, deux de suite
	add	w6,w6,1
	add	w2,w5,w6
	lsl	w3,w6,1
	add	x8,x19,a_waitN
	add	x8,x8,x1,lsl 2
	str	w5,[x8]
	str	w5,[x8,4]
	str	w6,[x8,64]
	str	w6,[x8,68]
	add	x8,x8,128
	str	w2,[x8]
	str	w2,[x8,4]
	str	w3,[x8,64]
	str	w3,[x8,68]
	ret

; ---------------------------------------------------------------------------
;  La memoire
; ---------------------------------------------------------------------------

; -> w0 = bus ouvert
open_bus:
	ldr	w0,[x19,a_pipe+4]
	tbz	w22,5,.r
	movl	w1,0x10001
	mul	w0,w0,w1
.r:	ret

; w0 = a -> w0 (a & 0x1FFFF, sans le miroir)
vram_off:
	and	w0,w0,0x1FFFF
	cmp	w0,0x18000
	b.lo	.r
	sub	w0,w0,0x8000
.r:	ret

macro eeprom_here reg_a, lab {		; saute a lab si l'EEPROM repond a cette adresse
	ldr	w2,[x19,a_saveType]
	cmp	w2,SAVE_EEPROM
	b.ne	@f
	lsr	w2,reg_a,24
	cmp	w2,0xD
	b.ne	@f
	ldr	w2,[x19,a_romSize]
	mov	w3,0x1000000
	cmp	w2,w3
	b.ls	lab
	and	w2,reg_a,0x1FFFFFF
	movl	w3,0x1FFFF00
	cmp	w2,w3
	b.hs	lab
@@:
}

; w0 = a -> w0
gba_read8:
	lsr	w1,w0,24
	cmp	w1,16
	b.hs	.open
	jtab	x1,.t
.t:	dw	.r0-.t,.open-.t,.r2-.t,.r3-.t,.r4-.t,.r5-.t,.r6-.t,.r7-.t
	dw	.r8-.t,.r8-.t,.r8-.t,.r8-.t,.r8-.t,.r8-.t,.re-.t,.re-.t
.r0:	cmp	w0,0x4000
	b.hs	.open
	ldr	w1,[x19,a_fetchPC]
	cmp	w1,0x4000
	b.hs	.la
	adm	x1,A_BIOS
	ldrb	w0,[x1,x0]
	ret
.la:	ldr	w1,[x19,a_biosLatch]
	and	w0,w0,3
	lsl	w0,w0,3
	lsr	w0,w1,w0
	and	w0,w0,0xFF
	ret
.open:	mov	w4,w0
	mov	x5,x30
	bl	open_bus
	and	w1,w4,3
	lsl	w1,w1,3
	lsr	w0,w0,w1
	and	w0,w0,0xFF
	br	x5
.r2:	and	w0,w0,0x3FFFF
	add	x1,x19,A_EWRAM
	ldrb	w0,[x1,x0]
	ret
.r3:	and	w0,w0,0x7FFF
	add	x1,x19,A_IWRAM
	ldrb	w0,[x1,x0]
	ret
.r4:	and	w1,w0,0xFFFFFF
	cmp	w1,0x400
	b.hs	.zero
	mov	w4,w0
	and	w0,w0,0x3FE
	stp	x4,x30,[sp,-16]!
	bl	io_read16
	ldp	x4,x30,[sp],16
	and	w1,w4,1
	lsl	w1,w1,3
	lsr	w0,w0,w1
	and	w0,w0,0xFF
	ret
.zero:	mov	w0,0
	ret
.r5:	and	w0,w0,0x3FF
	add	x1,x19,A_PAL
	ldrb	w0,[x1,x0]
	ret
.r6:	mov	x4,x30
	bl	vram_off
	mov	x30,x4
	add	x1,x19,A_VRAM
	ldrb	w0,[x1,x0]
	ret
.r7:	and	w0,w0,0x3FF
	add	x1,x19,A_OAM
	ldrb	w0,[x1,x0]
	ret
.r8:	eeprom_here w0,.ee8
	and	w1,w0,0x1FFFFFF
	ldr	w2,[x19,a_romSize]
	cmp	w1,w2
	b.hs	.nr
	ldr	x2,[x19,a_rom]
	ldrb	w0,[x2,x1]
	ret
.nr:	lsr	w1,w0,1
	and	w0,w0,1
	lsl	w0,w0,3
	lsr	w0,w1,w0
	and	w0,w0,0xFF
	ret
.ee8:	stp	xzr,x30,[sp,-16]!
	bl	eeprom_read
	ldp	x1,x30,[sp],16
	and	w0,w0,0xFF
	ret
.re:	b	save_read8

gba_read16:
	lsr	w1,w0,25
	cmp	w1,7
	b.ne	.a
	stp	xzr,x30,[sp,-16]!
	bl	save_read8
	ldp	x1,x30,[sp],16
	add	w0,w0,w0,lsl 8
	ret
.a:	and	w0,w0,0xFFFFFFFE
	lsr	w1,w0,24
	cmp	w1,16
	b.hs	.open
	jtab	x1,.t
.t:	dw	.r0-.t,.open-.t,.r2-.t,.r3-.t,.r4-.t,.r5-.t,.r6-.t,.r7-.t
	dw	.r8-.t,.r8-.t,.r8-.t,.r8-.t,.r8-.t,.r8-.t,.open-.t,.open-.t
.r0:	cmp	w0,0x4000
	b.hs	.open
	ldr	w1,[x19,a_fetchPC]
	cmp	w1,0x4000
	b.hs	.la
	adm	x1,A_BIOS
	ldrh	w0,[x1,x0]
	ret
.la:	ldr	w1,[x19,a_biosLatch]
	and	w0,w0,2
	lsl	w0,w0,3
	lsr	w0,w1,w0
	and	w0,w0,0xFFFF
	ret
.open:	mov	w4,w0
	mov	x5,x30
	bl	open_bus
	and	w1,w4,2
	lsl	w1,w1,3
	lsr	w0,w0,w1
	and	w0,w0,0xFFFF
	br	x5
.r2:	and	w0,w0,0x3FFFF
	add	x1,x19,A_EWRAM
	ldrh	w0,[x1,x0]
	ret
.r3:	and	w0,w0,0x7FFF
	add	x1,x19,A_IWRAM
	ldrh	w0,[x1,x0]
	ret
.r4:	and	w1,w0,0xFFFFFF
	cmp	w1,0x400
	b.hs	.zero
	and	w0,w0,0x3FE
	b	io_read16
.zero:	mov	w0,0
	ret
.r5:	and	w0,w0,0x3FF
	add	x1,x19,A_PAL
	ldrh	w0,[x1,x0]
	ret
.r6:	mov	x4,x30
	bl	vram_off
	mov	x30,x4
	add	x1,x19,A_VRAM
	ldrh	w0,[x1,x0]
	ret
.r7:	and	w0,w0,0x3FF
	add	x1,x19,A_OAM
	ldrh	w0,[x1,x0]
	ret
.r8:	eeprom_here w0,eeprom_read
	and	w1,w0,0x1FFFFFF
	ldr	w2,[x19,a_romSize]
	cmp	w1,w2
	b.hs	.nr
	ldr	x2,[x19,a_rom]
	ldrh	w0,[x2,x1]
	ret
.nr:	ubfx	w0,w0,1,16
	ret

gba_read32:
	lsr	w1,w0,25
	cmp	w1,7
	b.ne	.a
	stp	xzr,x30,[sp,-16]!
	bl	save_read8
	ldp	x1,x30,[sp],16
	movl	w1,0x01010101
	mul	w0,w0,w1
	ret
.a:	and	w0,w0,0xFFFFFFFC
	lsr	w1,w0,24
	cmp	w1,16
	b.hs	open_bus
	jtab	x1,.t
.t:	dw	.r0-.t,open_bus-.t,.r2-.t,.r3-.t,.r4-.t,.r5-.t,.r6-.t,.r7-.t
	dw	.r8-.t,.r8-.t,.r8-.t,.r8-.t,.r8-.t,.r8-.t,open_bus-.t,open_bus-.t
.r0:	cmp	w0,0x4000
	b.hs	open_bus
	ldr	w1,[x19,a_fetchPC]
	cmp	w1,0x4000
	b.hs	.la
	adm	x1,A_BIOS
	ldr	w0,[x1,x0]
	ret
.la:	ldr	w0,[x19,a_biosLatch]
	ret
.r2:	and	w0,w0,0x3FFFF
	add	x1,x19,A_EWRAM
	ldr	w0,[x1,x0]
	ret
.r3:	and	w0,w0,0x7FFF
	add	x1,x19,A_IWRAM
	ldr	w0,[x1,x0]
	ret
.r4:	and	w1,w0,0xFFFFFF
	cmp	w1,0x400
	b.hs	.zero
	and	w0,w0,0x3FC
	stp	x0,x30,[sp,-16]!
	bl	io_read16
	ldr	w1,[sp]
	str	w0,[sp]
	add	w0,w1,2
	bl	io_read16
	ldp	x1,x30,[sp],16
	and	w1,w1,0xFFFF
	orr	w0,w1,w0,lsl 16
	ret
.zero:	mov	w0,0
	ret
.r5:	and	w0,w0,0x3FF
	add	x1,x19,A_PAL
	ldr	w0,[x1,x0]
	ret
.r6:	mov	x4,x30
	bl	vram_off
	mov	x30,x4
	add	x1,x19,A_VRAM
	ldr	w0,[x1,x0]
	ret
.r7:	and	w0,w0,0x3FF
	add	x1,x19,A_OAM
	ldr	w0,[x1,x0]
	ret
.r8:	and	w1,w0,0x1FFFFFF
	ldr	w2,[x19,a_romSize]
	add	w3,w1,3
	cmp	w3,w2
	b.hs	.nr
	ldr	x2,[x19,a_rom]
	ldr	w0,[x2,x1]
	ret
.nr:	ubfx	w1,w0,1,16
	add	w0,w0,2
	ubfx	w0,w0,1,16
	orr	w0,w1,w0,lsl 16
	ret

; w0 = a, w1 = v
gba_write8:
	and	w1,w1,0xFF
	lsr	w2,w0,24
	cmp	w2,16
	b.hs	.r
	jtab	x2,.t
.t:	dw	.r-.t,.r-.t,.w2-.t,.w3-.t,.w4-.t,.w5-.t,.w6-.t,.r-.t
	dw	.r-.t,.r-.t,.r-.t,.r-.t,.r-.t,.r-.t,.we-.t,.we-.t
.r:	ret
.w2:	and	w0,w0,0x3FFFF
	add	x2,x19,A_EWRAM
	strb	w1,[x2,x0]
	ret
.w3:	and	w0,w0,0x7FFF
	add	x2,x19,A_IWRAM
	strb	w1,[x2,x0]
	ret
.w4:	and	w2,w0,0xFFFFFF
	cmp	w2,0x400
	b.hs	.r
	and	w0,w0,0x3FF
	b	io_write8
.w5:	and	w0,w0,0x3FE
	add	x2,x19,A_PAL
	add	w1,w1,w1,lsl 8
	strh	w1,[x2,x0]
	ret
.w6:	mov	x4,x30
	bl	vram_off
	mov	x30,x4
	io_b	w2,0
	and	w2,w2,7
	mov	w3,0x10000
	cmp	w2,3
	b.lo	.l
	movl	w3,0x14000
.l:	cmp	w0,w3
	b.hs	.r
	and	w0,w0,0xFFFFFFFE
	add	x2,x19,A_VRAM
	add	w1,w1,w1,lsl 8
	strh	w1,[x2,x0]
	ret
.we:	b	save_write8

gba_write16:
	and	w1,w1,0xFFFF
	lsr	w2,w0,25
	cmp	w2,7
	b.ne	.a
.sv:	and	w2,w0,1
	lsl	w2,w2,3
	lsr	w1,w1,w2
	and	w1,w1,0xFF
	b	save_write8
.a:	mov	w4,w0
	and	w0,w0,0xFFFFFFFE
	lsr	w2,w0,24
	cmp	w2,16
	b.hs	.r
	jtab	x2,.t
.t:	dw	.r-.t,.r-.t,.w2-.t,.w3-.t,.w4-.t,.w5-.t,.w6-.t,.w7-.t
	dw	.r-.t,.r-.t,.r-.t,.r-.t,.r-.t,.wd-.t,.we-.t,.we-.t
.r:	ret
.w2:	and	w0,w0,0x3FFFF
	add	x2,x19,A_EWRAM
	strh	w1,[x2,x0]
	ret
.w3:	and	w0,w0,0x7FFF
	add	x2,x19,A_IWRAM
	strh	w1,[x2,x0]
	ret
.w4:	and	w2,w0,0xFFFFFF
	cmp	w2,0x400
	b.hs	.r
	and	w0,w0,0x3FE
	b	io_write16
.w5:	and	w0,w0,0x3FF
	add	x2,x19,A_PAL
	strh	w1,[x2,x0]
	ret
.w6:	mov	x4,x30
	bl	vram_off
	mov	x30,x4
	add	x2,x19,A_VRAM
	strh	w1,[x2,x0]
	ret
.w7:	and	w0,w0,0x3FF
	add	x2,x19,A_OAM
	strh	w1,[x2,x0]
	ret
.wd:	ldr	w2,[x19,a_saveType]
	cmp	w2,SAVE_EEPROM
	b.ne	.r
	ldr	w2,[x19,a_romSize]
	mov	w3,0x1000000
	cmp	w2,w3
	b.ls	.ee
	and	w2,w0,0x1FFFFFF
	movl	w3,0x1FFFF00
	cmp	w2,w3
	b.lo	.r
.ee:	mov	w0,w1
	b	eeprom_write
.we:	mov	w0,w4				; (a & ~1 : deja fait dans le C++)
	and	w0,w0,0xFFFFFFFE
	b	.sv

gba_write32:
	lsr	w2,w0,25
	cmp	w2,7
	b.ne	.a
.sv:	and	w2,w0,3
	lsl	w2,w2,3
	lsr	w1,w1,w2
	and	w1,w1,0xFF
	b	save_write8
.a:	and	w0,w0,0xFFFFFFFC
	lsr	w2,w0,24
	cmp	w2,16
	b.hs	.r
	jtab	x2,.t
.t:	dw	.r-.t,.r-.t,.w2-.t,.w3-.t,.w4-.t,.w5-.t,.w6-.t,.w7-.t
	dw	.r-.t,.r-.t,.r-.t,.r-.t,.r-.t,.r-.t,.sv-.t,.sv-.t
.r:	ret
.w2:	and	w0,w0,0x3FFFF
	add	x2,x19,A_EWRAM
	str	w1,[x2,x0]
	ret
.w3:	and	w0,w0,0x7FFF
	add	x2,x19,A_IWRAM
	str	w1,[x2,x0]
	ret
.w4:	and	w2,w0,0xFFFFFF
	cmp	w2,0x400
	b.hs	.r
	and	w0,w0,0x3FC
	stp	x0,x1,[sp,-16]!
	str	x30,[sp,-16]!
	and	w1,w1,0xFFFF
	bl	io_write16
	ldr	x30,[sp],16
	ldp	x0,x1,[sp],16
	add	w0,w0,2
	lsr	w1,w1,16
	b	io_write16
.w5:	and	w0,w0,0x3FF
	add	x2,x19,A_PAL
	str	w1,[x2,x0]
	ret
.w6:	mov	x4,x30
	bl	vram_off
	mov	x30,x4
	add	x2,x19,A_VRAM
	str	w1,[x2,x0]
	ret
.w7:	and	w0,w0,0x3FF
	add	x2,x19,A_OAM
	str	w1,[x2,x0]
	ret

; ---------------------------------------------------------------------------
;  Les registres
; ---------------------------------------------------------------------------

; w0 = off -> w0
io_read16:
	cmp	w0,0x60
	b.lo	.plain0
	cmp	w0,0xA8
	b.hs	.n1
	stp	x0,x30,[sp,-16]!
	bl	apu_read
	ldr	w1,[sp]
	str	w0,[sp]
	add	w0,w1,1
	bl	apu_read
	ldp	x1,x30,[sp],16
	and	w1,w1,0xFF
	orr	w0,w1,w0,lsl 8
	ret
.plain0:
	cmp	w0,4
	b.eq	.stat
	cmp	w0,6
	b.eq	.vc
.plain:	add	x1,x19,A_IO
	ldrh	w0,[x1,x0]
	ret
.n1:	cmp	w0,0xB0
	b.lo	.plain
	cmp	w0,0xE0
	b.hs	.n2
	cmp	w0,0xBA
	b.eq	.dm
	cmp	w0,0xC6
	b.eq	.dm
	cmp	w0,0xD2
	b.eq	.dm
	cmp	w0,0xDE
	b.eq	.dm
	mov	w0,0
	ret
.dm:	sub	w0,w0,0xBA
	mov	w1,12
	udiv	w0,w0,w1
	add	x1,x19,a_dma
	add	x1,x1,x0,lsl 4
	ldrh	w0,[x1,D_ctl]
	ret
.n2:	sub	w1,w0,0x100
	cmp	w1,0x10
	b.hs	.n3
	lsr	w2,w1,2				; i
	tbnz	w1,1,.tc
	mov	w0,w2
	b	timer_read
.tc:	add	x1,x19,a_timers
	add	x1,x1,x2,lsl 4
	ldrh	w0,[x1,T_ctl]
	ret
.n3:	cmp	w0,0x130
	b.ne	.plain
	ldr	w0,[x19,a_keys]
	mvn	w0,w0
	and	w0,w0,0x3FF
	ret
.stat:	io_h	w0,4
	movl	w1,0xFF38
	and	w0,w0,w1
	ldr	w1,[x19,a_vcount]
	sub	w2,w1,160
	cmp	w2,67
	b.hs	.s1
	orr	w0,w0,1
.s1:	ldrb	w2,[x19,a_inHblank]
	cbz	w2,.s2
	orr	w0,w0,2
.s2:	io_b	w2,5
	cmp	w1,w2
	b.ne	.s3
	orr	w0,w0,4
.s3:	ret
.vc:	ldr	w0,[x19,a_vcount]
	ret

; w0 = off, w1 = v
io_write8:
	sub	w2,w0,0x60
	cmp	w2,0x48
	b.hs	.n1
	stp	x0,x1,[sp,-16]!
	str	x30,[sp,-16]!
	bl	apu_run
	ldr	x30,[sp],16
	ldp	x0,x1,[sp],16
	cmp	w0,0xA0
	b.lo	apu_write
	cmp	w0,0xA4
	cset	w0,hs
	b	fifo_push
.n1:	cmp	w0,0x202
	b.eq	.if
	cmp	w0,0x203
	b.ne	.n2
.if:	add	x2,x19,A_IO
	ldrb	w3,[x2,x0]
	bic	w3,w3,w1
	strb	w3,[x2,x0]
	b	check_irq
.n2:	cmp	w0,0x301
	b.ne	.n3
	mov	w2,1
	strb	w2,[x19,a_halted]
	strb	w1,[x19,A_IO+0x301]
	ret
.n3:	and	w2,w0,0xFFFFFFFE		; base
	add	x3,x19,A_IO
	ldrh	w3,[x3,x2]
	tbnz	w0,0,.h
	bfxil	w3,w1,0,8
	b	.w
.h:	bfi	w3,w1,8,8
.w:	mov	w0,w2
	mov	w1,w3
	b	io_write16

; w0 = off, w1 = v (16 bits)
io_write16:
	sub	w2,w0,0x60
	cmp	w2,0x48
	b.hs	.n1
	stp	x0,x1,[sp,-16]!
	str	x30,[sp,-16]!
	bl	apu_run
	ldr	x30,[sp],16
	ldp	x0,x1,[sp],16
	cmp	w0,0xA0
	b.lo	.psg
	stp	x9,x10,[sp,-16]!
	str	x30,[sp,-16]!
	mov	w9,w1
	cmp	w0,0xA4
	cset	w10,hs
	mov	w0,w10
	and	w1,w9,0xFF
	bl	fifo_push
	mov	w0,w10
	lsr	w1,w9,8
	bl	fifo_push
	ldr	x30,[sp],16
	ldp	x9,x10,[sp],16
	ret
.psg:	stp	x0,x1,[sp,-16]!
	str	x30,[sp,-16]!
	and	w1,w1,0xFF
	bl	apu_write
	ldr	x30,[sp],16
	ldp	x0,x1,[sp],16
	add	w0,w0,1
	lsr	w1,w1,8
	b	apu_write
.n1:	cmp	w0,4
	b.ne	.n2
	io_h	w2,4
	and	w2,w2,7
	and	w1,w1,0xFFF8
	orr	w1,w1,w2
	strh	w1,[x19,A_IO+4]
	ret
.n2:	cmp	w0,6
	b.eq	.ret
	sub	w2,w0,0x28
	cmp	w2,0x18
	b.lo	.aff
.n3:	sub	w2,w0,0x100
	cmp	w2,0x10
	b.hs	.n4
	lsr	w0,w2,2
	ubfx	w2,w2,1,1
	b	timer_write
.n4:	cmp	w0,0xBA
	b.eq	.dm
	cmp	w0,0xC6
	b.eq	.dm
	cmp	w0,0xD2
	b.eq	.dm
	cmp	w0,0xDE
	b.ne	.n5
.dm:	sub	w0,w0,0xBA
	mov	w2,12
	udiv	w0,w0,w2
	b	dma_write
.n5:	cmp	w0,0x130
	b.eq	.ret
	cmp	w0,0x200
	b.ne	.n6
	strh	w1,[x19,A_IO+0x200]
	b	check_irq
.n6:	cmp	w0,0x202
	b.ne	.n7
	io_h	w2,0x202
	bic	w2,w2,w1
	strh	w2,[x19,A_IO+0x202]
	b	check_irq
.n7:	cmp	w0,0x204
	b.ne	.n8
	strh	w1,[x19,A_IO+0x204]
	b	update_wait
.n8:	cmp	w0,0x208
	b.ne	.n9
	strh	w1,[x19,A_IO+0x208]
	b	check_irq
.n9:	cmp	w0,0x300
	b.ne	.n10
	strb	w1,[x19,A_IO+0x300]
	lsr	w2,w1,8
	cbz	w2,.ret
	strb	w2,[x19,A_IO+0x301]
	mov	w2,1
	strb	w2,[x19,a_halted]
	ret
.n10:	add	x2,x19,A_IO
	strh	w1,[x2,x0]
.ret:	ret
.aff:	cmp	w2,8
	b.lo	.a0
	cmp	w2,0x10
	b.lo	.n3
	; 0x38-0x3E
	add	x3,x19,A_IO
	strh	w1,[x3,x0]
	and	w4,w0,0xFFFFFFFC		; 0x38, 0x3C
	ldr	w5,[x3,x4]
	sbfx	w5,w5,0,28
	cmp	w4,0x38
	b.ne	.a1y
	str	w5,[x19,a_affX+4]
	ret
.a1y:	str	w5,[x19,a_affY+4]
	ret
.a0:	add	x3,x19,A_IO
	strh	w1,[x3,x0]
	and	w4,w0,0xFFFFFFFC
	ldr	w5,[x3,x4]
	sbfx	w5,w5,0,28
	cmp	w4,0x28
	b.ne	.a0y
	str	w5,[x19,a_affX]
	ret
.a0y:	str	w5,[x19,a_affY]
	ret

; garde tout sauf x0-x3
check_irq:
	io_b	w0,0x208
	and	w0,w0,1
	io_h	w1,0x200
	io_h	w2,0x202
	tst	w1,w2
	csel	w0,wzr,w0,eq
	strb	w0,[x19,a_irqLine]
	ret

raise_irq:				; w0 = bit
	mov	w1,1
	lsl	w1,w1,w0
	io_h	w2,0x202
	orr	w2,w2,w1
	strh	w2,[x19,A_IO+0x202]
	b	check_irq

; ---------------------------------------------------------------------------
;  Les minuteurs
; ---------------------------------------------------------------------------

timer_shift:				; PRESCALE, en decalages
	db	0,6,8,10

timers_update:
	push_x9
	mov	w9,0				; i
.l:	add	x10,x19,a_timers
	add	x10,x10,x9,lsl 4		; t
	ldrh	w0,[x10,T_ctl]
	tbz	w0,7,.n
	cbz	w9,.a
	tbnz	w0,2,.n
.a:	and	w0,w0,3
	adr	x1,timer_shift
	ldrb	w13,[x1,x0]			; decalage
	ldr	x1,[x10,T_last]
	cmp	x20,x1
	b.ls	.n
	sub	x11,x20,x1
	lsr	x11,x11,x13			; ticks
	lsl	x2,x11,x13
	add	x1,x1,x2
	str	x1,[x10,T_last]
	ldrh	w12,[x10,T_counter]		; c
.w:	cbz	x11,.e
	mov	w0,0x10000
	sub	w0,w0,w12			; room
	cmp	x11,x0
	b.hs	.o
	add	w12,w12,w11
	mov	x11,0
	b	.w
.o:	sub	x11,x11,x0
	ldrh	w12,[x10,T_reload]
	strh	w12,[x10,T_counter]
	mov	w0,w9
	bl	timer_overflow
	ldrh	w12,[x10,T_counter]
	b	.w
.e:	strh	w12,[x10,T_counter]
.n:	add	w9,w9,1
	cmp	w9,4
	b.lo	.l
	pop_x9
	ret

; -> x0 = le prochain debordement utile
timers_next:
	mov	x0,-1
	mov	x1,0				; i
.l:	add	x2,x19,a_timers
	add	x2,x2,x1,lsl 4
	ldrh	w3,[x2,T_ctl]
	tbz	w3,7,.n
	cbz	x1,.a
	tbnz	w3,2,.n
.a:	tbnz	w3,6,.u
	cmp	x1,2
	b.lo	.u
	b.hi	.n
	ldrh	w4,[x2,T_SIZE+T_ctl]
	mov	w5,0x84
	and	w4,w4,w5
	cmp	w4,0x84
	b.ne	.n
.u:	ldrh	w4,[x2,T_counter]
	mov	w5,0x10000
	sub	w4,w5,w4
	and	w3,w3,3
	adr	x5,timer_shift
	ldrb	w3,[x5,x3]
	lsl	x4,x4,x3
	ldr	x5,[x2,T_last]
	add	x4,x4,x5
	cmp	x4,x0
	csel	x0,x4,x0,lo
.n:	add	x1,x1,1
	cmp	x1,4
	b.lo	.l
	ret

; w0 = i
timer_overflow:
	stp	x9,x30,[sp,-16]!
	mov	w9,w0
	add	x1,x19,a_timers
	add	x1,x1,x9,lsl 4
	ldrh	w1,[x1,T_ctl]
	tbz	w1,6,.a
	add	w0,w9,3
	bl	raise_irq
.a:	cmp	w9,2
	b.hs	.b
	mov	w0,w9
	bl	fifo_timer
.b:	cmp	w9,3
	b.hs	.f
	add	x1,x19,a_timers
	add	x1,x1,x9,lsl 4
	add	x1,x1,T_SIZE			; n
	ldrh	w2,[x1,T_ctl]
	mov	w3,0x84
	and	w2,w2,w3
	cmp	w2,0x84
	b.ne	.f
	ldrh	w2,[x1,T_counter]
	add	w2,w2,1
	strh	w2,[x1,T_counter]
	tst	w2,0xFFFF
	b.ne	.f
	ldrh	w2,[x1,T_reload]
	strh	w2,[x1,T_counter]
	add	w0,w9,1
	bl	timer_overflow
.f:	ldp	x9,x30,[sp],16
	ret

timer_read:				; w0 = i -> w0
	stp	x0,x30,[sp,-16]!
	bl	timers_update
	ldp	x0,x30,[sp],16
	add	x1,x19,a_timers
	add	x1,x1,x0,lsl 4
	ldrh	w0,[x1,T_counter]
	ret

; w0 = i, w1 = v, w2 = ctl
timer_write:
	stp	x0,x1,[sp,-16]!
	stp	x2,x30,[sp,-16]!
	bl	timers_update
	ldp	x2,x30,[sp],16
	ldp	x0,x1,[sp],16
	add	x3,x19,a_timers
	add	x3,x3,x0,lsl 4			; t
	cbnz	w2,.c
	strh	w1,[x3,T_reload]
	ret
.c:	ldrh	w4,[x3,T_ctl]
	mov	w5,0xC7
	and	w5,w1,w5
	strh	w5,[x3,T_ctl]
	mov	w2,1
	strb	w2,[x19,a_evDirty]
	tbz	w1,7,.r
	tbnz	w4,7,.m
	ldrh	w0,[x3,T_reload]
	strh	w0,[x3,T_counter]
	str	x20,[x3,T_last]
	ret
.m:	and	w5,w5,3
	adr	x0,timer_shift
	ldrb	w5,[x0,x5]
	ldr	x0,[x3,T_last]
	sub	x0,x20,x0			; (now - last) % ps
	mov	x1,1
	lsl	x1,x1,x5
	sub	x1,x1,1
	and	x0,x0,x1
	sub	x0,x20,x0
	str	x0,[x3,T_last]
.r:	ret

; ---------------------------------------------------------------------------
;  DMA
; ---------------------------------------------------------------------------

; w0 = ch -> w1 = cnt rechargee (perd x3)
dma_count:
	add	w1,w0,w0,lsl 1
	lsl	w1,w1,2
	add	w1,w1,0xB8
	add	x3,x19,A_IO
	ldrh	w1,[x3,x1]
	cbnz	w1,.r
	mov	w1,0x4000
	cmp	w0,3
	b.ne	.r
	mov	w1,0x10000
.r:	ret

; w0 = ch, w1 = v
dma_write:
	add	x2,x19,a_dma
	add	x2,x2,x0,lsl 4			; d
	ldrh	w3,[x2,D_ctl]			; was
	strh	w1,[x2,D_ctl]
	tbnz	w1,15,.on
	strb	wzr,[x2,D_on]
	ret
.on:	tbz	w3,15,.st
	ret
.st:	add	w3,w0,w0,lsl 1
	lsl	w3,w3,2
	add	w3,w3,0xB0			; base
	add	x4,x19,A_IO
	ldr	w5,[x4,x3]
	movl	w6,0x07FFFFFF
	movl	w7,0x0FFFFFFF
	cmp	w0,0
	csel	w8,w6,w7,eq
	and	w5,w5,w8
	str	w5,[x2,D_src]
	add	w3,w3,4
	ldr	w5,[x4,x3]
	cmp	w0,3
	csel	w8,w7,w6,eq
	and	w5,w5,w8
	str	w5,[x2,D_dst]
	mov	w7,w1				; v
	mov	x8,x30
	bl	dma_count
	mov	x30,x8
	str	w1,[x2,D_cnt]
	mov	w3,1
	strb	w3,[x2,D_on]
	cmp	w0,3
	b.ne	.g
	ldr	w3,[x19,a_saveType]
	cmp	w3,SAVE_EEPROM
	b.ne	.g
	lsr	w3,w5,24
	cmp	w3,0xD
	b.ne	.g
	ldr	w3,[x19,a_eeAddrBits]
	cbnz	w3,.g
	cmp	w1,9
	b.eq	.e6
	cmp	w1,73
	b.eq	.e6
	cmp	w1,17
	b.eq	.e14
	cmp	w1,81
	b.ne	.g
.e14:	mov	w3,14
	str	w3,[x19,a_eeAddrBits]
	mov	w3,0x2000
	str	w3,[x19,a_saveSize]
	b	.g
.e6:	mov	w3,6
	str	w3,[x19,a_eeAddrBits]
	mov	w3,512
	str	w3,[x19,a_saveSize]
.g:	ubfx	w3,w7,12,2
	cbz	w3,dma_run
	ret

; w0 = timing
dma_start:
	stp	x9,x10,[sp,-16]!
	str	x30,[sp,-16]!
	mov	w9,w0
	mov	w10,0				; ch
.l:	add	x1,x19,a_dma
	add	x1,x1,x10,lsl 4
	ldrb	w2,[x1,D_on]
	cbz	w2,.n
	ldrh	w2,[x1,D_ctl]
	ubfx	w2,w2,12,2
	cmp	w2,w9
	b.ne	.n
	cmp	w9,3
	b.ne	.r
	cmp	w10,1
	b.eq	.n
	cmp	w10,2
	b.eq	.n
.r:	mov	w0,w10
	bl	dma_run
.n:	add	w10,w10,1
	cmp	w10,4
	b.lo	.l
	ldr	x30,[sp],16
	ldp	x9,x10,[sp],16
	ret

; w0 = ch
dma_run:
	push_x9
	sub	sp,sp,32
	mov	w9,w0				; ch
	add	x10,x19,a_dma
	add	x10,x10,x9,lsl 4		; d
	ldrh	w11,[x10,D_ctl]			; c
	ubfx	w12,w11,10,1			; word
	ubfx	w13,w11,5,2			; dstMode
	ubfx	w14,w11,7,2			; srcMode
	ldr	w15,[x10,D_cnt]			; n
	ubfx	w0,w11,12,2
	cmp	w0,3
	b.ne	.nf
	cmp	w9,1
	b.eq	.fi
	cmp	w9,2
	b.ne	.nf
.fi:	mov	w15,4
	mov	w12,1
	mov	w13,2
.nf:	str	w13,[sp]			; dstMode
	mov	w1,2
	lsl	w1,w1,w12			; step
	mov	w2,0				; sInc
	cbnz	w14,.s1
	mov	w2,w1
	b	.s2
.s1:	cmp	w14,1
	b.ne	.s2
	neg	w2,w1
.s2:	mov	w3,0				; dInc
	cbz	w13,.d0
	cmp	w13,3
	b.eq	.d0
	cmp	w13,1
	b.ne	.d2
	neg	w3,w1
	b	.d2
.d0:	mov	w3,w1
.d2:	str	w2,[sp,4]
	str	w3,[sp,8]
	ldr	w13,[x10,D_src]			; s
	ldr	w14,[x10,D_dst]			; t
	ubfx	w0,w13,24,4
	ubfx	w1,w14,24,4
	add	x2,x19,a_waitS
	cbz	w12,.ws
	add	x2,x19,a_waitS32
.ws:	ldr	w0,[x2,x0,lsl 2]
	ldr	w1,[x2,x1,lsl 2]
	add	w0,w0,w1
	str	w0,[sp,12]			; attente par unite
	str	w15,[sp,16]			; n
.xfer:	cbz	w15,.end
	cbz	w12,.h
	and	w0,w13,0xFFFFFFFC
	bl	gba_read32
	mov	w1,w0
	and	w0,w14,0xFFFFFFFC
	bl	gba_write32
	b	.nx
.h:	and	w0,w13,0xFFFFFFFE
	bl	gba_read16
	mov	w1,w0
	and	w0,w14,0xFFFFFFFE
	bl	gba_write16
.nx:	ldr	w0,[sp,4]
	add	w13,w13,w0
	ldr	w0,[sp,8]
	add	w14,w14,w0
	sub	w15,w15,1
	b	.xfer
.end:	ldr	w0,[sp,16]
	ldr	w1,[sp,12]
	mul	x0,x0,x1
	add	x0,x0,2
	add	x20,x20,x0
	str	w13,[x10,D_src]
	ldr	w15,[sp]			; dstMode
	cmp	w15,3
	b.eq	.nd
	str	w14,[x10,D_dst]
.nd:	tbz	w11,14,.ni
	add	w0,w9,8
	bl	raise_irq
.ni:	tbz	w11,9,.off
	ubfx	w0,w11,12,2
	cbz	w0,.off
	mov	w0,w9
	bl	dma_count
	str	w1,[x10,D_cnt]
	cmp	w15,3
	b.ne	.f
	add	w1,w9,w9,lsl 1
	lsl	w1,w1,2
	add	w1,w1,0xB4
	add	x2,x19,A_IO
	ldr	w1,[x2,x1]
	movl	w2,0x07FFFFFF
	movl	w3,0x0FFFFFFF
	cmp	w9,3
	csel	w2,w3,w2,eq
	and	w1,w1,w2
	str	w1,[x10,D_dst]
	b	.f
.off:	strb	wzr,[x10,D_on]
	ldrh	w0,[x10,D_ctl]
	and	w0,w0,0x7FFF
	strh	w0,[x10,D_ctl]
.f:	add	sp,sp,32
	pop_x9
	ret

; ---------------------------------------------------------------------------
;  L'image, le temps
; ---------------------------------------------------------------------------

ppu_event:
	prologue
	stp	x9,x10,[sp,-16]!
	io_h	w9,4				; stat
	ldrb	w0,[x19,a_inHblank]
	cbnz	w0,.e
	mov	w0,1
	strb	w0,[x19,a_inHblank]
	ldr	w0,[x19,a_vcount]
	cmp	w0,160
	b.ge	.a
	str	x9,[sp,-16]!
	bl	render_line
	ldr	x9,[sp],16
	mov	w0,2
	bl	dma_start
.a:	tbz	w9,4,.f
	mov	w0,1
	bl	raise_irq
	b	.f
.e:	strb	wzr,[x19,a_inHblank]
	ldr	x0,[x19,a_lineStart]
	add	x0,x0,1232
	str	x0,[x19,a_lineStart]
	ldr	w10,[x19,a_vcount]
	cmp	w10,160
	b.ge	.b
	ldrsh	w1,[x19,A_IO+0x22]
	ldr	w0,[x19,a_affX]
	add	w0,w0,w1
	str	w0,[x19,a_affX]
	ldrsh	w1,[x19,A_IO+0x26]
	ldr	w0,[x19,a_affY]
	add	w0,w0,w1
	str	w0,[x19,a_affY]
	ldrsh	w1,[x19,A_IO+0x32]
	ldr	w0,[x19,a_affX+4]
	add	w0,w0,w1
	str	w0,[x19,a_affX+4]
	ldrsh	w1,[x19,A_IO+0x36]
	ldr	w0,[x19,a_affY+4]
	add	w0,w0,w1
	str	w0,[x19,a_affY+4]
.b:	add	w10,w10,1
	cmp	w10,228
	csel	w10,wzr,w10,eq
	str	w10,[x19,a_vcount]
	cmp	w10,160
	b.ne	.c
	tbz	w9,3,.b1
	mov	w0,0
	bl	raise_irq
.b1:	mov	w0,1
	bl	dma_start
	bl	latch_affine
	mov	w0,1
	strb	w0,[x19,a_frameDone]
.c:	lsr	w0,w9,8
	cmp	w10,w0
	b.ne	.f
	tbz	w9,5,.f
	mov	w0,2
	bl	raise_irq
.f:	ldp	x9,x10,[sp],16
	epilogue

latch_affine:
	ldr	w0,[x19,A_IO+0x28]
	sbfx	w0,w0,0,28
	str	w0,[x19,a_affX]
	ldr	w0,[x19,A_IO+0x2C]
	sbfx	w0,w0,0,28
	str	w0,[x19,a_affY]
	ldr	w0,[x19,A_IO+0x38]
	sbfx	w0,w0,0,28
	str	w0,[x19,a_affX+4]
	ldr	w0,[x19,A_IO+0x3C]
	sbfx	w0,w0,0,28
	str	w0,[x19,a_affY+4]
	ret

; -> x0 = nextPpu ()
macro next_ppu {
	ldr	x0,[x19,a_lineStart]
	mov	w1,960
	mov	w2,1232
	ldrb	w3,[x19,a_inHblank]
	cmp	w3,0
	csel	w1,w2,w1,ne
	add	x0,x0,x1
}

run_frame:
	prologue
	strb	wzr,[x19,a_frameDone]
rf_loop:
	ldrb	w0,[x19,a_frameDone]
	cbnz	w0,rf_fin
	bl	timers_next
	mov	x4,x0
	next_ppu
	cmp	x4,x0
	csel	x0,x4,x0,lo
	str	x0,[x19,a_target]
step_next:
	ldr	x0,[x19,a_target]
	cmp	x20,x0
	b.hs	rf_ev
	ldrh	w1,[x19,a_halted]		; et irqLine
	cbnz	w1,rf_sp
step_go:
  if defined GBA_TRACE
	bl	gba_trace
  end if
	tbnz	w22,5,thumb_step
	b	arm_step
rf_sp:	ldrb	w1,[x19,a_halted]
	cbz	w1,.i
	io_h	w1,0x200
	io_h	w2,0x202
	tst	w1,w2
	b.ne	.w
	mov	x20,x0
	b	rf_ev
.w:	strb	wzr,[x19,a_halted]
.i:	ldrb	w1,[x19,a_irqLine]
	cbz	w1,step_go
	tbnz	w22,7,step_go
	bl	cpu_irq
	b	step_go
rf_ev:	next_ppu
	cmp	x20,x0
	b.lo	.tu
	bl	ppu_event
.tu:	bl	timers_update
	b	rf_loop
rf_fin:	bl	apu_run
	epilogue

; la fin d'un code : w21 = ses cycles
step_done:
	add	x20,x20,x21
	ldrb	w0,[x19,a_evDirty]
	cbz	w0,step_next
	strb	wzr,[x19,a_evDirty]
	b	rf_ev

; ---------------------------------------------------------------------------
;  Les sauvegardes
; ---------------------------------------------------------------------------

macro flash_kind reg, lab {		; saute a lab si ce n'est pas de la Flash
	ldr	reg,[x19,a_saveType]
	sub	reg,reg,SAVE_FLASH64
	cmp	reg,1
	b.hi	lab
}

; w0 = a -> w0
save_read8:
	ldr	w1,[x19,a_saveType]
	cmp	w1,SAVE_SRAM
	b.ne	.f
	and	w0,w0,0x7FFF
	adm	x1,A_SAVE
	ldrb	w0,[x1,x0]
	ret
.f:	flash_kind w2,.ff
	and	w0,w0,0xFFFF
	ldrb	w2,[x19,a_flashId]
	cbz	w2,.m
	cmp	w0,2
	b.hs	.m
	cmp	w1,SAVE_FLASH128
	b.ne	.p
	cbz	w0,.s1
	mov	w0,0x13
	ret
.s1:	mov	w0,0x62
	ret
.p:	cbz	w0,.s2
	mov	w0,0x1B
	ret
.s2:	mov	w0,0x32
	ret
.m:	ldr	w2,[x19,a_flashBank]
	orr	w0,w0,w2,lsl 16
	adm	x1,A_SAVE
	ldrb	w0,[x1,x0]
	ret
.ff:	mov	w0,0xFF
	ret

; w0 = a, w1 = v
save_write8:
	ldr	w2,[x19,a_saveType]
	cmp	w2,SAVE_SRAM
	b.ne	.f
	and	w0,w0,0x7FFF
	adm	x2,A_SAVE
	strb	w1,[x2,x0]
	mov	w0,1
	strb	w0,[x19,a_saveDirty]
	ret
.f:	flash_kind w3,.r
	and	w0,w0,0xFFFF			; off
	ldrb	w3,[x19,a_flashWrite]
	cbz	w3,.a
	ldr	w3,[x19,a_flashBank]
	orr	w0,w0,w3,lsl 16
	adm	x3,A_SAVE
	strb	w1,[x3,x0]
	strb	wzr,[x19,a_flashWrite]
	mov	w0,1
	strb	w0,[x19,a_saveDirty]
	ret
.a:	ldrb	w3,[x19,a_flashBankSel]
	cbz	w3,.b
	cbnz	w0,.a1
	and	w1,w1,1
	str	w1,[x19,a_flashBank]
.a1:	strb	wzr,[x19,a_flashBankSel]
	ret
.b:	ldr	w3,[x19,a_flashState]
	cbnz	w3,.s1
	movl	w3,0x5555
	cmp	w0,w3
	b.ne	.r
	cmp	w1,0xAA
	b.ne	.r
	mov	w3,1
	str	w3,[x19,a_flashState]
.r:	ret
.s1:	cmp	w3,1
	b.ne	.s2
	mov	w3,0
	movl	w4,0x2AAA
	cmp	w0,w4
	b.ne	.s1e
	cmp	w1,0x55
	b.ne	.s1e
	mov	w3,2
.s1e:	str	w3,[x19,a_flashState]
	ret
.s2:	str	wzr,[x19,a_flashState]
	ldrb	w3,[x19,a_flashErase]
	cbz	w3,.s3
	cmp	w1,0x30
	b.ne	.s3
	ldr	w3,[x19,a_flashBank]
	lsl	w3,w3,16
	and	w0,w0,0xF000
	add	w3,w3,w0
	adm	x4,A_SAVE
	add	x4,x4,x3
	mov	w1,0x1000
	mov	w5,0xFF
.e1:	strb	w5,[x4],1
	subs	w1,w1,1
	b.ne	.e1
	b	.done
.s3:	movl	w3,0x5555
	cmp	w0,w3
	b.ne	.r
	cmp	w1,0x90
	b.ne	.c1
	mov	w3,1
	strb	w3,[x19,a_flashId]
	ret
.c1:	cmp	w1,0xF0
	b.ne	.c2
	strb	wzr,[x19,a_flashId]
	ret
.c2:	cmp	w1,0x80
	b.ne	.c3
	mov	w3,1
	strb	w3,[x19,a_flashErase]
	ret
.c3:	cmp	w1,0x10
	b.ne	.c4
	ldrb	w3,[x19,a_flashErase]
	cbz	w3,.c4
	ldr	w3,[x19,a_saveSize]
	adm	x4,A_SAVE
	mov	w5,0xFF
.e2:	cbz	w3,.done
	strb	w5,[x4],1
	sub	w3,w3,1
	b	.e2
.done:	strb	wzr,[x19,a_flashErase]
	mov	w0,1
	strb	w0,[x19,a_saveDirty]
	ret
.c4:	cmp	w1,0xA0
	b.ne	.c5
	mov	w3,1
	strb	w3,[x19,a_flashWrite]
	ret
.c5:	cmp	w1,0xB0
	b.ne	.r
	cmp	w2,SAVE_FLASH128
	b.ne	.r
	mov	w3,1
	strb	w3,[x19,a_flashBankSel]
	ret

; -> w0
eeprom_read:
	ldr	w1,[x19,a_eeState]
	cmp	w1,2
	b.eq	.a
	mov	w0,1
	ret
.a:	ldr	w1,[x19,a_eeReadPos]		; pos
	add	w2,w1,1
	str	w2,[x19,a_eeReadPos]
	cmp	w1,67
	b.lt	.b
	str	wzr,[x19,a_eeState]
.b:	cmp	w1,4
	b.ge	.c
	mov	w0,0
	ret
.c:	sub	w1,w1,4				; bit
	ldr	w2,[x19,a_eeAddr]
	and	w2,w2,0x3FF
	lsl	w2,w2,3
	add	w2,w2,w1,lsr 3
	and	w2,w2,0x1FFF
	adm	x3,A_SAVE
	ldrb	w2,[x3,x2]
	and	w1,w1,7
	mov	w3,7
	sub	w1,w3,w1
	lsr	w0,w2,w1
	and	w0,w0,1
	ret

; w0 = v
eeprom_write:
	ldr	w1,[x19,a_eeAddrBits]		; ab
	mov	w2,14
	cmp	w1,0
	csel	w1,w2,w1,eq
	ldr	w2,[x19,a_eeState]
	cmp	w2,2
	b.ne	.a
	str	wzr,[x19,a_eeState]
.a:	ldr	x2,[x19,a_eeBuf]
	and	x0,x0,1
	orr	x2,x0,x2,lsl 1
	str	x2,[x19,a_eeBuf]
	ldr	w3,[x19,a_eeBits]
	add	w3,w3,1
	str	w3,[x19,a_eeBits]
	cmp	w3,2
	b.ne	.b
	and	w4,w2,3
	str	w4,[x19,a_eeCmd]
	cmp	w4,2
	b.hs	.r
.clr:	str	wzr,[x19,a_eeBits]
	str	xzr,[x19,a_eeBuf]
.r:	ret
.b:	add	w4,w1,2				; 2 + ab
	cmp	w3,w4
	b.ne	.c
	mov	w5,1
	lsl	w5,w5,w1
	sub	w5,w5,1
	and	w5,w5,w2
	str	w5,[x19,a_eeAddr]
	ret
.c:	ldr	w5,[x19,a_eeCmd]
	cmp	w5,3
	b.ne	.d
	add	w6,w4,1
	cmp	w3,w6
	b.ne	.d
	mov	w6,2
	str	w6,[x19,a_eeState]
	str	wzr,[x19,a_eeReadPos]
	b	.clr
.d:	cmp	w5,2
	b.ne	.r
	add	w6,w4,64
	cmp	w3,w6
	b.ne	.e
	ldr	w5,[x19,a_eeAddr]
	and	w5,w5,0x3FF
	lsl	w5,w5,3				; byte
	adm	x6,A_SAVE
	mov	w7,0				; i
.w:	add	w8,w5,w7
	and	w8,w8,0x1FFF
	mov	w16,7
	sub	w16,w16,w7
	lsl	w16,w16,3
	lsr	x17,x2,x16
	strb	w17,[x6,x8]
	add	w7,w7,1
	cmp	w7,8
	b.lo	.w
	mov	w5,1
	strb	w5,[x19,a_saveDirty]
	ret
.e:	add	w6,w4,65
	cmp	w3,w6
	b.ge	.clr
	ret

include 'gba_cpu.inc'
include 'gba_bios.inc'
include 'gba_ppu.inc'
include 'gba_apu.inc'
