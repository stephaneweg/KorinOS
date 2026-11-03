; ============================================================================
;  kits/uikit_value.inc - UIKit : les widgets d'une valeur bornee
;
;	UT_SLIDER	glissiere (DESIGN 11.6 : rail 4 px, partie remplie ACCENT,
;			bouton disque 18 blanc ; verticale si plus haute que large)
;	UT_PROGRESS	barre de progression (rail CREUX, remplie ACCENT)
;	UT_NUMERIC	nombre +/- (Onyx numeric.h) : champ et deux boutons
;	UT_KNOB		molette (Onyx knob.cpp) : disque, repere sur 270 degres
;	UT_VUMETER	vu-metre (Onyx vumeter.cpp) : segments SUCCES, ALERTE,
;			DANGER, crete gardee
;	UT_LCD		afficheur a sept segments (Onyx lcd.cpp) : chiffres, - : .
;
;  Plage : ui_w_set_range(widget, min, max, 0, pas) ; valeur : ui_w_set_value
;  / ui_w_get_value. Glissiere, nombre, molette : souris, molette, fleches,
;  Page (10 pas), Debut, Fin -> UN_CHANGE.
; ============================================================================

; x0..x3 = rectangle, x4 = min, x5 = max, x6 = valeur, x7 = type -> widget
uk_new_ranged:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x4
	mov	x20,x5
	mov	x21,x6
	stp	x0,x1,[sp,-16]!			; hauteur 0 : celle d'un controle
	stp	x2,x3,[sp,-16]!
	str	x7,[sp,-16]!
	bl	uk_met
	ldr	x4,[x0,MT_CTL]
	ldr	x7,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	cbz	x3,.h
	mov	x4,x3
.h:	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,x7
	bl	uk_w_new
	cbz	x0,.out
	stp	x19,x20,[x0,UW_MIN]
	str	x21,[x0,UW_VALUE]
	str	x0,[sp,-16]!
	bl	uk_w_clamp
	ldr	x0,[sp],16
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0..x3 = rectangle, x4 = min, x5 = max, x6 = valeur -> poignee
ui_slider:
	mov	x7,UT_SLIDER
	b	uk_new_ranged
; x0..x3, x4 = max, x5 = valeur (min 0)
ui_progress:
	mov	x6,x5
	mov	x5,x4
	mov	x4,0
	mov	x7,UT_PROGRESS
	b	uk_new_ranged
; x0..x3, x4 = min, x5 = max, x6 = valeur
ui_numeric:
	mov	x7,UT_NUMERIC
	b	uk_new_ranged
; x0..x3, x4 = min, x5 = max, x6 = valeur
ui_knob:
	mov	x7,UT_KNOB
	b	uk_new_ranged
; x0..x3, x4 = max (valeur 0)
ui_vumeter:
	mov	x5,x4
	mov	x4,0
	mov	x6,0
	mov	x7,UT_VUMETER
	b	uk_new_ranged
; x0..x3, x4 = texte (chiffres, « - », « : », « . », espace)
ui_lcd:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_LCD
	bl	uk_w_new
	cbz	x0,.out
	mov	x1,x19
	mov	x2,-1
	str	x0,[sp,-16]!
	bl	uk_set_text
	ldr	x0,[sp],16
.out:	ldr	x19,[sp],16
	epilogue

; x0 = widget -> x0 = 1 s'il est vertical (plus haut que large)
uk_vertical:
	ldp	w1,w2,[x0,UW_W]
	cmp	w2,w1
	cset	x0,gt
	ret

; x0 = widget, x1 = nouvelle valeur : bornee, redessine -> x0 = UN_CHANGE si
; elle a change (0 sinon)
uk_val_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,UW_VALUE]
	str	x1,[x19,UW_VALUE]
	bl	uk_w_clamp
	ldr	x0,[x19,UW_VALUE]
	cmp	x0,x20
	mov	x0,0
	b.eq	.out
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
.out:	ldp	x19,x20,[sp],16
	epilogue

; clavier commun : fleches (gauche, bas : -pas ; droite, haut : +pas), Page
; (10 pas), Debut, Fin
uk_val_key:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x2,[x19,UW_STEP]
	ldr	x3,[x19,UW_VALUE]
	cmp	w1,KEY_RIGHT
	b.eq	.p
	cmp	w1,KEY_UP
	b.eq	.p
	cmp	w1,KEY_LEFT
	b.eq	.m
	cmp	w1,KEY_DOWN
	b.eq	.m
	mov	x4,10
	mul	x4,x2,x4
	cmp	w1,KEY_PGUP
	b.eq	.pp
	cmp	w1,KEY_PGDN
	b.eq	.mp
	cmp	w1,KEY_HOME
	b.eq	.home
	cmp	w1,KEY_END
	b.eq	.end
	mov	x0,-1
	b	.out
.p:	add	x1,x3,x2
	b	.set
.m:	sub	x1,x3,x2
	b	.set
.pp:	add	x1,x3,x4
	b	.set
.mp:	sub	x1,x3,x4
	b	.set
.home:	ldr	x1,[x19,UW_MIN]
	b	.set
.end:	ldr	x1,[x19,UW_MAX]
.set:	mov	x0,x19
	bl	uk_val_set
.out:	ldp	x19,x20,[sp],16
	epilogue

; molette commune : un pas par cran
uk_val_wheel:
	ldr	x2,[x0,UW_STEP]
	mul	x1,x1,x2
	ldr	x2,[x0,UW_VALUE]
	add	x1,x2,x1
	b	uk_val_set

; ---------------------------------------------------------------------------
;  Glissiere
; ---------------------------------------------------------------------------

; x0 = glissiere -> x0 = position du bouton le long du rail (0..course),
; x1 = course (longueur - 18)
uk_sl_pos:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	uk_vertical
	ldr	w1,[x19,UW_W]
	cbz	x0,.h
	ldr	w1,[x19,UW_H]
.h:	sub	x1,x1,UK_BOX
	cmp	x1,1
	mov	x2,1
	csel	x1,x2,x1,lt
	ldp	x2,x3,[x19,UW_MIN]
	sub	x3,x3,x2
	ldr	x4,[x19,UW_VALUE]
	sub	x4,x4,x2
	mov	x0,0
	cmp	x3,0
	b.le	.out
	mul	x0,x4,x1
	udiv	x0,x0,x3
.out:	ldr	x19,[sp],16
	epilogue

uk_sl_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_FACE
	bl	ui_fill
	mov	x0,x19
	bl	uk_vertical
	mov	x24,x0
	mov	x0,x19
	bl	uk_sl_pos
	mov	x25,x0				; position
	mov	x26,x1				; course
	cbnz	x24,.v
	; --- horizontale : rail au milieu
	add	x0,x20,UK_BOX/2
	lsr	x1,x23,1
	add	x1,x21,x1
	sub	x1,x1,2
	mov	x2,x26
	mov	x3,4
	mov	x4,2
	jet	w5,TJ_LIGNE
	mov	w6,w5
	bl	ui_rbox
	add	x0,x20,UK_BOX/2
	lsr	x1,x23,1
	add	x1,x21,x1
	sub	x1,x1,2
	mov	x2,x25
	mov	x3,4
	mov	x4,2
	bl	uk_sl_accent
	mov	w6,w5
	bl	ui_rbox
	add	x0,x20,x25			; bouton
	lsr	x1,x23,1
	add	x1,x21,x1
	sub	x1,x1,UK_BOX/2
	b	.knob
.v:	; --- verticale : le minimum en bas
	lsr	x0,x22,1
	add	x0,x20,x0
	sub	x0,x0,2
	add	x1,x21,UK_BOX/2
	mov	x2,4
	mov	x3,x26
	mov	x4,2
	jet	w5,TJ_LIGNE
	mov	w6,w5
	bl	ui_rbox
	lsr	x0,x22,1
	add	x0,x20,x0
	sub	x0,x0,2
	add	x1,x21,UK_BOX/2
	add	x1,x1,x26
	sub	x1,x1,x25
	mov	x2,4
	mov	x3,x25
	mov	x4,2
	bl	uk_sl_accent
	mov	w6,w5
	bl	ui_rbox
	lsr	x0,x22,1
	add	x0,x20,x0
	sub	x0,x0,UK_BOX/2
	add	x1,x21,x26
	sub	x1,x1,x25
.knob:	stp	x0,x1,[sp,-16]!			; disque blanc, contour LIGNE_FORTE
	mov	x2,UK_BOX
	mov	x3,UK_BOX
	mov	x4,UK_BOX/2
	jet	w5,TJ_LIGNE_FORTE
	ldr	w6,[x19,UW_FLAGS]
	tbz	w6,3,.k1
	jet	w5,TJ_ACCENT
.k1:	mov	w6,w5
	bl	ui_rbox
	ldp	x0,x1,[sp],16
	add	x0,x0,1
	add	x1,x1,1
	mov	x2,UK_BOX-2
	mov	x3,UK_BOX-2
	mov	x4,UK_BOX/2-1
	movl	w5,0x00FFFFFF
	mov	w6,w5
	bl	ui_rbox
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> w5 = ACCENT, ou sa version grisee si le widget x19 est inactif
uk_sl_accent:
	prologue
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	str	x4,[sp,-16]!
	jet	w5,TJ_ACCENT
	mov	x0,x19
	bl	uk_w_enabled
	cbnz	x0,.out
	jet	w0,TJ_FACE
	jet	w1,TJ_ACCENT
	mov	x2,90
	bl	ui_mix
	mov	w5,w0
.out:	ldr	x4,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	epilogue

; souris : appui ou glisser -> la valeur sous le point
uk_sl_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	mov	x21,x3
	ldr	w0,[x19,UW_FLAGS]
	cmp	x1,1
	b.eq	.press
	cmp	x1,2
	b.eq	.rel
	cbnz	x1,.zero
	adr	x2,uk_capture
	ldr	x2,[x2]
	cmp	x2,x19
	b.ne	.zero
	b	.at
.press:	orr	w0,w0,UWF_PRESSED
	str	w0,[x19,UW_FLAGS]
.at:	mov	x0,x19
	bl	uk_vertical
	mov	x22,x0
	mov	x0,x19
	bl	uk_sl_pos
	mov	x2,x1				; course
	sub	x0,x20,UK_BOX/2
	cbz	x22,.h
	ldr	w0,[x19,UW_H]			; vertical : depuis le bas
	sub	x0,x0,x21
	sub	x0,x0,UK_BOX/2
.h:	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x0,x2
	csel	x0,x2,x0,gt
	ldp	x3,x4,[x19,UW_MIN]
	sub	x4,x4,x3
	mul	x0,x0,x4
	add	x0,x0,x2,lsr 1			; (arrondi)
	udiv	x0,x0,x2
	add	x1,x0,x3
	ldr	x2,[x19,UW_STEP]		; multiple du pas
	cmp	x2,1
	b.le	.set
	sub	x1,x1,x3
	add	x1,x1,x2,lsr 1
	udiv	x4,x1,x2
	mul	x1,x4,x2
	add	x1,x1,x3
.set:	mov	x0,x19
	bl	uk_val_set
	cbnz	x0,.out
	mov	x0,x19				; (appui sans changement : le bouton)
	bl	uk_w_refresh
	b	.zero
.rel:	and	w0,w0,not UWF_PRESSED
	str	w0,[x19,UW_FLAGS]
	mov	x0,x19
	bl	uk_w_refresh
.zero:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Barre de progression
; ---------------------------------------------------------------------------

uk_pg_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_FACE
	bl	ui_fill
	mov	x0,x20				; rail : CREUX, contour LIGNE
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	lsr	x4,x23,1
	jet	w5,TJ_LIGNE
	mov	w6,w5
	bl	ui_rbox
	add	x0,x20,1
	add	x1,x21,1
	sub	x2,x22,2
	sub	x3,x23,2
	lsr	x4,x3,1
	jet	w5,TJ_CREUX
	mov	w6,w5
	bl	ui_rbox
	ldp	x0,x1,[x19,UW_MIN]		; partie faite
	sub	x1,x1,x0
	ldr	x2,[x19,UW_VALUE]
	sub	x2,x2,x0
	cmp	x1,0
	b.le	.out
	mul	x2,x2,x22
	udiv	x2,x2,x1
	cbz	x2,.out
	cmp	x2,x23				; (au moins un disque)
	csel	x2,x23,x2,lo
	mov	x0,x20
	mov	x1,x21
	mov	x3,x23
	lsr	x4,x23,1
	jet	w5,TJ_ACCENT
	mov	w6,w5
	bl	ui_rbox
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Nombre +/-
; ---------------------------------------------------------------------------

uk_nm_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x19
	bl	uk_w_focused
	mov	x24,x0
	jet	w5,TJ_LIGNE_FORTE		; cadre d'un champ
	cbz	x24,.c
	jet	w5,TJ_ACCENT
.c:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	mov	x4,6
	bl	ui_rbox
	jet	w5,TJ_SURFACE
	mov	w6,w5
	add	x0,x20,1
	add	x1,x21,1
	sub	x2,x22,2
	sub	x3,x23,2
	mov	x4,5
	bl	ui_rbox
	add	x0,x20,x22			; deux boutons a droite : « − » « + »
	sub	x0,x0,x23,lsl 1
	mov	x1,x21
	mov	x2,1
	mov	x3,x23
	jet	w4,TJ_LIGNE
	bl	ui_fill
	add	x0,x20,x22
	sub	x0,x0,x23
	mov	x1,x21
	mov	x2,1
	mov	x3,x23
	jet	w4,TJ_LIGNE
	bl	ui_fill
	adr	x0,uk_s_minus
	add	x1,x20,x22
	sub	x1,x1,x23,lsl 1
	mov	x2,0
	bl	uk_nm_glyph
	adr	x0,uk_s_plus
	add	x1,x20,x22
	sub	x1,x1,x23
	mov	x2,1
	bl	uk_nm_glyph
	ldr	x0,[x19,UW_VALUE]		; la valeur
	adr	x1,uk_numbuf
	tbz	x0,63,.pos
	mov	w2,'-'
	strb	w2,[x1],1
	neg	x0,x0
.pos:	bl	fmt_dec
	strb	wzr,[x0]
	bl	uk_met
	ldr	x7,[x0,MT_PX]
	bl	uk_font_r
	mov	x5,x0
	adr	x0,uk_numbuf
	add	x1,x20,10
	mov	x2,x21
	mov	x3,x23
	jet	w4,TJ_TEXTE1
	sub	x6,x22,x23,lsl 1
	sub	x6,x6,12
	bl	uk_text_box
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (uk_nm_draw) x0 = glyphe, x1 = x du bouton carre (cote x23), x2 = 0 / 1
; (appuye si DRAG2 = x2 + 1)
uk_nm_glyph:
	prologue
	stp	x0,x1,[sp,-16]!
	ldr	x3,[x19,UW_DRAG2]
	sub	x3,x3,1
	cmp	x3,x2
	b.ne	.g
	add	x0,x1,1				; appuye : CREUX
	add	x1,x21,1
	sub	x2,x23,2
	sub	x3,x23,2
	jet	w4,TJ_CREUX
	bl	ui_fill
.g:	ldp	x0,x1,[sp],16
	add	x1,x1,7
	lsr	x2,x23,2
	add	x1,x1,x2
	mov	x2,x21
	mov	x3,x23
	jet	w4,TJ_TEXTE2
	str	x0,[sp,-16]!
	str	x1,[sp,8]
	bl	uk_bold
	mov	x5,x0
	ldr	x0,[sp]
	ldr	x1,[sp,8]
	add	sp,sp,16
	mov	x6,0
	mov	x7,15
	bl	uk_text_box
	epilogue

; souris : un bouton -> -/+ un pas ; molette, clavier : uk_val_*
uk_nm_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	x1,2
	b.eq	.rel
	cmp	x1,1
	b.ne	.zero
	ldp	w3,w4,[x19,UW_W]		; quel bouton ?
	sub	x5,x3,x4,lsl 1
	cmp	x2,x5
	b.lt	.zero
	sub	x5,x3,x4
	mov	x20,1				; -
	cmp	x2,x5
	b.lt	.b
	mov	x20,2				; +
.b:	str	x20,[x19,UW_DRAG2]
	ldr	x2,[x19,UW_STEP]
	cmp	x20,1
	cneg	x2,x2,eq
	ldr	x1,[x19,UW_VALUE]
	add	x1,x1,x2
	mov	x0,x19
	bl	uk_val_set
	str	x0,[sp,-16]!
	mov	x0,x19
	bl	uk_w_refresh
	ldr	x0,[sp],16
	b	.out
.rel:	str	xzr,[x19,UW_DRAG2]
	mov	x0,x19
	bl	uk_w_refresh
.zero:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; clavier : fleches, Page... ; chiffres : tapes a la suite ; Retour arriere
uk_nm_key:
	cmp	w2,'0'
	b.lo	.bs
	cmp	w2,'9'
	b.hi	uk_val_key
	sub	w2,w2,'0'			; valeur * 10 + chiffre (au-dela : le chiffre)
	ldr	x3,[x0,UW_VALUE]
	mov	x4,10
	madd	x1,x3,x4,x2
	ldr	x4,[x0,UW_MAX]
	cmp	x1,x4
	csel	x1,x2,x1,gt
	b	uk_val_set
.bs:	cmp	w1,KEY_BACKSPACE
	b.ne	uk_val_key
	ldr	x3,[x0,UW_VALUE]
	mov	x4,10
	sdiv	x1,x3,x4
	b	uk_val_set

; ---------------------------------------------------------------------------
;  Molette (bouton rond)
; ---------------------------------------------------------------------------

; cos et sin (x 1000) de -225 a +45 degres par pas de 10 (le repere va de
; 7 h 30 a 4 h 30 dans le sens des aiguilles d'une montre)
uk_knob_tab:
	dh -707,-707, -819,-574, -906,-423, -966,-259, -996,-87
	dh -996,87, -966,259, -906,423, -819,574, -707,707
	dh -574,819, -423,906, -259,966, -87,996, 87,996
	dh 259,966, 423,906, 574,819, 707,707, 819,574
	dh 906,423, 966,259, 996,87, 996,-87, 966,-259
	dh 906,-423, 819,-574, 707,-707
UK_KNOB_N	= 28

uk_kn_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_FACE
	bl	ui_fill
	cmp	x22,x23				; disque : le plus petit cote
	csel	x24,x22,x23,lo
	sub	x0,x22,x24
	add	x20,x20,x0,lsr 1
	sub	x0,x23,x24
	add	x21,x21,x0,lsr 1
	lsr	x25,x24,1			; rayon
	mov	x26,0				; graduations : points du cercle
.t:	cmp	x26,UK_KNOB_N
	b.hs	.disc
	mov	x0,x26
	sub	x1,x25,2
	bl	uk_kn_point
	sub	x0,x0,1
	sub	x1,x1,1
	mov	x2,2
	mov	x3,2
	jet	w4,TJ_LIGNE_FORTE
	bl	ui_fill
	add	x26,x26,3
	b	.t
.disc:	add	x0,x20,6			; disque ELEVE, contour LIGNE_FORTE
	add	x1,x21,6
	sub	x2,x24,12
	sub	x3,x24,12
	sub	x4,x25,6
	jet	w5,TJ_LIGNE_FORTE
	mov	w6,w5
	bl	ui_rbox
	add	x0,x20,7
	add	x1,x21,7
	sub	x2,x24,14
	sub	x3,x24,14
	sub	x4,x25,7
	jet	w5,TJ_ELEVE
	mov	w6,w5
	bl	ui_rbox
	ldp	x0,x1,[x19,UW_MIN]		; repere : ACCENT
	sub	x1,x1,x0
	ldr	x2,[x19,UW_VALUE]
	sub	x2,x2,x0
	mov	x0,0
	cmp	x1,0
	b.le	.mk
	mov	x3,UK_KNOB_N-1
	mul	x2,x2,x3
	udiv	x0,x2,x1
.mk:	mov	x26,x0
	mov	x24,4				; trait de 4 points vers le centre
.m:	cbz	x24,.out
	mov	x0,x26
	sub	x1,x25,12
	mov	x2,3
	msub	x1,x24,x2,x1
	bl	uk_kn_point
	sub	x0,x0,2
	sub	x1,x1,2
	mov	x2,4
	mov	x3,4
	mov	x4,2
	jet	w5,TJ_ACCENT
	mov	w6,w5
	bl	ui_rbox
	sub	x24,x24,1
	b	.m
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (uk_kn_draw) x0 = cran 0..27, x1 = rayon -> x0, x1 = point (centre :
; x20 + x25, x21 + x25)
uk_kn_point:
	adr	x2,uk_knob_tab
	add	x2,x2,x0,lsl 2
	ldrsh	x3,[x2]				; cos
	ldrsh	x4,[x2,2]			; sin (vers le bas : y croit)
	mul	x3,x3,x1
	mul	x4,x4,x1
	mov	x5,1000
	sdiv	x3,x3,x5
	sdiv	x4,x4,x5
	add	x0,x20,x25
	add	x0,x0,x3
	add	x1,x21,x25
	sub	x1,x1,x4
	ret

; souris : glisser vers le haut / la droite augmente (une course de 150 px)
uk_kn_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	x1,1
	b.ne	.mv
	sub	x2,x2,x3			; depart : x - y, valeur
	str	x2,[x19,UW_DRAG]
	ldr	x0,[x19,UW_VALUE]
	str	x0,[x19,UW_DRAG2]
	b	.zero
.mv:	cbnz	x1,.zero
	adr	x4,uk_capture
	ldr	x4,[x4]
	cmp	x4,x19
	b.ne	.zero
	sub	x2,x2,x3
	ldr	x4,[x19,UW_DRAG]
	sub	x2,x2,x4			; deplacement
	ldp	x4,x5,[x19,UW_MIN]
	sub	x5,x5,x4
	mul	x2,x2,x5
	mov	x5,150
	sdiv	x2,x2,x5
	ldr	x1,[x19,UW_DRAG2]
	add	x1,x1,x2
	mov	x0,x19
	bl	uk_val_set
	b	.out
.zero:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Vu-metre
; ---------------------------------------------------------------------------

UK_VU_SEG	= 6			; segment + 2 px d'espace

; x0 = vu-metre, x1 = niveau : la valeur et la crete (gardee jusqu'a ce
; qu'un niveau plus bas la fasse descendre d'un segment)
ui_vumeter_level:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x0,.out
	str	x1,[x19,UW_VALUE]
	bl	uk_w_clamp
	ldr	x1,[x19,UW_VALUE]
	ldr	x2,[x19,UW_AUX]			; crete
	cmp	x1,x2
	b.ge	.peak
	ldr	x3,[x19,UW_MAX]			; descend d'un trentieme
	mov	x4,30
	udiv	x3,x3,x4
	sub	x2,x2,x3
	cmp	x2,x1
	csel	x1,x1,x2,gt
.peak:	str	x1,[x19,UW_AUX]
	mov	x0,x19
	bl	uk_w_refresh
.out:	ldr	x19,[sp],16
	epilogue

uk_vu_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_CREUX
	bl	ui_fill
	mov	x0,x19
	bl	uk_vertical
	mov	x24,x0
	mov	x25,x22				; longueur
	cbz	x24,.l
	mov	x25,x23
.l:	mov	x0,UK_VU_SEG
	udiv	x26,x25,x0			; segments
	ldp	x0,x1,[x19,UW_MIN]
	sub	x1,x1,x0
	cmp	x1,1
	mov	x2,1
	csel	x1,x2,x1,lt
	ldr	x2,[x19,UW_VALUE]		; segments allumes
	sub	x2,x2,x0
	mul	x2,x2,x26
	udiv	x27,x2,x1
	ldr	x2,[x19,UW_AUX]			; segment de crete
	sub	x2,x2,x0
	mul	x2,x2,x26
	udiv	x28,x2,x1
	mov	x23,0				; (x23 : hauteur gardee ci-dessous)
	ldr	w23,[x19,UW_H]
	mov	x25,0				; segment
.s:	cmp	x25,x26
	b.hs	.out
	jet	w4,TJ_SUCCES			; vert, jaune aux 3/4, rouge au dernier
	lsl	x0,x25,2			; huitieme
	mov	x1,3
	mul	x1,x26,x1
	cmp	x0,x1
	b.lo	.col
	jet	w4,TJ_ALERTE
	mov	x1,7
	mul	x1,x26,x1
	lsl	x0,x25,3
	cmp	x0,x1
	b.lo	.col
	jet	w4,TJ_DANGER
.col:	cmp	x25,x27				; eteint : la couleur a peine visible
	b.lo	.lit
	sub	x0,x28,1
	cmp	x25,x0
	b.eq	.lit				; (crete)
	mov	w0,w4
	jet	w1,TJ_CREUX
	mov	x2,200
	bl	ui_mix
	mov	w4,w0
.lit:	mov	x0,UK_VU_SEG
	mul	x0,x25,x0
	cbnz	x24,.vs
	add	x0,x20,x0			; horizontal
	add	x1,x21,2
	mov	x2,UK_VU_SEG-2
	sub	x3,x23,4
	b	.f
.vs:	mov	x1,x0				; vertical : depuis le bas
	add	x1,x1,UK_VU_SEG
	add	x2,x21,x23
	sub	x1,x2,x1
	add	x1,x1,2
	add	x0,x20,2
	sub	x2,x22,4
	mov	x3,UK_VU_SEG-2
.f:	bl	ui_fill
	add	x25,x25,1
	b	.s
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Afficheur a sept segments
; ---------------------------------------------------------------------------

; segments : bit 0 haut, 1 haut droite, 2 bas droite, 3 bas, 4 bas gauche,
; 5 haut gauche, 6 milieu ; chiffres 0..9 puis « - »
uk_seg7:	db 0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F,0x40
		align 4

uk_lcd_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x20				; fond sombre (vert tres fonce), arrondi
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	mov	x4,6
	movl	w5,0x00101A14
	movl	w6,0x0008100C
	bl	ui_rbox
	sub	x24,x23,12			; hauteur d'un chiffre
	mov	x0,10
	udiv	x25,x24,x0			; epaisseur d'un segment
	cmp	x25,2
	mov	x0,2
	csel	x25,x0,x25,lo
	lsr	x26,x24,1			; largeur d'un chiffre
	add	x26,x26,x25
	add	x27,x20,8			; x du chiffre
	add	x21,x21,6
	ldr	x28,[x19,UW_TEXT]
.ch:	cbz	x28,.out
	ldrb	w0,[x28],1
	cbz	w0,.out
	cmp	w0,':'
	b.eq	.colon
	cmp	w0,'.'
	b.eq	.dot
	mov	w1,10				; « - »
	cmp	w0,'-'
	b.eq	.seg
	mov	w1,-1				; espace : rien
	sub	w2,w0,'0'
	cmp	w2,10
	b.hs	.adv
	mov	w1,w2
.seg:	mov	w2,0				; segments eteints a peine visibles,
	adr	x3,uk_seg7			; allumes en vert vif
	tbnz	w1,31,.adv
	ldrb	w2,[x3,x1]
	bl	uk_lcd_digit
.adv:	add	x27,x27,x26
	add	x27,x27,x25,lsl 1
	b	.ch
.colon:	add	x0,x27,x25			; deux points
	lsr	x1,x24,2
	add	x1,x21,x1
	mov	x2,x25
	mov	x3,x25
	movl	w4,0x0050FF7A
	bl	ui_fill
	add	x0,x27,x25
	lsr	x1,x24,2
	mov	x2,3
	mul	x1,x1,x2
	add	x1,x21,x1
	mov	x2,x25
	mov	x3,x25
	movl	w4,0x0050FF7A
	bl	ui_fill
	add	x27,x27,x25,lsl 2
	b	.ch
.dot:	sub	x0,x27,x25
	add	x1,x21,x24
	sub	x1,x1,x25
	mov	x2,x25
	mov	x3,x25
	movl	w4,0x0050FF7A
	bl	ui_fill
	b	.ch
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (uk_lcd_draw) w2 = segments allumes : un chiffre a x27, y x21 (hauteur
; x24, largeur x26, epaisseur x25)
uk_lcd_digit:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x2
	mov	x19,0				; segment
.s:	cmp	x19,7
	b.hs	.out
	movl	w4,0x0018301E			; eteint
	lsr	w0,w20,w19
	tbz	w0,0,.c
	movl	w4,0x0050FF7A			; allume
.c:	lsr	x5,x24,1			; moitie de hauteur
	cmp	x19,0
	b.ne	.s1
	add	x0,x27,x25			; haut
	mov	x1,x21
	sub	x2,x26,x25,lsl 1
	mov	x3,x25
	b	.f
.s1:	cmp	x19,1
	b.ne	.s2
	add	x0,x27,x26			; haut droite
	sub	x0,x0,x25
	add	x1,x21,x25
	mov	x2,x25
	sub	x3,x5,x25
	b	.f
.s2:	cmp	x19,2
	b.ne	.s3
	add	x0,x27,x26			; bas droite
	sub	x0,x0,x25
	add	x1,x21,x5
	mov	x2,x25
	sub	x3,x5,x25
	b	.f
.s3:	cmp	x19,3
	b.ne	.s4
	add	x0,x27,x25			; bas
	add	x1,x21,x24
	sub	x1,x1,x25
	sub	x2,x26,x25,lsl 1
	mov	x3,x25
	b	.f
.s4:	cmp	x19,4
	b.ne	.s5
	mov	x0,x27				; bas gauche
	add	x1,x21,x5
	mov	x2,x25
	sub	x3,x5,x25
	b	.f
.s5:	cmp	x19,5
	b.ne	.s6
	mov	x0,x27				; haut gauche
	add	x1,x21,x25
	mov	x2,x25
	sub	x3,x5,x25
	b	.f
.s6:	add	x0,x27,x25			; milieu
	add	x1,x21,x5
	lsr	x6,x25,1
	sub	x1,x1,x6
	sub	x2,x26,x25,lsl 1
	mov	x3,x25
.f:	bl	ui_fill
	add	x19,x19,1
	b	.s
.out:	ldp	x19,x20,[sp],16
	epilogue
