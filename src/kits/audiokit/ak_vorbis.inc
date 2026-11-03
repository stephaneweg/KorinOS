; ============================================================================
;  kits/audiokit/ak_vorbis.inc - Vorbis I : decodage (specification Xiph)
;
;  En-tetes : identification, commentaires (etiquettes), configuration
;  (livres de codes : longueurs ordonnees ou non, creuses, codes construits
;  dans l'ordre de la specification, recherche rapide sur 10 bits puis
;  dichotomie ; quantification vectorielle 1 et 2 precalculee ; plancher 1 ;
;  residus 0, 1, 2 ; correspondances avec couplage ; modes).
;  Paquet : plancher 1 (courbe, table de dB inverse), residu (8 passes,
;  classifications), couplage inverse, IMDCT (ak_mdct.inc), fenetres de
;  Vorbis (pentes courtes/longues), recouvrement du centre de la fenetre
;  precedente au centre de la courante. Le plancher 0 (abandonne par les
;  encodeurs depuis 2000) n'est pas pris en charge.
;  Lecture des bits poids faible d'abord : en-tetes par un lecteur en
;  memoire (LR_*), paquets par le lecteur en registres x9-x12.
; ============================================================================

; --- lecteur en memoire (en-tetes) ----------------------------------------------
LR_PTR		= 0
LR_END		= 8
LR_CACHE	= 16
LR_N		= 24
LR_EOP		= 32
LR_SIZE		= 48

; --- livre de codes (VCB_*) ------------------------------------------------------
VCB_DIM		= 0
VCB_ENTRIES	= 8
VCB_FASTBITS	= 16
VCB_FAST	= 24		; 2^bits mots : entree << 8 | longueur, -1 : code plus long
VCB_SORTED	= 32		; codes cadres a gauche (32 bits), tries
VCB_SORTVAL	= 40		; entree de chacun (32 bits)
VCB_SORTLEN	= 48		; longueur de chacun (octets)
VCB_NSORTED	= 56
VCB_VQ		= 64		; vecteurs precalcules (flottants, entries x dim), 0 : aucun
VCB_LENS	= 72
VCB_SIZE	= 128

; --- plancher 1 (VFL_*) -------------------------------------------------------------
VFL_PARTS	= 0
VFL_MULT	= 8
VFL_RANGEBITS	= 16
VFL_VALUES	= 24
VFL_RANGE	= 32
VFL_PCLASS	= 64		; 32 octets
VFL_CDIM	= 96		; 16
VFL_CSUB	= 112		; 16
VFL_CMASTER	= 128		; 16
VFL_SUBBOOK	= 144		; 16 x 8 x 16 bits = 256
VFL_X		= 400		; 65 x 16 bits
VFL_SORT	= 532		; 65 octets : ordre croissant de X
VFL_LOW		= 600		; 65 octets
VFL_HIGH	= 668		; 65 octets
VFL_SIZE	= 768

; --- residu (VRS_*) ----------------------------------------------------------------
VRS_TYPE	= 0
VRS_BEGIN	= 8
VRS_END		= 16
VRS_PSIZE	= 24
VRS_NCLASS	= 32
VRS_CLASSBOOK	= 40
VRS_BOOKS	= 64		; 64 x 8 x 16 bits = 1024
VRS_SIZE	= 1088

; --- correspondance (VBM_*) ---------------------------------------------------------
VBM_SUBMAPS	= 0
VBM_STEPS	= 8
VBM_MAG		= 16		; 256 octets
VBM_ANG		= 272		; 256
VBM_MUX		= 528		; 256
VBM_FLOOR	= 784		; 16
VBM_RES		= 800		; 16
VBM_SIZE	= 832

; --- etat (VB_*) ---------------------------------------------------------------------
VB_CH		= 0
VB_RATE		= 8
VB_BS0		= 16
VB_BS1		= 24
VB_NCB		= 32
VB_CB		= 40
VB_NFLOOR	= 48
VB_FLOOR	= 56
VB_NRES		= 64
VB_RES		= 72
VB_NMAP		= 80
VB_MAP		= 88
VB_NMODE	= 96
VB_MODEBITS	= 104
VB_HDR		= 112		; en-tetes lus (bits 0, 1, 2)
VB_PREVN	= 120		; taille du bloc precedent (0 : aucun)
VB_PREV		= 128		; moities droites precedentes (canaux x bs1/2)
VB_RESID	= 136		; spectres (canaux x bs1/2)
VB_TMP		= 144		; sortie d'IMDCT (bs1)
VB_CLASSIF	= 152		; classifications (8 x 8192 octets)
VB_SLOPE0	= 160		; pentes de fenetre (bs0/2, bs1/2)
VB_SLOPE1	= 168
VB_Y		= 176		; valeurs du plancher (canaux x 65 x 32 bits)
VB_USED		= 184		; plancher utilise (canaux, octets)
VB_DONOT	= 192		; residu a ne pas decoder (canaux, octets)
VB_FLAGS	= 200		; bloc courant : 0 court, bit 0 long, bit 1 fenetre precedente
				;   longue, bit 2 suivante longue
VB_N		= 208		; taille du bloc courant
VB_MODES	= 256		; 64 modes x 16 o : drapeau de bloc, correspondance
VB_MD0		= 1280		; IMDCT de bs0 (MD_SIZE)
VB_MD1		= VB_MD0 + MD_SIZE
VB_SIZE		= VB_MD1 + MD_SIZE

; x0 = decodeur -> 0 / < 0 : etat Vorbis dans OG_STATE (ou AKD_CODEC)
vb_new:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x1,VB_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	ldr	x1,[x19,AKD_CODEC]
	cbz	x1,.pkt
	str	x0,[x1,OG_STATE]
	mov	x0,0
	b	.out
.pkt:	str	x0,[x19,AKD_CODEC]		; (API par paquets : pas d'Ogg)
	mov	x0,0
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur -> x0 = etat Vorbis
vb_state:
	ldr	x1,[x0,AKD_MODE]
	ldr	x0,[x0,AKD_CODEC]
	cmp	x1,2
	b.eq	.ret
	ldr	x0,[x0,OG_STATE]
.ret:	ret

; x0 = decodeur : oublie le bloc precedent (recherche)
vb_reset:
	prologue
	bl	vb_state
	str	xzr,[x0,VB_PREVN]
	epilogue

; ---------------------------------------------------------------------------
;  Lecteurs de bits, poids faible d'abord
; ---------------------------------------------------------------------------

; x0 = lecteur, x1 = donnees, x2 = octets
lrm_init:
	str	x1,[x0,LR_PTR]
	add	x2,x1,x2
	str	x2,[x0,LR_END]
	str	xzr,[x0,LR_CACHE]
	str	xzr,[x0,LR_N]
	str	xzr,[x0,LR_EOP]
	ret

; x0 = lecteur, x1 = bits (0..32) -> x0 ; au-dela de la fin : 0 et LR_EOP
; ne touche que x0-x7
lrm_get:
	ldr	x2,[x0,LR_CACHE]
	ldr	x3,[x0,LR_N]
	ldr	x4,[x0,LR_PTR]
	ldr	x5,[x0,LR_END]
.f:	cmp	x3,x1
	b.hs	.ok
	cmp	x4,x5
	b.hs	.eop
	ldrb	w6,[x4],1
	lsl	x6,x6,x3
	orr	x2,x2,x6
	add	x3,x3,8
	b	.f
.eop:	mov	x6,1
	str	x6,[x0,LR_EOP]
	add	x3,x3,64			; (des zeros)
.ok:	cbz	x1,.z
	mov	x6,-1
	lsl	x6,x6,x1
	bic	x7,x2,x6
	lsr	x2,x2,x1
	sub	x3,x3,x1
	str	x2,[x0,LR_CACHE]
	str	x3,[x0,LR_N]
	str	x4,[x0,LR_PTR]
	mov	x0,x7
	ret
.z:	mov	x0,0
	ret

; lecteur en registres (x9 cache, x10 bits, x11 octet suivant, x12 fin) :
; remplit le cache (au moins 57 bits) ; ne touche que x16, x17
lr_fill:
	cmp	x10,56
	b.hi	.r
	add	x16,x11,8
	cmp	x16,x12
	b.hi	.b
	ldr	x16,[x11]
	lsl	x16,x16,x10
	orr	x9,x9,x16
	mov	x17,64
	sub	x17,x17,x10
	lsr	x17,x17,3
	add	x11,x11,x17
	add	x10,x10,x17,lsl 3
.r:	ret
.b:	cmp	x10,56
	b.hi	.r
	mov	x16,0
	cmp	x11,x12
	b.hs	.z
	ldrb	w16,[x11]
.z:	lsl	x16,x16,x10
	orr	x9,x9,x16
	add	x11,x11,1
	add	x10,x10,8
	b	.b

; x1 = bits (1..32) -> x0 ; ne touche que x0, x1, x8, x16, x17
lr_get:
	cmp	x10,x1
	b.hs	.ok
	mov	x8,x30
	bl	lr_fill
	mov	x30,x8
.ok:	mov	x0,-1
	lsl	x0,x0,x1
	bic	x0,x9,x0
	lsr	x9,x9,x1
	sub	x10,x10,x1
	ret

; fin du paquet depassee ? (lecteur en registres) -> saut a lbl
macro lr_eop lbl {
	sub	x16,x11,x12
	lsl	x16,x16,3
	sub	x16,x16,x10
	cmp	x16,0
	b.gt	lbl
}

; x0 = livre -> x0 = entree (-1 : code invalide) ; lecteur en registres
; ne touche que x0-x8, x16, x17
vcb_decode:
	cmp	x10,32
	b.hs	.f
	mov	x8,x30
	bl	lr_fill
	mov	x30,x8
.f:	ldr	w1,[x0,VCB_FASTBITS]
	mov	x2,-1
	lsl	x2,x2,x1
	bic	x2,x9,x2
	ldr	x3,[x0,VCB_FAST]
	ldr	w4,[x3,x2,lsl 2]
	cmn	w4,1
	b.eq	.long
	and	w5,w4,0xFF
	lsr	x9,x9,x5
	sub	x10,x10,x5
	lsr	w0,w4,8
	ret
.long:	rbit	w5,w9				; 32 bits, le premier en tete
	ldr	x3,[x0,VCB_SORTED]
	ldr	x6,[x0,VCB_NSORTED]
	cbz	x6,.bad
	mov	x1,0
	mov	x2,x6
.bs:	sub	x7,x2,x1
	cmp	x7,1
	b.ls	.bd
	add	x7,x1,x2
	lsr	x7,x7,1
	ldr	w4,[x3,x7,lsl 2]
	cmp	w4,w5
	csel	x1,x7,x1,ls
	csel	x2,x2,x7,ls
	b	.bs
.bd:	ldr	x3,[x0,VCB_SORTLEN]
	ldrb	w5,[x3,x1]
	lsr	x9,x9,x5
	sub	x10,x10,x5
	ldr	x3,[x0,VCB_SORTVAL]
	ldr	w0,[x3,x1,lsl 2]
	ret
.bad:	mov	x0,-1
	ret

; ---------------------------------------------------------------------------
;  En-tetes
; ---------------------------------------------------------------------------

; x0 = decodeur, x1 = paquet, x2 = octets -> 0 / < 0
vb_header:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,LR_SIZE
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	bl	vb_state
	mov	x20,x0
	cmp	x22,7
	b.lo	.bad
	ldrb	w23,[x21]			; type
	ldr	w0,[x21,1]			; "vorbis"
	movl	w1,0x62726F76
	cmp	w0,w1
	b.ne	.bad
	ldrh	w0,[x21,5]
	movl	w1,0x7369
	cmp	w0,w1
	b.ne	.bad
	mov	x0,sp
	add	x1,x21,7
	sub	x2,x22,7
	bl	lrm_init
	cmp	w23,1
	b.eq	.ident
	cmp	w23,3
	b.eq	.comment
	cmp	w23,5
	b.eq	.setup
	b	.bad
.ident:	mov	x0,sp
	mov	x1,32
	bl	lrm_get				; version
	cbnz	x0,.bad
	mov	x0,sp
	mov	x1,8
	bl	lrm_get
	cbz	x0,.bad
	cmp	x0,8				; (8 canaux au plus : AudioKit)
	b.hi	.unsup
	str	x0,[x20,VB_CH]
	str	x0,[x19,AKD_CH]
	mov	x0,sp
	mov	x1,32
	bl	lrm_get
	cbz	x0,.bad
	str	x0,[x20,VB_RATE]
	str	x0,[x19,AKD_RATE]
	mov	x0,sp
	mov	x1,32
	bl	lrm_get				; debit maximal
	mov	x0,sp
	mov	x1,32
	bl	lrm_get				; nominal
	str	x0,[x19,AKD_BITRATE]
	mov	x0,sp
	mov	x1,32
	bl	lrm_get				; minimal
	mov	x0,sp
	mov	x1,4
	bl	lrm_get
	mov	x23,x0
	mov	x0,sp
	mov	x1,4
	bl	lrm_get
	mov	x24,x0
	cmp	x23,6				; 64 <= bs0 <= bs1 <= 8192
	b.lo	.bad
	cmp	x24,13
	b.hi	.bad
	cmp	x23,x24
	b.hi	.bad
	mov	x0,1
	lsl	x1,x0,x23
	str	x1,[x20,VB_BS0]
	lsl	x1,x0,x24
	str	x1,[x20,VB_BS1]
	mov	x0,AKF_VORBIS
	str	x0,[x19,AKD_FMT]
	mov	x0,AKD_F32
	str	x0,[x19,AKD_PCMFMT]
	ldr	x0,[x19,AKD_MODE]		; (Ogg : avance de decodage)
	cmp	x0,2
	b.eq	.idok
	ldr	x0,[x19,AKD_CODEC]
	ldr	x1,[x20,VB_BS1]
	str	x1,[x0,OG_PREROLL]
.idok:	ldr	x0,[x20,VB_HDR]
	orr	x0,x0,1
	str	x0,[x20,VB_HDR]
	b	.ok
.comment:
	mov	x0,x19
	add	x1,x21,7
	sub	x2,x22,7
	bl	akd_vcomment
	ldr	x0,[x20,VB_HDR]
	orr	x0,x0,2
	str	x0,[x20,VB_HDR]
	b	.ok
.setup:	ldr	x0,[x20,VB_HDR]
	tbz	x0,0,.bad
	mov	x0,x19
	mov	x1,sp
	bl	vb_setup
	tbnz	x0,63,.out
	ldr	x0,[x20,VB_HDR]
	orr	x0,x0,4
	str	x0,[x20,VB_HDR]
	b	.ok
.unsup:	mov	x0,AKE_FORMAT
	b	.out
.bad:	mov	x0,AKE_CORRUPT
	b	.out
.ok:	mov	x0,0
.out:	add	sp,sp,LR_SIZE
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nombre -> x0 = ilog (bits necessaires ; ilog(0) = 0)
vb_ilog:
	clz	x0,x0
	mov	x1,64
	sub	x0,x1,x0
	ret

; x0 = decodeur, x1 = lecteur (LR_*) sur la configuration -> 0 / < 0
; x20 = etat (garde par l'appelant vb_header)
vb_setup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	; livres de codes
	mov	x0,x21
	mov	x1,8
	bl	lrm_get
	add	x22,x0,1
	str	x22,[x20,VB_NCB]
	mov	x0,x19
	mov	x1,VCB_SIZE
	mul	x1,x1,x22
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VB_CB]
	mov	x23,0
.cb:	cmp	x23,x22
	b.hs	.cbd
	ldr	x2,[x20,VB_CB]
	mov	x0,VCB_SIZE
	madd	x2,x23,x0,x2
	mov	x0,x19
	mov	x1,x21
	bl	vcb_setup
	tbnz	x0,63,.out
	add	x23,x23,1
	b	.cb
.cbd:	; transformees temporelles (inutilisees)
	mov	x0,x21
	mov	x1,6
	bl	lrm_get
	add	x22,x0,1
.tt:	cbz	x22,.ttd
	mov	x0,x21
	mov	x1,16
	bl	lrm_get
	cbnz	x0,.bad
	sub	x22,x22,1
	b	.tt
.ttd:	; planchers
	mov	x0,x21
	mov	x1,6
	bl	lrm_get
	add	x22,x0,1
	str	x22,[x20,VB_NFLOOR]
	mov	x0,x19
	mov	x1,VFL_SIZE
	mul	x1,x1,x22
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VB_FLOOR]
	mov	x23,0
.fl:	cmp	x23,x22
	b.hs	.fld
	ldr	x2,[x20,VB_FLOOR]
	mov	x0,VFL_SIZE
	madd	x2,x23,x0,x2
	mov	x0,x21
	mov	x1,x2
	bl	vfl_setup
	tbnz	x0,63,.out
	add	x23,x23,1
	b	.fl
.fld:	; residus
	mov	x0,x21
	mov	x1,6
	bl	lrm_get
	add	x22,x0,1
	str	x22,[x20,VB_NRES]
	mov	x0,x19
	mov	x1,VRS_SIZE
	mul	x1,x1,x22
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VB_RES]
	mov	x23,0
.rs:	cmp	x23,x22
	b.hs	.rsd
	ldr	x2,[x20,VB_RES]
	mov	x0,VRS_SIZE
	madd	x2,x23,x0,x2
	mov	x0,x21
	mov	x1,x2
	bl	vrs_setup
	tbnz	x0,63,.out
	add	x23,x23,1
	b	.rs
.rsd:	; correspondances
	mov	x0,x21
	mov	x1,6
	bl	lrm_get
	add	x22,x0,1
	str	x22,[x20,VB_NMAP]
	mov	x0,x19
	mov	x1,VBM_SIZE
	mul	x1,x1,x22
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VB_MAP]
	mov	x23,0
.mp:	cmp	x23,x22
	b.hs	.mpd
	ldr	x2,[x20,VB_MAP]
	mov	x0,VBM_SIZE
	madd	x2,x23,x0,x2
	mov	x0,x21
	mov	x1,x2
	bl	vbm_setup
	tbnz	x0,63,.out
	add	x23,x23,1
	b	.mp
.mpd:	; modes
	mov	x0,x21
	mov	x1,6
	bl	lrm_get
	add	x22,x0,1
	str	x22,[x20,VB_NMODE]
	sub	x0,x22,1
	bl	vb_ilog
	str	x0,[x20,VB_MODEBITS]
	mov	x23,0
.md:	cmp	x23,x22
	b.hs	.mdd
	add	x24,x20,VB_MODES
	add	x24,x24,x23,lsl 4
	mov	x0,x21
	mov	x1,1
	bl	lrm_get
	str	x0,[x24]
	mov	x0,x21
	mov	x1,16
	bl	lrm_get
	cbnz	x0,.bad
	mov	x0,x21
	mov	x1,16
	bl	lrm_get
	cbnz	x0,.bad
	mov	x0,x21
	mov	x1,8
	bl	lrm_get
	ldr	x1,[x20,VB_NMAP]
	cmp	x0,x1
	b.hs	.bad
	str	x0,[x24,8]
	add	x23,x23,1
	b	.md
.mdd:	mov	x0,x21
	mov	x1,1
	bl	lrm_get				; bit de trame
	cbz	x0,.bad
	ldr	x0,[x21,LR_EOP]
	cbnz	x0,.bad
	; tampons, IMDCT, pentes de fenetre
	mov	x0,x19
	bl	vb_buffers
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = lecteur, x2 = livre a remplir -> 0 / < 0
vcb_setup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,144			; marqueurs (33 x 32 bits) ; [128] : lookup
	mov	x19,x0
	mov	x21,x1
	mov	x20,x2
	mov	x0,x21
	mov	x1,24
	bl	lrm_get
	movl	x1,0x564342
	cmp	x0,x1
	b.ne	.bad
	mov	x0,x21
	mov	x1,16
	bl	lrm_get
	str	x0,[x20,VCB_DIM]
	mov	x0,x21
	mov	x1,24
	bl	lrm_get
	str	x0,[x20,VCB_ENTRIES]
	mov	x22,x0				; entrees
	cbz	x22,.bad
	mov	x0,x19
	add	x1,x22,16
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VCB_LENS]
	mov	x23,x0				; longueurs
	mov	x0,x21
	mov	x1,1
	bl	lrm_get				; ordonne ?
	cbnz	x0,.ord
	mov	x0,x21
	mov	x1,1
	bl	lrm_get
	mov	x24,x0				; creux ?
	mov	x25,0
.ul:	cmp	x25,x22
	b.hs	.lens
	cbz	x24,.full
	mov	x0,x21
	mov	x1,1
	bl	lrm_get
	cbz	x0,.unused
.full:	mov	x0,x21
	mov	x1,5
	bl	lrm_get
	add	x0,x0,1
	strb	w0,[x23,x25]
	b	.un
.unused:
	strb	wzr,[x23,x25]
.un:	add	x25,x25,1
	b	.ul
.ord:	mov	x0,x21
	mov	x1,5
	bl	lrm_get
	add	x24,x0,1			; longueur courante
	mov	x25,0				; entree courante
.ol:	cmp	x25,x22
	b.hs	.lens
	sub	x0,x22,x25
	bl	vb_ilog
	mov	x1,x0
	mov	x0,x21
	bl	lrm_get				; nombre
	add	x1,x25,x0
	cmp	x1,x22
	b.hi	.bad
.of:	cmp	x25,x1
	b.hs	.on
	strb	w24,[x23,x25]
	add	x25,x25,1
	b	.of
.on:	add	x24,x24,1
	cmp	x24,32
	b.hi	.bad
	b	.ol
.lens:	; quantification vectorielle
	mov	x0,x21
	mov	x1,4
	bl	lrm_get
	str	w0,[sp,128]
	cbz	x0,.codes
	cmp	x0,2
	b.hi	.bad
	mov	x0,x21
	mov	x1,32
	bl	lrm_get
	bl	vb_float32
	str	s0,[sp,132]			; minimum
	mov	x0,x21
	mov	x1,32
	bl	lrm_get
	bl	vb_float32
	str	s0,[sp,136]			; delta
	mov	x0,x21
	mov	x1,4
	bl	lrm_get
	add	x26,x0,1			; bits des valeurs
	mov	x0,x21
	mov	x1,1
	bl	lrm_get
	str	w0,[sp,140]			; suite (sequence_p)
	ldr	w0,[sp,128]
	ldr	x1,[x20,VCB_DIM]
	cmp	x0,1
	b.ne	.t2
	mov	x0,x22
	bl	vb_lookup1
	b	.tv
.t2:	mul	x0,x22,x1
.tv:	mov	x27,x0				; valeurs de la table
	movl	x1,16777216
	cmp	x27,x1
	b.hi	.bad
	mov	x0,x19
	lsl	x1,x27,1
	add	x1,x1,16
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x28,x0				; multiplicateurs (16 bits)
	mov	x25,0
.mu:	cmp	x25,x27
	b.hs	.vq
	mov	x0,x21
	mov	x1,x26
	bl	lrm_get
	strh	w0,[x28,x25,lsl 1]
	add	x25,x25,1
	b	.mu
.vq:	; vecteurs de chaque entree : entrees x dim flottants
	ldr	x1,[x20,VCB_DIM]
	mul	x1,x1,x22
	movl	x0,4194304
	cmp	x1,x0
	b.hi	.bad
	mov	x0,x19
	lsl	x1,x1,2
	add	x1,x1,16
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VCB_VQ]
	mov	x6,x0				; sortie
	ldr	s4,[sp,132]			; minimum
	ldr	s5,[sp,136]			; delta
	ldr	w7,[sp,140]			; suite
	ldr	x8,[x20,VCB_DIM]
	ldr	w13,[sp,128]			; type
	mov	x25,0				; entree
.ve:	cmp	x25,x22
	b.hs	.codes
	fmov	s6,wzr				; last
	mov	x14,1				; diviseur
	mov	x15,0				; i
.vd:	cmp	x15,x8
	b.hs	.ven
	cmp	x13,1
	b.ne	.vt2
	udiv	x0,x25,x14
	udiv	x1,x0,x27
	msub	x0,x1,x27,x0			; (e / div) % valeurs
	mul	x14,x14,x27
	b	.vm
.vt2:	madd	x0,x25,x8,x15			; e * dim + i
.vm:	ldrh	w0,[x28,x0,lsl 1]
	ucvtf	s0,w0
	fmadd	s0,s0,s5,s4
	fadd	s0,s0,s6
	cbz	w7,.vs
	fmov	s6,s0
.vs:	str	s0,[x6],4
	add	x15,x15,1
	b	.vd
.ven:	add	x25,x25,1
	b	.ve
.codes:	; codes (ordre de la specification), puis tables de recherche
	mov	x0,x19
	mov	x1,x20
	mov	x2,sp
	bl	vcb_codes
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	add	sp,sp,144
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entier de 32 bits -> s0 (float32_unpack de la specification)
vb_float32:
	and	x1,x0,0x1FFFFF			; mantisse
	ubfx	x2,x0,21,10			; exposant
	ucvtf	d0,x1
	tbz	w0,31,.p
	fneg	d0,d0
.p:	sub	x2,x2,788			; 2^(e - 788) en double
	add	x2,x2,1023
	cmp	x2,1
	b.lt	.z
	lsl	x2,x2,52
	fmov	d1,x2
	fmul	d0,d0,d1
	fcvt	s0,d0
	ret
.z:	fmov	s0,wzr
	ret

; x0 = entrees, x1 = dimensions -> x0 = plus grand r tel que r^dim <= entrees
vb_lookup1:
	mov	x2,0				; bas (r^dim <= e)
	add	x3,x0,1				; haut (exclu)
.b:	sub	x4,x3,x2
	cmp	x4,1
	b.ls	.d
	add	x4,x2,x3
	lsr	x4,x4,1				; r
	mov	x5,1				; r^dim, plafonne
	mov	x6,x1
.p:	cbz	x6,.c
	mul	x5,x5,x4
	cmp	x5,x0
	b.hi	.c
	sub	x6,x6,1
	b	.p
.c:	cmp	x5,x0
	csel	x2,x4,x2,ls
	csel	x3,x3,x4,ls
	b	.b
.d:	mov	x0,x2
	ret

; x0 = decodeur, x1 = livre, x2 = 33 marqueurs (32 bits, a zero) -> 0 / < 0
; codes de la specification (le plus a gauche disponible), table rapide,
; liste triee des codes cadres a gauche
vcb_codes:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,x21
	mov	x1,136
	bl	ak_memzero
	ldr	x22,[x20,VCB_ENTRIES]
	ldr	x23,[x20,VCB_LENS]
	; entrees utilisees et plus grande longueur
	mov	x24,0				; utilisees
	mov	x25,0				; longueur max
	mov	x0,0
.cnt:	cmp	x0,x22
	b.hs	.cntd
	ldrb	w1,[x23,x0]
	cbz	w1,.cn
	add	x24,x24,1
	cmp	x1,x25
	csel	x25,x1,x25,hi
.cn:	add	x0,x0,1
	b	.cnt
.cntd:	str	x24,[x20,VCB_NSORTED]
	; tableau des codes (64 bits : code cadre a gauche << 32 | entree << 8 | longueur)
	mov	x0,x19
	lsl	x1,x24,3
	add	x1,x1,16
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x26,x0
	mov	x27,0				; entree
	mov	x28,0				; rang dans le tableau
.cw:	cmp	x27,x22
	b.hs	.cwd
	ldrb	w2,[x23,x27]			; longueur
	cbz	w2,.cwn
	ldr	w3,[x21,x2,lsl 2]		; code = marqueur[longueur]
	cmp	w2,32
	b.hs	.ok32
	lsr	w4,w3,w2
	cbnz	w4,.bad				; (trop de codes de cette longueur)
.ok32:	mov	w9,w3				; (code de l'entree)
	; mise a jour des marqueurs
	mov	x4,x2
.m1:	cbz	x4,.m1d
	ldr	w5,[x21,x4,lsl 2]
	tbz	w5,0,.m1i
	cmp	x4,1
	b.ne	.m1s
	add	w5,w5,1
	str	w5,[x21,4]
	b	.m1d
.m1s:	sub	x6,x4,1
	ldr	w5,[x21,x6,lsl 2]
	lsl	w5,w5,1
	str	w5,[x21,x4,lsl 2]
	b	.m1d
.m1i:	add	w5,w5,1
	str	w5,[x21,x4,lsl 2]
	sub	x4,x4,1
	b	.m1
.m1d:	add	x4,x2,1
.m2:	cmp	x4,33
	b.hs	.m2d
	ldr	w5,[x21,x4,lsl 2]
	lsr	w6,w5,1
	cmp	w6,w3
	b.ne	.m2d
	mov	w3,w5
	sub	x6,x4,1
	ldr	w5,[x21,x6,lsl 2]
	lsl	w5,w5,1
	str	w5,[x21,x4,lsl 2]
	add	x4,x4,1
	b	.m2
.m2d:	; code cadre a gauche
	mov	w4,32
	sub	w4,w4,w2
	lsl	w5,w9,w4			; (w2 = 32 : decalage 0)
	lsl	x5,x5,32
	orr	x5,x5,x27,lsl 8
	orr	x5,x5,x2
	str	x5,[x26,x28,lsl 3]
	add	x28,x28,1
.cwn:	add	x27,x27,1
	b	.cw
.cwd:	; tri (Shell, ecarts de Knuth) des 64 bits
	mov	x0,x26
	mov	x1,x24
	bl	vb_sort64
	; tableaux tries : code, entree, longueur
	mov	x0,x19
	lsl	x1,x24,3
	add	x1,x1,x24
	add	x1,x1,16
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VCB_SORTED]
	add	x1,x0,x24,lsl 2
	str	x1,[x20,VCB_SORTVAL]
	add	x1,x1,x24,lsl 2
	str	x1,[x20,VCB_SORTLEN]
	mov	x0,0
.sp:	cmp	x0,x24
	b.hs	.spd
	ldr	x1,[x26,x0,lsl 3]
	lsr	x2,x1,32
	ldr	x3,[x20,VCB_SORTED]
	str	w2,[x3,x0,lsl 2]
	ubfx	x2,x1,8,24
	ldr	x3,[x20,VCB_SORTVAL]
	str	w2,[x3,x0,lsl 2]
	and	x2,x1,0xFF
	ldr	x3,[x20,VCB_SORTLEN]
	strb	w2,[x3,x0]
	add	x0,x0,1
	b	.sp
.spd:	; table rapide : 10 bits au plus
	mov	x0,10
	cmp	x25,x0
	csel	x25,x25,x0,lo
	cmp	x25,1
	csinc	x25,x25,xzr,hs			; (au moins 1)
	str	x25,[x20,VCB_FASTBITS]
	mov	x0,x19
	mov	x1,4
	lsl	x1,x1,x25
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VCB_FAST]
	mov	x1,1
	lsl	x1,x1,x25
	mov	w2,-1
.fi:	subs	x1,x1,1
	str	w2,[x0,x1,lsl 2]
	b.ne	.fi
	mov	x27,0
.fc:	cmp	x27,x24
	b.hs	.ok
	ldr	x1,[x26,x27,lsl 3]
	and	x2,x1,0xFF			; longueur
	cmp	x2,x25
	b.hi	.fn
	lsr	x3,x1,32			; code cadre a gauche
	rbit	w3,w3				; premier bit du flux en bit 0
	lsr	x4,x1,8				; entree << 8 | longueur
	lsl	x4,x4,8
	orr	x4,x4,x2
	mov	w4,w4
	mov	x5,0				; suffixes
	mov	x6,1
	sub	x7,x25,x2
	lsl	x6,x6,x7			; 2^(bits - longueur)
	ldr	x8,[x20,VCB_FAST]
.fs:	cmp	x5,x6
	b.hs	.fn
	lsl	x7,x5,x2
	orr	x7,x7,x3
	str	w4,[x8,x7,lsl 2]
	add	x5,x5,1
	b	.fs
.fn:	add	x27,x27,1
	b	.fc
.ok:	mov	x0,0
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tableau de 64 bits, x1 = nombre : tri croissant (Shell)
vb_sort64:
	mov	x2,1				; ecart : 1, 4, 13, 40...
.g:	mov	x3,3
	madd	x3,x2,x3,x3			; ...
	sub	x3,x3,2				; 3 g + 1
	cmp	x3,x1
	b.hs	.loop
	mov	x2,x3
	b	.g
.loop:	cbz	x2,.ret
	mov	x3,x2				; i
.i:	cmp	x3,x1
	b.hs	.ng
	ldr	x4,[x0,x3,lsl 3]		; v
	mov	x5,x3				; j
.j:	cmp	x5,x2
	b.lo	.put
	sub	x6,x5,x2
	ldr	x7,[x0,x6,lsl 3]
	cmp	x7,x4
	b.ls	.put
	str	x7,[x0,x5,lsl 3]
	mov	x5,x6
	b	.j
.put:	str	x4,[x0,x5,lsl 3]
	add	x3,x3,1
	b	.i
.ng:	cmp	x2,1
	b.eq	.ret
	sub	x2,x2,1				; g = (g - 1) / 3
	mov	x6,3
	udiv	x2,x2,x6
	b	.loop
.ret:	ret

; x0 = lecteur, x1 = plancher -> 0 / < 0
vfl_setup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x19
	mov	x1,16
	bl	lrm_get
	cmp	x0,1
	b.ne	.unsup				; plancher 0 : non
	mov	x0,x19
	mov	x1,5
	bl	lrm_get
	str	x0,[x20,VFL_PARTS]
	mov	x21,x0
	mov	x22,-1				; classe max
	mov	x23,0
.pc:	cmp	x23,x21
	b.hs	.pcd
	mov	x0,x19
	mov	x1,4
	bl	lrm_get
	add	x1,x20,VFL_PCLASS
	strb	w0,[x1,x23]
	cmp	x0,x22
	csel	x22,x0,x22,gt
	add	x23,x23,1
	b	.pc
.pcd:	mov	x23,0				; classes
.cl:	cmp	x23,x22
	b.gt	.cld
	mov	x0,x19
	mov	x1,3
	bl	lrm_get
	add	x0,x0,1
	add	x1,x20,VFL_CDIM
	strb	w0,[x1,x23]
	mov	x0,x19
	mov	x1,2
	bl	lrm_get
	add	x1,x20,VFL_CSUB
	strb	w0,[x1,x23]
	mov	x24,x0
	cbz	x24,.nm
	mov	x0,x19
	mov	x1,8
	bl	lrm_get
	add	x1,x20,VFL_CMASTER
	strb	w0,[x1,x23]
.nm:	mov	x0,1
	lsl	x24,x0,x24			; sous-classes
	mov	x1,0
.sb:	cmp	x1,x24
	b.hs	.sbd
	str	x1,[sp,-16]!
	mov	x0,x19
	mov	x1,8
	bl	lrm_get
	ldr	x1,[sp],16
	sub	x0,x0,1				; (-1 : aucun livre)
	add	x2,x20,VFL_SUBBOOK
	add	x2,x2,x23,lsl 4
	strh	w0,[x2,x1,lsl 1]
	add	x1,x1,1
	b	.sb
.sbd:	add	x23,x23,1
	b	.cl
.cld:	mov	x0,x19
	mov	x1,2
	bl	lrm_get
	add	x0,x0,1
	str	x0,[x20,VFL_MULT]
	adr	x1,vfl_ranges
	sub	x0,x0,1
	ldr	w1,[x1,x0,lsl 2]
	str	x1,[x20,VFL_RANGE]
	mov	x0,x19
	mov	x1,4
	bl	lrm_get
	str	x0,[x20,VFL_RANGEBITS]
	mov	x24,x0
	add	x23,x20,VFL_X
	strh	wzr,[x23]
	mov	x0,1
	lsl	x0,x0,x24
	strh	w0,[x23,2]
	mov	x22,2				; valeurs
	mov	x21,0				; partition
.xp:	ldr	x0,[x20,VFL_PARTS]
	cmp	x21,x0
	b.hs	.xd
	add	x0,x20,VFL_PCLASS
	ldrb	w0,[x0,x21]
	add	x1,x20,VFL_CDIM
	ldrb	w1,[x1,x0]
.xj:	cbz	x1,.xn
	cmp	x22,65
	b.hs	.bad
	str	x1,[sp,-16]!
	mov	x0,x19
	mov	x1,x24
	bl	lrm_get
	ldr	x1,[sp],16
	strh	w0,[x23,x22,lsl 1]
	add	x22,x22,1
	sub	x1,x1,1
	b	.xj
.xn:	add	x21,x21,1
	b	.xp
.xd:	str	x22,[x20,VFL_VALUES]
	; ordre croissant de X (insertion) ; voisins bas et haut
	add	x0,x20,VFL_SORT
	mov	x1,0
.si:	cmp	x1,x22
	b.hs	.ss
	strb	w1,[x0,x1]
	add	x1,x1,1
	b	.si
.ss:	mov	x1,1
.so:	cmp	x1,x22
	b.hs	.nb
	ldrb	w2,[x0,x1]			; indice a placer
	ldrh	w3,[x23,x2,lsl 1]		; son X
	mov	x4,x1
.sj:	cbz	x4,.sput
	sub	x5,x4,1
	ldrb	w6,[x0,x5]
	ldrh	w7,[x23,x6,lsl 1]
	cmp	w7,w3
	b.ls	.sput
	strb	w6,[x0,x4]
	mov	x4,x5
	b	.sj
.sput:	strb	w2,[x0,x4]
	add	x1,x1,1
	b	.so
.nb:	mov	x1,2				; low[i] : plus grand X[j] < X[i], j < i
.nbi:	cmp	x1,x22				; high[i] : plus petit X[j] > X[i], j < i
	b.hs	.ok
	ldrh	w2,[x23,x1,lsl 1]
	mov	x3,0				; meilleurs
	mov	w4,-1
	mov	x5,0
	movl	w6,0x10000
	mov	x7,0
.nj:	cmp	x7,x1
	b.hs	.nd
	ldrh	w8,[x23,x7,lsl 1]
	cmp	w8,w2
	b.hs	.nh
	cmp	w8,w4
	b.le	.nn
	mov	w4,w8
	mov	x3,x7
	b	.nn
.nh:	b.eq	.nn
	cmp	w8,w6
	b.ge	.nn
	mov	w6,w8
	mov	x5,x7
.nn:	add	x7,x7,1
	b	.nj
.nd:	add	x7,x20,VFL_LOW
	strb	w3,[x7,x1]
	add	x7,x20,VFL_HIGH
	strb	w5,[x7,x1]
	add	x1,x1,1
	b	.nbi
.ok:	mov	x0,0
	b	.out
.unsup:	mov	x0,AKE_FORMAT
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

vfl_ranges	dw	256, 128, 86, 64

; x0 = lecteur, x1 = residu -> 0 / < 0
vrs_setup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x19
	mov	x1,16
	bl	lrm_get
	cmp	x0,2
	b.hi	.bad
	str	x0,[x20,VRS_TYPE]
	mov	x0,x19
	mov	x1,24
	bl	lrm_get
	str	x0,[x20,VRS_BEGIN]
	mov	x0,x19
	mov	x1,24
	bl	lrm_get
	str	x0,[x20,VRS_END]
	mov	x0,x19
	mov	x1,24
	bl	lrm_get
	add	x0,x0,1
	str	x0,[x20,VRS_PSIZE]
	mov	x0,x19
	mov	x1,6
	bl	lrm_get
	add	x0,x0,1
	str	x0,[x20,VRS_NCLASS]
	mov	x21,x0
	mov	x0,x19
	mov	x1,8
	bl	lrm_get
	str	x0,[x20,VRS_CLASSBOOK]
	; cascade : 8 bits par classe
	sub	sp,sp,64
	mov	x22,0
.cs:	cmp	x22,x21
	b.hs	.bk
	mov	x0,x19
	mov	x1,3
	bl	lrm_get
	mov	x23,x0
	mov	x0,x19
	mov	x1,1
	bl	lrm_get
	cbz	x0,.lo
	mov	x0,x19
	mov	x1,5
	bl	lrm_get
	orr	x23,x23,x0,lsl 3
.lo:	strb	w23,[sp,x22]
	add	x22,x22,1
	b	.cs
.bk:	mov	x22,0				; classe
.bc:	cmp	x22,x21
	b.hs	.ok
	mov	x23,0				; passe
.bp:	cmp	x23,8
	b.hs	.bcn
	ldrb	w0,[sp,x22]
	lsr	w0,w0,w23
	mov	x24,-1
	tbz	w0,0,.st
	mov	x0,x19
	mov	x1,8
	bl	lrm_get
	mov	x24,x0
.st:	add	x0,x20,VRS_BOOKS
	add	x0,x0,x22,lsl 4
	strh	w24,[x0,x23,lsl 1]
	add	x23,x23,1
	b	.bp
.bcn:	add	x22,x22,1
	b	.bc
.ok:	add	sp,sp,64
	mov	x0,0
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = lecteur, x1 = correspondance -> 0 / < 0 (x20 = etat, pour les canaux)
vbm_setup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x23,x20				; etat
	mov	x19,x0
	mov	x20,x1
	mov	x0,x19
	mov	x1,16
	bl	lrm_get
	cbnz	x0,.bad
	mov	x21,1
	mov	x0,x19
	mov	x1,1
	bl	lrm_get
	cbz	x0,.sm
	mov	x0,x19
	mov	x1,4
	bl	lrm_get
	add	x21,x0,1
.sm:	str	x21,[x20,VBM_SUBMAPS]
	ldr	x0,[x23,VB_CH]			; bits d'un numero de canal
	sub	x0,x0,1
	bl	vb_ilog
	mov	x24,x0
	str	xzr,[x20,VBM_STEPS]
	mov	x0,x19
	mov	x1,1
	bl	lrm_get
	cbz	x0,.nc
	mov	x0,x19
	mov	x1,8
	bl	lrm_get
	add	x22,x0,1
	str	x22,[x20,VBM_STEPS]
	mov	x21,0
.cp:	cmp	x21,x22
	b.hs	.ncs
	mov	x0,x19
	mov	x1,x24
	bl	lrm_get
	add	x1,x20,VBM_MAG
	strb	w0,[x1,x21]
	mov	x0,x19
	mov	x1,x24
	bl	lrm_get
	add	x1,x20,VBM_ANG
	strb	w0,[x1,x21]
	add	x21,x21,1
	b	.cp
.ncs:	ldr	x21,[x20,VBM_SUBMAPS]
.nc:	mov	x0,x19
	mov	x1,2
	bl	lrm_get
	cbnz	x0,.bad
	ldr	x21,[x20,VBM_SUBMAPS]
	cmp	x21,1
	b.ls	.mx0
	mov	x22,0
.mx:	ldr	x0,[x23,VB_CH]
	cmp	x22,x0
	b.hs	.sub
	mov	x0,x19
	mov	x1,4
	bl	lrm_get
	cmp	x0,x21
	b.hs	.bad
	add	x1,x20,VBM_MUX
	strb	w0,[x1,x22]
	add	x22,x22,1
	b	.mx
.mx0:	; un seul sous-plan : mux a zero (deja)
.sub:	mov	x22,0
.sl:	cmp	x22,x21
	b.hs	.ok
	mov	x0,x19
	mov	x1,8
	bl	lrm_get				; (temps : inutilise)
	mov	x0,x19
	mov	x1,8
	bl	lrm_get
	ldr	x1,[x23,VB_NFLOOR]
	cmp	x0,x1
	b.hs	.bad
	add	x1,x20,VBM_FLOOR
	strb	w0,[x1,x22]
	mov	x0,x19
	mov	x1,8
	bl	lrm_get
	ldr	x1,[x23,VB_NRES]
	cmp	x0,x1
	b.hs	.bad
	add	x1,x20,VBM_RES
	strb	w0,[x1,x22]
	add	x22,x22,1
	b	.sl
.ok:	mov	x0,0
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur (x20 = etat) : tampons, IMDCT, pentes -> 0 / < 0
vb_buffers:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	mov	x19,x0
	ldr	x21,[x20,VB_BS1]
	ldr	x22,[x20,VB_CH]
	; precedent, spectres : canaux x bs1/2 ; IMDCT : bs1 ; Y : canaux x 65 x 4
	mov	x0,x19
	mul	x1,x21,x22
	lsl	x1,x1,1				; (x 4 / 2)
	add	x1,x1,64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VB_PREV]
	mov	x0,x19
	mul	x1,x21,x22
	lsl	x1,x1,1
	add	x1,x1,x21,lsl 2			; (+ bs1 x 4 : residu de type 2 entrelace)
	add	x1,x1,64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VB_RESID]
	mov	x0,x19
	lsl	x1,x21,2
	add	x1,x1,64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VB_TMP]
	mov	x0,x19
	mov	x1,8*8192
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VB_CLASSIF]
	mov	x0,x19
	mov	x1,8*65*4 + 64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,VB_Y]
	add	x1,x0,8*65*4
	str	x1,[x20,VB_USED]
	add	x1,x1,16
	str	x1,[x20,VB_DONOT]
	; IMDCT
	mov	x0,x19
	add	x1,x20,VB_MD0
	ldr	x2,[x20,VB_BS0]
	bl	ak_imdct_init
	tbnz	x0,63,.out
	mov	x0,x19
	add	x1,x20,VB_MD1
	ldr	x2,[x20,VB_BS1]
	bl	ak_imdct_init
	tbnz	x0,63,.out
	; pentes : sin(pi/2 sin^2((j + 1/2) / n * pi/2)), j < n = bs/2
	ldr	x1,[x20,VB_BS0]
	lsr	x1,x1,1
	mov	x0,x19
	bl	vb_slope
	cbz	x0,.nomem
	str	x0,[x20,VB_SLOPE0]
	ldr	x1,[x20,VB_BS1]
	lsr	x1,x1,1
	mov	x0,x19
	bl	vb_slope
	cbz	x0,.nomem
	str	x0,[x20,VB_SLOPE1]
	; sortie : bs1 trames par canal
	mov	x0,x19
	ldr	x1,[x20,VB_BS1]
	ldr	x2,[x20,VB_CH]
	bl	akd_planes
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	d8,d9,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = n -> x0 = pente (n flottants), 0 : memoire
vb_slope:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	mov	x20,x1
	lsl	x1,x1,2
	bl	akd_alloc
	cbz	x0,.out
	mov	x21,x0
	scvtf	d8,x20
	mov	x22,0
.l:	cmp	x22,x20
	b.hs	.done
	scvtf	d0,x22
	fmov	d1,0.5
	fadd	d0,d0,d1
	fdiv	d0,d0,d8
	adr	x0,vb_halfpi
	ldr	d9,[x0]
	fmul	d0,d0,d9
	bl	ak_sincos			; sin^2
	fmul	d0,d0,d0
	fmul	d0,d0,d9
	bl	ak_sincos
	fcvt	s0,d0
	str	s0,[x21,x22,lsl 2]
	add	x22,x22,1
	b	.l
.done:	mov	x0,x21
.out:	ldp	d8,d9,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

		align	8
vb_halfpi	dd	1.5707963267948966

; ---------------------------------------------------------------------------
;  Paquet audio
; ---------------------------------------------------------------------------

; x0 = decodeur, x1 = paquet, x2 = octets -> trames rendues (0 : premier
; paquet ou paquet d'en-tete, < 0 : erreur)
vb_packet:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	bl	vb_state
	mov	x20,x0
	cbz	x22,.zero
	ldr	x0,[x20,VB_HDR]
	cmp	x0,7
	b.ne	.hdr
	ldrb	w0,[x21]
	tbnz	w0,0,.zero			; (paquet d'en-tete : rien)
	mov	x0,x21
	mov	x1,x22
	rb_init
	mov	x1,1
	bl	lr_get				; type 0
	ldr	x1,[x20,VB_MODEBITS]
	mov	x0,0
	cbz	x1,.m0
	bl	lr_get
.m0:	ldr	x1,[x20,VB_NMODE]
	cmp	x0,x1
	b.hs	.bad
	add	x1,x20,VB_MODES
	add	x1,x1,x0,lsl 4
	ldr	x2,[x1]				; drapeau de bloc
	ldr	x3,[x1,8]			; correspondance
	ldr	x4,[x20,VB_MAP]
	mov	x5,VBM_SIZE
	madd	x23,x3,x5,x4			; x23 = correspondance
	ldr	x24,[x20,VB_BS0]
	mov	x25,0				; drapeaux de fenetre
	cbz	x2,.short
	ldr	x24,[x20,VB_BS1]
	mov	x25,1
	mov	x1,1
	bl	lr_get
	cbz	x0,.pf
	orr	x25,x25,2
.pf:	mov	x1,1
	bl	lr_get
	cbz	x0,.short
	orr	x25,x25,4
.short:	str	x24,[x20,VB_N]
	str	x25,[x20,VB_FLAGS]
	; planchers
	mov	x26,0				; canal
.fl:	ldr	x0,[x20,VB_CH]
	cmp	x26,x0
	b.hs	.fld
	add	x0,x23,VBM_MUX
	ldrb	w0,[x0,x26]
	add	x1,x23,VBM_FLOOR
	ldrb	w0,[x1,x0]
	ldr	x1,[x20,VB_FLOOR]
	mov	x2,VFL_SIZE
	madd	x0,x0,x2,x1			; plancher
	ldr	x1,[x20,VB_Y]
	mov	x2,260
	madd	x1,x26,x2,x1			; Y du canal
	bl	vfl_decode
	tbnz	x0,63,.bad
	ldr	x1,[x20,VB_USED]
	strb	w0,[x1,x26]
	eor	w0,w0,1
	ldr	x1,[x20,VB_DONOT]
	strb	w0,[x1,x26]
	add	x26,x26,1
	b	.fl
.fld:	; couplage : les deux canaux d'un pas sont decodes si l'un l'est
	ldr	x2,[x23,VBM_STEPS]
	ldr	x3,[x20,VB_DONOT]
	mov	x4,0
.cp:	cmp	x4,x2
	b.hs	.cpd
	add	x5,x23,VBM_MAG
	ldrb	w5,[x5,x4]
	add	x6,x23,VBM_ANG
	ldrb	w6,[x6,x4]
	ldrb	w7,[x3,x5]
	ldrb	w8,[x3,x6]
	and	w7,w7,w8
	strb	w7,[x3,x5]
	strb	w7,[x3,x6]
	add	x4,x4,1
	b	.cp
.cpd:	; spectres a zero
	ldr	x0,[x20,VB_RESID]
	ldr	x1,[x20,VB_BS1]
	ldr	x2,[x20,VB_CH]
	mul	x1,x1,x2
	lsl	x1,x1,1
	bl	ak_memzero
	; residus, par sous-plan
	mov	x26,0
.sm:	ldr	x0,[x23,VBM_SUBMAPS]
	cmp	x26,x0
	b.hs	.smd
	mov	x0,x26
	bl	vrs_decode
	tbnz	x0,63,.bad
	add	x26,x26,1
	b	.sm
.smd:	; couplage inverse (pas dans l'ordre inverse)
	ldr	x26,[x23,VBM_STEPS]
.ic:	cbz	x26,.icd
	sub	x26,x26,1
	add	x5,x23,VBM_MAG
	ldrb	w5,[x5,x26]
	add	x6,x23,VBM_ANG
	ldrb	w6,[x6,x26]
	ldr	x7,[x20,VB_RESID]
	ldr	x8,[x20,VB_BS1]
	lsl	x8,x8,1				; octets par canal
	madd	x5,x5,x8,x7			; M
	madd	x6,x6,x8,x7			; A
	lsr	x7,x24,1
.icl:	cbz	x7,.ic
	ldr	s0,[x5]				; m
	ldr	s1,[x6]				; a
	fcmp	s0,0.0
	b.le	.mneg
	fcmp	s1,0.0
	b.le	.a1
	fsub	s2,s0,s1			; A = m - a
	str	s2,[x6]
	b	.icn
.a1:	str	s0,[x6]				; A = m, M = m + a
	fadd	s2,s0,s1
	str	s2,[x5]
	b	.icn
.mneg:	fcmp	s1,0.0
	b.le	.a2
	fadd	s2,s0,s1			; A = m + a
	str	s2,[x6]
	b	.icn
.a2:	str	s0,[x6]				; A = m, M = m - a
	fsub	s2,s0,s1
	str	s2,[x5]
.icn:	add	x5,x5,4
	add	x6,x6,4
	sub	x7,x7,1
	b	.icl
.icd:	; plancher x residu, IMDCT, fenetre, recouvrement
	mov	x26,0
.ch:	ldr	x0,[x20,VB_CH]
	cmp	x26,x0
	b.hs	.chd
	ldr	x27,[x20,VB_RESID]
	ldr	x0,[x20,VB_BS1]
	lsl	x0,x0,1
	madd	x27,x26,x0,x27			; spectre du canal
	ldr	x0,[x20,VB_USED]
	ldrb	w0,[x0,x26]
	cbnz	w0,.used
	mov	x0,x27				; plancher inutilise : silence
	lsl	x1,x24,1
	bl	ak_memzero
	b	.imdct
.used:	add	x0,x23,VBM_MUX
	ldrb	w0,[x0,x26]
	add	x1,x23,VBM_FLOOR
	ldrb	w0,[x1,x0]
	ldr	x1,[x20,VB_FLOOR]
	mov	x2,VFL_SIZE
	madd	x0,x0,x2,x1
	ldr	x1,[x20,VB_Y]
	mov	x2,260
	madd	x1,x26,x2,x1
	mov	x2,x27
	lsr	x3,x24,1
	bl	vfl_render
.imdct:	add	x0,x20,VB_MD0
	ldr	x1,[x20,VB_BS0]
	cmp	x24,x1
	b.eq	.md
	add	x0,x20,VB_MD1
.md:	mov	x1,x27
	ldr	x2,[x20,VB_TMP]
	bl	ak_imdct
	mov	x0,x26
	bl	vb_window
	add	x26,x26,1
	b	.ch
.chd:	; trames rendues : prev/4 + n/4 (0 au premier bloc)
	ldr	x0,[x20,VB_PREVN]
	str	x24,[x20,VB_PREVN]
	cbz	x0,.ret
	lsr	x0,x0,2
	add	x0,x0,x24,lsr 2
	b	.ret
.hdr:	mov	x0,AKE_CORRUPT
	b	.ret
.zero:	mov	x0,0
	b	.ret
.bad:	mov	x0,AKE_CORRUPT
.ret:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = canal (interne a vb_packet : x19, x20, x24 = n, VB_TMP = IMDCT) :
; fenetre, recouvrement avec la moitie droite precedente -> AKD_PCM[canal],
; puis la nouvelle moitie droite
vb_window:
	stp	x29,x30,[sp,-16]!
	ldr	x1,[x20,VB_TMP]
	ldr	x2,[x20,VB_FLAGS]
	ldr	x3,[x20,VB_BS0]
	lsr	x4,x24,1			; n/2
	lsr	x5,x24,2			; n/4
	lsr	x6,x3,2				; bs0/4
	; gauche : [ls, le[ avec la pente de longueur le - ls
	mov	x7,0				; ls
	mov	x8,x4				; le
	ldr	x9,[x20,VB_SLOPE0]
	tbz	x2,0,.lset			; bloc court
	ldr	x9,[x20,VB_SLOPE1]
	tbnz	x2,1,.lset			; precedent long
	ldr	x9,[x20,VB_SLOPE0]
	sub	x7,x5,x6
	add	x8,x5,x6
.lset:	mov	x10,0
.lz:	cmp	x10,x7
	b.hs	.ls
	str	wzr,[x1,x10,lsl 2]
	add	x10,x10,1
	b	.lz
.ls:	mov	x11,0
.lsl:	cmp	x10,x8
	b.hs	.mid
	ldr	s0,[x1,x10,lsl 2]
	ldr	s1,[x9,x11,lsl 2]
	fmul	s0,s0,s1
	str	s0,[x1,x10,lsl 2]
	add	x10,x10,1
	add	x11,x11,1
	b	.lsl
.mid:	; droite : [rs, re[ avec la pente renversee
	mov	x7,x4				; rs
	mov	x8,x24				; re
	ldr	x9,[x20,VB_SLOPE0]
	tbz	x2,0,.rset
	ldr	x9,[x20,VB_SLOPE1]
	tbnz	x2,2,.rset			; suivant long
	ldr	x9,[x20,VB_SLOPE0]
	add	x10,x4,x5			; 3n/4
	sub	x7,x10,x6
	add	x8,x10,x6
.rset:	sub	x11,x8,x7			; longueur de la pente
	mov	x10,x7
.rsl:	cmp	x10,x8
	b.hs	.rz
	sub	x11,x11,1
	ldr	s0,[x1,x10,lsl 2]
	ldr	s1,[x9,x11,lsl 2]
	fmul	s0,s0,s1
	str	s0,[x1,x10,lsl 2]
	add	x10,x10,1
	b	.rsl
.rz:	cmp	x10,x24
	b.hs	.ola
	str	wzr,[x1,x10,lsl 2]
	add	x10,x10,1
	b	.rz
.ola:	; recouvrement : sortie[i] = prec[i] (i < prevn/2) + tmp[i + n/4 - prevn/4]
	ldr	x2,[x20,VB_PREV]
	ldr	x3,[x20,VB_BS1]
	lsl	x3,x3,1
	madd	x2,x0,x3,x2			; moitie precedente du canal
	ldr	x3,[x20,VB_PREVN]
	cbz	x3,.save
	add	x6,x19,AKD_PCM
	ldr	x6,[x6,x0,lsl 3]		; sortie du canal
	lsr	x7,x3,2				; prevn/4
	add	x8,x7,x5			; trames
	lsr	x9,x3,1				; prevn/2
	sub	x10,x5,x7			; decalage : n/4 - prevn/4
	mov	x11,0
.ol:	cmp	x11,x8
	b.hs	.save
	fmov	s0,wzr
	cmp	x11,x9
	b.hs	.op
	ldr	s0,[x2,x11,lsl 2]
.op:	adds	x12,x11,x10
	b.mi	.oc
	cmp	x12,x4
	b.hs	.oc
	ldr	s1,[x1,x12,lsl 2]
	fadd	s0,s0,s1
.oc:	str	s0,[x6,x11,lsl 2]
	add	x11,x11,1
	b	.ol
.save:	; nouvelle moitie droite
	mov	x0,x2
	add	x1,x1,x4,lsl 2
	lsl	x2,x4,2
	bl	ak_memmove
	ldp	x29,x30,[sp],16
	ret

; Plancher 1 (lecteur en registres) : x0 = plancher, x1 = Y (65 x 32 bits)
; -> x0 = 1 utilise, 0 inutilise, < 0 erreur ; ne garde que x19-x28, x9-x12
vfl_decode:
	stp	x29,x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x1,1
	bl	lr_get
	cbz	x0,.out
	ldr	x0,[x21,VFL_RANGE]
	sub	x0,x0,1
	clz	x0,x0
	mov	x1,64
	sub	x23,x1,x0			; ilog(range - 1)
	mov	x1,x23
	bl	lr_get
	str	w0,[x22]
	mov	x1,x23
	bl	lr_get
	str	w0,[x22,4]
	mov	x24,2				; rang
	mov	x25,0				; partition
.p:	ldr	x0,[x21,VFL_PARTS]
	cmp	x25,x0
	b.hs	.done
	add	x0,x21,VFL_PCLASS
	ldrb	w26,[x0,x25]			; classe
	add	x0,x21,VFL_CSUB
	ldrb	w27,[x0,x26]			; bits de sous-classe
	mov	x28,0				; cval
	cbz	w27,.nc
	add	x0,x21,VFL_CMASTER
	ldrb	w0,[x0,x26]
	bl	.book
	bl	vcb_decode
	tbnz	x0,63,.bad
	mov	x28,x0
.nc:	add	x0,x21,VFL_CDIM
	ldrb	w23,[x0,x26]			; dimensions
.j:	cbz	w23,.pn
	mov	x0,1
	lsl	x0,x0,x27
	sub	x0,x0,1
	and	x0,x28,x0
	lsr	x28,x28,x27
	add	x1,x21,VFL_SUBBOOK
	add	x1,x1,x26,lsl 4
	ldrsh	w0,[x1,x0,lsl 1]
	mov	w1,0
	tbnz	w0,31,.y
	bl	.book
	bl	vcb_decode
	tbnz	x0,63,.bad
	mov	w1,w0
.y:	str	w1,[x22,x24,lsl 2]
	add	x24,x24,1
	sub	w23,w23,1
	b	.j
.pn:	add	x25,x25,1
	b	.p
.done:	lr_eop	.unused				; paquet trop court : inutilise
	mov	x0,1
	b	.out
.unused:
	mov	x0,0
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
	ret
; w0 = numero de livre -> x0 = livre (x20 = etat)
.book:	ldr	x1,[x20,VB_CB]
	mov	x2,VCB_SIZE
	madd	x0,x0,x2,x1
	ret

; x0 = plancher, x1 = Y, x2 = spectre (multiplie par la courbe), x3 = n/2
vfl_render:
	stp	x29,x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,336			; Y final (65 x 4), step2 (65 + 3)
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
	mov	x24,x3
	ldr	x25,[x21,VFL_VALUES]
	add	x26,x21,VFL_X
	ldr	w0,[x22]
	str	w0,[sp]
	ldr	w0,[x22,4]
	str	w0,[sp,4]
	mov	w0,1
	strb	w0,[sp,260]
	strb	w0,[sp,261]
	ldr	x27,[x21,VFL_RANGE]
	mov	x1,2
.i:	cmp	x1,x25
	b.hs	.curve
	add	x2,x21,VFL_LOW
	ldrb	w2,[x2,x1]
	add	x3,x21,VFL_HIGH
	ldrb	w3,[x3,x1]
	; prediction : render_point(X[lo], Y[lo], X[hi], Y[hi], X[i])
	ldrh	w4,[x26,x2,lsl 1]		; x0
	ldrh	w5,[x26,x3,lsl 1]		; x1
	ldr	w6,[sp,x2,lsl 2]		; y0
	ldr	w7,[sp,x3,lsl 2]		; y1
	ldrh	w8,[x26,x1,lsl 1]		; X
	sub	w9,w7,w6			; dy
	sub	w10,w5,w4			; adx
	cmp	w9,0
	cneg	w11,w9,lt			; ady
	sub	w12,w8,w4
	mul	w11,w11,w12
	sdiv	w11,w11,w10			; off
	cmp	w9,0
	sub	w12,w6,w11
	add	w13,w6,w11
	csel	w12,w12,w13,lt			; predicted
	ldr	w4,[x22,x1,lsl 2]		; val
	sub	w5,w27,w12			; highroom
	mov	w6,w12				; lowroom
	cmp	w5,w6
	csel	w7,w5,w6,lo
	lsl	w7,w7,1				; room
	add	x8,sp,260
	cbz	w4,.z
	mov	w9,1
	strb	w9,[x8,x2]
	strb	w9,[x8,x3]
	strb	w9,[x8,x1]
	cmp	w4,w7
	b.lo	.small
	cmp	w5,w6
	b.ls	.hl
	sub	w9,w4,w6			; val - lowroom + predicted
	add	w9,w9,w12
	b	.st
.hl:	sub	w9,w12,w4			; predicted - val + highroom - 1
	add	w9,w9,w5
	sub	w9,w9,1
	b	.st
.small:	tbz	w4,0,.even
	add	w9,w4,1				; impaire : predicted - (val + 1) / 2
	lsr	w9,w9,1
	sub	w9,w12,w9
	b	.st
.even:	add	w9,w12,w4,lsr 1
	b	.st
.z:	strb	wzr,[x8,x1]
	mov	w9,w12
.st:	str	w9,[sp,x1,lsl 2]
	add	x1,x1,1
	b	.i
.curve:	; trace dans l'ordre des X : segments entre points step2
	ldr	x28,[x21,VFL_MULT]
	mov	w0,0				; lx
	ldr	w1,[sp]
	mul	w1,w1,w28			; ly
	mov	w2,0				; hx
	mov	w3,w1				; hy
	mov	x4,1
.c:	cmp	x4,x25
	b.hs	.tail
	add	x5,x21,VFL_SORT
	ldrb	w5,[x5,x4]			; point
	add	x6,sp,260
	ldrb	w6,[x6,x5]
	cbz	w6,.cn
	ldr	w3,[sp,x5,lsl 2]
	mul	w3,w3,w28			; hy
	ldrh	w2,[x26,x5,lsl 1]		; hx
	stp	x2,x3,[sp,-16]!
	str	x4,[sp,-16]!
	bl	.line				; (w0, w1) -> (w2, w3)
	ldr	x4,[sp],16
	ldp	x2,x3,[sp],16
	mov	w0,w2
	mov	w1,w3
.cn:	add	x4,x4,1
	b	.c
.tail:	cmp	w2,w24
	b.hs	.ret
	mov	w0,w2
	mov	w1,w3
	mov	w2,w24
	bl	.line
.ret:	add	sp,sp,336
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
	ret
; render_line (x0 = w0, y0 = w1, x1 = w2, y1 = w3) sur [x0, x1[ (borne a n/2) :
; spectre[x] *= dB_inverse[y]
.line:	adr	x17,vb_invdb
	sub	w9,w3,w1			; dy
	sub	w10,w2,w0			; adx
	cmp	w10,0
	b.le	.lret
	sdiv	w11,w9,w10			; base
	cmp	w9,0
	cneg	w12,w9,lt			; ady
	cmp	w11,0
	cneg	w13,w11,lt			; |base|
	msub	w12,w13,w10,w12			; ady - |base| adx
	mov	w13,1
	cmp	w9,0
	cneg	w13,w13,lt
	add	w13,w11,w13			; sy = base -/+ 1
	mov	w14,w0				; x
	mov	w15,w1				; y
	mov	w16,0				; err
.lp:	cmp	w14,w2
	b.ge	.lret
	cmp	w14,w24
	b.ge	.lret
	cmp	w15,255				; (borne de la table)
	mov	w8,255
	csel	w8,w8,w15,gt
	cmp	w8,0
	csel	w8,wzr,w8,lt
	ldr	s1,[x17,x8,lsl 2]
	ldr	s0,[x23,x14,lsl 2]
	fmul	s0,s0,s1
	str	s0,[x23,x14,lsl 2]
	add	w14,w14,1
	add	w16,w16,w12
	cmp	w16,w10
	b.lt	.base
	sub	w16,w16,w10
	add	w15,w15,w13
	b	.lp
.base:	add	w15,w15,w11
	b	.lp
.lret:	ret

; x0 = sous-plan (interne a vb_packet : x19, x20, x23 = correspondance,
; x24 = n, lecteur en registres) : residu des canaux de ce sous-plan
; -> 0 / < 0
vrs_decode:
	stp	x29,x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,96			; canaux du sous-plan [0..8], vecteurs [16..80]
	mov	x21,x0
	add	x1,x23,VBM_RES
	ldrb	w1,[x1,x21]
	ldr	x2,[x20,VB_RES]
	mov	x3,VRS_SIZE
	madd	x22,x1,x3,x2			; residu
	; canaux de ce sous-plan : ceux a decoder (donot = 0) ; type 2 : tous
	; s'il y en a un
	mov	x25,0				; nombre
	mov	x26,0				; canal
	mov	x27,0				; a decoder ?
.lc:	ldr	x0,[x20,VB_CH]
	cmp	x26,x0
	b.hs	.lcd
	add	x0,x23,VBM_MUX
	ldrb	w0,[x0,x26]
	cmp	x0,x21
	b.ne	.lcn
	strb	w26,[sp,x25]
	ldr	x0,[x20,VB_DONOT]
	ldrb	w0,[x0,x26]
	add	x1,sp,8
	strb	w0,[x1,x25]			; donot du rang
	cbnz	w0,.lc2
	mov	x27,1
.lc2:	ldr	x0,[x20,VB_RESID]		; vecteur du canal
	ldr	x1,[x20,VB_BS1]
	lsl	x1,x1,1
	madd	x0,x26,x1,x0
	add	x1,sp,16
	str	x0,[x1,x25,lsl 3]
	add	x25,x25,1
.lcn:	add	x26,x26,1
	b	.lc
.lcd:	cbz	x25,.ok
	cbz	x27,.ok				; rien a decoder
	ldr	x0,[x22,VRS_TYPE]
	cmp	x0,2
	b.ne	.t01
	; type 2 : un vecteur entrelace de canaux x n/2 (dans VB_TMP)
	ldr	x0,[x20,VB_TMP]
	lsr	x1,x24,1
	mul	x1,x1,x25
	lsl	x1,x1,2
	bl	ak_memzero
	mov	x0,1				; une voie, a decoder
	ldr	x1,[x20,VB_TMP]
	add	x2,sp,8
	strb	wzr,[x2]
	add	x2,sp,16
	str	x1,[x2]
	lsr	x2,x24,1
	mul	x2,x2,x25			; longueur
	bl	vrs_body
	tbnz	x0,63,.out
	; desentrelacement : v_c[i] = t[i * canaux + c]
	ldr	x0,[x20,VB_TMP]
	lsr	x1,x24,1
	mov	x2,0				; i
.di:	cmp	x2,x1
	b.hs	.ok
	mov	x3,0				; c
.dc:	cmp	x3,x25
	b.hs	.dn
	madd	x4,x2,x25,x3
	ldr	s0,[x0,x4,lsl 2]
	ldr	x5,[x20,VB_RESID]
	ldrb	w6,[sp,x3]			; canal
	ldr	x7,[x20,VB_BS1]
	lsl	x7,x7,1
	madd	x5,x6,x7,x5
	str	s0,[x5,x2,lsl 2]
	add	x3,x3,1
	b	.dc
.dn:	add	x2,x2,1
	b	.di
.t01:	mov	x0,x25
	lsr	x2,x24,1
	bl	vrs_body
	b	.out
.ok:	mov	x0,0
.out:	add	sp,sp,96
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
	ret

; Corps du residu (interne a vrs_decode : x22 = residu, la pile de
; l'appelant : donot [sp+8+..], vecteurs [sp+16+..] decales par nos
; sauvegardes) : x0 = voies, x2 = longueur d'une voie -> 0 / < 0
; (une fin de paquet arrete le decodage : le reste vaut zero)
vrs_body:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp				; (cadre de l'appelant : x29 + 80 + 16)
	stp	x21,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	stp	x28,x23,[sp,-16]!
	stp	x19,x24,[sp,-16]!
	mov	x25,x0				; voies
	add	x26,x29,16			; pile de l'appelant : donot, vecteurs
	; borne : debut, fin dans la longueur
	ldr	x3,[x22,VRS_BEGIN]
	ldr	x4,[x22,VRS_END]
	cmp	x4,x2
	csel	x4,x4,x2,lo
	cmp	x3,x4
	b.hs	.ok
	ldr	x5,[x22,VRS_PSIZE]
	sub	x6,x4,x3
	udiv	x27,x6,x5			; partitions a lire
	cbz	x27,.ok
	ldr	x0,[x22,VRS_CLASSBOOK]
	ldr	x1,[x20,VB_CB]
	mov	x2,VCB_SIZE
	madd	x28,x0,x2,x1			; livre des classes
	ldr	x24,[x28,VCB_DIM]		; mots de classe par code
	mov	x21,0				; passe
.pass:	cmp	x21,8
	b.hs	.ok
	mov	x19,0				; partition
.pl:	cmp	x19,x27
	b.hs	.pn
	cbnz	x21,.part
	; passe 0 : classifications de chaque voie
	mov	x23,0
.cv:	cmp	x23,x25
	b.hs	.part
	add	x0,x26,8
	ldrb	w0,[x0,x23]
	cbnz	w0,.cvn
	mov	x0,x28
	bl	vcb_decode
	tbnz	x0,63,.ok			; (fin ou code invalide : fini)
	lr_eop	.ok
	ldr	x2,[x22,VRS_NCLASS]
	sub	x3,x24,1			; i = dim - 1 .. 0
.cd:	tbnz	x3,63,.cvn
	udiv	x4,x0,x2
	msub	x5,x4,x2,x0			; temp % classes
	mov	x0,x4
	add	x6,x19,x3
	cmp	x6,x27
	b.hs	.cdn
	ldr	x7,[x20,VB_CLASSIF]
	lsl	x8,x23,13			; 8192 par voie
	add	x7,x7,x8
	strb	w5,[x7,x6]
.cdn:	sub	x3,x3,1
	b	.cd
.cvn:	add	x23,x23,1
	b	.cv
.part:	; une partition de chaque voie
	mov	x0,0				; i < dim
.pi:	cmp	x0,x24
	b.hs	.pl
	cmp	x19,x27
	b.hs	.pn
	str	x0,[sp,-16]!
	mov	x23,0
.pv:	cmp	x23,x25
	b.hs	.pvd
	add	x0,x26,8
	ldrb	w0,[x0,x23]
	cbnz	w0,.pvn
	ldr	x7,[x20,VB_CLASSIF]
	lsl	x8,x23,13
	add	x7,x7,x8
	ldrb	w0,[x7,x19]			; classe
	add	x1,x22,VRS_BOOKS
	add	x1,x1,x0,lsl 4
	ldrsh	w1,[x1,x21,lsl 1]		; livre de la passe
	tbnz	w1,31,.pvn
	ldr	x2,[x20,VB_CB]
	mov	x3,VCB_SIZE
	madd	x0,x1,x3,x2
	add	x2,x26,16
	ldr	x1,[x2,x23,lsl 3]		; vecteur
	ldr	x2,[x22,VRS_PSIZE]
	ldr	x3,[x22,VRS_BEGIN]
	madd	x3,x19,x2,x3
	add	x1,x1,x3,lsl 2			; debut de la partition
	bl	vrs_partition
	tbnz	x0,63,.stop
.pvn:	add	x23,x23,1
	b	.pv
.pvd:	ldr	x0,[sp],16
	add	x19,x19,1
	add	x0,x0,1
	b	.pi
.stop:	add	sp,sp,16
	b	.ok
.pn:	add	x21,x21,1
	b	.pass
.ok:	mov	x0,0
	ldp	x19,x24,[sp],16
	ldp	x28,x23,[sp],16
	ldp	x26,x27,[sp],16
	ldp	x21,x25,[sp],16
	ldp	x29,x30,[sp],16
	ret

; x0 = livre, x1 = debut de la partition (flottants), x2 = taille ; x22 =
; residu (type 0 : entrelace ; 1, 2 : a la suite) -> 0, < 0 : fin du paquet
vrs_partition:
	stp	x29,x30,[sp,-16]!
	stp	x21,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	mov	x21,x0
	mov	x23,x1
	mov	x24,x2
	ldr	x25,[x21,VCB_DIM]
	ldr	x0,[x21,VCB_VQ]
	cbz	x0,.eop				; (livre sans vecteurs : invalide)
	ldr	x0,[x22,VRS_TYPE]
	cbnz	x0,.seq
	; type 0 : pas = taille / dim ; v[j + k pas] += vec[k]
	udiv	x26,x24,x25
	mov	x27,0				; j
.t0:	cmp	x27,x26
	b.hs	.ok
	mov	x0,x21
	bl	vcb_decode
	tbnz	x0,63,.eop
	lr_eop	.eop
	ldr	x1,[x21,VCB_VQ]
	mul	x0,x0,x25
	add	x1,x1,x0,lsl 2
	mov	x2,0
	add	x3,x23,x27,lsl 2
.t0k:	cmp	x2,x25
	b.hs	.t0n
	ldr	s0,[x1,x2,lsl 2]
	ldr	s1,[x3]
	fadd	s1,s1,s0
	str	s1,[x3]
	add	x3,x3,x26,lsl 2
	add	x2,x2,1
	b	.t0k
.t0n:	add	x27,x27,1
	b	.t0
.seq:	mov	x27,0				; i
.s:	cmp	x27,x24
	b.hs	.ok
	mov	x0,x21
	bl	vcb_decode
	tbnz	x0,63,.eop
	lr_eop	.eop
	ldr	x1,[x21,VCB_VQ]
	mul	x0,x0,x25
	add	x1,x1,x0,lsl 2
	mov	x2,0
.sk:	cmp	x2,x25
	b.hs	.s
	cmp	x27,x24
	b.hs	.ok
	ldr	s0,[x1,x2,lsl 2]
	ldr	s1,[x23,x27,lsl 2]
	fadd	s1,s1,s0
	str	s1,[x23,x27,lsl 2]
	add	x27,x27,1
	add	x2,x2,1
	b	.sk
.ok:	mov	x0,0
	b	.out
.eop:	mov	x0,-1
.out:	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	ldp	x21,x23,[sp],16
	ldp	x29,x30,[sp],16
	ret

; ---------------------------------------------------------------------------
;  API par paquets (MediaKit) : en-tetes en lacage Xiph
; ---------------------------------------------------------------------------

; x0 = decodeur, x1 = donnees (02, tailles, 3 en-tetes), x2 = octets -> 0 / < 0
vb_init_pkt:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	add	x22,x1,x2			; fin
	str	xzr,[x19,AKD_CODEC]
	bl	vb_new
	tbnz	x0,63,.out
	cmp	x21,x22
	b.hs	.bad
	ldrb	w0,[x21],1
	cmp	w0,2
	b.ne	.bad
	; deux tailles en lacage (255 + ...)
	mov	x23,0
.l1:	cmp	x21,x22
	b.hs	.bad
	ldrb	w0,[x21],1
	add	x23,x23,x0
	cmp	w0,255
	b.eq	.l1
	mov	x24,0
.l2:	cmp	x21,x22
	b.hs	.bad
	ldrb	w0,[x21],1
	add	x24,x24,x0
	cmp	w0,255
	b.eq	.l2
	add	x0,x21,x23
	add	x0,x0,x24
	cmp	x0,x22
	b.hi	.bad
	mov	x0,x19
	mov	x1,x21
	mov	x2,x23
	bl	vb_header
	tbnz	x0,63,.out
	add	x21,x21,x23
	mov	x0,x19
	mov	x1,x21
	mov	x2,x24
	bl	vb_header
	tbnz	x0,63,.out
	add	x21,x21,x24
	mov	x0,x19
	mov	x1,x21
	sub	x2,x22,x21
	bl	vb_header
	tbnz	x0,63,.out
	adr	x0,vb_packet
	str	x0,[x19,AKD_PKTFN]
	adr	x0,vb_reset
	str	x0,[x19,AKD_RESETFN]
	mov	x0,0
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include 'ak_vbtab.inc'
		align	4
