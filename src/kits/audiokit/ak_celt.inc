; ============================================================================
;  kits/audiokit/ak_celt.inc - CELT (RFC 6716 4.3) : decodeur
;
;  Une trame : silence, post-filtre (filtre en peigne), transitoire (blocs
;  courts), energies grossieres (prediction, Laplace), tf_res, etalement,
;  dynalloc, trim, allocation des bits (4.3.3), energies fines, bandes
;  (quantification vectorielle PVQ, CWRS ; division recursive des bandes,
;  theta, stereo d'intensite, double stereo, M/S ; repliement avec la
;  graine), anti-effondrement, energies finales, denormalisation, IMDCT
;  (ak_mdct.inc), recouvrement de 120 (TDAC), post-filtre, desaccentuation.
;  Flottant 32 bits ; le signal interne est a l'echelle de 32768.
;
;  Dans tout le fichier : x19 = etat CELT (CE_*), x20 = decodeur de plage ;
;  aucune fonction ne les change.
; ============================================================================

CE_CC		= 0		; canaux de sortie (1, 2)
CE_C		= 8		; canaux de la trame
CE_START	= 16		; premiere bande (17 en hybride)
CE_END		= 24		; bande de fin (largeur de bande)
CE_N		= 32		; echantillons de la trame
CE_LM		= 40		; log2(N / 120)
CE_LEN		= 48		; octets de la trame
CE_SEED		= 64		; graine (rng final de la trame precedente)
CE_PFPER	= 72		; post-filtre : periode, gain, jeu de prises
CE_PFPEROLD	= 80
CE_PFGAIN	= 88		; (flottants)
CE_PFGAINOLD	= 96
CE_PFTAP	= 104
CE_PFTAPOLD	= 112
CE_PREEMPH	= 120		; memoire de la desaccentuation (2 flottants)
CE_OUT		= 128		; sortie de la trame (2 pointeurs, flottants -1..1)
CE_MEM		= 144		; historique : 2 x (2048 + 120) flottants
CE_X		= 160		; spectres normalises (2 x 960)
CE_NORM		= 168		; repliement : norm, norm2 (2 x 1024)
CE_FREQ		= 176		; spectres denormalises (2 x 960)
CE_TMP		= 184		; IMDCT, permutations (3 x 960)
CE_UT		= 192		; table U(n, k) de CWRS (2048 mots)
CE_IY		= 200		; impulsions (256 mots)
CE_MD		= 208		; IMDCT (4 plans MD_SIZE : N = 240 << k)
CE_SILENCE	= 216
CE_TRANS	= 224
CE_INTRA	= 232
CE_SPREAD	= 240
CE_TRIM		= 248
CE_INTENS	= 256
CE_DUAL		= 264
CE_CODED	= 272
CE_BALANCE	= 280
CE_ANTIRSV	= 288
CE_PFPITCH	= 296		; post-filtre de la trame
CE_PFG		= 304
CE_PFTAPN	= 312
CE_B		= 320		; blocs (M si transitoire, sinon 1)
CE_I		= 328		; bande courante
CE_TFC		= 336		; tf_change de la bande
CE_REM		= 344		; remaining_bits
CE_TOTAL	= 352		; bits de la trame (1/8)
CE_ACCUM	= 360		; 1 : la sortie s'ajoute a CE_OUT (hybride : SILK deja la)
CE_OLDE		= 512		; energies (log2, 2 x 21 flottants)
CE_OLDLOGE	= CE_OLDE + 192
CE_OLDLOGE2	= CE_OLDLOGE + 192
CE_TFRES	= CE_OLDLOGE2 + 192	; tableaux de 21 entiers (32 bits)
CE_CAP		= CE_TFRES + 96
CE_OFFS		= CE_CAP + 96
CE_PULSES	= CE_OFFS + 96
CE_FINEQ	= CE_PULSES + 96
CE_PRIO		= CE_FINEQ + 96
CE_THRESH	= CE_PRIO + 96
CE_TRIMOFF	= CE_THRESH + 96
CE_BITS1	= CE_TRIMOFF + 96
CE_BITS2	= CE_BITS1 + 96
CE_CMASK	= CE_BITS2 + 96	; masques d'effondrement (2 x 21 octets)
CE_SIZE		= CE_CMASK + 64

CE_MEMN		= 2048 + 120	; flottants d'historique par canal
CE_NBE		= 21

; x0 = decodeur (memoire), x1 = canaux, x2 = IMDCT -> x0 = etat / < 0
celt_new:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x1,CE_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x19,x0
	str	x21,[x19,CE_CC]
	str	x22,[x19,CE_MD]
	mov	x0,x20
	movl	x1,CE_MEMN * 4 * 2 + 64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x19,CE_MEM]
	movl	x1,CE_MEMN * 4
	add	x0,x0,x1
	str	x0,[x19,CE_MEM+8]
	mov	x0,x20
	movl	x1,2 * 960 * 4 + 64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x19,CE_X]
	mov	x0,x20
	movl	x1,2 * 1024 * 4 + 64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x19,CE_NORM]
	mov	x0,x20
	movl	x1,2 * 960 * 4 + 64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x19,CE_FREQ]
	mov	x0,x20
	movl	x1,3 * 960 * 4 + 64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x19,CE_TMP]
	mov	x0,x20
	movl	x1,2048 * 4 + 64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x19,CE_UT]
	mov	x0,x20
	movl	x1,256 * 4 + 64
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x19,CE_IY]
	mov	x0,x19
	bl	celt_reset
	mov	x0,x19
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat CELT : oublie l'historique (OPUS_RESET_STATE)
celt_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,CE_MEM]
	movl	x1,CE_MEMN * 4 * 2
	bl	ak_memzero
	str	xzr,[x19,CE_SEED]
	str	xzr,[x19,CE_PFPER]
	str	xzr,[x19,CE_PFPEROLD]
	str	xzr,[x19,CE_PFGAIN]
	str	xzr,[x19,CE_PFGAINOLD]
	str	xzr,[x19,CE_PFTAP]
	str	xzr,[x19,CE_PFTAPOLD]
	str	xzr,[x19,CE_PREEMPH]
	movl	w1,0xC1E00000			; -28
	mov	x0,0
.e:	add	x2,x19,CE_OLDE
	str	wzr,[x2,x0,lsl 2]
	add	x2,x19,CE_OLDLOGE
	str	w1,[x2,x0,lsl 2]
	add	x2,x19,CE_OLDLOGE2
	str	w1,[x2,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,2 * CE_NBE
	b.lo	.e
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Outils
; ---------------------------------------------------------------------------

; s0 = x -> s0 = 2^x (double en interne ; 0 sous 2^-1000). Ne touche que
; x0-x2, d0-d4.
ce_exp2:
	fcvt	d0,s0
	frintn	d1,d0				; k
	fsub	d0,d0,d1			; f dans [-1/2, 1/2]
	fcvtzs	x0,d1
	adr	x1,.c
	ldr	d2,[x1]				; ln 2
	fmul	d0,d0,d2			; t = f ln 2
	ldr	d3,[x1,8*8]			; e^t (Horner : 1/7! ... 1/0!)
	add	x1,x1,7*8
	mov	x2,7
.h:	ldr	d4,[x1],-8
	fmadd	d3,d3,d0,d4
	subs	x2,x2,1
	b.ne	.h
	cmn	x0,1000
	b.lt	.zero
	cmp	x0,1000
	b.gt	.big
	add	x0,x0,1023
	lsl	x0,x0,52
	fmov	d4,x0
	fmul	d3,d3,d4
	fcvt	s0,d3
	ret
.zero:	movi	d0,0
	ret
.big:	movl	w0,0x7F7FFFFF
	fmov	s0,w0
	ret
		align	8
.c:	dd	0.6931471805599453
	dd	1.0, 1.0, 0.5, 0.16666666666666666, 0.041666666666666664
	dd	0.008333333333333333, 0.001388888888888889, 0.0001984126984126984

; w0 = x (0..16384) -> w0 = cos approche bit-exact (bitexact_cos). Ne touche
; que x0-x4.
ce_bexcos:
	mul	w1,w0,w0
	add	w1,w1,4096
	asr	w1,w1,13			; x2
	mov	w2,-626				; FRAC_MUL16(-626, x2)
	mul	w2,w2,w1
	add	w2,w2,16384
	asr	w2,w2,15
	mov	w4,8277
	add	w2,w2,w4
	sxth	w2,w2
	mul	w2,w2,w1
	add	w2,w2,16384
	asr	w2,w2,15
	mov	w4,7651
	sub	w2,w2,w4
	sxth	w2,w2
	mul	w2,w2,w1
	add	w2,w2,16384
	asr	w2,w2,15
	mov	w3,32767
	sub	w3,w3,w1
	add	w0,w3,w2
	add	w0,w0,1
	ret

; w0 = isin, w1 = icos -> w0 = log2(isin / icos) en Q11 (bitexact_log2tan).
; Ne touche que x0-x6.
ce_log2tan:
	clz	w2,w1
	mov	w3,32
	sub	w2,w3,w2			; lc
	clz	w4,w0
	sub	w4,w3,w4			; ls
	mov	w3,15
	sub	w5,w3,w2
	lsl	w1,w1,w5			; icos << (15 - lc)
	sub	w5,w3,w4
	lsl	w0,w0,w5
	sub	w4,w4,w2
	lsl	w4,w4,11			; (ls - lc) << 11
	mov	w2,-2597			; + FRAC_MUL16(isin, FRAC_MUL16(isin, -2597) + 7932)
	mov	w6,7932
	mul	w3,w0,w2
	add	w3,w3,16384
	asr	w3,w3,15
	add	w3,w3,w6
	sxth	w3,w3
	mul	w3,w3,w0
	add	w3,w3,16384
	asr	w3,w3,15
	add	w4,w4,w3
	mul	w3,w1,w2			; - FRAC_MUL16(icos, ...)
	add	w3,w3,16384
	asr	w3,w3,15
	add	w3,w3,w6
	sxth	w3,w3
	mul	w3,w3,w1
	add	w3,w3,16384
	asr	w3,w3,15
	sub	w0,w4,w3
	ret

; w0 = v -> w0 = floor(sqrt(v)) (isqrt32). Ne touche que x0, d0.
ce_isqrt:
	ucvtf	d0,w0
	fsqrt	d0,d0
	fcvtzu	w0,d0
	ret

; w0 = bande, w1 = LM (-1..3) -> x0 = cache des impulsions de la bande
; (cache[0] = impulsions max, cache[q] = bits - 1). Ne touche que x0-x2.
ce_cache:
	add	w1,w1,1
	mov	w2,CE_NBE
	madd	w0,w1,w2,w0
	adr	x1,ce_cindex
	ldrsh	x0,[x1,x0,lsl 1]
	adr	x1,ce_cbits
	add	x0,x1,x0
	ret

; w0 = bande, w1 = LM, w2 = bits (1/8) -> w0 = q (bits2pulses). Ne touche que
; x0-x7.
ce_b2p:
	stp	x29,x30,[sp,-16]!
	mov	w7,w2
	bl	ce_cache
	sub	w7,w7,1				; bits - 1
	mov	w3,0				; lo
	ldrb	w4,[x0]				; hi
	mov	w5,6
.l:	add	w6,w3,w4
	add	w6,w6,1
	lsr	w6,w6,1				; mid
	ldrb	w1,[x0,w6,uxtw]
	cmp	w1,w7
	csel	w4,w6,w4,ge
	csel	w3,w3,w6,ge
	subs	w5,w5,1
	b.ne	.l
	mov	w1,-1				; bits - (lo ? cache[lo] : -1) <= cache[hi] - bits
	cbz	w3,.lo0
	ldrb	w1,[x0,w3,uxtw]
.lo0:	sub	w1,w7,w1
	ldrb	w2,[x0,w4,uxtw]
	sub	w2,w2,w7
	cmp	w1,w2
	csel	w0,w3,w4,le
	ldp	x29,x30,[sp],16
	ret

; w0 = bande, w1 = LM, w2 = q -> w0 = bits (1/8) (pulses2bits). Ne touche
; que x0-x7.
ce_p2b:
	cbz	w2,.z
	stp	x29,x30,[sp,-16]!
	mov	w7,w2
	bl	ce_cache
	ldrb	w0,[x0,w7,uxtw]
	add	w0,w0,1
	ldp	x29,x30,[sp],16
	ret
.z:	mov	w0,0
	ret

; w0 = q -> w0 = impulsions K (get_pulses)
ce_getp:
	cmp	w0,8
	b.lo	.r
	and	w1,w0,7
	add	w1,w1,8
	lsr	w2,w0,3
	sub	w2,w2,1
	lsl	w0,w1,w2
.r:	ret

; ---------------------------------------------------------------------------
;  Trame
; ---------------------------------------------------------------------------

; x0 = etat CELT (CE_START, CE_END, CE_OUT poses), x1 = octets, x2 = N
; (120, 240, 480, 960), x3 = canaux de la trame, x4 = decodeur de plage
; (initialise sur la trame) -> x0 = N / < 0 ; sortie dans CE_OUT
celt_decode:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	mov	x19,x0
	mov	x20,x4
	str	x1,[x19,CE_LEN]
	str	x2,[x19,CE_N]
	str	x3,[x19,CE_C]
	mov	x0,0				; LM
	mov	x4,120
.lm:	cmp	x4,x2
	b.eq	.lmok
	add	x0,x0,1
	lsl	x4,x4,1
	cmp	x0,3
	b.ls	.lm
	mov	x0,AKE_CORRUPT
	b	.out
.lmok:	str	x0,[x19,CE_LM]
	mov	x25,x0				; LM
	mov	x21,x1				; octets
	ldr	x22,[x19,CE_START]
	ldr	x23,[x19,CE_END]
	ldr	x24,[x19,CE_C]
	cmp	x24,1				; mono : oldE = max(gauche, droite)
	b.ne	.st
	add	x0,x19,CE_OLDE
	mov	x1,0
.mx:	ldr	s0,[x0,x1,lsl 2]
	add	x2,x1,CE_NBE
	ldr	s1,[x0,x2,lsl 2]
	fmax	s0,s0,s1
	str	s0,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,CE_NBE
	b.lo	.mx
.st:	lsl	x26,x21,3			; total_bits
	mov	x0,x20
	bl	ec_tell
	mov	x27,x0				; tell
	; silence
	mov	x28,1
	cmp	x27,x26
	b.ge	.sil
	mov	x28,0
	cmp	x27,1
	b.ne	.sil
	mov	x0,x20
	mov	w1,15
	bl	ec_bit_logp
	mov	x28,x0
.sil:	str	x28,[x19,CE_SILENCE]
	cbz	x28,.pf
	mov	x0,x20				; tous les bits sont comme lus
	bl	ec_tell
	sub	x1,x26,x0
	ldr	x2,[x20,EC_NBITS]
	add	x2,x2,x1
	str	x2,[x20,EC_NBITS]
	mov	x27,x26
.pf:	; post-filtre
	str	xzr,[x19,CE_PFPITCH]
	str	xzr,[x19,CE_PFG]
	str	xzr,[x19,CE_PFTAPN]
	cbnz	x22,.npf
	add	x0,x27,16
	cmp	x0,x26
	b.gt	.npf
	mov	x0,x20
	mov	w1,1
	bl	ec_bit_logp
	cbz	w0,.pft
	mov	x0,x20
	mov	w1,6
	bl	ec_uint
	mov	w9,w0				; octave
	mov	x0,x20
	add	w1,w9,4
	bl	ec_bits
	mov	w1,16
	lsl	w1,w1,w9
	add	w0,w0,w1
	sub	w0,w0,1
	str	x0,[x19,CE_PFPITCH]
	mov	x0,x20
	mov	w1,3
	bl	ec_bits
	mov	w9,w0				; qg
	mov	x0,x20
	bl	ec_tell
	add	x0,x0,2
	cmp	x0,x26
	b.gt	.ntap
	mov	x0,x20
	adr	x1,ce_tapset_icdf
	mov	w2,2
	bl	ec_icdf
	str	x0,[x19,CE_PFTAPN]
.ntap:	add	w9,w9,1				; gain = 0,09375 (qg + 1)
	scvtf	s0,w9
	movl	w0,0x3DC00000
	fmov	s1,w0
	fmul	s0,s0,s1
	str	s0,[x19,CE_PFG]
.pft:	mov	x0,x20
	bl	ec_tell
	mov	x27,x0
.npf:	; transitoire
	mov	x9,0
	cbz	x25,.ntr
	add	x0,x27,3
	cmp	x0,x26
	b.gt	.ntr
	mov	x0,x20
	mov	w1,3
	bl	ec_bit_logp
	mov	x9,x0
	mov	x0,x20
	bl	ec_tell
	mov	x27,x0
.ntr:	str	x9,[x19,CE_TRANS]
	mov	x0,1
	cbz	x9,.b1
	lsl	x0,x0,x25
.b1:	str	x0,[x19,CE_B]
	; energies : intra, grossieres
	mov	x9,0
	add	x0,x27,3
	cmp	x0,x26
	b.gt	.nin
	mov	x0,x20
	mov	w1,3
	bl	ec_bit_logp
	mov	x9,x0
.nin:	str	x9,[x19,CE_INTRA]
	bl	ce_coarse
	bl	ce_tf
	; etalement
	mov	x0,x20
	bl	ec_tell
	mov	x9,2
	add	x0,x0,4
	cmp	x0,x26
	b.gt	.nsp
	mov	x0,x20
	adr	x1,ce_spread_icdf
	mov	w2,5
	bl	ec_icdf
	mov	x9,x0
.nsp:	str	x9,[x19,CE_SPREAD]
	; plafonds : (caps + 64) C N / 4
	adr	x9,ce_eband
	adr	x10,ce_ccaps
	lsl	x0,x25,1
	add	x0,x0,x24
	sub	x0,x0,1
	mov	x1,CE_NBE
	mul	x0,x0,x1
	add	x10,x10,x0
	mov	x11,0
.cap:	ldrh	w0,[x9,x11,lsl 1]
	add	x1,x11,1
	ldrh	w1,[x9,x1,lsl 1]
	sub	w0,w1,w0
	lsl	w0,w0,w25			; N
	ldrb	w1,[x10,x11]
	add	w1,w1,64
	mul	w1,w1,w24
	mul	w1,w1,w0
	lsr	w1,w1,2
	add	x2,x19,CE_CAP
	str	w1,[x2,x11,lsl 2]
	add	x11,x11,1
	cmp	x11,CE_NBE
	b.lo	.cap
	; dynalloc
	lsl	x26,x26,3			; total_bits (1/8)
	mov	x0,x20
	bl	ec_tell_frac
	mov	x27,x0				; tell
	mov	x12,6				; dynalloc_logp
	mov	x11,x22				; i
.dy:	cmp	x11,x23
	b.hs	.dyd
	ldrh	w0,[x9,x11,lsl 1]
	add	x1,x11,1
	ldrh	w1,[x9,x1,lsl 1]
	sub	w0,w1,w0
	mul	w0,w0,w24
	lsl	w0,w0,w25			; width
	lsl	w1,w0,3
	mov	w2,48
	cmp	w0,w2
	csel	w2,w0,w2,gt
	cmp	w1,w2
	csel	w13,w1,w2,lt			; quanta
	mov	x14,x12				; logp de la boucle
	mov	x15,0				; boost
	add	x2,x19,CE_CAP
	ldr	w16,[x2,x11,lsl 2]
.dl:	add	x0,x27,x14,lsl 3
	cmp	x0,x26
	b.ge	.dle
	cmp	x15,x16
	b.ge	.dle
	mov	x0,x20
	mov	w1,w14
	bl	ec_bit_logp
	mov	w17,w0
	mov	x0,x20
	bl	ec_tell_frac
	mov	x27,x0
	cbz	w17,.dle
	add	x15,x15,x13
	sub	x26,x26,x13
	mov	x14,1
	b	.dl
.dle:	add	x2,x19,CE_OFFS
	str	w15,[x2,x11,lsl 2]
	cbz	x15,.dn
	sub	x12,x12,1
	cmp	x12,2
	mov	x0,2
	csel	x12,x0,x12,lt
.dn:	add	x11,x11,1
	b	.dy
.dyd:	str	x26,[x19,CE_TOTAL]
	; trim
	mov	x9,5
	add	x0,x27,48
	cmp	x0,x26
	b.gt	.ntrim
	mov	x0,x20
	adr	x1,ce_trim_icdf
	mov	w2,7
	bl	ec_icdf
	mov	x9,x0
.ntrim:	str	x9,[x19,CE_TRIM]
	; bits restants, reserve d'anti-effondrement
	mov	x0,x20
	bl	ec_tell_frac
	lsl	x1,x21,6
	sub	x1,x1,x0
	sub	x1,x1,1				; bits
	mov	x2,0
	ldr	x3,[x19,CE_TRANS]
	cbz	x3,.nar
	cmp	x25,2
	b.lt	.nar
	add	x3,x25,2
	lsl	x3,x3,3
	cmp	x1,x3
	b.lt	.nar
	mov	x2,8
.nar:	str	x2,[x19,CE_ANTIRSV]
	sub	x1,x1,x2
	bl	ce_alloc
	bl	ce_fine
	; historique : decalage de N
	mov	x26,0
.sh:	ldr	x0,[x19,CE_CC]
	cmp	x26,x0
	b.hs	.shd
	add	x0,x19,CE_MEM
	ldr	x0,[x0,x26,lsl 3]
	ldr	x2,[x19,CE_N]
	add	x1,x0,x2,lsl 2
	movl	x3,CE_MEMN
	sub	x2,x3,x2
	lsl	x2,x2,2
	bl	ak_memmove
	add	x26,x26,1
	b	.sh
.shd:	bl	ce_bands
	; anti-effondrement, energies finales
	mov	x26,0
	ldr	x0,[x19,CE_ANTIRSV]
	cbz	x0,.nac
	mov	x0,x20
	mov	w1,1
	bl	ec_bits
	mov	x26,x0
.nac:	bl	ce_final
	cbz	x26,.nanti
	bl	ce_anti
.nanti:	ldr	x0,[x19,CE_SILENCE]
	cbz	x0,.nsil
	movl	w1,0xC1E00000			; -28
	add	x0,x19,CE_OLDE
	mov	x2,0
.sl:	str	w1,[x0,x2,lsl 2]
	add	x2,x2,1
	cmp	x2,2 * CE_NBE
	b.lo	.sl
.nsil:	bl	ce_synth
	bl	ce_postfilter
	bl	ce_history
	bl	ce_deemph
	ldr	x0,[x19,CE_N]
.out:	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; energies grossieres (RFC 6716 4.3.2.1) : prediction temporelle (sauf intra)
; et frequentielle, residus de Laplace
ce_coarse:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	ldr	x21,[x19,CE_START]
	ldr	x22,[x19,CE_END]
	ldr	x23,[x19,CE_C]
	ldr	x24,[x19,CE_LM]
	ldr	x25,[x19,CE_INTRA]
	adr	x26,ce_eprob			; modele : [LM][intra][42]
	add	x0,x25,x24,lsl 1
	mov	x1,42
	madd	x26,x0,x1,x26
	adr	x0,ce_pred
	cbz	x25,.inter
	movi	d8,0				; coef
	ldr	s9,[x0,32]			; beta intra
	b	.cb
.inter:	ldr	s8,[x0,x24,lsl 2]
	add	x0,x0,16
	ldr	s9,[x0,x24,lsl 2]
.cb:	movi	d10,0				; prev[0], prev[1]
	movi	d11,0
	ldr	x27,[x20,EC_STORAGE]
	lsl	x27,x27,3			; budget
	add	x28,x19,CE_OLDE
	mov	x9,x21				; i
.i:	cmp	x9,x22
	b.hs	.done
	mov	x10,0				; c
.c:	cmp	x10,x23
	b.hs	.ni
	mov	x0,x20
	bl	ec_tell
	sub	x11,x27,x0
	cmp	x11,15
	b.lt	.small
	mov	x1,20
	cmp	x9,x1
	csel	x1,x9,x1,lo
	lsl	x1,x1,1
	ldrb	w2,[x26,x1]
	add	x1,x1,1
	ldrb	w3,[x26,x1]
	lsl	w1,w2,7
	lsl	w2,w3,6
	mov	x0,x20
	bl	ec_laplace
	b	.q
.small:	cmp	x11,2
	b.lt	.one
	mov	x0,x20
	adr	x1,ce_small_icdf
	mov	w2,2
	bl	ec_icdf
	and	w1,w0,1				; (qi >> 1) ^ -(qi & 1)
	neg	w1,w1
	eor	w0,w1,w0,lsr 1
	b	.q
.one:	cmp	x11,1
	b.lt	.none
	mov	x0,x20
	mov	w1,1
	bl	ec_bit_logp
	neg	w0,w0
	b	.q
.none:	mov	w0,-1
.q:	scvtf	s0,w0				; q
	mov	x1,CE_NBE
	madd	x1,x10,x1,x9			; i + c 21
	ldr	s1,[x28,x1,lsl 2]
	movl	w2,0xC1100000			; -9
	fmov	s2,w2
	fmax	s1,s1,s2
	fmul	s1,s1,s8			; coef x old
	cbnz	x10,.p1
	fadd	s1,s1,s10
	fadd	s1,s1,s0
	fadd	s10,s10,s0			; prev = prev + q - beta q
	fmsub	s10,s9,s0,s10
	b	.st
.p1:	fadd	s1,s1,s11
	fadd	s1,s1,s0
	fadd	s11,s11,s0
	fmsub	s11,s9,s0,s11
.st:	str	s1,[x28,x1,lsl 2]
	add	x10,x10,1
	b	.c
.ni:	add	x9,x9,1
	b	.i
.done:	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; tf_res (RFC 6716 4.3.4.5) -> CE_TFRES
ce_tf:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	ldr	x21,[x19,CE_START]
	ldr	x22,[x19,CE_END]
	ldr	x23,[x19,CE_TRANS]
	ldr	x24,[x19,CE_LM]
	ldr	x25,[x20,EC_STORAGE]
	lsl	x25,x25,3			; budget
	mov	x0,x20
	bl	ec_tell
	mov	x9,x0				; tell
	mov	x10,4				; logp
	cbz	x23,.l4
	mov	x10,2
.l4:	mov	x11,0				; tf_select_rsv
	cbz	x24,.nrsv
	add	x0,x9,x10
	add	x0,x0,1
	cmp	x0,x25
	b.gt	.nrsv
	mov	x11,1
.nrsv:	sub	x25,x25,x11
	mov	x12,0				; tf_changed
	mov	x13,0				; curr
	mov	x14,x21
.i:	cmp	x14,x22
	b.hs	.sel
	add	x0,x9,x10
	cmp	x0,x25
	b.gt	.keep
	mov	x0,x20
	mov	w1,w10
	bl	ec_bit_logp
	eor	x13,x13,x0
	mov	x0,x20
	bl	ec_tell
	mov	x9,x0
	orr	x12,x12,x13
.keep:	add	x0,x19,CE_TFRES
	str	w13,[x0,x14,lsl 2]
	mov	x10,5
	cbz	x23,.l5
	mov	x10,4
.l5:	add	x14,x14,1
	b	.i
.sel:	adr	x26,ce_tfsel			; [LM][8]
	add	x26,x26,x24,lsl 3
	add	x26,x26,x23,lsl 2		; + 4 isTransient
	mov	x15,0				; tf_select
	cbz	x11,.app
	ldrsb	w0,[x26,x12]
	add	x1,x12,2
	ldrsb	w1,[x26,x1]
	cmp	w0,w1
	b.eq	.app
	mov	x0,x20
	mov	w1,1
	bl	ec_bit_logp
	mov	x15,x0
.app:	add	x26,x26,x15,lsl 1
	mov	x14,x21
.a:	cmp	x14,x22
	b.hs	.out
	add	x0,x19,CE_TFRES
	ldr	w1,[x0,x14,lsl 2]
	ldrsb	w1,[x26,x1]
	str	w1,[x0,x14,lsl 2]
	add	x14,x14,1
	b	.a
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; energies fines (RFC 6716 4.3.2.2)
ce_fine:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldr	x21,[x19,CE_START]
	ldr	x22,[x19,CE_END]
	ldr	x23,[x19,CE_C]
.i:	cmp	x21,x22
	b.hs	.out
	add	x0,x19,CE_FINEQ
	ldr	w24,[x0,x21,lsl 2]
	cmp	w24,0
	b.le	.ni
	mov	x9,0
.c:	cmp	x9,x23
	b.hs	.ni
	mov	x0,x20
	mov	w1,w24
	bl	ec_bits
	ucvtf	s0,w0				; (q2 + 1/2) 2^-fq - 1/2
	fmov	s1,0.5
	fadd	s0,s0,s1
	mov	w0,127
	sub	w0,w0,w24
	lsl	w0,w0,23
	fmov	s2,w0
	fmul	s0,s0,s2
	fsub	s0,s0,s1
	mov	x0,CE_NBE
	madd	x0,x9,x0,x21
	add	x1,x19,CE_OLDE
	ldr	s1,[x1,x0,lsl 2]
	fadd	s1,s1,s0
	str	s1,[x1,x0,lsl 2]
	add	x9,x9,1
	b	.c
.ni:	add	x21,x21,1
	b	.i
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; energies finales : bits restants, deux passes de priorite (4.3.2.2)
ce_final:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	ldr	x21,[x19,CE_START]
	ldr	x22,[x19,CE_END]
	ldr	x23,[x19,CE_C]
	mov	x0,x20
	bl	ec_tell
	ldr	x1,[x19,CE_LEN]
	lsl	x1,x1,3
	sub	x24,x1,x0			; bits_left
	mov	x25,0				; priorite
.p:	cmp	x25,2
	b.hs	.out
	mov	x26,x21
.i:	cmp	x26,x22
	b.hs	.np
	cmp	x24,x23
	b.lt	.np
	add	x0,x19,CE_FINEQ
	ldr	w9,[x0,x26,lsl 2]
	cmp	w9,8
	b.ge	.ni
	add	x0,x19,CE_PRIO
	ldr	w0,[x0,x26,lsl 2]
	cmp	x0,x25
	b.ne	.ni
	mov	x10,0
.c:	cmp	x10,x23
	b.hs	.ni
	mov	x0,x20
	mov	w1,1
	bl	ec_bits
	ucvtf	s0,w0				; (q2 - 1/2) 2^-(fq + 1)
	fmov	s1,0.5
	fsub	s0,s0,s1
	mov	w0,126
	sub	w0,w0,w9
	lsl	w0,w0,23
	fmov	s2,w0
	fmul	s0,s0,s2
	mov	x0,CE_NBE
	madd	x0,x10,x0,x26
	add	x1,x19,CE_OLDE
	ldr	s1,[x1,x0,lsl 2]
	fadd	s1,s1,s0
	str	s1,[x1,x0,lsl 2]
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
;  Allocation des bits (RFC 6716 4.3.3 : clt_compute_allocation,
;  interp_bits2pulses)
; ---------------------------------------------------------------------------

; w1 = bits disponibles (1/8) -> CE_PULSES (bits de chaque bande), CE_FINEQ,
; CE_PRIO, CE_CODED, CE_INTENS, CE_DUAL, CE_BALANCE
ce_alloc:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	ldr	x21,[x19,CE_START]
	ldr	x22,[x19,CE_END]
	ldr	x23,[x19,CE_C]
	ldr	x24,[x19,CE_LM]
	sxtw	x25,w1
	cmp	x25,0
	csel	x25,x25,xzr,gt			; total
	mov	x26,x21				; skip_start
	adr	x27,ce_eband
	; reserves : fin des bandes sautees, intensite, double stereo
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
.r0:	; seuils et pente du trim
	ldr	x12,[x19,CE_TRIM]
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
	add	x3,x19,CE_THRESH
	str	w1,[x3,x13,lsl 2]
	mul	w1,w23,w0			; C N (trim - 5 - LM)(end - j - 1) 2^(LM+3) >> 6
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
.t1:	add	x3,x19,CE_TRIMOFF
	str	w1,[x3,x13,lsl 2]
	add	x13,x13,1
	b	.t
.td:	; bissection sur les vecteurs d'allocation
	mov	x14,1				; lo
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
	sub	w0,w1,w0			; N
	mov	x5,CE_NBE
	madd	x5,x14,x5,x8
	ldrb	w1,[x5,x13]
	mul	w1,w1,w0
	mul	w1,w1,w23
	lsl	w1,w1,w24
	asr	w1,w1,2				; bits1
	cmp	x15,11
	b.lo	.b2v
	add	x5,x19,CE_CAP
	ldr	w2,[x5,x13,lsl 2]
	b	.b2d
.b2v:	mov	x5,CE_NBE
	madd	x5,x15,x5,x8
	ldrb	w2,[x5,x13]
	mul	w2,w2,w0
	mul	w2,w2,w23
	lsl	w2,w2,w24
	asr	w2,w2,2				; bits2
.b2d:	add	x5,x19,CE_TRIMOFF
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
.n2:	add	x5,x19,CE_OFFS
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
	add	x5,x19,CE_BITS1
	str	w1,[x5,x13,lsl 2]
	add	x5,x19,CE_BITS2
	str	w2,[x5,x13,lsl 2]
	add	x13,x13,1
	b	.b
.bd:	; interp_bits2pulses : bissection (6 pas, 1/64)
	lsl	x28,x23,3			; alloc_floor
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
	add	x4,x19,CE_BITS1
	ldr	w4,[x4,x3,lsl 2]
	add	x5,x19,CE_BITS2
	ldr	w5,[x5,x3,lsl 2]
	mul	w5,w5,w16
	asr	w5,w5,6
	add	w4,w4,w5			; tmp
	add	x5,x19,CE_THRESH
	ldr	w5,[x5,x3,lsl 2]
	cmp	w4,w5
	b.ge	.ibu
	cbnz	x2,.ibu
	cmp	w4,w28
	b.lt	.ibj
	add	x1,x1,x28
	b	.ibj
.ibu:	mov	x2,1
	add	x5,x19,CE_CAP
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
	; bits de chaque bande (lo)
	mov	x12,0				; psum
	mov	x2,0				; done
	mov	x3,x22
.fb:	cmp	x3,x21
	b.ls	.fbd
	sub	x3,x3,1
	add	x4,x19,CE_BITS1
	ldr	w4,[x4,x3,lsl 2]
	add	x5,x19,CE_BITS2
	ldr	w5,[x5,x3,lsl 2]
	mul	w5,w5,w14
	asr	w5,w5,6
	add	w4,w4,w5
	add	x5,x19,CE_THRESH
	ldr	w5,[x5,x3,lsl 2]
	cmp	w4,w5
	b.ge	.fbu
	cbnz	x2,.fbu
	cmp	w4,w28
	csel	w4,w28,wzr,ge
	b	.fbc
.fbu:	mov	x2,1
.fbc:	add	x5,x19,CE_CAP
	ldr	w5,[x5,x3,lsl 2]
	cmp	w4,w5
	csel	w4,w4,w5,lt
	add	x5,x19,CE_PULSES
	str	w4,[x5,x3,lsl 2]
	add	x12,x12,w4,sxtw
	b	.fb
.fbd:	; bandes sautees, depuis la fin
	mov	x13,x22				; codedBands
.sk:	sub	x14,x13,1			; j
	cmp	x14,x26
	b.gt	.sk1
	add	x25,x25,x9			; (rend le bit reserve)
	b	.skd
.sk1:	sub	x1,x25,x12			; left
	ldrh	w2,[x27,x13,lsl 1]
	ldrh	w3,[x27,x21,lsl 1]
	sub	w2,w2,w3			; eb[coded] - eb[start]
	sdiv	w4,w1,w2			; percoeff
	msub	w1,w4,w2,w1			; left
	ldrh	w5,[x27,x14,lsl 1]
	sub	w6,w5,w3
	subs	w6,w1,w6
	csel	w6,w6,wzr,gt			; rem
	ldrh	w7,[x27,x13,lsl 1]
	sub	w7,w7,w5			; largeur
	add	x5,x19,CE_PULSES
	ldr	w15,[x5,x14,lsl 2]
	madd	w15,w4,w7,w15
	add	w15,w15,w6			; band_bits
	add	x5,x19,CE_THRESH
	ldr	w5,[x5,x14,lsl 2]
	add	w6,w28,8
	cmp	w5,w6
	csel	w5,w5,w6,gt
	cmp	w15,w5
	b.lt	.sk2
	mov	x0,x20
	mov	w1,1
	bl	ec_bit_logp
	cbnz	w0,.skd
	add	x12,x12,8
	sub	w15,w15,8
.sk2:	add	x5,x19,CE_PULSES
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
.skd:	str	x13,[x19,CE_CODED]
	; intensite, double stereo
	mov	x0,0
	cbz	x10,.ni
	mov	x0,x20
	add	x1,x13,1
	sub	x1,x1,x21
	bl	ec_uint
	add	x0,x0,x21
.ni:	str	x0,[x19,CE_INTENS]
	cmp	x0,x21
	b.gt	.nd
	add	x25,x25,x11
	mov	x11,0
.nd:	mov	x0,0
	cbz	x11,.nd2
	mov	x0,x20
	mov	w1,1
	bl	ec_bit_logp
.nd2:	str	x0,[x19,CE_DUAL]
	; le reste, reparti
	sub	x1,x25,x12			; left
	ldrh	w2,[x27,x13,lsl 1]
	ldrh	w3,[x27,x21,lsl 1]
	sub	w2,w2,w3
	sdiv	w4,w1,w2			; percoeff
	msub	w1,w4,w2,w1
	add	x5,x19,CE_PULSES
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
	ldr	x26,[x19,CE_INTENS]
	ldr	x9,[x19,CE_DUAL]
	mov	x14,x21
.fe:	cmp	x14,x13
	b.hs	.fed
	ldrh	w0,[x27,x14,lsl 1]
	add	x1,x14,1
	ldrh	w1,[x27,x1,lsl 1]
	sub	w0,w1,w0
	lsl	w0,w0,w24			; N
	add	x5,x19,CE_PULSES
	ldr	w15,[x5,x14,lsl 2]
	add	w15,w15,w16			; bit
	add	x6,x19,CE_CAP
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
	add	x5,x19,CE_FINEQ
	str	w3,[x5,x14,lsl 2]
	add	x5,x19,CE_PRIO
	str	w8,[x5,x14,lsl 2]
	add	x14,x14,1
	b	.fe
.fed:	sxtw	x16,w16
	str	x16,[x19,CE_BALANCE]
	; bandes sautees : tout en energie fine
.sf:	cmp	x14,x22
	b.hs	.out
	add	x5,x19,CE_PULSES
	ldr	w0,[x5,x14,lsl 2]
	str	wzr,[x5,x14,lsl 2]
	asr	w0,w0,w17
	asr	w0,w0,3
	add	x5,x19,CE_FINEQ
	str	w0,[x5,x14,lsl 2]
	cmp	w0,1
	cset	w0,lt
	add	x5,x19,CE_PRIO
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
	add	x7,x19,CE_TRIMOFF
	ldr	w7,[x7,x3,lsl 2]
	add	w5,w5,w7
	cmp	w5,0
	csel	w5,w5,wzr,gt
.v1:	add	x7,x19,CE_OFFS
	ldr	w7,[x7,x3,lsl 2]
	add	w5,w5,w7
	add	x7,x19,CE_THRESH
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
	add	x7,x19,CE_CAP
	ldr	w7,[x7,x3,lsl 2]
	cmp	w5,w7
	csel	w5,w5,w7,lt
	add	x1,x1,w5,sxtw
	b	.v
.vd:	mov	x0,x1
	ret

; ---------------------------------------------------------------------------
;  Bandes (RFC 6716 4.3.4 : quant_all_bands)
; ---------------------------------------------------------------------------

; spectres normalises de toutes les bandes -> CE_X (canal 0 ; canal 1 en
; CE_X + N), CE_CMASK ; CE_SEED mise a jour
ce_bands:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,128
	ldr	x21,[x19,CE_START]
	ldr	x22,[x19,CE_END]
	ldr	x0,[x19,CE_LM]
	mov	x24,1
	lsl	x24,x24,x0			; M
	adr	x0,ce_eband
	ldrh	w0,[x0,x21,lsl 1]
	mul	x25,x0,x24			; norm_offset
	mov	x26,0				; lowband_offset
	mov	x27,1				; update_lowband
	ldr	x28,[x19,CE_BALANCE]
	ldr	x0,[x19,CE_DUAL]
	str	x0,[sp,48]
	mov	x23,x21				; i
.band:	cmp	x23,x22
	b.hs	.done
	str	x23,[x19,CE_I]
	sub	x0,x22,1
	cmp	x23,x0
	cset	x0,eq
	str	x0,[sp,64]			; derniere bande
	adr	x9,ce_eband
	ldrh	w0,[x9,x23,lsl 1]
	mul	x10,x0,x24			; M eb[i]
	add	x1,x23,1
	ldrh	w1,[x9,x1,lsl 1]
	mul	x1,x1,x24
	sub	x11,x1,x10			; N
	str	x11,[sp,8]
	ldr	x0,[x19,CE_X]
	add	x1,x0,x10,lsl 2
	str	x1,[sp,72]			; X
	mov	x1,0
	ldr	x2,[x19,CE_C]
	cmp	x2,2
	b.ne	.noy
	ldr	x2,[x19,CE_N]
	add	x1,x0,x2,lsl 2
	add	x1,x1,x10,lsl 2
.noy:	str	x1,[sp,80]			; Y
	mov	x0,x20
	bl	ec_tell_frac
	str	x0,[sp]				; tell
	cmp	x23,x21
	b.eq	.nb
	sub	x28,x28,x0			; balance -= tell
.nb:	ldr	x1,[x19,CE_LEN]			; remaining = total - tell - 1
	lsl	x1,x1,6
	ldr	x2,[x19,CE_ANTIRSV]
	sub	x1,x1,x2
	sub	x1,x1,x0
	sub	x1,x1,1
	str	x1,[x19,CE_REM]
	mov	x2,0				; b
	ldr	x3,[x19,CE_CODED]
	cmp	x23,x3
	b.hs	.bset
	sub	x3,x3,x23
	mov	x4,3
	cmp	x3,x4
	csel	x3,x3,x4,lt
	sdiv	x3,x28,x3			; curr_balance
	add	x4,x19,CE_PULSES
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
.bset:	str	x2,[sp,16]
	; position du repliement
	adr	x9,ce_eband
	ldrh	w0,[x9,x21,lsl 1]
	mul	x0,x0,x24			; M eb[start]
	sub	x1,x10,x11
	cmp	x1,x0
	b.ge	.lo1
	add	x1,x21,1
	cmp	x23,x1
	b.ne	.lo2
.lo1:	cbnz	x27,.lo3
	cbnz	x26,.lo2
.lo3:	mov	x26,x23
.lo2:	add	x1,x21,1			; repliement special (hybride)
	cmp	x23,x1
	b.ne	.nhf
	ldrh	w0,[x9,x21,lsl 1]
	ldrh	w1,[x9,x1,lsl 1]
	add	x2,x21,2
	ldrh	w2,[x9,x2,lsl 1]
	sub	w3,w1,w0
	mul	x3,x3,x24			; n1
	sub	w4,w2,w1
	mul	x4,x4,x24			; n2
	subs	x5,x4,x3
	b.le	.nhf
	stp	x3,x4,[sp,96]
	ldr	x0,[x19,CE_NORM]
	add	x0,x0,x3,lsl 2
	lsl	x1,x3,1
	sub	x1,x1,x4
	ldr	x2,[x19,CE_NORM]
	add	x1,x2,x1,lsl 2
	lsl	x2,x5,2
	bl	ak_memmove
	ldr	x0,[sp,48]
	cbz	x0,.nhf
	ldp	x3,x4,[sp,96]
	ldr	x0,[x19,CE_NORM]
	add	x0,x0,4096
	mov	x6,x0
	add	x0,x0,x3,lsl 2
	lsl	x1,x3,1
	sub	x1,x1,x4
	add	x1,x6,x1,lsl 2
	sub	x2,x4,x3
	lsl	x2,x2,2
	bl	ak_memmove
.nhf:	add	x0,x19,CE_TFRES
	ldrsw	x0,[x0,x23,lsl 2]
	str	x0,[x19,CE_TFC]
	; zone de travail (la derniere bande de X), aucune pour la derniere
	mov	x1,0
	ldr	x2,[sp,64]
	cbnz	x2,.scr
	ldr	x1,[x19,CE_X]
	adr	x9,ce_eband
	ldrh	w2,[x9,CE_NBE*2-2]
	mul	x2,x2,x24
	add	x1,x1,x2,lsl 2
.scr:	str	x1,[sp,56]
	; masques des bandes repliees
	ldr	x2,[x19,CE_B]
	mov	x3,1
	lsl	x3,x3,x2
	sub	x3,x3,1
	str	x3,[sp,32]			; x_cm
	str	x3,[sp,40]			; y_cm
	mov	x12,-1				; effective_lowband
	cbz	x26,.nfold
	ldr	x4,[x19,CE_SPREAD]
	cmp	x4,3
	b.ne	.fold
	cmp	x2,1
	b.hi	.fold
	cmp	x0,0
	b.ge	.nfold
.fold:	adr	x9,ce_eband
	ldrh	w4,[x9,x26,lsl 1]
	mul	x4,x4,x24
	sub	x4,x4,x25
	ldr	x11,[sp,8]
	subs	x12,x4,x11
	csel	x12,x12,xzr,gt			; effective_lowband
	add	x5,x12,x25			; effective_lowband + norm_offset
	mov	x6,x26				; fold_start
.fs:	sub	x6,x6,1
	ldrh	w7,[x9,x6,lsl 1]
	mul	x7,x7,x24
	cmp	x7,x5
	b.gt	.fs
	sub	x7,x26,1			; fold_end
	add	x8,x5,x11
.fe:	add	x7,x7,1
	cmp	x7,x23
	b.ge	.fed
	ldrh	w13,[x9,x7,lsl 1]
	mul	x13,x13,x24
	cmp	x13,x8
	b.lt	.fe
.fed:	mov	x13,0
	mov	x14,0
	ldr	x15,[x19,CE_C]
	add	x16,x19,CE_CMASK
.fm:	mul	x17,x6,x15
	ldrb	w0,[x16,x17]
	orr	x13,x13,x0
	add	x17,x17,x15
	sub	x17,x17,1
	ldrb	w0,[x16,x17]
	orr	x14,x14,x0
	add	x6,x6,1
	cmp	x6,x7
	b.lt	.fm
	str	x13,[sp,32]
	str	x14,[sp,40]
.nfold:	str	x12,[sp,24]
	; double stereo arretee a la bande d'intensite
	ldr	x0,[sp,48]
	cbz	x0,.nds
	ldr	x1,[x19,CE_INTENS]
	cmp	x23,x1
	b.ne	.nds
	str	xzr,[sp,48]
	ldr	x0,[x19,CE_NORM]
	mov	x1,0
	sub	x2,x10,x25
	fmov	s2,0.5
.av:	cmp	x1,x2
	b.hs	.nds
	ldr	s0,[x0,x1,lsl 2]
	add	x3,x0,4096
	ldr	s1,[x3,x1,lsl 2]
	fadd	s0,s0,s1
	fmul	s0,s0,s2
	str	s0,[x0,x1,lsl 2]
	add	x1,x1,1
	b	.av
.nds:	; bande basse (repliement) et sortie de la bande pour la suite
	ldr	x0,[x19,CE_NORM]
	mov	x1,0
	ldr	x12,[sp,24]
	cmn	x12,1
	b.eq	.nlb
	add	x1,x0,x12,lsl 2
.nlb:	str	x1,[sp,88]			; lowband
	mov	x1,0
	ldr	x2,[sp,64]
	cbnz	x2,.nlo
	sub	x1,x10,x25
	add	x1,x0,x1,lsl 2
.nlo:	str	x1,[sp,96]			; lowband_out
	ldr	x0,[sp,48]
	cbz	x0,.single
	; double stereo : deux bandes mono
	ldr	x0,[sp,72]
	ldr	x1,[sp,8]
	ldr	x2,[sp,16]
	lsr	x2,x2,1
	ldr	x3,[x19,CE_B]
	ldr	x4,[sp,88]
	ldr	x5,[x19,CE_LM]
	ldr	x6,[sp,96]
	fmov	s0,1.0
	ldr	x7,[sp,56]
	ldr	x8,[sp,32]
	bl	ce_qb
	str	x0,[sp,32]
	ldr	x4,[sp,88]
	cbz	x4,.l2
	add	x4,x4,4096
.l2:	ldr	x6,[sp,96]
	cbz	x6,.l3
	add	x6,x6,4096
.l3:	ldr	x0,[sp,80]
	ldr	x1,[sp,8]
	ldr	x2,[sp,16]
	lsr	x2,x2,1
	ldr	x3,[x19,CE_B]
	ldr	x5,[x19,CE_LM]
	fmov	s0,1.0
	ldr	x7,[sp,56]
	ldr	x8,[sp,40]
	bl	ce_qb
	str	x0,[sp,40]
	b	.cm
.single:
	ldr	x1,[sp,80]
	cbz	x1,.mono
	ldr	x0,[sp,72]
	ldr	x2,[sp,8]
	ldr	x3,[sp,16]
	ldr	x4,[x19,CE_B]
	ldr	x5,[sp,88]
	ldr	x6,[x19,CE_LM]
	ldr	x7,[sp,96]
	ldr	x8,[sp,56]
	ldr	x9,[sp,32]
	ldr	x10,[sp,40]
	orr	x9,x9,x10
	bl	ce_qbs
	b	.cm1
.mono:	ldr	x0,[sp,72]
	ldr	x1,[sp,8]
	ldr	x2,[sp,16]
	ldr	x3,[x19,CE_B]
	ldr	x4,[sp,88]
	ldr	x5,[x19,CE_LM]
	ldr	x6,[sp,96]
	fmov	s0,1.0
	ldr	x7,[sp,56]
	ldr	x8,[sp,32]
	ldr	x9,[sp,40]
	orr	x8,x8,x9
	bl	ce_qb
.cm1:	str	x0,[sp,32]
	str	x0,[sp,40]
.cm:	ldr	x0,[x19,CE_C]
	mul	x1,x23,x0
	add	x2,x19,CE_CMASK
	ldr	x3,[sp,32]
	strb	w3,[x2,x1]
	add	x1,x1,x0
	sub	x1,x1,1
	ldr	x3,[sp,40]
	strb	w3,[x2,x1]
	add	x0,x19,CE_PULSES		; balance += pulses[i] + tell
	ldr	w0,[x0,x23,lsl 2]
	add	x28,x28,w0,sxtw
	ldr	x0,[sp]
	add	x28,x28,x0
	ldr	x0,[sp,16]			; update_lowband = b > N << 3
	ldr	x1,[sp,8]
	cmp	x0,x1,lsl 3
	cset	x27,gt
	add	x23,x23,1
	b	.band
.done:	add	sp,sp,128
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, w1 = N, w2 = b, w3 = B, x4 = bande basse (0), w5 = LM, x6 =
; sortie de la bande (0), s0 = gain, x7 = zone de travail (0), w8 = fill
; -> w0 = masque d'effondrement (quant_band, mono)
ce_qb:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	sub	sp,sp,64
	mov	x21,x0				; X
	mov	w22,w1				; N0
	mov	w23,w2				; b
	mov	w24,w3				; B
	mov	x25,x4				; bande basse
	mov	w26,w5				; LM
	mov	x27,x6				; sortie
	fmov	s8,s0				; gain
	mov	w28,w8				; fill
	str	x7,[sp]				; zone de travail
	cmp	w22,1
	b.ne	.n
	mov	x0,x21
	mov	x1,0
	mov	x2,x27
	bl	ce_qn1
	b	.out
.n:	cmp	w24,1
	cset	w0,eq
	str	x0,[sp,8]			; longBlocks
	udiv	w9,w22,w24			; N_B
	ldr	x10,[x19,CE_TFC]		; tf_change
	cmp	x10,0
	csel	x11,x10,xzr,gt			; recombine
	str	x11,[sp,16]
	str	xzr,[sp,24]			; time_divide
	; copie de la bande basse si elle doit etre transformee
	ldr	x0,[sp]
	cbz	x0,.ncp
	cbz	x25,.ncp
	cbnz	x11,.cp
	cmp	w24,1
	b.hi	.cp
	tbnz	w9,0,.ncp
	cmp	x10,0
	b.ge	.ncp
.cp:	stp	x9,x10,[sp,32]
	mov	x1,x25
	lsl	x2,x22,2
	mov	x25,x0
	bl	ak_memmove
	ldp	x9,x10,[sp,32]
.ncp:	; recombinaison (meilleure resolution en frequence)
	mov	x12,0
.rc:	ldr	x11,[sp,16]
	cmp	x12,x11
	b.hs	.rcd
	stp	x9,x10,[sp,32]
	str	x12,[sp,48]
	cbz	x25,.rc1
	mov	x0,x25
	lsr	w1,w22,w12
	mov	w2,1
	lsl	w2,w2,w12
	bl	ce_haar
.rc1:	adr	x0,ce_bitil
	and	w1,w28,15
	ldrb	w1,[x0,w1,uxtw]
	lsr	w2,w28,4
	ldrb	w2,[x0,w2,uxtw]
	orr	w28,w1,w2,lsl 2
	ldp	x9,x10,[sp,32]
	ldr	x12,[sp,48]
	add	x12,x12,1
	b	.rc
.rcd:	lsr	w24,w24,w11			; B >>= recombine
	lsl	w9,w9,w11			; N_B <<= recombine
	; division en temps
.td:	tbnz	w9,0,.tdd
	cmp	x10,0
	b.ge	.tdd
	stp	x9,x10,[sp,32]
	cbz	x25,.td1
	mov	x0,x25
	mov	w1,w9
	mov	w2,w24
	bl	ce_haar
.td1:	ldp	x9,x10,[sp,32]
	lsl	w0,w28,w24
	orr	w28,w28,w0
	lsl	w24,w24,1
	lsr	w9,w9,1
	ldr	x0,[sp,24]
	add	x0,x0,1
	str	x0,[sp,24]
	add	x10,x10,1
	b	.td
.tdd:	str	x9,[sp,32]			; N_B0
	str	x24,[sp,40]			; B0
	cmp	w24,1
	b.ls	.ndi
	cbz	x25,.ndi
	mov	x0,x25
	ldr	x11,[sp,16]
	lsr	w1,w9,w11
	lsl	w2,w24,w11
	ldr	x3,[sp,8]
	bl	ce_deint
.ndi:	mov	x0,x21
	mov	w1,w22
	mov	w2,w23
	mov	w3,w24
	mov	x4,x25
	mov	w5,w26
	fmov	s0,s8
	mov	w6,w28
	bl	ce_qp
	mov	w23,w0				; cm
	ldr	x24,[sp,40]			; B0
	cmp	w24,1
	b.ls	.nin
	mov	x0,x21
	ldr	x9,[sp,32]
	ldr	x11,[sp,16]
	lsr	w1,w9,w11
	lsl	w2,w24,w11
	ldr	x3,[sp,8]
	bl	ce_inter
.nin:	ldr	x9,[sp,32]			; N_B
	ldr	x12,[sp,24]			; time_divide
.tu:	cbz	x12,.tud
	lsr	w24,w24,1
	lsl	w9,w9,1
	lsr	w0,w23,w24
	orr	w23,w23,w0
	stp	x9,x12,[sp,48]
	mov	x0,x21
	mov	w1,w9
	mov	w2,w24
	bl	ce_haar
	ldp	x9,x12,[sp,48]
	sub	x12,x12,1
	b	.tu
.tud:	mov	x12,0
.ru:	ldr	x11,[sp,16]
	cmp	x12,x11
	b.hs	.rud
	adr	x0,ce_bitdi
	and	w23,w23,15
	ldrb	w23,[x0,w23,uxtw]
	str	x12,[sp,48]
	mov	x0,x21
	lsr	w1,w22,w12
	mov	w2,1
	lsl	w2,w2,w12
	bl	ce_haar
	ldr	x12,[sp,48]
	add	x12,x12,1
	b	.ru
.rud:	lsl	w24,w24,w11			; B <<= recombine
	cbz	x27,.nlo
	ucvtf	s0,w22				; sortie = sqrt(N0) X
	fsqrt	s0,s0
	mov	x0,0
.lo:	ldr	s1,[x21,x0,lsl 2]
	fmul	s1,s1,s0
	str	s1,[x27,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,x22
	b.lo	.lo
.nlo:	mov	w0,1
	lsl	w0,w0,w24
	sub	w0,w0,1
	and	w0,w23,w0
.out:	add	sp,sp,64
	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, x1 = Y, w2 = N, w3 = b, w4 = B, x5 = bande basse, w6 = LM, x7 =
; sortie de la bande, x8 = zone de travail, w9 = fill -> w0 = masque
; (quant_band_stereo)
ce_qbs:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	sub	sp,sp,96
	mov	x21,x0				; X
	mov	x22,x1				; Y
	mov	w23,w2				; N
	mov	w24,w4				; B
	mov	x25,x5				; bande basse
	mov	w26,w6				; LM
	mov	x27,x7				; sortie
	str	x8,[sp]				; zone de travail
	str	x9,[sp,8]			; orig_fill
	cmp	w23,1
	b.ne	.n
	mov	x0,x21
	mov	x1,x22
	mov	x2,x27
	bl	ce_qn1
	b	.out
.n:	mov	w0,w23
	mov	w1,w3
	mov	w2,w24
	mov	w3,w24
	mov	w4,w26
	mov	w5,1
	mov	w6,w9
	bl	ce_theta
	str	x0,[sp,16]			; itheta
	str	x3,[sp,24]			; delta
	str	x4,[sp,32]			; qalloc
	str	x5,[sp,40]			; b
	str	x6,[sp,48]			; fill
	str	x7,[sp,56]			; inv
	movl	w8,0x38000000			; 1/32768
	fmov	s2,w8
	scvtf	s8,w1
	fmul	s8,s8,s2			; mid
	scvtf	s9,w2
	fmul	s9,s9,s2			; side
	cmp	w23,2
	b.ne	.gen
	; N = 2 : un seul bit pour le cote
	mov	w28,0				; sbits
	cbz	w0,.s0
	cmp	w0,16384
	b.eq	.s0
	mov	w28,8
.s0:	ldr	x1,[x19,CE_REM]
	add	w2,w4,w28
	sub	x1,x1,w2,sxtw
	str	x1,[x19,CE_REM]
	cmp	w0,8192
	cset	w0,gt				; c
	mov	x1,x21				; x2, y2
	mov	x2,x22
	cbz	w0,.sw
	mov	x1,x22
	mov	x2,x21
.sw:	stp	x1,x2,[sp,64]
	mov	w0,0				; signe
	cbz	w28,.nsg
	mov	x0,x20
	mov	w1,1
	bl	ec_bits
.nsg:	str	x0,[sp,80]
	ldr	x0,[sp,64]
	mov	w1,2
	ldr	x2,[sp,40]
	sub	w2,w2,w28			; mbits
	mov	w3,w24
	mov	x4,x25
	mov	w5,w26
	mov	x6,x27
	fmov	s0,1.0
	ldr	x7,[sp]
	ldr	x8,[sp,8]
	bl	ce_qb
	mov	w28,w0				; cm
	ldp	x1,x2,[sp,64]
	ldr	x3,[sp,80]			; signe : 1 - 2 bit
	fmov	s2,1.0
	fmov	s3,-1.0
	cmp	w3,0
	fcsel	s2,s2,s3,eq			; sign
	ldp	s0,s1,[x1]			; y2 = (-sign x2[1], sign x2[0])
	fmul	s4,s1,s2
	fneg	s4,s4
	fmul	s5,s0,s2
	stp	s4,s5,[x2]
	ldp	s0,s1,[x21]
	fmul	s0,s0,s8
	fmul	s1,s1,s8
	ldp	s2,s3,[x22]
	fmul	s2,s2,s9
	fmul	s3,s3,s9
	fsub	s4,s0,s2
	fadd	s5,s0,s2
	fsub	s6,s1,s3
	fadd	s7,s1,s3
	stp	s4,s6,[x21]
	stp	s5,s7,[x22]
	b	.inv
.gen:	; mbits = max(0, min(b, (b - delta) / 2))
	ldr	x1,[sp,40]
	sub	w2,w1,w3
	mov	w5,2
	sdiv	w2,w2,w5
	cmp	w2,w1
	csel	w2,w2,w1,lt
	cmp	w2,0
	csel	w2,w2,wzr,gt
	sub	w3,w1,w2			; sbits
	stp	x2,x3,[sp,64]
	ldr	x1,[x19,CE_REM]
	sub	x1,x1,w4,sxtw
	str	x1,[x19,CE_REM]
	str	x1,[sp,80]			; rebalance
	cmp	w2,w3
	b.lt	.sfirst
	mov	x0,x21
	mov	w1,w23
	mov	w3,w24
	mov	x4,x25
	mov	w5,w26
	mov	x6,x27
	fmov	s0,1.0
	ldr	x7,[sp]
	ldr	x8,[sp,48]
	bl	ce_qb
	mov	w28,w0
	ldr	x1,[sp,80]			; rebalance = mbits - (rebalance - remaining)
	ldr	x2,[x19,CE_REM]
	sub	x1,x1,x2
	ldp	x2,x3,[sp,64]
	sxtw	x2,w2
	sub	x1,x2,x1
	cmp	x1,24
	b.le	.sb
	ldr	x4,[sp,16]
	cbz	x4,.sb
	sub	x1,x1,24
	add	w3,w3,w1
.sb:	mov	x0,x22
	mov	w1,w23
	mov	w2,w3
	mov	w3,w24
	mov	x4,0
	mov	w5,w26
	mov	x6,0
	fmov	s0,s9
	mov	x7,0
	ldr	x8,[sp,48]
	lsr	w8,w8,w24
	bl	ce_qb
	orr	w28,w28,w0
	b	.merge
.sfirst:
	mov	x0,x22
	mov	w1,w23
	mov	w2,w3
	mov	w3,w24
	mov	x4,0
	mov	w5,w26
	mov	x6,0
	fmov	s0,s9
	mov	x7,0
	ldr	x8,[sp,48]
	lsr	w8,w8,w24
	bl	ce_qb
	mov	w28,w0
	ldr	x1,[sp,80]
	ldr	x2,[x19,CE_REM]
	sub	x1,x1,x2
	ldp	x2,x3,[sp,64]
	sxtw	x3,w3
	sub	x1,x3,x1
	cmp	x1,24
	b.le	.mb
	ldr	x4,[sp,16]
	cmp	x4,16384
	b.eq	.mb
	sub	x1,x1,24
	add	w2,w2,w1
.mb:	mov	x0,x21
	mov	w1,w23
	mov	w3,w24
	mov	x4,x25
	mov	w5,w26
	mov	x6,x27
	fmov	s0,1.0
	ldr	x7,[sp]
	ldr	x8,[sp,48]
	bl	ce_qb
	orr	w28,w28,w0
.merge:	mov	x0,x21
	mov	x1,x22
	fmov	s0,s8
	mov	w2,w23
	bl	ce_merge
.inv:	ldr	x0,[sp,56]
	cbz	x0,.ret
	mov	x0,0
.ng:	ldr	s0,[x22,x0,lsl 2]
	fneg	s0,s0
	str	s0,[x22,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,x23
	b.lo	.ng
.ret:	mov	w0,w28
.out:	add	sp,sp,96
	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, w1 = N, w2 = b, w3 = B, x4 = bande basse (0), w5 = LM (-1..3),
; s0 = gain, w6 = fill -> w0 = masque (quant_partition : division
; recursive en deux moities, sinon PVQ ou repliement)
ce_qp:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	sub	sp,sp,64
	mov	x21,x0
	mov	w22,w1				; N
	mov	w23,w2				; b
	mov	w24,w3				; B
	mov	x25,x4
	mov	w26,w5				; LM
	fmov	s8,s0
	mov	w27,w6				; fill
	ldr	x0,[x19,CE_I]
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
	; division
	mov	w28,w24				; B0
	lsr	w22,w22,1
	sub	w26,w26,1
	cmp	w24,1
	b.ne	.sb
	and	w0,w27,1
	orr	w27,w0,w27,lsl 1
.sb:	add	w24,w24,1
	lsr	w24,w24,1
	mov	w0,w22
	mov	w1,w23
	mov	w2,w24
	mov	w3,w28
	mov	w4,w26
	mov	w5,0
	mov	w6,w27
	bl	ce_theta
	mov	w23,w5				; b
	mov	w27,w6				; fill
	str	x0,[sp]				; itheta
	movl	w8,0x38000000			; gain x mid, gain x side
	fmov	s2,w8
	scvtf	s0,w1
	fmul	s0,s0,s2
	fmul	s0,s8,s0
	scvtf	s1,w2
	fmul	s1,s1,s2
	fmul	s1,s8,s1
	stp	s0,s1,[sp,8]
	; pentes de masquage (blocs courts)
	cmp	w28,1
	b.ls	.nd
	tst	w0,0x3FFF
	b.eq	.nd
	cmp	w0,8192
	b.le	.dl
	mov	w5,4
	sub	w5,w5,w26
	asr	w6,w3,w5
	sub	w3,w3,w6
	b	.nd
.dl:	lsl	w5,w22,3
	mov	w6,5
	sub	w6,w6,w26
	asr	w5,w5,w6
	add	w3,w3,w5
	cmp	w3,0
	csel	w3,w3,wzr,lt
.nd:	sub	w2,w23,w3			; mbits = max(0, min(b, (b - delta) / 2))
	mov	w5,2
	sdiv	w2,w2,w5
	cmp	w2,w23
	csel	w2,w2,w23,lt
	cmp	w2,0
	csel	w2,w2,wzr,gt
	sub	w3,w23,w2			; sbits
	stp	w2,w3,[sp,16]
	ldr	x1,[x19,CE_REM]
	sub	x1,x1,w4,sxtw
	str	x1,[x19,CE_REM]
	str	x1,[sp,24]			; rebalance
	mov	x0,0				; bande basse de la seconde moitie
	cbz	x25,.nl2
	add	x0,x25,x22,lsl 2
.nl2:	str	x0,[sp,32]
	cmp	w2,w3
	b.lt	.sfirst
	mov	x0,x21
	mov	w1,w22
	mov	w3,w24
	mov	x4,x25
	mov	w5,w26
	ldr	s0,[sp,8]
	mov	w6,w27
	bl	ce_qp
	str	x0,[sp,40]
	ldr	x1,[sp,24]
	ldr	x2,[x19,CE_REM]
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
	mov	w3,w24
	ldr	x4,[sp,32]
	mov	w5,w26
	ldr	s0,[sp,12]
	lsr	w6,w27,w24
	bl	ce_qp
	lsr	w1,w28,1
	lsl	w0,w0,w1
	ldr	x1,[sp,40]
	orr	w0,w0,w1
	b	.out
.sfirst:
	add	x0,x21,x22,lsl 2
	mov	w1,w22
	mov	w2,w3
	mov	w3,w24
	ldr	x4,[sp,32]
	mov	w5,w26
	ldr	s0,[sp,12]
	lsr	w6,w27,w24
	bl	ce_qp
	lsr	w1,w28,1
	lsl	w0,w0,w1
	str	x0,[sp,40]
	ldr	x1,[sp,24]
	ldr	x2,[x19,CE_REM]
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
	mov	w3,w24
	mov	x4,x25
	mov	w5,w26
	ldr	s0,[sp,8]
	mov	w6,w27
	bl	ce_qp
	ldr	x1,[sp,40]
	orr	w0,w0,w1
	b	.out
.leaf:	; impulsions (bits2pulses, sans depasser le budget)
	ldr	x0,[x19,CE_I]
	mov	w1,w26
	mov	w2,w23
	bl	ce_b2p
	mov	w28,w0				; q
	ldr	x0,[x19,CE_I]
	mov	w1,w26
	mov	w2,w28
	bl	ce_p2b
	ldr	x1,[x19,CE_REM]
	sub	x1,x1,x0
	str	x1,[x19,CE_REM]
.bust:	ldr	x1,[x19,CE_REM]
	cmp	x1,0
	b.ge	.bok
	cbz	w28,.bok
	add	x1,x1,x0
	str	x1,[x19,CE_REM]
	sub	w28,w28,1
	ldr	x0,[x19,CE_I]
	mov	w1,w26
	mov	w2,w28
	bl	ce_p2b
	ldr	x1,[x19,CE_REM]
	sub	x1,x1,x0
	str	x1,[x19,CE_REM]
	b	.bust
.bok:	cbz	w28,.nopulse
	mov	w0,w28
	bl	ce_getp
	mov	w2,w0
	mov	x0,x21
	mov	w1,w22
	ldr	x3,[x19,CE_SPREAD]
	mov	w4,w24
	fmov	s0,s8
	bl	ce_algq
	b	.out
.nopulse:
	mov	w0,1
	lsl	w0,w0,w24
	sub	w9,w0,1				; masque
	and	w27,w27,w9
	cbnz	w27,.fill
	mov	x0,x21
	lsl	x1,x22,2
	bl	ak_memzero
	mov	w0,0
	b	.out
.fill:	ldr	w10,[x19,CE_SEED]
	movl	w11,1664525
	movl	w12,1013904223
	mov	x13,0
	cbnz	x25,.fold
	; bruit
.nz:	madd	w10,w10,w11,w12
	asr	w0,w10,20
	scvtf	s0,w0
	str	s0,[x21,x13,lsl 2]
	add	x13,x13,1
	cmp	x13,x22
	b.lo	.nz
	mov	w27,w9
	b	.ren
.fold:	movl	w0,0x3B800000			; 1/256
	fmov	s1,w0
	fneg	s2,s1
.fl:	madd	w10,w10,w11,w12
	tst	w10,0x8000
	fcsel	s3,s1,s2,ne
	ldr	s0,[x25,x13,lsl 2]
	fadd	s0,s0,s3
	str	s0,[x21,x13,lsl 2]
	add	x13,x13,1
	cmp	x13,x22
	b.lo	.fl
.ren:	str	w10,[x19,CE_SEED]
	mov	x0,x21
	mov	w1,w22
	fmov	s0,s8
	bl	ce_renorm
	mov	w0,w27
.out:	add	sp,sp,64
	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; w0 = N, w1 = b, w2 = B, w3 = B0, w4 = LM, w5 = stereo, w6 = fill -> w0 =
; itheta, w1 = imid, w2 = iside, w3 = delta, w4 = qalloc, w5 = b, w6 = fill,
; w7 = inv (compute_theta)
ce_theta:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	w21,w0				; N
	mov	w22,w1				; b
	mov	w23,w2				; B
	mov	w24,w3				; B0
	mov	w26,w5				; stereo
	mov	w27,w6				; fill
	ldr	x9,[x19,CE_I]
	adr	x0,ce_logn
	ldrh	w10,[x0,x9,lsl 1]
	add	w10,w10,w4,lsl 3		; pulse_cap
	mov	w11,4				; offset
	cbz	w26,.o1
	cmp	w21,2
	b.ne	.o1
	mov	w11,16
.o1:	neg	w11,w11
	add	w11,w11,w10,asr 1
	; qn (compute_qn)
	lsl	w0,w21,1
	sub	w0,w0,1				; N2
	cbz	w26,.q1
	cmp	w21,2
	b.ne	.q1
	sub	w0,w0,1
.q1:	madd	w1,w0,w11,w22
	sdiv	w1,w1,w0			; qb
	sub	w2,w22,w10
	sub	w2,w2,32
	cmp	w1,w2
	csel	w1,w1,w2,lt
	mov	w2,64
	cmp	w1,w2
	csel	w1,w1,w2,lt
	mov	w25,1				; qn
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
	lsl	w25,w3,1
.qd:	cbz	w26,.qi
	ldr	x0,[x19,CE_INTENS]
	cmp	x9,x0
	b.lo	.qi
	mov	w25,1
.qi:	mov	x0,x20
	bl	ec_tell_frac
	mov	x28,x0				; tell
	mov	w24,w24				; (B0)
	mov	w12,0				; itheta
	mov	w13,0				; inv
	cmp	w25,1
	b.eq	.q1n
	cbz	w26,.nst
	cmp	w21,2
	b.le	.nst
	; stereo : densite en marche (p0 = 3 jusqu'a qn/2)
	lsr	w14,w25,1			; x0
	add	w15,w14,1
	add	w16,w15,w15,lsl 1		; (x0 + 1) p0
	add	w17,w16,w14			; ft
	mov	x0,x20
	mov	w1,w17
	bl	ec_decode
	cmp	w0,w16
	b.hs	.sh
	mov	w1,3
	udiv	w12,w0,w1
	b	.sx
.sh:	sub	w12,w0,w16
	add	w12,w12,w15
.sx:	cmp	w12,w14
	b.hi	.sx2
	add	w1,w12,w12,lsl 1		; p0 x
	add	w2,w1,3
	b	.sx3
.sx2:	sub	w1,w12,1
	sub	w1,w1,w14
	add	w1,w1,w16
	sub	w2,w12,w14
	add	w2,w2,w16
.sx3:	mov	x0,x20
	mov	w3,w17
	bl	ec_update
	b	.qsc
.nst:	cmp	w24,1
	b.hi	.uni
	cbz	w26,.tri
.uni:	mov	x0,x20
	add	w1,w25,1
	bl	ec_uint
	mov	w12,w0
	b	.qsc
.tri:	; densite triangulaire
	lsr	w14,w25,1
	add	w15,w14,1
	mul	w17,w15,w15			; ft
	mov	x0,x20
	mov	w1,w17
	bl	ec_decode
	mov	w16,w0				; fm
	mul	w1,w14,w15
	lsr	w1,w1,1
	cmp	w16,w1
	b.hs	.th
	lsl	w0,w16,3
	add	w0,w0,1
	bl	ce_isqrt
	sub	w0,w0,1
	lsr	w12,w0,1
	add	w2,w12,1			; fs
	mul	w1,w12,w2
	lsr	w1,w1,1				; fl
	b	.tu
.th:	sub	w0,w17,w16
	sub	w0,w0,1
	lsl	w0,w0,3
	add	w0,w0,1
	bl	ce_isqrt
	add	w1,w25,1
	lsl	w1,w1,1
	sub	w0,w1,w0
	lsr	w12,w0,1
	add	w2,w25,1
	sub	w2,w2,w12			; fs
	add	w3,w2,1
	mul	w3,w2,w3
	lsr	w3,w3,1
	sub	w1,w17,w3			; fl
.tu:	add	w2,w1,w2
	mov	x0,x20
	mov	w3,w17
	bl	ec_update
.qsc:	lsl	w12,w12,14			; itheta = itheta 16384 / qn
	udiv	w12,w12,w25
	b	.qa
.q1n:	cbz	w26,.qa				; qn = 1, stereo : inversion
	cmp	w22,16
	b.le	.qa
	ldr	x0,[x19,CE_REM]
	cmp	x0,16
	b.le	.qa
	mov	x0,x20
	mov	w1,2
	bl	ec_bit_logp
	mov	w13,w0
	ldr	x0,[x19,CE_CC]			; sortie mono : pas d'inversion de phase
	cmp	x0,1				; (RFC 8251 4.7, comme libopus)
	csel	w13,wzr,w13,eq
.qa:	mov	x0,x20
	bl	ec_tell_frac
	sub	w4,w0,w28			; qalloc
	sub	w22,w22,w4			; b
	cbnz	w12,.t1
	mov	w1,32767
	mov	w2,0
	mov	w0,1
	lsl	w0,w0,w23
	sub	w0,w0,1
	and	w27,w27,w0
	mov	w3,-16384
	b	.tr
.t1:	cmp	w12,16384
	b.ne	.t2
	mov	w1,0
	mov	w2,32767
	mov	w0,1
	lsl	w0,w0,w23
	sub	w0,w0,1
	lsl	w0,w0,w23
	and	w27,w27,w0
	mov	w3,16384
	b	.tr
.t2:	str	x4,[sp,-16]!
	mov	w0,w12
	bl	ce_bexcos
	mov	w24,w0				; imid
	mov	w0,16384
	sub	w0,w0,w12
	bl	ce_bexcos
	mov	w25,w0				; iside
	mov	w1,w24
	bl	ce_log2tan
	sxth	w0,w0
	sub	w1,w21,1
	lsl	w1,w1,7
	sxth	w1,w1
	mul	w0,w0,w1
	add	w0,w0,16384
	asr	w3,w0,15			; delta
	ldr	x4,[sp],16
	mov	w1,w24
	mov	w2,w25
.tr:	mov	w0,w12
	mov	w5,w22
	mov	w6,w27
	mov	w7,w13
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, x1 = Y (0), x2 = sortie de la bande (0) -> w0 = 1 (bande d'un
; coefficient : un bit de signe par canal)
ce_qn1:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
	mov	x24,x21
.c:	mov	w0,0
	ldr	x1,[x19,CE_REM]
	cmp	x1,8
	b.lt	.s
	sub	x1,x1,8
	str	x1,[x19,CE_REM]
	mov	x0,x20
	mov	w1,1
	bl	ec_bits
.s:	fmov	s0,1.0
	fmov	s1,-1.0
	cmp	w0,0
	fcsel	s0,s0,s1,eq
	str	s0,[x24]
	cmp	x24,x22
	b.eq	.d
	mov	x24,x22
	cbnz	x22,.c
.d:	cbz	x23,.r
	ldr	s0,[x21]
	str	s0,[x23]
.r:	mov	w0,1
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, w1 = N, w2 = K, w3 = etalement, w4 = B, s0 = gain -> w0 = masque
; (alg_unquant : indice CWRS, impulsions, normalisation, rotation)
ce_algq:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	mov	x21,x0
	mov	w22,w1				; N
	mov	w23,w2				; K
	mov	w24,w3
	mov	w25,w4
	fmov	s8,s0
	; table U(n, k), n <= N, k <= K + 1 : U(0, 0) = 1, U(0, k) = 0,
	; U(n, 0) = 0, U(n, k) = U(n-1, k) + U(n, k-1) + U(n-1, k-1)
	ldr	x26,[x19,CE_UT]
	add	w27,w23,2			; pas des lignes
	mov	w0,1
	str	w0,[x26]
	mov	x1,1
.u0:	cmp	x1,x27
	b.hs	.u0d
	str	wzr,[x26,x1,lsl 2]
	add	x1,x1,1
	b	.u0
.u0d:	mov	x2,1				; n
.un:	cmp	x2,x22
	b.hi	.und
	mul	x3,x2,x27
	add	x3,x26,x3,lsl 2			; ligne n
	sub	x4,x3,x27,lsl 2			; ligne n - 1
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
.und:	; indice : uniforme dans V(N, K) = U(N, K) + U(N, K + 1)
	mul	x3,x22,x27
	add	x3,x26,x3,lsl 2
	ldr	w1,[x3,x23,lsl 2]
	add	x4,x23,1
	ldr	w2,[x3,x4,lsl 2]
	add	w1,w1,w2
	mov	x0,x20
	bl	ec_uint
	; CWRS : impulsions de chaque dimension (n > 2, puis 2 et 1)
	ldr	x28,[x19,CE_IY]
	mov	w9,w0				; i
	mov	w10,w23				; k
	mov	w11,w22				; n
	mov	x12,0				; sortie
	mov	w13,0				; Ryy
.cw:	cmp	w11,2
	b.ls	.cw2
	mul	x3,x11,x27
	add	x3,x26,x3,lsl 2
	ldr	w4,[x3,x10,lsl 2]		; p = U(n, k)
	add	x5,x10,1
	ldr	w5,[x3,x5,lsl 2]		; q = U(n, k + 1)
	cmp	w9,w4
	b.lo	.cwn
	cmp	w9,w5
	b.hs	.cwn
	sub	w9,w9,w4			; zero dans cette dimension
	mov	w6,0
	b	.cws
.cwn:	cmp	w9,w5
	cset	w7,hs				; negatif
	b.lo	.cwp
	sub	w9,w9,w5
.cwp:	mov	w6,w10				; k0
.cwk:	sub	w10,w10,1
	ldr	w4,[x3,x10,lsl 2]
	cmp	w4,w9
	b.hi	.cwk
	sub	w9,w9,w4
	sub	w6,w6,w10
	cbz	w7,.cws
	neg	w6,w6
.cws:	str	w6,[x28,x12,lsl 2]
	madd	w13,w6,w6,w13
	add	x12,x12,1
	sub	w11,w11,1
	b	.cw
.cw2:	add	w4,w10,w10			; n = 2 : p = 2k + 1
	add	w4,w4,1
	cmp	w9,w4
	cset	w7,hs
	b.lo	.c21
	sub	w9,w9,w4
.c21:	mov	w6,w10				; k0
	add	w10,w9,1
	lsr	w10,w10,1			; k
	cbz	w10,.c22
	lsl	w4,w10,1
	sub	w4,w4,1
	sub	w9,w9,w4
.c22:	sub	w6,w6,w10
	cbz	w7,.c23
	neg	w6,w6
.c23:	str	w6,[x28,x12,lsl 2]
	madd	w13,w6,w6,w13
	add	x12,x12,1
	mov	w6,w10				; n = 1 : signe de i
	cbz	w9,.c24
	neg	w6,w6
.c24:	str	w6,[x28,x12,lsl 2]
	madd	w13,w6,w6,w13
	; X = gain / sqrt(Ryy) iy
	ucvtf	s0,w13
	fsqrt	s0,s0
	fmov	s1,1.0
	fdiv	s0,s1,s0
	fmul	s0,s0,s8
	mov	x0,0
.nm:	ldr	w1,[x28,x0,lsl 2]
	scvtf	s1,w1
	fmul	s1,s1,s0
	str	s1,[x21,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,x22
	b.lo	.nm
	mov	x0,x21
	mov	w1,w22
	mov	w2,w25
	mov	w3,w23
	mov	w4,w24
	bl	ce_exprot
	; masque : blocs non nuls
	mov	w0,1
	cmp	w25,1
	b.ls	.out
	udiv	w1,w22,w25			; N0
	mov	w0,0
	mov	x2,0				; bloc
	mov	x3,0				; indice
.mb:	cmp	x2,x25
	b.hs	.out
	mov	w4,0
	mov	x5,0
.mj:	ldr	w6,[x28,x3,lsl 2]
	orr	w4,w4,w6
	add	x3,x3,1
	add	x5,x5,1
	cmp	x5,x1
	b.lo	.mj
	cbz	w4,.mn
	mov	w6,1
	lsl	w6,w6,w2
	orr	w0,w0,w6
.mn:	add	x2,x2,1
	b	.mb
.out:	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X, w1 = longueur, w2 = blocs (stride), w3 = K, w4 = etalement :
; rotation inverse d'etalement (exp_rotation, dir = -1)
ce_exprot:
	cbz	w4,.ret
	cmp	w1,w3,lsl 1
	b.ls	.ret
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	mov	x21,x0
	mov	w22,w1				; longueur
	mov	w23,w2				; stride
	adr	x5,ce_spreadf
	sub	w4,w4,1
	ldrb	w5,[x5,w4,uxtw]			; facteur
	mul	w5,w5,w3
	add	w5,w5,w22
	ucvtf	s0,w22
	ucvtf	s1,w5
	fdiv	s0,s0,s1			; gain
	fmul	s0,s0,s0
	fmov	s1,0.5
	fmul	s8,s0,s1			; theta
	fcvt	d0,s8
	adr	x0,ce_halfpi
	ldr	d9,[x0]
	fmul	d0,d0,d9
	bl	ak_sincos
	fcvt	s10,d1				; c = cos(pi/2 theta)
	fmov	s0,1.0
	fsub	s0,s0,s8
	fcvt	d0,s0
	fmul	d0,d0,d9
	bl	ak_sincos
	fcvt	s11,d1				; s = cos(pi/2 (1 - theta))
	; stride2 : environ sqrt(longueur / stride)
	mov	w24,0
	lsl	w0,w23,3
	cmp	w22,w0
	b.lo	.s2d
	mov	w24,1
.s2:	madd	w0,w24,w24,w24
	mul	w0,w0,w23
	add	w0,w0,w23,lsr 2
	cmp	w0,w22
	b.hs	.s2d
	add	w24,w24,1
	b	.s2
.s2d:	udiv	w22,w22,w23			; longueur par bloc
	mov	x9,0
.b:	cmp	x9,x23
	b.hs	.out
	mul	x0,x9,x22
	add	x0,x21,x0,lsl 2
	stp	x0,x9,[sp,-16]!
	cbz	w24,.b1
	mov	w1,w22
	mov	w2,w24
	fmov	s0,s11
	fmov	s1,s10
	bl	ce_rot1
.b1:	ldr	x0,[sp]
	mov	w1,w22
	mov	w2,1
	fmov	s0,s10
	fmov	s1,s11
	bl	ce_rot1
	ldp	x0,x9,[sp],16
	add	x9,x9,1
	b	.b
.out:	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x29,x30,[sp],16
.ret:	ret

; x0 = X, w1 = longueur, w2 = pas, s0 = c, s1 = s : deux passes de rotations
; de Givens (exp_rotation1). Ne touche que x0-x5, v0-v5.
ce_rot1:
	fneg	s5,s1				; -s
	sub	w3,w1,w2			; i < len - stride
	mov	x4,x0
.f:	cmp	w3,0
	b.le	.fd
	ldr	s2,[x4]				; x1
	ldr	s3,[x4,x2,lsl 2]		; x2
	fmul	s4,s0,s3
	fmadd	s4,s1,s2,s4
	str	s4,[x4,x2,lsl 2]
	fmul	s4,s0,s2
	fmadd	s4,s5,s3,s4
	str	s4,[x4],4
	sub	w3,w3,1
	b	.f
.fd:	sub	w3,w1,w2,lsl 1			; i = len - 2 stride - 1 .. 0
	sub	w3,w3,1
	cmp	w3,0
	b.lt	.ret
	add	x4,x0,x3,lsl 2
.b:	ldr	s2,[x4]
	ldr	s3,[x4,x2,lsl 2]
	fmul	s4,s0,s3
	fmadd	s4,s1,s2,s4
	str	s4,[x4,x2,lsl 2]
	fmul	s4,s0,s2
	fmadd	s4,s5,s3,s4
	str	s4,[x4],-4
	subs	w3,w3,1
	b.ge	.b
.ret:	ret

; x0 = X, w1 = N, s0 = gain : X = gain X / |X| (renormalise_vector). Ne
; touche que x0-x3, v0-v3.
ce_renorm:
	movi	d1,0
	mov	x2,0
.e:	ldr	s2,[x0,x2,lsl 2]
	fmadd	s1,s2,s2,s1
	add	x2,x2,1
	cmp	w2,w1
	b.lo	.e
	movl	w3,0x26901D7D			; 1e-15
	fmov	s2,w3
	fadd	s1,s1,s2
	fsqrt	s1,s1
	fmov	s2,1.0
	fdiv	s1,s2,s1
	fmul	s1,s1,s0
	mov	x2,0
.m:	ldr	s2,[x0,x2,lsl 2]
	fmul	s2,s2,s1
	str	s2,[x0,x2,lsl 2]
	add	x2,x2,1
	cmp	w2,w1
	b.lo	.m
	ret

; x0 = X, w1 = N0, w2 = stride : transformee de Haar (haar1). Ne touche que
; x0-x7, v0-v3.
ce_haar:
	lsr	w1,w1,1
	movl	w3,0x3F3504F3			; 0,70710678
	fmov	s3,w3
	mov	x3,0				; i
.i:	cmp	w3,w2
	b.hs	.ret
	mov	x4,0				; j
.j:	cmp	w4,w1
	b.hs	.ni
	lsl	x5,x4,1
	mul	x5,x5,x2
	add	x5,x5,x3			; stride 2j + i
	add	x6,x5,x2			; stride (2j + 1) + i
	ldr	s0,[x0,x5,lsl 2]
	ldr	s1,[x0,x6,lsl 2]
	fmul	s0,s0,s3
	fmul	s1,s1,s3
	fadd	s2,s0,s1
	str	s2,[x0,x5,lsl 2]
	fsub	s2,s0,s1
	str	s2,[x0,x6,lsl 2]
	add	x4,x4,1
	b	.j
.ni:	add	x3,x3,1
	b	.i
.ret:	ret

; x0 = X, w1 = N0, w2 = stride, w3 = hadamard : ordre du temps
; (deinterleave_hadamard). Ne touche que x0-x9, v0.
ce_deint:
	ldr	x4,[x19,CE_TMP]
	adr	x5,ce_ordery
	sub	w6,w2,2
	add	x5,x5,x6
	mov	x6,0				; i
.i:	cmp	w6,w2
	b.hs	.cp
	mov	x7,x6
	cbz	w3,.h
	ldrb	w7,[x5,x6]
.h:	mul	x7,x7,x1			; destination : ordre[i] N0
	mov	x8,0
.j:	cmp	w8,w1
	b.hs	.ni
	madd	x9,x8,x2,x6			; source : j stride + i
	ldr	s0,[x0,x9,lsl 2]
	add	x9,x7,x8
	str	s0,[x4,x9,lsl 2]
	add	x8,x8,1
	b	.j
.ni:	add	x6,x6,1
	b	.i
.cp:	mul	x6,x1,x2
	mov	x7,0
.c:	cmp	x7,x6
	b.hs	.ret
	ldr	s0,[x4,x7,lsl 2]
	str	s0,[x0,x7,lsl 2]
	add	x7,x7,1
	b	.c
.ret:	ret

; x0 = X, w1 = N0, w2 = stride, w3 = hadamard : ordre des frequences
; (interleave_hadamard). Ne touche que x0-x9, v0.
ce_inter:
	ldr	x4,[x19,CE_TMP]
	adr	x5,ce_ordery
	sub	w6,w2,2
	add	x5,x5,x6
	mov	x6,0
.i:	cmp	w6,w2
	b.hs	.cp
	mov	x7,x6
	cbz	w3,.h
	ldrb	w7,[x5,x6]
.h:	mul	x7,x7,x1			; source : ordre[i] N0
	mov	x8,0
.j:	cmp	w8,w1
	b.hs	.ni
	add	x9,x7,x8
	ldr	s0,[x0,x9,lsl 2]
	madd	x9,x8,x2,x6			; destination : j stride + i
	str	s0,[x4,x9,lsl 2]
	add	x8,x8,1
	b	.j
.ni:	add	x6,x6,1
	b	.i
.cp:	mul	x6,x1,x2
	mov	x7,0
.c:	cmp	x7,x6
	b.hs	.ret
	ldr	s0,[x4,x7,lsl 2]
	str	s0,[x0,x7,lsl 2]
	add	x7,x7,1
	b	.c
.ret:	ret

; x0 = X (milieu), x1 = Y (cote), s0 = mid, w2 = N : gauche, droite
; (stereo_merge). Ne touche que x0-x4, v0-v7.
ce_merge:
	movi	d1,0				; <Y, X>
	movi	d2,0				; <Y, Y>
	mov	x3,0
.d:	ldr	s3,[x0,x3,lsl 2]
	ldr	s4,[x1,x3,lsl 2]
	fmadd	s1,s4,s3,s1
	fmadd	s2,s4,s4,s2
	add	x3,x3,1
	cmp	w3,w2
	b.lo	.d
	fmul	s1,s1,s0			; xp = mid <Y, X>
	fmul	s3,s0,s0
	fadd	s3,s3,s2			; mid^2 + side
	fadd	s4,s1,s1
	fsub	s5,s3,s4			; El
	fadd	s6,s3,s4			; Er
	movl	w4,0x3A1D4952			; 6e-4
	fmov	s7,w4
	fcmp	s6,s7
	b.lt	.copy
	fcmp	s5,s7
	b.lt	.copy
	fsqrt	s5,s5
	fsqrt	s6,s6
	fmov	s7,1.0
	fdiv	s5,s7,s5			; lgain
	fdiv	s6,s7,s6			; rgain
	mov	x3,0
.m:	ldr	s3,[x0,x3,lsl 2]
	fmul	s3,s3,s0			; l = mid X
	ldr	s4,[x1,x3,lsl 2]		; r = Y
	fsub	s7,s3,s4
	fmul	s7,s7,s5
	str	s7,[x0,x3,lsl 2]
	fadd	s7,s3,s4
	fmul	s7,s7,s6
	str	s7,[x1,x3,lsl 2]
	add	x3,x3,1
	cmp	w3,w2
	b.lo	.m
	ret
.copy:	mov	x3,0
.c:	ldr	s3,[x0,x3,lsl 2]
	str	s3,[x1,x3,lsl 2]
	add	x3,x3,1
	cmp	w3,w2
	b.lo	.c
	ret

; ---------------------------------------------------------------------------
;  Anti-effondrement, synthese, post-filtre, desaccentuation
; ---------------------------------------------------------------------------

; anti-effondrement (RFC 6716 4.3.5) : bruit dans les blocs courts vides
ce_anti:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	ldr	x21,[x19,CE_START]
	ldr	x23,[x19,CE_C]
	ldr	x24,[x19,CE_LM]
	ldr	w25,[x19,CE_SEED]
.i:	ldr	x0,[x19,CE_END]
	cmp	x21,x0
	b.hs	.out
	adr	x0,ce_eband
	ldrh	w1,[x0,x21,lsl 1]
	add	x2,x21,1
	ldrh	w2,[x0,x2,lsl 1]
	sub	w22,w2,w1			; N0
	lsl	w26,w1,w24			; eb[i] << LM
	add	x0,x19,CE_PULSES		; depth = (1 + pulses) / N0 >> LM
	ldr	w0,[x0,x21,lsl 2]
	add	w0,w0,1
	udiv	w0,w0,w22
	lsr	w0,w0,w24
	ucvtf	s0,w0				; thresh = 0,5 2^(-depth / 8)
	movl	w0,0xBE000000
	fmov	s1,w0
	fmul	s0,s0,s1
	bl	ce_exp2
	fmov	s1,0.5
	fmul	s8,s0,s1
	lsl	w0,w22,w24			; sqrt_1 = 1 / sqrt(N0 << LM)
	ucvtf	s0,w0
	fsqrt	s0,s0
	fmov	s1,1.0
	fdiv	s9,s1,s0
	mov	x27,0				; c
.c:	cmp	x27,x23
	b.hs	.ni
	mov	x0,CE_NBE
	madd	x0,x27,x0,x21
	add	x1,x19,CE_OLDLOGE
	ldr	s0,[x1,x0,lsl 2]		; prev1
	add	x2,x19,CE_OLDLOGE2
	ldr	s1,[x2,x0,lsl 2]		; prev2
	cmp	x23,1
	b.ne	.st
	add	x3,x21,CE_NBE
	ldr	s2,[x1,x3,lsl 2]
	fmax	s0,s0,s2
	ldr	s2,[x2,x3,lsl 2]
	fmax	s1,s1,s2
.st:	fmin	s0,s0,s1
	add	x1,x19,CE_OLDE
	ldr	s1,[x1,x0,lsl 2]
	fsub	s0,s1,s0			; Ediff
	movi	d1,0
	fmax	s0,s0,s1
	fneg	s0,s0
	bl	ce_exp2
	fadd	s0,s0,s0			; r = 2 2^(-Ediff)
	cmp	x24,3
	b.ne	.r3
	movl	w0,0x3FB504F3			; sqrt(2)
	fmov	s1,w0
	fmul	s0,s0,s1
.r3:	fmin	s0,s8,s0
	fmul	s10,s0,s9			; r
	fneg	s11,s10
	ldr	x0,[x19,CE_X]			; X du canal
	ldr	x1,[x19,CE_N]
	mul	x1,x1,x27
	add	x0,x0,x1,lsl 2
	add	x28,x0,x26,lsl 2
	mov	x9,0				; renormaliser
	mov	x10,0				; k
	mov	x11,1
	lsl	x11,x11,x24			; blocs
	ldr	x2,[x19,CE_C]
	mul	x2,x21,x2
	add	x2,x2,x27
	add	x3,x19,CE_CMASK
	ldrb	w12,[x3,x2]			; masque
	movl	w13,1664525
	movl	w14,1013904223
.k:	cmp	x10,x11
	b.hs	.kd
	lsr	w0,w12,w10
	tbnz	w0,0,.nk
	mov	x0,0				; j
.j:	madd	w25,w25,w13,w14
	tst	w25,0x8000
	fcsel	s0,s10,s11,ne
	lsl	x1,x0,x24
	add	x1,x1,x10
	str	s0,[x28,x1,lsl 2]
	add	x0,x0,1
	cmp	x0,x22
	b.lo	.j
	mov	x9,1
.nk:	add	x10,x10,1
	b	.k
.kd:	cbz	x9,.nc
	mov	x0,x28
	lsl	w1,w22,w24
	fmov	s0,1.0
	bl	ce_renorm
.nc:	add	x27,x27,1
	b	.c
.ni:	add	x21,x21,1
	b	.i
.out:	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = X du canal, x1 = sortie (N), x2 = energies du canal : denormalise
; (gain 2^(oldE + eMeans) de chaque bande ; zero hors des bandes)
ce_denorm:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
	mov	x0,x22
	ldr	x1,[x19,CE_N]
	lsl	x1,x1,2
	bl	ak_memzero
	ldr	x0,[x19,CE_SILENCE]
	cbnz	x0,.out
	ldr	x24,[x19,CE_START]
	ldr	x25,[x19,CE_LM]
.b:	ldr	x0,[x19,CE_END]
	cmp	x24,x0
	b.hs	.out
	ldr	s0,[x23,x24,lsl 2]
	adr	x0,ce_emeans
	ldr	s1,[x0,x24,lsl 2]
	fadd	s0,s0,s1
	movl	w0,0x42000000			; min(32, lg)
	fmov	s1,w0
	fmin	s0,s0,s1
	bl	ce_exp2
	adr	x0,ce_eband
	ldrh	w1,[x0,x24,lsl 1]
	add	x2,x24,1
	ldrh	w2,[x0,x2,lsl 1]
	lsl	w1,w1,w25
	lsl	w2,w2,w25
.j:	ldr	s1,[x21,x1,lsl 2]
	fmul	s1,s1,s0
	str	s1,[x22,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,x2
	b.lo	.j
	add	x24,x24,1
	b	.b
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; synthese (celt_synthesis) : spectres -> IMDCT de chaque bloc -> historique
; (recouvrement TDAC de 120 avec la fin du bloc precedent)
ce_synth:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	ldr	x21,[x19,CE_N]
	ldr	x22,[x19,CE_FREQ]
	ldr	x0,[x19,CE_CC]
	ldr	x1,[x19,CE_C]
	cmp	x0,x1
	b.eq	.same
	cmp	x0,2
	b.eq	.up
	; flux stereo, sortie mono : moyenne des deux canaux
	ldr	x0,[x19,CE_X]
	mov	x1,x22
	add	x2,x19,CE_OLDE
	bl	ce_denorm
	ldr	x0,[x19,CE_X]
	add	x0,x0,x21,lsl 2
	add	x1,x22,x21,lsl 2
	add	x2,x19,CE_OLDE+CE_NBE*4
	bl	ce_denorm
	fmov	s2,0.5
	mov	x0,0
.dm:	ldr	s0,[x22,x0,lsl 2]
	add	x1,x0,x21
	ldr	s1,[x22,x1,lsl 2]
	fmul	s0,s0,s2
	fmul	s1,s1,s2
	fadd	s0,s0,s1
	str	s0,[x22,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,x21
	b.lo	.dm
	mov	x0,0
	bl	ce_imdcts
	b	.out
.up:	; flux mono, sortie stereo : le meme spectre
	ldr	x0,[x19,CE_X]
	mov	x1,x22
	add	x2,x19,CE_OLDE
	bl	ce_denorm
	mov	x0,0
	bl	ce_imdcts
	mov	x0,1
	bl	ce_imdcts
	b	.out
.same:	mov	x23,0
.sc:	ldr	x0,[x19,CE_CC]
	cmp	x23,x0
	b.hs	.out
	ldr	x0,[x19,CE_X]
	mul	x1,x21,x23
	add	x0,x0,x1,lsl 2
	mov	x1,x22
	add	x2,x19,CE_OLDE
	mov	x3,CE_NBE*4
	madd	x2,x23,x3,x2
	bl	ce_denorm
	mov	x0,x23
	bl	ce_imdcts
	add	x23,x23,1
	b	.sc
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = canal : IMDCT des blocs de CE_FREQ vers l'historique du canal
ce_imdcts:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	add	x1,x19,CE_MEM
	ldr	x21,[x1,x0,lsl 3]
	ldr	x0,[x19,CE_N]
	mov	x1,2048
	sub	x1,x1,x0
	add	x21,x21,x1,lsl 2		; out_syn
	ldr	x22,[x19,CE_B]			; blocs
	udiv	x23,x0,x22			; NB
	mov	x0,0				; plan : NB = 120 << k
	mov	x1,120
.pk:	cmp	x1,x23
	b.hs	.pkd
	add	x0,x0,1
	lsl	x1,x1,1
	b	.pk
.pkd:	ldr	x24,[x19,CE_MD]
	mov	x1,MD_SIZE
	madd	x24,x0,x1,x24
	ldr	x25,[x19,CE_TMP]		; entree (NB), sortie (2 NB)
	add	x26,x25,960*4
	mov	x27,0				; bloc
.b:	cmp	x27,x22
	b.hs	.out
	ldr	x0,[x19,CE_FREQ]		; coefficients du bloc : freq[b + j B]
	add	x0,x0,x27,lsl 2
	mov	x1,0
.g:	ldr	s0,[x0]
	add	x0,x0,x22,lsl 2
	str	s0,[x25,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,x23
	b.lo	.g
	mov	x0,x24
	mov	x1,x25
	mov	x2,x26
	bl	ak_imdct
	; out[60 + j] = y[NB/2 + j], j < NB
	mul	x0,x27,x23
	add	x28,x21,x0,lsl 2		; out du bloc
	add	x0,x28,60*4
	lsr	x1,x23,1
	add	x1,x26,x1,lsl 2
	lsl	x2,x23,2
	bl	ak_memmove
	; TDAC : out[i], out[119 - i], i < 60
	adr	x0,ce_window
	mov	x1,0
	mov	x2,119
.t:	ldr	s0,[x28,x2,lsl 2]		; x1 (nouveau)
	ldr	s1,[x28,x1,lsl 2]		; x2 (precedent)
	ldr	s2,[x0,x1,lsl 2]		; w[i]
	ldr	s3,[x0,x2,lsl 2]		; w[119 - i]
	fmul	s4,s3,s1
	fmsub	s4,s2,s0,s4
	str	s4,[x28,x1,lsl 2]
	fmul	s4,s2,s1
	fmadd	s4,s3,s0,s4
	str	s4,[x28,x2,lsl 2]
	add	x1,x1,1
	sub	x2,x2,1
	cmp	x1,60
	b.lo	.t
	add	x27,x27,1
	b	.b
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; post-filtre (filtre en peigne recursif, en place) de chaque canal ; puis
; ses parametres passent a la trame suivante
ce_postfilter:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,0
	ldr	x22,[x19,CE_N]
	ldr	x23,[x19,CE_LM]
.c:	ldr	x0,[x19,CE_CC]
	cmp	x21,x0
	b.hs	.st
	ldr	x0,[x19,CE_PFPER]
	mov	x1,15
	cmp	x0,x1
	csel	x0,x0,x1,gt
	str	x0,[x19,CE_PFPER]
	ldr	x0,[x19,CE_PFPEROLD]
	cmp	x0,x1
	csel	x0,x0,x1,gt
	str	x0,[x19,CE_PFPEROLD]
	add	x0,x19,CE_MEM
	ldr	x24,[x0,x21,lsl 3]
	mov	x0,2048
	sub	x0,x0,x22
	add	x24,x24,x0,lsl 2		; out_syn
	mov	x0,x24
	ldr	x1,[x19,CE_PFPEROLD]
	ldr	x2,[x19,CE_PFPER]
	mov	x3,120
	ldr	s0,[x19,CE_PFGAINOLD]
	ldr	s1,[x19,CE_PFGAIN]
	ldr	x4,[x19,CE_PFTAPOLD]
	ldr	x5,[x19,CE_PFTAP]
	bl	ce_comb
	cbz	x23,.nc
	add	x0,x24,120*4
	ldr	x1,[x19,CE_PFPER]
	ldr	x2,[x19,CE_PFPITCH]
	sub	x3,x22,120
	ldr	s0,[x19,CE_PFGAIN]
	ldr	s1,[x19,CE_PFG]
	ldr	x4,[x19,CE_PFTAP]
	ldr	x5,[x19,CE_PFTAPN]
	bl	ce_comb
.nc:	add	x21,x21,1
	b	.c
.st:	ldr	x0,[x19,CE_PFPER]		; old = courant ; courant = trame
	str	x0,[x19,CE_PFPEROLD]
	ldr	w0,[x19,CE_PFGAIN]
	str	w0,[x19,CE_PFGAINOLD]
	ldr	x0,[x19,CE_PFTAP]
	str	x0,[x19,CE_PFTAPOLD]
	ldr	x0,[x19,CE_PFPITCH]
	str	x0,[x19,CE_PFPER]
	ldr	w0,[x19,CE_PFG]
	str	w0,[x19,CE_PFGAIN]
	ldr	x0,[x19,CE_PFTAPN]
	str	x0,[x19,CE_PFTAP]
	cbz	x23,.out
	ldr	x0,[x19,CE_PFPER]
	str	x0,[x19,CE_PFPEROLD]
	ldr	w0,[x19,CE_PFGAIN]
	str	w0,[x19,CE_PFGAINOLD]
	ldr	x0,[x19,CE_PFTAP]
	str	x0,[x19,CE_PFTAPOLD]
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = signal (en place), x1 = T0, x2 = T1, x3 = N, s0 = g0, s1 = g1, x4 =
; prises 0, x5 = prises 1 : filtre en peigne, passage de (T0, g0) a (T1, g1)
; sur les 120 premiers echantillons (fenetre au carre). Ne touche que x0-x9,
; v0-v7, v16-v23.
ce_comb:
	fcmp	s0,0.0
	b.ne	.go
	fcmp	s1,0.0
	b.eq	.ret
.go:	fmov	s23,s1				; g1
	mov	x6,15
	cmp	x1,x6
	csel	x1,x1,x6,gt
	cmp	x2,x6
	csel	x2,x2,x6,gt
	adr	x6,ce_combg
	mov	x7,12
	madd	x8,x4,x7,x6
	ldp	s16,s17,[x8]
	ldr	s18,[x8,8]
	fmul	s16,s16,s0			; g00, g01, g02
	fmul	s17,s17,s0
	fmul	s18,s18,s0
	madd	x8,x5,x7,x6
	ldp	s19,s20,[x8]
	ldr	s21,[x8,8]
	fmul	s19,s19,s1			; g10, g11, g12
	fmul	s20,s20,s1
	fmul	s21,s21,s1
	mov	x9,120				; recouvrement (0 si rien ne change)
	fcmp	s0,s1
	b.ne	.ov
	cmp	x1,x2
	b.ne	.ov
	cmp	x4,x5
	b.ne	.ov
	mov	x9,0
.ov:	adr	x6,ce_window
	mov	x7,0				; i
	fmov	s22,1.0
.o:	cmp	x7,x9
	b.hs	.od
	ldr	s0,[x6,x7,lsl 2]
	fmul	s0,s0,s0			; f = w^2
	fsub	s1,s22,s0			; 1 - f
	sub	x8,x7,x1			; x[i - T0 + k]
	add	x8,x0,x8,lsl 2
	ldr	s2,[x8]
	fmul	s3,s1,s16
	fmul	s3,s3,s2			; (1 - f) g00 x[i - T0]
	ldr	s2,[x8,4]
	ldr	s4,[x8,-4]
	fadd	s2,s2,s4
	fmul	s4,s1,s17
	fmadd	s3,s4,s2,s3
	ldr	s2,[x8,8]
	ldr	s4,[x8,-8]
	fadd	s2,s2,s4
	fmul	s4,s1,s18
	fmadd	s3,s4,s2,s3
	sub	x8,x7,x2			; x[i - T1 + k]
	add	x8,x0,x8,lsl 2
	ldr	s2,[x8]
	fmul	s4,s0,s19
	fmadd	s3,s4,s2,s3
	ldr	s2,[x8,4]
	ldr	s5,[x8,-4]
	fadd	s2,s2,s5
	fmul	s4,s0,s20
	fmadd	s3,s4,s2,s3
	ldr	s2,[x8,8]
	ldr	s5,[x8,-8]
	fadd	s2,s2,s5
	fmul	s4,s0,s21
	fmadd	s3,s4,s2,s3
	ldr	s2,[x0,x7,lsl 2]
	fadd	s2,s2,s3
	str	s2,[x0,x7,lsl 2]
	add	x7,x7,1
	b	.o
.od:	fcmp	s23,0.0				; g1 = 0 : rien de plus
	b.eq	.ret
.k:	cmp	x7,x3				; filtre constant (T1, g1)
	b.hs	.ret
	sub	x8,x7,x2
	add	x8,x0,x8,lsl 2
	ldr	s2,[x8]
	fmul	s3,s19,s2
	ldr	s2,[x8,4]
	ldr	s4,[x8,-4]
	fadd	s2,s2,s4
	fmadd	s3,s20,s2,s3
	ldr	s2,[x8,8]
	ldr	s4,[x8,-8]
	fadd	s2,s2,s4
	fmadd	s3,s21,s2,s3
	ldr	s2,[x0,x7,lsl 2]
	fadd	s2,s2,s3
	str	s2,[x0,x7,lsl 2]
	add	x7,x7,1
	b	.k
.ret:	ret

; energies passees (anti-effondrement), bandes hors de [start, end)
ce_history:
	ldr	x0,[x19,CE_C]
	cmp	x0,1
	b.ne	.h
	add	x1,x19,CE_OLDE			; mono : copie dans le canal 1
	mov	x2,0
.cp:	ldr	w3,[x1,x2,lsl 2]
	add	x4,x2,CE_NBE
	str	w3,[x1,x4,lsl 2]
	add	x2,x2,1
	cmp	x2,CE_NBE
	b.lo	.cp
.h:	add	x1,x19,CE_OLDE
	add	x2,x19,CE_OLDLOGE
	add	x3,x19,CE_OLDLOGE2
	ldr	x0,[x19,CE_TRANS]
	mov	x4,0
	cbnz	x0,.tr
.nt:	ldr	w5,[x2,x4,lsl 2]
	str	w5,[x3,x4,lsl 2]
	ldr	w5,[x1,x4,lsl 2]
	str	w5,[x2,x4,lsl 2]
	add	x4,x4,1
	cmp	x4,2 * CE_NBE
	b.lo	.nt
	b	.ob
.tr:	ldr	s0,[x2,x4,lsl 2]
	ldr	s1,[x1,x4,lsl 2]
	fmin	s0,s0,s1
	str	s0,[x2,x4,lsl 2]
	add	x4,x4,1
	cmp	x4,2 * CE_NBE
	b.lo	.tr
.ob:	movl	w6,0xC1E00000			; -28
	ldr	x7,[x19,CE_START]
	ldr	x8,[x19,CE_END]
	mov	x4,0
.o:	cmp	x4,x7
	b.lo	.z
	cmp	x4,x8
	b.lo	.n
.z:	str	wzr,[x1,x4,lsl 2]
	str	w6,[x2,x4,lsl 2]
	str	w6,[x3,x4,lsl 2]
	add	x5,x4,CE_NBE
	str	wzr,[x1,x5,lsl 2]
	str	w6,[x2,x5,lsl 2]
	str	w6,[x3,x5,lsl 2]
.n:	add	x4,x4,1
	cmp	x4,CE_NBE
	b.lo	.o
	ldr	w0,[x20,EC_RNG]			; graine de la trame suivante
	str	x0,[x19,CE_SEED]
	ret

; desaccentuation (1 / (1 - 0,85 z^-1)) -> CE_OUT (flottants -1..1)
ce_deemph:
	ldr	x0,[x19,CE_N]
	movl	w1,0x3F599A00			; 0,85000610
	fmov	s4,w1
	movl	w1,0x0DA24260			; 1e-30
	fmov	s5,w1
	movl	w1,0x38000000			; 1 / 32768
	fmov	s6,w1
	mov	x1,0				; canal
.c:	ldr	x2,[x19,CE_CC]
	cmp	x1,x2
	b.hs	.ret
	add	x2,x19,CE_MEM
	ldr	x2,[x2,x1,lsl 3]
	mov	x3,2048
	sub	x3,x3,x0
	add	x2,x2,x3,lsl 2			; out_syn
	add	x3,x19,CE_OUT
	ldr	x3,[x3,x1,lsl 3]
	add	x4,x19,CE_PREEMPH
	ldr	s0,[x4,x1,lsl 2]		; m
	ldr	x6,[x19,CE_ACCUM]
	mov	x5,0
.j:	ldr	s1,[x2,x5,lsl 2]
	fadd	s1,s1,s5
	fadd	s1,s1,s0			; tmp
	fmul	s0,s1,s4
	fmul	s1,s1,s6
	cbz	x6,.st
	ldr	s2,[x3,x5,lsl 2]
	fadd	s1,s2,s1
.st:	str	s1,[x3,x5,lsl 2]
	add	x5,x5,1
	cmp	x5,x0
	b.lo	.j
	str	s0,[x4,x1,lsl 2]
	add	x1,x1,1
	b	.c
.ret:	ret

; ---------------------------------------------------------------------------
;  Petites tables (RFC 6716)
; ---------------------------------------------------------------------------
		align	8
ce_halfpi	dd	1.5707963267948966
ce_pred		dw	0x3F660000, 0x3F4C0000, 0x3F260000, 0x3F000000	; 29440, 26112, 21248, 16384 / 32768
		dw	0x3F6B8600, 0x3F2E1400, 0x3EBD7000, 0x3E4CD000	; beta : 30147, 22282, 12124, 6554 / 32768
		dw	0x3E199800					; beta intra : 4915 / 32768
ce_combg	dw	0x3E9D0000, 0x3E5E4000, 0x3E04C000		; gains du filtre en peigne (3 jeux)
		dw	0x3EED8000, 0x3E894000, 0
		dw	0x3F4CC000, 0x3DCD0000, 0
ce_exp2t8	dh	16384, 17866, 19483, 21247, 23170, 25267, 27554, 30048
ce_tfsel	db	0, -1, 0, -1, 0, -1, 0, -1
		db	0, -1, 0, -2, 1, 0, 1, -1
		db	0, -2, 0, -3, 2, 0, 1, -1
		db	0, -2, 0, -3, 3, 0, 1, -1
ce_spread_icdf	db	25, 23, 2, 0
ce_trim_icdf	db	126, 124, 119, 109, 87, 41, 19, 9, 4, 2, 0
ce_tapset_icdf	db	2, 1, 0
ce_small_icdf	db	2, 1, 0
ce_spreadf	db	15, 10, 5
ce_ordery	db	1, 0
		db	3, 0, 2, 1
		db	7, 0, 4, 3, 6, 1, 5, 2
		db	15, 0, 8, 7, 12, 3, 11, 4, 14, 1, 9, 6, 13, 2, 10, 5
ce_bitil	db	0, 1, 1, 1, 2, 3, 3, 3, 2, 3, 3, 3, 2, 3, 3, 3
ce_bitdi	db	0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F
		db	0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
		align	4

include 'ak_opustab.inc'
