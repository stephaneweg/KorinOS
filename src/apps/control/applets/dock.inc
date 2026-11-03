; applets/dock.inc - applet Dock du panneau de configuration, le dockconf
; d'Onyx (user/Apps/dockconf) : les reglages de SD0:/etc/dock.ini (inc/dockconf.inc)
; en widgets de UIKit, meme disposition :
;   * les TIROIRS, de gauche a droite : le dock en a un pour chaque categorie
;     des applications de la carte - ici leur ordre (^ v), ceux masques, et
;     l'application principale de chacun (l'icone du tiroir) ;
;   * les BUREAUX (espaces de travail) : combien (1 a 6) et leurs noms ;
;   * les LANCEURS apres les tiroirs : ajouter une application de la liste de
;     toutes, en retirer une, la deplacer ;
;   * (ARMPI, dans l'en-tete du volet) la TAILLE du dock et la CORBEILLE.
; Appliquer ecrit dock.ini et relance le dock ; Retablir relit ce qui est
; enregistre. La mise en page est celle d'Onyx (700 x 470 sous le titre), les
; listes plus petites quand la fenetre l'est.

DK_W		= 700			; le volet d'Onyx
DK_H		= 470
DK_CT		= 24			; haut du contenu d'un groupe
DK_BTN		= 28			; hauteur d'un bouton
DK_TXT		= 96

; x0 = widget a retirer quand on quitte l'applet : garde -> x0 inchange
dk_top:
	adr	x1,dk_ntop
	ldr	x2,[x1]
	add	x3,x2,1
	str	x3,[x1]
	adr	x1,dk_tops
	str	x0,[x1,x2,lsl 3]
	ret

; x0 = widget, x1 = fonction -> x0 = widget (ses evenements)
dk_on:
	prologue
	str	x0,[sp,-16]!
	mov	x2,0
	bl	ui_w_on
	ldr	x0,[sp],16
	epilogue

; x0 = widget, x1 = infobulle -> x0 = widget
dk_tip:
	prologue
	str	x0,[sp,-16]!
	bl	ui_w_tip
	ldr	x0,[sp],16
	epilogue

dk_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	bl	applis_load			; (les applications installees depuis)
	bl	apps_sort
	bl	dock_layout_load
	adr	x0,ap_dessin
	adr	x1,dk_dessin
	str	x1,[x0]
	adr	x0,ap_fermer
	adr	x1,dk_fermer
	str	x1,[x0]
	adr	x0,ap_widgets
	mov	x1,1
	str	x1,[x0]
	adr	x0,dk_ntop
	str	xzr,[x0]
	adr	x0,ui_w				; le volet
	ldp	x19,x20,[x0]
	sub	x19,x19,SIDE_W
	sub	x20,x20,AP_HEAD
	mov	x0,DK_W
	cmp	x19,x0
	csel	x19,x0,x19,gt
	mov	x0,DK_H				; x21 : hauteur des tiroirs (262 chez
	subs	x0,x0,x20			; Onyx), les 2/3 de ce qui manque
	csel	x0,xzr,x0,lt
	mov	x1,3
	udiv	x0,x0,x1
	lsl	x0,x0,1
	mov	x1,262
	sub	x21,x1,x0
	mov	x22,x21				; x22 : y des lanceurs, x23 : leur hauteur
	add	x22,x22,10
	sub	x23,x20,48
	sub	x23,x23,x22
	sub	x25,x21,82			; x25 : hauteur de la liste des tiroirs
	sub	x26,x19,230			; x26 : largeur du groupe des tiroirs
	mov	x27,SIDE_W+10			; x27 : x du groupe des tiroirs
	mov	x28,AP_HEAD+6			; x28 : son y

	; ---- les tiroirs
	mov	x0,x27
	mov	x1,x28
	mov	x2,x26
	mov	x3,x21
	adr	x4,dk_s_tiroirs
	bl	tr4
	bl	ui_groupbox
	bl	dk_top
	mov	x24,x0
	add	x0,x27,10			; toutes les categories
	add	x1,x28,DK_CT
	sub	x2,x26,180
	mov	x3,x25
	mov	x4,0
	bl	ui_list
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_w_tiroirs
	str	x0,[x1]
	adr	x1,dk_clic_tiroir
	bl	dk_on
	adr	x1,dk_t_tiroirs
	bl	dk_tip
	add	x0,x27,10			; ^
	add	x1,x28,DK_CT+8
	add	x1,x1,x25
	mov	x2,34
	mov	x3,DK_BTN
	adr	x4,dk_s_haut
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_clic_gauche
	bl	dk_on
	adr	x1,dk_t_gauche
	bl	dk_tip
	add	x0,x27,48			; v
	add	x1,x28,DK_CT+8
	add	x1,x1,x25
	mov	x2,34
	mov	x3,DK_BTN
	adr	x4,dk_s_bas
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_clic_droite
	bl	dk_on
	adr	x1,dk_t_droite
	bl	dk_tip
	add	x0,x27,96			; Masque
	add	x1,x28,DK_CT+10
	add	x1,x1,x25
	mov	x2,150
	mov	x3,24
	adr	x4,dk_s_masque
	bl	tr4
	mov	x5,0
	bl	ui_checkbox
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_w_masque
	str	x0,[x1]
	adr	x1,dk_clic_masque
	bl	dk_on
	adr	x1,dk_t_masque
	bl	dk_tip
	add	x0,x27,x26			; Application principale
	sub	x0,x0,158
	add	x1,x28,DK_CT-2
	mov	x2,148
	mov	x3,18
	adr	x4,dk_s_princ
	mov	x5,0
	bl	ui_label
	mov	x1,x24
	bl	ui_w_set_parent
	add	x0,x27,x26
	sub	x0,x0,158
	add	x1,x28,DK_CT+18
	mov	x2,148
	add	x3,x25,18
	mov	x4,0
	bl	ui_list
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_w_princ
	str	x0,[x1]
	adr	x1,dk_clic_princ
	bl	dk_on
	adr	x1,dk_t_princ
	bl	dk_tip

	; ---- les bureaux
	add	x27,x27,x26			; x27 : x du groupe
	add	x27,x27,10
	mov	x0,x27
	mov	x1,x28
	mov	x2,200
	mov	x3,x21
	adr	x4,dk_s_bureaux
	bl	tr4
	bl	ui_groupbox
	bl	dk_top
	mov	x24,x0
	add	x0,x27,10			; Nombre
	add	x1,x28,DK_CT+4
	mov	x2,70
	mov	x3,20
	adr	x4,dk_s_nombre
	mov	x5,0
	bl	ui_label
	mov	x1,x24
	bl	ui_w_set_parent
	add	x0,x27,84
	add	x1,x28,DK_CT
	mov	x2,106
	mov	x3,DK_BTN
	mov	x4,1
	mov	x5,DOCK_MAXDESKS
	adr	x6,dl_ndesks
	ldr	x6,[x6]
	bl	ui_numeric
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_w_nombre
	str	x0,[x1]
	adr	x1,dk_clic_nombre
	bl	dk_on
	sub	x25,x21,DK_CT+40+4		; x25 : une ligne de nom (32 au plus)
	mov	x0,DOCK_MAXDESKS
	udiv	x25,x25,x0
	mov	x0,32
	cmp	x25,x0
	csel	x25,x0,x25,hi
	mov	x26,0
.nom:	adr	x0,dk_s_num			; son numero
	add	w1,w26,'1'
	strb	w1,[x0]
	strb	wzr,[x0,1]
	add	x0,x27,10
	add	x1,x28,DK_CT+44
	madd	x1,x25,x26,x1
	mov	x2,20
	mov	x3,20
	adr	x4,dk_s_num
	mov	x5,0
	bl	ui_label
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_w_numero
	str	x0,[x1,x26,lsl 3]
	add	x0,x27,30			; son nom
	add	x1,x28,DK_CT+40
	madd	x1,x25,x26,x1
	mov	x2,158
	sub	x3,x25,2
	mov	x4,28
	cmp	x3,x4
	csel	x3,x4,x3,hi
	mov	x4,0
	bl	ui_textbox
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_w_nomb
	str	x0,[x1,x26,lsl 3]
	adr	x1,dk_t_nom
	bl	dk_tip
	add	x26,x26,1
	cmp	x26,DOCK_MAXDESKS
	b.lo	.nom

	; ---- les lanceurs
	mov	x27,SIDE_W+10
	add	x28,x28,x22
	sub	x28,x28,6			; x28 : y du groupe
	mov	x0,x27
	mov	x1,x28
	sub	x2,x19,20
	mov	x3,x23
	adr	x4,dk_s_lanceurs
	bl	tr4
	bl	ui_groupbox
	bl	dk_top
	mov	x24,x0
	sub	x25,x23,38			; x25 : hauteur des listes
	add	x28,x28,DK_CT
	add	x0,x27,10
	mov	x1,x28
	mov	x2,200
	mov	x3,x25
	mov	x4,0
	bl	ui_list
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_w_lanceurs
	str	x0,[x1]
	add	x26,x25,4			; x26 : une ligne de boutons (32 au plus)
	mov	x0,3
	udiv	x26,x26,x0
	mov	x0,32
	cmp	x26,x0
	csel	x26,x0,x26,hi
	add	x0,x27,218			; < Ajouter
	mov	x1,x28
	mov	x2,96
	sub	x3,x26,4
	adr	x4,dk_s_ajouter
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_clic_ajouter
	bl	dk_on
	add	x0,x27,218			; Retirer
	add	x1,x28,x26
	mov	x2,96
	sub	x3,x26,4
	adr	x4,dk_s_retirer
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_clic_retirer
	bl	dk_on
	add	x0,x27,218			; ^
	add	x1,x28,x26,lsl 1
	mov	x2,46
	sub	x3,x26,4
	adr	x4,dk_s_haut
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_clic_lhaut
	bl	dk_on
	add	x0,x27,268			; v
	add	x1,x28,x26,lsl 1
	mov	x2,46
	sub	x3,x26,4
	adr	x4,dk_s_bas
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_clic_lbas
	bl	dk_on
	add	x0,x27,322			; toutes les applications
	mov	x1,x28
	sub	x2,x19,352
	mov	x3,x25
	mov	x4,0
	bl	ui_list
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_w_toutes
	str	x0,[x1]
	adr	x1,dk_clic_toutes
	bl	dk_on
	adr	x1,dk_t_toutes
	bl	dk_tip

	; ---- le message, Appliquer, Retablir
	add	x24,x20,AP_HEAD			; x24 : y des boutons
	sub	x24,x24,42
	mov	x0,SIDE_W+12
	add	x1,x24,6
	sub	x2,x19,226
	mov	x3,20
	adr	x4,dk_s_vide
	mov	x5,ULB_DIM
	bl	ui_label
	bl	dk_top
	adr	x1,dk_w_message
	str	x0,[x1]
	add	x27,x19,SIDE_W			; x27 : bord droit du volet
	sub	x0,x27,196
	mov	x1,x24
	mov	x2,90
	mov	x3,32
	adr	x4,dk_s_appliquer
	mov	x5,UB_DEFAULT
	bl	ui_pushbutton
	bl	dk_top
	adr	x1,dk_clic_appliquer
	bl	dk_on
	sub	x0,x27,100
	mov	x1,x24
	mov	x2,90
	mov	x3,32
	adr	x4,dk_s_retablir
	mov	x5,0
	bl	ui_pushbutton
	bl	dk_top
	adr	x1,dk_clic_retablir
	bl	dk_on

	; ---- (ARMPI) dans l'en-tete : la taille, la corbeille
	sub	x0,x27,110
	mov	x1,10
	mov	x2,100
	mov	x3,24
	adr	x4,dk_s_corb
	bl	tr4
	adr	x5,dl_corbeille
	ldr	x5,[x5]
	bl	ui_checkbox
	bl	dk_top
	adr	x1,dk_w_corb
	str	x0,[x1]
	adr	x1,dk_clic_corb
	bl	dk_on
	sub	x0,x27,262
	mov	x1,10
	mov	x2,142
	mov	x3,26
	adr	x4,dk_s_tailles
	bl	tr4
	bl	ui_dropdown
	bl	dk_top
	adr	x1,dk_w_taille
	str	x0,[x1]
	adr	x1,dk_clic_taille
	bl	dk_on
	sub	x0,x27,314
	mov	x1,13
	mov	x2,48
	mov	x3,20
	adr	x4,dk_s_taille
	mov	x5,0
	bl	ui_label
	bl	dk_top
	bl	dk_tout
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; on quitte l'applet : ses widgets s'en vont
dk_fermer:
	prologue
	str	x19,[sp,-16]!
	mov	x19,0
.w:	adr	x0,dk_ntop
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	adr	x0,dk_tops
	ldr	x0,[x0,x19,lsl 3]
	bl	ui_w_destroy
	add	x19,x19,1
	b	.w
.out:	adr	x0,dk_ntop
	str	xzr,[x0]
	ldr	x19,[sp],16
	epilogue

dk_dessin:
	prologue
	mov	x0,0
	bl	ui_w_draw
	epilogue

; ---------------------------------------------------------------------------
;  Les listes
; ---------------------------------------------------------------------------

; x0 = nom d'application -> x0 = son libelle (son nom si elle n'y est pas)
dk_libelle:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	app_find
	tbnz	x0,63,.nom
	bl	app_label
	b	.out
.nom:	mov	x0,x19
.out:	ldr	x19,[sp],16
	epilogue

; x0 = choix : les tiroirs, une ligne par categorie qui a des applications
dk_remplir_tiroirs:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x19,0
	mov	x20,0
.v:	adr	x0,dl_ncats			; les categories montrees
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.vd
	adr	x0,dl_napps
	ldrb	w0,[x0,x19]
	cbz	w0,.vn
	adr	x0,dk_vis
	strb	w19,[x0,x20]
	add	x20,x20,1
.vn:	add	x19,x19,1
	b	.v
.vd:	adr	x0,dk_nvis
	str	x20,[x0]
	adr	x0,dk_w_tiroirs
	ldr	x0,[x0]
	bl	ui_list_clear
	mov	x19,0
.r:	adr	x0,dk_nvis
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.sel
	adr	x0,dk_vis
	ldrb	w20,[x0,x19]
	adr	x0,dk_txt			; « categorie : application »
	strb	wzr,[x0]
	str	x0,[sp,-16]!
	mov	x0,x20
	bl	dl_nom
	mov	x1,x0
	ldr	x0,[sp],16
	bl	str_cat
	adr	x1,dk_s_deuxp
	bl	str_cat
	mov	x22,x0
	adr	x0,dl_app
	add	x0,x0,x20,lsl 5
	ldrb	w1,[x0]
	cbz	w1,.prem
	bl	dk_libelle
	b	.lib
.prem:	mov	x0,x20				; (aucune choisie : la premiere par nom)
	bl	dl_main
	adr	x1,dk_s_inconnue
	tbnz	x0,63,.lib1
	bl	app_label
.lib:	mov	x1,x0
.lib1:	mov	x0,x22
	bl	str_cat
	adr	x1,dl_hidden
	ldrb	w1,[x1,x20]
	cbz	w1,.add
	adr	x1,dk_s_masquee
	bl	tr_cat
.add:	adr	x0,dk_w_tiroirs
	ldr	x0,[x0]
	adr	x1,dk_txt
	mov	x2,x19
	bl	ui_list_add
	add	x19,x19,1
	b	.r
.sel:	adr	x0,dk_nvis			; le choix
	ldr	x0,[x0]
	sub	x0,x0,1
	cmp	x21,x0
	csel	x1,x0,x21,ge
	adr	x0,dk_w_tiroirs
	ldr	x0,[x0]
	bl	ui_w_set_value
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = la ligne (dl_*) du tiroir choisi (-1 : aucun)
dk_cur:
	prologue
	adr	x0,dk_w_tiroirs
	ldr	x0,[x0]
	bl	ui_w_get_value
	tbnz	x0,63,.non
	adr	x1,dk_nvis
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.non
	adr	x1,dk_vis
	ldrb	w0,[x1,x0]
	b	.out
.non:	mov	x0,-1
.out:	epilogue

; les applications du tiroir choisi, son « Masque »
dk_remplir_princ:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,dk_w_princ
	ldr	x0,[x0]
	bl	ui_list_clear
	adr	x0,dk_ncatapps
	str	xzr,[x0]
	bl	dk_cur
	mov	x19,x0
	mov	x1,0
	tbnz	x19,63,.cb
	adr	x0,dl_hidden
	ldrb	w1,[x0,x19]
.cb:	adr	x0,dk_w_masque
	ldr	x0,[x0]
	bl	ui_w_set_value
	mov	x22,-1				; x22 : celle choisie
	tbnz	x19,63,.sel
	mov	x20,0
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.sel
	adr	x0,ap_ordre
	ldrb	w21,[x0,x20]
	add	x20,x20,1
	adr	x0,ap_cat
	ldrb	w0,[x0,x21]
	adr	x1,dl_cat
	ldrb	w1,[x1,x19]
	cmp	x0,x1
	b.ne	.a
	adr	x0,dk_ncatapps
	ldr	x1,[x0]
	add	x2,x1,1
	str	x2,[x0]
	adr	x0,dk_catapps
	strb	w21,[x0,x1]
	adr	x0,ap_nom			; la choisie ?
	add	x0,x0,x21,lsl 5
	adr	x1,dl_app
	add	x1,x1,x19,lsl 5
	bl	str_eq
	cbz	x0,.ad
	adr	x0,dk_ncatapps
	ldr	x22,[x0]
	sub	x22,x22,1
.ad:	mov	x0,x21
	bl	app_label
	mov	x1,x0
	adr	x0,dk_w_princ
	ldr	x0,[x0]
	mov	x2,0
	bl	ui_list_add
	b	.a
.sel:	mov	x1,x22
	tbz	x22,63,.set
	adr	x0,dk_ncatapps
	ldr	x0,[x0]
	cbz	x0,.set
	mov	x1,0
.set:	adr	x0,dk_w_princ
	ldr	x0,[x0]
	bl	ui_w_set_value
	adr	x0,dk_w_princ
	ldr	x0,[x0]
	bl	ui_w_draw
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = choix : les lanceurs
dk_remplir_lanceurs:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x0
	adr	x0,dk_w_lanceurs
	ldr	x0,[x0]
	bl	ui_list_clear
	mov	x19,0
.l:	adr	x0,dl_nlan
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.sel
	adr	x0,dl_lan
	add	x0,x0,x19,lsl 5
	bl	dk_libelle
	mov	x1,x0
	adr	x0,dk_w_lanceurs
	ldr	x0,[x0]
	mov	x2,0
	bl	ui_list_add
	add	x19,x19,1
	b	.l
.sel:	adr	x0,dl_nlan
	ldr	x0,[x0]
	sub	x0,x0,1
	cmp	x20,x0
	csel	x1,x0,x20,ge
	adr	x0,dk_w_lanceurs
	ldr	x0,[x0]
	bl	ui_w_set_value
	adr	x0,dk_w_lanceurs
	ldr	x0,[x0]
	bl	ui_w_draw
	ldp	x19,x20,[sp],16
	epilogue

; les bureaux montres, leurs noms ; le volet a redessiner (ceux caches)
dk_montrer_bureaux:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
.d:	adr	x0,dl_ndesks
	ldr	x0,[x0]
	cmp	x19,x0
	cset	x20,lo
	adr	x0,dk_w_numero
	ldr	x0,[x0,x19,lsl 3]
	mov	x1,x20
	bl	ui_w_show
	adr	x0,dk_w_nomb
	ldr	x0,[x0,x19,lsl 3]
	mov	x1,x20
	bl	ui_w_show
	cbz	x20,.n
	mov	x0,DL_DESK
	adr	x1,dl_desk
	madd	x1,x19,x0,x1
	adr	x0,dk_w_nomb
	ldr	x0,[x0,x19,lsl 3]
	bl	ui_w_set_text
.n:	add	x19,x19,1
	cmp	x19,DOCK_MAXDESKS
	b.lo	.d
	adr	x0,dk_w_nombre
	ldr	x0,[x0]
	adr	x1,dl_ndesks
	ldr	x1,[x1]
	bl	ui_w_set_value
	adr	x0,ap_redraw
	mov	x1,1
	str	x1,[x0]
	ldp	x19,x20,[sp],16
	epilogue

; tout d'apres dl_* (a l'ouverture, Retablir)
dk_tout:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,dk_w_toutes			; toutes les applications, par nom,
	ldr	x0,[x0]				; sauf les parties du bureau
	bl	ui_list_clear
	adr	x0,dk_nall
	str	xzr,[x0]
	mov	x19,0
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.fin
	adr	x0,ap_ordre
	ldrb	w20,[x0,x19]
	add	x19,x19,1
	adr	x0,ap_cat
	ldrb	w0,[x0,x20]
	mov	x1,CA_NOM
	adr	x2,ca_nom
	madd	x0,x0,x1,x2
	adr	x1,dk_s_shell
	bl	str_ci_cmp
	cbz	x0,.a
	adr	x0,dk_nall
	ldr	x1,[x0]
	add	x2,x1,1
	str	x2,[x0]
	adr	x0,dk_all
	strb	w20,[x0,x1]
	mov	x0,x20
	bl	app_label
	mov	x1,x0
	adr	x0,dk_w_toutes
	ldr	x0,[x0]
	mov	x2,0
	bl	ui_list_add
	b	.a
.fin:	mov	x0,0
	bl	dk_remplir_tiroirs
	bl	dk_remplir_princ
	mov	x0,0
	bl	dk_remplir_lanceurs
	bl	dk_montrer_bureaux
	adr	x0,dl_taille			; (ARMPI)
	ldr	x1,[x0]
	add	x1,x1,1
	adr	x0,dk_w_taille
	ldr	x0,[x0]
	bl	ui_w_set_value
	adr	x0,dk_w_corb
	ldr	x0,[x0]
	adr	x1,dl_corbeille
	ldr	x1,[x1]
	bl	ui_w_set_value
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte : le message, en bas
dk_message:
	prologue
	bl	ui_tr
	mov	x1,x0
	adr	x0,dk_w_message
	ldr	x0,[x0]
	bl	ui_w_set_text
	adr	x0,ap_redraw
	mov	x1,1
	str	x1,[x0]
	epilogue

; ---------------------------------------------------------------------------
;  Les actions (ui_w_on : x0 = widget, x1 = code)
; ---------------------------------------------------------------------------

dk_clic_tiroir:
	cmp	x1,UN_CHANGE
	b.eq	dk_remplir_princ
	ret

dk_clic_princ:
	cmp	x1,UN_CHANGE
	b.ne	.ret
	prologue
	stp	x19,x20,[sp,-16]!
	bl	dk_cur
	mov	x19,x0
	tbnz	x19,63,.out
	adr	x0,dk_w_princ
	ldr	x0,[x0]
	bl	ui_w_get_value
	tbnz	x0,63,.out
	adr	x1,dk_ncatapps
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.out
	adr	x1,dk_catapps
	ldrb	w0,[x1,x0]
	adr	x1,ap_nom
	add	x1,x1,x0,lsl 5
	adr	x0,dl_app
	add	x0,x0,x19,lsl 5
	mov	x2,AP_NOM
	bl	str_copy_n
	bl	dk_refaire_tiroirs
.out:	ldp	x19,x20,[sp],16
	epilogue
.ret:	ret

; les tiroirs, le meme choisi
dk_refaire_tiroirs:
	prologue
	adr	x0,dk_w_tiroirs
	ldr	x0,[x0]
	bl	ui_w_get_value
	bl	dk_remplir_tiroirs
	epilogue

dk_clic_masque:
	cmp	x1,UN_CHANGE
	b.ne	.ret
	prologue
	str	x19,[sp,-16]!
	bl	dk_cur
	mov	x19,x0
	tbnz	x19,63,.out
	adr	x0,dk_w_masque
	ldr	x0,[x0]
	bl	ui_w_get_value
	adr	x1,dl_hidden
	strb	w0,[x1,x19]
	bl	dk_refaire_tiroirs
.out:	ldr	x19,[sp],16
	epilogue
.ret:	ret

; x0 = sens : le tiroir choisi, une place plus loin (parmi ceux montres : les
; categories sans application gardent la leur)
dk_bouger:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,dk_w_tiroirs
	ldr	x0,[x0]
	bl	ui_w_get_value
	tbnz	x0,63,.out
	add	x20,x0,x19			; x20 : sa nouvelle place
	tbnz	x20,63,.out
	adr	x1,dk_nvis
	ldr	x1,[x1]
	cmp	x20,x1
	b.hs	.out
	adr	x1,dk_vis
	ldrb	w0,[x1,x0]
	ldrb	w1,[x1,x20]
	bl	dl_swap
	mov	x0,x20
	bl	dk_remplir_tiroirs
.out:	ldp	x19,x20,[sp],16
	epilogue

dk_clic_gauche:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	mov	x0,-1
	b	dk_bouger
.ret:	ret

dk_clic_droite:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	mov	x0,1
	b	dk_bouger
.ret:	ret

; (bouton « < Ajouter », double-clic dans la liste de toutes)
dk_clic_toutes:
dk_clic_ajouter:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	prologue
	adr	x0,dk_w_toutes
	ldr	x0,[x0]
	bl	ui_w_get_value
	tbnz	x0,63,.choisir
	adr	x1,dk_nall
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.choisir
	adr	x1,dl_nlan
	ldr	x2,[x1]
	cmp	x2,DOCK_MAXLAUNCHERS
	b.hs	.plein
	adr	x1,dk_all
	ldrb	w0,[x1,x0]
	adr	x1,ap_nom
	add	x0,x1,x0,lsl 5
	bl	dl_lan_add
	adr	x0,dl_nlan
	ldr	x0,[x0]
	sub	x0,x0,1
	bl	dk_remplir_lanceurs
	b	.out
.choisir:
	adr	x0,dk_s_choisir
	bl	dk_message
	b	.out
.plein:	adr	x0,dk_s_plein
	bl	dk_message
.out:	epilogue
.ret:	ret

dk_clic_retirer:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	prologue
	str	x19,[sp,-16]!
	adr	x0,dk_w_lanceurs
	ldr	x0,[x0]
	bl	ui_w_get_value
	mov	x19,x0
	tbnz	x19,63,.out
	adr	x1,dl_nlan
	ldr	x2,[x1]
	cmp	x19,x2
	b.hs	.out
	sub	x2,x2,1
	str	x2,[x1]
	adr	x3,dl_lan
	add	x3,x3,x19,lsl 5
	sub	x2,x2,x19
	lsl	x2,x2,5
.c:	cbz	x2,.fin				; les suivants remontent
	ldrb	w4,[x3,AP_NOM]
	strb	w4,[x3],1
	sub	x2,x2,1
	b	.c
.fin:	mov	x0,x19
	bl	dk_remplir_lanceurs
.out:	ldr	x19,[sp],16
	epilogue
.ret:	ret

; x0 = sens : le lanceur choisi, une place plus loin
dk_bouger_lanceur:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,dk_w_lanceurs
	ldr	x0,[x0]
	bl	ui_w_get_value
	tbnz	x0,63,.out
	add	x19,x0,x19			; x19 : sa nouvelle place
	tbnz	x19,63,.out
	adr	x1,dl_nlan
	ldr	x1,[x1]
	cmp	x19,x1
	b.hs	.out
	cmp	x0,x1
	b.hs	.out
	adr	x1,dl_lan
	add	x2,x1,x0,lsl 5
	add	x3,x1,x19,lsl 5
	mov	x4,AP_NOM/8
.c:	ldr	x5,[x2]
	ldr	x6,[x3]
	str	x6,[x2],8
	str	x5,[x3],8
	subs	x4,x4,1
	b.ne	.c
	mov	x0,x19
	bl	dk_remplir_lanceurs
.out:	ldp	x19,x20,[sp],16
	epilogue

dk_clic_lhaut:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	mov	x0,-1
	b	dk_bouger_lanceur
.ret:	ret

dk_clic_lbas:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	mov	x0,1
	b	dk_bouger_lanceur
.ret:	ret

; x0 = bureau, x1 = 1 : remplace un nom vide par son numero : son nom <- son champ
dk_lire_nom:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,dk_w_nomb
	ldr	x0,[x0,x19,lsl 3]
	bl	ui_w_get_text
	mov	x1,x0
	mov	x0,DL_DESK
	adr	x2,dl_desk
	madd	x0,x19,x0,x2
	mov	x2,DL_DESK
	str	x0,[sp,-16]!
	bl	str_copy_n
	ldr	x0,[sp],16
	bl	dk_borner_nom
	cbz	x20,.out
	mov	x0,DL_DESK
	adr	x1,dl_desk
	madd	x0,x19,x0,x1
	ldrb	w1,[x0]
	cbnz	w1,.out
	add	w1,w19,'1'
	strb	w1,[x0]
	strb	wzr,[x0,1]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom : sans les caracteres que dock.ini ne garderait pas, sans bout de
; caractere UTF-8 en fin (coupe a DL_DESK), sans espaces autour
dk_borner_nom:
	mov	x1,x0
	mov	x2,0				; x2 : apres le dernier caractere entier
.c:	ldrb	w3,[x1]
	cbz	w3,.fin
	cmp	w3,'#'
	b.ne	.s
	mov	w3,'-'
	strb	w3,[x1]
.s:	add	x1,x1,1
	and	w4,w3,0xC0
	cmp	w4,0x80
	b.eq	.c
	cmp	w3,0x80
	b.hs	.multi
	mov	x2,x1
	b	.c
.multi:	mov	w4,2				; octets de ce caractere
	cmp	w3,0xE0
	b.lo	.m
	mov	w4,3
	cmp	w3,0xF0
	b.lo	.m
	mov	w4,4
.m:	mov	w5,1
.e:	cmp	w5,w4
	b.hs	.ok
	ldrb	w6,[x1]
	cbz	w6,.fin
	add	x1,x1,1
	add	w5,w5,1
	b	.e
.ok:	mov	x2,x1
	b	.c
.fin:	cbz	x2,.ret
	strb	wzr,[x2]
.ret:	ret

dk_clic_nombre:
	cmp	x1,UN_CHANGE
	b.ne	.ret
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,dk_w_nombre
	ldr	x0,[x0]
	bl	ui_w_get_value
	mov	x20,x0				; x20 : le nombre
	adr	x0,dl_ndesks			; ceux qu'on ajoute : leur numero
	ldr	x19,[x0]
.n:	cmp	x19,x20
	b.hs	.garde
	mov	x0,DL_DESK
	adr	x1,dl_desk
	madd	x0,x19,x0,x1
	add	w1,w19,'1'
	strb	w1,[x0]
	strb	wzr,[x0,1]
	add	x19,x19,1
	b	.n
.garde:	mov	x19,0				; les noms tapes restent
.g:	adr	x0,dl_ndesks
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.fin
	mov	x0,x19
	mov	x1,0
	bl	dk_lire_nom
	add	x19,x19,1
	b	.g
.fin:	adr	x0,dl_ndesks
	str	x20,[x0]
	bl	dk_montrer_bureaux
	ldp	x19,x20,[sp],16
	epilogue
.ret:	ret

dk_clic_taille:
	cmp	x1,UN_CHANGE
	b.ne	.ret
	prologue
	bl	ui_w_get_value
	sub	x0,x0,1
	adr	x1,dl_taille
	str	x0,[x1]
	epilogue
.ret:	ret

dk_clic_corb:
	cmp	x1,UN_CHANGE
	b.ne	.ret
	prologue
	bl	ui_w_get_value
	adr	x1,dl_corbeille
	str	x0,[x1]
	epilogue
.ret:	ret

dk_clic_retablir:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	prologue
	bl	dock_layout_load
	bl	dk_tout
	adr	x0,dk_s_vide
	bl	dk_message
	epilogue
.ret:	ret

dk_clic_appliquer:
	cmp	x1,UN_ACTIVATE
	b.eq	dk_appliquer
	ret

; ---------------------------------------------------------------------------
;  Appliquer
; ---------------------------------------------------------------------------

; x0 = texte : ajoute au fichier (x19 : fin) -> x19 avance
dk_put:
	ldrb	w1,[x0],1
	cbz	w1,.ret
	strb	w1,[x19],1
	b	dk_put
.ret:	strb	wzr,[x19]
	ret

; reecrit SD0:/etc/dock.ini, relance le dock
dk_appliquer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,0				; les noms des bureaux
.d:	adr	x0,dl_ndesks
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.un
	mov	x0,x21
	mov	x1,1
	bl	dk_lire_nom
	add	x21,x21,1
	b	.d
.un:	mov	x20,0				; un tiroir au moins
	mov	x21,0
.v:	adr	x0,dk_nvis
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.v1
	adr	x0,dk_vis
	ldrb	w0,[x0,x21]
	adr	x1,dl_hidden
	ldrb	w0,[x1,x0]
	cmp	w0,0
	cinc	x20,x20,eq
	add	x21,x21,1
	b	.v
.v1:	adr	x0,dk_s_un
	cbz	x20,.msg
	adr	x19,dk_fichier
	adr	x0,dk_entete
	bl	dk_put
	mov	x21,0				; les categories, dans l'ordre
.t:	adr	x0,dl_ncats
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.h
	adr	x0,dk_k_drawer
	bl	dk_put
	mov	x0,x21
	bl	dl_nom
	bl	dk_put
	adr	x0,dk_s_virg
	bl	dk_put
	adr	x0,dl_app
	add	x0,x0,x21,lsl 5
	bl	dk_put
	adr	x0,s_crlf
	bl	dk_put
	add	x21,x21,1
	b	.t
.h:	mov	x21,0				; celles masquees
.hc:	adr	x0,dl_ncats
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.l
	adr	x0,dl_hidden
	ldrb	w0,[x0,x21]
	cbz	w0,.hn
	adr	x0,dk_k_hidden
	bl	dk_put
	mov	x0,x21
	bl	dl_nom
	bl	dk_put
	adr	x0,s_crlf
	bl	dk_put
.hn:	add	x21,x21,1
	b	.hc
.l:	mov	x21,0				; les lanceurs
.lc:	adr	x0,dl_nlan
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.bu
	adr	x0,dk_k_launcher
	bl	dk_put
	adr	x0,dl_lan
	add	x0,x0,x21,lsl 5
	bl	dk_put
	adr	x0,s_crlf
	bl	dk_put
	add	x21,x21,1
	b	.lc
.bu:	mov	x21,0				; les bureaux
.bc:	adr	x0,dl_ndesks
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.corb
	adr	x0,dk_k_desk
	bl	dk_put
	mov	x0,DL_DESK
	adr	x1,dl_desk
	madd	x0,x21,x0,x1
	bl	dk_put
	adr	x0,s_crlf
	bl	dk_put
	add	x21,x21,1
	b	.bc
.corb:	adr	x0,dk_k_corbeille
	bl	dk_put
	adr	x0,dl_corbeille
	ldr	x0,[x0]
	adr	x1,dk_v_oui
	cbnz	x0,.co
	adr	x1,dk_v_non
.co:	mov	x0,x1
	bl	dk_put
	adr	x0,s_crlf
	bl	dk_put
	adr	x0,dk_k_taille
	bl	dk_put
	adr	x0,dl_taille
	ldr	x0,[x0]
	add	x0,x0,1
	adr	x1,dk_v_tailles
	add	x0,x1,x0,lsl 4
	bl	dk_put
	adr	x0,s_crlf
	bl	dk_put
	adr	x0,dl_p_dock
	adr	x1,dk_fichier
	bl	file_write
	tbz	x0,63,.ok
	adr	x0,dk_s_ecrire
	b	.msg
.ok:	adr	x0,n_dock
	adr	x1,p_dockp
	bl	relaunch
	adr	x0,dk_s_fait
.msg:	bl	dk_message
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

dk_titre	db 'Dock',0
dk_s_tiroirs	db 'Tiroirs',0
dk_t_tiroirs	db 'Les tiroirs du dock, de gauche ',0xC3,0xA0,' droite : un par cat',0xC3,0xA9,'gorie d',0x27,'applications',0
dk_s_haut	db '^',0
dk_s_bas	db 'v',0
dk_t_gauche	db 'Plus ',0xC3,0xA0,' gauche',0
dk_t_droite	db 'Plus ',0xC3,0xA0,' droite',0
dk_s_masque	db 'Masqu',0xC3,0xA9,0
dk_t_masque	db 'Pas de tiroir pour cette cat',0xC3,0xA9,'gorie (ses applications restent dans le menu)',0
dk_s_princ	db 'Application principale',0
dk_t_princ	db 'L',0x27,'ic',0xC3,0xB4,'ne du tiroir : un clic dessus d',0xC3,0xA9,'marre cette application',0
dk_s_deuxp	db ' : ',0
dk_s_masquee	db '  -- masqu',0xC3,0xA9,0
dk_s_inconnue	db '?',0
dk_s_bureaux	db 'Bureaux',0
dk_s_nombre	db 'Nombre',0
dk_t_nom	db 'Son nom (affich',0xC3,0xA9,' par le carr',0xC3,0xA9,' du dock)',0
dk_s_lanceurs	db 'Lanceurs (apr',0xC3,0xA8,'s les tiroirs)',0
dk_s_ajouter	db '< Ajouter',0
dk_s_retirer	db 'Retirer',0
dk_t_toutes	db 'Toutes les applications : choisissez-en une, puis < Ajouter (ou double-cliquez dessus)',0
dk_s_appliquer	db 'Appliquer',0
dk_s_retablir	db 'R',0xC3,0xA9,'tablir',0
dk_s_choisir	db 'Choisissez d',0x27,'abord une application dans la liste de droite.',0
dk_s_plein	db '6 lanceurs au plus.',0
dk_s_un		db 'Gardez au moins un tiroir.',0
dk_s_ecrire	db 'Impossible d',0x27,0xC3,0xA9,'crire SD0:/etc/dock.ini.',0
dk_s_fait	db 'Appliqu',0xC3,0xA9,' : le dock en tient compte.',0
dk_s_vide	db 0
dk_s_taille	db 'Taille',0
dk_s_tailles	db 'Automatique|Grande|Moyenne|Petite',0
dk_s_corb	db 'Corbeille',0
dk_s_shell	db 'Shell',0
dk_v_tailles	db 'auto',0,0,0,0,0,0,0,0,0,0,0,0
		db 'grande',0,0,0,0,0,0,0,0,0,0
		db 'moyenne',0,0,0,0,0,0,0,0,0
		db 'petite',0,0,0,0,0,0,0,0,0,0
dk_k_drawer	db 'drawer    = ',0
dk_k_hidden	db 'hidden    = ',0
dk_k_launcher	db 'launcher  = ',0
dk_k_desk	db 'desk      = ',0
dk_k_corbeille	db 'corbeille = ',0
dk_k_taille	db 'taille    = ',0
dk_s_virg	db ', ',0
dk_v_oui	db 'oui',0
dk_v_non	db 'non',0
dk_entete	db '# Le dock (SD0:/sys/dock), comme le dock.ini d',0x27,'Onyx ; ecrit par le',13,10
		db '# panneau de configuration (applet Dock).',13,10
		db '# drawer    = categorie, application principale  (dans l',0x27,'ordre du dock ;',13,10
		db '#             une categorie non nommee vient a la fin)',13,10
		db '# hidden    = categorie                          (sans tiroir)',13,10
		db '# launcher  = application                        (apres les tiroirs)',13,10
		db '# desk      = nom                                (bureaux, 1 a 6)',13,10
		db '# corbeille = oui | non',13,10
		db '# taille    = auto | grande | moyenne | petite',13,10,0
		align 4

macro dk_data {
		align 8
dk_ntop		dd 0				; widgets a retirer en quittant l'applet
dk_tops		rd 16
dk_w_tiroirs	dd 0				; les widgets qu'on relit
dk_w_princ	dd 0
dk_w_masque	dd 0
dk_w_nombre	dd 0
dk_w_lanceurs	dd 0
dk_w_toutes	dd 0
dk_w_message	dd 0
dk_w_taille	dd 0
dk_w_corb	dd 0
dk_w_numero	rd DOCK_MAXDESKS
dk_w_nomb	rd DOCK_MAXDESKS
dk_nvis		dd 0				; lignes de la liste -> dl_*
dk_ncatapps	dd 0				; les applications du tiroir choisi
dk_nall		dd 0				; toutes, sauf les parties du bureau
dk_vis		rb CA_MAX
dk_catapps	rb AP_MAX
dk_all		rb AP_MAX
dk_s_num	rb 2
dk_txt		rb DK_TXT + CA_NOM + AP_LIB
		align 8
dk_fichier	rb 4096
}
