; wifimenu - le menu Wi-Fi de la barre de menus (comme wifimenu d'Onyx ;
; docs/RESEAU.md 4.6) : lance par menubar (un clic sur l'icone du reseau)
; avec l'abscisse de l'icone. Une petite fenetre sans cadre sous l'icone :
;  * les reseaux visibles, du plus fort au plus faible ("Recherche..."
;    pendant le balayage), signal, cadenas, coche sur le reseau courant ;
;  * un clic : connu ou ouvert -> connexion aussitot ; inconnu et securise ->
;    la ligne s'ouvre avec un champ de cle et Se connecter ; "Connexion..."
;    puis fermeture une fois connecte ;
;  * en bas, "Parametres Wi-Fi..." : le panneau de configuration sur l'applet
;    Wi-Fi ;
;  * l'etat du Wi-Fi est relu quand le pilote le publie sur le bus
;    (korin.net.wifi) ; sans busd, chaque seconde ;
;  * se ferme sur Echap, quand une autre fenetre prend le clavier, ou par un
;    second clic sur l'icone (service "wifimenu" : un second wifimenu le
;    ferme et s'arrete).
; La liste des reseaux est partagee avec les reglages rapides (inc/wifilist.inc).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/netkit/netkit.inc'
include '../../inc/netd.inc'
include '../../kits/buskit/buskit.inc'

MW		= 300			; largeur
HEAD		= 34
FOOT		= 34

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so

start:
	mov	x19,x0				; abscisse de l'icone
	bl	kits_open
	adr	x0,s_nom			; deja ouvert : on le ferme
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbz	x0,63,.seul
	adr	x0,s_nom
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x1,msg
	mov	x2,1				; op 1 : fermer
	str	x2,[x1]
	sys	SYS_SVC_CALL
	b	quit
.seul:	adr	x1,canal
	str	x0,[x1]
	mov	x20,400				; abscisse
	cbz	x19,.ui
	mov	x0,x19
	bl	parse_dec
	mov	x20,x0
.ui:	adr	x0,ui_win
	bl	ui_init
	adr	x0,scr
	bl	ui_screen
	adr	x0,scr				; sous la barre de menus, aligne sur
	ldr	x21,[x0]			; l'icone, dans l'ecran
	ldr	x22,[x0,24]			; (reserve en haut)
	add	x0,x20,24
	sub	x0,x0,MW
	cmp	x0,4
	mov	x1,4
	csel	x0,x1,x0,lt
	sub	x1,x21,MW+4
	cmp	x0,x1
	csel	x0,x1,x0,gt
	adr	x1,win_x
	str	x0,[x1]
	adr	x1,win_y
	add	x2,x22,2
	str	x2,[x1]
	adr	x0,wl_w
	mov	x1,MW
	str	x1,[x0]
	mov	x0,1				; l'etat, un balayage tout de suite,
	bl	wl_start			; le thread des travaux
	tbnz	x0,63,quit
	bl	ouvrir_fenetre
	tbnz	x0,63,quit
	bl	draw
.loop:	adr	x0,canal			; un second wifimenu : fermer
	ldr	x0,[x0]
	adr	x1,msg
	mov	x2,0
	sys	SYS_SVC_NEXT
	cmp	x0,0
	b.le	.ev
	adr	x1,msg
	str	xzr,[x1]
	sys	SYS_SVC_REPLY
	b	quit
.ev:	mov	x0,200
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_FOCUS
	b.eq	.focus
	cmp	x0,WEV_KEY
	b.eq	.keybuf
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cmp	x0,WEV_BUS
	b.eq	.bus
	cbnz	x0,.loop
.tick:	bl	wl_tick				; (rien : l'etat, les travaux)
	cmp	x0,2				; connecte : fermer
	b.eq	.fermer
	cbz	x0,.loop
	bl	redraw
	b	.loop
.bus:	adr	x9,ui_ev			; korin.net.wifi : l'etat a change
	ldr	w0,[x9,BEV_SUB]
	bl	wl_bus
	cbz	x0,.loop
	b	.tick
.focus:	adr	x9,ui_ev			; une autre fenetre a le clavier
	ldr	w0,[x9,EV_KIND]
	cbz	w0,quit
	b	.loop
.keybuf:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cbz	w0,.loop
	ldr	w0,[x9,EV_CODE]
	cmp	w0,KEY_ESC
	b.eq	quit
	mov	x0,x9
	bl	wl_key
	cbz	x0,.loop
	bl	redraw
	b	.loop
.mouse:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cmp	w0,1
	b.ne	.loop
	ldrsw	x0,[x9,EV_X]
	ldrsw	x1,[x9,EV_Y]
	bl	wl_click
	cmp	x0,3				; Parametres Wi-Fi...
	b.eq	.param
	cbz	x0,.loop
	bl	redraw
	b	.loop
.param:	adr	x0,p_control			; le panneau, sur l'applet Wi-Fi
	adr	x1,s_arg_wifi
	sys	SYS_SPAWN
	b	quit
.fermer:
	bl	redraw
	mov	x0,900
	sys	SYS_SLEEP
quit:	mov	x0,0
	sys	SYS_EXIT

; hauteur selon le contenu -> x0
hauteur:
	prologue
	bl	wl_height
	add	x0,x0,HEAD+FOOT+8
	epilogue

; -> x0 = poignee de la fenetre (a la hauteur du contenu)
ouvrir_fenetre:
	prologue
	bl	hauteur
	adr	x1,win_h
	str	x0,[x1]
	mov	x3,x0
	adr	x0,win_x
	ldr	x0,[x0]
	adr	x1,win_y
	ldr	x1,[x1]
	mov	x2,MW
	adr	x4,title
	bl	tr_x4
	mov	x5,WF_FRAMELESS or WF_TOP or WF_NOLIST or WF_ALPHA
	bl	ui_window_ex
	epilogue

; la hauteur a change : nouvelle fenetre ; puis dessin
redraw:
	prologue
	bl	hauteur
	adr	x1,win_h
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.d
	bl	ui_close
	bl	ouvrir_fenetre
.d:	bl	draw
	epilogue

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	wl_nhits_reset
	bl	ui_jetons
	mov	x23,x0
	adr	x0,ui_w
	ldp	x0,x1,[x0]
	bl	ui_ombre			; fond, ombre, contour
	ldr	w5,[x23,TJ_MENU]
	mov	w6,w5
	mov	x0,4
	mov	x1,4
	adr	x2,ui_w
	ldp	x2,x3,[x2]
	sub	x2,x2,8
	sub	x3,x3,8
	mov	x4,8
	bl	ui_rbox
	adr	x0,s_wifi			; titre, etat a droite
	mov	x1,16
	mov	x2,26
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	bl	wl_state_text
	mov	x1,90
	mov	x2,26
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,MW-104
	bl	ui_text
	mov	x0,12				; trait
	mov	x1,HEAD
	mov	x2,MW-24
	mov	x3,1
	ldr	w4,[x23,TJ_LIGNE]
	bl	ui_fill
	mov	x0,HEAD+4			; les reseaux
	bl	wl_draw
	add	x20,x0,4			; Parametres Wi-Fi...
	mov	x0,12
	mov	x1,x20
	mov	x2,MW-24
	mov	x3,1
	ldr	w4,[x23,TJ_LIGNE]
	bl	ui_fill
	mov	x0,8
	add	x1,x20,2
	mov	x2,MW-16
	mov	x3,FOOT-6
	mov	x4,3
	mov	x5,0
	bl	wl_hit_add
	adr	x0,s_parametres
	bl	tr_x0
	mov	x1,16
	add	x2,x20,21
	ldr	w3,[x23,TJ_ACCENT]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	bl	ui_update_all
	ldp	x23,x24,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include '../../inc/wifidraw.inc'
include '../../inc/wifilist.inc'

s_nom		db 'wifimenu',0
title		db 'Wi-Fi',0
s_wifi		db 'Wi-Fi',0
s_parametres	db 'Param',0xC3,0xA8,'tres Wi-Fi...',0
p_control	db 'SD0:/apps/control.app/main',0
s_arg_wifi	db 'wifi',0
		align 8

include '../../inc/tr.inc'

segment readable writeable
kit_data
ui_data
wifilist_data
		align 8
canal		dd 0
win_x		dd 0
win_y		dd 0
win_h		dd 0
scr		rb 32
msg		rb 512
