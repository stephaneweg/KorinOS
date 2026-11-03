; ============================================================================
;  gameart.inc - dessin soigne des jeux en pixels, au-dessus de gamefb.inc :
;  sprites a couche alpha (premultipliee, octet haut), blocs et spheres generes,
;  panneaux de verre, texte pre-rendu (dessine sans UIKit a chaque image),
;  chiffres, particules.
;
;  Le jeu inclut ce fichier apres gamefb.inc ; ga_data avant gf_data ; bl ga_init
;  (x0 = octets pour les sprites) apres gf_open.
;
;  Les sprites de gamefb (gf_sprite, gf_glow) lisent le meme format : un point
;  nul est transparent. ga_ablit melange, ga_shadow assombrit selon l'alpha.
; ============================================================================

GA_NP		= 320			; particules
GA_PSIZE	= 32			; +0 x, +4 y (16.16), +8 vx, +12 vy, +16 vie,
					; +20 couleur, +24 taille, +28 gravite

; x0 = octets pour les sprites -> x0 = 0 si plus de memoire
ga_init:
	prologue
	bl	malloc
	adr	x1,ga_bp
	str	x0,[x1]
	cbz	x0,.out
	bl	ga_gen_noise
	mov	x0,1
.out:	epilogue

; x0 = largeur, x1 = hauteur -> x0 = sprite vide (rempli de 0 : la pile l'est)
ga_new:
	adr	x4,ga_bp
	ldr	x2,[x4]
	stp	w0,w1,[x2]
	mul	x3,x0,x1
	add	x3,x2,x3,lsl 2
	add	x3,x3,8
	str	x3,[x4]
	add	x5,x2,8
.z:	cmp	x5,x3
	b.hs	.out
	str	wzr,[x5],4
	b	.z
.out:	mov	x0,x2
	ret

; w0, w1 = couleurs, x2 = 0..256 vers w1 -> w0 (touche x3..x5)
ga_mix:
	mov	w3,256
	sub	w3,w3,w2
	and	w4,w0,0xFF00FF
	mul	w4,w4,w3
	and	w5,w1,0xFF00FF
	mul	w5,w5,w2
	add	w4,w4,w5
	lsr	w4,w4,8
	and	w4,w4,0xFF00FF
	and	w0,w0,0xFF00
	mul	w0,w0,w3
	and	w1,w1,0xFF00
	mul	w1,w1,w2
	add	w0,w0,w1
	lsr	w0,w0,8
	and	w0,w0,0xFF00
	orr	w0,w0,w4
	ret

; w0 = couleur, w1 = facteur (256 : tel quel, plus : plus clair) -> w0 (touche x2..x4)
ga_shade:
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

; x0 = phase -> x0 = 0..255, une onde en paraboles (touche x1)
ga_sin8:
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
	mov	x1,128
	sub	x0,x1,x0
	ret

; x0 = px, x1 = py dans un rectangle x2 x x3 arrondi de rayon x4 -> x0 = couverture
; 0..256 (touche x5..x7)
ga_cover:
	cbz	x4,.full
	sub	x5,x2,1
	sub	x5,x5,x0
	cmp	x0,x5
	csel	x5,x0,x5,lo
	sub	x6,x3,1
	sub	x6,x6,x1
	cmp	x1,x6
	csel	x6,x1,x6,lo
	cmp	x5,x4
	b.hs	.full
	cmp	x6,x4
	b.hs	.full
	lsl	x7,x4,1
	sub	x5,x7,x5,lsl 1
	sub	x5,x5,1
	sub	x6,x7,x6,lsl 1
	sub	x6,x6,1
	mul	x5,x5,x5
	madd	x5,x6,x6,x5
	add	x6,x7,1
	mul	x6,x6,x6
	cmp	x5,x6
	b.hs	.none
	sub	x6,x6,x5
	lsl	x6,x6,8
	lsl	x7,x7,2
	udiv	x0,x6,x7
	cmp	x0,256
	mov	x6,256
	csel	x0,x6,x0,hi
	ret
.none:	mov	x0,0
	ret
.full:	mov	x0,256
	ret

; dans gf_dst : rectangle x0..x3, rayon x4, degrade w5 -> w6, opacite x7 (0..256),
; w8 = couleur du filet (0 : aucun). Decoupe a l'image.
ga_panel:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	w24,w5
	mov	w25,w6
	mov	x26,x7
	mov	w27,w8
	mov	x9,0				; ligne
.y:	cmp	x9,x22
	b.hs	.end
	add	x0,x20,x9
	cmp	x0,GF_H
	b.hs	.ny				; (hors de l'image, negatif compris)
	mov	x1,GF_PITCH
	mul	x0,x0,x1
	ldv	x1,gf_dst
	add	x11,x1,x0			; debut de la ligne
	mov	w0,w24
	mov	w1,w25
	lsl	x2,x9,8
	udiv	x2,x2,x22
	bl	ga_mix
	mov	w12,w0				; couleur de la ligne
	mov	x10,0				; colonne
.x:	add	x13,x19,x10
	cmp	x13,GF_W
	b.hs	.nx
	mov	x0,x10
	mov	x1,x9
	mov	x2,x21
	mov	x3,x22
	mov	x4,x23
	bl	ga_cover
	mov	x14,x0				; couverture
	cbz	x14,.nx
	ldr	w0,[x11,x13,lsl 2]
	mov	w1,w12
	mul	x2,x14,x26
	lsr	x2,x2,8
	bl	ga_mix
	cbz	w27,.put
	mov	w15,w0
	mov	x0,0				; le filet : couverture moins celle de dedans
	cmp	x10,0
	b.eq	.ri
	cmp	x9,0
	b.eq	.ri
	sub	x1,x21,1
	cmp	x10,x1
	b.hs	.ri
	sub	x1,x22,1
	cmp	x9,x1
	b.hs	.ri
	sub	x0,x10,1
	sub	x1,x9,1
	sub	x2,x21,2
	sub	x3,x22,2
	mov	x4,x23
	cbz	x4,.i2
	sub	x4,x4,1
.i2:	bl	ga_cover
.ri:	subs	x2,x14,x0
	mov	w0,w15
	b.ls	.put
	mov	w1,w27
	bl	ga_mix
.put:	str	w0,[x11,x13,lsl 2]
.nx:	add	x10,x10,1
	cmp	x10,x21
	b.lo	.x
.ny:	add	x9,x9,1
	b	.y
.end:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0..x3 = rectangle : octet haut 0xFF dans gf_dst (premier plan : gf_bglow s'arrete)
ga_mark:
	adr	x5,gf_dst
	ldr	x5,[x5]
	mov	x6,GF_PITCH
	madd	x5,x1,x6,x5
	add	x5,x5,x0,lsl 2
.y:	mov	x7,0
.x:	ldr	w4,[x5,x7,lsl 2]
	orr	w4,w4,0xFF000000
	str	w4,[x5,x7,lsl 2]
	add	x7,x7,1
	cmp	x7,x2
	b.lo	.x
	add	x5,x5,x6
	subs	x3,x3,1
	b.ne	.y
	ret

macro ga_blit_m name, shadow {
name:
	ldp	w3,w4,[x2],8
	mov	x5,0
	mov	x6,x3
	cmp	x0,0
	b.ge	.l1
	neg	x5,x0
.l1:	mov	x7,GF_W
	sub	x7,x7,x0
	cmp	x6,x7
	csel	x6,x7,x6,gt
	mov	x7,0
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
	add	x9,x9,x0,lsl 2
	lsl	x3,x3,2
	madd	x2,x7,x3,x2
.y:	mov	x11,x5
.x:	ldr	w12,[x2,x11,lsl 2]
	lsr	w13,w12,24
	cbz	w13,.n
 if shadow
	lsr	w15,w13,1
	add	w13,w15,w13,lsr 2		; (trois quarts)
	mov	w12,0
 else
	cmp	w13,255
	b.ne	.bl
	str	w12,[x9,x11,lsl 2]
	b	.n
 end if
.bl:	ldr	w14,[x9,x11,lsl 2]
	mov	w15,256
	sub	w15,w15,w13
	and	w16,w14,0xFF00FF
	mul	w16,w16,w15
	lsr	w16,w16,8
	and	w16,w16,0xFF00FF
	and	w17,w14,0xFF00
	mul	w17,w17,w15
	lsr	w17,w17,8
	and	w17,w17,0xFF00
	orr	w16,w16,w17
	and	w12,w12,0xFFFFFF
	add	w12,w12,w16
	str	w12,[x9,x11,lsl 2]
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
ga_blit_m ga_ablit, 0
ga_blit_m ga_shadow, 1

; x0 = x du centre, x1 = y, x2 = sprite : centre en largeur
ga_ablit_c:
	ldr	w3,[x2]
	sub	x0,x0,x3,lsr 1
	b	ga_ablit

; x0 = sprite, x1 = facteur 0..256 : tout le sprite s'estompe
ga_fade:
	ldp	w2,w3,[x0],8
	mul	x2,x2,x3
.p:	ldr	w3,[x0]
	lsr	w4,w3,24
	mul	w4,w4,w1
	lsr	w4,w4,8
	ubfx	w5,w3,16,8
	mul	w5,w5,w1
	lsr	w5,w5,8
	ubfx	w6,w3,8,8
	mul	w6,w6,w1
	lsr	w6,w6,8
	and	w3,w3,0xFF
	mul	w3,w3,w1
	lsr	w3,w3,8
	orr	w3,w3,w6,lsl 8
	orr	w3,w3,w5,lsl 16
	orr	w3,w3,w4,lsl 24
	str	w3,[x0],4
	subs	x2,x2,1
	b.ne	.p
	ret

; ecrit un point premultiplie : x0 = adresse, w1 = couleur, x2 = alpha 0..256
ga_put:
	cmp	x2,256
	b.lo	.a
	orr	w1,w1,0xFF000000
	str	w1,[x0]
	ret
.a:	ubfx	w3,w1,16,8
	mul	w3,w3,w2
	lsr	w3,w3,8
	ubfx	w4,w1,8,8
	mul	w4,w4,w2
	lsr	w4,w4,8
	and	w1,w1,0xFF
	mul	w1,w1,w2
	lsr	w1,w1,8
	orr	w1,w1,w4,lsl 8
	orr	w1,w1,w3,lsl 16
	orr	w1,w1,w2,lsl 24
	str	w1,[x0]
	ret

; x0 = largeur, x1 = hauteur, x2 = rayon, w3 = couleur -> x0 = sprite : un bloc
; arrondi, bombe, reflet en haut, biseau clair en haut et a gauche, sombre en bas
ga_block:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	bl	ga_new
	mov	x26,x0
	add	x25,x0,8
	mov	x23,0				; y
.y:	mov	x24,0				; x
.x:	mov	x0,x24
	mov	x1,x23
	mov	x2,x19
	mov	x3,x20
	mov	x4,x21
	bl	ga_cover
	cbz	x0,.n
	mov	x9,x0
	mov	w0,120				; degrade
	mul	x1,x23,x0
	udiv	x1,x1,x20
	mov	x0,296
	sub	x1,x0,x1
	lsl	x2,x23,1			; reflet : le tiers haut, sans les bords
	cmp	x2,x20
	b.hs	.bev
	cmp	x24,2
	b.lo	.bev
	sub	x2,x19,3
	cmp	x24,x2
	b.hs	.bev
	mov	x2,x20
	lsr	x2,x2,1
	sub	x2,x2,x23
	lsl	x2,x2,2
	add	x1,x1,x2
.bev:	cmp	x23,1				; biseau
	b.hi	.b2
	add	x1,x1,120
.b2:	cmp	x24,1
	b.hi	.b3
	add	x1,x1,70
.b3:	sub	x2,x20,2
	cmp	x23,x2
	b.lo	.b4
	sub	x1,x1,90
.b4:	sub	x2,x19,2
	cmp	x24,x2
	b.lo	.c
	sub	x1,x1,60
.c:	mov	w0,w22
	bl	ga_shade
	mov	w1,w0
	mov	x2,x9
	mov	x0,x25
	bl	ga_put
.n:	add	x25,x25,4
	add	x24,x24,1
	cmp	x24,x19
	b.lo	.x
	add	x23,x23,1
	cmp	x23,x20
	b.lo	.y
	mov	x0,x26
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = diametre, w1 = couleur -> x0 = sprite : une sphere eclairee d'en haut a gauche
ga_ball:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x22,x0
	mov	w23,w1
	mov	x1,x0
	bl	ga_new
	mov	x19,x0
	add	x24,x0,8
	mov	x20,0				; y
.y:	mov	x21,0				; x
.x:	mov	x0,x21
	mov	x1,x20
	mov	x2,x22
	mov	x3,x22
	mov	x4,x22
	lsr	x4,x4,1
	bl	ga_cover
	cbz	x0,.n
	mov	x25,x0
	lsl	x0,x21,1			; centre a 2 * point + 1 - d, la lumiere decalee
	add	x0,x0,1
	sub	x0,x0,x22
	add	x0,x0,x22,lsr 1
	lsl	x1,x20,1
	add	x1,x1,1
	sub	x1,x1,x22
	add	x1,x1,x22,lsr 1
	mul	x2,x0,x0
	madd	x2,x1,x1,x2
	mov	x3,270
	mul	x2,x2,x3
	mul	x3,x22,x22
	udiv	x2,x2,x3			; 0 au reflet, grand a l'oppose
	mov	x3,420
	subs	x1,x3,x2
	mov	x3,70
	csel	x1,x3,x1,lo
	cmp	x1,x3
	csel	x1,x3,x1,lo
	mov	x26,x2
	mov	w0,w23
	bl	ga_shade
	cmp	x26,12				; le point blanc
	b.hs	.p
	mov	x3,12
	sub	x2,x3,x26
	lsl	x2,x2,7
	mov	x3,12
	udiv	x2,x2,x3
	movl	w1,0x00FFFFFF
	bl	ga_mix
.p:	mov	w1,w0
	mov	x2,x25
	mov	x0,x24
	bl	ga_put
.n:	add	x24,x24,4
	add	x21,x21,1
	cmp	x21,x22
	b.lo	.x
	add	x20,x20,1
	cmp	x20,x22
	b.lo	.y
	mov	x0,x19
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = diametre, w1 = couleur -> x0 = sprite : une lumiere ronde (pour gf_glow)
ga_disc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x22,x0
	mov	w23,w1
	mov	x1,x0
	bl	ga_new
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
	b.hs	.n
	mov	x3,256
	mul	x2,x2,x3
	udiv	x2,x2,x25
	sub	x1,x3,x2
	mul	x1,x1,x1
	lsr	x1,x1,8
	mov	w0,w23
	bl	ga_shade
	str	w0,[x24]
.n:	add	x24,x24,4
	add	x21,x21,1
	cmp	x21,x22
	b.lo	.x
	add	x20,x20,1
	cmp	x20,x22
	b.lo	.y
	mov	x0,x19
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- texte pre-rendu --------------------------------------------------------
; Entre ui_canvas_begin et ui_canvas_end, avant de dessiner la couche : le coin
; (0,0) du canevas sert de brouillon.
; x0 = texte, x1 = corps, x2 = police, w3 = couleur du haut, w4 = du bas -> x0 = sprite
ga_text:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	mov	w23,w4
	bl	ui_tr
	mov	x19,x0
	mov	x1,x20
	mov	x2,x21
	bl	ui_text_width
	add	x24,x0,4			; largeur
	mov	x0,GF_W
	cmp	x24,x0
	csel	x24,x0,x24,hi
	add	x25,x20,x20,lsr 2
	add	x25,x25,4			; hauteur
	mov	x0,0
	mov	x1,0
	mov	x2,x24
	mov	x3,x25
	mov	w4,0
	bl	ui_fill
	mov	x0,x19
	mov	x1,1
	mov	x2,x20
	add	x2,x2,1
	movl	w3,0x00FFFFFF
	mov	x4,x20
	mov	x5,x21
	mov	x6,0
	bl	ui_text
	mov	x0,x24
	mov	x1,x25
	bl	ga_new
	mov	x26,x0
	add	x19,x0,8
	ldv	x20,gf_layer
	mov	x9,0				; ligne
.y:	mov	w0,w22
	mov	w1,w23
	lsl	x2,x9,8
	udiv	x2,x2,x25
	bl	ga_mix
	mov	w21,w0
	mov	x10,0
.x:	ldr	w0,[x20,x10,lsl 2]
	and	x2,x0,0xFF
	cbz	x2,.n
	cmp	x2,250
	b.lo	.a
	mov	x2,256
.a:	mov	w1,w21
	add	x0,x19,x10,lsl 2
	bl	ga_put
.n:	add	x10,x10,1
	cmp	x10,x24
	b.lo	.x
	add	x19,x19,x24,lsl 2
	add	x20,x20,GF_PITCH
	add	x9,x9,1
	cmp	x9,x25
	b.lo	.y
	mov	x0,x26
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = table de 10 sprites, x1 = corps, x2 = police, w3, w4 = couleurs : les chiffres
ga_digits:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	mov	w23,w4
	mov	x24,0
.d:	adr	x0,ga_tmp
	add	x1,x24,'0'
	strb	w1,[x0]
	strb	wzr,[x0,1]
	mov	x1,x20
	mov	x2,x21
	mov	w3,w22
	mov	w4,w23
	bl	ga_text
	str	x0,[x19,x24,lsl 3]
	add	x24,x24,1
	cmp	x24,10
	b.lo	.d
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur, x1 = x, x2 = y, x3 = table des chiffres, x4 = 0 gauche, 1 centre,
; 2 droite (x est ce bord), 4 en plus : ombre
ga_num:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	adr	x19,ga_tmp
	mov	x1,x19
	bl	fmt_dec
	sub	x24,x0,x19			; chiffres
	ldr	x1,[x22]
	ldr	w1,[x1]
	sub	x1,x1,3				; (les sprites ont 4 de marge, 3 a droite)
	mul	x1,x1,x24
	and	x2,x23,3
	cmp	x2,1
	b.ne	.r
	sub	x20,x20,x1,lsr 1
.r:	cmp	x2,2
	b.ne	.go
	sub	x20,x20,x1
.go:	ldrb	w0,[x19],1
	sub	x0,x0,'0'
	ldr	x2,[x22,x0,lsl 3]
	tbz	x23,2,.pl
	add	x0,x20,1
	add	x1,x21,2
	bl	ga_shadow
	ldrb	w0,[x19,-1]
	sub	x0,x0,'0'
	ldr	x2,[x22,x0,lsl 3]
.pl:	mov	x0,x20
	mov	x1,x21
	bl	ga_ablit
	ldr	x0,[x22]
	ldr	w0,[x0]
	sub	x0,x0,3
	add	x20,x20,x0
	subs	x24,x24,1
	b.ne	.go
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- bruit --------------------------------------------------------------------
ga_gen_noise:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,ga_nz
	mov	x20,4096
.n:	bl	gf_rand
	lsr	x0,x0,24
	strb	w0,[x19],1
	subs	x20,x20,1
	b.ne	.n
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, x2 = log2 du pas -> x0 = bruit lisse 0..255 (touche x1..x10)
ga_noise2:
	mov	x3,1
	lsl	x3,x3,x2
	sub	x3,x3,1
	and	x4,x0,x3
	and	x5,x1,x3
	add	x3,x3,1				; fractions adoucies : f*f*(3-2f)
	add	x6,x3,x3,lsl 1
	sub	x7,x6,x4,lsl 1
	mul	x7,x7,x4
	mul	x7,x7,x4
	lsr	x7,x7,x2
	lsr	x4,x7,x2
	sub	x7,x6,x5,lsl 1
	mul	x7,x7,x5
	mul	x7,x7,x5
	lsr	x7,x7,x2
	lsr	x5,x7,x2
	lsr	x0,x0,x2
	lsr	x1,x1,x2
	and	x0,x0,63
	and	x1,x1,63
	adr	x6,ga_nz
	add	x7,x0,x1,lsl 6
	ldrb	w8,[x6,x7]
	add	x7,x0,1
	and	x7,x7,63
	add	x7,x7,x1,lsl 6
	ldrb	w9,[x6,x7]
	sub	x7,x9,x8
	mul	x7,x7,x4
	asr	x7,x7,x2
	add	x10,x8,x7
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
	add	x8,x8,x7
	sub	x7,x8,x10
	mul	x7,x7,x5
	asr	x7,x7,x2
	add	x0,x10,x7
	ret

; x0 = x, x1 = y -> x0 = 0..255, trois octaves (touche x1..x12)
ga_fbm:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x2,7
	bl	ga_noise2
	mov	x11,x0
	mov	x0,x19
	mov	x1,x20
	mov	x2,6
	bl	ga_noise2
	mov	x12,x0
	mov	x0,x19
	mov	x1,x20
	mov	x2,5
	bl	ga_noise2
	add	x0,x0,x12
	add	x0,x0,x11,lsl 1
	lsr	x0,x0,2
	ldp	x19,x20,[sp],16
	epilogue

; --- particules ---------------------------------------------------------------
; x0 = x, x1 = y (points), x2 = vx, x3 = vy (16.16 par image), w4 = couleur,
; x5 = vie (images), x6 = taille, x7 = gravite (16.16)
ga_part:
	adr	x8,ga_pnext
	ldr	x9,[x8]
	add	x10,x9,1
	cmp	x10,GA_NP
	csel	x10,xzr,x10,hs
	str	x10,[x8]
	adr	x8,ga_parts
	add	x8,x8,x9,lsl 5
	lsl	x0,x0,16
	lsl	x1,x1,16
	stp	w0,w1,[x8]
	stp	w2,w3,[x8,8]
	stp	w5,w4,[x8,16]
	stp	w6,w7,[x8,24]
	ret

; x0 = x, x1 = y, w2 = couleur, x3 = nombre, x4 = vitesse (16.16), x5 = vie, x6 = taille,
; x7 = gravite : une gerbe dans toutes les directions
ga_burst:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	mov	x22,x3
	mov	x23,x4
	mov	x24,x5
	mov	x25,x6
	mov	x26,x7
.p:	cbz	x22,.out
	sub	x22,x22,1
	mov	x0,256
	bl	gf_below
	mov	x9,x0
	mov	x0,x9
	bl	ga_sin8
	sub	x10,x0,127
	add	x0,x9,64
	bl	ga_sin8
	sub	x11,x0,127
	mov	x0,256
	bl	gf_below
	add	x0,x0,64
	mul	x0,x0,x23
	asr	x0,x0,8				; vitesse au hasard
	mul	x2,x10,x0
	asr	x2,x2,7
	mul	x3,x11,x0
	asr	x3,x3,7
	stp	x2,x3,[sp,-16]!
	mov	x0,x24
	bl	gf_below
	add	x5,x24,x0
	ldp	x2,x3,[sp],16
	mov	x0,x19
	mov	x1,x20
	mov	w4,w21
	mov	x6,x25
	mov	x7,x26
	bl	ga_part
	b	.p
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

ga_parts_step:
	adr	x0,ga_parts
	mov	x1,GA_NP
.p:	ldr	w2,[x0,16]
	cbz	w2,.n
	sub	w2,w2,1
	str	w2,[x0,16]
	ldp	w2,w3,[x0]
	ldp	w4,w5,[x0,8]
	add	w2,w2,w4
	add	w3,w3,w5
	stp	w2,w3,[x0]
	ldr	w2,[x0,28]
	add	w5,w5,w2
	str	w5,[x0,12]
.n:	add	x0,x0,GA_PSIZE
	subs	x1,x1,1
	b.ne	.p
	ret

ga_parts_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,ga_parts
	mov	x20,GA_NP
.p:	ldr	w1,[x19,16]
	cbz	w1,.n
	lsl	w1,w1,4
	cmp	w1,256
	mov	w0,256
	csel	w1,w0,w1,hi
	ldr	w0,[x19,20]
	bl	ga_shade
	mov	w4,w0
	ldp	w0,w1,[x19]
	asr	w0,w0,16
	asr	w1,w1,16
	sxtw	x0,w0
	sxtw	x1,w1
	ldr	w2,[x19,24]
	mov	x3,x2
	bl	gf_rect
.n:	add	x19,x19,GA_PSIZE
	subs	x20,x20,1
	b.ne	.p
	ldp	x19,x20,[sp],16
	epilogue

; plus de particules
ga_parts_clear:
	adr	x0,ga_parts
	mov	x1,GA_NP
.p:	str	wzr,[x0,16]
	add	x0,x0,GA_PSIZE
	subs	x1,x1,1
	b.ne	.p
	ret

; --- zones d'appui ------------------------------------------------------------
; Entree : dh x, y, l, h, id ; fin : id 0. x0 = table, x1, x2 = point -> x0 = id (0 : aucun),
; x1 = entree
ga_zone:
.z:	ldrh	w3,[x0,8]
	cbz	w3,.none
	ldrh	w4,[x0]
	ldrh	w5,[x0,4]
	sub	x6,x1,x4
	cmp	x6,x5
	b.hs	.n
	ldrh	w4,[x0,2]
	ldrh	w5,[x0,6]
	sub	x6,x2,x4
	cmp	x6,x5
	b.hs	.n
	mov	x1,x0
	mov	x0,x3
	ret
.n:	add	x0,x0,10
	b	.z
.none:	mov	x0,0
	ret

; x0 = entree de zone, x1 = rayon, w2 = couleur, x3 = opacite, w4 = filet : son panneau
ga_zone_panel:
	mov	w5,w2
	mov	w8,w4
	mov	x7,x3
	mov	x4,x1
	ldrh	w1,[x0,2]
	ldrh	w2,[x0,4]
	ldrh	w3,[x0,6]
	ldrh	w0,[x0]
	mov	w6,w5
	b	ga_panel

; --- ce qui revient dans chaque jeu ---------------------------------------------
macro ga_push {
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
}
macro ga_pop {
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
}

; w0 = haut, w1 = bas, w2 = nuages, x3 = etoiles : un ciel dans la couche
ga_sky:
	prologue
	ga_push
	mov	w22,w0
	mov	w23,w1
	mov	w24,w2
	mov	x19,x3
	ldv	x25,gf_layer
	mov	x20,0
.y:	mov	w0,w22
	mov	w1,w23
	lsl	x2,x20,8
	mov	x3,GF_H
	udiv	x2,x2,x3
	bl	ga_mix
	mov	w26,w0
	mov	x21,0
.x:	mov	x0,x21
	mov	x1,x20
	bl	ga_fbm
	subs	x0,x0,96			; les nuages seulement au-dessus du seuil
	csel	x0,xzr,x0,lo
	lsl	x2,x0,1
	cmp	x2,160
	mov	x3,160
	csel	x2,x3,x2,hi
	mov	w0,w26
	mov	w1,w24
	bl	ga_mix
	str	w0,[x25],4
	add	x21,x21,1
	cmp	x21,GF_W
	b.lo	.x
	add	x20,x20,1
	cmp	x20,GF_H
	b.lo	.y
	ldv	x25,gf_layer
.s:	cbz	x19,.out
	movl	x0,GF_W*GF_H
	bl	gf_below
	mov	x20,x0
	mov	x0,120
	bl	gf_below
	add	x0,x0,100
	orr	w1,w0,w0,lsl 8
	orr	w1,w1,w0,lsl 16
	str	w1,[x25,x20,lsl 2]
	sub	x19,x19,1
	b	.s
.out:	ga_pop
	epilogue

; Table de textes : dw decalage depuis la base, db corps, 1 si gras, 0, 0, dw deux couleurs
macro ga_txe s, base, px, bold, c1, c2 {
	dw	s-base
	db	px, bold, 0, 0
	dw	c1, c2
}
; x0 = table, x1 = base, x2 = sprites (tableau), x3 = nombre (entre ui_canvas_begin et _end)
ga_texts:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x22,x1
	mov	x20,x2
	mov	x21,x3
.t:	ldr	w0,[x19]
	add	x0,x22,x0
	ldrb	w1,[x19,4]
	ldrb	w2,[x19,5]
	adr	x3,ui_font
	adr	x4,ui_font_b
	cmp	x2,0
	csel	x2,x4,x3,ne
	ldr	w3,[x19,8]
	ldr	w4,[x19,12]
	bl	ga_text
	str	x0,[x20],8
	add	x19,x19,16
	subs	x21,x21,1
	b.ne	.t
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = sprite, x1 = x du centre, x2 = y : ombre et texte
ga_show:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	mov	x20,x2
	ldr	w0,[x21]
	sub	x19,x1,x0,lsr 1
	add	x0,x19,2
	add	x1,x20,3
	mov	x2,x21
	bl	ga_shadow
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	ga_ablit
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = lueur 0, x1 = lueur 1, x2 = compteur : deux lumieres derivent sur le decor
ga_orbs:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x2
	mov	x21,x0
	mov	x22,x1
	lsr	x0,x19,2
	bl	ga_sin8
	lsl	x20,x0,1
	lsr	x0,x19,3
	add	x0,x0,40
	bl	ga_sin8
	mov	x1,x0
	mov	x0,x20
	mov	x2,x21
	bl	gf_bglow
	lsr	x0,x19,3
	add	x0,x0,128
	bl	ga_sin8
	lsl	x20,x0,1
	lsr	x0,x19,2
	add	x0,x0,200
	bl	ga_sin8
	add	x1,x0,120
	add	x0,x20,20
	mov	x2,x22
	bl	gf_bglow
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

macro ga_data {
		align 8
ga_bp		rd 1			; pile des sprites
ga_pnext	rd 1
ga_tmp		rb 32
ga_nz		rb 4096
		align 8
ga_parts	rb GA_NP*GA_PSIZE
}
