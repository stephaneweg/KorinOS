; quick - les reglages rapides et les notifications du mode poche
; (docs/MODE-POCHE.md, docs/DESIGN-ETUDES-ONYX.md 4) : un volet de 340 px
; sous la barre, a droite, du style des volets du son et du Wi-Fi (MENU,
; rayon 10, ombre). De haut en bas : la date et l'heure ; quatre tuiles
; (Wi-Fi, Ne pas deranger, Son, Repos) - toucher le rond bascule, le reste de
; la tuile ouvre le detail dans le meme volet (la liste du Wi-Fi comme
; wifimenu, la sortie du son comme soundmenu) ; le curseur du volume ;
; l'historique de notifyd (une notification touchee ouvre son app ; glissee a
; droite, son x ou Suppr l'efface ; « Tout effacer ») ; « Reglages » et
; « Alimentation... » (SD0:/sys/shutdown).
;
; Lance par la session poche (SD0:/etc/session.poche) : il reste la, volet
; ferme, et l'ouvre ou le ferme a chaque korin.shell.quick (Super+N, l'heure de
; la barre). Un second « quick » fait de meme (service « quick », op 1) ;
; « quick open » ouvre le volet des le depart. Il se ferme sur Echap, quand
; une autre fenetre prend le clavier, ou apres une action qui ouvre autre chose.
; Clavier : fleches, Espace bascule, Entree ouvre le detail, Suppr efface une
; notification, Echap ferme (ou revient du detail).
;
; Bus : korin.notify.dnd (Ne pas deranger, 1/0, garde ; notifyd ne montre plus
; les bulles), korin.shell.sleep (Repos) ; suit korin.notify.unread (notifyd)
; et korin.net.wifi. Le volet ouvert, les notifications sont marquees lues.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/netkit/netkit.inc'
include '../../inc/netd.inc'
include '../../inc/sound.inc'
include '../../kits/buskit/buskit.inc'

QW		= 340			; largeur du volet
QM		= 6			; marge sous la barre et a droite (visible)
OMBRE		= 4			; retrait de ui_ombre
RAYON		= 10
TILE_Y		= 48
TILE_H		= 60
TILE_W		= (QW - 32 - 8) / 2
TILE_X1		= 16 + TILE_W + 8
TILE_Y1		= TILE_Y + TILE_H + 8
ROND		= 32
SLID_Y		= TILE_Y1 + TILE_H + 14
SL_X0		= 42
SL_W0		= QW - SL_X0 - 64
SEP_Y		= SLID_Y + 28
NOTE_Y		= SEP_Y + 24		; ligne de base de « Notifications »
CARDS_Y		= NOTE_Y + 12
CARD_H		= 52
CARD_PAS	= CARD_H + 6
EMPTY_H		= 36
FOOT_H		= 54
BTN_H		= 30
BTN_SW		= 130			; Reglages
BTN_PW		= 150			; Alimentation...
DET_Y		= 52			; (detail) haut du contenu
DFOOT		= 34
NL_MAX		= 32			; notifications gardees ici
SWIPE		= 80			; glisse a droite : effacee
QK_SPACE	= 0x2C

; actions des zones (wl_hit_add ; 1 et 2 : la liste du Wi-Fi)
A_ROND		= 3			; argument : tuile
A_TILE		= 4
A_SLIDER	= 5
A_CLEAR		= 6
A_NOTE		= 7			; argument : numero dans la liste
A_NOTE_X	= 8
A_SETTINGS	= 9
A_POWER		= 10
A_BACK		= 11
A_WPARAM	= 12
A_MIXER		= 13
A_SOUT		= 14
A_MUTE		= 15

; touches du clavier (kf) : 0..3 tuiles, 4 curseur, 5 Tout effacer, puis les
; notifications, Reglages, Alimentation
K_SLIDER	= 4
K_CLEAR		= 5
K_NOTES		= 6

V_MAIN		= 0
V_WIFI		= 1
V_SOUND		= 2

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so
include '../../inc/sounddraw.inc'
include '../../inc/soundlist.inc'
include '../../inc/wifidraw.inc'
include '../../inc/wifilist.inc'
include '../../inc/applist.inc'
include '../../inc/procfind.inc'

start:
	mov	x19,x0				; arguments
	bl	kits_open
	adr	x0,s_nom			; deja la : il bascule, on s'en va
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbz	x0,63,.seul
	adr	x0,s_nom
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x1,msg
	mov	x2,1				; op 1 : basculer
	str	x2,[x1]
	sys	SYS_SVC_CALL
	b	quit
.seul:	adr	x1,canal
	str	x0,[x1]
	adr	x0,ui_win
	bl	ui_init
	bl	applis_load			; (les libelles des apps)
	adr	x0,wl_w
	mov	x1,QW
	str	x1,[x0]
	mov	x0,0				; le Wi-Fi : etat, thread (sans balayage)
	bl	wl_start
	bl	ak_state			; le son (0 : pas de service sound)
	adr	x1,sl_st
	str	x0,[x1]
	cbz	x0,.bus
	bl	sl_collect
.bus:	adr	x0,s_t_quick			; korin.shell.quick : ouvrir, fermer
	mov	x1,0
	bl	bus_subscribe
	adr	x1,sub_q
	str	x0,[x1]
	adr	x0,s_t_unread			; korin.notify.unread : la liste a change
	mov	x1,0
	bl	bus_subscribe
	adr	x1,sub_u
	str	x0,[x1]
	cbz	x19,.loop			; « quick open » : ouvert tout de suite
	ldrb	w0,[x19]
	cbz	w0,.loop
	bl	ouvrir
.loop:	adr	x0,canal			; un second quick : basculer
	ldr	x0,[x0]
	adr	x1,msg
	mov	x2,0
	sys	SYS_SVC_NEXT
	cmp	x0,0
	b.le	.ev
	adr	x1,msg
	str	xzr,[x1]
	sys	SYS_SVC_REPLY
	bl	basculer
	b	.loop
.ev:	mov	x0,1000				; (ferme : on attend le bus)
	adr	x1,ouvert
	ldr	x1,[x1]
	cbz	x1,.ev1
	mov	x0,200
.ev1:	bl	ui_event
	cmp	x0,WEV_BUS
	b.eq	.bus_ev
	adr	x1,ouvert
	ldr	x1,[x1]
	cbz	x1,.loop			; (le reste : volet ouvert seulement)
	cmp	x0,WEV_CLOSE
	b.eq	.fermer
	cmp	x0,WEV_FOCUS
	b.eq	.focus
	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cmp	x0,WEV_WHEEL
	b.eq	.wheel
	cmp	x0,WEV_THEME
	b.eq	.redraw
	cmp	x0,WEV_SCREEN
	b.eq	.screen
	cbnz	x0,.loop
	bl	tick				; (rien : l'etat a-t-il change ?)
	cbz	x0,.loop
.redraw:
	bl	redraw
	b	.loop
.screen:
	bl	fermer
	bl	ouvrir
	b	.loop
.fermer:
	bl	fermer
	b	.loop
.bus_ev:
	adr	x9,ui_ev
	ldr	w19,[x9,BEV_SUB]
	adr	x0,sub_q
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.bu
	bl	basculer
	b	.loop
.bu:	adr	x0,sub_u
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.bw
	adr	x0,ouvert			; ouvert : relue, lue, redessinee
	ldr	x0,[x0]
	cbz	x0,.loop
	bl	charger
	bl	redraw
	b	.loop
.bw:	mov	x0,x19				; korin.net.wifi
	bl	wl_bus
	cbz	x0,.loop
	adr	x0,ouvert
	ldr	x0,[x0]
	cbz	x0,.loop
	bl	tick
	cbz	x0,.loop
	bl	redraw
	b	.loop
.focus:	adr	x9,ui_ev			; une autre fenetre a le clavier
	ldr	w0,[x9,EV_KIND]
	cbnz	w0,.loop
	ldr	w0,[x9,EV_WIN]			; (pas une ancienne fenetre du volet)
	adr	x1,cur_win
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.loop
	bl	fermer
	sys	SYS_TIME_MS
	adr	x1,t_ferme
	str	x0,[x1]
	b	.loop
.key:	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.loop
	bl	touche
	cbz	x0,.loop
	cmp	x0,3
	b.eq	.fermer
	bl	redraw
	b	.loop
.wheel:	adr	x0,ui_ev
	ldrsw	x0,[x0,EV_Y]
	neg	x0,x0
	bl	defiler
	cbz	x0,.loop
	bl	redraw
	b	.loop
.mouse:	bl	souris
	cbz	x0,.loop
	cmp	x0,3
	b.eq	.fermer
	bl	redraw
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Le volet : ouvrir, fermer, sa taille
; ---------------------------------------------------------------------------

; korin.shell.quick, ou un second quick : ouvert <-> ferme. (Un toucher sur
; l'heure de la barre ferme d'abord le volet - il perd le clavier - puis
; publie : ce second signal, juste apres, ne le rouvre pas.)
basculer:
	prologue
	adr	x0,ouvert
	ldr	x0,[x0]
	cbz	x0,.ouvrir
	bl	fermer
	epilogue
.ouvrir:
	sys	SYS_TIME_MS
	adr	x1,t_ferme
	ldr	x1,[x1]
	sub	x0,x0,x1
	cmp	x0,400
	b.lo	.out
	bl	ouvrir
.out:	epilogue

ouvrir:
	prologue
	adr	x0,vue
	str	xzr,[x0]
	adr	x0,kf
	mov	x1,-1
	str	x1,[x0]
	adr	x0,nf_first
	str	xzr,[x0]
	adr	x0,press_note
	mov	x1,-1
	str	x1,[x0]
	adr	x0,wl_selrow
	str	x1,[x0]
	adr	x0,ouvert
	mov	x1,1
	str	x1,[x0]
	bl	wl_status			; l'etat de tout
	bl	lire_dnd
	adr	x0,sl_st
	ldr	x0,[x0]
	cbz	x0,.ch
	bl	sl_collect
.ch:	bl	charger
	bl	fenetre
	tbnz	x0,63,.ko
	bl	draw
	epilogue
.ko:	adr	x0,ouvert
	str	xzr,[x0]
	epilogue

fermer:
	prologue
	adr	x0,ouvert
	ldr	x1,[x0]
	cbz	x1,.out
	str	xzr,[x0]
	adr	x0,cur_win
	str	xzr,[x0]
	adr	x0,dragging
	str	xzr,[x0]
	bl	ui_close
.out:	epilogue

; -> x0 = hauteur de la fenetre (contenu, ombre comprise), selon la vue
hauteur:
	prologue
	adr	x0,vue
	ldr	x0,[x0]
	cmp	x0,V_WIFI
	b.eq	.wifi
	cmp	x0,V_SOUND
	b.eq	.son
	bl	nvis				; la vue principale
	cbz	x0,.vide
	mov	x1,CARD_PAS
	mul	x0,x0,x1
	sub	x0,x0,6
	b	.cartes
.vide:	mov	x0,EMPTY_H
.cartes:
	add	x0,x0,CARDS_Y+4+FOOT_H
	epilogue
.wifi:	bl	wl_height
	add	x0,x0,DET_Y+4+DFOOT+8
	epilogue
.son:	adr	x0,sl_nshow
	ldr	x0,[x0]
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lo
	mov	x1,SL_ROWH
	mul	x0,x0,x1
	add	x0,x0,DET_Y+92+48+8
	epilogue

; -> x0 = hauteur maximale du volet (l'ecran sous la barre)
hmax:
	adr	x0,scr
	ldr	x1,[x0,8]			; hauteur
	ldr	x2,[x0,24]			; reserve en haut
	sub	x0,x1,x2
	sub	x0,x0,2*(QM-OMBRE)
	ret

; -> x0 = notifications montrees (a partir de nf_first, autant que la
; hauteur permet)
nvis:
	prologue
	bl	hmax
	sub	x0,x0,CARDS_Y+4+FOOT_H-6
	mov	x1,CARD_PAS
	sdiv	x0,x0,x1
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lt
	adr	x1,n_notes
	ldr	x1,[x1]
	adr	x2,nf_first
	ldr	x2,[x2]
	sub	x1,x1,x2
	cmp	x1,0
	csel	x1,xzr,x1,lt
	cmp	x0,x1
	csel	x0,x1,x0,gt
	epilogue

; la fenetre, a la hauteur du contenu -> x0 = poignee (< 0 : echec)
fenetre:
	prologue
	adr	x0,scr
	bl	ui_screen
	bl	hauteur
	mov	x3,x0
	str	x3,[sp,-16]!
	bl	hmax
	ldr	x3,[sp],16
	cmp	x3,x0
	csel	x3,x0,x3,gt
	adr	x0,win_h
	str	x3,[x0]
	adr	x0,scr
	ldr	x1,[x0,24]			; sous la barre, a droite
	add	x1,x1,QM-OMBRE
	ldr	x0,[x0]
	sub	x0,x0,QW+QM-OMBRE
	mov	x2,QW
	adr	x4,s_titre
	bl	tr_x4
	mov	x5,WF_FRAMELESS or WF_TOP or WF_NOLIST or WF_ALPHA
	bl	ui_window_ex
	adr	x1,cur_win
	str	x0,[x1]
	epilogue

; la hauteur a change : nouvelle fenetre ; puis dessin
redraw:
	prologue
	bl	hauteur
	str	x0,[sp,-16]!
	bl	hmax
	ldr	x1,[sp],16
	cmp	x1,x0
	csel	x0,x0,x1,gt
	adr	x1,win_h
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.d
	bl	ui_close
	bl	fenetre
	tbz	x0,63,.d
	bl	fermer
	epilogue
.d:	bl	draw
	epilogue

; (toutes les 200 ms, volet ouvert) -> x0 = 1 : redessiner
tick:
	prologue
	str	x19,[sp,-16]!
	mov	x19,0
	bl	wl_tick
	cmp	x0,2				; connecte depuis le detail : on revient
	b.ne	.t1
	adr	x1,vue
	ldr	x2,[x1]
	cmp	x2,V_WIFI
	b.ne	.t1
	str	xzr,[x1]
.t1:	orr	x19,x19,x0
	adr	x0,sl_st
	ldr	x0,[x0]
	cbz	x0,.h
	bl	sl_changed
	orr	x19,x19,x0
.h:	bl	minute				; l'heure
	adr	x1,minute_vue
	ldr	x2,[x1]
	cmp	x0,x2
	b.eq	.out
	str	x0,[x1]
	mov	x19,1
.out:	cmp	x19,0
	cset	x0,ne
	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  L'etat : notifications, Ne pas deranger, Wi-Fi
; ---------------------------------------------------------------------------

; x0 = op, x1 = a -> x0 = resultat de notifyd (< 0 : absent)
nd_call:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,nd_canal
	ldr	x0,[x0]
	cbnz	x0,.c
	adr	x0,s_notify
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	adr	x1,nd_canal
	str	x0,[x1]
.c:	adr	x1,msg
	stp	x19,x20,[x1,WM_OP]
	str	wzr,[x1,WM_LEN]
	sys	SYS_SVC_CALL
	tbz	x0,63,.ok
	adr	x1,nd_canal			; (notifyd relance : canal a rouvrir)
	str	xzr,[x1]
	b	.out
.ok:	adr	x0,msg
	ldr	x0,[x0]
.out:	ldp	x19,x20,[sp],16
	epilogue

; l'historique de notifyd -> notes, n_notes ; toutes marquees lues
charger:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
	mov	x20,0				; (nombre rendu par notifyd)
.l:	cmp	x19,NL_MAX
	b.hs	.fin
	mov	x0,NOTIFY_OP_LIST
	mov	x1,x19
	bl	nd_call
	cmp	x0,0
	b.le	.fin0
	mov	x20,x0
	cmp	x19,x20
	b.hs	.fin
	adr	x0,notes
	mov	x1,NH_SIZE
	madd	x0,x19,x1,x0
	adr	x1,msg+WM_DATA
	mov	x2,NH_SIZE
	bl	mem_move
	add	x19,x19,1
	b	.l
.fin0:	mov	x19,0
.fin:	adr	x0,n_notes
	str	x19,[x0]
	adr	x0,nf_first			; (dans la liste)
	ldr	x1,[x0]
	cmp	x1,x19
	csel	x1,xzr,x1,hs
	str	x1,[x0]
	cbz	x19,.out
	mov	x0,NOTIFY_OP_READ
	mov	x1,0
	bl	nd_call
.out:	ldp	x19,x20,[sp],16
	epilogue

; dnd <- korin.notify.dnd
lire_dnd:
	prologue
	adr	x1,tmpv
	str	xzr,[x1]
	adr	x0,s_t_dnd
	mov	x2,8
	bl	bus_get
	adr	x1,tmpv
	ldrb	w1,[x1]
	cmp	w1,'1'
	cset	x1,eq
	cmp	x0,0
	csel	x0,x1,xzr,gt
	adr	x1,dnd
	str	x0,[x1]
	epilogue

; x0 = tuile : son rond (bascule) -> x0 = 3 si le volet se ferme
basculer_tuile:
	prologue
	str	x19,[sp,-16]!
	cbz	x0,.wifi
	cmp	x0,1
	b.eq	.dnd
	cmp	x0,2
	b.eq	.son
	adr	x0,s_t_sleep			; Repos : la coquille s'en charge
	adr	x1,s_un
	mov	x2,-1
	mov	x3,0
	bl	bus_publish
	mov	x0,3
	b	.out
.son:	bl	sl_toggle_mute
	b	.zero
.dnd:	adr	x1,dnd
	ldr	x0,[x1]
	eor	x0,x0,1
	str	x0,[x1]
	adr	x1,s_zero
	cbz	x0,.pub
	adr	x1,s_un
.pub:	adr	x0,s_t_dnd
	mov	x2,-1
	mov	x3,BUS_RETAIN
	bl	bus_publish
	b	.zero
.wifi:	adr	x0,wl_wstat			; connecte (ou en cours) : coupe
	ldr	w0,[x0]
	cmp	w0,WIFI_ST_JOINING
	b.lo	.on
	bl	wifi_leave
	adr	x0,wifi_off
	mov	x1,1
	str	x1,[x0]
	bl	wl_status
	b	.zero
.on:	cbz	w0,.zero			; (pas de puce)
	adr	x0,wl_known			; sinon : le premier reseau connu
	mov	x1,16
	bl	wifi_known
	cmp	x0,0
	b.gt	.join
	mov	x0,1				; aucun : le detail, pour en choisir un
	bl	detail
	b	.zero
.join:	adr	x0,wl_jssid
	strb	wzr,[x0]
	adr	x1,wl_known
	bl	str_cat
	adr	x0,wl_jkey
	strb	wzr,[x0]
	mov	x0,2
	bl	wl_lancer
	adr	x0,wifi_off
	str	xzr,[x0]
.zero:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = tuile : le reste de la tuile (le detail, ou comme le rond) -> x0 = 3 :
; fermer
ouvrir_tuile:
	prologue
	cbz	x0,.wifi
	cmp	x0,2
	b.eq	.son
	bl	basculer_tuile
	epilogue
.wifi:	mov	x0,V_WIFI
	bl	detail
	mov	x0,0
	epilogue
.son:	adr	x0,sl_st
	ldr	x0,[x0]
	cbz	x0,.non
	mov	x0,V_SOUND
	bl	detail
.non:	mov	x0,0
	epilogue

; x0 = vue de detail : y va (le Wi-Fi balaye)
detail:
	prologue
	adr	x1,vue
	str	x0,[x1]
	adr	x1,kf
	mov	x2,-1
	str	x2,[x1]
	cmp	x0,V_WIFI
	b.ne	.out
	mov	x0,1
	bl	wl_lancer
.out:	epilogue

; x0 = numero dans la liste : ouvre son app (ramene sa fenetre, ou la
; lance) -> x0 = 3 si une app s'ouvre (le volet se ferme)
ouvrir_note:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x1,notes
	mov	x2,NH_SIZE
	madd	x19,x0,x2,x1
	add	x19,x19,NH_APP
	mov	x0,x19
	bl	app_find			; une app du catalogue ?
	tbnz	x0,63,.non
	mov	x0,x19				; elle tourne : sa premiere fenetre
	mov	x1,0
	bl	proc_find
	mov	x20,x0
	cbz	x20,.lancer
	mov	x21,0
.page:	mov	x0,x21
	adr	x1,wlist
	bl	ui_winlist
	cmp	x0,x21
	b.le	.lancer
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
	b	.oui
.lancer:
	adr	x0,chemin			; SD0:/apps/<nom>.app/main
	strb	wzr,[x0]
	adr	x1,ap_path
	bl	str_cat
	mov	x1,x19
	bl	str_cat
	adr	x1,s_main
	bl	str_cat
	adr	x0,chemin
	mov	x1,0
	sys	SYS_SPAWN
.oui:	mov	x0,3
	b	.out
.non:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero dans la liste : effacee (notifyd, puis la liste relue)
effacer_note:
	prologue
	adr	x1,notes
	mov	x2,NH_SIZE
	madd	x1,x0,x2,x1
	ldr	x1,[x1,NH_ID]
	mov	x0,NOTIFY_OP_DELETE
	bl	nd_call
	bl	charger
	adr	x0,kf				; le clavier reste dans la liste
	ldr	x1,[x0]
	tbnz	x1,63,.out			; (pas d'anneau)
	adr	x2,n_notes
	ldr	x2,[x2]
	add	x2,x2,K_NOTES
	cmp	x1,x2
	b.lo	.out
	cmp	x1,K_NOTES
	b.lo	.out
	sub	x1,x2,1				; (la derniere, ou Tout effacer)
	str	x1,[x0]
.out:	bl	montrer_kf
	epilogue

; x0 = crans (> 0 : vers le bas) -> x0 = 1 si la liste a bouge
defiler:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x1,vue
	ldr	x1,[x1]
	cbnz	x1,.non
	adr	x1,nf_first
	ldr	x19,[x1]
	add	x0,x19,x0
	str	x0,[x1]
	bl	borner
	adr	x1,nf_first
	ldr	x0,[x1]
	cmp	x0,x19
	cset	x0,ne
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; nf_first ramene entre 0 et n_notes - montrees
borner:
	prologue
	str	x19,[sp,-16]!
	bl	hmax
	sub	x0,x0,CARDS_Y+4+FOOT_H-6
	mov	x1,CARD_PAS
	sdiv	x0,x0,x1
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lt
	adr	x1,n_notes
	ldr	x1,[x1]
	subs	x1,x1,x0
	csel	x1,xzr,x1,lt
	adr	x2,nf_first
	ldr	x3,[x2]
	cmp	x3,x1
	csel	x3,x1,x3,gt
	cmp	x3,0
	csel	x3,xzr,x3,lt
	str	x3,[x2]
	ldr	x19,[sp],16
	epilogue

; la notification du clavier dans la partie montree
montrer_kf:
	prologue
	adr	x0,kf
	ldr	x0,[x0]
	subs	x0,x0,K_NOTES
	b.lt	.out
	adr	x1,n_notes
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.out
	adr	x2,nf_first
	ldr	x3,[x2]
	cmp	x0,x3
	b.ge	.bas
	str	x0,[x2]				; au-dessus : premiere
	b	.out
.bas:	str	x0,[sp,-16]!
	bl	nvis
	mov	x1,x0
	ldr	x0,[sp],16
	adr	x2,nf_first
	ldr	x3,[x2]
	add	x4,x3,x1
	cmp	x0,x4
	b.lt	.out
	sub	x3,x0,x1			; au-dessous : derniere
	add	x3,x3,1
	str	x3,[x2]
.out:	bl	borner
	epilogue

; ---------------------------------------------------------------------------
;  Le dessin
; ---------------------------------------------------------------------------

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	bl	wl_nhits_reset
	bl	ui_jetons
	mov	x28,x0				; x28 : les jetons, pour tout le dessin
	adr	x0,ui_w
	ldp	x0,x1,[x0]
	bl	ui_ombre			; fond, ombre, contour
	ldr	w5,[x28,TJ_MENU]
	mov	w6,w5
	mov	x0,OMBRE
	mov	x1,OMBRE
	adr	x2,ui_w
	ldp	x2,x3,[x2]
	sub	x2,x2,2*OMBRE
	sub	x3,x3,2*OMBRE
	mov	x4,RAYON
	bl	ui_rbox
	adr	x0,vue
	ldr	x0,[x0]
	cmp	x0,V_WIFI
	b.eq	.wifi
	cmp	x0,V_SOUND
	b.eq	.son
	bl	draw_main
	b	.fin
.wifi:	bl	draw_wifi
	b	.fin
.son:	bl	draw_son
.fin:	bl	ui_update_all
	ldp	x27,x28,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (x28 = jetons) x0..x3 = rectangle, x4 = rayon : l'anneau du clavier si kf
; = x5 (un fond ACCENT un peu plus grand, sous l'element)
anneau:
	prologue
	adr	x6,kf
	ldr	x6,[x6]
	cmp	x6,x5
	b.ne	.out
	sub	x0,x0,2
	sub	x1,x1,2
	add	x2,x2,4
	add	x3,x3,4
	add	x4,x4,2
	ldr	w5,[x28,TJ_ACCENT]
	mov	w6,w5
	bl	ui_rbox
.out:	epilogue

; (x28) la vue principale
draw_main:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	minute				; la date, l'heure a droite
	adr	x1,minute_vue
	str	x0,[x1]
	bl	textes_date
	adr	x0,datebuf
	mov	x1,20
	mov	x2,34
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,16
	adr	x5,ui_font_b
	mov	x6,QW-110
	bl	ui_text
	adr	x0,heurebuf
	mov	x1,16
	adr	x2,ui_font_b
	bl	ui_text_width
	mov	x1,QW-20
	sub	x1,x1,x0
	adr	x0,heurebuf
	mov	x2,34
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,16
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x19,0				; les tuiles
.t:	mov	x0,x19
	bl	draw_tile
	add	x19,x19,1
	cmp	x19,4
	b.lo	.t
	adr	x0,sl_mainp			; le volume
	ldr	x0,[x0]
	cbz	x0,.sep
	adr	x0,sl_sx
	mov	x1,SL_X0
	mov	x2,SL_W0
	stp	x1,x2,[x0]
	mov	x0,8				; (haut-parleur : muet)
	mov	x1,SLID_Y-4
	mov	x2,30
	mov	x3,24
	mov	x4,A_MUTE
	mov	x5,0
	bl	wl_hit_add
	mov	x0,SL_X0-6
	mov	x1,SLID_Y-4
	mov	x2,SL_W0+12
	mov	x3,24
	mov	x4,A_SLIDER
	mov	x5,0
	bl	wl_hit_add
	adr	x0,kf
	ldr	x0,[x0]
	cmp	x0,K_SLIDER
	b.ne	.sl
	ldr	w5,[x28,TJ_ACCENT_CLAIR]
	mov	w6,w5
	mov	x0,8
	mov	x1,SLID_Y-6
	mov	x2,QW-16
	mov	x3,28
	mov	x4,8
	bl	ui_rbox
.sl:	mov	x0,SLID_Y
	bl	sl_draw_slider
.sep:	mov	x0,12
	mov	x1,SEP_Y
	mov	x2,QW-24
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	adr	x0,s_notifs			; Notifications, Tout effacer
	bl	tr_x0
	mov	x1,20
	mov	x2,NOTE_Y
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x19,x0
	adr	x0,n_notes
	ldr	x0,[x0]
	cbz	x0,.vide
	adr	x1,numbuf			; (leur nombre)
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,numbuf
	add	x1,x19,8
	mov	x2,NOTE_Y
	ldr	w3,[x28,TJ_TEXTE3]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	adr	x0,s_effacer
	bl	tr_x0
	mov	x1,13
	adr	x2,ui_font
	bl	ui_text_width
	mov	x20,x0
	mov	x0,QW-20
	sub	x0,x0,x20
	sub	x0,x0,6
	mov	x1,NOTE_Y-18
	add	x2,x20,12
	mov	x3,26
	mov	x4,A_CLEAR
	mov	x5,0
	bl	wl_hit_add
	adr	x0,kf
	ldr	x0,[x0]
	cmp	x0,K_CLEAR
	b.ne	.ef
	ldr	w5,[x28,TJ_ACCENT_CLAIR]
	mov	w6,w5
	mov	x0,QW-20
	sub	x0,x0,x20
	sub	x0,x0,6
	mov	x1,NOTE_Y-18
	add	x2,x20,12
	mov	x3,26
	mov	x4,6
	bl	ui_rbox
.ef:	adr	x0,s_effacer
	bl	tr_x0
	mov	x1,QW-20
	sub	x1,x1,x20
	mov	x2,NOTE_Y
	ldr	w3,[x28,TJ_ACCENT]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	bl	nvis				; les cartes
	mov	x21,x0
	mov	x20,0
	mov	x22,CARDS_Y
.c:	cmp	x20,x21
	b.hs	.foot
	adr	x0,nf_first
	ldr	x0,[x0]
	add	x0,x0,x20
	mov	x1,x22
	bl	draw_card
	add	x20,x20,1
	add	x22,x22,CARD_PAS
	b	.c
.vide:	adr	x0,s_aucune
	bl	tr_x0
	mov	x1,20
	mov	x2,CARDS_Y+22
	ldr	w3,[x28,TJ_TEXTE3]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,QW-40
	bl	ui_text
.foot:	adr	x0,ui_w				; Reglages, Alimentation...
	ldr	x23,[x0,8]
	sub	x23,x23,FOOT_H
	mov	x0,12
	mov	x1,x23
	mov	x2,QW-24
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	add	x23,x23,10
	adr	x0,n_notes
	ldr	x24,[x0]
	add	x24,x24,K_NOTES			; touche de Reglages
	mov	x0,16
	mov	x1,x23
	mov	x2,BTN_SW
	mov	x3,BTN_H
	mov	x4,8
	mov	x5,x24
	bl	anneau
	mov	x0,16
	mov	x1,x23
	mov	x2,BTN_SW
	mov	x3,BTN_H
	mov	x4,A_SETTINGS
	mov	x5,0
	bl	wl_hit_add
	mov	x0,16
	mov	x1,x23
	mov	x2,BTN_SW
	mov	x3,BTN_H
	adr	x4,s_reglages
	bl	tr_x4
	mov	x5,0
	bl	ui_button
	mov	x0,QW-16-BTN_PW
	mov	x1,x23
	mov	x2,BTN_PW
	mov	x3,BTN_H
	mov	x4,8
	add	x5,x24,1
	bl	anneau
	mov	x0,QW-16-BTN_PW
	mov	x1,x23
	mov	x2,BTN_PW
	mov	x3,BTN_H
	mov	x4,A_POWER
	mov	x5,0
	bl	wl_hit_add
	mov	x0,QW-16-BTN_PW
	mov	x1,x23
	mov	x2,BTN_PW
	mov	x3,BTN_H
	adr	x4,s_alim
	bl	tr_x4
	mov	x5,0
	bl	ui_button
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (x28) x0 = tuile 0..3 : fond (actif : ACCENT_CLAIR), rond (ACCENT), nom,
; etat
draw_tile:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,16				; x, y
	tst	x19,1
	b.eq	.x
	mov	x20,TILE_X1
.x:	mov	x21,TILE_Y
	tst	x19,2
	b.eq	.y
	mov	x21,TILE_Y1
.y:	mov	x0,x19
	bl	tile_state			; -> x0 actif, x1 nom, x2 etat
	mov	x22,x0
	mov	x23,x1
	mov	x24,x2
	mov	x0,x20
	mov	x1,x21
	mov	x2,TILE_W
	mov	x3,TILE_H
	mov	x4,RAYON
	mov	x5,x19
	bl	anneau
	cbz	x22,.inact
	ldr	w5,[x28,TJ_ACCENT_CLAIR]
	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,TILE_W
	mov	x3,TILE_H
	mov	x4,RAYON
	bl	ui_rbox
	b	.rond
.inact:	ldr	w5,[x28,TJ_LIGNE]
	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,TILE_W
	mov	x3,TILE_H
	mov	x4,RAYON
	bl	ui_rbox
	ldr	w5,[x28,TJ_ELEVE]
	ldr	w6,[x28,TJ_ELEVE_BAS]
	add	x0,x20,1
	add	x1,x21,1
	mov	x2,TILE_W-2
	mov	x3,TILE_H-2
	mov	x4,RAYON-1
	bl	ui_rbox
.rond:	ldr	w5,[x28,TJ_LIGNE_FORTE]
	cbz	x22,.r1
	ldr	w5,[x28,TJ_ACCENT]
.r1:	mov	w6,w5
	add	x0,x20,10
	add	x1,x21,(TILE_H-ROND)/2
	mov	x2,ROND
	mov	x3,ROND
	mov	x4,ROND/2
	bl	ui_rbox
	mov	x0,x19				; son dessin
	add	x1,x20,10
	add	x2,x21,(TILE_H-ROND)/2
	mov	x3,x22
	bl	glyphe_tuile
	mov	x0,x23				; nom trop long : sur deux lignes, coupe
	mov	x1,14				; au dernier espace, sans l'etat
	adr	x2,ui_font_b
	bl	ui_text_width
	cmp	x0,TILE_W-60
	b.le	.court
	adr	x0,lignebuf2
	mov	x1,x23
	bl	str_cat
	adr	x0,lignebuf2
	mov	x1,0
	mov	x2,x0
.sp:	ldrb	w3,[x2],1
	cbz	w3,.spf
	cmp	w3,' '
	csel	x1,x2,x1,eq			; (apres l'espace)
	b	.sp
.spf:	cbz	x1,.court
	strb	wzr,[x1,-1]
	mov	x23,x0				; premiere ligne, puis la suite a la
	mov	x24,x1				; place de l'etat
	adr	x0,nom2
	str	x1,[x0]
.court:	ldr	w3,[x28,TJ_TEXTE1]		; nom
	cbz	x22,.n1
	ldr	w3,[x28,TJ_ACCENT_FONCE]
.n1:	mov	x0,x23
	add	x1,x20,52
	add	x2,x21,26
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,TILE_W-60
	bl	ui_text
	mov	x0,x24				; etat (ou la suite du nom)
	add	x1,x20,52
	add	x2,x21,44
	ldr	w3,[x28,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	adr	x6,nom2
	ldr	x7,[x6]
	str	xzr,[x6]
	cbz	x7,.etat
	add	x2,x21,43
	ldr	w3,[x28,TJ_TEXTE1]
	cbz	x22,.n2
	ldr	w3,[x28,TJ_ACCENT_FONCE]
.n2:	mov	x4,14
	adr	x5,ui_font_b
.etat:	mov	x6,TILE_W-60
	bl	ui_text
	mov	x0,x20				; zones : la tuile, puis le rond
	mov	x1,x21
	mov	x2,TILE_W
	mov	x3,TILE_H
	mov	x4,A_TILE
	mov	x5,x19
	bl	wl_hit_add
	add	x0,x20,4
	add	x1,x21,4
	mov	x2,ROND+12
	mov	x3,TILE_H-8
	mov	x4,A_ROND
	mov	x5,x19
	bl	wl_hit_add
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tuile -> x0 = 1 active, x1 = nom, x2 = etat (traduits)
tile_state:
	prologue
	stp	x19,x20,[sp,-16]!
	cbz	x0,.wifi
	cmp	x0,1
	b.eq	.dnd
	cmp	x0,2
	b.eq	.son
	mov	x19,0				; Repos
	adr	x0,s_repos
	bl	tr_x0
	mov	x20,x0
	adr	x2,s_super_l
	b	.out
.dnd:	adr	x0,dnd
	ldr	x19,[x0]
	adr	x0,s_dnd
	bl	tr_x0
	mov	x20,x0
	adr	x2,s_desactive
	cbz	x19,.out
	adr	x2,s_active
	b	.out
.son:	adr	x0,s_son
	bl	tr_x0
	mov	x20,x0
	mov	x19,0
	adr	x2,s_aucune_sortie
	adr	x0,sl_mainp
	ldr	x3,[x0]
	cbz	x3,.out
	adr	x2,s_muet
	ldr	w0,[x3,SO_MUTE]
	cbnz	w0,.out
	mov	x19,1				; « 70 % · HDMI »
	ldr	w0,[x3,SO_VOLUME]
	adr	x1,etatbuf
	bl	fmt_dec
	adr	x1,s_pct
	bl	str_cat				; (str_cat copie en x0 : a la suite)
	adr	x1,sl_mainp
	ldr	x1,[x1]
	add	x1,x1,SO_NAME
	bl	str_cat
	adr	x0,etatbuf
	mov	x2,x0
	mov	x0,x19
	mov	x1,x20
	ldp	x19,x20,[sp],16
	epilogue
.wifi:	adr	x0,s_wifi
	mov	x20,x0
	adr	x0,wl_wstat
	ldr	w1,[x0]
	cmp	w1,WIFI_ST_JOINING
	cset	x19,hs
	adr	x2,s_indispo
	cbz	w1,.out
	cmp	w1,WIFI_ST_CONNECTED
	b.ne	.wj
	add	x0,x0,16			; le reseau
	mov	x1,x20
	mov	x2,x0
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue
.wj:	adr	x2,wl_s_connexion
	cmp	w1,WIFI_ST_JOINING
	b.eq	.out
	adr	x0,wl_job
	ldr	x0,[x0]
	cmp	x0,2
	b.eq	.out
	adr	x2,s_coupe
	adr	x0,wifi_off
	ldr	x0,[x0]
	cbnz	x0,.out
	adr	x2,s_nonco
.out:	bl	tr_x2
	mov	x0,x19
	mov	x1,x20
	ldp	x19,x20,[sp],16
	epilogue

; (x28) x0 = tuile, x1, x2 = coin du rond, x3 = actif : le dessin dans le rond
glyphe_tuile:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldr	w22,[x28,TJ_TEXTE1]		; encre
	cbz	x3,.c
	ldr	w22,[x28,TJ_ACCENT_INK]
.c:	cbz	x19,.wifi
	cmp	x19,1
	b.eq	.lune
	cmp	x19,2
	b.eq	.son
	adr	x0,s_g_power			; Repos
	add	x1,x20,8
	add	x2,x21,8
	mov	x3,16
	mov	w4,w22
	bl	ui_glyph
	b	.out
.son:	add	x0,x20,8
	add	x1,x21,9
	bl	sl_muted
	mov	x2,x0
	add	x0,x20,8
	add	x1,x21,9
	mov	w3,w22
	mov	w4,w22
	bl	sd_speaker
	b	.out
.lune:	mov	w5,w22				; un croissant : un disque, un autre
	mov	w6,w22				; de la couleur du rond par-dessus
	add	x0,x20,9
	add	x1,x21,9
	mov	x2,14
	mov	x3,14
	mov	x4,7
	bl	ui_rbox
	ldr	w5,[x28,TJ_LIGNE_FORTE]
	adr	x0,dnd
	ldr	x0,[x0]
	cbz	x0,.l1
	ldr	w5,[x28,TJ_ACCENT]
.l1:	mov	w6,w5
	add	x0,x20,14
	add	x1,x21,6
	mov	x2,13
	mov	x3,13
	mov	x4,6
	bl	ui_rbox
	b	.out
.wifi:	adr	x0,wl_wstat			; barres (le signal si connecte)
	ldr	w1,[x0]
	mov	x2,0
	cmp	w1,WIFI_ST_CONNECTED
	b.ne	.wb
	ldrsw	x0,[x0,4]
	bl	wd_level
	mov	x2,x0
.wb:	add	x0,x20,8
	add	x1,x21,9
	mov	w3,w22
	ldr	w4,[x28,TJ_TEXTE3]
	bl	wd_bars
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (x28) x0 = numero dans la liste, x1 = y : une carte ELEVE (icone de l'app,
; titre et texte, « App, il y a 2 min », x)
draw_card:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	adr	x0,notes
	mov	x1,NH_SIZE
	madd	x22,x19,x1,x0			; l'entree
	mov	x20,16				; x (glissee : decalee)
	adr	x0,press_note
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.x
	adr	x0,drag_dx
	ldr	x0,[x0]
	add	x20,x20,x0
.x:	mov	x23,QW-32			; largeur
	mov	x0,x20
	mov	x1,x21
	mov	x2,x23
	mov	x3,CARD_H
	mov	x4,RAYON
	add	x5,x19,K_NOTES
	bl	anneau
	ldr	w5,[x28,TJ_LIGNE]
	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,x23
	mov	x3,CARD_H
	mov	x4,RAYON
	bl	ui_rbox
	ldr	w5,[x28,TJ_ELEVE]
	ldr	w6,[x28,TJ_ELEVE_BAS]
	add	x0,x20,1
	add	x1,x21,1
	sub	x2,x23,2
	mov	x3,CARD_H-2
	mov	x4,RAYON-1
	bl	ui_rbox
	add	x0,x22,NH_APP			; icone
	add	x1,x20,10
	add	x2,x21,(CARD_H-28)/2
	mov	x3,28
	bl	ui_icone
	add	x0,x22,NH_TITRE			; titre, puis le texte
	add	x1,x20,48
	add	x2,x21,22
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font_b
	sub	x6,x23,48+34
	bl	ui_text
	add	x1,x0,8
	add	x2,x20,x23
	sub	x2,x2,34
	subs	x6,x2,x1
	b.le	.sub
	cmp	x6,24
	b.lt	.sub
	add	x0,x22,NH_TEXTE
	bl	premiere_ligne
	add	x2,x21,22
	ldr	w3,[x28,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	bl	ui_text
.sub:	mov	x0,x22				; « App, il y a 2 min »
	bl	texte_origine
	adr	x0,lignebuf
	add	x1,x20,48
	add	x2,x21,40
	ldr	w3,[x28,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font
	sub	x6,x23,48+34
	bl	ui_text
	adr	x0,s_g_cross			; x
	add	x1,x20,x23
	sub	x1,x1,26
	add	x2,x21,(CARD_H-16)/2
	mov	x3,16
	ldr	w4,[x28,TJ_TEXTE3]
	bl	ui_glyph
	mov	x0,x20				; zones : la carte, puis le x
	mov	x1,x21
	mov	x2,x23
	mov	x3,CARD_H
	mov	x4,A_NOTE
	mov	x5,x19
	bl	wl_hit_add
	add	x0,x20,x23
	sub	x0,x0,36
	add	x1,x21,6
	mov	x2,34
	mov	x3,CARD_H-12
	mov	x4,A_NOTE_X
	mov	x5,x19
	bl	wl_hit_add
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1, x6 gardes : sa premiere ligne dans lignebuf2 -> x0
premiere_ligne:
	adr	x2,lignebuf2
	mov	x3,0
.c:	ldrb	w4,[x0,x3]
	cbz	w4,.f
	cmp	w4,10
	b.eq	.f
	strb	w4,[x2,x3]
	add	x3,x3,1
	cmp	x3,190
	b.lo	.c
.f:	strb	wzr,[x2,x3]
	mov	x0,x2
	ret

; x0 = entree : lignebuf <- « <libelle de l'app>, il y a 2 min »
texte_origine:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	add	x0,x19,NH_APP			; le libelle du catalogue, ou le nom
	bl	app_find
	add	x1,x19,NH_APP
	tbnz	x0,63,.nom
	adr	x1,ap_lib
	mov	x2,AP_LIB
	madd	x1,x0,x2,x1
.nom:	adr	x0,lignebuf
	strb	wzr,[x0]
	bl	str_cat
	adr	x1,s_virgule
	bl	str_cat
	mov	x20,x0
	sys	SYS_TIME_MS			; depuis quand
	ldr	x1,[x19,NH_TIME]
	sub	x0,x0,x1
	movl	x1,60000
	udiv	x0,x0,x1			; minutes
	adr	x2,s_instant
	cbz	x0,.fixe
	adr	x2,s_il_min
	cmp	x0,60
	b.lo	.n
	mov	x1,60
	udiv	x0,x0,x1			; heures
	adr	x2,s_il_h
	cmp	x0,24
	b.lo	.n
	mov	x1,24
	udiv	x0,x0,x1			; jours
	adr	x2,s_il_j
.n:	mov	x3,x0				; « # » -> le nombre
	mov	x0,x2
	bl	tr_x0
	mov	x1,x0
	mov	x0,x20
.c:	ldrb	w2,[x1],1
	cbz	w2,.f
	cmp	w2,'#'
	b.eq	.num
	strb	w2,[x0],1
	b	.c
.num:	stp	x1,x3,[sp,-16]!
	mov	x1,x0
	mov	x0,x3
	bl	fmt_dec
	ldp	x1,x3,[sp],16
	b	.c
.f:	strb	wzr,[x0]
	b	.out
.fixe:	mov	x0,x2
	bl	tr_x0
	mov	x1,x0
	mov	x0,x20
	bl	str_cat
.out:	ldp	x19,x20,[sp],16
	epilogue

; (x28) le detail du Wi-Fi : la liste de wifimenu
draw_wifi:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,s_wifi
	bl	tete_detail
	bl	wl_state_text
	bl	etat_tete
	mov	x0,DET_Y
	bl	wl_draw
	add	x20,x0,4			; Parametres Wi-Fi...
	mov	x0,12
	mov	x1,x20
	mov	x2,QW-24
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	mov	x0,8
	add	x1,x20,2
	mov	x2,QW-16
	mov	x3,DFOOT-6
	mov	x4,A_WPARAM
	mov	x5,0
	bl	wl_hit_add
	adr	x0,s_parametres
	bl	tr_x0
	mov	x1,16
	add	x2,x20,21
	ldr	w3,[x28,TJ_ACCENT]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	ldp	x19,x20,[sp],16
	epilogue

; (x28) le detail du son : volume, muet, la sortie principale
draw_son:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,s_son
	bl	tete_detail
	adr	x0,sl_mainp
	ldr	x19,[x0]
	adr	x0,s_aucune_sortie
	bl	tr_x0
	cbz	x19,.et
	add	x0,x19,SO_NAME
.et:	bl	etat_tete
	cbz	x19,.list
	adr	x0,sl_sx
	mov	x1,SL_X0
	mov	x2,SL_W0
	stp	x1,x2,[x0]
	mov	x0,SL_X0-6
	mov	x1,DET_Y+8
	mov	x2,SL_W0+12
	mov	x3,24
	mov	x4,A_SLIDER
	mov	x5,0
	bl	wl_hit_add
	mov	x0,DET_Y+12
	bl	sl_draw_slider
	mov	x0,8				; Muet
	mov	x1,DET_Y+40
	mov	x2,140
	mov	x3,26
	mov	x4,A_MUTE
	mov	x5,0
	bl	wl_hit_add
	mov	x0,16
	mov	x1,DET_Y+46
	ldr	w2,[x19,SO_MUTE]
	bl	sd_check
	adr	x0,s_muet
	bl	tr_x0
	mov	x1,38
	mov	x2,DET_Y+58
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.list:	mov	x0,12
	mov	x1,DET_Y+74
	mov	x2,QW-24
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	adr	x0,s_sortie
	bl	tr_x0
	mov	x1,16
	mov	x2,DET_Y+92
	ldr	w3,[x28,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	adr	x0,sl_nshow
	ldr	x3,[x0]
	mov	x0,SL_ROWH
	mul	x3,x3,x0
	mov	x0,8
	mov	x1,DET_Y+100
	mov	x2,QW-16
	mov	x4,A_SOUT
	mov	x5,0
	bl	wl_hit_add
	mov	x0,DET_Y+100
	mov	x1,QW
	bl	sl_draw_outputs
	mov	x20,x0
	mov	x0,12
	add	x1,x20,6
	mov	x2,QW-24
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	mov	x0,QW-134
	add	x1,x20,12
	mov	x2,120
	mov	x3,26
	mov	x4,A_MIXER
	mov	x5,0
	bl	wl_hit_add
	mov	x0,QW-134
	add	x1,x20,12
	mov	x2,120
	mov	x3,26
	adr	x4,s_mixer
	bl	tr_x4
	mov	x5,0
	bl	ui_button
	ldp	x19,x20,[sp],16
	epilogue

; (x28) x0 = titre du detail : ‹ titre, trait
tete_detail:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,8
	mov	x1,8
	mov	x2,130
	mov	x3,38
	mov	x4,A_BACK
	mov	x5,0
	bl	wl_hit_add
	adr	x0,s_g_left
	mov	x1,14
	mov	x2,20
	mov	x3,16
	ldr	w4,[x28,TJ_TEXTE1]
	bl	ui_glyph
	mov	x0,x19
	bl	tr_x0
	mov	x1,38
	mov	x2,34
	ldr	w3,[x28,TJ_TEXTE1]
	mov	x4,16
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x0,12
	mov	x1,DET_Y-6
	mov	x2,QW-24
	mov	x3,1
	ldr	w4,[x28,TJ_LIGNE]
	bl	ui_fill
	ldr	x19,[sp],16
	epilogue

; (x28) x0 = texte : l'etat, a droite de la tete du detail
etat_tete:
	prologue
	mov	x1,130
	mov	x2,34
	ldr	w3,[x28,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,QW-150
	bl	ui_text
	epilogue

; ---------------------------------------------------------------------------
;  La date
; ---------------------------------------------------------------------------

; -> x0 = minute locale (depuis 1970)
minute:
	mov	x0,2
	mov	x8,SYS_CLOCK_GET
	svc	0
	mov	x1,60
	udiv	x0,x0,x1
	ret

; datebuf <- « jeudi 5 novembre », heurebuf <- « 14:32 »
textes_date:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	minute
	movl	x1,1440
	udiv	x19,x0,x1			; jours
	msub	x20,x19,x1,x0			; minute du jour
	adr	x0,heurebuf
	mov	x1,60
	udiv	x1,x20,x1
	bl	deux
	mov	w1,':'
	strb	w1,[x0],1
	mov	x2,60
	udiv	x1,x20,x2
	msub	x1,x1,x2,x20
	bl	deux
	strb	wzr,[x0]
	add	x0,x19,3			; jour de la semaine (1/1/1970 : jeudi)
	mov	x1,7
	udiv	x2,x0,x1
	msub	x21,x2,x1,x0			; 0 lundi
	mov	x0,x19
	bl	civil				; x1 mois, x2 jour
	mov	x22,x1
	mov	x19,x2
	adr	x0,datebuf
	strb	wzr,[x0]
	adr	x1,s_jours
	add	x1,x1,x21,lsl 4
	bl	tr_x1
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	mov	x0,x19
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	strb	wzr,[x0]
	sub	x1,x22,1
	adr	x2,s_mois
	add	x1,x2,x1,lsl 4
	bl	tr_x1
	bl	str_cat
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = 0..99 : deux chiffres -> x0 avance
deux:
	mov	x4,10
	udiv	x5,x1,x4
	msub	x6,x5,x4,x1
	add	w5,w5,'0'
	add	w6,w6,'0'
	strb	w5,[x0],1
	strb	w6,[x0],1
	ret

; x0 = jours depuis 1970 -> x0 annee, x1 mois, x2 jour (H. Hinnant)
civil:
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

; ---------------------------------------------------------------------------
;  Souris et clavier
; ---------------------------------------------------------------------------

; (ui_ev) -> x0 = 0 rien, 1 redessiner, 3 fermer
souris:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x9,ui_ev
	ldr	w19,[x9,EV_KIND]
	ldrsw	x20,[x9,EV_X]
	ldrsw	x21,[x9,EV_Y]
	cmp	w19,1
	b.eq	.appui
	cmp	w19,2
	b.eq	.relache
	adr	x0,dragging			; deplacement : curseur, carte glissee
	ldr	x0,[x0]
	cbz	x0,.carte
	mov	x0,x20
	bl	sl_set_volume
	b	.oui
.carte:	adr	x0,press_note
	ldr	x0,[x0]
	tbnz	x0,63,.rien
	adr	x0,press_x
	ldr	x0,[x0]
	sub	x0,x20,x0
	cmp	x0,0
	csel	x0,xzr,x0,lt
	adr	x1,drag_dx
	ldr	x2,[x1]
	cmp	x0,x2
	b.eq	.rien
	str	x0,[x1]
	b	.oui
.relache:
	adr	x0,dragging
	str	xzr,[x0]
	adr	x0,press_note
	ldr	x19,[x0]
	tbnz	x19,63,.rien
	mov	x1,-1
	str	x1,[x0]
	adr	x0,press_x
	ldr	x0,[x0]
	sub	x0,x20,x0
	adr	x1,drag_dx
	str	xzr,[x1]
	cmp	x0,SWIPE			; glissee a droite : effacee
	b.ge	.effacer
	cmp	x0,10				; touchee : son app
	b.ge	.oui
	cmn	x0,10
	b.le	.oui
	mov	x0,x19
	bl	ouvrir_note
	cbnz	x0,.out
	b	.oui
.effacer:
	mov	x0,x19
	bl	effacer_note
	b	.oui
.appui:	adr	x0,kf				; (la souris : plus d'anneau)
	mov	x1,-1
	str	x1,[x0]
	mov	x0,x20
	mov	x1,x21
	bl	wl_click
	cmp	x0,1
	b.eq	.oui
	cbz	x0,.rien
	mov	x2,x1				; une zone du volet
	mov	x1,x0
	mov	x0,x2
	cmp	x1,A_ROND
	b.eq	.rond
	cmp	x1,A_TILE
	b.eq	.tuile
	cmp	x1,A_SLIDER
	b.eq	.slider
	cmp	x1,A_MUTE
	b.eq	.muet
	cmp	x1,A_CLEAR
	b.eq	.tout
	cmp	x1,A_NOTE
	b.eq	.note
	cmp	x1,A_NOTE_X
	b.eq	.effacer2
	cmp	x1,A_SETTINGS
	b.eq	.reglages
	cmp	x1,A_POWER
	b.eq	.alim
	cmp	x1,A_BACK
	b.eq	.retour
	cmp	x1,A_WPARAM
	b.eq	.wparam
	cmp	x1,A_MIXER
	b.eq	.mixer
	cmp	x1,A_SOUT
	b.eq	.sortie
	b	.rien
.rond:	bl	basculer_tuile
	b	.res
.tuile:	bl	ouvrir_tuile
.res:	cbnz	x0,.out
	b	.oui
.slider:
	adr	x0,dragging
	mov	x1,1
	str	x1,[x0]
	mov	x0,x20
	bl	sl_set_volume
	b	.oui
.muet:	bl	sl_toggle_mute
	b	.oui
.tout:	mov	x0,NOTIFY_OP_CLEAR
	mov	x1,0
	bl	nd_call
	bl	charger
	b	.oui
.note:	adr	x1,press_note			; on attend le relachement
	str	x0,[x1]
	adr	x1,press_x
	str	x20,[x1]
	adr	x1,drag_dx
	str	xzr,[x1]
	b	.rien
.effacer2:
	bl	effacer_note
	b	.oui
.reglages:
	adr	x0,p_control
	mov	x1,0
	sys	SYS_SPAWN
	b	.ferme
.alim:	adr	x0,p_shutdown
	mov	x1,0
	sys	SYS_SPAWN
	b	.ferme
.wparam:
	adr	x0,p_control
	adr	x1,s_arg_wifi
	sys	SYS_SPAWN
	b	.ferme
.mixer:	adr	x0,p_mixer
	mov	x1,0
	sys	SYS_SPAWN
	b	.ferme
.retour:
	adr	x0,vue
	str	xzr,[x0]
	adr	x0,wl_selrow
	mov	x1,-1
	str	x1,[x0]
	b	.oui
.sortie:
	mov	x0,x21
	bl	sl_click_outputs
	b	.oui
.ferme:	mov	x0,3
	b	.out
.rien:	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ui_ev, un appui) -> x0 = 0 rien, 1 redessiner, 3 fermer
touche:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x9,ui_ev
	ldr	w19,[x9,EV_CODE]
	adr	x0,vue
	ldr	x20,[x0]
	cmp	x20,V_WIFI			; le Wi-Fi : le champ de la cle d'abord
	b.ne	.esc
	cmp	w19,KEY_ESC
	b.eq	.esc
	mov	x0,x9
	bl	wl_key
	cbnz	x0,.oui
.esc:	cmp	w19,KEY_ESC
	b.ne	.det
	cbz	x20,.ferme
	adr	x0,vue				; detail : retour
	str	xzr,[x0]
	adr	x0,wl_selrow
	mov	x1,-1
	str	x1,[x0]
	b	.oui
.det:	cbz	x20,.main
	cmp	x20,V_SOUND			; le son : fleches = volume, sortie
	b.ne	.rien
	cmp	w19,KEY_RIGHT
	b.eq	.plus
	cmp	w19,KEY_LEFT
	b.eq	.moins
	cmp	w19,QK_SPACE
	b.eq	.muet
	cmp	w19,KEY_DOWN
	b.eq	.sbas
	cmp	w19,KEY_UP
	b.eq	.shaut
	b	.rien
.sbas:	mov	x0,1
	b	.sortie
.shaut:	mov	x0,-1
.sortie:
	bl	sortie_voisine
	b	.oui
.main:	adr	x0,kf				; premiere touche : l'anneau parait
	ldr	x21,[x0]
	tbz	x21,63,.k
	mov	x1,0
	str	x1,[x0]
	cmp	w19,KEY_DELETE			; (Suppr : rien a effacer encore)
	b.eq	.oui
	b	.oui
.k:	adr	x0,n_notes
	ldr	x22,[x0]
	add	x22,x22,K_NOTES			; x22 : touche de Reglages
	cmp	w19,KEY_RIGHT
	b.eq	.droite
	cmp	w19,KEY_LEFT
	b.eq	.gauche
	cmp	w19,KEY_DOWN
	b.eq	.bas
	cmp	w19,KEY_UP
	b.eq	.haut
	cmp	w19,KEY_TAB
	b.eq	.bas
	cmp	w19,QK_SPACE
	b.eq	.espace
	cmp	w19,KEY_ENTER
	b.eq	.entree
	cmp	w19,KEY_DELETE
	b.eq	.suppr
	b	.rien
.droite:
	cmp	x21,K_SLIDER
	b.eq	.plus
	cmp	x21,x22				; Reglages -> Alimentation
	b.eq	.suiv
	cmp	x21,K_SLIDER
	b.hs	.rien
	tbnz	x21,0,.rien			; tuiles : 0 -> 1, 2 -> 3
	b	.suiv
.gauche:
	cmp	x21,K_SLIDER
	b.eq	.moins
	add	x0,x22,1
	cmp	x21,x0
	b.eq	.prec
	cmp	x21,K_SLIDER
	b.hs	.rien
	tbz	x21,0,.rien
	b	.prec
.bas:	cmp	x21,2				; tuiles du haut : celles du bas
	b.lo	.plus2
	cmp	x21,K_SLIDER
	b.lo	.vers_sl
	cmp	x21,K_CLEAR			; curseur, Tout effacer : la premiere
	b.hi	.b1				; notification, ou Reglages s'il n'y en
	adr	x1,n_notes			; a pas (Tout effacer est alors cache)
	ldr	x1,[x1]
	cbnz	x1,.b0
	mov	x21,x22
	b	.set
.b0:	b	.suiv
.b1:	cmp	x21,x22
	b.hs	.rien
	b	.suiv
.vers_sl:
	mov	x21,K_SLIDER
	adr	x0,sl_mainp
	ldr	x0,[x0]
	cbnz	x0,.set
	mov	x21,K_CLEAR
	b	.set
.plus2:	add	x21,x21,2
	b	.set
.haut:	cmp	x21,2
	b.lo	.rien
	cmp	x21,K_SLIDER
	b.lo	.moins2
	b.eq	.vers_t
	cmp	x21,K_CLEAR
	b.ne	.h1
	mov	x21,K_SLIDER
	adr	x0,sl_mainp
	ldr	x0,[x0]
	cbnz	x0,.set
.vers_t:
	mov	x21,2
	b	.set
.h1:	cmp	x21,x22				; Reglages, Alimentation : la derniere
	b.lo	.prec				; notification, sinon le curseur
	sub	x21,x22,1
	cmp	x21,K_CLEAR
	b.hi	.set
	mov	x21,K_SLIDER
	adr	x0,sl_mainp
	ldr	x0,[x0]
	cbnz	x0,.set
	mov	x21,2
	b	.set
.moins2:
	sub	x21,x21,2
	b	.set
.suiv:	add	x21,x21,1
	b	.set
.prec:	sub	x21,x21,1
.set:	adr	x0,kf
	str	x21,[x0]
	bl	montrer_kf
	b	.oui
.plus:	mov	x0,5
	bl	sl_step_volume
	b	.oui
.moins:	mov	x0,-5
	bl	sl_step_volume
	b	.oui
.muet:	bl	sl_toggle_mute
	b	.oui
.espace:
	cmp	x21,K_SLIDER
	b.lo	.esp_t
	b.eq	.muet
	b	.entree
.esp_t:	mov	x0,x21
	bl	basculer_tuile
	cbnz	x0,.out
	b	.oui
.entree:
	cmp	x21,K_SLIDER
	b.lo	.ent_t
	b.eq	.muet
	cmp	x21,K_CLEAR
	b.eq	.tout
	cmp	x21,x22
	b.eq	.reglages
	b.hi	.alim
	sub	x0,x21,K_NOTES			; une notification : son app
	bl	ouvrir_note
	cbnz	x0,.out
	b	.oui
.ent_t:	mov	x0,x21
	bl	ouvrir_tuile
	cbnz	x0,.out
	adr	x0,vue				; (un detail : l'anneau s'en va)
	ldr	x0,[x0]
	cbz	x0,.oui
	b	.oui
.tout:	mov	x0,NOTIFY_OP_CLEAR
	mov	x1,0
	bl	nd_call
	bl	charger
	adr	x0,kf
	mov	x1,K_CLEAR
	str	x1,[x0]
	b	.oui
.suppr:	cmp	x21,K_NOTES
	b.lo	.rien
	cmp	x21,x22
	b.hs	.rien
	sub	x0,x21,K_NOTES
	bl	effacer_note
	b	.oui
.reglages:
	adr	x0,p_control
	mov	x1,0
	sys	SYS_SPAWN
	b	.ferme
.alim:	adr	x0,p_shutdown
	mov	x1,0
	sys	SYS_SPAWN
.ferme:	mov	x0,3
	b	.out
.rien:	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = +1 / -1 : la sortie suivante ou precedente devient la principale
sortie_voisine:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,sl_nshow
	ldr	x20,[x0]
	cbz	x20,.out
	adr	x0,sl_mainp			; la principale dans la liste
	ldr	x1,[x0]
	adr	x2,sl_shown
	mov	x3,0
.c:	cmp	x3,x20
	b.hs	.pas
	ldr	x4,[x2,x3,lsl 3]
	cmp	x4,x1
	b.eq	.trouve
	add	x3,x3,1
	b	.c
.pas:	mov	x3,0
	mov	x19,0
.trouve:
	add	x3,x3,x19
	cmp	x3,0
	csel	x3,xzr,x3,lt
	cmp	x3,x20
	sub	x4,x20,1
	csel	x3,x4,x3,hs
	ldr	x0,[x2,x3,lsl 3]
	ldr	w0,[x0,SO_ID]
	bl	ak_main
	bl	sl_changed
.out:	ldp	x19,x20,[sp],16
	epilogue

include '../../inc/notify.inc'

s_nom		db 'quick',0
s_notify	db 'notify',0
s_titre		db 'R',0xC3,0xA9,'glages rapides',0
s_t_quick	db 'korin.shell.quick',0
s_t_sleep	db 'korin.shell.sleep',0
s_t_unread	db 'korin.notify.unread',0
s_t_dnd		db 'korin.notify.dnd',0
s_un		db '1',0
s_zero		db '0',0
s_wifi		db 'Wi-Fi',0
s_dnd		db 'Ne pas d',0xC3,0xA9,'ranger',0
s_son		db 'Son',0
s_repos		db 'Repos',0
s_super_l	db 'Super+L',0
s_active	db 'Activ',0xC3,0xA9,0
s_desactive	db 'D',0xC3,0xA9,'sactiv',0xC3,0xA9,0
s_muet		db 'Muet',0
s_aucune_sortie	db 'Aucune sortie',0
s_pct		db ' % ',0xC2,0xB7,' ',0
s_indispo	db 'Indisponible',0
s_coupe		db 'Coup',0xC3,0xA9,0
s_nonco		db 'Non connect',0xC3,0xA9,0
s_notifs	db 'Notifications',0
s_effacer	db 'Tout effacer',0
s_aucune	db 'Aucune notification',0
s_reglages	db 'R',0xC3,0xA9,'glages',0
s_alim		db 'Alimentation',0xE2,0x80,0xA6,0
s_parametres	db 'Param',0xC3,0xA8,'tres Wi-Fi...',0
s_sortie	db 'Sortie principale',0
s_mixer		db 'Mixer',0xE2,0x80,0xA6,0
s_virgule	db ', ',0
s_instant	db 0xC3,0xA0,' l',0x27,'instant',0
s_il_min	db 'il y a # min',0
s_il_h		db 'il y a # h',0
s_il_j		db 'il y a # j',0
s_main		db '.app/main',0
s_g_power	db 'glyph-power',0
s_g_cross	db 'glyph-tb-cross',0
s_g_left	db 'glyph-tb-left',0
p_control	db 'SD0:/apps/control.app/main',0
p_shutdown	db 'SD0:/sys/shutdown',0
p_mixer		db 'SD0:/apps/mixer.app/main',0
s_arg_wifi	db 'wifi',0
		align 8
s_jours:					; (16 octets chacun)
	db 'lundi',0
	times 16-6 db 0
	db 'mardi',0
	times 16-6 db 0
	db 'mercredi',0
	times 16-9 db 0
	db 'jeudi',0
	times 16-6 db 0
	db 'vendredi',0
	times 16-9 db 0
	db 'samedi',0
	times 16-7 db 0
	db 'dimanche',0
	times 16-9 db 0
s_mois:
	db 'janvier',0
	times 16-8 db 0
	db 'f',0xC3,0xA9,'vrier',0
	times 16-9 db 0
	db 'mars',0
	times 16-5 db 0
	db 'avril',0
	times 16-6 db 0
	db 'mai',0
	times 16-4 db 0
	db 'juin',0
	times 16-5 db 0
	db 'juillet',0
	times 16-8 db 0
	db 'ao',0xC3,0xBB,'t',0
	times 16-6 db 0
	db 'septembre',0
	times 16-10 db 0
	db 'octobre',0
	times 16-8 db 0
	db 'novembre',0
	times 16-9 db 0
	db 'd',0xC3,0xA9,'cembre',0
	times 16-10 db 0
		align 4

include '../../inc/tr.inc'

segment readable writeable
kit_data
ui_data
soundlist_data
wifilist_data
applis_data
		align 8
canal		dd 0
nd_canal	dd 0				; notifyd (0 : a ouvrir)
sub_q		dd 0				; korin.shell.quick
sub_u		dd 0				; korin.notify.unread
ouvert		dd 0
cur_win		dd 0				; la fenetre du volet (0 : fermee)
vue		dd 0				; V_*
kf		dd -1				; element du clavier (-1 : aucun)
nf_first	dd 0				; premiere notification montree
n_notes		dd 0
dnd		dd 0
wifi_off	dd 0				; coupe par la tuile
t_ferme		dd 0				; (ms) volet ferme par la perte du clavier
minute_vue	dd 0
dragging	dd 0				; le curseur du volume suit la souris
press_note	dd -1				; carte appuyee (on attend le relachement)
press_x		dd 0
drag_dx		dd 0
win_h		dd 0
nom2		dd 0				; (tuile) la suite d'un nom coupe
tmpv		rb 16
scr		rb 32
numbuf		rb 24
datebuf		rb 64
heurebuf	rb 16
etatbuf		rb 96
lignebuf	rb 160
lignebuf2	rb 200
chemin		rb 96
wlist		rb WL_PER_MSG * WL_SIZE
msg		rb 512
notes		rb NL_MAX * NH_SIZE
