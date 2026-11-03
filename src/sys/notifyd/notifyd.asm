; notifyd - les notifications (comme celui d'Onyx), lance par la session du
; bureau (SD0:/etc/session). Service « notify », op 1 : donnees = titre, 0,
; texte, 0 (inc/notify.inc ; commande SD0:/bin/notify). Chaque notification
; s'affiche en haut a droite, sous la barre de menus, pendant 4 s, dans une
; fenetre sans cadre, au-dessus des autres, qui ne prend pas le clavier ; un
; clic la ferme. Elles attendent leur tour (file de NOTE_MAX).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

NOTIFY_OP	= 1
NOTE_MAX	= 8
NOTE_TITRE	= 0			; 64 octets
NOTE_TEXTE	= 64			; 192 octets
NOTE_TAILLE	= 256
NW		= 330
NH		= 74
MARGE_D		= 8			; a droite
DUREE		= 4000			; ms
E_INVAL		= -22

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so

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
	mov	x2,60000			; rien a l'ecran : attendre une demande
.req:	adr	x0,canal
	ldr	x0,[x0]
	adr	x1,msg
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbz	x0,.loop
	mov	x19,x0
	adr	x20,msg
	ldr	x0,[x20]
	cmp	x0,NOTIFY_OP
	mov	x0,E_INVAL
	b.ne	.rep
	bl	ajouter
	adr	x0,affichee
	ldr	x0,[x0]
	cbnz	x0,.rep0
	bl	suivante
.rep0:	mov	x0,0
.rep:	str	x0,[x20]
	mov	x0,x19
	mov	x1,x20
	sys	SYS_SVC_REPLY
	b	.loop
quit:	mov	x0,1
	sys	SYS_EXIT

; (msg) titre, 0, texte : a la fin de la file (perdue si elle est pleine)
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
	add	x19,x19,x2,lsl 8
	adr	x20,msg+48
	mov	x0,x19				; titre
	mov	x1,x20
	mov	x2,64
	bl	copie
	mov	x20,x0				; (apres le 0 du titre)
	add	x0,x19,NOTE_TEXTE		; texte
	mov	x1,x20
	mov	x2,192
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
	add	x19,x19,x3,lsl 8
	adr	x0,ecran			; en haut a droite, sous la barre
	bl	ui_screen
	adr	x0,ecran
	ldr	x0,[x0]				; largeur
	sub	x0,x0,NW + MARGE_D
	adr	x1,ecran
	ldr	x1,[x1,24]			; reserve en haut (barre de menus)
	add	x1,x1,6
	mov	x2,NW
	mov	x3,NH
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

; x0 = notification : la bulle (fond du theme, pastille d'accent, titre en
; gras, texte sur deux lignes au plus : coupe au « \n » ou a la largeur)
dessiner:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,0
	mov	x1,0
	mov	x2,NW
	mov	x3,NH
	jeton	w4,TJ_LIGNE_FORTE
	bl	ui_fill
	mov	x0,1
	mov	x1,1
	mov	x2,NW-2
	mov	x3,NH-2
	mov	x4,6
	jeton	w5,TJ_SURFACE
	jeton	w6,TJ_SURFACE
	bl	ui_rbox
	mov	x0,8				; pastille d'accent
	mov	x1,10
	mov	x2,4
	mov	x3,NH-20
	mov	x4,2
	jeton	w5,TJ_ACCENT
	jeton	w6,TJ_ACCENT
	bl	ui_rbox
	mov	x0,x19				; titre
	mov	x1,20
	mov	x2,26
	jeton	w3,TJ_TEXTE1
	mov	x4,15
	adr	x5,ui_font_b
	mov	x6,NW-32
	bl	ui_text
	add	x20,x19,NOTE_TEXTE		; texte : premiere ligne
	mov	x0,x20
	bl	ligne
	mov	x0,x20
	mov	x1,20
	mov	x2,48
	jeton	w3,TJ_TEXTE2
	mov	x4,13
	adr	x5,ui_font
	mov	x6,NW-32
	bl	ui_text
	adr	x0,reste			; seconde ligne
	ldr	x0,[x0]
	cbz	x0,.fin
	mov	x1,20
	mov	x2,66
	jeton	w3,TJ_TEXTE2
	mov	x4,13
	adr	x5,ui_font
	mov	x6,NW-32
	bl	ui_text
.fin:	bl	ui_update_all
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

s_nom	db 'notify',0
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
ecran		rb 32
msg		rb 512
notes		rb NOTE_MAX * NOTE_TAILLE
ui_data
