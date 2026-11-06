; ============================================================================
;  snake - le Snake d'Onyx (user/Apps/snake) : 24 x 18 cases, 4 anneaux au depart,
;  10 points par pomme, une case toutes les 8 - score/60 images (3 au mieux), le
;  mur ou le corps mettent fin a la partie. Memes regles, habillees : serpent
;  qui ondule d'une case a l'autre, herbe generee, pomme qui bat, eclats, son.
;
;  Fleches ; un appui a cote de la tete, ou un glissement, tourne ; P : pause ;
;  R : recommencer ; manette : croix, A joue. Plein ecran ; Start, Echap ou le
;  bouton || : le menu de pause (gamepause.inc). Taille logique 640 x 480.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'
include '../../inc/pad.inc'

GF_W		= 640
GF_H		= 480
GW		= 24
GH		= 18
NCELLS		= GW*GH
CELL		= 24
OX		= 32
OY		= 38
SEG		= 23			; diametre du corps
NSZ		= 3			; tailles du corps (vers la queue)
SWIPE		= 18			; glissement qui compte

ST_TITLE	= 0
ST_PLAY		= 1
ST_PAUSE	= 2
ST_DYING	= 3
ST_OVER		= 4

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
TX_OVER		= 6
TX_AGAIN	= 7
TX_NEWBEST	= 8
NTX		= 9

virtual at 0
g_state		rd 1
g_timer		rd 1
g_tick		rd 1
g_len		rd 1
g_dx		rd 1
g_dy		rd 1
g_ndx		rd 1
g_ndy		rd 1
g_fx		rd 1
g_fy		rd 1
g_tailx		rd 1			; d'ou vient le dernier anneau
g_taily		rd 1
g_score		rd 1
g_show		rd 1
g_best		rd 1
g_newbest	rd 1
g_frames	rd 1
g_last		rd 1
g_speed		rd 1
g_dead		rd 1			; anneaux gris (depuis la tete)
g_shake		rd 1
g_gulp		rd 1			; la bouchee descend le long du corps : anneau + 1
g_flt		rd 1
g_fltx		rd 1
g_flty		rd 1
g_ax		rd 1			; ancre d'un glissement
g_ay		rd 1
g_down		rd 1
g_pad		rd 1
g_ldirty	rd 1
g_sbody		rd NSZ*3		; tailles x clair, sombre, gris
g_shead		rd 2
g_seye		rd 1
g_spupil	rd 1
g_sapple	rd 1
g_sleaf		rd 1
g_shalo		rd 1
g_sshad		rd 1
g_sorb		rd 2
g_sbulge	rd 1
g_dbig		rd 10
g_dsmall	rd 10
g_dgain		rd 10
g_tx		rd NTX
g_sx		rb NCELLS+1
g_sy		rb NCELLS+1
		align 8
g_ptx		rb (NCELLS+2)*4		; centres a l'ecran, l'anneau i
g_pty		rb (NCELLS+2)*4
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
	mov	x3,GF_W-32
	mov	x4,1
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
;  Sprites et couche
; ---------------------------------------------------------------------------
gen_sprites:
	prologue
	ga_push
	add	x19,x28,g_sbody
	mov	x20,0				; teinte
.t:	mov	x21,0				; taille
.s:	mov	x0,3
	mul	x0,x21,x0
	mov	x1,SEG
	sub	x0,x1,x0
	adr	x1,body_col
	ldr	w1,[x1,x20,lsl 2]
	bl	ga_ball
	str	x0,[x19],8
	add	x21,x21,1
	cmp	x21,NSZ
	b.lo	.s
	add	x20,x20,1
	cmp	x20,3
	b.lo	.t
	mov	x0,SEG+3
	movl	w1,0x0060D860
	bl	ga_ball
	str	x0,[x28,g_shead]
	mov	x0,SEG+3
	movl	w1,0x00787C78
	bl	ga_ball
	str	x0,[x28,g_shead+8]
	mov	x0,SEG+5
	movl	w1,0x0078E068
	bl	ga_ball
	str	x0,[x28,g_sbulge]
	mov	x0,9
	movl	w1,0x00F0F4E8
	bl	ga_ball
	str	x0,[x28,g_seye]
	mov	x0,5
	movl	w1,0x00181810
	bl	ga_ball
	str	x0,[x28,g_spupil]
	mov	x0,CELL-3
	movl	w1,0x00E83830
	bl	ga_ball
	str	x0,[x28,g_sapple]
	mov	x0,11				; la feuille
	mov	x1,6
	mov	x2,3
	movl	w3,0x0048B840
	bl	ga_block
	str	x0,[x28,g_sleaf]
	mov	x0,SEG
	movl	w1,0x00404040
	bl	ga_ball
	str	x0,[x28,g_sshad]
	mov	x1,120
	bl	ga_fade
	mov	x0,44
	movl	w1,0x00502018
	bl	ga_disc
	str	x0,[x28,g_shalo]
	mov	x0,170
	movl	w1,0x000C1C0C
	bl	ga_disc
	str	x0,[x28,g_sorb]
	mov	x0,130
	movl	w1,0x00081C10
	bl	ga_disc
	str	x0,[x28,g_sorb+8]
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	add	x0,x28,g_dbig
	mov	x1,19
	adr	x2,ui_font_b
	movl	w3,0x00FFFFFF
	movl	w4,0x00C8F0B8
	bl	ga_digits
	add	x0,x28,g_dgain
	mov	x1,18
	adr	x2,ui_font_b
	movl	w3,0x00FFF4B0
	movl	w4,0x00FFB040
	bl	ga_digits
	adr	x0,tx_table
	adr	x1,s_base
	add	x2,x28,g_tx
	mov	x3,NTX
	bl	ga_texts
	bl	ui_canvas_end
	ga_pop
	epilogue

; l'herbe, les cases, l'ombre des bords
gen_grass:
	prologue
	ga_push
	ldv	x25,gf_layer
	mov	x19,0				; ligne
.y:	mov	x20,0				; colonne
.x:	mov	x0,x20				; de grandes taches
	mov	x1,x19
	mov	x2,5
	bl	ga_noise2
	mov	x21,x0
	mov	x0,7				; des brins : du bruit etire en hauteur
	mul	x0,x20,x0
	lsr	x1,x19,2
	mov	x2,31
	mul	x1,x1,x2
	add	x0,x0,x1
	add	x1,x19,x20,lsl 2
	eor	x0,x0,x1,lsl 3
	and	x0,x0,4095
	adr	x1,ga_nz
	ldrb	w0,[x1,x0]
	sub	x22,x0,128
	asr	x22,x22,2
	mov	x3,CELL
	subs	x0,x20,OX
	csel	x0,xzr,x0,lo
	udiv	x1,x0,x3
	subs	x0,x19,OY
	csel	x0,xzr,x0,lo
	udiv	x2,x0,x3
	add	x1,x1,x2
	movl	w0,0x00306C34			; deux verts, en damier
	tbz	x1,0,.c
	movl	w0,0x00387C3C
.c:	sub	x1,x21,128
	asr	x1,x1,2
	add	x1,x1,x22
	add	x1,x1,240
	bl	ga_shade
	mov	w22,w0
	; plus sombre vers les bords
	mov	x0,GF_W/2
	sub	x0,x20,x0
	mov	x1,GF_H/2
	sub	x1,x19,x1
	mul	x0,x0,x0
	madd	x0,x1,x1,x0
	lsr	x0,x0,11
	mov	x1,280
	subs	x1,x1,x0
	mov	x0,120
	csel	x1,x0,x1,lo
	cmp	x1,x0
	csel	x1,x0,x1,lo
	mov	w0,w22
	bl	ga_shade
	str	w0,[x25],4
	add	x20,x20,1
	cmp	x20,GF_W
	b.lo	.x
	add	x19,x19,1
	cmp	x19,GF_H
	b.lo	.y
	ga_pop
	epilogue

build_layer:
	prologue
	ga_push
	str	xzr,[x28,g_ldirty]
	bl	gen_grass
	ldv	x0,gf_layer
	stv	x0,gf_dst
	; le bandeau du haut et le muret
	mov	x0,0
	mov	x1,0
	mov	x2,GF_W
	mov	x3,OY-9
	mov	x4,0
	movl	w5,0x001C2E1C
	movl	w6,0x00142014
	mov	x7,256
	mov	w8,0
	bl	ga_panel
	mov	x19,0
.w:	adr	x20,walls
	add	x20,x20,x19,lsl 3
	ldrh	w0,[x20]
	ldrh	w1,[x20,2]
	ldrh	w2,[x20,4]
	ldrh	w3,[x20,6]
	mov	x4,3
	movl	w5,0x00787058
	movl	w6,0x00484030
	mov	x7,256
	movl	w8,0x00A8A080
	bl	ga_panel
	add	x19,x19,1
	cmp	x19,4
	b.lo	.w
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.ne	.hud
	mov	x0,110
	mov	x1,92
	mov	x2,420
	mov	x3,230
	mov	x4,20
	movl	w5,0x00142814
	movl	w6,0x000C160C
	mov	x7,175
	movl	w8,0x0080A070
	bl	ga_panel
	b	.done
.hud:	ldv	x0,gf_cv
	bl	ui_canvas_begin
	adr	x0,s_score
	mov	x1,OX-4
	mov	x2,20
	movl	w3,0x00B8C8B0
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	adr	x0,s_len
	mov	x1,190
	mov	x2,20
	movl	w3,0x00B8C8B0
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	adr	x0,s_best
	mov	x1,350
	mov	x2,20
	movl	w3,0x00B8C8B0
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

place_food:
	prologue
	stp	x19,x20,[sp,-16]!
.try:	mov	x0,GW
	bl	gf_below
	mov	x19,x0
	mov	x0,GH
	bl	gf_below
	mov	x20,x0
	add	x2,x28,g_sx
	add	x3,x28,g_sy
	ldr	x4,[x28,g_len]
	mov	x5,0
.c:	ldrb	w6,[x2,x5]
	cmp	x6,x19
	b.ne	.n
	ldrb	w6,[x3,x5]
	cmp	x6,x20
	b.eq	.try
.n:	add	x5,x5,1
	cmp	x5,x4
	b.lo	.c
	str	x19,[x28,g_fx]
	str	x20,[x28,g_fy]
	ldp	x19,x20,[sp],16
	epilogue

new_game:
	prologue
	mov	x0,4
	str	x0,[x28,g_len]
	mov	x0,0
	add	x2,x28,g_sx
	add	x3,x28,g_sy
.b:	mov	x1,GW/2
	sub	x1,x1,x0
	strb	w1,[x2,x0]
	mov	x1,GH/2
	strb	w1,[x3,x0]
	add	x0,x0,1
	cmp	x0,4
	b.lo	.b
	mov	x0,GW/2-4
	str	x0,[x28,g_tailx]
	mov	x0,GH/2
	str	x0,[x28,g_taily]
	mov	x0,1
	str	x0,[x28,g_dx]
	str	x0,[x28,g_ndx]
	str	xzr,[x28,g_dy]
	str	xzr,[x28,g_ndy]
	str	xzr,[x28,g_score]
	str	xzr,[x28,g_show]
	str	xzr,[x28,g_newbest]
	str	xzr,[x28,g_frames]
	str	xzr,[x28,g_last]
	str	xzr,[x28,g_dead]
	str	xzr,[x28,g_gulp]
	str	xzr,[x28,g_flt]
	mov	x0,8
	str	x0,[x28,g_speed]
	bl	ga_parts_clear
	bl	place_food
	mov	x0,ST_PLAY
	bl	set_state
	mov	x0,1
	str	x0,[x28,g_ldirty]
	epilogue

die:
	prologue
	mov	x0,ST_DYING
	bl	set_state
	mov	x0,6
	str	x0,[x28,g_shake]
	ldr	x2,[x28,g_score]
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
	epilogue

; une case d'avance (step d'Onyx)
advance:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x0,[x28,g_ndx]
	str	x0,[x28,g_dx]
	ldr	x1,[x28,g_ndy]
	str	x1,[x28,g_dy]
	add	x2,x28,g_sx
	add	x3,x28,g_sy
	ldrb	w4,[x2]
	ldrb	w5,[x3]
	add	x19,x4,x0			; nx
	add	x20,x5,x1			; ny
	cmp	x19,GW
	b.hs	.dead				; (negatif compris)
	cmp	x20,GH
	b.hs	.dead
	ldr	x21,[x28,g_len]
	ldr	x4,[x28,g_fx]
	ldr	x5,[x28,g_fy]
	cmp	x19,x4
	cset	x22,eq
	cmp	x20,x5
	csel	x22,x22,xzr,eq			; 1 : la pomme
	mov	x6,0
.c:	sub	x7,x21,1
	cmp	x6,x7
	b.ne	.t
	cbz	x22,.nx				; (la queue s'en va)
.t:	ldrb	w7,[x2,x6]
	cmp	x7,x19
	b.ne	.nx
	ldrb	w7,[x3,x6]
	cmp	x7,x20
	b.eq	.dead
.nx:	add	x6,x6,1
	cmp	x6,x21
	b.lo	.c
	sub	x6,x21,1			; d'ou repart la queue
	ldrb	w4,[x2,x6]
	str	x4,[x28,g_tailx]
	ldrb	w4,[x3,x6]
	str	x4,[x28,g_taily]
	cbz	x22,.sh
	cmp	x21,NCELLS
	b.hs	.sh
	add	x21,x21,1
	str	x21,[x28,g_len]
.sh:	sub	x6,x21,1
.m:	cbz	x6,.h
	sub	x7,x6,1
	ldrb	w4,[x2,x7]
	strb	w4,[x2,x6]
	ldrb	w4,[x3,x7]
	strb	w4,[x3,x6]
	mov	x6,x7
	b	.m
.h:	strb	w19,[x2]
	strb	w20,[x3]
	cbz	x22,.ok
	ldr	x0,[x28,g_score]
	add	x0,x0,10
	str	x0,[x28,g_score]
	mov	x0,70				; (une case toutes les 8 - score/60 images)
	bl	eat
	bl	place_food
.ok:	ldr	x0,[x28,g_score]
	mov	x1,60
	udiv	x0,x0,x1
	mov	x1,5
	cmp	x0,x1
	csel	x0,x1,x0,hi
	mov	x1,8
	sub	x0,x1,x0
	str	x0,[x28,g_speed]
	b	.out
.dead:	bl	die
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la pomme est mangee
eat:
	prologue
	mov	x0,60
	str	x0,[x28,g_flt]
	ldr	x0,[x28,g_fx]
	mov	x2,CELL
	mul	x0,x0,x2
	add	x0,x0,OX+CELL/2
	str	x0,[x28,g_fltx]
	ldr	x1,[x28,g_fy]
	mul	x1,x1,x2
	add	x1,x1,OY+CELL/2
	str	x1,[x28,g_flty]
	movl	w2,0x00F05040
	mov	x3,26
	movl	x4,200000
	mov	x5,20
	mov	x6,3
	movl	x7,6000
	bl	ga_burst
	ldr	x0,[x28,g_fltx]
	ldr	x1,[x28,g_flty]
	movl	w2,0x0080E070
	mov	x3,10
	movl	x4,150000
	mov	x5,20
	mov	x6,2
	mov	x7,4000
	bl	ga_burst
	mov	x0,1
	str	x0,[x28,g_gulp]
	mov	x0,0
	mov	x1,660
	mov	x2,50
	mov	x3,AKW_TRIANGLE
	mov	x4,110
	bl	gs_later
	mov	x0,50
	mov	x1,990
	mov	x2,70
	mov	x3,AKW_TRIANGLE
	mov	x4,110
	bl	gs_later
	epilogue

; x0, x1 = direction voulue : pas de demi-tour
turn:
	ldr	x2,[x28,g_dx]
	cbz	x0,.v
	cbnz	x2,.out
	b	.set
.v:	ldr	x3,[x28,g_dy]
	cbnz	x3,.out
.set:	str	x0,[x28,g_ndx]
	str	x1,[x28,g_ndy]
.out:	ret

do_pause:
	b	gp_open

; (gamepause) la partie s'arrete -> x0 = 1 si elle etait en cours
game_pause:
	prologue
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

step:
	prologue
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
	b.ls	.f
	mov	x2,1
	add	x0,x0,x2
	str	x0,[x28,g_show]
.f:	ldr	x0,[x28,g_flt]
	cbz	x0,.sh
	sub	x0,x0,1
	str	x0,[x28,g_flt]
.sh:	ldr	x0,[x28,g_shake]
	cbz	x0,.st
	sub	x0,x0,1
	str	x0,[x28,g_shake]
.st:	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.ne	.dy
	ldr	x0,[x28,g_frames]
	add	x0,x0,1
	str	x0,[x28,g_frames]
	ldr	x1,[x28,g_last]
	sub	x0,x0,x1
	ldr	x1,[x28,g_speed]
	cmp	x0,x1
	b.lo	.out
	ldr	x0,[x28,g_frames]
	str	x0,[x28,g_last]
	ldr	x0,[x28,g_gulp]			; la bouchee avance d'un anneau
	cbz	x0,.ad
	add	x0,x0,1
	ldr	x1,[x28,g_len]
	cmp	x0,x1
	csel	x0,xzr,x0,hs
	str	x0,[x28,g_gulp]
.ad:	bl	advance
	b	.out
.dy:	cmp	x0,ST_DYING
	b.ne	.out
	ldr	x0,[x28,g_tick]
	tbz	x0,0,.out
	ldr	x0,[x28,g_dead]
	add	x0,x0,2
	str	x0,[x28,g_dead]
	ldr	x1,[x28,g_len]
	cmp	x0,x1
	b.lo	.out
	mov	x0,ST_OVER
	bl	set_state
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

; -> x0 = 1 si l'appui a servi (titre, fin, pause)
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
	bl	do_pause
	b	.out
.r:	cmp	w0,'r'
	b.ne	.c
	ldr	x0,[x28,g_state]
	cbz	x0,.out
	bl	new_game
	b	.out
.c:	ldr	w2,[x1,EV_CODE]
	cmp	w2,KEY_SPACE
	b.eq	.sp
	cmp	w2,KEY_ENTER
	b.ne	.l
.sp:	bl	start_act
	b	.out
.l:	cmp	w2,KEY_LEFT
	b.ne	.ri
	mov	x0,-1
	mov	x1,0
	b	.t
.ri:	cmp	w2,KEY_RIGHT
	b.ne	.u
	mov	x0,1
	mov	x1,0
	b	.t
.u:	cmp	w2,KEY_UP
	b.ne	.d
	mov	x0,0
	mov	x1,-1
	b	.t
.d:	cmp	w2,KEY_DOWN
	b.ne	.out
	mov	x0,0
	mov	x1,1
.t:	bl	turn
.out:	epilogue

; un appui a cote de la tete : tourner de ce cote
tap_turn:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	add	x2,x28,g_sx
	ldrb	w2,[x2]
	mov	x3,CELL
	mul	x2,x2,x3
	add	x2,x2,OX+CELL/2
	add	x4,x28,g_sy
	ldrb	w4,[x4]
	mul	x4,x4,x3
	add	x4,x4,OY+CELL/2
	ldr	x5,[x28,g_dx]
	cbz	x5,.h
	mov	x0,0				; on va de cote : monter ou descendre
	mov	x1,1
	cmp	x20,x4
	b.ge	.t
	mov	x1,-1
	b	.t
.h:	mov	x1,0
	mov	x0,1
	cmp	x19,x2
	b.ge	.t
	mov	x0,-1
.t:	bl	turn
	ldp	x19,x20,[sp],16
	epilogue

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
	ldr	x0,[x28,g_down]			; un glissement
	cbz	x0,.out
	ldr	x0,[x28,g_ax]
	ldr	x1,[x28,g_ay]
	sub	x0,x19,x0
	sub	x1,x20,x1
	cmp	x0,0
	cneg	x2,x0,lt
	cmp	x1,0
	cneg	x3,x1,lt
	cmp	x2,x3
	csel	x4,x2,x3,hi
	cmp	x4,SWIPE
	b.lo	.out
	str	x19,[x28,g_ax]
	str	x20,[x28,g_ay]
	cmp	x2,x3
	b.hi	.hz
	asr	x1,x1,63
	orr	x1,x1,1
	mov	x0,0
	bl	turn
	b	.out
.hz:	asr	x0,x0,63
	orr	x0,x0,1
	mov	x1,0
	bl	turn
	b	.out
.press:	ldr	w3,[x2,EV_CODE]
	tbz	w3,0,.out
	mov	x0,OY-8
	bl	gf_drag_top
	cbnz	x0,.out
	bl	start_act
	cbnz	x0,.out
	mov	x0,1
	str	x0,[x28,g_down]
	str	x19,[x28,g_ax]
	str	x20,[x28,g_ay]
	mov	x0,x19
	mov	x1,x20
	bl	tap_turn
	b	.out
.rel:	str	xzr,[x28,g_down]
.out:	ldp	x19,x20,[sp],16
	epilogue

on_pad:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,ui_ev
	ldr	w1,[x0,4]
	cbz	w1,.out
	ldv	x20,gp_new			; (Start : gamepause)
	tst	x20,PAD_A
	b.eq	.l
	bl	start_act
.l:	tst	x20,PAD_LEFT
	b.eq	.r
	mov	x0,-1
	mov	x1,0
	bl	turn
.r:	tst	x20,PAD_RIGHT
	b.eq	.u
	mov	x0,1
	mov	x1,0
	bl	turn
.u:	tst	x20,PAD_UP
	b.eq	.d
	mov	x0,0
	mov	x1,-1
	bl	turn
.d:	tst	x20,PAD_DOWN
	b.eq	.out
	mov	x0,0
	mov	x1,1
	bl	turn
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

; x0 = sprite, x1, x2 = centre : son ombre, decalee
centered_sh:
	add	x1,x1,3
	add	x2,x2,4
	ldr	x0,[x28,g_sshad]
	b	centered

; x0 = anneau -> x0 = sprite (taille selon la distance a la queue, teinte en bandes)
seg_sprite:
	ldr	x1,[x28,g_len]
	sub	x1,x1,x0			; 1 : la queue
	mov	x2,0
	cmp	x1,3
	b.hi	.t
	mov	x2,1
	cmp	x1,1
	b.hi	.t
	mov	x2,2
.t:	lsr	x3,x0,1
	and	x3,x3,1				; bandes
	ldr	x4,[x28,g_state]
	cmp	x4,ST_DYING
	b.lo	.c
	ldr	x4,[x28,g_dead]
	cmp	x0,x4
	b.hs	.c
	mov	x3,2
.c:	mov	x4,3
	madd	x3,x3,x4,x2
	add	x4,x28,g_sbody
	ldr	x0,[x4,x3,lsl 3]
	ret

; les centres de tous les anneaux, a l'image courante
snake_points:
	mov	x9,0				; fraction du pas
	ldr	x10,[x28,g_speed]
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.eq	.frac
	cmp	x0,ST_PAUSE
	b.ne	.fin
.frac:	ldr	x9,[x28,g_frames]
	ldr	x0,[x28,g_last]
	sub	x9,x9,x0
	b	.go
.fin:	mov	x9,x10
.go:	add	x11,x28,g_sx
	add	x12,x28,g_sy
	add	x13,x28,g_ptx
	add	x14,x28,g_pty
	ldr	x15,[x28,g_len]
	mov	x0,0				; anneau
.a:	ldrb	w1,[x11,x0]			; ou il va
	ldrb	w2,[x12,x0]
	add	x3,x0,1
	cmp	x3,x15
	b.lo	.pv
	ldr	x4,[x28,g_tailx]
	ldr	x5,[x28,g_taily]
	b	.lerp
.pv:	ldrb	w4,[x11,x3]			; d'ou il vient
	ldrb	w5,[x12,x3]
.lerp:	mov	x3,CELL
	mul	x1,x1,x3
	mul	x2,x2,x3
	mul	x4,x4,x3
	mul	x5,x5,x3
	sub	x1,x1,x4
	sub	x2,x2,x5
	mul	x1,x1,x9
	mul	x2,x2,x9
	sdiv	x1,x1,x10
	sdiv	x2,x2,x10
	add	x1,x1,x4
	add	x2,x2,x5
	add	x1,x1,OX+CELL/2
	add	x2,x2,OY+CELL/2
	str	w1,[x13,x0,lsl 2]
	str	w2,[x14,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,x15
	b.lo	.a
	ret

draw_snake:
	prologue
	ga_push
	bl	snake_points
	add	x21,x28,g_ptx
	add	x22,x28,g_pty
	ldr	x19,[x28,g_len]
	sub	x19,x19,1
	mov	x20,x19				; d'abord les ombres
.s:	mov	x0,x20
	bl	seg_sprite
	ldrsw	x1,[x21,x20,lsl 2]
	ldrsw	x2,[x22,x20,lsl 2]
	bl	centered_sh
	subs	x20,x20,1
	b.pl	.s
	mov	x20,x19				; de la queue vers la tete
.b:	cbz	x20,.head
	mov	x0,x20
	bl	seg_sprite
	mov	x23,x0
	ldrsw	x1,[x21,x20,lsl 2]
	ldrsw	x2,[x22,x20,lsl 2]
	mov	x0,x23
	bl	centered
	ldr	x0,[x28,g_gulp]			; la bouchee fait une bosse
	cmp	x0,x20
	b.ne	.mid
	ldr	x0,[x28,g_sbulge]
	ldrsw	x1,[x21,x20,lsl 2]
	ldrsw	x2,[x22,x20,lsl 2]
	bl	centered
.mid:	sub	x0,x20,1			; un anneau entre deux : corps continu
	ldrsw	x1,[x21,x20,lsl 2]
	ldrsw	x3,[x21,x0,lsl 2]
	add	x1,x1,x3
	asr	x1,x1,1
	ldrsw	x2,[x22,x20,lsl 2]
	ldrsw	x3,[x22,x0,lsl 2]
	add	x2,x2,x3
	asr	x2,x2,1
	mov	x0,x23
	bl	centered
	sub	x20,x20,1
	b	.b
.head:	ldrsw	x23,[x21]			; la tete
	ldrsw	x24,[x22]
	ldr	x25,[x28,g_dx]
	ldr	x26,[x28,g_dy]
	ldr	x0,[x28,g_tick]			; la langue
	and	x0,x0,63
	cmp	x0,12
	b.hs	.he
	ldr	x0,[x28,g_state]
	cmp	x0,ST_DYING
	b.hs	.he
	mov	x0,14
	mul	x0,x25,x0
	add	x0,x0,x23
	mov	x1,14
	mul	x1,x26,x1
	add	x1,x1,x24
	sub	x0,x0,1
	sub	x1,x1,1
	mov	x2,3
	mov	x3,3
	movl	w4,0x00E83040
	bl	gf_rect
.he:	mov	x0,0
	ldr	x1,[x28,g_state]
	cmp	x1,ST_DYING
	b.lo	.hs
	mov	x0,1
.hs:	add	x1,x28,g_shead
	ldr	x0,[x1,x0,lsl 3]
	mov	x1,x23
	mov	x2,x24
	bl	centered
	mov	x19,-1				; les yeux, de chaque cote du sens de marche
.e:	mov	x0,5
	mul	x20,x25,x0			; devant
	mul	x21,x26,x0
	mul	x1,x26,x19			; a cote
	mul	x1,x1,x0
	mul	x2,x25,x19
	mul	x2,x2,x0
	add	x20,x20,x1
	add	x20,x20,x23
	add	x21,x21,x2
	add	x21,x21,x24
	ldr	x0,[x28,g_seye]
	mov	x1,x20
	mov	x2,x21
	bl	centered
	ldr	x0,[x28,g_spupil]
	add	x1,x25,x25
	add	x1,x1,x20
	add	x2,x26,x26
	add	x2,x2,x21
	bl	centered
	add	x19,x19,2
	cmp	x19,1
	b.le	.e
	ga_pop
	epilogue

draw_apple:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x19,[x28,g_fx]
	mov	x0,CELL
	mul	x19,x19,x0
	add	x19,x19,OX+CELL/2
	ldr	x20,[x28,g_fy]
	mul	x20,x20,x0
	add	x20,x20,OY+CELL/2
	ldr	x0,[x28,g_tick]
	lsl	x0,x0,2
	bl	ga_sin8
	lsr	x0,x0,5				; 0..7 : elle saute
	sub	x20,x20,x0
	ldr	x0,[x28,g_shalo]
	mov	x1,x19
	add	x2,x20,3
	ldr	w3,[x0]
	lsr	x3,x3,1
	sub	x1,x1,x3
	sub	x2,x2,x3
	mov	x3,x0
	mov	x0,x1
	mov	x1,x2
	mov	x2,x3
	bl	gf_glow
	ldr	x0,[x28,g_sapple]
	mov	x1,x19
	add	x2,x20,4
	bl	centered_sh
	ldr	x0,[x28,g_sapple]
	mov	x1,x19
	mov	x2,x20
	bl	centered
	add	x0,x19,1			; la queue et la feuille
	sub	x1,x20,14
	mov	x2,2
	mov	x3,5
	movl	w4,0x00503820
	bl	gf_rect
	add	x0,x19,3
	sub	x1,x20,15
	ldr	x2,[x28,g_sleaf]
	bl	ga_ablit
	ldp	x19,x20,[sp],16
	epilogue

draw_title:
	prologue
	ga_push
	; un serpent ondule en bas de l'ecran
	ldr	x19,[x28,g_tick]
	mov	x20,26				; anneaux
.b:	lsl	x0,x20,4
	add	x0,x0,x19,lsl 1
	bl	ga_sin8
	lsr	x0,x0,3
	add	x24,x0,350
	mov	x0,20
	mul	x0,x20,x0
	add	x0,x0,x19
	mov	x1,560+640
	udiv	x2,x0,x1
	msub	x23,x2,x1,x0
	sub	x23,x23,280
	lsr	x0,x20,1
	and	x0,x0,1
	mov	x1,3
	mul	x0,x0,x1
	add	x1,x28,g_sbody
	ldr	x25,[x1,x0,lsl 3]
	mov	x0,x25
	mov	x1,x23
	mov	x2,x24
	bl	centered
	subs	x20,x20,1
	b.ne	.b
	ldr	x0,[x28,g_tx+TX_LOGO*8]
	mov	x1,GF_W/2
	mov	x2,112
	bl	ga_show
	ldr	x0,[x28,g_tx+TX_SUB*8]
	mov	x1,GF_W/2
	mov	x2,206
	bl	ga_show
	ldr	x0,[x28,g_tick]
	lsl	x0,x0,1
	bl	ga_sin8
	cmp	x0,40
	b.lo	.r
	ldr	x0,[x28,g_tx+TX_PROMPT*8]
	mov	x1,GF_W/2
	mov	x2,246
	bl	ga_show
.r:	ldr	x0,[x28,g_tx+TX_RECORD*8]
	mov	x1,GF_W/2-40
	mov	x2,284
	bl	ga_show
	ldr	x0,[x28,g_best]
	mov	x1,GF_W/2+8
	mov	x2,282
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ga_pop
	epilogue

; x0 = titre TX_*, x1 = suite TX_*, x2 = 1 : montrer le score
box:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	add	x3,x28,g_tx
	ldr	x19,[x3,x0,lsl 3]
	ldr	x20,[x3,x1,lsl 3]
	mov	x21,x2
	mov	x0,150
	mov	x1,160
	mov	x2,340
	mov	x3,170
	mov	x4,16
	movl	w5,0x00203820
	movl	w6,0x00101C10
	mov	x7,235
	movl	w8,0x0090B080
	bl	ga_panel
	mov	x0,x19
	mov	x1,GF_W/2
	mov	x2,176
	bl	ga_show
	cbz	x21,.a
	ldr	x0,[x28,g_score]
	mov	x1,GF_W/2
	mov	x2,216
	add	x3,x28,g_dbig
	mov	x4,5
	bl	ga_num
	ldr	x0,[x28,g_newbest]
	cbz	x0,.a
	ldr	x0,[x28,g_tx+TX_NEWBEST*8]
	mov	x1,GF_W/2
	mov	x2,250
	bl	ga_show
.a:	mov	x0,x20
	mov	x1,GF_W/2
	mov	x2,290
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
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.ne	.game
	bl	draw_title
	b	.out
.game:	ldr	x0,[x28,g_show]
	mov	x1,74
	mov	x2,2
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x0,[x28,g_len]
	mov	x1,270
	mov	x2,2
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x0,[x28,g_best]
	mov	x1,406
	mov	x2,2
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	bl	draw_apple
	bl	draw_snake
	bl	ga_parts_draw
	ldr	x19,[x28,g_flt]
	cbz	x19,.st
	mov	x0,10
	ldr	x1,[x28,g_fltx]
	ldr	x2,[x28,g_flty]
	sub	x2,x2,80
	add	x2,x2,x19
	add	x3,x28,g_dgain
	mov	x4,5
	bl	ga_num
.st:	ldr	x0,[x28,g_state]		; (la pause : gamepause)
	cmp	x0,ST_OVER
	b.ne	.out
	mov	x0,TX_OVER
	mov	x1,TX_AGAIN
	mov	x2,1
	bl	box
.out:	ga_pop
	epilogue

; ---------------------------------------------------------------------------
;  Tables
; ---------------------------------------------------------------------------
	align 4
body_col dw 0x0040B848,0x002C9038,0x00686C68
; le muret : x, y, l, h
walls	dh OX-9,OY-9,GW*CELL+18,9
	dh OX-9,OY+GH*CELL,GW*CELL+18,9
	dh OX-9,OY-9,9,GH*CELL+18
	dh OX+GW*CELL,OY-9,9,GH*CELL+18
tx_table:
	ga_txe	s_logo,s_base,72,1,0x00D8FFC0,0x0048C050
	ga_txe	s_sub,s_base,15,0,0x00D8E8D0,0x00A8C0A0
	ga_txe	s_prompt,s_base,18,1,0x00FFFFFF,0x00D8F0D0
	ga_txe	s_best,s_base,15,1,0x00C8D8C0,0x00A0B898
	ga_txe	s_pause,s_base,26,1,0x00FFFFFF,0x00B8F0A8
	ga_txe	s_resume,s_base,13,0,0x00D8E8D0,0x00A8C0A0
	ga_txe	s_over,s_base,26,1,0x00FFD878,0x00F09040
	ga_txe	s_again,s_base,13,0,0x00D8E8D0,0x00A8C0A0
	ga_txe	s_newbest,s_base,15,1,0x00FFE080,0x00F0B040
s_base:
s_logo		db 'Snake',0
s_sub		db 'Une pomme, un anneau de plus',0
s_prompt	db 'Espace, A ou toucher pour jouer',0
s_pause		db 'Pause',0
s_resume	db 'P ou toucher pour reprendre',0
s_over		db 'Partie termin',0xC3,0xA9,'e',0
s_again		db 'Espace, A ou toucher : rejouer',0
s_newbest	db 'Nouveau record !',0
s_title		db 'Snake',0
s_score		db 'Score',0
s_len		db 'Longueur',0
s_best		db 'Record',0
p_dir		db 'SD0:/home/snake',0
p_best		db 'SD0:/home/snake/best.txt',0
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
