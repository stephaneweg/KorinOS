; ============================================================================
;  bubbles - un jeu de tir de bulles, original. Un lanceur en bas qu'on oriente ;
;  la bulle tiree rebondit sur les murs et se colle a la grille hexagonale du
;  haut (rangees decalees) ; 3 bulles ou plus de la meme couleur reliees
;  eclatent ; celles qui ne tiennent plus au plafond tombent (points en plus).
;  Le plafond descend d'une rangee tous les N tirs ; une bulle sous la ligne du
;  bas : perdu. Niveaux generes (motifs, plus de couleurs, descente plus rapide).
;
;  Clavier : gauche et droite visent, Espace, Entree ou haut tirent, bas echange
;  la bulle et la suivante ; P : pause ; R : recommencer. Souris ou doigt : viser
;  en touchant ou glissant, tirer au relache ; toucher la suivante l'echange.
;  Manette : croix (tenue, elle tourne), A tire, X ou Y echangent. Plein ecran ;
;  Start, Echap ou le bouton || : le menu de pause (gamepause.inc). 640 x 480.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'
include '../../inc/pad.inc'

GF_W		= 640
GF_H		= 480
COLS		= 10			; bulles d'une rangee paire (9 sur une impaire)
GROWS		= 13
NCELL		= COLS*GROWS
LOSE_ROW	= 12			; rangee (plafond compris) qui fait perdre
BD		= 32			; diametre
BRAD		= 16
ROW_H		= 28
HIT2		= 28*28			; contact
FLD_X		= 160
FLD_W		= COLS*BD
FLD_Y		= 44			; bas du plafond, au depart
CEIL_TOP	= 26
DANGER_Y	= FLD_Y+LOSE_ROW*ROW_H+6
LX		= 320			; le lanceur
LY		= 434
NXX		= 232			; la suivante
NXY		= 430
AMIN		= 16			; angles en demi-degres (180 : vers le haut)
AMAX		= 344
NSUB		= 4			; sous-pas d'une image
SUBPX		= 3			; points par sous-pas
NCOLOR		= 7
NSIZE		= 4
FXN		= 160			; bulles qui eclatent ou tombent
FX_POP		= 1
FX_FALL		= 2
POP_T		= 8
GRAV		= 0x5800
DECO_N		= 14

ST_TITLE	= 0
ST_PLAY		= 1
ST_PAUSE	= 2
ST_LOST		= 3
ST_OVER		= 4
ST_CLEAR	= 5

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
TX_OVER		= 4
TX_AGAIN	= 5
TX_NEWBEST	= 6
TX_CLEAR	= 7
TX_BONUS	= 8
TX_CONT		= 9
TX_LEVEL	= 10
TX_HELP1	= 11
TX_HELP2	= 12
TX_HELP3	= 13
NTX		= 14

virtual at 0
g_state		rd 1
g_timer		rd 1
g_tick		rd 1
g_prev		rd 1			; l'etat d'avant la pause
g_score		rd 1
g_show		rd 1
g_best		rd 1
g_newbest	rd 1
g_level		rd 1
g_ncol		rd 1
g_every		rd 1			; tirs entre deux descentes
g_shots		rd 1
g_drop		rd 1			; rangees descendues
g_danim		rd 1			; la descente a l'ecran (points restants)
g_ang		rd 1
g_hold		rd 1
g_cur		rd 1			; couleurs 1..NCOLOR
g_next		rd 1
g_fly		rd 1
g_bx		rd 1			; la bulle tiree (16.16)
g_by		rd 1
g_vx		rd 1
g_vy		rd 1
g_down		rd 1
g_ldirty	rd 1
g_flt		rd 1			; le gain qui monte : reste, valeur, x, y
g_fltv		rd 1
g_fltx		rd 1
g_flty		rd 1
g_bonus		rd 1
g_banner	rd 1
g_popk		rd 1
g_shk		rd 1
g_sb		rd NCOLOR*NSIZE		; bulles, quatre tailles
g_sghost	rd NCOLOR
g_shalo		rd NCOLOR
g_sring		rd 3
g_sorb		rd 2
g_sdot		rd 1
g_sdot2		rd 1
g_sarrow	rd 1
g_shead		rd 1
g_sbase		rd 1
g_snbase	rd 1
g_son		rd 1
g_soff		rd 1
g_swarn		rd 1
g_dbig		rd 10
g_dgain		rd 10
g_tx		rd NTX
g_nb		rd 6
g_deco		rd DECO_N*4		; x, y (16.16), vitesse, taille
g_grid		rb NCELL		; 0 vide, 1..NCOLOR
g_mark		rb NCELL
		align 8
g_q		rb NCELL*2
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
	movl	x0,2*1024*1024
	bl	ga_init
	cbz	x0,quit
	bl	gs_open
	adr	x0,p_best
	bl	gf_load_best
	str	x0,[x28,g_best]
	bl	gen_sprites
	bl	deco_init
	adr	x0,game_pause
	adr	x1,game_resume
	adr	x2,new_game
	mov	x3,GF_W-34
	mov	x4,6
	bl	gp_init
	mov	x0,180
	str	x0,[x28,g_ang]
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
;  Sprites et couche
; ---------------------------------------------------------------------------
gen_sprites:
	prologue
	ga_push
	mov	x19,0				; couleur
.c:	adr	x0,colors
	ldr	w20,[x0,x19,lsl 2]
	mov	x21,0				; taille
.z:	adr	x0,sizes
	ldrb	w0,[x0,x21]
	mov	w1,w20
	bl	ga_ball
	lsl	x1,x19,2
	add	x1,x1,x21
	add	x2,x28,g_sb
	str	x0,[x2,x1,lsl 3]
	add	x21,x21,1
	cmp	x21,NSIZE
	b.lo	.z
	mov	x0,BD				; le fantome : ou elle se posera
	mov	w1,w20
	bl	ga_ball
	add	x1,x28,g_sghost
	str	x0,[x1,x19,lsl 3]
	mov	x1,80
	bl	ga_fade
	lsr	w1,w20,2			; la lueur, du quart de la couleur
	movl	w2,0x3F3F3F
	and	w1,w1,w2
	mov	x0,58
	bl	ga_disc
	add	x1,x28,g_shalo
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,NCOLOR
	b.lo	.c
	mov	x19,0				; bulles de savon du decor
.r:	adr	x0,ring_sz
	ldrb	w0,[x0,x19]
	movl	w1,0x00385074
	bl	gen_ring
	add	x1,x28,g_sring
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,3
	b.lo	.r
	mov	x0,170
	movl	w1,0x00141038
	bl	ga_disc
	str	x0,[x28,g_sorb]
	mov	x0,140
	movl	w1,0x00082C3C
	bl	ga_disc
	str	x0,[x28,g_sorb+8]
	mov	x0,7
	movl	w1,0x00E8F0FF
	bl	ga_ball
	str	x0,[x28,g_sdot]
	mov	x0,5
	movl	w1,0x0090A0C8
	bl	ga_ball
	str	x0,[x28,g_sdot2]
	mov	x0,9
	movl	w1,0x00FFC850
	bl	ga_ball
	str	x0,[x28,g_sarrow]
	mov	x0,6
	movl	w1,0x00FFE090
	bl	ga_ball
	str	x0,[x28,g_shead]
	mov	x0,50
	movl	w1,0x00485A80
	bl	ga_ball
	str	x0,[x28,g_sbase]
	mov	x0,34
	movl	w1,0x00303C5C
	bl	ga_ball
	str	x0,[x28,g_snbase]
	mov	x0,11
	movl	w1,0x00FFB840
	bl	ga_ball
	str	x0,[x28,g_son]
	mov	x0,11
	movl	w1,0x00384058
	bl	ga_ball
	str	x0,[x28,g_soff]
	mov	x0,11
	movl	w1,0x00FF4058
	bl	ga_ball
	str	x0,[x28,g_swarn]
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	add	x0,x28,g_dbig
	mov	x1,20
	adr	x2,ui_font_b
	movl	w3,0x00FFFFFF
	movl	w4,0x00C0D4FF
	bl	ga_digits
	add	x0,x28,g_dgain
	mov	x1,22
	adr	x2,ui_font_b
	movl	w3,0x00FFF8C0
	movl	w4,0x00FFA830
	bl	ga_digits
	adr	x0,tx_table
	adr	x1,s_base
	add	x2,x28,g_tx
	mov	x3,NTX
	bl	ga_texts
	bl	ui_canvas_end
	ga_pop
	epilogue

; x0 = diametre, w1 = couleur -> x0 = sprite : une bulle de savon (pour gf_glow),
; claire au bord, un reflet en haut a gauche
gen_ring:
	prologue
	ga_push
	mov	x22,x0
	mov	w23,w1
	mov	x1,x0
	bl	ga_new
	mov	x19,x0
	add	x24,x0,8
	mul	x25,x22,x22
	mov	x0,7
	mul	x26,x22,x0
	lsr	x26,x26,4			; le reflet : 0,44 du rayon
	mov	x20,0
.y:	mov	x21,0
.x:	lsl	x0,x21,1			; (en demi-points depuis le centre)
	add	x0,x0,1
	sub	x0,x0,x22
	lsl	x1,x20,1
	add	x1,x1,1
	sub	x1,x1,x22
	mul	x2,x0,x0
	madd	x2,x1,x1,x2
	cmp	x2,x25
	b.hs	.n
	lsl	x3,x2,8
	udiv	x3,x3,x25			; 0..255 du centre au bord
	mul	x3,x3,x3
	lsr	x3,x3,8
	mul	x3,x3,x3
	lsr	x3,x3,8
	add	x3,x3,20
	add	x4,x0,x26
	add	x5,x1,x26
	mul	x4,x4,x4
	madd	x4,x5,x5,x4
	lsr	x5,x25,4
	cmp	x4,x5
	b.hs	.c
	add	x3,x3,150
.c:	mov	x4,256
	cmp	x3,x4
	csel	x3,x4,x3,hi
	mov	w0,w23
	mov	x1,x3
	bl	ga_shade
	str	w0,[x24]
.n:	add	x24,x24,4
	add	x21,x21,1
	cmp	x21,x22
	b.lo	.x
	add	x20,x20,1
	cmp	x20,x22
	b.lo	.y
	mov	x0,x19
	ga_pop
	epilogue

build_layer:
	prologue
	ga_push
	str	xzr,[x28,g_ldirty]
	movl	w0,0x001A2C54
	movl	w1,0x00080A1C
	movl	w2,0x00304878
	mov	x3,90
	bl	ga_sky
	ldv	x0,gf_layer
	stv	x0,gf_dst
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.eq	.done
	mov	x0,FLD_X-10			; le cadre et la vitre du terrain
	mov	x1,18
	mov	x2,FLD_W+20
	mov	x3,460
	mov	x4,16
	movl	w5,0x003A4A70
	movl	w6,0x00182238
	mov	x7,220
	movl	w8,0x007088B8
	bl	ga_panel
	mov	x0,FLD_X-2
	mov	x1,CEIL_TOP
	mov	x2,FLD_W+4
	mov	x3,444
	mov	x4,10
	movl	w5,0x000A1022
	movl	w6,0x00141C36
	mov	x7,235
	mov	w8,0
	bl	ga_panel
	mov	x0,LX-118			; le pupitre du lanceur
	mov	x1,404
	mov	x2,236
	mov	x3,64
	mov	x4,20
	movl	w5,0x00242E4A
	movl	w6,0x00101828
	mov	x7,220
	movl	w8,0x00405478
	bl	ga_panel
	mov	x19,FLD_X+2			; la ligne a ne pas passer, en tirets
.dl:	mov	x0,x19
	mov	x1,DANGER_Y
	mov	x2,7
	mov	x3,2
	movl	w4,0x00B83850
	bl	gf_rect
	add	x19,x19,13
	cmp	x19,FLD_X+FLD_W-4
	b.lo	.dl
	mov	x0,14				; les panneaux des scores
	mov	x1,44
	mov	x2,128
	mov	x3,186
	mov	x4,14
	movl	w5,0x00243256
	movl	w6,0x00121A34
	mov	x7,200
	movl	w8,0x00506494
	bl	ga_panel
	mov	x0,GF_W-142
	mov	x1,44
	mov	x2,128
	mov	x3,70
	mov	x4,14
	movl	w5,0x00243256
	movl	w6,0x00121A34
	mov	x7,200
	movl	w8,0x00506494
	bl	ga_panel
	mov	x0,FLD_X-10			; (les bulles du decor passent derriere)
	mov	x1,18
	mov	x2,FLD_W+20
	mov	x3,460
	bl	ga_mark
	mov	x0,14
	mov	x1,44
	mov	x2,128
	mov	x3,186
	bl	ga_mark
	mov	x0,GF_W-142
	mov	x1,44
	mov	x2,128
	mov	x3,70
	bl	ga_mark
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	adr	x0,s_score
	mov	x1,30
	mov	x2,68
	movl	w3,0x00B8C4E0
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	adr	x0,s_level
	mov	x1,30
	mov	x2,128
	movl	w3,0x00B8C4E0
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	adr	x0,s_best
	mov	x1,30
	mov	x2,188
	movl	w3,0x00B8C4E0
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	adr	x0,s_drop
	mov	x1,GF_W-126
	mov	x2,68
	movl	w3,0x00B8C4E0
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	adr	x0,s_next
	mov	x1,NXX
	mov	x2,462
	movl	w3,0x008C9CC0
	mov	x4,11
	adr	x5,ui_font
	bl	gf_text_c
	bl	ui_canvas_end
.done:	adr	x0,gf_frame
	stv	x0,gf_dst
	ga_pop
	epilogue

; ---------------------------------------------------------------------------
;  La grille
; ---------------------------------------------------------------------------
; x0 = case -> x0, x1 = son centre (sans l'animation de descente) (touche x2..x4)
cell_pos:
	mov	x2,COLS
	udiv	x3,x0,x2			; rangee
	msub	x4,x3,x2,x0			; colonne
	lsl	x0,x4,5
	add	x0,x0,FLD_X+BRAD
	and	x2,x3,1
	add	x0,x0,x2,lsl 4
	ldr	x2,[x28,g_drop]
	add	x3,x3,x2
	mov	x2,ROW_H
	mul	x1,x3,x2
	add	x1,x1,FLD_Y+BRAD
	ret

; x0 = case -> x0 = nombre de voisines, leurs numeros dans g_nb (touche x1..x13)
; (rangees impaires decalees a droite d'un rayon)
nbrs:
	mov	x1,COLS
	udiv	x2,x0,x1			; r
	msub	x3,x2,x1,x0			; c
	add	x4,x28,g_nb
	mov	x5,0
	and	x6,x2,1
	sub	x7,x1,x6			; longueur de la rangee
	sub	x8,x3,1
	cmp	x8,x7
	sub	x13,x0,1
	str	x13,[x4,x5,lsl 3]
	cinc	x5,x5,lo
	add	x8,x3,1
	cmp	x8,x7
	add	x13,x0,1
	str	x13,[x4,x5,lsl 3]
	cinc	x5,x5,lo
	sub	x9,x3,1				; dans les rangees voisines : c-1+impair, c+impair
	add	x9,x9,x6
	add	x10,x9,1
	add	x7,x1,x6
	sub	x7,x7,1				; leur longueur
	cbz	x2,.dn
	sub	x11,x2,1
	cmp	x9,x7
	madd	x13,x11,x1,x9
	str	x13,[x4,x5,lsl 3]
	cinc	x5,x5,lo
	cmp	x10,x7
	madd	x13,x11,x1,x10
	str	x13,[x4,x5,lsl 3]
	cinc	x5,x5,lo
.dn:	add	x11,x2,1
	cmp	x11,GROWS
	b.hs	.out
	cmp	x9,x7
	madd	x13,x11,x1,x9
	str	x13,[x4,x5,lsl 3]
	cinc	x5,x5,lo
	cmp	x10,x7
	madd	x13,x11,x1,x10
	str	x13,[x4,x5,lsl 3]
	cinc	x5,x5,lo
.out:	mov	x0,x5
	ret

; x0 = case de depart (-1 : toutes les bulles de la rangee 0), x1 = couleur a
; suivre (0 : toutes) -> x0 = nombre de cases marquees (g_mark), g_q : la liste,
; des plus proches aux plus loin
bfs:
	prologue
	ga_push
	stp	x27,x28,[sp,-16]!
	mov	x21,x1
	add	x20,x28,g_grid
	add	x22,x28,g_mark
	add	x23,x28,g_q
	mov	x1,0
.z:	strb	wzr,[x22,x1]
	add	x1,x1,1
	cmp	x1,NCELL
	b.lo	.z
	mov	x25,0
	tbnz	x0,63,.top
	mov	w1,1
	strb	w1,[x22,x0]
	strh	w0,[x23]
	mov	x25,1
	b	.go
.top:	mov	x1,0
.t:	ldrb	w2,[x20,x1]
	cbz	w2,.tn
	mov	w2,1
	strb	w2,[x22,x1]
	strh	w1,[x23,x25,lsl 1]
	add	x25,x25,1
.tn:	add	x1,x1,1
	cmp	x1,COLS
	b.lo	.t
.go:	mov	x24,0
.pop:	cmp	x24,x25
	b.hs	.out
	ldrh	w0,[x23,x24,lsl 1]
	add	x24,x24,1
	bl	nbrs
	mov	x26,x0
	mov	x27,0
.nb:	cmp	x27,x26
	b.hs	.pop
	add	x1,x28,g_nb
	ldr	x1,[x1,x27,lsl 3]
	add	x27,x27,1
	ldrb	w2,[x22,x1]
	cbnz	w2,.nb
	ldrb	w2,[x20,x1]
	cbz	w2,.nb
	cbz	w21,.ok
	cmp	w2,w21
	b.ne	.nb
.ok:	mov	w2,1
	strb	w2,[x22,x1]
	strh	w1,[x23,x25,lsl 1]
	add	x25,x25,1
	b	.nb
.out:	mov	x0,x25
	ldp	x27,x28,[sp],16
	ga_pop
	epilogue

; x0 = x, x1 = y (points) -> x0 = 1 si une bulle a cet endroit touche le plafond
; ou une bulle de la grille (touche x2..x15)
hit_at:
	ldr	x2,[x28,g_drop]
	mov	x3,ROW_H
	mul	x4,x2,x3
	add	x5,x4,FLD_Y+BRAD			; centre de la rangee 0
	cmp	x1,x5
	b.le	.yes
	sub	x6,x1,x5
	udiv	x6,x6,x3
	sub	x7,x6,1
	add	x8,x6,1
	cmp	x7,0
	csel	x7,xzr,x7,lt
	mov	x9,GROWS-1
	cmp	x8,x9
	csel	x8,x9,x8,gt
.r:	cmp	x7,x8
	b.gt	.no
	madd	x9,x7,x3,x5
	sub	x9,x1,x9
	mul	x9,x9,x9			; dy2
	and	x10,x7,1
	mov	x11,FLD_X+BRAD
	add	x11,x11,x10,lsl 4
	mov	x12,COLS
	sub	x12,x12,x10
	mov	x13,COLS
	mul	x13,x7,x13
	add	x13,x13,x28
	add	x13,x13,g_grid
	mov	x14,0
.c:	ldrb	w15,[x13,x14]
	cbz	w15,.n
	lsl	x15,x14,5
	add	x15,x15,x11
	sub	x15,x0,x15
	madd	x15,x15,x15,x9
	cmp	x15,HIT2
	b.lo	.yes
.n:	add	x14,x14,1
	cmp	x14,x12
	b.lo	.c
	add	x7,x7,1
	b	.r
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = x, x1 = y -> x0 = la case libre la plus proche qui tient (plafond ou
; voisine), -1 : aucune
find_cell:
	prologue
	ga_push
	mov	x19,x0
	mov	x20,x1
	mov	x21,-1				; la meilleure
	mov	x22,-1				; sa distance (non signee)
	mov	x26,0				; 1 : sans la condition de tenue
.again:	mov	x23,0
.c:	cmp	x23,NCELL
	b.hs	.end
	mov	x0,COLS
	udiv	x1,x23,x0
	msub	x2,x1,x0,x23
	and	x3,x1,1
	cbz	x3,.ok1
	cmp	x2,COLS-1
	b.eq	.n
.ok1:	add	x3,x28,g_grid
	ldrb	w3,[x3,x23]
	cbnz	w3,.n
	cbnz	x26,.d
	cbz	x1,.d
	mov	x0,x23
	bl	nbrs
	mov	x24,x0
	mov	x25,0
.k:	cmp	x25,x24
	b.hs	.n
	add	x0,x28,g_nb
	ldr	x0,[x0,x25,lsl 3]
	add	x25,x25,1
	add	x1,x28,g_grid
	ldrb	w1,[x1,x0]
	cbz	w1,.k
.d:	mov	x0,x23
	bl	cell_pos
	sub	x0,x0,x19
	sub	x1,x1,x20
	mul	x0,x0,x0
	madd	x0,x1,x1,x0
	cmp	x0,x22
	b.hs	.n
	mov	x22,x0
	mov	x21,x23
.n:	add	x23,x23,1
	b	.c
.end:	tbz	x21,63,.out
	cbnz	x26,.out
	mov	x26,1
	b	.again
.out:	mov	x0,x21
	ga_pop
	epilogue

; -> x0 = les couleurs presentes (bit c) (touche x1..x4)
color_mask:
	add	x1,x28,g_grid
	mov	x2,NCELL
	mov	x0,0
	mov	x4,1
.c:	ldrb	w3,[x1],1
	lsl	x3,x4,x3
	orr	x0,x0,x3
	subs	x2,x2,1
	b.ne	.c
	lsr	x0,x0,1
	lsl	x0,x0,1
	ret

; -> x0 = une couleur 1..ncol, parmi celles qui restent sur le plateau
pick_color:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	color_mask
	mov	x19,x0
	mov	x20,40
.t:	ldr	x0,[x28,g_ncol]
	bl	gf_below
	add	x0,x0,1
	cbz	x19,.out
	lsr	x1,x19,x0
	tbnz	x1,0,.out
	subs	x20,x20,1
	b.ne	.t
	rbit	x1,x19
	clz	x0,x1
.out:	ldp	x19,x20,[sp],16
	epilogue

; la bulle et la suivante : des couleurs encore presentes
fix_colors:
	prologue
	str	x19,[sp,-16]!
	bl	color_mask
	mov	x19,x0
	cbz	x19,.out
	ldr	x0,[x28,g_cur]
	lsr	x1,x19,x0
	tbnz	x1,0,.n
	bl	pick_color
	str	x0,[x28,g_cur]
.n:	ldr	x0,[x28,g_next]
	lsr	x1,x19,x0
	tbnz	x1,0,.out
	bl	pick_color
	str	x0,[x28,g_next]
.out:	ldr	x19,[sp],16
	epilogue

; -> x0 = 1 si une bulle est sous la ligne
check_lose:
	ldr	x1,[x28,g_drop]
	mov	x2,LOSE_ROW
	subs	x2,x2,x1
	csel	x2,xzr,x2,lt
	mov	x3,COLS
	mul	x2,x2,x3
	add	x1,x28,g_grid
.c:	cmp	x2,NCELL
	b.hs	.no
	ldrb	w3,[x1,x2]
	cbnz	w3,.yes
	add	x2,x2,1
	b	.c
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  Bulles qui eclatent, bulles qui tombent
; ---------------------------------------------------------------------------
; x0 = type, x1 = x, x2 = y (points), x3 = couleur, x4 = minuterie -> x0 = l'entree
; (0 : plus de place)
fx_add:
	adr	x5,g_fx
	mov	x6,FXN
.f:	ldr	w7,[x5,16]
	cbz	w7,.ok
	add	x5,x5,32
	subs	x6,x6,1
	b.ne	.f
	mov	x0,0
	ret
.ok:	lsl	x1,x1,16
	lsl	x2,x2,16
	stp	w1,w2,[x5]
	stp	wzr,wzr,[x5,8]
	stp	w0,w3,[x5,16]
	str	w4,[x5,24]
	mov	x0,x5
	ret

fx_clear:
	adr	x0,g_fx
	mov	x1,FXN
.f:	str	wzr,[x0,16]
	add	x0,x0,32
	subs	x1,x1,1
	b.ne	.f
	ret

fx_step:
	prologue
	ga_push
	adr	x19,g_fx
	mov	x20,FXN
.f:	ldr	w0,[x19,16]
	cbz	w0,.n
	cmp	w0,FX_POP
	b.ne	.fall
	ldr	w1,[x19,24]
	sub	w1,w1,1
	str	w1,[x19,24]
	cbz	w1,.kill
	cmp	w1,POP_T
	b.ne	.n
	ldp	w0,w1,[x19]			; elle eclate : gerbe, note qui monte
	asr	w0,w0,16
	asr	w1,w1,16
	sxtw	x0,w0
	sxtw	x1,w1
	ldr	w2,[x19,20]
	sub	w2,w2,1
	adr	x3,colors
	ldr	w2,[x3,x2,lsl 2]
	mov	x3,14
	movl	x4,170000
	mov	x5,18
	mov	x6,3
	movl	x7,5000
	bl	ga_burst
	ldr	x0,[x28,g_popk]
	add	x1,x0,1
	str	x1,[x28,g_popk]
	mov	x1,20
	cmp	x0,x1
	csel	x0,x1,x0,hi
	mov	x1,45
	mul	x0,x0,x1
	add	x0,x0,520
	mov	x1,60
	mov	x2,AKW_SINE
	mov	x3,90
	bl	gs_note
	b	.n
.kill:	str	wzr,[x19,16]
	b	.n
.fall:	ldr	w1,[x19,24]			; (une attente d'abord)
	cbz	w1,.mv
	sub	w1,w1,1
	str	w1,[x19,24]
	b	.n
.mv:	ldp	w0,w1,[x19]
	ldp	w2,w3,[x19,8]
	add	w0,w0,w2
	add	w1,w1,w3
	movl	w4,GRAV
	add	w3,w3,w4
	stp	w0,w1,[x19]
	str	w3,[x19,12]
	asr	w1,w1,16
	cmp	w1,GF_H+24
	b.lt	.n
	str	wzr,[x19,16]
.n:	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.f
	ga_pop
	epilogue

fx_draw:
	prologue
	ga_push
	adr	x19,g_fx
	mov	x20,FXN
.f:	ldr	w0,[x19,16]
	cbz	w0,.n
	ldr	w21,[x19,20]
	sub	w21,w21,1
	ldp	w22,w23,[x19]
	asr	w22,w22,16
	asr	w23,w23,16
	sxtw	x22,w22
	sxtw	x23,w23
	ldr	w24,[x19,24]
	mov	x25,0				; taille
	cmp	w0,FX_POP
	b.ne	.d
	cmp	w24,POP_T
	b.hi	.d
	mov	x0,POP_T			; elle eclate : lueur, elle retrecit
	sub	x25,x0,x24
	lsr	x25,x25,1
	add	x0,x28,g_shalo
	ldr	x0,[x0,x21,lsl 3]
	mov	x1,x22
	mov	x2,x23
	bl	centered_glow
.d:	lsl	x0,x21,2
	add	x0,x0,x25
	add	x1,x28,g_sb
	ldr	x0,[x1,x0,lsl 3]
	mov	x1,x22
	mov	x2,x23
	bl	centered
.n:	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.f
	ga_pop
	epilogue

; ---------------------------------------------------------------------------
;  Les regles
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

new_game:
	prologue
	str	xzr,[x28,g_score]
	str	xzr,[x28,g_show]
	str	xzr,[x28,g_newbest]
	str	xzr,[x28,g_flt]
	str	xzr,[x28,g_down]
	mov	x0,1
	str	x0,[x28,g_level]
	mov	x0,180
	str	x0,[x28,g_ang]
	bl	fx_clear
	bl	ga_parts_clear
	bl	start_level
	epilogue

next_level:
	prologue
	ldr	x0,[x28,g_level]
	add	x0,x0,1
	str	x0,[x28,g_level]
	bl	start_level
	epilogue

; le niveau g_level : couleurs, rangees, descente, motif
start_level:
	prologue
	ga_push
	ldr	x19,[x28,g_level]
	lsr	x0,x19,1			; couleurs : 3 + niveau / 2, 7 au plus
	add	x0,x0,3
	mov	x1,NCOLOR
	cmp	x0,x1
	csel	x0,x1,x0,hi
	str	x0,[x28,g_ncol]
	lsr	x0,x19,1			; rangees : 4 + niveau / 2, 8 au plus
	add	x0,x0,4
	mov	x1,8
	cmp	x0,x1
	csel	x20,x1,x0,hi
	mov	x0,10				; tirs par descente : 10 - niveau, 4 au moins
	sub	x0,x0,x19
	mov	x1,4
	cmp	x0,x1
	csel	x0,x1,x0,lt
	str	x0,[x28,g_every]
	sub	x21,x19,1
	and	x21,x21,3			; motif
	str	xzr,[x28,g_drop]
	str	xzr,[x28,g_danim]
	str	xzr,[x28,g_shots]
	str	xzr,[x28,g_fly]
	add	x0,x28,g_grid
	mov	x1,NCELL
.z:	strb	wzr,[x0],1
	subs	x1,x1,1
	b.ne	.z
	mov	x22,0
.c:	mov	x0,COLS
	udiv	x23,x22,x0			; r
	msub	x24,x23,x0,x22			; c
	cmp	x23,x20
	b.hs	.gend
	and	x0,x23,1
	cbz	x0,.ok1
	cmp	x24,COLS-1
	b.eq	.n
.ok1:	lsl	x25,x24,1			; position en rayons
	and	x0,x23,1
	add	x25,x25,x0
	cmp	x21,1
	b.ne	.p2
	subs	x0,x25,9			; un entonnoir
	cneg	x0,x0,mi
	sub	x1,x20,1
	sub	x1,x1,x23
	lsl	x1,x1,1
	add	x1,x1,3
	cmp	x0,x1
	b.hi	.n
	b	.col
.p2:	cmp	x21,2
	b.ne	.p3
	cmp	x23,2				; des colonnes qui pendent
	b.lo	.col
	lsr	x0,x25,2
	tbnz	x0,0,.n
	b	.col
.p3:	cmp	x21,3
	b.ne	.col
	cbz	x23,.col			; un treillis a trous
	mov	x0,3
	madd	x0,x23,x0,x25
	mov	x1,7
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	cmp	x0,5
	b.hs	.n
.col:	mov	x0,100				; des grappes : souvent la couleur voisine
	bl	gf_below
	mov	x26,x0
	add	x3,x28,g_grid
	cmp	x26,45
	b.hs	.up
	cbz	x24,.up
	sub	x1,x22,1
	ldrb	w0,[x3,x1]
	cbnz	w0,.put
.up:	cmp	x26,70
	b.hs	.rnd
	cbz	x23,.rnd
	sub	x1,x22,COLS
	ldrb	w0,[x3,x1]
	cbnz	w0,.put
.rnd:	ldr	x0,[x28,g_ncol]
	bl	gf_below
	add	x0,x0,1
	add	x3,x28,g_grid
.put:	strb	w0,[x3,x22]
.n:	add	x22,x22,1
	cmp	x22,NCELL
	b.lo	.c
.gend:	mov	x0,-1				; rien ne flotte
	mov	x1,0
	bl	bfs
	add	x0,x28,g_grid
	add	x1,x28,g_mark
	mov	x2,0
.f:	ldrb	w3,[x1,x2]
	cbnz	w3,.fk
	strb	wzr,[x0,x2]
.fk:	add	x2,x2,1
	cmp	x2,NCELL
	b.lo	.f
 if defined BUB_TEST				; (essai : deux bulles d'une couleur)
	add	x0,x28,g_grid
	mov	x1,NCELL
.tz:	strb	wzr,[x0],1
	subs	x1,x1,1
	b.ne	.tz
	add	x0,x28,g_grid
	mov	w1,1
	strb	w1,[x0,4]
	strb	w1,[x0,5]
	str	x1,[x28,g_ncol]
 end if
 if defined BUB_LOSE				; (essai : le plafond descend a chaque tir)
	mov	x0,1
	str	x0,[x28,g_every]
 end if
	bl	color_mask
	cbnz	x0,.ok
	mov	x22,0				; (jamais vide)
.r0:	ldr	x0,[x28,g_ncol]
	bl	gf_below
	add	x0,x0,1
	add	x1,x28,g_grid
	strb	w0,[x1,x22]
	add	x22,x22,1
	cmp	x22,COLS
	b.lo	.r0
.ok:	bl	pick_color
	str	x0,[x28,g_cur]
	bl	pick_color
	str	x0,[x28,g_next]
	mov	x0,110
	str	x0,[x28,g_banner]
	mov	x0,ST_PLAY
	bl	set_state
	mov	x0,1
	str	x0,[x28,g_ldirty]
	ga_pop
	epilogue

; x0 = angle -> x0 = cos, x1 = sin (16.16 ; sin > 0 : vers le haut) (touche x2, x3)
aim_dir:
	adr	x2,sin_tab
	cmp	x0,180
	b.hi	.l
	ldr	w1,[x2,x0,lsl 2]
	mov	x3,180
	sub	x3,x3,x0
	ldr	w0,[x2,x3,lsl 2]
	ret
.l:	mov	x3,360
	sub	x3,x3,x0
	ldr	w1,[x2,x3,lsl 2]
	sub	x3,x0,180
	ldr	w0,[x2,x3,lsl 2]
	neg	x0,x0
	ret

; x0, x1 = point : le lanceur le vise (rien s'il est sous le lanceur)
aim_at:
	prologue
	ga_push
	sub	x19,x0,LX
	mov	x2,LY
	sub	x20,x2,x1
	cmp	x20,10
	b.lt	.out
	mov	x21,AMIN
	mov	x22,AMIN
	mov	x23,1
	lsl	x23,x23,63
.a:	mov	x0,x21
	bl	aim_dir
	mul	x0,x0,x19
	madd	x0,x1,x20,x0
	cmp	x0,x23
	b.le	.n
	mov	x23,x0
	mov	x22,x21
.n:	add	x21,x21,1
	cmp	x21,AMAX
	b.ls	.a
	str	x22,[x28,g_ang]
.out:	ga_pop
	epilogue

; gauche / droite tenues (clavier, croix) : le lanceur tourne, de plus en plus vite
aim_step:
	prologue
	adr	x0,gf_held
	ldrb	w1,[x0,KEY_LEFT]
	ldrb	w2,[x0,KEY_RIGHT]
	ldv	x3,gp_now
	and	x4,x3,PAD_LEFT
	orr	x1,x1,x4
	and	x4,x3,PAD_RIGHT
	orr	x2,x2,x4
	cmp	x1,0
	cset	x1,ne
	cmp	x2,0
	cset	x2,ne
	subs	x5,x1,x2			; +1 : vers la gauche
	b.ne	.go
	str	xzr,[x28,g_hold]
	b	.out
.go:	ldr	x0,[x28,g_hold]
	add	x0,x0,1
	str	x0,[x28,g_hold]
	mov	x1,1
	cmp	x0,10
	b.lo	.a
	mov	x1,2
	cmp	x0,30
	b.lo	.a
	mov	x1,3
.a:	mul	x1,x1,x5
	ldr	x0,[x28,g_ang]
	add	x0,x0,x1
	mov	x1,AMIN
	cmp	x0,x1
	csel	x0,x1,x0,lt
	mov	x1,AMAX
	cmp	x0,x1
	csel	x0,x1,x0,gt
	str	x0,[x28,g_ang]
.out:	epilogue

shoot:
	prologue
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.ne	.out
	ldr	x0,[x28,g_fly]
	cbnz	x0,.out
	ldr	x0,[x28,g_ang]
	bl	aim_dir
	mov	x2,SUBPX
	mul	x0,x0,x2
	mul	x1,x1,x2
	neg	x1,x1
	str	x0,[x28,g_vx]
	str	x1,[x28,g_vy]
	mov	x0,LX
	lsl	x0,x0,16
	str	x0,[x28,g_bx]
	mov	x0,LY
	lsl	x0,x0,16
	str	x0,[x28,g_by]
	mov	x0,1
	str	x0,[x28,g_fly]
	mov	x0,620
	mov	x1,45
	mov	x2,AKW_TRIANGLE
	mov	x3,90
	bl	gs_note
	mov	x0,35
	mov	x1,930
	mov	x2,45
	mov	x3,AKW_TRIANGLE
	mov	x4,70
	bl	gs_later
.out:	epilogue

do_swap:
	prologue
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.ne	.out
	ldr	x0,[x28,g_fly]
	cbnz	x0,.out
	ldr	x0,[x28,g_cur]
	ldr	x1,[x28,g_next]
	str	x1,[x28,g_cur]
	str	x0,[x28,g_next]
	mov	x0,880
	mov	x1,35
	mov	x2,AKW_SINE
	mov	x3,70
	bl	gs_note
.out:	epilogue

; la bulle tiree avance (quatre sous-pas), rebondit, se pose
fly_step:
	prologue
	ga_push
	mov	x19,NSUB
	mov	x20,0				; 1 : un rebond
.s:	ldr	x0,[x28,g_bx]
	ldr	x1,[x28,g_vx]
	add	x0,x0,x1
	ldr	x2,[x28,g_by]
	ldr	x3,[x28,g_vy]
	add	x2,x2,x3
	str	x2,[x28,g_by]
	movl	x4,(FLD_X+BRAD) shl 16
	cmp	x0,x4
	b.ge	.r
	lsl	x5,x4,1
	sub	x0,x5,x0
	b	.bn
.r:	movl	x4,(FLD_X+FLD_W-BRAD) shl 16
	cmp	x0,x4
	b.le	.w
	lsl	x5,x4,1
	sub	x0,x5,x0
.bn:	neg	x1,x1
	str	x1,[x28,g_vx]
	mov	x20,1
.w:	str	x0,[x28,g_bx]
	asr	x0,x0,16
	asr	x1,x2,16
	bl	hit_at
	cbnz	x0,.land
	subs	x19,x19,1
	b.ne	.s
	cbz	x20,.out
	mov	x0,1400
	mov	x1,18
	mov	x2,AKW_SINE
	mov	x3,50
	bl	gs_note
	b	.out
.land:	ldr	x0,[x28,g_bx]
	asr	x0,x0,16
	ldr	x1,[x28,g_by]
	asr	x1,x1,16
	bl	land
.out:	ga_pop
	epilogue

; x0, x1 = ou la bulle s'arrete : elle se pose, les groupes eclatent, ce qui ne
; tient plus tombe, le plafond descend, perdu ou gagne
land:
	prologue
	ga_push
	str	xzr,[x28,g_fly]
	str	xzr,[x28,g_popk]
	bl	find_cell
	mov	x19,x0
	tbnz	x19,63,.after
	add	x0,x28,g_grid
	ldr	x1,[x28,g_cur]
	strb	w1,[x0,x19]
	mov	x0,x19
	bl	bfs
	mov	x20,x0
	cmp	x20,3
	b.hs	.pop
	mov	x0,300				; elle colle
	mov	x1,60
	mov	x2,AKW_TRIANGLE
	mov	x3,80
	bl	gs_note
	b	.after
.pop:	mov	x21,0
.pk:	cmp	x21,x20
	b.hs	.pd
	add	x0,x28,g_q
	ldrh	w22,[x0,x21,lsl 1]
	add	x0,x28,g_grid
	ldrb	w23,[x0,x22]
	strb	wzr,[x0,x22]
	mov	x0,x22
	bl	cell_pos
	ldr	x2,[x28,g_danim]
	sub	x2,x1,x2
	mov	x1,x0
	mov	x0,FX_POP
	mov	x3,x23
	add	x4,x21,x21,lsl 1		; en cascade depuis le point d'impact
	add	x4,x4,POP_T+1
	bl	fx_add
	add	x21,x21,1
	b	.pk
.pd:	mov	x0,10
	mul	x24,x20,x0			; le gain
	mov	x0,-1				; ce qui ne tient plus
	mov	x1,0
	bl	bfs
	mov	x21,0
	mov	x25,0
.fl:	cmp	x21,NCELL
	b.hs	.fd
	add	x0,x28,g_grid
	ldrb	w23,[x0,x21]
	cbz	w23,.fn
	add	x1,x28,g_mark
	ldrb	w1,[x1,x21]
	cbnz	w1,.fn
	strb	wzr,[x0,x21]
	add	x25,x25,1
	mov	x0,x21
	bl	cell_pos
	ldr	x2,[x28,g_danim]
	sub	x2,x1,x2
	mov	x1,x0
	mov	x0,FX_FALL
	mov	x3,x23
	mov	x4,0
	bl	fx_add
	cbz	x0,.fn
	mov	x26,x0
	movl	x0,0x18000
	bl	gf_below
	movl	x1,0xC000
	sub	x0,x0,x1
	str	w0,[x26,8]
	movl	x0,0x14000
	bl	gf_below
	movl	x1,0x6000
	add	x0,x0,x1
	neg	x0,x0
	str	w0,[x26,12]
.fn:	add	x21,x21,1
	b	.fl
.fd:	cbz	x25,.sc
	add	x0,x25,3			; 20 par bulle, plus pour une grosse grappe
	lsr	x0,x0,2
	mul	x0,x0,x25
	mov	x1,20
	mul	x0,x0,x1
	add	x24,x24,x0
	adr	x0,snd_fall
	bl	gs_seq
.sc:	ldr	x0,[x28,g_score]
	add	x0,x0,x24
	str	x0,[x28,g_score]
	str	x24,[x28,g_fltv]
	mov	x0,60
	str	x0,[x28,g_flt]
	mov	x0,x19
	bl	cell_pos
	str	x0,[x28,g_fltx]
	str	x1,[x28,g_flty]
.after:	bl	color_mask
	cbnz	x0,.desc
	bl	level_clear
	b	.out
.desc:	ldr	x0,[x28,g_shots]
	add	x0,x0,1
	ldr	x1,[x28,g_every]
	cmp	x0,x1
	b.lo	.ns
	mov	x0,0
	ldr	x1,[x28,g_drop]
	add	x1,x1,1
	str	x1,[x28,g_drop]
	mov	x1,ROW_H
	str	x1,[x28,g_danim]
	str	x0,[x28,g_shots]
	adr	x0,snd_drop
	bl	gs_seq
	b	.lc
.ns:	str	x0,[x28,g_shots]
.lc:	bl	check_lose
	cbz	x0,.col
	bl	lose
	b	.out
.col:	ldr	x0,[x28,g_next]
	str	x0,[x28,g_cur]
	bl	pick_color
	str	x0,[x28,g_next]
	bl	fix_colors
.out:	ga_pop
	epilogue

; perdu : toutes les bulles tombent, du bas vers le haut
lose:
	prologue
	ga_push
	mov	x0,ST_LOST
	bl	set_state
	str	xzr,[x28,g_fly]
	mov	x19,0
.c:	cmp	x19,NCELL
	b.hs	.d
	add	x0,x28,g_grid
	ldrb	w20,[x0,x19]
	cbz	w20,.n
	strb	wzr,[x0,x19]
	mov	x0,x19
	bl	cell_pos
	ldr	x2,[x28,g_danim]
	sub	x2,x1,x2
	mov	x1,x0
	mov	x3,COLS
	udiv	x4,x19,x3
	mov	x3,GROWS
	sub	x4,x3,x4
	lsl	x4,x4,2
	mov	x0,FX_FALL
	mov	x3,x20
	bl	fx_add
	cbz	x0,.n
	mov	x21,x0
	movl	x0,0x20000
	bl	gf_below
	movl	x1,0x10000
	sub	x0,x0,x1
	str	w0,[x21,8]
.n:	add	x19,x19,1
	b	.c
.d:	ldr	x2,[x28,g_score]
	ldr	x0,[x28,g_best]
	cmp	x2,x0
	b.ls	.s
	str	x2,[x28,g_best]
	mov	x0,1
	str	x0,[x28,g_newbest]
	adr	x0,p_dir
	adr	x1,p_best
	bl	gf_save_best
.s:	adr	x0,gs_tab_lose
	bl	gs_seq
	ga_pop
	epilogue

level_clear:
	prologue
	mov	x0,ST_CLEAR
	bl	set_state
	ldr	x0,[x28,g_level]
	mov	x1,250
	mul	x0,x0,x1
	str	x0,[x28,g_bonus]
	ldr	x1,[x28,g_score]
	add	x1,x1,x0
	str	x1,[x28,g_score]
	adr	x0,gs_tab_win
	bl	gs_seq
	epilogue

; (gamepause) la partie s'arrete -> x0 = 1 si elle etait en cours
game_pause:
	prologue
	ldr	x1,[x28,g_state]
	mov	x0,0
	cmp	x1,ST_PLAY
	b.eq	.p
	cmp	x1,ST_LOST
	b.eq	.p
	cmp	x1,ST_CLEAR
	b.ne	.out
.p:	str	x1,[x28,g_prev]
	mov	x0,ST_PAUSE
	str	x0,[x28,g_state]
	str	xzr,[x28,g_down]
	mov	x0,1
	stv	x0,gf_still
	stv	x0,gf_redraw
.out:	epilogue

; (gamepause) la partie repart
game_resume:
	prologue
	ldr	x1,[x28,g_state]
	cmp	x1,ST_PAUSE
	b.ne	.out
	ldr	x0,[x28,g_prev]
	str	x0,[x28,g_state]
	stv	xzr,gf_still
	mov	x0,1
	stv	x0,gf_redraw
.out:	epilogue

deco_init:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x28,g_deco
	mov	x20,DECO_N
.d:	mov	x0,GF_W
	bl	gf_below
	str	x0,[x19]
	mov	x0,GF_H
	bl	gf_below
	lsl	x0,x0,16
	str	x0,[x19,8]
	movl	x0,0xA000
	bl	gf_below
	movl	x1,0x3000
	add	x0,x0,x1
	str	x0,[x19,16]
	mov	x0,3
	bl	gf_below
	str	x0,[x19,24]
	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.d
	ldp	x19,x20,[sp],16
	epilogue

deco_step:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x28,g_deco
	mov	x20,DECO_N
.d:	ldr	x0,[x19,8]
	ldr	x1,[x19,16]
	sub	x0,x0,x1
	str	x0,[x19,8]
	asr	x2,x0,16
	add	x2,x2,40
	tbz	x2,63,.n
	mov	x0,GF_H+30
	lsl	x0,x0,16
	str	x0,[x19,8]
	mov	x0,GF_W
	bl	gf_below
	str	x0,[x19]
.n:	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.d
	ldp	x19,x20,[sp],16
	epilogue

step:
	prologue
	ldr	x0,[x28,g_tick]
	add	x0,x0,1
	str	x0,[x28,g_tick]
	bl	gs_tick
	bl	ga_parts_step
	bl	deco_step
	bl	fx_step
	ldr	x0,[x28,g_timer]
	add	x0,x0,1
	str	x0,[x28,g_timer]
	ldr	x0,[x28,g_show]
	ldr	x1,[x28,g_score]
	subs	x2,x1,x0
	b.ls	.f
	lsr	x2,x2,3
	mov	x3,1
	cmp	x2,x3
	csel	x2,x3,x2,lo
	add	x0,x0,x2
	str	x0,[x28,g_show]
.f:	ldr	x0,[x28,g_flt]
	cbz	x0,.b
	sub	x0,x0,1
	str	x0,[x28,g_flt]
.b:	ldr	x0,[x28,g_banner]
	cbz	x0,.a
	sub	x0,x0,1
	str	x0,[x28,g_banner]
.a:	ldr	x0,[x28,g_danim]
	subs	x0,x0,2
	csel	x0,xzr,x0,lt
	str	x0,[x28,g_danim]
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.ne	.lo
	bl	aim_step
	ldr	x0,[x28,g_fly]
	cbz	x0,.out
	bl	fly_step
	b	.out
.lo:	cmp	x0,ST_LOST
	b.ne	.cl
	ldr	x0,[x28,g_timer]
	cmp	x0,120
	b.lo	.out
	mov	x0,ST_OVER
	bl	set_state
	b	.out
.cl:	cmp	x0,ST_CLEAR
	b.ne	.out
	ldr	x0,[x28,g_timer]
	cmp	x0,240
	b.lo	.out
	bl	next_level
.out:	epilogue

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
	bl	gp_open
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

; tirer, ou commencer / continuer / rejouer
act_fire:
	prologue
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.eq	.new
	cmp	x0,ST_OVER
	b.ne	.c
	ldr	x1,[x28,g_timer]
	cmp	x1,25
	b.lo	.out
.new:	bl	new_game
	b	.out
.c:	cmp	x0,ST_CLEAR
	b.ne	.p
	ldr	x1,[x28,g_timer]
	cmp	x1,40
	b.lo	.out
	bl	next_level
	b	.out
.p:	cmp	x0,ST_PLAY
	b.ne	.out
	bl	shoot
.out:	epilogue

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
	b.ne	.r
	bl	gp_open
	b	.out
.r:	cmp	w0,'r'
	b.ne	.c
	ldr	x0,[x28,g_state]
	cbz	x0,.out
	bl	new_game
	b	.out
.c:	ldr	w2,[x1,EV_CODE]
	cmp	w2,KEY_SPACE
	b.eq	.f
	cmp	w2,KEY_ENTER
	b.eq	.f
	cmp	w2,KEY_UP
	b.ne	.d
.f:	bl	act_fire
	b	.out
.d:	cmp	w2,KEY_DOWN
	b.ne	.out
	bl	do_swap
.out:	epilogue

on_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	gf_mouse
	mov	x19,x0
	mov	x20,x1
	adr	x2,ui_ev
	ldr	w3,[x2,EV_KIND]
	cmp	w3,1
	b.eq	.press
	cmp	w3,2
	b.eq	.rel
	cbnz	w3,.out
	ldr	x0,[x28,g_state]		; survol ou glissement : viser
	cmp	x0,ST_PLAY
	b.ne	.out
	mov	x0,x19
	mov	x1,x20
	bl	aim_at
	b	.out
.press:	ldr	w3,[x2,EV_CODE]
	tbz	w3,0,.out
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.eq	.pp
	bl	act_fire
	b	.out
.pp:	sub	x0,x19,NXX			; la suivante : echange
	cmp	x0,0
	cneg	x0,x0,lt
	cmp	x0,22
	b.hs	.aim
	sub	x0,x20,NXY
	cmp	x0,0
	cneg	x0,x0,lt
	cmp	x0,22
	b.hs	.aim
	bl	do_swap
	b	.out
.aim:	mov	x0,1
	str	x0,[x28,g_down]
	mov	x0,x19
	mov	x1,x20
	bl	aim_at
	b	.out
.rel:	ldr	x0,[x28,g_down]
	cbz	x0,.out
	str	xzr,[x28,g_down]
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.ne	.out
	mov	x0,x19
	mov	x1,x20
	bl	aim_at
	bl	shoot
.out:	ldp	x19,x20,[sp],16
	epilogue

on_pad:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,ui_ev
	ldr	w1,[x0,4]
	cbz	w1,.out
	ldv	x19,gp_new			; (Start : gamepause ; la croix tenue : aim_step)
	tst	x19,PAD_A
	b.ne	.f
	tst	x19,PAD_UP
	b.eq	.x
.f:	bl	act_fire
.x:	tst	x19,PAD_X or PAD_Y
	b.eq	.out
	bl	do_swap
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
	b.ne	.f
	bl	on_pad
	b	.out
.f:	cmp	x0,WEV_FOCUS
	b.ne	.out
	str	xzr,[x28,g_down]		; (la pause : gamepause)
.out:	epilogue

; ---------------------------------------------------------------------------
;  Le dessin
; ---------------------------------------------------------------------------
; x0 = sprite, x1, x2 = centre
centered:
	mov	x3,x2
	mov	x2,x0
	ldr	w4,[x2]
	sub	x0,x1,x4,lsr 1
	ldr	w4,[x2,4]
	sub	x1,x3,x4,lsr 1
	b	ga_ablit

; x0 = lueur, x1, x2 = centre
centered_glow:
	mov	x3,x2
	mov	x2,x0
	ldr	w4,[x2]
	sub	x0,x1,x4,lsr 1
	ldr	w4,[x2,4]
	sub	x1,x3,x4,lsr 1
	b	gf_glow

deco_draw:
	prologue
	ga_push
	add	x19,x28,g_deco
	mov	x20,0
.d:	ldr	x0,[x28,g_tick]
	mov	x1,45
	madd	x0,x20,x1,x0
	bl	ga_sin8
	sub	x0,x0,127
	asr	x21,x0,4
	ldr	x1,[x19,24]
	add	x2,x28,g_sring
	ldr	x22,[x2,x1,lsl 3]
	ldr	w3,[x22]
	ldr	x0,[x19]
	add	x0,x0,x21
	sub	x0,x0,x3,lsr 1
	ldr	x1,[x19,8]
	asr	x1,x1,16
	sub	x1,x1,x3,lsr 1
	mov	x2,x22
	bl	gf_bglow
	add	x19,x19,32
	add	x20,x20,1
	cmp	x20,DECO_N
	b.lo	.d
	ga_pop
	epilogue

draw_hud:
	prologue
	ga_push
	ldr	x0,[x28,g_show]
	mov	x1,30
	mov	x2,74
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x0,[x28,g_level]
	mov	x1,30
	mov	x2,134
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x0,[x28,g_best]
	mov	x1,30
	mov	x2,194
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x19,[x28,g_every]		; les tirs avant la descente
	ldr	x0,[x28,g_shots]
	sub	x20,x19,x0
	mov	x21,0
.d:	cmp	x21,x19
	b.hs	.out
	ldr	x0,[x28,g_soff]
	cmp	x21,x20
	b.hs	.p
	ldr	x0,[x28,g_son]
	cmp	x20,1
	b.ne	.p
	ldr	x1,[x28,g_tick]
	tbz	x1,3,.p
	ldr	x0,[x28,g_swarn]
.p:	mov	x1,12
	mul	x1,x21,x1
	add	x1,x1,GF_W-124
	mov	x2,92
	bl	centered
	add	x21,x21,1
	b	.d
.out:	ga_pop
	epilogue

; le plafond, qui descend
draw_ceiling:
	prologue
	ga_push
	ldr	x0,[x28,g_drop]
	mov	x1,ROW_H
	mul	x0,x0,x1
	add	x0,x0,FLD_Y
	ldr	x1,[x28,g_danim]
	sub	x19,x0,x1			; son bas
	mov	x20,CEIL_TOP
	sub	x21,x19,x20
	ldr	x22,[x28,g_shk]
.y:	cmp	x20,x19
	b.hs	.e
	movl	w0,0x005C6C94
	movl	w1,0x002C3858
	sub	x2,x20,CEIL_TOP
	lsl	x2,x2,8
	udiv	x2,x2,x21
	bl	ga_mix
	mov	w4,w0
	add	x0,x22,FLD_X-2
	mov	x1,x20
	mov	x2,FLD_W+4
	mov	x3,1
	bl	gf_rect
	add	x20,x20,1
	b	.y
.e:	add	x0,x22,FLD_X-2
	sub	x1,x19,2
	mov	x2,FLD_W+4
	mov	x3,2
	movl	w4,0x00A8C0E8
	bl	gf_rect
	add	x0,x22,FLD_X-2
	mov	x1,x19
	mov	x2,FLD_W+4
	mov	x3,3
	movl	w4,0x00060A14
	bl	gf_rect
	mov	x20,0				; les rivets
.r:	mov	x0,40
	mul	x1,x20,x0
	add	x1,x1,FLD_X+20
	add	x1,x1,x22
	sub	x2,x19,9
	ldr	x0,[x28,g_sdot2]
	bl	centered
	add	x20,x20,1
	cmp	x20,8
	b.lo	.r
	ga_pop
	epilogue

draw_grid:
	prologue
	ga_push
	mov	x19,0
.c:	add	x0,x28,g_grid
	ldrb	w20,[x0,x19]
	cbz	w20,.n
	mov	x0,x19
	bl	cell_pos
	ldr	x2,[x28,g_danim]
	sub	x2,x1,x2
	ldr	x3,[x28,g_shk]
	add	x1,x0,x3
	sub	x3,x20,1
	lsl	x3,x3,2
	add	x4,x28,g_sb
	ldr	x0,[x4,x3,lsl 3]
	bl	centered
.n:	add	x19,x19,1
	cmp	x19,NCELL
	b.lo	.c
	ga_pop
	epilogue

; les pointilles de la trajectoire (un rebond) et le fantome a l'arrivee
draw_aim:
	prologue
	ga_push
	ldr	x0,[x28,g_ang]
	bl	aim_dir
	lsl	x21,x0,2			; 4 points par pas
	neg	x22,x1
	lsl	x22,x22,2
	mov	x19,LX
	lsl	x19,x19,16
	mov	x20,LY
	lsl	x20,x20,16
	mov	x23,0
	mov	x24,0				; rebonds
.s:	add	x23,x23,1
	cmp	x23,220
	b.hs	.out
	add	x19,x19,x21
	add	x20,x20,x22
	movl	x0,(FLD_X+BRAD) shl 16
	cmp	x19,x0
	b.ge	.r
	lsl	x1,x0,1
	sub	x19,x1,x19
	b	.bn
.r:	movl	x0,(FLD_X+FLD_W-BRAD) shl 16
	cmp	x19,x0
	b.le	.h
	lsl	x1,x0,1
	sub	x19,x1,x19
.bn:	neg	x21,x21
	add	x24,x24,1
	cmp	x24,2
	b.hs	.out
.h:	asr	x0,x19,16
	asr	x1,x20,16
	bl	hit_at
	cbnz	x0,.land
	and	x0,x23,3
	cbnz	x0,.s
	cmp	x23,8
	b.lo	.s
	ldr	x0,[x28,g_sdot]
	cbz	x24,.d1
	ldr	x0,[x28,g_sdot2]
.d1:	asr	x1,x19,16
	asr	x2,x20,16
	bl	centered
	b	.s
.land:	asr	x0,x19,16
	asr	x1,x20,16
	bl	find_cell
	tbnz	x0,63,.out
	bl	cell_pos
	ldr	x2,[x28,g_danim]
	sub	x2,x1,x2
	mov	x1,x0
	ldr	x3,[x28,g_cur]
	sub	x3,x3,1
	add	x4,x28,g_sghost
	ldr	x0,[x4,x3,lsl 3]
	bl	centered
.out:	ga_pop
	epilogue

; le lanceur, sa fleche, la bulle prete et la suivante
draw_launcher:
	prologue
	ga_push
	ldr	x0,[x28,g_sbase]
	mov	x1,LX
	mov	x2,LY
	bl	centered
	ldr	x0,[x28,g_snbase]
	mov	x1,NXX
	mov	x2,NXY
	bl	centered
	ldr	x0,[x28,g_next]
	cbz	x0,.arrow
	sub	x0,x0,1
	lsl	x0,x0,2
	add	x0,x0,1
	add	x1,x28,g_sb
	ldr	x0,[x1,x0,lsl 3]
	mov	x1,NXX
	mov	x2,NXY
	bl	centered
.arrow:	ldr	x0,[x28,g_ang]
	bl	aim_dir
	mov	x21,x0				; cos
	mov	x22,x1				; sin
	mov	x23,20
.sh:	mul	x1,x21,x23
	asr	x1,x1,16
	add	x1,x1,LX
	mul	x2,x22,x23
	asr	x2,x2,16
	mov	x3,LY
	sub	x2,x3,x2
	ldr	x0,[x28,g_sarrow]
	bl	centered
	add	x23,x23,2
	cmp	x23,54
	b.lo	.sh
	mov	x23,50				; la pointe
.hd:	mov	x0,68
	sub	x24,x0,x23
	mov	x0,5
	mul	x24,x24,x0
	mov	x0,12
	udiv	x24,x24,x0			; demi-largeur
	neg	x25,x24
.hk:	mul	x1,x21,x23			; centre + dir * d + perp * k
	mul	x0,x22,x25
	add	x1,x1,x0
	asr	x1,x1,16
	add	x1,x1,LX
	mul	x0,x21,x25
	mul	x2,x22,x23
	sub	x2,x0,x2
	asr	x2,x2,16
	add	x2,x2,LY
	ldr	x0,[x28,g_shead]
	bl	centered
	add	x25,x25,2
	cmp	x25,x24
	b.le	.hk
	add	x23,x23,1
	cmp	x23,68
	b.lo	.hd
	ldr	x0,[x28,g_fly]
	cbnz	x0,.out
	ldr	x19,[x28,g_cur]
	cbz	x19,.out
	sub	x19,x19,1
	add	x0,x28,g_shalo
	ldr	x0,[x0,x19,lsl 3]
	mov	x1,LX
	mov	x2,LY
	bl	centered_glow
	lsl	x0,x19,2
	add	x1,x28,g_sb
	ldr	x0,[x1,x0,lsl 3]
	mov	x1,LX
	mov	x2,LY
	bl	centered
.out:	ga_pop
	epilogue

draw_flying:
	ldr	x0,[x28,g_fly]
	cbz	x0,.out
	ldr	x0,[x28,g_cur]
	sub	x0,x0,1
	lsl	x0,x0,2
	add	x1,x28,g_sb
	ldr	x0,[x1,x0,lsl 3]
	ldr	x1,[x28,g_bx]
	asr	x1,x1,16
	ldr	x2,[x28,g_by]
	asr	x2,x2,16
	b	centered
.out:	ret

draw_title:
	prologue
	ga_push
	mov	x19,0				; sept bulles qui dansent
.b:	mov	x0,44
	mul	x20,x19,x0
	add	x20,x20,GF_W/2-132
	ldr	x0,[x28,g_tick]
	lsl	x0,x0,2
	mov	x1,36
	madd	x0,x19,x1,x0
	bl	ga_sin8
	lsr	x0,x0,4
	add	x2,x0,52
	lsl	x0,x19,2
	add	x1,x28,g_sb
	ldr	x0,[x1,x0,lsl 3]
	mov	x1,x20
	bl	centered
	add	x19,x19,1
	cmp	x19,NCOLOR
	b.lo	.b
	ldr	x0,[x28,g_tx+TX_LOGO*8]
	mov	x1,GF_W/2
	mov	x2,92
	bl	ga_show
	ldr	x0,[x28,g_tx+TX_SUB*8]
	mov	x1,GF_W/2
	mov	x2,194
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
	mov	x2,276
	bl	ga_show
	ldr	x0,[x28,g_best]
	mov	x1,GF_W/2+8
	mov	x2,274
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x0,[x28,g_tx+TX_HELP1*8]
	mov	x1,GF_W/2
	mov	x2,332
	bl	ga_show
	ldr	x0,[x28,g_tx+TX_HELP2*8]
	mov	x1,GF_W/2
	mov	x2,354
	bl	ga_show
	ldr	x0,[x28,g_tx+TX_HELP3*8]
	mov	x1,GF_W/2
	mov	x2,376
	bl	ga_show
	ga_pop
	epilogue

; x0 = titre TX_*, x1 = suite TX_*, x2 = 1 : le score (et le record), 2 : le bonus
box:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	add	x3,x28,g_tx
	ldr	x19,[x3,x0,lsl 3]
	ldr	x20,[x3,x1,lsl 3]
	mov	x21,x2
	mov	x0,150
	mov	x1,140
	mov	x2,340
	mov	x3,190
	mov	x4,18
	movl	w5,0x00283C64
	movl	w6,0x00121A30
	mov	x7,252
	movl	w8,0x0090A8D8
	bl	ga_panel
	mov	x0,x19
	mov	x1,GF_W/2
	mov	x2,154
	bl	ga_show
	cmp	x21,1
	b.ne	.bn
	ldr	x0,[x28,g_score]
	mov	x1,GF_W/2
	mov	x2,200
	add	x3,x28,g_dbig
	mov	x4,5
	bl	ga_num
	ldr	x0,[x28,g_newbest]
	cbz	x0,.a
	ldr	x0,[x28,g_tx+TX_NEWBEST*8]
	mov	x1,GF_W/2
	mov	x2,236
	bl	ga_show
	b	.a
.bn:	ldr	x0,[x28,g_tx+TX_BONUS*8]
	mov	x1,GF_W/2
	mov	x2,198
	bl	ga_show
	ldr	x0,[x28,g_bonus]
	mov	x1,GF_W/2
	mov	x2,224
	add	x3,x28,g_dgain
	mov	x4,5
	bl	ga_num
.a:	mov	x0,x20
	mov	x1,GF_W/2
	mov	x2,288
	bl	ga_show
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
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
	bl	deco_draw
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.ne	.game
	bl	draw_title
	b	.out
.game:	bl	draw_hud
	mov	x0,0				; un tir avant la descente : le plafond tremble
	ldr	x1,[x28,g_state]
	cmp	x1,ST_PLAY
	b.ne	.sk
	ldr	x1,[x28,g_every]
	ldr	x2,[x28,g_shots]
	sub	x1,x1,x2
	cmp	x1,1
	b.ne	.sk
	ldr	x1,[x28,g_tick]
	ubfx	x1,x1,2,1
	lsl	x1,x1,1
	sub	x0,x1,1
.sk:	str	x0,[x28,g_shk]
	bl	draw_ceiling
	bl	draw_grid
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.ne	.l
	ldr	x0,[x28,g_fly]
	cbnz	x0,.l
	bl	draw_aim
.l:	bl	draw_launcher
	bl	draw_flying
	bl	fx_draw
	bl	ga_parts_draw
	ldr	x19,[x28,g_flt]			; le gain qui monte
	cbz	x19,.st
	ldr	x0,[x28,g_fltv]
	cbz	x0,.st
	ldr	x1,[x28,g_fltx]
	ldr	x2,[x28,g_flty]
	sub	x2,x2,40
	add	x2,x2,x19,lsr 1
	add	x3,x28,g_dgain
	mov	x4,5
	bl	ga_num
.st:	ldr	x0,[x28,g_state]		; (la pause : gamepause)
	cmp	x0,ST_OVER
	b.ne	.cl
	mov	x0,TX_OVER
	mov	x1,TX_AGAIN
	mov	x2,1
	bl	box
	b	.out
.cl:	cmp	x0,ST_CLEAR
	b.ne	.bnr
	ldr	x1,[x28,g_timer]
	cmp	x1,30
	b.lo	.out
	mov	x0,TX_CLEAR
	mov	x1,TX_CONT
	mov	x2,2
	bl	box
	b	.out
.bnr:	cmp	x0,ST_PLAY			; le numero du niveau, au debut
	b.ne	.out
	ldr	x0,[x28,g_banner]
	cmp	x0,12
	b.lo	.out
	ldr	x19,[x28,g_tx+TX_LEVEL*8]
	ldr	w20,[x19]
	mov	x0,x19
	mov	x1,GF_W/2-16
	mov	x2,206
	bl	ga_show
	ldr	x0,[x28,g_level]
	mov	x1,GF_W/2-10
	add	x1,x1,x20,lsr 1
	mov	x2,211
	add	x3,x28,g_dgain
	mov	x4,4
	bl	ga_num
.out:	ga_pop
	epilogue

; ---------------------------------------------------------------------------
;  Tables
; ---------------------------------------------------------------------------
	align 4
colors	dw 0x00F04860,0x003C8CF0,0x00FFC83C,0x003CCC7C,0x00A860F0,0x00FF8838,0x0040E0E0
sizes	db BD,24,16,8
ring_sz	db 16,26,40
	align 4
snd_fall:
	gs_ent	0,700,70,AKW_SINE,80
	gs_ent	70,560,70,AKW_SINE,80
	gs_ent	140,440,90,AKW_SINE,80
	gs_ent	230,330,140,AKW_SINE,70
	gs_end
snd_drop:
	gs_ent	0,98,220,AKW_SQUARE,90
	gs_ent	40,73,260,AKW_TRIANGLE,110
	gs_end
	align 4
tx_table:
	ga_txe	s_logo,s_base,76,1,0x00FFFFFF,0x0060B4FF
	ga_txe	s_sub,s_base,15,0,0x00D8E0F4,0x00A8B4D0
	ga_txe	s_prompt,s_base,18,1,0x00FFFFFF,0x00D0E0FF
	ga_txe	s_best,s_base,15,1,0x00C8D4F0,0x00A0B0D0
	ga_txe	s_over,s_base,26,1,0x00FFD878,0x00F09040
	ga_txe	s_again,s_base,13,0,0x00D8E0F4,0x00A8B4D0
	ga_txe	s_newbest,s_base,15,1,0x00FFE080,0x00F0B040
	ga_txe	s_clear,s_base,26,1,0x00C8FFD0,0x0048D088
	ga_txe	s_bonus,s_base,15,1,0x00FFE8A0,0x00F0C060
	ga_txe	s_cont,s_base,13,0,0x00D8E0F4,0x00A8B4D0
	ga_txe	s_level,s_base,28,1,0x00FFFFFF,0x0080C0FF
	ga_txe	s_help1,s_base,13,0,0x00B8C4E0,0x0098A4C4
	ga_txe	s_help2,s_base,13,0,0x00B8C4E0,0x0098A4C4
	ga_txe	s_help3,s_base,13,0,0x00B8C4E0,0x0098A4C4
s_base:
s_logo		db 'Bubbles',0
s_sub		db 'Visez, tirez, faites ',0xC3,0xA9,'clater les bulles',0
s_prompt	db 'Espace, A ou toucher pour jouer',0
s_over		db 'Partie termin',0xC3,0xA9,'e',0
s_again		db 'Espace, A ou toucher : rejouer',0
s_newbest	db 'Nouveau record !',0
s_clear		db 'Niveau r',0xC3,0xA9,'ussi !',0
s_bonus		db 'Bonus',0
s_cont		db 'Espace, A ou toucher : continuer',0
s_help1		db 'Gauche, droite ou souris : viser',0
s_help2		db 'Espace, haut, A ou rel',0xC3,0xA2,'cher : tirer',0
s_help3		db 'Bas, X, Y ou toucher la suivante : ',0xC3,0xA9,'changer',0
s_title		db 'Bubbles',0
s_score		db 'Score',0
s_level		db 'Niveau',0
s_best		db 'Record',0
s_drop		db 'Descente',0
s_next		db 'Suivante',0
p_dir		db 'SD0:/home/bubbles',0
p_best		db 'SD0:/home/bubbles/best.txt',0
s_menus		db 'MPartie',10
		db 'I1',9,'Nouvelle partie',9,'Ctrl+N',10
		db 'I2',9,'Pause',9,'P',10
		db 'I3c',9,'Son',10
		db '-',10
		db 'I4',9,'Quitter',10,0
	align 4
; sin(k / 2 degres) en 16.16, k = 0..180
sin_tab:
	dw	0,572,1144,1716,2287,2859,3430,4001
	dw	4572,5142,5712,6281,6850,7419,7987,8554
	dw	9121,9687,10252,10817,11380,11943,12505,13066
	dw	13626,14185,14742,15299,15855,16409,16962,17514
	dw	18064,18613,19161,19707,20252,20795,21336,21876
	dw	22415,22951,23486,24019,24550,25080,25607,26132
	dw	26656,27177,27697,28214,28729,29242,29753,30261
	dw	30767,31271,31772,32271,32768,33262,33754,34242
	dw	34729,35212,35693,36172,36647,37120,37590,38057
	dw	38521,38982,39441,39896,40348,40797,41243,41686
	dw	42126,42562,42995,43425,43852,44275,44695,45112
	dw	45525,45935,46341,46744,47143,47538,47930,48318
	dw	48703,49084,49461,49834,50203,50569,50931,51289
	dw	51643,51993,52339,52682,53020,53354,53684,54010
	dw	54332,54650,54963,55273,55578,55879,56175,56468
	dw	56756,57040,57319,57594,57865,58131,58393,58650
	dw	58903,59152,59396,59635,59870,60100,60326,60547
	dw	60764,60976,61183,61386,61584,61777,61966,62149
	dw	62328,62503,62672,62837,62997,63152,63303,63449
	dw	63589,63725,63856,63983,64104,64220,64332,64439
	dw	64540,64637,64729,64816,64898,64975,65048,65115
	dw	65177,65234,65287,65334,65376,65414,65446,65474
	dw	65496,65514,65526,65534,65536
	align 4

segment readable writeable
kit_data
		gs_data
		align 8
g_data		rb G_SIZE
g_fx		rb FXN*32		; +0 x, +4 y (16.16), +8 vx, +12 vy, +16 type,
					; +20 couleur, +24 minuterie
ui_data
ga_data
gp_data
gf_data
