; ============================================================================
;  kits/uikit_tabs.inc - UIKit : conteneurs et barres
;
;	UT_PANEL	panneau (Onyx panel.h) : une zone qui contient des widgets
;			(ui_w_set_parent) ; le cacher cache son contenu
;	UT_GROUP	cadre de groupe (Onyx groupbox.h) : un panneau titre
;	UT_SEGMENT	controle segmente (Onyx segmented.cpp, DESIGN 11.4) :
;			boutons radio « en pilules »
;	UT_TABS		onglets (Onyx tabhost.cpp) : le controle segmente en tete
;			et un panneau par page (ui_tabs_page)
;	UT_TABSTRIP	barre d'onglets de documents (Onyx tabstrip.cpp) : titres,
;			croix de fermeture, « + »
;	UT_TOOLBAR	barre d'outils (Onyx toolbar.cpp, DESIGN 11.7), et ses
;			boutons UT_TOOL (glyphe UTF-8, libelle, a bascule)
;
;  Les widgets d'un panneau gardent leurs coordonnees de fenetre ; un
;  panneau cache (ou une page d'onglets) cache tout ce qu'il contient, et se
;  dessine sous son contenu. Elements des controles segmentes et des barres
;  d'onglets : « a|b|c » a la creation, ou ui_list_add / ui_list_get.
; ============================================================================

UK_SEG_PAD	= 24			; segment : texte + 24 (DESIGN 11.4)

; ---------------------------------------------------------------------------
;  Panneau, cadre de groupe
; ---------------------------------------------------------------------------

; x0..x3 = rectangle, x4 = drapeaux (UPN_SURFACE : fond SURFACE et contour)
; -> poignee
ui_panel:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_PANEL
	bl	uk_w_new
	cbz	x0,.out
	str	x19,[x0,UW_STYLE]
.out:	ldr	x19,[sp],16
	epilogue

; x0..x3 = rectangle, x4 = titre -> poignee (un panneau titre)
ui_groupbox:
	prologue
	bl	tr_x4
	str	x19,[sp,-16]!
	mov	x19,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_GROUP
	bl	uk_w_new
	cbz	x0,.out
	mov	x1,x19
	mov	x2,-1
	str	x0,[sp,-16]!
	bl	uk_set_text
	ldr	x0,[sp],16
.out:	ldr	x19,[sp],16
	epilogue

; x0 = widget, x1 = panneau (0 : la fenetre) : le widget passe dans ce
; panneau (montre et cache avec lui)
ui_w_set_parent:
	cbz	x0,.ret
	str	x1,[x0,UW_PARENT]
.ret:	ret

uk_pn_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	w20,[x19,UW_TYPE]
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	ldr	x4,[x19,UW_STYLE]
	cmp	w20,UT_GROUP
	b.eq	.group
	tbnz	x4,0,.surf
	jet	w4,TJ_FACE
	bl	ui_fill
	b	.out
.surf:	mov	x4,6				; SURFACE, contour LIGNE, rayon 6
	jet	w5,TJ_LIGNE
	mov	w6,w5
	bl	ui_rbox
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	add	x0,x0,1
	add	x1,x1,1
	sub	x2,x2,2
	sub	x3,x3,2
	mov	x4,5
	jet	w5,TJ_SURFACE
	mov	w6,w5
	bl	ui_rbox
	b	.out
.group:	jet	w4,TJ_FACE			; cadre : contour LIGNE sous le titre
	bl	ui_fill
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	add	x1,x1,8
	sub	x3,x3,8
	mov	x4,6
	jet	w5,TJ_LIGNE
	mov	w6,w5
	bl	ui_rbox
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	add	x0,x0,1
	add	x1,x1,9
	sub	x2,x2,2
	sub	x3,x3,10
	mov	x4,5
	jet	w5,TJ_FACE
	mov	w6,w5
	bl	ui_rbox
	ldr	x0,[x19,UW_TEXT]		; titre : gras petit, sur le trait
	bl	uk_met
	ldr	x20,[x0,MT_SMALL]
	ldr	x0,[x19,UW_TEXT]
	mov	x1,x20
	str	x0,[sp,-16]!
	bl	uk_bold
	mov	x2,x0
	ldr	x0,[sp],16
	bl	ui_text_width
	add	x2,x0,8
	ldp	w0,w1,[x19,UW_X]
	add	x0,x0,10
	mov	x3,16
	jet	w4,TJ_FACE
	bl	ui_fill
	bl	uk_bold
	mov	x5,x0
	ldr	x0,[x19,UW_TEXT]
	ldp	w1,w2,[x19,UW_X]
	sxtw	x1,w1
	sxtw	x2,w2
	add	x1,x1,14
	mov	x3,16
	jet	w4,TJ_TEXTE2
	mov	x6,0
	mov	x7,x20
	bl	uk_text_box
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Controle segmente
; ---------------------------------------------------------------------------

; x0..x3 = rectangle (largeur 0 : selon les textes ; hauteur 0 : un
; controle), x4 = segments « a|b|c » -> poignee (valeur 0)
ui_segmented:
	prologue
	mov	x5,UT_SEGMENT
	bl	uk_new_items
	epilogue

; x0..x3, x4 = elements, x5 = type -> widget avec ses elements (largeur 0 :
; uk_seg_total ; hauteur 0 : un controle)
uk_new_items:
	prologue
	bl	uk_tr_items
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x4
	mov	x20,x2
	mov	x21,x5
	mov	x22,x0
	mov	x23,x1
	mov	x24,x3
	cbnz	x24,.h
	bl	uk_met
	ldr	x24,[x0,MT_CTL]
.h:	mov	x0,x21
	mov	x1,x22
	mov	x2,x23
	mov	x3,x20
	mov	x4,x24
	bl	uk_w_new
	cbz	x0,.out
	mov	x22,x0
	mov	x1,x19
	bl	uk_items_split
	cbnz	x20,.w				; largeur selon les segments
	mov	x0,x22
	bl	uk_seg_total
	str	w0,[x22,UW_W]
.w:	mov	x0,x22
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1 = element -> x0 = largeur de son segment (texte gras + 24)
uk_seg_w:
	prologue
	ldr	x2,[x0,UW_ITEMS]
	add	x2,x2,x1,lsl 4
	ldr	x0,[x2]
	str	x0,[sp,-16]!
	bl	uk_met
	ldr	x1,[x0,MT_PX]
	bl	uk_bold
	mov	x2,x0
	ldr	x0,[sp],16
	bl	ui_text_width
	add	x0,x0,UK_SEG_PAD
	epilogue

; x0 = widget -> x0 = largeur de tous ses segments (+ 4)
uk_seg_total:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,0
	mov	x21,4
.l:	ldr	x0,[x19,UW_NITEMS]
	cmp	x20,x0
	b.hs	.out
	mov	x0,x19
	mov	x1,x20
	bl	uk_seg_w
	add	x21,x21,x0
	add	x20,x20,1
	b	.l
.out:	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget -> x0 = x du rail (centre dans le widget), x1 = sa largeur
uk_seg_rail:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	uk_seg_total
	mov	x1,x0
	ldr	w2,[x19,UW_W]
	ldr	w0,[x19,UW_X]
	sxtw	x0,w0
	cmp	x1,x2
	b.hs	.out
	sub	x3,x2,x1
	add	x0,x0,x3,lsr 1
.out:	ldr	x19,[sp],16
	epilogue

; x0 = widget, x1 = y, x2 = hauteur : dessine le rail et ses segments (valeur
; = segment actif, HOT = survole)
uk_seg_paint:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	bl	uk_seg_rail
	mov	x20,x0
	mov	x2,x1
	mov	x0,x20				; rail CREUX (Givre) / SURFACE (Noir)
	mov	x1,x21
	mov	x3,x22
	mov	x4,8
	jet	w5,TJ_CREUX
	jet	w6,TJ_SOMBRE
	cbz	w6,.r
	jet	w5,TJ_SURFACE
.r:	mov	w6,w5
	bl	ui_rbox
	add	x23,x20,2			; x du segment
	mov	x24,0
.s:	ldr	x0,[x19,UW_NITEMS]
	cmp	x24,x0
	b.hs	.out
	mov	x0,x19
	mov	x1,x24
	bl	uk_seg_w
	mov	x25,x0
	ldr	x0,[x19,UW_VALUE]
	cmp	x0,x24
	b.ne	.text
	mov	x0,x23				; actif : ombre LIGNE d'un pixel, SURFACE
	add	x1,x21,2
	mov	x2,x25
	sub	x3,x22,3
	mov	x4,6
	jet	w5,TJ_LIGNE
	mov	w6,w5
	bl	ui_rbox
	mov	x0,x23
	add	x1,x21,2
	mov	x2,x25
	sub	x3,x22,4
	mov	x4,6
	jet	w5,TJ_SURFACE
	jet	w6,TJ_SOMBRE
	cbz	w6,.a
	jet	w5,TJ_ELEVE
.a:	mov	w6,w5
	bl	ui_rbox
.text:	jet	w26,TJ_TEXTE2			; texte : TEXTE1 actif ou survole
	ldr	x0,[x19,UW_VALUE]
	cmp	x0,x24
	b.eq	.t1
	ldr	x0,[x19,UW_HOT]
	cmp	x0,x24
	b.ne	.t
.t1:	jet	w26,TJ_TEXTE1
.t:	ldr	x0,[x19,UW_ITEMS]
	add	x0,x0,x24,lsl 4
	ldr	x0,[x0]
	str	x0,[sp,-16]!
	bl	uk_met
	ldr	x7,[x0,MT_PX]
	bl	uk_bold
	mov	x5,x0
	ldr	x0,[sp],16
	add	x1,x23,UK_SEG_PAD/2
	mov	x2,x21
	mov	x3,x22
	mov	w4,w26
	mov	x6,0
	bl	uk_text_box
	add	x23,x23,x25
	add	x24,x24,1
	b	.s
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1 = x local -> x0 = segment sous ce point (-1 : aucun)
uk_seg_at:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	w0,[x19,UW_X]
	sxtw	x0,w0
	add	x20,x1,x0			; x fenetre
	mov	x0,x19
	bl	uk_seg_rail
	add	x21,x0,2
	mov	x22,0
.s:	ldr	x0,[x19,UW_NITEMS]
	cmp	x22,x0
	b.hs	.no
	mov	x0,x19
	mov	x1,x22
	bl	uk_seg_w
	cmp	x20,x21
	b.lt	.no
	add	x21,x21,x0
	cmp	x20,x21
	b.lt	.yes
	add	x22,x22,1
	b	.s
.yes:	mov	x0,x22
	b	.out
.no:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

uk_sg_draw:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	jet	w4,TJ_FACE
	bl	ui_fill
	mov	x0,x19
	ldp	w1,w2,[x19,UW_Y]
	sxtw	x1,w1
	ldr	w2,[x19,UW_H]
	bl	uk_seg_paint
	ldr	x19,[sp],16
	epilogue

; x0 = widget, x1 = segment : le choisit -> x0 = UN_CHANGE s'il change
uk_seg_choose:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	tbnz	x1,63,.zero
	ldr	x2,[x19,UW_NITEMS]
	cmp	x1,x2
	b.hs	.zero
	ldr	x2,[x19,UW_VALUE]
	cmp	x1,x2
	b.eq	.zero
	str	x1,[x19,UW_VALUE]
	ldr	w0,[x19,UW_TYPE]		; onglets : la page
	cmp	w0,UT_TABS
	b.ne	.draw
	mov	x0,x19
	bl	uk_tabs_show
.draw:	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
	b	.out
.zero:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; souris d'un controle segmente (et de l'en-tete des onglets)
uk_sg_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	w0,[x19,UW_TYPE]		; onglets : l'en-tete seulement
	cmp	w0,UT_TABS
	b.ne	.in
	stp	x2,x3,[sp,-16]!
	bl	uk_met
	ldr	x0,[x0,MT_CTL]
	ldp	x2,x3,[sp],16
	cmp	x3,x0
	b.hs	.out0
.in:	mov	x4,-1
	cmp	x20,3
	b.eq	.hot
	mov	x0,x19
	mov	x1,x2
	bl	uk_seg_at
	mov	x4,x0
	cmp	x20,1
	b.ne	.hot
	mov	x1,x4
	mov	x0,x19
	bl	uk_seg_choose
	b	.out
.hot:	ldr	x0,[x19,UW_HOT]
	cmp	x0,x4
	b.eq	.out0
	str	x4,[x19,UW_HOT]
	mov	x0,x19
	bl	uk_w_refresh
.out0:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; clavier : gauche, droite
uk_sg_key:
	ldr	x2,[x0,UW_VALUE]
	cmp	w1,KEY_LEFT
	b.ne	.r
	sub	x1,x2,1
	b	uk_seg_choose
.r:	cmp	w1,KEY_RIGHT
	b.ne	.no
	add	x1,x2,1
	b	uk_seg_choose
.no:	mov	x0,-1
	ret

; ---------------------------------------------------------------------------
;  Onglets (pages)
; ---------------------------------------------------------------------------

; x0..x3 = rectangle, x4 = onglets « a|b|c » -> poignee ; chaque onglet a son
; panneau (ui_tabs_page), sous l'en-tete
ui_tabs:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x5,UT_TABS
	bl	uk_new_items
	cbz	x0,.out
	mov	x19,x0
	bl	uk_met
	ldr	x21,[x0,MT_CTL]
	mov	x20,0				; un panneau par onglet
.p:	ldr	x0,[x19,UW_NITEMS]
	cmp	x20,x0
	b.hs	.done
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	add	x1,x1,x21
	add	x1,x1,8
	sub	x3,x3,x21
	sub	x3,x3,8
	mov	x4,0
	bl	ui_panel
	cbz	x0,.n
	str	x19,[x0,UW_PARENT]
	ldr	x1,[x19,UW_ITEMS]		; donnee de l'onglet : son panneau
	add	x1,x1,x20,lsl 4
	str	x0,[x1,8]
.n:	add	x20,x20,1
	b	.p
.done:	mov	x0,x19
	bl	uk_tabs_show
	mov	x0,x19
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = onglets, x1 = numero -> x0 = panneau de cette page (0 : aucun)
ui_tabs_page:
	cbz	x0,.no
	ldr	x2,[x0,UW_NITEMS]
	cmp	x1,x2
	b.hs	.no
	ldr	x2,[x0,UW_ITEMS]
	add	x2,x2,x1,lsl 4
	ldr	x0,[x2,8]
	ret
.no:	mov	x0,0
	ret

; x0 = onglets : seul le panneau de la page choisie est montre
uk_tabs_show:
	ldr	x1,[x0,UW_NITEMS]
	ldr	x2,[x0,UW_ITEMS]
	ldr	x3,[x0,UW_VALUE]
	mov	x4,0
.l:	cmp	x4,x1
	b.hs	.ret
	add	x5,x2,x4,lsl 4
	ldr	x5,[x5,8]
	cbz	x5,.n
	ldr	w6,[x5,UW_FLAGS]
	orr	w6,w6,UWF_HIDDEN
	cmp	x4,x3
	b.ne	.st
	and	w6,w6,not UWF_HIDDEN
.st:	str	w6,[x5,UW_FLAGS]
.n:	add	x4,x4,1
	b	.l
.ret:	ret

uk_tabs_draw:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldp	w0,w1,[x19,UW_X]		; fond, en-tete (centre)
	ldp	w2,w3,[x19,UW_W]
	jet	w4,TJ_FACE
	bl	ui_fill
	bl	uk_met
	ldr	x2,[x0,MT_CTL]
	ldr	w1,[x19,UW_Y]
	sxtw	x1,w1
	mov	x0,x19
	bl	uk_seg_paint
	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Barre d'onglets de documents
; ---------------------------------------------------------------------------

UK_TS_MIN	= 90
UK_TS_MAX	= 200

; x0..x3 = rectangle, x4 = drapeaux (UTS_CLOSE : croix, UTS_PLUS : « + »)
; -> poignee (onglets : ui_tabstrip_add)
ui_tabstrip:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x4
	mov	x4,0
	mov	x5,UT_TABSTRIP
	bl	uk_new_items
	cbz	x0,.out
	str	x19,[x0,UW_STYLE]
	mov	x1,-1
	str	x1,[x0,UW_VALUE]
	str	x1,[x0,UW_AUX2]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = barre, x1 = titre, x2 = donnee -> x0 = numero de l'onglet (choisi)
ui_tabstrip_add:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	ui_list_add
	tbnz	x0,63,.out
	str	x0,[x19,UW_VALUE]
	str	x0,[sp,8]
	mov	x0,x19
	bl	uk_w_refresh
	ldr	x0,[sp,8]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = barre -> x0 = onglet dont la fermeture est demandee (UN_CLOSE)
ui_tabstrip_closing:
	ldr	x0,[x0,UW_AUX2]
	ret

; x0 = barre -> x0 = largeur d'un onglet (tous la meme, bornee)
uk_ts_tabw:
	ldr	x1,[x0,UW_NITEMS]
	ldr	w2,[x0,UW_W]
	ldr	x3,[x0,UW_STYLE]
	tbz	x3,1,.np
	sub	x2,x2,32			; place du « + »
.np:	mov	x0,UK_TS_MAX
	cbz	x1,.ret
	udiv	x0,x2,x1
	cmp	x0,UK_TS_MAX
	mov	x2,UK_TS_MAX
	csel	x0,x2,x0,hi
	cmp	x0,UK_TS_MIN
	mov	x2,UK_TS_MIN
	csel	x0,x2,x0,lo
.ret:	ret

uk_ts_draw:
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
	mov	x0,x20				; fond CREUX, trait LIGNE en bas
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_CREUX
	bl	ui_fill
	mov	x0,x20
	add	x1,x21,x23
	sub	x1,x1,1
	mov	x2,x22
	mov	x3,1
	jet	w4,TJ_LIGNE
	bl	ui_fill
	mov	x0,x19
	bl	uk_ts_tabw
	mov	x24,x0
	mov	x25,0				; onglet
.t:	ldr	x0,[x19,UW_NITEMS]
	cmp	x25,x0
	b.hs	.plus
	mul	x26,x25,x24
	add	x26,x20,x26			; x de l'onglet
	ldr	x0,[x19,UW_VALUE]
	cmp	x0,x25
	b.ne	.inact
	mov	x0,x26				; actif : SURFACE arrondi en haut, sur le
	add	x1,x21,4			; trait du bas
	mov	x2,x24
	sub	x3,x23,3
	mov	x4,6
	jet	w5,TJ_LIGNE
	mov	w6,w5
	bl	ui_rbox
	add	x0,x26,1
	add	x1,x21,5
	sub	x2,x24,2
	sub	x3,x23,4
	mov	x4,5
	jet	w5,TJ_SURFACE
	mov	w6,w5
	bl	ui_rbox
	jet	w3,TJ_TEXTE1
	b	.title
.inact:	add	x0,x26,x24			; inactif : un separateur a droite
	sub	x0,x0,1
	add	x1,x21,10
	mov	x2,1
	sub	x3,x23,20
	jet	w4,TJ_LIGNE
	bl	ui_fill
	jet	w3,TJ_TEXTE2
	ldr	x0,[x19,UW_HOT]
	cmp	x0,x25
	b.ne	.title
	jet	w3,TJ_TEXTE1
.title:	str	x3,[sp,-16]!
	bl	uk_met
	ldr	x7,[x0,MT_PX]
	bl	uk_font_r
	mov	x5,x0
	ldr	x4,[sp],16
	ldr	x0,[x19,UW_ITEMS]
	add	x0,x0,x25,lsl 4
	ldr	x0,[x0]
	add	x1,x26,12
	add	x2,x21,2
	mov	x3,x23
	sub	x6,x24,24
	ldr	x8,[x19,UW_STYLE]
	tbz	x8,0,.tw
	sub	x6,x6,18
.tw:	bl	uk_text_box
	ldr	x0,[x19,UW_STYLE]		; croix de fermeture
	tbz	x0,0,.next
	adr	x0,uk_s_cross
	add	x1,x26,x24
	sub	x1,x1,22
	add	x2,x21,2
	mov	x3,x23
	jet	w4,TJ_TEXTE3
	str	x0,[sp,-16]!
	str	x1,[sp,8]
	bl	uk_font_r
	mov	x5,x0
	ldr	x0,[sp]
	ldr	x1,[sp,8]
	add	sp,sp,16
	mov	x6,0
	mov	x7,UK_PX
	bl	uk_text_box
.next:	add	x25,x25,1
	b	.t
.plus:	ldr	x0,[x19,UW_STYLE]		; « + »
	tbz	x0,1,.out
	ldr	x0,[x19,UW_NITEMS]
	mul	x1,x0,x24
	add	x1,x20,x1
	add	x1,x1,10
	adr	x0,uk_s_plus
	add	x2,x21,2
	mov	x3,x23
	jet	w4,TJ_TEXTE2
	str	x1,[sp,-16]!
	bl	uk_font_r
	mov	x5,x0
	ldr	x1,[sp],16
	adr	x0,uk_s_plus
	mov	x6,0
	mov	x7,17
	bl	uk_text_box
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris : choisir, fermer (croix), nouveau (« + »), survol ; molette :
; l'onglet voisin
uk_ts_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	uk_ts_tabw
	mov	x22,x0
	mov	x4,-1
	cmp	x20,3
	b.eq	.hot
	udiv	x4,x21,x22			; onglet sous le point
	ldr	x0,[x19,UW_NITEMS]
	cmp	x4,x0
	b.lo	.in
	cmp	x20,1				; apres les onglets : « + » ?
	b.ne	.none
	ldr	x1,[x19,UW_STYLE]
	tbz	x1,1,.none
	mul	x1,x0,x22
	add	x1,x1,32
	cmp	x21,x1
	b.hs	.none
	mov	x0,UN_NEW
	b	.out
.none:	mov	x4,-1
	b	.hot
.in:	cmp	x20,1
	b.ne	.hot
	ldr	x0,[x19,UW_STYLE]		; la croix ?
	tbz	x0,0,.pick
	madd	x1,x4,x22,x22
	sub	x1,x1,26
	cmp	x21,x1
	b.lt	.pick
	str	x4,[x19,UW_AUX2]
	mov	x0,UN_CLOSE
	b	.out
.pick:	ldr	x0,[x19,UW_VALUE]
	cmp	x0,x4
	b.eq	.z
	str	x4,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
	b	.out
.hot:	ldr	x0,[x19,UW_HOT]
	cmp	x0,x4
	b.eq	.z
	str	x4,[x19,UW_HOT]
	mov	x0,x19
	bl	uk_w_refresh
.z:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

uk_ts_wheel:
	ldr	x2,[x0,UW_VALUE]
	sub	x1,x2,x1
	ldr	x2,[x0,UW_NITEMS]
	cmp	x1,x2
	b.hs	.z
	str	x1,[x0,UW_VALUE]
	str	x30,[sp,-16]!
	bl	uk_w_refresh
	ldr	x30,[sp],16
	mov	x0,UN_CHANGE
	ret
.z:	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  Barre d'outils
; ---------------------------------------------------------------------------

; x0..x3 = rectangle (hauteur 0 : controle + 16) -> poignee (boutons :
; ui_toolbar_add)
ui_toolbar:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x3
	cbnz	x19,.h
	stp	x0,x1,[sp,-16]!
	str	x2,[sp,-16]!
	bl	uk_met
	ldr	x19,[x0,MT_CTL]
	add	x19,x19,16
	ldr	x2,[sp],16
	ldp	x0,x1,[sp],16
.h:	mov	x4,x19
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_TOOLBAR
	bl	uk_w_new
	cbz	x0,.out
	mov	x1,8
	str	x1,[x0,UW_POS]			; x du prochain bouton (depuis le bord)
.out:	ldr	x19,[sp],16
	epilogue

; x0 = barre, x1 = glyphe UTF-8 (0 : aucun), x2 = libelle (0 : aucun), x3 =
; drapeaux (UTB_TOGGLE, UTB_SEP : un espace avant) -> poignee du bouton (clic :
; UN_ACTIVATE ; a bascule : sa valeur 0 / 1 change avant)
ui_toolbar_add:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	tbz	x22,1,.nosep			; separateur : un trait, de l'espace
	ldr	x0,[x19,UW_POS]
	add	x0,x0,12
	str	x0,[x19,UW_POS]
.nosep:	bl	uk_met
	ldr	x24,[x0,MT_CTL]			; hauteur
	mov	x23,x24				; largeur : carre, ou glyphe + libelle
	cbz	x21,.w
	ldr	x1,[x0,MT_PX]
	mov	x0,x21
	str	x1,[sp,-16]!
	bl	uk_font_r
	mov	x2,x0
	ldr	x1,[sp],16
	mov	x0,x21
	bl	ui_text_width
	add	x23,x0,20
	cbz	x20,.w
	add	x23,x23,20
.w:	ldp	w1,w2,[x19,UW_X]
	ldr	x0,[x19,UW_POS]
	add	x1,x1,x0
	ldr	w3,[x19,UW_H]
	sub	x3,x3,x24
	add	x2,x2,x3,lsr 1
	add	x0,x0,x23
	add	x0,x0,2
	str	x0,[x19,UW_POS]
	mov	x0,UT_TOOL
	mov	x3,x23
	mov	x4,x24
	bl	uk_w_new
	cbz	x0,.out
	mov	x23,x0
	str	x19,[x23,UW_PARENT]
	str	x22,[x23,UW_STYLE]
	cbz	x20,.ng				; glyphe : AUX2 ; libelle : le texte
	mov	x0,x20
	bl	uk_strdup
	str	x0,[x23,UW_AUX2]
.ng:	mov	x0,x23
	mov	x1,x21
	mov	x2,-1
	bl	uk_set_text
	mov	x0,x23
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les caracteres des barres d'outils des programmes -> glyphes de tools/icons (art.py) : point de code, nom
		align 4
uk_tool_map:
	dh 0x2610
	db 'glyph-tb-new',0
	times 22-13 db 0
	dh 0x21E7
	db 'glyph-tb-open',0
	times 22-14 db 0
	dh 0x21E9
	db 'glyph-tb-save',0
	times 22-14 db 0
	dh 0x21B6
	db 'glyph-tb-undo',0
	times 22-14 db 0
	dh 0x21B7
	db 'glyph-tb-redo',0
	times 22-14 db 0
	dh 0x229E
	db 'glyph-tb-copy',0
	times 22-14 db 0
	dh 0x2750
	db 'glyph-tb-copy',0
	times 22-14 db 0
	dh 0x2702
	db 'glyph-tb-cut',0
	times 22-13 db 0
	dh 0x25A3
	db 'glyph-tb-paste',0
	times 22-15 db 0
	dh 0x25A4
	db 'glyph-tb-paste',0
	times 22-15 db 0
	dh 0x2315
	db 'glyph-tb-find',0
	times 22-14 db 0
	dh 0x25A6
	db 'glyph-tb-table',0
	times 22-15 db 0
	dh 0x25A8
	db 'glyph-tb-image',0
	times 22-15 db 0
	dh 0x2212
	db 'glyph-tb-minus',0
	times 22-15 db 0
	dh 0x002B
	db 'glyph-tb-plus',0
	times 22-14 db 0
	dh 0x2715
	db 'glyph-tb-cross',0
	times 22-15 db 0
	dh 0x2717
	db 'glyph-tb-cross',0
	times 22-15 db 0
	dh 0x2713
	db 'glyph-tb-check',0
	times 22-15 db 0
	dh 0x21E4
	db 'glyph-tb-alignl',0
	times 22-16 db 0
	dh 0x2194
	db 'glyph-tb-alignc',0
	times 22-16 db 0
	dh 0x21E5
	db 'glyph-tb-alignr',0
	times 22-16 db 0
	dh 0x2261
	db 'glyph-tb-alignj',0
	times 22-16 db 0
	dh 0x2022
	db 'glyph-tb-bullets',0
	times 22-17 db 0
	dh 0x21A1
	db 'glyph-tb-pagebreak',0
	times 22-19 db 0
	dh 0x2191
	db 'glyph-tb-up',0
	times 22-12 db 0
	dh 0x2193
	db 'glyph-tb-down',0
	times 22-14 db 0
	dh 0x2190
	db 'glyph-tb-left',0
	times 22-14 db 0
	dh 0x2192
	db 'glyph-tb-right',0
	times 22-15 db 0
	dh 0x2197
	db 'glyph-tb-arrow',0
	times 22-15 db 0
	dh 0x25BD
	db 'glyph-tb-filter',0
	times 22-16 db 0
	dh 0x25D5
	db 'glyph-tb-chart',0
	times 22-15 db 0
	dh 0x25D4
	db 'glyph-tb-chart',0
	times 22-15 db 0
	dh 0x21A9
	db 'glyph-tb-wrap',0
	times 22-14 db 0
	dh 0x21D4
	db 'glyph-tb-merge',0
	times 22-15 db 0
	dh 0x256C
	db 'glyph-tb-freeze',0
	times 22-16 db 0
	dh 0x25A5
	db 'glyph-tb-layout',0
	times 22-16 db 0
	dh 0x25C6
	db 'glyph-tb-shape',0
	times 22-15 db 0
	dh 0x2195
	db 'glyph-tb-spacing',0
	times 22-17 db 0
	dh 0x21C8
	db 'glyph-tb-front',0
	times 22-15 db 0
	dh 0x21CA
	db 'glyph-tb-back',0
	times 22-14 db 0
	dh 0x25B6
	db 'glyph-tb-play',0
	times 22-14 db 0
	dh 0
		align 4

; x0 = glyphe UTF-8 d'un outil -> x0 = nom de son image, 0 : du texte
uk_tool_glyph:
	prologue
	bl	ttf_utf8_next
	cbz	x0,.non
	ldrb	w1,[x1]				; un seul caractere
	cbnz	w1,.non
	adr	x1,uk_tool_map
.c:	ldrh	w2,[x1]
	cbz	w2,.non
	cmp	x2,x0
	b.eq	.oui
	add	x1,x1,24
	b	.c
.oui:	add	x0,x1,2
	b	.out
.non:	mov	x0,0
.out:	epilogue

uk_tbar_draw:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldp	w0,w1,[x19,UW_X]		; FACE, trait LIGNE dessous
	ldp	w2,w3,[x19,UW_W]
	jet	w4,TJ_FACE
	bl	ui_fill
	ldr	x0,[x19,UW_STYLE]		; (UTBS_NOLINE : sans le trait)
	tbnz	x0,0,.out
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	add	x1,x1,x3
	sub	x1,x1,1
	mov	x3,1
	jet	w4,TJ_LIGNE
	bl	ui_fill
.out:	ldr	x19,[sp],16
	epilogue

uk_tool_draw:
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
	ldr	w24,[x19,UW_FLAGS]
	mov	x0,x19
	bl	uk_w_enabled
	cbnz	x0,.en
	orr	w24,w24,UWF_DISABLED
.en:	jet	w5,TJ_CREUX			; fantome : survol CREUX, appui plus
	tbz	w24,3,.np			; fonce ; a bascule allume : ACCENT_CLAIR
	jet	w0,TJ_CREUX
	mov	x1,116
	bl	ui_tone
	mov	w5,w0
	b	.box
.np:	ldr	x0,[x19,UW_VALUE]
	cbz	x0,.nv
	jet	w5,TJ_ACCENT_CLAIR
	b	.box
.nv:	tbz	w24,2,.ink
.box:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	mov	x4,8
	bl	ui_rbox
.ink:	jet	w24,TJ_TEXTE1
	mov	x0,x19
	bl	uk_w_enabled
	cbnz	x0,.g
	jet	w24,TJ_TEXTE3
.g:	ldr	x0,[x19,UW_AUX2]		; glyphe : image, sinon texte de 17 px
	cbz	x0,.lab
	bl	uk_tool_glyph
	cbz	x0,.gt
	add	x1,x20,10			; (avec un libelle : a gauche, seul : centre)
	ldr	x2,[x19,UW_TLEN]
	cbnz	x2,.gi
	sub	x1,x22,18
	add	x1,x20,x1,lsr 1
.gi:	sub	x2,x23,18
	add	x2,x21,x2,lsr 1
	mov	x3,18
	mov	w4,w24
	bl	ui_glyph
	b	.lab
.gt:	ldr	x0,[x19,UW_AUX2]
	mov	x1,x20
	ldr	x2,[x19,UW_TLEN]		; seul : centre
	cbnz	x2,.gl
	mov	x1,17
	str	x0,[sp,-16]!
	bl	uk_font_r
	mov	x2,x0
	ldr	x0,[sp],16
	bl	ui_text_width
	sub	x1,x22,x0
	add	x1,x20,x1,lsr 1
	ldr	x0,[x19,UW_AUX2]
	b	.gd
.gl:	add	x1,x20,10
.gd:	str	x1,[sp,-16]!
	bl	uk_font_r
	mov	x5,x0
	ldr	x1,[sp],16
	ldr	x0,[x19,UW_AUX2]
	mov	x2,x21
	mov	x3,x23
	mov	w4,w24
	mov	x6,0
	mov	x7,17
	bl	uk_text_box
.lab:	ldr	x0,[x19,UW_TLEN]		; libelle
	cbz	x0,.out
	add	x1,x20,10
	ldr	x0,[x19,UW_AUX2]
	cbz	x0,.lx
	add	x1,x20,30
.lx:	str	x1,[sp,-16]!
	bl	uk_met
	ldr	x7,[x0,MT_PX]
	bl	uk_font_r
	mov	x5,x0
	ldr	x1,[sp],16
	ldr	x0,[x19,UW_TEXT]
	mov	x2,x21
	mov	x3,x23
	mov	w4,w24
	mov	x6,0
	bl	uk_text_box
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris : comme un bouton ; a bascule : la valeur change au clic
uk_tool_mouse:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	uk_bt_mouse
	cmp	x0,UN_ACTIVATE
	b.ne	.out
	ldr	x1,[x19,UW_STYLE]
	tbz	x1,0,.out
	ldr	x1,[x19,UW_VALUE]
	eor	x1,x1,1
	str	x1,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_ACTIVATE
.out:	ldr	x19,[sp],16
	epilogue
