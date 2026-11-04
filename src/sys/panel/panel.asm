; panel - la barre du bureau Elegant (SD0:/sys/panel, lance par la session),
; a la place du dock (sys/dock, conserve, n'est plus lance).
; Collee au bas de l'ecran, toute sa largeur, aux couleurs du theme :
;   * a gauche, le bouton « apps » (grille de 3 x 3) : le lanceur (launcher.inc) ;
;   * au centre, les RACCOURCIS (inc/panelconf.inc : SD0:/etc/panel.ini), puis
;     les applications ouvertes qui n'en ont pas ; un clic lance ou ramene ;
;   * a droite, les ESPACES DE TRAVAIL (un carre chacun, leurs fenetres en
;     petit), la corbeille, le presse-papiers (sys/clipboard), les reglages,
;     l'alimentation.
; Un trait sous une application ouverte. Fenetre sans cadre, au-dessus des
; autres, sans focus ; elle reserve le bas de l'ecran.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

SL_MAX		= 24			; raccourcis et applications ouvertes
HIT_APPS	= -2			; ce qu'il y a sous le pointeur (0.. : emplacement)
HIT_TRASH	= -3
HIT_GEAR	= -4
HIT_POWER	= -5
HIT_CLIP	= -6
HIT_DESK	= 64			; + l'espace
SQGAP		= 5			; entre deux carres d'espaces de travail
MAXPROCS	= 64			; process lus par morceau (SYS_PROCLIST_FROM)
MINI_MAX	= 32			; fenetres dessinees dans les carres
PE_SIZE		= 64			; entree de proclist : nom en +24

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
include '../../inc/applist.inc'
include '../../inc/dockconf.inc'
include '../../inc/panelconf.inc'
include 'launcher.inc'

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,quit
	bl	applis_load
	bl	apps_sort
	bl	pan_load
	mov	x0,-1				; autant d'espaces de travail que nommes
	adr	x1,dl_ndesks
	ldr	x1,[x1]
	bl	ui_desk
	bl	metrics
	adr	x9,scr				; en bas, toute la largeur
	ldp	x2,x1,[x9]
	adr	x3,cur
	ldr	x3,[x3,C_DH]
	sub	x1,x1,x3
	mov	x0,0
	adr	x4,title
	mov	x5,WF_FRAMELESS or WF_TOP or WF_NOFOCUS or WF_NOLIST or WF_LISTEN or WF_RESERVE or WF_ALPHA
	bl	ui_window_ex
	tbnz	x0,63,quit
	adr	x1,pan_win
	str	x0,[x1]
	bl	refresh
	bl	slots_build
	bl	desks_refresh
	bl	draw
.loop:	mov	x0,1000
	bl	ui_event
	cbz	x0,.idle
	cmp	x0,WEV_WINLIST
	b.eq	.tick
	cmp	x0,WEV_SCREEN
	b.eq	.screen
	cmp	x0,WEV_THEME			; autre theme : redessine
	b.eq	.theme
	cmp	x0,WEV_MOUSE
	b.ne	.loop
	adr	x0,tip_ticks
	str	xzr,[x0]
	adr	x9,ui_ev
	ldrsw	x0,[x9,EV_X]
	ldrsw	x1,[x9,EV_Y]
	bl	hit
	adr	x9,ui_ev
	ldr	w1,[x9,EV_KIND]
	cmp	w1,1
	b.eq	.press
	bl	hover_set
	b	.loop
.press:	cmn	x0,1
	b.eq	.loop
	cmp	x0,HIT_DESK
	b.ge	.desk
	tbz	x0,63,.slot
	cmn	x0,-HIT_APPS
	b.eq	.apps
	cmn	x0,-HIT_TRASH
	b.eq	.trash
	adr	x1,p_clipboard			; le presse-papiers
	cmn	x0,-HIT_CLIP
	b.eq	.spawn
	adr	x1,p_control			; « ⚙ » : les reglages ; alimentation :
	cmn	x0,-HIT_POWER			; le dialogue de fin de session
	b.ne	.spawn
	adr	x1,p_shutdown
.spawn:	mov	x0,x1
	mov	x1,0
	sys	SYS_SPAWN
	b	.loop
.trash:	adr	x0,s_fichiers			; la corbeille : dans Fichiers
	bl	app_find
	tbnz	x0,63,.loop
	bl	app_launch
	b	.loop
.slot:	adr	x1,sl_app
	ldrb	w0,[x1,x0]
	bl	app_open
	b	.tick
.desk:	sub	x0,x0,HIT_DESK
	mov	x1,0
	bl	ui_desk
	b	.tick
.apps:	mov	x0,-1
	bl	hover_set
	bl	launcher
	adr	x0,screen_todo			; (la resolution a change pendant)
	ldr	x1,[x0]
	cbz	x1,.tick
	str	xzr,[x0]
.theme:	bl	draw
	b	.loop
.screen:
	bl	screen_follow
	b	.tick
.idle:	adr	x0,hover			; (pas de sortie signalee : l'infobulle
	ldr	x1,[x0]				; s'en va apres quelques secondes)
	cmn	x1,1
	b.eq	.tick
	adr	x0,tip_ticks
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	cmp	x1,4
	b.lo	.tick
	mov	x0,-1
	bl	hover_set
.tick:	bl	refresh				; une application lancee ou finie,
	mov	x19,x0				; un espace de travail, une fenetre
	cbz	x19,.nosl
	bl	slots_build
.nosl:	bl	desks_refresh
	orr	x0,x0,x19
	cbz	x0,.loop
	bl	draw
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; nouvelle resolution : refait a sa taille, en bas de l'ecran
screen_follow:
	prologue
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,.out
	bl	metrics
	mov	x0,1				; (le panel est plus bas que le minimum)
	mov	x1,1
	bl	ui_min_size
	adr	x9,scr
	ldr	x0,[x9]
	adr	x1,cur
	ldr	x1,[x1,C_DH]
	bl	ui_resize
	adr	x0,pan_msg
	mov	x1,WS_OP_WIN_PLACE
	str	x1,[x0,WM_OP]
	adr	x1,pan_win
	ldr	x1,[x1]
	str	x1,[x0,WM_A]
	adr	x9,scr
	ldr	x2,[x9,8]
	adr	x1,cur
	ldr	x1,[x1,C_DH]
	sub	x2,x2,x1
	stp	xzr,x2,[x0,WM_B]
	str	wzr,[x0,WM_LEN]
	sys	SYS_WS_CALL
	bl	slots_build
	bl	draw
.out:	epilogue

; ---------------------------------------------------------------------------
;  Disposition
; ---------------------------------------------------------------------------

; constantes de l'echelle (ou de « taille = »), ce qui est fixe a droite
metrics:
	prologue
	bl	ui_echelle
	adr	x1,dl_taille
	ldr	x1,[x1]
	tbnz	x1,63,.ech
	mov	x0,x1
.ech:	mov	x1,ECH_SIZE
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
	ldr	x2,[x9,C_BTN]
	adr	x3,scr
	ldr	x3,[x3]
	sub	x3,x3,8				; de la droite vers la gauche :
	sub	x3,x3,x2			; alimentation,
	adr	x4,power_x
	str	x3,[x4]
	sub	x3,x3,x2			; reglages,
	sub	x3,x3,4
	adr	x4,gear_x
	str	x3,[x4]
	sub	x3,x3,x2			; presse-papiers,
	sub	x3,x3,4
	adr	x4,clip_x
	str	x3,[x4]
	mov	x5,-1				; corbeille,
	adr	x4,dl_corbeille
	ldr	x4,[x4]
	cbz	x4,.nt
	sub	x3,x3,x2
	sub	x3,x3,4
	mov	x5,x3
.nt:	adr	x4,trash_x
	str	x5,[x4]
	sub	x3,x3,10			; espaces de travail
	adr	x4,dl_ndesks
	ldr	x4,[x4]
	ldr	x5,[x9,C_SQW]
	add	x5,x5,SQGAP
	mul	x4,x4,x5
	sub	x4,x4,SQGAP
	sub	x3,x3,x4
	adr	x4,desk_x
	str	x3,[x4]
	epilogue

; les emplacements : les raccourcis, puis les applications ouvertes sans
; raccourci ; leur place, centres sur l'ecran autant que possible
slots_build:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,0				; emplacements
	mov	x19,0
.p:	adr	x0,pan_npin
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.pd
	adr	x0,pan_pin
	add	x0,x0,x19,lsl 5
	add	x19,x19,1
	bl	app_find
	tbnz	x0,63,.p
	bl	slot_add
	b	.p
.pd:	adr	x0,npinned
	str	x20,[x0]
	mov	x19,0				; par nom
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.done
	adr	x0,ap_ordre
	ldrb	w21,[x0,x19]
	add	x19,x19,1
	adr	x0,pids
	ldr	x0,[x0,x21,lsl 3]
	cbz	x0,.a
	mov	x0,x21
	bl	pan_shell
	cbnz	x0,.a
	mov	x0,x21
	bl	slot_add
	b	.a
.done:	adr	x0,nslots
	str	x20,[x0]
	adr	x9,cur				; la place entre « apps » et les carres
	ldr	x0,[x9,C_BTN]
	add	x21,x0,16			; x21 : debut
	adr	x0,desk_x
	ldr	x22,[x0]
	sub	x22,x22,10			; x22 : fin
	subs	x0,x22,x21
	csel	x0,xzr,x0,lt
	mov	x4,x0				; (place)
	ldr	x1,[x9,C_CW]
	mov	x3,x20				; emplacements montres
	cbz	x20,.pos
	udiv	x2,x0,x20			; trop nombreux : plus etroits,
	cmp	x2,x1
	csel	x1,x2,x1,lo
	cmp	x1,30				; puis moins nombreux
	b.hs	.pos
	mov	x1,30
	udiv	x3,x0,x1
.pos:	adr	x0,slot_w
	str	x1,[x0]
	adr	x0,nslots
	str	x3,[x0]
	ldr	x2,[x9,C_ICON]			; l'icone tient dans l'emplacement
	sub	x0,x1,4
	cmp	x2,x0
	csel	x2,x0,x2,hi
	adr	x0,icon_px
	str	x2,[x0]
	mul	x5,x3,x1			; largeur des emplacements
	adr	x0,scr
	ldr	x0,[x0]
	sub	x0,x0,x5
	lsr	x0,x0,1				; centres
	sub	x6,x22,x5
	cmp	x0,x6
	csel	x0,x6,x0,gt
	cmp	x0,x21
	csel	x0,x21,x0,lt
	adr	x6,sl_x
	mov	x7,0
.x:	cmp	x7,x3
	b.hs	.out
	str	x0,[x6,x7,lsl 3]
	add	x0,x0,x1
	add	x7,x7,1
	b	.x
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = application, x20 = emplacements : ajoutee si elle n'y est pas -> x20
slot_add:
	adr	x1,sl_app
	mov	x2,0
.c:	cmp	x2,x20
	b.hs	.add
	ldrb	w3,[x1,x2]
	cmp	x3,x0
	b.eq	.ret
	add	x2,x2,1
	b	.c
.add:	cmp	x20,SL_MAX
	b.hs	.ret
	strb	w0,[x1,x20]
	add	x20,x20,1
.ret:	ret

; x0 = espace -> x0, x1 = son carre
desk_box:
	adr	x3,cur
	ldp	x4,x5,[x3,C_SQW]		; C_SQW, C_SQH
	ldr	x6,[x3,C_DH]
	add	x1,x4,SQGAP
	mul	x0,x0,x1
	adr	x1,desk_x
	ldr	x1,[x1]
	add	x0,x0,x1
	sub	x1,x6,x5
	lsr	x1,x1,1
	ret

; x0, x1 = point dans la fenetre -> x0 = ce qu'il y a dessous (-1 : rien, ou
; HIT_*, ou un emplacement)
hit:
	adr	x9,cur
	ldr	x2,[x9,C_BTN]
	sub	x3,x0,8
	cmp	x3,x2
	b.lo	.apps
	adr	x3,power_x
	ldr	x3,[x3]
	sub	x4,x0,x3
	cmp	x4,x2
	b.lo	.power
	adr	x3,gear_x
	ldr	x3,[x3]
	sub	x4,x0,x3
	cmp	x4,x2
	b.lo	.gear
	adr	x3,clip_x
	ldr	x3,[x3]
	sub	x4,x0,x3
	cmp	x4,x2
	b.lo	.clip
	adr	x3,trash_x
	ldr	x3,[x3]
	tbnz	x3,63,.desks
	sub	x4,x0,x3
	cmp	x4,x2
	b.lo	.trash
.desks:	ldp	x5,x6,[x9,C_SQW]		; (2 px autour)
	adr	x3,desk_x
	ldr	x3,[x3]
	sub	x3,x3,2
	add	x5,x5,SQGAP
	adr	x7,dl_ndesks
	ldr	x7,[x7]
	mov	x8,0
.d:	cmp	x8,x7
	b.hs	.slots
	sub	x4,x0,x3
	cmp	x4,x5
	b.lo	.desk
	add	x3,x3,x5
	add	x8,x8,1
	b	.d
.desk:	add	x0,x8,HIT_DESK
	ret
.slots:	adr	x2,nslots
	ldr	x2,[x2]
	adr	x3,slot_w
	ldr	x3,[x3]
	adr	x4,sl_x
	mov	x5,0
.s:	cmp	x5,x2
	b.hs	.none
	ldr	x6,[x4,x5,lsl 3]
	sub	x6,x0,x6
	cmp	x6,x3
	b.lo	.got
	add	x5,x5,1
	b	.s
.got:	mov	x0,x5
	ret
.none:	mov	x0,-1
	ret
.apps:	mov	x0,HIT_APPS
	ret
.gear:	mov	x0,HIT_GEAR
	ret
.clip:	mov	x0,HIT_CLIP
	ret
.power:	mov	x0,HIT_POWER
	ret
.trash:	mov	x0,HIT_TRASH
	ret

; x0 = ce qui est survole : son nom au-dessus du panel, redessine s'il change
hover_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,hover
	ldr	x1,[x0]
	cmp	x1,x19
	b.eq	.out
	str	x19,[x0]
	cmn	x19,1
	b.ne	.tip
	mov	x0,0
	mov	x1,0
	mov	x2,0
	bl	ui_tip_at
	b	.draw
.tip:	cmp	x19,HIT_DESK
	b.ge	.desk
	tbz	x19,63,.slot
	mov	x0,8
	adr	x2,s_apps
	cmn	x19,-HIT_APPS
	b.eq	.show
	adr	x0,gear_x
	ldr	x0,[x0]
	sub	x0,x0,50
	adr	x2,s_reglages
	cmn	x19,-HIT_GEAR
	b.eq	.show
	adr	x0,power_x
	ldr	x0,[x0]
	sub	x0,x0,40
	adr	x2,s_eteindre
	cmn	x19,-HIT_POWER
	b.eq	.show
	adr	x0,clip_x
	ldr	x0,[x0]
	sub	x0,x0,50
	adr	x2,s_clip
	cmn	x19,-HIT_CLIP
	b.eq	.show
	adr	x0,trash_x
	ldr	x0,[x0]
	sub	x0,x0,30
	adr	x2,s_corb
	b	.show
.slot:	adr	x0,sl_app
	ldrb	w0,[x0,x19]
	bl	app_label
	mov	x2,x0
	adr	x0,sl_x
	ldr	x0,[x0,x19,lsl 3]
	b	.show
.desk:	sub	x19,x19,HIT_DESK		; « Bureau 2 : son nom »
	adr	x0,tip_text
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
	sub	x0,x0,60
	adr	x2,tip_text
.show:	mov	x1,-28
	bl	tr_x2
	bl	ui_tip_at
.draw:	bl	draw
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Applications ouvertes
; ---------------------------------------------------------------------------

; pids des applications qui tournent (tous les process, par morceaux : le noyau nomme le process
; d'un paquet d'apres lui) ->
; x0 = 1 si quelque chose a change
refresh:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,pids_new			; rien de trouve encore
	mov	x1,0
.z:	str	xzr,[x0,x1,lsl 3]
	add	x1,x1,1
	cmp	x1,AP_MAX
	b.lo	.z
	mov	x21,0				; tous les process, par morceaux
.paq:	adr	x0,procs			; (SYS_PROCLIST_FROM : pas de plafond)
	mov	x1,MAXPROCS
	mov	x2,x21
	sys	SYS_PROCLIST_FROM
	cmp	x0,0
	b.le	.cmp
	mov	x22,x0
	mov	x24,x0
	adr	x20,procs
.e:	cbz	x22,.suite
	ldr	x21,[x20]			; (la suite : apres ce process)
	and	x21,x21,0xFFFF
	mov	x19,0				; une application de ce nom ?
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.en
	add	x0,x20,24
	adr	x1,ap_nom
	add	x1,x1,x19,lsl 5
	bl	str_eq
	cbz	x0,.an
	adr	x0,pids_new			; (le premier process de ce nom)
	ldr	x1,[x0,x19,lsl 3]
	cbnz	x1,.en
	ldr	w1,[x20]
	str	x1,[x0,x19,lsl 3]
	b	.en
.an:	add	x19,x19,1
	b	.a
.en:	add	x20,x20,PE_SIZE
	sub	x22,x22,1
	b	.e
.suite:	cmp	x24,MAXPROCS			; morceau plein : la suite
	b.eq	.paq
.cmp:	mov	x23,0				; change ?
	mov	x19,0
.c:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	adr	x0,pids_new
	ldr	x2,[x0,x19,lsl 3]
	adr	x0,pids
	ldr	x1,[x0,x19,lsl 3]
	cmp	x1,x2
	cinc	x23,x23,ne
	str	x2,[x0,x19,lsl 3]
	add	x19,x19,1
	b	.c
.out:	mov	x0,x23
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

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
	mov	x20,x0				; x, y du carre
	mov	x21,x1
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
	adr	x2,hover
	ldr	x2,[x2]
	sub	x2,x2,HIT_DESK
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

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------

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

; x0 = x, x1 = largeur : le fond d'un element survole
hover_box:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	ui_jetons
	ldr	w0,[x0,TJ_DOCK]
	and	w1,w0,0xFF000000
	str	x1,[sp,-16]!
	and	w0,w0,0xFFFFFF
	mov	x1,158
	bl	ui_tone
	ldr	x1,[sp],16
	orr	w5,w0,w1
	mov	w6,w5
	mov	x0,x19
	mov	x1,4
	mov	x2,x20
	adr	x3,cur
	ldr	x3,[x3,C_DH]
	sub	x3,x3,8
	mov	x4,10
	bl	ui_rbox
	ldp	x19,x20,[sp],16
	epilogue

; x0 = trait vertical a ce x
draw_sep:
	prologue
	str	x0,[sp,-16]!
	bl	sep_colour
	mov	w4,w0
	ldr	x0,[sp],16
	mov	x1,12
	mov	x2,1
	adr	x3,cur
	ldr	x3,[x3,C_DH]
	sub	x3,x3,24
	bl	ui_fill
	epilogue

; x0 = glyphe, x1 = x du bouton, w2 = couleur : au centre du bouton, cote moitie du bouton
btn_glyph:
	prologue
	mov	w5,w2
	adr	x9,cur
	ldr	x4,[x9,C_BTN]
	lsr	x3,x4,1
	add	x3,x3,1
	and	x3,x3,not 1
	sub	x4,x4,x3
	add	x1,x1,x4,lsr 1
	ldr	x4,[x9,C_DH]
	sub	x4,x4,x3
	lsr	x2,x4,1
	mov	w4,w5
	bl	ui_glyph
	epilogue

; -> w0 = encre lisible sur la barre
bar_ink:
	prologue
	bl	ui_jetons
	ldr	w0,[x0,TJ_DOCK]
	and	w0,w0,0xFFFFFF
	bl	ui_ink
	epilogue

; le bouton « apps » : pastille neutre (accent clair au survol), grille de 3 x 3
draw_apps:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	ui_jetons
	mov	x22,x0
	adr	x0,cur
	ldr	x19,[x0,C_BTN]
	ldr	x20,[x0,C_DH]
	sub	x20,x20,x19
	lsr	x20,x20,1			; y
	ldr	w21,[x22,TJ_CREUX]
	adr	x0,hover
	ldr	x0,[x0]
	cmp	x0,HIT_APPS
	b.ne	.face
	ldr	w21,[x22,TJ_ACCENT_CLAIR]
.face:	adr	x0,s_g_pill
	mov	x1,8
	mov	x2,x20
	mov	x3,x19
	mov	w4,w21
	bl	ui_glyph
	mov	w0,w21
	bl	ui_ink
	mov	w4,w0
	lsr	x3,x19,1			; la grille, centree
	add	x3,x3,2
	and	x3,x3,not 1
	sub	x0,x19,x3
	lsr	x0,x0,1
	add	x1,x0,8
	add	x2,x20,x0
	adr	x0,s_g_apps
	bl	ui_glyph
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom de l'icone, x1 = x, x2 = code HIT_* : un bouton a droite
draw_btn:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,hover
	ldr	x0,[x0]
	cmp	x0,x2
	b.ne	.icon
	mov	x0,x20
	adr	x1,cur
	ldr	x1,[x1,C_BTN]
	bl	hover_box
.icon:	bl	ui_jetons			; monochrome, comme ses voisins
	ldr	w2,[x0,TJ_TEXTE2]
	mov	x0,x19
	mov	x1,x20
	bl	btn_glyph
	ldp	x19,x20,[sp],16
	epilogue

; le bouton des reglages, a l'encre de la barre
draw_gear:
	prologue
	adr	x0,hover
	ldr	x0,[x0]
	cmp	x0,HIT_GEAR
	b.ne	.icon
	adr	x0,gear_x
	ldr	x0,[x0]
	adr	x1,cur
	ldr	x1,[x1,C_BTN]
	bl	hover_box
.icon:	bl	bar_ink
	mov	w2,w0
	adr	x0,s_g_gear
	adr	x1,gear_x
	ldr	x1,[x1]
	bl	btn_glyph
	epilogue

; le bouton d'alimentation, couleur de danger
draw_power:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,hover
	ldr	x0,[x0]
	cmp	x0,HIT_POWER
	b.ne	.icon
	adr	x0,power_x
	ldr	x0,[x0]
	adr	x1,cur
	ldr	x1,[x1,C_BTN]
	bl	hover_box
.icon:	bl	ui_jetons
	ldr	w2,[x0,TJ_TEXTE2]		; (rouge au survol)
	adr	x1,hover
	ldr	x1,[x1]
	cmp	x1,HIT_POWER
	b.ne	.pw
	ldr	w2,[x0,TJ_DANGER]
.pw:	adr	x0,s_g_power
	adr	x1,power_x
	ldr	x1,[x1]
	bl	btn_glyph
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; le bouton du presse-papiers, a l'encre de la barre
draw_clip:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,hover
	ldr	x0,[x0]
	cmp	x0,HIT_CLIP
	b.ne	.icon
	adr	x0,clip_x
	ldr	x0,[x0]
	adr	x1,cur
	ldr	x1,[x1,C_BTN]
	bl	hover_box
.icon:	bl	bar_ink
	mov	w2,w0
	adr	x0,s_g_clipboard
	adr	x1,clip_x
	ldr	x1,[x1]
	bl	btn_glyph
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	ui_jetons
	mov	x23,x0
	adr	x0,cur
	ldr	x22,[x0,C_DH]
	adr	x0,scr
	ldr	x21,[x0]
	mov	x0,0				; toute la largeur, le dock du theme
	mov	x1,0
	mov	x2,x21
	mov	x3,x22
	ldr	w4,[x23,TJ_DOCK]
	bl	ui_fill
	bl	sep_colour			; un trait en haut
	mov	w4,w0
	mov	x0,0
	mov	x1,0
	mov	x2,x21
	mov	x3,1
	bl	ui_fill
	bl	draw_apps
	mov	x19,0				; --- emplacements
.s:	adr	x0,nslots
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.right
	adr	x0,sl_x
	ldr	x20,[x0,x19,lsl 3]
	adr	x0,slot_w
	ldr	x24,[x0]
	adr	x0,hover
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.sep
	add	x0,x20,1
	sub	x1,x24,2
	bl	hover_box
.sep:	adr	x0,npinned			; raccourcis | applications ouvertes
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.icon
	cbz	x19,.icon
	mov	x0,x20
	bl	draw_sep
.icon:	adr	x0,icon_px
	ldr	x3,[x0]
	sub	x0,x24,x3
	add	x1,x20,x0,lsr 1
	sub	x0,x22,x3
	lsr	x2,x0,1
	sub	x2,x2,1
	adr	x0,sl_app
	ldrb	w0,[x0,x19]
	adr	x4,ap_nom
	add	x0,x4,x0,lsl 5
	bl	ui_icone
	adr	x0,sl_app			; un trait sous une application ouverte
	ldrb	w0,[x0,x19]
	adr	x1,pids
	ldr	x0,[x1,x0,lsl 3]
	cbz	x0,.next
	add	x0,x20,x24,lsr 1
	sub	x0,x0,7
	sub	x1,x22,5
	mov	x2,14
	mov	x3,3
	mov	x4,1
	ldr	w5,[x23,TJ_ACCENT_VIF]
	mov	w6,w5
	bl	ui_rbox
.next:	add	x19,x19,1
	b	.s
.right:	adr	x0,desk_x			; --- a droite
	ldr	x0,[x0]
	sub	x0,x0,7
	bl	draw_sep
	mov	x19,0
.sq:	adr	x0,dl_ndesks
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.btn
	mov	x0,x19
	bl	draw_square
	add	x19,x19,1
	b	.sq
.btn:	adr	x1,trash_x
	ldr	x1,[x1]
	tbnz	x1,63,.gear
	adr	x0,s_corbeille
	mov	x2,HIT_TRASH
	bl	draw_btn
.gear:	bl	draw_gear
	bl	draw_clip
	bl	draw_power
	bl	ui_update_all
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; constantes des echelles (L, M, P) : DH, CW, ICON, BTN, carre d'espace (l x h)
ECH_N		= 6
ECH_SIZE	= ECH_N * 4
C_DH		= 0
C_CW		= 8
C_ICON		= 16
C_BTN		= 24
C_SQW		= 32
C_SQH		= 40
ech:	dw 48, 40, 32, 34, 34, 25
	dw 48, 40, 32, 34, 30, 22
	dw 48, 44, 32, 40, 26, 19

title		db 'Panel',0
s_fichiers	db 'files',0
s_bureau	db 'Bureau ',0
s_nom		db ' : ',0
s_corbeille	db 'glyph-trash',0
s_control	db 'control',0
s_g_pill	db 'glyph-pill',0
s_g_apps	db 'glyph-apps',0
s_g_gear	db 'glyph-gear',0
s_g_power	db 'glyph-power',0
s_g_clipboard	db 'glyph-clipboard',0
s_apps		db 'Applications',0
s_reglages	db 'R',0xC3,0xA9,'glages',0
s_corb		db 'Corbeille',0
s_clip		db 'Presse-papiers',0
s_eteindre	db 0xC3,0x89,'teindre',0
p_control	db 'SD0:/apps/control.app/main',0
p_shutdown	db 'SD0:/sys/shutdown',0
p_clipboard	db 'SD0:/sys/clipboard',0
		align 4

include '../../inc/tr.inc'

segment readable writeable
kit_data
ui_data
applis_data
dockconf_data
panelconf_data
launcher_data
		align 8
scr		rb 32
pan_win		dd 0
pan_msg		rb WS_MSG
screen_todo	dd 0
cur		rd ECH_N			; constantes courantes (C_*)
nslots		dd 0
npinned		dd 0				; dont raccourcis
slot_w		dd 0				; largeur d'un emplacement
icon_px		dd 0
power_x		dd 0
gear_x		dd 0
clip_x		dd 0
trash_x		dd -1
desk_x		dd 0
desk_cour	dd 0				; espace de travail montre
tip_ticks	dd 0				; secondes sans mouvement
hover		dd -1				; survole (hit)
sl_x		rd SL_MAX
sl_app		rb SL_MAX
		align 8
pids		rd AP_MAX
pids_new	rd AP_MAX			; (refresh)
wlist		rb WL_PER_MSG * WL_SIZE
procs		rb MAXPROCS * PE_SIZE
nminis		dd 0				; les fenetres, en petit (16 octets : espace + 1
minis_sig	dd 0				; | clavier << 8, x, y, l, h en 16 bits)
minis		rb MINI_MAX * 16
wdet		rb WE_PER_MSG * WE_SIZE
tip_text	rb 8 + 8 + DL_DESK
tip_num		rb 8
