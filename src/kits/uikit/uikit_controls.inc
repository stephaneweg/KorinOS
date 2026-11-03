; ============================================================================
;  kits/uikit_controls.inc - UIKit : bouton (UT_BUTTON), etiquette (UT_LABEL),
;  texte coupe en lignes (ui_text_wrap).
;
;  Bouton (DESIGN-ELEGANT.md 11.1) : standard, principal (UB_DEFAULT : accent,
;  Entree d'un dialogue), fantome (UB_GHOST : barres d'outils) ; survol,
;  appui, grise. Clic, Espace ou Entree : UN_ACTIVATE. Largeur 0 : texte + 28
;  (au moins 80).
;  Etiquette : texte (UL_* de ui_label), coupe en lignes si ULB_WRAP.
; ============================================================================

; ---------------------------------------------------------------------------
;  Texte coupe en lignes
; ---------------------------------------------------------------------------

; x0 = texte (« \n » : nouvelle ligne), x1 = x, x2 = y (haut), x3 = largeur,
; w4 = couleur, x5 = police, x6 = taille px, x7 = drapeaux (bit 0 : mesurer
; seulement) -> x0 = hauteur occupee (lignes x 1,4 px). Les lignes se coupent
; aux espaces (un mot trop long : la ou il deborde).
ui_text_wrap:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,32
	str	x2,[sp,16]			; y de depart
	mov	x19,x0				; position
	mov	x20,x1
	mov	x21,x2				; y de la ligne
	mov	x22,x3
	str	w4,[sp]
	mov	x23,x5
	mov	x24,x6
	str	x7,[sp,8]
	mov	x0,x23
	mov	x1,x24
	bl	ttf_set_size
.line:	ldrb	w0,[x19]
	cbz	w0,.end
	mov	x25,0				; longueur jusqu'a la fin de ligne
.ll:	ldrb	w0,[x19,x25]
	cbz	w0,.lld
	cmp	w0,10
	b.eq	.lld
	add	x25,x25,1
	b	.ll
.lld:	mov	x0,x23				; ce qui tient
	mov	x1,x19
	mov	x2,x25
	mov	x3,x22
	mov	x4,1
	bl	ttf_text_pos
	mov	x26,x1				; octets montres
	mov	x27,x1				; octets consommes
	cmp	x26,x25
	b.hs	.nl				; toute la ligne tient
	mov	x2,x26				; sinon : dernier espace avant
.sp:	cbz	x2,.cut
	sub	x2,x2,1
	ldrb	w3,[x19,x2]
	cmp	w3,' '
	b.ne	.sp
	cbz	x2,.cut
	mov	x26,x2				; jusqu'a l'espace (exclu)
	add	x27,x2,1
	b	.draw
.cut:	cbnz	x26,.draw			; un mot trop long : coupe la ; au
	mov	x0,x19				; moins un point de code
	mov	x1,0
	mov	x2,x25
	bl	uk_u8_next
	mov	x26,x0
	mov	x27,x0
	b	.draw
.nl:	mov	x27,x25				; (et le saut de ligne)
	ldrb	w0,[x19,x25]
	cbz	w0,.draw
	add	x27,x27,1
.draw:	ldr	x0,[sp,8]
	tbnz	x0,0,.next
	mov	x2,x26
	cmp	x2,400
	mov	x3,400
	csel	x2,x3,x2,hi
	adr	x0,uk_fitbuf
	mov	x1,x19
	bl	uk_memcpy
	strb	wzr,[x0]
	adr	x0,uk_fitbuf
	mov	x1,x20
	mov	x2,x24				; ligne de base : y + px
	add	x2,x21,x2
	ldr	w3,[sp]
	mov	x4,x24
	mov	x5,x23
	mov	x6,0
	bl	ui_text
.next:	add	x19,x19,x27
	mov	x0,7				; interligne 1,4
	mul	x0,x0,x24
	mov	x1,5
	udiv	x0,x0,x1
	add	x21,x21,x0
	b	.line
.end:	ldr	x1,[sp,16]
	sub	x0,x21,x1
	add	sp,sp,32
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Bouton
; ---------------------------------------------------------------------------

; x0 = x, x1 = y, x2 = largeur (0 : selon le texte), x3 = hauteur (0 : celle
; d'un controle), x4 = texte, x5 = style (UB_*) -> x0 = poignee
ui_pushbutton:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x0,x4
	bl	ui_tr
	mov	x23,x0
	mov	x24,x5
	bl	uk_met
	cbnz	x22,.h
	ldr	x22,[x0,MT_CTL]
.h:	cbnz	x21,.w
	ldr	x1,[x0,MT_PX]
	mov	x0,x23
	str	x1,[sp,-16]!
	bl	uk_font_r
	mov	x2,x0
	ldr	x1,[sp],16
	mov	x0,x23
	bl	ui_text_width
	add	x21,x0,28
	cmp	x21,80
	mov	x0,80
	csel	x21,x0,x21,lo
.w:	mov	x0,UT_BUTTON
	mov	x1,x19
	mov	x2,x20
	mov	x3,x21
	mov	x4,x22
	bl	uk_w_new
	cbz	x0,.out
	mov	x19,x0
	str	x24,[x19,UW_STYLE]
	mov	x1,x23
	mov	x2,-1
	bl	uk_set_text
	mov	x0,x19
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

uk_bt_draw:
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
	ldr	x24,[x19,UW_STYLE]
	ldr	w25,[x19,UW_FLAGS]
	mov	x0,x19
	bl	uk_w_enabled
	cbnz	x0,.en
	orr	w25,w25,UWF_DISABLED
.en:	mov	x0,x20				; fond (FACE : sous les coins)
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_FACE
	bl	ui_fill
	jet	w26,TJ_TEXTE1			; couleur du texte
	tbnz	x24,1,.ghost
	tbnz	x24,0,.main
	; --- standard : contour LIGNE_FORTE, interieur ELEVE uni
	jet	w5,TJ_LIGNE_FORTE
	tbz	w25,1,.sc
	jet	w5,TJ_LIGNE
.sc:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	mov	x4,8
	bl	ui_rbox
	tbnz	w25,1,.sdis
	tbnz	w25,3,.sprs
	jet	w5,TJ_ELEVE
	mov	w6,w5
	tbz	w25,2,.sbox			; survol : CREUX mele a ELEVE
	jet	w0,TJ_ELEVE
	jet	w1,TJ_CREUX
	mov	x2,160
	bl	ui_mix
	mov	w5,w0
	mov	w6,w0
	b	.sbox
.sprs:	jet	w5,TJ_CREUX			; appui : CREUX
	mov	w6,w5
	b	.sbox
.sdis:	jet	w5,TJ_FACE
	mov	w6,w5
	jet	w26,TJ_TEXTE3
.sbox:	add	x0,x20,1
	add	x1,x21,1
	sub	x2,x22,2
	sub	x3,x23,2
	mov	x4,7
	bl	ui_rbox
	b	.text
	; --- principal : ACCENT uni, texte blanc ; survol et appui ACCENT_FONCE
.main:	jet	w26,TJ_ACCENT_INK
	tbnz	w25,1,.mdis
	jet	w5,TJ_ACCENT
	and	w0,w25,12			; survol (bit 2), appui (bit 3)
	cbz	w0,.mbox
	jet	w5,TJ_ACCENT_FONCE
	b	.mbox
.mdis:	jet	w5,TJ_CREUX
	jet	w26,TJ_TEXTE3
.mbox:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	mov	x4,8
	bl	ui_rbox
	b	.text
	; --- fantome : rien au repos ; survol CREUX, appui ton(CREUX, 116)
.ghost:	tbnz	w25,1,.gdis
	tbnz	w25,3,.gprs
	tbz	w25,2,.text
	jet	w5,TJ_CREUX
	b	.gbox
.gprs:	jet	w0,TJ_CREUX
	mov	x1,116
	bl	ui_tone
	mov	w5,w0
.gbox:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	mov	x4,8
	bl	ui_rbox
	b	.text
.gdis:	jet	w26,TJ_TEXTE3
.text:	bl	uk_met				; texte centre
	ldr	x24,[x0,MT_PX]
	ldr	x0,[x19,UW_TEXT]
	mov	x1,x24
	str	x0,[sp,-16]!
	bl	uk_font_r
	mov	x2,x0
	ldr	x0,[sp],16
	bl	ui_text_width
	sub	x1,x22,16
	cmp	x0,x1
	csel	x0,x1,x0,gt
	sub	x1,x22,x0
	asr	x1,x1,1
	add	x1,x20,x1
	str	x1,[sp,-16]!
	bl	uk_font_r
	mov	x5,x0
	ldr	x1,[sp],16
	ldr	x0,[x19,UW_TEXT]
	mov	x2,x21
	mov	x3,x23
	mov	w4,w26
	sub	x6,x22,16
	mov	x7,x24
	bl	uk_text_box
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris d'un bouton : survol, appui, relache dedans -> UN_ACTIVATE
uk_bt_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	w20,[x19,UW_FLAGS]
	cmp	x1,3
	b.eq	.leave
	cmp	x1,1
	b.eq	.press
	cmp	x1,2
	b.eq	.rel
	ldr	w0,[x19,UW_W]			; deplacement : dedans ?
	ldr	w4,[x19,UW_H]
	mov	x5,0
	cmp	x2,0
	b.lt	.in
	cmp	x2,x0
	b.ge	.in
	cmp	x3,0
	b.lt	.in
	cmp	x3,x4
	b.ge	.in
	mov	x5,1
.in:	orr	w0,w20,UWF_HOVER
	and	w1,w20,not (UWF_HOVER or UWF_PRESSED)
	adr	x2,uk_capture			; (bouton tenu : appuye s'il est dedans)
	ldr	x2,[x2]
	cmp	x2,x19
	b.ne	.hov
	orr	w0,w0,UWF_PRESSED
.hov:	cmp	x5,0
	csel	w0,w1,w0,eq
	b	.set
.leave:	and	w0,w20,not (UWF_HOVER or UWF_PRESSED)
	b	.set
.press:	orr	w0,w20,UWF_PRESSED or UWF_HOVER
	b	.set
.rel:	and	w0,w20,not UWF_PRESSED
	str	w0,[x19,UW_FLAGS]
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,0
	tbz	w20,3,.out			; relache sur le bouton appuye
	mov	x0,UN_ACTIVATE
	b	.out
.set:	str	w0,[x19,UW_FLAGS]
	cmp	w0,w20
	mov	x0,0
	b.eq	.out
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; clavier : Espace, Entree -> UN_ACTIVATE
uk_bt_key:
	mov	x0,UN_ACTIVATE
	cmp	w1,KEY_ENTER
	b.eq	.ret
	cmp	w2,' '
	b.eq	.ret
	mov	x0,-1
.ret:	ret

; ---------------------------------------------------------------------------
;  Etiquette
; ---------------------------------------------------------------------------

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, x4 = texte, x5 = drapeaux
; (ULB_*) -> x0 = poignee
ui_label:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x5
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	mov	x0,x4
	bl	ui_tr
	mov	x19,x0
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_LABEL
	bl	uk_w_new
	cbz	x0,.out
	mov	x21,x0
	str	x20,[x21,UW_STYLE]
	mov	x1,x19
	mov	x2,-1
	bl	uk_set_text
	mov	x0,x21
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

uk_lb_draw:
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
	ldr	x24,[x19,UW_STYLE]
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_FACE
	bl	ui_fill
	bl	uk_met				; taille, police, couleur
	ldr	x25,[x0,MT_PX]
	tbz	x24,5,.sz			; ULB_TITLE : 15 px gras
	mov	x25,15
.sz:	jet	w26,TJ_TEXTE1
	tbz	x24,1,.c1			; ULB_DIM
	jet	w26,TJ_TEXTE2
.c1:	mov	x0,x19
	bl	uk_w_enabled
	cbnz	x0,.c2
	jet	w26,TJ_TEXTE3
.c2:	bl	uk_font_r
	mov	x9,1 or 32			; ULB_BOLD, ULB_TITLE
	tst	x24,x9
	b.eq	.f
	bl	uk_bold
.f:	mov	x5,x0
	tbz	x24,2,.one			; ULB_WRAP : lignes
	ldr	x0,[x19,UW_TEXT]
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	mov	w4,w26
	mov	x6,x25
	mov	x7,0
	bl	ui_text_wrap
	b	.out
.one:	str	x5,[sp,-16]!			; une ligne : a gauche, a droite, au centre
	mov	x1,x20
	mov	x9,8 or 16
	tst	x24,x9
	b.eq	.left
	ldr	x0,[x19,UW_TEXT]
	mov	x1,x25
	ldr	x2,[sp]
	bl	ui_text_width
	cmp	x0,x22
	csel	x0,x22,x0,gt
	sub	x1,x22,x0
	tbnz	x24,3,.right
	asr	x1,x1,1
.right:	add	x1,x20,x1
.left:	ldr	x5,[sp],16
	ldr	x0,[x19,UW_TEXT]
	mov	x2,x21
	mov	x3,x23
	mov	w4,w26
	mov	x6,x22
	mov	x7,x25
	bl	uk_text_box
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
