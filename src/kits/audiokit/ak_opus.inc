; ============================================================================
;  kits/audiokit/ak_opus.inc - Opus (RFC 6716) dans Ogg (RFC 7845) : decodeur
;  de plage (RFC 6716 4.1), couche Opus (TOC, trames d'un paquet : codes 0 a
;  3, bourrage, VBR ; paquets auto-delimites des flux multiples), modes SILK,
;  hybride (SILK + CELT des bandes 17 et plus), CELT, trames redondantes CELT
;  des changements de mode (fondus de 2,5 ms), en-tetes Ogg Opus (OpusHead,
;  OpusTags, familles de canaux 0 et 1, gain de sortie), API par paquets.
;
;  Un flux Opus (OD_*) a son etat CELT (ak_celt.inc) et SILK (ak_silk.inc) ;
;  il decode ses trames dans ses propres plans (flottants, 48 kHz), que la
;  table des canaux (famille 1) recopie dans les plans du decodeur (AKD_PCM)
;  en appliquant le gain d'OpusHead, puis un ecretage doux (comme libopus en
;  sortie 16 bits).
;  Hors de la norme stricte (pas de dissimulation des pertes, PLC) : une
;  trame vide (DTX, 0 ou 1 octet) rend du silence ; un changement de mode
;  sans trame redondante passe directement au nouveau mode (libopus y fondrait
;  5 ms de dissimulation).
; ============================================================================

; --- decodeur de plage (EC_*) ------------------------------------------------------
EC_BUF		= 0		; debut des donnees
EC_PTR		= 8		; prochain octet lu par l'avant
EC_END		= 16		; fin des donnees (les octets de fin : bits bruts)
EC_EPTR		= 24		; prochain octet lu par la fin (decroissant)
EC_STORAGE	= 32		; octets
EC_ENDWIN	= 40		; bits bruts en attente (32 bits)
EC_NENDBITS	= 48
EC_NBITS	= 56		; bits lus en tout (nbits_total)
EC_RNG		= 64
EC_VAL		= 72
EC_EXT		= 80
EC_REM		= 88
EC_ERR		= 96
EC_SIZE		= 112

; --- etat Ogg Opus / API par paquets (OPS_*) --------------------------------------
OPS_CH		= 0		; canaux de sortie
OPS_NS		= 8		; flux
OPS_NC		= 16		; flux couples (stereo), les premiers
OPS_GAIN	= 24		; gain lineaire (flottant)
OPS_HASGAIN	= 32
OPS_MAP		= 40		; table des canaux (8 octets)
OPS_OD		= 48		; flux (8 pointeurs)
OPS_CLIP	= 112		; ecretage doux : memoire de chaque canal (8 flottants)
OPS_MD		= 160		; IMDCT de CELT : N = 240, 480, 960, 1920
OPS_SIZE	= OPS_MD + 4 * MD_SIZE

; --- un flux Opus (OD_*) ------------------------------------------------------------
OD_CC		= 0		; canaux du flux (1, 2)
OD_MODE		= 8		; mode de la trame : OPM_*
OD_PREVMODE	= 16		; mode de la trame precedente (0 : aucune)
OD_ENDBAND	= 24		; bande de fin de CELT (largeur de bande)
OD_FS		= 32		; echantillons par trame (48 kHz)
OD_SC		= 40		; canaux de la trame
OD_PREVRED	= 48		; la trame precedente finissait par une redondance CELT
OD_CE		= 56		; etat CELT
OD_SK		= 64		; etat SILK
OD_PCM		= 72		; 2 plans de sortie (OPS_MAXF + marge)
OD_BW		= 88		; largeur de bande : 0 NB, 1 MB, 2 WB, 3 SWB, 4 FB
OD_EC		= 96		; decodeur de plage de la trame
OD_EC2		= OD_EC + EC_SIZE	; celui de la trame redondante
OD_TR		= OD_EC2 + EC_SIZE	; trame de transition / redondante (2 x 240 flottants)
OD_SIZE		= OD_TR + 2 * 240 * 4

OPM_SILK	= 1
OPM_HYBRID	= 2
OPM_CELT	= 3
OPS_MAXF	= 5760		; 120 ms a 48 kHz

; ---------------------------------------------------------------------------
;  Decodeur de plage (RFC 6716 4.1). Les fonctions ec_* ne touchent que
;  x0-x7 (x0 = decodeur) : l'appelant garde ses valeurs dans x8-x17.
; ---------------------------------------------------------------------------

; x0 = decodeur, x1 = donnees, x2 = octets
ec_init:
	str	x1,[x0,EC_BUF]
	str	x2,[x0,EC_STORAGE]
	add	x3,x1,x2
	str	x3,[x0,EC_END]
	str	x3,[x0,EC_EPTR]
	str	xzr,[x0,EC_ENDWIN]
	str	xzr,[x0,EC_NENDBITS]
	str	xzr,[x0,EC_ERR]
	mov	x4,9				; EC_CODE_BITS + 1 - 3 x 8
	str	x4,[x0,EC_NBITS]
	mov	w4,0
	cmp	x1,x3
	b.hs	.e
	ldrb	w4,[x1],1
.e:	str	x1,[x0,EC_PTR]
	str	x4,[x0,EC_REM]
	mov	w5,127				; val = rng - 1 - (rem >> 1)
	sub	w5,w5,w4,lsr 1
	str	x5,[x0,EC_VAL]
	mov	x5,128
	str	x5,[x0,EC_RNG]
	b	ec_norm

; x0 = decodeur, x1 = octets : retire la fin des donnees (trame redondante ;
; aucun bit brut n'a encore ete lu)
ec_shrink:
	ldr	x2,[x0,EC_STORAGE]
	sub	x2,x2,x1
	str	x2,[x0,EC_STORAGE]
	ldr	x2,[x0,EC_END]
	sub	x2,x2,x1
	str	x2,[x0,EC_END]
	str	x2,[x0,EC_EPTR]
	ret

; x0 = decodeur : renormalise (rng > 2^23)
ec_norm:
	ldr	w1,[x0,EC_RNG]
	cmp	w1,0x800,lsl 12
	b.hi	.ret
	ldr	w3,[x0,EC_VAL]
	ldr	w4,[x0,EC_REM]
	ldr	x5,[x0,EC_PTR]
	ldr	x6,[x0,EC_END]
	ldr	x7,[x0,EC_NBITS]
.l:	add	x7,x7,8
	lsl	w1,w1,8
	lsl	w2,w4,8
	mov	w4,0
	cmp	x5,x6
	b.hs	.z
	ldrb	w4,[x5],1
.z:	orr	w2,w2,w4
	lsr	w2,w2,1
	mvn	w2,w2
	and	w2,w2,255
	add	w3,w2,w3,lsl 8
	and	w3,w3,0x7FFFFFFF
	cmp	w1,0x800,lsl 12
	b.ls	.l
	str	x1,[x0,EC_RNG]
	str	x3,[x0,EC_VAL]
	str	x4,[x0,EC_REM]
	str	x5,[x0,EC_PTR]
	str	x7,[x0,EC_NBITS]
.ret:	ret

; x0 = decodeur, w1 = ft -> w0 = symbole cumule (ec_decode)
ec_decode:
	ldr	w2,[x0,EC_RNG]
	udiv	w2,w2,w1
	str	x2,[x0,EC_EXT]
	ldr	w3,[x0,EC_VAL]
	udiv	w3,w3,w2
	add	w3,w3,1
	cmp	w3,w1
	csel	w3,w3,w1,lo
	sub	w0,w1,w3
	ret

; x0 = decodeur, w1 = bits -> w0 (ec_decode_bin)
ec_decode_bin:
	ldr	w2,[x0,EC_RNG]
	lsr	w2,w2,w1
	str	x2,[x0,EC_EXT]
	ldr	w3,[x0,EC_VAL]
	udiv	w3,w3,w2
	add	w3,w3,1
	mov	w4,1
	lsl	w4,w4,w1
	cmp	w3,w4
	csel	w3,w3,w4,lo
	sub	w0,w4,w3
	ret

; x0 = decodeur, w1 = fl, w2 = fh, w3 = ft (ec_dec_update)
ec_update:
	ldr	w4,[x0,EC_EXT]
	sub	w5,w3,w2
	mul	w5,w4,w5
	ldr	w6,[x0,EC_VAL]
	sub	w6,w6,w5
	str	x6,[x0,EC_VAL]
	ldr	w7,[x0,EC_RNG]
	sub	w7,w7,w5
	sub	w2,w2,w1
	mul	w2,w4,w2
	cmp	w1,0
	csel	w7,w2,w7,ne
	str	x7,[x0,EC_RNG]
	b	ec_norm

; x0 = decodeur, w1 = logp -> w0 = 1 (probabilite 1/2^logp) / 0
ec_bit_logp:
	ldr	w2,[x0,EC_RNG]
	ldr	w3,[x0,EC_VAL]
	lsr	w4,w2,w1			; s
	cmp	w3,w4
	cset	w5,lo
	sub	w6,w3,w4
	csel	w3,w3,w6,lo
	sub	w2,w2,w4
	csel	w2,w4,w2,lo
	str	x2,[x0,EC_RNG]
	str	x3,[x0,EC_VAL]
	stp	x29,x30,[sp,-16]!
	str	x5,[sp,-16]!
	bl	ec_norm
	ldr	x0,[sp],16
	ldp	x29,x30,[sp],16
	ret

; x0 = decodeur, x1 = table icdf (octets), w2 = ftb -> w0 = symbole
ec_icdf:
	ldr	w3,[x0,EC_RNG]			; s
	ldr	w4,[x0,EC_VAL]			; d
	lsr	w5,w3,w2			; r
	mov	w6,-1				; ret
.l:	mov	w2,w3				; t = s
	add	w6,w6,1
	ldrb	w7,[x1,w6,uxtw]
	mul	w3,w5,w7			; s = r icdf[ret]
	cmp	w4,w3
	b.lo	.l
	sub	w4,w4,w3
	str	x4,[x0,EC_VAL]
	sub	w2,w2,w3
	str	x2,[x0,EC_RNG]
	stp	x29,x30,[sp,-16]!
	str	x6,[sp,-16]!
	bl	ec_norm
	ldr	x0,[sp],16
	ldp	x29,x30,[sp],16
	ret

; x0 = decodeur, x1 = table icdf (16 bits), w2 = ftb -> w0 = symbole
ec_icdf16:
	ldr	w3,[x0,EC_RNG]
	ldr	w4,[x0,EC_VAL]
	lsr	w5,w3,w2
	mov	w6,-1
.l:	mov	w2,w3
	add	w6,w6,1
	ldrh	w7,[x1,w6,uxtw 1]
	mul	w3,w5,w7
	cmp	w4,w3
	b.lo	.l
	sub	w4,w4,w3
	str	x4,[x0,EC_VAL]
	sub	w2,w2,w3
	str	x2,[x0,EC_RNG]
	stp	x29,x30,[sp,-16]!
	str	x6,[sp,-16]!
	bl	ec_norm
	ldr	x0,[sp],16
	ldp	x29,x30,[sp],16
	ret

; x0 = decodeur, w1 = bits (0..25) -> w0 = bits bruts lus par la fin
ec_bits:
	ldr	w2,[x0,EC_ENDWIN]
	ldr	w3,[x0,EC_NENDBITS]
	cmp	w3,w1
	b.hs	.ok
	ldr	x4,[x0,EC_EPTR]
	ldr	x5,[x0,EC_BUF]
.f:	mov	w6,0
	cmp	x4,x5
	b.ls	.z
	ldrb	w6,[x4,-1]!
.z:	lsl	w6,w6,w3
	orr	w2,w2,w6
	add	w3,w3,8
	cmp	w3,24
	b.le	.f
	str	x4,[x0,EC_EPTR]
.ok:	mov	w6,1
	lsl	w6,w6,w1
	sub	w6,w6,1
	and	w7,w2,w6
	lsr	w2,w2,w1
	sub	w3,w3,w1
	str	x2,[x0,EC_ENDWIN]
	str	x3,[x0,EC_NENDBITS]
	ldr	x4,[x0,EC_NBITS]
	add	x4,x4,x1
	str	x4,[x0,EC_NBITS]
	mov	w0,w7
	ret

; x0 = decodeur, w1 = ft (> 1) -> w0 = entier uniforme dans [0, ft)
ec_uint:
	stp	x29,x30,[sp,-48]!
	str	x0,[sp,16]			; decodeur
	sub	w1,w1,1				; ft - 1
	clz	w2,w1
	mov	w3,32
	sub	w3,w3,w2			; ftb
	cmp	w3,8
	b.hi	.big
	add	w1,w1,1
	str	x1,[sp,24]			; ft
	bl	ec_decode
	str	x0,[sp,32]			; s
	mov	w1,w0
	add	w2,w0,1
	ldr	x3,[sp,24]
	ldr	x0,[sp,16]
	bl	ec_update
	ldr	x0,[sp,32]
	ldp	x29,x30,[sp],48
	ret
.big:	sub	w3,w3,8				; bits bruts
	str	x1,[sp,24]			; ft - 1
	str	x3,[sp,40]
	lsr	w1,w1,w3
	add	w1,w1,1				; (ft - 1) >> b + 1
	str	x1,[sp,32]
	bl	ec_decode
	ldr	x3,[sp,32]
	str	x0,[sp,32]			; s
	mov	w1,w0
	add	w2,w0,1
	ldr	x0,[sp,16]
	bl	ec_update
	ldr	x0,[sp,16]
	ldr	x1,[sp,40]
	bl	ec_bits
	ldr	x4,[sp,32]
	ldr	x3,[sp,40]
	lsl	w4,w4,w3
	orr	w0,w4,w0
	ldr	x1,[sp,24]
	cmp	w0,w1
	b.ls	.ret
	ldr	x2,[sp,16]			; trop grand : erreur
	mov	w3,1
	str	x3,[x2,EC_ERR]
	mov	w0,w1
.ret:	ldp	x29,x30,[sp],48
	ret

; x0 = decodeur -> w0 = bits lus (ec_tell)
ec_tell:
	ldr	x1,[x0,EC_NBITS]
	ldr	w2,[x0,EC_RNG]
	clz	w2,w2
	sub	x1,x1,32
	add	x0,x1,x2
	ret

; x0 = decodeur -> w0 = huitiemes de bits lus (ec_tell_frac)
ec_tell_frac:
	ldr	x1,[x0,EC_NBITS]
	lsl	x1,x1,3
	ldr	w2,[x0,EC_RNG]
	clz	w3,w2
	mov	w4,32
	sub	w4,w4,w3			; l
	sub	w5,w4,16
	lsr	w2,w2,w5			; r
	lsr	w5,w2,12
	sub	w5,w5,8				; b
	adr	x6,ec_corr
	ldr	w6,[x6,w5,uxtw 2]
	cmp	w2,w6
	cinc	w5,w5,hi
	add	w4,w5,w4,lsl 3
	sub	x0,x1,x4
	ret
		align	4
ec_corr	dw	35733, 38967, 42495, 46340, 50535, 55109, 60097, 65535

; x0 = decodeur, w1 = fs (probabilite de 0, Q15), w2 = decroissance -> w0 =
; valeur de Laplace (RFC 6716 4.3.2.1)
ec_laplace:
	stp	x29,x30,[sp,-48]!
	str	x0,[sp,16]
	stp	x1,x2,[sp,24]
	mov	w1,15
	bl	ec_decode_bin
	ldp	x1,x2,[sp,24]			; w0 = fm, w1 = fs, w2 = decroissance
	mov	w3,0				; val
	mov	w4,0				; fl
	cmp	w0,w1
	b.lo	.upd
	mov	w3,1
	mov	w4,w1				; fl = fs
	mov	w5,32768-32
	sub	w5,w5,w1
	mov	w6,16384
	sub	w6,w6,w2
	mul	w5,w5,w6
	lsr	w5,w5,15
	add	w1,w5,1				; fs
.dec:	cmp	w1,1
	b.ls	.tail
	add	w5,w4,w1,lsl 1
	cmp	w0,w5
	b.lo	.tail
	lsl	w1,w1,1				; fs *= 2
	add	w4,w4,w1			; fl += fs
	sub	w1,w1,2
	mul	w1,w1,w2
	lsr	w1,w1,15
	add	w1,w1,1
	add	w3,w3,1
	b	.dec
.tail:	cmp	w1,1
	b.hi	.sg
	sub	w5,w0,w4
	lsr	w5,w5,1				; di
	add	w3,w3,w5
	add	w4,w4,w5,lsl 1
.sg:	add	w5,w4,w1
	cmp	w0,w5
	b.hs	.pos
	neg	w3,w3
	b	.upd
.pos:	mov	w4,w5
.upd:	str	x3,[sp,24]
	add	w2,w4,w1
	mov	w5,32768
	cmp	w2,w5
	csel	w2,w2,w5,lo
	mov	w1,w4
	mov	w3,32768
	ldr	x0,[sp,16]
	bl	ec_update
	ldr	x0,[sp,24]
	ldp	x29,x30,[sp],48
	ret

; ---------------------------------------------------------------------------
;  Etat
; ---------------------------------------------------------------------------

; x0 = decodeur -> x0 = etat Opus (OPS_*)
ops_state:
	ldr	x1,[x0,AKD_MODE]
	ldr	x0,[x0,AKD_CODEC]
	cmp	x1,2
	b.eq	.ret
	ldr	x0,[x0,OG_STATE]
.ret:	ret

; x0 = decodeur, x1 = OpusHead, x2 = octets, x3 = 1 : dans Ogg -> 0 / < 0
; (ops_head, ops_init_pkt) : etat, flux, plans de sortie
ops_setup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	mov	x23,x3
	mov	x0,x19
	mov	x1,OPS_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x20,x0
	; en-tete : version, canaux, avance, gain, famille
	mov	x24,0				; avance
	mov	x25,0				; gain (Q7.8 dB)
	cbz	x21,.nohead
	cmp	x22,19
	b.lo	.bad
	ldr	x0,[x21]
	movl	x1,0x646165487375704F		; "OpusHead"
	cmp	x0,x1
	b.ne	.bad
	ldrb	w0,[x21,8]			; version : 0..15 (majeure 0)
	cmp	w0,15
	b.hi	.unsup
	ldrb	w26,[x21,9]			; canaux
	cbz	w26,.bad
	ldrh	w24,[x21,10]
	ldrsh	w25,[x21,16]
	ldrb	w0,[x21,18]			; famille
	cbnz	w0,.fam1
	cmp	w26,2
	b.hi	.bad
	b	.fam0
.nohead:
	ldr	x26,[x19,AKD_CH]
	cbz	x26,.bad
	cmp	x26,2
	b.hi	.unsup
.fam0:	str	x26,[x20,OPS_CH]
	mov	x0,1
	str	x0,[x20,OPS_NS]
	sub	x0,x26,1
	str	x0,[x20,OPS_NC]
	mov	w0,0x0100			; table 0, 1
	strh	w0,[x20,OPS_MAP]
	b	.streams
.fam1:	cmp	w26,8				; (8 plans au plus)
	b.hi	.unsup
	add	x0,x26,21
	cmp	x22,x0
	b.lo	.bad
	str	x26,[x20,OPS_CH]
	ldrb	w0,[x21,19]			; flux
	ldrb	w1,[x21,20]			; couples
	cbz	w0,.bad
	cmp	w1,w0
	b.hi	.bad
	cmp	w0,8
	b.hi	.unsup
	str	x0,[x20,OPS_NS]
	str	x1,[x20,OPS_NC]
	add	w2,w0,w1			; flux + couples : canaux codes
	mov	x3,0
.map:	cmp	x3,x26
	b.hs	.streams
	add	x4,x21,21
	ldrb	w4,[x4,x3]
	cmp	w4,255
	b.eq	.mok
	cmp	w4,w2
	b.hs	.bad
.mok:	add	x5,x20,OPS_MAP
	strb	w4,[x5,x3]
	add	x3,x3,1
	b	.map
.streams:
	; IMDCT de CELT (N = 240 << k)
	mov	x21,0
.md:	cmp	x21,4
	b.hs	.mdd
	mov	x0,x19
	mov	x1,MD_SIZE
	mul	x1,x1,x21
	add	x1,x1,x20
	add	x1,x1,OPS_MD
	mov	x2,240
	lsl	x2,x2,x21
	bl	ak_imdct_init
	tbnz	x0,63,.out
	add	x21,x21,1
	b	.md
.mdd:	; les flux : couples (stereo) d'abord
	mov	x21,0
.st:	ldr	x0,[x20,OPS_NS]
	cmp	x21,x0
	b.hs	.std
	ldr	x0,[x20,OPS_NC]
	cmp	x21,x0
	cset	x1,lo
	add	x1,x1,1				; canaux du flux
	mov	x0,x19
	add	x2,x20,OPS_MD
	bl	od_new
	tbnz	x0,63,.out
	add	x1,x20,OPS_OD
	str	x0,[x1,x21,lsl 3]
	add	x21,x21,1
	b	.st
.std:	; gain de sortie : 10^(g / (20 x 256)) = 2^(g x log2(10) / 5120)
	cbz	w25,.nog
	scvtf	s0,w25
	movl	w0,0x3A2A152D			; log2(10) / 5120
	fmov	s1,w0
	fmul	s0,s0,s1
	bl	ce_exp2
	str	s0,[x20,OPS_GAIN]
	mov	x0,1
	str	x0,[x20,OPS_HASGAIN]
.nog:	; le decodeur
	mov	x0,AKF_OPUS
	str	x0,[x19,AKD_FMT]
	movl	x0,48000
	str	x0,[x19,AKD_RATE]
	ldr	x0,[x20,OPS_CH]
	str	x0,[x19,AKD_CH]
	mov	x0,AKD_F32
	str	x0,[x19,AKD_PCMFMT]
	mov	x0,x19
	mov	x1,OPS_MAXF
	ldr	x2,[x20,OPS_CH]
	bl	akd_planes
	tbnz	x0,63,.out
	cbz	x23,.pkt
	ldr	x0,[x19,AKD_CODEC]		; Ogg : avance, pre-roll, rognage
	str	x20,[x0,OG_STATE]
	str	x24,[x0,OG_GRANOFF]
	mov	x1,3840
	str	x1,[x0,OG_PREROLL]
	str	x24,[x19,AKD_SKIP]
	b	.ok
.pkt:	str	x20,[x19,AKD_CODEC]
.ok:	mov	x0,0
	b	.out
.unsup:	mov	x0,AKE_FORMAT
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur (memoire), x1 = canaux (1, 2), x2 = IMDCT (OPS_MD) -> x0 =
; flux (OD_*), < 0 : memoire
od_new:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	mov	x1,OD_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x20,x0
	str	x21,[x20,OD_CC]
	mov	x0,x19				; plans de sortie
	movl	x1,(OPS_MAXF + 64) * 4
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,OD_PCM]
	mov	x0,x19
	movl	x1,(OPS_MAXF + 64) * 4
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,OD_PCM+8]
	mov	x0,x19
	mov	x1,x21
	mov	x2,x22
	bl	celt_new
	tbnz	x0,63,.out
	str	x0,[x20,OD_CE]
	mov	x0,x19
	mov	x1,x21
	bl	silk_new
	tbnz	x0,63,.out
	str	x0,[x20,OD_SK]
	mov	x0,x20
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = flux : oublie l'historique
od_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	str	xzr,[x19,OD_PREVMODE]
	str	xzr,[x19,OD_PREVRED]
	ldr	x0,[x19,OD_CE]
	bl	celt_reset
	ldr	x0,[x19,OD_SK]
	bl	silk_reset
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Paquets (RFC 6716 3)
; ---------------------------------------------------------------------------

; x0 = octet de TOC -> x0 = echantillons par trame (48 kHz), x1 = mode
; (OPM_*), x2 = largeur de bande (0 NB .. 4 FB)
op_toc:
	lsr	w3,w0,3				; configuration
	cmp	w3,12
	b.hs	.nsilk
	and	w4,w3,3				; SILK : 10, 20, 40, 60 ms
	mov	w0,480
	cmp	w4,1
	mov	w5,960
	csel	w0,w5,w0,eq
	cmp	w4,2
	mov	w5,1920
	csel	w0,w5,w0,eq
	cmp	w4,3
	mov	w5,2880
	csel	w0,w5,w0,eq
	mov	w1,OPM_SILK
	lsr	w2,w3,2				; NB, MB, WB
	ret
.nsilk:	cmp	w3,16
	b.hs	.celt
	tst	w3,1				; hybride : 10, 20 ms ; SWB, FB
	mov	w0,480
	mov	w5,960
	csel	w0,w5,w0,ne
	mov	w1,OPM_HYBRID
	ubfx	w2,w3,1,1
	add	w2,w2,3
	ret
.celt:	and	w4,w3,3				; CELT : 2,5, 5, 10, 20 ms
	mov	w0,120
	lsl	w0,w0,w4
	mov	w1,OPM_CELT
	sub	w2,w3,16
	lsr	w2,w2,2				; NB, WB, SWB, FB
	cmp	w2,0
	cinc	w2,w2,ne			; (pas de MB en CELT)
	ret

; x0 = taille codee (1 ou 2 octets), x1 = fin -> x0 = taille, x1 = octets lus
; (x1 = 0 : donnees manquantes)
op_size:
	cmp	x0,x1
	b.hs	.bad
	ldrb	w2,[x0]
	cmp	w2,252
	b.hs	.two
	mov	w0,w2
	mov	x1,1
	ret
.two:	add	x3,x0,1
	cmp	x3,x1
	b.hs	.bad
	ldrb	w3,[x0,1]
	add	w0,w2,w3,lsl 2
	mov	x1,2
	ret
.bad:	mov	x0,-1
	mov	x1,0
	ret

; x0 = paquet, x1 = octets, x2 = 1 : auto-delimite (RFC 6716 annexe B),
; x3 = 48 tailles (16 bits) et 48 debuts (64 bits) -> x0 = trames (< 0 :
; invalide), x1 = octets du paquet (avec le bourrage)
op_parse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0				; debut
	add	x20,x0,x1			; fin
	mov	x21,x2
	mov	x22,x3
	cbz	x1,.bad
	ldrb	w23,[x19]			; TOC
	add	x24,x19,1			; donnees
	mov	w0,w23
	bl	op_toc
	mov	x25,x0				; echantillons par trame
	and	w0,w23,3
	cmp	w0,1
	b.lo	.c0
	b.eq	.c1
	cmp	w0,2
	b.eq	.c2
	; code 3 : nombre de trames, bourrage, VBR
	cmp	x24,x20
	b.hs	.bad
	ldrb	w26,[x24],1
	and	x23,x26,63			; M
	cbz	x23,.bad
	mul	x0,x23,x25
	mov	x1,OPS_MAXF
	cmp	x0,x1
	b.hi	.bad
	mov	x25,0				; octets de bourrage
	tbz	w26,6,.nopad
.pad:	cmp	x24,x20				; bourrage : 255 -> 254 et suite
	b.hs	.bad
	ldrb	w0,[x24],1
	cmp	w0,255
	mov	w1,254
	csel	w0,w1,w0,eq
	add	x25,x25,x0
	sub	x20,x20,x0
	cmp	x20,x24
	b.lo	.bad
	ldrb	w0,[x24,-1]
	cmp	w0,255
	b.eq	.pad
.nopad:	tbz	w26,7,.cbr
	; VBR : M - 1 tailles (et la derniere si auto-delimite)
	mov	x26,0
.vs:	sub	x0,x23,1
	cmp	x26,x0
	b.hs	.vl
	mov	x0,x24
	mov	x1,x20
	bl	op_size
	cbz	x1,.bad
	add	x24,x24,x1
	strh	w0,[x22,x26,lsl 1]
	add	x26,x26,1
	b	.vs
.vl:	cbz	x21,.vlast
	mov	x0,x24
	mov	x1,x20
	bl	op_size
	cbz	x1,.bad
	add	x24,x24,x1
	strh	w0,[x22,x26,lsl 1]
	b	.place
.vlast:	; la derniere : le reste
	sub	x0,x20,x24
	mov	x1,0
	mov	x2,0
.vsum:	cmp	x2,x26
	b.hs	.vsd
	ldrh	w3,[x22,x2,lsl 1]
	add	x1,x1,x3
	add	x2,x2,1
	b	.vsum
.vsd:	subs	x0,x0,x1
	b.lt	.bad
	strh	w0,[x22,x26,lsl 1]
	b	.place
.cbr:	cbz	x21,.cbrr
	mov	x0,x24
	mov	x1,x20
	bl	op_size
	cbz	x1,.bad
	add	x24,x24,x1
	b	.cbrs
.cbrr:	sub	x0,x20,x24
	udiv	x1,x0,x23
	msub	x2,x1,x23,x0
	cbnz	x2,.bad
	mov	x0,x1
.cbrs:	mov	x1,0
.cbrl:	strh	w0,[x22,x1,lsl 1]
	add	x1,x1,1
	cmp	x1,x23
	b.lo	.cbrl
	b	.place
.c0:	mov	x23,1
	mov	x25,0
	cbz	x21,.c0r
	mov	x0,x24
	mov	x1,x20
	bl	op_size
	cbz	x1,.bad
	add	x24,x24,x1
	strh	w0,[x22]
	b	.place
.c0r:	sub	x0,x20,x24
	strh	w0,[x22]
	b	.place
.c1:	mov	x23,2
	mov	x25,0
	cbz	x21,.c1r
	mov	x0,x24
	mov	x1,x20
	bl	op_size
	cbz	x1,.bad
	add	x24,x24,x1
	strh	w0,[x22]
	strh	w0,[x22,2]
	b	.place
.c1r:	sub	x0,x20,x24
	tst	x0,1
	b.ne	.bad
	lsr	x0,x0,1
	strh	w0,[x22]
	strh	w0,[x22,2]
	b	.place
.c2:	mov	x23,2
	mov	x25,0
	mov	x0,x24
	mov	x1,x20
	bl	op_size
	cbz	x1,.bad
	add	x24,x24,x1
	strh	w0,[x22]
	cbz	x21,.c2r
	mov	x0,x24
	mov	x1,x20
	bl	op_size
	cbz	x1,.bad
	add	x24,x24,x1
	strh	w0,[x22,2]
	b	.place
.c2r:	sub	x0,x20,x24
	ldrh	w1,[x22]
	subs	x0,x0,x1
	b.lt	.bad
	strh	w0,[x22,2]
.place:	; debuts des trames ; tailles <= 1275, dans le paquet
	mov	x26,0
.pl:	cmp	x26,x23
	b.hs	.pld
	ldrh	w0,[x22,x26,lsl 1]
	cmp	w0,1275
	b.hi	.bad
	add	x1,x22,96
	str	x24,[x1,x26,lsl 3]
	add	x24,x24,x0
	cmp	x24,x20
	b.hi	.bad
	add	x26,x26,1
	b	.pl
.pld:	mov	x0,x23				; octets : fin des trames + bourrage
	sub	x1,x24,x19
	add	x1,x1,x25
	b	.out
.bad:	mov	x0,AKE_CORRUPT
	mov	x1,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = flux, x1 = paquet, x2 = octets, x3 = 1 : auto-delimite -> x0 =
; trames rendues dans OD_PCM (< 0 : erreur), x1 = octets consommes
od_packet:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x20,x1
	mov	x0,x1
	mov	x1,x2
	mov	x2,x3
	mov	x3,sp
	bl	op_parse
	tbnz	x0,63,.out
	mov	x21,x0				; trames
	mov	x26,x1				; octets consommes
	ldrb	w0,[x20]
	bl	op_toc
	str	x0,[x19,OD_FS]
	str	x1,[x19,OD_MODE]
	str	x2,[x19,OD_BW]
	ldrb	w0,[x20]
	ubfx	w0,w0,2,1
	add	w0,w0,1
	str	x0,[x19,OD_SC]
	mov	x22,0				; trame
	mov	x23,0				; echantillons rendus
.f:	cmp	x22,x21
	b.hs	.done
	mov	x0,x19
	add	x1,sp,96
	ldr	x1,[x1,x22,lsl 3]
	ldrh	w2,[sp,x22,lsl 1]
	mov	x3,x23
	bl	od_frame
	tbnz	x0,63,.out
	add	x23,x23,x0
	add	x22,x22,1
	b	.f
.done:	mov	x0,x23
	mov	x1,x26
.out:	add	sp,sp,512
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = flux, x1 = trame, x2 = octets, x3 = position dans OD_PCM (trames) ->
; x0 = trames (OD_FS) / < 0 (opus_decode_frame : SILK, hybride, CELT ;
; trames redondantes CELT des changements de mode)
od_frame:
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
	ldr	x24,[x19,OD_FS]
	ldr	x23,[x19,OD_CE]
	ldr	x25,[x19,OD_PCM]		; sorties de la trame
	add	x25,x25,x22,lsl 2
	ldr	x26,[x19,OD_PCM+8]
	add	x26,x26,x22,lsl 2
	cmp	x21,1
	b.ls	.lost
	add	x0,x19,OD_EC
	mov	x1,x20
	mov	x2,x21
	bl	ec_init
	ldr	x0,[x19,OD_BW]			; bande de fin de CELT
	adr	x1,od_endband
	ldrb	w0,[x1,x0]
	str	x0,[x23,CE_END]
	ldr	x0,[x19,OD_MODE]
	cmp	x0,OPM_CELT
	b.ne	.silk
	; CELT seul : etat CELT oublie apres un autre mode (sans redondance)
	ldr	x0,[x19,OD_PREVMODE]
	cbz	x0,.c1
	cmp	x0,OPM_CELT
	b.eq	.c1
	ldr	x0,[x19,OD_PREVRED]
	cbnz	x0,.c1
	mov	x0,x23
	bl	celt_reset
.c1:	str	xzr,[x23,CE_START]
	mov	x0,x21
	mov	x1,x24
	mov	x2,0
	add	x3,x19,OD_EC
	bl	.celt
	tbnz	x0,63,.out
	mov	x0,OPM_CELT
	str	x0,[x19,OD_PREVMODE]
	str	xzr,[x19,OD_PREVRED]
	mov	x0,x24
	b	.out
.silk:	; SILK (10 a 60 ms), apres CELT : remise a zero
	ldr	x0,[x19,OD_PREVMODE]
	cmp	x0,OPM_CELT
	b.ne	.s1
	ldr	x0,[x19,OD_SK]
	bl	silk_reset
.s1:	mov	x0,48				; duree du paquet (ms), 10 au moins
	udiv	x27,x24,x0
	cmp	x27,10
	mov	x0,10
	csel	x27,x27,x0,hs
	mov	x28,16				; frequence interne
	ldr	x0,[x19,OD_MODE]
	cmp	x0,OPM_SILK
	b.ne	.s2
	ldr	x0,[x19,OD_BW]
	mov	x28,8
	cmp	x0,1
	b.lo	.s2
	mov	x28,12
	b.eq	.s2
	mov	x28,16
.s2:	mov	x22,0				; echantillons rendus
.sl:	ldr	x0,[x19,OD_SK]
	add	x1,x19,OD_EC
	ldr	x2,[x19,OD_SC]
	mov	x3,x28
	mov	x4,x27
	cmp	x22,0
	cset	x5,eq
	add	x6,x25,x22,lsl 2
	add	x7,x26,x22,lsl 2
	bl	silk_decode
	tbnz	x0,63,.out
	cbz	x0,.bad
	add	x22,x22,x0
	cmp	x22,x24
	b.lo	.sl
	; trame redondante CELT ?
	mov	x27,0				; redondance
	mov	x28,0				; celt_to_silk
	mov	x22,0				; octets de la redondance
	add	x0,x19,OD_EC
	bl	ec_tell
	add	x0,x0,17
	ldr	x1,[x19,OD_MODE]
	cmp	x1,OPM_HYBRID
	b.ne	.r0
	add	x0,x0,20
.r0:	cmp	x0,x21,lsl 3
	b.gt	.nored
	mov	x27,1
	cmp	x1,OPM_HYBRID
	b.ne	.r1
	add	x0,x19,OD_EC
	mov	w1,12
	bl	ec_bit_logp
	mov	x27,x0
	cbz	x27,.nored
.r1:	add	x0,x19,OD_EC
	mov	w1,1
	bl	ec_bit_logp
	mov	x28,x0
	ldr	x1,[x19,OD_MODE]
	cmp	x1,OPM_HYBRID
	b.ne	.r2
	add	x0,x19,OD_EC
	mov	w1,256
	bl	ec_uint
	add	x22,x0,2
	b	.r3
.r2:	add	x0,x19,OD_EC
	bl	ec_tell
	add	x0,x0,7
	lsr	x0,x0,3
	sub	x22,x21,x0
.r3:	sub	x21,x21,x22
	add	x0,x19,OD_EC
	bl	ec_tell
	cmp	x0,x21,lsl 3
	b.ls	.r4
	mov	x21,0
	mov	x22,0
	mov	x27,0
.r4:	add	x0,x19,OD_EC
	mov	x1,x22
	bl	ec_shrink
.nored:	; redondance CELT -> SILK : decodee avant la trame
	cbz	x27,.h0
	cbz	x28,.h0
	str	xzr,[x23,CE_START]
	bl	.red
	tbnz	x0,63,.out
.h0:	ldr	x0,[x19,OD_MODE]
	cmp	x0,OPM_HYBRID
	b.ne	.h1
	; hybride : CELT a partir de la bande 17, ajoute a SILK
	mov	x0,17
	str	x0,[x23,CE_START]
	ldr	x0,[x19,OD_PREVMODE]
	cbz	x0,.h2
	cmp	x0,OPM_HYBRID
	b.eq	.h2
	ldr	x0,[x19,OD_PREVRED]
	cbnz	x0,.h2
	mov	x0,x23
	bl	celt_reset
.h2:	mov	x0,x21
	mov	x1,960
	cmp	x24,x1
	csel	x1,x24,x1,lo
	mov	x2,1
	add	x3,x19,OD_EC
	bl	.celt
	tbnz	x0,63,.out
	b	.h3
.h1:	; SILK seul apres hybride : CELT finit son recouvrement (trame de
	; silence de 2,5 ms)
	ldr	x0,[x19,OD_PREVMODE]
	cmp	x0,OPM_HYBRID
	b.ne	.h3
	cbz	x27,.h4
	cbz	x28,.h4
	ldr	x0,[x19,OD_PREVRED]
	cbnz	x0,.h3
.h4:	str	xzr,[x23,CE_START]
	add	x0,x19,OD_EC2
	adr	x1,od_silence
	mov	x2,2
	bl	ec_init
	mov	x0,2
	mov	x1,120
	mov	x2,1
	add	x3,x19,OD_EC2
	bl	.celt
	tbnz	x0,63,.out
.h3:	; redondance SILK -> CELT : decodee apres, fondu sur la fin
	cbz	x27,.f0
	cbnz	x28,.f0
	mov	x0,x23
	bl	celt_reset
	str	xzr,[x23,CE_START]
	bl	.red
	tbnz	x0,63,.out
	sub	x0,x24,120			; pcm[F - 120 ..] = fondu(pcm, red[120 ..])
	mov	x1,120
	mov	x2,0
	bl	.fade
	b	.f1
.f0:	; redondance CELT -> SILK : son debut remplace celui de la trame
	cbz	x27,.f1
	cbz	x28,.f1
	ldr	x0,[x19,OD_PREVMODE]
	cmp	x0,OPM_SILK
	b.ne	.f2
	ldr	x0,[x19,OD_PREVRED]
	cbz	x0,.f1
.f2:	mov	x0,0				; pcm[0 .. 120) = red[0 .. 120)
.cp:	add	x1,x19,OD_TR
	ldr	w2,[x1,x0,lsl 2]
	str	w2,[x25,x0,lsl 2]
	add	x1,x1,960
	ldr	w2,[x1,x0,lsl 2]
	str	w2,[x26,x0,lsl 2]
	add	x0,x0,1
	cmp	x0,120
	b.lo	.cp
	mov	x0,120				; pcm[120 .. 240) = fondu(red, pcm)
	mov	x1,120
	mov	x2,1
	bl	.fade
.f1:	ldr	x0,[x19,OD_MODE]
	str	x0,[x19,OD_PREVMODE]
	mov	x0,0
	cbz	x27,.pr
	cbnz	x28,.pr
	mov	x0,1
.pr:	str	x0,[x19,OD_PREVRED]
	mov	x0,x24
	b	.out
.lost:	; trame vide (DTX) : silence de la duree de la trame
	mov	x0,0
.lz:	cmp	x0,x24
	b.hs	.lzd
	str	wzr,[x25,x0,lsl 2]
	str	wzr,[x26,x0,lsl 2]
	add	x0,x0,1
	b	.lz
.lzd:	mov	x0,x24
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
; x0 = octets, x1 = N, x2 = 1 : ajout, x3 = decodeur de plage -> trame CELT
; dans la sortie de la trame
.celt:	str	x25,[x23,CE_OUT]
	str	x26,[x23,CE_OUT+8]
	str	x2,[x23,CE_ACCUM]
	mov	x2,x1
	mov	x1,x0
	mov	x0,x23
	mov	x4,x3
	ldr	x3,[x19,OD_SC]
	b	celt_decode
; trame redondante (5 ms, x21 = fin de la trame principale, x22 = octets) ->
; OD_TR
.red:	stp	x29,x30,[sp,-16]!
	add	x0,x19,OD_EC2
	add	x1,x20,x21
	mov	x2,x22
	bl	ec_init
	add	x0,x19,OD_TR
	str	x0,[x23,CE_OUT]
	add	x0,x0,960
	str	x0,[x23,CE_OUT+8]
	str	xzr,[x23,CE_ACCUM]
	mov	x0,x23
	mov	x1,x22
	mov	x2,240
	ldr	x3,[x19,OD_SC]
	add	x4,x19,OD_EC2
	bl	celt_decode
	ldp	x29,x30,[sp],16
	ret
; x0 = debut dans la trame, x1 = 120, x2 = 0 : pcm = fondu(pcm, red[120 ..]),
; 1 : pcm = fondu(red[120 ..], pcm) (smooth_fade : w^2 vers le second)
.fade:	adr	x3,ce_window
	add	x4,x19,OD_TR+480		; red[120 ..]
	mov	x5,0				; canal
.fc:	ldr	x6,[x19,OD_CC]
	cmp	x5,x6
	b.hs	.fr
	cmp	x5,0
	csel	x6,x25,x26,eq
	add	x6,x6,x0,lsl 2
	mov	x7,0
.fi:	ldr	s0,[x3,x7,lsl 2]
	fmul	s0,s0,s0			; w
	fmov	s1,1.0
	fsub	s1,s1,s0			; 1 - w
	ldr	s2,[x6,x7,lsl 2]		; pcm
	ldr	s3,[x4,x7,lsl 2]		; red
	cbnz	x2,.fb
	fmul	s3,s3,s0			; w red + (1 - w) pcm
	fmadd	s3,s1,s2,s3
	b	.fs
.fb:	fmul	s2,s2,s0			; w pcm + (1 - w) red
	fmadd	s3,s1,s3,s2
.fs:	str	s3,[x6,x7,lsl 2]
	add	x7,x7,1
	cmp	x7,x1
	b.lo	.fi
	add	x4,x4,960
	add	x5,x5,1
	b	.fc
.fr:	ret

od_silence	db	0xFF, 0xFF
od_endband	db	13, 17, 17, 19, 21, 0, 0, 0
		align	4

; ---------------------------------------------------------------------------
;  Ogg Opus et API par paquets
; ---------------------------------------------------------------------------

; x0 = decodeur, x1 = paquet OpusHead, x2 = octets -> 0 / < 0
ops_head:
	mov	x3,1
	b	ops_setup

; x0 = decodeur, x1 = paquet OpusTags, x2 = octets -> 0 / < 0
ops_tags:
	cmp	x2,8
	b.lo	.no
	ldr	x3,[x1]
	movl	x4,0x736761547375704F		; "OpusTags"
	cmp	x3,x4
	b.ne	.no
	add	x1,x1,8
	sub	x2,x2,8
	b	akd_vcomment
.no:	mov	x0,0
	ret

; x0 = decodeur, x1 = paquet, x2 = octets -> trames (48 kHz) / < 0
ops_packet:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	add	x22,x1,x2			; fin
	bl	ops_state
	mov	x20,x0
	cbz	x2,.bad
	; les flux : tous auto-delimites sauf le dernier
	mov	x23,0				; flux
	mov	x24,-1				; trames
.s:	ldr	x0,[x20,OPS_NS]
	cmp	x23,x0
	b.hs	.map
	sub	x0,x0,1
	cmp	x23,x0
	cset	x3,lo
	add	x0,x20,OPS_OD
	ldr	x0,[x0,x23,lsl 3]
	mov	x1,x21
	sub	x2,x22,x21
	cbz	x2,.bad
	bl	od_packet
	tbnz	x0,63,.out
	add	x21,x21,x1
	cmn	x24,1
	b.eq	.s1
	cmp	x0,x24
	b.ne	.bad
.s1:	mov	x24,x0
	add	x23,x23,1
	b	.s
.map:	; table des canaux, gain
	mov	x23,0				; canal de sortie
.ch:	ldr	x0,[x20,OPS_CH]
	cmp	x23,x0
	b.hs	.done
	add	x0,x20,OPS_MAP
	ldrb	w0,[x0,x23]
	add	x1,x19,AKD_PCM
	ldr	x25,[x1,x23,lsl 3]		; destination
	cmp	w0,255
	b.eq	.zero
	ldr	x1,[x20,OPS_NC]
	lsl	x2,x1,1
	cmp	x0,x2
	b.hs	.mono
	lsr	x3,x0,1				; flux couple, canal (bit 0)
	and	x4,x0,1
	b	.src
.mono:	sub	x3,x0,x1
	mov	x4,0
.src:	add	x0,x20,OPS_OD
	ldr	x0,[x0,x3,lsl 3]
	add	x0,x0,OD_PCM
	ldr	x26,[x0,x4,lsl 3]		; source
	ldr	x0,[x20,OPS_HASGAIN]
	ldr	s1,[x20,OPS_GAIN]
	mov	x1,0
.cp:	cmp	x1,x24
	b.hs	.clip
	ldr	s0,[x26,x1,lsl 2]
	cbz	x0,.ng
	fmul	s0,s0,s1
.ng:	str	s0,[x25,x1,lsl 2]
	add	x1,x1,1
	b	.cp
.clip:	mov	x0,x25				; ecretage doux du canal
	mov	w1,w24
	add	x2,x20,OPS_CLIP
	add	x2,x2,x23,lsl 2
	bl	ops_softclip
	b	.nch2
.zero:	mov	x1,0
.zl:	cmp	x1,x24
	b.hs	.nch
	str	wzr,[x25,x1,lsl 2]
	add	x1,x1,1
	b	.zl
.nch:	add	x2,x20,OPS_CLIP
	str	wzr,[x2,x23,lsl 2]
.nch2:	add	x23,x23,1
	b	.ch
.done:	mov	x0,x24
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = OpusHead (ou 0 : AKD_CH deja pose, famille 0), x2 =
; octets -> 0 / < 0 (API par paquets : sans rognage, l'appelant gere l'avance)
ops_init_pkt:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	str	xzr,[x19,AKD_CODEC]
	mov	x3,0
	bl	ops_setup
	tbnz	x0,63,.out
	adr	x0,ops_packet
	str	x0,[x19,AKD_PKTFN]
	adr	x0,ops_reset
	str	x0,[x19,AKD_RESETFN]
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur : oublie l'historique (apres une recherche)
ops_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	ops_state
	mov	x19,x0
	stp	xzr,xzr,[x19,OPS_CLIP]
	stp	xzr,xzr,[x19,OPS_CLIP+16]
	mov	x20,0
.l:	ldr	x0,[x19,OPS_NS]
	cmp	x20,x0
	b.hs	.out
	add	x0,x19,OPS_OD
	ldr	x0,[x0,x20,lsl 3]
	bl	od_reset
	add	x20,x20,1
	b	.l
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = plan (flottants), w1 = trames, x2 = memoire du canal (flottant) :
; ecretage doux (comme opus_pcm_soft_clip de libopus : hors de la norme, il
; evite d'ecreter net la sortie entiere) : entre deux passages par zero
; autour d'un pic au-dela de 1, x + a x^2 ramene le pic a 1
ops_softclip:
	cbz	w1,.ret
	fmov	s16,2.0
	fmov	s17,-2.0
	fmov	s18,1.0
	fmov	s19,-1.0
	mov	x3,0
.sat:	ldr	s0,[x0,x3,lsl 2]
	fmin	s0,s0,s16
	fmax	s0,s0,s17
	str	s0,[x0,x3,lsl 2]
	add	x3,x3,1
	cmp	w3,w1
	b.lo	.sat
	ldr	s7,[x2]				; a
	mov	x3,0				; suite de la trame precedente
.cont:	cmp	w3,w1
	b.hs	.cd
	ldr	s0,[x0,x3,lsl 2]
	fmul	s1,s0,s7
	fcmp	s1,0.0
	b.ge	.cd
	fmul	s1,s1,s0
	fadd	s0,s0,s1
	str	s0,[x0,x3,lsl 2]
	add	x3,x3,1
	b	.cont
.cd:	mov	x4,0				; curr
	ldr	s6,[x0]				; x0
.loop:	mov	x3,x4				; premier echantillon hors de [-1, 1]
.f:	cmp	w3,w1
	b.hs	.none
	ldr	s0,[x0,x3,lsl 2]
	fcmp	s0,s18
	b.gt	.got
	fcmp	s0,s19
	b.lt	.got
	add	x3,x3,1
	b	.f
.none:	movi	d7,0
	b	.end
.got:	mov	x5,x3				; peak_pos
	mov	x6,x3				; start
	mov	x7,x3				; end
	fabs	s2,s0				; maxval
.st:	cbz	x6,.std
	sub	x8,x6,1
	ldr	s1,[x0,x8,lsl 2]
	fmul	s1,s1,s0
	fcmp	s1,0.0
	b.lt	.std
	mov	x6,x8
	b	.st
.std:
.en:	cmp	w7,w1
	b.hs	.end2
	ldr	s1,[x0,x7,lsl 2]
	fmul	s3,s1,s0
	fcmp	s3,0.0
	b.lt	.end2
	fabs	s1,s1
	fcmp	s1,s2
	b.le	.en1
	fmov	s2,s1
	mov	x5,x7
.en1:	add	x7,x7,1
	b	.en
.end2:	mov	x9,0				; special : ecretage avant le premier zero
	cbnz	x6,.sp
	ldr	s1,[x0]
	fmul	s1,s1,s0
	fcmp	s1,0.0
	cset	x9,ge
.sp:	fsub	s3,s2,s18			; a = (maxval - 1) / maxval^2
	fmul	s4,s2,s2
	fdiv	s7,s3,s4
	movl	w8,0x3480D959			; 2,4e-7
	fmov	s4,w8
	fmadd	s7,s7,s4,s7
	fcmp	s0,0.0
	b.le	.neg
	fneg	s7,s7
.neg:	mov	x8,x6
.ap:	cmp	x8,x7
	b.hs	.apd
	ldr	s1,[x0,x8,lsl 2]
	fmul	s3,s1,s1
	fmadd	s1,s7,s3,s1
	str	s1,[x0,x8,lsl 2]
	add	x8,x8,1
	b	.ap
.apd:	cbz	x9,.nx
	cmp	x5,2
	b.lo	.nx
	ldr	s1,[x0]				; rampe du premier echantillon au pic
	fsub	s3,s6,s1			; offset
	ucvtf	s4,w5
	fdiv	s4,s3,s4			; delta
	mov	x8,x4
.rp:	cmp	x8,x5
	b.hs	.nx
	fsub	s3,s3,s4
	ldr	s1,[x0,x8,lsl 2]
	fadd	s1,s1,s3
	fmin	s1,s1,s18
	fmax	s1,s1,s19
	str	s1,[x0,x8,lsl 2]
	add	x8,x8,1
	b	.rp
.nx:	mov	x4,x7
	cmp	w4,w1
	b.lo	.loop
.end:	str	s7,[x2]
.ret:	ret
