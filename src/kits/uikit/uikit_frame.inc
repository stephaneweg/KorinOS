; ============================================================================
;  kits/uikit_frame.inc - UIKit : fenetres servies par Elegant, leur cadre,
;  les menus, la liste des fenetres (dock, barre de menus).
;
;  Comme dans Onyx (uikit/skin.cpp), le cadre est dessine par le programme :
;  deux copies (active, inactive) dans le tampon partage de la fenetre, a la
;  suite du contenu ; le serveur montre l'une ou l'autre selon le focus. Coins
;  arrondis : l'octet haut des pixels est la transparence (0 opaque).
;  Geometrie (FRAME_* de armpi.inc) : pas de barre de titre, bords 4 px, rayon
;  8 aux quatre coins ; reduire, agrandir, fermer sur le contenu, en haut a
;  droite (FRAME_CAP_*).
; ============================================================================

UK_OMBRE	= 4			; ombre autour d'un menu
UK_POP_PAD	= 6			; marge verticale d'un menu
UK_POP_SEP	= 11			; hauteur d'un separateur

uk_menu_title	db 'menu',0
uk_sous		db 0xE2,0x80,0xBA,0		; U+203A
		align 4

; x0 = image, x1 = largeur, x2 = hauteur : les dessins de UIKit y vont
; (jusqu'a uk_target_back)
uk_target:
	adr	x3,uk_vars
	ldr	x3,[x3]
	ldr	x4,[x3,UV_SURF]
	ldp	x5,x6,[x3,UV_W]
	adr	x7,uk_save
	str	x4,[x7]
	stp	x5,x6,[x7,8]
	str	x0,[x3,UV_SURF]
	stp	x1,x2,[x3,UV_W]
	adr	x7,uk_clip_on			; (sans la decoupe de ui_clip)
	ldr	x4,[x7]
	str	xzr,[x7]
	adr	x7,uk_save_clip
	str	x4,[x7]
	adr	x7,uk_clip_sp			; (la zone et la pile : ui_clip dans le dessin
	ldr	x4,[x7]				; d'une vue ne touche pas a celles du blit)
	stp	x0,x1,[sp,-16]!
	adr	x7,uk_clip
	ldp	x5,x6,[x7]
	ldp	x0,x1,[x7,16]
	adr	x7,uk_save_rect
	stp	x5,x6,[x7]
	stp	x0,x1,[x7,16]
	str	x4,[x7,32]
	ldp	x0,x1,[sp],16
	ret

uk_target_back:
	adr	x3,uk_vars
	ldr	x3,[x3]
	adr	x7,uk_save
	ldr	x4,[x7]
	ldp	x5,x6,[x7,8]
	str	x4,[x3,UV_SURF]
	stp	x5,x6,[x3,UV_W]
	adr	x7,uk_save_clip
	ldr	x4,[x7]
	adr	x7,uk_clip_on
	str	x4,[x7]
	stp	x0,x1,[sp,-16]!
	adr	x7,uk_save_rect
	ldp	x0,x1,[x7]
	ldp	x4,x5,[x7,16]
	ldr	x6,[x7,32]
	adr	x7,uk_clip
	stp	x0,x1,[x7]
	stp	x4,x5,[x7,16]
	adr	x7,uk_clip_sp
	str	x6,[x7]
	ldp	x0,x1,[sp],16
	ret

; -> x0 = police grasse du programme
uk_bold:
	adr	x0,uk_vars
	ldr	x0,[x0]
	add	x0,x0,UV_FONT_B
	ret

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, x4 = titre, x5 = role (WF_*)
; -> x0 = id (< 0 : erreur), reponse du serveur dans uk_msg. Si le serveur
; graphique n'est pas encore pret (demarrage), on l'attend jusqu'a 10 s.
uk_create:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x24,x5
	mov	x25,0				; essais
.again:	adr	x9,uk_msg
	stp	x19,x20,[x9,WM_A]
	stp	x21,x22,[x9,WM_C]
	str	x24,[x9,WM_DATA]
	add	x0,x9,WM_DATA+8
	mov	x1,x23
	bl	uk_title_copy
	mov	x0,WS_OP_CREATE
	bl	uk_call
	cmn	x0,19				; E_NODEV : pas (encore) de serveur
	b.ne	.out
	add	x25,x25,1
	cmp	x25,100
	b.hs	.out
	mov	x0,100
	sys	SYS_SLEEP
	b	.again
.out:	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = largeur, x1 = hauteur, x2 = titre -> x0 = poignee (<0 : erreur)
; Fenetre ordinaire, placee automatiquement, remplie de UI_BG.
ui_window:
	mov	x5,0
	mov	x4,x2
	mov	x3,x1
	mov	x2,x0
	mov	x0,-1
	mov	x1,-1
	b	ui_window_ex

; x0 = x, x1 = y (-1, -1 : automatique), x2 = largeur, x3 = hauteur,
; x4 = titre, x5 = role (WF_* : barre de menus, dock, fond d'ecran...)
; -> x0 = poignee (< 0 : erreur). Remplie de UI_BG (transparente : WF_ALPHA).
ui_window_ex:
	prologue
	bl	tr_x4
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x24,x5
	bl	uk_create
	tbnz	x0,63,.out
	mov	x19,x0
	bl	uk_wb_new			; son bloc : elle devient la courante
	cbnz	x0,.blk
	adr	x1,uk_msg
	str	x19,[x1,WM_A]
	mov	x0,WS_OP_CLOSE
	bl	uk_call
	mov	x0,E_NOMEM
	b	.out
.blk:	str	x19,[x0,WB_ID]
	adr	x1,uk_vars
	ldr	x1,[x1]
	str	x19,[x1,UV_WIN]
	adr	x9,uk_msg			; contenu et cadres : tampon partage
	ldr	x0,[x9,WM_A]
	adr	x1,uk_buf
	str	x0,[x1]
	sys	SYS_BUF_MAP
	tbnz	x0,63,.out
	mov	x20,x0
	adr	x1,uk_vars
	ldr	x1,[x1]
	str	x20,[x1,UV_SURF]
	stp	x21,x22,[x1,UV_W]
	adr	x9,uk_msg
	ldr	x0,[x9,WM_D]			; en-tete WD_* (present asynchrone)
	add	x0,x20,x0
	adr	x1,uk_dmg
	str	x0,[x1]
	ldp	x1,x2,[x9,WM_B]			; decalages des deux cadres (0 : aucun)
	adr	x3,uk_chr_a
	cbz	x1,.nochr
	add	x1,x20,x1
	add	x2,x20,x2
.nochr:	stp	x1,x2,[x3]			; uk_chr_a, uk_chr_i
	add	x0,x21,2*FRAME_BORDER
	add	x1,x22,FRAME_TITLE_H+FRAME_BORDER
	adr	x2,uk_ow
	stp	x0,x1,[x2]			; uk_ow, uk_oh
	adr	x0,uk_flags
	str	x24,[x0]
	adr	x0,uk_title
	mov	x1,x23
	bl	uk_title_copy
	tbnz	x24,8,.chrome			; WF_ALPHA : reste transparente
	mov	x0,0
	mov	x1,0
	mov	x2,x21
	mov	x3,x22
	adr	x17,uk_jetons
	ldr	w4,[x17,TJ_FACE]
	bl	ui_fill
.chrome:
	bl	uk_draw_chrome
	mov	x0,x19
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = titre : change le titre de la fenetre (et de son cadre)
ui_title:
	prologue
	bl	tr_x0
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,uk_title
	mov	x1,x19
	bl	uk_title_copy
	adr	x0,uk_msg+WM_DATA
	mov	x1,x19
	bl	uk_title_copy
	bl	uk_win
	adr	x1,uk_msg
	str	x0,[x1,WM_A]
	mov	x0,WS_OP_TITLE
	bl	uk_call
	bl	uk_draw_chrome
	ldr	x19,[sp],16
	epilogue

; dessine les deux copies du cadre (theme courant), puis previent le serveur
uk_draw_chrome:
	prologue
	adr	x0,uk_chr_a
	ldr	x0,[x0]
	cbz	x0,.out
	mov	x1,1
	bl	uk_draw_frame
	adr	x0,uk_chr_i
	ldr	x0,[x0]
	mov	x1,0
	bl	uk_draw_frame
	bl	uk_win
	adr	x1,uk_msg
	str	x0,[x1,WM_A]
	mov	x0,WS_OP_FRAME
	bl	uk_call
.out:	epilogue

; x0 = image (uk_ow x uk_oh), x1 = 1 actif : le cadre. Le bord de 12 px est une
; ombre, calculee ici une fois (onze anneaux noirs transparents, decales vers le
; bas, puis le contour) ; le corps, arrondi aux quatre coins, n'apparait qu'aux
; coins du contenu ; boutons par-dessus.
uk_draw_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x1
	adr	x1,uk_ow
	ldp	x21,x22,[x1]
	mov	x1,x21
	mov	x2,x22
	bl	uk_target
	adr	x19,uk_jetons
	adr	x23,uk_ombre_fa			; transparences des anneaux
	cbnz	x20,.rings
	adr	x23,uk_ombre_fi
.rings:	mov	x24,0				; anneau 0..10 (rayons 22 a 12)
.r:	ldr	w5,[x23,x24,lsl 2]
	mov	x0,x24
	mov	x4,22
	sub	x4,x4,x24
	mov	x1,10				; decalage : 3 px pour le plus large,
	sub	x1,x1,x24			; dans le bord
	add	x1,x1,x1,lsl 1
	mov	x2,10
	udiv	x1,x1,x2
	cmp	x1,x24
	csel	x1,x24,x1,hi
	add	x1,x1,x24
	sub	x2,x21,x24,lsl 1
	sub	x3,x22,x24,lsl 1
	mov	w6,w5
	bl	ui_rbox
	add	x24,x24,1
	cmp	x24,11
	b.lo	.r
	ldr	w5,[x19,TJ_CONTOUR]		; contour (inactif : plus leger)
	cbnz	x20,.ctr
	lsr	w0,w5,24
	add	w0,w0,0x10
	cmp	w0,255
	mov	w1,255
	csel	w0,w1,w0,hi
	and	w5,w5,0xFFFFFF
	orr	w5,w5,w0,lsl 24
.ctr:	mov	x0,11
	mov	x4,11
	bl	.ring
	ldr	w5,[x19,TJ_FACE]		; le corps
	adr	x0,uk_flags
	ldr	x0,[x0]
	tbz	x0,13,.body			; (WF_BODY_DARK)
	mov	w5,0
.body:	mov	x0,12
	mov	x4,10
	bl	.ring
	bl	uk_frame_buttons
	bl	uk_target_back
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.ring:	mov	x1,x0
	sub	x2,x21,x0,lsl 1
	sub	x3,x22,x0,lsl 1
	mov	w6,w5
	b	ui_rbox

; (uk_draw_frame) x19 = jetons, x20 = 1 actif, x21 = largeur de l'image : les
; trois boutons, sans fond ni contour : reduire, agrandir, fermer (mesures de
; win_hit, core.inc d'Elegant)
uk_frame_buttons:
	prologue
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	sub	x22,x21,FRAME_BORDER+FRAME_CAP_MARGIN+FRAME_CAP_W	; x22 : x
	mov	x23,FRAME_BORDER+FRAME_CAP_MARGIN			; x23 : y
	mov	x0,x22
	mov	x1,x23
	mov	x2,FRAME_CAP_W
	mov	x3,FRAME_CAP_H
	movl	w4,0xFF000000
	bl	ui_fill
	ldr	w24,[x19,TJ_TEXTE2]		; symboles (inactif : plus discrets)
	cbnz	x20,.grisr
	ldr	w24,[x19,TJ_TEXTE3]
.grisr:	ldr	w25,[x19,TJ_TEXTE3]		; x25 : gris
	adr	x0,uk_flags			; (WF_GLYPH_LIGHT : en clair)
	ldr	x0,[x0]
	tbz	x0,12,.ink
	movl	w24,0x00FFFFFF
	cbnz	x20,.lg
	movl	w24,0x00B0B4BA
.lg:	movl	w25,0x00808488
.ink:
	add	x0,x22,FRAME_CAP_W-FRAME_CAP_BTN/2	; fermer : une croix
	add	x1,x23,FRAME_CAP_H/2
	bl	.cross
	adr	x5,uk_flags			; agrandir : carre (grise sans WF_RESIZABLE,
	ldr	x5,[x5]				; rien sur un dialogue)
	tbnz	x5,10,.fin
	mov	w4,w24
	tbnz	x5,9,.mx
	mov	w4,w25
.mx:	add	x0,x22,FRAME_CAP_W-FRAME_CAP_BTN-FRAME_CAP_BTN/2-5
	add	x1,x23,FRAME_CAP_H/2-5
	mov	x2,10
	mov	x3,10
	bl	ui_frame
	adr	x5,uk_flags			; reduire : trait (pas sur un dialogue)
	ldr	x5,[x5]
	tbnz	x5,10,.fin
	add	x0,x22,FRAME_CAP_W-2*FRAME_CAP_BTN-FRAME_CAP_BTN/2-5
	add	x1,x23,FRAME_CAP_H/2+3
	mov	x2,10
	mov	x3,2
	mov	w4,w24
	bl	ui_fill
.fin:
	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	epilogue
.cross:	prologue				; x0, x1 = centre, w24 : croix de 9 x 9
	stp	x26,x27,[sp,-16]!
	mov	x26,x0
	mov	x27,x1
	mov	x0,-4
.cr:	str	x0,[sp,-16]!
	add	x1,x27,x0
	add	x0,x26,x0
	mov	x2,2
	mov	x3,1
	mov	w4,w24
	bl	ui_fill
	ldr	x0,[sp]
	sub	x1,x27,x0
	add	x0,x26,x0
	mov	x2,2
	mov	x3,1
	mov	w4,w24
	bl	ui_fill
	ldr	x0,[sp],16
	add	x0,x0,1
	cmp	x0,4
	b.le	.cr
	ldp	x26,x27,[sp],16
	epilogue

uk_ombre_a	dw 0xF4000000, 0xEC000000, 0xE0000000	; anneaux, cadre actif
uk_ombre_i	dw 0xF8000000, 0xF2000000, 0xEA000000	; inactif
uk_ombre_fa	dw 0xFE000000, 0xFD000000, 0xFA000000, 0xF5000000, 0xF0000000, 0xEA000000, 0xE2000000, 0xD9000000, 0xCF000000, 0xC3000000, 0xB7000000	; fenetre active
uk_ombre_fi	dw 0xFF000000, 0xFE000000, 0xFC000000, 0xFA000000, 0xF8000000, 0xF4000000, 0xF0000000, 0xEC000000, 0xE7000000, 0xE1000000, 0xDB000000	; inactive
		align 4

; ---------------------------------------------------------------------------
;  Menus
; ---------------------------------------------------------------------------

; x0 = x, x1 = y (ecran), x2 = libelles (chaines a la suite, une vide a la
; fin), x3 = largeur -> x0 = numero du libelle choisi, -1 si abandon
ui_popup:
	mov	x4,0
; x4 = drapeaux : bit 1, garder le menu ouvert quand on choisit une entree a
; sous-menu (« > ») : rend aussi x1 = y (ecran) de sa ligne, pour y ouvrir le
; sous-menu ; puis bit 2 : reprendre ce menu (sous-menu abandonne ; avec le
; bit 3, x5, x6 = le clic qui l'a ferme : sur ce menu il y choisit, ailleurs
; tout se ferme), ou ui_popup_close. Rend -1 (Echap), -2 (clic hors du menu :
; x1, x2 = sa position sur l'ecran). Bit 0, libelles « icone<tab>texte » (icone de ui_icone ;
; sans tabulation : pas d'icone, texte a gauche). Bit 4 (sans le bit 0),
; libelles « texte<tab>raccourci » : raccourci aligne a droite (menus des
; applications). Un libelle « - » est un
; separateur ; un texte qui commence par « > » ouvre un sous-menu (le menu
; dessine « › » a droite). Fenetre de menu au-dessus de tout, qui
; capture souris et clavier ; un clic ailleurs (ou Echap) l'abandonne ;
; fleches et Entree au clavier. Style plat : ombre, coins de 8, survol en
; couleur d'accent (docs/DESIGN-ELEGANT.md 7). (x, y) : coin du corps.
ui_popup_ex:
	prologue
	adr	x9,uk_capture			; (ouvert sur un appui : le relache ne
	str	xzr,[x9]			; reviendra pas au widget)
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,16			; +0 tampon partage
	tbnz	x4,2,.resume			; reprise d'un menu garde ouvert
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	adr	x0,uk_pop_fl
	str	x4,[x0]
	bl	ui_echelle			; hauteur d'une ligne : 28, 26, 24
	lsl	x0,x0,1
	mov	x1,28
	sub	x0,x1,x0
	adr	x1,uk_pop_rh
	str	x0,[x1]
	mov	x24,0				; libelles, hauteur du corps
	mov	x23,2*UK_POP_PAD
	mov	x0,x21
.cnt:	ldrb	w1,[x0]
	cbz	w1,.counted
	add	x24,x24,1
	mov	x2,UK_POP_SEP
	cmp	w1,'-'
	b.ne	.row
	ldrb	w1,[x0,1]
	cbz	w1,.h
.row:	adr	x2,uk_pop_rh
	ldr	x2,[x2]
.h:	add	x23,x23,x2
.skip:	ldrb	w1,[x0],1
	cbnz	w1,.skip
	b	.cnt
.counted:
	mov	x28,-1				; resultat
	cbz	x24,.ret
	adr	x0,uk_scr			; dans l'ecran (avec l'ombre)
	bl	ui_screen
	tbnz	x0,63,.place
	adr	x0,uk_scr
	ldp	x0,x1,[x0]
	sub	x0,x0,x22
	sub	x0,x0,UK_OMBRE
	cmp	x19,x0
	csel	x19,x0,x19,gt
	sub	x1,x1,x23
	sub	x1,x1,UK_OMBRE
	cmp	x20,x1
	csel	x20,x1,x20,gt
.place:	cmp	x19,UK_OMBRE
	mov	x0,UK_OMBRE
	csel	x19,x0,x19,lt
	cmp	x20,UK_OMBRE
	csel	x20,x0,x20,lt
	sub	x0,x19,UK_OMBRE			; fenetre : le corps et son ombre
	sub	x1,x20,UK_OMBRE
	add	x2,x22,2*UK_OMBRE
	add	x3,x23,2*UK_OMBRE
	adr	x4,uk_menu_title
	mov	x5,WF_FRAMELESS or WF_TOP or WF_NOFOCUS or WF_NOLIST or WF_GRAB or WF_ALPHA
	bl	uk_create
	tbnz	x0,63,.ret
	mov	x25,x0
	adr	x9,uk_msg
	ldr	x0,[x9,WM_A]
	str	x0,[sp]
	sys	SYS_BUF_MAP
	tbnz	x0,63,.close
	mov	x26,x0
	mov	x0,x26
	add	x1,x22,2*UK_OMBRE
	add	x2,x23,2*UK_OMBRE
	bl	uk_target
	mov	x27,-1				; libelle sous le pointeur
	bl	uk_popup_draw
.wait:	adr	x0,uk_pev
	mov	x1,30000
	sys	SYS_EV_WAIT
	cbz	x0,.done			; (30 s sans rien : abandon)
	adr	x9,uk_pev
	ldr	w1,[x9,EV_WIN]
	cmp	x1,x25
	b.ne	.wait				; (evenements de l'autre fenetre : perdus)
	cmp	w0,WEV_KEY
	b.eq	.key
	cmp	w0,WEV_MOUSE
	b.ne	.wait
	ldrsw	x0,[x9,EV_X]			; libelle sous le pointeur
	ldrsw	x1,[x9,EV_Y]
	bl	uk_pop_hit
	mov	x4,x0
	adr	x9,uk_pev
	ldr	w1,[x9,4]			; 0 deplacement, 1 appui, 2 relache
	cmp	w1,2
	b.eq	.release
	cmp	w1,1
	b.ne	.move
	cmn	x4,2				; appui hors du menu : abandon (-2),
	b.ne	.move			; position du clic sur l'ecran gardee
	ldrsw	x0,[x9,EV_X]
	ldrsw	x1,[x9,EV_Y]
	add	x0,x0,x19
	sub	x0,x0,UK_OMBRE
	add	x1,x1,x20
	sub	x1,x1,UK_OMBRE
	adr	x2,uk_pop_clic
	stp	x0,x1,[x2]
	mov	x28,-2
	b	.done
.move:	cmp	x4,-2
	csinv	x4,x4,xzr,ne			; (dehors : rien sous le pointeur)
	cmp	x4,x27
	b.eq	.wait
	mov	x27,x4
	bl	uk_popup_draw
	b	.wait
.release:
	tbnz	x4,63,.wait			; (le relache du clic qui a ouvert le menu)
	mov	x28,x4
	b	.chosen
.resume:					; --- reprise : etat garde dans uk_pp
	adr	x9,uk_pp
	ldp	x19,x20,[x9]
	ldp	x21,x22,[x9,16]
	ldp	x23,x24,[x9,32]
	ldp	x25,x26,[x9,48]
	ldp	x27,x0,[x9,64]
	str	x0,[sp]
	ldp	x0,x1,[x9,80]
	adr	x2,uk_pop_fl
	and	x0,x0,not 4
	str	x0,[x2]
	adr	x2,uk_pop_rh
	str	x1,[x2]
	mov	x28,-1
	tbz	x4,3,.redraw			; bit 3 : le clic (x5, x6) qui a ferme le
	sub	x0,x5,x19			; sous-menu : sur ce menu, il y choisit ;
	add	x0,x0,UK_OMBRE			; ailleurs, tout se ferme
	sub	x1,x6,x20
	add	x1,x1,UK_OMBRE
	stp	x5,x6,[sp,-16]!
	bl	uk_pop_hit
	ldp	x5,x6,[sp],16
	cmn	x0,2
	b.ne	.inside
	adr	x2,uk_pop_clic
	stp	x5,x6,[x2]
	mov	x28,-2
	b	.done
.inside:
	tbnz	x0,63,.redraw			; (separateur, marge)
	mov	x28,x0				; la ligne cliquee devient la ligne
	mov	x27,x0				; surlignee
	mov	x0,x26
	add	x1,x22,2*UK_OMBRE
	add	x2,x23,2*UK_OMBRE
	bl	uk_target
	bl	uk_popup_draw
	b	.chosen
.redraw:
	mov	x0,x26
	add	x1,x22,2*UK_OMBRE
	add	x2,x23,2*UK_OMBRE
	bl	uk_target
	bl	uk_popup_draw
	b	.wait
.chosen:					; un choix : sous-menu (« > ») et menu a
	adr	x0,uk_pop_fl			; garder ouvert ?
	ldr	x0,[x0]
	tbz	x0,1,.done
	mov	x0,x28
	bl	uk_pop_sub
	cbz	x0,.done
	adr	x9,uk_pp			; garde : etat dans uk_pp, le menu reste
	stp	x19,x20,[x9]			; affiche (ui_popup_ex : reprise ;
	stp	x21,x22,[x9,16]			; ui_popup_close : fermeture)
	stp	x23,x24,[x9,32]
	stp	x25,x26,[x9,48]
	ldr	x0,[sp]
	stp	x27,x0,[x9,64]
	adr	x0,uk_pop_fl
	ldr	x0,[x0]
	adr	x1,uk_pop_rh
	ldr	x1,[x1]
	stp	x0,x1,[x9,80]
	bl	uk_target_back
	mov	x0,x28				; y (ecran) de la ligne choisie
	bl	uk_pop_row_y
	mov	x1,x0
	mov	x0,x28
	b	.kept
.key:	ldr	w1,[x9,4]
	cbz	w1,.wait			; relache
	ldr	w1,[x9,EV_CODE]
	cmp	w1,KEY_ESC
	b.eq	.done
	cmp	w1,KEY_ENTER
	b.ne	.down
	tbnz	x27,63,.wait
	mov	x0,x27				; (entree grisee : rien)
	bl	uk_pop_item
	ldrb	w0,[x0]
	cmp	w0,1
	b.eq	.wait
	mov	x28,x27
	b	.chosen
.down:	cmp	w1,0x4F				; droite : comme Entree (sous-menu)
	b.ne	.dn
	tbnz	x27,63,.wait
	mov	x28,x27
	b	.chosen
.dn:	mov	x2,1				; bas : +1, haut : -1 (separateurs sautes)
	cmp	w1,0x51
	b.eq	.step
	mov	x2,-1
	cmp	w1,0x52
	b.ne	.wait
.step:	mov	x3,x24				; (au plus un tour)
	tbz	x27,63,.st
	mov	x27,-1				; rien de choisi : depuis le haut, ou le bas
	cmp	x2,0
	csel	x27,x24,x27,lt
.st:	add	x27,x27,x2
	cmp	x27,0
	csel	x27,xzr,x27,lt
	sub	x0,x24,1
	cmp	x27,x0
	csel	x27,x0,x27,gt
	stp	x2,x3,[sp,-16]!
	mov	x0,x27
	bl	uk_pop_item
	ldp	x2,x3,[sp],16
	ldrb	w1,[x0]
	cmp	w1,'-'
	b.ne	.drawk
	ldrb	w1,[x0,1]
	cbnz	w1,.drawk
	subs	x3,x3,1
	b.ne	.st
.drawk:	bl	uk_popup_draw
	b	.wait
.done:	bl	uk_target_back
.close:	adr	x1,uk_msg			; fermeture du menu
	str	x25,[x1,WM_A]
	mov	x0,WS_OP_CLOSE
	bl	uk_call
	ldr	x0,[sp]
	sys	SYS_BUF_FREE
.ret:	mov	x0,x28
	adr	x9,uk_pop_clic			; (-2 : x1, x2 = le clic)
	ldp	x1,x2,[x9]
.kept:	add	sp,sp,16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ferme le menu garde ouvert par ui_popup_ex (drapeau 2)
ui_popup_close:
	prologue
	adr	x9,uk_pp
	ldr	x0,[x9,48]			; fenetre
	adr	x1,uk_msg
	str	x0,[x1,WM_A]
	mov	x0,WS_OP_CLOSE
	bl	uk_call
	adr	x9,uk_pp
	ldr	x0,[x9,72]			; tampon partage
	sys	SYS_BUF_FREE
	epilogue

; (ui_popup_ex) x0 = numero -> x0 = 1 si son texte commence par « > »
uk_pop_sub:
	prologue
	bl	uk_pop_item
	adr	x1,uk_pop_fl
	ldr	x1,[x1]
	tbz	x1,0,.t
	mov	x2,x0				; avec icones : apres la tabulation
.tab:	ldrb	w3,[x2],1
	cbz	w3,.t
	cmp	w3,9
	b.ne	.tab
	mov	x0,x2
.t:	ldrb	w1,[x0]
	cmp	w1,'>'
	cset	x0,eq
	epilogue

; (ui_popup_ex) x0 = numero -> x0 = y (ecran) du haut de sa ligne
uk_pop_row_y:
	add	x5,x20,UK_POP_PAD
	mov	x2,x21
	mov	x3,0
	adr	x6,uk_pop_rh
	ldr	x6,[x6]
.w:	cmp	x3,x0
	b.hs	.ret
	mov	x4,x6
	ldrb	w7,[x2]
	cmp	w7,'-'
	b.ne	.h
	ldrb	w7,[x2,1]
	cbnz	w7,.h
	mov	x4,UK_POP_SEP
.h:	add	x5,x5,x4
.sk:	ldrb	w7,[x2],1
	cbnz	w7,.sk
	add	x3,x3,1
	b	.w
.ret:	mov	x0,x5
	ret

; (ui_popup_ex) x0 = numero -> x0 = son libelle (x21 : libelles)
uk_pop_item:
	mov	x1,x21
.f:	cbz	x0,.ret
.sk:	ldrb	w2,[x1],1
	cbnz	w2,.sk
	sub	x0,x0,1
	b	.f
.ret:	mov	x0,x1
	ret

; (ui_popup_ex) x0, x1 = point dans la fenetre -> x0 = libelle dessous
; (-1 : separateur ou marge, -2 : hors du menu)
uk_pop_hit:
	sub	x0,x0,UK_OMBRE
	sub	x1,x1,UK_OMBRE
	cmp	x0,0
	b.lt	.out
	cmp	x0,x22
	b.ge	.out
	cmp	x1,0
	b.lt	.out
	cmp	x1,x23
	b.ge	.out
	sub	x1,x1,UK_POP_PAD
	mov	x2,x21				; libelle
	mov	x3,0				; numero
	adr	x5,uk_pop_rh
	ldr	x5,[x5]
.w:	cmp	x3,x24
	b.hs	.none
	mov	x4,x5
	ldrb	w6,[x2]
	cmp	w6,'-'
	b.ne	.r
	ldrb	w6,[x2,1]
	cbnz	w6,.r
	mov	x4,UK_POP_SEP
.r:	cmp	x1,0
	b.lt	.none
	cmp	x1,x4
	b.lo	.in
	sub	x1,x1,x4
.sk:	ldrb	w6,[x2],1
	cbnz	w6,.sk
	add	x3,x3,1
	b	.w
.in:	cmp	x4,UK_POP_SEP
	b.eq	.none
	ldrb	w6,[x2]				; (entree grisee : rien)
	cmp	w6,1
	b.eq	.none
	mov	x0,x3
	ret
.none:	mov	x0,-1
	ret
.out:	mov	x0,-2
	ret

; (ui_popup_ex) dessine le menu : x21 libelles, x22 largeur, x23 hauteur du
; corps, x24 nombre, x25 fenetre, x27 libelle sous le pointeur ; puis le montre
uk_popup_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x26,x28,[sp,-16]!
	str	x23,[sp,-16]!
	adr	x9,uk_jetons
	ldr	w26,[x9,TJ_MENU]
	add	x0,x22,2*UK_OMBRE		; ombre, contour, corps arrondi
	add	x1,x23,2*UK_OMBRE
	bl	uk_ombre
	mov	x0,UK_OMBRE
	mov	x1,UK_OMBRE
	mov	x2,x22
	mov	x3,x23
	mov	x4,8
	mov	w5,w26
	mov	w6,w26
	bl	ui_rbox
	mov	x19,x21				; libelle
	mov	x20,0				; numero
	mov	x23,UK_OMBRE+UK_POP_PAD		; y de la ligne
.i:	cmp	x20,x24
	b.hs	.show
	ldrb	w0,[x19]			; separateur
	cmp	w0,'-'
	b.ne	.item
	ldrb	w0,[x19,1]
	cbnz	w0,.item
	adr	x9,uk_jetons
	ldr	w4,[x9,TJ_LIGNE]
	mov	x0,UK_OMBRE+10
	add	x1,x23,UK_POP_SEP/2
	sub	x2,x22,20
	mov	x3,1
	bl	ui_fill
	add	x23,x23,UK_POP_SEP
	b	.next
.item:	adr	x9,uk_jetons			; encre (sur l'accent : blanc)
	ldr	w28,[x9,TJ_TEXTE1]
	ldrb	w0,[x19]			; grisee (0x01) : TEXTE3, jamais surlignee
	cmp	w0,1
	b.ne	.en
	ldr	w28,[x9,TJ_TEXTE3]
	b	.icon
.en:	cmp	x20,x27
	b.ne	.icon
	ldr	w5,[x9,TJ_ACCENT]
	mov	w6,w5
	mov	x0,UK_OMBRE+5
	mov	x1,x23
	sub	x2,x22,10
	adr	x3,uk_pop_rh
	ldr	x3,[x3]
	mov	x4,5
	bl	ui_rbox
	adr	x17,uk_jetons
	ldr	w28,[x17,TJ_ACCENT_INK]
.icon:	mov	x26,x19				; texte (apres sa marque)
	ldrb	w0,[x26]
	cmp	w0,2
	b.hi	.nomk
	add	x26,x26,1
	cmp	w0,2				; cochee : « ✓ » dans la marge
	b.ne	.nomk
	adr	x0,uk_s_check
	mov	x1,UK_OMBRE+1
	adr	x2,uk_pop_rh
	ldr	x2,[x2]
	add	x2,x2,9
	add	x2,x23,x2,lsr 1
	mov	w3,w28
	mov	x4,12
	adr	x5,uk_vars
	ldr	x5,[x5]
	add	x5,x5,UV_FONT
	mov	x6,0
	bl	ui_text
.nomk:
	adr	x0,uk_pop_x			; texte a gauche, sauf s'il y a une icone
	mov	x1,UK_OMBRE+14
	str	x1,[x0]
	adr	x0,uk_pop_fl
	ldr	x0,[x0]
	tbz	x0,0,.txt
	adr	x1,uk_pop_nom			; icone : nom jusqu'a la tabulation
	mov	x2,0
.nm:	ldrb	w3,[x26],1
	cbz	w3,.nmend
	cmp	w3,9
	b.eq	.nmend
	cmp	x2,30
	b.hs	.nm
	strb	w3,[x1,x2]
	add	x2,x2,1
	b	.nm
.nmend:	strb	wzr,[x1,x2]
	cbnz	w3,.ic
	mov	x26,x19				; (pas de tabulation : tout est texte)
	b	.txt
.ic:	adr	x0,uk_pop_nom
	mov	x1,UK_OMBRE+11
	adr	x2,uk_pop_rh
	ldr	x2,[x2]
	sub	x2,x2,16
	add	x2,x23,x2,lsr 1
	mov	x3,16
	bl	ui_icone
	adr	x0,uk_pop_x
	mov	x1,UK_OMBRE+36
	str	x1,[x0]
.txt:	ldrb	w0,[x26]			; « > » : ouvre un sous-menu, le menu
	cmp	w0,'>'				; dessine « › » a droite
	b.ne	.tx
	add	x26,x26,1
	adr	x0,uk_sous
	mov	x1,14
	adr	x2,uk_vars
	ldr	x2,[x2]
	add	x2,x2,UV_FONT
	bl	ui_text_width
	add	x1,x22,UK_OMBRE-14
	sub	x1,x1,x0
	adr	x9,uk_jetons
	ldr	w3,[x9,TJ_TEXTE3]
	cmp	x20,x27
	b.ne	.arrow
	adr	x17,uk_jetons
	ldr	w3,[x17,TJ_ACCENT_INK]
.arrow:	adr	x0,uk_sous
	adr	x2,uk_pop_rh
	ldr	x2,[x2]
	add	x2,x2,9
	add	x2,x23,x2,lsr 1
	mov	x4,14
	adr	x5,uk_vars
	ldr	x5,[x5]
	add	x5,x5,UV_FONT
	mov	x6,0
	bl	ui_text
.tx:	adr	x0,uk_pop_fl			; bit 4 : « texte<tab>raccourci », le
	ldr	x0,[x0]				; raccourci aligne a droite en TEXTE3
	tbz	x0,4,.tx2
	adr	x1,uk_pop_lib			; le texte seul (copie bornee)
	mov	x2,0				; source
	mov	x4,0				; copie
.lb:	ldrb	w3,[x26,x2]
	cbz	w3,.lbend
	cmp	w3,9
	b.eq	.lbend
	add	x2,x2,1
	cmp	x4,126
	b.hs	.lb
	strb	w3,[x1,x4]
	add	x4,x4,1
	b	.lb
.lbend:	strb	wzr,[x1,x4]
	cbz	w3,.lbtxt
	add	x0,x26,x2			; le raccourci, apres la tabulation
	add	x0,x0,1
	adr	x1,uk_pop_rac
	str	x0,[x1]
	mov	x1,13
	adr	x2,uk_vars
	ldr	x2,[x2]
	add	x2,x2,UV_FONT
	bl	ui_text_width
	add	x1,x22,UK_OMBRE-14
	sub	x1,x1,x0
	adr	x9,uk_jetons
	ldr	w3,[x9,TJ_TEXTE3]
	cmp	x20,x27
	b.ne	.rac
	adr	x17,uk_jetons
	ldr	w3,[x17,TJ_ACCENT_INK]
.rac:	adr	x0,uk_pop_rac
	ldr	x0,[x0]
	adr	x2,uk_pop_rh
	ldr	x2,[x2]
	add	x2,x2,9
	add	x2,x23,x2,lsr 1
	mov	x4,13
	adr	x5,uk_vars
	ldr	x5,[x5]
	add	x5,x5,UV_FONT
	mov	x6,0
	bl	ui_text
.lbtxt:	adr	x26,uk_pop_lib
.tx2:	mov	x0,x26
	adr	x1,uk_pop_x
	ldr	x1,[x1]
	adr	x2,uk_pop_rh
	ldr	x2,[x2]
	add	x2,x2,9
	add	x2,x23,x2,lsr 1
	mov	w3,w28
	mov	x4,13
	adr	x5,uk_vars
	ldr	x5,[x5]
	add	x5,x5,UV_FONT
	add	x6,x22,UK_OMBRE-30
	sub	x6,x6,x1
	bl	ui_text
	adr	x0,uk_pop_rh
	ldr	x0,[x0]
	add	x23,x23,x0
.next:	ldrb	w0,[x19],1			; libelle suivant
	cbnz	w0,.next
	add	x20,x20,1
	b	.i
.show:	ldr	x23,[sp],16
	adr	x9,uk_msg
	str	x25,[x9,WM_A]
	stp	xzr,xzr,[x9,WM_B]
	add	x0,x22,2*UK_OMBRE
	str	x0,[x9,WM_D]
	add	x0,x23,2*UK_OMBRE
	str	x0,[x9,WM_DATA]
	mov	x0,WS_OP_UPDATE
	bl	uk_call
	ldp	x26,x28,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = largeur, x1 = hauteur : ombre (trois anneaux transparents) et contour
; autour d'un corps en retrait de UK_OMBRE, rayon 8 (dans la surface courante)
ui_ombre:
uk_ombre:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0
.r:	adr	x5,uk_ombre_a
	ldr	w5,[x5,x21,lsl 2]
	cmp	x21,3
	b.lo	.go
	adr	x5,uk_jetons
	ldr	w5,[x5,TJ_CONTOUR]
.go:	mov	x0,x21
	mov	x1,x21
	sub	x2,x19,x21,lsl 1
	sub	x3,x20,x21,lsl 1
	mov	x4,12
	sub	x4,x4,x21
	mov	w6,w5
	bl	ui_rbox
	add	x21,x21,1
	cmp	x21,4
	b.lo	.r
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Liste des fenetres (dock, barre de menus)
; ---------------------------------------------------------------------------

; x0 = premiere, x1 = tampon (WL_PER_MSG x WL_SIZE octets) -> x0 = nombre de
; fenetres listees (entrees WL_* a partir de la premiere)
ui_winlist:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	adr	x9,uk_msg
	str	x0,[x9,WM_A]
	mov	x0,WS_OP_LIST
	bl	uk_call
	tbnz	x0,63,.out
	adr	x1,uk_msg+WM_DATA
	mov	x2,WL_PER_MSG*WL_SIZE/16
.cp:	ldp	x3,x4,[x1],16
	stp	x3,x4,[x19],16
	subs	x2,x2,1
	b.ne	.cp
.out:	ldr	x19,[sp],16
	epilogue

; x0 = premiere, x1 = tampon de WE_PER_MSG entrees WE_* -> x0 = nombre de
; fenetres (du fond vers l'avant, toutes les couches)
ui_win_details:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	adr	x9,uk_msg
	str	x0,[x9,WM_A]
	mov	x0,WS_OP_WIN_LIST
	bl	uk_call
	tbnz	x0,63,.out
	adr	x1,uk_msg+WM_DATA
	mov	x2,WE_PER_MSG*WE_SIZE/16
.cp:	ldp	x3,x4,[x1],16
	stp	x3,x4,[x19],16
	subs	x2,x2,1
	b.ne	.cp
.out:	ldr	x19,[sp],16
	epilogue

; x0 = espace de travail a montrer (-1 : garder), x1 = nombre (0 : garder) ->
; x0 = courant | nombre << 8 | compteur << 16
ui_desk:
	prologue
	adr	x9,uk_msg
	stp	x0,x1,[x9,WM_A]
	mov	x0,WS_OP_DESK
	bl	uk_call
	epilogue

; x0 = id : fenetre au premier plan (restauree si reduite)
ui_raise:
	prologue
	adr	x1,uk_msg
	str	x0,[x1,WM_A]
	mov	x0,WS_OP_RAISE
	bl	uk_call
	epilogue
