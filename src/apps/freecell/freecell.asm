; ============================================================================
;  FreeCell - le jeu d'Onyx (user/Apps/freecell), en assembleur. Les 52 cartes
;  sont en huit colonnes ; quatre cases libres (en haut a gauche) tiennent une
;  carte chacune ; on monte les quatre fondations (en haut a droite) par
;  enseigne depuis l'as. On glisse les cartes a la souris ou au doigt : une
;  colonne prend une carte de un de moins et de l'autre couleur (n'importe
;  laquelle si elle est vide), une suite se deplace d'un coup si les cases
;  libres et les colonnes vides le permettent. Un toucher sans glisser envoie
;  la carte a la destination evidente. Les cartes qui ne servent plus montent
;  seules. Les parties sont numerotees de 1 a 32000, les memes que FreeCell de
;  Microsoft. `freecell <numero>` lance cette partie.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W		= 720
WIN_H		= 540
MIN_W		= 540
MIN_H		= 400
NCOL		= 8
COL_SIZE	= 56				; +0 nombre, +1 cartes
S_CELL		= NCOL*COL_SIZE			; 4 octets, -1 : vide
S_FOUND		= S_CELL+4			; 4 octets : rang du sommet, -1 : aucun
S_MOVES		= S_FOUND+4
FS_SIZE		= S_MOVES+8
UNDO_MAX	= 128
SRC_CELL	= 8				; source : 0..7 colonne, 8..11 case
ID_NEW		= 1
ID_SELECT	= 2
ID_RESTART	= 3
ID_UNDO		= 4
ID_QUIT		= 5
GAME_MAX	= 32000
MSG_MS		= 2500

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
	mov	x0,MIN_W
	mov	x1,MIN_H
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	bl	cards_init
	bl	band_text
	adr	x0,g_btns
	bl	band_set
	mov	x0,x20
	mov	x1,x21
	bl	set_size
	mov	x0,0
	cbz	x19,.deal
	ldrb	w1,[x19]			; numero donne ?
	sub	w1,w1,'1'
	cmp	w1,8
	b.hi	.deal
	mov	x0,x19
	bl	parse_dec
	mov	x1,GAME_MAX
	cmp	x0,x1
	b.ls	.deal
	mov	x0,0
.deal:	cbnz	x0,.go
	bl	random_game
.go:	bl	fc_new
.loop:	ldv	x1,win_on
	mov	x0,500
	cbz	x1,.wait
	mov	x0,16
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
	mov	x0,0
	bl	ui_menu_key
	tbnz	x0,63,.loop
	bl	command
	b	.loop
.mouse:	bl	on_mouse
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; -> x0 = numero de partie au hasard
random_game:
	prologue
	mov	x0,GAME_MAX
	bl	ck_rnd_n
	add	x0,x0,1
	epilogue

; x0 = ID_* de la barre de menus ou du bandeau
command:
	prologue
	cmp	x0,ID_NEW
	b.ne	.c1
	bl	random_game
	bl	fc_new
	b	.out
.c1:	cmp	x0,ID_SELECT
	b.ne	.c2
	bl	select_game
	b	.out
.c2:	cmp	x0,ID_RESTART
	b.ne	.c3
	ldv	x0,g_game
	bl	fc_new
	b	.out
.c3:	cmp	x0,ID_UNDO
	b.ne	.c4
	bl	do_undo
	b	.out
.c4:	cmp	x0,ID_QUIT
	b.eq	quit
.out:	epilogue

; Partie > Choisir une partie
select_game:
	prologue
	adr	x1,g_ask
	ldv	x0,g_game
	bl	fmt_dec
	adr	x0,s_sel_t
	adr	x1,s_sel_q
	bl	tr1
	adr	x2,g_ask
	mov	x3,16
	bl	ui_ask
	cbz	x0,.out
	adr	x0,g_ask
	bl	parse_dec
	cbz	x0,.out
	mov	x1,GAME_MAX
	cmp	x0,x1
	b.hi	.out
	bl	fc_new
.out:	epilogue

; x0 = largeur, x1 = hauteur : apres un redimensionnement
on_resize:
	prologue
	stp	xzr,xzr,[sp,-16]!
	bl	set_size
	stv	xzr,g_press			; (un glisser en cours est abandonne)
	stv	xzr,dr_on
	stv	xzr,win_on			; (l'animation aussi)
	bl	paint
	add	sp,sp,16
	epilogue

set_size:
	prologue
	bl	layers_size
	mov	x0,NCOL
	bl	cards_layout
	epilogue

; le temps qui passe : animation de fin, message
tick:
	prologue
	ldv	x0,win_on
	cbz	x0,.msg
	sys	SYS_TIME_MS
	ldv	x1,g_anim_t
	sub	x1,x0,x1
	cmp	x1,16
	b.lo	.msg
	stv	x0,g_anim_t
	bl	win_step
	cbnz	x0,.msg
	bl	paint				; finie : le message
.msg:	ldv	x0,g_msg
	cbz	x0,.out
	sys	SYS_TIME_MS
	ldv	x1,g_msg
	sub	x0,x0,x1
	cmp	x0,MSG_MS
	b.lo	.out
	stv	xzr,g_msg
	bl	paint
.out:	epilogue

; x0 = numero : distribue, comme FreeCell de Microsoft (son rand () de la
; bibliotheque C, semee par le numero)
ms_deal:
	sub	sp,sp,224
	mov	x1,0
.i:	mov	w2,51
	sub	w2,w2,w1
	str	w2,[sp,x1,lsl 2]		; numerotation : rang * 4 + enseigne
	add	x1,x1,1
	cmp	x1,52
	b.lo	.i
	mov	w3,w0
	movl	w4,214013
	movl	w5,2531011
	mov	x1,0
.s:	mul	w3,w3,w4
	add	w3,w3,w5
	and	w3,w3,0x7FFFFFFF
	lsr	w6,w3,16
	mov	w7,52
	sub	w7,w7,w1
	udiv	w8,w6,w7
	msub	w8,w8,w7,w6
	mov	w6,51
	sub	w6,w6,w8			; j
	ldr	w7,[sp,x1,lsl 2]
	ldr	w8,[sp,x6,lsl 2]
	str	w8,[sp,x1,lsl 2]
	str	w7,[sp,x6,lsl 2]
	add	x1,x1,1
	cmp	x1,51
	b.lo	.s
	adr	x4,g_s
	mov	x1,0
.z:	mov	x2,COL_SIZE
	mul	x2,x2,x1
	strb	wzr,[x4,x2]
	add	x1,x1,1
	cmp	x1,NCOL
	b.lo	.z
	mov	x1,0
.d:	ldr	w2,[sp,x1,lsl 2]
	lsr	w3,w2,2				; rang
	and	w2,w2,3				; enseigne
	mov	w5,13
	madd	w2,w2,w5,w3			; carte
	and	x3,x1,7
	mov	x5,COL_SIZE
	mul	x3,x3,x5
	add	x3,x3,x4			; colonne
	ldrb	w5,[x3]
	add	w5,w5,1
	strb	w5,[x3]
	strb	w2,[x3,x5]
	add	x1,x1,1
	cmp	x1,52
	b.lo	.d
	add	sp,sp,224
	ret

; x0 = numero : nouvelle partie
fc_new:
	prologue
	stv	x0,g_game
	bl	ms_deal
	adr	x0,g_s
	mov	w1,-1
	str	w1,[x0,S_CELL]
	str	w1,[x0,S_FOUND]
	str	wzr,[x0,S_MOVES]
	stv	xzr,g_nundo
	stv	xzr,g_won
	stv	xzr,g_press
	stv	xzr,dr_on
	stv	xzr,win_on
	stv	xzr,g_msg
	bl	auto_play
	bl	paint
	epilogue

; x0 = colonne -> x0 = adresse
col_ptr:
	mov	x1,COL_SIZE
	adr	x2,g_s
	madd	x0,x0,x1,x2
	ret

; x0 = colonne -> x0 = pas vertical de ses cartes (serre pour tenir)
col_dy:
	prologue
	bl	col_ptr
	ldrb	w1,[x0]
	ldv	x2,c_dy_max
	cmp	x1,2
	b.lo	.ret
	sub	x1,x1,1
	ldv	x3,g_h
	ldv	x4,l_tab
	sub	x3,x3,x4
	ldv	x4,c_h
	sub	x3,x3,x4
	sub	x3,x3,6
	sdiv	x3,x3,x1
	cmp	x3,x2
	csel	x2,x3,x2,lt
	ldv	x3,c_dy_min
	cmp	x2,x3
	csel	x2,x3,x2,lt
.ret:	mov	x0,x2
	epilogue

; -> x0 = cases libres
free_cells:
	adr	x1,g_s+S_CELL
	mov	x0,0
	mov	x2,0
.l:	ldrsb	w3,[x1,x2]
	tbz	w3,31,.n
	add	x0,x0,1
.n:	add	x2,x2,1
	cmp	x2,4
	b.lo	.l
	ret

; x0 = colonne a ne pas compter -> x0 = colonnes vides
empty_cols:
	adr	x1,g_s
	mov	x2,0
	mov	x3,0
.l:	cmp	x2,x0
	b.eq	.n
	ldrb	w4,[x1]
	cbnz	w4,.n
	add	x3,x3,1
.n:	add	x1,x1,COL_SIZE
	add	x2,x2,1
	cmp	x2,NCOL
	b.lo	.l
	mov	x0,x3
	ret

; x0 = colonne visee -> x0 = plus longue suite deplacable
max_run:
	prologue
	bl	empty_cols
	mov	x4,x0
	bl	free_cells
	add	x0,x0,1
	lsl	x0,x0,x4
	epilogue

; x0 = carte -> x0 = 1 si elle monte sur sa fondation
can_found:
	mov	x1,13
	udiv	x2,x0,x1			; enseigne
	msub	x3,x2,x1,x0			; rang
	adr	x1,g_s+S_FOUND
	ldrsb	w1,[x1,x2]
	add	w1,w1,1
	cmp	w1,w3
	cset	x0,eq
	ret

; x0 = carte -> x0 = 1 rouge
card_red:
	mov	x1,13
	udiv	x0,x0,x1
	sub	x0,x0,1
	cmp	x0,1
	cset	x0,ls
	ret

; x0 = carte, x1 = colonne -> x0 = 1 si elle s'y pose
can_stack:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,x1
	bl	col_ptr
	ldrb	w1,[x0]
	mov	x2,x0
	mov	x0,1
	cbz	w1,.out
	ldrb	w20,[x2,x1]			; sommet
	mov	x0,x20
	bl	card_red
	mov	x2,x0
	mov	x0,x19
	bl	card_red
	cmp	x0,x2
	b.eq	.no
	mov	x1,13
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19			; rang de la carte
	udiv	x3,x20,x1
	msub	x3,x3,x1,x20
	add	x2,x2,1
	cmp	x2,x3
	cset	x0,eq
	b	.out
.no:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = colonne, x1 = indice -> x0 = 1 si [indice .. sommet] est une suite
is_run:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	bl	col_ptr
	mov	x19,x0
	ldrb	w21,[x19]
.l:	add	x0,x20,1
	cmp	x0,x21
	b.hs	.yes
	add	x2,x19,1
	ldrb	w22,[x2,x20]			; carte
	add	x20,x20,1
	ldrb	w3,[x2,x20]			; suivante
	mov	x0,x22
	mov	x22,x3
	mov	x4,x0
	mov	x1,13
	udiv	x5,x4,x1
	msub	x5,x5,x1,x4			; rangs
	udiv	x6,x3,x1
	msub	x6,x6,x1,x3
	add	x6,x6,1
	cmp	x5,x6
	b.ne	.no
	bl	card_red
	mov	x4,x0
	mov	x0,x22
	stp	x4,xzr,[sp,-16]!
	bl	card_red
	ldp	x4,xzr,[sp],16
	cmp	x0,x4
	b.ne	.l
.no:	mov	x0,0
	b	.out
.yes:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

push_undo:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x19,g_nundo
	cmp	x19,UNDO_MAX
	b.lo	.put
	adr	x0,g_undo			; plein : le plus ancien s'en va
	movl	x1,FS_SIZE
	add	x1,x0,x1
	movl	x2,FS_SIZE*(UNDO_MAX-1)
	bl	mem_move
	sub	x19,x19,1
.put:	adr	x0,g_undo
	mov	x1,FS_SIZE
	madd	x0,x19,x1,x0
	adr	x1,g_s
	mov	x2,FS_SIZE
	bl	ck_move8
	add	x19,x19,1
	stv	x19,g_nundo
	ldp	x19,x20,[sp],16
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
	mov	x2,FS_SIZE
	madd	x1,x1,x2,x0
	adr	x0,g_s
	bl	ck_move8
	bl	paint
.out:	epilogue

; x0 = carte -> x0 = 1 si plus rien ne peut en avoir besoin
safe:
	mov	x1,13
	udiv	x2,x0,x1			; enseigne
	msub	x3,x2,x1,x0			; rang
	cmp	x3,1
	b.ls	.yes
	sub	x4,x2,1
	cmp	x4,1
	cset	x4,ls				; rouge ?
	adr	x5,g_s+S_FOUND
	sub	x6,x3,1
	mov	x2,0
.f:	sub	x7,x2,1
	cmp	x7,1
	cset	x7,ls
	cmp	x7,x4
	b.eq	.n
	ldrsb	x8,[x5,x2]
	cmp	x8,x6
	b.lt	.no
.n:	add	x2,x2,1
	cmp	x2,4
	b.lo	.f
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = carte -> x0 = 1 si elle peut monter sans risque (et compte sur la fondation)
try_up:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	can_found
	cbz	x0,.out
	mov	x0,x19
	bl	safe
	cbz	x0,.out
	mov	x1,13
	udiv	x2,x19,x1
	adr	x1,g_s+S_FOUND
	ldrb	w3,[x1,x2]
	add	w3,w3,1
	strb	w3,[x1,x2]
	mov	x0,1
.out:	ldr	x19,[sp],16
	epilogue

; les cartes qui ne servent plus montent seules
auto_play:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
.again:	mov	x21,0				; une de bougee ?
	mov	x19,0
.cell:	adr	x20,g_s+S_CELL
	ldrsb	x0,[x20,x19]
	tbnz	x0,63,.ncell
	bl	try_up
	cbz	x0,.ncell
	mov	w1,-1
	strb	w1,[x20,x19]
	mov	x21,1
.ncell:	add	x19,x19,1
	cmp	x19,4
	b.lo	.cell
	mov	x19,0
.col:	mov	x0,x19
	bl	col_ptr
	mov	x20,x0
	ldrb	w1,[x20]
	cbz	w1,.ncol
	ldrb	w0,[x20,x1]
	bl	try_up
	cbz	x0,.ncol
	ldrb	w1,[x20]
	sub	w1,w1,1
	strb	w1,[x20]
	mov	x21,1
.ncol:	add	x19,x19,1
	cmp	x19,NCOL
	b.lo	.col
	cbnz	x21,.again
	bl	check_win
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

check_win:
	prologue
	adr	x0,g_s+S_FOUND
	mov	x1,0
.f:	ldrsb	w2,[x0,x1]
	cmp	w2,12
	b.ne	.out
	add	x1,x1,1
	cmp	x1,4
	b.lo	.f
	ldv	x0,g_won
	cbnz	x0,.out
	mov	x0,1
	stv	x0,g_won
	stp	x19,x20,[sp,-16]!
	mov	x19,0
.x:	add	x0,x19,4
	bl	ck_col_x
	adr	x1,wa_fx
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,4
	b.lo	.x
	ldp	x19,x20,[sp],16
	ldv	x0,l_top
	bl	win_start
	sys	SYS_TIME_MS
	stv	x0,g_anim_t
.out:	epilogue

; un coup joue
after:
	prologue
	adr	x0,g_s
	ldr	w1,[x0,S_MOVES]
	add	w1,w1,1
	str	w1,[x0,S_MOVES]
	stv	xzr,g_msg
	bl	auto_play
	epilogue

; x0 = source, x1 = indice -> x0 = carte
src_card:
	cmp	x0,SRC_CELL
	b.hs	.cell
	mov	x2,COL_SIZE
	adr	x3,g_s
	madd	x0,x0,x2,x3
	add	x0,x0,1
	ldrb	w0,[x0,x1]
	ret
.cell:	adr	x3,g_s+S_CELL-SRC_CELL
	ldrb	w0,[x3,x0]
	ret

; x0 = source, x1 = indice -> x0 = cartes emportees
src_count:
	cmp	x0,SRC_CELL
	b.hs	.one
	mov	x2,COL_SIZE
	adr	x3,g_s
	madd	x0,x0,x2,x3
	ldrb	w0,[x0]
	sub	x0,x0,x1
	ret
.one:	mov	x0,1
	ret

; x0 = source, x1 = indice : les retire
take:
	cmp	x0,SRC_CELL
	b.hs	.cell
	mov	x2,COL_SIZE
	adr	x3,g_s
	madd	x0,x0,x2,x3
	strb	w1,[x0]
	ret
.cell:	adr	x3,g_s+S_CELL-SRC_CELL
	mov	w1,-1
	strb	w1,[x3,x0]
	ret

; x0 = source, x1 = indice -> x0 = 1 si pose sur sa fondation
drop_found:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	src_count
	cmp	x0,1
	b.ne	.no
	mov	x0,x19
	mov	x1,x20
	bl	src_card
	mov	x21,x0
	bl	can_found
	cbz	x0,.no
	bl	push_undo
	mov	x0,x19
	mov	x1,x20
	bl	take
	mov	x1,13
	udiv	x2,x21,x1
	adr	x1,g_s+S_FOUND
	ldrb	w3,[x1,x2]
	add	w3,w3,1
	strb	w3,[x1,x2]
	bl	after
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = source, x1 = indice, x2 = case -> x0 = 1 si posee
drop_cell:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	src_count
	cmp	x0,1
	b.ne	.no
	adr	x0,g_s+S_CELL
	ldrsb	w0,[x0,x21]
	tbz	w0,31,.no
	mov	x0,x19
	mov	x1,x20
	bl	src_card
	mov	x22,x0
	bl	push_undo
	mov	x0,x19
	mov	x1,x20
	bl	take
	adr	x0,g_s+S_CELL
	strb	w22,[x0,x21]
	bl	after
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = source, x1 = indice, x2 = colonne -> x0 = 1 si posee(s)
drop_col:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x19,x21
	b.eq	.no
	bl	src_count
	mov	x22,x0				; n
	cmp	x19,SRC_CELL			; sur une colonne vide, la plus longue
	b.hs	.st				; fin de suite qui passe
	mov	x0,x21
	bl	col_ptr
	ldrb	w0,[x0]
	cbnz	w0,.st
	mov	x0,x21
	bl	max_run
	cmp	x22,x0
	b.ls	.st
	mov	x22,x0
	mov	x0,x19
	bl	col_ptr
	ldrb	w0,[x0]
	sub	x20,x0,x22
.st:	mov	x0,x19
	mov	x1,x20
	bl	src_card
	mov	x1,x21
	bl	can_stack
	cbz	x0,.no
	mov	x0,x21
	bl	max_run
	cmp	x22,x0
	b.ls	.go
	sys	SYS_TIME_MS			; (0 serait « pas de message »)
	orr	x0,x0,1
	stv	x0,g_msg
	bl	paint
	b	.no
.go:	bl	push_undo
	mov	x23,0
.c:	cmp	x23,x22
	b.hs	.done
	mov	x0,x19
	add	x1,x20,x23
	bl	src_card
	mov	x24,x0
	mov	x0,x21
	bl	col_ptr
	ldrb	w1,[x0]
	add	w1,w1,1
	strb	w1,[x0]
	strb	w24,[x0,x1]
	add	x23,x23,1
	b	.c
.done:	mov	x0,x19
	mov	x1,x20
	bl	take
	bl	after
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = source, x1 = indice : le toucher sans glisser, vers la destination
; evidente : fondation, colonne garnie, case libre, colonne vide
click_send:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	src_card
	mov	x22,x0
	mov	x0,x19
	mov	x1,x20
	bl	drop_found
	cbnz	x0,.out
	mov	x21,0
.c:	mov	x0,x21
	bl	col_ptr
	ldrb	w0,[x0]
	cbz	w0,.cn
	cmp	x21,x19
	b.eq	.cn
	mov	x0,x22
	mov	x1,x21
	bl	can_stack
	cbz	x0,.cn
	mov	x0,x19
	mov	x1,x20
	bl	src_count
	mov	x2,x0
	stp	x2,xzr,[sp,-16]!
	mov	x0,x21
	bl	max_run
	ldp	x2,xzr,[sp],16
	cmp	x2,x0
	b.hi	.cn
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	drop_col
	b	.out
.cn:	add	x21,x21,1
	cmp	x21,NCOL
	b.lo	.c
	cmp	x19,SRC_CELL
	b.hs	.ec				; (d'une case : pas vers une autre case)
	mov	x21,0
.f:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	drop_cell
	cbnz	x0,.out
	add	x21,x21,1
	cmp	x21,4
	b.lo	.f
.ec:	cmp	x19,SRC_CELL			; une colonne vide, si ca change quelque chose
	b.hs	.e0
	cbz	x20,.out			; (la colonne entiere : deja seule)
.e0:	mov	x21,0
.e:	mov	x0,x21
	bl	col_ptr
	ldrb	w0,[x0]
	cbnz	w0,.en
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	drop_col
	b	.out
.en:	add	x21,x21,1
	cmp	x21,NCOL
	b.lo	.e
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = point -> x0 = 1 trouve, x1 = source, x2 = indice
hit_src:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldv	x23,c_w
	ldv	x24,c_h
	ldv	x0,l_top			; les cases
	cmp	x20,x0
	b.lt	.cols
	add	x0,x0,x24
	cmp	x20,x0
	b.ge	.cols
	mov	x21,0
.cc:	mov	x0,x21
	bl	ck_col_x
	cmp	x19,x0
	b.lt	.cn
	add	x0,x0,x23
	cmp	x19,x0
	b.ge	.cn
	adr	x0,g_s+S_CELL
	ldrsb	w0,[x0,x21]
	tbnz	w0,31,.none
	mov	x0,1
	add	x1,x21,SRC_CELL
	mov	x2,0
	b	.out
.cn:	add	x21,x21,1
	cmp	x21,4
	b.lo	.cc
.none:	mov	x0,0
	b	.out
.cols:	mov	x21,0				; colonne
.k:	mov	x0,x21
	bl	ck_col_x
	cmp	x19,x0
	b.lt	.kn
	add	x0,x0,x23
	cmp	x19,x0
	b.ge	.kn
	mov	x0,x21
	bl	col_dy
	mov	x22,x0
	mov	x0,x21
	bl	col_ptr
	ldrb	w2,[x0]
	cbz	w2,.none
	sub	x2,x2,1				; du sommet vers le bas
.ki:	mul	x3,x2,x22
	ldv	x4,l_tab
	add	x3,x3,x4
	cmp	x20,x3
	b.lt	.kd
	add	x3,x3,x24
	cmp	x20,x3
	b.ge	.none
	mov	x0,1
	mov	x1,x21
	b	.out
.kd:	cbz	x2,.none
	sub	x2,x2,1
	b	.ki
.kn:	add	x21,x21,1
	cmp	x21,NCOL
	b.lo	.k
	b	.none
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
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
.press:	tbz	w4,0,.out
	mov	x0,x2
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
	bl	random_game
	bl	fc_new
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
.t:	sys	SYS_TIME_MS			; second appui d'un double-clic : deja parti
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
.h:	mov	x0,x19
	mov	x1,x20
	bl	hit_src
	cbz	x0,.out
	mov	x21,x1
	mov	x22,x2
	cmp	x21,SRC_CELL
	b.hs	.ok
	mov	x0,x21
	mov	x1,x22
	bl	is_run
	cbz	x0,.out
	mov	x0,x21
	bl	col_dy
	mul	x1,x22,x0
	ldv	x0,l_tab
	add	x1,x1,x0
	mov	x0,x21
	b	.pos
.ok:	sub	x0,x21,SRC_CELL
	ldv	x1,l_top
.pos:	stp	x1,xzr,[sp,-16]!
	bl	ck_col_x
	ldp	x1,xzr,[sp],16
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
	mov	x19,x0
	mov	x20,x1
	ldv	x0,g_off_x
	sub	x19,x19,x0			; coin du paquet
	ldv	x0,g_off_y
	sub	x20,x20,x0
	ldv	x0,dr_on
	cbnz	x0,.drag
	ldv	x0,g_px				; encore un simple appui ?
	ldv	x1,g_off_x
	sub	x0,x0,x1
	sub	x0,x19,x0
	cmp	x0,0
	cneg	x0,x0,lt
	ldv	x1,g_py
	ldv	x2,g_off_y
	sub	x1,x1,x2
	sub	x1,x20,x1
	cmp	x1,0
	cneg	x1,x1,lt
	add	x0,x0,x1
	cmp	x0,DRAG_MIN
	b.lt	.out
	ldv	x21,g_src			; le paquet quitte sa place
	ldv	x22,g_idx
	mov	x0,x21
	mov	x1,x22
	bl	src_count
	stv	x0,dr_n
	stp	x23,x24,[sp,-16]!
	mov	x23,x0
	mov	x24,0
.c:	cmp	x24,x23
	b.hs	.d
	mov	x0,x21
	add	x1,x22,x24
	bl	src_card
	adr	x1,dr_cards
	strb	w0,[x1,x24]
	add	x24,x24,1
	b	.c
.d:	ldp	x23,x24,[sp],16
	ldv	x0,c_dy_max
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
	mov	x0,x19
	mov	x1,NCOL
	bl	ck_col_at
	mov	x19,x0
	ldv	x1,l_top
	ldv	x2,c_h
	add	x1,x1,x2
	add	x1,x1,10
	cmp	x20,x1
	b.ge	.col
	cmp	x19,4				; rangee du haut
	b.lo	.cell
	mov	x0,x21
	mov	x1,x22
	bl	drop_found
	b	.paint
.cell:	mov	x0,x21
	mov	x1,x22
	mov	x2,x19
	bl	drop_cell
	b	.paint
.col:	mov	x0,x21
	mov	x1,x22
	mov	x2,x19
	bl	drop_col
.paint:	bl	paint
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; g_btns <- les libelles du bandeau (s_btns), traduits. Le deuxieme, Annuler, a pour cle

; celle du menu : seul, il dirait Cancel.

; x0 = destination, x1 = morceau de texte -> x0 = fin : str_cat traduit. Les espaces autour du

; morceau restent ici (un dictionnaire ne garde pas ceux d'une cle).

tr_cat:

	prologue

	stp	x19,x20,[sp,-16]!

	str	x21,[sp,-16]!

	sub	sp,sp,240

	mov	x19,x0

	mov	x20,x1

.e:	ldrb	w0,[x20]			; espaces de tete

	cmp	w0,' '

	b.ne	.d

	strb	w0,[x19],1

	add	x20,x20,1

	b	.e

.d:	mov	x21,0				; longueur sans ceux de queue

	mov	x2,0

.c:	ldrb	w0,[x20,x2]

	strb	w0,[sp,x2]

	cbz	w0,.f

	add	x2,x2,1

	cmp	w0,' '

	csel	x21,x21,x2,eq

	cmp	x2,239

	b.lo	.c

.f:	strb	wzr,[sp,x21]

	add	x20,x20,x21

	mov	x0,sp

	bl	ui_tr

	mov	x1,x0

	mov	x0,x19

	bl	str_cat

.q:	ldrb	w1,[x20],1

	strb	w1,[x0],1

	cbnz	w1,.q

	sub	x0,x0,1

	add	sp,sp,240

	ldr	x21,[sp],16

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



; x1 = texte -> sa traduction ; les autres registres gardes

tr1:

	prologue

	str	x0,[sp,-16]!

	mov	x0,x1

	bl	tr0

	mov	x1,x0

	ldr	x0,[sp],16

	epilogue

band_text:

	prologue

	stp	x19,x20,[sp,-16]!

	adr	x19,s_btns

	adr	x20,g_btns

.l:	ldrb	w0,[x19]

	cbz	w0,.f

	mov	x0,x19

	adr	x1,s_btns+9

	cmp	x19,x1

	b.ne	.t

	adr	x0,s_undo_k

	bl	ui_tr

	adr	x1,s_undo_k

	cmp	x0,x1

	csel	x0,x19,x0,eq

	b	.c

.t:	bl	ui_tr

.c:	mov	x1,x0

	mov	x0,x20

	bl	str_cat

	add	x20,x0,1

.s:	ldrb	w0,[x19],1

	cbnz	w0,.s

	b	.l

.f:	strb	wzr,[x20]

	ldp	x19,x20,[sp],16

	epilogue

; -> x0 = texte du bandeau, w1 = couleur
status_text:
	prologue
	ldv	x0,g_msg
	cbz	x0,.st
	adr	x0,s_nomove
	bl	tr0
	movl	w1,TAPIS_NOTE
	b	.out
.st:	adr	x0,g_txt
	adr	x1,s_game
	bl	tr_cat
	mov	x1,x0
	ldv	x0,g_game
	bl	fmt_dec
	adr	x1,s_moves
	bl	tr_cat
	mov	x1,x0
	adr	x0,g_s
	ldr	w0,[x0,S_MOVES]
	bl	fmt_dec
	adr	x0,g_txt
	movl	w1,TAPIS_TEXT
.out:	epilogue

; la table, puis tout est montre
paint:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	lay_begin
	bl	status_text
	bl	table_bg
	mov	x19,0				; rangee du haut
.cell:	mov	x0,x19
	bl	ck_col_x
	mov	x20,x0
	ldv	x1,l_top
	mov	x2,-1
	bl	card_slot
	adr	x0,g_s+S_CELL
	ldrsb	x0,[x0,x19]
	tbnz	x0,63,.ncell
	ldv	x1,dr_on			; (glissee ?)
	cbz	x1,.cd
	ldv	x1,g_src
	add	x2,x19,SRC_CELL
	cmp	x1,x2
	b.eq	.ncell
.cd:	mov	x1,x20
	ldv	x2,l_top
	bl	card_blit
.ncell:	add	x19,x19,1
	cmp	x19,4
	b.lo	.cell
	mov	x19,0
.found:	add	x0,x19,4
	bl	ck_col_x
	mov	x20,x0
	adr	x0,g_s+S_FOUND
	ldrsb	x1,[x0,x19]
	tbz	x1,63,.fc
	mov	x0,x20
	ldv	x1,l_top
	mov	x2,x19
	bl	card_slot
	b	.nf
.fc:	mov	x0,13
	madd	x0,x19,x0,x1
	mov	x1,x20
	ldv	x2,l_top
	bl	card_blit
.nf:	add	x19,x19,1
	cmp	x19,4
	b.lo	.found
	mov	x19,0				; les colonnes
.col:	mov	x0,x19
	bl	ck_col_x
	mov	x20,x0
	mov	x0,x19
	bl	col_dy
	mov	x21,x0
	mov	x0,x19
	bl	col_ptr
	mov	x22,x0
	ldrb	w23,[x22]
	ldv	x24,dr_on			; x24 = premiere carte cachee
	cbz	x24,.all
	ldv	x0,g_src
	cmp	x0,x19
	b.ne	.all
	ldv	x24,g_idx
	cmp	x24,x23
	csel	x23,x24,x23,lo
.all:	cbnz	x23,.c0
	mov	x0,x20
	ldv	x1,l_tab
	mov	x2,-1
	bl	card_slot
	b	.ncol
.c0:	mov	x24,0
.c:	add	x0,x22,1
	ldrb	w0,[x0,x24]
	mul	x2,x24,x21
	ldv	x1,l_tab
	add	x2,x2,x1
	mov	x1,x20
	bl	card_blit
	add	x24,x24,1
	cmp	x24,x23
	b.lo	.c
.ncol:	add	x19,x19,1
	cmp	x19,NCOL
	b.lo	.col
	ldv	x0,g_won
	cbz	x0,.show
	ldv	x0,win_on
	cbnz	x0,.show
	adr	x0,s_won
	bl	tr0
	adr	x1,s_again
	bl	tr1
	bl	win_box
.show:	bl	lay_end
	bl	compose_all
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

	align 4
s_title		db 'FreeCell',0
s_game		db 'Partie n',0xC2,0xB0,' ',0
s_moves		db '    Coups : ',0
s_nomove	db 'Pas assez de cases libres pour autant de cartes',0
s_won		db 'Gagn',0xC3,0xA9,' !',0
s_again		db 'Toucher pour une nouvelle partie',0
s_sel_t		db 'Choisir une partie',0
s_sel_q		db 'Num',0xC3,0xA9,'ro de partie (1 ',0xC3,0xA0,' 32000) :',0
s_btns		db 'Nouvelle',0,'Annuler',0,'Partie',0xE2,0x80,0xA6,0,0
s_undo_k	db 'Annuler le coup',0		; (" Annuler " seul : Cancel dans les dictionnaires)
s_btn_ids	db ID_NEW,ID_UNDO,ID_SELECT
s_menus		db 'MPartie',10
		db 'I1',9,'Nouvelle partie',9,'Ctrl+N',10
		db 'I2',9,'Choisir une partie',0xE2,0x80,0xA6,10
		db 'I3',9,'Recommencer',10
		db 'I4',9,'Annuler le coup',9,'Ctrl+Z',10
		db '-',10
		db 'I5',9,'Quitter',10,0
		align 4

segment readable writeable
kit_data
cards_data
g_game		dd 0
g_nundo		dd 0
g_won		dd 0
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
g_msg		dd 0				; instant du message (0 : aucun)
g_anim_t	dd 0
g_s		rb FS_SIZE
g_undo		rb FS_SIZE*UNDO_MAX
g_txt		rb 96
g_btns		rb 80
g_ask		rb 32
		align 8
ui_data
