; ============================================================================
;  gbaemu - l'emulateur Game Boy Advance, en assembleur (docs/EMULATEURS.md).
;	gbaemu [rom.gba]
;  Le coeur : gba_core.inc. Pas de BIOS de Nintendo : gba_bios.inc en est un de
;  remplacement. Tout le reste : EmuKit. Boutons par leur place, comme sur les
;  manettes de Nintendo.
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
	mov	x0,0				; la machine, a zero
	movl	x1,GBA_SIZE
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.fail
	adr	x1,desc
	str	x0,[x1,EMD_CTX]
	mov	x1,SND_HZ
	bl	gba_audio_rate
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
.f:	and	x4,x3,0x30			; pas gauche et droite a la fois
	cmp	x4,0x30
	b.ne	.v
	bic	x3,x3,0x30
.v:	and	x4,x3,0xC0
	cmp	x4,0xC0
	b.ne	.g
	bic	x3,x3,0xC0
.g:	b	gba_frame

include 'gba_core.inc'

pad_map:
	dw	PAD_RIGHT, 16
	dw	PAD_LEFT, 32
	dw	PAD_UP, 64
	dw	PAD_DOWN, 128
	dw	PAD_B or PAD_Y, 1		; A de la GBA
	dw	PAD_A or PAD_X, 2		; B
	dw	PAD_START, 8
	dw	PAD_SELECT, 4
	dw	PAD_L or PAD_L2, 512
	dw	PAD_R or PAD_R2, 256
	dw	0

m_bad		db 'Not a Game Boy Advance ROM.',0
s_name		db 'Game Boy Advance',0
s_ext		db '*.gba',0
		align 4

segment readable writeable
kit_data
		align 8
desc:		dd s_name
		dd s_ext
		dd GBA_W, GBA_H
		dd 0, 0
		dd 59727
		dd SND_HZ
		dd 0				; ctx
		dd gba_load
		dd cb_frame
		dd gba_audio_read
		dd gba_reset
		dd gba_save_poll
		dd gba_set_save
		dd 0, 0, 0, 0
		dd 0
		dd m_bad
