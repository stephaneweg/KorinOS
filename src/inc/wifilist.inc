; ============================================================================
;  inc/wifilist.inc - la liste des reseaux Wi-Fi d'un volet : le menu Wi-Fi de
;  la barre (src/sys/wifimenu) et le detail Wi-Fi des reglages rapides
;  (src/sys/quick). Reseaux visibles du plus fort au plus faible, signal,
;  cadenas, coche sur le reseau courant ; un clic rejoint aussitot un reseau
;  connu ou ouvert, sinon la ligne s'ouvre avec un champ de cle et
;  « Se connecter ». Balayage et connexion dans un thread (wl_travaux) ;
;  l'etat suit korin.net.wifi (sans busd : relu chaque seconde).
;
;	segment executable (apres kit_use appkit, uikit, netkit, buskit) :
;		include '../../inc/wifidraw.inc' ; include '../../inc/wifilist.inc'
;	donnees : wifilist_data
;
;	bl wl_start		; x0 = 1 : un balayage tout de suite
;	(WEV_BUS) x0 = abonnement : bl wl_bus -> 1 si c'est korin.net.wifi
;	bl wl_tick		; toutes les 200 ms -> 0 rien, 1 redessiner, 2 connecte
;	wl_w = largeur ; bl wl_nhits_reset ; ... bl wl_draw (x0 = y) -> y apres
;	bl wl_click		; x0, x1 = clic -> 0 rien, 1 redessiner, >= 3 : action
;				; du programme (wl_hit_add), x1 = son argument
;	bl wl_key		; x0 = evenement clavier -> 1 pris
;  Le programme inclut tr.inc (tr_x0...) et les actions 1 et 2 sont a la liste.
; ============================================================================

WL_ROWH		= 26			; une ligne
WL_NMAX		= 8			; reseaux montres
WL_KEY_H	= 34			; ligne depliee
WL_FIELD_W	= 150
WL_BW		= 106
WL_HT_MAX	= 48

; x0 = 1 : balayer tout de suite. Abonnement, etat, thread des travaux
; -> x0 = 0 / < 0 (pas de memoire)
wl_start:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,wl_s_topic			; l'etat : le bus
	mov	x1,0
	bl	bus_subscribe
	cmp	x0,0
	csel	x0,x0,xzr,gt
	adr	x1,wl_sub
	str	x0,[x1]
	bl	wl_status
	cbz	x19,.th
	mov	x0,1
	bl	wl_lancer
.th:	mov	x0,16384			; le thread des travaux
	bl	malloc
	cbz	x0,.ko
	add	x1,x0,16384
	adr	x0,wl_travaux
	mov	x2,0
	sys	SYS_THREAD_CREATE
	mov	x0,0
	b	.out
.ko:	mov	x0,-1
.out:	ldr	x19,[sp],16
	epilogue

; x0 = abonnement d'un WEV_BUS -> x0 = 1 si korin.net.wifi (a relire au
; prochain wl_tick)
wl_bus:
	adr	x1,wl_sub
	ldr	x1,[x1]
	cbz	x1,.non
	cmp	x0,x1
	b.ne	.non
	adr	x0,wl_maj
	mov	x1,1
	str	x1,[x0]
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = travail (1 balayage, 2 connexion a wl_jssid / wl_jkey) : au thread,
; s'il est libre
wl_lancer:
	adr	x1,wl_job
	ldr	x2,[x1]
	cbnz	x2,.r
	adr	x2,wl_jdone
	str	xzr,[x2]
	dmb	ish
	str	x0,[x1]
.r:	ret

; thread des travaux
wl_travaux:
.loop:	mov	x0,50
	sys	SYS_SLEEP
	adr	x0,wl_job
	ldr	x19,[x0]
	cbz	x19,.loop
	cmp	x19,2
	b.eq	.join
	adr	x0,wl_scan2
	mov	x1,32
	bl	wifi_scan
	b	.fini
.join:	adr	x0,wl_jssid
	adr	x1,wl_jkey
	bl	wifi_join
.fini:	adr	x1,wl_jres
	str	x0,[x1]
	dmb	ish
	adr	x1,wl_jdone
	str	x19,[x1]
	adr	x1,wl_job
	str	xzr,[x1]
	b	.loop

wl_status:
	prologue
	adr	x0,wl_wstat
	bl	wifi_status
	epilogue

; (toutes les 200 ms, et a chaque korin.net.wifi) -> x0 = 0 rien,
; 1 redessiner, 2 connecte (le menu se ferme)
wl_tick:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
	adr	x0,wl_sub			; abonne : l'etat quand il est publie
	ldr	x0,[x0]
	cbz	x0,.compte
	adr	x0,wl_maj
	ldr	x1,[x0]
	cbz	x1,.job
	str	xzr,[x0]
	bl	wl_status
	mov	x19,1
	b	.job
.compte:
	adr	x0,wl_compte			; sans bus : l'etat chaque seconde
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	mov	x19,0
	cmp	x1,5
	b.lo	.job
	str	xzr,[x0]
	bl	wl_status
	mov	x19,1
.job:	adr	x0,wl_jdone
	ldr	x20,[x0]
	cbz	x20,.out
	str	xzr,[x0]
	mov	x19,1
	adr	x0,wl_jres
	ldr	x0,[x0]
	cmp	x20,1
	b.ne	.join
	tbnz	x0,63,.vide			; balayage : tries par signal
	cmp	x0,32
	mov	x1,32
	csel	x0,x0,x1,ls
	b	.liste
.vide:	mov	x0,0
.liste:	bl	wl_trier
	b	.out
.join:	bl	wl_status
	adr	x1,wl_s_connecte
	bl	tr_x1
	cbz	x0,.msg
	adr	x1,wl_s_refuse
	bl	tr_x1
	cmn	x0,13
	b.eq	.msg
	adr	x1,wl_s_echec
	bl	tr_x1
.msg:	adr	x2,wl_msgp
	str	x1,[x2]
	cbnz	x0,.out
	mov	x19,2
.out:	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entrees dans wl_scan2 : copiees dans wl_scan, du plus fort au plus
; faible ; wl_nvis = min(n, WL_NMAX)
wl_trier:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,wl_scan
	adr	x1,wl_scan2
	mov	x2,48
	mul	x2,x19,x2
	bl	mem_move
	mov	x20,0				; tri par insertion (RSSI decroissant)
.i:	add	x20,x20,1
	cmp	x20,x19
	b.hs	.fin
	mov	x3,x20
.j:	cbz	x3,.i
	adr	x4,wl_scan
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
.fin:	cmp	x19,WL_NMAX
	mov	x0,WL_NMAX
	csel	x0,x19,x0,ls
	adr	x1,wl_nvis
	str	x0,[x1]
	adr	x1,wl_scanned
	mov	x2,1
	str	x2,[x1]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree -> wl_tmp : son SSID termine
wl_ssid_de:
	ldrb	w1,[x0,32]
	cmp	w1,32
	mov	w2,32
	csel	w1,w1,w2,ls
	adr	x2,wl_tmp
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
wl_courant:
	prologue
	str	x19,[sp,-16]!
	adr	x1,wl_wstat
	ldr	w2,[x1]
	cmp	w2,WIFI_ST_CONNECTED
	b.ne	.non
	bl	wl_ssid_de
	adr	x0,wl_tmp
	adr	x1,wl_wstat
	add	x1,x1,16
	bl	str_eq
	b	.out
.non:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = entree -> x0 = 1 si connu (wifi_known)
wl_connu:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	wl_ssid_de
	adr	x0,wl_known
	mov	x1,16
	bl	wifi_known
	cmp	x0,0
	b.le	.non
	mov	x20,x0
	adr	x19,wl_known
.k:	mov	x0,x19
	adr	x1,wl_tmp
	bl	str_eq
	cbnz	x0,.out
	add	x19,x19,48
	subs	x20,x20,1
	b.ne	.k
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; les zones de clic sont refaites a chaque dessin
wl_nhits_reset:
	adr	x0,wl_nhits
	str	xzr,[x0]
	ret

; x0, x1, x2, x3 = rectangle, x4 = action, x5 = argument
wl_hit_add:
	adr	x6,wl_nhits
	ldr	x7,[x6]
	cmp	x7,WL_HT_MAX
	b.hs	.r
	add	x8,x7,1
	str	x8,[x6]
	adr	x6,wl_hits
	add	x6,x6,x7,lsl 5
	stp	w0,w1,[x6]
	stp	w2,w3,[x6,8]
	stp	w4,w5,[x6,16]
.r:	ret

; x0, x1 = point -> x0 = action (0 : aucune), x1 = argument, x2..x5 = son
; rectangle (la derniere zone posee gagne : elles se recouvrent)
wl_hit_find:
	adr	x2,wl_hits
	adr	x3,wl_nhits
	ldr	x3,[x3]
	mov	x9,0
.t:	cbz	x3,.fin
	ldp	w4,w5,[x2]
	ldp	w6,w7,[x2,8]
	cmp	x0,x4
	b.lt	.n
	add	x8,x4,x6
	cmp	x0,x8
	b.ge	.n
	cmp	x1,x5
	b.lt	.n
	add	x8,x5,x7
	cmp	x1,x8
	b.ge	.n
	mov	x9,x2
.n:	add	x2,x2,32
	sub	x3,x3,1
	b	.t
.fin:	cbz	x9,.rien
	ldp	w0,w1,[x9,16]
	ldp	w2,w3,[x9]
	ldp	w4,w5,[x9,8]
	ret
.rien:	mov	x0,0
	mov	x1,0
	ret

; -> x0 = hauteur de la liste (une ligne au moins : un message)
wl_height:
	adr	x0,wl_nvis
	ldr	x0,[x0]
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lo
	mov	x1,WL_ROWH
	mul	x0,x0,x1
	adr	x1,wl_selrow
	ldr	x1,[x1]
	tbnz	x1,63,.r
	add	x0,x0,WL_KEY_H
.r:	ret

; -> x0 = l'etat en quelques mots (pas de Wi-Fi, connecte, Recherche..., ou
; le message de la derniere action)
wl_state_text:
	prologue
	adr	x0,wl_wstat
	ldr	w0,[x0]
	adr	x1,wl_s_absent
	bl	tr_x1
	cbz	w0,.et
	adr	x1,wl_s_nonco
	bl	tr_x1
	cmp	w0,WIFI_ST_CONNECTED
	b.ne	.et
	adr	x1,wl_s_co
	bl	tr_x1
.et:	adr	x2,wl_job			; (ou l'action en cours)
	ldr	x2,[x2]
	adr	x3,wl_s_recherche
	bl	tr_x3
	cmp	x2,1
	csel	x1,x3,x1,eq
	adr	x3,wl_s_connexion
	bl	tr_x3
	cmp	x2,2
	csel	x1,x3,x1,eq
	adr	x3,wl_msgp
	ldr	x3,[x3]
	cmp	x2,0
	ccmp	x3,0,4,eq			; (pas de travail et un message)
	csel	x1,x3,x1,ne
	mov	x0,x1
	epilogue

; x0 = y du haut : les lignes (largeur wl_w ; jetons de ui_jetons) -> x0 = y
; sous la liste
wl_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0				; y
	bl	ui_jetons
	mov	x23,x0
	adr	x0,wl_w
	ldr	x24,[x0]			; largeur
	adr	x0,wl_nvis
	ldr	x0,[x0]
	cbnz	x0,.lignes
	adr	x0,wl_s_attente			; pas encore de liste
	bl	tr_x0
	adr	x1,wl_scanned
	ldr	x1,[x1]
	cbz	x1,.vide
	adr	x0,wl_s_aucun
	bl	tr_x0
.vide:	mov	x1,16
	add	x2,x20,17
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	sub	x6,x24,32
	bl	ui_text
	add	x20,x20,WL_ROWH
	b	.bas
.lignes:
	mov	x22,0
.l:	adr	x0,wl_nvis
	ldr	x0,[x0]
	cmp	x22,x0
	b.hs	.bas
	adr	x21,wl_scan
	mov	x0,48
	madd	x21,x22,x0,x21
	mov	x0,8
	mov	x1,x20
	sub	x2,x24,16
	mov	x3,WL_ROWH
	mov	x4,1
	mov	x5,x22
	bl	wl_hit_add
	adr	x0,wl_selrow
	ldr	x0,[x0]
	cmp	x0,x22
	b.ne	.li
	mov	x0,8
	mov	x1,x20
	sub	x2,x24,16
	mov	x3,WL_ROWH
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
	bl	wl_ssid_de
	adr	x0,wl_tmp
	mov	x1,54
	add	x2,x20,18
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	sub	x6,x24,100
	bl	ui_text
	mov	x0,x21
	bl	wl_courant
	cbz	x0,.ln
	sub	x0,x24,34
	add	x1,x20,9
	ldr	w2,[x23,TJ_ACCENT]
	bl	wd_check
.ln:	add	x20,x20,WL_ROWH
	adr	x0,wl_selrow			; ligne depliee : cle, Se connecter
	ldr	x0,[x0]
	cmp	x0,x22
	b.ne	.lsuiv
	mov	x0,16				; champ (masque)
	add	x1,x20,4
	mov	x2,WL_FIELD_W
	mov	x3,26
	mov	x4,5
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	mov	w6,w5
	bl	ui_rbox
	mov	x0,17
	add	x1,x20,5
	mov	x2,WL_FIELD_W-2
	mov	x3,24
	mov	x4,4
	ldr	w5,[x23,TJ_CREUX]
	mov	w6,w5
	bl	ui_rbox
	adr	x0,wl_buf
	adr	x2,wl_keylen
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
	adr	x0,wl_buf
	mov	x1,24
	add	x2,x20,22
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,WL_FIELD_W-12
	bl	ui_text
	sub	x0,x24,WL_BW+12			; Se connecter
	add	x1,x20,3
	mov	x2,WL_BW
	mov	x3,28
	mov	x4,2
	mov	x5,x22
	bl	wl_hit_add
	sub	x0,x24,WL_BW+12
	add	x1,x20,3
	mov	x2,WL_BW
	mov	x3,28
	adr	x4,wl_s_seco
	bl	tr_x4
	mov	x5,1
	bl	ui_button
	add	x20,x20,WL_KEY_H
.lsuiv:	add	x22,x22,1
	b	.l
.bas:	mov	x0,x20
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = clic -> x0 = 0 rien, 1 redessiner ; une zone du programme
; (action >= 3) : x0 = l'action, x1 = son argument
wl_click:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	wl_hit_find
	mov	x19,x0
	mov	x20,x1
	cbz	x19,.rien
	cmp	x19,1
	b.eq	.ligne
	cmp	x19,2
	b.eq	.seco
	mov	x0,x19				; (au programme)
	mov	x1,x20
	b	.out
.ligne:	adr	x21,wl_scan			; connu ou ouvert : tout de suite
	mov	x0,48
	madd	x21,x20,x0,x21
	ldrb	w0,[x21,34]
	cbz	w0,.direct
	mov	x0,x21
	bl	wl_connu
	cbnz	x0,.direct
	adr	x0,wl_selrow			; sinon : la cle
	str	x20,[x0]
	adr	x0,wl_keylen
	str	xzr,[x0]
	b	.oui
.direct:
	mov	x0,x21
	bl	wl_ssid_de
	adr	x0,wl_jssid
	strb	wzr,[x0]
	adr	x1,wl_tmp
	bl	str_cat
	adr	x0,wl_jkey
	strb	wzr,[x0]
	mov	x0,2
	bl	wl_lancer
	b	.oui
.seco:	mov	x19,x20
	bl	wl_connecter_sel
	b	.oui
.rien:	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = evenement clavier -> x0 = 1 si pris (champ de la cle)
wl_key:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,wl_selrow
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
	adr	x1,wl_keylen
	ldr	x2,[x1]
	cmp	x2,63
	b.hs	.oui
	adr	x1,wl_keybuf
	add	x1,x1,x2
	bl	utf8_put
	adr	x1,wl_keylen
	ldr	x2,[x1]
	add	x2,x2,x0
	cmp	x2,63
	b.hi	.oui
	str	x2,[x1]
	adr	x1,wl_keybuf
	strb	wzr,[x1,x2]
	b	.oui
.efface:
	adr	x1,wl_keylen
	ldr	x2,[x1]
	cbz	x2,.oui
	adr	x3,wl_keybuf
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
	mov	x19,x20				; comme Se connecter
	bl	wl_connecter_sel
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x19 = ligne depliee : Se connecter
wl_connecter_sel:
	prologue
	adr	x0,wl_keylen
	ldr	x0,[x0]
	cmp	x0,8
	b.lo	.courte
	adr	x2,wl_scan
	mov	x0,48
	madd	x0,x19,x0,x2
	bl	wl_ssid_de
	adr	x0,wl_jssid
	strb	wzr,[x0]
	adr	x1,wl_tmp
	bl	str_cat
	adr	x0,wl_jkey
	strb	wzr,[x0]
	adr	x1,wl_keybuf
	bl	str_cat
	adr	x0,wl_selrow
	mov	x1,-1
	str	x1,[x0]
	mov	x0,2
	bl	wl_lancer
	epilogue
.courte:
	adr	x0,wl_msgp
	adr	x1,wl_s_courte
	bl	tr_x1
	str	x1,[x0]
	epilogue

wl_s_topic	db 'korin.net.wifi',0
wl_s_absent	db 'pas de Wi-Fi',0
wl_s_nonco	db 'non connect',0xC3,0xA9,0
wl_s_co		db 'connect',0xC3,0xA9,0
wl_s_recherche	db 'Recherche...',0
wl_s_connexion	db 'Connexion...',0
wl_s_connecte	db 'Connect',0xC3,0xA9,0
wl_s_refuse	db 'Cl',0xC3,0xA9,' refus',0xC3,0xA9,'e',0
wl_s_echec	db 'Connexion impossible',0
wl_s_courte	db 'Cl',0xC3,0xA9,' trop courte',0
wl_s_attente	db 'Recherche des r',0xC3,0xA9,'seaux...',0
wl_s_aucun	db 'Aucun r',0xC3,0xA9,'seau visible',0
wl_s_seco	db 'Se connecter',0
		align 4

macro wifilist_data {
		align 8
wl_w		dd 300				; largeur du volet
wl_job		dd 0
wl_jdone	dd 0
wl_jres		dd 0
wl_selrow	dd -1
wl_keylen	dd 0
wl_nvis		dd 0
wl_scanned	dd 0
wl_compte	dd 0
wl_sub		dd 0				; abonnement a korin.net.wifi (0 : pas de bus)
wl_maj		dd 0				; l'etat a ete publie : a relire
wl_msgp		dd 0
wl_nhits	dd 0
wl_wstat	rb 64				; WIFI_OP_STATUS : etat, RSSI, MAC, SSID (+16)
wl_hits		rb WL_HT_MAX * 32
wl_scan		rb 32 * 48
wl_scan2	rb 32 * 48
wl_known	rb 16 * 48
wl_keybuf	rb 72
wl_jssid	rb 40
wl_jkey		rb 72
wl_tmp		rb 40
wl_buf		rb 128
}
