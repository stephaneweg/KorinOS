; ============================================================================
;  gamefb.inc - cadre commun des jeux qui dessinent leurs pixels (invaders,
;  arkanoid). Le jeu a une taille logique fixe GF_W x GF_H ; ses images sont
;  faites dans un tampon, puis mises a l'echelle (proportions gardees, plus
;  proche voisin) dans une surface attachee a toute la fenetre.
;
;  Le canevas (gf_layer, ses pixels) est la couche fixe du jeu : le decor et
;  les textes y sont dessines avec UIKit (entre ui_canvas_begin et _end) ;
;  chaque image en repart (gf_copy) et y ajoute ce qui bouge.
;
;  Le jeu definit GF_W, GF_H (largeurs multiples de 8), puis apres ses
;  kit_use : include gamefb.inc ; gf_data apres ui_data, en toute fin.
;  Boucle : gf_run(evenement, pas, dessin) ; pas : 60 fois par seconde.
; ============================================================================

GF_PITCH	= GF_W*4
GF_FRAMES	= 60
GF_MAXW		= 4096

macro ldv reg, var {
	adr	x16,var
	ldr	reg,[x16]
}
macro stv reg, var {
	adr	x16,var
	str	reg,[x16]
}

; x0 = titre, x1, x2 = taille minimale -> x0 < 0 : pas de fenetre
gf_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x0,ui_win
	bl	ui_init
	mov	x0,-1
	mov	x1,-1
	mov	x2,GF_W
	mov	x3,GF_H
	mov	x4,x19
	mov	x5,WF_RESIZABLE or WF_GLYPH_LIGHT or WF_BODY_DARK
	bl	ui_window_ex
	tbnz	x0,63,.out
	bl	ui_jetons
	stv	x0,gf_tok
	mov	x0,x20
	mov	x1,x21
	bl	ui_min_size
	adr	x0,gf_layout
	bl	ui_on_resize
	mov	x0,0				; (sous la surface, jamais vu)
	mov	x1,0
	mov	x2,GF_W
	mov	x3,GF_H
	bl	ui_canvas
	stv	x0,gf_cv
	bl	ui_canvas_pixels
	stv	x0,gf_layer
	adr	x0,gf_frame
	stv	x0,gf_dst
	sys	SYS_TIME_MS
	movl	x1,0x9E3779B97F4A7C15
	eor	x0,x0,x1
	stv	x0,gf_rng
	bl	gf_layout
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la surface couvre toute la fenetre ; l'image y est centree, proportions gardees
; (appelee aussi par ui_on_resize)
gf_layout:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x19,ui_w
	ldv	x20,ui_h
	mov	x21,x19				; largeur de l'image
	mov	x0,GF_H
	mul	x22,x19,x0
	mov	x0,GF_W
	udiv	x22,x22,x0			; hauteur
	cmp	x22,x20
	b.ls	.ok
	mov	x22,x20
	mov	x0,GF_W
	mul	x21,x20,x0
	mov	x0,GF_H
	udiv	x21,x21,x0
.ok:	mov	x0,GF_MAXW
	cmp	x21,x0
	csel	x21,x0,x21,hi
	adr	x0,gf_dw
	stp	x21,x22,[x0]
	sub	x1,x19,x21
	lsr	x1,x1,1
	sub	x2,x20,x22
	lsr	x2,x2,1
	stp	x1,x2,[x0,16]
	ldv	x0,gf_sf
	cbz	x0,.new
	mov	x1,0
	mov	x2,0
	mov	x3,x19
	mov	x4,x20
	bl	ui_surface_move
	b	.px
.new:	mov	x0,0
	mov	x1,0
	mov	x2,x19
	mov	x3,x20
	mov	x4,WS_SF_XRGB8888
	bl	ui_surface
	stv	x0,gf_sf
.px:	ldv	x0,gf_sf
	bl	ui_surface_pixels		; les bandes en noir
	cbz	x0,.map
	sub	x5,x1,x2,lsl 2
.cy:	mov	x6,x2
.cx:	str	wzr,[x0],4
	subs	x6,x6,1
	b.ne	.cx
	add	x0,x0,x5
	subs	x3,x3,1
	b.ne	.cy
.map:	adr	x0,gf_xmap			; colonne de chaque point de l'ecran
	mov	x1,0
	mov	x3,GF_W
.m:	cmp	x1,x21
	b.hs	.done
	mul	x2,x1,x3
	udiv	x2,x2,x21
	str	w2,[x0,x1,lsl 2]
	add	x1,x1,1
	b	.m
.done:	mov	x0,1
	stv	x0,gf_redraw
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source, x2 = octets (multiple de 32)
gf_copy:
	ldp	q0,q1,[x1],32
	stp	q0,q1,[x0],32
	subs	x2,x2,32
	b.hi	gf_copy
	ret

; x0 = image GF_W x GF_H : a l'ecran
gf_present:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldv	x0,gf_sf
	cbz	x0,.out
	bl	ui_surface_pixels
	cbz	x0,.out
	mov	x20,x1				; pas
	adr	x2,gf_dw
	ldp	x21,x22,[x2]
	ldp	x3,x4,[x2,16]
	madd	x0,x4,x20,x0
	add	x23,x0,x3,lsl 2			; destination
	cmp	x21,GF_W
	b.ne	.scale
	cmp	x22,GF_H
	b.ne	.scale
	mov	x24,GF_H
.row:	mov	x0,x23
	mov	x1,x19
	mov	x2,GF_PITCH
	bl	gf_copy
	add	x19,x19,GF_PITCH
	add	x23,x23,x20
	subs	x24,x24,1
	b.ne	.row
	b	.dmg
.scale:	mov	x24,0				; ligne de l'ecran
	mov	x6,-1				; ligne source d'avant
	mov	x8,GF_H
	mov	x9,GF_PITCH
.sy:	mul	x4,x24,x8
	udiv	x4,x4,x22
	cmp	x4,x6
	b.ne	.sn
	sub	x1,x23,x20			; la meme : recopie
	mov	x0,0
.sc:	ldr	w5,[x1,x0,lsl 2]
	str	w5,[x23,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,x21
	b.lo	.sc
	b	.sd
.sn:	mov	x6,x4
	madd	x1,x4,x9,x19
	adr	x2,gf_xmap
	mov	x0,0
.sx:	ldr	w3,[x2,x0,lsl 2]
	ldr	w5,[x1,x3,lsl 2]
	str	w5,[x23,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,x21
	b.lo	.sx
.sd:	add	x23,x23,x20
	add	x24,x24,1
	cmp	x24,x22
	b.lo	.sy
.dmg:	ldv	x0,gf_sf
	adr	x5,gf_dw
	ldp	x1,x2,[x5,16]
	ldp	x3,x4,[x5]
	bl	ui_surface_damage
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ui_ev (souris) -> x0, x1 = point dans l'image logique (borne)
gf_mouse:
	adr	x0,ui_ev
	ldrsw	x2,[x0,EV_X]
	ldrsw	x3,[x0,EV_Y]
	adr	x4,gf_dw
	ldp	x5,x6,[x4]
	ldp	x7,x8,[x4,16]
	sub	x2,x2,x7
	sub	x3,x3,x8
	mov	x7,GF_W
	mul	x0,x2,x7
	sdiv	x0,x0,x5
	mov	x8,GF_H
	mul	x1,x3,x8
	sdiv	x1,x1,x6
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	sub	x7,x7,1
	sub	x8,x8,1
	cmp	x0,x7
	csel	x0,x7,x0,gt
	cmp	x1,x8
	csel	x1,x8,x1,gt
	ret

; x0 = hauteur (logique) du bandeau : un appui dans ce bandeau, ailleurs que
; sous les boutons de la fenetre, la deplace -> x0 = 1 si c'est le cas
gf_drag_top:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	gf_mouse
	mov	x2,x1
	mov	x0,0
	cmp	x2,x19
	b.hs	.out
	adr	x1,ui_ev
	ldrsw	x1,[x1,EV_X]
	ldv	x2,ui_w
	sub	x2,x2,FRAME_CAP_W+8
	cmp	x1,x2
	b.ge	.out
	bl	ui_window_drag
	mov	x0,1
.out:	ldr	x19,[sp],16
	epilogue

; --- generateur ---------------------------------------------------------------
; -> x0 = pseudo-aleatoire (xorshift64 ; x1 touche)
gf_rand:
	adr	x1,gf_rng
	ldr	x0,[x1]
	eor	x0,x0,x0,lsl 13
	eor	x0,x0,x0,lsr 7
	eor	x0,x0,x0,lsl 17
	str	x0,[x1]
	ret

; x0 = borne -> x0 dans [0, borne[ (touche x1, x2)
gf_below:
	mov	x2,x0
	adr	x1,gf_rng
	ldr	x0,[x1]
	eor	x0,x0,x0,lsl 13
	eor	x0,x0,x0,lsr 7
	eor	x0,x0,x0,lsl 17
	str	x0,[x1]
	lsr	x0,x0,16
	udiv	x1,x0,x2
	msub	x0,x1,x2,x0
	ret

; --- dessin dans gf_dst -----------------------------------------------------
; x0, x1, x2, x3 = rectangle, w4 = couleur (decoupe)
gf_rect:
	add	x2,x2,x0
	add	x3,x3,x1
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	mov	x5,GF_W
	cmp	x2,x5
	csel	x2,x5,x2,gt
	mov	x5,GF_H
	cmp	x3,x5
	csel	x3,x5,x3,gt
	cmp	x0,x2
	b.ge	.out
	cmp	x1,x3
	b.ge	.out
	adr	x5,gf_dst
	ldr	x5,[x5]
	mov	x6,GF_PITCH
	madd	x5,x1,x6,x5
.y:	mov	x7,x0
.x:	str	w4,[x5,x7,lsl 2]
	add	x7,x7,1
	cmp	x7,x2
	b.lo	.x
	add	x5,x5,x6
	add	x1,x1,1
	cmp	x1,x3
	b.lo	.y
.out:	ret

; Sprite : dw largeur, hauteur, puis les points (0 : transparent).
; gf_sprite(x0 = x, x1 = y, x2 = sprite) : opaque ; gf_glow : ajoute la lumiere
macro gf_blit name, glow {
name:
	ldp	w3,w4,[x2],8			; largeur, hauteur
	mov	x5,0				; colonnes
	mov	x6,x3
	cmp	x0,0
	b.ge	.l1
	neg	x5,x0
.l1:	mov	x7,GF_W
	sub	x7,x7,x0
	cmp	x6,x7
	csel	x6,x7,x6,gt
	mov	x7,0				; lignes
	mov	x8,x4
	cmp	x1,0
	b.ge	.l2
	neg	x7,x1
.l2:	mov	x9,GF_H
	sub	x9,x9,x1
	cmp	x8,x9
	csel	x8,x9,x8,gt
	cmp	x7,x8
	b.ge	.out
	cmp	x5,x6
	b.ge	.out
	adr	x9,gf_dst
	ldr	x9,[x9]
	mov	x10,GF_PITCH
	add	x1,x1,x7
	madd	x9,x1,x10,x9
	add	x9,x9,x0,lsl 2			; destination, ligne x7
	lsl	x3,x3,2
	madd	x2,x7,x3,x2
.y:	mov	x11,x5
.x:	ldr	w12,[x2,x11,lsl 2]
	cbz	w12,.n
	add	x13,x9,x11,lsl 2
 if glow = 2
	ldr	w14,[x13]
	lsr	w14,w14,24
	cmp	w14,255
	b.eq	.n
 end if
 if glow
	ldr	s1,[x13]
	fmov	s0,w12
	uqadd	v0.8b,v0.8b,v1.8b
	str	s0,[x13]
 else
	str	w12,[x13]
 end if
.n:	add	x11,x11,1
	cmp	x11,x6
	b.lo	.x
	add	x2,x2,x3
	add	x9,x9,x10
	add	x7,x7,1
	cmp	x7,x8
	b.lo	.y
.out:	ret
}
gf_blit gf_sprite, 0
gf_blit gf_glow, 1
gf_blit gf_bglow, 2			; (le decor seul : pas sur les points d'octet haut 0xFF)

; x0 = texte -> x0 = sa traduction, x1..x5 gardes
gf_tr:
	prologue
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	str	x5,[sp,-16]!
	bl	ui_tr
	ldr	x5,[sp],16
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	epilogue

; x0 = texte, x1 = x, x2 = base, w3 = couleur, x4 = corps, x5 = police (dans la couche)
gf_text:
	prologue
	bl	gf_tr
	mov	x6,0
	bl	ui_text
	epilogue

; x0 = texte, x1 = x du centre, x2 = base, w3, x4, x5
gf_text_c:
	prologue
	bl	gf_tr
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	mov	x23,x4
	mov	x24,x5
	mov	x1,x4
	mov	x2,x5
	bl	ui_text_width
	sub	x1,x20,x0,asr 1
	mov	x0,x19
	mov	x2,x21
	mov	w3,w22
	mov	x4,x23
	mov	x5,x24
	mov	x6,0
	bl	ui_text
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- meilleur score -----------------------------------------------------------
; x0 = chemin -> x0 = valeur lue (0 : pas de fichier)
gf_load_best:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x20,0
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x1,sp
	mov	x2,31
	sys	SYS_READ
	mov	x2,x0
	mov	x0,x19
	sys	SYS_CLOSE
	mov	x1,sp
	mov	x4,0
	mov	x6,10
.d:	cmp	x4,x2
	b.ge	.out
	ldrb	w5,[x1,x4]
	sub	w5,w5,'0'
	cmp	w5,9
	b.hi	.out
	madd	x20,x20,x6,x5
	add	x4,x4,1
	b	.d
.out:	mov	x0,x20
	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dossier, x1 = chemin, x2 = valeur
gf_save_best:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x1
	mov	x20,x2
	sys	SYS_MKDIR
	mov	x0,x20
	mov	x1,sp
	bl	fmt_dec
	mov	w1,10
	strb	w1,[x0],1
	mov	x1,sp
	sub	x20,x0,x1
	mov	x0,x19
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x1,sp
	mov	x2,x20
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
.out:	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; --- la boucle -----------------------------------------------------------------
; x0 = evenement (x0 = type : WEV_KEY, WEV_MOUSE, WEV_MENU, WEV_FOCUS, WEV_PAD), x1 = pas,
; x2 = dessin (fait gf_frame). Rendue quand gf_quit est leve ou a la fermeture.
; gf_still = 1 : plus d'images (pause), sauf si gf_redraw est leve.
gf_run:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	sys	SYS_TIME_MS
	mov	x22,x0				; heure du dernier passage
	mov	x23,0				; reste (millemes de pas)
.loop:	ldv	x0,gf_quit
	cbnz	x0,.end
	ldv	x0,gf_still
	cbz	x0,.time
	ldv	x0,gf_redraw
	cbnz	x0,.show
	bl	gs_tick
	mov	x0,50
	bl	gf_pump
	sys	SYS_TIME_MS
	mov	x22,x0
	mov	x23,0
	b	.loop
.time:	sys	SYS_TIME_MS
	sub	x1,x0,x22
	mov	x22,x0
	mov	x2,GF_FRAMES
	madd	x23,x1,x2,x23
	mov	x2,1000
	udiv	x24,x23,x2
	msub	x23,x24,x2,x23
	cbnz	x24,.step
	mov	x0,2
	bl	gf_pump
	b	.loop
.step:	cmp	x24,5				; (trop de retard : on l'oublie)
	mov	x0,5
	csel	x24,x0,x24,hi
.s:	blr	x20
	subs	x24,x24,1
	b.ne	.s
	bl	gs_tick
.show:	stv	xzr,gf_redraw
	blr	x21
	adr	x0,gf_frame
	bl	gf_present
	bl	ui_frame_request		; la suivante quand celle-ci est prise
	cbz	x0,.loop
	sys	SYS_TIME_MS
	add	x24,x0,40
.w:	mov	x0,8
	bl	gf_pump
	cmp	x0,WEV_FRAME
	b.eq	.loop
	ldv	x0,gf_quit
	cbnz	x0,.end
	sys	SYS_TIME_MS
	cmp	x0,x24
	b.lo	.w
	b	.loop
.end:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = attente (ms), x19 = fonction du jeu : un evenement traite -> x0 = son type
gf_pump:
	prologue
	stp	x24,x25,[sp,-16]!
	bl	ui_event
	mov	x24,x0
	cbz	x0,.out
	cmp	x0,WEV_CLOSE
	b.ne	.r
	mov	x0,1
	stv	x0,gf_quit
	b	.out
.r:	cmp	x0,WEV_RESIZE
	b.ne	.k
	mov	x0,0
	bl	ui_w_event
	b	.out
.k:	cmp	x0,WEV_KEY
	b.ne	.f
	adr	x0,ui_ev			; touches tenues
	ldr	w1,[x0,EV_CODE]
	ldr	w2,[x0,EV_KIND]
	cmp	w1,256
	b.hs	.game
	adr	x3,gf_held
	cmp	w2,0
	cset	w2,ne
	strb	w2,[x3,x1]
	b	.game
.f:	cmp	x0,WEV_FOCUS
	b.ne	.m
	adr	x0,gf_held
	mov	x1,0
.z:	strb	wzr,[x0,x1]
	add	x1,x1,1
	cmp	x1,256
	b.lo	.z
	b	.game
.m:	cmp	x0,WEV_MOUSE
	b.eq	.game
	cmp	x0,WEV_PAD
	b.eq	.game
	cmp	x0,WEV_MENU
	b.ne	.out
.game:	mov	x0,x24
	blr	x19
.out:	mov	x0,x24
	ldp	x24,x25,[sp],16
	epilogue

macro gf_data {
		align 8
gf_tok		rd 1			; (tout en reserve : rien dans le fichier)
gf_cv		rd 1
gf_layer	rd 1			; pixels de la couche fixe
gf_sf		rd 1
gf_dst		rd 1			; ou dessinent gf_rect, gf_sprite
gf_rng		rd 1
gf_quit		rd 1
gf_still	rd 1
gf_redraw	rd 1
gf_dw		rd 4			; image a l'ecran : l, h, x, y
gf_held		rb 256
		align 16
gf_xmap		rw GF_MAXW
gf_frame	rb GF_W*GF_H*4
}
