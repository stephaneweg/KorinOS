; ============================================================================
;  snesemu - l'emulateur Super Nintendo, en assembleur (docs/EMULATEURS.md).
;	snesemu [rom.sfc]
;  Le coeur : snes_core.inc. Tout le reste : EmuKit. Boutons par leur place,
;  comme sur la manette de Nintendo.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/pad.inc'
include '../../kits/emukit/emukit.inc'

SND_HZ		= 44100

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/emukit/emukit.abi'

start:
	mov	x19,x0				; arguments
	bl	kits_open
	adr	x0,m_bad				; (ui_message ne traduit pas son texte)
	bl	lang_tr
	adr	x1,desc
	str	x0,[x1,EMD_ERROR]
	mov	x0,0				; la machine, a zero
	movl	x1,SNES_SIZE
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.fail
	adr	x1,desc
	str	x0,[x1,EMD_CTX]
	mov	x1,SND_HZ
	bl	snes_audio_rate
	adr	x0,desc
	mov	x1,0
	cbz	x19,.run
	ldrb	w2,[x19]
	cbz	w2,.run
	mov	x1,x19
.run:	bl	emu_run
	sys	SYS_EXIT
.fail:	mov	x0,1
	sys	SYS_EXIT

; (ctx, rom, octets) -> 1 / 0
cb_load:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	snes_load
	cbz	x0,.out
	ldrb	w0,[x19,s_pal]
	movl	x1,60099
	movl	x2,50007
	cmp	w0,0
	csel	x1,x2,x1,ne
	adr	x0,desc
	str	x1,[x0,EMD_FPS]
	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; (ctx, pixels, pas, boutons PAD_*, drapeaux)
cb_frame:
	mov	x4,x3
	mov	x3,0
	adr	x5,pad_map
.m:	ldr	w6,[x5],4
	cbz	w6,.f
	ldr	w7,[x5],4
	tst	x4,x6
	b.eq	.m
	orr	x3,x3,x7
	b	.m
.f:	and	x4,x3,0x0300			; pas gauche et droite a la fois
	cmp	x4,0x0300
	b.ne	.v
	bic	x3,x3,0x0300
.v:	and	x4,x3,0x0C00
	cmp	x4,0x0C00
	b.ne	.g
	bic	x3,x3,0x0C00
.g:	b	snes_frame

; (ctx) : la sauvegarde survit a la remise a zero
cb_reset:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	snes_reset
	strb	wzr,[x19,s_sramDirty]
	ldr	x19,[sp],16
	epilogue

include 'snes_core.inc'

pad_map:
	dw	PAD_RIGHT, 0x0100
	dw	PAD_LEFT, 0x0200
	dw	PAD_UP, 0x0800
	dw	PAD_DOWN, 0x0400
	dw	PAD_B, 0x0080			; A de la SNES
	dw	PAD_A, 0x8000			; B
	dw	PAD_Y, 0x0040			; X
	dw	PAD_X, 0x4000			; Y
	dw	PAD_L, 0x0020
	dw	PAD_R, 0x0010
	dw	PAD_START, 0x1000
	dw	PAD_SELECT, 0x2000
	dw	0

m_bad		db 'Not a Super Nintendo ROM, or it needs a chip not supported yet (SA-1, Super FX).',0
s_name		db 'Super Nintendo',0
s_ext		db '*.sfc;*.smc',0
		align 4

segment readable writeable
kit_data
		align 8
desc:		dd s_name
		dd s_ext
		dd SNES_W, SNES_H
		dd 0, 0
		dd 60099
		dd SND_HZ
		dd 0				; ctx
		dd cb_load
		dd cb_frame
		dd snes_audio_read
		dd cb_reset
		dd snes_save_poll
		dd snes_set_save
		dd 0, 0, 0, 0
		dd 0
		dd m_bad
