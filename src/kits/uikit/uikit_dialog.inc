; ============================================================================
;  kits/uikit_dialog.inc - UIKit : dialogues modaux
;
;  Un dialogue est une fenetre de plus du programme (uikit_window.inc), au role
;  WF_MODAL : Elegant bloque les autres fenetres du programme (un clic dessus
;  les passe devant, le dialogue par-dessus, et s'arrete la ; ni deplacement,
;  ni fermeture, ni clavier). Le kit cree cette fenetre, qui devient la
;  courante, y place des widgets et fait tourner sa propre boucle jusqu'a la
;  reponse. Puis il detruit les widgets du dialogue, ferme sa fenetre et rend
;  la fenetre d'avant. Les dialogues s'emboitent (« Remplacer ? » dans
;  « Enregistrer »).
;
;	ui_message(titre, texte, boutons)	OK, OK/Annuler, Oui/Non, Oui/Non/Annuler
;	ui_ask(titre, question, tampon, taille)	saisie d'une ligne
;	ui_file_open / ui_file_save / ui_folder_choose
;	ui_dialog_begin / ui_dialog_run / ui_dialog_end : un dialogue du programme
; ============================================================================

DG_PREV		= 0			; fenetre d'avant
DG_WIN		= 8
DG_RES		= 16
DG_DONE		= 24
DG_DEF		= 32			; bouton par defaut (Entree)
DG_CANRES	= 40			; reponse d'Echap, de la case de fermeture
DG_FN		= 48			; fonction du dialogue (x0 = dialogue, x1 = widget, x2 = code)
DG_B0		= 56			; boutons (3)
DG_TB		= 80			; champ (saisie, chemin)
DG_LIST		= 88
DG_UP		= 96
DG_NAME		= 104			; champ du nom (Enregistrer)
DG_BUF		= 112			; tampon du programme, sa taille
DG_SIZE		= 120
DG_KIND		= 128			; 0 ouvrir, 1 enregistrer, 2 dossier
DG_CWD		= 136			; dossier courant (bloc de 512 octets)
DG_W		= 144
DG_H		= 152
DG_RV		= 160			; reponses des 3 boutons
DG_UFN		= 184			; fonction du programme (ui_dialog_run)
DG_SL		= 192			; dialogue de couleur : trois glissieres
DG_KEYFN	= 216			; touche laissee par les widgets (x0 = dialogue,
					; x1 = code, x2 = mods -> 1 prise)
DG_BACK		= 224			; dialogue de fichiers : boutons Retour, Avant
DG_FWD		= 232
DG_SIDE		= 240			; panneau lateral
DG_FILT		= 248			; types de fichiers (0 : aucun)
DG_COUNT	= 256			; nombre d'elements (sans types)
DG_NAMEL	= 264			; « Nom : »
DG_HIST		= 272			; historique (UK_FD_HIST chemins)
DG_HPOS		= 280			; place du dossier courant dans l'historique
DG_HN		= 288
DG_SP		= 296			; chemins du panneau (UK_FD_SMAX chemins)
DG_SN		= 304
DG_NDIR		= 312			; dossiers en tete de la liste
DG_SORTC	= 320			; colonne triee, 1 : decroissant
DG_SORTD	= 328
DG_PATS		= 336			; motifs du type choisi (0 : tout)
DG_TICK		= 344			; ui_dialog_timer : fonction, periode (ms)
DG_TICKMS	= 352
DG_BYTES	= 360
UK_PATH_MAX	= 512

; ---------------------------------------------------------------------------
;  Dialogue generique
; ---------------------------------------------------------------------------

; x0 = largeur, x1 = hauteur, x2 = titre -> x0 = dialogue (0 : echec). La
; fenetre du dialogue devient la fenetre courante (fond FACE) : les widgets
; crees ensuite sont les siens.
ui_dialog_begin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x0,x2				; titre traduit
	bl	ui_tr
	mov	x22,x0
	mov	x0,DG_BYTES
	bl	uk_alloc
	cbz	x0,.out
	mov	x19,x0
	stp	x20,x21,[x19,DG_W]
	str	xzr,[x19,DG_TICK]
	adr	x0,uk_cur			; la fenetre d'avant
	ldr	x0,[x0]
	cbz	x0,.place
	ldr	x0,[x0,WB_ID]
	str	x0,[x19,DG_PREV]
.place:	adr	x0,uk_hover			; (la fenetre d'avant attend : ni survol,
	str	xzr,[x0]			; ni capture quand elle reviendra)
	adr	x0,uk_capture
	str	xzr,[x0]
	adr	x9,uk_pop_place			; a une place donnee ?
	ldr	x2,[x9,16]
	cbz	x2,.center
	str	xzr,[x9,16]
	ldp	x0,x1,[x9]
	b	.mk
.center:
	adr	x0,uk_scr			; centre a l'ecran
	bl	ui_screen
	mov	x2,x0
	mov	x0,-1
	mov	x1,-1
	tbnz	x2,63,.mk
	adr	x2,uk_scr
	ldp	x0,x1,[x2]
	sub	x0,x0,x20
	asr	x0,x0,1
	sub	x1,x1,x21
	asr	x1,x1,1
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x1,40
	mov	x2,40
	csel	x1,x2,x1,lt
.mk:	adr	x9,uk_dlg_role
	ldr	x5,[x9]
	str	xzr,[x9]
	orr	x5,x5,WF_MODAL
	mov	x2,x20
	mov	x3,x21
	mov	x4,x22
	bl	ui_window_ex
	tbnz	x0,63,.fail
	str	x0,[x19,DG_WIN]
	adr	x0,uk_win_dlg
	str	x19,[x0]
	mov	x0,0
	mov	x1,0
	mov	x2,x20
	mov	x3,x21
	jet	w4,TJ_FACE
	bl	ui_fill
	adr	x0,uk_dlg_buttons_fn
	str	x0,[x19,DG_FN]
	mov	x0,x19
	b	.out
.fail:	mov	x0,x19
	bl	uk_free
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue : detruit ses widgets, ferme sa fenetre, rend la fenetre d'avant
ui_dialog_end:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	ldr	x0,[x19,DG_WIN]
	bl	ui_window_use
	ldr	x0,[x19,DG_WIN]
	bl	uk_w_destroy_win
	ldr	x0,[x19,DG_CWD]
	bl	uk_free
	ldr	x0,[x19,DG_HIST]
	bl	uk_free
	ldr	x0,[x19,DG_SP]
	bl	uk_free
	bl	ui_close
	ldr	x0,[x19,DG_PREV]		; la fenetre d'avant revient devant
	cbnz	x0,.back
	adr	x0,uk_cur			; (pas de fenetre avant lui)
	ldr	x0,[x0]
	cbnz	x0,.free
	adr	x0,uk_minw
	mov	x1,120
	mov	x2,80
	stp	x1,x2,[x0]
	adr	x0,uk_resize_fn
	str	xzr,[x0]
	b	.free
.back:	bl	ui_window_use
	bl	uk_win
	bl	ui_raise
.free:	mov	x0,x19
	bl	uk_free
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue, x1 = numero (0..2), x2 = texte, x3 = reponse, x4 = style
; (UB_DEFAULT : Entree), x5 = bord droit -> x0 = bord droit du suivant. Bouton
; du bas, aligne a droite (DESIGN 11.1 : principal le plus a droite, 8 px).
ui_dialog_button:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x22,x3
	mov	x23,x4
	mov	x24,x5
	mov	x0,x2				; (texte traduit : TR)
	bl	ui_tr
	mov	x21,x0
	bl	uk_met
	ldr	x3,[x0,MT_CTL]
	ldr	x1,[x19,DG_H]
	sub	x1,x1,x3
	sub	x1,x1,12
	mov	x0,0
	mov	x2,0
	mov	x4,x21
	mov	x5,x23
	bl	ui_pushbutton			; (largeur selon le texte)
	cbz	x0,.out
	ldr	w1,[x0,UW_W]
	sub	x1,x24,x1
	str	w1,[x0,UW_X]
	add	x2,x19,DG_B0
	str	x0,[x2,x20,lsl 3]
	add	x2,x19,DG_RV
	str	x22,[x2,x20,lsl 3]
	tbz	x23,0,.nd
	str	x0,[x19,DG_DEF]
.nd:	sub	x0,x1,8
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue : barre du bas (trait LIGNE au-dessus des boutons)
uk_dlg_bar:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	uk_met
	ldr	x3,[x0,MT_CTL]
	ldr	x1,[x19,DG_H]
	sub	x1,x1,x3
	sub	x1,x1,24
	mov	x0,0
	ldr	x2,[x19,DG_W]
	mov	x3,1
	jet	w4,TJ_LIGNE
	bl	ui_fill
	ldr	x19,[sp],16
	epilogue

; fonction des dialogues a boutons : x0 = dialogue, x1 = widget, x2 = code
; (un bouton : sa reponse ; Entree dans le champ : le bouton par defaut)
uk_dlg_buttons_fn:
	cmp	x2,UN_ACTIVATE
	b.ne	.ret
	ldr	x3,[x0,DG_TB]
	cmp	x1,x3
	b.ne	.b
	ldr	x1,[x0,DG_DEF]
.b:	mov	x3,0
.l:	add	x4,x0,DG_B0
	ldr	x4,[x4,x3,lsl 3]
	cmp	x4,x1
	b.eq	.hit
	add	x3,x3,1
	cmp	x3,3
	b.lo	.l
.ret:	ret
.hit:	cbz	x4,.ret
	add	x4,x0,DG_RV
	ldr	x4,[x4,x3,lsl 3]
	str	x4,[x0,DG_RES]
	mov	x4,1
	str	x4,[x0,DG_DONE]
	ret

; x0 = dialogue, x1 = fonction du programme (x0 = dialogue, x1 = widget,
; x2 = code ; 0 : boutons seulement) -> x0 = reponse. Dessine les widgets du
; dialogue et attend : un bouton de ui_dialog_button donne sa reponse ;
; Echap ou la case de fermeture : celle de ui_dialog_cancel (0) ; Entree
; (non prise par un widget) : le bouton par defaut. La fonction du programme
; peut finir le dialogue par ui_dialog_close. Les autres fenetres du programme
; attendent : Elegant bloque leurs entrees ; le changement de focus et de
; taille les atteint quand meme.
ui_dialog_run:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	str	x1,[x19,DG_UFN]
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
.loop:	ldr	x0,[x19,DG_DONE]
	cbnz	x0,.done
	ldr	x9,[x19,DG_TICK]
	mov	x0,30000
	cbz	x9,.wait
	mov	x0,x19
	blr	x9
	ldr	x0,[x19,DG_DONE]
	cbnz	x0,.done
	ldr	x0,[x19,DG_TICKMS]
.wait:	bl	ui_event
	cbz	x0,.loop
	mov	x20,x0
	adr	x21,uk_vars
	ldr	x21,[x21]
	add	x21,x21,UV_EV
	ldr	w0,[x21,EV_WIN]
	ldr	x1,[x19,DG_WIN]
	cmp	x0,x1
	b.eq	.mine
	cmp	x20,WEV_FOCUS			; une autre fenetre : son etat suit, le
	b.eq	.other				; reste est perdu
	cmp	x20,WEV_RESIZE
	b.ne	.loop
.other:	mov	x0,0
	bl	ui_w_event
	b	.loop
.mine:	cmp	x20,WEV_CLOSE
	b.eq	.cancel
	mov	x0,0
	bl	ui_w_event
	cbz	x1,.untaken
	mov	x22,x0
	mov	x2,x1
	mov	x1,x22
	mov	x0,x19
	ldr	x9,[x19,DG_FN]
	stp	x1,x2,[sp,-16]!
	blr	x9
	ldp	x1,x2,[sp],16
	ldr	x9,[x19,DG_UFN]
	cbz	x9,.loop
	mov	x0,x19
	blr	x9
	b	.loop
.untaken:
	cbnz	x2,.loop
	cmp	x20,WEV_KEY
	b.ne	.loop
	ldr	w0,[x21,EV_KIND]
	cbz	w0,.loop
	ldr	x9,[x19,DG_KEYFN]
	cbz	x9,.keys
	mov	x0,x19
	ldr	w1,[x21,EV_CODE]
	ldr	w2,[x21,EV_MODS]
	blr	x9
	cbnz	x0,.loop
.keys:	ldr	w0,[x21,EV_CODE]
	cmp	w0,KEY_ESC
	b.eq	.cancel
	cmp	w0,KEY_ENTER
	b.ne	.loop
	ldr	x1,[x19,DG_DEF]
	cbz	x1,.loop
	mov	x0,x19
	mov	x2,UN_ACTIVATE
	ldr	x9,[x19,DG_FN]
	blr	x9
	b	.loop
.cancel:
	ldr	x0,[x19,DG_CANRES]
	str	x0,[x19,DG_RES]
.done:	ldr	x0,[x19,DG_RES]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue, x1 = reponse : finit le dialogue (depuis sa fonction)
ui_dialog_close:
	str	x1,[x0,DG_RES]
	mov	x1,1
	str	x1,[x0,DG_DONE]
	ret

; x0 = dialogue, x1 = fonction (x0 = dialogue), x2 = periode en ms : appelee
; par ui_dialog_run avant chaque attente, un evenement ou la periode finit
; l'attente (compte a rebours ; elle peut finir par ui_dialog_close)
ui_dialog_timer:
	str	x1,[x0,DG_TICK]
	str	x2,[x0,DG_TICKMS]
	ret

; x0 = dialogue, x1 = reponse d'Echap et de la case de fermeture
ui_dialog_cancel:
	str	x1,[x0,DG_CANRES]
	ret

; ---------------------------------------------------------------------------
;  Message
; ---------------------------------------------------------------------------

UK_DLG_W	= 420
UK_DLG_PAD	= 24

; x0 = titre, x1 = texte (« \n », lignes coupees), x2 = boutons (0 OK, 1 OK
; Annuler, 2 Oui Non, 3 Oui Non Annuler) -> x0 = 1 OK / Oui, 0 Annuler / Non
; (3 : Non = 2) ; Echap : 0
ui_message:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	tr_x0
	bl	tr_x1
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	bl	uk_met
	mov	x23,x0
	mov	x0,x21				; hauteur du texte
	mov	x1,0
	mov	x2,0
	mov	x3,UK_DLG_W-2*UK_DLG_PAD
	mov	w4,0
	bl	uk_font_r
	mov	x5,x0
	mov	x0,x21
	ldr	x6,[x23,MT_PX]
	mov	x7,1
	bl	ui_text_wrap
	ldr	x1,[x23,MT_CTL]
	add	x1,x1,x0
	add	x1,x1,UK_DLG_PAD*2+24
	mov	x0,UK_DLG_W
	mov	x2,x20
	bl	ui_dialog_begin
	cbz	x0,.fail
	mov	x19,x0
	mov	x0,x21				; le texte
	mov	x1,UK_DLG_PAD
	mov	x2,UK_DLG_PAD
	mov	x3,UK_DLG_W-2*UK_DLG_PAD
	jet	w4,TJ_TEXTE1
	str	x4,[sp,-16]!
	bl	uk_font_r
	mov	x5,x0
	ldr	x4,[sp],16
	mov	x0,x21
	ldr	x6,[x23,MT_PX]
	mov	x7,0
	bl	ui_text_wrap
	mov	x0,x19
	bl	uk_dlg_bar
	mov	x24,UK_DLG_W-16			; boutons, de droite a gauche
	adr	x2,uk_s_ok
	cmp	x22,2
	b.lo	.first
	adr	x2,uk_s_yes
.first:	mov	x0,x19
	mov	x1,0
	mov	x3,1
	mov	x4,UB_DEFAULT
	mov	x5,x24
	bl	ui_dialog_button
	mov	x24,x0
	cbz	x22,.run
	mov	x0,x19
	mov	x1,1
	adr	x2,uk_s_cancel
	mov	x3,0
	cmp	x22,2
	b.lo	.second
	adr	x2,uk_s_no
	cmp	x22,3
	b.ne	.second
	mov	x3,2
.second:
	mov	x4,0
	mov	x5,x24
	bl	ui_dialog_button
	mov	x24,x0
	cmp	x22,3
	b.ne	.run
	mov	x0,x19
	mov	x1,2
	adr	x2,uk_s_cancel
	mov	x3,0
	mov	x4,0
	mov	x5,x24
	bl	ui_dialog_button
.run:	ldr	x0,[x19,DG_DEF]
	bl	ui_w_focus
	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_run
	mov	x20,x0
	mov	x0,x19
	bl	ui_dialog_end
	mov	x0,x20
	b	.out
.fail:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Saisie
; ---------------------------------------------------------------------------

; x0 = titre, x1 = question, x2 = tampon (texte de depart, puis la reponse),
; x3 = taille du tampon -> x0 = 1 (OK : reponse dans le tampon), 0 (Annuler)
ui_ask:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	bl	tr_x0
	bl	tr_x1
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x25,x3
	bl	uk_met
	mov	x23,x0
	bl	uk_font_r
	mov	x5,x0
	mov	x0,x21
	mov	x1,0
	mov	x2,0
	mov	x3,UK_DLG_W-2*UK_DLG_PAD
	mov	w4,0
	ldr	x6,[x23,MT_PX]
	mov	x7,1
	bl	ui_text_wrap
	mov	x26,x0				; hauteur de la question
	ldr	x1,[x23,MT_CTL]
	add	x1,x26,x1,lsl 1
	add	x1,x1,UK_DLG_PAD*2+24+12
	mov	x0,UK_DLG_W
	mov	x2,x20
	bl	ui_dialog_begin
	cbz	x0,.fail
	mov	x19,x0
	bl	uk_font_r
	mov	x5,x0
	mov	x0,x21
	mov	x1,UK_DLG_PAD
	mov	x2,UK_DLG_PAD
	mov	x3,UK_DLG_W-2*UK_DLG_PAD
	jet	w4,TJ_TEXTE1
	ldr	x6,[x23,MT_PX]
	mov	x7,0
	bl	ui_text_wrap
	mov	x0,UK_DLG_PAD			; le champ
	add	x1,x26,UK_DLG_PAD+8
	mov	x2,UK_DLG_W-2*UK_DLG_PAD
	ldr	x3,[x23,MT_CTL]
	mov	x4,0
	bl	ui_textbox
	str	x0,[x19,DG_TB]
	mov	x24,x0
	sub	x1,x25,1
	str	x1,[x24,UW_MAX]
	mov	x1,x22
	bl	ui_w_set_text
	mov	x0,x24
	mov	x1,0
	mov	x2,-1
	bl	ui_textbox_select
	mov	x0,x19
	bl	uk_dlg_bar
	mov	x0,x19
	mov	x1,0
	adr	x2,uk_s_ok
	mov	x3,1
	mov	x4,UB_DEFAULT
	mov	x5,UK_DLG_W-16
	bl	ui_dialog_button
	mov	x5,x0
	mov	x0,x19
	mov	x1,1
	adr	x2,uk_s_cancel
	mov	x3,0
	mov	x4,0
	bl	ui_dialog_button
	mov	x0,x24
	bl	ui_w_focus
	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_run
	mov	x20,x0
	cbz	x20,.end
	ldr	x0,[x24,UW_TEXT]		; la reponse
	bl	uk_strlen
	sub	x2,x25,1
	cmp	x0,x2
	csel	x2,x0,x2,lo
	mov	x0,x22
	ldr	x1,[x24,UW_TEXT]
	bl	uk_memcpy
	strb	wzr,[x0]
.end:	mov	x0,x19
	bl	ui_dialog_end
	mov	x0,x20
	b	.out
.fail:	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Ouvrir, enregistrer, choisir un dossier
;
;  Une fenetre modale redimensionnable (a hauteur de FileDialog d'Onyx) :
;  en haut Retour, Avant, Parent et le chemin (on peut le taper) ; a gauche
;  les emplacements (SD0:/home, SD0:/etc/places.ini comme le navigateur) et les
;  volumes (VFS_VOLUMES) ; la liste Nom, Taille, Date, dossiers d'abord, triee
;  par un clic sur un en-tete ; le nom ; en bas les types de fichiers
;  (ui_file_filter) ou le nombre d'elements, et les boutons. Clavier : Retour
;  arriere (dossier parent), Alt + gauche / droite (Retour, Avant).
; ---------------------------------------------------------------------------

UK_FD_W		= 680
UK_FD_H		= 460
UK_FD_SIDE	= 156			; panneau lateral
UK_FD_HIST	= 16			; dossiers retenus par Retour
UK_FD_SMAX	= 24			; lignes du panneau
UK_FD_SPATH	= 128			; chemin d'une ligne du panneau
UK_FD_STATS	= 400			; au-dela, pas de date (un appel par fichier)
UK_FD_VE	= 56			; entree de VFS_VOLUMES
UK_FD_CSIZE	= 84			; colonnes Taille, Date
UK_FD_CDATE	= 132

; x0 = dossier de depart (0 : SD0:/), x1 = tampon (chemin choisi), x2 = sa
; taille -> x0 = 1 (chemin dans le tampon), 0 (Annuler)
ui_file_open:
	mov	x4,x2
	mov	x3,x1
	mov	x2,0
	mov	x1,0
	b	uk_file_dialog

; x0 = dossier de depart (0 : SD0:/), x1 = nom propose (0 : aucun ; un chemin :
; son dossier et son nom), x2 = tampon, x3 = taille -> x0 = 1 / 0. Un fichier
; qui existe : « Remplacer ? ». Un nom sans extension prend celle du type choisi.
ui_file_save:
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,1
	b	uk_file_dialog

; x0 = types de fichiers montres par les dialogues suivants, jusqu'au prochain
; appel (le texte reste au programme ; 0 : tous) : « *.kson;*.sq » (sans casse),
; ou « Nom|*.a;*.b|Nom|* » pour plusieurs types (comme setFilters d'Onyx). Liste
; des types en bas du dialogue ; un seul filtre : lui, ou tous les fichiers.
ui_file_filter:
	adr	x1,uk_fd_filter
	str	x0,[x1]
	ret

; x0 = nom, x1 = motifs (« *suite;*suite », finis par NUL ou « | » ; 0 : tout)
; -> x0 = 1 s'il passe (feuille)
uk_fd_match:
	cbz	x1,.yes
	mov	x2,x0				; fin du nom
.fin:	ldrb	w3,[x2]
	cbz	w3,.pat
	add	x2,x2,1
	b	.fin
.pat:	ldrb	w3,[x1]				; un motif : « *suite »
	cbz	w3,.no
	cmp	w3,'|'
	b.eq	.no
	cmp	w3,'*'
	b.ne	.skip
	add	x1,x1,1
	mov	x4,x1				; sa fin
.pe:	ldrb	w3,[x4]
	cbz	w3,.len
	cmp	w3,';'
	b.eq	.len
	cmp	w3,'|'
	b.eq	.len
	add	x4,x4,1
	b	.pe
.len:	sub	x5,x4,x1
	cmp	x5,2				; « *.* » : tout
	b.ne	.sfx
	ldrb	w3,[x1]
	cmp	w3,'.'
	b.ne	.sfx
	ldrb	w3,[x1,1]
	cmp	w3,'*'
	b.eq	.yes
.sfx:	sub	x6,x2,x0
	cmp	x5,x6
	b.hi	.next
	sub	x6,x2,x5			; la fin du nom, comparee
	mov	x7,x1
.c:	cmp	x7,x4
	b.hs	.yes
	ldrb	w3,[x6],1
	ldrb	w8,[x7],1
	orr	w3,w3,0x20
	orr	w8,w8,0x20
	cmp	w3,w8
	b.eq	.c
.next:	mov	x1,x4
.skip:	ldrb	w3,[x1]				; le motif suivant
	cbz	w3,.no
	cmp	w3,'|'
	b.eq	.no
	add	x1,x1,1
	cmp	w3,';'
	b.ne	.skip
	b	.pat
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = dossier de depart, x1 = tampon, x2 = taille -> x0 = 1 / 0 (le dossier
; choisi, termine par « / »)
ui_folder_choose:
	mov	x4,x2
	mov	x3,x1
	mov	x2,0
	mov	x1,2
	b	uk_file_dialog

; x0 = dossier, x1 = genre (0 ouvrir, 1 enregistrer, 2 dossier), x2 = nom
; propose, x3 = tampon, x4 = taille -> x0 = 1 / 0
uk_file_dialog:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x23,x3
	mov	x24,x4
	bl	uk_met
	mov	x25,x0
	mov	x26,UK_FD_W			; taille : dans l'ecran
	mov	x27,UK_FD_H
	adr	x0,uk_scr
	bl	ui_screen
	tbnz	x0,63,.sized
	adr	x0,uk_scr
	ldp	x1,x2,[x0]
	ldp	x3,x4,[x0,16]
	sub	x1,x1,2*FRAME_BORDER
	cmp	x26,x1
	csel	x26,x1,x26,gt
	sub	x2,x2,x3
	sub	x2,x2,x4
	sub	x2,x2,FRAME_TITLE_H+FRAME_BORDER
	cmp	x27,x2
	csel	x27,x2,x27,gt
.sized:	adr	x0,uk_dlg_role
	mov	x1,WF_RESIZABLE
	str	x1,[x0]
	mov	x0,x26
	mov	x1,x27
	adr	x2,uk_s_open
	cmp	x21,1
	b.ne	.t2
	adr	x2,uk_s_save
.t2:	cmp	x21,2
	b.ne	.t3
	adr	x2,uk_s_folder
.t3:	bl	ui_dialog_begin
	cbz	x0,.fail
	mov	x19,x0
	stp	x23,x24,[x19,DG_BUF]
	str	x21,[x19,DG_KIND]
	adr	x0,uk_fd_fn
	str	x0,[x19,DG_FN]
	adr	x0,uk_fd_key
	str	x0,[x19,DG_KEYFN]
	mov	x0,UK_PATH_MAX			; dossier courant, historique, panneau
	bl	uk_alloc
	str	x0,[x19,DG_CWD]
	cbz	x0,.nomem
	mov	x0,UK_FD_HIST*UK_PATH_MAX
	bl	uk_alloc
	str	x0,[x19,DG_HIST]
	cbz	x0,.nomem
	mov	x0,UK_FD_SMAX*UK_FD_SPATH
	bl	uk_alloc
	str	x0,[x19,DG_SP]
	cbz	x0,.nomem
	mov	x0,x19				; depart : le dossier, le nom propose
	mov	x1,x20
	mov	x2,x22
	bl	uk_fd_start
	mov	x22,x0
	ldr	x0,[x19,DG_HIST]
	ldr	x1,[x19,DG_CWD]
	bl	uk_path_set
	mov	x0,1
	str	x0,[x19,DG_HN]
	mov	x0,520				; taille
	mov	x1,320
	bl	ui_min_size
	adr	x0,uk_fd_resize
	bl	ui_on_resize
	; --- haut : Retour, Avant, Parent, chemin
	adr	x4,uk_s_left
	bl	.nav
	str	x0,[x19,DG_BACK]
	adr	x4,uk_s_right
	bl	.nav
	str	x0,[x19,DG_FWD]
	adr	x4,uk_s_upar
	bl	.nav
	str	x0,[x19,DG_UP]
	mov	x0,0
	mov	x1,0
	mov	x2,100
	ldr	x3,[x25,MT_CTL]
	mov	x4,0
	bl	ui_textbox
	str	x0,[x19,DG_TB]
	; --- panneau, liste : Nom, Taille, Date
	mov	x0,0
	mov	x1,0
	mov	x2,100
	mov	x3,100
	mov	x4,UL_SIDEBAR or UL_ICONS
	bl	ui_list
	str	x0,[x19,DG_SIDE]
	mov	x0,0
	mov	x1,0
	mov	x2,100
	mov	x3,100
	mov	x4,UL_HEADER or UL_USERSORT or UL_ICONS
	bl	ui_list
	str	x0,[x19,DG_LIST]
	mov	x20,x0
	adr	x0,uk_s_name
	bl	ui_tr
	mov	x1,x0
	mov	x0,x20
	mov	x2,200
	mov	x3,0
	bl	ui_list_column
	adr	x0,uk_s_size
	bl	ui_tr
	mov	x1,x0
	mov	x0,x20
	mov	x2,UK_FD_CSIZE
	mov	x3,ULC_RIGHT
	bl	ui_list_column
	adr	x0,uk_s_date
	bl	ui_tr
	mov	x1,x0
	mov	x0,x20
	mov	x2,UK_FD_CDATE
	mov	x3,0
	bl	ui_list_column
	cmp	x21,2				; --- « Nom : » et son champ
	b.eq	.btns
	adr	x0,uk_s_namec
	bl	ui_tr
	mov	x4,x0
	mov	x0,0
	mov	x1,0
	mov	x2,60
	ldr	x3,[x25,MT_CTL]
	mov	x5,0
	bl	ui_label
	str	x0,[x19,DG_NAMEL]
	mov	x0,0
	mov	x1,0
	mov	x2,100
	ldr	x3,[x25,MT_CTL]
	mov	x4,0
	bl	ui_textbox
	str	x0,[x19,DG_NAME]
	mov	x1,x22
	bl	ui_w_set_text
.btns:	adr	x2,uk_s_bopen
	cmp	x21,1
	b.ne	.b2
	adr	x2,uk_s_bsave
.b2:	cmp	x21,2
	b.ne	.b3
	adr	x2,uk_s_bchoose
.b3:	mov	x0,x19
	mov	x1,0
	mov	x3,1
	mov	x4,UB_DEFAULT
	sub	x5,x26,12
	bl	ui_dialog_button
	mov	x5,x0
	mov	x0,x19
	mov	x1,1
	adr	x2,uk_s_cancel
	mov	x3,0
	mov	x4,0
	bl	ui_dialog_button
	mov	x0,x19				; types de fichiers, ou nombre d'elements
	bl	uk_fd_kinds
	mov	x0,x19
	bl	uk_fd_layout
	mov	x0,0				; (un widget a pu se dessiner en 0, 0)
	mov	x1,0
	mov	x2,x26
	mov	x3,x27
	jet	w4,TJ_FACE
	bl	ui_fill
	mov	x0,x19
	bl	uk_dlg_bar
	mov	x0,x19
	bl	uk_fd_side
	mov	x0,x19
	bl	uk_fd_load
	ldr	x0,[x19,DG_NAME]		; focus : le nom, sinon la liste
	cbz	x21,.flist
	cbnz	x0,.foc
.flist:	ldr	x0,[x19,DG_LIST]
.foc:	bl	ui_w_focus
	cbz	x21,.run
	ldr	x0,[x19,DG_NAME]
	cbz	x0,.run
	mov	x1,0
	mov	x2,-1
	bl	ui_textbox_select
.run:	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_run
	mov	x20,x0
.end:	mov	x0,x19
	bl	ui_dialog_end
	mov	x0,x20
	b	.out
.nomem:	mov	x20,0
	b	.end
.fail:	mov	x0,0
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
; (uk_file_dialog) x4 = symbole, x25 = metriques -> un bouton carre
.nav:	mov	x0,0
	mov	x1,0
	ldr	x2,[x25,MT_CTL]
	mov	x3,x2
	mov	x5,0
	b	ui_pushbutton

; x0 = dialogue, x1 = dossier de depart (0 : SD0:/), x2 = nom propose (0 :
; aucun) : le dossier courant, le plus proche qui existe -> x0 = le nom a
; proposer (un chemin : son dernier composant ; celui d'un fichier de depart)
uk_fd_start:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbz	x21,.dir
	mov	x0,x21				; un chemin : la fin apres « / » ou « : »
	mov	x22,x21
.n:	ldrb	w1,[x0],1
	cbz	w1,.nend
	cmp	w1,'/'
	b.eq	.mark
	cmp	w1,':'
	b.ne	.n
.mark:	mov	x22,x0
	b	.n
.nend:	cmp	x22,x21
	b.eq	.dir
	adr	x0,uk_fdline			; son dossier : le depart
	mov	x1,x21
	sub	x2,x22,x21
	mov	x3,UK_PATH_MAX-2
	cmp	x2,x3
	csel	x2,x3,x2,hi
	add	x20,x0,x2
	bl	uk_memcpy
	strb	wzr,[x20]
	adr	x20,uk_fdline
	mov	x21,x22
.dir:	adr	x1,uk_s_sd0
	cbz	x20,.set
	mov	x1,x20
.set:	ldr	x0,[x19,DG_CWD]
	bl	uk_path_set
	mov	x20,0				; encore un dossier : un seul essai de nom
.try:	ldr	x0,[x19,DG_CWD]
	bl	uk_fd_isdir
	cbnz	x0,.done
	cbnz	x20,.up
	cbnz	x21,.up				; (un nom est deja propose)
	ldr	x0,[x19,DG_CWD]			; le dernier composant : un fichier ?
	mov	x1,0
.len:	ldrb	w2,[x0,x1]
	cbz	w2,.tail
	add	x1,x1,1
	b	.len
.tail:	sub	x1,x1,1				; (avant le « / » final)
	mov	x2,x1
.b:	cbz	x2,.up
	sub	x3,x2,1
	ldrb	w4,[x0,x3]
	cmp	w4,'/'
	b.eq	.cut
	cmp	w4,':'
	b.eq	.cut
	mov	x2,x3
	b	.b
.cut:	adr	x3,uk_fdname			; le nom
	mov	x4,0
.cn:	cmp	x2,x1
	b.hs	.cz
	ldrb	w5,[x0,x2]
	strb	w5,[x3,x4]
	add	x2,x2,1
	add	x4,x4,1
	cmp	x4,255
	b.lo	.cn
.cz:	strb	wzr,[x3,x4]
	mov	x21,x3
.up:	mov	x20,1
	ldr	x0,[x19,DG_CWD]
	bl	uk_path_up
	cbnz	x0,.try
	ldr	x0,[x19,DG_CWD]			; rien : SD0:/
	adr	x1,uk_s_sd0
	bl	uk_path_set
.done:	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin -> x0 = 1 si c'est un dossier
uk_fd_isdir:
	prologue
	adr	x1,uk_fdtmp			; sans son « / » final (sauf a la racine)
	mov	x2,0
.c:	ldrb	w3,[x0,x2]
	strb	w3,[x1,x2]
	cbz	w3,.e
	add	x2,x2,1
	cmp	x2,UK_PATH_MAX-1
	b.lo	.c
	strb	wzr,[x1,x2]
.e:	cmp	x2,2
	b.lo	.st
	sub	x3,x2,1
	ldrb	w4,[x1,x3]
	cmp	w4,'/'
	b.ne	.st
	sub	x4,x3,1
	ldrb	w4,[x1,x4]
	cmp	w4,':'
	b.eq	.st
	strb	wzr,[x1,x3]
.st:	mov	x0,x1
	adr	x1,uk_fdstat
	sys	SYS_STAT
	mov	x1,x0
	mov	x0,0
	cbnz	x1,.out
	adr	x1,uk_fdstat
	ldr	x1,[x1,8]
	ubfx	x0,x1,4,1			; ATTR_DIR
.out:	epilogue

; x0 = chemin -> x0 = date (secondes UTC ; 0 : aucune, ou inconnue)
uk_fd_date:
	prologue
	sub	sp,sp,MSG_TAILLE
	mov	x3,x0
	mov	x1,V_STAT
	stp	x1,xzr,[sp]
	str	xzr,[sp,MSG_D]
	add	x4,sp,MSG_DATA
	mov	x5,0
.c:	ldrb	w6,[x3,x5]
	strb	w6,[x4,x5]
	add	x5,x5,1
	cbz	w6,.go
	cmp	x5,254
	b.lo	.c
	mov	x0,0
	b	.out
.go:	str	w5,[sp,MSG_LEN]
	adr	x0,uk_s_vfs
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.none
	mov	x1,sp
	sys	SYS_SVC_CALL
	tbnz	x0,63,.none
	ldr	x0,[sp,MSG_D]
	b	.out
.none:	mov	x0,0
.out:	add	sp,sp,MSG_TAILLE
	epilogue

; x0 = date (secondes UTC), x1 = tampon <- « AAAA-MM-JJ HH:MM » a l'heure locale
; -> x0 = fin du texte
uk_fd_date_text:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,1				; decalage de l'heure locale
	sys	SYS_CLOCK_GET
	add	x19,x19,x0
	movl	x1,86400
	udiv	x2,x19,x1			; jours depuis 1970
	msub	x19,x2,x1,x19			; secondes du jour
	movl	x3,719468			; annee, mois, jour (civil_from_days)
	add	x2,x2,x3
	movl	x3,146097
	udiv	x4,x2,x3			; ere
	msub	x5,x4,x3,x2			; jour de l'ere
	mov	x3,1460
	udiv	x6,x5,x3
	sub	x6,x5,x6
	movl	x3,36524
	udiv	x7,x5,x3
	add	x6,x6,x7
	movl	x3,146096
	udiv	x7,x5,x3
	sub	x6,x6,x7
	mov	x3,365
	udiv	x6,x6,x3			; annee de l'ere
	mov	x3,400
	madd	x7,x4,x3,x6			; annee (depuis mars)
	mov	x3,365
	mul	x8,x6,x3
	add	x8,x8,x6,lsr 2
	mov	x3,100
	udiv	x9,x6,x3
	sub	x8,x8,x9
	sub	x8,x5,x8			; jour de l'annee
	mov	x3,5
	mul	x9,x8,x3
	add	x9,x9,2
	mov	x3,153
	udiv	x9,x9,x3			; mois depuis mars
	mul	x10,x9,x3
	add	x10,x10,2
	mov	x3,5
	udiv	x10,x10,x3
	sub	x8,x8,x10
	add	x8,x8,1				; jour
	add	x10,x9,3
	cmp	x9,10
	b.lo	.m
	sub	x10,x9,9
.m:	cmp	x10,2
	b.hi	.y
	add	x7,x7,1
.y:	lsl	x0,x7,16
	orr	x0,x0,x10,lsl 8
	orr	x0,x0,x8
	mov	x1,x20
	bl	uk_date_text
	mov	x0,x1				; (uk_date_text : x1 au NUL)
	mov	w2,' '
	strb	w2,[x0],1
	mov	x1,3600
	udiv	x2,x19,x1
	msub	x19,x2,x1,x19
	bl	.two
	mov	w2,':'
	strb	w2,[x0],1
	mov	x1,60
	udiv	x2,x19,x1
	bl	.two
	strb	wzr,[x0]
	ldp	x19,x20,[sp],16
	epilogue
.two:	mov	x3,10				; (x2 : deux chiffres en x0)
	udiv	x4,x2,x3
	add	w5,w4,'0'
	strb	w5,[x0],1
	msub	x4,x4,x3,x2
	add	w5,w4,'0'
	strb	w5,[x0],1
	ret

; x0 = octets, x1 = tampon <- « 812 o », « 14 Ko », « 3,2 Mo », « 1,5 Go »
; -> x0 = fin du texte
uk_fd_size_text:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x2,uk_s_oct
	mov	x3,10				; decimale (10 : aucune)
	mov	x0,x19
	cmp	x19,1024
	b.lo	.num
	adr	x2,uk_s_ko
	lsr	x0,x19,10
	cmp	x0,1024
	b.lo	.num
	mov	x5,20				; Mo, Go : une decimale sous 10
	adr	x2,uk_s_mo
	movl	x4,0x40000000
	cmp	x19,x4
	b.lo	.big
	mov	x5,30
	adr	x2,uk_s_go
.big:	lsr	x0,x19,x5
	cmp	x0,10
	b.hs	.num
	sub	x6,x5,10
	lsr	x3,x19,x6
	and	x3,x3,1023
	mov	x4,10
	mul	x3,x3,x4
	lsr	x3,x3,10
.num:	stp	x2,x3,[sp,-16]!
	mov	x1,x20
	bl	fmt_dec
	ldp	x2,x3,[sp],16
	cmp	x3,10
	b.hs	.unit
	mov	w4,','
	strb	w4,[x0],1
	add	w4,w3,'0'
	strb	w4,[x0],1
.unit:	ldrb	w1,[x2],1
	strb	w1,[x0],1
	cbnz	w1,.unit
	sub	x0,x0,1
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue, x1 = libelle, x2 = chemin, x3 = icone (1 dossier, 2 volume) :
; une ligne du panneau ; donnee de la ligne : chemin + icone
uk_fd_side_add:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x4,[x19,DG_SN]
	cmp	x4,UK_FD_SMAX
	b.hs	.out
	add	x5,x4,1
	str	x5,[x19,DG_SN]
	mov	x5,UK_FD_SPATH
	mul	x5,x4,x5
	ldr	x4,[x19,DG_SP]
	add	x4,x4,x5
	orr	x20,x4,x3
	mov	x5,0
.c:	ldrb	w6,[x2,x5]
	strb	w6,[x4,x5]
	cbz	w6,.add
	add	x5,x5,1
	cmp	x5,UK_FD_SPATH-1
	b.lo	.c
	strb	wzr,[x4,x5]
.add:	ldr	x0,[x19,DG_SIDE]
	mov	x2,x20
	bl	ui_list_add
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue : le panneau lateral, emplacements puis volumes
uk_fd_side:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,uk_s_places			; emplacements
	bl	ui_tr
	mov	x1,x0
	adr	x0,uk_fdline			; (une section : « #titre »)
	mov	w2,'#'
	strb	w2,[x0],1
	bl	str_cat
	ldr	x0,[x19,DG_SIDE]
	adr	x1,uk_fdline
	mov	x2,0
	bl	ui_list_add
	adr	x0,uk_s_home
	bl	uk_fd_isdir
	cbz	x0,.ini
	adr	x0,uk_s_homel
	bl	ui_tr
	mov	x1,x0
	mov	x0,x19
	adr	x2,uk_s_home
	mov	x3,1
	bl	uk_fd_side_add
.ini:	mov	x0,x19
	bl	uk_fd_places
	adr	x0,uk_s_volumes			; volumes
	bl	ui_tr
	mov	x1,x0
	adr	x0,uk_fdline
	mov	w2,'#'
	strb	w2,[x0],1
	bl	str_cat
	ldr	x0,[x19,DG_SIDE]
	adr	x1,uk_fdline
	mov	x2,0
	bl	ui_list_add
	mov	x0,x19				; SD0: toujours, d'abord
	adr	x1,uk_s_sd0c
	adr	x2,uk_s_sd0
	mov	x3,2
	bl	uk_fd_side_add
	sub	sp,sp,MSG_TAILLE
	adr	x0,uk_s_vfs
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.end
	mov	x1,VFS_VOLUMES
	str	x1,[sp]
	str	xzr,[sp,MSG_B]
	mov	x1,sp
	sys	SYS_SVC_CALL
	tbnz	x0,63,.end
	ldr	x20,[sp,MSG_B]			; nombre
	mov	x0,464/UK_FD_VE
	cmp	x20,x0
	csel	x20,x0,x20,hi
	add	x21,sp,MSG_DATA
.v:	cbz	x20,.end
	ldrb	w0,[x21]			; (sd0 : deja la)
	cmp	w0,'s'
	b.ne	.name
	ldrb	w0,[x21,1]
	cmp	w0,'d'
	b.ne	.name
	ldrb	w0,[x21,2]
	cmp	w0,'0'
	b.ne	.name
	ldrb	w0,[x21,3]
	cbz	w0,.nv
.name:	adr	x0,uk_fdline			; « nom » -> « NOM: », « NOM:/ »
	mov	x1,0
.c:	ldrb	w2,[x21,x1]
	cbz	w2,.nd
	sub	w3,w2,'a'
	cmp	w3,25
	b.hi	.u
	sub	w2,w2,32
.u:	strb	w2,[x0,x1]
	add	x1,x1,1
	cmp	x1,15
	b.lo	.c
.nd:	mov	w2,':'
	strb	w2,[x0,x1]
	add	x1,x1,1
	strb	wzr,[x0,x1]
	adr	x3,uk_fdtmp			; (uk_fdline + « / »)
	mov	x4,0
.p:	ldrb	w2,[x0,x4]
	strb	w2,[x3,x4]
	add	x4,x4,1
	cmp	x4,x1
	b.lo	.p
	mov	w2,'/'
	strb	w2,[x3,x4]
	add	x4,x4,1
	strb	wzr,[x3,x4]
	mov	x0,x19
	adr	x1,uk_fdline
	adr	x2,uk_fdtmp
	mov	x3,2
	bl	uk_fd_side_add
.nv:	add	x21,x21,UK_FD_VE
	sub	x20,x20,1
	b	.v
.end:	add	sp,sp,MSG_TAILLE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue : les acces rapides de SD0:/etc/places.ini (« libelle = dossier »,
; ceux du navigateur de fichiers ; un dossier absent n'est pas montre)
uk_fd_places:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,2048
	mov	x19,x0
	adr	x0,uk_s_placesf
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.end
	mov	x20,x0
	mov	x1,sp
	mov	x2,2047
	sys	SYS_READ
	mov	x21,x0
	mov	x0,x20
	sys	SYS_CLOSE
	cmp	x21,0
	b.le	.end
	strb	wzr,[sp,x21]
	mov	x22,sp				; ligne
.line:	ldrb	w0,[x22]
	cbz	w0,.end
	mov	x23,x22				; sa fin
.e:	ldrb	w0,[x23]
	cbz	w0,.eol
	cmp	w0,10
	b.eq	.eol
	add	x23,x23,1
	b	.e
.eol:	ldrb	w24,[x23]
	strb	wzr,[x23]
.ws:	ldrb	w0,[x22]			; libelle
	cmp	w0,' '
	b.ne	.lab
	add	x22,x22,1
	b	.ws
.lab:	cbz	w0,.next
	cmp	w0,13
	b.eq	.next
	cmp	w0,'#'
	b.eq	.next
	mov	x1,x22				; « = »
.eq:	ldrb	w0,[x1]
	cbz	w0,.next
	cmp	w0,'='
	b.eq	.eqf
	add	x1,x1,1
	b	.eq
.eqf:	strb	wzr,[x1]
	add	x20,x1,1			; dossier : espaces autour
.wp:	ldrb	w0,[x20]
	cmp	w0,' '
	b.ne	.tr
	add	x20,x20,1
	b	.wp
.tr:	mov	x2,x1				; fin du libelle : sans espaces
.tl:	cmp	x2,x22
	b.ls	.pe
	sub	x3,x2,1
	ldrb	w0,[x3]
	cmp	w0,' '
	b.ne	.pe
	strb	wzr,[x3]
	mov	x2,x3
	b	.tl
.pe:	mov	x2,x23				; fin du dossier
.tp:	cmp	x2,x20
	b.ls	.chk
	sub	x3,x2,1
	ldrb	w0,[x3]
	cmp	w0,' '
	b.eq	.cut
	cmp	w0,13
	b.ne	.chk
.cut:	strb	wzr,[x3]
	mov	x2,x3
	b	.tp
.chk:	mov	x0,x20
	bl	uk_fd_isdir
	cbz	x0,.next
	mov	x0,x19
	mov	x1,x22
	mov	x2,x20
	mov	x3,1
	bl	uk_fd_side_add
.next:	cbz	w24,.end
	add	x22,x23,1
	b	.line
.end:	add	sp,sp,2048
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue : la ligne du panneau dont le chemin contient le dossier courant
; (la plus longue) est choisie
uk_fd_side_sync:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,DG_SIDE]
	mov	x21,-1				; meilleure ligne, sa longueur
	mov	x22,0
	ldr	x23,[x20,UW_NITEMS]
.l:	cbz	x23,.set
	sub	x23,x23,1
	ldr	x0,[x20,UW_ITEMS]
	add	x0,x0,x23,lsl 4
	ldr	x0,[x0,8]
	and	x0,x0,not 3
	cbz	x0,.l
	ldr	x1,[x19,DG_CWD]
	mov	x2,0
.c:	ldrb	w3,[x0,x2]
	cbz	w3,.pre
	ldrb	w4,[x1,x2]
	cmp	w3,w4
	b.ne	.l
	add	x2,x2,1
	b	.c
.pre:	cbz	x2,.l				; (fin d'un composant du dossier ?)
	sub	x3,x2,1
	ldrb	w4,[x0,x3]
	cmp	w4,'/'
	b.eq	.ok
	ldrb	w4,[x1,x2]
	cbz	w4,.ok
	cmp	w4,'/'
	b.ne	.l
.ok:	cmp	x2,x22
	b.ls	.l
	mov	x22,x2
	mov	x21,x23
	b	.l
.set:	mov	x0,x20
	mov	x1,x21
	bl	ui_w_set_value
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue : la liste des types de fichiers (ou le nombre d'elements), et
; les motifs du premier type
uk_fd_kinds:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x20,uk_fd_filter
	ldr	x20,[x20]
	ldr	x0,[x19,DG_KIND]
	cmp	x0,2
	b.eq	.count				; (un dossier : pas de types)
	cbz	x20,.count
	ldrb	w0,[x20]
	cbnz	w0,.drop
.count:	mov	x0,0
	mov	x1,0
	mov	x2,200
	mov	x3,0
	adr	x4,uk_empty
	mov	x5,ULB_DIM
	bl	ui_label
	str	x0,[x19,DG_COUNT]
	b	.out
.drop:	mov	x0,0
	mov	x1,0
	mov	x2,200
	mov	x3,0
	mov	x4,0
	bl	ui_dropdown
	str	x0,[x19,DG_FILT]
	mov	x21,x0
	mov	x0,x20				; « | » dans le texte ?
.bar:	ldrb	w1,[x0],1
	cbz	w1,.one
	cmp	w1,'|'
	b.ne	.bar
.kind:	ldrb	w0,[x20]			; --- « Nom|motifs|Nom|motifs »
	cbz	w0,.first
	adr	x0,uk_fdline			; « Nom (motifs) »
	mov	x1,0
.n:	ldrb	w2,[x20]
	cbz	w2,.first			; (un nom sans motifs : pas un type)
	add	x20,x20,1
	cmp	w2,'|'
	b.eq	.pats
	cmp	x1,120
	b.hs	.n
	strb	w2,[x0,x1]
	add	x1,x1,1
	b	.n
.pats:	mov	w2,' '
	strb	w2,[x0,x1]
	add	x1,x1,1
	mov	w2,'('
	strb	w2,[x0,x1]
	add	x1,x1,1
.p:	ldrb	w2,[x20]
	cbz	w2,.pe
	add	x20,x20,1
	cmp	w2,'|'
	b.eq	.pe
	cmp	x1,240
	b.hs	.p
	strb	w2,[x0,x1]
	add	x1,x1,1
	b	.p
.pe:	mov	w2,')'
	strb	w2,[x0,x1]
	add	x1,x1,1
	strb	wzr,[x0,x1]
	mov	x0,x21
	adr	x1,uk_fdline
	mov	x2,0
	bl	ui_list_add
	b	.kind
.one:	mov	x0,x21				; --- un filtre : lui, ou tout
	mov	x1,x20
	mov	x2,0
	bl	ui_list_add
	adr	x0,uk_s_allfiles
	bl	ui_tr
	mov	x1,x0
	mov	x0,x21
	mov	x2,0
	bl	ui_list_add
.first:	mov	x0,x21
	mov	x1,0
	bl	ui_w_set_value
	mov	x0,x19
	mov	x1,0
	bl	uk_fd_pick
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue, x1 = numero du type : ses motifs dans DG_PATS (feuille)
uk_fd_pick:
	adr	x2,uk_fd_filter
	ldr	x2,[x2]
	mov	x3,x2				; « | » dans le texte ?
.bar:	ldrb	w4,[x3],1
	cbz	w4,.one
	cmp	w4,'|'
	b.ne	.bar
.kind:	ldrb	w4,[x2],1			; le nom
	cbz	w4,.none
	cmp	w4,'|'
	b.ne	.kind
	cbz	x1,.set				; (x2 : ses motifs)
.p:	ldrb	w4,[x2],1			; le type suivant
	cbz	w4,.none
	cmp	w4,'|'
	b.ne	.p
	sub	x1,x1,1
	b	.kind
.one:	cbz	x1,.set
.none:	mov	x2,0
.set:	str	x2,[x0,DG_PATS]
	ret

; x0 = dialogue : place ses widgets (taille DG_W x DG_H)
uk_fd_layout:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	bl	uk_met
	ldr	x20,[x0,MT_CTL]
	ldp	x21,x22,[x19,DG_W]
	sub	x23,x22,x20			; y des boutons
	sub	x23,x23,12
	mov	x25,12				; haut : boutons carres, chemin
	mov	x26,0
.nav:	add	x0,x19,DG_UP			; (Retour, Avant, Parent)
	cmp	x26,2
	b.eq	.nv
	add	x0,x19,DG_BACK
	add	x0,x0,x26,lsl 3
.nv:	ldr	x0,[x0]
	mov	x1,x25
	mov	x2,12
	mov	x3,x20
	mov	x4,x20
	bl	ui_w_move
	add	x25,x25,x20
	add	x25,x25,4
	add	x26,x26,1
	cmp	x26,3
	b.lo	.nav
	add	x25,x25,4
	ldr	x0,[x19,DG_TB]
	mov	x1,x25
	mov	x2,12
	sub	x3,x21,12+FRAME_CAP_W		; (a gauche des boutons de la fenetre)
	sub	x3,x3,x25
	mov	x4,x20
	bl	ui_w_move
	add	x24,x20,20			; y du panneau et de la liste
	sub	x26,x23,20			; bas de la liste
	ldr	x0,[x19,DG_NAME]
	cbz	x0,.nonm
	sub	x26,x26,x20
	sub	x26,x26,8
.nonm:	mov	x25,12				; panneau (fenetre etroite : aucun)
	cmp	x21,520
	b.hs	.side
	ldr	x0,[x19,DG_SIDE]
	mov	x1,0
	bl	ui_w_show
	b	.list
.side:	ldr	x0,[x19,DG_SIDE]
	mov	x1,12
	mov	x2,x24
	mov	x3,UK_FD_SIDE
	sub	x4,x26,x24
	bl	ui_w_move
	ldr	x0,[x19,DG_SIDE]
	mov	x1,1
	bl	ui_w_show
	mov	x25,12+UK_FD_SIDE+8
.list:	ldr	x20,[x19,DG_LIST]
	mov	x0,x20
	mov	x1,x25
	mov	x2,x24
	sub	x3,x21,12
	sub	x3,x3,x25
	sub	x4,x26,x24
	bl	ui_w_move
	ldr	w0,[x20,UW_W]			; colonne du nom : le reste
	sub	x0,x0,UK_FD_CSIZE+UK_FD_CDATE+14
	cmp	x0,80
	mov	x1,80
	csel	x0,x1,x0,lt
	ldr	x1,[x20,UW_COLS]
	str	w0,[x1]
	bl	uk_met
	ldr	x24,[x0,MT_CTL]
	ldr	x0,[x19,DG_NAME]		; « Nom : » et son champ
	cbz	x0,.btn
	ldr	x0,[x19,DG_NAMEL]
	mov	x1,x25
	add	x2,x26,8
	mov	x3,56
	mov	x4,x24
	bl	ui_w_move
	ldr	x0,[x19,DG_NAME]
	add	x1,x25,56
	add	x2,x26,8
	sub	x3,x21,12
	sub	x3,x3,x1
	mov	x4,x24
	bl	ui_w_move
.btn:	ldr	x20,[x19,DG_B0]			; boutons, de droite a gauche
	ldp	w3,w4,[x20,UW_W]
	sub	x1,x21,12
	sub	x1,x1,x3
	mov	x25,x1
	mov	x0,x20
	mov	x2,x23
	bl	ui_w_move
	ldr	x20,[x19,DG_B0+8]
	ldp	w3,w4,[x20,UW_W]
	sub	x1,x25,8
	sub	x1,x1,x3
	mov	x25,x1
	mov	x0,x20
	mov	x2,x23
	bl	ui_w_move
	sub	x3,x25,28			; types, ou nombre d'elements
	mov	x0,280
	cmp	x3,x0
	csel	x3,x0,x3,gt
	ldr	x0,[x19,DG_FILT]
	cbnz	x0,.low
	ldr	x0,[x19,DG_COUNT]
.low:	mov	x1,12
	mov	x2,x23
	mov	x4,x24
	bl	ui_w_move
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ui_on_resize du dialogue) x0 = largeur, x1 = hauteur
uk_fd_resize:
	prologue
	str	x19,[sp,-16]!
	adr	x2,uk_win_dlg
	ldr	x19,[x2]
	stp	x0,x1,[x19,DG_W]
	mov	x0,x19
	bl	uk_fd_layout
	mov	x0,x19
	bl	uk_dlg_bar
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	ldr	x19,[sp],16
	epilogue

; x0 = dossier (termine par « / ») : remonte d'un cran (« SD0:/ » reste)
; -> x0 = 1 s'il a change (feuille)
uk_path_up:
	mov	x1,x0
	mov	x2,0
.len:	ldrb	w3,[x1,x2]
	cbz	w3,.l
	add	x2,x2,1
	b	.len
.l:	cmp	x2,2
	b.lo	.no
	sub	x3,x2,2				; avant le « / » final
.b:	ldrb	w4,[x1,x3]
	cmp	w4,':'
	b.eq	.no				; (racine d'un volume)
	cmp	w4,'/'
	b.eq	.cut
	cbz	x3,.no
	sub	x3,x3,1
	b	.b
.cut:	add	x3,x3,1
	strb	wzr,[x1,x3]
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = tampon du dossier (UK_PATH_MAX), x1 = chemin : le copie, termine par
; « / » (feuille)
uk_path_set:
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	cbz	w3,.e
	add	x2,x2,1
	cmp	x2,UK_PATH_MAX-2
	b.lo	.c
.e:	cbz	x2,.sl
	sub	x3,x2,1
	ldrb	w3,[x0,x3]
	cmp	w3,'/'
	b.eq	.z
.sl:	mov	w3,'/'
	strb	w3,[x0,x2]
	add	x2,x2,1
.z:	strb	wzr,[x0,x2]
	ret

; x0 = destination, x1 = dossier, x2 = nom (jusqu'a la tabulation ou NUL),
; x3 = taille -> chemin complet (feuille)
uk_path_join:
	sub	x3,x3,1
	mov	x4,0
.d:	cmp	x4,x3
	b.hs	.z
	ldrb	w5,[x1],1
	cbz	w5,.n
	strb	w5,[x0,x4]
	add	x4,x4,1
	b	.d
.n:	cmp	x4,x3
	b.hs	.z
	ldrb	w5,[x2],1
	cbz	w5,.z
	cmp	w5,9
	b.eq	.z
	strb	w5,[x0,x4]
	add	x4,x4,1
	b	.n
.z:	strb	wzr,[x0,x4]
	ret

; (uk_fd_load) x19 = dialogue, x21 = 1 dossier : la ligne de uk_dirent dans
; uk_fdline : « nom<tab>taille<tab>date<tab>octets » (la derniere colonne,
; cachee, trie par taille)
uk_fd_line:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x1,uk_dirent
	adr	x20,uk_fdline
	add	x1,x1,DIRENT_NAME
	mov	x2,240
.cp:	ldrb	w3,[x1],1
	cbz	w3,.cpd
	strb	w3,[x20],1
	subs	x2,x2,1
	b.ne	.cp
.cpd:	mov	w3,9
	strb	w3,[x20],1
	adr	x22,uk_dirent
	ldr	x22,[x22,DIRENT_SIZE]
	cbnz	x21,.dir
	mov	x0,x22
	mov	x1,x20
	bl	uk_fd_size_text
	mov	x20,x0
	b	.tab
.dir:	adr	x1,uk_s_dir
	mov	x0,x20
	bl	uk_tr_copy
	mov	x20,x0
.tab:	mov	w3,9
	strb	w3,[x20],1
	ldr	x0,[x19,DG_LIST]		; la date (un appel a vfs par element)
	ldr	x0,[x0,UW_NITEMS]
	cmp	x0,UK_FD_STATS
	b.hs	.nodate
	adr	x0,uk_fdpath
	ldr	x1,[x19,DG_CWD]
	adr	x2,uk_dirent+DIRENT_NAME
	mov	x3,UK_PATH_MAX
	bl	uk_path_join
	adr	x0,uk_fdpath
	bl	uk_fd_date
	cbz	x0,.nodate
	mov	x1,x20
	bl	uk_fd_date_text
	mov	x20,x0
.nodate:
	mov	w3,9
	strb	w3,[x20],1
	cbnz	x21,.zero			; octets (dossiers : 0)
	mov	x0,x22
	b	.raw
.zero:	mov	x0,0
.raw:	mov	x1,x20
	bl	fmt_dec
	strb	wzr,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = texte : copie sa traduction -> x0 = fin
uk_tr_copy:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,x1
	bl	ui_tr
.c:	ldrb	w1,[x0],1
	strb	w1,[x19],1
	cbnz	w1,.c
	sub	x0,x19,1
	ldr	x19,[sp],16
	epilogue

; x0 = dialogue : trie la liste, dossiers d'abord, selon DG_SORTC, DG_SORTD
uk_fd_sort:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,DG_LIST]
	ldr	x22,[x19,DG_SORTC]
	cmp	x22,1				; Taille : les octets (colonne cachee)
	b.ne	.c
	mov	x22,3
.c:	ldr	x21,[x20,UW_NITEMS]
	ldr	x0,[x19,DG_NDIR]		; les dossiers
	str	x0,[x20,UW_NITEMS]
	mov	x0,x20
	mov	x1,x22
	ldr	x2,[x19,DG_SORTD]
	mov	x3,0
	bl	uk_ls_sort_range
	str	x21,[x20,UW_NITEMS]		; puis les fichiers
	mov	x0,x20
	mov	x1,x22
	ldr	x2,[x19,DG_SORTD]
	ldr	x3,[x19,DG_NDIR]
	bl	uk_ls_sort_range
	ldr	x0,[x19,DG_SORTC]		; (la fleche : la colonne montree)
	str	x0,[x20,UW_SORT]
	mov	x0,x20
	bl	uk_w_refresh
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue : lit son dossier dans la liste (dossiers d'abord ; Dossier :
; les dossiers seulement), met a jour les boutons, le panneau, le nombre
uk_fd_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,DG_TB]			; le chemin dans son champ
	ldr	x1,[x19,DG_CWD]
	bl	ui_w_set_text
	ldr	x20,[x19,DG_LIST]
	mov	x0,x20
	bl	ui_list_clear
	mov	x21,1				; passe : 1 dossiers, 0 fichiers
.pass:	ldr	x0,[x19,DG_CWD]
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.sorted
	mov	x22,x0
.rd:	mov	x0,x22
	adr	x1,uk_dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.close
	adr	x1,uk_dirent
	ldrb	w2,[x1,DIRENT_NAME]		; « . », « .. » : sautes
	cmp	w2,'.'
	b.ne	.keep
	ldrb	w2,[x1,DIRENT_NAME+1]
	cbz	w2,.rd
	cmp	w2,'.'
	b.ne	.keep
	ldrb	w2,[x1,DIRENT_NAME+2]
	cbz	w2,.rd
.keep:	ldr	x2,[x1,DIRENT_ATTR]
	ubfx	x2,x2,4,1			; ATTR_DIR
	cmp	x2,x21
	b.ne	.rd
	cbnz	x21,.line
	add	x0,x1,DIRENT_NAME		; un fichier : le filtre
	ldr	x1,[x19,DG_PATS]
	bl	uk_fd_match
	cbz	x0,.rd
.line:	bl	uk_fd_line
	mov	x0,x20
	adr	x1,uk_fdline
	mov	x2,x21				; donnee : 1 dossier (icone)
	bl	ui_list_add
	b	.rd
.close:	mov	x0,x22
	sys	SYS_CLOSE
.sorted:
	cbz	x21,.done
	ldr	x0,[x20,UW_NITEMS]
	str	x0,[x19,DG_NDIR]
	ldr	x0,[x19,DG_KIND]
	cmp	x0,2
	b.eq	.done
	mov	x21,0
	b	.pass
.done:	mov	x0,x19
	bl	uk_fd_sort
	mov	x0,x20
	mov	x1,-1
	bl	ui_w_set_value
	ldr	x0,[x19,DG_BACK]		; Retour, Avant, Parent
	ldr	x1,[x19,DG_HPOS]
	cmp	x1,0
	cset	x1,ne
	bl	ui_w_enable
	ldr	x0,[x19,DG_HPOS]
	add	x0,x0,1
	ldr	x1,[x19,DG_HN]
	cmp	x0,x1
	cset	x1,lo
	ldr	x0,[x19,DG_FWD]
	bl	ui_w_enable
	adr	x0,uk_fdtmp
	ldr	x1,[x19,DG_CWD]
	bl	uk_path_set
	adr	x0,uk_fdtmp
	bl	uk_path_up
	mov	x1,x0
	ldr	x0,[x19,DG_UP]
	bl	ui_w_enable
	mov	x0,x19
	bl	uk_fd_side_sync
	ldr	x22,[x19,DG_COUNT]		; nombre d'elements
	cbz	x22,.out
	ldr	x0,[x20,UW_NITEMS]
	adr	x1,uk_numbuf
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	adr	x1,uk_s_items
	bl	uk_tr_copy
	mov	x0,x22
	adr	x1,uk_numbuf
	bl	ui_w_set_text
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue : le dossier courant dans l'historique (a sa place)
uk_fd_hist_put:
	ldr	x1,[x0,DG_HPOS]
	mov	x2,UK_PATH_MAX
	mul	x1,x1,x2
	ldr	x2,[x0,DG_HIST]
	ldr	x3,[x0,DG_CWD]
	add	x0,x2,x1
	mov	x1,x3
	b	uk_path_set

; x0 = dialogue, x1 = chemin : y aller (Retour y reviendra)
uk_fd_go:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x0,[x19,DG_CWD]
	mov	x1,x20
	bl	uk_path_set
	ldr	x0,[x19,DG_HPOS]
	add	x0,x0,1
	cmp	x0,UK_FD_HIST
	b.lo	.put
	ldr	x0,[x19,DG_HIST]		; plein : le plus vieux sort
	mov	x1,UK_PATH_MAX
	add	x1,x0,x1
	mov	x2,(UK_FD_HIST-1)*UK_PATH_MAX
	bl	uk_memcpy
	mov	x0,UK_FD_HIST-1
.put:	str	x0,[x19,DG_HPOS]
	add	x0,x0,1
	str	x0,[x19,DG_HN]
	mov	x0,x19
	bl	uk_fd_hist_put
	mov	x0,x19
	bl	uk_fd_load
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue, x1 = -1 Retour / 1 Avant
uk_fd_step:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x2,[x19,DG_HPOS]
	add	x2,x2,x1
	tbnz	x2,63,.out
	ldr	x3,[x19,DG_HN]
	cmp	x2,x3
	b.hs	.out
	str	x2,[x19,DG_HPOS]
	mov	x3,UK_PATH_MAX
	mul	x2,x2,x3
	ldr	x1,[x19,DG_HIST]
	add	x1,x1,x2
	ldr	x0,[x19,DG_CWD]
	bl	uk_path_set
	mov	x0,x19
	bl	uk_fd_load
.out:	ldr	x19,[sp],16
	epilogue

; x0 = dialogue : le dossier parent
uk_fd_parent:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,uk_fdtmp2
	ldr	x1,[x19,DG_CWD]
	bl	uk_path_set
	adr	x0,uk_fdtmp2
	bl	uk_path_up
	cbz	x0,.out
	mov	x0,x19
	adr	x1,uk_fdtmp2
	bl	uk_fd_go
.out:	ldr	x19,[sp],16
	epilogue

; x0 = dialogue, x1 = code, x2 = mods : touche laissee par les widgets -> 1 prise
uk_fd_key:
	prologue
	cmp	w1,KEY_BACKSPACE
	b.ne	.alt
	bl	uk_fd_parent
	mov	x0,1
	b	.out
.alt:	mov	w3,0x44				; Alt
	tst	w2,w3
	b.eq	.no
	cmp	w1,KEY_LEFT
	b.ne	.r
	mov	x1,-1
	b	.step
.r:	cmp	w1,KEY_RIGHT
	b.ne	.no
	mov	x1,1
.step:	bl	uk_fd_step
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	epilogue

; x0 = dialogue, x1 = nom (jusqu'a la tabulation) : entre dans ce dossier
uk_fd_enter:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,uk_fdline
	mov	x2,x1
	ldr	x1,[x19,DG_CWD]
	mov	x3,UK_PATH_MAX
	bl	uk_path_join
	mov	x0,x19
	adr	x1,uk_fdline
	bl	uk_fd_go
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue, x1 = chemin choisi : dans le tampon du programme, fin (1)
uk_fd_accept:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldp	x0,x3,[x19,DG_BUF]
	sub	x3,x3,1
	mov	x2,0
.c:	cmp	x2,x3
	b.hs	.z
	ldrb	w4,[x20,x2]
	strb	w4,[x0,x2]
	cbz	w4,.done
	add	x2,x2,1
	b	.c
.z:	strb	wzr,[x0,x2]
.done:	mov	x0,x19
	mov	x1,1
	bl	ui_dialog_close
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dialogue, x1 = nom tape : son chemin dans uk_fdpath (un chemin complet
; reste tel quel) ; Enregistrer : sans extension, celle du type choisi
uk_fd_name_path:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x2,0				; « : » : un chemin complet
.s:	ldrb	w3,[x20,x2]
	cbz	w3,.rel
	add	x2,x2,1
	cmp	w3,':'
	b.ne	.s
	adr	x0,uk_fdpath
	adr	x1,uk_empty
	mov	x2,x20
	mov	x3,UK_PATH_MAX
	bl	uk_path_join
	b	.ext
.rel:	adr	x0,uk_fdpath
	ldr	x1,[x19,DG_CWD]
	mov	x2,x20
	mov	x3,UK_PATH_MAX
	bl	uk_path_join
.ext:	ldr	x0,[x19,DG_KIND]
	cmp	x0,1
	b.ne	.out
	ldr	x1,[x19,DG_PATS]		; « *.ext » : le type a une extension ?
	cbz	x1,.out
	ldrb	w2,[x1]
	cmp	w2,'*'
	b.ne	.out
	ldrb	w2,[x1,1]
	cmp	w2,'.'
	b.ne	.out
	ldrb	w2,[x1,2]
	cmp	w2,'*'
	b.eq	.out
	cbz	w2,.out
	mov	x2,x20				; le nom en a-t-il une ?
.d:	ldrb	w3,[x2],1
	cbz	w3,.add
	cmp	w3,'.'
	b.ne	.d
	b	.out
.add:	adr	x0,uk_fdpath
	mov	x2,0
.e:	ldrb	w3,[x0,x2]
	cbz	w3,.a
	add	x2,x2,1
	b	.e
.a:	add	x1,x1,1				; apres « * »
.ap:	ldrb	w3,[x1],1
	cbz	w3,.z
	cmp	w3,';'
	b.eq	.z
	cmp	w3,'|'
	b.eq	.z
	cmp	x2,UK_PATH_MAX-2
	b.hs	.z
	strb	w3,[x0,x2]
	add	x2,x2,1
	b	.ap
.z:	strb	wzr,[x0,x2]
.out:	ldp	x19,x20,[sp],16
	epilogue

; fonction du dialogue de fichiers : x0 = dialogue, x1 = widget, x2 = code
uk_fd_fn:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x21,UN_ACTIVATE
	b.ne	.side
	ldr	x0,[x19,DG_UP]			; Parent, Retour, Avant
	cmp	x20,x0
	b.ne	.back
	mov	x0,x19
	bl	uk_fd_parent
	b	.out
.back:	ldr	x0,[x19,DG_BACK]
	cmp	x20,x0
	b.ne	.fwd
	mov	x0,x19
	mov	x1,-1
	bl	uk_fd_step
	b	.out
.fwd:	ldr	x0,[x19,DG_FWD]
	cmp	x20,x0
	b.ne	.path
	mov	x0,x19
	mov	x1,1
	bl	uk_fd_step
	b	.out
.side:	cmp	x21,UN_CHANGE
	b.ne	.path
	ldr	x0,[x19,DG_SIDE]		; une ligne du panneau
	cmp	x20,x0
	b.ne	.filt
	bl	ui_w_get_value
	tbnz	x0,63,.out
	mov	x1,x0
	mov	x0,x20
	bl	ui_list_get
	and	x1,x1,not 3
	cbz	x1,.out
	mov	x0,x19
	bl	uk_fd_go
	b	.out
.filt:	ldr	x0,[x19,DG_FILT]		; un autre type
	cmp	x20,x0
	b.ne	.path
	bl	ui_w_get_value
	mov	x1,x0
	mov	x0,x19
	bl	uk_fd_pick
	mov	x0,x19
	bl	uk_fd_load
	b	.out
.path:	ldr	x0,[x19,DG_TB]			; chemin tape, Entree
	cmp	x20,x0
	b.ne	.list
	cmp	x21,UN_ACTIVATE
	b.ne	.out
	ldr	x0,[x20,UW_TEXT]
	bl	uk_fd_isdir
	cbz	x0,.badp
	mov	x0,x19
	ldr	x1,[x20,UW_TEXT]
	bl	uk_fd_go
	b	.out
.badp:	mov	x0,x20				; (pas un dossier : le chemin revient)
	ldr	x1,[x19,DG_CWD]
	bl	ui_w_set_text
	b	.out
.list:	ldr	x0,[x19,DG_LIST]
	cmp	x20,x0
	b.ne	.name
	cmp	x21,UN_SORT
	b.ne	.lsel
	ldr	x0,[x20,UW_SORT]		; tri par un en-tete
	ldr	x1,[x20,UW_ANCHOR]
	stp	x0,x1,[x19,DG_SORTC]
	mov	x0,x19
	bl	uk_fd_sort
	b	.out
.lsel:	mov	x0,x20				; element choisi : texte, dossier ?
	bl	ui_w_get_value
	tbnz	x0,63,.out
	mov	x1,x0
	mov	x0,x20
	bl	ui_list_get
	mov	x22,x0
	cmp	x21,UN_ACTIVATE
	b.eq	.lact
	cbnz	x1,.out				; choix d'un fichier : son nom dans le
	ldr	x0,[x19,DG_NAME]		; champ
	cbz	x0,.out
	mov	x0,x22
	mov	x1,0
	bl	uk_field
	mov	x1,x0
	ldr	x0,[x19,DG_NAME]
	bl	ui_w_set_text
	b	.out
.lact:	cbz	x1,.lfile			; double-clic : dossier -> y entrer
	mov	x0,x19
	mov	x1,x22
	bl	uk_fd_enter
	b	.out
.lfile:	ldr	x0,[x19,DG_KIND]		; fichier -> choisi (Ouvrir), ou comme le
	cbnz	x0,.okb				; bouton par defaut
	adr	x0,uk_fdpath
	ldr	x1,[x19,DG_CWD]
	mov	x2,x22
	mov	x3,UK_PATH_MAX
	bl	uk_path_join
	mov	x0,x19
	adr	x1,uk_fdpath
	bl	uk_fd_accept
	b	.out
.name:	ldr	x0,[x19,DG_NAME]		; Entree dans le nom
	cmp	x20,x0
	b.ne	.btn
	cmp	x21,UN_ACTIVATE
	b.eq	.typed
	b	.out
.btn:	cmp	x21,UN_ACTIVATE
	b.ne	.out
	ldr	x0,[x19,DG_B0+8]		; Annuler
	cmp	x20,x0
	b.ne	.okbt
	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_close
	b	.out
.okbt:	ldr	x0,[x19,DG_B0]
	cmp	x20,x0
	b.ne	.out
.okb:	ldr	x0,[x19,DG_LIST]		; --- le bouton principal
	bl	ui_w_get_value
	tbnz	x0,63,.nosel
	mov	x1,x0
	ldr	x0,[x19,DG_LIST]
	bl	ui_list_get
	mov	x22,x0
	ldr	x2,[x19,DG_KIND]
	cmp	x2,2
	b.eq	.fold
	cbz	x1,.nosel			; un fichier : le nom
	ldr	x0,[x19,DG_NAME]		; un dossier : y entrer, sauf si
	ldr	x0,[x0,UW_TEXT]			; Enregistrer a un nom tape
	cbz	x2,.ent
	ldrb	w0,[x0]
	cbnz	w0,.typed
.ent:	mov	x0,x19
	mov	x1,x22
	bl	uk_fd_enter
	b	.out
.fold:	adr	x0,uk_fdpath			; Dossier : le dossier choisi
	ldr	x1,[x19,DG_CWD]
	mov	x2,x22
	mov	x3,UK_PATH_MAX-2
	bl	uk_path_join
	adr	x0,uk_fdline
	adr	x1,uk_fdpath
	bl	uk_path_set
	mov	x0,x19
	adr	x1,uk_fdline
	bl	uk_fd_accept
	b	.out
.nosel:	ldr	x0,[x19,DG_KIND]		; rien de choisi : Dossier = le courant
	cmp	x0,2
	b.ne	.typed
	mov	x0,x19
	ldr	x1,[x19,DG_CWD]
	bl	uk_fd_accept
	b	.out
.typed:	ldr	x20,[x19,DG_NAME]		; Ouvrir, Enregistrer : le nom tape
	cbz	x20,.out
	ldr	x20,[x20,UW_TEXT]
	ldrb	w1,[x20]
	cbz	w1,.out
	mov	x0,x19
	mov	x1,x20
	bl	uk_fd_name_path
	adr	x0,uk_fdpath			; un dossier : y aller
	bl	uk_fd_isdir
	cbz	x0,.file
	mov	x0,x19
	adr	x1,uk_fdpath
	bl	uk_fd_go
	ldr	x0,[x19,DG_NAME]
	adr	x1,uk_empty
	bl	ui_w_set_text
	b	.out
.file:	adr	x0,uk_fdpath			; il existe ?
	adr	x1,uk_fdstat
	sys	SYS_STAT
	mov	x1,x0
	ldr	x0,[x19,DG_KIND]
	cbnz	x0,.save
	cbnz	x1,.out				; Ouvrir : il le faut
	b	.new
.save:	cbnz	x1,.new				; Enregistrer : remplacer ?
	adr	x0,uk_s_replace
	bl	ui_tr
	mov	x1,x0
	adr	x0,uk_s_save
	mov	x2,2
	bl	ui_message
	cmp	x0,1
	b.ne	.out
.new:	mov	x0,x19
	adr	x1,uk_fdpath
	bl	uk_fd_accept
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y (contenu, a l'ecran), x2 = largeur, x3 = hauteur, x4 = titre ->
; un dialogue a cette place (sinon, ui_dialog_begin le centre)
uk_dialog_at:
	adr	x9,uk_pop_place
	stp	x0,x1,[x9]
	mov	x0,1
	str	x0,[x9,16]
	mov	x0,x2
	mov	x1,x3
	mov	x2,x4
	b	ui_dialog_begin

; x0, x1 = point de l'ecran : le prochain ui_dialog_begin sera sans cadre, a ce
; point, devant tout (a la taille de l'ecran : le plein ecran d'un diaporama)
ui_dialog_at:
	adr	x9,uk_pop_place
	stp	x0,x1,[x9]
	mov	x0,1
	str	x0,[x9,16]
	adr	x9,uk_dlg_role
	mov	x0,WF_FRAMELESS or WF_TOP or WF_NOLIST or WF_GRAB
	str	x0,[x9]
	ret

; x0 = widget, x1 = largeur, x2 = hauteur, x3 = titre -> dialogue sous le
; widget, cadre compris (au-dessus s'il n'y a pas la place)
uk_dialog_under:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x23,x3
	bl	uk_win_pos
	ldp	w2,w3,[x19,UW_X]
	sxtw	x2,w2
	sxtw	x3,w3
	add	x0,x0,x2
	add	x22,x1,x3			; haut du widget a l'ecran
	ldr	w3,[x19,UW_H]
	add	x1,x22,x3
	add	x1,x1,2+FRAME_TITLE_H
	stp	x0,x1,[sp,-16]!
	adr	x0,uk_scr
	bl	ui_screen
	ldp	x0,x1,[sp],16
	adr	x2,uk_scr
	ldr	x3,[x2,16]			; reserve en bas
	ldr	x2,[x2,8]
	sub	x2,x2,x3
	add	x3,x1,x21
	add	x3,x3,FRAME_BORDER
	cmp	x3,x2
	b.le	.ok
	sub	x1,x22,x21			; au-dessus
	sub	x1,x1,2+FRAME_BORDER
.ok:	mov	x2,x20
	mov	x3,x21
	mov	x4,x23
	bl	uk_dialog_at
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
