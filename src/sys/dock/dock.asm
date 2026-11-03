; dock - le dock du bureau Elegant (SD0:/sys/dock, lance par la session).
; Structure du dock d'Onyx (Onyx/user/Apps/dock/main.cpp), rendu modernise
; (docs/DESIGN-ELEGANT.md 8) :
;   * les TIROIRS, un par categorie des applications (inc/dockconf.inc :
;     SD0:/etc/dock.ini en donne l'ordre, l'application principale, celles
;     cachees) : l'icone de l'application principale (un clic la lance ou
;     ramene sa fenetre) ; la languette en haut ouvre le tiroir au-dessus (ses
;     applications par nom, avec leurs icones) ;
;   * les LANCEURS, puis la CORBEILLE ;
;   * au milieu, le PUPITRE : les ESPACES DE TRAVAIL (un carre chacun, leurs
;     fenetres en petit ; un clic y va, le survol dit le nom), verrou,
;     presse-papiers (grises : ces programmes n'existent pas encore), « ⚙ »
;     (le panneau de configuration), alimentation (le dialogue shutdown).
; Un point sous une application ouverte (pour un tiroir : l'une des siennes).
; Tailles d'Onyx selon l'echelle (80 / 60 / 40 en grand), plus petites
; ensemble quand le dock serait plus large que l'ecran, jusqu'a 60 / 34 / 22.
; Fenetre sans cadre, au-dessus des autres, sans focus, transparente hors du
; panneau ; elle reserve le bas de l'ecran.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

SL_MAX		= CA_MAX + DOCK_MAXLAUNCHERS + 1 ; emplacements
SL_TIROIR	= 0
SL_LANCEUR	= 1
SL_CORBEILLE	= 2
SQGAP		= 5			; entre deux carres d'espaces de travail
PGCOL		= 30			; pupitre : colonne des boutons, de chaque cote
HV_BOUTON	= 8			; survol (hover_desk) : 8 + 1 reglages, 8 + 2 alimentation
COTE		= 4			; marge de la fenetre autour du panneau (ombre)
MAXPROCS	= 64
MINI_MAX	= 32			; fenetres dessinees dans les carres
PE_SIZE		= 64			; entree de proclist : nom en +24

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
include '../../inc/applist.inc'
include '../../inc/dockconf.inc'

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,quit
	bl	applis_load
	bl	apps_sort
	bl	load_dock
	mov	x0,-1				; autant d'espaces de travail que nommes
	adr	x1,dl_ndesks
	ldr	x1,[x1]
	bl	ui_desk
	adr	x0,nslots
	ldr	x0,[x0]
	cbz	x0,quit
	bl	layout
	adr	x9,geom				; x, y, largeur, hauteur de la fenetre
	ldp	x0,x1,[x9]
	ldp	x2,x3,[x9,16]
	adr	x4,title
	mov	x5,WF_FRAMELESS or WF_TOP or WF_NOFOCUS or WF_NOLIST or WF_LISTEN or WF_RESERVE or WF_ALPHA
	bl	ui_window_ex
	tbnz	x0,63,quit
	adr	x1,dock_win
	str	x0,[x1]
	bl	refresh
	bl	desks_refresh
	bl	draw
.loop:	mov	x0,1000
	bl	ui_event
	cbz	x0,.idle
	cmp	x0,WEV_WINLIST
	b.eq	.tick
	cmp	x0,WEV_SCREEN
	b.eq	.screen
	cmp	x0,WEV_MOUSE
	b.ne	.loop
	adr	x0,tip_ticks
	str	xzr,[x0]
	adr	x9,ui_ev			; un carre survole : son nom
	ldrsw	x0,[x9,EV_X]
	ldrsw	x1,[x9,EV_Y]
	bl	desk_hit
	tbz	x0,63,.dh
	adr	x9,ui_ev			; un bouton : son nom
	ldrsw	x0,[x9,EV_X]
	ldrsw	x1,[x9,EV_Y]
	bl	desk_bouton
	add	x0,x0,HV_BOUTON
	cmp	x0,HV_BOUTON
	mov	x1,-1
	csel	x0,x1,x0,eq
.dh:	bl	desk_hover
	adr	x9,ui_ev
	ldrsw	x0,[x9,EV_X]
	ldrsw	x1,[x9,EV_Y]
	bl	hit				; x0 = emplacement (-1), x1 = 1 languette
	ldr	w2,[x9,EV_KIND]
	cmp	w2,1
	b.eq	.press
	adr	x2,hover			; survol : redessiner s'il change
	ldp	x3,x4,[x2]
	cmp	x0,x3
	b.ne	.hov
	cmp	x1,x4
	b.eq	.loop
.hov:	stp	x0,x1,[x2]
	bl	draw
	b	.loop
.press:	tbnz	x0,63,.desk
	bl	activate
	b	.loop
.desk:	ldrsw	x0,[x9,EV_X]			; pupitre : un carre y va,
	ldrsw	x1,[x9,EV_Y]
	bl	desk_hit
	tbnz	x0,63,.gear
	mov	x1,0
	bl	ui_desk
	b	.tick
.gear:	adr	x9,ui_ev			; « ⚙ » ouvre les reglages
	ldrsw	x0,[x9,EV_X]
	ldrsw	x1,[x9,EV_Y]
	bl	desk_gear
	b	.loop
.idle:	adr	x0,hover_desk			; (pas de sortie signalee : le nom s'en
	ldr	x1,[x0]				; va apres quelques secondes sans bouger)
	tbnz	x1,63,.tick
	adr	x0,tip_ticks
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	cmp	x1,4
	b.lo	.tick
	mov	x0,-1
	bl	desk_hover
.tick:	bl	refresh				; une application lancee ou finie,
	mov	x19,x0				; un espace de travail, une fenetre
	bl	desks_refresh
	orr	x0,x0,x19
	cbz	x0,.loop
	bl	draw
	b	.loop
.screen:
	bl	screen_follow
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; nouvelle resolution : refait a sa taille, recentre, en bas de l'ecran
screen_follow:
	prologue
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,.out
	bl	layout
	mov	x0,1				; (le dock est plus bas que le minimum)
	mov	x1,1
	bl	ui_min_size
	adr	x9,geom
	ldp	x0,x1,[x9,16]
	bl	ui_resize
	adr	x0,dock_msg
	mov	x1,WS_OP_WIN_PLACE
	str	x1,[x0,WM_OP]
	adr	x1,dock_win
	ldr	x1,[x1]
	str	x1,[x0,WM_A]
	adr	x9,geom
	ldp	x1,x2,[x9]
	stp	x1,x2,[x0,WM_B]
	str	wzr,[x0,WM_LEN]
	sys	SYS_WS_CALL
	bl	draw
.out:	epilogue

; ---------------------------------------------------------------------------
;  Configuration
; ---------------------------------------------------------------------------

; emplacements (dockconf.inc) : un tiroir par categorie affichee qui a des
; applications, les lanceurs de la carte, la corbeille
load_dock:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,nslots
	str	xzr,[x0]
	bl	dock_layout_load
	mov	x19,0
.c:	adr	x0,dl_ncats
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.l
	adr	x0,dl_hidden
	ldrb	w0,[x0,x19]
	cbnz	w0,.cn
	adr	x0,dl_napps
	ldrb	w0,[x0,x19]
	cbz	w0,.cn
	mov	x0,x19
	bl	dl_main
	tbnz	x0,63,.cn
	mov	x2,x0
	adr	x0,dl_cat
	ldrb	w1,[x0,x19]
	mov	x0,SL_TIROIR
	bl	slot_add
.cn:	add	x19,x19,1
	b	.c
.l:	mov	x19,0
.ln:	adr	x0,dl_nlan
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.trash
	adr	x0,dl_lan
	add	x0,x0,x19,lsl 5
	bl	app_find
	tbnz	x0,63,.lc
	mov	x1,x0
	mov	x2,x0
	mov	x0,SL_LANCEUR
	bl	slot_add
.lc:	add	x19,x19,1
	b	.ln
.trash:	adr	x0,dl_corbeille
	ldr	x0,[x0]
	cbz	x0,.out
	mov	x0,SL_CORBEILLE
	mov	x1,0
	mov	x2,0
	bl	slot_add
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = genre, x1 = categorie (tiroir) ou application, x2 = application
; principale : ajoute un emplacement
slot_add:
	adr	x3,nslots
	ldr	x4,[x3]
	cmp	x4,SL_MAX
	b.hs	.ret
	adr	x5,sl_type
	strb	w0,[x5,x4]
	adr	x5,sl_val
	strb	w1,[x5,x4]
	adr	x5,sl_app
	strb	w2,[x5,x4]
	add	x4,x4,1
	str	x4,[x3]
.ret:	ret

; ---------------------------------------------------------------------------
;  Disposition (algorithme du dock d'Onyx, constantes de l'echelle)
; ---------------------------------------------------------------------------
layout:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	ui_echelle			; constantes de l'echelle (ou celle
	adr	x1,dl_taille			; de « taille = » de dock.ini)
	ldr	x1,[x1]
	tbnz	x1,63,.ech
	mov	x0,x1
.ech:
	mov	x1,ECH_SIZE
	adr	x2,ech
	madd	x0,x0,x1,x2
	adr	x1,cur
	mov	x2,0
.cp:	ldr	w3,[x0,x2,lsl 2]
	str	x3,[x1,x2,lsl 3]
	add	x2,x2,1
	cmp	x2,ECH_N
	b.lo	.cp
	adr	x9,cur
	adr	x0,dl_ndesks			; pupitre : 1 rangee jusqu'a 3 espaces,
	ldr	x0,[x0]				; sinon 2
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lt
	mov	x1,1
	cmp	x0,3
	b.ls	.rows
	mov	x1,2
.rows:	adr	x2,pg_rows
	str	x1,[x2]
	add	x0,x0,x1
	sub	x0,x0,1
	udiv	x0,x0,x1
	adr	x2,pg_cols
	str	x0,[x2]
	ldr	x1,[x9,C_SQW]
	add	x1,x1,SQGAP
	mul	x0,x0,x1
	add	x0,x0,2*PGCOL+6-SQGAP
	adr	x2,pgw
	str	x0,[x2]
	adr	x0,nslots
	ldr	x19,[x0]			; n
	add	x20,x19,1			; a gauche : (n + 1) / 2
	lsr	x20,x20,1
	adr	x0,nleft
	str	x20,[x0]
	adr	x0,pgw
	ldr	x21,[x0]			; fixe
	add	x21,x21,24
	cbz	x20,.f1
	add	x21,x21,8
.f1:	cmp	x19,x20
	b.ls	.f2
	add	x21,x21,8
.f2:	adr	x0,scr
	ldr	x22,[x0]
	sub	x22,x22,16			; place
	ldr	x0,[x9,C_CW]
	madd	x1,x19,x0,x21
	cmp	x1,x22
	b.le	.place
	sub	x0,x22,x21			; trop large : tout retrecit ensemble
	udiv	x0,x0,x19
	ldr	x1,[x9,C_CWMIN]
	cmp	x0,x1
	csel	x0,x1,x0,lt
	ldr	x2,[x9,C_CW]			; ICON = max(min, CW x ICON0 / CW0)
	ldr	x3,[x9,C_ICON]
	mul	x3,x3,x0
	udiv	x3,x3,x2
	ldr	x4,[x9,C_ICONMIN]
	cmp	x3,x4
	csel	x3,x4,x3,lt
	str	x3,[x9,C_ICON]
	ldr	x4,[x9,C_DH]			; DH = max(min, DH0 - (CW0 - CW) x
	ldr	x5,[x9,C_DHMIN]			; (DH0 - DHmin) / (CW0 - CWmin))
	sub	x6,x2,x0
	sub	x7,x4,x5
	mul	x6,x6,x7
	sub	x7,x2,x1
	udiv	x6,x6,x7
	sub	x4,x4,x6
	cmp	x4,x5
	csel	x4,x5,x4,lt
	str	x4,[x9,C_DH]
	str	x0,[x9,C_CW]
.place:	ldr	x0,[x9,C_CW]			; positions : lanceurs de gauche,
	mov	x1,12				; pupitre, lanceurs de droite
	mov	x2,0
	adr	x3,sl_x
.lft:	cmp	x2,x20
	b.hs	.pg
	str	x1,[x3,x2,lsl 3]
	add	x1,x1,x0
	add	x2,x2,1
	b	.lft
.pg:	add	x1,x1,8
	adr	x4,pgx
	str	x1,[x4]
	adr	x4,pgw
	ldr	x4,[x4]
	add	x1,x1,x4
	add	x1,x1,8
.rgt:	cmp	x2,x19
	b.hs	.dw
	str	x1,[x3,x2,lsl 3]
	add	x1,x1,x0
	add	x2,x2,1
	b	.rgt
.dw:	add	x1,x1,12			; largeur du panneau
	adr	x4,dw_
	str	x1,[x4]
	adr	x5,scr
	ldp	x6,x7,[x5]			; ecran
	sub	x8,x6,x1			; fenetre : centree en bas, ombre autour
	lsr	x8,x8,1
	sub	x8,x8,COTE
	ldr	x10,[x9,C_DH]
	ldr	x11,[x9,C_GAP]
	add	x12,x10,x11
	add	x12,x12,COTE
	sub	x13,x7,x12
	add	x14,x1,2*COTE
	adr	x5,geom
	stp	x8,x13,[x5]
	stp	x14,x12,[x5,16]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = point dans la fenetre -> x0 = emplacement (-1 : aucun), x1 = 1 sur
; la languette d'un tiroir
hit:
	sub	x0,x0,COTE
	sub	x1,x1,COTE
	mov	x2,0
	adr	x3,nslots
	ldr	x3,[x3]
	adr	x4,cur
	ldr	x5,[x4,C_CW]
	ldr	x6,[x4,C_DH]
	cmp	x1,0
	b.lt	.none
	cmp	x1,x6
	b.ge	.none
	adr	x4,sl_x
.s:	cmp	x2,x3
	b.hs	.none
	ldr	x7,[x4,x2,lsl 3]
	cmp	x0,x7
	b.lt	.n
	add	x7,x7,x5
	cmp	x0,x7
	b.ge	.n
	mov	x0,x2
	adr	x4,sl_type			; languette : un tiroir, en haut
	ldrb	w4,[x4,x2]
	mov	x2,0
	cmp	w4,SL_TIROIR
	b.ne	.r
	adr	x4,cur
	ldr	x4,[x4,C_STRIP]
	add	x4,x4,2
	cmp	x1,x4
	cset	x2,lt
.r:	mov	x1,x2
	ret
.n:	add	x2,x2,1
	b	.s
.none:	mov	x0,-1
	mov	x1,0
	ret

; ---------------------------------------------------------------------------
;  Applications ouvertes
; ---------------------------------------------------------------------------

; pids des applications qui tournent (proclist : le noyau nomme le process
; d'un paquet d'apres lui) ->
; x0 = 1 si quelque chose a change
refresh:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	sub	sp,sp,48
	adr	x0,procs
	mov	x1,MAXPROCS
	sys	SYS_PROCLIST
	cmp	x0,0
	csel	x21,x0,xzr,gt			; process listes
	mov	x23,0				; change ?
	mov	x19,0				; application
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	mov	x0,sp				; son nom
	strb	wzr,[x0]
	adr	x1,ap_nom
	add	x1,x1,x19,lsl 5
	bl	str_cat
	mov	x22,0				; pid trouve
	adr	x20,procs
	mov	x9,0
.p:	cmp	x9,x21
	b.hs	.pdone
	str	x9,[sp,40]
	add	x0,x20,24
	mov	x1,sp
	bl	str_eq
	ldr	x9,[sp,40]
	cbz	x0,.pn
	ldr	w22,[x20]
	b	.pdone
.pn:	add	x20,x20,PE_SIZE
	add	x9,x9,1
	b	.p
.pdone:	adr	x0,pids
	ldr	x1,[x0,x19,lsl 3]
	cmp	x1,x22
	cinc	x23,x23,ne
	str	x22,[x0,x19,lsl 3]
	add	x19,x19,1
	b	.a
.out:	mov	x0,x23
	add	sp,sp,48
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = emplacement -> x0 = 1 si une de ses applications tourne
slot_running:
	adr	x1,sl_type
	ldrb	w2,[x1,x0]
	cmp	w2,SL_CORBEILLE
	b.eq	.no
	adr	x1,sl_val
	ldrb	w3,[x1,x0]
	adr	x4,pids
	cmp	w2,SL_LANCEUR
	b.ne	.cat
	ldr	x0,[x4,x3,lsl 3]
	cmp	x0,0
	cset	x0,ne
	ret
.cat:	mov	x5,0				; tiroir : une application de la categorie
	adr	x6,ap_n
	ldr	x6,[x6]
	adr	x7,ap_cat
.c:	cmp	x5,x6
	b.hs	.no
	ldrb	w8,[x7,x5]
	cmp	w8,w3
	b.ne	.cn
	ldr	x0,[x4,x5,lsl 3]
	cbnz	x0,.yes
.cn:	add	x5,x5,1
	b	.c
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = application : ramene sa fenetre (reduite aussi), ou la lance
app_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,pids
	ldr	x20,[x0,x19,lsl 3]
	cbz	x20,.launch
	mov	x21,0				; ses fenetres : la premiere
.page:	mov	x0,x21
	adr	x1,wlist
	bl	ui_winlist
	cmp	x0,x21
	b.le	.launch				; pas de fenetre : relancer
	sub	x22,x0,x21
	cmp	x22,WL_PER_MSG
	mov	x1,WL_PER_MSG
	csel	x22,x1,x22,gt
	adr	x2,wlist
.e:	cbz	x22,.np
	ldr	w3,[x2,WL_PID]
	cmp	x3,x20
	b.eq	.raise
	add	x2,x2,WL_SIZE
	sub	x22,x22,1
	b	.e
.np:	add	x21,x21,WL_PER_MSG
	b	.page
.raise:	ldr	x0,[x2,WL_ID]
	bl	ui_raise
	b	.out
.launch:
	mov	x0,x19
	bl	app_launch
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = emplacement, x1 = 1 languette : agit
activate:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x2,sl_type
	ldrb	w2,[x2,x19]
	cmp	w2,SL_CORBEILLE
	b.eq	.trash
	cbnz	x1,.drawer
	adr	x0,sl_app			; lanceur, ou application principale
	ldrb	w0,[x0,x19]
	bl	app_open
	b	.out
.trash:	adr	x0,s_fichiers			; la corbeille : dans Fichiers
	bl	app_find
	tbnz	x0,63,.out
	bl	app_launch
	b	.out
.drawer:
	mov	x0,x19
	bl	open_drawer
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = emplacement (tiroir) : ouvre le tiroir au-dessus de la languette
; (menu des applications de la categorie, avec leurs icones)
open_drawer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	adr	x0,open_slot			; languette ouverte (accent)
	str	x19,[x0]
	bl	draw
	adr	x0,sl_val
	ldrb	w20,[x0,x19]			; categorie
	adr	x21,items			; « nom<tab>libelle », a la suite
	mov	x22,0				; applications listees
	mov	x23,0				; (par nom)
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x23,x0
	b.hs	.end
	adr	x0,ap_ordre
	ldrb	w9,[x0,x23]
	add	x23,x23,1
	adr	x0,ap_cat
	ldrb	w0,[x0,x9]
	cmp	w0,w20
	b.ne	.an
	str	x9,[sp,-16]!
	adr	x0,dr_map			; numero du libelle -> application
	strb	w9,[x0,x22]
	add	x22,x22,1
	mov	x0,x21
	adr	x1,ap_nom
	add	x1,x1,x9,lsl 5
	bl	str_copy_n_
	mov	w1,9
	strb	w1,[x0],1
	ldr	x9,[sp]
	adr	x1,ap_lib
	mov	x2,AP_LIB
	madd	x1,x9,x2,x1
	bl	str_copy_n_
	add	x21,x0,1
	ldr	x9,[sp],16
.an:	b	.a
.end:	strb	wzr,[x21]
	cbz	x22,.close
	adr	x9,cur				; au-dessus du dock, centre sur le lanceur
	ldr	x0,[x9,C_CW]
	adr	x1,sl_x
	ldr	x1,[x1,x19,lsl 3]
	add	x0,x1,x0,lsr 1
	adr	x1,geom
	ldp	x2,x3,[x1]
	add	x0,x0,x2
	add	x0,x0,COTE
	sub	x0,x0,DRAWER_W/2
	bl	ui_echelle			; hauteur : lignes + marges
	mov	x1,x0
	lsl	x1,x1,1
	mov	x2,28
	sub	x1,x2,x1
	mul	x1,x1,x22
	add	x1,x1,12
	adr	x2,geom
	ldr	x3,[x2,8]
	add	x3,x3,COTE
	sub	x3,x3,12
	sub	x1,x3,x1			; y du corps
	adr	x9,cur
	ldr	x0,[x9,C_CW]
	adr	x2,sl_x
	ldr	x2,[x2,x19,lsl 3]
	add	x0,x2,x0,lsr 1
	adr	x2,geom
	ldr	x2,[x2]
	add	x0,x0,x2
	add	x0,x0,COTE
	sub	x0,x0,DRAWER_W/2
	adr	x2,items
	mov	x3,DRAWER_W
	mov	x4,1				; avec icones
	bl	ui_popup_ex
	tbnz	x0,63,.close
	adr	x1,dr_map
	ldrb	w0,[x1,x0]
	bl	app_open
.close:	adr	x0,open_slot
	mov	x1,-1
	str	x1,[x0]
	bl	draw
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source : copie, x0 = fin (sur le NUL)
str_copy_n_:
	ldrb	w2,[x1],1
	strb	w2,[x0]
	cbz	w2,.ret
	add	x0,x0,1
	b	str_copy_n_
.ret:	ret

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------
draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,32
	bl	ui_jetons
	mov	x27,x0
	adr	x9,cur
	ldr	x22,[x9,C_DH]
	adr	x0,dw_
	ldr	x21,[x0]
	mov	x0,0				; tout transparent
	mov	x1,0
	adr	x2,geom
	ldp	x2,x3,[x2,16]
	movl	w4,0xFF000000
	bl	ui_fill
	ldr	w23,[x27,TJ_DOCK]		; face (avec sa transparence)
	add	x0,x21,2*COTE			; ombre, contour
	add	x1,x22,2*COTE
	bl	ui_ombre
	and	w0,w23,0xFFFFFF			; bord 1 px
	mov	x1,92
	bl	ui_tone
	ldr	w1,[x27,TJ_SOMBRE]
	cbz	w1,.edge
	ldr	w0,[x27,TJ_LIGNE]
.edge:	movl	w1,0x4D000000
	orr	w5,w0,w1
	mov	w6,w5
	mov	x0,COTE
	mov	x1,COTE
	mov	x2,x21
	mov	x3,x22
	adr	x4,cur
	ldr	x4,[x4,C_R]
	bl	ui_rbox
	and	w0,w23,0xFFFFFF			; face : plate (8 niveaux d'ecart)
	mov	x1,124
	bl	ui_tone
	and	w1,w23,0xFF000000
	orr	w0,w0,w1
	str	x0,[sp]
	and	w0,w23,0xFFFFFF
	mov	x1,132
	bl	ui_tone
	and	w1,w23,0xFF000000
	orr	w5,w0,w1
	ldr	x6,[sp]
	mov	x0,COTE+1
	mov	x1,COTE+1
	sub	x2,x21,2
	sub	x3,x22,2
	adr	x4,cur
	ldr	x4,[x4,C_R]
	sub	x4,x4,1
	bl	ui_rbox
	ldr	w0,[x27,TJ_SOMBRE]		; clair : reflet en haut
	cbnz	w0,.slots
	adr	x9,cur
	ldr	x0,[x9,C_R]
	add	x0,x0,COTE
	mov	x1,COTE+1
	sub	x2,x21,x0,lsl 1
	add	x2,x2,2*COTE
	mov	x3,1
	movl	w4,0x4DFFFFFF
	bl	ui_fill
.slots:	mov	x19,0				; --- emplacements
.s:	adr	x0,nslots
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.desk
	adr	x9,cur
	ldr	x24,[x9,C_CW]
	adr	x0,sl_x
	ldr	x20,[x0,x19,lsl 3]
	add	x20,x20,COTE			; x du lanceur dans la fenetre
	adr	x0,hover			; survol
	ldp	x1,x2,[x0]
	cmp	x1,x19
	b.ne	.icon
	cbnz	x2,.icon
	and	w0,w23,0xFFFFFF
	mov	x1,150
	ldr	w2,[x27,TJ_SOMBRE]
	cbz	w2,.hc
	mov	x1,150
.hc:	bl	ui_tone
	and	w1,w23,0xFF000000
	orr	w5,w0,w1
	mov	w6,w5
	add	x0,x20,4
	mov	x1,COTE+19
	sub	x2,x24,8
	sub	x3,x22,26
	mov	x4,10
	bl	ui_rbox
.icon:	adr	x9,cur				; icone
	ldr	x25,[x9,C_ICON]
	sub	x0,x24,x25
	add	x26,x20,x0,lsr 1		; x de l'icone
	sub	x0,x22,x25
	ldr	x1,[x9,C_BAS]
	sub	x0,x0,x1
	add	x28,x0,COTE			; y de l'icone
	adr	x0,sl_type
	ldrb	w0,[x0,x19]
	adr	x1,s_corbeille
	cmp	w0,SL_CORBEILLE
	b.eq	.ic
	adr	x1,sl_app
	ldrb	w1,[x1,x19]
	adr	x2,ap_nom
	add	x1,x2,x1,lsl 5
.ic:	mov	x0,x1
	mov	x1,x26
	mov	x2,x28
	mov	x3,x25
	bl	ui_icone
	adr	x0,sl_type			; languette d'un tiroir
	ldrb	w0,[x0,x19]
	cmp	w0,SL_TIROIR
	b.ne	.dot
	add	x26,x20,x24,lsr 1		; centre du lanceur
	ldr	w0,[x27,TJ_SOMBRE]
	cbz	w0,.tl
	ldr	w0,[x27,TJ_ELEVE]
	b	.tc
.tl:	and	w0,w23,0xFFFFFF
	mov	x1,112
	bl	ui_tone
.tc:	adr	x1,hover
	ldp	x2,x3,[x1]
	cmp	x2,x19
	b.ne	.to
	cbz	x3,.to
	mov	x1,100				; survol de la languette
	bl	ui_tone
.to:	adr	x1,open_slot
	ldr	x1,[x1]
	cmp	x1,x19
	b.ne	.tab
	ldr	w0,[x27,TJ_ACCENT]
.tab:	str	x0,[sp]
	mov	w5,w0
	mov	w6,w0
	mov	x2,36				; 36 x 12 (plus etroite si CW < 60)
	cmp	x24,60
	b.hs	.tw
	sub	x2,x24,8
.tw:	sub	x0,x26,x2,lsr 1
	mov	x1,COTE+1
	mov	x3,12
	mov	x4,5
	bl	ui_rbox
	ldr	w0,[x27,TJ_TEXTE2]		; chevron vers le haut (ouvert : bas)
	adr	x1,open_slot
	ldr	x1,[x1]
	cmp	x1,x19
	b.ne	.chv
	mov	w0,0x00FFFFFF
.chv:	str	x0,[sp,8]
	mov	x21,0				; (x21 reprend la largeur plus bas)
.cr:	ldr	x4,[sp,8]			; rangee j : deux points 2 x 1
	sub	x0,x26,x21
	sub	x0,x0,2
	mov	x1,COTE+5
	adr	x2,open_slot
	ldr	x2,[x2]
	cmp	x2,x19
	b.ne	.up
	add	x1,x1,3				; ouvert : vers le bas
	sub	x1,x1,x21
	sub	x1,x1,x21
.up:	add	x1,x1,x21
	str	x1,[sp,16]
	mov	x2,2
	mov	x3,1
	bl	ui_fill
	ldr	x4,[sp,8]
	add	x0,x26,x21
	ldr	x1,[sp,16]
	mov	x2,2
	mov	x3,1
	bl	ui_fill
	add	x21,x21,1
	cmp	x21,4
	b.lo	.cr
	adr	x0,dw_
	ldr	x21,[x0]
.dot:	mov	x0,x19				; point d'application ouverte
	bl	slot_running
	cbz	x0,.sep
	add	x0,x20,x24,lsr 1
	sub	x0,x0,2
	sub	x1,x22,9-COTE
	mov	x2,5
	mov	x3,5
	mov	x4,2
	ldr	w5,[x27,TJ_ACCENT_VIF]
	mov	w6,w5
	bl	ui_rbox
.sep:	adr	x0,nslots			; separateur avec le voisin
	ldr	x0,[x0]
	sub	x0,x0,1
	cmp	x19,x0
	b.hs	.next
	adr	x0,nleft
	ldr	x0,[x0]
	sub	x0,x0,1
	cmp	x19,x0
	b.eq	.next
	bl	sep_colour
	mov	w4,w0
	add	x0,x20,x24
	sub	x0,x0,1
	mov	x1,COTE+14
	mov	x2,1
	sub	x3,x22,28
	bl	ui_fill
.next:	add	x19,x19,1
	b	.s
.desk:	bl	draw_desk
	bl	ui_update_all
	add	sp,sp,32
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> w0 = couleur des separateurs (avec transparence 0x66)
sep_colour:
	prologue
	bl	ui_jetons
	ldr	w1,[x0,TJ_SOMBRE]
	ldr	w2,[x0,TJ_LIGNE]
	ldr	w0,[x0,TJ_DOCK]
	cbnz	w1,.d
	and	w0,w0,0xFFFFFF
	mov	x1,100
	bl	ui_tone
	b	.a
.d:	mov	w0,w2
.a:	movl	w1,0x66000000
	orr	w0,w0,w1
	epilogue

; x0 = espace -> x0, x1 = son carre (coordonnees du panneau)
desk_box:
	adr	x2,pg_cols
	ldr	x2,[x2]
	adr	x3,cur
	ldp	x4,x5,[x3,C_SQW]		; C_SQW, C_SQH
	ldr	x6,[x3,C_DH]
	udiv	x7,x0,x2			; rangee
	msub	x8,x7,x2,x0			; colonne
	add	x0,x4,SQGAP
	mul	x0,x0,x8
	add	x0,x0,PGCOL
	adr	x2,pgx
	ldr	x2,[x2]
	add	x0,x0,x2
	adr	x2,pg_rows
	ldr	x2,[x2]
	cmp	x2,1
	b.ne	.two
	sub	x1,x6,x5
	lsr	x1,x1,1
	ret
.two:	sub	x1,x6,x5,lsl 1
	sub	x1,x1,6
	lsr	x1,x1,1
	add	x2,x5,6
	madd	x1,x7,x2,x1
	ret

; x0, x1 = point dans la fenetre -> x0 = espace sous le point (-1 : aucun)
desk_hit:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	sub	x19,x0,COTE
	sub	x20,x1,COTE
	mov	x21,0
.d:	adr	x0,dl_ndesks
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.none
	mov	x0,x21
	bl	desk_box
	adr	x2,cur
	ldp	x3,x4,[x2,C_SQW]
	sub	x0,x19,x0			; (2 px autour)
	add	x0,x0,2
	sub	x1,x20,x1
	add	x1,x1,2
	add	x3,x3,4
	add	x4,x4,4
	cmp	x0,x3
	b.hs	.n
	cmp	x1,x4
	b.lo	.got
.n:	add	x21,x21,1
	b	.d
.got:	mov	x0,x21
	b	.out
.none:	mov	x0,-1
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = espace survole (-1 : aucun) : son nom au-dessus du dock, le carre eclaire
desk_hover:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,hover_desk
	ldr	x1,[x0]
	cmp	x1,x19
	b.eq	.out
	str	x19,[x0]
	tbz	x19,63,.tip
	mov	x0,0
	mov	x1,0
	mov	x2,0
	bl	ui_tip_at
	b	.draw
.tip:	cmp	x19,HV_BOUTON
	b.lo	.sq
	adr	x0,pgx				; un bouton du pupitre : son nom
	ldr	x0,[x0]
	add	x0,x0,COTE
	adr	x2,s_reglages
	bl	tr_x2
	cmp	x19,HV_BOUTON+1
	b.eq	.btn
	adr	x1,pgw
	ldr	x1,[x1]
	add	x0,x0,x1
	sub	x0,x0,PGCOL+30
	adr	x2,s_eteindre
	bl	tr_x2
.btn:	mov	x1,-28
	bl	ui_tip_at
	b	.draw
.sq:	adr	x0,tip_text			; « Bureau 2 : son nom »
	strb	wzr,[x0]
	adr	x1,s_bureau
	bl	tr_x1
	bl	str_cat
	add	w1,w19,'1'
	strb	w1,[x0],1
	strb	wzr,[x0]
	mov	x1,DL_DESK
	adr	x2,dl_desk
	madd	x20,x19,x1,x2
	ldrb	w1,[x20]
	cbz	w1,.pos
	add	w2,w19,'1'			; (le nom d'origine, son numero : rien de plus)
	cmp	w1,w2
	b.ne	.nom
	ldrb	w1,[x20,1]
	cbz	w1,.pos
.nom:	adr	x1,s_nom
	bl	str_cat
	mov	x1,x20
	bl	str_cat
.pos:	mov	x0,x19
	bl	desk_box
	add	x0,x0,COTE
	mov	x1,-28
	adr	x2,tip_text
	bl	ui_tip_at
.draw:	bl	draw
.out:	ldp	x19,x20,[sp],16
	epilogue

; les fenetres, en petit dans les carres -> x0 = 1 si quelque chose a change
desks_refresh:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x0,-1
	mov	x1,0
	bl	ui_desk
	and	x23,x0,0xFFFF			; (empreinte : l'espace, les fenetres)
	adr	x1,desk_cour
	and	x0,x0,0xFF
	str	x0,[x1]
	mov	x19,0				; premiere de la page
	mov	x21,0				; fenetres gardees
.page:	mov	x0,x19
	adr	x1,wdet
	bl	ui_win_details
	cmp	x0,x19
	b.le	.fin
	sub	x20,x0,x19
	cmp	x20,WE_PER_MSG
	mov	x1,WE_PER_MSG
	csel	x20,x1,x20,gt
	adr	x22,wdet
.e:	cbz	x20,.np
	ldr	w0,[x22,WE_FLAGS]
	mov	x1,WF_FRAMELESS or WF_TOP or WF_BACK or WF_NOLIST
	tst	x0,x1
	b.ne	.en
	ldr	w2,[x22,WE_STATE]
	tbnz	x2,2,.en			; (WES_REDUITE)
	cmp	x21,MINI_MAX
	b.hs	.en
	adr	x3,minis
	add	x3,x3,x21,lsl 4
	add	x21,x21,1
	ubfx	x4,x2,WES_DESK,8		; espace + 1
	and	x5,x2,WES_KEYS
	orr	x4,x4,x5,lsl 8
	strh	w4,[x3]
	ldrsh	x5,[x22,WE_X]
	strh	w5,[x3,2]
	ldrsh	x6,[x22,WE_Y]
	strh	w6,[x3,4]
	ldrh	w7,[x22,WE_W]
	strh	w7,[x3,6]
	ldrh	w8,[x22,WE_H]
	strh	w8,[x3,8]
	mov	x0,33
	mul	x23,x23,x0
	add	x23,x23,x4
	eor	x23,x23,x5,lsl 8
	eor	x23,x23,x6,lsl 20
	eor	x23,x23,x7,lsl 32
	eor	x23,x23,x8,lsl 44
.en:	add	x22,x22,WE_SIZE
	sub	x20,x20,1
	b	.e
.np:	add	x19,x19,WE_PER_MSG
	b	.page
.fin:	adr	x0,nminis
	str	x21,[x0]
	adr	x0,minis_sig
	ldr	x1,[x0]
	str	x23,[x0]
	cmp	x1,x23
	cset	x0,ne
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = point dans la fenetre -> x0 = bouton du pupitre : 1 « ⚙ » (en bas a
; gauche), 2 alimentation (en haut a droite), 0 aucun
desk_bouton:
	adr	x2,pgx
	ldr	x2,[x2]
	add	x2,x2,COTE
	subs	x0,x0,x2
	b.lt	.non
	adr	x2,cur
	ldr	x2,[x2,C_DH]
	subs	x1,x1,COTE
	b.lt	.non
	cmp	x1,x2
	b.ge	.non
	lsr	x2,x2,1
	cmp	x0,PGCOL
	b.ge	.droite
	cmp	x1,x2
	b.lt	.non
	mov	x0,1
	ret
.droite:
	cmp	x1,x2
	b.ge	.non
	adr	x2,pgw
	ldr	x2,[x2]
	cmp	x0,x2
	b.ge	.non
	sub	x2,x2,PGCOL
	cmp	x0,x2
	b.lt	.non
	mov	x0,2
	ret
.non:	mov	x0,0
	ret

; x0, x1 = clic hors des emplacements : « ⚙ » lance le panneau de
; configuration, l'alimentation le dialogue de fin de session
desk_gear:
	prologue
	bl	desk_bouton
	cbz	x0,.out
	adr	x1,p_shutdown
	cmp	x0,2
	b.eq	.go
	adr	x1,p_control
.go:	mov	x0,x1
	mov	x1,0
	sys	SYS_SPAWN
.out:	epilogue

; x0 = espace : son carre, ses fenetres en petit (son numero s'il n'en a pas)
draw_square:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	bl	desk_box
	add	x20,x0,COTE			; x, y du carre
	add	x21,x1,COTE
	adr	x0,cur
	ldp	x22,x23,[x0,C_SQW]		; largeur, hauteur
	bl	ui_jetons
	mov	x24,x0
	adr	x0,desk_cour
	ldr	x25,[x0]
	cmp	x25,x19
	cset	x25,eq				; x25 : le courant
	ldr	w0,[x24,TJ_ACCENT]		; contour
	cbnz	x25,.cont
	ldr	w0,[x24,TJ_DOCK]
	and	w0,w0,0xFFFFFF
	mov	x1,70
	bl	ui_tone
.cont:	mov	w5,w0
	mov	w6,w0
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	mov	x4,5
	bl	ui_rbox
	cbz	x25,.off			; fond du carre
	ldr	w0,[x24,TJ_ACCENT]
	mov	w1,0x00FFFFFF
	mov	x2,196
	ldr	w3,[x24,TJ_SOMBRE]
	cbz	w3,.mix
	ldr	w0,[x24,TJ_ACCENT_CLAIR]
	mov	x2,0
.mix:	bl	ui_mix
	b	.wall
.off:	ldr	w0,[x24,TJ_DOCK]
	and	w0,w0,0xFFFFFF
	mov	x1,128
	adr	x2,hover_desk
	ldr	x2,[x2]
	cmp	x2,x19
	b.ne	.tone
	mov	x1,150
.tone:	bl	ui_tone
.wall:	mov	w26,w0
	mov	w5,w0
	mov	w6,w0
	add	x0,x20,1
	add	x1,x21,1
	sub	x2,x22,2
	sub	x3,x23,2
	mov	x4,4
	bl	ui_rbox
	mov	x27,0				; ses fenetres
	mov	x28,0				; (en a-t-il ?)
.m:	adr	x0,nminis
	ldr	x0,[x0]
	cmp	x27,x0
	b.hs	.num
	adr	x0,minis
	add	x8,x0,x27,lsl 4
	add	x27,x27,1
	ldrh	w0,[x8]
	and	x1,x0,0xFF			; espace + 1 (0 : tous)
	cbnz	x1,.one
	cbz	x25,.m				; (sur tous : dans le courant)
	b	.mini
.one:	sub	x1,x1,1
	cmp	x1,x19
	b.ne	.m
.mini:	adr	x9,scr
	ldp	x10,x11,[x9]			; ecran
	ldr	x12,[x9,24]			; barre du haut
	sub	x11,x11,x12
	sub	x13,x22,4			; place dans le carre
	sub	x14,x23,4
	ldrsh	x0,[x8,2]			; x0
	mul	x1,x0,x13
	sdiv	x1,x1,x10
	ldrh	w2,[x8,6]			; x1
	add	x2,x0,x2
	mul	x2,x2,x13
	sdiv	x2,x2,x10
	ldrsh	x0,[x8,4]			; y0
	sub	x0,x0,x12
	mul	x3,x0,x14
	sdiv	x3,x3,x11
	ldrh	w4,[x8,8]			; y1
	add	x4,x0,x4
	mul	x4,x4,x14
	sdiv	x4,x4,x11
	cmp	x1,0				; dans le carre
	csel	x1,xzr,x1,lt
	cmp	x3,0
	csel	x3,xzr,x3,lt
	cmp	x2,x13
	csel	x2,x13,x2,gt
	cmp	x4,x14
	csel	x4,x14,x4,gt
	sub	x2,x2,x1			; largeur, hauteur
	sub	x4,x4,x3
	cmp	x2,3
	b.lt	.m
	cmp	x4,3
	b.lt	.m
	mov	x28,1
	add	x1,x1,x20			; x, y
	add	x1,x1,2
	add	x3,x3,x21
	add	x3,x3,2
	stp	x1,x3,[sp,-16]!
	stp	x2,x4,[sp,-16]!
	ldrh	w0,[x8]				; le clavier : le cadre actif
	ldr	w5,[x24,TJ_TITRE_INACTIF]
	tbz	w0,8,.fi
	cbz	x25,.fi
	ldr	w5,[x24,TJ_TITRE_ACTIF]
.fi:	and	w0,w5,0xFFFFFF
	str	x0,[sp,-16]!
	mov	x1,176
	bl	ui_tone
	mov	w4,w0
	ldp	x2,x3,[sp,16]			; largeur, hauteur
	ldp	x0,x1,[sp,32]
	bl	ui_fill
	ldr	x0,[sp]
	mov	x1,70
	bl	ui_tone
	mov	w4,w0
	ldp	x2,x3,[sp,16]
	ldp	x0,x1,[sp,32]
	bl	ui_frame
	ldr	x4,[sp]
	ldr	x2,[sp,16]
	ldp	x0,x1,[sp,32]
	add	x0,x0,1
	add	x1,x1,1
	sub	x2,x2,2
	mov	x3,2
	bl	ui_fill
	add	sp,sp,48
	b	.m
.num:	cbnz	x28,.out			; vide : son numero
	mov	w0,w26
	bl	ui_ink
	mov	w27,w0
	cbnz	x25,.nt
	mov	w0,w26
	mov	w1,w27
	mov	x2,150
	bl	ui_mix
	mov	w27,w0
.nt:	adr	x0,tip_num
	add	w1,w19,'1'
	strb	w1,[x0]
	strb	wzr,[x0,1]
	mov	x1,12
	adr	x2,ui_font
	cbz	x25,.w
	adr	x2,ui_font_b
.w:	bl	ui_text_width
	sub	x1,x22,x0
	add	x1,x20,x1,lsr 1
	add	x2,x21,x23,lsr 1
	add	x2,x2,4
	adr	x0,tip_num
	mov	w3,w27
	mov	x4,12
	adr	x5,ui_font
	cbz	x25,.t
	adr	x5,ui_font_b
.t:	mov	x6,0
	bl	ui_text
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; le pupitre : les espaces de travail, les boutons systeme (grises, sauf
; « ⚙ » : le panneau de configuration)
draw_desk:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	ui_jetons
	mov	x23,x0
	adr	x0,pgw
	ldr	x24,[x0]			; largeur du pupitre
	adr	x0,pgx
	ldr	x19,[x0]
	add	x19,x19,COTE			; x du pupitre
	adr	x0,cur
	ldr	x20,[x0,C_DH]
	bl	sep_colour			; separateurs de part et d'autre
	mov	w4,w0
	sub	x0,x19,5
	mov	x1,COTE+14
	mov	x2,1
	sub	x3,x20,28
	bl	ui_fill
	bl	sep_colour
	mov	w4,w0
	add	x0,x19,x24
	add	x0,x0,3
	mov	x1,COTE+14
	mov	x2,1
	sub	x3,x20,28
	bl	ui_fill
	ldr	w0,[x23,TJ_DOCK]		; fond du pupitre
	and	w0,w0,0xFFFFFF
	mov	x1,112
	bl	ui_tone
	ldr	w1,[x23,TJ_SOMBRE]
	cbz	w1,.f
	ldr	w0,[x23,TJ_FACE]
.f:	movl	w1,0x40000000
	orr	w5,w0,w1
	mov	w6,w5
	mov	x0,x19
	mov	x1,COTE+8
	mov	x2,x24
	sub	x3,x20,16
	mov	x4,10
	bl	ui_rbox
	mov	x21,0				; les espaces de travail
.sq:	adr	x0,dl_ndesks
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.btn
	mov	x0,x21
	bl	draw_square
	add	x21,x21,1
	b	.sq
.btn:
	ldr	w0,[x23,TJ_TEXTE3]		; boutons systeme, grises : verrou,
	ldr	w1,[x23,TJ_DOCK]		; presse-papiers
	and	w1,w1,0xFFFFFF
	mov	x2,110
	bl	ui_mix
	mov	w21,w0
	mul	x0,x20,x20			; (centres a DH x 27/80 et DH x 53/80)
	mov	x22,27
	mul	x22,x22,x20
	mov	x0,80
	udiv	x22,x22,x0
	add	x22,x22,COTE
	adr	x0,s_g_lock			; verrou (grise)
	add	x1,x19,15-8
	sub	x2,x22,8
	mov	x3,16
	mov	w4,w21
	bl	ui_glyph
	adr	x0,s_g_power			; alimentation (actif)
	add	x1,x19,x24
	sub	x1,x1,17+8
	sub	x2,x22,8
	mov	x3,16
	ldr	w4,[x23,TJ_DANGER]
	bl	ui_glyph
	mov	x22,53				; rangee du bas
	mul	x22,x22,x20
	mov	x0,80
	udiv	x22,x22,x0
	add	x22,x22,COTE
	ldr	w4,[x23,TJ_TEXTE1]		; reglages (actif)
	ldr	w0,[x23,TJ_SOMBRE]
	cbz	w0,.g
	ldr	w4,[x23,TJ_TEXTE2]
.g:	adr	x0,s_g_gear
	add	x1,x19,15-9
	sub	x2,x22,9
	mov	x3,18
	bl	ui_glyph
	adr	x0,s_g_clipboard		; presse-papiers (grise)
	add	x1,x19,x24
	sub	x1,x1,17+8
	sub	x2,x22,8
	mov	x3,16
	mov	w4,w21
	bl	ui_glyph
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; constantes des echelles (L, M, P) : DH, CW, ICON, DHmin, CWmin, ICONmin,
; GAP, rayon, marge sous l'icone, languette, carre d'espace (l x h)
ECH_N		= 12
ECH_SIZE	= ECH_N * 4
C_DH		= 0
C_CW		= 8
C_ICON		= 16
C_DHMIN		= 24
C_CWMIN		= 32
C_ICONMIN	= 40
C_GAP		= 48
C_R		= 56
C_BAS		= 64
C_STRIP		= 72
C_SQW		= 80
C_SQH		= 88
ech:	dw 80, 60, 40, 60, 34, 22, 12, 18, 14, 16, 34, 25
	dw 72, 56, 40, 56, 34, 22, 6, 16, 14, 16, 30, 22
	dw 64, 50, 36, 52, 34, 22, 4, 14, 12, 14, 26, 19
DRAWER_W	= 200

title		db 'Dock',0
s_fichiers	db 'files',0
s_bureau	db 'Bureau ',0
s_nom		db ' : ',0
s_corbeille	db 'corbeille',0
p_control	db 'SD0:/apps/control.app/main',0
p_shutdown	db 'SD0:/sys/shutdown',0
s_g_lock	db 'glyph-lock',0
s_g_power	db 'glyph-power',0
s_g_gear	db 'glyph-gear',0
s_g_clipboard	db 'glyph-clipboard',0
s_reglages	db 'R',0xC3,0xA9,'glages',0
s_eteindre	db 0xC3,0x89,'teindre',0
		align 4

include '../../inc/tr.inc'

segment readable writeable
kit_data
ui_data
applis_data
dockconf_data
		align 8
scr		rb 32
geom		rd 4				; fenetre : x, y, largeur, hauteur
dock_win	dd 0				; sa poignee
dock_msg	rb WS_MSG
cur		rd ECH_N			; constantes courantes (C_*)
nslots		dd 0
nleft		dd 0
pgx		dd 0
dw_		dd 0				; largeur du panneau
pgw		dd 0				; pupitre : largeur, colonnes, rangees
pg_cols		dd 1
pg_rows		dd 1
desk_cour	dd 0				; espace de travail montre
hover_desk	dd -1				; carre survole
tip_ticks	dd 0				; secondes sans mouvement
hover		dd -1, 0			; emplacement survole, languette
open_slot	dd -1				; tiroir ouvert
sl_x		rd SL_MAX
sl_type		rb SL_MAX
sl_val		rb SL_MAX
sl_app		rb SL_MAX
dr_map		rb AP_MAX
		align 8
pids		rd AP_MAX
wlist		rb WL_PER_MSG * WL_SIZE
items		rb AP_MAX * (AP_NOM + AP_LIB + 2) + 1
procs		rb MAXPROCS * PE_SIZE
nminis		dd 0				; les fenetres, en petit (16 octets : espace + 1
minis_sig	dd 0				; | clavier << 8, x, y, l, h en 16 bits)
minis		rb MINI_MAX * 16
wdet		rb WE_PER_MSG * WE_SIZE
tip_text	rb 8 + 8 + DL_DESK
tip_num		rb 8
