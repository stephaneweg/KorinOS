; launcher.inc - le lanceur du panel : le bouton « apps » cache les fenetres
; (rideau d'Elegant, WF_CURTAIN) et montre une feuille opaque : les categories
; dans un rail a gauche, une recherche, les applications de la categorie
; choisie. La recherche porte sur la categorie choisie, sans la casse ni les
; accents. Comme un menu, la fenetre capture souris et clavier : un clic hors
; de la feuille (le bouton « apps » compris), ou Echap, la ferme.

LA_SHEET_R	= 14
LA_RAIL_M	= 8				; rail : marge dans la feuille,
LA_RAIL_W	= 160
LA_RAIL_R	= 10
LA_RAIL_IN	= 8				; marge en haut et en bas
LA_ROW_H	= 34				; ligne : pas (moins s'il y en a trop),
LA_ROW_X	= 6				; retrait,
LA_ROW_R	= 8				; rayon de sa boite
LA_GAP		= 12				; entre le rail et la recherche
LA_BOX_Y	= 12				; recherche : haut dans la feuille,
LA_BOX_H	= 38
LA_BOX_R	= 10
LA_BOX_TX	= 30				; le champ commence apres la loupe
LA_GRID_Y	= 11				; sous la recherche
LA_GRID_B	= 12				; sous la grille
LA_ICON_Y	= 11				; icone : haut dans la cellule
LA_HI_X		= 6				; boite de survol : retrait,
LA_HI_Y		= 3
LA_HI_R		= 10
LA_TAB_PX	= 14
LA_LBL_PX	= 13
LA_PAD		= 16
LA_MAXTABS	= CA_MAX + 1
LA_ALL		= 0xFF				; onglet « Toutes »
LA_NORM		= 96				; texte normalise

; les onglets : « Toutes », puis les categories qui ont des applications, par nom
la_cats:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,la_tab
	mov	w1,LA_ALL
	strb	w1,[x0]
	mov	x21,1				; onglets
	mov	x19,0				; categorie
.c:	adr	x0,ca_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	mov	x20,0				; une application montree ?
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.cn
	adr	x0,ap_cat
	ldrb	w0,[x0,x20]
	cmp	x0,x19
	b.ne	.an
	mov	x0,x20
	bl	pan_shell
	cbz	x0,.ins
.an:	add	x20,x20,1
	b	.a
.ins:	adr	x0,la_tab			; insertion, par nom
	strb	w19,[x0,x21]
	mov	x20,x21
	add	x21,x21,1
.j:	cmp	x20,1
	b.ls	.cn
	sub	x22,x20,1
	adr	x0,la_tab
	ldrb	w0,[x0,x22]
	bl	la_cat_name
	str	x0,[sp,-16]!
	adr	x0,la_tab
	ldrb	w0,[x0,x20]
	bl	la_cat_name
	mov	x1,x0
	ldr	x0,[sp],16
	bl	str_ci_cmp
	cmp	x0,0
	b.le	.cn
	adr	x0,la_tab
	ldrb	w1,[x0,x22]
	ldrb	w2,[x0,x20]
	strb	w2,[x0,x22]
	strb	w1,[x0,x20]
	mov	x20,x22
	b	.j
.cn:	add	x19,x19,1
	b	.c
.out:	adr	x0,la_ntab
	str	x21,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = categorie (LA_ALL : toutes) -> x0 = son nom, traduit (ca_nom : celui du
; manifeste, qui sert de cle)
la_cat_name:
	cmp	x0,LA_ALL
	b.eq	.all
	mov	x1,CA_NOM
	adr	x2,ca_nom
	madd	x0,x0,x1,x2
	b	lang_tr
.all:	adr	x0,la_s_toutes
	b	lang_tr

; la disposition : fenetre, feuille, rail, recherche, grille
la_metrics:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	adr	x0,scr				; (la barre du haut a pu venir apres nous)
	bl	ui_screen
	adr	x9,scr
	ldp	x0,x1,[x9]			; ecran
	ldr	x2,[x9,24]			; barre du haut
	adr	x3,cur
	ldr	x3,[x3,C_DH]
	sub	x1,x1,x2
	sub	x1,x1,x3
	adr	x3,la_geom
	stp	x2,x0,[x3]			; y, largeur, hauteur de la fenetre
	str	x1,[x3,16]
	cmp	x0,900				; la feuille : marges, 1000 x 640 au plus
	mov	x4,8
	mov	x5,20
	csel	x4,x4,x5,lo
	sub	x5,x0,x4,lsl 1
	mov	x6,1000
	cmp	x5,x6
	csel	x5,x6,x5,hi
	sub	x6,x1,x4,lsl 1
	mov	x7,640
	cmp	x6,x7
	csel	x6,x7,x6,hi
	sub	x0,x0,x5
	lsr	x0,x0,1
	sub	x1,x1,x6
	lsr	x1,x1,1
	adr	x3,la_cx
	stp	x0,x1,[x3]			; la_cx, la_cy
	stp	x5,x6,[x3,16]			; la_cw, la_ch
	mov	x19,x0
	mov	x20,x1
	mov	x21,x5
	mov	x22,x6
	add	x0,x19,LA_RAIL_M+LA_RAIL_W+LA_GAP ; recherche et grille : meme colonne
	add	x2,x19,x21
	sub	x2,x2,LA_PAD
	sub	x2,x2,x0
	add	x1,x20,LA_BOX_Y
	adr	x3,la_gx
	stp	x0,x2,[x3]			; la_gx, la_gw
	str	x1,[x3,16]			; la_by
	sub	x0,x22,2*(LA_RAIL_M+LA_RAIL_IN)	; les lignes du rail tiennent toutes
	adr	x1,la_ntab
	ldr	x1,[x1]
	udiv	x0,x0,x1
	mov	x1,LA_ROW_H
	cmp	x0,x1
	csel	x0,x1,x0,hi
	adr	x1,la_row
	str	x0,[x1]
	bl	ui_echelle			; cellule selon l'echelle, a la largeur
	adr	x1,la_ech
	add	x0,x1,x0,lsl 2
	ldrb	w2,[x0]
	ldrb	w3,[x0,1]
	ldrb	w4,[x0,2]
	adr	x5,la_icon
	str	x4,[x5]
	adr	x5,la_gw
	ldr	x0,[x5]
	udiv	x4,x0,x2			; colonnes
	cmp	x4,1
	mov	x5,1
	csel	x4,x5,x4,lo
	udiv	x2,x0,x4			; largeur d'une cellule
	add	x23,x20,LA_BOX_Y+LA_BOX_H+LA_GRID_Y ; haut de la grille
	add	x0,x20,x22
	sub	x0,x0,LA_GRID_B
	sub	x0,x0,x23
	udiv	x5,x0,x3			; rangees
	cmp	x5,1
	mov	x6,1
	csel	x5,x6,x5,lo
	adr	x9,la_cols
	stp	x4,x5,[x9]			; la_cols, la_rows
	stp	x2,x3,[x9,16]			; la_cellw, la_cellh
	str	x23,[x9,32]			; la_gy
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source : en minuscules, sans accents (Latin-1 en
; UTF-8), borne a LA_NORM
la_norm:
	mov	x2,LA_NORM-1
	adr	x5,la_fold
.c:	cbz	x2,.end
	ldrb	w3,[x1],1
	cbz	w3,.end
	cmp	w3,0xC3
	b.ne	.a
	ldrb	w3,[x1]
	cbz	w3,.end
	add	x1,x1,1
	and	w3,w3,0x3F
	ldrb	w3,[x5,x3]
	b	.put
.a:	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.put
	add	w3,w3,32
.put:	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c
.end:	strb	wzr,[x0]
	ret

; x0 = texte, x1 = cherche -> x0 = 1 si le texte le contient
la_has:
.i:	mov	x2,0
.j:	ldrb	w3,[x1,x2]
	cbz	w3,.yes
	ldrb	w4,[x0,x2]
	cmp	w3,w4
	b.ne	.nx
	add	x2,x2,1
	b	.j
.nx:	ldrb	w3,[x0],1
	cbnz	w3,.i
	mov	x0,0
	ret
.yes:	mov	x0,1
	ret

; la_res <- les applications de l'onglet choisi que la recherche garde
la_filter:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,la_w_box
	ldr	x0,[x0]
	bl	ui_w_get_text
	mov	x1,x0
	adr	x0,la_q
	bl	la_norm
	adr	x0,la_cur
	ldr	x0,[x0]
	adr	x1,la_tab
	ldrb	w21,[x1,x0]			; x21 : categorie
	mov	x19,0				; par nom
	mov	x20,0				; gardees
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.end
	adr	x0,ap_ordre
	ldrb	w22,[x0,x19]
	add	x19,x19,1
	cmp	w21,LA_ALL
	b.eq	.sh
	adr	x0,ap_cat
	ldrb	w0,[x0,x22]
	cmp	w0,w21
	b.ne	.a
.sh:	mov	x0,x22
	bl	pan_shell
	cbnz	x0,.a
	adr	x0,la_q
	ldrb	w0,[x0]
	cbz	w0,.keep
	adr	x0,la_t				; le libelle, puis le nom du paquet
	mov	x1,x22
	mov	x2,AP_LIB
	adr	x3,ap_lib
	madd	x1,x1,x2,x3
	bl	la_norm
	adr	x0,la_t
	adr	x1,la_q
	bl	la_has
	cbnz	x0,.keep
	adr	x0,ap_nom
	add	x1,x0,x22,lsl 5
	adr	x0,la_t
	bl	la_norm
	adr	x0,la_t
	adr	x1,la_q
	bl	la_has
	cbz	x0,.a
.keep:	adr	x0,la_res
	strb	w22,[x0,x20]
	add	x20,x20,1
	b	.a
.end:	adr	x0,la_n
	str	x20,[x0]
	adr	x0,la_scroll
	str	xzr,[x0]
	adr	x0,la_hover
	mov	x1,-1
	str	x1,[x0]
	adr	x0,la_q				; une recherche : Entree prend la premiere
	ldrb	w0,[x0]
	cmp	w0,0
	csinv	x0,xzr,xzr,ne
	cmp	x20,0
	csinv	x0,x0,xzr,ne
	adr	x1,la_sel
	str	x0,[x1]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = onglet : le choisit
la_tab_set:
	prologue
	adr	x1,la_cur
	str	x0,[x1]
	bl	la_filter
	bl	la_draw
	epilogue

; -> x0 = rangees de la_res
la_total:
	adr	x0,la_n
	ldr	x0,[x0]
	adr	x1,la_cols
	ldr	x1,[x1]
	add	x0,x0,x1
	sub	x0,x0,1
	udiv	x0,x0,x1
	ret

; x0 = rangee du haut voulue : bornee, redessine si elle change
la_scroll_to:
	prologue
	str	x0,[sp,-16]!
	bl	la_total
	ldr	x1,[sp],16
	adr	x2,la_rows
	ldr	x2,[x2]
	subs	x0,x0,x2
	csel	x0,xzr,x0,lt			; x0 : le plus bas
	cmp	x1,x0
	csel	x1,x0,x1,gt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	adr	x2,la_scroll
	ldr	x3,[x2]
	cmp	x3,x1
	b.eq	.out
	str	x1,[x2]
	adr	x0,la_hover			; (la case survolee a change)
	mov	x1,-1
	str	x1,[x0]
	bl	la_draw
.out:	epilogue

; x0 = deplacement : la selection, gardee visible
la_sel_move:
	prologue
	adr	x1,la_n
	ldr	x1,[x1]
	cbz	x1,.out
	adr	x2,la_sel
	ldr	x3,[x2]
	tbz	x3,63,.m
	mov	x3,0				; (rien de choisi : la premiere)
	b	.b
.m:	add	x3,x3,x0
.b:	cmp	x3,0
	csel	x3,xzr,x3,lt
	sub	x4,x1,1
	cmp	x3,x4
	csel	x3,x4,x3,gt
	str	x3,[x2]
	adr	x0,la_cols
	ldp	x4,x5,[x0]			; colonnes, rangees
	udiv	x3,x3,x4			; sa rangee
	adr	x0,la_scroll
	ldr	x1,[x0]
	cmp	x3,x1
	b.lt	.top
	add	x1,x1,x5
	cmp	x3,x1
	b.lo	.draw
	sub	x3,x3,x5
	add	x3,x3,1
.top:	str	x3,[x0]
.draw:	bl	la_draw
.out:	epilogue

; x0 = y dans la barre de defilement : la rangee du haut suit
la_bar_set:
	prologue
	str	x0,[sp,-16]!
	bl	la_total
	ldr	x1,[sp],16
	adr	x2,la_rows
	ldr	x2,[x2]
	subs	x0,x0,x2
	b.le	.out
	adr	x3,la_gy
	ldr	x3,[x3]
	subs	x1,x1,x3
	csel	x1,xzr,x1,lt
	adr	x3,la_cellh
	ldr	x3,[x3]
	mul	x2,x2,x3			; hauteur de la barre
	mul	x0,x0,x1
	add	x0,x0,x2,lsr 1
	udiv	x0,x0,x2
	bl	la_scroll_to
.out:	epilogue

; x0, x1 = point -> x0 = onglet (-1 : aucun)
la_tab_at:
	adr	x9,la_cx
	ldp	x2,x3,[x9]
	add	x2,x2,LA_RAIL_M
	sub	x0,x0,x2
	cmp	x0,LA_RAIL_W
	b.hs	.none
	add	x3,x3,LA_RAIL_M+LA_RAIL_IN
	sub	x1,x1,x3
	adr	x2,la_row
	ldr	x2,[x2]
	udiv	x0,x1,x2
	adr	x2,la_ntab
	ldr	x2,[x2]
	cmp	x0,x2
	b.hs	.none
	ret
.none:	mov	x0,-1
	ret

; x0, x1 = point -> x0 = application dessous, numero dans la_res (-1 : aucune)
la_cell_at:
	adr	x9,la_gx
	ldr	x2,[x9]
	subs	x0,x0,x2
	b.lt	.none
	adr	x9,la_cols
	ldp	x2,x3,[x9]			; colonnes, rangees
	ldp	x4,x5,[x9,16]			; cellule
	ldr	x6,[x9,32]			; haut
	subs	x1,x1,x6
	b.lt	.none
	udiv	x0,x0,x4
	cmp	x0,x2
	b.hs	.none
	udiv	x1,x1,x5
	cmp	x1,x3
	b.hs	.none
	adr	x6,la_scroll
	ldr	x6,[x6]
	add	x1,x1,x6
	madd	x0,x1,x2,x0
	adr	x1,la_n
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.none
	ret
.none:	mov	x0,-1
	ret

; w0 = fond -> w0 = ce fond sous un survol
la_hi:
	prologue
	str	x0,[sp,-16]!
	bl	ui_jetons
	ldr	w1,[x0,TJ_LIGNE]
	ldr	x0,[sp],16
	mov	x2,128
	bl	ui_mix
	epilogue

; x0 = numero dans la_res, x1 = 1 : montrer tout de suite
la_cell_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x26,x1
	tbnz	x19,63,.out
	adr	x0,la_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	adr	x9,la_cols
	ldp	x0,x1,[x9]			; colonnes, rangees
	ldp	x22,x23,[x9,16]			; cellule
	udiv	x2,x19,x0			; rangee
	msub	x3,x2,x0,x19			; colonne
	adr	x4,la_scroll
	ldr	x4,[x4]
	subs	x2,x2,x4
	b.lt	.out
	cmp	x2,x1
	b.hs	.out
	ldr	x4,[x9,32]
	madd	x21,x2,x23,x4			; y
	adr	x4,la_gx
	ldr	x4,[x4]
	madd	x20,x3,x22,x4			; x
	adr	x0,la_res
	ldrb	w25,[x0,x19]			; application
	bl	ui_jetons
	ldr	w4,[x0,TJ_FACE]
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	bl	ui_fill
	adr	x0,la_hover			; survol, selection
	ldr	x0,[x0]
	cmp	x0,x19
	b.eq	.hi
	adr	x0,la_sel
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.icon
.hi:	bl	ui_jetons
	ldr	w0,[x0,TJ_FACE]
	bl	la_hi
	mov	w5,w0
	mov	w6,w0
	add	x0,x20,LA_HI_X
	add	x1,x21,LA_HI_Y
	sub	x2,x22,2*LA_HI_X
	sub	x3,x23,2*LA_HI_Y
	mov	x4,LA_HI_R
	bl	ui_rbox
.icon:	adr	x0,la_icon
	ldr	x24,[x0]
	sub	x0,x22,x24
	add	x1,x20,x0,lsr 1
	add	x2,x21,LA_ICON_Y
	mov	x3,x24
	adr	x0,ap_nom
	add	x0,x0,x25,lsl 5
	bl	ui_icone
	mov	x0,x25				; libelle, centre
	bl	app_label
	mov	x25,x0
	mov	x1,LA_LBL_PX
	adr	x2,ui_font
	bl	ui_text_width
	sub	x2,x22,2*LA_HI_X+4
	cmp	x0,x2
	csel	x0,x2,x0,hi
	sub	x0,x22,x0
	add	x1,x20,x0,lsr 1
	add	x2,x21,x24
	add	x2,x2,LA_ICON_Y+16
	bl	ui_jetons_texte
	mov	x0,x25
	mov	x4,LA_LBL_PX
	adr	x5,ui_font
	sub	x6,x22,2*LA_HI_X+4
	bl	ui_text_fit
	cbz	x26,.out
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	bl	ui_update
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1, x2 gardes -> w3 = couleur du texte
ui_jetons_texte:
	prologue
	stp	x1,x2,[sp,-16]!
	bl	ui_jetons
	ldr	w3,[x0,TJ_TEXTE1]
	ldp	x1,x2,[sp],16
	epilogue

; x0 = onglet, x1 = 1 : montrer tout de suite
la_tab_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x24,x1
	adr	x9,la_cx
	ldp	x20,x21,[x9]
	add	x20,x20,LA_RAIL_M+LA_ROW_X
	add	x21,x21,LA_RAIL_M+LA_RAIL_IN
	adr	x0,la_row
	ldr	x22,[x0]
	madd	x21,x19,x22,x21			; y
	sub	x22,x22,4			; hauteur de la boite
	bl	ui_jetons
	mov	x23,x0
	ldr	w4,[x23,TJ_CREUX]
	mov	x0,x20
	mov	x1,x21
	mov	x2,LA_RAIL_W-2*LA_ROW_X
	mov	x3,x22
	bl	ui_fill
	adr	x0,la_cur
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.other
	ldr	w5,[x23,TJ_ELEVE]		; choisi : boite relevee
	b	.box
.other:	adr	x0,la_tab_hover
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.plain
	ldr	w0,[x23,TJ_CREUX]
	bl	la_hi
	mov	w5,w0
.box:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,LA_RAIL_W-2*LA_ROW_X
	mov	x3,x22
	mov	x4,LA_ROW_R
	bl	ui_rbox
.plain:	adr	x0,la_tab
	ldrb	w0,[x0,x19]
	bl	la_cat_name
	add	x1,x20,12
	add	x2,x21,x22,lsr 1
	add	x2,x2,5
	ldr	w3,[x23,TJ_TEXTE2]
	adr	x5,ui_font
	adr	x4,la_cur
	ldr	x4,[x4]
	cmp	x4,x19
	b.ne	.text
	ldr	w3,[x23,TJ_TEXTE1]
	adr	x5,ui_font_b
.text:	mov	x4,LA_TAB_PX
	mov	x6,LA_RAIL_W-2*LA_ROW_X-24
	bl	ui_text_fit
	cbz	x24,.out
	mov	x0,x20
	mov	x1,x21
	mov	x2,LA_RAIL_W-2*LA_ROW_X
	mov	x3,x22
	bl	ui_update
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la recherche : contour, fond, loupe. Le champ dessine son propre contour de
; rayon 6 : il est plus grand que ce qu'on en montre, voir la_clip_field.
la_box_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x9,la_gx
	ldp	x19,x21,[x9]
	ldr	x20,[x9,16]
	bl	ui_jetons
	mov	x22,x0
	ldr	w5,[x22,TJ_LIGNE_FORTE]
	mov	w6,w5
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,LA_BOX_H
	mov	x4,LA_BOX_R
	bl	ui_rbox
	ldr	w5,[x22,TJ_SURFACE]
	mov	w6,w5
	add	x0,x19,1
	add	x1,x20,1
	sub	x2,x21,2
	mov	x3,LA_BOX_H-2
	mov	x4,LA_BOX_R-1
	bl	ui_rbox
	ldr	w5,[x22,TJ_TEXTE3]		; loupe : anneau, manche
	mov	w6,w5
	add	x0,x19,15
	add	x1,x20,12
	mov	x2,12
	mov	x3,12
	mov	x4,6
	bl	ui_rbox
	ldr	w5,[x22,TJ_SURFACE]
	mov	w6,w5
	add	x0,x19,17
	add	x1,x20,14
	mov	x2,8
	mov	x3,8
	mov	x4,4
	bl	ui_rbox
	mov	x21,0
.h:	add	x0,x19,x21
	add	x0,x0,25
	add	x1,x20,x21
	add	x1,x1,22
	mov	x2,2
	mov	x3,2
	ldr	w4,[x22,TJ_TEXTE3]
	bl	ui_fill
	add	x21,x21,1
	cmp	x21,4
	b.lo	.h
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; decoupe : l'interieur du champ, sans son contour ni son anneau de focus
la_clip_field:
	adr	x9,la_gx
	ldp	x0,x2,[x9]
	ldr	x1,[x9,16]
	add	x0,x0,LA_BOX_TX+7
	add	x1,x1,1
	sub	x2,x2,LA_BOX_TX+16
	mov	x3,LA_BOX_H-2
	b	ui_clip

; ui_w_event, le champ decoupe -> x1 = code UN_*
la_w_event:
	prologue
	bl	la_clip_field
	mov	x0,0
	bl	ui_w_event
	str	x1,[sp,-16]!
	mov	x2,0
	bl	ui_clip
	ldr	x1,[sp],16
	epilogue

; tout le lanceur
la_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x0,0				; hors de la feuille : transparent
	mov	x1,0
	adr	x2,la_geom
	ldr	x3,[x2,16]
	ldr	x2,[x2,8]
	movl	w4,0xFF000000
	bl	ui_fill
	adr	x9,la_cx
	ldp	x19,x20,[x9]
	ldp	x21,x22,[x9,16]
	bl	ui_jetons
	ldr	w5,[x0,TJ_LIGNE_FORTE]		; la feuille : contour, face
	mov	w6,w5
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	mov	x4,LA_SHEET_R
	bl	ui_rbox
	bl	ui_jetons
	ldr	w5,[x0,TJ_FACE]
	mov	w6,w5
	add	x0,x19,1
	add	x1,x20,1
	sub	x2,x21,2
	sub	x3,x22,2
	mov	x4,LA_SHEET_R-1
	bl	ui_rbox
	bl	ui_jetons
	ldr	w5,[x0,TJ_CREUX]		; le rail
	mov	w6,w5
	add	x0,x19,LA_RAIL_M
	add	x1,x20,LA_RAIL_M
	mov	x2,LA_RAIL_W
	sub	x3,x22,2*LA_RAIL_M
	mov	x4,LA_RAIL_R
	bl	ui_rbox
	bl	la_box_draw
	mov	x19,0				; onglets
.t:	adr	x0,la_ntab
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.cells
	mov	x0,x19
	mov	x1,0
	bl	la_tab_draw
	add	x19,x19,1
	b	.t
.cells:	mov	x19,0				; applications
.c:	adr	x0,la_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.bar
	mov	x0,x19
	mov	x1,0
	bl	la_cell_draw
	add	x19,x19,1
	b	.c
.bar:	cbnz	x0,.sb				; (rien ne correspond)
	adr	x0,la_s_aucune
	bl	tr_x0
	mov	x1,14
	adr	x2,ui_font
	bl	ui_text_width
	adr	x9,la_gx
	ldp	x1,x3,[x9]
	sub	x3,x3,x0
	add	x1,x1,x3,lsr 1
	adr	x2,la_gy
	ldr	x2,[x2]
	add	x2,x2,40
	bl	ui_jetons_texte
	adr	x0,la_s_aucune
	bl	tr_x0
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	b	.wid
.sb:	bl	la_total			; barre de defilement
	mov	x19,x0
	adr	x1,la_rows
	ldr	x20,[x1]
	cmp	x19,x20
	b.ls	.wid
	adr	x1,la_cellh
	ldr	x21,[x1]
	mul	x21,x21,x20			; hauteur de la barre
	mul	x22,x21,x20
	udiv	x22,x22,x19			; du curseur
	cmp	x22,24
	mov	x0,24
	csel	x22,x0,x22,lo
	sub	x0,x21,x22
	adr	x1,la_scroll
	ldr	x1,[x1]
	mul	x0,x0,x1
	sub	x1,x19,x20
	udiv	x0,x0,x1
	adr	x1,la_gy
	ldr	x1,[x1]
	add	x1,x1,x0
	adr	x9,la_cx
	ldr	x0,[x9]
	ldr	x2,[x9,16]
	add	x0,x0,x2
	sub	x0,x0,12
	mov	x2,5
	mov	x3,x22
	mov	x4,2
	bl	la_bar_color
	bl	ui_rbox
.wid:	bl	la_clip_field
	mov	x0,0
	bl	ui_w_draw
	mov	x2,0
	bl	ui_clip
	bl	ui_update_all
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0..x4 gardes -> w5, w6 = couleur de la barre de defilement
la_bar_color:
	prologue
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	str	x4,[sp,-16]!
	bl	ui_jetons
	ldr	w5,[x0,TJ_LIGNE_FORTE]
	mov	w6,w5
	ldr	x4,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	epilogue

; x0 = nouvelle case survolee
la_hover_set:
	prologue
	str	x19,[sp,-16]!
	adr	x1,la_hover
	ldr	x19,[x1]
	cmp	x19,x0
	b.eq	.out
	str	x0,[x1]
	mov	x1,1
	bl	la_cell_draw
	mov	x0,x19
	mov	x1,1
	bl	la_cell_draw
.out:	ldr	x19,[sp],16
	epilogue

; x0 = nouvel onglet survole
la_tab_hover_set:
	prologue
	str	x19,[sp,-16]!
	adr	x1,la_tab_hover
	ldr	x19,[x1]
	cmp	x19,x0
	b.eq	.out
	str	x0,[x1]
	tbnz	x0,63,.old
	mov	x1,1
	bl	la_tab_draw
.old:	tbnz	x19,63,.out
	mov	x0,x19
	mov	x1,1
	bl	la_tab_draw
.out:	ldr	x19,[sp],16
	epilogue

; le lanceur, jusqu'a ce qu'il se ferme ; une application choisie est lancee
; (ou sa fenetre ramenee)
launcher:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	la_cats
	bl	la_metrics
	adr	x9,la_geom
	ldp	x1,x2,[x9]			; y, largeur
	ldr	x3,[x9,16]
	mov	x0,0
	adr	x4,la_title
	bl	tr_x4
	movl	x5,WF_FRAMELESS or WF_TOP or WF_NOFOCUS or WF_NOLIST or WF_GRAB or WF_ALPHA or WF_CURTAIN
	bl	ui_window_ex
	tbnz	x0,63,.gone
	adr	x1,la_win
	str	x0,[x1]
	adr	x9,la_gx			; la recherche, le clavier dessus
	ldp	x0,x2,[x9]
	ldr	x1,[x9,16]
	add	x0,x0,LA_BOX_TX
	sub	x2,x2,LA_BOX_TX+2
	mov	x3,LA_BOX_H
	mov	x4,0
	bl	ui_textbox
	adr	x1,la_w_box
	str	x0,[x1]
	bl	ui_w_focus
	adr	x0,la_w_box
	ldr	x0,[x0]
	adr	x1,la_s_cherche
	bl	tr_x1
	bl	ui_textbox_hint
	adr	x0,la_hover
	mov	x1,-1
	str	x1,[x0]
	adr	x0,la_tab_hover
	str	x1,[x0]
	adr	x0,la_press
	str	x1,[x0]
	adr	x0,la_bar_drag
	str	xzr,[x0]
	mov	x0,0
	bl	la_tab_set
	mov	x19,-1				; x19 : l'application choisie
.loop:	mov	x0,1000
	bl	ui_event
	cbz	x0,.loop
	cmp	x0,WEV_SCREEN			; nouvelle resolution : le panel la suivra
	b.eq	.screen
	adr	x9,ui_ev
	ldr	w1,[x9,EV_WIN]
	adr	x2,la_win
	ldr	x2,[x2]
	cmp	x1,x2
	b.ne	.loop				; (le panel attend)
	cmp	w0,WEV_MOUSE
	b.eq	.mouse
	cmp	w0,WEV_WHEEL
	b.eq	.wheel
	cmp	w0,WEV_KEY
	b.ne	.pass
	ldr	w1,[x9,EV_KIND]
	cbz	w1,.pass			; relache
	ldr	w1,[x9,EV_CODE]
	cmp	w1,KEY_ESC
	b.eq	.close
	cmp	w1,KEY_ENTER
	b.eq	.enter
	cmp	w1,KEY_TAB
	b.eq	.tab
	cmp	w1,KEY_DOWN
	b.eq	.down
	cmp	w1,KEY_UP
	b.eq	.up
	cmp	w1,KEY_PGDN
	b.eq	.pgdn
	cmp	w1,KEY_PGUP
	b.eq	.pgup
.pass:	bl	la_w_event
	cmp	x1,UN_CHANGE
	b.ne	.loop
	bl	la_filter
	bl	la_draw
	b	.loop
.screen:
	adr	x0,screen_todo
	mov	x1,1
	str	x1,[x0]
	b	.close
.enter:	adr	x0,la_sel
	ldr	x0,[x0]
	tbnz	x0,63,.loop
	b	.pick
.tab:	adr	x0,la_cur			; onglet suivant
	ldr	x0,[x0]
	add	x0,x0,1
	adr	x1,la_ntab
	ldr	x1,[x1]
	cmp	x0,x1
	csel	x0,xzr,x0,hs
	bl	la_tab_set
	b	.loop
.down:	adr	x0,la_cols
	ldr	x0,[x0]
	bl	la_sel_move
	b	.loop
.up:	adr	x0,la_cols
	ldr	x0,[x0]
	neg	x0,x0
	bl	la_sel_move
	b	.loop
.pgdn:	adr	x0,la_rows
	ldr	x1,[x0]
	adr	x0,la_scroll
	ldr	x0,[x0]
	add	x0,x0,x1
	bl	la_scroll_to
	b	.loop
.pgup:	adr	x0,la_rows
	ldr	x1,[x0]
	adr	x0,la_scroll
	ldr	x0,[x0]
	sub	x0,x0,x1
	bl	la_scroll_to
	b	.loop
.wheel:	ldrsw	x1,[x9,EV_Y]			; (un cran vers le haut : positif)
	adr	x0,la_scroll
	ldr	x0,[x0]
	sub	x0,x0,x1
	bl	la_scroll_to
	b	.loop
.mouse:	ldrsw	x20,[x9,EV_X]
	ldrsw	x21,[x9,EV_Y]
	ldr	w22,[x9,EV_KIND]
	cmp	w22,1
	b.eq	.press
	cmp	w22,2
	b.eq	.rel
	adr	x0,la_bar_drag			; deplacement
	ldr	x0,[x0]
	cbz	x0,.hov
	mov	x0,x21
	bl	la_bar_set
	b	.pass
.hov:	mov	x0,x20
	mov	x1,x21
	bl	la_cell_at
	bl	la_hover_set
	mov	x0,x20
	mov	x1,x21
	bl	la_tab_at
	bl	la_tab_hover_set
	b	.pass
.press:	adr	x9,la_cx			; hors de la feuille : ferme
	ldp	x0,x1,[x9]
	ldp	x2,x3,[x9,16]
	sub	x0,x20,x0
	cmp	x0,x2
	b.hs	.close
	sub	x1,x21,x1
	cmp	x1,x3
	b.hs	.close
	sub	x1,x2,LA_PAD			; barre de defilement
	cmp	x0,x1
	b.lo	.ptab
	adr	x1,la_gy
	ldr	x1,[x1]
	cmp	x21,x1
	b.lt	.ptab
	adr	x0,la_bar_drag
	mov	x1,1
	str	x1,[x0]
	mov	x0,x21
	bl	la_bar_set
	b	.pass
.ptab:	mov	x0,x20
	mov	x1,x21
	bl	la_tab_at
	tbnz	x0,63,.pcell
	bl	la_tab_set
	b	.pass
.pcell:	mov	x0,x20
	mov	x1,x21
	bl	la_cell_at
	adr	x1,la_press
	str	x0,[x1]
	b	.pass
.rel:	adr	x0,la_bar_drag
	str	xzr,[x0]
	adr	x0,la_press
	ldr	x22,[x0]
	mov	x1,-1
	str	x1,[x0]
	tbnz	x22,63,.pass			; (le relache du clic qui a ouvert)
	mov	x0,x20
	mov	x1,x21
	bl	la_cell_at
	cmp	x0,x22
	b.ne	.pass
.pick:	adr	x1,la_res
	ldrb	w19,[x1,x0]
.close:	adr	x0,la_win
	ldr	x0,[x0]
	bl	ui_window_close
	adr	x0,pan_win
	ldr	x0,[x0]
	bl	ui_window_use
	tbnz	x19,63,.gone
	mov	x0,x19
	bl	app_open
.gone:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; cellule (largeur mini, hauteur, icone) par echelle, 4 octets chacune
la_ech:		db 112, 94, 48, 0
		db 112, 94, 48, 0
		db 96, 84, 40, 0
la_fold		db 'aaaaaaaceeeeiiiidnooooo*ouuuuyts'
		db 'aaaaaaaceeeeiiiidnooooo/ouuuuyty'
la_title	db 'Applications',0
la_s_toutes	db 'Toutes',0
la_s_cherche	db 'Rechercher une application',0
la_s_aucune	db 'Aucune application',0
		align 4

macro launcher_data {
		align 8
la_win		dd 0
la_w_box	dd 0
la_geom		rd 3				; y, largeur, hauteur de la fenetre
la_cx		rd 4				; la feuille : x, y, largeur, hauteur
la_gx		dd 0				; recherche et grille : x, largeur,
la_gw		dd 0
la_by		dd 0				; haut de la recherche
la_row		dd LA_ROW_H			; pas du rail
la_cols		dd 1				; grille : colonnes, rangees,
la_rows		dd 1
la_cellw	dd 1				; cellule,
la_cellh	dd 1
la_gy		dd 0				; haut
la_icon		dd 48
la_ntab		dd 0
la_cur		dd 0				; onglet choisi
la_n		dd 0				; applications montrees
la_scroll	dd 0				; rangee du haut
la_sel		dd -1				; Entree, fleches
la_hover	dd -1
la_tab_hover	dd -1
la_press	dd -1				; case de l'appui
la_bar_drag	dd 0
la_tab		rb LA_MAXTABS
la_res		rb AP_MAX
la_q		rb LA_NORM
la_t		rb LA_NORM
}
