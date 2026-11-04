; ============================================================================
;  kits/uikit_widgets.inc - UIKit : les widgets (poignees opaques)
;
;  Modele : l'API C a poignees d'Onyx (uikit/flat.h). Un widget est cree par
;  une fonction de la table (ui_list, ui_scrollbar...) qui rend une poignee
;  (adresse d'un bloc du kit, 0 : echec) ; le programme la redonne aux
;  fonctions generiques ui_w_* et aux fonctions de son type. Les widgets
;  appartiennent a la fenetre courante au moment de leur creation (ui_window_use,
;  uikit_window.inc) et sont dessines dans sa surface.
;
;  Boucle du programme :
;	mov x0,attente ; bl ui_event      ; evenement dans ui_ev
;	mov x0,0 ; bl ui_w_event          ; -> x0 = widget, x1 = code UN_*,
;	                                   ;    x2 = 1 si un widget l'a pris
;  Le kit gere le survol, la capture de la souris (glisser), le focus
;  clavier (Tab, Maj+Tab), le double-clic et la molette ; un widget qui
;  change rend UN_CHANGE, une validation (double-clic, Entree, clic d'un
;  bouton) UN_ACTIVATE ; ui_w_on y attache une fonction (x0 = widget,
;  x1 = code), appelee depuis ui_w_event.
;
;  Dessin aux jetons du theme (TJ_*), tailles de l'echelle
;  (docs/DESIGN-ELEGANT.md 2 et 11). Fonctions AAPCS64 : x0-x7 arguments,
;  x19-x28 preserves, appelables depuis C (docs/KITS.md, « en-tete C »).
; ============================================================================

; --- bloc d'un widget (interne au kit) ---------------------------------------
UW_NEXT		= 0			; widget suivant (ordre de creation)
UW_TYPE		= 8			; 32 bits : UT_*
UW_FLAGS	= 12			; 32 bits : UWF_*
UW_X		= 16			; 32 bits signes chacun : place dans la fenetre
UW_Y		= 20
UW_W		= 24
UW_H		= 28
UW_WIN		= 32			; fenetre (id du serveur)
UW_FN		= 40			; fonction (ui_w_on) : x0 = widget, x1 = code
UW_USER		= 48			; donnee du programme
UW_VALUE	= 56			; valeur, element choisi (-1 : aucun), curseur
UW_MIN		= 64
UW_MAX		= 72
UW_PAGE		= 80			; partie visible (defilement)
UW_POS		= 88			; defilement (premiere ligne...)
UW_TEXT		= 96			; texte (bloc du kit)
UW_TLEN		= 104
UW_TCAP		= 112
UW_ITEMS	= 120			; elements : 16 octets (texte, donnee)
UW_NITEMS	= 128
UW_ICAP		= 136
UW_COLS		= 144			; colonnes : 16 octets (largeur 32, drapeaux 32, titre)
UW_NCOLS	= 152
UW_ANCHOR	= 160			; ancre de la selection (texte)
UW_SORT		= 168			; colonne triee (-1 : aucune), sens (+4 : 0 croissant)
UW_DRAG		= 176			; glisser en cours (selon le type)
UW_AUX		= 184			; widget enfant, image...
UW_AUX2		= 192
UW_HOT		= 200			; element survole (-1 : aucun)
UW_STYLE	= 208			; drapeaux de creation
UW_DRAG2	= 216
UW_TIP		= 224			; infobulle
UW_PARENT	= 232			; widget qui le contient (0 : la fenetre)
UW_STEP		= 240			; pas (molette, fleches)
UW_UNDO		= 248			; journal annuler / retablir
UW_PX		= 256			; taille de police (0 : celle de l'interface)
UW_KWFN		= 264			; fonction des mots-cles (code)
UW_HBAR		= 272			; barre horizontale
UW_CURSOR	= 280			; forme du curseur + 1 (0 : celle du type)
UW_HPOS		= 288			; liste UL_HSCROLL : colonnes decalees de tant de pixels
UW_CELLFN	= 296			; liste : fonction des cellules (ui_list_cell)
UW_SIZE		= 320

UWF_HIDDEN	= 1
UWF_DISABLED	= 2
UWF_HOVER	= 4
UWF_PRESSED	= 8

; types
UT_SCROLL	= 1
UT_LIST		= 2
UT_TEXT		= 3
UT_BUTTON	= 4
UT_LABEL	= 5
UT_CHECK	= 6
UT_RADIO	= 7
UT_SWITCH	= 8
UT_DROP		= 9
UT_COMBO	= 10
UT_AREA		= 11
UT_SLIDER	= 12
UT_PROGRESS	= 13
UT_NUMERIC	= 14
UT_KNOB		= 15
UT_VUMETER	= 16
UT_LCD		= 17
UT_SPLIT	= 18
UT_PANEL	= 19
UT_GROUP	= 20
UT_SEGMENT	= 21
UT_TABS		= 22
UT_TABSTRIP	= 23
UT_TOOLBAR	= 24
UT_TOOL		= 25
UT_IMAGE	= 26
UT_CAL		= 27
UT_DATE		= 28
UT_PALETTE	= 29
UT_COLOR	= 30
UT_TREE		= 31
UT_CANVAS	= 32
UT_VIEW		= 33			; vue dessinee a la main (uikit_view.inc)
UT_VIEWF	= 34			; la meme, qui prend le clavier

; table des types (uk_vt) : 8 decalages de 32 bits par type
VT_DRAW		= 0			; x0 = widget
VT_MOUSE	= 4			; x0, x1 = genre (0 depl., 1 appui, 2 relache,
					; 3 sortie), x2, x3 = point local, x4 = clics
VT_KEY		= 8			; x0, x1 = code HID, x2 = caractere, x3 = mods
					; -> code, ou -1 si la touche n'est pas prise
VT_WHEEL	= 12			; x0, x1 = crans (> 0 : vers le haut)
VT_CHILD	= 16			; x0 = parent, x1 = enfant, x2 = code -> code
VT_LAYOUT	= 20			; x0 : place ses enfants (apres ui_w_move)
VT_FLAGS	= 24			; bits VTF_*
VT_ROW		= 32
VTF_FOCUS	= 1			; prend le focus clavier
VTF_RING	= 2			; anneau de focus autour (11.8)
VTF_TAB		= 4			; garde Tab (Ctrl+Tab : focus suivant)
VTF_MOVE	= 8			; en haut de la fenetre : l'appui attend (uk_held)

; metriques de l'echelle (uk_met)
MT_PX		= 0			; police d'interface
MT_SMALL	= 8			; petite police
MT_ROW		= 16			; ligne de liste
MT_CTL		= 24			; hauteur d'un controle
MT_SB		= 32			; largeur d'une barre de defilement
MT_HDR		= 40			; en-tete de colonnes

UK_DBL_MS	= 450			; double-clic : delai,
UK_DBL_PX	= 5			; et ecart en pixels
UK_HDR_H	= 24

; couleur d'un jeton : jet w4, TJ_SURFACE (ne modifie que x17)
macro jet r*, t* {
	adr	x17,uk_jetons
	ldr	r,[x17,t]
}

; ---------------------------------------------------------------------------
;  Tas du kit : classes de puissances de deux (32 o a 1 Go), une liste libre
;  par classe ; en-tete de 16 octets (+0 lien quand libre, +8 classe).
; ---------------------------------------------------------------------------

; x0 = taille -> x0 = bloc mis a zero (0 : echec)
uk_alloc:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x0,x0,16
	mov	x19,5
.cl:	mov	x1,1
	lsl	x1,x1,x19
	cmp	x1,x0
	b.hs	.got
	add	x19,x19,1
	cmp	x19,30
	b.ls	.cl
	b	.fail
.got:	adr	x2,uk_free_heads
	ldr	x0,[x2,x19,lsl 3]
	cbz	x0,.new
	ldr	x3,[x0]
	str	x3,[x2,x19,lsl 3]
	b	.init
.new:	mov	x0,1
	lsl	x0,x0,x19
	bl	uk_malloc
	cbz	x0,.fail
.init:	str	x19,[x0,8]
	add	x20,x0,16
	mov	x1,1				; mise a zero
	lsl	x1,x1,x19
	sub	x1,x1,16
	mov	x2,x20
.z:	stp	xzr,xzr,[x2],16
	subs	x1,x1,16
	b.hi	.z
	mov	x0,x20
	b	.out
.fail:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = bloc de uk_alloc (0 : rien)
uk_free:
	cbz	x0,.ret
	sub	x0,x0,16
	ldr	x1,[x0,8]
	adr	x2,uk_free_heads
	ldr	x3,[x2,x1,lsl 3]
	str	x3,[x0]
	str	x0,[x2,x1,lsl 3]
.ret:	ret

; x0 = bloc de uk_alloc -> x0 = octets utilisables
uk_alloc_size:
	sub	x1,x0,16
	ldr	x1,[x1,8]
	mov	x0,1
	lsl	x0,x0,x1
	sub	x0,x0,16
	ret

; x0 = chaine -> x0 = longueur (feuille)
uk_strlen:
	mov	x1,x0
.l:	ldrb	w2,[x1],1
	cbnz	w2,.l
	sub	x0,x1,x0
	sub	x0,x0,1
	ret

; x0 = destination, x1 = source, x2 = octets (sans recouvrement genant :
; vers le bas, ou blocs separes) (feuille)
uk_memcpy:
	cbz	x2,.ret
.l:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.l
.ret:	ret

; x0 = destination, x1 = source, x2 = octets : copie vers le haut (dest >
; source, recouvrement) (feuille)
uk_memcpy_up:
	cbz	x2,.ret
.l:	sub	x2,x2,1
	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	cbnz	x2,.l
.ret:	ret

; x0 = chaine (0 : vide) -> x0 = copie dans le tas du kit (0 : echec)
uk_strdup:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,uk_empty
	cbz	x0,.e
	mov	x19,x0
.e:	mov	x0,x19
	bl	uk_strlen
	add	x20,x0,1
	mov	x0,x20
	bl	uk_alloc
	cbz	x0,.out
	mov	x2,x20
	mov	x1,x19
	str	x0,[sp,-16]!
	bl	uk_memcpy
	ldr	x0,[sp],16
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Metriques, polices
; ---------------------------------------------------------------------------

; -> x0 = metriques de l'echelle (MT_*)
uk_met:
	prologue
	adr	x0,uk_mt_ok
	ldr	x1,[x0]
	cbnz	x1,.ok
	mov	x1,1
	str	x1,[x0]
	bl	ui_echelle
	cmp	x0,2
	mov	x1,2
	csel	x0,x1,x0,hi
	adr	x1,uk_mt_tab
	mov	x2,6
	mul	x2,x2,x0
	add	x1,x1,x2			; 6 octets par echelle
	adr	x3,uk_mt
	mov	x4,0
.c:	ldrb	w5,[x1,x4]
	str	x5,[x3,x4,lsl 3]
	add	x4,x4,1
	cmp	x4,6
	b.lo	.c
.ok:	adr	x0,uk_mt
	epilogue

uk_mt_tab:	db 14,13,30,32,8,24		; grande  (px, petite, ligne, controle, barre, en-tete)
		db 14,13,30,32,10,24		; moyenne
		db 13,12,26,28,10,22		; petite
		align 4

; -> x0 = police reguliere du programme
uk_font_r:
	adr	x0,uk_vars
	ldr	x0,[x0]
	add	x0,x0,UV_FONT
	ret

; -> x0 = police mono du programme
uk_font_m:
	adr	x0,uk_vars
	ldr	x0,[x0]
	add	x0,x0,UV_FONT_M
	ret

; ---------------------------------------------------------------------------
;  Texte : largeur d'un prefixe, point -> position, ellipse
; ---------------------------------------------------------------------------

; x0 = chaine, x1 = octets (-1 : tout), x2 = taille px, x3 = police -> x0 =
; largeur du prefixe en pixels (exactement le stylo de ui_text)
ui_text_prefix:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x3
	mov	x0,x3
	mov	x1,x2
	bl	ttf_set_size
	mov	x0,x21
	mov	x1,x19
	mov	x2,x20
	mov	x3,-1
	mov	x4,0
	bl	ttf_text_pos
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine, x1 = taille px, x2 = police, x3 = x (depuis le debut du
; texte) -> x0 = octets avant le point le plus proche (clic dans un texte),
; x1 = sa position en pixels
ui_text_hit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x2
	mov	x22,x3
	mov	x0,x2
	bl	ttf_set_size
	mov	x0,0
	mov	x1,0
	tbnz	x22,63,.out
	mov	x0,x21
	mov	x1,x19
	mov	x2,-1
	mov	x3,x22
	mov	x4,0
	bl	ttf_text_pos
	mov	x2,x0
	mov	x0,x1
	mov	x1,x2
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Comme ui_text, mais x6 = largeur max (obligatoire) : un texte trop long est
; coupe et termine par « … ». -> x0 = x final
ui_text_fit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	mov	x23,x4
	mov	x24,x5
	mov	x25,x6
	cmp	x25,0
	b.le	.plain
	mov	x0,x24
	mov	x1,x23
	bl	ttf_set_size
	mov	x0,x24
	mov	x1,x19
	bl	ttf_text_width
	cmp	x0,x25
	b.le	.plain
	mov	x0,x24				; largeur de « … »
	adr	x1,uk_s_ellipsis
	bl	ttf_text_width
	subs	x3,x25,x0
	csel	x3,xzr,x3,lt
	mov	x0,x24
	mov	x1,x19
	mov	x2,-1
	mov	x4,1				; ce qui tient
	bl	ttf_text_pos
	mov	x26,400				; (borne du tampon)
	cmp	x1,x26
	csel	x2,x26,x1,hi
	adr	x0,uk_fitbuf
	mov	x1,x19
	bl	uk_memcpy			; -> x0 = fin
	mov	w1,0xE2				; « … » U+2026
	strb	w1,[x0]
	mov	w1,0x80
	strb	w1,[x0,1]
	mov	w1,0xA6
	strb	w1,[x0,2]
	strb	wzr,[x0,3]
	adr	x19,uk_fitbuf
.plain:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	w3,w22
	mov	x4,x23
	mov	x5,x24
	mov	x6,x25
	cmp	x6,0
	csel	x6,xzr,x6,lt
	bl	ui_text
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine, x1 = x, x2 = y (haut de la boite), x3 = hauteur de la boite,
; w4 = couleur, x5 = police, x6 = largeur max (ellipse), x7 = taille px :
; texte centre verticalement -> x0 = x final
uk_text_box:
	mov	x9,37
	mul	x9,x7,x9
	add	x9,x9,50
	mov	x10,100
	udiv	x9,x9,x10
	add	x2,x2,x3,lsr 1
	add	x2,x2,x9
	mov	w3,w4
	mov	x4,x7
	b	ui_text_fit

; ---------------------------------------------------------------------------
;  Decoupe : ui_clip (programme) et pile interne (widgets)
; ---------------------------------------------------------------------------

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur : les dessins (ui_fill, ui_text,
; ui_pixel, ui_rbox, ui_frame...) ne sortent plus de ce rectangle ;
; largeur <= 0 : plus de decoupe
ui_clip:
	adr	x9,uk_clip_sp			; (la pile interne repart de zero)
	str	xzr,[x9]
	adr	x9,uk_clip_on
	cmp	x2,0
	b.le	.off
	cmp	x3,0
	b.le	.off
	mov	x4,1
	str	x4,[x9]
	add	x2,x0,x2
	add	x3,x1,x3
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	adr	x9,uk_clip
	stp	x0,x1,[x9]
	stp	x2,x3,[x9,16]
	ret
.off:	str	xzr,[x9]
	ret

; x0..x3 = rectangle : decoupe a son intersection avec la decoupe courante
; (l'ancienne est empilee ; uk_clip_pop la rend). Feuille.
uk_clip_push:
	adr	x9,uk_clip_sp
	ldr	x10,[x9]
	cmp	x10,8
	b.hs	.ret				; (pile pleine : on garde l'ancienne)
	adr	x11,uk_clip_stack
	mov	x12,40
	madd	x11,x10,x12,x11
	add	x10,x10,1
	str	x10,[x9]
	adr	x9,uk_clip
	ldp	x12,x13,[x9]
	stp	x12,x13,[x11]
	ldp	x12,x13,[x9,16]
	stp	x12,x13,[x11,16]
	adr	x9,uk_clip_on
	ldr	x12,[x9]
	str	x12,[x11,32]
	add	x2,x0,x2
	add	x3,x1,x3
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	cbz	x12,.set
	adr	x9,uk_clip
	ldp	x12,x13,[x9]
	cmp	x0,x12
	csel	x0,x12,x0,lt
	cmp	x1,x13
	csel	x1,x13,x1,lt
	ldp	x12,x13,[x9,16]
	cmp	x2,x12
	csel	x2,x12,x2,gt
	cmp	x3,x13
	csel	x3,x13,x3,gt
.set:	cmp	x2,x0				; (vide : un rectangle nul)
	csel	x2,x0,x2,lt
	cmp	x3,x1
	csel	x3,x1,x3,lt
	adr	x9,uk_clip
	stp	x0,x1,[x9]
	stp	x2,x3,[x9,16]
	adr	x9,uk_clip_on
	mov	x12,1
	str	x12,[x9]
.ret:	ret

uk_clip_pop:
	adr	x9,uk_clip_sp
	ldr	x10,[x9]
	cbz	x10,.ret
	sub	x10,x10,1
	str	x10,[x9]
	adr	x11,uk_clip_stack
	mov	x12,40
	madd	x11,x10,x12,x11
	adr	x9,uk_clip
	ldp	x12,x13,[x11]
	stp	x12,x13,[x9]
	ldp	x12,x13,[x11,16]
	stp	x12,x13,[x9,16]
	ldr	x12,[x11,32]
	adr	x9,uk_clip_on
	str	x12,[x9]
.ret:	ret

; ---------------------------------------------------------------------------
;  Table des types
; ---------------------------------------------------------------------------
macro uk_vt_row d*, m*, k*, w*, c*, l*, f* {
	dw	d - uk_vt, m - uk_vt, k - uk_vt, w - uk_vt
	dw	c - uk_vt, l - uk_vt, f, 0
}
uk_vt:	uk_vt_row uk_vt, uk_vt, uk_vt, uk_vt, uk_vt, uk_vt, 0		; (0 : aucun)
	uk_vt_row uk_sb_draw, uk_sb_mouse, uk_vt, uk_sb_wheel, uk_vt, uk_vt, 0
	uk_vt_row uk_ls_draw, uk_ls_mouse, uk_ls_key, uk_ls_wheel, uk_ls_child, uk_ls_layout, VTF_FOCUS
	uk_vt_row uk_tb_draw, uk_tb_mouse, uk_tb_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_RING
	uk_vt_row uk_bt_draw, uk_bt_mouse, uk_bt_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_RING or VTF_MOVE
	uk_vt_row uk_lb_draw, uk_vt, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_tg_draw, uk_tg_mouse, uk_tg_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_RING or VTF_MOVE
	uk_vt_row uk_tg_draw, uk_tg_mouse, uk_tg_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_RING or VTF_MOVE
	uk_vt_row uk_tg_draw, uk_tg_mouse, uk_tg_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_RING or VTF_MOVE
	uk_vt_row uk_dd_draw, uk_dd_mouse, uk_dd_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_RING
	uk_vt_row uk_dd_draw, uk_dd_mouse, uk_vt, uk_vt, uk_cb_child, uk_cb_layout, 0
	uk_vt_row uk_ta_draw, uk_ta_mouse, uk_ta_key, uk_ta_wheel, uk_ta_child, uk_ta_layout, VTF_FOCUS or VTF_RING or VTF_TAB
	uk_vt_row uk_sl_draw, uk_sl_mouse, uk_val_key, uk_val_wheel, uk_vt, uk_vt, VTF_FOCUS or VTF_RING
	uk_vt_row uk_pg_draw, uk_vt, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_nm_draw, uk_nm_mouse, uk_nm_key, uk_val_wheel, uk_vt, uk_vt, VTF_FOCUS or VTF_RING
	uk_vt_row uk_kn_draw, uk_kn_mouse, uk_val_key, uk_val_wheel, uk_vt, uk_vt, VTF_FOCUS or VTF_RING
	uk_vt_row uk_vu_draw, uk_vt, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_lcd_draw, uk_vt, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_sp_draw, uk_sp_mouse, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_pn_draw, uk_vt, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_pn_draw, uk_vt, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_sg_draw, uk_sg_mouse, uk_sg_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_RING or VTF_MOVE
	uk_vt_row uk_tabs_draw, uk_sg_mouse, uk_sg_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_MOVE
	uk_vt_row uk_ts_draw, uk_ts_mouse, uk_vt, uk_ts_wheel, uk_vt, uk_vt, 0
	uk_vt_row uk_tbar_draw, uk_vt, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_tool_draw, uk_tool_mouse, uk_vt, uk_vt, uk_vt, uk_vt, VTF_MOVE
	uk_vt_row uk_im_draw, uk_vt, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_cal_draw, uk_cal_mouse, uk_cal_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_RING
	uk_vt_row uk_dp_draw, uk_dp_mouse, uk_dp_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_RING
	uk_vt_row uk_pal_draw, uk_pal_mouse, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_cp_draw, uk_cp_mouse, uk_cp_key, uk_vt, uk_vt, uk_vt, VTF_FOCUS or VTF_RING
	uk_vt_row uk_tr_draw, uk_tr_mouse, uk_tr_key, uk_tr_wheel, uk_tr_child, uk_tr_layout, VTF_FOCUS
	uk_vt_row uk_cv_draw, uk_cv_mouse, uk_vt, uk_vt, uk_vt, uk_vt, 0
	uk_vt_row uk_vw_draw, uk_vw_mouse, uk_vw_key, uk_vw_wheel, uk_vt, uk_vw_fit, 0
	uk_vt_row uk_vw_draw, uk_vw_mouse, uk_vw_key, uk_vw_wheel, uk_vt, uk_vw_fit, VTF_FOCUS or VTF_TAB

; x0 = widget, x1 = entree VT_* -> x9 = fonction (0 : aucune) ; feuille,
; ne modifie que x9, x10
uk_vt_fn:
	ldr	w9,[x0,UW_TYPE]
	lsl	x9,x9,5				; VT_ROW = 32
	add	x9,x9,x1
	adr	x10,uk_vt
	ldrsw	x9,[x10,x9]
	cbz	x9,.ret
	add	x9,x10,x9
.ret:	ret

; x0 = widget -> x0 = drapeaux VTF_* de son type
uk_vt_flags:
	ldr	w1,[x0,UW_TYPE]
	lsl	x1,x1,5
	add	x1,x1,VT_FLAGS
	adr	x2,uk_vt
	ldr	w0,[x2,x1]
	ret

; ---------------------------------------------------------------------------
;  Creation, liste des widgets
; ---------------------------------------------------------------------------

; x0 = type, x1..x4 = x, y, largeur, hauteur -> x0 = widget (0 : echec),
; ajoute a la fin de la liste, dans la fenetre courante
uk_w_new:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x0,UW_SIZE
	bl	uk_alloc
	cbz	x0,.out
	mov	x24,x0
	str	w19,[x24,UW_TYPE]
	stp	w20,w21,[x24,UW_X]
	stp	w22,w23,[x24,UW_W]
	bl	uk_win
	str	x0,[x24,UW_WIN]
	mov	x0,-1
	str	x0,[x24,UW_HOT]
	str	x0,[x24,UW_SORT]
	mov	x0,1
	str	x0,[x24,UW_STEP]
	adr	x0,uk_wlast			; a la fin de la liste
	ldr	x1,[x0]
	str	x24,[x0]
	cbz	x1,.first
	str	x24,[x1,UW_NEXT]
	b	.done
.first:	adr	x0,uk_wfirst
	str	x24,[x0]
.done:	mov	x0,x24
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget -> x0 = 1 s'il est montre (lui et ce qui le contient) dans la
; fenetre courante
uk_w_shown:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	uk_win
	ldr	x1,[x19,UW_WIN]
	cmp	x0,x1
	b.ne	.no
.l:	ldr	w1,[x19,UW_FLAGS]
	tbnz	w1,0,.no			; UWF_HIDDEN
	ldr	x19,[x19,UW_PARENT]
	cbnz	x19,.l
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = widget -> x0 = 1 s'il est actif (lui et ce qui le contient)
uk_w_enabled:
.l:	ldr	w1,[x0,UW_FLAGS]
	tbnz	w1,1,.no			; UWF_DISABLED
	ldr	x0,[x0,UW_PARENT]
	cbnz	x0,.l
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = widget -> x0 = 1 s'il a le focus (et la fenetre aussi)
uk_w_focused:
	adr	x1,uk_focus
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.no
	adr	x1,uk_winact
	ldr	x0,[x1]
	ret
.no:	mov	x0,0
	ret

; x0 = x, x1 = y (fenetre), x2 = fenetre -> x0 = widget montre le plus en
; avant sous ce point (0 : aucun)
uk_w_at:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x23,0
	adr	x19,uk_wfirst
	ldr	x19,[x19]
.l:	cbz	x19,.end
	ldr	x0,[x19,UW_WIN]
	cmp	x0,x22
	b.ne	.n
	ldp	w0,w1,[x19,UW_X]
	sxtw	x0,w0
	sxtw	x1,w1
	cmp	x20,x0
	b.lt	.n
	cmp	x21,x1
	b.lt	.n
	ldp	w2,w3,[x19,UW_W]
	add	x0,x0,x2
	add	x1,x1,x3
	cmp	x20,x0
	b.ge	.n
	cmp	x21,x1
	b.ge	.n
	mov	x0,x19
	bl	uk_w_shown
	cbz	x0,.n
	mov	x0,x19				; (un enfant coupe par sa vue : pas la)
	mov	x1,x20
	mov	x2,x21
	bl	uk_w_inview
	cbz	x0,.n
	mov	x23,x19
.n:	ldr	x19,[x19,UW_NEXT]
	b	.l
.end:	mov	x0,x23
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1, x2 = point de la fenetre -> x0 = 0 si une vue qui le contient
; coupe ce point (feuille)
uk_w_inview:
.l:	ldr	x0,[x0,UW_PARENT]
	cbz	x0,.yes
	ldr	w3,[x0,UW_TYPE]
	cmp	w3,UT_VIEW
	b.lo	.l
	ldrsw	x3,[x0,UW_X]
	ldrsw	x4,[x0,UW_Y]
	cmp	x1,x3
	b.lt	.no
	cmp	x2,x4
	b.lt	.no
	ldp	w5,w6,[x0,UW_W]
	add	x3,x3,x5
	add	x4,x4,x6
	cmp	x1,x3
	b.ge	.no
	cmp	x2,x4
	b.lt	.l
.no:	mov	x0,0
	ret
.yes:	mov	x0,1
	ret

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------

; x0 = widget : le dessine (lui puis ses enfants), sans ui_update
uk_w_paint:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	uk_w_shown
	cbz	x0,.out
	mov	x0,x19
	bl	uk_vt_flags
	tbz	w0,1,.noring			; VTF_RING : anneau de focus (11.8)
	mov	x0,x19
	bl	uk_ring
.noring:
	ldp	w0,w1,[x19,UW_X]
	sxtw	x0,w0
	sxtw	x1,w1
	ldp	w2,w3,[x19,UW_W]
	bl	uk_clip_push
	mov	x0,x19
	mov	x1,VT_DRAW
	bl	uk_vt_fn
	cbz	x9,.nod
	mov	x0,x19
	blr	x9
.nod:	ldr	w0,[x19,UW_TYPE]		; une vue coupe ses enfants (formulaire
	cmp	w0,UT_VIEW			; qui defile) ; sinon ils peuvent deborder
	b.hs	.kids
	bl	uk_clip_pop
.kids:	adr	x20,uk_wfirst			; enfants
	ldr	x20,[x20]
.c:	cbz	x20,.end
	ldr	x0,[x20,UW_PARENT]
	cmp	x0,x19
	b.ne	.cn
	mov	x0,x20
	bl	uk_w_paint
.cn:	ldr	x20,[x20,UW_NEXT]
	b	.c
.end:	ldr	w0,[x19,UW_TYPE]
	cmp	w0,UT_VIEW
	b.lo	.out
	bl	uk_clip_pop
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget : dessine et montre (ui_update de son rectangle, anneau compris)
uk_w_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,UW_WIN]			; (widget d'une autre fenetre : on y va)
	bl	uk_route
	mov	x20,x0
	mov	x0,x19
	bl	uk_w_shown
	cbz	x0,.back
	mov	x0,x19
	bl	uk_w_paint
	ldp	w0,w1,[x19,UW_X]
	sxtw	x0,w0
	sxtw	x1,w1
	ldp	w2,w3,[x19,UW_W]
	sub	x0,x0,3				; (anneau de focus)
	sub	x1,x1,3
	add	x2,x2,6
	add	x3,x3,6
	bl	uk_update_clip
.back:	cbz	x20,.out
	mov	x0,x20
	bl	ui_window_use
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0..x3 = rectangle (peut deborder) : ui_update borne a la surface
uk_update_clip:
	adr	x9,uk_vars
	ldr	x9,[x9]
	ldp	x10,x11,[x9,UV_W]
	add	x2,x0,x2
	add	x3,x1,x3
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	cmp	x2,x10
	csel	x2,x10,x2,gt
	cmp	x3,x11
	csel	x3,x11,x3,gt
	subs	x2,x2,x0
	b.le	.ret
	subs	x3,x3,x1
	b.le	.ret
	b	ui_update
.ret:	ret

; x0 = widget : a redessiner apres un changement (son parent s'il en a un :
; le parent dessine ses enfants par-dessus)
uk_w_refresh:
	ldr	x1,[x0,UW_PARENT]
	cbz	x1,uk_w_draw
	mov	x0,x1
	b	uk_w_refresh

; x0 = widget : anneau de focus (11.8) ou son effacement (FACE) autour
uk_ring:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	sub	x0,x20,3			; efface le tour (3 px)
	sub	x1,x21,3
	ldr	w2,[x19,UW_W]
	add	x2,x2,6
	ldr	w3,[x19,UW_H]
	add	x3,x3,6
	jet	w4,TJ_FACE
	bl	ui_fill
	ldr	w0,[x19,UW_H]			; (pas autour d'une grande zone)
	cmp	w0,80
	b.hs	.out
	mov	x0,x19
	bl	uk_w_focused
	cbz	x0,.out
	jet	w0,TJ_FACE
	jet	w1,TJ_ACCENT
	mov	x2,110
	bl	ui_mix
	mov	w5,w0
	mov	w6,w0
	sub	x0,x20,3
	sub	x1,x21,3
	ldr	w2,[x19,UW_W]
	add	x2,x2,6
	ldr	w3,[x19,UW_H]
	add	x3,x3,6
	mov	x4,11
	bl	ui_rbox
	jet	w5,TJ_FACE
	mov	w6,w5
	sub	x0,x20,1
	sub	x1,x21,1
	ldr	w2,[x19,UW_W]
	add	x2,x2,2
	ldr	w3,[x19,UW_H]
	add	x3,x3,2
	mov	x4,9
	bl	ui_rbox
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Fonctions generiques (table)
; ---------------------------------------------------------------------------

; x0 = widget (0 : tous ceux de la fenetre courante, sans parent) : dessine
; et montre
ui_w_draw:
	prologue
	str	x19,[sp,-16]!
	cbz	x0,.all
	bl	uk_w_draw
	b	.out
.all:	adr	x19,uk_wfirst
	ldr	x19,[x19]
.l:	cbz	x19,.out
	ldr	x0,[x19,UW_PARENT]
	cbnz	x0,.n
	mov	x0,x19
	bl	uk_w_draw
.n:	ldr	x19,[x19,UW_NEXT]
	b	.l
.out:	ldr	x19,[sp],16
	epilogue

; x0 = widget, x1 = x, x2 = y, x3 = largeur, x4 = hauteur : nouvelle place
; (ses enfants suivent ; rien n'est redessine : ui_w_draw)
ui_w_move:
	cbz	x0,.ret
	stp	w1,w2,[x0,UW_X]
	stp	w3,w4,[x0,UW_W]
	mov	x1,VT_LAYOUT
	str	x30,[sp,-16]!
	bl	uk_vt_fn
	ldr	x30,[sp],16
	cbz	x9,.ret
	br	x9
.ret:	ret

; x0 = widget, x1 = 1 montre / 0 cache (redessine s'il est montre ; un
; widget cache : le programme redessine le fond)
ui_w_show:
	cbz	x0,.ret
	ldr	w2,[x0,UW_FLAGS]
	orr	w2,w2,UWF_HIDDEN
	cbz	x1,.st
	and	w2,w2,not UWF_HIDDEN
.st:	str	w2,[x0,UW_FLAGS]
	cbz	x1,.hide
	b	uk_w_refresh
.hide:	adr	x1,uk_focus			; (cache : perd le focus)
	ldr	x2,[x1]
	cmp	x2,x0
	b.ne	.ret
	str	xzr,[x1]
.ret:	ret

; x0 = widget, x1 = 1 actif / 0 grise
ui_w_enable:
	cbz	x0,.ret
	ldr	w2,[x0,UW_FLAGS]
	orr	w2,w2,UWF_DISABLED
	cbz	x1,.st
	and	w2,w2,not UWF_DISABLED
.st:	str	w2,[x0,UW_FLAGS]
	b	uk_w_refresh
.ret:	ret

; x0 = widget (0 : aucun) : prend le focus clavier
ui_w_focus:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,0				; (widget d'une autre fenetre : son focus)
	cbz	x19,.cur
	ldr	x0,[x19,UW_WIN]
	bl	uk_route
	mov	x21,x0
.cur:	adr	x1,uk_focus
	ldr	x20,[x1]
	cmp	x20,x19
	b.eq	.back
	str	x19,[x1]
	cbz	x20,.new
	mov	x0,x20
	bl	uk_w_refresh
.new:	cbz	x19,.back
	mov	x0,x19
	bl	uk_w_refresh
.back:	cbz	x21,.out
	mov	x0,x21
	bl	ui_window_use
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = widget qui a le focus (0 : aucun)
ui_w_get_focus:
	adr	x0,uk_focus
	ldr	x0,[x0]
	ret

; x0 = widget, x1 = fonction (x0 = widget, x1 = code ; 0 : aucune), x2 =
; donnee du programme (ui_w_user)
ui_w_on:
	cbz	x0,.ret
	str	x1,[x0,UW_FN]
	str	x2,[x0,UW_USER]
.ret:	ret

; x0 = widget -> x0 = donnee du programme (ui_w_on)
ui_w_user:
	cbz	x0,.ret
	ldr	x0,[x0,UW_USER]
.ret:	ret

; x0 = widget -> x0 = valeur (element choisi d'une liste, -1 : aucun ;
; position d'une barre de defilement...)
ui_w_get_value:
	cbz	x0,.ret
	ldr	x0,[x0,UW_VALUE]
.ret:	ret

; x0 = widget, x1 = valeur : la change (bornee) et redessine, sans UN_CHANGE
ui_w_set_value:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x0,.out
	ldr	w2,[x19,UW_TYPE]
	cmp	w2,UT_LIST
	b.ne	.gen
	bl	uk_ls_select			; liste : choisit et montre l'element
	b	.draw
.gen:	cmp	w2,UT_CHECK			; case, interrupteur : 0 / 1
	b.eq	.bool
	cmp	w2,UT_TOOL			; bouton d'outil a bascule
	b.eq	.bool
	cmp	w2,UT_SWITCH
	b.eq	.bool
	cmp	w2,UT_RADIO			; radio : choisi (les autres decoches)
	b.ne	.dd
	cbz	x1,.bool
	ldr	x2,[x19,UW_VALUE]
	cbnz	x2,.draw
	bl	uk_tg_toggle
	b	.out
.bool:	cmp	x1,0
	cset	x1,ne
	str	x1,[x19,UW_VALUE]
	b	.draw
.dd:	cmp	w2,UT_DROP			; listes deroulantes : -1..n-1
	b.eq	.ddv
	cmp	w2,UT_COMBO
	b.ne	.gen1
.ddv:	ldr	x3,[x19,UW_NITEMS]
	cmp	x1,x3
	mov	x4,-1
	csel	x1,x4,x1,hs
	str	x1,[x19,UW_VALUE]
	cmp	w2,UT_COMBO
	b.ne	.draw
	tbnz	x1,63,.draw
	mov	x0,x19
	bl	uk_dd_current
	mov	x1,x0
	ldr	x0,[x19,UW_AUX2]
	bl	ui_w_set_text
	b	.draw
.gen1:	cmp	w2,UT_TREE			; arbre : l'id du noeud choisi
	b.ne	.gen4
	bl	uk_tr_index
	tbnz	x0,63,.out
	mov	x1,x0
	mov	x0,x19
	bl	uk_tr_select
	b	.out
.gen4:	cmp	w2,UT_SEGMENT			; segments, onglets : le choisi (page)
	b.eq	.seg
	cmp	w2,UT_TABS
	b.eq	.seg
	cmp	w2,UT_TABSTRIP
	b.ne	.gen3
	ldr	x3,[x19,UW_NITEMS]
	cmp	x1,x3
	b.hs	.out
	str	x1,[x19,UW_VALUE]
	b	.draw
.seg:	bl	uk_seg_choose
	b	.out
.gen3:	cmp	w2,UT_TEXT			; champ : le curseur (sans selection)
	b.ne	.gen2
	ldr	x2,[x19,UW_TLEN]
	cmp	x1,x2
	csel	x1,x2,x1,hi
	str	x1,[x19,UW_VALUE]
	str	x1,[x19,UW_ANCHOR]
	b	.draw
.gen2:	str	x1,[x19,UW_VALUE]
	bl	uk_w_clamp
.draw:	mov	x0,x19
	bl	uk_w_refresh
.out:	ldr	x19,[sp],16
	epilogue

; x0 = widget, x1 = min, x2 = max, x3 = page (partie visible ; barre de
; defilement : le pouce), x4 = pas (molette, fleches ; 0 : inchange)
ui_w_set_range:
	prologue
	cbz	x0,.out
	stp	x1,x2,[x0,UW_MIN]
	str	x3,[x0,UW_PAGE]
	cbz	x4,.c
	str	x4,[x0,UW_STEP]
.c:	str	x0,[sp,-16]!
	bl	uk_w_clamp
	ldr	x0,[sp],16
	bl	uk_w_refresh
.out:	epilogue

; x0 = widget : valeur bornee a [min, max - page] (feuille)
uk_w_clamp:
	ldp	x1,x2,[x0,UW_MIN]
	ldr	x3,[x0,UW_PAGE]
	sub	x2,x2,x3
	cmp	x2,x1
	csel	x2,x1,x2,lt
	ldr	x3,[x0,UW_VALUE]
	cmp	x3,x1
	csel	x3,x1,x3,lt
	cmp	x3,x2
	csel	x3,x2,x3,gt
	str	x3,[x0,UW_VALUE]
	ret

; x0 = widget : le retire de la fenetre et rend sa memoire (ses enfants aussi)
ui_w_destroy:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
.kids:	adr	x20,uk_wfirst			; enfants d'abord
	ldr	x20,[x20]
.k:	cbz	x20,.self
	ldr	x0,[x20,UW_PARENT]
	cmp	x0,x19
	b.ne	.kn
	mov	x0,x20
	bl	ui_w_destroy
	b	.kids
.kn:	ldr	x20,[x20,UW_NEXT]
	b	.k
.self:	adr	x0,uk_focus			; plus de references
	mov	x1,x19
	bl	uk_forget
	adr	x0,uk_capture
	bl	uk_forget
	adr	x0,uk_hover
	bl	uk_forget
	adr	x0,uk_click
	bl	uk_forget
	adr	x0,uk_held
	bl	uk_forget
	adr	x21,uk_wins			; (les autres fenetres : dans leur bloc)
	ldr	x21,[x21]
.wb:	cbz	x21,.list
	add	x0,x21,WB_FOCUS
	bl	uk_forget
	add	x0,x21,WB_FOCUS+8
	bl	uk_forget
	add	x0,x21,WB_FOCUS+16
	bl	uk_forget
	ldr	x21,[x21,WB_NEXT]
	b	.wb
.list:	mov	x20,0				; retire de la liste
	adr	x21,uk_wfirst
	ldr	x21,[x21]
.f:	cbz	x21,.free
	cmp	x21,x19
	b.eq	.found
	mov	x20,x21
	ldr	x21,[x21,UW_NEXT]
	b	.f
.found:	ldr	x0,[x19,UW_NEXT]
	cbz	x20,.head
	str	x0,[x20,UW_NEXT]
	b	.tail
.head:	adr	x1,uk_wfirst
	str	x0,[x1]
.tail:	adr	x1,uk_wlast
	ldr	x2,[x1]
	cmp	x2,x19
	b.ne	.free
	str	x20,[x1]
.free:	ldr	w0,[x19,UW_TYPE]
	cmp	w0,UT_VIEW			; (une vue : ses pixels)
	b.lo	.nov
	ldr	x0,[x19,UW_AUX]
	bl	ui_image_free
.nov:	ldr	x0,[x19,UW_TEXT]
	bl	uk_free
	ldr	x0,[x19,UW_COLS]		; titres des colonnes
	ldr	x21,[x19,UW_NCOLS]
.fc:	cbz	x21,.fc2
	sub	x21,x21,1
	add	x1,x0,x21,lsl 4
	str	x0,[sp,-16]!
	ldr	x0,[x1,8]
	bl	uk_free
	ldr	x0,[sp],16
	b	.fc
.fc2:	bl	uk_free
	mov	x0,x19				; elements
	bl	uk_items_clear
	ldr	x0,[x19,UW_ITEMS]
	bl	uk_free
	ldr	x0,[x19,UW_TIP]
	bl	uk_free
	mov	x0,x19
	bl	uk_undo_clear
	ldr	x0,[x19,UW_UNDO]
	bl	uk_free
	mov	x0,x19
	bl	uk_free
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = variable, x1 = widget : la variable est mise a 0 si elle le designe
uk_forget:
	ldr	x2,[x0]
	cmp	x2,x1
	b.ne	.ret
	str	xzr,[x0]
.ret:	ret

; ---------------------------------------------------------------------------
;  Evenements
; ---------------------------------------------------------------------------

; x0 = evenement (0 : ui_ev) -> x0 = widget concerne (0 : aucun), x1 = code
; UN_* (0 : rien a signaler), x2 = 1 si un widget a pris l'evenement
ui_w_event:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbnz	x19,.ev
	adr	x19,uk_vars
	ldr	x19,[x19]
	add	x19,x19,UV_EV
.ev:	ldr	w0,[x19,EV_WIN]			; la fenetre de l'evenement, le temps
	bl	uk_route			; de le traiter
	mov	x20,x0
	mov	x0,x19
	bl	uk_w_event_in
	cbz	x20,.out
	stp	x0,x1,[sp,-16]!
	str	x2,[sp,-16]!
	mov	x0,x20
	bl	ui_window_use
	ldr	x2,[sp],16
	ldp	x0,x1,[sp],16
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = evenement de la fenetre courante (ou d'une fenetre sans bloc)
uk_w_event_in:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	ldr	w0,[x19,EV_TYPE]
	cmp	w0,WEV_MOUSE
	b.eq	.mouse
	cmp	w0,WEV_KEY
	b.eq	.key
	cmp	w0,WEV_WHEEL
	b.eq	.wheel
	cmp	w0,WEV_FOCUS
	b.eq	.wfocus
	cmp	w0,WEV_DROP			; glisser-deposer (uikit_misc.inc)
	b.ne	.nodrop
	bl	uk_on_drop
	mov	x24,x0
	cbz	x24,.none
	b	.notify2
.nodrop:
	cmp	w0,WEV_RESIZE
	b.ne	.none
	bl	uk_win				; nouvelle taille (uikit_window.inc)
	ldr	w1,[x19,EV_WIN]
	cmp	x0,x1
	b.ne	.none
	bl	uk_on_resize_ev
	mov	x24,0
	b	.taken

.mouse:	ldr	w20,[x19,EV_KIND]
	ldrsw	x21,[x19,EV_X]
	ldrsw	x22,[x19,EV_Y]
	ldr	w23,[x19,EV_WIN]
	adr	x0,uk_mx			; (infobulle : le point, le temps ; une
	stp	x21,x22,[x0]			; infobulle montree s'en va)
	adr	x0,uk_mwin
	str	x23,[x0]
	sys	SYS_TIME_MS
	adr	x1,uk_hover_t
	str	x0,[x1]
	bl	uk_tip_hide
	adr	x9,uk_held			; un appui attend : deplacement ou clic ?
	ldr	x24,[x9]
	cbnz	x24,.held
.nothold:
	adr	x0,uk_capture
	ldr	x24,[x0]
	cbnz	x24,.send
	mov	x0,x21
	mov	x1,x22
	mov	x2,x23
	bl	uk_w_at
	mov	x24,x0
	adr	x1,uk_hover			; survol : sortie de l'ancien
	ldr	x25,[x1]
	cmp	x25,x24
	b.eq	.same
	str	x24,[x1]
	mov	x0,x24				; la forme du curseur suit le widget
	bl	uk_w_shape
	bl	ui_cursor
	cbz	x25,.same
	mov	x0,x25
	mov	x1,3
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	uk_w_mouse
.same:	cbz	x24,.free
	cmp	w20,1				; un widget sans souris (panneau, texte,
	b.ne	.widget				; barre d'outils) laisse faire comme le vide
	cmp	x22,UK_DRAG_H
	b.hs	.widget
	mov	x0,x24
	mov	x1,VT_MOUSE
	bl	uk_vt_fn
	cbnz	x9,.widget
	b	.drag
.free:	cmp	w20,1				; appui hors des widgets, en haut de la
	b.ne	.none				; fenetre : elle se deplace
	cmp	x22,UK_DRAG_H
	b.hs	.none
.drag:	bl	ui_window_drag
	b	.none
.widget:
	mov	x0,x24
	bl	uk_w_enabled
	cbz	x0,.taken
	cmp	w20,1
	b.ne	.send
	cmp	x22,UK_INTENT_H			; appui en haut de la fenetre, sur un
	b.hs	.press				; bouton : il attend de voir si la souris
	adr	x0,uk_replay			; bouge (la fenetre) ou non (le clic)
	ldr	x0,[x0]
	cbnz	x0,.press
	ldr	w0,[x19,EV_CODE]		; (bouton droit : un appui normal)
	tbnz	w0,1,.press
	mov	x0,x24
	bl	uk_vt_flags
	tbz	w0,3,.press
	adr	x9,uk_held
	str	x24,[x9]
	stp	x21,x22,[x9,8]
	adr	x0,uk_capture
	str	x24,[x0]
	b	.none
.held:	cmp	w20,2
	b.eq	.hrel
	cbnz	w20,.none
	ldp	x0,x1,[x9,8]			; bouge : la fenetre part, le widget ne
	subs	x0,x0,x21			; saura rien de cet appui
	cneg	x0,x0,mi
	subs	x1,x1,x22
	cneg	x1,x1,mi
	cmp	x0,UK_INTENT_PX
	b.hs	.hgo
	cmp	x1,UK_INTENT_PX
	b.lo	.none
.hgo:	str	xzr,[x9]
	adr	x0,uk_capture
	str	xzr,[x0]
	adr	x0,uk_hover
	str	xzr,[x0]
	bl	ui_window_drag
	b	.none
.hrel:	ldp	x0,x1,[x9,8]			; relache sans bouger : c'est un clic,
	str	xzr,[x9]			; l'appui passe, puis ce relache
	adr	x9,uk_capture
	str	xzr,[x9]
	sub	sp,sp,32
	ldp	x2,x3,[x19]
	stp	x2,x3,[sp]
	ldp	x2,x3,[x19,16]
	stp	x2,x3,[sp,16]
	mov	w2,1
	str	w2,[sp,EV_KIND]
	str	w0,[sp,EV_X]
	str	w1,[sp,EV_Y]
	adr	x9,uk_replay
	mov	x2,1
	str	x2,[x9]
	mov	x0,sp
	bl	uk_w_event_in
	adr	x9,uk_replay
	str	xzr,[x9]
	add	sp,sp,32
	cbz	x1,.nothold
	adr	x3,uk_capture			; (onglets : choisis a l'appui, le relache
	str	xzr,[x3]			; n'a plus rien a dire)
	adr	x3,uk_hover
	str	xzr,[x3]
	b	.out
.press:	adr	x0,uk_capture			; appui : capture, focus, double-clic
	str	x24,[x0]
	mov	x0,x24
	bl	uk_vt_flags
	tbz	w0,0,.dbl
	mov	x0,x24
	bl	ui_w_focus
.dbl:	sys	SYS_TIME_MS
	mov	x26,1				; clics
	adr	x9,uk_click			; widget, temps, x, y
	ldp	x1,x2,[x9]
	cmp	x1,x24
	b.ne	.single
	sub	x2,x0,x2
	cmp	x2,UK_DBL_MS
	b.hs	.single
	ldp	x3,x4,[x9,16]
	subs	x3,x3,x21
	cneg	x3,x3,mi
	cmp	x3,UK_DBL_PX
	b.hs	.single
	subs	x4,x4,x22
	cneg	x4,x4,mi
	cmp	x4,UK_DBL_PX
	b.hs	.single
	mov	x26,2
	str	xzr,[x9]			; (un troisieme clic : simple)
	b	.send2
.single:
	stp	x24,x0,[x9]
	stp	x21,x22,[x9,16]
	b	.send2
.send:	mov	x26,1
.send2:	cmp	w20,2				; relache : fin de la capture (le survol
	b.ne	.call				; sera recalcule au prochain mouvement)
	adr	x0,uk_capture
	str	xzr,[x0]
	adr	x0,uk_hover
	str	xzr,[x0]
.call:	ldr	w0,[x19,16]			; boutons (uk_mbtn : bit 1 droit)
	adr	x1,uk_mbtn
	str	x0,[x1]
	mov	x0,x24
	mov	x1,x20
	ldr	w2,[x24,UW_X]
	sxtw	x2,w2
	sub	x2,x21,x2
	ldr	w3,[x24,UW_Y]
	sxtw	x3,w3
	sub	x3,x22,x3
	mov	x4,x26
	bl	uk_w_mouse
	b	.notify

.key:	bl	uk_tip_hide
	ldr	w0,[x19,EV_KIND]
	cbz	w0,.none			; relache
	ldr	w20,[x19,EV_CODE]
	ldr	w21,[x19,EV_CHAR]
	ldr	w22,[x19,EV_MODS]
	cmp	w20,KEY_TAB
	b.ne	.kf
	mov	w0,MOD_CTRL			; (un widget qui garde Tab : sans Ctrl)
	tst	w22,w0
	b.ne	.tabf
	adr	x0,uk_focus
	ldr	x0,[x0]
	cbz	x0,.tabf
	ldr	x1,[x0,UW_STYLE]		; (UTA_FORMTAB : Tab passe au suivant)
	tbnz	x1,10,.tabf
	bl	uk_vt_flags
	tbnz	w0,2,.kf
.tabf:	ldr	w0,[x19,EV_WIN]
	mov	w1,0x22				; Maj : vers l'arriere
	tst	w22,w1
	cset	x1,ne
	bl	uk_focus_next
	cbz	x0,.none
	mov	x24,x0
	b	.taken
.kf:	adr	x0,uk_focus
	ldr	x24,[x0]
	cbz	x24,.none
	ldr	x0,[x24,UW_WIN]
	ldr	w1,[x19,EV_WIN]
	cmp	x0,x1
	b.ne	.none
	mov	x0,x24
	bl	uk_w_shown
	cbz	x0,.none
	mov	x0,x24
	bl	uk_w_enabled
	cbz	x0,.none
	mov	x0,x24
	mov	x1,VT_KEY
	bl	uk_vt_fn
	cbz	x9,.none
	mov	x0,x24
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	blr	x9
	tbnz	x0,63,.none			; touche laissee au programme
	mov	x1,x0
	b	.notify2

.wheel:	bl	uk_tip_hide
	ldrsw	x20,[x19,EV_Y]			; crans
	adr	x0,uk_hover
	ldr	x24,[x0]
	cbnz	x24,.wl
	adr	x0,uk_focus
	ldr	x24,[x0]
.wl:	cbz	x24,.none			; le widget, ou celui qui le contient
	mov	x0,x24
	mov	x1,VT_WHEEL
	bl	uk_vt_fn
	cbnz	x9,.wcall
	ldr	x24,[x24,UW_PARENT]
	b	.wl
.wcall:	mov	x0,x24
	bl	uk_w_shown
	cbz	x0,.none
	mov	x0,x24
	bl	uk_w_enabled
	cbz	x0,.none
	mov	x0,x24
	mov	x1,VT_WHEEL
	bl	uk_vt_fn
	mov	x0,x24
	mov	x1,x20
	blr	x9
	b	.notify

.wfocus:
	ldr	w0,[x19,EV_KIND]		; la fenetre gagne / perd le focus
	adr	x1,uk_winact
	str	x0,[x1]
	adr	x0,uk_focus
	ldr	x0,[x0]
	cbz	x0,.none
	bl	uk_w_refresh
	b	.none

.notify:					; x0 = code du widget x24
	mov	x1,x0
.notify2:
	mov	x26,x1
	cbz	x26,.taken
.up:	ldr	x25,[x24,UW_PARENT]		; un enfant previent son parent
	cbz	x25,.fn
	mov	x0,x25
	mov	x1,VT_CHILD
	bl	uk_vt_fn
	cbz	x9,.fn
	mov	x0,x25
	mov	x1,x24
	mov	x2,x26
	blr	x9
	mov	x24,x25
	mov	x26,x0
	cbz	x26,.taken
	b	.up
.fn:	ldr	x9,[x24,UW_FN]
	cbz	x9,.ret
	mov	x0,x24
	mov	x1,x26
	blr	x9
.ret:	mov	x0,x24
	mov	x1,x26
	mov	x2,1
	b	.out
.taken:	mov	x0,x24
	mov	x1,0
	mov	x2,1
	b	.out
.none:	mov	x0,0
	mov	x1,0
	mov	x2,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1..x4 : appelle sa fonction souris -> x0 = code
uk_w_mouse:
	prologue
	stp	x0,x1,[sp,-16]!
	mov	x1,VT_MOUSE
	bl	uk_vt_fn
	ldp	x0,x1,[sp],16
	cbz	x9,.none
	blr	x9
	b	.out
.none:	mov	x0,0
.out:	epilogue

; x0 = fenetre, x1 = 1 vers l'arriere -> x0 = widget qui prend le focus
; (suivant dans l'ordre de creation, montre, actif, VTF_FOCUS ; 0 : aucun)
uk_focus_next:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	adr	x0,uk_focus
	ldr	x20,[x0]
	mov	x23,0				; dernier candidat avant le focus
	mov	x24,0				; premier candidat
	mov	x25,0				; premier apres le focus
	mov	x26,0				; dernier candidat
	mov	x27,0				; 1 : le focus est passe
	adr	x19,uk_wfirst
	ldr	x19,[x19]
.l:	cbz	x19,.end
	cmp	x19,x20
	b.ne	.cand
	mov	x27,1
	b	.n
.cand:	ldr	x0,[x19,UW_WIN]
	cmp	x0,x21
	b.ne	.n
	mov	x0,x19
	bl	uk_vt_flags
	tbz	w0,0,.n
	mov	x0,x19
	bl	uk_w_shown
	cbz	x0,.n
	mov	x0,x19
	bl	uk_w_enabled
	cbz	x0,.n
	cbnz	x24,.f1
	mov	x24,x19
.f1:	mov	x26,x19
	cbz	x27,.before
	cbnz	x25,.n
	mov	x25,x19
	b	.n
.before:
	mov	x23,x19
.n:	ldr	x19,[x19,UW_NEXT]
	b	.l
.end:	cbnz	x22,.back
	mov	x0,x25				; en avant : apres, sinon le premier
	cbnz	x0,.set
	mov	x0,x24
	b	.set
.back:	mov	x0,x23				; en arriere : avant, sinon le dernier
	cbnz	x0,.set
	mov	x0,x26
.set:	mov	x19,x0
	cbz	x0,.out
	bl	ui_w_focus
	mov	x0,x19
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Barre de defilement (UT_SCROLL) : MIN..MAX = le contenu, PAGE = la partie
;  visible, VALUE = la position (MIN..MAX - PAGE). Sans fleches, pouce
;  arrondi (11.5).
; ---------------------------------------------------------------------------

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, x4 = drapeaux (USB_HORIZONTAL)
; -> x0 = poignee
ui_scrollbar:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_SCROLL
	bl	uk_w_new
	cbz	x0,.out
	str	x19,[x0,UW_STYLE]
	mov	x1,1
	str	x1,[x0,UW_PAGE]
	str	x1,[x0,UW_MAX]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = barre -> x0 = longueur du rail, x1 = longueur du pouce, x2 = son
; decalage, x3 = 1 si le contenu depasse (pouce utile)
uk_sb_geom:
	ldr	x4,[x0,UW_STYLE]
	ldr	w5,[x0,UW_H]
	tbz	x4,0,.v
	ldr	w5,[x0,UW_W]
.v:	ldp	x6,x7,[x0,UW_MIN]
	sub	x7,x7,x6			; etendue
	ldr	x8,[x0,UW_PAGE]
	mov	x1,x5
	mov	x2,0
	mov	x3,0
	cmp	x7,x8
	b.le	.full
	cmp	x8,0
	b.le	.full
	mul	x1,x5,x8			; pouce = rail * page / etendue
	udiv	x1,x1,x7
	cmp	x1,24
	mov	x9,24
	csel	x1,x9,x1,lt
	cmp	x1,x5
	csel	x1,x5,x1,gt
	ldr	x9,[x0,UW_VALUE]
	sub	x9,x9,x6
	sub	x7,x7,x8			; course de la valeur
	sub	x10,x5,x1			; course du pouce
	mul	x2,x9,x10
	udiv	x2,x2,x7
	mov	x3,1
.full:	mov	x0,x5
	ret

uk_sb_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,UW_PARENT]		; seule : son fond (FACE) ; dans un
	cbnz	x0,.nobg			; widget : par-dessus son contenu
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	jet	w4,TJ_FACE
	bl	ui_fill
.nobg:	mov	x0,x19
	bl	uk_sb_geom
	cbz	x3,.out
	mov	x20,x1				; pouce
	mov	x21,x2
	ldp	w22,w23,[x19,UW_X]
	ldr	w0,[x19,UW_FLAGS]
	tbz	w0,2,.thumb			; survol : le rail en CREUX
	mov	x0,x22
	mov	x1,x23
	ldp	w2,w3,[x19,UW_W]
	mov	x4,4
	jet	w5,TJ_CREUX
	mov	w6,w5
	bl	ui_rbox
.thumb:	mov	x1,128				; pouce : LIGNE_FORTE, survol 100,
	ldr	w2,[x19,UW_FLAGS]		; appui 84
	tbz	w2,2,.t1
	mov	x1,100
.t1:	tbz	w2,3,.t2
	mov	x1,84
.t2:	jet	w0,TJ_LIGNE_FORTE
	bl	ui_tone
.col:	mov	w24,w0
	ldr	x0,[x19,UW_STYLE]
	tbnz	x0,0,.h
	add	x0,x22,1			; vertical
	add	x1,x23,x21
	ldr	w2,[x19,UW_W]
	sub	x2,x2,2
	mov	x3,x20
	b	.box
.h:	add	x0,x22,x21			; horizontal
	add	x1,x23,1
	mov	x2,x20
	ldr	w3,[x19,UW_H]
	sub	x3,x3,2
.box:	cmp	x2,x3
	csel	x4,x2,x3,lo
	lsr	x4,x4,1
	mov	w5,w24
	mov	w6,w24
	bl	ui_rbox
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris : x1 genre, x2, x3 point local
uk_sb_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x0,[x19,UW_STYLE]		; position le long du rail
	mov	x21,x3
	tbz	x0,0,.v
	mov	x21,x2
.v:	ldr	x22,[x19,UW_VALUE]
	cmp	x20,3
	b.eq	.leave
	cmp	x20,2
	b.eq	.rel
	cmp	x20,1
	b.eq	.press
	ldr	x0,[x19,UW_DRAG]		; deplacement
	cbnz	x0,.drag
	ldr	w0,[x19,UW_FLAGS]
	tbnz	w0,2,.none
	orr	w0,w0,UWF_HOVER
	str	w0,[x19,UW_FLAGS]
	b	.redraw0
.leave:	ldr	w0,[x19,UW_FLAGS]
	and	w0,w0,not UWF_HOVER
	str	w0,[x19,UW_FLAGS]
	b	.redraw0
.rel:	str	xzr,[x19,UW_DRAG]
	ldr	w0,[x19,UW_FLAGS]
	and	w0,w0,not UWF_PRESSED
	str	w0,[x19,UW_FLAGS]
	b	.redraw0
.press:	mov	x0,x19
	bl	uk_sb_geom
	cbz	x3,.none
	cmp	x21,x2
	b.lt	.pgup
	add	x4,x2,x1
	cmp	x21,x4
	b.ge	.pgdn
	sub	x0,x21,x2			; pouce saisi
	add	x0,x0,1
	str	x0,[x19,UW_DRAG]
	ldr	w0,[x19,UW_FLAGS]
	orr	w0,w0,UWF_PRESSED
	str	w0,[x19,UW_FLAGS]
	b	.redraw0
.pgup:	ldr	x0,[x19,UW_PAGE]
	sub	x0,x22,x0
	b	.setv
.pgdn:	ldr	x0,[x19,UW_PAGE]
	add	x0,x22,x0
	b	.setv
.drag:	sub	x23,x21,x0			; nouveau haut du pouce
	add	x23,x23,1
	mov	x0,x19
	bl	uk_sb_geom
	sub	x4,x0,x1			; course du pouce
	cmp	x4,0
	b.le	.none
	cmp	x23,0
	csel	x23,xzr,x23,lt
	cmp	x23,x4
	csel	x23,x4,x23,gt
	ldp	x5,x6,[x19,UW_MIN]
	ldr	x7,[x19,UW_PAGE]
	sub	x6,x6,x5
	sub	x6,x6,x7			; course de la valeur
	mul	x0,x23,x6
	lsr	x8,x4,1				; (arrondi)
	add	x0,x0,x8
	udiv	x0,x0,x4
	add	x0,x0,x5
.setv:	str	x0,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_w_clamp
	ldr	x0,[x19,UW_VALUE]
	cmp	x0,x22
	b.eq	.redraw0
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
	b	.out
.redraw0:
	mov	x0,x19
	bl	uk_w_refresh
.none:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = crans (> 0 : vers le haut) : 3 pas par cran
uk_sb_wheel:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x2,[x19,UW_STEP]
	mov	x3,3
	mul	x2,x2,x3
	mul	x1,x1,x2
	ldr	x20,[x19,UW_VALUE]
	sub	x0,x20,x1
	str	x0,[x19,UW_VALUE]
	mov	x0,x19
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

include 'uikit_list.inc'
include 'uikit_text.inc'
include 'uikit_controls.inc'
include 'uikit_dialog.inc'
include 'uikit_choice.inc'
include 'uikit_area.inc'
include 'uikit_lex.inc'
include 'uikit_value.inc'
include 'uikit_undo.inc'
include 'uikit_window.inc'
include 'uikit_tabs.inc'
include 'uikit_image.inc'
include 'uikit_misc.inc'
include 'uikit_more.inc'
include 'uikit_tree.inc'
include 'uikit_extra.inc'
include 'uikit_view.inc'
include 'uikit_assoc.inc'
