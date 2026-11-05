; ============================================================================
;  same - le SameGame d'Onyx (user/Apps/same) : 18 x 13 billes de 4 couleurs ; un
;  groupe de 2 billes ou plus s'en va, les colonnes tombent, les colonnes vides
;  se referment a gauche ; (n-1)^2 points. Memes regles, habillees : billes
;  eclairees dans des alveoles, groupe survole qui luit, chute et glissement
;  animes, eclats, son, record.
;
;  Souris ou doigt ; fleches et Espace ; R : nouvelle partie ; manette : la croix
;  deplace le curseur, A retire le groupe. Plein ecran ; Start, Echap ou le
;  bouton || : le menu de pause (gamepause.inc).
;  Taille logique 640 x 480.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'
include '../../inc/pad.inc'

GF_W		= 640
GF_H		= 480
GW		= 18
GH		= 13
NCOL		= 4
NCELLS		= GW*GH
CELL		= 33
BALL		= 31
OX		= 23
OY		= 42
HUD_H		= 36

ST_TITLE	= 0
ST_PLAY		= 1
ST_OVER		= 2

ID_NEW		= 1
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
TX_NOMOVE	= 4
TX_EMPTY	= 5
TX_AGAIN	= 6
TX_NEWBEST	= 7
NTX		= 8

virtual at 0
g_state		rd 1
g_timer		rd 1
g_tick		rd 1
g_score		rd 1
g_show		rd 1
g_best		rd 1
g_newbest	rd 1
g_left		rd 1			; billes restantes
g_busy		rd 1			; des billes bougent
g_vy		rd 1
g_hc		rd 1			; case visee (-1 : aucune)
g_hr		rd 1
g_hn		rd 1			; taille du groupe vise (0 : pas de groupe)
g_cursor	rd 1			; 1 : le curseur clavier est montre
g_flt		rd 1			; le gain qui monte : reste, valeur, x, y
g_fltv		rd 1
g_fltx		rd 1
g_flty		rd 1
g_pad		rd 1
g_ldirty	rd 1
g_sb		rd NCOL			; billes
g_sh		rd NCOL			; lueurs
g_ssock		rd 1
g_scur		rd 1
g_sorb		rd 2
g_dbig		rd 10
g_dsmall	rd 10
g_dgain		rd 10
g_tx		rd NTX
g_grid		rb NCELLS		; 0 vide, 1..NCOL
g_hover		rb NCELLS		; le groupe vise
g_mark		rb NCELLS
		align 8
g_oy		rb NCELLS*2		; retard de chaque bille, en points
g_ox		rb NCELLS*2
g_stk		rb NCELLS*2
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
	mov	x4,4
	bl	gp_init
	mov	x0,1
	str	x0,[x28,g_ldirty]
	mov	x0,-1
	str	x0,[x28,g_hc]
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
	mov	x19,0
.b:	adr	x0,colors
	ldr	w20,[x0,x19,lsl 2]
	mov	x0,BALL
	mov	w1,w20
	bl	ga_ball
	add	x1,x28,g_sb
	str	x0,[x1,x19,lsl 3]
	mov	x0,CELL+10
	mov	w1,w20
	bl	ga_disc
	add	x1,x28,g_sh
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,NCOL
	b.lo	.b
	mov	x0,140
	movl	w1,0x00281838
	bl	ga_disc
	str	x0,[x28,g_sorb]
	mov	x0,180
	movl	w1,0x00102838
	bl	ga_disc
	str	x0,[x28,g_sorb+8]
	mov	x0,CELL				; le curseur du clavier
	mov	x1,CELL
	bl	ga_new
	str	x0,[x28,g_scur]
	mov	x1,0
	mov	x2,0
.cu:	cmp	x2,3
	b.lo	.cw
	mov	x3,CELL-3
	cmp	x2,x3
	b.hs	.cw
	cmp	x1,3
	b.lo	.cw
	cmp	x1,x3
	b.lo	.cn
.cw:	mov	x3,CELL
	mul	x3,x1,x3
	add	x3,x3,x2
	add	x3,x0,x3,lsl 2
	movl	w4,0xFFFFFFFF
	str	w4,[x3,8]
.cn:	add	x2,x2,1
	cmp	x2,CELL
	b.lo	.cu
	mov	x2,0
	add	x1,x1,1
	cmp	x1,CELL
	b.lo	.cu
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	add	x0,x28,g_dbig
	mov	x1,20
	adr	x2,ui_font_b
	movl	w3,0x00FFFFFF
	movl	w4,0x00E8C8FF
	bl	ga_digits
	add	x0,x28,g_dsmall
	mov	x1,14
	adr	x2,ui_font_b
	movl	w3,0x00E0E4F0
	movl	w4,0x00B0B8D0
	bl	ga_digits
	add	x0,x28,g_dgain
	mov	x1,26
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

build_layer:
	prologue
	ga_push
	str	xzr,[x28,g_ldirty]
	movl	w0,0x00241438
	movl	w1,0x000C0A1C
	movl	w2,0x005C2C70
	mov	x3,70
	bl	ga_sky
	ldv	x0,gf_layer
	stv	x0,gf_dst
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.eq	.done
	; le plateau et ses alveoles
	mov	x0,OX-7
	mov	x1,OY-6
	mov	x2,GW*CELL+14
	mov	x3,GH*CELL+11
	mov	x4,12
	movl	w5,0x00584868
	movl	w6,0x00281C34
	mov	x7,256
	movl	w8,0x00B098C8
	bl	ga_panel
	mov	x0,OX-3
	mov	x1,OY-2
	mov	x2,GW*CELL+6
	mov	x3,GH*CELL+3
	mov	x4,8
	movl	w5,0x00140E20
	movl	w6,0x001C1428
	mov	x7,256
	mov	w8,0
	bl	ga_panel
	mov	x19,0
.r:	mov	x20,0
.c:	mov	x0,CELL
	mul	x1,x19,x0
	add	x1,x1,OY+2
	mul	x0,x20,x0
	add	x0,x0,OX+2
	mov	x2,CELL-4
	mov	x3,CELL-4
	mov	x4,(CELL-4)/2
	movl	w5,0x000A0612
	movl	w6,0x00221A30
	mov	x7,256
	mov	w8,0
	bl	ga_panel
	add	x20,x20,1
	cmp	x20,GW
	b.lo	.c
	add	x19,x19,1
	cmp	x19,GH
	b.lo	.r
	mov	x0,OX-7
	mov	x1,OY-6
	mov	x2,GW*CELL+14
	mov	x3,GH*CELL+11
	bl	ga_mark
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	adr	x0,s_score
	mov	x1,OX
	mov	x2,24
	movl	w3,0x00C8BCD8
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	adr	x0,s_left
	mov	x1,212
	mov	x2,24
	movl	w3,0x00C8BCD8
	mov	x4,13
	adr	x5,ui_font_b
	bl	gf_text
	adr	x0,s_best
	mov	x1,362
	mov	x2,24
	movl	w3,0x00C8BCD8
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
new_game:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x28,g_grid
	mov	x20,0
.c:	mov	x0,NCOL
	bl	gf_below
	add	x0,x0,1
	strb	w0,[x19,x20]
	add	x20,x20,1
	cmp	x20,NCELLS
	b.lo	.c
	add	x0,x28,g_ox			; (elles entrent par le haut, un peu decalees)
	mov	x1,NCELLS
.o:	strh	wzr,[x0],2
	subs	x1,x1,1
	b.ne	.o
	add	x19,x28,g_oy
	mov	x20,0
.y:	mov	x0,60
	bl	gf_below
	add	x0,x0,(GH+1)*CELL
	strh	w0,[x19,x20,lsl 1]
	add	x20,x20,1
	cmp	x20,NCELLS
	b.lo	.y
	mov	x0,1
	str	x0,[x28,g_busy]
	mov	x0,4
	str	x0,[x28,g_vy]
	str	xzr,[x28,g_score]
	str	xzr,[x28,g_show]
	str	xzr,[x28,g_newbest]
	str	xzr,[x28,g_hn]
	str	xzr,[x28,g_flt]
	mov	x0,NCELLS
	str	x0,[x28,g_left]
	mov	x0,1
	str	x0,[x28,g_ldirty]
	bl	ga_parts_clear
	mov	x0,ST_PLAY
	str	x0,[x28,g_state]
	str	xzr,[x28,g_timer]
	mov	x0,-1
	str	x0,[x28,g_hc]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = colonne, x1 = ligne, x2 = marques (NCELLS octets), -> x0 = taille du groupe
; (pile bornee : une case est empilee une seule fois)
flood:
	mov	x14,x30
	add	x3,x28,g_grid
	mov	x4,GW
	madd	x5,x1,x4,x0			; case de depart
	ldrb	w6,[x3,x5]			; sa couleur
	mov	x7,0
.z:	strb	wzr,[x2,x7]
	add	x7,x7,1
	cmp	x7,NCELLS
	b.lo	.z
	mov	x0,0				; taille
	cbz	w6,.out
	add	x7,x28,g_stk
	mov	x8,0				; hauteur
	mov	w9,1
	strb	w9,[x2,x5]
	strh	w5,[x7],2
	mov	x8,1
.pop:	cbz	x8,.out
	sub	x8,x8,1
	ldrh	w5,[x7,-2]!
	add	x0,x0,1
	udiv	x10,x5,x4			; ligne
	msub	x11,x10,x4,x5			; colonne
	cbz	x11,.e				; gauche
	sub	x12,x5,1
	bl	.push
.e:	add	x13,x11,1
	cmp	x13,GW
	b.hs	.u
	add	x12,x5,1
	bl	.push
.u:	cbz	x10,.d
	sub	x12,x5,GW
	bl	.push
.d:	add	x13,x10,1
	cmp	x13,GH
	b.hs	.pop
	add	x12,x5,GW
	bl	.push
	b	.pop
.out:	br	x14
.push:	ldrb	w13,[x2,x12]
	cbnz	w13,.pr
	ldrb	w13,[x3,x12]
	cmp	w13,w6
	b.ne	.pr
	strb	w9,[x2,x12]
	strh	w12,[x7],2
	add	x8,x8,1
.pr:	ret

; la bille (x0, x1) -> plus rien ne bouge ? recalcule le groupe vise
hover_at:
	str	x0,[x28,g_hc]
	str	x1,[x28,g_hr]
; le groupe sous la case visee
hover_update:
	prologue
	str	xzr,[x28,g_hn]
	ldr	x0,[x28,g_hc]
	tbnz	x0,63,.out
	ldr	x1,[x28,g_busy]
	cbnz	x1,.out
	ldr	x1,[x28,g_state]
	cmp	x1,ST_PLAY
	b.ne	.out
	ldr	x1,[x28,g_hr]
	add	x2,x28,g_hover
	bl	flood
	cmp	x0,2
	mov	x1,0
	csel	x0,x1,x0,lo
	str	x0,[x28,g_hn]
.out:	epilogue

; x0 = colonne, x1 = ligne : un coup
play:
	prologue
	ga_push
	mov	x19,x0
	mov	x20,x1
	ldr	x2,[x28,g_busy]
	cbnz	x2,.out
	add	x2,x28,g_mark
	bl	flood
	mov	x21,x0
	cmp	x0,2
	b.hs	.go
	cbz	x0,.out
	mov	x0,140				; pas de groupe
	mov	x1,60
	mov	x2,AKW_SQUARE
	mov	x3,40
	bl	gs_note
	b	.out
.go:	sub	x0,x21,1			; (n-1)^2
	mul	x22,x0,x0
	ldr	x1,[x28,g_score]
	add	x1,x1,x22
	str	x1,[x28,g_score]
	ldr	x1,[x28,g_left]
	sub	x1,x1,x21
	str	x1,[x28,g_left]
	mov	x0,70				; le gain, la ou l'on a touche
	str	x0,[x28,g_flt]
	str	x22,[x28,g_fltv]
	mov	x0,CELL
	mul	x1,x19,x0
	add	x1,x1,OX+CELL/2
	str	x1,[x28,g_fltx]
	mul	x1,x20,x0
	add	x1,x1,OY
	str	x1,[x28,g_flty]
	add	x23,x28,g_mark			; les billes partent en eclats
	add	x24,x28,g_grid
	mov	x25,0				; case
.c:	ldrb	w0,[x23,x25]
	cbz	w0,.n
	ldrb	w0,[x24,x25]
	strb	wzr,[x24,x25]
	sub	x0,x0,1
	adr	x1,colors
	ldr	w2,[x1,x0,lsl 2]
	mov	x4,GW
	udiv	x1,x25,x4
	msub	x0,x1,x4,x25
	mov	x4,CELL
	mul	x0,x0,x4
	add	x0,x0,OX+CELL/2
	mul	x1,x1,x4
	add	x1,x1,OY+CELL/2
	mov	x3,8
	movl	x4,150000
	mov	x5,20
	mov	x6,3
	movl	x7,7000
	bl	ga_burst
.n:	add	x25,x25,1
	cmp	x25,NCELLS
	b.lo	.c
	mov	x0,x21				; le son monte avec le groupe
	mov	x1,14
	cmp	x0,x1
	csel	x0,x1,x0,hi
	lsl	x0,x0,5
	add	x0,x0,300
	mov	x1,90
	mov	x2,AKW_TRIANGLE
	mov	x3,110
	bl	gs_note
	mov	x0,120
	mov	x1,50
	mov	x2,AKW_NOISE
	mov	x3,40
	bl	gs_note
	bl	collapse
	str	xzr,[x28,g_hn]
.out:	ga_pop
	epilogue

; les colonnes tombent, les vides se referment ; chaque bille garde son retard
collapse:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x9,x28,g_grid
	add	x10,x28,g_oy
	add	x11,x28,g_ox
	mov	x12,GW
	mov	x0,0				; colonne
.col:	mov	x1,GH-1				; ecriture
	mov	x2,GH-1				; lecture
.r:	madd	x3,x2,x12,x0
	ldrb	w4,[x9,x3]
	cbz	w4,.rn
	madd	x5,x1,x12,x0
	strb	w4,[x9,x5]
	sub	x6,x1,x2
	mov	x7,CELL
	mul	x6,x6,x7
	strh	w6,[x10,x5,lsl 1]
	sub	x1,x1,1
.rn:	subs	x2,x2,1
	b.pl	.r
.z:	tbnz	x1,63,.next
	madd	x5,x1,x12,x0
	strb	wzr,[x9,x5]
	strh	wzr,[x10,x5,lsl 1]
	sub	x1,x1,1
	b	.z
.next:	add	x0,x0,1
	cmp	x0,GW
	b.lo	.col
	mov	x0,0				; lecture
	mov	x1,0				; ecriture
.c2:	mov	x2,GH-1
	madd	x3,x2,x12,x0
	ldrb	w4,[x9,x3]
	cbz	w4,.c2n
	cmp	x0,x1
	b.eq	.c2k
	sub	x6,x0,x1
	mov	x7,CELL
	mul	x6,x6,x7
	mov	x2,0
.mv:	madd	x3,x2,x12,x0
	madd	x5,x2,x12,x1
	ldrb	w4,[x9,x3]
	strb	w4,[x9,x5]
	ldrh	w4,[x10,x3,lsl 1]
	strh	w4,[x10,x5,lsl 1]
	strh	w6,[x11,x5,lsl 1]
	add	x2,x2,1
	cmp	x2,GH
	b.lo	.mv
.c2k:	add	x1,x1,1
.c2n:	add	x0,x0,1
	cmp	x0,GW
	b.lo	.c2
.c3:	cmp	x1,GW
	b.hs	.end
	mov	x2,0
.c3r:	madd	x5,x2,x12,x1
	strb	wzr,[x9,x5]
	strh	wzr,[x10,x5,lsl 1]
	strh	wzr,[x11,x5,lsl 1]
	add	x2,x2,1
	cmp	x2,GH
	b.lo	.c3r
	add	x1,x1,1
	b	.c3
.end:	mov	x0,1
	str	x0,[x28,g_busy]
	mov	x0,4
	str	x0,[x28,g_vy]
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 si un coup reste
moves_left:
	add	x9,x28,g_grid
	mov	x0,0
	mov	x1,0				; ligne
.r:	mov	x2,0				; colonne
.c:	mov	x3,GW
	madd	x3,x1,x3,x2
	ldrb	w4,[x9,x3]
	cbz	w4,.n
	add	x5,x2,1
	cmp	x5,GW
	b.hs	.d
	add	x6,x3,1
	ldrb	w6,[x9,x6]
	cmp	w6,w4
	b.eq	.yes
.d:	add	x5,x1,1
	cmp	x5,GH
	b.hs	.n
	add	x6,x3,GW
	ldrb	w6,[x9,x6]
	cmp	w6,w4
	b.eq	.yes
.n:	add	x2,x2,1
	cmp	x2,GW
	b.lo	.c
	add	x1,x1,1
	cmp	x1,GH
	b.lo	.r
	ret
.yes:	mov	x0,1
	ret

settled:
	prologue
	str	xzr,[x28,g_busy]
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.ne	.out
	bl	moves_left
	cbnz	x0,.more
	mov	x0,ST_OVER
	str	x0,[x28,g_state]
	str	xzr,[x28,g_timer]
	ldr	x2,[x28,g_score]
	ldr	x0,[x28,g_best]
	cmp	x2,x0
	b.ls	.lose
	str	x2,[x28,g_best]
	mov	x0,1
	str	x0,[x28,g_newbest]
	adr	x0,p_dir
	adr	x1,p_best
	bl	gf_save_best
.lose:	adr	x0,gs_tab_win
	ldr	x1,[x28,g_left]
	cbz	x1,.s
	adr	x0,gs_tab_lose
.s:	bl	gs_seq
	b	.out
.more:	bl	hover_update
.out:	epilogue

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
	b.ls	.f
	lsr	x2,x2,3
	add	x0,x0,x2
	add	x0,x0,1
	str	x0,[x28,g_show]
.f:	ldr	x0,[x28,g_flt]
	cbz	x0,.b
	sub	x0,x0,1
	str	x0,[x28,g_flt]
.b:	ldr	x0,[x28,g_busy]
	cbz	x0,.out
	ldr	x3,[x28,g_vy]			; d'abord tout tombe
	add	x4,x3,1
	cmp	x4,22
	b.hi	.v
	str	x4,[x28,g_vy]
.v:	add	x0,x28,g_oy
	mov	x1,NCELLS
	mov	x2,0
.y:	ldrh	w4,[x0]
	cbz	w4,.yn
	mov	x2,1
	subs	x4,x4,x3
	csel	x4,xzr,x4,lo
	strh	w4,[x0]
.yn:	add	x0,x0,2
	subs	x1,x1,1
	b.ne	.y
	cbnz	x2,.out
	add	x0,x28,g_ox			; puis les colonnes se referment
	mov	x1,NCELLS
.x:	ldrh	w4,[x0]
	cbz	w4,.xn
	mov	x2,1
	subs	x4,x4,12
	csel	x4,xzr,x4,lo
	strh	w4,[x0]
.xn:	add	x0,x0,2
	subs	x1,x1,1
	b.ne	.x
	cbnz	x2,.out
	bl	settled
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
	b.ne	.c
	bl	new_game
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

; Entree, clic hors de la partie -> x0 = 1 si cela a servi
start_act:
	prologue
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.eq	.no
	cmp	x0,ST_OVER
	b.ne	.new
	ldr	x0,[x28,g_timer]
	cmp	x0,25
	b.lo	.yes
.new:	bl	new_game
.yes:	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	epilogue

; x0, x1 = decalage du curseur
cursor_move:
	prologue
	ldr	x2,[x28,g_hc]
	tbz	x2,63,.h
	mov	x2,GW/2
	mov	x3,GH/2
	b	.s
.h:	ldr	x3,[x28,g_hr]
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
.s:	mov	x0,1
	str	x0,[x28,g_cursor]
	mov	x0,x2
	mov	x1,x3
	bl	hover_at
	epilogue

cursor_play:
	prologue
	ldr	x0,[x28,g_hc]
	tbnz	x0,63,.out
	ldr	x1,[x28,g_hr]
	bl	play
.out:	epilogue

; (gamepause) pas de temps qui court : rien a arreter
game_pause:
	mov	x0,0
game_resume:
	ret

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
	cmp	w0,'r'
	b.ne	.c
	bl	new_game
	b	.out
.c:	ldr	w2,[x1,EV_CODE]
	cmp	w2,KEY_SPACE
	b.eq	.sp
	cmp	w2,KEY_ENTER
	b.ne	.l
.sp:	bl	start_act
	cbnz	x0,.out
	bl	cursor_play
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
.mv:	bl	cursor_move
.out:	epilogue

on_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	gf_mouse
	sub	x2,x0,OX
	sub	x3,x1,OY
	mov	x4,CELL
	sdiv	x19,x2,x4
	sdiv	x20,x3,x4
	cmp	x2,0
	b.lt	.off
	cmp	x3,0
	b.lt	.off
	cmp	x19,GW
	b.hs	.off
	cmp	x20,GH
	b.lo	.on
.off:	mov	x19,-1
	mov	x20,0
.on:	adr	x2,ui_ev
	ldr	w3,[x2,EV_KIND]
	cmp	w3,1
	b.eq	.press
	cbnz	w3,.out
	ldr	x0,[x28,g_hc]			; le survol
	ldr	x1,[x28,g_hr]
	cmp	x0,x19
	b.ne	.upd
	cmp	x1,x20
	b.eq	.out
.upd:	str	xzr,[x28,g_cursor]
	mov	x0,x19
	mov	x1,x20
	bl	hover_at
	b	.out
.press:	ldr	w3,[x2,EV_CODE]
	tbz	w3,0,.out
	mov	x0,HUD_H-6
	bl	gf_drag_top
	cbnz	x0,.out
	bl	start_act
	cbnz	x0,.out
	tbnz	x19,63,.out
	str	xzr,[x28,g_cursor]
	mov	x0,x19
	mov	x1,x20
	bl	hover_at
	mov	x0,x19
	mov	x1,x20
	bl	play
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
	b.eq	.m
	bl	start_act
	cbnz	x0,.out
	bl	cursor_play
.m:	tst	x20,PAD_LEFT
	b.eq	.r
	mov	x0,-1
	mov	x1,0
	bl	cursor_move
.r:	tst	x20,PAD_RIGHT
	b.eq	.u
	mov	x0,1
	mov	x1,0
	bl	cursor_move
.u:	tst	x20,PAD_UP
	b.eq	.d
	mov	x0,0
	mov	x1,-1
	bl	cursor_move
.d:	tst	x20,PAD_DOWN
	b.eq	.out
	mov	x0,0
	mov	x1,1
	bl	cursor_move
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
draw_board:
	prologue
	ga_push
	add	x19,x28,g_grid
	add	x20,x28,g_oy
	add	x21,x28,g_ox
	add	x22,x28,g_hover
	mov	x23,0				; case
	ldr	x25,[x28,g_tick]		; la lueur du groupe bat
	lsl	x0,x25,3
	bl	ga_sin8
	mov	x26,x0
.c:	ldrb	w0,[x19,x23]
	cbz	w0,.n
	mov	x4,GW
	udiv	x1,x23,x4
	msub	x2,x1,x4,x23
	mov	x4,CELL
	mul	x2,x2,x4
	add	x2,x2,OX+1
	mul	x1,x1,x4
	add	x1,x1,OY+1
	ldrh	w4,[x20,x23,lsl 1]
	sub	x1,x1,x4
	ldrh	w4,[x21,x23,lsl 1]
	add	x2,x2,x4
	mov	x24,x0
	mov	x0,x2
	sub	x3,x1,OY-1			; (au-dessus du plateau : cachees)
	tbnz	x3,63,.n
	ldr	x3,[x28,g_hn]
	cbz	x3,.bl
	ldrb	w3,[x22,x23]
	cbz	w3,.bl
	stp	x0,x1,[sp,-16]!
	sub	x0,x0,6
	sub	x1,x1,6
	add	x2,x28,g_sh-8
	ldr	x2,[x2,x24,lsl 3]
	cmp	x26,100
	b.lo	.ng
	bl	gf_glow
.ng:	ldp	x0,x1,[sp],16
.bl:	add	x2,x28,g_sb-8
	ldr	x2,[x2,x24,lsl 3]
	bl	ga_ablit
.n:	add	x23,x23,1
	cmp	x23,NCELLS
	b.lo	.c
	ga_pop
	epilogue

; x0 = titre, x1 = suite, x2 = troisieme (0 : aucun) : la boite de fin
end_box:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,120
	mov	x1,150
	mov	x2,400
	mov	x3,180
	mov	x4,16
	movl	w5,0x00382848
	movl	w6,0x001C1428
	mov	x7,235
	movl	w8,0x00B8A0D0
	bl	ga_panel
	mov	x0,x19
	mov	x1,GF_W/2
	mov	x2,166
	bl	ga_show
	ldr	x0,[x28,g_score]
	mov	x1,GF_W/2
	mov	x2,208
	add	x3,x28,g_dgain
	mov	x4,5
	bl	ga_num
	cbz	x21,.a
	mov	x0,x21
	mov	x1,GF_W/2
	mov	x2,246
	bl	ga_show
.a:	mov	x0,x20
	mov	x1,GF_W/2
	mov	x2,288
	bl	ga_show
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

draw_title:
	prologue
	ga_push
	mov	x19,0				; une ronde de billes
	ldr	x20,[x28,g_tick]
.b:	lsl	x0,x19,3
	add	x0,x0,x19,lsl 2
	add	x0,x0,x20
	bl	ga_sin8
	mov	x21,x0
	mov	x2,x19
	and	x2,x2,NCOL-1
	add	x3,x28,g_sb
	ldr	x2,[x3,x2,lsl 3]
	mov	x0,36
	mul	x0,x19,x0
	add	x0,x0,14
	lsr	x1,x21,3
	add	x1,x1,300
	bl	ga_ablit
	lsl	x0,x19,3
	add	x0,x0,x20
	add	x0,x0,128
	bl	ga_sin8
	lsr	x1,x0,3
	add	x1,x1,80
	add	x2,x19,2
	and	x2,x2,NCOL-1
	add	x3,x28,g_sb
	ldr	x2,[x3,x2,lsl 3]
	mov	x0,36
	mul	x0,x19,x0
	add	x0,x0,14
	bl	ga_ablit
	add	x19,x19,1
	cmp	x19,17
	b.lo	.b
	ldr	x0,[x28,g_tx+TX_LOGO*8]
	mov	x1,GF_W/2
	mov	x2,120
	bl	ga_show
	ldr	x0,[x28,g_tx+TX_SUB*8]
	mov	x1,GF_W/2
	mov	x2,214
	bl	ga_show
	ldr	x0,[x28,g_tick]
	lsl	x0,x0,1
	bl	ga_sin8
	cmp	x0,40
	b.lo	.r
	ldr	x0,[x28,g_tx+TX_PROMPT*8]
	mov	x1,GF_W/2
	mov	x2,256
	bl	ga_show
.r:	ldr	x0,[x28,g_tx+TX_RECORD*8]
	mov	x1,GF_W/2-40
	mov	x2,380
	bl	ga_show
	ldr	x0,[x28,g_best]
	mov	x1,GF_W/2+8
	mov	x2,379
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
.game:	ldr	x0,[x28,g_show]
	mov	x1,70
	mov	x2,8
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x0,[x28,g_left]
	mov	x1,262
	mov	x2,8
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	ldr	x0,[x28,g_best]
	mov	x1,418
	mov	x2,8
	add	x3,x28,g_dbig
	mov	x4,4
	bl	ga_num
	bl	draw_board
	ldr	x0,[x28,g_cursor]		; le curseur du clavier
	cbz	x0,.prev
	ldr	x0,[x28,g_hc]
	tbnz	x0,63,.prev
	ldr	x1,[x28,g_hr]
	mov	x2,CELL
	mul	x0,x0,x2
	add	x0,x0,OX
	mul	x1,x1,x2
	add	x1,x1,OY
	ldr	x2,[x28,g_scur]
	bl	ga_ablit
.prev:	bl	ga_parts_draw
	ldr	x19,[x28,g_flt]			; le gain qui monte
	cbz	x19,.st
	ldr	x0,[x28,g_fltv]
	ldr	x1,[x28,g_fltx]
	ldr	x2,[x28,g_flty]
	sub	x2,x2,70
	add	x2,x2,x19
	add	x3,x28,g_dgain
	mov	x4,5
	bl	ga_num
.st:	ldr	x0,[x28,g_state]
	cmp	x0,ST_OVER
	b.ne	.out
	ldr	x0,[x28,g_tx+TX_NOMOVE*8]
	ldr	x1,[x28,g_left]
	cbnz	x1,.ebx
	ldr	x0,[x28,g_tx+TX_EMPTY*8]
.ebx:	ldr	x1,[x28,g_tx+TX_AGAIN*8]
	mov	x2,0
	ldr	x3,[x28,g_newbest]
	cbz	x3,.eb
	ldr	x2,[x28,g_tx+TX_NEWBEST*8]
.eb:	bl	end_box
.out:	ga_pop
	epilogue

; ---------------------------------------------------------------------------
;  Tables
; ---------------------------------------------------------------------------
	align 4
colors	dw 0x00E85050,0x0048C068,0x004888F0,0x00F0C840
tx_table:
	ga_txe	s_logo,s_base,78,1,0x00FFF0C0,0x00F07858
	ga_txe	s_sub,s_base,16,0,0x00E0D8F0,0x00B0A8C8
	ga_txe	s_prompt,s_base,18,1,0x00FFFFFF,0x00D8D0F0
	ga_txe	s_best,s_base,15,1,0x00C8BCD8,0x00A098B8
	ga_txe	s_nomove,s_base,26,1,0x00FFD878,0x00F09040
	ga_txe	s_empty,s_base,26,1,0x00C8FFD0,0x0060D890
	ga_txe	s_again,s_base,14,0,0x00E0D8F0,0x00B0A8C8
	ga_txe	s_newbest,s_base,16,1,0x00FFE080,0x00F0B040
s_base:
s_logo		db 'Same',0
s_sub		db 'Des groupes de m',0xC3,0xAA,'me couleur, et plus rien',0
s_prompt	db 'Espace, A ou toucher pour jouer',0
s_nomove	db 'Plus de coup possible',0
s_empty		db 'Plateau vide !',0
s_again		db 'Espace, A ou toucher : nouvelle partie',0
s_newbest	db 'Nouveau record !',0
s_title		db 'Same',0
s_score		db 'Score',0
s_left		db 'Reste',0
s_best		db 'Record',0
p_dir		db 'SD0:/home/same',0
p_best		db 'SD0:/home/same/best.txt',0
s_menus		db 'MPartie',10
		db 'I1',9,'Nouvelle partie',9,'Ctrl+N',10
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
