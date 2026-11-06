; notifyd - les notifications (comme celui d'Onyx), lance par la session
; (SD0:/etc/session, SD0:/etc/session.poche). Service « notify »
; (inc/notify.inc ; commande SD0:/bin/notify) :
;  * op 1 : donnees = titre, 0, texte, 0 [, app, 0] : une notification (app :
;    le paquet SD0:/apps/<app>.app qui l'envoie ; sans : le nom du process
;    appelant). Elle va dans l'historique (non lue) et, sauf « Ne pas
;    deranger » (korin.notify.dnd = 1), s'affiche en haut a droite, sous la
;    barre de menus, pendant 4 s, dans une fenetre sans cadre, au-dessus des
;    autres, qui ne prend pas le clavier ; un clic la ferme. En mode poche
;    (korin.shell.mode = poche, ou shell = poche dans SD0:/etc/system.ini) elle
;    prend toute la largeur sous la barre. Elles attendent leur tour (file de
;    NOTE_MAX).
;  * op 2 : a = rang (0 : la plus recente) -> nombre de notifications gardees ;
;    donnees : l'entree NH_* de ce rang (s'il existe) ;
;  * op 3 : a = id -> 0, ou -2 si elle n'y est plus : l'efface ;
;  * op 4 : tout effacer ; op 5 : toutes lues.
; L'historique garde les NH_MAX dernieres. Le nombre de non lues est publie
; sur le bus : korin.notify.unread (garde ; docs/MODE-POCHE.md). Les reglages
; rapides (SD0:/sys/quick) montrent l'historique.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/buskit/buskit.inc'

NOTE_MAX	= 8
NOTE_TITRE	= 0			; 64 octets
NOTE_TEXTE	= 64			; 192 octets
NOTE_APP	= 256			; 32 octets
NOTE_TAILLE	= 512
NW		= 330
NH		= 74
PH		= 64			; (poche) hauteur
MARGE_D		= 8			; a droite
DUREE		= 4000			; ms
NH_MAX		= 32			; historique
E_INVAL		= -22
E_NOENT		= -2

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so
include '../../inc/notify.inc'

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	adr	x0,s_nom
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	bl	lire_systeme
	bl	publier				; korin.notify.unread = 0
.loop:	adr	x0,affichee			; une notification a l'ecran :
	ldr	x0,[x0]				; ses evenements (clic), son temps
	cbz	x0,.attente
	mov	x0,50
	bl	ui_event
	cmp	x0,WEV_MOUSE
	b.ne	.temps
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,1				; appui : fermee
	b.eq	.fermer
.temps:	sys	SYS_TIME_MS
	adr	x1,fin
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.fermer
	mov	x2,0				; (les demandes, sans attendre)
	b	.req
.fermer:
	bl	ui_close
	adr	x0,affichee
	str	xzr,[x0]
	bl	suivante
	b	.loop
.attente:
	movl	x2,60000			; rien a l'ecran : attendre une demande
.req:	adr	x0,canal
	ldr	x0,[x0]
	adr	x1,msg
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbz	x0,.loop
	mov	x19,x0
	adr	x20,msg
	ldr	x0,[x20,WM_OP]
	cmp	x0,NOTIFY_OP
	b.eq	.notif
	cmp	x0,NOTIFY_OP_LIST
	b.eq	.liste
	cmp	x0,NOTIFY_OP_DELETE
	b.eq	.eff
	cmp	x0,NOTIFY_OP_CLEAR
	b.eq	.tout
	cmp	x0,NOTIFY_OP_READ
	b.eq	.lues
	mov	x0,E_INVAL
	b	.rep
.notif:	bl	historique
	bl	publier
	bl	dnd				; Ne pas deranger : pas de bulle
	cbnz	x0,.rep0
	bl	ajouter
	adr	x0,affichee
	ldr	x0,[x0]
	cbnz	x0,.rep0
	bl	suivante
	b	.rep0
.liste:	ldr	x0,[x20,WM_A]
	bl	h_copie
	b	.rep
.eff:	ldr	x0,[x20,WM_A]
	bl	h_effacer
	str	x0,[sp,-16]!
	bl	publier
	ldr	x0,[sp],16
	b	.rep
.tout:	adr	x0,h_n
	str	xzr,[x0]
	bl	publier
	b	.rep0
.lues:	bl	h_lues
	bl	publier
.rep0:	mov	x0,0
.rep:	str	x0,[x20]
	mov	x0,x19
	mov	x1,x20
	sys	SYS_SVC_REPLY
	b	.loop
quit:	mov	x0,1
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  L'historique : h_tab[0] la plus ancienne .. h_tab[h_n - 1] la plus recente
; ---------------------------------------------------------------------------

; x0 = rang (0 : la plus recente) -> x0 = entree, ou 0
h_rang:
	adr	x1,h_n
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.non
	sub	x0,x1,x0
	sub	x0,x0,1
	mov	x1,NH_SIZE
	adr	x2,h_tab
	madd	x0,x0,x1,x2
	ret
.non:	mov	x0,0
	ret

; (msg) la notification recue, en tete de l'historique (non lue)
historique:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,h_n				; pleine : la plus ancienne s'en va
	ldr	x1,[x0]
	cmp	x1,NH_MAX
	b.lo	.place
	sub	x1,x1,1
	str	x1,[x0]
	adr	x0,h_tab
	add	x1,x0,NH_SIZE
	mov	x2,(NH_MAX-1)*NH_SIZE
	bl	mem_move
.place:	adr	x0,h_n
	ldr	x1,[x0]
	add	x2,x1,1
	str	x2,[x0]
	mov	x2,NH_SIZE
	adr	x19,h_tab
	madd	x19,x1,x2,x19
	adr	x0,h_id				; id, heure, non lue
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	str	x1,[x19,NH_ID]
	sys	SYS_TIME_MS
	str	x0,[x19,NH_TIME]
	mov	x0,1
	str	x0,[x19,NH_UNREAD]
	adr	x20,msg+WM_DATA
	add	x0,x19,NH_TITRE			; titre
	mov	x1,x20
	mov	x2,NH_TITRE_N
	bl	copie
	mov	x20,x0
	add	x0,x19,NH_TEXTE			; texte
	mov	x1,x20
	mov	x2,NH_TEXTE_N
	bl	copie
	mov	x20,x0
	adr	x1,msg+WS_MSG-1			; l'app, si elle est dite
	cmp	x20,x1
	b.hs	.pid
	ldrb	w1,[x20]
	cbz	w1,.pid
	add	x0,x19,NH_APP
	mov	x1,x20
	mov	x2,NH_APP_N
	bl	copie
	add	x0,x19,NH_APP			; (un vrai paquet : des expediteurs
	bl	app_valide			; laissent des restes apres le texte)
	cbnz	x0,.out
.pid:	strb	wzr,[x19,NH_APP]		; sinon : le nom du process appelant
	adr	x0,msg
	ldr	w0,[x0,WM_PID]
	adr	x1,pinfo
	sys	SYS_PROC_INFO
	tbnz	x0,63,.out
	add	x0,x19,NH_APP
	adr	x1,pinfo+24
	mov	x2,NH_APP_N
	bl	copie
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom -> x0 = 1 si c'est un paquet : [a-z0-9._-], et SD0:/apps/<nom>.app/main
app_valide:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x1,x0
.c:	ldrb	w2,[x1],1
	cbz	w2,.fin
	sub	w3,w2,'a'
	cmp	w3,25
	b.ls	.c
	sub	w3,w2,'0'
	cmp	w3,9
	b.ls	.c
	cmp	w2,'_'
	b.eq	.c
	cmp	w2,'-'
	b.eq	.c
	cmp	w2,'.'
	b.eq	.c
	b	.non
.fin:	sub	x1,x1,x19
	cmp	x1,2				; (vide)
	b.lo	.non
	adr	x0,chemin
	strb	wzr,[x0]
	adr	x1,p_apps
	bl	str_cat
	mov	x1,x19
	bl	str_cat
	adr	x1,p_main
	bl	str_cat
	adr	x0,chemin
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.non
	sys	SYS_CLOSE
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = rang -> x0 = nombre ; l'entree dans les donnees de msg
h_copie:
	prologue
	bl	h_rang
	cbz	x0,.n
	mov	x1,x0
	adr	x0,msg+WM_DATA
	mov	x2,NH_SIZE
	bl	mem_move
.n:	adr	x0,h_n
	ldr	x0,[x0]
	epilogue

; x0 = id -> x0 = 0, E_NOENT
h_effacer:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.c:	adr	x0,h_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.non
	adr	x1,h_tab
	mov	x2,NH_SIZE
	madd	x1,x20,x2,x1
	ldr	x3,[x1,NH_ID]
	cmp	x3,x19
	b.eq	.oui
	add	x20,x20,1
	b	.c
.oui:	sub	x0,x0,1				; les suivantes remontent
	adr	x3,h_n
	str	x0,[x3]
	sub	x2,x0,x20
	mov	x3,NH_SIZE
	mul	x2,x2,x3
	mov	x0,x1
	add	x1,x1,NH_SIZE
	bl	mem_move
	mov	x0,0
	b	.out
.non:	mov	x0,E_NOENT
.out:	ldp	x19,x20,[sp],16
	epilogue

; toutes lues
h_lues:
	adr	x0,h_n
	ldr	x1,[x0]
	adr	x0,h_tab
.l:	cbz	x1,.r
	str	xzr,[x0,NH_UNREAD]
	add	x0,x0,NH_SIZE
	sub	x1,x1,1
	b	.l
.r:	ret

; korin.notify.unread = le nombre de non lues, s'il a change
publier:
	prologue
	adr	x0,h_n
	ldr	x1,[x0]
	adr	x0,h_tab
	mov	x2,0
.l:	cbz	x1,.compte
	ldr	x3,[x0,NH_UNREAD]
	add	x2,x2,x3
	add	x0,x0,NH_SIZE
	sub	x1,x1,1
	b	.l
.compte:
	adr	x0,publie
	ldr	x1,[x0]
	cmp	x1,x2
	b.eq	.out
	str	x2,[x0]
	mov	x0,x2
	adr	x1,valeur
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,s_unread
	adr	x1,valeur
	mov	x2,-1
	mov	x3,BUS_RETAIN
	bl	bus_publish
.out:	epilogue

; -> x0 = 1 si « Ne pas deranger » (korin.notify.dnd = 1)
dnd:
	prologue
	adr	x1,valeur
	str	xzr,[x1]
	adr	x0,s_dnd
	mov	x2,8
	bl	bus_get
	cmp	x0,0
	b.le	.non
	adr	x0,valeur
	ldrb	w0,[x0]
	cmp	w0,'1'
	cset	x0,eq
	epilogue
.non:	mov	x0,0
	epilogue

; -> x0 = 1 en mode poche : korin.shell.mode (Elegant), sinon system.ini
poche:
	prologue
	adr	x1,valeur
	stp	xzr,xzr,[x1]
	adr	x0,s_mode
	mov	x2,15
	bl	bus_get
	cmp	x0,0
	b.le	.ini
	adr	x0,valeur
	adr	x1,s_poche
	bl	str_eq
	epilogue
.ini:	adr	x0,ini_poche
	ldr	x0,[x0]
	epilogue

; SD0:/etc/system.ini : « shell = poche » ? -> ini_poche
lire_systeme:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,p_system
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,fbuf
	mov	x2,1023
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	b.le	.out
	adr	x19,fbuf
	strb	wzr,[x19,x20]
.ligne:	ldrb	w0,[x19]			; debut de ligne : « shell » ?
	cbz	w0,.out
	mov	x0,x19
	adr	x1,s_shell
	bl	prefixe
	cbz	x0,.suite
.esp:	ldrb	w1,[x0]				; espaces et « = »
	cmp	w1,' '
	b.eq	.e1
	cmp	w1,9
	b.eq	.e1
	cmp	w1,'='
	b.ne	.val
.e1:	add	x0,x0,1
	b	.esp
.val:	adr	x1,s_poche
	bl	prefixe
	cbz	x0,.suite
	adr	x0,ini_poche
	mov	x1,1
	str	x1,[x0]
.suite:	ldrb	w0,[x19]			; ligne suivante
	cbz	w0,.out
	add	x19,x19,1
	cmp	w0,10
	b.ne	.suite
	b	.ligne
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = prefixe -> x0 = apres le prefixe, ou 0
prefixe:
.c:	ldrb	w2,[x1],1
	cbz	w2,.oui
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	.c
	mov	x0,0
.oui:	ret

; ---------------------------------------------------------------------------
;  Les bulles
; ---------------------------------------------------------------------------

; (msg) l'entree la plus recente de l'historique : a la fin de la file des
; bulles (perdue si elle est pleine)
ajouter:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,notes_n
	ldr	x1,[x0]
	cmp	x1,NOTE_MAX
	b.hs	.out
	adr	x2,notes_t			; place : (tete + n) mod NOTE_MAX
	ldr	x2,[x2]
	add	x2,x2,x1
	and	x2,x2,NOTE_MAX-1
	add	x1,x1,1
	str	x1,[x0]
	adr	x19,notes
	add	x19,x19,x2,lsl 9
	mov	x0,0
	bl	h_rang
	mov	x20,x0
	add	x0,x19,NOTE_TITRE
	add	x1,x20,NH_TITRE
	mov	x2,64
	bl	copie
	add	x0,x19,NOTE_TEXTE
	add	x1,x20,NH_TEXTE
	mov	x2,192
	bl	copie
	add	x0,x19,NOTE_APP
	add	x1,x20,NH_APP
	mov	x2,32
	bl	copie
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source, x2 = taille (0 compris) -> x0 = apres le 0
; de la source
copie:
	sub	x2,x2,1
.c:	ldrb	w3,[x1],1
	cbz	w3,.f
	cbz	x2,.c				; (trop long : coupe)
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c
.f:	strb	wzr,[x0]
	mov	x0,x1
	ret

; la notification suivante de la file, s'il y en a : fenetre, dessin
suivante:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,notes_n
	ldr	x1,[x0]
	cbz	x1,.out
	sub	x1,x1,1
	str	x1,[x0]
	adr	x2,notes_t
	ldr	x3,[x2]
	add	x4,x3,1
	and	x4,x4,NOTE_MAX-1
	str	x4,[x2]
	adr	x19,notes
	add	x19,x19,x3,lsl 9
	mov	x0,0				; le theme du moment (sans bulle, notifyd
	bl	ui_theme_texte			; ne lit pas ses evenements : korin.theme
	adr	x0,ecran			; est passe sans lui) ; sous la barre
	bl	ui_screen
	bl	poche
	adr	x1,en_poche
	str	x0,[x1]
	cbnz	x0,.poche
	adr	x0,ecran			; bureau : en haut a droite
	ldr	x0,[x0]				; largeur
	sub	x0,x0,NW + MARGE_D
	adr	x1,ecran
	ldr	x1,[x1,24]			; reserve en haut (barre de menus)
	add	x1,x1,6
	mov	x2,NW
	mov	x3,NH
	b	.fen
.poche:	adr	x1,ecran			; poche : toute la largeur
	ldr	x2,[x1]
	sub	x2,x2,12
	ldr	x1,[x1,24]
	add	x1,x1,6
	mov	x0,6
	mov	x3,PH
.fen:	adr	x4,tw
	stp	x2,x3,[x4]
	mov	x4,x19
	mov	x5,WF_FRAMELESS or WF_TOP or WF_NOFOCUS or WF_NOLIST
	bl	ui_window_ex
	tbnz	x0,63,.out
	mov	x0,x19
	bl	dessiner
	sys	SYS_TIME_MS
	add	x0,x0,DUREE
	adr	x1,fin
	str	x0,[x1]
	mov	x0,1
	adr	x1,affichee
	str	x0,[x1]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = notification : la bulle (fond du theme, pastille d'accent - en poche,
; l'icone de l'app -, titre en gras, texte sur deux lignes au plus : coupe au
; « \n » ou a la largeur)
dessiner:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,tw
	ldp	x21,x22,[x0]			; taille
	mov	x0,0
	mov	x1,0
	mov	x2,x21
	mov	x3,x22
	jeton	w4,TJ_LIGNE_FORTE
	bl	ui_fill
	mov	x0,1
	mov	x1,1
	sub	x2,x21,2
	sub	x3,x22,2
	mov	x4,6
	jeton	w5,TJ_SURFACE
	jeton	w6,TJ_SURFACE
	bl	ui_rbox
	mov	x20,20				; x du texte
	adr	x0,en_poche
	ldr	x0,[x0]
	cbz	x0,.pastille
	ldrb	w0,[x19,NOTE_APP]		; poche : l'icone de l'app
	cbz	w0,.pastille
	add	x0,x19,NOTE_APP
	mov	x1,14
	mov	x2,(PH-28)/2
	mov	x3,28
	bl	ui_icone
	mov	x20,54
	b	.titre
.pastille:
	mov	x0,8				; pastille d'accent
	mov	x1,10
	mov	x2,4
	sub	x3,x22,20
	mov	x4,2
	jeton	w5,TJ_ACCENT
	jeton	w6,TJ_ACCENT
	bl	ui_rbox
.titre:	mov	x0,x19				; titre
	mov	x1,x20
	mov	x2,26
	jeton	w3,TJ_TEXTE1
	mov	x4,15
	adr	x5,ui_font_b
	sub	x6,x21,x20
	sub	x6,x6,12
	bl	ui_text
	add	x0,x19,NOTE_TEXTE		; texte : premiere ligne
	str	x0,[sp,-16]!
	bl	ligne
	ldr	x0,[sp],16
	mov	x1,x20
	mov	x2,46
	jeton	w3,TJ_TEXTE2
	mov	x4,13
	adr	x5,ui_font
	sub	x6,x21,x20
	sub	x6,x6,12
	bl	ui_text
	adr	x0,reste			; seconde ligne (s'il y a la place)
	ldr	x0,[x0]
	cbz	x0,.fin
	cmp	x22,NH
	b.lo	.fin
	mov	x1,x20
	mov	x2,64
	jeton	w3,TJ_TEXTE2
	mov	x4,13
	adr	x5,ui_font
	sub	x6,x21,x20
	sub	x6,x6,12
	bl	ui_text
.fin:	bl	ui_update_all
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte : coupe la premiere ligne au « \n » (reste = la suite, 0 si
; aucune)
ligne:
	adr	x2,reste
	str	xzr,[x2]
.c:	ldrb	w1,[x0]
	cbz	w1,.f
	cmp	w1,10
	b.eq	.nl
	add	x0,x0,1
	b	.c
.nl:	strb	wzr,[x0],1
	str	x0,[x2]
.f:	ret

s_nom		db 'notify',0
s_unread	db 'korin.notify.unread',0
s_dnd		db 'korin.notify.dnd',0
s_mode		db 'korin.shell.mode',0
s_poche		db 'poche',0
s_shell		db 'shell',0
p_system	db 'SD0:/etc/system.ini',0
p_apps		db 'SD0:/apps/',0
p_main		db '.app/main',0
		align 8

segment readable writeable
kit_data
		align 8
canal		dd -1
affichee	dd 0
fin		dd 0
reste		dd 0
notes_t		dd 0
notes_n		dd 0
en_poche	dd 0
ini_poche	dd 0
publie		dd -1				; dernier nombre de non lues publie
h_n		dd 0
h_id		dd 0
tw		rd 2				; taille de la bulle
ecran		rb 32
pinfo		rb 64
valeur		rb 32
chemin		rb 96
msg		rb 512
fbuf		rb 1024
notes		rb NOTE_MAX * NOTE_TAILLE
h_tab		rb NH_MAX * NH_SIZE
ui_data
