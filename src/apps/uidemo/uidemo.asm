; ============================================================================
;  uidemo - demonstration et essai des widgets de UIKit (docs/KITS.md,
;  docs/UIKIT-MANQUES.md) : chaque etape du chantier y ajoute une page.
;
;  A gauche, la liste des pages (une ui_list) ; a droite, les widgets de la
;  page choisie (les autres sont caches : ui_w_show). Ligne d'etat en bas :
;  le dernier evenement rendu par ui_w_event.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W	= 780
WIN_H	= 540
M	= 16				; marge
NAV_W	= 150
CX	= M+NAV_W+M			; x du contenu
CW	= WIN_W-CX-M			; sa largeur
ST_H	= 34				; ligne d'etat
MAXW	= 128				; widgets enregistres

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'

start:
	stp	x0,x1,[sp,-16]!
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,ui_win
	bl	ui_init
	mov	x0,-1				; fenetre redimensionnable
	mov	x1,-1
	mov	x2,WIN_W
	mov	x3,WIN_H
	adr	x4,s_title
	bl	tr_x4
	mov	x5,WF_RESIZABLE
	bl	ui_window_ex
	tbnz	x0,63,quit
	adr	x0,menus			; menus (etats : ui_menu_state)
	bl	ui_menu
	mov	x0,3				; « Exporter » : grisee
	mov	x1,UMS_DISABLED
	bl	ui_menu_state
	mov	x0,600				; taille minimale, fonction de relayout
	mov	x1,420
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	bl	ui_jetons
	adr	x1,jetons
	str	x0,[x1]
	bl	build
	bl	paint
.loop:	mov	x0,200				; (200 ms : infobulles)
	bl	ui_event
	cbnz	x0,.ev
	bl	ui_w_idle
	b	.loop
.ev:	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MENU
	b.eq	.menu
	cmp	x0,WEV_KEY			; raccourcis des menus (par le kit)
	b.ne	.w
	mov	x0,0
	bl	ui_menu_key
	tbnz	x0,63,.w
	bl	on_menu
	b	.loop
.menu:	adr	x0,ui_ev
	ldr	w0,[x0,EV_CODE]
	bl	on_menu
	b	.loop
.w:	mov	x0,0
	bl	ui_w_event			; les widgets d'abord
	cbz	x1,.loop
	bl	report				; x0 = widget, x1 = code
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; menu contextuel de la grille, au point du clic
uk_ctx_menu:
	prologue
	adr	x0,s_ctx				; (les libelles, traduits)
	adr	x1,ctx_tr
	bl	tr_block
	adr	x0,ui_ev
	ldrsw	x0,[x0,EV_X]
	adr	x1,ui_ev
	ldrsw	x1,[x1,EV_Y]
	adr	x2,ctx_tr
	mov	x3,160
	bl	ui_popup_local
	tbnz	x0,63,.out
	adr	x1,ctx_tr
.f:	cbz	x0,.got
	ldrb	w2,[x1],1
	cbnz	w2,.f
	sub	x0,x0,1
	b	.f
.got:	adr	x0,s_ctxr
	bl	status2
.out:	epilogue

; x0 = entree de menu choisie (barre de menus ou raccourci)
on_menu:
	prologue
	cmp	x0,1
	b.eq	quit
	cmp	x0,2				; Grille : cochee / decochee
	b.ne	.out
	adr	x1,grid
	ldr	x2,[x1]
	eor	x2,x2,1
	str	x2,[x1]
	lsl	x1,x2,1				; (UMS_CHECKED)
	mov	x0,2
	bl	ui_menu_state
	adr	x0,s_grid_off
	adr	x1,grid
	ldr	x1,[x1]
	cbz	x1,.st
	adr	x0,s_grid_on
.st:	bl	status
.out:	epilogue

; x0 = widget, x1 = page : enregistre le widget de cette page
reg:
	adr	x2,nw
	ldr	x3,[x2]
	cmp	x3,MAXW
	b.hs	.ret
	add	x4,x3,1
	str	x4,[x2]
	adr	x2,wtab
	add	x2,x2,x3,lsl 4
	stp	x0,x1,[x2]
.ret:	ret

; cree les widgets de toutes les pages
build:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,M				; --- les pages
	mov	x1,M
	mov	x2,NAV_W
	mov	x3,WIN_H-ST_H-2*M
	mov	x4,0
	bl	ui_list
	adr	x1,w_nav
	str	x0,[x1]
	mov	x19,x0
	adr	x20,pages
.pg:	ldrb	w0,[x20]
	cbz	w0,.pgd
	mov	x0,x19
	mov	x1,x20
	bl	tr_x1
	mov	x2,0
	bl	ui_list_add
.pgs:	ldrb	w0,[x20],1
	cbnz	w0,.pgs
	b	.pg
.pgd:	mov	x0,x19
	mov	x1,0
	bl	ui_w_set_value
	bl	build_list
	bl	build_text
	bl	build_dialogs
	bl	build_choice
	bl	build_area
	bl	build_values
	bl	build_split
	bl	build_tabs
	bl	build_misc
	bl	build_dates
	bl	build_grid
	ldp	x19,x20,[sp],16
	epilogue

; --- page 0 : liste et defilement ---------------------------------------------
build_list:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,CX
	mov	x1,M+24
	mov	x2,440
	mov	x3,300
	mov	x4,UL_HEADER
	bl	ui_list
	adr	x1,w_list
	str	x0,[x1]
	mov	x19,x0
	mov	x1,0
	bl	reg
	mov	x0,x19
	adr	x1,s_c_name
	bl	tr_x1
	mov	x2,200
	mov	x3,0
	bl	ui_list_column
	mov	x0,x19
	adr	x1,s_c_size
	bl	tr_x1
	mov	x2,100
	mov	x3,ULC_RIGHT
	bl	ui_list_column
	mov	x0,x19
	adr	x1,s_c_kind
	bl	tr_x1
	mov	x2,140
	mov	x3,0
	bl	ui_list_column
	adr	x20,items
.add:	ldrb	w0,[x20]
	cbz	w0,.added
	mov	x0,x19
	mov	x1,x20
	bl	tr_x1
	mov	x2,0
	bl	ui_list_add
.skip:	ldrb	w0,[x20],1
	cbnz	w0,.skip
	b	.add
.added:	mov	x0,x19
	mov	x1,0
	bl	ui_w_set_value
	mov	x0,x19
	bl	ui_w_focus
	mov	x0,CX+440+24			; barre de defilement seule
	mov	x1,M+24
	mov	x2,12
	mov	x3,300
	mov	x4,0
	bl	ui_scrollbar
	adr	x1,w_sb
	str	x0,[x1]
	mov	x19,x0
	mov	x1,0
	bl	reg
	mov	x0,x19
	mov	x1,0
	mov	x2,100
	mov	x3,20
	mov	x4,1
	bl	ui_w_set_range
	mov	x0,x19
	adr	x1,on_scroll
	mov	x2,0
	bl	ui_w_on
	ldp	x19,x20,[sp],16
	epilogue

; --- page 1 : champs de texte -------------------------------------------------
TB_W	= 320
build_text:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,0				; champ
.f:	cmp	x20,4
	b.hs	.out
	mov	x0,CX+140
	mov	x1,52
	mul	x1,x1,x20
	add	x1,x1,M+24
	mov	x2,TB_W
	mov	x3,30
	adr	x4,tb_flags
	ldrb	w4,[x4,x20]
	bl	ui_textbox
	mov	x19,x0
	adr	x1,w_tb
	str	x19,[x1,x20,lsl 3]
	mov	x1,1
	bl	reg
	adr	x1,tb_hints			; indication
	ldr	w1,[x1,x20,lsl 2]
	adr	x2,tb_hints
	add	x1,x2,x1
	bl	tr_x1
	mov	x0,x19
	bl	ui_textbox_hint
	add	x20,x20,1
	b	.f
.out:	adr	x0,w_tb+16			; lecture seule : un texte
	ldr	x0,[x0]
	adr	x1,s_ro
	bl	tr_x1
	bl	ui_w_set_text
	adr	x0,w_tb+24			; mono
	ldr	x0,[x0]
	adr	x1,s_mono
	bl	tr_x1
	bl	ui_w_set_text
	ldp	x19,x20,[sp],16
	epilogue

; --- page 2 : dialogues --------------------------------------------------------
NDLG	= 6
build_dialogs:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,0
.b:	cmp	x20,NDLG
	b.hs	.lab
	mov	x0,CX
	mov	x1,44
	mul	x1,x1,x20
	add	x1,x1,M+24
	mov	x2,240
	mov	x3,0
	adr	x4,dlg_labels
	ldr	w4,[x4,x20,lsl 2]
	adr	x5,dlg_labels
	add	x4,x5,x4
	mov	x5,0
	cmp	x20,0
	csinc	x5,x5,xzr,ne			; (le premier : principal)
	bl	ui_pushbutton
	adr	x1,w_dlg
	str	x0,[x1,x20,lsl 3]
	mov	x1,2
	bl	reg
	add	x20,x20,1
	b	.b
.lab:	mov	x0,CX+270			; explication (etiquette coupee en lignes)
	mov	x1,M+24
	mov	x2,CW-270
	mov	x3,200
	adr	x4,s_dlg_help
	mov	x5,ULB_WRAP or ULB_DIM
	bl	ui_label
	mov	x1,2
	bl	reg
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero du bouton : ouvre son dialogue, la reponse dans la ligne d'etat
run_dialog:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	x19,2
	b.hs	.ask
	adr	x0,s_dlg_t			; 0 : message ; 1 : Oui / Non / Annuler
	adr	x1,s_dlg_msg
	bl	tr_x1
	mov	x2,UM_OK
	cbz	x19,.m
	adr	x1,s_dlg_q
	bl	tr_x1
	mov	x2,UM_YESNOCANCEL
.m:	bl	ui_message
	b	.num
.ask:	cmp	x19,2
	b.ne	.files
	adr	x0,answer
	adr	x1,s_dlg_def
.cp:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.cp
	adr	x0,s_dlg_t
	adr	x1,s_dlg_ask
	bl	tr_x1
	adr	x2,answer
	mov	x3,200
	bl	ui_ask
	cbz	x0,.num
	adr	x0,s_dlg_r
	adr	x1,answer
	bl	status2
	b	.out
.files:	cmp	x19,3
	b.ne	.f4
	adr	x0,s_sd0
	adr	x1,answer
	mov	x2,256
	bl	ui_file_open
	b	.fres
.f4:	cmp	x19,4
	b.ne	.f5
	adr	x0,s_home
	adr	x1,s_newname
	bl	tr_x1
	adr	x2,answer
	mov	x3,256
	bl	ui_file_save
	b	.fres
.f5:	adr	x0,s_sd0
	adr	x1,answer
	mov	x2,256
	bl	ui_folder_choose
.fres:	cbz	x0,.num
	adr	x0,s_dlg_r
	adr	x1,answer
	bl	status2
	b	.out
.num:	adr	x1,numbuf			; reponse : un nombre
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,s_dlg_r
	adr	x1,numbuf
	bl	status2
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- page 3 : choix -------------------------------------------------------------
build_choice:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,CX				; cases a cocher
	mov	x1,M+24
	mov	x2,0
	mov	x3,0
	adr	x4,s_ck1
	bl	tr_x4
	mov	x5,1
	bl	ui_checkbox
	mov	x1,3
	bl	reg
	mov	x0,CX
	mov	x1,M+24+36
	mov	x2,0
	mov	x3,0
	adr	x4,s_ck2
	bl	tr_x4
	mov	x5,0
	bl	ui_checkbox
	mov	x1,3
	bl	reg
	mov	x0,CX
	mov	x1,M+24+72
	mov	x2,0
	mov	x3,0
	adr	x4,s_ck3
	bl	tr_x4
	mov	x5,1
	bl	ui_checkbox
	mov	x19,x0
	mov	x1,3
	bl	reg
	mov	x0,x19				; (grisee)
	mov	x1,0
	bl	ui_w_enable
	mov	x20,0				; radios du groupe 1
.r:	cmp	x20,3
	b.hs	.sw
	mov	x0,CX
	mov	x1,36
	mul	x1,x1,x20
	add	x1,x1,M+24+132
	mov	x2,0
	mov	x3,0
	adr	x4,radio_labels
	ldr	w4,[x4,x20,lsl 2]
	adr	x5,radio_labels
	add	x4,x5,x4
	bl	tr_x4
	mov	x5,1
	bl	ui_radio
	mov	x19,x0
	mov	x1,3
	bl	reg
	cmp	x20,1
	b.ne	.rn
	mov	x0,x19
	mov	x1,1
	bl	ui_w_set_value
.rn:	add	x20,x20,1
	b	.r
.sw:	mov	x0,CX+280			; interrupteurs
	mov	x1,M+24
	mov	x2,0
	mov	x3,0
	adr	x4,s_sw1
	mov	x5,1
	bl	ui_switch
	mov	x1,3
	bl	reg
	mov	x0,CX+280
	mov	x1,M+24+36
	mov	x2,0
	mov	x3,0
	adr	x4,s_sw2
	mov	x5,0
	bl	ui_switch
	mov	x1,3
	bl	reg
	mov	x0,CX+280			; liste deroulante
	mov	x1,M+24+132
	mov	x2,220
	mov	x3,0
	adr	x4,s_dd
	bl	ui_dropdown
	mov	x1,3
	bl	reg
	mov	x0,CX+280			; liste modifiable
	mov	x1,M+24+132+72
	mov	x2,220
	mov	x3,0
	adr	x4,s_cb
	bl	ui_combobox
	mov	x19,x0
	mov	x1,3
	bl	reg
	mov	x0,x19
	adr	x1,s_cb0
	bl	ui_w_set_text
	ldp	x19,x20,[sp],16
	epilogue

; --- page 4 : texte multiligne ----------------------------------------------------
build_area:
	prologue
	str	x19,[sp,-16]!
	mov	x0,CX
	mov	x1,M+24
	mov	x2,CW
	mov	x3,WIN_H-ST_H-M-24-M
	mov	x4,UTF_MONO or UTA_LINENUM or UTA_VSCROLL or UTA_HSCROLL or UTA_CODE
	bl	ui_textarea
	adr	x1,w_area
	str	x0,[x1]
	mov	x19,x0
	mov	x1,4
	bl	reg
	mov	x0,x19
	adr	x1,s_area
	bl	tr_x1
	bl	ui_w_set_text
	ldr	x19,[sp],16
	epilogue

; --- page 5 : valeurs ---------------------------------------------------------------
build_values:
	prologue
	str	x19,[sp,-16]!
	mov	x0,CX				; glissiere horizontale
	mov	x1,M+24
	mov	x2,300
	mov	x3,28
	mov	x4,0
	mov	x5,100
	mov	x6,40
	bl	ui_slider
	adr	x1,w_slider
	str	x0,[x1]
	mov	x1,5
	bl	reg
	mov	x0,CX				; barre de progression
	mov	x1,M+24+48
	mov	x2,300
	mov	x3,10
	mov	x4,100
	mov	x5,40
	bl	ui_progress
	adr	x1,w_prog
	str	x0,[x1]
	mov	x1,5
	bl	reg
	mov	x0,CX				; vu-metre
	mov	x1,M+24+80
	mov	x2,300
	mov	x3,14
	mov	x4,100
	bl	ui_vumeter
	adr	x1,w_vu
	str	x0,[x1]
	mov	x1,5
	bl	reg
	mov	x0,CX				; afficheur
	mov	x1,M+24+120
	mov	x2,200
	mov	x3,56
	adr	x4,s_lcd0
	bl	ui_lcd
	adr	x1,w_lcd
	str	x0,[x1]
	mov	x1,5
	bl	reg
	mov	x0,CX				; nombre +/-
	mov	x1,M+24+200
	mov	x2,160
	mov	x3,0
	mov	x4,0
	mov	x5,999
	mov	x6,42
	bl	ui_numeric
	mov	x1,5
	bl	reg
	mov	x0,CX+340			; glissiere verticale
	mov	x1,M+24
	mov	x2,28
	mov	x3,200
	mov	x4,0
	mov	x5,10
	mov	x6,7
	bl	ui_slider
	mov	x1,5
	bl	reg
	mov	x0,CX+400			; molette
	mov	x1,M+24
	mov	x2,120
	mov	x3,120
	mov	x4,0
	mov	x5,100
	mov	x6,70
	bl	ui_knob
	mov	x1,5
	bl	reg
	ldr	x19,[sp],16
	epilogue

; --- page 6 : volets (separateur) ---------------------------------------------------
build_split:
	prologue
	mov	x0,CX
	mov	x1,M+24
	mov	x2,200
	mov	x3,100
	mov	x4,UL_HEADER
	bl	ui_list
	adr	x1,w_left
	str	x0,[x1]
	mov	x1,6
	bl	reg
	adr	x0,w_left
	ldr	x0,[x0]
	adr	x1,s_c_name
	bl	tr_x1
	mov	x2,300
	mov	x3,0
	bl	ui_list_column
	adr	x1,items			; les memes elements (premiere colonne)
	str	x19,[sp,-16]!
	mov	x19,x1
.add:	ldrb	w0,[x19]
	cbz	w0,.added
	adr	x0,w_left
	ldr	x0,[x0]
	mov	x1,x19
	mov	x2,0
	bl	ui_list_add
.skip:	ldrb	w0,[x19],1
	cbnz	w0,.skip
	b	.add
.added:	ldr	x19,[sp],16
	mov	x0,CX+200			; separateur
	mov	x1,M+24
	mov	x2,8
	mov	x3,100
	mov	x4,CX+120
	mov	x5,CX+CW-160
	bl	ui_splitter
	adr	x1,w_split
	str	x0,[x1]
	mov	x1,6
	bl	reg
	mov	x0,CX+208			; volet de droite
	mov	x1,M+24
	mov	x2,100
	mov	x3,100
	mov	x4,UTA_VSCROLL
	bl	ui_textarea
	adr	x1,w_right
	str	x0,[x1]
	mov	x1,6
	bl	reg
	adr	x0,w_right
	ldr	x0,[x0]
	adr	x1,s_split
	bl	tr_x1
	bl	ui_w_set_text
	bl	layout
	epilogue

; --- page 7 : onglets, barres -------------------------------------------------------
build_tabs:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,CX				; barre d'outils
	mov	x1,M+24
	mov	x2,CW
	mov	x3,0
	bl	ui_toolbar
	adr	x1,w_tbar
	str	x0,[x1]
	mov	x1,7
	bl	reg
	mov	x20,0
.tb:	cmp	x20,NTOOLS
	b.hs	.seg
	adr	x0,w_tbar
	ldr	x0,[x0]
	adr	x4,tools			; glyphe, libelle, drapeaux (3 x 32 bits)
	mov	x5,12
	madd	x4,x20,x5,x4
	ldr	w1,[x4]
	adr	x5,tools
	cbz	w1,.g0
	add	x1,x5,x1
.g0:	ldr	w2,[x4,4]
	cbz	w2,.l0
	add	x2,x5,x2
	bl	tr_x2
.l0:	ldr	w3,[x4,8]
	bl	ui_toolbar_add
	adr	x1,w_tools
	str	x0,[x1,x20,lsl 3]
	add	x20,x20,1
	b	.tb
.seg:	mov	x0,CX				; controle segmente
	mov	x1,M+24+66
	mov	x2,0
	mov	x3,0
	adr	x4,s_seg
	bl	tr_x4
	bl	ui_segmented
	adr	x1,w_seg
	str	x0,[x1]
	mov	x1,7
	bl	reg
	mov	x0,CX				; onglets a pages
	mov	x1,M+24+112
	mov	x2,440
	mov	x3,160
	adr	x4,s_tabs
	bl	tr_x4
	bl	ui_tabs
	mov	x19,x0
	adr	x1,w_tabs
	str	x0,[x1]
	mov	x1,7
	bl	reg
	mov	x0,CX+16			; page 1 : une etiquette, une case
	mov	x1,M+24+112+56
	mov	x2,400
	mov	x3,24
	adr	x4,s_tp1
	mov	x5,0
	bl	ui_label
	mov	x20,x0
	mov	x0,x19
	mov	x1,0
	bl	ui_tabs_page
	mov	x1,x0
	mov	x0,x20
	bl	ui_w_set_parent
	mov	x0,CX+16
	mov	x1,M+24+112+92
	mov	x2,0
	mov	x3,0
	adr	x4,s_tp1b
	bl	tr_x4
	mov	x5,1
	bl	ui_checkbox
	mov	x20,x0
	mov	x0,x19
	mov	x1,0
	bl	ui_tabs_page
	mov	x1,x0
	mov	x0,x20
	bl	ui_w_set_parent
	mov	x0,CX+16			; page 2 : une glissiere
	mov	x1,M+24+112+60
	mov	x2,300
	mov	x3,28
	mov	x4,0
	mov	x5,100
	mov	x6,25
	bl	ui_slider
	mov	x20,x0
	mov	x0,x19
	mov	x1,1
	bl	ui_tabs_page
	mov	x1,x0
	mov	x0,x20
	bl	ui_w_set_parent
	mov	x0,CX+16			; page 3 : un champ
	mov	x1,M+24+112+60
	mov	x2,300
	mov	x3,30
	mov	x4,0
	bl	ui_textbox
	mov	x20,x0
	adr	x1,s_tp3
	bl	tr_x1
	bl	ui_textbox_hint
	mov	x0,x19
	mov	x1,2
	bl	ui_tabs_page
	mov	x1,x0
	mov	x0,x20
	bl	ui_w_set_parent
	mov	x0,CX				; barre d'onglets de documents
	mov	x1,M+24+296
	mov	x2,CW
	mov	x3,34
	mov	x4,UTS_CLOSE or UTS_PLUS
	bl	ui_tabstrip
	adr	x1,w_strip
	str	x0,[x1]
	mov	x1,7
	bl	reg
	mov	x20,1
.doc:	cmp	x20,3
	b.hi	.out
	mov	x0,x20
	bl	new_doc
	add	x20,x20,1
	b	.doc
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero : un onglet « Document N » de plus
new_doc:
	prologue
	str	x0,[sp,-16]!
	adr	x0,numbuf
	adr	x1,s_doc
	bl	tr_x1
.c:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.c
	sub	x1,x0,1
	ldr	x0,[sp],16
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,w_strip
	ldr	x0,[x0]
	adr	x1,numbuf
	mov	x2,0
	bl	ui_tabstrip_add
	adr	x0,w_strip
	ldr	x0,[x0]
	bl	ui_w_draw
	epilogue

; --- page 8 : interaction -----------------------------------------------------------
build_misc:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,CX				; liste dont on glisse les elements
	mov	x1,M+24
	mov	x2,200
	mov	x3,200
	mov	x4,UL_DRAG
	bl	ui_list
	mov	x19,x0
	mov	x1,8
	bl	reg
	adr	x20,s_fruits
.f:	ldrb	w0,[x20]
	cbz	w0,.fd
	mov	x0,x19
	mov	x1,x20
	bl	tr_x1
	mov	x2,0
	bl	ui_list_add
.fs:	ldrb	w0,[x20],1
	cbnz	w0,.fs
	b	.f
.fd:	mov	x0,x19
	adr	x1,s_tip_list
	bl	tr_x1
	bl	ui_w_tip
	mov	x0,CX+220			; un champ ou deposer
	mov	x1,M+24+20
	mov	x2,240
	mov	x3,30
	mov	x4,0
	bl	ui_textbox
	mov	x19,x0
	mov	x1,8
	bl	reg
	mov	x0,x19
	adr	x1,s_drop_hint
	bl	tr_x1
	bl	ui_textbox_hint
	mov	x0,x19
	adr	x1,s_tip_tb
	bl	tr_x1
	bl	ui_w_tip
	mov	x0,CX+220			; une zone de depot (UN_DROP) : un panneau
	mov	x1,M+24+70
	mov	x2,240
	mov	x3,60
	mov	x4,UPN_SURFACE
	bl	ui_panel
	mov	x19,x0
	adr	x1,w_drop
	str	x0,[x1]
	mov	x1,8
	bl	reg
	mov	x0,x19
	mov	x1,UC_CROSS
	bl	ui_w_cursor
	mov	x0,x19
	adr	x1,s_tip_zone
	bl	tr_x1
	bl	ui_w_tip
	mov	x0,CX+220			; un bouton « main » avec infobulle
	mov	x1,M+24+150
	mov	x2,0
	mov	x3,0
	adr	x4,s_hand
	mov	x5,0
	bl	ui_pushbutton
	mov	x19,x0
	mov	x1,8
	bl	reg
	mov	x0,x19
	mov	x1,UC_HAND
	bl	ui_w_cursor
	mov	x0,x19
	adr	x1,s_tip_hand
	bl	tr_x1
	bl	ui_w_tip
	mov	x0,CX				; langue (TR), italique
	mov	x1,M+24+220
	mov	x2,CW
	mov	x3,24
	adr	x4,s_none
	mov	x5,0
	bl	ui_label
	adr	x1,w_lang
	str	x0,[x1]
	mov	x1,8
	bl	reg
	bl	ui_lang
	mov	x19,x0
	adr	x0,numbuf
	adr	x1,s_langis
	bl	tr_x1
.c1:	ldrb	w2,[x1],1
	cbz	w2,.c2
	strb	w2,[x0],1
	b	.c1
.c2:	mov	x1,x19
.c3:	ldrb	w2,[x1],1
	cbz	w2,.c4
	strb	w2,[x0],1
	b	.c3
.c4:	adr	x1,s_trdemo
	bl	tr_x1
.c5:	ldrb	w2,[x1],1
	cbz	w2,.c6
	strb	w2,[x0],1
	b	.c5
.c6:	str	x0,[sp,-16]!
	adr	x0,s_cancel
	bl	ui_tr
	mov	x1,x0
	ldr	x0,[sp],16
.c7:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.c7
	adr	x0,w_lang
	ldr	x0,[x0]
	adr	x1,numbuf
	bl	ui_w_set_text
	ldp	x19,x20,[sp],16
	epilogue

; --- page 9 : dates, couleurs, arbre ------------------------------------------------
TODAY	= (2026 shl 16) or (10 shl 8) or 29
build_dates:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,CX				; calendrier
	mov	x1,M+24
	mov	x2,0
	mov	x3,0
	movl	x4,TODAY
	bl	ui_calendar
	mov	x19,x0
	movl	x1,TODAY
	bl	ui_calendar_today
	mov	x0,x19
	mov	x1,9
	bl	reg
	mov	x0,CX				; selecteur de date
	mov	x1,M+24+262
	mov	x2,0
	mov	x3,0
	movl	x4,(2026 shl 16) or (12 shl 8) or 24
	bl	ui_datepicker
	mov	x19,x0
	movl	x1,TODAY
	bl	ui_datepicker_today
	mov	x0,x19
	mov	x1,9
	bl	reg
	mov	x0,CX+160			; selecteur de couleur
	mov	x1,M+24+262
	mov	x2,0
	mov	x3,0
	movl	x4,0x2F74E0
	bl	ui_colorpicker
	mov	x1,9
	bl	reg
	mov	x0,CX+216			; dialogue de couleur
	mov	x1,M+24+262
	mov	x2,0
	mov	x3,0
	adr	x4,s_colordlg
	bl	tr_x4
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_cdlg
	str	x0,[x1]
	mov	x1,9
	bl	reg
	mov	x0,CX+270			; arborescence
	mov	x1,M+24
	mov	x2,CW-270
	mov	x3,250
	bl	ui_tree
	adr	x1,w_tree
	str	x0,[x1]
	mov	x19,x0
	mov	x1,9
	bl	reg
	mov	x0,x19				; Systeme > Kits > uikit, appkit...
	mov	x1,-1
	adr	x2,tn_sys
	bl	tr_x2
	mov	x3,0
	bl	ui_tree_add
	mov	x20,x0
	mov	x0,x19
	mov	x1,x20
	adr	x2,tn_kits
	mov	x3,0
	bl	ui_tree_add
	str	x0,[sp,-16]!
	mov	x0,x19
	ldr	x1,[sp]
	adr	x2,tn_uikit
	mov	x3,0
	bl	ui_tree_add
	mov	x0,x19
	ldr	x1,[sp]
	adr	x2,tn_appkit
	mov	x3,0
	bl	ui_tree_add
	mov	x0,x19
	ldr	x1,[sp],16
	adr	x2,tn_netkit
	mov	x3,0
	bl	ui_tree_add
	mov	x0,x19
	mov	x1,x20
	adr	x2,tn_elegant
	mov	x3,0
	bl	ui_tree_add
	mov	x0,x19
	mov	x1,x20
	mov	x2,1
	bl	ui_tree_expand
	mov	x0,x19
	mov	x1,-1
	adr	x2,tn_apps
	bl	tr_x2
	mov	x3,0
	bl	ui_tree_add
	mov	x20,x0
	mov	x0,x19
	mov	x1,x20
	adr	x2,tn_editor
	mov	x3,0
	bl	ui_tree_add
	mov	x0,x19
	mov	x1,x20
	adr	x2,tn_viewer
	mov	x3,0
	bl	ui_tree_add
	mov	x0,x19
	mov	x1,-1
	adr	x2,tn_docs
	bl	tr_x2
	mov	x3,0
	bl	ui_tree_add
	ldp	x19,x20,[sp],16
	epilogue

; --- page 10 : grille de donnees, panneau lateral, canevas, mise en page -----------------
VROWS	= 1000
build_grid:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,CX				; grille virtuelle : 1000 lignes
	mov	x1,M+24
	mov	x2,300
	mov	x3,200
	mov	x4,UL_HEADER or UL_STRIPES or UL_GRID
	bl	ui_list
	mov	x19,x0
	adr	x1,w_grid
	str	x0,[x1]
	mov	x1,10
	bl	reg
	mov	x0,x19
	adr	x1,s_g1
	bl	tr_x1
	mov	x2,110
	mov	x3,0
	bl	ui_list_column
	mov	x0,x19
	adr	x1,s_g2
	bl	tr_x1
	mov	x2,110
	mov	x3,ULC_RIGHT
	bl	ui_list_column
	mov	x0,x19
	adr	x1,s_g3
	mov	x2,80
	mov	x3,ULC_RIGHT
	bl	ui_list_column
	mov	x0,x19
	mov	x1,VROWS
	adr	x2,grid_row
	bl	ui_list_virtual
	mov	x0,CX+316			; panneau lateral
	mov	x1,M+24
	mov	x2,150
	mov	x3,200
	mov	x4,UL_SIDEBAR
	bl	ui_list
	mov	x19,x0
	mov	x1,10
	bl	reg
	adr	x20,s_side
.sd:	ldrb	w0,[x20]
	cbz	w0,.sdd
	mov	x0,x19
	mov	x1,x20
	bl	tr_x1
	mov	x2,0
	bl	ui_list_add
.ss:	ldrb	w0,[x20],1
	cbnz	w0,.ss
	b	.sd
.sdd:	mov	x0,CX				; canevas
	mov	x1,M+24+216
	mov	x2,220
	mov	x3,120
	bl	ui_canvas
	adr	x1,w_canvas
	str	x0,[x1]
	mov	x1,10
	bl	reg
	bl	canvas_paint
	mov	x0,CX+236			; panneau et sa grille de boutons
	mov	x1,M+24+216
	mov	x2,230
	mov	x3,120
	mov	x4,UPN_SURFACE
	bl	ui_panel
	mov	x19,x0
	mov	x1,10
	bl	reg
	mov	x20,0
.b:	cmp	x20,4
	b.hs	.lay
	mov	x0,0
	mov	x1,0
	mov	x2,0
	mov	x3,0
	adr	x4,lay_labels
	ldr	w5,[x4,x20,lsl 2]
	add	x4,x4,x5
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x19
	bl	ui_w_set_parent
	add	x20,x20,1
	b	.b
.lay:	mov	x0,x19				; grille de 2 colonnes, ecart 10
	mov	x1,ULY_GRID
	mov	x2,10
	mov	x3,2
	bl	ui_layout
	ldp	x19,x20,[sp],16
	epilogue

; fonction de la grille virtuelle : x0 = liste, x1 = ligne -> x0 = texte
grid_row:
	prologue
	str	x19,[sp,-16]!
	adr	x2,vdesc			; ordre : croissant / decroissant
	ldr	x2,[x2]
	mov	x19,x1
	cbz	x2,.a
	mov	x19,VROWS-1
	sub	x19,x19,x1
.a:	adr	x0,vbuf
	adr	x1,s_line0
	bl	tr_x1
.c:	ldrb	w2,[x1],1
	cbz	w2,.n
	strb	w2,[x0],1
	b	.c
.n:	mov	x1,x0
	add	x0,x19,1
	bl	fmt_dec
	mov	w1,9
	strb	w1,[x0],1
	mov	x1,x0
	add	x0,x19,1
	mul	x0,x0,x0
	bl	fmt_dec
	mov	w1,9
	strb	w1,[x0],1
	mov	x1,x0
	mov	x2,7
	add	x0,x19,1
	udiv	x3,x0,x2
	msub	x0,x3,x2,x0
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,vbuf
	ldr	x19,[sp],16
	epilogue

; dessine le canevas : formes anticrenelees, texte
canvas_paint:
	prologue
	adr	x0,w_canvas
	ldr	x0,[x0]
	bl	ui_canvas_begin
	mov	x0,0
	mov	x1,0
	mov	x2,220
	mov	x3,120
	movl	w4,0x00FFFFFF
	bl	ui_fill
	mov	x0,50*16			; disque
	mov	x1,55*16
	mov	x2,30*16
	movl	w3,0x002F74E0
	bl	ui_vp_disc
	mov	x0,100*16			; traits
	mov	x1,20*16
	mov	x2,200*16
	mov	x3,100*16
	mov	x4,6*16
	movl	w5,0x00E5484D
	bl	ui_vp_line
	mov	x0,100*16
	mov	x1,100*16
	mov	x2,200*16
	mov	x3,30*16
	mov	x4,24
	movl	w5,0x0030A46C
	bl	ui_vp_line
	adr	x0,star				; etoile
	mov	x1,10
	movl	w2,0x00F1B33B
	bl	ui_vp_poly
	adr	x0,s_canvas
	bl	tr_x0
	mov	x1,8
	mov	x2,112
	movl	w3,0x00333333
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	bl	ui_canvas_end
	epilogue

; souris sur le canevas (UN_CHANGE) : un point tant que le bouton est tenu
canvas_mouse:
	prologue
	adr	x0,ui_ev
	ldr	w1,[x0,EV_CODE]			; boutons
	tbz	w1,0,.out
	ldrsw	x1,[x0,EV_X]
	ldrsw	x2,[x0,EV_Y]
	adr	x3,w_canvas
	ldr	x3,[x3]
	stp	x1,x2,[sp,-16]!
	mov	x0,x3
	bl	ui_canvas_begin
	ldp	x1,x2,[sp],16
	sub	x0,x1,CX			; (coordonnees du canevas)
	sub	x1,x2,M+24+216
	lsl	x0,x0,4
	lsl	x1,x1,4
	mov	x2,3*16
	movl	w3,0x006040C0
	bl	ui_vp_disc
	bl	ui_canvas_end
	adr	x0,w_canvas
	ldr	x0,[x0]
	bl	ui_w_draw
.out:	epilogue

; place les widgets qui suivent la taille de la fenetre (cur_w, cur_h)
layout:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x9,cur_w
	ldp	x19,x20,[x9]
	adr	x0,w_nav			; liste des pages : toute la hauteur
	ldr	x0,[x0]
	mov	x1,M
	mov	x2,M
	mov	x3,NAV_W
	sub	x4,x20,ST_H+2*M
	bl	ui_w_move
	adr	x0,w_area			; texte multiligne : tout le contenu
	ldr	x0,[x0]
	mov	x1,CX
	mov	x2,M+24
	sub	x3,x19,CX+M
	sub	x4,x20,ST_H+M+24+M
	bl	ui_w_move
	adr	x0,w_split			; volets : gauche | separateur | droite
	ldr	x0,[x0]
	mov	x1,CX+120			; (bornes du separateur)
	sub	x2,x19,M+160
	mov	x3,0
	mov	x4,0
	bl	ui_w_set_range
	adr	x0,w_split
	ldr	x0,[x0]
	bl	ui_w_get_value
	mov	x1,x0
	str	x1,[sp,-16]!
	adr	x0,w_split
	ldr	x0,[x0]
	mov	x2,M+24
	mov	x3,8
	sub	x4,x20,ST_H+M+24+M
	bl	ui_w_move
	adr	x0,w_left
	ldr	x0,[x0]
	mov	x1,CX
	mov	x2,M+24
	ldr	x3,[sp]
	sub	x3,x3,CX
	sub	x4,x20,ST_H+M+24+M
	bl	ui_w_move
	adr	x0,w_right
	ldr	x0,[x0]
	ldr	x1,[sp],16
	add	x1,x1,8
	mov	x2,M+24
	sub	x3,x19,M
	sub	x3,x3,x1
	sub	x4,x20,ST_H+M+24+M
	bl	ui_w_move
	ldp	x19,x20,[sp],16
	epilogue

; fonction de ui_on_resize : x0 = largeur, x1 = hauteur
on_resize:
	prologue
	adr	x9,cur_w
	stp	x0,x1,[x9]
	bl	layout
	bl	paint
	epilogue

; x0 = page : la montre (les widgets des autres caches)
show_page:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x1,page
	str	x0,[x1]
	mov	x21,x0
	mov	x0,CX-4				; fond du contenu
	mov	x1,0
	adr	x9,cur_w
	ldp	x2,x3,[x9]
	sub	x2,x2,CX-4
	sub	x3,x3,ST_H
	bl	face
	bl	ui_fill
	adr	x19,wtab
	adr	x20,nw
	ldr	x20,[x20]
.w:	cbz	x20,.titles
	ldp	x0,x1,[x19],16
	cmp	x1,x21
	cset	x1,eq
	bl	ui_w_show
	sub	x20,x20,1
	b	.w
.titles:
	cbnz	x21,.p1
	adr	x0,s_h_list
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	adr	x0,s_h_sb
	mov	x1,CX+440+24-60			; (a gauche des boutons de la fenetre)
	mov	x2,M+12
	bl	heading
	b	.done
.p1:	cmp	x21,1
	b.ne	.p2
	adr	x0,s_h_text
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	mov	x22,0				; libelles des champs
.lb:	cmp	x22,4
	b.hs	.done
	adr	x0,tb_labels
	ldr	w0,[x0,x22,lsl 2]
	adr	x1,tb_labels
	add	x0,x1,x0
	mov	x1,CX
	mov	x2,52
	mul	x2,x2,x22
	add	x2,x2,M+24+20
	bl	draw_label
	add	x22,x22,1
	b	.lb
.p2:	cmp	x21,2
	b.ne	.p3
	adr	x0,s_h_dlg
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	b	.done
.p3:	cmp	x21,3
	b.ne	.p4
	adr	x0,s_h_choice
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	adr	x0,s_h_dd
	mov	x1,CX+280
	mov	x2,M+24+124
	bl	heading
	adr	x0,s_h_cb
	mov	x1,CX+280
	mov	x2,M+24+196
	bl	heading
	b	.done
.p4:	cmp	x21,4
	b.ne	.p5
	adr	x0,s_h_area
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	b	.done
.p5:	cmp	x21,5
	b.ne	.p6
	adr	x0,s_h_val
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	b	.done
.p6:	cmp	x21,6
	b.ne	.p7
	adr	x0,s_h_split
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	b	.done
.p7:	cmp	x21,7
	b.ne	.p8
	adr	x0,s_h_tabs
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	b	.done
.p8:	cmp	x21,8
	b.ne	.p9
	adr	x0,s_h_misc
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	mov	x22,0				; une ligne par police (ui_font)
.fl:	cmp	x22,7
	b.hs	.done
	add	x0,x22,3
	bl	ui_font_get
	mov	x5,x0
	adr	x0,font_names
	ldr	w0,[x0,x22,lsl 2]
	adr	x1,font_names
	add	x0,x1,x0
	bl	tr_x0
	mov	x1,CX
	mov	x2,22
	mul	x2,x2,x22
	add	x2,x2,M+24+280
	adr	x3,jetons
	ldr	x3,[x3]
	ldr	w3,[x3,TJ_TEXTE1]
	mov	x4,15
	mov	x6,0
	bl	ui_text
	add	x22,x22,1
	b	.fl
	b	.done
.p9:	cmp	x21,9
	b.ne	.p10
	adr	x0,s_h_dates
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	b	.done
.p10:	cmp	x21,10
	b.ne	.done
	adr	x0,s_h_grid
	mov	x1,CX
	mov	x2,M+12
	bl	heading
	adr	x0,s_pad			; indications de manette
	bl	tr_x0
	mov	x1,CX
	mov	x2,M+24+350
	mov	x3,24
	mov	x4,0
	bl	ui_pad_hints
.done:	mov	x0,CX-4
	mov	x1,0
	adr	x9,cur_w
	ldp	x2,x3,[x9]
	sub	x2,x2,CX-4
	sub	x3,x3,ST_H
	bl	ui_update
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; fond, liste des pages, page 0
paint:
	prologue
	mov	x0,0
	mov	x1,0
	adr	x9,cur_w
	ldp	x2,x3,[x9]
	bl	face
	bl	ui_fill
	adr	x0,w_nav
	ldr	x0,[x0]
	bl	ui_w_draw
	adr	x0,page
	ldr	x0,[x0]
	bl	show_page
	bl	ui_update_all
	adr	x0,s_hello
	bl	status
	epilogue

; -> w4 = couleur du fond (TJ_FACE)
face:
	adr	x4,jetons
	ldr	x4,[x4]
	ldr	w4,[x4,TJ_FACE]
	ret

; x0 = texte, x1 = x, x2 = ligne de base : titre de section
heading:
	prologue
	bl	tr_x0
	adr	x3,jetons
	ldr	x3,[x3]
	ldr	w3,[x3,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	epilogue

; x0 = texte, x1 = x, x2 = ligne de base : libelle
draw_label:
	prologue
	bl	tr_x0
	adr	x3,jetons
	ldr	x3,[x3]
	ldr	w3,[x3,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	epilogue

; x0 = texte : ligne d'etat en bas de la fenetre
status:
	prologue
	str	x19,[sp,-16]!
	bl	tr_x0
	mov	x19,x0
	mov	x0,0
	adr	x9,cur_w
	ldp	x2,x1,[x9]
	sub	x1,x1,ST_H
	mov	x3,ST_H
	bl	face
	bl	ui_fill
	mov	x0,0
	adr	x9,cur_w
	ldp	x2,x1,[x9]
	sub	x1,x1,ST_H
	mov	x3,1
	adr	x4,jetons
	ldr	x4,[x4]
	ldr	w4,[x4,TJ_LIGNE]
	bl	ui_fill
	mov	x0,x19
	mov	x1,M
	adr	x9,cur_h
	ldr	x2,[x9]
	sub	x2,x2,12
	adr	x3,jetons
	ldr	x3,[x3]
	ldr	w3,[x3,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	adr	x9,cur_w
	ldr	x6,[x9]
	sub	x6,x6,2*M
	bl	ui_text_fit
	mov	x0,0
	adr	x9,cur_w
	ldp	x2,x1,[x9]
	sub	x1,x1,ST_H
	mov	x3,ST_H
	bl	ui_update
	ldr	x19,[sp],16
	epilogue

; x0 = prefixe, x1 = texte : « prefixe texte » (tabulations -> « | ») dans
; la ligne d'etat
status2:
	prologue
	bl	tr_x0
	adr	x2,line
	mov	x3,0
.c:	ldrb	w4,[x0,x3]
	cbz	w4,.p
	strb	w4,[x2,x3]
	add	x3,x3,1
	b	.c
.p:	add	x2,x2,x3
	mov	x3,0
	cbz	x1,.e
.d:	ldrb	w4,[x1,x3]
	cmp	w4,9
	b.ne	.st
	mov	w4,'|'
.st:	strb	w4,[x2,x3]
	cbz	w4,.e
	add	x3,x3,1
	cmp	x3,200
	b.lo	.d
.e:	strb	wzr,[x2,x3]
	adr	x0,line
	bl	status
	epilogue

; x0 = widget, x1 = code (rendus par ui_w_event) : l'evenement dans la
; ligne d'etat
report:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,w_nav			; liste des pages
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.list
	cmp	x20,UN_CHANGE
	b.ne	.out
	mov	x0,x19
	bl	ui_w_get_value
	bl	show_page
	b	.out
.list:	adr	x0,w_list
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.text
	cmp	x20,UN_SORT
	b.ne	.li
	adr	x0,s_sorted
	bl	status
	b	.out
.li:	mov	x0,x19
	bl	ui_w_get_value
	mov	x1,x0
	mov	x0,x19
	bl	ui_list_get
	mov	x1,x0
	adr	x0,s_sel
	cmp	x20,UN_ACTIVATE
	b.ne	.li2
	adr	x0,s_act
.li2:	bl	status2
	b	.out
.text:	adr	x0,w_split			; separateur : volets replaces
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.text2
	bl	layout
	adr	x0,w_left
	ldr	x0,[x0]
	bl	ui_w_draw
	adr	x0,w_right
	ldr	x0,[x0]
	bl	ui_w_draw
	adr	x0,w_split
	ldr	x0,[x0]
	bl	ui_w_draw
	b	.out
.text2:	adr	x0,w_canvas			; canevas
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.text2g
	bl	canvas_mouse
	b	.out
.text2g:
	adr	x0,w_grid			; grille : trier, menu contextuel
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.text2c
	cmp	x20,UN_SORT
	b.ne	.gctx
	mov	x0,x19
	bl	ui_list_sorted
	adr	x2,vdesc
	str	x1,[x2]
	mov	x0,x19
	bl	ui_w_draw
	b	.out
.gctx:	cmp	x20,UN_CONTEXT
	b.ne	.gsel
	bl	uk_ctx_menu
	b	.out
.gsel:	mov	x0,x19
	bl	ui_w_get_value
	mov	x1,x0
	mov	x0,x19
	bl	ui_list_get
	mov	x1,x0
	adr	x0,s_sel
	bl	status2
	b	.out
.text2c:
	adr	x0,w_cdlg			; dialogue de couleur
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.text2t
	adr	x0,s_colordlg
	bl	tr_x0
	movl	x1,0x30A46C
	bl	ui_color_dialog
	adr	x1,numbuf
	tbnz	x0,63,.cdn
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,s_val
	adr	x1,numbuf
	bl	status2
	b	.out
.cdn:	adr	x0,s_cancelled
	bl	status
	b	.out
.text2t:
	adr	x0,w_tree			; arbre : le libelle du noeud
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.text2a
	mov	x0,x19
	bl	ui_w_get_value
	mov	x1,x0
	mov	x0,x19
	bl	ui_tree_get
	mov	x1,x0
	adr	x0,s_node
	cmp	x20,UN_ACTIVATE
	b.ne	.tn
	adr	x0,s_act
.tn:	bl	status2
	b	.out
.text2a:
	cmp	x20,UN_DROP			; un depot (sur la zone) : son texte
	b.ne	.text2b
	adr	x0,dropbuf
	mov	x1,256
	bl	ui_drop_get
	adr	x0,s_dropped
	adr	x1,dropbuf
	bl	status2
	b	.out
.text2b:
	adr	x0,w_strip			; barre d'onglets : choisir, fermer, +
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.text3
	cmp	x20,UN_NEW
	b.ne	.tsc
	adr	x0,ndocs
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	mov	x0,x1
	bl	new_doc
	b	.tsv
.tsc:	cmp	x20,UN_CLOSE
	b.ne	.tsv
	mov	x0,x19
	bl	ui_tabstrip_closing
	mov	x1,x0
	mov	x0,x19
	bl	ui_list_remove
.tsv:	mov	x0,x19
	bl	ui_w_get_value
	mov	x1,x0
	mov	x0,x19
	bl	ui_list_get
	mov	x1,x0
	adr	x0,s_tabsel
	bl	status2
	b	.out
.text3:	adr	x0,page				; page des champs
	ldr	x0,[x0]
	cmp	x0,7
	b.ne	.text4
	mov	x0,x19
	bl	ui_w_get_text
	mov	x1,x0
	adr	x0,s_tool
	bl	status2
	b	.out
.text4:	adr	x0,page
	ldr	x0,[x0]
	cmp	x0,5
	b.eq	.vals
	cmp	x0,4
	b.eq	.area
	cmp	x0,3
	b.eq	.choice
	cmp	x0,2
	b.eq	.dlg
	cmp	x0,1
	b.ne	.out
	mov	x0,x19
	bl	ui_w_get_text
	mov	x1,x0
	adr	x0,s_changed
	cmp	x20,UN_ACTIVATE
	b.ne	.t2
	adr	x0,s_entered
.t2:	bl	status2
	b	.out
.dlg:	cmp	x20,UN_ACTIVATE			; un bouton de la page des dialogues
	b.ne	.out
	mov	x0,0
.dn:	cmp	x0,NDLG
	b.hs	.out
	adr	x1,w_dlg
	ldr	x1,[x1,x0,lsl 3]
	cmp	x1,x19
	b.eq	.drun
	add	x0,x0,1
	b	.dn
.drun:	bl	run_dialog
	b	.out
.vals:	adr	x0,w_slider			; la glissiere mene les autres
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.choice
	mov	x0,x19
	bl	ui_w_get_value
	mov	x20,x0
	adr	x0,w_prog
	ldr	x0,[x0]
	mov	x1,x20
	bl	ui_w_set_value
	adr	x0,w_vu
	ldr	x0,[x0]
	mov	x1,x20
	bl	ui_vumeter_level
	mov	x0,x20
	adr	x1,numbuf
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,w_lcd
	ldr	x0,[x0]
	adr	x1,numbuf
	bl	ui_w_set_text
	b	.choice
.area:	mov	x0,x19				; ligne et colonne du curseur
	bl	ui_textarea_caret
	add	x20,x1,1
	add	x0,x0,1
	adr	x1,numbuf
	bl	fmt_dec
	adr	x1,s_col
	bl	tr_x1
.ac:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.ac
	sub	x1,x0,1
	mov	x0,x20
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,s_line
	adr	x1,numbuf
	bl	status2
	b	.out
.choice:					; valeur et texte du widget
	mov	x0,x19
	bl	ui_w_get_value
	adr	x1,numbuf
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	x20,x0
	mov	x0,x19
	bl	ui_w_get_text
.cc:	ldrb	w1,[x0],1
	strb	w1,[x20],1
	cbnz	w1,.cc
	adr	x0,s_val
	adr	x1,numbuf
	bl	status2
.out:	ldp	x19,x20,[sp],16
	epilogue

; fonction de la barre (ui_w_on) : x0 = widget, x1 = code
on_scroll:
	prologue
	bl	ui_w_get_value
	adr	x1,line
	adr	x2,s_pos
	bl	tr_x2
.c:	ldrb	w3,[x2],1
	strb	w3,[x1],1
	cbnz	w3,.c
	sub	x1,x1,1
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,line
	bl	status
	epilogue

include '../../inc/tr.inc'

segment readable writeable
kit_data

s_title	db 'UIKit : d',0xC3,0xA9,'monstration',0
pages	db 'Liste',0
	db 'Texte',0
	db 'Dialogues',0
	db 'Choix',0
	db 'Multiligne',0
	db 'Valeurs',0
	db 'Volets',0
	db 'Onglets',0
	db 'Interaction',0
	db 'Dates, arbre',0
	db 'Grille, canevas',0
	db 0
s_h_grid db 'GRILLE VIRTUELLE, PANNEAU LAT',0xC3,0x89,'RAL, CANEVAS, MISE EN PAGE',0
s_g1	db 'Ligne',0
s_g2	db 'Carr',0xC3,0xA9,0
s_g3	db 'Mod 7',0
s_line0	db 'Ligne ',0
s_side	db '#Acc',0xC3,0xA8,'s rapides',0,'Documents',0,'Images',0,'Musique',0
	db '#Volumes',0,'SD0:',0,'RAM0:',0,'IMG0:',0,0
s_canvas db 'Canevas : cliquez pour peindre',0
lay_labels dw ll0-lay_labels, ll1-lay_labels, ll2-lay_labels, ll3-lay_labels
ll0	db 'Un',0
ll1	db 'Deux',0
ll2	db 'Trois',0
ll3	db 'Quatre',0
s_pad	db 'A',9,'choisir',10,'B',9,'retour',10,'+',9,'d',0xC3,0xA9,'placer',10,'Start',9,'menu',0
s_ctx	db 'Copier',0,'Exporter',0,'-',0,'Propri',0xC3,0xA9,'t',0xC3,0xA9,'s',0,0
s_ctxr	db 'Menu contextuel : ',0
	align 4
star	dw 110*16,10*16, 118*16,32*16, 140*16,32*16, 122*16,46*16, 130*16,68*16
	dw 110*16,54*16, 90*16,68*16, 98*16,46*16, 80*16,32*16, 102*16,32*16
s_h_dates db 'CALENDRIER, DATE, COULEURS, ARBORESCENCE',0
s_colordlg db 'Couleur',0xE2,0x80,0xA6,0
s_cancelled db 'Annul',0xC3,0xA9,'.',0
s_node	db 'Noeud : ',0
tn_sys	db 'Syst',0xC3,0xA8,'me',0
tn_kits	db 'Kits',0
tn_uikit db 'uikit.so',0
tn_appkit db 'appkit.so',0
tn_netkit db 'netkit.so',0
tn_elegant db 'elegant',0
tn_apps	db 'Applications',0
tn_editor db 'editor',0
tn_viewer db 'viewer',0
tn_docs	db 'Documents',0
s_h_misc db 'GLISSER-D',0xC3,0x89,'POSER, INFOBULLES, CURSEURS, TR(), MENUS',0
s_fruits db 'Pomme',0,'Poire',0,'Cerise',0,'Abricot',0,'Prune',0,'Figue',0,0
s_tip_list db 'Glissez un fruit vers le champ ou la zone',0
s_drop_hint db 'D',0xC3,0xA9,'posez ici',0
s_tip_tb db 'Un texte d',0xC3,0xA9,'pos',0xC3,0xA9,' s',0x27,'ins',0xC3,0xA8,'re au point',0
s_tip_zone db 'Zone de d',0xC3,0xA9,'p',0xC3,0xB4,'t : curseur en croix',0
s_hand	db 'Bouton main',0
s_tip_hand db 'Le curseur devient une main',0
s_none	db 0
s_langis db 'Langue : ',0
s_trdemo db '  -  TR(',0x22,'Annuler',0x22,') = ',0
s_cancel db 'Annuler',0
font_names dw fn0-font_names, fn1-font_names, fn2-font_names, fn3-font_names, fn4-font_names, fn5-font_names, fn6-font_names
fn0	db 'Italique (DejaVu Sans Oblique)',0
fn1	db 'Gras italique',0
fn2	db 'Mono gras : mov x0, #42',0
fn3	db 'Serif : Portez ce vieux whisky au juge blond',0
fn4	db 'Serif gras',0
fn5	db 'Serif italique',0
fn6	db 'Serif gras italique',0
s_dropped db 'D',0xC3,0xA9,'pos',0xC3,0xA9,' : ',0
s_grid_on db 'Grille : cochee (Options > Grille, ou Ctrl+G)',0
s_grid_off db 'Grille : d',0xC3,0xA9,'coch',0xC3,0xA9,'e',0
menus	db 'MFichier',10,'I1',9,'Fermer',9,'Ctrl+W',10
	db 'MOptions',10,'I2',9,'Grille',9,'Ctrl+G',10,'I3',9,'Exporter',9,'Ctrl+E',10,0
s_h_tabs db 'BARRE D',0x27,'OUTILS, SEGMENTS, ONGLETS',0
s_seg	db 'Jour|Semaine|Mois|Ann',0xC3,0xA9,'e',0
s_tabs	db 'G',0xC3,0xA9,'n',0xC3,0xA9,'ral|Son|Compte',0
s_tp1	db 'Chaque onglet a sa page (ui_tabs_page) : ses widgets.',0
s_tp1b	db 'Ouvrir au d',0xC3,0xA9,'marrage',0
s_tp3	db 'Nom du compte',0
s_doc	db 'Document ',0
s_tabsel db 'Onglet : ',0
s_tool	db 'Outil : ',0
NTOOLS	= 9
tools	dw tg0-tools, 0, 0
	dw tg1-tools, 0, 0
	dw tg2-tools, 0, 0
	dw tg3-tools, 0, 0
	dw tg4-tools, tl4-tools, UTB_SEP
	dw tg5-tools, tl5-tools, 0
	dw 0, tl6-tools, UTB_SEP or UTB_TOGGLE
	dw tg7-tools, 0, UTB_SEP
	dw tg8-tools, tl8-tools, 0
tg0	db 0xE2,0x86,0x90,0			; « ← »
tg1	db 0xE2,0x86,0x92,0			; « → »
tg2	db 0xE2,0x86,0xBB,0			; « ↻ »
tg3	db 0xE2,0x8C,0x82,0			; « ⌂ »
tg4	db 0xE2,0x9C,0x82,0			; « ✂ »
tl4	db 'Couper',0
tg5	db 0xE2,0x97,0xAB,0			; « ◫ »
tl5	db 'Copier',0
tl6	db 'Gras',0
tg7	db 0xE2,0x9A,0x99,0			; « ⚙ »
tg8	db 0xE2,0x96,0xB6,0			; « ▶ »
tl8	db 'Lire',0
s_h_split db 'VOLETS ET S',0xC3,0x89,'PARATEUR (FEN',0xC3,0x8A,'TRE REDIMENSIONNABLE)',0
s_split	db 'Glissez la poign',0xC3,0xA9,'e entre les deux volets.',10,10,'La fen',0xC3,0xAA,'tre se redimensionne par son bord droit ou bas, ou par le bouton agrandir : les volets, la liste des pages et le texte multiligne suivent (ui_on_resize).',0
s_h_val	db 'GLISSI',0xC3,0x88,'RES, PROGRESSION, VU-M',0xC3,0x88,'TRE, LCD, NOMBRE, MOLETTE',0
s_lcd0	db '12:40',0
s_h_area db 'TEXTE MULTILIGNE (MONO, NUM',0xC3,0x89,'ROS DE LIGNE)',0
s_line	db 'Modifi',0xC3,0xA9,' : ligne ',0
s_col	db ', colonne ',0
s_area	db '; hello.asm - le premier programme de Korin OS',10
	db 'include ',0x27,'../../inc/armpi.inc',0x27,10
	db 'app_header',10,10
	db 'segment readable executable',10
	db 'start:',10
	db '	adr	x0,msg			; le message',10
	db '	sys	SYS_PRINT',10
	db '	mov	x0,0',10
	db '	sys	SYS_EXIT',10,10
	db 'segment readable writeable',10
	db 'msg	db ',0x27,'Bonjour, Korin OS !',0x27,',10,0',10,10
	db '; Essayez : clic, glisser, Maj + fl',0xC3,0xA8,'ches, Ctrl+A / C / X / V,',10
	db '; Tab (quatre espaces), Entr',0xC3,0xA9,'e, la molette et la barre de d',0xC3,0xA9,'filement.',10
	db '; Une ligne tr',0xC3,0xA8,'s longue fait d',0xC3,0xA9,'filer le texte horizontalement quand le curseur va au-del',0xC3,0xA0,' du bord droit de la zone.',10
	db 10,'ligne 18',10,'ligne 19',10,'ligne 20',10,'ligne 21',10,'ligne 22',10,'ligne 23',10,'ligne 24',10,'ligne 25',10,'ligne 26',10,'ligne 27',10,'ligne 28',10,'ligne 29',10,'ligne 30',10,'fin',0
s_h_choice db 'CASES, RADIOS, INTERRUPTEURS',0
s_h_dd	db 'LISTE D',0xC3,0x89,'ROULANTE',0
s_h_cb	db 'LISTE MODIFIABLE',0
s_ck1	db 'Afficher les fichiers cach',0xC3,0xA9,'s',0
s_ck2	db 'Confirmer avant de supprimer',0
s_ck3	db 'Option gris',0xC3,0xA9,'e',0
radio_labels dw rl0-radio_labels, rl1-radio_labels, rl2-radio_labels
rl0	db 'Petite',0
rl1	db 'Moyenne',0
rl2	db 'Grande',0
s_sw1	db 'Wi-Fi',0
s_sw2	db 'Bluetooth',0
s_dd	db 'Givre|Onyx Noir',0
s_cb	db 'Paris|Lyon|Marseille|Bruxelles|Gen',0xC3,0xA8,'ve|Montr',0xC3,0xA9,'al',0
s_cb0	db 'Lausanne',0
s_val	db 'Valeur : ',0
s_h_dlg	db 'DIALOGUES MODAUX',0
dlg_labels dw dl0-dlg_labels, dl1-dlg_labels, dl2-dlg_labels, dl3-dlg_labels, dl4-dlg_labels, dl5-dlg_labels
dl0	db 'Message',0
dl1	db 'Oui / Non / Annuler',0
dl2	db 'Saisie',0
dl3	db 'Ouvrir un fichier',0
dl4	db 'Enregistrer sous',0
dl5	db 'Choisir un dossier',0
s_dlg_help db 'Chaque bouton ouvre un dialogue modal : une seconde fenetre du programme, centr',0xC3,0xA9,'e. ',0xC3,0x89,'chap annule, Entr',0xC3,0xA9,'e choisit le bouton principal. La r',0xC3,0xA9,'ponse s',0x27,'affiche en bas.',0
s_dlg_t	db 'UIKit',0
s_dlg_msg db 'Ceci est un message de UIKit. Le texte est coup',0xC3,0xA9,' en lignes aux espaces ; un saut de ligne',10,'commence une nouvelle ligne.',0
s_dlg_q	db 'Enregistrer les modifications avant de fermer ?',0
s_dlg_ask db 'Comment s',0x27,'appelle votre machine ?',0
s_dlg_def db 'pocketterm',0
s_dlg_r	db 'R',0xC3,0xA9,'ponse : ',0
s_sd0	db 'SD0:/',0
s_home	db 'SD0:/home/documents/',0
s_newname db 'nouveau.txt',0
s_h_list db 'LISTE ',0xC3,0x80,' COLONNES',0
s_h_sb	db 'D',0xC3,0x89,'FILEMENT',0
s_h_text db 'CHAMPS DE TEXTE',0
s_c_name db 'Nom',0
s_c_size db 'Taille',0
s_c_kind db 'Type',0
s_hello	db 'Liste : cliquez un en-t',0xC3,0xAA,'te pour trier, glissez son bord ; double-clic : activer.',0
s_sel	db 'Choisi : ',0
s_act	db 'Activ',0xC3,0xA9,' : ',0
s_sorted db 'Liste tri',0xC3,0xA9,'e.',0
s_pos	db 'Barre de d',0xC3,0xA9,'filement : ',0
s_changed db 'Texte : ',0
s_entered db 'Entr',0xC3,0xA9,'e : ',0
s_ro	db 'Lecture seule : s',0xC3,0xA9,'lectionnez et copiez (Ctrl+C).',0
s_mono	db 'mov x0,#42 ; police mono',0
tb_flags db 0, UTF_PASSWORD, UTF_READONLY, UTF_MONO
	align 4
tb_labels dw tl0-tb_labels, tl1-tb_labels, tl2-tb_labels, tl3-tb_labels
tl0	db 'Nom',0
tl1	db 'Mot de passe',0
tl2	db 'Lecture seule',0
tl3	db 'Mono',0
	align 4
tb_hints dw th0-tb_hints, th1-tb_hints, th2-tb_hints, th3-tb_hints
th0	db 'Votre nom',0
th1	db 'Au moins 8 caract',0xC3,0xA8,'res',0
th2	db 0
th3	db 0
items:	db 'kernel8.img',9,'412 288',9,'Noyau',0
	db 'uikit.so',9,'61 440',9,'Kit',0
	db 'appkit.so',9,'18 432',9,'Kit',0
	db 'netkit.so',9,'9 216',9,'Kit',0
	db 'elegant',9,'46 200',9,'Programme',0
	db 'dock',9,'34 376',9,'Programme',0
	db 'menubar',9,'28 104',9,'Programme',0
	db 'theme.txt',9,'1 204',9,'Texte',0
	db 'system.ini',9,'312',9,'Texte',0
	db 'DejaVuSans.ttf',9,'757 076',9,'Police',0
	db 'DejaVuSans-Bold.ttf',9,'705 684',9,'Police',0
	db 'DejaVuSansMono.ttf',9,'340 712',9,'Police',0
	db 'readme.txt',9,'58',9,'Texte',0
	db 'paint.bmp',9,'1 440 054',9,'Image',0
	db 'Un nom de fichier vraiment tr',0xC3,0xA8,'s long pour voir l',0x27,'ellipse.txt',9,'7',9,'Texte',0
	db 'music',9,'0',9,'Dossier',0
	db 'pictures',9,'0',9,'Dossier',0
	db 'documents',9,'0',9,'Dossier',0
	db 'video',9,'0',9,'Dossier',0
	db 'fasmarm',9,'262 144',9,'Programme',0
	db 'selfhost',9,'12 288',9,'Programme',0
	db 'hello.asm',9,'1 024',9,'Source',0
	db 'clock.cfg',9,'64',9,'Texte',0
	db 0
	align 8
jetons	dd 0
page	dd 0
cur_w	dd WIN_W				; taille du contenu (contigus)
cur_h	dd WIN_H
w_left	dd 0
w_split	dd 0
w_right	dd 0
w_drop	dd 0
w_grid	dd 0
w_canvas dd 0
vdesc	dd 0
vbuf	rb 64
w_cdlg	dd 0
w_tree	dd 0
w_lang	dd 0
grid	dd 0
w_tbar	dd 0
w_seg	dd 0
w_tabs	dd 0
w_strip	dd 0
ndocs	dd 3
w_tools	rd 16
w_nav	dd 0
w_list	dd 0
w_sb	dd 0
w_tb	rd 4
w_area	dd 0
w_slider dd 0
w_prog	dd 0
w_vu	dd 0
w_lcd	dd 0
w_dlg	rd NDLG
numbuf	rb 512
answer	rb 256
dropbuf	rb 256
nw	dd 0
wtab	rb MAXW*16
line	rb 256
ctx_tr	rb 256
ui_data
