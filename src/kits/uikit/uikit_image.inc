; ============================================================================
;  kits/uikit_image.inc - UIKit : les images (BMP, PNG) et leur widget
;
;  Une image (poignee) : +0 largeur, +8 hauteur, +16 pixels (largeur x
;  hauteur x 4 octets, 0xAARRGGBB, AA = opacite 255). Decodees dans le kit
;  (clean-room, d'apres les specifications) :
;   * BMP : non compresse 24 / 32 bits (BI_RGB, BI_BITFIELDS en BGRA), 8, 4
;     et 1 bit avec palette ; de bas en haut ou de haut en bas ;
;   * PNG : toutes les couleurs (gris, RVB, palette, gris + alpha, RVBA),
;     profondeurs 1, 2, 4, 8, 16 (octet fort), tRNS, filtres 0-4 ; pas
;     l'entrelacement Adam7 (refuse) ; zlib / deflate (RFC 1950, 1951 :
;     blocs stockes, Huffman fixe et dynamique) decode ici (uk_inflate).
;  ui_image_draw met une image a l'echelle (plus proche voisin) et la melange
;  par son alpha, dans la decoupe courante. Widget : ui_imagebox (image
;  ajustee, centree ; Onyx imagebox).
; ============================================================================

IMG_W		= 0
IMG_H		= 8
IMG_PIX		= 16

; x0 = largeur, x1 = hauteur -> x0 = image vide (0 : echec)
uk_img_new:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cbz	x19,.fail
	cbz	x20,.fail
	mov	x2,16384
	cmp	x19,x2
	b.hi	.fail
	cmp	x20,x2
	b.hi	.fail
	mul	x0,x19,x20
	lsl	x0,x0,2
	add	x0,x0,IMG_PIX
	bl	uk_alloc
	cbz	x0,.out
	stp	x19,x20,[x0]
	b	.out
.fail:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = image (0 : rien) : rendue
ui_image_free:
	b	uk_free

; x0 = image -> x0 = largeur, x1 = hauteur, x2 = pixels
ui_image_size:
	cbz	x0,.z
	ldp	x1,x2,[x0]
	add	x3,x0,IMG_PIX
	mov	x0,x1
	mov	x1,x2
	mov	x2,x3
	ret
.z:	mov	x1,0
	mov	x2,0
	ret

; x0 = chemin -> x0 = image (0 : illisible ou format inconnu)
ui_image_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	mov	x1,sp
	sys	SYS_STAT
	cbnz	x0,.fail
	ldr	x20,[sp]
	cbz	x20,.fail
	mov	x0,x20
	bl	uk_alloc
	cbz	x0,.fail
	mov	x21,x0
	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.free
	mov	x19,x0
	mov	x1,x21
	mov	x2,x20
	sys	SYS_READ
	mov	x22,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x22,x20
	b.ne	.free
	mov	x0,x21
	mov	x1,x20
	bl	ui_image_decode
	mov	x19,x0
	mov	x0,x21
	bl	uk_free
	mov	x0,x19
	b	.out
.free:	mov	x0,x21
	bl	uk_free
.fail:	mov	x0,0
.out:	add	sp,sp,16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = donnees, x1 = octets -> x0 = image (BMP ou PNG d'apres la signature ;
; 0 : inconnu ou invalide)
ui_image_decode:
	cmp	x1,26
	b.lo	.no
	ldrh	w2,[x0]
	mov	w3,0x4D42			; « BM »
	cmp	w2,w3
	b.eq	uk_bmp_decode
	ldr	w2,[x0]
	movl	w3,0x474E5089			; 89 « PNG »
	cmp	w2,w3
	b.eq	uk_png_decode
.no:	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  BMP
; ---------------------------------------------------------------------------

; x0 = donnees, x1 = octets -> x0 = image (0 : non pris en charge)
uk_bmp_decode:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	w21,[x19,10]			; debut des pixels
	ldr	w22,[x19,14]			; taille de l'en-tete
	ldrsw	x23,[x19,18]			; largeur
	ldrsw	x24,[x19,22]			; hauteur (< 0 : de haut en bas)
	ldrh	w25,[x19,28]			; bits par pixel
	ldr	w26,[x19,30]			; compression
	cmp	x26,3				; BI_RGB, BI_BITFIELDS (32 bits)
	b.hi	.fail
	cmp	x26,0
	b.eq	.c
	cmp	x25,32
	b.ne	.fail
.c:	mov	x27,0				; 1 : de haut en bas
	tbz	x24,63,.pos
	neg	x24,x24
	mov	x27,1
.pos:	cmp	x23,0
	b.le	.fail
	mov	x0,x23
	mov	x1,x24
	bl	uk_img_new
	cbz	x0,.fail
	mov	x28,x0
	mul	x0,x23,x25			; pas d'une ligne : multiple de 4 octets
	add	x0,x0,31
	lsr	x0,x0,5
	lsl	x0,x0,2
	str	x0,[sp,-16]!			; [sp] = pas
	madd	x0,x0,x24,x21			; tout dans le fichier ?
	cmp	x0,x20
	b.hi	.bad
	add	x0,x19,14			; palette (apres l'en-tete)
	add	x0,x0,x22
	str	x0,[sp,8]
	mov	x26,0				; ligne de l'image
.row:	cmp	x26,x24
	b.hs	.ok
	sub	x0,x24,1			; ligne du fichier
	sub	x0,x0,x26
	cmp	x27,0
	csel	x0,x26,x0,ne
	ldr	x1,[sp]
	madd	x0,x0,x1,x21
	add	x0,x19,x0			; source
	add	x1,x28,IMG_PIX			; destination
	mul	x2,x26,x23
	add	x1,x1,x2,lsl 2
	mov	x2,0				; x
.px:	cmp	x2,x23
	b.hs	.nrow
	cmp	x25,24
	b.ne	.p32
	mov	x3,3
	mul	x3,x2,x3
	add	x3,x0,x3
	ldrb	w4,[x3]				; B, G, R
	ldrb	w5,[x3,1]
	ldrb	w6,[x3,2]
	orr	w4,w4,w5,lsl 8
	orr	w4,w4,w6,lsl 16
	orr	w4,w4,0xFF000000
	b	.put
.p32:	cmp	x25,32
	b.ne	.pal
	ldr	w4,[x0,x2,lsl 2]		; BGRA (alpha 0 partout : opaque)
	tst	w4,0xFF000000
	b.ne	.put
	orr	w4,w4,0xFF000000
	b	.put
.pal:	cmp	x25,8				; palette : 8, 4, 1 bits
	b.ne	.p4
	ldrb	w5,[x0,x2]
	b	.lut
.p4:	cmp	x25,4
	b.ne	.p1
	lsr	x3,x2,1
	ldrb	w5,[x0,x3]
	tst	x2,1
	b.ne	.lo4
	lsr	w5,w5,4
.lo4:	and	w5,w5,15
	b	.lut
.p1:	cmp	x25,1
	b.ne	.bad
	lsr	x3,x2,3
	ldrb	w5,[x0,x3]
	and	x3,x2,7
	mov	x6,7
	sub	x3,x6,x3
	lsr	w5,w5,w3
	and	w5,w5,1
.lut:	ldr	x3,[sp,8]
	ldr	w4,[x3,x5,lsl 2]
	orr	w4,w4,0xFF000000
.put:	str	w4,[x1,x2,lsl 2]
	add	x2,x2,1
	b	.px
.nrow:	add	x26,x26,1
	b	.row
.ok:	add	sp,sp,16
	mov	x0,x28
	b	.out
.bad:	add	sp,sp,16
	mov	x0,x28
	bl	uk_free
.fail:	mov	x0,0
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Inflate (RFC 1951) : lecture bit a bit, codes de Huffman canoniques
;  (comptes par longueur + symboles tries, decodage par premiers codes)
; ---------------------------------------------------------------------------

; etat du decodeur : uk_inf (variables du kit)
INF_SRC		= 0			; octet courant, fin
INF_END		= 8
INF_BITS	= 16			; reserve de bits, leur nombre
INF_NB		= 24
INF_DST		= 32			; sortie, fin
INF_DEND	= 40
INF_DBEG	= 48
INF_LCNT	= 56			; litteraux / longueurs : comptes (16 x 2), symboles (288 x 2)
INF_LSYM	= INF_LCNT+32
INF_DCNT	= INF_LSYM+576		; distances : comptes, symboles (32 x 2)
INF_DSYM	= INF_DCNT+32
INF_SIZE	= INF_DSYM+64

; x0 = nombre de bits (<= 24) -> x0 = ces bits (poids faible d'abord) ; x0 =
; -1 a la fin des donnees (feuille ; x9 = uk_inf)
uk_inf_bits:
	adr	x9,uk_inf
	ldr	x1,[x9,INF_BITS]
	ldr	x2,[x9,INF_NB]
.fill:	cmp	x2,x0
	b.hs	.take
	ldp	x3,x4,[x9,INF_SRC]
	cmp	x3,x4
	b.hs	.end
	ldrb	w5,[x3],1
	str	x3,[x9,INF_SRC]
	lsl	x5,x5,x2
	orr	x1,x1,x5
	add	x2,x2,8
	b	.fill
.take:	mov	x3,1
	lsl	x3,x3,x0
	sub	x3,x3,1
	and	x4,x1,x3
	lsr	x1,x1,x0
	sub	x2,x2,x0
	str	x1,[x9,INF_BITS]
	str	x2,[x9,INF_NB]
	mov	x0,x4
	ret
.end:	mov	x0,-1
	ret

; x0 = comptes (16 x 16 bits), x1 = symboles, x2 = longueurs (octets), x3 =
; nombre de symboles : construit la table (feuille)
uk_inf_build:
	mov	x4,0				; comptes a zero
.z:	strh	wzr,[x0,x4,lsl 1]
	add	x4,x4,1
	cmp	x4,16
	b.lo	.z
	mov	x4,0
.c:	cmp	x4,x3
	b.hs	.offs
	ldrb	w5,[x2,x4]
	ldrh	w6,[x0,x5,lsl 1]
	add	w6,w6,1
	strh	w6,[x0,x5,lsl 1]
	add	x4,x4,1
	b	.c
.offs:	strh	wzr,[x0]			; (longueur 0 : pas de code)
	sub	sp,sp,32			; decalages par longueur
	mov	x4,0
	mov	x5,0
.o:	cmp	x4,16
	b.hs	.sym
	strh	w5,[sp,x4,lsl 1]
	ldrh	w6,[x0,x4,lsl 1]
	add	x5,x5,x6
	add	x4,x4,1
	b	.o
.sym:	mov	x4,0
.s:	cmp	x4,x3
	b.hs	.done
	ldrb	w5,[x2,x4]
	cbz	w5,.sn
	ldrh	w6,[sp,x5,lsl 1]
	strh	w4,[x1,x6,lsl 1]
	add	w6,w6,1
	strh	w6,[sp,x5,lsl 1]
.sn:	add	x4,x4,1
	b	.s
.done:	add	sp,sp,32
	ret

; x0 = comptes, x1 = symboles -> x0 = symbole (-1 : erreur)
uk_inf_decode:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0				; code
	mov	x22,0				; premier code de la longueur
	mov	x23,0				; indice du premier symbole
	mov	x24,1				; longueur
.l:	cmp	x24,15
	b.hi	.err
	mov	x0,1
	bl	uk_inf_bits
	tbnz	x0,63,.err
	orr	x21,x0,x21,lsl 1
	ldrh	w1,[x19,x24,lsl 1]		; codes de cette longueur
	sub	x2,x21,x22
	cmp	x2,x1
	b.lo	.got
	add	x23,x23,x1
	add	x22,x22,x1
	lsl	x22,x22,1
	add	x24,x24,1
	b	.l
.got:	add	x2,x23,x2
	ldrh	w0,[x20,x2,lsl 1]
	b	.out
.err:	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

uk_len_base:	dh 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
uk_len_extra:	db 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
		align 2
uk_dist_base:	dh 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073
		dh 4097,6145,8193,12289,16385,24577
uk_dist_extra:	db 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
uk_clen_order:	db 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
		align 4

; x0 = donnees zlib, x1 = octets, x2 = sortie, x3 = taille de la sortie ->
; x0 = octets produits (< 0 : erreur)
uk_inflate:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,368			; longueurs (19 puis 32 + 316 octets)
	adr	x19,uk_inf
	add	x0,x0,2				; en-tete zlib (methode 8 : rien a voir)
	sub	x1,x1,2
	add	x1,x0,x1
	stp	x0,x1,[x19,INF_SRC]
	stp	xzr,xzr,[x19,INF_BITS]
	add	x3,x2,x3
	stp	x2,x3,[x19,INF_DST]
	str	x2,[x19,INF_DBEG]
.block:	mov	x0,1				; dernier bloc ?
	bl	uk_inf_bits
	tbnz	x0,63,.err
	mov	x25,x0
	mov	x0,2
	bl	uk_inf_bits
	tbnz	x0,63,.err
	cbz	x0,.stored
	cmp	x0,1
	b.eq	.fixed
	cmp	x0,2
	b.eq	.dyn
	b	.err
.stored:					; bloc stocke : octets alignes
	str	xzr,[x19,INF_BITS]
	str	xzr,[x19,INF_NB]
	ldp	x0,x1,[x19,INF_SRC]
	add	x2,x0,4
	cmp	x2,x1
	b.hi	.err
	ldrh	w3,[x0]
	mov	x0,x2
	ldp	x4,x5,[x19,INF_DST]
.sc:	cbz	x3,.sd
	cmp	x0,x1
	b.hs	.err
	cmp	x4,x5
	b.hs	.err
	ldrb	w6,[x0],1
	strb	w6,[x4],1
	sub	x3,x3,1
	b	.sc
.sd:	str	x0,[x19,INF_SRC]
	str	x4,[x19,INF_DST]
	b	.next
.fixed:	mov	x0,0				; Huffman fixe
.fl:	mov	w1,8
	cmp	x0,144
	b.lo	.fs
	mov	w1,9
	cmp	x0,256
	b.lo	.fs
	mov	w1,7
	cmp	x0,280
	b.lo	.fs
	mov	w1,8
.fs:	strb	w1,[sp,x0]
	add	x0,x0,1
	cmp	x0,288
	b.lo	.fl
	mov	w1,5
.fd:	strb	w1,[sp,x0]
	add	x0,x0,1
	cmp	x0,288+30
	b.lo	.fd
	add	x0,x19,INF_LCNT
	add	x1,x19,INF_LSYM
	mov	x2,sp
	mov	x3,288
	bl	uk_inf_build
	add	x0,x19,INF_DCNT
	add	x1,x19,INF_DSYM
	add	x2,sp,288
	mov	x3,30
	bl	uk_inf_build
	b	.codes
.dyn:	mov	x0,5				; Huffman dynamique
	bl	uk_inf_bits
	tbnz	x0,63,.err
	add	x20,x0,257			; HLIT
	mov	x0,5
	bl	uk_inf_bits
	tbnz	x0,63,.err
	add	x21,x0,1			; HDIST
	mov	x0,4
	bl	uk_inf_bits
	tbnz	x0,63,.err
	add	x22,x0,4			; HCLEN
	mov	x0,0				; longueurs des codes de longueurs
.cz:	strb	wzr,[sp,x0]
	add	x0,x0,1
	cmp	x0,19
	b.lo	.cz
	mov	x23,0
.cl:	cmp	x23,x22
	b.hs	.clb
	mov	x0,3
	bl	uk_inf_bits
	tbnz	x0,63,.err
	adr	x1,uk_clen_order
	ldrb	w1,[x1,x23]
	strb	w0,[sp,x1]
	add	x23,x23,1
	b	.cl
.clb:	add	x0,x19,INF_LCNT			; (table provisoire dans celle des
	add	x1,x19,INF_LSYM			; litteraux)
	mov	x2,sp
	mov	x3,19
	bl	uk_inf_build
	add	x24,x20,x21			; longueurs des deux tables, a la suite
	mov	x23,0
.ll:	cmp	x23,x24
	b.hs	.lld
	add	x0,x19,INF_LCNT
	add	x1,x19,INF_LSYM
	bl	uk_inf_decode
	tbnz	x0,63,.err
	cmp	x0,16
	b.hs	.rep
	add	x1,sp,32			; (apres 19 : sp+32 garde les longueurs)
	strb	w0,[x1,x23]
	add	x23,x23,1
	b	.ll
.rep:	mov	x26,0				; valeur repetee
	cmp	x0,16
	b.ne	.r17
	cbz	x23,.err
	add	x1,sp,32
	sub	x2,x23,1
	ldrb	w26,[x1,x2]
	mov	x0,2
	bl	uk_inf_bits
	tbnz	x0,63,.err
	add	x2,x0,3
	b	.rp
.r17:	cmp	x0,17
	b.ne	.r18
	mov	x0,3
	bl	uk_inf_bits
	tbnz	x0,63,.err
	add	x2,x0,3
	b	.rp
.r18:	mov	x0,7
	bl	uk_inf_bits
	tbnz	x0,63,.err
	add	x2,x0,11
.rp:	add	x1,sp,32
.rpl:	cbz	x2,.ll
	cmp	x23,x24
	b.hs	.err
	strb	w26,[x1,x23]
	add	x23,x23,1
	sub	x2,x2,1
	b	.rpl
.lld:	add	x0,x19,INF_LCNT
	add	x1,x19,INF_LSYM
	add	x2,sp,32
	mov	x3,x20
	bl	uk_inf_build
	add	x0,x19,INF_DCNT
	add	x1,x19,INF_DSYM
	add	x2,sp,32
	add	x2,x2,x20
	mov	x3,x21
	bl	uk_inf_build
.codes:	add	x0,x19,INF_LCNT			; --- litteraux, longueurs / distances
	add	x1,x19,INF_LSYM
	bl	uk_inf_decode
	tbnz	x0,63,.err
	cmp	x0,256
	b.lo	.lit
	b.eq	.next
	sub	x0,x0,257			; longueur
	cmp	x0,29
	b.hs	.err
	adr	x1,uk_len_base
	ldrh	w20,[x1,x0,lsl 1]
	adr	x1,uk_len_extra
	ldrb	w0,[x1,x0]
	cbz	w0,.nle
	bl	uk_inf_bits
	tbnz	x0,63,.err
	add	x20,x20,x0
.nle:	add	x0,x19,INF_DCNT
	add	x1,x19,INF_DSYM
	bl	uk_inf_decode
	tbnz	x0,63,.err
	cmp	x0,30
	b.hs	.err
	adr	x1,uk_dist_base
	ldrh	w21,[x1,x0,lsl 1]
	adr	x1,uk_dist_extra
	ldrb	w0,[x1,x0]
	cbz	w0,.nde
	bl	uk_inf_bits
	tbnz	x0,63,.err
	add	x21,x21,x0
.nde:	ldp	x4,x5,[x19,INF_DST]		; copie depuis la distance
	ldr	x6,[x19,INF_DBEG]
	sub	x7,x4,x21
	cmp	x7,x6
	b.lo	.err
.cp:	cbz	x20,.cpd
	cmp	x4,x5
	b.hs	.err
	ldrb	w8,[x7],1
	strb	w8,[x4],1
	sub	x20,x20,1
	b	.cp
.cpd:	str	x4,[x19,INF_DST]
	b	.codes
.lit:	ldp	x4,x5,[x19,INF_DST]
	cmp	x4,x5
	b.hs	.err
	strb	w0,[x4],1
	str	x4,[x19,INF_DST]
	b	.codes
.next:	cbz	x25,.block
	ldr	x0,[x19,INF_DST]
	ldr	x1,[x19,INF_DBEG]
	sub	x0,x0,x1
	b	.out
.err:	mov	x0,-1
.out:	add	sp,sp,368
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  PNG
; ---------------------------------------------------------------------------

; x0 = adresse -> w0 = entier de 32 bits gros-boutiste (feuille)
uk_be32:
	ldr	w0,[x0]
	rev	w0,w0
	ret

; x0 = donnees, x1 = octets -> x0 = image (0 : invalide, entrelacee)
uk_png_decode:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,96			; +0 w, +8 h, +16 profondeur, +24 couleur,
	mov	x19,x0				; +32 palette, +40 tRNS, +48 longueur tRNS,
	add	x20,x0,x1			; +56 idat, +64 longueur, +72 brut, +80 image
	stp	xzr,xzr,[sp,32]
	stp	xzr,xzr,[sp,48]
	stp	xzr,xzr,[sp,64]
	stp	xzr,xzr,[sp,80]
	mov	x0,x1				; IDAT mis bout a bout (au plus la taille
	bl	uk_alloc			; du fichier)
	cbz	x0,.fail
	str	x0,[sp,56]
	add	x21,x19,8			; chunks
.chunk:	add	x0,x21,8
	cmp	x0,x20
	b.hi	.chend
	mov	x0,x21
	bl	uk_be32
	mov	x22,x0				; longueur
	ldr	w23,[x21,4]			; type
	add	x24,x21,8			; donnees
	add	x0,x24,x22
	add	x0,x0,4
	cmp	x0,x20
	b.hi	.chend
	movl	w0,0x52444849			; IHDR
	cmp	w23,w0
	b.ne	.c2
	mov	x0,x24
	bl	uk_be32
	str	x0,[sp]
	add	x0,x24,4
	bl	uk_be32
	str	x0,[sp,8]
	ldrb	w0,[x24,8]
	str	x0,[sp,16]
	ldrb	w0,[x24,9]
	str	x0,[sp,24]
	ldrb	w0,[x24,12]			; entrelacement : refuse
	cbnz	w0,.fail
	b	.cnext
.c2:	movl	w0,0x45544C50			; PLTE
	cmp	w23,w0
	b.ne	.c3
	str	x24,[sp,32]
	b	.cnext
.c3:	movl	w0,0x534E5274			; tRNS
	cmp	w23,w0
	b.ne	.c4
	str	x24,[sp,40]
	str	x22,[sp,48]
	b	.cnext
.c4:	movl	w0,0x54414449			; IDAT
	cmp	w23,w0
	b.ne	.c5
	ldr	x0,[sp,56]
	ldr	x1,[sp,64]
	add	x0,x0,x1
	mov	x1,x24
	mov	x2,x22
	bl	uk_memcpy
	ldr	x1,[sp,64]
	add	x1,x1,x22
	str	x1,[sp,64]
	b	.cnext
.c5:	movl	w0,0x444E4549			; IEND
	cmp	w23,w0
	b.eq	.chend
.cnext:	add	x21,x24,x22
	add	x21,x21,4			; (CRC)
	b	.chunk
.chend:	ldp	x25,x26,[sp]			; largeur, hauteur
	cbz	x25,.fail
	cbz	x26,.fail
	ldr	x27,[sp,16]			; bits par echantillon
	ldr	x0,[sp,24]			; echantillons par pixel
	mov	x28,1				; gris, palette : 1
	cmp	x0,2
	b.ne	.s4
	mov	x28,3				; RVB
.s4:	cmp	x0,4
	b.ne	.s6
	mov	x28,2				; gris + alpha
.s6:	cmp	x0,6
	b.ne	.sx
	mov	x28,4				; RVBA
.sx:	mul	x0,x25,x28			; octets d'une ligne
	mul	x0,x0,x27
	add	x0,x0,7
	lsr	x23,x0,3			; x23 = pas
	add	x0,x23,1			; brut : h x (1 + pas)
	mul	x0,x0,x26
	str	x0,[sp,88]			; (taille du brut, provisoire)
	bl	uk_alloc
	cbz	x0,.fail
	str	x0,[sp,72]
	ldr	x0,[sp,56]
	ldr	x1,[sp,64]
	ldr	x2,[sp,72]
	ldr	x3,[sp,88]
	bl	uk_inflate
	ldr	x1,[sp,88]
	cmp	x0,x1
	b.lt	.fail
	mul	x0,x27,x28			; octets par pixel pour les filtres (>= 1)
	add	x0,x0,7
	lsr	x24,x0,3
	ldr	x0,[sp,72]			; --- les filtres, ligne a ligne
	mov	x1,x23
	mov	x2,x26
	mov	x3,x24
	bl	uk_png_unfilter
	mov	x0,x25
	mov	x1,x26
	bl	uk_img_new
	cbz	x0,.fail
	str	x0,[sp,80]
	mov	x0,sp				; --- en pixels 0xAARRGGBB
	mov	x1,x23
	mov	x2,x28
	bl	uk_png_pixels
	ldr	x0,[sp,80]
	b	.out
.fail:	ldr	x0,[sp,80]
	bl	uk_free
	mov	x0,0
.out:	str	x0,[sp,80]
	ldr	x0,[sp,56]
	bl	uk_free
	ldr	x0,[sp,72]
	bl	uk_free
	ldr	x0,[sp,80]
	add	sp,sp,96
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = brut (lignes : filtre + pas octets), x1 = pas, x2 = lignes, x3 =
; octets par pixel : defait les filtres sur place (Sub, Up, Average, Paeth)
uk_png_unfilter:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x9,0				; ligne
.row:	cmp	x9,x21
	b.hs	.out
	add	x10,x20,1
	mul	x10,x9,x10
	add	x10,x19,x10			; x10 = filtre ; x10 + 1 = la ligne
	ldrb	w11,[x10],1
	sub	x12,x10,x20			; ligne d'avant (sans son octet de
	sub	x12,x12,1			; filtre) ; 0 pour la premiere
	cmp	x9,0
	csel	x12,xzr,x12,eq
	mov	x13,0				; octet
.b:	cmp	x13,x20
	b.hs	.nrow
	ldrb	w14,[x10,x13]
	mov	w15,0				; a (gauche), b (haut), c (haut gauche)
	cmp	x13,x22
	b.lo	.na
	sub	x1,x13,x22
	ldrb	w15,[x10,x1]
.na:	mov	w16,0
	mov	w17,0
	cbz	x12,.nb
	ldrb	w16,[x12,x13]
	cmp	x13,x22
	b.lo	.nb
	sub	x1,x13,x22
	ldrb	w17,[x12,x1]
.nb:	cmp	w11,1
	b.ne	.f2
	add	w14,w14,w15			; Sub
	b	.st
.f2:	cmp	w11,2
	b.ne	.f3
	add	w14,w14,w16			; Up
	b	.st
.f3:	cmp	w11,3
	b.ne	.f4
	add	w1,w15,w16			; Average
	add	w14,w14,w1,lsr 1
	b	.st
.f4:	cmp	w11,4
	b.ne	.st
	add	w1,w15,w16			; Paeth : p = a + b - c
	sub	w1,w1,w17
	subs	w2,w1,w15			; pa
	cneg	w2,w2,mi
	subs	w3,w1,w16			; pb
	cneg	w3,w3,mi
	subs	w4,w1,w17			; pc
	cneg	w4,w4,mi
	mov	w5,w15
	cmp	w2,w3
	b.gt	.pb
	cmp	w2,w4
	b.le	.pp
.pb:	mov	w5,w16
	cmp	w3,w4
	b.le	.pp
	mov	w5,w17
.pp:	add	w14,w14,w5
.st:	strb	w14,[x10,x13]
	add	x13,x13,1
	b	.b
.nrow:	add	x9,x9,1
	b	.row
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat de uk_png_decode (+0 w, +8 h, +16 bits, +24 couleur, +32 PLTE,
; +40 tRNS, +48 sa longueur, +72 brut, +80 image), x1 = pas, x2 = echantillons
uk_png_pixels:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x21,[x19,80]
	add	x21,x21,IMG_PIX			; destination
	mov	x22,0				; y
.row:	ldr	x0,[x19,8]
	cmp	x22,x0
	b.hs	.out
	add	x23,x20,1
	mul	x23,x22,x23
	ldr	x0,[x19,72]
	add	x23,x0,x23
	add	x23,x23,1			; la ligne
	mov	x24,0				; x
.px:	ldr	x0,[x19]
	cmp	x24,x0
	b.hs	.nrow
	ldr	x25,[x19,16]			; bits
	ldr	x26,[x19,24]			; couleur
	cmp	x26,3
	b.eq	.pal
	cmp	x26,0
	b.eq	.gray
	cmp	x26,4
	b.eq	.ga
	cmp	x26,2
	b.eq	.rgb
	; RVBA (6)
	mov	x0,4
	bl	uk_png_sample_at		; x0 = echantillons par pixel -> w1..w4
	orr	w0,w3,w2,lsl 8
	orr	w0,w0,w1,lsl 16
	orr	w0,w0,w4,lsl 24
	b	.put
.rgb:	mov	x0,3
	bl	uk_png_sample_at
	orr	w0,w3,w2,lsl 8
	orr	w0,w0,w1,lsl 16
	ldr	x5,[x19,40]			; tRNS : la couleur transparente
	cbz	x5,.opq
	ldrb	w6,[x5,1]
	cmp	w6,w1
	b.ne	.opq
	ldrb	w6,[x5,3]
	cmp	w6,w2
	b.ne	.opq
	ldrb	w6,[x5,5]
	cmp	w6,w3
	b.ne	.opq
	b	.put
.ga:	mov	x0,2
	bl	uk_png_sample_at
	orr	w0,w1,w1,lsl 8
	orr	w0,w0,w1,lsl 16
	orr	w0,w0,w2,lsl 24
	b	.put
.gray:	mov	x0,x24				; gris 1, 2, 4, 8, 16 bits
	bl	uk_png_bits
	mov	w6,w0				; (valeur brute : tRNS)
	mov	w1,255				; mise a l'echelle 0..255
	cmp	x25,8
	b.hs	.g8
	mov	w2,1
	lsl	w2,w2,w25
	sub	w2,w2,1
	mul	w0,w0,w1
	udiv	w0,w0,w2
.g8:	orr	w1,w0,w0,lsl 8
	orr	w0,w1,w0,lsl 16
	ldr	x5,[x19,40]
	cbz	x5,.opq
	ldrb	w7,[x5,1]
	cmp	w7,w6
	b.eq	.put
	b	.opq
.pal:	mov	x0,x24				; palette 1, 2, 4, 8 bits
	bl	uk_png_bits
	mov	w6,w0
	ldr	x5,[x19,32]
	cbz	x5,.opq0
	mov	x1,3
	mul	x1,x6,x1
	add	x5,x5,x1
	ldrb	w0,[x5,2]
	ldrb	w1,[x5,1]
	ldrb	w2,[x5]
	orr	w0,w0,w1,lsl 8
	orr	w0,w0,w2,lsl 16
	ldr	x5,[x19,40]			; alpha de la palette (tRNS)
	cbz	x5,.opq
	ldr	x7,[x19,48]
	cmp	x6,x7
	b.hs	.opq
	ldrb	w7,[x5,x6]
	orr	w0,w0,w7,lsl 24
	b	.put
.opq0:	mov	w0,0
.opq:	orr	w0,w0,0xFF000000
.put:	str	w0,[x21],4
	add	x24,x24,1
	b	.px
.nrow:	add	x22,x22,1
	b	.row
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (uk_png_pixels) x0 = echantillons par pixel (8 ou 16 bits) -> w1..w4 =
; leurs valeurs sur 8 bits (x23 ligne, x24 x, x25 bits)
uk_png_sample_at:
	mul	x5,x24,x0
	cmp	x25,16
	b.ne	.b8
	lsl	x5,x5,1				; 16 bits : l'octet fort
	add	x5,x23,x5
	ldrb	w1,[x5]
	ldrb	w2,[x5,2]
	ldrb	w3,[x5,4]
	ldrb	w4,[x5,6]
	ret
.b8:	add	x5,x23,x5
	ldrb	w1,[x5]
	ldrb	w2,[x5,1]
	ldrb	w3,[x5,2]
	ldrb	w4,[x5,3]
	ret

; (uk_png_pixels) x0 = x -> w0 = l'echantillon de x25 bits (1, 2, 4, 8 ;
; 16 : l'octet fort), ligne x23
uk_png_bits:
	cmp	x25,16
	b.ne	.n16
	lsl	x0,x0,1
	ldrb	w0,[x23,x0]
	ret
.n16:	cmp	x25,8
	b.ne	.sub
	ldrb	w0,[x23,x0]
	ret
.sub:	mul	x1,x0,x25			; position en bits (poids fort d'abord)
	lsr	x2,x1,3
	ldrb	w0,[x23,x2]
	and	x1,x1,7
	add	x1,x1,x25
	mov	x2,8
	sub	x1,x2,x1
	lsr	w0,w0,w1
	mov	w2,1
	lsl	w2,w2,w25
	sub	w2,w2,1
	and	w0,w0,w2
	ret

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------

; x0 = image, x1 = x, x2 = y, x3 = largeur, x4 = hauteur (0, 0 : taille
; de l'image) : a l'echelle (plus proche voisin), melangee par son alpha,
; decoupee a la surface et a ui_clip
ui_image_draw:
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
	mov	x22,x3
	mov	x23,x4
	cbnz	x22,.w
	ldr	x22,[x19,IMG_W]
.w:	cbnz	x23,.h
	ldr	x23,[x19,IMG_H]
.h:	cmp	x22,0
	b.le	.out
	cmp	x23,0
	b.le	.out
	adr	x0,uk_vars			; bornes : surface, decoupe
	ldr	x0,[x0]
	ldp	x24,x25,[x0,UV_W]		; x24, x25 = bord droit, bas
	mov	x26,0				; x26, x27 = bord gauche, haut
	mov	x27,0
	adr	x1,uk_clip_on
	ldr	x1,[x1]
	cbz	x1,.b
	adr	x1,uk_clip
	ldp	x26,x27,[x1]
	ldp	x2,x3,[x1,16]
	cmp	x2,x24
	csel	x24,x2,x24,lt
	cmp	x3,x25
	csel	x25,x3,x25,lt
.b:	cmp	x27,x21				; premiere ligne
	csel	x27,x21,x27,lt
	add	x0,x21,x23
	cmp	x25,x0
	csel	x25,x0,x25,gt
	cmp	x26,x20
	csel	x26,x20,x26,lt
	add	x0,x20,x22
	cmp	x24,x0
	csel	x24,x0,x24,gt
.y:	cmp	x27,x25
	b.ge	.out
	sub	x0,x27,x21			; ligne source
	ldr	x1,[x19,IMG_H]
	mul	x0,x0,x1
	udiv	x0,x0,x23
	ldr	x1,[x19,IMG_W]
	mul	x0,x0,x1
	add	x28,x19,IMG_PIX
	add	x28,x28,x0,lsl 2		; x28 = ligne source
	adr	x0,uk_vars
	ldr	x0,[x0]
	ldr	x1,[x0,UV_W]
	ldr	x2,[x0,UV_SURF]
	mul	x1,x27,x1
	add	x2,x2,x1,lsl 2			; x2 = ligne destination
	mov	x3,x26				; x
.x:	cmp	x3,x24
	b.ge	.ny
	sub	x4,x3,x20
	ldr	x5,[x19,IMG_W]
	mul	x4,x4,x5
	udiv	x4,x4,x22
	ldr	w5,[x28,x4,lsl 2]		; 0xAARRGGBB
	lsr	w6,w5,24			; alpha
	cbz	w6,.nx
	cmp	w6,255
	b.eq	.opaque
	ldr	w7,[x2,x3,lsl 2]		; melange : d + (s - d) * a / 255
	mov	w8,0
	mov	w9,0				; decalage
.ch:	lsr	w10,w5,w9
	and	w10,w10,0xFF
	lsr	w11,w7,w9
	and	w11,w11,0xFF
	sub	w10,w10,w11
	mul	w10,w10,w6
	mov	w12,255
	sdiv	w10,w10,w12
	add	w10,w10,w11
	and	w10,w10,0xFF
	lsl	w10,w10,w9
	orr	w8,w8,w10
	add	w9,w9,8
	cmp	w9,24
	b.lo	.ch
	str	w8,[x2,x3,lsl 2]
	b	.nx
.opaque:
	and	w5,w5,0x00FFFFFF
	str	w5,[x2,x3,lsl 2]
.nx:	add	x3,x3,1
	b	.x
.ny:	add	x27,x27,1
	b	.y
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Widget : boite d'image (UT_IMAGE)
; ---------------------------------------------------------------------------

; x0..x3 = rectangle, x4 = image (0 : aucune ; elle reste au programme),
; x5 = drapeaux (UIM_STRETCH : remplir sans garder les proportions ;
; UIM_NOSCALE : taille reelle, centree) -> poignee ; ui_imagebox_set change
; l'image
ui_imagebox:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x4
	mov	x20,x5
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_IMAGE
	bl	uk_w_new
	cbz	x0,.out
	str	x19,[x0,UW_AUX]
	str	x20,[x0,UW_STYLE]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = boite, x1 = image : la montre
ui_imagebox_set:
	cbz	x0,.ret
	str	x1,[x0,UW_AUX]
	b	uk_w_refresh
.ret:	ret

uk_im_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_CREUX
	bl	ui_fill
	ldr	x0,[x19,UW_AUX]
	cbz	x0,.out
	ldp	x1,x2,[x0]			; taille de l'image
	ldr	x3,[x19,UW_STYLE]
	tbnz	x3,0,.stretch
	tbnz	x3,1,.place
	mul	x4,x1,x23			; ajustee : le plus petit rapport
	mul	x5,x2,x22
	cmp	x4,x5
	b.hi	.byw
	mul	x1,x1,x23			; par la hauteur
	udiv	x1,x1,x2
	mov	x2,x23
	b	.place
.byw:	mul	x2,x2,x22			; par la largeur
	udiv	x2,x2,x1
	mov	x1,x22
.place:	sub	x3,x22,x1			; centree
	asr	x3,x3,1
	add	x3,x20,x3
	sub	x4,x23,x2
	asr	x4,x4,1
	add	x4,x21,x4
	mov	x5,x1
	mov	x6,x2
	ldr	x0,[x19,UW_AUX]
	mov	x1,x3
	mov	x2,x4
	mov	x3,x5
	mov	x4,x6
	bl	ui_image_draw
	b	.out
.stretch:
	ldr	x0,[x19,UW_AUX]
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	mov	x4,x23
	bl	ui_image_draw
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
