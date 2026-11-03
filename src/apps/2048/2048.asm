; ============================================================================
;  2048 - le jeu d'Onyx (user/Apps/2048), en assembleur : on fait glisser les
;  tuiles, deux tuiles egales fusionnent. Fleches ou balayage (ecran tactile,
;  souris) ; R ou Partie > Nouvelle partie recommence. La fenetre est
;  redimensionnable, le plateau suit.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

N		= 4
WIN_W		= 400
WIN_H		= 452
HEAD		= 64				; bandeau du score
PAD		= 12
GAP		= 8
SWIPE		= 24				; balayage : au moins ce trajet
ID_NEW		= 1
ID_QUIT		= 2
KEY_R		= 0x15
KEY_RIGHT	= 0x4F
KEY_LEFT	= 0x50
KEY_DOWN	= 0x51
KEY_UP		= 0x52

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'

macro ldv reg, var {
	adr	x16,var
	ldr	reg,[x16]
}
macro stv reg, var {
	adr	x16,var
	str	reg,[x16]
}

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	mov	x0,-1
	mov	x1,-1
	mov	x2,WIN_W
	mov	x3,WIN_H
	adr	x4,s_title
	mov	x5,WF_RESIZABLE
	bl	ui_window_ex
	tbnz	x0,63,quit
	adr	x0,s_menus
	bl	ui_menu
	bl	ui_jetons
	stv	x0,g_tok
	mov	x0,300
	mov	x1,380
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	sys	SYS_TIME_MS
	orr	w0,w0,1
	adr	x1,g_rng
	str	w0,[x1]
	bl	restart
	mov	x0,WIN_W
	mov	x1,WIN_H
	bl	on_resize
.loop:	mov	x0,1000
	bl	ui_event
	cbz	x0,.loop
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MENU
	b.eq	.menu
	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	mov	x0,0
	bl	ui_w_event
	b	.loop
.menu:	adr	x0,ui_ev
	ldr	w1,[x0,EV_CODE]
	cmp	w1,ID_QUIT
	b.eq	quit
	cmp	w1,ID_NEW
	b.ne	.loop
.new:	bl	restart
	bl	paint
	b	.loop
.key:	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.loop
	ldr	w1,[x0,EV_CODE]
	cmp	w1,KEY_R
	b.eq	.new
	mov	x0,0
	cmp	w1,KEY_LEFT
	b.eq	.play
	mov	x0,1
	cmp	w1,KEY_RIGHT
	b.eq	.play
	mov	x0,2
	cmp	w1,KEY_UP
	b.eq	.play
	mov	x0,3
	cmp	w1,KEY_DOWN
	b.ne	.loop
.play:	bl	play
	b	.loop
.mouse:	bl	on_mouse
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; -> w0 = pseudo-aleatoire sur 15 bits (le generateur d'Onyx)
rnd:
	adr	x1,g_rng
	ldr	w0,[x1]
	movl	w2,1103515245
	mul	w0,w0,w2
	mov	w2,12345
	add	w0,w0,w2
	str	w0,[x1]
	lsr	w0,w0,16
	ret

; une tuile 2 (9 fois sur 10) ou 4 dans une case vide au hasard
add_tile:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,g_grid
	mov	x20,0				; cases vides
	mov	x1,0
.c:	ldr	w2,[x19,x1,lsl 2]
	cbnz	w2,.c1
	add	x20,x20,1
.c1:	add	x1,x1,1
	cmp	x1,N*N
	b.lo	.c
	cbz	x20,.out
	bl	rnd
	udiv	w1,w0,w20
	msub	w20,w1,w20,w0			; rang de la case vide
	bl	rnd
	mov	w1,10
	udiv	w2,w0,w1
	msub	w0,w2,w1,w0
	cmp	w0,0
	mov	w2,2
	mov	w3,4
	csel	w2,w3,w2,eq
	mov	x1,0
.f:	ldr	w3,[x19,x1,lsl 2]
	cbnz	w3,.f1
	cbz	x20,.put
	sub	x20,x20,1
.f1:	add	x1,x1,1
	b	.f
.put:	str	w2,[x19,x1,lsl 2]
.out:	ldp	x19,x20,[sp],16
	epilogue

restart:
	prologue
	adr	x0,g_grid
	mov	x1,0
.z:	str	wzr,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,N*N
	b.lo	.z
	adr	x0,g_score
	str	xzr,[x0]
	adr	x0,g_over
	str	xzr,[x0]
	bl	add_tile
	bl	add_tile
	epilogue

; fait glisser une ligne de 4 cases vers sa premiere case (avec fusion)
; x0 = indice de la premiere case, x1 = pas -> x0 = 1 si elle a change
slide:
	sub	sp,sp,16
	adr	x2,g_grid
	mov	x3,0				; cases gardees (tmp)
	mov	x4,0
	mov	x5,x0
.g:	ldr	w6,[x2,x5,lsl 2]
	cbz	w6,.g1
	str	w6,[sp,x3,lsl 2]
	add	x3,x3,1
.g1:	add	x5,x5,x1
	add	x4,x4,1
	cmp	x4,N
	b.lo	.g
.z:	cmp	x3,N
	b.hs	.m
	str	wzr,[sp,x3,lsl 2]
	add	x3,x3,1
	b	.z
.m:	mov	x3,0
.mi:	ldr	w4,[sp,x3,lsl 2]		; tmp[i] == tmp[i + 1] ?
	add	x5,x3,1
	ldr	w6,[sp,x5,lsl 2]
	cbz	w4,.mn
	cmp	w4,w6
	b.ne	.mn
	lsl	w4,w4,1
	str	w4,[sp,x3,lsl 2]
	adr	x6,g_score
	ldr	x7,[x6]
	add	x7,x7,x4
	str	x7,[x6]
.sh:	cmp	x5,N-1				; le reste recule d'une case
	b.hs	.sz
	add	x6,x5,1
	ldr	w7,[sp,x6,lsl 2]
	str	w7,[sp,x5,lsl 2]
	mov	x5,x6
	b	.sh
.sz:	mov	x5,N-1
	str	wzr,[sp,x5,lsl 2]
.mn:	add	x3,x3,1
	cmp	x3,N-1
	b.lo	.mi
	mov	x3,0				; recopie
	mov	x4,0				; change ?
	mov	x5,x0
.w:	ldr	w6,[sp,x3,lsl 2]
	ldr	w7,[x2,x5,lsl 2]
	cmp	w6,w7
	b.eq	.w1
	mov	x4,1
	str	w6,[x2,x5,lsl 2]
.w1:	add	x5,x5,x1
	add	x3,x3,1
	cmp	x3,N
	b.lo	.w
	mov	x0,x4
	add	sp,sp,16
	ret

; x0 = 0 gauche, 1 droite, 2 haut, 3 bas : joue le coup et redessine
play:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x1,g_over
	cbnz	x1,.out
	mov	x19,x0
	mov	x20,0				; ligne
	mov	x21,0				; change ?
.k:	cmp	x19,0
	b.ne	.d1
	lsl	x0,x20,2			; gauche : k * 4, pas 1
	mov	x1,1
	b	.s
.d1:	cmp	x19,1
	b.ne	.d2
	lsl	x0,x20,2			; droite : k * 4 + 3, pas -1
	add	x0,x0,N-1
	mov	x1,-1
	b	.s
.d2:	cmp	x19,2
	b.ne	.d3
	mov	x0,x20				; haut : k, pas 4
	mov	x1,N
	b	.s
.d3:	add	x0,x20,N*(N-1)			; bas : 12 + k, pas -4
	mov	x1,-N
.s:	bl	slide
	orr	x21,x21,x0
	add	x20,x20,1
	cmp	x20,N
	b.lo	.k
	cbz	x21,.out
	bl	add_tile
	bl	check_over
	bl	paint
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; g_over = 1 si plus aucun coup : ni case vide, ni voisines egales
check_over:
	adr	x0,g_grid
	mov	x1,0				; ligne
.r:	mov	x2,0				; colonne
.c:	add	x3,x2,x1,lsl 2
	ldr	w4,[x0,x3,lsl 2]
	cbz	w4,.free
	cmp	x2,N-1
	b.hs	.b
	add	x5,x3,1
	ldr	w5,[x0,x5,lsl 2]
	cmp	w4,w5
	b.eq	.free
.b:	cmp	x1,N-1
	b.hs	.n
	add	x5,x3,N
	ldr	w5,[x0,x5,lsl 2]
	cmp	w4,w5
	b.eq	.free
.n:	add	x2,x2,1
	cmp	x2,N
	b.lo	.c
	add	x1,x1,1
	cmp	x1,N
	b.lo	.r
	mov	x0,1
	stv	x0,g_over
	ret
.free:	stv	xzr,g_over
	ret

; la souris : un appui dans le bandeau deplace la fenetre ; un balayage joue
on_mouse:
	prologue
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	ldrsw	x2,[x0,EV_X]
	ldrsw	x3,[x0,EV_Y]
	cmp	w1,1
	b.ne	.rel
	ldr	w4,[x0,EV_CODE]
	tbz	w4,0,.out
	cmp	x3,HEAD
	b.ge	.down
	ldv	x4,g_win_w
	sub	x4,x4,FRAME_CAP_W
	cmp	x2,x4
	b.ge	.out
	bl	ui_window_drag
	b	.out
.down:	adr	x4,g_press
	stp	x2,x3,[x4]
	mov	x5,1
	str	x5,[x4,16]
	b	.out
.rel:	cmp	w1,2
	b.ne	.out
	adr	x4,g_press
	ldr	x5,[x4,16]
	cbz	x5,.out
	str	xzr,[x4,16]
	ldp	x5,x6,[x4]
	sub	x2,x2,x5			; trajet
	sub	x3,x3,x6
	cmp	x2,0
	cneg	x4,x2,lt
	cmp	x3,0
	cneg	x5,x3,lt
	cmp	x4,x5
	b.lt	.v
	cmp	x4,SWIPE
	b.lt	.tap
	mov	x0,0
	cmp	x2,0
	cinc	x0,x0,gt
	bl	play
	b	.out
.v:	cmp	x5,SWIPE
	b.lt	.tap
	mov	x0,2
	cmp	x3,0
	cinc	x0,x0,gt
	bl	play
	b	.out
.tap:	ldv	x0,g_over			; partie finie : un toucher recommence
	cbz	x0,.out
	bl	restart
	bl	paint
.out:	epilogue

; x0 = largeur, x1 = hauteur : le plateau, centre, le plus grand possible
on_resize:
	prologue
	adr	x2,g_win_w
	stp	x0,x1,[x2]
	sub	x2,x0,PAD*2
	sub	x3,x1,HEAD+PAD
	cmp	x2,x3
	csel	x2,x3,x2,gt			; cote du plateau
	sub	x3,x2,GAP*(N+1)
	mov	x4,N
	udiv	x3,x3,x4			; cote d'une tuile
	stv	x3,g_cell
	mul	x4,x3,x4
	add	x4,x4,GAP*(N+1)
	stv	x4,g_side
	sub	x2,x0,x4
	lsr	x2,x2,1
	stv	x2,g_ox
	bl	paint
	epilogue

; x0 = valeur d'une tuile -> w0 = fond, w1 = encre
tile_color:
	cmp	w0,2048
	b.ls	.t
	mov	w0,2048				; (au-dela : la couleur de 2048)
.t:	rbit	w2,w0
	clz	w2,w2				; log2
	sub	w2,w2,1
	adr	x3,tile_rgb
	ldr	w0,[x3,x2,lsl 2]
	movl	w1,0x00F9F6F2
	cmp	w2,2
	b.hs	.out
	movl	w1,0x00776E65
.out:	ret

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

; ecrit le score, a droite du puits, x0 = score
draw_score:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	mov	x1,sp
	bl	fmt_dec
	strb	wzr,[x0]
	mov	x0,sp
	mov	x1,22
	adr	x2,ui_font_b
	bl	ui_text_width
	ldv	x1,g_win_w
	sub	x1,x1,FRAME_CAP_W+PAD*2
	sub	x1,x1,x0
	mov	x0,sp
	mov	x2,40
	ldv	x3,g_tok
	ldr	w3,[x3,TJ_TEXTE1]
	mov	x4,22
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur, x1 = x, x2 = y (coin de la tuile)
draw_tile:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	tile_color
	mov	w22,w1				; encre
	mov	w5,w0
	mov	w6,w0
	mov	x0,x20
	mov	x1,x21
	ldv	x2,g_cell
	mov	x3,x2
	mov	x4,6
	bl	ui_rbox
	mov	x0,x19
	mov	x1,sp
	bl	fmt_dec
	strb	wzr,[x0]
	mov	x1,sp
	sub	x1,x0,x1			; chiffres
	ldv	x3,g_cell			; le corps diminue avec les chiffres
	mov	x4,5
	cmp	x1,3
	b.lo	.px
	mov	x4,3
	mul	x3,x3,x4
	mov	x4,10
	cmp	x1,4
	b.lo	.px
	mov	x4,12
	cmp	x1,5
	b.lo	.px
	mov	x4,15
.px:	cmp	x4,5
	b.ne	.px1
	lsl	x3,x3,1
.px1:	udiv	x19,x3,x4			; corps
	mov	x0,sp
	mov	x1,x19
	adr	x2,ui_font_b
	bl	ui_text_width
	ldv	x1,g_cell
	sub	x1,x1,x0
	asr	x1,x1,1
	add	x1,x1,x20
	ldv	x2,g_cell
	add	x2,x2,x19,lsr 1			; ligne de base
	sub	x2,x2,x19,lsr 3
	asr	x2,x2,1
	add	x2,x2,x19,lsr 2
	add	x2,x2,x21
	mov	x0,sp
	mov	w3,w22
	mov	x4,x19
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	add	sp,sp,16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; toute la fenetre
paint:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x0,0				; le fond
	mov	x1,0
	adr	x4,g_win_w
	ldp	x2,x3,[x4]
	ldv	x4,g_tok
	ldr	w4,[x4,TJ_FACE]
	bl	ui_fill
	mov	x0,PAD				; le puits du score
	mov	x1,12
	ldv	x2,g_win_w
	sub	x2,x2,PAD*2+FRAME_CAP_W
	mov	x3,40
	mov	x4,8
	ldv	x5,g_tok
	ldr	w6,[x5,TJ_CREUX]
	mov	w5,w6
	bl	ui_rbox
	adr	x0,s_score
	bl	tr0
	mov	x1,PAD+12
	mov	x2,38
	ldv	x3,g_tok
	ldr	w3,[x3,TJ_TEXTE2]
	mov	x4,15
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	ldv	x0,g_score
	bl	draw_score
	ldv	x0,g_ox				; le plateau (ses couleurs a lui)
	mov	x1,HEAD
	ldv	x2,g_side
	mov	x3,x2
	mov	x4,10
	movl	w5,0x00BBADA0
	mov	w6,w5
	bl	ui_rbox
	mov	x19,0				; case
	adr	x20,g_grid
.cell:	and	x1,x19,N-1
	lsr	x2,x19,2
	ldv	x3,g_cell
	add	x3,x3,GAP
	mul	x21,x1,x3
	mul	x22,x2,x3
	ldv	x3,g_ox
	add	x21,x21,x3
	add	x21,x21,GAP
	add	x22,x22,HEAD+GAP
	ldr	w0,[x20,x19,lsl 2]
	cbz	w0,.empty
	mov	x1,x21
	mov	x2,x22
	bl	draw_tile
	b	.next
.empty:	mov	x0,x21
	mov	x1,x22
	ldv	x2,g_cell
	mov	x3,x2
	mov	x4,6
	movl	w5,0x00CDC1B4
	mov	w6,w5
	bl	ui_rbox
.next:	add	x19,x19,1
	cmp	x19,N*N
	b.lo	.cell
	ldv	x0,g_over
	cbz	x0,.show
	bl	draw_over
.show:	bl	ui_update_all
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; le message de fin, sur le plateau
draw_over:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x0,g_ox
	ldv	x1,g_side
	mov	x2,240
	cmp	x1,x2
	csel	x2,x1,x2,lo
	sub	x1,x1,x2
	add	x19,x0,x1,lsr 1			; x
	ldv	x1,g_side
	sub	x1,x1,100
	add	x20,x1,HEAD*2
	asr	x20,x20,1			; y
	mov	x0,x19
	mov	x1,x20
	mov	x3,100
	mov	x4,12
	ldv	x5,g_tok
	ldr	w6,[x5,TJ_SURFACE]
	mov	w5,w6
	bl	ui_rbox
	adr	x0,s_over
	bl	tr0
	mov	x1,22
	adr	x2,ui_font_b
	bl	ui_text_width
	bl	.center
	adr	x0,s_over
	bl	tr0
	add	x2,x20,44
	ldv	x3,g_tok
	ldr	w3,[x3,TJ_TEXTE1]
	mov	x4,22
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	adr	x0,s_again
	bl	tr0
	mov	x1,15
	adr	x2,ui_font
	bl	ui_text_width
	bl	.center
	adr	x0,s_again
	bl	tr0
	add	x2,x20,76
	ldv	x3,g_tok
	ldr	w3,[x3,TJ_TEXTE2]
	mov	x4,15
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	ldp	x19,x20,[sp],16
	epilogue
.center:				; x0 = largeur du texte -> x1 = x centre
	ldv	x2,g_win_w
	sub	x1,x2,x0
	asr	x1,x1,1
	ret

segment readable writeable
kit_data
		align 8
g_tok		dd 0
g_win_w		dd WIN_W			; taille (contigus)
g_win_h		dd WIN_H
g_cell		dd 0
g_side		dd 0
g_ox		dd 0
g_score		dd 0
g_over		dd 0
g_press		rd 3				; appui : x, y, en cours
g_rng		dw 1
		align 4
g_grid		rw N*N
tile_rgb	dw 0x00EEE4DA,0x00EDE0C8,0x00F2B179,0x00F59563,0x00F67C5F,0x00F65E3B
		dw 0x00EDCF72,0x00EDCC61,0x00EDC850,0x00EDC53F,0x00EDC22E
s_title		db '2048',0
s_score		db 'Score',0
s_over		db 'Partie termin',0xC3,0xA9,'e',0
s_again		db 'Toucher ou R : nouvelle partie',0
s_menus		db 'MPartie',10
		db 'I1',9,'Nouvelle partie',9,'R',10
		db '-',10
		db 'I2',9,'Quitter',10,0
		align 8
ui_data
