; editeur - editeur de texte d'ARMPI (UTF-8, police mono, numeros de ligne)
;
; Le texte est un widget de UIKit (ui_textarea : numeros de ligne dans une
; marge, barres de defilement verticale et horizontale, couleurs du code
; pour les sources .asm / .inc, annuler / retablir, presse-papiers). En
; haut, le champ chemin (ui_textbox : nom pour Ouvrir/Enregistrer, Entree
; ouvre). Menus Fichier et Edition dans la barre de menus globale. Ctrl+S
; enregistre, Ctrl+O ouvre le chemin du champ, Ctrl+N nouveau, Ctrl+W ferme,
; Ctrl+B ouvre PiBuild (SD0:/apps/builder.app) sur le dossier du
; fichier, Ctrl+F cherche, F3 cherche encore ; le widget traite Ctrl+Z, Y,
; X, C, V, A et le reste de la frappe.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W		= 720
WIN_H		= 500
TB_H		= 40
SB_H		= 22
FONT_PX		= 15
TCAP		= 1024*1024			; taille max d'un fichier
TABW		= 4

PATH_X		= 8
PATH_W		= WIN_W - PATH_X - 8 - FRAME_CAP_W	; (a gauche des boutons de la fenetre)
WIN_MIN_W	= 360
WIN_MIN_H	= 200

ID_NOUVEAU	= 1				; entrees du menu Fichier
ID_OUVRIR	= 2
ID_ENREG	= 3
ID_FERMER	= 4
ID_ASSEMBLER	= 5
ID_PARCOURIR	= 6
ID_ENREG_SOUS	= 7
ID_ANNULER	= 10				; menu Edition
ID_RETABLIR	= 11
ID_COUPER	= 12
ID_COPIER	= 13
ID_COLLER	= 14
ID_TOUT		= 15
ID_CHERCHER	= 16
ID_SUIVANT	= 17

AREA_FLAGS	= UTF_MONO or UTA_LINENUM or UTA_VSCROLL or UTA_HSCROLL or UTA_TABS

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	bl	ui_arg				; (un chemin entre guillemets : UIKit)
	mov	x19,x0				; ligne d'arguments
	adr	x0,ui_win				; variables de l'interface
	bl	ui_init
	mov	x0,TCAP				; tampon de lecture
	bl	malloc
	cbz	x0,quit
	adr	x1,text
	str	x0,[x1]
	mov	x0,-1
	mov	x1,-1
	mov	x2,WIN_W
	mov	x3,WIN_H
	adr	x4,s_title0
	bl	tr4
	mov	x5,WF_RESIZABLE
	bl	ui_window_ex
	tbnz	x0,63,quit
	adr	x0,menus			; menus dans la barre de menus globale
	bl	ui_menu
	bl	ui_jetons
	adr	x1,jetons
	str	x0,[x1]
	mov	x0,WIN_MIN_W
	mov	x1,WIN_MIN_H
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	mov	x0,PATH_X			; champ chemin
	mov	x1,6
	mov	x2,PATH_W
	mov	x3,28
	mov	x4,0
	bl	ui_textbox
	adr	x1,w_path
	str	x0,[x1]
	mov	x0,4				; le texte
	mov	x1,TB_H+2
	mov	x2,WIN_W-8
	mov	x3,WIN_H-TB_H-SB_H-6
	mov	x4,AREA_FLAGS
	bl	ui_textarea
	adr	x1,w_area
	str	x0,[x1]
	mov	x1,FONT_PX
	bl	ui_textarea_font
	adr	x0,w_area
	ldr	x0,[x0]
	bl	ui_w_focus
	adr	x0,pathbuf			; chemin : argument ou defaut
	adr	x1,s_default
	bl	tr1
	cbz	x19,.def
	mov	x1,x19
.def:	bl	str_cat
	bl	path_to_field
	bl	paint
	cbz	x19,.empty
	bl	do_open
	b	.go
.empty:	bl	set_title
.go:	bl	status

.loop:	mov	x0,1000
	bl	ui_event
	cbz	x0,.loop
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MENU
	b.eq	.menu
	cmp	x0,WEV_KEY
	b.ne	.widgets
	bl	on_key				; raccourcis du programme
	cbnz	x0,.loop
.widgets:
	mov	x0,0
	bl	ui_w_event
	cbz	x2,.loop
	mov	x19,x0
	mov	x20,x1
	adr	x0,w_path			; Entree dans le chemin : ouvrir
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.area
	cmp	x20,UN_ACTIVATE
	b.ne	.loop
	bl	field_to_path
	bl	do_open
	adr	x0,w_area
	ldr	x0,[x0]
	bl	ui_w_focus
	bl	status
	b	.loop
.area:	cmp	x20,UN_CHANGE			; le texte change : modifie
	b.ne	.st
	adr	x0,msg
	str	xzr,[x0]
	bl	mark_modified
.st:	bl	status				; (curseur deplace)
	b	.loop
.menu:	adr	x0,ui_ev			; choix dans la barre de menus
	ldr	w1,[x0,EV_CODE]
	bl	on_command
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; w1 = commande (menu ou raccourci)
on_command:
	prologue
	str	x19,[sp,-16]!
	mov	w19,w1
	cmp	w19,ID_FERMER
	b.eq	quit
	adr	x0,msg				; efface le message d'etat
	str	xzr,[x0]
	cmp	w19,ID_NOUVEAU
	b.ne	.m2
	bl	do_new
	b	.draw
.m2:	cmp	w19,ID_OUVRIR
	b.ne	.m3
	bl	field_to_path
	bl	do_open
	b	.draw
.m3:	cmp	w19,ID_ENREG
	b.ne	.m5
	bl	field_to_path
	bl	do_save
	b	.draw
.m5:	cmp	w19,ID_ASSEMBLER
	b.ne	.m6
	bl	field_to_path
	bl	do_assemble
	b	.draw
.m6:	cmp	w19,ID_PARCOURIR		; Parcourir : dialogue Ouvrir
	b.ne	.m7
	mov	x0,0
	adr	x1,dlgbuf
	mov	x2,256
	bl	ui_file_open
	cbz	x0,.draw
	bl	dlg_to_path
	bl	do_open
	b	.draw
.m7:	cmp	w19,ID_ENREG_SOUS		; Enregistrer sous : dialogue
	b.ne	.edit
	mov	x0,0
	adr	x1,pathbuf			; (nom propose : celui du champ)
	bl	base_name
	mov	x1,x0
	mov	x0,0
	adr	x2,dlgbuf
	mov	x3,256
	bl	ui_file_save
	cbz	x0,.draw
	bl	dlg_to_path
	bl	do_save
	b	.draw
.edit:	cmp	w19,ID_CHERCHER
	b.ne	.next
	adr	x0,s_find_t			; Rechercher : le mot
	adr	x1,s_find_q
	bl	tr1
	adr	x2,findbuf
	mov	x3,128
	bl	ui_ask
	cbz	x0,.draw
	b	.find
.next:	cmp	w19,ID_SUIVANT
	b.ne	.ed
.find:	adr	x0,w_area
	ldr	x0,[x0]
	adr	x1,findbuf
	bl	ui_textarea_find
	cbnz	x0,.draw
	adr	x0,s_notfound
	bl	tr0
	adr	x1,msg
	str	x0,[x1]
	b	.draw
.ed:	sub	w1,w19,ID_ANNULER-UTE_UNDO	; Annuler... Tout selectionner
	cmp	w1,UTE_UNDO
	b.lo	.draw
	cmp	w1,UTE_ALL
	b.hi	.draw
	adr	x0,w_area
	ldr	x0,[x0]
	bl	ui_textarea_edit
	cmp	x0,UN_CHANGE
	b.ne	.draw
	bl	mark_modified
.draw:	bl	status
	ldr	x19,[sp],16
	epilogue

; -> x0 = 1 si la touche (ui_ev) est un raccourci du programme (traite)
on_key:
	prologue
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.no
	ldr	w2,[x0,EV_CODE]
	ldr	w3,[x0,EV_MODS]
	mov	w1,ID_SUIVANT
	cmp	w2,0x3C				; F3 : suivant
	b.eq	.cmd
	mov	w4,0x11
	tst	w3,w4
	b.eq	.no
	mov	w1,ID_ENREG
	cmp	w2,0x16				; Ctrl+S
	b.eq	.cmd
	mov	w1,ID_OUVRIR
	cmp	w2,0x12				; Ctrl+O
	b.eq	.cmd
	mov	w1,ID_NOUVEAU
	cmp	w2,0x11				; Ctrl+N
	b.eq	.cmd
	mov	w1,ID_FERMER
	cmp	w2,0x1A				; Ctrl+W
	b.eq	.cmd
	mov	w1,ID_ASSEMBLER
	cmp	w2,0x05				; Ctrl+B
	b.eq	.cmd
	mov	w1,ID_CHERCHER
	cmp	w2,0x09				; Ctrl+F
	b.eq	.cmd
.no:	mov	x0,0
	b	.out
.cmd:	bl	on_command
	mov	x0,1
.out:	epilogue

; le chemin (pathbuf) dans son champ
path_to_field:
	prologue
	adr	x0,w_path
	ldr	x0,[x0]
	adr	x1,pathbuf
	bl	ui_w_set_text
	epilogue

; le champ dans pathbuf
field_to_path:
	prologue
	adr	x0,w_path
	ldr	x0,[x0]
	bl	ui_w_get_text
	mov	x1,x0
	adr	x0,pathbuf
	strb	wzr,[x0]
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	cbz	w3,.out
	add	x2,x2,1
	cmp	x2,255
	b.lo	.c
	strb	wzr,[x0,x2]
.out:	epilogue

; le chemin choisi dans un dialogue (dlgbuf) : pathbuf et le champ
dlg_to_path:
	prologue
	adr	x0,pathbuf
	strb	wzr,[x0]
	adr	x1,dlgbuf
	bl	str_cat
	bl	path_to_field
	epilogue

; x1 = chemin -> x0 = son dernier composant
base_name:
	mov	x0,x1
.n:	ldrb	w2,[x1],1
	cbz	w2,.r
	cmp	w2,'/'
	b.ne	.n
	mov	x0,x1
	b	.n
.r:	ret

mark_modified:
	adr	x0,modified
	ldr	x1,[x0]
	cbnz	x1,.r
	mov	x1,1
	str	x1,[x0]
	b	set_title
.r:	ret

; style du texte : couleurs du code pour un source (.asm, .inc)
set_style:
	prologue
	str	x19,[sp,-16]!
	mov	x19,AREA_FLAGS
	adr	x0,pathbuf
	adr	x1,s_ext_asm
	bl	str_ends
	cbnz	x0,.code
	adr	x0,pathbuf
	adr	x1,s_ext_inc
	bl	str_ends
	cbz	x0,.set
.code:	orr	x19,x19,UTA_CODE
.set:	adr	x0,w_area
	ldr	x0,[x0]
	mov	x1,x19
	bl	ui_w_style
	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Fichiers
; ---------------------------------------------------------------------------
do_new:
	prologue
	adr	x0,w_area
	ldr	x0,[x0]
	adr	x1,s_empty
	bl	ui_w_set_text
	adr	x0,modified
	str	xzr,[x0]
	adr	x0,has_file			; jamais enregistre
	str	xzr,[x0]
	adr	x0,pathbuf
	strb	wzr,[x0]
	adr	x1,s_default
	bl	tr_cat
	bl	path_to_field
	bl	set_style
	bl	set_title
	epilogue

do_open:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,16
	adr	x0,pathbuf
	mov	x1,sp
	sys	SYS_STAT
	cbnz	x0,.err
	ldr	x20,[sp]
	movl	x0,TCAP-1
	cmp	x20,x0
	b.hs	.big
	adr	x0,pathbuf
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x19,x0
	adr	x1,text
	ldr	x1,[x1]
	mov	x2,x20
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	tbnz	x20,63,.err_v
	adr	x0,text				; texte termine par NUL, au widget
	ldr	x0,[x0]
	strb	wzr,[x0,x20]
	bl	set_style
	adr	x0,w_area
	ldr	x0,[x0]
	adr	x1,text
	ldr	x1,[x1]
	bl	ui_w_set_text
	adr	x0,modified
	str	xzr,[x0]
	adr	x0,has_file
	mov	x1,1
	str	x1,[x0]
	adr	x0,s_opened
	bl	tr0
	b	.msg
.big:	adr	x0,s_toobig
	bl	tr0
	b	.msg
.err:	mov	x20,x0
.err_v:	adr	x0,has_file			; le chemin n'est pas un fichier lu
	str	xzr,[x0]
	mov	x0,x20
	bl	set_err
	b	.title
.msg:	adr	x1,msg
	str	x0,[x1]
.title:	bl	set_title
	add	sp,sp,16
	ldp	x19,x20,[sp],16
	epilogue

do_save:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,pathbuf
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x19,x0
	adr	x0,w_area
	ldr	x0,[x0]
	bl	ui_w_text_len
	mov	x20,x0
	adr	x0,w_area
	ldr	x0,[x0]
	bl	ui_w_get_text
	mov	x1,x0
	mov	x2,x20
	mov	x0,x19
	sys	SYS_WRITE
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	tbnz	x20,63,.errv
	adr	x0,modified
	str	xzr,[x0]
	adr	x0,has_file
	mov	x1,1
	str	x1,[x0]
	adr	x0,s_saved
	bl	tr0
	adr	x1,msg
	str	x0,[x1]
	bl	set_style
	bl	set_title
	b	.out
.errv:	mov	x0,x20
.err:	bl	set_err
.out:	ldp	x19,x20,[sp],16
	epilogue

; Fichier > Construire (Ctrl+B) : PiBuild, lance detache avec le chemin absolu
; du fichier (il prend son dossier, et le nom de ce dossier).
; Jamais enregistre : message ; modifie : enregistre d'abord.
do_assemble:
	prologue
	sub	sp,sp,800			; chemin absolu (520), dossier courant (256)
	adr	x0,has_file
	ldr	x0,[x0]
	cbz	x0,.nofile
	adr	x0,modified
	ldr	x0,[x0]
	cbz	x0,.abs
	bl	do_save
	adr	x0,modified			; echec : le message de do_save reste
	ldr	x0,[x0]
	cbnz	x0,.out
.abs:	strb	wzr,[sp]
	adr	x1,pathbuf			; "VOL:..." ou "/..." : deja absolu
	ldrb	w2,[x1]
	cmp	w2,'/'
	b.eq	.copy
	mov	x2,x1
.f:	ldrb	w3,[x2],1
	cbz	w3,.join
	cmp	w3,':'
	b.ne	.f
.copy:	mov	x0,sp
	bl	str_cat
	b	.spawn
.join:	add	x0,sp,528			; relatif : au dossier courant (comme
	mov	x1,256				; l'enregistrement)
	sys	SYS_GETCWD
	mov	x0,sp
	add	x1,sp,528
	adr	x2,pathbuf
	bl	path_join
.spawn:	adr	x0,s_asm_app
	mov	x1,sp
	sys	SYS_SPAWN
	tbnz	x0,63,.err
	adr	x0,s_asm_ok
	bl	tr0
	adr	x1,msg
	str	x0,[x1]
	b	.out
.err:	bl	set_err
	b	.out
.nofile:
	adr	x0,s_save_first
	bl	tr0
	adr	x1,msg
	str	x0,[x1]
.out:	add	sp,sp,800
	epilogue

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



; x4 = texte -> sa traduction ; les autres registres gardes

tr4:

	prologue

	str	x0,[sp,-16]!

	mov	x0,x4

	bl	tr0

	mov	x4,x0

	ldr	x0,[sp],16

	epilogue

; x0 = code d'erreur -> message d'etat "Erreur N"
set_err:
	prologue
	mov	x2,x0
	adr	x0,errbuf
	strb	wzr,[x0]
	adr	x1,s_error
	str	x2,[sp,-16]!
	bl	tr_cat
	ldr	x2,[sp],16
	mov	x1,x0
	neg	x0,x2
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,errbuf
	adr	x1,msg
	str	x0,[x1]
	epilogue

; titre : "Editeur - nom" (+ " *" si modifie)
set_title:
	prologue
	sub	sp,sp,304
	strb	wzr,[sp]
	mov	x0,sp
	adr	x1,s_title
	bl	tr_cat
	adr	x1,pathbuf			; nom = dernier composant
	str	x0,[sp,-16]!
	bl	base_name
	mov	x1,x0
	ldr	x0,[sp],16
	bl	str_cat
	adr	x1,modified
	ldr	x1,[x1]
	cbz	x1,.set
	adr	x1,s_star
	bl	str_cat
.set:	mov	x0,sp
	bl	ui_title
	add	sp,sp,304
	epilogue

; ---------------------------------------------------------------------------
;  Affichage
; ---------------------------------------------------------------------------

; fond, bandeau du champ chemin, widgets
; fonction de ui_on_resize (x0, x1 = taille) : le chemin et le texte suivent
on_resize:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,w_path
	ldr	x0,[x0]
	mov	x1,PATH_X
	mov	x2,6
	sub	x3,x19,PATH_X+8+FRAME_CAP_W
	mov	x4,28
	bl	ui_w_move
	adr	x0,w_area
	ldr	x0,[x0]
	mov	x1,4
	mov	x2,TB_H+2
	sub	x3,x19,8
	sub	x4,x20,TB_H+SB_H+6
	bl	ui_w_move
	bl	paint
	bl	status
	ldp	x19,x20,[sp],16
	epilogue

paint:
	prologue
	adr	x2,ui_w
	ldp	x2,x3,[x2]
	mov	x0,0
	mov	x1,0
	adr	x4,jetons
	ldr	x4,[x4]
	ldr	w4,[x4,TJ_FACE]
	bl	ui_fill
	mov	x0,0
	mov	x1,TB_H-1
	adr	x2,ui_w
	ldr	x2,[x2]
	mov	x3,1
	adr	x4,jetons
	ldr	x4,[x4]
	ldr	w4,[x4,TJ_LIGNE]
	bl	ui_fill
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	epilogue

; x0 = debut de ligne, x1 = position, x2 = texte -> x0 = colonne affichee
; (tabulations de TABW)
disp_col:
	mov	x3,0
.c:	cmp	x0,x1
	b.hs	.r
	ldrb	w4,[x2,x0]
	add	x0,x0,1
	cmp	w4,13
	b.eq	.c
	cmp	w4,9
	b.eq	.tab
	and	w4,w4,0xC0
	cmp	w4,0x80
	cinc	x3,x3,ne
	b	.c
.tab:	add	x3,x3,TABW
	and	x3,x3,not (TABW-1)
	b	.c
.r:	mov	x0,x3
	ret

; barre d'etat : "Ligne L, colonne C  -  N octets  -  modifie  -  message"
status:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,320
	adr	x2,ui_w
	ldp	x2,x1,[x2]
	sub	x1,x1,SB_H
	mov	x0,0
	mov	x3,SB_H
	adr	x4,jetons
	ldr	x4,[x4]
	ldr	w4,[x4,TJ_CREUX]
	bl	ui_fill
	adr	x0,w_area			; ligne, colonne (caracteres, tabulations)
	ldr	x0,[x0]
	bl	ui_textarea_caret
	mov	x19,x0				; ligne
	mov	x20,x1				; colonne en octets
	adr	x0,w_area
	ldr	x0,[x0]
	bl	ui_w_get_value			; curseur (octets)
	mov	x21,x0
	adr	x0,w_area
	ldr	x0,[x0]
	bl	ui_w_get_text
	mov	x2,x0
	sub	x0,x21,x20			; debut de la ligne
	mov	x1,x21
	bl	disp_col
	mov	x20,x0
	strb	wzr,[sp]
	mov	x0,sp
	adr	x1,s_line
	bl	tr_cat
	mov	x1,x0
	add	x0,x19,1
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x1,s_col
	bl	tr_cat
	mov	x1,x0
	add	x0,x20,1
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x1,s_sep
	bl	str_cat
	mov	x19,x0
	adr	x0,w_area
	ldr	x0,[x0]
	bl	ui_w_text_len
	mov	x1,x19
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x1,s_bytes
	bl	tr_cat
	adr	x1,modified
	ldr	x1,[x1]
	cbz	x1,.m
	adr	x1,s_modif
	bl	tr_cat
.m:	adr	x1,msg
	ldr	x1,[x1]
	cbz	x1,.st
	mov	x19,x1
	adr	x1,s_sep
	bl	str_cat
	mov	x1,x19
	bl	str_cat
.st:	adr	x6,ui_w
	ldp	x6,x2,[x6]
	sub	x2,x2,6
	sub	x6,x6,20
	mov	x0,sp
	mov	x1,10
	adr	x3,jetons
	ldr	x3,[x3]
	ldr	w3,[x3,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	bl	ui_text_fit
	adr	x2,ui_w
	ldp	x2,x1,[x2]
	sub	x1,x1,SB_H
	mov	x0,0
	mov	x3,SB_H
	bl	ui_update
	add	sp,sp,320
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue


segment readable writeable
kit_data

ui_data
		align 8
text		dd 0				; tampon de lecture d'un fichier
modified	dd 0
has_file	dd 0				; 1 : pathbuf a ete lu ou enregistre
msg		dd 0
jetons		dd 0
w_path		dd 0				; widgets : chemin, texte
w_area		dd 0
s_title0	db 0xC3,0x89,'diteur',0
s_title		db 0xC3,0x89,'diteur - ',0
s_star		db ' *',0
s_empty		db 0
s_default	db 'SD0:/sans titre.txt',0
s_ext_asm	db '.asm',0
s_ext_inc	db '.inc',0
menus		db 'MFichier',10
		db 'I1',9,'Nouveau',9,'Ctrl+N',10
		db 'I2',9,'Ouvrir le chemin',9,'Ctrl+O',10
		db 'I6',9,'Parcourir',0xE2,0x80,0xA6,10
		db 'I3',9,'Enregistrer',9,'Ctrl+S',10
		db 'I7',9,'Enregistrer sous',0xE2,0x80,0xA6,10
		db '-',10
		db 'I5',9,'Construire',9,'Ctrl+B',10
		db '-',10
		db 'I4',9,'Fermer',9,'Ctrl+W',10
		db 'M',0xC3,0x89,'dition',10
		db 'I10',9,'Annuler la saisie',9,'Ctrl+Z',10
		db 'I11',9,'R',0xC3,0xA9,'tablir la saisie',9,'Ctrl+Y',10
		db '-',10
		db 'I12',9,'Couper',9,'Ctrl+X',10
		db 'I13',9,'Copier',9,'Ctrl+C',10
		db 'I14',9,'Coller',9,'Ctrl+V',10
		db 'I15',9,'Tout s',0xC3,0xA9,'lectionner',9,'Ctrl+A',10
		db '-',10
		db 'I16',9,'Rechercher',0xE2,0x80,0xA6,9,'Ctrl+F',10
		db 'I17',9,'Suivant',9,'F3',10,0
s_line		db 'Ligne ',0
s_col		db ', colonne ',0
s_sep		db '   -   ',0
s_bytes		db ' octets',0
s_modif		db '   -   modifi',0xC3,0xA9,0
s_saved		db 'Enregistr',0xC3,0xA9,0
s_opened	db 'Fichier ouvert',0
s_save_first	db 'Enregistrez d',39,'abord le fichier',0
s_asm_ok	db 'PiBuild ouvert',0
s_asm_app	db 'SD0:/apps/builder.app/main',0
s_toobig	db 'Fichier trop gros',0
s_error		db 'Erreur -',0
s_find_t	db 'Rechercher',0
s_find_q	db 'Texte ',0xC3,0xA0,' chercher (sans tenir compte de la casse) :',0
s_notfound	db 'Introuvable',0
		align 8
errbuf		rb 32
pathbuf		rb 260
dlgbuf		rb 260
findbuf		rb 132
