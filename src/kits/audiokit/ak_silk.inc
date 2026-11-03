; ============================================================================
;  kits/audiokit/ak_silk.inc - SILK (RFC 6716 4.2) : decodeur
;
;  Entiers (comme la norme : resultat exact a l'echantillon) : drapeaux VAD
;  et LBRR (les trames LBRR sont lues et sautees), prediction stereo, indices
;  (type, gains, NLSF a deux etages, interpolation, hauteur, LTP, graine),
;  impulsions (niveaux de debit, codage en coquille, bits faibles, signes),
;  parametres (gains, NLSF -> LPC avec stabilisation, hauteurs, filtres LTP),
;  excitation, synthese LTP (reblanchiment) et LPC, M/S -> G/D, puis
;  reechantillonnage vers 48 kHz (passe-tout x2 puis FIR fractionnaire).
;  NB, MB, WB ; trames de 10 ou 20 ms (paquets de 10 a 60 ms) ; mono, stereo.
;  Pas de PLC ni de bruit de confort (sans perte de paquets, ils ne changent
;  pas la sortie).
;
;  Dans tout le fichier : x19 = etat SILK (SK_*), x20 = decodeur de plage.
; ============================================================================

; --- un canal (SKC_*) -------------------------------------------------------------
SKC_FS		= 0		; frequence interne (kHz, 0 : aucune)
SKC_NBSUB	= 4		; sous-trames (2, 4)
SKC_FL		= 8		; echantillons par trame
SKC_SL		= 12		; echantillons par sous-trame
SKC_LTPMEM	= 16
SKC_ORDER	= 20		; ordre LPC (10, 16)
SKC_WB		= 24		; dictionnaire NLSF : 0 NB/MB, 1 WB
SKC_FIRST	= 28		; premiere trame apres une remise a zero
SKC_LAGPREV	= 32
SKC_LASTGAIN	= 36
SKC_PREVTYPE	= 40
SKC_PREVGAIN	= 44
SKC_ECPTYPE	= 48
SKC_ECPLAG	= 52
SKC_NFDEC	= 56		; trames decodees dans le paquet
SKC_NFPP	= 60		; trames par paquet
SKC_CONTOUR	= 64		; table icdf du contour de hauteur
SKC_LOWBITS	= 72		; table icdf des bits faibles de la hauteur
SKC_VAD		= 80		; 3 octets
SKC_LBRR	= 84		; 3 octets
SKC_LBRRF	= 88
SKC_TYPE	= 92		; indices de la trame
SKC_QOFF	= 96
SKC_INTERP	= 100
SKC_LAGIDX	= 104
SKC_CONTIDX	= 108
SKC_PERIDX	= 112
SKC_LTPSCIDX	= 116
SKC_SEED	= 120
SKC_LTPSCALE	= 124
SKC_GIDX	= 128		; 4 octets
SKC_LTPIDX	= 132		; 4 octets
SKC_NLSFIDX	= 136		; 17 octets signes
SKC_GAINS	= 160		; parametres : 4 x 32 bits
SKC_PITCHL	= 176		; 4 x 32 bits
SKC_A		= 192		; 2 x 16 x 16 bits (Q12)
SKC_LTPC	= 256		; 20 x 16 bits (Q14)
SKC_PREVNLSF	= 304		; 16 x 16 bits
SKC_SLPC	= 336		; 16 x 32 bits (Q14)
SKC_RS		= 400		; reechantillonneur (RS_*)
SKC_OUTBUF	= 496		; 480 x 16 bits
SKC_SIZE	= 1456

RS_FSIN		= 0
RS_DELAY	= 4
RS_BATCH	= 8
RS_INV		= 12
RS_IIR		= 16		; 6 x 32 bits
RS_FIR		= 40		; 8 x 16 bits
RS_DBUF		= 56		; 16 x 16 bits
RS_SIZE		= 96

; --- le decodeur SILK d'un flux (SK_*) --------------------------------------------
SK_CC		= 0		; canaux de sortie
SK_NCHI		= 4		; canaux internes (du paquet precedent)
SK_NCHAPI	= 8
SK_ONLYMID	= 12		; trame precedente : milieu seul
SK_PREDPREV	= 16		; 2 x 32 bits (Q13)
SK_SMID		= 24		; 2 x 16 bits
SK_SSIDE	= 28
SK_MS		= 32		; predicteurs de la trame (2 x 32 bits)
SK_TMP		= 40		; 2 x 32 bits (predicteurs LBRR ignores)
SK_CH		= 64		; 2 canaux
SK_XSZ		= 2 * (2 + 320 + 6)
SK_X		= SK_CH + 2 * SKC_SIZE	; sorties des canaux (2 + trame, 16 bits)
SK_PULSES	= SK_X + 2 * SK_XSZ	; 336 x 16 bits
SK_SLTP		= SK_PULSES + 672	; 320 x 16 bits
SK_SLTPQ	= SK_SLTP + 640		; 640 x 32 bits
SK_SLPCW	= SK_SLTPQ + 2560	; 96 x 32 bits
SK_EXC		= SK_SLPCW + 384	; 320 x 32 bits
SK_RSOUT	= SK_EXC + 1280		; 960 x 16 bits (+ marge)
SK_RSBUF	= SK_RSOUT + 2048	; 2 x 160 + 8 x 16 bits
SK_SIZE		= SK_RSBUF + 704

; reg = x19 + decalage (au-dela de 12 bits)
macro skadd reg*, off* {
 if (off) < 4096
	add	reg,x19,off
 else
	add	reg,x19,((off) shr 12),lsl 12
  if (off) and 0xFFF <> 0
	add	reg,reg,(off) and 0xFFF
  end if
 end if
}

; icdf : x0 = decodeur, x1 = table -> w0
macro skicdf tab {
	mov	x0,x20
	adr	x1,tab
	mov	w2,8
	bl	ec_icdf
}

; x0 = decodeur (memoire), x1 = canaux de sortie -> x0 = etat SILK / < 0
silk_new:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x1
	movl	x1,SK_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x19,x0
	str	w20,[x19,SK_CC]
	bl	silk_reset
	mov	x0,x19
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat SILK : oublie l'historique (silk_ResetDecoder)
silk_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	skadd	x0,SK_CH
	bl	skc_reset
	skadd	x0,SK_CH+SKC_SIZE
	bl	skc_reset
	str	wzr,[x19,SK_ONLYMID]
	stp	xzr,xzr,[x19,SK_PREDPREV]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = canal : tout a zero (silk_init_decoder)
skc_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	movl	x1,SKC_SIZE
	bl	ak_memzero
	mov	w0,1
	str	w0,[x19,SKC_FIRST]
	mov	w0,65536
	str	w0,[x19,SKC_PREVGAIN]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = canal, w1 = frequence (kHz), w2 = sous-trames (silk_decoder_set_fs)
skc_setfs:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	w22,w1
	mov	w23,w2
	str	w23,[x21,SKC_NBSUB]
	mov	w0,5
	mul	w0,w0,w22
	str	w0,[x21,SKC_SL]
	mul	w24,w0,w23			; frame_length
	ldr	w0,[x21,SKC_FS]
	cmp	w0,w22
	b.eq	.nrs
	add	x0,x21,SKC_RS			; reechantillonneur vers 48 kHz
	mov	w1,w22
	bl	sk_rsinit
.nrs:	ldr	w0,[x21,SKC_FS]
	cmp	w0,w22
	b.ne	.ch
	ldr	w0,[x21,SKC_FL]
	cmp	w0,w24
	b.eq	.out
.ch:	cmp	w22,8
	b.ne	.c16
	adr	x0,sk_cont_nb
	adr	x1,sk_cont10_nb
	b	.cs
.c16:	adr	x0,sk_cont
	adr	x1,sk_cont10
.cs:	cmp	w23,4
	csel	x0,x0,x1,eq
	str	x0,[x21,SKC_CONTOUR]
	ldr	w0,[x21,SKC_FS]
	cmp	w0,w22
	b.eq	.nfs
	mov	w0,20
	mul	w0,w0,w22
	str	w0,[x21,SKC_LTPMEM]
	mov	w0,10
	mov	w1,0
	cmp	w22,16
	b.ne	.o10
	mov	w0,16
	mov	w1,1
.o10:	str	w0,[x21,SKC_ORDER]
	str	w1,[x21,SKC_WB]
	adr	x0,sk_unif8
	cmp	w22,12
	b.ne	.lb
	adr	x0,sk_unif6
.lb:	cmp	w22,8
	b.ne	.lb2
	adr	x0,sk_unif4
.lb2:	str	x0,[x21,SKC_LOWBITS]
	mov	w0,1
	str	w0,[x21,SKC_FIRST]
	mov	w0,100
	str	w0,[x21,SKC_LAGPREV]
	mov	w0,10
	str	w0,[x21,SKC_LASTGAIN]
	str	wzr,[x21,SKC_PREVTYPE]
	add	x0,x21,SKC_OUTBUF
	mov	x1,960
	bl	ak_memzero
	add	x0,x21,SKC_SLPC
	mov	x1,64
	bl	ak_memzero
.nfs:	str	w22,[x21,SKC_FS]
	str	w24,[x21,SKC_FL]
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Arithmetique entiere de SILK (Inlines.h)
; ---------------------------------------------------------------------------

; w0 = gain (log, Q7) -> w0 = lineaire (silk_log2lin). Ne touche que x0-x3.
sk_log2lin:
	cmp	w0,0
	b.lt	.z
	mov	w1,3967
	cmp	w0,w1
	b.ge	.max
	lsr	w1,w0,7
	mov	w2,1
	lsl	w1,w2,w1			; out
	and	w2,w0,0x7F			; frac
	mov	w3,128
	sub	w3,w3,w2
	mul	w3,w3,w2			; frac (128 - frac)
	mov	x4,-174
	smull	x3,w3,w4
	asr	x3,x3,16
	add	w3,w3,w2			; SMLAWB(frac, ..., -174)
	cmp	w0,2048
	b.ge	.hi
	mul	w3,w1,w3
	add	w0,w1,w3,asr 7
	ret
.hi:	asr	w2,w1,7
	madd	w0,w2,w3,w1
	ret
.z:	mov	w0,0
	ret
.max:	movl	w0,0x7FFFFFFF
	ret

; w0 = b32, w1 = Qres -> w0 = 2^Qres / b32 approche (silk_INVERSE32_varQ).
; Ne touche que x0-x7.
sk_inv32:
	cmp	w0,0
	cneg	w2,w0,lt
	clz	w2,w2
	sub	w2,w2,1				; b_headrm
	lsl	w3,w0,w2			; b32_nrm
	asr	w4,w3,16
	movl	w5,0x1FFFFFFF
	sdiv	w4,w5,w4			; b32_inv
	lsl	w5,w4,16			; result
	sxth	w6,w4				; err = ((1 << 29) - SMULWB(b32_nrm, b32_inv)) << 3
	smull	x6,w3,w6
	asr	x6,x6,16
	mov	w7,0x20000000
	sub	w6,w7,w6
	lsl	w6,w6,3
	smull	x6,w6,w4			; result += (err b32_inv) >> 16
	asr	x6,x6,16
	add	w5,w5,w6
	mov	w6,61
	sub	w6,w6,w2
	sub	w6,w6,w1			; lshift
	cmp	w6,0
	b.gt	.r
	neg	w6,w6				; LSHIFT_SAT32
	b	sk_lsat
.r:	cmp	w6,32
	b.ge	.zero
	asr	w0,w5,w6
	ret
.zero:	mov	w0,0
	ret
; w5 = valeur, w6 = decalage -> w0 = silk_LSHIFT_SAT32. Ne touche que x0-x7.
sk_lsat:
	movl	w7,0x80000000
	asr	w7,w7,w6			; min >> s
	movl	w4,0x7FFFFFFF
	asr	w4,w4,w6			; max >> s
	cmp	w5,w7
	csel	w5,w7,w5,lt
	cmp	w5,w4
	csel	w5,w4,w5,gt
	lsl	w0,w5,w6
	ret

; w0 = a32, w1 = b32, w2 = Qres -> w0 = (a32 << Qres) / b32 approche
; (silk_DIV32_varQ). Ne touche que x0-x9.
sk_div32:
	cmp	w0,0
	cneg	w3,w0,lt
	clz	w3,w3
	sub	w3,w3,1				; a_headrm
	lsl	w4,w0,w3			; a32_nrm
	cmp	w1,0
	cneg	w5,w1,lt
	clz	w5,w5
	sub	w5,w5,1				; b_headrm
	lsl	w6,w1,w5			; b32_nrm
	asr	w7,w6,16
	movl	w8,0x1FFFFFFF
	sdiv	w7,w8,w7			; b32_inv
	sxth	w8,w7
	smull	x9,w4,w8
	asr	x9,x9,16			; result = SMULWB(a32_nrm, b32_inv)
	smull	x0,w6,w9			; a32_nrm -= SMMUL(b32_nrm, result) << 3
	asr	x0,x0,32
	sub	w4,w4,w0,lsl 3
	smull	x0,w4,w8
	asr	x0,x0,16
	add	w9,w9,w0			; result += SMULWB(a32_nrm, b32_inv)
	mov	w0,29
	add	w0,w0,w3
	sub	w0,w0,w5
	sub	w6,w0,w2			; lshift
	cmp	w6,0
	b.ge	.r
	neg	w6,w6
	mov	w5,w9
	b	sk_lsat
.r:	cmp	w6,32
	b.ge	.zero
	asr	w0,w9,w6
	ret
.zero:	mov	w0,0
	ret

; ---------------------------------------------------------------------------
;  Une trame SILK d'un paquet Opus (silk_Decode)
; ---------------------------------------------------------------------------

; x0 = etat SILK, x1 = decodeur de plage, w2 = canaux du paquet, w3 =
; frequence interne (kHz), w4 = duree du paquet (ms), w5 = 1 : premiere trame
; du paquet, x6, x7 = sorties (flottants -1..1, 48 kHz) -> x0 = trames rendues
; (48 kHz) / < 0
silk_decode:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2				; canaux internes
	mov	w22,w3				; kHz
	mov	w23,w4				; ms
	stp	x6,x7,[sp,16]			; sorties
	skadd	x27,SK_CH			; canal 0
	add	x28,x27,SKC_SIZE		; canal 1
	cbz	w5,.nnew
	str	wzr,[x27,SKC_NFDEC]
	str	wzr,[x28,SKC_NFDEC]
.nnew:	ldr	w0,[x19,SK_NCHI]		; mono -> stereo : canal 1 neuf
	cmp	w21,w0
	b.ls	.n1
	mov	x0,x28
	bl	skc_reset
.n1:	mov	w0,0				; stereo -> mono a la meme frequence
	cmp	w21,1
	b.ne	.n2
	ldr	w1,[x19,SK_NCHI]
	cmp	w1,2
	b.ne	.n2
	ldr	w1,[x27,SKC_FS]
	cmp	w1,w22
	cset	w0,eq
.n2:	str	x0,[sp]				; stereo_to_mono
	ldr	w0,[x27,SKC_NFDEC]
	cbnz	w0,.n3
	; duree : trames par paquet, sous-trames
	mov	w24,1
	mov	w25,2
	cmp	w23,10
	b.eq	.fp
	mov	w25,4
	cmp	w23,20
	b.eq	.fp
	mov	w24,2
	cmp	w23,40
	b.eq	.fp
	mov	w24,3
	cmp	w23,60
	b.ne	.bad
.fp:	str	w24,[x27,SKC_NFPP]
	mov	x0,x27
	mov	w1,w22
	mov	w2,w25
	bl	skc_setfs
	cmp	w21,2
	b.ne	.n3
	str	w24,[x28,SKC_NFPP]
	mov	x0,x28
	mov	w1,w22
	mov	w2,w25
	bl	skc_setfs
.n3:	; passage en stereo : predicteurs, cote, reechantillonneur du canal 1
	ldr	w0,[x19,SK_CC]
	cmp	w0,2
	b.ne	.n4
	cmp	w21,2
	b.ne	.n4
	ldr	w0,[x19,SK_NCHAPI]
	cmp	w0,1
	b.eq	.st
	ldr	w0,[x19,SK_NCHI]
	cmp	w0,1
	b.ne	.n4
.st:	stp	xzr,xzr,[x19,SK_PREDPREV]
	str	wzr,[x19,SK_SSIDE]
	add	x0,x28,SKC_RS
	add	x1,x27,SKC_RS
	mov	x2,RS_SIZE
	bl	ak_memmove
.n4:	ldr	w0,[x19,SK_CC]
	str	w0,[x19,SK_NCHAPI]
	str	w21,[x19,SK_NCHI]
	str	xzr,[x19,SK_MS]
	mov	w26,0				; only_mid
	str	xzr,[sp,40]
	ldr	w0,[x27,SKC_NFDEC]
	cbnz	w0,.ms
	; drapeaux VAD et LBRR de chaque canal
	mov	x24,x27
	mov	w25,0
.vf:	cmp	w25,w21
	b.hs	.vfd
	mov	x23,0
.vf1:	ldr	w0,[x24,SKC_NFPP]
	cmp	w23,w0
	b.hs	.vf2
	mov	x0,x20
	mov	w1,1
	bl	ec_bit_logp
	add	x1,x24,SKC_VAD
	strb	w0,[x1,x23]
	add	x23,x23,1
	b	.vf1
.vf2:	mov	x0,x20
	mov	w1,1
	bl	ec_bit_logp
	str	w0,[x24,SKC_LBRRF]
	mov	x24,x28
	add	w25,w25,1
	b	.vf
.vfd:	mov	x24,x27
	mov	w25,0
.lf:	cmp	w25,w21
	b.hs	.lfd
	str	wzr,[x24,SKC_LBRR]
	ldr	w0,[x24,SKC_LBRRF]
	cbz	w0,.lf2
	ldr	w0,[x24,SKC_NFPP]
	cmp	w0,1
	b.ne	.lf1
	mov	w0,1
	strb	w0,[x24,SKC_LBRR]
	b	.lf2
.lf1:	adr	x1,sk_lbrr2
	adr	x2,sk_lbrr3
	cmp	w0,2
	csel	x1,x1,x2,eq
	mov	x0,x20
	mov	w2,8
	bl	ec_icdf
	add	w0,w0,1
	and	w1,w0,1
	strb	w1,[x24,SKC_LBRR]
	ubfx	w1,w0,1,1
	strb	w1,[x24,SKC_LBRR+1]
	ubfx	w1,w0,2,1
	strb	w1,[x24,SKC_LBRR+2]
.lf2:	mov	x24,x28
	add	w25,w25,1
	b	.lf
.lfd:	; trames LBRR : lues et sautees
	mov	w23,0				; i
.lb:	ldr	w0,[x27,SKC_NFPP]
	cmp	w23,w0
	b.hs	.ms
	mov	x24,x27
	mov	w25,0				; n
.lbn:	cmp	w25,w21
	b.hs	.lbi
	add	x0,x24,SKC_LBRR
	ldrb	w0,[x0,x23]
	cbz	w0,.lbx
	cmp	w21,2
	b.ne	.lb1
	cbnz	w25,.lb1
	mov	x0,x20
	skadd	x1,SK_TMP
	bl	sk_spred
	add	x0,x28,SKC_LBRR
	ldrb	w0,[x0,x23]
	cbnz	w0,.lb1
	skicdf	sk_only_mid
.lb1:	mov	w4,0				; codage : conditionnel si LBRR avant
	cbz	w23,.lb2
	add	x0,x24,SKC_LBRR
	sub	x1,x23,1
	ldrb	w0,[x0,x1]
	cbz	w0,.lb2
	mov	w4,2
.lb2:	mov	x0,x24
	mov	w1,w23
	mov	w2,1
	mov	w3,w4
	bl	sk_indices
	skadd	x1,SK_PULSES
	ldr	w2,[x24,SKC_TYPE]
	ldr	w3,[x24,SKC_QOFF]
	ldr	w4,[x24,SKC_FL]
	bl	sk_pulses
.lbx:	mov	x24,x28
	add	w25,w25,1
	b	.lbn
.lbi:	add	w23,w23,1
	b	.lb
.ms:	; predicteurs stereo, milieu seul
	cmp	w21,2
	b.ne	.ms2
	mov	x0,x20
	skadd	x1,SK_MS
	bl	sk_spred
	ldr	w0,[x27,SKC_NFDEC]
	add	x1,x28,SKC_VAD
	ldrb	w0,[x1,x0]
	cbnz	w0,.ms2
	skicdf	sk_only_mid
	mov	w26,w0
	str	x26,[sp,40]
.ms2:	; premier cote apres des trames de milieu seul : canal 1 neuf
	cmp	w21,2
	b.ne	.ms3
	cbnz	w26,.ms3
	ldr	w0,[x19,SK_ONLYMID]
	cmp	w0,1
	b.ne	.ms3
	add	x0,x28,SKC_OUTBUF
	mov	x1,960
	bl	ak_memzero
	add	x0,x28,SKC_SLPC
	mov	x1,64
	bl	ak_memzero
	mov	w0,100
	str	w0,[x28,SKC_LAGPREV]
	mov	w0,10
	str	w0,[x28,SKC_LASTGAIN]
	str	wzr,[x28,SKC_PREVTYPE]
	mov	w0,1
	str	w0,[x28,SKC_FIRST]
.ms3:	; les trames des canaux
	mov	x24,x27
	mov	w25,0
	mov	w23,0				; echantillons decodes
.dc:	cmp	w25,w21
	b.hs	.dcd
	skadd	x0,SK_X
	mov	x1,SK_XSZ
	madd	x0,x25,x1,x0
	str	x0,[sp,32]
	cbz	w25,.dec
	cbnz	w26,.zero
.dec:	ldr	w0,[x27,SKC_NFDEC]		; codage
	sub	w0,w0,w25
	mov	w1,0
	cmp	w0,0
	b.le	.cd
	mov	w1,2
	cbz	w25,.cd
	ldr	w0,[x19,SK_ONLYMID]
	cbz	w0,.cd
	mov	w1,1
.cd:	mov	x0,x24
	ldr	x2,[sp,32]
	add	x2,x2,4
	bl	sk_frame
	mov	w23,w0
	b	.dn
.zero:	ldr	x0,[sp,32]
	add	x0,x0,4
	lsl	w1,w23,1
	bl	ak_memzero
.dn:	ldr	w0,[x24,SKC_NFDEC]
	add	w0,w0,1
	str	w0,[x24,SKC_NFDEC]
	mov	x24,x28
	add	w25,w25,1
	b	.dc
.dcd:	skadd	x24,SK_X			; x1 du canal 0
	ldr	w0,[x19,SK_CC]
	cmp	w0,2
	b.ne	.buf
	cmp	w21,2
	b.ne	.buf
	mov	x0,x24
	add	x1,x24,SK_XSZ
	ldr	w2,[x27,SKC_FS]
	mov	w3,w23
	bl	sk_ms2lr
	b	.rs
.buf:	ldr	w0,[x19,SK_SMID]		; retard d'un echantillon (milieu)
	str	w0,[x24]
	add	x1,x24,x23,lsl 1
	ldr	w0,[x1]
	str	w0,[x19,SK_SMID]
.rs:	; reechantillonnage vers 48 kHz, en flottants
	ldr	w0,[x27,SKC_FS]
	mov	w1,48
	mul	w1,w1,w23
	udiv	w25,w1,w0			; trames rendues
	ldr	w0,[x19,SK_CC]
	cmp	w0,w21
	csel	w0,w0,w21,lo
	mov	w26,0
.rc:	cmp	w26,w0
	b.hs	.rcd
	str	x0,[sp,8]
	mov	x0,SKC_SIZE
	madd	x0,x26,x0,x27
	add	x0,x0,SKC_RS
	skadd	x2,SK_X
	mov	x1,SK_XSZ
	madd	x2,x26,x1,x2
	add	x2,x2,2				; &x[1]
	mov	w3,w23
	add	x4,sp,16
	ldr	x4,[x4,x26,lsl 3]
	bl	sk_rsfloat
	ldr	x0,[sp,8]
	add	w26,w26,1
	b	.rc
.rcd:	ldr	w0,[x19,SK_CC]			; flux mono, sortie stereo
	cmp	w0,2
	b.ne	.ret
	cmp	w21,1
	b.ne	.ret
	ldr	x0,[sp]
	cbz	x0,.cp
	add	x0,x28,SKC_RS
	skadd	x2,SK_X+2
	mov	w3,w23
	ldr	x4,[sp,24]
	bl	sk_rsfloat
	b	.ret
.cp:	ldp	x0,x1,[sp,16]
	mov	x2,0
.cpl:	ldr	w3,[x0,x2,lsl 2]
	str	w3,[x1,x2,lsl 2]
	add	x2,x2,1
	cmp	w2,w25
	b.lo	.cpl
.ret:	ldr	x0,[sp,40]
	str	w0,[x19,SK_ONLYMID]
	mov	w0,w25
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	add	sp,sp,64
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = reechantillonneur, x2 = entree (16 bits), w3 = echantillons, x4 =
; sortie (flottants) : reechantillonne vers 48 kHz, puis / 32768
sk_rsfloat:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x4
	skadd	x1,SK_RSOUT
	bl	sk_resample
	mov	w22,w0
	skadd	x1,SK_RSOUT
	movl	w2,0x38000000			; 1 / 32768
	fmov	s1,w2
	mov	x2,0
.c:	cmp	w2,w22
	b.hs	.out
	ldrsh	w3,[x1,x2,lsl 1]
	scvtf	s0,w3
	fmul	s0,s0,s1
	str	s0,[x21,x2,lsl 2]
	add	x2,x2,1
	b	.c
.out:	ldp	x21,x22,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Trame d'un canal (silk_decode_frame)
; ---------------------------------------------------------------------------

; x0 = canal, w1 = codage (0 independant, 1 sans echelle LTP, 2 conditionnel),
; x2 = sortie (16 bits) -> w0 = echantillons
sk_frame:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	w22,w1
	mov	x23,x2
	ldr	w1,[x21,SKC_NFDEC]
	mov	w2,0
	mov	w3,w22
	bl	sk_indices
	skadd	x1,SK_PULSES
	ldr	w2,[x21,SKC_TYPE]
	ldr	w3,[x21,SKC_QOFF]
	ldr	w4,[x21,SKC_FL]
	bl	sk_pulses
	mov	x0,x21
	mov	w1,w22
	bl	sk_params
	mov	x0,x21
	skadd	x1,SK_PULSES
	mov	x2,x23
	bl	sk_core
	; historique de sortie
	ldr	w24,[x21,SKC_FL]
	ldr	w0,[x21,SKC_LTPMEM]
	sub	w22,w0,w24			; mv_len
	add	x0,x21,SKC_OUTBUF
	add	x1,x0,x24,lsl 1
	lsl	x2,x22,1
	bl	ak_memmove
	add	x0,x21,SKC_OUTBUF
	add	x0,x0,x22,lsl 1
	mov	x1,x23
	lsl	x2,x24,1
	bl	ak_memmove
	ldr	w0,[x21,SKC_TYPE]
	str	w0,[x21,SKC_PREVTYPE]
	str	wzr,[x21,SKC_FIRST]
	ldr	w0,[x21,SKC_NBSUB]
	sub	w0,w0,1
	add	x1,x21,SKC_PITCHL
	ldr	w0,[x1,w0,uxtw 2]
	str	w0,[x21,SKC_LAGPREV]
	mov	w0,w24
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = canal, w1 = trame dans le paquet, w2 = 1 : LBRR, w3 = codage :
; indices de la trame (silk_decode_indices)
sk_indices:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x21,x0
	mov	w22,w1
	mov	w23,w2
	mov	w24,w3
	; type de signal, decalage de quantification
	cbnz	w23,.vad
	add	x0,x21,SKC_VAD
	ldrb	w0,[x0,x22]
	cbnz	w0,.vad
	skicdf	sk_type_novad
	b	.ty
.vad:	skicdf	sk_type_vad
	add	w0,w0,2
.ty:	lsr	w1,w0,1
	str	w1,[x21,SKC_TYPE]
	and	w0,w0,1
	str	w0,[x21,SKC_QOFF]
	; gains
	cmp	w24,2
	b.ne	.gi
	skicdf	sk_dgain
	b	.g0
.gi:	ldr	w1,[x21,SKC_TYPE]
	adr	x0,sk_gain_icdf
	add	x1,x0,x1,lsl 3
	mov	x0,x20
	mov	w2,8
	bl	ec_icdf
	lsl	w25,w0,3
	skicdf	sk_unif8
	add	w0,w0,w25
.g0:	strb	w0,[x21,SKC_GIDX]
	mov	w25,1
.gl:	ldr	w0,[x21,SKC_NBSUB]
	cmp	w25,w0
	b.hs	.nl
	skicdf	sk_dgain
	add	x1,x21,SKC_GIDX
	strb	w0,[x1,x25]
	add	w25,w25,1
	b	.gl
.nl:	; NLSF : premier etage, puis residus
	ldr	w0,[x21,SKC_WB]
	adr	x1,sk_cb1i_nb
	adr	x2,sk_cb1i_wb
	cmp	w0,0
	csel	x1,x1,x2,eq
	ldr	w0,[x21,SKC_TYPE]
	lsr	w0,w0,1
	add	x1,x1,x0,lsl 5
	mov	x0,x20
	mov	w2,8
	bl	ec_icdf
	strb	w0,[x21,SKC_NLSFIDX]
	ldr	w1,[x21,SKC_ORDER]
	mul	w0,w0,w1
	lsr	w0,w0,1
	ldr	w2,[x21,SKC_WB]
	adr	x3,sk_sel_nb
	adr	x4,sk_sel_wb
	cmp	w2,0
	csel	x3,x3,x4,eq
	add	x26,x3,x0			; selection des tables
	mov	w25,0
.nr:	ldr	w0,[x21,SKC_ORDER]
	cmp	w25,w0
	b.hs	.nrd
	lsr	w0,w25,1
	ldrb	w0,[x26,x0]
	tst	w25,1
	b.ne	.odd
	ubfx	w0,w0,1,3
	b	.ix
.odd:	ubfx	w0,w0,5,3
.ix:	mov	w1,9
	mul	w0,w0,w1
	ldr	w2,[x21,SKC_WB]
	adr	x1,sk_cb2i_nb
	adr	x3,sk_cb2i_wb
	cmp	w2,0
	csel	x1,x1,x3,eq
	add	x1,x1,x0
	mov	x0,x20
	mov	w2,8
	bl	ec_icdf
	cbnz	w0,.n8
	skicdf	sk_nlsf_ext
	neg	w0,w0
	b	.nst
.n8:	cmp	w0,8
	b.ne	.nst
	mov	w22,w0				; (trame : plus utile)
	skicdf	sk_nlsf_ext
	add	w0,w0,w22
.nst:	sub	w0,w0,4
	add	x1,x21,SKC_NLSFIDX+1
	strb	w0,[x1,x25]
	add	w25,w25,1
	b	.nr
.nrd:	; interpolation
	mov	w0,4
	ldr	w1,[x21,SKC_NBSUB]
	cmp	w1,4
	b.ne	.ni
	skicdf	sk_interp
.ni:	str	w0,[x21,SKC_INTERP]
	ldr	w0,[x21,SKC_TYPE]
	cmp	w0,2
	b.ne	.seed
	; hauteur : relative ou absolue
	mov	w25,1
	cmp	w24,2
	b.ne	.abs
	ldr	w0,[x21,SKC_ECPTYPE]
	cmp	w0,2
	b.ne	.abs
	skicdf	sk_dlag_icdf
	cbz	w0,.abs
	ldr	w1,[x21,SKC_ECPLAG]
	add	w0,w0,w1
	sub	w0,w0,9
	str	w0,[x21,SKC_LAGIDX]
	mov	w25,0
.abs:	cbz	w25,.lag
	skicdf	sk_lag_icdf
	ldr	w1,[x21,SKC_FS]
	lsr	w1,w1,1
	mul	w25,w0,w1
	mov	x0,x20
	ldr	x1,[x21,SKC_LOWBITS]
	mov	w2,8
	bl	ec_icdf
	add	w0,w0,w25
	str	w0,[x21,SKC_LAGIDX]
.lag:	ldr	w0,[x21,SKC_LAGIDX]
	str	w0,[x21,SKC_ECPLAG]
	mov	x0,x20
	ldr	x1,[x21,SKC_CONTOUR]
	mov	w2,8
	bl	ec_icdf
	str	w0,[x21,SKC_CONTIDX]
	skicdf	sk_per_icdf
	str	w0,[x21,SKC_PERIDX]
	mov	w25,0
.lt:	ldr	w0,[x21,SKC_NBSUB]
	cmp	w25,w0
	b.hs	.ltd
	ldr	w0,[x21,SKC_PERIDX]
	adr	x1,sk_ltpg0
	cmp	w0,1
	b.lo	.lg
	adr	x1,sk_ltpg1
	b.eq	.lg
	adr	x1,sk_ltpg2
.lg:	mov	x0,x20
	mov	w2,8
	bl	ec_icdf
	add	x1,x21,SKC_LTPIDX
	strb	w0,[x1,x25]
	add	w25,w25,1
	b	.lt
.ltd:	mov	w0,0
	cbnz	w24,.lsc
	skicdf	sk_ltpscale_icdf
.lsc:	str	w0,[x21,SKC_LTPSCIDX]
.seed:	ldr	w0,[x21,SKC_TYPE]
	str	w0,[x21,SKC_ECPTYPE]
	skicdf	sk_unif4
	str	w0,[x21,SKC_SEED]
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; w0 = p, x1 = table de coquille -> w0 = premier fils, w1 = second
; (decode_split)
sk_split:
	cbz	w0,.z
	stp	x29,x30,[sp,-16]!
	str	x0,[sp,-16]!
	adr	x2,sk_shell_off
	ldrb	w2,[x2,w0,uxtw]
	add	x1,x1,x2
	mov	x0,x20
	mov	w2,8
	bl	ec_icdf
	ldr	x1,[sp],16
	sub	w1,w1,w0
	ldp	x29,x30,[sp],16
	ret
.z:	mov	w1,0
	ret

; x1 = sortie (16 x 16 bits), w2 = impulsions du bloc : codage en coquille
; (silk_shell_decoder : 4 niveaux de divisions binaires)
sk_shell:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x21,x1
	mov	w0,w2
	adr	x1,sk_shell3
	bl	sk_split
	mov	w22,w0				; p3[0]
	mov	w23,w1				; p3[1]
	mov	x25,0				; moitie (0, 8)
.h:	mov	w0,w22
	adr	x1,sk_shell2
	bl	sk_split
	mov	w24,w1				; p2[1]
	adr	x1,sk_shell1
	bl	sk_split
	mov	w26,w1				; p1[1]
	adr	x1,sk_shell0
	bl	sk_split
	add	x2,x21,x25,lsl 1
	strh	w0,[x2]
	strh	w1,[x2,2]
	mov	w0,w26
	adr	x1,sk_shell0
	bl	sk_split
	add	x2,x21,x25,lsl 1
	strh	w0,[x2,4]
	strh	w1,[x2,6]
	mov	w0,w24
	adr	x1,sk_shell1
	bl	sk_split
	mov	w26,w1
	adr	x1,sk_shell0
	bl	sk_split
	add	x2,x21,x25,lsl 1
	strh	w0,[x2,8]
	strh	w1,[x2,10]
	mov	w0,w26
	adr	x1,sk_shell0
	bl	sk_split
	add	x2,x21,x25,lsl 1
	strh	w0,[x2,12]
	strh	w1,[x2,14]
	cbnz	x25,.out
	mov	x25,8
	mov	w22,w23
	b	.h
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x1 = impulsions (16 bits), w2 = type de signal, w3 = decalage, w4 =
; echantillons (silk_decode_pulses)
sk_pulses:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,176			; sommes (20 x 4), decalages (20 x 4), icdf (2)
	mov	x21,x1
	mov	w22,w2
	mov	w23,w3
	mov	w24,w4
	str	x23,[sp,160]
	str	x24,[sp,168]
	lsr	w0,w22,1			; niveau de debit
	mov	w1,9
	mul	w0,w0,w1
	adr	x1,sk_rate
	add	x1,x1,x0
	mov	x0,x20
	mov	w2,8
	bl	ec_icdf
	mov	w26,w0
	lsr	w25,w24,4			; blocs de 16
	lsl	w0,w25,4
	cmp	w0,w24
	cinc	w25,w25,lo
	mov	w27,0
.s:	cmp	w27,w25
	b.hs	.sd
	mov	w28,0				; bits faibles
	mov	w0,18
	mul	w0,w0,w26
	adr	x1,sk_ppb
	add	x1,x1,x0
	mov	x0,x20
	mov	w2,8
	bl	ec_icdf
.sl:	cmp	w0,17
	b.ne	.sx
	add	w28,w28,1
	adr	x1,sk_ppb+9*18
	cmp	w28,10
	cinc	x1,x1,eq
	mov	x0,x20
	mov	w2,8
	bl	ec_icdf
	b	.sl
.sx:	str	w0,[sp,x27,lsl 2]
	add	x1,sp,80
	str	w28,[x1,x27,lsl 2]
	add	w27,w27,1
	b	.s
.sd:	mov	w27,0				; coquilles
.c:	cmp	w27,w25
	b.hs	.cd
	add	x1,x21,x27,lsl 5
	ldr	w2,[sp,x27,lsl 2]
	cbz	w2,.cz
	bl	sk_shell
	b	.cn
.cz:	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
.cn:	add	w27,w27,1
	b	.c
.cd:	mov	w27,0				; bits faibles
.l:	cmp	w27,w25
	b.hs	.ld
	add	x0,sp,80
	ldr	w28,[x0,x27,lsl 2]
	cbz	w28,.ln
	mov	w26,0				; k
.lk:	add	x0,x27,x27
	lsl	x0,x0,3
	add	x0,x0,x26
	ldrsh	w24,[x21,x0,lsl 1]		; (w24 : plus utile)
	mov	w23,0
.lj:	cmp	w23,w28
	b.hs	.ljd
	skicdf	sk_lsb
	add	w24,w0,w24,lsl 1
	add	w23,w23,1
	b	.lj
.ljd:	add	x0,x27,x27
	lsl	x0,x0,3
	add	x0,x0,x26
	strh	w24,[x21,x0,lsl 1]
	add	w26,w26,1
	cmp	w26,16
	b.lo	.lk
	ldr	w0,[sp,x27,lsl 2]
	orr	w0,w0,w28,lsl 5
	str	w0,[sp,x27,lsl 2]
.ln:	add	w27,w27,1
	b	.l
.ld:	; signes
	ldr	w23,[sp,160]			; decalage
	ldr	w24,[sp,168]			; echantillons
	add	w0,w23,w22,lsl 1
	mov	w1,7
	mul	w26,w0,w1			; 7 (decalage + 2 type)
	add	w25,w24,8
	lsr	w25,w25,4
	mov	w27,0
.g:	cmp	w27,w25
	b.hs	.out
	ldr	w28,[sp,x27,lsl 2]
	cbz	w28,.gn
	and	w0,w28,31
	mov	w1,6
	cmp	w0,w1
	csel	w0,w0,w1,lo
	add	w0,w0,w26
	adr	x1,sk_sign
	ldrb	w0,[x1,w0,uxtw]
	strb	w0,[sp,172]
	strb	wzr,[sp,173]
	mov	w23,0
.gj:	lsl	x0,x27,4
	add	x0,x0,x23
	ldrsh	w24,[x21,x0,lsl 1]
	cmp	w24,0
	b.le	.gk
	mov	x0,x20
	add	x1,sp,172
	mov	w2,8
	bl	ec_icdf
	lsl	w0,w0,1
	sub	w0,w0,1
	mul	w24,w24,w0
	lsl	x0,x27,4
	add	x0,x0,x23
	strh	w24,[x21,x0,lsl 1]
.gk:	add	w23,w23,1
	cmp	w23,16
	b.lo	.gj
.gn:	add	w27,w27,1
	b	.g
.out:	add	sp,sp,176
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Parametres (silk_decode_parameters)
; ---------------------------------------------------------------------------

; x0 = canal, w1 = codage
sk_params:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,64			; NLSF (16 x 16 bits), NLSF0
	mov	x21,x0
	mov	w22,w1
	; gains (silk_gains_dequant)
	ldr	w23,[x21,SKC_LASTGAIN]
	mov	w24,0
.g:	ldr	w0,[x21,SKC_NBSUB]
	cmp	w24,w0
	b.hs	.gd
	add	x0,x21,SKC_GIDX
	ldrsb	w0,[x0,x24]
	cbnz	w24,.gdl
	cmp	w22,2
	b.eq	.gdl
	sub	w1,w23,16
	cmp	w0,w1
	csel	w23,w0,w1,gt
	b	.gl
.gdl:	sub	w0,w0,4				; ind + MIN_DELTA_GAIN_QUANT
	add	w1,w23,8			; 2 MAX_DELTA - N_LEVELS + prev
	cmp	w0,w1
	b.le	.g1
	lsl	w2,w0,1
	sub	w2,w2,w1
	add	w23,w23,w2
	b	.gl
.g1:	add	w23,w23,w0
.gl:	cmp	w23,0
	csel	w23,w23,wzr,gt
	mov	w0,63
	cmp	w23,w0
	csel	w23,w23,w0,lt
	movl	w0,1907825			; INV_SCALE_Q16
	sxth	w1,w23
	smull	x0,w0,w1
	asr	x0,x0,16
	add	w0,w0,2090			; OFFSET
	mov	w1,3967
	cmp	w0,w1
	csel	w0,w0,w1,lt
	bl	sk_log2lin
	add	x1,x21,SKC_GAINS
	str	w0,[x1,x24,lsl 2]
	add	w24,w24,1
	b	.g
.gd:	str	w23,[x21,SKC_LASTGAIN]
	; NLSF -> LPC (deux moities de trame)
	mov	x0,x21
	mov	x1,sp
	bl	sk_nlsfdec
	mov	x0,sp
	ldr	w1,[x21,SKC_ORDER]
	add	x2,x21,SKC_A+32
	bl	sk_nlsf2a
	ldr	w0,[x21,SKC_FIRST]
	cmp	w0,1
	b.ne	.ni
	mov	w0,4
	str	w0,[x21,SKC_INTERP]
.ni:	ldr	w23,[x21,SKC_INTERP]
	cmp	w23,4
	b.hs	.cpa
	mov	x24,0
.ip:	ldr	w0,[x21,SKC_ORDER]
	cmp	w24,w0
	b.hs	.ipd
	add	x0,x21,SKC_PREVNLSF
	ldrsh	w1,[x0,x24,lsl 1]
	ldrsh	w2,[sp,x24,lsl 1]
	sub	w2,w2,w1
	mul	w2,w2,w23
	add	w1,w1,w2,asr 2
	add	x0,sp,32
	strh	w1,[x0,x24,lsl 1]
	add	x24,x24,1
	b	.ip
.ipd:	add	x0,sp,32
	ldr	w1,[x21,SKC_ORDER]
	add	x2,x21,SKC_A
	bl	sk_nlsf2a
	b	.pn
.cpa:	add	x0,x21,SKC_A
	add	x1,x21,SKC_A+32
	mov	x2,32
	bl	ak_memmove
.pn:	add	x0,x21,SKC_PREVNLSF
	mov	x1,sp
	mov	x2,32
	bl	ak_memmove
	ldr	w0,[x21,SKC_TYPE]
	cmp	w0,2
	b.ne	.unv
	; hauteurs (silk_decode_pitch)
	ldr	w0,[x21,SKC_FS]
	ldr	w1,[x21,SKC_NBSUB]
	cmp	w0,8
	b.ne	.p16
	adr	x2,sk_lags2
	mov	w3,11
	cmp	w1,4
	b.eq	.pc
	adr	x2,sk_lags2_10
	mov	w3,3
	b	.pc
.p16:	adr	x2,sk_lags3
	mov	w3,34
	cmp	w1,4
	b.eq	.pc
	adr	x2,sk_lags3_10
	mov	w3,12
.pc:	lsl	w4,w0,1				; min_lag
	mov	w5,18
	mul	w5,w5,w0			; max_lag
	ldr	w6,[x21,SKC_LAGIDX]
	add	w6,w6,w4			; lag
	ldr	w7,[x21,SKC_CONTIDX]
	mov	x8,0
.pk:	cmp	w8,w1
	b.hs	.pkd
	madd	w9,w8,w3,w7
	ldrsb	w9,[x2,w9,uxtw]
	add	w9,w9,w6
	cmp	w9,w4
	csel	w9,w9,w4,gt
	cmp	w9,w5
	csel	w9,w9,w5,lt
	add	x10,x21,SKC_PITCHL
	str	w9,[x10,x8,lsl 2]
	add	x8,x8,1
	b	.pk
.pkd:	; filtres LTP (Q7 -> Q14)
	ldr	w0,[x21,SKC_PERIDX]
	adr	x2,sk_ltpvq0
	cmp	w0,1
	b.lo	.vq
	adr	x2,sk_ltpvq1
	b.eq	.vq
	adr	x2,sk_ltpvq2
.vq:	mov	x8,0
.vk:	cmp	w8,w1
	b.hs	.vkd
	add	x3,x21,SKC_LTPIDX
	ldrb	w3,[x3,x8]
	mov	w4,5
	mul	w3,w3,w4
	mov	x5,0
.vi:	add	w6,w3,w5
	ldrsb	w6,[x2,w6,uxtw]
	lsl	w6,w6,7
	madd	w7,w8,w4,w5
	add	x9,x21,SKC_LTPC
	strh	w6,[x9,w7,uxtw 1]
	add	x5,x5,1
	cmp	x5,5
	b.lo	.vi
	add	x8,x8,1
	b	.vk
.vkd:	ldr	w0,[x21,SKC_LTPSCIDX]
	adr	x1,sk_ltpscales
	ldrsh	w0,[x1,w0,uxtw 1]
	str	w0,[x21,SKC_LTPSCALE]
	b	.out
.unv:	add	x0,x21,SKC_PITCHL
	stp	xzr,xzr,[x0]
	add	x0,x21,SKC_LTPC
	mov	x1,40
	bl	ak_memzero
	str	wzr,[x21,SKC_PERIDX]
	str	wzr,[x21,SKC_LTPSCALE]
.out:	add	sp,sp,64
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = canal, x1 = NLSF (sortie, 16 bits, Q15) (silk_NLSF_decode)
sk_nlsfdec:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,64			; pred (16 octets), res (16 x 16 bits)
	mov	x21,x0
	mov	x22,x1
	ldr	w23,[x21,SKC_ORDER]
	ldrb	w24,[x21,SKC_NLSFIDX]		; premier etage
	ldr	w0,[x21,SKC_WB]
	cbz	w0,.nb
	adr	x25,sk_sel_wb
	adr	x26,sk_pred_wb
	adr	x27,sk_cb1_wb
	adr	x28,sk_wght_wb
	mov	w9,9830				; pas (Q16)
	b	.tb
.nb:	adr	x25,sk_sel_nb
	adr	x26,sk_pred_nb
	adr	x27,sk_cb1_nb
	adr	x28,sk_wght_nb
	mov	w9,11796
.tb:	; predicteurs (silk_NLSF_unpack)
	mul	w0,w24,w23
	lsr	w0,w0,1
	add	x25,x25,x0
	sub	w10,w23,1			; order - 1
	mov	x1,0
.u:	cmp	w1,w23
	b.hs	.ud
	lsr	w2,w1,1
	ldrb	w2,[x25,x2]
	and	w3,w2,1
	madd	w3,w3,w10,w1
	ldrb	w3,[x26,x3]
	strb	w3,[sp,x1]
	ubfx	w3,w2,4,1
	madd	w3,w3,w10,w1
	add	w3,w3,1
	ldrb	w3,[x26,x3]
	add	x4,x1,1
	strb	w3,[sp,x4]
	add	x1,x1,2
	b	.u
.ud:	; residus (silk_NLSF_residual_dequant)
	mov	w0,0				; out_Q10
	sub	w1,w23,1
.r:	cmp	w1,0
	b.lt	.rd
	ldrb	w2,[sp,w1,uxtw]
	sxth	w3,w0
	mul	w3,w3,w2
	asr	w3,w3,8				; pred_Q10
	add	x4,x21,SKC_NLSFIDX+1
	ldrsb	w0,[x4,w1,uxtw]
	lsl	w0,w0,10
	cmp	w0,0
	b.le	.r1
	sub	w0,w0,102
	b	.r2
.r1:	b.eq	.r2
	add	w0,w0,102
.r2:	sxth	w4,w9				; SMLAWB(pred, out, pas)
	smull	x4,w0,w4
	asr	x4,x4,16
	add	w0,w3,w4
	add	x4,sp,16
	strh	w0,[x4,w1,uxtw 1]
	sub	w1,w1,1
	b	.r
.rd:	; NLSF = res << 14 / poids + premier etage << 7
	mul	w0,w24,w23
	add	x27,x27,x0
	add	x28,x28,x0,lsl 1
	mov	x1,0
.n:	cmp	w1,w23
	b.hs	.nd
	add	x2,sp,16
	ldrsh	w2,[x2,x1,lsl 1]
	lsl	w2,w2,14
	ldrsh	w3,[x28,x1,lsl 1]
	sdiv	w2,w2,w3
	ldrb	w3,[x27,x1]
	add	w2,w2,w3,lsl 7
	cmp	w2,0
	csel	w2,w2,wzr,gt
	mov	w3,32767
	cmp	w2,w3
	csel	w2,w2,w3,lt
	strh	w2,[x22,x1,lsl 1]
	add	x1,x1,1
	b	.n
.nd:	ldr	w0,[x21,SKC_WB]
	adr	x1,sk_dmin_nb
	adr	x2,sk_dmin_wb
	cmp	w0,0
	csel	x1,x1,x2,eq
	mov	x0,x22
	mov	w2,w23
	bl	sk_stabilize
	add	sp,sp,64
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = NLSF (16 bits), x1 = ecarts minimaux (L + 1), w2 = L : ecarte les
; NLSF (silk_NLSF_stabilize). Ne touche que x0-x15.
sk_stabilize:
	mov	w15,20				; essais
.lp:	ldrsh	w3,[x0]
	ldrsh	w4,[x1]
	sub	w5,w3,w4			; min_diff
	mov	w6,0				; I
	mov	w7,1
.m:	cmp	w7,w2
	b.hs	.md
	ldrsh	w3,[x0,w7,uxtw 1]
	sub	w8,w7,1
	ldrsh	w4,[x0,w8,uxtw 1]
	ldrsh	w8,[x1,w7,uxtw 1]
	add	w4,w4,w8
	sub	w3,w3,w4
	cmp	w3,w5
	csel	w5,w3,w5,lt
	csel	w6,w7,w6,lt
	add	w7,w7,1
	b	.m
.md:	sub	w8,w2,1
	ldrsh	w3,[x0,w8,uxtw 1]
	ldrsh	w4,[x1,w2,uxtw 1]
	add	w3,w3,w4
	mov	w4,32768
	sub	w3,w4,w3
	cmp	w3,w5
	csel	w5,w3,w5,lt
	csel	w6,w2,w6,lt
	cmp	w5,0
	b.ge	.ret
	cbnz	w6,.n0
	ldrh	w3,[x1]				; premier
	strh	w3,[x0]
	b	.nx
.n0:	cmp	w6,w2
	b.ne	.mid
	ldrsh	w3,[x1,w2,uxtw 1]		; dernier
	mov	w4,32768
	sub	w3,w4,w3
	sub	w8,w2,1
	strh	w3,[x0,w8,uxtw 1]
	b	.nx
.mid:	mov	w9,0				; min_center
	mov	w7,0
.mc:	cmp	w7,w6
	b.hs	.mcd
	ldrsh	w3,[x1,w7,uxtw 1]
	add	w9,w9,w3
	add	w7,w7,1
	b	.mc
.mcd:	ldrsh	w10,[x1,w6,uxtw 1]		; NDeltaMin[I]
	add	w9,w9,w10,asr 1
	mov	w11,32768			; max_center
	mov	w7,w2
.xc:	cmp	w7,w6
	b.ls	.xcd
	ldrsh	w3,[x1,w7,uxtw 1]
	sub	w11,w11,w3
	sub	w7,w7,1
	b	.xc
.xcd:	sub	w11,w11,w10,asr 1
	sub	w8,w6,1
	ldrsh	w3,[x0,w8,uxtw 1]
	ldrsh	w4,[x0,w6,uxtw 1]
	add	w3,w3,w4
	asr	w4,w3,1				; RSHIFT_ROUND(., 1)
	and	w3,w3,1
	add	w3,w3,w4
	cmp	w9,w11				; silk_LIMIT_32(c, min, max)
	b.gt	.lmr
	cmp	w3,w9
	csel	w3,w9,w3,lt
	cmp	w3,w11
	csel	w3,w11,w3,gt
	b	.lmd
.lmr:	cmp	w3,w9
	csel	w3,w9,w3,gt
	cmp	w3,w11
	csel	w3,w11,w3,lt
.lmd:	sxth	w3,w3
	sub	w3,w3,w10,asr 1
	strh	w3,[x0,w8,uxtw 1]
	sxth	w3,w3
	add	w3,w3,w10
	strh	w3,[x0,w6,uxtw 1]
.nx:	subs	w15,w15,1
	b.ne	.lp
	; repli : tri par insertion, puis ecarts imposes
	mov	w7,1
.is:	cmp	w7,w2
	b.hs	.isd
	ldrsh	w3,[x0,w7,uxtw 1]
	sub	w8,w7,1
.is1:	cmp	w8,0
	b.lt	.is2
	ldrsh	w4,[x0,w8,uxtw 1]
	cmp	w3,w4
	b.ge	.is2
	add	w9,w8,1
	strh	w4,[x0,w9,uxtw 1]
	sub	w8,w8,1
	b	.is1
.is2:	add	w9,w8,1
	strh	w3,[x0,w9,uxtw 1]
	add	w7,w7,1
	b	.is
.isd:	ldrsh	w3,[x0]
	ldrsh	w4,[x1]
	cmp	w3,w4
	csel	w3,w3,w4,gt
	strh	w3,[x0]
	mov	w7,1
.f1:	cmp	w7,w2
	b.hs	.f1d
	sub	w8,w7,1
	ldrsh	w3,[x0,w8,uxtw 1]
	ldrsh	w4,[x1,w7,uxtw 1]
	add	w3,w3,w4
	mov	w4,32767			; silk_ADD_SAT16
	cmp	w3,w4
	csel	w3,w3,w4,lt
	ldrsh	w4,[x0,w7,uxtw 1]
	cmp	w4,w3
	csel	w3,w4,w3,gt
	strh	w3,[x0,w7,uxtw 1]
	add	w7,w7,1
	b	.f1
.f1d:	sub	w8,w2,1
	ldrsh	w3,[x0,w8,uxtw 1]
	ldrsh	w4,[x1,w2,uxtw 1]
	mov	w5,32768
	sub	w4,w5,w4
	cmp	w3,w4
	csel	w3,w3,w4,lt
	strh	w3,[x0,w8,uxtw 1]
	sub	w7,w2,2
.f2:	cmp	w7,0
	b.lt	.ret
	add	w8,w7,1
	ldrsh	w3,[x0,w8,uxtw 1]
	ldrsh	w4,[x1,w8,uxtw 1]
	sub	w3,w3,w4
	ldrsh	w4,[x0,w7,uxtw 1]
	cmp	w4,w3
	csel	w3,w4,w3,lt
	strh	w3,[x0,w7,uxtw 1]
	sub	w7,w7,1
	b	.f2
.ret:	ret

; x0 = NLSF (Q15), w1 = ordre (10, 16), x2 = A (sortie, Q12, 16 bits)
; (silk_NLSF2A : polynomes P et Q, ajustement, stabilisation)
sk_nlsf2a:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,224			; cos (16 x 4), P (9 x 4), Q (9 x 4), a32 (16 x 4)
	mov	x21,x0
	mov	w22,w1
	mov	x23,x2
	adr	x3,sk_ord10
	adr	x4,sk_ord16
	cmp	w22,16
	csel	x3,x4,x3,eq
	adr	x4,sk_cos
	mov	x5,0
.c:	cmp	w5,w22
	b.hs	.cd
	ldrsh	w6,[x21,x5,lsl 1]
	asr	w7,w6,8				; f_int
	sub	w6,w6,w7,lsl 8			; f_frac
	ldrsh	w8,[x4,w7,uxtw 1]		; cos_val
	add	w7,w7,1
	ldrsh	w9,[x4,w7,uxtw 1]
	sub	w9,w9,w8			; delta
	lsl	w8,w8,8
	madd	w8,w9,w6,w8
	asr	w8,w8,3				; RSHIFT_ROUND(., 4)
	add	w8,w8,1
	asr	w8,w8,1
	ldrb	w6,[x3,x5]
	str	w8,[sp,w6,uxtw 2]
	add	x5,x5,1
	b	.c
.cd:	lsr	w24,w22,1			; dd
	add	x0,sp,64			; P
	mov	x1,sp
	mov	w2,w24
	bl	sk_poly
	add	x0,sp,112			; Q
	add	x1,sp,4
	mov	w2,w24
	bl	sk_poly
	mov	x5,0
.pq:	cmp	w5,w24
	b.hs	.pqd
	add	x6,sp,64
	ldr	w7,[x6,x5,lsl 2]
	add	x8,x5,1
	ldr	w9,[x6,x8,lsl 2]
	add	w7,w7,w9			; Ptmp
	add	x6,sp,112
	ldr	w9,[x6,x5,lsl 2]
	ldr	w10,[x6,x8,lsl 2]
	sub	w9,w10,w9			; Qtmp
	add	x6,sp,160
	neg	w10,w9
	sub	w10,w10,w7
	str	w10,[x6,x5,lsl 2]		; a32[k] = -Qtmp - Ptmp
	sub	w10,w9,w7
	sub	w11,w22,w5
	sub	w11,w11,1
	str	w10,[x6,w11,uxtw 2]		; a32[d-k-1] = Qtmp - Ptmp
	add	x5,x5,1
	b	.pq
.pqd:	add	x0,sp,160
	mov	w1,w22
	mov	x2,x23
	bl	sk_lpcfit
	mov	w25,0
.st:	mov	x0,x23
	mov	w1,w22
	bl	sk_invgain
	cbnz	w0,.out
	cmp	w25,16
	b.hs	.out
	add	x0,sp,160			; expansion de bande, puis Q12
	mov	w1,w22
	mov	w2,2
	lsl	w2,w2,w25
	mov	w3,65536
	sub	w2,w3,w2
	bl	sk_bwexp32
	add	x6,sp,160
	mov	x5,0
.q12:	ldr	w7,[x6,x5,lsl 2]
	asr	w7,w7,4
	add	w7,w7,1
	asr	w7,w7,1
	strh	w7,[x23,x5,lsl 1]
	add	x5,x5,1
	cmp	w5,w22
	b.lo	.q12
	add	w25,w25,1
	b	.st
.out:	add	sp,sp,224
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = sortie (dd + 1 x 32 bits), x1 = 2 cos(LSF) (pas de 2 mots), w2 = dd
; (silk_NLSF2A_find_poly). Ne touche que x0-x9.
sk_poly:
	mov	w3,65536
	str	w3,[x0]
	ldr	w3,[x1]
	neg	w3,w3
	str	w3,[x0,4]
	mov	w4,1				; k
.k:	cmp	w4,w2
	b.hs	.ret
	add	x5,x1,w4,uxtw 3		; f = cLSF[2k] (8 octets par k)
	ldr	w5,[x5]
	sub	w6,w4,1
	ldr	w6,[x0,w6,uxtw 2]		; out[k-1]
	ldr	w7,[x0,w4,uxtw 2]		; out[k]
	smull	x7,w5,w7
	asr	x7,x7,15
	add	x7,x7,1
	asr	x7,x7,1
	lsl	w6,w6,1
	sub	w6,w6,w7
	add	w8,w4,1
	str	w6,[x0,w8,uxtw 2]
	mov	w8,w4				; n = k .. 2
.n:	cmp	w8,1
	b.ls	.nd
	sub	w9,w8,1
	ldr	w9,[x0,w9,uxtw 2]
	smull	x9,w5,w9
	asr	x9,x9,15
	add	x9,x9,1
	asr	x9,x9,1
	sub	w6,w8,2
	ldr	w6,[x0,w6,uxtw 2]
	sub	w6,w6,w9
	ldr	w7,[x0,w8,uxtw 2]
	add	w7,w7,w6
	str	w7,[x0,w8,uxtw 2]
	sub	w8,w8,1
	b	.n
.nd:	ldr	w6,[x0,4]
	sub	w6,w6,w5
	str	w6,[x0,4]
	add	w4,w4,1
	b	.k
.ret:	ret

; x0 = a32 (Q17), w1 = d, x2 = A (sortie, Q12) (silk_LPC_fit)
sk_lpcfit:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	w22,w1
	mov	x23,x2
	mov	w24,0
.it:	cmp	w24,10
	b.hs	.clip
	mov	w3,0				; maxabs
	mov	w4,0				; idx
	mov	x5,0
.mx:	ldr	w6,[x21,x5,lsl 2]
	cmp	w6,0
	cneg	w6,w6,lt
	cmp	w6,w3
	csel	w4,w5,w4,gt
	csel	w3,w6,w3,gt
	add	x5,x5,1
	cmp	w5,w22
	b.lo	.mx
	asr	w3,w3,4				; RSHIFT_ROUND(maxabs, 5)
	add	w3,w3,1
	asr	w3,w3,1
	mov	w5,32767
	cmp	w3,w5
	b.le	.fit
	movl	w6,163838
	cmp	w3,w6
	csel	w3,w3,w6,lt
	sub	w6,w3,w5
	lsl	w6,w6,14
	add	w7,w4,1
	mul	w7,w3,w7
	asr	w7,w7,2
	sdiv	w6,w6,w7
	movl	w7,65470			; 0,999 en Q16
	sub	w2,w7,w6
	mov	x0,x21
	mov	w1,w22
	bl	sk_bwexp32
	add	w24,w24,1
	b	.it
.fit:	mov	x5,0
.f:	ldr	w6,[x21,x5,lsl 2]
	asr	w6,w6,4
	add	w6,w6,1
	asr	w6,w6,1
	strh	w6,[x23,x5,lsl 1]
	add	x5,x5,1
	cmp	w5,w22
	b.lo	.f
	b	.out
.clip:	mov	x5,0
.cl:	ldr	w6,[x21,x5,lsl 2]
	asr	w6,w6,4
	add	w6,w6,1
	asr	w6,w6,1
	mov	w7,32767
	cmp	w6,w7
	csel	w6,w6,w7,lt
	mov	w7,-32768
	cmp	w6,w7
	csel	w6,w6,w7,gt
	strh	w6,[x23,x5,lsl 1]
	lsl	w6,w6,5
	str	w6,[x21,x5,lsl 2]
	add	x5,x5,1
	cmp	w5,w22
	b.lo	.cl
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = a32, w1 = d, w2 = chirp (Q16) (silk_bwexpander_32). Ne touche que
; x0-x7.
sk_bwexp32:
	mov	w3,65536
	sub	w3,w2,w3			; chirp - 1
	sub	w4,w1,1
	mov	x5,0
.l:	cmp	w5,w4
	b.hs	.last
	ldr	w6,[x0,x5,lsl 2]
	smull	x6,w2,w6
	asr	x6,x6,16
	str	w6,[x0,x5,lsl 2]
	mul	w6,w2,w3			; chirp += RSHIFT_ROUND(chirp (chirp - 1), 16)
	asr	w6,w6,15
	add	w6,w6,1
	asr	w6,w6,1
	add	w2,w2,w6
	add	x5,x5,1
	b	.l
.last:	ldr	w6,[x0,x5,lsl 2]
	smull	x6,w2,w6
	asr	x6,x6,16
	str	w6,[x0,x5,lsl 2]
	ret

; x0 = A (Q12), w1 = ordre -> w0 = gain de prediction inverse (Q30), 0 :
; instable (silk_LPC_inverse_pred_gain)
sk_invgain:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,64
	mov	w22,w1
	mov	w2,0				; DC
	mov	x3,0
.q:	ldrsh	w4,[x0,x3,lsl 1]
	add	w2,w2,w4
	lsl	w4,w4,12
	str	w4,[sp,x3,lsl 2]
	add	x3,x3,1
	cmp	w3,w22
	b.lo	.q
	cmp	w2,4096
	b.ge	.zero
	mov	w23,0x40000000			; invGain_Q30
	movl	w24,16773022			; A_LIMIT (Q24)
	sub	w25,w22,1			; k
.k:	ldr	w0,[sp,x25,lsl 2]
	cmp	w0,w24
	b.gt	.zero
	cmn	w0,w24
	b.lt	.zero
	lsl	w26,w0,7
	neg	w26,w26				; rc_Q31
	smull	x0,w26,w26
	asr	x0,x0,32
	mov	w1,0x40000000
	sub	w27,w1,w0			; rc_mult1_Q30
	smull	x0,w23,w27
	asr	x0,x0,32
	lsl	w23,w0,2
	movl	w0,107374
	cmp	w23,w0
	b.lt	.zero
	cbz	w25,.done
	clz	w0,w27
	mov	w1,32
	sub	w28,w1,w0			; mult2Q
	mov	w0,w27
	add	w1,w28,30
	bl	sk_inv32
	mov	w21,w0				; rc_mult2
	add	w0,w25,1
	lsr	w9,w0,1				; n < (k + 1) / 2
	mov	x8,0
.n:	cmp	w8,w9
	b.hs	.nd
	ldr	w10,[sp,x8,lsl 2]		; tmp1
	sub	w11,w25,w8
	sub	w11,w11,1
	ldr	w12,[sp,w11,uxtw 2]		; tmp2
	mov	w0,w10
	mov	w1,w12
	bl	.upd
	cbz	x2,.zero
	str	w0,[sp,x8,lsl 2]
	mov	w0,w12
	mov	w1,w10
	bl	.upd
	cbz	x2,.zero
	str	w0,[sp,w11,uxtw 2]
	add	x8,x8,1
	b	.n
.nd:	sub	w25,w25,1
	b	.k
.done:	mov	w0,w23
	b	.out
.zero:	mov	w0,0
.out:	add	sp,sp,64
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue
; w0 = a, w1 = b -> w0 = (SUB_SAT32(a, ROUND(b rc, 31)) rc_mult2) >> mult2Q
; arrondi, x2 = 0 si hors de 32 bits ; ne touche que x0-x7
.upd:	smull	x2,w1,w26
	asr	x2,x2,30
	add	x2,x2,1
	asr	x2,x2,1				; MUL32_FRAC_Q(b, rc, 31)
	sxtw	x3,w0
	sub	x3,x3,x2			; silk_SUB_SAT32
	movl	x4,0x7FFFFFFF
	cmp	x3,x4
	csel	x3,x4,x3,gt
	movn	x4,0x7FFF,lsl 16		; -2^31
	cmp	x3,x4
	csel	x3,x4,x3,lt
	sxtw	x5,w21
	mul	x3,x3,x5
	sub	w5,w28,1
	asr	x3,x3,x5
	add	x3,x3,1
	asr	x3,x3,1				; RSHIFT_ROUND64(., mult2Q)
	sxtw	x4,w3
	cmp	x4,x3
	cset	x2,eq
	mov	w0,w3
	ret

; ---------------------------------------------------------------------------
;  Synthese (silk_decode_core)
; ---------------------------------------------------------------------------

; x0 = canal, x1 = impulsions, x2 = sortie (16 bits)
sk_core:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,96
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
	; excitation
	ldr	w0,[x21,SKC_TYPE]
	lsr	w0,w0,1
	ldr	w1,[x21,SKC_QOFF]
	add	w0,w1,w0,lsl 1
	adr	x1,sk_qoff
	ldrsh	w9,[x1,w0,uxtw 1]
	lsl	w9,w9,4				; offset_Q10 << 4
	ldr	w10,[x21,SKC_SEED]
	movl	w11,196314165
	movl	w12,907633515
	skadd	x13,SK_EXC
	ldr	w14,[x21,SKC_FL]
	mov	x0,0
.ex:	cmp	w0,w14
	b.hs	.exd
	madd	w10,w10,w11,w12			; silk_RAND
	ldrsh	w1,[x22,x0,lsl 1]
	lsl	w2,w1,14
	cmp	w2,0
	b.le	.e1
	sub	w2,w2,1280
	b	.e2
.e1:	b.eq	.e2
	add	w2,w2,1280
.e2:	add	w2,w2,w9
	cmp	w10,0
	cneg	w2,w2,lt
	str	w2,[x13,x0,lsl 2]
	add	w10,w10,w1
	add	x0,x0,1
	b	.ex
.exd:	; etat LPC : sLPC_Q14[16] puis la sous-trame
	skadd	x0,SK_SLPCW
	add	x1,x21,SKC_SLPC
	mov	x2,64
	bl	ak_memmove
	ldr	w0,[x21,SKC_LTPMEM]
	str	w0,[sp]				; sLTP_buf_idx
	str	xzr,[sp,8]			; k
	ldr	w0,[x21,SKC_INTERP]
	cmp	w0,4
	cset	w0,lo
	str	w0,[sp,16]			; NLSF_interpolation_flag
.sub:	ldr	w24,[sp,8]			; k
	ldr	w0,[x21,SKC_NBSUB]
	cmp	w24,w0
	b.hs	.end
	ldr	w25,[x21,SKC_SL]
	lsr	w0,w24,1			; A de la moitie
	add	x26,x21,SKC_A
	add	x26,x26,x0,lsl 5
	add	x0,x21,SKC_GAINS
	ldr	w27,[x0,x24,lsl 2]		; Gains_Q16[k]
	mov	w0,w27
	mov	w1,47
	bl	sk_inv32
	str	w0,[sp,24]			; inv_gain_Q31
	mov	w28,65536			; gain_adj_Q16
	ldr	w0,[x21,SKC_PREVGAIN]
	cmp	w0,w27
	b.eq	.ga
	mov	w1,w27
	mov	w2,16
	bl	sk_div32
	mov	w28,w0
	skadd	x1,SK_SLPCW
	mov	x2,0
.gs:	ldr	w3,[x1,x2,lsl 2]
	smull	x3,w28,w3
	asr	x3,x3,16
	str	w3,[x1,x2,lsl 2]
	add	x2,x2,1
	cmp	x2,16
	b.lo	.gs
.ga:	str	w27,[x21,SKC_PREVGAIN]
	ldr	w0,[x21,SKC_TYPE]
	cmp	w0,2
	b.ne	.lpc0
	; voise : reblanchiment ou mise a l'echelle de l'etat LTP
	add	x0,x21,SKC_PITCHL
	ldr	w0,[x0,x24,lsl 2]
	str	w0,[sp,32]			; lag
	cbz	w24,.rw
	cmp	w24,2
	b.ne	.nrw
	ldr	w1,[sp,16]
	cbz	w1,.nrw
.rw:	ldr	w1,[x21,SKC_LTPMEM]
	sub	w2,w1,w0
	ldr	w3,[x21,SKC_ORDER]
	sub	w2,w2,w3
	sub	w2,w2,2				; start_idx
	str	w2,[sp,40]
	cmp	w24,2
	b.ne	.rw1
	add	x0,x21,SKC_OUTBUF		; outBuf[ltp_mem] = xq[0 .. 2 sl)
	add	x0,x0,x1,lsl 1
	mov	x1,x23
	lsl	w2,w25,2
	bl	ak_memmove
.rw1:	ldr	w2,[sp,40]
	skadd	x0,SK_SLTP
	add	x0,x0,x2,lsl 1			; sortie : sLTP[start]
	mul	w3,w24,w25
	add	w3,w3,w2
	add	x1,x21,SKC_OUTBUF
	add	x1,x1,x3,lsl 1			; entree : outBuf[start + k sl]
	ldr	w3,[x21,SKC_LTPMEM]
	sub	w3,w3,w2			; longueur
	ldr	w4,[x21,SKC_ORDER]
	mov	x2,x26
	bl	sk_lpcana
	cbnz	w24,.rw2
	ldr	w0,[sp,24]			; echelle LTP (trame independante)
	ldr	w1,[x21,SKC_LTPSCALE]
	sxth	w1,w1
	smull	x0,w0,w1
	asr	x0,x0,16
	lsl	w0,w0,2
	str	w0,[sp,24]
.rw2:	ldr	w0,[sp,32]
	add	w0,w0,2				; lag + LTP_ORDER / 2
	ldr	w1,[sp,24]
	ldr	w2,[sp]				; sLTP_buf_idx
	skadd	x3,SK_SLTPQ
	skadd	x4,SK_SLTP
	ldr	w5,[x21,SKC_LTPMEM]
	mov	x6,0
.rw3:	cmp	w6,w0
	b.hs	.ltp
	sub	w7,w5,w6
	sub	w7,w7,1
	ldrsh	w7,[x4,w7,uxtw 1]
	smull	x7,w1,w7
	asr	x7,x7,16
	sub	w8,w2,w6
	sub	w8,w8,1
	str	w7,[x3,w8,uxtw 2]
	add	x6,x6,1
	b	.rw3
.nrw:	cmp	w28,65536
	b.eq	.ltp
	ldr	w0,[sp,32]
	add	w0,w0,2
	ldr	w2,[sp]
	skadd	x3,SK_SLTPQ
	mov	x6,0
.sc:	cmp	w6,w0
	b.hs	.ltp
	sub	w8,w2,w6
	sub	w8,w8,1
	ldr	w7,[x3,w8,uxtw 2]
	smull	x7,w28,w7
	asr	x7,x7,16
	str	w7,[x3,w8,uxtw 2]
	add	x6,x6,1
	b	.sc
.ltp:	; prediction a long terme -> res (dans SK_SLTPQ, a sLTP_buf_idx)
	ldr	w2,[sp]				; sLTP_buf_idx
	ldr	w0,[sp,32]			; lag
	sub	w1,w2,w0
	add	w1,w1,2				; pred_lag_ptr
	skadd	x3,SK_SLTPQ
	add	x9,x21,SKC_LTPC
	mov	w4,10
	mul	w4,w4,w24
	add	x9,x9,x4			; B_Q14[k 5]
	skadd	x13,SK_EXC
	mul	w4,w24,w25
	add	x13,x13,x4,lsl 2		; exc de la sous-trame
	add	x14,sp,48			; (res : SK_SLTPQ + 4 sbi apres coup)
	mov	x6,0
.lp:	cmp	w6,w25
	b.hs	.lpd
	mov	w7,2
	mov	x8,0
.lt:	sub	w10,w1,w8
	ldr	w10,[x3,w10,uxtw 2]
	ldrsh	w11,[x9,x8,lsl 1]
	smull	x10,w10,w11
	asr	x10,x10,16
	add	w7,w7,w10
	add	x8,x8,1
	cmp	x8,5
	b.lo	.lt
	add	w1,w1,1
	ldr	w10,[x13,x6,lsl 2]
	add	w10,w10,w7,lsl 1		; res = exc + pred << 1
	str	w10,[x13,x6,lsl 2]		; (res remplace exc : plus lu ensuite)
	lsl	w10,w10,1
	str	w10,[x3,w2,uxtw 2]
	add	w2,w2,1
	add	x6,x6,1
	b	.lp
.lpd:	str	w2,[sp]
.lpc0:	; prediction a court terme, gain
	skadd	x13,SK_EXC			; res (ou exc)
	mul	w4,w24,w25
	add	x13,x13,x4,lsl 2
	skadd	x14,SK_SLPCW		; sLPC
	ldr	w15,[x21,SKC_ORDER]
	asr	w16,w27,6			; Gain_Q10
	mul	w4,w24,w25
	add	x17,x23,x4,lsl 1		; xq de la sous-trame
	mov	x6,0
.sl:	cmp	w6,w25
	b.hs	.sld
	lsr	w7,w15,1			; LPC_pred_Q10
	add	x8,x14,x6,lsl 2
	add	x8,x8,64			; &sLPC[16 + i]
	mov	x9,0
.sk:	sub	x10,x8,4
	sub	x10,x10,x9,lsl 2
	ldr	w10,[x10]
	ldrsh	w11,[x26,x9,lsl 1]
	smull	x10,w10,w11
	asr	x10,x10,16
	add	w7,w7,w10
	add	x9,x9,1
	cmp	w9,w15
	b.lo	.sk
	ldr	w10,[x13,x6,lsl 2]		; ADD_SAT32(res, LSHIFT_SAT32(pred, 4))
	movl	w11,0xF8000000
	cmp	w7,w11
	csel	w7,w11,w7,lt
	movl	w11,0x07FFFFFF
	cmp	w7,w11
	csel	w7,w11,w7,gt
	lsl	w7,w7,4
	adds	w10,w10,w7
	b.vc	.sv
	movl	w10,0x7FFFFFFF
	cmp	w7,0
	b.gt	.sv
	movl	w10,0x80000000
.sv:	str	w10,[x8]
	smull	x10,w10,w16			; SMULWW(sLPC, Gain_Q10)
	asr	x10,x10,16
	asr	w10,w10,7			; RSHIFT_ROUND(., 8)
	add	w10,w10,1
	asr	w10,w10,1
	mov	w11,32767
	cmp	w10,w11
	csel	w10,w10,w11,lt
	mov	w11,-32768
	cmp	w10,w11
	csel	w10,w10,w11,gt
	strh	w10,[x17,x6,lsl 1]
	add	x6,x6,1
	b	.sl
.sld:	; etat LPC : les 16 derniers
	skadd	x0,SK_SLPCW
	add	x1,x0,x25,lsl 2
	mov	x2,64
	bl	ak_memmove
	add	w24,w24,1
	str	w24,[sp,8]
	b	.sub
.end:	add	x0,x21,SKC_SLPC
	skadd	x1,SK_SLPCW
	mov	x2,64
	bl	ak_memmove
	add	sp,sp,96
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = sortie (16 bits), x1 = entree (16 bits), x2 = B (Q12), w3 = longueur,
; w4 = ordre : filtre d'analyse LPC (silk_LPC_analysis_filter). Ne touche que
; x0-x9.
sk_lpcana:
	mov	x5,0
.z:	cmp	w5,w4
	b.hs	.zd
	strh	wzr,[x0,x5,lsl 1]
	add	x5,x5,1
	b	.z
.zd:	mov	x5,x4				; ix
.l:	cmp	w5,w3
	b.hs	.ret
	mov	w6,0
	mov	x7,0
.j:	sub	x8,x5,1
	sub	x8,x8,x7
	ldrsh	w8,[x1,x8,lsl 1]
	ldrsh	w9,[x2,x7,lsl 1]
	madd	w6,w8,w9,w6
	add	x7,x7,1
	cmp	w7,w4
	b.lo	.j
	ldrsh	w8,[x1,x5,lsl 1]
	lsl	w8,w8,12
	sub	w6,w8,w6
	asr	w6,w6,11			; RSHIFT_ROUND(., 12)
	add	w6,w6,1
	asr	w6,w6,1
	mov	w8,32767
	cmp	w6,w8
	csel	w6,w6,w8,lt
	mov	w8,-32768
	cmp	w6,w8
	csel	w6,w6,w8,gt
	strh	w6,[x0,x5,lsl 1]
	add	x5,x5,1
	b	.l
.ret:	ret

; ---------------------------------------------------------------------------
;  Stereo
; ---------------------------------------------------------------------------

; x1 = predicteurs (sortie, 2 x 32 bits, Q13) (silk_stereo_decode_pred)
sk_spred:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x21,x1
	skicdf	sk_pred_joint
	mov	w1,5
	udiv	w22,w0,w1			; ix[0][2]
	msub	w23,w22,w1,w0			; ix[1][2]
	skicdf	sk_unif3
	mov	w24,w0
	skicdf	sk_unif5
	mov	w25,w0
	skicdf	sk_unif3
	mov	w26,w0
	skicdf	sk_unif5
	mov	w5,w0
	; n = 0 : w24, w25, w22 ; n = 1 : w26, w5, w23
	add	w0,w24,w22,lsl 1
	add	w0,w0,w22			; ix[0][0] + 3 ix[0][2]
	mov	w1,w25
	bl	.dq
	str	w0,[x21]
	add	w0,w26,w23,lsl 1
	add	w0,w0,w23
	mov	w1,w5
	bl	.dq
	str	w0,[x21,4]
	ldr	w1,[x21]
	sub	w1,w1,w0
	str	w1,[x21]
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue
; w0 = indice, w1 = pas -> w0 = predicteur ; ne touche que x0-x4
.dq:	adr	x2,sk_pred_q13
	ldrsh	w3,[x2,w0,uxtw 1]		; low
	add	w0,w0,1
	ldrsh	w4,[x2,w0,uxtw 1]
	sub	w4,w4,w3
	mov	w2,6554				; 0,5 / 5 en Q16
	smull	x4,w4,w2
	asr	x4,x4,16			; step
	lsl	w1,w1,1
	add	w1,w1,1
	sxth	w4,w4
	madd	w0,w4,w1,w3
	ret

; x0 = milieu (x1, 2 + L x 16 bits), x1 = cote (x2), w2 = kHz, w3 = L :
; M/S -> gauche/droite (silk_stereo_MS_to_LR)
sk_ms2lr:
	ldr	w4,[x19,SK_SMID]		; retard de deux echantillons
	ldr	w5,[x19,SK_SSIDE]
	str	w4,[x0]
	str	w5,[x1]
	add	x4,x0,x3,lsl 1
	ldr	w4,[x4]
	str	w4,[x19,SK_SMID]
	add	x4,x1,x3,lsl 1
	ldr	w4,[x4]
	str	w4,[x19,SK_SSIDE]
	ldr	w4,[x19,SK_PREDPREV]		; pred0
	ldr	w5,[x19,SK_PREDPREV+4]		; pred1
	lsl	w6,w2,3				; 8 kHz : interpolation sur 8 ms
	mov	w7,65536
	sdiv	w7,w7,w6			; denom_Q16
	ldr	w8,[x19,SK_MS]
	sub	w9,w8,w4
	sxth	w9,w9
	sxth	w10,w7
	mul	w9,w9,w10
	asr	w9,w9,15
	add	w9,w9,1
	asr	w9,w9,1				; delta0
	ldr	w8,[x19,SK_MS+4]
	sub	w10,w8,w5
	sxth	w10,w10
	sxth	w11,w7
	mul	w10,w10,w11
	asr	w10,w10,15
	add	w10,w10,1
	asr	w10,w10,1			; delta1
	mov	x11,0				; n
.l:	cmp	w11,w3
	b.hs	.ld
	cmp	w11,w6
	b.hs	.fix
	add	w4,w4,w9
	add	w5,w5,w10
	b	.p
.fix:	ldr	w4,[x19,SK_MS]
	ldr	w5,[x19,SK_MS+4]
.p:	ldrsh	w12,[x0,x11,lsl 1]		; x1[n]
	add	x13,x11,2
	ldrsh	w13,[x0,x13,lsl 1]		; x1[n+2]
	add	w12,w12,w13
	add	x13,x11,1
	ldrsh	w14,[x0,x13,lsl 1]		; x1[n+1]
	add	w12,w12,w14,lsl 1
	lsl	w12,w12,9			; sum (Q11)
	ldrsh	w15,[x1,x13,lsl 1]		; x2[n+1]
	lsl	w15,w15,8
	sxth	w16,w4
	smull	x12,w12,w16
	asr	x12,x12,16
	add	w15,w15,w12
	lsl	w14,w14,11
	sxth	w16,w5
	smull	x14,w14,w16
	asr	x14,x14,16
	add	w15,w15,w14
	asr	w15,w15,7
	add	w15,w15,1
	asr	w15,w15,1
	mov	w16,32767
	cmp	w15,w16
	csel	w15,w15,w16,lt
	mov	w16,-32768
	cmp	w15,w16
	csel	w15,w15,w16,gt
	strh	w15,[x1,x13,lsl 1]
	add	x11,x11,1
	b	.l
.ld:	ldr	x4,[x19,SK_MS]
	str	x4,[x19,SK_PREDPREV]
	mov	x11,0				; G = M + S, D = M - S
.lr:	cmp	w11,w3
	b.hs	.ret
	add	x13,x11,1
	ldrsh	w12,[x0,x13,lsl 1]
	ldrsh	w14,[x1,x13,lsl 1]
	add	w15,w12,w14
	sub	w16,w12,w14
	mov	w17,32767
	cmp	w15,w17
	csel	w15,w15,w17,lt
	cmp	w16,w17
	csel	w16,w16,w17,lt
	mov	w17,-32768
	cmp	w15,w17
	csel	w15,w15,w17,gt
	cmp	w16,w17
	csel	w16,w16,w17,gt
	strh	w15,[x0,x13,lsl 1]
	strh	w16,[x1,x13,lsl 1]
	add	x11,x11,1
	b	.lr
.ret:	ret

; ---------------------------------------------------------------------------
;  Reechantillonnage vers 48 kHz (silk_resampler : IIR_FIR)
; ---------------------------------------------------------------------------

; x0 = reechantillonneur, w1 = frequence d'entree (kHz)
sk_rsinit:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	w22,w1
	mov	x1,RS_SIZE
	bl	ak_memzero
	str	w22,[x21,RS_FSIN]
	mov	w0,0				; retard (8, 12, 16 kHz -> 48 kHz)
	cmp	w22,12
	mov	w1,4
	csel	w0,w1,w0,eq
	cmp	w22,16
	mov	w1,7
	csel	w0,w1,w0,eq
	str	w0,[x21,RS_DELAY]
	mov	w0,10
	mul	w0,w0,w22
	str	w0,[x21,RS_BATCH]
	mov	w0,1000
	mul	w1,w22,w0			; Fs_in
	lsl	w2,w1,15
	movl	w3,48000
	udiv	w2,w2,w3
	lsl	w2,w2,2				; invRatio_Q16
	lsl	w1,w1,1
.i:	smull	x4,w2,w3
	asr	x4,x4,16
	cmp	w4,w1
	b.ge	.id
	add	w2,w2,1
	b	.i
.id:	str	w2,[x21,RS_INV]
	ldp	x21,x22,[sp],16
	epilogue

; x0 = reechantillonneur, x1 = sortie, x2 = entree, w3 = echantillons ->
; w0 = echantillons de sortie (silk_resampler)
sk_resample:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
	mov	w24,w3
	ldr	w25,[x21,RS_FSIN]
	ldr	w26,[x21,RS_DELAY]
	add	x0,x21,RS_DBUF			; delayBuf[delay ..] = in[0 .. fs - delay)
	add	x0,x0,x26,lsl 1
	mov	x1,x23
	sub	w2,w25,w26
	lsl	w2,w2,1
	bl	ak_memmove
	mov	x0,x21
	mov	x1,x22
	add	x2,x21,RS_DBUF
	mov	w3,w25
	bl	sk_iirfir
	sub	w2,w25,w26
	add	x2,x23,x2,lsl 1
	sub	w3,w24,w25
	mov	x1,x0
	mov	x0,x21
	bl	sk_iirfir
	sub	x22,x0,x22
	add	x0,x21,RS_DBUF			; delayBuf = in[len - delay ..]
	sub	w1,w24,w26
	add	x1,x23,x1,lsl 1
	lsl	w2,w26,1
	bl	ak_memmove
	lsr	x0,x22,1
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = reechantillonneur, x1 = sortie, x2 = entree, w3 = echantillons ->
; x0 = fin de la sortie (silk_resampler_private_IIR_FIR)
sk_iirfir:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x21,x0
	mov	x22,x1
	mov	x23,x2
	mov	w24,w3
	skadd	x25,SK_RSBUF
	ldp	x0,x1,[x21,RS_FIR]		; buf[0 .. 8) = sFIR
	stp	x0,x1,[x25]
.b:	ldr	w26,[x21,RS_BATCH]
	cmp	w24,w26
	csel	w26,w24,w26,lo			; n
	add	x0,x21,RS_IIR
	add	x1,x25,16
	mov	x2,x23
	mov	w3,w26
	bl	sk_up2
	lsl	w0,w26,17			; max_index_Q16
	ldr	w1,[x21,RS_INV]
	adr	x2,sk_fir12
	mov	w3,0				; index_Q16
.f:	cmp	w3,w0
	b.hs	.fd
	and	w4,w3,0xFFFF
	mov	w5,12
	mul	w4,w4,w5
	lsr	w4,w4,16			; table_index
	lsr	w5,w3,16
	add	x5,x25,x5,lsl 1			; buf_ptr
	add	x6,x2,x4,lsl 3			; FIR[t]
	mov	w7,11
	sub	w7,w7,w4
	add	x7,x2,x7,lsl 3			; FIR[11 - t]
	ldrsh	w8,[x5]
	ldrsh	w9,[x6]
	mul	w10,w8,w9
	ldrsh	w8,[x5,2]
	ldrsh	w9,[x6,2]
	madd	w10,w8,w9,w10
	ldrsh	w8,[x5,4]
	ldrsh	w9,[x6,4]
	madd	w10,w8,w9,w10
	ldrsh	w8,[x5,6]
	ldrsh	w9,[x6,6]
	madd	w10,w8,w9,w10
	ldrsh	w8,[x5,8]
	ldrsh	w9,[x7,6]
	madd	w10,w8,w9,w10
	ldrsh	w8,[x5,10]
	ldrsh	w9,[x7,4]
	madd	w10,w8,w9,w10
	ldrsh	w8,[x5,12]
	ldrsh	w9,[x7,2]
	madd	w10,w8,w9,w10
	ldrsh	w8,[x5,14]
	ldrsh	w9,[x7]
	madd	w10,w8,w9,w10
	asr	w10,w10,14			; RSHIFT_ROUND(., 15)
	add	w10,w10,1
	asr	w10,w10,1
	mov	w8,32767
	cmp	w10,w8
	csel	w10,w10,w8,lt
	mov	w8,-32768
	cmp	w10,w8
	csel	w10,w10,w8,gt
	strh	w10,[x22],2
	add	w3,w3,w1
	b	.f
.fd:	add	x23,x23,x26,lsl 1
	sub	w24,w24,w26
	add	x0,x25,x26,lsl 2		; 8 derniers echantillons de buf
	ldp	x0,x1,[x0]
	cbz	w24,.end
	stp	x0,x1,[x25]
	b	.b
.end:	stp	x0,x1,[x21,RS_FIR]
	mov	x0,x22
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = etat (6 x 32 bits), x1 = sortie (2 len), x2 = entree, w3 = len :
; surechantillonnage x2 par filtres passe-tout (silk_resampler_private_up2_HQ).
; Ne touche que x0-x15.
sk_up2:
	adr	x4,sk_up2c
	ldrsh	w5,[x4]
	ldrsh	w6,[x4,2]
	ldrsh	w7,[x4,4]
	ldrsh	w8,[x4,6]
	ldrsh	w9,[x4,8]
	ldrsh	w10,[x4,10]
	mov	x11,0
.l:	cmp	w11,w3
	b.hs	.ret
	ldrsh	w12,[x2,x11,lsl 1]
	lsl	w12,w12,10			; in32
	; pair
	ldr	w13,[x0]
	sub	w14,w12,w13
	smull	x14,w14,w5
	asr	x14,x14,16
	add	w15,w13,w14			; out32_1
	add	w13,w12,w14
	str	w13,[x0]
	ldr	w13,[x0,4]
	sub	w14,w15,w13
	smull	x14,w14,w6
	asr	x14,x14,16
	add	w4,w13,w14			; out32_2
	add	w13,w15,w14
	str	w13,[x0,4]
	ldr	w13,[x0,8]
	sub	w14,w4,w13
	smull	x15,w14,w7
	asr	x15,x15,16
	add	w14,w14,w15			; X = Y + SMULWB(Y, c)
	add	w15,w13,w14			; out32_1
	add	w13,w4,w14
	str	w13,[x0,8]
	asr	w15,w15,9
	add	w15,w15,1
	asr	w15,w15,1
	mov	w13,32767
	cmp	w15,w13
	csel	w15,w15,w13,lt
	mov	w13,-32768
	cmp	w15,w13
	csel	w15,w15,w13,gt
	strh	w15,[x1],2
	; impair
	ldr	w13,[x0,12]
	sub	w14,w12,w13
	smull	x14,w14,w8
	asr	x14,x14,16
	add	w15,w13,w14
	add	w13,w12,w14
	str	w13,[x0,12]
	ldr	w13,[x0,16]
	sub	w14,w15,w13
	smull	x14,w14,w9
	asr	x14,x14,16
	add	w4,w13,w14
	add	w13,w15,w14
	str	w13,[x0,16]
	ldr	w13,[x0,20]
	sub	w14,w4,w13
	smull	x15,w14,w10
	asr	x15,x15,16
	add	w14,w14,w15
	add	w15,w13,w14
	add	w13,w4,w14
	str	w13,[x0,20]
	asr	w15,w15,9
	add	w15,w15,1
	asr	w15,w15,1
	mov	w13,32767
	cmp	w15,w13
	csel	w15,w15,w13,lt
	mov	w13,-32768
	cmp	w15,w13
	csel	w15,w15,w13,gt
	strh	w15,[x1],2
	add	x11,x11,1
	b	.l
.ret:	ret

; ordre des coefficients 2 cos(LSF) (silk_NLSF2A)
sk_ord16	db	0, 15, 8, 7, 4, 11, 12, 3, 2, 13, 10, 5, 6, 9, 14, 1
sk_ord10	db	0, 9, 6, 3, 4, 5, 8, 1, 2, 7
		align	4
