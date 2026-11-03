; applets/remote.inc - applet Acces a distance du panneau de configuration
; (docs/RESEAU.md) :
;  * Services : quatre cases (lignes du cadre) - Terminal a distance (telnetd),
;    Bureau a distance (vncd), Fenetres a distance (rdpd), Partage de fichiers
;    (ftpd). Elles lisent et ecrivent SD0:/etc/services.txt comme Setup : la
;    ligne « SD0:/sys/svc/<nom> » lance le service au demarrage, la meme
;    precedee de « # » le coupe ; les autres lignes sont gardees. Effet au
;    prochain demarrage.
;  * Mot de passe : deux champs masques (ap_extra, sous les cases ; clic ou
;    Tab pour passer de l'un a l'autre). Appliquer ecrit « password = ... »
;    dans SD0:/etc/remote.txt (inc/remotepw.inc) ; vides : il ne change pas ;
;    differents : un message. Effet tout de suite (relu a chaque connexion).
;  * Un service coche sans mot de passe defini : un avertissement.

RA_N		= 4			; services
RA_LW		= 130			; libelle d'un champ
RA_FW		= 220			; un champ
RA_FH		= 26
RA_RH		= 34			; une ligne de champ
RA_COLW		= 320			; les cases : une colonne large
RA_Y		= AP_HEAD+4+(RA_N+1)*ROW_H+10	; haut du bloc du mot de passe

ra_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,ap_extra
	adr	x1,ra_dessin
	str	x1,[x0]
	adr	x0,ap_xclic
	adr	x1,ra_xclic
	str	x1,[x0]
	adr	x0,ap_touche
	adr	x1,ra_touche
	str	x1,[x0]
	adr	x0,ap_colw
	mov	x1,RA_COLW
	str	x1,[x0]
	adr	x0,ra_len
	stp	xzr,xzr,[x0]			; champs vides, le premier choisi
	adr	x0,ra_champ
	str	xzr,[x0]
	adr	x0,ra_pw0
	strb	wzr,[x0]
	adr	x0,ra_pw1
	strb	wzr,[x0]
	bl	ra_svc_lire
	bl	rp_lire
	adr	x1,ra_pwok
	str	x0,[x1]
	adr	x0,ra_s_services
	mov	x1,R_TITRE
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	applet_row
	mov	x19,0				; les cases, dans l'ordre de ra_libs
.c:	cmp	x19,RA_N
	b.hs	.out
	adr	x0,ra_libs
	ldrsw	x1,[x0,x19,lsl 2]
	add	x0,x0,x1
	mov	x1,R_CASE
	mov	x2,0
	adr	x3,ra_en
	ldrb	w3,[x3,x19]
	mov	x4,x19
	bl	applet_row
	add	x19,x19,1
	b	.c
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = service -> x0 = 1 si sa case est cochee (lignes 1 a RA_N)
ra_coche:
	prologue
	add	x0,x0,1
	bl	row_ptr
	ldrb	w0,[x0,10]
	epilogue

; ---------------------------------------------------------------------------
;  SD0:/etc/services.txt
; ---------------------------------------------------------------------------

; x0 = debut de ligne (apres « # » et espaces) -> x0 = service (-1 : aucun)
ra_match:
	mov	x9,0
.s:	cmp	x9,RA_N
	b.hs	.non
	adr	x10,ra_chemins
	ldrsw	x11,[x10,x9,lsl 2]
	add	x10,x10,x11			; « SD0:/sys/svc/<nom> »
	mov	x11,x0
.c:	ldrb	w12,[x10],1
	cbz	w12,.fin
	ldrb	w13,[x11],1
	cmp	w12,w13
	b.eq	.c
	b	.n
.fin:	ldrb	w13,[x11]			; puis fin de ligne ou espace
	cbz	w13,.oui
	cmp	w13,10
	b.eq	.oui
	cmp	w13,13
	b.eq	.oui
	cmp	w13,' '
	b.eq	.oui
	cmp	w13,9
	b.eq	.oui
.n:	add	x9,x9,1
	b	.s
.oui:	mov	x0,x9
	ret
.non:	mov	x0,-1
	ret

; le fichier dans ap_file (termine par 0) -> x0 = octets lus
ra_svc_charger:
	prologue
	str	x19,[sp,-16]!
	adr	x0,ra_p_services
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x1,0
	tbnz	x0,63,.fin
	mov	x19,x0
	adr	x1,ap_file
	mov	x2,AP_FILE_MAX-1
	sys	SYS_READ
	cmp	x0,0
	csel	x1,x0,xzr,gt
	str	x1,[sp,-16]!
	mov	x0,x19
	sys	SYS_CLOSE
	ldr	x1,[sp],16
.fin:	adr	x0,ap_file
	strb	wzr,[x0,x1]
	mov	x0,x1
	ldr	x19,[sp],16
	epilogue

; ra_en : 1 pour chaque service lance au demarrage (une ligne sans « # »)
ra_svc_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,ra_en
	str	wzr,[x0]
	bl	ra_svc_charger
	adr	x19,ap_file
.l:	ldrb	w0,[x19]
	cbz	w0,.out
	mov	x20,x19				; « # » et espaces de tete
	mov	x21,0				; 1 : commentee
.d:	ldrb	w0,[x20]
	cmp	w0,' '
	b.eq	.d1
	cmp	w0,'#'
	b.ne	.m
	mov	x21,1
.d1:	add	x20,x20,1
	b	.d
.m:	mov	x0,x20
	bl	ra_match
	tbnz	x0,63,.suiv
	cbnz	x21,.suiv
	adr	x1,ra_en
	mov	w2,1
	strb	w2,[x1,x0]
.suiv:	ldrb	w0,[x19]			; ligne suivante
	cbz	w0,.out
	add	x19,x19,1
	cmp	w0,10
	b.ne	.suiv
	b	.l
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les cases -> SD0:/etc/services.txt (regle de Setup : « # » devant une ligne
; de service coupe, sans le « # » lance ; un service coche absent : ajoute a la
; fin) -> x0 = 0 ou erreur
ra_svc_ecrire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,ra_vu
	str	wzr,[x0]
	bl	ra_svc_charger
	adr	x19,ap_file			; lecture
	adr	x22,ap_out			; ecriture
.l:	ldrb	w0,[x19]
	cbz	w0,.ajout
	adr	x0,ap_out			; (borne : ap_out a 128 octets de plus)
	sub	x0,x22,x0
	mov	x1,AP_FILE_MAX+64
	cmp	x0,x1
	b.hs	.ajout
	mov	x20,x19				; apres « # » et espaces
.d:	ldrb	w0,[x20]
	cmp	w0,' '
	b.eq	.d1
	cmp	w0,'#'
	b.ne	.m
.d1:	add	x20,x20,1
	b	.d
.m:	mov	x0,x20
	bl	ra_match
	tbnz	x0,63,.copie
	mov	x23,x0
	adr	x1,ra_vu
	mov	w2,1
	strb	w2,[x1,x23]
	mov	x0,x23
	bl	ra_coche
	cbnz	x0,.sans
	mov	w0,'#'				; coupe
	strb	w0,[x22],1
.sans:	mov	x19,x20				; la ligne sans son ancien prefixe
.copie:	ldrb	w0,[x19]			; jusqu'au saut de ligne compris
	cbz	w0,.ajout
	strb	w0,[x22],1
	add	x19,x19,1
	cmp	w0,10
	b.ne	.copie
	b	.l
.ajout:	mov	x23,0				; les services coches absents
.a:	cmp	x23,RA_N
	b.hs	.ecrit
	adr	x0,ra_vu
	ldrb	w0,[x0,x23]
	cbnz	w0,.an
	mov	x0,x23
	bl	ra_coche
	cbz	x0,.an
	adr	x0,ap_out			; (ligne precedente terminee)
	cmp	x22,x0
	b.eq	.ac
	ldurb	w0,[x22,-1]
	cmp	w0,10
	b.eq	.ac
	mov	w0,10
	strb	w0,[x22],1
.ac:	adr	x0,ra_chemins
	ldrsw	x1,[x0,x23,lsl 2]
	add	x1,x0,x1
.ap:	ldrb	w0,[x1],1
	cbz	w0,.al
	strb	w0,[x22],1
	b	.ap
.al:	mov	w0,10
	strb	w0,[x22],1
.an:	add	x23,x23,1
	b	.a
.ecrit:	adr	x0,ra_p_services
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x23,x0
	adr	x1,ap_out
	sub	x2,x22,x1
	sys	SYS_WRITE
	mov	x24,x0
	mov	x0,x23
	sys	SYS_CLOSE
	mov	x0,x24
	cmp	x0,0
	csel	x0,xzr,x0,ge
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Appliquer
; ---------------------------------------------------------------------------
ra_appliquer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,ra_len
	ldp	x19,x20,[x0]			; longueurs saisies
	orr	x0,x19,x20
	mov	x21,0				; 1 : mot de passe a ecrire
	cbz	x0,.svc
	adr	x1,ra_s_differents		; les deux champs : egaux
	cmp	x19,x20
	b.ne	.msg
	adr	x2,ra_pw0
	adr	x3,ra_pw1
	mov	x4,0
.cmp:	cmp	x4,x19
	b.hs	.bords
	ldrb	w5,[x2,x4]
	ldrb	w6,[x3,x4]
	cmp	w5,w6
	b.ne	.msg
	add	x4,x4,1
	b	.cmp
.bords:	adr	x1,ra_s_espaces			; (remote.txt perd les espaces autour)
	ldrb	w5,[x2]
	cmp	w5,' '
	b.eq	.msg
	sub	x4,x19,1
	ldrb	w5,[x2,x4]
	cmp	w5,' '
	b.eq	.msg
	mov	x21,1
.svc:	bl	ra_svc_ecrire
	adr	x1,ra_s_ecriture
	cbnz	x0,.msg
	cbz	x21,.ok
	adr	x0,ra_buf			; « password = ... »
	strb	wzr,[x0]
	adr	x1,ra_s_cle
	bl	str_cat
	adr	x1,ra_pw0
	bl	str_cat
	mov	w1,10
	strb	w1,[x0],1
	strb	wzr,[x0]
	adr	x0,rp_s_fichier
	adr	x1,ra_buf
	bl	file_write
	adr	x1,ra_s_ecriture
	cbnz	x0,.msg
	adr	x0,ra_len			; champs vides, mot de passe relu
	stp	xzr,xzr,[x0]
	adr	x0,ra_pw0
	strb	wzr,[x0]
	adr	x0,ra_pw1
	strb	wzr,[x0]
	adr	x0,ra_buf
	strb	wzr,[x0]
	adr	x0,ra_champ
	str	xzr,[x0]
	bl	rp_lire
	adr	x1,ra_pwok
	str	x0,[x1]
	adr	x1,ra_s_okpw
	b	.msg
.ok:	adr	x1,ra_s_ok
.msg:	adr	x0,ap_msg
	str	x1,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Dessin (ap_extra : sous les cases, sur toute la largeur du volet)
; ---------------------------------------------------------------------------
ra_dessin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	ui_jetons
	mov	x23,x0
	mov	x19,SIDE_W+AP_MARGE		; x
	mov	x20,RA_Y			; y
	adr	x0,ui_w
	ldr	x21,[x0]
	sub	x21,x21,SIDE_W+2*AP_MARGE	; largeur
	adr	x0,ra_s_commun			; titre (comme ceux des lignes)
	bl	ui_tr
	mov	x1,x19
	add	x2,x20,18
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,x21
	bl	ui_text
	add	x20,x20,ROW_H+2
	mov	x22,0				; les deux champs
.f:	cmp	x22,2
	b.hs	.etat
	adr	x0,ra_libs_mdp
	ldrsw	x1,[x0,x22,lsl 2]
	add	x0,x0,x1
	bl	ui_tr
	mov	x1,x19
	add	x2,x20,18
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,RA_LW-8
	bl	ui_text
	mov	x0,x22
	mov	x1,x20
	bl	ra_champ_dessin
	add	x20,x20,RA_RH
	add	x22,x22,1
	b	.f
.etat:	add	x20,x20,4			; mot de passe defini ou pas
	adr	x0,ra_pwok
	ldr	x0,[x0]
	adr	x24,ra_s_defini
	cbnz	x0,.e
	adr	x24,ra_s_aucun
.e:	mov	x0,x24
	bl	ui_tr
	mov	x1,x19
	add	x2,x20,14
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,x21
	bl	ui_text
	add	x20,x20,20
	adr	x0,ra_pwok			; un service coche sans mot de passe
	ldr	x0,[x0]
	cbnz	x0,.notes
	mov	x22,0
.k:	cmp	x22,RA_N
	b.hs	.notes
	mov	x0,x22
	bl	ra_coche
	add	x22,x22,1
	cbz	x0,.k
	adr	x0,ra_s_avert
	bl	ui_tr
	mov	x1,x19
	add	x2,x20,14
	ldr	w3,[x23,TJ_DANGER]
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,x21
	bl	ui_text
	add	x20,x20,20
.notes:	add	x20,x20,6
	adr	x0,ra_s_note1
	bl	ui_tr
	mov	x1,x19
	add	x2,x20,14
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,x21
	bl	ui_text
	add	x20,x20,18
	adr	x0,ra_s_note2
	bl	ui_tr
	mov	x1,x19
	add	x2,x20,14
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,x21
	bl	ui_text
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ra_dessin : x19 = x, x23 = jetons) x0 = champ, x1 = y de sa ligne : le
; champ masque, comme la cle du Wi-Fi (une puce par caractere, le curseur dans
; le champ choisi)
ra_champ_dessin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x20,x1
	adr	x0,ra_champ
	ldr	x0,[x0]
	cmp	x0,x21
	cset	x22,eq				; x22 : 1 si choisi
	add	x0,x19,RA_LW			; contour (accent : choisi)
	mov	x1,x20
	mov	x2,RA_FW
	mov	x3,RA_FH
	mov	x4,5
	ldr	w5,[x23,TJ_LIGNE_FORTE]
	cbz	x22,.c
	ldr	w5,[x23,TJ_ACCENT]
.c:	mov	w6,w5
	bl	ui_rbox
	add	x0,x19,RA_LW+1
	add	x1,x20,1
	mov	x2,RA_FW-2
	mov	x3,RA_FH-2
	mov	x4,4
	ldr	w5,[x23,TJ_CREUX]
	mov	w6,w5
	bl	ui_rbox
	adr	x0,ra_buf			; puces (U+2022), puis le curseur
	adr	x1,ra_len
	ldr	x2,[x1,x21,lsl 3]
	cmp	x2,28
	mov	x3,28
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
.puf:	cbz	x22,.z
	mov	w3,'|'
	strb	w3,[x0],1
.z:	strb	wzr,[x0]
	adr	x0,ra_buf
	add	x1,x19,RA_LW+8
	add	x2,x20,18
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,RA_FW-12
	bl	ui_text
	adr	x0,ra_buf			; (ra_buf ne garde rien)
	strb	wzr,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Clics et clavier
; ---------------------------------------------------------------------------

; (ap_xclic) x0, x1 = clic -> x0 = 1 si sur un champ (il devient le choisi)
ra_xclic:
	mov	x2,SIDE_W+AP_MARGE+RA_LW
	cmp	x0,x2
	b.lt	.non
	add	x2,x2,RA_FW
	cmp	x0,x2
	b.ge	.non
	mov	x2,RA_Y+ROW_H+2			; haut du premier champ
	subs	x1,x1,x2
	b.lt	.non
	mov	x2,RA_RH
	udiv	x3,x1,x2
	cmp	x3,2
	b.hs	.non
	msub	x1,x3,x2,x1
	cmp	x1,RA_FH
	b.hs	.non
	adr	x0,ra_champ
	str	x3,[x0]
	adr	x0,ap_msg
	str	xzr,[x0]
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; (ap_touche) x0 = evenement clavier -> x0 = 1 si pris (le champ choisi ;
; Tab : l'autre ; Entree reste au cadre : Appliquer)
ra_touche:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,ra_champ
	ldr	x20,[x0]			; champ choisi
	ldr	w0,[x19,EV_CODE]
	cmp	w0,KEY_TAB
	b.eq	.tab
	cmp	w0,KEY_BACKSPACE
	b.eq	.efface
	ldr	w0,[x19,EV_CHAR]		; un caractere
	cmp	w0,32
	b.lo	.non
	cmp	w0,127
	b.eq	.non
	adr	x1,ra_len
	ldr	x2,[x1,x20,lsl 3]
	cmp	x2,RP_MAX
	b.hs	.oui
	adr	x1,ra_tmp			; (octets ; trop long : refuse)
	bl	utf8_put
	adr	x1,ra_len
	ldr	x2,[x1,x20,lsl 3]
	add	x3,x2,x0
	cmp	x3,RP_MAX
	b.hi	.oui
	str	x3,[x1,x20,lsl 3]
	bl	ra_pw_de
	adr	x4,ra_tmp
.cp:	cbz	x0,.cz
	ldrb	w5,[x4],1
	strb	w5,[x1,x2]
	add	x2,x2,1
	sub	x0,x0,1
	b	.cp
.cz:	strb	wzr,[x1,x2]
	b	.chg
.efface:
	adr	x1,ra_len			; (dernier caractere UTF-8)
	ldr	x2,[x1,x20,lsl 3]
	cbz	x2,.oui
	mov	x0,0
	bl	ra_pw_de
	mov	x3,x1
	adr	x1,ra_len
.e:	sub	x2,x2,1
	cbz	x2,.ef
	ldrb	w4,[x3,x2]
	and	w4,w4,0xC0
	cmp	w4,0x80
	b.eq	.e
.ef:	str	x2,[x1,x20,lsl 3]
	strb	wzr,[x3,x2]
	b	.chg
.tab:	eor	x20,x20,1
	adr	x0,ra_champ
	str	x20,[x0]
	b	.oui
.chg:	adr	x0,ap_msg			; (une saisie efface le message)
	str	xzr,[x0]
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; (ra_touche : x20 = champ) -> x1 = son texte ; x0, x2 gardes
ra_pw_de:
	adr	x1,ra_pw0
	cbz	x20,.r
	adr	x1,ra_pw1
.r:	ret

ra_titre	db 'Acc',0xC3,0xA8,'s ',0xC3,0xA0,' distance',0
ra_s_liste	db 'Acc',0xC3,0xA8,'s distant',0		; (la liste : plus court)
ra_s_commun	db 'Mot de passe commun',0
ra_s_services	db 'Services',0
ra_s_telnet	db 'Terminal ',0xC3,0xA0,' distance (telnet)',0
ra_s_vnc	db 'Bureau ',0xC3,0xA0,' distance (VNC)',0
ra_s_rdp	db 'Fen',0xC3,0xAA,'tres ',0xC3,0xA0,' distance (RDP)',0
ra_s_ftp	db 'Partage de fichiers (FTP)',0
ra_s_mdp	db 'Mot de passe',0
ra_s_confirmer	db 'Confirmer',0
ra_s_defini	db 'Un mot de passe est d',0xC3,0xA9,'fini. Champs vides : il ne change pas.',0
ra_s_aucun	db 'Aucun mot de passe d',0xC3,0xA9,'fini.',0
ra_s_avert	db 'Sans mot de passe, les services activ',0xC3,0xA9,'s refusent toute connexion.',0
ra_s_note1	db 'Services : effet au prochain d',0xC3,0xA9,'marrage.',0
ra_s_note2	db 'Mot de passe : effet imm',0xC3,0xA9,'diat (relu ',0xC3,0xA0,' chaque connexion).',0
ra_s_differents	db 'Les deux mots de passe sont diff',0xC3,0xA9,'rents.',0
ra_s_espaces	db 'Pas d',0x27,'espace au d',0xC3,0xA9,'but ni ',0xC3,0xA0,' la fin du mot de passe.',0
ra_s_ecriture	db 'Enregistrement impossible.',0
ra_s_ok		db 'Enregistr',0xC3,0xA9,'. Services : au prochain d',0xC3,0xA9,'marrage.',0
ra_s_okpw	db 'Mot de passe chang',0xC3,0xA9,'. Services : au prochain d',0xC3,0xA9,'marrage.',0
ra_s_cle	db 'password = ',0
ra_p_services	db 'SD0:/etc/services.txt',0
ra_c_telnet	db 'SD0:/sys/svc/telnetd',0
ra_c_vnc	db 'SD0:/sys/svc/vncd',0
ra_c_rdp	db 'SD0:/sys/svc/rdpd',0
ra_c_ftp	db 'SD0:/sys/svc/ftpd',0
		align 4
ra_libs		dw ra_s_telnet-ra_libs, ra_s_vnc-ra_libs, ra_s_rdp-ra_libs, ra_s_ftp-ra_libs
ra_chemins	dw ra_c_telnet-ra_chemins, ra_c_vnc-ra_chemins, ra_c_rdp-ra_chemins, ra_c_ftp-ra_chemins
ra_libs_mdp	dw ra_s_mdp-ra_libs_mdp, ra_s_confirmer-ra_libs_mdp
		align 4

macro ra_data {
		align 8
ra_len		dd 0, 0				; longueurs saisies (contigues)
ra_champ	dd 0				; champ choisi (0, 1)
ra_pwok		dd 0				; longueur du mot de passe de remote.txt
ra_en		rb 4				; services au demarrage (lus)
ra_vu		rb 4				; (ecriture : lignes trouvees)
ra_pw0		rb RP_MAX+1
ra_pw1		rb RP_MAX+1
ra_tmp		rb 8
ra_buf		rb 128
}
