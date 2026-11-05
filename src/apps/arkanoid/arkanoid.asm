; ============================================================================
;  arkanoid - un casse-briques neuf dans l'esprit de LBreakout2 : briques en
;  relief et ombre portee, decors generes par niveau, lumieres qui derivent,
;  balles a trainee, debris, briques dures, indestructibles et explosives,
;  capsules de bonus. Souris, doigt ou fleches ; Espace, clic ou appui : lancer
;  (et tirer au laser). Manette : la croix deplace la raquette, A, B, X, Y (ou
;  haut) lancent et tirent. Plein ecran ; Start, Echap, P ou le bouton || : le
;  menu de pause (gamepause.inc).
;
;  Couche fixe (gf_layer) : decor, ombres, briques, bandeau, messages ; refaite
;  quand elle change. Octet haut d'un point de la couche : 0xFF premier plan
;  (ni ombre ni lumiere dessus), 0x80 deja dans l'ombre, 0 decor.
;  Taille logique 640 x 480 ; tout est genere au demarrage, rien n'est lu.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'
include '../../inc/pad.inc'

GF_W		= 640
GF_H		= 480
HUD_H		= 28
FIELD_L		= 20
FIELD_R		= 620
FIELD_T		= 36
BRICK_T		= 48
BW		= 40
BH		= 18
BSW		= BW-1			; sprite d'une brique
BSH		= BH-1
COLS		= 15
ROWS		= 14
NCELL		= COLS*ROWS
SHADOW		= 5
RKT_Y		= 446
RKT_H		= 14
BALL_R		= 6			; rayon d'une balle
BALL_D		= 13
NB		= 8			; balles
NP		= 240			; particules
NC		= 8			; capsules
NL		= 10			; tirs de laser
NF		= 8			; eclairs
NM		= 20			; poussieres
HIST		= 8			; trainee
CAP_W		= 30
CAP_H		= 16
ORB_D		= 128
FLASH_D		= 72
BANK_SIZE	= 512*1024

; etats
ST_TITLE	= 0
ST_READY	= 1
ST_PLAY		= 2
ST_PAUSE	= 3
ST_LOST		= 4
ST_CLEAR	= 5
ST_OVER		= 6

; briques (genre + 1 dans les cases)
BK_SILVER	= 8
BK_GOLD		= 10
BK_STEEL	= 13
BK_BOMB		= 14
NBRICK		= 15

; capsules
CP_WIDE		= 0
CP_NARROW	= 1
CP_MULTI	= 2
CP_SLOW		= 3
CP_FAST		= 4
CP_THRU		= 5
CP_GLUE		= 6
CP_LASER	= 7
CP_LIFE		= 8
NCAP		= 9

ID_NEW		= 1
ID_PAUSE	= 2
ID_SOUND	= 3
ID_QUIT		= 4

KEY_ENTER	= 0x28
KEY_SPACE	= 0x2C
KEY_RIGHT	= 0x4F
KEY_LEFT	= 0x50

; une balle
B_ON		= 0
B_X		= 8			; position 16.16
B_Y		= 16
B_DX		= 24			; direction 1.14
B_DY		= 32
B_STUCK		= 40
B_OFF		= 48			; collee : decalage depuis la raquette
B_T		= 56
B_HI		= 64			; dernier point de la trainee
B_H		= 72			; trainee : HIST fois x, y (32 bits, pixels)
B_SIZE		= B_H+HIST*8

; une particule
P_X		= 0			; 16.16
P_Y		= 4
P_VX		= 8
P_VY		= 12
P_LIFE		= 16
P_COL		= 20
P_KIND		= 24			; 0 etincelle, 1 debris
P_SIZE		= 32

; l'etat du jeu : x28 pointe dessus
virtual at 0
g_state		rd 1
g_prev		rd 1			; l'etat d'avant la pause
g_timer		rd 1
g_tick		rd 1
g_level		rd 1
g_lives		rd 1
g_score		rd 1
g_best		rd 1
g_px		rd 1			; raquette : gauche, taille (0..3), largeur
g_psize		rd 1
g_pw		rd 1
g_mx		rd 1
g_mmoved	rd 1
g_btn		rd 1
g_keyspd	rd 1
g_t_slow	rd 1			; effets : images restantes
g_t_fast	rd 1
g_t_thru	rd 1
g_t_glue	rd 1
g_t_laser	rd 1
g_laser_cd	rd 1
g_left		rd 1			; briques a casser
g_combo		rd 1
g_loop		rd 1			; images sans rien casser
g_fdirty	rd 1			; a refaire : briques, bandeau, toute la couche
g_hdirty	rd 1
g_ldirty	rd 1
g_label		rd 1			; le dernier bonus (texte)
g_label_t	rd 1
g_pnext		rd 1
g_bp		rd 1			; pile des sprites
g_bg		rd 1			; le decor seul
g_sb		rd NBRICK		; sprites
g_spad		rd 12
g_scap		rd NCAP
g_sball		rd 2
g_sdisc		rd 6
g_smini		rd 1
g_sorb		rd 2
g_sflash	rd 1
g_smote		rd 1
g_slaser	rd 1
g_slogo		rd 1			; le titre, en degrade (gameart)
g_cells		rb NCELL
g_fuse		rb NCELL
		align 8
g_ball		rb NB*B_SIZE
g_cap		rb NC*32		; +0 actif, +8 x, +16 y (16.16), +24 genre
g_las		rb NL*16		; +0 actif, +4 x, +8 y
g_flash		rb NF*24		; +0 reste, +8 x, +16 y
g_mote		rb NM*16		; +0 x, +4 y (16.16), +8 vitesse
G_SIZE		= $
end virtual

macro push_all {
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
}
macro pop_all {
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
}

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
	movl	x0,GF_PITCH*GF_H
	bl	malloc
	cbz	x0,quit
	str	x0,[x28,g_bg]
	bl	gs_open
	adr	x0,p_best
	bl	gf_load_best
	str	x0,[x28,g_best]
	adr	x0,g_bank
	str	x0,[x28,g_bp]
	bl	gen_noise
	bl	gen_sprites
	bl	gen_motes
	movl	x0,512*1024
	bl	ga_init
	cbz	x0,quit
	adr	x0,game_pause
	adr	x1,game_resume
	adr	x2,new_game
	mov	x3,GF_W-32
	mov	x4,0
	bl	gp_init
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	adr	x0,s_title
	mov	x1,64
	adr	x2,ui_font_b
	movl	w3,0x00FFF0B8
	movl	w4,0x00F08828
	bl	ga_text
	str	x0,[x28,g_slogo]
	bl	ui_canvas_end
	bl	goto_title
	adr	x0,on_event
	adr	x1,step
	adr	x2,draw
	bl	gf_run
	bl	gs_close
quit:	mov	x0,0
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Generation des images
; ---------------------------------------------------------------------------

; w0 = couleur, w1 = facteur (256 : tel quel) -> w0 (touche x2, x3, x4)
shade:
	mov	w4,255
	ubfx	w2,w0,16,8
	mul	w2,w2,w1
	lsr	w2,w2,8
	cmp	w2,w4
	csel	w2,w4,w2,hi
	ubfx	w3,w0,8,8
	mul	w3,w3,w1
	lsr	w3,w3,8
	cmp	w3,w4
	csel	w3,w4,w3,hi
	and	w0,w0,0xFF
	mul	w0,w0,w1
	lsr	w0,w0,8
	cmp	w0,w4
	csel	w0,w4,w0,hi
	orr	w0,w0,w3,lsl 8
	orr	w0,w0,w2,lsl 16
	ret

; x0 = phase -> x0 = 0..255, une onde en paraboles
sin8:
	and	x0,x0,255
	cmp	x0,128
	b.hs	.neg
	mov	x1,128
	sub	x1,x1,x0
	mul	x0,x0,x1
	lsr	x0,x0,5
	add	x0,x0,127
	ret
.neg:	sub	x0,x0,128
	mov	x1,128
	sub	x1,x1,x0
	mul	x0,x0,x1
	lsr	x0,x0,5
	mov	x1,127
	sub	x0,x1,x0
	ret

; x0 = largeur, x1 = hauteur -> x0 = sprite vide (dans la pile)
new_sprite:
	ldr	x2,[x28,g_bp]
	stp	w0,w1,[x2]
	mul	x3,x0,x1
	add	x3,x2,x3,lsl 2
	add	x3,x3,8
	str	x3,[x28,g_bp]
	mov	x0,x2
	ret

gen_noise:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,g_noise
	mov	x20,4096
.n:	bl	gf_rand
	lsr	x0,x0,24
	strb	w0,[x19],1
	subs	x20,x20,1
	b.ne	.n
	ldp	x19,x20,[sp],16
	epilogue

gen_sprites:
	prologue
	push_all
	mov	x19,0				; les briques
.b:	mov	x0,x19
	bl	gen_brick
	add	x1,x28,g_sb
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,NBRICK
	b.lo	.b
	mov	x19,0				; les raquettes
	add	x20,x28,g_spad
.p:	and	x0,x19,3
	lsr	x1,x19,2
	bl	gen_paddle
	str	x0,[x20,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,12
	b.lo	.p
	mov	x0,BALL_D			; les balles
	movl	w1,0x0090B8F0
	bl	gen_ball
	str	x0,[x28,g_sball]
	mov	x0,BALL_D
	movl	w1,0x00FF8830
	bl	gen_ball
	str	x0,[x28,g_sball+8]
	mov	x0,9
	movl	w1,0x00E0E8F4
	bl	gen_ball
	str	x0,[x28,g_smini]
	mov	x0,11				; les trainees (lumieres)
	movl	w1,0x00304870
	bl	gen_disc
	str	x0,[x28,g_sdisc]
	mov	x0,9
	movl	w1,0x00203450
	bl	gen_disc
	str	x0,[x28,g_sdisc+8]
	mov	x0,7
	movl	w1,0x00142038
	bl	gen_disc
	str	x0,[x28,g_sdisc+16]
	mov	x0,11
	movl	w1,0x00804010
	bl	gen_disc
	str	x0,[x28,g_sdisc+24]
	mov	x0,9
	movl	w1,0x00602808
	bl	gen_disc
	str	x0,[x28,g_sdisc+32]
	mov	x0,7
	movl	w1,0x00401804
	bl	gen_disc
	str	x0,[x28,g_sdisc+40]
	mov	x0,FLASH_D
	movl	w1,0x00FFA040
	bl	gen_disc
	str	x0,[x28,g_sflash]
	mov	x0,7
	movl	w1,0x00506070
	bl	gen_disc
	str	x0,[x28,g_smote]
	mov	x0,ORB_D			; deux lumieres, teintes par niveau
	movl	w1,0x00203040
	bl	gen_disc
	str	x0,[x28,g_sorb]
	mov	x0,ORB_D
	movl	w1,0x00301830
	bl	gen_disc
	str	x0,[x28,g_sorb+8]
	mov	x0,4				; le tir de laser
	mov	x1,14
	bl	new_sprite
	str	x0,[x28,g_slaser]
	add	x0,x0,8
	mov	x1,56
	movl	w2,0x00FFE080
.l:	str	w2,[x0],4
	subs	x1,x1,1
	b.ne	.l
	bl	gen_capsules
	pop_all
	epilogue

; x0 = genre -> x0 = sprite
gen_brick:
	prologue
	push_all
	mov	x22,x0
	adr	x1,brick_col
	ldr	w23,[x1,x22,lsl 2]
	mov	x0,BSW
	mov	x1,BSH
	bl	new_sprite
	mov	x19,x0
	add	x24,x0,8
	mov	x20,0				; y
.y:	mov	x21,0				; x
.x:	cmp	x21,0				; coins arrondis
	b.eq	.cx
	cmp	x21,BSW-1
	b.ne	.go
.cx:	cmp	x20,0
	b.eq	.next
	cmp	x20,BSH-1
	b.eq	.next
.go:	mov	w0,110				; degrade du haut vers le bas
	mul	w1,w20,w0
	mov	w0,BSH-1
	udiv	w1,w1,w0
	mov	w0,300
	sub	w1,w0,w1
	cmp	x20,6				; reflet
	b.hs	.bev
	cmp	x21,2
	b.lo	.bev
	cmp	x21,BSW-2
	b.hs	.bev
	add	w1,w1,36
.bev:	cmp	x20,0				; biseau
	b.eq	.hi
	cmp	x21,0
	b.ne	.h2
.hi:	mov	w1,450
	b	.lo
.h2:	cmp	x20,1
	b.eq	.hi2
	cmp	x21,1
	b.ne	.lo
.hi2:	add	w1,w1,70
.lo:	cmp	x20,BSH-1
	b.eq	.dk
	cmp	x21,BSW-1
	b.ne	.l2
.dk:	mov	w1,104
	b	.pat
.l2:	cmp	x20,BSH-2
	b.eq	.dk2
	cmp	x21,BSW-2
	b.ne	.pat
.dk2:	sub	w1,w1,70
.pat:	mov	w0,w23
	cmp	x22,BK_STEEL
	b.ne	.bomb
	add	x2,x20,x21			; acier : hachures, rivets
	and	x2,x2,7
	cmp	x2,2
	b.hs	.riv
	sub	w1,w1,40
.riv:	cmp	x20,3
	b.eq	.r1
	cmp	x20,BSH-4
	b.ne	.put
.r1:	cmp	x21,3
	b.eq	.r2
	cmp	x21,BSW-4
	b.ne	.put
.r2:	mov	w1,520
	b	.put
.bomb:	cmp	x22,BK_BOMB
	b.ne	.put
	cmp	x21,8				; explosive : rayures aux bouts, point au milieu
	b.lo	.st
	cmp	x21,BSW-8
	b.hs	.st
	mov	x2,x21
	sub	x2,x2,BSW/2
	mul	x2,x2,x2
	sub	x3,x20,BSH/2
	madd	x2,x3,x3,x2
	cmp	x2,14
	b.hi	.put
	movl	w0,0x00FFE040
	b	.put
.st:	add	x2,x20,x21
	and	x2,x2,7
	cmp	x2,4
	b.hs	.st1
	movl	w0,0x00F0C020
	b	.put
.st1:	movl	w0,0x00301008
.put:	bl	shade
	orr	w0,w0,0xFF000000
	str	w0,[x24]
.next:	add	x24,x24,4
	add	x21,x21,1
	cmp	x21,BSW
	b.lo	.x
	add	x20,x20,1
	cmp	x20,BSH
	b.lo	.y
	cmp	x22,BK_SILVER+1			; les fissures
	b.eq	.c1
	cmp	x22,BK_GOLD+1
	b.eq	.c1
	cmp	x22,BK_GOLD+2
	b.ne	.out
	mov	x0,x19
	mov	x1,9
	bl	crack
.c1:	mov	x0,x19
	mov	x1,25
	bl	crack
.out:	mov	x0,x19
	pop_all
	epilogue

; x0 = sprite de brique, x1 = colonne de depart : une fissure de haut en bas
crack:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	add	x19,x0,8
	mov	x20,x1
	mov	x21,1
.l:	mov	x0,3
	bl	gf_below
	add	x20,x20,x0
	sub	x20,x20,1
	mov	x0,BSW
	madd	x0,x21,x0,x20
	movl	w1,0xFF181010
	str	w1,[x19,x0,lsl 2]
	add	x21,x21,1
	cmp	x21,BSH-1
	b.lo	.l
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = taille 0..3, x1 = style 0 normal, 1 colle, 2 laser -> x0 = sprite
gen_paddle:
	prologue
	push_all
	adr	x2,pad_w
	ldrb	w22,[x2,x0]			; largeur
	mov	x23,x1
	adr	x2,pad_cap
	ldr	w24,[x2,x1,lsl 2]		; couleur des bouts
	mov	x0,x22
	mov	x1,RKT_H
	bl	new_sprite
	mov	x19,x0
	add	x25,x0,8
	mov	x20,0				; y
.y:	mov	x21,0				; x
.x:	mov	x0,x21				; bouts ronds (rayon 7)
	sub	x1,x22,1
	sub	x1,x1,x21
	cmp	x0,x1
	csel	x0,x1,x0,hi			; distance au bord
	cmp	x0,7
	b.hs	.body
	mov	x1,7
	sub	x1,x1,x0
	sub	x2,x20,6
	mul	x1,x1,x1
	mul	x3,x2,x2
	add	x1,x1,x3
	cmp	x1,52
	b.hi	.next
.body:	adr	x2,pad_prof			; profil vertical
	ldrh	w1,[x2,x20,lsl 1]
	movl	w0,0x00B4BCC8
	mov	x2,x21				; les bouts en couleur
	sub	x3,x22,1
	sub	x3,x3,x21
	cmp	x2,x3
	csel	x2,x3,x2,hi
	cmp	x2,11
	b.hs	.mid
	mov	w0,w24
.mid:	cmp	x2,11
	b.ne	.put
	mov	w1,70
.put:	bl	shade
	orr	w0,w0,0xFF000000
	str	w0,[x25]
.next:	add	x25,x25,4
	add	x21,x21,1
	cmp	x21,x22
	b.lo	.x
	add	x20,x20,1
	cmp	x20,RKT_H
	b.lo	.y
	mov	x0,x19
	pop_all
	epilogue

; x0 = diametre, w1 = couleur -> x0 = sprite : une sphere eclairee d'en haut a gauche
gen_ball:
	prologue
	push_all
	mov	x22,x0
	mov	w23,w1
	mov	x1,x0
	bl	new_sprite
	mov	x19,x0
	add	x24,x0,8
	mul	x25,x22,x22			; d^2
	mov	x20,0
.y:	mov	x21,0
.x:	lsl	x0,x21,1			; centre a 2 * point + 1 - d
	add	x0,x0,1
	sub	x0,x0,x22
	lsl	x1,x20,1
	add	x1,x1,1
	sub	x1,x1,x22
	mul	x2,x0,x0
	madd	x2,x1,x1,x2
	cmp	x2,x25
	b.hi	.next
	add	x0,x0,x22,lsr 1			; lumiere vers le coin haut gauche
	add	x1,x1,x22,lsr 1
	mul	x2,x0,x0
	madd	x2,x1,x1,x2
	mov	x3,300
	mul	x2,x2,x3
	udiv	x2,x2,x25			; 0 au reflet, grand a l'oppose
	mov	x3,440
	subs	x1,x3,x2
	mov	x3,60
	csel	x1,x3,x1,lo
	cmp	x1,x3
	csel	x1,x3,x1,lo
	mov	w0,w23
	bl	shade
	orr	w0,w0,0xFF000000
	str	w0,[x24]
.next:	add	x24,x24,4
	add	x21,x21,1
	cmp	x21,x22
	b.lo	.x
	add	x20,x20,1
	cmp	x20,x22
	b.lo	.y
	mov	x0,x19
	pop_all
	epilogue

; x0 = diametre, w1 = couleur -> x0 = sprite : une lumiere ronde (a ajouter)
gen_disc:
	prologue
	push_all
	mov	x22,x0
	mov	w23,w1
	mov	x1,x0
	bl	new_sprite
	mov	x19,x0
	add	x24,x0,8
	mul	x25,x22,x22
	mov	x20,0
.y:	mov	x21,0
.x:	lsl	x0,x21,1
	add	x0,x0,1
	sub	x0,x0,x22
	lsl	x1,x20,1
	add	x1,x1,1
	sub	x1,x1,x22
	mul	x2,x0,x0
	madd	x2,x1,x1,x2
	cmp	x2,x25
	b.hs	.next
	mov	x3,256				; 256 au centre, 0 au bord, en cloche
	mul	x2,x2,x3
	udiv	x2,x2,x25
	sub	x1,x3,x2
	mul	x1,x1,x1
	lsr	x1,x1,8
	mov	w0,w23
	bl	shade
	str	w0,[x24]
.next:	add	x24,x24,4
	add	x21,x21,1
	cmp	x21,x22
	b.lo	.x
	add	x20,x20,1
	cmp	x20,x22
	b.lo	.y
	mov	x0,x19
	pop_all
	epilogue

; les capsules : une pastille, sa lettre (ecrite par UIKit dans la couche)
gen_capsules:
	prologue
	push_all
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	mov	x19,0
.c:	mov	x0,CAP_W
	mov	x1,CAP_H
	bl	new_sprite
	add	x1,x28,g_scap
	str	x0,[x1,x19,lsl 3]
	add	x24,x0,8
	adr	x1,cap_col
	ldr	w23,[x1,x19,lsl 2]
	mov	x20,0
.y:	mov	x21,0
.x:	mov	x0,x21				; bouts ronds (rayon 8)
	mov	x1,CAP_W-1
	sub	x1,x1,x21
	cmp	x0,x1
	csel	x0,x1,x0,hi
	cmp	x0,8
	b.hs	.body
	mov	x1,8
	sub	x1,x1,x0
	lsl	x2,x20,1
	sub	x2,x2,15
	asr	x2,x2,1
	mul	x1,x1,x1
	madd	x1,x2,x2,x1
	cmp	x1,62
	b.hi	.next
.body:	mov	w0,18				; degrade, reflet en haut
	mul	w1,w20,w0
	mov	w0,330
	sub	w1,w0,w1
	cmp	x20,3
	b.hi	.sh
	add	w1,w1,110
.sh:	mov	w0,w23
	bl	shade
	orr	w0,w0,0xFF000000
	str	w0,[x24]
.next:	add	x24,x24,4
	add	x21,x21,1
	cmp	x21,CAP_W
	b.lo	.x
	add	x20,x20,1
	cmp	x20,CAP_H
	b.lo	.y
	mov	x0,0				; la lettre, en blanc sur noir
	mov	x1,0
	mov	x2,CAP_W
	mov	x3,CAP_H
	mov	w4,0
	bl	ui_fill
	adr	x0,cap_text
	add	x0,x0,x19,lsl 2
	mov	x1,CAP_W/2
	mov	x2,12
	movl	w3,0x00FFFFFF
	mov	x4,11
	adr	x5,ui_font_b
	bl	gf_text_c
	add	x0,x28,g_scap			; sa couverture devient du clair
	ldr	x0,[x0,x19,lsl 3]
	add	x0,x0,8
	ldv	x1,gf_layer
	mov	x2,CAP_H
.ty:	mov	x3,CAP_W
.tx:	ldr	w4,[x1],4
	and	w4,w4,0xFF
	cmp	w4,70
	b.lo	.tn
	ldr	w5,[x0]
	cbz	w5,.tn
	orr	w4,w4,w4,lsl 8
	orr	w4,w4,w4,lsl 8
	orr	w4,w4,0xFF000000
	str	w4,[x0]
.tn:	add	x0,x0,4
	subs	x3,x3,1
	b.ne	.tx
	add	x1,x1,GF_PITCH-CAP_W*4
	subs	x2,x2,1
	b.ne	.ty
	add	x19,x19,1
	cmp	x19,NCAP
	b.lo	.c
	bl	ui_canvas_end
	pop_all
	epilogue

gen_motes:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x28,g_mote
	mov	x20,NM
.m:	mov	x0,FIELD_R-FIELD_L-8
	bl	gf_below
	add	x0,x0,FIELD_L
	lsl	x0,x0,16
	str	w0,[x19]
	mov	x0,GF_H-HUD_H
	bl	gf_below
	add	x0,x0,HUD_H
	lsl	x0,x0,16
	str	w0,[x19,4]
	movl	x0,40000
	bl	gf_below
	movl	x1,12000
	add	x0,x0,x1
	str	w0,[x19,8]
	add	x19,x19,16
	subs	x20,x20,1
	b.ne	.m
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, x2 = style -> x0 = valeur 0..255 du decor
texture:
	prologue
	push_all
	mov	x19,x0
	mov	x20,x1
	cmp	x2,0
	b.ne	.t1
	lsr	x0,x19,1			; plasma
	bl	sin8
	mov	x21,x0
	mov	x0,x20
	bl	sin8
	add	x21,x21,x0
	add	x0,x19,x20
	lsr	x0,x0,2
	bl	sin8
	add	x21,x21,x0
	sub	x0,x19,x20,lsl 1
	asr	x0,x0,3
	bl	sin8
	add	x0,x21,x0
	lsr	x0,x0,2
	b	.out
.t1:	cmp	x2,1
	b.ne	.t2
	mov	x1,24				; paves
	udiv	x21,x20,x1
	msub	x22,x21,x1,x20			; fy
	tst	x21,1
	add	x23,x19,24
	csel	x23,x23,x19,ne			; bx
	mov	x1,48
	udiv	x24,x23,x1
	msub	x23,x24,x1,x23			; fx
	mov	x0,20
	cmp	x22,2
	b.lo	.out
	cmp	x23,2
	b.lo	.out
	mov	x1,37				; teinte du pave
	mul	x0,x21,x1
	mov	x1,101
	madd	x0,x24,x1,x0
	and	x0,x0,4095
	adr	x1,g_noise
	ldrb	w0,[x1,x0]
	lsr	x0,x0,2
	add	x0,x0,84
	cmp	x22,2
	b.eq	.th
	cmp	x23,2
	b.ne	.tl
.th:	add	x0,x0,50
.tl:	cmp	x22,23
	b.eq	.td
	cmp	x23,47
	b.ne	.out
.td:	sub	x0,x0,44
	b	.out
.t2:	cmp	x2,2
	b.ne	.t3
	add	x0,x19,x20			; tissage
	lsr	x0,x0,4
	add	x1,x19,1024
	sub	x1,x1,x20
	lsr	x1,x1,4
	eor	x0,x0,x1
	tst	x0,1
	mov	x21,110
	mov	x22,62
	csel	x21,x21,x22,ne
	lsr	x0,x20,1
	bl	sin8
	lsr	x0,x0,3
	add	x0,x0,x21
	b	.out
.t3:	cmp	x2,3
	b.ne	.t4
	mov	x0,x19				; nuages : deux octaves
	mov	x1,x20
	mov	x2,5
	bl	noise2
	mov	x21,x0
	mov	x0,x19
	mov	x1,x20
	mov	x2,4
	bl	noise2
	add	x21,x21,x21,lsl 1
	add	x0,x21,x0
	lsr	x0,x0,2
	b	.out
.t4:	cmp	x2,4
	b.ne	.t5
	mov	x0,320				; anneaux
	sub	x0,x19,x0
	sub	x1,x20,200
	mul	x0,x0,x0
	madd	x0,x1,x1,x0
	lsr	x0,x0,9
	bl	sin8
	lsr	x0,x0,1
	add	x0,x0,44
	b	.out
.t5:	mov	x1,24				; circuit
	udiv	x21,x19,x1
	msub	x22,x21,x1,x19
	udiv	x23,x20,x1
	msub	x24,x23,x1,x20
	cmp	x22,3
	b.hs	.c1
	cmp	x24,3
	b.hs	.c1
	mov	x0,170
	b	.out
.c1:	mov	x0,92
	cmp	x22,1
	b.eq	.out
	cmp	x24,1
	b.eq	.out
	mov	x1,53
	mul	x0,x23,x1
	add	x0,x0,x21
	and	x0,x0,4095
	adr	x1,g_noise
	ldrb	w0,[x1,x0]
	lsr	x0,x0,3
	add	x0,x0,26
.out:	pop_all
	epilogue

; x0 = x, x1 = y, x2 = log2 du pas -> x0 = bruit lisse 0..255 (touche x1-x9)
noise2:
	mov	x3,1
	lsl	x3,x3,x2
	sub	x3,x3,1
	and	x4,x0,x3			; fractions
	and	x5,x1,x3
	lsr	x0,x0,x2
	lsr	x1,x1,x2
	and	x0,x0,63
	and	x1,x1,63
	adr	x6,g_noise
	add	x7,x0,x1,lsl 6
	ldrb	w8,[x6,x7]
	add	x7,x0,1
	and	x7,x7,63
	add	x7,x7,x1,lsl 6
	ldrb	w9,[x6,x7]
	sub	x7,x9,x8
	mul	x7,x7,x4
	asr	x7,x7,x2
	add	x10,x8,x7			; haut
	add	x1,x1,1
	and	x1,x1,63
	add	x7,x0,x1,lsl 6
	ldrb	w8,[x6,x7]
	add	x7,x0,1
	and	x7,x7,63
	add	x7,x7,x1,lsl 6
	ldrb	w9,[x6,x7]
	sub	x7,x9,x8
	mul	x7,x7,x4
	asr	x7,x7,x2
	add	x8,x8,x7			; bas
	sub	x7,x8,x10
	mul	x7,x7,x5
	asr	x7,x7,x2
	add	x0,x10,x7
	ret

; x0 = niveau (entree de la table) : le decor, puis les murs
gen_bg:
	prologue
	push_all
	mov	x19,x0
	ldrb	w25,[x19]			; style
	ldr	w26,[x19,4]			; teinte
	ldr	x22,[x28,g_bg]
	mov	x20,0
.y:	mov	x21,0
.x:	mov	x0,x21
	mov	x1,x20
	mov	x2,x25
	bl	texture
	mov	w1,190				; plus sombre vers le bas
	mov	x3,GF_H
	sub	x3,x3,x20
	lsr	x3,x3,3
	add	w1,w1,w3
	mul	w1,w1,w0
	lsr	w1,w1,8
	add	w1,w1,24
	mov	w0,w26
	bl	shade
	str	w0,[x22],4
	add	x21,x21,1
	cmp	x21,GF_W
	b.lo	.x
	add	x20,x20,1
	cmp	x20,GF_H
	b.lo	.y
	; les murs : gauche et droit, puis le haut
	ldr	x22,[x28,g_bg]
	mov	x20,0
.wy:	mov	x21,0
.wx:	adr	x0,wall_prof
	ldrh	w1,[x0,x21,lsl 1]
	and	x2,x20,63			; un joint de temps en temps
	cmp	x2,1
	b.hi	.wc
	lsr	w1,w1,1
.wc:	movl	w0,0x00687484
	bl	shade
	orr	w0,w0,0xFF000000
	str	w0,[x22,x21,lsl 2]
	mov	x2,GF_W-1
	sub	x2,x2,x21
	str	w0,[x22,x2,lsl 2]
	add	x21,x21,1
	cmp	x21,FIELD_L
	b.lo	.wx
	add	x22,x22,GF_PITCH
	add	x20,x20,1
	cmp	x20,GF_H
	b.lo	.wy
	ldr	x22,[x28,g_bg]
	movl	x0,GF_PITCH*HUD_H
	add	x22,x22,x0
	mov	x20,0
.ty:	adr	x0,wall_prof
	ldrh	w1,[x0,x20,lsl 1]
	movl	w0,0x00687484
	bl	shade
	orr	w0,w0,0xFF000000
	mov	x21,FIELD_L
.tx:	str	w0,[x22,x21,lsl 2]
	add	x21,x21,1
	cmp	x21,FIELD_R
	b.lo	.tx
	add	x22,x22,GF_PITCH
	add	x20,x20,1
	cmp	x20,FIELD_T-HUD_H
	b.lo	.ty
	pop_all
	epilogue

; ---------------------------------------------------------------------------
;  La couche fixe
; ---------------------------------------------------------------------------

; les briques et leurs ombres, sur le decor
draw_field:
	prologue
	push_all
	ldv	x0,gf_layer
	movl	x1,BRICK_T*GF_PITCH
	add	x0,x0,x1
	ldr	x1,[x28,g_bg]
	movl	x2,BRICK_T*GF_PITCH
	add	x1,x1,x2
	movl	x2,(ROWS*BH+SHADOW+3)*GF_PITCH
	bl	gf_copy
	add	x19,x28,g_cells			; les ombres d'abord
	mov	x20,0				; ligne
	ldv	x21,gf_layer
	mov	x22,GF_PITCH
.sr:	mov	x23,0
.sc:	ldrb	w0,[x19],1
	cbz	w0,.sn
	mov	x0,BH
	mul	x0,x20,x0
	add	x0,x0,BRICK_T+SHADOW
	mul	x0,x0,x22
	add	x0,x0,x21
	mov	x1,BW
	mul	x1,x23,x1
	add	x1,x1,FIELD_L+SHADOW
	add	x0,x0,x1,lsl 2
	mov	x1,BSH
.sy:	mov	x2,BSW
	mov	x3,x0
.sx:	ldr	w4,[x3]
	lsr	w5,w4,24
	cbnz	w5,.sk
	lsr	w4,w4,1
	and	w4,w4,0x7F7F7F7F
	orr	w4,w4,0x80000000
	str	w4,[x3]
.sk:	add	x3,x3,4
	subs	x2,x2,1
	b.ne	.sx
	add	x0,x0,x22
	subs	x1,x1,1
	b.ne	.sy
.sn:	add	x23,x23,1
	cmp	x23,COLS
	b.lo	.sc
	add	x20,x20,1
	cmp	x20,ROWS
	b.lo	.sr
	add	x19,x28,g_cells			; puis les briques
	mov	x20,0
.br:	mov	x23,0
.bc:	ldrb	w2,[x19],1
	cbz	w2,.bn
	sub	x2,x2,1
	add	x0,x28,g_sb
	ldr	x2,[x0,x2,lsl 3]
	mov	x0,BW
	mul	x0,x23,x0
	add	x0,x0,FIELD_L
	mov	x1,BH
	mul	x1,x20,x1
	add	x1,x1,BRICK_T
	bl	gf_sprite
.bn:	add	x23,x23,1
	cmp	x23,COLS
	b.lo	.bc
	add	x20,x20,1
	cmp	x20,ROWS
	b.lo	.br
	pop_all
	epilogue

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

; x0 = etiquette, x1 = x, x2 = valeur -> x0 = x de fin
hud_item:
	prologue
	push_all
	sub	sp,sp,32
	bl	tr0
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x2,19
	movl	w3,0x00A8B4C4
	mov	x4,13
	adr	x5,ui_font
	bl	gf_text
	mov	x0,x19
	mov	x1,13
	adr	x2,ui_font
	bl	ui_text_width
	add	x20,x20,x0
	add	x20,x20,6
	mov	x0,x21
	mov	x1,sp
	bl	fmt_dec
	strb	wzr,[x0]
	mov	x0,sp
	mov	x1,x20
	mov	x2,19
	movl	w3,0x00FFFFFF
	mov	x4,14
	adr	x5,ui_font_b
	bl	gf_text
	mov	x0,sp
	mov	x1,14
	adr	x2,ui_font_b
	bl	ui_text_width
	add	x0,x0,x20
	add	sp,sp,32
	pop_all
	epilogue

draw_hud:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0				; (degrade du bandeau)
.g:	mov	x0,0
	mov	x1,x19
	mov	x2,GF_W
	mov	x3,1
	movl	w4,0xFF343840
	movl	w5,0x00010101
	mul	w5,w5,w19
	sub	w4,w4,w5
	bl	gf_rect
	add	x19,x19,1
	cmp	x19,HUD_H
	b.lo	.g
	mov	x0,0
	mov	x1,HUD_H-1
	mov	x2,GF_W
	mov	x3,1
	ldv	x4,gf_tok
	ldr	w4,[x4,TJ_ACCENT]
	orr	w4,w4,0xFF000000
	bl	gf_rect
	adr	x0,s_score
	mov	x1,24
	ldr	x2,[x28,g_score]
	bl	hud_item
	adr	x0,s_best
	mov	x1,140
	ldr	x2,[x28,g_best]
	bl	hud_item
	adr	x0,s_level
	mov	x1,264
	ldr	x2,[x28,g_level]
	add	x2,x2,1
	bl	hud_item
	mov	x19,0				; les vies
.l:	ldr	x0,[x28,g_lives]
	cmp	x19,x0
	b.hs	.lb
	cmp	x19,5
	b.hs	.lb
	mov	x0,12
	mul	x0,x0,x19
	add	x0,x0,356
	mov	x1,9
	ldr	x2,[x28,g_smini]
	bl	gf_sprite
	add	x19,x19,1
	b	.l
.lb:	ldr	x0,[x28,g_label_t]		; le dernier bonus
	cbz	x0,.out
	ldr	x0,[x28,g_label]
	bl	ui_tr
	mov	x1,422
	mov	x2,19
	ldv	x3,gf_tok
	ldr	w3,[x3,TJ_ACCENT_CLAIR]
	mov	x4,12
	adr	x5,ui_font_b
	bl	gf_text
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = y, x1 = hauteur : un panneau sombre pour un message
panel:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,104				; l'ombre, puis le verre (gameart)
	add	x1,x19,7
	mov	x2,440
	mov	x3,x20
	mov	x4,18
	mov	w5,0
	mov	w6,0
	mov	x7,110
	mov	w8,0
	bl	ga_panel
	mov	x0,100
	mov	x1,x19
	mov	x2,440
	mov	x3,x20
	mov	x4,18
	movl	w5,0x002A3452
	movl	w6,0x00121A2E
	mov	x7,230
	movl	w8,0x00687EB0
	bl	ga_panel
	mov	x0,100				; premier plan : ni ombre ni lumiere
	mov	x1,x19
	mov	x2,440
	mov	x3,x20
	bl	ga_mark
	ldp	x19,x20,[sp],16
	epilogue

; x0 = titre, x1 = deuxieme ligne : un message au milieu du terrain
message:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x1
	bl	ui_tr
	mov	x19,x0
	mov	x0,x20
	bl	ui_tr
	mov	x20,x0
	mov	x0,190
	mov	x1,90
	bl	panel
	mov	x0,x19
	mov	x1,GF_W/2
	mov	x2,232
	movl	w3,0x00FFFFFF
	mov	x4,26
	adr	x5,ui_font_b
	bl	gf_text_c
	cbz	x20,.out
	mov	x0,x20
	mov	x1,GF_W/2
	mov	x2,260
	movl	w3,0x00C8D2DE
	mov	x4,14
	adr	x5,ui_font
	bl	gf_text_c
.out:	ldp	x19,x20,[sp],16
	epilogue

title_screen:
	prologue
	sub	sp,sp,48
	mov	x0,120
	mov	x1,210
	bl	panel
	ldr	x0,[x28,g_slogo]
	mov	x1,GF_W/2
	mov	x2,200-65
	bl	ga_show
	adr	x0,s_sub
	bl	tr0
	mov	x1,GF_W/2
	mov	x2,232
	movl	w3,0x00C8D2DE
	mov	x4,15
	adr	x5,ui_font
	bl	gf_text_c
	adr	x0,s_start
	bl	tr0
	mov	x1,GF_W/2
	mov	x2,272
	movl	w3,0x00FFFFFF
	mov	x4,18
	adr	x5,ui_font_b
	bl	gf_text_c
	mov	x0,sp
	adr	x1,s_best1
	bl	tr_cat
	mov	x1,x0
	ldr	x0,[x28,g_best]
	bl	fmt_dec
	strb	wzr,[x0]
	mov	x0,sp
	mov	x1,GF_W/2
	mov	x2,304
	movl	w3,0x00A8B4C4
	mov	x4,14
	adr	x5,ui_font
	bl	gf_text_c
	mov	x0,100
	mov	x1,120
	mov	x2,440
	mov	x3,210
	bl	ga_mark
	add	sp,sp,48
	epilogue

; toute la couche
build_layer:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,64
	str	xzr,[x28,g_ldirty]
	str	xzr,[x28,g_fdirty]
	str	xzr,[x28,g_hdirty]
	ldv	x0,gf_layer
	stv	x0,gf_dst
	ldr	x1,[x28,g_bg]
	movl	x2,GF_PITCH*GF_H
	bl	gf_copy
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	bl	draw_field
	bl	draw_hud
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.ne	.r
	bl	title_screen
	b	.end
.r:	cmp	x0,ST_READY
	b.ne	.p
	bl	level_ptr
	add	x0,x0,8
	adr	x1,s_launch
	bl	message
	b	.end
.p:	cmp	x0,ST_OVER			; (la pause : gamepause)
	b.ne	.c
	adr	x0,s_over
	adr	x1,s_again
	bl	message
	b	.end
.c:	cmp	x0,ST_CLEAR
	b.ne	.end
	mov	x0,sp
	adr	x1,s_level1
	bl	tr_cat
	mov	x1,x0
	ldr	x0,[x28,g_level]
	add	x0,x0,1
	bl	fmt_dec
	adr	x1,s_clear
	bl	tr_cat
	mov	x0,sp
	adr	x1,s_bonus
	bl	message
.end:	bl	ui_canvas_end
	adr	x0,gf_frame
	stv	x0,gf_dst
	add	sp,sp,64
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Parties et niveaux
; ---------------------------------------------------------------------------

; x0 = etat
set_state:
	str	x0,[x28,g_state]
	str	xzr,[x28,g_timer]
	mov	x1,1
	str	x1,[x28,g_ldirty]
	cmp	x0,ST_PAUSE
	cset	x0,eq
	stv	x0,gf_still
	stv	x1,gf_redraw
	ret

; -> x0 = entree du niveau courant
level_ptr:
	ldr	x0,[x28,g_level]
	mov	x1,NLEVELS
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	mov	x1,LV_SIZE
	mul	x0,x0,x1
	adr	x1,levels
	add	x0,x0,x1
	ret

; le niveau courant : les briques, le decor, une balle sur la raquette
load_level:
	prologue
	push_all
	bl	level_ptr
	mov	x19,x0
	bl	gen_bg
	add	x0,x19,LV_HEAD
	add	x1,x28,g_cells
	add	x2,x28,g_fuse
	mov	x3,NCELL
	mov	x4,0				; a casser
.c:	ldrb	w5,[x0],1
	mov	w6,0
	cmp	w5,'.'
	b.eq	.put
	mov	w6,BK_STEEL+1
	cmp	w5,'#'
	b.eq	.put
	mov	w6,BK_BOMB+1
	cmp	w5,'*'
	b.eq	.cnt
	mov	w6,BK_SILVER+1
	cmp	w5,'S'
	b.eq	.cnt
	mov	w6,BK_GOLD+1
	cmp	w5,'G'
	b.eq	.cnt
	sub	w6,w5,'a'-1
.cnt:	add	x4,x4,1
.put:	strb	w6,[x1],1
	strb	wzr,[x2],1
	subs	x3,x3,1
	b.ne	.c
	str	x4,[x28,g_left]
	bl	reset_round
	pop_all
	epilogue

; plus rien en jeu, une balle sur la raquette
reset_round:
	prologue
	add	x0,x28,g_ball
	mov	x1,NB
.b:	str	xzr,[x0,B_ON]
	add	x0,x0,B_SIZE
	subs	x1,x1,1
	b.ne	.b
	add	x0,x28,g_cap
	mov	x1,NC
.k:	str	xzr,[x0]
	add	x0,x0,32
	subs	x1,x1,1
	b.ne	.k
	add	x0,x28,g_las
	mov	x1,NL
.l:	str	xzr,[x0]
	add	x0,x0,16
	subs	x1,x1,1
	b.ne	.l
	str	xzr,[x28,g_t_slow]
	str	xzr,[x28,g_t_fast]
	str	xzr,[x28,g_t_thru]
	str	xzr,[x28,g_t_glue]
	str	xzr,[x28,g_t_laser]
	str	xzr,[x28,g_combo]
	str	xzr,[x28,g_loop]
	mov	x0,1
	bl	set_psize
	ldr	x0,[x28,g_pw]			; au milieu
	mov	x1,GF_W
	sub	x1,x1,x0
	lsr	x1,x1,1
	str	x1,[x28,g_px]
	add	x0,x28,g_ball
	mov	x1,1
	str	x1,[x0,B_ON]
	str	x1,[x0,B_STUCK]
	ldr	x2,[x28,g_pw]
	lsr	x2,x2,1
	str	x2,[x0,B_OFF]
	str	xzr,[x0,B_T]
	mov	x2,0				; (vers le haut, un peu de biais)
	str	x2,[x0,B_DX]
	movl	x2,-16384
	str	x2,[x0,B_DY]
	epilogue

; x0 = taille 0..3 de la raquette (elle garde son milieu)
set_psize:
	str	x0,[x28,g_psize]
	adr	x1,pad_w
	ldrb	w1,[x1,x0]
	ldr	x2,[x28,g_pw]
	ldr	x3,[x28,g_px]
	sub	x4,x2,x1
	add	x3,x3,x4,asr 1
	str	x3,[x28,g_px]
	str	x1,[x28,g_pw]
	ret

goto_title:
	prologue
	str	xzr,[x28,g_level]
	str	xzr,[x28,g_score]
	mov	x0,3
	str	x0,[x28,g_lives]
	bl	load_level
	add	x0,x28,g_ball			; (pas de balle au titre)
	str	xzr,[x0,B_ON]
	mov	x0,ST_TITLE
	bl	set_state
	epilogue

new_game:
	prologue
	str	xzr,[x28,g_level]
	str	xzr,[x28,g_score]
	mov	x0,3
	str	x0,[x28,g_lives]
	bl	load_level
	mov	x0,ST_READY
	bl	set_state
	epilogue

keep_best:
	prologue
	ldr	x2,[x28,g_score]
	ldr	x0,[x28,g_best]
	cmp	x2,x0
	b.ls	.out
	str	x2,[x28,g_best]
	adr	x0,p_dir
	adr	x1,p_best
	bl	gf_save_best
.out:	epilogue

; x0 = points
add_score:
	ldr	x1,[x28,g_score]
	add	x1,x1,x0
	str	x1,[x28,g_score]
	mov	x1,1
	str	x1,[x28,g_hdirty]
	ret

; les balles collees partent
launch:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x28,g_ball
	mov	x20,NB
.b:	ldr	x0,[x19,B_ON]
	cbz	x0,.n
	ldr	x0,[x19,B_STUCK]
	cbz	x0,.n
	str	xzr,[x19,B_STUCK]
	mov	x0,5
	bl	gf_below
	add	x0,x0,5
	mov	x1,x19
	bl	set_dir
.n:	add	x19,x19,B_SIZE
	subs	x20,x20,1
	b.ne	.b
	ldp	x19,x20,[sp],16
	epilogue

; x0 = rang 0..14, x1 = balle : direction vers le haut de la table
set_dir:
	adr	x2,dirs
	add	x2,x2,x0,lsl 2
	ldrsh	x3,[x2]
	ldrsh	x4,[x2,2]
	neg	x4,x4
	str	x3,[x1,B_DX]
	str	x4,[x1,B_DY]
	ret

; ---------------------------------------------------------------------------
;  Effets
; ---------------------------------------------------------------------------

; x0 = x, x1 = y (pixels), x2 = genre, w3 = couleur
spawn_part:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	ldr	x0,[x28,g_pnext]
	add	x1,x0,1
	cmp	x1,NP
	csel	x1,xzr,x1,hs
	str	x1,[x28,g_pnext]
	lsl	x0,x0,5
	adr	x1,g_parts
	add	x4,x1,x0
	lsl	w19,w19,16
	lsl	w20,w20,16
	stp	w19,w20,[x4,P_X]
	str	w22,[x4,P_COL]
	str	w21,[x4,P_KIND]
	movl	x0,0x60000			; vitesses au hasard
	bl	gf_below
	movl	x1,0x30000
	sub	w0,w0,w1
	str	w0,[x4,P_VX]
	movl	x0,0x60000
	bl	gf_below
	movl	x1,0x40000
	sub	w0,w0,w1
	str	w0,[x4,P_VY]
	mov	x0,40
	bl	gf_below
	add	x0,x0,24
	str	w0,[x4,P_LIFE]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, x2 = nombre, x3 = genre, w4 = couleur
burst:
	prologue
	push_all
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	w23,w4
.l:	mov	x0,BW
	bl	gf_below
	sub	x0,x0,BW/2
	add	x0,x0,x19
	mov	x24,x0
	mov	x0,BH
	bl	gf_below
	sub	x1,x0,BH/2
	add	x1,x1,x20
	mov	x0,x24
	mov	x2,x22
	mov	w3,w23
	bl	spawn_part
	subs	x21,x21,1
	b.ne	.l
	pop_all
	epilogue

; x0 = x, x1 = y : un eclair
add_flash:
	add	x2,x28,g_flash
	mov	x3,NF
.f:	ldr	x4,[x2]
	cbz	x4,.got
	add	x2,x2,24
	subs	x3,x3,1
	b.ne	.f
	ret
.got:	mov	x4,12
	str	x4,[x2]
	stp	x0,x1,[x2,8]
	ret

; x0 = case : son centre -> x0 = x, x1 = y
cell_xy:
	mov	x1,COLS
	udiv	x2,x0,x1
	msub	x3,x2,x1,x0
	mov	x0,BW
	mul	x0,x3,x0
	add	x0,x0,FIELD_L+BW/2
	mov	x1,BH
	mul	x1,x2,x1
	add	x1,x1,BRICK_T+BH/2
	ret

; x0 = x, x1 = y : peut-etre une capsule
maybe_capsule:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,5
	bl	gf_below
	cbnz	x0,.out
	add	x4,x28,g_cap
	mov	x5,NC
.f:	ldr	x0,[x4]
	cbz	x0,.got
	add	x4,x4,32
	subs	x5,x5,1
	b.ne	.f
	b	.out
.got:	mov	x0,1
	str	x0,[x4]
	sub	x0,x19,CAP_W/2
	lsl	x0,x0,16
	lsl	x1,x20,16
	stp	x0,x1,[x4,8]
	mov	x0,CAP_ODDS
	mov	x19,x4
	bl	gf_below
	adr	x1,cap_odds
	ldrb	w0,[x1,x0]
	str	x0,[x19,24]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = case : la brique disparait
destroy_cell:
	prologue
	push_all
	mov	x19,x0
	add	x1,x28,g_cells
	ldrb	w20,[x1,x19]
	cbz	w20,.out
	sub	x20,x20,1			; genre
	strb	wzr,[x1,x19]
	add	x1,x28,g_fuse
	strb	wzr,[x1,x19]
	mov	x0,1
	str	x0,[x28,g_fdirty]
	ldr	x0,[x28,g_left]
	sub	x0,x0,1
	str	x0,[x28,g_left]
	str	xzr,[x28,g_loop]
	adr	x1,brick_pts
	ldrb	w0,[x1,x20]
	ldr	x1,[x28,g_combo]
	add	x0,x0,x1,lsl 1
	add	x1,x1,1
	cmp	x1,20
	mov	x2,20
	csel	x1,x2,x1,hi
	str	x1,[x28,g_combo]
	bl	add_score
	mov	x0,x19
	bl	cell_xy
	mov	x21,x0
	mov	x22,x1
	adr	x2,brick_col
	ldr	w4,[x2,x20,lsl 2]
	mov	x2,10
	mov	x3,1
	bl	burst
	mov	x0,x21
	mov	x1,x22
	mov	x2,6
	mov	x3,0
	movl	w4,0x00FFFFFF
	bl	burst
	cmp	x20,BK_BOMB
	b.eq	.bomb
	mov	x0,COLS				; le son monte avec la ligne
	udiv	x0,x19,x0
	mov	x1,ROWS
	sub	x0,x1,x0
	mov	x1,28
	mul	x0,x0,x1
	add	x0,x0,420
	mov	x1,40
	mov	x2,AKW_SQUARE
	mov	x3,56
	bl	gs_note
	mov	x0,x21
	mov	x1,x22
	bl	maybe_capsule
	b	.out
.bomb:	mov	x0,x21
	mov	x1,x22
	bl	add_flash
	mov	x0,x21
	mov	x1,x22
	mov	x2,18
	mov	x3,0
	movl	w4,0x00FFA030
	bl	burst
	mov	x0,120
	mov	x1,300
	mov	x2,AKW_NOISE
	mov	x3,170
	bl	gs_note
	mov	x0,COLS				; les voisines : la meche
	udiv	x21,x19,x0
	msub	x22,x21,x0,x19			; x21 ligne, x22 colonne
	mov	x23,-1
.dy:	mov	x24,-1
.dx:	add	x0,x21,x23
	add	x1,x22,x24
	cmp	x0,ROWS
	b.hs	.nn
	cmp	x1,COLS
	b.hs	.nn
	mov	x2,COLS
	madd	x0,x0,x2,x1
	add	x1,x28,g_cells
	ldrb	w2,[x1,x0]
	cbz	w2,.nn
	cmp	w2,BK_STEEL+1
	b.eq	.nn
	add	x1,x28,g_fuse
	ldrb	w2,[x1,x0]
	cbnz	w2,.nn
	mov	w2,7
	strb	w2,[x1,x0]
.nn:	add	x24,x24,1
	cmp	x24,1
	b.le	.dx
	add	x23,x23,1
	cmp	x23,1
	b.le	.dy
.out:	pop_all
	epilogue

; x0 = case, x1 = 1 : la balle traverse -> x0 = 1 si le projectile rebondit
hit_brick:
	prologue
	add	x2,x28,g_cells
	ldrb	w3,[x2,x0]
	sub	w3,w3,1				; genre
	cbz	x1,.std
	cmp	w3,BK_STEEL
	b.eq	.none
	bl	destroy_cell
.none:	mov	x0,0
	b	.out
.std:	cmp	w3,BK_STEEL
	b.ne	.h1
	mov	x0,1800
	mov	x1,16
	mov	x2,AKW_TRIANGLE
	mov	x3,60
	bl	gs_note
	b	.one
.h1:	cmp	w3,BK_SILVER
	b.eq	.dmg
	cmp	w3,BK_GOLD
	b.eq	.dmg
	cmp	w3,BK_GOLD+1
	b.eq	.dmg
	bl	destroy_cell
	b	.one
.dmg:	add	w3,w3,2				; une fissure de plus
	strb	w3,[x2,x0]
	mov	x0,1
	str	x0,[x28,g_fdirty]
	str	xzr,[x28,g_loop]
	mov	x0,1200
	mov	x1,24
	mov	x2,AKW_TRIANGLE
	mov	x3,70
	bl	gs_note
.one:	mov	x0,1
.out:	epilogue

; x0 = x, x1 = y (pixels), x2 = 1 : traverse -> x0 = 1 rebond
try_hit:
	prologue
	sub	x0,x0,FIELD_L
	sub	x1,x1,BRICK_T
	mov	x3,BW
	cmp	x0,COLS*BW
	b.hs	.no
	mov	x4,ROWS*BH
	cmp	x1,x4
	b.hs	.no
	udiv	x0,x0,x3
	mov	x3,BH
	udiv	x1,x1,x3
	mov	x3,COLS
	madd	x0,x1,x3,x0
	add	x3,x28,g_cells
	ldrb	w3,[x3,x0]
	cbz	w3,.no
	mov	x1,x2
	bl	hit_brick
	b	.out
.no:	mov	x0,0
.out:	epilogue

; x19 = balle, x0 = 1 : rebond sur une paroi (son)
wall_sound:
	mov	x0,520
	mov	x1,16
	mov	x2,AKW_TRIANGLE
	mov	x3,40
	b	gs_note

; x19 = balle : contre la raquette
paddle_hit:
	prologue
	str	x20,[sp,-16]!
	ldr	x0,[x19,B_X]
	asr	x0,x0,16
	ldr	x1,[x28,g_px]
	sub	x20,x0,x1			; decalage
	mov	x0,RKT_Y-BALL_R
	lsl	x0,x0,16
	str	x0,[x19,B_Y]
	mov	x0,15
	mul	x0,x20,x0
	ldr	x1,[x28,g_pw]
	sdiv	x0,x0,x1
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x0,14
	mov	x1,14
	csel	x0,x1,x0,gt
	mov	x1,x19
	bl	set_dir
	str	xzr,[x28,g_combo]
	str	xzr,[x28,g_loop]
	ldr	x0,[x28,g_t_glue]
	cbz	x0,.snd
	mov	x0,1
	str	x0,[x19,B_STUCK]
	str	x20,[x19,B_OFF]
	str	xzr,[x19,B_T]
.snd:	mov	x0,300
	mov	x1,30
	mov	x2,AKW_SQUARE
	mov	x3,70
	bl	gs_note
	ldr	x0,[x19,B_X]
	asr	x0,x0,16
	mov	x1,RKT_Y
	mov	x2,4
	mov	x3,0
	movl	w4,0x00C0D8FF
	bl	burst
	ldr	x20,[sp],16
	epilogue

; x19 = balle, x20 = vitesse (8.8) : un demi-pas
ball_sub:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldr	x24,[x28,g_t_thru]
	cmp	x24,0
	cset	x24,ne				; traversante ?
	; --- en x
	ldr	x21,[x19,B_DX]
	mul	x0,x21,x20
	asr	x0,x0,7
	ldr	x22,[x19,B_X]
	add	x22,x22,x0			; nouveau x
	asr	x23,x22,16
	mov	x0,BALL_R
	cmp	x21,0
	cneg	x0,x0,lt
	add	x23,x23,x0			; bord qui avance
	cmp	x23,FIELD_L
	b.ge	.xr
	neg	x0,x21
	cmp	x21,0
	csel	x21,x0,x21,lt
	mov	x22,FIELD_L+BALL_R
	lsl	x22,x22,16
	str	x21,[x19,B_DX]
	bl	wall_sound
	b	.xs
.xr:	cmp	x23,FIELD_R
	b.lt	.xb
	neg	x0,x21
	cmp	x21,0
	csel	x21,x0,x21,gt
	mov	x22,FIELD_R-BALL_R-1
	lsl	x22,x22,16
	str	x21,[x19,B_DX]
	bl	wall_sound
	b	.xs
.xb:	ldr	x0,[x19,B_Y]
	asr	x0,x0,16
	sub	x1,x0,3
	mov	x0,x23
	mov	x2,x24
	bl	try_hit
	cbnz	x0,.xh
	ldr	x0,[x19,B_Y]
	asr	x0,x0,16
	add	x1,x0,3
	mov	x0,x23
	mov	x2,x24
	bl	try_hit
	cbz	x0,.xs
.xh:	neg	x21,x21
	str	x21,[x19,B_DX]
	ldr	x22,[x19,B_X]
.xs:	str	x22,[x19,B_X]
	; --- en y
	ldr	x21,[x19,B_DY]
	mul	x0,x21,x20
	asr	x0,x0,7
	ldr	x22,[x19,B_Y]
	add	x22,x22,x0
	asr	x23,x22,16
	mov	x0,BALL_R
	cmp	x21,0
	cneg	x0,x0,lt
	add	x23,x23,x0
	cmp	x23,FIELD_T
	b.ge	.yb
	neg	x21,x21
	str	x21,[x19,B_DY]
	mov	x22,FIELD_T+BALL_R
	lsl	x22,x22,16
	bl	wall_sound
	b	.ys
.yb:	ldr	x0,[x19,B_X]
	asr	x0,x0,16
	sub	x0,x0,3
	mov	x1,x23
	mov	x2,x24
	bl	try_hit
	cbnz	x0,.yh
	ldr	x0,[x19,B_X]
	asr	x0,x0,16
	add	x0,x0,3
	mov	x1,x23
	mov	x2,x24
	bl	try_hit
	cbz	x0,.ys
.yh:	neg	x21,x21
	str	x21,[x19,B_DY]
	ldr	x22,[x19,B_Y]
.ys:	str	x22,[x19,B_Y]
	; --- la raquette, le bas
	ldr	x21,[x19,B_DY]
	cmp	x21,0
	b.le	.out
	asr	x23,x22,16
	add	x0,x23,BALL_R
	cmp	x0,RKT_Y
	b.lt	.out
	sub	x0,x23,BALL_R
	cmp	x0,RKT_Y+RKT_H
	b.ge	.low
	ldr	x0,[x19,B_X]
	asr	x0,x0,16
	ldr	x1,[x28,g_px]
	sub	x2,x1,BALL_R
	cmp	x0,x2
	b.lt	.low
	ldr	x2,[x28,g_pw]
	add	x2,x2,x1
	add	x2,x2,BALL_R
	cmp	x0,x2
	b.gt	.low
	bl	paddle_hit
	b	.out
.low:	cmp	x23,GF_H+8
	b.lt	.out
	str	xzr,[x19,B_ON]
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; -> x0 = vitesse des balles (pixels par image, 8.8)
ball_speed:
	ldr	x1,[x28,g_level]
	mov	x0,28
	mul	x0,x1,x0
	add	x0,x0,900
	mov	x1,1380
	cmp	x0,x1
	csel	x0,x1,x0,hi
	ldr	x1,[x28,g_t_slow]
	cbz	x1,.f
	add	x0,x0,x0,lsl 1
	lsr	x0,x0,2
.f:	ldr	x1,[x28,g_t_fast]
	cbz	x1,.out
	add	x0,x0,x0,lsr 1
.out:	ret

balls_step:
	prologue
	push_all
	bl	ball_speed
	mov	x20,x0
	add	x19,x28,g_ball
	mov	x21,NB
	mov	x22,0				; balles vivantes
.b:	ldr	x0,[x19,B_ON]
	cbz	x0,.n
	ldr	x0,[x19,B_STUCK]
	cbz	x0,.free
	ldr	x0,[x28,g_px]			; collee a la raquette
	ldr	x1,[x19,B_OFF]
	add	x0,x0,x1
	lsl	x0,x0,16
	str	x0,[x19,B_X]
	mov	x0,RKT_Y-BALL_R
	lsl	x0,x0,16
	str	x0,[x19,B_Y]
	ldr	x0,[x19,B_X]			; (la trainee partira d'ici)
	asr	x0,x0,16
	mov	x1,RKT_Y-BALL_R
	add	x2,x19,B_H
	mov	x3,HIST
.h:	stp	w0,w1,[x2],8
	subs	x3,x3,1
	b.ne	.h
	add	x22,x22,1
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.ne	.n
	ldr	x0,[x19,B_T]			; part toute seule au bout de 3 s
	add	x0,x0,1
	str	x0,[x19,B_T]
	cmp	x0,180
	b.lo	.n
	bl	launch
	b	.n
.free:	ldr	x0,[x19,B_HI]			; la trainee
	add	x0,x0,1
	and	x0,x0,HIST-1
	str	x0,[x19,B_HI]
	add	x1,x19,B_H
	ldr	x2,[x19,B_X]
	asr	x2,x2,16
	ldr	x3,[x19,B_Y]
	asr	x3,x3,16
	add	x0,x1,x0,lsl 3
	stp	w2,w3,[x0]
	bl	ball_sub
	ldr	x0,[x19,B_STUCK]
	cbnz	x0,.cnt
	ldr	x0,[x19,B_ON]
	cbz	x0,.n
	bl	ball_sub
	ldr	x0,[x19,B_ON]
	cbz	x0,.n
.cnt:	add	x22,x22,1
.n:	add	x19,x19,B_SIZE
	subs	x21,x21,1
	b.ne	.b
	cbnz	x22,.loop
	bl	life_lost
	b	.out
.loop:	ldr	x0,[x28,g_loop]			; la balle tourne en rond : un coup de pouce
	add	x0,x0,1
	str	x0,[x28,g_loop]
	cmp	x0,1200
	b.lo	.out
	str	xzr,[x28,g_loop]
	add	x19,x28,g_ball
	mov	x21,NB
.nudge:	ldr	x0,[x19,B_ON]
	cbz	x0,.nn
	mov	x0,15
	bl	gf_below
	mov	x1,x19
	bl	set_dir
.nn:	add	x19,x19,B_SIZE
	subs	x21,x21,1
	b.ne	.nudge
.out:	pop_all
	epilogue

life_lost:
	prologue
	ldr	x0,[x28,g_lives]
	sub	x0,x0,1
	str	x0,[x28,g_lives]
	mov	x0,1
	str	x0,[x28,g_hdirty]
	ldr	x0,[x28,g_px]
	ldr	x1,[x28,g_pw]
	add	x0,x0,x1,lsr 1
	mov	x1,RKT_Y
	mov	x2,30
	mov	x3,1
	movl	w4,0x00B4BCC8
	bl	burst
	adr	x0,gs_tab_lose
	bl	gs_seq
	mov	x0,ST_LOST
	bl	set_state
	epilogue

; x0 = genre : la capsule est prise
catch:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,cap_labels			; son nom
	mov	x2,x19
.lb:	cbz	x2,.got
.sk:	ldrb	w3,[x1],1
	cbnz	w3,.sk
	sub	x2,x2,1
	b	.lb
.got:	str	x1,[x28,g_label]
	mov	x0,180
	str	x0,[x28,g_label_t]
	mov	x0,50
	bl	add_score
	adr	x0,snd_catch
	bl	gs_seq
	cmp	x19,CP_WIDE
	b.ne	.c1
	ldr	x0,[x28,g_psize]
	add	x0,x0,1
	cmp	x0,3
	b.hi	.out
	bl	set_psize
	b	.out
.c1:	cmp	x19,CP_NARROW
	b.ne	.c2
	ldr	x0,[x28,g_psize]
	cbz	x0,.out
	sub	x0,x0,1
	bl	set_psize
	b	.out
.c2:	cmp	x19,CP_MULTI
	b.ne	.c3
	bl	multi_ball
	b	.out
.c3:	cmp	x19,CP_SLOW
	b.ne	.c4
	mov	x0,600
	str	x0,[x28,g_t_slow]
	str	xzr,[x28,g_t_fast]
	b	.out
.c4:	cmp	x19,CP_FAST
	b.ne	.c5
	mov	x0,480
	str	x0,[x28,g_t_fast]
	str	xzr,[x28,g_t_slow]
	b	.out
.c5:	cmp	x19,CP_THRU
	b.ne	.c6
	mov	x0,480
	str	x0,[x28,g_t_thru]
	b	.out
.c6:	cmp	x19,CP_GLUE
	b.ne	.c7
	mov	x0,900
	str	x0,[x28,g_t_glue]
	b	.out
.c7:	cmp	x19,CP_LASER
	b.ne	.c8
	mov	x0,900
	str	x0,[x28,g_t_laser]
	b	.out
.c8:	ldr	x0,[x28,g_lives]
	cmp	x0,9
	b.hs	.out
	add	x0,x0,1
	str	x0,[x28,g_lives]
.out:	mov	x0,1
	str	x0,[x28,g_hdirty]
	ldp	x19,x20,[sp],16
	epilogue

; deux balles de plus, au depart de la premiere
multi_ball:
	prologue
	push_all
	add	x19,x28,g_ball
	mov	x20,NB
.f:	ldr	x0,[x19,B_ON]
	cbnz	x0,.got
	add	x19,x19,B_SIZE
	subs	x20,x20,1
	b.ne	.f
	b	.out
.got:	mov	x21,2				; a creer
	add	x22,x28,g_ball
	mov	x20,NB
.n:	ldr	x0,[x22,B_ON]
	cbnz	x0,.nx
	mov	x0,x22				; toute la balle, sa trainee comprise
	mov	x1,x19
	mov	x2,B_SIZE
	bl	mem_move
	str	xzr,[x22,B_STUCK]
	mov	x0,11
	bl	gf_below
	add	x0,x0,2
	mov	x1,x22
	bl	set_dir
	subs	x21,x21,1
	b.eq	.out
.nx:	add	x22,x22,B_SIZE
	subs	x20,x20,1
	b.ne	.n
.out:	pop_all
	epilogue

capsules_step:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x28,g_cap
	mov	x20,NC
.c:	ldr	x0,[x19]
	cbz	x0,.n
	ldr	x1,[x19,16]
	movl	x0,0x18000			; 1,5 point par image
	add	x1,x1,x0
	str	x1,[x19,16]
	asr	x1,x1,16
	add	x2,x1,CAP_H
	cmp	x2,RKT_Y
	b.lt	.n
	cmp	x1,RKT_Y+RKT_H
	b.ge	.miss
	ldr	x0,[x19,8]
	asr	x0,x0,16
	ldr	x2,[x28,g_px]
	add	x3,x0,CAP_W
	cmp	x3,x2
	b.lt	.n
	ldr	x3,[x28,g_pw]
	add	x2,x2,x3
	cmp	x0,x2
	b.gt	.n
	str	xzr,[x19]
	ldr	x0,[x19,24]
	bl	catch
	b	.n
.miss:	cmp	x1,GF_H
	b.lt	.n
	str	xzr,[x19]
.n:	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.c
	ldp	x19,x20,[sp],16
	epilogue

; les tirs de laser
lasers_step:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x0,[x28,g_laser_cd]
	cbz	x0,.fire
	sub	x0,x0,1
	str	x0,[x28,g_laser_cd]
	b	.mv
.fire:	ldr	x0,[x28,g_t_laser]
	cbz	x0,.mv
	ldr	x0,[x28,g_btn]
	adr	x1,gf_held
	ldrb	w1,[x1,KEY_SPACE]
	orr	x0,x0,x1
	ldv	x1,gp_now			; (A, B, X ou Y tenus)
	and	x1,x1,PAD_A or PAD_B or PAD_X or PAD_Y
	orr	x0,x0,x1
	cbz	x0,.mv
	mov	x0,16
	str	x0,[x28,g_laser_cd]
	ldr	x0,[x28,g_px]
	add	x0,x0,6
	bl	.add
	ldr	x0,[x28,g_px]
	ldr	x1,[x28,g_pw]
	add	x0,x0,x1
	sub	x0,x0,10
	bl	.add
	mov	x0,1400
	mov	x1,40
	mov	x2,AKW_SAW
	mov	x3,50
	bl	gs_note
.mv:	add	x19,x28,g_las
	mov	x20,NL
.l:	ldr	w0,[x19]
	cbz	w0,.n
	ldr	w1,[x19,8]
	sub	w1,w1,8
	str	w1,[x19,8]
	cmp	w1,FIELD_T
	b.lt	.dead
	ldr	w0,[x19,4]
	add	x0,x0,2
	mov	x2,0
	bl	try_hit
	cbz	x0,.n
.dead:	str	wzr,[x19]
.n:	add	x19,x19,16
	subs	x20,x20,1
	b.ne	.l
	ldp	x19,x20,[sp],16
	epilogue
.add:	add	x2,x28,g_las			; x0 = x : un tir
	mov	x3,NL
.a:	ldr	w4,[x2]
	cbz	w4,.ag
	add	x2,x2,16
	subs	x3,x3,1
	b.ne	.a
	ret
.ag:	mov	w4,1
	str	w4,[x2]
	str	w0,[x2,4]
	mov	w4,RKT_Y-14
	str	w4,[x2,8]
	ret

; les meches des explosions
fuses_step:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
.f:	add	x0,x28,g_fuse
	ldrb	w1,[x0,x19]
	cbz	w1,.n
	subs	w1,w1,1
	strb	w1,[x0,x19]
	b.ne	.n
	mov	x0,x19
	bl	destroy_cell
.n:	add	x19,x19,1
	cmp	x19,NCELL
	b.lo	.f
	ldp	x19,x20,[sp],16
	epilogue

; x0 = adresse d'un compteur : un de moins
dec_t:
	ldr	x1,[x0]
	cbz	x1,.r
	sub	x1,x1,1
	str	x1,[x0]
.r:	ret

paddle_step:
	mov	x0,0
	adr	x1,gf_held
	ldrb	w2,[x1,KEY_LEFT]
	ldrb	w3,[x1,KEY_RIGHT]
	adr	x4,gp_now			; (ou la croix)
	ldr	x4,[x4]
	ubfx	x5,x4,2,1
	orr	w2,w2,w5
	ubfx	x5,x4,3,1
	orr	w3,w3,w5
	sub	w2,w3,w2			; -1, 0, 1
	sxtw	x2,w2
	ldr	x3,[x28,g_keyspd]
	cbnz	x2,.key
	mov	x3,4
	str	x3,[x28,g_keyspd]
	ldr	x0,[x28,g_mmoved]
	cbz	x0,.clip
	str	xzr,[x28,g_mmoved]
	ldr	x0,[x28,g_mx]
	ldr	x1,[x28,g_pw]
	sub	x0,x0,x1,lsr 1
	str	x0,[x28,g_px]
	b	.clip
.key:	add	x3,x3,1				; ca accelere
	cmp	x3,11
	mov	x4,11
	csel	x3,x4,x3,hi
	str	x3,[x28,g_keyspd]
	mul	x2,x2,x3
	ldr	x0,[x28,g_px]
	add	x0,x0,x2
	str	x0,[x28,g_px]
.clip:	ldr	x0,[x28,g_px]
	ldr	x1,[x28,g_pw]
	mov	x2,FIELD_R
	sub	x2,x2,x1
	cmp	x0,FIELD_L
	mov	x3,FIELD_L
	csel	x0,x3,x0,lt
	cmp	x0,x2
	csel	x0,x2,x0,gt
	str	x0,[x28,g_px]
	ret

parts_step:
	adr	x0,g_parts
	mov	x1,NP
.p:	ldr	w2,[x0,P_LIFE]
	cbz	w2,.n
	sub	w2,w2,1
	str	w2,[x0,P_LIFE]
	ldp	w2,w3,[x0,P_X]
	ldp	w4,w5,[x0,P_VX]
	add	w2,w2,w4
	add	w3,w3,w5
	stp	w2,w3,[x0,P_X]
	ldr	w2,[x0,P_KIND]
	mov	w3,0x1800			; pesanteur
	cbz	w2,.g
	mov	w3,0x3800
.g:	add	w5,w5,w3
	str	w5,[x0,P_VY]
.n:	add	x0,x0,P_SIZE
	subs	x1,x1,1
	b.ne	.p
	ret

motes_step:
	add	x0,x28,g_mote
	mov	x1,NM
.m:	ldp	w2,w3,[x0]
	ldr	w4,[x0,8]
	sub	w3,w3,w4
	cmp	w3,0x200000
	b.hs	.s
	movl	w3,0x1D00000
.s:	str	w3,[x0,4]
	add	x0,x0,16
	subs	x1,x1,1
	b.ne	.m
	ret

; une image de jeu
step:
	prologue
	ldr	x0,[x28,g_tick]
	add	x0,x0,1
	str	x0,[x28,g_tick]
	bl	parts_step
	bl	motes_step
	add	x0,x28,g_flash
	mov	x1,NF
.fl:	ldr	x2,[x0]
	cbz	x2,.fn
	sub	x2,x2,1
	str	x2,[x0]
.fn:	add	x0,x0,24
	subs	x1,x1,1
	b.ne	.fl
	ldr	x0,[x28,g_label_t]		; le nom du bonus s'efface
	cbz	x0,.st
	sub	x0,x0,1
	str	x0,[x28,g_label_t]
	cbnz	x0,.st
	mov	x0,1
	str	x0,[x28,g_hdirty]
.st:	ldr	x0,[x28,g_state]
	cmp	x0,ST_READY
	b.ne	.pl
	bl	paddle_step
	bl	balls_step
	b	.out
.pl:	cmp	x0,ST_PLAY
	b.ne	.lost
	bl	paddle_step
	add	x0,x28,g_t_slow
	bl	dec_t
	add	x0,x28,g_t_fast
	bl	dec_t
	add	x0,x28,g_t_thru
	bl	dec_t
	add	x0,x28,g_t_glue
	bl	dec_t
	add	x0,x28,g_t_laser
	bl	dec_t
	bl	balls_step
	ldr	x0,[x28,g_state]
	cmp	x0,ST_PLAY
	b.ne	.out
	bl	capsules_step
	bl	lasers_step
	bl	fuses_step
	ldr	x0,[x28,g_left]
	cbnz	x0,.out
	movl	x0,500
	bl	add_score
	mov	x0,ST_CLEAR
	bl	set_state
	adr	x0,gs_tab_win
	bl	gs_seq
	b	.out
.lost:	cmp	x0,ST_LOST
	b.ne	.clr
	ldr	x0,[x28,g_timer]
	add	x0,x0,1
	str	x0,[x28,g_timer]
	cmp	x0,50
	b.lo	.out
	ldr	x0,[x28,g_lives]
	cbz	x0,.over
	bl	reset_round
	mov	x0,ST_READY
	bl	set_state
	b	.out
.over:	bl	keep_best
	mov	x0,ST_OVER
	bl	set_state
	b	.out
.clr:	cmp	x0,ST_CLEAR
	b.ne	.out
	ldr	x0,[x28,g_timer]
	add	x0,x0,1
	str	x0,[x28,g_timer]
	cmp	x0,110
	b.lo	.out
	bl	next_level
.out:	epilogue

next_level:
	prologue
	ldr	x0,[x28,g_level]
	add	x0,x0,1
	str	x0,[x28,g_level]
	bl	keep_best
	bl	load_level
	mov	x0,ST_READY
	bl	set_state
	epilogue

; ---------------------------------------------------------------------------
;  Evenements
; ---------------------------------------------------------------------------
do_pause:
	b	gp_open

; (gamepause) la partie s'arrete (balle en jeu ou pas encore lancee) -> x0 = 1
; si elle etait en cours
game_pause:
	prologue
	str	xzr,[x28,g_btn]
	ldr	x1,[x28,g_state]
	mov	x0,0
	cmp	x1,ST_PLAY
	b.eq	.p
	cmp	x1,ST_READY
	b.ne	.out
.p:	str	x1,[x28,g_prev]
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
	ldr	x0,[x28,g_prev]
	bl	set_state
.out:	epilogue

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

; Espace, Entree, clic, appui : selon l'etat
action:
	prologue
	ldr	x0,[x28,g_state]
	cmp	x0,ST_TITLE
	b.eq	.new
	cmp	x0,ST_OVER
	b.ne	.r
.new:	bl	new_game
	b	.out
.r:	cmp	x0,ST_READY
	b.ne	.pl
	bl	launch
	mov	x0,ST_PLAY
	bl	set_state
	b	.out
.pl:	cmp	x0,ST_PLAY
	b.ne	.p
	bl	launch
	b	.out
.p:	cmp	x0,ST_CLEAR
	b.ne	.out
	ldr	x0,[x28,g_timer]		; (pas d'un coup de doigt egare)
	cmp	x0,30
	b.lo	.out
	bl	next_level
.out:	epilogue

; la manette : A, B, X, Y ou haut (fronts) agissent comme Espace ; la croix
; tenue deplace la raquette, A, B, X, Y tenus tirent au laser (step)
on_pad:
	prologue
	ldv	x0,gp_new
	tst	x0,PAD_A or PAD_B or PAD_X or PAD_Y
	b.ne	.go
	tst	x0,PAD_UP
	b.eq	.out
	ldr	x1,[x28,g_state]		; (haut : seulement pour lancer)
	cmp	x1,ST_PLAY
	b.eq	.out
.go:	bl	action
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
	cmp	w3,2
	b.eq	.rel
	str	x0,[x28,g_mx]
	mov	x0,1
	str	x0,[x28,g_mmoved]
	cbz	w3,.out
	ldr	w3,[x2,EV_CODE]
	tbz	w3,0,.out
	mov	x0,HUD_H
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
;  Une image
; ---------------------------------------------------------------------------
draw:
	prologue
	push_all
	ldr	x0,[x28,g_ldirty]
	cbz	x0,.parts
	bl	build_layer
	b	.base
.parts:	ldr	x0,[x28,g_fdirty]
	ldr	x1,[x28,g_hdirty]
	orr	x2,x0,x1
	cbz	x2,.base
	ldv	x0,gf_layer
	stv	x0,gf_dst
	ldr	x0,[x28,g_fdirty]
	cbz	x0,.h
	str	xzr,[x28,g_fdirty]
	bl	draw_field
.h:	ldr	x0,[x28,g_hdirty]
	cbz	x0,.pe
	str	xzr,[x28,g_hdirty]
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	bl	draw_hud
	bl	ui_canvas_end
.pe:	adr	x0,gf_frame
	stv	x0,gf_dst
.base:	adr	x0,gf_frame
	ldv	x1,gf_layer
	movl	x2,GF_PITCH*GF_H
	bl	gf_copy
	ldr	x19,[x28,g_tick]		; deux lumieres derivent sur le decor
	lsr	x0,x19,2
	bl	sin8
	mov	x20,x0
	lsr	x0,x19,3
	add	x0,x0,40
	bl	sin8
	lsl	x0,x0,1
	mov	x1,x0
	lsl	x0,x20,1
	add	x0,x0,50
	lsr	x1,x1,1
	add	x1,x1,80
	ldr	x2,[x28,g_sorb]
	bl	gf_bglow
	lsr	x0,x19,3
	add	x0,x0,128
	bl	sin8
	mov	x20,x0
	lsr	x0,x19,2
	add	x0,x0,200
	bl	sin8
	add	x1,x0,x0,lsr 1
	lsl	x0,x20,1
	add	x0,x0,10
	ldr	x2,[x28,g_sorb+8]
	bl	gf_bglow
	add	x19,x28,g_mote			; les poussieres
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
	ldr	x0,[x28,g_state]		; le jeu lui-meme
	cmp	x0,ST_TITLE
	b.eq	.pts
	add	x19,x28,g_cap			; capsules
	mov	x20,NC
.c:	ldr	x0,[x19]
	cbz	x0,.cn
	ldr	x0,[x19,8]
	asr	x0,x0,16
	ldr	x1,[x19,16]
	asr	x1,x1,16
	ldr	x2,[x19,24]
	add	x3,x28,g_scap
	ldr	x2,[x3,x2,lsl 3]
	bl	gf_sprite
.cn:	add	x19,x19,32
	subs	x20,x20,1
	b.ne	.c
	add	x19,x28,g_las			; lasers
	mov	x20,NL
.l:	ldr	w0,[x19]
	cbz	w0,.ln
	ldr	w0,[x19,4]
	ldr	w1,[x19,8]
	ldr	x2,[x28,g_slaser]
	bl	gf_sprite
.ln:	add	x19,x19,16
	subs	x20,x20,1
	b.ne	.l
	ldr	x0,[x28,g_state]		; raquette
	cmp	x0,ST_OVER
	b.eq	.balls
	cmp	x0,ST_LOST
	b.eq	.balls
	mov	x2,0
	ldr	x0,[x28,g_t_glue]
	cbz	x0,.ps
	mov	x2,1
.ps:	ldr	x0,[x28,g_t_laser]
	cbz	x0,.ps2
	mov	x2,2
.ps2:	ldr	x0,[x28,g_psize]
	add	x2,x0,x2,lsl 2
	add	x0,x28,g_spad
	ldr	x2,[x0,x2,lsl 3]
	ldr	x0,[x28,g_px]
	mov	x1,RKT_Y
	bl	gf_sprite
.balls:	ldr	x0,[x28,g_t_thru]
	cmp	x0,0
	cset	x21,ne				; 1 : balle traversante
	add	x19,x28,g_ball
	mov	x20,NB
.b:	ldr	x0,[x19,B_ON]
	cbz	x0,.bn
	ldr	x0,[x19,B_STUCK]
	cbnz	x0,.bs
	ldr	x22,[x19,B_HI]			; la trainee : 3 points sur la longueur
	mov	x23,0
.t:	add	x0,x23,1
	lsl	x0,x0,1
	sub	x0,x22,x0
	and	x0,x0,HIST-1
	add	x1,x19,B_H
	add	x1,x1,x0,lsl 3
	ldp	w24,w25,[x1]
	add	x0,x23,x21,lsl 1
	add	x0,x0,x21
	add	x1,x28,g_sdisc
	ldr	x2,[x1,x0,lsl 3]
	ldr	w0,[x2]
	lsr	x0,x0,1
	sub	x0,x24,x0
	ldr	w1,[x2,4]
	lsr	x1,x1,1
	sub	x1,x25,x1
	bl	gf_glow
	add	x23,x23,1
	cmp	x23,3
	b.lo	.t
.bs:	ldr	x0,[x19,B_X]
	asr	x0,x0,16
	sub	x0,x0,BALL_R
	ldr	x1,[x19,B_Y]
	asr	x1,x1,16
	sub	x1,x1,BALL_R
	add	x2,x28,g_sball
	ldr	x2,[x2,x21,lsl 3]
	bl	gf_sprite
.bn:	add	x19,x19,B_SIZE
	subs	x20,x20,1
	b.ne	.b
.pts:	add	x19,x28,g_flash			; eclairs
	mov	x20,NF
.f:	ldr	x0,[x19]
	cbz	x0,.fn
	ldp	x0,x1,[x19,8]
	sub	x0,x0,FLASH_D/2
	sub	x1,x1,FLASH_D/2
	ldr	x2,[x28,g_sflash]
	bl	gf_glow
.fn:	add	x19,x19,24
	subs	x20,x20,1
	b.ne	.f
	adr	x19,g_parts			; particules
	mov	x20,NP
.p:	ldr	w21,[x19,P_LIFE]
	cbz	w21,.pn
	mov	w1,w21
	lsl	w1,w1,4
	cmp	w1,256
	mov	w0,256
	csel	w1,w0,w1,hi
	ldr	w0,[x19,P_COL]
	bl	shade
	mov	w4,w0
	ldp	w0,w1,[x19,P_X]
	asr	w0,w0,16
	asr	w1,w1,16
	sxtw	x0,w0
	sxtw	x1,w1
	ldr	w2,[x19,P_KIND]
	add	w2,w2,2
	mov	x3,x2
	bl	gf_rect
.pn:	add	x19,x19,P_SIZE
	subs	x20,x20,1
	b.ne	.p
	mov	x19,28				; le temps des bonus, en bas
	ldr	x2,[x28,g_t_thru]
	movl	w4,0x00FF8830
	bl	.bar
	ldr	x2,[x28,g_t_glue]
	movl	w4,0x0040D080
	bl	.bar
	ldr	x2,[x28,g_t_laser]
	movl	w4,0x00F06040
	bl	.bar
	ldr	x2,[x28,g_t_slow]
	movl	w4,0x0060A0F0
	bl	.bar
	ldr	x2,[x28,g_t_fast]
	movl	w4,0x00F0D040
	bl	.bar
	pop_all
	epilogue
.bar:	cbz	x2,.ret
	prologue
	lsr	x2,x2,3
	mov	x0,x19
	add	x19,x19,x2
	add	x19,x19,6
	mov	x1,GF_H-5
	mov	x3,3
	bl	gf_rect
	epilogue
.ret:	ret

; ---------------------------------------------------------------------------
;  Tables
; ---------------------------------------------------------------------------
	align 4
brick_col dw 0x00E04848,0x00F08A34,0x00E8C838,0x0048C468,0x003C8CE4,0x00A45CDC,0x00E868A8,0x0048CCD8
	dw 0x00C0CAD8,0x00C0CAD8,0x00D8A830,0x00D8A830,0x00D8A830,0x00687080,0x00C02818
brick_pts db 10,12,14,16,18,20,22,24,20,30,30,40,50,0,30
	align 4
pad_w	db 44,68,96,128
	align 4
pad_cap	dw 0x00E04848,0x0040D080,0x00F09030
pad_prof dh 170,300,400,340,290,260,240,220,200,180,160,140,120,90
wall_prof dh 150,300,340,330,300,280,260,250,240,235,230,225,220,210,200,190,170,150,120,80
	; rang 0 a 14 : sinus et cosinus (1.14) de -66 a 66 degres
dirs	dh -14968,6664, -13674,9026, -12010,11144, -10022,12961, -7764,14428, -5295,15505, -2684,16163, 0,16384
	dh 2684,16163, 5295,15505, 7764,14428, 10022,12961, 12010,11144, 13674,9026, 14968,6664
	align 4
cap_col	dw 0x0040B860,0x00D04848,0x004080E0,0x0040A0C0,0x00D07830,0x00E08020,0x0040B890,0x00D05030,0x00E04870
cap_text db '<>',0,0, '><',0,0, 'x3',0,0, 'L',0,0,0, 'R',0,0,0, 'T',0,0,0, 'C',0,0,0, 'Z',0,0,0, '+1',0,0
cap_odds db CP_WIDE,CP_WIDE,CP_WIDE,CP_MULTI,CP_MULTI,CP_MULTI,CP_LASER,CP_LASER,CP_LASER,CP_GLUE,CP_GLUE
	db CP_SLOW,CP_SLOW,CP_THRU,CP_THRU,CP_NARROW,CP_NARROW,CP_FAST,CP_LIFE
CAP_ODDS = $-cap_odds
cap_labels db 'Raquette large',0,'Raquette courte',0,'Multi-balle',0,'Balle lente',0,'Balle rapide',0
	db 'Balle traversante',0,'Colle',0,'Laser',0,'Vie en plus',0
	align 4
snd_catch:
	gs_ent	0,660,50,AKW_TRIANGLE,90
	gs_ent	50,880,70,AKW_TRIANGLE,90
	gs_end
	align 4

; Niveaux : 15 colonnes, 14 lignes au plus. a-h : couleurs ; S argent (2 coups),
; G or (3 coups), # acier (indestructible), * explosive, . vide.
LV_HEAD		= 40			; style (dw), teinte (dw), nom
LV_SIZE		= LV_HEAD+NCELL
macro level name, style, tint {
	lv_cur = $
	dw	style, tint
	db	name, 0
	db	(lv_cur+LV_HEAD-$) dup 0
}
macro row s {
	local a
	a = $
	db	s
	if $-a <> COLS
	 err
	end if
}
macro level_end {
	db	(lv_cur+LV_SIZE-$) dup '.'
}
levels:
level <'Premiers pas'>, 0, 0x00385888
	row '...............'
	row '...............'
	row 'aaaaaaaaaaaaaaa'
	row 'bbbbbbbbbbbbbbb'
	row 'ccccccccccccccc'
	row 'ddddddddddddddd'
	row 'eeeeeeeeeeeeeee'
	row 'fffffffffffffff'
level_end
level <'Pyramide'>, 1, 0x00806850
	row '.......S.......'
	row '......SaS......'
	row '.....SaaaS.....'
	row '....SbbbbbS....'
	row '...SbbbbbbbS...'
	row '..SccccccccS...'
	row '.SccccccccccS..'
	row 'SddddddddddddS.'
	row 'ddddddddddddddd'
level_end
level <'Damier'>, 2, 0x00407058
	row '...............'
	row 'a.b.c.d.e.f.g.h'
	row '.a.b.c.d.e.f.g.'
	row 'h.a.b.c.d.e.f.g'
	row '.h.a.b.c.d.e.f.'
	row 'g.h.a.b.c.d.e.f'
	row '.g.h.a.b.c.d.e.'
	row 'f.g.h.a.b.c.d.e'
	row '.f.g.h.a.b.c.d.'
level_end
level <'Diamant'>, 3, 0x00604888
	row '.......h.......'
	row '......hSh......'
	row '.....hSeSh.....'
	row '....hSeeeSh....'
	row '...hSeeGeeSh...'
	row '..hSeeG*GeeSh..'
	row '...hSeeGeeSh...'
	row '....hSeeeSh....'
	row '.....hSeSh.....'
	row '......hSh......'
	row '.......h.......'
level_end
level <'Forteresse'>, 1, 0x00586878
	row '...............'
	row 'S.S.S.S.S.S.S.S'
	row 'SSSSSSSSSSSSSSS'
	row '#ccccccccccccc#'
	row '#c###c###c###c#'
	row '#c#G#c#G#c#G#c#'
	row '#ccccccccccccc#'
	row '#bbbbbb.bbbbbb#'
	row '#bbbbb...bbbbb#'
	row '##...........##'
level_end
level <'Envahisseur'>, 5, 0x00387050
	row '...............'
	row '...d.......d...'
	row '....d.....d....'
	row '...ddddddddd...'
	row '..dd.ddddd.dd..'
	row '.ddddddddddddd.'
	row '.d.ddddddddd.d.'
	row '.d.d.......d.d.'
	row '....dd...dd....'
	row '...............'
	row 'SSS.SSS.SSS.SSS'
level_end
level <'C',0xC5,0x93,'ur'>, 4, 0x00803858
	row '...............'
	row '...aaa...aaa...'
	row '..aaaaa.aaaaa..'
	row '.aagaaaaaaagaa.'
	row '.aaggaaaaaggaa.'
	row '.aaagg*a*ggaaa.'
	row '..aaaggggggaa..'
	row '...aaaggggaa...'
	row '....aaaggaa....'
	row '.....aaaaa.....'
	row '......aaa......'
	row '.......a.......'
level_end
level <'Vagues'>, 3, 0x00306888
	row '...............'
	row 'ee...ee...ee...'
	row '..ee...ee...ee.'
	row '....ee...ee...e'
	row 'hh...hh...hh...'
	row '..hh...hh...hh.'
	row '....hh...hh...h'
	row 'dd...dd...dd...'
	row '..dd...dd...dd.'
	row '....dd...dd...d'
	row '...............'
	row '#..#..#..#..#..'
level_end
level <'Couloirs'>, 5, 0x00507088
	row '...............'
	row '#a#b#c#d#e#f#g#'
	row '#a#b#c#d#e#f#g#'
	row '#a#b#c#d#e#f#g#'
	row '#a#b#c#d#e#f#g#'
	row '#a#b#c#d#e#f#g#'
	row '#S#S#S#S#S#S#S#'
	row '#.#.#.#.#.#.#.#'
	row '#.#.#.#.#.#.#.#'
	row '...............'
level_end
level <'Poudri',0xC3,0xA8,'re'>, 0, 0x00885030
	row '...............'
	row 'bb*bb*bb*bb*bb.'
	row 'a*bb*bb*bb*bba.'
	row 'cc*cc*cc*cc*cc.'
	row 'S.S.S.S.S.S.S.S'
	row '.*.*.*.*.*.*.*.'
	row 'S.S.S.S.S.S.S.S'
	row 'aa*aa*aa*aa*aa.'
level_end
level <'Tunnel'>, 4, 0x00486080
	row '###############'
	row '#ffffffffffffe#'
	row '#f###########e#'
	row '#f#ddddddddd#e#'
	row '#f#d#######d#e#'
	row '#f#d#GGGGG#d#e#'
	row '#f#d#######d#e#'
	row '#f#ddddddddd#e#'
	row '#f#####.#####e#'
	row '#fffff...eeeee#'
	row '###.........###'
level_end
level <'Spirale'>, 2, 0x00705090
	row 'fffffffffffffff'
	row 'f.............S'
	row 'f.eeeeeeeeeee.S'
	row 'f.e.........d.S'
	row 'f.e.ccccccc.d.S'
	row 'f.e.c.....b.d.S'
	row 'f.e.c.G*G.b.d.S'
	row 'f.e.c.....b.d.S'
	row 'f.e.bbbbbbb.d.S'
	row 'f.e.........d.S'
	row 'f.ddddddddddd.S'
	row 'f.............S'
	row 'SSSSSSSSSSSSSSS'
level_end
level <'Cible'>, 4, 0x00885868
	row '...............'
	row '.....aaaaa.....'
	row '...aaSSSSSaa...'
	row '..aSSbbbbbSSa..'
	row '.aSbbGGGGGbbSa.'
	row '.aSbGG***GGbSa.'
	row '.aSbGG***GGbSa.'
	row '.aSbbGGGGGbbSa.'
	row '..aSSbbbbbSSa..'
	row '...aaSSSSSaa...'
	row '.....aaaaa.....'
level_end
level <'Citadelle'>, 1, 0x00707078
	row 'G.G.G.G.G.G.G.G'
	row 'GGGGGGGGGGGGGGG'
	row '#*S*S*S*S*S*S*#'
	row '#SSSSSSSSSSSSS#'
	row '#hhh##hhh##hhh#'
	row '#hhh##hhh##hhh#'
	row '#ggg##ggg##ggg#'
	row '#ggg##ggg##ggg#'
	row '#fffffffffffff#'
	row '#ffff#####ffff#'
	row '#ffff#...#ffff#'
	row '##...........##'
level_end
NLEVELS = ($-levels)/LV_SIZE
	align 4

segment readable writeable
kit_data
s_title		db 'Arkanoid',0
s_sub		db 'Casse-briques',0
s_start		db 'Espace, A ou toucher pour jouer',0
s_score		db 'Score',0
s_best		db 'Record',0
s_best1		db 'Record : ',0
s_level		db 'Niveau',0
s_level1	db 'Niveau ',0
s_launch	db 'Espace, A ou toucher pour lancer',0
s_over		db 'Partie termin',0xC3,0xA9,'e',0
s_again		db 'Espace, A ou toucher pour rejouer',0
s_clear		db ' termin',0xC3,0xA9,0,0
s_bonus		db 'Bonus 500',0
p_dir		db 'SD0:/home/arkanoid',0
p_best		db 'SD0:/home/arkanoid/best.txt',0
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
		align 8
g_noise		rb 4096
g_parts		rb NP*P_SIZE
g_bank		rb BANK_SIZE
ga_data
gp_data
gf_data
