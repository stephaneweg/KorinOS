; ============================================================================
;  kits/uikit_choice.inc - UIKit : case a cocher (UT_CHECK), bouton radio
;  (UT_RADIO), interrupteur (UT_SWITCH), liste deroulante (UT_DROP), liste
;  deroulante modifiable (UT_COMBO). docs/DESIGN-ELEGANT.md 11.6.
;
;  Valeur : 0 / 1 (case, radio, interrupteur), element choisi (-1 : aucun)
;  pour les listes deroulantes. Un clic (relache sur le widget) ou Espace
;  change la valeur : UN_CHANGE. Radio : ceux du meme groupe (UW_AUX) dans la
;  fenetre se decochent. Elements des listes deroulantes : ui_list_add,
;  ui_list_clear, ui_list_count, ui_list_get (comme une liste), ou le texte
;  « un|deux|trois » de la creation ; le menu s'ouvre sous le widget
;  (ui_popup ; place de la fenetre : WS_OP_WIN_POS).
; ============================================================================

UK_BOX		= 18			; case, disque
UK_POP_MAX	= 4096			; libelles du menu d'une liste deroulante

; -> x0, x1 = coin du contenu de la fenetre courante a l'ecran (0, 0 si
; inconnu)
uk_win_pos:
	prologue
	bl	uk_win
	adr	x1,uk_msg
	str	x0,[x1,WM_A]
	mov	x0,WS_OP_WIN_POS
	bl	uk_call
	mov	x2,x0
	mov	x0,0
	mov	x1,0
	tbnz	x2,63,.out
	adr	x9,uk_msg
	ldp	x0,x1,[x9,WM_A]
.out:	epilogue

; x0 = type, x1..x4 = rectangle (largeur 0 : texte + x6, hauteur 0 : un
; controle), x5 = texte -> x0 = widget avec son texte
uk_new_sized:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x24,x5
	mov	x25,x6
	bl	uk_met
	mov	x26,x0
	cbnz	x23,.h
	ldr	x23,[x26,MT_CTL]
.h:	cbnz	x22,.w
	mov	x22,x25
	cbz	x24,.w
	bl	uk_font_r
	mov	x2,x0
	mov	x0,x24
	ldr	x1,[x26,MT_PX]
	bl	ui_text_width
	add	x22,x0,x25
.w:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	mov	x4,x23
	bl	uk_w_new
	cbz	x0,.out
	mov	x19,x0
	mov	x1,x24
	mov	x2,-1
	bl	uk_set_text
	mov	x0,x19
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0..x3 = rectangle (0 : auto), x4 = libelle, x5 = 1 cochee -> poignee
ui_checkbox:
	mov	x6,UT_CHECK
	b	uk_new_toggle
; x0..x3, x4 = libelle, x5 = groupe (les radios d'un groupe s'excluent)
ui_radio:
	mov	x6,UT_RADIO
	b	uk_new_toggle
; x0..x3, x4 = libelle, x5 = 1 allume
ui_switch:
	mov	x6,UT_SWITCH

; x0..x3, x4 = libelle, x5 = valeur (radio : groupe), x6 = type
uk_new_toggle:
	prologue
	bl	tr_x4
	stp	x19,x20,[sp,-16]!
	mov	x19,x5
	mov	x20,x6
	mov	x5,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,x20
	mov	x6,UK_BOX+10
	cmp	x20,UT_SWITCH
	b.ne	.n
	mov	x6,46
.n:	bl	uk_new_sized
	cbz	x0,.out
	cmp	x20,UT_RADIO
	b.eq	.grp
	cmp	x19,0
	cset	x1,ne
	str	x1,[x0,UW_VALUE]
	b	.out
.grp:	str	x19,[x0,UW_AUX]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget : texte (a x + decalage) centre verticalement, TEXTE1 / TEXTE3
; x1 = decalage
uk_tg_text:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	uk_w_enabled
	jet	w21,TJ_TEXTE1
	cbnz	x0,.c
	jet	w21,TJ_TEXTE3
.c:	bl	uk_met
	ldr	x22,[x0,MT_PX]
	bl	uk_font_r
	mov	x5,x0
	ldr	x0,[x19,UW_TEXT]
	ldp	w1,w2,[x19,UW_X]
	sxtw	x1,w1
	sxtw	x2,w2
	add	x1,x1,x20
	ldp	w3,w6,[x19,UW_W]
	sub	x6,x3,x20
	ldr	w3,[x19,UW_H]
	mov	w4,w21
	mov	x7,x22
	bl	uk_text_box
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget -> x0 = x, x1 = y du carre de UK_BOX (centre verticalement)
uk_tg_box:
	ldp	w1,w2,[x0,UW_X]
	sxtw	x1,w1
	sxtw	x2,w2
	ldr	w3,[x0,UW_H]
	sub	x3,x3,UK_BOX
	add	x2,x2,x3,asr 1
	mov	x0,x1
	mov	x1,x2
	ret

; dessin d'une case, d'un radio, d'un interrupteur
uk_tg_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldp	w0,w1,[x19,UW_X]		; fond
	ldp	w2,w3,[x19,UW_W]
	jet	w4,TJ_FACE
	bl	ui_fill
	mov	x0,x19
	bl	uk_w_enabled
	mov	x23,x0
	ldr	x24,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_tg_box
	mov	x20,x0
	mov	x21,x1
	ldr	w22,[x19,UW_TYPE]
	cmp	w22,UT_SWITCH
	b.eq	.switch
	mov	x4,4				; case : rayon 4 ; radio : un disque
	cmp	w22,UT_RADIO
	b.ne	.r
	mov	x4,UK_BOX/2
.r:	str	x4,[sp,-16]!
	cbz	x24,.off
	jet	w5,TJ_ACCENT			; choisi : ACCENT
	cbnz	x23,.on
	jet	w0,TJ_FACE
	jet	w1,TJ_ACCENT
	mov	x2,90
	bl	ui_mix
	mov	w5,w0
.on:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,UK_BOX
	mov	x3,UK_BOX
	ldr	x4,[sp],16
	bl	ui_rbox
	cmp	w22,UT_RADIO
	b.eq	.dot
	adr	x0,uk_s_check			; « ✓ » Sans Bold 13 blanc centre
	mov	x1,13
	str	x0,[sp,-16]!
	bl	uk_bold
	mov	x2,x0
	ldr	x0,[sp],16
	bl	ui_text_width
	mov	x1,UK_BOX
	sub	x1,x1,x0
	add	x1,x20,x1,asr 1
	adr	x0,uk_s_check
	mov	x2,x21
	mov	x3,UK_BOX
	jet	w4,TJ_ACCENT_INK
	str	x1,[sp,-16]!
	bl	uk_bold
	mov	x5,x0
	ldr	x1,[sp],16
	adr	x0,uk_s_check
	mov	x6,0
	mov	x7,13
	bl	uk_text_box
	b	.text
.dot:	add	x0,x20,6			; point blanc de 6
	add	x1,x21,6
	mov	x2,6
	mov	x3,6
	mov	x4,3
	jet	w5,TJ_ACCENT_INK
	mov	w6,w5
	bl	ui_rbox
	b	.text
.off:	jet	w5,TJ_LIGNE_FORTE		; contour LIGNE_FORTE, interieur SURFACE
	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,UK_BOX
	mov	x3,UK_BOX
	ldr	x4,[sp]
	bl	ui_rbox
	jet	w5,TJ_SURFACE
	cbnz	x23,.in
	jet	w5,TJ_FACE
.in:	ldr	w0,[x19,UW_FLAGS]		; survol : interieur CREUX
	tbz	w0,2,.in2
	jet	w5,TJ_CREUX
.in2:	mov	w6,w5
	add	x0,x20,1
	add	x1,x21,1
	mov	x2,UK_BOX-2
	mov	x3,UK_BOX-2
	ldr	x4,[sp],16
	sub	x4,x4,1
	bl	ui_rbox
	b	.text
.switch:					; rail 34 x 20, bouton 16
	sub	x21,x21,1
	jet	w5,TJ_LIGNE_FORTE
	cbz	x24,.s1
	jet	w5,TJ_ACCENT
.s1:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,34
	mov	x3,20
	mov	x4,10
	bl	ui_rbox
	cbnz	x24,.s2				; eteint : interieur CREUX
	jet	w5,TJ_CREUX
	mov	w6,w5
	add	x0,x20,1
	add	x1,x21,1
	mov	x2,32
	mov	x3,18
	mov	x4,9
	bl	ui_rbox
.s2:	add	x0,x20,2			; le bouton
	cbz	x24,.s3
	add	x0,x20,16
.s3:	add	x1,x21,2
	mov	x2,16
	mov	x3,16
	mov	x4,8
	movl	w5,0x00FFFFFF
	cbnz	x24,.s4
	jet	w5,TJ_LIGNE_FORTE
	mov	w6,w5
	stp	x0,x1,[sp,-16]!
	bl	ui_rbox
	ldp	x0,x1,[sp],16
	add	x0,x0,1
	add	x1,x1,1
	mov	x2,14
	mov	x3,14
	mov	x4,7
	movl	w5,0x00FFFFFF
.s4:	mov	w6,w5
	bl	ui_rbox
	mov	x0,x19
	mov	x1,46
	bl	uk_tg_text
	b	.out
.text:	mov	x0,x19
	mov	x1,UK_BOX+8
	bl	uk_tg_text
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget : la valeur change (radio : choisi, les autres du groupe
; decoches) -> x0 = UN_CHANGE (0 : radio deja choisi)
uk_tg_toggle:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	w1,[x19,UW_TYPE]
	cmp	w1,UT_RADIO
	b.eq	.radio
	ldr	x1,[x19,UW_VALUE]
	eor	x1,x1,1
	str	x1,[x19,UW_VALUE]
	b	.done
.radio:	ldr	x1,[x19,UW_VALUE]
	mov	x0,0
	cbnz	x1,.out
	mov	x1,1
	str	x1,[x19,UW_VALUE]
	adr	x20,uk_wfirst			; les autres du groupe
	ldr	x20,[x20]
.l:	cbz	x20,.done
	cmp	x20,x19
	b.eq	.n
	ldr	w0,[x20,UW_TYPE]
	cmp	w0,UT_RADIO
	b.ne	.n
	ldr	x0,[x20,UW_WIN]
	ldr	x1,[x19,UW_WIN]
	cmp	x0,x1
	b.ne	.n
	ldr	x0,[x20,UW_AUX]
	ldr	x1,[x19,UW_AUX]
	cmp	x0,x1
	b.ne	.n
	ldr	x0,[x20,UW_VALUE]
	cbz	x0,.n
	str	xzr,[x20,UW_VALUE]
	mov	x0,x20
	bl	uk_w_refresh
.n:	ldr	x20,[x20,UW_NEXT]
	b	.l
.done:	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
.out:	ldp	x19,x20,[sp],16
	epilogue

; souris : comme un bouton ; un clic change la valeur
uk_tg_mouse:
	prologue
	str	x0,[sp,-16]!
	bl	uk_bt_mouse
	cmp	x0,UN_ACTIVATE
	ldr	x1,[sp],16
	b.ne	.no
	mov	x0,x1
	bl	uk_tg_toggle
	b	.out
.no:	mov	x0,0
.out:	epilogue

; clavier : Espace
uk_tg_key:
	cmp	w2,' '
	b.ne	.no
	b	uk_tg_toggle
.no:	mov	x0,-1
	ret

; ---------------------------------------------------------------------------
;  Listes deroulantes
; ---------------------------------------------------------------------------

; x0 = widget, x1 = elements « un|deux|trois » (0 : aucun) : les ajoute
uk_items_split:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cbz	x20,.out
.it:	ldrb	w0,[x20]
	cbz	w0,.out
	adr	x0,uk_fbuf			; un element
	mov	x1,0
.c:	ldrb	w2,[x20],1
	cbz	w2,.last
	cmp	w2,'|'
	b.eq	.add
	cmp	x1,254
	b.hs	.c
	strb	w2,[x0,x1]
	add	x1,x1,1
	b	.c
.last:	sub	x20,x20,1
.add:	strb	wzr,[x0,x1]
	mov	x0,x19
	adr	x1,uk_fbuf
	mov	x2,0
	bl	ui_list_add
	b	.it
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0..x3 = rectangle (hauteur 0 : un controle), x4 = elements « a|b|c »
; -> poignee (valeur 0 : le premier)
ui_dropdown:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	uk_tr_items
	mov	x19,x4
	mov	x5,0
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_DROP
	mov	x6,0
	bl	uk_new_sized
	cbz	x0,.out
	mov	x20,x0
	mov	x1,x19
	bl	uk_items_split
	str	xzr,[x20,UW_VALUE]
	ldr	x0,[x20,UW_NITEMS]
	cbnz	x0,.r
	mov	x0,-1
	str	x0,[x20,UW_VALUE]
.r:	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0..x3, x4 = elements : liste deroulante modifiable (un champ de texte et
; le bouton de son menu) -> poignee ; texte : ui_w_get_text
ui_combobox:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x4
	mov	x5,0
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_COMBO
	mov	x6,0
	bl	uk_new_sized
	cbz	x0,.out
	mov	x20,x0
	mov	x1,x19
	bl	uk_items_split
	mov	x0,-1
	str	x0,[x20,UW_VALUE]
	ldp	w0,w1,[x20,UW_X]		; le champ, a gauche du bouton
	ldp	w2,w3,[x20,UW_W]
	sub	x2,x2,x3
	sub	x2,x2,4
	mov	x4,0
	bl	ui_textbox
	cbz	x0,.ok
	str	x20,[x0,UW_PARENT]
	str	x0,[x20,UW_AUX2]
.ok:	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; place du champ d'une liste modifiable (apres ui_w_move)
uk_cb_layout:
	ldr	x1,[x0,UW_AUX2]
	cbz	x1,.ret
	ldp	w2,w3,[x0,UW_X]
	stp	w2,w3,[x1,UW_X]
	ldp	w2,w3,[x0,UW_W]
	sub	w2,w2,w3
	sub	w2,w2,4
	stp	w2,w3,[x1,UW_W]
.ret:	ret

; x0 = widget -> x0 = texte de l'element choisi (0 : aucun)
uk_dd_current:
	ldr	w1,[x0,UW_TYPE]			; (selecteur de date : son texte)
	cmp	w1,UT_DATE
	b.ne	.it
	ldr	x0,[x0,UW_TEXT]
	ret
.it:	ldr	x1,[x0,UW_VALUE]
	tbnz	x1,63,.no
	ldr	x2,[x0,UW_NITEMS]
	cmp	x1,x2
	b.hs	.no
	ldr	x2,[x0,UW_ITEMS]
	add	x2,x2,x1,lsl 4
	ldr	x0,[x2]
	ret
.no:	mov	x0,0
	ret

uk_dd_draw:
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
	mov	x0,x19
	bl	uk_w_enabled
	mov	x24,x0
	ldr	w0,[x19,UW_TYPE]
	cmp	w0,UT_COMBO
	b.eq	.combo
	jet	w5,TJ_LIGNE_FORTE		; contour, interieur ELEVE (survol : CREUX)
	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	mov	x4,8
	bl	ui_rbox
	jet	w5,TJ_ELEVE
	cbnz	x24,.e
	jet	w5,TJ_FACE
.e:	ldr	w0,[x19,UW_FLAGS]
	tbz	w0,2,.e2
	jet	w5,TJ_CREUX
.e2:	mov	w6,w5
	add	x0,x20,1
	add	x1,x21,1
	sub	x2,x22,2
	sub	x3,x23,2
	mov	x4,7
	bl	ui_rbox
	mov	x0,x19				; l'element choisi
	bl	uk_dd_current
	cbz	x0,.chev
	str	x0,[sp,-16]!
	bl	uk_met
	ldr	x7,[x0,MT_PX]
	bl	uk_font_r
	mov	x5,x0
	ldr	x0,[sp],16
	add	x1,x20,10
	mov	x2,x21
	mov	x3,x23
	jet	w4,TJ_TEXTE1
	cbnz	x24,.t
	jet	w4,TJ_TEXTE3
.t:	sub	x6,x22,40
	bl	uk_text_box
	b	.chev
.combo:	jet	w5,TJ_LIGNE_FORTE		; le bouton du menu a droite (le champ
	ldr	w0,[x19,UW_FLAGS]		; est l'enfant)
	tbz	w0,2,.cb
	jet	w5,TJ_ACCENT
.cb:	mov	w6,w5
	add	x0,x20,x22
	sub	x0,x0,x23
	mov	x1,x21
	mov	x2,x23
	mov	x3,x23
	mov	x4,8
	bl	ui_rbox
	jet	w5,TJ_ELEVE
	mov	w6,w5
	add	x0,x20,x22
	sub	x0,x0,x23
	add	x0,x0,1
	add	x1,x21,1
	sub	x2,x23,2
	sub	x3,x23,2
	mov	x4,7
	bl	ui_rbox
.chev:	bl	uk_font_r			; « ▾ » a droite
	mov	x5,x0
	adr	x0,uk_s_chev
	add	x1,x20,x22
	sub	x1,x1,22
	ldr	w2,[x19,UW_TYPE]
	cmp	w2,UT_COMBO
	b.ne	.cx
	add	x1,x20,x22			; (au milieu du bouton carre)
	sub	x1,x1,x23,lsr 1
	sub	x1,x1,5
.cx:	mov	x2,x21
	mov	x3,x23
	jet	w4,TJ_TEXTE2
	mov	x6,0
	mov	x7,13
	bl	uk_text_box
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget : ouvre son menu sous lui -> x0 = UN_CHANGE si un autre element
; est choisi (liste modifiable : son texte dans le champ)
uk_dd_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,UW_NITEMS]
	cbz	x20,.zero
	adr	x21,uk_popbuf			; libelles a la suite, un vide a la fin
	mov	x22,0
	ldr	w23,[x19,UW_W]			; largeur : le widget, ou son plus long
.it:	cmp	x22,x20
	b.hs	.end
	bl	uk_font_r
	mov	x2,x0
	ldr	x0,[x19,UW_ITEMS]
	add	x0,x0,x22,lsl 4
	ldr	x0,[x0]
	mov	x1,UK_PX
	bl	ui_text_width
	add	x0,x0,48
	cmp	x0,x23
	csel	x23,x0,x23,hi
	ldr	x1,[x19,UW_ITEMS]
	add	x1,x1,x22,lsl 4
	ldr	x1,[x1]
	adr	x9,uk_popbuf+UK_POP_MAX-2
.c:	cmp	x21,x9
	b.hs	.end
	ldrb	w2,[x1],1
	strb	w2,[x21],1
	cbnz	w2,.c
	add	x22,x22,1
	b	.it
.end:	strb	wzr,[x21]
	bl	uk_win_pos
	ldp	w2,w3,[x19,UW_X]
	sxtw	x2,w2
	sxtw	x3,w3
	add	x0,x0,x2
	add	x1,x1,x3
	ldr	w3,[x19,UW_H]
	add	x1,x1,x3
	add	x1,x1,2
	adr	x2,uk_popbuf
	mov	x3,x23
	bl	ui_popup
	tbnz	x0,63,.zero
	ldr	x1,[x19,UW_VALUE]
	cmp	x0,x1
	b.eq	.zero
	str	x0,[x19,UW_VALUE]
	ldr	w1,[x19,UW_TYPE]
	cmp	w1,UT_COMBO
	b.ne	.draw
	mov	x0,x19
	bl	uk_dd_current
	mov	x1,x0
	ldr	x0,[x19,UW_AUX2]
	bl	ui_w_set_text
.draw:	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
	b	.out
.zero:	mov	x0,0
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris : survol ; appui -> le menu
uk_dd_mouse:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cmp	x1,1
	b.eq	.open
	ldr	w2,[x19,UW_FLAGS]
	orr	w3,w2,UWF_HOVER
	cmp	x1,3
	b.ne	.st
	and	w3,w2,not UWF_HOVER
.st:	cmp	w2,w3
	b.eq	.zero
	str	w3,[x19,UW_FLAGS]
	mov	x0,x19
	bl	uk_w_refresh
.zero:	mov	x0,0
	b	.out
.open:	adr	x0,uk_capture			; (le menu prend la souris)
	str	xzr,[x0]
	ldr	w2,[x19,UW_FLAGS]
	and	w2,w2,not UWF_HOVER
	str	w2,[x19,UW_FLAGS]
	mov	x0,x19
	bl	uk_dd_open
.out:	ldr	x19,[sp],16
	epilogue

; clavier : Espace, Entree, Alt+bas -> menu ; haut, bas -> element voisin
uk_dd_key:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	w2,' '
	b.eq	.open
	cmp	w1,KEY_ENTER
	b.eq	.open
	ldr	x20,[x19,UW_VALUE]
	cmp	w1,KEY_UP
	b.ne	.d
	sub	x1,x20,1
	b	.set
.d:	cmp	w1,KEY_DOWN
	b.ne	.no
	mov	w2,0x44				; Alt
	tst	w3,w2
	b.ne	.open
	add	x1,x20,1
.set:	ldr	x2,[x19,UW_NITEMS]
	cmp	x1,x2
	b.hs	.zero				; (hors bornes : rien)
	str	x1,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
	b	.out
.open:	mov	x0,x19
	bl	uk_dd_open
	b	.out
.zero:	mov	x0,0
	b	.out
.no:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; liste modifiable : son champ a change -> UN_CHANGE de la liste
uk_cb_child:
	mov	x0,x2
	ret
