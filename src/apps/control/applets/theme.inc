; applets/theme.inc - applet Theme du panneau de configuration : le theme du
; bureau (SD0:/etc/theme.txt, cles d'Onyx), avec son apercu (un petit bureau
; dans les couleurs du theme coche). Appliquer : ecrit le fichier, relance le
; fond, la barre de menus et le dock, et le panneau le prend aussitot ; les
; autres applications le prendront a leur prochain lancement.

TH_N	= 2
TH_PW	= COL_W-8			; apercu
TH_PH	= 150
TH_NACC	= 8				; couleurs d'accent (uk_accents de UIKit)
TH_AD	= 22				; leur pastille
TH_AP	= 30				; leur pas
TH_AY	= AP_HEAD+8+TH_PH+58		; haut des pastilles

th_ouvrir:
	prologue
	str	x19,[sp,-16]!
	bl	th_lire				; theme actuel
	adr	x0,th_courant
	adr	x1,th_givre
	mov	x2,32
	bl	str_copy_n
	adr	x0,th_acc
	str	xzr,[x0]
	adr	x0,p_theme
	adr	x1,th_ligne
	bl	ini_read
	adr	x0,ap_extra
	adr	x1,th_apercu
	str	x1,[x0]
	adr	x0,ap_xclic
	adr	x1,th_clic
	str	x1,[x0]
	adr	x0,ap_surface
	mov	x1,1
	str	x1,[x0]
	adr	x0,th_s_titre
	mov	x1,R_TITRE
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	applet_row
	mov	x19,0				; un choix par theme
.t:	cmp	x19,TH_N
	b.hs	.note
	adr	x0,th_noms
	add	x0,x0,x19,lsl 4
	adr	x1,th_courant
	bl	ci_eq
	mov	x3,x0
	adr	x0,th_noms
	add	x0,x0,x19,lsl 4
	mov	x1,R_CHOIX
	mov	x2,0
	mov	x4,x19
	bl	applet_row
	add	x19,x19,1
	b	.t
.note:	adr	x0,th_s_note
	mov	x1,R_NOTE
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	applet_row
	adr	x0,th_s_note2
	mov	x1,R_NOTE
	bl	applet_row
	ldr	x19,[sp],16
	epilogue

; (ini_read) x0 = cle, x1 = valeur : « theme », « accent »
th_ligne:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x1,th_k_theme
	bl	str_eq
	cbz	x0,.acc
	adr	x0,th_courant
	mov	x1,x20
	mov	x2,32
	bl	str_copy_n
	b	.out
.acc:	mov	x0,x19
	adr	x1,th_k_accent
	bl	str_eq
	cbz	x0,.out
	mov	x19,0
.n:	cmp	x19,TH_NACC
	b.hs	.out
	adr	x0,th_acc_noms
	add	x0,x0,x19,lsl 4
	mov	x1,x20
	bl	ci_eq
	add	x19,x19,1
	cbz	x0,.n
	sub	x19,x19,1
	adr	x0,th_acc
	str	x19,[x0]
.out:	ldp	x19,x20,[sp],16
	epilogue

; (pane_click) x0, x1 = point : une pastille ? -> x0 = 1 si prise (redessiner)
th_clic:
	sub	x1,x1,TH_AY
	cmp	x1,TH_AD
	b.hs	.non
	sub	x0,x0,SIDE_W+AP_MARGE+COL_W
	tbnz	x0,63,.non
	mov	x1,TH_AP
	udiv	x2,x0,x1
	msub	x3,x2,x1,x0
	cmp	x3,TH_AD
	b.hs	.non
	cmp	x2,TH_NACC
	b.hs	.non
	adr	x0,th_acc
	str	x2,[x0]
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; th_actuel <- SD0:/etc/theme.txt (vide : sans fichier)
th_lire:
	prologue
	str	x19,[sp,-16]!
	adr	x0,th_actuel
	strb	wzr,[x0]
	adr	x0,p_theme
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,th_actuel
	mov	x2,TH_TXT-1
	sys	SYS_READ
	cmp	x0,0
	csel	x0,x0,xzr,gt
	adr	x1,th_actuel
	strb	wzr,[x1,x0]
	mov	x0,x19
	sys	SYS_CLOSE
.out:	ldr	x19,[sp],16
	epilogue

; le theme du panneau redevient celui du fichier (th_actuel)
th_revenir:
	prologue
	adr	x0,th_tampon
	adr	x1,th_actuel
	mov	x2,TH_TXT
	bl	str_copy_n
	adr	x0,th_tampon
	bl	ui_theme_texte
	epilogue

; x0, x1 = coin, x2 = largeur : un petit bureau dans les couleurs du theme
; coche (ses jetons : pris un instant par le panneau, puis rendus)
th_apercu:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,0
	bl	applet_choice
	tbnz	x0,63,.out
	adr	x1,th_noms			; « theme = <nom> », « accent = <nom> »
	add	x21,x1,x0,lsl 4
	adr	x0,th_tampon
	strb	wzr,[x0]
	adr	x1,th_k_egal
	bl	str_cat
	mov	x1,x21
	bl	str_cat
	adr	x1,th_k_acc_eg
	bl	str_cat
	adr	x1,th_acc_noms
	adr	x2,th_acc
	ldr	x2,[x2]
	add	x1,x1,x2,lsl 4
	bl	str_cat
	adr	x0,th_tampon
	bl	ui_theme_texte
	bl	ui_jetons			; ses jetons, copies
	adr	x1,th_j
	mov	x2,TJ_SIZE/8
.cp:	ldr	x3,[x0],8
	str	x3,[x1],8
	subs	x2,x2,1
	b.ne	.cp
	bl	th_revenir
	adr	x21,th_j
	movl	w5,0x00D6DEE9			; le fond calme (voronoy), selon le theme
	ldr	w0,[x21,TJ_SOMBRE]
	cbz	w0,.fond
	movl	w5,0x001A1E26
.fond:	mov	w6,w5
	mov	x0,x19
	mov	x1,x20
	mov	x2,TH_PW
	mov	x3,TH_PH
	mov	x4,6
	bl	ui_rbox
	ldr	w5,[x21,TJ_BARRE]		; barre de menus
	and	w5,w5,0x00FFFFFF
	mov	w6,w5
	add	x0,x19,6
	add	x1,x20,6
	mov	x2,TH_PW-12
	mov	x3,12
	mov	x4,4
	bl	ui_rbox
	ldr	w0,[x21,TJ_BARRE]		; ses menus : traits d'encre
	and	w0,w0,0x00FFFFFF
	bl	ui_ink
	mov	w22,w0
	mov	x23,0
.m:	cmp	x23,3
	b.hs	.win
	mov	x0,22
	mul	x0,x23,x0
	add	x0,x0,x19
	add	x0,x0,14
	add	x1,x20,11
	mov	x2,14
	mov	x3,2
	mov	w4,w22
	bl	ui_fill
	add	x23,x23,1
	b	.m
.win:	ldr	w5,[x21,TJ_LIGNE_FORTE]		; fenetre : contour, titre, corps
	mov	w6,w5
	add	x0,x19,20
	add	x1,x20,26
	mov	x2,TH_PW-40
	mov	x3,TH_PH-62
	mov	x4,6
	bl	ui_rbox
	ldr	w5,[x21,TJ_FACE]
	mov	w6,w5
	add	x0,x19,21
	add	x1,x20,27
	mov	x2,TH_PW-42
	mov	x3,TH_PH-64
	mov	x4,5
	bl	ui_rbox
	ldr	w4,[x21,TJ_TITRE_ACTIF]
	and	w4,w4,0x00FFFFFF
	add	x0,x19,22
	add	x1,x20,28
	mov	x2,TH_PW-44
	mov	x3,14
	bl	ui_fill
	ldr	w5,[x21,TJ_DANGER]		; ses boutons
	mov	w6,w5
	add	x0,x19,TH_PW-38
	add	x1,x20,32
	mov	x2,7
	mov	x3,7
	mov	x4,3
	bl	ui_rbox
	ldr	w0,[x21,TJ_TITRE_ACTIF]		; son titre : un trait d'encre
	and	w0,w0,0x00FFFFFF
	bl	ui_ink
	mov	w4,w0
	add	x0,x19,30
	add	x1,x20,34
	mov	x2,40
	mov	x3,2
	bl	ui_fill
	ldr	w5,[x21,TJ_SURFACE]		; un champ
	mov	w6,w5
	add	x0,x19,30
	add	x1,x20,52
	mov	x2,TH_PW-60
	mov	x3,26
	mov	x4,4
	bl	ui_rbox
	mov	x23,0				; son texte
.l:	cmp	x23,2
	b.hs	.btn
	ldr	w4,[x21,TJ_TEXTE1]
	cbz	x23,.l1
	ldr	w4,[x21,TJ_TEXTE3]
.l1:	add	x0,x19,36
	mov	x1,10
	madd	x1,x23,x1,x20
	add	x1,x1,58
	mov	x2,TH_PW-110
	lsl	x3,x23,4
	sub	x2,x2,x3
	mov	x3,3
	bl	ui_fill
	add	x23,x23,1
	b	.l
.btn:	ldr	w5,[x21,TJ_ELEVE]		; deux boutons, le principal accentue
	ldr	w6,[x21,TJ_ELEVE_BAS]
	add	x0,x19,TH_PW-112
	add	x1,x20,86
	mov	x2,38
	mov	x3,14
	mov	x4,4
	bl	ui_rbox
	ldr	w5,[x21,TJ_ACCENT]
	mov	w6,w5
	add	x0,x19,TH_PW-68
	add	x1,x20,86
	mov	x2,38
	mov	x3,14
	mov	x4,4
	bl	ui_rbox
	ldr	w5,[x21,TJ_DOCK]		; le dock et ses lanceurs
	and	w5,w5,0x00FFFFFF
	mov	w6,w5
	add	x0,x19,TH_PW/2-44
	add	x1,x20,TH_PH-24
	mov	x2,88
	mov	x3,18
	mov	x4,6
	bl	ui_rbox
	mov	x23,0
.d:	cmp	x23,4
	b.hs	.nom
	adr	x0,th_lanceurs
	ldr	w5,[x0,x23,lsl 2]
	cmp	x23,1
	b.ne	.d1
	ldr	w5,[x21,TJ_ACCENT]
.d1:	mov	w6,w5
	mov	x0,20
	madd	x0,x23,x0,x19
	add	x0,x0,TH_PW/2-38
	add	x1,x20,TH_PH-21
	mov	x2,12
	mov	x3,12
	mov	x4,3
	bl	ui_rbox
	add	x23,x23,1
	b	.d
.nom:	bl	ui_jetons			; « Aperçu », sous le dessin
	ldr	w3,[x0,TJ_TEXTE3]
	adr	x0,th_s_apercu
	bl	tr0
	mov	x1,x19
	add	x2,x20,TH_PH+18
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	bl	ui_jetons			; la couleur d'accent : ses pastilles
	mov	x24,x0
	ldr	w3,[x24,TJ_TEXTE1]
	adr	x0,th_s_accent
	bl	tr0
	mov	x1,x19
	add	x2,x20,TH_PH+44
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x23,0
.pa:	cmp	x23,TH_NACC
	b.hs	.pnom
	mov	x0,TH_AP
	mul	x22,x23,x0
	add	x22,x22,x19
	adr	x0,th_acc
	ldr	x0,[x0]
	cmp	x0,x23
	b.ne	.pd
	ldr	w5,[x24,TJ_TEXTE1]		; choisie : un anneau
	mov	w6,w5
	sub	x0,x22,3
	add	x1,x20,TH_PH+50-3
	mov	x2,TH_AD+6
	mov	x3,TH_AD+6
	mov	x4,14
	bl	ui_rbox
	ldr	w5,[x24,TJ_SURFACE]
	mov	w6,w5
	sub	x0,x22,1
	add	x1,x20,TH_PH+50-1
	mov	x2,TH_AD+2
	mov	x3,TH_AD+2
	mov	x4,12
	bl	ui_rbox
.pd:	mov	x0,0				; (la colonne du theme coche)
	bl	applet_choice
	cmp	x0,1
	cset	x0,eq
	adr	x1,th_acc_coul
	add	x1,x1,x23,lsl 3
	ldr	w5,[x1,x0,lsl 2]
	mov	w6,w5
	mov	x0,x22
	add	x1,x20,TH_PH+50
	mov	x2,TH_AD
	mov	x3,TH_AD
	mov	x4,11
	bl	ui_rbox
	add	x23,x23,1
	b	.pa
.pnom:	adr	x0,th_acc			; son nom
	ldr	x0,[x0]
	adr	x1,th_acc_lib
	ldr	w0,[x1,x0,lsl 2]
	add	x0,x1,x0
	bl	ui_tr
	ldr	w3,[x24,TJ_TEXTE2]
	mov	x1,x19
	add	x2,x20,TH_PH+90
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = chaines -> x0 = 1 si egales sans tenir compte des capitales (ASCII)
ci_eq:
	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	orr	w4,w2,0x20
	orr	w5,w3,0x20
	cmp	w4,w5
	b.ne	.no
	cbnz	w2,ci_eq
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; Appliquer : SD0:/etc/theme.txt, la session relancee, le panneau rethemé
th_appliquer:
	prologue
	mov	x0,0
	bl	applet_choice
	tbnz	x0,63,.out
	adr	x1,th_noms
	add	x1,x1,x0,lsl 4
	adr	x0,th_fichier			; en-tete, puis « theme = <nom> »
	strb	wzr,[x0]
	str	x1,[sp,-16]!
	adr	x1,th_entete
	bl	str_cat
	ldr	x1,[sp],16
	bl	str_cat
	adr	x1,s_crlf
	bl	str_cat
	adr	x1,th_k_acc_f
	bl	str_cat
	adr	x1,th_acc_noms
	adr	x2,th_acc
	ldr	x2,[x2]
	add	x1,x1,x2,lsl 4
	bl	str_cat
	adr	x1,s_crlf
	bl	str_cat
	adr	x0,p_theme
	adr	x1,th_fichier
	bl	file_write
	bl	th_lire
	bl	th_revenir
	adr	x0,title			; (le cadre du panneau aussi : redessine)
	bl	ui_tr
	bl	ui_title
	adr	x0,th_n_fond
	adr	x1,th_p_fond
	bl	relaunch
	adr	x0,th_n_barre
	adr	x1,th_p_barre
	bl	relaunch
	adr	x0,n_dock
	adr	x1,p_dockp
	bl	relaunch
	adr	x0,ap_msg
	adr	x1,th_s_fait
	str	x1,[x0]
.out:	epilogue

th_noms:
	db 'Givre',0,0,0,0,0,0,0,0,0,0,0
	db 'Onyx Noir',0,0,0,0,0,0,0
	align 4
th_lanceurs	dw 0x00E25B4B, 0, 0x0030A46C, 0x00F2A93B
th_acc_noms:				; (les noms de uk_accents, UIKit)
	db 'bleu',0
	times 15-4 db 0
	db 'orange',0
	times 15-6 db 0
	db 'vert',0
	times 15-4 db 0
	db 'jaune',0
	times 15-5 db 0
	db 'violet',0
	times 15-6 db 0
	db 'fuchsia',0
	times 15-7 db 0
	db 'sarcelle',0
	times 15-8 db 0
	db 'graphite',0
	times 15-8 db 0
	align 4
th_acc_coul:				; (leurs couleurs : Givre, Onyx Noir)
	dw 0x002563D8, 0x002F72DC
	dw 0x00CF3F0A, 0x00D04510
	dw 0x005FBF12, 0x00A6E22E
	dw 0x00F2B705, 0x00FFD02E
	dw 0x007340E0, 0x007E52E6
	dw 0x00C2188F, 0x00D12A9C
	dw 0x000B7F7A, 0x000D827C
	dw 0x003E4A5B, 0x00A4AEBD
th_acc_lib	dw th_al0-th_acc_lib, th_al1-th_acc_lib, th_al2-th_acc_lib, th_al3-th_acc_lib, th_al4-th_acc_lib, th_al5-th_acc_lib, th_al6-th_acc_lib, th_al7-th_acc_lib
th_al0	db 'Bleu',0
th_al1	db 'Rouge orang',0xC3,0xA9,0
th_al2	db 'Vert vif',0
th_al3	db 'Jaune',0
th_al4	db 'Violet',0
th_al5	db 'Fuchsia',0
th_al6	db 'Sarcelle',0
th_al7	db 'Graphite',0
	align 4
th_givre	db 'Givre',0
th_titre	db 'Th',0xC3,0xA8,'me',0
th_s_titre	db 'Th',0xC3,0xA8,'me du bureau',0
th_s_note	db 'Les applications ouvertes le',0
th_s_note2	db 'prendront ',0xC3,0xA0,' leur relance.',0
th_s_apercu	db 'Aper',0xC3,0xA7,'u',0
th_s_fait	db 'Th',0xC3,0xA8,'me appliqu',0xC3,0xA9,'.',0
th_k_theme	db 'theme',0
th_k_accent	db 'accent',0
th_k_egal	db 'theme = ',0
th_k_acc_eg	db 10,'accent = ',0
th_k_acc_f	db 'accent = ',0
th_s_accent	db 'Couleur d',0x27,'accent',0
p_theme		db 'SD0:/etc/theme.txt',0
th_entete	db '# Le look du bureau (cles d''Onyx), ecrit par le panneau de configuration',13,10
		db '# (applet Theme). Autres cles : voir docs/DESIGN-ELEGANT.md.',13,10
		db 'theme = ',0
th_n_fond	db 'voronoy',0
th_p_fond	db 'SD0:/sys/voronoy',0
th_n_barre	db 'menubar',0
th_p_barre	db 'SD0:/sys/menubar',0
		align 4

TH_TXT	= 2048

macro th_data {
		align 8
th_j		rb TJ_SIZE
th_acc		dd 0
th_courant	rb 32
th_fichier	rb 512
th_actuel	rb TH_TXT
th_tampon	rb TH_TXT
}
