; ============================================================================
;  kits/audiokit/ak_opusenc.inc - encodeur Opus (RFC 6716) en mode CELT seul,
;  dans Ogg Opus (RFC 7845)
;
;  Trames CELT de 20 ms (TOC : configuration CELT 20 ms, code 0), debit
;  constant (paquets de taille fixe : debit / 400 octets). Largeur de bande
;  d'apres le debit par voie (FB, SWB, WB, NB). Une trame :
;  pre-accentuation, MDCT (fenetre de recouvrement de 120, par la FFT de
;  480 points d'ak_mdct.inc), energies des bandes (log2), codeur de plage
;  (RFC 6716 5.1) ; silence, post-filtre, transitoire a 0 ; intra a la
;  premiere trame ; energies grossieres (prediction, Laplace), tf_res a 0,
;  etalement normal, dynalloc (analyse simplifiee de libopus : enveloppe
;  suiveuse), trim (pente spectrale, correlation stereo), allocation (le
;  calcul du decodeur ; bandes sautees au-dela de la largeur de bande du
;  signal, intensite d'apres le debit, pas de double stereo), energies
;  fines, bandes : division recursive, theta (stereo M/S ou intensite),
;  recherche PVQ (projection puis impulsions gloutonnes), rotation
;  d'etalement, indice CWRS ; energies finales.
;  Pas de blocs courts (ni anti-effondrement), ni de post-filtre.
;  Retard : 120 echantillons (pre-skip). Hors de 48 kHz : reechantillonneur
;  polyphase a sinus cardinal fenetre (Blackman, 32 passages par zero de
;  chaque cote, table de 128 pas par passage interpolee lineairement ;
;  coupure a 0,455 de la plus petite des deux frequences ; poids de chaque
;  phase en table quand 48000 / pgcd(fs, 48000) phases tiennent), sans
;  retard (noyau symetrique).
;
;  Dans tout le fichier : x19 = etat (OE_*), x20 = codeur de plage (OEC_*,
;  dans l'etat) ; aucune fonction ne les change.
; ============================================================================

OE_N		= 960		; echantillons d'une trame (20 ms, LM = 3)
OE_LM		= 3
OE_PCMN		= OE_N + 120	; tampon d'une voie : recouvrement + trame
OE_PAGEPK	= 50		; paquets par page au plus (1 s)
OE_MINRATE	= 16000
OE_MAXRATE	= 256000
OE_DEFRATE	= 96000
OE_SERIAL	= 0x41524D50
OE_RSZ		= 32		; reechantillonneur : demi-largeur (passages par zero)
OE_RSR		= 128		; pas de la table par passage
OE_RSHB		= 8192		; historique d'une voie (flottants)
OE_RSWTN	= 32768		; table des poids : phases x prises au plus

; --- codeur de plage (OEC_* ; EC_NBITS et EC_RNG comme le decodeur, pour
; ec_tell et ec_tell_frac d'ak_opus.inc) -------------------------------------
OEC_BUF		= 0
OEC_STORAGE	= 8
OEC_OFFS	= 16		; octets ecrits par l'avant
OEC_ENDOFFS	= 24		; octets ecrits par la fin
OEC_WIN		= 32		; bits bruts en attente
OEC_NEND	= 40
OEC_VAL		= 72		; bas de l'intervalle
OEC_EXT		= 80		; octets 255 en attente (retenue)
OEC_REM		= 88		; octet en attente (-1 : aucun)
OEC_ERR		= 96
OEC_SIZE	= 112

; --- etat --------------------------------------------------------------------------
OE_C		= 0		; voies (1, 2)
OE_RATE		= 8		; frequence d'entree
OE_NBYTES	= 16		; octets CELT d'une trame (paquet - TOC)
OE_END		= 24		; bande de fin
OE_TOC		= 32
OE_ITGT		= 40		; bande d'intensite visee
OE_PRESKIP	= 48
OE_FILL		= 56		; echantillons (48 kHz) de la trame en cours
OE_LEN48	= 64		; echantillons utiles a 48 kHz
OE_PKTS		= 72		; paquets audio
OE_SEQ		= 80		; numero de la prochaine page
OE_NPKT		= 88		; page en cours : paquets, valeurs de lacage, octets
OE_NSEG		= 96
OE_DLEN		= 104
OE_LASTCODED	= 112		; bandes codees de la trame precedente
OE_FRAMES	= 120
OE_INTENS	= 128
OE_CODED	= 136
OE_BALANCE	= 144
OE_I		= 152		; bande courante
OE_REM		= 160		; remaining_bits
OE_TRIM		= 168
OE_INTRA	= 176
OE_ENC		= 184		; encodeur generique (akn_out)
OE_NIN		= 192		; trames d'entree recues
OE_RSIP		= 200		; reechantillonneur : position de la sortie (entier,
OE_RSFRAC	= 208		; reste sur 48000), indice absolu de H[0],
OE_RSBASE	= 216		; echantillons dans H, demi-largeur, sorties maximales,
OE_RSCNT	= 224		; pas de la table, gain (2 fc)
OE_RSTI		= 232
OE_RSLIM	= 240
OE_RSSTEP	= 248
OE_RSGAIN	= 252
OE_PRE		= 256		; pre-accentuation : memoire (2 flottants)
OE_TOT8		= 264		; total (1/8 bits) apres le dynalloc
OE_EC		= 272		; codeur de plage
OE_MD		= OE_EC + OEC_SIZE	; plan de la MDCT (MD_SIZE, N = 1920)
OE_DUAL		= OE_MD + MD_SIZE
OE_SIGBW	= OE_DUAL + 8	; derniere bande ou il y a du signal
OE_RSG		= OE_SIGBW + 8	; pgcd(fs, 48000) ; 0 : poids sans table
OE_BANDE	= 640		; amplitudes des bandes (2 x 21 flottants)
OE_LOGE		= OE_BANDE + 192	; log2 (moins eMeans)
OE_OLDE		= OE_LOGE + 192		; energies du decodeur
OE_ERR		= OE_OLDE + 192		; erreur de quantification
OE_FOL		= OE_ERR + 192		; dynalloc : suiveuse
OE_CAP		= OE_FOL + 192		; tableaux de 21 entiers (32 bits)
OE_OFFS		= OE_CAP + 96
OE_PULSES	= OE_OFFS + 96
OE_FINEQ	= OE_PULSES + 96
OE_PRIO		= OE_FINEQ + 96
OE_THRESH	= OE_PRIO + 96
OE_TRIMOFF	= OE_THRESH + 96
OE_BITS1	= OE_TRIMOFF + 96
OE_BITS2	= OE_BITS1 + 96
OE_WANT		= OE_BITS2 + 96		; dynalloc : quanta voulus
OE_A_PCM	= OE_WANT + 96		; tampons des voies (2 x OE_PCMN flottants)
OE_A_FREQ	= OE_A_PCM + 2 * OE_PCMN * 4	; spectres (2 x 960)
OE_A_X		= OE_A_FREQ + 2 * 960 * 4	; spectres normalises (2 x 960)
OE_A_V		= OE_A_X + 2 * 960 * 4		; MDCT : trame virtuelle (1920)
OE_A_UT		= OE_A_V + 1920 * 4		; table U(n, k) de CWRS (2048 mots)
OE_A_IY		= OE_A_UT + 2048 * 4		; impulsions (256)
OE_A_PX		= OE_A_IY + 256 * 4		; PVQ : |X| (256)
OE_A_PY		= OE_A_PX + 256 * 4		; PVQ : 2 y (256)
OE_A_PKT	= OE_A_PY + 256 * 4		; paquet (TOC + 1275)
OE_A_CRC	= OE_A_PKT + 1280		; table du CRC d'Ogg
OE_A_PAGE	= OE_A_CRC + 1024		; en-tete de page et lacage (27 + 255)
OE_A_PDATA	= OE_A_PAGE + 288		; donnees de la page (255 x 255)
OE_A_RSG	= OE_A_PDATA + 65040		; noyau du reechantillonneur
OE_A_RSW	= OE_A_RSG + (OE_RSZ * OE_RSR + 4) * 4	; poids d'une sortie (512)
OE_A_RSH	= OE_A_RSW + 512 * 4		; historique (2 x OE_RSHB)
OE_A_RSWT	= OE_A_RSH + 2 * OE_RSHB * 4	; poids de chaque phase (OE_RSWTN)
OE_SIZE		= OE_A_RSWT + OE_RSWTN * 4

; reg = adresse d'un tableau de l'etat (decalage au-dela de 4095)
macro oeadr reg*, off* {
	movl	reg,off
	add	reg,x19,reg
}

; ---------------------------------------------------------------------------
;  Interface (ak_enc.inc)
; ---------------------------------------------------------------------------

; x0 = encodeur (AKN_RATE, AKN_CH, AKN_QUALITY : debit en bits/s) -> 0 / < 0
opus_enc_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	ldr	x0,[x21,AKN_CH]
	sub	x0,x0,1
	cmp	x0,1
	b.hi	.fmt
	mov	x0,x21
	movl	x1,OE_SIZE
	bl	akn_alloc
	cbz	x0,.nomem
	mov	x19,x0
	add	x20,x19,OE_EC
	str	x19,[x21,AKN_CODEC]
	str	x21,[x19,OE_ENC]
	ldr	x0,[x21,AKN_CH]
	str	x0,[x19,OE_C]
	ldr	x0,[x21,AKN_RATE]
	str	x0,[x19,OE_RATE]
	; debit : paquets de debit / 400 octets
	ldr	x0,[x21,AKN_QUALITY]
	cbnz	x0,.q
	movl	x0,OE_DEFRATE
.q:	movl	x1,OE_MINRATE
	cmp	x0,x1
	csel	x0,x0,x1,hi
	movl	x1,OE_MAXRATE
	cmp	x0,x1
	csel	x0,x0,x1,lo
	mov	x1,400
	udiv	x2,x0,x1
	sub	x2,x2,1
	str	x2,[x19,OE_NBYTES]
	; largeur de bande d'apres le debit par voie : bande de fin, configuration
	ldr	x4,[x19,OE_C]
	udiv	x5,x0,x4
	mov	x6,21
	mov	x7,31
	movl	x8,40000
	cmp	x5,x8
	b.hs	.bw
	mov	x6,19
	mov	x7,27
	movl	x8,24000
	cmp	x5,x8
	b.hs	.bw
	mov	x6,17
	mov	x7,23
	movl	x8,14000
	cmp	x5,x8
	b.hs	.bw
	mov	x6,13
	mov	x7,19
.bw:	str	x6,[x19,OE_END]
	lsl	x7,x7,3
	cmp	x4,2
	b.ne	.t1
	orr	x7,x7,4
.t1:	str	x7,[x19,OE_TOC]
	; bande d'intensite : seuils de libopus (kbit/s) pris au double du debit
	; (moins de bandes en intensite : la forme d'onde garde mieux la stereo)
	mov	x1,500
	udiv	x5,x0,x1
	adr	x2,ope_ithr
	mov	x3,0
.it:	cmp	x3,21
	b.hs	.itd
	ldrh	w4,[x2,x3,lsl 1]
	cmp	x5,x4
	b.lo	.itd
	add	x3,x3,1
	b	.it
.itd:	str	x3,[x19,OE_ITGT]
	mov	x0,120
	str	x0,[x19,OE_PRESKIP]
	; plan de la MDCT (N = 1920 : FFT de 480 et rotations). ak_imdct_init
	; prend ses blocs par akd_alloc(decodeur) : on lui passe l'encodeur
	; decale pour que AKD_ALLOCS tombe sur AKN_ALLOCS (meme liste)
	sub	x0,x21,AKD_ALLOCS - AKN_ALLOCS
	add	x1,x19,OE_MD
	mov	x2,1920
	bl	ak_imdct_init
	tbnz	x0,63,.out
	; table du CRC d'Ogg (polynome 0x04C11DB7)
	oeadr	x0,OE_A_CRC
	mov	x1,0
.ct:	lsl	w2,w1,24
	mov	x3,8
.cb:	lsl	w4,w2,1
	tst	w2,0x80000000
	b.eq	.c0
	movl	w5,0x04C11DB7
	eor	w4,w4,w5
.c0:	mov	w2,w4
	subs	x3,x3,1
	b.ne	.cb
	str	w2,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,256
	b.lo	.ct
	ldr	x0,[x19,OE_RATE]
	movl	x1,48000
	cmp	x0,x1
	b.eq	.hd
	bl	ope_rs_init
.hd:	; OpusHead, OpusTags : une page chacun
	oeadr	x22,OE_A_PKT
	mov	x0,x22
	adr	x1,ope_head
	mov	x2,19
	bl	ak_memmove
	ldr	x0,[x19,OE_C]
	strb	w0,[x22,9]
	add	x0,x22,10
	ldr	x1,[x19,OE_PRESKIP]
	mov	x2,2
	bl	ope_putle
	add	x0,x22,12
	ldr	x1,[x19,OE_RATE]
	mov	x2,4
	bl	ope_putle
	mov	x0,x22
	mov	x1,19
	bl	ope_page_add
	mov	x0,0
	mov	x1,2				; debut du flux
	bl	ope_page_flush
	adr	x0,ope_tags
	mov	x1,30
	bl	ope_page_add
	mov	x0,0
	mov	x1,0
	bl	ope_page_flush
	adr	x0,opus_enc_push
	str	x0,[x21,AKN_PUSH]
	adr	x0,opus_enc_finish
	str	x0,[x21,AKN_FINISH]
	mov	x0,0
	b	.out
.fmt:	mov	x0,AKE_FORMAT
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur, x1 = PCM s16 entrelace, x2 = trames -> 0
opus_enc_push:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldr	x19,[x0,AKN_CODEC]
	add	x20,x19,OE_EC
	mov	x21,x1
	mov	x22,x2
	ldr	x0,[x19,OE_NIN]
	add	x0,x0,x22
	str	x0,[x19,OE_NIN]
	ldr	x0,[x19,OE_RATE]
	movl	x1,48000
	cmp	x0,x1
	b.ne	.rs
	ldr	x23,[x19,OE_C]
.l:	cbz	x22,.ok
	ldrsh	w0,[x21]
	scvtf	s0,w0
	fmov	s1,s0
	cmp	x23,2
	b.ne	.m
	ldrsh	w0,[x21,2]
	scvtf	s1,w0
.m:	add	x21,x21,x23,lsl 1
	ldr	x0,[x19,OE_LEN48]
	add	x0,x0,1
	str	x0,[x19,OE_LEN48]
	bl	ope_feed
	sub	x22,x22,1
	b	.l
.rs:	mov	x0,x21
	mov	x1,x22
	bl	ope_rs_push
.ok:	mov	x0,0
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur : derniere trame (zeros jusqu'a couvrir le retard), derniere
; page (granule final, fin du flux) -> 0
opus_enc_finish:
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x19,[x0,AKN_CODEC]
	add	x20,x19,OE_EC
	ldr	x0,[x19,OE_RATE]
	movl	x1,48000
	cmp	x0,x1
	b.eq	.z
	bl	ope_rs_flush
.z:	ldr	x0,[x19,OE_PKTS]
	mov	x1,OE_N
	mul	x0,x0,x1
	ldr	x1,[x19,OE_LEN48]
	ldr	x2,[x19,OE_PRESKIP]
	add	x1,x1,x2
	cmp	x0,x1
	b.hs	.zd
	movi	d0,0
	movi	d1,0
	bl	ope_feed
	b	.z
.zd:	ldr	x0,[x19,OE_LEN48]
	ldr	x1,[x19,OE_PRESKIP]
	add	x0,x0,x1
	mov	x1,4				; fin du flux
	bl	ope_page_flush
	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

; s0, s1 = echantillon (48 kHz, echelle 32768) des voies : pre-accentuation,
; tampon de la trame ; la trame pleine est encodee
ope_feed:
	ldr	x0,[x19,OE_FILL]
	oeadr	x1,OE_A_PCM
	add	x2,x0,120
	movl	w3,0x3F599A00			; 0,85000610
	fmov	s4,w3
	ldr	s2,[x19,OE_PRE]
	fsub	s3,s0,s2
	str	s3,[x1,x2,lsl 2]
	fmul	s2,s0,s4
	str	s2,[x19,OE_PRE]
	ldr	x3,[x19,OE_C]
	cmp	x3,2
	b.ne	.one
	movl	x3,OE_PCMN * 4
	add	x1,x1,x3
	ldr	s2,[x19,OE_PRE+4]
	fsub	s3,s1,s2
	str	s3,[x1,x2,lsl 2]
	fmul	s2,s1,s4
	str	s2,[x19,OE_PRE+4]
.one:	add	x0,x0,1
	str	x0,[x19,OE_FILL]
	cmp	x0,OE_N
	b.lo	.r
	str	xzr,[x19,OE_FILL]
	b	ope_frame
.r:	ret

; ---------------------------------------------------------------------------
;  Ogg (RFC 3533, 7845)
; ---------------------------------------------------------------------------

; x0 = octets, x1 = longueur : ajoute un paquet a la page (une page pleine
; part d'abord)
ope_page_add:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x2,255
	udiv	x3,x22,x2
	add	x3,x3,1				; valeurs de lacage
	ldr	x4,[x19,OE_NSEG]
	add	x4,x4,x3
	cmp	x4,255
	b.hi	.fl
	ldr	x4,[x19,OE_NPKT]
	cmp	x4,OE_PAGEPK
	b.lo	.add
.fl:	ldr	x0,[x19,OE_PKTS]
	mov	x1,OE_N
	mul	x0,x0,x1
	mov	x1,0
	bl	ope_page_flush
.add:	oeadr	x5,OE_A_PAGE+27
	ldr	x4,[x19,OE_NSEG]
	mov	x6,x22
.lc:	cmp	x6,255
	b.lo	.lce
	mov	w7,255
	strb	w7,[x5,x4]
	add	x4,x4,1
	sub	x6,x6,255
	b	.lc
.lce:	strb	w6,[x5,x4]
	add	x4,x4,1
	str	x4,[x19,OE_NSEG]
	oeadr	x0,OE_A_PDATA
	ldr	x1,[x19,OE_DLEN]
	add	x0,x0,x1
	add	x1,x1,x22
	str	x1,[x19,OE_DLEN]
	mov	x1,x21
	mov	x2,x22
	bl	ak_memmove
	ldr	x0,[x19,OE_NPKT]
	add	x0,x0,1
	str	x0,[x19,OE_NPKT]
	ldp	x21,x22,[sp],16
	epilogue

; x0 = granule, x1 = drapeaux (2 debut, 4 fin) : ecrit la page en cours
ope_page_flush:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x22,x0
	mov	x23,x1
	oeadr	x21,OE_A_PAGE
	movl	w0,0x5367674F			; 'OggS'
	str	w0,[x21]
	strb	wzr,[x21,4]
	strb	w23,[x21,5]
	add	x0,x21,6
	mov	x1,x22
	mov	x2,8
	bl	ope_putle
	add	x0,x21,14
	movl	x1,OE_SERIAL
	mov	x2,4
	bl	ope_putle
	add	x0,x21,18
	ldr	x1,[x19,OE_SEQ]
	mov	x2,4
	bl	ope_putle
	add	x0,x21,22
	mov	x1,0
	mov	x2,4
	bl	ope_putle
	ldr	x0,[x19,OE_NSEG]
	strb	w0,[x21,26]
	mov	w0,0				; CRC de l'en-tete (CRC a 0) et des donnees
	mov	x1,x21
	ldr	x2,[x19,OE_NSEG]
	add	x2,x2,27
	bl	ope_crc
	oeadr	x1,OE_A_PDATA
	ldr	x2,[x19,OE_DLEN]
	bl	ope_crc
	mov	x1,x0
	add	x0,x21,22
	mov	x2,4
	bl	ope_putle
	ldr	x0,[x19,OE_ENC]
	mov	x1,x21
	ldr	x2,[x19,OE_NSEG]
	add	x2,x2,27
	bl	akn_out
	ldr	x0,[x19,OE_ENC]
	oeadr	x1,OE_A_PDATA
	ldr	x2,[x19,OE_DLEN]
	bl	akn_out
	ldr	x0,[x19,OE_SEQ]
	add	x0,x0,1
	str	x0,[x19,OE_SEQ]
	str	xzr,[x19,OE_NSEG]
	str	xzr,[x19,OE_NPKT]
	str	xzr,[x19,OE_DLEN]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = adresse, x1 = valeur, x2 = octets : petit-boutiste, octet par octet.
; Ne touche que x0-x2.
ope_putle:
	cbz	x2,.r
	strb	w1,[x0],1
	lsr	x1,x1,8
	sub	x2,x2,1
	b	ope_putle
.r:	ret

; w0 = CRC, x1 = octets, x2 = longueur -> w0 = CRC (Ogg). Ne touche que x0-x5.
ope_crc:
	oeadr	x3,OE_A_CRC
.l:	cbz	x2,.r
	ldrb	w4,[x1],1
	eor	w5,w4,w0,lsr 24
	ldr	w5,[x3,w5,uxtw 2]
	eor	w0,w5,w0,lsl 8
	sub	x2,x2,1
	b	.l
.r:	ret

; ---------------------------------------------------------------------------
;  Codeur de plage (RFC 6716 5.1). Les fonctions ope_ec_* ne touchent que
;  x0-x7 (x0 = codeur) : l'appelant garde ses valeurs dans x8-x17.
; ---------------------------------------------------------------------------

; x0 = codeur, x1 = tampon, x2 = octets
ope_ec_init:
	str	x1,[x0,OEC_BUF]
	str	x2,[x0,OEC_STORAGE]
	str	xzr,[x0,OEC_OFFS]
	str	xzr,[x0,OEC_ENDOFFS]
	str	xzr,[x0,OEC_WIN]
	str	xzr,[x0,OEC_NEND]
	mov	x3,33				; EC_CODE_BITS + 1
	str	x3,[x0,EC_NBITS]
	movl	x3,0x80000000
	str	x3,[x0,EC_RNG]
	str	xzr,[x0,OEC_VAL]
	str	xzr,[x0,OEC_EXT]
	mov	x3,-1
	str	x3,[x0,OEC_REM]
	str	xzr,[x0,OEC_ERR]
	ret

; x0 = codeur, w4 = octet : ecrit par l'avant. Ne touche que x5-x7.
ope_ec_wb:
	ldr	x5,[x0,OEC_OFFS]
	ldr	x6,[x0,OEC_ENDOFFS]
	add	x6,x6,x5
	ldr	x7,[x0,OEC_STORAGE]
	cmp	x6,x7
	b.hs	.err
	ldr	x7,[x0,OEC_BUF]
	strb	w4,[x7,x5]
	add	x5,x5,1
	str	x5,[x0,OEC_OFFS]
	ret
.err:	mov	x5,1
	str	x5,[x0,OEC_ERR]
	ret

; x0 = codeur, w4 = octet : ecrit par la fin. Ne touche que x5-x7.
ope_ec_wbe:
	ldr	x5,[x0,OEC_OFFS]
	ldr	x6,[x0,OEC_ENDOFFS]
	add	x5,x6,x5
	ldr	x7,[x0,OEC_STORAGE]
	cmp	x5,x7
	b.hs	.err
	add	x6,x6,1
	str	x6,[x0,OEC_ENDOFFS]
	sub	x5,x7,x6
	ldr	x7,[x0,OEC_BUF]
	strb	w4,[x7,x5]
	ret
.err:	mov	x5,1
	str	x5,[x0,OEC_ERR]
	ret

; x0 = codeur, w1 = symbole (9 bits : retenue en bit 8) (ec_enc_carry_out)
ope_ec_carry:
	cmp	w1,255
	b.ne	.n
	ldr	x2,[x0,OEC_EXT]
	add	x2,x2,1
	str	x2,[x0,OEC_EXT]
	ret
.n:	stp	x29,x30,[sp,-16]!
	lsr	w2,w1,8				; retenue
	ldr	x3,[x0,OEC_REM]
	tbnz	x3,63,.x
	add	w4,w3,w2
	bl	ope_ec_wb
.x:	ldr	x3,[x0,OEC_EXT]
	cbz	x3,.r
	add	w4,w2,255
.e:	bl	ope_ec_wb
	subs	x3,x3,1
	b.ne	.e
	str	xzr,[x0,OEC_EXT]
.r:	and	w1,w1,255
	str	x1,[x0,OEC_REM]
	ldp	x29,x30,[sp],16
	ret

; x0 = codeur : renormalise (rng > 2^23)
ope_ec_norm:
	ldr	w1,[x0,EC_RNG]
	cmp	w1,0x800,lsl 12
	b.hi	.ret
	stp	x29,x30,[sp,-16]!
.l:	ldr	x1,[x0,OEC_VAL]
	lsr	x1,x1,23
	bl	ope_ec_carry
	ldr	x1,[x0,OEC_VAL]
	lsl	x1,x1,8
	and	x1,x1,0x7FFFFFFF
	str	x1,[x0,OEC_VAL]
	ldr	w1,[x0,EC_RNG]
	lsl	w1,w1,8
	str	x1,[x0,EC_RNG]
	ldr	x1,[x0,EC_NBITS]
	add	x1,x1,8
	str	x1,[x0,EC_NBITS]
	ldr	w1,[x0,EC_RNG]
	cmp	w1,0x800,lsl 12
	b.ls	.l
	ldp	x29,x30,[sp],16
.ret:	ret

; x0 = codeur, w1 = fl, w2 = fh, w3 = ft (ec_encode)
ope_ec_encode:
	ldr	w4,[x0,EC_RNG]
	udiv	w5,w4,w3			; r
; (w4 = rng, w5 = r)
ope_ec_enc_r:
	cbz	w1,.z
	sub	w6,w3,w1
	mul	w6,w5,w6
	sub	w6,w4,w6			; rng - r (ft - fl)
	ldr	x7,[x0,OEC_VAL]
	add	x7,x7,w6,uxtw
	str	x7,[x0,OEC_VAL]
	sub	w6,w2,w1
	mul	w4,w5,w6
	b	.st
.z:	sub	w6,w3,w2
	mul	w6,w5,w6
	sub	w4,w4,w6
.st:	str	x4,[x0,EC_RNG]
	b	ope_ec_norm

; x0 = codeur, w1 = fl, w2 = fh, w3 = bits (ec_encode_bin)
ope_ec_encode_bin:
	ldr	w4,[x0,EC_RNG]
	lsr	w5,w4,w3
	mov	w6,1
	lsl	w3,w6,w3
	b	ope_ec_enc_r

; x0 = codeur, w1 = bit, w2 = logp (probabilite de 1 : 1/2^logp)
ope_ec_bit_logp:
	ldr	w4,[x0,EC_RNG]
	lsr	w5,w4,w2			; s
	sub	w4,w4,w5			; r
	cbz	w1,.z
	ldr	x6,[x0,OEC_VAL]
	add	x6,x6,w4,uxtw
	str	x6,[x0,OEC_VAL]
	mov	w4,w5
.z:	str	x4,[x0,EC_RNG]
	b	ope_ec_norm

; x0 = codeur, w1 = symbole, x2 = table icdf (octets), w3 = ftb
ope_ec_icdf:
	ldr	w4,[x0,EC_RNG]
	lsr	w5,w4,w3			; r
	cbz	w1,.z
	sub	w6,w1,1
	ldrb	w6,[x2,w6,uxtw]			; icdf[s - 1]
	mul	w7,w5,w6
	sub	w7,w4,w7
	ldr	x3,[x0,OEC_VAL]
	add	x3,x3,w7,uxtw
	str	x3,[x0,OEC_VAL]
	ldrb	w7,[x2,w1,uxtw]
	sub	w6,w6,w7
	mul	w4,w5,w6
	b	.st
.z:	ldrb	w6,[x2]
	mul	w6,w5,w6
	sub	w4,w4,w6
.st:	str	x4,[x0,EC_RNG]
	b	ope_ec_norm

; x0 = codeur, w1 = valeur, w2 = ft (> 1) : entier uniforme (ec_enc_uint)
ope_ec_uint:
	stp	x29,x30,[sp,-32]!
	sub	w2,w2,1				; ft - 1
	clz	w3,w2
	mov	w4,32
	sub	w3,w4,w3			; ftb
	cmp	w3,8
	b.hi	.big
	add	w3,w2,1
	add	w2,w1,1
	bl	ope_ec_encode
	ldp	x29,x30,[sp],32
	ret
.big:	sub	w3,w3,8				; bits bruts
	stp	x1,x3,[sp,16]
	lsr	w4,w2,w3
	add	w4,w4,1				; ft1
	lsr	w1,w1,w3			; fl1
	add	w2,w1,1
	mov	w3,w4
	bl	ope_ec_encode
	ldp	x1,x3,[sp,16]
	mov	w2,1
	lsl	w2,w2,w3
	sub	w2,w2,1
	and	w1,w1,w2
	mov	w2,w3
	bl	ope_ec_bits
	ldp	x29,x30,[sp],32
	ret

; x0 = codeur, w1 = valeur, w2 = bits (<= 25) : bits bruts par la fin
ope_ec_bits:
	stp	x29,x30,[sp,-32]!
	stp	x1,x2,[sp,16]
	ldr	x3,[x0,OEC_WIN]
	ldr	x2,[x0,OEC_NEND]
	ldr	x1,[sp,24]
	add	x1,x1,x2
	cmp	x1,32
	b.ls	.nf
.f:	and	w4,w3,255
	bl	ope_ec_wbe
	lsr	x3,x3,8
	sub	x2,x2,8
	cmp	x2,8
	b.hs	.f
.nf:	ldp	x1,x4,[sp,16]
	lsl	x1,x1,x2
	orr	x3,x3,x1
	add	x2,x2,x4
	str	x3,[x0,OEC_WIN]
	str	x2,[x0,OEC_NEND]
	ldr	x1,[x0,EC_NBITS]
	add	x1,x1,x4
	str	x1,[x0,EC_NBITS]
	ldp	x29,x30,[sp],32
	ret

; x0 = codeur : fin (ec_enc_done) ; les octets libres du milieu a zero
ope_ec_done:
	stp	x29,x30,[sp,-48]!
	ldr	w1,[x0,EC_RNG]
	clz	w2,w1				; l = 32 - ilog(rng)
	movl	w3,0x7FFFFFFF
	lsr	w3,w3,w2			; msk
	ldr	x4,[x0,OEC_VAL]
	add	x5,x4,x3
	bic	x5,x5,x3			; end
	orr	x6,x5,x3
	add	x7,x4,x1
	cmp	x6,x7
	b.lo	.ok
	add	w2,w2,1
	lsr	w3,w3,1
	add	x5,x4,x3
	bic	x5,x5,x3
.ok:	sxtw	x2,w2
	str	x2,[sp,16]
	str	x5,[sp,24]
.cl:	ldr	x2,[sp,16]
	cmp	x2,0
	b.le	.cd
	ldr	x5,[sp,24]
	lsr	x1,x5,23
	lsl	x5,x5,8
	and	x5,x5,0x7FFFFFFF
	str	x5,[sp,24]
	sub	x2,x2,8
	str	x2,[sp,16]
	bl	ope_ec_carry
	b	.cl
.cd:	ldr	x3,[x0,OEC_REM]
	tbz	x3,63,.c0
	ldr	x3,[x0,OEC_EXT]
	cbz	x3,.nc
.c0:	mov	w1,0
	bl	ope_ec_carry
.nc:	ldr	x3,[x0,OEC_WIN]			; bits bruts restants
	ldr	x2,[x0,OEC_NEND]
.w:	cmp	x2,8
	b.lo	.wd
	and	w4,w3,255
	bl	ope_ec_wbe
	lsr	x3,x3,8
	sub	x2,x2,8
	b	.w
.wd:	ldr	x1,[x0,OEC_ERR]
	cbnz	x1,.out
	ldr	x4,[x0,OEC_OFFS]		; le milieu a zero
	ldr	x5,[x0,OEC_ENDOFFS]
	ldr	x6,[x0,OEC_STORAGE]
	ldr	x7,[x0,OEC_BUF]
	sub	x1,x6,x5
.zl:	cmp	x4,x1
	b.hs	.zd
	strb	wzr,[x7,x4]
	add	x4,x4,1
	b	.zl
.zd:	cbz	x2,.out
	cmp	x5,x6
	b.hs	.e1
	ldr	x1,[sp,16]
	neg	x1,x1				; bits libres du dernier octet
	ldr	x4,[x0,OEC_OFFS]
	add	x4,x4,x5
	cmp	x4,x6
	b.lo	.nm
	cmp	x1,x2
	b.hs	.nm
	mov	x4,1
	lsl	x4,x4,x1
	sub	x4,x4,1
	and	x3,x3,x4
	mov	x4,1
	str	x4,[x0,OEC_ERR]
.nm:	sub	x4,x6,x5
	sub	x4,x4,1
	ldrb	w1,[x7,x4]
	orr	w1,w1,w3
	strb	w1,[x7,x4]
	b	.out
.e1:	mov	x1,1
	str	x1,[x0,OEC_ERR]
.out:	ldp	x29,x30,[sp],48
	ret

; x0 = codeur, w1 = valeur, w2 = fs (probabilite de 0, Q15), w3 =
; decroissance -> w0 = valeur codee (bornee) (Laplace, RFC 6716 4.3.2.1).
; Ne touche que x0-x15.
ope_laplace:
	stp	x29,x30,[sp,-32]!
	mov	w4,0				; fl
	cbz	w1,.enc
	asr	w5,w1,31			; s
	add	w6,w1,w5
	eor	w6,w6,w5			; |valeur|
	mov	w4,w2				; fl = fs
	mov	w7,32768-32
	sub	w7,w7,w2
	mov	w8,16384
	sub	w8,w8,w3
	mul	w7,w7,w8
	lsr	w7,w7,15			; fs (sans la probabilite minimale)
	mov	w9,1				; i
.l:	cbz	w7,.tail
	cmp	w9,w6
	b.ge	.lend
	lsl	w7,w7,1
	add	w4,w4,w7
	add	w4,w4,2
	mul	w7,w7,w3
	lsr	w7,w7,15
	add	w9,w9,1
	b	.l
.lend:	add	w7,w7,1
	bic	w10,w7,w5
	add	w4,w4,w10
	mov	w2,w7
	b	.enc
.tail:	mov	w10,32768			; probabilite minimale : borne
	sub	w10,w10,w4
	sub	w10,w10,w5
	asr	w10,w10,1			; ndi_max
	sub	w11,w6,w9
	sub	w12,w10,1
	cmp	w11,w12
	csel	w11,w11,w12,lt			; di
	lsl	w12,w11,1
	add	w12,w12,1
	add	w12,w12,w5
	add	w4,w4,w12
	mov	w12,32768
	sub	w12,w12,w4
	mov	w2,1
	cmp	w12,1
	csel	w2,w12,w2,lt
	add	w1,w9,w11
	add	w1,w1,w5
	eor	w1,w1,w5
.enc:	str	x1,[sp,16]
	add	w2,w4,w2
	mov	w1,w4
	mov	w3,15
	bl	ope_ec_encode_bin
	ldr	x0,[sp,16]
	ldp	x29,x30,[sp],32
	ret

; ---------------------------------------------------------------------------
;  Trame
; ---------------------------------------------------------------------------

; la trame (OE_A_PCM) -> un paquet dans la page
ope_frame:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	ldr	x22,[x19,OE_END]
	ldr	x23,[x19,OE_C]
	; MDCT de chaque voie ; le recouvrement passe au debut du tampon
	mov	x21,0
.md:	cmp	x21,x23
	b.hs	.mdd
	movl	x0,OE_PCMN * 4
	mul	x0,x0,x21
	movl	x1,OE_A_PCM
	add	x0,x0,x1
	add	x24,x19,x0
	movl	x0,960 * 4
	mul	x0,x0,x21
	movl	x1,OE_A_FREQ
	add	x0,x0,x1
	add	x1,x19,x0
	mov	x0,x24
	bl	ope_mdct
	mov	x0,x24
	add	x1,x24,960*4
	mov	x2,120*4
	bl	ak_memmove
	add	x21,x21,1
	b	.md
.mdd:	bl	ope_bandse
	; largeur de bande du signal : derniere bande dont l'amplitude moyenne
	; par coefficient depasse 1/4 (plus de 10 dB au-dessus du bruit du s16) ;
	; les bandes au-dela sont sautees (allocation)
	str	xzr,[x19,OE_SIGBW]
	adr	x9,ce_eband
	mov	x11,0
.sb:	cmp	x11,x22
	b.hs	.sbd
	ldrh	w0,[x9,x11,lsl 1]
	add	x1,x11,1
	ldrh	w1,[x9,x1,lsl 1]
	sub	w0,w1,w0
	lsl	w0,w0,OE_LM
	ucvtf	s1,w0
	movl	w0,0x3D800000			; N / 16
	fmov	s2,w0
	fmul	s1,s1,s2
	mov	x10,0
.sbc:	cmp	x10,x23
	b.hs	.sbn
	mov	x0,CE_NBE
	madd	x0,x10,x0,x11
	add	x1,x19,OE_BANDE
	ldr	s0,[x1,x0,lsl 2]
	fmul	s0,s0,s0
	fcmp	s0,s1
	b.le	.sbx
	str	x11,[x19,OE_SIGBW]
.sbx:	add	x10,x10,1
	b	.sbc
.sbn:	add	x11,x11,1
	b	.sb
.sbd:
	mov	x0,x20
	oeadr	x1,OE_A_PKT+1
	ldr	x2,[x19,OE_NBYTES]
	bl	ope_ec_init
	ldr	x26,[x19,OE_NBYTES]
	lsl	x26,x26,3			; total_bits
	; silence (0), post-filtre (0), transitoire (0), intra (premiere trame)
	mov	x0,x20
	mov	w1,0
	mov	w2,15
	bl	ope_ec_bit_logp
	mov	x0,x20
	bl	ec_tell
	add	x0,x0,16
	cmp	x0,x26
	b.gt	.npf
	mov	x0,x20
	mov	w1,0
	mov	w2,1
	bl	ope_ec_bit_logp
.npf:	mov	x0,x20
	bl	ec_tell
	add	x0,x0,3
	cmp	x0,x26
	b.gt	.ntr
	mov	x0,x20
	mov	w1,0
	mov	w2,3
	bl	ope_ec_bit_logp
.ntr:	ldr	x0,[x19,OE_FRAMES]
	cmp	x0,0
	cset	x27,eq
	mov	x0,x20
	bl	ec_tell
	add	x0,x0,3
	cmp	x0,x26
	b.le	.in
	mov	x27,0
	b	.ind
.in:	mov	x0,x20
	mov	w1,w27
	mov	w2,3
	bl	ope_ec_bit_logp
.ind:	str	x27,[x19,OE_INTRA]
	bl	ope_coarse
	; tf_res : 0 partout (pas de tf_select : il ne change rien sans transitoire)
	mov	x0,x20
	bl	ec_tell
	mov	x9,x26				; budget
	add	x0,x0,5
	cmp	x0,x9
	cset	x1,ls				; tf_select_rsv
	sub	x9,x9,x1
	mov	x10,4				; logp
	mov	x11,0
.tf:	cmp	x11,x22
	b.hs	.tfd
	mov	x0,x20
	bl	ec_tell
	add	x0,x0,x10
	cmp	x0,x9
	b.gt	.tfn
	mov	x0,x20
	mov	w1,0
	mov	w2,w10
	bl	ope_ec_bit_logp
.tfn:	mov	x10,5
	add	x11,x11,1
	b	.tf
.tfd:	; etalement normal
	mov	x0,x20
	bl	ec_tell
	add	x0,x0,4
	cmp	x0,x26
	b.gt	.nsp
	mov	x0,x20
	mov	w1,2
	adr	x2,ce_spread_icdf
	mov	w3,5
	bl	ope_ec_icdf
.nsp:	; plafonds : (caps + 64) C N / 4
	adr	x9,ce_eband
	adr	x10,ce_ccaps
	mov	x0,OE_LM*2
	add	x0,x0,x23
	sub	x0,x0,1
	mov	x1,CE_NBE
	mul	x0,x0,x1
	add	x10,x10,x0
	mov	x11,0
.cap:	ldrh	w0,[x9,x11,lsl 1]
	add	x1,x11,1
	ldrh	w1,[x9,x1,lsl 1]
	sub	w0,w1,w0
	lsl	w0,w0,OE_LM
	ldrb	w1,[x10,x11]
	add	w1,w1,64
	mul	w1,w1,w23
	mul	w1,w1,w0
	lsr	w1,w1,2
	add	x2,x19,OE_CAP
	str	w1,[x2,x11,lsl 2]
	add	x11,x11,1
	cmp	x11,CE_NBE
	b.lo	.cap
	; dynalloc : analyse, puis les quanta voulus (tant qu'ils tiennent)
	bl	ope_dynalloc
	lsl	x26,x26,3			; total (1/8 bits)
	mov	x0,x20
	bl	ec_tell_frac
	mov	x27,x0				; tell
	mov	x12,6				; dynalloc_logp
	mov	x11,0
	adr	x9,ce_eband
.dy:	cmp	x11,x22
	b.hs	.dyd
	ldrh	w0,[x9,x11,lsl 1]
	add	x1,x11,1
	ldrh	w1,[x9,x1,lsl 1]
	sub	w0,w1,w0
	mul	w0,w0,w23
	lsl	w0,w0,OE_LM			; width
	lsl	w1,w0,3
	mov	w2,48
	cmp	w0,w2
	csel	w2,w0,w2,gt
	cmp	w1,w2
	csel	w13,w1,w2,lt			; quanta
	mov	x14,x12				; logp
	mov	x15,0				; boost
	mov	x16,0				; j
	add	x2,x19,OE_CAP
	ldr	w17,[x2,x11,lsl 2]
	add	x2,x19,OE_WANT
	ldr	w8,[x2,x11,lsl 2]
.dl:	add	x0,x27,x14,lsl 3
	cmp	x0,x26
	b.ge	.dle
	cmp	x15,x17
	b.ge	.dle
	cmp	x16,x8
	cset	x10,lt				; drapeau
	mov	x0,x20
	mov	w1,w10
	mov	w2,w14
	bl	ope_ec_bit_logp
	mov	x0,x20
	bl	ec_tell_frac
	mov	x27,x0
	cbz	x10,.dle
	add	x15,x15,x13
	sub	x26,x26,x13
	mov	x14,1
	add	x16,x16,1
	b	.dl
.dle:	add	x2,x19,OE_OFFS
	str	w15,[x2,x11,lsl 2]
	cbz	x16,.dn
	sub	x12,x12,1
	cmp	x12,2
	mov	x0,2
	csel	x12,x0,x12,lt
.dn:	add	x11,x11,1
	b	.dy
.dyd:	str	x26,[x19,OE_TOT8]
	; trim
	bl	ope_trim
	str	x0,[x19,OE_TRIM]
	mov	x0,x20
	bl	ec_tell_frac
	add	x0,x0,48
	cmp	x0,x26
	b.gt	.ntrim
	mov	x0,x20
	ldr	x1,[x19,OE_TRIM]
	adr	x2,ce_trim_icdf
	mov	w3,7
	bl	ope_ec_icdf
	b	.al
.ntrim:	mov	x0,5
	str	x0,[x19,OE_TRIM]
.al:	; allocation : bits restants (1/8), sans reserve d'anti-effondrement
	mov	x0,x20
	bl	ec_tell_frac
	ldr	x1,[x19,OE_NBYTES]
	lsl	x1,x1,6
	sub	x1,x1,x0
	sub	x1,x1,1
	bl	ope_alloc
	bl	ope_fine
	bl	ope_bands
	bl	ope_final
	mov	x0,x20
	bl	ope_ec_done
	; paquet : TOC + trame
	oeadr	x0,OE_A_PKT
	ldr	x1,[x19,OE_TOC]
	strb	w1,[x0]
	ldr	x1,[x19,OE_NBYTES]
	add	x1,x1,1
	bl	ope_page_add
	ldr	x0,[x19,OE_PKTS]
	add	x0,x0,1
	str	x0,[x19,OE_PKTS]
	ldr	x0,[x19,OE_FRAMES]
	add	x0,x0,1
	str	x0,[x19,OE_FRAMES]
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = entree (OE_PCMN : recouvrement + trame), x1 = sortie (960) : MDCT
; de la fenetre de CELT, comme si la trame de 1920 points etait [420 zeros,
; entree fenetree, 420 zeros] ; pliage en u (960), puis la DCT-IV par une
; FFT de 480 points : z = (u[2k] + i u[959-2k]) e^(-i t), Z = FFT(z) e^(-i t),
; X[2k] = Re Z, X[959-2k] = -Im Z, t = 2 pi (k + 1/8) / 1920 ; echelle 1/480
; (la synthese du decodeur rend alors l'entree)
ope_mdct:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	oeadr	x23,OE_A_V
	mov	x0,x23
	mov	x1,1920*4
	bl	ak_memzero
	adr	x2,ce_window
	add	x3,x23,420*4
	mov	x4,0
.w1:	ldr	s0,[x21,x4,lsl 2]
	ldr	s1,[x2,x4,lsl 2]
	fmul	s0,s0,s1
	str	s0,[x3,x4,lsl 2]
	add	x4,x4,1
	cmp	x4,120
	b.lo	.w1
.w2:	ldr	s0,[x21,x4,lsl 2]
	str	s0,[x3,x4,lsl 2]
	add	x4,x4,1
	cmp	x4,960
	b.lo	.w2
	mov	x5,1079
.w3:	ldr	s0,[x21,x4,lsl 2]
	sub	x6,x5,x4
	ldr	s1,[x2,x6,lsl 2]
	fmul	s0,s0,s1
	str	s0,[x3,x4,lsl 2]
	add	x4,x4,1
	cmp	x4,OE_PCMN
	b.lo	.w3
	add	x24,x19,OE_MD
	ldr	x5,[x24,MD_TW]
	ldr	x6,[x24,MD_Z]
	mov	x7,0
.z:	lsl	x8,x7,1
	bl	.u
	fmov	s2,s0				; re
	mov	x9,959
	lsl	x8,x7,1
	sub	x8,x9,x8
	bl	.u
	fmov	s3,s0				; im
	add	x9,x5,x7,lsl 3
	ldp	s4,s5,[x9]			; cos, sin
	fmul	s6,s2,s4
	fmadd	s6,s3,s5,s6			; re c + im s
	fmul	s7,s3,s4
	fmsub	s7,s2,s5,s7			; im c - re s
	add	x9,x6,x7,lsl 3
	stp	s6,s7,[x9]
	add	x7,x7,1
	cmp	x7,480
	b.lo	.z
	add	x0,x24,MD_FFT
	ldr	x1,[x24,MD_Z]
	ldr	x2,[x24,MD_Z2]
	bl	ak_fft
	ldr	x5,[x24,MD_TW]
	ldr	x6,[x24,MD_Z2]
	movl	w0,0x3B088889			; 1/480
	fmov	s16,w0
	mov	x7,0
.p:	add	x9,x6,x7,lsl 3
	ldp	s0,s1,[x9]
	add	x9,x5,x7,lsl 3
	ldp	s2,s3,[x9]
	fmul	s4,s0,s2
	fmadd	s4,s1,s3,s4
	fmul	s5,s1,s2
	fmsub	s5,s0,s3,s5
	fmul	s4,s4,s16
	fmul	s5,s5,s16
	fneg	s5,s5
	lsl	x8,x7,1
	str	s4,[x22,x8,lsl 2]
	mov	x9,959
	sub	x8,x9,x8
	str	s5,[x22,x8,lsl 2]
	add	x7,x7,1
	cmp	x7,480
	b.lo	.p
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue
; x8 = n -> s0 = u[n] (u[n] = -v[1439-n] - v[1440+n] sous 480,
; v[n-480] - v[1439-n] au-dela). Ne touche que x9, x10, v0, v1.
.u:	cmp	x8,480
	b.hs	.u2
	mov	x9,1439
	sub	x9,x9,x8
	ldr	s0,[x23,x9,lsl 2]
	add	x10,x8,1440
	ldr	s1,[x23,x10,lsl 2]
	fadd	s0,s0,s1
	fneg	s0,s0
	ret
.u2:	sub	x9,x8,480
	ldr	s0,[x23,x9,lsl 2]
	mov	x10,1439
	sub	x10,x10,x8
	ldr	s1,[x23,x10,lsl 2]
	fsub	s0,s0,s1
	ret

; energies des bandes : amplitudes (OE_BANDE), log2 moins eMeans (OE_LOGE),
; spectres normalises (OE_A_X)
ope_bandse:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	ldr	x22,[x19,OE_END]
	ldr	x23,[x19,OE_C]
	mov	x21,0
.c:	cmp	x21,x23
	b.hs	.out
	movl	x0,960 * 4
	mul	x0,x0,x21
	movl	x1,OE_A_FREQ
	add	x1,x1,x0
	add	x24,x19,x1
	movl	x1,OE_A_X
	add	x1,x1,x0
	add	x25,x19,x1
	mov	x26,0
.i:	cmp	x26,x22
	b.hs	.nc
	adr	x9,ce_eband
	ldrh	w10,[x9,x26,lsl 1]
	add	x11,x26,1
	ldrh	w11,[x9,x11,lsl 1]
	lsl	w10,w10,OE_LM
	lsl	w11,w11,OE_LM
	movl	w0,0x129E74D2			; 1e-27
	fmov	s0,w0
	mov	x1,x10
.s:	ldr	s1,[x24,x1,lsl 2]
	fmadd	s0,s1,s1,s0
	add	x1,x1,1
	cmp	x1,x11
	b.lo	.s
	fsqrt	s8,s0
	movl	w0,0x129E74D2
	fmov	s1,w0
	fadd	s1,s8,s1
	fmov	s2,1.0
	fdiv	s1,s2,s1
	mov	x1,x10
.n:	ldr	s2,[x24,x1,lsl 2]
	fmul	s2,s2,s1
	str	s2,[x25,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,x11
	b.lo	.n
	mov	x0,CE_NBE
	madd	x9,x21,x0,x26
	add	x0,x19,OE_BANDE
	str	s8,[x0,x9,lsl 2]
	fmov	s0,s8
	bl	ope_log2
	adr	x0,ce_emeans
	ldr	s1,[x0,x26,lsl 2]
	fsub	s0,s0,s1
	mov	x0,CE_NBE
	madd	x9,x21,x0,x26
	add	x0,x19,OE_LOGE
	str	s0,[x0,x9,lsl 2]
	add	x26,x26,1
	b	.i
.nc:	add	x21,x21,1
	b	.c
.out:	ldp	d8,d9,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; s0 = x (> 0) -> s0 = log2 x. Ne touche que x0-x2, d0-d5.
ope_log2:
	fcvt	d0,s0
	fmov	x0,d0
	lsr	x1,x0,52
	and	x1,x1,0x7FF
	sub	x1,x1,1023			; exposant
	and	x0,x0,0x000FFFFFFFFFFFFF
	orr	x0,x0,0x3FF0000000000000
	fmov	d0,x0				; m dans [1, 2)
	adr	x2,.c
	ldr	d1,[x2]
	fcmp	d0,d1
	b.le	.n
	fmov	d2,0.5
	fmul	d0,d0,d2
	add	x1,x1,1
.n:	fmov	d1,1.0				; s = (m - 1) / (m + 1)
	fsub	d2,d0,d1
	fadd	d3,d0,d1
	fdiv	d2,d2,d3
	fmul	d3,d2,d2
	ldr	d4,[x2,16]			; 1 + s^2/3 + s^4/5 + ... + s^10/11
	ldr	d5,[x2,24]
	fmadd	d4,d4,d3,d5
	ldr	d5,[x2,32]
	fmadd	d4,d4,d3,d5
	ldr	d5,[x2,40]
	fmadd	d4,d4,d3,d5
	ldr	d5,[x2,48]
	fmadd	d4,d4,d3,d5
	fmadd	d4,d4,d3,d1
	fmul	d2,d2,d4
	ldr	d5,[x2,8]			; 2 / ln 2
	fmul	d2,d2,d5
	scvtf	d1,x1
	fadd	d0,d1,d2
	fcvt	s0,d0
	ret
		align	8
.c:	dd	1.4142135623730951, 2.8853900817779268
	dd	0.09090909090909091, 0.1111111111111111, 0.14285714285714285
	dd	0.2, 0.3333333333333333

; s0 = y >= 0, s1 = x > 0 -> s0 = atan2(y, x) (0..pi/2). Ne touche que
; x0-x1, d0-d7.
ope_atan2:
	fcvt	d0,s0
	fcvt	d1,s1
	mov	x1,0
	fcmp	d0,d1
	b.le	.a
	fmov	d2,d0				; y > x : pi/2 - atan(x / y)
	fmov	d0,d1
	fmov	d1,d2
	mov	x1,1
.a:	fdiv	d0,d0,d1			; t dans [0, 1]
	adr	x0,.c
	movi	d6,0
	ldr	d2,[x0]				; tan(pi/12)
	fcmp	d0,d2
	b.le	.s
	ldr	d3,[x0,8]			; atan t = pi/6 + atan((t - 1/r3) / (1 + t/r3))
	fsub	d4,d0,d3
	fmul	d5,d0,d3
	fmov	d7,1.0
	fadd	d5,d5,d7
	fdiv	d0,d4,d5
	ldr	d6,[x0,16]
.s:	fmul	d2,d0,d0
	ldr	d3,[x0,24]
	ldr	d4,[x0,32]
	fmadd	d3,d3,d2,d4
	ldr	d4,[x0,40]
	fmadd	d3,d3,d2,d4
	ldr	d4,[x0,48]
	fmadd	d3,d3,d2,d4
	ldr	d4,[x0,56]
	fmadd	d3,d3,d2,d4
	fmov	d4,1.0
	fmadd	d3,d3,d2,d4
	fmul	d0,d0,d3
	fadd	d0,d0,d6
	cbz	x1,.r
	ldr	d2,[x0,64]
	fsub	d0,d2,d0
.r:	fcvt	s0,d0
	ret
		align	8
.c:	dd	0.2679491924311227, 0.5773502691896258, 0.5235987755982988
	dd	-0.09090909090909091, 0.1111111111111111, -0.14285714285714285
	dd	0.2, -0.3333333333333333, 1.5707963267948966

; energies grossieres : prediction (intra ou d'une trame a l'autre), residus
; de Laplace (bornes comme libopus pres de la fin du budget)
ope_coarse:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	stp	d12,d13,[sp,-16]!
	stp	d14,d15,[sp,-16]!
	ldr	x22,[x19,OE_END]
	ldr	x23,[x19,OE_C]
	ldr	x24,[x19,OE_INTRA]
	adr	x25,ce_eprob			; modele : [LM][intra][42]
	add	x0,x24,OE_LM*2
	mov	x1,42
	madd	x25,x0,x1,x25
	adr	x0,ce_pred
	cbz	x24,.inter
	movi	d8,0				; coef
	ldr	s9,[x0,32]			; beta intra
	b	.cb
.inter:	ldr	s8,[x0,OE_LM*4]
	ldr	s9,[x0,16+OE_LM*4]
.cb:	movi	d10,0				; prev[0], prev[1]
	movi	d11,0
	ldr	x26,[x19,OE_NBYTES]
	ucvtf	s12,x26				; max_decay = min(16, octets / 8)
	fmov	s0,0.125
	fmul	s12,s12,s0
	fmov	s0,16.0
	fmin	s12,s12,s0
	lsl	x26,x26,3			; budget
	mov	x27,0				; i
.i:	cmp	x27,x22
	b.hs	.done
	mov	x28,0				; c
.c:	cmp	x28,x23
	b.hs	.ni
	mov	x0,CE_NBE
	madd	x9,x28,x0,x27
	add	x0,x19,OE_LOGE
	ldr	s0,[x0,x9,lsl 2]		; x
	add	x0,x19,OE_OLDE
	ldr	s1,[x0,x9,lsl 2]		; old
	movl	w1,0xC1100000			; -9
	fmov	s2,w1
	fmax	s2,s1,s2
	fmul	s3,s2,s8			; coef max(old, -9)
	fmov	s4,s10
	cbz	x28,.p0
	fmov	s4,s11
.p0:	fsub	s5,s0,s3
	fsub	s5,s5,s4			; f
	fmov	s13,s5
	fadd	s14,s3,s4
	fmov	s6,0.5
	fadd	s6,s5,s6
	frintm	s6,s6
	fcvtzs	w10,s6				; qi
	movl	w1,0xC1E00000			; decay_bound = max(-28, old) - max_decay
	fmov	s7,w1
	fmax	s7,s1,s7
	fsub	s7,s7,s12
	cmp	w10,0
	b.ge	.nd
	fcmp	s0,s7
	b.ge	.nd
	fsub	s6,s7,s0
	fcvtzs	w1,s6
	add	w10,w10,w1
	cmp	w10,0
	csel	w10,wzr,w10,gt
.nd:	mov	x0,x20
	bl	ec_tell
	sub	x12,x26,x0			; budget - tell
	sub	x1,x22,x27			; bits_left = budget - tell - 3 C (end - i)
	mul	x1,x1,x23
	add	x1,x1,x1,lsl 1
	sub	x1,x12,x1
	cbz	x27,.nc
	cmp	x1,30
	b.ge	.nc
	cmp	x1,24
	b.ge	.c16
	mov	w2,1
	cmp	w10,w2
	csel	w10,w10,w2,lt
.c16:	cmp	x1,16
	b.ge	.nc
	mov	w2,-1
	cmp	w10,w2
	csel	w10,w10,w2,gt
.nc:	cmp	x12,15
	b.lt	.small
	mov	x1,20
	cmp	x27,x1
	csel	x1,x27,x1,lo
	lsl	x1,x1,1
	ldrb	w2,[x25,x1]
	add	x1,x1,1
	ldrb	w3,[x25,x1]
	lsl	w2,w2,7
	lsl	w3,w3,6
	mov	w1,w10
	mov	x0,x20
	bl	ope_laplace
	mov	w10,w0
	b	.q
.small:	cmp	x12,2
	b.lt	.one
	mov	w1,1
	cmp	w10,w1
	csel	w10,w10,w1,lt
	mov	w1,-1
	cmp	w10,w1
	csel	w10,w10,w1,gt
	lsl	w1,w10,1
	asr	w2,w10,31
	eor	w1,w1,w2			; 2 qi ^ -(qi < 0)
	adr	x2,ce_small_icdf
	mov	w3,2
	mov	x0,x20
	bl	ope_ec_icdf
	b	.q
.one:	cmp	x12,1
	b.lt	.none
	cmp	w10,0
	csel	w10,w10,wzr,lt
	neg	w1,w10
	mov	w2,1
	mov	x0,x20
	bl	ope_ec_bit_logp
	b	.q
.none:	mov	w10,-1
.q:	scvtf	s0,w10
	mov	x0,CE_NBE
	madd	x9,x28,x0,x27
	fsub	s1,s13,s0			; erreur
	add	x0,x19,OE_ERR
	str	s1,[x0,x9,lsl 2]
	fadd	s1,s14,s0			; energie du decodeur
	add	x0,x19,OE_OLDE
	str	s1,[x0,x9,lsl 2]
	cbnz	x28,.u1
	fadd	s10,s10,s0
	fmsub	s10,s9,s0,s10
	b	.nx
.u1:	fadd	s11,s11,s0
	fmsub	s11,s9,s0,s11
.nx:	add	x28,x28,1
	b	.c
.ni:	add	x27,x27,1
	b	.i
.done:	ldp	d14,d15,[sp],16
	ldp	d12,d13,[sp],16
	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = valeurs (flottants), x1 = n (3 ou 5) -> s0 = mediane. Ne touche que
; x0-x4, v0-v1.
ope_median:
	sub	sp,sp,32
	mov	x2,0
.cp:	ldr	s0,[x0,x2,lsl 2]
	str	s0,[sp,x2,lsl 2]
	add	x2,x2,1
	cmp	x2,x1
	b.lo	.cp
	mov	x2,1
.i:	cmp	x2,x1
	b.hs	.d
	ldr	s0,[sp,x2,lsl 2]
	mov	x3,x2
.j:	cbz	x3,.ins
	sub	x4,x3,1
	ldr	s1,[sp,x4,lsl 2]
	fcmp	s1,s0
	b.le	.ins
	str	s1,[sp,x3,lsl 2]
	mov	x3,x4
	b	.j
.ins:	str	s0,[sp,x3,lsl 2]
	add	x2,x2,1
	b	.i
.d:	lsr	x2,x1,1
	ldr	s0,[sp,x2,lsl 2]
	add	sp,sp,32
	ret

; dynalloc (analyse simplifiee de libopus) : enveloppe suiveuse des
; energies, plancher de bruit ; ce qui la depasse -> quanta voulus (OE_WANT),
; au plus 2/3 de la trame
ope_dynalloc:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	add	x0,x19,OE_WANT
	mov	x1,96
	bl	ak_memzero
	ldr	x0,[x19,OE_NBYTES]
	cmp	x0,50
	b.ls	.out
	ldr	x22,[x19,OE_END]
	ldr	x23,[x19,OE_C]
	mov	x24,0				; c
.c:	cmp	x24,x23
	b.hs	.cd
	mov	x0,CE_NBE
	mul	x0,x24,x0
	add	x25,x19,OE_LOGE
	add	x25,x25,x0,lsl 2		; e
	add	x26,x19,OE_FOL
	add	x26,x26,x0,lsl 2		; f
	ldr	s0,[x25]
	str	s0,[x26]
	mov	x27,0				; last
	mov	x1,1
	fmov	s4,0.5
	fmov	s5,1.5
.f1:	cmp	x1,x22
	b.hs	.f1d
	ldr	s0,[x25,x1,lsl 2]
	sub	x2,x1,1
	ldr	s1,[x25,x2,lsl 2]
	fadd	s1,s1,s4
	fcmp	s0,s1
	csel	x27,x1,x27,gt
	ldr	s2,[x26,x2,lsl 2]
	fadd	s2,s2,s5
	fmin	s2,s2,s0
	str	s2,[x26,x1,lsl 2]
	add	x1,x1,1
	b	.f1
.f1d:	fmov	s5,2.0
	sub	x1,x27,1
.f2:	tbnz	x1,63,.f2d
	ldr	s0,[x26,x1,lsl 2]
	add	x2,x1,1
	ldr	s1,[x26,x2,lsl 2]
	fadd	s1,s1,s5
	ldr	s2,[x25,x1,lsl 2]
	fmin	s1,s1,s2
	fmin	s0,s0,s1
	str	s0,[x26,x1,lsl 2]
	sub	x1,x1,1
	b	.f2
.f2d:	mov	x27,2				; mediane de 5 - 1
.m5:	sub	x0,x22,2
	cmp	x27,x0
	b.hs	.m5d
	sub	x0,x27,2
	add	x0,x25,x0,lsl 2
	mov	x1,5
	bl	ope_median
	fmov	s1,1.0
	fsub	s0,s0,s1
	ldr	s1,[x26,x27,lsl 2]
	fmax	s1,s1,s0
	str	s1,[x26,x27,lsl 2]
	add	x27,x27,1
	b	.m5
.m5d:	mov	x0,x25				; bords : mediane de 3 - 1
	mov	x1,3
	bl	ope_median
	fmov	s1,1.0
	fsub	s0,s0,s1
	ldp	s1,s2,[x26]
	fmax	s1,s1,s0
	fmax	s2,s2,s0
	stp	s1,s2,[x26]
	sub	x0,x22,3
	add	x0,x25,x0,lsl 2
	mov	x1,3
	bl	ope_median
	fmov	s1,1.0
	fsub	s0,s0,s1
	sub	x0,x22,2
	add	x0,x26,x0,lsl 2
	ldp	s1,s2,[x0]
	fmax	s1,s1,s0
	fmax	s2,s2,s0
	stp	s1,s2,[x0]
	; plancher : logN / 16 + 0,5 - 7 - eMeans + 0,0062 (i + 5)^2
	mov	x1,0
	adr	x2,ce_logn
	adr	x3,ce_emeans
.nf:	cmp	x1,x22
	b.hs	.nfd
	ldrh	w4,[x2,x1,lsl 1]
	ucvtf	s0,w4
	movl	w4,0x3D800000			; 1/16
	fmov	s1,w4
	fmul	s0,s0,s1
	fmov	s1,-6.5
	fadd	s0,s0,s1
	ldr	s1,[x3,x1,lsl 2]
	fsub	s0,s0,s1
	add	x4,x1,5
	mul	x4,x4,x4
	ucvtf	s1,x4
	movl	w4,0x3BCB295F			; 0,0062
	fmov	s2,w4
	fmadd	s0,s1,s2,s0
	ldr	s1,[x26,x1,lsl 2]
	fmax	s1,s1,s0
	str	s1,[x26,x1,lsl 2]
	add	x1,x1,1
	b	.nf
.nfd:	add	x24,x24,1
	b	.c
.cd:	; depassement (stereo : diaphonie de 24 dB, moyenne des voies)
	add	x25,x19,OE_FOL
	add	x26,x25,CE_NBE*4
	add	x27,x19,OE_LOGE
	fmov	s6,4.0
	movi	d7,0
	fmov	s16,0.5
	mov	x1,0
.g:	cmp	x1,x22
	b.hs	.gd
	ldr	s0,[x27,x1,lsl 2]
	ldr	s2,[x25,x1,lsl 2]
	cmp	x23,2
	b.ne	.gm
	add	x2,x1,CE_NBE
	ldr	s1,[x27,x2,lsl 2]
	ldr	s3,[x26,x1,lsl 2]
	fsub	s4,s2,s6
	fmax	s3,s3,s4
	fsub	s4,s3,s6
	fmax	s2,s2,s4
	fsub	s0,s0,s2
	fmax	s0,s0,s7
	fsub	s1,s1,s3
	fmax	s1,s1,s7
	fadd	s0,s0,s1
	fmul	s0,s0,s16
	b	.gs
.gm:	fsub	s0,s0,s2
	fmax	s0,s0,s7
.gs:	fmul	s0,s0,s16			; debit constant : moitie
	cmp	x1,8
	b.hs	.g8
	fadd	s0,s0,s0
.g8:	cmp	x1,12
	b.lo	.g12
	fmul	s0,s0,s16
.g12:	fmin	s0,s0,s6
	str	s0,[x25,x1,lsl 2]
	add	x1,x1,1
	b	.g
.gd:	; quanta voulus, au plus 2/3 de la trame
	mov	x1,0
	mov	x2,0				; total (1/8 bits)
	ldr	x3,[x19,OE_NBYTES]
	lsl	x3,x3,1
	mov	x4,3
	udiv	x3,x3,x4
	lsl	x3,x3,6
	adr	x9,ce_eband
.b:	cmp	x1,x22
	b.hs	.out
	ldr	s0,[x25,x1,lsl 2]
	ldrh	w5,[x9,x1,lsl 1]
	add	x6,x1,1
	ldrh	w6,[x9,x6,lsl 1]
	sub	w5,w6,w5
	mul	w5,w5,w23
	lsl	w5,w5,OE_LM			; width
	cmp	w5,6
	b.hs	.b1
	fcvtzs	w6,s0
	mul	w7,w6,w5
	lsl	w7,w7,3
	b	.b3
.b1:	cmp	w5,48
	b.ls	.b2
	fmov	s1,8.0
	fmul	s1,s0,s1
	fcvtzs	w6,s1
	mul	w7,w6,w5
	b	.b3
.b2:	ucvtf	s1,w5
	fmul	s1,s0,s1
	fmov	s2,6.0
	fdiv	s1,s1,s2
	fcvtzs	w6,s1
	mov	w7,48
	mul	w7,w6,w7
.b3:	add	x8,x2,w7,uxtw
	cmp	x8,x3
	b.le	.b4
	lsl	w8,w5,3				; trop : les quanta qui tiennent
	mov	w10,48
	cmp	w5,w10
	csel	w10,w5,w10,hi
	cmp	w8,w10
	csel	w8,w8,w10,lo
	sub	x10,x3,x2
	udiv	x10,x10,x8
	add	x0,x19,OE_WANT
	str	w10,[x0,x1,lsl 2]
	b	.out
.b4:	add	x0,x19,OE_WANT
	str	w6,[x0,x1,lsl 2]
	mov	x2,x8
	add	x1,x1,1
	b	.b
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; -> x0 = trim (0..10) : d'apres le debit, la correlation des voies et la
; pente du spectre (alloc_trim_analysis de libopus, simplifie)
ope_trim:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	ldr	x0,[x19,OE_NBYTES]
	mov	x1,400
	mul	x0,x0,x1
	fmov	s8,5.0
	movl	x1,80000
	cmp	x0,x1
	b.hs	.r
	fmov	s8,4.0
	movl	x1,64000
	cmp	x0,x1
	b.lo	.r
	sub	x0,x0,x1
	lsr	x0,x0,10
	ucvtf	s0,x0
	fmov	s1,16.0
	fdiv	s0,s0,s1
	fadd	s8,s8,s0
.r:	ldr	x0,[x19,OE_C]
	cmp	x0,2
	b.ne	.tilt
	oeadr	x1,OE_A_X			; correlation des 8 premieres bandes
	add	x2,x1,960*4
	movi	d9,0
	mov	x3,0
.d:	ldr	s0,[x1,x3,lsl 2]
	ldr	s1,[x2,x3,lsl 2]
	fmadd	s9,s0,s1,s9
	add	x3,x3,1
	cmp	x3,64
	b.lo	.d
	fmov	s0,0.125
	fmul	s9,s9,s0
	fabs	s9,s9
	fmov	s0,1.0
	fmin	s9,s9,s0
	movl	w0,0x3F8020C5			; log2(1,001 - s^2)
	fmov	s0,w0
	fmsub	s0,s9,s9,s0
	bl	ope_log2
	fmov	s1,0.75
	fmul	s0,s0,s1
	fmov	s1,-4.0
	fmax	s0,s0,s1
	fadd	s8,s8,s0
.tilt:	ldr	x21,[x19,OE_END]
	ldr	x22,[x19,OE_C]
	movi	d9,0
	mov	x0,0
.tc:	cmp	x0,x22
	b.hs	.td
	mov	x1,0
	sub	x2,x21,1
.ti:	cmp	x1,x2
	b.hs	.tn
	mov	x3,CE_NBE
	madd	x3,x0,x3,x1
	add	x4,x19,OE_LOGE
	ldr	s0,[x4,x3,lsl 2]
	lsl	x5,x1,1
	add	x5,x5,2
	sub	x5,x5,x21
	scvtf	s1,x5
	fmadd	s9,s0,s1,s9
	add	x1,x1,1
	b	.ti
.tn:	add	x0,x0,1
	b	.tc
.td:	sub	x0,x21,1
	mul	x0,x0,x22
	scvtf	s0,x0
	fdiv	s9,s9,s0
	fmov	s0,1.0
	fadd	s9,s9,s0
	fmov	s0,6.0
	fdiv	s9,s9,s0
	fmov	s0,2.0
	fmin	s9,s9,s0
	fmov	s0,-2.0
	fmax	s9,s9,s0
	fsub	s8,s8,s9
	fmov	s0,0.5
	fadd	s8,s8,s0
	frintm	s8,s8
	fcvtzs	w0,s8
	cmp	w0,0
	csel	w0,w0,wzr,gt
	mov	w1,10
	cmp	w0,w1
	csel	w0,w0,w1,lt
	ldp	d8,d9,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Allocation des bits : le calcul du decodeur (ce_alloc, RFC 6716 4.3.3),
;  les choix codes (sauts de bandes, intensite, double stereo a 0)
; ---------------------------------------------------------------------------

; w1 = bits disponibles (1/8) -> OE_PULSES, OE_FINEQ, OE_PRIO, OE_CODED,
; OE_INTENS, OE_DUAL, OE_BALANCE
ope_alloc:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x21,0				; start
	ldr	x22,[x19,OE_END]
	ldr	x23,[x19,OE_C]
	mov	x24,OE_LM
	sxtw	x25,w1
	cmp	x25,0
	csel	x25,x25,xzr,gt			; total
	mov	x26,x21				; skip_start
	adr	x27,ce_eband
	mov	x9,0
	cmp	x25,8
	b.lt	.s0
	mov	x9,8
.s0:	sub	x25,x25,x9			; x9 = skip_rsv
	mov	x10,0				; intensity_rsv
	mov	x11,0				; dual_stereo_rsv
	cmp	x23,2
	b.ne	.r0
	adr	x0,ce_log2frac
	sub	x1,x22,x21
	ldrb	w10,[x0,x1]
	cmp	x10,x25
	b.le	.r1
	mov	x10,0
	b	.r0
.r1:	sub	x25,x25,x10
	cmp	x25,8
	b.lt	.r0
	mov	x11,8
	sub	x25,x25,x11
.r0:	ldr	x12,[x19,OE_TRIM]
	mov	x13,x21
.t:	cmp	x13,x22
	b.hs	.td
	ldrh	w0,[x27,x13,lsl 1]
	add	x1,x13,1
	ldrh	w1,[x27,x1,lsl 1]
	sub	w0,w1,w0			; N
	add	w1,w0,w0,lsl 1
	lsl	w1,w1,w24
	lsl	w1,w1,3
	lsr	w1,w1,4
	lsl	w2,w23,3
	cmp	w1,w2
	csel	w1,w1,w2,gt
	add	x3,x19,OE_THRESH
	str	w1,[x3,x13,lsl 2]
	mul	w1,w23,w0
	sub	w2,w12,5
	sub	w2,w2,w24
	mul	w1,w1,w2
	sub	w2,w22,w13
	sub	w2,w2,1
	mul	w1,w1,w2
	add	w2,w24,3
	lsl	w1,w1,w2
	asr	w1,w1,6
	lsl	w2,w0,w24
	cmp	w2,1
	b.ne	.t1
	sub	w1,w1,w23,lsl 3
.t1:	add	x3,x19,OE_TRIMOFF
	str	w1,[x3,x13,lsl 2]
	add	x13,x13,1
	b	.t
.td:	mov	x14,1				; lo
	mov	x15,10				; hi
.bi:	cmp	x14,x15
	b.gt	.bid
	add	x16,x14,x15
	lsr	x16,x16,1
	mov	x0,x16
	bl	.vsum
	cmp	x0,x25
	b.le	.bl
	sub	x15,x16,1
	b	.bi
.bl:	add	x14,x16,1
	b	.bi
.bid:	mov	x15,x14				; hi
	sub	x14,x14,1			; lo
	adr	x8,ce_balloc
	mov	x13,x21
.b:	cmp	x13,x22
	b.hs	.bd
	ldrh	w0,[x27,x13,lsl 1]
	add	x1,x13,1
	ldrh	w1,[x27,x1,lsl 1]
	sub	w0,w1,w0
	mov	x5,CE_NBE
	madd	x5,x14,x5,x8
	ldrb	w1,[x5,x13]
	mul	w1,w1,w0
	mul	w1,w1,w23
	lsl	w1,w1,w24
	asr	w1,w1,2				; bits1
	cmp	x15,11
	b.lo	.b2v
	add	x5,x19,OE_CAP
	ldr	w2,[x5,x13,lsl 2]
	b	.b2d
.b2v:	mov	x5,CE_NBE
	madd	x5,x15,x5,x8
	ldrb	w2,[x5,x13]
	mul	w2,w2,w0
	mul	w2,w2,w23
	lsl	w2,w2,w24
	asr	w2,w2,2				; bits2
.b2d:	add	x5,x19,OE_TRIMOFF
	ldr	w5,[x5,x13,lsl 2]
	cmp	w1,0
	b.le	.n1
	add	w1,w1,w5
	cmp	w1,0
	csel	w1,w1,wzr,gt
.n1:	cmp	w2,0
	b.le	.n2
	add	w2,w2,w5
	cmp	w2,0
	csel	w2,w2,wzr,gt
.n2:	add	x5,x19,OE_OFFS
	ldr	w5,[x5,x13,lsl 2]
	cbz	x14,.n3
	add	w1,w1,w5
.n3:	add	w2,w2,w5
	cmp	w5,0
	b.le	.n4
	mov	x26,x13				; skip_start
.n4:	sub	w2,w2,w1
	cmp	w2,0
	csel	w2,w2,wzr,gt
	add	x5,x19,OE_BITS1
	str	w1,[x5,x13,lsl 2]
	add	x5,x19,OE_BITS2
	str	w2,[x5,x13,lsl 2]
	add	x13,x13,1
	b	.b
.bd:	lsl	x28,x23,3			; alloc_floor
	mov	x14,0				; lo
	mov	x15,64				; hi
	mov	x17,6
.ib:	add	x16,x14,x15
	lsr	x16,x16,1			; mid
	mov	x1,0				; psum
	mov	x2,0				; done
	mov	x3,x22
.ibj:	cmp	x3,x21
	b.ls	.ibd
	sub	x3,x3,1
	add	x4,x19,OE_BITS1
	ldr	w4,[x4,x3,lsl 2]
	add	x5,x19,OE_BITS2
	ldr	w5,[x5,x3,lsl 2]
	mul	w5,w5,w16
	asr	w5,w5,6
	add	w4,w4,w5			; tmp
	add	x5,x19,OE_THRESH
	ldr	w5,[x5,x3,lsl 2]
	cmp	w4,w5
	b.ge	.ibu
	cbnz	x2,.ibu
	cmp	w4,w28
	b.lt	.ibj
	add	x1,x1,x28
	b	.ibj
.ibu:	mov	x2,1
	add	x5,x19,OE_CAP
	ldr	w5,[x5,x3,lsl 2]
	cmp	w4,w5
	csel	w4,w4,w5,lt
	add	x1,x1,w4,sxtw
	b	.ibj
.ibd:	cmp	x1,x25
	b.le	.ibl
	mov	x15,x16
	b	.ibn
.ibl:	mov	x14,x16
.ibn:	subs	x17,x17,1
	b.ne	.ib
	mov	x12,0				; psum
	mov	x2,0				; done
	mov	x3,x22
.fb:	cmp	x3,x21
	b.ls	.fbd
	sub	x3,x3,1
	add	x4,x19,OE_BITS1
	ldr	w4,[x4,x3,lsl 2]
	add	x5,x19,OE_BITS2
	ldr	w5,[x5,x3,lsl 2]
	mul	w5,w5,w14
	asr	w5,w5,6
	add	w4,w4,w5
	add	x5,x19,OE_THRESH
	ldr	w5,[x5,x3,lsl 2]
	cmp	w4,w5
	b.ge	.fbu
	cbnz	x2,.fbu
	cmp	w4,w28
	csel	w4,w28,wzr,ge
	b	.fbc
.fbu:	mov	x2,1
.fbc:	add	x5,x19,OE_CAP
	ldr	w5,[x5,x3,lsl 2]
	cmp	w4,w5
	csel	w4,w4,w5,lt
	add	x5,x19,OE_PULSES
	str	w4,[x5,x3,lsl 2]
	add	x12,x12,w4,sxtw
	b	.fb
.fbd:	; bandes sautees, depuis la fin (choix : profondeur de libopus)
	mov	x13,x22				; codedBands
.sk:	sub	x14,x13,1			; j
	cmp	x14,x26
	b.gt	.sk1
	add	x25,x25,x9			; (rend le bit reserve)
	b	.skd
.sk1:	sub	x1,x25,x12			; left
	ldrh	w2,[x27,x13,lsl 1]
	ldrh	w3,[x27,x21,lsl 1]
	sub	w2,w2,w3
	sdiv	w4,w1,w2			; percoeff
	msub	w1,w4,w2,w1			; left
	ldrh	w5,[x27,x14,lsl 1]
	sub	w6,w5,w3
	subs	w6,w1,w6
	csel	w6,w6,wzr,gt			; rem
	ldrh	w7,[x27,x13,lsl 1]
	sub	w7,w7,w5			; largeur
	add	x5,x19,OE_PULSES
	ldr	w15,[x5,x14,lsl 2]
	madd	w15,w4,w7,w15
	add	w15,w15,w6			; band_bits
	add	x5,x19,OE_THRESH
	ldr	w5,[x5,x14,lsl 2]
	add	w6,w28,8
	cmp	w5,w6
	csel	w5,w5,w6,gt
	cmp	w15,w5
	b.lt	.sk2
	mov	w0,1				; garder : codedBands <= start + 2, ou
	cmp	x13,17				; band_bits > profondeur x largeur, j <= sigbw
	b.le	.dp
	ldr	x1,[x19,OE_LASTCODED]
	mov	w0,9
	mov	w2,7
	cmp	x14,x1
	csel	w0,w2,w0,lt
.dp:	mul	w0,w0,w7
	lsl	w0,w0,OE_LM+3
	asr	w0,w0,4
	mov	w8,1
	add	x2,x21,2
	cmp	x13,x2
	b.le	.kp
	cmp	w15,w0
	cset	w8,gt
	ldr	x1,[x19,OE_SIGBW]		; et j dans la bande du signal
	cmp	x14,x1
	csel	w8,w8,wzr,le
.kp:	mov	x0,x20
	mov	w1,w8
	mov	w2,1
	bl	ope_ec_bit_logp
	cbnz	w8,.skd
	add	x12,x12,8
	sub	w15,w15,8
.sk2:	add	x5,x19,OE_PULSES
	ldr	w0,[x5,x14,lsl 2]
	add	x0,x0,x10
	sub	x12,x12,x0
	cbz	x10,.sk3
	adr	x0,ce_log2frac
	sub	x1,x14,x21
	ldrb	w10,[x0,x1]
.sk3:	add	x12,x12,x10
	mov	w0,0
	cmp	w15,w28
	b.lt	.sk4
	add	x12,x12,x28
	mov	w0,w28
.sk4:	str	w0,[x5,x14,lsl 2]
	sub	x13,x13,1
	b	.sk
.skd:	str	x13,[x19,OE_CODED]
	str	x13,[x19,OE_LASTCODED]
	; intensite (d'apres le debit, au plus codedBands), double stereo : 0
	mov	x0,0
	cbz	x10,.ni
	ldr	x1,[x19,OE_ITGT]
	cmp	x1,x13
	csel	x1,x1,x13,lt
	cmp	x1,x21
	csel	x1,x1,x21,gt
	str	x1,[sp,-16]!
	sub	w1,w1,w21
	add	x2,x13,1
	sub	x2,x2,x21
	mov	x0,x20
	bl	ope_ec_uint
	ldr	x0,[sp],16
.ni:	str	x0,[x19,OE_INTENS]
	cmp	x0,x21
	b.gt	.nd
	add	x25,x25,x11
	mov	x11,0
.nd:	cbz	x11,.nd2
	mov	x0,x20
	mov	w1,0
	mov	w2,1
	bl	ope_ec_bit_logp
.nd2:	str	xzr,[x19,OE_DUAL]
	; le reste, reparti
	sub	x1,x25,x12			; left
	ldrh	w2,[x27,x13,lsl 1]
	ldrh	w3,[x27,x21,lsl 1]
	sub	w2,w2,w3
	sdiv	w4,w1,w2			; percoeff
	msub	w1,w4,w2,w1
	add	x5,x19,OE_PULSES
	mov	x14,x21
.rp:	cmp	x14,x13
	b.hs	.rpd
	ldrh	w2,[x27,x14,lsl 1]
	add	x3,x14,1
	ldrh	w3,[x27,x3,lsl 1]
	sub	w3,w3,w2
	ldr	w2,[x5,x14,lsl 2]
	madd	w2,w4,w3,w2
	str	w2,[x5,x14,lsl 2]
	add	x14,x14,1
	b	.rp
.rpd:	mov	x14,x21
.rl:	cmp	x14,x13
	b.hs	.rld
	ldrh	w2,[x27,x14,lsl 1]
	add	x3,x14,1
	ldrh	w3,[x27,x3,lsl 1]
	sub	w3,w3,w2
	cmp	w1,w3
	csel	w3,w1,w3,lt
	ldr	w2,[x5,x14,lsl 2]
	add	w2,w2,w3
	str	w2,[x5,x14,lsl 2]
	sub	w1,w1,w3
	add	x14,x14,1
	b	.rl
.rld:	; energie fine et impulsions de chaque bande
	mov	x16,0				; balance
	sub	x17,x23,1			; stereo
	ldr	x26,[x19,OE_INTENS]
	ldr	x9,[x19,OE_DUAL]
	mov	x14,x21
.fe:	cmp	x14,x13
	b.hs	.fed
	ldrh	w0,[x27,x14,lsl 1]
	add	x1,x14,1
	ldrh	w1,[x27,x1,lsl 1]
	sub	w0,w1,w0
	lsl	w0,w0,w24			; N
	add	x5,x19,OE_PULSES
	ldr	w15,[x5,x14,lsl 2]
	add	w15,w15,w16			; bit
	add	x6,x19,OE_CAP
	ldr	w6,[x6,x14,lsl 2]
	cmp	w0,1
	b.le	.fe1
	subs	w10,w15,w6			; excess
	csel	w10,w10,wzr,gt
	sub	w15,w15,w10			; bits[j]
	mul	w11,w23,w0			; den
	cmp	x23,2
	b.ne	.fd
	cmp	w0,2
	b.le	.fd
	cbnz	x9,.fd
	cmp	x14,x26
	b.ge	.fd
	add	w11,w11,1
.fd:	adr	x1,ce_logn
	ldrh	w1,[x1,x14,lsl 1]
	add	w1,w1,w24,lsl 3
	mul	w1,w1,w11			; NClogN
	mov	w2,21
	mul	w2,w2,w11
	neg	w2,w2
	add	w2,w2,w1,asr 1			; offset
	cmp	w0,2
	b.ne	.fo
	add	w2,w2,w11,lsl 1
.fo:	add	w3,w15,w2
	cmp	w3,w11,lsl 4
	b.ge	.fo2
	add	w2,w2,w1,asr 2
	b	.fo3
.fo2:	add	w4,w11,w11,lsl 1
	cmp	w3,w4,lsl 3
	b.ge	.fo3
	add	w2,w2,w1,asr 3
.fo3:	add	w3,w15,w2
	add	w3,w3,w11,lsl 2
	cmp	w3,0
	csel	w3,w3,wzr,gt
	udiv	w3,w3,w11
	lsr	w3,w3,3				; ebits
	mul	w4,w23,w3
	cmp	w4,w15,asr 3
	b.le	.fo4
	asr	w3,w15,w17
	asr	w3,w3,3
.fo4:	mov	w4,8
	cmp	w3,w4
	csel	w3,w3,w4,lt
	mul	w4,w3,w11
	lsl	w4,w4,3
	add	w7,w15,w2
	cmp	w4,w7
	cset	w8,ge				; priorite
	mul	w4,w23,w3
	sub	w15,w15,w4,lsl 3
	b	.fe2
.fe1:	lsl	w1,w23,3			; N = 1 : un bit de signe
	subs	w10,w15,w1
	csel	w10,w10,wzr,gt
	sub	w15,w15,w10
	mov	w3,0
	mov	w8,1
.fe2:	cmp	w10,0
	b.le	.fe3
	add	w1,w17,3			; bits fins en plus
	asr	w1,w10,w1
	mov	w4,8
	sub	w4,w4,w3
	cmp	w1,w4
	csel	w1,w1,w4,lt
	add	w3,w3,w1
	mul	w1,w1,w23
	lsl	w1,w1,3				; extra_bits
	sub	w4,w10,w16
	cmp	w1,w4
	cset	w8,ge
	sub	w10,w10,w1
.fe3:	mov	w16,w10				; balance
	str	w15,[x5,x14,lsl 2]
	add	x5,x19,OE_FINEQ
	str	w3,[x5,x14,lsl 2]
	add	x5,x19,OE_PRIO
	str	w8,[x5,x14,lsl 2]
	add	x14,x14,1
	b	.fe
.fed:	sxtw	x16,w16
	str	x16,[x19,OE_BALANCE]
.sf:	cmp	x14,x22				; bandes sautees : tout en energie fine
	b.hs	.out
	add	x5,x19,OE_PULSES
	ldr	w0,[x5,x14,lsl 2]
	str	wzr,[x5,x14,lsl 2]
	asr	w0,w0,w17
	asr	w0,w0,3
	add	x5,x19,OE_FINEQ
	str	w0,[x5,x14,lsl 2]
	cmp	w0,1
	cset	w0,lt
	add	x5,x19,OE_PRIO
	str	w0,[x5,x14,lsl 2]
	add	x14,x14,1
	b	.sf
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue
; x0 = vecteur -> x0 = bits demandes (bissection) ; ne touche que x0-x7
.vsum:	mov	x1,0				; psum
	mov	x2,0				; done
	mov	x3,x22
	adr	x4,ce_balloc
	mov	x5,CE_NBE
	madd	x4,x0,x5,x4
.v:	cmp	x3,x21
	b.ls	.vd
	sub	x3,x3,1
	ldrh	w5,[x27,x3,lsl 1]
	add	x6,x3,1
	ldrh	w6,[x27,x6,lsl 1]
	sub	w5,w6,w5
	ldrb	w6,[x4,x3]
	mul	w5,w5,w23
	mul	w5,w5,w6
	lsl	w5,w5,w24
	asr	w5,w5,2
	cmp	w5,0
	b.le	.v1
	add	x7,x19,OE_TRIMOFF
	ldr	w7,[x7,x3,lsl 2]
	add	w5,w5,w7
	cmp	w5,0
	csel	w5,w5,wzr,gt
.v1:	add	x7,x19,OE_OFFS
	ldr	w7,[x7,x3,lsl 2]
	add	w5,w5,w7
	add	x7,x19,OE_THRESH
	ldr	w7,[x7,x3,lsl 2]
	cmp	w5,w7
	b.ge	.v2
	cbnz	x2,.v2
	lsl	w7,w23,3
	cmp	w5,w7
	b.lt	.v
	add	x1,x1,x7
	b	.v
.v2:	mov	x2,1
	add	x7,x19,OE_CAP
	ldr	w7,[x7,x3,lsl 2]
	cmp	w5,w7
	csel	w5,w5,w7,lt
	add	x1,x1,w5,sxtw
	b	.v
.vd:	mov	x0,x1
	ret

; ---------------------------------------------------------------------------
;  Energies fines et finales (RFC 6716 4.3.2.2)
; ---------------------------------------------------------------------------

ope_fine:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	ldr	x22,[x19,OE_END]
	ldr	x23,[x19,OE_C]
	mov	x21,0
.i:	cmp	x21,x22
	b.hs	.out
	add	x0,x19,OE_FINEQ
	ldr	w24,[x0,x21,lsl 2]
	cmp	w24,0
	b.le	.ni
	mov	x25,0
.c:	cmp	x25,x23
	b.hs	.ni
	mov	x0,CE_NBE
	madd	x26,x25,x0,x21
	add	x0,x19,OE_ERR			; q2 = floor((erreur + 1/2) 2^fq)
	ldr	s0,[x0,x26,lsl 2]
	fmov	s1,0.5
	fadd	s0,s0,s1
	add	w1,w24,127
	lsl	w1,w1,23
	fmov	s2,w1
	fmul	s0,s0,s2
	frintm	s0,s0
	fcvtzs	w9,s0
	mov	w2,1
	lsl	w2,w2,w24
	sub	w2,w2,1
	cmp	w9,w2
	csel	w9,w9,w2,lt
	cmp	w9,0
	csel	w9,w9,wzr,gt
	mov	x0,x20
	mov	w1,w9
	mov	w2,w24
	bl	ope_ec_bits
	ucvtf	s0,w9				; (q2 + 1/2) 2^-fq - 1/2
	fmov	s1,0.5
	fadd	s0,s0,s1
	mov	w0,127
	sub	w0,w0,w24
	lsl	w0,w0,23
	fmov	s2,w0
	fmul	s0,s0,s2
	fsub	s0,s0,s1
	add	x0,x19,OE_OLDE
	ldr	s1,[x0,x26,lsl 2]
	fadd	s1,s1,s0
	str	s1,[x0,x26,lsl 2]
	add	x0,x19,OE_ERR
	ldr	s1,[x0,x26,lsl 2]
	fsub	s1,s1,s0
	str	s1,[x0,x26,lsl 2]
	add	x25,x25,1
	b	.c
.ni:	add	x21,x21,1
	b	.i
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; energies finales : les bits restants, deux passes de priorite
ope_final:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	ldr	x22,[x19,OE_END]
	ldr	x23,[x19,OE_C]
	mov	x0,x20
	bl	ec_tell
	ldr	x1,[x19,OE_NBYTES]
	lsl	x1,x1,3
	sub	x24,x1,x0			; bits_left
	mov	x25,0				; priorite
.p:	cmp	x25,2
	b.hs	.out
	mov	x26,0
.i:	cmp	x26,x22
	b.hs	.np
	cmp	x24,x23
	b.lt	.np
	add	x0,x19,OE_FINEQ
	ldr	w9,[x0,x26,lsl 2]
	cmp	w9,8
	b.ge	.ni
	add	x0,x19,OE_PRIO
	ldr	w0,[x0,x26,lsl 2]
	cmp	x0,x25
	b.ne	.ni
	mov	x10,0
.c:	cmp	x10,x23
	b.hs	.ni
	mov	x0,CE_NBE
	madd	x12,x10,x0,x26
	add	x0,x19,OE_ERR
	ldr	s0,[x0,x12,lsl 2]
	fcmp	s0,0.0
	cset	w11,ge
	mov	x0,x20
	mov	w1,w11
	mov	w2,1
	bl	ope_ec_bits
	ucvtf	s0,w11				; (q2 - 1/2) 2^-(fq + 1)
	fmov	s1,0.5
	fsub	s0,s0,s1
	mov	w0,126
	sub	w0,w0,w9
	lsl	w0,w0,23
	fmov	s2,w0
	fmul	s0,s0,s2
	add	x0,x19,OE_OLDE
	ldr	s1,[x0,x12,lsl 2]
	fadd	s1,s1,s0
	str	s1,[x0,x12,lsl 2]
	add	x0,x19,OE_ERR
	ldr	s1,[x0,x12,lsl 2]
	fsub	s1,s1,s0
	str	s1,[x0,x12,lsl 2]
	sub	x24,x24,1
	add	x10,x10,1
	b	.c
.ni:	add	x26,x26,1
	b	.i
.np:	add	x25,x25,1
	b	.p
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Bandes (quant_all_bands, cote encodeur ; blocs longs, tf_change = 0)
; ---------------------------------------------------------------------------

ope_bands:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	ldr	x22,[x19,OE_END]
	ldr	x24,[x19,OE_CODED]
	ldr	x28,[x19,OE_BALANCE]
	ldr	x25,[x19,OE_NBYTES]
	lsl	x25,x25,6			; total (1/8 bits)
	adr	x26,ce_eband
	mov	x23,0
.b:	cmp	x23,x22
	b.hs	.out
	str	x23,[x19,OE_I]
	mov	x0,x20
	bl	ec_tell_frac
	mov	x27,x0				; tell
	cbz	x23,.nb
	sub	x28,x28,x27			; balance -= tell
.nb:	sub	x1,x25,x27			; remaining = total - tell - 1
	sub	x1,x1,1
	str	x1,[x19,OE_REM]
	mov	x2,0				; b
	cmp	x23,x24
	b.hs	.bs
	sub	x3,x24,x23
	mov	x4,3
	cmp	x3,x4
	csel	x3,x3,x4,lt
	sdiv	x3,x28,x3			; curr_balance
	add	x4,x19,OE_PULSES
	ldr	w4,[x4,x23,lsl 2]
	add	x3,x3,w4,sxtw
	add	x4,x1,1
	cmp	x3,x4
	csel	x3,x3,x4,lt
	mov	x4,16383
	cmp	x3,x4
	csel	x3,x3,x4,lt
	cmp	x3,0
	csel	x2,x3,xzr,gt
.bs:	ldrh	w0,[x26,x23,lsl 1]
	add	x1,x23,1
	ldrh	w1,[x26,x1,lsl 1]
	sub	w1,w1,w0
	lsl	w1,w1,OE_LM			; N
	lsl	x0,x0,OE_LM
	oeadr	x3,OE_A_X
	add	x0,x3,x0,lsl 2			; X
	ldr	x4,[x19,OE_C]
	cmp	x4,2
	b.ne	.mono
	add	x3,x0,960*4			; Y
	mov	w4,w2
	mov	w2,w1
	mov	x1,x3
	mov	w3,w4
	bl	ope_qbs
	b	.nx
.mono:	mov	w3,OE_LM
	bl	ope_qb
.nx:	add	x0,x19,OE_PULSES		; balance += pulses[i] + tell
	ldr	w0,[x0,x23,lsl 2]
	add	x28,x28,w0,sxtw
	add	x28,x28,x27
	add	x23,x23,1
	b	.b
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, w1 = N, w2 = b, w3 = LM : une bande mono (quant_band)
ope_qb:
	cmp	w1,1
	b.ne	ope_qp
	mov	x1,0
	b	ope_qn1

; x0 = X, x1 = Y (0) : bande d'un coefficient, un bit de signe par voie
ope_qn1:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
.c:	ldr	x1,[x19,OE_REM]
	cmp	x1,8
	b.lt	.s
	sub	x1,x1,8
	str	x1,[x19,OE_REM]
	ldr	s0,[x21]
	fcmp	s0,0.0
	cset	w1,mi
	mov	x0,x20
	mov	w2,1
	bl	ope_ec_bits
.s:	cbz	x22,.d
	mov	x21,x22
	mov	x22,0
	b	.c
.d:	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, w1 = N, w2 = b, w3 = LM (-1..3) : division recursive en deux
; moities (theta), sinon PVQ (quant_partition)
ope_qp:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,48
	mov	x21,x0
	mov	w22,w1				; N
	mov	w23,w2				; b
	mov	w26,w3				; LM
	ldr	x0,[x19,OE_I]
	mov	w1,w26
	bl	ce_cache
	ldrb	w1,[x0]
	ldrb	w1,[x0,w1,uxtw]
	add	w1,w1,12
	cmn	w26,1
	b.eq	.leaf
	cmp	w23,w1
	b.le	.leaf
	cmp	w22,2
	b.le	.leaf
	lsr	w22,w22,1			; division
	sub	w26,w26,1
	mov	x0,x21
	add	x1,x21,x22,lsl 2
	mov	w2,w22
	mov	w3,w23
	mov	w4,w26
	mov	w5,0
	bl	ope_theta
	mov	w23,w5				; b
	str	x0,[sp]				; itheta
	sub	w2,w23,w3			; mbits = max(0, min(b, (b - delta) / 2))
	mov	w5,2
	sdiv	w2,w2,w5
	cmp	w2,w23
	csel	w2,w2,w23,lt
	cmp	w2,0
	csel	w2,w2,wzr,gt
	sub	w3,w23,w2			; sbits
	stp	w2,w3,[sp,16]
	ldr	x1,[x19,OE_REM]
	sub	x1,x1,w4,sxtw
	str	x1,[x19,OE_REM]
	str	x1,[sp,24]			; rebalance
	cmp	w2,w3
	b.lt	.sfirst
	mov	x0,x21
	mov	w1,w22
	mov	w3,w26
	bl	ope_qp
	ldr	x1,[sp,24]			; rebalance = mbits - (rebalance - remaining)
	ldr	x2,[x19,OE_REM]
	sub	x1,x1,x2
	ldrsw	x2,[sp,16]
	sub	x1,x2,x1
	ldr	w3,[sp,20]
	cmp	x1,24
	b.le	.s2
	ldr	x4,[sp]
	cbz	x4,.s2
	sub	x1,x1,24
	add	w3,w3,w1
.s2:	add	x0,x21,x22,lsl 2
	mov	w1,w22
	mov	w2,w3
	mov	w3,w26
	bl	ope_qp
	b	.out
.sfirst:
	add	x0,x21,x22,lsl 2
	mov	w1,w22
	mov	w2,w3
	mov	w3,w26
	bl	ope_qp
	ldr	x1,[sp,24]
	ldr	x2,[x19,OE_REM]
	sub	x1,x1,x2
	ldrsw	x3,[sp,20]
	sub	x1,x3,x1
	ldr	w2,[sp,16]
	cmp	x1,24
	b.le	.m2
	ldr	x4,[sp]
	cmp	x4,16384
	b.eq	.m2
	sub	x1,x1,24
	add	w2,w2,w1
.m2:	mov	x0,x21
	mov	w1,w22
	mov	w3,w26
	bl	ope_qp
	b	.out
.leaf:	; impulsions (bits2pulses, sans depasser le budget)
	ldr	x0,[x19,OE_I]
	mov	w1,w26
	mov	w2,w23
	bl	ce_b2p
	mov	w28,w0				; q
	ldr	x0,[x19,OE_I]
	mov	w1,w26
	mov	w2,w28
	bl	ce_p2b
	ldr	x1,[x19,OE_REM]
	sub	x1,x1,x0
	str	x1,[x19,OE_REM]
.bust:	ldr	x1,[x19,OE_REM]
	cmp	x1,0
	b.ge	.bok
	cbz	w28,.bok
	add	x1,x1,x0
	str	x1,[x19,OE_REM]
	sub	w28,w28,1
	ldr	x0,[x19,OE_I]
	mov	w1,w26
	mov	w2,w28
	bl	ce_p2b
	ldr	x1,[x19,OE_REM]
	sub	x1,x1,x0
	str	x1,[x19,OE_REM]
	b	.bust
.bok:	cbz	w28,.out
	mov	w0,w28
	bl	ce_getp
	mov	w2,w0
	mov	x0,x21
	mov	w1,w22
	bl	ope_algq
.out:	add	sp,sp,48
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, x1 = Y, w2 = N, w3 = b : bande stereo (quant_band_stereo : M/S
; ou intensite ; pas de double stereo)
ope_qbs:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,48
	mov	x21,x0
	mov	x22,x1
	mov	w23,w2
	cmp	w23,1
	b.ne	.n
	bl	ope_qn1
	b	.out
.n:	mov	w4,OE_LM
	mov	w5,1
	bl	ope_theta
	str	x0,[sp]				; itheta
	cmp	w23,2
	b.ne	.gen
	; N = 2 : un seul bit pour le cote
	mov	w24,0				; sbits
	cbz	w0,.s0
	cmp	w0,16384
	b.eq	.s0
	mov	w24,8
.s0:	ldr	x1,[x19,OE_REM]
	add	w2,w4,w24
	sub	x1,x1,w2,sxtw
	str	x1,[x19,OE_REM]
	sub	w25,w5,w24			; mbits
	mov	x1,x21
	mov	x2,x22
	cmp	w0,8192
	b.le	.sw
	mov	x1,x22
	mov	x2,x21
.sw:	str	x1,[sp,16]
	cbz	w24,.nsg
	ldp	s0,s1,[x1]			; signe de x2[0] y2[1] - x2[1] y2[0]
	ldp	s2,s3,[x2]
	fmul	s4,s0,s3
	fmsub	s4,s1,s2,s4
	fcmp	s4,0.0
	cset	w1,mi
	mov	x0,x20
	mov	w2,1
	bl	ope_ec_bits
.nsg:	ldr	x0,[sp,16]
	mov	w1,2
	mov	w2,w25
	mov	w3,OE_LM
	bl	ope_qb
	b	.out
.gen:	sub	w2,w5,w3			; mbits = max(0, min(b, (b - delta) / 2))
	mov	w6,2
	sdiv	w2,w2,w6
	cmp	w2,w5
	csel	w2,w2,w5,lt
	cmp	w2,0
	csel	w2,w2,wzr,gt
	sub	w3,w5,w2			; sbits
	stp	w2,w3,[sp,16]
	ldr	x1,[x19,OE_REM]
	sub	x1,x1,w4,sxtw
	str	x1,[x19,OE_REM]
	str	x1,[sp,24]			; rebalance
	cmp	w2,w3
	b.lt	.sfirst
	mov	x0,x21
	mov	w1,w23
	mov	w3,OE_LM
	bl	ope_qb
	ldr	x1,[sp,24]
	ldr	x2,[x19,OE_REM]
	sub	x1,x1,x2
	ldrsw	x2,[sp,16]
	sub	x1,x2,x1
	ldr	w3,[sp,20]
	cmp	x1,24
	b.le	.sb
	ldr	x4,[sp]
	cbz	x4,.sb
	sub	x1,x1,24
	add	w3,w3,w1
.sb:	mov	x0,x22
	mov	w1,w23
	mov	w2,w3
	mov	w3,OE_LM
	bl	ope_qb
	b	.out
.sfirst:
	mov	x0,x22
	mov	w1,w23
	mov	w2,w3
	mov	w3,OE_LM
	bl	ope_qb
	ldr	x1,[sp,24]
	ldr	x2,[x19,OE_REM]
	sub	x1,x1,x2
	ldrsw	x3,[sp,20]
	sub	x1,x3,x1
	ldr	w2,[sp,16]
	cmp	x1,24
	b.le	.mb
	ldr	x4,[sp]
	cmp	x4,16384
	b.eq	.mb
	sub	x1,x1,24
	add	w2,w2,w1
.mb:	mov	x0,x21
	mov	w1,w23
	mov	w3,OE_LM
	bl	ope_qb
.out:	add	sp,sp,48
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, x1 = Y, w2 = N, w3 = b, w4 = LM, w5 = stereo -> w0 = itheta,
; w3 = delta, w4 = qalloc, w5 = b (compute_theta, cote encodeur : l'angle
; mesure, quantifie et code ; en stereo, X et Y deviennent milieu et cote, ou
; X l'intensite)
ope_theta:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	sub	sp,sp,16
	mov	x21,x0
	mov	x22,x1
	mov	w23,w2				; N
	mov	w24,w3				; b
	mov	w25,w4				; LM
	mov	w26,w5				; stereo
	ldr	x9,[x19,OE_I]
	adr	x0,ce_logn
	ldrh	w10,[x0,x9,lsl 1]
	add	w10,w10,w25,lsl 3		; pulse_cap
	mov	w11,4				; offset
	cbz	w26,.o1
	cmp	w23,2
	b.ne	.o1
	mov	w11,16
.o1:	neg	w11,w11
	add	w11,w11,w10,asr 1
	lsl	w0,w23,1			; qn (compute_qn)
	sub	w0,w0,1
	cbz	w26,.q1
	cmp	w23,2
	b.ne	.q1
	sub	w0,w0,1
.q1:	madd	w1,w0,w11,w24
	sdiv	w1,w1,w0			; qb
	sub	w2,w24,w10
	sub	w2,w2,32
	cmp	w1,w2
	csel	w1,w1,w2,lt
	mov	w2,64
	cmp	w1,w2
	csel	w1,w1,w2,lt
	mov	w27,1				; qn
	cmp	w1,4
	b.lt	.qd
	adr	x2,ce_exp2t8
	and	w3,w1,7
	ldrh	w3,[x2,w3,uxtw 1]
	mov	w4,14
	sub	w4,w4,w1,asr 3
	lsr	w3,w3,w4
	add	w3,w3,1
	lsr	w3,w3,1
	lsl	w27,w3,1
.qd:	cbz	w26,.qi
	ldr	x0,[x19,OE_INTENS]
	cmp	x9,x0
	b.lo	.qi
	mov	w27,1
.qi:	; angle mesure : 16384 x 0,63662 atan2(|cote|, |milieu|)
	movl	w0,0x26901D7D			; 1e-15
	fmov	s8,w0
	fmov	s9,w0
	mov	x0,0
.e:	ldr	s0,[x21,x0,lsl 2]
	ldr	s1,[x22,x0,lsl 2]
	cbz	w26,.em
	fadd	s2,s0,s1
	fsub	s3,s0,s1
	fmadd	s8,s2,s2,s8
	fmadd	s9,s3,s3,s9
	b	.en
.em:	fmadd	s8,s0,s0,s8
	fmadd	s9,s1,s1,s9
.en:	add	x0,x0,1
	cmp	w0,w23
	b.lo	.e
	fsqrt	s1,s8
	fsqrt	s0,s9
	bl	ope_atan2
	movl	w0,0x4622F987			; 16384 x 0,63662
	fmov	s1,w0
	fmul	s0,s0,s1
	fmov	s1,0.5
	fadd	s0,s0,s1
	frintm	s0,s0
	fcvtzs	w28,s0				; itheta
	mov	x0,x20
	bl	ec_tell_frac
	str	x0,[sp]				; tell
	cmp	w27,1
	b.eq	.q1n
	mul	w0,w28,w27			; quantifie : (itheta qn + 8192) >> 14
	add	w0,w0,8192
	lsr	w28,w0,14
	cbz	w26,.tri
	cmp	w23,2
	b.le	.uni
	; stereo : densite en marche (p0 = 3 jusqu'a qn/2)
	lsr	w4,w27,1			; x0
	add	w5,w4,1
	add	w5,w5,w5,lsl 1			; (x0 + 1) p0
	add	w3,w5,w4			; ft
	cmp	w28,w4
	b.hi	.sh
	add	w1,w28,w28,lsl 1
	add	w2,w1,3
	b	.se
.sh:	sub	w1,w28,1
	sub	w1,w1,w4
	add	w1,w1,w5
	sub	w2,w28,w4
	add	w2,w2,w5
.se:	mov	x0,x20
	bl	ope_ec_encode
	b	.qsc
.uni:	mov	x0,x20
	mov	w1,w28
	add	w2,w27,1
	bl	ope_ec_uint
	b	.qsc
.tri:	lsr	w4,w27,1			; densite triangulaire
	add	w5,w4,1
	mul	w3,w5,w5			; ft
	cmp	w28,w4
	b.hi	.th
	add	w2,w28,1			; fs
	mul	w1,w28,w2
	lsr	w1,w1,1				; fl
	b	.te
.th:	add	w2,w27,1
	sub	w2,w2,w28			; fs = qn + 1 - itheta
	add	w6,w2,1
	mul	w6,w2,w6
	lsr	w6,w6,1
	sub	w1,w3,w6			; fl
.te:	add	w2,w1,w2
	mov	x0,x20
	bl	ope_ec_encode
.qsc:	lsl	w28,w28,14			; itheta = itheta 16384 / qn
	udiv	w28,w28,w27
	cbz	w26,.qa
	cbnz	w28,.split
	mov	x0,x21
	mov	x1,x22
	mov	w2,w23
	bl	ope_istereo
	b	.qa
.split:	movl	w0,0x3F3504F3			; milieu et cote (stereo_split)
	fmov	s2,w0
	mov	x0,0
.sl:	ldr	s0,[x21,x0,lsl 2]
	ldr	s1,[x22,x0,lsl 2]
	fmul	s0,s0,s2
	fmul	s1,s1,s2
	fadd	s3,s0,s1
	fsub	s4,s1,s0
	str	s3,[x21,x0,lsl 2]
	str	s4,[x22,x0,lsl 2]
	add	x0,x0,1
	cmp	w0,w23
	b.lo	.sl
	b	.qa
.q1n:	cbz	w26,.qa				; qn = 1, stereo : intensite (inversion ?)
	cmp	w28,8192
	cset	w0,gt
	str	x0,[sp,8]
	cbz	w0,.ni
	mov	x0,0
.ng:	ldr	s0,[x22,x0,lsl 2]
	fneg	s0,s0
	str	s0,[x22,x0,lsl 2]
	add	x0,x0,1
	cmp	w0,w23
	b.lo	.ng
.ni:	mov	x0,x21
	mov	x1,x22
	mov	w2,w23
	bl	ope_istereo
	cmp	w24,16
	b.le	.ninv
	ldr	x0,[x19,OE_REM]
	cmp	x0,16
	b.le	.ninv
	mov	x0,x20
	ldr	x1,[sp,8]
	mov	w2,2
	bl	ope_ec_bit_logp
.ninv:	mov	w28,0
.qa:	mov	x0,x20
	bl	ec_tell_frac
	ldr	x1,[sp]
	sub	w4,w0,w1			; qalloc
	sub	w24,w24,w4			; b
	cbnz	w28,.t1
	mov	w3,-16384
	b	.tr
.t1:	cmp	w28,16384
	b.ne	.t2
	mov	w3,16384
	b	.tr
.t2:	str	x4,[sp,8]
	mov	w0,w28
	bl	ce_bexcos
	mov	w27,w0				; imid
	mov	w0,16384
	sub	w0,w0,w28
	bl	ce_bexcos			; iside
	mov	w1,w27
	bl	ce_log2tan
	sxth	w0,w0
	sub	w1,w23,1
	lsl	w1,w1,7
	sxth	w1,w1
	mul	w0,w0,w1
	add	w0,w0,16384
	asr	w3,w0,15			; delta
	ldr	x4,[sp,8]
.tr:	mov	w0,w28
	mov	w5,w24
	add	sp,sp,16
	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, x1 = Y, w2 = N : X = intensite (gauche et droite ponderees par les
; amplitudes de la bande OE_I). Ne touche que x0-x5, v0-v4.
ope_istereo:
	ldr	x3,[x19,OE_I]
	add	x4,x19,OE_BANDE
	ldr	s0,[x4,x3,lsl 2]
	add	x3,x3,CE_NBE
	ldr	s1,[x4,x3,lsl 2]
	movl	w5,0x26901D7D			; 1e-15
	fmov	s4,w5
	fmul	s2,s0,s0
	fmadd	s2,s1,s1,s2
	fadd	s2,s2,s4
	fsqrt	s2,s2
	fadd	s2,s2,s4
	fdiv	s0,s0,s2
	fdiv	s1,s1,s2
	mov	x3,0
.l:	ldr	s2,[x0,x3,lsl 2]
	ldr	s3,[x1,x3,lsl 2]
	fmul	s2,s2,s0
	fmadd	s2,s3,s1,s2
	str	s2,[x0,x3,lsl 2]
	add	x3,x3,1
	cmp	w3,w2
	b.lo	.l
	ret

; x0 = X, w1 = N, w2 = K : rotation d'etalement, recherche PVQ, indice CWRS
; code (alg_quant)
ope_algq:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	mov	x21,x0
	mov	w22,w1				; N
	mov	w23,w2				; K
	; rotation d'etalement (normal : facteur 10), sens direct
	lsl	w0,w23,1
	cmp	w0,w22
	b.hs	.nrot
	adr	x5,ce_spreadf
	ldrb	w5,[x5,1]
	mul	w5,w5,w23
	add	w5,w5,w22
	ucvtf	s0,w22
	ucvtf	s1,w5
	fdiv	s0,s0,s1
	fmul	s0,s0,s0
	fmov	s1,0.5
	fmul	s8,s0,s1			; theta
	adr	x0,ce_halfpi
	ldr	d9,[x0]
	fcvt	d0,s8
	fmul	d0,d0,d9
	bl	ak_sincos
	fcvt	s10,d1				; c
	fmov	s0,1.0
	fsub	s0,s0,s8
	fcvt	d0,s0
	fmul	d0,d0,d9
	bl	ak_sincos
	fcvt	s11,d1				; s
	mov	w24,0				; stride2
	cmp	w22,8
	b.lo	.s2d
	mov	w24,1
.s2:	madd	w0,w24,w24,w24
	cmp	w0,w22
	b.hs	.s2d
	add	w24,w24,1
	b	.s2
.s2d:	mov	x0,x21
	mov	w1,w22
	mov	w2,1
	fmov	s0,s10
	fneg	s1,s11
	bl	ce_rot1
	cbz	w24,.nrot
	mov	x0,x21
	mov	w1,w22
	mov	w2,w24
	fmov	s0,s11
	fneg	s1,s10
	bl	ce_rot1
.nrot:	; recherche PVQ (op_pvq_search) : |X|, y = 0, impulsions a 0
	oeadr	x24,OE_A_PX
	oeadr	x25,OE_A_PY
	oeadr	x26,OE_A_IY
	movi	d0,0				; somme |X|
	mov	x0,0
.ab:	ldr	s1,[x21,x0,lsl 2]
	fabs	s1,s1
	str	s1,[x24,x0,lsl 2]
	fadd	s0,s0,s1
	str	wzr,[x25,x0,lsl 2]
	str	wzr,[x26,x0,lsl 2]
	add	x0,x0,1
	cmp	w0,w22
	b.lo	.ab
	movi	d8,0				; xy
	movi	d9,0				; yy
	mov	w27,w23				; impulsions restantes
	cmp	w23,w22,lsr 1
	b.ls	.greedy
	; projection : floor((K + 0,8) / somme |X|)
	movl	w0,0x26901D7D
	fmov	s1,w0
	fcmp	s0,s1
	b.le	.fb
	movl	w0,0x42800000			; 64
	fmov	s1,w0
	fcmp	s0,s1
	b.lt	.pj
.fb:	fmov	s0,1.0				; X = (1, 0, ...)
	str	s0,[x24]
	mov	x0,1
.fz:	cmp	w0,w22
	b.hs	.pj
	str	wzr,[x24,x0,lsl 2]
	add	x0,x0,1
	b	.fz
.pj:	ucvtf	s1,w23
	movl	w0,0x3F4CCCCD			; 0,8
	fmov	s2,w0
	fadd	s1,s1,s2
	fdiv	s1,s1,s0			; rcp
	mov	x0,0
.pl:	ldr	s2,[x24,x0,lsl 2]
	fmul	s3,s2,s1
	fcvtzs	w1,s3
	str	w1,[x26,x0,lsl 2]
	scvtf	s3,w1
	fmadd	s9,s3,s3,s9
	fmadd	s8,s2,s3,s8
	fadd	s3,s3,s3
	str	s3,[x25,x0,lsl 2]
	sub	w27,w27,w1
	add	x0,x0,1
	cmp	w0,w22
	b.lo	.pl
.greedy:
	add	w0,w22,3			; trop d'impulsions : toutes sur la premiere
	cmp	w27,w0
	b.le	.gl
	scvtf	s0,w27
	fmadd	s9,s0,s0,s9
	ldr	s1,[x25]
	fmadd	s9,s0,s1,s9
	ldr	w1,[x26]
	add	w1,w1,w27
	str	w1,[x26]
	mov	w27,0
.gl:	cmp	w27,0
	b.le	.sign
	fmov	s0,1.0
	fadd	s9,s9,s0			; yy += 1
	ldr	s1,[x24]
	fadd	s1,s8,s1
	fmul	s2,s1,s1			; best_num
	ldr	s3,[x25]
	fadd	s3,s9,s3			; best_den
	mov	x3,0
	mov	x0,1
.gj:	cmp	w0,w22
	b.hs	.gd
	ldr	s4,[x24,x0,lsl 2]
	fadd	s4,s8,s4
	fmul	s4,s4,s4			; Rxy
	ldr	s5,[x25,x0,lsl 2]
	fadd	s5,s9,s5			; Ryy
	fmul	s6,s3,s4
	fmul	s7,s5,s2
	fcmp	s6,s7
	b.le	.gn
	fmov	s3,s5
	fmov	s2,s4
	mov	x3,x0
.gn:	add	x0,x0,1
	b	.gj
.gd:	ldr	s4,[x24,x3,lsl 2]
	fadd	s8,s8,s4
	ldr	s5,[x25,x3,lsl 2]
	fadd	s9,s9,s5
	fmov	s6,2.0
	fadd	s5,s5,s6
	str	s5,[x25,x3,lsl 2]
	ldr	w1,[x26,x3,lsl 2]
	add	w1,w1,1
	str	w1,[x26,x3,lsl 2]
	sub	w27,w27,1
	b	.gl
.sign:	mov	x0,0
.sg:	ldr	s0,[x21,x0,lsl 2]
	fcmp	s0,0.0
	b.ge	.sp
	ldr	w1,[x26,x0,lsl 2]
	neg	w1,w1
	str	w1,[x26,x0,lsl 2]
.sp:	add	x0,x0,1
	cmp	w0,w22
	b.lo	.sg
	; table U(n, k), n <= N, k <= K + 1 (comme le decodeur)
	oeadr	x28,OE_A_UT
	add	w27,w23,2			; pas des lignes
	mov	w0,1
	str	w0,[x28]
	mov	x1,1
.u0:	cmp	x1,x27
	b.hs	.u0d
	str	wzr,[x28,x1,lsl 2]
	add	x1,x1,1
	b	.u0
.u0d:	mov	x2,1
.un:	cmp	x2,x22
	b.hi	.und
	mul	x3,x2,x27
	add	x3,x28,x3,lsl 2
	sub	x4,x3,x27,lsl 2
	str	wzr,[x3]
	mov	x1,1
.uk:	cmp	x1,x27
	b.hs	.ukd
	sub	x5,x1,1
	ldr	w6,[x4,x1,lsl 2]
	ldr	w7,[x3,x5,lsl 2]
	add	w6,w6,w7
	ldr	w7,[x4,x5,lsl 2]
	add	w6,w6,w7
	str	w6,[x3,x1,lsl 2]
	add	x1,x1,1
	b	.uk
.ukd:	add	x2,x2,1
	b	.un
.und:	; indice CWRS, depuis les deux dernieres dimensions
	sub	w0,w22,1
	ldr	w1,[x26,w0,uxtw 2]		; iy[N-1]
	cmp	w1,0
	cneg	w2,w1,lt			; kr
	cset	w3,lt				; indice
	sub	w0,w22,2
	ldr	w4,[x26,w0,uxtw 2]		; iy[N-2]
	cmp	w4,0
	cneg	w5,w4,lt
	add	w5,w5,w2			; k
	cbz	w2,.i0
	lsl	w6,w2,1
	sub	w6,w6,1
	add	w3,w3,w6
	b	.i1
.i0:	mov	w3,0
.i1:	cmp	w4,0
	b.ge	.i2
	lsl	w6,w5,1
	add	w6,w6,1
	add	w3,w3,w6
.i2:	mov	w2,w5				; kr
	sub	w9,w22,3			; j
.ij:	tbnz	w9,31,.id
	sub	w10,w22,w9			; n
	ldr	w4,[x26,w9,uxtw 2]
	cmp	w4,0
	cneg	w5,w4,lt
	add	w5,w5,w2			; k
	mul	w11,w10,w27
	add	x11,x28,x11,lsl 2		; ligne n
	cbnz	w4,.inz
	ldr	w6,[x11,w5,uxtw 2]		; zero : + U(n, k)
	add	w3,w3,w6
	b	.inx
.inz:	ldr	w6,[x11,w2,uxtw 2]		; + U(n, kr)
	add	w3,w3,w6
	cmp	w4,0
	b.gt	.inx
	add	w7,w5,1				; negatif : + U(n, k + 1)
	ldr	w6,[x11,w7,uxtw 2]
	add	w3,w3,w6
.inx:	mov	w2,w5
	sub	w9,w9,1
	b	.ij
.id:	mul	w11,w22,w27			; V(N, K) = U(N, K) + U(N, K + 1)
	add	x11,x28,x11,lsl 2
	ldr	w1,[x11,w23,uxtw 2]
	add	w4,w23,1
	ldr	w4,[x11,w4,uxtw 2]
	add	w2,w1,w4
	mov	w1,w3
	mov	x0,x20
	bl	ope_ec_uint
	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Reechantillonneur (frequence d'entree -> 48 kHz) : sinus cardinal fenetre
;  (Blackman), noyau en table (OE_RSZ passages par zero de chaque cote, OE_RSR
;  pas par passage, interpolation lineaire) ; coupure a 0,455 x la plus petite
;  des deux frequences. La sortie m tombe a la position m fs / 48000 de
;  l'entree (entier OE_RSIP + reste OE_RSFRAC / 48000) ; noyau symetrique :
;  pas de retard.
; ---------------------------------------------------------------------------

ope_rs_init:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	ldr	x0,[x19,OE_RATE]		; 2 fc = 0,91 min(1, 48000 / fs)
	ucvtf	d0,x0
	movl	x1,48000
	ucvtf	d1,x1
	fdiv	d1,d1,d0
	fmov	d2,1.0
	fcmp	d1,d2
	b.le	.f
	fmov	d1,d2
.f:	adr	x0,.c
	ldr	d2,[x0]
	fmul	d1,d1,d2
	fcvt	s2,d1
	str	s2,[x19,OE_RSGAIN]
	mov	x0,OE_RSR
	ucvtf	d2,x0
	fmul	d2,d2,d1
	fcvt	s2,d2
	str	s2,[x19,OE_RSSTEP]
	mov	x0,OE_RSZ			; demi-largeur : ceil(Z / 2 fc) + 1
	ucvtf	d2,x0
	fdiv	d2,d2,d1
	frintp	d2,d2
	fcvtzs	x0,d2
	add	x0,x0,1
	str	x0,[x19,OE_RSTI]
	neg	x1,x0				; historique : Ti zeros avant le debut
	str	x1,[x19,OE_RSBASE]
	str	x0,[x19,OE_RSCNT]
	str	xzr,[x19,OE_RSIP]
	str	xzr,[x19,OE_RSFRAC]
	mov	x0,-1
	lsr	x0,x0,1
	str	x0,[x19,OE_RSLIM]
	; noyau : g[j] = sinc(j / R) blackman(j / (Z R)), j < Z R
	oeadr	x21,OE_A_RSG
	fmov	s0,1.0
	str	s0,[x21]
	mov	x22,1
.g:	mov	x0,OE_RSZ*OE_RSR
	cmp	x22,x0
	b.hs	.gd
	ucvtf	d8,x22
	adr	x0,.c
	ldr	d9,[x0,8]			; pi / R
	fmul	d8,d8,d9			; pi u
	fmov	d0,d8
	bl	ak_sincos
	fdiv	d10,d0,d8			; sinc
	ucvtf	d8,x22
	adr	x0,.c
	ldr	d9,[x0,16]			; pi / (Z R)
	fmul	d8,d8,d9			; pi x
	fmov	d0,d8
	bl	ak_sincos
	fmov	d11,d1				; cos(pi x)
	fadd	d0,d8,d8
	bl	ak_sincos			; cos(2 pi x)
	adr	x0,.c
	ldr	d2,[x0,24]			; 0,42 + 0,5 cos + 0,08 cos 2
	fmov	d3,0.5
	fmadd	d2,d11,d3,d2
	ldr	d3,[x0,32]
	fmadd	d2,d1,d3,d2
	fmul	d10,d10,d2
	fcvt	s0,d10
	str	s0,[x21,x22,lsl 2]
	add	x22,x22,1
	b	.g
.gd:	; phases : 48000 / pgcd ; leurs poids en table si elle tient
	ldr	x0,[x19,OE_RATE]
	movl	x1,48000
.gcd:	udiv	x2,x0,x1
	msub	x2,x2,x1,x0
	mov	x0,x1
	mov	x1,x2
	cbnz	x1,.gcd
	mov	x21,x0				; pgcd
	movl	x1,48000
	udiv	x22,x1,x21			; phases
	ldr	x1,[x19,OE_RSTI]
	lsl	x1,x1,1
	mul	x1,x1,x22
	movl	x2,OE_RSWTN
	cmp	x1,x2
	b.hi	.out
	str	x21,[x19,OE_RSG]
	ldr	x0,[x19,OE_RSTI]
	lsl	x0,x0,3				; octets d'une phase
	str	x0,[sp,-16]!
	mov	x21,0				; reste (multiple du pgcd)
	oeadr	x0,OE_A_RSWT
	str	x0,[sp,8]
.ph:	cbz	x22,.phd
	ldr	x0,[sp,8]
	mov	x1,x21
	bl	ope_rs_weights
	ldp	x1,x0,[sp]
	add	x0,x0,x1
	str	x0,[sp,8]
	ldr	x0,[x19,OE_RSG]
	add	x21,x21,x0
	sub	x22,x22,1
	b	.ph
.phd:	add	sp,sp,16
.out:	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x21,x22,[sp],16
	epilogue
		align	8
.c:	dd	0.91, 0.02454369260617026, 0.0007669903939428206, 0.42, 0.08

; x0 = poids (2 Ti flottants), x1 = reste (sur 48000) : poids des prises
; t = (m - Ti + 1) - reste / 48000. Ne touche que x0-x7, v0-v7.
ope_rs_weights:
	ucvtf	s4,x1
	movl	w1,0x37AEC33E			; 1 / 48000
	fmov	s1,w1
	fmul	s4,s4,s1
	ldr	s5,[x19,OE_RSSTEP]
	ldr	s6,[x19,OE_RSGAIN]
	oeadr	x7,OE_A_RSG
	ldr	x1,[x19,OE_RSTI]
	lsl	x2,x1,1
	mov	x3,0
	mov	w4,OE_RSZ*OE_RSR
.w:	cmp	x3,x2
	b.hs	.r
	sub	x5,x3,x1
	add	x5,x5,1
	scvtf	s0,x5
	fsub	s0,s0,s4
	fabs	s0,s0
	fmul	s0,s0,s5			; position dans la table
	fcvtzu	w5,s0
	movi	d3,0
	cmp	w5,w4
	b.hs	.ws
	ucvtf	s1,w5
	fsub	s1,s0,s1
	ldr	s2,[x7,w5,uxtw 2]
	add	w6,w5,1
	ldr	s3,[x7,w6,uxtw 2]
	fsub	s3,s3,s2
	fmadd	s3,s3,s1,s2
	fmul	s3,s3,s6
.ws:	str	s3,[x0,x3,lsl 2]
	add	x3,x3,1
	b	.w
.r:	ret

; x0 = PCM s16 entrelace, x1 = trames : dans l'historique, puis les sorties
ope_rs_push:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	ldr	x23,[x19,OE_C]
.l:	cbz	x22,.out
	ldr	x0,[x19,OE_RSCNT]
	movl	x1,OE_RSHB
	sub	x1,x1,x0
	cmp	x1,x22
	csel	x24,x1,x22,lo
	oeadr	x2,OE_A_RSH
	add	x2,x2,x0,lsl 2
	movl	x3,OE_RSHB * 4
	add	x3,x2,x3
	mov	x4,0
.c:	cmp	x4,x24
	b.hs	.cd
	ldrsh	w5,[x21]
	scvtf	s0,w5
	str	s0,[x2,x4,lsl 2]
	cmp	x23,2
	b.ne	.c1
	ldrsh	w5,[x21,2]
	scvtf	s0,w5
	str	s0,[x3,x4,lsl 2]
.c1:	add	x21,x21,x23,lsl 1
	add	x4,x4,1
	b	.c
.cd:	add	x0,x0,x24
	str	x0,[x19,OE_RSCNT]
	sub	x22,x22,x24
	bl	ope_rs_run
	b	.l
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; fin : sorties jusqu'a ceil(entree x 48000 / fs), l'entree prolongee de zeros
ope_rs_flush:
	prologue
	ldr	x0,[x19,OE_NIN]
	movl	x1,48000
	mul	x0,x0,x1
	ldr	x1,[x19,OE_RATE]
	add	x0,x0,x1
	sub	x0,x0,1
	udiv	x0,x0,x1
	str	x0,[x19,OE_RSLIM]
	ldr	x0,[x19,OE_RSCNT]
	ldr	x1,[x19,OE_RSTI]
	add	x1,x1,2				; zeros
	oeadr	x2,OE_A_RSH
	add	x2,x2,x0,lsl 2
	movl	x3,OE_RSHB * 4
	add	x3,x2,x3
	mov	x4,0
.z:	cmp	x4,x1
	b.hs	.zd
	str	wzr,[x2,x4,lsl 2]
	str	wzr,[x3,x4,lsl 2]
	add	x4,x4,1
	b	.z
.zd:	add	x0,x0,x1
	str	x0,[x19,OE_RSCNT]
	bl	ope_rs_run
	epilogue

; les sorties calculables (vers ope_feed), puis l'historique inutile part
ope_rs_run:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
.l:	ldr	x21,[x19,OE_RSIP]
	ldr	x22,[x19,OE_RSTI]
	ldr	x23,[x19,OE_RSBASE]
	ldr	x24,[x19,OE_RSCNT]
	add	x0,x21,x22			; ip + Ti dans l'historique ?
	sub	x0,x0,x23
	cmp	x0,x24
	b.ge	.done
	ldr	x0,[x19,OE_LEN48]
	ldr	x1,[x19,OE_RSLIM]
	cmp	x0,x1
	b.ge	.done
	; poids de la phase : en table, sinon calcules
	ldr	x0,[x19,OE_RSG]
	cbz	x0,.wc
	ldr	x1,[x19,OE_RSFRAC]
	udiv	x1,x1,x0			; phase
	lsl	x2,x22,3
	mul	x1,x1,x2
	oeadr	x25,OE_A_RSWT
	add	x25,x25,x1
	b	.wd
.wc:	oeadr	x25,OE_A_RSW
	mov	x0,x25
	ldr	x1,[x19,OE_RSFRAC]
	bl	ope_rs_weights
.wd:	lsl	x2,x22,1
	sub	x0,x21,x22			; H[ip - Ti + 1 - base + m]
	add	x0,x0,1
	sub	x0,x0,x23
	oeadr	x1,OE_A_RSH
	add	x1,x1,x0,lsl 2
	movi	d0,0
	mov	x3,0
.d0:	ldr	s2,[x1,x3,lsl 2]
	ldr	s3,[x25,x3,lsl 2]
	fmadd	s0,s2,s3,s0
	add	x3,x3,1
	cmp	x3,x2
	b.lo	.d0
	fmov	s1,s0
	ldr	x4,[x19,OE_C]
	cmp	x4,2
	b.ne	.d2
	movi	d1,0
	movl	x4,OE_RSHB * 4
	add	x1,x1,x4
	mov	x3,0
.d1:	ldr	s2,[x1,x3,lsl 2]
	ldr	s3,[x25,x3,lsl 2]
	fmadd	s1,s2,s3,s1
	add	x3,x3,1
	cmp	x3,x2
	b.lo	.d1
.d2:	ldr	x0,[x19,OE_LEN48]
	add	x0,x0,1
	str	x0,[x19,OE_LEN48]
	bl	ope_feed
	ldr	x0,[x19,OE_RSFRAC]		; position suivante
	ldr	x1,[x19,OE_RATE]
	add	x0,x0,x1
	ldr	x2,[x19,OE_RSIP]
	movl	x3,48000
.a:	cmp	x0,x3
	b.lo	.ad
	sub	x0,x0,x3
	add	x2,x2,1
	b	.a
.ad:	str	x0,[x19,OE_RSFRAC]
	str	x2,[x19,OE_RSIP]
	b	.l
.done:	; garde l'historique depuis ip - Ti + 1
	sub	x25,x21,x22
	add	x25,x25,1
	sub	x25,x25,x23			; decalage
	cmp	x25,0
	b.le	.out
	cmp	x25,x24
	csel	x25,x25,x24,lt
	sub	x26,x24,x25			; restants
	oeadr	x0,OE_A_RSH
	add	x1,x0,x25,lsl 2
	lsl	x2,x26,2
	bl	ak_memmove
	ldr	x0,[x19,OE_C]
	cmp	x0,2
	b.ne	.c1
	oeadr	x0,OE_A_RSH+OE_RSHB*4
	add	x1,x0,x25,lsl 2
	lsl	x2,x26,2
	bl	ak_memmove
.c1:	add	x23,x23,x25
	str	x23,[x19,OE_RSBASE]
	str	x26,[x19,OE_RSCNT]
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Donnees
; ---------------------------------------------------------------------------
ope_head	db	'OpusHead', 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
ope_tags	db	'OpusTags', 14, 0, 0, 0, 'Korin AudioKit', 0, 0, 0, 0
		align	2
ope_ithr	dh	1, 2, 3, 4, 5, 6, 7, 8, 16, 24, 36, 44, 50, 56, 62, 67, 72, 79, 88, 106, 134
		align	4
