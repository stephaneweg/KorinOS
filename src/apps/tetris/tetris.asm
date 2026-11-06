; ============================================================================
;  tetris - le Tetris d'Onyx (user/Apps/tetris), memes pieces, memes regles
;  (gravite 30 - lignes images, 5 au mieux ; 100 points par ligne ; rotation qui
;  se decale d'une case, deux pour la barre), habille : blocs en relief, ciel
;  genere qui change tous les 10 lignes, fantome, reserve, trois pieces a venir,
;  animation de ligne, particules, son.
;
;  Fleches ou boutons a l'ecran ; haut, clic dans le puits : tourner ; Espace :
;  chute ; C : reserve ; P : pause ; R : recommencer. Manette : croix, A tourne,
;  B chute, X reserve. Plein ecran ; Start, Echap, P ou le bouton Pause : le
;  menu de pause (gamepause.inc).
;
;  Couche fixe (gf_layer) : ciel, puits, panneaux, libelles ; refaite a chaque
;  niveau. Tout ce qui bouge est en sprites (gameart.inc). Taille logique 640x480.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'
include '../../inc/pad.inc'

GF_W		= 640
GF_H		= 480
COLS		= 10
ROWS		= 20
CELL		= 22
MINI		= 16
WX		= 210			; le puits
WY		= 20
WW		= COLS*CELL
WH		= ROWS*CELL
NRAIN		= 9			; pieces qui tombent sur l'ecran titre
NM		= 18			; poussieres
NTHEME		= 6
LOGO_X		= 40
LOGO_Y		= 72

ST_TITLE	= 0
ST_PLAY		= 1
ST_PAUSE	= 2
ST_CLEAR	= 3			; lignes en train de partir
ST_DYING	= 4			; le puits se fige
ST_OVER		= 5

AC_LEFT		= 1			; aussi les numeros des zones d'appui
AC_RIGHT	= 2
AC_DOWN		= 3
AC_ROT		= 4
AC_DROP		= 5
AC_HOLD		= 6
AC_PAUSE	= 7

ID_NEW		= 1
ID_PAUSE	= 2
ID_SOUND	= 3
ID_QUIT		= 4

KEY_ENTER	= 0x28
KEY_SPACE	= 0x2C
KEY_RIGHT	= 0x4F
KEY_LEFT	= 0x50
KEY_DOWN	= 0x51
KEY_UP		= 0x52

; textes pre-rendus
TX_PROMPT	= 0
TX_PAUSE	= 1
TX_RESUME	= 2
TX_OVER		= 3
TX_AGAIN	= 4
TX_TETRIS	= 5
TX_LEVEL	= 6
TX_RECORD	= 7
TX_BEST		= 8
TX_SUB		= 9
NTX		= 10

virtual at 0
g_state		rd 1
g_timer		rd 1
g_tick		rd 1
g_type		rd 1
g_rot		rd 1
g_px		rd 1
g_py		rd 1
g_hold		rd 1			; piece en reserve, -1 : aucune
g_hused		rd 1			; deja prise pour cette piece
g_score		rd 1
g_show		rd 1			; le score affiche, qui rattrape
g_lines		rd 1
g_level		rd 1
g_best		rd 1
g_newbest	rd 1
g_frames	rd 1
g_last		rd 1
g_nclr		rd 1
g_grey		rd 1			; lignes figees (depuis le bas)
g_shake		rd 1
g_pop		rd 1			; texte qui monte : reste, sprite
g_popspr	rd 1
g_rep		rd 1			; action qui se repete, son delai, sa touche
g_rept		rd 1			;  (0 : bouton d'ecran, -1 : manette)
g_repk		rd 1
g_rotk		rd 1
g_btn		rd 1			; bouton d'ecran tenu
g_pad		rd 1
g_ldirty	rd 1
g_sb		rd 8			; blocs : 7 couleurs, gris
g_sm		rd 8			; petits blocs
g_sg		rd 7			; fantomes
g_sglow		rd 1
g_sorb		rd 2
g_smote		rd 1
g_dbig		rd 10
g_dsmall	rd 10
g_tx		rd NTX
g_next		rd 3
g_pc		rb 8			; les quatre cases d'une piece : x, y
		align 8
g_off		rd ROWS			; decalage de chaque ligne quand elle tombe
g_clr		rb ROWS
g_field		rb ROWS*COLS
g_flash		rb ROWS*COLS
		align 8
g_rain		rb NRAIN*32		; +0 x, +8 y (16.16), +16 vitesse, +24 type, +28 rotation
g_mote		rb NM*16
G_SIZE		= $
end virtual

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'
kit_use '../../kits/audiokit/audiokit.abi'
include '../../inc/gamesnd.inc'
include '../../inc/gamefb.inc'
include '../../inc/gameart.inc'
include '../../inc/gamepause.inc'

start:
	bl	kits_open
	adr	x28,g_data
	adr	x0,s_title
	mov	x1,320
	mov	x2,240
	bl	gf_open
	tbnz	x0,63,quit
	adr	x0,s_menus
	bl	ui_menu
	movl	x0,1024*1024
	bl	ga_init
	cbz	x0,quit
	bl	gs_open
	adr	x0,p_best
	bl	gf_load_best
	str	x0,[x28,g_best]
	bl	gen_sprites
	bl	gen_motes
	bl	gen_rain
	adr	x0,game_pause
	adr	x1,game_resume
	adr	x2,new_game
	mov	x3,-1
	mov	x4,0
	adr	x5,on_pad
	bl	gp_init
	mov	x0,ST_TITLE
	bl	set_state
	mov	x0,1
	str	x0,[x28,g_ldirty]
	adr	x0,on_event
	adr	x1,step
	adr	x2,draw
	bl	gf_run
	bl	keep_best
	bl	gs_close
quit:	mov	x0,0
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Sprites
; ---------------------------------------------------------------------------
gen_sprites:
	prologue
	ga_push
	mov	x19,0
.b:	adr	x0,colors
	ldr	w20,[x0,x19,lsl 2]
	mov	x0,CELL-1
	mov	x1,CELL-1
	mov	x2,4
	mov	w3,w20
	bl	ga_block
	add	x1,x28,g_sb
	str	x0,[x1,x19,lsl 3]
	mov	x0,MINI-1
	mov	x1,MINI-1
	mov	x2,3
	mov	w3,w20
	bl	ga_block
	add	x1,x28,g_sm
	str	x0,[x1,x19,lsl 3]
	cmp	x19,7
	b.hs	.nb
	mov	x0,CELL-1			; le fantome : un bloc tres estompe
	mov	x1,CELL-1
	mov	x2,4
	mov	w3,w20
	bl	ga_block
	add	x1,x28,g_sg
	str	x0,[x1,x19,lsl 3]
	mov	x1,62
	bl	ga_fade
.nb:	add	x19,x19,1
	cmp	x19,8
	b.lo	.b
	mov	x0,CELL+6			; lueur d'un bloc qui se pose
	movl	w1,0x00707078
	bl	ga_disc
	str	x0,[x28,g_sglow]
	mov	x0,148
	movl	w1,0x00203048
	bl	ga_disc
	str	x0,[x28,g_sorb]
	mov	x0,120
	movl	w1,0x00301838
	bl	ga_disc
	str	x0,[x28,g_sorb+8]
	mov	x0,5
	movl	w1,0x00607088
	bl	ga_disc
	str	x0,[x28,g_smote]
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	add	x0,x28,g_dbig
	mov	x1,22
	adr	x2,ui_font_b
	movl	w3,0x00FFFFFF
	movl	w4,0x00A8D8FF
	bl	ga_digits
	add	x0,x28,g_dsmall
	mov	x1,15
	adr	x2,ui_font_b
	movl	w3,0x00E8F0FF
	movl	w4,0x00B8C8E0
	bl	ga_digits
	adr	x0,tx_table
	adr	x1,s_base
	add	x2,x28,g_tx
	mov	x3,NTX
	bl	ga_texts
	bl	ui_canvas_end
	ga_pop
	epilogue

gen_motes:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x28,g_mote
	mov	x20,NM
.m:	mov	x0,GF_W
	bl	gf_below
	lsl	x0,x0,16
	str	w0,[x19]
	mov	x0,GF_H
	bl	gf_below
	lsl	x0,x0,16
	str	w0,[x19,4]
	movl	x0,30000
	bl	gf_below
	movl	x1,9000
	add	x0,x0,x1
	str	w0,[x19,8]
	add	x19,x19,16
	subs	x20,x20,1
	b.ne	.m
	ldp	x19,x20,[sp],16
	epilogue

gen_rain:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x28,g_rain
	mov	x20,NRAIN
.r:	mov	x0,x19
	bl	rain_new
	mov	x0,GF_H
	bl	gf_below
	lsl	x0,x0,16
	str	x0,[x19,8]
	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.r
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree de g_rain
rain_new:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,GF_W-40
	bl	gf_below
	lsl	x0,x0,16
	str	x0,[x19]
	mov	x0,-60
	lsl	x0,x0,16
	str	x0,[x19,8]
	movl	x0,50000
	bl	gf_below
	movl	x1,24000
	add	x0,x0,x1
	str	x0,[x19,16]
	mov	x0,7
	bl	gf_below
	str	w0,[x19,24]
	mov	x0,4
	bl	gf_below
	str	w0,[x19,28]
	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  La couche fixe
; ---------------------------------------------------------------------------

; le ciel du niveau, dans la couche
gen_sky:
	prologue
	ldr	x0,[x28,g_level]
	mov	x1,NTHEME
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	adr	x1,themes
	add	x0,x1,x0,lsl 4
	ldp	w1,w2,[x0,4]			; bas, nuages
	ldr	w0,[x0]				; haut
	mov	x3,140
	bl	ga_sky
	epilogue

; x0..x3 = rectangle : un panneau de verre (couche)
glass:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x4,10
	movl	w5,0x00202C40
	movl	w6,0x00121A28
	mov	x7,210
	movl	w8,0x00708098
	bl	ga_panel
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	bl	ga_mark
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = x, x2 = base : un libelle discret (canevas)
t_label:
	movl	w3,0x00A8B4C4
	mov	x4,12
	adr	x5,ui_font_b
	b	gf_text

build_layer:
	prologue
	ga_push
	str	xzr,[x28,g_ldirty]
	bl	gen_sky
	ldv	x0,gf_layer
	stv	x0,gf_dst
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.eq	.title
	; le puits : un cadre de metal, le fond sombre
	mov	x0,WX-8
	mov	x1,WY-8
	mov	x2,WW+16
	mov	x3,WH+16
	mov	x4,10
	movl	w5,0x005C6A84
	movl	w6,0x0028303E
	mov	x7,256
	movl	w8,0x00A0B0C8
	bl	ga_panel
	mov	x0,WX-2
	mov	x1,WY-2
	mov	x2,WW+3
	mov	x3,WH+3
	mov	x4,3
	movl	w5,0x00080C14
	movl	w6,0x00121826
	mov	x7,256
	mov	w8,0
	bl	ga_panel
	mov	x19,1				; les colonnes
.gc:	mov	x0,CELL
	mul	x0,x19,x0
	add	x0,x0,WX-1
	mov	x1,WY
	mov	x2,1
	mov	x3,WH
	movl	w4,0x00182030
	bl	gf_rect
	add	x19,x19,1
	cmp	x19,COLS
	b.lo	.gc
	mov	x19,1				; les lignes
.gr:	mov	x0,CELL
	mul	x1,x19,x0
	add	x1,x1,WY-1
	mov	x0,WX
	mov	x2,WW
	mov	x3,1
	movl	w4,0x00182030
	bl	gf_rect
	add	x19,x19,1
	cmp	x19,ROWS
	b.lo	.gr
	mov	x19,0				; l'ombre du bord haut
.gs:	mov	x0,WX
	add	x1,x19,WY
	mov	x2,WW
	mov	x3,1
	mov	x4,0
	mov	w5,0
	mov	w6,0
	mov	x7,16
	sub	x7,x7,x19
	lsl	x7,x7,4
	mov	w8,0
	bl	ga_panel
	add	x19,x19,1
	cmp	x19,14
	b.lo	.gs
	mov	x0,WX-8
	mov	x1,WY-8
	mov	x2,WW+16
	mov	x3,WH+16
	bl	ga_mark
	; les panneaux
	mov	x0,16				; reserve
	mov	x1,20
	mov	x2,178
	mov	x3,104
	bl	glass
	mov	x0,16				; score
	mov	x1,134
	mov	x2,178
	mov	x3,62
	bl	glass
	mov	x0,16				; lignes
	mov	x1,204
	mov	x2,86
	mov	x3,62
	bl	glass
	mov	x0,108				; niveau
	mov	x1,204
	mov	x2,86
	mov	x3,62
	bl	glass
	mov	x0,446				; a venir
	mov	x1,36
	mov	x2,178
	mov	x3,158
	bl	glass
	mov	x0,446				; record
	mov	x1,202
	mov	x2,178
	mov	x3,50
	bl	glass
	adr	x19,zones			; les boutons d'ecran (sauf la reserve)
.z:	ldrh	w0,[x19,8]
	cbz	w0,.zend
	cmp	w0,AC_HOLD
	b.eq	.zn
	mov	x0,x19
	mov	x1,10
	movl	w2,0x00303C54
	mov	x3,150
	movl	w4,0x008090A8
	bl	ga_zone_panel
	ldrh	w0,[x19]
	ldrh	w1,[x19,2]
	ldrh	w2,[x19,4]
	ldrh	w3,[x19,6]
	bl	ga_mark
.zn:	add	x19,x19,10
	b	.z
.zend:	ldv	x0,gf_cv
	bl	ui_canvas_begin
	adr	x0,s_hold
	mov	x1,28
	mov	x2,38
	bl	t_label
	adr	x0,s_score
	mov	x1,28
	mov	x2,152
	bl	t_label
	adr	x0,s_lines
	mov	x1,28
	mov	x2,222
	bl	t_label
	adr	x0,s_level
	mov	x1,120
	mov	x2,222
	bl	t_label
	adr	x0,s_next
	mov	x1,458
	mov	x2,54
	bl	t_label
	adr	x0,s_best
	mov	x1,458
	mov	x2,220
	bl	t_label
	adr	x19,zones			; les libelles des boutons
	adr	x20,glyphs
.zt:	ldrh	w21,[x19,8]
	cbz	w21,.ze
	cmp	w21,AC_HOLD
	b.eq	.ztn
	ldrh	w0,[x19]
	ldrh	w1,[x19,4]
	add	x1,x0,x1,lsr 1
	ldrh	w2,[x19,2]
	ldrh	w3,[x19,6]
	add	x2,x2,x3,lsr 1
	add	x2,x2,8
	sub	x3,x21,1
	lsl	x3,x3,3
	add	x0,x20,x3
	movl	w3,0x00E0E8F4
	mov	x4,22
	cmp	w21,AC_PAUSE
	b.ne	.zg
	mov	x4,13
	sub	x2,x2,3
.zg:	adr	x5,ui_font_b
	bl	gf_text_c
.ztn:	add	x19,x19,10
	b	.zt
.ze:	bl	ui_canvas_end
	b	.out
.title:	mov	x0,120				; l'ecran titre : un voile sous le texte
	mov	x1,214
	mov	x2,400
	mov	x3,170
	mov	x4,18
	movl	w5,0x00101828
	movl	w6,0x00080C18
	mov	x7,180
	movl	w8,0x00506080
	bl	ga_panel
.out:	adr	x0,gf_frame
	stv	x0,gf_dst
	ga_pop
	epilogue

; ---------------------------------------------------------------------------
;  Regles (celles d'Onyx)
; ---------------------------------------------------------------------------

; x0 = piece, x1 = rotation -> g_pc : ses quatre cases
cells_of:
	adr	x2,pieces
	add	x3,x1,x0,lsl 2
	ldrh	w2,[x2,x3,lsl 1]
	add	x3,x28,g_pc
	mov	x4,0
.y:	mov	x5,0
.x:	lsl	x6,x4,2
	add	x6,x6,x5
	mov	x7,15
	sub	x6,x7,x6
	lsr	x7,x2,x6
	tbz	x7,0,.n
	strb	w5,[x3],1
	strb	w4,[x3],1
.n:	add	x5,x5,1
	cmp	x5,4
	b.lo	.x
	add	x4,x4,1
	cmp	x4,4
	b.lo	.y
	ret

; x0 = piece, x1 = rotation, x2 = x, x3 = y -> x0 = 1 si elle touche
collide:
	prologue
	mov	x8,x2
	mov	x9,x3
	bl	cells_of
	add	x2,x28,g_pc
	mov	x3,4
.c:	ldrb	w4,[x2],1
	ldrb	w5,[x2],1
	add	x4,x4,x8
	add	x5,x5,x9
	cmp	x4,0
	b.lt	.hit
	cmp	x4,COLS
	b.ge	.hit
	cmp	x5,ROWS
	b.ge	.hit
	tbnz	x5,63,.n
	mov	x6,COLS
	madd	x6,x5,x6,x4
	add	x7,x28,g_field
	ldrb	w6,[x7,x6]
	cbnz	w6,.hit
.n:	subs	x3,x3,1
	b.ne	.c
	mov	x0,0
	epilogue
.hit:	mov	x0,1
	epilogue

; x0 = decalage x, x1 = decalage y -> x0 = 1 si la piece n'y entre pas
blocked:
	prologue
	ldr	x2,[x28,g_px]
	add	x2,x2,x0
	ldr	x3,[x28,g_py]
	add	x3,x3,x1
	ldr	x0,[x28,g_type]
	ldr	x1,[x28,g_rot]
	bl	collide
	epilogue

new_game:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x0,x28,g_field
	mov	x1,ROWS*COLS*2
.z:	strb	wzr,[x0],1
	subs	x1,x1,1
	b.ne	.z
	add	x0,x28,g_off
	mov	x1,ROWS
.o:	str	xzr,[x0],8
	subs	x1,x1,1
	b.ne	.o
	add	x0,x28,g_clr
	mov	x1,ROWS
.c:	strb	wzr,[x0],1
	subs	x1,x1,1
	b.ne	.c
	str	xzr,[x28,g_score]
	str	xzr,[x28,g_show]
	str	xzr,[x28,g_lines]
	str	xzr,[x28,g_level]
	str	xzr,[x28,g_frames]
	str	xzr,[x28,g_last]
	str	xzr,[x28,g_newbest]
	str	xzr,[x28,g_pop]
	str	xzr,[x28,g_rep]
	mov	x0,-1
	str	x0,[x28,g_hold]
	bl	ga_parts_clear
	mov	x19,0
.n:	mov	x0,7
	bl	gf_below
	add	x1,x28,g_next
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,3
	b.lo	.n
	mov	x0,ST_PLAY
	bl	set_state
	mov	x0,1
	str	x0,[x28,g_ldirty]
	bl	spawn
	ldp	x19,x20,[sp],16
	epilogue

; la prochaine piece entre (x0 = sa forme si elle vient de la reserve : voir put_piece)
spawn:
	prologue
	ldr	x0,[x28,g_next]
	ldr	x1,[x28,g_next+8]
	str	x1,[x28,g_next]
	ldr	x1,[x28,g_next+16]
	str	x1,[x28,g_next+8]
	str	x0,[x28,g_type]
	mov	x0,7
	bl	gf_below
	str	x0,[x28,g_next+16]
	str	xzr,[x28,g_hused]
	bl	put_piece
	epilogue

put_piece:
	prologue
	str	xzr,[x28,g_rot]
	mov	x0,3
	str	x0,[x28,g_px]
	mov	x0,-1
	str	x0,[x28,g_py]
	mov	x0,0
	mov	x1,0
	bl	blocked
	cbz	x0,.out
	bl	game_over
.out:	epilogue

game_over:
	prologue
	bl	keep_best
	mov	x0,ST_DYING
	bl	set_state
	mov	x0,ROWS
	str	x0,[x28,g_grey]
	adr	x0,gs_tab_lose
	bl	gs_seq
	epilogue

keep_best:
	prologue
	ldr	x2,[x28,g_score]
	ldr	x0,[x28,g_best]
	cmp	x2,x0
	b.ls	.out
	str	x2,[x28,g_best]
	mov	x0,1
	str	x0,[x28,g_newbest]
	adr	x0,p_dir
	adr	x1,p_best
	bl	gf_save_best
.out:	epilogue

; la piece se pose, les lignes pleines partent
lock_piece:
	prologue
	ga_push
	ldr	x0,[x28,g_type]
	ldr	x1,[x28,g_rot]
	bl	cells_of
	ldr	x19,[x28,g_px]
	ldr	x20,[x28,g_py]
	ldr	x21,[x28,g_type]
	add	x21,x21,1
	add	x22,x28,g_pc
	mov	x23,4
.c:	ldrb	w0,[x22],1
	ldrb	w1,[x22],1
	add	x0,x0,x19
	add	x1,x1,x20
	tbnz	x1,63,.n
	mov	x2,COLS
	madd	x2,x1,x2,x0
	add	x3,x28,g_field
	strb	w21,[x3,x2]
	add	x3,x28,g_flash
	mov	w4,10
	strb	w4,[x3,x2]
.n:	subs	x23,x23,1
	b.ne	.c
	mov	x0,180
	mov	x1,40
	mov	x2,AKW_TRIANGLE
	mov	x3,110
	bl	gs_note
	; les lignes pleines
	mov	x19,0				; ligne
	mov	x20,0				; combien
	add	x21,x28,g_field
	add	x22,x28,g_clr
.r:	mov	x1,0
	mov	x2,1
.rc:	ldrb	w3,[x21,x1]
	cbnz	w3,.rn
	mov	x2,0
.rn:	add	x1,x1,1
	cmp	x1,COLS
	b.lo	.rc
	strb	w2,[x22,x19]
	add	x20,x20,x2
	add	x21,x21,COLS
	add	x19,x19,1
	cmp	x19,ROWS
	b.lo	.r
	cbz	x20,.none
	str	x20,[x28,g_nclr]
	ldr	x0,[x28,g_lines]
	add	x0,x0,x20
	str	x0,[x28,g_lines]
	mov	x1,100
	mul	x1,x20,x1
	ldr	x0,[x28,g_score]
	add	x0,x0,x1
	str	x0,[x28,g_score]
	mov	x0,ST_CLEAR
	bl	set_state
	adr	x0,snd_line
	cmp	x20,4
	b.lo	.s
	adr	x0,snd_tetris
	add	x1,x28,g_tx
	ldr	x1,[x1,TX_TETRIS*8]
	str	x1,[x28,g_popspr]
	mov	x1,70
	str	x1,[x28,g_pop]
	mov	x1,6
	str	x1,[x28,g_shake]
.s:	bl	gs_seq
	b	.out
.none:	bl	spawn
.out:	ga_pop
	epilogue

; les lignes marquees s'en vont, le reste tombe
collapse:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,ROWS-1			; ecriture
	mov	x20,ROWS-1			; lecture
	add	x9,x28,g_clr
.r:	ldrb	w0,[x9,x20]
	cbnz	w0,.n
	cmp	x19,x20
	b.eq	.keep
	mov	x0,COLS
	mul	x1,x20,x0
	mul	x2,x19,x0
	add	x3,x28,g_field
	add	x1,x1,x3
	add	x2,x2,x3
	mov	x3,0
.cp:	ldrb	w4,[x1,x3]
	strb	w4,[x2,x3]
	add	x3,x3,1
	cmp	x3,COLS
	b.lo	.cp
.keep:	sub	x0,x20,x19
	mov	x1,CELL
	mul	x0,x0,x1
	add	x1,x28,g_off
	str	x0,[x1,x19,lsl 3]
	sub	x19,x19,1
.n:	subs	x20,x20,1
	b.pl	.r
	add	x2,x28,g_field			; le haut se vide
.top:	tbnz	x19,63,.c
	mov	x0,COLS
	mul	x1,x19,x0
	add	x1,x1,x2
	mov	x3,0
.tz:	strb	wzr,[x1,x3]
	add	x3,x3,1
	cmp	x3,COLS
	b.lo	.tz
	sub	x19,x19,1
	b	.top
.c:	mov	x0,0				; marques effacees
.cz:	strb	wzr,[x9,x0]
	add	x0,x0,1
	cmp	x0,ROWS
	b.lo	.cz
	ldr	x0,[x28,g_lines]		; le niveau : tous les 10 lignes
	mov	x1,10
	udiv	x0,x0,x1
	ldr	x1,[x28,g_level]
	cmp	x0,x1
	b.eq	.out
	str	x0,[x28,g_level]
	mov	x0,1
	str	x0,[x28,g_ldirty]
	add	x1,x28,g_tx
	ldr	x1,[x1,TX_LEVEL*8]
	str	x1,[x28,g_popspr]
	mov	x1,70
	str	x1,[x28,g_pop]
	adr	x0,gs_tab_win
	bl	gs_seq
.out:	ldp	x19,x20,[sp],16
	epilogue

step_down:
	prologue
	mov	x0,0
	mov	x1,1
	bl	blocked
	cbnz	x0,.lock
	ldr	x0,[x28,g_py]
	add	x0,x0,1
	str	x0,[x28,g_py]
	b	.out
.lock:	bl	lock_piece
.out:	epilogue

; tourner : sur place, puis une case a cote, deux pour la barre
try_rotate:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x19,[x28,g_rot]
	add	x19,x19,1
	and	x19,x19,3
	adr	x20,kicks
.k:	ldrsb	x2,[x20],1
	cmp	x2,99
	b.eq	.out
	ldr	x0,[x28,g_type]
	cbnz	x0,.t				; (la barre seule a +-2)
	b	.try
.t:	cmp	x2,2
	b.eq	.k
	cmp	x2,-2
	b.eq	.k
.try:	ldr	x3,[x28,g_px]
	add	x2,x2,x3
	stp	x2,xzr,[sp,-16]!
	ldr	x3,[x28,g_py]
	mov	x1,x19
	bl	collide
	ldp	x2,x3,[sp],16
	cbnz	x0,.k
	str	x2,[x28,g_px]
	str	x19,[x28,g_rot]
	mov	x0,620
	mov	x1,25
	mov	x2,AKW_TRIANGLE
	mov	x3,70
	bl	gs_note
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = decalage : la piece glisse
slide:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x1,0
	bl	blocked
	cbnz	x0,.out
	ldr	x0,[x28,g_px]
	add	x0,x0,x19
	str	x0,[x28,g_px]
	mov	x0,300
	mov	x1,12
	mov	x2,AKW_SQUARE
	mov	x3,26
	bl	gs_note
.out:	ldr	x19,[sp],16
	epilogue

hard_drop:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,0
.d:	mov	x0,0
	mov	x1,1
	bl	blocked
	cbnz	x0,.land
	ldr	x0,[x28,g_py]
	add	x0,x0,1
	str	x0,[x28,g_py]
	add	x19,x19,1
	b	.d
.land:	cbz	x19,.go				; la trainee, sous chaque case
	ldr	x0,[x28,g_type]
	ldr	x1,[x28,g_rot]
	bl	cells_of
	add	x20,x28,g_pc
	mov	x21,4
.c:	ldrb	w0,[x20],1
	ldrb	w1,[x20],1
	ldr	x2,[x28,g_px]
	add	x0,x0,x2
	mov	x2,CELL
	mul	x0,x0,x2
	add	x0,x0,WX+10
	ldr	x2,[x28,g_py]
	add	x1,x1,x2
	mov	x2,CELL
	mul	x1,x1,x2
	add	x1,x1,WY
	stp	x0,x1,[sp,-16]!
	mov	x22,6
.p:	mov	x0,x19
	add	x0,x0,1
	lsl	x0,x0,1
	bl	gf_below
	movl	x3,50000
	mul	x3,x0,x3
	asr	x3,x3,3
	neg	x3,x3
	add	x0,x28,g_rain
	mov	x2,0
	ldr	x4,[x28,g_type]
	adr	x5,colors
	ldr	w4,[x5,x4,lsl 2]
	ldp	x0,x1,[sp]
	mov	x5,14
	mov	x6,2
	mov	x7,0
	bl	ga_part
	subs	x22,x22,1
	b.ne	.p
	add	sp,sp,16
	subs	x21,x21,1
	b.ne	.c
	mov	x0,3
	str	x0,[x28,g_shake]
	mov	x0,110
	mov	x1,70
	mov	x2,AKW_NOISE
	mov	x3,90
	bl	gs_note
.go:	bl	lock_piece
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la reserve : la piece y va, celle qui y etait entre
do_hold:
	prologue
	ldr	x0,[x28,g_hused]
	cbnz	x0,.out
	ldr	x1,[x28,g_type]
	ldr	x0,[x28,g_hold]
	str	x1,[x28,g_hold]
	tbnz	x0,63,.new
	str	x0,[x28,g_type]
	bl	put_piece
	b	.m
.new:	bl	spawn
.m:	mov	x0,1
	str	x0,[x28,g_hused]
	mov	x0,440
	mov	x1,40
	mov	x2,AKW_SINE
	mov	x3,90
	bl	gs_note
.out:	epilogue

; x0 = etat
set_state:
	str	x0,[x28,g_state]
	str	xzr,[x28,g_timer]
	mov	x1,1
	cmp	x0,ST_PAUSE
	cset	x0,eq
	stv	x0,gf_still
	stv	x1,gf_redraw
	ret

do_pause:
	b	gp_open

; (gamepause) la partie s'arrete -> x0 = 1 si elle etait en cours
game_pause:
	prologue
	str	xzr,[x28,g_rep]
	str	xzr,[x28,g_btn]
	ldr	x1,[x28,g_state]
	mov	x0,0
	cmp	x1,ST_PLAY
	b.ne	.out
	mov	x0,ST_PAUSE
	bl	set_state
	mov	x0,1
.out:	epilogue

; (gamepause) la partie repart
game_resume:
	prologue
	ldr	x1,[x28,g_state]
	cmp	x1,ST_PAUSE
	b.ne	.out
	mov	x0,ST_PLAY
	bl	set_state
.out:	epilogue

; x0 = action AC_*
act:
	prologue
	cmp	x0,AC_PAUSE
	b.eq	.pause
	ldr	x1,[x28,g_state]
	cmp	x1,ST_PLAY
	b.ne	.out
	cmp	x0,AC_LEFT
	b.ne	.r
	mov	x0,-1
	bl	slide
	b	.out
.r:	cmp	x0,AC_RIGHT
	b.ne	.d
	mov	x0,1
	bl	slide
	b	.out
.d:	cmp	x0,AC_DOWN
	b.ne	.t
	bl	step_down
	ldr	x0,[x28,g_frames]
	str	x0,[x28,g_last]
	b	.out
.t:	cmp	x0,AC_ROT
	b.ne	.h
	bl	try_rotate
	b	.out
.h:	cmp	x0,AC_DROP
	b.ne	.hd
	bl	hard_drop
	ldr	x0,[x28,g_frames]
	str	x0,[x28,g_last]
	b	.out
.hd:	cmp	x0,AC_HOLD
	b.ne	.out
	bl	do_hold
	b	.out
.pause:	bl	do_pause
.out:	epilogue

; x0 = action qui se repete, x1 = sa touche (0 : bouton, -1 : manette)
rep_start:
	prologue
	ldr	x2,[x28,g_rep]
	cmp	x2,x0
	b.eq	.out				; (la repetition du clavier)
	str	x0,[x28,g_rep]
	str	x1,[x28,g_repk]
	mov	x1,13
	str	x1,[x28,g_rept]
	bl	act
.out:	epilogue

; ---------------------------------------------------------------------------
;  Une image
; ---------------------------------------------------------------------------
step:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x0,[x28,g_tick]
	add	x0,x0,1
	str	x0,[x28,g_tick]
	bl	gs_tick
	bl	ga_parts_step
	add	x19,x28,g_mote			; les poussieres montent
	mov	x20,NM
.m:	ldp	w0,w1,[x19,4]
	sub	w0,w0,w1
	cmp	w0,0
	b.ge	.mo
	mov	w1,GF_H
	lsl	w0,w1,16
.mo:	str	w0,[x19,4]
	add	x19,x19,16
	subs	x20,x20,1
	b.ne	.m
	ldr	x0,[x28,g_show]			; le score affiche rattrape
	ldr	x1,[x28,g_score]
	subs	x2,x1,x0
	b.ls	.sh
	lsr	x2,x2,3
	add	x0,x0,x2
	add	x0,x0,1
	str	x0,[x28,g_show]
.sh:	ldr	x0,[x28,g_shake]
	cbz	x0,.pop
	sub	x0,x0,1
	str	x0,[x28,g_shake]
.pop:	ldr	x0,[x28,g_pop]
	cbz	x0,.fl
	sub	x0,x0,1
	str	x0,[x28,g_pop]
.fl:	add	x0,x28,g_flash
	mov	x1,ROWS*COLS
.f:	ldrb	w2,[x0]
	cbz	w2,.fn
	sub	w2,w2,1
	strb	w2,[x0]
.fn:	add	x0,x0,1
	subs	x1,x1,1
	b.ne	.f
	add	x0,x28,g_off			; les lignes finissent de tomber
	mov	x1,ROWS
.o:	ldr	x2,[x0]
	cbz	x2,.on
	subs	x2,x2,7
	csel	x2,xzr,x2,lo
	str	x2,[x0]
.on:	add	x0,x0,8
	subs	x1,x1,1
	b.ne	.o
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.ne	.play
	add	x19,x28,g_rain
	mov	x20,NRAIN
.r:	ldp	x0,x1,[x19,8]
	add	x0,x0,x1
	str	x0,[x19,8]
	asr	x0,x0,16
	cmp	x0,GF_H
	b.lt	.rn
	mov	x0,x19
	bl	rain_new
.rn:	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.r
	b	.out
.play:	cmp	x0,ST_PLAY
	b.ne	.clr
	ldr	x19,[x28,g_rep]			; la touche tenue
	cbz	x19,.grav
	ldr	x1,[x28,g_repk]
	cbz	x1,.heldo
	tbnz	x1,63,.padh
	adr	x2,gf_held
	ldrb	w1,[x2,x1]
	cbnz	w1,.heldo
	b	.drop
.padh:	adr	x2,pad_bits
	ldrb	w1,[x2,x19]
	ldv	x2,gp_now
	lsr	x2,x2,x1
	tbnz	x2,0,.heldo
.drop:	str	xzr,[x28,g_rep]
	b	.grav
.heldo:	ldr	x0,[x28,g_rept]
	subs	x0,x0,1
	str	x0,[x28,g_rept]
	b.ne	.grav
	mov	x1,3
	cmp	x19,AC_DOWN
	b.ne	.setr
	mov	x1,2
.setr:	str	x1,[x28,g_rept]
	mov	x0,x19
	bl	act
.grav:	ldr	x0,[x28,g_state]		; (elle a pu changer)
	cmp	x0,ST_PLAY
	b.ne	.out
	ldr	x0,[x28,g_frames]
	add	x0,x0,1
	str	x0,[x28,g_frames]
	ldr	x1,[x28,g_last]
	sub	x0,x0,x1
	ldr	x1,[x28,g_lines]		; plus vite avec les lignes, comme Onyx
	mov	x2,30
	subs	x1,x2,x1
	mov	x2,5
	csel	x1,x2,x1,lo
	cmp	x1,5
	csel	x1,x2,x1,lt
	cmp	x0,x1
	b.lo	.out
	ldr	x0,[x28,g_frames]
	str	x0,[x28,g_last]
	bl	step_down
	b	.out
.clr:	cmp	x0,ST_CLEAR
	b.ne	.dy
	ldr	x19,[x28,g_timer]		; les cases s'en vont depuis le milieu
	add	x19,x19,1
	str	x19,[x28,g_timer]
	and	x0,x19,3
	cmp	x0,1
	b.ne	.cend
	lsr	x0,x19,2			; 0..4
	bl	clear_step
.cend:	cmp	x19,20
	b.lo	.out
	bl	collapse
	mov	x0,ST_PLAY
	bl	set_state
	bl	spawn
	b	.out
.dy:	cmp	x0,ST_DYING
	b.ne	.ov
	ldr	x19,[x28,g_tick]
	tbz	x19,0,.out
	ldr	x0,[x28,g_grey]
	cbz	x0,.dead
	sub	x0,x0,1
	str	x0,[x28,g_grey]
	b	.out
.dead:	mov	x0,ST_OVER
	bl	set_state
	b	.out
.ov:	ldr	x0,[x28,g_timer]
	add	x0,x0,1
	str	x0,[x28,g_timer]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = rang (0..4) : les deux cases de ce rang, sur chaque ligne marquee
clear_step:
	prologue
	ga_push
	mov	x24,x0
	mov	x19,0				; ligne
.r:	add	x0,x28,g_clr
	ldrb	w0,[x0,x19]
	cbz	w0,.n
	mov	x20,4
	sub	x20,x20,x24			; colonne de gauche
	mov	x21,5
	add	x21,x21,x24			; de droite
	mov	x0,x20
	mov	x1,x19
	bl	blow_cell
	mov	x0,x21
	mov	x1,x19
	bl	blow_cell
.n:	add	x19,x19,1
	cmp	x19,ROWS
	b.lo	.r
	ga_pop
	epilogue

; x0 = colonne, x1 = ligne : la case part en eclats
blow_cell:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x2,COLS
	madd	x2,x1,x2,x0
	add	x3,x28,g_field
	ldrb	w4,[x3,x2]
	strb	wzr,[x3,x2]
	cbz	w4,.out
	sub	x4,x4,1
	adr	x3,colors
	ldr	w2,[x3,x4,lsl 2]
	mov	x3,CELL
	mul	x0,x0,x3
	add	x0,x0,WX+10
	mul	x1,x1,x3
	add	x1,x1,WY+10
	mov	x3,9
	movl	x4,180000
	mov	x5,16
	mov	x6,3
	movl	x7,9000
	bl	ga_burst
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Evenements
; ---------------------------------------------------------------------------
do_menu:
	prologue
	cmp	x0,ID_QUIT
	b.ne	.a
	mov	x0,1
	stv	x0,gf_quit
	b	.out
.a:	cmp	x0,ID_NEW
	b.ne	.b
	bl	new_game
	b	.out
.b:	cmp	x0,ID_PAUSE
	b.ne	.c
	bl	do_pause
	b	.out
.c:	cmp	x0,ID_SOUND
	b.ne	.out
	bl	gs_toggle
	cmp	x0,0
	mov	x1,0
	mov	x2,UMS_CHECKED
	csel	x1,x2,x1,ne
	mov	x0,ID_SOUND
	bl	ui_menu_state
.out:	epilogue

; Entree, clic, appui hors de la partie -> x0 = 1 si cela a servi
start_act:
	prologue
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.eq	.new
	cmp	x0,ST_OVER
	b.ne	.p
	ldr	x0,[x28,g_timer]
	cmp	x0,25
	b.lo	.yes
.new:	bl	new_game
.yes:	mov	x0,1
	b	.out
.p:	cmp	x0,ST_PAUSE
	b.ne	.no
	bl	do_pause
	b	.yes
.no:	mov	x0,0
.out:	epilogue

on_key:
	prologue
	adr	x1,ui_ev
	ldr	w2,[x1,EV_KIND]
	cbnz	w2,.press
	ldr	w0,[x1,EV_CODE]
	cmp	w0,KEY_UP
	b.ne	.out
	str	xzr,[x28,g_rotk]
	b	.out
.press:	mov	x0,0
	bl	ui_menu_key
	tbnz	x0,63,.key
	bl	do_menu
	b	.out
.key:	adr	x1,ui_ev
	ldr	w0,[x1,EV_CHAR]
	orr	w0,w0,0x20
	cmp	w0,'p'
	b.ne	.r
	bl	do_pause
	b	.out
.r:	cmp	w0,'r'
	b.ne	.h
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.eq	.out
	bl	new_game
	b	.out
.h:	cmp	w0,'c'
	b.ne	.c
	mov	x0,AC_HOLD
	bl	act
	b	.out
.c:	ldr	w19,[x1,EV_CODE]
	cmp	w19,KEY_ENTER
	b.eq	.go
	cmp	w19,KEY_SPACE
	b.ne	.k
	bl	start_act
	cbnz	x0,.out
	mov	x0,AC_DROP
	bl	act
	b	.out
.go:	bl	start_act
	b	.out
.k:	cmp	w19,KEY_LEFT
	b.ne	.k2
	mov	x0,AC_LEFT
	b	.rep
.k2:	cmp	w19,KEY_RIGHT
	b.ne	.k3
	mov	x0,AC_RIGHT
	b	.rep
.k3:	cmp	w19,KEY_DOWN
	b.ne	.k4
	mov	x0,AC_DOWN
.rep:	mov	x1,x19
	bl	rep_start
	b	.out
.k4:	cmp	w19,KEY_UP
	b.ne	.out
	ldr	x0,[x28,g_rotk]
	cbnz	x0,.out
	mov	x0,1
	str	x0,[x28,g_rotk]
	mov	x0,AC_ROT
	bl	act
.out:	epilogue

on_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	gf_mouse
	mov	x19,x0
	mov	x20,x1
	adr	x2,ui_ev
	ldr	w3,[x2,EV_KIND]
	cmp	w3,2
	b.eq	.rel
	cmp	w3,1
	b.ne	.out
	ldr	w3,[x2,EV_CODE]
	tbz	w3,0,.out
	mov	x0,16
	bl	gf_drag_top
	cbnz	x0,.out
	bl	start_act
	cbnz	x0,.out
	adr	x0,zones
	mov	x1,x19
	mov	x2,x20
	bl	ga_zone
	cbz	x0,.well
	str	x0,[x28,g_btn]
	cmp	x0,AC_DOWN
	b.hi	.one
	mov	x1,0
	bl	rep_start
	b	.out
.one:	bl	act
	b	.out
.well:	sub	x0,x19,WX			; un appui dans le puits tourne
	cmp	x0,WW
	b.hs	.top
	sub	x0,x20,WY
	cmp	x0,WH
	b.hs	.top
	mov	x0,AC_ROT
	bl	act
	b	.out
.top:	b	.out
.rel:	str	xzr,[x28,g_btn]
	ldr	x0,[x28,g_rep]
	cbz	x0,.out
	ldr	x0,[x28,g_repk]
	cbnz	x0,.out
	str	xzr,[x28,g_rep]
.out:	ldp	x19,x20,[sp],16
	epilogue

; WEV_PAD : les fronts ; manette 0 (le clavier mappe) deja arrivee en touches
on_pad:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,ui_ev
	ldr	w1,[x0,4]
	cbz	w1,.out
	ldv	x20,gp_new			; (Start : gamepause)
	tst	x20,PAD_A
	b.eq	.b
	bl	start_act
	cbnz	x0,.out
	mov	x0,AC_ROT
	bl	act
.b:	tst	x20,PAD_B
	b.eq	.x
	mov	x0,AC_DROP
	bl	act
.x:	tst	x20,PAD_X
	b.eq	.l
	mov	x0,AC_HOLD
	bl	act
.l:	tst	x20,PAD_LEFT
	b.eq	.r
	mov	x0,AC_LEFT
	mov	x1,-1
	bl	rep_start
.r:	tst	x20,PAD_RIGHT
	b.eq	.d
	mov	x0,AC_RIGHT
	mov	x1,-1
	bl	rep_start
.d:	tst	x20,PAD_DOWN
	b.eq	.out
	mov	x0,AC_DOWN
	mov	x1,-1
	bl	rep_start
.out:	ldp	x19,x20,[sp],16
	epilogue

on_event:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	gp_event
	cbnz	x0,.out
	mov	x0,x19
	cmp	x0,WEV_MENU
	b.ne	.k
	adr	x0,ui_ev
	ldr	w0,[x0,EV_CODE]
	bl	do_menu
	b	.out
.k:	cmp	x0,WEV_KEY
	b.ne	.m
	bl	on_key
	b	.out
.m:	cmp	x0,WEV_MOUSE
	b.ne	.p
	bl	on_mouse
	b	.out
.p:	cmp	x0,WEV_PAD
	b.ne	.f
	bl	on_pad
	b	.out
.f:	cmp	x0,WEV_FOCUS
	b.ne	.out
	str	xzr,[x28,g_btn]
	str	xzr,[x28,g_rep]
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Le dessin
; ---------------------------------------------------------------------------

; x0 = piece, x1 = rotation, x2, x3 = coin, x4 = pas, x5 = table de sprites, x6 = y minimal
draw_piece:
	prologue
	ga_push
	mov	x19,x0
	mov	x20,x2
	mov	x21,x3
	mov	x22,x4
	ldr	x23,[x5,x0,lsl 3]
	mov	x24,x6
	bl	cells_of
	add	x25,x28,g_pc
	mov	x26,4
.c:	ldrb	w0,[x25],1
	ldrb	w1,[x25],1
	mul	x0,x0,x22
	add	x0,x0,x20
	mul	x1,x1,x22
	add	x1,x1,x21
	cmp	x1,x24
	b.lt	.n
	mov	x2,x23
	bl	ga_ablit
.n:	subs	x26,x26,1
	b.ne	.c
	ga_pop
	epilogue

; x0 = piece, x1, x2 = centre : la piece en petit, au milieu
draw_mini:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x1,0
	bl	cells_of
	add	x0,x28,g_pc			; sa boite
	mov	x1,9
	mov	x2,0
	mov	x3,9
	mov	x4,0
	mov	x5,4
.b:	ldrb	w6,[x0],1
	ldrb	w7,[x0],1
	cmp	x6,x1
	csel	x1,x6,x1,lo
	cmp	x6,x2
	csel	x2,x6,x2,hi
	cmp	x7,x3
	csel	x3,x7,x3,lo
	cmp	x7,x4
	csel	x4,x7,x4,hi
	subs	x5,x5,1
	b.ne	.b
	add	x1,x1,x2
	add	x3,x3,x4
	add	x1,x1,1
	add	x3,x3,1
	mov	x2,MINI
	mul	x1,x1,x2
	mul	x3,x3,x2
	sub	x2,x20,x1,lsr 1
	sub	x3,x21,x3,lsr 1
	mov	x0,x19
	mov	x1,0
	mov	x4,MINI
	add	x5,x28,g_sm
	mov	x6,0
	bl	draw_piece
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

draw_field:
	prologue
	ga_push
	ldr	x26,[x28,g_shake]		; le puits tremble
	tbz	x26,0,.ns
	neg	x26,x26
.ns:	add	x26,x26,x26
	add	x21,x28,g_field
	add	x22,x28,g_flash
	mov	x19,0				; ligne
.r:	add	x0,x28,g_off
	ldr	x23,[x0,x19,lsl 3]
	mov	x0,CELL
	mul	x24,x19,x0
	add	x24,x24,WY
	sub	x24,x24,x23
	add	x24,x24,x26
	mov	x20,0				; colonne
.c:	ldrb	w0,[x21],1
	cbz	w0,.n
	sub	x2,x0,1
	ldr	x3,[x28,g_state]
	cmp	x3,ST_DYING
	b.lo	.col
	ldr	x3,[x28,g_grey]
	cmp	x19,x3
	b.lo	.col
	mov	x2,7
.col:	add	x3,x28,g_sb
	ldr	x2,[x3,x2,lsl 3]
	mov	x0,CELL
	mul	x0,x20,x0
	add	x0,x0,WX
	mov	x1,x24
	cmp	x1,WY
	b.lt	.n
	bl	ga_ablit
	ldrb	w0,[x22]			; elle vient de se poser
	cbz	w0,.n
	mov	x0,CELL
	mul	x0,x20,x0
	add	x0,x0,WX-3
	sub	x1,x24,3
	ldr	x2,[x28,g_sglow]
	bl	gf_glow
.n:	add	x22,x22,1
	add	x20,x20,1
	cmp	x20,COLS
	b.lo	.c
	add	x19,x19,1
	cmp	x19,ROWS
	b.lo	.r
	ldr	x0,[x28,g_state]		; les lignes qui partent : un eclair
	cmp	x0,ST_CLEAR
	b.ne	.out
	ldr	x25,[x28,g_timer]
	mov	x19,0
.w:	add	x0,x28,g_clr
	ldrb	w0,[x0,x19]
	cbz	w0,.wn
	mov	x0,WX
	mov	x1,CELL
	mul	x1,x19,x1
	add	x1,x1,WY
	mov	x2,WW
	mov	x3,CELL-1
	mov	x4,0
	movl	w5,0x00FFFFFF
	mov	w6,w5
	mov	x7,20
	sub	x7,x7,x25
	lsl	x7,x7,3
	mov	w8,0
	bl	ga_panel
.wn:	add	x19,x19,1
	cmp	x19,ROWS
	b.lo	.w
.out:	ga_pop
	epilogue

; x0 = texte TX_*, x1 = centre, x2 = y
tx_c:
	add	x3,x28,g_tx
	ldr	x0,[x3,x0,lsl 3]
	b	ga_show

; un voile et deux lignes au milieu du puits
; x0 = titre TX_*, x1 = suite TX_*
well_box:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,WX
	mov	x1,WY
	mov	x2,WW
	mov	x3,WH
	mov	x4,0
	mov	w5,0
	mov	w6,0
	mov	x7,120
	mov	w8,0
	bl	ga_panel
	mov	x0,WX+12
	mov	x1,170
	mov	x2,WW-24
	mov	x3,120
	mov	x4,12
	movl	w5,0x00283448
	movl	w6,0x00141C2C
	mov	x7,240
	movl	w8,0x00809CC0
	bl	ga_panel
	mov	x0,x19
	mov	x1,WX+WW/2
	mov	x2,184
	bl	tx_c
	mov	x0,x20
	mov	x1,WX+WW/2
	mov	x2,252
	bl	tx_c
	ldp	x19,x20,[sp],16
	epilogue

draw_title:
	prologue
	ga_push
	add	x19,x28,g_rain			; la pluie de pieces
	mov	x20,NRAIN
.r:	ldr	x2,[x19]
	asr	x2,x2,16
	ldr	x3,[x19,8]
	asr	x3,x3,16
	ldr	w0,[x19,24]
	ldr	w1,[x19,28]
	mov	x4,MINI
	add	x5,x28,g_sm
	mov	x6,-100
	bl	draw_piece
	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.r
	; le nom, en blocs ; une vague le traverse
	adr	x19,logo
	mov	x20,0				; lettre
.l:	mov	x21,0				; ligne
.ly:	ldrb	w22,[x19,x21]
	mov	x23,0				; colonne
.lx:	mov	x0,4
	sub	x0,x0,x23
	lsr	x1,x22,x0
	tbz	x1,0,.ln
	mov	x0,6
	mul	x0,x20,x0
	add	x0,x0,x23
	lsl	x25,x0,3
	ldr	x1,[x28,g_tick]
	add	x1,x1,x1
	add	x0,x25,x1
	bl	ga_sin8
	lsr	x0,x0,4
	mov	x1,MINI
	mul	x1,x21,x1
	add	x1,x1,LOGO_Y
	add	x24,x1,x0
	mov	x1,MINI
	mul	x0,x20,x1
	add	x0,x0,x0,lsl 1
	add	x0,x0,x0
	mul	x1,x23,x1
	add	x0,x0,x1
	add	x0,x0,LOGO_X
	stp	x0,x24,[sp,-16]!
	add	x0,x0,2
	add	x1,x24,4
	adr	x3,logo_col
	ldrb	w3,[x3,x20]
	add	x4,x28,g_sm
	ldr	x2,[x4,x3,lsl 3]
	bl	ga_shadow
	ldp	x0,x1,[sp],16
	adr	x3,logo_col
	ldrb	w3,[x3,x20]
	add	x4,x28,g_sm
	ldr	x2,[x4,x3,lsl 3]
	bl	ga_ablit
.ln:	add	x23,x23,1
	cmp	x23,5
	b.lo	.lx
	add	x21,x21,1
	cmp	x21,7
	b.lo	.ly
	add	x19,x19,7
	add	x20,x20,1
	cmp	x20,6
	b.lo	.l
	mov	x0,TX_SUB
	mov	x1,GF_W/2
	mov	x2,226
	bl	tx_c
	ldr	x0,[x28,g_tick]			; l'invite clignote doucement
	lsr	x0,x0,1
	lsl	x0,x0,2
	bl	ga_sin8
	cmp	x0,40
	b.lo	.rec
	mov	x0,TX_PROMPT
	mov	x1,GF_W/2
	mov	x2,284
	bl	tx_c
.rec:	mov	x0,TX_RECORD
	mov	x1,GF_W/2-40
	mov	x2,332
	bl	tx_c
	ldr	x0,[x28,g_best]
	mov	x1,GF_W/2+8
	mov	x2,330
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ga_pop
	epilogue

draw:
	prologue
	ga_push
	ldr	x0,[x28,g_ldirty]
	cbz	x0,.base
	bl	build_layer
.base:	adr	x0,gf_frame
	ldv	x1,gf_layer
	movl	x2,GF_PITCH*GF_H
	bl	gf_copy
	ldr	x0,[x28,g_sorb]			; deux lumieres derivent derriere
	ldr	x1,[x28,g_sorb+8]
	ldr	x2,[x28,g_tick]
	bl	ga_orbs
	add	x19,x28,g_mote
	mov	x20,NM
.mt:	ldr	w0,[x19]
	asr	w0,w0,16
	ldr	w1,[x19,4]
	asr	w1,w1,16
	ldr	x2,[x28,g_smote]
	bl	gf_bglow
	add	x19,x19,16
	subs	x20,x20,1
	b.ne	.mt
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.ne	.game
	bl	draw_title
	bl	ga_parts_draw
	b	.out
.game:	bl	draw_field
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.ne	.side
	; le fantome : ou elle tomberait
	mov	x19,0
.g:	add	x1,x19,1
	mov	x0,0
	bl	blocked
	cbnz	x0,.gd
	add	x19,x19,1
	b	.g
.gd:	cbz	x19,.cur
	ldr	x0,[x28,g_type]
	ldr	x1,[x28,g_rot]
	ldr	x2,[x28,g_px]
	mov	x3,CELL
	mul	x2,x2,x3
	add	x2,x2,WX
	ldr	x3,[x28,g_py]
	add	x3,x3,x19
	mov	x4,CELL
	mul	x3,x3,x4
	add	x3,x3,WY
	add	x5,x28,g_sg
	mov	x6,WY
	bl	draw_piece
.cur:	ldr	x0,[x28,g_type]
	ldr	x1,[x28,g_rot]
	ldr	x2,[x28,g_px]
	mov	x3,CELL
	mul	x2,x2,x3
	add	x2,x2,WX
	ldr	x3,[x28,g_py]
	mov	x4,CELL
	mul	x3,x3,x4
	add	x3,x3,WY
	add	x5,x28,g_sb
	mov	x6,WY
	bl	draw_piece
.side:	ldr	x0,[x28,g_hold]
	tbnz	x0,63,.nx
	mov	x1,105
	mov	x2,80
	bl	draw_mini
.nx:	mov	x19,0				; les pieces a venir
.n:	add	x0,x28,g_next
	ldr	x0,[x0,x19,lsl 3]
	mov	x1,535
	mov	x2,44
	mul	x2,x19,x2
	add	x2,x2,84
	bl	draw_mini
	add	x19,x19,1
	cmp	x19,3
	b.lo	.n
	ldr	x0,[x28,g_show]
	mov	x1,182
	mov	x2,158
	add	x3,x28,g_dbig
	mov	x4,6
	bl	ga_num
	ldr	x0,[x28,g_lines]
	mov	x1,90
	mov	x2,228
	add	x3,x28,g_dbig
	mov	x4,6
	bl	ga_num
	ldr	x0,[x28,g_level]
	add	x0,x0,1
	mov	x1,182
	mov	x2,228
	add	x3,x28,g_dbig
	mov	x4,6
	bl	ga_num
	ldr	x0,[x28,g_best]
	mov	x1,612
	mov	x2,222
	add	x3,x28,g_dsmall
	mov	x4,6
	bl	ga_num
	ldr	x0,[x28,g_btn]			; le bouton tenu s'allume
	cbz	x0,.parts
	adr	x19,zones
.bz:	ldrh	w1,[x19,8]
	cbz	w1,.parts
	cmp	x1,x0
	b.eq	.bf
	add	x19,x19,10
	b	.bz
.bf:	cmp	x1,AC_HOLD
	b.eq	.parts
	mov	x0,x19
	mov	x1,10
	movl	w2,0x0080B0F0
	mov	x3,70
	mov	w4,0
	bl	ga_zone_panel
.parts:	bl	ga_parts_draw
	ldr	x19,[x28,g_pop]			; le texte qui monte
	cbz	x19,.st
	ldr	x2,[x28,g_popspr]
	ldr	w0,[x2]
	lsr	x0,x0,1
	mov	x1,WX+WW/2
	sub	x20,x1,x0
	mov	x0,x20
	add	x1,x19,120
	bl	ga_ablit
.st:	ldr	x0,[x28,g_state]		; (la pause : gamepause)
	cmp	x0,ST_OVER
	b.ne	.out
	mov	x0,TX_OVER
	mov	x1,TX_AGAIN
	bl	well_box
	ldr	x0,[x28,g_score]
	mov	x1,WX+WW/2
	mov	x2,218
	add	x3,x28,g_dbig
	mov	x4,5
	bl	ga_num
	ldr	x0,[x28,g_newbest]
	cbz	x0,.out
	mov	x0,TX_BEST
	mov	x1,WX+WW/2
	mov	x2,300
	bl	tx_c
.out:	ga_pop
	epilogue

; ---------------------------------------------------------------------------
;  Tables
; ---------------------------------------------------------------------------
	align 4
; les sept pieces d'Onyx, quatre rotations : bit 15 - (y * 4 + x)
pieces	dh 0x0F00,0x2222,0x00F0,0x4444		; I
	dh 0xCC00,0xCC00,0xCC00,0xCC00		; O
	dh 0x0E40,0x4C40,0x4E00,0x4640		; T
	dh 0x06C0,0x8C40,0x6C00,0x4620		; S
	dh 0x0C60,0x4C80,0xC600,0x2640		; Z
	dh 0x44C0,0x8E00,0xC880,0x0E20		; J
	dh 0x4460,0x0E80,0xC440,0x2E00		; L
	align 4
colors	dw 0x0020C8E0,0x00F0C820,0x00A848E8,0x0040C850,0x00E84040,0x004070F0,0x00F08A28,0x00687080
kicks	db 0,-1,1,-2,2,99
	align 4
; x, y, l, h, action
zones	dh 16,372,85,88,AC_LEFT
	dh 109,372,85,88,AC_RIGHT
	dh 446,300,178,64,AC_ROT
	dh 446,372,85,88,AC_DOWN
	dh 539,372,85,88,AC_DROP
	dh 446,260,178,32,AC_PAUSE
	dh 16,20,178,104,AC_HOLD
	dh 0,0,0,0,0
	align 4
; ciels : haut, bas, nuages, reserve
themes	dw 0x00101C3C,0x00061024,0x00304C80,0
	dw 0x00301438,0x00100820,0x00783868,0
	dw 0x000C3034,0x00061414,0x00207870,0
	dw 0x00382010,0x00180C08,0x00885830,0
	dw 0x00142C14,0x00081408,0x00388040,0
	dw 0x00301020,0x00180410,0x00903048,0
	align 4
pad_bits db 0,2,3,1
	align 4
logo_col db 0,4,1,3,2,6
	align 4
; T E T R I S, 5 x 7
logo	db 11111b,00100b,00100b,00100b,00100b,00100b,00100b
	db 11111b,10000b,10000b,11110b,10000b,10000b,11111b
	db 11111b,00100b,00100b,00100b,00100b,00100b,00100b
	db 11110b,10001b,10001b,11110b,10100b,10010b,10001b
	db 01110b,00100b,00100b,00100b,00100b,00100b,01110b
	db 01111b,10000b,10000b,01110b,00001b,00001b,11110b
	align 4
snd_line:
	gs_ent	0,523,70,AKW_TRIANGLE,110
	gs_ent	70,784,110,AKW_TRIANGLE,110
	gs_end
snd_tetris:
	gs_ent	0,523,70,AKW_SQUARE,90
	gs_ent	70,659,70,AKW_SQUARE,90
	gs_ent	140,784,70,AKW_SQUARE,90
	gs_ent	210,1047,240,AKW_SQUARE,100
	gs_end
	align 4
tx_table:
	ga_txe	s_prompt,s_base,18,1,0x00FFFFFF,0x00C8DCF8
	ga_txe	s_pause,s_base,26,1,0x00FFFFFF,0x00A8D8FF
	ga_txe	s_resume,s_base,12,0,0x00C8D4E4,0x00A8B4C8
	ga_txe	s_over,s_base,22,1,0x00FFD070,0x00F08840
	ga_txe	s_again,s_base,12,0,0x00C8D4E4,0x00A8B4C8
	ga_txe	s_tetris,s_base,34,1,0x00FFF0A0,0x00FF9030
	ga_txe	s_up,s_base,24,1,0x00C0FFD0,0x0060D890
	ga_txe	s_record,s_base,15,1,0x00B8C8E0,0x008C9CB8
	ga_txe	s_newbest,s_base,15,1,0x00FFE080,0x00F0B040
	ga_txe	s_sub,s_base,14,0,0x00D0DCF0,0x00A0B0C8

; dans le segment de code : tx_table s'en sert a l'adresse relative
s_base:
s_prompt	db 'Espace, A ou toucher pour jouer',0
s_pause		db 'Pause',0
s_resume	db 'P ou toucher pour reprendre',0
s_over		db 'Partie termin',0xC3,0xA9,'e',0
s_again		db 'Espace, A ou toucher : rejouer',0
s_tetris	db 'TETRIS !',0
s_up		db 'Niveau sup',0xC3,0xA9,'rieur',0
s_record	db 'Record',0
s_newbest	db 'Nouveau record !',0
s_sub		db 'Les pi',0xC3,0xA8,'ces tombent, les lignes s',0x27,'en vont',0
s_title		db 'Tetris',0
s_hold		db 'R',0xC3,0xA9,'serve',0
s_score		db 'Score',0
s_lines		db 'Lignes',0
s_level		db 'Niveau',0
s_next		db 'A venir',0
s_best		db 'Record',0
; libelles des boutons, dans l'ordre des actions (8 octets chacun)
glyphs	db 0xE2,0x97,0x80,0,0,0,0,0		; gauche
	db 0xE2,0x96,0xB6,0,0,0,0,0		; droite
	db 0xE2,0x96,0xBC,0,0,0,0,0		; bas
	db 0xE2,0x86,0xBB,0,0,0,0,0		; tourner
	db 0xE2,0x87,0x93,0,0,0,0,0		; chute
	db 0,0,0,0,0,0,0,0
	db 'Pause',0,0,0
p_dir		db 'SD0:/home/tetris',0
p_best		db 'SD0:/home/tetris/best.txt',0
s_menus		db 'MPartie',10
		db 'I1',9,'Nouvelle partie',9,'Ctrl+N',10
		db 'I2',9,'Pause',9,'P',10
		db 'I3c',9,'Son',10
		db '-',10
		db 'I4',9,'Quitter',10,0
	align 4

segment readable writeable
kit_data
		gs_data
		align 8
g_data		rb G_SIZE
ui_data
ga_data
gp_data
gf_data
