; ============================================================================
;  invaders - Space Invaders, le jeu d'Onyx (user/Apps/invaders), en assembleur.
;  Fleches tenues, souris ou doigt : le canon ; Espace, clic ou appui : le tir
;  (un seul a la fois). La flotte accelere en s'amenuisant, les boucliers
;  s'effritent des deux cotes, une soucoupe passe de temps en temps. P : pause.
;  Manette : croix, A tire. Plein ecran ; Start, Echap, P ou le bouton || : le
;  menu de pause (gamepause.inc). Taille logique 448 x 480.
;
;  Memes regles qu'Onyx, habillees : sprites « neon » calcules depuis les
;  dessins d'origine (degrade, lueur), ciel de nebuleuse, eclats, bandeau de
;  verre, ecran titre.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'
include '../../inc/pad.inc'

GF_W		= 448
GF_H		= 480
GF_BAND_TOP	= 0x00040616		; les bandes de part et d'autre, comme le ciel
GF_BAND_BOT	= 0x00140A26
HUD		= 26
S		= 2			; echelle des sprites
ACOLS		= 11
AROWS		= 5
ASTEPX		= 32
ASTEPY		= 26
GROUND		= GF_H-30
SHIPY		= GROUND-22
NSHIELD		= 4
SHW		= 22
SHH		= 16
SHIELDY		= SHIPY-30-SHH*S
SHIELDDX	= (GF_W-88-SHW*S)/(NSHIELD-1)
SHIP_W		= 26
UFO_Y		= HUD+10
UFO_W		= 32
GLOW		= 4			; lueur autour des sprites

ST_PLAY		= 0
ST_PAUSE	= 1
ST_HIT		= 2
ST_OVER		= 3
ST_CLEAR	= 4
ST_TITLE	= 5

TX_LOGO		= 0
TX_SUB		= 1
TX_PROMPT	= 2
TX_RECORD	= 3
TX_OVER		= 4
TX_AGAIN	= 5
TX_NEWBEST	= 6
TX_CLEAR	= 7
TX_SCORE	= 8
TX_BEST		= 9
TX_WAVE		= 10
NTX		= 11

ID_NEW		= 1
ID_PAUSE	= 2
ID_SOUND	= 3
ID_QUIT		= 4

KEY_ENTER	= 0x28
KEY_SPACE	= 0x2C
KEY_RIGHT	= 0x4F
KEY_LEFT	= 0x50

COL_SHIP	= 0x0040FF40
COL_SHIELD	= 0x0040E040
COL_SQUID	= 0x00F060F0
COL_CRAB	= 0x0060E0F0
COL_OCTO	= 0x0060F060
COL_UFO		= 0x00FF4040

; l'etat du jeu : x28 pointe dessus
virtual at 0
g_fx		rd 1			; origine de la flotte
g_fy		rd 1
g_fdir		rd 1
g_frame		rd 1			; image de la marche
g_stept		rd 1
g_note		rd 1
g_shipx		rd 1
g_lives		rd 1
g_score		rd 1
g_best		rd 1
g_wave		rd 1
g_state		rd 1
g_statet	rd 1
g_mx		rd 1			; souris (logique), -1 : pas encore
g_lastmx	rd 1
g_btn		rd 1			; bouton ou doigt tenu
g_now		rd 1
g_shot		rd 1			; tir : vivant, x, y
g_shotx		rd 1
g_shoty		rd 1
g_bomb		rd 3*4			; bombes : vivante, x, y, vitesse
g_ufo		rd 1			; soucoupe : vivante, x, sens, prochaine, points affiches
g_ufox		rd 1
g_ufod		rd 1
g_ufonext	rd 1
g_ufos		rd 1
g_ufost		rd 1
g_ufosx		rd 1
g_boomt		rd 1			; derniere explosion
g_boomx		rd 1
g_boomy		rd 1
g_dirty		rd 1			; couche fixe a refaire
g_prev		rd 1			; l'etat d'avant la pause
g_pst		rd 1			; son debut
g_pauset	rd 1
g_tick		rd 1
g_newbest	rd 1
g_salien	rd 6			; calmar, crabe, poulpe : deux images
g_sship		rd 1
g_slife		rd 1
g_sufo		rd 1
g_sboom		rd 1
g_sshot		rd 1
g_sbomb		rd 2
g_sorb		rd 2
g_dig		rd 10
g_dsm		rd 10
g_tx		rd NTX
g_alive		rb AROWS*ACOLS
g_shield	rb NSHIELD*SHH*SHW
		align 8
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
	mov	x1,224
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
	mov	x3,GF_W-30
	mov	x4,0
	bl	gp_init
	mov	x0,-1
	str	x0,[x28,g_mx]
	str	x0,[x28,g_lastmx]
	mov	x0,1
	str	x0,[x28,g_dirty]
	bl	now
	str	x0,[x28,g_now]
	mov	x0,ST_TITLE
	bl	set_state
	adr	x0,on_event
	adr	x1,step
	adr	x2,draw
	bl	gf_run
	bl	gs_close
quit:	mov	x0,0
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Parties
; ---------------------------------------------------------------------------
now:	sys	SYS_TIME_MS
	ret

new_game:
	prologue
	bl	ga_parts_clear
	str	xzr,[x28,g_newbest]
	str	xzr,[x28,g_score]
	str	xzr,[x28,g_wave]
	mov	x0,3
	str	x0,[x28,g_lives]
	bl	new_wave
	bl	build_shields
	epilogue

new_wave:
	prologue
	add	x0,x28,g_alive
	mov	x1,AROWS*ACOLS
	mov	w2,1
.a:	strb	w2,[x0],1
	subs	x1,x1,1
	b.ne	.a
	mov	x0,40
	str	x0,[x28,g_fx]
	ldr	x0,[x28,g_wave]			; HUD + 60 + (vague % 6) * 12
	mov	x1,6
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	mov	x1,12
	mul	x0,x0,x1
	add	x0,x0,HUD+60
	str	x0,[x28,g_fy]
	mov	x0,1
	str	x0,[x28,g_fdir]
	str	xzr,[x28,g_frame]
	str	xzr,[x28,g_note]
	bl	now
	str	x0,[x28,g_now]
	str	x0,[x28,g_stept]
	mov	x0,GF_W/2-13
	str	x0,[x28,g_shipx]
	str	xzr,[x28,g_shot]
	str	xzr,[x28,g_bomb]
	str	xzr,[x28,g_bomb+32]
	str	xzr,[x28,g_bomb+64]
	str	xzr,[x28,g_ufo]
	str	xzr,[x28,g_ufost]
	str	xzr,[x28,g_boomt]
	movl	x0,10000
	bl	gf_below
	movl	x1,15000
	add	x0,x0,x1
	ldr	x1,[x28,g_now]
	add	x0,x0,x1
	str	x0,[x28,g_ufonext]
	mov	x0,ST_PLAY
	bl	set_state
	epilogue

; x0 = etat
set_state:
	prologue
	str	x0,[x28,g_state]
	ldr	x1,[x28,g_now]
	str	x1,[x28,g_statet]
	cmp	x0,ST_PAUSE
	cset	x0,eq
	stv	x0,gf_still
	mov	x0,1
	stv	x0,gf_redraw
	epilogue

build_shields:
	add	x0,x28,g_shield
	adr	x1,shield_rows
	mov	x2,0				; bouclier
.s:	mov	x3,0				; ligne
.y:	mov	x4,0				; colonne
.x:	mov	x5,x4				; (la moitie droite : en miroir)
	cmp	x4,SHW/2
	b.lo	.h
	mov	x5,SHW-1
	sub	x5,x5,x4
.h:	mov	x6,SHW/2
	madd	x6,x3,x6,x5
	ldrb	w6,[x1,x6]
	cmp	w6,'#'
	cset	w6,eq
	strb	w6,[x0],1
	add	x4,x4,1
	cmp	x4,SHW
	b.lo	.x
	add	x3,x3,1
	cmp	x3,SHH
	b.lo	.y
	add	x2,x2,1
	cmp	x2,NSHIELD
	b.lo	.s
	ret

; x0 = bouclier -> x0 = son x
shield_x:
	mov	x1,SHIELDDX
	mul	x0,x0,x1
	add	x0,x0,44
	ret

; -> x0 = aliens restants
remaining:
	add	x1,x28,g_alive
	mov	x2,AROWS*ACOLS
	mov	x0,0
.a:	ldrb	w3,[x1],1
	add	x0,x0,x3
	subs	x2,x2,1
	b.ne	.a
	ret

; x0 = rangee, x1 = colonne -> x0 = x, x1 = y, x2 = largeur (pixels)
alien_xy:
	adr	x2,alien_w
	ldrb	w2,[x2,x0]
	lsl	x2,x2,1
	mov	x3,24
	sub	x3,x3,x2
	lsr	x3,x3,1
	ldr	x4,[x28,g_fx]
	add	x3,x3,x4
	mov	x4,ASTEPX
	madd	x4,x1,x4,x3
	mov	x5,ASTEPY
	ldr	x3,[x28,g_fy]
	madd	x1,x0,x5,x3
	mov	x0,x4
	ret

; x0 = x, x1 = y, x2 = 1 vers le bas / -1 vers le haut : un tir contre les
; boucliers (une petite explosion) -> x0 = 1 s'il a touche
hit_shield:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,0				; bouclier
.s:	mov	x0,x22
	bl	shield_x
	sub	x0,x19,x0
	mov	x1,S
	sdiv	x23,x0,x1			; sx
	sub	x0,x20,SHIELDY
	sdiv	x24,x0,x1			; sy
	cmp	x23,SHW
	b.hs	.n
	cmp	x24,SHH
	b.hs	.n
	mov	x0,x22
	mov	x1,x23
	mov	x2,x24
	bl	shield_at
	ldrb	w1,[x0]
	cbz	w1,.n
	mov	x25,-2				; dy
.dy:	mov	x26,-2				; dx
.dx:	mul	x0,x25,x25
	madd	x0,x26,x26,x0
	cmp	x0,4
	b.gt	.nx
	add	x2,x24,x25
	add	x2,x2,x21			; yy
	add	x1,x23,x26			; xx
	cmp	x2,SHH
	b.hs	.nx
	cmp	x1,SHW
	b.hs	.nx
	mov	x0,x22
	bl	shield_at
	mov	x3,x0
	mov	x0,4
	bl	gf_below
	cbz	x0,.nx
	strb	wzr,[x3]
.nx:	add	x26,x26,1
	cmp	x26,2
	b.le	.dx
	add	x25,x25,1
	cmp	x25,2
	b.le	.dy
	mov	x0,1
	b	.out
.n:	add	x22,x22,1
	cmp	x22,NSHIELD
	b.lo	.s
	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = bouclier, x1 = colonne, x2 = ligne -> x0 = adresse de la case
shield_at:
	mov	x3,SHH*SHW
	mul	x0,x0,x3
	mov	x3,SHW
	madd	x0,x2,x3,x0
	add	x0,x0,x1
	add	x0,x0,g_shield
	add	x0,x0,x28
	ret

fire:
	prologue
	ldr	x0,[x28,g_state]
	cbnz	x0,.out
	ldr	x0,[x28,g_shot]
	cbnz	x0,.out
	mov	x0,1
	str	x0,[x28,g_shot]
	ldr	x0,[x28,g_shipx]
	add	x0,x0,12
	str	x0,[x28,g_shotx]
	mov	x0,SHIPY-8
	str	x0,[x28,g_shoty]
	adr	x0,snd_fire
	bl	gs_seq
.out:	epilogue

; ---------------------------------------------------------------------------
;  Une image de jeu
; ---------------------------------------------------------------------------
march:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	bl	remaining
	mov	x1,14
	mul	x0,x0,x1			; 30 + n * 14 - vague * 20
	add	x0,x0,30
	ldr	x1,[x28,g_wave]
	mov	x2,20
	msub	x0,x1,x2,x0
	cmp	x0,30
	mov	x1,30
	csel	x0,x1,x0,lt
	ldr	x1,[x28,g_now]
	ldr	x2,[x28,g_stept]
	sub	x2,x1,x2
	cmp	x2,x0
	b.lo	.out
	str	x1,[x28,g_stept]
	mov	x19,GF_W			; bords de la flotte vivante
	mov	x20,0
	mov	x21,0
	mov	x22,0				; rangee
.r:	mov	x23,0
.c:	mov	x0,ACOLS
	mul	x0,x22,x0
	add	x0,x0,x23
	add	x0,x0,g_alive
	ldrb	w0,[x28,x0]
	cbz	w0,.nc
	mov	x0,x22
	mov	x1,x23
	bl	alien_xy
	cmp	x0,x19
	csel	x19,x0,x19,lt
	add	x0,x0,x2
	cmp	x0,x20
	csel	x20,x0,x20,gt
	add	x1,x1,16
	cmp	x1,x21
	csel	x21,x1,x21,gt
.nc:	add	x23,x23,1
	cmp	x23,ACOLS
	b.lo	.c
	add	x22,x22,1
	cmp	x22,AROWS
	b.lo	.r
	ldr	x0,[x28,g_fdir]
	cmp	x0,0
	b.le	.left
	add	x1,x20,8
	cmp	x1,GF_W-8
	b.gt	.turn
	b	.go
.left:	sub	x1,x19,8
	cmp	x1,8
	b.ge	.go
.turn:	neg	x0,x0
	str	x0,[x28,g_fdir]
	ldr	x0,[x28,g_fy]
	add	x0,x0,12
	str	x0,[x28,g_fy]
	b	.anim
.go:	lsl	x0,x0,3
	ldr	x1,[x28,g_fx]
	add	x1,x1,x0
	str	x1,[x28,g_fx]
.anim:	ldr	x0,[x28,g_frame]
	eor	x0,x0,1
	str	x0,[x28,g_frame]
	ldr	x1,[x28,g_note]			; la basse qui descend
	adr	x0,bass
	ldrh	w0,[x0,x1,lsl 1]
	add	x1,x1,1
	and	x1,x1,3
	str	x1,[x28,g_note]
	mov	x1,70
	mov	x2,AKW_SQUARE
	mov	x3,110
	bl	gs_note
	mov	x22,0				; la flotte mange les boucliers qu'elle traverse
.er:	mov	x23,0
.ec:	mov	x0,ACOLS
	mul	x0,x22,x0
	add	x0,x0,x23
	add	x0,x0,g_alive
	ldrb	w0,[x28,x0]
	cbz	w0,.enc
	mov	x0,x22
	mov	x1,x23
	bl	alien_xy
	add	x2,x1,16
	cmp	x2,SHIELDY
	b.le	.enc
	mov	x24,x0
	mov	x25,x1
	mov	x26,0				; bouclier
.es:	mov	x19,0				; ligne
.ey:	mov	x20,0				; colonne
.ex:	mov	x0,x26
	bl	shield_x
	add	x0,x0,x20,lsl 1
	cmp	x0,x24
	b.lt	.enx
	add	x1,x24,24
	cmp	x0,x1
	b.ge	.enx
	add	x0,x19,x19
	add	x0,x0,SHIELDY
	cmp	x0,x25
	b.lt	.enx
	add	x1,x25,16
	cmp	x0,x1
	b.ge	.enx
	mov	x0,x26
	mov	x1,x20
	mov	x2,x19
	bl	shield_at
	strb	wzr,[x0]
.enx:	add	x20,x20,1
	cmp	x20,SHW
	b.lo	.ex
	add	x19,x19,1
	cmp	x19,SHH
	b.lo	.ey
	add	x26,x26,1
	cmp	x26,NSHIELD
	b.lo	.es
.enc:	add	x23,x23,1
	cmp	x23,ACOLS
	b.lo	.ec
	add	x22,x22,1
	cmp	x22,AROWS
	b.lo	.er
	cmp	x21,SHIPY			; (x21 : calcule avant ; au sol, c'est fini)
	b.lt	.out
	str	xzr,[x28,g_lives]
	bl	ship_hit
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

alien_fire:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,100
	bl	gf_below
	ldr	x1,[x28,g_wave]
	add	x1,x1,2
	cmp	x0,x1
	b.hs	.out
	mov	x0,ACOLS
	bl	gf_below
	mov	x19,x0				; colonne
	mov	x20,AROWS-1
.r:	mov	x0,ACOLS
	mul	x0,x20,x0
	add	x0,x0,x19
	add	x0,x0,g_alive
	ldrb	w0,[x28,x0]
	cbnz	w0,.got
	subs	x20,x20,1
	b.pl	.r
	b	.out
.got:	mov	x0,x20
	mov	x1,x19
	bl	alien_xy
	add	x4,x28,g_bomb
	mov	x5,3
.b:	ldr	x2,[x4]
	cbz	x2,.free
	add	x4,x4,32
	subs	x5,x5,1
	b.ne	.b
	b	.out
.free:	mov	x2,1
	str	x2,[x4]
	add	x0,x0,10
	add	x1,x1,16
	stp	x0,x1,[x4,8]
	mov	x19,x4
	mov	x0,2
	bl	gf_below
	ldr	x1,[x28,g_wave]
	add	x0,x0,3
	cmp	x1,3
	cinc	x0,x0,hi
	str	x0,[x19,24]
.out:	ldp	x19,x20,[sp],16
	epilogue

ship_hit:
	prologue
	ldr	x0,[x28,g_shipx]
	add	x0,x0,SHIP_W/2
	mov	x1,SHIPY+8
	movl	w2,0x0080FFC0
	mov	x3,60
	movl	x4,0x40000
	mov	x5,40
	mov	x6,3
	mov	x7,0x1800
	bl	ga_burst
	mov	x0,ST_HIT
	bl	set_state
	mov	x0,120
	mov	x1,400
	mov	x2,AKW_NOISE
	mov	x3,160
	bl	gs_note
	ldr	x0,[x28,g_lives]
	subs	x0,x0,1
	b.gt	.live
	str	xzr,[x28,g_lives]
	mov	x0,ST_OVER
	bl	set_state
	bl	keep_best
	adr	x0,gs_tab_lose
	bl	gs_seq
	b	.clr
.live:	str	x0,[x28,g_lives]
.clr:	str	xzr,[x28,g_shot]
	str	xzr,[x28,g_bomb]
	str	xzr,[x28,g_bomb+32]
	str	xzr,[x28,g_bomb+64]
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

; le tir du canon
shot_step:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x0,[x28,g_shot]
	cbz	x0,.out
	ldr	x19,[x28,g_shotx]
	ldr	x20,[x28,g_shoty]
	sub	x20,x20,9
	str	x20,[x28,g_shoty]
	cmp	x20,HUD+4
	b.lt	.dead
	mov	x0,x19
	mov	x1,x20
	mov	x2,-1
	bl	hit_shield
	cbnz	x0,.dead
	mov	x21,0				; rangee
.r:	mov	x22,0
.c:	mov	x0,ACOLS
	mul	x0,x21,x0
	add	x0,x0,x22
	add	x0,x0,g_alive
	ldrb	w0,[x28,x0]
	cbz	w0,.nc
	mov	x0,x21
	mov	x1,x22
	bl	alien_xy
	cmp	x19,x0
	b.lt	.nc
	add	x2,x2,x0
	cmp	x19,x2
	b.ge	.nc
	cmp	x20,x1
	b.lt	.nc
	add	x2,x1,16
	cmp	x20,x2
	b.ge	.nc
	str	x0,[x28,g_boomx]		; touche
	str	x1,[x28,g_boomy]
	ldr	x0,[x28,g_now]
	str	x0,[x28,g_boomt]
	mov	x0,ACOLS
	mul	x0,x21,x0
	add	x0,x0,x22
	add	x0,x0,g_alive
	strb	wzr,[x28,x0]
	mov	x0,10
	cmp	x21,3
	b.hs	.pts
	mov	x0,20
	cbnz	x21,.pts
	mov	x0,30
.pts:	bl	add_score
	ldr	x0,[x28,g_boomx]		; les eclats, de la couleur de l'alien
	add	x0,x0,12
	ldr	x1,[x28,g_boomy]
	add	x1,x1,8
	adr	x2,alien_col
	ldr	w2,[x2,x21,lsl 2]
	mov	x3,22
	movl	x4,0x28000
	mov	x5,18
	mov	x6,2
	mov	x7,0x1000
	bl	ga_burst
	mov	x0,220
	mov	x1,90
	mov	x2,AKW_NOISE
	mov	x3,120
	bl	gs_note
	b	.dead
.nc:	add	x22,x22,1
	cmp	x22,ACOLS
	b.lo	.c
	add	x21,x21,1
	cmp	x21,AROWS
	b.lo	.r
	add	x4,x28,g_bomb			; les tirs peuvent se heurter
	mov	x5,3
.b:	ldr	x0,[x4]
	cbz	x0,.nb
	ldp	x0,x1,[x4,8]
	sub	x2,x0,3
	cmp	x19,x2
	b.lt	.nb
	add	x2,x0,3
	cmp	x19,x2
	b.gt	.nb
	add	x2,x1,10
	cmp	x20,x2
	b.gt	.nb
	sub	x2,x1,4
	cmp	x20,x2
	b.lt	.nb
	str	xzr,[x4]
	b	.dead
.nb:	add	x4,x4,32
	subs	x5,x5,1
	b.ne	.b
	ldr	x0,[x28,g_ufo]			; la soucoupe
	cbz	x0,.out
	ldr	x0,[x28,g_ufox]
	cmp	x19,x0
	b.lt	.out
	add	x0,x0,UFO_W
	cmp	x19,x0
	b.ge	.out
	cmp	x20,HUD+10
	b.lt	.out
	cmp	x20,HUD+24
	b.ge	.out
	mov	x0,4
	bl	gf_below
	adr	x1,ufo_pts
	ldrh	w0,[x1,x0,lsl 1]
	str	x0,[x28,g_ufos]
	bl	add_score
	str	xzr,[x28,g_ufo]
	ldr	x0,[x28,g_ufox]
	str	x0,[x28,g_ufosx]
	ldr	x0,[x28,g_now]
	str	x0,[x28,g_ufost]
	ldr	x0,[x28,g_ufox]
	add	x0,x0,UFO_W/2
	mov	x1,UFO_Y+7
	movl	w2,COL_UFO
	mov	x3,40
	movl	x4,0x38000
	mov	x5,26
	mov	x6,2
	mov	x7,0x800
	bl	ga_burst
	adr	x0,snd_ufo
	bl	gs_seq
.dead:	str	xzr,[x28,g_shot]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = points
add_score:
	ldr	x1,[x28,g_score]
	add	x1,x1,x0
	str	x1,[x28,g_score]
	ret

bombs_step:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x28,g_bomb
	mov	x20,3
.b:	ldr	x0,[x19]
	cbz	x0,.nb
	ldr	x0,[x19,16]
	ldr	x1,[x19,24]
	add	x1,x0,x1
	str	x1,[x19,16]
	cmp	x1,GROUND
	b.gt	.dead
	ldr	x0,[x19,8]
	add	x1,x1,8
	mov	x2,1
	bl	hit_shield
	cbnz	x0,.dead
	ldp	x0,x1,[x19,8]
	ldr	x2,[x28,g_shipx]
	cmp	x0,x2
	b.lt	.nb
	add	x2,x2,SHIP_W
	cmp	x0,x2
	b.ge	.nb
	add	x2,x1,8
	cmp	x2,SHIPY
	b.lt	.nb
	cmp	x1,SHIPY+16
	b.ge	.nb
	str	xzr,[x19]
	bl	ship_hit
	b	.out
.dead:	str	xzr,[x19]
.nb:	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.b
.out:	ldp	x19,x20,[sp],16
	epilogue

ufo_step:
	prologue
	ldr	x0,[x28,g_ufo]
	cbnz	x0,.live
	ldr	x0,[x28,g_now]
	ldr	x1,[x28,g_ufonext]
	cmp	x0,x1
	b.ls	.out
	bl	remaining
	cmp	x0,6
	b.le	.out
	mov	x0,2
	bl	gf_below
	mov	x1,-1
	mov	x2,GF_W
	cbz	x0,.d
	mov	x1,1
	mov	x2,-UFO_W
.d:	str	x1,[x28,g_ufod]
	str	x2,[x28,g_ufox]
	mov	x0,1
	str	x0,[x28,g_ufo]
	movl	x0,15000
	bl	gf_below
	movl	x1,20000
	add	x0,x0,x1
	ldr	x1,[x28,g_now]
	add	x0,x0,x1
	str	x0,[x28,g_ufonext]
.live:	ldr	x0,[x28,g_ufod]
	ldr	x1,[x28,g_ufox]
	add	x1,x1,x0,lsl 1
	str	x1,[x28,g_ufox]
	ldr	x2,[x28,g_now]
	mov	x3,120
	udiv	x2,x2,x3
	tbz	x2,0,.bd
	cmp	x0,0
	mov	x0,740
	mov	x3,700
	csel	x0,x3,x0,lt
	mov	x1,60
	mov	x2,AKW_TRIANGLE
	mov	x3,50
	bl	gs_note
.bd:	ldr	x1,[x28,g_ufox]
	cmn	x1,40
	b.lt	.gone
	cmp	x1,GF_W+8
	b.le	.out
.gone:	str	xzr,[x28,g_ufo]
.out:	epilogue

step:
	prologue
	ldr	x0,[x28,g_tick]
	add	x0,x0,1
	str	x0,[x28,g_tick]
	bl	ga_parts_step
	bl	now
	str	x0,[x28,g_now]
	ldr	x1,[x28,g_ufost]		; le score de la soucoupe s'efface
	cbz	x1,.st
	sub	x2,x0,x1
	cmp	x2,1000
	b.lo	.st
	str	xzr,[x28,g_ufost]
.st:	ldr	x1,[x28,g_state]
	cmp	x1,ST_PAUSE
	b.eq	.out
	cmp	x1,ST_TITLE
	b.eq	.out
	cmp	x1,ST_OVER
	b.eq	.out
	cmp	x1,ST_HIT
	b.ne	.clr
	ldr	x1,[x28,g_statet]
	sub	x1,x0,x1
	cmp	x1,1200
	b.ls	.out
	mov	x0,ST_PLAY
	bl	set_state
	b	.out
.clr:	cmp	x1,ST_CLEAR
	b.ne	.play
	ldr	x1,[x28,g_statet]
	sub	x1,x0,x1
	cmp	x1,1500
	b.ls	.out
	ldr	x0,[x28,g_wave]
	add	x0,x0,1
	str	x0,[x28,g_wave]
	bl	new_wave
	b	.out
.play:	mov	x0,0				; le canon (touches ou croix tenues)
	adr	x1,gf_held
	ldv	x5,gp_now
	ldrb	w2,[x1,KEY_LEFT]
	ubfx	x3,x5,2,1			; (PAD_LEFT)
	orr	w2,w2,w3
	cbz	w2,.nl
	sub	x0,x0,4
.nl:	ldrb	w2,[x1,KEY_RIGHT]
	ubfx	x3,x5,3,1			; (PAD_RIGHT)
	orr	w2,w2,w3
	cbz	w2,.nr
	add	x0,x0,4
.nr:	ldr	x2,[x28,g_shipx]
	ldr	x3,[x28,g_mx]
	ldr	x4,[x28,g_lastmx]
	str	x3,[x28,g_lastmx]
	cbz	x0,.mouse
	add	x2,x2,x0
	b	.clip
.mouse:	cmp	x3,x4
	b.eq	.clip
	tbnz	x3,63,.clip
	sub	x2,x3,13
.clip:	cmp	x2,8
	mov	x0,8
	csel	x2,x0,x2,lt
	mov	x0,GF_W-8-SHIP_W
	cmp	x2,x0
	csel	x2,x0,x2,gt
	str	x2,[x28,g_shipx]
	ldrb	w0,[x1,KEY_SPACE]
	ldr	x2,[x28,g_btn]
	orr	x0,x0,x2
	ldv	x2,gp_now			; (A tenu tire aussi)
	ubfx	x2,x2,4,1
	orr	x0,x0,x2
	cbz	x0,.mar
	bl	fire
.mar:	bl	march
	ldr	x0,[x28,g_state]
	cbnz	x0,.out
	bl	alien_fire
	bl	shot_step
	bl	bombs_step
	ldr	x0,[x28,g_state]
	cbnz	x0,.out
	bl	ufo_step
	bl	remaining
	cbnz	x0,.out
	mov	x0,ST_CLEAR
	bl	set_state
	bl	keep_best
	adr	x0,gs_tab_win
	bl	gs_seq
.out:	epilogue

; ---------------------------------------------------------------------------
;  Evenements
; ---------------------------------------------------------------------------
do_pause:
	b	gp_open

; (gamepause) le jeu s'arrete, une explosion ou une fin de vague comprise ->
; x0 = 1 si une partie etait en cours
game_pause:
	prologue
	str	xzr,[x28,g_btn]
	ldr	x1,[x28,g_state]
	mov	x0,0
	cmp	x1,ST_PAUSE
	b.eq	.out
	cmp	x1,ST_OVER
	b.eq	.out
	cmp	x1,ST_TITLE
	b.eq	.out
	str	x1,[x28,g_prev]
	ldr	x0,[x28,g_statet]
	str	x0,[x28,g_pst]
	bl	now
	str	x0,[x28,g_pauset]
	mov	x0,ST_PAUSE
	bl	set_state
	mov	x0,1
.out:	epilogue

; (gamepause) le jeu repart : les heures retenues avancent de la duree de la pause
game_resume:
	prologue
	str	x19,[sp,-16]!
	ldr	x1,[x28,g_state]
	cmp	x1,ST_PAUSE
	b.ne	.out
	bl	now
	str	x0,[x28,g_now]
	ldr	x1,[x28,g_pauset]
	sub	x19,x0,x1
	ldr	x0,[x28,g_prev]
	bl	set_state
	ldr	x0,[x28,g_pst]
	add	x0,x0,x19
	str	x0,[x28,g_statet]
	ldr	x0,[x28,g_stept]
	add	x0,x0,x19
	str	x0,[x28,g_stept]
	ldr	x0,[x28,g_ufonext]
	add	x0,x0,x19
	str	x0,[x28,g_ufonext]
	ldr	x0,[x28,g_ufost]
	cbz	x0,.b
	add	x0,x0,x19
	str	x0,[x28,g_ufost]
.b:	ldr	x0,[x28,g_boomt]
	cbz	x0,.out
	add	x0,x0,x19
	str	x0,[x28,g_boomt]
.out:	ldr	x19,[sp],16
	epilogue

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

; Espace, Entree, clic : selon l'etat
action:
	prologue
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.eq	.new
	cmp	x0,ST_OVER
	b.ne	.p
	ldr	x1,[x28,g_now]			; (pas un tir d'avant la fin)
	ldr	x2,[x28,g_statet]
	sub	x1,x1,x2
	cmp	x1,600
	b.lo	.out
.new:
	bl	new_game
	b	.out
.p:	cmp	x0,ST_PAUSE
	b.ne	.f
	mov	x0,ST_PLAY
	bl	set_state
	b	.out
.f:	bl	fire
.out:	epilogue

; la manette : A hors de la partie (en jeu, A tenu tire, voir step)
on_pad:
	prologue
	ldv	x0,gp_new
	tst	x0,PAD_A
	b.eq	.out
	ldr	x1,[x28,g_state]
	cmp	x1,ST_PLAY
	b.eq	.out
	bl	action
.out:	epilogue

on_event:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	gp_event
	cbnz	x0,.out
	mov	x0,x19
	cmp	x0,WEV_PAD
	b.ne	.mn
	bl	on_pad
	b	.out
.mn:	cmp	x0,WEV_MENU
	b.ne	.k
	adr	x0,ui_ev
	ldr	w0,[x0,EV_CODE]
	bl	do_menu
	b	.out
.k:	cmp	x0,WEV_KEY
	b.ne	.m
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
	b.ne	.sp
	bl	do_pause
	b	.out
.sp:	ldr	w0,[x1,EV_CODE]
	cmp	w0,KEY_SPACE
	b.eq	.act
	cmp	w0,KEY_ENTER
	b.ne	.out
.act:	bl	action
	b	.out
.m:	cmp	x0,WEV_FOCUS
	b.ne	.mm
	str	xzr,[x28,g_btn]
	b	.out
.mm:	cmp	x0,WEV_MOUSE
	b.ne	.out
	bl	gf_mouse
	adr	x2,ui_ev
	ldr	w3,[x2,EV_KIND]
	cbnz	w3,.pr
	str	x0,[x28,g_mx]
	b	.out
.pr:	cmp	w3,1
	b.ne	.rel
	ldr	w3,[x2,EV_CODE]
	tbz	w3,0,.out
	str	x0,[x28,g_mx]
	mov	x0,HUD
	bl	gf_drag_top
	cbnz	x0,.out
	mov	x0,1
	str	x0,[x28,g_btn]
	bl	action
	b	.out
.rel:	str	xzr,[x28,g_btn]
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Sprites, decor
; ---------------------------------------------------------------------------

; x0 = dessin '#', x1 = largeur, x2 = hauteur (cases), w3 = couleur, x4 = echelle
; -> x0 = sprite (GLOW points de marge) : les cases en degrade, clair en haut,
; entourees d'une lueur de leur couleur
neon:
	prologue
	ga_push
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	mov	x23,x4
	mul	x0,x20,x23
	add	x0,x0,GLOW*2
	mul	x1,x21,x23
	add	x1,x1,GLOW*2
	stp	x0,x1,[sp]
	bl	ga_new
	mov	x24,x0
	add	x25,x0,8			; le point courant
	mov	x26,0				; y
.y:	mov	x27,0				; x
.x:	sub	x0,x27,GLOW			; la case sous le point
	sub	x1,x26,GLOW
	tbnz	x0,63,.glow
	tbnz	x1,63,.glow
	udiv	x0,x0,x23
	udiv	x1,x1,x23
	cmp	x0,x20
	b.hs	.glow
	cmp	x1,x21
	b.hs	.glow
	madd	x2,x1,x20,x0
	ldrb	w2,[x19,x2]
	cmp	w2,'#'
	b.ne	.glow
	sub	x1,x26,GLOW			; le corps : 380 en haut, 200 en bas
	lsl	x1,x1,8
	mul	x2,x21,x23
	udiv	x1,x1,x2
	mov	x2,180
	mul	x1,x1,x2
	lsr	x1,x1,8
	mov	x2,380
	sub	x1,x2,x1
	mov	w0,w22
	bl	ga_shade
	mov	w1,w0
	mov	x2,256
	mov	x0,x25
	bl	ga_put
	b	.next
.glow:	lsl	x9,x27,1			; distance (en demi-points) a la case allumee
	add	x9,x9,1				; la plus proche
	lsl	x10,x26,1
	add	x10,x10,1
	movl	x11,0x7FFFFFFF
	mov	x12,0
.gy:	mov	x13,0
.gx:	madd	x0,x12,x20,x13
	ldrb	w0,[x19,x0]
	cmp	w0,'#'
	b.ne	.gn
	mul	x0,x13,x23
	add	x0,x0,GLOW
	lsl	x0,x0,1
	add	x1,x0,x23,lsl 1
	sub	x2,x0,x9
	sub	x3,x9,x1
	cmp	x2,x3
	csel	x2,x2,x3,gt
	cmp	x2,0
	csel	x2,xzr,x2,lt
	mul	x0,x12,x23
	add	x0,x0,GLOW
	lsl	x0,x0,1
	add	x1,x0,x23,lsl 1
	sub	x3,x0,x10
	sub	x4,x10,x1
	cmp	x3,x4
	csel	x3,x3,x4,gt
	cmp	x3,0
	csel	x3,xzr,x3,lt
	mul	x2,x2,x2
	madd	x2,x3,x3,x2
	cmp	x2,x11
	csel	x11,x2,x11,lo
.gn:	add	x13,x13,1
	cmp	x13,x20
	b.lo	.gx
	add	x12,x12,1
	cmp	x12,x21
	b.lo	.gy
	mov	x0,4*GLOW*GLOW
	cmp	x11,x0
	b.hs	.next
	sub	x0,x0,x11
	mov	x1,110
	mul	x0,x0,x1
	mov	x1,4*GLOW*GLOW
	udiv	x2,x0,x1
	mul	x2,x2,x2
	mov	x1,110
	udiv	x2,x2,x1
	mov	w1,w22
	mov	x0,x25
	bl	ga_put
.next:	add	x25,x25,4
	add	x27,x27,1
	ldr	x0,[sp]
	cmp	x27,x0
	b.lo	.x
	add	x26,x26,1
	ldr	x0,[sp,8]
	cmp	x26,x0
	b.lo	.y
	mov	x0,x24
	add	sp,sp,16
	ldp	x27,x28,[sp],16
	ga_pop
	epilogue

gen_sprites:
	prologue
	ga_push
	add	x19,x28,g_salien
	adr	x20,neon_tab
	mov	x21,6
.a:	ldr	w0,[x20]
	adr	x1,spr_ship
	add	x0,x1,x0
	ldrb	w1,[x20,4]
	mov	x2,8
	ldr	w3,[x20,8]
	mov	x4,S
	bl	neon
	str	x0,[x19],8
	add	x20,x20,12
	subs	x21,x21,1
	b.ne	.a
	adr	x0,spr_ship
	mov	x1,13
	mov	x2,8
	movl	w3,COL_SHIP
	mov	x4,S
	bl	neon
	str	x0,[x28,g_sship]
	adr	x0,spr_ship
	mov	x1,13
	mov	x2,8
	movl	w3,COL_SHIP
	mov	x4,1
	bl	neon
	str	x0,[x28,g_slife]
	adr	x0,spr_ufo
	mov	x1,16
	mov	x2,7
	movl	w3,COL_UFO
	mov	x4,S
	bl	neon
	str	x0,[x28,g_sufo]
	adr	x0,spr_boom
	mov	x1,12
	mov	x2,8
	movl	w3,0x00FFE0A0
	mov	x4,S
	bl	neon
	str	x0,[x28,g_sboom]
	adr	x0,spr_shot
	mov	x1,1
	mov	x2,4
	movl	w3,0x00B0E8FF
	mov	x4,2
	bl	neon
	str	x0,[x28,g_sshot]
	adr	x0,spr_bomb
	mov	x1,2
	mov	x2,4
	movl	w3,0x00FFB040
	mov	x4,2
	bl	neon
	str	x0,[x28,g_sbomb]
	adr	x0,spr_bomb+8
	mov	x1,2
	mov	x2,4
	movl	w3,0x00FFB040
	mov	x4,2
	bl	neon
	str	x0,[x28,g_sbomb+8]
	mov	x0,200
	movl	w1,0x00140828
	bl	ga_disc
	str	x0,[x28,g_sorb]
	mov	x0,160
	movl	w1,0x00081428
	bl	ga_disc
	str	x0,[x28,g_sorb+8]
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	add	x0,x28,g_dig
	mov	x1,16
	adr	x2,ui_font_b
	movl	w3,0x00FFFFFF
	movl	w4,0x00B8E0FF
	bl	ga_digits
	add	x0,x28,g_dsm
	mov	x1,14
	adr	x2,ui_font_b
	movl	w3,0x00FFD0C0
	movl	w4,0x00FF6050
	bl	ga_digits
	adr	x0,tx_table
	adr	x1,s_base
	add	x2,x28,g_tx
	mov	x3,NTX
	bl	ga_texts
	bl	ui_canvas_end
	ga_pop
	epilogue

; la couche fixe : nebuleuse, etoiles, sol lumineux
paint_layer:
	prologue
	str	xzr,[x28,g_dirty]
	ldv	x0,gf_layer
	stv	x0,gf_dst
	movl	w0,0x00040616
	movl	w1,0x00140A26
	movl	w2,0x00342050
	mov	x3,260
	bl	ga_sky
	mov	x0,0
	mov	x1,GROUND-1
	mov	x2,GF_W
	mov	x3,4
	movl	w4,0x00103828
	bl	gf_rect
	mov	x0,0
	mov	x1,GROUND
	mov	x2,GF_W
	mov	x3,2
	movl	w4,0x0060E0A8
	bl	gf_rect
	mov	x0,0
	mov	x1,GROUND+3
	mov	x2,GF_W
	mov	x3,GF_H-GROUND-3
	movl	w4,0x00060A12
	bl	gf_rect
	adr	x0,gf_frame
	stv	x0,gf_dst
	epilogue

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------

; x0 = texte TX_*, x1 = x, x2 = valeur : etiquette et chiffres du bandeau -> x0 = x d'apres
hud_item:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x3,x28,g_tx
	ldr	x19,[x3,x0,lsl 3]
	mov	x20,x2
	stp	x1,x1,[sp,-16]!
	mov	x0,x1
	mov	x1,4
	mov	x2,x19
	bl	ga_ablit
	ldp	x1,x2,[sp],16
	ldr	w0,[x19]
	add	x1,x1,x0
	add	x1,x1,2
	mov	x0,x20
	mov	x2,3
	add	x3,x28,g_dig
	mov	x4,4
	bl	ga_num
	ldp	x19,x20,[sp],16
	epilogue

; x0 = y, x1 = hauteur : un panneau de verre au milieu
glass:
	mov	x3,x1
	mov	x1,x0
	mov	x0,56
	mov	x2,GF_W-112
	mov	x4,16
	movl	w5,0x00241C44
	movl	w6,0x00100C22
	mov	x7,232
	movl	w8,0x006858A8
	b	ga_panel

; x0 = rangee -> x2 = sprite (image de la marche)
alien_sprite:
	adr	x1,alien_kind
	ldrb	w1,[x1,x0]
	ldr	x2,[x28,g_frame]
	add	x1,x2,x1,lsl 1
	add	x2,x28,g_salien
	ldr	x2,[x2,x1,lsl 3]
	ret

draw_title:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x0,[x28,g_tx+TX_LOGO*8]
	mov	x1,GF_W/2
	mov	x2,86
	bl	ga_show
	ldr	x0,[x28,g_tx+TX_SUB*8]
	mov	x1,GF_W/2
	mov	x2,170
	bl	ga_show
	mov	x19,0				; les trois aliens et leurs points
.k:	ldr	x0,[x28,g_tick]
	add	x0,x0,x19,lsl 6
	lsl	x0,x0,2
	bl	ga_sin8
	lsr	x0,x0,5
	add	x22,x0,226			; le haut, qui flotte
	mov	x0,110
	mul	x20,x19,x0
	add	x20,x20,GF_W/2-110		; le centre
	ldr	x0,[x28,g_tick]
	lsr	x0,x0,5
	and	x0,x0,1
	add	x0,x0,x19,lsl 1
	add	x2,x28,g_salien
	ldr	x2,[x2,x0,lsl 3]
	ldr	w0,[x2]
	sub	x0,x20,x0,lsr 1
	mov	x1,x22
	bl	ga_ablit
	adr	x0,kind_pts
	ldrh	w0,[x0,x19,lsl 1]
	mov	x1,x20
	mov	x2,268
	add	x3,x28,g_dsm
	mov	x4,5
	bl	ga_num
	add	x19,x19,1
	cmp	x19,3
	b.lo	.k
	ldr	x0,[x28,g_tick]
	lsl	x0,x0,1
	bl	ga_sin8
	cmp	x0,40
	b.lo	.r
	ldr	x0,[x28,g_tx+TX_PROMPT*8]
	mov	x1,GF_W/2
	mov	x2,322
	bl	ga_show
.r:	ldr	x0,[x28,g_tx+TX_RECORD*8]
	mov	x1,GF_W/2-34
	mov	x2,370
	bl	ga_show
	ldr	x0,[x28,g_best]
	mov	x1,GF_W/2+12
	mov	x2,368
	add	x3,x28,g_dig
	mov	x4,4
	bl	ga_num
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x0,[x28,g_dirty]
	cbz	x0,.base
	bl	paint_layer
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
	bl	ga_parts_draw
	b	.out
.game:	mov	x0,0				; le bandeau
	mov	x1,0
	mov	x2,GF_W
	mov	x3,HUD
	mov	x4,0
	movl	w5,0x00202848
	movl	w6,0x00101428
	mov	x7,200
	mov	w8,0
	bl	ga_panel
	mov	x0,0
	mov	x1,HUD-1
	mov	x2,GF_W
	mov	x3,1
	movl	w4,0x004068C0
	bl	gf_rect
	mov	x0,TX_SCORE
	mov	x1,10
	ldr	x2,[x28,g_score]
	bl	hud_item
	mov	x0,TX_BEST
	mov	x1,148
	ldr	x2,[x28,g_best]
	bl	hud_item
	mov	x0,TX_WAVE
	mov	x1,290
	ldr	x2,[x28,g_wave]
	add	x2,x2,1
	bl	hud_item
	mov	x19,0				; la flotte
.r:	mov	x20,0
.c:	mov	x0,ACOLS
	mul	x0,x19,x0
	add	x0,x0,x20
	add	x0,x0,g_alive
	ldrb	w0,[x28,x0]
	cbz	w0,.nc
	mov	x0,x19
	mov	x1,x20
	bl	alien_xy
	sub	x21,x0,GLOW
	sub	x22,x1,GLOW
	mov	x0,x19
	bl	alien_sprite
	mov	x0,x21
	mov	x1,x22
	bl	ga_ablit
.nc:	add	x20,x20,1
	cmp	x20,ACOLS
	b.lo	.c
	add	x19,x19,1
	cmp	x19,AROWS
	b.lo	.r
	ldr	x1,[x28,g_boomt]		; l'explosion
	cbz	x1,.ufo
	ldr	x0,[x28,g_now]
	sub	x0,x0,x1
	cmp	x0,200
	b.hs	.ufo
	ldr	x0,[x28,g_boomx]
	sub	x0,x0,GLOW
	ldr	x1,[x28,g_boomy]
	sub	x1,x1,GLOW
	ldr	x2,[x28,g_sboom]
	bl	ga_ablit
.ufo:	ldr	x0,[x28,g_ufo]
	cbz	x0,.shld
	ldr	x0,[x28,g_ufox]
	sub	x0,x0,GLOW
	mov	x1,UFO_Y-GLOW
	ldr	x2,[x28,g_sufo]
	bl	ga_ablit
.shld:	mov	x19,0				; les boucliers, en degrade
	add	x20,x28,g_shield
.s:	mov	x0,x19
	bl	shield_x
	mov	x21,x0
	ldv	x7,gf_dst
	movl	x8,SHIELDY*GF_PITCH
	add	x7,x7,x8
	add	x7,x7,x21,lsl 2
	mov	x9,GF_PITCH
	adr	x10,shield_col
	mov	x5,SHH
.sy:	ldr	w4,[x10],4
	mov	x6,0
.sx:	ldrb	w0,[x20],1
	cbz	w0,.sn
	add	x1,x7,x6,lsl 3
	str	w4,[x1]
	str	w4,[x1,4]
	add	x1,x1,x9
	str	w4,[x1]
	str	w4,[x1,4]
.sn:	add	x6,x6,1
	cmp	x6,SHW
	b.lo	.sx
	add	x7,x7,x9,lsl 1
	subs	x5,x5,1
	b.ne	.sy
	add	x19,x19,1
	cmp	x19,NSHIELD
	b.lo	.s
	ldr	x0,[x28,g_state]		; le canon
	cmp	x0,ST_OVER
	b.eq	.shot
	ldr	x2,[x28,g_sship]
	cmp	x0,ST_HIT
	b.ne	.ship
	ldr	x0,[x28,g_now]
	mov	x1,100
	udiv	x0,x0,x1
	tbz	x0,0,.ship
	ldr	x2,[x28,g_sboom]
.ship:	ldr	x0,[x28,g_shipx]
	sub	x0,x0,GLOW
	mov	x1,SHIPY-GLOW
	bl	ga_ablit
.shot:	ldr	x0,[x28,g_shot]
	cbz	x0,.bomb
	ldr	x0,[x28,g_shotx]
	sub	x0,x0,1+GLOW
	ldr	x1,[x28,g_shoty]
	sub	x1,x1,GLOW
	ldr	x2,[x28,g_sshot]
	bl	ga_ablit
.bomb:	add	x19,x28,g_bomb
	mov	x20,3
.bm:	ldr	x0,[x19]
	cbz	x0,.nb
	ldp	x0,x1,[x19,8]
	lsr	x2,x1,2
	and	x2,x2,1
	add	x3,x28,g_sbomb
	ldr	x2,[x3,x2,lsl 3]
	sub	x0,x0,1+GLOW
	sub	x1,x1,GLOW
	bl	ga_ablit
.nb:	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.bm
	bl	ga_parts_draw
	ldr	x0,[x28,g_ufost]		; les points de la soucoupe
	cbz	x0,.lives
	ldr	x0,[x28,g_ufos]
	ldr	x1,[x28,g_ufosx]
	add	x1,x1,UFO_W/2
	mov	x2,UFO_Y-4
	add	x3,x28,g_dsm
	mov	x4,5
	bl	ga_num
.lives:	ldr	x0,[x28,g_lives]		; les vies
	mov	x1,10
	mov	x2,GROUND+5
	add	x3,x28,g_dig
	mov	x4,4
	bl	ga_num
	mov	x19,0
.lv:	ldr	x0,[x28,g_lives]
	sub	x0,x0,1
	cmp	x19,x0
	b.ge	.msg
	cmp	x19,5
	b.hs	.msg
	mov	x0,28
	mul	x0,x0,x19
	add	x0,x0,32
	mov	x1,GROUND+6
	ldr	x2,[x28,g_slife]
	bl	ga_ablit
	add	x19,x19,1
	b	.lv
.msg:	ldr	x0,[x28,g_state]
	cmp	x0,ST_OVER
	b.ne	.m2
	mov	x0,150
	mov	x1,172
	bl	glass
	ldr	x0,[x28,g_tx+TX_OVER*8]
	mov	x1,GF_W/2
	mov	x2,164
	bl	ga_show
	ldr	x0,[x28,g_score]
	mov	x1,GF_W/2
	mov	x2,208
	add	x3,x28,g_dig
	mov	x4,5
	bl	ga_num
	ldr	x0,[x28,g_newbest]
	cbz	x0,.ag
	ldr	x0,[x28,g_tx+TX_NEWBEST*8]
	mov	x1,GF_W/2
	mov	x2,240
	bl	ga_show
.ag:	ldr	x0,[x28,g_tx+TX_AGAIN*8]
	mov	x1,GF_W/2
	mov	x2,280
	bl	ga_show
	b	.out
.m2:	cmp	x0,ST_CLEAR
	b.ne	.out
	mov	x0,196
	mov	x1,64
	bl	glass
	ldr	x0,[x28,g_tx+TX_CLEAR*8]
	mov	x1,GF_W/2
	mov	x2,210
	bl	ga_show
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les textes (dans le segment de code : tx_table s'en sert a l'adresse relative)
	align 4
tx_table:
	ga_txe	s_logo,s_base,58,1,0x00C8FFF0,0x0040B0FF
	ga_txe	s_sub,s_base,15,0,0x00D0D8F0,0x0098A4C8
	ga_txe	s_prompt,s_base,18,1,0x00FFFFFF,0x00D0E0FF
	ga_txe	s_record,s_base,15,1,0x00B8C4E0,0x008C98B8
	ga_txe	s_over,s_base,26,1,0x00FFD878,0x00F09040
	ga_txe	s_again,s_base,13,0,0x00D0D8F0,0x00A0A8C8
	ga_txe	s_newbest,s_base,15,1,0x00FFE080,0x00F0B040
	ga_txe	s_clear,s_base,24,1,0x00C8FFD0,0x0060D890
	ga_txe	s_score,s_base,12,0,0x0098A8C8,0x007888A8
	ga_txe	s_record,s_base,12,0,0x0098A8C8,0x007888A8
	ga_txe	s_wave,s_base,12,0,0x0098A8C8,0x007888A8
s_base:
s_logo		db 'INVADERS',0
s_sub		db 'La flotte descend : tenez la ligne',0
s_prompt	db 'Espace, A ou toucher pour jouer',0
s_record	db 'Record',0
s_over		db 'Partie termin',0xC3,0xA9,'e',0
s_again		db 'Espace, A ou toucher : nouvelle partie',0
s_newbest	db 'Nouveau record !',0
s_clear		db 'Vague termin',0xC3,0xA9,'e !',0
s_score		db 'Score',0
s_wave		db 'Vague',0
	align 4
; rangee -> sorte (calmar, crabe, poulpe) ; sorte -> points ; dessins a calculer
alien_kind	db 0,1,1,2,2
	align 4
kind_pts	dh 30,20,10
	align 4
neon_tab:
	dw spr_squid-spr_ship, 8, COL_SQUID
	dw spr_squid+64-spr_ship, 8, COL_SQUID
	dw spr_crab-spr_ship, 11, COL_CRAB
	dw spr_crab+88-spr_ship, 11, COL_CRAB
	dw spr_octo-spr_ship, 12, COL_OCTO
	dw spr_octo+96-spr_ship, 12, COL_OCTO
shield_col:
	dw 0x0090FFD0,0x0084F8C4,0x0078F0B8,0x006CE8AC,0x0060E0A0,0x0058D498,0x0050C890,0x0048BC88
	dw 0x0040B080,0x0038A478,0x00349870,0x00308C68,0x002C8060,0x00287458,0x00246850,0x00205C48
spr_shot	db '####'
spr_bomb	db '#..##..#','.##..##.'

; les sprites (deux images par alien, a la suite)
spr_ship:
	db '......#......','.....###.....','.....###.....','.###########.'
	db '#############','#############','#############','#############'
spr_squid:
	db '...##...','..####..','.######.','##.##.##','########','..#..#..','.#.##.#.','#.#..#.#'
	db '...##...','..####..','.######.','##.##.##','########','.#.##.#.','#......#','.#....#.'
spr_crab:
	db '..#.....#..','...#...#...','..#######..','.##.###.##.','###########','#.#######.#','#.#.....#.#','...##.##...'
	db '..#.....#..','#..#...#..#','#.#######.#','###.###.###','###########','.#########.','..#.....#..','.#.......#.'
spr_octo:
	db '....####....','.##########.','############','###..##..###','############','...##..##...','..##.##.##..','##........##'
	db '....####....','.##########.','############','###..##..###','############','..###..###..','.##..##..##.','..##....##..'
spr_ufo:
	db '.....######.....','...##########...','..############..','.##.##.##.##.##.','################','..###..##..###..','...#........#...'
spr_boom:
	db '#...#..#...#','.#...##...#.','..#......#..','##........##','..#......#..','.#..#..#..#.','#...#..#...#','............'
shield_rows:
	db '....#######','...########','..#########','.##########','###########','###########'
	db '###########','###########','###########','###########','###########','#######....'
	db '######.....','#####......','#####......','#####......'
	align 4
alien_w	db 8,11,11,12,12
	align 4
alien_spr dw spr_squid-spr_ship, spr_crab-spr_ship, spr_crab-spr_ship, spr_octo-spr_ship, spr_octo-spr_ship
alien_col dw COL_SQUID, COL_CRAB, COL_CRAB, COL_OCTO, COL_OCTO
bass	dh 98, 87, 78, 73
ufo_pts	dh 50, 100, 150, 300
snd_fire:
	gs_ent	0,1600,40,AKW_SAW,60
	gs_ent	40,1100,40,AKW_SAW,50
	gs_end
snd_ufo:
	gs_ent	0,880,80,AKW_SQUARE,90
	gs_ent	90,660,80,AKW_SQUARE,90
	gs_ent	180,440,120,AKW_SQUARE,90
	gs_end
	align 4

segment readable writeable
kit_data
s_title		db 'Invaders',0
p_dir		db 'SD0:/home/invaders',0
p_best		db 'SD0:/home/invaders/best.txt',0
s_menus		db 'MPartie',10
		db 'I1',9,'Nouvelle partie',9,'Ctrl+N',10
		db 'I2',9,'Pause',9,'P',10
		db 'I3c',9,'Son',10
		db '-',10
		db 'I4',9,'Quitter',10,0
		gs_data
		align 8
g_data		rb G_SIZE
ui_data
ga_data
gp_data
gf_data
