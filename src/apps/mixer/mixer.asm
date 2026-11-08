; mixer - l'application Mixer (docs/SON.md 4.4) : le service sound par
; AudioKit.
;  * en haut, une ligne par sortie : etoile (la principale ; un clic en
;    fait la preferee), nom, curseur du volume, case Muet, crete ;
;  * en bas, une ligne par process qui joue : nom, liste deroulante de sa
;    sortie (Principale, puis les sorties de sound : ak_route), curseur de
;    son volume, case Muet, crete.
; Les listes deroulantes sont des widgets d'UIKit (ui_dropdown), une par
; ligne montree (DROP_MAX), deplacees ou cachees a chaque dessin et refaites
; quand la liste des sorties change.
; Volumes, routage et preferee sont retenus par sound (SD0:/etc/sound.txt).
; L'etat (tampon partage de sound) : son adresse redemandee a chaque
; korin.audio.state (docs/BUS.md ; sans busd, toutes les 200 ms) ; les
; vumetres sont redessines toutes les 200 ms.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../inc/sound.inc'
include '../../kits/buskit/buskit.inc'

WIN_W		= 690
WIN_H		= 420
ROW		= 38
OUT_Y		= 36			; premiere sortie
NAME_X		= 40
DROP_X		= 156			; liste deroulante de la sortie d'un client
DROP_W		= 180
DROP_H		= 26
DROP_DY		= 3
DROP_MAX	= 10
SL_X		= 348
SL_W		= 160
VAL_X		= 516
MUTE_X		= 558
METER_X		= 618
METER_W		= 54

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so
include '../../inc/sounddraw.inc'

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	mov	x0,WIN_W
	mov	x1,WIN_H
	adr	x2,title
	bl	ui_window
	tbnz	x0,63,quit
	adr	x0,s_audio_topic		; sound a change : le bus
	mov	x1,0
	bl	bus_subscribe
	cmp	x0,0
	csel	x0,x0,xzr,gt
	adr	x1,audio_sub
	str	x0,[x1]
	bl	ak_state
	adr	x1,stptr
	str	x0,[x1]
	bl	draw
.loop:	mov	x0,200
	bl	ui_event
	cbz	x0,.tick
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_BUS
	b.eq	.etat
	mov	x19,x0				; (type de l'evenement)
	mov	x0,0
	bl	ui_w_event			; les listes deroulantes d'abord
	cbz	x2,.nw
	cmp	x1,UN_CHANGE
	b.ne	.taken
	bl	drop_chosen
	bl	draw
	b	.loop
.taken:	adr	x3,drag				; (un curseur tenu continue)
	ldr	x3,[x3]
	cbz	x3,.loop
.nw:	cmp	x19,WEV_MOUSE
	b.eq	.mouse
	b	.loop
.tick:	adr	x0,audio_sub			; sans bus : l'etat redemande
	ldr	x0,[x0]
	cbnz	x0,.vu
.etat:	bl	ak_state			; (sound relance : nouveau tampon)
	adr	x1,stptr
	str	x0,[x1]
.vu:	bl	draw				; vumetres et changements
	b	.loop
.mouse:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	ldrsw	x1,[x9,EV_X]
	ldrsw	x2,[x9,EV_Y]
	cmp	w0,1
	b.eq	.press
	cmp	w0,2
	b.eq	.rel
	adr	x3,drag				; glisser un curseur
	ldr	x3,[x3]
	cbz	x3,.loop
	mov	x0,x1
	bl	slide
	b	.loop
.rel:	adr	x3,drag
	str	xzr,[x3]
	b	.loop
.press:	mov	x0,x1
	mov	x1,x2
	bl	click
	bl	draw
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; Tout : sorties, puis clients ; lignes notees dans rows (entree de l'etat,
; genre : 1 sortie, 2 client) pour les clics
draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	bl	ui_jetons
	mov	x23,x0
	ldr	w4,[x23,TJ_FACE]
	mov	x0,0
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,WIN_H
	bl	ui_fill
	adr	x0,nrows
	str	xzr,[x0]
	mov	x25,0				; clients montres
	adr	x0,stptr
	ldr	x19,[x0]
	cbnz	x19,.ok
	adr	x0,s_nosvc
	bl	tr0
	mov	x1,16
	mov	x2,40
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	b	.done
.ok:	adr	x0,s_outs
	bl	tr0
	mov	x1,14
	mov	x2,24
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x24,OUT_Y			; y de la ligne
	add	x20,x19,SS_OUTS
	mov	x21,SS_OUT_MAX
.o:	ldr	w0,[x20,SO_ID]
	cbz	w0,.on
	mov	x0,x20
	mov	x1,1
	mov	x2,x24
	bl	row_note
	ldr	w0,[x20,SO_FLAGS]		; etoile : la principale
	adr	x1,s_star0
	adr	x2,s_star1
	tst	w0,1
	csel	x0,x2,x1,ne
	mov	x1,14
	add	x2,x24,20
	ldr	w3,[x23,TJ_ACCENT]
	mov	x4,16
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	add	x0,x20,SO_NAME
	ldr	w4,[x20,SO_VOLUME]
	ldr	w5,[x20,SO_MUTE]
	ldr	w6,[x20,SO_PEAK]
	mov	x1,x24
	mov	x2,x23
	mov	x7,SL_X-NAME_X-12
	bl	line_common
	add	x24,x24,ROW
.on:	add	x20,x20,64
	subs	x21,x21,1
	b.ne	.o
	add	x24,x24,8			; les applications
	mov	x0,14
	mov	x1,x24
	mov	x2,WIN_W-28
	mov	x3,1
	ldr	w4,[x23,TJ_LIGNE]
	bl	ui_fill
	adr	x0,s_apps
	bl	tr0
	mov	x1,14
	add	x2,x24,22
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	add	x24,x24,32
	bl	drop_items
	add	x20,x19,SS_CLIS
	mov	x21,SS_CLI_MAX
.c:	ldr	w0,[x20,SC_PID]
	cbz	w0,.cn
	add	x0,x24,ROW
	cmp	x0,WIN_H
	b.hi	.cn
	mov	x0,x20
	mov	x1,2
	mov	x2,x24
	bl	row_note
	add	x0,x20,SC_NAME
	ldr	w4,[x20,SC_VOLUME]
	ldr	w5,[x20,SC_MUTE]
	ldr	w6,[x20,SC_PEAK]
	mov	x1,x24
	mov	x2,x23
	mov	x7,DROP_X-NAME_X-6
	bl	line_common
	mov	x0,x25				; sa sortie
	mov	x1,x20
	mov	x2,x24
	bl	drop_row
	add	x25,x25,1
	add	x24,x24,ROW
.cn:	add	x20,x20,64
	subs	x21,x21,1
	b.ne	.c
	cbnz	x25,.done
	adr	x0,s_noapp
	bl	tr0
	mov	x1,40
	add	x2,x24,18
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.done:	mov	x0,x25				; listes des lignes absentes
	bl	drop_hide
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom, x1 = y, x2 = jetons, x4 = volume, x5 = muet, x6 = crete,
; x7 = largeur du nom : nom, curseur, valeur, case Muet, vumetre (communs
; aux sorties et aux clients)
line_common:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x1
	mov	x23,x2
	mov	x20,x4
	mov	x21,x5
	mov	x22,x6
	mov	x6,x7
	mov	x1,NAME_X
	add	x2,x19,20
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	bl	ui_text
	mov	x0,METER_X			; vumetre
	add	x1,x19,15
	mov	x2,METER_W
	mov	x3,8
	mov	x4,x22
	bl	sd_meter
	mov	x0,SL_X
	add	x1,x19,4
	mov	x2,SL_W
	mov	x3,x20
	mov	x4,x21
	bl	sd_slider
	mov	x0,x20
	adr	x1,numbuf
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	w1,'%'
	strb	w1,[x0],1
	strb	wzr,[x0]
	adr	x0,numbuf
	mov	x1,VAL_X
	add	x2,x19,18
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x0,MUTE_X
	add	x1,x19,6
	mov	x2,x21
	bl	sd_check
	adr	x0,s_muet
	bl	tr0
	mov	x1,MUTE_X+18
	add	x2,x19,18
	ldr	w3,[x23,TJ_TEXTE2]
	mov	x4,11
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree de l'etat, x1 = genre, x2 = y : une ligne cliquable
row_note:
	adr	x3,nrows
	ldr	x4,[x3]
	cmp	x4,48
	b.hs	.ret
	adr	x5,rows
	add	x5,x5,x4,lsl 5
	stp	x0,x1,[x5]
	str	x2,[x5,16]
	add	x4,x4,1
	str	x4,[x3]
.ret:	ret

; x0 = y -> x0 = ligne (rows), 0 si aucune
row_at:
	adr	x1,nrows
	ldr	x1,[x1]
	adr	x2,rows
.t:	cbz	x1,.non
	ldr	x3,[x2,16]
	cmp	x0,x3
	b.lt	.n
	add	x3,x3,ROW
	cmp	x0,x3
	b.ge	.n
	mov	x0,x2
	ret
.n:	add	x2,x2,32
	sub	x1,x1,1
	b	.t
.non:	mov	x0,0
	ret

; x0 = ligne (rows), x1 = volume, x2 = muet : au service
set_vol:
	prologue
	ldp	x3,x4,[x0]			; entree, genre
	cmp	x4,1
	b.ne	.cli
	ldr	w0,[x3,SO_ID]
	bl	ak_out_volume
	b	.out
.cli:	ldr	w0,[x3,SC_PID]
	bl	ak_client_volume
.out:	epilogue

; x0 = abscisse : le curseur qu'on fait glisser
slide:
	prologue
	str	x19,[sp,-16]!
	adr	x1,drag
	ldr	x19,[x1]
	mov	x1,SL_X
	mov	x2,SL_W
	bl	sd_slider_value
	mov	x1,x0
	ldp	x3,x4,[x19]
	ldr	w2,[x3,SO_MUTE]			; (SO_MUTE = 16, SC_MUTE = 8)
	cmp	x4,1
	b.eq	.m
	ldr	w2,[x3,SC_MUTE]
.m:	mov	x0,x19
	bl	set_vol
	bl	draw
	ldr	x19,[sp],16
	epilogue

; x0, x1 = clic
click:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x0,x1
	bl	row_at
	cbz	x0,.out
	mov	x20,x0
	ldp	x21,x22,[x20]			; entree, genre
	cmp	x19,SL_X-8			; curseur
	b.lt	.left
	cmp	x19,SL_X+SL_W+4
	b.ge	.right
	adr	x0,drag
	str	x20,[x0]
	mov	x0,x19
	bl	slide
	b	.out
.right:	cmp	x19,MUTE_X-4			; Muet
	b.lt	.out
	cmp	x22,1
	b.ne	.mc
	ldr	w1,[x21,SO_VOLUME]
	ldr	w2,[x21,SO_MUTE]
	b	.mt
.mc:	ldr	w1,[x21,SC_VOLUME]
	ldr	w2,[x21,SC_MUTE]
.mt:	eor	x2,x2,1
	mov	x0,x20
	bl	set_vol
	b	.out
.left:	cmp	x22,2				; (client : sa liste deroulante)
	b.eq	.out
	cmp	x19,NAME_X			; etoile d'une sortie : preferee
	b.ge	.out
	ldr	w0,[x21,SO_ID]
	bl	ak_main
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> sa traduction ; x1 a x7 gardes

tr0:

	prologue

	stp	x1,x2,[sp,-16]!

	stp	x3,x4,[sp,-16]!

	stp	x5,x6,[sp,-16]!

	str	x7,[sp,-16]!

	bl	ui_tr

	ldr	x7,[sp],16

	ldp	x5,x6,[sp],16

	ldp	x3,x4,[sp],16

	ldp	x1,x2,[sp],16

	epilogue

; Les elements des listes deroulantes : « Principale|sortie|... » (drop_text)
; et le numero de sortie de chacun (drop_ids, 0 : la principale) ; si le
; texte change (sortie branchee, retiree), les listes faites sont detruites
drop_items:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,s_mainout
	bl	tr0
	adr	x1,drop_text
	bl	str_copy_z
	mov	x19,x0				; fin du texte
	adr	x20,drop_ids
	str	xzr,[x20],8
	adr	x0,stptr
	ldr	x21,[x0]
	add	x21,x21,SS_OUTS
	mov	x22,SS_OUT_MAX
.o:	ldr	w0,[x21,SO_ID]
	cbz	w0,.n
	str	x0,[x20],8
	mov	w1,'|'
	strb	w1,[x19],1
	add	x2,x21,SO_NAME
	mov	x3,31
.c:	ldrb	w4,[x2],1
	cbz	w4,.e
	cmp	w4,'|'				; (le separateur des elements)
	b.ne	.s
	mov	w4,'/'
.s:	strb	w4,[x19],1
	subs	x3,x3,1
	b.ne	.c
.e:	strb	wzr,[x19]
.n:	add	x21,x21,64
	subs	x22,x22,1
	b.ne	.o
	adr	x0,drop_ids
	sub	x1,x20,x0
	lsr	x1,x1,3
	adr	x0,drop_n
	str	x1,[x0]
	adr	x0,drop_text			; change ?
	adr	x1,drop_prev
.k:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.ne	.chg
	cbnz	w2,.k
	b	.out
.chg:	adr	x0,drop_text
	adr	x1,drop_prev
	bl	str_copy_z
	mov	x19,0
.d:	adr	x20,drop_w
	ldr	x0,[x20,x19,lsl 3]
	str	xzr,[x20,x19,lsl 3]
	cbz	x0,.dn
	bl	ui_w_destroy
.dn:	add	x19,x19,1
	cmp	x19,DROP_MAX
	b.lo	.d
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ligne de client montree (0..), x1 = son entree dans l'etat, x2 = y :
; sa liste deroulante (faite, ou deplacee), sur sa sortie
drop_row:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x19,DROP_MAX
	b.hs	.out
	adr	x0,drop_pid
	ldr	w1,[x20,SC_PID]
	str	x1,[x0,x19,lsl 3]
	adr	x22,drop_w
	ldr	x0,[x22,x19,lsl 3]
	cbnz	x0,.mv
	mov	x0,DROP_X
	add	x1,x21,DROP_DY
	mov	x2,DROP_W
	mov	x3,DROP_H
	adr	x4,drop_text
	bl	ui_dropdown
	str	x0,[x22,x19,lsl 3]
	cbz	x0,.out
	b	.on
.mv:	mov	x1,DROP_X
	add	x2,x21,DROP_DY
	mov	x3,DROP_W
	mov	x4,DROP_H
	bl	ui_w_move
	adr	x0,drop_on
	ldr	x0,[x0,x19,lsl 3]
	cbnz	x0,.val
	ldr	x0,[x22,x19,lsl 3]
	mov	x1,1
	bl	ui_w_show
.on:	adr	x0,drop_on
	mov	x1,1
	str	x1,[x0,x19,lsl 3]
.val:	ldr	w0,[x20,SC_ROUTE]		; l'element de sa sortie (absente :
	adr	x1,drop_ids			; la principale)
	adr	x2,drop_n
	ldr	x2,[x2]
	mov	x3,0
.t:	cmp	x3,x2
	b.hs	.z
	ldr	x4,[x1,x3,lsl 3]
	cmp	x4,x0
	b.eq	.f
	add	x3,x3,1
	b	.t
.z:	mov	x3,0
.f:	mov	x20,x3
	ldr	x0,[x22,x19,lsl 3]
	bl	ui_w_get_value
	cmp	x0,x20
	b.eq	.out
	ldr	x0,[x22,x19,lsl 3]
	mov	x1,x20
	bl	ui_w_set_value
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = premiere ligne sans client : cache les listes deroulantes suivantes
drop_hide:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
.t:	cmp	x19,DROP_MAX
	b.hs	.out
	adr	x20,drop_on
	ldr	x0,[x20,x19,lsl 3]
	cbz	x0,.n
	str	xzr,[x20,x19,lsl 3]
	adr	x0,drop_w
	ldr	x0,[x0,x19,lsl 3]
	mov	x1,0
	bl	ui_w_show
.n:	add	x19,x19,1
	b	.t
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget dont l'element a change : si c'est une liste deroulante, son
; client va sur la sortie choisie
drop_chosen:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.t:	adr	x1,drop_w
	ldr	x1,[x1,x20,lsl 3]
	cmp	x1,x19
	b.eq	.ok
	add	x20,x20,1
	cmp	x20,DROP_MAX
	b.lo	.t
	b	.out
.ok:	mov	x0,x19
	bl	ui_w_get_value
	tbnz	x0,63,.out
	adr	x1,drop_n
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.out
	adr	x1,drop_ids
	ldr	x1,[x1,x0,lsl 3]
	adr	x0,drop_pid
	ldr	x0,[x0,x20,lsl 3]
	bl	ak_route
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = source, x1 = destination (copie avec le 0) -> x0 = fin (sur le 0)
str_copy_z:
	ldrb	w2,[x0],1
	strb	w2,[x1],1
	cbnz	w2,str_copy_z
	sub	x0,x1,1
	ret

title		db 'Mixer',0
s_outs		db 'Sorties',0
s_apps		db 'Applications',0
s_noapp		db 'Aucune application ne joue.',0
s_nosvc		db 'Le service sound ne r',0xC3,0xA9,'pond pas.',0
s_muet		db 'Muet',0
s_star0		db 0xE2,0x98,0x86,0		; U+2606
s_star1		db 0xE2,0x98,0x85,0		; U+2605
s_mainout	db 'Principale',0
		align 4

s_audio_topic	db 'korin.audio.state',0
		align 8

segment readable writeable
kit_data
ui_data
		align 8
stptr		dd 0				; tampon d'etat de sound
audio_sub	dd 0				; abonnement a korin.audio.state (0 : pas de bus)
drag		dd 0				; ligne du curseur tenu (0 : aucun)
nrows		dd 0
rows		rb 48 * 32			; entree, genre, y
numbuf		rb 24
drop_w		rb DROP_MAX * 8			; listes deroulantes (0 : pas faite)
drop_on		rb DROP_MAX * 8			; 1 : montree
drop_pid	rb DROP_MAX * 8			; client de chacune
drop_ids	rb (SS_OUT_MAX + 1) * 8		; numero de sortie de chaque element
drop_n		dd 0				; elements
drop_text	rb 288				; « Principale|sortie|... »
drop_prev	rb 288				; celle des listes faites
