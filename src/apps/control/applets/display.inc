; applets/display.inc - applet Ecran du panneau de configuration : la resolution
; de l'ecran, changee a chaud (elegant/screen.inc) comme displayconf d'Onyx. La
; liste vient d'Elegant : sa table, les modes de l'ecran d'apres son EDID, la
; taille actuelle. Appliquer : l'ecran change tout de suite, puis « Garder
; cette resolution ? » : sans reponse en 15 s, ou sur Revenir, la taille d'avant
; revient (Elegant le fait aussi seul, panneau mort ou ecran noir). Gardee :
; « ecran=LxH » dans SD0:/etc/options.txt, lu par le noyau au demarrage.
; Section « Interface » (docs/MODE-POCHE.md) : deux cartes illustrees, Bureau
; et Poche ; la taille des controles, Normale ou Tactile (« density = normal |
; touch » dans SD0:/etc/theme.txt, lue par UIKit ; Appliquer l'ecrit et publie
; korin.theme a nouveau) ; « Appliquer et relancer » ecrit « shell = desktop |
; poche » dans SD0:/etc/system.ini (lu par Elegant au demarrage) puis
; redemarre (init2, INIT_OP_REBOOT : en premier, un redemarrage complet
; relance la session).

EC_MAX		= WS_SM_N
EC_LABEL	= 32			; un libelle
EC_KEEP_MS	= 15000			; le temps de repondre
EC_WAIT_MS	= 17000			; ... puis Elegant revient de lui-meme
EC_DLG_W	= 440
EC_DLG_H	= 132
EC_IF_Y		= 154			; ligne de base du titre « Interface »
EC_CARD_Y	= EC_IF_Y + 12
EC_CARD_H	= 128
EC_VIG_W	= 112			; vignette
EC_VIG_H	= 72
EC_DENS_Y	= EC_CARD_Y + EC_CARD_H + 14
EC_SEG_W	= 220
EC_SEG_H	= 30
EC_WARN_Y	= EC_DENS_Y + EC_SEG_H + 22	; ligne de base de l'avertissement
EC_BTN_Y	= EC_WARN_Y + 12
EC_BTN_W	= 200
EC_BTN_H	= 30

; x0 = op, x1..x3 = a, b, c -> x0 = resultat (reponse dans ec_msg)
ec_call:
	prologue
	mov	x4,x0
	adr	x0,ec_msg
	str	x4,[x0,WM_OP]
	stp	x1,x2,[x0,WM_A]
	str	x3,[x0,WM_C]
	str	wzr,[x0,WM_LEN]
	sys	SYS_WS_CALL
	epilogue

; x0 = fin de chaine, x1 = nombre -> x0 = apres les chiffres (0 final mis)
ec_dec:
	mov	x2,1
	mov	x3,10
.p:	mul	x4,x2,x3
	cmp	x4,x1
	b.hi	.d
	mov	x2,x4
	b	.p
.d:	udiv	x4,x1,x2
	msub	x1,x4,x2,x1
	add	w4,w4,'0'
	strb	w4,[x0],1
	udiv	x2,x2,x3
	cbnz	x2,.d
	strb	wzr,[x0]
	ret

; x0 = fin de chaine, x1 = chaine -> x0 = sur le 0 final
ec_str:
	ldrb	w2,[x1],1
	strb	w2,[x0]
	cbz	w2,.ret
	add	x0,x0,1
	b	ec_str
.ret:	ret

; x0 = fin de chaine, x1 = largeur, x2 = hauteur : « LxH » -> x0 = sur le 0 final
ec_size:
	prologue
	str	x2,[sp,-16]!
	bl	ec_dec
	mov	w1,'x'
	strb	w1,[x0],1
	ldr	x1,[sp],16
	bl	ec_dec
	epilogue

ec_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,ec_n
	str	xzr,[x0]
	mov	x0,WS_OP_SCREEN_MODES
	mov	x1,0
	mov	x2,0
	mov	x3,0
	bl	ec_call
	cmp	x0,0
	b.le	.head
	cmp	x0,EC_MAX
	mov	x1,EC_MAX
	csel	x0,x1,x0,hi
	adr	x1,ec_n
	str	x0,[x1]
	adr	x9,ec_msg
	ldp	x1,x2,[x9,WM_C]			; taille actuelle
	adr	x3,ec_cur
	stp	x1,x2,[x3]
	add	x1,x9,WM_DATA			; les modes
	adr	x2,ec_modes
	lsl	x0,x0,3
.cp:	ldr	x3,[x1],8
	str	x3,[x2],8
	subs	x0,x0,8
	b.ne	.cp
.head:	mov	x0,0
	adr	x1,ec_s_note
	bl	fm_debut
	adr	x0,ec_s_titre
	bl	fm_ligne
	mov	x19,0
.r:	adr	x0,ec_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.fin
	adr	x20,ec_labels			; le libelle
	add	x20,x20,x19,lsl 5
	adr	x21,ec_modes
	add	x21,x21,x19,lsl 3
	mov	x0,x20
	ldrh	w1,[x21,WSM_W]
	ldrh	w2,[x21,WSM_H]
	bl	ec_size
	ldr	w2,[x21,WSM_FLAGS]
	adr	x1,ec_s_native
	tbnz	w2,1,.suf			; WSMF_NATIVE
	adr	x1,ec_s_edid
	tbnz	w2,0,.suf			; WSMF_EDID
	b	.row
.suf:	bl	tr_cat
.row:	mov	x0,x19
	bl	ec_checked
	mov	x2,x0
	mov	x0,0
	mov	x1,x20
	bl	fm_item
	add	x19,x19,1
	b	.r
.fin:	bl	ec_if_ouvrir
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero de mode -> x0 = 1 si c'est la taille actuelle
ec_checked:
	adr	x1,ec_modes
	add	x1,x1,x0,lsl 3
	ldrh	w2,[x1,WSM_W]
	ldrh	w3,[x1,WSM_H]
	adr	x1,ec_cur
	ldp	x4,x5,[x1]
	cmp	x2,x4
	ccmp	x3,x5,0,eq
	cset	x0,eq
	ret

; la liste revient sur la taille actuelle
ec_recheck:
	prologue
	str	x19,[sp,-16]!
	mov	x0,0
	mov	x1,-1
	bl	fm_choisir
	mov	x19,0
.r:	adr	x0,ec_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	mov	x0,x19
	bl	ec_checked
	add	x19,x19,1
	cbz	x0,.r
	mov	x0,0
	sub	x1,x19,1
	bl	fm_choisir
.out:	ldr	x19,[sp],16
	epilogue

; Appliquer : la taille choisie tout de suite, gardee si l'utilisateur le dit
ec_appliquer:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,0
	bl	fm_valeur
	tbnz	x0,63,.out
	adr	x1,ec_modes
	add	x1,x1,x0,lsl 3
	ldrh	w19,[x1,WSM_W]
	ldrh	w20,[x1,WSM_H]
	adr	x0,ec_cur			; deja a cette taille : rien a confirmer
	ldp	x0,x1,[x0]
	cmp	x0,x19
	ccmp	x1,x20,0,eq
	b.eq	.save
	mov	x0,WS_OP_SCREEN_SET
	mov	x1,x19
	mov	x2,x20
	movl	x3,EC_WAIT_MS
	bl	ec_call
	adr	x1,ec_s_refus
	tbnz	x0,63,.msg
	bl	ec_confirm
	mov	x1,x0
	cmp	x1,1
	b.eq	.keep
	mov	x0,WS_OP_SCREEN_KEEP		; (Revenir, ou plus de temps)
	mov	x2,0
	mov	x3,0
	bl	ec_call
	bl	ec_recheck
	adr	x1,ec_s_retour
	b	.msg
.keep:	mov	x0,WS_OP_SCREEN_KEEP
	mov	x2,0
	mov	x3,0
	bl	ec_call
	adr	x0,ec_cur
	stp	x19,x20,[x0]
.save:	adr	x0,ec_value
	mov	x1,x19
	mov	x2,x20
	bl	ec_size
	adr	x0,p_options
	adr	x1,ec_k_ecran
	adr	x2,ec_value
	bl	ini_set
	adr	x1,ec_s_fait
.msg:	adr	x0,ap_msg
	str	x1,[x0]
	bl	ap_fit
.out:	bl	ec_dens_appliquer		; la taille des controles
	ldp	x19,x20,[sp],16
	epilogue

; « Garder cette resolution ? » -> x0 = 1 Garder, 0 Revenir (ou le temps ecoule)
ec_confirm:
	prologue
	str	x19,[sp,-16]!
	sys	SYS_TIME_MS
	adr	x1,ec_t0
	str	x0,[x1]
	adr	x0,ec_left
	str	xzr,[x0]
	mov	x0,EC_DLG_W
	mov	x1,EC_DLG_H
	adr	x2,ec_titre
	bl	ui_dialog_begin
	cbz	x0,.out
	mov	x19,x0
	bl	ec_text
	mov	x0,16				; la question
	mov	x1,14
	mov	x2,EC_DLG_W-32
	mov	x3,24
	adr	x4,ec_line
	mov	x5,0
	bl	ui_label
	adr	x1,ec_w_label
	str	x0,[x1]
	mov	x0,16				; le temps qui reste
	mov	x1,44
	mov	x2,EC_DLG_W-32
	mov	x3,10
	movl	x4,EC_KEEP_MS
	movl	x5,EC_KEEP_MS
	bl	ui_progress
	adr	x1,ec_w_bar
	str	x0,[x1]
	mov	x0,x19
	mov	x1,0
	adr	x2,ec_s_garder
	mov	x3,1
	mov	x4,UB_DEFAULT
	mov	x5,EC_DLG_W-12
	bl	ui_dialog_button
	mov	x5,x0
	mov	x0,x19
	mov	x1,1
	adr	x2,ec_s_revenir
	mov	x3,0
	mov	x4,0
	bl	ui_dialog_button
	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_cancel
	mov	x0,x19
	adr	x1,ec_tick
	mov	x2,200
	bl	ui_dialog_timer
	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_run
	mov	x1,x0
	mov	x0,x19
	mov	x19,x1
	bl	ui_dialog_end
	mov	x0,x19
.out:	ldr	x19,[sp],16
	epilogue

; (minuteur du dialogue) x0 = dialogue
ec_tick:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	sys	SYS_TIME_MS
	adr	x1,ec_t0
	ldr	x1,[x1]
	sub	x20,x0,x1
	movl	x0,EC_KEEP_MS
	subs	x20,x0,x20			; reste
	b.le	.fin
	adr	x0,ec_w_bar
	ldr	x0,[x0]
	mov	x1,x20
	bl	ui_w_set_value
	bl	ec_text
	cbz	x0,.draw
	adr	x0,ec_w_label
	ldr	x0,[x0]
	adr	x1,ec_line
	bl	ui_w_set_text
.draw:	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	b	.out
.fin:	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_close
.out:	ldp	x19,x20,[sp],16
	epilogue

; ec_line <- la question, secondes qui restent -> x0 = 1 si elle a change
ec_text:
	prologue
	sys	SYS_TIME_MS
	adr	x1,ec_t0
	ldr	x1,[x1]
	sub	x0,x0,x1
	movl	x1,EC_KEEP_MS+999
	subs	x1,x1,x0
	csel	x1,xzr,x1,lt
	mov	x2,1000
	udiv	x1,x1,x2
	adr	x2,ec_left
	ldr	x3,[x2]
	mov	x0,0
	cmp	x1,x3
	b.eq	.out
	str	x1,[x2]
	str	x1,[sp,-16]!
	adr	x0,ec_line
	adr	x1,ec_s_q1
	bl	tr_cat
	ldr	x1,[sp],16
	bl	ec_dec
	adr	x1,ec_s_q2
	bl	ec_str
	mov	x0,1
.out:	epilogue

; ---------------------------------------------------------------------------
;  Interface : Bureau ou Poche, taille des controles
; ---------------------------------------------------------------------------

; (ouverture) l'etat des fichiers, la liste Normale | Tactile, le dessin et
; les clics de la section
ec_if_ouvrir:
	prologue
	adr	x0,ec_shell
	str	xzr,[x0]
	adr	x0,p_systeme			; shell = poche ?
	adr	x1,ec_ini_sys
	bl	ini_read
	bl	ec_dens_fichier			; density = touch ?
	adr	x1,ec_dens_lu
	str	x0,[x1]
	adr	x0,ec_shell
	ldr	x0,[x0]
	adr	x1,ec_choix
	str	x0,[x1]
	adr	x0,ui_w			; Normale | Tactile (replacee au dessin)
	ldr	x0,[x0]
	sub	x0,x0,AP_MARGE+EC_SEG_W
	mov	x1,EC_DENS_Y
	mov	x2,EC_SEG_W
	mov	x3,EC_SEG_H
	adr	x4,ec_s_seg
	bl	ui_segmented
	adr	x1,ec_w_seg
	str	x0,[x1]
	adr	x1,ec_dens_lu
	ldr	x1,[x1]
	bl	ui_w_set_value
	adr	x0,ec_w_seg
	ldr	x0,[x0]
	adr	x1,fm_change			; (un changement efface le message)
	mov	x2,0
	bl	ui_w_on
	adr	x0,ec_dessin
	adr	x1,ap_dessin
	str	x0,[x1]
	adr	x0,ec_fermer
	adr	x1,ap_fermer
	str	x0,[x1]
	adr	x0,ec_xclic
	adr	x1,ap_xclic
	str	x0,[x1]
	epilogue

; (ini_read de system.ini) x0 = cle, x1 = valeur
ec_ini_sys:
	prologue
	str	x1,[sp,-16]!
	adr	x1,ec_k_shell
	bl	str_eq
	ldr	x1,[sp],16
	cbz	x0,.out
	mov	x0,x1
	adr	x1,ec_v_poche
	bl	str_eq
	adr	x1,ec_shell
	str	x0,[x1]
.out:	epilogue

; (ini_read de theme.txt) x0 = cle, x1 = valeur
ec_ini_theme:
	prologue
	str	x1,[sp,-16]!
	adr	x1,ec_k_density
	bl	str_eq
	ldr	x1,[sp],16
	cbz	x0,.out
	mov	x0,x1
	adr	x1,ec_v_touch
	bl	str_eq
	adr	x1,ec_dens
	str	x0,[x1]
.out:	epilogue

; -> x0 = 1 si theme.txt dit « density = touch » (aussi pour l'applet Theme,
; qui reecrit le fichier)
ec_dens_fichier:
	prologue
	adr	x0,ec_dens
	str	xzr,[x0]
	adr	x0,p_ec_theme
	adr	x1,ec_ini_theme
	bl	ini_read
	adr	x0,ec_dens
	ldr	x0,[x0]
	epilogue

; (ap_fermer) la liste Normale | Tactile, puis celles des lignes
ec_fermer:
	prologue
	adr	x1,ec_w_seg
	ldr	x0,[x1]
	str	xzr,[x1]
	cbz	x0,.fm
	bl	ui_w_destroy
.fm:	bl	fm_fermer
	epilogue

; la taille des controles choisie : dans theme.txt si elle a change, puis
; korin.theme publie a nouveau (les programmes relisent le theme)
ec_dens_appliquer:
	prologue
	str	x19,[sp,-16]!
	adr	x0,ec_w_seg
	ldr	x0,[x0]
	cbz	x0,.out
	bl	ui_w_get_value
	cmp	x0,1
	cset	x19,eq
	adr	x0,ec_dens_lu
	ldr	x1,[x0]
	cmp	x1,x19
	b.eq	.out
	str	x19,[x0]
	bl	ec_dens_ecrire
	adr	x0,th_topic			; la valeur gardee de korin.theme, reprise
	adr	x1,th_bus
	mov	x2,63
	bl	bus_get
	cmp	x0,0
	b.le	.out
	cmp	x0,63
	mov	x1,63
	csel	x0,x1,x0,gt
	adr	x1,th_bus
	strb	wzr,[x1,x0]
	adr	x0,th_topic
	adr	x1,th_bus
	mov	x2,-1
	mov	x3,BUS_RETAIN
	bl	bus_publish
.out:	ldr	x19,[sp],16
	epilogue

; ec_dens_lu -> « density=touch | normal » dans theme.txt
ec_dens_ecrire:
	prologue
	adr	x2,ec_v_normal
	adr	x0,ec_dens_lu
	ldr	x0,[x0]
	cbz	x0,.w
	adr	x2,ec_v_touch
.w:	adr	x0,p_ec_theme
	adr	x1,ec_k_density
	bl	ini_set
	epilogue

; (ap_dessin) les lignes de reglages, puis la section Interface
ec_dessin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,SIDE_W+AP_MARGE		; x19 : gauche, x21 : largeur
	adr	x0,ui_w
	ldr	x21,[x0]
	sub	x21,x21,SIDE_W+2*AP_MARGE
	adr	x0,ec_w_seg			; Normale | Tactile a sa place, avant
	ldr	x0,[x0]				; que fm_dessin dessine les widgets
	cbz	x0,.lignes
	add	x1,x19,x21
	sub	x1,x1,EC_SEG_W
	mov	x2,EC_DENS_Y
	mov	x3,EC_SEG_W
	mov	x4,EC_SEG_H
	bl	ui_w_move
.lignes:
	bl	fm_dessin
	bl	ui_jetons
	mov	x23,x0
	adr	x0,ec_s_interface		; titre
	bl	ui_tr
	add	x1,x19,2
	mov	x2,EC_IF_Y
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	sub	x22,x21,12			; deux cartes
	lsr	x22,x22,1
	adr	x0,ec_cw
	str	x22,[x0]
	mov	x20,0
.carte:	cmp	x20,2
	b.hs	.dens
	add	x0,x22,12
	madd	x24,x20,x0,x19			; x de la carte
	adr	x0,ec_cx
	str	x24,[x0,x20,lsl 3]
	adr	x0,ec_choix
	ldr	x25,[x0]
	cmp	x25,x20
	cset	x25,eq				; x25 : choisie
	ldr	w5,[x23,TJ_LIGNE]		; contour (choisie : ACCENT, 2 px)
	cbz	x25,.c1
	ldr	w5,[x23,TJ_ACCENT]
.c1:	mov	w6,w5
	mov	x0,x24
	mov	x1,EC_CARD_Y
	mov	x2,x22
	mov	x3,EC_CARD_H
	mov	x4,FM_RAYON
	bl	ui_rbox
	ldr	w5,[x23,TJ_FACE]
	cbz	x25,.c2
	ldr	w5,[x23,TJ_ACCENT_CLAIR]
.c2:	mov	w6,w5
	add	x0,x24,1			; (choisie : retrait de 2)
	add	x0,x0,x25
	mov	x1,EC_CARD_Y+1
	add	x1,x1,x25
	sub	x2,x22,2
	sub	x2,x2,x25,lsl 1
	mov	x3,EC_CARD_H-2
	sub	x3,x3,x25,lsl 1
	mov	x4,FM_RAYON-1
	bl	ui_rbox
	sub	x0,x22,EC_VIG_W			; la vignette, centree
	add	x0,x24,x0,lsr 1
	mov	x1,EC_CARD_Y+12
	mov	x2,x20
	bl	ec_vignette
	adr	x0,ec_s_bureau			; nom, description
	cbz	x20,.n
	adr	x0,ec_s_poche
.n:	bl	ui_tr
	mov	x26,x0
	mov	x1,14
	adr	x2,ui_font_b
	bl	ui_text_width
	sub	x1,x22,x0
	add	x1,x24,x1,lsr 1
	mov	x0,x26
	mov	x2,EC_CARD_Y+EC_VIG_H+32
	ldr	w3,[x23,TJ_TEXTE1]
	cbz	x25,.n1
	ldr	w3,[x23,TJ_ACCENT_FONCE]
.n1:	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	adr	x0,ec_s_bureau_d
	cbz	x20,.d
	adr	x0,ec_s_poche_d
.d:	bl	ui_tr
	mov	x26,x0
	mov	x1,12
	adr	x2,ui_font
	bl	ui_text_width
	subs	x1,x22,x0
	csel	x1,xzr,x1,lt
	add	x1,x24,x1,lsr 1
	mov	x0,x26
	mov	x2,EC_CARD_Y+EC_VIG_H+50
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	sub	x6,x22,8
	bl	ui_text
	add	x20,x20,1
	b	.carte
.dens:	adr	x0,ec_s_taille			; Taille des controles : [Normale|Tactile]
	bl	ui_tr
	add	x1,x19,2
	mov	x2,EC_DENS_Y+EC_SEG_H/2+5
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	adr	x0,ec_w_seg			; (par-dessus le fond du volet)
	ldr	x0,[x0]
	cbz	x0,.warn
	bl	ui_w_draw
.warn:	adr	x0,ec_s_avert			; l'avertissement, le bouton
	bl	ui_tr
	add	x1,x19,2
	mov	x2,EC_WARN_Y
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,x21
	bl	ui_text
	adr	x5,ec_shell			; principal si le mode choisi change
	ldr	x5,[x5]
	adr	x6,ec_choix
	ldr	x6,[x6]
	cmp	x5,x6
	cset	x5,ne
	mov	x0,x19
	mov	x1,EC_BTN_Y
	mov	x2,EC_BTN_W
	mov	x3,EC_BTN_H
	adr	x4,ec_s_relancer
	bl	tr4
	bl	ui_button
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = coin, x2 = 0 bureau / 1 poche : la petite image du mode
; (x23 = jetons)
ec_vignette:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldr	w5,[x23,TJ_LIGNE_FORTE]		; l'ecran
	mov	w6,w5
	mov	x0,x19
	mov	x1,x20
	mov	x2,EC_VIG_W
	mov	x3,EC_VIG_H
	mov	x4,5
	bl	ui_rbox
	ldr	w5,[x23,TJ_CREUX]
	mov	w6,w5
	add	x0,x19,1
	add	x1,x20,1
	mov	x2,EC_VIG_W-2
	mov	x3,EC_VIG_H-2
	mov	x4,4
	bl	ui_rbox
	add	x0,x19,1			; la barre du haut
	add	x1,x20,1
	mov	x2,EC_VIG_W-2
	mov	x3,6
	ldr	w4,[x23,TJ_BARRE]
	bl	ui_fill
	cbnz	x21,.poche
	add	x0,x19,10			; bureau : deux fenetres, le panel
	add	x1,x20,14
	mov	x2,56
	mov	x3,36
	bl	ec_fen
	add	x0,x19,44
	add	x1,x20,22
	mov	x2,58
	mov	x3,34
	bl	ec_fen
	add	x0,x19,1
	add	x1,x20,EC_VIG_H-9
	mov	x2,EC_VIG_W-2
	mov	x3,8
	ldr	w4,[x23,TJ_DOCK]
	bl	ui_fill
	mov	x22,0
.p:	cmp	x22,5
	b.hs	.out
	mov	x0,10
	mul	x0,x22,x0
	add	x0,x0,x19
	add	x0,x0,EC_VIG_W/2-24
	add	x1,x20,EC_VIG_H-7
	mov	x2,6
	mov	x3,4
	mov	x4,1
	ldr	w5,[x23,TJ_ACCENT]
	cbz	x22,.pc
	ldr	w5,[x23,TJ_LIGNE_FORTE]
.pc:	mov	w6,w5
	bl	ui_rbox
	add	x22,x22,1
	b	.p
.poche:	add	x0,x19,1			; poche : l'accueil plein ecran
	add	x1,x20,7
	mov	x2,EC_VIG_W-2
	mov	x3,EC_VIG_H-8
	ldr	w4,[x23,TJ_FACE]
	bl	ui_fill
	add	x0,x19,8			; l'heure en grand
	add	x1,x20,11
	mov	x2,30
	mov	x3,7
	mov	x4,2
	ldr	w5,[x23,TJ_TEXTE1]
	mov	w6,w5
	bl	ui_rbox
	mov	x22,0				; des icones, 5 x 2
.ic:	cmp	x22,10
	b.hs	.ouv
	mov	x0,5
	udiv	x1,x22,x0
	msub	x0,x1,x0,x22			; colonne, ligne
	mov	x2,19
	mul	x0,x0,x2
	add	x0,x0,x19
	add	x0,x0,10
	mov	x2,16
	mul	x1,x1,x2
	add	x1,x1,x20
	add	x1,x1,24
	mov	x2,12
	mov	x3,12
	mov	x4,3
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	tst	x22,3
	b.ne	.icc
	ldr	w5,[x23,TJ_ACCENT]
.icc:	mov	w6,w5
	bl	ui_rbox
	add	x22,x22,1
	b	.ic
.ouv:	add	x0,x19,1			; la bande « Ouvertes »
	add	x1,x20,EC_VIG_H-13
	mov	x2,EC_VIG_W-2
	mov	x3,12
	ldr	w4,[x23,TJ_SURFACE]
	bl	ui_fill
	mov	x22,0
.pa:	cmp	x22,3
	b.hs	.out
	mov	x0,26
	mul	x0,x22,x0
	add	x0,x0,x19
	add	x0,x0,8
	add	x1,x20,EC_VIG_H-11
	mov	x2,22
	mov	x3,8
	mov	x4,4
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	mov	w6,w5
	bl	ui_rbox
	add	x22,x22,1
	b	.pa
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0..x3 = une petite fenetre de vignette (x23 = jetons)
ec_fen:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	mov	w6,w5
	mov	x4,3
	bl	ui_rbox
	ldr	w5,[x23,TJ_FACE]
	mov	w6,w5
	add	x0,x19,1
	add	x1,x20,1
	sub	x2,x21,2
	sub	x3,x22,2
	mov	x4,2
	bl	ui_rbox
	add	x0,x19,4
	add	x1,x20,4
	sub	x2,x21,8
	mov	x3,3
	ldr	w4,[x23,TJ_LIGNE]
	bl	ui_fill
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ap_xclic) x0, x1 = clic -> x0 = 1 si pris (une carte, le bouton)
ec_xclic:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x20,EC_CARD_Y			; les cartes
	b.lt	.btn
	cmp	x20,EC_CARD_Y+EC_CARD_H
	b.ge	.btn
	adr	x0,ec_cw
	ldr	x2,[x0]
	adr	x0,ec_cx
	mov	x3,0
.c:	cmp	x3,2
	b.hs	.non
	ldr	x4,[x0,x3,lsl 3]
	cmp	x19,x4
	b.lt	.cn
	add	x4,x4,x2
	cmp	x19,x4
	b.ge	.cn
	adr	x0,ec_choix
	str	x3,[x0]
	adr	x0,ap_msg
	str	xzr,[x0]
	b	.oui
.cn:	add	x3,x3,1
	b	.c
.btn:	cmp	x20,EC_BTN_Y			; Appliquer et relancer
	b.lt	.non
	cmp	x20,EC_BTN_Y+EC_BTN_H
	b.ge	.non
	cmp	x19,SIDE_W+AP_MARGE
	b.lt	.non
	cmp	x19,SIDE_W+AP_MARGE+EC_BTN_W
	b.ge	.non
	bl	ec_relancer
	b	.oui
.non:	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; Appliquer et relancer : shell et densite ecrits, puis le redemarrage
ec_relancer:
	prologue
	adr	x0,ec_w_seg			; la taille des controles
	ldr	x0,[x0]
	cbz	x0,.shell
	bl	ui_w_get_value
	cmp	x0,1
	cset	x0,eq
	adr	x1,ec_dens_lu
	str	x0,[x1]
	bl	ec_dens_ecrire
.shell:	adr	x2,ec_v_desktop			; le mode
	adr	x0,ec_choix
	ldr	x0,[x0]
	cbz	x0,.w
	adr	x2,ec_v_poche
.w:	adr	x0,p_systeme
	adr	x1,ec_k_shell
	bl	ini_set
	adr	x0,ap_msg			; « Redemarrage... », vu un instant
	adr	x1,ec_s_redem
	str	x1,[x0]
	bl	draw
	mov	x0,400
	sys	SYS_SLEEP
	adr	x0,ec_s_init2
	mov	x1,1000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.ko
	adr	x1,ec_msg
	mov	x2,INIT_OP_REBOOT
	stp	x2,xzr,[x1]
	str	wzr,[x1,WM_LEN]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.ko
	mov	x0,5000				; (le Pi s'arrete pendant ce temps)
	sys	SYS_SLEEP
.ko:	adr	x0,ap_msg
	adr	x1,ec_s_echec
	str	x1,[x0]
	epilogue

ec_titre	db 0xC3,0x89,'cran',0
ec_s_titre	db 'R',0xC3,0xA9,'solution',0
ec_s_note	db 'Le natif est le plus net.',0
ec_s_native	db ' (natif)',0
ec_s_edid	db ' (',0xC3,0xA9,'cran)',0
ec_s_fait	db 'Gard',0xC3,0xA9,'e, aussi au prochain d',0xC3,0xA9,'marrage.',0
ec_s_retour	db 'La r',0xC3,0xA9,'solution d',0x27,'avant est revenue.',0
ec_s_refus	db 'Cette r',0xC3,0xA9,'solution est refus',0xC3,0xA9,'e.',0
ec_s_garder	db 'Garder',0
ec_s_revenir	db 'Revenir',0
ec_s_q1		db 'Garder cette r',0xC3,0xA9,'solution ? Retour dans ',0
ec_s_q2		db ' s.',0
ec_k_ecran	db 'ecran',0
ec_s_interface	db 'Interface',0
ec_s_bureau	db 'Bureau',0
ec_s_poche	db 'Poche',0
ec_s_bureau_d	db 'Fen',0xC3,0xAA,'tres, panel en bas',0
ec_s_poche_d	db 'Plein ',0xC3,0xA9,'cran, accueil, tactile',0
ec_s_taille	db 'Taille des contr',0xC3,0xB4,'les',0
ec_s_seg	db 'Normale|Tactile',0
ec_s_avert	db 'Passer ',0xC3,0xA0,' Poche relance la session. Les apps ouvertes sont ferm',0xC3,0xA9,'es.',0
ec_s_relancer	db 'Appliquer et relancer',0
ec_s_redem	db 'Red',0xC3,0xA9,'marrage...',0
ec_s_echec	db 'init2 ne r',0xC3,0xA9,'pond pas : red',0xC3,0xA9,'marrez ',0xC3,0xA0,' la main.',0
ec_s_init2	db 'init2',0
ec_k_shell	db 'shell',0
ec_k_density	db 'density',0
ec_v_poche	db 'poche',0
ec_v_desktop	db 'desktop',0
ec_v_touch	db 'touch',0
ec_v_normal	db 'normal',0
p_ec_theme	db 'SD0:/etc/theme.txt',0
	align 4


macro ec_data {
		align 8
ec_n		dd 0				; modes
ec_cur		rd 2				; taille actuelle
ec_t0		dd 0				; (confirmation) depart, ms
ec_left		dd 0				; secondes affichees
ec_w_label	dd 0
ec_w_bar	dd 0
ec_modes	rb EC_MAX * WSM_SIZE
ec_labels	rb EC_MAX * EC_LABEL
ec_value	rb EC_LABEL
ec_line		rb 96
ec_shell	dd 0				; system.ini : 1 poche
ec_dens		dd 0				; (lecture) theme.txt : 1 touch
ec_dens_lu	dd 0				; la densite du fichier
ec_choix	dd 0				; carte choisie : 0 Bureau, 1 Poche
ec_w_seg	dd 0				; Normale | Tactile
ec_cw		dd 0				; largeur d'une carte
ec_cx		rd 2				; leurs x
ec_msg		rb WS_MSG
}
