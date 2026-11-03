; ============================================================================
;  Solitaire - le Klondike d'Onyx (user/Apps/solitaire), en assembleur. On
;  glisse les cartes a la souris ou au doigt : sept colonnes descendantes en
;  couleurs alternees (un roi sur une colonne vide), quatre fondations
;  montantes par enseigne depuis l'as. Un toucher du talon retourne une carte
;  (ou trois : Partie > Tirer trois cartes) ; vide, il reprend le rebut. Un
;  toucher sans glisser envoie la carte a la destination evidente ; Auto ou le
;  bouton droit monte tout ce qui peut monter. Les cartes cachees se retournent
;  seules. Score a la Windows et chronometre ; les cartes rebondissent a la fin.
;  `solitaire <n>` : n seme la donne.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W		= 640
WIN_H		= 540
MIN_W		= 500
MIN_H		= 400
STOCK		= 0
WASTE		= 1
FOUND		= 2
TAB		= 6
NPILE		= 13
PILE_SIZE	= 56				; +0 nombre, +1 cartes
UP		= 0x40				; bit d'une carte : visible
S_SCORE		= NPILE*PILE_SIZE		; 32 bits
SS_SIZE		= S_SCORE+8
UNDO_MAX	= 64
ID_NEW		= 1
ID_UNDO		= 2
ID_AUTO		= 3
ID_DRAW1	= 4
ID_DRAW3	= 5
ID_QUIT		= 6

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'
include '../../inc/cards.inc'

start:
	mov	x19,x0				; ligne d'arguments
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	mov	x0,WIN_W
	mov	x1,WIN_H
	bl	cards_fit
	mov	x20,x0
	mov	x21,x1
	mov	x0,-1
	mov	x1,-1
	mov	x2,x20
	mov	x3,x21
	adr	x4,s_title
	mov	x5,WF_RESIZABLE or WF_GLYPH_LIGHT
	bl	ui_window_ex
	tbnz	x0,63,quit
	adr	x0,s_menus
	bl	ui_menu
	mov	x0,ID_DRAW1
	mov	x1,UMS_CHECKED
	bl	ui_menu_state
	mov	x0,MIN_W
	mov	x1,MIN_H
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	bl	cards_init
	adr	x0,s_btns
	bl	band_set
	mov	x0,x20
	mov	x1,x21
	bl	set_size
	cbz	x19,.deal
	ldrb	w1,[x19]			; graine donnee ?
	sub	w1,w1,'0'
	cmp	w1,9
	b.hi	.deal
	mov	x0,x19
	bl	parse_dec
	adr	x1,ck_seed
	str	w0,[x1]
.deal:	bl	deal
.loop:	ldv	x1,win_on
	mov	x0,16
	cbnz	x1,.wait
	ldv	x1,g_running
	mov	x0,250
	cbnz	x1,.wait
	mov	x0,1000
.wait:	bl	ui_event
	mov	x19,x0
	bl	tick
	cbz	x19,.loop
	cmp	x19,WEV_CLOSE
	b.eq	quit
	cmp	x19,WEV_MENU
	b.eq	.menu
	cmp	x19,WEV_KEY
	b.eq	.key
	cmp	x19,WEV_MOUSE
	b.eq	.mouse
	mov	x0,0
	bl	ui_w_event
	b	.loop
.menu:	adr	x0,ui_ev
	ldr	w0,[x0,EV_CODE]
	bl	command
	b	.loop
.key:	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.loop
	ldr	w1,[x0,EV_CHAR]
	cmp	w1,' '
	b.eq	.space
	mov	x0,0
	bl	ui_menu_key
	tbnz	x0,63,.loop
	bl	command
	b	.loop
.space:	bl	turn_stock
	b	.loop
.mouse:	bl	on_mouse
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; x0 = ID_* de la barre de menus ou du bandeau
command:
	prologue
	cmp	x0,ID_NEW
	b.ne	.c1
	bl	deal
	b	.out
.c1:	cmp	x0,ID_UNDO
	b.ne	.c2
	bl	do_undo
	b	.out
.c2:	cmp	x0,ID_AUTO
	b.ne	.c3
	bl	auto_all
	b	.out
.c3:	cmp	x0,ID_DRAW1
	b.eq	.d1
	cmp	x0,ID_DRAW3
	b.ne	.c4
	mov	x0,1
	b	.draw
.d1:	mov	x0,0
.draw:	stv	x0,g_draw3
	mov	x1,UMS_CHECKED
	mov	x2,0
	cmp	x0,0
	csel	x1,x1,x2,ne
	mov	x0,ID_DRAW3
	bl	ui_menu_state
	ldv	x1,g_draw3
	mov	x2,UMS_CHECKED
	cmp	x1,0
	csel	x1,x2,xzr,eq
	mov	x0,ID_DRAW1
	bl	ui_menu_state
	bl	deal
	b	.out
.c4:	cmp	x0,ID_QUIT
	b.eq	quit
.out:	epilogue

; x0 = largeur, x1 = hauteur : apres un redimensionnement
on_resize:
	prologue
	bl	set_size
	stv	xzr,g_press			; (un glisser en cours est abandonne)
	stv	xzr,dr_on
	stv	xzr,win_on			; (l'animation aussi)
	bl	paint
	epilogue

set_size:
	prologue
	bl	layers_size
	mov	x0,7
	bl	cards_layout
	ldv	x0,c_h
	lsr	x0,x0,4
	cmp	x0,3
	mov	x1,3
	csel	x0,x1,x0,lo
	stv	x0,g_down			; pas d'une carte cachee
	ldv	x0,c_w
	mov	x1,7
	mul	x0,x0,x1
	lsr	x0,x0,5
	stv	x0,g_fan			; rebut en eventail
	epilogue

; le temps qui passe : animation de fin, chronometre
tick:
	prologue
	ldv	x0,win_on
	cbz	x0,.time
	sys	SYS_TIME_MS
	ldv	x1,g_anim_t
	sub	x1,x0,x1
	cmp	x1,16
	b.lo	.out
	stv	x0,g_anim_t
	bl	win_step
	cbnz	x0,.out
	bl	paint				; finie : le message
	b	.out
.time:	ldv	x0,g_running
	cbz	x0,.out
	sys	SYS_TIME_MS
	ldv	x1,g_t0
	sub	x0,x0,x1
	mov	x1,1000
	udiv	x0,x0,x1
	ldv	x1,g_elapsed
	cmp	x0,x1
	b.eq	.out
	stv	x0,g_elapsed
	bl	lay_begin
	bl	status_text
	bl	band_draw
	bl	lay_end
	mov	x0,0
	mov	x1,0
	ldv	x2,g_w
	mov	x3,BAND_H
	bl	compose
.out:	epilogue

; x0 = pile -> x0 = adresse
pile_ptr:
	mov	x1,PILE_SIZE
	adr	x2,g_s
	madd	x0,x0,x1,x2
	ret

; x0 = pile, w1 = carte (avec UP) : l'ajoute (feuille, garde x3..)
pile_push:
	mov	x2,PILE_SIZE
	mul	x0,x0,x2
	adr	x2,g_s
	add	x0,x0,x2
	ldrb	w2,[x0]
	add	w2,w2,1
	strb	w2,[x0]
	strb	w1,[x0,x2]
	ret

; x0 = pile -> x0 = x
pile_x:
	cmp	x0,STOCK
	b.eq	.x
	cmp	x0,WASTE
	b.eq	.x
	add	x1,x0,1				; fondations : colonnes 3 a 6
	cmp	x0,TAB
	sub	x0,x0,TAB
	csel	x0,x1,x0,lo
.x:	b	ck_col_x

; x0 = pile de colonne -> x0 = pas de ses cartes visibles (serre pour tenir)
up_dy:
	prologue
	bl	pile_ptr
	ldrb	w1,[x0]
	mov	x2,0				; visibles
	mov	x3,1
.c:	cmp	x3,x1
	b.hi	.d
	ldrb	w4,[x0,x3]
	tbz	w4,6,.n
	add	x2,x2,1
.n:	add	x3,x3,1
	b	.c
.d:	sub	x3,x1,x2			; cachees
	ldv	x5,c_dy_max
	cmp	x2,2
	b.lo	.ret
	ldv	x4,g_h
	sub	x4,x4,6
	ldv	x6,l_tab
	sub	x4,x4,x6
	ldv	x6,c_h
	sub	x4,x4,x6
	ldv	x6,g_down
	mul	x6,x6,x3
	sub	x4,x4,x6
	sub	x2,x2,1
	sdiv	x4,x4,x2
	cmp	x4,x5
	csel	x5,x4,x5,lt
	ldv	x4,c_dy_min
	cmp	x5,x4
	csel	x5,x4,x5,lt
.ret:	mov	x0,x5
	epilogue

; x0 = pile, x1 = carte -> x0 = x, x1 = y
card_pos:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	pile_x
	mov	x21,x0
	cmp	x19,TAB
	b.hs	.tab
	ldv	x22,l_top
	cmp	x19,WASTE
	b.ne	.out
	ldv	x0,g_draw3
	cbz	x0,.out
	mov	x0,WASTE
	bl	pile_ptr
	ldrb	w0,[x0]
	subs	x0,x0,3
	csel	x0,xzr,x0,lt			; premiere de l'eventail
	subs	x1,x20,x0
	b.lt	.out
	ldv	x2,g_fan
	madd	x21,x1,x2,x21
	b	.out
.tab:	ldv	x22,l_tab
	mov	x0,x19
	bl	up_dy
	mov	x3,x0
	mov	x0,x19
	bl	pile_ptr
	ldv	x4,g_down
	mov	x1,0
.k:	cmp	x1,x20
	b.hs	.out
	add	x2,x1,1
	ldrb	w2,[x0,x2]
	tst	w2,UP
	csel	x2,x3,x4,ne
	add	x22,x22,x2
	add	x1,x1,1
	b	.k
.out:	mov	x0,x21
	mov	x1,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la donne
deal:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,64
	mov	x1,0
.i:	strb	w1,[sp,x1]
	add	x1,x1,1
	cmp	x1,52
	b.lo	.i
	mov	x19,51
.sh:	add	x0,x19,1
	bl	ck_rnd_n
	ldrb	w1,[sp,x19]
	ldrb	w2,[sp,x0]
	strb	w2,[sp,x19]
	strb	w1,[sp,x0]
	subs	x19,x19,1
	b.ne	.sh
	adr	x0,g_s
	mov	x1,0
.z:	strb	wzr,[x0],PILE_SIZE
	add	x1,x1,1
	cmp	x1,NPILE
	b.lo	.z
	mov	x19,0				; colonne
	mov	x20,0				; carte de la donne
.col:	mov	x21,0
.r:	ldrb	w1,[sp,x20]
	add	x20,x20,1
	cmp	x21,x19
	b.ne	.h
	orr	w1,w1,UP
.h:	add	x0,x19,TAB
	bl	pile_push
	add	x21,x21,1
	cmp	x21,x19
	b.ls	.r
	add	x19,x19,1
	cmp	x19,7
	b.lo	.col
.st:	cmp	x20,52
	b.hs	.e
	ldrb	w1,[sp,x20]
	mov	x0,STOCK
	bl	pile_push
	add	x20,x20,1
	b	.st
.e:	adr	x0,g_s
	str	wzr,[x0,S_SCORE]
	stv	xzr,g_nundo
	stv	xzr,g_elapsed
	stv	xzr,g_running
	stv	xzr,g_won
	stv	xzr,g_press
	stv	xzr,dr_on
	stv	xzr,win_on
	bl	paint
	add	sp,sp,64
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

push_undo:
	prologue
	str	x19,[sp,-16]!
	ldv	x19,g_nundo
	cmp	x19,UNDO_MAX
	b.lo	.put
	adr	x0,g_undo			; plein : le plus ancien s'en va
	mov	x1,SS_SIZE
	add	x1,x0,x1
	movl	x2,SS_SIZE*(UNDO_MAX-1)
	bl	mem_move
	sub	x19,x19,1
.put:	adr	x0,g_undo
	mov	x1,SS_SIZE
	madd	x0,x19,x1,x0
	adr	x1,g_s
	mov	x2,SS_SIZE
	bl	ck_move8
	add	x19,x19,1
	stv	x19,g_nundo
	ldr	x19,[sp],16
	epilogue

do_undo:
	prologue
	ldv	x1,g_nundo
	cbz	x1,.out
	ldv	x0,g_won
	cbnz	x0,.out
	sub	x1,x1,1
	stv	x1,g_nundo
	adr	x0,g_undo
	mov	x2,SS_SIZE
	madd	x1,x1,x2,x0
	adr	x0,g_s
	bl	ck_move8
	mov	w0,-2
	bl	add_score
	bl	paint
.out:	epilogue

; w0 = points (le score ne descend pas sous 0)
add_score:
	adr	x1,g_s
	ldr	w2,[x1,S_SCORE]
	adds	w2,w2,w0
	csel	w2,wzr,w2,mi
	str	w2,[x1,S_SCORE]
	ret

start_clock:
	ldv	x0,g_running
	cbnz	x0,.ret
	ldv	x0,g_won
	cbnz	x0,.ret
	stp	x30,xzr,[sp,-16]!
	sys	SYS_TIME_MS
	ldv	x1,g_elapsed
	mov	x2,1000
	msub	x0,x1,x2,x0			; (reprend apres une pause de zero seconde)
	stv	x0,g_t0
	mov	x0,1
	stv	x0,g_running
	ldp	x30,xzr,[sp],16
.ret:	ret

; x0 = pile -> x0 = carte du sommet avec UP (-1 : vide)
pile_top:
	mov	x1,PILE_SIZE
	adr	x2,g_s
	madd	x1,x0,x1,x2
	ldrb	w0,[x1]
	cbz	w0,.none
	ldrb	w0,[x1,x0]
	ret
.none:	mov	x0,-1
	ret

; x0 = carte (sans UP) -> x0 = 1 si elle monte sur la fondation de son enseigne
can_found:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x1,13
	udiv	x0,x19,x1
	add	x0,x0,FOUND
	bl	pile_top
	mov	x1,13
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19			; rang
	tbnz	x0,63,.ace
	and	x0,x0,0x3F
	udiv	x3,x0,x1
	msub	x3,x3,x1,x0
	add	x3,x3,1
	cmp	x2,x3
	cset	x0,eq
	b	.out
.ace:	cmp	x2,0
	cset	x0,eq
.out:	ldr	x19,[sp],16
	epilogue

; x0 = carte -> x0 = 1 rouge
card_red:
	mov	x1,13
	udiv	x0,x0,x1
	sub	x0,x0,1
	cmp	x0,1
	cset	x0,ls
	ret

; x0 = carte (sans UP), x1 = colonne -> x0 = 1 si elle s'y pose
can_tab:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,x1
	bl	pile_top
	mov	x1,13
	tbz	x0,63,.t
	udiv	x2,x19,x1			; vide : un roi
	msub	x2,x2,x1,x19
	cmp	x2,12
	cset	x0,eq
	b	.out
.t:	tbz	x0,6,.no
	and	x20,x0,0x3F
	mov	x0,x20
	bl	card_red
	mov	x2,x0
	mov	x0,x19
	bl	card_red
	cmp	x0,x2
	b.eq	.no
	mov	x1,13
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19
	udiv	x3,x20,x1
	msub	x3,x3,x1,x20
	add	x2,x2,1
	cmp	x2,x3
	cset	x0,eq
	b	.out
.no:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = de, x1 = indice, x2 = vers : deplace [indice ..] (regles verifiees)
do_move:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	push_undo
	mov	x0,x19
	bl	pile_ptr
	mov	x22,x0
	ldrb	w23,[x22]
	mov	x24,x20
.c:	cmp	x24,x23
	b.hs	.d
	add	x1,x22,x24
	ldrb	w1,[x1,1]
	orr	w1,w1,UP
	mov	x0,x21
	bl	pile_push
	add	x24,x24,1
	b	.c
.d:	strb	w20,[x22]
	cmp	x19,WASTE			; le score de Windows
	b.ne	.s1
	cmp	x21,TAB
	b.lo	.s1
	mov	w0,5
	bl	add_score
.s1:	sub	x23,x21,FOUND			; vers une fondation ?
	cmp	x23,4
	cset	x23,lo
	sub	x24,x19,FOUND			; d'une fondation ?
	cmp	x24,4
	cset	x24,lo
	cbz	x23,.s2
	cbnz	x24,.s2
	mov	w0,10
	bl	add_score
.s2:	cbz	x24,.s3
	cmp	x21,TAB
	b.lo	.s3
	mov	w0,-15
	bl	add_score
.s3:	cmp	x19,TAB				; la carte cachee se retourne
	b.lo	.s4
	cbz	x20,.s4
	ldrb	w0,[x22,x20]
	tbnz	w0,6,.s4
	orr	w0,w0,UP
	strb	w0,[x22,x20]
	mov	w0,5
	bl	add_score
.s4:	bl	start_clock
	bl	check_win
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

check_win:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
	mov	x20,FOUND
.n:	mov	x0,x20
	bl	pile_ptr
	ldrb	w0,[x0]
	add	x19,x19,x0
	add	x20,x20,1
	cmp	x20,TAB
	b.lo	.n
	cmp	x19,52
	b.lo	.out
	mov	x0,1
	stv	x0,g_won
	stv	xzr,g_running
	ldv	x0,g_t0				; prime de temps
	mov	x19,x0
	sys	SYS_TIME_MS
	sub	x0,x0,x19
	mov	x1,1000
	udiv	x0,x0,x1
	stv	x0,g_elapsed
	cmp	x0,30
	b.ls	.x
	movl	x1,700000
	udiv	x1,x1,x0
	mov	x2,10
	udiv	x0,x1,x2
	bl	add_score
.x:	mov	x19,0
.xl:	add	x0,x19,FOUND
	bl	pile_x
	adr	x1,wa_fx
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,4
	b.lo	.xl
	ldv	x0,l_top
	bl	win_start
	sys	SYS_TIME_MS
	stv	x0,g_anim_t
.out:	ldp	x19,x20,[sp],16
	epilogue

; le talon : une ou trois cartes, ou le rebut qui revient
turn_stock:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x0,g_won
	cbnz	x0,.out
	adr	x19,g_s+STOCK*PILE_SIZE
	adr	x20,g_s+WASTE*PILE_SIZE
	ldrb	w0,[x19]
	cbnz	w0,.draw
	ldrb	w21,[x20]
	cbz	w21,.out
	bl	push_undo
.back:	cbz	w21,.bd
	ldrb	w1,[x20,x21]
	and	w1,w1,0x3F
	mov	x0,STOCK
	bl	pile_push
	sub	w21,w21,1
	b	.back
.bd:	strb	wzr,[x20]
	ldv	x0,g_draw3
	mov	w1,-100
	mov	w2,-20
	cmp	x0,0
	csel	w0,w2,w1,ne
	bl	add_score
	b	.done
.draw:	bl	push_undo
	ldv	x22,g_draw3
	mov	x21,1
	cbz	x22,.dl
	mov	x21,3
.dl:	cbz	x21,.done
	ldrb	w2,[x19]
	cbz	w2,.done
	ldrb	w1,[x19,x2]
	sub	w2,w2,1
	strb	w2,[x19]
	orr	w1,w1,UP
	mov	x0,WASTE
	bl	pile_push
	sub	x21,x21,1
	b	.dl
.done:	bl	start_clock
	bl	paint
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pile -> x0 = 1 si son sommet est monte
to_foundation:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	sub	x1,x19,FOUND
	cmp	x1,4
	b.lo	.no
	bl	pile_top
	tbnz	x0,63,.no
	tbz	x0,6,.no
	and	x20,x0,0x3F
	mov	x0,x20
	bl	can_found
	cbz	x0,.no
	mov	x0,x19
	bl	pile_ptr
	ldrb	w1,[x0]
	sub	x1,x1,1
	mov	x0,x20
	mov	x2,13
	udiv	x2,x0,x2
	add	x2,x2,FOUND
	mov	x0,x19
	bl	do_move
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; Auto : tout ce qui peut monter
auto_all:
	prologue
	stp	x19,x20,[sp,-16]!
.again:	ldv	x0,g_won
	cbnz	x0,.end
	mov	x20,0
	mov	x0,WASTE
	bl	to_foundation
	orr	x20,x20,x0
	mov	x19,TAB
.p:	mov	x0,x19
	bl	to_foundation
	orr	x20,x20,x0
	add	x19,x19,1
	cmp	x19,NPILE
	b.lo	.p
	cbnz	x20,.again
.end:	bl	paint
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = point -> x0 = 1 trouve, x1 = pile, x2 = carte (-1 : la pile vide)
hit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0				; pile
.p:	mov	x0,x21
	bl	pile_ptr
	ldrb	w22,[x0]
.i:	cbz	x22,.base
	sub	x22,x22,1
	mov	x0,x21
	mov	x1,x22
	bl	card_pos
	mov	x2,x0
	mov	x3,x1
	mov	x0,x19
	mov	x1,x20
	bl	.in
	cbz	x0,.i
	mov	x0,1
	mov	x1,x21
	mov	x2,x22
	b	.out
.base:	mov	x0,x21
	mov	x1,0
	bl	pile_x
	mov	x2,x0
	ldv	x3,l_top
	cmp	x21,TAB
	b.lo	.b
	ldv	x3,l_tab
.b:	mov	x0,x19
	mov	x1,x20
	bl	.in
	cbz	x0,.np
	mov	x0,1
	mov	x1,x21
	mov	x2,-1
	b	.out
.np:	add	x21,x21,1
	cmp	x21,NPILE
	b.lo	.p
	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.in:	cmp	x0,x2				; x0, x1 dans la carte en x2, x3 ?
	b.lt	.nin
	cmp	x1,x3
	b.lt	.nin
	ldv	x4,c_w
	add	x2,x2,x4
	cmp	x0,x2
	b.ge	.nin
	ldv	x4,c_h
	add	x3,x3,x4
	cmp	x1,x3
	b.ge	.nin
	mov	x0,1
	ret
.nin:	mov	x0,0
	ret

; x0 = pile, x1 = carte : le toucher sans glisser, vers la destination
; evidente : fondation (la carte du sommet), colonne
click_send:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	pile_ptr
	ldrb	w1,[x0]
	sub	x1,x1,1
	add	x2,x0,x20
	ldrb	w22,[x2,1]
	and	w22,w22,0x3F			; la carte
	cmp	x20,x1
	b.ne	.tabs
	mov	x0,x19
	bl	to_foundation
	cbnz	x0,.out
.tabs:	mov	x21,TAB
.t:	cmp	x21,x19
	b.eq	.n
	mov	x0,x22
	mov	x1,x21
	bl	can_tab
	cbz	x0,.n
	cmp	x19,TAB				; un roi deja au fond ne bouge pas
	b.lo	.go
	cbnz	x20,.go
	mov	x0,x21
	bl	pile_ptr
	ldrb	w0,[x0]
	cbz	w0,.n
.go:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	do_move
	b	.out
.n:	add	x21,x21,1
	cmp	x21,NPILE
	b.lo	.t
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris et doigt
on_mouse:
	prologue
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	ldrsw	x2,[x0,EV_X]
	ldrsw	x3,[x0,EV_Y]
	ldr	w4,[x0,EV_CODE]
	cmp	w1,1
	b.eq	.press
	cmp	w1,2
	b.eq	.rel
	ldv	x5,g_press
	cbz	x5,.out
	tbz	w4,0,.rel			; (relache perdu)
	mov	x0,x2
	mov	x1,x3
	bl	pointer_move
	b	.out
.press:	tbnz	w4,0,.left
	tbz	w4,1,.out
	ldv	x0,g_won
	cbnz	x0,.out
	bl	auto_all			; bouton droit
	b	.out
.left:	mov	x0,x2
	mov	x1,x3
	bl	pointer_down
	b	.out
.rel:	mov	x0,x2
	mov	x1,x3
	bl	pointer_up
.out:	epilogue

; x0, x1 = point : appui
pointer_down:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldv	x0,win_on			; l'animation : passe au message
	cbz	x0,.w
	stv	xzr,win_on
	bl	paint
	b	.out
.w:	ldv	x0,g_won
	cbz	x0,.b
	bl	deal
	b	.out
.b:	cmp	x20,BAND_H
	b.ge	.t
	mov	x0,x19
	bl	band_hit
	tbz	x0,63,.btn
	cbz	x1,.out
	bl	ui_window_drag
	b	.out
.btn:	adr	x1,s_btn_ids
	ldrb	w0,[x1,x0]
	bl	command
	b	.out
.t:	mov	x0,x19
	mov	x1,x20
	bl	hit
	cbz	x0,.out
	mov	x21,x1
	mov	x22,x2
	cmp	x21,STOCK
	b.ne	.card
	bl	turn_stock
	b	.out
.card:	tbnz	x22,63,.out
	sys	SYS_TIME_MS			; second appui d'un double-clic : deja parti
	ldv	x1,g_sent_t
	sub	x0,x0,x1
	cmp	x0,CK_TAP_MS
	b.hs	.h
	ldv	x0,g_sent_x
	sub	x0,x0,x19
	cmp	x0,0
	cneg	x0,x0,lt
	cmp	x0,16
	b.hs	.h
	ldv	x0,g_sent_y
	sub	x0,x0,x20
	cmp	x0,0
	cneg	x0,x0,lt
	cmp	x0,16
	b.lo	.out
.h:	mov	x0,x21
	bl	pile_ptr
	ldrb	w1,[x0]
	cmp	x21,TAB				; rebut, fondation : le sommet seul
	b.hs	.up
	sub	x22,x1,1
.up:	add	x2,x0,x22
	ldrb	w2,[x2,1]
	tbz	w2,6,.out			; (cachee : on n'y touche pas)
	mov	x0,x21
	mov	x1,x22
	bl	card_pos
	sub	x0,x19,x0
	sub	x1,x20,x1
	stv	x0,g_off_x
	stv	x1,g_off_y
	stv	x19,g_px
	stv	x20,g_py
	stv	x21,g_src
	stv	x22,g_idx
	mov	x0,1
	stv	x0,g_press
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = point : le doigt bouge
pointer_move:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	ldv	x0,g_off_x
	sub	x19,x21,x0			; coin du paquet
	ldv	x0,g_off_y
	sub	x20,x22,x0
	ldv	x0,dr_on
	cbnz	x0,.drag
	ldv	x0,g_px				; encore un simple appui ?
	sub	x0,x21,x0
	cmp	x0,0
	cneg	x0,x0,lt
	ldv	x1,g_py
	sub	x1,x22,x1
	cmp	x1,0
	cneg	x1,x1,lt
	add	x0,x0,x1
	cmp	x0,DRAG_MIN
	b.lt	.out
	ldv	x0,g_src			; le paquet quitte sa place
	bl	pile_ptr
	ldrb	w1,[x0]
	ldv	x2,g_idx
	sub	x3,x1,x2
	stv	x3,dr_n
	add	x0,x0,x2
	add	x0,x0,1
	adr	x1,dr_cards
	mov	x2,0
.c:	cmp	x2,x3
	b.hs	.d
	ldrb	w4,[x0,x2]
	and	w4,w4,0x3F
	strb	w4,[x1,x2]
	add	x2,x2,1
	b	.c
.d:	ldv	x0,c_dy_max
	stv	x0,dr_dy
	stv	x19,dr_x
	stv	x20,dr_y
	mov	x0,1
	stv	x0,dr_on
	bl	paint
	b	.out
.drag:	mov	x0,x19
	mov	x1,x20
	bl	drag_move
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = point : relache
pointer_up:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldv	x0,g_press
	cbz	x0,.out
	stv	xzr,g_press
	ldv	x21,g_src
	ldv	x22,g_idx
	ldv	x0,dr_on
	cbnz	x0,.drop
	stv	x19,g_sent_x
	stv	x20,g_sent_y
	sys	SYS_TIME_MS
	stv	x0,g_sent_t
	mov	x0,x21
	mov	x1,x22
	bl	click_send
	bl	paint
	b	.out
.drop:	stv	xzr,dr_on
	ldv	x0,g_off_x			; le centre de la carte du dessus
	sub	x19,x19,x0
	ldv	x0,c_w
	add	x19,x19,x0,lsr 1
	ldv	x0,g_off_y
	sub	x20,x20,x0
	ldv	x0,c_h
	add	x20,x20,x0,lsr 1
	mov	x0,x21				; la carte
	bl	pile_ptr
	add	x1,x0,x22
	ldrb	w23,[x1,1]
	and	w23,w23,0x3F
	ldrb	w1,[x0]
	sub	x24,x1,x22			; cartes emportees
	ldv	x0,l_top			; rangee du haut : sa fondation
	ldv	x1,c_h
	add	x0,x0,x1
	add	x0,x0,8
	cmp	x20,x0
	b.ge	.tabs
	mov	x0,3
	bl	ck_col_x
	sub	x0,x0,8
	cmp	x19,x0
	b.lt	.paint
	cmp	x24,1
	b.ne	.paint
	mov	x1,13
	udiv	x1,x23,x1
	add	x1,x1,FOUND
	cmp	x1,x21
	b.eq	.paint
	mov	x0,x23
	bl	can_found
	cbz	x0,.paint
	mov	x1,13
	udiv	x2,x23,x1
	add	x2,x2,FOUND
	mov	x0,x21
	mov	x1,x22
	bl	do_move
	b	.paint
.tabs:	mov	x0,x19				; la colonne sous le centre
	mov	x1,7
	bl	ck_col_at
	add	x2,x0,TAB
	cmp	x2,x21
	b.eq	.paint
	str	x2,[sp,-16]!
	mov	x0,x23
	mov	x1,x2
	bl	can_tab
	ldr	x2,[sp],16
	cbz	x0,.paint
	mov	x0,x21
	mov	x1,x22
	bl	do_move
.paint:	bl	paint
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = texte du bandeau, w1 = couleur
status_text:
	prologue
	adr	x0,g_txt
	adr	x1,s_score
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	adr	x0,g_s
	ldr	w0,[x0,S_SCORE]
	bl	fmt_dec
	adr	x1,s_time
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	ldv	x0,g_elapsed
	bl	fmt_dec
	adr	x1,s_sec
	bl	str_cat
	adr	x0,g_txt
	movl	w1,TAPIS_TEXT
	epilogue

; la table, puis tout est montre
paint:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	lay_begin
	bl	status_text
	bl	table_bg
	mov	x0,STOCK			; le talon
	bl	pile_ptr
	ldrb	w19,[x0]
	mov	x0,STOCK
	bl	pile_x
	mov	x20,x0
	cbz	w19,.empty
	cmp	w19,1
	b.ls	.one
	mov	x0,CARD_BACK			; (une epaisseur)
	mov	x1,x20
	ldv	x2,l_top
	sub	x2,x2,2
	bl	card_blit
.one:	mov	x0,CARD_BACK
	mov	x1,x20
	ldv	x2,l_top
	bl	card_blit
	b	.slots
.empty:	mov	x0,x20
	ldv	x1,l_top
	mov	x2,-1
	bl	card_slot
	mov	x0,WASTE
	bl	pile_ptr
	ldrb	w0,[x0]
	cbz	w0,.slots
	adr	x0,s_again_g			; rien que le signe : on reprend le rebut
	ldv	x1,c_h
	lsr	x1,x1,1
	stp	x1,xzr,[sp,-16]!
	adr	x2,ui_font_b
	bl	ui_text_width
	ldp	x4,xzr,[sp],16
	ldv	x1,c_w
	sub	x1,x1,x0
	asr	x1,x1,1
	add	x1,x1,x20
	ldv	x2,c_h
	add	x2,x2,x4,lsr 1
	asr	x2,x2,1
	add	x2,x2,x4,lsr 2
	ldv	x3,l_top
	add	x2,x2,x3
	adr	x0,s_again_g
	movl	w3,TAPIS_SLOT_L
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
.slots:	mov	x0,WASTE			; les places vides
	bl	pile_x
	ldv	x1,l_top
	mov	x2,-1
	bl	card_slot
	mov	x19,0
.f:	add	x0,x19,FOUND
	bl	pile_x
	ldv	x1,l_top
	mov	x2,x19
	bl	card_slot
	add	x19,x19,1
	cmp	x19,4
	b.lo	.f
	mov	x19,0
.t:	mov	x0,x19
	bl	ck_col_x
	ldv	x1,l_tab
	mov	x2,-1
	bl	card_slot
	add	x19,x19,1
	cmp	x19,7
	b.lo	.t
	mov	x19,WASTE			; les cartes
.pile:	mov	x0,x19
	bl	pile_ptr
	mov	x20,x0
	ldrb	w21,[x20]			; cartes a poser
	ldv	x0,dr_on
	cbz	x0,.from
	ldv	x0,g_src
	cmp	x0,x19
	b.ne	.from
	ldv	x21,g_idx
.from:	ldrb	w0,[x20]			; premiere carte visible
	mov	x22,0
	cmp	x19,WASTE
	b.ne	.ff
	sub	x22,x0,1
	ldv	x1,g_draw3
	cbz	x1,.cl
	sub	x22,x0,3
.cl:	cmp	x22,0
	csel	x22,xzr,x22,lt
	b	.c
.ff:	cmp	x19,TAB
	b.hs	.c
	sub	x22,x0,2
	cmp	x22,0
	csel	x22,xzr,x22,lt
.c:	cmp	x22,x21
	b.hs	.np
	mov	x0,x19
	mov	x1,x22
	bl	card_pos
	mov	x2,x1
	mov	x1,x0
	add	x0,x20,x22
	ldrb	w0,[x0,1]
	tbnz	w0,6,.show
	mov	w0,CARD_BACK+UP
.show:	and	w0,w0,0x3F
	bl	card_blit
	add	x22,x22,1
	b	.c
.np:	add	x19,x19,1
	cmp	x19,NPILE
	b.lo	.pile
	ldv	x0,g_won
	cbz	x0,.end
	ldv	x0,win_on
	cbnz	x0,.end
	adr	x0,s_won
	adr	x1,s_replay
	bl	win_box
.end:	bl	lay_end
	bl	compose_all
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

	align 4
s_title		db 'Solitaire',0
s_score		db 'Score : ',0
s_time		db '    Temps : ',0
s_sec		db ' s',0
s_again_g	db 0xE2,0x86,0xBB,0
s_won		db 'Gagn',0xC3,0xA9,' !',0
s_replay	db 'Toucher pour rejouer',0
s_btns		db 'Nouvelle',0,'D',0xC3,0xA9,'faire',0,'Auto',0,0
s_btn_ids	db ID_NEW,ID_UNDO,ID_AUTO
s_menus		db 'MPartie',10
		db 'I1',9,'Distribuer',9,'Ctrl+N',10
		db 'I2',9,'Annuler le coup',9,'Ctrl+Z',10
		db 'I3',9,'Cartes vers les fondations',10
		db '-',10
		db 'I4',9,'Tirer une carte',10
		db 'I5',9,'Tirer trois cartes',10
		db '-',10
		db 'I6',9,'Quitter',10,0
		align 4

include '../../inc/tr.inc'

segment readable writeable
kit_data
cards_data
g_nundo		dd 0
g_won		dd 0
g_draw3		dd 0
g_running	dd 0
g_t0		dd 0
g_elapsed	dd 0
g_down		dd 0
g_fan		dd 0
g_press		dd 0
g_px		dd 0
g_py		dd 0
g_off_x		dd 0
g_off_y		dd 0
g_src		dd 0
g_idx		dd 0
g_sent_t	dd 0
g_sent_x	dd 0
g_sent_y	dd 0
g_anim_t	dd 0
g_s		rb SS_SIZE
g_undo		rb SS_SIZE*UNDO_MAX
g_txt		rb 96
		align 8
ui_data
