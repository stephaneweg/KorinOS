; horloge - horloge analogique anti-crenelee + heure numerique
; Pas d'horloge temps reel materielle : heure = decalage regle (boutons, garde
; dans SD0:/etc/clock.cfg en secondes) + temps ecoule depuis le demarrage.
; Rendu : couverture calculee par distance au segment / au cercle (flottants).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W	= 360
WIN_H	= 470
CX	= 180
CY	= 186
RAD	= 150
BTN_Y	= 414
BTN_W	= 96
BTN_H	= 34

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,ui_win				; variables de l'interface
	bl	ui_init
	mov	x0,WIN_W
	mov	x1,WIN_H
	adr	x2,title
	bl	tr2
	bl	ui_window
	tbnz	x0,63,quit
	bl	load_cfg
	bl	draw_buttons
	bl	draw_clock
	sys	SYS_TIME_MS
	mov	x1,1000
	udiv	x0,x0,x1
	mul	x0,x0,x1
	add	x19,x0,1000			; prochaine seconde pleine
.loop:	sys	SYS_TIME_MS
	subs	x0,x19,x0
	b.le	.tick
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MOUSE
	b.ne	.loop
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,1
	b.ne	.loop
	mov	x20,0				; bouton 0..2
.btn:	bl	btn_rect
	bl	ui_hit
	cbnz	x0,.press
	add	x20,x20,1
	cmp	x20,3
	b.lo	.btn
	b	.loop
.press:	adr	x0,adjust
	ldr	x1,[x0,x20,lsl 3]
	adr	x2,offset
	ldr	x3,[x2]
	add	x3,x3,x1
	movl	x4,86400
	sdiv	x5,x3,x4			; modulo 24 h, positif
	msub	x3,x5,x4,x3
	cmp	x3,0
	b.ge	.st
	add	x3,x3,x4
.st:	str	x3,[x2]
	bl	save_cfg
	bl	draw_clock
	b	.loop
.tick:	add	x19,x19,1000
	bl	draw_clock
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; x20 = bouton -> x0..x3 = rectangle
btn_rect:
	mov	x0,BTN_W+12
	mul	x0,x0,x20
	add	x0,x0,24
	mov	x1,BTN_Y
	mov	x2,BTN_W
	mov	x3,BTN_H
	ret

draw_buttons:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,0
.b:	bl	btn_rect
	adr	x4,labels
	ldr	x4,[x4,x20,lsl 3]
	mov	x5,0
	bl	ui_button
	add	x20,x20,1
	cmp	x20,3
	b.lo	.b
	mov	x0,0
	mov	x1,BTN_Y
	mov	x2,WIN_W
	mov	x3,BTN_H
	bl	ui_update
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = secondes de la journee
now_seconds:
	prologue
	sys	SYS_TIME_MS
	mov	x1,1000
	udiv	x0,x0,x1
	adr	x1,offset
	ldr	x1,[x1]
	add	x0,x0,x1
	movl	x1,86400
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	epilogue

; ---------------------------------------------------------------------------
draw_clock:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	bl	now_seconds
	mov	x19,x0
	; fond + cadran
	mov	x0,0
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,BTN_Y-4
	jeton	w4,TJ_FACE
	bl	ui_fill
	mov	x0,RAD+8
	movl	w1,0x00303A46
	bl	disc
	mov	x0,RAD+2
	movl	w1,0x00FFFFFF
	bl	disc
	; graduations
	mov	x20,0
.tk:	scvtf	s0,w20
	adr	x0,k_six_deg
	ldr	s1,[x0]
	fmul	s0,s0,s1			; angle
	mov	x0,x20
	mov	x1,5
	udiv	x2,x0,x1
	msub	x2,x2,x1,x0
	cbz	x2,.hour
	fmov	s4,1.0				; minute : fine
	mov	x0,RAD-10
	mov	x1,RAD-2
	movl	w2,0x008A96A6
	b	.dr
.hour:	fmov	s4,2.5
	mov	x0,RAD-22
	mov	x1,RAD-2
	movl	w2,0x00222B36
.dr:	bl	radial
	add	x20,x20,1
	cmp	x20,60
	b.lo	.tk
	; chiffres 12, 3, 6, 9
	adr	x0,s_12
	mov	x1,CX-14
	mov	x2,CY-RAD+52
	bl	numeral
	adr	x0,s_3
	mov	x1,CX+RAD-46
	mov	x2,CY+9
	bl	numeral
	adr	x0,s_6
	mov	x1,CX-7
	mov	x2,CY+RAD-32
	bl	numeral
	adr	x0,s_9
	mov	x1,CX-RAD+32
	mov	x2,CY+9
	bl	numeral
	; aiguilles
	mov	x0,3600				; heures : (s mod 43200) / 43200 tours
	movl	x1,43200
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19
	scvtf	s0,x2
	scvtf	s1,x1
	fdiv	s0,s0,s1
	bl	turn_to_rad
	fmov	s4,4.0
	mov	x0,-14
	mov	x1,RAD*52/100
	movl	w2,0x00222B36
	bl	radial
	mov	x1,3600				; minutes
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19
	scvtf	s0,x2
	scvtf	s1,x1
	fdiv	s0,s0,s1
	bl	turn_to_rad
	fmov	s4,2.75
	mov	x0,-18
	mov	x1,RAD*78/100
	movl	w2,0x00222B36
	bl	radial
	mov	x1,60				; secondes
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19
	scvtf	s0,x2
	scvtf	s1,x1
	fdiv	s0,s0,s1
	bl	turn_to_rad
	fmov	s4,1.0
	mov	x0,-26
	mov	x1,RAD*88/100
	movl	w2,0x00D9534F
	bl	radial
	mov	x0,7				; axe
	movl	w1,0x00D9534F
	bl	disc
	mov	x0,3
	movl	w1,0x00FFFFFF
	bl	disc
	; heure numerique
	sub	sp,sp,32
	mov	x0,sp
	mov	x1,3600
	udiv	x1,x19,x1
	bl	two_digits
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,60
	udiv	x2,x19,x1
	mov	x3,60
	udiv	x4,x2,x3
	msub	x1,x4,x3,x2
	bl	two_digits
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,60
	udiv	x2,x19,x1
	msub	x1,x2,x1,x19
	bl	two_digits
	mov	x0,sp
	mov	x1,30
	adr	x2,ui_font_b
	bl	ui_text_width
	mov	x1,WIN_W
	sub	x1,x1,x0
	lsr	x1,x1,1
	mov	x0,sp
	mov	x2,CY+RAD+50
	jeton	w3,TJ_TEXTE1
	mov	x4,30
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	add	sp,sp,32
	adr	x0,s_note
	bl	tr0
	mov	x1,24
	mov	x2,BTN_Y+BTN_H+16
	jeton	w3,TJ_TEXTE2
	mov	x4,11
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x0,0
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,WIN_H
	bl	ui_update
	ldp	d8,d9,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = x, x2 = ligne de base : chiffre du cadran
numeral:
	movl	w3,0x00222B36
	mov	x4,22
	adr	x5,ui_font_b
	mov	x6,0
	b	ui_text

; x0 = texte -> sa traduction ; x1 a x7 gardes
tr0:
	prologue
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	stp	x5,x6,[sp,-16]!
	str	x7,[sp,-16]!
	bl	ui_tr
	ldr	x7,[sp],16
	ldp	x5,x6,[sp],16
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	epilogue

; x2 = texte -> sa traduction ; les autres registres gardes
tr2:
	prologue
	str	x0,[sp,-16]!
	mov	x0,x2
	bl	tr0
	mov	x2,x0
	ldr	x0,[sp],16
	epilogue

; x0 = destination, x1 = 0..99 -> x0 avance, 0 final
two_digits:
	mov	x6,10
	udiv	x7,x1,x6
	msub	x8,x7,x6,x1
	add	w7,w7,'0'
	add	w8,w8,'0'
	strb	w7,[x0],1
	strb	w8,[x0],1
	strb	wzr,[x0]
	ret

; s0 = fraction de tour -> s0 = angle en radians
turn_to_rad:
	adr	x0,k_two_pi
	ldr	s1,[x0]
	fmul	s0,s0,s1
	ret

; s0 = angle (0 = midi, sens horaire), x0 = rayon interieur (peut etre <0),
; x1 = rayon exterieur, w2 = couleur, s4 = demi-epaisseur
radial:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	str	x2,[sp,-16]!
	fmov	s11,s4
	bl	sincos				; s0 = sin, s1 = cos
	fmov	s8,s0
	fmov	s9,s1
	mov	x0,CX
	scvtf	s10,x0
	mov	x0,CY
	scvtf	s5,x0
	scvtf	s6,x19				; A = centre + r_int * (sin, -cos)
	fmul	s0,s8,s6
	fadd	s0,s10,s0
	fmul	s1,s9,s6
	fsub	s1,s5,s1
	scvtf	s6,x20				; B = centre + r_ext * (sin, -cos)
	fmul	s2,s8,s6
	fadd	s2,s10,s2
	fmul	s3,s9,s6
	fsub	s3,s5,s3
	fmov	s4,s11
	ldr	x0,[sp],16
	bl	aa_segment
	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = rayon, w1 = couleur : disque centre sur (CX, CY), bord anti-crenele
disc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	scvtf	s8,x19				; rayon
	fmov	s9,0.5
	fadd	s8,s8,s9			; rayon + 0,5
	mov	x21,CY
	sub	x21,x21,x19
	sub	x21,x21,1
.y:	mov	x0,CY
	add	x0,x0,x19
	add	x0,x0,1
	cmp	x21,x0
	b.gt	.done
	mov	x22,CX
	sub	x22,x22,x19
	sub	x22,x22,1
.x:	mov	x0,CX
	add	x0,x0,x19
	add	x0,x0,1
	cmp	x22,x0
	b.gt	.ny
	mov	x0,CX
	sub	x0,x22,x0
	scvtf	s0,x0
	mov	x0,CY
	sub	x0,x21,x0
	scvtf	s1,x0
	fmul	s0,s0,s0
	fmadd	s0,s1,s1,s0
	fsqrt	s0,s0
	fsub	s0,s8,s0			; couverture = r + 0,5 - d
	mov	x0,x22
	mov	x1,x21
	mov	w2,w20
	bl	blend_px
	add	x22,x22,1
	b	.x
.ny:	add	x21,x21,1
	b	.y
.done:	ldp	d8,d9,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; s0,s1 = A ; s2,s3 = B ; s4 = demi-epaisseur ; w0 = couleur
; Couverture = demi-epaisseur + 0,5 - distance au segment (extremites rondes)
aa_segment:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	stp	d12,d13,[sp,-16]!
	stp	d14,d15,[sp,-16]!
	mov	w19,w0
	fmov	s8,s0				; Ax
	fmov	s9,s1				; Ay
	fsub	s10,s2,s0			; ABx
	fsub	s11,s3,s1			; ABy
	fmov	s12,0.5
	fadd	s12,s4,s12			; demi-epaisseur + 0,5
	fmul	s13,s10,s10
	fmadd	s13,s11,s11,s13			; |AB|^2
	fmov	s14,1.0
	fcmp	s13,s14
	b.ge	.lok
	fmov	s13,1.0
.lok:	; boite englobante
	fminnm	s0,s8,s2
	fmaxnm	s5,s8,s2
	fminnm	s1,s9,s3
	fmaxnm	s6,s9,s3
	fsub	s0,s0,s12
	fadd	s5,s5,s12
	fsub	s1,s1,s12
	fadd	s6,s6,s12
	fcvtms	x20,s0				; x min
	fcvtps	x21,s5				; x max
	fcvtms	x22,s1				; y min
	fcvtps	x23,s6				; y max
.y:	cmp	x22,x23
	b.gt	.done
	mov	x24,x20
.x:	cmp	x24,x21
	b.gt	.ny
	scvtf	s0,x24
	scvtf	s1,x22
	fsub	s0,s0,s8			; AP
	fsub	s1,s1,s9
	fmul	s2,s0,s10			; t = AP.AB / |AB|^2, borne a [0,1]
	fmadd	s2,s1,s11,s2
	fdiv	s2,s2,s13
	fmov	s3,wzr
	fmaxnm	s2,s2,s3
	fminnm	s2,s2,s14
	fmsub	s0,s2,s10,s0			; AP - t*AB
	fmsub	s1,s2,s11,s1
	fmul	s0,s0,s0
	fmadd	s0,s1,s1,s0
	fsqrt	s0,s0
	fsub	s0,s12,s0
	mov	x0,x24
	mov	x1,x22
	mov	w2,w19
	bl	blend_px
	add	x24,x24,1
	b	.x
.ny:	add	x22,x22,1
	b	.y
.done:	ldp	d14,d15,[sp],16
	ldp	d12,d13,[sp],16
	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, w2 = couleur, s0 = couverture (bornee a [0,1] ici)
; Melange avec la surface. N'utilise que s0, s1, s2, s3 et x0-x7.
blend_px:
	fmov	s1,wzr
	fcmp	s0,s1
	b.le	.ret
	adr	x3,ui_w
	ldp	x4,x5,[x3]
	cmp	x0,x4
	b.hs	.ret
	cmp	x1,x5
	b.hs	.ret
	adr	x3,ui_surf
	ldr	x3,[x3]
	lsl	x4,x4,2
	madd	x3,x1,x4,x3
	add	x3,x3,x0,lsl 2
	fmov	s1,1.0
	fcmp	s0,s1
	b.lt	.mix
	str	w2,[x3]
	ret
.mix:	mov	w4,256				; alpha sur 0..256
	scvtf	s1,w4
	fmul	s0,s0,s1
	fcvtzs	w4,s0
	ldr	w5,[x3]
	mov	w6,0
	mov	w7,16
.ch:	lsrv	w0,w2,w7			; fg
	and	w0,w0,0xFF
	lsrv	w1,w5,w7			; bg
	and	w1,w1,0xFF
	sub	w0,w0,w1
	mul	w0,w0,w4
	asr	w0,w0,8
	add	w0,w0,w1
	lslv	w0,w0,w7
	orr	w6,w6,w0
	subs	w7,w7,8
	b.pl	.ch
	str	w6,[x3]
.ret:	ret

; s0 = angle (radians, 0..2pi) -> s0 = sin, s1 = cos (series, |erreur| < 3e-4)
sincos:
	adr	x0,k_pi
	ldr	s1,[x0]
	fsub	s0,s0,s1			; x dans [-pi, pi] : sin(a) = -sin(x)
	fmul	s2,s0,s0			; x^2
	; sin x = x(1 - x2/6(1 - x2/20(1 - x2/42(1 - x2/72(1 - x2/110)))))
	adr	x0,k_sin
	ldr	s3,[x0,16]			; 1/110
	fmov	s4,1.0
	fmul	s5,s2,s3
	fsub	s5,s4,s5
	ldr	s3,[x0,12]			; 1/72
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0,8]			; 1/42
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0,4]			; 1/20
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0]				; 1/6
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	fmul	s6,s5,s0			; sin x
	; cos x = 1 - x2/2(1 - x2/12(1 - x2/30(1 - x2/56(1 - x2/90(1 - x2/132)))))
	adr	x0,k_cos
	ldr	s3,[x0,20]
	fmul	s5,s2,s3
	fsub	s5,s4,s5
	ldr	s3,[x0,16]
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0,12]
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0,8]
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0,4]
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0]
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5			; cos x
	fneg	s0,s6
	fneg	s1,s5
	ret

; --- configuration -------------------------------------------------------------
load_cfg:
	prologue
	str	x19,[sp,-16]!
	adr	x0,p_cfg
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,cfgbuf
	mov	x2,31
	sys	SYS_READ
	mov	x2,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x2,0
	b.le	.out
	adr	x1,cfgbuf
	mov	x3,0
	mov	x4,0
.d:	cmp	x4,x2
	b.hs	.st
	ldrb	w5,[x1,x4]
	sub	w5,w5,'0'
	cmp	w5,9
	b.hi	.st
	mov	x6,10
	madd	x3,x3,x6,x5
	add	x4,x4,1
	b	.d
.st:	movl	x6,86400
	udiv	x5,x3,x6
	msub	x3,x5,x6,x3
	adr	x0,offset
	str	x3,[x0]
.out:	ldr	x19,[sp],16
	epilogue

save_cfg:
	prologue
	str	x19,[sp,-16]!
	adr	x0,offset
	ldr	x0,[x0]
	adr	x1,cfgbuf
	bl	fmt_dec
	mov	w1,10
	strb	w1,[x0],1
	adr	x1,cfgbuf
	sub	x19,x0,x1
	adr	x0,p_cfg
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x2,x19
	mov	x19,x0
	adr	x1,cfgbuf
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
.out:	ldr	x19,[sp],16
	epilogue


segment readable writeable
kit_data

title	db 'Horloge',0
p_cfg	db 'SD0:/etc/clock.cfg',0
s_12	db '12',0
s_3	db '3',0
s_6	db '6',0
s_9	db '9',0
s_ph	db '+1 h',0
s_pm	db '+1 min',0
s_mm	db 0xE2,0x88,0x92,'1 min',0
s_note	db 'Pas d',0x27,'horloge mat',0xC3,0xA9,'rielle : r',0xC3,0xA9,'glez l',0x27,'heure avec les boutons.',0
	align 8
labels	dd s_ph, s_pm, s_mm
adjust	dd 3600, 60, -60
offset	dd 0
	align 4
k_pi	dw 0x40490FDB			; pi
k_two_pi dw 0x40C90FDB			; 2 pi
k_six_deg dw 0x3DD67750			; 6 degres en radians
k_sin	dw 0x3E2AAAAB, 0x3D4CCCCD, 0x3CC30C31, 0x3C638E39, 0x3C14F209	; 1/6 1/20 1/42 1/72 1/110
k_cos	dw 0x3F000000, 0x3DAAAAAB, 0x3D088889, 0x3C924925, 0x3C360B61, 0x3BF83E10	; 1/2 1/12 1/30 1/56 1/90 1/132
cfgbuf	rb 32
ui_data
