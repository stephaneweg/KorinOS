; ============================================================================
;  ttf.asm - rasteriseur TrueType d'ARMPI (le "FreeType maison")
;
;  Bibliotheque autonome : aucune dependance au noyau, aucune variable globale
;  mutable. Tout l'etat est dans une structure TTF_FONT_SIZE octets fournie par
;  l'appelant, plus un tampon de travail ("scratch") qu'il fournit aussi.
;  Utilisable en EL1 comme en EL0, reentrante entre threads (une structure +
;  un scratch par thread qui dessine).
;
;  Convention AAPCS64 : x0-x7 arguments, x0 retour, x19-x28 et d8-d15
;  preserves (d8-d15 ne sont jamais utilises ici). Calculs en flottant simple
;  precision (registres s0-s7 et s16-s31).
;
;  Rasterisation : contours TrueType (B-splines quadratiques, points hors
;  courbe implicites) aplatis en segments, accumules sous forme de couverture
;  signee dans une grille de flottants (methode "font-rs"), puis somme
;  prefixe par ligne : |somme| = couverture anti-aliasee (regle non nulle
;  approchee, exacte pour les contours qui ne se recouvrent pas).
;
;  Tables lues : head, maxp, hhea, hmtx, cmap (formats 4 et 12), loca, glyf
;  (glyphes simples et composites), kern (format 0, optionnelle).
;  Ignores : instructions de hinting, GPOS/GSUB (kerning OpenType, ligatures).
; ============================================================================

; --- structure "font" (TTF_FONT_SIZE octets, alignee 8) ----------------------
F_DATA		= 0		; fichier TTF en memoire
F_LEN		= 8
F_CMAP		= 16		; sous-table cmap retenue
F_CMAPFMT	= 24		; 4 ou 12
F_LOCA		= 32
F_GLYF		= 40
F_HMTX		= 48
F_KERN		= 56		; premiere paire kern (format 0), 0 si absente
F_NKERN		= 64
F_UPEM		= 72		; unites par em
F_LOCFMT	= 80		; 0 = loca courte, 1 = longue
F_NGLYPHS	= 88
F_NHM		= 96		; numberOfHMetrics
F_ASC		= 104		; ascendante (unites, signee)
F_DESC		= 112		; descendante (unites, negative)
F_GAP		= 120
F_SCALE		= 128		; flottant 32 bits : pixels par unite
F_PIXELS	= 136		; taille courante (pixels par em)
F_SCRATCH	= 144		; tampon de travail
F_SCRATCH_LEN	= 152
F_PTSBYTES	= 160		; zone des points en tete du scratch
F_ASC_PX	= 168		; metriques de ligne a la taille courante (pixels)
F_DESC_PX	= 176		;   (descendante positive, sous la ligne de base)
F_LINE_PX	= 184		;   interligne recommande
F_ACC		= 192		; resultat du dernier ttf_raster : grille
F_ACCW		= 200		;   largeur de ligne de la grille (flottants)
F_ACCH		= 208		;   hauteur (lignes)
F_GX0		= 216		;   decalage x du coin haut-gauche / point d'origine
F_GY0		= 224		;   decalage y (negatif = au-dessus de la ligne de base)
F_GW		= 232		;   largeur utile (pixels)
F_GADV		= 240		;   avance en 1/64 de pixel
F_MAXPTS	= 248
F_THEAD		= 256		; (pointeurs de tables pendant ttf_open)
F_TMAXP		= 264
F_THHEA		= 272
F_TKERN		= 280
F_CACHE		= 288		; cache de glyphes (fourni par ttf_set_cache), 0 = aucun
F_CACHE_LEN	= 296
F_CACHE_USED	= 304		; octets occupes dans la zone de donnees du cache
F_FALLBACK	= 312		; x0 = font -> x0 = police de repli des glyphes absents, 0 = aucune
TTF_FONT_SIZE	= 320

; --- cache de glyphes ---------------------------------------------------------
;  [256 entrees de 16 octets][zone de bitmaps 8 bits]. Correspondance directe
;  par (glyphe, taille) ; zone pleine -> cache vide en bloc. 32 Ko suffisent
;  pour du texte d'interface ; 128 Ko ou plus pour plusieurs tailles.
TC_SLOTS	= 256
TC_KEY		= 0		; glyphe | taille << 16 | 1 << 31 (0 = libre)
TC_W		= 4		; 16 bits
TC_H		= 6
TC_BX		= 8		; 16 bits signes
TC_BY		= 10
TC_OFF		= 12		; decalage du bitmap dans la zone de donnees
TC_HEADER	= TC_SLOTS * 16
TTF_CACHE_MIN	= TC_HEADER + 4096

; --- metriques rendues par ttf_render_glyph (5 mots de 32 bits) -------------
GM_W		= 0		; largeur du bitmap
GM_H		= 4		; hauteur
GM_BX		= 8		; decalage x depuis le point d'origine du stylo
GM_BY		= 12		; decalage y du haut depuis la ligne de base (<0 = dessus)
GM_ADV64	= 16		; avance en 1/64 de pixel
GM_SIZE		= 24

; Taille de scratch conseillee : 16 Ko + 4*(taille+4)^2 octets
; (64 Ko suffisent jusqu'a ~100 px, 256 Ko jusqu'a ~240 px).
TTF_SCRATCH_MIN	= 65536

; --- lecture big-endian -------------------------------------------------------
macro be16 r*, base*, off {
  if off eq
	ldrh	r,[base]
  else
	ldrh	r,[base,off]
  end if
	rev16	r,r
}
macro bes16 r*, base*, off {
  if off eq
	ldrh	r,[base]
  else
	ldrh	r,[base,off]
  end if
	rev16	r,r
	sxth	r,r
}
macro be32 r*, base*, off {
  if off eq
	ldr	r,[base]
  else
	ldr	r,[base,off]
  end if
	rev	r,r
}

macro ttf_tab tag*, field* {
	movz	w2,(tag) and 0xFFFF
	movk	w2,((tag) shr 16) and 0xFFFF,lsl 16
	cmp	w0,w2
	b.ne	@f
	str	x1,[x19,field]
@@:
}

; ----------------------------------------------------------------------------
; ttf_open : x0 = font, x1 = fichier TTF, x2 = taille, x3 = scratch,
;            x4 = taille du scratch  ->  x0 = 1 si la police est utilisable
; ----------------------------------------------------------------------------
ttf_open:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x5,x19				; structure a zero
	mov	x6,TTF_FONT_SIZE / 8
.z:	str	xzr,[x5],8
	subs	x6,x6,1
	b.ne	.z
	str	x20,[x19,F_DATA]
	str	x2,[x19,F_LEN]
	str	x3,[x19,F_SCRATCH]
	str	x4,[x19,F_SCRATCH_LEN]
	cmp	x2,12
	b.lo	.fail

	be16	w21,x20,4			; nombre de tables
	add	x22,x20,12
.tab:	cbz	w21,.tabs
	ldr	w0,[x22]			; etiquette (octets bruts)
	be32	w1,x22,8			; decalage
	add	x1,x20,x1
	ttf_tab	'head',F_THEAD
	ttf_tab	'maxp',F_TMAXP
	ttf_tab	'hhea',F_THHEA
	ttf_tab	'cmap',F_CMAP
	ttf_tab	'loca',F_LOCA
	ttf_tab	'glyf',F_GLYF
	ttf_tab	'hmtx',F_HMTX
	ttf_tab	'kern',F_TKERN
	add	x22,x22,16
	sub	w21,w21,1
	b	.tab
.tabs:	ldr	x0,[x19,F_THEAD]
	cbz	x0,.fail
	ldr	x0,[x19,F_TMAXP]
	cbz	x0,.fail
	ldr	x0,[x19,F_THHEA]
	cbz	x0,.fail
	ldr	x0,[x19,F_LOCA]
	cbz	x0,.fail
	ldr	x0,[x19,F_GLYF]
	cbz	x0,.fail
	ldr	x0,[x19,F_HMTX]
	cbz	x0,.fail

	ldr	x1,[x19,F_THEAD]		; --- head
	be16	w0,x1,18
	cbz	w0,.fail
	str	x0,[x19,F_UPEM]
	bes16	w0,x1,50
	str	x0,[x19,F_LOCFMT]
	ldr	x1,[x19,F_TMAXP]		; --- maxp
	be16	w0,x1,4
	str	x0,[x19,F_NGLYPHS]
	be16	w0,x1,6				; maxPoints
	be16	w2,x1,10			; maxCompositePoints
	cmp	w0,w2
	csel	w0,w0,w2,hs
	add	w0,w0,4
	str	x0,[x19,F_MAXPTS]
	mov	w2,12
	mul	w0,w0,w2
	add	w0,w0,15
	and	w0,w0,not 15
	str	x0,[x19,F_PTSBYTES]
	ldr	x1,[x19,F_THHEA]		; --- hhea
	bes16	w0,x1,4
	sxtw	x0,w0
	str	x0,[x19,F_ASC]
	bes16	w0,x1,6
	sxtw	x0,w0
	str	x0,[x19,F_DESC]
	bes16	w0,x1,8
	sxtw	x0,w0
	str	x0,[x19,F_GAP]
	be16	w0,x1,34
	cbz	w0,.fail
	str	x0,[x19,F_NHM]

	; --- cmap : on prefere le format 12 (Unicode complet), sinon le 4
	ldr	x20,[x19,F_CMAP]
	str	xzr,[x19,F_CMAP]
	cbz	x20,.fail
	be16	w21,x20,2
	add	x22,x20,4
.cm:	cbz	w21,.cm_done
	be16	w3,x22				; plateforme
	be16	w4,x22,2			; encodage
	be32	w5,x22,4
	add	x5,x20,x5			; sous-table
	be16	w6,x5				; format
	cmp	w6,12
	b.ne	.cm4
	cmp	w3,0
	b.eq	.take12
	cmp	w3,3
	b.ne	.cm_next
	cmp	w4,10
	b.ne	.cm_next
.take12:
	str	x5,[x19,F_CMAP]
	mov	x0,12
	str	x0,[x19,F_CMAPFMT]
	b	.cm_done
.cm4:	cmp	w6,4
	b.ne	.cm_next
	ldr	x0,[x19,F_CMAP]
	cbnz	x0,.cm_next
	cmp	w3,0
	b.eq	.take4
	cmp	w3,3
	b.ne	.cm_next
	cmp	w4,1
	b.ne	.cm_next
.take4:	str	x5,[x19,F_CMAP]
	mov	x0,4
	str	x0,[x19,F_CMAPFMT]
.cm_next:
	add	x22,x22,8
	sub	w21,w21,1
	b	.cm
.cm_done:
	ldr	x0,[x19,F_CMAP]
	cbz	x0,.fail

	; --- kern (format 0, horizontal, premiere sous-table)
	ldr	x1,[x19,F_TKERN]
	cbz	x1,.kern_done
	be16	w0,x1
	cbnz	w0,.kern_done			; seule la version 0 (Microsoft)
	be16	w0,x1,2
	cbz	w0,.kern_done
	add	x2,x1,4
	be16	w3,x2,4				; coverage
	lsr	w4,w3,8
	cbnz	w4,.kern_done			; format 0 seulement
	tbz	w3,0,.kern_done			; horizontal
	be16	w0,x2,6
	str	x0,[x19,F_NKERN]
	add	x2,x2,14
	str	x2,[x19,F_KERN]
.kern_done:
	mov	x0,x19
	mov	x1,16
	bl	ttf_set_size
	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ----------------------------------------------------------------------------
; ttf_set_size : x0 = font, x1 = taille en pixels par em
; ----------------------------------------------------------------------------
ttf_set_size:
	str	x1,[x0,F_PIXELS]
	scvtf	s0,x1
	ldr	x2,[x0,F_UPEM]
	scvtf	s1,x2
	fdiv	s0,s0,s1
	str	s0,[x0,F_SCALE]
	ldr	x2,[x0,F_ASC]
	scvtf	s1,x2
	fmul	s1,s1,s0
	fcvtps	x3,s1				; ascendante arrondie vers le haut
	str	x3,[x0,F_ASC_PX]
	ldr	x4,[x0,F_DESC]
	neg	x4,x4
	scvtf	s1,x4
	fmul	s1,s1,s0
	fcvtps	x5,s1
	str	x5,[x0,F_DESC_PX]
	ldr	x6,[x0,F_GAP]
	add	x2,x2,x4
	add	x2,x2,x6
	scvtf	s1,x2
	fmul	s1,s1,s0
	fcvtns	x2,s1
	add	x3,x3,x5
	cmp	x2,x3
	csel	x2,x2,x3,hs
	str	x2,[x0,F_LINE_PX]
	ret

; ----------------------------------------------------------------------------
; ttf_glyph_index : x0 = font, x1 = point de code Unicode -> x0 = glyphe (0 = absent)
; ----------------------------------------------------------------------------
ttf_glyph_index:
	ldr	x2,[x0,F_CMAP]
	ldr	x3,[x0,F_CMAPFMT]
	ldr	x15,[x0,F_NGLYPHS]
	cmp	x3,12
	b.eq	.f12
	; --- format 4 : segments [start, end] tries par end
	mov	x3,0xFFFF
	cmp	x1,x3
	b.hi	.none
	be16	w4,x2,6				; segCountX2
	lsr	w5,w4,1
	cbz	w5,.none
	add	x6,x2,14			; endCode[]
	mov	w7,0
	sub	w8,w5,1
.bs4:	cmp	w7,w8				; premier segment avec end >= cp
	b.hs	.f4
	add	w9,w7,w8
	lsr	w9,w9,1
	add	x10,x6,x9,lsl 1
	be16	w10,x10
	cmp	w10,w1
	b.hs	.left4
	add	w7,w9,1
	b	.bs4
.left4:	mov	w8,w9
	b	.bs4
.f4:	add	x10,x6,x7,lsl 1
	be16	w11,x10
	cmp	w11,w1
	b.lo	.none
	add	x12,x6,x4
	add	x12,x12,2
	add	x12,x12,x7,lsl 1		; startCode[i]
	be16	w13,x12
	cmp	w1,w13
	b.lo	.none
	add	x14,x12,x4			; idDelta[i]
	be16	w9,x14
	add	x16,x14,x4			; idRangeOffset[i]
	be16	w17,x16
	cbnz	w17,.ro
	add	w0,w1,w9
	and	w0,w0,0xFFFF
	b	.check
.ro:	sub	w10,w1,w13
	add	x16,x16,x17
	add	x16,x16,x10,lsl 1
	be16	w0,x16
	cbz	w0,.none
	add	w0,w0,w9
	and	w0,w0,0xFFFF
	b	.check
	; --- format 12 : groupes [start, end] -> startGlyph
.f12:	be32	w4,x2,12			; nGroups
	cbz	w4,.none
	add	x6,x2,16
	mov	w7,0
	sub	w8,w4,1
.bs12:	cmp	w7,w8
	b.hs	.g12
	add	w9,w7,w8
	lsr	w9,w9,1
	mov	w10,12
	umaddl	x10,w9,w10,x6
	be32	w11,x10,4			; endCharCode
	cmp	w11,w1
	b.hs	.left12
	add	w7,w9,1
	b	.bs12
.left12:
	mov	w8,w9
	b	.bs12
.g12:	mov	w10,12
	umaddl	x10,w7,w10,x6
	be32	w11,x10				; start
	be32	w12,x10,4			; end
	cmp	w1,w11
	b.lo	.none
	cmp	w1,w12
	b.hi	.none
	be32	w13,x10,8
	sub	w0,w1,w11
	add	w0,w0,w13
.check:	cmp	x0,x15
	b.hs	.none
	ret
.none:	mov	x0,0
	ret

; x0 = font, x1 = glyphe -> x0 = donnees glyf, x1 = longueur (0 si vide)
ttf__glyph_loc:
	ldr	x2,[x0,F_NGLYPHS]
	cmp	x1,x2
	b.hs	.none
	ldr	x3,[x0,F_LOCA]
	ldr	x4,[x0,F_LOCFMT]
	cbnz	x4,.long
	add	x5,x3,x1,lsl 1
	be16	w6,x5
	be16	w7,x5,2
	lsl	w6,w6,1
	lsl	w7,w7,1
	b	.got
.long:	add	x5,x3,x1,lsl 2
	be32	w6,x5
	be32	w7,x5,4
.got:	cmp	w7,w6
	b.ls	.none
	sub	w1,w7,w6
	ldr	x2,[x0,F_GLYF]
	add	x0,x2,x6
	ret
.none:	mov	x0,0
	mov	x1,0
	ret

; x0 = font, x1 = glyphe -> x0 = avance (unites)
ttf__advance:
	ldr	x2,[x0,F_NHM]
	ldr	x3,[x0,F_HMTX]
	sub	x4,x2,1
	cmp	x1,x2
	csel	x1,x1,x4,lo
	add	x4,x3,x1,lsl 2
	be16	w0,x4
	ret

; ----------------------------------------------------------------------------
; ttf_kern : x0 = font, x1 = glyphe gauche, x2 = glyphe droit -> x0 = valeur (unites)
; ----------------------------------------------------------------------------
ttf_kern:
	ldr	x3,[x0,F_KERN]
	cbz	x3,.none
	ldr	x4,[x0,F_NKERN]
	cbz	x4,.none
	orr	w5,w2,w1,lsl 16			; cle (gauche << 16) | droit
	mov	w6,0
	sub	w7,w4,1
	mov	w9,6
.bs:	cmp	w6,w7
	b.hi	.none
	add	w8,w6,w7
	lsr	w8,w8,1
	umaddl	x10,w8,w9,x3
	be32	w11,x10
	cmp	w11,w5
	b.eq	.found
	b.hi	.left
	add	w6,w8,1
	b	.bs
.left:	cbz	w8,.none
	sub	w7,w8,1
	b	.bs
.found:	bes16	w0,x10,4
	sxtw	x0,w0
	ret
.none:	mov	x0,0
	ret

; ----------------------------------------------------------------------------
; Accumulation d'un segment dans la grille.
; x0 = font, (s0,s1) -> (s2,s3) en pixels de la grille. Feuille.
; ----------------------------------------------------------------------------
ttf__line:
	fcmp	s1,s3
	b.eq	.ret
	fmov	s4,1.0				; sens
	b.mi	.ordered
	fmov	s5,s0
	fmov	s0,s2
	fmov	s2,s5
	fmov	s5,s1
	fmov	s1,s3
	fmov	s3,s5
	fmov	s4,-1.0
.ordered:
	ldr	x10,[x0,F_ACC]
	ldr	x11,[x0,F_ACCW]
	ldr	x12,[x0,F_ACCH]
	fsub	s5,s2,s0
	fsub	s6,s3,s1
	fdiv	s5,s5,s6			; dx/dy
	fmov	s7,s0				; x courant
	fcmp	s1,0.0
	b.ge	.ypos
	fmsub	s7,s1,s5,s7			; x a y = 0
	mov	w13,0
	b	.yend
.ypos:	fcvtms	w13,s1
.yend:	fcvtps	w14,s3
	cmp	w14,w12
	csel	w14,w14,w12,lt
	fmov	s26,1.0
	fmov	s27,0.5
	sub	w9,w11,2			; derniers index autorises
	sub	w8,w11,1
.row:	cmp	w13,w14
	b.ge	.ret
	scvtf	s16,w13
	fadd	s17,s16,s26
	fmin	s18,s17,s3
	fmax	s19,s16,s1
	fsub	s18,s18,s19			; dy dans cette ligne
	fmadd	s19,s5,s18,s7			; x suivant
	fmul	s20,s18,s4			; d = dy * sens
	fmin	s21,s7,s19			; x0
	fmax	s22,s7,s19			; x1
	frintm	s23,s21
	fcvtzs	w15,s23
	frintp	s24,s22
	fcvtzs	w16,s24
	cmp	w15,0				; bornage memoire
	csel	w15,w15,wzr,gt
	cmp	w15,w9
	csel	w15,w15,w9,lt
	cmp	w16,w8
	csel	w16,w16,w8,lt
	mul	w17,w13,w11
	add	x17,x10,w17,uxtw 2		; debut de ligne
	add	w6,w15,1
	cmp	w16,w6
	b.gt	.multi
	; --- une seule cellule (et sa voisine)
	fadd	s25,s7,s19
	fmul	s25,s25,s27
	fsub	s25,s25,s23			; milieu - x0 entier
	fmul	s28,s20,s25
	fsub	s29,s20,s28
	add	x6,x17,w15,uxtw 2
	ldp	s30,s31,[x6]
	fadd	s30,s30,s29
	fadd	s31,s31,s28
	stp	s30,s31,[x6]
	b	.next
.multi:	fsub	s25,s22,s21
	fdiv	s25,s26,s25			; s = 1/(x1-x0)
	fsub	s28,s21,s23			; x0f
	fsub	s29,s26,s28
	fmul	s29,s29,s29
	fmul	s29,s29,s25
	fmul	s29,s29,s27			; a0 = s/2 (1-x0f)^2
	fsub	s30,s22,s24
	fadd	s30,s30,s26			; x1f
	fmul	s30,s30,s30
	fmul	s30,s30,s25
	fmul	s30,s30,s27			; am = s/2 x1f^2
	add	x6,x17,w15,uxtw 2
	ldr	s31,[x6]
	fmadd	s31,s20,s29,s31
	str	s31,[x6]
	add	w7,w15,2
	cmp	w16,w7
	b.ne	.wide
	fsub	s31,s26,s29
	fsub	s31,s31,s30
	ldr	s0,[x6,4]
	fmadd	s0,s20,s31,s0
	str	s0,[x6,4]
	b	.last
.wide:	fmov	s0,1.5
	fsub	s0,s0,s28
	fmul	s0,s0,s25			; a1 = s (1.5 - x0f)
	fsub	s6,s0,s29
	ldr	s2,[x6,4]
	fmadd	s2,s20,s6,s2
	str	s2,[x6,4]
	fmul	s6,s20,s25			; d * s
	sub	w5,w16,1
.mid:	cmp	w7,w5
	b.ge	.mid_done
	add	x4,x17,w7,uxtw 2
	ldr	s2,[x4]
	fadd	s2,s2,s6
	str	s2,[x4]
	add	w7,w7,1
	b	.mid
.mid_done:
	sub	w7,w16,w15
	sub	w7,w7,3
	scvtf	s2,w7
	fmadd	s2,s2,s25,s0			; a2
	fsub	s31,s26,s2
	fsub	s31,s31,s30
	add	x4,x17,w5,uxtw 2
	ldr	s2,[x4]
	fmadd	s2,s20,s31,s2
	str	s2,[x4]
.last:	add	x4,x17,w16,uxtw 2
	ldr	s2,[x4]
	fmadd	s2,s20,s30,s2
	str	s2,[x4]
.next:	fmov	s7,s19
	add	w13,w13,1
	b	.row
.ret:	ret

; x0 = font, x1 = point A, x2 = point B (paires de flottants)
ttf__seg_line:
	ldp	s0,s1,[x1]
	ldp	s2,s3,[x2]
	b	ttf__line

; x0 = font, x1 = P0, x2 = P1 (controle), x3 = P2 : courbe quadratique aplatie
ttf__seg_quad:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	ldr	d0,[x1]				; copies : [sp] P0, [sp+8] P1, [sp+16] P2,
	str	d0,[sp]				;          [sp+24] point precedent
	str	d0,[sp,24]
	ldr	d0,[x2]
	str	d0,[sp,8]
	ldr	d0,[x3]
	str	d0,[sp,16]
	ldp	s0,s1,[sp]			; deviation |P0 - 2 P1 + P2|^2
	ldp	s2,s3,[sp,8]
	ldp	s4,s5,[sp,16]
	fadd	s6,s0,s4
	fsub	s6,s6,s2
	fsub	s6,s6,s2
	fadd	s7,s1,s5
	fsub	s7,s7,s3
	fsub	s7,s7,s3
	fmul	s6,s6,s6
	fmadd	s6,s7,s7,s6
	fmov	s7,3.0
	fmul	s6,s6,s7
	fsqrt	s6,s6
	fsqrt	s6,s6
	fcvtzs	w20,s6
	add	w20,w20,1			; nombre de segments
	cmp	w20,32
	mov	w0,32
	csel	w20,w20,w0,lo
	mov	w21,1
.step:	cmp	w21,w20
	b.hi	.done
	scvtf	s0,w21
	scvtf	s1,w20
	fdiv	s0,s0,s1			; t
	fmov	s1,1.0
	fsub	s1,s1,s0			; 1 - t
	fmul	s2,s1,s1			; (1-t)^2
	fmul	s3,s0,s1
	fadd	s3,s3,s3			; 2t(1-t)
	fmul	s4,s0,s0			; t^2
	ldp	s16,s17,[sp]
	ldp	s18,s19,[sp,8]
	ldp	s20,s21,[sp,16]
	fmul	s5,s2,s16
	fmadd	s5,s3,s18,s5
	fmadd	s5,s4,s20,s5			; x
	fmul	s6,s2,s17
	fmadd	s6,s3,s19,s6
	fmadd	s6,s4,s21,s6			; y
	ldr	s0,[sp,24]			; segment depuis le point precedent
	ldr	s1,[sp,28]
	fmov	s2,s5
	fmov	s3,s6
	str	s5,[sp,24]
	str	s6,[sp,28]
	mov	x0,x19
	bl	ttf__line
	add	w21,w21,1
	b	.step
.done:	add	sp,sp,32
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ----------------------------------------------------------------------------
; Parcours d'un contour : x0 = font, x1 = premier point, x2 = nombre de points
; Points : 12 octets (x flottant, y flottant, drapeaux), bit 0 = sur la courbe
; ----------------------------------------------------------------------------
PT_SIZE		= 12
C_CUR		= 0
C_CTRL		= 8
C_START		= 16
C_MID		= 24
ttf__contour:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x21,2
	b.lt	.out
	mov	x0,PT_SIZE
	sub	x1,x21,1
	madd	x22,x1,x0,x20			; dernier point
	ldrb	w0,[x20,8]
	tbz	w0,0,.first_off
	ldr	d0,[x20]			; debut = premier point (sur la courbe)
	str	d0,[sp,C_START]
	add	x23,x20,PT_SIZE
	sub	x24,x21,1
	b	.go
.first_off:
	ldrb	w0,[x22,8]
	tbz	w0,0,.both_off
	ldr	d0,[x22]			; debut = dernier point
	str	d0,[sp,C_START]
	mov	x23,x20
	sub	x24,x21,1
	b	.go
.both_off:					; debut = milieu implicite
	ldp	s0,s1,[x20]
	ldp	s2,s3,[x22]
	fadd	s0,s0,s2
	fadd	s1,s1,s3
	fmov	s4,0.5
	fmul	s0,s0,s4
	fmul	s1,s1,s4
	stp	s0,s1,[sp,C_START]
	mov	x23,x20
	mov	x24,x21
.go:	ldr	d0,[sp,C_START]
	str	d0,[sp,C_CUR]
	mov	x21,0				; x21 = controle en attente
.pt:	cbz	x24,.close
	ldrb	w0,[x23,8]
	tbz	w0,0,.off
	mov	x0,x19				; point sur la courbe
	add	x1,sp,C_CUR
	cbz	x21,.on_line
	add	x2,sp,C_CTRL
	mov	x3,x23
	bl	ttf__seg_quad
	b	.on_done
.on_line:
	mov	x2,x23
	bl	ttf__seg_line
.on_done:
	ldr	d0,[x23]
	str	d0,[sp,C_CUR]
	mov	x21,0
	b	.adv
.off:	cbz	x21,.set_ctrl			; point hors courbe
	ldp	s0,s1,[sp,C_CTRL]		; deux controles : milieu implicite
	ldp	s2,s3,[x23]
	fadd	s0,s0,s2
	fadd	s1,s1,s3
	fmov	s4,0.5
	fmul	s0,s0,s4
	fmul	s1,s1,s4
	stp	s0,s1,[sp,C_MID]
	mov	x0,x19
	add	x1,sp,C_CUR
	add	x2,sp,C_CTRL
	add	x3,sp,C_MID
	bl	ttf__seg_quad
	ldr	d0,[sp,C_MID]
	str	d0,[sp,C_CUR]
.set_ctrl:
	ldr	d0,[x23]
	str	d0,[sp,C_CTRL]
	mov	x21,1
.adv:	add	x23,x23,PT_SIZE
	sub	x24,x24,1
	b	.pt
.close:	mov	x0,x19
	add	x1,sp,C_CUR
	cbz	x21,.close_line
	add	x2,sp,C_CTRL
	add	x3,sp,C_START
	bl	ttf__seg_quad
	b	.out
.close_line:
	add	x2,sp,C_START
	bl	ttf__seg_line
.out:	add	sp,sp,32
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ----------------------------------------------------------------------------
; Glyphe simple : x0 = font, x1 = donnees glyf, x2 = nombre de contours,
; x3 = transformation (6 flottants : xx xy yx yy dx dy, unites -> grille)
; ----------------------------------------------------------------------------
ttf__simple:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	cbz	x21,.out
	ldr	x23,[x19,F_SCRATCH]		; zone des points
	add	x25,x20,10			; endPtsOfContours[]
	sub	x0,x21,1
	add	x0,x25,x0,lsl 1
	be16	w24,x0
	add	w24,w24,1			; nombre de points
	ldr	x0,[x19,F_MAXPTS]
	cmp	x24,x0
	b.hi	.out
	add	x0,x25,x21,lsl 1
	be16	w1,x0				; longueur des instructions
	add	x0,x0,2
	add	x26,x0,x1			; drapeaux

	; --- drapeaux (avec repetitions)
	mov	x1,0
	mov	x5,PT_SIZE
.fl:	cmp	x1,x24
	b.hs	.fl_done
	ldrb	w2,[x26],1
	madd	x3,x1,x5,x23
	strb	w2,[x3,8]
	add	x1,x1,1
	tbz	w2,3,.fl
	ldrb	w4,[x26],1			; nombre de repetitions
.rep:	cbz	w4,.fl
	cmp	x1,x24
	b.hs	.fl_done
	madd	x3,x1,x5,x23
	strb	w2,[x3,8]
	add	x1,x1,1
	sub	w4,w4,1
	b	.rep
.fl_done:
	; --- coordonnees x (bits 1 = court, 4 = signe/identique)
	mov	x1,0
	mov	w6,0
.xs:	cmp	x1,x24
	b.hs	.xs_done
	madd	x3,x1,x5,x23
	ldrb	w2,[x3,8]
	tbz	w2,1,.x_long
	ldrb	w4,[x26],1
	tbnz	w2,4,.x_pos
	neg	w4,w4
.x_pos:	add	w6,w6,w4
	b	.x_set
.x_long:
	tbnz	w2,4,.x_set			; identique au precedent
	ldrb	w4,[x26],1
	ldrb	w7,[x26],1
	orr	w4,w7,w4,lsl 8
	sxth	w4,w4
	add	w6,w6,w4
.x_set:	str	w6,[x3]
	add	x1,x1,1
	b	.xs
.xs_done:
	; --- coordonnees y (bits 2 = court, 5 = signe/identique)
	mov	x1,0
	mov	w6,0
.ys:	cmp	x1,x24
	b.hs	.ys_done
	madd	x3,x1,x5,x23
	ldrb	w2,[x3,8]
	tbz	w2,2,.y_long
	ldrb	w4,[x26],1
	tbnz	w2,5,.y_pos
	neg	w4,w4
.y_pos:	add	w6,w6,w4
	b	.y_set
.y_long:
	tbnz	w2,5,.y_set
	ldrb	w4,[x26],1
	ldrb	w7,[x26],1
	orr	w4,w7,w4,lsl 8
	sxth	w4,w4
	add	w6,w6,w4
.y_set:	str	w6,[x3,4]
	add	x1,x1,1
	b	.ys
.ys_done:
	; --- transformation vers la grille
	ldp	s16,s17,[x22]			; xx, xy
	ldp	s18,s19,[x22,8]			; yx, yy
	ldp	s20,s21,[x22,16]		; dx, dy
	mov	x1,0
.tr:	cmp	x1,x24
	b.hs	.tr_done
	madd	x3,x1,x5,x23
	ldp	w6,w7,[x3]
	scvtf	s0,w6
	scvtf	s1,w7
	fmul	s2,s16,s0
	fmadd	s2,s17,s1,s2
	fadd	s2,s2,s20
	fmul	s3,s18,s0
	fmadd	s3,s19,s1,s3
	fadd	s3,s3,s21
	stp	s2,s3,[x3]
	add	x1,x1,1
	b	.tr
.tr_done:
	; --- contours
	mov	x26,0				; premier point du contour
	mov	x22,0				; index du contour
.ct:	cmp	x22,x21
	b.hs	.out
	add	x0,x25,x22,lsl 1
	be16	w1,x0				; dernier point du contour
	cmp	x1,x24
	b.hs	.out
	cmp	x1,x26
	b.lo	.out
	sub	x2,x1,x26
	add	x2,x2,1
	mov	x0,PT_SIZE
	madd	x1,x26,x0,x23
	add	x26,x26,x2
	mov	x0,x19
	bl	ttf__contour
	add	x22,x22,1
	b	.ct
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ----------------------------------------------------------------------------
; Contour d'un glyphe quelconque : x0 = font, x1 = glyphe, x2 = transformation,
; x3 = profondeur (glyphes composites imbriques)
; ----------------------------------------------------------------------------
ttf__outline:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,32			; transformation composee (6 flottants)
	mov	x19,x0
	mov	x21,x2
	mov	x22,x3
	cmp	x22,6
	b.hi	.out
	bl	ttf__glyph_loc
	cmp	x1,10
	b.lo	.out
	mov	x20,x0
	bes16	w2,x20				; nombre de contours
	tbnz	w2,31,.composite
	mov	x0,x19
	mov	x1,x20
	sxtw	x2,w2
	mov	x3,x21
	bl	ttf__simple
	b	.out

.composite:
	add	x23,x20,10			; curseur dans les composants
.comp:	be16	w24,x23				; drapeaux
	be16	w25,x23,2			; glyphe du composant
	add	x23,x23,4
	tbz	w24,0,.args8
	bes16	w0,x23
	bes16	w1,x23,2
	add	x23,x23,4
	b	.args
.args8:	ldrsb	w0,[x23]
	ldrsb	w1,[x23,1]
	add	x23,x23,2
.args:	tbnz	w24,1,.xy			; ARGS_ARE_XY_VALUES ; sinon points apparies
	mov	w0,0
	mov	w1,0
.xy:	scvtf	s4,w0				; dx
	scvtf	s5,w1				; dy
	fmov	s0,1.0				; xx
	fmov	s1,wzr				; xy
	fmov	s2,wzr				; yx
	fmov	s3,1.0				; yy
	mov	w6,16384
	scvtf	s6,w6
	tbz	w24,3,.no_scale
	bes16	w0,x23				; WE_HAVE_A_SCALE
	scvtf	s0,w0
	fdiv	s0,s0,s6
	fmov	s3,s0
	add	x23,x23,2
	b	.matrix
.no_scale:
	tbz	w24,6,.no_xy
	bes16	w0,x23				; WE_HAVE_AN_X_AND_Y_SCALE
	scvtf	s0,w0
	fdiv	s0,s0,s6
	bes16	w0,x23,2
	scvtf	s3,w0
	fdiv	s3,s3,s6
	add	x23,x23,4
	b	.matrix
.no_xy:	tbz	w24,7,.matrix
	bes16	w0,x23				; WE_HAVE_A_TWO_BY_TWO
	scvtf	s0,w0
	fdiv	s0,s0,s6			; xscale
	bes16	w0,x23,2
	scvtf	s2,w0
	fdiv	s2,s2,s6			; scale01 -> yx
	bes16	w0,x23,4
	scvtf	s1,w0
	fdiv	s1,s1,s6			; scale10 -> xy
	bes16	w0,x23,6
	scvtf	s3,w0
	fdiv	s3,s3,s6			; yscale
	add	x23,x23,8
.matrix:					; R = parent o composant
	ldp	s16,s17,[x21]			; P.xx P.xy
	ldp	s18,s19,[x21,8]			; P.yx P.yy
	ldp	s20,s21,[x21,16]		; P.dx P.dy
	fmul	s22,s16,s0
	fmadd	s22,s17,s2,s22			; R.xx
	fmul	s23,s16,s1
	fmadd	s23,s17,s3,s23			; R.xy
	fmul	s24,s18,s0
	fmadd	s24,s19,s2,s24			; R.yx
	fmul	s25,s18,s1
	fmadd	s25,s19,s3,s25			; R.yy
	fmul	s26,s16,s4
	fmadd	s26,s17,s5,s26
	fadd	s26,s26,s20			; R.dx
	fmul	s27,s18,s4
	fmadd	s27,s19,s5,s27
	fadd	s27,s27,s21			; R.dy
	stp	s22,s23,[sp]
	stp	s24,s25,[sp,8]
	stp	s26,s27,[sp,16]
	mov	x0,x19
	mov	x1,x25
	mov	x2,sp
	add	x3,x22,1
	bl	ttf__outline
	tbnz	w24,5,.comp			; MORE_COMPONENTS
.out:	add	sp,sp,32
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ----------------------------------------------------------------------------
; ttf_raster : x0 = font, x1 = glyphe -> x0 = 1 (ou 0 si scratch trop petit)
; Remplit la grille et F_GX0, F_GY0, F_GW, F_ACCH, F_ACCW, F_GADV.
; ----------------------------------------------------------------------------
ttf_raster:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	mov	x20,x1
	bl	ttf__advance			; avance en 1/64 de pixel
	scvtf	s0,x0
	ldr	s1,[x19,F_SCALE]
	fmul	s0,s0,s1
	mov	w2,64
	scvtf	s2,w2
	fmul	s0,s0,s2
	fcvtns	x0,s0
	str	x0,[x19,F_GADV]
	str	xzr,[x19,F_GW]
	str	xzr,[x19,F_ACCH]
	str	xzr,[x19,F_GX0]
	str	xzr,[x19,F_GY0]
	mov	x0,x19
	mov	x1,x20
	bl	ttf__glyph_loc
	cmp	x1,10
	b.lo	.ok				; glyphe vide (espace)
	ldr	s0,[x19,F_SCALE]
	bes16	w1,x0,2				; xMin
	bes16	w2,x0,4				; yMin
	bes16	w3,x0,6				; xMax
	bes16	w4,x0,8				; yMax
	scvtf	s1,w1
	fmul	s1,s1,s0
	fcvtms	w21,s1				; bx0 = floor(xMin * s)
	scvtf	s1,w3
	fmul	s1,s1,s0
	fcvtps	w5,s1				; bx1 = ceil(xMax * s)
	scvtf	s1,w4
	fnmul	s1,s1,s0
	fcvtms	w22,s1				; by0 = floor(-yMax * s)
	scvtf	s1,w2
	fnmul	s1,s1,s0
	fcvtps	w6,s1				; by1 = ceil(-yMin * s)
	sub	w23,w5,w21			; largeur
	sub	w24,w6,w22			; hauteur
	cmp	w23,0
	b.le	.ok
	cmp	w24,0
	b.le	.ok
	add	w7,w23,3			; largeur de ligne de la grille
	add	w8,w24,1
	mul	w8,w8,w7
	lsl	x8,x8,2
	add	x8,x8,15
	and	x8,x8,not 15			; octets de grille
	ldr	x9,[x19,F_PTSBYTES]
	ldr	x10,[x19,F_SCRATCH_LEN]
	add	x11,x9,x8
	cmp	x11,x10
	b.hi	.fail
	ldr	x10,[x19,F_SCRATCH]
	add	x10,x10,x9
	str	x10,[x19,F_ACC]
	sxtw	x7,w7
	str	x7,[x19,F_ACCW]
	sxtw	x0,w24
	str	x0,[x19,F_ACCH]
	sxtw	x0,w21
	str	x0,[x19,F_GX0]
	sxtw	x0,w22
	str	x0,[x19,F_GY0]
	add	w0,w23,2
	sxtw	x0,w0
	str	x0,[x19,F_GW]
.zero:	stp	xzr,xzr,[x10],16		; grille a zero
	subs	x8,x8,16
	b.ne	.zero
	; transformation initiale : unites -> grille (y vers le bas)
	ldr	s0,[x19,F_SCALE]
	fmov	s1,wzr
	fneg	s3,s0
	scvtf	s4,w21
	fneg	s4,s4
	scvtf	s5,w22
	fneg	s5,s5
	stp	s0,s1,[sp]
	stp	s1,s3,[sp,8]
	stp	s4,s5,[sp,16]
	mov	x0,x19
	mov	x1,x20
	mov	x2,sp
	mov	x3,0
	bl	ttf__outline
.ok:	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	add	sp,sp,32
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ----------------------------------------------------------------------------
; ttf_render_glyph : x0 = font, x1 = glyphe, x2 = metriques (GM_SIZE octets),
;   x3 = bitmap 8 bits de destination (0 = metriques seules), x4 = pas en octets
;   -> x0 = 1 si succes. Le bitmap doit faire au moins GM_W x GM_H.
; ----------------------------------------------------------------------------
ttf_render_glyph:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	mov	x21,x3
	mov	x22,x4
	bl	ttf_raster
	cbz	x0,.out
	ldr	x0,[x19,F_GW]
	ldr	x1,[x19,F_ACCH]
	stp	w0,w1,[x20,GM_W]
	ldr	x0,[x19,F_GX0]
	ldr	x1,[x19,F_GY0]
	stp	w0,w1,[x20,GM_BX]
	ldr	x0,[x19,F_GADV]
	str	w0,[x20,GM_ADV64]
	cbz	x21,.done
	ldr	x2,[x19,F_ACC]
	ldr	x3,[x19,F_ACCW]
	ldr	x4,[x19,F_ACCH]
	ldr	x5,[x19,F_GW]
	fmov	s6,1.0
	mov	w7,255
	scvtf	s7,w7
.row:	cbz	x4,.done
	fmov	s0,wzr				; somme prefixe de la ligne
	mov	x6,0
.col:	cmp	x6,x5
	b.hs	.row_end
	ldr	s1,[x2,x6,lsl 2]
	fadd	s0,s0,s1
	fabs	s1,s0
	fmin	s1,s1,s6
	fmul	s1,s1,s7
	fcvtns	w7,s1
	strb	w7,[x21,x6]
	add	x6,x6,1
	b	.col
.row_end:
	add	x2,x2,x3,lsl 2
	add	x21,x21,x22
	sub	x4,x4,1
	b	.row
.done:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ----------------------------------------------------------------------------
; ttf_utf8_next : x0 = chaine UTF-8 -> x0 = point de code (0 = fin), x1 = suite
; ----------------------------------------------------------------------------
ttf_utf8_next:
	ldrb	w2,[x0]
	cbz	w2,.end
	add	x1,x0,1
	tbnz	w2,7,.multi
	mov	w0,w2
	ret
.multi:	and	w3,w2,0xE0
	cmp	w3,0xC0
	b.ne	.three
	and	w0,w2,0x1F
	mov	w4,1
	b	.cont
.three:	and	w3,w2,0xF0
	cmp	w3,0xE0
	b.ne	.four
	and	w0,w2,0x0F
	mov	w4,2
	b	.cont
.four:	and	w3,w2,0xF8
	cmp	w3,0xF0
	b.ne	.bad
	and	w0,w2,0x07
	mov	w4,3
.cont:	ldrb	w2,[x1]
	and	w3,w2,0xC0
	cmp	w3,0x80
	b.ne	.bad				; suite invalide : on ne la consomme pas
	add	x1,x1,1
	and	w2,w2,0x3F
	orr	w0,w2,w0,lsl 6
	subs	w4,w4,1
	b.ne	.cont
	ret
.bad:	mov	w0,0xFFFD
	ret
.end:	mov	x0,0
	mov	x1,x0
	ret

; ----------------------------------------------------------------------------
; ttf__pick : x0 = font, x1 = point de code -> x0 = police qui a le glyphe (celle
;   de F_FALLBACK, a la meme taille, s'il manque), x1 = glyphe
; ----------------------------------------------------------------------------
ttf__pick:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	ttf_glyph_index
	mov	x21,x0
	cbnz	x21,.self
	ldr	x2,[x19,F_FALLBACK]
	cbz	x2,.self
	mov	x0,x19
	blr	x2
	cbz	x0,.self
	mov	x2,x0
	ldr	x1,[x19,F_PIXELS]
	ldr	x3,[x2,F_PIXELS]
	stp	x2,x3,[sp,-16]!
	cmp	x1,x3
	b.eq	.sized
	mov	x0,x2
	bl	ttf_set_size
.sized:	ldr	x0,[sp]
	mov	x1,x20
	bl	ttf_glyph_index
	ldp	x2,x3,[sp],16
	cbz	x0,.self			; absent des deux : celui de la police
	mov	x1,x0
	mov	x0,x2
	b	.out
.self:	mov	x0,x19
	mov	x1,x21
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ----------------------------------------------------------------------------
; ttf_text_width : x0 = font, x1 = chaine UTF-8 -> x0 = largeur en pixels
; ----------------------------------------------------------------------------
ttf_text_width:
	mov	x2,-1
	mov	x3,-1
	mov	x4,0
	b	ttf_text_pos

; ----------------------------------------------------------------------------
; ttf_text_pos : x0 = font, x1 = chaine UTF-8, x2 = octets au plus (-1 :
;   jusqu'au NUL), x3 = x cherche (-1 : aucun) -> x0 = largeur parcourue en
;   pixels, x1 = octets parcourus. Meme cumul que ttf_draw_text (1/64 de
;   pixel arrondi par glyphe) : la largeur d'un prefixe tombe exactement sur
;   le stylo du dessin (curseur de texte). Avec x3 >= 0, s'arrete avant le
;   premier glyphe dont le milieu (x4 = 0 : clic dans un texte) ou la fin
;   (x4 = 1 : ce qui tient dans x3 pixels) depasse x3.
; ----------------------------------------------------------------------------
ttf_text_pos:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x27,x4				; 0 milieu, 1 fin
	sub	sp,sp,16			; [x29-96] : 1 si tabulation, [x29-88] : police
	str	x0,[x29,-88]
	mov	x19,x0
	mov	x20,x1				; position courante
	mov	x25,x1				; debut
	add	x23,x1,x2			; fin (borne)
	cmn	x2,1
	b.ne	.lim
	mov	x23,-1
.lim:	lsl	x24,x3,6			; x cherche en 1/64 (negatif : aucun)
	tbz	x3,63,.go
	mov	x24,-1
.go:	mov	x21,0				; stylo en 1/64
	mov	x22,0				; glyphe precedent
.ch:	ldr	x0,[x29,-88]			; (apres un glyphe de repli : la police
	cmp	x0,x19				;  du texte, sans crenage entre les deux)
	csel	x22,x22,xzr,eq
	mov	x19,x0
	cmp	x20,x23
	b.hs	.end
	mov	x0,x20
	bl	ttf_utf8_next
	cbz	x0,.end
	cmp	x1,x23				; (glyphe coupe par la borne : arret)
	b.hi	.end
	mov	x26,x1				; position apres ce glyphe
	cmp	x0,13				; retour chariot : invisible, sans largeur
	b.ne	.ncr
	mov	x20,x1
	b	.ch
.ncr:	cmp	x0,9				; tabulation : 4 espaces
	cset	x1,eq
	str	x1,[x29,-96]
	mov	x1,32
	csel	x0,x1,x0,eq
	mov	x1,x0
	mov	x0,x19
	bl	ttf__pick
	ldr	x2,[x29,-88]
	cmp	x0,x2
	csel	x22,x22,xzr,eq
	mov	x19,x0
	mov	x0,x1
	str	x0,[sp,-16]!
	mov	x2,x21				; stylo avant le glyphe (kerning compris)
	cbz	x22,.nokern
	mov	x2,x0
	mov	x1,x22
	mov	x0,x19
	bl	ttf_kern
	scvtf	s0,x0
	ldr	s1,[x19,F_SCALE]
	fmul	s0,s0,s1
	mov	w2,64
	scvtf	s2,w2
	fmul	s0,s0,s2
	fcvtns	x0,s0
	add	x2,x21,x0
.nokern:
	ldr	x22,[sp],16
	str	x2,[sp,-16]!
	mov	x0,x19
	mov	x1,x22
	bl	ttf__adv64
	ldr	x2,[sp],16
	ldr	x1,[x29,-96]			; tabulation : jusqu'au taquet suivant
	cbz	x1,.na
	lsl	x1,x0,2
	cmp	x2,0
	csel	x3,xzr,x2,lt
	udiv	x4,x3,x1
	msub	x4,x4,x1,x3
	sub	x0,x1,x4
.na:	tbnz	x24,63,.take
	asr	x3,x0,1				; milieu (ou fin) du glyphe au-dela du x
	cbz	x27,.mid			; cherche : arret avant lui
	mov	x3,x0
.mid:	add	x3,x2,x3
	cmp	x3,x24
	b.gt	.end
.take:	add	x21,x2,x0
	mov	x20,x26
	b	.ch
.end:	add	x0,x21,32
	asr	x0,x0,6
	sub	x1,x20,x25
	add	sp,sp,16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ----------------------------------------------------------------------------
; ttf_draw_text : x0 = font, x1 = chaine UTF-8, x2 = surface 32 bpp,
;   x3 = pas en octets, x4 = x du stylo, x5 = y de la ligne de base,
;   w6 = couleur 0x00RRGGBB, x7 = clip (largeur | hauteur << 32)
;   Melange alpha avec le fond. -> x0 = x du stylo apres le texte
; ----------------------------------------------------------------------------
ttf_draw_text:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	lsl	x23,x4,6			; stylo en 1/64 de pixel
	mov	x24,x5
	mov	w25,w6
	mov	w26,w7				; largeur de clip
	lsr	x27,x7,32			; hauteur de clip
	sub	sp,sp,32			; [x29-96] : 1 si tabulation, [x29-88] : debut,
	str	x23,[x29,-88]			; [x29-112] : police
	str	x19,[x29,-112]
	mov	x28,0				; glyphe precedent
.ch:	ldr	x0,[x29,-112]			; (apres un glyphe de repli : la police
	cmp	x0,x19				;  du texte, sans crenage entre les deux)
	csel	x28,x28,xzr,eq
	mov	x19,x0
	mov	x0,x20
	bl	ttf_utf8_next
	cbz	x0,.end
	mov	x20,x1
	cmp	x0,13				; retour chariot : invisible
	b.eq	.ch
	cmp	x0,9				; tabulation : 4 espaces
	cset	x1,eq
	str	x1,[x29,-96]
	mov	x1,32
	csel	x0,x1,x0,eq
	mov	x1,x0
	mov	x0,x19
	bl	ttf__pick
	ldr	x2,[x29,-112]
	cmp	x0,x2
	csel	x28,x28,xzr,eq
	mov	x19,x0
	mov	x0,x1
	str	x0,[sp,-16]!
	cbz	x28,.nokern
	mov	x2,x0
	mov	x1,x28
	mov	x0,x19
	bl	ttf_kern
	scvtf	s0,x0
	ldr	s1,[x19,F_SCALE]
	fmul	s0,s0,s1
	mov	w2,64
	scvtf	s2,w2
	fmul	s0,s0,s2
	fcvtns	x0,s0
	add	x23,x23,x0
.nokern:
	ldr	x28,[sp],16
	ldr	x0,[x19,F_CACHE]
	cbz	x0,.direct
	mov	x0,x19
	mov	x1,x28
	bl	ttf__cache_get
	cbz	x0,.uncached
	ldrh	w1,[x0,TC_W]			; bitmap du cache
	cbz	w1,.advance
	mov	x1,x0
	mov	x0,x19
	mov	x2,x21
	mov	x3,x22
	add	x4,x23,32
	asr	x4,x4,6
	mov	x5,x24
	mov	w6,w25
	orr	x7,x26,x27,lsl 32
	bl	ttf__blend8
	b	.advance
.uncached:
	cbz	x1,.advance			; echec de rasterisation
	b	.from_acc			; trop gros pour le cache : grille valide
.direct:
	mov	x0,x19
	mov	x1,x28
	bl	ttf_raster
	cbz	x0,.advance
.from_acc:
	ldr	x0,[x19,F_GW]
	cbz	x0,.advance
	mov	x0,x19
	mov	x1,x21
	mov	x2,x22
	add	x3,x23,32
	asr	x3,x3,6
	ldr	x4,[x19,F_GX0]
	add	x3,x3,x4			; x du coin haut-gauche
	ldr	x4,[x19,F_GY0]
	add	x4,x24,x4			; y du coin haut-gauche
	mov	w5,w25
	mov	w6,w26
	mov	w7,w27
	bl	ttf__blend
.advance:
	mov	x0,x19
	mov	x1,x28
	bl	ttf__adv64
	ldr	x1,[x29,-96]			; tabulation : jusqu'au taquet suivant
	cbz	x1,.na
	lsl	x1,x0,2
	ldr	x2,[x29,-88]
	subs	x2,x23,x2
	csel	x2,xzr,x2,lt
	udiv	x3,x2,x1
	msub	x3,x3,x1,x2
	sub	x0,x1,x3
.na:	add	x23,x23,x0
	b	.ch
.end:	add	x0,x23,32
	asr	x0,x0,6
	add	sp,sp,32
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; Melange de la grille courante : x0 = font, x1 = surface, x2 = pas,
; x3 = x, x4 = y (coin haut-gauche), w5 = couleur, w6/w7 = clip largeur/hauteur
ttf__blend:
	stp	x19,x20,[sp,-16]!
	ldr	x8,[x0,F_ACC]
	ldr	x9,[x0,F_ACCW]
	ldr	x10,[x0,F_ACCH]
	ldr	x11,[x0,F_GW]
	sxtw	x6,w6
	sxtw	x7,w7
	fmov	s6,1.0
	mov	w12,256
	scvtf	s7,w12
	and	w13,w5,0x00FF00FF		; rouge + bleu
	and	w14,w5,0x0000FF00		; vert
.row:	cbz	x10,.ret
	cmp	x4,0
	b.lt	.row_next
	cmp	x4,x7
	b.ge	.ret
	madd	x15,x4,x2,x1			; ligne de destination
	fmov	s0,wzr
	mov	x16,0				; colonne
.col:	cmp	x16,x11
	b.hs	.row_next
	ldr	s1,[x8,x16,lsl 2]
	fadd	s0,s0,s1
	add	x17,x3,x16			; x ecran
	add	x16,x16,1
	tbnz	x17,63,.col
	cmp	x17,x6
	b.ge	.col
	fabs	s1,s0
	fmin	s1,s1,s6
	fmul	s1,s1,s7
	fcvtns	w12,s1				; alpha 0..256
	cbz	w12,.col
	add	x5,x15,x17,lsl 2
	cmp	w12,256
	b.lo	.mix
	orr	w19,w13,w14			; opaque : couleur pure
	str	w19,[x5]
	b	.col
.mix:	ldr	w5,[x5]				; fond
	mov	w20,256
	sub	w20,w20,w12			; 256 - alpha
	and	w0,w5,0x00FF00FF
	mul	w0,w0,w20
	madd	w0,w13,w12,w0
	lsr	w0,w0,8
	and	w0,w0,0x00FF00FF
	and	w19,w5,0x0000FF00
	mul	w19,w19,w20
	madd	w19,w14,w12,w19
	lsr	w19,w19,8
	and	w19,w19,0x0000FF00
	orr	w0,w0,w19
	str	w0,[x15,x17,lsl 2]
	b	.col
.row_next:
	add	x8,x8,x9,lsl 2
	add	x4,x4,1
	sub	x10,x10,1
	b	.row
.ret:	ldp	x19,x20,[sp],16
	ret

; x0 = font, x1 = glyphe -> x0 = avance en 1/64 de pixel
ttf__adv64:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	ttf__advance
	scvtf	s0,x0
	ldr	s1,[x19,F_SCALE]
	fmul	s0,s0,s1
	mov	w2,64
	scvtf	s2,w2
	fmul	s0,s0,s2
	fcvtns	x0,s0
	ldr	x19,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ----------------------------------------------------------------------------
; ttf_set_cache : x0 = font, x1 = tampon (aligne 16), x2 = taille
;   (>= TTF_CACHE_MIN ; 0 = pas de cache). Plusieurs polices peuvent avoir
;   chacune leur cache ; un cache ne se partage pas entre deux polices.
; ----------------------------------------------------------------------------
ttf_set_cache:
	cmp	x2,TTF_CACHE_MIN
	b.lo	.off
	str	x1,[x0,F_CACHE]
	str	x2,[x0,F_CACHE_LEN]
	b	ttf__cache_flush
.off:	str	xzr,[x0,F_CACHE]
	ret

; x0 = font : vide le cache
ttf__cache_flush:
	str	xzr,[x0,F_CACHE_USED]
	ldr	x1,[x0,F_CACHE]
	mov	x2,TC_HEADER / 16
.z:	stp	xzr,xzr,[x1],16
	subs	x2,x2,1
	b.ne	.z
	ret

; x0 = font, x1 = glyphe -> x0 = entree du cache (remplie), ou 0 avec
;   x1 = 1 si la grille de ttf_raster est valide mais non mise en cache,
;   x1 = 0 en cas d'echec
ttf__cache_get:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x2,[x19,F_PIXELS]
	and	x2,x2,0x7FFF
	orr	w21,w20,w2,lsl 16
	orr	w21,w21,0x80000000		; cle
	mov	w3,31
	mul	w3,w20,w3
	add	w3,w3,w2,lsl 3
	add	w3,w3,w2
	and	w3,w3,TC_SLOTS-1
	ldr	x22,[x19,F_CACHE]
	add	x22,x22,x3,lsl 4		; entree
	ldr	w4,[x22,TC_KEY]
	cmp	w4,w21
	b.eq	.hit
	mov	x0,x19				; absent : rasteriser
	mov	x1,x20
	bl	ttf_raster
	cbz	x0,.fail
	ldr	x23,[x19,F_GW]
	ldr	x24,[x19,F_ACCH]
	mul	x5,x23,x24			; octets du bitmap
	add	x5,x5,15
	and	x5,x5,not 15
	ldr	x6,[x19,F_CACHE_LEN]
	sub	x6,x6,TC_HEADER			; taille de la zone de donnees
	cmp	x5,x6
	b.hi	.too_big
	ldr	x7,[x19,F_CACHE_USED]
	add	x8,x7,x5
	cmp	x8,x6
	b.ls	.room
	mov	x0,x19				; zone pleine : on vide tout
	stp	x5,xzr,[sp,-16]!
	bl	ttf__cache_flush
	ldp	x5,xzr,[sp],16
	mov	x7,0
.room:	add	x8,x7,x5
	str	x8,[x19,F_CACHE_USED]
	str	w21,[x22,TC_KEY]
	strh	w23,[x22,TC_W]
	strh	w24,[x22,TC_H]
	ldr	x0,[x19,F_GX0]
	strh	w0,[x22,TC_BX]
	ldr	x0,[x19,F_GY0]
	strh	w0,[x22,TC_BY]
	str	w7,[x22,TC_OFF]
	ldr	x9,[x19,F_CACHE]		; grille -> octets de couverture
	add	x9,x9,TC_HEADER
	add	x9,x9,x7
	ldr	x2,[x19,F_ACC]
	ldr	x3,[x19,F_ACCW]
	fmov	s6,1.0
	mov	w7,255
	scvtf	s7,w7
.row:	cbz	x24,.hit
	fmov	s0,wzr
	mov	x10,0
.col:	cmp	x10,x23
	b.hs	.row_end
	ldr	s1,[x2,x10,lsl 2]
	fadd	s0,s0,s1
	fabs	s1,s0
	fmin	s1,s1,s6
	fmul	s1,s1,s7
	fcvtns	w7,s1
	strb	w7,[x9],1
	add	x10,x10,1
	b	.col
.row_end:
	add	x2,x2,x3,lsl 2
	sub	x24,x24,1
	b	.row
.hit:	mov	x0,x22
	b	.out
.too_big:
	mov	x0,0
	mov	x1,1
	b	.out
.fail:	mov	x0,0
	mov	x1,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	ret

; Melange d'un bitmap du cache : x0 = font, x1 = entree, x2 = surface,
; x3 = pas, x4 = x du stylo, x5 = y de base, w6 = couleur,
; x7 = clip (largeur | hauteur << 32)
ttf__blend8:
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x8,[x0,F_CACHE]
	add	x8,x8,TC_HEADER
	ldr	w9,[x1,TC_OFF]
	add	x8,x8,x9			; bitmap
	ldrh	w9,[x1,TC_W]
	ldrh	w10,[x1,TC_H]
	ldrsh	x11,[x1,TC_BX]
	add	x4,x4,x11			; coin haut-gauche
	ldrsh	x11,[x1,TC_BY]
	add	x5,x5,x11
	mov	w11,w7				; largeur de clip
	lsr	x12,x7,32			; hauteur de clip
	and	w13,w6,0x00FF00FF
	and	w14,w6,0x0000FF00
	orr	w22,w13,w14
.row:	cbz	w10,.ret
	cmp	x5,0
	b.lt	.row_next
	cmp	x5,x12
	b.ge	.ret
	madd	x15,x5,x3,x2
	mov	x16,0
.col:	cmp	x16,x9
	b.hs	.row_next
	ldrb	w17,[x8,x16]
	add	x7,x4,x16
	add	x16,x16,1
	cbz	w17,.col
	tbnz	x7,63,.col
	cmp	x7,x11
	b.ge	.col
	add	x21,x15,x7,lsl 2
	cmp	w17,255
	b.lo	.mix
	str	w22,[x21]
	b	.col
.mix:	add	w17,w17,w17,lsr 7		; 0..255 -> 0..256
	ldr	w0,[x21]
	mov	w20,256
	sub	w20,w20,w17
	and	w19,w0,0x00FF00FF
	mul	w19,w19,w20
	madd	w19,w13,w17,w19
	lsr	w19,w19,8
	and	w19,w19,0x00FF00FF
	and	w0,w0,0x0000FF00
	mul	w0,w0,w20
	madd	w0,w14,w17,w0
	lsr	w0,w0,8
	and	w0,w0,0x0000FF00
	orr	w0,w0,w19
	str	w0,[x21]
	b	.col
.row_next:
	add	x8,x8,x9
	add	x5,x5,1
	sub	w10,w10,1
	b	.row
.ret:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ret
