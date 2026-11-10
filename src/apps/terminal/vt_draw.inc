; vt_draw.inc - le Terminal : dessin de la grille
;
; L'ombre (vt_shadow) garde ce qui est a l'ecran, cellule par cellule. Un
; dessin compare chaque cellule montree a son ombre et ne redessine que les
; lignes qui ont change, par plages de meme style : un fond (ui_fill) et un
; ui_text par plage. Le curseur (barre) est redessine a part ; la cellule
; qu'il couvrait est marquee changee.

; -> palette de 256 couleurs : 16 de base, cube 6 x 6 x 6, 24 gris. Feuille.
pal_init:
	adr	x0,palette
	adr	x1,pal16
	mov	x2,0
.b:	ldr	w3,[x1,x2,lsl 2]
	str	w3,[x0,x2,lsl 2]
	add	x2,x2,1
	cmp	x2,16
	b.lo	.b
	adr	x4,pal_lv
	mov	x5,0				; rouge
.r:	mov	x6,0				; vert
.g:	mov	x7,0				; bleu
.bl:	ldrb	w8,[x4,x5]
	ldrb	w9,[x4,x6]
	ldrb	w10,[x4,x7]
	lsl	w8,w8,16
	orr	w8,w8,w9,lsl 8
	orr	w8,w8,w10
	str	w8,[x0,x2,lsl 2]
	add	x2,x2,1
	add	x7,x7,1
	cmp	x7,6
	b.lo	.bl
	add	x6,x6,1
	cmp	x6,6
	b.lo	.g
	add	x5,x5,1
	cmp	x5,6
	b.lo	.r
	mov	w5,8				; gris : 8, 18 ... 238
.gr:	orr	w6,w5,w5,lsl 8
	orr	w6,w6,w5,lsl 16
	str	w6,[x0,x2,lsl 2]
	add	w5,w5,10
	add	x2,x2,1
	cmp	x2,256
	b.lo	.gr
	ret

; w0 = couleur d'une cellule, w1 = couleur par defaut -> w0 = 0x00RRGGBB. Feuille.
col_rgb:
	cbz	w0,.def
	lsr	w2,w0,24
	cmp	w2,1
	b.eq	.rgb
	and	w0,w0,0xFF
	adr	x2,palette
	ldr	w0,[x2,x0,lsl 2]
	ret
.rgb:	and	w0,w0,0xFFFFFF
	ret
.def:	mov	w0,w1
	ret

; x0 = colonne -> x0 = x en pixels. Feuille (x0, x1).
col_x:
	adr	x1,adv100
	ldr	x1,[x1]
	mul	x0,x0,x1
	mov	x1,100
	udiv	x0,x0,x1
	add	x0,x0,PAD
	ret

; x0 = ligne de l'ecran -> x0 = cellules montrees (grille, ou historique si
; la vue est remontee). Feuille.
vt_line_disp:
	adr	x9,vt
	ldr	x1,[x9,V_ALT]
	adr	x2,scroll
	ldr	x2,[x2]
	cbnz	x1,.grid
	cbz	x2,.grid
	subs	x3,x0,x2
	b.lt	.sb
	mov	x0,x3
.grid:	ldr	x1,[x9,V_TBL]
	ldr	x0,[x1,x0,lsl 3]
	ret
.sb:	ldr	x5,[x9,V_SBHEAD]		; x3 < 0 : x3 lignes avant la place libre
	add	x5,x5,x3
	cmp	x5,0
	b.ge	@f
	add	x5,x5,VT_SBMAX
@@:	cmp	x5,VT_SBMAX
	b.lo	@f
	sub	x5,x5,VT_SBMAX
@@:	adr	x6,vt_sb
	ldr	x6,[x6]
	add	x0,x6,x5,lsl VT_ROWSH
	ret

; toute l'ombre marquee changee (lignes et colonnes en cours). Feuille.
shadow_inval:
	adr	x9,vt
	ldr	x2,[x9,V_ROWS]
	ldr	x3,[x9,V_COLS]
	adr	x4,vt_shadow
	ldr	x4,[x4]
	mov	x5,-1
	mov	x6,0
.r:	cmp	x6,x2
	b.hs	.ret
	add	x7,x4,x6,lsl VT_ROWSH
	mov	x8,x3
.c:	str	x5,[x7],16
	subs	x8,x8,1
	b.ne	.c
	add	x6,x6,1
	b	.r
.ret:	ret

; etend la zone a montrer (x27 haut, x28 bas) a la ligne r de l'ecran
macro yrange r* {
	mov	x9,LH
	mul	x9,r,x9
	add	x9,x9,PAD
	cmp	x9,x27
	csel	x27,x9,x27,lt
	add	x9,x9,LH
	cmp	x9,x28
	csel	x28,x9,x28,gt
}

; dessine ce qui a change depuis le dernier dessin (tout si full_redraw)
term_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	adr	x19,vt
	adr	x0,full_redraw
	ldr	x1,[x0]
	cbz	x1,.part
	str	xzr,[x0]
	adr	x9,ui_w
	ldp	x2,x3,[x9]
	mov	x0,0
	mov	x1,0
	movl	w4,COL_BG
	bl	ui_fill
	bl	shadow_inval
	adr	x0,lc_valid
	str	xzr,[x0]
	mov	x27,0
	adr	x9,ui_h
	ldr	x28,[x9]
	b	.rows
.part:	movl	x27,0x7FFFFFFF
	mov	x28,0
	adr	x9,lc_valid			; l'ancien curseur s'efface
	ldr	x0,[x9]
	cbz	x0,.rows
	str	xzr,[x9]
	adr	x9,lc_row
	ldp	x20,x21,[x9]
	ldr	x0,[x19,V_ROWS]
	cmp	x20,x0
	b.hs	.rows
	yrange	x20
	ldr	x0,[x19,V_COLS]
	cmp	x21,x0
	b.hs	.padc
	adr	x9,vt_shadow
	ldr	x9,[x9]
	add	x9,x9,x20,lsl VT_ROWSH
	add	x9,x9,x21,lsl 4
	mov	x0,-1
	str	x0,[x9]
	b	.rows
.padc:	mov	x0,x21				; (apres la derniere colonne)
	bl	col_x
	mov	x9,LH
	mul	x1,x20,x9
	add	x1,x1,PAD
	mov	x2,2
	mov	x3,LH
	movl	w4,COL_BG
	bl	ui_fill
.rows:	mov	x20,0
.row:	ldr	x0,[x19,V_ROWS]
	cmp	x20,x0
	b.hs	.cursor
	mov	x0,x20
	bl	vt_line_disp
	mov	x21,x0				; cellules montrees
	adr	x9,vt_shadow
	ldr	x22,[x9]
	add	x22,x22,x20,lsl VT_ROWSH	; ombre
	mov	x23,0				; une cellule changee ?
.cell:	ldr	x0,[x19,V_COLS]
	cmp	x23,x0
	b.hs	.nextrow
	add	x9,x21,x23,lsl 4
	ldp	x0,x1,[x9]
	add	x9,x22,x23,lsl 4
	ldp	x2,x3,[x9]
	add	x23,x23,1
	cmp	x0,x2
	ccmp	x1,x3,0,eq
	b.eq	.cell
	; oui : toute la ligne est redessinee (un glyphe deborde parfois d'un
	; pixel sur la cellule voisine : une cellule seule laisserait des restes)
	mov	x23,0
.run0:	ldr	x0,[x19,V_COLS]
	cmp	x23,x0
	b.hs	.rowdone
	mov	x24,x23				; debut de plage, son style
	add	x9,x21,x23,lsl 4
	ldp	x0,x26,[x9]
	lsr	x25,x0,32
	add	x23,x23,1
.ext:	ldr	x0,[x19,V_COLS]
	cmp	x23,x0
	b.hs	.run
	add	x9,x21,x23,lsl 4
	ldp	x0,x1,[x9]
	lsr	x2,x0,32
	cmp	x2,x25
	ccmp	x1,x26,0,eq
	b.ne	.run				; autre style : fin de plage
	add	x23,x23,1
	b	.ext
.run:	mov	x0,x20
	mov	x1,x24
	mov	x2,x23
	mov	x3,x21
	mov	x4,x25
	mov	x5,x26
	bl	draw_run
	b	.run0
.rowdone:
	mov	x0,x22				; l'ombre suit
	mov	x1,x21
	ldr	x2,[x19,V_COLS]
	bl	cells_move
	yrange	x20
.nextrow:
	add	x20,x20,1
	b	.row
.cursor:
	adr	x9,run_pid			; le Terminal attend une commande : toujours ;
	ldr	x0,[x9]				; un programme tourne : selon ?25
	cbz	x0,.cvis
	ldr	x0,[x19,V_MODES]
	tbz	x0,0,.upd			; (VM_CURSOR)
.cvis:	ldr	x20,[x19,V_CY]
	ldr	x0,[x19,V_ALT]
	cbnz	x0,.cr
	adr	x9,scroll
	ldr	x0,[x9]
	add	x20,x20,x0
.cr:	ldr	x0,[x19,V_ROWS]
	cmp	x20,x0
	b.hs	.upd
	ldr	x21,[x19,V_CX]
	ldr	x0,[x19,V_WRAP]
	add	x21,x21,x0
	mov	x0,x21
	bl	col_x
	mov	x9,LH
	mul	x1,x20,x9
	add	x1,x1,PAD+1
	mov	x2,2
	mov	x3,LH-2
	movl	w4,COL_CURSOR
	bl	ui_fill
	adr	x9,lc_row
	stp	x20,x21,[x9]
	adr	x9,lc_valid
	mov	x0,1
	str	x0,[x9]
	yrange	x20
.upd:	cmp	x28,x27
	b.le	.out
	mov	x0,0
	mov	x1,x27
	adr	x9,ui_w
	ldr	x2,[x9]
	sub	x3,x28,x27
	bl	ui_update
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ligne de l'ecran, x1 = premiere colonne, x2 = fin (exclue),
; x3 = cellules, x4 = attributs, x5 = couleurs (texte | fond << 32)
draw_run:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	w23,w4
	mov	x24,x5
	mov	w0,w24				; texte ; gras : 0-7 -> 8-15 (comme xterm)
	tbz	w23,0,.nb
	lsr	w1,w0,24
	cmp	w1,2
	b.ne	.nb
	and	w1,w0,0xFF
	cmp	w1,8
	b.hs	.nb
	add	w0,w0,8
.nb:	movl	w1,COL_TXT
	bl	col_rgb
	mov	w25,w0
	lsr	x0,x24,32
	movl	w1,COL_BG
	bl	col_rgb
	mov	w26,w0
	tbz	w23,5,.nr			; inverse
	mov	w0,w25
	mov	w25,w26
	mov	w26,w0
.nr:	tbz	w23,1,.nd			; atténué : moitie du chemin vers le fond
	movl	w2,0xFEFEFE
	and	w0,w25,w2
	and	w1,w26,w2
	lsr	w0,w0,1
	lsr	w1,w1,1
	add	w25,w0,w1
.nd:	tbz	w23,6,.nh			; cache
	mov	w25,w26
.nh:	; texte de la plage en UTF-8 (0 : espace)
	mov	x6,x20
	adr	x7,textbuf
	mov	x8,0				; 1 : autre chose que des espaces
.tl:	cmp	x6,x21
	b.hs	.te
	add	x9,x22,x6,lsl 4
	ldr	w0,[x9]
	add	x6,x6,1
	cmp	w0,0x20
	b.hi	.ns
	mov	w0,0x20
	b	.a1
.ns:	mov	x8,1
	cmp	w0,0x80
	b.hs	.m2
.a1:	strb	w0,[x7],1
	b	.tl
.m2:	cmp	w0,0x800
	b.hs	.m3
	lsr	w1,w0,6
	orr	w1,w1,0xC0
	strb	w1,[x7],1
	b	.cont
.m3:	mov	w2,0x10000
	cmp	w0,w2
	b.hs	.m4
	lsr	w1,w0,12
	orr	w1,w1,0xE0
	strb	w1,[x7],1
	b	.c1
.m4:	lsr	w1,w0,18
	orr	w1,w1,0xF0
	strb	w1,[x7],1
	lsr	w1,w0,12
	and	w1,w1,0x3F
	orr	w1,w1,0x80
	strb	w1,[x7],1
.c1:	lsr	w1,w0,6
	and	w1,w1,0x3F
	orr	w1,w1,0x80
	strb	w1,[x7],1
.cont:	and	w1,w0,0x3F
	orr	w1,w1,0x80
	strb	w1,[x7],1
	b	.tl
.te:	strb	wzr,[x7]
	mov	x24,x8
	mov	x0,x20				; rectangle de la plage
	bl	col_x
	mov	x20,x0
	mov	x0,x21
	bl	col_x
	mov	x21,x0
	mov	x9,LH
	mul	x22,x19,x9
	add	x22,x22,PAD
	mov	x0,x20
	mov	x1,x22
	sub	x2,x21,x20
	mov	x3,LH
	mov	w4,w26
	bl	ui_fill
	cbz	x24,.lines
	mov	x0,x20				; texte decoupe a la plage : un accent ou
	mov	x1,x22				; une descente ne deborde pas sur la ligne
	sub	x2,x21,x20			; voisine (qui ne serait pas redessinee)
	mov	x3,LH
	bl	ui_clip
	adr	x5,ui_font_m
	tbz	w23,0,.f
	adr	x9,font_bold
	ldr	x5,[x9]
.f:	adr	x0,textbuf
	mov	x1,x20
	add	x2,x22,LH-4
	mov	w3,w25
	mov	x4,FONT_PX
	sub	x6,x21,x20
	bl	ui_text
	mov	x0,0
	mov	x1,0
	mov	x2,0
	mov	x3,0
	bl	ui_clip
.lines:	tbz	w23,3,.nu			; souligne
	mov	x0,x20
	add	x1,x22,LH-2
	sub	x2,x21,x20
	mov	x3,1
	mov	w4,w25
	bl	ui_fill
.nu:	tbz	w23,7,.out			; barre
	mov	x0,x20
	add	x1,x22,LH/2
	sub	x2,x21,x20
	mov	x3,1
	mov	w4,w25
	bl	ui_fill
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; palette de base (comme le terminal de VS Code) et niveaux du cube
macro pal_data {
		align 4
pal16	dw 0x000000, 0xCD3131, 0x0DBC79, 0xE5E510, 0x2472C8, 0xBC3FBC, 0x11A8CD, 0xE5E5E5
	dw 0x666666, 0xF14C4C, 0x23D18B, 0xF5F543, 0x3B8EEA, 0xD670D6, 0x29B8DB, 0xFFFFFF
pal_lv	db 0, 95, 135, 175, 215, 255
		align 4
palette	rb 256*4
}
