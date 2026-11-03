; ============================================================================
;  inc/cards.inc - les cartes des jeux de patience (d'apres games/cards.h
;  d'Onyx) et leur table : carte = enseigne * 13 + rang (enseigne 0 trefle,
;  1 carreau, 2 coeur, 3 pique ; rang 0 as .. 12 roi).
;
;  A inclure dans le code, apres kit_use d'AppKit et d'UIKit ; `cards_data`
;  dans le segment inscriptible, avant ui_data.
;
;  Dessin : les enseignes sont des formes anticrenelees (ui_vp_*) rendues une
;  fois par taille dans des masques ; chaque carte est composee dans une
;  planche (canevas cache) au premier besoin, puis copiee avec ses coins
;  arrondis : poser 52 cartes ne coute que des copies.
;
;  Table : deux couches de la taille de la fenetre (canevas caches). T tient
;  la table sans les cartes glissees ; K compose T + cartes glissees ; ce qui
;  change seulement va dans la surface de la fenetre, d'un coup, sans passer
;  par un etat ou la carte manque : le glisser ne scintille pas.
; ============================================================================

CARD_BACK	= 52			; le dos, dans card_blit
CARD_MIN_W	= 32
CARD_MAX_W	= 200			; (grand as : 2/5, doit tenir dans CK_CELL)
CK_CELL		= 80			; case d'un masque (3 tailles x 4 enseignes)
CK_CANW		= 3*CK_CELL
CK_CANH		= 4*CK_CELL + 40
CK_CORNER	= 32			; pas de la table des coins
CK_EDGE		= 0x00505860
CK_FACE		= 0x00FFFFFF
CK_RED		= 0x00C81828
CK_BLACK	= 0x00141820
CK_TINT_R	= 0x00F8E2DC
CK_TINT_B	= 0x00DEE4F4
CK_BACK		= 0x002050B0
CK_BACK_DOT	= 0x004078D8
CK_BACK_LINE	= 0x0090B8F0

BAND_H		= 36			; bandeau du haut : boutons, etat, cadre
BAND_BTN_H	= 26
BAND_MAX	= 4			; boutons
TAPIS_BAND	= 0x00124E26
TAPIS_HAUT	= 0x001F7A3A
TAPIS_BAS	= 0x0017622E
TAPIS_SLOT	= 0x0019692F
TAPIS_SLOT_L	= 0x003E9A5C
TAPIS_HINT	= 0x002E8A4C
TAPIS_BTN	= 0x001F7A3A
TAPIS_TEXT	= 0x00E4F4E8
TAPIS_NOTE	= 0x00FFE08A

DRAG_MIN	= 6			; pixels avant qu'un appui devienne un glisser
CK_TAP_MS	= 350			; deuxieme appui d'un double-clic : ignore

macro ldv reg, var {
	adr	x16,var
	ldr	reg,[x16]
}
macro stv reg, var {
	adr	x16,var
	str	reg,[x16]
}

; rd = pixel, rs = couleur, ra = 0..255 -> rd ; modifie w9..w12
macro px_mix rd, rs, ra {
	add	w12,ra,ra,lsr 7
	mov	w11,256
	sub	w11,w11,w12
	and	w9,rd,0x00FF00FF
	and	w10,rs,0x00FF00FF
	mul	w9,w9,w11
	mul	w10,w10,w12
	add	w9,w9,w10
	lsr	w9,w9,8
	and	w9,w9,0x00FF00FF
	and	w10,rd,0x0000FF00
	and	rd,rs,0x0000FF00
	mul	w10,w10,w11
	mul	rd,rd,w12
	add	rd,rd,w10
	lsr	rd,rd,8
	and	rd,rd,0x0000FF00
	orr	rd,rd,w9
}

; -> x0 = pseudo-aleatoire sur 15 bits
ck_rnd:
	adr	x1,ck_seed
	ldr	w0,[x1]
	movl	w2,1103515245
	mul	w0,w0,w2
	mov	w2,12345
	add	w0,w0,w2
	str	w0,[x1]
	ubfx	w0,w0,16,15
	ret

; x0 = n -> x0 = 0 .. n - 1
ck_rnd_n:
	stp	x30,x0,[sp,-16]!
	bl	ck_rnd
	ldp	x30,x1,[sp],16
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	ret

; x0 = largeur, x1 = hauteur -> x0 = canevas cache
ck_canvas:
	prologue
	str	x19,[sp,-16]!
	mov	x3,x1
	mov	x2,x0
	mov	x0,0
	mov	x1,0
	bl	ui_canvas
	cbz	x0,.fail
	mov	x19,x0
	bl	ui_canvas_pixels
	cbz	x0,.fail
	mov	x0,x19
	mov	x1,0
	bl	ui_w_show
	mov	x0,x19
	ldr	x19,[sp],16
	epilogue
.fail:	mov	x0,1				; (plus de memoire)
	sys	SYS_EXIT

; x0, x1 = taille voulue -> x0, x1 = ce qui tient entre les barres de l'ecran
; (avant la fenetre : le PocketTerm n'a que 800 x 600)
cards_fit:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	mov	x20,x1
	mov	x0,sp
	bl	ui_screen
	ldp	x0,x1,[sp]
	ldp	x2,x3,[sp,16]
	mov	x4,64				; (le panel flotte : hors des reserves)
	cmp	x2,x4
	csel	x2,x4,x2,lo
	mov	x4,44				; (ou Elegant pose une nouvelle fenetre)
	cmp	x3,x4
	csel	x3,x4,x3,lo
	sub	x0,x0,16
	sub	x1,x1,x2
	sub	x1,x1,x3
	cmp	x0,x19
	csel	x0,x19,x0,hi
	cmp	x1,x20
	csel	x1,x20,x1,hi
	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; a appeler une fois la fenetre creee
cards_init:
	prologue
	mov	x0,CK_CANW
	mov	x1,CK_CANH
	bl	ck_canvas
	stv	x0,c_mask
	bl	ui_canvas_pixels
	stv	x0,c_maskpx
	mov	x0,UFN_SERIFBOLD
	bl	ui_font_get
	stv	x0,c_serif
	sys	SYS_TIME_MS
	orr	w0,w0,1
	adr	x1,ck_seed
	str	w0,[x1]
	epilogue

; x0 = enseigne, x1, x2 = coin, x3 = cote : la forme, en blanc
ck_suit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,128
	adr	x4,suit_ofs
	ldrb	w4,[x4,x0]
	adr	x19,suit_data
	add	x19,x19,x4
	lsl	x20,x1,4
	lsl	x21,x2,4
	mov	x22,x3
.o:	ldrb	w0,[x19],1
	cbz	w0,.out
	cmp	w0,1
	b.ne	.poly
	ldrb	w0,[x19],1
	mul	x0,x0,x22
	add	x0,x20,x0,lsr 4
	ldrb	w1,[x19],1
	mul	x1,x1,x22
	add	x1,x21,x1,lsr 4
	ldrb	w2,[x19],1
	mul	x2,x2,x22
	lsr	x2,x2,4
	movl	w3,0x00FFFFFF
	bl	ui_vp_disc
	b	.o
.poly:	ldrb	w4,[x19],1			; points
	mov	x5,0
.p:	ldrb	w0,[x19],1
	mul	x0,x0,x22
	add	x0,x20,x0,lsr 4
	ldrb	w1,[x19],1
	mul	x1,x1,x22
	add	x1,x21,x1,lsr 4
	add	x2,sp,x5,lsl 3
	stp	w0,w1,[x2]
	add	x5,x5,1
	cmp	x5,x4
	b.lo	.p
	mov	x0,sp
	mov	x1,x4
	movl	w2,0x00FFFFFF
	bl	ui_vp_poly
	b	.o
.out:	add	sp,sp,128
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les masques de la taille courante : enseignes, coin
ck_masks:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x0,c_mask
	bl	ui_canvas_begin
	mov	x0,0
	mov	x1,0
	mov	x2,CK_CANW
	mov	x3,CK_CANH
	mov	w4,0
	bl	ui_fill
	mov	x19,0				; taille
.t:	mov	x20,0				; enseigne
.s:	mov	x0,x20
	mov	x1,CK_CELL
	mul	x1,x1,x19
	mov	x2,CK_CELL
	mul	x2,x2,x20
	adr	x3,c_pip
	ldr	x3,[x3,x19,lsl 3]
	bl	ck_suit
	add	x20,x20,1
	cmp	x20,4
	b.lo	.s
	add	x19,x19,1
	cmp	x19,3
	b.lo	.t
	ldv	x2,c_r				; le coin : disque de rayon r
	lsl	x0,x2,4
	mov	x1,4*CK_CELL
	add	x1,x1,x2
	lsl	x1,x1,4
	lsl	x2,x2,4
	movl	w3,0x00FFFFFF
	bl	ui_vp_disc
	bl	ui_canvas_end
	ldv	x0,c_maskpx			; ... recopie dans sa table
	movl	x1,4*CK_CELL*CK_CANW
	add	x0,x0,x1,lsl 2
	adr	x1,c_corner
	ldv	x2,c_r
	mov	x3,0
.r:	mov	x4,0
.c:	mov	x5,CK_CANW
	mul	x5,x5,x3
	add	x5,x5,x4
	add	x5,x0,x5,lsl 2
	ldrb	w5,[x5]
	add	x6,x4,x3,lsl 5
	strb	w5,[x1,x6]
	add	x4,x4,1
	cmp	x4,x2
	b.lo	.c
	add	x3,x3,1
	cmp	x3,x2
	b.lo	.r
	ldp	x19,x20,[sp],16
	epilogue

; x0 = largeur d'une carte : les tailles de tout ce qui en depend
cards_size:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldv	x1,c_w
	cmp	x0,x1
	b.eq	.out
	stv	x0,c_w
	mov	x1,11
	mul	x20,x0,x1
	lsr	x20,x20,3			; hauteur (64 x 88 chez Onyx)
	stv	x20,c_h
	mov	x1,10
	udiv	x2,x19,x1			; rayon
	cmp	x2,3
	mov	x3,3
	csel	x2,x3,x2,lo
	stv	x2,c_r
	mov	x1,15
	mul	x2,x19,x1
	lsr	x2,x2,6				; corps de la valeur
	cmp	x2,11
	mov	x3,11
	csel	x2,x3,x2,lo
	stv	x2,c_idx
	add	x3,x2,3
	stv	x3,c_ih				; bande de la valeur
	mov	x1,3
	mul	x3,x2,x1
	lsr	x3,x3,2				; enseigne de la valeur
	cmp	x3,7
	mov	x4,7
	csel	x3,x4,x3,lo
	adr	x4,c_pip
	str	x3,[x4]
	mul	x3,x19,x1
	lsr	x3,x3,4				; enseigne du milieu
	str	x3,[x4,8]
	lsl	x3,x19,1
	mov	x1,5
	udiv	x3,x3,x1			; celle de l'as
	str	x3,[x4,16]
	lsr	x3,x20,2
	stv	x3,c_dy_max			; pas d'une colonne
	lsr	x3,x20,3
	stv	x3,c_dy_min
	mov	x0,13				; la planche
	mul	x0,x0,x19
	mov	x1,5
	mul	x1,x1,x20
	ldv	x2,c_sheet_w
	ldv	x3,c_sheet_h
	cmp	x0,x2
	b.hi	.grow
	cmp	x1,x3
	b.ls	.same
.grow:	stp	x0,x1,[sp,-16]!
	ldv	x0,c_sheet
	cbz	x0,.ng
	bl	ui_w_destroy
.ng:	ldp	x0,x1,[sp],16
	stv	x0,c_sheet_w
	stv	x1,c_sheet_h
	bl	ck_canvas
	stv	x0,c_sheet
	bl	ui_canvas_pixels
	stv	x0,c_sheet_px
	stv	x1,c_sheet_pitch
.same:	adr	x0,c_ready
	mov	x1,0
.z:	strb	wzr,[x0,x1]
	add	x1,x1,1
	cmp	x1,53
	b.lo	.z
	bl	ck_masks
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = colonnes : la carte la plus grande pour la fenetre (ecarts, marges)
; hauteur : rangee du haut + colonne de trois cartes ou presque
cards_layout:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldv	x1,g_w
	add	x2,x0,1
	lsl	x2,x2,2				; ecarts d'au moins 4
	sub	x1,x1,x2
	udiv	x20,x1,x0			; selon la largeur
	ldv	x1,g_h
	sub	x1,x1,BAND_H+16
	lsl	x1,x1,3				; selon la hauteur : 4 hauteurs de carte
	mov	x2,44
	udiv	x1,x1,x2
	cmp	x1,x20
	csel	x20,x1,x20,lo
	cmp	x20,CARD_MIN_W
	mov	x1,CARD_MIN_W
	csel	x20,x1,x20,lo
	cmp	x20,CARD_MAX_W
	mov	x1,CARD_MAX_W
	csel	x20,x1,x20,hi
	mov	x0,x20
	bl	cards_size
	ldv	x1,g_w
	mul	x2,x19,x20
	sub	x1,x1,x2
	add	x2,x19,1
	sdiv	x1,x1,x2			; ecart
	lsr	x2,x20,2
	cmp	x1,x2
	csel	x1,x2,x1,gt			; (pas trop : le jeu reste groupe)
	stv	x1,l_gap
	sub	x2,x19,1
	mul	x2,x2,x1
	madd	x2,x19,x20,x2			; largeur du jeu
	ldv	x3,g_w
	sub	x3,x3,x2
	asr	x3,x3,1
	stv	x3,l_ox
	mov	x3,BAND_H+6
	stv	x3,l_top
	ldv	x4,c_h
	add	x3,x3,x4
	lsr	x4,x4,6
	add	x3,x3,x4
	add	x3,x3,8
	stv	x3,l_tab
	ldp	x19,x20,[sp],16
	epilogue

; x0 = colonne -> x0 = x
ck_col_x:
	ldv	x1,c_w
	ldv	x2,l_gap
	add	x1,x1,x2
	mul	x0,x0,x1
	ldv	x1,l_ox
	add	x0,x0,x1
	ret

; x0 = x (centre) -> x0 = colonne la plus proche, bornee (x1 = colonnes)
ck_col_at:
	ldv	x2,l_ox
	sub	x0,x0,x2
	ldv	x2,l_gap
	asr	x3,x2,1
	add	x0,x0,x3
	ldv	x3,c_w
	add	x2,x2,x3
	sdiv	x0,x0,x2
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x0,x1
	b.lo	.ret
	sub	x0,x1,1
.ret:	ret

; x0..x3 = rectangle, x4 = rayon, w5 = couleur : boite aux coins anticrenels,
; dans la cible courante
ck_rbox:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	w24,w5
	add	x0,x19,x23
	mov	x1,x20
	sub	x2,x21,x23,lsl 1
	mov	x3,x22
	mov	w4,w24
	bl	ui_fill
	mov	x0,x19
	add	x1,x20,x23
	mov	x2,x21
	sub	x3,x22,x23,lsl 1
	mov	w4,w24
	bl	ui_fill
	add	x0,x19,x23
	add	x1,x20,x23
	bl	.disc
	add	x0,x19,x21
	sub	x0,x0,x23
	add	x1,x20,x23
	bl	.disc
	add	x0,x19,x23
	add	x1,x20,x22
	sub	x1,x1,x23
	bl	.disc
	add	x0,x19,x21
	sub	x0,x0,x23
	add	x1,x20,x22
	sub	x1,x1,x23
	bl	.disc
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.disc:	lsl	x0,x0,4
	lsl	x1,x1,4
	lsl	x2,x23,4
	mov	w3,w24
	b	ui_vp_disc

; x0 = taille 0..2, x1 = enseigne, x2, x3 = coin, w4 = couleur, x5 = 1
; renversee : pose le masque dans st_px (feuille)
ck_stamp:
	adr	x6,c_pip
	ldr	x6,[x6,x0,lsl 3]
	mov	x8,CK_CELL
	mul	x1,x1,x8
	mul	x0,x0,x8
	mov	x8,CK_CANW
	madd	x1,x1,x8,x0
	ldv	x8,c_maskpx
	add	x7,x8,x1,lsl 2
	ldv	x13,st_pitch
	ldv	x14,st_px
	madd	x3,x3,x13,x2
	add	x14,x14,x3,lsl 2
	mov	x15,0
.r:	cmp	x15,x6
	b.hs	.ret
	mov	x8,x15
	cbz	x5,.n
	sub	x8,x6,1
	sub	x8,x8,x15
.n:	mov	x0,CK_CANW*4
	mul	x8,x8,x0
	add	x8,x8,x7
	mov	x1,0
.c:	add	x2,x8,x1,lsl 2
	ldrb	w2,[x2]
	cbz	w2,.nx
	ldr	w3,[x14,x1,lsl 2]
	px_mix	w3,w4,w2
	str	w3,[x14,x1,lsl 2]
.nx:	add	x1,x1,1
	cmp	x1,x6
	b.lo	.c
	add	x14,x14,x13,lsl 2
	add	x15,x15,1
	b	.r
.ret:	ret

; x0 = carte (ou CARD_BACK) : la compose dans la planche
ck_render:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	ldv	x0,cd_lay			; (la cible n'a qu'un niveau)
	cbz	x0,.nl
	bl	ui_canvas_end
.nl:	mov	x2,13
	udiv	x26,x19,x2			; enseigne (ligne)
	msub	x25,x26,x2,x19			; rang (colonne)
	ldv	x22,c_w
	ldv	x23,c_h
	mul	x20,x25,x22
	mul	x21,x26,x23
	ldv	x0,c_sheet
	bl	ui_canvas_begin
	ldv	x0,c_sheet_px
	stv	x0,st_px
	ldv	x0,c_sheet_pitch
	stv	x0,st_pitch
	mov	x0,x20				; le bord, puis le corps
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	movl	w4,CK_EDGE
	bl	ui_fill
	add	x0,x20,1
	add	x1,x21,1
	sub	x2,x22,2
	sub	x3,x23,2
	ldv	x4,c_r
	sub	x4,x4,1
	movl	w5,CK_FACE
	bl	ck_rbox
	cmp	x19,CARD_BACK
	b.eq	.back
	movl	w24,CK_BLACK
	sub	x0,x26,1
	cmp	x0,1
	b.hi	.ink
	movl	w24,CK_RED
.ink:	adr	x0,s_ranks			; valeur et enseigne, cote a cote : c'est
	add	x0,x0,x25,lsl 1			; ce qu'on voit d'une carte recouverte
	add	x0,x0,x25
	mov	x27,x0
	lsr	x28,x22,5
	add	x28,x28,2			; x de la valeur
	add	x28,x28,x20
	mov	x1,x28
	ldv	x4,c_idx
	mov	x3,4
	mul	x2,x4,x3
	mov	x3,5
	udiv	x2,x2,x3
	add	x2,x2,x21
	add	x2,x2,3				; ligne de base
	mov	w3,w24
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x0,x27
	ldv	x1,c_idx
	adr	x2,ui_font_b
	bl	ui_text_width
	add	x2,x28,x0
	add	x2,x2,1
	ldv	x4,c_idx
	mov	x3,4
	mul	x3,x4,x3
	mov	x4,5
	udiv	x3,x3,x4
	add	x3,x3,x21
	add	x3,x3,3
	adr	x4,c_pip
	ldr	x4,[x4]
	sub	x3,x3,x4			; (assise sur la ligne de base)
	mov	x0,0
	mov	x1,x26
	mov	w4,w24
	mov	x5,0
	bl	ck_stamp
	bl	.turn				; le coin oppose
	cmp	x25,0
	b.eq	.ace
	cmp	x25,10
	b.hs	.face
	bl	.pips
	b	.end
.ace:	adr	x4,c_pip
	ldr	x4,[x4,16]
	sub	x2,x22,x4
	asr	x2,x2,1
	add	x2,x2,x20
	sub	x3,x23,x4
	asr	x3,x3,1
	add	x3,x3,x21
	mov	x0,2
	mov	x1,x26
	mov	w4,w24
	mov	x5,0
	bl	ck_stamp
	b	.end
.face:	bl	.figure
	b	.end
.back:	add	x0,x20,3			; le dos
	add	x1,x21,3
	sub	x2,x22,6
	sub	x3,x23,6
	mov	x4,3
	movl	w5,CK_BACK
	bl	ck_rbox
	lsr	x24,x22,4			; pas des points, au moins 4
	cmp	x24,4
	mov	x0,4
	csel	x24,x0,x24,lo
	lsr	x25,x24,1			; cote d'un point
	add	x26,x21,6			; y
	mov	x27,0				; ligne
.by:	add	x0,x21,x23
	sub	x0,x0,6
	sub	x0,x0,x25
	cmp	x26,x0
	b.gt	.bf
	add	x28,x20,6
	tbz	x27,0,.bx
	add	x28,x28,x25
.bx:	add	x0,x20,x22
	sub	x0,x0,6
	sub	x0,x0,x25
	cmp	x28,x0
	b.gt	.bn
	mov	x0,x28
	mov	x1,x26
	mov	x2,x25
	mov	x3,x25
	movl	w4,CK_BACK_DOT
	bl	ui_fill
	add	x28,x28,x24
	b	.bx
.bn:	add	x26,x26,x24
	add	x27,x27,1
	b	.by
.bf:	add	x0,x20,5
	add	x1,x21,5
	sub	x2,x22,10
	sub	x3,x23,10
	movl	w4,CK_BACK_LINE
	bl	ui_frame
.end:	bl	ui_canvas_end
	adr	x0,c_ready
	mov	w1,1
	strb	w1,[x0,x19]
	ldv	x0,t_px
	stv	x0,st_px
	ldv	x0,t_pitch
	stv	x0,st_pitch
	ldv	x0,cd_lay
	cbz	x0,.fin
	ldv	x0,t_cv
	bl	ui_canvas_begin
.fin:
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ck_render) recopie le coin de la valeur, retourne, au coin oppose
.turn:	ldv	x0,c_sheet_px
	ldv	x1,c_sheet_pitch
	lsl	x1,x1,2				; pas en octets
	add	x2,x21,2
	madd	x2,x2,x1,x0
	add	x3,x20,2
	add	x2,x2,x3,lsl 2			; source
	add	x3,x21,x23
	sub	x3,x3,3
	madd	x3,x3,x1,x0
	add	x4,x20,x22
	sub	x4,x4,3
	add	x3,x3,x4,lsl 2			; destination
	mov	x4,3
	mul	x4,x22,x4
	mov	x5,5
	udiv	x4,x4,x5			; largeur
	ldv	x5,c_ih				; hauteur
.tr:	mov	x6,0
.tc:	ldr	w7,[x2,x6,lsl 2]
	neg	x8,x6
	str	w7,[x3,x8,lsl 2]
	add	x6,x6,1
	cmp	x6,x4
	b.lo	.tc
	add	x2,x2,x1
	sub	x3,x3,x1
	subs	x5,x5,1
	b.ne	.tr
	ret

; (ck_render) les enseignes d'une carte de 2 a 10
.pips:	prologue
	stp	x19,x25,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	adr	x27,c_pip
	ldr	x27,[x27,8]			; cote
	adr	x28,pip_lay
	sub	x0,x25,1
	mov	x1,PIP_LAY_ROW
	mul	x0,x0,x1
	add	x28,x28,x0
	add	x25,x25,1			; nombre
.pl:	cbz	x25,.pe
	ldrb	w0,[x28],1			; colonne
	mov	x1,27
	mul	x1,x22,x1
	mov	x2,100
	udiv	x1,x1,x2
	sub	x0,x0,1
	mul	x0,x0,x1
	add	x2,x20,x22,lsr 1
	add	x2,x2,x0
	sub	x2,x2,x27,lsr 1
	ldrb	w5,[x28],1			; ligne
	ldv	x3,c_ih
	add	x3,x3,3				; premiere ligne
	lsl	x4,x3,1
	add	x4,x4,x27
	sub	x4,x23,x4			; hauteur
	mul	x4,x4,x5
	mov	x0,6
	udiv	x4,x4,x0
	add	x3,x3,x4
	add	x3,x3,x21
	cmp	x5,3
	cset	x5,hi				; renversee en bas
	mov	x0,1
	mov	x1,x26
	mov	w4,w24
	bl	ck_stamp
	sub	x25,x25,1
	b	.pl
.pe:	ldp	x27,x28,[sp],16
	ldp	x19,x25,[sp],16
	epilogue

; (ck_render) valet, dame, roi : un cadre, la lettre, deux enseignes
.figure:
	prologue
	stp	x19,x25,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	lsr	x27,x22,3			; marge
	ldv	x28,c_ih
	add	x28,x28,3
	add	x0,x20,x27
	add	x1,x21,x28
	sub	x2,x22,x27,lsl 1
	sub	x3,x23,x28,lsl 1
	movl	w4,CK_TINT_B
	movl	w5,CK_RED
	cmp	w24,w5
	b.ne	.ft
	movl	w4,CK_TINT_R
.ft:	bl	ui_fill
	add	x0,x20,x27
	add	x1,x21,x28
	sub	x2,x22,x27,lsl 1
	sub	x3,x23,x28,lsl 1
	mov	w4,w24
	bl	ui_frame
	adr	x19,c_pip
	ldr	x19,[x19,8]
	add	x2,x20,x27			; enseigne en haut a gauche du cadre...
	add	x2,x2,2
	add	x3,x21,x28
	add	x3,x3,2
	mov	x0,1
	mov	x1,x26
	mov	w4,w24
	mov	x5,0
	bl	ck_stamp
	add	x2,x20,x22			; ... et renversee en bas a droite
	sub	x2,x2,x27
	sub	x2,x2,2
	sub	x2,x2,x19
	add	x3,x21,x23
	sub	x3,x3,x28
	sub	x3,x3,2
	sub	x3,x3,x19
	mov	x0,1
	mov	x1,x26
	mov	w4,w24
	mov	x5,1
	bl	ck_stamp
	sub	x19,x23,x28,lsl 1		; la lettre
	mov	x0,5
	lsl	x19,x19,1
	udiv	x19,x19,x0			; corps
	adr	x0,s_ranks
	add	x0,x0,x25,lsl 1
	add	x0,x0,x25
	mov	x1,x19
	ldv	x2,c_serif
	bl	ui_text_width
	sub	x1,x22,x0
	asr	x1,x1,1
	add	x1,x1,x20
	adr	x0,s_ranks
	add	x0,x0,x25,lsl 1
	add	x0,x0,x25
	mov	x2,3
	mul	x2,x19,x2
	lsr	x2,x2,3
	add	x2,x2,x23,lsr 1
	add	x2,x2,x21
	mov	w3,w24
	mov	x4,x19
	ldv	x5,c_serif
	mov	x6,0
	bl	ui_text
	ldp	x27,x28,[sp],16
	ldp	x19,x25,[sp],16
	epilogue

; x0 = carte, x1, x2 = coin : la pose dans la couche courante (cd_*), coins
; arrondis, decoupee
card_blit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x0
	adr	x1,c_ready
	ldrb	w1,[x1,x0]
	cbnz	w1,.ok
	bl	ck_render
.ok:	ldv	x22,c_w
	ldv	x23,c_h
	mov	x0,13
	udiv	x1,x21,x0
	msub	x2,x1,x0,x21
	mul	x2,x2,x22
	mul	x1,x1,x23
	ldv	x3,c_sheet_pitch
	madd	x1,x1,x3,x2
	ldv	x0,c_sheet_px
	add	x24,x0,x1,lsl 2			; source
	neg	x0,x19				; colonnes a, b de la carte
	cmp	x0,0
	csel	x25,x0,xzr,gt
	ldv	x0,cd_w
	sub	x0,x0,x19
	cmp	x0,x22
	csel	x26,x0,x22,lt
	cmp	x25,x26
	b.ge	.out
	ldv	x28,cd_h
	mov	x27,0				; ligne
.row:	add	x0,x20,x27
	cmp	x0,0
	b.lt	.next
	cmp	x0,x28
	b.ge	.out
	ldv	x1,cd_pitch
	mul	x1,x1,x0
	add	x1,x1,x19
	ldv	x2,cd_px
	add	x1,x2,x1,lsl 2			; (colonne 0 de la carte)
	mov	x0,x24
	ldv	x2,c_r
	sub	x3,x23,1
	sub	x3,x3,x27
	cmp	x27,x3
	csel	x3,x27,x3,lo			; distance au bord
	cmp	x3,x2
	b.lo	.corner
	add	x4,x0,x25,lsl 2
	add	x5,x1,x25,lsl 2
	sub	x6,x26,x25
.cp8:	cmp	x6,2
	b.lo	.cp1
	ldr	x7,[x4],8
	str	x7,[x5],8
	sub	x6,x6,2
	b	.cp8
.cp1:	cbz	x6,.next
	ldr	w7,[x4]
	str	w7,[x5]
	b	.next
.corner:
	adr	x4,c_corner
	add	x4,x4,x3,lsl 5
	mov	x5,x25
.cc:	cmp	x5,x26
	b.hs	.next
	sub	x6,x22,1
	sub	x6,x6,x5
	cmp	x5,x6
	csel	x6,x5,x6,lo
	mov	w7,255
	cmp	x6,x2
	b.hs	.op
	ldrb	w7,[x4,x6]
.op:	cbz	w7,.nc
	ldr	w8,[x0,x5,lsl 2]
	cmp	w7,255
	b.hs	.st
	ldr	w13,[x1,x5,lsl 2]
	px_mix	w13,w8,w7
	mov	w8,w13
.st:	str	w8,[x1,x5,lsl 2]
.nc:	add	x5,x5,1
	b	.cc
.next:	ldv	x0,c_sheet_pitch
	add	x24,x24,x0,lsl 2
	add	x27,x27,1
	cmp	x27,x23
	b.lo	.row
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = coin, x2 = enseigne indiquee (-1 : aucune) : une place vide, sur la
; couche T, cible active
card_slot:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldv	x2,c_w
	ldv	x3,c_h
	ldv	x4,c_r
	movl	w5,TAPIS_SLOT_L
	bl	ck_rbox
	add	x0,x19,1
	add	x1,x20,1
	ldv	x2,c_w
	sub	x2,x2,2
	ldv	x3,c_h
	sub	x3,x3,2
	ldv	x4,c_r
	sub	x4,x4,1
	movl	w5,TAPIS_SLOT
	bl	ck_rbox
	tbnz	x21,63,.out
	adr	x4,c_pip
	ldr	x4,[x4,16]
	ldv	x2,c_w
	sub	x2,x2,x4
	asr	x2,x2,1
	add	x2,x2,x19
	ldv	x3,c_h
	sub	x3,x3,x4
	asr	x3,x3,1
	add	x3,x3,x20
	mov	x0,2
	mov	x1,x21
	movl	w4,TAPIS_HINT
	mov	x5,0
	bl	ck_stamp
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = largeur, x1 = hauteur de la fenetre : les couches suivent (elles ne
; font que grandir)
layers_size:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	stv	x0,g_w
	stv	x1,g_h
	ldv	x2,t_pitch
	ldv	x3,t_cap_h
	cmp	x0,x2
	b.hi	.grow
	cmp	x1,x3
	b.ls	.set
.grow:	cmp	x19,x2
	csel	x19,x2,x19,lo
	cmp	x20,x3
	csel	x20,x3,x20,lo
	ldv	x0,t_cv
	cbz	x0,.new
	bl	ui_w_destroy
	ldv	x0,k_cv
	bl	ui_w_destroy
.new:	mov	x0,x19
	mov	x1,x20
	bl	ck_canvas
	stv	x0,t_cv
	bl	ui_canvas_pixels
	stv	x0,t_px
	stv	x1,t_pitch
	stv	x2,t_cap_h
	mov	x0,x19
	mov	x1,x20
	bl	ck_canvas
	stv	x0,k_cv
	bl	ui_canvas_pixels
	stv	x0,k_px
.set:	bl	ck_use_t
	ldp	x19,x20,[sp],16
	epilogue

; les cartes vont dans la couche T
ck_use_t:
	ldv	x0,t_px
	stv	x0,cd_px
	stv	x0,st_px
	ldv	x0,t_pitch
	stv	x0,cd_pitch
	stv	x0,st_pitch
	ldv	x0,g_w
	stv	x0,cd_w
	ldv	x0,g_h
	stv	x0,cd_h
	ret

; les dessins de UIKit vont dans la couche T
lay_begin:
	prologue
	ldv	x0,t_cv
	bl	ui_canvas_begin
	bl	ck_use_t
	mov	x0,1
	stv	x0,cd_lay
	epilogue

lay_end:
	stv	xzr,cd_lay
	b	ui_canvas_end

; x0 = destination, x1 = son pas, x2 = source, x3 = son pas (pixels), x4, x5 =
; coin, x6, x7 = taille
ck_copy:
	madd	x8,x5,x1,x4
	add	x0,x0,x8,lsl 2
	madd	x8,x5,x3,x4
	add	x2,x2,x8,lsl 2
	lsl	x1,x1,2
	lsl	x3,x3,2
.r:	mov	x8,x0
	mov	x9,x2
	mov	x10,x6
.q:	cmp	x10,8
	b.lo	.s
	ldp	x11,x12,[x9],16
	ldp	x13,x14,[x9],16
	stp	x11,x12,[x8],16
	stp	x13,x14,[x8],16
	sub	x10,x10,8
	b	.q
.s:	cbz	x10,.n
	ldr	w11,[x9],4
	str	w11,[x8],4
	sub	x10,x10,1
	b	.s
.n:	add	x0,x0,x1
	add	x2,x2,x3
	subs	x7,x7,1
	b.ne	.r
	ret

; x0..x3 = rectangle : T, et les cartes glissees par-dessus, montres d'un coup
compose:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	add	x2,x0,x2
	add	x3,x1,x3
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	ldv	x4,g_w
	cmp	x2,x4
	csel	x2,x4,x2,gt
	ldv	x4,g_h
	cmp	x3,x4
	csel	x3,x4,x3,gt
	subs	x21,x2,x0
	b.le	.out
	subs	x22,x3,x1
	b.le	.out
	mov	x19,x0
	mov	x20,x1
	ldv	x0,dr_on
	cbz	x0,.t
	ldv	x0,k_px
	ldv	x1,t_pitch
	ldv	x2,t_px
	mov	x3,x1
	mov	x4,x19
	mov	x5,x20
	mov	x6,x21
	mov	x7,x22
	bl	ck_copy
	ldv	x0,k_px
	stv	x0,cd_px
	stp	x23,x24,[sp,-16]!
	mov	x23,0
	ldv	x24,dr_y
.c:	ldv	x0,dr_n
	cmp	x23,x0
	b.hs	.cd
	adr	x0,dr_cards
	ldrb	w0,[x0,x23]
	ldv	x1,dr_x
	mov	x2,x24
	bl	card_blit
	ldv	x0,dr_dy
	add	x24,x24,x0
	add	x23,x23,1
	b	.c
.cd:	ldp	x23,x24,[sp],16
	ldv	x0,t_px
	stv	x0,cd_px
	ldv	x2,k_px
	b	.show
.t:	ldv	x2,t_px
.show:	ldv	x0,ui_surf
	ldv	x1,ui_w
	ldv	x3,t_pitch
	mov	x4,x19
	mov	x5,x20
	mov	x6,x21
	mov	x7,x22
	bl	ck_copy
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	bl	ui_update
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; toute la fenetre
compose_all:
	mov	x0,0
	mov	x1,0
	ldv	x2,g_w
	ldv	x3,g_h
	b	compose

; x0, x1 = nouveau coin des cartes glissees : seul ce qui change est refait
drag_move:
	prologue
	ldv	x2,dr_dy
	ldv	x3,dr_n
	sub	x3,x3,1
	mul	x2,x2,x3
	ldv	x3,c_h
	add	x3,x3,x2			; hauteur du paquet
	ldv	x2,c_w
	ldv	x4,dr_x				; union avec l'ancienne place
	ldv	x5,dr_y
	stv	x0,dr_x
	stv	x1,dr_y
	cmp	x0,x4
	csel	x6,x4,x0,gt			; gauche
	cmp	x1,x5
	csel	x7,x5,x1,gt			; haut
	cmp	x0,x4
	csel	x8,x0,x4,gt			; droite
	add	x8,x8,x2
	cmp	x1,x5
	csel	x9,x1,x5,gt			; bas
	add	x9,x9,x3
	mov	x0,x6
	mov	x1,x7
	sub	x2,x8,x6
	sub	x3,x9,x7
	bl	compose
	epilogue

; x0 = texte : la table et le bandeau, sur la couche T (cible active) ; w1 =
; couleur du texte
table_bg:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	mov	x0,0
	mov	x1,BAND_H
	ldv	x2,g_w
	ldv	x3,g_h
	sub	x3,x3,BAND_H
	mov	x4,0
	movl	w5,TAPIS_HAUT
	movl	w6,TAPIS_BAS
	bl	ui_rbox
	mov	x0,x19
	mov	w1,w20
	bl	band_draw
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, w1 = sa couleur : le bandeau (boutons de band_set)
band_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	mov	x0,0
	mov	x1,0
	ldv	x2,g_w
	mov	x3,BAND_H
	movl	w4,TAPIS_BAND
	bl	ui_fill
	mov	x21,0
	ldv	x22,bb_s
.b:	ldv	x0,bb_n
	cmp	x21,x0
	b.hs	.t
	adr	x0,bb_x
	ldr	x1,[x0,x21,lsl 3]
	mov	x0,x1
	mov	x1,(BAND_H-BAND_BTN_H)/2
	adr	x2,bb_w
	ldr	x2,[x2,x21,lsl 3]
	mov	x3,BAND_BTN_H
	mov	x4,6
	movl	w5,TAPIS_BTN
	mov	w6,w5
	bl	ui_rbox
	mov	x0,x22
	bl	ui_tr
	str	x0,[sp,-16]!
	adr	x0,bb_x
	ldr	x1,[x0,x21,lsl 3]
	add	x1,x1,10
	ldr	x0,[sp],16
	mov	x2,BAND_H/2+5
	movl	w3,TAPIS_TEXT
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.s:	ldrb	w0,[x22],1
	cbnz	w0,.s
	add	x21,x21,1
	b	.b
.t:	ldv	x1,bb_end
	add	x1,x1,10
	ldv	x6,g_w
	sub	x6,x6,x1
	sub	x6,x6,FRAME_CAP_W+8
	cmp	x6,8
	b.lt	.out
	mov	x0,x19
	mov	x2,BAND_H/2+5
	mov	w3,w20
	mov	x4,14
	adr	x5,ui_font
	bl	ui_text_fit
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = libelles (chaines a la suite, un 0 de plus a la fin) : boutons du bandeau
band_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	stv	x0,bb_s
	mov	x20,0
	mov	x21,8				; x
.l:	ldrb	w0,[x19]
	cbz	w0,.e
	mov	x0,x19
	bl	ui_tr
	mov	x1,14
	adr	x2,ui_font
	bl	ui_text_width
	add	x0,x0,20
	adr	x1,bb_x
	str	x21,[x1,x20,lsl 3]
	adr	x1,bb_w
	str	x0,[x1,x20,lsl 3]
	add	x21,x21,x0
	add	x21,x21,6
	add	x20,x20,1
.s:	ldrb	w0,[x19],1
	cbnz	w0,.s
	b	.l
.e:	stv	x20,bb_n
	stv	x21,bb_end
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x -> x0 = bouton du bandeau sous x (-1 : aucun, 1 de plus dans x1 si la
; place est libre : un appui y deplace la fenetre)
band_hit:
	mov	x3,0
.b:	ldv	x4,bb_n
	cmp	x3,x4
	b.hs	.none
	adr	x4,bb_x
	ldr	x4,[x4,x3,lsl 3]
	adr	x5,bb_w
	ldr	x5,[x5,x3,lsl 3]
	cmp	x0,x4
	b.lt	.n
	add	x4,x4,x5
	cmp	x0,x4
	b.lt	.hit
.n:	add	x3,x3,1
	b	.b
.hit:	mov	x0,x3
	mov	x1,0
	ret
.none:	ldv	x1,g_w
	sub	x1,x1,FRAME_CAP_W
	cmp	x0,x1
	cset	x1,lt
	mov	x0,-1
	ret

; x0 = titre, x1 = texte : le message de fin, sur T (cible active), couleurs du
; theme
win_box:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	ui_tr
	mov	x19,x0
	mov	x0,x20
	bl	ui_tr
	mov	x20,x0
	bl	ui_jetons
	mov	x24,x0
	mov	x0,x20
	mov	x1,15
	adr	x2,ui_font
	bl	ui_text_width
	add	x21,x0,56			; largeur
	mov	x0,x19
	mov	x1,22
	adr	x2,ui_font_b
	bl	ui_text_width
	add	x0,x0,56
	cmp	x0,x21
	csel	x21,x0,x21,gt
	ldv	x23,g_w
	sub	x23,x23,x21
	asr	x23,x23,1			; x
	ldv	x22,g_h
	sub	x22,x22,104
	asr	x22,x22,1			; y
	mov	x0,x23
	mov	x1,x22
	mov	x2,x21
	mov	x3,104
	mov	x4,12
	ldr	w5,[x24,TJ_SURFACE]
	bl	ck_rbox
	mov	x0,x19
	mov	x1,22
	adr	x2,ui_font_b
	bl	ui_text_width
	sub	x1,x21,x0
	asr	x1,x1,1
	add	x1,x1,x23
	mov	x0,x19
	add	x2,x22,44
	ldr	w3,[x24,TJ_TEXTE1]
	mov	x4,22
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x0,x20
	mov	x1,15
	adr	x2,ui_font
	bl	ui_text_width
	sub	x1,x21,x0
	asr	x1,x1,1
	add	x1,x1,x23
	mov	x0,x20
	add	x2,x22,76
	ldr	w3,[x24,TJ_TEXTE2]
	mov	x4,15
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les quatre fondations (wa_fx rempli, y en x0) : les cartes vont rebondir
win_start:
	prologue
	stv	x0,wa_fy
	stv	xzr,wa_next
	stv	xzr,wa_fly
	mov	x0,1
	stv	x0,win_on
	epilogue

; un pas de l'animation, sur la couche T (les traces restent) ->
; x0 = 0 : finie
win_step:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	ck_use_t
	mov	x19,3
.k:	ldv	x0,wa_fly
	cbnz	x0,.go
	ldv	x20,wa_next
	cmp	x20,52
	b.hs	.done
	and	x1,x20,3			; enseigne
	lsr	x2,x20,2
	mov	x3,12
	sub	x2,x3,x2			; rang
	mov	x3,13
	madd	x0,x1,x3,x2
	stv	x0,wa_card
	adr	x0,wa_fx
	ldr	x0,[x0,x1,lsl 3]
	lsl	x0,x0,4
	stv	x0,wa_x
	ldv	x0,wa_fy
	lsl	x0,x0,4
	stv	x0,wa_y
	mov	x0,60
	bl	ck_rnd_n
	add	x21,x0,40
	bl	ck_rnd
	tbz	x0,0,.side
	neg	x21,x21
.side:	stv	x21,wa_vx
	mov	x0,80
	bl	ck_rnd_n
	neg	x0,x0
	stv	x0,wa_vy
	mov	x0,1
	stv	x0,wa_fly
	add	x20,x20,1
	stv	x20,wa_next
.go:	ldv	x20,wa_x			; le pas
	ldv	x0,wa_vx
	ldv	x1,c_w
	mul	x0,x0,x1
	asr	x0,x0,6
	add	x20,x20,x0
	stv	x20,wa_x
	ldv	x21,wa_y
	ldv	x22,wa_vy
	add	x21,x21,x22
	add	x22,x22,12
	ldv	x0,g_h
	ldv	x1,c_h
	sub	x0,x0,x1
	cmp	x21,x0,lsl 4
	b.le	.nb
	lsl	x21,x0,4			; rebond
	neg	x22,x22
	mov	x0,3
	mul	x22,x22,x0
	asr	x22,x22,2
	cmn	x22,60
	b.lt	.nb
	mov	x0,40
	bl	ck_rnd_n
	mov	x1,-60
	sub	x22,x1,x0
.nb:	stv	x21,wa_y
	stv	x22,wa_vy
	ldv	x0,wa_card
	asr	x1,x20,4
	asr	x2,x21,4
	bl	card_blit
	asr	x0,x20,4
	asr	x1,x21,4
	ldv	x2,c_w
	ldv	x3,c_h
	bl	compose
	asr	x0,x20,4			; sortie de la table ?
	ldv	x1,c_w
	add	x1,x0,x1
	cmp	x1,0
	b.lt	.off
	ldv	x1,g_w
	cmp	x0,x1
	b.le	.nk
.off:	stv	xzr,wa_fly
.nk:	subs	x19,x19,1
	b.ne	.k
	mov	x0,1
	b	.out
.done:	stv	xzr,win_on
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source, x2 = octets (multiple de 8)
ck_move8:
	ldr	x3,[x1],8
	str	x3,[x0],8
	subs	x2,x2,8
	b.ne	ck_move8
	ret

; formes des enseignes (1 disque : x, y, r ; 2 polygone : n, points ; 0 fin),
; sur 256 x 256 ; trefle, carreau, coeur, pique
suit_data:
sd_c:	db 1,128,70,60, 1,66,150,60, 1,190,150,60
	db 2,4, 128,100, 180,150, 128,190, 76,150
	db 2,5, 128,130, 114,212, 84,250, 172,250, 142,212
	db 0
sd_d:	db 2,8, 128,4, 172,74, 236,128, 172,182, 128,252, 84,182, 20,128, 84,74
	db 0
sd_h:	db 1,74,78,70, 1,182,78,70
	db 2,8, 8,92, 22,140, 72,196, 128,250, 184,196, 234,140, 248,92, 128,60
	db 0
sd_s:	db 1,72,156,64, 1,184,156,64
	db 2,7, 128,4, 176,62, 232,112, 248,160, 8,160, 24,112, 80,62
	db 2,5, 128,150, 114,214, 84,250, 172,250, 142,214
	db 0
suit_ofs:
	db sd_c-suit_data, sd_d-suit_data, sd_h-suit_data, sd_s-suit_data

; valeurs : 3 octets chacune (jeu francais : valet, dame, roi)
s_ranks	db 'A',0,0,'2',0,0,'3',0,0,'4',0,0,'5',0,0,'6',0,0,'7',0,0,'8',0,0
	db '9',0,0,'10',0,'V',0,0,'D',0,0,'R',0,0

; enseignes d'une carte de 2 a 10 : colonne 0..2, ligne 0..6
PIP_LAY_ROW	= 20
pip_lay:
	db 1,0, 1,6, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	db 1,0, 1,3, 1,6, 0,0,0,0,0,0,0,0,0,0,0,0,0,0
	db 0,0, 2,0, 0,6, 2,6, 0,0,0,0,0,0,0,0,0,0,0,0
	db 0,0, 2,0, 1,3, 0,6, 2,6, 0,0,0,0,0,0,0,0,0,0
	db 0,0, 2,0, 0,3, 2,3, 0,6, 2,6, 0,0,0,0,0,0,0,0
	db 0,0, 2,0, 1,1, 0,3, 2,3, 0,6, 2,6, 0,0,0,0,0,0
	db 0,0, 2,0, 1,1, 0,3, 2,3, 1,5, 0,6, 2,6, 0,0,0,0
	db 0,0, 2,0, 0,2, 2,2, 1,3, 0,4, 2,4, 0,6, 2,6, 0,0
	db 0,0, 2,0, 1,1, 0,2, 2,2, 0,4, 2,4, 1,5, 0,6, 2,6
	align 4

macro cards_data {
		align 8
c_mask		dd 0
c_maskpx	dd 0
c_serif		dd 0
c_sheet		dd 0
c_sheet_px	dd 0
c_sheet_pitch	dd 0
c_sheet_w	dd 0
c_sheet_h	dd 0
c_w		dd 0
c_h		dd 0
c_r		dd 0
c_idx		dd 0
c_ih		dd 0
c_dy_max	dd 0
c_dy_min	dd 0
c_pip		rd 3
l_gap		dd 0
l_ox		dd 0
l_top		dd 0
l_tab		dd 0
g_w		dd 0
g_h		dd 0
t_cv		dd 0
t_px		dd 0
t_pitch		dd 0
t_cap_h		dd 0
k_cv		dd 0
k_px		dd 0
cd_px		dd 0
cd_pitch	dd 0
cd_w		dd 0
cd_h		dd 0
cd_lay		dd 0
st_px		dd 0
st_pitch	dd 0
dr_on		dd 0
dr_n		dd 0
dr_dy		dd 0
dr_x		dd 0
dr_y		dd 0
bb_n		dd 0
bb_end		dd 0
bb_s		dd 0
bb_x		rd BAND_MAX
bb_w		rd BAND_MAX
win_on		dd 0
wa_fx		rd 4
wa_fy		dd 0
wa_next		dd 0
wa_fly		dd 0
wa_card		dd 0
wa_x		dd 0
wa_y		dd 0
wa_vx		dd 0
wa_vy		dd 0
ck_seed		dw 1
		align 8
dr_cards	rb 56
c_ready		rb 56
c_corner	rb CK_CORNER*CK_CORNER
		align 8
}
