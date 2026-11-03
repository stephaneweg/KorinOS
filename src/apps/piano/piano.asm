; piano - petit synthetiseur : deux octaves cliquables ou jouees au clavier
; (rangee AZERTY q s d f g h j k l m = do re mi fa sol la si do re mi ;
;  z e t y u o p = touches noires). Sinus par rotation de vecteur (FP),
; enveloppe exponentielle, flux d'AudioKit (stereo 16 bits 44,1 kHz).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W	= 700
WIN_H	= 260
KEYS_Y	= 70
WHITE_W	= 46
WHITE_H	= 170
BLACK_W	= 28
BLACK_H	= 105
N_WHITE	= 14
FRAMES	= 256			; trames generees par passe

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,ui_win				; variables de l'interface
	bl	ui_init
	mov	x0,WIN_W
	mov	x1,WIN_H
	adr	x2,title
	bl	ui_window
	tbnz	x0,63,quit
	movl	x0,SND_RATE			; flux s16 stereo 44,1 kHz
	mov	x1,2
	mov	x2,16
	mov	x3,0				; (sortie du process)
	bl	ak_open
	adr	x1,stream
	str	x0,[x1]
	bl	draw
.loop:	bl	feed				; garder le flux alimente
	mov	x0,8
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cmp	x0,WEV_KEY
	b.ne	.loop
	adr	x0,ui_ev			; touche
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.loop
	ldr	w1,[x0,EV_CHAR]
	adr	x2,keymap			; caractere -> demi-ton
	mov	x3,0
.k:	ldrb	w4,[x2,x3]
	cbz	w4,.loop
	cmp	w4,w1
	b.eq	.found
	add	x3,x3,1
	b	.k
.found:	adr	x2,keysemi
	ldrb	w0,[x2,x3]
	bl	note_on
	b	.loop
.mouse:	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,1
	b.ne	.loop
	ldrsw	x1,[x0,EV_X]
	ldrsw	x2,[x0,EV_Y]
	bl	key_at
	tbnz	x0,63,.loop
	bl	note_on
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; x1 = x, x2 = y (fenetre) -> x0 = demi-ton (0..23), -1 si aucun
key_at:
	sub	x2,x2,KEYS_Y
	cmp	x2,0
	b.lt	.none
	cmp	x2,WHITE_H
	b.ge	.none
	sub	x1,x1,20
	cmp	x1,0
	b.lt	.none
	cmp	x2,BLACK_H			; touches noires d'abord
	b.ge	.white
	mov	x3,0
.b:	cmp	x3,10
	b.hs	.white
	adr	x4,black_pos
	ldrb	w5,[x4,x3]			; blanche de gauche
	mov	x6,WHITE_W
	mul	x5,x5,x6
	add	x5,x5,WHITE_W-BLACK_W/2
	cmp	x1,x5
	b.lt	.bn
	add	x6,x5,BLACK_W
	cmp	x1,x6
	b.ge	.bn
	adr	x4,black_semi
	ldrb	w0,[x4,x3]
	ret
.bn:	add	x3,x3,1
	b	.b
.white:	mov	x3,WHITE_W
	udiv	x3,x1,x3
	cmp	x3,N_WHITE
	b.hs	.none
	adr	x4,white_semi
	ldrb	w0,[x4,x3]
	ret
.none:	mov	x0,-1
	ret

; x0 = demi-ton depuis do4 : frequence = 261,63 * 2^(n/12), enveloppe a 1
note_on:
	prologue
	str	x0,[sp,-16]!
	adr	x1,semitone_ratio
	ldr	d0,[x1,x0,lsl 3]
	adr	x1,c4_freq
	ldr	d1,[x1]
	fmul	d0,d0,d1			; frequence
	adr	x1,two_pi_over_rate
	ldr	d1,[x1]
	fmul	d0,d0,d1			; theta par echantillon
	fmul	d1,d0,d0			; cos/sin de theta (series de Taylor)
	fmov	d2,1.0
	fmov	d3,0.5
	fmul	d4,d1,d3
	fsub	d2,d2,d4			; 1 - t^2/2
	fmul	d4,d1,d1
	adr	x1,inv24
	ldr	d5,[x1]
	fmul	d4,d4,d5
	fadd	d2,d2,d4			; + t^4/24
	adr	x1,inv6
	ldr	d5,[x1]
	fmul	d4,d1,d5
	fmul	d4,d4,d0
	fsub	d3,d0,d4			; t - t^3/6
	adr	x1,osc
	str	d2,[x1,16]			; cos(theta)
	str	d3,[x1,24]			; sin(theta)
	fmov	d4,1.0
	str	d4,[x1]				; vecteur (1, 0)
	str	xzr,[x1,8]
	adr	x1,env
	fmov	d4,1.0
	str	d4,[x1]
	ldr	x0,[sp],16
	adr	x1,cur_note
	str	x0,[x1]
	bl	draw_keys
	epilogue

; produit des echantillons tant que le flux a de la place
feed:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
.again:	adr	x0,stream
	ldr	x0,[x0]
	tbnz	x0,63,.out
	mov	x1,sp
	bl	ak_status
	cbnz	x0,.out
	ldr	x0,[sp,8]			; place libre (octets)
	cmp	x0,FRAMES*4
	b.lo	.out
	ldr	x0,[sp]				; deja ~0,1 s d'avance : suffisant
	cmp	x0,16384
	b.hs	.out
	adr	x19,pcm
	adr	x1,osc
	ldp	d0,d1,[x1]			; (c, s)
	ldp	d2,d3,[x1,16]			; (cos t, sin t)
	adr	x1,env
	ldr	d4,[x1]
	adr	x1,decay
	ldr	d5,[x1]
	adr	x1,amp
	ldr	d6,[x1]
	mov	x20,FRAMES
.s:	fmul	d7,d0,d2			; rotation
	fmul	d16,d1,d3
	fsub	d7,d7,d16
	fmul	d16,d0,d3
	fmul	d17,d1,d2
	fadd	d1,d16,d17
	fmov	d0,d7
	fmul	d4,d4,d5			; enveloppe
	fmul	d7,d1,d4
	fmul	d7,d7,d6
	fcvtzs	w2,d7
	strh	w2,[x19],2			; gauche
	strh	w2,[x19],2			; droite
	subs	x20,x20,1
	b.ne	.s
	fmul	d7,d0,d0			; renormaliser (c, s)
	fmul	d16,d1,d1
	fadd	d7,d7,d16
	fsqrt	d7,d7
	fdiv	d0,d0,d7
	fdiv	d1,d1,d7
	adr	x1,osc
	stp	d0,d1,[x1]
	adr	x1,env
	str	d4,[x1]
	adr	x0,stream
	ldr	x0,[x0]
	adr	x1,pcm
	mov	x2,FRAMES*4
	bl	ak_write
	b	.again
.out:	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

draw:
	prologue
	adr	x0,s_title
	bl	tr_x0
	mov	x1,20
	mov	x2,34
	jeton	w3,TJ_TEXTE1
	mov	x4,20
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	adr	x0,s_help
	bl	tr_x0
	mov	x1,20
	mov	x2,56
	jeton	w3,TJ_TEXTE2
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	bl	draw_keys
	bl	ui_update_all
	epilogue

draw_keys:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
.w:	cmp	x19,N_WHITE
	b.hs	.blacks
	mov	x0,WHITE_W
	mul	x0,x0,x19
	add	x0,x0,20
	mov	x1,KEYS_Y
	mov	x2,WHITE_W-2
	mov	x3,WHITE_H
	movl	w4,0x00FFFFFF
	adr	x5,white_semi
	ldrb	w5,[x5,x19]
	adr	x6,cur_note
	ldr	x6,[x6]
	cmp	x5,x6
	b.ne	.wc
	jeton	w4,TJ_ACCENT_CLAIR
.wc:	bl	ui_fill
	mov	x0,WHITE_W
	mul	x0,x0,x19
	add	x0,x0,20
	mov	x1,KEYS_Y
	mov	x2,WHITE_W-2
	mov	x3,WHITE_H
	jeton	w4,TJ_LIGNE_FORTE
	bl	ui_frame
	add	x19,x19,1
	b	.w
.blacks:
	mov	x19,0
.b:	cmp	x19,10
	b.hs	.done
	adr	x0,black_pos
	ldrb	w0,[x0,x19]
	mov	x1,WHITE_W
	mul	x0,x0,x1
	add	x0,x0,20+WHITE_W-BLACK_W/2
	mov	x1,KEYS_Y
	mov	x2,BLACK_W
	mov	x3,BLACK_H
	movl	w4,0x00202833
	adr	x5,black_semi
	ldrb	w5,[x5,x19]
	adr	x6,cur_note
	ldr	x6,[x6]
	cmp	x5,x6
	b.ne	.bc
	jeton	w4,TJ_ACCENT
.bc:	bl	ui_fill
	add	x19,x19,1
	b	.b
.done:	mov	x0,0
	mov	x1,KEYS_Y
	mov	x2,WIN_W
	mov	x3,WHITE_H
	bl	ui_update
	ldp	x19,x20,[sp],16
	epilogue


include '../../inc/tr.inc'

segment readable writeable
kit_data

title		db 'Piano',0
s_title		db 'Piano Korin',0
s_help		db 'Cliquez les touches ou jouez avec  q s d f g h j k l m  (noires : z e t y u o p)',0
keymap		db 'qzsedftgyhujklopm',0
keysemi		db 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
white_semi	db 0,2,4,5,7,9,11,12,14,16,17,19,21,23
black_pos	db 0,1,3,4,5,7,8,10,11,12	; blanche a gauche de chaque noire
black_semi	db 1,3,6,8,10,13,15,18,20,22
		align 8
stream		dd -1
cur_note	dd -1
c4_freq		dd 261.6256
two_pi_over_rate dd 0.000142475857
inv24		dd 0.0416666667
inv6		dd 0.1666666667
decay		dd 0.99993
amp		dd 12000.0
semitone_ratio	dd 1.0, 1.0594631, 1.1224620, 1.1892071, 1.2599210, 1.3348399
		dd 1.4142136, 1.4983071, 1.5874011, 1.6817928, 1.7817974, 1.8877486
		dd 2.0, 2.1189262, 2.2449241, 2.3784142, 2.5198421, 2.6696797
		dd 2.8284271, 2.9966142, 3.1748021, 3.3635857, 3.5635949, 3.7754973
osc		dd 1.0, 0.0, 1.0, 0.0
env		dd 0.0
pcm		rb FRAMES*4
ui_data
