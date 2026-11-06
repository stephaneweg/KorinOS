; ============================================================================
;  pipes - le Pipes d'Onyx (user/Apps/pipes, d'apres « Pipe Dream ») : poser les
;  tuyaux de la file avant que l'eau n'arrive ; chaque piece traversee vaut 50
;  (100 en debit rapide), une croix traversee deux fois 500 de plus, une piece
;  remplacee coute 50 ; la manche est gagnee si l'eau passe par assez de pieces
;  avant de deborder. Memes regles et memes durees, habillees : tuyaux de cuivre
;  calcules point par point, eau qui avance dans le tuyau, eclaboussures, son.
;
;  Souris ou doigt ; fleches et Espace ; F : l'eau part ; P : pause ; manette :
;  croix, A pose, X l'eau. Plein ecran ; Start, Echap, P ou le bouton Pause : le
;  menu de pause (gamepause.inc). Taille logique 640 x 480.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'
include '../../inc/pad.inc'

GF_W		= 640
GF_H		= 480
GW		= 10
GH		= 7
NCELLS		= GW*GH
CELL		= 54
NQ		= 5
QX		= 14
PX0		= 88
PY0		= 54
TUBE		= 16*16			; rayon du tuyau, en 1/16 de point
BORE		= 7*16			; rayon de l'interieur
NMAP		= 13			; 0 rien ; 1..7 les pieces ; 9..12 le depart (N, E, S, O)

; cotes
D_N		= 1
D_E		= 2
D_S		= 4
D_W		= 8

; pieces
P_START		= 8
P_WALL		= 9

; etats : ceux d'Onyx, plus le titre
ST_BUILD	= 0
ST_FLOW		= 1
ST_WON		= 2
ST_LOST		= 3
ST_PAUSE	= 4
ST_TITLE	= 5

Z_FAST		= 1
Z_PAUSE		= 2

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

TX_LOGO		= 0
TX_SUB		= 1
TX_PROMPT	= 2
TX_RECORD	= 3
TX_PAUSE	= 4
TX_RESUME	= 5
TX_WON		= 6
TX_NEXT		= 7
TX_LOST		= 8
TX_AGAIN	= 9
TX_HINT		= 10
TX_FAST		= 11
TX_SLASH	= 12
TX_NEWBEST	= 13
NTX		= 14

virtual at 0
g_state		rd 1
g_prev		rd 1
g_timer		rd 1
g_tick		rd 1
g_level		rd 1
g_score		rd 1			; (signe : peut passer sous 0)
g_show		rd 1
g_best		rd 1
g_newbest	rd 1
g_need		rd 1
g_done		rd 1
g_count		rd 1			; ms avant l'eau
g_total		rd 1
g_flow		rd 1			; ms par piece
g_fast		rd 1
g_wx		rd 1			; la case qui se remplit, le cote d'entree
g_wy		rd 1
g_wside		rd 1
g_cx		rd 1			; curseur
g_cy		rd 1
g_bomb		rd 1			; piece remplacee : reste, case
g_bombc		rd 1
g_place		rd 1			; piece posee : reste, case
g_placec	rd 1
g_pad		rd 1
g_ldirty	rd 1
g_btn		rd 1
g_sp		rd NMAP			; sprites des pieces
g_swall		rd 1
g_scur		rd 1
g_svalve	rd 1
g_spiece	rd 1			; lueur
g_sorb		rd 2
g_dbig		rd 10
g_dsmall	rd 10
g_tx		rd NTX
g_q		rd NQ			; la file : [NQ-1] part la premiere
g_type		rb NCELLS
g_dir		rb NCELLS		; depart : son cote
g_inh		rb NCELLS		; cote d'entree de l'eau, horizontal ou seul
g_inv		rb NCELLS
		align 8
g_fh		rb NCELLS*2		; remplissage 0..1000
g_fv		rb NCELLS*2
g_pal		rb 256*4		; l'eau
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
	bl	gs_close
quit:	mov	x0,0
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Les tuyaux, point par point
; ---------------------------------------------------------------------------

; x0 -> x0 = racine entiere (touche x1..x3)
isqrt:
	mov	x1,0
	mov	x2,1
	lsl	x2,x2,30
.b:	cmp	x2,x0
	b.ls	.l
	lsr	x2,x2,2
	cbnz	x2,.b
	b	.out
.l:	cbz	x2,.out
	add	x3,x1,x2
	cmp	x0,x3
	b.lo	.n
	sub	x0,x0,x3
	add	x1,x3,x2
.n:	lsr	x1,x1,1
	lsr	x2,x2,2
	b	.l
.out:	mov	x0,x1
	ret

; x9 = piece, x10, x11 = point (1/16) -> x0 = distance a l'axe (1/16, grand : dehors),
; x1 = position le long du tuyau 0..255 depuis le premier cote
pipe_eval:
	prologue
	mov	x12,CELL*8			; le milieu
	mov	x13,CELL*16			; le bord
	cmp	x9,1
	b.ne	.v
	subs	x0,x11,x12
	cneg	x0,x0,mi
	mov	x1,x10
	b	.ms
.v:	cmp	x9,2
	b.ne	.bend
	subs	x0,x10,x12
	cneg	x0,x0,mi
	mov	x1,x11
.ms:	mov	x2,255
	mul	x1,x1,x2
	udiv	x1,x1,x13
	b	.out
.bend:	cmp	x9,7
	b.eq	.cross
	cmp	x9,9
	b.hs	.st
	; les coudes : un quart de cercle centre sur un coin
	mov	x14,x10				; distance au coin
	mov	x15,x11
	cmp	x9,3
	b.eq	.xr
	cmp	x9,4
	b.ne	.c5
.xr:	sub	x14,x13,x10			; coin de droite
.c5:	cmp	x9,4
	b.eq	.yb
	cmp	x9,5
	b.ne	.c6
.yb:	sub	x15,x13,x11			; coin du bas
.c6:	mul	x0,x14,x14
	madd	x0,x15,x15,x0
	bl	isqrt
	subs	x0,x0,x12
	cneg	x0,x0,mi
	mov	x2,x15				; 3, 5 : dy / (dx + dy)
	cmp	x9,4
	b.eq	.sx
	cmp	x9,6
	b.ne	.sd
.sx:	mov	x2,x14				; 4, 6 : dx / (dx + dy)
.sd:	mov	x3,255
	mul	x2,x2,x3
	add	x3,x14,x15
	add	x3,x3,1
	udiv	x1,x2,x3
	b	.out
.cross:	subs	x0,x11,x12
	cneg	x0,x0,mi
	subs	x1,x10,x12
	cneg	x1,x1,mi
	cmp	x1,x0
	csel	x0,x1,x0,lo
	mov	x1,128
	b	.out
.st:	sub	x2,x9,9				; depart : du milieu vers son cote
	mov	x0,x12
	lsl	x0,x0,4
	mov	x1,0
	cbnz	x2,.st1
	cmp	x11,x12				; N
	b.hs	.out
	sub	x0,x10,x12
	sub	x1,x12,x11
	b	.sm
.st1:	cmp	x2,1
	b.ne	.st2
	cmp	x10,x12				; E
	b.lo	.out
	sub	x0,x11,x12
	sub	x1,x10,x12
	b	.sm
.st2:	cmp	x2,2
	b.ne	.st3
	cmp	x11,x12				; S
	b.lo	.out
	sub	x0,x10,x12
	sub	x1,x11,x12
	b	.sm
.st3:	cmp	x10,x12				; O
	b.hs	.out
	sub	x0,x11,x12
	sub	x1,x12,x10
.sm:	cmp	x0,0
	cneg	x0,x0,lt
	mov	x2,255
	mul	x1,x1,x2
	udiv	x1,x1,x12
.out:	epilogue

; x0 = piece : son sprite (cuivre, ombre et reflet), sa carte (position le long de
; l'interieur, 0 ailleurs)
gen_pipe:
	prologue
	ga_push
	mov	x19,x0
	mov	x0,CELL
	mov	x1,CELL
	bl	ga_new
	add	x1,x28,g_sp
	str	x0,[x1,x19,lsl 3]
	add	x20,x0,8
	mov	x21,CELL*CELL
	mul	x21,x19,x21
	adr	x0,g_maps
	add	x21,x0,x21
	mov	x22,0				; y
.y:	mov	x23,0				; x
.x:	mov	x9,x19
	lsl	x10,x23,4
	add	x10,x10,8
	lsl	x11,x22,4
	add	x11,x11,8
	bl	pipe_eval
	mov	x24,x0				; distance
	mov	x25,x1				; position
	mov	x26,TUBE
	mov	x0,x23				; la bride, au bord de la case
	mov	x1,CELL-1
	sub	x1,x1,x23
	cmp	x0,x1
	csel	x0,x1,x0,hi
	mov	x1,x22
	mov	x2,CELL-1
	sub	x2,x2,x22
	cmp	x1,x2
	csel	x1,x2,x1,hi
	cmp	x0,x1
	csel	x0,x1,x0,hi
	mov	x3,0
	cmp	x0,5
	b.hs	.nf
	cmp	x19,9
	b.lo	.fl
	b	.nf
.fl:	add	x26,x26,32
	mov	x3,1
.nf:	add	x0,x26,8			; dehors ?
	cmp	x24,x0
	b.hs	.n
	cmp	x24,BORE			; l'interieur
	b.hs	.wall
	movl	w1,0x00141C20
	mov	x0,x24
	lsl	x0,x0,6
	mov	x2,BORE
	udiv	x0,x0,x2			; 0..64 : plus sombre au bord
	mov	x2,64
	sub	x2,x2,x0
	movl	w0,0x00202C34
	bl	ga_mix
	mov	w1,w0
	mov	x2,256
	mov	x0,x20
	bl	ga_put
	orr	x0,x25,1
	strb	w0,[x21]
	b	.n
.wall:	mul	x0,x24,x24			; le cuivre : un cylindre eclaire d'en haut a gauche
	mov	x1,190
	mul	x0,x0,x1
	mul	x1,x26,x26
	udiv	x0,x0,x1
	mov	x1,370
	sub	x1,x1,x0
	add	x0,x23,x22
	sub	x0,x0,CELL
	mov	x2,5
	mul	x0,x0,x2
	sub	x1,x1,x0
	cbz	x3,.col
	add	x1,x1,40
.col:	cmp	x1,60
	mov	x0,60
	csel	x1,x0,x1,lo
	movl	w0,0x00C87C44
	bl	ga_shade
	mov	w1,w0
	add	x2,x26,8			; le bord, adouci
	sub	x2,x2,x24
	lsl	x2,x2,4
	mov	x0,x20
	bl	ga_put
.n:	add	x20,x20,4
	add	x21,x21,1
	add	x23,x23,1
	cmp	x23,CELL
	b.lo	.x
	add	x22,x22,1
	cmp	x22,CELL
	b.lo	.y
	ga_pop
	epilogue

gen_sprites:
	prologue
	ga_push
	mov	x19,1
.p:	cmp	x19,8
	b.eq	.pn
	mov	x0,x19
	bl	gen_pipe
.pn:	add	x19,x19,1
	cmp	x19,NMAP
	b.lo	.p
	add	x19,x28,g_pal			; l'eau : une onde entre deux bleus
	mov	x20,0
.w:	lsl	x0,x20,2
	bl	ga_sin8
	mov	x2,x0
	movl	w0,0x001C74E0
	movl	w1,0x0068D0FF
	bl	ga_mix
	str	w0,[x19,x20,lsl 2]
	add	x20,x20,1
	cmp	x20,256
	b.lo	.w
	mov	x0,CELL-8			; un mur de briques
	mov	x1,CELL-8
	mov	x2,5
	movl	w3,0x00A85C40
	bl	ga_block
	str	x0,[x28,g_swall]
	add	x0,x0,8
	mov	x1,0				; y
.by:	mov	x2,0				; x
.bx:	and	x3,x1,7			; (un joint toutes les 8 lignes, decale d'un rang a l'autre)
	mov	x4,x2
	tst	x1,8
	b.eq	.bo
	add	x4,x4,11
.bo:	and	x4,x4,15
	cmp	x4,1
	b.ls	.bj
	cbnz	x3,.bn
.bj:	mov	x4,CELL-8
	madd	x4,x1,x4,x2
	ldr	w5,[x0,x4,lsl 2]
	lsr	w6,w5,24
	cbz	w6,.bn
	movl	w5,0xFF482820
	str	w5,[x0,x4,lsl 2]
.bn:	add	x2,x2,1
	cmp	x2,CELL-8
	b.lo	.bx
	add	x1,x1,1
	cmp	x1,CELL-8
	b.lo	.by
	mov	x0,28				; le volant du depart
	movl	w1,0x00E04030
	bl	ga_ball
	str	x0,[x28,g_svalve]
	mov	x0,CELL				; le curseur : un cadre arrondi
	mov	x1,CELL
	bl	ga_new
	str	x0,[x28,g_scur]
	add	x19,x0,8
	mov	x20,0
.cy:	mov	x21,0
.cx:	mov	x0,x21
	mov	x1,x20
	mov	x2,CELL
	mov	x3,CELL
	mov	x4,10
	bl	ga_cover
	mov	x22,x0
	mov	x0,0
	sub	x1,x21,3
	cmp	x1,CELL-6
	b.hs	.ci
	sub	x1,x20,3
	cmp	x1,CELL-6
	b.hs	.ci
	sub	x0,x21,3
	sub	x1,x20,3
	mov	x2,CELL-6
	mov	x3,CELL-6
	mov	x4,7
	bl	ga_cover
.ci:	subs	x2,x22,x0
	b.ls	.cn
	mov	x0,x19
	movl	w1,0x00FFE860
	bl	ga_put
.cn:	add	x19,x19,4
	add	x21,x21,1
	cmp	x21,CELL
	b.lo	.cx
	add	x20,x20,1
	cmp	x20,CELL
	b.lo	.cy
	mov	x0,CELL+16
	movl	w1,0x00606058
	bl	ga_disc
	str	x0,[x28,g_spiece]
	mov	x0,170
	movl	w1,0x00102428
	bl	ga_disc
	str	x0,[x28,g_sorb]
	mov	x0,140
	movl	w1,0x00141C30
	bl	ga_disc
	str	x0,[x28,g_sorb+8]
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	add	x0,x28,g_dbig
	mov	x1,20
	adr	x2,ui_font_b
	movl	w3,0x00FFFFFF
	movl	w4,0x00B8E0F0
	bl	ga_digits
	add	x0,x28,g_dsmall
	mov	x1,15
	adr	x2,ui_font_b
	movl	w3,0x00FFFFFF
	movl	w4,0x00C8D8E0
	bl	ga_digits
	adr	x0,tx_table
	adr	x1,s_base
	add	x2,x28,g_tx
	mov	x3,NTX
	bl	ga_texts
	bl	ui_canvas_end
	ga_pop
	epilogue

; ---------------------------------------------------------------------------
;  La couche
; ---------------------------------------------------------------------------
build_layer:
	prologue
	ga_push
	str	xzr,[x28,g_ldirty]
	movl	w0,0x00123038
	movl	w1,0x00081820
	movl	w2,0x00206068
	mov	x3,0
	bl	ga_sky
	ldv	x0,gf_layer
	stv	x0,gf_dst
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.eq	.done
	; le plateau : un cadre, des dalles
	mov	x0,PX0-6
	mov	x1,PY0-6
	mov	x2,GW*CELL+12
	mov	x3,GH*CELL+12
	mov	x4,8
	movl	w5,0x00687C84
	movl	w6,0x00303C44
	mov	x7,256
	movl	w8,0x00A0B8C0
	bl	ga_panel
	mov	x19,0
.ty:	mov	x20,0
.tx:	mov	x0,CELL
	mul	x1,x19,x0
	add	x1,x1,PY0
	mul	x0,x20,x0
	add	x0,x0,PX0
	mov	x2,CELL-1
	mov	x3,CELL-1
	mov	x4,3
	add	x5,x19,x20
	movl	w6,0x00384850
	movl	w7,0x00303F48
	tst	x5,1
	csel	w5,w6,w7,eq
	movl	w6,0x00283438
	mov	x7,256
	mov	w8,0
	bl	ga_panel
	add	x20,x20,1
	cmp	x20,GW
	b.lo	.tx
	add	x19,x19,1
	cmp	x19,GH
	b.lo	.ty
	mov	x0,PX0-6
	mov	x1,PY0-6
	mov	x2,GW*CELL+12
	mov	x3,GH*CELL+12
	bl	ga_mark
	; la file
	mov	x0,QX-4
	mov	x1,PY0-6
	mov	x2,CELL+8
	mov	x3,NQ*CELL+12
	mov	x4,8
	movl	w5,0x00687C84
	movl	w6,0x00303C44
	mov	x7,256
	movl	w8,0x00A0B8C0
	bl	ga_panel
	mov	x19,0
.q:	mov	x0,QX
	mov	x1,CELL
	mul	x1,x19,x1
	add	x1,x1,PY0
	mov	x2,CELL
	sub	x2,x2,1
	mov	x3,x2
	mov	x4,4
	movl	w5,0x00203038
	movl	w6,0x00182428
	cmp	x19,NQ-1
	b.ne	.qc
	movl	w5,0x00486878
	movl	w6,0x00385060
.qc:	mov	x7,256
	mov	w8,0
	bl	ga_panel
	add	x19,x19,1
	cmp	x19,NQ
	b.lo	.q
	mov	x0,QX-4
	mov	x1,PY0-6
	mov	x2,CELL+8
	mov	x3,NQ*CELL+12
	bl	ga_mark
	; les boutons
	adr	x19,zones
.z:	ldrh	w0,[x19,8]
	cbz	w0,.zd
	mov	x0,x19
	mov	x1,8
	movl	w2,0x00385060
	mov	x3,220
	movl	w4,0x0090A8B8
	bl	ga_zone_panel
	add	x19,x19,10
	b	.z
.zd:	ldv	x0,gf_cv
	bl	ui_canvas_begin
	adr	x0,s_fast
	mov	x1,QX+CELL/2-2
	mov	x2,371
	movl	w3,0x00E8F0F4
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text_c
	adr	x0,s_pausel
	mov	x1,QX+CELL/2-2
	mov	x2,421
	movl	w3,0x00E8F0F4
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text_c
	adr	x0,s_score
	mov	x1,QX
	mov	x2,28
	movl	w3,0x00B8C8D0
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	adr	x0,s_round
	mov	x1,194
	mov	x2,28
	movl	w3,0x00B8C8D0
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	adr	x0,s_pipes
	mov	x1,316
	mov	x2,28
	movl	w3,0x00B8C8D0
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	bl	ui_canvas_end
.done:	adr	x0,gf_frame
	stv	x0,gf_dst
	ga_pop
	epilogue

; ---------------------------------------------------------------------------
;  Les regles d'Onyx
; ---------------------------------------------------------------------------
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

; x0 = cote -> x0 = dx, x1 = dy
dir_xy:
	mov	x1,0
	cmp	x0,D_N
	b.ne	.s
	mov	x1,-1
	mov	x0,0
	ret
.s:	cmp	x0,D_S
	b.ne	.e
	mov	x1,1
	mov	x0,0
	ret
.e:	cmp	x0,D_E
	mov	x0,1
	mov	x2,-1
	csel	x0,x0,x2,eq
	ret

; x0 = cote -> x0 = cote oppose
opposite:
	cmp	x0,D_N
	b.ne	.s
	mov	x0,D_S
	ret
.s:	cmp	x0,D_S
	b.ne	.e
	mov	x0,D_N
	ret
.e:	cmp	x0,D_E
	mov	x0,D_W
	mov	x1,D_E
	csel	x0,x0,x1,eq
	ret

rand_piece:
	prologue
	mov	x0,100
	bl	gf_below
	cmp	x0,8
	b.hs	.o
	mov	x0,7
	b	.out
.o:	mov	x0,6
	bl	gf_below
	add	x0,x0,1
.out:	epilogue

new_game:
	prologue
	str	xzr,[x28,g_level]
	str	xzr,[x28,g_score]
	str	xzr,[x28,g_show]
	str	xzr,[x28,g_newbest]
	bl	new_round
	mov	x0,1
	str	x0,[x28,g_ldirty]
	epilogue

new_round:
	prologue
	ga_push
	bl	ga_parts_clear
	add	x0,x28,g_type
	mov	x1,NCELLS*4+NCELLS*4
.z:	strb	wzr,[x0],1
	subs	x1,x1,1
	b.ne	.z
	mov	x0,GW-2				; le depart, loin des bords, sa sortie vers l'interieur
	bl	gf_below
	add	x19,x0,1
	mov	x0,GH-2
	bl	gf_below
	add	x20,x0,1
.d:	mov	x0,4
	bl	gf_below
	adr	x1,dirs
	ldrb	w21,[x1,x0]
	mov	x0,x21
	bl	dir_xy
	add	x22,x19,x0			; la case de sortie
	add	x23,x20,x1
	sub	x0,x22,1
	cmp	x0,GW-2
	b.hs	.d
	sub	x0,x23,1
	cmp	x0,GH-2
	b.hs	.d
	mov	x2,GW
	madd	x0,x20,x2,x19
	add	x1,x28,g_type
	mov	w2,P_START
	strb	w2,[x1,x0]
	add	x1,x28,g_dir
	strb	w21,[x1,x0]
	ldr	x24,[x28,g_level]		; des murs, a partir de la manche 3
	mov	x25,0
	cmp	x24,2
	b.lo	.nw
	sub	x25,x24,1
	cmp	x25,6
	mov	x0,6
	csel	x25,x0,x25,hi
.nw:	cbz	x25,.q
	mov	x0,GW
	bl	gf_below
	mov	x26,x0
	mov	x0,GH
	bl	gf_below
	cmp	x26,x22
	b.ne	.wc
	cmp	x0,x23
	b.eq	.nw
.wc:	mov	x2,GW
	madd	x0,x0,x2,x26
	add	x1,x28,g_type
	ldrb	w2,[x1,x0]
	cbnz	w2,.nw
	mov	w2,P_WALL
	strb	w2,[x1,x0]
	sub	x25,x25,1
	b	.nw
.q:	mov	x25,0
.ql:	bl	rand_piece
	add	x1,x28,g_q
	str	x0,[x1,x25,lsl 3]
	add	x25,x25,1
	cmp	x25,NQ
	b.lo	.ql
	str	x22,[x28,g_cx]
	str	x23,[x28,g_cy]
	mov	x0,2				; 10 + 2 par manche, 30 au plus
	mul	x0,x24,x0
	add	x0,x0,10
	cmp	x0,30
	mov	x1,30
	csel	x0,x1,x0,hi
	str	x0,[x28,g_need]
	str	xzr,[x28,g_done]
	movl	x0,1500				; 22 s - 1,5 s par manche, 8 s au moins
	mul	x0,x24,x0
	movl	x1,22000
	subs	x0,x1,x0
	movl	x1,8000
	csel	x0,x1,x0,lo
	cmp	x0,x1
	csel	x0,x1,x0,lo
	str	x0,[x28,g_count]
	str	x0,[x28,g_total]
	mov	x0,250				; 3,2 s - 0,25 s par manche, 1 s au moins
	mul	x0,x24,x0
	movl	x1,3200
	subs	x0,x1,x0
	mov	x1,1000
	csel	x0,x1,x0,lo
	cmp	x0,x1
	csel	x0,x1,x0,lo
	str	x0,[x28,g_flow]
	str	xzr,[x28,g_fast]
	str	x19,[x28,g_wx]
	str	x20,[x28,g_wy]
	str	xzr,[x28,g_wside]
	str	xzr,[x28,g_bomb]
	str	xzr,[x28,g_place]
	mov	x0,ST_BUILD
	bl	set_state
	ga_pop
	epilogue

keep_best:
	prologue
	ldr	x2,[x28,g_score]
	ldr	x0,[x28,g_best]
	cmp	x2,x0
	b.le	.out
	str	x2,[x28,g_best]
	mov	x0,1
	str	x0,[x28,g_newbest]
	adr	x0,p_dir
	adr	x1,p_best
	bl	gf_save_best
.out:	epilogue

; x0 = x, x1 = y : poser la piece suivante
place:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x2,[x28,g_state]
	cmp	x2,ST_FLOW
	b.hi	.out
	mov	x2,GW
	madd	x19,x1,x2,x0
	add	x2,x28,g_type
	ldrb	w20,[x2,x19]
	cmp	w20,P_START
	b.hs	.no
	add	x2,x28,g_fh
	ldrh	w3,[x2,x19,lsl 1]
	cbnz	w3,.no
	add	x2,x28,g_fv
	ldrh	w3,[x2,x19,lsl 1]
	cbnz	w3,.no
	cbz	w20,.free
	ldr	x0,[x28,g_score]		; une piece remplacee : -50
	sub	x0,x0,50
	str	x0,[x28,g_score]
	mov	x0,18
	str	x0,[x28,g_bomb]
	str	x19,[x28,g_bombc]
	mov	x0,GW
	udiv	x1,x19,x0
	msub	x0,x1,x0,x19
	mov	x2,CELL
	mul	x0,x0,x2
	add	x0,x0,PX0+CELL/2
	mul	x1,x1,x2
	add	x1,x1,PY0+CELL/2
	movl	w2,0x00E89048
	mov	x3,26
	movl	x4,220000
	mov	x5,18
	mov	x6,3
	movl	x7,8000
	bl	ga_burst
	mov	x0,90
	mov	x1,160
	mov	x2,AKW_NOISE
	mov	x3,120
	bl	gs_note
	b	.put
.free:	mov	x0,700
	mov	x1,25
	mov	x2,AKW_TRIANGLE
	mov	x3,100
	bl	gs_note
.put:	add	x2,x28,g_q
	ldr	x0,[x2,(NQ-1)*8]
	add	x3,x28,g_type
	strb	w0,[x3,x19]
	mov	x1,NQ-1
.s:	sub	x3,x1,1
	ldr	x4,[x2,x3,lsl 3]
	str	x4,[x2,x1,lsl 3]
	mov	x1,x3
	cbnz	x1,.s
	bl	rand_piece
	str	x0,[x28,g_q]
	mov	x0,12
	str	x0,[x28,g_place]
	str	x19,[x28,g_placec]
	b	.out
.no:	mov	x0,140
	mov	x1,60
	mov	x2,AKW_SQUARE
	mov	x3,70
	bl	gs_note
.out:	ldp	x19,x20,[sp],16
	epilogue

; l'eau deborde en (x0, x1) : (la case d'ou elle sort)
spill:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x2,CELL
	mul	x0,x19,x2
	add	x0,x0,PX0+CELL/2
	mul	x1,x20,x2
	add	x1,x1,PY0+CELL/2
	movl	w2,0x0060C0FF
	mov	x3,60
	movl	x4,260000
	mov	x5,26
	mov	x6,3
	movl	x7,14000
	bl	ga_burst
	ldr	x0,[x28,g_done]
	ldr	x1,[x28,g_need]
	cmp	x0,x1
	b.lo	.lost
	ldr	x0,[x28,g_level]		; 1000 + 250 par manche
	mov	x1,250
	mul	x0,x0,x1
	add	x0,x0,1000
	ldr	x1,[x28,g_score]
	add	x0,x0,x1
	str	x0,[x28,g_score]
	mov	x0,ST_WON
	bl	set_state
	adr	x0,gs_tab_win
	bl	gs_seq
	b	.end
.lost:	mov	x0,ST_LOST
	bl	set_state
	adr	x0,gs_tab_lose
	bl	gs_seq
.end:	bl	keep_best
	ldp	x19,x20,[sp],16
	epilogue

; (wx, wy) est plein : l'eau passe a la case suivante
advance_water:
	prologue
	ga_push
	ldr	x19,[x28,g_wx]
	ldr	x20,[x28,g_wy]
	mov	x0,GW
	madd	x21,x20,x0,x19
	add	x0,x28,g_type
	ldrb	w22,[x0,x21]
	ldr	x23,[x28,g_wside]
	cmp	x22,P_START
	b.ne	.a
	add	x0,x28,g_dir
	ldrb	w24,[x0,x21]
	b	.go
.a:	cmp	x22,7
	b.ne	.b
	mov	x0,x23
	bl	opposite
	mov	x24,x0
	b	.go
.b:	adr	x0,conn
	ldrb	w24,[x0,x22]
	bic	x24,x24,x23
.go:	mov	x0,x24				; la sortie
	bl	dir_xy
	add	x25,x19,x0			; nx
	add	x26,x20,x1			; ny
	cmp	x25,GW
	b.hs	.spill
	cmp	x26,GH
	b.hs	.spill
	mov	x0,x24
	bl	opposite
	mov	x24,x0				; l'entree
	mov	x0,GW
	madd	x21,x26,x0,x25
	add	x0,x28,g_type
	ldrb	w22,[x0,x21]
	cbz	w22,.spill
	cmp	w22,P_START
	b.hs	.spill
	adr	x0,conn
	ldrb	w0,[x0,x22]
	tst	x0,x24
	b.eq	.spill
	mov	x1,D_E or D_W
	and	x1,x1,x24
	add	x2,x28,g_fh
	cmp	x22,7
	b.ne	.chk
	cbnz	x1,.chk
	add	x2,x28,g_fv
.chk:	ldrh	w0,[x2,x21,lsl 1]
	cbnz	w0,.spill
	str	x25,[x28,g_wx]
	str	x26,[x28,g_wy]
	str	x24,[x28,g_wside]
	add	x2,x28,g_inh
	cmp	x22,7
	b.ne	.in
	cbnz	x1,.in
	add	x2,x28,g_inv
.in:	strb	w24,[x2,x21]
	b	.out
.spill:	mov	x0,x19
	mov	x1,x20
	bl	spill
.out:	ga_pop
	epilogue

; x0 = ms depuis l'image d'avant
flow_step:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x28,g_wx]
	ldr	x1,[x28,g_wy]
	mov	x2,GW
	madd	x20,x1,x2,x0
	add	x0,x28,g_type
	ldrb	w21,[x0,x20]
	add	x22,x28,g_fh
	cmp	x21,7
	b.ne	.h
	ldr	x0,[x28,g_wside]
	mov	x1,D_N or D_S
	and	x0,x0,x1
	cbz	x0,.h
	add	x22,x28,g_fv
.h:	ldr	x0,[x28,g_flow]
	ldr	x1,[x28,g_fast]
	cbz	x1,.m
	mov	x0,160
.m:	cmp	x21,P_START
	b.ne	.f
	lsr	x0,x0,1
.f:	mov	x1,1000
	mul	x1,x19,x1
	udiv	x1,x1,x0
	ldrh	w2,[x22,x20,lsl 1]
	add	x2,x2,x1
	cmp	x2,1000
	b.lo	.keep
	mov	x2,1000
	strh	w2,[x22,x20,lsl 1]
	cmp	x21,P_START
	b.eq	.adv
	ldr	x0,[x28,g_done]
	add	x0,x0,1
	str	x0,[x28,g_done]
	ldr	x1,[x28,g_fast]
	mov	x2,50
	mov	x3,100
	cmp	x1,0
	csel	x2,x3,x2,ne
	ldr	x1,[x28,g_score]
	add	x1,x1,x2
	str	x1,[x28,g_score]
	cmp	x21,7
	b.ne	.son
	add	x1,x28,g_fh
	ldrh	w2,[x1,x20,lsl 1]
	cmp	w2,1000
	b.ne	.son
	add	x1,x28,g_fv
	ldrh	w2,[x1,x20,lsl 1]
	cmp	w2,1000
	b.ne	.son
	ldr	x1,[x28,g_score]		; la croix prise dans les deux sens
	add	x1,x1,500
	str	x1,[x28,g_score]
	mov	x0,1320
	mov	x1,80
	mov	x2,AKW_SQUARE
	mov	x3,100
	bl	gs_note
	b	.adv
.son:	and	x0,x0,7				; (done % 8)
	mov	x1,60
	mul	x0,x0,x1
	add	x0,x0,400
	mov	x1,30
	mov	x2,AKW_TRIANGLE
	mov	x3,80
	bl	gs_note
.adv:	bl	advance_water
	b	.out
.keep:	strh	w2,[x22,x20,lsl 1]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

step:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x0,[x28,g_tick]
	add	x0,x0,1
	str	x0,[x28,g_tick]
	bl	gs_tick
	bl	ga_parts_step
	ldr	x0,[x28,g_timer]
	add	x0,x0,1
	str	x0,[x28,g_timer]
	ldr	x0,[x28,g_show]
	ldr	x1,[x28,g_score]
	subs	x2,x1,x0
	b.eq	.b
	asr	x3,x2,3
	cbnz	x3,.s
	mov	x3,x2
.s:	add	x0,x0,x3
	str	x0,[x28,g_show]
.b:	ldr	x0,[x28,g_bomb]
	cbz	x0,.pl
	sub	x0,x0,1
	str	x0,[x28,g_bomb]
.pl:	ldr	x0,[x28,g_place]
	cbz	x0,.st
	sub	x0,x0,1
	str	x0,[x28,g_place]
.st:	ldr	x0,[x28,g_tick]			; 16,67 ms : 17, 17, 16
	mov	x1,3
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	mov	x19,17
	cbnz	x0,.d
	mov	x19,16
.d:	ldr	x0,[x28,g_state]
	cmp	x0,ST_BUILD
	b.ne	.fl
	ldr	x1,[x28,g_fast]
	cbnz	x1,.gow
	ldr	x1,[x28,g_count]
	cmp	x19,x1
	b.hs	.gow
	sub	x1,x1,x19
	str	x1,[x28,g_count]
	movl	x2,5000				; les bips des 5 dernieres secondes
	cmp	x1,x2
	b.hs	.out
	mov	x2,1000
	udiv	x3,x1,x2
	msub	x3,x3,x2,x1
	cmp	x3,x19
	b.hs	.out
	mov	x0,1000
	mov	x1,30
	mov	x2,AKW_SQUARE
	mov	x3,60
	bl	gs_note
	b	.out
.gow:	str	xzr,[x28,g_count]
	mov	x0,ST_FLOW
	bl	set_state
	mov	x0,220
	mov	x1,200
	mov	x2,AKW_NOISE
	mov	x3,60
	bl	gs_note
	b	.out
.fl:	cmp	x0,ST_FLOW
	b.ne	.out
	mov	x0,x19
	bl	flow_step
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Evenements
; ---------------------------------------------------------------------------
do_pause:
	b	gp_open

; (gamepause) la manche s'arrete -> x0 = 1 si elle etait en cours
game_pause:
	prologue
	ldr	x1,[x28,g_state]
	mov	x0,0
	cmp	x1,ST_FLOW			; (construire ou l'eau qui coule)
	b.hi	.out
	str	x1,[x28,g_prev]
	mov	x0,ST_PAUSE
	bl	set_state
	mov	x0,1
.out:	epilogue

; (gamepause) la manche repart
game_resume:
	prologue
	ldr	x1,[x28,g_state]
	cmp	x1,ST_PAUSE
	b.ne	.out
	ldr	x0,[x28,g_prev]
	bl	set_state
.out:	epilogue

do_fast:
	ldr	x0,[x28,g_state]
	cmp	x0,ST_FLOW
	b.hi	.out
	mov	x0,1
	str	x0,[x28,g_fast]
.out:	ret

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

; -> x0 = 1 si l'appui a servi (titre, fin de manche, pause)
start_act:
	prologue
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.eq	.new
	cmp	x0,ST_PAUSE
	b.ne	.w
	bl	do_pause
	b	.yes
.w:	cmp	x0,ST_WON
	b.ne	.l
	ldr	x1,[x28,g_timer]
	cmp	x1,30
	b.lo	.yes
	ldr	x1,[x28,g_level]
	add	x1,x1,1
	str	x1,[x28,g_level]
	bl	new_round
	b	.yes
.l:	cmp	x0,ST_LOST
	b.ne	.no
	ldr	x1,[x28,g_timer]
	cmp	x1,30
	b.lo	.yes
.new:	bl	new_game
.yes:	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	epilogue

; x0, x1 = decalage du curseur
cursor:
	ldr	x2,[x28,g_cx]
	ldr	x3,[x28,g_cy]
	add	x2,x2,x0
	add	x3,x3,x1
	cmp	x2,0
	csel	x2,xzr,x2,lt
	mov	x4,GW-1
	cmp	x2,x4
	csel	x2,x4,x2,gt
	cmp	x3,0
	csel	x3,xzr,x3,lt
	mov	x4,GH-1
	cmp	x3,x4
	csel	x3,x4,x3,gt
	str	x2,[x28,g_cx]
	str	x3,[x28,g_cy]
	ret

place_cursor:
	prologue
	ldr	x0,[x28,g_cx]
	ldr	x1,[x28,g_cy]
	bl	place
	epilogue

on_key:
	prologue
	adr	x1,ui_ev
	ldr	w2,[x1,EV_KIND]
	cbz	w2,.out
	mov	x0,0
	bl	ui_menu_key
	tbnz	x0,63,.key
	bl	do_menu
	b	.out
.key:	adr	x1,ui_ev
	ldr	w0,[x1,EV_CHAR]
	orr	w0,w0,0x20
	cmp	w0,'p'
	b.ne	.f
	bl	do_pause
	b	.out
.f:	cmp	w0,'f'
	b.ne	.c
	bl	do_fast
	b	.out
.c:	ldr	w2,[x1,EV_CODE]
	cmp	w2,KEY_SPACE
	b.eq	.sp
	cmp	w2,KEY_ENTER
	b.ne	.l
.sp:	bl	start_act
	cbnz	x0,.out
	bl	place_cursor
	b	.out
.l:	mov	x0,0
	mov	x1,0
	cmp	w2,KEY_LEFT
	b.ne	.r
	mov	x0,-1
	b	.mv
.r:	cmp	w2,KEY_RIGHT
	b.ne	.u
	mov	x0,1
	b	.mv
.u:	cmp	w2,KEY_UP
	b.ne	.d
	mov	x1,-1
	b	.mv
.d:	cmp	w2,KEY_DOWN
	b.ne	.out
	mov	x1,1
.mv:	bl	cursor
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
	b.ne	.hov
	ldr	w3,[x2,EV_CODE]
	tbz	w3,0,.out
	mov	x0,PY0-14
	bl	gf_drag_top
	cbnz	x0,.out
	bl	start_act
	cbnz	x0,.out
	adr	x0,zones
	mov	x1,x19
	mov	x2,x20
	bl	ga_zone
	cbz	x0,.brd
	str	x0,[x28,g_btn]
	cmp	x0,Z_FAST
	b.ne	.zp
	bl	do_fast
	b	.out
.zp:	bl	do_pause
	b	.out
.brd:	bl	cell_at
	cbz	x0,.out
	str	x1,[x28,g_cx]
	str	x2,[x28,g_cy]
	mov	x0,x1
	mov	x1,x2
	bl	place
	b	.out
.hov:	bl	cell_at
	cbz	x0,.out
	str	x1,[x28,g_cx]
	str	x2,[x28,g_cy]
	b	.out
.rel:	str	xzr,[x28,g_btn]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x19, x20 = point -> x0 = 1 si sur le plateau, x1, x2 = case
cell_at:
	sub	x1,x19,PX0
	sub	x2,x20,PY0
	mov	x3,CELL
	cmp	x1,GW*CELL
	b.hs	.no
	cmp	x2,GH*CELL
	b.hs	.no
	udiv	x1,x1,x3
	udiv	x2,x2,x3
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

on_pad:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,ui_ev
	ldr	w1,[x0,4]
	cbz	w1,.out
	ldv	x20,gp_new			; (Start : gamepause)
	tst	x20,PAD_A
	b.eq	.x
	bl	start_act
	cbnz	x0,.out
	bl	place_cursor
.x:	tst	x20,PAD_X
	b.eq	.l
	bl	do_fast
.l:	tst	x20,PAD_LEFT
	b.eq	.r
	mov	x0,-1
	mov	x1,0
	bl	cursor
.r:	tst	x20,PAD_RIGHT
	b.eq	.u
	mov	x0,1
	mov	x1,0
	bl	cursor
.u:	tst	x20,PAD_UP
	b.eq	.d
	mov	x0,0
	mov	x1,-1
	bl	cursor
.d:	tst	x20,PAD_DOWN
	b.eq	.out
	mov	x0,0
	mov	x1,1
	bl	cursor
.out:	ldp	x19,x20,[sp],16
	epilogue

on_event:
	prologue
	str	x0,[sp,-16]!
	bl	gp_event
	mov	x1,x0
	ldr	x0,[sp],16
	cbnz	x1,.out
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
	b.ne	.out
	bl	on_pad
.out:	epilogue

; ---------------------------------------------------------------------------
;  Le dessin
; ---------------------------------------------------------------------------

; l'eau dans une carte : x0 = carte, x1 = x, x2 = y, x3 = remplissage 0..1000, x4 = 1 : a
; rebours
draw_water:
	mov	x5,CELL*CELL
	mul	x5,x0,x5
	adr	x6,g_maps
	add	x5,x5,x6
	mov	x6,255
	mul	x3,x3,x6
	mov	x6,1000
	udiv	x3,x3,x6			; ou elle en est
	adr	x6,gf_dst
	ldr	x6,[x6]
	mov	x7,GF_PITCH
	madd	x6,x2,x7,x6
	add	x6,x6,x1,lsl 2
	add	x8,x28,g_pal
	ldr	x9,[x28,g_tick]
	lsl	x9,x9,2
	mov	x10,0				; y
.y:	mov	x11,0
.x:	ldrb	w12,[x5],1
	cbz	w12,.n
	mov	x13,x12
	cbz	x4,.d
	mov	x13,256
	sub	x13,x13,x12
.d:	cmp	x13,x3
	b.hi	.n
	add	x12,x13,x13
	add	x12,x12,x10,lsl 1
	sub	x12,x12,x9
	and	x12,x12,255
	ldr	w12,[x8,x12,lsl 2]
	sub	x14,x3,x13			; le front, plus clair
	cmp	x14,10
	b.hs	.p
	orr	w12,w12,0xC0C0C0C0
.p:	str	w12,[x6,x11,lsl 2]
.n:	add	x11,x11,1
	cmp	x11,CELL
	b.lo	.x
	add	x6,x6,x7
	add	x10,x10,1
	cmp	x10,CELL
	b.lo	.y
	ret

; x0 = piece, x1 = cote d'entree -> x0 = 1 si l'eau va a rebours de la carte
reversed:
	adr	x2,first
	ldrb	w2,[x2,x0]
	cmp	x1,x2
	cset	x0,ne
	ret

; x0 = case : son eau
tile_water:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x1,GW
	udiv	x2,x19,x1
	msub	x0,x2,x1,x19
	mov	x1,CELL
	mul	x20,x0,x1
	add	x20,x20,PX0
	mul	x21,x2,x1
	add	x21,x21,PY0
	add	x0,x28,g_type
	ldrb	w22,[x0,x19]
	cmp	x22,P_START
	b.ne	.pipe
	add	x0,x28,g_fh
	ldrh	w3,[x0,x19,lsl 1]
	cbz	w3,.out
	add	x0,x28,g_dir
	ldrb	w0,[x0,x19]
	rbit	w0,w0
	clz	w0,w0				; rang du cote : 0 N, 1 E, 2 S, 3 O
	add	x0,x0,9
	mov	x1,x20
	mov	x2,x21
	mov	x4,0
	bl	draw_water
	b	.out
.pipe:	add	x0,x28,g_fh
	ldrh	w3,[x0,x19,lsl 1]
	cbz	w3,.v
	mov	x0,x22
	cmp	x22,7
	b.ne	.h
	mov	x0,1
.h:	add	x1,x28,g_inh
	ldrb	w1,[x1,x19]
	mov	x4,x0
	stp	x0,x3,[sp,-16]!
	mov	x0,x4
	bl	reversed
	mov	x4,x0
	ldp	x0,x3,[sp],16
	mov	x1,x20
	mov	x2,x21
	bl	draw_water
.v:	cmp	x22,7
	b.ne	.out
	add	x0,x28,g_fv
	ldrh	w3,[x0,x19,lsl 1]
	cbz	w3,.out
	add	x1,x28,g_inv
	ldrb	w1,[x1,x19]
	mov	x0,2
	stp	x3,xzr,[sp,-16]!
	bl	reversed
	mov	x4,x0
	ldp	x3,x5,[sp],16
	mov	x0,2
	mov	x1,x20
	mov	x2,x21
	bl	draw_water
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

draw_board:
	prologue
	ga_push
	mov	x19,0				; case
	add	x20,x28,g_type
.c:	ldrb	w21,[x20,x19]
	cbz	w21,.n
	mov	x1,GW
	udiv	x2,x19,x1
	msub	x0,x2,x1,x19
	mov	x1,CELL
	mul	x22,x0,x1
	add	x22,x22,PX0
	mul	x23,x2,x1
	add	x23,x23,PY0
	cmp	x21,P_WALL
	b.ne	.st
	add	x0,x22,4
	add	x1,x23,4
	ldr	x2,[x28,g_swall]
	bl	ga_ablit
	b	.n
.st:	cmp	x21,P_START
	b.ne	.p
	add	x0,x28,g_dir
	ldrb	w0,[x0,x19]
	rbit	w0,w0
	clz	w0,w0
	add	x0,x0,9
	add	x1,x28,g_sp
	ldr	x2,[x1,x0,lsl 3]
	mov	x0,x22
	mov	x1,x23
	bl	ga_ablit
	mov	x0,x19
	bl	tile_water
	add	x0,x22,13			; le volant
	add	x1,x23,13
	ldr	x2,[x28,g_svalve]
	bl	ga_ablit
	b	.n
.p:	add	x0,x28,g_sp
	ldr	x2,[x0,x21,lsl 3]
	mov	x0,x22
	mov	x1,x23
	bl	ga_ablit
	mov	x0,x19
	bl	tile_water
.n:	add	x19,x19,1
	cmp	x19,NCELLS
	b.lo	.c
	ldr	x0,[x28,g_place]		; la piece posee luit un instant
	cbz	x0,.b
	ldr	x0,[x28,g_placec]
	mov	x1,GW
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	mov	x3,CELL
	mul	x0,x0,x3
	add	x0,x0,PX0-8
	mul	x1,x2,x3
	add	x1,x1,PY0-8
	ldr	x2,[x28,g_spiece]
	bl	gf_glow
.b:	ldr	x0,[x28,g_bomb]
	cbz	x0,.out
	ldr	x0,[x28,g_bombc]
	mov	x1,GW
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	mov	x3,CELL
	mul	x0,x0,x3
	add	x0,x0,PX0-8
	mul	x1,x2,x3
	add	x1,x1,PY0-8
	ldr	x2,[x28,g_spiece]
	bl	gf_glow
.out:	ga_pop
	epilogue

draw_side:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0				; la file
.q:	add	x0,x28,g_q
	ldr	x0,[x0,x19,lsl 3]
	add	x1,x28,g_sp
	ldr	x2,[x1,x0,lsl 3]
	mov	x0,QX
	mov	x1,CELL
	mul	x1,x19,x1
	add	x1,x1,PY0
	bl	ga_ablit
	add	x19,x19,1
	cmp	x19,NQ
	b.lo	.q
	ldr	x0,[x28,g_state]		; le temps : l'eau monte dans un tube
	cbnz	x0,.sc
	ldr	x0,[x28,g_count]
	mov	x1,GH*CELL
	mul	x0,x0,x1
	ldr	x1,[x28,g_total]
	udiv	x19,x0,x1
	cbz	x19,.sc
	mov	x0,PX0-17
	mov	x1,PY0+GH*CELL
	sub	x1,x1,x19
	mov	x2,8
	mov	x3,x19
	mov	x4,3
	movl	w5,0x0070C8FF
	movl	w6,0x002C80E0
	mov	x7,256
	mov	w8,0
	bl	ga_panel
.sc:	ldr	x0,[x28,g_show]
	cmp	x0,0
	csel	x0,xzr,x0,lt
	mov	x1,62
	mov	x2,10
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x0,[x28,g_level]
	add	x0,x0,1
	mov	x1,258
	mov	x2,10
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x0,[x28,g_done]
	mov	x1,420
	mov	x2,10
	add	x3,x28,g_dbig
	mov	x4,6
	bl	ga_num
	ldr	x2,[x28,g_tx+TX_SLASH*8]
	mov	x0,422
	mov	x1,10
	bl	ga_ablit
	ldr	x0,[x28,g_need]
	mov	x1,434
	mov	x2,10
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x0,[x28,g_btn]
	cbz	x0,.hint
	adr	x1,zones
	sub	x0,x0,1
	mov	x2,10
	madd	x0,x0,x2,x1
	mov	x1,8
	movl	w2,0x0080C0F0
	mov	x3,80
	mov	w4,0
	bl	ga_zone_panel
.hint:	ldr	x0,[x28,g_state]
	cmp	x0,ST_FLOW
	b.hi	.out
	ldr	x1,[x28,g_fast]
	cbnz	x1,.fh
	cbnz	x0,.out
	ldr	x0,[x28,g_tx+TX_HINT*8]
	b	.sh
.fh:	ldr	x0,[x28,g_tx+TX_FAST*8]
.sh:	mov	x1,PX0+GW*CELL/2
	mov	x2,446
	bl	ga_show
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = sprites : une boite au milieu du plateau ; x2 = 1 : le score
box:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,PX0+90
	mov	x1,PY0+90
	mov	x2,GW*CELL-180
	mov	x3,GH*CELL-180
	mov	x4,14
	movl	w5,0x00304858
	movl	w6,0x00182830
	mov	x7,240
	movl	w8,0x0090B8C8
	bl	ga_panel
	mov	x0,x19
	mov	x1,PX0+GW*CELL/2
	mov	x2,PY0+106
	bl	ga_show
	cbz	x21,.a
	ldr	x0,[x28,g_score]
	cmp	x0,0
	csel	x0,xzr,x0,lt
	mov	x1,PX0+GW*CELL/2
	mov	x2,PY0+142
	add	x3,x28,g_dbig
	mov	x4,5
	bl	ga_num
	ldr	x0,[x28,g_newbest]
	cbz	x0,.a
	ldr	x0,[x28,g_tx+TX_NEWBEST*8]
	mov	x1,PX0+GW*CELL/2
	mov	x2,PY0+170
	bl	ga_show
.a:	mov	x0,x20
	mov	x1,PX0+GW*CELL/2
	mov	x2,PY0+196
	bl	ga_show
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

draw_title:
	prologue
	ga_push
	; une conduite qui se remplit, d'un bord a l'autre
	ldr	x0,[x28,g_tick]
	mov	x1,400
	udiv	x2,x0,x1
	msub	x19,x2,x1,x0			; 0..399
	mov	x20,0				; case
.p:	ldr	x2,[x28,g_sp+1*8]
	mov	x0,CELL
	mul	x0,x20,x0
	sub	x0,x0,10
	mov	x1,352
	bl	ga_ablit
	mov	x0,x19
	mov	x1,36
	mul	x1,x20,x1
	subs	x0,x0,x1
	b.lo	.np
	mov	x1,36
	cmp	x0,x1
	csel	x0,x1,x0,hi
	mov	x1,1000
	mul	x3,x0,x1
	mov	x1,36
	udiv	x3,x3,x1
	mov	x0,1
	mov	x1,CELL
	mul	x1,x20,x1
	cbz	x20,.np
	sub	x1,x1,10
	mov	x2,352
	mov	x4,0
	bl	draw_water
.np:	add	x20,x20,1
	cmp	x20,12
	b.lo	.p
	ldr	x0,[x28,g_tx+TX_LOGO*8]
	mov	x1,GF_W/2
	mov	x2,84
	bl	ga_show
	ldr	x0,[x28,g_tx+TX_SUB*8]
	mov	x1,GF_W/2
	mov	x2,182
	bl	ga_show
	ldr	x0,[x28,g_tick]
	lsl	x0,x0,1
	bl	ga_sin8
	cmp	x0,40
	b.lo	.r
	ldr	x0,[x28,g_tx+TX_PROMPT*8]
	mov	x1,GF_W/2
	mov	x2,234
	bl	ga_show
.r:	ldr	x0,[x28,g_tx+TX_RECORD*8]
	mov	x1,GF_W/2-40
	mov	x2,282
	bl	ga_show
	ldr	x0,[x28,g_best]
	mov	x1,GF_W/2+8
	mov	x2,280
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
	ldr	x0,[x28,g_sorb]
	ldr	x1,[x28,g_sorb+8]
	ldr	x2,[x28,g_tick]
	bl	ga_orbs
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.ne	.game
	bl	draw_title
	b	.out
.game:	bl	draw_side
	bl	draw_board
	ldr	x0,[x28,g_cx]			; le curseur
	mov	x2,CELL
	mul	x0,x0,x2
	add	x0,x0,PX0
	ldr	x1,[x28,g_cy]
	mul	x1,x1,x2
	add	x1,x1,PY0
	ldr	x2,[x28,g_scur]
	bl	ga_ablit
	bl	ga_parts_draw
	ldr	x0,[x28,g_state]		; (la pause : gamepause)
	cmp	x0,ST_WON
	b.ne	.l
	ldr	x0,[x28,g_tx+TX_WON*8]
	ldr	x1,[x28,g_tx+TX_NEXT*8]
	mov	x2,1
	bl	box
	b	.out
.l:	cmp	x0,ST_LOST
	b.ne	.out
	ldr	x0,[x28,g_tx+TX_LOST*8]
	ldr	x1,[x28,g_tx+TX_AGAIN*8]
	mov	x2,1
	bl	box
.out:	ga_pop
	epilogue

; ---------------------------------------------------------------------------
;  Tables
; ---------------------------------------------------------------------------
	align 4
; 0 aucun, 1 -, 2 |, 3 N+E, 4 E+S, 5 S+O, 6 O+N, 7 croix
conn	db 0,D_E or D_W,D_N or D_S,D_N or D_E,D_E or D_S,D_S or D_W,D_W or D_N,15,0,0
; le cote par ou commence la carte de chaque piece
first	db 0,D_W,D_N,D_N,D_E,D_S,D_W,0,0,0
dirs	db D_N,D_E,D_S,D_W
	align 4
zones	dh QX-4,338,CELL+8,44,Z_FAST
	dh QX-4,388,CELL+8,44,Z_PAUSE
	dh 0,0,0,0,0
	align 4
tx_table:
	ga_txe	s_logo,s_base,72,1,0x00FFE0B0,0x00D87838
	ga_txe	s_sub,s_base,15,0,0x00D0E0E8,0x00A0B8C0
	ga_txe	s_prompt,s_base,18,1,0x00FFFFFF,0x00C8E8F0
	ga_txe	s_rec,s_base,15,1,0x00B8C8D0,0x0090A8B0
	ga_txe	s_pause,s_base,26,1,0x00FFFFFF,0x00B8E0F0
	ga_txe	s_resume,s_base,13,0,0x00D0E0E8,0x00A0B8C0
	ga_txe	s_won,s_base,24,1,0x00C8FFD0,0x0060D890
	ga_txe	s_nextr,s_base,13,0,0x00D0E0E8,0x00A0B8C0
	ga_txe	s_lost,s_base,24,1,0x00FFD878,0x00F09040
	ga_txe	s_again,s_base,13,0,0x00D0E0E8,0x00A0B8C0
	ga_txe	s_hint,s_base,14,0,0x00E0ECF0,0x00B0C4CC
	ga_txe	s_fasth,s_base,14,1,0x0090E0FF,0x0060B8E8
	ga_txe	s_slash,s_base,20,1,0x00C8D8E0,0x00A0B0B8
	ga_txe	s_newbest,s_base,14,1,0x00FFE080,0x00F0B040
s_base:
s_logo		db 'Pipes',0
s_sub		db 'Des tuyaux avant que l',0x27,'eau n',0x27,'arrive',0
s_prompt	db 'Espace, A ou toucher pour jouer',0
s_rec		db 'Record',0
s_pause		db 'Pause',0
s_resume	db 'P ou toucher pour reprendre',0
s_won		db 'Manche gagn',0xC3,0xA9,'e !',0
s_nextr		db 'Toucher pour la manche suivante',0
s_lost		db 'Ca d',0xC3,0xA9,'borde !',0
s_again		db 'Toucher pour une nouvelle partie',0
s_hint		db 'Posez les tuyaux ! F ou Eau : l',0x27,'eau part tout de suite',0
s_fasth		db 'D',0xC3,0xA9,'bit rapide : points doubl',0xC3,0xA9,'s',0
s_slash		db '/',0
s_newbest	db 'Nouveau record !',0
s_title		db 'Pipes',0
s_score		db 'Score',0
s_round		db 'Manche',0
s_pipes		db 'Tuyaux',0
s_fast		db 'Eau',0
s_pausel	db 'Pause',0
p_dir		db 'SD0:/home/pipes',0
p_best		db 'SD0:/home/pipes/best.txt',0
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
g_maps		rb NMAP*CELL*CELL
gp_data
gf_data
