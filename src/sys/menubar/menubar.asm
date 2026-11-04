; menubar - la barre de menus du bureau Elegant (SD0:/sys/menubar, lancee
; par la session, comme le menubar d'Onyx ; docs/DESIGN-ELEGANT.md 6) :
;   * a gauche, le menu ARMPI (losange d'accent + nom) : A propos, les
;     categories d'applications (manifestes app.txt) qui ouvrent leur
;     sous-menu, les fenetres ouvertes ;
;   * a cote, le nom de l'application active (WEV_WINLIST) ;
;     si elle a publie des menus (ui_menu ; lus par ui_menu_get toutes les
;     200 ms et a chaque WEV_WINLIST, refaits quand le numero de serie
;     change) : un menu en gras a son nom (Quitter), puis ses menus ; un
;     choix lui est renvoye (ui_menu_command : WEV_MENU) ; un clic sur un
;     autre titre pendant qu'un menu est ouvert ouvre celui-la ;
;   * a droite, l'heure (decalage de SD0:/etc/clock.cfg, regle par
;     l'Horloge, plus le temps depuis le demarrage), l'etat du reseau (un
;     clic : l'adresse) et le haut-parleur (barre si la sortie principale
;     est muette ; etat de sound relu par AudioKit toutes les 200 ms) : un
;     clic ouvre le volet du son (soundmenu), un clic droit le menu
;     « Avance... » qui lance le Mixer (docs/SON.md 4.4).
; Fenetre sans cadre, translucide, au-dessus des autres, sans focus ; elle
; reserve le haut de l'ecran.
; A gauche du son, l'icone de PocketQI (une bulle et ses deux yeux), si
; l'assistante est installee : un clic ouvre sa bulle sous l'icone
; (SD0:/apps/pocketqi.app/main --popup <x>), un second clic la ferme
; (src/apps/pocketqi/README.md).
; Un menu Aide suit les menus de l'application active si son paquet a un
; fichier d'aide (SD0:/apps/<nom>.app/help_<langue>.md, sinon celui de
; l'autre langue) : « Aide de <nom> » l'ouvre dans le lecteur d'aide
; (SD0:/apps/help.app/main <fichier>), « Sommaire de l'aide » ouvre le
; lecteur sur son sommaire. Si l'application a deja un menu Aide (ou Help),
; ces deux lignes vont en tete du sien.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/netkit/netkit.inc'
include '../../inc/netd.inc'
include '../../inc/sound.inc'

MENU_W		= 220
ITEMS_MAX	= 4096
MB_MAX		= 10			; titres : ARMPI, l'application, ses menus
MB_PX		= 14			; taille du texte de la barre
MB_ITEMS	= 64			; lignes d'un menu d'application
MB_POLL		= 200			; ms entre deux lectures du menu actif
PL_MAX		= 64			; entrees de la liste des process (64 o)

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so
include '../../inc/applist.inc'
include '../../inc/wifidraw.inc'
include '../../inc/sounddraw.inc'

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,quit
	bl	ui_echelle			; hauteur : 30, 28, 26
	lsl	x0,x0,1
	mov	x1,30
	sub	x0,x1,x0
	adr	x1,bar_h
	str	x0,[x1]
	mov	x3,x0
	adr	x0,scr
	ldr	x2,[x0]
	mov	x0,0
	mov	x1,0
	adr	x4,title
	bl	tr_x4
	mov	x5,WF_FRAMELESS or WF_TOP or WF_NOFOCUS or WF_NOLIST or WF_LISTEN or WF_RESERVE or WF_ALPHA
	bl	ui_window_ex
	tbnz	x0,63,quit
	bl	applis_load
	bl	pq_probe
	bl	load_offset
	bl	menu_poll
	bl	help_probe
	bl	draw
.loop:	bl	periodic			; menu actif, heure
	cbz	x0,.wait
	bl	draw
.wait:	mov	x0,MB_POLL
	bl	ui_event
	cbz	x0,.loop
	cmp	x0,WEV_WINLIST
	b.eq	.wins
	cmp	x0,WEV_SCREEN
	b.eq	.screen
	cmp	x0,WEV_MOUSE
	b.ne	.loop
	adr	x9,ui_ev
	ldrsw	x0,[x9,EV_X]
	ldr	w1,[x9,EV_KIND]
	cmp	w1,1
	b.eq	.press
	bl	title_at			; survol des titres de menus
	add	x0,x0,1
	adr	x1,hov
	ldr	x2,[x1]
	cmp	x0,x2
	b.eq	.loop
	str	x0,[x1]
	bl	draw
	b	.loop
.press:	str	x0,[sp,-16]!
	bl	title_at
	ldr	x1,[sp],16
	tbnz	x0,63,.right
	bl	open_menus
	b	.redraw
.right:	adr	x2,pq_x				; PocketQI
	ldr	x2,[x2]
	sub	x3,x1,x2
	add	x3,x3,4
	cmp	x3,24
	b.hs	.snd
	bl	pq_click
	b	.loop
.snd:	adr	x2,snd_x			; haut-parleur
	ldr	x2,[x2]
	sub	x3,x1,x2
	add	x3,x3,4
	cmp	x3,24
	b.hs	.net
	bl	snd_click
	b	.loop
.net:	adr	x2,net_x			; icone du reseau
	ldr	x2,[x2]
	sub	x3,x1,x2
	cmp	x3,24
	b.hs	.loop
	bl	net_click
	b	.loop
.screen:					; nouvelle resolution : toute la largeur
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,.loop
	mov	x0,1				; (la barre est plus basse que le minimum)
	mov	x1,1
	bl	ui_min_size
	bl	ui_echelle			; hauteur : 30, 28, 26
	lsl	x0,x0,1
	mov	x1,30
	sub	x1,x1,x0
	adr	x0,bar_h
	str	x1,[x0]
	adr	x0,scr
	ldr	x0,[x0]
	bl	ui_resize
	b	.redraw
.wins:	bl	menu_poll			; (la fenetre active a pu changer)
	bl	help_probe
.redraw:
	bl	draw
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Menus
; ---------------------------------------------------------------------------

; x0 = libelle (texte) : l'ajoute aux libelles en construction (x19 : ecriture)
; -> x19 avance
add_text:
	ldrb	w1,[x0],1
	strb	w1,[x19],1
	cbnz	w1,add_text
	ret

; « icone<tab>texte » : x0 = icone, x1 = texte
add_icon:
	ldrb	w2,[x0],1
	cbz	w2,.tab
	strb	w2,[x19],1
	b	add_icon
.tab:	mov	w2,9
	strb	w2,[x19],1
	mov	x0,x1
	b	add_text

; « icone<tab>>texte » (entree a sous-menu) : x0 = icone, x2 = texte
add_icon_sub:
	ldrb	w1,[x0],1
	cbz	w1,.tab
	strb	w1,[x19],1
	b	add_icon_sub
.tab:	mov	w1,9
	strb	w1,[x19],1
	mov	w1,'>'
	strb	w1,[x19],1
	mov	x0,x2
	b	add_text

; le menu ARMPI : A propos, categories, fenetres ouvertes
armpi_menu:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,items
	adr	x0,s_bienvenue			; A propos (icone de Bienvenue)
	adr	x1,s_apropos
	bl	tr_x1
	bl	add_icon
	adr	x0,s_sep
	bl	add_text
	mov	x20,0				; une ligne par categorie : l'icone de sa
.c:	adr	x0,ca_n				; premiere application
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.cats
	mov	x21,0
.a:	adr	x0,ap_cat
	ldrb	w0,[x0,x21]
	cmp	x0,x20
	b.eq	.ic
	add	x21,x21,1
	b	.a
.ic:	adr	x0,ap_nom
	add	x0,x0,x21,lsl 5
	adr	x1,ca_nom
	mov	x2,CA_NOM
	madd	x1,x20,x2,x1
	bl	tr_x1
	mov	x2,x1				; « icone<tab>>categorie » : sous-menu
	bl	add_icon_sub
	add	x20,x20,1
	b	.c
.cats:	adr	x0,s_sep
	bl	add_text
	adr	x0,s_fenetres
	bl	tr_x0
	bl	add_text
	strb	wzr,[x19]
	mov	x0,4				; menu garde ouvert pendant ses sous-menus
	adr	x1,bar_h
	ldr	x1,[x1]
	sub	x1,x1,2
	adr	x2,items
	mov	x3,MENU_W
	mov	x4,3				; icones, garder ouvert (sous-menus)
	bl	ui_popup_ex
.choice:					; x0 = choix, x1 = y de sa ligne
	tbz	x0,63,.ok			; abandon : tout est ferme ; un clic
	cmn	x0,2				; dehors : garde (un autre titre ?)
	b.ne	.out
	adr	x3,pend_x
	stp	x1,x2,[x3]
	b	.out
.ok:	cbz	x0,.about
	mov	x21,x1				; (y de la ligne : le sous-menu s'y ouvre)
	sub	x0,x0,2				; categorie ?
	adr	x1,ca_n
	ldr	x1,[x1]
	cmp	x0,x1
	b.lo	.cat
	bl	windows_items			; fenetres ouvertes
	cbz	x0,.resume
	mov	x22,1
	b	.sub
.about:	adr	x0,s_bienvenue
	bl	app_find
	tbnz	x0,63,.out
	bl	app_launch
	b	.out
.cat:	mov	x20,x0				; les applications de la categorie
	adr	x19,items2
	mov	x9,0
	mov	x22,0
.s:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x9,x0
	b.hs	.sd
	adr	x0,ap_cat
	ldrb	w0,[x0,x9]
	cmp	x0,x20
	b.ne	.sn
	adr	x0,sub_map
	strb	w9,[x0,x22]
	add	x22,x22,1
	adr	x0,ap_nom
	add	x0,x0,x9,lsl 5
	adr	x1,ap_lib
	mov	x2,AP_LIB
	madd	x1,x9,x2,x1
	str	x9,[sp,-16]!
	bl	add_icon
	ldr	x9,[sp],16
.sn:	add	x9,x9,1
	b	.s
.sd:	strb	wzr,[x19]
	mov	x22,0				; (0 : applications, 1 : fenetres)
.sub:	mov	x0,4+MENU_W-6			; sous-menu a droite, sa premiere ligne
	sub	x1,x21,6			; en face de la ligne choisie
	adr	x2,items2
	mov	x3,220
	mov	x4,1
	cbz	x22,.subgo
	mov	x3,260
	mov	x4,0
.subgo:	bl	ui_popup_ex
	tbz	x0,63,.picked
	cmn	x0,2				; clic hors du sous-menu : le menu le
	b.ne	.resume				; traite (sur lui : un choix ; ailleurs :
	mov	x5,x1				; tout se ferme) ; Echap : retour au menu
	mov	x6,x2
	mov	x4,1+2+4+8
	bl	ui_popup_ex
	b	.choice
.resume:
	mov	x4,1+2+4
	bl	ui_popup_ex
	b	.choice
.picked:
	mov	x20,x0
	bl	ui_popup_close			; le menu se ferme
	cbnz	x22,.win
	adr	x1,sub_map
	ldrb	w0,[x1,x20]
	bl	app_launch
	b	.out
.win:	adr	x1,win_ids
	ldr	x0,[x1,x20,lsl 3]
	bl	ui_raise
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; items2 <- titres des fenetres ouvertes (win_ids : leurs id) -> x0 = nombre
windows_items:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,items2
	mov	x20,0				; premiere de la page
	mov	x22,0				; fenetres listees
.page:	mov	x0,x20
	adr	x1,wlist
	bl	ui_winlist
	cmp	x0,x20
	b.le	.done
	sub	x21,x0,x20
	cmp	x21,WL_PER_MSG
	mov	x1,WL_PER_MSG
	csel	x21,x1,x21,gt
	adr	x9,wlist
.e:	cbz	x21,.np
	cmp	x22,AP_MAX
	b.hs	.done
	ldr	x0,[x9,WL_ID]
	adr	x1,win_ids
	str	x0,[x1,x22,lsl 3]
	add	x22,x22,1
	add	x0,x9,WL_TITLE
	str	x9,[sp,-16]!
	bl	add_text
	ldr	x9,[sp],16
	add	x9,x9,WL_SIZE
	sub	x21,x21,1
	b	.e
.np:	add	x20,x20,WL_PER_MSG
	b	.page
.done:	strb	wzr,[x19]
	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = titre clique (0 : ARMPI, 1 : l'application, 2... : ses menus) : ouvre
; son menu ; un clic sur un autre titre pendant qu'il est ouvert ouvre
; celui-la (sur le meme : il se ferme)
open_menus:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
.m:	adr	x0,pend_y			; (pas de clic dehors garde)
	mov	x1,-1
	str	x1,[x0]
	adr	x0,open_menu
	add	x1,x19,1
	str	x1,[x0]
	bl	draw
	cbnz	x19,.app
	bl	armpi_menu
	b	.after
.app:	mov	x0,x19
	bl	app_menu
.after:	adr	x0,open_menu
	str	xzr,[x0]
	adr	x0,pend_y			; menu ferme par un clic dans la barre,
	ldr	x1,[x0]				; sur un autre titre ?
	tbnz	x1,63,.out
	adr	x2,bar_h
	ldr	x2,[x2]
	cmp	x1,x2
	b.ge	.out
	adr	x0,pend_x
	ldr	x0,[x0]
	bl	title_at
	tbnz	x0,63,.out
	cmp	x0,x19
	b.eq	.out
	mov	x19,x0
	b	.m
.out:	ldr	x19,[sp],16
	epilogue

; x0 = titre (1 : menu au nom de l'application, 2... : ses menus) : le menu
; deroulant ; un choix part au programme (ui_menu_command)
app_menu:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	adr	x20,items			; ecriture des libelles
	mov	x21,0				; lignes
	mov	x22,0				; largeur max (libelle + raccourci)
	adr	x0,help_t			; le menu Aide : ses lignes d'abord
	ldr	x0,[x0]
	cmp	x19,x0
	b.eq	.help
	cmp	x19,1
	b.ne	.spec
	adr	x0,s_quitter			; menu de l'application : Quitter (-1)
	bl	tr_x0
	mov	x1,-1
	bl	mb_item
	b	.show
.help:	adr	x0,s_aide_de			; « Aide de <nom> » (-10)
	bl	tr_x0
	adr	x1,hbuf
	mov	x2,x1
.hc:	ldrb	w3,[x0],1
	cbz	w3,.hn
	strb	w3,[x2],1
	b	.hc
.hn:	mov	w3,' '
	strb	w3,[x2],1
	adr	x1,help_name
.hn2:	ldrb	w3,[x1],1
	strb	w3,[x2],1
	cbnz	w3,.hn2
	adr	x0,hbuf
	mov	x1,-10
	bl	mb_item
	adr	x0,s_sommaire			; « Sommaire de l'aide » (-11)
	bl	tr_x0
	mov	x1,-11
	bl	mb_item
	adr	x0,mb_n				; titre ajoute par la barre : rien de plus
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.show
	mov	w0,'-'				; menu Aide de l'application : un trait,
	strb	w0,[x20],1			; puis ses lignes
	strb	wzr,[x20],1
	adr	x0,mb_ids
	mov	x1,-2
	str	x1,[x0,x21,lsl 3]
	add	x21,x21,1
.spec:	adr	x0,mb_ptr			; lignes apres « M<titre> »
	ldr	x23,[x0,x19,lsl 3]
	adr	x24,mb_end
	ldr	x24,[x24]
.skip:	ldrb	w0,[x23],1
	cbnz	w0,.skip
.line:	cmp	x23,x24
	b.hs	.show
	ldrb	w0,[x23]
	cmp	w0,'M'				; menu suivant : fini
	b.eq	.show
	cmp	x21,MB_ITEMS
	b.hs	.show
	cmp	w0,'-'
	b.ne	.item
	strb	w0,[x20],1			; separateur
	strb	wzr,[x20],1
	adr	x0,mb_ids
	mov	x1,-2
	str	x1,[x0,x21,lsl 3]
	add	x21,x21,1
	b	.next
.item:	cmp	w0,'I'				; « I<id><tab><libelle><tab><raccourci> »
	b.ne	.next
	add	x0,x23,1
	mov	x1,0
.d:	ldrb	w2,[x0]
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.did
	mov	x3,10
	madd	x1,x1,x3,x2
	add	x0,x0,1
	b	.d
.did:	ldrb	w2,[x0]			; marques : « d » grisee, « c » cochee
	mov	w3,1				; (le menu les recoit en tete du libelle :
	cmp	w2,'d'				; 0x01, 0x02 : ui_popup_ex)
	b.eq	.mk
	mov	w3,2
	cmp	w2,'c'
	b.ne	.nomk
.mk:	strb	w3,[x20],1
	add	x0,x0,1
	b	.did
.nomk:	cmp	w2,9
	b.ne	.lab
	add	x0,x0,1
.lab:	bl	mb_item
.next:	ldrb	w0,[x23],1			; ligne suivante
	cbnz	w0,.next
	b	.line
.show:	strb	wzr,[x20]
	cbz	x21,.out
	add	x3,x22,60			; largeur : 180 a 420
	mov	x0,180
	cmp	x3,x0
	csel	x3,x0,x3,lo
	mov	x0,420
	cmp	x3,x0
	csel	x3,x0,x3,hi
	adr	x0,mb_x0			; sous le titre (comme le menu ARMPI)
	ldr	x0,[x0,x19,lsl 3]
	sub	x0,x0,2
	adr	x1,bar_h
	ldr	x1,[x1]
	sub	x1,x1,2
	adr	x2,items
	mov	x4,16				; « texte<tab>raccourci »
	bl	ui_popup_ex
	tbz	x0,63,.pick
	cmn	x0,2				; clic dehors : garde (un autre titre ?)
	b.ne	.out
	adr	x3,pend_x
	stp	x1,x2,[x3]
	b	.out
.pick:	adr	x1,mb_ids
	ldr	x0,[x1,x0,lsl 3]
	cmn	x0,2
	b.eq	.out
	cmn	x0,10				; aide de l'application
	b.ne	.pk2
	adr	x0,p_help
	adr	x1,help_path
	sys	SYS_SPAWN
	b	.out
.pk2:	cmn	x0,11				; sommaire de l'aide
	b.ne	.pk3
	adr	x0,p_help
	mov	x1,0
	sys	SYS_SPAWN
	b	.out
.pk3:
	adr	x1,mb_serial
	ldr	x1,[x1]
	bl	ui_menu_command
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (app_menu) x0 = « libelle[<tab>raccourci] » (fin : NUL), x1 = id : ajoute
; la ligne (x20 ecriture, x21 lignes, x22 largeur max ; x25, x26 detruits)
mb_item:
	prologue
	adr	x2,mb_ids
	str	x1,[x2,x21,lsl 3]
	add	x21,x21,1
	mov	x25,x20				; libelle
.l:	ldrb	w1,[x0]
	cbz	w1,.lend
	cmp	w1,9
	b.eq	.lend
	strb	w1,[x20],1
	add	x0,x0,1
	b	.l
.lend:	strb	wzr,[x20]
	mov	x26,x0
	mov	x0,x25
	mov	x1,MB_PX
	adr	x2,ui_font
	bl	ui_text_width
	mov	x25,x0				; largeur du libelle
	ldrb	w1,[x26]
	cbz	w1,.w
	add	x26,x26,1			; raccourci (vide : aucun)
	ldrb	w1,[x26]
	cbz	w1,.w
	mov	w1,9
	strb	w1,[x20],1
	str	x20,[sp,-16]!
.s:	ldrb	w1,[x26],1
	cbz	w1,.send
	cmp	w1,9
	b.eq	.send
	strb	w1,[x20],1
	b	.s
.send:	strb	wzr,[x20]
	ldr	x0,[sp],16
	mov	x1,MB_PX
	adr	x2,ui_font
	bl	ui_text_width
	add	x25,x25,x0
	add	x25,x25,24			; ecart libelle - raccourci
.w:	cmp	x25,x22
	csel	x22,x25,x22,hi
	add	x20,x20,1			; (apres le NUL)
	epilogue

; x0 = x dans la barre -> x0 = titre dessous (0 : ARMPI ; -1 : aucun)
title_at:
	adr	x1,mb_shown
	ldr	x1,[x1]
	adr	x3,mb_x0
	adr	x4,mb_x1
	mov	x2,0
.t:	cmp	x2,x1
	b.hs	.none
	ldr	x5,[x3,x2,lsl 3]
	cmp	x0,x5
	b.lt	.n
	ldr	x5,[x4,x2,lsl 3]
	cmp	x0,x5
	b.ge	.n
	mov	x0,x2
	ret
.n:	add	x2,x2,1
	b	.t
.none:	mov	x0,-1
	ret

; toutes les MB_POLL ms : le menu de la fenetre active ; chaque seconde :
; l'heure (l'Horloge a pu etre reglee) -> x0 = 1 si la barre est a redessiner
periodic:
	prologue
	str	x19,[sp,-16]!
	sys	SYS_TIME_MS
	mov	x19,x0
	adr	x1,t_poll
	ldr	x2,[x1]
	sub	x2,x19,x2
	cmp	x2,MB_POLL
	b.lo	.none
	str	x19,[x1]
	bl	menu_poll
	cbz	x0,.snd				; un autre menu : peut-etre une autre
	bl	help_probe			; application (a redessiner)
	bl	snd_poll
	b	.redraw
.snd:	bl	snd_poll			; (le son a change : a redessiner)
	cbz	x0,.clk
.redraw:
	adr	x1,t_clock			; (l'heure sera relue a son tour)
	mov	x0,1
	b	.out
.clk:	adr	x1,t_clock
	ldr	x2,[x1]
	sub	x2,x19,x2
	cmp	x2,1000
	b.lo	.out
	str	x19,[x1]
	bl	load_offset
	mov	x0,1
	b	.out
.none:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; lit le menu de la fenetre active (ui_menu_get) -> x0 = 1 s'il a change
; (titres refaits par menu_parse)
menu_poll:
	prologue
	adr	x0,spec_buf
	adr	x1,app_title
	adr	x2,mb_serial
	ldr	x2,[x2]
	bl	ui_menu_get
	adr	x9,mb_serial
	ldr	x3,[x9]
	cmp	x0,x3
	b.eq	.same
	str	x0,[x9]
	adr	x9,spec_buf			; fin du texte
	add	x9,x9,x2
	adr	x1,mb_end
	str	x9,[x1]
	bl	menu_parse
	mov	x0,1
	b	.out
.same:	mov	x0,0
.out:	epilogue

; texte des menus (spec_buf) -> mb_ptr (titres ; [1] : le nom de
; l'application), mb_n ; chaque ligne devient une chaine (NUL a la place de
; « \n », CR retire)
menu_parse:
	adr	x0,mb_n
	mov	x1,1
	str	x1,[x0]
	adr	x0,mb_serial
	ldr	x0,[x0]
	cbz	x0,.ret				; pas de menu : ARMPI seul
	adr	x2,mb_ptr
	adr	x1,app_title
	str	x1,[x2,8]
	mov	x3,2
	adr	x0,spec_buf
	adr	x4,mb_end
	ldr	x4,[x4]
.line:	cmp	x0,x4
	b.hs	.done
	ldrb	w1,[x0]
	cmp	w1,'M'
	b.ne	.eol
	cmp	x3,MB_MAX
	b.hs	.eol
	add	x1,x0,1
	str	x1,[x2,x3,lsl 3]
	add	x3,x3,1
.eol:	cmp	x0,x4
	b.hs	.done
	ldrb	w1,[x0]
	cbz	w1,.nl
	cmp	w1,10
	b.eq	.nl
	cmp	w1,13
	b.ne	.c
	strb	wzr,[x0]
.c:	add	x0,x0,1
	b	.eol
.nl:	strb	wzr,[x0],1
	b	.line
.done:	adr	x0,mb_n
	str	x3,[x0]
.ret:	ret

; (draw) x0 = fin du menu ARMPI, x1 = ligne de base : le nom de
; l'application (gras) puis ses menus ; zones de clic dans mb_x0, mb_x1
draw_titles:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	add	x19,x0,10			; x du texte
	mov	x20,x1
	mov	x21,1				; titre
	adr	x0,ui_w				; limite : avant l'heure et le reseau
	ldr	x22,[x0]
	sub	x22,x22,158
	adr	x0,help_t			; (le menu Aide : retrouve ci-dessous)
	mov	x1,-1
	str	x1,[x0]
.t:	adr	x0,mb_n
	ldr	x0,[x0]
	cmp	x21,x0
	b.lo	.menu
	b.hi	.done
	adr	x0,help_on			; apres les menus : Aide, si l'application
	ldr	x0,[x0]				; en a une et pas deja un menu Aide
	cbz	x0,.done
	adr	x0,help_t
	ldr	x0,[x0]
	tbz	x0,63,.done
	cmp	x21,MB_MAX
	b.hs	.done
	adr	x0,s_aide
	bl	tr_x0
	mov	x23,x0
	adr	x24,ui_font
	b	.w
.menu:	adr	x0,mb_ptr
	ldr	x23,[x0,x21,lsl 3]
	adr	x24,ui_font
	cmp	x21,1
	b.ne	.isaid
	adr	x24,ui_font_b			; le nom de l'application, en gras
	b	.w
.isaid:	mov	x0,x23				; son menu Aide (ou Help) ?
	bl	is_help_title
	cbz	x0,.w
	adr	x0,help_on
	ldr	x0,[x0]
	cbz	x0,.w
	adr	x0,help_t
	str	x21,[x0]
.w:	mov	x0,x23
	mov	x1,MB_PX
	mov	x2,x24
	bl	ui_text_width
	mov	x25,x0
	cmp	x25,220
	mov	x0,220
	csel	x25,x0,x25,hi
	add	x0,x19,x25
	cmp	x0,x22
	b.gt	.full				; plus de place
	adr	x0,mb_n				; (le titre Aide ajoute : il est montre)
	ldr	x0,[x0]
	cmp	x21,x0
	b.ne	.room
	adr	x0,help_t
	str	x21,[x0]
.room:
	sub	x0,x19,8			; zone : 8 px de chaque cote
	adr	x1,mb_x0
	str	x0,[x1,x21,lsl 3]
	add	x0,x19,x25
	add	x0,x0,8
	adr	x1,mb_x1
	str	x0,[x1,x21,lsl 3]
	bl	ui_jetons			; ouvert, survole
	mov	x26,x0
	add	x1,x21,1
	ldr	w5,[x26,TJ_ACCENT_CLAIR]
	adr	x0,open_menu
	ldr	x0,[x0]
	cmp	x0,x1
	b.eq	.box
	ldr	w5,[x26,TJ_CREUX]
	adr	x0,hov
	ldr	x0,[x0]
	cmp	x0,x1
	b.ne	.txt
.box:	mov	w6,w5
	sub	x0,x19,8
	mov	x1,4
	add	x2,x25,16
	adr	x3,bar_h
	ldr	x3,[x3]
	sub	x3,x3,8
	mov	x4,5
	bl	ui_rbox
.txt:	mov	x0,x23
	mov	x1,x19
	mov	x2,x20
	ldr	w3,[x26,TJ_TEXTE1]
	mov	x4,MB_PX
	mov	x5,x24
	add	x6,x25,4
	bl	ui_text
	add	x19,x19,x25
	add	x19,x19,18
	add	x21,x21,1
	b	.t
.full:	adr	x0,help_t			; menu Aide pas montre : sans effet
	ldr	x1,[x0]
	cmp	x1,x21
	b.lo	.done
	mov	x1,-1
	str	x1,[x0]
.done:	adr	x0,mb_shown
	str	x21,[x0]
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; un clic sur l'icone du reseau : avec le Wi-Fi, le menu Wi-Fi (wifimenu,
; sous l'icone ; un second clic le ferme) ; sinon l'adresse
net_click:
	prologue
	adr	x0,wst
	netcall	wifi_status
	adr	x0,wst
	ldr	w0,[x0]
	cmp	w0,WIFI_ST_READY
	b.lo	.adresse
	adr	x0,net_x
	ldr	x0,[x0]
	adr	x1,argbuf
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,p_wifimenu
	adr	x1,argbuf
	sys	SYS_SPAWN
	epilogue
.adresse:
	bl	net_menu
	epilogue

; -> x0 = 1 si l'etat du son a change (generation de sound)
snd_poll:
	prologue
	bl	ak_state
	adr	x1,snd_st
	str	x0,[x1]
	mov	x1,-1				; (sans service : -1)
	cbz	x0,.g
	ldr	x1,[x0,SS_GEN]
.g:	adr	x2,snd_gen
	ldr	x3,[x2]
	str	x1,[x2]
	cmp	x1,x3
	cset	x0,ne
	epilogue

; (draw) haut-parleur en x23 : barre si la principale est muette ; rien sans
; service sound
snd_icon:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,snd_st
	ldr	x19,[x0]
	cbz	x19,.out
	mov	x20,0				; muet ?
	add	x1,x19,SS_OUTS
	mov	x2,SS_OUT_MAX
	ldr	x3,[x19,SS_MAIN]
.o:	ldr	w4,[x1,SO_ID]
	cmp	x4,x3
	b.ne	.n
	ldr	w20,[x1,SO_MUTE]
.n:	add	x1,x1,64
	subs	x2,x2,1
	b.ne	.o
	bl	ui_jetons
	ldr	w3,[x0,TJ_TEXTE1]
	ldr	w4,[x0,TJ_TEXTE3]
	adr	x0,bar_h
	ldr	x1,[x0]
	lsr	x1,x1,1
	sub	x1,x1,7
	mov	x0,x23
	mov	x2,x20
	bl	sd_speaker
.out:	ldp	x19,x20,[sp],16
	epilogue

; un clic sur le haut-parleur : le volet du son (soundmenu, sous l'icone ; un
; second clic le ferme) ; clic droit : « Avance... » -> le Mixer
snd_click:
	prologue
	adr	x0,ui_ev
	ldr	w0,[x0,EV_CODE]			; boutons : 2 droit
	tbnz	w0,1,.right
	adr	x0,snd_x
	ldr	x0,[x0]
	adr	x1,argbuf
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,p_soundmenu
	adr	x1,argbuf
	sys	SYS_SPAWN
	epilogue
.right:	adr	x0,s_avance
	adr	x1,items
	bl	tr_block
	adr	x0,snd_x
	ldr	x0,[x0]
	sub	x0,x0,150
	adr	x1,bar_h
	ldr	x1,[x1]
	sub	x1,x1,2
	adr	x2,items
	mov	x3,180
	bl	ui_popup
	cbnz	x0,.out				; (0 : la ligne ; -1 : rien)
	adr	x0,p_mixer
	mov	x1,0
	sys	SYS_SPAWN
.out:	epilogue

; PocketQI installee ? (son executable) -> pq_on ; sinon pas d'icone, et
; sa zone de clic hors de la barre
pq_probe:
	prologue
	adr	x1,pq_x
	mov	x0,-100
	str	x0,[x1]
	adr	x0,p_pocketqi
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	sys	SYS_CLOSE
	adr	x0,pq_on
	mov	x1,1
	str	x1,[x0]
.out:	epilogue

; (draw) l'icone de PocketQI en x23 : une bulle pleine, sa pointe en bas a
; gauche, et les deux yeux de Pocket dans la couleur de la barre
pq_icon:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	ui_jetons
	ldr	w19,[x0,TJ_TEXTE1]
	ldr	w20,[x0,TJ_BARRE]
	adr	x0,bar_h
	ldr	x21,[x0]
	lsr	x21,x21,1
	sub	x21,x21,7			; haut de l'icone
	mov	x0,x23				; la bulle
	mov	x1,x21
	mov	x2,16
	mov	x3,11
	mov	x4,4
	mov	w5,w19
	mov	w6,w19
	bl	ui_rbox
	add	x0,x23,3			; la pointe
	add	x1,x21,10
	mov	x2,4
	mov	x3,2
	mov	w4,w19
	bl	ui_fill
	add	x0,x23,3
	add	x1,x21,12
	mov	x2,2
	mov	x3,2
	mov	w4,w19
	bl	ui_fill
	add	x0,x23,4			; les yeux
	add	x1,x21,3
	mov	x2,3
	mov	x3,5
	mov	w4,w20
	bl	ui_fill
	add	x0,x23,9
	add	x1,x21,3
	mov	x2,3
	mov	x3,5
	mov	w4,w20
	bl	ui_fill
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; un clic sur l'icone de PocketQI : sa bulle sous l'icone (« --popup x » ; un
; second clic : une seconde bulle ferme la premiere et s'arrete)
pq_click:
	prologue
	adr	x1,argbuf
	adr	x2,s_popup
.c:	ldrb	w3,[x2],1
	cbz	w3,.n
	strb	w3,[x1],1
	b	.c
.n:	adr	x0,pq_x
	ldr	x0,[x0]
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,p_pocketqi
	adr	x1,argbuf
	sys	SYS_SPAWN
	epilogue

; menu du reseau : l'adresse, l'etat
net_menu:
	prologue
	str	x19,[sp,-16]!
	bl	net_text
	adr	x19,items
	adr	x0,s_reseau
	bl	tr_x0
	bl	add_text
	sub	x19,x19,1
	adr	x0,netbuf
	bl	add_text
	strb	wzr,[x19]
	adr	x0,net_x
	ldr	x0,[x0]
	sub	x0,x0,160
	adr	x1,bar_h
	ldr	x1,[x1]
	sub	x1,x1,2
	adr	x2,items
	mov	x3,200
	bl	ui_popup
	ldr	x19,[sp],16
	epilogue

; x0 = titre d'un menu de l'application -> x0 = 1 si c'est son menu Aide
is_help_title:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x1,s_aide
	bl	str_eq
	cbnz	x0,.out
	mov	x0,x19
	adr	x1,s_help
	bl	str_eq
	cbnz	x0,.out
	adr	x0,s_aide
	bl	tr_x0
	mov	x1,x0
	mov	x0,x19
	bl	str_eq
.out:	ldr	x19,[sp],16
	epilogue

; l'aide de l'application active : son process (WL_PID, nom dans la liste
; des process : « letters » pour SD0:/apps/letters.app/main), puis
; SD0:/apps/<nom>.app/help_<langue>.md, ou help_en.md, ou help_fr.md ->
; help_on, help_path, help_name ; refait seulement quand le process actif
; change
help_probe:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,0				; premiere entree de la page
.page:	mov	x0,x19
	adr	x1,wlist
	bl	ui_winlist
	cmp	x0,x19
	b.le	.none
	sub	x20,x0,x19
	cmp	x20,WL_PER_MSG
	mov	x1,WL_PER_MSG
	csel	x20,x1,x20,gt
	adr	x2,wlist
.e:	cbz	x20,.np
	ldr	w3,[x2,WL_STATE]
	tbnz	w3,0,.found
	add	x2,x2,WL_SIZE
	sub	x20,x20,1
	b	.e
.np:	add	x19,x19,WL_PER_MSG
	b	.page
.found:	ldr	w19,[x2,WL_PID]
	b	.pid
.none:	mov	x19,0				; (aucune fenetre active)
.pid:	adr	x0,help_pid
	ldr	x1,[x0]
	cmp	x1,x19
	b.eq	.out
	str	x19,[x0]
	adr	x0,help_on
	str	xzr,[x0]
	cbz	x19,.out
	adr	x0,plist			; son nom
	mov	x1,PL_MAX
	sys	SYS_PROCLIST
	cmp	x0,0
	b.le	.out
	mov	x20,x0
	adr	x21,plist
.p:	ldr	x0,[x21]
	cmp	x0,x19
	b.eq	.named
	add	x21,x21,64
	subs	x20,x20,1
	b.ne	.p
	b	.out
.named:	add	x21,x21,24
	strb	wzr,[x21,31]
	mov	x0,x21				; une application installee ?
	bl	app_find
	tbnz	x0,63,.out
	adr	x1,ap_lib			; son nom affiche
	mov	x2,AP_LIB
	madd	x1,x0,x2,x1
	adr	x0,help_name
	mov	x2,AP_LIB
	bl	str_copy_n
	adr	x0,help_path			; SD0:/apps/<nom>.app/help_
	adr	x1,ap_path
	bl	str_cat
	mov	x1,x21
	bl	str_cat
	adr	x1,s_help_f
	bl	str_cat
	adr	x1,help_tail
	str	x0,[x1]
	bl	lang_get
	bl	help_try
	cbnz	x0,.yes
	adr	x0,s_en
	bl	help_try
	cbnz	x0,.yes
	adr	x0,s_fr
	bl	help_try
	cbz	x0,.out
.yes:	adr	x0,help_on
	mov	x1,1
	str	x1,[x0]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (help_probe) x0 = langue : help_path <- ...help_<langue>.md -> x0 = 1 s'il
; existe
help_try:
	prologue
	mov	x1,x0
	adr	x0,help_tail
	ldr	x0,[x0]
	bl	str_cat
	adr	x1,s_md
	bl	str_cat
	adr	x0,help_path
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.no
	sys	SYS_CLOSE
	mov	x0,1
	epilogue
.no:	mov	x0,0
	epilogue

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------
draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	ui_jetons
	mov	x21,x0
	adr	x0,bar_h
	ldr	x22,[x0]
	ldr	w4,[x21,TJ_BARRE]		; fond translucide, trait en bas
	mov	x0,0
	mov	x1,0
	adr	x2,ui_w
	ldr	x2,[x2]
	sub	x3,x22,1
	bl	ui_fill
	ldr	w4,[x21,TJ_LIGNE]
	mov	x0,0
	sub	x1,x22,1
	adr	x2,ui_w
	ldr	x2,[x2]
	mov	x3,1
	bl	ui_fill
	ldr	w20,[x21,TJ_TEXTE1]		; menu ARMPI : survol, ouvert
	adr	x0,str_armpi
	mov	x1,MB_PX
	adr	x2,ui_font_b
	bl	ui_text_width
	add	x23,x0,31+8			; fin de la zone
	adr	x1,mb_x1			; (titre 0 : de 0 a la fin de la zone)
	str	x23,[x1]
	adr	x1,mb_x0
	str	xzr,[x1]
	adr	x1,mb_shown
	mov	x0,1
	str	x0,[x1]
	ldr	w5,[x21,TJ_ACCENT_CLAIR]
	adr	x0,open_menu
	ldr	x0,[x0]
	cmp	x0,1
	b.eq	.box
	ldr	w5,[x21,TJ_CREUX]
	adr	x0,hov
	ldr	x0,[x0]
	cmp	x0,1
	b.ne	.logo
.box:	mov	w6,w5
	mov	x0,6
	mov	x1,4
	sub	x2,x23,6
	sub	x3,x22,8
	mov	x4,5
	bl	ui_rbox
.logo:	lsr	x24,x22,1			; ligne de base : milieu + 5
	add	x24,x24,5
	adr	x0,s_losange
	mov	x1,12
	mov	x2,x24
	ldr	w3,[x21,TJ_ACCENT]
	mov	x4,MB_PX+2
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	adr	x0,str_armpi
	mov	x1,31
	mov	x2,x24
	mov	w3,w20
	mov	x4,MB_PX
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	adr	x0,mb_serial			; menus de l'application active ?
	ldr	x0,[x0]
	cbz	x0,.name
	mov	x0,x23
	mov	x1,x24
	bl	draw_titles
	b	.right
.name:	bl	active_title			; sinon, le titre de la fenetre active
	cbz	x0,.right
	mov	x19,x0
	add	x1,x23,10
	mov	x2,x24
	mov	w3,w20
	mov	x4,MB_PX
	adr	x5,ui_font_b
	mov	x6,320
	bl	ui_text
	mov	x0,x19				; puis le menu Aide (draw_titles : rien
	mov	x1,MB_PX			; d'autre, mb_n vaut 1)
	adr	x2,ui_font_b
	bl	ui_text_width
	mov	x1,320
	cmp	x0,x1
	csel	x0,x1,x0,hi
	add	x0,x0,x23
	add	x0,x0,18
	mov	x1,x24
	bl	draw_titles
.right:	bl	clock_text			; heure, a droite
	adr	x0,clockbuf
	mov	x1,MB_PX
	adr	x2,ui_font_b
	bl	ui_text_width
	adr	x1,ui_w
	ldr	x1,[x1]
	sub	x1,x1,x0
	sub	x23,x1,12
	adr	x0,clockbuf
	mov	x1,x23
	mov	x2,x24
	mov	w3,w20
	mov	x4,MB_PX
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	sub	x23,x23,14+16			; reseau : icone 16 x 14
	adr	x0,net_x
	str	x23,[x0]
	bl	net_icon
	sub	x23,x23,12+16			; son : haut-parleur 16 x 14
	adr	x0,snd_x
	str	x23,[x0]
	bl	snd_icon
	adr	x0,pq_on			; PocketQI : bulle 16 x 14
	ldr	x0,[x0]
	cbz	x0,.upd
	sub	x23,x23,12+16
	adr	x0,pq_x
	str	x23,[x0]
	bl	pq_icon
.upd:	bl	ui_update_all
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (draw) icone du reseau en x23, milieu de la barre : filaire (relie), ou
; barres eteintes et croix (pas de reseau)
net_icon:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,netinfo
	netcall	net_info
	adr	x0,netinfo
	ldr	w19,[x0]			; adresse IPv4 (0 : aucune)
	bl	ui_jetons
	ldr	w20,[x0,TJ_TEXTE1]
	ldr	w21,[x0,TJ_TEXTE3]
	adr	x0,bar_h
	ldr	x0,[x0]
	lsr	x0,x0,1
	sub	x22,x0,7			; haut de l'icone
	adr	x0,wst				; Wi-Fi : barres selon le signal
	netcall	wifi_status
	adr	x0,nact
	netcall	net_active
	adr	x0,nact
	ldr	w0,[x0,8]			; type de l'interface active
	cmp	w0,NIF_WIFI
	b.eq	.wifi
	cbnz	w19,.wired			; Ethernet, USB : filaire
.wifi:	adr	x0,wst
	ldr	w1,[x0]
	cmp	w1,WIFI_ST_CONNECTED
	b.ne	.off
	ldr	w0,[x0,4]
	bl	wd_level
	mov	x2,x0
	mov	x0,x23
	mov	x1,x22
	mov	w3,w20
	mov	w4,w21
	bl	wd_bars
	b	.out
.wired:
	add	x0,x23,5			; filaire : une boite en haut, deux en bas,
	mov	x1,x22				; reliees
	mov	x2,6
	mov	x3,5
	mov	x4,1
	mov	w5,w20
	mov	w6,w20
	bl	ui_rbox
	add	x0,x23,7
	add	x1,x22,5
	mov	x2,2
	mov	x3,3
	mov	w4,w20
	bl	ui_fill
	add	x0,x23,2
	add	x1,x22,7
	mov	x2,12
	mov	x3,1
	mov	w4,w20
	bl	ui_fill
	add	x0,x23,2
	add	x1,x22,7
	mov	x2,1
	mov	x3,3
	mov	w4,w20
	bl	ui_fill
	add	x0,x23,13
	add	x1,x22,7
	mov	x2,1
	mov	x3,3
	mov	w4,w20
	bl	ui_fill
	mov	x0,x23
	add	x1,x22,9
	mov	x2,5
	mov	x3,5
	mov	x4,1
	mov	w5,w20
	mov	w6,w20
	bl	ui_rbox
	add	x0,x23,11
	add	x1,x22,9
	mov	x2,5
	mov	x3,5
	mov	x4,1
	mov	w5,w20
	mov	w6,w20
	bl	ui_rbox
	b	.out
.off:	mov	x19,0				; barres de 4, 7, 10, 13, eteintes
.b:	mov	x0,4
	mov	x1,3
	madd	x2,x19,x1,x0			; hauteur
	add	x0,x22,13
	sub	x1,x0,x2
	lsl	x0,x19,2
	add	x0,x23,x0
	mov	x3,x2
	mov	x2,3
	mov	w4,w21
	bl	ui_fill
	add	x19,x19,1
	cmp	x19,4
	b.lo	.b
	adr	x0,s_croix
	add	x1,x23,11
	add	x2,x22,15
	mov	w3,w20
	mov	x4,9
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = titre de la fenetre active (0 : aucune)
active_title:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0				; premiere entree de la page
.page:	mov	x0,x19
	adr	x1,wlist
	bl	ui_winlist
	cmp	x0,x19
	b.le	.none
	sub	x20,x0,x19			; entrees dans cette page
	cmp	x20,WL_PER_MSG
	mov	x1,WL_PER_MSG
	csel	x20,x1,x20,gt
	adr	x2,wlist
.e:	cbz	x20,.np
	ldr	w3,[x2,WL_STATE]
	tbnz	w3,0,.found
	add	x2,x2,WL_SIZE
	sub	x20,x20,1
	b	.e
.np:	add	x19,x19,WL_PER_MSG
	b	.page
.found:	add	x0,x2,WL_TITLE
	b	.out
.none:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; decalage de l'heure (secondes), garde par l'Horloge
load_offset:
	prologue
	str	x19,[sp,-16]!
	adr	x0,offset
	str	xzr,[x0]
	adr	x0,p_cfg
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,cfgbuf
	mov	x2,31
	sys	SYS_READ
	mov	x2,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x2,0
	b.le	.out
	adr	x0,cfgbuf
	strb	wzr,[x0,x2]
	bl	parse_dec
	adr	x1,offset
	str	x0,[x1]
.out:	ldr	x19,[sp],16
	epilogue

; clockbuf <- « HH:MM »
clock_text:
	prologue
	sys	SYS_TIME_MS
	mov	x1,1000
	udiv	x0,x0,x1
	adr	x1,offset
	ldr	x1,[x1]
	add	x0,x0,x1
	movl	x1,86400
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	mov	x1,3600
	udiv	x2,x0,x1			; heures
	msub	x0,x2,x1,x0
	mov	x1,60
	udiv	x3,x0,x1			; minutes
	adr	x0,clockbuf
	mov	x1,x2
	bl	two_digits
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,x3
	bl	two_digits
	strb	wzr,[x0]
	epilogue

; x0 = destination, x1 = 0..99 -> deux chiffres, x0 avance (x3 preserve)
two_digits:
	mov	x4,10
	udiv	x5,x1,x4
	msub	x6,x5,x4,x1
	add	w5,w5,'0'
	add	w6,w6,'0'
	strb	w5,[x0],1
	strb	w6,[x0],1
	ret

; netbuf <- adresse IPv4 (« a.b.c.d »), ou « hors ligne »
net_text:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,netinfo
	netcall	net_info
	adr	x0,netinfo
	ldr	w19,[x0]
	cbz	w19,.off
	adr	x20,netbuf
	strb	wzr,[x20]
	mov	x2,0				; (adresse : octet de poids fort en tete)
.b:	str	x2,[sp,-16]!
	mov	x3,24
	sub	x3,x3,x2,lsl 3
	lsrv	w0,w19,w3
	and	x0,x0,0xFF
	mov	x1,x20
	bl	fmt_dec
	mov	x20,x0
	ldr	x2,[sp],16
	add	x2,x2,1
	cmp	x2,4
	b.hs	.end
	mov	w1,'.'
	strb	w1,[x20],1
	b	.b
.end:	strb	wzr,[x20]
	b	.out
.off:	adr	x0,netbuf
	strb	wzr,[x0]
	adr	x1,str_off
	bl	tr_x1
	bl	str_cat
.out:	ldp	x19,x20,[sp],16
	epilogue

title		db 'Barre de menus',0
str_armpi	db 'Korin OS',0
str_off		db 'hors ligne',0
p_cfg		db 'SD0:/etc/clock.cfg',0
p_wifimenu	db 'SD0:/sys/wifimenu',0
p_soundmenu	db 'SD0:/sys/soundmenu',0
s_avance	db 'Avanc',0xC3,0xA9,0xE2,0x80,0xA6,0,0
p_mixer		db 'SD0:/apps/mixer.app/main',0
p_pocketqi	db 'SD0:/apps/pocketqi.app/main',0
s_popup		db '--popup ',0
p_help		db 'SD0:/apps/help.app/main',0
s_help_f	db '.app/help_',0
s_md		db '.md',0
s_en		db 'en',0
s_fr		db 'fr',0
s_aide		db 'Aide',0
s_help		db 'Help',0
s_aide_de	db 'Aide de',0
s_sommaire	db 'Sommaire de l',0x27,'aide',0
s_losange	db 0xE2,0x97,0x86,0		; U+25C6
s_croix		db 0xC3,0x97,0			; U+00D7
s_sep		db '-',0
s_bienvenue	db 'welcome',0
s_apropos	db 'A propos de Korin OS',0xE2,0x80,0xA6,0
s_fenetres	db '>Fen',0xC3,0xAA,'tres ouvertes',0
s_reseau	db 'R',0xC3,0xA9,'seau : ',0
s_quitter	db 'Quitter',9,'Ctrl+Q',0
		align 4

include '../../inc/tr.inc'

segment readable writeable
kit_data
ui_data
applis_data
		align 8
net_x		dd 0				; icone du reseau
snd_x		dd 0				; haut-parleur
pq_x		dd 0				; PocketQI (-100 : pas installee)
pq_on		dd 0				; 1 : PocketQI installee
snd_st		dd 0				; tampon d'etat de sound (0 : aucun)
snd_gen		dd -2				; derniere generation vue
wst		rb 64				; etat du Wi-Fi
nact		rb 16				; interface active (nom, type)
argbuf		rb 24
bar_h		dd 30
hov		dd 0				; titre survole + 1 (0 : aucun)
open_menu	dd 0				; titre ouvert + 1 (0 : aucun)
pend_x		dd 0				; clic qui a ferme un menu (ecran ;
pend_y		dd -1				; -1 : aucun), contigus
t_poll		dd 0				; derniere lecture du menu actif (ms)
t_clock		dd 0				; derniere mise a l'heure (ms)
mb_serial	dd 0				; numero de serie du menu montre (0 : aucun)
mb_n		dd 1				; titres connus (ARMPI compris)
mb_shown	dd 1				; titres dessines (zones de clic valides)
mb_end		dd 0				; fin du texte des menus
mb_ptr		rd MB_MAX			; titres ([1] : nom de l'application)
mb_x0		rd MB_MAX			; zones de clic : debut, fin
mb_x1		rd MB_MAX
mb_ids		rd MB_ITEMS			; menu ouvert : id de chaque ligne
app_title	rb WS_MENU_TITLE
spec_buf	rb WS_MENU_MAX + 16
offset		dd 0
scr		rb 32
netinfo		rb 32
clockbuf	rb 16
netbuf		rb 32
cfgbuf		rb 32
wlist		rb WL_PER_MSG * WL_SIZE
win_ids		rd AP_MAX
sub_map		rb AP_MAX
items		rb ITEMS_MAX
items2		rb ITEMS_MAX			; sous-menu (le menu garde les siens)
help_on		dd 0				; 1 : l'application active a une aide
help_pid	dd 0				; son process (0 : aucun)
help_t		dd -1				; titre du menu Aide (-1 : pas montre)
help_tail	dd 0				; (help_probe) fin de « .../help_ »
help_path	rb 128				; son fichier d'aide
help_name	rb AP_LIB			; son nom affiche
hbuf		rb 128				; « Aide de <nom> »
plist		rb PL_MAX * 64			; liste des process
