; fichiers - navigateur de fichiers d'ARMPI
;
; A gauche, un panneau : les volumes montes (service vfs, relu a chaque
; montage ou demontage publie sur le bus, � korin.storage.volume.* �, sinon
; chaque seconde ; bouton d'ejection des cles USB) et les acces
; rapides (SD0:/etc/places.ini). A droite : en-tete (precedent, suivant,
; parent, fil d'Ariane, choix de la vue ; FRAME_CAP_W libres a droite),
; contenu du dossier courant (dossiers d'abord, tri par nom ; paquets .app
; avec leur icone et leur nom), ligne d'etat. Barre de menus globale.
; Ouvrir : dossier -> on y entre, .app -> son main, ELF -> lance, autre ->
; l'editeur. Clavier : fleches, Entree, Retour, Suppr, Ctrl+N, Ctrl+L, F5.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../inc/fs.inc'
include '../../inc/bus.inc'
include '../../kits/buskit/buskit.inc'

WIN_W		= 760
WIN_H		= 480
PAN_W		= 190			; panneau de gauche
PAN_Y0		= 30			; premiere ligne de volume
VROW_H		= 36			; ligne de volume (nom, genre)
PROW_H		= 28			; ligne d'acces rapide
PL_GAP		= 30			; en-tete des acces rapides
TOP_H		= 44			; en-tete
LIST_Y		= TOP_H + 3
NAV_X		= PAN_W + 8		; precedent, suivant, parent
NAV_Y		= 6
NAV_W		= 32
NAV_H		= 32
NAV_PX		= 16
SEG_W		= 28			; un segment icones / liste
SEG_RAIL	= 2*SEG_W + 4
SEG_Y		= 8
SEG_H		= 28
SEG_X_R		= FRAME_CAP_W + 8 + SEG_RAIL	; x du rail, depuis la droite
CR_X0		= NAV_X + 3*NAV_W + 10	; fil d'Ariane
CR_R		= SEG_X_R + 12		; sa fin, depuis la droite
CR_BASE		= 27			; ligne de base
CR_PX		= 14
CR_GAP		= 6			; de part et d'autre du separateur
CR_MAX		= 32
CR_SZ		= 32			; (decalage 5)
CR_TXT		= 0			; nom (dans crbuf)
CR_END		= 8			; sa fin dans path
CR_X		= 16			; x affiche (-1 : cache)
CR_W		= 24			; largeur affichee
HIST_MAX	= 16			; dossiers de l'historique
HIST_SZ		= 272
H_BACK		= 1			; elements de l'en-tete (head_at)
H_NEXT		= 2
H_UP		= 3
H_ICONS		= 4
H_LIST		= 5
H_CRUMB		= 8			; 8 + rang du segment
SB_H		= 24			; ligne d'etat
ROW_H		= 26
MAXE		= 4096		; (ents reste le dernier du .bss : seul son debut doit etre a portee d adr)
ENT_SZ		= 512			; (decalage 9)
EN_NAME		= 0			; nom (256 o)
EN_ICON		= 256			; paquet : nom sans .app (32 o)
EN_LABEL	= 288			; paquet : nom affiche (64 o)
EN_SIZE		= 352
EN_ATTR		= 360
EN_KIND		= 368
EN_TYPE		= 376			; « Dossier », « TXT »... (16 o)
K_FILE		= 0
K_DIR		= 1
K_APP		= 2
VMAX		= 8
V_SZ		= 64			; (decalage 6)
V_DISP		= 0			; « USB0: »
V_LOW		= 16			; « usb0 »
V_SRC		= 32			; « blk.usb0p1 »
V_REM		= 56			; 1 : amovible
VE_SZ		= 56			; entree de VFS_VOLUMES
PL_MAX		= 10
PL_SZ		= 128			; (decalage 7)
PL_LAB		= 0
PL_PATH		= 48
PL_BUF		= 2048
DCLICK_MS	= 600
POLL_MS		= 1000

; codes HID
KEY_N		= 0x11
KEY_L		= 0x0F
KEY_W		= 0x1A
KEY_F5		= 0x3E
KEY_KPENT	= 0x58
MOD_CTRL	= 0x11

; entrees des menus
ID_OPEN		= 1
ID_NEWDIR	= 2
ID_DELETE	= 3
ID_REFRESH	= 4
ID_CLOSE	= 5
ID_OPENWITH	= 6
ID_PARENT	= 10
ID_SD0		= 11
ID_PLACE	= 20			; 20 + rang de l'acces rapide
ID_EJECT	= 30
ID_ICONS	= 40
ID_LIST		= 41

HDR_H		= 24			; en-tete des colonnes (vue liste)
CELL_W		= 96			; case de la vue icones
CELL_H		= 88
ICON_S		= 48
COL_SIZE_W	= 90
COL_TYPE_W	= 90
COL_MIN		= 50			; Taille, Type : largeur minimale
NAME_MIN	= 120			; Nom : largeur minimale
SEP_HIT		= 6			; prise d'un separateur (+-)
S_NAME		= 0			; tri : colonne
S_SIZE		= 1
S_TYPE		= 2

COL_FOLDER	= 0x00E8B64C
COL_FOLDER_D	= 0x00C99A35

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so
include '../../inc/applist.inc'				; ini_read, ini_trim, str_copy_n

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	bl	ui_arg				; (un chemin entre guillemets : UIKit)
	mov	x19,x0
	adr	x0,ui_win
	bl	ui_init
	mov	x20,WIN_W			; taille : bornee par l'ecran
	mov	x21,WIN_H
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,.win
	adr	x0,scr
	ldp	x1,x2,[x0]
	ldp	x3,x4,[x0,16]
	sub	x1,x1,40
	cmp	x20,x1
	csel	x20,x1,x20,gt
	sub	x2,x2,x3
	sub	x2,x2,x4
	sub	x2,x2,FRAME_TITLE_H+FRAME_BORDER+24
	cmp	x21,x2
	csel	x21,x2,x21,gt
	mov	x0,480
	cmp	x20,x0
	csel	x20,x0,x20,lt
	mov	x0,300
	cmp	x21,x0
	csel	x21,x0,x21,lt
.win:	mov	x0,-1
	mov	x1,-1
	mov	x2,x20
	mov	x3,x21
	adr	x4,s_title
	bl	tr4
	mov	x5,WF_RESIZABLE
	bl	ui_window_ex
	tbnz	x0,63,quit
	mov	x0,480
	mov	x1,300
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	bl	places_load
	bl	menu_build
	adr	x0,menu_buf			; barre de menus globale
	bl	ui_menu
	adr	x0,s_cfg			; vue choisie (SD0:/etc/files.ini)
	adr	x1,cfg_line
	bl	ini_read
	bl	cols_fix
	bl	layout
	adr	x0,path				; dossier initial : argument ou SD0:/
	adr	x1,s_sd
	cbz	x19,.def
	ldrb	w2,[x19]
	cbz	w2,.def
	mov	x1,x19
.def:	mov	x2,256
	bl	str_copy_n
	adr	x0,s_vfs
	mov	x1,500
	sys	SYS_SVC_OPEN
	adr	x1,vfs_ch
	str	x0,[x1]
	bl	load_dir
	adr	x0,s_topic_vols			; montages et demontages : le bus
	mov	x1,0
	bl	bus_subscribe
	cmp	x0,0
	csel	x0,x0,xzr,gt
	adr	x1,vol_sub
	str	x0,[x1]
	sys	SYS_TIME_MS
	adr	x1,last_poll
	str	x0,[x1]
	bl	vol_poll
	bl	draw

main_loop:
	mov	x0,500
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_RESIZE
	b.ne	.ms
	mov	x0,0				; (le kit prend la taille, puis on_resize)
	bl	ui_w_event
	b	.tick
.ms:	cmp	x0,WEV_MOUSE
	b.ne	.k
	bl	on_mouse
	b	.tick
.k:	cmp	x0,WEV_KEY
	b.ne	.m
	bl	on_key
	b	.tick
.m:	cmp	x0,WEV_MENU
	b.ne	.w
	adr	x0,ui_ev
	ldr	w0,[x0,EV_CODE]
	bl	on_menu
	b	.tick
.w:	cmp	x0,WEV_BUS
	b.ne	.wh
	adr	x0,ui_ev			; un volume monte ou demonte
	ldr	w0,[x0,BEV_SUB]
	adr	x1,vol_sub
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.tick
	bl	vol_poll
	cbz	x0,.tick
	bl	draw
	b	.tick
.wh:	cmp	x0,WEV_WHEEL
	b.ne	.tick
	adr	x0,ui_ev
	ldrsw	x1,[x0,EV_Y]
	adr	x2,view				; 3 lignes (liste), 1 rangee (icones)
	ldr	x2,[x2]
	mov	x4,3
	mov	x5,1
	cmp	x2,0
	csel	x2,x5,x4,eq
	mul	x1,x1,x2
	adr	x2,scroll
	ldr	x3,[x2]
	sub	x3,x3,x1
	bl	set_scroll
	bl	draw
.tick:	adr	x0,vol_sub			; sans le bus : les volumes environ
	ldr	x0,[x0]				; chaque seconde
	cbnz	x0,main_loop
	sys	SYS_TIME_MS
	adr	x1,last_poll
	ldr	x2,[x1]
	sub	x3,x0,x2
	cmp	x3,POLL_MS
	b.lo	main_loop
	str	x0,[x1]
	bl	vol_poll
	cbz	x0,main_loop
	bl	draw
	b	main_loop

; fonction de ui_on_resize : colonnes, rangees et dessin suivent la fenetre
on_resize:
	prologue
	bl	cols_fix
	bl	layout
	bl	keep_visible
	bl	draw
	epilogue

quit:	mov	x0,0
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Outils
; ---------------------------------------------------------------------------

; x0, x1 = chaines -> x0 < 0, 0, > 0 (casse ASCII ignoree)
str_icmp:
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
	b.ne	.d
	cbnz	w2,.c
	mov	x0,0
	ret
.d:	sub	x0,x2,x3
	ret

; x0 = chaine, x1 = prefixe -> x0 = 1 si la chaine commence par le prefixe
str_pre:
.c:	ldrb	w3,[x1],1
	cbz	w3,.yes
	ldrb	w2,[x0],1
	cmp	w2,w3
	b.eq	.c
	mov	x0,0
	ret
.yes:	mov	x0,1
	ret

; x3 = defilement voulu : borne et enregistre
set_scroll:
	adr	x4,nents
	ldr	x4,[x4]
	adr	x5,cols				; lignes de la vue
	ldr	x5,[x5]
	add	x4,x4,x5
	sub	x4,x4,1
	udiv	x4,x4,x5
	adr	x5,rows
	ldr	x5,[x5]
	sub	x4,x4,x5
	cmp	x4,0
	csel	x4,xzr,x4,lt
	cmp	x3,x4
	csel	x3,x4,x3,gt
	cmp	x3,0
	csel	x3,xzr,x3,lt
	adr	x2,scroll
	str	x3,[x2]
	ret

; la selection reste visible
keep_visible:
	adr	x0,cursel
	ldr	x0,[x0]
	tbnz	x0,63,.r
	adr	x1,cols				; sa ligne
	ldr	x1,[x1]
	udiv	x0,x0,x1
	adr	x1,scroll
	ldr	x2,[x1]
	adr	x3,rows
	ldr	x3,[x3]
	cmp	x0,x2
	b.ge	.b
	str	x0,[x1]
	ret
.b:	add	x4,x2,x3
	cmp	x0,x4
	b.lt	.r
	sub	x0,x0,x3
	add	x0,x0,1
	str	x0,[x1]
.r:	ret

; x0 = rang affiche -> x0 = entree
ent_ptr:
	adr	x1,order
	ldr	x0,[x1,x0,lsl 3]
	adr	x1,ents
	add	x0,x1,x0,lsl 9
	ret

; x0 = rang affiche -> chemin complet dans fullpath (x0 = sa fin)
make_full:
	prologue
	bl	ent_ptr
	mov	x2,x0
	adr	x0,fullpath
	adr	x1,path
	bl	path_join
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

; x0 = code d'erreur -> message d'etat
set_err:
	prologue
	mov	x2,x0
	adr	x1,s_err_dir
	cmn	x2,39
	b.eq	.fixed
	adr	x1,s_err_noent
	cmn	x2,2
	b.eq	.fixed
	str	x2,[sp,-16]!
	adr	x0,errbuf
	adr	x1,s_error
	bl	tr_cat
	ldr	x2,[sp],16
	mov	x1,x0
	neg	x0,x2
	bl	fmt_dec
	adr	x1,errbuf
.fixed:	mov	x0,x1
	bl	ui_tr
	adr	x1,msg
	str	x0,[x1]
	epilogue

; x0 = message d'etat (chaine fixe)
set_msg:
	prologue
	bl	ui_tr
	adr	x1,msg
	str	x0,[x1]
	epilogue

; x0 = octets, x1 = tampon : "123 o", "12 Ko", "3 Mo" -> x0 = fin
fmt_size:
	prologue
	adr	x2,s_o
	mov	x3,10240
	cmp	x0,x3
	b.lo	.f
	lsr	x0,x0,10
	adr	x2,s_ko
	cmp	x0,x3
	b.lo	.f
	lsr	x0,x0,10
	adr	x2,s_mo
.f:	str	x2,[sp,-16]!
	bl	fmt_dec
	ldr	x1,[sp],16
	bl	tr_cat
	epilogue

; ---------------------------------------------------------------------------
;  Lecture du dossier (dossiers d'abord, puis par nom)
; ---------------------------------------------------------------------------
load_dir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	hist_note
	adr	x0,nents
	str	xzr,[x0]
	adr	x0,scroll
	str	xzr,[x0]
	mov	x1,-1
	adr	x0,cursel
	str	x1,[x0]
	adr	x0,last_idx
	str	x1,[x0]
	adr	x0,msg
	str	xzr,[x0]
	adr	x0,path
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x19,x0
	mov	x20,0				; entrees
.rd:	cmp	x20,MAXE
	b.hs	.close
	mov	x0,x19
	adr	x1,dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.close
	adr	x0,dirent			; caches : « .xxx » (et . ..)
	ldrb	w1,[x0]
	cbz	w1,.rd
	cmp	w1,'.'
	b.eq	.rd
	adr	x0,ents
	add	x21,x0,x20,lsl 9
	mov	x0,x21
	adr	x1,dirent
	mov	x2,256
	bl	str_copy_n
	adr	x1,dirent
	ldr	x2,[x1,DIRENT_SIZE]
	str	x2,[x21,EN_SIZE]
	ldr	x2,[x1,DIRENT_ATTR]
	str	x2,[x21,EN_ATTR]
	mov	x3,K_FILE
	tst	x2,ATTR_DIR
	b.eq	.kind
	mov	x0,x21				; paquet .app ?
	adr	x1,s_app
	bl	str_ends
	mov	x3,K_DIR
	cbz	x0,.kind
	bl	app_info
	mov	x3,K_APP
.kind:	str	x3,[x21,EN_KIND]
	bl	set_type
	adr	x0,order
	str	x20,[x0,x20,lsl 3]
	add	x20,x20,1
	b	.rd
.close:	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,nents
	str	x20,[x0]
	bl	sort_ents
	b	.out
.err:	bl	set_err
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (load_dir) x21 = entree d'un paquet : icone (nom sans .app) et nom affiche
; (« name = » de son app.txt)
app_info:
	prologue
	mov	x0,x21
	bl	str_len
	sub	x2,x0,4
	cmp	x2,31
	mov	x3,31
	csel	x2,x3,x2,gt
	add	x0,x21,EN_ICON
	mov	x1,x21
.c:	cbz	x2,.e
	ldrb	w3,[x1],1
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c
.e:	strb	wzr,[x0]
	add	x0,x21,EN_LABEL
	add	x1,x21,EN_ICON
	mov	x2,64
	bl	str_copy_n
	adr	x0,fullpath			; <dossier>/<nom>.app/app.txt
	adr	x1,path
	mov	x2,x21
	bl	path_join
	adr	x1,s_apptxt
	bl	str_cat
	adr	x0,cur_ent
	str	x21,[x0]
	adr	x0,fullpath
	adr	x1,man_line
	bl	ini_read
	epilogue

; (ini_read) x0 = cle, x1 = valeur : « name » -> nom affiche du paquet
man_line:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	adr	x1,k_name
	bl	str_eq
	cbz	x0,.out
	adr	x0,cur_ent
	ldr	x0,[x0]
	add	x0,x0,EN_LABEL
	mov	x1,x19
	mov	x2,64
	bl	str_copy_n
.out:	ldr	x19,[sp],16
	epilogue

; x0, x1 = numeros d'entrees -> x0 < 0 si x0 passe avant (sort_key,
; sort_desc ; dossiers d'abord, sauf tri par type)
ent_cmp:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x2,ents
	add	x19,x2,x0,lsl 9
	add	x20,x2,x1,lsl 9
	adr	x2,sort_key
	ldr	x2,[x2]
	cmp	x2,S_TYPE
	b.eq	.type
	ldr	x3,[x19,EN_ATTR]
	and	x3,x3,ATTR_DIR
	ldr	x4,[x20,EN_ATTR]
	and	x4,x4,ATTR_DIR
	cmp	x3,x4
	b.eq	.same
	mov	x0,1
	mov	x1,-1
	cmp	x3,0
	csel	x0,x1,x0,ne
	b	.out
.same:	cmp	x2,S_SIZE
	b.ne	.name
	ldr	x3,[x19,EN_SIZE]
	ldr	x4,[x20,EN_SIZE]
	cmp	x3,x4
	b.eq	.name
	mov	x0,1
	mov	x1,-1
	csel	x0,x1,x0,lo
	b	.dir
.type:	add	x0,x19,EN_TYPE
	add	x1,x20,EN_TYPE
	bl	str_icmp
	cbnz	x0,.dir
.name:	mov	x0,x19
	mov	x1,x20
	bl	str_icmp
.dir:	adr	x1,sort_desc
	ldr	x1,[x1]
	cbz	x1,.out
	neg	x0,x0
.out:	ldp	x19,x20,[sp],16
	epilogue

; (load_dir) x21 = entree : son type affiche
set_type:
	prologue
	ldr	x1,[x21,EN_KIND]
	add	x0,x21,EN_TYPE
	adr	x2,s_t_dir
	cmp	x1,K_DIR
	b.eq	.cp
	adr	x2,s_t_app
	cmp	x1,K_APP
	b.eq	.cp
	mov	x3,0				; apres le dernier « . » (pas en tete)
	add	x4,x21,1
.f:	ldrb	w5,[x4],1
	cbz	w5,.fe
	cmp	w5,'.'
	b.ne	.f
	mov	x3,x4
	b	.f
.fe:	adr	x2,s_t_file
	cbz	x3,.cp
	mov	x5,0				; 1 a 8 caracteres
.l:	ldrb	w6,[x3,x5]
	cbz	w6,.le
	add	x5,x5,1
	cmp	x5,8
	b.hi	.cp
	b	.l
.le:	cbz	x5,.cp
	mov	x5,0
.u:	ldrb	w6,[x3,x5]
	cbz	w6,.ue
	sub	w7,w6,'a'
	cmp	w7,25
	b.hi	.s
	sub	w6,w6,32
.s:	strb	w6,[x0,x5]
	add	x5,x5,1
	b	.u
.ue:	strb	wzr,[x0,x5]
	b	.out
.cp:	bl	tr2
	mov	x1,x2
	mov	x2,16
	bl	str_copy_n
.out:	epilogue

; retrier en gardant la selection
resort:
	prologue
	str	x19,[sp,-16]!
	mov	x19,-1
	adr	x0,cursel
	ldr	x0,[x0]
	tbnz	x0,63,.s
	adr	x1,order
	ldr	x19,[x1,x0,lsl 3]
.s:	bl	sort_ents
	tbnz	x19,63,.out
	mov	x0,0
	adr	x1,order
	adr	x2,nents
	ldr	x2,[x2]
.f:	cmp	x0,x2
	b.hs	.out
	ldr	x3,[x1,x0,lsl 3]
	cmp	x3,x19
	b.eq	.found
	add	x0,x0,1
	b	.f
.found:	adr	x1,cursel
	str	x0,[x1]
	bl	keep_visible
.out:	ldr	x19,[sp],16
	epilogue

; x0 = x du clic dans l'en-tete : tri par cette colonne (deuxieme clic :
; ordre inverse)
header_click:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	col_x
	mov	x2,S_NAME
	cmp	x19,x0
	b.lt	.k
	mov	x2,S_SIZE
	cmp	x19,x1
	b.lt	.k
	mov	x2,S_TYPE
.k:	adr	x3,sort_key
	ldr	x4,[x3]
	adr	x5,sort_desc
	cmp	x4,x2
	b.ne	.new
	ldr	x6,[x5]
	eor	x6,x6,1
	str	x6,[x5]
	b	.s
.new:	str	x2,[x3]
	str	xzr,[x5]
.s:	bl	resort
	ldr	x19,[sp],16
	epilogue

; -> x0 = x de la colonne Taille, x1 = x de la colonne Type
col_x:
	adr	x0,ui_w
	ldr	x0,[x0]
	adr	x2,w_type
	ldr	x2,[x2]
	sub	x1,x0,x2
	sub	x1,x1,6
	adr	x2,w_size
	ldr	x2,[x2]
	sub	x0,x1,x2
	ret

; largeurs lues : bornees (sinon celles par defaut)
cols_fix:
	adr	x0,w_size
	adr	x1,w_type
	ldr	x2,[x0]
	ldr	x3,[x1]
	cmp	x2,COL_MIN
	b.lt	.def
	cmp	x3,COL_MIN
	b.lt	.def
	adr	x4,ui_w				; Nom garde sa largeur minimale ?
	ldr	x4,[x4]
	sub	x4,x4,x2
	sub	x4,x4,x3
	sub	x4,x4,6+PAN_W+NAME_MIN
	tbz	x4,63,.ok
.def:	mov	x2,COL_SIZE_W
	str	x2,[x0]
	mov	x3,COL_TYPE_W
	str	x3,[x1]
.ok:	ret

; x0 = x, x1 = y -> x0 = separateur sous la souris (1 avant Taille, 2 avant
; Type, 0 aucun ; vue liste, en-tete)
sep_at:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,view
	ldr	x0,[x0]
	cbz	x0,.none
	cmp	x20,LIST_Y
	b.lt	.none
	cmp	x20,LIST_Y+HDR_H
	b.ge	.none
	bl	col_x
	sub	x2,x19,x0
	cmp	x2,0
	cneg	x2,x2,lt
	cmp	x2,SEP_HIT
	b.le	.s1
	sub	x2,x19,x1
	cmp	x2,0
	cneg	x2,x2,lt
	cmp	x2,SEP_HIT
	b.le	.s2
.none:	mov	x0,0
	b	.out
.s1:	mov	x0,1
	b	.out
.s2:	mov	x0,2
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = x de la souris : deplace le separateur saisi (drag) -> x0 = 1 si
; les largeurs ont change
drag_to:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	col_x				; x0 = Taille, x1 = Type
	adr	x2,ui_w
	ldr	x2,[x2]
	adr	x3,drag
	ldr	x3,[x3]
	cmp	x3,1
	b.ne	.t
	mov	x4,PAN_W+NAME_MIN		; avant Taille : Type ne bouge pas
	sub	x5,x1,COL_MIN
	bl	.clamp
	sub	x6,x1,x19			; largeur de Taille
	adr	x4,w_size
	ldr	x5,[x4]
	str	x6,[x4]
	cmp	x5,x6
	cset	x0,ne
	b	.out
.t:	add	x4,x0,COL_MIN			; avant Type : Taille ne bouge pas a gauche
	sub	x5,x2,6+COL_MIN
	bl	.clamp
	sub	x6,x19,x0			; largeur de Taille
	sub	x7,x2,6
	sub	x7,x7,x19			; largeur de Type
	adr	x4,w_size
	adr	x5,w_type
	ldr	x8,[x5]
	str	x6,[x4]
	str	x7,[x5]
	cmp	x8,x7
	cset	x0,ne
	b	.out
.clamp:	cmp	x19,x5				; (x19 dans [x4, x5])
	csel	x19,x5,x19,gt
	cmp	x19,x4
	csel	x19,x4,x19,lt
	ret
.out:	ldr	x19,[sp],16
	epilogue

; souris hors appui : survol (separateurs, en-tete), glisser, relache
mouse_aux:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,ui_ev
	ldr	w19,[x0,EV_KIND]
	ldrsw	x20,[x0,EV_X]
	ldr	w2,[x0,EV_CODE]			; boutons
	adr	x3,drag
	ldr	x3,[x3]
	cbz	x3,.hover
	cmp	w19,2
	b.eq	.end
	cbz	w2,.end
	mov	x0,x20
	bl	drag_to
	cbz	x0,.out
	bl	draw
	b	.out
.end:	adr	x3,drag
	str	xzr,[x3]
	bl	save_cfg
	bl	draw
	b	.out
.hover:	cbnz	w19,.out
	adr	x0,ui_ev
	ldrsw	x1,[x0,EV_Y]
	mov	x0,x20
	bl	sep_at
	adr	x1,hover
	ldr	x2,[x1]
	cmp	x0,x2
	cset	x21,ne
	str	x0,[x1]
	adr	x0,ui_ev
	ldrsw	x1,[x0,EV_Y]
	mov	x0,x20
	bl	head_at
	adr	x1,hhot
	ldr	x2,[x1]
	cmp	x0,x2
	b.eq	.h1
	mov	x21,1
	str	x0,[x1]
.h1:	cbz	x21,.out
	bl	draw
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; SD0:/etc/files.ini : « vue = ... », « taille = ... », « type = ... »
save_cfg:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,cfgbuf
	adr	x1,s_cfg_ic
	adr	x2,view
	ldr	x2,[x2]
	cbz	x2,.v
	adr	x1,s_cfg_li
.v:	bl	str_cat
	adr	x1,s_cfg_ta
	bl	str_cat
	mov	x1,x0
	adr	x0,w_size
	ldr	x0,[x0]
	bl	fmt_dec
	adr	x1,s_cfg_ty
	bl	str_cat
	mov	x1,x0
	adr	x0,w_type
	ldr	x0,[x0]
	bl	fmt_dec
	mov	w1,10
	strb	w1,[x0],1
	strb	wzr,[x0]
	adr	x1,cfgbuf
	sub	x20,x0,x1			; octets
	adr	x0,s_cfg
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,cfgbuf
	mov	x2,x20
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = x de la premiere colonne de la grille (centree)
grid_x0:
	adr	x0,ui_w
	ldr	x0,[x0]
	sub	x0,x0,PAN_W
	adr	x1,cols
	ldr	x1,[x1]
	mov	x2,CELL_W
	mul	x1,x1,x2
	sub	x0,x0,x1
	asr	x0,x0,1
	add	x0,x0,PAN_W
	ret

; disposition de la vue : cols, body_y, line_h, rows
layout:
	adr	x0,ui_w
	ldp	x1,x2,[x0]
	adr	x3,view
	ldr	x3,[x3]
	cbnz	x3,.list
	sub	x4,x1,PAN_W+8
	mov	x5,CELL_W
	udiv	x4,x4,x5
	mov	x5,1
	cmp	x4,1
	csel	x4,x5,x4,lt
	mov	x6,LIST_Y+4
	mov	x7,CELL_H
	b	.set
.list:	mov	x4,1
	mov	x6,LIST_Y+HDR_H
	mov	x7,ROW_H
.set:	sub	x8,x2,SB_H
	sub	x8,x8,x6
	udiv	x8,x8,x7
	mov	x9,1
	cmp	x8,1
	csel	x8,x9,x8,lt
	adr	x0,cols
	str	x4,[x0]
	adr	x0,body_y
	str	x6,[x0]
	adr	x0,line_h
	str	x7,[x0]
	adr	x0,rows
	str	x8,[x0]
	ret

; x0 = vue (0 icones, 1 liste) : la prend et l'enregistre
set_view:
	prologue
	str	x19,[sp,-16]!
	adr	x1,view
	str	x0,[x1]
	bl	layout
	adr	x0,scroll
	str	xzr,[x0]
	bl	keep_visible
	bl	save_cfg
	ldr	x19,[sp],16
	epilogue

; (ini_read de files.ini) x0 = cle, x1 = valeur : « vue = liste »,
; « taille = <px> », « type = <px> »
cfg_line:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x0
	adr	x1,k_vue
	bl	str_eq
	cbz	x0,.ta
	mov	x0,x19
	adr	x1,s_liste
	bl	str_eq
	adr	x1,view
	str	x0,[x1]
	b	.out
.ta:	mov	x0,x20
	adr	x1,k_taille
	bl	str_eq
	cbz	x0,.ty
	mov	x0,x19
	bl	parse_dec
	adr	x1,w_size
	str	x0,[x1]
	b	.out
.ty:	mov	x0,x20
	adr	x1,k_type
	bl	str_eq
	cbz	x0,.out
	mov	x0,x19
	bl	parse_dec
	adr	x1,w_type
	str	x0,[x1]
.out:	ldp	x19,x20,[sp],16
	epilogue

; tri par insertion de order[nents]
sort_ents:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x19,order
	adr	x0,nents
	ldr	x20,[x0]
	mov	x21,1
.i:	cmp	x21,x20
	b.hs	.done
	ldr	x22,[x19,x21,lsl 3]
	mov	x23,x21
.j:	cbz	x23,.put
	sub	x0,x23,1
	ldr	x24,[x19,x0,lsl 3]
	mov	x0,x24
	mov	x1,x22
	bl	ent_cmp
	cmp	x0,0
	b.le	.put
	str	x24,[x19,x23,lsl 3]
	sub	x23,x23,1
	b	.j
.put:	str	x22,[x19,x23,lsl 3]
	add	x21,x21,1
	b	.i
.done:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom : le selectionne s'il est dans la liste
select_name:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.e:	adr	x0,nents
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.out
	mov	x0,x20
	bl	ent_ptr
	mov	x1,x19
	bl	str_eq
	cbnz	x0,.found
	add	x20,x20,1
	b	.e
.found:	adr	x0,cursel
	str	x20,[x0]
	bl	keep_visible
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin : y aller
go_dir:
	prologue
	mov	x1,x0
	adr	x0,path
	mov	x2,256
	bl	str_copy_n
	bl	load_dir
	epilogue

go_sd0:
	adr	x0,s_sd
	b	go_dir

; ---------------------------------------------------------------------------
;  Historique (hist[0..h_n[, h_pos = dossier courant) : load_dir y note tout
;  changement de dossier, hist_go ne fait que deplacer h_pos
; ---------------------------------------------------------------------------

; x0 = rang -> x0 = entree
hist_ptr:
	mov	x1,HIST_SZ
	adr	x2,hist
	madd	x0,x0,x1,x2
	ret

; path a change ? il devient la derniere entree (la suite est perdue ; pleine,
; la plus ancienne sort)
hist_note:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,h_pos
	ldr	x20,[x19]
	tbnz	x20,63,.add
	mov	x0,x20
	bl	hist_ptr
	adr	x1,path
	bl	str_eq
	cbnz	x0,.out
.add:	add	x20,x20,1
	cmp	x20,HIST_MAX
	b.lo	.put
	mov	x20,HIST_MAX-1
	adr	x0,hist
	add	x1,x0,HIST_SZ
	mov	x2,(HIST_MAX-1)*HIST_SZ
.mv:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	sub	x2,x2,1
	cbnz	x2,.mv
.put:	str	x20,[x19]
	add	x0,x20,1
	adr	x1,h_n
	str	x0,[x1]
	mov	x0,x20
	bl	hist_ptr
	adr	x1,path
	mov	x2,HIST_SZ
	bl	str_copy_n
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = -1 precedent, +1 suivant
hist_go:
	prologue
	adr	x1,h_pos
	ldr	x2,[x1]
	add	x2,x2,x0
	tbnz	x2,63,.out
	adr	x3,h_n
	ldr	x3,[x3]
	cmp	x2,x3
	b.hs	.out
	str	x2,[x1]
	mov	x0,x2
	bl	hist_ptr
	mov	x1,x0
	adr	x0,path
	mov	x2,256
	bl	str_copy_n
	bl	load_dir
.out:	epilogue

; ---------------------------------------------------------------------------
;  Acces rapides (SD0:/etc/places.ini, « libelle = dossier », dans l'ordre ;
;  seulement les dossiers presents)
; ---------------------------------------------------------------------------
places_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,pl_n
	str	xzr,[x0]
	adr	x0,s_placesf
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,pl_buf
	mov	x2,PL_BUF-1
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	csel	x20,xzr,x20,lt
	adr	x19,pl_buf
	strb	wzr,[x19,x20]
.line:	ldrb	w0,[x19]
	cbz	w0,.out
	mov	x20,x19				; fin de ligne ; « # » : commentaire
.eol:	ldrb	w0,[x20]
	cbz	w0,.cut
	cmp	w0,10
	b.eq	.cut
	cmp	w0,'#'
	b.ne	.e1
	strb	wzr,[x20]
.e1:	add	x20,x20,1
	b	.eol
.cut:	ldrb	w22,[x20]
	strb	wzr,[x20]
	mov	x0,x19
.eq:	ldrb	w1,[x0]
	cbz	w1,.next
	cmp	w1,'='
	b.eq	.split
	add	x0,x0,1
	b	.eq
.split:	strb	wzr,[x0]
	add	x23,x0,1
	mov	x0,x19
	bl	ini_trim
	mov	x24,x0				; libelle (casse gardee)
	mov	x0,x23
	bl	ini_trim
	mov	x23,x0				; dossier
	ldrb	w0,[x24]
	cbz	w0,.next
	ldrb	w0,[x23]
	cbz	w0,.next
	mov	x0,x23				; present, et un dossier ?
	adr	x1,statbuf
	sys	SYS_STAT
	cbnz	x0,.next
	adr	x0,statbuf
	ldr	x0,[x0,8]
	tst	x0,ATTR_DIR
	b.eq	.next
	adr	x0,pl_n
	ldr	x21,[x0]
	cmp	x21,PL_MAX
	b.hs	.out
	add	x1,x21,1
	str	x1,[x0]
	adr	x0,places
	add	x21,x0,x21,lsl 7
	mov	x0,x21
	mov	x1,x24
	mov	x2,PL_PATH-PL_LAB
	bl	str_copy_n
	add	x0,x21,PL_PATH
	mov	x1,x23
	mov	x2,PL_SZ-PL_PATH
	bl	str_copy_n
.next:	cbz	w22,.out
	add	x19,x20,1
	b	.line
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; texte des menus : Fichier, Aller (avec les acces rapides), Volume
menu_build:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,menu_buf
	adr	x1,m_head
	bl	str_cat
	mov	x19,x0
	mov	x20,0
.p:	adr	x0,pl_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.tail
	mov	x0,x19				; « I<id>\t<libelle>\n »
	adr	x1,s_mi
	bl	str_cat
	mov	x1,x0
	add	x0,x20,ID_PLACE
	bl	fmt_dec
	mov	w1,9
	strb	w1,[x0],1
	adr	x1,places
	add	x1,x1,x20,lsl 7
	bl	str_cat
	mov	w1,10
	strb	w1,[x0],1
	strb	wzr,[x0]
	mov	x19,x0
	add	x20,x20,1
	b	.p
.tail:	adr	x1,m_tail
	cbnz	x20,.t
	add	x1,x1,2				; (pas d'acces rapide : pas de separateur)
.t:	mov	x0,x19
	bl	str_cat
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Volumes (service vfs : VFS_VOLUMES, VFS_DEMONTER)
; ---------------------------------------------------------------------------

; relit la liste -> x0 = 1 si elle a change (table vols refaite)
vol_poll:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,vfs_ch
	ldr	x0,[x0]
	tbz	x0,63,.call
	adr	x0,s_vfs			; service pas encore ouvert
	mov	x1,0
	sys	SYS_SVC_OPEN
	adr	x1,vfs_ch
	str	x0,[x1]
	tbnz	x0,63,.none
.call:	adr	x1,vmsg
	mov	x2,VFS_VOLUMES
	str	x2,[x1]
	str	xzr,[x1,MSG_B]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.fail
	adr	x1,vmsg
	ldr	x19,[x1,MSG_B]
	cmp	x19,VMAX
	mov	x2,VMAX
	csel	x19,x2,x19,hi
	b	.cmp
.fail:	mov	x0,-1
	adr	x1,vfs_ch
	str	x0,[x1]
.none:	mov	x19,0
.cmp:	adr	x0,vraw_n			; meme liste ?
	ldr	x1,[x0]
	cmp	x1,x19
	b.ne	.changed
	adr	x2,vmsg
	add	x2,x2,MSG_DATA
	adr	x3,vraw
	mov	x4,VE_SZ
	mul	x4,x4,x19
.c:	cbz	x4,.same
	ldrb	w5,[x2],1
	ldrb	w6,[x3],1
	cmp	w5,w6
	b.ne	.changed
	sub	x4,x4,1
	b	.c
.same:	mov	x0,0
	b	.out
.changed:
	adr	x0,vraw_n
	str	x19,[x0]
	adr	x2,vmsg
	add	x2,x2,MSG_DATA
	adr	x3,vraw
	mov	x4,VE_SZ
	mul	x4,x4,x19
.cp:	cbz	x4,.built
	ldrb	w5,[x2],1
	strb	w5,[x3],1
	sub	x4,x4,1
	b	.cp
.built:	bl	vol_build
	adr	x0,vfs_ch			; le volume du dossier courant a disparu ?
	ldr	x0,[x0]
	tbnz	x0,63,.ok
	bl	cur_vol
	tbz	x0,63,.ok
	bl	go_sd0
	adr	x0,s_gone
	bl	set_msg
.ok:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; vraw -> vols : SD0: d'abord (toujours present), puis les autres
vol_build:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,vol_n
	str	xzr,[x0]
	mov	x19,0
.f:	adr	x0,vraw_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.nosd
	mov	x0,VE_SZ
	adr	x1,vraw
	madd	x20,x19,x0,x1
	mov	x0,x20
	adr	x1,s_sd0l
	bl	str_eq
	cbnz	x0,.sd
	add	x19,x19,1
	b	.f
.sd:	mov	x0,x20
	add	x1,x20,16
	bl	add_vol
	b	.rest
.nosd:	adr	x0,s_sd0l
	adr	x1,s_sd0src
	bl	add_vol
.rest:	mov	x19,0
.r:	adr	x0,vraw_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	mov	x0,VE_SZ
	adr	x1,vraw
	madd	x20,x19,x0,x1
	mov	x0,x20
	adr	x1,s_sd0l
	bl	str_eq
	cbnz	x0,.n
	mov	x0,x20
	add	x1,x20,16
	bl	add_vol
.n:	add	x19,x19,1
	b	.r
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom en minuscules, x1 = source : ajoute un volume a vols
add_vol:
	prologue
	str	x19,[sp,-16]!
	adr	x2,vol_n
	ldr	x3,[x2]
	cmp	x3,VMAX
	b.hs	.out
	adr	x19,vols
	add	x19,x19,x3,lsl 6
	add	x3,x3,1
	str	x3,[x2]
	add	x7,x19,V_LOW
	mov	x5,0
.l:	cmp	x5,13
	b.hs	.le
	ldrb	w6,[x0,x5]
	cbz	w6,.le
	strb	w6,[x7,x5]
	sub	w8,w6,'a'
	cmp	w8,25
	b.hi	.u
	sub	w6,w6,32
.u:	strb	w6,[x19,x5]
	add	x5,x5,1
	b	.l
.le:	strb	wzr,[x7,x5]
	mov	w6,':'
	strb	w6,[x19,x5]
	add	x5,x5,1
	strb	wzr,[x19,x5]
	add	x7,x19,V_SRC
	mov	x5,0
.s:	cmp	x5,23
	b.hs	.se
	ldrb	w6,[x1,x5]
	cbz	w6,.se
	strb	w6,[x7,x5]
	add	x5,x5,1
	b	.s
.se:	strb	wzr,[x7,x5]
	mov	x0,x7				; amovible : « blk.usb... »
	adr	x1,s_src_usb
	bl	str_pre
	strb	w0,[x19,V_REM]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = volume -> x0 = libelle de son genre
vol_kind:
	prologue
	str	x19,[sp,-16]!
	add	x19,x0,V_SRC
	mov	x0,x19
	adr	x1,s_src_usb
	bl	str_pre
	adr	x2,s_k_usb
	cbnz	x0,.r
	mov	x0,x19
	adr	x1,s_src_sd
	bl	str_pre
	adr	x2,s_k_sd
	cbnz	x0,.r
	mov	x0,x19
	adr	x1,s_src_ram
	bl	str_pre
	adr	x2,s_k_ram
	cbnz	x0,.r
	mov	x0,x19
	adr	x1,s_src_img
	bl	str_pre
	adr	x2,s_k_img
	cbnz	x0,.r
	adr	x2,s_k_vol
.r:	mov	x0,x2
	bl	ui_tr
	ldr	x19,[sp],16
	epilogue

; -> x0 = volume du dossier courant (-1 : aucun)
cur_vol:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,16
	adr	x0,path
	mov	x1,0
.c:	ldrb	w2,[x0,x1]
	cbz	w2,.none
	cmp	w2,':'
	b.eq	.end
	cmp	x1,14
	b.hs	.none
	sub	w3,w2,'A'
	cmp	w3,25
	b.hi	.s
	add	w2,w2,32
.s:	strb	w2,[sp,x1]
	add	x1,x1,1
	b	.c
.end:	strb	wzr,[sp,x1]
	mov	x19,0
.v:	adr	x0,vol_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.none
	adr	x0,vols
	add	x0,x0,x19,lsl 6
	add	x0,x0,V_LOW
	mov	x1,sp
	bl	str_eq
	cbnz	x0,.found
	add	x19,x19,1
	b	.v
.found:	mov	x0,x19
	b	.out
.none:	mov	x0,-1
.out:	add	sp,sp,16
	ldr	x19,[sp],16
	epilogue

; x0 = volume : y aller (sa racine)
go_vol:
	prologue
	mov	x1,x0
	adr	x0,vols
	add	x1,x0,x1,lsl 6
	adr	x0,path
	bl	str_cat
	mov	w1,'/'
	strb	w1,[x0],1
	strb	wzr,[x0]
	bl	load_dir
	epilogue

; x0 = volume : le demonter (son service s'arrete)
eject:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,vfs_ch
	ldr	x0,[x0]
	tbnz	x0,63,.out
	adr	x1,vmsg
	mov	x2,VFS_DEMONTER
	str	x2,[x1]
	adr	x3,vols
	add	x3,x3,x19,lsl 6
	ldp	x4,x5,[x3,V_LOW]
	stp	x4,x5,[x1,MSG_DATA]
	sys	SYS_SVC_CALL
	mov	x20,x0
	bl	cur_vol				; on y etait : retour a SD0:/
	cmp	x0,x19
	b.ne	.poll
	bl	go_sd0
.poll:	mov	x0,200				; le volume part a la fin du service
	sys	SYS_SLEEP
	bl	vol_poll
	tbnz	x20,63,.e
	adr	x0,s_unmounted
	bl	set_msg
	b	.out
.e:	mov	x0,x20
	bl	set_err
.out:	ldp	x19,x20,[sp],16
	epilogue

; demonter le volume courant s'il est amovible
eject_cur:
	prologue
	bl	cur_vol
	tbnz	x0,63,.out
	adr	x1,vols
	add	x1,x1,x0,lsl 6
	ldrb	w1,[x1,V_REM]
	cbz	w1,.out
	bl	eject
.out:	epilogue

; ---------------------------------------------------------------------------
;  Actions
; ---------------------------------------------------------------------------

; ouvrir l'entree selectionnee
open_sel:
	prologue
	str	x19,[sp,-16]!
	adr	x0,cursel
	ldr	x19,[x0]
	tbnz	x19,63,.out
	mov	x0,x19
	bl	make_full			; x0 = fin du chemin
	str	x0,[sp,8]
	mov	x0,x19
	bl	ent_ptr
	ldr	x1,[x0,EN_KIND]
	cmp	x1,K_FILE
	b.eq	.file
	cmp	x1,K_APP
	b.ne	.dir
	ldr	x0,[sp,8]			; paquet : son main
	adr	x1,s_main
	bl	str_cat
	b	.run
.dir:	adr	x0,fullpath
	bl	go_dir
	b	.out
.file:	adr	x0,fullpath			; son application (UIKit : app.txt,
	bl	ui_assoc_open			; SD0:/etc/fileassoc.ini), ou il se lance
	b	.res
.run:	adr	x0,fullpath
	mov	x1,0
	sys	SYS_SPAWN
.res:	tbz	x0,63,.launched
	bl	set_err
	b	.out
.launched:
	adr	x0,s_launched
	bl	set_msg
.out:	ldr	x19,[sp],16
	epilogue

; x0, x1 = point de la fenetre : menu de l'entree selectionnee (Ouvrir,
; Ouvrir avec... pour un fichier, Supprimer)
ctx_menu:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	adr	x0,cursel
	ldr	x0,[x0]
	tbnz	x0,63,.out
	bl	ent_ptr
	ldr	x19,[x0,EN_KIND]
	cmp	x19,K_FILE
	cset	x19,eq				; 1 : un fichier
	adr	x0,popbuf
	adr	x1,s_cx_open
	bl	tr_cat
	add	x0,x0,1
	cbz	x19,.del
	adr	x1,s_cx_with
	bl	tr_cat
	add	x0,x0,1
.del:	adr	x1,s_cx_del
	bl	tr_cat
	strb	wzr,[x0,1]
	mov	x0,x21
	mov	x1,x22
	adr	x2,popbuf
	mov	x3,220
	bl	ui_popup_local
	tbnz	x0,63,.out			; Echap, clic ailleurs
	cbz	x0,.open
	cmp	x0,1
	b.ne	.d
	cbnz	x19,.with
.d:	bl	delete_sel
	b	.out
.with:	bl	open_with_sel
	b	.out
.open:	bl	open_sel
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; « Ouvrir avec » : l'application choisie dans le dialogue de UIKit
open_with_sel:
	prologue
	adr	x0,cursel
	ldr	x0,[x0]
	tbnz	x0,63,.out
	bl	ent_ptr
	ldr	x1,[x0,EN_KIND]
	cmp	x1,K_FILE
	b.ne	.out
	adr	x0,cursel
	ldr	x0,[x0]
	bl	make_full
	adr	x0,fullpath
	bl	ui_open_with
	cbz	x0,.out				; Annuler
	tbz	x0,63,.ok
	bl	set_err
	b	.out
.ok:	adr	x0,s_launched
	bl	set_msg
.out:	epilogue

; dossier parent (le dossier quitte reste selectionne)
go_parent:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,path				; dernier composant -> tmpname
	mov	x1,x0
.c:	ldrb	w2,[x0],1
	cbz	w2,.e
	cmp	w2,'/'
	b.ne	.c
	mov	x1,x0
	b	.c
.e:	adr	x0,tmpname
	mov	x2,256
	bl	str_copy_n
	adr	x0,path
	bl	str_len
	mov	x19,x0
	adr	x0,path
	bl	path_parent
	adr	x0,path
	bl	str_len
	cmp	x0,x19
	b.eq	.out				; deja a la racine
	bl	load_dir
	adr	x0,tmpname
	bl	select_name
.out:	ldp	x19,x20,[sp],16
	epilogue

; « Nouveau dossier », « Nouveau dossier 2 »... (le premier libre)
new_folder:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,1
.try:	adr	x0,tmpname
	adr	x1,s_newdir
	bl	tr_cat
	cmp	x19,1
	b.eq	.j
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	mov	x0,x19
	bl	fmt_dec
.j:	adr	x0,fullpath
	adr	x1,path
	adr	x2,tmpname
	bl	path_join
	adr	x0,fullpath
	adr	x1,statbuf
	sys	SYS_STAT
	cbnz	x0,.free
	add	x19,x19,1
	cmp	x19,100
	b.lo	.try
	b	.out
.free:	adr	x0,fullpath
	sys	SYS_MKDIR
	mov	x20,x0
	bl	load_dir
	cbz	x20,.ok
	mov	x0,x20
	bl	set_err
	b	.out
.ok:	adr	x0,tmpname
	bl	select_name
.out:	ldp	x19,x20,[sp],16
	epilogue

; supprimer la selection, apres confirmation (menu surgissant)
delete_sel:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,cursel
	ldr	x0,[x0]
	tbnz	x0,63,.out
	bl	ent_ptr
	mov	x19,x0
	adr	x0,popbuf			; « Supprimer « nom » », « Annuler »
	adr	x1,s_pop1
	bl	tr_cat
	mov	x1,x19
	ldr	x2,[x19,EN_KIND]
	cmp	x2,K_APP
	b.ne	.nm
	add	x1,x19,EN_LABEL
.nm:	bl	str_cat
	adr	x1,s_pop2
	bl	tr_cat
	add	x0,x0,1
	adr	x1,s_cancel
	bl	tr_cat
	strb	wzr,[x0,1]
	adr	x0,scr				; au milieu de l'ecran
	ldp	x0,x1,[x0]
	cbnz	x0,.pos
	mov	x0,640
	mov	x1,480
.pos:	sub	x0,x0,320
	asr	x0,x0,1
	asr	x1,x1,1
	sub	x1,x1,40
	adr	x2,popbuf
	mov	x3,320
	bl	ui_popup
	cbnz	x0,.out				; Annuler, Echap, clic ailleurs
	adr	x0,cursel
	ldr	x0,[x0]
	bl	make_full
	adr	x0,fullpath
	sys	SYS_UNLINK
	mov	x20,x0
	bl	load_dir
	cbz	x20,.ok
	mov	x0,x20
	bl	set_err
	b	.out
.ok:	adr	x0,s_deleted
	bl	set_msg
.out:	ldp	x19,x20,[sp],16
	epilogue

; actualiser (dossier et volumes ; la selection est gardee)
refresh:
	prologue
	adr	x0,tmpname
	strb	wzr,[x0]
	adr	x0,cursel
	ldr	x0,[x0]
	tbnz	x0,63,.l
	bl	ent_ptr
	mov	x1,x0
	adr	x0,tmpname
	mov	x2,256
	bl	str_copy_n
.l:	bl	load_dir
	adr	x0,tmpname
	bl	select_name
	adr	x0,vraw_n
	mov	x1,-1
	str	x1,[x0]
	bl	vol_poll
	epilogue

; ---------------------------------------------------------------------------
;  Entrees
; ---------------------------------------------------------------------------

; w0 = entree de menu choisie
on_menu:
	prologue
	cmp	w0,ID_OPEN
	b.ne	.m2
	bl	open_sel
	b	.draw
.m2:	cmp	w0,ID_NEWDIR
	b.ne	.m3
	bl	new_folder
	b	.draw
.m3:	cmp	w0,ID_DELETE
	b.ne	.m4
	bl	delete_sel
	b	.draw
.m4:	cmp	w0,ID_REFRESH
	b.ne	.m5
	bl	refresh
	b	.draw
.m5:	cmp	w0,ID_CLOSE
	b.eq	quit
	cmp	w0,ID_OPENWITH
	b.ne	.m10
	bl	open_with_sel
	b	.draw
.m10:	cmp	w0,ID_PARENT
	b.ne	.m11
	bl	go_parent
	b	.draw
.m11:	cmp	w0,ID_SD0
	b.ne	.m30
	bl	go_sd0
	b	.draw
.m30:	cmp	w0,ID_EJECT
	b.ne	.m40
	bl	eject_cur
	b	.draw
.m40:	cmp	w0,ID_ICONS
	b.ne	.m41
	mov	x0,0
	bl	set_view
	b	.draw
.m41:	cmp	w0,ID_LIST
	b.ne	.pl
	mov	x0,1
	bl	set_view
	b	.draw
.pl:	sub	w0,w0,ID_PLACE
	adr	x1,pl_n
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.out
	adr	x1,places
	add	x0,x1,x0,lsl 7
	add	x0,x0,PL_PATH
	bl	go_dir
.draw:	bl	draw
.out:	epilogue

on_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,1				; appui
	b.eq	.press
	bl	mouse_aux			; deplacement, relache
	b	.out
.press:	ldrsw	x21,[x0,EV_X]
	ldrsw	x22,[x0,EV_Y]
	cmp	x21,PAN_W
	b.ge	.main
	cmp	x22,TOP_H-8			; le haut du panneau deplace la fenetre
	b.hs	.pan
	bl	ui_window_drag
	b	.out
.pan:	; panneau : acces rapides puis volumes
	cmp	x22,PAN_Y0
	b.lt	.out
	adr	x3,pl_n
	ldr	x3,[x3]
	sub	x0,x22,PAN_Y0
	mov	x1,PROW_H
	udiv	x19,x0,x1
	cmp	x19,x3
	b.hs	.vols
	adr	x0,places
	add	x0,x0,x19,lsl 7
	add	x0,x0,PL_PATH
	bl	go_dir
	b	.draw
.vols:	bl	vol_y0
	cmp	x22,x0
	b.lt	.out
	sub	x0,x22,x0
	mov	x1,VROW_H
	udiv	x19,x0,x1
	adr	x3,vol_n
	ldr	x3,[x3]
	cmp	x19,x3
	b.hs	.out
	adr	x0,vols
	add	x0,x0,x19,lsl 6
	ldrb	w0,[x0,V_REM]
	cbz	w0,.vgo
	cmp	x21,PAN_W-36			; bouton d'ejection
	b.lt	.vgo
	mov	x0,x19
	bl	eject
	b	.draw
.vgo:	mov	x0,x19
	bl	go_vol
	b	.draw
.main:	cmp	x22,TOP_H			; en-tete
	b.ge	.list
	mov	x0,x21
	mov	x1,x22
	bl	head_at
	cbz	x0,.out
	bl	head_click
	b	.draw
.list:	adr	x0,view				; en-tete des colonnes
	ldr	x0,[x0]
	cbz	x0,.body
	cmp	x22,LIST_Y
	b.lt	.out
	cmp	x22,LIST_Y+HDR_H
	b.ge	.body
	mov	x0,x21				; sur un separateur : on le saisit
	mov	x1,x22
	bl	sep_at
	cbz	x0,.hc
	adr	x1,drag
	str	x0,[x1]
	b	.draw
.hc:	mov	x0,x21
	bl	header_click
	b	.draw
.body:	adr	x0,body_y
	ldr	x0,[x0]
	cmp	x22,x0
	b.lt	.out
	sub	x1,x22,x0
	adr	x2,line_h
	ldr	x2,[x2]
	udiv	x1,x1,x2			; ligne visible
	adr	x2,rows
	ldr	x2,[x2]
	cmp	x1,x2
	b.hs	.out
	adr	x2,scroll
	ldr	x2,[x2]
	add	x1,x1,x2
	adr	x2,cols
	ldr	x2,[x2]
	mul	x19,x1,x2			; premier rang de la ligne
	adr	x0,view
	ldr	x0,[x0]
	cbnz	x0,.idx
	bl	grid_x0				; colonne de la grille
	cmp	x21,x0
	b.lt	.empty
	sub	x0,x21,x0
	mov	x1,CELL_W
	udiv	x0,x0,x1
	adr	x1,cols
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.empty
	add	x19,x19,x0
.idx:	adr	x0,nents
	ldr	x0,[x0]
	cmp	x19,x0
	b.lo	.hit
.empty:	mov	x1,-1				; dans le vide : plus de selection
	adr	x0,cursel
	str	x1,[x0]
	adr	x0,last_idx
	str	x1,[x0]
	b	.draw
.hit:	adr	x0,ui_ev			; bouton droit : le menu de l'entree
	ldr	w0,[x0,EV_CODE]
	tbz	w0,1,.left
	adr	x1,cursel
	str	x19,[x1]
	bl	draw
	mov	x0,x21
	mov	x1,x22
	bl	ctx_menu
	b	.draw
.left:	sys	SYS_TIME_MS
	mov	x20,x0
	adr	x1,last_idx			; double-clic ?
	ldr	x2,[x1]
	cmp	x2,x19
	b.ne	.single
	adr	x3,last_time
	ldr	x3,[x3]
	sub	x3,x20,x3
	cmp	x3,DCLICK_MS
	b.hs	.single
	mov	x2,-1
	str	x2,[x1]
	adr	x1,cursel
	str	x19,[x1]
	bl	open_sel
	b	.draw
.single:
	str	x19,[x1]
	adr	x1,last_time
	str	x20,[x1]
	adr	x1,cursel
	str	x19,[x1]
	adr	x1,msg				; le message laisse place a la selection
	str	xzr,[x1]
.draw:	bl	draw
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

on_key:
	prologue
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.out				; relache
	ldr	w1,[x0,EV_CODE]
	ldr	w5,[x0,EV_MODS]
	mov	w6,MOD_CTRL
	tst	w5,w6
	b.eq	.plain
	cmp	w1,KEY_N
	b.ne	.cl
	bl	new_folder
	b	.draw
.cl:	cmp	w1,KEY_L
	b.ne	.cw
	bl	refresh
	b	.draw
.cw:	cmp	w1,KEY_W
	b.eq	quit
	b	.out
.plain:	adr	x2,cursel
	ldr	x3,[x2]
	adr	x4,nents
	ldr	x4,[x4]
	adr	x6,cols				; haut / bas : une ligne de la vue
	ldr	x6,[x6]
	adr	x7,view
	ldr	x7,[x7]
	cmp	w1,KEY_RIGHT			; gauche / droite : vue icones
	b.ne	.lt
	cbnz	x7,.out
	mov	x6,1
	b	.down
.lt:	cmp	w1,KEY_LEFT
	b.ne	.dn
	cbnz	x7,.out
	mov	x6,1
	b	.up2
.dn:	cmp	w1,KEY_DOWN
	b.ne	.up
.down:	tbnz	x3,63,.first
	add	x3,x3,x6
	sub	x5,x4,1
	cmp	x3,x5
	csel	x3,x5,x3,gt
	tbnz	x3,63,.out
	b	.setsel
.up:	cmp	w1,KEY_UP
	b.ne	.home
.up2:	cmp	x3,0
	b.le	.first
	sub	x3,x3,x6
	cmp	x3,0
	csel	x3,xzr,x3,lt
	b	.setsel
.home:	cmp	w1,KEY_HOME
	b.ne	.end
.first:	cbz	x4,.out
	mov	x3,0
	b	.setsel
.end:	cmp	w1,KEY_END
	b.ne	.enter
	cbz	x4,.out
	sub	x3,x4,1
.setsel:
	str	x3,[x2]
	adr	x2,msg
	str	xzr,[x2]
	bl	keep_visible
	b	.draw
.enter:	cmp	w1,KEY_ENTER
	b.eq	.open
	cmp	w1,KEY_KPENT
	b.ne	.bs
.open:	bl	open_sel
	b	.draw
.bs:	cmp	w1,KEY_BACKSPACE
	b.ne	.del
	bl	go_parent
	b	.draw
.del:	cmp	w1,KEY_DELETE
	b.ne	.f5
	bl	delete_sel
	b	.draw
.f5:	cmp	w1,KEY_F5
	b.ne	.out
	bl	refresh
.draw:	bl	draw
.out:	epilogue

; ---------------------------------------------------------------------------
;  Affichage (x26 = largeur, x27 = hauteur, x28 = jetons du theme : poses
;  par draw pour les draw_*)
; ---------------------------------------------------------------------------
draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	bl	ui_jetons
	mov	x28,x0
	adr	x0,ui_w
	ldp	x26,x27,[x0]
	bl	draw_panel
	bl	draw_top
	adr	x0,view
	ldr	x0,[x0]
	cbz	x0,.g
	bl	draw_rows
	b	.st
.g:	bl	draw_grid
.st:	bl	draw_status
	bl	ui_update_all
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, w2 = onglet, w3 = corps : petit dossier (20 x 17)
draw_folder:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w3
	mov	w4,w2
	add	x1,x20,1
	mov	x2,8
	mov	x3,3
	bl	ui_fill
	mov	x0,x19
	add	x1,x20,3
	mov	x2,20
	mov	x3,14
	mov	w4,w21
	bl	ui_fill
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y : page (14 x 18)
draw_doc:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x2,14
	mov	x3,18
	ldr	w4,[x28,TJ_ELEVE]
	bl	ui_fill
	mov	x0,x19
	mov	x1,x20
	mov	x2,14
	mov	x3,18
	ldr	w4,[x28,TJ_LIGNE_FORTE]
	bl	ui_frame
	mov	x0,3				; lignes de texte
.l:	str	x0,[sp,-16]!
	mov	x1,3
	mul	x1,x0,x1
	add	x1,x20,x1
	add	x0,x19,3
	mov	x2,8
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE_FORTE]
	bl	ui_fill
	ldr	x0,[sp],16
	add	x0,x0,1
	cmp	x0,6
	b.lo	.l
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x du milieu, x1 = haut : symbole d'ejection
draw_eject:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0
.t:	sub	x0,x19,x21
	add	x1,x20,x21
	lsl	x2,x21,1
	add	x2,x2,1
	mov	x3,1
	ldr	w4,[x28,TJ_TEXTE2]
	bl	ui_fill
	add	x21,x21,1
	cmp	x21,5
	b.lo	.t
	sub	x0,x19,4
	add	x1,x20,7
	mov	x2,9
	mov	x3,2
	ldr	w4,[x28,TJ_TEXTE2]
	bl	ui_fill
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = haut, x2 = largeur, x3 = hauteur : fond de selection
draw_sel:
	ldr	w5,[x28,TJ_ACCENT_CLAIR]
	mov	w6,w5
	mov	x4,8
	b	ui_rbox

; -> x0 = element du panneau du dossier courant : 0x200 + n (acces rapide
; exact), n (volume qui le contient), -1
cur_hl:
	prologue
	str	x19,[sp,-16]!
	mov	x19,0
.p:	adr	x0,pl_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.v
	adr	x1,places
	add	x1,x1,x19,lsl 7
	add	x1,x1,PL_PATH
	adr	x0,path
	bl	str_icmp
	cbz	x0,.pl
	add	x19,x19,1
	b	.p
.pl:	add	x0,x19,0x200
	b	.out
.v:	bl	cur_vol
.out:	ldr	x19,[sp],16
	epilogue

; -> x0 = haut de la premiere ligne de volume (sous les acces rapides)
vol_y0:
	adr	x0,pl_n
	ldr	x0,[x0]
	mov	x1,PAN_Y0
	cbz	x0,.r
	mov	x2,PROW_H
	madd	x1,x0,x2,x1
	add	x1,x1,PL_GAP
.r:	mov	x0,x1
	ret

draw_panel:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x0,0
	mov	x1,0
	mov	x2,PAN_W-1
	mov	x3,x27
	ldr	w4,[x28,TJ_CREUX]
	bl	ui_fill
	mov	x0,PAN_W-1
	mov	x1,0
	mov	x2,1
	mov	x3,x27
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	bl	cur_hl
	mov	x22,x0
	mov	x23,PAN_Y0			; haut de la section
	adr	x0,pl_n
	ldr	x0,[x0]
	cbz	x0,.vols
	adr	x0,s_places			; acces rapides
	bl	tr0
	mov	x1,14
	sub	x2,x23,10
	ldr	w3,[x28,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x19,0
.p:	adr	x0,pl_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.pend
	mov	x0,PROW_H
	madd	x20,x19,x0,x23
	add	x0,x19,0x200
	cmp	x22,x0
	b.ne	.pic
	mov	x0,6
	add	x1,x20,1
	mov	x2,PAN_W-13
	mov	x3,PROW_H-2
	bl	draw_sel
.pic:	mov	x0,14
	add	x1,x20,5
	ldr	w2,[x28,TJ_TEXTE2]
	mov	w3,w2
	bl	draw_folder
	adr	x0,places
	add	x0,x0,x19,lsl 7
	mov	x1,42
	add	x2,x20,19
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,PAN_W-50
	bl	ui_text
	add	x19,x19,1
	b	.p
.pend:	bl	vol_y0
	mov	x23,x0
.vols:	adr	x0,s_vols			; volumes
	bl	tr0
	mov	x1,14
	sub	x2,x23,10
	ldr	w3,[x28,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x19,0
.v:	adr	x0,vol_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	mov	x0,VROW_H
	madd	x20,x19,x0,x23			; haut de la ligne
	add	x0,x20,VROW_H
	cmp	x0,x27
	b.gt	.out
	adr	x21,vols
	add	x21,x21,x19,lsl 6
	cmp	x22,x19
	b.ne	.vic
	mov	x0,6
	add	x1,x20,1
	mov	x2,PAN_W-13
	mov	x3,VROW_H-2
	bl	draw_sel
.vic:	mov	x0,14				; disque
	add	x1,x20,10
	mov	x2,20
	mov	x3,15
	mov	x4,3
	ldr	w5,[x28,TJ_TEXTE2]
	mov	w6,w5
	bl	ui_rbox
	mov	x0,28
	add	x1,x20,19
	mov	x2,3
	mov	x3,3
	ldr	w4,[x28,TJ_CREUX]
	bl	ui_fill
	mov	x0,x21				; « USB0: »
	mov	x1,42
	add	x2,x20,16
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,PAN_W-42-36
	bl	ui_text
	mov	x0,x21				; genre
	bl	vol_kind
	mov	x1,42
	add	x2,x20,30
	ldr	w3,[x28,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,PAN_W-42-36
	bl	ui_text
	ldrb	w0,[x21,V_REM]
	cbz	w0,.vn
	mov	x0,PAN_W-22
	add	x1,x20,13
	bl	draw_eject
.vn:	add	x19,x19,1
	b	.v
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; en-tete : precedent, suivant, parent, fil d'Ariane, choix de la vue ; rien
; dans les FRAME_CAP_W de droite (boutons de la fenetre)
draw_top:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,PAN_W
	mov	x1,0
	sub	x2,x26,PAN_W
	mov	x3,TOP_H
	ldr	w4,[x28,TJ_SURFACE]
	bl	ui_fill
	mov	x0,PAN_W
	mov	x1,TOP_H
	sub	x2,x26,PAN_W
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	mov	x0,PAN_W
	mov	x1,TOP_H+1
	sub	x2,x26,PAN_W
	mov	x3,LIST_Y-TOP_H-1
	ldr	w4,[x28,TJ_SURFACE]
	bl	ui_fill
	bl	crumbs_make
	mov	x19,H_BACK
.nav:	mov	x0,x19
	bl	draw_nav
	add	x19,x19,1
	cmp	x19,H_UP
	b.ls	.nav
	bl	draw_seg
	bl	draw_crumbs
	ldp	x19,x20,[sp],16
	epilogue

; x0 = H_BACK, H_NEXT, H_UP -> x0 = 1 si le bouton sert (0 : grise)
nav_on:
	adr	x1,h_pos
	ldr	x1,[x1]
	cmp	x0,H_NEXT
	b.eq	.n
	b.hi	.u
	cmp	x1,0
	cset	x0,gt
	ret
.n:	adr	x2,h_n
	ldr	x2,[x2]
	add	x1,x1,1
	cmp	x1,x2
	cset	x0,lo
	ret
.u:	adr	x1,cr_n
	ldr	x1,[x1]
	cmp	x1,1
	cset	x0,hi
	ret

; x0 = H_BACK, H_NEXT, H_UP : bouton fantome
draw_nav:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	sub	x0,x19,H_BACK
	mov	x1,NAV_W
	mul	x0,x0,x1
	add	x20,x0,NAV_X
	mov	x0,x19
	bl	nav_on
	ldr	w22,[x28,TJ_TEXTE3]
	cbz	x0,.txt
	ldr	w22,[x28,TJ_TEXTE1]
	adr	x0,hhot
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.txt
	mov	x0,x20
	mov	x1,NAV_Y
	mov	x2,NAV_W
	mov	x3,NAV_H
	mov	x4,8
	ldr	w5,[x28,TJ_CREUX]
	mov	w6,w5
	bl	ui_rbox
.txt:	adr	x21,s_back
	cmp	x19,H_NEXT
	b.lo	.t
	adr	x21,s_next
	b.eq	.t
	adr	x21,s_up
.t:	mov	x0,x21
	mov	x1,NAV_PX
	adr	x2,ui_font
	bl	ui_text_width
	mov	x1,NAV_W
	sub	x1,x1,x0
	asr	x1,x1,1
	add	x1,x20,x1
	mov	x0,x21
	mov	x2,NAV_Y+22
	mov	w3,w22
	mov	x4,NAV_PX
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = segment (0 icones, 1 liste) -> w0 = encre : TEXTE1 choisi ou survole
seg_ink:
	adr	x1,view
	ldr	x1,[x1]
	adr	x2,hhot
	ldr	x2,[x2]
	sub	x2,x2,H_ICONS
	cmp	x1,x0
	ccmp	x2,x0,4,ne
	ldr	w0,[x28,TJ_TEXTE2]
	b.ne	.r
	ldr	w0,[x28,TJ_TEXTE1]
.r:	ret

; controle segmente icones / liste (rail CREUX, comme ui_segmented ; segment
; choisi ELEVE sur une ombre LIGNE)
draw_seg:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	x19,x26,SEG_X_R
	mov	x0,x19
	mov	x1,SEG_Y
	mov	x2,SEG_RAIL
	mov	x3,SEG_H
	mov	x4,8
	ldr	w5,[x28,TJ_CREUX]
	mov	w6,w5
	bl	ui_rbox
	adr	x0,view
	ldr	x0,[x0]
	mov	x1,SEG_W
	madd	x20,x0,x1,x19
	add	x20,x20,2
	mov	x0,x20
	mov	x1,SEG_Y+2
	mov	x2,SEG_W
	mov	x3,SEG_H-3
	mov	x4,6
	ldr	w5,[x28,TJ_LIGNE]
	mov	w6,w5
	bl	ui_rbox
	mov	x0,x20
	mov	x1,SEG_Y+2
	mov	x2,SEG_W
	mov	x3,SEG_H-4
	mov	x4,6
	ldr	w5,[x28,TJ_ELEVE]
	mov	w6,w5
	bl	ui_rbox
	mov	x0,0				; icones : quatre carres
	bl	seg_ink
	mov	w21,w0
	mov	x20,0
.gr:	and	x0,x20,1
	lsl	x0,x0,3
	add	x0,x0,x19
	add	x0,x0,2+(SEG_W-14)/2
	lsr	x1,x20,1
	lsl	x1,x1,3
	add	x1,x1,SEG_Y+7
	mov	x2,6
	mov	x3,6
	mov	x4,1
	mov	w5,w21
	mov	w6,w21
	bl	ui_rbox
	add	x20,x20,1
	cmp	x20,4
	b.lo	.gr
	mov	x0,1				; liste : trois lignes pointees
	bl	seg_ink
	mov	w21,w0
	add	x19,x19,2+SEG_W+(SEG_W-14)/2
	mov	x22,SEG_Y+8
	mov	x20,3
.ls:	mov	x0,x19
	mov	x1,x22
	mov	x2,2
	mov	x3,2
	mov	w4,w21
	bl	ui_fill
	add	x0,x19,4
	mov	x1,x22
	mov	x2,10
	mov	x3,2
	mov	w4,w21
	bl	ui_fill
	add	x22,x22,5
	sub	x20,x20,1
	cbnz	x20,.ls
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; path -> segments du fil d'Ariane (crumbs, cr_n ; noms dans crbuf). Trop
; profond : le dernier segment reste le vrai
crumbs_make:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,crbuf
	adr	x1,path
	mov	x2,272
	bl	str_copy_n
	adr	x19,crbuf
	mov	x20,x19				; debut du composant
	mov	x21,0
	adr	x22,crumbs
.c:	ldrb	w0,[x19]
	cbz	w0,.seg
	cmp	w0,'/'
	b.ne	.n
.seg:	strb	wzr,[x19]
	cmp	x19,x20
	b.eq	.skip
	cmp	x21,CR_MAX
	b.lo	.r
	mov	x21,CR_MAX-1
.r:	add	x1,x22,x21,lsl 5
	str	x20,[x1,CR_TXT]
	adr	x2,crbuf
	sub	x2,x19,x2
	str	x2,[x1,CR_END]
	add	x21,x21,1
.skip:	add	x20,x19,1
	cbz	w0,.end
.n:	add	x19,x19,1
	b	.c
.end:	adr	x0,cr_n
	str	x21,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = rang du segment -> w0 = encre : TEXTE2, TEXTE1 au survol
crumb_ink:
	adr	x1,hhot
	ldr	x1,[x1]
	sub	x1,x1,H_CRUMB
	cmp	x1,x0
	ldr	w0,[x28,TJ_TEXTE2]
	b.ne	.r
	ldr	w0,[x28,TJ_TEXTE1]
.r:	ret

; (draw_crumbs) x21 = x, x24 = pas : separateur
crumb_sep:
	prologue
	adr	x0,s_sep
	add	x1,x21,CR_GAP
	mov	x2,CR_BASE
	ldr	w3,[x28,TJ_TEXTE3]
	mov	x4,CR_PX
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	add	x21,x21,x24
	epilogue

; fil d'Ariane : dernier segment gras ; trop long, on coupe a gauche, le « … »
; mene au segment qu'il remplace (CR_X, CR_W pour head_at)
draw_crumbs:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	sub	sp,sp,16			; [sp] : largeur de « … », [sp,8] : premier
	adr	x19,crumbs			; segment affiche
	adr	x0,cr_n
	ldr	x23,[x0]
	cbz	x23,.out
	sub	x23,x23,1			; le dernier
	mov	x21,CR_X0
	sub	x22,x26,CR_R
	adr	x0,s_ell
	mov	x1,CR_PX
	adr	x2,ui_font
	bl	ui_text_width
	str	x0,[sp]
	adr	x0,s_sep
	mov	x1,CR_PX
	adr	x2,ui_font
	bl	ui_text_width
	add	x24,x0,2*CR_GAP
	mov	x20,0				; largeurs
.w:	add	x0,x19,x20,lsl 5
	ldr	x0,[x0,CR_TXT]
	mov	x1,CR_PX
	adr	x2,ui_font
	cmp	x20,x23
	b.ne	.w1
	adr	x2,ui_font_b
.w1:	bl	ui_text_width
	add	x1,x19,x20,lsl 5
	str	x0,[x1,CR_W]
	mov	x0,-1
	str	x0,[x1,CR_X]
	add	x20,x20,1
	cmp	x20,x23
	b.ls	.w
	cmp	x22,x21
	b.le	.out
	add	x0,x19,x23,lsl 5		; depuis la fin, ce qui tient
	ldr	x25,[x0,CR_W]
	str	x23,[sp,8]
	mov	x20,x23
.fit:	cbz	x20,.place
	sub	x20,x20,1
	add	x0,x19,x20,lsl 5
	ldr	x0,[x0,CR_W]
	add	x0,x0,x25
	add	x0,x0,x24
	mov	x1,x0
	cbz	x20,.f1				; (reste a gauche : la place du « … »)
	ldr	x2,[sp]
	add	x1,x1,x2
	add	x1,x1,x24
.f1:	add	x1,x1,x21
	cmp	x1,x22
	b.gt	.place
	mov	x25,x0
	str	x20,[sp,8]
	b	.fit
.place:	ldr	x20,[sp,8]
	cbz	x20,.seg
	sub	x20,x20,1
	add	x0,x19,x20,lsl 5
	str	x21,[x0,CR_X]
	ldr	x1,[sp]
	str	x1,[x0,CR_W]
	mov	x0,x20
	bl	crumb_ink
	mov	w3,w0
	adr	x0,s_ell
	mov	x1,x21
	mov	x2,CR_BASE
	mov	x4,CR_PX
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	ldr	x0,[sp]
	add	x21,x21,x0
	bl	crumb_sep
	add	x20,x20,1
.seg:	add	x25,x19,x20,lsl 5
	str	x21,[x25,CR_X]
	cmp	x20,x23
	b.eq	.last
	mov	x0,x20
	bl	crumb_ink
	mov	w3,w0
	ldr	x0,[x25,CR_TXT]
	mov	x1,x21
	mov	x2,CR_BASE
	mov	x4,CR_PX
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	ldr	x0,[x25,CR_W]
	add	x21,x21,x0
	bl	crumb_sep
	add	x20,x20,1
	b	.seg
.last:	sub	x6,x22,x21
	cmp	x6,8
	b.lt	.out
	ldr	x0,[x25,CR_TXT]
	mov	x1,x21
	mov	x2,CR_BASE
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,CR_PX
	adr	x5,ui_font_b
	bl	ui_text_fit
.out:	add	sp,sp,16
	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y -> x0 = element de l'en-tete sous ce point (H_*, 0 : aucun ;
; le dernier segment n'en est pas un, on y est deja)
head_at:
	mov	x4,x0
	cmp	x1,NAV_Y
	b.lt	.none
	cmp	x1,NAV_Y+NAV_H
	b.ge	.none
	sub	x0,x4,NAV_X
	tbnz	x0,63,.none
	mov	x1,NAV_W
	udiv	x0,x0,x1
	cmp	x0,3
	b.hs	.seg
	add	x0,x0,H_BACK
	ret
.seg:	adr	x0,ui_w
	ldr	x0,[x0]
	sub	x0,x0,SEG_X_R
	sub	x0,x4,x0
	tbnz	x0,63,.cr
	cmp	x0,SEG_RAIL
	b.hs	.none
	cmp	x0,2+SEG_W
	mov	x0,H_ICONS
	mov	x1,H_LIST
	csel	x0,x1,x0,hs
	ret
.cr:	adr	x1,crumbs
	adr	x2,cr_n
	ldr	x2,[x2]
	mov	x3,0
.c:	add	x5,x3,1
	cmp	x5,x2
	b.hs	.none
	ldr	x5,[x1,CR_X]
	tbnz	x5,63,.nx
	sub	x5,x4,x5
	add	x5,x5,CR_GAP
	tbnz	x5,63,.nx
	ldr	x6,[x1,CR_W]
	add	x6,x6,2*CR_GAP
	cmp	x5,x6
	b.lo	.hit
.nx:	add	x1,x1,CR_SZ
	add	x3,x3,1
	b	.c
.hit:	add	x0,x3,H_CRUMB
	ret
.none:	mov	x0,0
	ret

; x0 = rang du segment : aller a ce dossier
crumb_go:
	prologue
	adr	x1,crumbs
	add	x1,x1,x0,lsl 5
	ldr	x2,[x1,CR_END]
	adr	x0,fullpath
	adr	x1,path
.c:	cbz	x2,.e
	ldrb	w3,[x1],1
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c
.e:	ldurb	w3,[x0,-1]			; « SD0: » : sa racine
	cmp	w3,':'
	b.ne	.z
	mov	w3,'/'
	strb	w3,[x0],1
.z:	strb	wzr,[x0]
	adr	x0,fullpath
	bl	go_dir
	epilogue

; x0 = element de l'en-tete : son action
head_click:
	prologue
	cmp	x0,H_BACK
	b.ne	.n
	mov	x0,-1
	bl	hist_go
	b	.out
.n:	cmp	x0,H_NEXT
	b.ne	.u
	mov	x0,1
	bl	hist_go
	b	.out
.u:	cmp	x0,H_UP
	b.ne	.v
	bl	go_parent
	b	.out
.v:	cmp	x0,H_LIST
	b.hi	.c
	sub	x0,x0,H_ICONS
	adr	x1,view
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.out
	bl	set_view
	b	.out
.c:	sub	x0,x0,H_CRUMB
	bl	crumb_go
.out:	epilogue

; x0 = libelle, x1 = x, x2 = colonne (S_*), x3 = largeur max : titre de colonne, avec la
; fleche du tri
draw_hdr:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x6,x3
	mov	x2,LIST_Y+16
	ldr	w3,[x28,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font_b
	bl	ui_text
	adr	x0,sort_key
	ldr	x0,[x0]
	cmp	x0,x21
	b.ne	.out
	mov	x0,x19
	mov	x1,12
	adr	x2,ui_font_b
	bl	ui_text_width
	add	x1,x20,x0
	add	x1,x1,5
	adr	x0,s_asc
	adr	x2,sort_desc
	ldr	x2,[x2]
	cbz	x2,.a
	adr	x0,s_desc
.a:	mov	x2,LIST_Y+15
	ldr	w3,[x28,TJ_TEXTE2]
	mov	x4,9
	adr	x5,ui_font
	mov	x6,16
	bl	ui_text
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; vue liste : en-tete (Nom, Taille, Type) et lignes
draw_rows:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	sub	sp,sp,32
	mov	x0,PAN_W
	mov	x1,LIST_Y
	sub	x2,x26,PAN_W
	sub	x3,x27,LIST_Y+SB_H
	ldr	w4,[x28,TJ_SURFACE]
	bl	ui_fill
	bl	col_x
	mov	x24,x0				; x de Taille
	mov	x25,x1				; x de Type
	mov	x0,PAN_W
	mov	x1,LIST_Y+HDR_H-1
	sub	x2,x26,PAN_W
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	sub	x23,x25,x24			; largeur de Taille
	mov	x0,x24				; separateurs
	mov	x1,LIST_Y+4
	mov	x2,1
	mov	x3,HDR_H-8
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	mov	x0,x25
	mov	x1,LIST_Y+4
	mov	x2,1
	mov	x3,HDR_H-8
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	adr	x0,s_h_name
	bl	tr0
	mov	x1,PAN_W+42
	mov	x2,S_NAME
	sub	x3,x24,PAN_W+50
	bl	draw_hdr
	adr	x0,s_h_size
	bl	tr0
	add	x1,x24,8
	mov	x2,S_SIZE
	sub	x3,x23,12
	bl	draw_hdr
	adr	x0,s_h_type
	bl	tr0
	add	x1,x25,8
	mov	x2,S_TYPE
	sub	x3,x26,x25
	sub	x3,x3,18
	bl	draw_hdr
	mov	x19,0				; ligne visible
.row:	adr	x0,rows
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	adr	x0,scroll
	ldr	x22,[x0]
	add	x22,x22,x19			; rang
	adr	x0,nents
	ldr	x0,[x0]
	cmp	x22,x0
	b.hs	.out
	mov	x0,ROW_H
	mov	x1,LIST_Y+HDR_H
	madd	x20,x19,x0,x1			; haut de la ligne
	adr	x0,cursel
	ldr	x0,[x0]
	cmp	x0,x22
	b.ne	.ic
	mov	x0,PAN_W+4
	add	x1,x20,1
	sub	x2,x26,PAN_W+8
	mov	x3,ROW_H-2
	bl	draw_sel
.ic:	mov	x0,x22
	bl	ent_ptr
	mov	x21,x0
	ldr	x1,[x21,EN_KIND]
	cmp	x1,K_DIR
	b.ne	.ic2
	mov	x0,PAN_W+12
	add	x1,x20,4
	movl	w2,COL_FOLDER_D
	movl	w3,COL_FOLDER
	bl	draw_folder
	b	.size
.ic2:	cmp	x1,K_APP
	b.ne	.ic3
	add	x0,x21,EN_ICON
	mov	x1,PAN_W+12
	add	x2,x20,3
	mov	x3,20
	bl	ui_icone
	b	.size
.ic3:	mov	x0,PAN_W+15
	add	x1,x20,4
	bl	draw_doc
.size:	ldr	x1,[x21,EN_KIND]
	cmp	x1,K_FILE
	b.ne	.type
	ldr	x0,[x21,EN_SIZE]		; taille, alignee a droite
	mov	x1,sp
	bl	fmt_size
	mov	x0,sp
	mov	x1,12
	adr	x2,ui_font
	bl	ui_text_width
	sub	x1,x25,10
	sub	x1,x1,x0
	cmp	x1,x24
	csel	x1,x24,x1,lt
	mov	x0,sp
	add	x2,x20,18
	ldr	w3,[x28,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	sub	x6,x23,4
	bl	ui_text
.type:	add	x0,x21,EN_TYPE
	add	x1,x25,8
	add	x2,x20,18
	ldr	w3,[x28,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	sub	x6,x26,x25
	sub	x6,x6,18
	bl	ui_text
	mov	x0,x21				; nom
	ldr	x1,[x21,EN_KIND]
	cmp	x1,K_APP
	b.ne	.nd
	add	x0,x21,EN_LABEL
.nd:	mov	x1,PAN_W+42
	add	x2,x20,18
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	sub	x6,x24,PAN_W+50
	cmp	x6,8
	b.ge	.tx
	mov	x6,8
.tx:	bl	ui_text
	add	x19,x19,1
	b	.row
.out:	adr	x0,drag				; separateur saisi (toute la hauteur)
	ldr	x0,[x0]
	sub	x3,x27,LIST_Y+SB_H
	cbnz	x0,.mark
	adr	x0,hover			; ou survole (l'en-tete)
	ldr	x0,[x0]
	mov	x3,HDR_H
	cbz	x0,.done
.mark:	cmp	x0,1
	csel	x0,x24,x25,eq
	sub	x0,x0,1
	mov	x1,LIST_Y
	mov	x2,2
	ldr	w4,[x28,TJ_ACCENT]
	bl	ui_fill
.done:	add	sp,sp,32
	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y : grand dossier (48 x 44)
draw_folder_big:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	add	x0,x19,2
	add	x1,x20,4
	mov	x2,20
	mov	x3,10
	mov	x4,3
	movl	w5,COL_FOLDER_D
	mov	w6,w5
	bl	ui_rbox
	mov	x0,x19
	add	x1,x20,9
	mov	x2,ICON_S
	mov	x3,35
	mov	x4,4
	movl	w5,COL_FOLDER
	mov	w6,w5
	bl	ui_rbox
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y : grande page (32 x 44, centree dans 48)
draw_doc_big:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x0,8
	add	x20,x1,2
	mov	x0,x19
	mov	x1,x20
	mov	x2,32
	mov	x3,44
	ldr	w4,[x28,TJ_ELEVE]
	bl	ui_fill
	mov	x0,x19
	mov	x1,x20
	mov	x2,32
	mov	x3,44
	ldr	w4,[x28,TJ_LIGNE_FORTE]
	bl	ui_frame
	mov	x0,0				; lignes de texte
.l:	str	x0,[sp,-16]!
	mov	x1,5
	mul	x1,x0,x1
	add	x1,x20,x1
	add	x1,x1,10
	add	x0,x19,6
	mov	x2,20
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE_FORTE]
	bl	ui_fill
	ldr	x0,[sp],16
	add	x0,x0,1
	cmp	x0,6
	b.lo	.l
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = x de la case, x2 = ligne de base : centre dans la case
; (trop large : a gauche, coupe)
text_center:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x1,13
	adr	x2,ui_font
	bl	ui_text_width
	mov	x1,CELL_W
	sub	x1,x1,x0
	asr	x1,x1,1
	cmp	x1,4
	mov	x2,4
	csel	x1,x2,x1,lt
	add	x1,x20,x1
	mov	x0,x19
	mov	x2,x21
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,CELL_W-8
	bl	ui_text
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom, x1 = x, x2 = y de la case : nom centre sur deux lignes au plus
cell_name:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x1,13				; tout tient ?
	adr	x2,ui_font
	bl	ui_text_width
	cmp	x0,CELL_W-8
	b.gt	.split
	mov	x0,x19
	mov	x1,x20
	add	x2,x21,ICON_S+18
	bl	text_center
	b	.out
.split:	mov	x22,0				; longueur qui tient
	mov	x23,0				; coupure apres une espace
	mov	x24,0
.g:	ldrb	w0,[x19,x24]			; un caractere UTF-8 de plus
	cbz	w0,.cut
	add	x24,x24,1
.cont:	ldrb	w0,[x19,x24]
	and	w1,w0,0xC0
	cmp	w1,0x80
	b.ne	.meas
	add	x24,x24,1
	b	.cont
.meas:	adr	x0,cellbuf
	mov	x1,0
.cp:	cmp	x1,x24
	b.hs	.cpe
	ldrb	w2,[x19,x1]
	strb	w2,[x0,x1]
	add	x1,x1,1
	b	.cp
.cpe:	strb	wzr,[x0,x1]
	mov	x1,13
	adr	x2,ui_font
	bl	ui_text_width
	cmp	x0,CELL_W-8
	b.gt	.cut
	mov	x22,x24
	add	x0,x19,x24
	ldurb	w0,[x0,-1]
	cmp	w0,' '
	b.ne	.g
	mov	x23,x24
	b	.g
.cut:	cbnz	x22,.c1
	mov	x22,x24				; (au moins un caractere)
.c1:	cbz	x23,.c2
	mov	x22,x23
.c2:	adr	x0,cellbuf			; premiere ligne
	mov	x1,0
.cq:	cmp	x1,x22
	b.hs	.cqe
	ldrb	w2,[x19,x1]
	strb	w2,[x0,x1]
	add	x1,x1,1
	b	.cq
.cqe:	strb	wzr,[x0,x1]
	mov	x1,x20
	add	x2,x21,ICON_S+18
	bl	text_center
	add	x0,x19,x22			; la suite
	mov	x1,x20
	add	x2,x21,ICON_S+32
	bl	text_center
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; vue icones : grille de cases
draw_grid:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	mov	x0,PAN_W
	mov	x1,LIST_Y
	sub	x2,x26,PAN_W
	sub	x3,x27,LIST_Y+SB_H
	ldr	w4,[x28,TJ_SURFACE]
	bl	ui_fill
	bl	grid_x0
	mov	x25,x0
	adr	x0,cols
	ldr	x24,[x0]
	mov	x19,0				; case visible
.cell:	adr	x0,rows
	ldr	x0,[x0]
	mul	x0,x0,x24
	cmp	x19,x0
	b.hs	.out
	adr	x0,scroll
	ldr	x0,[x0]
	madd	x22,x0,x24,x19			; rang
	adr	x0,nents
	ldr	x0,[x0]
	cmp	x22,x0
	b.hs	.out
	udiv	x0,x19,x24			; ligne, colonne
	msub	x1,x0,x24,x19
	mov	x2,CELL_W
	madd	x20,x1,x2,x25			; x de la case
	mov	x2,CELL_H
	adr	x3,body_y
	ldr	x3,[x3]
	madd	x21,x0,x2,x3			; y de la case
	adr	x0,cursel
	ldr	x0,[x0]
	cmp	x0,x22
	b.ne	.ic
	add	x0,x20,3
	add	x1,x21,1
	mov	x2,CELL_W-6
	mov	x3,CELL_H-2
	bl	draw_sel
.ic:	mov	x0,x22
	bl	ent_ptr
	mov	x23,x0
	ldr	x1,[x23,EN_KIND]
	add	x0,x20,(CELL_W-ICON_S)/2
	add	x2,x21,5
	cmp	x1,K_DIR
	b.ne	.ic2
	mov	x1,x2
	bl	draw_folder_big
	b	.name
.ic2:	cmp	x1,K_APP
	b.ne	.ic3
	mov	x1,x0
	add	x0,x23,EN_ICON
	mov	x3,ICON_S
	bl	ui_icone
	b	.name
.ic3:	mov	x1,x2
	bl	draw_doc_big
.name:	mov	x0,x23
	ldr	x1,[x23,EN_KIND]
	cmp	x1,K_APP
	b.ne	.nd
	add	x0,x23,EN_LABEL
.nd:	mov	x1,x20
	mov	x2,x21
	bl	cell_name
	add	x19,x19,1
	b	.cell
.out:	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ligne d'etat : « N elements », la selection (nom, taille) ou un message
draw_status:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	x19,x27,SB_H
	mov	x0,PAN_W
	mov	x1,x19
	sub	x2,x26,PAN_W
	mov	x3,SB_H
	ldr	w4,[x28,TJ_SURFACE]
	bl	ui_fill
	mov	x0,PAN_W
	mov	x1,x19
	sub	x2,x26,PAN_W
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	adr	x0,msg
	ldr	x0,[x0]
	cbnz	x0,.txt
	adr	x0,nents
	ldr	x0,[x0]
	mov	x20,x0
	adr	x1,stbuf
	bl	fmt_dec
	adr	x1,s_els
	cmp	x20,1
	b.ne	.pl
	adr	x1,s_el
.pl:	bl	tr_cat
	adr	x1,cursel
	ldr	x1,[x1]
	tbnz	x1,63,.done
	mov	x20,x0
	mov	x0,x1
	bl	ent_ptr
	mov	x1,x0
	mov	x0,x20
	mov	x20,x1				; entree
	adr	x1,s_dash
	bl	str_cat
	mov	x1,x20
	ldr	x2,[x20,EN_KIND]
	cmp	x2,K_APP
	b.ne	.nm
	add	x1,x20,EN_LABEL
.nm:	bl	str_cat
	ldr	x2,[x20,EN_KIND]
	cmp	x2,K_FILE
	b.ne	.done
	adr	x1,s_comma
	bl	str_cat
	mov	x1,x0
	ldr	x0,[x20,EN_SIZE]
	bl	fmt_size
.done:	adr	x0,stbuf
.txt:	mov	x1,PAN_W+12
	sub	x2,x27,8
	ldr	w3,[x28,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	sub	x6,x26,PAN_W+24
	bl	ui_text
	ldp	x19,x20,[sp],16
	epilogue

s_vfs		db 'vfs',0
s_topic_vols	db BUS_TOPIC_VOLUME,'*',0
s_sd		db 'SD0:/',0
s_sd0l		db 'sd0',0
s_sd0src	db 'blk.sd0p1',0
s_src_usb	db 'blk.usb',0
s_src_sd	db 'blk.sd',0
s_src_ram	db 'ram:',0
s_src_img	db 'blk.img',0
s_placesf	db 'SD0:/etc/places.ini',0
s_app		db '.app',0
s_apptxt	db '/app.txt',0
s_main		db '/main',0
s_mi		db 'I',0
m_head		db 'MFichier',10
		db 'I1',9,'Ouvrir',9,'Entr',0xC3,0xA9,'e',10
		db 'I6',9,'Ouvrir avec',0xE2,0x80,0xA6,10
		db 'I2',9,'Nouveau dossier',9,'Ctrl+N',10
		db 'I3',9,'Supprimer',9,'Suppr',10
		db '-',10
		db 'I4',9,'Actualiser',9,'Ctrl+L',10
		db '-',10
		db 'I5',9,'Fermer',9,'Ctrl+W',10
		db 'MAller',10
		db 'I10',9,'Dossier parent',9,'Retour',10
		db '-',10,0
m_tail		db '-',10
		db 'I11',9,'SD0:',10
		db 'MAffichage',10
		db 'I40',9,'Ic',0xC3,0xB4,'nes',10
		db 'I41',9,'Liste',10
		db 'MVolume',10
		db 'I30',9,'D',0xC3,0xA9,'monter le volume courant',10,0
		align 4

segment readable writeable
kit_data
applis_data
ui_data
		align 8
nents		dd 0
cursel		dd -1
scroll		dd 0
rows		dd 1
cols		dd 1
body_y		dd LIST_Y+HDR_H
line_h		dd ROW_H
view		dd 0				; 0 icones, 1 liste
sort_key	dd S_NAME
sort_desc	dd 0
w_size		dd COL_SIZE_W		; largeurs des colonnes (vue liste)
w_type		dd COL_TYPE_W
drag		dd 0			; separateur saisi (1, 2)
hover		dd 0			; separateur survole
hhot		dd 0			; element de l'en-tete survole
cr_n		dd 0			; segments du fil d'Ariane
h_pos		dd -1			; historique
h_n		dd 0
last_idx	dd -1
last_time	dd 0
last_poll	dd 0
vol_sub		dd 0				; abonnement aux volumes (0 : pas de bus)
msg		dd 0
cur_ent		dd 0
vfs_ch		dd -1
vraw_n		dd -1
vol_n		dd 0
pl_n		dd 0
statbuf		dd 0, 0
scr		dd 0, 0, 0, 0
s_title		db 'Fichiers',0
s_back		db 0xE2,0x86,0x90,0
s_next		db 0xE2,0x86,0x92,0
s_up		db 0xE2,0x86,0x91,0
s_sep		db 0xE2,0x80,0xBA,0
s_ell		db 0xE2,0x80,0xA6,0
s_vols		db 'Volumes',0
s_places	db 'Acc',0xC3,0xA8,'s rapides',0
s_k_usb		db 'Cl',0xC3,0xA9,' USB',0
s_k_sd		db 'Carte SD',0
s_k_ram		db 'RAM',0
s_k_img		db 'Image',0
s_k_vol		db 'Volume',0
s_el		db ' ',0xC3,0xA9,'l',0xC3,0xA9,'ment',0
s_els		db ' ',0xC3,0xA9,'l',0xC3,0xA9,'ments',0
s_dash		db '   ',0xE2,0x80,0x94,'   ',0
s_comma		db ', ',0
s_pop1		db 'Supprimer ',0xC2,0xAB,' ',0
s_pop2		db ' ',0xC2,0xBB,0
s_cancel	db 'Annuler',0
s_t_dir		db 'Dossier',0
s_t_app		db 'Application',0
s_t_file	db 'Fichier',0
s_h_name	db 'Nom',0
s_h_size	db 'Taille',0
s_h_type	db 'Type',0
s_asc		db 0xE2,0x96,0xB2,0
s_desc		db 0xE2,0x96,0xBC,0
s_cfg		db 'SD0:/etc/files.ini',0
k_vue		db 'vue',0
s_liste		db 'liste',0
s_cfg_ic	db 'vue = icones',10,0
s_cfg_li	db 'vue = liste',10,0
s_cfg_ta	db 'taille = ',0
s_cfg_ty	db 10,'type = ',0
k_taille	db 'taille',0
k_type		db 'type',0
s_newdir	db 'Nouveau dossier',0
s_launched	db 'Programme lanc',0xC3,0xA9,0
s_cx_open	db 'Ouvrir',0
s_cx_with	db 'Ouvrir avec',0xE2,0x80,0xA6,0
s_cx_del	db 'Supprimer',0
s_deleted	db 'Supprim',0xC3,0xA9,0
s_unmounted	db 'Volume d',0xC3,0xA9,'mont',0xC3,0xA9,0
s_gone		db 'Volume retir',0xC3,0xA9,' : retour ',0xC3,0xA0,' SD0:',0
s_err_dir	db 'Le dossier n',39,'est pas vide',0
s_err_noent	db 'Introuvable',0
s_error		db 'Erreur -',0
s_o		db ' o',0
s_ko		db ' Ko',0
s_mo		db ' Mo',0
		align 8
errbuf		rb 32
vmsg		rb MSG_TAILLE
vraw		rb VMAX*VE_SZ
		align 8
vols		rb VMAX*V_SZ
places		rb PL_MAX*PL_SZ
pl_buf		rb PL_BUF
menu_buf	rb WS_MENU_MAX+16
path		rb 272
fullpath	rb 600
tmpname		rb 272
crbuf		rb 272
stbuf		rb 400
popbuf		rb 400
cellbuf		rb 272
cfgbuf		rb 128
dirent		rb DIRENT_BYTES
		align 8
crumbs		rb CR_MAX*CR_SZ
hist		rb HIST_MAX*HIST_SZ
order		rb MAXE*8
ents		rb MAXE*ENT_SZ
