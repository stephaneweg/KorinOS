; ============================================================================
;  nesemu - l'emulateur NES / Famicom, en assembleur (docs/EMULATEURS.md).
;	nesemu [rom.nes]
;  Le coeur : nes_core.inc. Tout le reste (fenetre, son, manette, cadence,
;  <rom>.sav, pause) : EmuKit. Boutons par leur place, comme sur les manettes
;  de Nintendo : a droite et en haut A, en bas et a gauche B.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/pad.inc'
include '../../kits/emukit/emukit.inc'

NES_RATE	= 44100

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/emukit/emukit.abi'

start:
	mov	x19,x0				; arguments
	bl	kits_open
	adr	x0,m_mapper			; (ui_message ne traduit pas son texte)
	bl	lang_tr
	adr	x1,desc
	str	x0,[x1,EMD_ERROR]
	mov	x0,0				; la machine, a zero
	movl	x1,NES_SIZE
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.fail
	adr	x1,desc
	str	x0,[x1,EMD_CTX]
	mov	x1,NES_RATE
	bl	nes_audio_rate
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

; (ctx, rom, octets) -> 1 / 0 ; PAL : l'en-tete, ou le nom du fichier
cb_load:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	nes_load
	cbz	x0,.out
	bl	emu_rom_path
	bl	name_pal
	cbz	x0,.h
	mov	x1,1
	mov	x0,x19
	bl	nes_set_pal
.h:	ldr	w0,[x19,n_pal]
	movl	x1,60099
	movl	x2,50007
	cmp	w0,0
	csel	x1,x2,x1,ne
	adr	x0,desc
	str	x1,[x0,EMD_FPS]
	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin -> x0 = 1 : « (Europe) », « (E) », « (PAL) »... dans le nom
name_pal:
	adr	x1,pal_tags
.t:	ldrb	w2,[x1]
	cbz	w2,.non
	mov	x2,x0
.p:	mov	x3,x2
	mov	x4,x1
.c:	ldrb	w5,[x4],1
	cbz	w5,.oui
	ldrb	w6,[x3],1
	cmp	w5,w6
	b.eq	.c
	ldrb	w6,[x2],1
	cbnz	w6,.p
.n:	ldrb	w5,[x1],1			; le motif suivant
	cbnz	w5,.n
	b	.t
.non:	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; (ctx, pixels, pas, boutons, drapeaux)
cb_frame:
	mov	x2,0				; PAD_* -> boutons de la NES
	mov	x4,x3
	mov	x5,PAD_B or PAD_Y
	tst	x4,x5
	b.eq	.1
	orr	x2,x2,1
.1:	mov	x5,PAD_A or PAD_X
	tst	x4,x5
	b.eq	.2
	orr	x2,x2,2
.2:	tbz	x4,12,.3			; PAD_SELECT
	orr	x2,x2,4
.3:	tbz	x4,13,.4			; PAD_START
	orr	x2,x2,8
.4:	tbz	x4,0,.5				; PAD_UP
	tbnz	x4,1,.6				; (pas les deux)
	orr	x2,x2,16
	b	.6
.5:	tbz	x4,1,.6
	orr	x2,x2,32
.6:	tbz	x4,2,.7				; PAD_LEFT
	tbnz	x4,3,.8
	orr	x2,x2,64
	b	.8
.7:	tbz	x4,3,.8
	orr	x2,x2,128
.8:	b	nes_frame

cb_save:				; (ctx, &pointeur) -> octets, 0 : rien de neuf
	mov	x5,x1
	mov	x6,x30
	bl	nes_save_poll
	mov	x30,x6
	str	x1,[x5]
	cmp	x0,0
	csel	x0,x2,xzr,ne
	ret

include 'nes_core.inc'

m_mapper	db 'This game uses a mapper not supported yet.',10,'Supported: 0, 1, 2, 3, 4, 7, 66.',0
s_name		db 'NES',0
s_ext		db '*.nes',0
pal_tags	db '(Europe',0,'(E)',0,'(PAL',0,'(Australia',0,'(Germany',0,'(France',0
		db '(Spain',0,'(Italy',0,'(Sweden',0,'(Netherlands',0,'(UK',0,0
		align 4

segment readable writeable
kit_data
		align 8
desc:		dd s_name
		dd s_ext
		dd NES_W, NES_H
		dd 0, 0
		dd 60099
		dd NES_RATE
		dd 0				; ctx
		dd cb_load
		dd cb_frame
		dd nes_audio_read
		dd nes_reset
		dd cb_save
		dd nes_set_save
		dd 0, 0, 0, 0
		dd 0
		dd m_mapper
