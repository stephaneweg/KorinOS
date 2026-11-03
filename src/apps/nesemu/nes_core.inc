; ============================================================================
;  nes_core.inc - la NES : le 2A03 (6502, officiel et instable connu), le 2C02,
;  l'APU, les mappers 0 1 2 3 4 7 66. Ecrit d'apres le coeur C++ d'Onyx
;  (user/Emulators/nes), image pour image : tools/emu/ (docs/EMULATEURS.md).
;
;	nes_load (m, rom, octets)	-> 1 ; 0 : pas une ROM, ou mapper inconnu
;	nes_reset (m)
;	nes_frame (m, pixels, boutons)	une image, 0x00RRGGBB, 256 x 240
;	nes_audio_rate (m, hz), nes_audio_read (m, lr, trames) -> trames
;	nes_set_pal (m, 0 / 1), nes_set_save (m, donnees, octets)
;	m = NES_SIZE octets a zero
;
;  Registres dans le coeur : x19 = m, w20 A, w21 X, w22 Y, w23 S, w24 P, w25 PC,
;  w26 cycles de l'instruction, w27 adresse effective, x28 table des codes.
;  nes_read et nes_write gardent x9 et x10 ; le reste de x0-x15 est perdu.
; ============================================================================

virtual at 0
n_prgptr:	rb 32			; fenetres de 8 Ko
n_chrptr:	rb 64			; fenetres de 1 Ko
n_fbptr:	rb 8
n_prg:		rb 8
n_chrrom:	rb 8
n_cycles:	rb 8
n_mmc1Last:	rb 8
n_a12Low:	rb 8
n_sampleAcc:	rb 8
n_hpAcc:	rb 8
n_dotAcc:	rb 8
n_prgMap:	rb 16			; decalages dans la PRG
n_chrMap:	rb 32
n_prgSize:	rb 4
n_chrSize:	rb 4
n_chrIsRam:	rb 4
n_mirror:	rb 4
n_fourScreen:	rb 4
n_mapper:	rb 4
n_battery:	rb 4
n_sramDirty:	rb 4
n_pal:		rb 4
n_mmc1Shift:	rb 4
n_mmc1Count:	rb 4
n_mmc1Ctrl:	rb 4
n_mmc1Chr0:	rb 4
n_mmc1Chr1:	rb 4
n_mmc1Prg:	rb 4
n_mmc3Sel:	rb 4
n_mmc3Latch:	rb 4
n_mmc3Counter:	rb 4
n_mmc3Reload:	rb 4
n_mmc3IrqOn:	rb 4
n_mmc3Irq:	rb 4
n_mmc3PrgMode:	rb 4
n_mmc3ChrMode:	rb 4
n_simpleBank:	rb 4
n_sramOn:	rb 4
n_a12:		rb 4
n_nmiPending:	rb 4
n_stall:	rb 4
n_ctrl:		rb 4
n_mask:		rb 4
n_status:	rb 4
n_oamAddr:	rb 4
n_v:		rb 4
n_t:		rb 4
n_fineX:	rb 4
n_wLatch:	rb 4
n_readBuf:	rb 4
n_openBus:	rb 4
n_line:		rb 4
n_dot:		rb 4
n_oddFrame:	rb 4
n_frameDone:	rb 4
n_sprite0Dot:	rb 4
n_nmiOccurred:	rb 4
n_frameMode:	rb 4
n_frameIrqOff:	rb 4
n_frameIrq:	rb 4
n_frameCycle:	rb 4
n_apuPending:	rb 4
n_instrCyc:	rb 4
n_apuAhead:	rb 4
n_rate:		rb 4
n_cpuHz:	rb 4
n_lastOut:	rb 4
n_ahead:	rb 4
n_atail:	rb 4
n_pad:		rb 4
n_padShift:	rb 4
n_strobe:	rb 4
n_regA:		rb 4			; le processeur entre deux images
n_regX:		rb 4
n_regY:		rb 4
n_regS:		rb 4
n_regP:		rb 4
n_regPC:	rb 4
n_mmc3Regs:	rb 8
n_pu:		rb 2*PU_SIZE
n_tr:		rb TR_SIZE
n_no:		rb NO_SIZE
n_dmc:		rb DMC_SIZE
n_end:
end virtual

PU_on		= 0
PU_duty		= 4
PU_seq		= 8
PU_timer	= 12
PU_period	= 16
PU_len		= 20
PU_vol		= 24
PU_envPeriod	= 28
PU_envDiv	= 32
PU_envVol	= 36
PU_sweepPeriod	= 40
PU_sweepDiv	= 44
PU_sweepShift	= 48
PU_constVol	= 52
PU_halt		= 56
PU_envStart	= 60
PU_sweepOn	= 64
PU_sweepNeg	= 68
PU_sweepReload	= 72
PU_SIZE		= 76

TR_on		= 0
TR_halt		= 4
TR_linReload	= 8
TR_timer	= 12
TR_period	= 16
TR_len		= 20
TR_linCounter	= 24
TR_linPeriod	= 28
TR_seq		= 32
TR_SIZE		= 36

NO_on		= 0
NO_halt		= 4
NO_constVol	= 8
NO_envStart	= 12
NO_mode		= 16
NO_timer	= 20
NO_period	= 24
NO_len		= 28
NO_vol		= 32
NO_envPeriod	= 36
NO_envDiv	= 40
NO_envVol	= 44
NO_lfsr		= 48
NO_SIZE		= 52

DMC_irqOn	= 0
DMC_loop	= 4
DMC_irq		= 8
DMC_silence	= 12
DMC_rate	= 16
DMC_timer	= 20
DMC_level	= 24
DMC_bits	= 28
DMC_shift	= 32
DMC_bufFull	= 36
DMC_buf		= 40
DMC_addr	= 44
DMC_start	= 48
DMC_remain	= 52
DMC_len		= 56
DMC_SIZE	= 60

assert n_end <= 0x800
N_SCR		= 0x800			; lignes de travail du dessin : 0x420 octets
N_BG		= N_SCR
N_BGPAL		= N_SCR + 0x110
N_SPR		= N_SCR + 0x220
N_SPRATTR	= N_SCR + 0x320
N_OAM		= 0x0C80
N_PALRAM	= 0x0D80
N_RAM		= 0x1000		; 0x800
N_CIRAM		= 0x2000		; 0x1000
N_SRAM		= 0x3000		; 0x2000
N_CHRRAM	= 0x5000		; 0x8000
N_ABUF		= 0xD000		; 8192 trames s16 stereo
NES_ABUF	= 8192
NES_SIZE	= N_ABUF + NES_ABUF*4
NES_W		= 256
NES_H		= 240

; ---------------------------------------------------------------------------
;  Les codes : adressage (-> w27, +1 cycle), puis l'operation
; ---------------------------------------------------------------------------

macro setzn reg {
	mov	w16,0x82
	bic	w24,w24,w16
	and	w16,reg,0x80
	orr	w24,w24,w16
	tst	reg,0xFF
	cset	w16,eq
	orr	w24,w24,w16,lsl 1
}
macro pcinc {
	add	w25,w25,1
	and	w25,w25,0xFFFF
}
macro rdad {
	mov	w0,w27
	bl	nes_read
}
macro wrad reg {
	mov	w1,reg
	mov	w0,w27
	bl	nes_write
}
macro setc_b7 reg {			; C = bit 7
	and	w24,w24,0xFE
	orr	w24,w24,reg,lsr 7
}
macro setc_b0 reg {
	and	w16,reg,1
	and	w24,w24,0xFE
	orr	w24,w24,w16
}

macro addr_IMP cross {}
macro addr_ACC cross {}
macro addr_IMM cross {
	mov	w27,w25
	pcinc
}
macro addr_REL cross {
	mov	w27,w25
	pcinc
}
macro addr_ZP cross {
	bl	fetch8
	mov	w27,w0
}
macro addr_ZPX cross {
	bl	fetch8
	add	w27,w0,w21
	and	w27,w27,0xFF
}
macro addr_ZPY cross {
	bl	fetch8
	add	w27,w0,w22
	and	w27,w27,0xFF
}
macro addr_ABS cross {
	bl	fetch16
	mov	w27,w0
}
macro pagecross cross {
  if cross
	eor	w1,w0,w27
	tst	w1,0xFF00
	cinc	w26,w26,ne
  end if
}
macro addr_ABX cross {
	bl	fetch16
	add	w27,w0,w21
	and	w27,w27,0xFFFF
	pagecross cross
}
macro addr_ABY cross {
	bl	fetch16
	add	w27,w0,w22
	and	w27,w27,0xFFFF
	pagecross cross
}
macro addr_IND cross {
	bl	fetch16
	mov	w9,w0
	bl	nes_read
	mov	w10,w0
	add	w1,w9,1
	and	w1,w1,0xFF
	and	w0,w9,0xFF00
	orr	w0,w0,w1
	bl	nes_read
	orr	w27,w10,w0,lsl 8
}
macro addr_IZX cross {
	bl	fetch8
	add	w9,w0,w21
	and	w9,w9,0xFF
	mov	w0,w9
	bl	nes_read
	mov	w10,w0
	add	w0,w9,1
	and	w0,w0,0xFF
	bl	nes_read
	orr	w27,w10,w0,lsl 8
}
macro addr_IZY cross {
	bl	fetch8
	mov	w9,w0
	bl	nes_read
	mov	w10,w0
	add	w0,w9,1
	and	w0,w0,0xFF
	bl	nes_read
	orr	w0,w10,w0,lsl 8
	add	w27,w0,w22
	and	w27,w27,0xFFFF
	pagecross cross
}

macro op_ADC mode {
	rdad
	bl	do_adc
}
macro op_SBC mode {
	rdad
	mvn	w0,w0
	and	w0,w0,0xFF
	bl	do_adc
}
macro op_AND mode {
	rdad
	and	w20,w20,w0
	setzn	w20
}
macro op_ORA mode {
	rdad
	orr	w20,w20,w0
	setzn	w20
}
macro op_EOR mode {
	rdad
	eor	w20,w20,w0
	setzn	w20
}
macro op_CMP mode {
	rdad
	mov	w1,w20
	bl	do_cmp
}
macro op_CPX mode {
	rdad
	mov	w1,w21
	bl	do_cmp
}
macro op_CPY mode {
	rdad
	mov	w1,w22
	bl	do_cmp
}
macro op_BIT mode {
	rdad
	mov	w16,0xC2
	bic	w24,w24,w16
	and	w1,w0,0xC0
	orr	w24,w24,w1
	tst	w0,w20
	cset	w1,eq
	orr	w24,w24,w1,lsl 1
}
macro op_LDA mode {
	rdad
	mov	w20,w0
	setzn	w20
}
macro op_LDX mode {
	rdad
	mov	w21,w0
	setzn	w21
}
macro op_LDY mode {
	rdad
	mov	w22,w0
	setzn	w22
}
macro op_STA mode {
	wrad	w20
}
macro op_STX mode {
	wrad	w21
}
macro op_STY mode {
	wrad	w22
}
macro rmw_read mode {
  if mode eq ACC
	mov	w9,w20
  else
	rdad
	mov	w9,w0
  end if
}
macro rmw_write mode {
  if mode eq ACC
	mov	w20,w9
  else
	wrad	w9
  end if
}
macro op_ASL mode {
	rmw_read mode
	setc_b7	w9
	lsl	w9,w9,1
	and	w9,w9,0xFF
	setzn	w9
	rmw_write mode
}
macro op_LSR mode {
	rmw_read mode
	setc_b0	w9
	lsr	w9,w9,1
	setzn	w9
	rmw_write mode
}
macro op_ROL mode {
	rmw_read mode
	and	w10,w24,1
	setc_b7	w9
	lsl	w9,w9,1
	orr	w9,w9,w10
	and	w9,w9,0xFF
	setzn	w9
	rmw_write mode
}
macro op_ROR mode {
	rmw_read mode
	and	w10,w24,1
	setc_b0	w9
	lsr	w9,w9,1
	orr	w9,w9,w10,lsl 7
	setzn	w9
	rmw_write mode
}
macro op_INC mode {
	rdad
	add	w9,w0,1
	and	w9,w9,0xFF
	wrad	w9
	setzn	w9
}
macro op_DEC mode {
	rdad
	sub	w9,w0,1
	and	w9,w9,0xFF
	wrad	w9
	setzn	w9
}
macro op_INX mode {
	add	w21,w21,1
	and	w21,w21,0xFF
	setzn	w21
}
macro op_INY mode {
	add	w22,w22,1
	and	w22,w22,0xFF
	setzn	w22
}
macro op_DEX mode {
	sub	w21,w21,1
	and	w21,w21,0xFF
	setzn	w21
}
macro op_DEY mode {
	sub	w22,w22,1
	and	w22,w22,0xFF
	setzn	w22
}
macro op_TAX mode {
	mov	w21,w20
	setzn	w21
}
macro op_TAY mode {
	mov	w22,w20
	setzn	w22
}
macro op_TXA mode {
	mov	w20,w21
	setzn	w20
}
macro op_TYA mode {
	mov	w20,w22
	setzn	w20
}
macro op_TSX mode {
	mov	w21,w23
	setzn	w21
}
macro op_TXS mode {
	mov	w23,w21
}
macro op_PHA mode {
	mov	w0,w20
	bl	do_push
}
macro op_PHP mode {
	orr	w0,w24,0x30
	bl	do_push
}
macro op_PLA mode {
	bl	do_pull
	mov	w20,w0
	setzn	w20
}
macro op_PLP mode {
	bl	do_pull
	and	w24,w0,0xFFFFFFCF
	orr	w24,w24,0x20
}
macro op_CLC mode {
	and	w24,w24,0xFE
}
macro op_SEC mode {
	orr	w24,w24,1
}
macro op_CLI mode {
	and	w24,w24,0xFFFFFFFB
}
macro op_SEI mode {
	orr	w24,w24,4
}
macro op_CLV mode {
	and	w24,w24,0xFFFFFFBF
}
macro op_CLD mode {
	and	w24,w24,0xFFFFFFF7
}
macro op_SED mode {
	orr	w24,w24,8
}
macro op_BPL mode {
	tbnz	w24,7,.nt
	bl	do_branch
.nt:
}
macro op_BMI mode {
	tbz	w24,7,.nt
	bl	do_branch
.nt:
}
macro op_BVC mode {
	tbnz	w24,6,.nt
	bl	do_branch
.nt:
}
macro op_BVS mode {
	tbz	w24,6,.nt
	bl	do_branch
.nt:
}
macro op_BCC mode {
	tbnz	w24,0,.nt
	bl	do_branch
.nt:
}
macro op_BCS mode {
	tbz	w24,0,.nt
	bl	do_branch
.nt:
}
macro op_BNE mode {
	tbnz	w24,1,.nt
	bl	do_branch
.nt:
}
macro op_BEQ mode {
	tbz	w24,1,.nt
	bl	do_branch
.nt:
}
macro op_JMP mode {
	mov	w25,w27
}
macro op_JSR mode {
	sub	w9,w25,1
	and	w9,w9,0xFFFF
	lsr	w0,w9,8
	bl	do_push
	and	w0,w9,0xFF
	bl	do_push
	mov	w25,w27
}
macro op_RTS mode {
	bl	do_pull
	mov	w9,w0
	bl	do_pull
	orr	w25,w9,w0,lsl 8
	add	w25,w25,1
	and	w25,w25,0xFFFF
}
macro op_RTI mode {
	bl	do_pull
	and	w24,w0,0xFFFFFFCF
	orr	w24,w24,0x20
	bl	do_pull
	mov	w9,w0
	bl	do_pull
	orr	w25,w9,w0,lsl 8
}
macro op_BRK mode {
	pcinc
	mov	w0,0xFFFE
	mov	w1,1
	bl	do_interrupt
}
macro op_NOP mode {
  if ~ mode eq IMP
	rdad
  end if
}
macro op_KIL mode {
	sub	w25,w25,1
	and	w25,w25,0xFFFF
}
macro op_LAX mode {
	rdad
	mov	w20,w0
	mov	w21,w0
	setzn	w20
}
macro op_SAX mode {
	and	w9,w20,w21
	wrad	w9
}
macro op_DCP mode {
	rdad
	sub	w9,w0,1
	and	w9,w9,0xFF
	wrad	w9
	mov	w0,w9
	mov	w1,w20
	bl	do_cmp
}
macro op_ISB mode {
	rdad
	add	w9,w0,1
	and	w9,w9,0xFF
	wrad	w9
	mvn	w0,w9
	and	w0,w0,0xFF
	bl	do_adc
}
macro op_SLO mode {
	rdad
	mov	w9,w0
	setc_b7	w9
	lsl	w9,w9,1
	and	w9,w9,0xFF
	wrad	w9
	orr	w20,w20,w9
	setzn	w20
}
macro op_RLA mode {
	rdad
	mov	w9,w0
	and	w10,w24,1
	setc_b7	w9
	lsl	w9,w9,1
	orr	w9,w9,w10
	and	w9,w9,0xFF
	wrad	w9
	and	w20,w20,w9
	setzn	w20
}
macro op_SRE mode {
	rdad
	mov	w9,w0
	setc_b0	w9
	lsr	w9,w9,1
	wrad	w9
	eor	w20,w20,w9
	setzn	w20
}
macro op_RRA mode {
	rdad
	mov	w9,w0
	and	w10,w24,1
	setc_b0	w9
	lsr	w9,w9,1
	orr	w9,w9,w10,lsl 7
	wrad	w9
	mov	w0,w9
	bl	do_adc
}
macro op_ANC mode {
	rdad
	and	w20,w20,w0
	setzn	w20
	setc_b7	w20
}
macro op_ALR mode {
	rdad
	and	w20,w20,w0
	setc_b0	w20
	lsr	w20,w20,1
	setzn	w20
}
macro op_ARR mode {
	rdad
	and	w20,w20,w0
	and	w1,w24,1
	lsr	w20,w20,1
	orr	w20,w20,w1,lsl 7
	setzn	w20
	mov	w16,0x41
	bic	w24,w24,w16
	ubfx	w1,w20,6,1
	orr	w24,w24,w1
	ubfx	w2,w20,5,1
	eor	w1,w1,w2
	orr	w24,w24,w1,lsl 6
}
macro op_AXS mode {
	rdad
	and	w1,w20,w21
	subs	w1,w1,w0
	cset	w2,ge
	and	w24,w24,0xFE
	orr	w24,w24,w2
	and	w21,w1,0xFF
	setzn	w21
}
macro op_LXA mode {
	rdad
	mov	w20,w0
	mov	w21,w0
	setzn	w20
}
macro op_XAA mode {
	rdad
	mov	w1,0xEE
	orr	w1,w1,w20
	and	w1,w1,w21
	and	w20,w1,w0
	setzn	w20
}
macro op_LAS mode {
	rdad
	and	w0,w0,w23
	mov	w20,w0
	mov	w21,w0
	mov	w23,w0
	setzn	w0
}
macro sh_write reg, idx {
	lsr	w1,w27,8
	add	w1,w1,1
	and	w1,w1,0xFF
	and	w9,reg,w1			; val
	sub	w1,w27,idx
	eor	w1,w1,w27
	tst	w1,0xFF00
	b.eq	.m
	and	w27,w27,0xFF
	orr	w27,w27,w9,lsl 8
.m:	wrad	w9
}
macro op_SHY mode {
	sh_write w22,w21
}
macro op_SHX mode {
	sh_write w21,w22
}
macro op_SHA mode {
	lsr	w1,w27,8
	add	w1,w1,1
	and	w9,w20,w21
	and	w9,w9,w1
	wrad	w9
}
macro op_TAS mode {
	and	w23,w20,w21
	lsr	w1,w27,8
	add	w1,w1,1
	and	w9,w23,w1
	wrad	w9
}

macro opc n, mode, op, cyc, cross {
h_#n:	mov	w26,cyc
	addr_#mode cross
	str	w26,[x19,n_instrCyc]
	op_#op mode
	b	step_done
}

; ---------------------------------------------------------------------------
;  Une image
; ---------------------------------------------------------------------------

; x0 = m, x1 = pixels, x2 = boutons
nes_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	str	x1,[x19,n_fbptr]
	str	w2,[x19,n_pad]
	ldr	x0,[x19,n_prg]
	cbz	x0,nf_out
	ldr	w20,[x19,n_regA]
	ldr	w21,[x19,n_regX]
	ldr	w22,[x19,n_regY]
	ldr	w23,[x19,n_regS]
	ldr	w24,[x19,n_regP]
	ldr	w25,[x19,n_regPC]
	adr	x28,optab
	str	wzr,[x19,n_frameDone]
	movl	w15,200000
	str	w15,[sp,-16]!
step_loop:
	ldr	w0,[x19,n_frameDone]
	cbnz	w0,nf_fin
	ldr	w15,[sp]
	subs	w15,w15,1
	b.lo	nf_fin
	str	w15,[sp]
	ldr	w26,[x19,n_stall]
	cbz	w26,.step
	str	wzr,[x19,n_stall]
	b	step_done
.step:	str	wzr,[x19,n_apuAhead]
	mov	w0,7
	str	w0,[x19,n_instrCyc]
	ldr	w0,[x19,n_nmiPending]
	cbz	w0,.irq
	str	wzr,[x19,n_nmiPending]
	mov	w26,7
	mov	w0,0xFFFA
	mov	w1,0
	bl	do_interrupt
	b	step_done
.irq:	ldr	w0,[x19,n_mmc3Irq]
	ldr	w1,[x19,n_frameIrq]
	orr	w0,w0,w1
	ldr	w1,[x19,n_dmc+DMC_irq]
	orr	w0,w0,w1
	cbz	w0,.ins
	tbnz	w24,2,.ins
	mov	w26,7
	mov	w0,0xFFFE
	mov	w1,0
	bl	do_interrupt
	b	step_done
.ins:
  if defined NES_TRACE
	bl	nes_trace
  end if
	bl	fetch8
	ldrsw	x1,[x28,x0,lsl 2]
	add	x1,x28,x1
	br	x1
step_done:
	ldr	x0,[x19,n_cycles]
	add	x0,x0,x26
	str	x0,[x19,n_cycles]
	ldr	w0,[x19,n_apuPending]
	ldr	w1,[x19,n_apuAhead]
	add	w0,w0,w26
	sub	w0,w0,w1
	str	wzr,[x19,n_apuAhead]
	str	wzr,[x19,n_apuPending]
	cbz	w0,.ppu
	bl	apu_run
.ppu:	bl	ppu_run
	b	step_loop
nf_fin:	add	sp,sp,16
	ldr	w0,[x19,n_apuPending]
	cbz	w0,.save
	str	wzr,[x19,n_apuPending]
	bl	apu_run
.save:	str	w20,[x19,n_regA]
	str	w21,[x19,n_regX]
	str	w22,[x19,n_regY]
	str	w23,[x19,n_regS]
	str	w24,[x19,n_regP]
	str	w25,[x19,n_regPC]
nf_out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

fetch8:
	mov	w0,w25
	pcinc
	b	nes_read

; -> w0 = le mot en PC ; PC + 2
fetch16:
	str	x30,[sp,-16]!
	bl	fetch8
	mov	w9,w0
	bl	fetch8
	orr	w0,w9,w0,lsl 8
	ldr	x30,[sp],16
	ret

do_adc:					; w0 = m
	and	w1,w24,1
	add	w2,w20,w0
	add	w2,w2,w1
	eor	w3,w20,w0
	mvn	w3,w3
	eor	w4,w20,w2
	and	w3,w3,w4
	and	w3,w3,0x80
	mov	w16,0x41
	bic	w24,w24,w16
	cmp	w2,0xFF
	cset	w1,hi
	orr	w24,w24,w1
	orr	w24,w24,w3,lsr 1
	and	w20,w2,0xFF
	setzn	w20
	ret

do_cmp:					; w1 = r, w0 = m
	subs	w1,w1,w0
	cset	w2,ge
	and	w24,w24,0xFE
	orr	w24,w24,w2
	and	w1,w1,0xFF
	setzn	w1
	ret

do_push:				; w0 = valeur
	mov	w1,w0
	orr	w0,w23,0x100
	sub	w23,w23,1
	and	w23,w23,0xFF
	b	nes_write

do_pull:
	add	w23,w23,1
	and	w23,w23,0xFF
	orr	w0,w23,0x100
	b	nes_read

do_branch:
	str	x30,[sp,-16]!
	mov	w0,w27
	bl	nes_read
	sxtb	w0,w0
	add	w1,w25,w0
	and	w1,w1,0xFFFF
	eor	w2,w1,w25
	tst	w2,0xFF00
	add	w26,w26,1
	cinc	w26,w26,ne
	mov	w25,w1
	ldr	x30,[sp],16
	ret

; w0 = vecteur, w1 = 1 : BRK
do_interrupt:
	str	x30,[sp,-16]!
	mov	w9,w0
	mov	w10,w1
	lsr	w0,w25,8
	bl	do_push
	and	w0,w25,0xFF
	bl	do_push
	and	w0,w24,0xFFFFFFEF
	orr	w0,w0,0x20
	orr	w0,w0,w10,lsl 4
	bl	do_push
	orr	w24,w24,4
	mov	w0,w9
	bl	nes_read
	mov	w10,w0
	add	w0,w9,1
	bl	nes_read
	orr	w25,w10,w0,lsl 8
	ldr	x30,[sp],16
	ret

; ---------------------------------------------------------------------------
;  Le bus
; ---------------------------------------------------------------------------

; w0 = adresse -> w0 (garde x9, x10)
nes_read:
	cmp	w0,0x2000
	b.hs	.h
	and	w0,w0,0x7FF
	add	x1,x19,N_RAM
	ldrb	w0,[x1,x0]
	ret
.h:	tbz	w0,15,.io
	ubfx	w1,w0,13,2
	ldr	x1,[x19,x1,lsl 3]		; n_prgptr = 0
	and	w0,w0,0x1FFF
	ldrb	w0,[x1,x0]
	ret
.io:	stp	x9,x10,[sp,-32]!
	str	x30,[sp,16]
	cmp	w0,0x4000
	b.hs	.1
	and	w0,w0,7
	bl	reg_read
	b	.r
.1:	cmp	w0,0x6000
	b.lo	.2
	ldr	w1,[x19,n_sramOn]
	cbz	w1,.open
	and	w0,w0,0x1FFF
	add	x1,x19,N_SRAM
	ldrb	w0,[x1,x0]
	b	.r
.2:	movl	w1,0x4015
	cmp	w0,w1
	b.ne	.3
	bl	apu_sync
	bl	apu_status
	b	.r
.3:	add	w1,w1,1				; 0x4016
	cmp	w0,w1
	b.ne	.4
	ldr	w1,[x19,n_strobe]
	cbz	w1,.sh
	ldr	w0,[x19,n_pad]
	and	w0,w0,1
	orr	w0,w0,0x40
	b	.r
.sh:	ldr	w1,[x19,n_padShift]
	and	w0,w1,1
	lsr	w1,w1,1
	orr	w1,w1,0x80
	str	w1,[x19,n_padShift]
	orr	w0,w0,0x40
	b	.r
.4:	movl	w1,0x4017
	cmp	w0,w1
	b.ne	.open
	mov	w0,0x40
	b	.r
.open:	lsr	w0,w0,8
.r:	ldr	x30,[sp,16]
	ldp	x9,x10,[sp],32
	ret

; w0 = adresse, w1 = valeur (garde x9, x10)
nes_write:
	cmp	w0,0x2000
	b.hs	.h
	and	w0,w0,0x7FF
	add	x2,x19,N_RAM
	strb	w1,[x2,x0]
	ret
.h:	stp	x9,x10,[sp,-32]!
	str	x30,[sp,16]
	tbz	w0,15,.io
	bl	mapper_write
	b	.r
.io:	cmp	w0,0x4000
	b.hs	.1
	and	w0,w0,7
	bl	reg_write
	b	.r
.1:	cmp	w0,0x6000
	b.lo	.2
	ldr	w2,[x19,n_sramOn]
	cbz	w2,.r
	and	w0,w0,0x1FFF
	add	x2,x19,N_SRAM
	ldrb	w3,[x2,x0]
	cmp	w3,w1
	b.eq	.r
	strb	w1,[x2,x0]
	ldr	w0,[x19,n_battery]
	cbz	w0,.r
	mov	w0,1
	str	w0,[x19,n_sramDirty]
	b	.r
.2:	movl	w2,0x4014
	cmp	w0,w2
	b.ne	.3
	lsl	w9,w1,8				; DMA des sprites
	mov	w10,0
.dma:	add	w0,w9,w10
	and	w0,w0,0xFFFF
	bl	nes_read
	ldr	w1,[x19,n_oamAddr]
	add	w1,w1,w10
	and	w1,w1,0xFF
	add	x2,x19,N_OAM
	strb	w0,[x2,x1]
	add	w10,w10,1
	cmp	w10,256
	b.lo	.dma
	ldr	w0,[x19,n_stall]
	ldr	x1,[x19,n_cycles]
	and	w1,w1,1
	add	w0,w0,w1
	add	w0,w0,513
	str	w0,[x19,n_stall]
	b	.r
.3:	add	w2,w2,2				; 0x4016
	cmp	w0,w2
	b.ne	.4
	and	w1,w1,1
	str	w1,[x19,n_strobe]
	ldr	w1,[x19,n_pad]
	str	w1,[x19,n_padShift]
	b	.r
.4:	add	w2,w2,1				; 0x4017
	cmp	w0,w2
	b.hi	.r
	stp	x0,x1,[sp,-16]!
	bl	apu_sync
	ldp	x0,x1,[sp],16
	bl	apu_write
.r:	ldr	x30,[sp,16]
	ldp	x9,x10,[sp],32
	ret

; ---------------------------------------------------------------------------
;  La cartouche
; ---------------------------------------------------------------------------

; x0 = m, x1 = rom, x2 = octets -> x0 = 1 / 0
nes_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbz	x20,.bad
	movl	x0,16+0x4000
	cmp	x21,x0
	b.lo	.bad
	ldr	w0,[x20]
	movl	w1,0x1A53454E
	cmp	w0,w1
	b.ne	.bad
	ldrb	w3,[x20,6]
	ldrb	w4,[x20,7]
	ldrb	w5,[x20,9]
	ldrb	w1,[x20,4]			; prgBanks
	ldrb	w2,[x20,5]			; chrBanks
	and	w6,w4,0x0C
	cmp	w6,8
	cset	w6,eq				; nes2
	cbz	w6,.n1
	and	w0,w5,0x0F
	orr	w1,w1,w0,lsl 8
	and	w0,w5,0xF0
	orr	w2,w2,w0,lsl 4
.n1:	lsr	w7,w3,4
	and	w0,w4,0xF0
	orr	w7,w7,w0
	cbz	w6,.n2
	ldrb	w0,[x20,8]
	and	w0,w0,0x0F
	orr	w7,w7,w0,lsl 8
	b	.n3
.n2:	and	w0,w4,0x0C
	cmp	w0,4
	b.ne	.n3
	and	w7,w7,0x0F
.n3:	cmp	w7,4
	b.ls	.mok3
	cmp	w7,7
	b.eq	.mok
	cmp	w7,66
	b.ne	.bad
	b	.mok
.mok3:
.mok:	str	w7,[x19,n_mapper]
	ubfx	w0,w3,1,1
	str	w0,[x19,n_battery]
	ubfx	w0,w3,3,1
	str	w0,[x19,n_fourScreen]
	mov	w8,4
	cbnz	w0,.mir
	and	w8,w3,1
.mir:	str	w8,[x19,n_mirror]
	mov	w8,16
	tbz	w3,2,.tr
	add	w8,w8,512
.tr:	lsl	w1,w1,14
	lsl	w2,w2,13
	str	w1,[x19,n_prgSize]
	str	w2,[x19,n_chrSize]
	cbz	w1,.bad
	add	x0,x8,x1
	cmp	x0,x21
	b.hi	.bad
	add	x0,x20,x8
	str	x0,[x19,n_prg]
	cbz	w2,.cram
	add	x9,x8,x1
	add	x9,x9,x2
	cmp	x9,x21
	b.hi	.cram
	add	x0,x0,x1
	str	x0,[x19,n_chrrom]
	str	wzr,[x19,n_chrIsRam]
	b	.rg
.cram:	str	xzr,[x19,n_chrrom]
	mov	w0,0x2000
	str	w0,[x19,n_chrSize]
	mov	w0,1
	str	w0,[x19,n_chrIsRam]
.rg:	cbz	w6,.i1
	ldrb	w0,[x20,12]
	and	w0,w0,3
	cmp	w0,1
	cset	w1,eq
	b	.i2
.i1:	and	w1,w5,1
.i2:	mov	x0,x19
	bl	nes_set_pal
	mov	x0,x19
	bl	nes_reset
	mov	x0,1
	b	.out
.bad:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = m, x1 = 1 : PAL
nes_set_pal:
	and	w1,w1,1
	str	w1,[x0,n_pal]
	movl	w2,1789773
	movl	w3,1662607
	cmp	w1,0
	csel	w2,w3,w2,ne
	str	w2,[x0,n_cpuHz]
	ret

nes_audio_rate:				; x0 = m, x1 = Hz
	movl	w2,48000
	movl	w3,8000
	cmp	w1,w3
	csel	w1,w1,w2,gt
	str	w1,[x0,n_rate]
	ret

; x0 = m, x1 = lr, x2 = trames au plus -> x0 = trames
nes_audio_read:
	ldr	w3,[x0,n_ahead]
	ldr	w4,[x0,n_atail]
	mov	x5,0
	add	x6,x0,N_ABUF
.l:	cmp	w3,w4
	b.eq	.f
	cmp	x5,x2
	b.hs	.f
	ldr	w7,[x6,x3,lsl 2]
	str	w7,[x1],4
	add	w3,w3,1
	and	w3,w3,NES_ABUF-1
	add	x5,x5,1
	b	.l
.f:	str	w3,[x0,n_ahead]
	mov	x0,x5
	ret

; x0 = m, x1 = donnees, x2 = octets
nes_set_save:
	add	x3,x0,N_SRAM
	mov	x4,0
.l:	cmp	x4,x2
	b.hs	.f
	cmp	x4,0x2000
	b.hs	.f
	ldrb	w5,[x1,x4]
	strb	w5,[x3,x4]
	add	x4,x4,1
	b	.l
.f:	str	wzr,[x0,n_sramDirty]
	ret

; x0 = m -> x0 = 1 : SRAM a ecrire (la drapeau tombe), x1 = pointeur, x2 = octets
nes_save_poll:
	ldr	w2,[x0,n_battery]
	ldr	w1,[x0,n_sramDirty]
	and	w1,w1,w2
	str	wzr,[x0,n_sramDirty]
	add	x3,x0,N_SRAM
	mov	x2,0x2000
	mov	x4,x0
	mov	x0,x1
	mov	x1,x3
	ret

; x0 = destination, x1 = octets
nes_zero:
	add	x1,x0,x1
.l:	cmp	x0,x1
	b.hs	.f
	strb	wzr,[x0],1
	b	.l
.f:	ret

; x0 = m
nes_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	add	x0,x19,N_RAM
	mov	x1,0x800
	bl	nes_zero
	add	x0,x19,N_CIRAM
	mov	x1,0x1000
	bl	nes_zero
	add	x0,x19,N_OAM
	mov	x1,256
	bl	nes_zero
	add	x0,x19,N_PALRAM
	mov	x1,32
	bl	nes_zero
	ldr	w0,[x19,n_chrIsRam]
	cbz	w0,.m
	add	x0,x19,N_CHRRAM
	mov	x1,0x8000
	bl	nes_zero
.m:	; le mapper
	add	x0,x19,n_mmc1Shift
	add	x1,x19,n_sramOn
	sub	x1,x1,x0
	bl	nes_zero
	mov	w0,0x0C
	str	w0,[x19,n_mmc1Ctrl]
	str	xzr,[x19,n_mmc1Last]
	str	xzr,[x19,n_a12Low]
	str	wzr,[x19,n_a12]
	mov	w0,1
	str	w0,[x19,n_sramOn]
	mov	w0,0
	add	x1,x19,n_mmc3Regs
	strb	w0,[x1]
	mov	w0,2
	strb	w0,[x1,1]
	mov	w0,4
	strb	w0,[x1,2]
	mov	w0,5
	strb	w0,[x1,3]
	mov	w0,6
	strb	w0,[x1,4]
	mov	w0,7
	strb	w0,[x1,5]
	strb	wzr,[x1,6]
	mov	w0,1
	strb	w0,[x1,7]
	bl	update_banks
	; le processeur et le reste
	add	x0,x19,n_nmiPending
	add	x1,x19,n_rate
	sub	x1,x1,x0
	bl	nes_zero
	str	xzr,[x19,n_cycles]
	str	xzr,[x19,n_dotAcc]
	str	xzr,[x19,n_sampleAcc]
	str	xzr,[x19,n_hpAcc]
	str	wzr,[x19,n_lastOut]
	str	wzr,[x19,n_ahead]
	str	wzr,[x19,n_atail]
	str	wzr,[x19,n_pad]
	str	wzr,[x19,n_padShift]
	str	wzr,[x19,n_strobe]
	mov	w0,-1
	str	w0,[x19,n_sprite0Dot]
	add	x0,x19,n_pu
	mov	x1,2*PU_SIZE+TR_SIZE+NO_SIZE+DMC_SIZE
	bl	nes_zero
	mov	w0,1
	str	w0,[x19,n_no+NO_lfsr]
	str	w0,[x19,n_dmc+DMC_silence]
	mov	w0,8
	str	w0,[x19,n_dmc+DMC_bits]
	mov	w0,428
	str	w0,[x19,n_dmc+DMC_rate]
	str	w0,[x19,n_dmc+DMC_timer]
	mov	w0,0xFD
	str	w0,[x19,n_regS]
	mov	w0,0x24
	str	w0,[x19,n_regP]
	str	wzr,[x19,n_regA]
	str	wzr,[x19,n_regX]
	str	wzr,[x19,n_regY]
	movl	w0,0xFFFC
	bl	nes_read
	mov	w9,w0
	movl	w0,0xFFFD
	bl	nes_read
	orr	w0,w9,w0,lsl 8
	str	w0,[x19,n_regPC]
	ldp	x19,x20,[sp],16
	epilogue

; fenetre : w0 = fenetre, w1 = banque de 8 Ko (x19 = m ; w14 = banques)
set_prg:
	udiv	w2,w1,w14
	msub	w1,w2,w14,w1
	lsl	w1,w1,13
	add	x2,x19,n_prgMap
	str	w1,[x2,x0,lsl 2]
	ldr	x2,[x19,n_prg]
	add	x2,x2,x1
	str	x2,[x19,x0,lsl 3]
	ret

; fenetre : w0 = fenetre, w1 = banque de 1 Ko (w15 = banques)
set_chr:
	udiv	w2,w1,w15
	msub	w1,w2,w15,w1
	lsl	w1,w1,10
	add	x2,x19,n_chrMap
	str	w1,[x2,x0,lsl 2]
	ldr	x2,[x19,n_chrrom]
	ldr	w3,[x19,n_chrIsRam]
	cbz	w3,.p
	add	x2,x19,N_CHRRAM
.p:	add	x2,x2,x1
	add	x3,x19,n_chrptr
	str	x2,[x3,x0,lsl 3]
	ret

macro prg_set slot, bank {
	mov	w0,slot
	mov	w1,bank
	bl	set_prg
}
macro chr_set slot, bank {
	mov	w0,slot
	mov	w1,bank
	bl	set_chr
}
macro prg_id {
	prg_set 0,0
	prg_set 1,1
	prg_set 2,2
	prg_set 3,3
}

update_banks:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	w14,[x19,n_prgSize]
	lsr	w14,w14,13
	ldr	w15,[x19,n_chrSize]
	lsr	w15,w15,10
	ldr	w0,[x19,n_mapper]
	cbz	w0,.m0
	cmp	w0,1
	b.eq	.m1
	cmp	w0,2
	b.eq	.m2
	cmp	w0,3
	b.eq	.m3
	cmp	w0,4
	b.eq	.m4
	cmp	w0,7
	b.eq	.m7
	cmp	w0,66
	b.eq	.m66
	b	.out
.m0:	prg_id
	bl	chr_id
	b	.out
.m1:	ldr	w20,[x19,n_mmc1Prg]
	and	w20,w20,0x0F			; bank
	ldr	w0,[x19,n_mmc1Ctrl]
	ubfx	w21,w0,2,2			; mode
	mov	w22,0				; outer
	ldr	w0,[x19,n_prgSize]
	movl	w1,0x40000
	cmp	w0,w1
	b.ls	.o
	ldr	w22,[x19,n_mmc1Chr0]
	and	w22,w22,0x10
	lsl	w22,w22,1
.o:	cmp	w21,2
	b.hs	.m1b
	and	w0,w20,0x0E
	add	w4,w22,w0,lsl 1
	prg_set	0,w4
	add	w4,w4,1
	prg_set	1,w4
	orr	w0,w20,1
	add	w4,w22,w0,lsl 1
	prg_set	2,w4
	add	w4,w4,1
	prg_set	3,w4
	b	.m1c
.m1b:	b.ne	.m1d
	prg_set	0,w22
	add	w4,w22,1
	prg_set	1,w4
	add	w4,w22,w20,lsl 1
	prg_set	2,w4
	add	w4,w4,1
	prg_set	3,w4
	b	.m1c
.m1d:	add	w4,w22,w20,lsl 1
	prg_set	0,w4
	add	w4,w4,1
	prg_set	1,w4
	add	w4,w22,30
	prg_set	2,w4
	add	w4,w22,31
	prg_set	3,w4
.m1c:	ldr	w0,[x19,n_prgSize]
	movl	w1,0x40000
	cmp	w0,w1
	b.hi	.m1e
	cmp	w21,3
	b.ne	.m1f
	sub	w4,w14,2
	prg_set	2,w4
	sub	w4,w14,1
	prg_set	3,w4
.m1f:	cmp	w21,2
	b.ne	.m1e
	prg_set	0,0
	prg_set	1,1
.m1e:	ldr	w0,[x19,n_mmc1Ctrl]
	tbz	w0,4,.c8
	mov	w20,0
.cl:	ldr	w1,[x19,n_mmc1Chr0]
	add	w1,w20,w1,lsl 2
	mov	w0,w20
	bl	set_chr
	ldr	w1,[x19,n_mmc1Chr1]
	add	w1,w20,w1,lsl 2
	add	w0,w20,4
	bl	set_chr
	add	w20,w20,1
	cmp	w20,4
	b.lo	.cl
	b	.m1g
.c8:	mov	w20,0
.c9:	ldr	w1,[x19,n_mmc1Chr0]
	and	w1,w1,0x1E
	add	w1,w20,w1,lsl 2
	mov	w0,w20
	bl	set_chr
	add	w20,w20,1
	cmp	w20,8
	b.lo	.c9
.m1g:	ldr	w0,[x19,n_mmc1Ctrl]
	and	w0,w0,3
	adr	x1,.mt
	ldrb	w0,[x1,x0]
	str	w0,[x19,n_mirror]
	ldr	w0,[x19,n_mmc1Prg]
	ubfx	w0,w0,4,1
	eor	w0,w0,1
	str	w0,[x19,n_sramOn]
	b	.out
.mt:	db	2,3,1,0
.m2:	ldr	w4,[x19,n_simpleBank]
	lsl	w4,w4,1
	prg_set	0,w4
	add	w4,w4,1
	prg_set	1,w4
	sub	w4,w14,2
	prg_set	2,w4
	sub	w4,w14,1
	prg_set	3,w4
	bl	chr_id
	b	.out
.m3:	prg_id
	ldr	w20,[x19,n_simpleBank]
	lsl	w20,w20,3
	bl	chr_from
	b	.out
.m4:	add	x20,x19,n_mmc3Regs
	ldrb	w21,[x20,6]
	and	w21,w21,0x3F
	ldrb	w22,[x20,7]
	and	w22,w22,0x3F
	ldr	w0,[x19,n_mmc3PrgMode]
	cbnz	w0,.p1
	prg_set	0,w21
	prg_set	1,w22
	sub	w4,w14,2
	prg_set	2,w4
	b	.p2
.p1:	sub	w4,w14,2
	prg_set	0,w4
	prg_set	1,w22
	prg_set	2,w21
.p2:	sub	w4,w14,1
	prg_set	3,w4
	ldr	w0,[x19,n_mmc3ChrMode]
	mov	w21,0				; lo
	mov	w22,4				; hi
	cbz	w0,.c0
	mov	w21,4
	mov	w22,0
.c0:	ldrb	w4,[x20,0]
	and	w1,w4,0xFE
	mov	w0,w21
	bl	set_chr
	ldrb	w4,[x20,0]
	orr	w1,w4,1
	add	w0,w21,1
	bl	set_chr
	ldrb	w4,[x20,1]
	and	w1,w4,0xFE
	add	w0,w21,2
	bl	set_chr
	ldrb	w4,[x20,1]
	orr	w1,w4,1
	add	w0,w21,3
	bl	set_chr
	mov	w21,0
.c1:	add	x1,x20,x21
	ldrb	w1,[x1,2]
	add	w0,w22,w21
	bl	set_chr
	add	w21,w21,1
	cmp	w21,4
	b.lo	.c1
	b	.out
.m7:	ldr	w20,[x19,n_simpleBank]
	and	w20,w20,7
	lsl	w20,w20,2
	bl	prg_from
	bl	chr_id
	ldr	w0,[x19,n_simpleBank]
	ubfx	w0,w0,4,1
	add	w0,w0,2
	str	w0,[x19,n_mirror]
	b	.out
.m66:	ldr	w20,[x19,n_simpleBank]
	ubfx	w20,w20,4,2
	lsl	w20,w20,2
	bl	prg_from
	ldr	w20,[x19,n_simpleBank]
	and	w20,w20,3
	lsl	w20,w20,3
	bl	chr_from
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

chr_id:
	mov	w20,0
chr_from:				; w20 = premiere banque de 1 Ko
	str	x30,[sp,-16]!
	mov	w21,0
.l:	mov	w0,w21
	add	w1,w20,w21
	bl	set_chr
	add	w21,w21,1
	cmp	w21,8
	b.lo	.l
	ldr	x30,[sp],16
	ret

prg_from:				; w20 = premiere banque de 8 Ko
	str	x30,[sp,-16]!
	mov	w21,0
.l:	mov	w0,w21
	add	w1,w20,w21
	bl	set_prg
	add	w21,w21,1
	cmp	w21,4
	b.lo	.l
	ldr	x30,[sp],16
	ret

; w0 = adresse, w1 = valeur
mapper_write:
	ldr	w2,[x19,n_mapper]
	cmp	w2,1
	b.eq	.m1
	cmp	w2,4
	b.eq	.m4
	cbz	w2,.r
	str	w1,[x19,n_simpleBank]
	b	update_banks
.r:	ret
.m1:	tbz	w1,7,.a
	str	wzr,[x19,n_mmc1Shift]
	str	wzr,[x19,n_mmc1Count]
	ldr	w0,[x19,n_mmc1Ctrl]
	orr	w0,w0,0x0C
	str	w0,[x19,n_mmc1Ctrl]
	b	update_banks
.a:	ldr	x2,[x19,n_cycles]
	ldr	x3,[x19,n_mmc1Last]
	add	x3,x3,1
	cmp	x2,x3
	b.eq	.r
	str	x2,[x19,n_mmc1Last]
	ldr	w2,[x19,n_mmc1Shift]
	lsr	w2,w2,1
	and	w1,w1,1
	orr	w2,w2,w1,lsl 4
	str	w2,[x19,n_mmc1Shift]
	ldr	w3,[x19,n_mmc1Count]
	add	w3,w3,1
	str	w3,[x19,n_mmc1Count]
	cmp	w3,5
	b.ne	.r
	ubfx	w0,w0,13,2
	add	x1,x19,n_mmc1Ctrl
	str	w2,[x1,x0,lsl 2]		; Ctrl, Chr0, Chr1, Prg se suivent
	str	wzr,[x19,n_mmc1Shift]
	str	wzr,[x19,n_mmc1Count]
	b	update_banks
.m4:	movl	w2,0xE001
	and	w2,w0,w2
	mov	w3,0x8000
	cmp	w2,w3
	b.ne	.1
	and	w2,w1,7
	str	w2,[x19,n_mmc3Sel]
	ubfx	w2,w1,6,1
	str	w2,[x19,n_mmc3PrgMode]
	ubfx	w2,w1,7,1
	str	w2,[x19,n_mmc3ChrMode]
	b	update_banks
.1:	add	w3,w3,1
	cmp	w2,w3
	b.ne	.2
	ldr	w2,[x19,n_mmc3Sel]
	add	x3,x19,n_mmc3Regs
	strb	w1,[x3,x2]
	b	update_banks
.2:	movl	w3,0xA000
	cmp	w2,w3
	b.ne	.3
	ldr	w2,[x19,n_fourScreen]
	cbnz	w2,.r
	and	w1,w1,1
	eor	w1,w1,1
	str	w1,[x19,n_mirror]
	ret
.3:	add	w3,w3,1
	cmp	w2,w3
	b.ne	.4
	ubfx	w1,w1,7,1
	str	w1,[x19,n_sramOn]
	ret
.4:	movl	w3,0xC000
	cmp	w2,w3
	b.ne	.5
	str	w1,[x19,n_mmc3Latch]
	ret
.5:	add	w3,w3,1
	cmp	w2,w3
	b.ne	.6
	str	wzr,[x19,n_mmc3Counter]
	mov	w1,1
	str	w1,[x19,n_mmc3Reload]
	ret
.6:	movl	w3,0xE000
	cmp	w2,w3
	b.ne	.7
	str	wzr,[x19,n_mmc3IrqOn]
	str	wzr,[x19,n_mmc3Irq]
	ret
.7:	mov	w1,1				; 0xE001
	str	w1,[x19,n_mmc3IrqOn]
	ret

; w0 = adresse sur le bus du PPU
a12_access:
	ubfx	w1,w0,12,1			; hi
	ldr	w2,[x19,n_a12]
	ldr	x3,[x19,n_cycles]
	cbz	w1,.lo
	cbnz	w2,.w
	ldr	x4,[x19,n_a12Low]
	sub	x4,x3,x4
	cmp	x4,3
	b.lo	.w
	str	w1,[x19,n_a12]
	b	mmc3_clock
.lo:	cbz	w2,.w
	str	x3,[x19,n_a12Low]
.w:	str	w1,[x19,n_a12]
	ret

mmc3_clock:
	ldr	w0,[x19,n_mapper]
	cmp	w0,4
	b.ne	.r
	ldr	w0,[x19,n_mmc3Counter]
	ldr	w1,[x19,n_mmc3Reload]
	cbz	w0,.l
	cbnz	w1,.l
	sub	w0,w0,1
	b	.s
.l:	ldr	w0,[x19,n_mmc3Latch]
	str	wzr,[x19,n_mmc3Reload]
.s:	str	w0,[x19,n_mmc3Counter]
	cbnz	w0,.r
	ldr	w0,[x19,n_mmc3IrqOn]
	cbz	w0,.r
	mov	w0,1
	str	w0,[x19,n_mmc3Irq]
.r:	ret

; ---------------------------------------------------------------------------
;  Le 2C02 : memoire et registres
; ---------------------------------------------------------------------------

; w0 = adresse $2000-$2FFF.. -> x0 = octet de la table de noms (perd x1, x2)
nt_ptr:
	and	w1,w0,0xFFF			; (ad - 0x2000) & 0xFFF = ad & 0xFFF
	lsr	w2,w1,10			; table
	ldr	w0,[x19,n_mirror]
	cbz	w0,.h
	cmp	w0,1
	b.eq	.v
	cmp	w0,2
	b.eq	.a
	cmp	w0,3
	b.eq	.b
	b	.m
.h:	lsr	w2,w2,1
	b	.m
.v:	and	w2,w2,1
	b	.m
.a:	mov	w2,0
	b	.m
.b:	mov	w2,1
.m:	and	w1,w1,0x3FF
	orr	w1,w1,w2,lsl 10
	add	x0,x19,N_CIRAM
	add	x0,x0,x1
	ret

; w0 = adresse -> x0 = octet de CHR (perd x1)
macro chr_ptr {
	ubfx	w1,w0,10,3
	and	w0,w0,0x3FF
	add	x1,x19,x1,lsl 3
	ldr	x1,[x1,n_chrptr]
	add	x0,x1,x0
}

; w0 = adresse -> w0
ppu_read:
	and	w0,w0,0x3FFF
	cmp	w0,0x2000
	b.hs	.1
	chr_ptr
	ldrb	w0,[x0]
	ret
.1:	movl	w1,0x3F00
	cmp	w0,w1
	b.hs	.2
	str	x30,[sp,-16]!
	bl	nt_ptr
	ldrb	w0,[x0]
	ldr	x30,[sp],16
	ret
.2:	and	w0,w0,0x1F
	mov	w2,0x13
	and	w1,w0,w2
	cmp	w1,0x10
	b.ne	.3
	and	w0,w0,0x0F
.3:	add	x1,x19,N_PALRAM
	ldrb	w0,[x1,x0]
	ret

; w0 = adresse, w4 = valeur
ppu_write:
	and	w0,w0,0x3FFF
	cmp	w0,0x2000
	b.hs	.1
	ldr	w1,[x19,n_chrIsRam]
	cbz	w1,.r
	chr_ptr
	strb	w4,[x0]
.r:	ret
.1:	movl	w1,0x3F00
	cmp	w0,w1
	b.hs	.2
	str	x30,[sp,-16]!
	bl	nt_ptr
	strb	w4,[x0]
	ldr	x30,[sp],16
	ret
.2:	and	w0,w0,0x1F
	mov	w2,0x13
	and	w1,w0,w2
	cmp	w1,0x10
	b.ne	.3
	and	w0,w0,0x0F
.3:	add	x1,x19,N_PALRAM
	and	w4,w4,0x3F
	strb	w4,[x1,x0]
	ret

; -> eq : A12 suivi par le processeur (!rendering || line >= 240)
macro a12_when lab {
	ldr	w0,[x19,n_mask]
	tst	w0,0x18
	b.eq	.yes#lab
	ldr	w0,[x19,n_line]
	cmp	w0,240
	b.lo	.no#lab
.yes#lab:
	ldr	w0,[x19,n_v]
	bl	a12_access
.no#lab:
}
macro v_step {
	ldr	w0,[x19,n_v]
	ldr	w1,[x19,n_ctrl]
	mov	w2,1
	tst	w1,4
	mov	w3,32
	csel	w2,w3,w2,ne
	add	w0,w0,w2
	and	w0,w0,0x7FFF
	str	w0,[x19,n_v]
}

; w0 = registre -> w0
reg_read:
	cmp	w0,2
	b.eq	.r2
	cmp	w0,4
	b.eq	.r4
	cmp	w0,7
	b.eq	.r7
	ldr	w0,[x19,n_openBus]
	ret
.r2:	ldr	w0,[x19,n_status]
	ldr	w1,[x19,n_openBus]
	and	w2,w0,0xE0
	and	w1,w1,0x1F
	orr	w1,w1,w2
	and	w0,w0,0x7F
	str	w0,[x19,n_status]
	str	wzr,[x19,n_wLatch]
	ldr	w0,[x19,n_line]
	cmp	w0,241
	b.ne	.r2a
	ldr	w0,[x19,n_dot]
	cbnz	w0,.r2a
	str	wzr,[x19,n_nmiOccurred]
.r2a:	str	w1,[x19,n_openBus]
	mov	w0,w1
	ret
.r4:	ldr	w0,[x19,n_oamAddr]
	add	x1,x19,N_OAM
	ldrb	w0,[x1,x0]
	str	w0,[x19,n_openBus]
	ret
.r7:	str	x30,[sp,-16]!
	a12_when a
	ldr	w0,[x19,n_v]
	and	w1,w0,0x3FFF
	movl	w2,0x3F00
	cmp	w1,w2
	b.hs	.pal
	ldr	w9,[x19,n_readBuf]
	bl	ppu_read
	str	w0,[x19,n_readBuf]
	b	.st
.pal:	bl	ppu_read
	mov	w9,w0
	ldr	w0,[x19,n_v]
	sub	w0,w0,0x1000
	and	w0,w0,0xFFFF
	bl	ppu_read
	str	w0,[x19,n_readBuf]
.st:	v_step
	a12_when b
	str	w9,[x19,n_openBus]
	mov	w0,w9
	ldr	x30,[sp],16
	ret

; w0 = registre, w1 = valeur
reg_write:
	str	w1,[x19,n_openBus]
	cmp	w0,7
	b.hi	.r
	adr	x2,.t
	ldrb	w0,[x2,x0]
	adr	x2,.base
	add	x0,x2,x0,lsl 2
	br	x0
.t:	db	(.w0-.base)/4,(.w1-.base)/4,(.r-.base)/4,(.w3-.base)/4
	db	(.w4-.base)/4,(.w5-.base)/4,(.w6-.base)/4,(.w7-.base)/4
.base:
.r:	ret
.w0:	ldr	w2,[x19,n_ctrl]
	str	w1,[x19,n_ctrl]
	ldr	w3,[x19,n_t]
	and	w3,w3,0xFFFFF3FF
	and	w4,w1,3
	orr	w3,w3,w4,lsl 10
	str	w3,[x19,n_t]
	tbnz	w2,7,.r
	tbz	w1,7,.r
	ldr	w2,[x19,n_status]
	tbz	w2,7,.r
	mov	w2,1
	str	w2,[x19,n_nmiPending]
	ret
.w1:	str	w1,[x19,n_mask]
	ret
.w3:	str	w1,[x19,n_oamAddr]
	ret
.w4:	ldr	w0,[x19,n_oamAddr]
	add	x2,x19,N_OAM
	strb	w1,[x2,x0]
	add	w0,w0,1
	and	w0,w0,0xFF
	str	w0,[x19,n_oamAddr]
	ret
.w5:	ldr	w3,[x19,n_t]
	ldr	w2,[x19,n_wLatch]
	cbnz	w2,.w5b
	and	w3,w3,0xFFE0
	orr	w3,w3,w1,lsr 3
	and	w2,w1,7
	str	w2,[x19,n_fineX]
	b	.w5c
.w5b:	movl	w2,0x8C1F
	and	w3,w3,w2
	and	w2,w1,7
	orr	w3,w3,w2,lsl 12
	and	w2,w1,0xF8
	orr	w3,w3,w2,lsl 2
.w5c:	str	w3,[x19,n_t]
	ldr	w2,[x19,n_wLatch]
	eor	w2,w2,1
	str	w2,[x19,n_wLatch]
	ret
.w6:	ldr	w3,[x19,n_t]
	ldr	w2,[x19,n_wLatch]
	eor	w4,w2,1
	str	w4,[x19,n_wLatch]
	cbnz	w2,.w6b
	and	w3,w3,0x00FF
	and	w2,w1,0x3F
	orr	w3,w3,w2,lsl 8
	str	w3,[x19,n_t]
	ret
.w6b:	and	w3,w3,0xFF00
	orr	w3,w3,w1
	str	w3,[x19,n_t]
	str	w3,[x19,n_v]
	ldr	w0,[x19,n_mask]
	tst	w0,0x18
	b.eq	.w6c
	ldr	w0,[x19,n_line]
	cmp	w0,240
	b.lo	.r
.w6c:	mov	w0,w3
	b	a12_access
.w7:	str	x30,[sp,-16]!
	mov	w9,w1
	a12_when c
	ldr	w0,[x19,n_v]
	mov	w4,w9
	bl	ppu_write
	v_step
	a12_when d
	ldr	x30,[sp],16
	ret

; ---------------------------------------------------------------------------
;  Le 2C02 : le temps (w26 = cycles du processeur)
; ---------------------------------------------------------------------------

ppu_run:
	str	x30,[sp,-16]!
	ldr	w0,[x19,n_pal]
	cbnz	w0,.pal
	add	w11,w26,w26,lsl 1
.l:	cbz	w11,.f
	sub	w11,w11,1
	bl	ppu_dot
	b	.l
.pal:	ldr	x11,[x19,n_dotAcc]
	add	x11,x11,x26,lsl 4
.p:	cmp	x11,5
	b.lo	.q
	sub	x11,x11,5
	bl	ppu_dot
	b	.p
.q:	str	x11,[x19,n_dotAcc]
.f:	ldr	x30,[sp],16
	ret

; garde x11
ppu_dot:
	ldr	w2,[x19,n_line]
	ldr	w3,[x19,n_dot]
	cmp	w2,240
	b.hs	.nvis
	cmp	w3,1
	b.ne	.v1
	str	x30,[sp,-16]!
	str	x11,[sp,-16]!
	bl	render_line
	ldr	x11,[sp],16
	ldr	x30,[sp],16
	ldr	w2,[x19,n_line]
	mov	w3,1
.v1:	ldr	w0,[x19,n_sprite0Dot]
	cmp	w3,w0
	b.ne	.v2
	ldr	w0,[x19,n_status]
	orr	w0,w0,0x40
	str	w0,[x19,n_status]
.v2:	cmp	w3,256
	b.lo	.next
	ldr	w0,[x19,n_mask]
	tst	w0,0x18
	b.eq	.next
	cmp	w3,256
	b.ne	.v3
	ldr	w0,[x19,n_v]			; increment vertical
	and	w1,w0,0x7000
	cmp	w1,0x7000
	b.eq	.v4
	add	w0,w0,0x1000
	b	.v5
.v4:	and	w0,w0,0xFFFF8FFF
	ubfx	w1,w0,5,5
	cmp	w1,29
	b.ne	.v6
	mov	w1,0
	eor	w0,w0,0x0800
	b	.v7
.v6:	cmp	w1,31
	b.ne	.v8
	mov	w1,0
	b	.v7
.v8:	add	w1,w1,1
.v7:	mov	w4,0x03E0
	bic	w0,w0,w4
	orr	w0,w0,w1,lsl 5
.v5:	str	w0,[x19,n_v]
	b	.next
.v3:	cmp	w3,257
	b.ne	.v9
.hcopy:	ldr	w0,[x19,n_v]
	ldr	w1,[x19,n_t]
	movl	w4,0x041F
	bic	w0,w0,w4
	and	w1,w1,w4
	orr	w0,w0,w1
	str	w0,[x19,n_v]
	b	.next
.v9:	cmp	w3,260
	b.ne	.v10
	ldr	w0,[x19,n_ctrl]
	and	w0,w0,0x18
	cmp	w0,0x10
	b.eq	.next
	str	x30,[sp,-16]!
	bl	mmc3_clock
	ldr	x30,[sp],16
	ldr	w2,[x19,n_line]
	ldr	w3,[x19,n_dot]
	b	.next
.v10:	cmp	w3,324
	b.ne	.next
	ldr	w0,[x19,n_ctrl]
	and	w0,w0,0x18
	cmp	w0,0x10
	b.ne	.next
	str	x30,[sp,-16]!
	bl	mmc3_clock
	ldr	x30,[sp],16
	ldr	w2,[x19,n_line]
	ldr	w3,[x19,n_dot]
	b	.next
.nvis:	cmp	w2,241
	b.ne	.nv2
	cmp	w3,1
	b.ne	.nv2
	ldr	w0,[x19,n_status]
	orr	w0,w0,0x80
	str	w0,[x19,n_status]
	mov	w0,1
	str	w0,[x19,n_nmiOccurred]
	str	w0,[x19,n_frameDone]
	ldr	w1,[x19,n_ctrl]
	tbz	w1,7,.next
	str	w0,[x19,n_nmiPending]
	b	.next
.nv2:	ldr	w5,[x19,n_pal]
	cmp	w5,0
	mov	w0,261
	mov	w1,311
	csel	w5,w1,w0,ne			; lastLine
	cmp	w2,w5
	b.ne	.next
	cmp	w3,1
	b.ne	.nv3
	ldr	w0,[x19,n_status]
	and	w0,w0,0x1F
	str	w0,[x19,n_status]
	mov	w0,-1
	str	w0,[x19,n_sprite0Dot]
.nv3:	ldr	w0,[x19,n_mask]
	tst	w0,0x18
	b.eq	.next
	cmp	w3,257
	b.eq	.hcopy2
	cmp	w3,280
	b.ne	.nv4
	ldr	w0,[x19,n_v]
	ldr	w1,[x19,n_t]
	movl	w4,0x7BE0
	bic	w0,w0,w4
	and	w1,w1,w4
	orr	w0,w0,w1
	str	w0,[x19,n_v]
	b	.nv7
.hcopy2:
	ldr	w0,[x19,n_v]
	ldr	w1,[x19,n_t]
	movl	w4,0x041F
	bic	w0,w0,w4
	and	w1,w1,w4
	orr	w0,w0,w1
	str	w0,[x19,n_v]
	b	.nv7
.nv4:	cmp	w3,260
	b.ne	.nv5
	ldr	w0,[x19,n_ctrl]
	and	w0,w0,0x18
	cmp	w0,0x10
	b.eq	.nv7
	b	.nv6
.nv5:	cmp	w3,324
	b.ne	.nv7
	ldr	w0,[x19,n_ctrl]
	and	w0,w0,0x18
	cmp	w0,0x10
	b.ne	.nv7
.nv6:	str	x30,[sp,-16]!
	bl	mmc3_clock
	ldr	x30,[sp],16
	ldr	w2,[x19,n_line]
	ldr	w3,[x19,n_dot]
.nv7:	cmp	w3,339
	b.ne	.next
	ldr	w0,[x19,n_oddFrame]
	cbz	w0,.next
	ldr	w0,[x19,n_pal]
	cbnz	w0,.next
	add	w3,w3,1
.next:	add	w3,w3,1
	cmp	w3,340
	b.ls	.w
	mov	w3,0
	ldr	w5,[x19,n_pal]
	cmp	w5,0
	mov	w0,261
	mov	w1,311
	csel	w5,w1,w0,ne
	add	w2,w2,1
	cmp	w2,w5
	b.ls	.w2
	mov	w2,0
	ldr	w0,[x19,n_oddFrame]
	eor	w0,w0,1
	str	w0,[x19,n_oddFrame]
.w2:	str	w2,[x19,n_line]
	cmp	w2,240
	b.hs	.w
	mov	w0,-1
	str	w0,[x19,n_sprite0Dot]
.w:	str	w3,[x19,n_dot]
	ret

; ---------------------------------------------------------------------------
;  Une ligne de l'image
; ---------------------------------------------------------------------------

render_line:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	ldr	w20,[x19,n_mask]
	add	x21,x19,N_BG
	add	x22,x19,N_BGPAL
	tbnz	w20,3,.bg
	mov	x0,x21				; pas de fond
	mov	x1,W_BG
	bl	nes_zero
	b	.spr
.bg:	ldr	w23,[x19,n_v]			; vv
	ubfx	w24,w23,12,3			; fy
	ldr	w0,[x19,n_ctrl]
	ubfx	w25,w0,4,1
	lsl	w25,w25,12			; base
	mov	w26,0				; tile
	ldr	w27,[x19,n_fineX]
.tile:	and	w0,w23,0x0FFF
	orr	w0,w0,0x2000
	bl	nt_ptr
	ldrb	w9,[x0]				; nt
	and	w0,w23,0x0C00
	movl	w1,0x23C0
	orr	w0,w0,w1
	ubfx	w1,w23,4,6
	and	w1,w1,0x38
	orr	w0,w0,w1
	ubfx	w1,w23,2,3
	orr	w0,w0,w1
	bl	nt_ptr
	ldrb	w10,[x0]			; at
	ubfx	w0,w23,4,3
	and	w0,w0,4
	and	w1,w23,2
	orr	w0,w0,w1
	lsr	w10,w10,w0
	and	w10,w10,3			; pl
	add	w0,w25,w9,lsl 4
	add	w0,w0,w24
	chr_ptr
	ldrb	w11,[x0]			; lo
	ldrb	w12,[x0,8]			; hi
	lsl	w13,w26,3
	sub	w13,w13,w27			; px du premier point
	mov	w14,7				; 7 - b
.b:	cmp	w13,256
	b.hs	.bn				; (< 0 aussi)
	lsr	w0,w11,w14
	and	w0,w0,1
	lsr	w1,w12,w14
	and	w1,w1,1
	orr	w0,w0,w1,lsl 1
	strb	w0,[x21,x13]
	strb	w10,[x22,x13]
.bn:	add	w13,w13,1
	subs	w14,w14,1
	b.hs	.b
	and	w0,w23,0x1F
	cmp	w0,31
	b.ne	.inc
	and	w23,w23,0xFFE0
	eor	w23,w23,0x0400
	b	.nt
.inc:	add	w23,w23,1
.nt:	add	w26,w26,1
	cmp	w26,33
	b.lo	.tile
	tbnz	w20,1,.spr
	str	xzr,[x21]			; 8 points de gauche caches
.spr:	add	x23,x19,N_SPR
	add	x24,x19,N_SPRATTR
	mov	x0,x23
	mov	x1,256
	bl	nes_zero
	tbz	w20,4,.out0
	ldr	w25,[x19,n_line]
	cbz	w25,.out0
	ldr	w0,[x19,n_ctrl]
	tst	w0,0x20
	mov	w26,8
	mov	w1,16
	csel	w26,w1,w26,ne			; h
	mov	w27,0				; found
	mov	w9,0				; i
.s:	add	x0,x19,N_OAM
	add	x0,x0,x9,lsl 2
	ldrb	w1,[x0]
	add	w1,w1,1
	subs	w10,w25,w1			; row
	b.lt	.sn
	cmp	w10,w26
	b.ge	.sn
	add	w27,w27,1
	cmp	w27,8
	b.ls	.sf
	ldr	w0,[x19,n_status]
	orr	w0,w0,0x20
	str	w0,[x19,n_status]
	b	.out0
.sf:	ldrb	w11,[x0,1]			; tuile
	ldrb	w12,[x0,2]			; attr
	ldrb	w13,[x0,3]			; sx
	tbz	w12,7,.sv
	sub	w10,w26,1
	ldrb	w1,[x0]
	add	w1,w1,1
	sub	w1,w25,w1
	sub	w10,w10,w1
.sv:	cmp	w26,16
	b.ne	.s8
	and	w0,w11,1
	lsl	w0,w0,12
	and	w1,w11,0xFE
	add	w0,w0,w1,lsl 4
	cmp	w10,8
	b.lo	.s16
	add	w0,w0,16
.s16:	and	w1,w10,7
	add	w0,w0,w1
	b	.sa
.s8:	ldr	w0,[x19,n_ctrl]
	ubfx	w0,w0,3,1
	lsl	w0,w0,12
	add	w0,w0,w11,lsl 4
	add	w0,w0,w10
.sa:	chr_ptr
	ldrb	w14,[x0]			; lo
	ldrb	w15,[x0,8]			; hi
	mov	w10,0				; b
.bit:	add	w11,w13,w10			; px
	cmp	w11,256
	b.hs	.sn
	mov	w0,7
	sub	w0,w0,w10
	tst	w12,0x40
	csel	w0,w10,w0,ne			; bit
	lsr	w1,w14,w0
	and	w1,w1,1
	lsr	w2,w15,w0
	and	w2,w2,1
	orr	w1,w1,w2,lsl 1			; c
	cbz	w1,.bitn
	cmp	w11,8
	b.hs	.nclip
	tbz	w20,2,.bitn
.nclip:	cbnz	w9,.nhit
	tbz	w20,3,.nhit
	ldrb	w0,[x21,x11]
	cbz	w0,.nhit
	cmp	w11,255
	b.eq	.nhit
	ldr	w0,[x19,n_sprite0Dot]
	tbz	w0,31,.nhit
	ldr	w0,[x19,n_status]
	tbnz	w0,6,.nhit
	add	w0,w11,2
	str	w0,[x19,n_sprite0Dot]
.nhit:	ldrb	w0,[x23,x11]
	cbnz	w0,.bitn
	and	w0,w12,3
	orr	w1,w1,w0,lsl 2
	orr	w1,w1,0x10
	strb	w1,[x23,x11]
	strb	w12,[x24,x11]
.bitn:	add	w10,w10,1
	cmp	w10,8
	b.lo	.bit
.sn:	add	w9,w9,1
	cmp	w9,64
	b.lo	.s
.out0:	mov	w0,0x3F
	mov	w1,0x30
	tst	w20,1
	csel	w12,w1,w0,ne			; grey
	ldr	w0,[x19,n_line]
	mov	w1,NES_W
	mul	w0,w0,w1
	ldr	x9,[x19,n_fbptr]
	add	x9,x9,x0,lsl 2
	adr	x10,nes_rgb
	add	x11,x19,N_PALRAM
	mov	x13,0
.px:	ldrb	w0,[x21,x13]			; b
	ldrb	w1,[x23,x13]			; spr
	cbz	w1,.pb
	cbz	w0,.ps
	ldrb	w2,[x24,x13]
	tbnz	w2,5,.pb
.ps:	and	w1,w1,0x0F
	orr	w1,w1,0x10
	ldrb	w1,[x11,x1]
	b	.po
.pb:	cbz	w0,.p0
	ldrb	w1,[x22,x13]
	orr	w1,w0,w1,lsl 2
	ldrb	w1,[x11,x1]
	b	.po
.p0:	ldrb	w1,[x11]
.po:	and	w1,w1,w12
	ldr	w1,[x10,x1,lsl 2]
	str	w1,[x9,x13,lsl 2]
	add	x13,x13,1
	cmp	x13,NES_W
	b.lo	.px
	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

W_BG		= 256

nes_rgb:
	dw 0x747474,0x24188C,0x0000A8,0x44009C,0x8C0074,0xA80010,0xA40000,0x7C0800
	dw 0x402C00,0x004400,0x005000,0x003C14,0x183C5C,0x000000,0x000000,0x000000
	dw 0xBCBCBC,0x0070EC,0x2038EC,0x8000F0,0xBC00BC,0xE40058,0xD82800,0xC84C0C
	dw 0x887000,0x009400,0x00A800,0x009038,0x008088,0x000000,0x000000,0x000000
	dw 0xFCFCFC,0x3CBCFC,0x5C94FC,0xCC88FC,0xF478FC,0xFC74B4,0xFC7460,0xFC9838
	dw 0xF0BC3C,0x80D010,0x4CDC48,0x58F898,0x00E8D8,0x787878,0x000000,0x000000
	dw 0xFCFCFC,0xA8E4FC,0xC4D4FC,0xD4C8FC,0xFCC4FC,0xFCC4D8,0xFCBCB0,0xFCD8A8
	dw 0xFCE4A0,0xE0FCA0,0xA8F0BC,0xB0FCCC,0x9CFCF0,0xC4C4C4,0x000000,0x000000

; ---------------------------------------------------------------------------
;  L'APU
; ---------------------------------------------------------------------------

apu_len:
	db 10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14
	db 12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30
apu_duty:
	db 0,1,0,0,0,0,0,0, 0,1,1,0,0,0,0,0, 0,1,1,1,1,0,0,0, 1,0,0,1,1,1,1,1
apu_tri:
	db 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
	db 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
apu_noise:
	dh 4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068
	dh 4,8,14,30,60,88,118,148,188,236,354,472,708,944,1890,3778
apu_dmc:
	dh 428,380,340,320,286,254,226,214,190,160,142,128,106,84,72,54
	dh 398,354,316,298,276,236,210,198,176,148,132,118,98,78,66,50
apu_seq:
	dw 7457,14913,22371,29829,37281
	dw 8313,16627,24939,33253,41565
include 'nes_mix.inc'

; (w1 >> 3 -> w0 = duree)
macro len_lookup {
	lsr	w0,w1,3
	adr	x2,apu_len
	ldrb	w0,[x2,x0]
}
macro setbit base, off, bit {
	ubfx	w2,w1,bit,1
	str	w2,[base,off]
}

; w0 = adresse, w1 = valeur
apu_write:
	movl	w2,0x4008
	cmp	w0,w2
	b.hs	.hi
	ubfx	w2,w0,2,1
	mov	w3,PU_SIZE
	mul	w2,w2,w3
	add	x4,x19,n_pu
	add	x4,x4,x2
	and	w2,w0,3
	cbz	w2,.p0
	cmp	w2,1
	b.eq	.p1
	cmp	w2,2
	b.eq	.p2
	ldr	w0,[x4,PU_period]		; 3
	and	w0,w0,0xFF
	and	w2,w1,7
	orr	w0,w0,w2,lsl 8
	str	w0,[x4,PU_period]
	ldr	w2,[x4,PU_on]
	cbz	w2,.p3
	len_lookup
	str	w0,[x4,PU_len]
.p3:	str	wzr,[x4,PU_seq]
	mov	w0,1
	str	w0,[x4,PU_envStart]
	ret
.p0:	lsr	w0,w1,6
	str	w0,[x4,PU_duty]
	setbit	x4,PU_halt,5
	setbit	x4,PU_constVol,4
	and	w0,w1,15
	str	w0,[x4,PU_vol]
	str	w0,[x4,PU_envPeriod]
	ret
.p1:	setbit	x4,PU_sweepOn,7
	ubfx	w0,w1,4,3
	str	w0,[x4,PU_sweepPeriod]
	setbit	x4,PU_sweepNeg,3
	and	w0,w1,7
	str	w0,[x4,PU_sweepShift]
	mov	w0,1
	str	w0,[x4,PU_sweepReload]
	ret
.p2:	ldr	w0,[x4,PU_period]
	and	w0,w0,0x700
	orr	w0,w0,w1
	str	w0,[x4,PU_period]
	ret
.hi:	sub	w0,w0,w2			; 0..15 (0x4008..)
	add	x4,x19,n_tr
	add	x5,x19,n_no
	add	x6,x19,n_dmc
	cbz	w0,.t8
	cmp	w0,2
	b.eq	.tA
	cmp	w0,3
	b.eq	.tB
	cmp	w0,4
	b.eq	.nC
	cmp	w0,6
	b.eq	.nE
	cmp	w0,7
	b.eq	.nF
	cmp	w0,8
	b.eq	.d0
	cmp	w0,9
	b.eq	.d1
	cmp	w0,10
	b.eq	.d2
	cmp	w0,11
	b.eq	.d3
	cmp	w0,13
	b.eq	.s5
	cmp	w0,15
	b.eq	.s7
	ret
.t8:	setbit	x4,TR_halt,7
	and	w0,w1,0x7F
	str	w0,[x4,TR_linPeriod]
	ret
.tA:	ldr	w0,[x4,TR_period]
	and	w0,w0,0x700
	orr	w0,w0,w1
	str	w0,[x4,TR_period]
	ret
.tB:	ldr	w0,[x4,TR_period]
	and	w0,w0,0xFF
	and	w2,w1,7
	orr	w0,w0,w2,lsl 8
	str	w0,[x4,TR_period]
	ldr	w2,[x4,TR_on]
	cbz	w2,.tB2
	len_lookup
	str	w0,[x4,TR_len]
.tB2:	mov	w0,1
	str	w0,[x4,TR_linReload]
	ret
.nC:	setbit	x5,NO_halt,5
	setbit	x5,NO_constVol,4
	and	w0,w1,15
	str	w0,[x5,NO_vol]
	str	w0,[x5,NO_envPeriod]
	ret
.nE:	setbit	x5,NO_mode,7
	and	w0,w1,15
	ldr	w2,[x19,n_pal]
	add	w0,w0,w2,lsl 4
	adr	x2,apu_noise
	ldrh	w0,[x2,x0,lsl 1]
	str	w0,[x5,NO_period]
	ret
.nF:	ldr	w2,[x5,NO_on]
	cbz	w2,.nF2
	len_lookup
	str	w0,[x5,NO_len]
.nF2:	mov	w0,1
	str	w0,[x5,NO_envStart]
	ret
.d0:	setbit	x6,DMC_irqOn,7
	setbit	x6,DMC_loop,6
	and	w0,w1,15
	ldr	w2,[x19,n_pal]
	add	w0,w0,w2,lsl 4
	adr	x2,apu_dmc
	ldrh	w0,[x2,x0,lsl 1]
	str	w0,[x6,DMC_rate]
	tbnz	w1,7,.r
	str	wzr,[x6,DMC_irq]
.r:	ret
.d1:	and	w0,w1,0x7F
	str	w0,[x6,DMC_level]
	ret
.d2:	mov	w0,0xC000
	add	w0,w0,w1,lsl 6
	and	w0,w0,0xFFFF
	str	w0,[x6,DMC_start]
	ret
.d3:	lsl	w0,w1,4
	add	w0,w0,1
	str	w0,[x6,DMC_len]
	ret
.s5:	add	x3,x19,n_pu
	ubfx	w2,w1,0,1
	str	w2,[x3,PU_on]
	cbnz	w2,.a1
	str	wzr,[x3,PU_len]
.a1:	add	x3,x3,PU_SIZE
	ubfx	w2,w1,1,1
	str	w2,[x3,PU_on]
	cbnz	w2,.a2
	str	wzr,[x3,PU_len]
.a2:	ubfx	w2,w1,2,1
	str	w2,[x4,TR_on]
	cbnz	w2,.a3
	str	wzr,[x4,TR_len]
.a3:	ubfx	w2,w1,3,1
	str	w2,[x5,NO_on]
	cbnz	w2,.a4
	str	wzr,[x5,NO_len]
.a4:	str	wzr,[x6,DMC_irq]
	tbnz	w1,4,.a5
	str	wzr,[x6,DMC_remain]
	ret
.a5:	ldr	w0,[x6,DMC_remain]
	cbnz	w0,.r
	ldr	w0,[x6,DMC_start]
	str	w0,[x6,DMC_addr]
	ldr	w0,[x6,DMC_len]
	str	w0,[x6,DMC_remain]
	ret
.s7:	lsr	w0,w1,7
	str	w0,[x19,n_frameMode]
	ubfx	w0,w1,6,1
	str	w0,[x19,n_frameIrqOff]
	cbz	w0,.s7a
	str	wzr,[x19,n_frameIrq]
.s7a:	ldr	x2,[x19,n_cycles]
	ldr	w3,[x19,n_apuAhead]
	add	x2,x2,x3
	tst	x2,1
	mov	w0,-4
	mov	w2,-3
	csel	w0,w2,w0,ne
	str	w0,[x19,n_frameCycle]
	ldr	w0,[x19,n_frameMode]
	cbz	w0,.r
	str	x30,[sp,-16]!
	bl	quarter_frame
	bl	half_frame
	ldr	x30,[sp],16
	ret

apu_status:
	add	x1,x19,n_pu
	ldr	w0,[x1,PU_len]
	cmp	w0,0
	cset	w0,ne
	ldr	w2,[x1,PU_SIZE+PU_len]
	cbz	w2,.1
	orr	w0,w0,2
.1:	ldr	w2,[x19,n_tr+TR_len]
	cbz	w2,.2
	orr	w0,w0,4
.2:	ldr	w2,[x19,n_no+NO_len]
	cbz	w2,.3
	orr	w0,w0,8
.3:	ldr	w2,[x19,n_dmc+DMC_remain]
	cbz	w2,.4
	orr	w0,w0,0x10
.4:	ldr	w2,[x19,n_frameIrq]
	cbz	w2,.5
	orr	w0,w0,0x40
.5:	ldr	w2,[x19,n_dmc+DMC_irq]
	cbz	w2,.6
	orr	w0,w0,0x80
.6:	str	wzr,[x19,n_frameIrq]
	ret

; x1 = bloc, avec les decalages de l'enveloppe du canal
macro envelope es, dv, pr, vl, ht {
	local e1, e2, e3, e4
	ldr	w0,[x1,es]
	cbz	w0,e1
	str	wzr,[x1,es]
	mov	w0,15
	str	w0,[x1,vl]
	ldr	w0,[x1,pr]
	str	w0,[x1,dv]
	b	e4
e1:	ldr	w0,[x1,dv]
	cbz	w0,e2
	sub	w0,w0,1
	str	w0,[x1,dv]
	b	e4
e2:	ldr	w0,[x1,pr]
	str	w0,[x1,dv]
	ldr	w0,[x1,vl]
	cbz	w0,e3
	sub	w0,w0,1
	str	w0,[x1,vl]
	b	e4
e3:	ldr	w0,[x1,ht]
	cbz	w0,e4
	mov	w0,15
	str	w0,[x1,vl]
e4:
}

quarter_frame:
	add	x1,x19,n_pu
	envelope PU_envStart,PU_envDiv,PU_envPeriod,PU_envVol,PU_halt
	add	x1,x1,PU_SIZE
	envelope PU_envStart,PU_envDiv,PU_envPeriod,PU_envVol,PU_halt
	add	x1,x19,n_no
	envelope NO_envStart,NO_envDiv,NO_envPeriod,NO_envVol,NO_halt
	add	x1,x19,n_tr
	ldr	w0,[x1,TR_linReload]
	cbz	w0,.a
	ldr	w0,[x1,TR_linPeriod]
	str	w0,[x1,TR_linCounter]
	b	.b
.a:	ldr	w0,[x1,TR_linCounter]
	cbz	w0,.b
	sub	w0,w0,1
	str	w0,[x1,TR_linCounter]
.b:	ldr	w0,[x1,TR_halt]
	cbnz	w0,.c
	str	wzr,[x1,TR_linReload]
.c:	ret

; x1 = canal, w2 = 1 : le premier -> w0 = cible du balayage
sweep_target:
	ldr	w3,[x1,PU_period]
	ldr	w0,[x1,PU_sweepShift]
	lsr	w4,w3,w0
	ldr	w0,[x1,PU_sweepNeg]
	cbz	w0,.p
	sub	w0,w3,w4
	sub	w0,w0,w2
	ret
.p:	add	w0,w3,w4
	ret

half_frame:
	str	x30,[sp,-16]!
	add	x1,x19,n_pu
	mov	w2,1
	bl	.pulse
	add	x1,x1,PU_SIZE
	mov	w2,0
	bl	.pulse
	ldr	w0,[x19,n_tr+TR_len]
	cbz	w0,.t
	ldr	w2,[x19,n_tr+TR_halt]
	cbnz	w2,.t
	sub	w0,w0,1
	str	w0,[x19,n_tr+TR_len]
.t:	ldr	w0,[x19,n_no+NO_len]
	cbz	w0,.n
	ldr	w2,[x19,n_no+NO_halt]
	cbnz	w2,.n
	sub	w0,w0,1
	str	w0,[x19,n_no+NO_len]
.n:	ldr	x30,[sp],16
	ret
.pulse:	ldr	w0,[x1,PU_len]
	cbz	w0,.p1
	ldr	w3,[x1,PU_halt]
	cbnz	w3,.p1
	sub	w0,w0,1
	str	w0,[x1,PU_len]
.p1:	mov	x5,x30
	bl	sweep_target
	mov	x30,x5
	ldr	w2,[x1,PU_sweepDiv]
	cbnz	w2,.p2
	ldr	w3,[x1,PU_sweepOn]
	cbz	w3,.p2
	ldr	w3,[x1,PU_sweepShift]
	cbz	w3,.p2
	ldr	w3,[x1,PU_period]
	cmp	w3,8
	b.lt	.p2
	cmp	w0,0x7FF
	b.gt	.p2
	str	w0,[x1,PU_period]
.p2:	cbz	w2,.p3
	ldr	w3,[x1,PU_sweepReload]
	cbnz	w3,.p3
	sub	w2,w2,1
	str	w2,[x1,PU_sweepDiv]
	ret
.p3:	ldr	w0,[x1,PU_sweepPeriod]
	str	w0,[x1,PU_sweepDiv]
	str	wzr,[x1,PU_sweepReload]
	ret

; -> w0 = echantillon (0..40000) ; perd x1-x7
mix_sample:
	mov	x7,x30
	mov	w6,0				; p1 + p2
	add	x1,x19,n_pu
	mov	w2,1
	bl	.pv
	add	x1,x1,PU_SIZE
	mov	w2,0
	bl	.pv
	ldr	w0,[x19,n_tr+TR_seq]
	adr	x1,apu_tri
	ldrb	w0,[x1,x0]			; tv
	add	w5,w0,w0,lsl 1
	add	x1,x19,n_no
	ldr	w0,[x1,NO_len]
	cbz	w0,.nz
	ldr	w0,[x1,NO_lfsr]
	tbnz	w0,0,.nz
	ldr	w0,[x1,NO_constVol]
	cbz	w0,.ne
	ldr	w0,[x1,NO_vol]
	b	.nv
.ne:	ldr	w0,[x1,NO_envVol]
.nv:	add	w5,w5,w0,lsl 1
.nz:	ldr	w0,[x19,n_dmc+DMC_level]
	add	w5,w5,w0
	adr	x1,mix_pulse
	ldrh	w0,[x1,x6,lsl 1]
	adr	x1,mix_tnd
	ldrh	w1,[x1,x5,lsl 1]
	add	w0,w0,w1
	br	x7
.pv:	mov	x5,x30
	bl	sweep_target
	mov	x30,x5
	ldr	w3,[x1,PU_len]
	cbz	w3,.pr
	ldr	w3,[x1,PU_period]
	cmp	w3,8
	b.lt	.pr
	ldr	w3,[x1,PU_sweepNeg]
	cbnz	w3,.p1
	cmp	w0,0x7FF
	b.gt	.pr
.p1:	ldr	w3,[x1,PU_duty]
	ldr	w4,[x1,PU_seq]
	add	w3,w4,w3,lsl 3
	adr	x4,apu_duty
	ldrb	w3,[x4,x3]
	cbz	w3,.pr
	ldr	w3,[x1,PU_constVol]
	cbz	w3,.pe
	ldr	w3,[x1,PU_vol]
	b	.pa
.pe:	ldr	w3,[x1,PU_envVol]
.pa:	add	w6,w6,w3
.pr:	ret

; w0 = cycles
apu_run:
	cbz	w0,.ret
	stp	x30,x9,[sp,-16]!
	stp	x10,x11,[sp,-16]!
	mov	w9,w0
	adr	x10,apu_seq
	ldr	w0,[x19,n_pal]
	cbz	w0,.lp
	add	x10,x10,20
.lp:	ldr	w11,[x19,n_frameCycle]
	add	w11,w11,1
	str	w11,[x19,n_frameCycle]
	ldp	w12,w13,[x10]			; seq[0], seq[1]
	ldr	w14,[x10,8]			; seq[2]
	cmp	w11,w12
	b.eq	.q
	cmp	w11,w14
	b.eq	.q
	cmp	w11,w13
	b.eq	.qh
	ldr	w0,[x19,n_frameMode]
	cbnz	w0,.seq2
	ldr	w0,[x10,12]
	cmp	w11,w0
	b.ne	.seq2
.qh:	bl	quarter_frame
	bl	half_frame
	b	.seq2
.q:	bl	quarter_frame
.seq2:	ldr	w11,[x19,n_frameCycle]
	ldr	w0,[x19,n_frameMode]
	cbnz	w0,.m5
	ldr	w1,[x10,12]
	sub	w2,w1,1
	cmp	w11,w2
	b.lt	.chan
	ldr	w0,[x19,n_frameIrqOff]
	cbnz	w0,.f1
	mov	w0,1
	str	w0,[x19,n_frameIrq]
.f1:	add	w1,w1,1
	cmp	w11,w1
	b.ne	.chan
	str	wzr,[x19,n_frameCycle]
	mov	w11,0
	b	.chan
.m5:	ldr	w1,[x10,16]
	cmp	w11,w1
	b.ne	.m5b
	bl	quarter_frame
	bl	half_frame
	b	.chan
.m5b:	add	w1,w1,1
	cmp	w11,w1
	b.ne	.chan
	str	wzr,[x19,n_frameCycle]
	mov	w11,0
.chan:	tbz	w11,0,.noise
	add	x1,x19,n_pu
	bl	.pt
	add	x1,x1,PU_SIZE
	bl	.pt
.noise:	add	x1,x19,n_no
	ldr	w0,[x1,NO_timer]
	cbz	w0,.n1
	sub	w0,w0,1
	str	w0,[x1,NO_timer]
	b	.tri
.n1:	ldr	w0,[x1,NO_period]
	cbz	w0,.n2
	sub	w0,w0,1
.n2:	str	w0,[x1,NO_timer]
	ldr	w2,[x1,NO_lfsr]
	ldr	w3,[x1,NO_mode]
	mov	w0,1
	mov	w4,6
	cmp	w3,0
	csel	w3,w4,w0,ne
	lsr	w3,w2,w3
	eor	w3,w3,w2
	and	w3,w3,1
	lsr	w2,w2,1
	orr	w2,w2,w3,lsl 14
	str	w2,[x1,NO_lfsr]
.tri:	add	x1,x19,n_tr
	ldr	w0,[x1,TR_timer]
	cbz	w0,.t1
	sub	w0,w0,1
	str	w0,[x1,TR_timer]
	b	.dmc
.t1:	ldr	w0,[x1,TR_period]
	str	w0,[x1,TR_timer]
	cmp	w0,2
	b.lt	.dmc
	ldr	w0,[x1,TR_len]
	cbz	w0,.dmc
	ldr	w0,[x1,TR_linCounter]
	cbz	w0,.dmc
	ldr	w0,[x1,TR_seq]
	add	w0,w0,1
	and	w0,w0,31
	str	w0,[x1,TR_seq]
.dmc:	add	x1,x19,n_dmc
	ldr	w0,[x1,DMC_timer]
	cbz	w0,.d1
	sub	w0,w0,1
	str	w0,[x1,DMC_timer]
	b	.rd
.d1:	ldr	w0,[x1,DMC_rate]
	sub	w0,w0,1
	str	w0,[x1,DMC_timer]
	ldr	w0,[x1,DMC_silence]
	cbnz	w0,.d4
	ldr	w2,[x1,DMC_level]
	ldr	w3,[x1,DMC_shift]
	tbz	w3,0,.d2
	cmp	w2,125
	b.gt	.d3
	add	w2,w2,2
	b	.d3
.d2:	cmp	w2,2
	b.lt	.d3
	sub	w2,w2,2
.d3:	str	w2,[x1,DMC_level]
	lsr	w3,w3,1
	str	w3,[x1,DMC_shift]
.d4:	ldr	w0,[x1,DMC_bits]
	sub	w0,w0,1
	str	w0,[x1,DMC_bits]
	cmp	w0,0
	b.gt	.rd
	mov	w0,8
	str	w0,[x1,DMC_bits]
	ldr	w0,[x1,DMC_bufFull]
	cbz	w0,.d5
	ldr	w0,[x1,DMC_buf]
	str	w0,[x1,DMC_shift]
	str	wzr,[x1,DMC_bufFull]
	str	wzr,[x1,DMC_silence]
	b	.rd
.d5:	mov	w0,1
	str	w0,[x1,DMC_silence]
.rd:	ldr	w0,[x1,DMC_bufFull]
	cbnz	w0,.smp
	ldr	w2,[x1,DMC_remain]
	cmp	w2,0
	b.le	.smp
	ldr	w0,[x1,DMC_addr]
	bl	nes_read
	add	x1,x19,n_dmc
	str	w0,[x1,DMC_buf]
	mov	w0,1
	str	w0,[x1,DMC_bufFull]
	ldr	w0,[x1,DMC_addr]
	add	w0,w0,1
	mov	w2,0x8000
	tst	w0,0x10000
	csel	w0,w2,w0,ne
	str	w0,[x1,DMC_addr]
	ldr	w2,[x1,DMC_remain]
	sub	w2,w2,1
	str	w2,[x1,DMC_remain]
	cbnz	w2,.smp
	ldr	w0,[x1,DMC_loop]
	cbz	w0,.r1
	ldr	w0,[x1,DMC_start]
	str	w0,[x1,DMC_addr]
	ldr	w0,[x1,DMC_len]
	str	w0,[x1,DMC_remain]
	b	.smp
.r1:	ldr	w0,[x1,DMC_irqOn]
	cbz	w0,.smp
	mov	w0,1
	str	w0,[x1,DMC_irq]
.smp:	ldr	x0,[x19,n_sampleAcc]
	ldr	w1,[x19,n_rate]
	ldr	w2,[x19,n_cpuHz]
	add	x0,x0,x1
	cmp	x0,x2
	b.hs	.s1
	str	x0,[x19,n_sampleAcc]
	b	.nxt
.s1:	sub	x0,x0,x2
	str	x0,[x19,n_sampleAcc]
	bl	mix_sample
	ldr	w1,[x19,n_lastOut]
	str	w0,[x19,n_lastOut]
	sub	w0,w0,w1
	sxtw	x0,w0
	lsl	x0,x0,16
	ldr	x1,[x19,n_hpAcc]
	sub	x0,x0,x1,asr 9
	add	x1,x1,x0
	str	x1,[x19,n_hpAcc]
	asr	x1,x1,16
	movl	x2,32767
	cmp	x1,x2
	csel	x1,x2,x1,gt
	mov	x2,-32768
	cmp	x1,x2
	csel	x1,x2,x1,lt
	ldr	w3,[x19,n_atail]
	add	w4,w3,1
	and	w4,w4,NES_ABUF-1
	ldr	w5,[x19,n_ahead]
	cmp	w4,w5
	b.eq	.nxt
	add	x5,x19,N_ABUF
	add	x5,x5,x3,lsl 2
	strh	w1,[x5]
	strh	w1,[x5,2]
	str	w4,[x19,n_atail]
.nxt:	subs	w9,w9,1
	b.ne	.lp
	ldp	x10,x11,[sp],16
	ldp	x30,x9,[sp],16
.ret:	ret
.pt:	ldr	w0,[x1,PU_timer]		; (feuille)
	cbz	w0,.pt1
	sub	w0,w0,1
	str	w0,[x1,PU_timer]
	ret
.pt1:	ldr	w0,[x1,PU_period]
	str	w0,[x1,PU_timer]
	ldr	w0,[x1,PU_seq]
	add	w0,w0,1
	and	w0,w0,7
	str	w0,[x1,PU_seq]
	ret

; L'APU rejoint le dernier cycle de l'instruction (un acces a ses registres)
apu_sync:
	str	x30,[sp,-16]!
	ldr	w0,[x19,n_apuPending]
	cbz	w0,.a
	str	wzr,[x19,n_apuPending]
	bl	apu_run
.a:	ldr	w0,[x19,n_instrCyc]
	sub	w0,w0,1
	ldr	w1,[x19,n_apuAhead]
	cmp	w1,w0
	b.ge	.f
	str	w0,[x19,n_apuAhead]
	sub	w0,w0,w1
	bl	apu_run
.f:	ldr	x30,[sp],16
	ret

; ---------------------------------------------------------------------------
;  Les 256 codes
; ---------------------------------------------------------------------------
	align	4
optab:
rept 256 i:0 {
	dw	h_#i - optab
}
include 'nes_ops.inc'
