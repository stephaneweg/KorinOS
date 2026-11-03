; applets/padconf.inc - applet Manette du panneau de configuration (le padconf
; d'Onyx). SD0:/etc/gamepad.ini, lu par HIDManager (inc/pad.inc) :
;  * onglet Clavier : la touche de chaque bouton de la manette 0, le clavier
;    (une ligne touchee, puis la touche) ; section [keyboard] ;
;  * onglets Manette : ce qu'envoie la manette en direct (WEV_PAD : boutons
;    bruts, axes) et ce que les applications voient (le dessin) ; Configurer :
;    appuyer sur chaque bouton demande, Echap : ce bouton n'existe pas, Retour
;    arriere : abandon ; section [vvvv:pppp] du modele ; Oublier la retire.
;  Une section ecrite, HIDManager relit le fichier (HID_OP_PADMAP).

GP_TOP		= AP_HEAD + 42			; sous les onglets
GP_TAB_W	= 112
GP_ROW_H	= 24
GP_COLS		= 2
GP_ROWS		= 9				; lignes par colonne (17 boutons)
GP_COL_W	= 330
GP_WORD		= 12				; un mot du fichier

; le bas : Oublier, a gauche d'Appliquer
GP_FORGET_W	= 96

gp_ouvrir:
	prologue
	adr	x0,ap_dessin
	adr	x1,gp_dessin
	str	x1,[x0]
	adr	x0,ap_clic
	adr	x1,gp_clic
	str	x1,[x0]
	adr	x0,ap_touche
	adr	x1,gp_touche
	str	x1,[x0]
	adr	x0,ap_manette
	adr	x1,gp_pad
	str	x1,[x0]
	adr	x0,gp_tab
	str	xzr,[x0]
	adr	x0,gp_wait
	str	xzr,[x0]
	adr	x0,gp_step
	mov	x1,-1
	str	x1,[x0]
	bl	gp_lire
	bl	gp_libelle
	epilogue

; le bouton du bas selon l'onglet
gp_libelle:
	adr	x0,gp_tab
	ldr	x0,[x0]
	adr	x1,gp_s_defaut
	cbz	x0,.s
	adr	x1,gp_s_config
.s:	adr	x0,ap_bouton
	str	x1,[x0]
	ret

; Appliquer : Clavier, les touches de depart ; Manette, l'assistant
gp_appliquer:
	prologue
	adr	x0,gp_tab
	ldr	x0,[x0]
	cbnz	x0,.wiz
	adr	x0,gp_s_keyboard		; la section part : les touches de depart
	adr	x1,gp_vide
	bl	gp_section
	bl	gp_lire
	b	.out
.wiz:	adr	x0,gp_id
	adr	x1,gp_tab
	ldr	x1,[x1]
	ldr	w0,[x0,x1,lsl 2]
	cbnz	w0,.go
	adr	x0,ap_msg
	adr	x1,gp_s_nopad
	str	x1,[x0]
	b	.out
.go:	adr	x0,gp_step
	str	xzr,[x0]
	adr	x0,gp_map
	mov	x1,0
.z:	strb	wzr,[x0,x1]
	add	x1,x1,1
	cmp	x1,PAD_NBUTTONS
	b.lo	.z
	adr	x0,ap_msg
	str	xzr,[x0]
.out:	epilogue

; --- le fichier -----------------------------------------------------------------
; gp_keys : les touches de la section [keyboard], les defauts de HIDManager sinon
gp_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,gp_keys
	adr	x1,gp_def
	mov	x2,PAD_NBUTTONS*GP_WORD
	bl	mem_move
	adr	x0,p_gpini
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,ap_file
	mov	x2,AP_FILE_MAX-1
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	b.le	.out
	adr	x19,ap_file
	strb	wzr,[x19,x20]
	mov	x21,0				; dans [keyboard]
.l:	ldrb	w0,[x19]
	cbz	w0,.out
	mov	x20,x19				; fin de ligne
.e:	ldrb	w0,[x20]
	cbz	w0,.e1
	cmp	w0,10
	b.eq	.e1
	add	x20,x20,1
	b	.e
.e1:	ldrb	w0,[x19]
	cmp	w0,'['
	b.ne	.kv
	mov	x0,x19
	adr	x1,gp_s_keyboard
	bl	gp_prefixe
	mov	x21,x0
	b	.nx
.kv:	cbz	x21,.nx
	mov	x0,x19				; « mot = touche »
	mov	x1,x20
	bl	gp_ligne
.nx:	ldrb	w0,[x20]
	cbz	w0,.out
	add	x19,x20,1
	b	.l
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = debut attendu -> x0 = 1 s'il commence ainsi
gp_prefixe:
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	cbz	w3,.oui
	ldrb	w4,[x0,x2]
	cmp	w3,w4
	b.ne	.non
	add	x2,x2,1
	b	.c
.oui:	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0, x1 = une ligne « bouton = touche » de [keyboard] : dans gp_keys
gp_ligne:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x0				; le mot : jusqu'au blanc ou au =
.a:	cmp	x21,x20
	b.hs	.out
	ldrb	w0,[x21]
	cmp	w0,' '
	b.eq	.a1
	cmp	w0,'='
	b.eq	.a1
	add	x21,x21,1
	b	.a
.a1:	sub	x4,x21,x19			; longueur
	cbz	x4,.out
	adr	x2,gp_noms			; quel bouton ?
	mov	x3,0
.b:	cmp	x3,PAD_NBUTTONS
	b.hs	.out
	mov	x5,0
.c:	cmp	x5,x4
	b.hs	.cf
	ldrb	w6,[x19,x5]
	ldrb	w7,[x2,x5]
	cmp	w6,w7
	b.ne	.n
	add	x5,x5,1
	b	.c
.cf:	ldrb	w7,[x2,x5]
	cbz	w7,.got
.n:	add	x2,x2,8
	add	x3,x3,1
	b	.b
.got:	ldrb	w0,[x21]			; la valeur
	add	x21,x21,1
	cmp	x21,x20
	b.hs	.out
	cmp	w0,'='
	b.ne	.got
.sp:	ldrb	w0,[x21]
	cmp	w0,' '
	b.ne	.val
	add	x21,x21,1
	b	.sp
.val:	adr	x0,gp_keys
	mov	x1,GP_WORD
	madd	x0,x3,x1,x0
	mov	x1,0
.v:	cmp	x21,x20
	b.hs	.vf
	ldrb	w2,[x21],1
	cmp	w2,' '
	b.ls	.vf
	cmp	x1,GP_WORD-1
	b.hs	.vf
	strb	w2,[x0,x1]
	add	x1,x1,1
	b	.v
.vf:	strb	wzr,[x0,x1]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = en-tete « [xxx] », x1 = corps (0 final ; vide : la section part) : la
; section remplacee dans SD0:/etc/gamepad.ini, HIDManager la relit -> x0 < 0 si erreur
gp_section:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x23,x0
	mov	x24,x1
	mov	x22,0				; octets du fichier
	adr	x0,p_gpini
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.vide
	mov	x19,x0
	adr	x1,ap_file
	mov	x2,AP_FILE_MAX-1
	sys	SYS_READ
	cmp	x0,0
	csel	x22,x0,xzr,gt
	mov	x0,x19
	sys	SYS_CLOSE
.vide:	adr	x19,ap_file
	strb	wzr,[x19,x22]
	adr	x20,ap_out			; sortie
	mov	x21,0				; 1 : dans la section remplacee
.l:	ldrb	w0,[x19]
	cbz	w0,.fin
	ldrb	w0,[x19]
	cmp	w0,'['
	b.ne	.ligne
	mov	x0,x19
	mov	x1,x23
	bl	gp_prefixe
	mov	x21,x0
.ligne:	mov	x2,x19				; fin de ligne
.e:	ldrb	w0,[x2]
	cbz	w0,.e1
	add	x2,x2,1
	cmp	w0,10
	b.ne	.e
.e1:	cbnz	x21,.saut
.c:	cmp	x19,x2
	b.hs	.l
	ldrb	w0,[x19],1
	strb	w0,[x20],1
	b	.c
.saut:	mov	x19,x2
	b	.l
.fin:	ldrb	w0,[x24]
	cbz	w0,.ecr
	mov	x0,x20				; la nouvelle section, a la fin
	mov	x1,x23
	bl	str_cat
	mov	w1,10
	strb	w1,[x0],1
	strb	wzr,[x0]
	mov	x1,x24
	bl	str_cat
	mov	x20,x0
.ecr:	strb	wzr,[x20]
	adr	x0,p_gpini
	adr	x1,ap_out
	bl	file_write
	mov	x19,x0
	adr	x0,s_gphid			; HIDManager relit
	mov	x1,500
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	sub	sp,sp,512
	mov	x1,HID_OP_PADMAP
	str	x1,[sp]
	mov	x1,sp
	sys	SYS_SVC_CALL
	add	sp,sp,512
.out:	mov	x0,x19
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; [keyboard] depuis gp_keys
gp_sauve_clavier:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,gp_corps
	mov	x19,0
.l:	adr	x1,gp_noms
	add	x1,x1,x19,lsl 3
	bl	str_cat
	adr	x1,gp_s_eq
	bl	str_cat
	adr	x1,gp_keys
	mov	x2,GP_WORD
	madd	x1,x19,x2,x1
	bl	str_cat
	mov	w1,10
	strb	w1,[x0],1
	strb	wzr,[x0]
	add	x19,x19,1
	cmp	x19,PAD_NBUTTONS
	b.lo	.l
	adr	x0,gp_s_keyboard
	adr	x1,gp_corps
	bl	gp_section
	bl	gp_msg_ecrit
	ldp	x19,x20,[sp],16
	epilogue

; x0 = resultat d'ecriture : le message
gp_msg_ecrit:
	adr	x1,gp_s_ok
	tbz	x0,63,.s
	adr	x1,gp_s_err
.s:	adr	x0,ap_msg
	str	x1,[x0]
	ret

; « [vvvv:pppp] » du modele de l'onglet -> dans gp_tete
gp_tete_modele:
	adr	x0,gp_id
	adr	x1,gp_tab
	ldr	x1,[x1]
	ldr	w0,[x0,x1,lsl 2]
	adr	x1,gp_tete
	mov	w2,'['
	strb	w2,[x1],1
	mov	x3,28
.h:	cmp	x3,12
	b.ne	.h1
	mov	w2,':'
	strb	w2,[x1],1
.h1:	lsr	w2,w0,w3
	and	w2,w2,15
	cmp	w2,10
	add	w4,w2,'0'
	add	w5,w2,'a'-10
	csel	w2,w4,w5,lo
	strb	w2,[x1],1
	subs	x3,x3,4
	b.pl	.h
	mov	w2,']'
	strb	w2,[x1],1
	strb	wzr,[x1]
	ret

; l'assistant fini : [vvvv:pppp], un numero de bouton brut par bouton
gp_sauve_modele:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,gp_corps
	mov	x19,0
.l:	adr	x1,gp_map
	ldrb	w20,[x1,x19]
	cbz	w20,.n				; (absent, ou le chapeau)
	adr	x1,gp_noms
	add	x1,x1,x19,lsl 3
	bl	str_cat
	adr	x1,gp_s_eq
	bl	str_cat
	mov	x1,x0
	mov	x0,x20
	bl	fmt_dec
	mov	w1,10
	strb	w1,[x0],1
	strb	wzr,[x0]
.n:	add	x19,x19,1
	cmp	x19,PAD_NBUTTONS
	b.lo	.l
	bl	gp_tete_modele
	adr	x0,gp_tete
	adr	x1,gp_corps
	bl	gp_section
	bl	gp_msg_ecrit
	ldp	x19,x20,[sp],16
	epilogue

gp_oublier:
	prologue
	adr	x0,gp_id
	adr	x1,gp_tab
	ldr	x1,[x1]
	ldr	w0,[x0,x1,lsl 2]
	cbz	w0,.out
	bl	gp_tete_modele
	adr	x0,gp_tete
	adr	x1,gp_vide
	bl	gp_section
	adr	x0,ap_msg
	adr	x1,gp_s_oublie
	str	x1,[x0]
.out:	epilogue

; --- les evenements ---------------------------------------------------------------
; WEV_PAD (x0 = evenement) -> x0 = 1 : redessiner
gp_pad:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	w19,[x0,4]			; manette
	cmp	w19,PAD_MAX
	b.hs	.non
	ldr	w1,[x0,8]			; boutons
	ldr	w2,[x0,12]			; bruts
	ldr	x3,[x0,16]			; axes
	ldr	w4,[x0,24]			; modele
if defined GP_TEST				; (essai sans manette : le clavier fait la manette 1)
	mov	w19,1
	mov	w2,w1
	movl	w4,0x045E028E
end if
	adr	x5,gp_raw
	ldr	w20,[x5,x19,lsl 2]		; ancien brut
	str	w2,[x5,x19,lsl 2]
	adr	x5,gp_btn
	ldr	w6,[x5,x19,lsl 2]
	str	w1,[x5,x19,lsl 2]
	adr	x5,gp_ax
	str	x3,[x5,x19,lsl 3]
	adr	x5,gp_id
	str	w4,[x5,x19,lsl 2]
	adr	x5,gp_step			; l'assistant
	ldr	x7,[x5]
	tbnz	x7,63,.oui
	adr	x8,gp_tab
	ldr	x8,[x8]
	cmp	x8,x19
	b.ne	.oui
	cmp	x7,4				; (la croix : le chapeau suffit)
	b.hs	.brut
	mov	w9,1
	lsl	w9,w9,w7
	tst	w1,w9
	b.eq	.brut
	tst	w6,w9
	b.ne	.brut
	mov	x0,0				; ce bouton est le chapeau
	b	.pris
.brut:	bic	w9,w2,w20			; un bouton vient d'etre presse
	and	w9,w9,0x0FFFFFFF
	cbz	w9,.oui
	rbit	w9,w9
	clz	w9,w9
	add	x0,x9,1
.pris:	adr	x1,gp_map
	strb	w0,[x1,x7]
	bl	gp_suivant
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; l'assistant passe au bouton suivant ; apres le dernier, le fichier
gp_suivant:
	prologue
	adr	x0,gp_step
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	cmp	x1,PAD_NBUTTONS
	b.lo	.out
	mov	x1,-1
	str	x1,[x0]
	bl	gp_sauve_modele
.out:	epilogue

; x0 = evenement clavier -> x0 = 1 : pris (l'applet garde presque tout : une
; touche de test ne doit pas changer d'applet)
gp_touche:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	w20,[x19,EV_CODE]
	adr	x0,gp_step
	ldr	x0,[x0]
	tbnz	x0,63,.nw
	cmp	w20,KEY_ESC
	b.ne	.w1
	bl	gp_suivant			; ce bouton n'existe pas
	b	.oui
.w1:	cmp	w20,0x2A			; retour arriere : abandon
	b.ne	.oui
	adr	x0,gp_step
	mov	x1,-1
	str	x1,[x0]
	adr	x0,ap_msg
	adr	x1,gp_s_abandon
	str	x1,[x0]
	b	.oui
.nw:	adr	x0,gp_wait
	ldr	x0,[x0]
	cbz	x0,.libre
	adr	x0,gp_wait
	str	xzr,[x0]
	cmp	w20,KEY_ESC
	b.eq	.oui
	bl	gp_mot				; la touche de la ligne choisie
	cbz	x0,.oui
	mov	x1,x0
	adr	x0,gp_sel
	ldr	x0,[x0]
	adr	x2,gp_keys
	mov	x3,GP_WORD
	madd	x0,x0,x3,x2
	mov	x2,GP_WORD
	bl	mem_move
	bl	gp_unique
	bl	gp_sauve_clavier
	b	.oui
.libre:	cmp	w20,KEY_ESC			; Echap reste au panneau
	b.ne	.oui
	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; une touche ne fait qu'un bouton : celle de gp_sel quitte les autres lignes
gp_unique:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,gp_keys
	adr	x0,gp_sel
	ldr	x20,[x0]
	mov	x0,GP_WORD
	madd	x21,x20,x0,x19			; la touche choisie
	mov	x22,0
.l:	cmp	x22,x20
	b.eq	.n
	mov	x0,GP_WORD
	madd	x0,x22,x0,x19
	mov	x1,x21
	bl	str_eq
	cbz	x0,.n
	mov	x0,GP_WORD
	madd	x0,x22,x0,x19
	adr	x1,gp_none
	mov	x2,5
	bl	mem_move
.n:	add	x22,x22,1
	cmp	x22,PAD_NBUTTONS
	b.lo	.l
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (x19 = evenement, w20 = code HID) -> x0 = le mot du fichier (gp_un), 0 : aucun
gp_mot:
	adr	x0,gp_codes
	adr	x1,gp_mots
.c:	ldrb	w2,[x0],1
	cbz	w2,.lettre
	cmp	w2,w20
	b.eq	.got
	add	x1,x1,GP_WORD
	b	.c
.got:	mov	x0,x1
	ret
.lettre:
	ldr	w2,[x19,EV_CHAR]
	orr	w3,w2,0x20
	sub	w4,w3,'a'
	cmp	w4,25
	b.ls	.un
	sub	w4,w2,'0'
	cmp	w4,9
	b.hi	.non
	mov	w3,w2
.un:	adr	x0,gp_un
	strb	w3,[x0]
	strb	wzr,[x0,1]
	ret
.non:	mov	x0,0
	ret

; x0, x1 = clic dans la fenetre -> x0 = 1 : redessiner
gp_clic:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	subs	x0,x20,AP_HEAD+2		; les onglets
	b.lt	.non
	cmp	x0,30
	b.hs	.corps
	subs	x0,x19,SIDE_W+AP_MARGE
	b.lt	.non
	mov	x1,GP_TAB_W
	udiv	x0,x0,x1
	cmp	x0,PAD_MAX
	b.hs	.non
	adr	x1,gp_tab
	str	x0,[x1]
	adr	x1,gp_wait
	str	xzr,[x1]
	adr	x1,gp_step
	mov	x2,-1
	str	x2,[x1]
	adr	x1,ap_msg
	str	xzr,[x1]
	bl	gp_libelle
	b	.oui
.corps:	adr	x0,gp_tab
	ldr	x0,[x0]
	cbnz	x0,.pad
	subs	x0,x19,SIDE_W+AP_MARGE		; Clavier : une ligne
	b.lt	.non
	mov	x1,GP_COL_W
	udiv	x2,x0,x1
	cmp	x2,GP_COLS
	b.hs	.non
	subs	x0,x20,GP_TOP
	b.lt	.non
	mov	x1,GP_ROW_H
	udiv	x3,x0,x1
	cmp	x3,GP_ROWS
	b.hs	.non
	mov	x1,GP_ROWS
	madd	x2,x2,x1,x3
	cmp	x2,PAD_NBUTTONS
	b.hs	.non
	adr	x0,gp_sel
	str	x2,[x0]
	adr	x0,gp_wait
	mov	x1,1
	str	x1,[x0]
	b	.oui
.pad:	adr	x0,ui_w				; Oublier
	ldp	x2,x3,[x0]
	sub	x4,x3,BTN_H+12
	cmp	x20,x4
	b.lt	.non
	sub	x5,x2,AP_MARGE+2*BTN_W+8
	cmp	x19,x5
	b.lt	.non
	add	x5,x5,BTN_W
	cmp	x19,x5
	b.ge	.non
	bl	gp_oublier
	bl	draw				; (le message reste)
.non:	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- le dessin ------------------------------------------------------------------
gp_dessin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	ui_jetons
	mov	x23,x0
	mov	x19,0				; les onglets
.t:	mov	x0,GP_TAB_W
	mov	x1,SIDE_W+AP_MARGE
	madd	x20,x19,x0,x1
	adr	x0,gp_tab
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.t0
	ldr	w5,[x23,TJ_ACCENT_CLAIR]
	mov	w6,w5
	mov	x0,x20
	mov	x1,AP_HEAD+2
	mov	x2,GP_TAB_W-6
	mov	x3,28
	mov	x4,6
	bl	ui_rbox
.t0:	adr	x0,gp_s_tab0
	cbz	x19,.t1
	adr	x0,gp_s_tabn
	add	x0,x0,x19,lsl 4
.t1:	bl	ui_tr
	add	x1,x20,10
	mov	x2,AP_HEAD+21
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	add	x19,x19,1
	cmp	x19,PAD_MAX
	b.lo	.t
	adr	x0,gp_tab
	ldr	x0,[x0]
	cbnz	x0,.pad
	bl	gp_d_clavier
	b	.out
.pad:	bl	gp_d_manette
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (x23 = jetons) l'onglet Clavier : un bouton par ligne, sa touche, allumee tenue
gp_d_clavier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,0
.l:	mov	x1,GP_ROWS
	udiv	x0,x19,x1			; colonne
	msub	x1,x0,x1,x19			; ligne
	mov	x2,GP_COL_W
	mov	x3,SIDE_W+AP_MARGE
	madd	x20,x0,x2,x3			; x
	mov	x2,GP_ROW_H
	mov	x3,GP_TOP
	madd	x21,x1,x2,x3			; y
	adr	x0,gp_sel			; la ligne qui attend sa touche
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.r0
	adr	x0,gp_wait
	ldr	x0,[x0]
	cbz	x0,.r0
	ldr	w5,[x23,TJ_ACCENT_CLAIR]
	mov	w6,w5
	sub	x0,x20,4
	mov	x1,x21
	mov	x2,GP_COL_W-14
	mov	x3,GP_ROW_H-2
	mov	x4,5
	bl	ui_rbox
.r0:	adr	x0,gp_labels
	mov	x1,16
	madd	x0,x19,x1,x0
	bl	ui_tr
	add	x1,x20,4
	add	x2,x21,16
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x0,x19				; le capuchon : la touche
	bl	gp_lit_clavier
	mov	x22,x0
	adr	x0,gp_keys
	mov	x1,GP_WORD
	madd	x0,x19,x1,x0
	adr	x1,gp_sel
	ldr	x1,[x1]
	cmp	x1,x19
	b.ne	.k
	adr	x1,gp_wait
	ldr	x1,[x1]
	cbz	x1,.k
	adr	x0,gp_s_q
.k:	add	x1,x20,150
	add	x2,x21,1
	mov	x3,110
	mov	x4,GP_ROW_H-4
	mov	x5,x22
	bl	gp_cap
	add	x19,x19,1
	cmp	x19,PAD_NBUTTONS
	b.lo	.l
	adr	x0,gp_wait
	ldr	x0,[x0]
	adr	x1,gp_s_hint
	cbz	x0,.h
	adr	x1,gp_s_press
.h:	mov	x0,x1
	bl	ui_tr
	mov	x1,SIDE_W+AP_MARGE
	mov	x2,GP_TOP+GP_ROWS*GP_ROW_H+24
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = bouton -> x0 = 1 si le clavier le tient
gp_lit_clavier:
	adr	x1,gp_btn
	ldr	w1,[x1]
	lsr	w1,w1,w0
	and	x0,x1,1
	ret

; x0 = texte, x1..x4 = rectangle, x5 = 1 allume ; (x23 = jetons) : un capuchon
gp_cap:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x24,x4
	mov	x25,x5
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	cbz	x25,.c
	ldr	w5,[x23,TJ_ACCENT_FONCE]
.c:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x24
	mov	x4,4
	bl	ui_rbox
	ldr	w5,[x23,TJ_ELEVE]
	ldr	w6,[x23,TJ_ELEVE_BAS]
	cbz	x25,.f
	ldr	w5,[x23,TJ_ACCENT]
	mov	w6,w5
.f:	add	x0,x20,1
	add	x1,x21,1
	sub	x2,x22,2
	sub	x3,x24,2
	mov	x4,3
	bl	ui_rbox
	ldrb	w0,[x19]
	cbz	w0,.out
	mov	x0,x19
	mov	x1,12
	adr	x2,ui_font
	bl	ui_text_width
	sub	x1,x22,x0
	asr	x1,x1,1
	add	x1,x20,x1
	mov	x2,x24
	lsr	x2,x2,1
	add	x2,x21,x2
	add	x2,x2,4
	ldr	w3,[x23,TJ_TEXTE1]
	cbz	x25,.tx
	mov	w3,0x00FFFFFF
.tx:	mov	x0,x19
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.out:	ldp	x24,x25,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (x23 = jetons) l'onglet d'une manette
gp_d_manette:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,gp_tab
	ldr	x19,[x19]
	adr	x0,gp_id
	ldr	w20,[x0,x19,lsl 2]
	cbnz	w20,.vue
	adr	x0,gp_s_none1
	bl	tr0
	mov	x1,SIDE_W+AP_MARGE
	mov	x2,GP_TOP+20
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	b	.out
.vue:	sub	sp,sp,32			; le modele
	mov	x0,sp
	adr	x1,gp_s_model
	bl	tr_cat
	bl	gp_tete_modele
	mov	x0,sp
	bl	str_len
	add	x0,x0,sp
	adr	x1,gp_tete
	bl	str_cat
	mov	x0,sp
	mov	x1,SIDE_W+AP_MARGE
	mov	x2,GP_TOP+14
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	add	sp,sp,32
	adr	x0,gp_raw			; boutons bruts, 1 a 16
	ldr	w21,[x0,x19,lsl 2]
	mov	x20,0
.b:	sub	sp,sp,16
	add	x0,x20,1
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	mov	x1,31
	mov	x2,SIDE_W+AP_MARGE
	madd	x1,x20,x1,x2
	mov	x2,GP_TOP+24
	mov	x3,28
	mov	x4,20
	lsr	w5,w21,w20
	and	x5,x5,1
	bl	gp_cap
	add	sp,sp,16
	add	x20,x20,1
	cmp	x20,16
	b.lo	.b
	adr	x0,gp_ax			; quatre axes, -1000..1000
	ldr	x21,[x0,x19,lsl 3]
	mov	x20,0
.a:	lsl	x0,x20,4
	asr	x22,x21,x0
	sxth	x22,w22
	mov	x0,124
	mov	x1,SIDE_W+AP_MARGE
	madd	x0,x20,x0,x1
	mov	x1,GP_TOP+54
	mov	x2,112
	mov	x3,14
	mov	x4,4
	ldr	w5,[x23,TJ_CREUX]
	mov	w6,w5
	bl	ui_rbox
	add	x2,x22,1000			; 0..2000 -> 0..112
	mov	x0,112
	mul	x2,x2,x0
	mov	x0,2000
	sdiv	x2,x2,x0
	cmp	x2,4
	b.lt	.an
	mov	x0,124
	mov	x1,SIDE_W+AP_MARGE
	madd	x0,x20,x0,x1
	mov	x1,GP_TOP+54
	mov	x3,14
	mov	x4,4
	ldr	w5,[x23,TJ_ACCENT]
	mov	w6,w5
	bl	ui_rbox
.an:	add	x20,x20,1
	cmp	x20,4
	b.lo	.a
	adr	x0,gp_btn			; ce que voient les applications
	ldr	w0,[x0,x19,lsl 2]
	mov	x1,SIDE_W+AP_MARGE+150
	mov	x2,GP_TOP+84
	bl	gp_d_pad
	adr	x0,gp_step			; l'assistant
	ldr	x20,[x0]
	tbnz	x20,63,.forget
	adr	x0,gp_steps
	mov	x1,40
	madd	x20,x20,x1,x0
	mov	x0,sp				; (sous le dessin)
	sub	sp,sp,96
	mov	x0,sp
	adr	x1,gp_s_appuyer
	bl	tr_cat
	mov	x1,x20
	bl	tr_cat
	mov	x0,sp
	mov	x1,SIDE_W+AP_MARGE
	mov	x2,GP_TOP+262
	ldr	w3,[x23,TJ_ACCENT]
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	add	sp,sp,96
	adr	x0,gp_s_wizhint
	bl	tr0
	mov	x1,SIDE_W+AP_MARGE
	mov	x2,GP_TOP+282
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.forget:
	adr	x0,ui_w				; Oublier, a gauche d'Appliquer
	ldp	x20,x21,[x0]
	sub	x0,x20,AP_MARGE+2*BTN_W+8
	sub	x1,x21,BTN_H+12
	adr	x2,gp_s_oublier
	mov	x3,0
	bl	ap_button
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = boutons PAD_*, x1, x2 = coin (300 x 150) ; (x23 = jetons) : la manette vue du dessus
gp_d_pad:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	mov	w19,w0
	mov	x20,x1
	mov	x21,x2
	ldr	w5,[x23,TJ_ELEVE]
	ldr	w6,[x23,TJ_CREUX]
	add	x1,x21,20
	mov	x0,x20
	mov	x2,300
	mov	x3,130
	mov	x4,14
	bl	ui_rbox
	adr	x22,gp_shape
	mov	x24,17
.l:	ldrb	w0,[x22]			; bouton
	ldrb	w2,[x22,1]			; dy
	ldrb	w3,[x22,2]			; l
	ldrb	w4,[x22,3]			; h
	ldrh	w1,[x22,4]			; dx
	lsr	w5,w19,w0
	and	x5,x5,1
	add	x1,x1,x20
	add	x2,x2,x21
	add	x0,x22,6
	bl	gp_cap
	add	x22,x22,12
	subs	x24,x24,1
	b.ne	.l
	ldp	x24,x25,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

macro gp_k b_, dx_, dy_, l_, h_, t_ {
local a
	db	b_, dy_, l_, h_
	dh	dx_
a:	db	t_, 0
	rb	6-($-a)
}
gp_shape:
	gp_k 8, 10, 0, 60, 16, 'L'
	gp_k 10, 80, 0, 50, 16, 'L2'
	gp_k 11, 170, 0, 50, 16, 'R2'
	gp_k 9, 230, 0, 60, 16, 'R'
	gp_k 0, 44, 40, 22, 22, ''
	gp_k 1, 44, 84, 22, 22, ''
	gp_k 2, 22, 62, 22, 22, ''
	gp_k 3, 66, 62, 22, 22, ''
	gp_k 7, 234, 40, 24, 22, 'Y'
	gp_k 4, 234, 84, 24, 22, 'A'
	gp_k 6, 210, 62, 24, 22, 'X'
	gp_k 5, 258, 62, 24, 22, 'B'
	gp_k 12, 112, 56, 34, 14, 'SEL'
	gp_k 13, 154, 56, 34, 14, 'STA'
	gp_k 16, 136, 34, 28, 14, 'H'
	gp_k 14, 104, 104, 34, 18, 'L3'
	gp_k 15, 162, 104, 34, 18, 'R3'

macro gp_8 t_ {
local a
a:	db	t_, 0
	rb	8-($-a)
}
macro gp_12 t_ {
local a
a:	db	t_, 0
	rb	12-($-a)
}
macro gp_16 t_ {
local a
a:	db	t_, 0
	rb	16-($-a)
}
macro gp_40 t_ {
local a
a:	db	t_, 0
	rb	40-($-a)
}
; les noms du fichier, dans l'ordre des bits PAD_*
gp_noms:
	gp_8 'up'
	gp_8 'down'
	gp_8 'left'
	gp_8 'right'
	gp_8 'a'
	gp_8 'b'
	gp_8 'x'
	gp_8 'y'
	gp_8 'l'
	gp_8 'r'
	gp_8 'l2'
	gp_8 'r2'
	gp_8 'select'
	gp_8 'start'
	gp_8 'l3'
	gp_8 'r3'
	gp_8 'home'
gp_labels:
	gp_16 'Haut'
	gp_16 'Bas'
	gp_16 'Gauche'
	gp_16 'Droite'
	gp_16 'A (bas)'
	gp_16 'B (droite)'
	gp_16 'X (gauche)'
	gp_16 'Y (haut)'
	gp_16 'L'
	gp_16 'R'
	gp_16 'L2'
	gp_16 'R2'
	gp_16 'Select'
	gp_16 'Start'
	gp_16 'L3'
	gp_16 'R3'
	gp_16 'Home'
; les touches de depart de HIDManager (pad_def_keys)
gp_def:
	gp_12 'up'
	gp_12 'down'
	gp_12 'left'
	gp_12 'right'
	gp_12 'a'
	gp_12 'b'
	gp_12 'x'
	gp_12 'y'
	gp_12 'l'
	gp_12 'r'
	gp_12 'none'
	gp_12 'none'
	gp_12 'backspace'
	gp_12 'enter'
	gp_12 'none'
	gp_12 'none'
	gp_12 'none'
gp_codes	db 0x52,0x51,0x50,0x4F,0x28,0x2A,0x2C,0x2B,0x46,0x48,0
		align 4
gp_mots:
	gp_12 'up'
	gp_12 'down'
	gp_12 'left'
	gp_12 'right'
	gp_12 'enter'
	gp_12 'backspace'
	gp_12 'space'
	gp_12 'tab'
	gp_12 'prtsc'
	gp_12 'pause'
gp_steps:
	gp_40 'HAUT de la croix'
	gp_40 'BAS de la croix'
	gp_40 'GAUCHE de la croix'
	gp_40 'DROITE de la croix'
	gp_40 'le bouton du BAS (Xbox A)'
	gp_40 'le bouton de DROITE (Xbox B)'
	gp_40 'le bouton de GAUCHE (Xbox X)'
	gp_40 'le bouton du HAUT (Xbox Y)'
	gp_40 <'l',0x27,0xC3,0xA9,'paule GAUCHE (L)'>
	gp_40 <'l',0x27,0xC3,0xA9,'paule DROITE (R)'>
	gp_40 <'la g',0xC3,0xA2,'chette GAUCHE (L2)'>
	gp_40 <'la g',0xC3,0xA2,'chette DROITE (R2)'>
	gp_40 'SELECT'
	gp_40 'START'
	gp_40 'le clic du stick GAUCHE (L3)'
	gp_40 'le clic du stick DROIT (R3)'
	gp_40 'HOME'
gp_titre	db 'Manette',0
gp_s_tab0	db 'Clavier',0
		rb 8
gp_s_tabn	rb 16
		gp_16 'Manette 1'
		gp_16 'Manette 2'
		gp_16 'Manette 3'
gp_s_defaut	db 'Par d',0xC3,0xA9,'faut',0
gp_s_config	db 'Configurer',0
gp_s_oublier	db 'Oublier',0
gp_s_keyboard	db '[keyboard]',0
gp_s_eq		db ' = ',0
gp_vide		db 0
gp_none		db 'none',0
gp_s_q		db '?',0
gp_s_hint	db 'Touchez une ligne, puis la touche de ce bouton.',0
gp_s_press	db 'Appuyez sur la touche (',0xC3,0x89,'chap : annuler).',0
gp_s_nopad	db 'Appuyez d',0x27,'abord sur un bouton de la manette.',0
gp_s_none1	db 'Aucune manette vue ici : branchez-la, appuyez sur un bouton.',0
gp_s_model	db 'Mod',0xC3,0xA8,'le ',0
gp_s_appuyer	db 'Appuyez sur ',0
gp_s_wizhint	db 0xC3,0x89,'chap : ce bouton n',0x27,'existe pas. Retour arri',0xC3,0xA8,'re : abandon.',0
gp_s_ok		db 'Enregistr',0xC3,0xA9,' : les applications l',0x27,'utilisent.',0
gp_s_err	db 'SD0:/etc/gamepad.ini non modifi',0xC3,0xA9,0
gp_s_oublie	db 'Ce mod',0xC3,0xA8,'le reprend les boutons par d',0xC3,0xA9,'faut.',0
gp_s_abandon	db 'Abandon.',0
p_gpini		db 'SD0:/etc/gamepad.ini',0
s_gphid		db 'hid',0
		align 4

macro gp_data {
		align 8
gp_tab		dd 0
gp_sel		dd 0
gp_wait		dd 0
gp_step		dd -1
gp_btn		rb PAD_MAX*4
gp_raw		rb PAD_MAX*4
gp_id		rb PAD_MAX*4
		align 8
gp_ax		rb PAD_MAX*8
gp_keys		rb PAD_NBUTTONS*GP_WORD
gp_map		rb PAD_NBUTTONS
gp_un		rb 4
gp_tete		rb 16
gp_corps	rb 512
}
