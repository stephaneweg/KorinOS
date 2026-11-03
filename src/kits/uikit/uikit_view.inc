; ============================================================================
;  kits/uikit_view.inc - UIKit : la vue dessinee a la main
;
;  Un widget dont le programme fait lui-meme le contenu : un document, une
;  grille, une page. Il a ses pixels, redessines par la fonction du programme
;  quand il est marque (ui_view_dirty), recopies sinon ; UIKit lui apporte la
;  place dans la fenetre, le survol, la capture du glisser, le double-clic,
;  le focus, et ses enfants (ui_w_set_parent) par-dessus.
;
;	dessin (vue, largeur, hauteur)		les ui_* vont dans ses pixels
;	souris (vue, genre, x, y, infos)	genre : 0 deplacement, 1 appui,
;						2 relache, 3 sortie ; infos : clics
;						| boutons << 8 | mods << 16 -> UN_*
;	touche (vue, code HID, caractere, mods)	-> -1 : laissee au programme
;	molette (vue, crans)			-> UN_*
;
;  La taille suit ui_w_move. ui_view_flush, a chaque tour de la boucle, dessine
;  ce qui est marque ; ui_w_user donne la donnee de ui_w_on.
; ============================================================================

UW_VWPAINT	= UW_MIN			; (les quatre fonctions)
UW_VWMOUSE	= UW_MAX
UW_VWKEY	= UW_PAGE
UW_VWWHEEL	= UW_POS
UW_VWDIRTY	= UW_DRAG

; x0..x3 = rectangle, x4 = UVW_* -> vue
ui_view:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_VIEW
	tbz	x19,0,.new
	mov	x0,UT_VIEWF
.new:	bl	uk_w_new
	ldp	x19,x20,[sp],16
	epilogue

; x0 = vue, x1 = dessin, x2 = souris, x3 = touche, x4 = molette (0 : aucune)
ui_view_on:
	cbz	x0,.ret
	stp	x1,x2,[x0,UW_VWPAINT]
	stp	x3,x4,[x0,UW_VWKEY]
	mov	x1,1
	str	x1,[x0,UW_VWDIRTY]
.ret:	ret

; x0 = vue : a redessiner
ui_view_dirty:
	cbz	x0,.ret
	mov	x1,1
	str	x1,[x0,UW_VWDIRTY]
.ret:	ret

; dessine et montre les vues marquees de la fenetre courante
ui_view_flush:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	uk_win
	mov	x20,x0
	adr	x19,uk_wfirst
	ldr	x19,[x19]
.l:	cbz	x19,.out
	ldr	x0,[x19,UW_WIN]
	cmp	x0,x20
	b.ne	.n
	ldr	w0,[x19,UW_TYPE]
	cmp	w0,UT_VIEW
	b.lo	.n
	ldr	x0,[x19,UW_VWDIRTY]
	cbz	x0,.n
	mov	x0,x19
	bl	uk_w_draw
.n:	ldr	x19,[x19,UW_NEXT]
	b	.l
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = vue : ses pixels a sa taille (VT_LAYOUT, et avant un dessin)
uk_vw_fit:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldp	w1,w2,[x19,UW_W]
	ldr	x3,[x19,UW_AUX]
	cbz	x3,.new
	ldp	x4,x5,[x3]
	cmp	x4,x1
	b.ne	.new
	cmp	x5,x2
	b.eq	.out
.new:	mov	x0,x3
	bl	ui_image_free
	str	xzr,[x19,UW_AUX]
	ldp	w0,w1,[x19,UW_W]
	bl	uk_img_new
	str	x0,[x19,UW_AUX]
	mov	x0,1
	str	x0,[x19,UW_VWDIRTY]
.out:	ldp	x19,x20,[sp],16
	epilogue

uk_vw_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	uk_vw_fit
	ldr	x3,[x19,UW_AUX]
	cbz	x3,.out
	ldr	x0,[x19,UW_VWDIRTY]
	cbz	x0,.blit
	str	xzr,[x19,UW_VWDIRTY]
	ldr	x20,[x19,UW_VWPAINT]
	cbz	x20,.blit
	ldp	x1,x2,[x3]
	add	x0,x3,IMG_PIX
	bl	uk_target
	mov	x0,x19
	ldp	w1,w2,[x19,UW_W]
	blr	x20
	bl	uk_target_back
.blit:	mov	x0,x19
	bl	uk_cv_draw
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = vue, x1 = genre, x2, x3 = point, x4 = clics -> UN_*
uk_vw_mouse:
	ldr	x9,[x0,UW_VWMOUSE]
	cbz	x9,.none
	adr	x10,uk_vars
	ldr	x10,[x10]
	add	x10,x10,UV_EV
	ldrb	w11,[x10,16]			; boutons
	orr	x4,x4,x11,lsl 8
	ldr	w11,[x10,EV_MODS]
	orr	x4,x4,x11,lsl 16
	br	x9
.none:	mov	x0,0
	ret

; x0 = vue, x1 = code HID, x2 = caractere, x3 = mods -> UN_*, -1 : pas prise
uk_vw_key:
	ldr	x9,[x0,UW_VWKEY]
	cbz	x9,.none
	br	x9
.none:	mov	x0,-1
	ret

; x0 = vue, x1 = crans -> UN_*
uk_vw_wheel:
	ldr	x9,[x0,UW_VWWHEEL]
	cbz	x9,.none
	br	x9
.none:	mov	x0,0
	ret
