; ============================================================================
;  gbemu - l'emulateur Game Boy et Game Boy Color, en assembleur
;  (docs/EMULATEURS.md).
;	gbemu [rom.gb | rom.gbc]
;  Le coeur : gb_core.inc. Tout le reste : EmuKit. Boutons par leur place,
;  comme sur les manettes de Nintendo : a droite et en haut A, en bas et a
;  gauche B.
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
	movl	x1,GB_SIZE
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.fail
	adr	x1,desc
	str	x0,[x1,EMD_CTX]
	mov	x1,SND_HZ
	bl	gb_audio_rate
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
	mov	x5,PAD_B or PAD_Y
	tst	x4,x5
	b.eq	.1
	orr	x3,x3,16
.1:	mov	x5,PAD_A or PAD_X
	tst	x4,x5
	b.eq	.2
	orr	x3,x3,32
.2:	tbz	x4,12,.3			; PAD_SELECT
	orr	x3,x3,64
.3:	tbz	x4,13,.4			; PAD_START
	orr	x3,x3,128
.4:	tbz	x4,0,.5				; PAD_UP
	tbnz	x4,1,.6				; (pas les deux)
	orr	x3,x3,4
	b	.6
.5:	tbz	x4,1,.6
	orr	x3,x3,8
.6:	tbz	x4,2,.7				; PAD_LEFT
	tbnz	x4,3,.8
	orr	x3,x3,2
	b	.8
.7:	tbz	x4,3,.8
	orr	x3,x3,1
.8:	b	gb_frame

include 'gb_core.inc'

m_bad		db 'Not a Game Boy ROM.',0
s_name		db 'Game Boy',0
s_ext		db '*.gb;*.gbc',0
		align 4

segment readable writeable
kit_data
		align 8
desc:		dd s_name
		dd s_ext
		dd GB_W, GB_H
		dd 0, 0
		dd 59727
		dd SND_HZ
		dd 0				; ctx
		dd gb_load
		dd cb_frame
		dd gb_audio_read
		dd gb_reset
		dd gb_save_poll
		dd gb_set_save
		dd 0, 0, 0, 0
		dd 0
		dd m_bad
