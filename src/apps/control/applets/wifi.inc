; applets/wifi.inc - applet Wi-Fi du panneau de configuration (docs/RESEAU.md
; 4.5), dessine par lui-meme (ap_dessin, ap_clic) et rafraichi chaque seconde
; (ap_tick : les travaux du thread) ; l'etat est relu quand netd ou le pilote
; le publient sur le bus (korin.net.*, ap_net_maj), sans busd chaque seconde :
;  * Etat : reseau, adresse, passerelle, signal ; bouton Deconnecter ;
;  * Reseaux visibles (balayage dans un thread : l'interface ne gele pas ;
;    bouton du bas : Actualiser) : signal, cadenas, coche sur le reseau
;    courant ; un clic deplie la ligne : champ de cle (masque) si le reseau
;    est inconnu et securise, Se connecter (memorise) ;
;  * Reseaux connus : priorite (- / +), Oublier.
; Tout passe par NetKit (service "wifi") ; sans Wi-Fi : "Pas de Wi-Fi".

WF_VIS		= 6			; reseaux visibles montres
WF_KN		= 5			; reseaux connus montres
WF_RH		= 24			; hauteur d'une ligne
WF_HT_MAX	= 40			; zones cliquables
WF_FIELD_W	= 200

wf_ouvrir:
	prologue
	adr	x0,ap_dessin
	adr	x1,wf_dessin
	str	x1,[x0]
	adr	x0,ap_clic
	adr	x1,wf_clic
	str	x1,[x0]
	adr	x0,ap_tick
	adr	x1,wf_tick
	str	x1,[x0]
	adr	x0,ap_touche
	adr	x1,wf_touche
	str	x1,[x0]
	adr	x0,ap_bouton
	adr	x1,wf_s_actualiser
	str	x1,[x0]
	adr	x0,wf_sel
	mov	x1,-1
	str	x1,[x0]
	adr	x0,wf_keylen
	str	xzr,[x0]
	bl	wf_lire_etat
	bl	wf_lire_connus
	adr	x0,wf_thread_ok			; le thread des travaux (une fois)
	ldr	x0,[x0]
	cbnz	x0,.scan
	mov	x0,16384
	bl	malloc
	cbz	x0,.scan
	add	x1,x0,16384
	adr	x0,wf_travaux
	mov	x2,0
	sys	SYS_THREAD_CREATE
	tbnz	x0,63,.scan
	adr	x0,wf_thread_ok
	mov	x1,1
	str	x1,[x0]
.scan:	adr	x0,wf_st			; Wi-Fi present : un balayage
	ldr	w0,[x0]
	cmp	w0,WIFI_ST_READY
	b.lo	.out
	mov	x0,1
	bl	wf_lancer
.out:	epilogue

; Actualiser (bouton du bas)
wf_appliquer:
	prologue
	mov	x0,1
	bl	wf_lancer
	epilogue

; x0 = travail (1 balayage, 2 connexion) : au thread, s'il est libre
wf_lancer:
	adr	x1,wf_job
	ldr	x2,[x1]
	cbnz	x2,.r
	adr	x2,wf_done
	str	xzr,[x2]
	dmb	ish
	str	x0,[x1]
.r:	ret

; Thread des travaux : balayage (3 s), connexion (jusqu'a 20 s)
wf_travaux:
.loop:	mov	x0,50
	sys	SYS_SLEEP
	adr	x0,wf_job
	ldr	x19,[x0]
	cbz	x19,.loop
	cmp	x19,2
	b.eq	.join
	adr	x0,wf_scan2			; (dans une copie : l'interface lit
	mov	x1,32				;  wf_scan pendant ce temps)
	bl	wifi_scan
	b	.fini
.join:	adr	x0,wf_jssid
	adr	x1,wf_jkey
	mov	x2,1				; memoriser
	mov	x3,0
	bl	wifi_join_ex
.fini:	adr	x1,wf_res
	str	x0,[x1]
	dmb	ish
	adr	x1,wf_done
	str	x19,[x1]
	adr	x1,wf_job
	str	xzr,[x1]
	b	.loop

wf_lire_etat:
	prologue
	adr	x0,wf_st
	bl	wifi_status
	adr	x0,wf_ni
	bl	net_info
	epilogue

wf_lire_connus:
	prologue
	adr	x0,wf_known
	mov	x1,16
	bl	wifi_known
	cmp	x0,0
	csel	x0,xzr,x0,lt
	adr	x1,wf_known_n
	str	x0,[x1]
	epilogue

; chaque seconde, et a chaque korin.net.* -> x0 = 1 (redessiner)
wf_tick:
	prologue
	str	x19,[sp,-16]!
	adr	x0,ap_net_sub			; abonne : l'etat quand il est publie
	ldr	x0,[x0]
	cbz	x0,.lire
	adr	x0,ap_net_maj
	ldr	x1,[x0]
	cbz	x1,.lu
	str	xzr,[x0]
.lire:	bl	wf_lire_etat
.lu:
	adr	x0,wf_done			; un travail fini ?
	ldr	x19,[x0]
	cbz	x19,.oui
	str	xzr,[x0]
	adr	x0,wf_res
	ldr	x0,[x0]
	cmp	x19,1
	b.ne	.join
	tbnz	x0,63,.oui			; balayage : la liste
	cmp	x0,32
	mov	x1,32
	csel	x0,x0,x1,ls
	adr	x1,wf_scan_n
	str	x0,[x1]
	mov	x2,48
	mul	x2,x0,x2
	adr	x0,wf_scan
	adr	x1,wf_scan2
	bl	mem_move
	b	.oui
.join:	adr	x1,wf_s_connecte		; connexion : le message
	cbz	x0,.msg
	adr	x1,wf_s_refuse
	cmn	x0,13
	b.eq	.msg
	adr	x1,wf_s_introuvable
	cmn	x0,2
	b.eq	.msg
	adr	x1,wf_s_delai
	cmn	x0,110
	b.eq	.msg
	adr	x1,wf_s_echec
.msg:	adr	x2,ap_msg
	str	x1,[x2]
	cbnz	x0,.connus
	adr	x0,wf_sel			; connecte : la ligne se replie
	mov	x1,-1
	str	x1,[x0]
.connus:
	bl	wf_lire_connus
.oui:	mov	x0,1
	ldr	x19,[sp],16
	epilogue

; x0, x1, x2, x3 = rectangle, x4 = action, x5 = argument : zone cliquable
wf_hit_add:
	adr	x6,wf_nhits
	ldr	x7,[x6]
	cmp	x7,WF_HT_MAX
	b.hs	.r
	add	x8,x7,1
	str	x8,[x6]
	adr	x6,wf_hits
	add	x6,x6,x7,lsl 5
	stp	w0,w1,[x6]
	stp	w2,w3,[x6,8]
	stp	w4,w5,[x6,16]
.r:	ret

; x0 = entree de balayage -> x0 = 1 si son SSID est le reseau courant
wf_courant:
	adr	x1,wf_st
	ldr	w2,[x1]
	cmp	w2,WIFI_ST_CONNECTED
	b.ne	.non
	add	x1,x1,16
	ldrb	w2,[x0,32]			; longueur du SSID
	mov	x3,0
.c:	cmp	x3,x2
	b.eq	.fin
	ldrb	w4,[x0,x3]
	ldrb	w5,[x1,x3]
	cmp	w4,w5
	b.ne	.non
	add	x3,x3,1
	b	.c
.fin:	ldrb	w4,[x1,x3]
	cbnz	w4,.non
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = SSID (termine) -> x0 = 1 s'il est connu
wf_connu:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x19,wf_known
	adr	x0,wf_known_n
	ldr	x20,[x0]
.k:	cbz	x20,.non
	mov	x0,x19
	mov	x1,x21
	bl	str_eq
	cbnz	x0,.out
	add	x19,x19,48
	sub	x20,x20,1
	b	.k
.non:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree de balayage -> wf_tmp : son SSID termine par 0
wf_ssid_de:
	ldrb	w1,[x0,32]
	cmp	w1,32
	mov	w2,32
	csel	w1,w1,w2,ls
	adr	x2,wf_tmp
	mov	x3,0
.c:	cmp	x3,x1
	b.hs	.f
	ldrb	w4,[x0,x3]
	strb	w4,[x2,x3]
	add	x3,x3,1
	b	.c
.f:	strb	wzr,[x2,x3]
	ret

; x0 = entree de balayage -> x0 = 1 s'il faut une cle (securise, inconnu)
wf_besoin_cle:
	prologue
	ldrb	w1,[x0,34]
	cbz	w1,.non
	bl	wf_ssid_de
	adr	x0,wf_tmp
	bl	wf_connu
	cmp	x0,0
	cset	x0,eq
	epilogue
.non:	mov	x0,0
	epilogue

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------
wf_dessin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	adr	x0,wf_nhits
	str	xzr,[x0]
	bl	ui_jetons
	mov	x23,x0				; jetons
	mov	x19,SIDE_W+AP_MARGE		; x
	mov	x20,AP_HEAD+4			; y
	adr	x0,ui_w
	ldr	x21,[x0]
	sub	x21,x21,SIDE_W+2*AP_MARGE	; largeur
	adr	x0,wf_st
	ldr	w24,[x0]			; etat
	; --- etat -------------------------------------------------------------
	adr	x0,wf_s_absent
	cbz	w24,.etat
	adr	x0,wf_s_demarre
	cmp	w24,WIFI_ST_START
	b.eq	.etat
	adr	x0,wf_s_nonco
	cmp	w24,WIFI_ST_READY
	b.eq	.etat
	adr	x0,wf_s_enco
	cmp	w24,WIFI_ST_JOINING
	b.eq	.etat
	adr	x0,wf_buf			; "Connecte a << X >>"
	strb	wzr,[x0]
	adr	x1,wf_s_coa
	bl	tr_cat
	adr	x1,wf_st
	add	x1,x1,16
	bl	str_cat
	adr	x1,wf_s_guil
	bl	tr_cat
	adr	x0,wf_buf
.etat:	bl	ui_tr
	mov	x1,x19
	add	x2,x20,16
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font_b
	sub	x6,x21,BTN_W+8
	bl	ui_text
	cmp	w24,WIFI_ST_CONNECTED
	b.ne	.ligne2
	add	x0,x19,x21			; Deconnecter
	sub	x0,x0,BTN_W
	mov	x1,x20
	adr	x2,wf_s_deco
	mov	x3,0
	bl	ap_button
	add	x0,x19,x21
	sub	x0,x0,BTN_W
	mov	x1,x20
	mov	x2,BTN_W
	mov	x3,BTN_H
	mov	x4,1
	mov	x5,0
	bl	wf_hit_add
	adr	x22,wf_buf			; adresse, passerelle, signal
	strb	wzr,[x22]
	adr	x0,wf_ni
	ldr	w0,[x0]
	cbz	w0,.sig
	mov	x0,x22
	adr	x1,wf_s_adr
	bl	tr_cat
	mov	x1,x0
	adr	x0,wf_ni
	ldr	w0,[x0]
	bl	fmt_ip
	adr	x1,wf_s_pass
	bl	tr_cat
	mov	x1,x0
	adr	x0,wf_ni
	ldr	w0,[x0,8]
	bl	fmt_ip
	adr	x1,wf_s_point
	bl	str_cat
	b	.sg
.sig:	mov	x0,x22
.sg:	adr	x1,wf_s_signal
	bl	tr_cat
	mov	x1,x0
	adr	x0,wf_st
	ldrsw	x0,[x0,4]
	cmp	x0,0
	b.ge	.pos
	mov	w2,'-'
	strb	w2,[x1],1
	neg	x0,x0
.pos:	bl	fmt_dec
	adr	x1,wf_s_dbm
	bl	str_cat
	mov	x0,x22
	mov	x1,x19
	add	x2,x20,38
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,x21
	bl	ui_text
.ligne2:
	add	x20,x20,52
	cmp	w24,WIFI_ST_READY
	b.hs	.visibles
	adr	x0,wf_s_note			; pas de Wi-Fi : une note
	bl	tr0
	mov	x1,x19
	add	x2,x20,4
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,x21
	bl	ui_text
	b	.fin
	; --- reseaux visibles ------------------------------------------------------
.visibles:
	adr	x0,wf_s_visibles
	bl	wf_section
	adr	x0,wf_job			; balayage en cours
	ldr	x0,[x0]
	cmp	x0,1
	b.ne	.v0
	adr	x0,wf_s_recherche
	bl	tr0
	mov	x1,x19
	add	x1,x1,x21
	sub	x1,x1,90
	add	x2,x20,14
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.v0:	add	x20,x20,24
	mov	x22,0				; ligne
.v:	adr	x0,wf_scan_n
	ldr	x0,[x0]
	cmp	x22,x0
	b.hs	.vfin
	cmp	x22,WF_VIS
	b.hs	.vfin
	adr	x25,wf_scan
	mov	x0,48
	madd	x25,x22,x0,x25			; entree
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,WF_RH
	mov	x4,3
	mov	x5,x22
	bl	wf_hit_add
	adr	x0,wf_sel			; choisie : fond accentue
	ldr	x0,[x0]
	cmp	x0,x22
	b.ne	.vb
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,WF_RH
	ldr	w4,[x23,TJ_ACCENT_CLAIR]
	bl	ui_fill
.vb:	ldrsh	w0,[x25,36]			; signal
	bl	wd_level
	mov	x2,x0
	add	x0,x19,4
	add	x1,x20,5
	ldr	w3,[x23,TJ_TEXTE1]
	ldr	w4,[x23,TJ_LIGNE_FORTE]
	bl	wd_bars
	ldrb	w0,[x25,34]			; cadenas
	cbz	w0,.vs
	add	x0,x19,24
	add	x1,x20,7
	ldr	w2,[x23,TJ_TEXTE2]
	bl	wd_lock
.vs:	mov	x0,x25				; SSID
	bl	wf_ssid_de
	adr	x0,wf_tmp
	add	x1,x19,40
	add	x2,x20,17
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	sub	x6,x21,70
	bl	ui_text
	mov	x0,x25				; coche : le reseau courant
	bl	wf_courant
	cbz	x0,.vn
	add	x0,x19,x21
	sub	x0,x0,22
	add	x1,x20,8
	ldr	w2,[x23,TJ_ACCENT]
	bl	wd_check
.vn:	add	x20,x20,WF_RH
	adr	x0,wf_sel			; ligne depliee : cle, Se connecter
	ldr	x0,[x0]
	cmp	x0,x22
	b.ne	.vsuiv
	mov	x0,x25
	bl	wf_besoin_cle
	cbz	x0,.vbtn
	add	x0,x19,40			; champ de la cle (masque)
	add	x1,x20,3
	mov	x2,WF_FIELD_W
	mov	x3,26
	mov	x4,5
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	mov	w6,w5
	bl	ui_rbox
	add	x0,x19,41
	add	x1,x20,4
	mov	x2,WF_FIELD_W-2
	mov	x3,24
	mov	x4,4
	ldr	w5,[x23,TJ_CREUX]
	mov	w6,w5
	bl	ui_rbox
	adr	x0,wf_buf			; une puce par caractere, puis le
	mov	x1,0				; curseur
	adr	x2,wf_keylen
	ldr	x2,[x2]
	cmp	x2,24
	mov	x3,24
	csel	x2,x2,x3,ls
.pu:	cbz	x2,.puf
	mov	w3,0xE2				; U+2022
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
	adr	x0,wf_buf
	add	x1,x19,48
	add	x2,x20,21
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,WF_FIELD_W-12
	bl	ui_text
	adr	x0,wf_s_cle			; (libelle)
	bl	tr0
	add	x1,x19,40+WF_FIELD_W+8
	add	x2,x20,21
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.vbtn:	add	x0,x19,x21			; Se connecter
	sub	x0,x0,BTN_W
	add	x1,x20,2
	adr	x2,wf_s_seco
	mov	x3,1
	bl	ap_button
	add	x0,x19,x21
	sub	x0,x0,BTN_W
	add	x1,x20,2
	mov	x2,BTN_W
	mov	x3,BTN_H
	mov	x4,4
	mov	x5,x22
	bl	wf_hit_add
	add	x20,x20,34
.vsuiv:	add	x22,x22,1
	b	.v
.vfin:	cbnz	x22,.connus
	adr	x0,wf_s_aucun_vis
	bl	tr0
	mov	x1,x19
	add	x2,x20,16
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,x21
	bl	ui_text
	add	x20,x20,WF_RH
	; --- reseaux connus --------------------------------------------------------
.connus:
	add	x20,x20,8
	adr	x0,wf_s_connus
	bl	wf_section
	add	x20,x20,24
	mov	x22,0
.k:	adr	x0,wf_known_n
	ldr	x0,[x0]
	cmp	x22,x0
	b.hs	.kfin
	cmp	x22,WF_KN
	b.hs	.kfin
	adr	x25,wf_known
	mov	x0,48
	madd	x25,x22,x0,x25
	mov	x0,x25				; SSID
	add	x1,x19,4
	add	x2,x20,17
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	sub	x6,x21,230
	bl	ui_text
	adr	x0,wf_buf			; "priorite N"
	strb	wzr,[x0]
	adr	x1,wf_s_prio
	bl	tr_cat
	mov	x1,x0
	ldr	w0,[x25,40]
	bl	fmt_dec
	adr	x0,wf_buf
	add	x1,x19,x21
	sub	x1,x1,222
	add	x2,x20,17
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,70
	bl	ui_text
	add	x26,x19,x21			; - et +
	sub	x26,x26,146
	mov	x0,x26
	adr	x1,wf_s_moins
	mov	x2,x22
	mov	x3,5
	bl	wf_petit
	add	x0,x26,26
	adr	x1,wf_s_plus
	mov	x2,x22
	mov	x3,6
	bl	wf_petit
	add	x0,x19,x21			; Oublier
	sub	x0,x0,BTN_W
	add	x1,x20,1
	mov	x2,BTN_W
	mov	x3,22
	mov	x4,7
	mov	x5,x22
	bl	wf_hit_add
	add	x0,x19,x21
	sub	x0,x0,BTN_W-14
	adr	x1,wf_s_oublier
	add	x2,x20,17
	ldr	w3,[x23,TJ_DANGER]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	mov	x7,x1
	mov	x1,x0
	mov	x0,x7
	bl	tr0
	bl	ui_text
	add	x20,x20,WF_RH
	add	x22,x22,1
	b	.k
.kfin:	cbnz	x22,.fin
	adr	x0,wf_s_aucun_connu
	bl	tr0
	mov	x1,x19
	add	x2,x20,16
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,x21
	bl	ui_text
.fin:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (wf_dessin : x19 = x, x20 = y, x21 = largeur, x23 = jetons) x0 = titre de
; section : texte gras et trait
wf_section:
	prologue
	bl	ui_tr
	mov	x1,x19
	add	x2,x20,14
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x0,x19
	add	x1,x20,20
	mov	x2,x21
	mov	x3,1
	ldr	w4,[x23,TJ_LIGNE]
	bl	ui_fill
	epilogue

; (wf_dessin) x0 = x, x1 = texte ("-" / "+"), x2 = argument, x3 = action :
; petit bouton de 20 x 18 sur la ligne x20
wf_petit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x24,x23				; (jetons)
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	mov	x23,x3
	mov	x0,x19
	add	x1,x20,3
	mov	x2,20
	mov	x3,18
	mov	x4,x23
	mov	x5,x22
	bl	wf_hit_add
	mov	x0,x19
	add	x1,x20,3
	mov	x2,20
	mov	x3,18
	mov	x4,4
	ldr	w5,[x24,TJ_ELEVE]
	ldr	w6,[x24,TJ_ELEVE_BAS]
	bl	ui_rbox
	mov	x0,x21
	add	x1,x19,6
	add	x2,x20,17
	ldr	w3,[x24,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Clics et clavier
; ---------------------------------------------------------------------------

; x0, x1 = clic -> x0 = 1 si a redessiner
wf_clic:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x2,wf_hits
	adr	x3,wf_nhits
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
	ldp	w19,w20,[x2,16]			; action, argument
	b	.action
.n:	add	x2,x2,32
	sub	x3,x3,1
	b	.t
.action:
	cmp	w19,1
	b.eq	.deco
	cmp	w19,3
	b.eq	.choix
	cmp	w19,4
	b.eq	.seco
	cmp	w19,5
	b.eq	.moins
	cmp	w19,6
	b.eq	.plus
	cmp	w19,7
	b.eq	.oubli
	b	.rien
.deco:	bl	wifi_leave
	adr	x0,ap_msg
	adr	x1,wf_s_decoche
	str	x1,[x0]
	b	.oui
.choix:	adr	x0,wf_sel			; une autre ligne : cle effacee
	ldr	x1,[x0]
	cmp	x1,x20
	b.eq	.oui
	str	x20,[x0]
	adr	x0,wf_keylen
	str	xzr,[x0]
	b	.oui
.seco:	mov	x0,x20
	bl	wf_connecter
	b	.oui
.moins:	mov	x21,-1
	b	.prio
.plus:	mov	x21,1
.prio:	adr	x0,wf_known
	mov	x1,48
	madd	x22,x20,x1,x0
	ldr	w1,[x22,40]
	add	w1,w1,w21
	cmp	w1,0
	csel	w1,wzr,w1,lt
	mov	x0,x22
	bl	wifi_priority
	bl	wf_lire_connus
	b	.oui
.oubli:	adr	x0,wf_known
	mov	x1,48
	madd	x0,x20,x1,x0
	bl	wifi_forget
	bl	wf_lire_connus
	b	.oui
.rien:	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ligne visible : connexion (dans le thread) avec la cle saisie
wf_connecter:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x1,wf_scan
	mov	x2,48
	madd	x19,x0,x2,x1
	adr	x0,wf_job			; un travail en cours : plus tard
	ldr	x0,[x0]
	cbnz	x0,.out
	mov	x0,x19
	bl	wf_ssid_de
	adr	x0,wf_jssid
	adr	x1,wf_tmp
	bl	str_cat_z
	adr	x0,wf_jkey			; la cle saisie (vide : connu ou ouvert)
	strb	wzr,[x0]
	mov	x0,x19
	bl	wf_besoin_cle
	cbz	x0,.go
	adr	x0,wf_keylen
	ldr	x0,[x0]
	cmp	x0,8				; WPA : 8 a 63 caracteres
	b.lo	.courte
	adr	x0,wf_jkey
	adr	x1,wf_key
	bl	str_cat_z
.go:	adr	x0,ap_msg
	adr	x1,wf_s_connexion
	str	x1,[x0]
	mov	x0,2
	bl	wf_lancer
	b	.out
.courte:
	adr	x0,ap_msg
	adr	x1,wf_s_courte
	str	x1,[x0]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = chaine : copie (destination videe d'abord)
str_cat_z:
	strb	wzr,[x0]
	b	str_cat

; x0 = evenement clavier -> x0 = 1 si pris (champ de la cle)
wf_touche:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,wf_sel			; une ligne depliee ?
	ldr	x20,[x0]
	tbnz	x20,63,.non
	ldr	w0,[x19,EV_CODE]
	cmp	w0,KEY_ENTER
	b.eq	.entree
	adr	x0,wf_scan
	mov	x1,48
	madd	x0,x20,x1,x0
	bl	wf_besoin_cle
	cbz	x0,.non
	ldr	w0,[x19,EV_CODE]
	cmp	w0,KEY_BACKSPACE
	b.eq	.efface
	ldr	w0,[x19,EV_CHAR]		; un caractere
	cmp	w0,32
	b.lo	.non
	adr	x1,wf_keylen
	ldr	x2,[x1]
	cmp	x2,63
	b.hs	.oui
	adr	x1,wf_key
	add	x1,x1,x2
	bl	utf8_put			; (octets)
	adr	x1,wf_keylen
	ldr	x2,[x1]
	add	x2,x2,x0
	cmp	x2,63
	b.hi	.oui
	str	x2,[x1]
	adr	x1,wf_key
	strb	wzr,[x1,x2]
	b	.oui
.efface:
	adr	x1,wf_keylen			; (dernier caractere UTF-8)
	ldr	x2,[x1]
	cbz	x2,.oui
	adr	x3,wf_key
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
	mov	x0,x20
	bl	wf_connecter
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

wf_titre	db 'Wi-Fi',0
wf_s_actualiser	db 'Actualiser',0
wf_s_absent	db 'Pas de Wi-Fi sur cet appareil',0
wf_s_note	db 'La puce Wi-Fi ne r',0xC3,0xA9,'pond pas (ou QEMU) : voir le journal.',0
wf_s_demarre	db 'Mise en route du Wi-Fi...',0
wf_s_nonco	db 'Non connect',0xC3,0xA9,0
wf_s_enco	db 'Connexion en cours...',0
wf_s_coa	db 'Connect',0xC3,0xA9,' ',0xC3,0xA0,' ',0xC2,0xAB,' ',0
wf_s_guil	db ' ',0xC2,0xBB,0
wf_s_deco	db 'D',0xC3,0xA9,'connecter',0
wf_s_adr	db 'Adresse ',0
wf_s_pass	db ' ',0xC2,0xB7,' passerelle ',0
wf_s_point	db ' ',0xC2,0xB7,' ',0
wf_s_signal	db 'signal ',0
wf_s_dbm	db ' dBm',0
wf_s_visibles	db 'R',0xC3,0xA9,'seaux visibles',0
wf_s_recherche	db 'Recherche...',0
wf_s_aucun_vis	db 'Aucun r',0xC3,0xA9,'seau visible (Actualiser).',0
wf_s_connus	db 'R',0xC3,0xA9,'seaux connus',0
wf_s_aucun_connu db 'Aucun r',0xC3,0xA9,'seau m',0xC3,0xA9,'moris',0xC3,0xA9,'.',0
wf_s_prio	db 'priorit',0xC3,0xA9,' ',0
wf_s_moins	db '-',0
wf_s_plus	db '+',0
wf_s_oublier	db 'Oublier',0
wf_s_seco	db 'Se connecter',0
wf_s_cle	db 'cl',0xC3,0xA9,0
wf_s_connexion	db 'Connexion...',0
wf_s_connecte	db 'Connect',0xC3,0xA9,'.',0
wf_s_refuse	db 'Cl',0xC3,0xA9,' refus',0xC3,0xA9,'e.',0
wf_s_introuvable db 'R',0xC3,0xA9,'seau introuvable.',0
wf_s_delai	db 'D',0xC3,0xA9,'lai d',0x27,'attente d',0xC3,0xA9,'pass',0xC3,0xA9,'.',0
wf_s_echec	db 'Connexion impossible.',0
wf_s_courte	db 'Cl',0xC3,0xA9,' trop courte (8 caract',0xC3,0xA8,'res au moins).',0
wf_s_decoche	db 'D',0xC3,0xA9,'connect',0xC3,0xA9,'.',0
		align 4

macro wf_data {
		align 8
wf_job		dd 0				; travail demande (1 balayage, 2 connexion)
wf_done		dd 0				; travail fini (son genre)
wf_res		dd 0				; son resultat
wf_thread_ok	dd 0
wf_sel		dd -1				; ligne visible depliee
wf_keylen	dd 0
wf_scan_n	dd 0
wf_known_n	dd 0
wf_nhits	dd 0
wf_st		rb 64
wf_ni		rb 32
wf_hits		rb WF_HT_MAX * 32
wf_scan		rb 32 * 48
wf_scan2	rb 32 * 48
wf_known	rb 16 * 48
wf_key		rb 72
wf_jssid	rb 40
wf_jkey		rb 72
wf_tmp		rb 40
wf_buf		rb 160
}
