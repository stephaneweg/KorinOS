; ============================================================================
;  inc/sounddraw.inc - petits dessins du son (UIKit) : icone haut-parleur,
;  curseur de volume, case a cocher, vumetre. Pour la barre de menus, son
;  volet (soundmenu), le Mixer (docs/SON.md 4.4).
;
;	segment executable (apres kit_use uikit) : include '../../inc/sounddraw.inc'
;  Les couleurs viennent des jetons du style plat (ui_jetons, TJ_*).
; ============================================================================

SD_SLIDER_H	= 16			; hauteur de la zone d'un curseur

; x0, x1 = coin (16 x 14), x2 = 1 : muet (barre), w3 = encre, w4 = encre
; attenuee : haut-parleur et ondes
sd_speaker:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	mov	w23,w4
	add	x0,x19,1			; corps
	add	x1,x20,5
	mov	x2,3
	mov	x3,5
	mov	w4,w22
	bl	ui_fill
	mov	x24,0				; pavillon : de 5 a 13 de haut
.p:	add	x0,x19,4
	add	x0,x0,x24
	sub	x1,x20,x24
	add	x1,x1,5
	lsl	x3,x24,1
	add	x3,x3,5
	mov	x2,1
	mov	w4,w22
	bl	ui_fill
	add	x24,x24,1
	cmp	x24,4
	b.ls	.p
	cbnz	x21,.mute
	add	x0,x19,11			; ondes
	add	x1,x20,5
	mov	x2,1
	mov	x3,5
	mov	w4,w22
	bl	ui_fill
	add	x0,x19,14
	add	x1,x20,2
	mov	x2,1
	mov	x3,11
	mov	w4,w22
	bl	ui_fill
	add	x0,x19,12
	add	x1,x20,4
	mov	x2,1
	mov	x3,1
	mov	w4,w22
	bl	ui_fill
	add	x0,x19,12
	add	x1,x20,10
	mov	x2,1
	mov	x3,1
	mov	w4,w22
	bl	ui_fill
	add	x0,x19,13
	add	x1,x20,1
	mov	x2,1
	mov	x3,1
	mov	w4,w22
	bl	ui_fill
	add	x0,x19,13
	add	x1,x20,13
	mov	x2,1
	mov	x3,1
	mov	w4,w22
	bl	ui_fill
	b	.out
.mute:	adr	x0,sd_s_cross			; muet : une croix
	add	x1,x19,10
	add	x2,x20,12
	mov	w3,w22
	mov	x4,11
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = coin, x2 = largeur, x3 = valeur 0..100, x4 = 1 : grise (muet) :
; piste, part remplie, bouton
sd_slider:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x3,100
	mov	x5,100
	csel	x22,x3,x5,ls
	mov	x24,x4
	bl	ui_jetons
	mov	x23,x0
	ldr	w5,[x23,TJ_CREUX]		; piste
	mov	w6,w5
	mov	x0,x19
	add	x1,x20,6
	mov	x2,x21
	mov	x3,4
	mov	x4,2
	bl	ui_rbox
	sub	x0,x21,12			; position du bouton
	mul	x0,x0,x22
	mov	x1,100
	udiv	x22,x0,x1
	ldr	w5,[x23,TJ_ACCENT]		; part remplie
	cbz	x24,.f
	ldr	w5,[x23,TJ_TEXTE3]
.f:	mov	w6,w5
	mov	x0,x19
	add	x1,x20,6
	add	x2,x22,6
	mov	x3,4
	mov	x4,2
	bl	ui_rbox
	ldr	w5,[x23,TJ_LIGNE_FORTE]		; bouton : contour, puis clair
	mov	w6,w5
	add	x0,x19,x22
	mov	x1,x20
	mov	x2,16
	mov	x3,16
	mov	x4,8
	bl	ui_rbox
	ldr	w5,[x23,TJ_ELEVE]
	ldr	w6,[x23,TJ_ELEVE_BAS]
	add	x0,x19,x22
	add	x0,x0,1
	add	x1,x20,1
	mov	x2,14
	mov	x3,14
	mov	x4,7
	bl	ui_rbox
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = abscisse du clic, x1 = debut du curseur, x2 = largeur -> x0 = 0..100
sd_slider_value:
	sub	x0,x0,x1
	sub	x0,x0,8
	sub	x2,x2,12
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x0,x2
	csel	x0,x2,x0,gt
	mov	x3,100
	mul	x0,x0,x3
	add	x0,x0,x2,lsr 1
	udiv	x0,x0,x2
	ret

; x0, x1 = coin (14 x 14), x2 = 1 : cochee
sd_check:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	ui_jetons
	mov	x22,x0
	ldr	w5,[x22,TJ_LIGNE_FORTE]
	cbz	x21,.c
	ldr	w5,[x22,TJ_ACCENT]
.c:	mov	w6,w5
	mov	x0,x19
	mov	x1,x20
	mov	x2,14
	mov	x3,14
	mov	x4,3
	bl	ui_rbox
	cbnz	x21,.v
	ldr	w5,[x22,TJ_FACE]
	mov	w6,w5
	add	x0,x19,1
	add	x1,x20,1
	mov	x2,12
	mov	x3,12
	mov	x4,2
	bl	ui_rbox
	b	.out
.v:	adr	x0,sd_s_check
	add	x1,x19,2
	add	x2,x20,12
	mov	w3,0xFFFFFF
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = coin, x2 = largeur, x3 = hauteur, x4 = crete (0..32767)
sd_meter:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	bl	ui_jetons
	mov	x24,x0
	ldr	w4,[x24,TJ_CREUX]
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	bl	ui_fill
	mov	x0,x21				; part allumee
	mul	x0,x0,x23
	lsr	x2,x0,15
	cbz	x2,.out
	ldr	w4,[x24,TJ_SUCCES]
	movl	x0,29000			; pres de la saturation : alerte
	cmp	x23,x0
	b.lo	.d
	ldr	w4,[x24,TJ_ALERTE]
.d:	mov	x0,x19
	mov	x1,x20
	mov	x3,x22
	bl	ui_fill
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

sd_s_cross	db 0xC3,0x97,0			; U+00D7
sd_s_check	db 0xE2,0x9C,0x93,0		; U+2713
		align 4
