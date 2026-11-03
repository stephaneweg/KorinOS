; applets/panel.inc - applet Panel du panneau de configuration : les reglages de
; SD0:/etc/panel.ini (inc/panelconf.inc) en widgets de UIKit :
;   * les RACCOURCIS du panel : ajouter une application de la liste de toutes,
;     en retirer une, la deplacer ;
;   * les BUREAUX (espaces de travail) : combien (1 a 6) et leurs noms ;
;   * dans l'en-tete du volet, la TAILLE du panel et la CORBEILLE.
; Appliquer ecrit panel.ini et relance le panel ; Retablir relit ce qui est
; enregistre. Reprend de applets/dock.inc (l'applet de l'ancien dock, masque) ses
; aides, les bureaux, la taille et la corbeille.

pnl_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	bl	applis_load			; (les applications installees depuis)
	bl	apps_sort
	bl	pan_load
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
	sub	x21,x20,54			; x21 : hauteur des groupes
	sub	x26,x19,230			; x26 : largeur du groupe des raccourcis
	mov	x27,SIDE_W+10			; x27 : son x
	mov	x28,AP_HEAD+6			; x28 : son y

	; ---- les raccourcis
	mov	x0,x27
	mov	x1,x28
	mov	x2,x26
	mov	x3,x21
	adr	x4,pnl_s_pins
	bl	tr4
	bl	ui_groupbox
	bl	dk_top
	mov	x24,x0
	sub	x22,x26,30
	lsr	x22,x22,1			; x22 : largeur d'une liste
	sub	x25,x21,DK_CT+18+DK_BTN+16	; x25 : leur hauteur
	add	x0,x27,10			; sur le panel
	add	x1,x28,DK_CT-2
	mov	x2,x22
	mov	x3,18
	adr	x4,pnl_s_sur
	mov	x5,0
	bl	ui_label
	mov	x1,x24
	bl	ui_w_set_parent
	add	x0,x27,10
	add	x1,x28,DK_CT+18
	mov	x2,x22
	mov	x3,x25
	mov	x4,0
	bl	ui_list
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_w_lanceurs
	str	x0,[x1]
	add	x23,x28,DK_CT+26
	add	x23,x23,x25			; x23 : y des boutons
	add	x0,x27,10			; Retirer
	mov	x1,x23
	sub	x2,x22,84
	mov	x3,DK_BTN
	adr	x4,dk_s_retirer
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,pnl_clic_retirer
	bl	dk_on
	add	x0,x27,x22			; ^
	sub	x0,x0,68
	mov	x1,x23
	mov	x2,36
	mov	x3,DK_BTN
	adr	x4,dk_s_haut
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,pnl_clic_gauche
	bl	dk_on
	adr	x1,dk_t_gauche
	bl	dk_tip
	add	x0,x27,x22			; v
	sub	x0,x0,28
	mov	x1,x23
	mov	x2,36
	mov	x3,DK_BTN
	adr	x4,dk_s_bas
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,pnl_clic_droite
	bl	dk_on
	adr	x1,dk_t_droite
	bl	dk_tip
	add	x0,x27,x22			; toutes les applications
	add	x0,x0,20
	add	x1,x28,DK_CT-2
	mov	x2,x22
	mov	x3,18
	adr	x4,pnl_s_toutes
	mov	x5,0
	bl	ui_label
	mov	x1,x24
	bl	ui_w_set_parent
	add	x0,x27,x22
	add	x0,x0,20
	add	x1,x28,DK_CT+18
	mov	x2,x22
	mov	x3,x25
	mov	x4,0
	bl	ui_list
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,dk_w_toutes
	str	x0,[x1]
	adr	x1,pnl_clic_ajouter
	bl	dk_on
	adr	x1,dk_t_toutes
	bl	dk_tip
	add	x0,x27,x22			; < Ajouter
	add	x0,x0,20
	mov	x1,x23
	mov	x2,x22
	mov	x3,DK_BTN
	adr	x4,dk_s_ajouter
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,pnl_clic_ajouter
	bl	dk_on

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
	adr	x1,pnl_t_nom
	bl	dk_tip
	add	x26,x26,1
	cmp	x26,DOCK_MAXDESKS
	b.lo	.nom

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
	adr	x1,pnl_clic_appliquer
	bl	dk_on
	sub	x0,x27,100
	mov	x1,x24
	mov	x2,90
	mov	x3,32
	adr	x4,dk_s_retablir
	mov	x5,0
	bl	ui_pushbutton
	bl	dk_top
	adr	x1,pnl_clic_retablir
	bl	dk_on

	; ---- dans l'en-tete, a gauche des boutons de la fenetre : la taille, la corbeille
	sub	x27,x27,FRAME_CAP_W
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
	bl	pnl_tout
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = choix : les raccourcis
pnl_remplir:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x0
	adr	x0,dk_w_lanceurs
	ldr	x0,[x0]
	bl	ui_list_clear
	mov	x19,0
.l:	adr	x0,pan_npin
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.sel
	adr	x0,pan_pin
	add	x0,x0,x19,lsl 5
	bl	dk_libelle
	mov	x1,x0
	adr	x0,dk_w_lanceurs
	ldr	x0,[x0]
	mov	x2,0
	bl	ui_list_add
	add	x19,x19,1
	b	.l
.sel:	adr	x0,pan_npin
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

; tout d'apres les reglages (a l'ouverture, Retablir)
pnl_tout:
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
	mov	x0,x20
	bl	pan_shell
	cbnz	x0,.a
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
	bl	pnl_remplir
	bl	dk_montrer_bureaux
	adr	x0,dl_taille
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

; (bouton « < Ajouter », double-clic dans la liste de toutes)
pnl_clic_ajouter:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,dk_w_toutes
	ldr	x0,[x0]
	bl	ui_w_get_value
	tbnz	x0,63,.choisir
	adr	x1,dk_nall
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.choisir
	adr	x1,dk_all
	ldrb	w0,[x1,x0]
	adr	x1,ap_nom
	add	x19,x1,x0,lsl 5			; x19 : son nom
	mov	x20,0				; deja la ?
.d:	adr	x0,pan_npin
	ldr	x1,[x0]
	cmp	x20,x1
	b.hs	.add
	adr	x0,pan_pin
	add	x0,x0,x20,lsl 5
	mov	x1,x19
	bl	str_eq
	cbnz	x0,.deja
	add	x20,x20,1
	b	.d
.add:	cmp	x1,PANEL_MAXPINS
	b.hs	.plein
	mov	x0,x19
	bl	pan_add
	adr	x0,pan_npin
	ldr	x0,[x0]
	sub	x0,x0,1
	bl	pnl_remplir
	b	.out
.deja:	adr	x0,pnl_s_deja
	bl	dk_message
	b	.out
.choisir:
	adr	x0,dk_s_choisir
	bl	dk_message
	b	.out
.plein:	adr	x0,pnl_s_plein
	bl	dk_message
.out:	ldp	x19,x20,[sp],16
	epilogue
.ret:	ret

pnl_clic_retirer:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	prologue
	str	x19,[sp,-16]!
	adr	x0,dk_w_lanceurs
	ldr	x0,[x0]
	bl	ui_w_get_value
	mov	x19,x0
	tbnz	x19,63,.out
	adr	x1,pan_npin
	ldr	x2,[x1]
	cmp	x19,x2
	b.hs	.out
	sub	x2,x2,1
	str	x2,[x1]
	adr	x3,pan_pin
	add	x3,x3,x19,lsl 5
	sub	x2,x2,x19
	lsl	x2,x2,5
.c:	cbz	x2,.fin				; les suivants remontent
	ldrb	w4,[x3,AP_NOM]
	strb	w4,[x3],1
	sub	x2,x2,1
	b	.c
.fin:	mov	x0,x19
	bl	pnl_remplir
.out:	ldr	x19,[sp],16
	epilogue
.ret:	ret

; x0 = sens : le raccourci choisi, une place plus loin
pnl_bouger:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,dk_w_lanceurs
	ldr	x0,[x0]
	bl	ui_w_get_value
	tbnz	x0,63,.out
	add	x19,x0,x19			; x19 : sa nouvelle place
	tbnz	x19,63,.out
	adr	x1,pan_npin
	ldr	x1,[x1]
	cmp	x19,x1
	b.hs	.out
	cmp	x0,x1
	b.hs	.out
	adr	x1,pan_pin
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
	bl	pnl_remplir
.out:	ldp	x19,x20,[sp],16
	epilogue

pnl_clic_gauche:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	mov	x0,-1
	b	pnl_bouger
.ret:	ret

pnl_clic_droite:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	mov	x0,1
	b	pnl_bouger
.ret:	ret

pnl_clic_retablir:
	cmp	x1,UN_ACTIVATE
	b.ne	.ret
	prologue
	bl	pan_load
	bl	pnl_tout
	adr	x0,dk_s_vide
	bl	dk_message
	epilogue
.ret:	ret

pnl_clic_appliquer:
	cmp	x1,UN_ACTIVATE
	b.eq	pnl_appliquer
	ret

; reecrit SD0:/etc/panel.ini, relance le panel
pnl_appliquer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,0				; les noms des bureaux
.d:	adr	x0,dl_ndesks
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.go
	mov	x0,x21
	mov	x1,1
	bl	dk_lire_nom
	add	x21,x21,1
	b	.d
.go:	adr	x19,dk_fichier
	adr	x0,pnl_entete
	bl	dk_put
	mov	x21,0				; les raccourcis
.p:	adr	x0,pan_npin
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.bu
	adr	x0,pnl_k_pin
	bl	dk_put
	adr	x0,pan_pin
	add	x0,x0,x21,lsl 5
	bl	dk_put
	adr	x0,s_crlf
	bl	dk_put
	add	x21,x21,1
	b	.p
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
	adr	x0,pan_p_ini
	adr	x1,dk_fichier
	bl	file_write
	tbz	x0,63,.ok
	adr	x0,pnl_s_ecrire
	b	.msg
.ok:	adr	x0,n_dock
	adr	x1,p_dockp
	bl	relaunch
	adr	x0,pnl_s_fait
.msg:	bl	dk_message
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

pnl_titre	db 'Panel',0
pnl_s_pins	db 'Raccourcis',0
pnl_s_sur	db 'Sur le panel',0
pnl_s_toutes	db 'Toutes les applications',0
pnl_t_nom	db 'Son nom (affich',0xC3,0xA9,' par le carr',0xC3,0xA9,' du panel)',0
pnl_s_deja	db 'Cette application a d',0xC3,0xA9,'j',0xC3,0xA0,' un raccourci.',0
pnl_s_plein	db '12 raccourcis au plus.',0
pnl_s_ecrire	db 'Impossible d',0x27,0xC3,0xA9,'crire SD0:/etc/panel.ini.',0
pnl_s_fait	db 'Appliqu',0xC3,0xA9,' : le panel en tient compte.',0
pnl_k_pin	db 'pin       = ',0
pnl_entete	db '# Le panel (SD0:/sys/panel) ; ecrit par le panneau de configuration',13,10
		db '# (applet Panel).',13,10
		db '# pin       = application       (raccourcis, 12 au plus)',13,10
		db '# desk      = nom               (bureaux, 1 a 6)',13,10
		db '# corbeille = oui | non',13,10
		db '# taille    = auto | grande | moyenne | petite',13,10,0
		align 4
