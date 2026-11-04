; ============================================================================
;  apps/control/applet.inc - le cadre des applets du panneau de configuration
;  (control, comme celui d'Onyx : une fenetre, la liste des applets a gauche,
;  l'applet choisi dans le volet a droite). Un applet : des lignes - titres,
;  choix uniques (groupes), cases a cocher, notes - en deux colonnes, un
;  message, le bouton « Appliquer » ; ap_extra peut dessiner dans la 2e
;  colonne (l'apercu du theme) ; ap_dessin et ap_clic remplacent les lignes
;  par le dessin de l'applet (le Dock). Trop de lignes pour le volet : elles
;  defilent (molette, barre a droite).
;
;  Son ouverture remplit rows (applet_row) ; sa fonction d'application lit
;  les lignes (applet_choice), ecrit dans SD0:/etc, peut mettre un message
;  dans ap_msg. Un applet en widgets (ap_widgets : le Dock) les cree a son
;  ouverture, ils recoivent les evenements avant le cadre, et il les retire
;  dans ap_fermer ; il a son propre bas de volet. Un applet en lignes de
;  reglages (fm_debut : Region, Ecran) a une carte de listes deroulantes et
;  garde le bas du cadre. Aides : ini_set (une cle d'un fichier « cle = valeur »),
;  file_write, relaunch (relancer un programme de la session).
;
;	segment readable executable	... include 'applet.inc'
;	segment readable writeable	... applet_data
; ============================================================================

R_TITRE		= 0
R_CHOIX		= 1			; choix unique dans son groupe
R_CASE		= 2			; case a cocher
R_NOTE		= 3
ROW_SIZE	= 24			; texte (8), genre, groupe, coche, valeur (8)
ROWS_MAX	= 208
ROW_H		= 26
COL_W		= 210
AP_COLS		= 2
AP_MARGE	= 16
AP_HEAD		= 48			; titre de l'applet
AP_TITRE_Y	= 32			; sa ligne de base (20 px)
AP_SOUS_Y	= 50			; celle du sous-titre (12 px)
AP_BAS		= 56			; message + bouton
SIDE_W		= 160			; la liste des applets
BTN_W		= 96
BTN_H		= 28
AP_FILE_MAX	= 4096
AP_FEN_W	= SIDE_W + 700		; la fenetre : le volet d'Onyx, 700 x 470
AP_FEN_H	= AP_HEAD + 470		; sous le titre, si l'ecran le permet
FM_N		= 4			; lignes de reglages
FM_H		= 48			; une ligne
FM_LW		= 194			; sa liste deroulante
FM_LH		= 30
FM_PAD		= 10			; marge dans la carte
FM_RAYON	= 10
FM_Y_SOUS	= 66			; haut de la carte, sous un sous-titre

; x0 = texte, x1 = genre, x2 = groupe, x3 = coche, x4 = valeur (libre) :
; ajoute une ligne -> x0 = son numero
applet_row:
	adr	x5,nrows
	ldr	x6,[x5]
	cmp	x6,ROWS_MAX
	b.hs	.full
	adr	x7,rows
	mov	x8,ROW_SIZE
	madd	x7,x6,x8,x7
	str	x0,[x7]
	strb	w1,[x7,8]
	strb	w2,[x7,9]
	strb	w3,[x7,10]
	str	x4,[x7,16]
	add	x0,x6,1
	str	x0,[x5]
	sub	x0,x0,1
	ret
.full:	mov	x0,-1
	ret

; x0 = numero de ligne -> x0 = adresse de la ligne
row_ptr:
	adr	x1,rows
	mov	x2,ROW_SIZE
	madd	x0,x0,x2,x1
	ret

; x0 = groupe -> x0 = valeur de la ligne cochee du groupe (-1 : aucune)
applet_choice:
	mov	x1,0
	adr	x2,nrows
	ldr	x2,[x2]
	adr	x3,rows
.r:	cmp	x1,x2
	b.hs	.none
	ldrb	w4,[x3,8]
	cmp	w4,R_CHOIX
	b.ne	.n
	ldrb	w4,[x3,9]
	cmp	w4,w0
	b.ne	.n
	ldrb	w4,[x3,10]
	cbz	w4,.n
	ldr	x0,[x3,16]
	ret
.n:	add	x1,x1,1
	add	x3,x3,ROW_SIZE
	b	.r
.none:	mov	x0,-1
	ret

; -> x0 = colonnes de lignes (1 si l'applet dessine dans la 2e : apercu, ou
; prend les deux pour de longs libelles : ap_colw),
; x1 = lignes que le volet montre
ap_place:
	adr	x0,ap_extra
	ldr	x0,[x0]
	adr	x1,ap_colw
	ldr	x1,[x1]
	sub	x1,x1,COL_W
	orr	x0,x0,x1
	cmp	x0,0
	mov	x0,AP_COLS
	mov	x1,1
	csel	x0,x1,x0,ne
	adr	x1,ap_percol
	ldr	x1,[x1]
	mul	x1,x1,x0
	ret

; ap_defil ramene entre 0 et (lignes - montrees)
ap_borne:
	prologue
	bl	ap_place
	adr	x0,nrows
	ldr	x0,[x0]
	subs	x0,x0,x1
	csel	x0,xzr,x0,lt
	adr	x2,ap_defil
	ldr	x3,[x2]
	cmp	x3,x0
	csel	x3,x0,x3,gt
	cmp	x3,0
	csel	x3,xzr,x3,lt
	str	x3,[x2]
	epilogue

; x0 = crans de molette : les lignes defilent -> x0 = 1 si elles ont bouge
; (l'applet peut le remplacer : ap_molette)
pane_molette:
	prologue
	str	x19,[sp,-16]!
	adr	x1,ap_molette
	ldr	x16,[x1]
	cbz	x16,.lignes
	blr	x16
	b	.out
.lignes:
	adr	x2,ap_defil
	ldr	x19,[x2]
	sub	x3,x19,x0
	str	x3,[x2]
	bl	ap_borne
	adr	x2,ap_defil
	ldr	x0,[x2]
	cmp	x0,x19
	cset	x0,ne
.out:	ldr	x19,[sp],16
	epilogue

; x0, x1 = clic dans la fenetre -> x0 = 0 rien, 1 Appliquer, 2 ligne changee
pane_click:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x2,ap_xclic			; (un clic sur le dessin de l'applet)
	ldr	x2,[x2]
	cbz	x2,.ligne
	blr	x2
	cbz	x0,.pasx
	mov	x0,2
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.pasx:	mov	x0,x19
	mov	x1,x20
.ligne:	adr	x0,ap_widgets			; (widgets : ils ont tout vu, sauf le
	ldr	x0,[x0]				; bas du cadre pour les lignes de reglages)
	cmp	x0,1
	b.eq	.none
	adr	x0,ui_w				; Appliquer, en bas a droite
	ldp	x2,x3,[x0]
	sub	x4,x3,BTN_H+12
	cmp	x20,x4
	b.lt	.rows
	add	x5,x4,BTN_H
	cmp	x20,x5
	b.ge	.rows
	sub	x5,x2,AP_MARGE+BTN_W
	cmp	x19,x5
	b.lt	.rows
	add	x5,x5,BTN_W
	cmp	x19,x5
	b.ge	.rows
	mov	x0,1
	b	.out
.rows:	adr	x0,ap_clic			; applet dessine par lui-meme
	ldr	x0,[x0]
	cbnz	x0,.libre
	adr	x0,ui_w				; la barre de defilement (tactile :
	ldr	x0,[x0]				; pas de molette) : 3 lignes plus
	sub	x0,x0,14			; haut ou plus bas
	cmp	x19,x0
	b.lt	.libre
	subs	x0,x20,AP_HEAD+4
	b.lt	.libre
	adr	x1,ap_percol
	ldr	x1,[x1]
	mov	x2,ROW_H
	mul	x1,x1,x2
	cmp	x0,x1
	b.hs	.libre
	mov	x2,3
	cmp	x0,x1,lsr 1
	b.lo	.haut
	mov	x2,-3
.haut:	mov	x0,x2
	bl	pane_molette
	cbz	x0,.none
	mov	x0,2
	b	.out
.libre:	adr	x0,ap_clic
	ldr	x16,[x0]
	cbz	x16,.lignes
	mov	x0,x19
	mov	x1,x20
	blr	x16
	cbnz	x0,.chg
	b	.none
.lignes:
	subs	x0,x19,SIDE_W+AP_MARGE		; colonne, ligne
	b.lt	.none
	adr	x1,ap_colw
	ldr	x1,[x1]
	udiv	x21,x0,x1
	bl	ap_place
	cmp	x21,x0
	b.hs	.none
	subs	x0,x20,AP_HEAD+4
	b.lt	.none
	mov	x1,ROW_H
	udiv	x22,x0,x1
	adr	x1,ap_percol
	ldr	x1,[x1]
	cmp	x22,x1
	b.hs	.none
	madd	x0,x21,x1,x22			; numero de ligne
	adr	x1,ap_defil
	ldr	x1,[x1]
	add	x0,x0,x1
	adr	x1,nrows
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.none
	bl	row_ptr
	ldrb	w1,[x0,8]
	cmp	w1,R_CASE
	b.eq	.case
	cmp	w1,R_CHOIX
	b.ne	.none
	ldrb	w2,[x0,9]			; choix : decocher le groupe, cocher
	adr	x3,rows
	adr	x4,nrows
	ldr	x4,[x4]
.g:	cbz	x4,.set
	ldrb	w5,[x3,8]
	cmp	w5,R_CHOIX
	b.ne	.gn
	ldrb	w5,[x3,9]
	cmp	w5,w2
	b.ne	.gn
	strb	wzr,[x3,10]
.gn:	add	x3,x3,ROW_SIZE
	sub	x4,x4,1
	b	.g
.set:	mov	w1,1
	strb	w1,[x0,10]
	b	.chg
.case:	ldrb	w1,[x0,10]
	eor	w1,w1,1
	strb	w1,[x0,10]
.chg:	adr	x0,ap_msg			; (un changement efface le message)
	str	xzr,[x0]
	mov	x0,2
	b	.out
.none:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; dessine le volet de l'applet, a droite de la liste : son titre, ses lignes
; (deux colonnes), son dessin a lui (ap_extra : apercu), le message, Appliquer
pane_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	ui_jetons
	mov	x23,x0
	ldr	w4,[x23,TJ_FACE]
	adr	x0,ap_surface			; (ou SURFACE : ap_surface)
	ldr	x0,[x0]
	cbz	x0,.fond
	ldr	w4,[x23,TJ_SURFACE]
.fond:	mov	x0,SIDE_W
	mov	x1,0
	adr	x2,ui_w
	ldp	x2,x3,[x2]
	sub	x2,x2,SIDE_W
	bl	ui_fill
	adr	x0,ap_titre			; titre
	ldr	x0,[x0]
	cbz	x0,.sous
	bl	ui_tr
	mov	x1,SIDE_W+AP_MARGE
	mov	x2,AP_TITRE_Y
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,20
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
.sous:	adr	x0,ap_sous			; sous-titre
	ldr	x0,[x0]
	cbz	x0,.corps
	bl	ui_tr
	mov	x1,SIDE_W+AP_MARGE
	mov	x2,AP_SOUS_Y
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.corps:	adr	x0,ap_dessin			; applet dessine par lui-meme
	ldr	x16,[x0]
	cbz	x16,.lignes
	blr	x16
	b	.bas
.lignes:
	bl	ap_borne
	adr	x0,ap_defil
	ldr	x19,[x0]			; ligne (depuis la premiere montree)
.r:	adr	x0,nrows
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.barre
	bl	ap_place			; position
	mov	x3,x0
	adr	x0,ap_percol
	ldr	x0,[x0]
	adr	x4,ap_defil
	ldr	x4,[x4]
	sub	x4,x19,x4
	udiv	x1,x4,x0
	cmp	x1,x3
	b.hs	.barre
	msub	x2,x1,x0,x4
	mov	x3,COL_W
	mov	x4,SIDE_W+AP_MARGE
	madd	x20,x1,x3,x4			; x
	mov	x3,ROW_H
	mov	x4,AP_HEAD+4
	madd	x21,x2,x3,x4			; y
	mov	x0,x19
	bl	row_ptr
	mov	x22,x0
	ldrb	w0,[x22,8]
	cmp	w0,R_TITRE
	b.eq	.titre
	cmp	w0,R_NOTE
	b.eq	.note
	cmp	w0,R_CASE
	b.eq	.case
	ldrb	w0,[x22,10]			; choix : rond
	cbz	w0,.ro
	ldr	w5,[x23,TJ_ACCENT]
	mov	w6,w5
	add	x1,x21,5
	mov	x0,x20
	mov	x2,16
	mov	x3,16
	mov	x4,8
	bl	ui_rbox
	mov	w5,0x00FFFFFF
	mov	w6,w5
	add	x0,x20,5
	add	x1,x21,10
	mov	x2,6
	mov	x3,6
	mov	x4,3
	bl	ui_rbox
	b	.lab
.ro:	ldr	w5,[x23,TJ_LIGNE_FORTE]
	mov	w6,w5
	mov	x0,x20
	add	x1,x21,5
	mov	x2,16
	mov	x3,16
	mov	x4,8
	bl	ui_rbox
	ldr	w5,[x23,TJ_SURFACE]
	mov	w6,w5
	add	x0,x20,1
	add	x1,x21,6
	mov	x2,14
	mov	x3,14
	mov	x4,7
	bl	ui_rbox
	b	.lab
.case:	ldrb	w0,[x22,10]			; case : carre arrondi, « ✓ »
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	cbz	w0,.cb
	ldr	w5,[x23,TJ_ACCENT]
.cb:	mov	w6,w5
	mov	x0,x20
	add	x1,x21,5
	mov	x2,16
	mov	x3,16
	mov	x4,4
	bl	ui_rbox
	ldrb	w0,[x22,10]
	cbnz	w0,.tick
	ldr	w5,[x23,TJ_SURFACE]
	mov	w6,w5
	add	x0,x20,1
	add	x1,x21,6
	mov	x2,14
	mov	x3,14
	mov	x4,3
	bl	ui_rbox
	b	.lab
.tick:	adr	x0,s_coche
	add	x1,x20,3
	add	x2,x21,18
	mov	w3,0x00FFFFFF
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
.lab:	ldr	x0,[x22]			; libelle
	bl	ui_tr
	add	x1,x20,26
	add	x2,x21,18
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font
	adr	x6,ap_colw
	ldr	x6,[x6]
	sub	x6,x6,34
	bl	ui_text
	b	.next
.titre:	ldr	x0,[x22]
	bl	ui_tr
	mov	x1,x20
	add	x2,x21,18
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,COL_W-8
	bl	ui_text
	b	.next
.note:	ldr	x0,[x22]
	bl	ui_tr
	mov	x1,x20
	add	x2,x21,18
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,COL_W-8
	bl	ui_text
.next:	add	x19,x19,1
	b	.r
.barre:	bl	ap_place			; barre de defilement : s'il en faut
	mov	x21,x1
	adr	x0,nrows
	ldr	x20,[x0]
	cmp	x20,x21
	b.ls	.extra
	adr	x0,ap_percol
	ldr	x22,[x0]
	mov	x0,ROW_H
	mul	x22,x22,x0			; hauteur du rail
	ldr	w5,[x23,TJ_CREUX]
	mov	w6,w5
	adr	x0,ui_w
	ldr	x0,[x0]
	sub	x0,x0,10
	mov	x1,AP_HEAD+4
	mov	x2,6
	mov	x3,x22
	mov	x4,3
	bl	ui_rbox
	mul	x3,x22,x21			; curseur : montrees / toutes
	udiv	x3,x3,x20
	adr	x0,ap_defil
	ldr	x0,[x0]
	mul	x1,x22,x0
	udiv	x1,x1,x20
	add	x1,x1,AP_HEAD+4
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	mov	w6,w5
	adr	x0,ui_w
	ldr	x0,[x0]
	sub	x0,x0,10
	mov	x2,6
	mov	x4,3
	bl	ui_rbox
.extra:	adr	x0,ap_extra			; dessin de l'applet (2e colonne)
	ldr	x16,[x0]
	cbz	x16,.bas
	mov	x0,SIDE_W+AP_MARGE+COL_W
	mov	x1,AP_HEAD+8
	mov	x2,COL_W-8
	blr	x16
.bas:	adr	x0,ap_widgets			; (widgets : leur propre bas)
	ldr	x0,[x0]
	cmp	x0,1
	b.eq	.fin
	adr	x0,ui_w				; trait au-dessus du bouton
	ldp	x20,x21,[x0]
	ldr	w4,[x23,TJ_LIGNE]
	mov	x0,SIDE_W
	sub	x1,x21,AP_BAS
	sub	x2,x20,SIDE_W
	mov	x3,1
	bl	ui_fill
	adr	x0,ap_msg			; message
	ldr	x0,[x0]
	cbz	x0,.btn
	bl	ui_tr
	mov	x1,SIDE_W+AP_MARGE
	sub	x2,x21,22
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	sub	x6,x20,SIDE_W+BTN_W+3*AP_MARGE
	bl	ui_text
.btn:	sub	x0,x20,AP_MARGE+BTN_W		; Appliquer (principal)
	sub	x1,x21,BTN_H+12
	adr	x2,s_appliquer
	adr	x3,ap_bouton			; (ou le libelle de l'applet)
	ldr	x3,[x3]
	cmp	x3,0
	csel	x2,x2,x3,eq
	mov	x3,1
	bl	ap_button
.fin:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = coin, x2 = libelle, x3 = 1 principal : bouton plat
ap_button:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x2
	mov	x22,x3
	bl	ui_tr
	mov	x21,x0
	bl	ui_jetons
	ldr	w5,[x0,TJ_LIGNE_FORTE]		; contour, puis face
	cbz	x22,.c
	ldr	w5,[x0,TJ_ACCENT_FONCE]
.c:	mov	w6,w5
	str	x0,[sp,-16]!
	mov	x0,x19
	mov	x1,x20
	mov	x2,BTN_W
	mov	x3,BTN_H
	mov	x4,6
	bl	ui_rbox
	ldr	x0,[sp],16
	ldr	w5,[x0,TJ_ELEVE]
	ldr	w6,[x0,TJ_ELEVE_BAS]
	cbz	x22,.f
	ldr	w5,[x0,TJ_ACCENT]
	mov	w6,w5
.f:	str	x0,[sp,-16]!
	add	x0,x19,1
	add	x1,x20,1
	mov	x2,BTN_W-2
	mov	x3,BTN_H-2
	mov	x4,5
	bl	ui_rbox
	mov	x0,x21				; texte centre
	mov	x1,14
	adr	x2,ui_font
	bl	ui_text_width
	mov	x1,BTN_W
	sub	x1,x1,x0
	add	x1,x19,x1,lsr 1
	ldr	x0,[sp],16
	ldr	w3,[x0,TJ_TEXTE1]
	cbz	x22,.t
	mov	w3,0x00FFFFFF
.t:	mov	x0,x21
	add	x2,x20,19
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Lignes de reglages : une carte de lignes « libelle a gauche, liste
;  deroulante a droite », une note dessous. L'applet les cree a son ouverture
;  (fm_debut, fm_ligne, fm_item) et lit fm_valeur dans sa fonction
;  d'application ; le message et Appliquer restent au cadre (ap_widgets = 2).
; ---------------------------------------------------------------------------

; x0 = sous-titre (0 : aucun), x1 = note sous la carte (0 : aucune)
fm_debut:
	adr	x2,ap_sous
	str	x0,[x2]
	adr	x2,fm_note
	str	x1,[x2]
	adr	x2,fm_n
	str	xzr,[x2]
	mov	x1,1
	adr	x2,ap_surface
	str	x1,[x2]
	mov	x1,2
	adr	x2,ap_widgets
	str	x1,[x2]
	adr	x1,fm_dessin
	adr	x2,ap_dessin
	str	x1,[x2]
	adr	x1,fm_fermer
	adr	x2,ap_fermer
	str	x1,[x2]
	ret

; -> x0 = y du haut de la carte
fm_haut:
	adr	x0,ap_sous
	ldr	x1,[x0]
	mov	x0,AP_HEAD+4
	cbz	x1,.ret
	mov	x0,FM_Y_SOUS
.ret:	ret

; x0 = libelle : une ligne de plus, sa liste vide
fm_ligne:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,SIDE_W
	mov	x1,0
	mov	x2,FM_LW
	mov	x3,FM_LH
	mov	x4,0
	bl	ui_dropdown
	adr	x1,fm_n
	ldr	x2,[x1]
	add	x3,x2,1
	str	x3,[x1]
	adr	x1,fm_lib
	str	x19,[x1,x2,lsl 3]
	adr	x1,fm_w
	str	x0,[x1,x2,lsl 3]
	adr	x1,fm_change
	mov	x2,0
	bl	ui_w_on
	bl	fm_placer
	ldr	x19,[sp],16
	epilogue

; x0 = ligne, x1 = texte, x2 = 1 si c'est le choix actuel : un element de plus
fm_item:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	mov	x0,x1
	bl	ui_tr
	mov	x1,x0
	adr	x2,fm_w
	ldr	x0,[x2,x19,lsl 3]
	mov	x2,0
	bl	ui_list_add
	cbz	x20,.out
	mov	x1,x0
	mov	x0,x19
	bl	fm_choisir
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = ligne, x1 = element (-1 : aucun)
fm_choisir:
	adr	x2,fm_w
	ldr	x0,[x2,x0,lsl 3]
	b	ui_w_set_value

; x0 = ligne -> x0 = element choisi (-1 : aucun)
fm_valeur:
	adr	x1,fm_w
	ldr	x0,[x1,x0,lsl 3]
	b	ui_w_get_value

; (ui_w_on) un changement efface le message
fm_change:
	cmp	x1,UN_CHANGE
	b.ne	.ret
	adr	x0,ap_msg
	str	xzr,[x0]
	adr	x0,ap_redraw
	mov	x1,1
	str	x1,[x0]
.ret:	ret

; les listes a leur place, d'apres la largeur de la fenetre
fm_placer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,ui_w
	ldr	x21,[x0]
	sub	x21,x21,AP_MARGE+FM_PAD+FM_LW
	bl	fm_haut
	add	x22,x0,(FM_H-FM_LH)/2
	mov	x19,0
.l:	adr	x0,fm_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	adr	x0,fm_w
	ldr	x0,[x0,x19,lsl 3]
	mov	x1,x21
	mov	x2,x22
	mov	x3,FM_LW
	mov	x4,FM_LH
	bl	ui_w_move
	add	x22,x22,FM_H
	add	x19,x19,1
	b	.l
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ap_dessin) la carte, ses filets et libelles, la note, les listes
fm_dessin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	fm_placer
	bl	ui_jetons
	mov	x23,x0
	bl	fm_haut
	mov	x20,x0				; x20 : y de la ligne
	adr	x0,ui_w
	ldr	x21,[x0]
	sub	x21,x21,SIDE_W+2*AP_MARGE	; x21 : largeur de la carte
	adr	x0,fm_n
	ldr	x22,[x0]
	mov	x0,FM_H
	mul	x24,x22,x0			; x24 : sa hauteur
	ldr	w5,[x23,TJ_LIGNE]
	mov	w6,w5
	mov	x0,SIDE_W+AP_MARGE
	mov	x1,x20
	mov	x2,x21
	mov	x3,x24
	mov	x4,FM_RAYON
	bl	ui_rbox
	ldr	w5,[x23,TJ_FACE]
	mov	w6,w5
	mov	x0,SIDE_W+AP_MARGE+1
	add	x1,x20,1
	sub	x2,x21,2
	sub	x3,x24,2
	mov	x4,FM_RAYON-1
	bl	ui_rbox
	mov	x19,0
.l:	cmp	x19,x22
	b.hs	.note
	cbz	x19,.lib
	mov	x0,SIDE_W+AP_MARGE+1		; filet
	mov	x1,x20
	sub	x2,x21,2
	mov	x3,1
	ldr	w4,[x23,TJ_LIGNE]
	bl	ui_fill
.lib:	adr	x0,fm_lib
	ldr	x0,[x0,x19,lsl 3]
	bl	ui_tr
	mov	x1,SIDE_W+AP_MARGE+FM_PAD+4
	add	x2,x20,FM_H/2+5
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font
	sub	x6,x21,FM_LW+3*FM_PAD
	bl	ui_text
	add	x20,x20,FM_H
	add	x19,x19,1
	b	.l
.note:	adr	x0,fm_note
	ldr	x0,[x0]
	cbz	x0,.w
	bl	ui_tr
	mov	x1,SIDE_W+AP_MARGE+2
	add	x2,x20,22
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,x21
	bl	ui_text
.w:	mov	x0,0
	bl	ui_w_draw
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ap_fermer) les listes s'en vont
fm_fermer:
	prologue
	str	x19,[sp,-16]!
	mov	x19,0
.l:	adr	x0,fm_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	adr	x0,fm_w
	ldr	x0,[x0,x19,lsl 3]
	bl	ui_w_destroy
	add	x19,x19,1
	b	.l
.out:	adr	x0,fm_n
	str	xzr,[x0]
	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Fichiers de reglages
; ---------------------------------------------------------------------------

; x0 = destination, x1 = morceau de texte -> x0 = fin : str_cat traduit. Les espaces autour du
; morceau restent ici (un dictionnaire ne garde pas ceux d'une cle).
tr_cat:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	sub	sp,sp,240
	mov	x19,x0
	mov	x20,x1
.e:	ldrb	w0,[x20]			; espaces de tete
	cmp	w0,' '
	b.ne	.d
	strb	w0,[x19],1
	add	x20,x20,1
	b	.e
.d:	mov	x21,0				; longueur sans ceux de queue
	mov	x2,0
.c:	ldrb	w0,[x20,x2]
	strb	w0,[sp,x2]
	cbz	w0,.f
	add	x2,x2,1
	cmp	w0,' '
	csel	x21,x21,x2,eq
	cmp	x2,239
	b.lo	.c
.f:	strb	wzr,[sp,x21]
	add	x20,x20,x21
	mov	x0,sp
	bl	ui_tr
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
.q:	ldrb	w1,[x20],1
	strb	w1,[x0],1
	cbnz	w1,.q
	sub	x0,x0,1
	add	sp,sp,240
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> sa traduction ; x1 a x7 gardes
tr0:
	prologue
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	stp	x5,x6,[sp,-16]!
	str	x7,[sp,-16]!
	bl	ui_tr
	ldr	x7,[sp],16
	ldp	x5,x6,[sp],16
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	epilogue

; x1 = texte -> sa traduction ; les autres registres gardes
tr1:
	prologue
	str	x0,[sp,-16]!
	mov	x0,x1
	bl	tr0
	mov	x1,x0
	ldr	x0,[sp],16
	epilogue

; x2 = texte -> sa traduction ; les autres registres gardes
tr2:
	prologue
	str	x0,[sp,-16]!
	mov	x0,x2
	bl	tr0
	mov	x2,x0
	ldr	x0,[sp],16
	epilogue

; x4 = texte -> sa traduction ; les autres registres gardes
tr4:
	prologue
	str	x0,[sp,-16]!
	mov	x0,x4
	bl	tr0
	mov	x4,x0
	ldr	x0,[sp],16
	epilogue

; x0 = chemin, x1 = cle, x2 = valeur (0 : retirer la cle) : les lignes de
; cette cle sont remplacees par « cle=valeur » (a la fin) -> x0 = 0 ou erreur
ini_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,x20
	bl	str_len
	mov	x24,x0				; longueur de la cle
	mov	x0,x19				; contenu actuel
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x22,0
	tbnz	x0,63,.new
	mov	x23,x0
	adr	x1,ap_file
	mov	x2,AP_FILE_MAX-1
	sys	SYS_READ
	cmp	x0,0
	csel	x22,x0,xzr,gt
	mov	x0,x23
	sys	SYS_CLOSE
.new:	adr	x0,ap_file
	strb	wzr,[x0,x22]
	adr	x23,ap_out			; sortie : les autres lignes
	adr	x9,ap_file
.line:	ldrb	w0,[x9]
	cbz	w0,.add
	mov	x10,x9				; fin de ligne
.e:	ldrb	w0,[x10]
	cbz	w0,.l2
	cmp	w0,10
	b.eq	.l1
	add	x10,x10,1
	b	.e
.l1:	add	x10,x10,1
.l2:	mov	x11,x9				; la cle ? (espaces, puis cle, puis
.sp:	ldrb	w0,[x11]			; espace ou « = »)
	cmp	w0,' '
	b.ne	.k
	add	x11,x11,1
	b	.sp
.k:	mov	x12,0
.kc:	cmp	x12,x24
	b.hs	.kend
	ldrb	w0,[x11,x12]
	ldrb	w1,[x20,x12]
	cmp	w0,w1
	b.ne	.keep
	add	x12,x12,1
	b	.kc
.kend:	ldrb	w0,[x11,x12]
	cmp	w0,'='
	b.eq	.skip
	cmp	w0,' '
	b.eq	.skip
.keep:	ldrb	w0,[x9],1			; ligne gardee
	strb	w0,[x23],1
	cmp	x9,x10
	b.lo	.keep
	b	.line
.skip:	mov	x9,x10				; ligne de la cle : retiree
	b	.line
.add:	cbz	x21,.write			; « cle=valeur »
	adr	x0,ap_out
	cmp	x23,x0
	b.eq	.kv
	ldurb	w0,[x23,-1]			; (ligne precedente terminee)
	cmp	w0,10
	b.eq	.kv
	mov	w0,13
	strb	w0,[x23],1
	mov	w0,10
	strb	w0,[x23],1
.kv:	mov	x1,x20
.c1:	ldrb	w0,[x1],1
	cbz	w0,.eq
	strb	w0,[x23],1
	b	.c1
.eq:	mov	w0,'='
	strb	w0,[x23],1
	mov	x1,x21
.c2:	ldrb	w0,[x1],1
	cbz	w0,.crlf
	strb	w0,[x23],1
	b	.c2
.crlf:	mov	w0,13
	strb	w0,[x23],1
	mov	w0,10
	strb	w0,[x23],1
.write:	mov	x0,x19
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x22,x0
	adr	x1,ap_out
	sub	x2,x23,x1
	sys	SYS_WRITE
	mov	x0,x22
	sys	SYS_CLOSE
	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin, x1 = texte (chaine) : ecrit le fichier entier -> x0 = 0 ou erreur
file_write:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x1
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x0,x20
	bl	str_len
	mov	x2,x0
	mov	x0,x19
	mov	x1,x20
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom du process (« dock »), x1 = chemin : arrete le programme de la
; session (s'il tourne) et le relance
relaunch:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0				; les process de ce nom (procfind.inc)
.p:	mov	x0,x19
	mov	x1,x21
	bl	proc_find
	cbz	x0,.go
	mov	x21,x0
	sys	SYS_KILL
	b	.p
.go:	mov	x0,200				; (le temps que le serveur le voie partir)
	sys	SYS_SLEEP
	mov	x0,x20
	mov	x1,0
	sys	SYS_SPAWN
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_appliquer	db 'Appliquer',0
s_coche		db 0xE2,0x9C,0x93,0		; U+2713
		align 4

macro applet_data {
		align 8
nrows		dd 0
ap_percol	dd 8
ap_colw		dd COL_W			; largeur d'une colonne de lignes
ap_titre	dd 0
ap_sous		dd 0				; sous-titre (0 : aucun)
ap_surface	dd 0				; 1 : volet sur SURFACE, pas FACE
ap_extra	dd 0				; x0, x1 = coin, x2 = largeur (0 : rien)
ap_xclic	dd 0				; x0, x1 = point -> x0 = 1 si pris (0 : rien)
ap_dessin	dd 0				; dessin libre du volet, a la place
ap_clic		dd 0				; des lignes (x0, x1 = clic -> 1 change)
ap_molette	dd 0				; x0 = crans -> 1 change (0 : rien)
ap_tick		dd 0				; chaque seconde -> x0 = 1 : redessiner
ap_net_sub	dd 0				; abonnement a korin.net.* (0 : pas de bus)
ap_net_maj	dd 0				; le reseau a ete publie : etat a relire
ap_touche	dd 0				; x0 = evenement clavier -> x0 = 1 : pris
ap_manette	dd 0				; x0 = evenement WEV_PAD -> x0 = 1 : redessiner
ap_bouton	dd 0				; libelle du bouton du bas (0 : Appliquer)
ap_widgets	dd 0				; 1 : applet en widgets (ils ont son volet) ; 2 : le bas reste au cadre
ap_fermer	dd 0				; a sa fermeture (0 : rien)
ap_redraw	dd 0				; 1 : tout redessiner (applet en widgets)
ap_defil	dd 0				; premiere ligne montree
ap_msg		dd 0				; message sous les lignes (0 : aucun)
fm_n		dd 0				; lignes de reglages
fm_note		dd 0
fm_lib		rb FM_N * 8
fm_w		rb FM_N * 8
rows		rb ROWS_MAX * ROW_SIZE
ap_file		rb AP_FILE_MAX
ap_out		rb AP_FILE_MAX + 128
}
