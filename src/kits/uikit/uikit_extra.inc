; ============================================================================
;  kits/uikit_extra.inc - UIKit : mise en page, canevas, dessin vectoriel
;  anticrenele, indications de manette, navigation entre objets
;
;	ui_layout	piles et grille (Onyx layout.h) : place les widgets d'un
;			panneau
;	UT_CANVAS	canevas (Onyx canvas.h) : un tampon de pixels a soi, ou
;			les dessins de UIKit peuvent aller (ui_canvas_begin)
;	ui_vp_*		formes anticrenelees (Onyx vpaint.h) : disque, trait
;			epais, polygone (coordonnees en 1/16 de pixel)
;	ui_pad_hints	indications de manette (Onyx padhint.h : « (A) choisir »)
;	ui_objnav	l'objet voisin dans une direction (Onyx objnav.h)
; ============================================================================

; ---------------------------------------------------------------------------
;  Mise en page
; ---------------------------------------------------------------------------

; x0 = panneau, x1 = genre (ULY_VSTACK : colonne, pleine largeur, chacun
; garde sa hauteur ; ULY_HSTACK : ligne, pleine hauteur, chacun garde sa
; largeur ; ULY_GRID : grille de x3 colonnes egales, rangees de la plus haute
; de leurs cases), x2 = ecart (et marge), x3 = colonnes : place les widgets
; du panneau (ordre de creation) ; ne redessine pas
ui_layout:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	mov	x20,x1
	mov	x21,x2
	cmp	x3,1
	mov	x0,1
	csel	x22,x0,x3,lt			; colonnes
	ldp	w23,w24,[x19,UW_X]		; curseur : x, y
	sxtw	x23,w23
	sxtw	x24,w24
	add	x23,x23,x21
	add	x24,x24,x21
	mov	x27,0				; colonne courante (grille)
	mov	x28,0				; hauteur de la rangee (grille)
	adr	x25,uk_wfirst
	ldr	x25,[x25]
.w:	cbz	x25,.out
	ldr	x0,[x25,UW_PARENT]
	cmp	x0,x19
	b.ne	.n
	ldp	w0,w1,[x19,UW_W]		; place utile du panneau
	sub	x0,x0,x21,lsl 1
	sub	x1,x1,x21,lsl 1
	cmp	x20,ULY_HSTACK
	b.eq	.h
	cmp	x20,ULY_GRID
	b.eq	.g
	mov	x0,x25				; colonne : pleine largeur
	mov	x1,x23
	mov	x2,x24
	ldp	w3,w4,[x19,UW_W]
	sub	x3,x3,x21,lsl 1
	ldr	w4,[x25,UW_H]
	add	x24,x24,x4
	add	x24,x24,x21
	bl	ui_w_move
	b	.n
.h:	mov	x0,x25				; ligne : pleine hauteur
	mov	x1,x23
	mov	x2,x24
	ldr	w3,[x25,UW_W]
	ldr	w4,[x19,UW_H]
	sub	x4,x4,x21,lsl 1
	add	x23,x23,x3
	add	x23,x23,x21
	bl	ui_w_move
	b	.n
.g:	sub	x2,x22,1			; grille : largeur d'une case
	mul	x2,x2,x21
	sub	x0,x0,x2
	udiv	x26,x0,x22
	ldp	w0,w1,[x19,UW_X]
	sxtw	x0,w0
	add	x1,x26,x21
	madd	x1,x27,x1,x0
	add	x1,x1,x21			; x de la case
	mov	x0,x25
	mov	x2,x24
	mov	x3,x26
	ldr	w4,[x25,UW_H]
	cmp	x4,x28
	csel	x28,x4,x28,hi
	bl	ui_w_move
	add	x27,x27,1
	cmp	x27,x22
	b.lo	.n
	mov	x27,0				; rangee suivante
	add	x24,x24,x28
	add	x24,x24,x21
	mov	x28,0
.n:	ldr	x25,[x25,UW_NEXT]
	b	.w
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Canevas
; ---------------------------------------------------------------------------

; x0..x3 = rectangle -> canevas (blanc ; pixels : ui_canvas_pixels ; la
; souris y rend UN_CHANGE, le point dans ui_ev)
ui_canvas:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_CANVAS
	bl	uk_w_new
	cbz	x0,.out
	mov	x19,x0
	ldp	w0,w1,[x19,UW_W]
	bl	uk_img_new			; (une image : largeur, hauteur, pixels)
	str	x0,[x19,UW_AUX]
	cbz	x0,.out0
	ldp	x1,x2,[x0]
	mul	x1,x1,x2
	add	x0,x0,IMG_PIX
	mov	w2,0x00FFFFFF
.f:	cbz	x1,.out0
	str	w2,[x0],4
	sub	x1,x1,1
	b	.f
.out0:	mov	x0,x19
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = canevas -> x0 = pixels (0x00RRGGBB), x1 = largeur, x2 = hauteur
ui_canvas_pixels:
	ldr	x3,[x0,UW_AUX]
	cbz	x3,.z
	ldp	x1,x2,[x3]
	add	x0,x3,IMG_PIX
	ret
.z:	mov	x0,0
	mov	x1,0
	mov	x2,0
	ret

; x0 = canevas : les dessins de UIKit (ui_fill, ui_text, ui_rbox, ui_vp_*,
; ui_image_draw...) vont dans ses pixels jusqu'a ui_canvas_end
ui_canvas_begin:
	ldr	x3,[x0,UW_AUX]
	cbz	x3,.ret
	ldp	x1,x2,[x3]
	add	x0,x3,IMG_PIX
	b	uk_target
.ret:	ret

; fin des dessins dans un canevas (le montrer : ui_w_draw)
ui_canvas_end:
	b	uk_target_back

uk_cv_draw:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,UW_AUX]
	cbz	x0,.out
	ldp	w1,w2,[x19,UW_X]
	sxtw	x1,w1
	sxtw	x2,w2
	mov	x3,0
	mov	x4,0
	bl	uk_blit
.out:	ldr	x19,[sp],16
	epilogue

; x0 = image, x1, x2 = place : copie opaque (taille reelle, decoupee)
uk_blit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,0				; ligne
.r:	ldr	x0,[x19,IMG_H]
	cmp	x22,x0
	b.hs	.out
	ldr	x0,[x19,IMG_W]			; une ligne : ui_fill pixel par pixel
	mul	x1,x22,x0			; serait lent ; copie directe, bornee
	add	x1,x19,x1,lsl 2
	add	x1,x1,IMG_PIX
	add	x2,x21,x22
	mov	x3,x20
	mov	x4,x0
	bl	uk_blit_row
	add	x22,x22,1
	b	.r
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = pixels source, x2 = y, x3 = x, x4 = largeur : une ligne copiee dans
; la surface (decoupe : surface, ui_clip) (feuille)
uk_blit_row:
	adr	x5,uk_vars
	ldr	x5,[x5]
	ldp	x6,x7,[x5,UV_W]			; bornes droite, bas
	mov	x8,0				; bornes gauche, haut
	mov	x9,0
	adr	x10,uk_clip_on
	ldr	x10,[x10]
	cbz	x10,.b
	adr	x10,uk_clip
	ldp	x8,x9,[x10]
	ldp	x11,x12,[x10,16]
	cmp	x11,x6
	csel	x6,x11,x6,lt
	cmp	x12,x7
	csel	x7,x12,x7,lt
.b:	cmp	x2,x9
	b.lt	.ret
	cmp	x2,x7
	b.ge	.ret
	ldr	x10,[x5,UV_W]
	ldr	x11,[x5,UV_SURF]
	mul	x12,x2,x10
	add	x11,x11,x12,lsl 2		; ligne de destination
	mov	x12,0
.p:	cmp	x12,x4
	b.hs	.ret
	add	x13,x3,x12
	cmp	x13,x8
	b.lt	.n
	cmp	x13,x6
	b.ge	.ret
	ldr	w14,[x1,x12,lsl 2]
	and	w14,w14,0x00FFFFFF
	str	w14,[x11,x13,lsl 2]
.n:	add	x12,x12,1
	b	.p
.ret:	ret

; ---------------------------------------------------------------------------
;  Formes anticrenelees (coordonnees en 1/16 de pixel)
; ---------------------------------------------------------------------------

; x0 = x, x1 = y (pixel), w2 = couleur, x3 = couverture 0..256 : melange un
; pixel (decoupe comme ui_pixel) (feuille)
uk_blend_px:
	cbz	x3,.ret
	adr	x4,uk_vars
	ldr	x4,[x4]
	ldp	x5,x6,[x4,UV_W]
	cmp	x0,x5
	b.hs	.ret
	cmp	x1,x6
	b.hs	.ret
	adr	x7,uk_clip_on
	ldr	x7,[x7]
	cbz	x7,.in
	adr	x7,uk_clip
	ldp	x8,x9,[x7]
	cmp	x0,x8
	b.lt	.ret
	cmp	x1,x9
	b.lt	.ret
	ldp	x8,x9,[x7,16]
	cmp	x0,x8
	b.ge	.ret
	cmp	x1,x9
	b.ge	.ret
.in:	ldr	x7,[x4,UV_SURF]
	madd	x8,x1,x5,x0
	add	x7,x7,x8,lsl 2
	ldr	w8,[x7]
	mov	w9,0
	mov	w10,0
	mov	w13,256
	sub	w13,w13,w3
.c:	lsr	w11,w2,w10
	and	w11,w11,0xFF
	lsr	w12,w8,w10
	and	w12,w12,0xFF
	mul	w11,w11,w3
	madd	w11,w12,w13,w11
	lsr	w11,w11,8
	lsl	w11,w11,w10
	orr	w9,w9,w11
	add	w10,w10,8
	cmp	w10,24
	b.lo	.c
	str	w9,[x7]
.ret:	ret

; x0 = cx, x1 = cy, x2 = rayon (1/16 px), w3 = couleur : disque anticrenele
ui_vp_disc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	sub	x23,x20,x21			; lignes
	asr	x23,x23,4
	sub	x23,x23,1
.y:	add	x0,x20,x21
	asr	x0,x0,4
	add	x0,x0,1
	cmp	x23,x0
	b.gt	.out
	sub	x24,x19,x21
	asr	x24,x24,4
	sub	x24,x24,1
.x:	add	x0,x19,x21
	asr	x0,x0,4
	add	x0,x0,1
	cmp	x24,x0
	b.gt	.ny
	lsl	x0,x24,4			; distance du centre du pixel (1/16)
	add	x0,x0,8
	sub	x0,x0,x19
	lsl	x1,x23,4
	add	x1,x1,8
	sub	x1,x1,x20
	scvtf	d0,x0
	scvtf	d1,x1
	fmul	d0,d0,d0
	fmadd	d0,d1,d1,d0
	fsqrt	d0,d0
	scvtf	d1,x21				; couverture : (r + 8 - d) * 16, bornee
	fsub	d0,d1,d0
	fcvtzs	x3,d0
	add	x3,x3,8
	lsl	x3,x3,4
	cmp	x3,0
	b.le	.nx
	mov	x4,256
	cmp	x3,x4
	csel	x3,x4,x3,gt
	mov	x0,x24
	mov	x1,x23
	mov	w2,w22
	bl	uk_blend_px
.nx:	add	x24,x24,1
	b	.x
.ny:	add	x23,x23,1
	b	.y
.out:	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = debut, x2, x3 = fin, x4 = epaisseur (1/16 px), w5 = couleur :
; trait anticrenele a bouts ronds
ui_vp_line:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	str	d10,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	lsr	x23,x4,1			; demi-epaisseur
	mov	w24,w5
	cmp	x19,x21				; boite : min, max +/- demi-epaisseur
	csel	x25,x19,x21,lt
	sub	x25,x25,x23
	asr	x25,x25,4
	sub	x25,x25,1			; x de depart
	cmp	x20,x22
	csel	x26,x20,x22,lt
	sub	x26,x26,x23
	asr	x26,x26,4
	sub	x26,x26,1			; y
	sub	x0,x21,x19			; d = fin - debut, |d|^2
	sub	x1,x22,x20
	scvtf	d8,x0
	scvtf	d9,x1
	fmul	d10,d8,d8
	fmadd	d10,d9,d9,d10
.y:	cmp	x20,x22
	csel	x0,x22,x20,lt
	add	x0,x0,x23
	asr	x0,x0,4
	add	x0,x0,1
	cmp	x26,x0
	b.gt	.out
	cmp	x19,x21
	csel	x27,x19,x21,lt
	sub	x27,x27,x23
	asr	x27,x27,4
	sub	x27,x27,1
.x:	cmp	x19,x21
	csel	x0,x21,x19,lt
	add	x0,x0,x23
	asr	x0,x0,4
	add	x0,x0,1
	cmp	x27,x0
	b.gt	.ny
	lsl	x0,x27,4			; p - debut
	add	x0,x0,8
	sub	x0,x0,x19
	lsl	x1,x26,4
	add	x1,x1,8
	sub	x1,x1,x20
	scvtf	d0,x0
	scvtf	d1,x1
	fmul	d2,d0,d8			; t = (p.d) / |d|^2, borne a [0, 1]
	fmadd	d2,d1,d9,d2
	fmov	d3,xzr
	fcmp	d10,d3
	b.eq	.t0
	fdiv	d2,d2,d10
	fmax	d2,d2,d3
	fmov	d3,1.0
	fmin	d2,d2,d3
	b	.t1
.t0:	fmov	d2,xzr
.t1:	fmsub	d0,d2,d8,d0			; p - t d
	fmsub	d1,d2,d9,d1
	fmul	d0,d0,d0
	fmadd	d0,d1,d1,d0
	fsqrt	d0,d0
	scvtf	d1,x23
	fsub	d0,d1,d0
	fcvtzs	x3,d0
	add	x3,x3,8
	lsl	x3,x3,4
	cmp	x3,0
	b.le	.nx
	mov	x4,256
	cmp	x3,x4
	csel	x3,x4,x3,gt
	mov	x0,x27
	mov	x1,x26
	mov	w2,w24
	bl	uk_blend_px
.nx:	add	x27,x27,1
	b	.x
.ny:	add	x26,x26,1
	b	.y
.out:	ldr	d10,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = points (x, y en 1/16 de pixel, 32 bits chacun), x1 = nombre, w2 =
; couleur : polygone plein anticrenele (regle pair-impair, 4 x 4 echantillons
; par pixel)
ui_vp_poly:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	cmp	x20,3
	b.lo	.out
	ldp	w22,w23,[x19]			; boite
	sxtw	x22,w22
	sxtw	x23,w23
	mov	x24,x22
	mov	x25,x23
	mov	x0,1
.bb:	cmp	x0,x20
	b.hs	.bbd
	add	x1,x19,x0,lsl 3
	ldp	w2,w3,[x1]
	sxtw	x2,w2
	sxtw	x3,w3
	cmp	x2,x22
	csel	x22,x2,x22,lt
	cmp	x2,x24
	csel	x24,x2,x24,gt
	cmp	x3,x23
	csel	x23,x3,x23,lt
	cmp	x3,x25
	csel	x25,x3,x25,gt
	add	x0,x0,1
	b	.bb
.bbd:	asr	x22,x22,4
	asr	x23,x23,4
	asr	x24,x24,4
	asr	x25,x25,4
	mov	x26,x23				; y
.y:	cmp	x26,x25
	b.gt	.out
	mov	x27,x22				; x
.x:	cmp	x27,x24
	b.gt	.ny
	mov	x28,0				; echantillons dedans
	mov	x6,0				; 4 x 4 : (2, 6, 10, 14) / 16
.sy:	cmp	x6,4
	b.hs	.cov
	mov	x7,0
.sx:	cmp	x7,4
	b.hs	.nsy
	lsl	x0,x27,4
	add	x0,x0,x7,lsl 2
	add	x0,x0,2
	lsl	x1,x26,4
	add	x1,x1,x6,lsl 2
	add	x1,x1,2
	bl	uk_poly_in
	add	x28,x28,x0
	add	x7,x7,1
	b	.sx
.nsy:	add	x6,x6,1
	b	.sy
.cov:	cbz	x28,.nx
	lsl	x3,x28,4			; 16 echantillons -> 0..256
	mov	x0,x27
	mov	x1,x26
	mov	w2,w21
	bl	uk_blend_px
.nx:	add	x27,x27,1
	b	.x
.ny:	add	x26,x26,1
	b	.y
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ui_vp_poly) x0, x1 = point (1/16) -> x0 = 1 dedans (x19 points, x20
; nombre ; regle pair-impair) ; ne modifie que x0-x5, x8-x15
uk_poly_in:
	mov	x8,0				; dedans
	sub	x9,x20,1			; j = dernier
	mov	x10,0				; i
.e:	cmp	x10,x20
	b.hs	.end
	add	x11,x19,x10,lsl 3
	ldp	w12,w13,[x11]			; xi, yi
	sxtw	x12,w12
	sxtw	x13,w13
	add	x11,x19,x9,lsl 3
	ldp	w14,w15,[x11]			; xj, yj
	sxtw	x14,w14
	sxtw	x15,w15
	cmp	x13,x1				; (yi > y) != (yj > y)
	cset	x2,gt
	cmp	x15,x1
	cset	x3,gt
	cmp	x2,x3
	b.eq	.nx
	sub	x2,x14,x12			; x < xi + (xj - xi) (y - yi) / (yj - yi)
	sub	x3,x1,x13
	mul	x2,x2,x3
	sub	x3,x15,x13
	sdiv	x2,x2,x3
	add	x2,x2,x12
	cmp	x0,x2
	b.ge	.nx
	eor	x8,x8,1
.nx:	mov	x9,x10
	add	x10,x10,1
	b	.e
.end:	mov	x0,x8
	ret

; ---------------------------------------------------------------------------
;  Indications de manette
; ---------------------------------------------------------------------------

; x0 = indications (« A<tab>choisir<lf>B<tab>retour... » : un bouton, son
; role), x1 = x, x2 = y, x3 = hauteur, x4 = 1 mesurer seulement -> x0 =
; largeur. Bouton : pastille ronde (A, B, X, Y), arrondie (L, R, Start...)
; ou croix (« + » : la croix directionnelle) ; texte TEXTE2.
ui_pad_hints:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1				; x courant
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x24,x1				; depart
.h:	ldrb	w0,[x19]
	cbz	w0,.end
	adr	x25,uk_fitbuf			; le bouton
	mov	x26,0
.b:	ldrb	w0,[x19],1
	cbz	w0,.bend
	cmp	w0,9
	b.eq	.bend
	strb	w0,[x25,x26]
	add	x26,x26,1
	cmp	x26,7
	b.lo	.b
.bend:	strb	wzr,[x25,x26]
	sub	x0,x22,6			; pastille : largeur = hauteur, ou texte
	cmp	x26,1
	b.ls	.pw
	stp	x0,x0,[sp,-16]!
	mov	x0,x25
	mov	x1,11
	bl	uk_bold_w
	add	x0,x0,10
	ldp	x1,x2,[sp],16
	cmp	x0,x1
	csel	x0,x1,x0,lo
.pw:	mov	x26,x0				; largeur de la pastille
	cbnz	x23,.lab
	ldrb	w0,[x25]			; « + » : croix directionnelle
	cmp	w0,'+'
	b.ne	.pill
	ldrb	w0,[x25,1]
	cbnz	w0,.pill
	lsr	x0,x22,1			; deux barres
	sub	x0,x0,4
	add	x1,x21,3
	add	x0,x20,x0
	mov	x2,8
	sub	x3,x22,6
	mov	x4,2
	jet	w5,TJ_TEXTE2
	mov	w6,w5
	bl	ui_rbox
	add	x0,x20,0
	lsr	x1,x22,1
	sub	x1,x1,4
	add	x1,x21,x1
	sub	x2,x22,6
	add	x0,x0,3
	mov	x3,8
	mov	x4,2
	jet	w5,TJ_TEXTE2
	mov	w6,w5
	bl	ui_rbox
	b	.lab
.pill:	mov	x0,x20				; pastille TEXTE2, lettre FACE
	add	x1,x21,3
	mov	x2,x26
	sub	x3,x22,6
	lsr	x4,x3,1
	jet	w5,TJ_TEXTE2
	mov	w6,w5
	bl	ui_rbox
	mov	x0,x25
	mov	x1,11
	bl	uk_bold_w
	sub	x1,x26,x0
	add	x1,x20,x1,lsr 1
	str	x1,[sp,-16]!
	bl	uk_bold
	mov	x5,x0
	ldr	x1,[sp],16
	mov	x0,x25
	mov	x2,x21
	mov	x3,x22
	jet	w4,TJ_FACE
	mov	x6,0
	mov	x7,11
	bl	uk_text_box
.lab:	add	x20,x20,x26			; le role
	add	x20,x20,5
	adr	x25,uk_fitbuf
	mov	x26,0
.l:	ldrb	w0,[x19]
	cbz	w0,.lend
	add	x19,x19,1
	cmp	w0,10
	b.eq	.lend
	strb	w0,[x25,x26]
	add	x26,x26,1
	cmp	x26,200
	b.lo	.l
.lend:	strb	wzr,[x25,x26]
	bl	uk_met
	ldr	x1,[x0,MT_SMALL]
	str	x1,[sp,-16]!
	bl	uk_font_r
	mov	x2,x0
	ldr	x1,[sp]
	mov	x0,x25
	bl	ui_text_width
	str	x0,[sp,8]
	cbnz	x23,.adv
	bl	uk_font_r
	mov	x5,x0
	mov	x0,x25
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	jet	w4,TJ_TEXTE2
	mov	x6,0
	ldr	x7,[sp]
	bl	uk_text_box
.adv:	ldr	x0,[sp,8]
	add	sp,sp,16
	add	x20,x20,x0
	add	x20,x20,14			; entre deux indications
	b	.h
.end:	sub	x0,x20,x24
	subs	x0,x0,14
	csel	x0,xzr,x0,lt
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = taille -> x0 = largeur en gras
uk_bold_w:
	prologue
	stp	x0,x1,[sp,-16]!
	bl	uk_bold
	mov	x2,x0
	ldp	x0,x1,[sp],16
	bl	ui_text_width
	epilogue

; ---------------------------------------------------------------------------
;  Navigation entre objets (croix directionnelle, sans pointeur)
; ---------------------------------------------------------------------------

; x0 = rectangles (x, y, l, h : 32 bits chacun), x1 = nombre, x2 = objet
; courant, x3 = direction (0 gauche, 1 droite, 2 haut, 3 bas) -> x0 =
; l'objet le plus proche dans cette direction (centre a centre, l'ecart de
; cote compte double ; -1 : aucun)
ui_objnav:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x3
	mov	x0,-1
	cmp	x2,x20
	b.hs	.out
	add	x4,x19,x2,lsl 4			; centre du courant (x2, x2 garde)
	ldp	w5,w6,[x4]
	ldp	w7,w8,[x4,8]
	add	x22,x5,x7,lsr 1
	add	x23,x6,x8,lsr 1
	mov	x24,-1				; meilleur cout
	mov	x9,0
	mov	x10,-1
.l:	cmp	x9,x20
	b.hs	.done
	cmp	x9,x2
	b.eq	.n
	add	x4,x19,x9,lsl 4
	ldp	w5,w6,[x4]
	ldp	w7,w8,[x4,8]
	add	x5,x5,x7,lsr 1			; son centre
	add	x6,x6,x8,lsr 1
	sub	x11,x5,x22			; dx, dy
	sub	x12,x6,x23
	cmp	x21,1
	b.hi	.vert
	mov	x13,x11				; gauche : dx < 0 ; droite : dx > 0
	cbnz	x21,.hc
	neg	x13,x11
.hc:	cmp	x13,0
	b.le	.n
	cmp	x12,0
	cneg	x14,x12,lt
	b	.cost
.vert:	mov	x13,x12				; haut : dy < 0 ; bas : dy > 0
	cmp	x21,2
	b.ne	.vc
	neg	x13,x12
.vc:	cmp	x13,0
	b.le	.n
	cmp	x11,0
	cneg	x14,x11,lt
.cost:	add	x13,x13,x14,lsl 1
	cmp	x13,x24
	b.hs	.n
	mov	x24,x13
	mov	x10,x9
.n:	add	x9,x9,1
	b	.l
.done:	mov	x0,x10
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris d'un canevas : UN_CHANGE (le point et le genre dans ui_ev)
uk_cv_mouse:
	mov	x0,UN_CHANGE
	cmp	x1,3
	b.ne	.ret
	mov	x0,0
.ret:	ret

; x0 = x, x1 = y (dans la fenetre), x2 = libelles, x3 = largeur -> comme
; ui_popup (menu contextuel au point d'un clic)
ui_popup_local:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	bl	uk_win_pos
	add	x0,x0,x19
	add	x1,x1,x20
	mov	x2,x21
	mov	x3,x22
	bl	ui_popup
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
