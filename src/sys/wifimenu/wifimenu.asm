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
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/netkit/netkit.inc'
include '../../inc/netd.inc'
include '../../kits/buskit/buskit.inc'

MW		= 300			; largeur
ROWH		= 26			; une ligne
NMAX		= 8			; reseaux montres
HEAD		= 34
FOOT		= 34
KEY_H		= 34			; ligne depliee
FIELD_W		= 150
BW		= 106
HT_MAX		= 24

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
	adr	x0,s_topic_wifi			; l'etat : le bus
	mov	x1,0
	bl	bus_subscribe
	cmp	x0,0
	csel	x0,x0,xzr,gt
	adr	x1,wm_sub
	str	x0,[x1]
	bl	wm_status
	mov	x0,1				; un balayage tout de suite
	bl	lancer
	mov	x0,16384			; le thread des travaux
	bl	malloc
	cbz	x0,quit
	add	x1,x0,16384
	adr	x0,travaux
	mov	x2,0
	sys	SYS_THREAD_CREATE
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
.tick:	bl	tick				; (rien : l'etat, les travaux)
	cmp	x0,2				; connecte : fermer
	b.eq	.fermer
	cbz	x0,.loop
	bl	redraw
	b	.loop
.bus:	adr	x9,ui_ev			; korin.net.wifi : l'etat a change
	ldr	w0,[x9,BEV_SUB]
	adr	x1,wm_sub
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.loop
	adr	x0,wm_maj
	mov	x1,1
	str	x1,[x0]
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
	bl	touche
	cbz	x0,.loop
	bl	redraw
	b	.loop
.mouse:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cmp	w0,1
	b.ne	.loop
	ldrsw	x0,[x9,EV_X]
	ldrsw	x1,[x9,EV_Y]
	bl	clic
	cmp	x0,3
	b.eq	quit
	cbz	x0,.loop
	bl	redraw
	b	.loop
.fermer:
	bl	redraw
	mov	x0,900
	sys	SYS_SLEEP
quit:	mov	x0,0
	sys	SYS_EXIT

; hauteur selon le contenu -> x0
hauteur:
	adr	x0,nvis
	ldr	x0,[x0]
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lo			; (au moins une ligne : un message)
	mov	x1,ROWH
	mul	x0,x0,x1
	add	x0,x0,HEAD+FOOT+8
	adr	x1,selrow
	ldr	x1,[x1]
	tbnz	x1,63,.r
	add	x0,x0,KEY_H
.r:	ret

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

; x0 = travail (1 balayage, 2 connexion) : au thread, s'il est libre
lancer:
	adr	x1,job
	ldr	x2,[x1]
	cbnz	x2,.r
	adr	x2,jdone
	str	xzr,[x2]
	dmb	ish
	str	x0,[x1]
.r:	ret

; thread des travaux
travaux:
.loop:	mov	x0,50
	sys	SYS_SLEEP
	adr	x0,job
	ldr	x19,[x0]
	cbz	x19,.loop
	cmp	x19,2
	b.eq	.join
	adr	x0,scan2
	mov	x1,32
	bl	wifi_scan
	b	.fini
.join:	adr	x0,jssid
	adr	x1,jkey
	bl	wifi_join
.fini:	adr	x1,jres
	str	x0,[x1]
	dmb	ish
	adr	x1,jdone
	str	x19,[x1]
	adr	x1,job
	str	xzr,[x1]
	b	.loop

wm_status:
	prologue
	adr	x0,wstat
	bl	wifi_status
	epilogue

; (toutes les 200 ms, et a chaque korin.net.wifi) -> x0 = 0 rien,
; 1 redessiner, 2 connecte (fermer)
tick:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
	adr	x0,wm_sub			; abonne : l'etat quand il est publie
	ldr	x0,[x0]
	cbz	x0,.compte
	adr	x0,wm_maj
	ldr	x1,[x0]
	cbz	x1,.job
	str	xzr,[x0]
	bl	wm_status
	mov	x19,1
	b	.job
.compte:
	adr	x0,compte			; sans bus : l'etat chaque seconde
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	mov	x19,0
	cmp	x1,5
	b.lo	.job
	str	xzr,[x0]
	bl	wm_status
	mov	x19,1
.job:	adr	x0,jdone
	ldr	x20,[x0]
	cbz	x20,.out
	str	xzr,[x0]
	mov	x19,1
	adr	x0,jres
	ldr	x0,[x0]
	cmp	x20,1
	b.ne	.join
	tbnz	x0,63,.vide			; balayage : tries par signal
	cmp	x0,32
	mov	x1,32
	csel	x0,x0,x1,ls
	b	.liste
.vide:	mov	x0,0
.liste:	bl	trier
	b	.out
.join:	bl	wm_status
	adr	x1,s_connecte
	bl	tr_x1
	cbz	x0,.msg
	adr	x1,s_refuse
	bl	tr_x1
	cmn	x0,13
	b.eq	.msg
	adr	x1,s_echec
	bl	tr_x1
.msg:	adr	x2,msgp
	str	x1,[x2]
	cbnz	x0,.out
	mov	x19,2
.out:	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entrees dans scan2 : copiees dans scan, du plus fort au plus faible ;
; nvis = min(n, NMAX)
trier:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,scan
	adr	x1,scan2
	mov	x2,48
	mul	x2,x19,x2
	bl	mem_move
	mov	x20,0				; tri par insertion (RSSI decroissant)
.i:	add	x20,x20,1
	cmp	x20,x19
	b.hs	.fin
	mov	x3,x20
.j:	cbz	x3,.i
	adr	x4,scan
	mov	x5,48
	madd	x6,x3,x5,x4			; entree j
	sub	x7,x6,48			; entree j - 1
	ldrsh	w8,[x6,36]
	ldrsh	w9,[x7,36]
	cmp	w8,w9
	b.le	.i
	mov	x10,0				; echange
.e:	ldr	x11,[x6,x10]
	ldr	x12,[x7,x10]
	str	x12,[x6,x10]
	str	x11,[x7,x10]
	add	x10,x10,8
	cmp	x10,48
	b.lo	.e
	sub	x3,x3,1
	b	.j
.fin:	cmp	x19,NMAX
	mov	x0,NMAX
	csel	x0,x19,x0,ls
	adr	x1,nvis
	str	x0,[x1]
	adr	x1,scanned
	mov	x2,1
	str	x2,[x1]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree -> tmp : son SSID termine
ssid_de:
	ldrb	w1,[x0,32]
	cmp	w1,32
	mov	w2,32
	csel	w1,w1,w2,ls
	adr	x2,tmp
	mov	x3,0
.c:	cmp	x3,x1
	b.hs	.f
	ldrb	w4,[x0,x3]
	strb	w4,[x2,x3]
	add	x3,x3,1
	b	.c
.f:	strb	wzr,[x2,x3]
	ret

; x0 = entree -> x0 = 1 si son SSID est le reseau courant
courant:
	prologue
	str	x19,[sp,-16]!
	adr	x1,wstat
	ldr	w2,[x1]
	cmp	w2,WIFI_ST_CONNECTED
	b.ne	.non
	bl	ssid_de
	adr	x0,tmp
	adr	x1,wstat
	add	x1,x1,16
	bl	str_eq
	b	.out
.non:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = entree -> x0 = 1 si connu (wifi_known)
connu:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	ssid_de
	adr	x0,known			; (lu une fois)
	mov	x1,16
	bl	wifi_known
	cmp	x0,0
	b.le	.non
	mov	x20,x0
	adr	x19,known
.k:	mov	x0,x19
	adr	x1,tmp
	bl	str_eq
	cbnz	x0,.out
	add	x19,x19,48
	subs	x20,x20,1
	b.ne	.k
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0, x1, x2, x3 = rectangle, x4 = action, x5 = argument
hit_add:
	adr	x6,nhits
	ldr	x7,[x6]
	cmp	x7,HT_MAX
	b.hs	.r
	add	x8,x7,1
	str	x8,[x6]
	adr	x6,hits
	add	x6,x6,x7,lsl 5
	stp	w0,w1,[x6]
	stp	w2,w3,[x6,8]
	stp	w4,w5,[x6,16]
.r:	ret

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,nhits
	str	xzr,[x0]
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
	adr	x0,wstat
	ldr	w0,[x0]
	adr	x1,s_absent
	bl	tr_x1
	cbz	w0,.et
	adr	x1,s_nonco
	bl	tr_x1
	cmp	w0,WIFI_ST_CONNECTED
	b.ne	.et
	adr	x1,s_co
	bl	tr_x1
.et:	adr	x2,job				; (ou l'action en cours)
	ldr	x2,[x2]
	adr	x3,s_recherche
	bl	tr_x3
	cmp	x2,1
	csel	x1,x3,x1,eq
	adr	x3,s_connexion
	bl	tr_x3
	cmp	x2,2
	csel	x1,x3,x1,eq
	adr	x3,msgp
	ldr	x3,[x3]
	cmp	x2,0
	ccmp	x3,0,4,eq			; (pas de travail et un message)
	csel	x1,x3,x1,ne
	mov	x0,x1
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
	mov	x20,HEAD+4			; y
	adr	x0,nvis
	ldr	x0,[x0]
	cbnz	x0,.lignes
	adr	x0,s_attente			; pas encore de liste
	bl	tr_x0
	adr	x1,scanned
	ldr	x1,[x1]
	cbz	x1,.vide
	adr	x0,s_aucun
	bl	tr_x0
.vide:	mov	x1,16
	add	x2,x20,17
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,MW-32
	bl	ui_text
	add	x20,x20,ROWH
	b	.bas
.lignes:
	mov	x22,0
.l:	adr	x0,nvis
	ldr	x0,[x0]
	cmp	x22,x0
	b.hs	.bas
	adr	x21,scan
	mov	x0,48
	madd	x21,x22,x0,x21
	mov	x0,8
	mov	x1,x20
	mov	x2,MW-16
	mov	x3,ROWH
	mov	x4,1
	mov	x5,x22
	bl	hit_add
	adr	x0,selrow
	ldr	x0,[x0]
	cmp	x0,x22
	b.ne	.li
	mov	x0,8
	mov	x1,x20
	mov	x2,MW-16
	mov	x3,ROWH
	ldr	w4,[x23,TJ_ACCENT_CLAIR]
	bl	ui_fill
.li:	ldrsh	w0,[x21,36]
	bl	wd_level
	mov	x2,x0
	mov	x0,16
	add	x1,x20,6
	ldr	w3,[x23,TJ_TEXTE1]
	ldr	w4,[x23,TJ_LIGNE_FORTE]
	bl	wd_bars
	ldrb	w0,[x21,34]
	cbz	w0,.ls
	mov	x0,38
	add	x1,x20,8
	ldr	w2,[x23,TJ_TEXTE2]
	bl	wd_lock
.ls:	mov	x0,x21
	bl	ssid_de
	adr	x0,tmp
	mov	x1,54
	add	x2,x20,18
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,MW-100
	bl	ui_text
	mov	x0,x21
	bl	courant
	cbz	x0,.ln
	mov	x0,MW-34
	add	x1,x20,9
	ldr	w2,[x23,TJ_ACCENT]
	bl	wd_check
.ln:	add	x20,x20,ROWH
	adr	x0,selrow				; ligne depliee : cle, Se connecter
	ldr	x0,[x0]
	cmp	x0,x22
	b.ne	.lsuiv
	mov	x0,16				; champ (masque)
	add	x1,x20,4
	mov	x2,FIELD_W
	mov	x3,26
	mov	x4,5
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	mov	w6,w5
	bl	ui_rbox
	mov	x0,17
	add	x1,x20,5
	mov	x2,FIELD_W-2
	mov	x3,24
	mov	x4,4
	ldr	w5,[x23,TJ_CREUX]
	mov	w6,w5
	bl	ui_rbox
	adr	x0,buf
	adr	x2,keylen
	ldr	x2,[x2]
	cmp	x2,18
	mov	x3,18
	csel	x2,x2,x3,ls
.pu:	cbz	x2,.puf
	mov	w3,0xE2
	strb	w3,[x0],1
	mov	w3,0x80
	strb	w3,[x0],1
	mov	w3,0xA2
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.pu
.puf:	mov	w3,'|'
	strb	w3,[x0],1
	strb	wzr,[x0]
	adr	x0,buf
	mov	x1,24
	add	x2,x20,22
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,FIELD_W-12
	bl	ui_text
	mov	x0,MW-BW-12			; Se connecter
	add	x1,x20,3
	mov	x2,BW
	mov	x3,28
	mov	x4,2
	mov	x5,x22
	bl	hit_add
	mov	x0,MW-BW-12
	add	x1,x20,3
	mov	x2,BW
	mov	x3,28
	adr	x4,s_seco
	bl	tr_x4
	mov	x5,1
	bl	ui_button
	add	x20,x20,KEY_H
.lsuiv:	add	x22,x22,1
	b	.l
.bas:	add	x20,x20,4			; Parametres Wi-Fi...
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
	bl	hit_add
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
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = clic -> x0 = 0 rien, 1 redessiner, 3 fermer
clic:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x2,hits
	adr	x3,nhits
	ldr	x3,[x3]
.t:	cbz	x3,.rien
	ldp	w4,w5,[x2]
	ldp	w6,w7,[x2,8]
	cmp	x0,x4
	b.lt	.n
	add	x4,x4,x6
	cmp	x0,x4
	b.ge	.n
	cmp	x1,x5
	b.lt	.n
	add	x5,x5,x7
	cmp	x1,x5
	b.ge	.n
	ldp	w19,w20,[x2,16]
	b	.act
.n:	add	x2,x2,32
	sub	x3,x3,1
	b	.t
.act:	cmp	w19,1
	b.eq	.ligne
	cmp	w19,2
	b.eq	.seco
	cmp	w19,3
	b.eq	.param
	b	.rien
.ligne:	adr	x21,scan			; connu ou ouvert : tout de suite
	mov	x0,48
	madd	x21,x20,x0,x21
	ldrb	w0,[x21,34]
	cbz	w0,.direct
	mov	x0,x21
	bl	connu
	cbnz	x0,.direct
	adr	x0,selrow				; sinon : la cle
	str	x20,[x0]
	adr	x0,keylen
	str	xzr,[x0]
	b	.oui
.direct:
	mov	x0,x21
	bl	ssid_de
	adr	x0,jssid
	strb	wzr,[x0]
	adr	x1,tmp
	bl	str_cat
	adr	x0,jkey
	strb	wzr,[x0]
	mov	x0,2
	bl	lancer
	b	.oui
.seco:	adr	x0,keylen
	ldr	x0,[x0]
	cmp	x0,8
	b.lo	.courte
	adr	x21,scan
	mov	x0,48
	madd	x21,x20,x0,x21
	mov	x0,x21
	bl	ssid_de
	adr	x0,jssid
	strb	wzr,[x0]
	adr	x1,tmp
	bl	str_cat
	adr	x0,jkey
	strb	wzr,[x0]
	adr	x1,keybuf
	bl	str_cat
	adr	x0,selrow
	mov	x1,-1
	str	x1,[x0]
	mov	x0,2
	bl	lancer
	b	.oui
.courte:
	adr	x0,msgp
	adr	x1,s_courte
	bl	tr_x1
	str	x1,[x0]
	b	.oui
.param:	adr	x0,p_control			; le panneau, sur l'applet Wi-Fi
	adr	x1,s_arg_wifi
	sys	SYS_SPAWN
	mov	x0,3
	b	.out
.rien:	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = evenement clavier -> x0 = 1 si pris (champ de la cle)
touche:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,selrow
	ldr	x20,[x0]
	tbnz	x20,63,.non
	ldr	w0,[x19,EV_CODE]
	cmp	w0,KEY_ENTER
	b.eq	.entree
	cmp	w0,KEY_BACKSPACE
	b.eq	.efface
	ldr	w0,[x19,EV_CHAR]
	cmp	w0,32
	b.lo	.non
	adr	x1,keylen
	ldr	x2,[x1]
	cmp	x2,63
	b.hs	.oui
	adr	x1,keybuf
	add	x1,x1,x2
	bl	utf8_put
	adr	x1,keylen
	ldr	x2,[x1]
	add	x2,x2,x0
	cmp	x2,63
	b.hi	.oui
	str	x2,[x1]
	adr	x1,keybuf
	strb	wzr,[x1,x2]
	b	.oui
.efface:
	adr	x1,keylen
	ldr	x2,[x1]
	cbz	x2,.oui
	adr	x3,keybuf
.e:	sub	x2,x2,1
	cbz	x2,.ef
	ldrb	w4,[x3,x2]
	and	w4,w4,0xC0
	cmp	w4,0x80
	b.eq	.e
.ef:	str	x2,[x1]
	strb	wzr,[x3,x2]
	b	.oui
.entree:
	adr	x0,ui_ev			; comme Se connecter
	mov	x0,0
	mov	x1,0
	mov	x19,x20
	bl	connecter_sel
	b	.oui
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x19 = ligne depliee : Se connecter
connecter_sel:
	prologue
	adr	x0,keylen
	ldr	x0,[x0]
	cmp	x0,8
	b.lo	.courte
	adr	x2,scan
	mov	x0,48
	madd	x0,x19,x0,x2
	bl	ssid_de
	adr	x0,jssid
	strb	wzr,[x0]
	adr	x1,tmp
	bl	str_cat
	adr	x0,jkey
	strb	wzr,[x0]
	adr	x1,keybuf
	bl	str_cat
	adr	x0,selrow
	mov	x1,-1
	str	x1,[x0]
	mov	x0,2
	bl	lancer
	epilogue
.courte:
	adr	x0,msgp
	adr	x1,s_courte
	bl	tr_x1
	str	x1,[x0]
	epilogue

include '../../inc/wifidraw.inc'

s_nom		db 'wifimenu',0
s_topic_wifi	db 'korin.net.wifi',0
title		db 'Wi-Fi',0
s_wifi		db 'Wi-Fi',0
s_absent	db 'pas de Wi-Fi',0
s_nonco		db 'non connect',0xC3,0xA9,0
s_co		db 'connect',0xC3,0xA9,0
s_recherche	db 'Recherche...',0
s_connexion	db 'Connexion...',0
s_connecte	db 'Connect',0xC3,0xA9,0
s_refuse	db 'Cl',0xC3,0xA9,' refus',0xC3,0xA9,'e',0
s_echec		db 'Connexion impossible',0
s_courte	db 'Cl',0xC3,0xA9,' trop courte',0
s_attente	db 'Recherche des r',0xC3,0xA9,'seaux...',0
s_aucun		db 'Aucun r',0xC3,0xA9,'seau visible',0
s_seco		db 'Se connecter',0
s_parametres	db 'Param',0xC3,0xA8,'tres Wi-Fi...',0
p_control	db 'SD0:/apps/control.app/main',0
s_arg_wifi	db 'wifi',0
		align 8

include '../../inc/tr.inc'

segment readable writeable
kit_data
ui_data
		align 8
canal		dd 0
win_x		dd 0
win_y		dd 0
win_h		dd 0
job		dd 0
jdone		dd 0
jres		dd 0
selrow		dd -1
keylen		dd 0
nvis		dd 0
scanned		dd 0
compte		dd 0
wm_sub		dd 0				; abonnement a korin.net.wifi (0 : pas de bus)
wm_maj		dd 0				; l'etat a ete publie : a relire
msgp		dd 0
nhits		dd 0
scr		rb 32
wstat		rb 64
hits		rb HT_MAX * 32
scan		rb 32 * 48
scan2		rb 32 * 48
known		rb 16 * 48
keybuf		rb 72
jssid		rb 40
jkey		rb 72
tmp		rb 40
buf		rb 128
msg		rb 512
