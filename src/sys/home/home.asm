; ============================================================================
;  home - l'accueil du mode poche (SD0:/sys/home, lance par la session poche,
;  SD0:/etc/session.poche ; docs/MODE-POCHE.md, variante A) : le tableau du
;  jour, plein ecran sous la barre de menus, derriere les applications.
;    * en haut, l'heure en grand et la date ; a droite, la recherche (apps,
;      puis applets de Reglages, puis fichiers de SD0:/home) ;
;    * « Aujourd'hui » : trois cartes, le prochain rendez-vous de l'agenda
;      (SD0:/apps/calendar.app/agenda.txt), la meteo de PocketQI
;      (SD0:/home/pocketqi/weather.txt, s'il l'a ecrit), la musique en cours
;      (korin.media.now ; le bouton rond publie korin.media.control) ;
;    * « Recentes » (inc/recent.inc), « Toutes les apps » : le lanceur du
;      panel (sys/panel/launcher.inc), en second niveau ;
;    * en bas, la bande « Ouvertes » : une pastille par application qui a une
;      fenetre, l'app devant entouree d'ACCENT, une croix qui la ferme.
;  Clavier : taper ecrit dans la recherche ; Tab (ou haut, bas) passe cartes,
;  Recentes, Ouvertes ; gauche, droite dans une rangee ; Entree ouvre ; Suppr
;  ferme l'app choisie dans Ouvertes ; Echap vide la recherche.
;  korin.shell.home (bus) : l'accueil vient devant ; deja devant, l'app d'avant
;  revient. Un second « home » lance publie korin.shell.home et s'en va.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/buskit/buskit.inc'


HM_M		= 24			; marges
HM_BOX_Y	= 24			; recherche (LA_BOX_H de haut)
HM_TIME_PX	= 44
HM_TIME_B	= 66			; lignes de base : l'heure,
HM_DATE_PX	= 15
HM_DATE_B	= 92			; la date,
HM_SEC_PX	= 14
HM_TODAY_B	= 134			; « Aujourd'hui »,
HM_CARD_Y	= 146			; cartes : haut, hauteur, rayon, ecart
HM_CARD_H	= 76
HM_CARD_R	= 10
HM_CARD_G	= 12
HM_CARD_IC	= 36
HM_RECENT_B	= 260			; « Recentes »
HM_ROW_Y	= 272			; rangee : haut, cellule, icone
HM_CELL		= 96
HM_CELL_IC	= 52
HM_NCELL	= 6
HM_BAND_H	= 64			; bande « Ouvertes »
HM_PILL_H	= 44
HM_PILL_IC	= 24
HM_PILL_X	= 28			; la croix
HM_PILL_TXT	= 120			; nom : largeur au plus
HM_RES_Y	= 116			; resultats : haut, ligne
HM_RES_H	= 44
HM_BODY_Y	= 106			; sous l'en-tete
OP_MAX		= 16			; applications ouvertes
RES_MAX		= 16
RES_APPS	= 6			; resultats au plus, par genre
RES_SETS	= 4
RES_FILES	= 6
RES_TXT		= 96
RES_PATH	= 192
FS_BUDGET	= 3000			; entrees lues au plus par recherche
FBUF		= 16384
Z_CARDS		= 0			; zones du clavier
Z_RECENT	= 1
Z_OPEN		= 2
LA_WFLAGS	= WF_FRAMELESS or WF_TOP or WF_NOFOCUS or WF_NOLIST or WF_GRAB or WF_ALPHA
					; (le lanceur : sans rideau, l'accueil est derriere)
MD_NONE		= 0			; korin.media.now
MD_PLAY		= 1
MD_PAUSE	= 2
MD_STOP		= 3

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so
include '../../inc/applet.inc'			; (read_file, ldv, stv)
include '../../inc/applist.inc'
include '../../inc/dockconf.inc'
include '../../inc/panelconf.inc'
include '../../inc/recent.inc'
include '../../inc/apprun.inc'
include '../panel/launcher.inc'

start:
	bl	kits_open
	adr	x0,s_svc			; un seul accueil : le second le montre
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbz	x0,63,.one
	adr	x0,t_home
	adr	x1,s_one
	mov	x2,-1
	mov	x3,0
	bl	bus_publish
	b	quit
.one:	adr	x0,ui_win
	bl	ui_init
	bl	applis_load
	bl	apps_sort
	bl	pan_load
	bl	rc_load
	bl	apps_running
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,quit
	bl	layout
	adr	x9,scr
	ldr	x1,[x9,24]			; sous la barre du haut, au-dessus du
	stv	x1,g_top			; panel s'il y en a un
	ldr	x2,[x9,16]
	stv	x2,g_bot
	ldr	x2,[x9]
	ldv	x3,g_h
	mov	x0,0
	adr	x4,s_title
	bl	tr_x4
	movl	x5,WF_FRAMELESS or WF_NOLIST or WF_LISTEN or WF_HOME
	bl	ui_window_ex
	tbnz	x0,63,quit
	stv	x0,g_win
	bl	box_make
	adr	x0,t_home			; le bus : l'accueil demande, la musique
	mov	x1,0
	bl	bus_subscribe
	stv	x0,g_sub_home
	adr	x0,t_media
	mov	x1,BUS_CURRENT
	bl	bus_subscribe
	stv	x0,g_sub_media
	bl	data_load
	bl	open_build
	bl	rows_build
	bl	draw_all
	adr	x0,m_ready
	bl	puts
.loop:	mov	x0,1000
	bl	ui_event
	cbz	x0,.idle
	cmp	x0,WEV_WINLIST
	b.eq	.wins
	cmp	x0,WEV_THEME
	b.eq	.theme
	cmp	x0,WEV_SCREEN
	b.eq	.screen
	cmp	x0,WEV_BUS
	b.eq	.bus
	cmp	x0,WEV_FOCUS
	b.eq	.focus
	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cmp	x0,WEV_CLOSE
	b.eq	.loop				; (l'accueil reste)
	bl	box_event
	b	.loop
.idle:	bl	tick
	b	.loop
.wins:	bl	screen_check			; (la barre du haut a pu venir apres nous)
	cbnz	x0,.loop
	bl	open_build
	cbz	x0,.loop
	bl	rows_build
	bl	draw_lower
	b	.loop
.theme:	bl	draw_all
	b	.loop
.screen:
	bl	screen_follow
	b	.loop
.focus:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cbz	w0,.loop
	bl	data_load			; (revenu devant : les donnees du moment)
	bl	box_refocus
	bl	draw_all
	b	.loop
.bus:	bl	on_bus
	b	.loop
.key:	bl	on_key
	b	.loop
.mouse:	bl	on_mouse
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Disposition
; ---------------------------------------------------------------------------

; g_w, g_h et ce qui en decoule, d'apres scr
layout:
	adr	x9,scr
	ldp	x0,x1,[x9]
	ldr	x2,[x9,16]
	ldr	x3,[x9,24]
	sub	x1,x1,x2
	sub	x1,x1,x3
	stv	x0,g_w
	stv	x1,g_h
	mov	x2,13				; la recherche : de 13/40 de la largeur
	mul	x2,x0,x2			; au bord droit
	mov	x3,40
	udiv	x2,x2,x3
	cmp	x2,200
	mov	x3,200
	csel	x2,x3,x2,lo
	stv	x2,g_bx
	sub	x3,x0,x2
	sub	x3,x3,HM_M
	stv	x3,g_bw
	sub	x3,x0,2*HM_M+2*HM_CARD_G	; cartes
	mov	x4,3
	udiv	x3,x3,x4
	stv	x3,g_cw
	sub	x3,x0,2*HM_M+HM_NCELL*HM_CELL	; rangee : ecart entre les cellules
	cmp	x3,0
	csel	x3,xzr,x3,lt
	mov	x4,HM_NCELL-1
	udiv	x3,x3,x4
	stv	x3,g_gap
	sub	x3,x1,HM_BAND_H			; la bande
	stv	x3,g_band
	ret

; le champ de la recherche (dans la boite de la_box_at)
box_make:
	prologue
	ldv	x0,g_bx
	add	x0,x0,LA_BOX_TX
	ldv	x2,g_bw
	sub	x2,x2,LA_BOX_TX+2
	mov	x1,HM_BOX_Y
	mov	x3,LA_BOX_H
	mov	x4,0
	bl	ui_textbox
	stv	x0,g_box
	adr	x1,s_hint
	bl	tr_x1
	bl	ui_textbox_hint
	ldv	x0,g_box
	bl	ui_w_focus
	epilogue

; le clavier revient au champ
box_refocus:
	prologue
	bl	ui_w_get_focus
	ldv	x1,g_box
	cmp	x0,x1
	b.eq	.out
	mov	x0,x1
	bl	ui_w_focus
.out:	epilogue

; ui_w_event, le champ decoupe -> x0 = widget, x1 = code UN_* ; une recherche
; qui change : resultats et dessin
box_event:
	prologue
	ldv	x0,g_bx
	mov	x1,HM_BOX_Y
	ldv	x2,g_bw
	bl	la_clip_at
	mov	x0,0
	bl	ui_w_event
	stp	x0,x1,[sp,-16]!
	mov	x2,0
	bl	ui_clip
	ldp	x0,x1,[sp],16
	cmp	x1,UN_CHANGE
	b.ne	.out
	ldv	x2,g_box
	cmp	x0,x2
	b.ne	.out
	bl	search_run
	bl	draw_lower
.out:	epilogue

; la barre du haut ou le panel ont change de place -> x0 = 1 : refait
screen_check:
	prologue
	adr	x0,scr
	bl	ui_screen
	mov	x0,0
	adr	x9,scr
	ldr	x1,[x9,24]
	ldr	x2,[x9,16]
	ldv	x3,g_top
	ldv	x4,g_bot
	cmp	x1,x3
	b.ne	.redo
	cmp	x2,x4
	b.eq	.out
.redo:	bl	screen_follow
	mov	x0,1
.out:	epilogue

; nouvelle resolution (ou nouvelles reserves) : refait a sa taille
screen_follow:
	prologue
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,.out
	bl	layout
	adr	x9,scr
	ldr	x1,[x9,24]
	stv	x1,g_top
	ldr	x2,[x9,16]
	stv	x2,g_bot
	mov	x0,1
	mov	x1,1
	bl	ui_min_size
	ldv	x0,g_w
	ldv	x1,g_h
	bl	ui_resize
	adr	x0,g_msg
	mov	x1,WS_OP_WIN_PLACE
	str	x1,[x0,WM_OP]
	ldv	x1,g_win
	str	x1,[x0,WM_A]
	ldv	x2,g_top
	stp	xzr,x2,[x0,WM_B]
	str	wzr,[x0,WM_LEN]
	sys	SYS_WS_CALL
	ldv	x0,g_box
	ldv	x1,g_bx
	add	x1,x1,LA_BOX_TX
	mov	x2,HM_BOX_Y
	ldv	x3,g_bw
	sub	x3,x3,LA_BOX_TX+2
	mov	x4,LA_BOX_H
	bl	ui_w_move
	bl	open_build
	bl	draw_all
.out:	epilogue

; ---------------------------------------------------------------------------
;  Heure et dates
; ---------------------------------------------------------------------------

; x0 = jours depuis 1970 -> x0, x1, x2 = annee, mois, jour (H. Hinnant)
civil_from_days:
	movl	x1,719468
	add	x0,x0,x1
	movl	x1,146097
	udiv	x2,x0,x1			; era
	msub	x3,x2,x1,x0			; doe
	movl	x1,1460
	udiv	x4,x3,x1
	sub	x4,x3,x4
	movl	x1,36524
	udiv	x5,x3,x1
	add	x4,x4,x5
	movl	x1,146096
	udiv	x5,x3,x1
	sub	x4,x4,x5
	mov	x1,365
	udiv	x4,x4,x1			; yoe
	mov	x1,400
	mul	x5,x2,x1
	add	x5,x5,x4			; y
	mov	x1,365
	mul	x6,x4,x1
	add	x6,x6,x4,lsr 2
	mov	x1,100
	udiv	x7,x4,x1
	sub	x6,x6,x7
	sub	x3,x3,x6			; doy
	mov	x1,5
	mul	x6,x3,x1
	add	x6,x6,2
	mov	x1,153
	udiv	x6,x6,x1			; mp
	mul	x7,x6,x1
	add	x7,x7,2
	mov	x1,5
	udiv	x7,x7,x1
	sub	x2,x3,x7
	add	x2,x2,1				; d
	mov	x1,-9
	cmp	x6,10
	b.hs	.m
	mov	x1,3
.m:	add	x1,x1,x6			; m
	cmp	x1,2
	cinc	x5,x5,ls
	mov	x0,x5
	ret

; x0 = jours depuis 1970 -> x0 = AAAAMMJJ
day_key:
	prologue
	bl	civil_from_days
	movl	x3,10000
	mul	x0,x0,x3
	mov	x3,100
	madd	x0,x1,x3,x0
	add	x0,x0,x2
	epilogue

; -> x0 = secondes locales
now_local:
	mov	x0,2
	sys	SYS_CLOCK_GET
	ret

; x0 = destination, x1 = 0..99 -> deux chiffres, x0 avance
put2:
	mov	x4,10
	udiv	x5,x1,x4
	msub	x6,x5,x4,x1
	add	w5,w5,'0'
	add	w6,w6,'0'
	strb	w5,[x0],1
	strb	w6,[x0],1
	strb	wzr,[x0]
	ret

; x0 = destination, x1 = minutes du jour -> « H:MM », x0 = fin
put_hm:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x2,60
	udiv	x0,x1,x2
	msub	x20,x0,x2,x1
	mov	x1,x19
	bl	fmt_dec
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,x20
	bl	put2
	ldp	x19,x20,[sp],16
	epilogue

; t_clock <- « HH:MM », t_date <- « mercredi 5 novembre » (traduits)
clock_text:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	bl	now_local
	movl	x1,86400
	udiv	x19,x0,x1			; jours
	msub	x20,x19,x1,x0			; secondes du jour
	adr	x0,t_clock
	mov	x2,3600
	udiv	x1,x20,x2
	bl	put2
	mov	w1,':'
	strb	w1,[x0],1
	mov	x2,3600
	udiv	x3,x20,x2
	msub	x1,x3,x2,x20
	mov	x2,60
	udiv	x1,x1,x2
	bl	put2
	add	x0,x19,3			; jour de la semaine (0 : lundi)
	mov	x1,7
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	adr	x1,s_wdays
	add	x0,x1,x0,lsl 4
	bl	tr_x0
	mov	x1,x0
	adr	x0,t_date
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	mov	x21,x0
	mov	x0,x19
	bl	civil_from_days
	mov	x20,x1				; mois
	mov	x0,x2
	mov	x1,x21
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	x21,x0
	adr	x0,s_months
	sub	x1,x20,1
	add	x0,x0,x1,lsl 4
	bl	tr_x0
	mov	x1,x0
	mov	x0,x21
	bl	str_cat
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Les donnees des cartes
; ---------------------------------------------------------------------------

; agenda et meteo relus, minute notee
data_load:
	prologue
	bl	now_local
	mov	x1,60
	udiv	x0,x0,x1
	stv	x0,g_minute
	bl	agenda_load
	bl	weather_load
	epilogue

; chaque seconde : nouvelle minute (heure, agenda) ; musique jouee (son temps)
tick:
	prologue
	bl	now_local
	mov	x1,60
	udiv	x0,x0,x1
	ldv	x1,g_minute
	cmp	x0,x1
	b.eq	.music
	bl	data_load
	bl	draw_all
	b	.out
.music:	ldv	x0,md_state
	cmp	x0,MD_PLAY
	b.ne	.out
	bl	searching
	cbnz	x0,.out
	mov	x0,2
	mov	x1,1
	bl	draw_card
.out:	epilogue

; x0 = texte, x1 = chiffres -> x0 = valeur (-1 : pas un chiffre)
digits:
	mov	x2,0
	mov	x3,10
.c:	cbz	x1,.ok
	ldrb	w4,[x0],1
	sub	w4,w4,'0'
	cmp	w4,9
	b.hi	.no
	mul	x2,x2,x3
	add	x2,x2,x4
	sub	x1,x1,1
	b	.c
.ok:	mov	x0,x2
	ret
.no:	mov	x0,-1
	ret

; x0 = destination, x1 = source Latin-1 (ou UTF-8), x2 = fin de la source, x3 =
; place : en UTF-8, termine par 0 (comme sys/agenda)
to_utf8:
	sub	x3,x3,3
	add	x3,x3,x0			; x3 : limite
.c:	cmp	x1,x2
	b.hs	.end
	cmp	x0,x3
	b.hs	.end
	ldrb	w4,[x1],1
	cmp	w4,13
	b.eq	.c
	cmp	w4,0x80
	b.hs	.hi
	strb	w4,[x0],1
	b	.c
.hi:	and	w5,w4,0xE0			; (deja de l'UTF-8 : garde)
	cmp	w5,0xC0
	b.ne	.enc
	cmp	x1,x2
	b.hs	.enc
	ldrb	w5,[x1]
	and	w6,w5,0xC0
	cmp	w6,0x80
	b.ne	.enc
	add	x1,x1,1
	strb	w4,[x0],1
	strb	w5,[x0],1
	b	.c
.enc:	lsr	w5,w4,6
	orr	w5,w5,0xC0
	strb	w5,[x0],1
	and	w5,w4,0x3F
	orr	w5,w5,0x80
	strb	w5,[x0],1
	b	.c
.end:	strb	wzr,[x0]
	ret

; le prochain rendez-vous : agenda.txt du Calendrier, « AAAAMMJJ|[H:MM ]titre »
; -> ag_ok, ag_key, ag_min (-1 : sans heure), ag_title
agenda_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stv	xzr,ag_ok
	bl	now_local
	movl	x1,86400
	udiv	x2,x0,x1
	msub	x3,x2,x1,x0
	mov	x1,60
	udiv	x25,x3,x1			; x25 : minute du jour
	mov	x0,x2
	bl	day_key
	mov	x23,x0				; x23 : aujourd'hui
	stv	x0,ag_today
	adr	x0,s_agenda
	bl	read_file
	adr	x19,fbuf
	add	x20,x19,x0			; fin
	mov	x26,-1				; x26 : le meilleur (cle composee)
.line:	cmp	x19,x20
	b.hs	.done
	mov	x21,x19				; debut de ligne
.e:	cmp	x19,x20
	b.hs	.le
	ldrb	w0,[x19]
	cmp	w0,10
	b.eq	.le
	add	x19,x19,1
	b	.e
.le:	mov	x22,x19				; fin de ligne
	add	x19,x19,1
	sub	x0,x22,x21
	cmp	x0,10
	b.lo	.line
	ldrb	w0,[x21,8]
	cmp	w0,'|'
	b.ne	.line
	mov	x0,x21
	mov	x1,8
	bl	digits
	tbnz	x0,63,.line
	cmp	x0,x23				; (les jours passes : non)
	b.lo	.line
	mov	x24,x0				; x24 : cle
	add	x27,x21,9			; x27 : le titre ; x28 : minute
	mov	x28,-1
	mov	x0,x27				; « H:MM » ou « HH:MM », puis un espace
	mov	x1,1
	bl	digits
	tbnz	x0,63,.cand
	mov	x2,x0
	add	x3,x27,1
	ldrb	w4,[x3]
	cmp	w4,':'
	b.eq	.mm
	sub	w4,w4,'0'
	cmp	w4,9
	b.hi	.cand
	mov	x5,10
	madd	x2,x2,x5,x4
	add	x3,x3,1
	ldrb	w4,[x3]
	cmp	w4,':'
	b.ne	.cand
.mm:	add	x0,x3,1
	stp	x2,x3,[sp,-16]!
	mov	x1,2
	bl	digits
	ldp	x2,x3,[sp],16
	tbnz	x0,63,.cand
	ldrb	w4,[x3,3]
	cmp	w4,' '
	b.ne	.cand
	mov	x5,60
	madd	x28,x2,x5,x0
	add	x27,x3,4
.cand:	cmp	x24,x23				; aujourd'hui, l'heure passee : non
	b.ne	.keep
	tbnz	x28,63,.keep
	cmp	x28,x25
	b.lo	.line
.keep:	mov	x0,2048				; cle composee : jour, puis minute
	add	x1,x28,1			; (sans heure : en tete du jour)
	madd	x0,x24,x0,x1
	cmp	x0,x26
	b.hs	.line
	mov	x26,x0
	stv	x24,ag_key
	stv	x28,ag_min
	mov	x0,1
	stv	x0,ag_ok
	adr	x0,ag_title
	mov	x1,x27
	mov	x2,x22
	mov	x3,AG_TITLE
	bl	to_utf8
	b	.line
.done:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la meteo que PocketQI a gardee : SD0:/home/pocketqi/weather.txt (cles temp,
; sky, city, forecast, time ; docs/MODE-POCHE.md) -> wx_ok
weather_load:
	prologue
	stv	xzr,wx_ok
	adr	x0,wx_temp
	strb	wzr,[x0]
	adr	x0,wx_sky
	strb	wzr,[x0]
	adr	x0,wx_city
	strb	wzr,[x0]
	adr	x0,wx_fc
	strb	wzr,[x0]
	stv	xzr,wx_time
	adr	x0,s_weather
	adr	x1,wx_line
	bl	ini_read
	cbz	x0,.out
	adr	x0,wx_temp			; une temperature, au moins
	ldrb	w0,[x0]
	cbz	w0,.out
	ldv	x1,wx_time			; et pas trop vieille (12 h)
	cbz	x1,.ok
	mov	x0,0
	sys	SYS_CLOCK_GET
	ldv	x1,wx_time
	sub	x0,x0,x1
	movl	x1,43200
	cmp	x0,x1
	b.gt	.out
.ok:	mov	x0,1
	stv	x0,wx_ok
.out:	epilogue

; (ini_read) x0 = cle, x1 = valeur
wx_line:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x1,k_temp
	bl	str_eq
	adr	x2,wx_temp
	mov	x3,16
	cbnz	x0,.copy
	mov	x0,x19
	adr	x1,k_sky
	bl	str_eq
	adr	x2,wx_sky
	mov	x3,48
	cbnz	x0,.copy
	mov	x0,x19
	adr	x1,k_city
	bl	str_eq
	adr	x2,wx_city
	mov	x3,48
	cbnz	x0,.copy
	mov	x0,x19
	adr	x1,k_forecast
	bl	str_eq
	adr	x2,wx_fc
	mov	x3,64
	cbnz	x0,.copy
	mov	x0,x19
	adr	x1,k_time
	bl	str_eq
	cbz	x0,.out
	mov	x0,x20
	bl	parse_dec
	stv	x0,wx_time
	b	.out
.copy:	mov	x0,x2
	mov	x1,x20
	mov	x2,x3
	bl	str_copy_n
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> x0 apres le mot et les espaces qui suivent
skip_word:
.w:	ldrb	w1,[x0]
	cbz	w1,.r
	cmp	w1,' '
	b.eq	.s
	add	x0,x0,1
	b	.w
.s:	ldrb	w1,[x0]
	cmp	w1,' '
	b.ne	.r
	add	x0,x0,1
	b	.s
.r:	ret

; la musique en cours, d'apres korin.media.now : « <etat> <position ms>
; <duree ms> <instant ms> <app> <titre> » (docs/MODE-POCHE.md)
media_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stv	xzr,md_state
	adr	x0,t_media
	adr	x1,md_raw
	mov	x2,BUS_VALUE_MAX
	bl	bus_get
	cmp	x0,0
	b.le	.out
	cmp	x0,BUS_VALUE_MAX
	mov	x1,BUS_VALUE_MAX
	csel	x0,x1,x0,hi
	adr	x1,md_raw
	strb	wzr,[x1,x0]
	mov	x19,MD_PLAY
	adr	x0,md_raw
	adr	x1,s_playing
	bl	md_word
	cbnz	x0,.st
	mov	x19,MD_PAUSE
	adr	x0,md_raw
	adr	x1,s_paused
	bl	md_word
	cbnz	x0,.st
	mov	x19,MD_STOP
	adr	x0,md_raw
	adr	x1,s_stopped
	bl	md_word
	cbz	x0,.out
.st:	adr	x0,md_raw
	bl	skip_word
	mov	x20,x0
	bl	parse_dec
	stv	x0,md_pos
	mov	x0,x20
	bl	skip_word
	mov	x20,x0
	bl	parse_dec
	stv	x0,md_dur
	mov	x0,x20
	bl	skip_word
	mov	x20,x0
	bl	parse_dec
	stv	x0,md_t
	mov	x0,x20
	bl	skip_word
	mov	x20,x0				; l'app : jusqu'a l'espace
	adr	x1,md_app
	mov	x2,AP_NOM-1
.a:	ldrb	w3,[x20]
	cbz	w3,.ae
	cmp	w3,' '
	b.eq	.ae
	strb	w3,[x1],1
	add	x20,x20,1
	subs	x2,x2,1
	b.ne	.a
.ae:	strb	wzr,[x1]
	mov	x0,x20
	bl	skip_word.s
	mov	x1,x0
	adr	x0,md_title
	mov	x2,AG_TITLE
	bl	str_copy_n
	stv	x19,md_state
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur, x1 = mot -> x0 = 1 si la valeur commence par ce mot (puis espace)
md_word:
.c:	ldrb	w2,[x1],1
	cbz	w2,.end
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	.c
	mov	x0,0
	ret
.end:	ldrb	w3,[x0]
	cmp	w3,' '
	cset	x0,eq
	ret

; c_l1, c_l2, c_icon, c_full (1 : une donnee) des trois cartes
cards_text:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	; --- l'agenda
	adr	x0,s_ic_cal
	stv	x0,c_icon
	ldv	x0,ag_ok
	adr	x1,c_full
	str	x0,[x1]
	cbnz	x0,.ag
	adr	x0,s_e_agenda
	adr	x1,s_n_agenda
	mov	x2,0
	bl	card_empty
	b	.wx
.ag:	adr	x19,c_l1			; « 15:00 Dentiste »
	strb	wzr,[x19]
	ldv	x1,ag_min
	tbnz	x1,63,.agt
	mov	x0,x19
	bl	put_hm
	mov	w1,' '
	strb	w1,[x0],1
	mov	x19,x0
.agt:	mov	x0,x19
	adr	x1,ag_title
	bl	str_cat
	adr	x19,c_l2			; quand
	ldv	x20,ag_key
	ldv	x0,ag_today
	cmp	x20,x0
	b.ne	.later
	ldv	x1,ag_min
	tbz	x1,63,.soon
	adr	x0,s_today
	bl	tr_x0
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
	b	.wx
.soon:	bl	now_local			; « dans 28 minutes », « dans 2 h 05 »
	movl	x1,86400
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	mov	x1,60
	udiv	x0,x0,x1
	ldv	x1,ag_min
	subs	x20,x1,x0
	csel	x20,xzr,x20,lt
	cbnz	x20,.dans
	adr	x0,s_now
	bl	tr_x0
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
	b	.wx
.dans:	adr	x0,s_dans
	bl	tr_x0
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	cmp	x20,60
	b.hs	.hours
	mov	x0,x20
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	x19,x0
	adr	x0,s_minutes
	cmp	x20,1
	b.ne	.mn
	adr	x0,s_minute
.mn:	bl	tr_x0
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
	b	.wx
.hours:	mov	x2,60
	udiv	x0,x20,x2
	msub	x21,x0,x2,x20
	bl	fmt_dec
	adr	x1,s_h
	bl	str_cat
	mov	x1,x21
	bl	put2
	b	.wx
.later:	bl	now_local			; demain, ou « Mer 12 nov. »
	movl	x1,86400
	udiv	x0,x0,x1
	add	x21,x0,1
	mov	x0,x21
	bl	day_key
	cmp	x0,x20
	b.ne	.date
	adr	x0,s_tomorrow
	bl	tr_x0
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
	b	.wx
.date:	mov	x22,400				; jours de cette date : depuis demain, en
					; avancant (au plus 400 : la recherche s'arrete)
.dd:	mov	x0,x21
	bl	day_key
	cmp	x0,x20
	b.eq	.dfound
	add	x21,x21,1
	subs	x22,x22,1
	b.ne	.dd
	mov	x0,x19				; (loin : la date en chiffres)
	strb	wzr,[x0]
	b	.wx
.dfound:
	add	x0,x21,3
	mov	x1,7
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	adr	x1,s_wd_short
	add	x0,x1,x0,lsl 3
	bl	tr_x0
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	mov	x19,x0
	mov	x0,x21
	bl	civil_from_days
	mov	x21,x1
	mov	x0,x2
	mov	x1,x19
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	x19,x0
	adr	x0,s_mo_short
	sub	x1,x21,1
	add	x0,x0,x1,lsl 3
	bl	tr_x0
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
	; --- la meteo
.wx:	adr	x0,s_ic_qi
	adr	x1,c_icon
	str	x0,[x1,8]
	ldv	x0,wx_ok
	adr	x1,c_full
	str	x0,[x1,8]
	cbnz	x0,.wxd
	adr	x0,s_e_weather
	adr	x1,s_n_weather
	mov	x2,1
	bl	card_empty
	b	.md
.wxd:	adr	x0,c_l1+96			; « 12° nuageux »
	adr	x1,wx_temp
	bl	str_cat
	mov	w1,0xC2
	strb	w1,[x0],1
	mov	w1,0xB0
	strb	w1,[x0],1
	mov	w1,' '
	strb	w1,[x0],1
	adr	x1,wx_sky
	bl	str_cat
	adr	x0,c_l2+96			; « Lyon · demain 14° »
	adr	x1,wx_city
	bl	str_cat
	adr	x1,wx_fc
	ldrb	w1,[x1]
	cbz	w1,.md
	adr	x1,wx_city
	ldrb	w1,[x1]
	cbz	w1,.fc
	adr	x1,s_dot
	bl	str_cat
.fc:	adr	x1,wx_fc
	bl	str_cat
	; --- la musique
.md:	adr	x0,s_ic_music
	ldv	x1,md_state
	cbz	x1,.mic
	adr	x0,md_app
.mic:	adr	x1,c_icon
	str	x0,[x1,16]
	ldv	x0,md_state
	cmp	x0,MD_PLAY
	cset	x1,eq
	cmp	x0,MD_PAUSE
	cinc	x1,x1,eq
	adr	x2,c_full
	str	x1,[x2,16]
	cbnz	x1,.mdd
	adr	x0,s_e_music
	adr	x1,s_n_music
	mov	x2,2
	bl	card_empty
	b	.out
.mdd:	adr	x0,c_l1+192			; le titre
	adr	x1,md_title
	bl	str_cat
	adr	x19,c_l2+192			; « Musique · 1:23 / 3:25 »
	adr	x0,md_app
	bl	app_find
	adr	x1,md_app
	tbnz	x0,63,.mapp
	bl	app_label
	mov	x1,x0
.mapp:	mov	x0,x19
	bl	str_cat
	adr	x1,s_dot
	bl	str_cat
	mov	x19,x0
	ldv	x20,md_pos			; la position, avancee depuis l'instant
	ldv	x0,md_state
	cmp	x0,MD_PLAY
	b.ne	.pos
	sys	SYS_TIME_MS
	ldv	x1,md_t
	subs	x0,x0,x1
	csel	x0,xzr,x0,lt
	add	x20,x20,x0
	ldv	x0,md_dur
	cbz	x0,.pos
	cmp	x20,x0
	csel	x20,x0,x20,hi
.pos:	mov	x0,x19
	mov	x1,x20
	bl	put_ms
	ldv	x1,md_dur
	cbz	x1,.paus
	mov	x19,x0
	adr	x1,s_slash
	bl	str_cat
	ldv	x1,md_dur
	bl	put_ms
.paus:	ldv	x1,md_state
	cmp	x1,MD_PAUSE
	b.ne	.out
	adr	x1,s_dot
	bl	str_cat
	mov	x19,x0
	adr	x0,s_en_pause
	bl	tr_x0
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = ms -> « m:ss », x0 = fin
put_ms:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x2,1000
	udiv	x1,x1,x2
	mov	x2,60
	udiv	x0,x1,x2
	msub	x20,x0,x2,x1
	mov	x1,x19
	bl	fmt_dec
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,x20
	bl	put2
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat vide, x1 = nom de la source, x2 = carte : ses deux lignes
card_empty:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x3,96
	mul	x21,x2,x3
	mov	x0,x19
	bl	tr_x0
	mov	x1,x0
	adr	x0,c_l1
	add	x0,x0,x21
	bl	str_cat
	mov	x0,x20
	bl	tr_x0
	mov	x1,x0
	adr	x0,c_l2
	add	x0,x0,x21
	bl	str_cat
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Recentes, ouvertes
; ---------------------------------------------------------------------------

; rw_app <- les recentes, puis les raccourcis du panel, puis les applications
; par nom, jusqu'a HM_NCELL (sans la coquille, sans doublon)
rows_build:
	prologue
	stp	x19,x20,[sp,-16]!
	stv	xzr,rw_n
	mov	x19,0
.r:	adr	x0,rc_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.p
	adr	x0,rc_app
	ldrb	w0,[x0,x19]
	add	x19,x19,1
	bl	rw_add
	b	.r
.p:	mov	x19,0
.pp:	adr	x0,pan_npin
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.a
	adr	x0,pan_pin
	add	x0,x0,x19,lsl 5
	add	x19,x19,1
	bl	app_find
	tbnz	x0,63,.pp
	bl	rw_add
	b	.pp
.a:	mov	x19,0
.aa:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	adr	x0,ap_ordre
	ldrb	w0,[x0,x19]
	add	x19,x19,1
	bl	rw_add
	b	.aa
.out:	ldv	x0,g_zone			; la selection reste dans la rangee
	cmp	x0,Z_RECENT
	b.ne	.ret
	ldv	x0,g_sel
	ldv	x1,rw_n
	sub	x1,x1,1
	cmp	x0,x1
	csel	x0,x1,x0,gt
	cmp	x0,0
	csel	x0,xzr,x0,lt
	stv	x0,g_sel
.ret:	ldp	x19,x20,[sp],16
	epilogue

; x0 = application : ajoutee a la rangee (pas la coquille, pas deux fois)
rw_add:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldv	x1,rw_n
	cmp	x1,HM_NCELL
	b.hs	.out
	bl	pan_shell
	cbnz	x0,.out
	ldv	x1,rw_n
	adr	x2,rw_app
	mov	x3,0
.d:	cmp	x3,x1
	b.hs	.add
	ldrb	w4,[x2,x3]
	cmp	x4,x19
	b.eq	.out
	add	x3,x3,1
	b	.d
.add:	strb	w19,[x2,x1]
	add	x1,x1,1
	stv	x1,rw_n
.out:	ldr	x19,[sp],16
	epilogue

; x0 = pid -> x0 = son application (-1 : aucune du catalogue)
app_of_pid:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.p:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.info
	adr	x0,pids
	ldr	x0,[x0,x20,lsl 3]
	cmp	x0,x19
	b.eq	.got
	add	x20,x20,1
	b	.p
.info:	mov	x0,x19				; un autre process d'une application ?
	adr	x1,pinfo
	sys	SYS_PROC_INFO
	tbnz	x0,63,.none
	adr	x0,pinfo+24
	bl	app_find
	b	.out
.got:	mov	x0,x20
	b	.out
.none:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; les fenetres d'Elegant : nw_* (une entree par programme qui a une fenetre
; normale, sa fenetre la plus devant), g_front_win, g_front_idx (la plus
; devant des visibles), g_my_idx
win_scan:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stv	xzr,nw_n
	stv	xzr,g_front_win
	mov	x0,-1
	stv	x0,g_front_idx
	stv	x0,g_my_idx
	mov	x19,0				; premiere de la page
.page:	mov	x0,x19
	adr	x1,wdet
	bl	ui_win_details
	cmp	x0,x19
	b.le	.out
	sub	x20,x0,x19
	cmp	x20,WE_PER_MSG
	mov	x1,WE_PER_MSG
	csel	x20,x1,x20,gt
	adr	x21,wdet
	mov	x23,x19				; x23 : numero de la fenetre
.e:	cbz	x20,.np
	ldr	w0,[x21,WE_ID]
	ldv	x1,g_win
	cmp	x0,x1
	b.ne	.other
	stv	x23,g_my_idx
	b	.en
.other:	ldr	w0,[x21,WE_FLAGS]
	mov	x1,WF_TOP or WF_BACK or WF_NOLIST or WF_GRAB
	tst	x0,x1
	b.ne	.en
	ldr	w0,[x21,WE_STATE]
	tbnz	w0,1,.listed			; (WES_MINI : cachee)
	ldr	w1,[x21,WE_ID]
	stv	x1,g_front_win
	stv	x23,g_front_idx
.listed:
	ldr	w22,[x21,WE_PID]		; ce programme : deja vu ?
	ldv	x1,nw_n
	adr	x2,nw_pid
	mov	x3,0
.f:	cmp	x3,x1
	b.hs	.new
	ldr	x4,[x2,x3,lsl 3]
	cmp	x4,x22
	b.eq	.upd
	add	x3,x3,1
	b	.f
.new:	cmp	x1,OP_MAX
	b.hs	.en
	str	x22,[x2,x1,lsl 3]
	mov	x3,x1
	add	x1,x1,1
	stv	x1,nw_n
	adr	x0,nw_title			; son titre
	add	x0,x0,x3,lsl 5
	add	x1,x21,WE_TITLE
	mov	x2,32
	str	x3,[sp,-16]!
	bl	str_copy_n
	ldr	x3,[sp],16
.upd:	ldr	w0,[x21,WE_ID]			; la plus devant de ses fenetres
	adr	x2,nw_win
	str	x0,[x2,x3,lsl 3]
.en:	add	x21,x21,WE_SIZE
	add	x23,x23,1
	sub	x20,x20,1
	b	.e
.np:	add	x19,x19,WE_PER_MSG
	b	.page
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la bande « Ouvertes » : op_* dans l'ordre ou elles sont venues -> x0 = 1 si
; quelque chose a change
open_build:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	apps_running
	mov	x23,x0				; x23 : change ?
	bl	win_scan
	ldv	x0,g_front_win			; l'app devant
	mov	x24,0
	cbz	x0,.fr
	ldv	x1,nw_n
	adr	x2,nw_win
	adr	x3,nw_pid
	mov	x4,0
.fw:	cmp	x4,x1
	b.hs	.fr
	ldr	x5,[x2,x4,lsl 3]
	cmp	x5,x0
	b.eq	.fp
	add	x4,x4,1
	b	.fw
.fp:	ldr	x24,[x3,x4,lsl 3]
.fr:	ldv	x0,op_front
	cmp	x0,x24
	cinc	x23,x23,ne
	stv	x24,op_front
	mov	x19,0				; les anciennes encore la, dans leur ordre
	mov	x20,0				; (x20 : gardees)
.o:	ldv	x0,op_n
	cmp	x19,x0
	b.hs	.add
	adr	x0,op_pid
	ldr	x21,[x0,x19,lsl 3]
	mov	x0,x21
	bl	nw_find
	tbnz	x0,63,.gone
	mov	x1,x0
	mov	x0,x20
	bl	op_set
	add	x20,x20,1
	b	.on
.gone:	add	x23,x23,1
.on:	add	x19,x19,1
	b	.o
.add:	mov	x19,0				; puis les nouvelles
.a:	ldv	x0,nw_n
	cmp	x19,x0
	b.hs	.done
	adr	x0,nw_pid
	ldr	x21,[x0,x19,lsl 3]
	mov	x0,x21				; (deja gardee ?)
	mov	x1,0
.k:	cmp	x1,x20
	b.hs	.put
	adr	x2,op_pid
	ldr	x2,[x2,x1,lsl 3]
	cmp	x2,x21
	b.eq	.an
	add	x1,x1,1
	b	.k
.put:	cmp	x20,OP_MAX
	b.hs	.done
	mov	x0,x20
	mov	x1,x19
	bl	op_set
	add	x20,x20,1
	add	x23,x23,1
.an:	add	x19,x19,1
	b	.a
.done:	stv	x20,op_n
	ldv	x0,g_zone			; la selection reste dans la bande
	cmp	x0,Z_OPEN
	b.ne	.ret
	ldv	x0,g_sel
	sub	x1,x20,1
	cmp	x0,x1
	csel	x0,x1,x0,gt
	cmp	x0,0
	csel	x0,xzr,x0,lt
	stv	x0,g_sel
.ret:	cmp	x23,0
	cset	x0,ne
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid -> x0 = son numero dans nw_* (-1 : aucun)
nw_find:
	ldv	x1,nw_n
	adr	x2,nw_pid
	mov	x3,0
.f:	cmp	x3,x1
	b.hs	.none
	ldr	x4,[x2,x3,lsl 3]
	cmp	x4,x0
	b.eq	.got
	add	x3,x3,1
	b	.f
.got:	mov	x0,x3
	ret
.none:	mov	x0,-1
	ret

; x0 = place dans op_*, x1 = numero dans nw_* : recopiee (son application)
op_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,nw_pid
	ldr	x2,[x0,x20,lsl 3]
	adr	x0,op_pid
	str	x2,[x0,x19,lsl 3]
	adr	x0,nw_win
	ldr	x3,[x0,x20,lsl 3]
	adr	x0,op_win
	str	x3,[x0,x19,lsl 3]
	adr	x0,op_title
	add	x0,x0,x19,lsl 5
	adr	x1,nw_title
	add	x1,x1,x20,lsl 5
	mov	x2,32
	bl	str_copy_n
	adr	x0,nw_pid
	ldr	x0,[x0,x20,lsl 3]
	bl	app_of_pid
	adr	x1,op_app
	str	x0,[x1,x19,lsl 3]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = place dans op_* -> x0 = son nom (libelle de l'app, sinon le titre)
op_label:
	adr	x1,op_app
	ldr	x2,[x1,x0,lsl 3]
	tbnz	x2,63,.t
	mov	x0,x2
	b	app_label
.t:	adr	x1,op_title
	add	x0,x1,x0,lsl 5
	ret

; x0 = place dans op_* : ses fenetres recoivent WEV_CLOSE (la croix)
op_close:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x1,op_pid
	ldr	x19,[x1,x0,lsl 3]
	mov	x20,0
.page:	mov	x0,x20
	adr	x1,wdet
	bl	ui_win_details
	cmp	x0,x20
	b.le	.out
	sub	x21,x0,x20
	cmp	x21,WE_PER_MSG
	mov	x1,WE_PER_MSG
	csel	x21,x1,x21,gt
	adr	x22,wdet
.e:	cbz	x21,.np
	ldr	w0,[x22,WE_PID]
	cmp	x0,x19
	b.ne	.en
	ldr	w0,[x22,WE_FLAGS]
	mov	x1,WF_TOP or WF_BACK or WF_NOLIST or WF_GRAB
	tst	x0,x1
	b.ne	.en
	adr	x0,g_msg
	mov	x1,WS_OP_WIN_CLOSE
	str	x1,[x0,WM_OP]
	ldr	w1,[x22,WE_ID]
	str	x1,[x0,WM_A]
	str	wzr,[x0,WM_LEN]
	sys	SYS_WS_CALL
.en:	add	x22,x22,WE_SIZE
	sub	x21,x21,1
	b	.e
.np:	add	x20,x20,WE_PER_MSG
	b	.page
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Recherche : applications, applets de Reglages, fichiers de SD0:/home
; ---------------------------------------------------------------------------

; -> x0 = 1 si une recherche est en cours
searching:
	adr	x0,g_q
	ldrb	w0,[x0]
	cmp	w0,0
	cset	x0,ne
	ret

; x0 = genre (1 app, 2 reglage, 3 fichier), x1 = reference, x2 = texte, x3 =
; nom de l'icone -> x0 = place (-1 : plein)
res_add:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x19,res_n
	cmp	x19,RES_MAX
	b.hs	.full
	adr	x4,res_kind
	strb	w0,[x4,x19]
	adr	x4,res_ref
	str	x1,[x4,x19,lsl 3]
	mov	x20,x2
	mov	x21,x3
	mov	x0,RES_TXT
	adr	x1,res_text
	madd	x0,x19,x0,x1
	mov	x1,x20
	mov	x2,RES_TXT
	bl	str_copy_n
	adr	x0,res_icon
	add	x0,x0,x19,lsl 5
	mov	x1,x21
	mov	x2,AP_NOM
	bl	str_copy_n
	add	x0,x19,1
	stv	x0,res_n
	mov	x0,x19
	b	.out
.full:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> x0 = 1 si, sans casse ni accents, il contient la recherche
q_match:
	prologue
	mov	x1,x0
	adr	x0,la_t
	bl	la_norm
	adr	x0,la_t
	adr	x1,g_q
	bl	la_has
	epilogue

; res_* <- ce que la recherche trouve
search_run:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stv	xzr,res_n
	stv	xzr,res_sel
	stv	xzr,res_top
	ldv	x0,g_box
	bl	ui_w_get_text
	mov	x1,x0
	adr	x0,g_q
	bl	la_norm
	bl	searching
	cbz	x0,.out
	mov	x19,0				; --- les applications, par nom
	mov	x20,0
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.sets
	cmp	x20,RES_APPS
	b.hs	.sets
	adr	x0,ap_ordre
	ldrb	w21,[x0,x19]
	add	x19,x19,1
	mov	x0,x21
	bl	pan_shell
	cbnz	x0,.a
	mov	x0,x21
	bl	app_label
	bl	q_match
	cbnz	x0,.ak
	adr	x0,ap_nom
	add	x0,x0,x21,lsl 5
	bl	q_match
	cbz	x0,.a
.ak:	mov	x0,x21
	bl	app_label
	mov	x2,x0
	mov	x0,1
	mov	x1,x21
	adr	x3,ap_nom
	add	x3,x3,x21,lsl 5
	bl	res_add
	add	x20,x20,1
	b	.a
.sets:	adr	x19,settings			; --- les applets de Reglages
	mov	x20,0
.s:	ldrb	w0,[x19]
	cbz	w0,.files
	cmp	x20,RES_SETS
	b.hs	.files
	mov	x21,x19				; argument, libelle, mots
	mov	x0,x19
	bl	str_len
	add	x22,x19,x0
	add	x22,x22,1			; x22 : libelle
	mov	x0,x22
	bl	str_len
	add	x19,x22,x0
	add	x19,x19,1			; x19 : mots
	mov	x0,x22
	bl	tr_x0
	bl	q_match
	cbnz	x0,.sk
	mov	x0,x19
	bl	q_match
.sk:	str	x0,[sp,-16]!
	mov	x0,x19				; (la suivante)
	bl	str_len
	add	x19,x19,x0
	add	x19,x19,1
	ldr	x0,[sp],16
	cbz	x0,.s
	mov	x0,x22
	bl	tr_x0
	mov	x2,x0
	mov	x0,2
	mov	x1,x21
	adr	x3,s_ic_control
	bl	res_add
	add	x20,x20,1
	b	.s
.files:	adr	x0,g_q				; --- les fichiers (deux lettres au moins)
	bl	str_len
	cmp	x0,2
	b.lo	.out
	mov	x0,FS_BUDGET
	stv	x0,fs_budget
	stv	xzr,fs_found
	adr	x0,fs_path
	adr	x1,s_homedir
	bl	str_cat
	adr	x0,fs_path
	mov	x1,0
	bl	fs_walk
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin d'un dossier (dans fs_path : on y ajoute, puis on le rend),
; x1 = profondeur : les noms qui correspondent
fs_walk:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,DIRENT_BYTES
	mov	x20,x1
	bl	str_len
	adr	x21,fs_path
	add	x21,x21,x0			; x21 : fin du chemin du dossier
	adr	x0,fs_path
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
.next:	ldv	x0,fs_budget
	cbz	x0,.close
	sub	x0,x0,1
	stv	x0,fs_budget
	ldv	x0,fs_found
	cmp	x0,RES_FILES
	b.hs	.close
	mov	x0,x19
	mov	x1,sp
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.close
	ldrb	w0,[sp]				; ni « . » ni « .. » ni caches
	cmp	w0,'.'
	b.eq	.next
	mov	x0,x21				; le chemin de l'entree
	mov	w1,'/'
	strb	w1,[x0],1
	mov	x1,sp
	bl	str_cat
	adr	x1,fs_path			; (trop long : ni garde ni parcouru)
	sub	x0,x0,x1
	cmp	x0,RES_PATH-2
	b.hs	.cut
	mov	x0,sp
	bl	q_match
	cbz	x0,.sub
	bl	fs_add
.sub:	ldr	x0,[sp,DIRENT_ATTR]		; un dossier : dedans
	tst	x0,ATTR_DIR
	b.eq	.cut
	cmp	x20,2
	b.hs	.cut
	adr	x0,fs_path
	add	x1,x20,1
	bl	fs_walk
.cut:	strb	wzr,[x21]
	b	.next
.close:	mov	x0,x19
	sys	SYS_CLOSE
.out:	strb	wzr,[x21]
	add	sp,sp,DIRENT_BYTES
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; fs_path (une entree trouvee ; son nom apres le dernier « / ») : un resultat,
; l'icone de l'app qui l'ouvre
fs_add:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,fs_path			; le nom
	mov	x20,x19
.n:	ldrb	w0,[x19],1
	cbz	w0,.nd
	cmp	w0,'/'
	b.ne	.n
	mov	x20,x19
	b	.n
.nd:	adr	x0,fs_path			; l'app qui l'ouvre : SD0:/apps/<nom>.app/main
	adr	x1,fs_app
	mov	x2,RES_PATH
	bl	ui_assoc_app
	adr	x3,s_ic_editor
	cbz	x0,.ic
	adr	x0,fs_app
	adr	x1,ap_path
	bl	str_prefix
	cbz	x0,.ic
	mov	x3,x0
	mov	x1,x0				; « <nom>.app/main » -> « <nom> »
.dot:	ldrb	w2,[x1]
	cbz	w2,.ic
	cmp	w2,'.'
	b.eq	.cutn
	add	x1,x1,1
	b	.dot
.cutn:	strb	wzr,[x1]
.ic:	mov	x0,3
	mov	x1,0
	mov	x2,x20
	bl	res_add
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x1,RES_PATH
	adr	x2,res_path
	madd	x0,x19,x1,x2
	adr	x1,fs_path
	mov	x2,RES_PATH
	bl	str_copy_n
	ldv	x0,fs_found
	add	x0,x0,1
	stv	x0,fs_found
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Ouvrir, fermer
; ---------------------------------------------------------------------------

; ce qui est choisi au clavier (ou x0 = place dans la zone) : ouvert
activate:
	prologue
	bl	searching
	cbnz	x0,.res
	ldv	x0,g_zone
	ldv	x1,g_sel
	cmp	x0,Z_CARDS
	b.eq	.card
	cmp	x0,Z_RECENT
	b.eq	.rec
	ldv	x0,op_n				; Ouvertes : devant
	cmp	x1,x0
	b.hs	.out
	adr	x0,op_win
	ldr	x0,[x0,x1,lsl 3]
	bl	ui_raise
	b	.out
.rec:	ldv	x0,rw_n
	cmp	x1,x0
	b.hs	.out
	adr	x0,rw_app
	ldrb	w0,[x0,x1]
	bl	app_open
	b	.out
.card:	mov	x0,x1
	bl	card_open
	b	.out
.res:	ldv	x0,res_sel
	bl	res_open
.out:	epilogue

; x0 = carte : son application (agenda : le Calendrier a ce jour)
card_open:
	prologue
	cmp	x0,1
	b.eq	.qi
	b.hi	.music
	ldv	x0,ag_ok			; le Calendrier
	cbz	x0,.cal
	ldv	x0,ag_key
	adr	x1,tbuf
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,p_calendar
	adr	x1,tbuf
	sys	SYS_SPAWN
	b	.out
.cal:	adr	x0,s_ic_cal
	b	.app
.qi:	adr	x0,s_ic_qi
	b	.app
.music:	adr	x0,md_app			; le lecteur qui joue, sinon Media Player
	ldv	x1,md_state
	cbnz	x1,.app
	adr	x0,s_media
	bl	app_find
	tbz	x0,63,.open
	adr	x0,s_ic_music
.app:	bl	app_find
	tbnz	x0,63,.out
.open:	bl	app_open
.out:	epilogue

; x0 = numero d'un resultat : ouvert, la recherche videe
res_open:
	prologue
	str	x19,[sp,-16]!
	ldv	x1,res_n
	cmp	x0,x1
	b.hs	.out
	mov	x19,x0
	adr	x1,res_kind
	ldrb	w1,[x1,x19]
	adr	x2,res_ref
	ldr	x0,[x2,x19,lsl 3]
	cmp	w1,2
	b.eq	.set
	b.hi	.file
	bl	app_open
	b	.clear
.set:	mov	x1,x0				; Reglages, a cet applet
	adr	x0,p_control
	sys	SYS_SPAWN
	b	.clear
.file:	mov	x0,RES_PATH
	adr	x1,res_path
	madd	x0,x19,x0,x1
	bl	ui_assoc_open
.clear:	bl	query_clear
.out:	ldr	x19,[sp],16
	epilogue

; la recherche videe, l'accueil redessine
query_clear:
	prologue
	ldv	x0,g_box
	adr	x1,s_empty
	bl	ui_w_set_text
	adr	x0,g_q
	strb	wzr,[x0]
	stv	xzr,res_n
	bl	draw_all
	epilogue

; la musique : pause ou reprise, par korin.media.control (« <app> toggle »)
media_toggle:
	prologue
	ldv	x0,md_state
	cbz	x0,.out
	adr	x0,tbuf
	adr	x1,md_app
	bl	str_cat
	adr	x1,s_toggle
	bl	str_cat
	adr	x0,t_control
	adr	x1,tbuf
	mov	x2,-1
	mov	x3,0
	bl	bus_publish
.out:	epilogue

; korin.shell.home : l'accueil devant ; il y est deja : l'app d'avant revient
home_toggle:
	prologue
	bl	win_scan
	ldv	x0,g_front_idx
	tbnz	x0,63,.front			; (aucune app visible)
	ldv	x1,g_my_idx
	cmp	x1,x0
	b.gt	.front
	ldv	x0,g_front_win			; montre l'accueil, garde l'app d'avant
	stv	x0,g_prev
	stv	xzr,g_zone			; (le clavier repart des Recentes)
	mov	x0,Z_RECENT
	stv	x0,g_zone
	stv	xzr,g_sel
	ldv	x0,g_win
	bl	ui_raise
	bl	searching
	cbz	x0,.draw
	bl	query_clear
	b	.out
.draw:	bl	draw_lower
	b	.out
.front:	ldv	x0,g_prev
	cbz	x0,.out
	stv	xzr,g_prev
	bl	ui_raise
.out:	epilogue

; ---------------------------------------------------------------------------
;  Evenements
; ---------------------------------------------------------------------------

on_bus:
	prologue
	adr	x9,ui_ev
	ldr	w0,[x9,BEV_SUB]
	ldv	x1,g_sub_home
	cmp	x0,x1
	b.eq	.home
	ldv	x1,g_sub_media
	cmp	x0,x1
	b.ne	.out
	bl	media_load
	bl	searching
	cbnz	x0,.out
	mov	x0,2
	mov	x1,1
	bl	draw_card
	b	.out
.home:	bl	home_toggle
.out:	epilogue

on_key:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cbz	w0,.pass			; relache
	ldr	w19,[x9,EV_CODE]
	bl	searching
	mov	x20,x0				; x20 : recherche en cours
	cmp	w19,KEY_ESC
	b.eq	.esc
	cmp	w19,KEY_ENTER
	b.eq	.enter
	cmp	w19,0x58			; (Entree du pave numerique)
	b.eq	.enter
	cmp	w19,KEY_TAB
	b.eq	.tab
	cmp	w19,KEY_DOWN
	b.eq	.down
	cmp	w19,KEY_UP
	b.eq	.up
	cbnz	x20,.pass			; (le champ garde gauche, droite, Suppr)
	cmp	w19,KEY_LEFT
	b.eq	.left
	cmp	w19,KEY_RIGHT
	b.eq	.right
	cmp	w19,KEY_DELETE
	b.eq	.del
.pass:	bl	box_event
	b	.out
.esc:	cbz	x20,.out
	bl	query_clear
	b	.out
.enter:	bl	activate
	b	.out
.tab:	cbnz	x20,.out
	adr	x9,ui_ev
	ldr	w0,[x9,EV_MODS]			; (Maj : en arriere)
	mov	w1,0x22
	tst	w0,w1
	b.ne	.up
.down:	cbnz	x20,.rdown
	mov	x0,1
	bl	zone_move
	b	.out
.up:	cbnz	x20,.rup
	mov	x0,-1
	bl	zone_move
	b	.out
.rdown:	mov	x0,1
	bl	res_move
	b	.out
.rup:	mov	x0,-1
	bl	res_move
	b	.out
.left:	mov	x0,-1
	bl	sel_move
	b	.out
.right:	mov	x0,1
	bl	sel_move
	b	.out
.del:	ldv	x0,g_zone
	cmp	x0,Z_OPEN
	b.ne	.out
	ldv	x0,g_sel
	ldv	x1,op_n
	cmp	x0,x1
	b.hs	.out
	bl	op_close
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = place dans une zone -> x0 = nombre d'elements de la zone x1
zone_count:
	cmp	x1,Z_CARDS
	b.ne	.r
	mov	x0,3
	ret
.r:	cmp	x1,Z_RECENT
	b.ne	.o
	adr	x0,rw_n
	ldr	x0,[x0]
	ret
.o:	adr	x0,op_n
	ldr	x0,[x0]
	ret

; x0 = +1 / -1 : la zone suivante (une zone vide est sautee)
zone_move:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldv	x20,g_zone
	mov	x2,3
.n:	add	x20,x20,x19
	cmp	x20,0
	mov	x3,Z_OPEN
	csel	x20,x3,x20,lt
	cmp	x20,Z_OPEN
	csel	x20,xzr,x20,gt
	mov	x1,x20
	bl	zone_count
	cbnz	x0,.got
	sub	x2,x2,1
	cbnz	x2,.n
	b	.out
.got:	stv	x20,g_zone
	stv	xzr,g_sel
	bl	draw_lower
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = +1 / -1 : dans la rangee
sel_move:
	prologue
	str	x19,[sp,-16]!
	ldv	x1,g_sel
	add	x19,x1,x0
	ldv	x1,g_zone
	bl	zone_count
	cbz	x0,.out
	cmp	x19,0
	b.lt	.out
	cmp	x19,x0
	b.hs	.out
	stv	x19,g_sel
	bl	draw_lower
.out:	ldr	x19,[sp],16
	epilogue

; x0 = +1 / -1 : dans les resultats, gardee visible
res_move:
	prologue
	ldv	x1,res_sel
	add	x1,x1,x0
	ldv	x2,res_n
	cmp	x1,0
	b.lt	.out
	cmp	x1,x2
	b.hs	.out
	stv	x1,res_sel
	bl	res_rows
	ldv	x2,res_top
	ldv	x1,res_sel
	cmp	x1,x2
	b.lt	.top
	add	x3,x2,x0
	cmp	x1,x3
	b.lo	.draw
	sub	x2,x1,x0
	add	x2,x2,1
	b	.st
.top:	mov	x2,x1
.st:	stv	x2,res_top
.draw:	bl	draw_lower
.out:	epilogue

; -> x0 = lignes de resultats montrees
res_rows:
	ldv	x0,g_band
	sub	x0,x0,HM_RES_Y+8
	mov	x1,HM_RES_H
	udiv	x0,x0,x1
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lt
	ret

on_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	ldrsw	x19,[x9,EV_X]
	ldrsw	x20,[x9,EV_Y]
	cmp	w0,1
	b.ne	.pass				; (deplacement, relache : au champ)
	ldv	x0,g_bx				; la recherche
	subs	x1,x19,x0
	b.lt	.nbox
	ldv	x2,g_bw
	cmp	x1,x2
	b.hs	.nbox
	subs	x1,x20,HM_BOX_Y
	b.lt	.nbox
	cmp	x1,LA_BOX_H
	b.lo	.pass
.nbox:	ldv	x0,g_band			; la bande
	cmp	x20,x0
	b.ge	.band
	bl	searching
	cbnz	x0,.res
	cmp	x20,HM_CARD_Y			; les cartes
	b.lt	.all
	cmp	x20,HM_CARD_Y+HM_CARD_H
	b.ge	.cells
	mov	x21,0
.c:	cmp	x21,3
	b.hs	.refocus
	mov	x0,x21
	bl	card_x
	subs	x1,x19,x0
	b.lt	.cn
	ldv	x2,g_cw
	cmp	x1,x2
	b.hs	.cn
	cmp	x21,2				; le bouton rond de la musique
	b.ne	.co
	ldv	x3,c_full+16
	cbz	x3,.co
	sub	x2,x2,14+HM_CARD_IC
	cmp	x1,x2
	b.lo	.co
	bl	media_toggle
	b	.refocus
.co:	stv	xzr,g_zone
	stv	x21,g_sel
	mov	x0,x21
	bl	card_open
	b	.refocus
.cn:	add	x21,x21,1
	b	.c
.all:	ldv	x0,g_all_x			; « Toutes les apps »
	subs	x1,x19,x0
	b.lt	.refocus
	ldv	x2,g_all_w
	cmp	x1,x2
	b.hs	.refocus
	cmp	x20,HM_RECENT_B-20
	b.lt	.refocus
	cmp	x20,HM_RECENT_B+8
	b.ge	.refocus
	bl	open_launcher
	b	.refocus
.cells:	cmp	x20,HM_ROW_Y			; « Toutes les apps » aussi (a cote
	b.lt	.all				; de « Recentes »)
	cmp	x20,HM_ROW_Y+HM_CELL
	b.ge	.refocus
	subs	x0,x19,HM_M
	b.lt	.refocus
	ldv	x1,g_gap
	add	x1,x1,HM_CELL
	udiv	x21,x0,x1
	msub	x2,x21,x1,x0
	cmp	x2,HM_CELL
	b.hs	.refocus
	ldv	x0,rw_n
	cmp	x21,x0
	b.hs	.refocus
	mov	x0,Z_RECENT
	stv	x0,g_zone
	stv	x21,g_sel
	adr	x0,rw_app
	ldrb	w0,[x0,x21]
	bl	app_open
	b	.refocus
.res:	cmp	x20,HM_RES_Y			; un resultat
	b.lt	.refocus
	bl	res_rows
	mov	x2,x0
	subs	x0,x20,HM_RES_Y
	mov	x1,HM_RES_H
	udiv	x0,x0,x1
	cmp	x0,x2
	b.hs	.refocus
	ldv	x1,res_top
	add	x0,x0,x1
	bl	res_open
	b	.refocus
.band:	mov	x21,0				; une pastille
.b:	ldv	x0,op_vis
	cmp	x21,x0
	b.hs	.refocus
	adr	x0,op_px
	ldr	x0,[x0,x21,lsl 3]
	subs	x1,x19,x0
	b.lt	.bn
	adr	x2,op_pw
	ldr	x2,[x2,x21,lsl 3]
	cmp	x1,x2
	b.hs	.bn
	mov	x0,Z_OPEN
	stv	x0,g_zone
	stv	x21,g_sel
	sub	x2,x2,6+HM_PILL_X		; la croix
	cmp	x1,x2
	b.lo	.raise
	mov	x0,x21
	bl	op_close
	b	.refocus
.raise:	adr	x0,op_win
	ldr	x0,[x0,x21,lsl 3]
	bl	ui_raise
	b	.refocus
.bn:	add	x21,x21,1
	b	.b
.pass:	bl	box_event
	b	.out
.refocus:
	bl	box_refocus
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; le lanceur du panel, en second niveau
open_launcher:
	prologue
	bl	launcher
	adr	x0,screen_todo			; (la resolution a change pendant)
	ldr	x1,[x0]
	cbz	x1,.draw
	str	xzr,[x0]
	bl	screen_follow
	b	.out
.draw:	bl	draw_all
.out:	epilogue

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------

; x0 = carte -> x0 = son x
card_x:
	adr	x1,g_cw
	ldr	x1,[x1]
	add	x1,x1,HM_CARD_G
	mov	x2,HM_M
	madd	x0,x0,x1,x2
	ret

; x0..x3 = rectangle, x4 = rayon, w5 = fond : une boite au contour LIGNE
lined_box:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	w24,w5
	jeton	w5,TJ_LIGNE
	mov	w6,w5
	bl	ui_rbox
	add	x0,x19,1
	add	x1,x20,1
	sub	x2,x21,2
	sub	x3,x22,2
	sub	x4,x23,1
	mov	w5,w24
	mov	w6,w24
	bl	ui_rbox
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; tout l'accueil
draw_all:
	prologue
	bl	clock_text
	jeton	w4,TJ_FACE
	mov	x0,0
	mov	x1,0
	ldv	x2,g_w
	mov	x3,HM_BODY_Y
	bl	ui_fill
	adr	x0,t_clock			; l'heure, la date
	mov	x1,HM_M-2
	mov	x2,HM_TIME_B
	jeton	w3,TJ_TEXTE1
	mov	x4,HM_TIME_PX
	adr	x5,ui_font_b
	ldv	x6,g_bx
	sub	x6,x6,HM_M+8
	bl	ui_text_fit
	adr	x0,t_date
	mov	x1,HM_M
	mov	x2,HM_DATE_B
	jeton	w3,TJ_TEXTE2
	mov	x4,HM_DATE_PX
	adr	x5,ui_font
	ldv	x6,g_bx
	sub	x6,x6,HM_M+8
	bl	ui_text_fit
	ldv	x0,g_bx				; la recherche
	mov	x1,HM_BOX_Y
	ldv	x2,g_bw
	bl	la_box_at
	ldv	x0,g_bx
	mov	x1,HM_BOX_Y
	ldv	x2,g_bw
	bl	la_clip_at
	ldv	x0,g_box
	bl	ui_w_draw
	mov	x2,0
	bl	ui_clip
	bl	draw_lower
	epilogue

; sous l'en-tete : cartes et recentes (ou resultats), la bande, montres
draw_lower:
	prologue
	stp	x19,x20,[sp,-16]!
	jeton	w4,TJ_FACE
	mov	x0,0
	mov	x1,HM_BODY_Y
	ldv	x2,g_w
	ldv	x3,g_band
	sub	x3,x3,HM_BODY_Y
	bl	ui_fill
	bl	searching
	cbz	x0,.home
	bl	draw_results
	b	.band
.home:	adr	x0,s_aujourdhui			; « Aujourd'hui »
	bl	tr_x0
	mov	x1,HM_M
	mov	x2,HM_TODAY_B
	bl	sec_title
	bl	media_load
	bl	cards_text
	mov	x19,0
.c:	mov	x0,x19
	mov	x1,0
	bl	draw_card
	add	x19,x19,1
	cmp	x19,3
	b.lo	.c
	adr	x0,s_recentes			; « Recentes », « Toutes les apps › »
	bl	tr_x0
	mov	x1,HM_M
	mov	x2,HM_RECENT_B
	bl	sec_title
	adr	x0,s_all
	bl	tr_x0
	mov	x19,x0
	mov	x1,HM_SEC_PX
	adr	x2,ui_font_b
	bl	ui_text_width
	stv	x0,g_all_w
	ldv	x1,g_w
	sub	x1,x1,HM_M
	sub	x1,x1,x0
	stv	x1,g_all_x
	mov	x0,x19
	mov	x2,HM_RECENT_B
	jeton	w3,TJ_ACCENT
	mov	x4,HM_SEC_PX
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x19,0
.r:	ldv	x0,rw_n
	cmp	x19,x0
	b.hs	.band
	mov	x0,x19
	bl	draw_cell
	add	x19,x19,1
	b	.r
.band:	bl	draw_band
	bl	ui_update_all
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = x, x2 = ligne de base : un titre de section (TEXTE2, gras)
sec_title:
	prologue
	jeton	w3,TJ_TEXTE2
	mov	x4,HM_SEC_PX
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	epilogue

; x0 = carte, x1 = 1 : montree tout de suite
draw_card:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x26,x1
	cbz	x26,.prep
	bl	media_load			; (la musique : son temps du moment)
	bl	cards_text
.prep:	mov	x0,x19
	bl	card_x
	mov	x20,x0				; x20 : x ; x21 : largeur
	ldv	x21,g_cw
	adr	x0,c_full
	ldr	x22,[x0,x19,lsl 3]		; x22 : une donnee
	jeton	w23,TJ_SURFACE
	ldv	x0,g_zone			; choisie au clavier
	cmp	x0,Z_CARDS
	b.ne	.bg
	ldv	x0,g_sel
	cmp	x0,x19
	b.ne	.bg
	jeton	w23,TJ_ACCENT_CLAIR
.bg:	jeton	w4,TJ_FACE
	mov	x0,x20
	mov	x1,HM_CARD_Y
	mov	x2,x21
	mov	x3,HM_CARD_H
	bl	ui_fill
	mov	x0,x20
	mov	x1,HM_CARD_Y
	mov	x2,x21
	mov	x3,HM_CARD_H
	mov	x4,HM_CARD_R
	mov	w5,w23
	bl	lined_box
	adr	x0,c_icon			; l'icone de l'app
	ldr	x0,[x0,x19,lsl 3]
	add	x1,x20,14
	mov	x2,HM_CARD_Y+(HM_CARD_H-HM_CARD_IC)/2
	mov	x3,HM_CARD_IC
	bl	ui_icone
	sub	x24,x21,14+HM_CARD_IC+12+14	; x24 : place du texte
	cmp	x19,2				; le bouton rond de la musique
	b.ne	.text
	cbz	x22,.text
	sub	x24,x24,HM_CARD_IC+8
	add	x0,x20,x21
	sub	x0,x0,14+HM_CARD_IC
	mov	x25,x0
	mov	x1,HM_CARD_Y+(HM_CARD_H-HM_CARD_IC)/2
	mov	x2,HM_CARD_IC
	mov	x3,HM_CARD_IC
	mov	x4,HM_CARD_IC/2
	jeton	w5,TJ_ACCENT_CLAIR
	jeton	w6,TJ_SURFACE
	cmp	w5,w23				; (sur une carte choisie : SURFACE)
	csel	w5,w6,w5,eq
	mov	w6,w5
	bl	ui_rbox
	ldv	x0,md_state
	cmp	x0,MD_PLAY
	b.ne	.play
	jeton	w4,TJ_ACCENT_FONCE		; deux barres : pause
	add	x0,x25,13
	mov	x1,HM_CARD_Y+(HM_CARD_H-14)/2
	mov	x2,4
	mov	x3,14
	bl	ui_fill
	jeton	w4,TJ_ACCENT_FONCE
	add	x0,x25,19
	mov	x1,HM_CARD_Y+(HM_CARD_H-14)/2
	mov	x2,4
	mov	x3,14
	bl	ui_fill
	b	.text
.play:	adr	x0,s_g_play			; un triangle : reprise
	add	x1,x25,10
	mov	x2,HM_CARD_Y+(HM_CARD_H-16)/2
	mov	x3,16
	jeton	w4,TJ_ACCENT_FONCE
	bl	ui_glyph
.text:	mov	x0,96
	adr	x1,c_l1
	madd	x0,x19,x0,x1
	add	x1,x20,14+HM_CARD_IC+12
	mov	x2,HM_CARD_Y+33
	jeton	w3,TJ_TEXTE1
	adr	x5,ui_font_b
	cbnz	x22,.l1
	jeton	w3,TJ_TEXTE2
	adr	x5,ui_font
.l1:	mov	x4,15
	mov	x6,x24
	bl	ui_text_fit
	mov	x0,96
	adr	x1,c_l2
	madd	x0,x19,x0,x1
	add	x1,x20,14+HM_CARD_IC+12
	mov	x2,HM_CARD_Y+55
	jeton	w3,TJ_TEXTE2
	cbnz	x22,.l2
	jeton	w3,TJ_TEXTE3
.l2:	mov	x4,13
	adr	x5,ui_font
	mov	x6,x24
	bl	ui_text_fit
	cbz	x26,.out
	mov	x0,x20
	mov	x1,HM_CARD_Y
	mov	x2,x21
	mov	x3,HM_CARD_H
	bl	ui_update
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = cellule des recentes
draw_cell:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldv	x1,g_gap
	add	x1,x1,HM_CELL
	mov	x2,HM_M
	madd	x20,x19,x1,x2			; x20 : x
	adr	x0,rw_app
	ldrb	w21,[x0,x19]			; x21 : application
	ldv	x0,g_zone
	cmp	x0,Z_RECENT
	b.ne	.icon
	ldv	x0,g_sel
	cmp	x0,x19
	b.ne	.icon
	jeton	w5,TJ_ACCENT_CLAIR		; choisie
	mov	w6,w5
	mov	x0,x20
	mov	x1,HM_ROW_Y
	mov	x2,HM_CELL
	mov	x3,HM_CELL
	mov	x4,HM_CARD_R
	bl	ui_rbox
.icon:	adr	x0,ap_nom
	add	x0,x0,x21,lsl 5
	add	x1,x20,(HM_CELL-HM_CELL_IC)/2
	mov	x2,HM_ROW_Y+10
	mov	x3,HM_CELL_IC
	bl	ui_icone
	mov	x0,x21				; le nom, centre
	bl	app_label
	mov	x22,x0
	mov	x1,14
	adr	x2,ui_font
	bl	ui_text_width
	mov	x2,HM_CELL-8
	cmp	x0,x2
	csel	x0,x2,x0,hi
	mov	x1,HM_CELL
	sub	x1,x1,x0
	add	x1,x20,x1,lsr 1
	mov	x0,x22
	mov	x2,HM_ROW_Y+10+HM_CELL_IC+22
	jeton	w3,TJ_TEXTE1
	mov	x4,14
	adr	x5,ui_font
	mov	x6,HM_CELL-8
	bl	ui_text_fit
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la bande « Ouvertes »
draw_band:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	ldv	x19,g_band
	jeton	w4,TJ_SURFACE
	mov	x0,0
	mov	x1,x19
	ldv	x2,g_w
	mov	x3,HM_BAND_H
	bl	ui_fill
	jeton	w4,TJ_LIGNE
	mov	x0,0
	mov	x1,x19
	ldv	x2,g_w
	mov	x3,1
	bl	ui_fill
	adr	x0,s_ouvertes
	bl	tr_x0
	mov	x20,x0
	mov	x1,HM_M
	add	x2,x19,HM_BAND_H/2+5
	bl	sec_title
	mov	x0,x20
	mov	x1,HM_SEC_PX
	adr	x2,ui_font_b
	bl	ui_text_width
	add	x20,x0,HM_M+16			; x20 : x de la pastille
	stv	xzr,op_vis
	ldv	x0,op_n
	cbnz	x0,.pills
	adr	x0,s_none_open
	bl	tr_x0
	mov	x1,x20
	add	x2,x19,HM_BAND_H/2+5
	jeton	w3,TJ_TEXTE3
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	b	.out
.pills:	mov	x21,0
.p:	ldv	x0,op_n
	cmp	x21,x0
	b.hs	.out
	mov	x0,x21
	bl	op_label
	mov	x22,x0				; x22 : nom
	mov	x1,14
	adr	x2,ui_font
	bl	ui_text_width
	cmp	x0,HM_PILL_TXT
	mov	x1,HM_PILL_TXT
	csel	x23,x1,x0,hi			; x23 : largeur du nom
	add	x24,x23,8+HM_PILL_IC+8+4+HM_PILL_X+6	; x24 : largeur
	add	x0,x20,x24
	ldv	x1,g_w
	sub	x1,x1,HM_M
	cmp	x0,x1
	b.hi	.out				; (plus de place)
	adr	x0,op_px
	str	x20,[x0,x21,lsl 3]
	adr	x0,op_pw
	str	x24,[x0,x21,lsl 3]
	add	x0,x21,1
	stv	x0,op_vis
	jeton	w25,TJ_CREUX			; fond : choisie, sinon creux
	ldv	x0,g_zone
	cmp	x0,Z_OPEN
	b.ne	.ring
	ldv	x0,g_sel
	cmp	x0,x21
	b.ne	.ring
	jeton	w25,TJ_ACCENT_CLAIR
.ring:	add	x26,x19,(HM_BAND_H-HM_PILL_H)/2	; x26 : y
	adr	x0,op_pid			; l'app devant : anneau ACCENT
	ldr	x0,[x0,x21,lsl 3]
	ldv	x1,op_front
	cmp	x0,x1
	b.ne	.plain
	jeton	w5,TJ_ACCENT
	mov	w6,w5
	mov	x0,x20
	mov	x1,x26
	mov	x2,x24
	mov	x3,HM_PILL_H
	mov	x4,HM_CARD_R+2
	bl	ui_rbox
	mov	w5,w25
	mov	w6,w25
	add	x0,x20,2
	add	x1,x26,2
	sub	x2,x24,4
	mov	x3,HM_PILL_H-4
	mov	x4,HM_CARD_R
	bl	ui_rbox
	b	.inner
.plain:	mov	w5,w25
	mov	w6,w25
	mov	x0,x20
	mov	x1,x26
	mov	x2,x24
	mov	x3,HM_PILL_H
	mov	x4,HM_CARD_R+2
	bl	ui_rbox
.inner:	adr	x0,op_app			; icone (sans app : le titre en initiale)
	ldr	x1,[x0,x21,lsl 3]
	adr	x0,ap_nom
	add	x0,x0,x1,lsl 5
	tbz	x1,63,.ic
	mov	x0,x22
.ic:	add	x1,x20,8
	add	x2,x26,(HM_PILL_H-HM_PILL_IC)/2
	mov	x3,HM_PILL_IC
	bl	ui_icone
	mov	x0,x22
	add	x1,x20,8+HM_PILL_IC+8
	add	x2,x26,HM_PILL_H/2+5
	jeton	w3,TJ_TEXTE1
	mov	x4,14
	adr	x5,ui_font
	mov	x6,x23
	bl	ui_text_fit
	adr	x0,s_g_cross			; la croix
	add	x1,x20,x24
	sub	x1,x1,6+HM_PILL_X-7
	add	x2,x26,(HM_PILL_H-14)/2
	mov	x3,14
	jeton	w4,TJ_TEXTE2
	bl	ui_glyph
	add	x20,x20,x24
	add	x20,x20,8
	add	x21,x21,1
	b	.p
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les resultats de la recherche
draw_results:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldv	x0,res_n
	cbnz	x0,.rows
	adr	x0,s_noresult
	bl	tr_x0
	mov	x1,HM_M+12
	mov	x2,HM_RES_Y+28
	jeton	w3,TJ_TEXTE2
	mov	x4,15
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	b	.out
.rows:	bl	res_rows
	mov	x22,x0				; x22 : lignes montrees
	ldv	x19,res_top
	mov	x20,0				; x20 : ligne
.r:	cmp	x20,x22
	b.hs	.out
	ldv	x0,res_n
	cmp	x19,x0
	b.hs	.out
	mov	x0,HM_RES_H
	mov	x1,HM_RES_Y
	madd	x21,x20,x0,x1			; x21 : y
	ldv	x0,res_sel
	cmp	x0,x19
	b.ne	.ic
	jeton	w5,TJ_ACCENT_CLAIR
	mov	w6,w5
	mov	x0,HM_M
	mov	x1,x21
	ldv	x2,g_w
	sub	x2,x2,2*HM_M
	mov	x3,HM_RES_H-4
	mov	x4,HM_CARD_R
	bl	ui_rbox
.ic:	adr	x0,res_icon
	add	x0,x0,x19,lsl 5
	mov	x1,HM_M+10
	add	x2,x21,(HM_RES_H-4-HM_PILL_IC)/2
	mov	x3,HM_PILL_IC
	bl	ui_icone
	adr	x0,res_kind			; le genre, a droite
	ldrb	w0,[x0,x19]
	adr	x1,s_kinds
	sub	x0,x0,1
	add	x0,x1,x0,lsl 4
	bl	tr_x0
	mov	x23,x0
	mov	x1,13
	adr	x2,ui_font
	bl	ui_text_width
	mov	x24,x0
	ldv	x1,g_w
	sub	x1,x1,HM_M+12
	sub	x1,x1,x0
	mov	x0,x23
	add	x2,x21,(HM_RES_H-4)/2+5
	jeton	w3,TJ_TEXTE3
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x0,RES_TXT			; le nom
	adr	x1,res_text
	madd	x0,x19,x0,x1
	mov	x1,HM_M+10+HM_PILL_IC+12
	add	x2,x21,(HM_RES_H-4)/2+5
	jeton	w3,TJ_TEXTE1
	mov	x4,15
	adr	x5,ui_font
	ldv	x6,g_w
	sub	x6,x6,2*HM_M+10+HM_PILL_IC+12+24
	sub	x6,x6,x24
	bl	ui_text_fit
	add	x19,x19,1
	add	x20,x20,1
	b	.r
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------

s_svc		db 'home',0
s_title		db 'Accueil',0
m_ready		db 'home : accueil pr',0xC3,0xAA,'t',10,0
t_home		db 'korin.shell.home',0
t_media		db 'korin.media.now',0
t_control	db 'korin.media.control',0
s_one		db '1',0
s_toggle	db ' toggle',0
s_playing	db 'playing',0
s_paused	db 'paused',0
s_stopped	db 'stopped',0
s_empty		db 0
s_hint		db 'Rechercher une app, un r',0xC3,0xA9,'glage, un fichier',0
s_aujourdhui	db 'Aujourd',0x27,'hui',0
s_recentes	db 'R',0xC3,0xA9,'centes',0
s_all		db 'Toutes les apps ',0xE2,0x80,0xBA,0
s_ouvertes	db 'Ouvertes',0
s_none_open	db 'Aucune app ouverte',0
s_noresult	db 'Aucun r',0xC3,0xA9,'sultat',0
s_e_agenda	db 'Rien aujourd',0x27,'hui',0
s_n_agenda	db 'Agenda',0
s_e_weather	db 'M',0xC3,0xA9,'t',0xC3,0xA9,'o indisponible',0
s_n_weather	db 'PocketQI',0
s_e_music	db 'Aucune musique',0
s_n_music	db 'Musique',0
s_today		db 'Aujourd',0x27,'hui',0
s_tomorrow	db 'Demain',0
s_now		db 'Maintenant',0
s_dans		db 'dans',0
s_minute	db 'minute',0
s_minutes	db 'minutes',0
s_h		db ' h ',0
s_en_pause	db 'en pause',0
s_dot		db ' ',0xC2,0xB7,' ',0
s_slash		db ' / ',0
s_ic_cal	db 'calendar',0
s_ic_qi		db 'pocketqi',0
s_ic_music	db 'music',0
s_ic_control	db 'control',0
s_ic_editor	db 'editor',0
s_media		db 'media',0
s_homedir	db 'SD0:/home',0
s_g_play	db 'glyph-tb-play',0
s_g_cross	db 'glyph-tb-cross',0
s_agenda	db 'SD0:/apps/calendar.app/agenda.txt',0
s_weather	db 'SD0:/home/pocketqi/weather.txt',0
p_calendar	db 'SD0:/apps/calendar.app/main',0
p_control	db 'SD0:/apps/control.app/main',0
k_temp		db 'temp',0
k_sky		db 'sky',0
k_city		db 'city',0
k_forecast	db 'forecast',0
k_time		db 'time',0
		align 4
s_kinds		db 'App',0,0,0,0,0,0,0,0,0,0,0,0,0
		db 'R',0xC3,0xA9,'glage',0,0,0,0,0,0,0,0
		db 'Fichier',0,0,0,0,0,0,0,0,0
s_wdays		db 'lundi',0,0,0,0,0,0,0,0,0,0,0
		db 'mardi',0,0,0,0,0,0,0,0,0,0,0
		db 'mercredi',0,0,0,0,0,0,0,0
		db 'jeudi',0,0,0,0,0,0,0,0,0,0,0
		db 'vendredi',0,0,0,0,0,0,0,0
		db 'samedi',0,0,0,0,0,0,0,0,0,0
		db 'dimanche',0,0,0,0,0,0,0,0
s_months	db 'janvier',0,0,0,0,0,0,0,0,0
		db 'f',0xC3,0xA9,'vrier',0,0,0,0,0,0,0,0
		db 'mars',0,0,0,0,0,0,0,0,0,0,0,0
		db 'avril',0,0,0,0,0,0,0,0,0,0,0
		db 'mai',0,0,0,0,0,0,0,0,0,0,0,0,0
		db 'juin',0,0,0,0,0,0,0,0,0,0,0,0
		db 'juillet',0,0,0,0,0,0,0,0,0
		db 'ao',0xC3,0xBB,'t',0,0,0,0,0,0,0,0,0,0,0
		db 'septembre',0,0,0,0,0,0,0
		db 'octobre',0,0,0,0,0,0,0,0,0
		db 'novembre',0,0,0,0,0,0,0,0
		db 'd',0xC3,0xA9,'cembre',0,0,0,0,0,0,0,0
s_wd_short	db 'Lun',0,0,0,0,0,'Mar',0,0,0,0,0,'Mer',0,0,0,0,0,'Jeu',0,0,0,0,0
		db 'Ven',0,0,0,0,0,'Sam',0,0,0,0,0,'Dim',0,0,0,0,0
s_mo_short	db 'janv.',0,0,0,'f',0xC3,0xA9,'vr.',0,0,'mars',0,0,0,0,'avr.',0,0,0,0
		db 'mai',0,0,0,0,0,'juin',0,0,0,0,'juil.',0,0,0,'ao',0xC3,0xBB,'t',0,0,0
		db 'sept.',0,0,0,'oct.',0,0,0,0,'nov.',0,0,0,0,'d',0xC3,0xA9,'c.',0,0,0
; les applets de Reglages (src/apps/control : leur argument) : argument,
; libelle, mots cherches aussi (sans accents)
settings	db 'theme',0,'Th',0xC3,0xA8,'me',0,'theme couleurs accent givre onyx sombre clair fond ecran apparence',0
		db 'clavier',0,'R',0xC3,0xA9,'gion et langue',0,'clavier langue region azerty qwerty heure fuseau date',0
		db 'ecran',0,0xC3,0x89,'cran',0,'ecran resolution affichage taille moniteur poche bureau',0
		db 'panelconf',0,'Panel',0,'panel barre raccourcis dock espaces bureaux',0
		db 'wifi',0,'Wi-Fi',0,'wifi reseau internet sans fil connexion',0
		db 'remote',0,'Acc',0xC3,0xA8,'s distant',0,'telnet ftp rdp distance distant mot de passe',0
		db 'sound',0,'Son',0,'son volume audio haut-parleur sortie casque',0
		db 'padconf',0,'Manette',0,'manette joystick jeu gamepad boutons',0
		db 'preload',0,'Pr',0xC3,0xA9,'charg',0xC3,0xA9,'s',0,'prechargement demarrage memoire',0
		db 'dotnet',0,'.NET',0,'dotnet net csharp',0
		db 'pkgman',0,'Logiciels',0,'logiciels paquets mises a jour installer depot',0
		db 'printconf',0,'Imprimantes',0,'imprimante impression imprimer',0
		db 0
		align 4

; (fs_add) x0 = texte, x1 = prefixe -> x0 = la suite du texte (0 : autre prefixe)
str_prefix:
.c:	ldrb	w2,[x1],1
	cbz	w2,.yes
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	.c
	mov	x0,0
.yes:	ret

include '../../inc/tr.inc'

segment readable writeable
kit_data
ui_data
applis_data
dockconf_data
panelconf_data
launcher_data
recent_data
apprun_data
		align 8
AG_TITLE	= 152
scr		rb 32
g_win		dd 0
g_box		dd 0
g_w		dd 0
g_h		dd 0
g_top		dd 0				; reserves de l'ecran au placement
g_bot		dd 0
g_bx		dd 0				; la recherche : x, largeur
g_bw		dd 0
g_cw		dd 0				; largeur d'une carte
g_gap		dd 0				; entre deux cellules
g_band		dd 0				; haut de la bande
g_all_x		dd 0				; « Toutes les apps »
g_all_w		dd 0
g_zone		dd Z_RECENT			; le clavier : zone, element
g_sel		dd 0
g_minute	dd 0
g_sub_home	dd 0
g_sub_media	dd 0
g_prev		dd 0				; l'app d'avant l'accueil (sa fenetre)
g_front_win	dd 0				; (win_scan)
g_front_idx	dd -1
g_my_idx	dd -1
g_msg		rb WS_MSG
ag_ok		dd 0
ag_key		dd 0
ag_min		dd -1
ag_today	dd 0
ag_title	rb AG_TITLE
wx_ok		dd 0
wx_time		dd 0
wx_temp		rb 16
wx_sky		rb 48
wx_city		rb 48
wx_fc		rb 64
		align 8
md_state	dd 0
md_pos		dd 0
md_dur		dd 0
md_t		dd 0
md_app		rb AP_NOM
md_title	rb AG_TITLE
md_raw		rb BUS_VALUE_MAX + 8
		align 8
c_icon		rd 3				; les cartes : icone, une donnee,
c_full		rd 3
c_l1		rb 3 * 96			; deux lignes
c_l2		rb 3 * 96
		align 8
rw_n		dd 0
rw_app		rb 8
		align 8
op_n		dd 0				; Ouvertes
op_vis		dd 0				; (pastilles dessinees)
op_front	dd 0				; pid de l'app devant
op_pid		rd OP_MAX
op_win		rd OP_MAX
op_app		rd OP_MAX
op_px		rd OP_MAX
op_pw		rd OP_MAX
op_title	rb OP_MAX * 32
nw_n		dd 0				; (win_scan)
nw_pid		rd OP_MAX
nw_win		rd OP_MAX
nw_title	rb OP_MAX * 32
wdet		rb WE_PER_MSG * WE_SIZE
pinfo		rb 64
		align 8
g_q		rb LA_NORM			; recherche, normalisee
res_n		dd 0
res_sel		dd 0
res_top		dd 0
res_ref		rd RES_MAX
res_kind	rb RES_MAX
res_icon	rb RES_MAX * AP_NOM
res_text	rb RES_MAX * RES_TXT
res_path	rb RES_MAX * RES_PATH
fs_budget	dd 0
fs_found	dd 0
fs_path		rb 512
fs_app		rb RES_PATH
tbuf		rb 320
t_clock		rb 16
t_date		rb 64
		align 8
fbuf		rb FBUF
