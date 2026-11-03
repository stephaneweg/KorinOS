; ============================================================================
;  kits/audiokit/ak_mp3.inc - MP3 : MPEG-1 (ISO/IEC 11172-3), MPEG-2 et 2.5
;  (13818-3, frequences moitie) couche III : decodage
;
;  Trame : en-tete, CRC saute, informations de cote, donnees principales
;  avec le reservoir de bits ; facteurs d'echelle (scfsi ; LSF), Huffman
;  (tables 0-31 et quadruplets, tables d'ak_mp3tab.inc), requantification
;  (|x|^4/3 et 2^(q/4)), stereo M/S et d'intensite (MPEG-1 et 2), blocs
;  courts et mixtes, anticrenelage, IMDCT 36/12 et fenetres, recouvrement,
;  inversion de frequence, banc de synthese polyphase (matrice N et
;  fenetre D en NEON). Sortie : flottants plans (AKD_F32).
;  Fichier : synchronisation, en-tete Xing/Info (nombre de trames, table de
;  recherche), ID3v1 a la fin ; recherche par la table Xing ou au prorata.
; ============================================================================

; --- etat ---------------------------------------------------------------------
MP_VER		= 0		; 1 MPEG-1, 2 MPEG-2, 3 MPEG-2.5
MP_LSF		= 8
MP_SRI		= 16		; frequence : indice 0..8 des tables
MP_RATE		= 24
MP_MODE		= 32		; 0 stereo, 1 stereo jointe, 2 double, 3 mono
MP_MODEXT	= 40
MP_NCH		= 48
MP_FLEN		= 56		; octets de la trame
MP_CRC		= 64
MP_BR		= 72		; kbit/s
MP_NGR		= 80
MP_MDB		= 88		; main_data_begin
MP_RESLEN	= 96		; octets du reservoir
MP_VPOS		= 104		; synthese : position dans V (2 canaux)
MP_SCFSI	= 128		; 2 x 4 octets
MP_GR		= 256		; granules : [gr][canal] x 128 o
MP_SF		= 768		; facteurs : [canal] x 64 o (longs 0..21, courts 24 + 3 sfb + f)
MP_ISMAX	= 896		; LSF : position d'intensite interdite par bande (64 o)
MP_NZ		= 960		; lignes non nulles (2 canaux)
MP_SFBL		= 976		; debuts des bandes longues (23 x 16 bits)
MP_SFBS		= 984		; debuts des bandes courtes (14 x 16 bits)
MP_TOCOK	= 992		; table Xing presente
MP_FRAMES	= 1000		; trames (Xing)
MP_POW43	= 1008		; tableaux (pointeurs)
MP_RES		= 1016		; reservoir (MP_RESMAX)
MP_MAIN		= 1024		; donnees principales de la trame
MP_IS		= 1032		; valeurs de Huffman (576 x 32 bits)
MP_XR		= 1040		; spectre (2 x 576 flottants)
MP_OVL		= 1048		; recouvrement (2 x 576)
MP_V		= 1056		; synthese (2 x 1024)
MP_SBS		= 1064		; sorties hybrides (18 x 32)
MP_TMP		= 1072		; 64 flottants
MP_SPF		= 1080		; trames par trame MP3 (1152 / 576)
MP_SIDE		= 1088		; octets d'informations de cote
MP_HDR		= 1096		; en-tete de reference (masque de synchro)
MP_TOC		= 1104		; table Xing (100 o)
MP_MIXL		= 1208		; blocs mixtes : lignes longues (36, 72 a 8 kHz)
MP_SIZE		= 1280

GI_P23		= 0		; informations d'un granule (32 bits)
GI_BIGV		= 4
GI_GAIN		= 8
GI_SFC		= 12
GI_WSF		= 16
GI_BT		= 20
GI_MIX		= 24
GI_TS		= 28		; 3
GI_SBG		= 40		; 3
GI_R0		= 52
GI_R1		= 56
GI_PRE		= 60
GI_SCALE	= 64
GI_C1T		= 68

MP_RESMAX	= 4096
MP_MAINMAX	= 8192
MP_POW43N	= 8208
MP_ARRAYS	= MP_POW43N*4 + MP_RESMAX + MP_MAINMAX + 2304 + 4608 + 4608 + 8192 + 2304 + 256 + 64
MP_SYNC_MASK	= 0xFFFE0C00	; synchro, version, couche, frequence

; position du lecteur en registres (bits depuis x21) -> x0 (interne)
macro mp3_bitpos {
	sub	x0,x11,x21
	lsl	x0,x0,3
	sub	x0,x0,x10
}

; ---------------------------------------------------------------------------
;  Ouverture
; ---------------------------------------------------------------------------

; x0 = decodeur -> x0 = etat (0 : memoire)
mp3_new:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x1,MP_SIZE
	bl	akd_alloc
	cbz	x0,.fail
	mov	x20,x0
	str	x20,[x19,AKD_CODEC]
	mov	x0,x19
	movl	x1,MP_ARRAYS
	bl	akd_alloc
	cbz	x0,.fail
	str	x0,[x20,MP_POW43]
	movl	x1,MP_POW43N*4
	add	x0,x0,x1
	str	x0,[x20,MP_RES]
	add	x0,x0,MP_RESMAX
	str	x0,[x20,MP_MAIN]
	add	x0,x0,MP_MAINMAX
	str	x0,[x20,MP_IS]
	add	x0,x0,2304
	str	x0,[x20,MP_XR]
	add	x0,x0,4096
	add	x0,x0,512
	str	x0,[x20,MP_OVL]
	add	x0,x0,4096
	add	x0,x0,512
	str	x0,[x20,MP_V]
	add	x0,x0,8192
	str	x0,[x20,MP_SBS]
	add	x0,x0,2304
	str	x0,[x20,MP_TMP]
	; |i|^(4/3) = i * racine cubique (Newton en double)
	ldr	x21,[x20,MP_POW43]
	str	wzr,[x21]
	fmov	d1,1.0				; y (racine cubique de i)
	fmov	d5,3.0
	mov	x22,1
.p:	scvtf	d0,x22
	mov	x2,4
.nw:	fmul	d2,d1,d1			; y - (y^3 - i) / (3 y^2)
	fmul	d3,d2,d1
	fsub	d3,d3,d0
	fmul	d2,d2,d5
	fdiv	d3,d3,d2
	fsub	d1,d1,d3
	subs	x2,x2,1
	b.ne	.nw
	fmul	d2,d0,d1
	fcvt	s2,d2
	str	s2,[x21,x22,lsl 2]
	add	x22,x22,1
	movl	x0,MP_POW43N
	cmp	x22,x0
	b.lo	.p
	mov	x0,x19
	mov	x1,1152
	mov	x2,2
	bl	akd_planes
	tbnz	x0,63,.fail
	mov	x0,AKD_F32
	str	x0,[x19,AKD_PCMFMT]
	mov	x0,AKF_MP3
	str	x0,[x19,AKD_FMT]
	adr	x0,mp3_pkt
	str	x0,[x19,AKD_PKTFN]
	adr	x0,mp3_reset
	str	x0,[x19,AKD_RESETFN]
	mov	x0,x20
	b	.out
.fail:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur : oublie le reservoir, le recouvrement et la synthese
mp3_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	str	xzr,[x20,MP_RESLEN]
	ldr	x0,[x20,MP_OVL]
	mov	x1,4608
	bl	ak_memzero
	ldr	x0,[x20,MP_V]
	mov	x1,8192
	bl	ak_memzero
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = donnees (rien), x2 = octets (API par paquets)
mp3_init_pkt:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	mp3_new
	cbz	x0,.nomem
	mov	x0,0
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = paquet (trames MP3), x2 = octets -> trames
mp3_pkt:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	mp3_frame
	tbnz	x0,63,.out
	ldr	x20,[x19,AKD_CODEC]
	ldr	x1,[x20,MP_RATE]
	str	x1,[x19,AKD_RATE]
	ldr	x1,[x20,MP_NCH]
	str	x1,[x19,AKD_CH]
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = en-tete (gros-boutiste, lu tel quel), x1 = etat -> x0 = octets de la
; trame (0 : en-tete invalide) ; pose MP_VER .. MP_SIDE
mp3_hdr:
	lsr	w2,w0,21
	cmp	w2,0x7FF
	b.ne	.bad
	ubfx	w2,w0,19,2			; version : 0 2.5, 2 MPEG-2, 3 MPEG-1
	cmp	w2,1
	b.eq	.bad
	ubfx	w3,w0,17,2			; couche III = 01
	cmp	w3,1
	b.ne	.bad
	ubfx	w4,w0,12,4			; debit
	cbz	w4,.bad				; (format libre : non)
	cmp	w4,15
	b.eq	.bad
	ubfx	w5,w0,10,2			; frequence
	cmp	w5,3
	b.eq	.bad
	mov	w6,1				; MPEG-1
	mov	w7,0				; indice de frequence
	cmp	w2,3
	b.eq	.v
	mov	w6,2
	mov	w7,3
	cbnz	w2,.v
	mov	w6,3
	mov	w7,6
.v:	add	w7,w7,w5
	str	x6,[x1,MP_VER]
	cmp	w6,1
	cset	w3,ne
	str	x3,[x1,MP_LSF]
	str	x7,[x1,MP_SRI]
	adr	x8,mp3_rates
	ldr	w8,[x8,x7,lsl 2]
	str	x8,[x1,MP_RATE]
	adr	x9,mp3_brtab
	add	x9,x9,x3,lsl 5
	ldrh	w9,[x9,x4,lsl 1]		; (tables de 16 x 16 bits)
	str	x9,[x1,MP_BR]
	ubfx	w10,w0,6,2
	str	x10,[x1,MP_MODE]
	ubfx	w11,w0,4,2
	str	x11,[x1,MP_MODEXT]
	cmp	w10,3				; mono : 1 canal
	mov	w11,2
	mov	w12,1
	csel	w11,w12,w11,eq
	str	x11,[x1,MP_NCH]
	ubfx	w12,w0,16,1			; protection : 0 -> CRC
	eor	w12,w12,1
	str	x12,[x1,MP_CRC]
	mov	w13,2				; granules
	mov	w14,1152
	cbz	w3,.g
	mov	w13,1
	mov	w14,576
.g:	str	x13,[x1,MP_NGR]
	str	x14,[x1,MP_SPF]
	; informations de cote : MPEG-1 17 / 32, LSF 9 / 17
	mov	w15,17
	cmp	w11,1
	b.eq	.s1
	mov	w15,32
.s1:	cbz	w3,.s2
	mov	w15,9
	cmp	w11,1
	b.eq	.s2
	mov	w15,17
.s2:	str	x15,[x1,MP_SIDE]
	; octets = 144000 * debit / frequence (MPEG-1), 72000 (LSF) + bourrage
	movl	w16,144000
	cbz	w3,.f
	movl	w16,72000
.f:	mul	x16,x16,x9
	udiv	x16,x16,x8
	ubfx	w17,w0,9,1
	add	x16,x16,x17
	str	x16,[x1,MP_FLEN]
	mov	x0,x16
	ret
.bad:	mov	x0,0
	ret

; x0 = decodeur (lecture au debut des trames) -> 0 / < 0
mp3_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	bl	mp3_new
	cbz	x0,.nomem
	mov	x20,x0
	; ID3v1 a la fin
	ldr	x0,[x19,AKD_DATAEND]
	cmp	x0,128
	b.lo	.noid3
	ldr	x21,[x19,AKD_OFF]
	sub	x1,x0,128
	mov	x0,x19
	bl	akd_seek
	mov	x0,x19
	mov	x1,128
	bl	akd_need
	cmp	x1,128
	b.lo	.back
	mov	x1,x0
	mov	x0,x19
	bl	akd_id3v1
	cbz	x0,.back
	ldr	x0,[x19,AKD_DATAEND]
	sub	x0,x0,128
	str	x0,[x19,AKD_DATAEND]
.back:	mov	x0,x19
	mov	x1,x21
	bl	akd_seek
.noid3:	; premiere trame valide (suivie d'une autre du meme genre)
	mov	x0,x19
	bl	mp3_sync
	tbnz	x0,63,.bad
	ldr	x0,[x19,AKD_OFF]
	str	x0,[x19,AKD_DATA0]
	ldr	x0,[x20,MP_RATE]
	str	x0,[x19,AKD_RATE]
	ldr	x0,[x20,MP_NCH]
	str	x0,[x19,AKD_CH]
	ldr	x0,[x20,MP_BR]
	mov	x1,1000
	mul	x0,x0,x1
	str	x0,[x19,AKD_BITRATE]
	; en-tete Xing / Info dans la premiere trame ?
	mov	x0,x19
	ldr	x1,[x20,MP_FLEN]
	bl	akd_need
	ldr	x2,[x20,MP_FLEN]
	cmp	x1,x2
	b.lo	.noxing
	mov	x21,x0
	ldr	x2,[x20,MP_SIDE]
	add	x2,x2,4
	ldr	x3,[x20,MP_CRC]
	add	x2,x2,x3,lsl 1
	add	x22,x21,x2			; apres les informations de cote
	ldr	w0,[x22]
	mov	x24,1				; 'Xing' : debit variable
	movl	w1,0x676E6958
	cmp	w0,w1
	b.eq	.xing
	mov	x24,0				; 'Info' : debit constant (au prorata)
	movl	w1,0x6F666E49
	cmp	w0,w1
	b.ne	.noxing
.xing:	ldr	w23,[x22,4]			; drapeaux
	rev	w23,w23
	add	x22,x22,8
	tbz	w23,0,.nofr
	ldr	w0,[x22],4			; trames
	rev	w0,w0
	str	x0,[x20,MP_FRAMES]
	ldr	x1,[x20,MP_SPF]
	mul	x0,x0,x1
	str	x0,[x19,AKD_TOTAL]
.nofr:	tbz	w23,1,.nob
	add	x22,x22,4			; octets
.nob:	tbz	w23,2,.skipx
	add	x0,x20,MP_TOC			; table de recherche (100 o)
	mov	x1,x22
	mov	x2,100
	bl	ak_memmove
	str	x24,[x20,MP_TOCOK]
.skipx:	mov	x0,x19				; la trame Xing n'a pas de son
	ldr	x1,[x20,MP_FLEN]
	bl	akd_skip
	ldr	x0,[x19,AKD_OFF]
	str	x0,[x19,AKD_DATA0]
	ldr	x0,[x20,MP_FRAMES]		; debit moyen
	cbz	x0,.noxing
	ldr	x1,[x19,AKD_DATAEND]
	ldr	x2,[x19,AKD_DATA0]
	sub	x1,x1,x2
	lsl	x1,x1,3
	ldr	x2,[x20,MP_RATE]
	mul	x1,x1,x2
	ldr	x2,[x19,AKD_TOTAL]
	udiv	x1,x1,x2
	str	x1,[x19,AKD_BITRATE]
	b	.total
.noxing:
	; duree estimee au debit de la premiere trame
	ldr	x0,[x19,AKD_DATAEND]
	ldr	x1,[x19,AKD_DATA0]
	sub	x0,x0,x1
	ldr	x1,[x20,MP_FLEN]
	udiv	x0,x0,x1
	ldr	x1,[x20,MP_SPF]
	mul	x0,x0,x1
	str	x0,[x19,AKD_TOTAL]
.total:	adr	x0,mp3_next
	str	x0,[x19,AKD_NEXT]
	adr	x0,mp3_seek
	str	x0,[x19,AKD_SEEKFN]
	mov	x0,0
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.bad:	mov	x0,AKE_FORMAT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur : avance jusqu'a une trame valide suivie d'une autre
; compatible (ou de la fin) -> 0 (MP_* poses), < 0 : rien dans les 1 Mo
mp3_sync:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x23,0				; octets sautes
.s:	ldr	x0,[x19,AKD_DATAEND]
	ldr	x1,[x19,AKD_OFF]
	sub	x24,x0,x1			; restants
	cmp	x24,4
	b.lt	.none
	mov	x0,x19
	mov	x1,8192
	bl	akd_need
	cmp	x1,x24
	csel	x24,x1,x24,lo
	cmp	x24,4
	b.lt	.none
	mov	x21,x0
	ldr	w22,[x21]
	rev	w22,w22
	mov	w0,w22
	mov	x1,x20
	bl	mp3_hdr
	cbz	x0,.n
	add	x1,x0,4				; la suivante
	cmp	x1,x24
	b.hi	.ok				; (fin des donnees : accepte)
	ldr	w2,[x21,x0]
	rev	w2,w2
	movl	w3,MP_SYNC_MASK
	and	w4,w2,w3
	and	w5,w22,w3
	cmp	w4,w5
	b.ne	.n
	mov	w0,w2				; l'autre doit etre valide aussi
	mov	x1,x20
	bl	mp3_hdr
	cbz	x0,.n
	mov	w0,w22				; (en-tete de la premiere)
	mov	x1,x20
	bl	mp3_hdr
.ok:	movl	w3,MP_SYNC_MASK
	and	w22,w22,w3
	str	x22,[x20,MP_HDR]
	mov	x0,0
	b	.out
.n:	mov	x0,x19
	mov	x1,1
	bl	akd_skip
	add	x23,x23,1
	movl	x0,1048576
	cmp	x23,x0
	b.lo	.s
.none:	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur -> trames decodees (0 : fin)
mp3_next:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x22,0				; essais
.again:	ldr	x0,[x19,AKD_DATAEND]
	ldr	x1,[x19,AKD_OFF]
	subs	x21,x0,x1
	cmp	x21,4
	b.lt	.end
	mov	x0,x19
	mov	x1,4096
	bl	akd_need
	cmp	x1,x21
	csel	x21,x1,x21,lo
	cmp	x21,4
	b.lt	.end
	ldr	w2,[x0]				; meme genre de trame qu'au debut ?
	rev	w2,w2
	movl	w3,MP_SYNC_MASK
	and	w2,w2,w3
	ldr	x3,[x20,MP_HDR]
	cmp	w2,w3
	b.ne	.resync
	mov	x1,x0
	mov	x2,x21
	mov	x0,x19
	bl	mp3_frame
	tbnz	x0,63,.resync
	mov	x22,x0
	mov	x0,x19				; x1 = octets de la trame
	bl	akd_skip
	mov	x0,x22
	b	.out
.resync:
	add	x22,x22,1
	cmp	x22,64
	b.hs	.end
	mov	x0,x19
	mov	x1,1
	bl	akd_skip
	mov	x0,x19
	bl	mp3_sync
	tbnz	x0,63,.end
	b	.again
.end:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = trame voulue -> 0 (a la trame MP3 pres)
mp3_seek:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x21,x1
	ldr	x0,[x19,AKD_TOTAL]
	cmp	x0,1
	b.lt	.start
	ldr	x2,[x19,AKD_DATAEND]
	ldr	x3,[x19,AKD_DATA0]
	sub	x2,x2,x3			; octets de son
	ldr	x4,[x20,MP_TOCOK]
	cbz	x4,.prop
	ldr	x6,[x20,MP_SPF]			; table Xing (8 trames plus tot) :
	mov	x5,8				; pourcentage -> 1/256 des octets
	mul	x5,x5,x6
	subs	x4,x21,x5
	csel	x4,xzr,x4,lt
	mov	x5,100
	mul	x4,x4,x5
	udiv	x4,x4,x0
	cmp	x4,99
	mov	x5,99
	csel	x4,x4,x5,ls
	add	x5,x20,MP_TOC
	ldrb	w5,[x5,x4]
	mul	x2,x2,x5
	lsr	x2,x2,8
	add	x2,x2,x3
	; position a la trame : celle du pourcentage
	ldr	x6,[x20,MP_SPF]
	mul	x4,x4,x0
	mov	x5,100
	udiv	x4,x4,x5
	udiv	x4,x4,x6
	mul	x22,x4,x6
	b	.go
.prop:	; au prorata (debit constant), 8 trames plus tot : le reservoir de bits
	; se remplit avant la trame voulue (elles sont sautees)
	ldr	x6,[x20,MP_SPF]
	udiv	x4,x21,x6			; trame voulue
	subs	x4,x4,8
	csel	x4,xzr,x4,lt
	mul	x22,x4,x6			; premiere trame decodee (echantillon)
	mul	x2,x2,x22
	udiv	x2,x2,x0
	add	x2,x2,x3
.go:	mov	x1,x2
	mov	x0,x19
	bl	akd_seek
	mov	x0,x19
	bl	mp3_sync
	ldr	x1,[x19,AKD_OFF]
	tbnz	x0,63,.start
	ldr	x0,[x20,MP_TOCOK]
	cbnz	x0,.done
	; debit constant : numero de la trame trouvee, d'apres sa position
	ldr	x0,[x19,AKD_OFF]
	ldr	x1,[x19,AKD_DATA0]
	sub	x0,x0,x1
	ldr	x6,[x20,MP_SPF]
	ldr	x2,[x19,AKD_TOTAL]
	udiv	x2,x2,x6			; trames
	mul	x0,x0,x2
	ldr	x3,[x19,AKD_DATAEND]
	sub	x3,x3,x1
	lsr	x4,x3,1
	add	x0,x0,x4			; (arrondi)
	udiv	x0,x0,x3
	mul	x22,x0,x6
	b	.done
.start:	ldr	x1,[x19,AKD_DATA0]
	mov	x0,x19
	bl	akd_seek
	mov	x22,0
.done:	mov	x0,x19
	bl	mp3_reset
	sub	x0,x21,x22			; le reste : saute
	cmp	x0,0
	csel	x0,xzr,x0,lt
	str	x0,[x19,AKD_SKIP]
	add	x0,x22,x0
	str	x0,[x19,AKD_POS]
	mov	x0,0
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Une trame
; ---------------------------------------------------------------------------

; x0 = decodeur, x1 = trame, x2 = octets disponibles -> x0 = trames (< 0 :
; erreur), x1 = octets de la trame
mp3_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,64			; lecteur de bits (BR_*)
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x21,x1
	mov	x22,x2
	cmp	x22,4
	b.lo	.short
	ldr	w0,[x21]
	rev	w0,w0
	mov	x1,x20
	bl	mp3_hdr
	cbz	x0,.bad
	mov	x23,x0				; octets de la trame
	cmp	x23,x22
	b.hi	.short
	ldr	x0,[x20,MP_SRI]			; tables de bandes de cette frequence
	adr	x1,mp3_sfb_long
	mov	x2,48
	madd	x1,x0,x2,x1
	str	x1,[x20,MP_SFBL]
	adr	x1,mp3_sfb_short
	mov	x2,32
	madd	x1,x0,x2,x1
	str	x1,[x20,MP_SFBS]
	cmp	x0,8
	mov	x1,36
	mov	x2,72
	csel	x1,x2,x1,eq
	str	x1,[x20,MP_MIXL]
	; informations de cote
	ldr	x0,[x20,MP_CRC]
	add	x1,x21,4
	add	x1,x1,x0,lsl 1
	ldr	x2,[x20,MP_SIDE]
	add	x24,x1,x2			; debut des donnees principales
	mov	x0,sp
	bl	br_init
	mov	x0,x20
	mov	x1,sp
	bl	mp3_side
	tbnz	x0,63,.bad
	; donnees principales : reservoir + celles de cette trame
	add	x25,x21,x23
	sub	x25,x25,x24			; octets de cette trame
	tbnz	x25,63,.bad
	ldr	x26,[x20,MP_MDB]
	ldr	x27,[x20,MP_RESLEN]
	mov	x28,0				; 1 : reservoir insuffisant
	cmp	x26,x27
	b.ls	.res
	mov	x28,1
	mov	x26,0
.res:	ldr	x0,[x20,MP_MAIN]
	ldr	x1,[x20,MP_RES]
	add	x1,x1,x27
	sub	x1,x1,x26
	mov	x2,x26
	bl	ak_memmove
	ldr	x0,[x20,MP_MAIN]
	add	x0,x0,x26
	mov	x1,x24
	mov	x2,x25
	bl	ak_memmove
	; le reservoir garde la fin (au plus MP_RESMAX octets)
	add	x0,x27,x25
	mov	x1,MP_RESMAX
	cmp	x0,x1
	b.ls	.app
	sub	x2,x0,x1			; a oublier
	ldr	x0,[x20,MP_RES]
	add	x1,x0,x2
	sub	x2,x27,x2
	mov	x27,x2
	bl	ak_memmove
.app:	ldr	x0,[x20,MP_RES]
	add	x0,x0,x27
	mov	x1,x24
	mov	x2,x25
	bl	ak_memmove
	add	x0,x27,x25
	str	x0,[x20,MP_RESLEN]
	cbnz	x28,.silent
	; granules
	add	x0,x26,x25
	str	x0,[sp,48]			; octets des donnees principales
	mov	x0,x19
	ldr	x1,[x20,MP_MAIN]
	ldr	x2,[sp,48]
	bl	mp3_granules
	tbnz	x0,63,.bad
	ldr	x0,[x20,MP_SPF]
	b	.ok
.silent:
	; pas assez de reservoir (debut de flux ou apres une recherche) :
	; du silence, et l'etat de synthese repart de zero
	add	x0,x19,AKD_PCM
	ldr	x0,[x0]
	ldr	x1,[x20,MP_SPF]
	lsl	x1,x1,2
	bl	ak_memzero
	add	x0,x19,AKD_PCM
	ldr	x0,[x0,8]
	ldr	x1,[x20,MP_SPF]
	lsl	x1,x1,2
	bl	ak_memzero
	ldr	x0,[x20,MP_SPF]
.ok:	mov	x1,x23
	b	.out
.short:	mov	x0,AKE_CORRUPT
	b	.out
.bad:	mov	x0,AKE_CORRUPT
	mov	x1,1
.out:	add	sp,sp,64
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat, x1 = lecteur (BR_*) sur les informations de cote -> 0 / < 0
mp3_side:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0
	mov	x19,x1
	ldr	x21,[x20,MP_LSF]
	ldr	x22,[x20,MP_NCH]
	mov	x0,x19
	mov	x1,9
	cbz	x21,.mdb
	mov	x1,8
.mdb:	bl	br_bits
	str	x0,[x20,MP_MDB]
	; bits prives
	mov	x1,5
	cmp	x22,1
	b.eq	.pv
	mov	x1,3
.pv:	cbz	x21,.pv2
	mov	x1,1
	cmp	x22,1
	b.eq	.pv2
	mov	x1,2
.pv2:	mov	x0,x19
	bl	br_bits
	cbnz	x21,.nosc
	mov	x23,0				; scfsi (MPEG-1)
.sc:	cmp	x23,x22
	b.hs	.nosc
	mov	x24,0
.sb:	mov	x0,x19
	mov	x1,1
	bl	br_bits
	add	x1,x20,MP_SCFSI
	add	x1,x1,x23,lsl 2
	strb	w0,[x1,x24]
	add	x24,x24,1
	cmp	x24,4
	b.lo	.sb
	add	x23,x23,1
	b	.sc
.nosc:	mov	x23,0				; granule
.gr:	ldr	x0,[x20,MP_NGR]
	cmp	x23,x0
	b.hs	.done
	mov	x24,0				; canal
.ch:	cmp	x24,x22
	b.hs	.ngr
	add	x0,x24,x23,lsl 1
	add	x8,x20,MP_GR
	add	x8,x8,x0,lsl 7			; informations du granule
	str	x8,[sp,-16]!
	mov	x0,x19
	mov	x1,12
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_P23]
	mov	x0,x19
	mov	x1,9
	bl	br_bits
	ldr	x8,[sp]
	cmp	w0,288
	b.hi	.badp
	str	w0,[x8,GI_BIGV]
	mov	x0,x19
	mov	x1,8
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_GAIN]
	mov	x0,x19
	mov	x1,4
	cbz	x21,.sfc
	mov	x1,9
.sfc:	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_SFC]
	mov	x0,x19
	mov	x1,1
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_WSF]
	cbz	w0,.nowsf
	mov	x0,x19
	mov	x1,2
	bl	br_bits
	ldr	x8,[sp]
	cbz	w0,.badp			; (type 0 interdit ici)
	str	w0,[x8,GI_BT]
	mov	x0,x19
	mov	x1,1
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_MIX]
	mov	x0,x19
	mov	x1,5
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_TS]
	mov	x0,x19
	mov	x1,5
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_TS+4]
	str	wzr,[x8,GI_TS+8]
	mov	x0,x19
	mov	x1,3
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_SBG]
	mov	x0,x19
	mov	x1,3
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_SBG+4]
	mov	x0,x19
	mov	x1,3
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_SBG+8]
	; regions implicites : 7 (8 pour les blocs courts non mixtes), le reste
	mov	w0,7
	ldr	w1,[x8,GI_BT]
	cmp	w1,2
	b.ne	.r7
	ldr	w1,[x8,GI_MIX]
	cbnz	w1,.r7
	mov	w0,8
.r7:	str	w0,[x8,GI_R0]
	mov	w0,20
	str	w0,[x8,GI_R1]
	b	.tail
.nowsf:	str	wzr,[x8,GI_BT]
	str	wzr,[x8,GI_MIX]
	str	wzr,[x8,GI_SBG]
	str	wzr,[x8,GI_SBG+4]
	str	wzr,[x8,GI_SBG+8]
	mov	x0,x19
	mov	x1,5
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_TS]
	mov	x0,x19
	mov	x1,5
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_TS+4]
	mov	x0,x19
	mov	x1,5
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_TS+8]
	mov	x0,x19
	mov	x1,4
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_R0]
	mov	x0,x19
	mov	x1,3
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_R1]
.tail:	str	wzr,[x8,GI_PRE]
	cbnz	x21,.nopre
	mov	x0,x19
	mov	x1,1
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_PRE]
.nopre:	mov	x0,x19
	mov	x1,1
	bl	br_bits
	ldr	x8,[sp]
	str	w0,[x8,GI_SCALE]
	mov	x0,x19
	mov	x1,1
	bl	br_bits
	ldr	x8,[sp],16
	str	w0,[x8,GI_C1T]
	add	x24,x24,1
	b	.ch
.ngr:	add	x23,x23,1
	b	.gr
.done:	mov	x0,0
	b	.out
.badp:	add	sp,sp,16
	mov	x0,AKE_CORRUPT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = donnees principales, x2 = octets -> 0 / < 0 :
; facteurs, Huffman, requantification, stereo, synthese de chaque granule
mp3_granules:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x21,x1				; debut
	mov	x22,x2
	mov	x25,0				; position (bits) dans les donnees
	mov	x23,0				; granule
.gr:	ldr	x0,[x20,MP_NGR]
	cmp	x23,x0
	b.hs	.done
	mov	x24,0				; canal
.ch:	ldr	x0,[x20,MP_NCH]
	cmp	x24,x0
	b.hs	.stereo
	add	x0,x24,x23,lsl 1
	add	x26,x20,MP_GR
	add	x26,x26,x0,lsl 7		; informations du granule
	; lecteur en registres a la position x25
	lsr	x0,x25,3
	add	x0,x21,x0
	add	x1,x21,x22
	sub	x1,x1,x0
	cmp	x1,0
	csel	x1,xzr,x1,lt
	rb_init
	and	x1,x25,7
	cbz	x1,.al
	bl	rb_get
.al:	ldr	w27,[x26,GI_P23]
	add	x27,x25,x27			; fin des bits du granule
	; facteurs d'echelle
	mov	x0,x24
	mov	x1,x23
	bl	mp3_scalefactors
	; Huffman
	mov	x0,x26
	mov	x1,x27
	mov	x2,x25
	mov	x3,x24
	bl	mp3_huffman
	; requantification -> spectre du canal
	mov	x0,x26
	mov	x1,x24
	bl	mp3_requant
	mov	x25,x27
	add	x24,x24,1
	b	.ch
.stereo:
	ldr	x0,[x20,MP_MODE]		; stereo jointe
	cmp	x0,1
	b.ne	.hyb
	mov	x0,x23
	bl	mp3_stereo
.hyb:	mov	x24,0
.hc:	ldr	x0,[x20,MP_NCH]
	cmp	x24,x0
	b.hs	.ngr
	add	x0,x24,x23,lsl 1
	add	x26,x20,MP_GR
	add	x26,x26,x0,lsl 7
	mov	x0,x26
	mov	x1,x24
	bl	mp3_hybrid
	mov	x0,x24
	mov	x1,x23
	bl	mp3_synth
	add	x24,x24,1
	b	.hc
.ngr:	add	x23,x23,1
	b	.gr
.done:	mov	x0,0
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Facteurs d'echelle (interne a mp3_granules : x19, x20, x26 = granule,
; lecteur x9-x12) ; x0 = canal, x1 = granule
mp3_scalefactors:
	stp	x29,x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x27,[sp,-16]!
	mov	x22,x0				; canal
	mov	x23,x1				; granule
	add	x24,x20,MP_SF
	add	x24,x24,x22,lsl 6		; facteurs du canal
	ldr	x0,[x20,MP_LSF]
	cbnz	x0,.lsf
	ldr	w0,[x26,GI_SFC]
	adr	x1,mp3_slen
	ldrb	w21,[x1,x0]			; slen1
	add	x1,x1,16
	ldrb	w25,[x1,x0]			; slen2
	ldr	w0,[x26,GI_BT]
	cmp	w0,2
	b.ne	.long
	ldr	w0,[x26,GI_MIX]
	cbz	w0,.short
	; mixte : longs 0..7 (slen1), courts 3..5 (slen1), 6..11 (slen2)
	mov	x27,0
.ml:	mov	x1,x21
	bl	.get
	strb	w0,[x24,x27]
	add	x27,x27,1
	cmp	x27,8
	b.lo	.ml
	mov	x27,3
	b	.sh
.short:	mov	x27,0
.sh:	cmp	x27,12
	b.hs	.s12
	cmp	x27,6
	csel	x1,x21,x25,lo
	mov	x0,3
	mul	x3,x27,x0
	add	x3,x3,24
	str	x3,[sp,-16]!
	bl	.get
	ldr	x3,[sp]
	strb	w0,[x24,x3]
	cmp	x27,6
	csel	x1,x21,x25,lo
	bl	.get
	ldr	x3,[sp]
	add	x3,x3,1
	strb	w0,[x24,x3]
	cmp	x27,6
	csel	x1,x21,x25,lo
	bl	.get
	ldr	x3,[sp],16
	add	x3,x3,2
	strb	w0,[x24,x3]
	add	x27,x27,1
	b	.sh
.s12:	add	x0,x24,24+36			; bande 12 : 0
	strb	wzr,[x0]
	strb	wzr,[x0,1]
	strb	wzr,[x0,2]
	b	.ret
.long:	; groupes 0-5, 6-10, 11-15, 16-20 ; scfsi au second granule
	mov	x27,0
.lg:	cmp	x27,21
	b.hs	.l21
	mov	x0,0
	cmp	x27,6
	b.lo	.grp
	mov	x0,1
	cmp	x27,11
	b.lo	.grp
	mov	x0,2
	cmp	x27,16
	b.lo	.grp
	mov	x0,3
.grp:	cbz	x23,.read
	add	x1,x20,MP_SCFSI
	add	x1,x1,x22,lsl 2
	ldrb	w1,[x1,x0]
	cbnz	w1,.keep
.read:	cmp	x0,2
	csel	x1,x21,x25,lo
	bl	.get
	strb	w0,[x24,x27]
.keep:	add	x27,x27,1
	b	.lg
.l21:	strb	wzr,[x24,21]
	b	.ret
.lsf:	; MPEG-2 : facteurs d'apres scalefac_compress (13818-3)
	ldr	w0,[x26,GI_SFC]
	sub	sp,sp,16			; slen[4] en [sp], indice de table en x3
	mov	w4,0				; preflag
	ldr	x5,[x20,MP_MODEXT]
	ldr	x6,[x20,MP_MODE]
	cmp	x6,1
	cset	w6,eq
	and	w5,w5,w6			; intensite ?
	and	w5,w5,1
	cmp	x22,1
	cset	w6,eq
	and	w5,w5,w6			; ... et canal droit
	cbnz	w5,.lsfi
	cmp	w0,400
	b.hs	.l500
	lsr	w1,w0,4				; < 400
	mov	w2,5
	udiv	w3,w1,w2
	msub	w7,w3,w2,w1
	strb	w3,[sp]
	strb	w7,[sp,1]
	ubfx	w1,w0,2,2
	strb	w1,[sp,2]
	and	w1,w0,3
	strb	w1,[sp,3]
	mov	x3,0
	b	.lsfr
.l500:	cmp	w0,500
	b.hs	.l512
	sub	w0,w0,400
	lsr	w1,w0,2
	mov	w2,5
	udiv	w3,w1,w2
	msub	w7,w3,w2,w1
	strb	w3,[sp]
	strb	w7,[sp,1]
	and	w1,w0,3
	strb	w1,[sp,2]
	strb	wzr,[sp,3]
	mov	x3,1
	b	.lsfr
.l512:	sub	w0,w0,500
	mov	w2,3
	udiv	w3,w0,w2
	msub	w7,w3,w2,w0
	strb	w3,[sp]
	strb	w7,[sp,1]
	strh	wzr,[sp,2]
	mov	x3,2
	mov	w4,1
	b	.lsfr
.lsfi:	lsr	w0,w0,1				; canal droit en intensite
	cmp	w0,180
	b.hs	.i244
	mov	w2,36
	udiv	w3,w0,w2
	msub	w7,w3,w2,w0			; % 36
	strb	w3,[sp]
	mov	w2,6
	udiv	w3,w7,w2
	msub	w7,w3,w2,w7
	strb	w3,[sp,1]
	strb	w7,[sp,2]
	strb	wzr,[sp,3]
	mov	x3,3
	b	.lsfr
.i244:	cmp	w0,244
	b.hs	.i255
	sub	w0,w0,180
	ubfx	w1,w0,4,2
	strb	w1,[sp]
	ubfx	w1,w0,2,2
	strb	w1,[sp,1]
	and	w1,w0,3
	strb	w1,[sp,2]
	strb	wzr,[sp,3]
	mov	x3,4
	b	.lsfr
.i255:	sub	w0,w0,244
	mov	w2,3
	udiv	w3,w0,w2
	msub	w7,w3,w2,w0
	strb	w3,[sp]
	strb	w7,[sp,1]
	strh	wzr,[sp,2]
	mov	x3,5
.lsfr:	str	w4,[x26,GI_PRE]
	; nombre de bandes : nsf[table][genre de bloc][4]
	mov	x1,0				; longs
	ldr	w0,[x26,GI_BT]
	cmp	w0,2
	b.ne	.bk
	mov	x1,1
	ldr	w0,[x26,GI_MIX]
	cbz	w0,.bk
	mov	x1,2
.bk:	mov	x0,12
	mul	x3,x3,x0
	add	x3,x3,x1,lsl 2
	adr	x0,mp3_nsf
	add	x3,x0,x3			; nsf[4]
	str	x1,[sp,8]			; genre de bloc
	; lire les facteurs dans une liste (MP_TMP, octets), avec le maximum
	; interdit de chacun (MP_ISMAX)
	ldr	x25,[x20,MP_TMP]
	mov	x27,0				; facteurs lus
	mov	x6,0				; groupe
.lg2:	cmp	x6,4
	b.hs	.map
	ldrb	w7,[x3,x6]			; nombre
	ldrb	w21,[sp,x6]			; bits
.lgn:	cbz	w7,.lgx
	mov	x1,x21
	stp	x3,x6,[sp,-16]!
	str	x7,[sp,-16]!
	bl	.get
	ldr	x7,[sp],16
	ldp	x3,x6,[sp],16
	strb	w0,[x25,x27]
	mov	w1,1				; position interdite = 2^slen - 1
	lsl	w1,w1,w21			; (aucune si slen = 0)
	sub	w1,w1,1
	cmp	w21,0
	mov	w2,255
	csel	w1,w2,w1,eq
	add	x2,x25,128			; (maxima : MP_TMP + 128)
	strb	w1,[x2,x27]
	add	x27,x27,1
	sub	w7,w7,1
	b	.lgn
.lgx:	add	x6,x6,1
	b	.lg2
.map:	; liste -> bandes longues ou courtes (et l'intensite au meme rang)
	ldr	x1,[sp,8]
	add	sp,sp,16
	add	x7,x20,MP_ISMAX
	add	x8,x25,128			; maxima de la liste
	mov	x2,0				; indice dans la liste
	mov	x27,0
	cbz	x1,.mlong
	cmp	x1,2
	b.ne	.mshort
	; mixte : 6 longues puis courtes depuis 3
.mm:	ldrb	w0,[x25,x2]
	strb	w0,[x24,x27]
	ldrb	w0,[x8,x2]
	strb	w0,[x7,x27]
	add	x2,x2,1
	add	x27,x27,1
	cmp	x27,6
	b.lo	.mm
	mov	x27,3
	b	.msl
.mshort:
	mov	x27,0
.msl:	cmp	x27,12
	b.hs	.ms12
	mov	x3,3
	mul	x3,x27,x3
.msw:	ldrb	w0,[x25,x2]
	add	x4,x3,24
	strb	w0,[x24,x4]
	ldrb	w0,[x8,x2]
	strb	w0,[x7,x4]
	add	x2,x2,1
	add	x3,x3,1
	mov	x4,3
	mul	x4,x27,x4
	add	x4,x4,3
	cmp	x3,x4
	b.lo	.msw
	add	x27,x27,1
	b	.msl
.ms12:	add	x0,x24,24+36
	strb	wzr,[x0]
	strb	wzr,[x0,1]
	strb	wzr,[x0,2]
	b	.ret
.mlong:	cmp	x27,21
	b.hs	.ml21
	ldrb	w0,[x25,x27]
	strb	w0,[x24,x27]
	ldrb	w0,[x8,x27]
	strb	w0,[x7,x27]
	add	x27,x27,1
	b	.mlong
.ml21:	strb	wzr,[x24,21]
.ret:	ldp	x25,x27,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
	ret
; x1 = bits (0 : 0) -> x0
.get:	cbz	x1,.g0
	b	rb_get
.g0:	mov	x0,0
	ret

; Huffman (interne) : x0 = granule, x1 = fin (bits), x2 = debut (bits, pour
; rien), x3 = canal ; lecteur x9-x12, debut des donnees x21
; -> MP_IS[576] (entiers signes), MP_NZ[canal] = lignes jusqu'a la derniere non nulle
mp3_huffman:
	stp	x29,x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x26,x0				; granule
	mov	x27,x1				; fin
	mov	x28,x3				; canal
	ldr	x22,[x20,MP_IS]
	; regions : debuts (lignes)
	ldr	w0,[x26,GI_BIGV]
	lsl	w23,w0,1			; fin des grandes valeurs
	cmp	w23,576
	mov	w0,576
	csel	w23,w0,w23,hi
	ldr	x5,[x20,MP_SFBL]
	ldr	w0,[x26,GI_WSF]
	cbz	w0,.reg
	ldr	w0,[x26,GI_BT]			; blocs speciaux : region 1 a la ligne
	cmp	w0,2				; 36 (courts et mixtes ; 72 a 8 kHz),
	b.ne	.r8				; a la bande longue 8 sinon ; pas de 2
	mov	w24,36
	ldr	x0,[x20,MP_SRI]
	cmp	x0,8
	b.ne	.r2
	mov	w24,72
	b	.r2
.r8:	ldrh	w24,[x5,16]
.r2:	mov	w25,576
	b	.regok
.reg:	ldr	w0,[x26,GI_R0]
	add	w0,w0,1
	cmp	w0,22
	mov	w1,22
	csel	w0,w1,w0,hi
	ldrh	w24,[x5,x0,lsl 1]
	ldr	w1,[x26,GI_R1]
	add	w0,w0,w1
	add	w0,w0,1
	cmp	w0,22
	mov	w1,22
	csel	w0,w1,w0,hi
	ldrh	w25,[x5,x0,lsl 1]
.regok:	; w24 = debut de la region 1, w25 = de la region 2
	mov	x13,0				; ligne
	mov	x14,0				; region
.big:	cmp	w13,w23
	b.hs	.count1
	; table de la region
	mov	w0,w23
	cmp	w13,w24
	b.hs	.rr1
	cmp	w24,w0
	csel	w0,w24,w0,lo
	ldr	w1,[x26,GI_TS]
	b	.tab
.rr1:	cmp	w13,w25
	b.hs	.rr2
	cmp	w25,w0
	csel	w0,w25,w0,lo
	ldr	w1,[x26,GI_TS+4]
	b	.tab
.rr2:	ldr	w1,[x26,GI_TS+8]
.tab:	mov	w15,w0				; fin de la region
	adr	x2,mp3_huffdir
	add	x2,x2,x1,lsl 3
	ldrh	w3,[x2]				; debut
	ldrh	w16,[x2,2]			; bits du premier niveau
	ldrh	w17,[x2,4]			; linbits
	cbnz	w16,.tok
	; table 0 : des zeros
.z0:	cmp	w13,w15
	b.hs	.big
	add	x0,x22,x13,lsl 2		; (2 lignes)
	str	xzr,[x0]
	add	x13,x13,2
	b	.z0
.tok:	adr	x4,mp3_huff
	add	x4,x4,x3,lsl 1			; table
	stp	x4,x16,[sp,-16]!
	str	x17,[sp,-16]!
	; x4 = table, x16 = bits, x17 = linbits (sur la pile : rb_fill les ecrase)
.pair:	cmp	w13,w15
	b.hs	.pend
	cmp	x10,48
	b.hs	.pk
	bl	rb_fill
.pk:	ldr	x0,[sp,16]			; table
	ldr	x1,[sp,24]			; bits
.lk:	mov	x2,64
	sub	x2,x2,x1
	lsr	x2,x9,x2
	ldrh	w3,[x0,x2,lsl 1]
	tbz	w3,15,.leaf
	lsl	x9,x9,x1			; noeud : sous-table
	sub	x10,x10,x1
	and	x2,x3,0x7FF
	ldr	x0,[sp,16]
	add	x0,x0,x2,lsl 1
	ubfx	x1,x3,11,4
	b	.lk
.leaf:	ubfx	x2,x3,8,4			; longueur
	lsl	x9,x9,x2
	sub	x10,x10,x2
	ubfx	x5,x3,4,4			; x
	and	x6,x3,15			; y
	ldr	x7,[sp]				; linbits
	cbz	x5,.x0
	cmp	x5,15
	b.ne	.xs
	cbz	x7,.xs
	mov	x2,64
	sub	x2,x2,x7
	lsr	x2,x9,x2
	lsl	x9,x9,x7
	sub	x10,x10,x7
	add	x5,x5,x2
.xs:	tbz	x9,63,.xp			; signe
	neg	x5,x5
.xp:	lsl	x9,x9,1
	sub	x10,x10,1
.x0:	cbz	x6,.y0
	cmp	x6,15
	b.ne	.ys
	cbz	x7,.ys
	mov	x2,64
	sub	x2,x2,x7
	lsr	x2,x9,x2
	lsl	x9,x9,x7
	sub	x10,x10,x7
	add	x6,x6,x2
.ys:	tbz	x9,63,.yp
	neg	x6,x6
.yp:	lsl	x9,x9,1
	sub	x10,x10,1
.y0:	str	w5,[x22,x13,lsl 2]
	add	x13,x13,1
	str	w6,[x22,x13,lsl 2]
	add	x13,x13,1
	b	.pair
.pend:	add	sp,sp,32
	b	.big
.count1:
	; quadruplets jusqu'a la fin des bits du granule
	ldr	w0,[x26,GI_C1T]
	mov	x15,x13				; derniere ligne non nulle + 1 (approchee)
.q:	cmp	x13,572
	b.hi	.qend
	mp3_bitpos
	cmp	x0,x27
	b.hs	.qend
	cmp	x10,32
	b.hs	.qk
	bl	rb_fill
.qk:	ldr	w0,[x26,GI_C1T]
	cbnz	w0,.qb
	adr	x0,mp3_quadA			; table A
	mov	x1,MP3_QUADA_BITS
	mov	x2,64
	sub	x2,x2,x1
	lsr	x2,x9,x2
	ldrh	w3,[x0,x2,lsl 1]
	ubfx	x2,x3,8,4
	lsl	x9,x9,x2
	sub	x10,x10,x2
	and	x3,x3,15
	b	.qv
.qb:	lsr	x3,x9,60			; table B : 4 bits inverses
	lsl	x9,x9,4
	sub	x10,x10,4
	mvn	w3,w3
	and	w3,w3,15
.qv:	mov	x4,3				; v, w, x, y (bits 3..0)
.qs:	lsr	w5,w3,w4
	and	w5,w5,1
	cbz	w5,.qz
	tbz	x9,63,.qsp
	neg	w5,w5
.qsp:	lsl	x9,x9,1
	sub	x10,x10,1
.qz:	str	w5,[sp,-16]!			; (garde dans l'ordre)
	subs	x4,x4,1
	b.pl	.qs
	mp3_bitpos				; depasse la fin : quadruplet ignore
	cmp	x0,x27
	b.hi	.qdrop
	ldr	w5,[sp,48]
	str	w5,[x22,x13,lsl 2]
	ldr	w5,[sp,32]
	add	x0,x13,1
	str	w5,[x22,x0,lsl 2]
	ldr	w5,[sp,16]
	add	x0,x13,2
	str	w5,[x22,x0,lsl 2]
	ldr	w5,[sp]
	add	x0,x13,3
	str	w5,[x22,x0,lsl 2]
	add	sp,sp,64
	add	x13,x13,4
	b	.q
.qdrop:	add	sp,sp,64
.qend:	; le reste a zero
	add	x0,x22,x13,lsl 2
	mov	x1,576
	sub	x1,x1,x13
	lsl	x1,x1,2
	bl	ak_memzero
	; derniere ligne non nulle
	mov	x0,x13
.nz:	cbz	x0,.nzd
	sub	x1,x0,1
	ldr	w2,[x22,x1,lsl 2]
	cbnz	w2,.nzd
	mov	x0,x1
	b	.nz
.nzd:	add	x1,x20,MP_NZ
	str	x0,[x1,x28,lsl 3]

	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
	ret

; x0 = granule, x1 = canal : MP_IS -> MP_XR[canal] (ordre de la norme)
; xr = signe * |is|^4/3 * 2^(q / 4)
mp3_requant:
	stp	x29,x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x26,x0
	mov	x2,2304
	ldr	x21,[x20,MP_XR]
	madd	x21,x1,x2,x21			; sortie
	add	x22,x20,MP_SF
	add	x22,x22,x1,lsl 6		; facteurs
	ldr	x23,[x20,MP_IS]
	ldr	x24,[x20,MP_POW43]
	ldr	w0,[x26,GI_GAIN]
	sub	w25,w0,210			; q de base
	ldr	w0,[x26,GI_SCALE]
	add	w0,w0,1
	lsl	w8,w0,1				; multiplicateur : 2 ou 4 (quarts)
	ldr	w0,[x26,GI_PRE]
	adr	x7,mp3_pretab
	cmp	w0,0
	csel	x7,x7,xzr,ne			; (0 : pas de pretab)
	mov	x13,0				; ligne
	ldr	w0,[x26,GI_BT]
	cmp	w0,2
	b.ne	.long
	ldr	w0,[x26,GI_MIX]
	cbz	w0,.short
	; mixte : bandes longues jusqu'a la ligne 36 (MPEG-1 : 8 bandes ; LSF : 6)
	mov	x14,0				; bande
	ldr	x5,[x20,MP_SFBL]
	ldr	x6,[x20,MP_MIXL]
.ml:	ldrh	w15,[x5,x14,lsl 1]
	cmp	w15,w6
	b.hs	.mle
	ldrb	w0,[x22,x14]
	mov	w1,0
	cbz	x7,.mlp
	ldrb	w1,[x7,x14]
.mlp:	add	w0,w0,w1
	mul	w0,w0,w8
	sub	w0,w25,w0
	add	x1,x14,1
	ldrh	w15,[x5,x1,lsl 1]
	cmp	w15,w6
	csel	w15,w6,w15,hi
	bl	.band
	add	x14,x14,1
	b	.ml
.mle:	mov	x14,3				; puis les courtes depuis la bande 3
	b	.sl
.short:	mov	x14,0
.sl:	cmp	x14,13
	b.hs	.ret
	ldr	x5,[x20,MP_SFBS]
	ldrh	w0,[x5,x14,lsl 1]
	add	x1,x14,1
	ldrh	w1,[x5,x1,lsl 1]
	sub	w6,w1,w0			; largeur
	mov	w2,3
	mul	w13,w0,w2			; debut (3 fenetres)
	mov	x16,0				; fenetre
.sw:	cmp	x16,3
	b.hs	.snx
	add	x0,x26,GI_SBG
	ldr	w0,[x0,x16,lsl 2]
	lsl	w0,w0,3
	sub	w0,w25,w0
	mov	w1,0
	cmp	x14,12
	b.hs	.s12
	mov	x2,3
	madd	x2,x14,x2,x16
	add	x2,x2,24
	ldrb	w1,[x22,x2]
.s12:	mul	w1,w1,w8
	sub	w0,w0,w1
	add	w15,w13,w6
	stp	x6,x16,[sp,-16]!
	bl	.band
	ldp	x6,x16,[sp],16
	add	x16,x16,1
	b	.sw
.snx:	add	x14,x14,1
	b	.sl
.long:	mov	x14,0
	ldr	x5,[x20,MP_SFBL]
.ll:	cmp	x14,22
	b.hs	.ret
	mov	w0,0
	cmp	x14,21
	b.hs	.l21
	ldrb	w0,[x22,x14]
	cbz	x7,.l21
	ldrb	w1,[x7,x14]
	add	w0,w0,w1
.l21:	mul	w0,w0,w8
	sub	w0,w25,w0
	add	x1,x14,1
	ldrh	w15,[x5,x1,lsl 1]
	bl	.band
	add	x14,x14,1
	b	.ll
.ret:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
	ret
; lignes [x13, w15[ avec q = w0 ; x13 avance
.band:	asr	w1,w0,2				; 2^(q/4) = 2^(q>>2) * 2^((q&3)/4)
	and	w2,w0,3
	adr	x3,mp3_pow2q
	ldr	w3,[x3,x2,lsl 2]
	add	w3,w3,w1,lsl 23
	cmp	w1,-120
	csel	w3,wzr,w3,lt
	fmov	s1,w3
.bl:	cmp	w13,w15
	b.hs	.bret
	ldr	w0,[x23,x13,lsl 2]
	cmp	w0,0
	cneg	w1,w0,lt
	movl	w2,MP_POW43N-1
	cmp	w1,w2
	csel	w1,w2,w1,hi
	ldr	s0,[x24,x1,lsl 2]
	fmul	s0,s0,s1
	cmp	w0,0
	fneg	s2,s0
	fcsel	s0,s2,s0,lt
	str	s0,[x21,x13,lsl 2]
	add	x13,x13,1
	b	.bl
.bret:	ret

; x0 = granule : stereo jointe (M/S, intensite) sur MP_XR
mp3_stereo:
	stp	x29,x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	add	x26,x20,MP_GR
	add	x26,x26,x0,lsl 8		; canal 0 du granule (canal 1 : +128)
	ldr	x21,[x20,MP_XR]			; gauche
	add	x22,x21,2304			; droite
	ldr	x23,[x20,MP_MODEXT]
	mov	x24,576				; limite de M/S (intensite au-dessus)
	tbz	x23,0,.ms
	bl	mp3_intensity			; -> x24 = debut de l'intensite
.ms:	tbz	x23,1,.ret
	movl	w0,0x3F3504F3			; 1 / racine de 2
	dup	v7.4s,w0
	mov	x0,0
.l:	add	x1,x0,4
	cmp	x1,x24
	b.hi	.l1
	add	x2,x21,x0,lsl 2
	add	x3,x22,x0,lsl 2
	ldr	q0,[x2]
	ldr	q1,[x3]
	fadd	v2.4s,v0.4s,v1.4s
	fsub	v3.4s,v0.4s,v1.4s
	fmul	v2.4s,v2.4s,v7.4s
	fmul	v3.4s,v3.4s,v7.4s
	str	q2,[x2]
	str	q3,[x3]
	add	x0,x0,4
	b	.l
.l1:	cmp	x0,x24
	b.hs	.ret
	ldr	s0,[x21,x0,lsl 2]
	ldr	s1,[x22,x0,lsl 2]
	fadd	s2,s0,s1
	fsub	s3,s0,s1
	fmul	s2,s2,s7
	fmul	s3,s3,s7
	str	s2,[x21,x0,lsl 2]
	str	s3,[x22,x0,lsl 2]
	add	x0,x0,1
	b	.l1
.ret:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
	ret

; Stereo d'intensite (interne a mp3_stereo : x20, x21 gauche, x22 droite,
; x26 granule du canal 0) -> x24 = premiere ligne en intensite (bandes
; longues ; pour les blocs courts, M/S s'applique sous la limite de chaque
; fenetre : approchee par la plus basse)
mp3_intensity:
	stp	x29,x30,[sp,-16]!
	add	x27,x26,128			; granule du canal droit
	add	x28,x20,MP_SF+64		; facteurs du canal droit
	ldr	x0,[x20,MP_NZ+8]		; derniere ligne non nulle a droite
	mov	x24,576
	ldr	w1,[x27,GI_BT]
	cmp	w1,2
	b.eq	.short
	; longues : la premiere bande au-dessus de la derniere ligne non nulle
	ldr	x5,[x20,MP_SFBL]
	mov	x14,0
.f:	cmp	x14,22
	b.hs	.ret
	add	x1,x14,1
	ldrh	w1,[x5,x1,lsl 1]
	cmp	w1,w0
	b.hs	.fl
	add	x14,x14,1
	b	.f
.fl:	add	x1,x14,1			; bande suivant celle de la derniere non nulle
	cbz	w0,.fz
	mov	x14,x1
.fz:	ldrh	w24,[x5,x14,lsl 1]
	cmp	x14,22
	b.hs	.ret
	mov	x6,x14				; premiere bande en intensite
.lb:	cmp	x14,22
	b.hs	.ret
	ldrh	w13,[x5,x14,lsl 1]
	add	x1,x14,1
	ldrh	w15,[x5,x1,lsl 1]
	mov	x2,x14
	cmp	x2,21				; bande 21 : la position de la 20 si
	b.lo	.lpos				; elle est en intensite, sinon celle
	mov	x2,20				; par defaut
	cmp	x6,20
	b.hi	.ldef
.lpos:	ldrb	w0,[x28,x2]
	add	x1,x20,MP_ISMAX
	ldrb	w1,[x1,x2]
	b	.lap
.ldef:	bl	.defpos
.lap:	bl	.apply
	add	x14,x14,1
	b	.lb
.short:	; courtes : par fenetre, au-dessus de la derniere ligne non nulle de
	; la fenetre (droite)
	ldr	x5,[x20,MP_SFBS]
	mov	x16,0				; fenetre
.sw:	cmp	x16,3
	b.hs	.ret
	ldr	x6,[x20,MP_IS]			; (valeurs du canal droit perdues :
	; on repere la derniere bande non nulle sur le spectre requantifie)
	mov	x14,12
	mov	x17,0				; premiere bande en intensite
.sb:	ldrh	w0,[x5,x14,lsl 1]
	add	x1,x14,1
	ldrh	w1,[x5,x1,lsl 1]
	sub	w2,w1,w0
	mov	w3,3
	mul	w13,w0,w3
	madd	w13,w2,w16,w13			; debut de la fenetre dans la bande
	add	w15,w13,w2
	mov	x3,x13
.sz:	cmp	w3,w15
	b.hs	.szz
	ldr	w4,[x22,x3,lsl 2]
	cbnz	w4,.snz
	add	x3,x3,1
	b	.sz
.szz:	cbz	x14,.snz0
	sub	x14,x14,1
	b	.sb
.snz0:	mov	x14,-1
.snz:	add	x17,x14,1			; premiere bande en intensite
	mov	x14,x17
	ldrh	w0,[x5,x14,lsl 1]
	mov	w3,3
	mul	w0,w0,w3
	cmp	x24,x0
	csel	x24,x0,x24,hi
.sib:	cmp	x14,13
	b.hs	.snw
	ldrh	w0,[x5,x14,lsl 1]
	add	x1,x14,1
	ldrh	w1,[x5,x1,lsl 1]
	sub	w2,w1,w0
	mov	w3,3
	mul	w13,w0,w3
	madd	w13,w2,w16,w13
	add	w15,w13,w2
	mov	x2,x14
	cmp	x2,12				; bande 12 : comme la 21 des longues
	b.lo	.spos
	mov	x2,11
	cmp	x17,11
	b.ls	.spos
	bl	.defpos
	b	.sap
.spos:	mov	x3,3
	madd	x2,x2,x3,x16
	add	x3,x2,24
	ldrb	w0,[x28,x3]
	add	x1,x20,MP_ISMAX
	ldrb	w1,[x1,x3]
.sap:	stp	x14,x16,[sp,-16]!
	str	x5,[sp,-16]!
	bl	.apply
	ldr	x5,[sp],16
	ldp	x14,x16,[sp],16
	add	x14,x14,1
	b	.sib
.snw:	add	x16,x16,1
	b	.sw
.ret:	ldp	x29,x30,[sp],16
	ret
; position par defaut de la bande du haut : 3 (MPEG-1), 0 (LSF) ; w1 :
; aucune position interdite
.defpos:
	mov	w0,3
	ldr	x1,[x20,MP_LSF]
	cmp	x1,0
	csel	w0,w0,wzr,eq
	mov	w1,255
	ret
; lignes [w13, w15[, position w0, position interdite w1 (LSF)
.apply:	ldr	x2,[x20,MP_LSF]
	cbnz	x2,.alsf
	cmp	w0,7
	b.hs	.ams				; 7 : pas d'intensite (M/S eventuel)
	adr	x2,mp3_is_l
	ldr	s2,[x2,x0,lsl 2]
	adr	x2,mp3_is_r
	ldr	s3,[x2,x0,lsl 2]
	b	.aap
.alsf:	cmp	w0,w1
	b.eq	.ams
	ldr	w2,[x27,GI_SFC]			; echelle : bit 0 de scalefac_compress
	and	w2,w2,1
	adr	x3,mp3_is_lsf
	add	x3,x3,x2,lsl 7
	fmov	s2,1.0
	fmov	s3,1.0
	cbz	w0,.aap
	add	w2,w0,1
	lsr	w2,w2,1
	ldr	s4,[x3,x2,lsl 2]
	tbz	w0,0,.even
	fmov	s2,s4				; impaire : gauche reduite
	b	.aap
.even:	lsr	w2,w0,1
	ldr	s3,[x3,x2,lsl 2]
.aap:	cmp	w13,w15
	b.hs	.aret
	ldr	s0,[x21,x13,lsl 2]
	fmul	s1,s0,s3
	fmul	s0,s0,s2
	str	s0,[x21,x13,lsl 2]
	str	s1,[x22,x13,lsl 2]
	add	x13,x13,1
	b	.aap
.ams:	tbz	x23,1,.aret			; M/S dans cette bande
	movl	w2,0x3F3504F3
	fmov	s4,w2
.am:	cmp	w13,w15
	b.hs	.aret
	ldr	s0,[x21,x13,lsl 2]
	ldr	s1,[x22,x13,lsl 2]
	fadd	s2,s0,s1
	fsub	s3,s0,s1
	fmul	s2,s2,s4
	fmul	s3,s3,s4
	str	s2,[x21,x13,lsl 2]
	str	s3,[x22,x13,lsl 2]
	add	x13,x13,1
	b	.am
.aret:	ret

; x0 = granule, x1 = canal : MP_XR -> MP_SBS (18 x 32) : blocs courts
; reordonnes, anticrenelage, IMDCT, fenetre, recouvrement, inversion
mp3_hybrid:
	stp	x29,x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x26,x0
	mov	x2,2304
	ldr	x21,[x20,MP_XR]
	madd	x21,x1,x2,x21			; spectre
	ldr	x22,[x20,MP_OVL]
	madd	x22,x1,x2,x22			; recouvrement
	ldr	x23,[x20,MP_SBS]
	ldr	x24,[x20,MP_TMP]
	ldr	w27,[x26,GI_BT]
	ldr	w28,[x26,GI_MIX]
	cmp	w27,2
	b.ne	.alias
	; blocs courts : (bande, fenetre, f) -> (sous-bande, fenetre, k)
	; dans MP_TMP... par une copie du spectre court
	mov	x13,0				; debut de la partie courte
	cbz	w28,.ro
	ldr	x13,[x20,MP_MIXL]
.ro:	ldr	x6,[x20,MP_IS]			; (tampon libre de 576 x 4)
	mov	x0,x6
	add	x1,x21,x13,lsl 2
	mov	x2,576
	sub	x2,x2,x13
	lsl	x2,x2,2
	bl	ak_memmove
	ldr	x5,[x20,MP_SFBS]
	mov	x14,0				; bande courte
	cbz	w28,.rb
	mov	x14,3
.rb:	cmp	x14,13
	b.hs	.rdone
	ldrh	w0,[x5,x14,lsl 1]		; f0
	add	x1,x14,1
	ldrh	w1,[x5,x1,lsl 1]
	sub	w2,w1,w0			; largeur
	mov	x16,0
.rw:	cmp	x16,3
	b.hs	.rbn
	mov	x3,0
.rf:	cmp	w3,w2
	b.hs	.rwn
	; source : 3 * f0 + w * largeur + f (depuis le debut de la partie courte)
	mov	w4,3
	mul	w4,w0,w4
	madd	w4,w16,w2,w4
	add	w4,w4,w3
	sub	w4,w4,w13
	ldr	s0,[x6,x4,lsl 2]
	; destination : 18 * sb + 6 * w + k, avec f0 + f = 6 * sb + k
	add	w7,w0,w3
	mov	w8,6
	udiv	w15,w7,w8			; sb
	msub	w17,w15,w8,w7			; k
	mov	w8,18
	mul	w15,w15,w8
	mov	w8,6
	madd	w15,w16,w8,w15
	add	w15,w15,w17
	str	s0,[x21,x15,lsl 2]
	add	w3,w3,1
	b	.rf
.rwn:	add	x16,x16,1
	b	.rw
.rbn:	add	x14,x14,1
	b	.rb
.rdone:	cbz	w28,.imdct			; court pur : pas d'anticrenelage
	ldr	x25,[x20,MP_MIXL]		; mixte : entre les sous-bandes longues
	mov	x0,18
	udiv	x25,x25,x0
	sub	x25,x25,1
	b	.al
.alias:	mov	x25,31
.al:	; anticrenelage entre sb-1 et sb, sb = 1..x25
	adr	x6,mp3_cs
	adr	x7,mp3_ca
	mov	x14,1
.as:	cmp	x14,x25
	b.hi	.imdct
	mov	x0,18
	mul	x0,x14,x0
	mov	x16,0
.ai:	sub	x1,x0,1
	sub	x1,x1,x16			; 18 sb - 1 - i
	add	x2,x0,x16			; 18 sb + i
	ldr	s0,[x21,x1,lsl 2]		; a
	ldr	s1,[x21,x2,lsl 2]		; b
	ldr	s2,[x6,x16,lsl 2]
	ldr	s3,[x7,x16,lsl 2]
	fmul	s4,s0,s2
	fmsub	s4,s1,s3,s4			; a cs - b ca
	fmul	s5,s1,s2
	fmadd	s5,s0,s3,s5			; b cs + a ca
	str	s4,[x21,x1,lsl 2]
	str	s5,[x21,x2,lsl 2]
	add	x16,x16,1
	cmp	x16,8
	b.lo	.ai
	add	x14,x14,1
	b	.as
.imdct:	; derniere sous-bande non nulle : au-dela, seul le recouvrement sort
	ldr	x0,[x20,MP_NZ]
	mov	x25,0				; sous-bande
.sb:	cmp	x25,32
	b.hs	.done
	mov	x0,18
	mul	x0,x25,x0
	add	x8,x21,x0,lsl 2			; entree (18)
	add	x15,x22,x0,lsl 2		; recouvrement (18)
	; genre de bloc de cette sous-bande
	mov	w0,w27
	cmp	w27,2
	b.ne	.bt
	cbz	w28,.bt
	ldr	x1,[x20,MP_MIXL]
	mov	x2,18
	mul	x2,x25,x2
	cmp	x2,x1
	b.hs	.bt
	mov	w0,0				; mixte : longues en fenetre normale
.bt:	cmp	w0,2
	b.eq	.short
	; longue : z[i] = somme X[k] c36[k][i] (NEON, 9 accumulateurs)
	movi	v16.4s,0
	movi	v17.4s,0
	movi	v18.4s,0
	movi	v19.4s,0
	movi	v20.4s,0
	movi	v21.4s,0
	movi	v22.4s,0
	movi	v23.4s,0
	movi	v24.4s,0
	adr	x1,mp3_imdct36
	mov	x2,0
.lk:	ldr	s0,[x8,x2,lsl 2]
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x1],64
	fmla	v16.4s,v1.4s,v0.s[0]
	fmla	v17.4s,v2.4s,v0.s[0]
	fmla	v18.4s,v3.4s,v0.s[0]
	fmla	v19.4s,v4.4s,v0.s[0]
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x1],64
	fmla	v20.4s,v1.4s,v0.s[0]
	fmla	v21.4s,v2.4s,v0.s[0]
	fmla	v22.4s,v3.4s,v0.s[0]
	fmla	v23.4s,v4.4s,v0.s[0]
	ld1	{v1.4s},[x1],16
	fmla	v24.4s,v1.4s,v0.s[0]
	add	x2,x2,1
	cmp	x2,18
	b.lo	.lk
	; fenetre du type de bloc
	adr	x1,mp3_win
	mov	x2,144
	madd	x1,x0,x2,x1
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x1],64
	fmul	v16.4s,v16.4s,v1.4s
	fmul	v17.4s,v17.4s,v2.4s
	fmul	v18.4s,v18.4s,v3.4s
	fmul	v19.4s,v19.4s,v4.4s
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x1],64
	fmul	v20.4s,v20.4s,v1.4s
	fmul	v21.4s,v21.4s,v2.4s
	fmul	v22.4s,v22.4s,v3.4s
	fmul	v23.4s,v23.4s,v4.4s
	ld1	{v1.4s},[x1]
	fmul	v24.4s,v24.4s,v1.4s
	st1	{v16.4s,v17.4s,v18.4s,v19.4s},[x24]
	add	x1,x24,64
	st1	{v20.4s,v21.4s,v22.4s,v23.4s},[x1]
	add	x1,x24,128
	st1	{v24.4s},[x1]
	b	.ovl
.short:	; courte : 3 IMDCT de 12, fenetrees, placees a 6 + 6 w
	movi	v0.4s,0
	mov	x1,x24
	mov	x2,9
.zs:	str	q0,[x1],16
	subs	x2,x2,1
	b.ne	.zs
	mov	x16,0				; fenetre
.swin:	movi	v16.4s,0
	movi	v17.4s,0
	movi	v18.4s,0
	adr	x1,mp3_imdct12
	mov	x2,0
	mov	x3,6
	mul	x3,x16,x3
	add	x3,x8,x3,lsl 2			; X de la fenetre
.sk:	ldr	s0,[x3,x2,lsl 2]
	ld1	{v1.4s,v2.4s,v3.4s},[x1],48
	fmla	v16.4s,v1.4s,v0.s[0]
	fmla	v17.4s,v2.4s,v0.s[0]
	fmla	v18.4s,v3.4s,v0.s[0]
	add	x2,x2,1
	cmp	x2,6
	b.lo	.sk
	adr	x1,mp3_win+2*144
	ld1	{v1.4s,v2.4s,v3.4s},[x1]
	fmul	v16.4s,v16.4s,v1.4s
	fmul	v17.4s,v17.4s,v2.4s
	fmul	v18.4s,v18.4s,v3.4s
	mov	x3,6
	mul	x3,x16,x3
	add	x3,x3,6
	add	x3,x24,x3,lsl 2			; z + 6 + 6 w
	ld1	{v1.4s,v2.4s,v3.4s},[x3]
	fadd	v1.4s,v1.4s,v16.4s
	fadd	v2.4s,v2.4s,v17.4s
	fadd	v3.4s,v3.4s,v18.4s
	st1	{v1.4s,v2.4s,v3.4s},[x3]
	add	x16,x16,1
	cmp	x16,3
	b.lo	.swin
.ovl:	; sortie = z[0..17] + recouvrement ; recouvrement = z[18..35]
	mov	x2,0
.ov:	ldr	s0,[x24,x2,lsl 2]
	ldr	s1,[x15,x2,lsl 2]
	fadd	s0,s0,s1
	add	x3,x2,18
	ldr	s1,[x24,x3,lsl 2]
	str	s1,[x15,x2,lsl 2]
	tbz	x25,0,.ni			; sous-bande impaire, echantillon impair :
	tbz	x2,0,.ni			; signe inverse
	fneg	s0,s0
.ni:	lsl	x3,x2,5				; SBS[t][sb]
	add	x3,x3,x25
	str	s0,[x23,x3,lsl 2]
	add	x2,x2,1
	cmp	x2,18
	b.lo	.ov
	add	x25,x25,1
	b	.sb
.done:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
	ret

; x0 = canal, x1 = granule : synthese polyphase de MP_SBS (18 tranches de 32
; sous-bandes) -> AKD_PCM[canal] + 576 * granule
; V[0..63] = N . S ; sortie[j] = somme_i V[...] D[32 i + j]
mp3_synth:
	stp	x29,x30,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	add	x2,x19,AKD_PCM
	ldr	x21,[x2,x0,lsl 3]
	mov	x2,2304
	madd	x21,x1,x2,x21			; sortie
	ldr	x22,[x20,MP_V]
	add	x22,x22,x0,lsl 12		; V du canal (1024 flottants)
	add	x25,x20,MP_VPOS
	add	x25,x25,x0,lsl 3
	ldr	x23,[x20,MP_SBS]		; tranche
	mov	x24,0
.t:	cmp	x24,18
	b.hs	.ret
	ldr	x26,[x25]			; position : V recule de 64
	sub	x26,x26,64
	and	x26,x26,1023
	str	x26,[x25]
	; V = N . S (16 accumulateurs : 64 valeurs)
	movi	v16.4s,0
	movi	v17.4s,0
	movi	v18.4s,0
	movi	v19.4s,0
	movi	v20.4s,0
	movi	v21.4s,0
	movi	v22.4s,0
	movi	v23.4s,0
	movi	v24.4s,0
	movi	v25.4s,0
	movi	v26.4s,0
	movi	v27.4s,0
	movi	v28.4s,0
	movi	v29.4s,0
	movi	v30.4s,0
	movi	v31.4s,0
	adr	x1,mp3_N
	mov	x2,0
.k:	ldr	s0,[x23,x2,lsl 2]
	fcmp	s0,0.0
	b.eq	.kz
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x1],64
	fmla	v16.4s,v1.4s,v0.s[0]
	fmla	v17.4s,v2.4s,v0.s[0]
	fmla	v18.4s,v3.4s,v0.s[0]
	fmla	v19.4s,v4.4s,v0.s[0]
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x1],64
	fmla	v20.4s,v1.4s,v0.s[0]
	fmla	v21.4s,v2.4s,v0.s[0]
	fmla	v22.4s,v3.4s,v0.s[0]
	fmla	v23.4s,v4.4s,v0.s[0]
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x1],64
	fmla	v24.4s,v1.4s,v0.s[0]
	fmla	v25.4s,v2.4s,v0.s[0]
	fmla	v26.4s,v3.4s,v0.s[0]
	fmla	v27.4s,v4.4s,v0.s[0]
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x1],64
	fmla	v28.4s,v1.4s,v0.s[0]
	fmla	v29.4s,v2.4s,v0.s[0]
	fmla	v30.4s,v3.4s,v0.s[0]
	fmla	v31.4s,v4.4s,v0.s[0]
	b	.kn
.kz:	add	x1,x1,256
.kn:	add	x2,x2,1
	cmp	x2,32
	b.lo	.k
	add	x1,x22,x26,lsl 2
	st1	{v16.4s,v17.4s,v18.4s,v19.4s},[x1],64
	st1	{v20.4s,v21.4s,v22.4s,v23.4s},[x1],64
	st1	{v24.4s,v25.4s,v26.4s,v27.4s},[x1],64
	st1	{v28.4s,v29.4s,v30.4s,v31.4s},[x1]
	; fenetre : 16 tranches de 32 V, a 128 m (i pair) ou 128 m + 96 (impair)
	movi	v16.4s,0
	movi	v17.4s,0
	movi	v18.4s,0
	movi	v19.4s,0
	movi	v20.4s,0
	movi	v21.4s,0
	movi	v22.4s,0
	movi	v23.4s,0
	adr	x3,mp3_D
	mov	x2,0				; i
.w:	lsr	x4,x2,1
	lsl	x4,x4,7
	tbz	x2,0,.we
	add	x4,x4,96
.we:	add	x4,x4,x26
	and	x4,x4,1023
	add	x4,x22,x4,lsl 2
	ld1	{v0.4s,v1.4s,v2.4s,v3.4s},[x4],64
	ld1	{v4.4s,v5.4s,v6.4s,v7.4s},[x3],64
	fmla	v16.4s,v0.4s,v4.4s
	fmla	v17.4s,v1.4s,v5.4s
	fmla	v18.4s,v2.4s,v6.4s
	fmla	v19.4s,v3.4s,v7.4s
	ld1	{v0.4s,v1.4s,v2.4s,v3.4s},[x4]
	ld1	{v4.4s,v5.4s,v6.4s,v7.4s},[x3],64
	fmla	v20.4s,v0.4s,v4.4s
	fmla	v21.4s,v1.4s,v5.4s
	fmla	v22.4s,v2.4s,v6.4s
	fmla	v23.4s,v3.4s,v7.4s
	add	x2,x2,1
	cmp	x2,16
	b.lo	.w
	st1	{v16.4s,v17.4s,v18.4s,v19.4s},[x21],64
	st1	{v20.4s,v21.4s,v22.4s,v23.4s},[x21],64
	add	x23,x23,128
	add	x24,x24,1
	b	.t
.ret:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
	ret

		align	4
mp3_rates	dw	44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000
mp3_brtab	dh	0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
		dh	0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
		align	4
include 'ak_mp3tab.inc'
		align	4
