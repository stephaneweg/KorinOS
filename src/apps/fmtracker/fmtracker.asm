; fmtracker - FM Tracker : le tracker FM d'Onyx en assembleur. 8 canaux
; d'instruments FM a 2 operateurs (la synthese FM d'AudioKit) sur une grille de
; tranches de temps ; lit et ecrit les .FMS de FM Song et les instruments .FMI.
;
;   * La fenetre : barre de transport (lecture, depuis le debut, stop, boucle du
;     motif, position et temps, annuler / retablir, couper / copier / coller,
;     suivre), a gauche les motifs du morceau (nouveau, dupliquer, monter,
;     descendre, supprimer ; lignes et vitesse du motif ; octave et pas), la
;     grille, un piano dessous.
;   * Une colonne par canal ; son en-tete : l'instrument (un clic ouvre son
;     editeur), M (muet dans ce motif ; clic droit aussi), S (solo), un niveau.
;     Une ligne = une tranche de temps (vitesse / 20 s).
;   * Une case : une note commence (" C#4 "), " --- " : la note continue, vide :
;     silence. Une note dure jusqu'a la note ou au silence suivant de son canal.
;   * Touches : C D E F G A B une note (Maj : diese ; le curseur descend de
;     " pas " lignes), 0-7 l'octave, Espace un silence, Suppr " --- " (un bloc
;     choisi : efface), Retour arriere efface la tranche du dessus, # le diese,
;     Ctrl+haut / bas un demi-ton (Maj : une octave), fleches, Page, Debut, Fin,
;     Maj + celles-ci un bloc, Tab le canal suivant. Ctrl+X C V le bloc, Ctrl+A
;     tout le motif, Ctrl+Z Y annuler / retablir. Clic : une case, glisser : un
;     bloc ; molette et barre de defilement. Clic sur le piano : cette note.
;   * Ctrl+P joue depuis le curseur et suit la position ; Echap arrete. Un
;     morceau est une liste de motifs (chacun sa longueur, sa vitesse), joues
;     dans l'ordre.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'

; --- le format ---------------------------------------------------------------
FMS_CH		= 8
FMS_MAXPAT	= 64
FMS_MAXROWS	= 256
FMS_CONT	= 0				; octet de note : rien de neuf
FMS_OFF		= 128				; octet de note : silence
CELLS		= FMS_CH*FMS_MAXROWS		; une case de notes : n[canal * 256 + ligne]
P_AR		= 0				; les 26 octets d'un instrument
P_DR		= 2
P_SL		= 4
P_RR		= 6
P_TL		= 8
P_KSL		= 10
P_VIB		= 12
P_AM		= 14
P_MULT		= 16
P_KSR		= 18
P_WAVE		= 20
P_FB		= 22
P_CON		= 23
P_EGT		= 24
INS_P		= 9				; un instrument : nom (9), 26 octets
INS_SIZE	= 36
PT_ROWS		= 0				; fiche d'un motif
PT_SPEED	= 8
PT_N		= 16
PT_MUTE		= 24
PT_SIZE		= 32
UNDO_MAX	= 48
SN_PAT		= 0				; une photo de motif
SN_ROWS		= 8
SN_SPEED	= 16
SN_MUTE		= 24
SN_N		= 32
SN_SIZE		= SN_N + CELLS
FILE_MAX	= 140*1024			; un .FMS au plus (64 x 256 lignes)
ROW_MS		= 50				; une ligne : vitesse * 50 ms
PREVIEW_MS	= 350
TEST_MS		= 700
TEST_VOICE	= 15
SYNTH_VOICES	= 16
PCM_FRAMES	= 4096
MAX_PRESETS	= 96
PRESET_SIZE	= 13

; --- la fenetre -----------------------------------------------------------------
WIN_W0		= 980
WIN_H0		= 660
WIN_MIN_W	= 760
WIN_MIN_H	= 440
TOOL_H		= 50
SIDE_W		= 156
ST_H		= 22
PIANO_H		= 58
HEAD_H		= 46
GRID_Y		= TOOL_H + HEAD_H
SB_W		= 14
NUM_W		= 40
ROW_H		= 17
LCD_X		= 146
LCD_W		= 226
LCD_H		= 34
T2_X		= LCD_X + LCD_W + 4
LIST_Y		= TOOL_H + 30
LIST_ROW	= 24
SIDE_BELOW	= 300				; le panneau sous la liste
PIANO_KEYS	= 8*7				; touches blanches
PIANO_KH	= PIANO_H - 16
PIANO_BH	= PIANO_KH * 6 / 10
PLAY_COL	= 0x0048B068
SD_ROWS		= 24				; decalages depuis g_side_y (panneau)
SD_SPEED	= 56
SD_HTYPE	= 98
SD_OCT		= 122
SD_STEP		= 154
SD_HSONG	= 196
SD_TITLE	= 216

MODS_CTRL	= 0x11
MODS_SHIFT	= 0x22

; --- ce qui est a repeindre (flush) -----------------------------------------------
BIT_GRID	= 0
BIT_HEAD	= 1
BIT_LIST	= 2
BIT_PIANO	= 3
BIT_STATUS	= 4
BIT_LCD		= 5
BIT_BOXES	= 6
BIT_SCROLL	= 7
BIT_SIDE	= 8
BIT_ALL		= 9
BIT_TITLE	= 10
D_GRID		= 1 shl BIT_GRID
D_HEAD		= 1 shl BIT_HEAD
D_LIST		= 1 shl BIT_LIST
D_PIANO		= 1 shl BIT_PIANO
D_STATUS	= 1 shl BIT_STATUS
D_LCD		= 1 shl BIT_LCD
D_BOXES		= 1 shl BIT_BOXES
D_SCROLL	= 1 shl BIT_SCROLL
D_SIDE		= 1 shl BIT_SIDE
D_ALL		= 1 shl BIT_ALL
D_TITLE		= 1 shl BIT_TITLE
D_SONG		= D_GRID or D_HEAD or D_LIST or D_PIANO or D_LCD

; --- les commandes (menus, boutons) ------------------------------------------------
ID_NEW		= 1
ID_OPEN		= 2
ID_SAVE		= 3
ID_SAVEAS	= 4
ID_EXPORT	= 5
ID_INFO		= 6
ID_UNDO		= 10
ID_REDO		= 11
ID_CUT		= 12
ID_COPY		= 13
ID_PASTE	= 14
ID_ALL		= 15
ID_INSROW	= 16
ID_DELROW	= 17
ID_CLRCH	= 18
ID_UP1		= 19
ID_DOWN1	= 20
ID_UP12		= 21
ID_DOWN12	= 22
ID_PREV		= 30
ID_NEXT		= 31
ID_NEWPAT	= 32
ID_DUPPAT	= 33
ID_PATUP	= 34
ID_PATDOWN	= 35
ID_DELPAT	= 36
ID_INSTR	= 40
ID_MUTE		= 41
ID_SOLO		= 42
ID_PLAY		= 50
ID_START	= 51
ID_LOOP		= 52
ID_FOLLOW	= 53
ID_STOP		= 54
W_ROWS		= 60				; les nombres du panneau
W_SPEED		= 61
W_OCT		= 62
W_STEP		= 63

; lecture / ecriture d'une variable de 64 bits
macro ldv r*, v* {
	adr	r,v
	ldr	r,[r]
}
macro stv r*, v* {
	adr	x9,v
	str	r,[x9]
}
; lecture / ecriture d'une couleur de 32 bits
macro ldc r*, c* {
	adr	x9,c
	ldr	r,[x9]
}
macro stc r*, c* {
	adr	x9,c
	str	r,[x9]
}
; a repeindre (x9-x11 changent)
macro mark b* {
	adr	x9,g_redraw
	ldr	x10,[x9]
	movl	x11,b
	orr	x10,x10,x11
	str	x10,[x9]
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	stv	x0,g_args
	adr	x0,ui_win			; variables de l'interface
	bl	ui_init
	bl	dico_load
	bl	sin_init
	bl	slots_init
	bl	colors_init
	bl	win_size
	mov	x0,-1
	mov	x1,-1
	ldv	x2,g_w
	ldv	x3,g_h
	adr	x4,s_app
	mov	x5,WF_RESIZABLE
	bl	ui_window_ex
	tbnz	x0,63,quit
	mov	x0,SYNTH_VOICES			; la synthese, jouee en direct
	bl	ak_fm_new
	tbnz	x0,63,quit
	stv	x0,g_fm
	mov	x1,0
	bl	ak_fm_live
	mov	x0,WIN_MIN_W
	mov	x1,WIN_MIN_H
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	bl	menu_build
	adr	x0,g_menu
	bl	ui_menu
	mov	x0,ID_FOLLOW
	mov	x1,UMS_CHECKED
	bl	ui_menu_state
	mov	x0,UFN_MONOBOLD			; la grille : mono, notes en gras
	bl	ui_font_get
	stv	x0,g_font_note
	adr	x0,s_cell_w
	mov	x1,13
	adr	x2,ui_font_m
	bl	ui_text_width
	stv	x0,g_cell_tw
	bl	make_widgets
	bl	layout
	bl	song_defaults
	bl	after_load
	adr	x0,s_help
	bl	status_tr
	ldv	x0,g_args			; argument : le fichier a ouvrir
	cbz	x0,.go
	adr	x0,g_dlgpath
	ldv	x1,g_args
	mov	x2,256
	bl	str_copy
	adr	x0,g_dlgpath
	ldrb	w1,[x0]
	cbz	w1,.go
	bl	load_song
.go:	mark	D_ALL
main_loop:
	bl	tick
	bl	flush
	bl	busy
	mov	x1,200
	cmp	x0,0
	mov	x0,8				; (attente courte quand ca bouge)
	csel	x0,x0,x1,ne
	bl	ui_event
	cbnz	x0,.ev
	bl	ui_w_idle
	b	main_loop
.ev:	cmp	x0,WEV_CLOSE
	b.eq	.close
	cmp	x0,WEV_MENU
	b.eq	.menu
	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cmp	x0,WEV_WHEEL
	b.eq	.wheel
	cmp	x0,WEV_DROP
	b.eq	.drop
	mov	x0,0
	bl	ui_w_event
	b	main_loop
.close:	bl	confirm_discard
	cbz	x0,main_loop
	b	quit
.menu:	adr	x0,ui_ev
	ldr	w0,[x0,EV_CODE]
	bl	on_command
	b	main_loop
.key:	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,main_loop
	mov	x0,0
	bl	ui_menu_key
	tbnz	x0,63,.nk
	bl	on_command
	b	main_loop
.nk:	bl	ui_w_get_focus
	cbz	x0,.grid
	mov	x0,0
	bl	ui_w_event
	cbnz	x2,main_loop
.grid:	bl	grid_key
	b	main_loop
.mouse:	adr	x0,ui_ev
	ldrsw	x1,[x0,EV_X]
	ldrsw	x2,[x0,EV_Y]
	stv	x1,g_mx
	stv	x2,g_my
	ldv	x0,g_drag
	cbnz	x0,.mm
	mov	x0,0
	bl	ui_w_event
	cbnz	x2,main_loop
.mm:	bl	on_mouse
	b	main_loop
.wheel:	mov	x0,0
	bl	ui_w_event
	cbnz	x2,main_loop
	adr	x0,ui_ev
	ldrsw	x0,[x0,EV_Y]
	bl	on_wheel
	b	main_loop
.drop:	mov	x0,0
	bl	ui_w_event
	adr	x0,g_dlgpath			; un fichier depose : ouvert
	mov	x1,255
	bl	ui_drop_get
	cmp	x1,UDT_FILES
	b.ne	main_loop
	cmp	x0,0
	b.le	main_loop
	adr	x1,g_dlgpath
	strb	wzr,[x1,x0]
	mov	x2,0
.dl:	ldrb	w3,[x1,x2]
	cbz	w3,.dg
	cmp	w3,10
	b.eq	.dc
	add	x2,x2,1
	b	.dl
.dc:	strb	wzr,[x1,x2]
.dg:	bl	confirm_discard
	cbz	x0,main_loop
	adr	x0,g_dlgpath
	bl	load_song
	b	main_loop

quit:	ldv	x0,g_fm
	cbz	x0,.x
	mov	x1,-1
	bl	ak_fm_live
.x:	mov	x0,0
	sys	SYS_EXIT

; -> x0 = 1 si quelque chose bouge (lecture, note d'essai, niveaux)
busy:
	ldv	x0,g_playing
	cbnz	x0,.y
	ldv	x0,g_pv_voice
	tbz	x0,63,.y
	adr	x0,g_vu
	mov	x1,0
	mov	x2,0
.l:	ldr	x3,[x0,x2,lsl 3]
	orr	x1,x1,x3
	add	x2,x2,1
	cmp	x2,FMS_CH
	b.lo	.l
	cbnz	x1,.y
	mov	x0,0
	ret
.y:	mov	x0,1
	ret

; le dictionnaire du programme : SD0:/apps/fmtracker.app/lang/<langue>.txt
dico_load:
	prologue
	sub	sp,sp,96
	strb	wzr,[sp]
	mov	x0,sp
	adr	x1,s_lang_dir
	bl	str_cat
	str	x0,[sp,88]
	bl	ui_lang
	mov	x1,x0
	ldr	x0,[sp,88]
	bl	str_cat
	adr	x1,s_lang_ext
	bl	str_cat
	mov	x0,sp
	bl	ui_tr_load
	add	sp,sp,96
	epilogue

; la taille de depart : celle d'Onyx, moins si l'ecran est plus petit
win_size:
	prologue
	sub	sp,sp,32
	stp	xzr,xzr,[sp]
	stp	xzr,xzr,[sp,16]
	mov	x0,sp
	bl	ui_screen
	ldp	x1,x2,[sp]
	ldp	x3,x4,[sp,16]
	add	sp,sp,32
	mov	x5,WIN_W0
	mov	x6,WIN_H0
	cbz	x1,.st
	sub	x1,x1,12
	cmp	x1,x5
	csel	x5,x1,x5,lt
	sub	x2,x2,x3
	sub	x2,x2,x4
	sub	x2,x2,FRAME_TITLE_H+8
	cmp	x2,x6
	csel	x6,x2,x6,lt
	mov	x1,WIN_MIN_W
	cmp	x5,x1
	csel	x5,x1,x5,lt
	mov	x1,WIN_MIN_H
	cmp	x6,x1
	csel	x6,x1,x6,lt
.st:	adr	x0,g_w
	stp	x5,x6,[x0]
	epilogue

; le texte des menus, libelles traduits, dans g_menu
menu_build:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,menus_src
	adr	x20,g_menu
.line:	ldrb	w0,[x19]
	cbz	w0,.end
	cmp	w0,'M'
	b.eq	.m
	cmp	w0,'I'
	b.eq	.i
.copy:	ldrb	w0,[x19],1			; le reste de la ligne tel quel
	strb	w0,[x20],1
	cbz	w0,.end0
	cmp	w0,10
	b.ne	.copy
	b	.line
.m:	strb	w0,[x20],1
	add	x19,x19,1
	b	.label
.i:	ldrb	w0,[x19],1			; id, tabulation
	strb	w0,[x20],1
	cmp	w0,9
	b.ne	.i
.label:	adr	x0,g_tmp2
	mov	x1,0
.l:	ldrb	w2,[x19]
	cbz	w2,.tr
	cmp	w2,9
	b.eq	.tr
	cmp	w2,10
	b.eq	.tr
	strb	w2,[x0,x1]
	add	x1,x1,1
	add	x19,x19,1
	b	.l
.tr:	strb	wzr,[x0,x1]
	bl	ui_tr
.c:	ldrb	w1,[x0],1
	cbz	w1,.copy
	strb	w1,[x20],1
	b	.c
.end:	strb	wzr,[x20]
.end0:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = barre, x1 = table (premiere entree), x2 = entrees, x3 = variables des
; boutons a garder (ou 0) : les boutons d'une barre d'outils
tools_add:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x22,tools
.l:	cbz	x21,.e
	ldr	w0,[x20,4]			; libelle
	mov	x23,0
	cbz	w0,.g
	add	x0,x0,x22
	bl	ui_tr
	mov	x23,x0
.g:	ldr	w1,[x20]			; glyphe
	cbz	w1,.a
	add	x1,x1,x22
.a:	mov	x0,x19
	mov	x2,x23
	ldr	w3,[x20,8]
	bl	ui_toolbar_add
	mov	x23,x0
	ldrsw	x1,[x20,20]
	cbz	x1,.nv
	adr	x2,tools
	add	x1,x1,x2
	str	x23,[x1]
.nv:	mov	x0,x23
	adr	x1,on_widget
	ldr	w2,[x20,12]
	bl	ui_w_on
	mov	x0,x23				; (valeur 0 / 1 des boutons a bascule)
	mov	x1,0
	mov	x2,1
	mov	x3,0
	mov	x4,0
	bl	ui_w_set_range
	ldr	w0,[x20,16]			; infobulle
	add	x0,x0,x22
	bl	ui_tr
	mov	x1,x0
	mov	x0,x23
	bl	ui_w_tip
	add	x20,x20,TOOL_SIZE
	sub	x21,x21,1
	b	.l
.e:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = glyphe, x1 = id, x2 = infobulle, x3 = rang : petit bouton du panneau
side_button:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x4,x0
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	mov	x0,0
	mov	x1,0
	mov	x2,27
	mov	x3,26
	mov	x5,UB_GHOST
	bl	ui_pushbutton
	adr	x1,w_side
	str	x0,[x1,x21,lsl 3]
	mov	x21,x0
	adr	x1,on_widget
	mov	x2,x19
	bl	ui_w_on
	mov	x0,x20
	bl	ui_tr
	mov	x1,x0
	mov	x0,x21
	bl	ui_w_tip
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = min, x1 = max, x2 = valeur, x3 = id, x4 = infobulle (0 : aucune), x5 =
; variable du widget
side_numeric:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x3
	mov	x20,x4
	mov	x21,x5
	mov	x4,x0
	mov	x5,x1
	mov	x6,x2
	mov	x0,0
	mov	x1,0
	mov	x2,80
	mov	x3,28
	bl	ui_numeric
	str	x0,[x21]
	mov	x21,x0
	adr	x1,on_widget
	mov	x2,x19
	bl	ui_w_on
	cbz	x20,.o
	mov	x0,x20
	bl	ui_tr
	mov	x1,x0
	mov	x0,x21
	bl	ui_w_tip
.o:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

make_widgets:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,0				; transport
	mov	x1,0
	mov	x2,LCD_X-4
	mov	x3,TOOL_H
	bl	ui_toolbar
	stv	x0,w_t1
	adr	x1,tools_1
	mov	x2,TOOLS_1
	bl	tools_add
	mov	x0,LCD_X			; position, temps
	mov	x1,(TOOL_H-LCD_H)/2
	mov	x2,LCD_W
	mov	x3,LCD_H
	adr	x4,s_lcd0
	bl	ui_lcd
	stv	x0,w_lcd
	adr	x0,s_lcd_tip
	bl	ui_tr
	mov	x1,x0
	ldv	x0,w_lcd
	bl	ui_w_tip
	mov	x0,T2_X				; edition
	mov	x1,0
	mov	x2,WIN_W0-T2_X
	mov	x3,TOOL_H
	bl	ui_toolbar
	stv	x0,w_t2
	adr	x1,tools_2
	mov	x2,TOOLS_2
	bl	tools_add
	adr	x0,s_g_plus			; panneau : les motifs
	mov	x1,ID_NEWPAT
	adr	x2,s_t_newpat
	mov	x3,0
	bl	side_button
	adr	x0,s_g_dup
	mov	x1,ID_DUPPAT
	adr	x2,s_t_duppat
	mov	x3,1
	bl	side_button
	adr	x0,s_g_up
	mov	x1,ID_PATUP
	adr	x2,s_t_patup
	mov	x3,2
	bl	side_button
	adr	x0,s_g_down
	mov	x1,ID_PATDOWN
	adr	x2,s_t_patdown
	mov	x3,3
	bl	side_button
	adr	x0,s_g_del
	mov	x1,ID_DELPAT
	adr	x2,s_t_delpat
	mov	x3,4
	bl	side_button
	mov	x0,1				; ce motif
	mov	x1,FMS_MAXROWS
	mov	x2,64
	mov	x3,W_ROWS
	mov	x4,0
	adr	x5,w_rows
	bl	side_numeric
	mov	x0,1
	mov	x1,40
	mov	x2,3
	mov	x3,W_SPEED
	adr	x4,s_t_speed
	adr	x5,w_speed
	bl	side_numeric
	mov	x0,0				; saisie
	mov	x1,7
	ldv	x2,g_oct
	mov	x3,W_OCT
	mov	x4,0
	adr	x5,w_oct
	bl	side_numeric
	mov	x0,0
	mov	x1,16
	ldv	x2,g_step
	mov	x3,W_STEP
	adr	x4,s_t_step
	adr	x5,w_step
	bl	side_numeric
	mov	x0,0				; barre de defilement
	mov	x1,0
	mov	x2,SB_W
	mov	x3,100
	mov	x4,0
	bl	ui_scrollbar
	stv	x0,w_sb
	adr	x1,on_scrollbar
	mov	x2,0
	bl	ui_w_on
	ldp	x19,x20,[sp],16
	epilogue

; la fenetre change de taille : tout se replace
layout:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,g_w
	ldp	x19,x20,[x0]
	sub	x0,x19,SIDE_W+SB_W+NUM_W	; colonnes
	lsr	x0,x0,3
	stv	x0,g_col_w
	lsl	x1,x0,3
	add	x21,x1,NUM_W
	stv	x21,g_gw
	sub	x22,x20,ST_H+PIANO_H+GRID_Y
	stv	x22,g_gh
	mov	x0,ROW_H
	udiv	x0,x22,x0
	stv	x0,g_vis
	sub	x0,x19,SIDE_W			; piano
	sub	x1,x0,16
	mov	x2,PIANO_KEYS
	udiv	x1,x1,x2
	stv	x1,g_kw
	mul	x1,x1,x2
	sub	x0,x0,x1
	lsr	x0,x0,1
	add	x0,x0,SIDE_W
	stv	x0,g_kx
	sub	x0,x20,TOOL_H+ST_H+30+SIDE_BELOW ; liste des motifs
	mov	x1,LIST_ROW*2
	cmp	x0,x1
	csel	x0,x1,x0,lt
	mov	x1,LIST_ROW
	udiv	x0,x0,x1
	stv	x0,g_lvis
	mul	x0,x0,x1
	stv	x0,g_list_h
	add	x19,x0,LIST_Y+8			; y des petits boutons
	mov	x20,0
.b:	adr	x0,w_side
	ldr	x0,[x0,x20,lsl 3]
	mov	x1,29
	mul	x1,x1,x20
	add	x1,x1,6
	mov	x2,x19
	mov	x3,27
	mov	x4,26
	bl	ui_w_move
	add	x20,x20,1
	cmp	x20,5
	b.lo	.b
	add	x19,x19,40			; g_side_y
	stv	x19,g_side_y
	ldv	x0,w_rows
	add	x2,x19,SD_ROWS
	bl	.num
	ldv	x0,w_speed
	add	x2,x19,SD_SPEED
	bl	.num
	ldv	x0,w_oct
	add	x2,x19,SD_OCT
	bl	.num
	ldv	x0,w_step
	add	x2,x19,SD_STEP
	bl	.num
	ldv	x0,w_t2
	mov	x1,T2_X
	mov	x2,0
	ldv	x3,g_w
	sub	x3,x3,T2_X
	mov	x4,TOOL_H
	bl	ui_w_move
	ldv	x0,w_sb
	add	x1,x21,SIDE_W
	mov	x2,GRID_Y
	mov	x3,SB_W
	mov	x4,x22
	bl	ui_w_move
	stv	xzr,g_sb_rows
	ldv	x0,g_row
	mov	x1,0
	bl	ensure_visible
	bl	list_show
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.num:	mov	x1,60				; (x0 = nombre, x2 = y)
	add	x2,x2,2
	mov	x3,90
	mov	x4,24
	b	ui_w_move

; fonction de ui_on_resize : x0 = largeur, x1 = hauteur
on_resize:
	prologue
	adr	x2,g_w
	stp	x0,x1,[x2]
	bl	layout
	mark	D_ALL
	epilogue

; fonction des widgets de la fenetre : x0 = widget, x1 = code
on_widget:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	ui_w_user
	cmp	x0,W_ROWS
	b.hs	.num
	cmp	x20,UN_ACTIVATE
	b.ne	.out
	bl	on_command
	mov	x0,0
	bl	ui_w_focus
	b	.out
.num:	cmp	x20,UN_CHANGE
	b.ne	.out
	mov	x20,x0
	mov	x0,x19
	bl	ui_w_get_value
	mov	x19,x0
	cmp	x20,W_ROWS
	b.ne	.sp
	bl	undo_push
	bl	cur_pat
	mov	x1,x19
	bl	pattern_resize
	bl	refresh_pattern
	b	.foc
.sp:	cmp	x20,W_SPEED
	b.ne	.oc
	bl	undo_push
	bl	cur_pat
	str	x19,[x0,PT_SPEED]
	mark	D_LCD
	b	.foc
.oc:	cmp	x20,W_OCT
	b.ne	.st
	stv	x19,g_oct
	mark	D_PIANO
	b	.foc
.st:	stv	x19,g_step
.foc:	adr	x0,ui_ev			; a la souris : le clavier revient a la grille
	ldr	w0,[x0,EV_TYPE]
	cmp	w0,WEV_MOUSE
	b.ne	.out
	mov	x0,0
	bl	ui_w_focus
.out:	ldp	x19,x20,[sp],16
	epilogue

include 'fmt_song.inc'
include 'fmt_edit.inc'
include 'fmt_view.inc'
include 'fmt_ins.inc'

segment readable writeable
kit_data

		align 8
g_args		dd 0
g_fm		dd 0
g_w		dd WIN_W0			; g_w, g_h contigus
g_h		dd WIN_H0
g_col_w		dd 96
g_gw		dd 0
g_gh		dd 0
g_vis		dd 1
g_kw		dd 1
g_kx		dd 0
g_lvis		dd 1
g_list_h	dd 0
g_ltop		dd 0
g_side_y	dd 0
g_redraw	dd 0
g_in_dlg	dd 0
g_pat		dd 0
g_row		dd 0
g_ch		dd 0
g_top		dd 0
g_oct		dd 4
g_step		dd 1
g_sel		dd 0
g_ar		dd 0
g_ac		dd 0
g_modified	dd 0
g_playing	dd 0
g_loop		dd 0
g_follow	dd 1
g_solo		dd -1
g_play_pat	dd 0
g_play_row	dd 0
g_shown_row	dd -1
g_next_tick	dd 0
g_pv_voice	dd -1
g_pv_end	dd 0
g_vu_tick	dd 0
g_nundo		dd 0
g_nredo		dd 0
g_clip_r	dd 0
g_clip_c	dd 0
g_npat		dd 1
g_mx		dd 0
g_my		dd 0
g_drag		dd 0				; 1 : la grille, 2 : un nom de canal
g_down_ch	dd 0
g_hot_head	dd -1
g_hot_list	dd -1
g_sb_rows	dd 0
g_sb_vis	dd 0
g_sb_top	dd 0
g_font_note	dd 0
g_cell_tw	dd 24
g_m_done	dd 0
g_m_res		dd 0
g_dlg		dd 0
g_dch		dd 0
g_anim		dd 0
g_anim_t0	dd 0
g_anim_t	dd 0
g_npreset	dd 0
g_vu		rd FMS_CH
w_t1		dd 0
w_t2		dd 0
w_lcd		dd 0
w_play		dd 0
w_loop		dd 0
w_follow	dd 0
w_rows		dd 0
w_speed		dd 0
w_oct		dd 0
w_step		dd 0
w_sb		dd 0
w_side		rd 5
d_name		dd 0
d_conn		dd 0
d_fb		dd 0
d_list		dd 0
d_sl		rd 14
d_chk		rd 8
d_wave		rd 2
sin_c		dd 1.9999623505652, 0.00613588464915, 4096.0
c_face		dw 0
c_field		dw 0
c_text		dw 0
c_text2		dw 0
c_ln		dw 0
c_acc		dw 0
c_acc_ink	dw 0
c_hollow	dw 0
c_gline		dw 0
c_row4		dw 0
c_row16		dw 0
c_gnum		dw 0
c_cont		dw 0
c_gmute		dw 0
c_gutter	dw 0
c_playbg	dw 0
c_gplay		dw 0
c_gcur		dw 0
c_headcur	dw 0
ch_col		dw 0x00C98A36, 0x00BC5069, 0x005E9F58, 0x00C85C40
		dw 0x008E6CC4, 0x004A9FB0, 0x00B5A040, 0x007484C8
include 'fmt_tables.inc'

; les boutons des barres d'outils : glyphe, libelle, drapeaux, id, infobulle,
; variable du bouton (decalages depuis tools ; 0 : aucun)
macro tool g*, l*, f*, id*, tip*, v* {
	dw g - tools, l - tools, f, id, tip - tools, v - tools
}
TOOL_SIZE	= 24
tools:
tools_1:
	tool s_g_play, tools, UTB_TOGGLE, ID_PLAY, s_t_play, w_play
	tool s_g_start, tools, 0, ID_START, s_t_start, tools
	tool s_g_stop, tools, 0, ID_STOP, s_t_stop, tools
	tool s_g_loop, tools, UTB_TOGGLE, ID_LOOP, s_t_loop, w_loop
TOOLS_1		= ($ - tools_1) / TOOL_SIZE
tools_2:
	tool s_g_undo, tools, 0, ID_UNDO, s_t_undo, tools
	tool s_g_redo, tools, 0, ID_REDO, s_t_redo, tools
	tool tools, s_cut, UTB_SEP, ID_CUT, s_t_cut, tools
	tool tools, s_copy, 0, ID_COPY, s_t_copy, tools
	tool tools, s_paste, 0, ID_PASTE, s_t_paste, tools
	tool tools, s_follow, UTB_SEP or UTB_TOGGLE, ID_FOLLOW, s_t_follow, w_follow
TOOLS_2		= ($ - tools_2) / TOOL_SIZE
sl_names	dw s_attack-sl_names, s_decay-sl_names, s_sustain-sl_names, s_release-sl_names
		dw s_volume-sl_names, s_multiplier-sl_names, s_keyscale-sl_names
chk_names	dw s_held-chk_names, s_tremolo-chk_names, s_vibrato-chk_names, s_ksr-chk_names

semi_tab	db 0,0,2,4,5,7,9,11
tr_note		db 1,1,2,2,3,4,4,5,5,6,6,7
tr_sharp	db 0,1,0,1,0,0,1,0,1,0,1,0
fmi_order	db P_AR,P_DR,P_SL,P_RR,P_TL,P_KSL,P_VIB,P_AM,P_MULT,P_KSR,P_WAVE,P_EGT
sl_field	db P_AR,P_DR,P_SL,P_RR,P_TL,P_MULT,P_KSL
sl_hi		db 15,15,15,15,63,15,3
chk_field	db P_EGT,P_AM,P_VIB,P_KSR
ins_epiano	db 15,15,1,2,5,7,3,4,20,0,0,0,0,0,0,0,1,1,0,0,0,0,3,0,1,0
s_epiano	db 'EPIANO',0,0,0
s_magic		db 'fm-song-project'
s_note_letters	db '?CDEFGAB'
s_note_keys	db 'cdefgab',0
s_fmi_head	db 'fm-song instrument',0
s_fmi_start	db '"fm-song instrument"',13,10,'"',0
s_fmi_q		db '"',13,10,0
s_crlf		db 13,10,0
s_app		db 'FM Tracker',0
s_untitled_raw	db 'Untitled',0
s_lang_dir	db 'SD0:/apps/fmtracker.app/lang/',0
s_lang_ext	db '.txt',0
s_piano_fmi	db 'SD0:/apps/fmtracker.app/ins/PIANO.FMI',0
s_ins_dir	db 'SD0:/apps/fmtracker.app/ins',0
s_song_dir	db 'SD0:/home/music/fms',0
s_tmp_dir	db 'SD0:/tmp',0
s_tmp_fms	db 'SD0:/tmp/fmtracker-export.fms',0
s_default_fms	db 'SONG.FMS',0
s_ext_fms	db '.FMS',0
s_ext_fmi	db '.FMI',0
s_ext_wav	db '.WAV',0
s_slash		db '/',0
s_space		db ' ',0
s_dash		db ' - ',0
s_star		db ' *',0
s_empty		db 0
s_cell_w	db 'C#4',0
s_lcd0		db '01:000 0:00.0',0
s_head_fmt	db '%d  ',0
s_m		db 'M',0
s_s		db 'S',0
s_half		db 'x1/2',0
s_arrow		db 0xE2,0x96,0xB6,0
s_fm_bar	db 'FM|',0
s_out		db 'out',0
s_instr_name	db 'INSTR',0
s_g_play	db 0xE2,0x96,0xB6,0
s_g_start	db 0xE2,0x87,0xA4,0
s_g_stop	db 0xE2,0x96,0xA0,0
s_g_loop	db 0xE2,0x86,0xBB,0
s_g_undo	db 0xE2,0x86,0xB6,0
s_g_redo	db 0xE2,0x86,0xB7,0
s_g_plus	db '+',0
s_g_dup		db 0xE2,0x8A,0x9E,0
s_g_up		db 0xE2,0x96,0xB2,0
s_g_down	db 0xE2,0x96,0xBC,0
s_g_del		db 0xE2,0x9C,0x95,0
menus_src	db 'MFile',10
		db 'I1',9,'New',9,'Ctrl+N',10
		db 'I2',9,'Open...',9,'Ctrl+O',10
		db 'I3',9,'Save',9,'Ctrl+S',10
		db 'I4',9,'Save As...',10
		db 'I5',9,'Export WAV...',10
		db '-',10
		db 'I6',9,'Song Info...',10
		db 'MEdit',10
		db 'I10',9,'Undo',9,'Ctrl+Z',10
		db 'I11',9,'Redo',9,'Ctrl+Y',10
		db '-',10
		db 'I12',9,'Cut',9,'Ctrl+X',10
		db 'I13',9,'Copy',9,'Ctrl+C',10
		db 'I14',9,'Paste',9,'Ctrl+V',10
		db 'I15',9,'Select the Pattern',9,'Ctrl+A',10
		db '-',10
		db 'I16',9,'Insert Slice',9,'Ctrl+E',10
		db 'I17',9,'Delete Slice',9,'Ctrl+D',10
		db 'I18',9,'Clear Channel',10
		db '-',10
		db 'I19',9,'Semitone Up',10
		db 'I20',9,'Semitone Down',10
		db 'I21',9,'Octave Up',10
		db 'I22',9,'Octave Down',10
		db 'MPattern',10
		db 'I30',9,'Previous',9,'Ctrl+B',10
		db 'I31',9,'Next',9,'Ctrl+F',10
		db '-',10
		db 'I32',9,'New Pattern',10
		db 'I33',9,'Duplicate',10
		db 'I34',9,'Move Earlier',10
		db 'I35',9,'Move Later',10
		db 'I36',9,'Delete...',10
		db 'MChannel',10
		db 'I40',9,'Instrument...',10
		db 'I41',9,'Mute in this Pattern',10
		db 'I42',9,'Solo',10
		db 'MPlay',10
		db 'I50',9,'Play / Stop',9,'Ctrl+P',10
		db 'I51',9,'From the Start',10
		db 'I52',9,'Loop the Pattern',9,'Ctrl+L',10
		db 'I53',9,'Follow',10
		db 'I54',9,'Stop',10,0
s_help		db 'C D E F G A B: a note (Shift: #), 0-7: octave, Space: silence, Del: ---, Shift+arrows: a block, Ctrl+P: play',0
s_no_undo	db 'Nothing to undo.',0
s_no_redo	db 'Nothing to redo.',0
s_copied	db 'Copied: %d rows x %d channels.',0
s_no_paste	db 'Nothing to paste: copy a block first.',0
s_stopped	db 'Stopped.',0
s_playing	db 'Playing -- Esc or Stop to stop.',0
s_playing_loop	db 'Playing this pattern in a loop -- Esc or Stop to stop.',0
s_loop_on	db 'Loop: the pattern plays again and again.',0
s_loop_off	db 'Loop off: the patterns play in order.',0
s_untitled	db 'Untitled',0
s_modif		db '  (modified)',0
s_save_first	db 'The song has changed. Save it first?',0
s_cant_open	db 'Cannot open',0
s_not_fms	db 'This is not an FM Song (.FMS) file.',0
s_cant_save	db 'Cannot save',0
s_cant_export	db 'Cannot export',0
s_exporting	db 'Exporting',0
s_exported	db 'Exported',0
s_max_pat	db 'At most 64 patterns.',0
s_keep_pat	db 'A song keeps at least one pattern.',0
s_pattern	db 'Pattern',0
s_del_pat	db 'Delete this pattern?',0
s_none		db '(none)',0
s_nrows		db '%d rows',0
s_h_patterns	db 'PATTERNS',0
s_h_this	db 'THIS PATTERN',0
s_h_typing	db 'TYPING',0
s_h_song	db 'SONG',0
s_rows		db 'Rows',0
s_speed		db 'Speed',0
s_octave	db 'Octave',0
s_step		db 'Step',0
s_cut		db 'Cut',0
s_copy		db 'Copy',0
s_paste		db 'Paste',0
s_follow	db 'Follow',0
s_t_play	db 'Play from the cursor / stop (Ctrl+P)',0
s_t_start	db 'Play from the start',0
s_t_stop	db 'Stop (Esc)',0
s_t_loop	db 'Loop: play this pattern again and again (Ctrl+L)',0
s_t_undo	db 'Undo (Ctrl+Z)',0
s_t_redo	db 'Redo (Ctrl+Y)',0
s_t_cut		db 'Cut the block (Ctrl+X)',0
s_t_copy	db 'Copy the block (Ctrl+C)',0
s_t_paste	db 'Paste at the cursor (Ctrl+V)',0
s_t_follow	db 'Follow the position while it plays',0
s_t_newpat	db 'A new pattern after this one',0
s_t_duppat	db 'Duplicate this pattern',0
s_t_patup	db 'Move it earlier in the song',0
s_t_patdown	db 'Move it later in the song',0
s_t_delpat	db 'Delete this pattern',0
s_t_speed	db 'A row lasts speed / 20 s',0
s_t_step	db 'The rows the cursor goes down after a note',0
s_lcd_tip	db 'PATTERN : ROW',0
s_ins_of	db 'Instrument of channel %d',0
s_name		db 'Name',0
s_preset_btn	db '(preset...)',0
s_preset_title	db 'Presets',0
s_load		db 'Load...',0
s_save_btn	db 'Save...',0
s_algorithm	db 'Algorithm',0
s_additive	db 'Additive',0
s_feedback	db 'Feedback',0
s_op1		db 'Op 1',0
s_op2		db 'Op 2',0
s_op_1a		db 'Operator 1',0
s_op_2a		db 'Operator 2',0
s_op_1f		db 'Operator 1 - modulator (the timbre)',0
s_op_2f		db 'Operator 2 - carrier (what is heard)',0
s_attack	db 'Attack',0
s_decay		db 'Decay',0
s_sustain	db 'Sustain',0
s_release	db 'Release',0
s_volume	db 'Volume',0
s_multiplier	db 'Multiplier',0
s_keyscale	db 'Key scale',0
s_held		db 'Held (sustain)',0
s_tremolo	db 'Tremolo',0
s_vibrato	db 'Vibrato',0
s_ksr		db 'Key scale rate',0
s_test		db 'Test',0
s_ok		db 'OK',0
s_cancel	db 'Cancel',0
s_test_hint	db 'Test plays it: the wave moves with the note.',0
s_wave_static	db "The sound's wave (two periods, at full volume)",0
s_wave_anim	db "The sound's wave, as the note sounds",0
s_instrument	db 'Instrument',0
s_not_fmi	db 'Not an .FMI instrument file.',0
s_cant_write	db 'Cannot write the file.',0
s_song_info	db 'Song information',0
s_title_l	db 'Title',0
s_author_l	db 'Author',0
s_comment_l	db 'Comment',0
		align 8
g_title		rb 21
g_author	rb 21
g_comment	rb 51
g_snd		rb FMS_CH + 8
		align 8
g_akins		rb AKFI_SIZE + 8
d_ins		rb INS_SIZE
g_ins_tmp	rb INS_SIZE
g_ins_keep	rb INS_SIZE
		align 8
g_line		rb 96
g_lcdtxt	rb 32
g_tmp		rb 512
g_tmp2		rb 128
g_status	rb 512
g_path		rb 260
g_dlgpath	rb 260
g_dirbuf	rb 260
g_fmibuf	rb 1028
g_dirent	rb DIRENT_BYTES + 8
g_menu		rb WS_MENU_MAX + 16
g_ins		rb FMS_CH*INS_SIZE
		align 8
g_pats		rb FMS_MAXPAT*PT_SIZE
sin_tab		rh 1024
g_presets	rb MAX_PRESETS*PRESET_SIZE
		align 8
ui_data
		align 8
g_clip		rb CELLS
g_pcm		rb PCM_FRAMES*4*2
g_notes		rb FMS_MAXPAT*CELLS
g_file		rb FILE_MAX + 16
g_undo		rb UNDO_MAX*SN_SIZE
g_redo		rb UNDO_MAX*SN_SIZE
