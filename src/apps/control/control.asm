; control - le panneau de configuration, comme le control d'Onyx : une seule
; fenetre, la liste des applets a gauche (icone, nom), l'applet choisi dans le
; volet a droite (applet.inc). Les applets sont dans applets/*.inc : Theme
; (avec son apercu), Clavier et langue, Ecran, Dock, Wi-Fi, Acces a distance,
; Son, Logiciels. « control <applet> » l'ouvre sur cet applet (theme,
; clavier, ecran, dockconf, wifi, remote, sound, padconf).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/netkit/netkit.inc'
include '../../kits/printerkit/printerkit.inc'
include '../../inc/netd.inc'
include '../../inc/sound.inc'
include '../../inc/pad.inc'
include '../../inc/bus.inc'
include '../../kits/buskit/buskit.inc'

SIDE_H	= 40				; une ligne de la liste
SIDE_IC	= 24
APN	= 12				; applets
AP_E	= 24				; une entree de la table

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so
kit_use '../../kits/printerkit/printerkit.abi'		; SD0:/lib/printerkit.so
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so (korin.theme)
include '../../inc/applist.inc'
include '../../inc/dockconf.inc'
include '../../inc/panelconf.inc'
include 'applet.inc'
include 'applets/theme.inc'
include 'applets/keyboard.inc'
include 'applets/display.inc'
include 'applets/dock.inc'
include 'applets/panel.inc'
include '../../inc/wifidraw.inc'
include 'applets/wifi.inc'
include '../../inc/remotepw.inc'
include 'applets/remote.inc'
include 'applets/sound.inc'
include 'applets/padconf.inc'
include '../../inc/notify.inc'
include '../../inc/preload_notify.inc'
include 'applets/preload.inc'
include 'applets/dotnet.inc'
include 'applets/pkgman.inc'
include 'applets/printers.inc'

start:
	mov	x19,x0				; arguments : l'applet a ouvrir
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	bl	applis_load
	mov	x20,0				; l'applet nomme, sinon le premier
	cbz	x19,.size
.n:	cmp	x20,APN
	b.hs	.n0
	mov	x0,x20
	mov	x1,0
	bl	table
	mov	x1,x19
	bl	str_eq
	cbnz	x0,.size
	add	x20,x20,1
	b	.n
.n0:	mov	x20,0
.size:	bl	ap_height
	adr	x2,title
	bl	tr2
	bl	ui_window
	tbnz	x0,63,quit
	adr	x0,s_net_topics			; le reseau (applet Wi-Fi) : le bus
	mov	x1,0
	bl	bus_subscribe
	cmp	x0,0
	csel	x0,x0,xzr,gt
	adr	x1,ap_net_sub
	str	x0,[x1]
	mov	x0,x20
	bl	ouvre
	bl	side_montre
	bl	draw
.loop:	movl	x0,60000
	adr	x1,ap_tick			; applet rafraichi chaque seconde
	ldr	x1,[x1]
	cbz	x1,.ev0
	mov	x0,1000
.ev0:	adr	x1,ap_widgets			; (widgets : leurs infobulles)
	ldr	x1,[x1]
	cbz	x1,.ev
	mov	x0,250
.ev:	bl	ui_event
	cbnz	x0,.evt
	adr	x1,ap_widgets
	ldr	x1,[x1]
	cbz	x1,.notip
	bl	ui_w_idle
	b	.notip
.bus:	adr	x1,ui_ev			; korin.net.* : l'applet relit l'etat
	ldr	w1,[x1,BEV_SUB]
	adr	x2,ap_net_sub
	ldr	x2,[x2]
	cmp	x1,x2
	b.ne	.loop
	adr	x1,ap_net_maj
	mov	x2,1
	str	x2,[x1]
.notip:	adr	x1,ap_tick
	ldr	x16,[x1]
	cbz	x16,.loop
	blr	x16
	cbz	x0,.loop
	bl	draw
	b	.loop
.evt:	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_SCREEN
	b.eq	.screen
	cmp	x0,WEV_PAD
	b.eq	.pad
	cmp	x0,WEV_BUS
	b.eq	.bus
	cmp	x0,WEV_MOUSE			; (ou est la souris : pour la molette)
	b.ne	.nm
	adr	x1,ui_ev
	ldrsw	x1,[x1,EV_X]
	adr	x2,side_mx
	str	x1,[x2]
.nm:	cmp	x0,WEV_WHEEL			; molette sur la liste : elle defile
	b.ne	.nw
	adr	x1,side_mx
	ldr	x1,[x1]
	cmp	x1,SIDE_W
	b.hs	.nw
	adr	x0,ui_ev
	ldrsw	x0,[x0,EV_Y]
	bl	side_molette
	bl	draw
	b	.loop
.nw:	adr	x1,ap_widgets			; applet en widgets : a eux d'abord
	ldr	x1,[x1]
	cbz	x1,.nowid
	mov	x0,0
	bl	ui_w_event
	str	x2,[sp,-16]!
	adr	x0,ap_redraw			; (un widget cache, un message change)
	ldr	x1,[x0]
	cbz	x1,.nored
	str	xzr,[x0]
	bl	draw
.nored:	ldr	x2,[sp],16
	cbnz	x2,.loop
	adr	x0,ui_ev
	ldr	w0,[x0,EV_TYPE]
.nowid:	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_WHEEL
	b.eq	.wheel
	cmp	x0,WEV_MOUSE
	b.ne	.loop
	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cmp	w0,1
	b.ne	.loop
	ldrsw	x0,[x9,EV_X]
	ldrsw	x1,[x9,EV_Y]
	cmp	x1,36				; le haut du volet deplace la fenetre
	b.hs	.clic
	bl	ui_window_drag
	b	.loop
.clic:
	cmp	x0,SIDE_W
	b.hs	.pane
	subs	x0,x1,12			; la liste
	b.lt	.loop
	mov	x1,SIDE_H
	udiv	x0,x0,x1
	adr	x1,side_first
	ldr	x1,[x1]
	add	x0,x0,x1
	cmp	x0,APN
	b.hs	.loop
	b	.go
.pane:	bl	pane_click
	cmp	x0,1
	b.eq	.apply
	cbz	x0,.loop
	bl	draw
	b	.loop
.pad:	adr	x1,ap_manette			; une manette : l'applet qui la montre
	ldr	x16,[x1]
	cbz	x16,.loop
	adr	x0,ui_ev
	blr	x16
	cbz	x0,.loop
	bl	draw
	b	.loop
.screen:
	bl	ap_fit
	cbz	x0,.sdraw
	adr	x0,ap_widgets			; (widgets : refaits a la nouvelle taille)
	ldr	x0,[x0]
	cbz	x0,.sdraw
	adr	x0,courant
	ldr	x0,[x0]
	bl	ouvre
.sdraw:	bl	draw
	b	.loop
.wheel:	adr	x0,ui_ev			; molette : les lignes defilent
	ldrsw	x0,[x0,EV_Y]
	bl	pane_molette
	cbz	x0,.loop
	bl	draw
	b	.loop
.apply:	adr	x0,courant			; Appliquer
	ldr	x0,[x0]
	mov	x1,4
	bl	table
	blr	x0
	bl	draw
	b	.loop
.key:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cbz	w0,.loop
	adr	x1,ap_touche			; l'applet d'abord (champ de saisie)
	ldr	x16,[x1]
	cbz	x16,.global
	mov	x0,x9
	blr	x16
	cbz	x0,.global
	bl	draw
	b	.loop
.global:
	adr	x9,ui_ev
	ldr	w0,[x9,EV_CODE]
	cmp	w0,KEY_ESC
	b.eq	quit
	cmp	w0,KEY_ENTER
	b.eq	.apply
	adr	x1,courant
	ldr	x1,[x1]
	cmp	w0,KEY_UP
	b.eq	.up
	cmp	w0,KEY_DOWN
	b.ne	.loop
	add	x0,x1,1				; applet suivant, precedent
	cmp	x0,APN
	b.hs	.loop
	b	.go
.up:	subs	x0,x1,1
	b.lt	.loop
.go:	adr	x1,courant
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.loop
	bl	ouvre
	bl	side_montre
	bl	draw
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; x0 = applet, x1 = champ (0 nom, 1 libelle, 2 titre, 3 ouvrir, 4 appliquer,
; 5 icone)
; -> x0 = son adresse
table:
	adr	x2,applets
	mov	x3,AP_E
	madd	x3,x0,x3,x2
	ldrsw	x0,[x3,x1,lsl 2]
	add	x0,x2,x0
	ret

; x0 = applet : l'ouvre dans le volet (ses lignes, lues dans SD0:/etc)
ouvre:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,ap_fermer			; l'applet d'avant s'en va (ses widgets)
	ldr	x16,[x0]
	cbz	x16,.nf
	str	xzr,[x0]
	blr	x16
.nf:	adr	x0,ap_widgets
	str	xzr,[x0]
	adr	x0,ap_redraw
	str	xzr,[x0]
	adr	x0,courant
	str	x19,[x0]
	adr	x0,nrows
	str	xzr,[x0]
	adr	x0,ap_colw
	mov	x1,COL_W
	str	x1,[x0]
	adr	x0,ap_msg
	str	xzr,[x0]
	adr	x0,ap_sous
	str	xzr,[x0]
	adr	x0,ap_surface
	str	xzr,[x0]
	adr	x0,ap_extra
	str	xzr,[x0]
	adr	x0,ap_xclic
	str	xzr,[x0]
	adr	x0,ap_dessin
	str	xzr,[x0]
	adr	x0,ap_clic
	str	xzr,[x0]
	adr	x0,ap_molette
	str	xzr,[x0]
	adr	x0,ap_tick
	str	xzr,[x0]
	adr	x0,ap_touche
	str	xzr,[x0]
	adr	x0,ap_manette
	str	xzr,[x0]
	adr	x0,ap_bouton
	str	xzr,[x0]
	adr	x0,ap_defil
	str	xzr,[x0]
	mov	x0,x19
	mov	x1,2
	bl	table
	adr	x1,ap_titre
	str	x0,[x1]
	mov	x0,x19
	mov	x1,3
	bl	table
	blr	x0
	ldr	x19,[sp],16
	epilogue

; toute la fenetre : la liste, puis le volet
draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	side_borne
	bl	ui_jetons
	mov	x21,x0
	ldr	w4,[x21,TJ_CREUX]		; la liste : plus foncee, comme Fichiers
	mov	x0,0
	mov	x1,0
	mov	x2,SIDE_W
	adr	x3,ui_w
	ldr	x3,[x3,8]
	bl	ui_fill
	ldr	w4,[x21,TJ_LIGNE]
	mov	x0,SIDE_W-1
	mov	x1,0
	mov	x2,1
	adr	x3,ui_w
	ldr	x3,[x3,8]
	bl	ui_fill
	adr	x0,side_first			; (la liste defile : les lignes qui tiennent)
	ldr	x19,[x0]
.a:	cmp	x19,APN
	b.hs	.bar
	adr	x0,side_first
	ldr	x0,[x0]
	sub	x0,x19,x0
	mov	x1,SIDE_H
	mov	x2,12
	madd	x20,x0,x1,x2			; y de la ligne
	adr	x0,ui_w
	ldr	x0,[x0,8]
	add	x1,x20,SIDE_H-4
	cmp	x1,x0
	b.gt	.bar
	adr	x0,courant			; choisi : en creux accentue
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.ic
	ldr	w5,[x21,TJ_ACCENT_CLAIR]
	mov	w6,w5
	mov	x0,8
	mov	x1,x20
	mov	x2,SIDE_W-16
	mov	x3,SIDE_H-4
	mov	x4,8
	bl	ui_rbox
.ic:	mov	x0,x19
	mov	x1,5
	bl	table
	mov	x1,16
	add	x2,x20,(SIDE_H-4-SIDE_IC)/2
	mov	x3,SIDE_IC
	bl	ui_icone
	mov	x0,x19
	mov	x1,1
	bl	table
	bl	ui_tr
	mov	x1,16+SIDE_IC+10
	add	x2,x20,(SIDE_H-4)/2+5
	ldr	w3,[x21,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	adr	x6,courant
	ldr	x6,[x6]
	cmp	x6,x19
	b.ne	.t
	adr	x5,ui_font_b
.t:	mov	x6,SIDE_W-16-SIDE_IC-18
	bl	ui_text
	add	x19,x19,1
	b	.a
.bar:	bl	side_vis			; pas toutes : la barre de defilement
	cmp	x0,APN
	b.hs	.pane
	mov	x22,x0
	adr	x0,ui_w
	ldr	x3,[x0,8]
	sub	x3,x3,24			; x3 : la piste
	adr	x0,side_first
	ldr	x0,[x0]
	mul	x1,x0,x3
	mov	x2,APN
	udiv	x1,x1,x2
	add	x1,x1,12			; y du curseur
	mul	x3,x22,x3
	udiv	x3,x3,x2			; sa hauteur
	mov	x0,SIDE_W-7
	mov	x2,3
	mov	x4,1
	ldr	w5,[x21,TJ_LIGNE_FORTE]
	mov	w6,w5
	bl	ui_rbox
.pane:	bl	pane_draw
	bl	ui_update_all
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = lignes de la liste que la fenetre montre (1 au moins)
side_vis:
	adr	x0,ui_w
	ldr	x0,[x0,8]
	sub	x0,x0,12
	mov	x1,SIDE_H
	sdiv	x0,x0,x1
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lt
	ret

; side_first ramene dans [0, APN - visibles]
side_borne:
	prologue
	bl	side_vis
	mov	x1,APN
	subs	x1,x1,x0
	csel	x1,xzr,x1,lt
	adr	x2,side_first
	ldr	x3,[x2]
	cmp	x3,x1
	csel	x3,x1,x3,gt
	cmp	x3,0
	csel	x3,xzr,x3,lt
	str	x3,[x2]
	epilogue

; l'applet ouvert dans la partie visible de la liste
side_montre:
	prologue
	bl	side_vis
	adr	x2,side_first
	ldr	x3,[x2]
	adr	x4,courant
	ldr	x4,[x4]
	cmp	x4,x3
	csel	x3,x4,x3,lt			; au-dessus : il devient le premier
	add	x5,x3,x0
	cmp	x4,x5
	b.lt	.ok
	sub	x3,x4,x0			; au-dessous : le dernier
	add	x3,x3,1
.ok:	str	x3,[x2]
	bl	side_borne
	epilogue

; x0 = crans de la molette (> 0 : vers le haut)
side_molette:
	prologue
	adr	x2,side_first
	ldr	x3,[x2]
	sub	x3,x3,x0
	str	x3,[x2]
	bl	side_borne
	epilogue

; -> x0 = largeur, x1 = hauteur de la fenetre selon la place sur l'ecran ;
; ap_w, ap_h, ap_percol
ap_height:
	prologue
	adr	x0,ap_scr
	bl	ui_screen
	mov	x1,AP_FEN_H
	mov	x4,AP_FEN_W
	tbnz	x0,63,.h
	adr	x0,ap_scr
	ldr	x2,[x0]				; largeur : le volet d'Onyx si l'ecran
	sub	x2,x2,2*FRAME_BORDER		; le permet
	cmp	x2,x4
	csel	x4,x2,x4,lo
	ldr	x1,[x0,8]			; hauteur - reserves - cadre - marge
	ldp	x2,x3,[x0,16]
	sub	x1,x1,x2
	sub	x1,x1,x3
	sub	x1,x1,FRAME_TITLE_H+FRAME_BORDER+24
.h:	mov	x2,AP_FEN_H
	cmp	x1,x2
	csel	x1,x2,x1,gt
	cmp	x1,320
	mov	x2,320
	csel	x1,x2,x1,lt
	adr	x0,ap_w
	stp	x4,x1,[x0]
	mov	x3,x1
	sub	x1,x1,AP_HEAD+AP_BAS+8		; lignes par colonne
	mov	x2,ROW_H
	udiv	x1,x1,x2
	adr	x0,ap_percol
	str	x1,[x0]
	mov	x0,x4
	mov	x1,x3
	epilogue

; l'ecran a change : la fenetre a la taille qu'il permet -> x0 = 1 si elle a
; change (le programme redessine ; pendant le dialogue d'Ecran, WEV_SCREEN est
; perdu : son applet appelle ceci)
ap_fit:
	prologue
	adr	x0,ap_w
	ldp	x2,x3,[x0]
	stp	x2,x3,[sp,-16]!
	bl	ap_height
	ldp	x2,x3,[sp],16
	cmp	x0,x2
	ccmp	x1,x3,0,eq
	b.eq	.same
	bl	ui_resize
	bl	ap_borne
	mov	x0,1
	epilogue
.same:	bl	ap_borne
	mov	x0,0
	epilogue

; les applets : nom (argument), libelle de la liste, titre, ouvrir,
; appliquer, icone (decalages depuis la table)
applets:
	dw	n_theme-applets, n_theme_l-applets, th_titre-applets, th_ouvrir-applets, th_appliquer-applets, n_theme-applets
	dw	n_clavier-applets, n_clavier_l-applets, cl_titre-applets, cl_ouvrir-applets, cl_appliquer-applets, n_clavier-applets
	dw	n_ecran-applets, ec_titre-applets, ec_titre-applets, ec_ouvrir-applets, ec_appliquer-applets, n_ecran-applets
	; (l'applet de l'ancien dock, masque : le panel l'a remplace)
	; dw	n_dockconf-applets, dk_titre-applets, dk_titre-applets, dk_ouvrir-applets, dk_appliquer-applets, n_dockconf-applets
	dw	n_panelconf-applets, pnl_titre-applets, pnl_titre-applets, pnl_ouvrir-applets, pnl_appliquer-applets, n_panelconf-applets
	dw	n_wifi-applets, wf_titre-applets, wf_titre-applets, wf_ouvrir-applets, wf_appliquer-applets, n_wifi-applets
	dw	n_remote-applets, ra_s_liste-applets, ra_titre-applets, ra_ouvrir-applets, ra_appliquer-applets, n_remote_ic-applets
	dw	n_sound-applets, sn_titre-applets, sn_titre-applets, sn_ouvrir-applets, sn_appliquer-applets, n_sound-applets
	dw	n_padconf-applets, gp_titre-applets, gp_titre-applets, gp_ouvrir-applets, gp_appliquer-applets, n_padconf-applets
	dw	n_preload-applets, pl_s_droite-applets, pl_titre-applets, pl_ouvrir-applets, pl_appliquer-applets, n_preload-applets
	dw	n_dotnet-applets, dn_s_liste-applets, dn_titre-applets, dn_ouvrir-applets, dn_appliquer-applets, n_dotnet-applets
	dw	n_pkgman-applets, sw_titre-applets, sw_titre-applets, sw_ouvrir-applets, sw_appliquer-applets, n_pkgman-applets
	dw	n_printconf-applets, pr_titre-applets, pr_titre-applets, pr_ouvrir-applets, pr_appliquer-applets, n_printconf-applets
n_theme		db 'theme',0
n_theme_l	db 'Th',0xC3,0xA8,'me',0
n_clavier	db 'clavier',0
n_clavier_l	db 'R',0xC3,0xA9,'gion',0
n_ecran		db 'ecran',0
n_panelconf	db 'panelconf',0
n_dock		db 'panel',0			; (la barre du bureau, relancee)
n_wifi		db 'wifi',0
n_remote	db 'remote',0
n_remote_ic	db 'terminal',0			; (icone du Terminal)
n_sound		db 'sound',0
n_padconf	db 'padconf',0
n_preload	db 'preload',0
n_dotnet	db 'dotnet',0
n_pkgman	db 'pkgman',0
n_printconf	db 'printconf',0
title		db 'R',0xC3,0xA9,'glages',0
s_crlf		db 13,10,0
s_net_topics	db 'korin.net.*',0
p_options	db 'SD0:/etc/options.txt',0
p_systeme	db 'SD0:/etc/system.ini',0
p_dockp		db 'SD0:/sys/panel',0
		align 4

segment readable writeable
courant		dd 0				; l'applet ouvert
side_first	dd 0				; la premiere ligne montree de la liste
side_mx		dd 0				; x de la souris (molette sur la liste ?)
kit_data
ui_data
applis_data
dockconf_data
panelconf_data
applet_data
th_data
cl_data
ec_data
dk_data
wf_data
rp_data
ra_data
sn_data
gp_data
pl_data
dn_data
pr_data
		align 8
ap_scr		rb 32
ap_w		dd 0				; taille de la fenetre (contigus)
ap_h		dd 0
