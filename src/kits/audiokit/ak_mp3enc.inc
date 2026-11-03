; ============================================================================
;  kits/audiokit/ak_mp3enc.inc - encodeur MP3 : MPEG-1 couche III (ISO/IEC
;  11172-3), MPEG-2 et 2.5 (13818-3, frequences moitie), debit constant
;
;  Banc d'analyse polyphase (fenetre C[i] = D[i] / 32 de la norme, matrice
;  cos((2k + 1)(i - 16) pi / 64)), MDCT de 36 points par sous-bande (blocs
;  longs seulement), inversion de frequence, papillons d'anticrenelage
;  inverses. Stereo : gauche/droite ou M/S (stereo jointe) par trame.
;  Pas de modele psychoacoustique : les facteurs d'echelle mettent le
;  bruit en forme d'apres l'energie des bandes (une bande r fois plus
;  faible que la plus forte gagne alpha log2 r / 2 en precision, sans
;  boucle) ; global_gain commun aux canaux du granule, cherche par
;  dichotomie pour tenir dans le budget de bits ; une cible de global_gain
;  (bruit constant) et le reservoir de bits (main_data_begin jusqu'a
;  511 octets, 255 en LSF) reportent les bits des granules faciles vers
;  les difficiles (attaques). Huffman : big_values en trois regions
;  (toutes les coupes essayees, meilleure table par region), quadruplets
;  (table A ou B). Bourrage (padding) pour le debit exact ; passe-bas
;  selon le debit par canal. Fin : zeros pour sortir le retard du banc
;  et de la MDCT (1057 echantillons au decodage). Pas d'en-tete Xing.
;  Tables de codage : ak_mp3tab.inc (mp3e_*, gen_mp3tab.py).
;  Debits : MPEG-1 32..320 kbit/s (32, 44,1, 48 kHz) ; MPEG-2 et 2.5
;  8..160 kbit/s (16, 22,05, 24 ; 8, 11,025, 12 kHz) ; mono ou stereo.
; ============================================================================

; --- etat ---------------------------------------------------------------------
ME_NCH		= 0
ME_NGR		= 8		; granules par trame (2 ; LSF : 1)
ME_SPF		= 16		; echantillons par trame (1152 / 576)
ME_SRI		= 24		; frequence : indice 0..8 des tables
ME_LSF		= 32
ME_HDR		= 40		; en-tete (sans bourrage ni stereo jointe)
ME_NUM		= 48		; 144 (72) x debit : octets d'une trame x frequence
ME_RATE		= 56
ME_ACC		= 64		; reste cumule (bourrage)
ME_RMAX		= 72		; reservoir maximal (octets)
ME_SIDE		= 80		; octets d'informations de cote
ME_FILL		= 88		; echantillons de la trame en cours
ME_LIM		= 96		; lignes gardees (passe-bas)
ME_T		= 104		; cible de global_gain (flottant 32 bits)
ME_CPOS		= 112		; flux principal : debut du creneau de la trame
ME_PPOS		= 120		; debut des donnees de la trame
ME_BASE		= 128		; position du premier octet de ME_MSB
ME_NPEND	= 136		; trames en attente
ME_MS		= 144		; trame en M/S
ME_PCM		= 152		; tableaux (pointeurs)
ME_CR		= 160
ME_MT		= 168
ME_WM		= 176
ME_SBH		= 184
ME_XR		= 192
ME_X34		= 200
ME_IX		= 208
ME_STEP		= 216
ME_MSB		= 224
ME_PEND		= 232
ME_ZERO		= 240
ME_SFB		= 248		; debuts des bandes longues de la frequence
ME_WK		= 256		; travail du comptage
ME_USED		= 264		; bits de la trame deja pris
ME_AVAIL	= 272		; bits disponibles (reservoir + creneau)
ME_MEANGR	= 280		; part d'un granule
ME_WACC		= 288		; ecrivain de bits : accumulateur, bits, pointeur
ME_WN		= 296
ME_WPTR		= 304
ME_GI		= 512		; informations des granules [gr][canal] x 128 o
ME_SIZE		= 1024

; informations d'un granule (32 bits)
ME_GBITS	= 0		; part2_3_length
ME_GBV		= 4		; big_values
ME_GGG		= 8		; global_gain
ME_GT0		= 12		; table_select[3]
ME_GT1		= 16
ME_GT2		= 20
ME_GR0		= 24		; region0_count, region1_count
ME_GR1		= 28
ME_GC1T		= 32		; count1table_select
ME_GC1		= 36		; quadruplets
ME_GP2		= 40		; bits des facteurs d'echelle (part2)
ME_GSFC		= 44		; scalefac_compress
ME_GSF		= 48		; facteurs d'echelle des bandes 0..20 (octets)
ME_GSL		= 72		; bits de chacun (octets)

; tableaux (decalages dans le bloc)
ME_PCMCH	= 6528		; par canal : 480 d'historique + 1152 (flottants)
ME_XRG		= 2368		; spectre d'un [gr][canal] (576 + 16 flottants)
ME_A_PCM	= 0
ME_A_CR		= 13056		; fenetre d'analyse (512)
ME_A_MT		= 15104		; matrice d'analyse (64 x 32)
ME_A_WM		= 23296		; MDCT (36 x 20)
ME_A_SBH	= 26176		; sous-bandes [canal][32][36]
ME_A_XR		= 35392		; [gr][canal] x ME_XRG
ME_A_X34	= 44864		; |xr|^3/4 [canal] x ME_XRG
ME_A_IX		= 49600		; valeurs quantifiees [canal] x 576 (32 bits)
ME_A_STEP	= 54208		; 2^(-3/16 (g - 210)), g = 0..255
ME_A_MSB	= 55232		; flux principal (ME_MSBN)
ME_A_PEND	= 63424		; trames en attente (ME_PENDN x 64)
ME_A_ZERO	= 65472		; 576 trames stereo nulles (fin)
ME_A_WK		= 67776		; travail (2048)
ME_ARRAYS	= 69824
ME_MSBN		= 8192
ME_PENDN	= 32

; travail du comptage
ME_WK_E		= 1472		; nombre de valeurs >= 15, cumule par segment
ME_WK_M		= 1568		; maximum par segment
ME_WK_B0	= 1664		; region 0 [0, a) : cout, table
ME_WK_B2	= 1808		; region 2 [c, fin) : cout, table

ME_INF		= 1 shl 30

; ecrit n bits (registres) : x12 accumulateur, x13 bits en attente, x14 sortie
macro me_put v*, n* {
	lsl	x12,x12,n
	orr	x12,x12,v
	add	x13,x13,n
	bl	mp3e_flushw
}

; ---------------------------------------------------------------------------
;  Ouverture
; ---------------------------------------------------------------------------

; x0 = encodeur -> 0 / < 0
mp3_enc_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x20,x0
	; frequence
	ldr	x0,[x20,AKN_RATE]
	adr	x1,mp3e_rates
	mov	x21,0
.r:	ldr	w2,[x1,x21,lsl 2]
	cmp	x2,x0
	b.eq	.rf
	add	x21,x21,1
	cmp	x21,9
	b.lo	.r
	b	.fmt
.rf:	ldr	x0,[x20,AKN_CH]
	sub	x0,x0,1
	cmp	x0,1
	b.hi	.fmt
	; debit (exact, dans la table de la version)
	ldr	x22,[x20,AKN_QUALITY]
	cbnz	x22,.q
	movl	x22,128000
.q:	mov	x0,1000
	udiv	x24,x22,x0
	msub	x2,x24,x0,x22
	cbnz	x2,.fmt
	adr	x2,mp3e_br1
	cmp	x21,3
	b.lo	.br
	adr	x2,mp3e_br2
.br:	mov	x23,1
.bk:	ldrh	w3,[x2,x23,lsl 1]
	cmp	x3,x24
	b.eq	.bf
	add	x23,x23,1
	cmp	x23,15
	b.lo	.bk
	b	.fmt
.bf:	mov	x0,x20
	movl	x1,ME_SIZE+ME_ARRAYS
	bl	akn_alloc
	cbz	x0,.nomem
	mov	x19,x0
	str	x19,[x20,AKN_CODEC]
	; tableaux
	add	x0,x19,ME_SIZE
	str	x0,[x19,ME_PCM]
	movl	x1,ME_A_CR
	add	x1,x0,x1
	str	x1,[x19,ME_CR]
	movl	x1,ME_A_MT
	add	x1,x0,x1
	str	x1,[x19,ME_MT]
	movl	x1,ME_A_WM
	add	x1,x0,x1
	str	x1,[x19,ME_WM]
	movl	x1,ME_A_SBH
	add	x1,x0,x1
	str	x1,[x19,ME_SBH]
	movl	x1,ME_A_XR
	add	x1,x0,x1
	str	x1,[x19,ME_XR]
	movl	x1,ME_A_X34
	add	x1,x0,x1
	str	x1,[x19,ME_X34]
	movl	x1,ME_A_IX
	add	x1,x0,x1
	str	x1,[x19,ME_IX]
	movl	x1,ME_A_STEP
	add	x1,x0,x1
	str	x1,[x19,ME_STEP]
	movl	x1,ME_A_MSB
	add	x1,x0,x1
	str	x1,[x19,ME_MSB]
	movl	x1,ME_A_PEND
	add	x1,x0,x1
	str	x1,[x19,ME_PEND]
	movl	x1,ME_A_ZERO
	add	x1,x0,x1
	str	x1,[x19,ME_ZERO]
	movl	x1,ME_A_WK
	add	x1,x0,x1
	str	x1,[x19,ME_WK]
	; parametres
	ldr	x25,[x20,AKN_CH]
	str	x25,[x19,ME_NCH]
	ldr	x0,[x20,AKN_RATE]
	str	x0,[x19,ME_RATE]
	str	x21,[x19,ME_SRI]
	cmp	x21,3
	cset	x26,hs				; LSF (MPEG-2, 2.5)
	str	x26,[x19,ME_LSF]
	mov	x0,2
	sub	x0,x0,x26
	str	x0,[x19,ME_NGR]
	mov	x1,576
	mul	x0,x0,x1
	str	x0,[x19,ME_SPF]
	adr	x0,mp3_sfb_long
	mov	x1,48
	madd	x0,x21,x1,x0
	str	x0,[x19,ME_SFB]
	mov	x0,144
	lsr	x0,x0,x26
	mul	x0,x0,x22
	str	x0,[x19,ME_NUM]
	; informations de cote : 17 / 32 octets ; LSF : 9 / 17
	mov	x0,17
	mov	x1,32
	cmp	x25,1
	csel	x0,x0,x1,eq
	mov	x1,9
	mov	x2,17
	cmp	x25,1
	csel	x1,x1,x2,eq
	cmp	x26,0
	csel	x0,x0,x1,eq
	str	x0,[x19,ME_SIDE]
	; en-tete : synchro, version, couche III, sans CRC, debit, frequence,
	; original ; mono
	mov	x0,3				; MPEG-1
	cmp	x21,6
	mov	x1,0				; 2.5
	csel	x0,x1,x0,hs
	cmp	x21,3
	b.lo	.v
	cmp	x21,6
	b.hs	.v
	mov	x0,2				; MPEG-2
.v:	movl	x1,0xFFE30004
	orr	x1,x1,x0,lsl 19
	orr	x1,x1,x23,lsl 12
	mov	x2,3
	udiv	x3,x21,x2
	msub	x3,x3,x2,x21
	orr	x1,x1,x3,lsl 10
	cmp	x25,1
	b.ne	.hd
	orr	x1,x1,0xC0			; mono
.hd:	str	x1,[x19,ME_HDR]
	; reservoir : 511 (LSF 255) octets, au plus 16 creneaux minimaux
	ldr	x0,[x19,ME_NUM]
	ldr	x1,[x19,ME_RATE]
	udiv	x0,x0,x1
	ldr	x1,[x19,ME_SIDE]
	sub	x0,x0,x1
	subs	x0,x0,4
	b.le	.fmt
	lsl	x0,x0,4
	mov	x1,511
	cmp	x26,0
	mov	x2,255
	csel	x1,x1,x2,eq
	cmp	x0,x1
	csel	x0,x0,x1,lo
	str	x0,[x19,ME_RMAX]
	; passe-bas selon le debit par canal
	udiv	x0,x24,x25
	adr	x1,mp3e_lp
.lp:	ldrh	w2,[x1],4
	cmp	x0,x2
	b.lo	.lp
	ldrh	w2,[x1,-2]
	mov	x3,1152
	mul	x2,x2,x3
	ldr	x3,[x19,ME_RATE]
	udiv	x2,x2,x3
	mov	x3,576
	cmp	x2,x3
	csel	x2,x2,x3,lo
	str	x2,[x19,ME_LIM]
	adr	x0,mp3e_kf
	ldr	w1,[x0,8]			; cible de depart
	str	w1,[x19,ME_T]
	bl	mp3e_tables
	adr	x0,mp3_enc_push
	str	x0,[x20,AKN_PUSH]
	adr	x0,mp3_enc_finish
	str	x0,[x20,AKN_FINISH]
	mov	x0,0
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.fmt:	mov	x0,AKE_FORMAT
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x19 = etat : fenetre, matrices, pas de quantification
mp3e_tables:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	; C inversee : Cr[m] = D[511 - m] / 32 / 32768
	adr	x0,mp3_D+511*4
	ldr	x1,[x19,ME_CR]
	adr	x2,mp3e_kf
	ldr	s1,[x2,12]			; 2^-20
	mov	x3,512
.c:	ldr	s0,[x0],-4
	fmul	s0,s0,s1
	str	s0,[x1],4
	subs	x3,x3,1
	b.ne	.c
	; analyse : Mt[i][k] = cos((2k + 1)(47 - i) pi / 64), i = 0..63, k = 0..31
	adr	x0,mp3e_kd
	ldr	d8,[x0]				; pi / 64
	ldr	x21,[x19,ME_MT]
	mov	x22,0				; i
.mi:	mov	x23,0				; k
.mk:	lsl	x0,x23,1
	add	x0,x0,1
	mov	x1,47
	sub	x1,x1,x22
	mul	x0,x0,x1
	scvtf	d0,x0
	fmul	d0,d0,d8
	bl	ak_sincos
	fcvt	s1,d1
	str	s1,[x21],4
	add	x23,x23,1
	cmp	x23,32
	b.lo	.mk
	add	x22,x22,1
	cmp	x22,64
	b.lo	.mi
	; MDCT : W[n][k] = sin(pi/36 (n + 1/2)) cos(pi/72 (2n + 19)(2k + 1)) / 9
	; (k = 0..17, 20 par ligne)
	adr	x0,mp3e_kd
	ldr	d9,[x0,8]			; pi / 36
	ldr	d10,[x0,16]			; pi / 72
	ldr	d11,[x0,24]			; 1 / 9
	ldr	x21,[x19,ME_WM]
	mov	x22,0				; n
.wn:	scvtf	d0,x22
	fmov	d1,0.5
	fadd	d0,d0,d1
	fmul	d0,d0,d9
	bl	ak_sincos
	fmul	d8,d0,d11			; fenetre / 9
	mov	x23,0				; k
.wk:	lsl	x0,x22,1
	add	x0,x0,19
	lsl	x1,x23,1
	add	x1,x1,1
	mul	x0,x0,x1
	scvtf	d0,x0
	fmul	d0,d0,d10
	bl	ak_sincos
	fmul	d1,d1,d8
	fcvt	s1,d1
	str	s1,[x21,x23,lsl 2]
	add	x23,x23,1
	cmp	x23,18
	b.lo	.wk
	add	x21,x21,80
	add	x22,x22,1
	cmp	x22,36
	b.lo	.wn
	; pas : 2^(-3/16 (g - 210))
	adr	x0,mp3e_kd
	ldr	d0,[x0,32]			; 2^39.375
	ldr	d1,[x0,40]			; 2^-0.1875
	ldr	x1,[x19,ME_STEP]
	mov	x2,256
.sto:	fcvt	s2,d0
	str	s2,[x1],4
	fmul	d0,d0,d1
	subs	x2,x2,1
	b.ne	.sto
	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Entree
; ---------------------------------------------------------------------------

; x0 = encodeur, x1 = PCM s16 entrelace, x2 = trames -> 0 / < 0
mp3_enc_push:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0
	ldr	x19,[x20,AKN_CODEC]
	mov	x21,x1
	mov	x22,x2
.l:	cbz	x22,.ok
	ldr	x0,[x19,ME_SPF]
	ldr	x1,[x19,ME_FILL]
	sub	x0,x0,x1
	cmp	x22,x0
	csel	x23,x22,x0,lo			; trames prises
	ldr	x2,[x19,ME_PCM]
	add	x2,x2,480*4
	add	x2,x2,x1,lsl 2
	mov	x4,x23
	ldr	x3,[x19,ME_NCH]
	cmp	x3,1
	b.ne	.sto
.mo:	ldrsh	w5,[x21],2
	scvtf	s0,w5
	str	s0,[x2],4
	subs	x4,x4,1
	b.ne	.mo
	b	.cd
.sto:	movl	x6,ME_PCMCH
	add	x6,x2,x6
.sl:	ldrsh	w5,[x21],2
	scvtf	s0,w5
	str	s0,[x2],4
	ldrsh	w5,[x21],2
	scvtf	s0,w5
	str	s0,[x6],4
	subs	x4,x4,1
	b.ne	.sl
.cd:	ldr	x1,[x19,ME_FILL]
	add	x1,x1,x23
	str	x1,[x19,ME_FILL]
	sub	x22,x22,x23
	ldr	x0,[x19,ME_SPF]
	cmp	x1,x0
	b.lo	.l
	mov	x0,x20
	bl	mp3e_frame
	tbnz	x0,63,.out
	str	xzr,[x19,ME_FILL]
	; historique : les 480 derniers echantillons de chaque canal
	mov	x24,0
.h:	ldr	x0,[x19,ME_PCM]
	movl	x1,ME_PCMCH
	madd	x0,x24,x1,x0
	ldr	x1,[x19,ME_SPF]
	add	x1,x0,x1,lsl 2
	mov	x2,480*4
	bl	ak_memmove
	add	x24,x24,1
	ldr	x0,[x19,ME_NCH]
	cmp	x24,x0
	b.lo	.h
	b	.l
.ok:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur : complete la trame par des zeros (et le retard du banc et
; de la MDCT), vide le reservoir -> 0 / < 0
mp3_enc_finish:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x0
	ldr	x19,[x20,AKN_CODEC]
	ldr	x0,[x19,ME_SPF]
	ldr	x1,[x19,ME_FILL]
	sub	x21,x0,x1
	udiv	x2,x21,x0
	msub	x21,x2,x0,x21
	add	x21,x21,1152
.z:	cbz	x21,.e
	mov	x22,576
	cmp	x21,x22
	csel	x22,x21,x22,lo
	mov	x0,x20
	ldr	x1,[x19,ME_ZERO]
	mov	x2,x22
	bl	mp3_enc_push
	tbnz	x0,63,.out
	sub	x21,x21,x22
	b	.z
.e:	mov	x0,x20
	mov	x1,1
	bl	mp3e_emit
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Trame
; ---------------------------------------------------------------------------

; x0 = encodeur : encode la trame de ME_PCM -> 0 / < 0
mp3e_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x28,x0
	ldr	x19,[x28,AKN_CODEC]
	; analyse : sous-bandes et MDCT de chaque granule et canal
	mov	x23,0
.ag:	mov	x24,0
.ac:	mov	x0,x19
	mov	x1,x24
	mov	x2,x23
	bl	mp3e_analyze
	add	x24,x24,1
	ldr	x0,[x19,ME_NCH]
	cmp	x24,x0
	b.lo	.ac
	add	x23,x23,1
	ldr	x0,[x19,ME_NGR]
	cmp	x23,x0
	b.lo	.ag
	; stereo
	str	xzr,[x19,ME_MS]
	ldr	x0,[x19,ME_NCH]
	cmp	x0,2
	b.ne	.lp
	mov	x0,x19
	bl	mp3e_stereo
	; passe-bas
.lp:	mov	x23,0
.lz:	ldr	x0,[x19,ME_XR]
	mov	x1,ME_XRG
	madd	x0,x23,x1,x0
	ldr	x1,[x19,ME_LIM]
	add	x0,x0,x1,lsl 2
	mov	x2,576
	sub	x1,x2,x1
	lsl	x1,x1,2
	bl	ak_memzero
	add	x23,x23,1
	cmp	x23,4
	b.lo	.lz
	; taille de la trame (bourrage), creneau des donnees principales
	ldr	x0,[x19,ME_NUM]
	ldr	x1,[x19,ME_RATE]
	udiv	x2,x0,x1
	msub	x3,x2,x1,x0
	ldr	x4,[x19,ME_ACC]
	add	x4,x4,x3
	mov	x20,0
	cmp	x4,x1
	b.lo	.np
	sub	x4,x4,x1
	mov	x20,1				; bourrage
.np:	str	x4,[x19,ME_ACC]
	add	x2,x2,x20
	ldr	x3,[x19,ME_SIDE]
	sub	x21,x2,x3
	sub	x21,x21,4			; creneau (octets)
	ldr	x0,[x19,ME_CPOS]
	ldr	x1,[x19,ME_PPOS]
	sub	x22,x0,x1			; main_data_begin
	add	x0,x22,x21
	lsl	x0,x0,3
	str	x0,[x19,ME_AVAIL]
	str	xzr,[x19,ME_USED]
	lsl	x0,x21,3
	ldr	x1,[x19,ME_NGR]
	udiv	x0,x0,x1
	str	x0,[x19,ME_MEANGR]
	; ecrivain : au debut des donnees
	ldr	x0,[x19,ME_MSB]
	ldr	x1,[x19,ME_PPOS]
	ldr	x2,[x19,ME_BASE]
	sub	x1,x1,x2
	add	x0,x0,x1
	str	x0,[x19,ME_WPTR]
	str	xzr,[x19,ME_WACC]
	str	xzr,[x19,ME_WN]
	; granules
	mov	x23,0
.g:	mov	x0,x19
	mov	x1,x23
	bl	mp3e_granule
	add	x23,x23,1
	ldr	x0,[x19,ME_NGR]
	cmp	x23,x0
	b.lo	.g
	; dernier octet
	ldr	x12,[x19,ME_WACC]
	ldr	x13,[x19,ME_WN]
	ldr	x14,[x19,ME_WPTR]
	cbz	x13,.fl
	mov	x0,8
	sub	x0,x0,x13
	lsl	x12,x12,x0
	strb	w12,[x14],1
.fl:	ldr	x0,[x19,ME_MSB]
	sub	x24,x14,x0
	ldr	x1,[x19,ME_BASE]
	add	x24,x24,x1			; fin des donnees (position)
	; cible : le reservoir se remplit -> pas plus fin
	ldr	x0,[x19,ME_AVAIL]
	ldr	x1,[x19,ME_USED]
	sub	x0,x0,x1
	scvtf	s0,x0
	ldr	x1,[x19,ME_RMAX]
	lsl	x1,x1,3
	scvtf	s1,x1
	fdiv	s0,s0,s1
	fmov	s1,0.5
	fcmp	s0,s1
	b.le	.tf
	fsub	s0,s0,s1
	fadd	s0,s0,s0
	ldr	s2,[x19,ME_T]
	fsub	s2,s2,s0
	fmov	s3,wzr
	fmax	s2,s2,s3			; (silence : pas sous 0)
	str	s2,[x19,ME_T]
.tf:	; trame en attente : en-tete, informations de cote
	ldr	x0,[x19,ME_NPEND]
	ldr	x25,[x19,ME_PEND]
	add	x25,x25,x0,lsl 6
	add	x0,x0,1
	str	x0,[x19,ME_NPEND]
	ldr	w0,[x19,ME_HDR]
	orr	w0,w0,w20,lsl 9
	ldr	x1,[x19,ME_MS]
	cbz	x1,.nms
	orr	w0,w0,0x60			; stereo jointe, M/S
.nms:	rev	w0,w0
	str	w0,[x25]
	mov	x0,x19
	add	x1,x25,4
	mov	x2,x22
	bl	mp3e_side
	ldr	x0,[x19,ME_CPOS]
	str	x0,[x25,40]
	str	x21,[x25,48]
	ldr	x1,[x19,ME_SIDE]
	add	x1,x1,4
	str	x1,[x25,56]
	; trame suivante : creneau, debut des donnees (reservoir <= ME_RMAX)
	add	x0,x0,x21
	str	x0,[x19,ME_CPOS]
	ldr	x1,[x19,ME_RMAX]
	sub	x1,x0,x1
	cmp	x24,x1
	csel	x1,x24,x1,gt
	str	x1,[x19,ME_PPOS]
	mov	x0,x28
	mov	x1,0
	bl	mp3e_emit
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur, x1 = 1 : tout -> ecrit les trames dont le creneau est
; complet (avant ME_PPOS), compacte le flux principal -> 0 / < 0
mp3e_emit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0
	ldr	x19,[x20,AKN_CODEC]
	mov	x21,x1
	mov	x22,0
.e:	ldr	x0,[x19,ME_NPEND]
	cmp	x22,x0
	b.hs	.ed
	ldr	x23,[x19,ME_PEND]
	add	x23,x23,x22,lsl 6
	ldr	x0,[x23,40]
	ldr	x1,[x23,48]
	add	x0,x0,x1
	cbnz	x21,.go
	ldr	x2,[x19,ME_PPOS]
	cmp	x0,x2
	b.hi	.ed
.go:	mov	x0,x20
	mov	x1,x23
	ldr	x2,[x23,56]
	bl	akn_out
	tbnz	x0,63,.out
	mov	x0,x20
	ldr	x1,[x19,ME_MSB]
	ldr	x2,[x23,40]
	ldr	x3,[x19,ME_BASE]
	sub	x2,x2,x3
	add	x1,x1,x2
	ldr	x2,[x23,48]
	bl	akn_out
	tbnz	x0,63,.out
	add	x22,x22,1
	b	.e
.ed:	cbz	x22,.cp
	ldr	x0,[x19,ME_NPEND]
	sub	x24,x0,x22
	str	x24,[x19,ME_NPEND]
	ldr	x0,[x19,ME_PEND]
	add	x1,x0,x22,lsl 6
	lsl	x2,x24,6
	bl	ak_memmove
.cp:	cbnz	x21,.ok
	; nouvelle base : premier creneau en attente (sinon ME_PPOS)
	ldr	x0,[x19,ME_NPEND]
	ldr	x1,[x19,ME_PPOS]
	cbz	x0,.nb
	ldr	x2,[x19,ME_PEND]
	ldr	x1,[x2,40]
.nb:	ldr	x2,[x19,ME_BASE]
	subs	x3,x1,x2
	b.eq	.ok
	str	x1,[x19,ME_BASE]
	ldr	x0,[x19,ME_MSB]
	add	x1,x0,x3
	ldr	x2,[x19,ME_PPOS]
	ldr	x4,[x19,ME_BASE]
	sub	x2,x2,x4
	mov	x23,x2
	bl	ak_memmove
	ldr	x0,[x19,ME_MSB]
	add	x0,x0,x23
	mov	x1,ME_MSBN
	sub	x1,x1,x23
	bl	ak_memzero
.ok:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat, x1 = sortie, x2 = main_data_begin : informations de cote
mp3e_side:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x14,x1
	mov	x12,0
	mov	x13,0
	ldr	x20,[x19,ME_LSF]
	ldr	x21,[x19,ME_NCH]
	mov	x0,x2
	cbnz	x20,.l
	mov	x1,9
	bl	mp3e_putc
	mov	x0,0				; private_bits
	mov	x1,3
	mov	x2,5
	cmp	x21,1
	csel	x1,x2,x1,eq
	bl	mp3e_putc
	mov	x0,0				; scfsi
	lsl	x1,x21,2
	bl	mp3e_putc
	b	.g
.l:	mov	x1,8
	bl	mp3e_putc
	mov	x0,0
	mov	x1,x21
	bl	mp3e_putc
.g:	mov	x22,0
.gl:	mov	x23,0
.cl:	add	x24,x19,ME_GI
	add	x0,x23,x22,lsl 1
	add	x24,x24,x0,lsl 7
	ldr	w0,[x24,ME_GBITS]
	mov	x1,12
	bl	mp3e_putc
	ldr	w0,[x24,ME_GBV]
	mov	x1,9
	bl	mp3e_putc
	ldr	w0,[x24,ME_GGG]
	mov	x1,8
	bl	mp3e_putc
	ldr	w0,[x24,ME_GSFC]
	mov	x1,4
	cbz	x20,.sf
	mov	x1,9
.sf:	bl	mp3e_putc
	mov	x0,0				; window_switching_flag
	mov	x1,1
	bl	mp3e_putc
	ldr	w0,[x24,ME_GT0]
	mov	x1,5
	bl	mp3e_putc
	ldr	w0,[x24,ME_GT1]
	mov	x1,5
	bl	mp3e_putc
	ldr	w0,[x24,ME_GT2]
	mov	x1,5
	bl	mp3e_putc
	ldr	w0,[x24,ME_GR0]
	mov	x1,4
	bl	mp3e_putc
	ldr	w0,[x24,ME_GR1]
	mov	x1,3
	bl	mp3e_putc
	cbnz	x20,.np
	mov	x0,0				; preflag
	mov	x1,1
	bl	mp3e_putc
.np:	mov	x0,0				; scalefac_scale
	mov	x1,1
	bl	mp3e_putc
	ldr	w0,[x24,ME_GC1T]
	mov	x1,1
	bl	mp3e_putc
	add	x23,x23,1
	cmp	x23,x21
	b.lo	.cl
	add	x22,x22,1
	ldr	x0,[x19,ME_NGR]
	cmp	x22,x0
	b.lo	.gl
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur, x1 = bits (ecrivain x12-x14 ; detruit x15)
mp3e_putc:
	lsl	x12,x12,x1
	orr	x12,x12,x0
	add	x13,x13,x1
; ecrit les octets complets de l'accumulateur
mp3e_flushw:
.l:	cmp	x13,8
	b.lo	.r
	sub	x13,x13,8
	lsr	x15,x12,x13
	strb	w15,[x14],1
	b	.l
.r:	ret

; ---------------------------------------------------------------------------
;  Analyse
; ---------------------------------------------------------------------------

; x0 = etat, x1 = canal, x2 = granule : 576 echantillons -> ME_XR[gr][canal]
mp3e_analyze:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,384			; Y inverse (64), S (32)
	mov	x19,x0
	ldr	x20,[x19,ME_PCM]
	movl	x3,ME_PCMCH
	madd	x20,x1,x3,x20
	mov	x3,576*4
	madd	x20,x2,x3,x20			; echantillons du granule (480 avant)
	ldr	x21,[x19,ME_SBH]
	mov	x3,32*36*4
	madd	x21,x1,x3,x21
	ldr	x22,[x19,ME_XR]
	add	x3,x1,x2,lsl 1
	mov	x4,ME_XRG
	madd	x22,x3,x4,x22
	mov	x23,0				; t : 18 pas de 32 echantillons
.t:	; Y'[i] = somme_j Cr[64 j + i] s[p + 64 j + i] (= Y[63 - i])
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
	add	x1,x20,x23,lsl 7
	ldr	x2,[x19,ME_CR]
	mov	x3,8
.j:	ld1	{v0.4s,v1.4s,v2.4s,v3.4s},[x2],64
	ld1	{v4.4s,v5.4s,v6.4s,v7.4s},[x1],64
	fmla	v16.4s,v0.4s,v4.4s
	fmla	v17.4s,v1.4s,v5.4s
	fmla	v18.4s,v2.4s,v6.4s
	fmla	v19.4s,v3.4s,v7.4s
	ld1	{v0.4s,v1.4s,v2.4s,v3.4s},[x2],64
	ld1	{v4.4s,v5.4s,v6.4s,v7.4s},[x1],64
	fmla	v20.4s,v0.4s,v4.4s
	fmla	v21.4s,v1.4s,v5.4s
	fmla	v22.4s,v2.4s,v6.4s
	fmla	v23.4s,v3.4s,v7.4s
	ld1	{v0.4s,v1.4s,v2.4s,v3.4s},[x2],64
	ld1	{v4.4s,v5.4s,v6.4s,v7.4s},[x1],64
	fmla	v24.4s,v0.4s,v4.4s
	fmla	v25.4s,v1.4s,v5.4s
	fmla	v26.4s,v2.4s,v6.4s
	fmla	v27.4s,v3.4s,v7.4s
	ld1	{v0.4s,v1.4s,v2.4s,v3.4s},[x2],64
	ld1	{v4.4s,v5.4s,v6.4s,v7.4s},[x1],64
	fmla	v28.4s,v0.4s,v4.4s
	fmla	v29.4s,v1.4s,v5.4s
	fmla	v30.4s,v2.4s,v6.4s
	fmla	v31.4s,v3.4s,v7.4s
	subs	x3,x3,1
	b.ne	.j
	mov	x4,sp
	st1	{v16.4s,v17.4s,v18.4s,v19.4s},[x4],64
	st1	{v20.4s,v21.4s,v22.4s,v23.4s},[x4],64
	st1	{v24.4s,v25.4s,v26.4s,v27.4s},[x4],64
	st1	{v28.4s,v29.4s,v30.4s,v31.4s},[x4]
	; S[k] = somme_i Mt[i][k] Y'[i]
	movi	v16.4s,0
	movi	v17.4s,0
	movi	v18.4s,0
	movi	v19.4s,0
	movi	v20.4s,0
	movi	v21.4s,0
	movi	v22.4s,0
	movi	v23.4s,0
	ldr	x2,[x19,ME_MT]
	mov	x4,sp
	mov	x3,64
.i:	ldr	s0,[x4],4
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x2],64
	fmla	v16.4s,v1.4s,v0.s[0]
	fmla	v17.4s,v2.4s,v0.s[0]
	fmla	v18.4s,v3.4s,v0.s[0]
	fmla	v19.4s,v4.4s,v0.s[0]
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x2],64
	fmla	v20.4s,v1.4s,v0.s[0]
	fmla	v21.4s,v2.4s,v0.s[0]
	fmla	v22.4s,v3.4s,v0.s[0]
	fmla	v23.4s,v4.4s,v0.s[0]
	subs	x3,x3,1
	b.ne	.i
	; inversion de frequence : sous-bandes impaires, pas impairs
	tbz	x23,0,.ev
	adr	x0,mp3e_sgn
	ld1	{v0.4s},[x0]
	fmul	v16.4s,v16.4s,v0.4s
	fmul	v17.4s,v17.4s,v0.4s
	fmul	v18.4s,v18.4s,v0.4s
	fmul	v19.4s,v19.4s,v0.4s
	fmul	v20.4s,v20.4s,v0.4s
	fmul	v21.4s,v21.4s,v0.4s
	fmul	v22.4s,v22.4s,v0.4s
	fmul	v23.4s,v23.4s,v0.4s
.ev:	add	x4,sp,256
	st1	{v16.4s,v17.4s,v18.4s,v19.4s},[x4],64
	st1	{v20.4s,v21.4s,v22.4s,v23.4s},[x4]
	; sous-bande k, echantillon 18 + t
	add	x5,x21,18*4
	add	x5,x5,x23,lsl 2
	add	x4,sp,256
	mov	x3,32
.sc:	ldr	s0,[x4],4
	str	s0,[x5]
	add	x5,x5,36*4
	subs	x3,x3,1
	b.ne	.sc
	add	x23,x23,1
	cmp	x23,18
	b.lo	.t
	; MDCT de chaque sous-bande (18 precedents + 18 nouveaux)
	mov	x24,0
.sbd:	mov	x3,36*4
	madd	x1,x24,x3,x21
	movi	v16.4s,0
	movi	v17.4s,0
	movi	v18.4s,0
	movi	v19.4s,0
	movi	v20.4s,0
	ldr	x2,[x19,ME_WM]
	mov	x3,36
.n:	ldr	s0,[x1],4
	ld1	{v1.4s,v2.4s,v3.4s,v4.4s},[x2],64
	ld1	{v5.4s},[x2],16
	fmla	v16.4s,v1.4s,v0.s[0]
	fmla	v17.4s,v2.4s,v0.s[0]
	fmla	v18.4s,v3.4s,v0.s[0]
	fmla	v19.4s,v4.4s,v0.s[0]
	fmla	v20.4s,v5.4s,v0.s[0]
	subs	x3,x3,1
	b.ne	.n
	mov	x3,18*4
	madd	x4,x24,x3,x22
	st1	{v16.4s,v17.4s,v18.4s,v19.4s},[x4],64
	str	d20,[x4]
	; les 18 nouveaux deviennent les precedents
	mov	x3,36*4
	madd	x1,x24,x3,x21
	add	x2,x1,18*4
	ld1	{v0.4s,v1.4s,v2.4s,v3.4s},[x2],64
	ldr	d4,[x2]
	st1	{v0.4s,v1.4s,v2.4s,v3.4s},[x1],64
	str	d4,[x1]
	add	x24,x24,1
	cmp	x24,32
	b.lo	.sbd
	; anticrenelage inverse : bu' = bu cs + bd ca, bd' = bd cs - bu ca
	adr	x6,mp3_cs
	adr	x7,mp3_ca
	mov	x24,1
.aa:	mov	x3,18*4
	madd	x8,x24,x3,x22
	mov	x9,0
.ab:	ldr	s0,[x6,x9,lsl 2]
	ldr	s1,[x7,x9,lsl 2]
	sub	x10,x8,x9,lsl 2
	sub	x10,x10,4
	ldr	s2,[x10]
	ldr	s3,[x8,x9,lsl 2]
	fmul	s4,s2,s0
	fmadd	s4,s3,s1,s4
	fmul	s5,s3,s0
	fmsub	s5,s2,s1,s5
	str	s4,[x10]
	str	s5,[x8,x9,lsl 2]
	add	x9,x9,1
	cmp	x9,8
	b.lo	.ab
	add	x24,x24,1
	cmp	x24,32
	b.lo	.aa
	add	sp,sp,384
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat : M/S si la somme des |M| + |S| est moindre que |G| + |D|
; (M = (G + D) / racine 2, S = (G - D) / racine 2)
mp3e_stereo:
	ldr	x1,[x0,ME_XR]
	ldr	x2,[x0,ME_NGR]
	movi	v4.4s,0
	movi	v5.4s,0
	mov	x3,x1
	mov	x6,x2
.s:	mov	x4,144
	add	x5,x3,ME_XRG
.sl:	ld1	{v0.4s},[x3],16
	ld1	{v1.4s},[x5],16
	fabs	v2.4s,v0.4s
	fabs	v3.4s,v1.4s
	fadd	v4.4s,v4.4s,v2.4s
	fadd	v4.4s,v4.4s,v3.4s
	fadd	v2.4s,v0.4s,v1.4s
	fsub	v3.4s,v0.4s,v1.4s
	fabs	v2.4s,v2.4s
	fabs	v3.4s,v3.4s
	fadd	v5.4s,v5.4s,v2.4s
	fadd	v5.4s,v5.4s,v3.4s
	subs	x4,x4,1
	b.ne	.sl
	add	x3,x3,ME_XRG*2-576*4
	subs	x6,x6,1
	b.ne	.s
	faddp	v4.4s,v4.4s,v4.4s
	faddp	s4,v4.2s
	faddp	v5.4s,v5.4s,v5.4s
	faddp	s5,v5.2s
	adr	x3,mp3e_kf
	ldr	s6,[x3,4]			; racine de 1/2
	fmul	s5,s5,s6
	fcmp	s5,s4
	b.ge	.nms
	mov	x3,1
	str	x3,[x0,ME_MS]
	dup	v6.4s,v6.s[0]
.m:	mov	x4,144
	add	x5,x1,ME_XRG
.ml:	ld1	{v0.4s},[x1]
	ld1	{v1.4s},[x5]
	fadd	v2.4s,v0.4s,v1.4s
	fsub	v3.4s,v0.4s,v1.4s
	fmul	v2.4s,v2.4s,v6.4s
	fmul	v3.4s,v3.4s,v6.4s
	st1	{v2.4s},[x1],16
	st1	{v3.4s},[x5],16
	subs	x4,x4,1
	b.ne	.ml
	add	x1,x1,ME_XRG*2-576*4
	subs	x2,x2,1
	b.ne	.m
.nms:	ret

; ---------------------------------------------------------------------------
;  Quantification
; ---------------------------------------------------------------------------

; x0 = etat, x1 = granule -> global_gain, bits ; ecrit le granule
mp3e_granule:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	; |xr|^(3/4) = racine(|x| racine |x|)
	mov	x21,0
.p:	ldr	x0,[x19,ME_XR]
	add	x1,x21,x20,lsl 1
	mov	x2,ME_XRG
	madd	x0,x1,x2,x0
	ldr	x1,[x19,ME_X34]
	madd	x1,x21,x2,x1
	mov	x2,144
.pl:	ld1	{v0.4s},[x0],16
	fabs	v0.4s,v0.4s
	fsqrt	v1.4s,v0.4s
	fmul	v1.4s,v1.4s,v0.4s
	fsqrt	v1.4s,v1.4s
	st1	{v1.4s},[x1],16
	subs	x2,x2,1
	b.ne	.pl
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	mp3e_scf
	add	x21,x21,1
	ldr	x0,[x19,ME_NCH]
	cmp	x21,x0
	b.lo	.p
	; budget : disponible - parts des granules suivants (<= 4095 par canal)
	ldr	x21,[x19,ME_AVAIL]
	ldr	x22,[x19,ME_USED]
	ldr	x23,[x19,ME_MEANGR]
	ldr	x24,[x19,ME_NGR]
	sub	x24,x24,x20
	sub	x24,x24,1
	sub	x25,x21,x22
	msub	x25,x23,x24,x25
	ldr	x0,[x19,ME_NCH]
	mov	x1,4095
	mul	x0,x0,x1
	cmp	x25,x0
	csel	x25,x25,x0,lt
	; plus petit global_gain qui tient (dichotomie)
	mov	x26,0
	mov	x27,255
.bs:	cmp	x26,x27
	b.hs	.bsd
	add	x28,x26,x27
	lsr	x28,x28,1
	mov	x0,x19
	mov	x1,x20
	mov	x2,x28
	bl	mp3e_bitsat
	cmp	x0,x25
	b.gt	.up
	mov	x27,x28
	b	.bs
.up:	add	x28,x28,1
	mov	x26,x28
	b	.bs
.bsd:	; pas plus fin que la cible
	ldr	s0,[x19,ME_T]
	fcvtns	x27,s0
	cmp	x27,x26
	csel	x27,x27,x26,gt
	mov	x0,255
	cmp	x27,x0
	csel	x27,x27,x0,lt
	; mais assez de bits pour ne pas deborder du reservoir
	ldr	x0,[x19,ME_RMAX]
	lsl	x0,x0,3
	sub	x28,x21,x22
	sub	x28,x28,x0
	msub	x28,x23,x24,x28
.mn:	cmp	x27,x26
	b.ls	.mnd
	mov	x0,x19
	mov	x1,x20
	mov	x2,x27
	bl	mp3e_bitsat
	cmp	x0,x28
	b.ge	.mnd
	sub	x27,x27,1
	b	.mn
.mnd:	; la cible suit les granules difficiles
	scvtf	s1,x27
	ldr	s0,[x19,ME_T]
	fcmp	s1,s0
	b.le	.fin
	fsub	s2,s1,s0
	fmov	s3,0.25
	fmadd	s0,s2,s3,s0
	str	s0,[x19,ME_T]
.fin:	; quantification retenue (au besoin plus grossiere : budget)
	mov	x0,x19
	mov	x1,x20
	mov	x2,x27
	bl	mp3e_bitsat
	cmp	x0,x25
	b.le	.ok
	cmp	x27,255
	b.hs	.ok
	add	x27,x27,1
	b	.fin
.ok:	add	x22,x22,x0
	str	x22,[x19,ME_USED]
	; Huffman de chaque canal
	mov	x24,0
.w:	add	x2,x19,ME_GI
	add	x3,x24,x20,lsl 1
	add	x2,x2,x3,lsl 7
	str	w27,[x2,ME_GGG]
	ldr	x0,[x19,ME_IX]
	mov	x3,576*4
	madd	x0,x24,x3,x0
	ldr	x1,[x19,ME_XR]
	add	x3,x24,x20,lsl 1
	mov	x4,ME_XRG
	madd	x1,x3,x4,x1
	ldr	x3,[x19,ME_SFB]
	mov	x4,x19
	bl	mp3e_hwrite
	add	x24,x24,1
	ldr	x0,[x19,ME_NCH]
	cmp	x24,x0
	b.lo	.w
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat, x1 = granule, x2 = canal : facteurs d'echelle (mise en forme
; du bruit, sans modele psychoacoustique) : une bande dont l'energie par
; ligne est r fois moindre que celle de la plus forte (r <= 2^13) est
; amplifiee de 2^(sf / 2), sf = alpha log2 r (bornes 15 / 7) ; appliques a
; |xr|^3/4 ; part2 et scalefac_compress (MPEG-1 : 2 groupes, table ;
; LSF : partitions 6 5 5 5)
mp3e_scf:
	sub	sp,sp,96
	add	x3,x0,ME_GI
	add	x4,x2,x1,lsl 1
	add	x3,x3,x4,lsl 7			; informations du granule
	ldr	x5,[x0,ME_XR]
	mov	x6,ME_XRG
	madd	x5,x4,x6,x5			; xr
	ldr	x7,[x0,ME_X34]
	madd	x7,x2,x6,x7			; |xr|^3/4
	ldr	x8,[x0,ME_SFB]
	adr	x9,mp3e_kf
	ldr	s16,[x9,16]			; 2^-23
	ldr	s17,[x9,20]			; 127
	ldr	s18,[x9,24]			; plancher (energie par ligne)
	ldr	s19,[x9,28]			; alpha
	ldr	s20,[x9,32]			; -1000 : bande muette
	fmov	s21,s20				; maximum
	; log2 de l'energie par ligne (approche : bits du flottant)
	mov	x10,0
.e:	ldrh	w11,[x8,x10,lsl 1]
	add	x12,x10,1
	ldrh	w12,[x8,x12,lsl 1]
	sub	x13,x12,x11
	fmov	s0,wzr
.el:	ldr	s1,[x5,x11,lsl 2]
	fmadd	s0,s1,s1,s0
	add	x11,x11,1
	cmp	x11,x12
	b.lo	.el
	scvtf	s1,x13
	fdiv	s0,s0,s1
	fmov	s2,s20
	fcmp	s0,s18
	b.lt	.es
	fmov	w14,s0
	scvtf	s2,w14
	fmul	s2,s2,s16
	fsub	s2,s2,s17
.es:	str	s2,[sp,x10,lsl 2]
	fmax	s21,s21,s2
	add	x10,x10,1
	cmp	x10,21
	b.lo	.e
	; facteurs
	fmov	s3,13.0
	fsub	s4,s21,s3
	mov	x10,0
.f:	ldr	s2,[sp,x10,lsl 2]
	mov	w14,0
	fcmp	s2,s20
	b.eq	.fs
	fmax	s2,s2,s4
	fsub	s2,s21,s2
	fmul	s2,s2,s19
	fcvtns	w14,s2
	mov	w13,15
	cmp	x10,11
	b.lo	.fm
	mov	w13,7
.fm:	cmp	w14,w13
	csel	w14,w14,w13,lo
.fs:	add	x13,x3,x10
	strb	w14,[x13,ME_GSF]
	cbz	w14,.fn
	adr	x9,mp3e_amp			; 2^(3/8 sf) sur |xr|^3/4
	ldr	s5,[x9,x14,lsl 2]
	ldrh	w11,[x8,x10,lsl 1]
	add	x12,x10,1
	ldrh	w12,[x8,x12,lsl 1]
.fa:	ldr	s1,[x7,x11,lsl 2]
	fmul	s1,s1,s5
	str	s1,[x7,x11,lsl 2]
	add	x11,x11,1
	cmp	x11,x12
	b.lo	.fa
.fn:	add	x10,x10,1
	cmp	x10,21
	b.lo	.f
	; bits necessaires par partition (sp : 4 octets)
	adr	x9,mp3e_part1			; MPEG-1 : 0..10, 11..20
	ldr	x10,[x0,ME_LSF]
	cbz	x10,.pt
	adr	x9,mp3e_part2			; LSF : 0..5, 6..10, 11..15, 16..20
.pt:	mov	x10,0				; partition
	mov	x11,0				; bande
.pp:	ldrb	w12,[x9,x10]
	cbz	w12,.ppd
	mov	w13,0
.pm:	add	x14,x3,x11
	ldrb	w14,[x14,ME_GSF]
	cmp	w14,w13
	csel	w13,w14,w13,hi
	add	x11,x11,1
	cmp	x11,x12
	b.lo	.pm
	clz	w13,w13
	mov	w14,32
	sub	w13,w14,w13
	strb	w13,[sp,x10]
	add	x10,x10,1
	b	.pp
.ppd:	ldr	x10,[x0,ME_LSF]
	cbnz	x10,.lsf
	; MPEG-1 : (slen1, slen2) de la table, le moins de bits
	ldrb	w15,[sp]
	ldrb	w16,[sp,1]
	adr	x9,mp3e_sfc1
	mov	x10,0
	mov	w11,999
	mov	x12,0
.c:	add	x14,x9,x10,lsl 1
	ldrb	w13,[x14]
	ldrb	w14,[x14,1]
	cmp	w13,w15
	b.lo	.cn
	cmp	w14,w16
	b.lo	.cn
	mov	w17,11
	mul	w13,w13,w17
	mov	w17,10
	madd	w13,w14,w17,w13
	cmp	w13,w11
	b.hs	.cn
	mov	w11,w13
	mov	x12,x10
.cn:	add	x10,x10,1
	cmp	x10,16
	b.lo	.c
	str	w12,[x3,ME_GSFC]
	str	w11,[x3,ME_GP2]
	add	x14,x9,x12,lsl 1
	ldrb	w13,[x14]
	strb	w13,[sp]
	ldrb	w13,[x14,1]
	strb	w13,[sp,1]
	adr	x9,mp3e_part1
	b	.sl
.lsf:	; scalefac_compress = ((s0 5 + s1) << 4) + (s2 << 2) + s3
	ldrb	w12,[sp]
	ldrb	w13,[sp,1]
	ldrb	w14,[sp,2]
	ldrb	w15,[sp,3]
	mov	w16,5
	madd	w16,w12,w16,w13
	lsl	w16,w16,4
	orr	w16,w16,w14,lsl 2
	orr	w16,w16,w15
	str	w16,[x3,ME_GSFC]
	add	w16,w13,w14
	add	w16,w16,w15
	mov	w17,5
	mul	w16,w16,w17
	mov	w17,6
	madd	w16,w12,w17,w16
	str	w16,[x3,ME_GP2]
	adr	x9,mp3e_part2
.sl:	; bits de chaque bande
	mov	x10,0
	mov	x11,0
.sq:	ldrb	w12,[x9,x10]
	cbz	w12,.sd
	ldrb	w13,[sp,x10]
.sbb:	add	x14,x3,x11
	strb	w13,[x14,ME_GSL]
	add	x11,x11,1
	cmp	x11,x12
	b.lo	.sbb
	add	x10,x10,1
	b	.sq
.sd:	add	sp,sp,96
	ret

; x0 = etat, x1 = granule, x2 = global_gain -> x0 = bits des canaux
; (ME_INF : valeur > 8206 ou plus de 4095 bits) ; ME_IX, ME_GI remplis
mp3e_bitsat:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,0
	mov	x23,0
.c:	ldr	x0,[x19,ME_X34]
	mov	x3,ME_XRG
	madd	x0,x22,x3,x0
	ldr	x1,[x19,ME_IX]
	mov	x3,576*4
	madd	x1,x22,x3,x1
	ldr	x2,[x19,ME_STEP]
	ldr	s0,[x2,x21,lsl 2]
	bl	mp3e_quant
	movl	x1,8206
	cmp	x0,x1
	b.hi	.inf
	ldr	x0,[x19,ME_IX]
	mov	x3,576*4
	madd	x0,x22,x3,x0
	ldr	x1,[x19,ME_SFB]
	add	x2,x19,ME_GI
	add	x3,x22,x20,lsl 1
	add	x24,x2,x3,lsl 7
	mov	x2,x24
	ldr	x3,[x19,ME_WK]
	bl	mp3e_count
	ldr	w1,[x24,ME_GP2]			; + facteurs d'echelle
	add	x0,x0,x1
	str	w0,[x24,ME_GBITS]
	cmp	x0,4095
	b.hi	.inf
	add	x23,x23,x0
	add	x22,x22,1
	ldr	x0,[x19,ME_NCH]
	cmp	x22,x0
	b.lo	.c
	mov	x0,x23
	b	.out
.inf:	movl	x0,ME_INF
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = |xr|^3/4 (576), x1 = sortie (32 bits), s0 = pas
; -> x0 = valeur maximale ; ix = partie entiere(x34 pas + 0,4054)
mp3e_quant:
	dup	v1.4s,v0.s[0]
	adr	x2,mp3e_kf
	ld1r	{v2.4s},[x2]
	movi	v3.4s,0
	mov	x3,72
.l:	ld1	{v4.4s,v5.4s},[x0],32
	fmul	v4.4s,v4.4s,v1.4s
	fmul	v5.4s,v5.4s,v1.4s
	fadd	v4.4s,v4.4s,v2.4s
	fadd	v5.4s,v5.4s,v2.4s
	fcvtzu	v4.4s,v4.4s
	fcvtzu	v5.4s,v5.4s
	umax	v3.4s,v3.4s,v4.4s
	umax	v3.4s,v3.4s,v5.4s
	st1	{v4.4s,v5.4s},[x1],32
	subs	x3,x3,1
	b.ne	.l
	umaxv	s3,v3.4s
	fmov	w0,s3
	ret

; ---------------------------------------------------------------------------
;  Huffman : comptage
; ---------------------------------------------------------------------------

; x0 = valeurs absolues (576 x 32 bits), x1 = debuts des bandes (23 x 16
; bits), x2 = informations du granule, x3 = travail -> x0 = bits (codes,
; linbits, signes ; big_values, regions, tables, quadruplets remplis)
mp3e_count:
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
	; derniere valeur non nulle -> fin des paires
	mov	x4,576
.z:	cbz	x4,.zd
	sub	x5,x4,1
	ldr	w6,[x19,x5,lsl 2]
	cbnz	w6,.zd
	mov	x4,x5
	b	.z
.zd:	add	x4,x4,1
	and	x4,x4,not 1
	; quadruplets (valeurs <= 1) depuis la fin : tables A et B
	mov	x5,x4
	mov	x6,0
	mov	x7,0
	adr	x8,mp3e_quad
.q:	cmp	x5,4
	b.lo	.qd
	sub	x9,x5,4
	add	x10,x19,x9,lsl 2
	ldp	w11,w12,[x10]
	ldp	w13,w14,[x10,8]
	orr	w15,w11,w12
	orr	w15,w15,w13
	orr	w15,w15,w14
	cmp	w15,1
	b.hi	.qd
	lsl	w15,w11,3
	orr	w15,w15,w12,lsl 2
	orr	w15,w15,w13,lsl 1
	orr	w15,w15,w14
	add	w16,w11,w12
	add	w16,w16,w13
	add	w16,w16,w14
	add	x10,x8,x15,lsl 1
	ldrb	w10,[x10,1]
	add	x6,x6,x10
	add	x6,x6,x16
	add	x7,x7,x16
	add	x7,x7,4
	mov	x5,x9
	b	.q
.qd:	sub	x9,x4,x5
	lsr	x9,x9,2
	str	w9,[x21,ME_GC1]
	cmp	x6,x7
	cset	w10,hi
	str	w10,[x21,ME_GC1T]
	csel	x25,x6,x7,ls			; bits des quadruplets
	lsr	x9,x5,1
	str	w9,[x21,ME_GBV]
	mov	x23,x5				; lignes des big_values
	; segments : bandes coupees a la fin des big_values
	mov	x24,0
	cbz	x23,.sg
	mov	x24,1
.ns:	cmp	x24,22
	b.hs	.sg
	ldrh	w9,[x20,x24,lsl 1]
	cmp	x9,x23
	b.hs	.sg
	add	x24,x24,1
	b	.ns
.sg:	stp	xzr,xzr,[x22]
	stp	xzr,xzr,[x22,16]
	stp	xzr,xzr,[x22,32]
	stp	xzr,xzr,[x22,48]
	str	wzr,[x22,ME_WK_E]
	mov	x26,0				; segment j
.seg:	cmp	x26,x24
	b.hs	.sgx
	ldrh	w9,[x20,x26,lsl 1]		; [a, b)
	add	x10,x26,1
	ldrh	w10,[x20,x10,lsl 1]
	cmp	x10,x23
	csel	x10,x10,x23,lo
	; maximum, valeurs >= 15, non nulles (signes)
	mov	x11,0
	mov	x12,0
	mov	x13,0
	mov	x14,x9
.m:	cmp	x14,x10
	b.hs	.md
	ldr	w15,[x19,x14,lsl 2]
	cmp	x15,x11
	csel	x11,x15,x11,hi
	cmp	x15,15
	cinc	x12,x12,hs
	cmp	x15,0
	cinc	x13,x13,ne
	add	x14,x14,1
	b	.m
.md:	add	x25,x25,x13
	add	x15,x22,ME_WK_E
	ldr	w16,[x15,x26,lsl 2]
	add	w16,w16,w12
	add	x17,x26,1
	str	w16,[x15,x17,lsl 2]
	add	x15,x22,ME_WK_M
	str	w11,[x15,x26,lsl 2]
	; cout de chaque table de codes possible, cumule (P[j + 1] = P[j] + cout)
	lsl	x27,x26,6
	add	x27,x22,x27
	adr	x28,mp3e_hlist
	mov	x14,0
.k:	ldrb	w15,[x28,2]			; dimension
	cmp	x14,13
	b.hs	.kv				; tables 16 et 24 : toujours
	cmp	x11,x15
	b.hs	.kn
.kv:	ldrh	w16,[x28]
	adr	x17,mp3e_hlen
	add	x16,x17,x16
	mov	x17,0
	mov	x13,x9
	mov	w3,15
.pp:	cmp	x13,x10
	b.hs	.pd
	add	x2,x19,x13,lsl 2
	ldp	w0,w1,[x2]
	cmp	w0,15
	csel	w0,w0,w3,lo
	cmp	w1,15
	csel	w1,w1,w3,lo
	madd	w0,w0,w15,w1
	ldrb	w0,[x16,x0]
	add	x17,x17,x0
	add	x13,x13,2
	b	.pp
.pd:	ldr	w0,[x27,x14,lsl 2]
	add	w0,w0,w17
	add	x1,x27,64
	str	w0,[x1,x14,lsl 2]
.kn:	add	x28,x28,4
	add	x14,x14,1
	cmp	x14,15
	b.lo	.k
	add	x26,x26,1
	b	.seg
.sgx:	cbz	x24,.empty
	; region 0 = [0, a) (a <= 16), region 2 = [c, fin)
	mov	x26,1
.bz:	cmp	x26,x24
	b.hi	.bzd
	cmp	x26,16
	b.hi	.bzd
	mov	x0,0
	mov	x1,x26
	bl	mp3e_best
	add	x2,x22,ME_WK_B0
	add	x2,x2,x26,lsl 3
	stp	w0,w1,[x2]
	add	x26,x26,1
	b	.bz
.bzd:	mov	x26,0
.bt:	cmp	x26,x24
	b.hi	.btd
	mov	x0,x26
	mov	x1,x24
	bl	mp3e_best
	add	x2,x22,ME_WK_B2
	add	x2,x2,x26,lsl 3
	stp	w0,w1,[x2]
	add	x26,x26,1
	b	.bt
.btd:	; toutes les coupes (region0_count, region1_count)
	movl	x27,0x7FFFFFFF
	mov	x26,0				; r0
.ra:	add	x9,x26,1
	cmp	x9,x24
	csel	x10,x9,x24,lo			; a
	mov	x28,0				; r1
.rb:	add	x11,x9,x28
	add	x11,x11,1
	cmp	x11,22
	b.hi	.rbx
	cmp	x11,x24
	csel	x12,x11,x24,lo			; c
	mov	x0,x10
	mov	x1,x12
	bl	mp3e_best
	add	x2,x22,ME_WK_B0
	add	x2,x2,x10,lsl 3
	ldp	w3,w4,[x2]
	add	x2,x22,ME_WK_B2
	add	x2,x2,x12,lsl 3
	ldp	w5,w6,[x2]
	add	x0,x0,x3
	add	x0,x0,x5
	cmp	x0,x27
	b.hs	.nb
	mov	x27,x0
	str	w26,[x21,ME_GR0]
	str	w28,[x21,ME_GR1]
	str	w4,[x21,ME_GT0]
	str	w1,[x21,ME_GT1]
	str	w6,[x21,ME_GT2]
.nb:	cmp	x11,x24
	b.hs	.rbx
	add	x28,x28,1
	cmp	x28,8
	b.lo	.rb
.rbx:	cmp	x9,x24
	b.hs	.rax
	add	x26,x26,1
	cmp	x26,16
	b.lo	.ra
.rax:	add	x0,x27,x25
	b	.out
.empty:	str	wzr,[x21,ME_GT0]
	str	wzr,[x21,ME_GT1]
	str	wzr,[x21,ME_GT2]
	str	wzr,[x21,ME_GR0]
	str	wzr,[x21,ME_GR1]
	mov	x0,x25
.out:	str	w0,[x21,ME_GBITS]
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = premier segment, x1 = fin (segments), x22 = travail du comptage
; -> x0 = bits des codes et linbits (sans les signes), x1 = table
; (ne detruit que x0-x8, x13-x17)
mp3e_best:
	cmp	x0,x1
	b.hs	.zero
	add	x2,x22,ME_WK_M
	mov	x3,0				; maximum de la region
	mov	x4,x0
.m:	ldr	w5,[x2,x4,lsl 2]
	cmp	x5,x3
	csel	x3,x5,x3,hi
	add	x4,x4,1
	cmp	x4,x1
	b.lo	.m
	cbz	x3,.zero
	add	x2,x22,ME_WK_E
	ldr	w8,[x2,x1,lsl 2]
	ldr	w5,[x2,x0,lsl 2]
	sub	w8,w8,w5			; valeurs >= 15
	lsl	x4,x0,6
	add	x4,x22,x4			; P[debut]
	lsl	x5,x1,6
	add	x5,x22,x5			; P[fin]
	movl	w6,0x7FFFFFFF
	mov	x7,0
	adr	x0,mp3e_hlist
	mov	x1,0
.k:	ldrb	w2,[x0,2]
	cmp	x3,x2
	b.hs	.kn
	ldr	w2,[x5,x1,lsl 2]
	ldr	w13,[x4,x1,lsl 2]
	sub	w2,w2,w13
	cmp	w2,w6
	b.hs	.kn
	mov	w6,w2
	ldrb	w7,[x0,3]
.kn:	add	x0,x0,4
	add	x1,x1,1
	cmp	x1,13
	b.lo	.k
	; familles 16 et 24 : linbits selon le maximum
	mov	x13,0
	cmp	x3,15
	b.lo	.nd
	sub	x13,x3,15
	clz	x13,x13
	mov	x14,64
	sub	x13,x14,x13
.nd:	adr	x14,mp3e_lbsel
	add	x14,x14,x13,lsl 2
	ldr	w2,[x5,13*4]
	ldr	w15,[x4,13*4]
	sub	w2,w2,w15
	ldrb	w15,[x14,1]
	madd	w2,w15,w8,w2
	cmp	w2,w6
	b.hs	.fa
	mov	w6,w2
	ldrb	w7,[x14]
.fa:	ldr	w2,[x5,14*4]
	ldr	w15,[x4,14*4]
	sub	w2,w2,w15
	ldrb	w15,[x14,3]
	madd	w2,w15,w8,w2
	cmp	w2,w6
	b.hs	.fd
	mov	w6,w2
	ldrb	w7,[x14,2]
.fd:	mov	x0,x6
	mov	x1,x7
	ret
.zero:	mov	x0,0
	mov	x1,0
	ret

; ---------------------------------------------------------------------------
;  Huffman : ecriture
; ---------------------------------------------------------------------------

; x0 = valeurs absolues, x1 = xr (signes), x2 = informations du granule,
; x3 = debuts des bandes, x4 = etat (ecrivain ME_WACC..ME_WPTR)
mp3e_hwrite:
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
	ldr	x12,[x23,ME_WACC]
	ldr	x13,[x23,ME_WN]
	ldr	x14,[x23,ME_WPTR]
	mov	x27,0				; facteurs d'echelle
.sf:	add	x0,x21,x27
	ldrb	w11,[x0,ME_GSF]
	ldrb	w10,[x0,ME_GSL]
	me_put	x11,x10
	add	x27,x27,1
	cmp	x27,21
	b.lo	.sf
	ldr	w24,[x21,ME_GBV]
	lsl	x24,x24,1
	ldr	w0,[x21,ME_GR0]
	add	x1,x0,1
	ldrh	w25,[x22,x1,lsl 1]
	cmp	x25,x24
	csel	x25,x25,x24,lo			; fin de la region 0
	ldr	w1,[x21,ME_GR1]
	add	x1,x0,x1
	add	x1,x1,2
	ldrh	w26,[x22,x1,lsl 1]
	cmp	x26,x24
	csel	x26,x26,x24,lo			; fin de la region 1
	mov	x27,0
.p:	cmp	x27,x24
	b.hs	.c1
	ldr	w0,[x21,ME_GT0]
	cmp	x27,x25
	b.lo	.t
	ldr	w0,[x21,ME_GT1]
	cmp	x27,x26
	b.lo	.t
	ldr	w0,[x21,ME_GT2]
.t:	cbz	w0,.pn
	adr	x1,mp3e_hdir
	add	x1,x1,x0,lsl 2
	ldrh	w2,[x1]
	ldrb	w3,[x1,2]
	ldrb	w28,[x1,3]			; linbits
	add	x4,x19,x27,lsl 2
	ldp	w5,w6,[x4]
	mov	w7,15
	cmp	w5,15
	csel	w8,w5,w7,lo
	cmp	w6,15
	csel	w9,w6,w7,lo
	madd	w8,w8,w3,w9
	add	w8,w8,w2
	adr	x1,mp3e_hlen
	ldrb	w10,[x1,x8]
	adr	x1,mp3e_hcode
	ldr	w11,[x1,x8,lsl 2]
	me_put	x11,x10
	cbz	w5,.y
	cbz	w28,.xs
	cmp	w5,15
	b.lo	.xs
	sub	w11,w5,15
	me_put	x11,x28
.xs:	ldr	w11,[x20,x27,lsl 2]
	lsr	w11,w11,31
	mov	x10,1
	me_put	x11,x10
.y:	cbz	w6,.pn
	cbz	w28,.ys
	cmp	w6,15
	b.lo	.ys
	sub	w11,w6,15
	me_put	x11,x28
.ys:	add	x4,x20,x27,lsl 2
	ldr	w11,[x4,4]
	lsr	w11,w11,31
	mov	x10,1
	me_put	x11,x10
.pn:	add	x27,x27,2
	b	.p
.c1:	; quadruplets
	ldr	w24,[x21,ME_GC1]
	ldr	w25,[x21,ME_GC1T]
.q:	cbz	w24,.done
	add	x4,x19,x27,lsl 2
	ldp	w5,w6,[x4]
	ldp	w7,w8,[x4,8]
	lsl	w9,w5,3
	orr	w9,w9,w6,lsl 2
	orr	w9,w9,w7,lsl 1
	orr	w9,w9,w8
	cbnz	w25,.qb
	adr	x1,mp3e_quad
	add	x1,x1,x9,lsl 1
	ldrb	w11,[x1]
	ldrb	w10,[x1,1]
	b	.qp
.qb:	mov	w11,15				; table B : 4 bits inverses
	sub	w11,w11,w9
	mov	w10,4
.qp:	me_put	x11,x10
	mov	x26,0
.qs:	add	x4,x19,x27,lsl 2
	ldr	w5,[x4,x26,lsl 2]
	cbz	w5,.qn
	add	x4,x20,x27,lsl 2
	ldr	w11,[x4,x26,lsl 2]
	lsr	w11,w11,31
	mov	x10,1
	me_put	x11,x10
.qn:	add	x26,x26,1
	cmp	x26,4
	b.lo	.qs
	add	x27,x27,4
	sub	w24,w24,1
	b	.q
.done:	str	x12,[x23,ME_WACC]
	str	x13,[x23,ME_WN]
	str	x14,[x23,ME_WPTR]
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Donnees
; ---------------------------------------------------------------------------

		align	8
mp3e_kd:	dd	0.04908738521234052		; pi / 64
		dd	0.08726646259971647		; pi / 36
		dd	0.04363323129985824		; pi / 72
		dd	0.1111111111111111		; 1 / 9
		dd	712945084849.3173		; 2^39,375 (pas de global_gain 0)
		dd	0.8781260801866497		; 2^-0,1875
mp3e_kf:	dw	0x3ECF9097			; 0,4054 (arrondi de la norme)
		dw	0x3F3504F3			; racine de 1/2
		dw	0x43160000			; 150 : cible de global_gain au depart
		dw	0x35800000			; 2^-20
		dw	0x34000000			; 2^-23 (log2 approche)
		dw	0x42FE0000			; 127
		dw	0x322BCC77			; 1e-8 : energie par ligne negligeable
		dw	0x3F000000			; alpha : 1/2 (mise en forme du bruit)
		dw	0xC47A0000			; -1000
; 2^(3/8 sf) : facteur d'echelle sf (2^(sf/2)) sur |xr|^3/4
mp3e_amp:	dw	0x3F800000, 0x3FA5FED7, 0x3FD744FD, 0x400B95C2, 0x403504F3, 0x406AC0C7
		dw	0x409837F0, 0x40C5672A, 0x41000000, 0x4125FED7, 0x415744FD, 0x418B95C2
		dw	0x41B504F3, 0x41EAC0C7, 0x421837F0, 0x4245672A
mp3e_sgn:	dw	0x3F800000, 0xBF800000, 0x3F800000, 0xBF800000
mp3e_rates:	dw	44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000
mp3e_br1:	dh	0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320
mp3e_br2:	dh	0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160
; passe-bas : kbit/s par canal (au moins), frequence de coupure (Hz)
mp3e_lp:	dh	96, 24000, 80, 20000, 64, 18500, 56, 17000, 48, 15500
		dh	40, 14000, 32, 12000, 24, 9500, 16, 7500, 0, 5000
; MPEG-1 : scalefac_compress -> slen1 (bandes 0..10), slen2 (11..20)
mp3e_sfc1:	db	0, 0, 0, 1, 0, 2, 0, 3, 3, 0, 1, 1, 1, 2, 1, 3
		db	2, 1, 2, 2, 2, 3, 3, 1, 3, 2, 3, 3, 4, 2, 4, 3
; fins des partitions de facteurs d'echelle (0 : fin)
mp3e_part1:	db	11, 21, 0
mp3e_part2:	db	6, 11, 16, 21, 0
		align	4
