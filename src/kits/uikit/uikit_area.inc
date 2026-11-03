; ============================================================================
;  kits/uikit_area.inc - UIKit : le texte multiligne (UT_AREA), d'apres
;  l'editeur (src/apps/editor) : tampon plat UTF-8 (« \n » entre les lignes),
;  curseur et ancre en octets, insertion / suppression par deplacement.
;
;  Clic, glisser, double-clic (mot), fleches (haut / bas gardent la colonne
;  en pixels), Debut / Fin (ligne), Ctrl+Debut / Fin (texte), Page, Maj :
;  selection, Ctrl + fleches (mot), Ctrl+A, C, X, V (plusieurs lignes),
;  Entree, Tab (4 espaces : le widget garde Tab), Retour arriere, Suppr,
;  molette, barre de defilement (enfant), defilement horizontal au curseur.
;  UTF_READONLY, UTF_MONO, UTA_LINENUM (numeros de ligne dans une marge).
;  Champs : VALUE curseur, ANCHOR ancre, POS premiere ligne, DRAG
;  defilement horizontal (pixels), DRAG2 colonne voulue (pixels), AUX barre.
; ============================================================================

UK_AREA_MAX	= 1024*1024		; longueur max par defaut
UK_LINE_MAX	= 1024			; octets dessines par ligne
UK_TA_PAD	= 6

; x0..x3 = rectangle, x4 = drapeaux (UTF_READONLY, UTF_MONO, UTA_LINENUM)
; -> poignee
UW_TA_BG	= 304			; (ui_textarea_paper) papier + 1, 0 : le theme
UW_TA_INK	= 312			; encre + 1
UW_TA_LEX	= UW_HPOS		; (UTA_CS, UTA_XML) etat de l'analyse, de ligne en ligne

; x0 = texte multiligne, x1 = papier 0x00RRGGBB (-1 : celui du theme), x2 = encre
ui_textarea_paper:
	add	x1,x1,1
	add	x2,x2,1
	str	x1,[x0,UW_TA_BG]
	str	x2,[x0,UW_TA_INK]
	ret

ui_textarea:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_AREA
	bl	uk_w_new
	cbz	x0,.out
	mov	x20,x0
	str	x19,[x20,UW_STYLE]
	mov	x1,0
	mov	x2,0
	bl	uk_set_text
	mov	x0,x20
	bl	uk_ta_bars
	mov	x0,x20
	bl	uk_ta_layout
	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte multiligne : cree ou retire ses barres de defilement selon son
; style (UTA_VSCROLL, UTA_HSCROLL)
uk_ta_bars:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,UW_STYLE]
	ldr	x0,[x19,UW_AUX]			; verticale
	tbnz	x20,4,.vwant
	cbz	x0,.h
	bl	ui_w_destroy
	str	xzr,[x19,UW_AUX]
	b	.h
.vwant:	cbnz	x0,.h
	mov	x0,0
	mov	x1,0
	mov	x2,8
	mov	x3,8
	mov	x4,0
	bl	ui_scrollbar
	cbz	x0,.h
	str	x19,[x0,UW_PARENT]
	str	x0,[x19,UW_AUX]
.h:	ldr	x0,[x19,UW_HBAR]		; horizontale
	tbnz	x20,5,.hwant
	cbz	x0,.out
	bl	ui_w_destroy
	str	xzr,[x19,UW_HBAR]
	b	.out
.hwant:	cbnz	x0,.out
	mov	x0,0
	mov	x1,0
	mov	x2,8
	mov	x3,8
	mov	x4,USB_HORIZONTAL
	bl	ui_scrollbar
	cbz	x0,.out
	str	x19,[x0,UW_PARENT]
	str	x0,[x19,UW_HBAR]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte multiligne -> x0 = police, x1 = taille, x2 = hauteur de ligne
uk_ta_font:
	prologue
	bl	uk_tb_font
	add	x2,x1,x1,lsr 1			; 1,5 x la taille
	epilogue

; x0 = texte multiligne -> x0 = marge des numeros de ligne (0 : sans)
uk_ta_gutter:
	ldr	x1,[x0,UW_STYLE]
	mov	x0,0
	tbz	x1,3,.ret
	mov	x0,44
.ret:	ret

; x0 = widget -> x0 = lignes entieres visibles (>= 1)
uk_ta_rows:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	uk_ta_bottom
	ldr	w1,[x19,UW_H]
	sub	x1,x1,x0
	sub	x1,x1,2*UK_TA_PAD
	str	x1,[sp,8]
	mov	x0,x19
	bl	uk_ta_font
	ldr	x0,[sp,8]
	udiv	x0,x0,x2
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lt
	ldr	x19,[sp],16
	epilogue

; x0 = widget -> x0 = place de la barre horizontale en bas (0 : sans), x1 =
; place de la verticale a droite
uk_ta_bottom:
	prologue
	stp	x0,x0,[sp,-16]!
	bl	uk_met
	ldr	x2,[x0,MT_SB]
	add	x2,x2,2
	ldr	x3,[sp]
	add	sp,sp,16
	ldr	x4,[x3,UW_HBAR]
	cmp	x4,0
	csel	x0,xzr,x2,eq
	ldr	x4,[x3,UW_AUX]
	cmp	x4,0
	csel	x1,xzr,x2,eq
	epilogue

; x0 = widget -> x0 = nombre de lignes (feuille)
uk_ta_nlines:
	ldr	x1,[x0,UW_TEXT]
	ldr	x2,[x0,UW_TLEN]
	mov	x0,1
	mov	x3,0
.l:	cmp	x3,x2
	b.hs	.ret
	ldrb	w4,[x1,x3]
	add	x3,x3,1
	cmp	w4,10
	b.ne	.l
	add	x0,x0,1
	b	.l
.ret:	ret

; x0 = widget, x1 = decalage -> x0 = numero de sa ligne, x1 = debut de la
; ligne (feuille)
uk_ta_line_of:
	ldr	x2,[x0,UW_TEXT]
	mov	x0,0
	mov	x3,0				; debut de la ligne courante
	mov	x4,0
.l:	cmp	x4,x1
	b.hs	.ret
	ldrb	w5,[x2,x4]
	add	x4,x4,1
	cmp	w5,10
	b.ne	.l
	add	x0,x0,1
	mov	x3,x4
	b	.l
.ret:	mov	x1,x3
	ret

; x0 = widget, x1 = numero de ligne -> x0 = son debut (au-dela : la fin du
; texte), x1 = sa fin (le « \n » ou la fin du texte) (feuille)
uk_ta_line:
	ldr	x2,[x0,UW_TEXT]
	ldr	x3,[x0,UW_TLEN]
	mov	x4,0
.f:	cbz	x1,.got
	cmp	x4,x3
	b.hs	.got
	ldrb	w5,[x2,x4]
	add	x4,x4,1
	cmp	w5,10
	b.ne	.f
	sub	x1,x1,1
	b	.f
.got:	mov	x0,x4
.e:	cmp	x4,x3
	b.hs	.ret
	ldrb	w5,[x2,x4]
	cmp	w5,10
	b.eq	.ret
	add	x4,x4,1
	b	.e
.ret:	mov	x1,x4
	ret

; x0 = widget, x1 = debut, x2 = octets -> x0 = largeur en pixels
uk_ta_width:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	uk_ta_font
	mov	x22,x0
	bl	ttf_set_size
	mov	x0,x22
	ldr	x1,[x19,UW_TEXT]
	add	x1,x1,x20
	mov	x2,x21
	mov	x3,-1
	mov	x4,0
	bl	ttf_text_pos
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1 = numero de ligne, x2 = x en pixels (depuis le debut du
; texte de la ligne) -> x0 = decalage sous ce point
uk_ta_offset_at:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	bl	uk_ta_line
	mov	x21,x0				; debut
	sub	x22,x1,x0			; longueur
	mov	x0,x19
	bl	uk_ta_font
	str	x0,[sp,-16]!
	bl	ttf_set_size
	ldr	x0,[sp],16
	ldr	x1,[x19,UW_TEXT]
	add	x1,x1,x21
	mov	x2,x22
	cmp	x20,0
	csel	x3,xzr,x20,lt
	mov	x4,0
	bl	ttf_text_pos
	add	x0,x21,x1
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget : place la barre de defilement, recopie la plage
uk_ta_layout:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	uk_met
	ldr	x20,[x0,MT_SB]
	mov	x0,x19
	bl	uk_ta_bottom
	mov	x21,x0				; place en bas
	ldp	w4,w5,[x19,UW_X]
	ldp	w6,w7,[x19,UW_W]
	ldr	x1,[x19,UW_AUX]			; verticale : a droite
	cbz	x1,.h
	add	x2,x4,x6
	sub	x2,x2,x20
	sub	x2,x2,2
	add	x3,x5,2
	stp	w2,w3,[x1,UW_X]
	str	w20,[x1,UW_W]
	sub	x2,x7,4
	sub	x2,x2,x21
	str	w2,[x1,UW_H]
.h:	ldr	x1,[x19,UW_HBAR]		; horizontale : en bas, apres la marge
	cbz	x1,.sync
	mov	x0,x19
	bl	uk_ta_gutter
	ldp	w4,w5,[x19,UW_X]
	ldp	w6,w7,[x19,UW_W]
	ldr	x1,[x19,UW_HBAR]
	add	x2,x4,x0
	add	x2,x2,2
	add	x3,x5,x7
	sub	x3,x3,x20
	sub	x3,x3,2
	stp	w2,w3,[x1,UW_X]
	sub	x2,x6,x0
	sub	x2,x2,4
	ldr	x3,[x19,UW_AUX]
	cbz	x3,.hw
	sub	x2,x2,x20
	sub	x2,x2,2
.hw:	str	w2,[x1,UW_W]
	str	w20,[x1,UW_H]
.sync:	mov	x0,x19
	bl	uk_ta_sync
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget : premiere ligne bornee, plage de la barre
uk_ta_sync:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	uk_ta_rows
	mov	x20,x0
	mov	x0,x19
	bl	uk_ta_nlines
	sub	x2,x0,x20
	cmp	x2,0
	csel	x2,xzr,x2,lt
	ldr	x3,[x19,UW_POS]
	cmp	x3,x2
	csel	x3,x2,x3,gt
	cmp	x3,0
	csel	x3,xzr,x3,lt
	str	x3,[x19,UW_POS]
	ldr	x1,[x19,UW_AUX]
	cbz	x1,.h
	str	xzr,[x1,UW_MIN]
	str	x0,[x1,UW_MAX]
	str	x20,[x1,UW_PAGE]
	str	x3,[x1,UW_VALUE]
.h:	ldr	x21,[x19,UW_HBAR]		; horizontale : la ligne la plus longue
	cbz	x21,.out
	mov	x0,x19
	bl	uk_ta_maxw
	mov	x22,x0
	mov	x0,x19
	bl	uk_ta_textw
	str	xzr,[x21,UW_MIN]
	str	x22,[x21,UW_MAX]
	str	x0,[x21,UW_PAGE]
	mov	x1,10
	str	x1,[x21,UW_STEP]
	sub	x1,x22,x0			; defilement borne
	cmp	x1,0
	csel	x1,xzr,x1,lt
	ldr	x2,[x19,UW_DRAG]
	cmp	x2,x1
	csel	x2,x1,x2,gt
	str	x2,[x19,UW_DRAG]
	str	x2,[x21,UW_VALUE]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget -> x0 = largeur estimee de la ligne la plus longue (colonnes,
; taquets de 4, x largeur d'une espace ; exacte en mono) + 40
uk_ta_maxw:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,UW_TEXT]
	ldr	x2,[x19,UW_TLEN]
	mov	x3,0				; colonne
	mov	x20,0				; max
	mov	x4,0
.l:	cmp	x4,x2
	b.hs	.e
	ldrb	w5,[x1,x4]
	add	x4,x4,1
	cmp	w5,10
	b.ne	.t
	cmp	x3,x20
	csel	x20,x3,x20,hi
	mov	x3,0
	b	.l
.t:	cmp	w5,9
	b.ne	.c
	add	x3,x3,4
	and	x3,x3,not 3
	b	.l
.c:	and	w5,w5,0xC0
	cmp	w5,0x80
	b.eq	.l
	cmp	w5,13				; (retour chariot : rien)
	b.eq	.l
	add	x3,x3,1
	b	.l
.e:	cmp	x3,x20
	csel	x20,x3,x20,hi
	mov	x0,x19				; largeur d'une espace
	bl	uk_ta_font
	mov	x2,x0
	adr	x0,uk_s_space
	bl	ui_text_width
	mul	x0,x0,x20
	add	x0,x0,40
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget -> x0 = largeur utile du texte (sans marges, numeros, barre)
uk_ta_textw:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	uk_ta_gutter
	ldr	w1,[x19,UW_W]
	sub	x1,x1,x0
	sub	x1,x1,2*UK_TA_PAD+4
	str	x1,[sp,8]
	mov	x0,x19
	bl	uk_ta_bottom
	ldr	x0,[sp,8]
	sub	x0,x0,x1
	ldr	x19,[sp],16
	epilogue

; x0 = widget : rend le curseur visible (lignes et pixels)
uk_ta_show_caret:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,UW_VALUE]
	bl	uk_ta_line_of
	mov	x20,x0				; ligne
	mov	x21,x1				; son debut
	mov	x0,x19
	bl	uk_ta_rows
	ldr	x1,[x19,UW_POS]
	cmp	x20,x1
	b.ge	.b
	str	x20,[x19,UW_POS]
	b	.h
.b:	add	x2,x1,x0
	cmp	x20,x2
	b.lt	.h
	sub	x1,x20,x0
	add	x1,x1,1
	str	x1,[x19,UW_POS]
.h:	mov	x0,x19				; horizontal
	mov	x1,x21
	ldr	x2,[x19,UW_VALUE]
	sub	x2,x2,x21
	bl	uk_ta_width
	mov	x22,x0
	mov	x0,x19
	bl	uk_ta_textw
	ldr	x1,[x19,UW_DRAG]
	sub	x2,x22,x1
	cmp	x2,0
	b.ge	.r
	sub	x1,x22,40			; (un peu de marge a gauche)
	cmp	x1,0
	csel	x1,xzr,x1,lt
	b	.st
.r:	cmp	x2,x0
	b.le	.st
	sub	x1,x22,x0
	add	x1,x1,40
.st:	str	x1,[x19,UW_DRAG]
	mov	x0,x19
	bl	uk_ta_sync
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

uk_ta_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,64			; +0 x du texte, +8 y, +16 lignes, +24 sel a, +32 sel b, +40 marge, +48 bas
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x19
	bl	uk_w_focused
	mov	x24,x0
	jet	w5,TJ_LIGNE_FORTE		; cadre comme un champ
	cbz	x24,.c
	ldr	x0,[x19,UW_STYLE]		; (editeur de code : sans le bleu du focus)
	tbnz	x0,3,.c
	jet	w5,TJ_ACCENT
.c:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	mov	x4,6
	bl	ui_rbox
	jet	w5,TJ_SURFACE
	ldr	x0,[x19,UW_TA_BG]		; (ui_textarea_paper)
	cbz	x0,.tjbg
	sub	w5,w0,1
.tjbg:	mov	w6,w5
	add	x0,x20,1
	add	x1,x21,1
	sub	x2,x22,2
	sub	x3,x23,2
	mov	x4,5
	bl	ui_rbox
	mov	x0,x19
	bl	uk_ta_gutter
	str	x0,[sp,40]
	cbz	x0,.ng
	mov	x2,x0				; marge des numeros : CREUX
	add	x0,x20,1
	add	x1,x21,1
	sub	x3,x23,2
	jet	w4,TJ_CREUX
	bl	ui_fill
.ng:	mov	x0,x19
	bl	ui_textbox_selection
	stp	x0,x1,[sp,24]
	mov	x0,x19
	bl	uk_ta_font
	mov	x25,x0				; police
	mov	x26,x1				; taille
	mov	x27,x2				; hauteur de ligne
	mov	x0,x19
	bl	uk_ta_bottom			; bas des lignes (barre horizontale)
	add	x1,x21,x23
	sub	x1,x1,UK_TA_PAD
	sub	x1,x1,x0
	str	x1,[sp,48]
	mov	x0,x19
	bl	uk_ta_nlines
	str	x0,[sp,16]			; (nombre de lignes)
	ldr	x28,[x19,UW_POS]		; ligne
	add	x0,x21,UK_TA_PAD
	str	x0,[sp,8]
	mov	x0,x19
	bl	uk_lex_start
.line:	ldr	x0,[sp,16]
	cmp	x28,x0
	b.hs	.caret
	ldr	x0,[sp,8]			; encore la place ?
	add	x0,x0,x27
	ldr	x1,[sp,48]
	cmp	x0,x1
	b.gt	.caret
	mov	x0,x19
	mov	x1,x28
	bl	uk_ta_line
	stp	x0,x1,[sp,-16]!			; debut, fin
	ldr	x0,[sp,16+40]			; numero de ligne
	cbz	x0,.nonum
	add	x0,x28,1
	adr	x1,uk_fitbuf
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,uk_fitbuf
	mov	x1,x26
	mov	x2,x25
	bl	ui_text_width
	ldr	x1,[sp,16+40]
	sub	x1,x1,x0
	sub	x1,x1,8
	add	x1,x20,x1
	adr	x0,uk_fitbuf
	ldr	x2,[sp,16+8]
	mov	x3,x27
	jet	w4,TJ_TEXTE3
	mov	x5,x25
	mov	x6,0
	mov	x7,x26
	bl	uk_text_box
.nonum:	ldr	x0,[sp,16+40]			; zone du texte : decoupe
	add	x0,x20,x0
	add	x0,x0,UK_TA_PAD
	ldr	x1,[sp,16+8]
	mov	x2,x19
	str	x0,[sp,16]			; (x de la zone)
	mov	x0,x19
	bl	uk_ta_textw
	add	x2,x0,8
	ldr	x0,[sp,16]
	ldr	x1,[sp,16+8]
	mov	x3,x27
	bl	uk_clip_push
	ldr	x0,[x19,UW_HOT]			; ligne eclairee (ui_textarea_highlight)
	cmp	x0,x28
	b.ne	.nohot
	mov	x0,x19
	bl	uk_ta_textw
	add	x2,x0,8
	ldr	x0,[sp,16]
	ldr	x1,[sp,16+8]
	mov	x3,x27
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	jet	w0,TJ_SURFACE
	jet	w1,TJ_ALERTE
	mov	x2,80
	bl	ui_mix
	mov	w4,w0
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	bl	ui_fill
.nohot:	ldr	x0,[sp,16]			; x du debut du texte
	ldr	x1,[x19,UW_DRAG]
	sub	x0,x0,x1
	str	x0,[sp,16]
	ldp	x0,x1,[sp]			; selection dans cette ligne ?
	ldp	x2,x3,[sp,16+24]
	cmp	x2,x3
	b.eq	.text
	cmp	x3,x0				; fin de selection avant la ligne
	b.lo	.text
	cmp	x2,x1				; debut apres la ligne
	b.hi	.text
	cmp	x2,x0
	csel	x2,x0,x2,lo
	cmp	x3,x1
	mov	x4,0				; (la fin de ligne choisie : 6 px)
	b.ls	.sb
	mov	x3,x1
	mov	x4,6
.sb:	stp	x3,x4,[sp,-16]!
	sub	x2,x2,x0
	mov	x1,x0
	mov	x0,x19
	bl	uk_ta_width
	mov	x5,x0				; xa
	ldp	x3,x4,[sp],16
	stp	x5,x4,[sp,-16]!
	ldr	x1,[sp,16]
	sub	x2,x3,x1
	mov	x0,x19
	bl	uk_ta_width
	ldp	x5,x4,[sp],16
	add	x2,x0,x4
	sub	x2,x2,x5			; largeur
	ldr	x0,[sp,16]
	add	x0,x0,x5
	ldr	x1,[sp,16+8]
	mov	x3,x27
	jet	w4,TJ_ACCENT_CLAIR
	cbnz	x24,.sc
	jet	w4,TJ_CREUX
.sc:	bl	ui_fill
.text:	ldp	x1,x2,[sp]			; le texte de la ligne
	mov	x0,x19
	ldr	x3,[sp,16]
	ldr	x4,[sp,16+8]
	bl	uk_ta_paint_line
	bl	uk_clip_pop
	add	sp,sp,16
	ldr	x0,[sp,8]
	add	x0,x0,x27
	str	x0,[sp,8]
	add	x28,x28,1
	b	.line
.caret:	cbz	x24,.out			; curseur
	ldr	x0,[x19,UW_STYLE]
	tbnz	x0,1,.out
	ldr	x1,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_ta_line_of
	ldr	x2,[x19,UW_POS]
	subs	x2,x0,x2
	b.lt	.out
	mul	x26,x2,x27			; y relatif
	add	x26,x26,x21
	add	x26,x26,UK_TA_PAD
	add	x0,x26,x27
	ldr	x2,[sp,48]
	cmp	x0,x2
	b.gt	.out
	ldr	x2,[x19,UW_VALUE]
	sub	x2,x2,x1
	mov	x0,x19
	bl	uk_ta_width
	ldr	x1,[x19,UW_DRAG]
	sub	x0,x0,x1
	tbnz	x0,63,.out
	mov	x25,x0
	mov	x0,x19
	bl	uk_ta_textw
	cmp	x25,x0
	b.gt	.out
	ldr	x0,[sp,40]
	add	x0,x20,x0
	add	x0,x0,UK_TA_PAD
	add	x0,x0,x25
	mov	x1,x26
	mov	x2,1
	mov	x3,x27
	jet	w4,TJ_ACCENT
	bl	ui_fill
.out:	add	sp,sp,64
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x2, x3 = point local -> x0 = decalage sous ce point
uk_ta_hit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	mov	x21,x3
	bl	uk_ta_font
	sub	x0,x21,UK_TA_PAD
	cmp	x0,0
	csel	x0,xzr,x0,lt
	udiv	x0,x0,x2
	ldr	x1,[x19,UW_POS]
	add	x21,x0,x1			; ligne
	mov	x0,x19
	bl	uk_ta_nlines
	sub	x0,x0,1
	cmp	x21,x0
	csel	x21,x0,x21,gt
	mov	x0,x19
	bl	uk_ta_gutter
	sub	x2,x20,x0
	sub	x2,x2,UK_TA_PAD
	ldr	x1,[x19,UW_DRAG]
	add	x2,x2,x1
	str	x2,[x19,UW_DRAG2]		; (colonne voulue)
	mov	x0,x19
	mov	x1,x21
	bl	uk_ta_offset_at
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris : appui (double : un mot), glisser
uk_ta_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x4
	cmp	x20,1
	b.eq	.press
	cbnz	x20,.zero
	adr	x0,uk_capture
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.zero
	mov	x0,x19
	bl	uk_ta_hit
	str	x0,[x19,UW_VALUE]
	b	.draw
.press:	mov	x0,x19
	bl	uk_ta_hit
	str	x0,[x19,UW_VALUE]
	str	x0,[x19,UW_ANCHOR]
	cmp	x21,2
	b.ne	.draw
	mov	x22,x0
	ldr	x0,[x19,UW_TEXT]
	mov	x1,x22
	ldr	x2,[x19,UW_TLEN]
	mov	x3,0
	bl	uk_word_edge
	str	x0,[x19,UW_ANCHOR]
	ldr	x0,[x19,UW_TEXT]
	mov	x1,x22
	ldr	x2,[x19,UW_TLEN]
	mov	x3,1
	bl	uk_word_edge
	str	x0,[x19,UW_VALUE]
.draw:	mov	x0,x19
	bl	uk_ta_show_caret
	mov	x0,x19
	bl	uk_w_refresh
.zero:	mov	x0,0
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = code, x2 = caractere, x3 = mods -> code (-1 : non pris)
uk_ta_key:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	ldr	x23,[x19,UW_STYLE]
	mov	x24,0
	mov	w9,MOD_CTRL
	tst	w22,w9
	b.eq	.nav
	cmp	w20,0x1D			; Ctrl+Z : annuler ; Ctrl+Y : retablir
	b.eq	.undo
	cmp	w20,0x1C
	b.ne	.cv
	mov	x1,1
	b	.undo1
.undo:	mov	x1,0
.undo1:	tbnz	x23,1,.zero
	mov	x0,x19
	bl	uk_undo_step
	cbz	x0,.zero
	mov	x24,UN_CHANGE
	b	.show
.cv:	cmp	w20,HID_V			; Ctrl+V : plusieurs lignes
	b.ne	.ctl
	tbnz	x23,1,.zero
	movl	x0,UK_CLIP_BIG
	bl	uk_alloc
	cbz	x0,.zero
	mov	x24,x0
	movl	x1,UK_CLIP_BIG
	bl	ui_clip_get
	cmp	x0,0
	b.le	.pfree
	mov	x2,x0
	mov	x1,x24
	mov	x0,x19
	bl	uk_tb_insert
.pfree:	mov	x0,x24
	bl	uk_free
	mov	x24,UN_CHANGE
	b	.show
.ctl:	cmp	w20,KEY_HOME			; Ctrl+Debut, Ctrl+Fin : le texte
	b.ne	.ce
	mov	x0,0
	b	.move
.ce:	cmp	w20,KEY_END
	b.ne	.tb
	ldr	x0,[x19,UW_TLEN]
	b	.move
.tb:	mov	x0,x19				; le reste comme un champ (Ctrl+A, C, X,
	mov	x1,x20				; fleches de mot)
	mov	x2,x21
	mov	x3,x22
	bl	uk_tb_key
	mov	x24,x0
	tbnz	x0,63,.out0
	b	.show
.nav:	cmp	w20,KEY_UP
	b.eq	.vert
	cmp	w20,KEY_DOWN
	b.eq	.vert
	cmp	w20,KEY_PGUP
	b.eq	.vert
	cmp	w20,KEY_PGDN
	b.eq	.vert
	cmp	w20,KEY_HOME
	b.eq	.home
	cmp	w20,KEY_END
	b.eq	.end
	cmp	w20,KEY_ENTER
	b.eq	.nl
	cmp	w20,KEY_TAB
	b.eq	.tab
	mov	x0,x19				; fleches gauche / droite, edition : comme
	mov	x1,x20				; un champ
	mov	x2,x21
	mov	x3,x22
	bl	uk_tb_key
	mov	x24,x0
	tbnz	x0,63,.out0
	mov	x0,x19				; colonne voulue : la nouvelle
	ldr	x1,[x19,UW_VALUE]
	bl	uk_ta_line_of
	mov	x2,x1
	ldr	x3,[x19,UW_VALUE]
	sub	x2,x3,x2
	mov	x0,x19
	bl	uk_ta_width
	str	x0,[x19,UW_DRAG2]
	b	.show
.nl:	tbnz	x23,1,.zero
	tbz	x23,6,.nl1			; code : garde le retrait de la ligne
	ldr	x1,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_ta_line_of
	ldr	x2,[x19,UW_TEXT]
	adr	x3,uk_linebuf
	mov	w4,10
	strb	w4,[x3]
	mov	x5,1
	ldr	x6,[x19,UW_VALUE]
.ind:	cmp	x1,x6
	b.hs	.indd
	ldrb	w4,[x2,x1]
	cmp	w4,' '
	b.eq	.indc
	cmp	w4,9
	b.ne	.indd
.indc:	strb	w4,[x3,x5]
	add	x5,x5,1
	add	x1,x1,1
	cmp	x5,200
	b.lo	.ind
.indd:	adr	x1,uk_linebuf
	mov	x2,x5
	b	.ins
.nl1:	mov	w0,10
	b	.ins1
.tab:	tbnz	x23,1,.no
	tbz	x23,7,.tab4			; UTA_TABS : une vraie tabulation
	mov	w0,9
	b	.ins1
.tab4:	adr	x1,uk_s_tab
	mov	x2,4
	b	.ins
.ins1:	adr	x1,uk_chbuf
	strb	w0,[x1]
	mov	x2,1
.ins:	mov	x0,x19
	bl	uk_tb_insert
	mov	x24,UN_CHANGE
	b	.show
.home:	ldr	x1,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_ta_line_of
	mov	x0,x1
	b	.move
.end:	ldr	x1,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_ta_line_of
	mov	x1,x0
	mov	x0,x19
	bl	uk_ta_line
	mov	x0,x1
	b	.move
.vert:	ldr	x1,[x19,UW_VALUE]		; haut, bas, Page : meme colonne
	mov	x0,x19
	bl	uk_ta_line_of
	mov	x21,x0				; ligne
	mov	x0,x19
	bl	uk_ta_rows
	mov	x2,1
	cmp	w20,KEY_PGUP
	b.eq	.pg
	cmp	w20,KEY_PGDN
	b.ne	.v1
.pg:	mov	x2,x0
.v1:	cmp	w20,KEY_UP
	b.eq	.minus
	cmp	w20,KEY_PGUP
	b.ne	.plus
.minus:	subs	x21,x21,x2
	b.ge	.go
	mov	x0,0				; avant la premiere : le debut
	b	.move
.plus:	add	x21,x21,x2
	mov	x0,x19
	bl	uk_ta_nlines
	cmp	x21,x0
	b.lo	.go
	ldr	x0,[x19,UW_TLEN]		; apres la derniere : la fin
	b	.move
.go:	mov	x0,x19
	mov	x1,x21
	ldr	x2,[x19,UW_DRAG2]
	bl	uk_ta_offset_at
	b	.move2
.move:	str	x0,[sp,-16]!			; colonne voulue : la nouvelle
	mov	x1,x0
	mov	x0,x19
	bl	uk_ta_line_of
	ldr	x2,[sp]
	sub	x2,x2,x1
	mov	x0,x19
	bl	uk_ta_width
	str	x0,[x19,UW_DRAG2]
	ldr	x0,[sp],16
.move2:	str	x0,[x19,UW_VALUE]
	mov	w9,MOD_SHIFT
	tst	w22,w9
	b.ne	.show
	str	x0,[x19,UW_ANCHOR]
.show:	mov	x0,x19
	bl	uk_ta_show_caret
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,x24
	b	.out
.zero:	mov	x0,0
	b	.out
.no:	mov	x0,-1
	b	.out
.out0:	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = crans : 3 lignes
uk_ta_wheel:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x2,3
	mul	x1,x1,x2
	ldr	x2,[x19,UW_POS]
	sub	x2,x2,x1
	str	x2,[x19,UW_POS]
	bl	uk_ta_sync
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; la barre a bouge
uk_ta_child:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x1,UW_VALUE]
	ldr	x2,[x19,UW_HBAR]
	cmp	x1,x2
	b.eq	.h
	str	x0,[x19,UW_POS]
	b	.s
.h:	str	x0,[x19,UW_DRAG]
.s:	mov	x0,x19
	bl	uk_ta_sync
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; x0 = texte multiligne -> x0 = ligne du curseur, x1 = colonne (octets)
ui_textarea_caret:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,UW_VALUE]
	bl	uk_ta_line_of
	ldr	x2,[x19,UW_VALUE]
	sub	x1,x2,x1
	ldr	x19,[sp],16
	epilogue

; x0 = texte multiligne, x1 = decalage : y place le curseur (sans selection)
; et le rend visible
ui_textarea_goto:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x2,[x19,UW_TLEN]
	cmp	x1,x2
	csel	x1,x2,x1,hi
	str	x1,[x19,UW_VALUE]
	str	x1,[x19,UW_ANCHOR]
	bl	uk_ta_show_caret
	mov	x0,x19
	bl	uk_w_refresh
	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Une ligne : texte uni, ou couleurs du code (UTA_CODE : assembleur FASMARM,
;  UTA_BASIC ; UTA_CS, UTA_XML : uikit_lex.inc)
; ---------------------------------------------------------------------------

; x0 = widget, x1 = debut, x2 = fin de la ligne, x3 = x du debut du texte,
; x4 = y de la ligne
uk_ta_paint_line:
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
	ldr	x0,[x19,UW_STYLE]
	tbnz	x0,6,.code
	mov	x1,x20				; uni : toute la ligne, TEXTE1
	mov	x2,x21
	jet	w3,TJ_TEXTE1
	ldr	x0,[x19,UW_TA_INK]
	cbz	x0,.tjink
	sub	w3,w0,1
.tjink:	mov	x0,x19
	bl	uk_ta_piece
	b	.out
.code:	ldr	x24,[x19,UW_TEXT]
	mov	x25,x20				; position
	tst	x0,UTA_CS or UTA_XML
	b.ne	.lex
.tok:	cmp	x25,x21
	b.hs	.out
	ldrb	w0,[x24,x25]
	cmp	w0,' '
	b.eq	.blank
	cmp	w0,9
	b.eq	.blank
	cmp	w0,13
	b.eq	.blank
	ldr	x1,[x19,UW_STYLE]
	tbnz	x1,11,.bcm			; (UTA_BASIC)
	cmp	w0,';'				; commentaire : le reste de la ligne
	b.ne	.str
.cmnt:	mov	x1,x25
	mov	x2,x21
	mov	x25,x21
	mov	x3,UKC_COMMENT
	b	.draw
.bcm:	cmp	w0,0x27				; BASIC : ' ou REM, le reste de la ligne
	b.eq	.cmnt
	orr	w1,w0,0x20
	cmp	w1,'r'
	b.ne	.bstr
	add	x1,x25,3
	cmp	x1,x21
	b.hi	.bstr
	add	x2,x24,x25
	ldrb	w1,[x2,1]
	orr	w1,w1,0x20
	cmp	w1,'e'
	b.ne	.bstr
	ldrb	w1,[x2,2]
	orr	w1,w1,0x20
	cmp	w1,'m'
	b.ne	.bstr
	ldrb	w1,[x2,3]
	cmp	x25,x20				; (un mot : debut de ligne ou blanc avant, blanc apres)
	b.eq	.bafter
	ldrb	w3,[x2,-1]
	cmp	w3,' '
	b.eq	.bafter
	cmp	w3,9
	b.eq	.bafter
	cmp	w3,':'
	b.ne	.bstr
.bafter:add	x3,x25,3
	cmp	x3,x21
	b.eq	.cmnt
	cmp	w1,' '
	b.eq	.cmnt
	cmp	w1,9
	b.eq	.cmnt
.bstr:	ldrb	w0,[x24,x25]
	cmp	w0,'"'
	b.eq	.q
	b	.num
.str:	cmp	w0,0x27				; chaine : jusqu'au meme guillemet
	b.eq	.q
	cmp	w0,'"'
	b.ne	.num
.q:	mov	w4,w0
	add	x26,x25,1
.qs:	cmp	x26,x21
	b.hs	.qd
	ldrb	w0,[x24,x26]
	add	x26,x26,1
	cmp	w0,w4
	b.ne	.qs
.qd:	mov	x1,x25
	mov	x2,x26
	mov	x25,x26
	mov	x3,UKC_STRING
	b	.draw
.num:	sub	w1,w0,'0'			; nombre : chiffres, lettres
	cmp	w1,10
	b.lo	.nr
	cmp	w0,'#'
	b.ne	.word
.nr:	add	x26,x25,1
.ns:	cmp	x26,x21
	b.hs	.nd
	ldrb	w0,[x24,x26]
	bl	uk_identch
	cbz	x0,.nd
	add	x26,x26,1
	b	.ns
.nd:	mov	x1,x25
	mov	x2,x26
	mov	x25,x26
	mov	x3,UKC_NUMBER
	b	.draw
.word:	bl	uk_identch			; mot : registre, mot-cle, etiquette
	cbz	x0,.punct
	mov	x26,x25
.ws:	cmp	x26,x21
	b.hs	.wd
	ldrb	w0,[x24,x26]
	bl	uk_identch
	cbz	x0,.wd
	add	x26,x26,1
	b	.ws
.wd:	add	x0,x24,x25
	sub	x1,x26,x25
	bl	uk_ta_wordkind
	mov	x3,x0
	cmp	x26,x21				; suivi de « : » : une etiquette
	b.hs	.wk
	ldrb	w0,[x24,x26]
	cmp	w0,':'
	b.ne	.wk
	mov	x3,UKC_LABEL
.wk:	mov	x1,x25
	mov	x2,x26
	mov	x25,x26
	b	.draw
.punct:	mov	x1,x25				; ponctuation : un caractere
	add	x25,x25,1
	mov	x2,x25
	mov	x3,UKC_PUNCT
	b	.draw
.blank:	add	x25,x25,1
	b	.tok
.draw:	stp	x1,x2,[sp,-16]!
	mov	x0,x3
	bl	uk_ta_color
	mov	w3,w0
	ldp	x1,x2,[sp],16
	mov	x0,x19
	bl	uk_ta_piece
	b	.tok
.lex:	cmp	x25,x21				; C#, XML : l'etat suit les lignes
	b.hs	.out
	mov	x0,x24
	mov	x1,x25
	mov	x2,x21
	ldr	x3,[x19,UW_TA_LEX]
	ldr	x4,[x19,UW_STYLE]
	bl	uk_lex
	str	x2,[x19,UW_TA_LEX]
	mov	x26,x0
	tbnz	x1,63,.lexb
	cmp	x1,UKC_WORD
	b.ne	.lexc
	add	x0,x24,x25
	sub	x1,x26,x25
	bl	uk_ta_wordkind
	mov	x1,x0
.lexc:	mov	x0,x1
	bl	uk_ta_color
	mov	w3,w0
	mov	x1,x25
	mov	x2,x26
	mov	x0,x19
	bl	uk_ta_piece
.lexb:	mov	x25,x26
	b	.lex
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

UKC_TEXT	= 0
UKC_COMMENT	= 1
UKC_STRING	= 2
UKC_NUMBER	= 3
UKC_REGISTER	= 4
UKC_KEYWORD	= 5
UKC_LABEL	= 6
UKC_PUNCT	= 7
UKC_WORD	= 8			; (uk_lex) un nom, classe par uk_ta_wordkind

; x0 = genre UKC_* -> w0 = sa couleur (jetons du theme)
uk_ta_color:
	prologue
	cmp	x0,UKC_COMMENT
	b.ne	.s
	jet	w0,TJ_TEXTE3
	b	.out
.s:	mov	x2,110				; (melange avec TEXTE1 : lisible sur
	cmp	x0,UKC_STRING			; les deux themes)
	b.ne	.n
	jet	w1,TJ_SUCCES
	b	.mix
.n:	cmp	x0,UKC_NUMBER
	b.ne	.r
	jet	w1,TJ_ALERTE
	mov	x2,140
	b	.mix
.r:	cmp	x0,UKC_REGISTER
	b.ne	.k
	jet	w0,TJ_ACCENT
	b	.out
.k:	cmp	x0,UKC_KEYWORD
	b.ne	.l
	jet	w1,TJ_DANGER
	mov	x2,90
	b	.mix
.l:	cmp	x0,UKC_LABEL
	b.ne	.p
	jet	w0,TJ_ACCENT_FONCE
	b	.out
.p:	cmp	x0,UKC_PUNCT
	b.ne	.t
	jet	w0,TJ_TEXTE2
	b	.out
.t:	jet	w0,TJ_TEXTE1
	b	.out
.mix:	jet	w0,TJ_TEXTE1
	mov	x3,256
	sub	x2,x3,x2
	bl	ui_mix
.out:	epilogue

; w0 = octet -> x0 = 1 s'il fait partie d'un nom (lettre, chiffre, _ . @ $ ?)
uk_identch:
	cmp	w0,0x80
	b.hs	.y
	sub	w1,w0,'0'
	cmp	w1,10
	b.lo	.y
	orr	w1,w0,0x20
	sub	w1,w1,'a'
	cmp	w1,26
	b.lo	.y
	cmp	w0,'_'
	b.eq	.y
	cmp	w0,'.'
	b.eq	.y
	cmp	w0,'@'
	b.eq	.y
	cmp	w0,'$'
	b.eq	.y
	cmp	w0,'?'
	b.eq	.y
	mov	x0,0
	ret
.y:	mov	x0,1
	ret

; x0 = mot, x1 = longueur -> x0 = UKC_REGISTER, UKC_KEYWORD ou UKC_TEXT (le
; widget x19 : sa fonction de mots-cles, ui_textarea_keywords, sinon les
; directives de FASMARM)
uk_ta_wordkind:
	prologue
	stp	x20,x21,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	ldr	x0,[x19,UW_STYLE]
	tst	x0,UTA_BASIC or UTA_CS		; (pas de registres)
	b.ne	.kw
	mov	x0,x20
	bl	uk_is_register
	cbz	x0,.kw
	mov	x0,UKC_REGISTER
	b	.out
.kw:	ldr	x9,[x19,UW_KWFN]
	cbz	x9,.builtin
	mov	x0,x20
	mov	x1,x21
	blr	x9
	b	.res
.builtin:
	ldr	x0,[x19,UW_STYLE]
	tbz	x0,12,.fasm
	mov	x0,x20				; (UTA_CS)
	mov	x1,x21
	bl	uk_cs_word
	b	.res
.fasm:	adr	x2,uk_fasm_words		; mots a la suite, un vide a la fin
.w:	ldrb	w3,[x2]
	cbz	w3,.no
	mov	x4,0
.c:	ldrb	w3,[x2,x4]
	cbz	w3,.end
	cmp	x4,x21
	b.hs	.skip
	ldrb	w5,[x20,x4]
	orr	w5,w5,0x20			; (sans casse)
	cmp	w5,w3
	b.ne	.skip
	add	x4,x4,1
	b	.c
.end:	cmp	x4,x21
	b.eq	.yes
.skip:	ldrb	w3,[x2],1
	cbnz	w3,.skip
	b	.w
.yes:	mov	x0,1
	b	.res
.no:	mov	x0,0
.res:	cmp	x0,0
	mov	x0,UKC_KEYWORD
	mov	x1,UKC_TEXT
	csel	x0,x1,x0,eq
.out:	ldp	x20,x21,[sp],16
	epilogue

; x0 = mot, x1 = longueur -> x0 = 1 si c'est un registre AArch64
uk_is_register:
	cmp	x1,2
	b.lo	.no
	cmp	x1,3
	b.hi	.no
	ldrb	w2,[x0]
	orr	w2,w2,0x20
	ldrb	w3,[x0,1]
	orr	w3,w3,0x20
	cmp	x1,2				; sp, lr, fp
	b.ne	.three
	cmp	w2,'s'
	b.ne	.lr
	cmp	w3,'p'
	b.eq	.yes
.lr:	cmp	w2,'l'
	b.ne	.fp
	cmp	w3,'r'
	b.eq	.yes
.fp:	cmp	w2,'f'
	b.ne	.pre
	cmp	w3,'p'
	b.eq	.yes
	b	.pre
.three:	cmp	w3,'z'				; xzr, wzr
	b.ne	.pre
	ldrb	w4,[x0,2]
	orr	w4,w4,0x20
	cmp	w4,'r'
	b.ne	.no
	cmp	w2,'x'
	b.eq	.yes
	cmp	w2,'w'
	b.eq	.yes
	b	.no
.pre:	adr	x4,uk_reg_prefix		; x0..x30, w, s, d, q, v, b, h
.p:	ldrb	w5,[x4],1
	cbz	w5,.no
	cmp	w5,w2
	b.ne	.p
	mov	x4,1				; chiffres, 0..31
	mov	x5,0
.d:	cmp	x4,x1
	b.hs	.dn
	ldrb	w6,[x0,x4]
	sub	w6,w6,'0'
	cmp	w6,10
	b.hs	.no
	mov	x7,10
	madd	x5,x5,x7,x6
	add	x4,x4,1
	b	.d
.dn:	cmp	x5,31
	b.hi	.no
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

uk_reg_prefix	db 'xwsdqvbh',0
uk_fasm_words	db 'include',0,'macro',0,'segment',0,'db',0,'dh',0,'dw',0,'dd',0,'rb',0
		db 'rh',0,'rw',0,'rd',0,'align',0,'if',0,'else',0,'end',0,'equ',0
		db 'format',0,'entry',0,'virtual',0,'repeat',0,'while',0,'local',0
		db 'struc',0,'common',0,'forward',0,'reverse',0,'times',0,'label',0
		db 'display',0,'err',0,'assert',0,'readable',0,'writeable',0
		db 'executable',0,'at',0,'defined',0,'used',0,'file',0,'match',0
		db 'irp',0,'irps',0,'rept',0,'purge',0,'restore',0,'fix',0,0
		align 4

; x0 = widget, x1 = debut, x2 = fin, w3 = couleur : dessine ces octets a leur
; place, morceau par morceau entre les tabulations (x22 : x du debut du
; texte de la ligne, x20 : debut de la ligne, x23 : y ; ceux de
; uk_ta_paint_line)
uk_ta_piece:
	prologue
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	mov	x24,x1
	mov	x25,x2
	mov	w26,w3
.run:	cmp	x24,x25
	b.hs	.out
	ldr	x0,[x19,UW_TEXT]		; un morceau sans tabulation
	ldrb	w1,[x0,x24]
	cmp	w1,9
	b.ne	.r
	add	x24,x24,1
	b	.run
.r:	mov	x27,x24
.re:	cmp	x27,x25
	b.hs	.rd
	ldrb	w1,[x0,x27]
	cmp	w1,9
	b.eq	.rd
	add	x27,x27,1
	b	.re
.rd:	mov	x0,x19				; x : largeur depuis le debut de la ligne
	mov	x1,x20
	sub	x2,x24,x20
	bl	uk_ta_width
	add	x0,x22,x0
	str	x0,[sp,-16]!
	sub	x2,x27,x24			; le morceau, termine par NUL
	cmp	x2,UK_LINE_MAX-1
	mov	x3,UK_LINE_MAX-1
	csel	x2,x3,x2,hi
	ldr	x1,[x19,UW_TEXT]
	add	x1,x1,x24
	adr	x0,uk_linebuf
	bl	uk_memcpy
	strb	wzr,[x0]
	mov	x0,x19
	bl	uk_ta_font
	mov	x5,x0
	mov	x7,x1
	mov	x3,x2				; hauteur de ligne
	ldr	x1,[sp],16
	adr	x0,uk_linebuf
	mov	x2,x23
	mov	w4,w26
	mov	x6,0
	bl	uk_text_box
	mov	x24,x27
	b	.run
.out:	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Editeur de code : fonctions du programme
; ---------------------------------------------------------------------------

; x0 = texte multiligne, x1 = ligne eclairee (-1 : aucune) : une ligne a
; montrer (erreur, pas a pas)
ui_textarea_highlight:
	prologue
	cbz	x0,.out
	str	x1,[x0,UW_HOT]
	bl	uk_w_refresh
.out:	epilogue

; x0 = texte multiligne, x1 = fonction (x0 = mot, x1 = longueur -> x0 = 1
; mot-cle ; 0 : les directives de FASMARM) : mots-cles de UTA_CODE
ui_textarea_keywords:
	cbz	x0,.ret
	str	x1,[x0,UW_KWFN]
.ret:	ret

; x0 = texte multiligne, x1 = texte : insere au curseur, a la place de la
; selection (une operation pour Annuler) -> x0 = UN_CHANGE
ui_textarea_insert:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cbz	x0,.zero
	mov	x0,x1
	bl	uk_strlen
	mov	x2,x0
	mov	x1,x20
	mov	x0,x19
	bl	uk_tb_insert
	mov	x0,x19
	bl	uk_ta_sync
	mov	x0,x19
	bl	uk_ta_show_caret
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
	b	.out
.zero:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte multiligne, x1 = taille de la police en pixels (0 : celle de
; l'interface)
ui_textarea_font:
	prologue
	cbz	x0,.out
	str	x1,[x0,UW_PX]
	str	x0,[sp,-16]!
	bl	uk_ta_sync
	ldr	x0,[sp],16
	bl	uk_w_refresh
.out:	epilogue

; x0 = texte multiligne, x1 = operation (UTE_UNDO, UTE_REDO, UTE_CUT,
; UTE_COPY, UTE_PASTE, UTE_ALL) -> x0 = UN_CHANGE si le texte a change
; (menus Edition du programme)
ui_textarea_edit:
	prologue
	cbz	x0,.zero
	adr	x2,uk_ute_keys
	sub	x1,x1,1
	cmp	x1,6
	b.hs	.zero
	ldrb	w1,[x2,x1]
	mov	x2,0
	mov	x3,MOD_CTRL
	bl	uk_ta_key
	cmp	x0,0
	csel	x0,xzr,x0,lt
	b	.out
.zero:	mov	x0,0
.out:	epilogue
uk_ute_keys	db 0x1D,0x1C,HID_X,HID_C,HID_V,HID_A	; Z, Y, X, C, V, A
		align 4

; x0 = texte multiligne, x1 = mot (sans casse ASCII) -> x0 = 1 trouve (choisi,
; montre ; apres la selection, puis depuis le debut), 0 sinon
ui_textarea_find:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x20
	bl	uk_strlen
	mov	x21,x0				; longueur du mot
	cbz	x21,.no
	ldr	x22,[x19,UW_TLEN]
	sub	x22,x22,x21			; dernier depart possible
	tbnz	x22,63,.no
	ldr	x0,[x19,UW_VALUE]		; depart : apres la selection
	ldr	x1,[x19,UW_ANCHOR]
	cmp	x0,x1
	csel	x23,x0,x1,hi
	mov	x24,0				; 1 : deja repris au debut
.at:	cmp	x23,x22
	b.ls	.cmp
	cbnz	x24,.no
	mov	x24,1
	mov	x23,0
.cmp:	ldr	x3,[x19,UW_TEXT]
	add	x3,x3,x23
	mov	x4,0
.c:	cmp	x4,x21
	b.hs	.found
	ldrb	w5,[x3,x4]
	ldrb	w6,[x20,x4]
	sub	w7,w5,'A'
	cmp	w7,26
	b.hs	.a
	add	w5,w5,32
.a:	sub	w7,w6,'A'
	cmp	w7,26
	b.hs	.b
	add	w6,w6,32
.b:	cmp	w5,w6
	b.ne	.next
	add	x4,x4,1
	b	.c
.next:	add	x23,x23,1
	b	.at
.found:	str	x23,[x19,UW_ANCHOR]
	add	x0,x23,x21
	str	x0,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_ta_show_caret
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1 = drapeaux de creation : nouveau style (texte multiligne :
; barres, numeros, code ; champ : mot de passe, mono...) ; replace et redessine
ui_w_style:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	str	x1,[x19,UW_STYLE]
	ldr	w0,[x19,UW_TYPE]
	cmp	w0,UT_AREA
	b.ne	.draw
	mov	x0,x19
	bl	uk_ta_bars
	mov	x0,x19
	bl	uk_ta_layout
.draw:	mov	x0,x19
	bl	uk_w_refresh
.out:	ldr	x19,[sp],16
	epilogue
