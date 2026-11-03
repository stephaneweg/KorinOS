; ============================================================================
;  kits/uikit_window.inc - UIKit : les fenetres du programme, fenetre
;  redimensionnable (WF_RESIZABLE)
;
;  Plusieurs fenetres par programme : la poignee d'une fenetre est son id
;  chez Elegant (ui_win, UW_WIN, EV_WIN). Les fonctions du kit travaillent sur
;  la fenetre courante, dont l'etat est dans les variables du programme
;  (UV_WIN...) et du kit (uk_dmg, uk_focus...) ; celui des autres fenetres
;  attend dans leur bloc (WB_*), et ui_window_use echange. Un programme a une
;  fenetre n'echange donc jamais rien.
;
;	ui_window / ui_window_ex	une fenetre de plus, elle devient la courante
;	ui_window_use(fenetre)		-> celle d'avant : dessins et widgets y vont
;	ui_window_close(fenetre)	widgets detruits, fenetre fermee
;
;  Une seule boucle : ui_event rend les evenements de toutes les fenetres
;  (EV_WIN) ; ui_w_event rend courante, le temps de l'evenement et des
;  fonctions qu'il appelle, la fenetre de l'evenement, puis rend celle d'avant.
;  WEV_CLOSE reste au programme : ui_window_close, et il finit quand
;  ui_window_count vaut 0. Redessiner un widget ou lui donner le focus marche
;  quelle que soit la fenetre courante.
;
;  Elegant envoie WEV_RESIZE (bord droit / bas glisse, bouton agrandir) ;
;  ui_w_event y repond seul : ui_resize (nouveau tampon partage, cadre
;  redessine, fond FACE), puis la fonction de ui_on_resize (x0 = largeur,
;  x1 = hauteur : le programme replace ses widgets, ui_w_move, et redessine) ;
;  sans fonction, les widgets sont redessines tels quels. Un programme qui
;  n'utilise pas ui_w_event appelle ui_resize lui-meme. Taille minimale :
;  ui_min_size.
; ============================================================================

; --- bloc d'une fenetre (interne au kit) -------------------------------------
WB_NEXT		= 0
WB_ID		= 8			; poignee (0 : bloc libre)
WB_VARS		= 16			; UV_WIN, UV_SURF, UV_W, UV_H
WB_DMG		= 48			; uk_dmg .. uk_flags
WB_FOCUS	= 104			; uk_focus, uk_capture, uk_hover
WB_ACT		= 128			; uk_winact, uk_minw, uk_minh, uk_resize_fn
WB_SHAPE	= 160
WB_CLIP		= 168			; uk_clip
WB_CLIP_ON	= 200
WB_MENU		= 208			; uk_menu_src
WB_MENU_P	= 216			; uk_menu_p
WB_DLG		= 224			; uk_win_dlg
WB_TITLE	= 232			; uk_title
WB_SIZE		= 360

; ce qui passe de la fenetre courante a son bloc, et l'inverse : decalage de
; la variable depuis cette table, place dans le bloc, octets
macro uk_wb_row v*, o*, n* {
	dw	v - uk_wb_tab, o, n
}
uk_wb_tab:
	uk_wb_row uk_dmg, WB_DMG, 56
	uk_wb_row uk_focus, WB_FOCUS, 24
	uk_wb_row uk_winact, WB_ACT, 32
	uk_wb_row uk_cur_shape, WB_SHAPE, 8
	uk_wb_row uk_clip, WB_CLIP, 32
	uk_wb_row uk_clip_on, WB_CLIP_ON, 8
	uk_wb_row uk_menu_src, WB_MENU, 8
	uk_wb_row uk_menu_p, WB_MENU_P, 8
	uk_wb_row uk_win_dlg, WB_DLG, 8
	uk_wb_row uk_title, WB_TITLE, 128
	dw	0

; x0 = bloc, x1 = 0 : l'etat de la fenetre courante y va ; 1 : il en vient
; (feuille)
uk_wb_xfer:
	adr	x2,uk_vars
	ldr	x2,[x2]
	add	x3,x0,WB_VARS
	cbnz	x1,.vl
	ldp	x4,x5,[x2,UV_WIN]
	stp	x4,x5,[x3]
	ldp	x4,x5,[x2,UV_W]
	stp	x4,x5,[x3,16]
	b	.tab
.vl:	ldp	x4,x5,[x3]
	stp	x4,x5,[x2,UV_WIN]
	ldp	x4,x5,[x3,16]
	stp	x4,x5,[x2,UV_W]
.tab:	adr	x2,uk_wb_tab
	mov	x3,x2
.r:	ldrsw	x4,[x3]
	cbz	x4,.ret
	add	x4,x2,x4			; la variable
	ldr	w5,[x3,4]
	add	x5,x0,x5			; sa place
	ldr	w6,[x3,8]
.c:	cbz	x1,.sv
	ldr	x7,[x5],8
	str	x7,[x4],8
	b	.nx
.sv:	ldr	x7,[x4],8
	str	x7,[x5],8
.nx:	subs	x6,x6,8
	b.ne	.c
	add	x3,x3,12
	b	.r
.ret:	ret

; x0 = poignee -> x0 = son bloc (0 : aucun) (feuille)
uk_wb_find:
	adr	x1,uk_wins
	ldr	x1,[x1]
.l:	cbz	x1,.no
	ldr	x2,[x1,WB_ID]
	cmp	x2,x0
	b.eq	.yes
	ldr	x1,[x1,WB_NEXT]
	b	.l
.yes:	mov	x0,x1
	ret
.no:	mov	x0,0
	ret

; -> x0 = bloc d'une fenetre de plus, devenue la courante (0 : plus de
; memoire). La premiere garde les variables telles qu'elles sont (ui_min_size,
; ui_on_resize ont pu etre appeles avant ui_window) ; une autre part de zero.
uk_wb_new:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,uk_win0
	ldr	x0,[x19,WB_ID]
	cbz	x0,.got
	mov	x0,WB_SIZE
	bl	uk_alloc
	mov	x19,x0
	cbz	x0,.out
.got:	adr	x20,uk_cur
	ldr	x0,[x20]
	cbz	x0,.link
	mov	x1,0				; la fenetre d'avant va dans son bloc
	bl	uk_wb_xfer
	adr	x0,uk_focus
	stp	xzr,xzr,[x0]
	str	xzr,[x0,16]
	adr	x0,uk_winact
	mov	x1,1
	mov	x2,120
	stp	x1,x2,[x0]
	mov	x1,80
	stp	x1,xzr,[x0,16]
	adr	x0,uk_cur_shape
	mov	x1,-1
	str	x1,[x0]
	adr	x0,uk_clip_on
	str	xzr,[x0]
	adr	x0,uk_clip_sp
	str	xzr,[x0]
	adr	x0,uk_menu_src
	str	xzr,[x0]
	adr	x0,uk_menu_p
	mov	x1,-1
	str	x1,[x0]
	adr	x0,uk_win_dlg
	str	xzr,[x0]
.link:	adr	x0,uk_wins
	ldr	x1,[x0]
	str	x1,[x19,WB_NEXT]
	str	x19,[x0]
	str	x19,[x20]
.out:	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; (fenetre courante fermee) retire son bloc ; la derniere ouverte des autres
; devient la courante
uk_wb_drop:
	prologue
	str	x19,[sp,-16]!
	adr	x0,uk_cur
	ldr	x19,[x0]
	cbz	x19,.out
	adr	x0,uk_wins			; hors de la liste
.f:	ldr	x1,[x0]
	cbz	x1,.gone
	cmp	x1,x19
	b.eq	.un
	add	x0,x1,WB_NEXT
	b	.f
.un:	ldr	x1,[x19,WB_NEXT]
	str	x1,[x0]
.gone:	adr	x0,uk_menu_p			; (memoire des menus)
	ldr	x0,[x0]
	add	x1,x0,1
	cmp	x1,1
	b.ls	.nomenu
	bl	uk_free
.nomenu:
	str	xzr,[x19,WB_ID]
	adr	x0,uk_win0
	cmp	x0,x19
	b.eq	.next
	mov	x0,x19
	bl	uk_free
.next:	adr	x0,uk_wins
	ldr	x0,[x0]
	adr	x1,uk_cur
	str	x0,[x1]
	cbz	x0,.none
	mov	x1,1
	bl	uk_wb_xfer
	b	.out
.none:	adr	x0,uk_dmg			; plus de fenetre
	str	xzr,[x0]
	adr	x0,uk_menu_p
	str	xzr,[x0]
.out:	ldr	x19,[sp],16
	epilogue

UK_DRAG_H	= 36				; haut du contenu qui deplace la fenetre
UK_INTENT_H	= 32				; sur un bouton de ce haut : l'appui attend,
UK_INTENT_PX	= 8				; cet ecart : la fenetre se deplace

; depuis un appui dans le contenu, bouton tenu : la fenetre courante suit la
; souris, comme sur une barre de titre -> x0 = 0 (< 0 : pas d'appui en cours)
ui_window_drag:
	prologue
	bl	uk_win
	adr	x1,uk_msg
	str	x0,[x1,WM_A]
	mov	x0,WS_OP_WIN_DRAG
	bl	uk_call
	epilogue

; x0 = fenetre (poignee) -> x0 = la fenetre courante d'avant (0 : aucune, ou
; poignee inconnue : rien n'a change). Les dessins et les widgets crees vont
; dans cette fenetre.
ui_window_use:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	uk_wb_find
	mov	x19,x0
	cbz	x19,.out
	mov	x20,0				; la courante d'avant
	adr	x0,uk_cur
	ldr	x0,[x0]
	cbz	x0,.load
	ldr	x20,[x0,WB_ID]
	cmp	x0,x19
	b.eq	.same
	mov	x1,0
	bl	uk_wb_xfer
.load:	mov	x0,x19
	mov	x1,1
	bl	uk_wb_xfer
	adr	x0,uk_cur
	str	x19,[x0]
.same:	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = poignee (EV_WIN, UW_WIN) -> x0 = fenetre a rendre apres (ui_window_use) ;
; 0 si c'est deja la courante, ou une fenetre sans bloc
uk_route:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	uk_win
	cmp	x0,x19
	b.eq	.no
	mov	x0,x19
	bl	ui_window_use
	b	.out
.no:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = fenetre (0 : la courante) : ses widgets detruits, fermee ; la fenetre
; courante le reste, sinon la derniere ouverte le devient -> x0 = 0 (E_BADF :
; poignee inconnue)
ui_window_close:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbnz	x19,.id
	bl	uk_win
	mov	x19,x0
.id:	mov	x0,x19
	bl	uk_wb_find
	cbnz	x0,.ok
	mov	x0,E_BADF
	b	.out
.ok:	mov	x0,x19
	bl	uk_route
	mov	x20,x0
	bl	uk_tip_hide
	mov	x0,x19
	bl	uk_w_destroy_win
	bl	ui_close
	cbz	x20,.done
	mov	x0,x20
	bl	ui_window_use
.done:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = nombre de fenetres ouvertes du programme (feuille)
ui_window_count:
	adr	x1,uk_wins
	ldr	x1,[x1]
	mov	x0,0
.l:	cbz	x1,.ret
	add	x0,x0,1
	ldr	x1,[x1,WB_NEXT]
	b	.l
.ret:	ret

; x0 = widget -> x0 = sa fenetre
ui_w_window:
	cbz	x0,.ret
	ldr	x0,[x0,UW_WIN]
.ret:	ret

; x0 = fenetre : detruit ses widgets (avec leurs enfants)
uk_w_destroy_win:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
.d:	adr	x20,uk_wfirst
	ldr	x20,[x20]
.l:	cbz	x20,.out
	ldr	x0,[x20,UW_WIN]
	cmp	x0,x19
	b.ne	.n
	ldr	x0,[x20,UW_PARENT]		; (un enfant part avec son parent)
	cbnz	x0,.n
	mov	x0,x20
	bl	ui_w_destroy
	b	.d
.n:	ldr	x20,[x20,UW_NEXT]
	b	.l
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = largeur, x1 = hauteur (bornees a la taille minimale) : la fenetre
; courante change de taille -> x0 = 0 (< 0 : erreur)
ui_resize:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x2,uk_minw
	ldp	x2,x3,[x2]
	cmp	x0,x2
	csel	x0,x2,x0,lt
	cmp	x1,x3
	csel	x1,x3,x1,lt
	mov	x19,x0
	mov	x20,x1
	bl	uk_win
	adr	x9,uk_msg
	str	x0,[x9,WM_A]
	stp	x19,x20,[x9,WM_B]
	mov	x0,WS_OP_RESIZE
	bl	uk_call
	tbnz	x0,63,.out
	adr	x0,uk_buf			; l'ancien tampon rendu, le nouveau mappe
	ldr	x0,[x0]
	sys	SYS_BUF_FREE
	adr	x9,uk_msg
	ldr	x0,[x9,WM_A]
	adr	x1,uk_buf
	str	x0,[x1]
	sys	SYS_BUF_MAP
	tbnz	x0,63,.out
	mov	x21,x0
	adr	x1,uk_vars
	ldr	x1,[x1]
	str	x21,[x1,UV_SURF]
	stp	x19,x20,[x1,UV_W]
	adr	x9,uk_msg
	ldr	x0,[x9,WM_D]			; en-tete WD_*
	add	x0,x21,x0
	adr	x1,uk_dmg
	str	x0,[x1]
	ldp	x1,x2,[x9,WM_B]			; cadres
	adr	x3,uk_chr_a
	cbz	x1,.nochr
	add	x1,x21,x1
	add	x2,x21,x2
.nochr:	stp	x1,x2,[x3]
	add	x0,x19,2*FRAME_BORDER
	add	x1,x20,FRAME_TITLE_H+FRAME_BORDER
	adr	x2,uk_ow
	stp	x0,x1,[x2]
	adr	x0,uk_clip_on			; (plus de decoupe)
	str	xzr,[x0]
	mov	x0,0
	mov	x1,0
	mov	x2,x19
	mov	x3,x20
	jet	w4,TJ_FACE
	bl	ui_fill
	bl	uk_draw_chrome
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 0 : la fenetre (WF_RESIZABLE) agrandie, ou rendue a sa taille d'avant,
; comme le bouton de son cadre ; WEV_RESIZE suit
ui_maximize:
	prologue
	bl	uk_win
	adr	x9,uk_msg
	str	x0,[x9,WM_A]
	mov	x0,WS_OP_MAXIMIZE
	bl	uk_call
	epilogue

; x0 = largeur, x1 = hauteur minimales de la fenetre (redimensionnable)
ui_min_size:
	adr	x2,uk_minw
	stp	x0,x1,[x2]
	ret

; x0 = fonction (x0 = largeur, x1 = hauteur ; 0 : aucune), appelee par
; ui_w_event apres un changement de taille
ui_on_resize:
	adr	x1,uk_resize_fn
	str	x0,[x1]
	ret

; (ui_w_event) x19 = evenement WEV_RESIZE de la fenetre courante
uk_on_resize_ev:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	w0,[x19,EV_X]
	ldr	w1,[x19,EV_Y]
	bl	ui_resize
	tbnz	x0,63,.out
	adr	x0,uk_vars
	ldr	x0,[x0]
	ldp	x0,x1,[x0,UV_W]
	adr	x9,uk_resize_fn
	ldr	x9,[x9]
	cbz	x9,.draw
	blr	x9
	b	.out
.draw:	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Separateur de volets (UT_SPLIT, Onyx splitter.cpp) : une poignee que l'on
;  glisse ; verticale (plus haute que large : glisse en x) ou horizontale.
;  Valeur = sa position (x ou y) ; plage : ui_w_set_range(sep, min, max, 0, 1).
;  Le glisser la deplace et rend UN_CHANGE : le programme replace ses volets
;  (ui_w_move) et les redessine.
; ---------------------------------------------------------------------------

; x0..x3 = rectangle de la poignee (6 px d'epaisseur conseilles), x4 = min,
; x5 = max de sa position -> poignee (valeur : x ou y de depart)
ui_splitter:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x4
	mov	x20,x5
	mov	x21,x0
	mov	x22,x1
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_SPLIT
	bl	uk_w_new
	cbz	x0,.out
	stp	x19,x20,[x0,UW_MIN]
	str	x21,[x0,UW_VALUE]
	ldp	w1,w2,[x0,UW_W]
	cmp	w2,w1				; horizontale : y
	b.gt	.out
	str	x22,[x0,UW_VALUE]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

uk_sp_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	jet	w4,TJ_FACE			; fond ; survol, glisser : CREUX
	ldr	w5,[x19,UW_FLAGS]
	tst	w5,UWF_HOVER or UWF_PRESSED
	b.eq	.f
	jet	w4,TJ_CREUX
.f:	bl	ui_fill
	mov	x0,x19
	bl	uk_vertical
	mov	x22,x0
	ldp	w2,w3,[x19,UW_W]
	cbz	x22,.h
	lsr	x0,x2,1				; trait LIGNE au milieu, trois points
	add	x0,x20,x0
	mov	x1,x21
	mov	x2,1
	jet	w4,TJ_LIGNE
	bl	ui_fill
	ldp	w2,w3,[x19,UW_W]
	lsr	x0,x2,1
	add	x0,x20,x0
	sub	x0,x0,1
	lsr	x1,x3,1
	add	x1,x21,x1
	sub	x1,x1,9
	b	.dots
.h:	mov	x0,x20
	lsr	x1,x3,1
	add	x1,x21,x1
	mov	x3,1
	jet	w4,TJ_LIGNE
	bl	ui_fill
	ldp	w2,w3,[x19,UW_W]
	lsr	x0,x2,1
	add	x0,x20,x0
	sub	x0,x0,9
	lsr	x1,x3,1
	add	x1,x21,x1
	sub	x1,x1,1
.dots:	mov	x20,0
.d:	cmp	x20,3
	b.hs	.out
	stp	x0,x1,[sp,-16]!
	mov	x2,3
	mov	x3,3
	jet	w4,TJ_TEXTE3
	bl	ui_fill
	ldp	x0,x1,[sp],16
	cbz	x22,.dh
	add	x1,x1,7
	b	.dn
.dh:	add	x0,x0,7
.dn:	add	x20,x20,1
	b	.d
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris : survol ; appui, glisser -> nouvelle position
uk_sp_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	w20,[x19,UW_FLAGS]
	cmp	x1,3
	b.eq	.leave
	cmp	x1,2
	b.eq	.rel
	cmp	x1,1
	b.ne	.move
	orr	w0,w20,UWF_PRESSED		; prise : decalage dans la poignee
	str	w0,[x19,UW_FLAGS]
	str	x2,[x19,UW_DRAG]
	str	x3,[x19,UW_DRAG2]
	b	.redraw
.move:	tbz	w20,3,.hover
	stp	x2,x3,[sp,-16]!
	mov	x0,x19
	bl	uk_vertical
	ldp	x2,x3,[sp],16
	cbz	x0,.hy
	ldr	w1,[x19,UW_X]			; nouvelle position en x
	sxtw	x1,w1
	add	x1,x1,x2
	ldr	x2,[x19,UW_DRAG]
	sub	x1,x1,x2
	b	.set
.hy:	ldr	w1,[x19,UW_Y]
	sxtw	x1,w1
	add	x1,x1,x3
	ldr	x2,[x19,UW_DRAG2]
	sub	x1,x1,x2
.set:	ldp	x2,x3,[x19,UW_MIN]
	cmp	x1,x2
	csel	x1,x2,x1,lt
	cmp	x1,x3
	csel	x1,x3,x1,gt
	ldr	x2,[x19,UW_VALUE]
	cmp	x1,x2
	b.eq	.zero
	str	x1,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_vertical
	ldr	x1,[x19,UW_VALUE]
	cbz	x0,.sy
	str	w1,[x19,UW_X]
	b	.ch
.sy:	str	w1,[x19,UW_Y]
.ch:	mov	x0,UN_CHANGE			; (le programme redessine)
	b	.out
.hover:	orr	w0,w20,UWF_HOVER
	cmp	w0,w20
	b.eq	.zero
	str	w0,[x19,UW_FLAGS]
	b	.redraw
.leave:	and	w0,w20,not (UWF_HOVER or UWF_PRESSED)
	str	w0,[x19,UW_FLAGS]
	b	.redraw
.rel:	and	w0,w20,not UWF_PRESSED
	str	w0,[x19,UW_FLAGS]
.redraw:
	mov	x0,x19
	bl	uk_w_refresh
.zero:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue
