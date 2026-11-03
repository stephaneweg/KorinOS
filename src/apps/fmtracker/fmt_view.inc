; ============================================================================
;  fmt_view.inc - FM Tracker : dessin de la fenetre (grille, en-tetes, liste
;  des motifs, piano, barre d'etat), souris et clavier de ces zones
;
;  Ces zones sont dessinees a la main dans la surface ; les boutons, les nombres
;  et la barre de defilement sont des widgets. flush repeint ce que g_redraw
;  demande (macro mark).
; ============================================================================

; x0 = dest, x1 = format (%d), x2, x3 = valeurs
fmt:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
.l:	ldrb	w0,[x20],1
	cbz	w0,.e
	cmp	w0,'%'
	b.ne	.c
	ldrb	w1,[x20]
	cmp	w1,'d'
	b.ne	.c
	add	x20,x20,1
	mov	x0,x21
	mov	x1,x19
	bl	fmt_dec
	mov	x19,x0
	mov	x21,x22
	b	.l
.c:	strb	w0,[x19],1
	b	.l
.e:	strb	wzr,[x19]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = x, x2 = ligne de base, w3 = couleur : 13 px
txt:
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	b	ui_text

; idem, gras
txtb:
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,0
	b	ui_text

; x0 = fond, x1 = x, x2 = y, x3 = largeur, x4 = hauteur, x5 = rayon, w6 = fond
; (cadre LIGNE)
sunken:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	mov	x22,x4
	mov	x23,x5
	mov	w24,w6
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	mov	x4,x23
	ldc	w5,c_ln
	mov	w6,w5
	bl	ui_rbox
	add	x0,x19,1
	add	x1,x20,1
	sub	x2,x21,2
	sub	x3,x22,2
	sub	x4,x23,1
	mov	w5,w24
	mov	w6,w24
	bl	ui_rbox
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les couleurs d'apres le theme
colors_init:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	ui_jetons
	mov	x19,x0
	ldr	w0,[x19,TJ_FACE]
	stc	w0,c_face
	ldr	w0,[x19,TJ_SURFACE]
	stc	w0,c_field
	ldr	w0,[x19,TJ_TEXTE1]
	stc	w0,c_text
	ldr	w0,[x19,TJ_TEXTE2]
	stc	w0,c_text2
	ldr	w0,[x19,TJ_LIGNE]
	stc	w0,c_ln
	ldr	w0,[x19,TJ_ACCENT]
	stc	w0,c_acc
	ldr	w0,[x19,TJ_CREUX]
	stc	w0,c_hollow
	ldr	w0,[x19,TJ_ACCENT]
	bl	ui_ink
	stc	w0,c_acc_ink
	mov	x1,26				; teintes du fond de la grille
	bl	f_mix
	stc	w0,c_gline
	mov	x1,14
	bl	f_mix
	stc	w0,c_row4
	mov	x1,30
	bl	f_mix
	stc	w0,c_row16
	mov	x1,120
	bl	f_mix
	stc	w0,c_gnum
	mov	x1,76
	bl	f_mix
	stc	w0,c_cont
	mov	x1,70
	bl	f_mix
	stc	w0,c_gmute
	ldc	w0,c_field
	ldc	w1,c_face
	mov	x2,150
	bl	ui_mix
	stc	w0,c_gutter
	ldc	w0,c_field
	movl	w1,PLAY_COL
	mov	x2,96
	bl	ui_mix
	stc	w0,c_playbg
	ldc	w0,c_gutter
	movl	w1,PLAY_COL
	mov	x2,90
	bl	ui_mix
	stc	w0,c_gplay
	ldc	w0,c_gutter
	ldc	w1,c_acc
	mov	x2,90
	bl	ui_mix
	stc	w0,c_gcur
	ldc	w0,c_face
	ldc	w1,c_acc
	mov	x2,46
	bl	ui_mix
	stc	w0,c_headcur
	ldp	x19,x20,[sp],16
	epilogue

; x1 = 0..256 -> w0 = teinte : fond de la grille vers son texte
f_mix:
	mov	x2,x1
	ldc	w0,c_field
	ldc	w1,c_text
	b	ui_mix

; ---- le fond et le panneau de gauche ---------------------------------------

; x0 = chaine originale, x1 = y (haut du titre) : titre de section
heading:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	bl	ui_tr
	mov	x1,10
	add	x2,x19,13
	ldc	w3,c_text2
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	ldr	x19,[sp],16
	epilogue

; x0 = chaine originale, x1 = y (haut du nombre) : libelle a gauche d'un nombre
field_label:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	bl	ui_tr
	mov	x1,10
	add	x2,x19,19
	ldc	w3,c_text
	bl	txt
	ldr	x19,[sp],16
	epilogue

paint_side:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,s_h_patterns
	mov	x1,TOOL_H+8
	bl	heading
	ldv	x19,g_side_y
	adr	x0,s_h_this
	mov	x1,x19
	bl	heading
	adr	x0,s_rows
	add	x1,x19,SD_ROWS
	bl	field_label
	adr	x0,s_speed
	add	x1,x19,SD_SPEED
	bl	field_label
	adr	x0,s_h_typing
	add	x1,x19,SD_HTYPE
	bl	heading
	adr	x0,s_octave
	add	x1,x19,SD_OCT
	bl	field_label
	adr	x0,s_step
	add	x1,x19,SD_STEP
	bl	field_label
	adr	x0,s_h_song
	add	x1,x19,SD_HSONG
	bl	heading
	ldp	x19,x20,[sp],16
	epilogue

; le titre et l'auteur du morceau
draw_song_labels:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x19,g_side_y
	add	x19,x19,SD_TITLE
	mov	x0,10
	mov	x1,x19
	mov	x2,SIDE_W-14
	mov	x3,40
	ldc	w4,c_face
	bl	ui_fill
	adr	x0,g_title
	ldrb	w1,[x0]
	cbnz	w1,.t
	adr	x0,g_path			; sans titre : le nom du fichier
	ldrb	w1,[x0]
	cbz	w1,.u
	bl	base_name
	b	.t
.u:	adr	x0,s_untitled
	bl	ui_tr
.t:	mov	x1,12
	add	x2,x19,14
	ldc	w3,c_text
	mov	x4,13
	adr	x5,ui_font
	mov	x6,SIDE_W-22
	bl	ui_text_fit
	adr	x0,g_author
	mov	x1,12
	add	x2,x19,34
	ldc	w3,c_text2
	mov	x4,13
	adr	x5,ui_font
	mov	x6,SIDE_W-22
	bl	ui_text_fit
	mov	x0,0
	mov	x1,x19
	mov	x2,SIDE_W
	mov	x3,40
	bl	ui_update
	ldp	x19,x20,[sp],16
	epilogue

; tout le fond, les widgets
paint_all:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	colors_init
	adr	x0,g_w
	ldp	x19,x20,[x0]
	mov	x0,0
	mov	x1,0
	mov	x2,x19
	mov	x3,x20
	ldc	w4,c_face
	bl	ui_fill
	mov	x0,0				; trait sous la barre, bord du panneau
	mov	x1,TOOL_H-1
	mov	x2,x19
	mov	x3,1
	ldc	w4,c_ln
	bl	ui_fill
	mov	x0,SIDE_W-1
	mov	x1,TOOL_H
	mov	x2,1
	sub	x3,x20,TOOL_H+ST_H
	ldc	w4,c_ln
	bl	ui_fill
	bl	paint_side
	mov	x0,0
	bl	ui_w_draw
	ldp	x19,x20,[sp],16
	epilogue

; ---- la position -----------------------------------------------------------

; x0 = dest, x1 = valeur -> x0 = fin : au moins deux chiffres
put_2d:
	prologue
	cmp	x1,10
	b.hs	.d
	mov	w2,'0'
	strb	w2,[x0],1
.d:	mov	x2,x0
	mov	x0,x1
	mov	x1,x2
	bl	fmt_dec
	epilogue

; l'afficheur : motif:ligne temps
update_lcd:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x19,g_pat			; motif, ligne
	ldv	x20,g_row
	ldv	x0,g_playing
	cbz	x0,.pos
	ldv	x19,g_play_pat
	ldv	x20,g_shown_row
	tbz	x20,63,.pos
	mov	x20,0
.pos:	ldv	x0,g_npat
	cmp	x19,x0
	b.lo	.t
	sub	x19,x0,1
.t:	mov	x21,0				; centiemes
	mov	x22,0
.l:	cmp	x22,x19
	b.hs	.cur
	mov	x0,x22
	bl	pat_ptr
	ldr	x1,[x0,PT_ROWS]
	ldr	x2,[x0,PT_SPEED]
	mul	x1,x1,x2
	mov	x2,5
	mul	x1,x1,x2
	add	x21,x21,x1
	add	x22,x22,1
	b	.l
.cur:	mov	x0,x19
	bl	pat_ptr
	ldr	x2,[x0,PT_SPEED]
	mul	x1,x20,x2
	mov	x2,5
	mul	x1,x1,x2
	add	x21,x21,x1
	adr	x0,g_lcdtxt
	add	x1,x19,1
	bl	put_2d
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,x20
	cmp	x1,100
	b.hs	.r3
	mov	w2,'0'
	strb	w2,[x0],1
	cmp	x1,10
	b.hs	.r3
	strb	w2,[x0],1
.r3:	mov	x2,x0
	mov	x0,x1
	mov	x1,x2
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,6000				; minutes
	udiv	x1,x21,x1
	mov	x2,x0
	mov	x0,x1
	mov	x1,x2
	bl	fmt_dec
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,100				; secondes
	udiv	x1,x21,x1
	mov	x2,60
	udiv	x3,x1,x2
	msub	x1,x3,x2,x1
	bl	put_2d
	mov	w1,'.'
	strb	w1,[x0],1
	mov	x1,10				; dixiemes
	udiv	x2,x21,x1
	udiv	x3,x2,x1
	msub	x2,x3,x1,x2
	add	w2,w2,'0'
	strb	w2,[x0],1
	strb	wzr,[x0]
	ldv	x0,w_lcd
	adr	x1,g_lcdtxt
	bl	ui_w_set_text
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---- en-tetes des canaux ---------------------------------------------------

; x0 = x, x1 = pastille (texte), w2 = couleur si allumee, x3 = 1 allumee, x4 = survolee
head_pill:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x3
	mov	w22,w2
	cbz	x3,.off
	mov	w5,w22
	mov	w6,w22
	b	.box
.off:	ldc	w5,c_hollow
	mov	w6,w5
	cbz	x4,.box
	ldc	w5,c_ln
	mov	w6,w5
.box:	mov	x0,x19
	mov	x1,TOOL_H+26
	mov	x2,20
	mov	x3,16
	mov	x4,4
	bl	ui_rbox
	mov	x0,x20
	mov	x1,12
	adr	x2,ui_font_b
	bl	ui_text_width
	mov	x1,20
	sub	x1,x1,x0
	asr	x1,x1,1
	add	x1,x19,x1
	mov	x0,x20
	mov	x2,TOOL_H+38
	ldc	w3,c_text
	cbz	x21,.t
	movl	w3,0x00FFFFFF
.t:	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = canal
draw_head:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldv	x21,g_col_w
	mul	x20,x19,x21
	add	x20,x20,SIDE_W+NUM_W		; x
	ldc	w22,c_face
	ldv	x0,g_ch
	cmp	x0,x19
	b.ne	.bg
	ldc	w22,c_headcur
.bg:	mov	x0,x20
	mov	x1,TOOL_H
	mov	x2,x21
	mov	x3,HEAD_H
	mov	w4,w22
	bl	ui_fill
	mov	x0,x20				; la couleur du canal
	mov	x1,TOOL_H
	sub	x2,x21,1
	mov	x3,3
	adr	x4,ch_col
	ldr	w4,[x4,x19,lsl 2]
	bl	ui_fill
	add	x0,x20,x21
	sub	x0,x0,1
	mov	x1,TOOL_H
	mov	x2,1
	mov	x3,HEAD_H
	ldc	w4,c_ln
	bl	ui_fill
	ldv	x0,g_hot_head			; le nom survole ou appuye : un bouton
	lsl	x1,x19,2
	cmp	x0,x1
	b.ne	.name
	add	x0,x20,3
	mov	x1,TOOL_H+5
	sub	x2,x21,8
	mov	x3,19
	mov	x4,4
	ldc	w5,c_hollow
	mov	w6,w5
	bl	ui_rbox
.name:	mov	x0,x19
	bl	silenced
	mov	x23,x0
	mov	x0,x19
	bl	ins_ptr
	mov	x24,x0
	ldrb	w1,[x0]
	cbnz	w1,.nm
	adr	x0,s_none
	bl	ui_tr
	mov	x24,x0
.nm:	adr	x0,g_tmp2
	adr	x1,s_head_fmt
	add	x2,x19,1
	bl	fmt
	adr	x0,g_tmp2
	bl	str_len
	adr	x1,g_tmp2
	add	x0,x1,x0
	mov	x1,x24
	mov	x2,10
	bl	str_copy
	adr	x0,g_tmp2
	add	x1,x20,8
	mov	x2,TOOL_H+19
	ldc	w3,c_text
	cbz	x23,.nc
	ldc	w3,c_gmute
.nc:	mov	x4,13
	adr	x5,ui_font_b
	sub	x6,x21,16
	bl	ui_text_fit
	bl	cur_pat				; M
	add	x0,x0,PT_MUTE
	ldrb	w3,[x0,x19]
	ldv	x4,g_hot_head
	lsl	x5,x19,2
	add	x5,x5,1
	cmp	x4,x5
	cset	x4,eq
	add	x0,x20,4
	adr	x1,s_m
	movl	w2,0x00D9534A
	bl	head_pill
	ldv	x3,g_solo			; S
	cmp	x3,x19
	cset	x3,eq
	ldv	x4,g_hot_head
	lsl	x5,x19,2
	add	x5,x5,2
	cmp	x4,x5
	cset	x4,eq
	add	x0,x20,26
	adr	x1,s_s
	movl	w2,0x00D08A1E
	bl	head_pill
	add	x24,x20,52			; le niveau
	sub	x21,x21,60
	cmp	x21,6
	b.lt	.out
	mov	x0,x24
	mov	x1,TOOL_H+31
	mov	x2,x21
	mov	x3,6
	mov	x4,3
	ldc	w5,c_hollow
	mov	w6,w5
	bl	ui_rbox
	cbnz	x23,.out
	adr	x0,g_vu
	ldr	x0,[x0,x19,lsl 3]
	mul	x0,x0,x21
	mov	x1,100
	udiv	x2,x0,x1
	cmp	x2,2
	b.ls	.out
	mov	x0,x24
	mov	x1,TOOL_H+31
	mov	x3,6
	mov	x4,3
	movl	w5,0x0062B87A
	movl	w6,0x004A9E62
	bl	ui_rbox
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

draw_heads:
	prologue
	str	x19,[sp,-16]!
	mov	x19,0
.l:	mov	x0,x19
	bl	draw_head
	add	x19,x19,1
	cmp	x19,FMS_CH
	b.lo	.l
	mov	x0,SIDE_W
	mov	x1,TOOL_H
	ldv	x2,g_w
	sub	x2,x2,SIDE_W
	mov	x3,HEAD_H
	bl	ui_update
	ldr	x19,[sp],16
	epilogue

; ---- la grille --------------------------------------------------------------

draw_grid:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,64
	bl	sel_rect
	stp	x0,x1,[sp]			; r0, r1
	stp	x2,x3,[sp,16]			; c0, c1
	bl	cur_pat
	mov	x19,x0
	ldv	x25,g_gh
	ldv	x28,g_col_w
	mov	x0,SIDE_W
	mov	x1,GRID_Y
	ldv	x2,g_gw
	mov	x3,x25
	bl	ui_clip
	mov	x0,SIDE_W
	mov	x1,GRID_Y
	ldv	x2,g_gw
	mov	x3,x25
	ldc	w4,c_field
	bl	ui_fill
	mov	x0,SIDE_W
	mov	x1,GRID_Y
	mov	x2,NUM_W
	mov	x3,x25
	ldc	w4,c_gutter
	bl	ui_fill
	mov	x20,0				; rang a l'ecran
.row:	ldv	x0,g_vis
	cmp	x20,x0
	b.hi	.end
	ldv	x21,g_top
	add	x21,x21,x20			; ligne
	ldr	x0,[x19,PT_ROWS]
	cmp	x21,x0
	b.hs	.end
	mov	x0,ROW_H
	mul	x22,x20,x0
	add	x22,x22,GRID_Y			; y
	mov	x26,0				; 1 : ligne jouee
	ldv	x0,g_playing
	cbz	x0,.nop
	ldv	x0,g_play_pat
	ldv	x1,g_pat
	cmp	x0,x1
	b.ne	.nop
	ldv	x0,g_shown_row
	cmp	x0,x21
	cset	x26,eq
.nop:	ldc	w23,c_field			; fond de la ligne
	and	x0,x21,3
	cbnz	x0,.bg0
	ldc	w23,c_row4
	and	x0,x21,15
	cbnz	x0,.bg0
	ldc	w23,c_row16
.bg0:	cbz	x26,.bg1
	ldc	w23,c_playbg
	b	.bg2
.bg1:	ldv	x0,g_row
	cmp	x0,x21
	b.ne	.bg2
	mov	w0,w23
	ldc	w1,c_acc
	mov	x2,36
	bl	ui_mix
	mov	w23,w0
.bg2:	mov	x0,SIDE_W+NUM_W
	mov	x1,x22
	ldv	x2,g_gw
	sub	x2,x2,NUM_W
	mov	x3,ROW_H
	mov	w4,w23
	bl	ui_fill
	ldv	x0,g_row			; numero
	cmp	x0,x21
	cset	x0,eq
	orr	x0,x0,x26
	cbz	x0,.num
	mov	x0,SIDE_W
	mov	x1,x22
	mov	x2,NUM_W
	mov	x3,ROW_H
	ldc	w4,c_gplay
	cbnz	x26,.nb
	ldc	w4,c_gcur
.nb:	bl	ui_fill
.num:	add	x0,sp,32
	mov	x1,x21
	bl	put_2d
	strb	wzr,[x0]
	and	x24,x21,3			; 0 : gras
	add	x0,sp,32
	mov	x1,13
	adr	x2,ui_font_b
	cbz	x24,.nw
	adr	x2,ui_font
.nw:	bl	ui_text_width
	mov	x1,SIDE_W+NUM_W-8
	sub	x1,x1,x0
	add	x0,sp,32
	add	x2,x22,13
	ldc	w3,c_text
	cbz	x24,.nt
	ldc	w3,c_gnum
.nt:	mov	x4,13
	adr	x5,ui_font_b
	cbz	x24,.nf
	adr	x5,ui_font
.nf:	mov	x6,0
	bl	ui_text
	mov	x24,0				; canal
.cell:	mul	x27,x24,x28
	add	x27,x27,SIDE_W+NUM_W		; x
	ldv	x0,g_sel			; dans le bloc ?
	cbz	x0,.cur
	ldp	x0,x1,[sp]
	cmp	x21,x0
	b.lt	.cur
	cmp	x21,x1
	b.gt	.cur
	ldp	x0,x1,[sp,16]
	cmp	x24,x0
	b.lt	.cur
	cmp	x24,x1
	b.gt	.cur
	mov	w0,w23
	ldc	w1,c_acc
	mov	x2,96
	bl	ui_mix
	mov	w4,w0
	mov	x0,x27
	mov	x1,x22
	mov	x2,x28
	mov	x3,ROW_H
	bl	ui_fill
.cur:	mov	x0,x27				; trait du canal
	mov	x1,x22
	mov	x2,1
	mov	x3,ROW_H
	ldc	w4,c_gline
	bl	ui_fill
	ldv	x0,g_row			; le curseur
	cmp	x0,x21
	b.ne	.txt
	ldv	x0,g_ch
	cmp	x0,x24
	b.ne	.txt
	add	x0,x27,1
	mov	x1,x22
	sub	x2,x28,1
	mov	x3,ROW_H
	mov	x4,4
	ldc	w5,c_acc
	mov	w6,w5
	bl	ui_rbox
.txt:	ldr	x0,[x19,PT_N]
	lsl	x1,x24,8
	add	x1,x1,x21
	ldrb	w26,[x0,x1]
	mov	x0,x26
	add	x1,sp,40
	bl	note_text
	ldrb	w0,[sp,40]
	cbz	w0,.ncell			; silence : rien
	mov	x0,x24
	bl	silenced
	mov	x1,x0
	ldc	w3,c_gmute			; la couleur
	cbnz	x1,.cc
	ldc	w3,c_cont
	tst	w26,7
	b.eq	.cc
	ldc	w3,c_text
.cc:	ldv	x0,g_row
	cmp	x0,x21
	b.ne	.tt
	ldv	x0,g_ch
	cmp	x0,x24
	b.ne	.tt
	ldc	w3,c_acc_ink
.tt:	ldv	x1,g_cell_tw
	sub	x1,x28,x1
	asr	x1,x1,1
	add	x1,x1,x27
	add	x0,sp,40
	add	x2,x22,13
	mov	x4,13
	adr	x5,ui_font_m
	tst	w26,7
	b.eq	.tf
	ldv	x5,g_font_note
.tf:	mov	x6,0
	bl	ui_text
.ncell:	add	x24,x24,1
	cmp	x24,FMS_CH
	b.lo	.cell
	add	x20,x20,1
	b	.row
.end:	mov	x0,SIDE_W+NUM_W-1		; bord de la gouttiere
	mov	x1,GRID_Y
	mov	x2,1
	mov	x3,x25
	ldc	w4,c_gline
	bl	ui_fill
	mov	x0,0
	mov	x1,0
	mov	x2,0
	mov	x3,0
	bl	ui_clip
	mov	x0,SIDE_W
	mov	x1,GRID_Y
	ldv	x2,g_gw
	mov	x3,x25
	bl	ui_update
	add	sp,sp,64
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---- la liste des motifs ----------------------------------------------------

; g_pat visible dans la liste
list_show:
	ldv	x0,g_pat
	ldv	x1,g_ltop
	ldv	x2,g_lvis
	cmp	x0,x1
	b.ge	.h
	stv	x0,g_ltop
	ret
.h:	add	x3,x1,x2
	cmp	x0,x3
	b.lt	.r
	sub	x1,x0,x2
	add	x1,x1,1
	stv	x1,g_ltop
.r:	ret

draw_plist:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x0,0
	mov	x1,LIST_Y
	mov	x2,SIDE_W-1
	ldv	x3,g_list_h
	ldc	w4,c_face
	bl	ui_fill
	ldv	x0,g_npat			; g_ltop borne
	ldv	x1,g_lvis
	sub	x0,x0,x1
	cmp	x0,0
	csel	x0,xzr,x0,lt
	ldv	x1,g_ltop
	cmp	x1,x0
	csel	x1,x0,x1,gt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	stv	x1,g_ltop
	mov	x19,0				; rang
.l:	ldv	x0,g_lvis
	cmp	x19,x0
	b.hs	.e
	ldv	x20,g_ltop
	add	x20,x20,x19			; motif
	ldv	x0,g_npat
	cmp	x20,x0
	b.hs	.e
	mov	x0,LIST_ROW
	mul	x21,x19,x0
	add	x21,x21,LIST_Y			; y
	ldv	x0,g_pat
	cmp	x0,x20
	cset	x22,eq
	cbz	x22,.hot
	mov	x0,4
	add	x1,x21,1
	mov	x2,SIDE_W-13
	mov	x3,LIST_ROW-2
	mov	x4,4
	ldc	w5,c_acc
	mov	w6,w5
	bl	ui_rbox
	b	.tx
.hot:	ldv	x0,g_hot_list
	cmp	x0,x20
	b.ne	.tx
	mov	x0,4
	add	x1,x21,1
	mov	x2,SIDE_W-13
	mov	x3,LIST_ROW-2
	mov	x4,4
	ldc	w5,c_hollow
	mov	w6,w5
	bl	ui_rbox
.tx:	ldc	w23,c_text
	ldc	w24,c_text2
	cbz	x22,.ink
	ldc	w23,c_acc_ink
	mov	w24,w23
.ink:	ldv	x0,g_playing			; le motif joue
	cbz	x0,.nop
	ldv	x0,g_play_pat
	cmp	x0,x20
	b.ne	.nop
	adr	x0,s_arrow
	mov	x1,10
	add	x2,x21,16
	mov	w3,w23
	mov	x4,9
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.nop:	adr	x0,g_tmp2
	add	x1,x20,1
	bl	put_2d
	strb	wzr,[x0]
	adr	x0,g_tmp2
	mov	x1,24
	add	x2,x21,16
	mov	w3,w23
	bl	txtb
	mov	x0,x20
	bl	pat_ptr
	ldr	x2,[x0,PT_ROWS]
	str	x2,[sp,-16]!
	adr	x0,s_nrows
	bl	ui_tr
	mov	x1,x0
	ldr	x2,[sp],16
	adr	x0,g_tmp2
	bl	fmt
	adr	x0,g_tmp2
	mov	x1,50
	add	x2,x21,16
	mov	w3,w24
	mov	x4,13
	adr	x5,ui_font
	mov	x6,SIDE_W-62
	bl	ui_text_fit
	add	x19,x19,1
	b	.l
.e:	mov	x0,0
	mov	x1,LIST_Y
	mov	x2,SIDE_W-1
	ldv	x3,g_list_h
	bl	ui_update
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---- le piano ---------------------------------------------------------------

; x0 = touche blanche, x1 = 1 noire -> w0 = couleur du canal qui la joue (0 : aucun)
piano_lit:
	adr	x2,g_snd
	mov	x3,0
.l:	ldrb	w4,[x2,x3]
	tbnz	w4,7,.n
	ands	w5,w4,7
	b.eq	.n
	ubfx	x6,x4,3,3
	mov	x7,7
	mul	x6,x6,x7
	add	x6,x6,x5
	sub	x6,x6,1
	cmp	x6,x0
	b.ne	.n
	ubfx	x6,x4,6,1
	cmp	x6,x1
	b.ne	.n
	adr	x2,ch_col
	ldr	w0,[x2,x3,lsl 2]
	ret
.n:	add	x3,x3,1
	cmp	x3,FMS_CH
	b.lo	.l
	mov	w0,0
	ret

; x0 = touche blanche -> x0 = 1 si une noire la suit (pas apres mi, si)
piano_has_black:
	mov	x1,7
	udiv	x2,x0,x1
	msub	x2,x2,x1,x0
	cmp	x2,2
	b.eq	.n
	cmp	x2,6
	b.eq	.n
	mov	x0,1
	ret
.n:	mov	x0,0
	ret

draw_piano:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	ldv	x22,g_h
	sub	x22,x22,ST_H+PIANO_H		; haut
	ldv	x25,g_w
	sub	x25,x25,SIDE_W			; largeur
	mov	x0,SIDE_W
	mov	x1,x22
	mov	x2,x25
	mov	x3,PIANO_H
	ldc	w4,c_face
	bl	ui_fill
	mov	x0,SIDE_W
	mov	x1,x22
	mov	x2,x25
	mov	x3,1
	ldc	w4,c_ln
	bl	ui_fill
	ldv	x20,g_kw
	ldv	x21,g_kx			; x de la premiere touche
	add	x22,x22,7			; y
	sub	x0,x21,1			; le lit des touches : leurs joints se voient
	sub	x1,x22,1
	mov	x2,PIANO_KEYS
	mul	x2,x2,x20
	add	x2,x2,1
	mov	x3,PIANO_KH+2
	movl	w4,0x00989CA4
	bl	ui_fill
	mov	x19,0
.w:	cmp	x19,PIANO_KEYS
	b.hs	.bl
	mov	x0,x19
	mov	x1,0
	bl	piano_lit
	mov	w5,w0
	mov	w6,w0
	cbnz	w0,.wb
	movl	w5,0x00FFFFFF
	movl	w6,0x00ECECEC
.wb:	mul	x0,x19,x20
	add	x0,x0,x21
	mov	x1,x22
	sub	x2,x20,1
	mov	x3,PIANO_KH
	mov	x4,2
	bl	ui_rbox
	add	x19,x19,1
	b	.w
.bl:	mov	x19,0
	mov	x0,10				; largeur des noires
	mov	x1,3
	mul	x23,x20,x1
	udiv	x23,x23,x0			; k * 3 / 10
.b:	cmp	x19,PIANO_KEYS
	b.hs	.mark
	mov	x0,x19
	bl	piano_has_black
	cbz	x0,.nb
	mov	x0,x19
	mov	x1,1
	bl	piano_lit
	mov	w5,w0
	mov	w6,w0
	cbnz	w0,.bb
	movl	w5,0x00303030
	movl	w6,0x00181818
.bb:	add	x0,x19,1
	mul	x0,x0,x20
	add	x0,x0,x21
	sub	x0,x0,x23
	sub	x0,x0,1
	mov	x1,x22
	lsl	x2,x23,1
	add	x2,x2,1
	mov	x3,PIANO_BH
	mov	x4,2
	bl	ui_rbox
.nb:	add	x19,x19,1
	b	.b
.mark:	ldv	x0,g_oct			; l'octave ou le clavier ecrit
	mov	x1,7
	mul	x0,x0,x1
	mul	x0,x0,x20
	add	x0,x0,x21
	add	x1,x22,PIANO_KH+3
	mov	x2,7
	mul	x2,x2,x20
	sub	x2,x2,1
	mov	x3,3
	ldc	w4,c_acc
	bl	ui_fill
	mov	x0,SIDE_W
	ldv	x1,g_h
	sub	x1,x1,ST_H+PIANO_H
	mov	x2,x25
	mov	x3,PIANO_H
	bl	ui_update
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---- la barre d'etat --------------------------------------------------------

draw_status:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,g_w
	ldp	x19,x20,[x0]
	sub	x20,x20,ST_H
	mov	x0,0
	mov	x1,x20
	mov	x2,x19
	mov	x3,ST_H
	ldc	w4,c_hollow
	bl	ui_fill
	mov	x0,0
	mov	x1,x20
	mov	x2,x19
	mov	x3,1
	ldc	w4,c_ln
	bl	ui_fill
	adr	x0,g_status
	mov	x1,8
	add	x2,x20,16
	ldc	w3,c_text
	mov	x4,13
	adr	x5,ui_font
	sub	x6,x19,16
	bl	ui_text_fit
	mov	x0,0
	mov	x1,x20
	mov	x2,x19
	mov	x3,ST_H
	bl	ui_update
	ldp	x19,x20,[sp],16
	epilogue

; le titre de la fenetre : " FM Tracker - nom * "
update_title:
	prologue
	str	x19,[sp,-16]!
	adr	x0,g_tmp
	strb	wzr,[x0]
	adr	x1,s_app
	bl	str_cat
	mov	x19,x0
	adr	x0,g_path
	ldrb	w1,[x0]
	cbz	w1,.m
	mov	x0,x19
	adr	x1,s_dash
	bl	str_cat
	mov	x19,x0
	adr	x0,g_path
	bl	base_name
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
	mov	x19,x0
.m:	ldv	x1,g_modified
	cbz	x1,.s
	mov	x0,x19
	adr	x1,s_star
	bl	str_cat
.s:	adr	x0,g_tmp
	bl	ui_title
	ldr	x19,[sp],16
	epilogue

; ---- flush : repeint ce qui a change ----------------------------------------

flush:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x0,g_in_dlg
	cbnz	x0,.out
	ldv	x19,g_redraw
	cbz	x19,.out
	stv	xzr,g_redraw
	tbz	x19,BIT_ALL,.na
	bl	paint_all
	movl	x0,D_SONG or D_BOXES or D_SCROLL or D_SIDE or D_STATUS
	orr	x19,x19,x0
.na:	tbz	x19,BIT_BOXES,.nb
	bl	cur_pat
	ldr	x1,[x0,PT_ROWS]
	ldv	x0,w_rows
	bl	ui_w_set_value
	bl	cur_pat
	ldr	x1,[x0,PT_SPEED]
	ldv	x0,w_speed
	bl	ui_w_set_value
	ldv	x0,w_oct
	ldv	x1,g_oct
	bl	ui_w_set_value
	ldv	x0,w_step
	ldv	x1,g_step
	bl	ui_w_set_value
	ldv	x0,w_play
	ldv	x1,g_playing
	bl	ui_w_set_value
	ldv	x0,w_loop
	ldv	x1,g_loop
	bl	ui_w_set_value
	ldv	x0,w_follow
	ldv	x1,g_follow
	bl	ui_w_set_value
.nb:	tbz	x19,BIT_SCROLL,.ns
	bl	cur_pat
	ldr	x2,[x0,PT_ROWS]
	ldv	x3,g_vis
	ldv	x0,g_sb_rows			; la plage, si elle change
	cmp	x0,x2
	b.ne	.sr
	ldv	x0,g_sb_vis
	cmp	x0,x3
	b.eq	.sv
.sr:	stv	x2,g_sb_rows
	stv	x3,g_sb_vis
	ldv	x0,w_sb
	mov	x1,0
	mov	x4,1
	bl	ui_w_set_range
.sv:	ldv	x0,g_top
	ldv	x1,g_sb_top
	cmp	x0,x1
	b.eq	.ns
	stv	x0,g_sb_top
	mov	x1,x0
	ldv	x0,w_sb
	bl	ui_w_set_value
.ns:	tbz	x19,BIT_LCD,.nl
	bl	update_lcd
.nl:	tbz	x19,BIT_GRID,.ng
	bl	draw_grid
.ng:	tbz	x19,BIT_HEAD,.nh
	bl	draw_heads
.nh:	tbz	x19,BIT_LIST,.nli
	bl	draw_plist
.nli:	tbz	x19,BIT_PIANO,.np
	bl	draw_piano
.np:	tbz	x19,BIT_STATUS,.nst
	bl	draw_status
.nst:	tbz	x19,BIT_SIDE,.ni
	bl	draw_song_labels
.ni:	tbz	x19,BIT_TITLE,.nt
	bl	update_title
.nt:	tbz	x19,BIT_ALL,.out
	bl	ui_update_all
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---- clavier ---------------------------------------------------------------

; (ui_ev) une touche pour la grille -> x0 = 1 si prise
grid_key:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,ui_ev
	ldr	w19,[x0,EV_CODE]
	ldr	w20,[x0,EV_CHAR]
	ldr	w21,[x0,EV_MODS]
	mov	w0,MODS_CTRL
	and	w22,w21,w0			; Ctrl
	mov	w0,MODS_SHIFT
	and	w23,w21,w0			; Maj
	cmp	w19,KEY_ESC
	b.ne	.ne
	ldv	x0,g_sel
	cbz	x0,.esc
	ldv	x0,g_playing
	cbnz	x0,.esc
	stv	xzr,g_sel
	mark	D_GRID
	b	.yes
.esc:	bl	op_stop
	b	.yes
.ne:	cbnz	w22,.arrows
	adr	x0,s_note_keys			; une note
	mov	x1,0
.nk:	ldrb	w2,[x0,x1]
	cbz	w2,.dig
	cmp	w2,w20
	b.eq	.note
	sub	w2,w2,32
	cmp	w2,w20
	b.eq	.sharp
	add	x1,x1,1
	b	.nk
.note:	add	x0,x1,1
	mov	x1,0
	b	.en
.sharp:	add	x0,x1,1
	mov	x1,1
.en:	ldv	x2,g_oct
	bl	enter_note
	b	.yes
.dig:	sub	w0,w20,'0'			; l'octave
	cmp	w0,8
	b.hs	.sp
	stv	x0,g_oct
	mark	D_BOXES or D_PIANO
	b	.yes
.sp:	cmp	w20,' '
	b.ne	.del
	mov	x0,FMS_OFF
	ldv	x1,g_step
	bl	set_cell
	b	.yes
.del:	cmp	w19,KEY_DELETE
	b.ne	.bs
	ldv	x0,g_sel
	cbz	x0,.del1
	bl	op_clear_block
	b	.yes
.del1:	mov	x0,FMS_CONT
	ldv	x1,g_step
	bl	set_cell
	b	.yes
.bs:	cmp	w19,KEY_BACKSPACE
	b.ne	.hash
	ldv	x0,g_row
	cbz	x0,.yes
	sub	x0,x0,1
	ldv	x1,g_ch
	bl	move_to
	mov	x0,FMS_CONT
	mov	x1,0
	bl	set_cell
	b	.yes
.hash:	cmp	w20,'#'
	b.eq	.hs
	cmp	w20,'+'
	b.ne	.arrows
.hs:	bl	cur_pat				; le diese de la note sous le curseur
	ldr	x0,[x0,PT_N]
	ldv	x1,g_ch
	lsl	x1,x1,8
	ldv	x2,g_row
	add	x1,x1,x2
	ldrb	w3,[x0,x1]
	tbnz	w3,7,.yes
	ands	w0,w3,7
	b.eq	.yes
	ubfx	x1,x3,6,1
	eor	x1,x1,1
	ubfx	x2,x3,3,3
	bl	make_note
	mov	x1,0
	bl	set_cell
	b	.yes
.arrows:
	cbz	w22,.mv				; Ctrl + haut / bas : demi-ton (Maj : octave)
	cmp	w19,KEY_UP
	b.eq	.tu
	cmp	w19,KEY_DOWN
	b.ne	.no
	mov	x0,-1
	mov	x1,-12
	b	.td
.tu:	mov	x0,1
	mov	x1,12
.td:	cmp	w23,0
	csel	x0,x1,x0,ne
	bl	transpose
	b	.yes
.mv:	ldv	x0,g_row
	ldv	x1,g_ch
	ldv	x2,g_vis
	cmp	w19,KEY_UP
	b.ne	.k1
	sub	x0,x0,1
	b	.go
.k1:	cmp	w19,KEY_DOWN
	b.ne	.k2
	add	x0,x0,1
	b	.go
.k2:	cmp	w19,KEY_LEFT
	b.ne	.k3
	sub	x1,x1,1
	b	.go
.k3:	cmp	w19,KEY_RIGHT
	b.ne	.k4
	add	x1,x1,1
	b	.go
.k4:	cmp	w19,KEY_TAB
	b.ne	.k5
	add	x1,x1,1
	b	.go
.k5:	cmp	w19,KEY_PGUP
	b.ne	.k6
	sub	x0,x0,x2
	b	.go
.k6:	cmp	w19,KEY_PGDN
	b.ne	.k7
	add	x0,x0,x2
	b	.go
.k7:	cmp	w19,KEY_HOME
	b.ne	.k8
	mov	x0,0
	b	.go
.k8:	cmp	w19,KEY_END
	b.ne	.no
	stp	x0,x1,[sp,-16]!
	bl	cur_pat
	ldr	x2,[x0,PT_ROWS]
	ldp	x0,x1,[sp],16
	sub	x0,x2,1
.go:	mov	x24,x1
	cbz	w23,.nosel
	cmp	w19,KEY_TAB
	b.eq	.nosel
	ldv	x2,g_sel			; Maj : le bloc s'etend
	cbnz	x2,.cl
	ldv	x2,g_row
	stv	x2,g_ar
	ldv	x2,g_ch
	stv	x2,g_ac
	mov	x2,1
	stv	x2,g_sel
.cl:	cmp	x24,0
	csel	x24,xzr,x24,lt
	cmp	x24,FMS_CH-1
	mov	x2,FMS_CH-1
	csel	x24,x2,x24,gt
	b	.mt
.nosel:	stv	xzr,g_sel
.mt:	mov	x1,x24
	bl	move_to
.yes:	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---- souris ----------------------------------------------------------------

; x0 = x, x1 = y -> x0 = canal * 4 + partie (0 nom, 1 M, 2 S), -1 : rien
head_part:
	subs	x2,x0,SIDE_W+NUM_W
	b.mi	.no
	subs	x3,x1,TOOL_H
	b.mi	.no
	cmp	x3,HEAD_H
	b.hs	.no
	ldv	x4,g_col_w
	udiv	x5,x2,x4
	cmp	x5,FMS_CH
	b.hs	.no
	msub	x2,x5,x4,x2			; x dans la colonne
	lsl	x0,x5,2
	cmp	x3,25
	b.lo	.r
	cmp	x2,4
	b.lo	.no
	cmp	x2,24
	b.hs	.s
	add	x0,x0,1
	ret
.s:	cmp	x2,26
	b.lo	.no
	cmp	x2,46
	b.hs	.no
	add	x0,x0,2
.r:	ret
.no:	mov	x0,-1
	ret

; x0 = x, x1 = y -> x0 = 1 dans la grille (colonnes) ; x1 = ligne, x2 = canal
grid_cell:
	stp	x0,x1,[sp,-16]!
	ldv	x2,g_top
	ldp	x0,x1,[sp]
	subs	x1,x1,GRID_Y
	csel	x1,xzr,x1,mi
	mov	x3,ROW_H
	udiv	x1,x1,x3
	add	x1,x1,x2
	sub	x0,x0,SIDE_W+NUM_W
	mov	x4,x0
	ldv	x3,g_col_w
	cmp	x0,0
	csel	x0,xzr,x0,lt
	udiv	x2,x0,x3
	mov	x3,FMS_CH-1
	cmp	x2,x3
	csel	x2,x3,x2,hi
	add	sp,sp,16
	ldv	x3,g_pat
	lsl	x3,x3,5
	adr	x5,g_pats
	ldr	x3,[x5,x3]			; (PT_ROWS = 0)
	cmp	x1,x3
	b.lo	.in
	sub	x1,x3,1
.in:	cmp	x4,0
	cset	x0,ge
	ret

; (ui_ev) souris hors des widgets
on_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,ui_ev
	ldr	w19,[x0,EV_KIND]
	ldrsw	x20,[x0,EV_X]
	ldrsw	x21,[x0,EV_Y]
	ldr	w22,[x0,EV_CODE]
	stv	x20,g_mx
	stv	x21,g_my
	ldv	x0,g_drag
	cmp	x0,1
	b.eq	.gdrag
	cmp	x0,2
	b.eq	.hdrag
	cbnz	w19,.press
	; --- survol : en-tetes, liste
	mov	x0,x20
	mov	x1,x21
	bl	head_part
	ldv	x1,g_hot_head
	cmp	x0,x1
	b.eq	.lh
	stv	x0,g_hot_head
	mark	D_HEAD
.lh:	bl	list_hit
	ldv	x1,g_hot_list
	cmp	x0,x1
	b.eq	.out
	stv	x0,g_hot_list
	mark	D_LIST
	b	.out
.press:	cmp	w19,1
	b.ne	.out
	mov	x0,0				; le clavier revient a la grille
	bl	ui_w_focus
	ands	w0,w22,3
	b.eq	.out
	mov	x0,x20				; en-tete
	mov	x1,x21
	bl	head_part
	tbnz	x0,63,.ngr
	mov	x1,x0
	lsr	x0,x1,2				; canal
	and	x1,x1,3
	tbnz	w22,1,.rc			; bouton droit : muet
	cbz	x1,.nm
	cmp	x1,1
	b.eq	.mu
	bl	solo_channel
	b	.out
.mu:	bl	mute_channel
	b	.out
.rc:	bl	mute_channel
	b	.out
.nm:	mov	x1,2				; le nom : au relache
	stv	x1,g_drag
	stv	x0,g_down_ch
	b	.out
.ngr:	cmp	x20,SIDE_W
	b.lt	.lst
	mov	x0,GRID_Y
	cmp	x21,x0
	b.lt	.out
	ldv	x1,g_gh
	add	x0,x0,x1
	cmp	x21,x0
	b.ge	.npi
	ldv	x1,g_gw
	add	x1,x1,SIDE_W
	cmp	x20,x1
	b.ge	.out
	mov	x0,x20
	mov	x1,x21
	bl	grid_cell
	cbz	x0,.out
	stp	x1,x2,[sp,-16]!
	adr	x0,ui_ev
	ldr	w0,[x0,EV_MODS]
	mov	w3,MODS_SHIFT
	tst	w0,w3
	b.eq	.nosh
	ldv	x0,g_sel
	cbnz	x0,.gd
	ldv	x0,g_row
	stv	x0,g_ar
	ldv	x0,g_ch
	stv	x0,g_ac
	mov	x0,1
	stv	x0,g_sel
	b	.gd
.nosh:	stv	xzr,g_sel
	ldp	x1,x2,[sp]
	stv	x1,g_ar
	stv	x2,g_ac
.gd:	mov	x0,1
	stv	x0,g_drag
	ldp	x0,x1,[sp],16
	bl	move_to
	b	.out
.npi:	ldv	x1,g_h
	sub	x1,x1,ST_H
	cmp	x21,x1
	b.ge	.out
	mov	x0,x20
	mov	x1,x21
	bl	piano_click
	b	.out
.lst:	bl	list_hit
	tbnz	x0,63,.out
	ldv	x1,g_pat
	cmp	x0,x1
	b.eq	.out
	stv	x0,g_pat
	stv	xzr,g_sel
	bl	refresh_pattern
	b	.out
.gdrag:	cmp	w19,2				; glisser dans la grille : le bloc
	b.ne	.gm
	stv	xzr,g_drag
	b	.out
.gm:	cbnz	w19,.out
	mov	x0,x20
	mov	x1,x21
	bl	grid_cell
	ldv	x0,g_row
	cmp	x0,x1
	b.ne	.gs
	ldv	x0,g_ch
	cmp	x0,x2
	b.eq	.out
.gs:	ldv	x0,g_ar
	eor	x0,x0,x1
	ldv	x3,g_ac
	eor	x3,x3,x2
	orr	x0,x0,x3
	cmp	x0,0
	cset	x0,ne
	stv	x0,g_sel
	mov	x0,x1
	mov	x1,x2
	bl	move_to
	b	.out
.hdrag:	cmp	w19,2				; le nom d'un canal : relache dessus
	b.ne	.out
	stv	xzr,g_drag
	mov	x0,x20
	mov	x1,x21
	bl	head_part
	ldv	x1,g_down_ch
	lsl	x1,x1,2
	cmp	x0,x1
	b.ne	.out
	ldv	x0,g_down_ch
	bl	edit_instrument
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (g_mx, g_my) -> x0 = motif sous le pointeur, -1 : aucun
list_hit:
	ldv	x0,g_mx
	ldv	x1,g_my
	cmp	x0,SIDE_W-4
	b.hs	.no
	subs	x1,x1,LIST_Y
	b.mi	.no
	ldv	x2,g_list_h
	cmp	x1,x2
	b.hs	.no
	mov	x2,LIST_ROW
	udiv	x1,x1,x2
	ldv	x0,g_ltop
	add	x0,x0,x1
	ldv	x1,g_npat
	cmp	x0,x1
	b.hs	.no
	ret
.no:	mov	x0,-1
	ret

; x0 = x, x1 = y : un clic sur le piano entre cette note
piano_click:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x19,g_kw
	ldv	x2,g_kx
	subs	x20,x0,x2			; x dans le clavier
	b.mi	.out
	mov	x2,PIANO_KEYS
	mul	x2,x2,x19
	cmp	x20,x2
	b.hs	.out
	ldv	x2,g_h
	sub	x2,x2,ST_H+PIANO_H-7
	subs	x21,x1,x2			; y dans le clavier
	b.mi	.out
	cmp	x21,PIANO_KH
	b.hs	.out
	udiv	x22,x20,x19			; touche blanche
	mov	x4,0				; diese
	cmp	x21,PIANO_BH
	b.hs	.pick
	msub	x20,x22,x19,x20			; decalage dans la touche
	mov	x0,3
	mul	x5,x19,x0
	mov	x0,10
	udiv	x5,x5,x0			; k * 3 / 10
	sub	x6,x19,x5
	sub	x6,x6,1
	cmp	x20,x6
	b.lt	.left
	mov	x0,x22
	stp	x4,x5,[sp,-16]!
	bl	piano_has_black
	ldp	x4,x5,[sp],16
	cbz	x0,.left
	mov	x4,1
	b	.pick
.left:	cmp	x20,x5
	b.hi	.pick
	cbz	x22,.pick
	sub	x0,x22,1
	str	x4,[sp,-16]!
	bl	piano_has_black
	ldr	x4,[sp],16
	cbz	x0,.pick
	sub	x22,x22,1
	mov	x4,1
.pick:	mov	x0,7
	udiv	x1,x22,x0
	stv	x1,g_oct
	msub	x0,x1,x0,x22			; note 0..6
	add	x0,x0,1
	mov	x1,x4
	ldv	x2,g_oct
	bl	enter_note
	mark	D_BOXES or D_PIANO
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = crans (> 0 : vers le haut) : molette sur la grille ou la liste
on_wheel:
	prologue
	mov	x3,x0
	ldv	x1,g_mx
	cmp	x1,SIDE_W
	b.hs	.g
	ldv	x1,g_ltop
	sub	x1,x1,x3
	stv	x1,g_ltop
	mark	D_LIST
	b	.o
.g:	mov	x1,3
	mul	x1,x1,x3
	ldv	x2,g_top
	sub	x2,x2,x1
	stv	x2,g_top
	bl	sync_scroll
.o:	epilogue

; (fonction de la barre de defilement) x0 = widget, x1 = code
on_scrollbar:
	prologue
	bl	ui_w_get_value
	stv	x0,g_top
	stv	x0,g_sb_top
	mark	D_GRID
	epilogue
