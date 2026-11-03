; applets/preload.inc - applet Prechargement du panneau de configuration :
; a gauche les programmes de la carte (applications, SD0:/bin), a droite ceux
; de SD0:/etc/preload.txt avec leur etat (SYS_IMAGE_LIST, relu chaque seconde :
; image epinglee ou pas encore). La fleche droite ajoute le programme choisi (a
; la liste et tout de suite : commande preload, qui notifie a la fin), la fleche
; gauche le retire (commande unload). Les commentaires du fichier sont gardes.

PL_LMAX	= 192				; programmes a gauche
PL_RMAX	= 32				; prechargeables a droite
PL_PATH	= 80
PL_W	= 186				; une liste
PL_X1	= SIDE_W+AP_MARGE
PL_BX	= PL_X1+PL_W+8			; les fleches
PL_BW	= 32
PL_BH	= 30
PL_X2	= PL_BX+PL_BW+8
PL_Y	= AP_HEAD+26
PL_RH	= 22
PL_BAR	= 12				; bande de defilement (tactile)
PL_IMG	= 64				; images lues

pl_ouvrir:
	prologue
	adr	x0,ap_dessin
	adr	x1,pl_dessin
	str	x1,[x0]
	adr	x0,ap_clic
	adr	x1,pl_clic
	str	x1,[x0]
	adr	x0,ap_molette
	adr	x1,pl_molette
	str	x1,[x0]
	adr	x0,ap_tick
	adr	x1,pl_status
	str	x1,[x0]
	adr	x0,ap_bouton
	adr	x1,pl_s_actualiser
	str	x1,[x0]
	bl	pl_load
	epilogue

; les deux listes, relues ; rien de choisi
pl_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,pl_sel
	mov	x1,-1
	stp	x1,x1,[x0]
	adr	x0,pl_off
	stp	xzr,xzr,[x0]
	adr	x0,pl_nl
	stp	xzr,xzr,[x0]			; pl_nl, pl_nr
	; ---- a droite : les lignes du fichier
	bl	pl_file_read
	adr	x19,ap_file
	strb	wzr,[x19,x0]
.ligne:	ldrb	w0,[x19]
	cbz	w0,.gauche
	mov	x20,x19				; fin de ligne
.f:	ldrb	w0,[x20]
	cbz	w0,.lu
	cmp	w0,10
	b.eq	.lu
	add	x20,x20,1
	b	.f
.lu:	mov	x0,x19
	mov	x1,x20
	bl	pl_trim				; x0 debut, x1 fin
	mov	x19,x20
	ldrb	w2,[x19]
	cbz	w2,.lv
	add	x19,x19,1
.lv:	cmp	x0,x1
	b.hs	.ligne
	ldrb	w2,[x0]
	cmp	w2,'#'
	b.eq	.ligne
	adr	x2,pl_nr
	ldr	x3,[x2]
	cmp	x3,PL_RMAX
	b.hs	.ligne
	add	x4,x3,1
	str	x4,[x2]
	adr	x2,pl_right
	mov	x4,PL_PATH
	madd	x2,x3,x4,x2
	mov	x3,PL_PATH-1
.cp:	cmp	x0,x1
	b.hs	.cz
	cbz	x3,.cz
	ldrb	w4,[x0],1
	strb	w4,[x2],1
	sub	x3,x3,1
	b	.cp
.cz:	strb	wzr,[x2]
	b	.ligne
	; ---- a gauche : les applications, puis SD0:/bin
.gauche:
	bl	applis_load
	mov	x19,0
.ap:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.bin
	adr	x0,pl_path
	adr	x1,pl_p_apps
	bl	str_cat
	adr	x1,ap_nom
	mov	x2,AP_NOM
	madd	x1,x19,x2,x1
	bl	str_cat
	adr	x1,pl_p_main
	bl	str_cat
	bl	pl_left_add
	add	x19,x19,1
	b	.ap
.bin:	adr	x0,pl_p_bin
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.fin
	mov	x19,x0
.d:	mov	x0,x19
	adr	x1,ap_dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.df
	adr	x0,ap_dirent
	ldrb	w1,[x0]
	cmp	w1,'.'
	b.eq	.d
	adr	x0,pl_path
	adr	x1,pl_p_bin2
	bl	str_cat
	adr	x1,ap_dirent
	bl	str_cat
	bl	pl_left_add
	b	.d
.df:	mov	x0,x19
	sys	SYS_CLOSE
.fin:	bl	pl_status
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = debut, x1 = fin -> sans espaces, tabulations ni CR autour
pl_trim:
.a:	cmp	x0,x1
	b.hs	.b
	ldrb	w2,[x0]
	cmp	w2,' '
	b.eq	.an
	cmp	w2,9
	b.eq	.an
	cmp	w2,13
	b.ne	.b
.an:	add	x0,x0,1
	b	.a
.b:	cmp	x1,x0
	b.ls	.r
	ldurb	w2,[x1,-1]
	cmp	w2,' '
	b.eq	.bn
	cmp	w2,9
	b.eq	.bn
	cmp	w2,10
	b.eq	.bn
	cmp	w2,13
	b.ne	.r
.bn:	sub	x1,x1,1
	b	.b
.r:	ret

; pl_path a gauche, s'il n'est pas deja a droite
pl_left_add:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
.r:	adr	x0,pl_nr
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.add
	adr	x0,pl_right
	mov	x1,PL_PATH
	madd	x0,x19,x1,x0
	adr	x1,pl_path
	bl	str_eq
	cbnz	x0,.out
	add	x19,x19,1
	b	.r
.add:	adr	x0,pl_nl
	ldr	x1,[x0]
	cmp	x1,PL_LMAX
	b.hs	.out
	add	x2,x1,1
	str	x2,[x0]
	adr	x0,pl_left
	mov	x2,PL_PATH
	madd	x0,x1,x2,x0
	adr	x1,pl_path
	mov	x2,PL_PATH-1
.c:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	cbz	w3,.out
	subs	x2,x2,1
	b.ne	.c
	strb	wzr,[x0]
.out:	ldp	x19,x20,[sp],16
	epilogue

; (ap_tick) l'etat de chaque programme de droite -> x0 = 1 s'il a change
pl_status:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,pl_img
	mov	x1,PL_IMG
	sys	SYS_IMAGE_LIST
	cmp	x0,0
	csel	x20,x0,xzr,gt
	mov	x21,0				; change ?
	mov	x19,0
.r:	adr	x0,pl_nr
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	adr	x9,pl_right
	mov	x1,PL_PATH
	madd	x9,x19,x1,x9
	mov	x22,0				; etat
	adr	x10,pl_img
	mov	x11,x20
.i:	cbz	x11,.set
	ldr	x0,[x10,IML_KIND]
	tbz	x0,0,.in			; IML_PIN
	add	x0,x10,IML_PATH
	mov	x1,x9
	bl	pl_same
	cbnz	x0,.oui
.in:	add	x10,x10,IML_SIZE
	sub	x11,x11,1
	b	.i
.oui:	ldr	x22,[x10,IML_FSIZE]		; (non nul : chargee)
	orr	x22,x22,1
.set:	adr	x0,pl_stat
	ldr	x1,[x0,x19,lsl 3]
	str	x22,[x0,x19,lsl 3]
	cmp	x1,x22
	b.eq	.n
	mov	x21,1
.n:	add	x19,x19,1
	b	.r
.out:	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = chemins -> x0 = 1 si egaux aux majuscules pres (ASCII) ; garde x9-x11
pl_same:
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	sub	w4,w2,'A'
	cmp	w4,25
	b.hi	.a
	add	w2,w2,32
.a:	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.b
	add	w3,w3,32
.b:	cmp	w2,w3
	b.ne	.non
	cbnz	w2,.c
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; -> x0 = lignes que chaque liste montre
pl_rows:
	adr	x0,ui_w
	ldr	x0,[x0,8]
	sub	x0,x0,PL_Y+AP_BAS+6
	mov	x1,PL_RH
	udiv	x0,x0,x1
	ret

pl_dessin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	bl	ui_jetons
	mov	x23,x0
	adr	x0,pl_s_gauche
	bl	tr0
	mov	x1,PL_X1
	mov	x2,AP_HEAD+18
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,PL_W
	bl	ui_text
	adr	x0,pl_s_droite
	bl	tr0
	mov	x1,PL_X2
	mov	x2,AP_HEAD+18
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,PL_W
	bl	ui_text
	bl	pl_rows
	mov	x24,x0				; lignes montrees
	mov	x19,0				; liste
.list:	mov	x20,PL_X1
	cbz	x19,.fond
	mov	x20,PL_X2
.fond:	ldr	w5,[x23,TJ_LIGNE]		; cadre, fond
	mov	w6,w5
	mov	x0,x20
	mov	x1,PL_Y
	mov	x2,PL_W
	mov	x3,PL_RH
	mul	x3,x3,x24
	add	x3,x3,2
	mov	x4,6
	bl	ui_rbox
	ldr	w5,[x23,TJ_SURFACE]
	mov	w6,w5
	add	x0,x20,1
	mov	x1,PL_Y+1
	mov	x2,PL_W-2
	mov	x3,PL_RH
	mul	x3,x3,x24
	mov	x4,5
	bl	ui_rbox
	adr	x0,pl_off
	ldr	x21,[x0,x19,lsl 3]		; premiere montree
	mov	x22,0				; ligne a l'ecran
.row:	cmp	x22,x24
	b.hs	.bar
	adr	x0,pl_nl
	ldr	x0,[x0,x19,lsl 3]
	cmp	x21,x0
	b.hs	.bar
	mov	x0,PL_RH
	mov	x1,PL_Y+1
	madd	x25,x22,x0,x1			; y
	adr	x0,pl_sel			; choisie
	ldr	x0,[x0,x19,lsl 3]
	cmp	x0,x21
	b.ne	.txt
	ldr	w5,[x23,TJ_ACCENT_CLAIR]
	mov	w6,w5
	add	x0,x20,3
	mov	x1,x25
	mov	x2,PL_W-6-PL_BAR
	mov	x3,PL_RH
	mov	x4,4
	bl	ui_rbox
.txt:	adr	x0,pl_left
	cbz	x19,.nom
	adr	x0,pl_right
.nom:	mov	x1,PL_PATH
	madd	x0,x21,x1,x0
	adr	x1,pl_nomb
	bl	preload_name
	mov	x26,8				; a droite : le voyant d'abord
	cbz	x19,.tn
	mov	x26,20
	adr	x0,pl_stat
	ldr	x0,[x0,x21,lsl 3]
	movl	w5,0x002EA043			; vert : prechargee
	movl	w1,0x00C8A020			; ambre : pas encore
	cmp	x0,0
	csel	w5,w5,w1,ne
	mov	w6,w5
	add	x0,x20,7
	add	x1,x25,7
	mov	x2,8
	mov	x3,8
	mov	x4,4
	bl	ui_rbox
.tn:	adr	x0,pl_nomb
	add	x1,x20,x26
	add	x2,x25,16
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,PL_W-34
	cbz	x19,.tt
	mov	x6,76
.tt:	bl	ui_text
	cbz	x19,.nx
	adr	x0,pl_stat			; l'etat en toutes lettres
	ldr	x0,[x0,x21,lsl 3]
	adr	x1,pl_s_oui
	adr	x2,pl_s_non
	cmp	x0,0
	csel	x0,x1,x2,ne
	bl	ui_tr
	add	x1,x20,98
	add	x2,x25,16
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,11
	adr	x5,ui_font
	mov	x6,PL_W-98-PL_BAR
	bl	ui_text
.nx:	add	x21,x21,1
	add	x22,x22,1
	b	.row
.bar:	adr	x0,pl_nl			; trop de lignes : la barre
	ldr	x21,[x0,x19,lsl 3]
	cmp	x21,x24
	b.ls	.next
	ldr	w5,[x23,TJ_CREUX]
	mov	w6,w5
	add	x0,x20,PL_W-9
	mov	x1,PL_Y+3
	mov	x2,6
	mov	x3,PL_RH
	mul	x25,x3,x24
	sub	x3,x25,4
	mov	x4,3
	bl	ui_rbox
	mul	x3,x25,x24			; curseur
	udiv	x3,x3,x21
	adr	x0,pl_off
	ldr	x0,[x0,x19,lsl 3]
	mul	x1,x25,x0
	udiv	x1,x1,x21
	add	x1,x1,PL_Y+3
	sub	x3,x3,4
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	mov	w6,w5
	add	x0,x20,PL_W-9
	mov	x2,6
	mov	x4,3
	bl	ui_rbox
.next:	add	x19,x19,1
	cmp	x19,2
	b.lo	.list
	mov	x0,0				; la fleche vers la droite
	adr	x1,pl_s_ajout
	bl	pl_btn
	mov	x0,1				; la fleche vers la gauche
	adr	x1,pl_s_retire
	bl	pl_btn
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = 0 le bouton du haut, 1 celui du bas -> x0 = son y
pl_btn_y:
	mov	x1,PL_BH+8
	mul	x0,x0,x1
	add	x0,x0,PL_Y+70
	ret

; x0 = rang, x1 = fleche : un bouton (accentue si un programme est choisi du bon cote)
pl_btn:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	pl_btn_y
	mov	x21,x0
	bl	ui_jetons
	mov	x22,x0
	adr	x0,pl_sel
	ldr	x0,[x0,x19,lsl 3]
	ldr	w5,[x22,TJ_LIGNE_FORTE]
	tbnz	x0,63,.c
	ldr	w5,[x22,TJ_ACCENT]
.c:	mov	w6,w5
	mov	x0,PL_BX
	mov	x1,x21
	mov	x2,PL_BW
	mov	x3,PL_BH
	mov	x4,6
	bl	ui_rbox
	mov	x0,x20
	mov	x1,PL_BX+8
	add	x2,x21,21
	mov	w3,0x00FFFFFF
	mov	x4,15
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = clic -> x0 = 1 change
pl_clic:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	sub	x0,x19,PL_BX			; les boutons
	cmp	x0,PL_BW
	b.hs	.listes
	mov	x21,0
.b:	mov	x0,x21
	bl	pl_btn_y
	cmp	x20,x0
	b.lt	.bn
	add	x0,x0,PL_BH
	cmp	x20,x0
	b.ge	.bn
	cbz	x21,.add
	bl	pl_remove
	b	.yes
.add:	bl	pl_add
	b	.yes
.bn:	add	x21,x21,1
	cmp	x21,2
	b.lo	.b
	b	.no
.listes:
	mov	x21,0				; quelle liste
	sub	x22,x19,PL_X1
	cmp	x22,PL_W
	b.lo	.dedans
	mov	x21,1
	sub	x22,x19,PL_X2
	cmp	x22,PL_W
	b.hs	.no
.dedans:
	adr	x0,pl_focus
	str	x21,[x0]
	subs	x20,x20,PL_Y+1
	b.lt	.no
	bl	pl_rows
	mov	x2,PL_RH
	mul	x1,x0,x2
	cmp	x20,x1
	b.hs	.no
	sub	x3,x22,PL_W-PL_BAR		; la bande : 3 lignes
	cmp	x3,PL_BAR
	b.hs	.ligne
	mov	x0,-3
	cmp	x20,x1,lsr 1
	b.hs	.mol
	mov	x0,3
.mol:	bl	pl_molette
	mov	x0,1
	b	.out
.ligne:	udiv	x0,x20,x2
	adr	x1,pl_off
	ldr	x1,[x1,x21,lsl 3]
	add	x0,x0,x1
	adr	x1,pl_nl
	ldr	x1,[x1,x21,lsl 3]
	cmp	x0,x1
	b.hs	.no
	adr	x1,pl_sel
	str	x0,[x1,x21,lsl 3]
	mov	x2,-1				; l'autre liste : plus de choix
	eor	x3,x21,1
	str	x2,[x1,x3,lsl 3]
.yes:	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = crans : la derniere liste touchee defile -> x0 = 1
pl_molette:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	pl_rows
	adr	x1,pl_focus
	ldr	x1,[x1]
	adr	x2,pl_nl
	ldr	x2,[x2,x1,lsl 3]
	subs	x2,x2,x0			; derniere premiere possible
	csel	x2,xzr,x2,lt
	adr	x3,pl_off
	ldr	x4,[x3,x1,lsl 3]
	sub	x4,x4,x19
	cmp	x4,x2
	csel	x4,x2,x4,gt
	cmp	x4,0
	csel	x4,xzr,x4,lt
	str	x4,[x3,x1,lsl 3]
	mov	x0,1
	ldp	x19,x20,[sp],16
	epilogue

; fleche droite : le programme choisi a gauche ajoute au fichier et precharge
pl_add:
	prologue
	str	x19,[sp,-16]!
	adr	x0,pl_sel
	ldr	x19,[x0]
	tbnz	x19,63,.out
	adr	x0,pl_nr
	ldr	x0,[x0]
	cmp	x0,PL_RMAX
	b.hs	.out
	adr	x0,pl_left
	mov	x1,PL_PATH
	madd	x1,x19,x1,x0
	adr	x0,pl_path
	bl	str_cat
	bl	pl_file_read			; x0 = octets
	adr	x1,ap_out
	adr	x3,ap_file
	mov	x4,x0
.c:	cbz	x4,.fin
	ldrb	w5,[x3],1
	strb	w5,[x1],1
	sub	x4,x4,1
	b	.c
.fin:	adr	x2,ap_out			; ligne precedente terminee
	cmp	x1,x2
	b.eq	.add
	ldurb	w3,[x1,-1]
	cmp	w3,10
	b.eq	.add
	mov	w3,13
	strb	w3,[x1],1
	mov	w3,10
	strb	w3,[x1],1
.add:	adr	x2,pl_path
.p:	ldrb	w3,[x2],1
	cbz	w3,.e
	strb	w3,[x1],1
	b	.p
.e:	mov	w3,13
	strb	w3,[x1],1
	mov	w3,10
	strb	w3,[x1],1
	bl	pl_file_write
	adr	x0,pl_p_preload
	adr	x1,pl_path
	sys	SYS_SPAWN
	bl	pl_load
.out:	ldr	x19,[sp],16
	epilogue

; fleche gauche : le programme choisi a droite retire du fichier et de la memoire
pl_remove:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,pl_sel
	ldr	x19,[x0,8]
	tbnz	x19,63,.out
	adr	x0,pl_right
	mov	x1,PL_PATH
	madd	x1,x19,x1,x0
	adr	x0,pl_path
	bl	str_cat
	bl	pl_file_read
	adr	x19,ap_file
	strb	wzr,[x19,x0]
	adr	x20,ap_out
.l:	ldrb	w0,[x19]
	cbz	w0,.fin
	mov	x21,x19				; fin de ligne (LF compris)
.f:	ldrb	w0,[x21]
	cbz	w0,.lu
	add	x21,x21,1
	cmp	w0,10
	b.ne	.f
.lu:	mov	x0,x19				; cette ligne ?
	mov	x1,x21
	bl	pl_trim
	sub	x2,x1,x0
	cbz	x2,.garde
	adr	x3,pl_path
	mov	x4,0
.cmp:	cmp	x4,x2
	b.hs	.bout
	ldrb	w5,[x0,x4]
	ldrb	w6,[x3,x4]
	cmp	w5,w6
	b.ne	.garde
	add	x4,x4,1
	b	.cmp
.bout:	ldrb	w5,[x3,x4]
	cbnz	w5,.garde
	mov	x19,x21				; retiree
	b	.l
.garde:	ldrb	w0,[x19],1
	strb	w0,[x20],1
	cmp	x19,x21
	b.lo	.garde
	b	.l
.fin:	mov	x1,x20
	bl	pl_file_write
	adr	x0,pl_p_unload
	adr	x1,pl_path
	sys	SYS_SPAWN
	bl	pl_load
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; SD0:/etc/preload.txt dans ap_file -> x0 = octets
pl_file_read:
	prologue
	str	x19,[sp,-16]!
	adr	x0,pl_p_file
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x1,0
	tbnz	x0,63,.vide
	mov	x19,x0
	adr	x1,ap_file
	mov	x2,AP_FILE_MAX-1
	sys	SYS_READ
	cmp	x0,0
	csel	x1,x0,xzr,gt
	str	x1,[sp,8]
	mov	x0,x19
	sys	SYS_CLOSE
	ldr	x1,[sp,8]
.vide:	mov	x0,x1
	ldr	x19,[sp],16
	epilogue

; x1 = fin de ap_out : le fichier reecrit
pl_file_write:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,ap_out
	sub	x20,x1,x0
	adr	x0,pl_p_file
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,ap_out
	mov	x2,x20
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
.out:	ldp	x19,x20,[sp],16
	epilogue

; (le bouton du bas) Actualiser
pl_appliquer:
	prologue
	bl	pl_load
	epilogue

pl_titre	db 'Pr',0xC3,0xA9,'chargement',0
pl_s_gauche	db 'Programmes',0
pl_s_droite	db 'Pr',0xC3,0xA9,'charg',0xC3,0xA9,'s',0
pl_s_oui	db 'pr',0xC3,0xA9,'charg',0xC3,0xA9,0
pl_s_non	db 'pas encore',0
pl_s_ajout	db 0xE2,0x86,0x92,0		; U+2192
pl_s_retire	db 0xE2,0x86,0x90,0		; U+2190
pl_s_actualiser	db 'Actualiser',0
pl_p_file	db 'SD0:/etc/preload.txt',0
pl_p_apps	db 'SD0:/apps/',0
pl_p_main	db '.app/main',0
pl_p_bin	db 'SD0:/bin',0
pl_p_bin2	db 'SD0:/bin/',0
pl_p_preload	db 'SD0:/bin/preload',0
pl_p_unload	db 'SD0:/bin/unload',0
		align 4

macro pl_data {
		align 8
pl_nl		dd 0				; lignes a gauche, a droite
pl_nr		dd 0
pl_sel		dd 0, 0				; choisie (-1 : aucune)
pl_off		dd 0, 0				; premiere montree
pl_focus	dd 0				; la liste que la molette fait defiler
pl_stat		rb PL_RMAX * 8			; 0 : pas encore, sinon la taille
pl_path		rb PL_PATH + 128
pl_nomb		rb 64
pl_left		rb PL_LMAX * PL_PATH
pl_right	rb PL_RMAX * PL_PATH
pl_img		rb PL_IMG * IML_SIZE
		align 8
}
