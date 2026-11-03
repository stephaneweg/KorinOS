; soundmenu - le volet du son de la barre de menus (docs/SON.md 4.4) : lance
; par menubar (un clic sur le haut-parleur) avec l'abscisse de l'icone. Une
; petite fenetre sans cadre sous l'icone :
;  * le volume de la sortie principale (curseur, on peut le faire glisser),
;    la case « Muet » ;
;  * la liste des sorties : un clic en fait la principale (preferee, retenue
;    dans SD0:/etc/sound.txt par sound) ;
;  * en bas, « Mixer... » : l'application Mixer.
; Tout passe par AudioKit (service sound) ; l'etat est relu toutes les 200 ms
; (redessin si sa generation change). Se ferme sur Echap, quand une autre
; fenetre prend le clavier, ou par un second clic sur l'icone (service
; « soundmenu » : un second soundmenu le ferme et s'arrete).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../inc/sound.inc'

MW		= 280			; largeur
HEAD		= 34
SL_Y		= 44			; curseur du volume
SL_X		= 40
SL_W		= MW - SL_X - 56
MUTE_Y		= 74
LIST_Y		= 126			; premiere sortie
ROWH		= 26
FOOT		= 46
BTN_W		= 120

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so
include '../../inc/sounddraw.inc'

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
	bl	ak_state
	cbz	x0,quit				; pas de service sound
	adr	x1,stptr
	str	x0,[x1]
	mov	x20,400
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
	ldr	x22,[x0,24]
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
	bl	collect
	bl	open_win
	tbnz	x0,63,quit
	bl	draw
.loop:	adr	x0,canal			; un second soundmenu : fermer
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
	b.eq	.key
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cbnz	x0,.loop
	bl	refresh				; (rien : l'etat a-t-il change ?)
	b	.loop
.focus:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cbz	w0,quit
	b	.loop
.key:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cbz	w0,.loop
	ldr	w0,[x9,EV_CODE]
	cmp	w0,KEY_ESC
	b.eq	quit
	b	.loop
.mouse:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	ldrsw	x1,[x9,EV_X]
	ldrsw	x2,[x9,EV_Y]
	cmp	w0,1
	b.eq	.press
	cmp	w0,2				; relache : fin du glisser
	b.eq	.rel
	adr	x3,dragging			; deplacement : le curseur suit
	ldr	x3,[x3]
	cbz	x3,.loop
	mov	x0,x1
	bl	set_volume
	b	.loop
.rel:	adr	x3,dragging
	str	xzr,[x3]
	b	.loop
.press:	mov	x0,x1
	mov	x1,x2
	bl	click
	cmp	x0,3
	b.eq	quit
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; l'etat a change (generation) : relu, fenetre refaite si le nombre de
; sorties change, redessin
refresh:
	prologue
	adr	x0,stptr
	ldr	x0,[x0]
	ldr	x0,[x0,SS_GEN]
	adr	x1,gen
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.out
	adr	x0,nshow
	ldr	x19,[x0]
	bl	collect
	adr	x0,nshow
	ldr	x0,[x0]
	cmp	x0,x19
	b.eq	.d
	bl	ui_close
	bl	open_win
.d:	bl	draw
.out:	epilogue

; Les sorties a montrer (shown : leurs entrees dans l'etat) ; les
; virtuelles seulement s'il n'y a qu'elles ; la principale (mainp)
collect:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,stptr
	ldr	x19,[x0]
	ldr	x0,[x19,SS_GEN]
	adr	x1,gen
	str	x0,[x1]
	adr	x0,mainp
	str	xzr,[x0]
	mov	x21,0				; une sortie reelle ?
	add	x20,x19,SS_OUTS
	mov	x22,SS_OUT_MAX
.r:	ldr	w0,[x20,SO_ID]
	cbz	w0,.rn
	ldr	w0,[x20,SO_TYPE]
	cmp	w0,SND_OUT_VIRTUAL
	cinc	x21,x21,ne
.rn:	add	x20,x20,64
	subs	x22,x22,1
	b.ne	.r
	mov	x9,0				; montrees
	add	x20,x19,SS_OUTS
	mov	x22,SS_OUT_MAX
.o:	ldr	w0,[x20,SO_ID]
	cbz	w0,.on
	ldr	x1,[x19,SS_MAIN]
	cmp	x0,x1
	b.ne	.v
	adr	x1,mainp
	str	x20,[x1]
.v:	ldr	w0,[x20,SO_TYPE]
	cmp	w0,SND_OUT_VIRTUAL
	b.ne	.add
	cbnz	x21,.on				; virtuelle cachee
.add:	adr	x1,shown
	str	x20,[x1,x9,lsl 3]
	add	x9,x9,1
.on:	add	x20,x20,64
	subs	x22,x22,1
	b.ne	.o
	adr	x0,nshow
	str	x9,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = poignee de la fenetre (a la hauteur du contenu)
open_win:
	prologue
	adr	x0,nshow
	ldr	x0,[x0]
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lo
	mov	x1,ROWH
	mul	x0,x0,x1
	add	x3,x0,LIST_Y+FOOT
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

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
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
	adr	x0,s_son			; titre, la principale a droite
	bl	tr_x0
	mov	x1,16
	mov	x2,26
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	adr	x0,mainp
	ldr	x19,[x0]			; entree de la principale (0 : aucune)
	adr	x0,s_aucune
	bl	tr_x0
	cbz	x19,.nm
	add	x0,x19,SO_NAME
.nm:	mov	x1,60
	mov	x2,26
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,MW-76
	bl	ui_text
	mov	x0,12
	mov	x1,HEAD
	mov	x2,MW-24
	mov	x3,1
	ldr	w4,[x23,TJ_LIGNE]
	bl	ui_fill
	cbz	x19,.list
	mov	x0,14				; haut-parleur, curseur, valeur
	mov	x1,SL_Y+1
	ldr	w2,[x19,SO_MUTE]
	ldr	w3,[x23,TJ_TEXTE1]
	ldr	w4,[x23,TJ_TEXTE3]
	bl	sd_speaker
	mov	x0,SL_X
	mov	x1,SL_Y
	mov	x2,SL_W
	ldr	w3,[x19,SO_VOLUME]
	ldr	w4,[x19,SO_MUTE]
	bl	sd_slider
	ldr	w0,[x19,SO_VOLUME]
	adr	x1,numbuf
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	w1,'%'
	strb	w1,[x0],1
	strb	wzr,[x0]
	adr	x0,numbuf
	mov	x1,SL_X+SL_W+10
	mov	x2,SL_Y+13
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x0,16				; Muet
	mov	x1,MUTE_Y+2
	ldr	w2,[x19,SO_MUTE]
	bl	sd_check
	adr	x0,s_muet
	bl	tr_x0
	mov	x1,38
	mov	x2,MUTE_Y+14
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.list:	mov	x0,12
	mov	x1,LIST_Y-26
	mov	x2,MW-24
	mov	x3,1
	ldr	w4,[x23,TJ_LIGNE]
	bl	ui_fill
	adr	x0,s_sortie
	bl	tr_x0
	mov	x1,16
	mov	x2,LIST_Y-8
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x20,0
	mov	x21,LIST_Y			; y de la ligne
.r:	adr	x0,nshow
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.foot
	adr	x0,shown
	ldr	x22,[x0,x20,lsl 3]
	ldr	w5,[x23,TJ_LIGNE_FORTE]		; rond : plein pour la principale
	cmp	x22,x19
	b.ne	.c
	ldr	w5,[x23,TJ_ACCENT]
.c:	mov	w6,w5
	mov	x0,16
	add	x1,x21,6
	mov	x2,14
	mov	x3,14
	mov	x4,7
	bl	ui_rbox
	cmp	x22,x19
	b.eq	.t
	ldr	w5,[x23,TJ_FACE]
	mov	w6,w5
	mov	x0,18
	add	x1,x21,8
	mov	x2,10
	mov	x3,10
	mov	x4,5
	bl	ui_rbox
.t:	add	x0,x22,SO_NAME
	mov	x1,40
	add	x2,x21,18
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	cmp	x22,x19
	b.ne	.tf
	adr	x5,ui_font_b
.tf:	mov	x6,MW-56
	bl	ui_text
	add	x20,x20,1
	add	x21,x21,ROWH
	b	.r
.foot:	mov	x0,12
	add	x1,x21,6
	mov	x2,MW-24
	mov	x3,1
	ldr	w4,[x23,TJ_LIGNE]
	bl	ui_fill
	mov	x0,MW-BTN_W-14
	add	x1,x21,12
	mov	x2,BTN_W
	mov	x3,26
	adr	x4,s_mixer
	bl	tr_x4
	mov	x5,0
	bl	ui_button
	bl	ui_update_all
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = abscisse : volume de la principale d'apres le curseur
set_volume:
	prologue
	str	x19,[sp,-16]!
	adr	x1,mainp
	ldr	x19,[x1]
	cbz	x19,.out
	mov	x1,SL_X
	mov	x2,SL_W
	bl	sd_slider_value
	mov	x1,x0
	ldr	w0,[x19,SO_ID]
	ldr	w2,[x19,SO_MUTE]
	bl	ak_out_volume
	bl	refresh
.out:	ldr	x19,[sp],16
	epilogue

; x0, x1 = clic -> x0 = 3 : fermer
click:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,mainp
	ldr	x21,[x0]
	cbz	x21,.list
	cmp	x20,SL_Y-4			; curseur : volume, et on glisse
	b.lt	.list
	cmp	x20,SL_Y+SD_SLIDER_H+4
	b.ge	.mute
	cmp	x19,SL_X-6
	b.lt	.none
	adr	x0,dragging
	mov	x1,1
	str	x1,[x0]
	mov	x0,x19
	bl	set_volume
	b	.none
.mute:	cmp	x20,MUTE_Y+24			; Muet
	b.ge	.list
	ldr	w0,[x21,SO_ID]
	ldr	w1,[x21,SO_VOLUME]
	ldr	w2,[x21,SO_MUTE]
	eor	x2,x2,1
	bl	ak_out_volume
	bl	refresh
	b	.none
.list:	subs	x0,x20,LIST_Y			; une sortie : la principale
	b.lt	.none
	mov	x1,ROWH
	udiv	x0,x0,x1
	adr	x1,nshow
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.foot
	adr	x1,shown
	ldr	x0,[x1,x0,lsl 3]
	ldr	w0,[x0,SO_ID]
	bl	ak_main
	bl	refresh
	b	.none
.foot:	adr	x0,ui_w				; « Mixer... » : l'application
	ldr	x2,[x0,8]
	sub	x2,x2,FOOT
	cmp	x20,x2
	b.lt	.none
	adr	x0,p_mixer
	mov	x1,0
	sys	SYS_SPAWN
	mov	x0,3
	b	.out
.none:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_nom		db 'soundmenu',0
title		db 'Son',0
s_son		db 'Son',0
s_aucune	db 'aucune sortie',0
s_muet		db 'Muet',0
s_sortie	db 'Sortie principale',0
s_mixer		db 'Mixer',0xE2,0x80,0xA6,0
p_mixer		db 'SD0:/apps/mixer.app/main',0
		align 4

include '../../inc/tr.inc'

segment readable writeable
kit_data
ui_data
		align 8
canal		dd 0
stptr		dd 0				; tampon d'etat de sound
gen		dd -1
mainp		dd 0				; entree de la principale (0 : aucune)
nshow		dd 0
shown		rd SS_OUT_MAX
dragging	dd 0
win_x		dd 0
win_y		dd 0
scr		rb 32
numbuf		rb 24
msg		rb 512
