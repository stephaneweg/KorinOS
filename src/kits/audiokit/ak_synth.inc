; ============================================================================
;  kits/audiokit/ak_synth.inc - synthetiseur MIDI d'AudioKit (docs/SON.md 9)
;
;  16 canaux General MIDI, deux moteurs :
;  * SoundFont 2 (ecrit d'apres la specification SoundFont 2.04) : voix a table
;    d'ondes, enveloppes de volume et de modulation, deux LFO, filtre passe-bas
;    resonant, panoramique, reverberation, chorus ; les calculs lents se font par
;    bloc de 64 trames (~1,45 ms) ;
;  * banque FM (ak_gmbank.inc) quand aucune SoundFont n'est la : les 128
;    programmes et les percussions, sur la synthese FM (ak_fm.inc).
;  MIDI : note, controleurs 1, 6, 7, 10, 11, 38, 64, 91, 93, 100, 101, 120, 121,
;  123, programme, molette de hauteur (etendue par le RPN 0), banque (CC 0).
;  Chaque synthe est un bloc a soi : plusieurs peuvent jouer ensemble, sur une
;  meme SoundFont (lecture seule). Verrou : une note peut arriver d'un autre
;  thread pendant le rendu.
; ============================================================================

macro bl_save {
	stp	x29,x30,[sp,-16]!
}
macro bl_rest {
	ldp	x29,x30,[sp],16
}

SY_BLOCK	= 64		; trames par bloc
SY_NCH		= 16
SY_NV		= 64		; voix SoundFont
SY_NFM		= 32		; voix FM

; --- synthe ---------------------------------------------------------------------
SY_LOCK		= 0		; (meme place que AKFM_LOCK : fm_lock)
SY_SF		= 8		; SoundFont, 0 : banque FM
SY_FM		= 16		; synthe FM
SY_AGE		= 24		; compteur de notes
SY_BPOS		= 32		; trames deja rendues du bloc courant
SY_RVB		= 40		; reverberation
SY_GAIN		= 48		; (flottant) volume general
SY_MFC		= 52		; modulateurs de la note : coupure (cents)
SY_MME		= 56		; enveloppe -> coupure (cents)
SY_MATT		= 60		; attenuation (centibels)
SY_MVEL		= 64		; 1 : la SoundFont donne velocite -> attenuation
SY_CHO		= 72		; chorus
SY_CACT		= 68		; 1 : un envoi de chorus pendant ce bloc
SY_CHAN		= 80
SC_SIZE		= 64
SY_FMV		= SY_CHAN + SC_SIZE * SY_NCH
FV_SIZE		= 16
SY_BL		= SY_FMV + FV_SIZE * SY_NFM	; bloc gauche (flottant)
SY_BR		= SY_BL + SY_BLOCK * 4		; bloc droit
SY_BV		= SY_BR + SY_BLOCK * 4		; envoi de reverberation
SY_BC		= SY_BV + SY_BLOCK * 4		; envoi de chorus
SY_VOICES	= SY_BC + SY_BLOCK * 4

; --- canal ------------------------------------------------------------------------
SC_PROG		= 0
SC_BANK		= 4
SC_VOL		= 8		; CC 7
SC_EXPR		= 12		; CC 11
SC_PAN		= 16		; CC 10
SC_BEND		= 20		; -8192..8191
SC_RANGE	= 24		; etendue (cents)
SC_SUST		= 28		; CC 64
SC_MOD		= 32		; CC 1
SC_RPN		= 36		; RPN choisi
SC_REV		= 40		; CC 91
SC_CHO		= 44		; CC 93
SC_PRESET	= 48		; preset SoundFont + 1, 0 : a chercher

; --- voix FM de la banque ------------------------------------------------------------
FV_STATE	= 0		; octet : 0 libre, 1 touche, 2 tenue (pedale), 3 relachee
FV_CHAN		= 1
FV_KEY		= 2		; touche MIDI
FV_PLAYED	= 3		; touche jouee (percussions : celle du tableau)
FV_AGE		= 4
FV_INS		= 8		; instrument charge + 1
FV_VEL		= 12		; velocite

; --- voix SoundFont --------------------------------------------------------------------
SV_STATE	= 0		; 0 libre, 1 sonne
SV_REL		= 4		; 0 touche enfoncee, 1 relachee, 2 tenue par la pedale
SV_CHAN		= 8
SV_KEY		= 12
SV_VEL		= 16
SV_AGE		= 20
SV_EXCL		= 24		; classe exclusive
SV_MODE		= 28		; 0 sans boucle, 1 boucle, 3 boucle jusqu'au relachement
SV_POS		= 32		; position 32.32
SV_INC		= 40		; pas 32.32
SV_SMP		= 48		; echantillons
SV_END		= 56		; fin (indice)
SV_LS		= 64		; boucle
SV_LE		= 72
SV_CENTS	= 80
SV_RATE		= 84		; frequence relative 12.20
SV_VE		= 88		; enveloppe de volume
SV_ME		= 128		; enveloppe de modulation
SV_ATT		= 168		; attenuation (centibels)
SV_PAN		= 172		; -500..500
SV_RSEND	= 176		; 0..1000
SV_CSEND	= 180
SV_GL		= 184		; gains du bloc precedent (flottants)
SV_GR		= 188
SV_FC		= 192
SV_Q		= 196
SV_ML2P		= 200		; LFO de modulation -> hauteur
SV_VL2P		= 204		; LFO de vibrato -> hauteur
SV_ME2P		= 208		; enveloppe -> hauteur
SV_ML2F		= 212
SV_ME2F		= 216
SV_ML2V		= 220
SV_MLPH		= 224
SV_MLINC	= 228
SV_MLDEL	= 232
SV_VLPH		= 236
SV_VLINC	= 240
SV_VLDEL	= 244
SV_Z1		= 248		; filtre (flottants)
SV_Z2		= 252
SV_B0		= 256
SV_B1		= 260
SV_A1		= 264
SV_A2		= 268
SV_LASTFC	= 272		; dernier reglage (cents)
SV_SIZE		= 288

SY_SIZE		= SY_VOICES + SV_SIZE * SY_NV

; enveloppe : niveau 16.16 (0..65536) ; volume : lineaire en dB (0 = -96 dB),
; sauf l'attaque (lineaire en amplitude)
EN_STAGE	= 0
EN_COUNT	= 4
EN_LVL		= 8
EN_ATTS		= 12
EN_DECS		= 16
EN_SUS		= 20
EN_RELS		= 24
EN_DELAY	= 28
EN_HOLD		= 32
ES_DELAY	= 0
ES_ATTACK	= 1
ES_HOLD		= 2
ES_DECAY	= 3
ES_SUSTAIN	= 4
ES_RELEASE	= 5
ES_DONE		= 6

; --- generateurs SoundFont ---------------------------------------------------------
G_INSTRUMENT	= 41
G_KEYRANGE	= 43
G_VELRANGE	= 44
G_SAMPLEID	= 53
SF_NGEN		= 61

; --- SoundFont ------------------------------------------------------------------------
SF_MEM		= 0		; fichier en memoire
SF_SIZE_	= 8
SF_SMPL		= 16
SF_NSMPL	= 24		; echantillons
SF_PHDR		= 32
SF_NPHDR	= 40		; (sans le dernier, EOP)
SF_PBAG		= 48
SF_PGEN		= 56
SF_INST		= 64
SF_IBAG		= 72
SF_IGEN		= 80
SF_SHDR		= 88
SF_NSHDR	= 96
SF_NAME		= 104		; 64 o
SF_PMOD		= 168
SF_IMOD		= 176
SF_BYTES	= 184

; ---------------------------------------------------------------------------
;  Creation
; ---------------------------------------------------------------------------

; x0 = SoundFont (0 : celle du systeme, ak_sf_default), x1 = AKS_* -> synthe, < 0
ak_synth_new:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	cbnz	x20,.have
	tbnz	x21,0,.have			; AKS_FM : pas de SoundFont
	bl	ak_sf_default
	mov	x20,x0
.have:	tst	x21,AKS_FM
	csel	x20,xzr,x20,ne
	movl	x0,SY_SIZE
	bl	ak_alloc
	cbz	x0,.nomem
	mov	x19,x0
	str	x20,[x19,SY_SF]
	mov	x0,SY_BLOCK
	str	x0,[x19,SY_BPOS]
	movl	w0,0x3F000000			; 0,5
	str	w0,[x19,SY_GAIN]
	mov	x0,x19
	bl	sy_channels_reset
	cbnz	x20,.sf
	mov	x0,SY_NFM
	bl	ak_fm_new
	tbnz	x0,63,.fail
	str	x0,[x19,SY_FM]
	b	.ok
.sf:	tst	x21,AKS_NOFX
	b.ne	.ok
	bl	rv_new
	str	x0,[x19,SY_RVB]			; (0 : sans reverberation)
	movl	x0,CH_SIZE
	bl	ak_alloc
	str	x0,[x19,SY_CHO]			; (0 : sans chorus)
.ok:	mov	x0,x19
	b	.out
.fail:	mov	x20,x0
	mov	x0,x19
	bl	ak_free
	mov	x0,x20
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = synthe -> 0
ak_synth_free:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	ldr	x0,[x19,SY_FM]
	cbz	x0,.r
	bl	ak_fm_free
.r:	ldr	x0,[x19,SY_RVB]
	bl	ak_free
	ldr	x0,[x19,SY_CHO]
	bl	ak_free
	mov	x0,x19
	bl	ak_free
.out:	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; x0 = synthe : canaux aux valeurs de depart
sy_channels_reset:
	add	x1,x0,SY_CHAN
	mov	x2,0
.c:	str	wzr,[x1,SC_PROG]
	str	wzr,[x1,SC_BANK]
	mov	w3,100
	str	w3,[x1,SC_VOL]
	mov	w3,127
	str	w3,[x1,SC_EXPR]
	mov	w3,64
	str	w3,[x1,SC_PAN]
	str	wzr,[x1,SC_BEND]
	mov	w3,200
	str	w3,[x1,SC_RANGE]
	str	wzr,[x1,SC_SUST]
	str	wzr,[x1,SC_MOD]
	movl	w3,0x3FFF
	str	w3,[x1,SC_RPN]
	mov	w3,40
	str	w3,[x1,SC_REV]
	str	wzr,[x1,SC_CHO]
	str	wzr,[x1,SC_PRESET]
	add	x1,x1,SC_SIZE
	add	x2,x2,1
	cmp	x2,SY_NCH
	b.lo	.c
	ret

; x0 = synthe -> 0 : tout se tait, canaux remis a zero
ak_synth_reset:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	fm_lock
	bl	sy_hush
	mov	x0,x19
	bl	sy_channels_reset
	mov	x0,x19
	bl	fm_unlock
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; x19 = synthe : toutes les voix coupees
sy_hush:
	prologue
	ldr	x0,[x19,SY_FM]
	cbz	x0,.sf
	bl	ak_fm_silence
	add	x1,x19,SY_FMV
	mov	x2,SY_NFM
.f:	strb	wzr,[x1,FV_STATE]
	add	x1,x1,FV_SIZE
	subs	x2,x2,1
	b.ne	.f
.sf:	add	x1,x19,SY_VOICES
	mov	x2,SY_NV
.s:	str	wzr,[x1,SV_STATE]
	add	x1,x1,SV_SIZE
	subs	x2,x2,1
	b.ne	.s
	epilogue

; x0 = synthe -> voix qui sonnent
ak_synth_voices:
	ldr	x1,[x0,SY_FM]
	cbz	x1,.sf
	mov	x0,x1
	ldr	x5,[x0,AKFM_NV]
	add	x2,x0,AKFM_VOICES
	mov	x0,0
	mov	x6,x30
.v:	cbz	x5,.e
	bl	fm_voice_active
	add	x0,x0,x3
	add	x2,x2,FMV_SIZE
	sub	x5,x5,1
	b	.v
.e:	mov	x30,x6
	ret
.sf:	add	x1,x0,SY_VOICES
	mov	x2,SY_NV
	mov	x0,0
.s:	ldr	w3,[x1,SV_STATE]
	add	x0,x0,x3
	add	x1,x1,SV_SIZE
	subs	x2,x2,1
	b.ne	.s
	ret

; ---------------------------------------------------------------------------
;  Messages MIDI
; ---------------------------------------------------------------------------

; x0 = synthe, x1 = canal 0..15, x2 = commande (0x80 fin de note, 0x90 note,
; 0xB0 controleur, 0xC0 programme, 0xE0 molette ; quatre bits bas ignores),
; x3, x4 = donnees -> 0
ak_synth_midi:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	and	w23,w2,0xF0
	and	w21,w3,127
	and	w22,w4,127
	cmp	x1,SY_NCH
	b.hs	.ret
	add	x20,x19,SY_CHAN
	lsl	x1,x1,6
	add	x20,x20,x1			; le canal
	mov	x0,x19
	bl	fm_lock
	cmp	w23,0x90
	b.ne	.n9
	cbz	w22,.off
	bl	sy_note_on
	b	.done
.n9:	cmp	w23,0x80
	b.ne	.nb
.off:	bl	sy_note_off
	b	.done
.nb:	cmp	w23,0xB0
	b.ne	.nc
	bl	sy_control
	b	.done
.nc:	cmp	w23,0xC0
	b.ne	.ne
	str	w21,[x20,SC_PROG]
	str	wzr,[x20,SC_PRESET]
	b	.done
.ne:	cmp	w23,0xE0
	b.ne	.done
	orr	w0,w21,w22,lsl 7
	sub	w0,w0,8192
	str	w0,[x20,SC_BEND]
	bl	sy_refresh
.done:	mov	x0,x19
	bl	fm_unlock
.ret:	mov	x0,0
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x19 = synthe, x20 = canal, w21 = controleur, w22 = valeur
sy_control:
	prologue
	cmp	w21,1
	b.ne	.c7
	str	w22,[x20,SC_MOD]
	b	.ret
.c7:	cmp	w21,7
	b.ne	.c10
	str	w22,[x20,SC_VOL]
	bl	sy_refresh
	b	.ret
.c10:	cmp	w21,10
	b.ne	.c11
	str	w22,[x20,SC_PAN]
	bl	sy_refresh
	b	.ret
.c11:	cmp	w21,11
	b.ne	.c64
	str	w22,[x20,SC_EXPR]
	bl	sy_refresh
	b	.ret
.c64:	cmp	w21,64
	b.ne	.c0
	cmp	w22,64
	cset	w0,hs
	str	w0,[x20,SC_SUST]
	cbnz	w0,.ret
	bl	sy_pedal_up
	b	.ret
.c0:	cbnz	w21,.c6
	str	w22,[x20,SC_BANK]
	str	wzr,[x20,SC_PRESET]
	b	.ret
.c6:	cmp	w21,6
	b.ne	.c38
	ldr	w0,[x20,SC_RPN]			; RPN 0 : etendue de la molette
	cbnz	w0,.ret
	ldr	w1,[x20,SC_RANGE]
	mov	w2,100
	udiv	w3,w1,w2
	msub	w1,w3,w2,w1			; cents gardes
	mul	w3,w22,w2
	add	w1,w1,w3
	str	w1,[x20,SC_RANGE]
	b	.ret
.c38:	cmp	w21,38
	b.ne	.c91
	ldr	w0,[x20,SC_RPN]
	cbnz	w0,.ret
	ldr	w1,[x20,SC_RANGE]
	mov	w2,100
	udiv	w3,w1,w2
	mul	w3,w3,w2
	add	w3,w3,w22
	str	w3,[x20,SC_RANGE]
	b	.ret
.c91:	cmp	w21,91
	b.ne	.c93
	str	w22,[x20,SC_REV]
	b	.ret
.c93:	cmp	w21,93
	b.ne	.c100
	str	w22,[x20,SC_CHO]
	b	.ret
.c100:	cmp	w21,100
	b.ne	.c101
	ldr	w0,[x20,SC_RPN]
	and	w0,w0,0x3F80
	orr	w0,w0,w22
	str	w0,[x20,SC_RPN]
	b	.ret
.c101:	cmp	w21,101
	b.ne	.c120
	ldr	w0,[x20,SC_RPN]
	and	w0,w0,0x7F
	orr	w0,w0,w22,lsl 7
	str	w0,[x20,SC_RPN]
	b	.ret
.c120:	cmp	w21,120
	b.ne	.c121
	bl	sy_channel_cut
	b	.ret
.c121:	cmp	w21,121
	b.ne	.c123
	str	wzr,[x20,SC_BEND]
	str	wzr,[x20,SC_MOD]
	mov	w0,127
	str	w0,[x20,SC_EXPR]
	str	wzr,[x20,SC_SUST]
	bl	sy_pedal_up
	bl	sy_refresh
	b	.ret
.c123:	cmp	w21,123
	b.ne	.ret
	bl	sy_all_off
.ret:	epilogue

; x19, x20 : le numero de ce canal -> x0
sy_chan_index:
	add	x0,x19,SY_CHAN
	sub	x0,x20,x0
	lsr	x0,x0,6
	ret

; ---------------------------------------------------------------------------
;  Banque FM
; ---------------------------------------------------------------------------

; w0 = touche, x20 = canal -> x0 = milli-Hz, molette comprise
fmb_freq:
	prologue
	sub	w0,w0,69
	mov	w1,100
	mul	w0,w0,w1
	ldr	w1,[x20,SC_BEND]
	ldr	w2,[x20,SC_RANGE]
	mul	w1,w1,w2
	asr	w1,w1,13
	add	w0,w0,w1
	sxtw	x0,w0
	movl	x1,440000
	bl	ak_cents_mul
	epilogue

; x20 = canal, w0 = velocite -> x0 = volume 0..255
fmb_gain:
	ldr	w1,[x20,SC_VOL]
	mul	w0,w0,w1
	ldr	w1,[x20,SC_EXPR]
	mul	w0,w0,w1
	mov	w1,16129			; 127 x 127
	udiv	w0,w0,w1			; 0..127
	mul	w0,w0,w0
	mov	w1,127
	udiv	w0,w0,w1
	lsl	w0,w0,1
	ret

; x19, x20 canal, w21 touche, w22 velocite
fmb_note_on:
	prologue
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	bl	sy_chan_index
	mov	x23,x0				; numero du canal
	mov	w25,w21				; touche jouee
	cmp	x23,9
	b.ne	.melo
	cmp	w21,35				; percussions
	b.lo	.ret
	cmp	w21,81
	b.hi	.ret
	sub	w0,w21,35
	adr	x1,ak_gm_drum
	add	x1,x1,x0,lsl 1
	ldrb	w24,[x1]
	ldrb	w25,[x1,1]
	b	.got
.melo:	ldr	w0,[x20,SC_PROG]
	adr	x1,ak_gm_prog
	ldrb	w24,[x1,x0]
.got:	; une voix : libre, sinon la plus ancienne relachee, sinon la plus ancienne
	ldr	x0,[x19,SY_FM]
	add	x2,x0,AKFM_VOICES
	add	x6,x19,SY_FMV
	mov	x7,0
	mov	x8,-1				; meilleure
	movl	w10,0xFFFFFFFF			; age
	mov	w11,0				; 1 : la meilleure est relachee
.s:	bl	fm_voice_active
	cbz	x3,.free
	ldrb	w3,[x6,FV_STATE]
	cmp	w3,3
	cset	w3,eq
	ldr	w4,[x6,FV_AGE]
	cmp	w3,w11
	b.hi	.better
	b.lo	.nxt
	cmp	w4,w10
	b.hs	.nxt
.better:
	mov	x8,x7
	mov	w10,w4
	mov	w11,w3
.nxt:	add	x2,x2,FMV_SIZE
	add	x6,x6,FV_SIZE
	add	x7,x7,1
	cmp	x7,SY_NFM
	b.lo	.s
	mov	x7,x8
.free:	mov	x26,x7
	add	x6,x19,SY_FMV
	add	x6,x6,x26,lsl 4
	strb	w23,[x6,FV_CHAN]
	strb	w21,[x6,FV_KEY]
	strb	w25,[x6,FV_PLAYED]
	strb	w22,[x6,FV_VEL]
	mov	w0,1
	strb	w0,[x6,FV_STATE]
	ldr	x0,[x19,SY_AGE]
	add	x0,x0,1
	str	x0,[x19,SY_AGE]
	str	w0,[x6,FV_AGE]
	ldr	w0,[x6,FV_INS]			; instrument
	add	w1,w24,1
	cmp	w0,w1
	b.eq	.loaded
	str	w1,[x6,FV_INS]
	ldr	x0,[x19,SY_FM]
	mov	x1,x26
	mov	x3,AKFI_SIZE
	mul	x3,x3,x24
	adr	x2,ak_gm_ins
	add	x2,x2,x3
	bl	ak_fm_instrument
.loaded:
	ldr	w2,[x20,SC_PAN]
	lsl	w2,w2,1
	cmp	w2,255
	mov	w0,255
	csel	w2,w0,w2,hi
	ldr	x0,[x19,SY_FM]
	mov	x1,x26
	bl	ak_fm_pan
	mov	w0,w25
	bl	fmb_freq
	mov	x2,x0
	mov	w0,w22
	bl	fmb_gain
	mov	x4,x0
	ldr	x0,[x19,SY_FM]
	mov	x1,x26
	mov	x3,AKW_FM
	bl	ak_fm_start
.ret:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	epilogue

; x19, x20 canal, w21 touche
fmb_note_off:
	prologue
	stp	x23,x24,[sp,-16]!
	bl	sy_chan_index
	mov	x23,x0
	add	x24,x19,SY_FMV
	mov	x1,0
.v:	ldrb	w2,[x24,FV_STATE]
	cmp	w2,1
	b.ne	.n
	ldrb	w2,[x24,FV_CHAN]
	cmp	x2,x23
	b.ne	.n
	ldrb	w2,[x24,FV_KEY]
	cmp	w2,w21
	b.ne	.n
	ldr	w2,[x20,SC_SUST]
	cbz	w2,.stop
	mov	w2,2
	strb	w2,[x24,FV_STATE]
	b	.n
.stop:	mov	w2,3
	strb	w2,[x24,FV_STATE]
	str	x1,[sp,-16]!
	ldr	x0,[x19,SY_FM]
	bl	ak_fm_stop
	ldr	x1,[sp],16
.n:	add	x24,x24,FV_SIZE
	add	x1,x1,1
	cmp	x1,SY_NFM
	b.lo	.v
	ldp	x23,x24,[sp],16
	epilogue

; x19, x20 canal : applique molette, volume, panoramique aux voix qui sonnent
fmb_refresh:
	prologue
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	bl	sy_chan_index
	mov	x23,x0
	add	x24,x19,SY_FMV
	mov	x25,0
.v:	ldrb	w2,[x24,FV_STATE]
	cbz	w2,.n
	cmp	w2,3
	b.eq	.n
	ldrb	w2,[x24,FV_CHAN]
	cmp	x2,x23
	b.ne	.n
	ldrb	w0,[x24,FV_PLAYED]
	bl	fmb_freq
	mov	x2,x0
	ldr	x0,[x19,SY_FM]
	mov	x1,x25
	bl	ak_fm_pitch
	ldr	w2,[x20,SC_PAN]
	lsl	w2,w2,1
	cmp	w2,255
	mov	w0,255
	csel	w2,w0,w2,hi
	ldr	x0,[x19,SY_FM]
	mov	x1,x25
	bl	ak_fm_pan
	ldrb	w0,[x24,FV_VEL]
	bl	fmb_gain
	mov	x2,x0
	ldr	x0,[x19,SY_FM]
	mov	x1,x25
	bl	ak_fm_gain
.n:	add	x24,x24,FV_SIZE
	add	x25,x25,1
	cmp	x25,SY_NFM
	b.lo	.v
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	epilogue

; x19, x20 canal, x1 = etat a relacher (2 : pedale ; 1 : toutes) ; x2 = 1 coupe
fmb_release:
	prologue
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x25,x1
	bl	sy_chan_index
	mov	x23,x0
	add	x24,x19,SY_FMV
	mov	x26,0
.v:	ldrb	w2,[x24,FV_STATE]
	cbz	w2,.n
	cmp	w2,3
	b.eq	.n
	cmp	x2,x25
	b.lo	.n
	ldrb	w2,[x24,FV_CHAN]
	cmp	x2,x23
	b.ne	.n
	mov	w2,3
	strb	w2,[x24,FV_STATE]
	ldr	x0,[x19,SY_FM]
	mov	x1,x26
	bl	ak_fm_stop
.n:	add	x24,x24,FV_SIZE
	add	x26,x26,1
	cmp	x26,SY_NFM
	b.lo	.v
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Aiguillage des moteurs (x19 synthe, x20 canal, w21 touche, w22 velocite)
; ---------------------------------------------------------------------------
sy_note_on:
	ldr	x0,[x19,SY_SF]
	cbnz	x0,sf_note_on
	b	fmb_note_on

sy_note_off:
	ldr	x0,[x19,SY_SF]
	cbz	x0,fmb_note_off
	; (suite)
sfv_note_off:
	bl_save
	bl	sy_chan_index
	add	x1,x19,SY_VOICES
	mov	x2,SY_NV
	ldr	w5,[x20,SC_SUST]
.v:	ldr	w3,[x1,SV_STATE]
	cbz	w3,.n
	ldr	w3,[x1,SV_REL]
	cbnz	w3,.n
	ldr	w3,[x1,SV_CHAN]
	cmp	x3,x0
	b.ne	.n
	ldr	w3,[x1,SV_KEY]
	cmp	w3,w21
	b.ne	.n
	cbz	w5,.rel
	mov	w3,2
	str	w3,[x1,SV_REL]
	b	.n
.rel:	bl	sv_release
.n:	add	x1,x1,SV_SIZE
	subs	x2,x2,1
	b.ne	.v
	bl_rest
	ret

; x19, x20 canal : molette, volume, panoramique : aux voix FM (les voix
; SoundFont lisent le canal a chaque bloc)
sy_refresh:
	ldr	x0,[x19,SY_SF]
	cbz	x0,fmb_refresh
	ret

; x19, x20 : pedale relevee
sy_pedal_up:
	ldr	x0,[x19,SY_SF]
	cbnz	x0,.sf
	mov	x1,2
	b	fmb_release
.sf:	bl_save
	bl	sy_chan_index
	add	x1,x19,SY_VOICES
	mov	x2,SY_NV
.v:	ldr	w3,[x1,SV_STATE]
	cbz	w3,.n
	ldr	w3,[x1,SV_REL]
	cmp	w3,2
	b.ne	.n
	ldr	w3,[x1,SV_CHAN]
	cmp	x3,x0
	b.ne	.n
	bl	sv_release
.n:	add	x1,x1,SV_SIZE
	subs	x2,x2,1
	b.ne	.v
	bl_rest
	ret

; x19, x20 : CC 123, toutes les notes relachees
sy_all_off:
	ldr	x0,[x19,SY_SF]
	cbnz	x0,.sf
	mov	x1,1
	b	fmb_release
.sf:	bl_save
	bl	sy_chan_index
	add	x1,x19,SY_VOICES
	mov	x2,SY_NV
.v:	ldr	w3,[x1,SV_STATE]
	cbz	w3,.n
	ldr	w3,[x1,SV_REL]
	cmp	w3,1
	b.eq	.n
	ldr	w3,[x1,SV_CHAN]
	cmp	x3,x0
	b.ne	.n
	bl	sv_release
.n:	add	x1,x1,SV_SIZE
	subs	x2,x2,1
	b.ne	.v
	bl_rest
	ret

; x19, x20 : CC 120, ce canal coupe
sy_channel_cut:
	prologue
	ldr	x0,[x19,SY_SF]
	cbnz	x0,.sf
	mov	x1,1
	bl	fmb_release
	b	.ret
.sf:	bl	sy_chan_index
	add	x1,x19,SY_VOICES
	mov	x2,SY_NV
.v:	ldr	w3,[x1,SV_CHAN]
	cmp	x3,x0
	b.ne	.n
	str	wzr,[x1,SV_STATE]
.n:	add	x1,x1,SV_SIZE
	subs	x2,x2,1
	b.ne	.v
.ret:	epilogue

; ---------------------------------------------------------------------------
;  SoundFont : lecture du fichier
; ---------------------------------------------------------------------------

; x0 = fichier en memoire, x1 = octets -> SoundFont, < 0 ; le bloc reste a
; l'appelant jusqu'a ak_sf_free (qui ne le rend que si SF_MEM est pose)
sf_parse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	add	x22,x0,x1			; fin
	cmp	x1,12
	b.lo	.bad
	ldr	w2,[x21]
	movl	w3,0x46464952			; 'RIFF'
	cmp	w2,w3
	b.ne	.bad
	ldr	w2,[x21,8]
	movl	w3,0x6B626673			; 'sfbk'
	cmp	w2,w3
	b.ne	.bad
	mov	x0,SF_BYTES
	bl	ak_alloc
	cbz	x0,.nomem
	mov	x19,x0
	add	x20,x21,12
.chunk:	add	x0,x20,8
	cmp	x0,x22
	b.hi	.end
	ldr	w2,[x20]			; identifiant
	ldr	w23,[x20,4]			; taille
	movl	w3,0x5453494C			; 'LIST'
	cmp	w2,w3
	b.ne	.skip
	ldr	w2,[x20,8]
	movl	w3,0x4F464E49			; 'INFO'
	cmp	w2,w3
	b.eq	.sub
	movl	w3,0x61746473			; 'sdta'
	cmp	w2,w3
	b.eq	.sub
	movl	w3,0x61746470			; 'pdta'
	cmp	w2,w3
	b.ne	.skip
.sub:	add	x24,x20,12			; sous-morceaux
	add	x23,x23,8
	add	x23,x20,x23			; fin de la liste
.s:	add	x0,x24,8
	cmp	x0,x23
	b.hi	.next
	ldr	w2,[x24]
	ldr	w3,[x24,4]
	add	x4,x24,8
	mov	x0,x2
	mov	x1,x3
	mov	x2,x4
	bl	sf_chunk
	ldr	w3,[x24,4]
	add	x3,x3,1
	and	x3,x3,not 1
	add	x24,x24,x3
	add	x24,x24,8
	b	.s
.next:	mov	x20,x23
	add	x20,x20,1
	and	x20,x20,not 1
	b	.chunk
.skip:	add	x23,x23,1
	and	x23,x23,not 1
	add	x20,x20,x23
	add	x20,x20,8
	b	.chunk
.end:	ldr	x0,[x19,SF_SMPL]		; le minimum
	cbz	x0,.badf
	ldr	x0,[x19,SF_PHDR]
	cbz	x0,.badf
	ldr	x0,[x19,SF_SHDR]
	cbz	x0,.badf
	ldr	x0,[x19,SF_IGEN]
	cbz	x0,.badf
	ldr	x0,[x19,SF_INST]
	cbz	x0,.badf
	ldr	x0,[x19,SF_IBAG]
	cbz	x0,.badf
	ldr	x0,[x19,SF_PGEN]
	cbz	x0,.badf
	ldr	x0,[x19,SF_PBAG]
	cbz	x0,.badf
	mov	x0,x19
	b	.out
.badf:	mov	x0,x19
	bl	ak_free
.bad:	mov	x0,AKE_CORRUPT
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fichier en memoire, x1 = octets -> SoundFont, < 0
ak_sf_open_mem:
	b	sf_parse

; x19 = SoundFont, w0 = identifiant, x1 = taille, x2 = donnees
sf_chunk:
	movl	w3,0x4D414E49			; 'INAM'
	cmp	w0,w3
	b.ne	.smpl
	add	x4,x19,SF_NAME
	mov	x5,63
	cmp	x1,x5
	csel	x1,x1,x5,lo
.n:	cbz	x1,.ret
	ldrb	w3,[x2],1
	strb	w3,[x4],1
	sub	x1,x1,1
	b	.n
.smpl:	movl	w3,0x6C706D73			; 'smpl'
	cmp	w0,w3
	b.ne	.phdr
	str	x2,[x19,SF_SMPL]
	lsr	x1,x1,1
	str	x1,[x19,SF_NSMPL]
	ret
.phdr:	movl	w3,0x72646870			; 'phdr'
	cmp	w0,w3
	b.ne	.pbag
	str	x2,[x19,SF_PHDR]
	mov	x3,38
	udiv	x1,x1,x3
	sub	x1,x1,1
	str	x1,[x19,SF_NPHDR]
	ret
.pbag:	movl	w3,0x67616270			; 'pbag'
	cmp	w0,w3
	b.ne	.pgen
	str	x2,[x19,SF_PBAG]
	ret
.pgen:	movl	w3,0x6E656770			; 'pgen'
	cmp	w0,w3
	b.ne	.pmod
	str	x2,[x19,SF_PGEN]
	ret
.pmod:	movl	w3,0x646F6D70			; 'pmod'
	cmp	w0,w3
	b.ne	.imod
	str	x2,[x19,SF_PMOD]
	ret
.imod:	movl	w3,0x646F6D69			; 'imod'
	cmp	w0,w3
	b.ne	.inst
	str	x2,[x19,SF_IMOD]
	ret
.inst:	movl	w3,0x74736E69			; 'inst'
	cmp	w0,w3
	b.ne	.ibag
	str	x2,[x19,SF_INST]
	ret
.ibag:	movl	w3,0x67616269			; 'ibag'
	cmp	w0,w3
	b.ne	.igen
	str	x2,[x19,SF_IBAG]
	ret
.igen:	movl	w3,0x6E656769			; 'igen'
	cmp	w0,w3
	b.ne	.shdr
	str	x2,[x19,SF_IGEN]
	ret
.shdr:	movl	w3,0x72646873			; 'shdr'
	cmp	w0,w3
	b.ne	.ret
	str	x2,[x19,SF_SHDR]
	mov	x3,46
	udiv	x1,x1,x3
	str	x1,[x19,SF_NSHDR]
.ret:	ret

; x0 = SoundFont -> son nom (texte)
ak_sf_name:
	add	x0,x0,SF_NAME
	ret

; x0 = SoundFont -> nombre de presets
ak_sf_presets:
	ldr	x0,[x0,SF_NPHDR]
	ret

; x0 = SoundFont, x1 = numero, x2 = tampon AKSP_SIZE -> 0 / < 0
ak_sf_preset:
	ldr	x3,[x0,SF_NPHDR]
	cmp	x1,x3
	b.hs	.bad
	ldr	x3,[x0,SF_PHDR]
	mov	x4,38
	mul	x4,x4,x1
	add	x3,x3,x4
	ldrh	w4,[x3,22]
	str	w4,[x2,AKSP_BANK]
	ldrh	w4,[x3,20]
	str	w4,[x2,AKSP_PROGRAM]
	add	x4,x2,AKSP_NAME
	mov	x5,0
.c:	ldrb	w6,[x3,x5]
	strb	w6,[x4,x5]
	add	x5,x5,1
	cmp	x5,20
	b.lo	.c
	strb	wzr,[x4,20]
	mov	x0,0
	ret
.bad:	mov	x0,AKE_INVAL
	ret

; ---------------------------------------------------------------------------
;  SoundFont : note
; ---------------------------------------------------------------------------

; x19 synthe, x20 canal -> w0 = preset + 1 (0 : aucun)
sf_find_preset:
	ldr	w0,[x20,SC_PRESET]
	cbnz	w0,.ret
	ldr	x1,[x19,SY_SF]
	ldr	w2,[x20,SC_PROG]
	ldr	w3,[x20,SC_BANK]
	add	x4,x19,SY_CHAN
	sub	x4,x20,x4
	lsr	x4,x4,6
	cmp	x4,9				; canal 10 : les percussions (banque 128)
	mov	w5,128
	csel	w3,w5,w3,eq
	ldr	x4,[x1,SF_PHDR]
	ldr	x5,[x1,SF_NPHDR]
	mov	x6,0
	mov	w7,0				; repli : meme programme, banque 0
	mov	w10,0				; repli : premier de la banque des percussions
.s:	cmp	x6,x5
	b.hs	.nf
	ldrh	w8,[x4,20]
	ldrh	w9,[x4,22]
	cmp	w9,w3
	b.ne	.a
	cmp	w8,w2
	b.ne	.a0
	add	w0,w6,1
	b	.st
.a0:	cbnz	w10,.n
	add	w10,w6,1
	b	.n
.a:	cbnz	w9,.n
	cmp	w8,w2
	b.ne	.n
	add	w7,w6,1
.n:	add	x4,x4,38
	add	x6,x6,1
	b	.s
.nf:	cmp	w3,128
	csel	w7,w10,w7,eq			; (percussions : un kit d'abord)
	mov	w0,w7
.st:	str	w0,[x20,SC_PRESET]
.ret:	ret

; x0 = generateurs, x1 = nombre, x2 = table s16[SF_NGEN] : x2[oper] = montant
; (l'ordre du fichier : le dernier gagne)
sf_apply:
	cbz	x1,.ret
	ldrh	w3,[x0]
	cmp	w3,SF_NGEN
	b.hs	.n
	ldrh	w4,[x0,2]
	strh	w4,[x2,x3,lsl 1]
.n:	add	x0,x0,4
	sub	x1,x1,1
	b	sf_apply
.ret:	ret

; x0 = generateurs, x1 = nombre, w21 touche, w22 velocite -> Z mis si la zone
; ne convient pas (le premier generateur d'etendue compte)
sf_zone_ok:
	cbz	x1,.yes
	ldrh	w3,[x0]
	cmp	w3,G_KEYRANGE
	b.ne	.v
	ldrb	w4,[x0,2]
	ldrb	w5,[x0,3]
	cmp	w21,w4
	b.lo	.no
	cmp	w21,w5
	b.hi	.no
	b	.n
.v:	cmp	w3,G_VELRANGE
	b.ne	.n
	ldrb	w4,[x0,2]
	ldrb	w5,[x0,3]
	cmp	w22,w4
	b.lo	.no
	cmp	w22,w5
	b.hi	.no
.n:	add	x0,x0,4
	sub	x1,x1,1
	b	sf_zone_ok
.yes:	mov	x0,1
	cmp	x0,0
	ret
.no:	cmp	xzr,xzr
	ret

; x20 canal, w0 = controleur -> w0 = sa valeur (0 : pas suivi)
sf_cc_value:
	cmp	w0,7
	b.ne	.c10
	ldr	w0,[x20,SC_VOL]
	ret
.c10:	cmp	w0,10
	b.ne	.c11
	ldr	w0,[x20,SC_PAN]
	ret
.c11:	cmp	w0,11
	b.ne	.c1
	ldr	w0,[x20,SC_EXPR]
	ret
.c1:	cmp	w0,1
	b.ne	.c91
	ldr	w0,[x20,SC_MOD]
	ret
.c91:	cmp	w0,91
	b.ne	.c93
	ldr	w0,[x20,SC_REV]
	ret
.c93:	cmp	w0,93
	b.ne	.no
	ldr	w0,[x20,SC_CHO]
	ret
.no:	mov	w0,0
	ret

; Les modulateurs de la SoundFont vus a l'attaque (SoundFont 2.04, 8.2 et 9.5.3) :
; sources velocite, touche, controleur ; sans source de quantite ; vers
; l'attenuation, la coupure, enveloppe -> coupure.
; x0 = modulateurs (10 o), x1 = nombre, x2 = modulateurs qui les masquent (meme
; source, destination, source de quantite), x3 = leur nombre ; x19, x20, w21, w22
sf_mods:
	prologue
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x23,x0
	mov	x24,x1
	mov	x25,x2
	mov	x26,x3
.m:	cbz	x24,.ret
	ldrh	w4,[x23,2]			; destination
	cmp	w4,48
	b.eq	.d
	cmp	w4,8
	b.eq	.d
	cmp	w4,11
	b.ne	.n
.d:	ldrh	w5,[x23,6]			; source de quantite
	tst	w5,0xFF
	b.ne	.n
	ldrh	w6,[x23]			; source
	mov	x7,x25				; masque ?
	mov	x8,x26
.h:	cbz	x8,.free
	ldrh	w9,[x7]
	cmp	w9,w6
	b.ne	.h1
	ldrh	w9,[x7,2]
	cmp	w9,w4
	b.ne	.h1
	ldrh	w9,[x7,6]
	cmp	w9,w5
	b.eq	.n
.h1:	add	x7,x7,10
	sub	x8,x8,1
	b	.h
.free:	mov	w9,0				; 1 : source velocite
	tbz	w6,7,.gc
	and	w0,w6,127			; un controleur
	bl	sf_cc_value
	b	.val
.gc:	and	w0,w6,127
	cmp	w0,2
	b.ne	.key
	mov	w0,w22
	mov	w9,1
	b	.val
.key:	cmp	w0,3
	b.ne	.n
	mov	w0,w21
.val:	tst	w6,0x100			; sens
	b.eq	.cur
	mov	w1,127
	sub	w0,w1,w0
.cur:	ubfx	w2,w6,10,6			; forme : 16.16 sur 0..1
	adr	x7,ak_concave
	cmp	w2,1
	b.ne	.cvx
	mov	w1,127				; concave
	sub	w1,w1,w0
	ldrsh	w0,[x7,x1,lsl 1]
	b	.db
.cvx:	cmp	w2,2
	b.ne	.sw
	ldrsh	w0,[x7,x0,lsl 1]		; convexe
	mov	w1,960
	sub	w0,w1,w0
.db:	lsl	w0,w0,16
	mov	w1,960
	udiv	w0,w0,w1
	b	.pol
.sw:	cmp	w2,3
	b.ne	.lin
	cmp	w0,64
	cset	w0,hs
	lsl	w0,w0,16
	b	.pol
.lin:	lsl	w0,w0,16
	mov	w1,127
	udiv	w0,w0,w1
.pol:	tst	w6,0x200			; bipolaire
	b.eq	.amt
	lsl	w0,w0,1
	sub	w0,w0,0x10000
.amt:	ldrsh	x1,[x23,4]			; quantite
	sxtw	x0,w0
	mul	x0,x0,x1
	asr	x0,x0,16
	ldrh	w4,[x23,2]
	cmp	w4,48
	b.ne	.f8
	ldr	w1,[x19,SY_MATT]
	add	w1,w1,w0
	str	w1,[x19,SY_MATT]
	cbz	w9,.n
	str	w9,[x19,SY_MVEL]
	b	.n
.f8:	cmp	w4,8
	b.ne	.f11
	ldr	w1,[x19,SY_MFC]
	add	w1,w1,w0
	str	w1,[x19,SY_MFC]
	b	.n
.f11:	ldr	w1,[x19,SY_MME]
	add	w1,w1,w0
	str	w1,[x19,SY_MME]
.n:	add	x23,x23,10
	sub	x24,x24,1
	b	.m
.ret:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	epilogue

; x19 synthe, x20 canal, w21 touche, w22 velocite (x0 = SoundFont)
sf_note_on:
	prologue
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,256+128			; pg[], ig[] : 2 x 128 ; variables
	PG	= 0
	IG	= 128
	V_PGLOB	= 256				; generateurs globaux du preset
	V_PGN	= 264
	V_IGLOB	= 272
	V_IGN	= 280
	V_ZEND	= 288				; fin des zones d'instrument
	V_PEND	= 296				; fin des zones du preset
	V_PMG	= 304				; modulateurs : preset, global et zone
	V_PMGN	= 312
	V_PML	= 320
	V_PMLN	= 328
	V_IMG	= 336				; instrument, global et zone
	V_IMGN	= 344
	V_IML	= 352
	V_IMLN	= 360
	mov	x23,x0				; SoundFont
	bl	sf_find_preset
	cbz	w0,.ret
	sub	w0,w0,1
	ldr	x1,[x23,SF_PHDR]
	mov	x2,38
	mul	x2,x2,x0
	add	x1,x1,x2
	ldrh	w24,[x1,24]			; premiere zone
	ldrh	w0,[x1,24+38]
	str	x0,[sp,V_PEND]
	str	xzr,[sp,V_PGLOB]
	str	xzr,[sp,V_PGN]
	str	xzr,[sp,V_PMG]
	str	xzr,[sp,V_PMGN]
	mov	x25,x24				; zone du preset
.pz:	ldr	x0,[sp,V_PEND]
	cmp	x25,x0
	b.hs	.ret
	ldr	x1,[x23,SF_PBAG]
	add	x1,x1,x25,lsl 2
	ldrh	w2,[x1,2]			; modulateurs de la zone
	ldrh	w3,[x1,6]
	subs	x3,x3,x2
	csel	x3,xzr,x3,lo
	ldr	x4,[x23,SF_PMOD]
	mov	x5,10
	mul	x5,x5,x2
	add	x4,x4,x5
	str	x4,[sp,V_PML]
	str	x3,[sp,V_PMLN]
	ldrh	w2,[x1]				; generateurs de la zone
	ldrh	w3,[x1,4]
	subs	x3,x3,x2
	b.ls	.pn
	ldr	x4,[x23,SF_PGEN]
	add	x26,x4,x2,lsl 2			; premier
	mov	x27,x3				; nombre
	add	x0,x26,x27,lsl 2
	ldurh	w1,[x0,-4]
	cmp	w1,G_INSTRUMENT
	b.eq	.pinst
	cmp	x25,x24				; pas d'instrument : zone globale
	b.ne	.pn
	str	x26,[sp,V_PGLOB]
	str	x27,[sp,V_PGN]
	ldr	x0,[sp,V_PML]
	str	x0,[sp,V_PMG]
	ldr	x0,[sp,V_PMLN]
	str	x0,[sp,V_PMGN]
	b	.pn
.pinst:	mov	x0,x26
	mov	x1,x27
	bl	sf_zone_ok
	b.eq	.pn
	add	x0,sp,PG			; pg : global puis zone
	mov	x1,128
	bl	ak_memzero
	ldr	x0,[sp,V_PGLOB]
	ldr	x1,[sp,V_PGN]
	add	x2,sp,PG
	bl	sf_apply
	mov	x0,x26
	mov	x1,x27
	add	x2,sp,PG
	bl	sf_apply
	add	x0,x26,x27,lsl 2
	ldurh	w0,[x0,-2]			; instrument
	ldr	x1,[x23,SF_INST]
	mov	x2,22
	mul	x2,x2,x0
	add	x1,x1,x2
	ldrh	w26,[x1,20]			; premiere zone
	ldrh	w0,[x1,20+22]
	str	x0,[sp,V_ZEND]
	str	xzr,[sp,V_IGLOB]
	str	xzr,[sp,V_IGN]
	str	xzr,[sp,V_IMG]
	str	xzr,[sp,V_IMGN]
	mov	x27,x26				; zone de l'instrument
.iz:	ldr	x0,[sp,V_ZEND]
	cmp	x27,x0
	b.hs	.pn
	ldr	x1,[x23,SF_IBAG]
	add	x1,x1,x27,lsl 2
	ldrh	w2,[x1,2]
	ldrh	w3,[x1,6]
	subs	x3,x3,x2
	csel	x3,xzr,x3,lo
	ldr	x4,[x23,SF_IMOD]
	mov	x5,10
	mul	x5,x5,x2
	add	x4,x4,x5
	str	x4,[sp,V_IML]
	str	x3,[sp,V_IMLN]
	ldrh	w2,[x1]
	ldrh	w3,[x1,4]
	subs	x3,x3,x2
	b.ls	.in
	ldr	x4,[x23,SF_IGEN]
	add	x4,x4,x2,lsl 2
	add	x0,x4,x3,lsl 2
	ldurh	w1,[x0,-4]
	cmp	w1,G_SAMPLEID
	b.eq	.isamp
	cmp	x27,x26
	b.ne	.in
	str	x4,[sp,V_IGLOB]
	str	x3,[sp,V_IGN]
	ldr	x0,[sp,V_IML]
	str	x0,[sp,V_IMG]
	ldr	x0,[sp,V_IMLN]
	str	x0,[sp,V_IMGN]
	b	.in
.isamp:	stp	x4,x3,[sp,-16]!
	mov	x0,x4
	mov	x1,x3
	bl	sf_zone_ok
	ldp	x4,x3,[sp],16
	b.eq	.in
	stp	x4,x3,[sp,-16]!
	add	x0,sp,IG+16			; ig : valeurs par defaut, global, zone
	adr	x1,sf_defaults
	mov	x2,SF_NGEN*2
	bl	ak_memmove
	ldr	x0,[sp,V_IGLOB+16]
	ldr	x1,[sp,V_IGN+16]
	add	x2,sp,IG+16
	bl	sf_apply
	ldp	x0,x1,[sp],16
	add	x2,sp,IG
	bl	sf_apply
	str	wzr,[x19,SY_MFC]		; modulateurs de cette voix
	str	wzr,[x19,SY_MME]
	str	wzr,[x19,SY_MATT]
	str	wzr,[x19,SY_MVEL]
	ldr	x0,[sp,V_PMG]
	ldr	x1,[sp,V_PMGN]
	ldr	x2,[sp,V_PML]
	ldr	x3,[sp,V_PMLN]
	bl	sf_mods
	ldr	x0,[sp,V_PML]
	ldr	x1,[sp,V_PMLN]
	mov	x2,0
	mov	x3,0
	bl	sf_mods
	ldr	x0,[sp,V_IMG]
	ldr	x1,[sp,V_IMGN]
	ldr	x2,[sp,V_IML]
	ldr	x3,[sp,V_IMLN]
	bl	sf_mods
	ldr	x0,[sp,V_IML]
	ldr	x1,[sp,V_IMLN]
	mov	x2,0
	mov	x3,0
	bl	sf_mods
	add	x0,sp,PG
	add	x1,sp,IG
	mov	x2,x23
	bl	sf_voice_start
.in:	add	x27,x27,1
	b	.iz
.pn:	add	x25,x25,1
	b	.pz
.ret:	add	sp,sp,256+128
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	epilogue

; w0 = centibels de temps (timecents) -> w0 = blocs (1 au moins)
sf_tc_blocks:
	prologue
	sxtw	x0,w0
	movl	x1,689*16			; blocs par seconde, 12.4
	bl	ak_cents_mul
	add	x0,x0,8
	lsr	x0,x0,4
	cmp	x0,0
	cinc	x0,x0,eq
	movl	x1,0x7FFFFF
	cmp	x0,x1
	csel	x0,x1,x0,hi
	epilogue

; x0 = enveloppe, w1 = delai, w2 = attaque, w3 = tenue, w4 = declin, w5 = niveau
; 16.16 de maintien, w6 = relachement (timecents)
sf_env_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	w20,w2
	mov	w21,w3
	mov	w22,w4
	str	w5,[x19,EN_SUS]
	mov	w23,w6
	mov	w0,w1
	bl	sf_tc_blocks
	cmp	w0,1				; (delai minimal : aucun)
	csel	w0,wzr,w0,ls
	str	w0,[x19,EN_DELAY]
	str	w0,[x19,EN_COUNT]
	mov	w0,w20
	bl	sf_tc_blocks
	mov	w1,0x10000
	udiv	w0,w1,w0
	cmp	w0,0
	cinc	w0,w0,eq
	str	w0,[x19,EN_ATTS]
	mov	w0,w21
	bl	sf_tc_blocks
	cmp	w0,1
	csel	w0,wzr,w0,ls
	str	w0,[x19,EN_HOLD]
	mov	w0,w22
	bl	sf_tc_blocks
	mov	w1,0x10000
	udiv	w0,w1,w0
	cmp	w0,0
	cinc	w0,w0,eq
	str	w0,[x19,EN_DECS]
	mov	w0,w23
	bl	sf_tc_blocks
	mov	w1,0x10000
	udiv	w0,w1,w0
	cmp	w0,0
	cinc	w0,w0,eq
	str	w0,[x19,EN_RELS]
	str	wzr,[x19,EN_LVL]
	ldr	w0,[x19,EN_COUNT]
	cmp	w0,0
	mov	w0,ES_ATTACK
	mov	w1,ES_DELAY
	csel	w0,w0,w1,eq
	str	w0,[x19,EN_STAGE]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = enveloppe -> w0 = niveau, w1 = etape : un bloc de plus
sf_env_step:
	ldr	w1,[x0,EN_STAGE]
	ldr	w2,[x0,EN_LVL]
	cmp	w1,ES_DELAY
	b.ne	.a
	ldr	w3,[x0,EN_COUNT]
	subs	w3,w3,1
	str	w3,[x0,EN_COUNT]
	b.hi	.out
	mov	w1,ES_ATTACK
	b	.st
.a:	cmp	w1,ES_ATTACK
	b.ne	.h
	ldr	w3,[x0,EN_ATTS]
	add	w2,w2,w3
	mov	w3,0x10000
	cmp	w2,w3
	b.lo	.out
	mov	w2,w3
	ldr	w3,[x0,EN_HOLD]
	str	w3,[x0,EN_COUNT]
	mov	w1,ES_HOLD
	cbnz	w3,.st
	mov	w1,ES_DECAY
	b	.st
.h:	cmp	w1,ES_HOLD
	b.ne	.d
	ldr	w3,[x0,EN_COUNT]
	subs	w3,w3,1
	str	w3,[x0,EN_COUNT]
	b.hi	.out
	mov	w1,ES_DECAY
	b	.st
.d:	cmp	w1,ES_DECAY
	b.ne	.r
	ldr	w3,[x0,EN_DECS]
	subs	w2,w2,w3
	csel	w2,wzr,w2,mi
	ldr	w3,[x0,EN_SUS]
	cmp	w2,w3
	b.gt	.out
	mov	w2,w3
	mov	w1,ES_SUSTAIN
	b	.st
.r:	cmp	w1,ES_RELEASE
	b.ne	.out
	ldr	w3,[x0,EN_RELS]
	subs	w2,w2,w3
	b.gt	.out
	mov	w2,0
	mov	w1,ES_DONE
.st:	str	w1,[x0,EN_STAGE]
.out:	str	w2,[x0,EN_LVL]
	mov	w0,w2
	ret

; x1 = voix : relachement (garde x0-x2, x5)
sv_release:
	mov	w3,1
	str	w3,[x1,SV_REL]
	add	x3,x1,SV_ME
	mov	w4,ES_RELEASE
	str	w4,[x3,EN_STAGE]
	add	x3,x1,SV_VE
	ldr	w6,[x3,EN_STAGE]
	str	w4,[x3,EN_STAGE]
	cmp	w6,ES_ATTACK
	b.hi	.ret
	; (attaque : niveau lineaire en amplitude -> lineaire en dB : 96 dB =
	; 16 octaves, soit 4096 unites 16.16 / ... log2 approche par morceaux)
	ldr	w6,[x3,EN_LVL]
	cbz	w6,.zero
	clz	w7,w6
	mov	w8,31
	sub	w8,w8,w7			; log2 entier
	lsl	w6,w6,w7			; 1.31
	lsl	w6,w6,1				; fraction
	lsr	w6,w6,16			; 16 bits : log2 approche par la droite
	add	w8,w6,w8,lsl 16			; log2(niveau) 16.16 (niveau 16.16)
	sub	w8,w8,0x100000			; log2(amplitude) <= 0
	asr	w8,w8,4				; / 16 octaves (96 dB)
	mov	w6,0x10000
	adds	w6,w6,w8
	csel	w6,wzr,w6,mi
	str	w6,[x3,EN_LVL]
	ret
.zero:	str	wzr,[x3,EN_LVL]
.ret:	ret

; x19 synthe -> x0 = voix libre (sinon la moins audible des relachees, sinon
; la plus ancienne, sur laquelle on continue)
sf_voice_alloc:
	add	x1,x19,SY_VOICES
	mov	x2,SY_NV
	mov	x3,0				; meilleure
	mov	w4,0				; 1 : relachee
	mov	w5,-1				; age ou niveau
.s:	ldr	w6,[x1,SV_STATE]
	cbz	w6,.free
	ldr	w6,[x1,SV_REL]
	cmp	w6,1
	cset	w6,eq
	cmp	w6,w4
	b.hi	.take
	b.lo	.n
	ldr	w7,[x1,SV_AGE]
	cmp	w7,w5
	b.hs	.n
.take:	mov	x3,x1
	mov	w4,w6
	ldr	w5,[x1,SV_AGE]
.n:	add	x1,x1,SV_SIZE
	subs	x2,x2,1
	b.ne	.s
	mov	x0,x3
	ret
.free:	mov	x0,x1
	ret

; x0 = pg[], x1 = ig[], x2 = SoundFont ; x19, x20, w21, w22 : la voix demarre
sf_voice_start:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x24,x0
	mov	x25,x1
	mov	x26,x2
	ldrh	w0,[x25,G_SAMPLEID*2]
	ldr	x1,[x26,SF_NSHDR]
	cmp	x0,x1
	b.hs	.ret
	ldr	x1,[x26,SF_SHDR]
	mov	x2,46
	mul	x2,x2,x0
	add	x27,x1,x2			; en-tete de l'echantillon
	bl	sf_voice_alloc
	mov	x23,x0
	ldr	x0,[x19,SY_AGE]
	add	x0,x0,1
	str	x0,[x19,SY_AGE]
	mov	x28,x0
	; classe exclusive (generateur 57) : coupe les autres du canal
	ldrsh	w0,[x25,57*2]
	bl	sy_chan_index
	mov	x8,x0
	ldrsh	w4,[x25,57*2]
	ldrsh	w5,[x24,57*2]
	add	w4,w4,w5
	cbz	w4,.noex
	add	x1,x19,SY_VOICES
	mov	x2,SY_NV
.x:	ldr	w3,[x1,SV_STATE]
	cbz	w3,.xn
	ldr	w3,[x1,SV_EXCL]
	cmp	w3,w4
	b.ne	.xn
	ldr	w3,[x1,SV_CHAN]
	cmp	x3,x8
	b.ne	.xn
	str	wzr,[x1,SV_STATE]
.xn:	add	x1,x1,SV_SIZE
	subs	x2,x2,1
	b.ne	.x
.noex:	mov	x9,x23				; voix a zero
	mov	x10,SV_SIZE
.z:	str	xzr,[x9],8
	subs	x10,x10,8
	b.ne	.z
	str	w8,[x23,SV_CHAN]
	str	w4,[x23,SV_EXCL]
	str	w21,[x23,SV_KEY]
	str	w22,[x23,SV_VEL]
	str	w28,[x23,SV_AGE]
	; touche et velocite effectives (generateurs 46, 47)
	ldrsh	w0,[x25,46*2]
	cmp	w0,0
	csel	w28,w0,w21,ge			; touche
	ldrsh	w0,[x25,47*2]
	cmp	w0,0
	csel	w20,w0,w22,ge			; velocite (x20 : le canal, garde sur la pile)
	; bornes : decalages (0..3, 4, 12, 45, 50 : additifs)
	ldr	x9,[x26,SF_NSMPL]
	ldr	w0,[x27,20]			; debut
	ldrsh	w1,[x25,0]
	ldrsh	w2,[x24,0]
	add	w1,w1,w2
	ldrsh	w2,[x25,4*2]
	ldrsh	w3,[x24,4*2]
	add	w2,w2,w3
	lsl	w2,w2,15
	add	w1,w1,w2
	add	x0,x0,w1,sxtw
	cmp	x0,x9
	b.hs	.ret
	lsl	x1,x0,32
	str	x1,[x23,SV_POS]
	ldr	w0,[x27,24]			; fin
	ldrsh	w1,[x25,1*2]
	ldrsh	w2,[x24,1*2]
	add	w1,w1,w2
	ldrsh	w2,[x25,12*2]
	ldrsh	w3,[x24,12*2]
	add	w2,w2,w3
	lsl	w2,w2,15
	add	w1,w1,w2
	add	x0,x0,w1,sxtw
	cmp	x0,x9
	csel	x0,x9,x0,hi
	str	x0,[x23,SV_END]
	ldr	w0,[x27,28]			; boucle
	ldrsh	w1,[x25,2*2]
	ldrsh	w2,[x24,2*2]
	add	w1,w1,w2
	ldrsh	w2,[x25,45*2]
	ldrsh	w3,[x24,45*2]
	add	w2,w2,w3
	lsl	w2,w2,15
	add	w1,w1,w2
	add	x0,x0,w1,sxtw
	str	x0,[x23,SV_LS]
	ldr	w0,[x27,32]
	ldrsh	w1,[x25,3*2]
	ldrsh	w2,[x24,3*2]
	add	w1,w1,w2
	ldrsh	w2,[x25,50*2]
	ldrsh	w3,[x24,50*2]
	add	w2,w2,w3
	lsl	w2,w2,15
	add	w1,w1,w2
	add	x0,x0,w1,sxtw
	str	x0,[x23,SV_LE]
	ldr	x1,[x23,SV_LS]
	cmp	x0,x1
	b.hi	.lok
	mov	w0,0				; boucle vide : pas de boucle
	b	.lmode
.lok:	ldrh	w0,[x25,54*2]
	and	w0,w0,3
	cmp	w0,2
	csel	w0,wzr,w0,eq
.lmode:	str	w0,[x23,SV_MODE]
	ldr	x0,[x26,SF_SMPL]
	str	x0,[x23,SV_SMP]
	; hauteur : (touche - racine) x accord + grossier + fin + correction
	ldrsh	w0,[x25,58*2]
	cmp	w0,0
	b.ge	.root
	ldrb	w0,[x27,40]
.root:	sub	w0,w28,w0
	ldrsh	w1,[x25,56*2]
	ldrsh	w2,[x24,56*2]
	add	w1,w1,w2
	mul	w0,w0,w1
	ldrsh	w1,[x25,51*2]
	ldrsh	w2,[x24,51*2]
	add	w1,w1,w2
	mov	w2,100
	mul	w1,w1,w2
	add	w0,w0,w1
	ldrsh	w1,[x25,52*2]
	add	w0,w0,w1
	ldrsh	w1,[x24,52*2]
	add	w0,w0,w1
	ldrsb	w1,[x27,41]
	add	w0,w0,w1
	str	w0,[x23,SV_CENTS]
	ldr	w0,[x27,36]			; frequence de l'echantillon
	lsl	x0,x0,20
	movl	x1,FM_RATE
	udiv	x0,x0,x1
	str	w0,[x23,SV_RATE]
	; modulations (generateurs additifs)
	mov	w2,5
	bl	.gen
	str	w0,[x23,SV_ML2P]
	mov	w2,6
	bl	.gen
	str	w0,[x23,SV_VL2P]
	mov	w2,7
	bl	.gen
	str	w0,[x23,SV_ME2P]
	mov	w2,8
	bl	.gen
	ldr	w1,[x19,SY_MFC]
	add	w0,w0,w1
	str	w0,[x23,SV_FC]
	mov	w2,9
	bl	.gen
	str	w0,[x23,SV_Q]
	mov	w2,10
	bl	.gen
	str	w0,[x23,SV_ML2F]
	mov	w2,11
	bl	.gen
	ldr	w1,[x19,SY_MME]
	add	w0,w0,w1
	str	w0,[x23,SV_ME2F]
	mov	w2,13
	bl	.gen
	str	w0,[x23,SV_ML2V]
	mov	w2,15
	bl	.gen
	str	w0,[x23,SV_CSEND]
	mov	w2,16
	bl	.gen
	str	w0,[x23,SV_RSEND]
	mov	w2,17
	bl	.gen
	str	w0,[x23,SV_PAN]
	; attenuation : generateur 48 x 0,4 (usage des SoundFont) + velocite
	mov	w2,48
	bl	.gen
	lsl	w0,w0,1
	mov	w1,5
	sdiv	w0,w0,w1
	ldr	w1,[x19,SY_MATT]
	add	w0,w0,w1
	ldr	w1,[x19,SY_MVEL]		; (la SoundFont remplace le modulateur par defaut)
	cbnz	w1,.novel
	adr	x1,ak_concave
	ldrsh	w1,[x1,x20,lsl 1]
	add	w0,w0,w1
.novel:	ldr	w1,[x23,SV_Q]			; un filtre resonant perd la moitie de sa resonance
	cmp	w1,0
	csel	w1,wzr,w1,lt
	add	w0,w0,w1,asr 1
	str	w0,[x23,SV_ATT]
	; LFO
	mov	w2,21
	bl	.gen
	bl	sf_tc_blocks
	str	w0,[x23,SV_MLDEL]
	mov	w2,22
	bl	.gen
	sxtw	x0,w0
	movl	x1,5096414
	bl	ak_cents_mul
	str	w0,[x23,SV_MLINC]
	mov	w2,23
	bl	.gen
	bl	sf_tc_blocks
	str	w0,[x23,SV_VLDEL]
	mov	w2,24
	bl	.gen
	sxtw	x0,w0
	movl	x1,5096414
	bl	ak_cents_mul
	str	w0,[x23,SV_VLINC]
	; enveloppe de modulation (25..32)
	mov	w2,27
	bl	.gen
	mov	w20,w0
	mov	w2,31
	bl	.gen
	mov	w1,60
	sub	w1,w1,w21
	mul	w0,w0,w1
	add	w20,w20,w0			; tenue
	mov	w2,28
	bl	.gen
	mov	w8,w0
	str	w8,[sp,-16]!
	mov	w2,32
	bl	.gen
	ldr	w8,[sp],16
	mov	w1,60
	sub	w1,w1,w21
	mul	w0,w0,w1
	add	w8,w8,w0			; declin
	stp	x20,x8,[sp,-16]!
	mov	w2,29
	bl	.gen
	mov	w1,1000
	sub	w0,w1,w0
	cmp	w0,0
	csel	w0,wzr,w0,lt
	lsl	w0,w0,16
	udiv	w0,w0,w1
	mov	w5,w0
	stp	x5,x5,[sp,-16]!
	mov	w2,30
	bl	.gen
	mov	w6,w0
	stp	x6,x6,[sp,-16]!
	mov	w2,25
	bl	.gen
	mov	w1,w0
	stp	x1,x1,[sp,-16]!
	mov	w2,26
	bl	.gen
	mov	w1,145				; attaque plus courte a forte velocite : x (145 - v) / 144
	sub	w1,w1,w22
	clz	w2,w1
	mov	w3,31
	sub	w3,w3,w2			; log2 entier
	add	w2,w2,1
	lsl	w1,w1,w2			; fraction (en haut du mot)
	lsr	w1,w1,22			; 10 bits
	mov	w2,1200
	mul	w3,w3,w2
	mul	w1,w1,w2
	add	w3,w3,w1,lsr 10			; 1200 log2 (145 - v), droite entre les octaves
	movl	w1,8604				; 1200 log2 144
	sub	w3,w3,w1
	add	w0,w0,w3
	mov	w2,w0
	ldp	x1,x7,[sp],16
	ldp	x6,x7,[sp],16
	ldp	x5,x7,[sp],16
	ldp	x3,x4,[sp],16
	add	x0,x23,SV_ME
	bl	sf_env_init
	; enveloppe de volume (33..40)
	mov	w2,35
	bl	.gen
	mov	w20,w0
	mov	w2,39
	bl	.gen
	mov	w1,60
	sub	w1,w1,w21
	mul	w0,w0,w1
	add	w20,w20,w0
	mov	w2,36
	bl	.gen
	mov	w8,w0
	str	w8,[sp,-16]!
	mov	w2,40
	bl	.gen
	ldr	w8,[sp],16
	mov	w1,60
	sub	w1,w1,w21
	mul	w0,w0,w1
	add	w8,w8,w0
	stp	x20,x8,[sp,-16]!
	mov	w2,37
	bl	.gen
	mov	w1,960
	cmp	w0,0
	csel	w0,wzr,w0,lt
	sub	w0,w1,w0
	cmp	w0,0
	csel	w0,wzr,w0,lt
	lsl	w0,w0,16
	udiv	w0,w0,w1
	mov	w5,w0
	stp	x5,x5,[sp,-16]!
	mov	w2,38
	bl	.gen
	mov	w6,w0
	stp	x6,x6,[sp,-16]!
	mov	w2,33
	bl	.gen
	mov	w1,w0
	stp	x1,x1,[sp,-16]!
	mov	w2,34
	bl	.gen
	mov	w2,w0
	ldp	x1,x7,[sp],16
	ldp	x6,x7,[sp],16
	ldp	x5,x7,[sp],16
	ldp	x3,x4,[sp],16
	add	x0,x23,SV_VE
	bl	sf_env_init
	movl	w0,0x7FFFFFFF
	str	w0,[x23,SV_LASTFC]
	mov	w0,1
	str	w0,[x23,SV_STATE]		; (la voix sonne : derniere ecriture)
.ret:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
; w2 = generateur -> w0 = ig + pg (x24 pg, x25 ig) ; ne touche que w0, w1
.gen:	ldrsh	w0,[x25,x2,lsl 1]
	ldrsh	w1,[x24,x2,lsl 1]
	add	w0,w0,w1
	ret

; valeurs par defaut des generateurs (SoundFont 2.04, 8.1.3)
sf_defaults:
	dh 0,0,0,0,0,0,0,0, 13500,0,0,0,0,0,0,0		; 0-15
	dh 0,0,0,0,0,-12000,0,-12000, 0,-12000,-12000,-12000,-12000,0,-12000,0	; 16-31
	dh 0,-12000,-12000,-12000,-12000,0,-12000,0, 0,0,0,0,0,0,-1,-1		; 32-47
	dh 0,0,0,0,0,0,0,0, 100,0,-1,0,0					; 48-60
	align 4

; ---------------------------------------------------------------------------
;  SoundFont : rendu d'une voix
; ---------------------------------------------------------------------------

; x0 = valeur LFO en phase 32 bits -> w0 = triangle -65536..65536
sf_tri:
	lsr	w0,w0,14			; 0..262143
	mov	w1,0x20000
	cmp	w0,w1
	b.hs	.d
	sub	w0,w0,0x10000
	ret					; montee : -1 .. 1
.d:	movl	w1,0x30000
	sub	w0,w1,w0
	ret

; x19 synthe, x20 voix : les trames du bloc ajoutees a SY_BL, SY_BR, SY_BV
sf_voice_block:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	; enveloppes
	add	x0,x20,SV_ME
	bl	sf_env_step
	mov	w21,w0				; enveloppe de modulation
	add	x0,x20,SV_VE
	bl	sf_env_step
	mov	w22,w0				; niveau de volume
	cmp	w1,ES_DONE
	b.eq	.kill
	cmp	w1,ES_DECAY			; maintien a -96 dB : fini
	b.lo	.alive
	cbz	w0,.kill
.alive:
	mov	w23,w1				; etape
	; LFO
	ldr	w0,[x20,SV_MLDEL]
	cbz	w0,.ml
	sub	w0,w0,1
	str	w0,[x20,SV_MLDEL]
	mov	w24,0
	b	.mlx
.ml:	ldr	w0,[x20,SV_MLPH]
	ldr	w1,[x20,SV_MLINC]
	add	w0,w0,w1
	str	w0,[x20,SV_MLPH]
	bl	sf_tri
	mov	w24,w0
.mlx:	ldr	w0,[x20,SV_VLDEL]
	cbz	w0,.vl
	sub	w0,w0,1
	str	w0,[x20,SV_VLDEL]
	mov	w25,0
	b	.vlx
.vl:	ldr	w0,[x20,SV_VLPH]
	ldr	w1,[x20,SV_VLINC]
	add	w0,w0,w1
	str	w0,[x20,SV_VLPH]
	bl	sf_tri
	mov	w25,w0
.vlx:	ldr	w0,[x20,SV_CHAN]
	add	x26,x19,SY_CHAN
	add	x26,x26,x0,lsl 6		; canal
	; attenuation (centibels)
	ldr	w0,[x20,SV_ATT]
	adr	x2,ak_concave
	ldr	w1,[x26,SC_VOL]
	ldrsh	w1,[x2,x1,lsl 1]
	add	w0,w0,w1
	ldr	w1,[x26,SC_EXPR]
	ldrsh	w1,[x2,x1,lsl 1]
	add	w0,w0,w1
	ldr	w1,[x20,SV_ML2V]
	mul	w1,w1,w24
	asr	w1,w1,16
	add	w0,w0,w1
	cmp	w23,ES_ATTACK
	b.ls	.notdb
	mov	w1,0x10000
	sub	w1,w1,w22
	mov	w2,960
	umull	x1,w1,w2
	lsr	x1,x1,16
	add	w0,w0,w1
.notdb:	cmp	w0,0
	csel	w0,wzr,w0,lt
	cmp	w0,1440				; muette
	b.lt	.aud
	mov	w0,1440
.aud:	mov	w1,1993				; 10^(-cB / 200) = 2^(-cB x 19,93 / 1200)
	mul	w0,w0,w1
	mov	w1,100
	udiv	w0,w0,w1
	neg	w0,w0
	sxtw	x0,w0
	movl	x1,1 shl 24
	bl	ak_cents_mul
	scvtf	s0,w0				; gain 8.24
	cmp	w23,ES_ATTACK
	b.hi	.nofade
	cmp	w23,ES_DELAY
	fmov	s1,wzr
	fcsel	s0,s1,s0,eq
	b.eq	.nofade
	scvtf	s1,w22				; attaque : le niveau en amplitude
	movl	w0,0x37800000			; 2^-16
	fmov	s2,w0
	fmul	s1,s1,s2
	fmul	s0,s0,s1
.nofade:
	movl	w0,0x2C000000			; 2^-39 : gain 8.24, echantillon 15 bits -> -1..1
	fmov	s1,w0
	fmul	s0,s0,s1
	ldr	s1,[x19,SY_GAIN]
	fmul	s0,s0,s1
	; panoramique : loi du sinus
	ldr	w0,[x20,SV_PAN]
	ldr	w1,[x26,SC_PAN]
	sub	w1,w1,64
	mov	w2,1000
	mul	w1,w1,w2
	mov	w2,127
	sdiv	w1,w1,w2
	add	w0,w0,w1
	mov	w1,500
	add	w0,w0,w1
	cmp	w0,0
	csel	w0,wzr,w0,lt
	mov	w1,1000
	cmp	w0,w1
	csel	w0,w1,w0,gt
	lsl	w0,w0,8				; 0..256 sur le quart de periode
	mov	w1,1000
	udiv	w0,w0,w1
	adr	x2,ak_sin1024
	ldrsh	w1,[x2,x0,lsl 1]		; droite
	mov	w3,256
	sub	w3,w3,w0
	ldrsh	w3,[x2,x3,lsl 1]		; gauche
	scvtf	s1,w1
	scvtf	s2,w3
	movl	w0,0x38000000			; 2^-15
	fmov	s3,w0
	fmul	s1,s1,s3
	fmul	s2,s2,s3
	fmul	s4,s0,s2			; gain gauche
	fmul	s5,s0,s1			; gain droite
	ldr	s6,[x20,SV_GL]			; gains du bloc precedent
	ldr	s7,[x20,SV_GR]
	str	s4,[x20,SV_GL]
	str	s5,[x20,SV_GR]
	; envoi de reverberation : generateur 16 (0,1 %) + CC 91
	ldr	w0,[x20,SV_RSEND]
	ldr	w1,[x26,SC_REV]
	mov	w2,200
	mul	w1,w1,w2
	mov	w2,127
	udiv	w1,w1,w2
	add	w0,w0,w1
	cmp	w0,0
	csel	w0,wzr,w0,lt
	scvtf	s16,w0
	movl	w0,0x3A83126F			; 0,001
	fmov	s17,w0
	fmul	s16,s16,s17
	fmul	s16,s16,s0			; gain de l'envoi
	; envoi de chorus : generateur 15 (0,1 %) + CC 93
	ldr	w0,[x20,SV_CSEND]
	ldr	w1,[x26,SC_CHO]
	mov	w2,200
	mul	w1,w1,w2
	mov	w2,127
	udiv	w1,w1,w2
	add	w0,w0,w1
	cmp	w0,0
	csel	w0,wzr,w0,lt
	mov	w28,w0				; 0 : pas d'envoi
	cbz	w0,.nocho
	mov	w1,1
	str	w1,[x19,SY_CACT]
.nocho:	scvtf	s26,w0
	movl	w0,0x3A83126F			; 0,001
	fmov	s17,w0
	fmul	s26,s26,s17
	fmul	s26,s26,s0
	; hauteur (cents)
	ldr	w0,[x20,SV_CENTS]
	ldr	w1,[x20,SV_ME2P]
	mul	w1,w1,w21
	asr	w1,w1,16
	add	w0,w0,w1
	ldr	w1,[x20,SV_ML2P]
	mul	w1,w1,w24
	asr	w1,w1,16
	add	w0,w0,w1
	ldr	w1,[x20,SV_VL2P]
	ldr	w2,[x26,SC_MOD]			; molette de modulation : 50 cents au plus
	mov	w3,50
	mul	w2,w2,w3
	mov	w3,127
	udiv	w2,w2,w3
	add	w1,w1,w2
	mul	w1,w1,w25
	asr	w1,w1,16
	add	w0,w0,w1
	ldr	w1,[x26,SC_BEND]
	ldr	w2,[x26,SC_RANGE]
	mul	w1,w1,w2
	asr	w1,w1,13
	add	w0,w0,w1
	sxtw	x0,w0
	ldr	w1,[x20,SV_RATE]
	stp	s4,s5,[sp,-16]!
	stp	s6,s7,[sp,-16]!
	str	s16,[sp,-16]!
	bl	ak_cents_mul
	lsl	x0,x0,12
	str	x0,[x20,SV_INC]
	; filtre
	ldr	w0,[x20,SV_FC]
	ldr	w1,[x20,SV_ME2F]
	mul	w1,w1,w21
	asr	w1,w1,16
	add	w0,w0,w1
	ldr	w1,[x20,SV_ML2F]
	mul	w1,w1,w24
	asr	w1,w1,16
	add	w0,w0,w1
	mov	w1,1500
	cmp	w0,w1
	csel	w0,w1,w0,lt
	ldr	w1,[x20,SV_LASTFC]
	cmp	w0,w1
	b.eq	.fok
	str	w0,[x20,SV_LASTFC]
	bl	sf_filter_set
.fok:	ldr	s16,[sp],16
	ldp	s6,s7,[sp],16
	ldp	s4,s5,[sp],16
	; pentes des gains
	movl	w0,0x3C800000			; 1 / 64
	fmov	s17,w0
	fsub	s18,s4,s6
	fmul	s18,s18,s17			; pas gauche
	fsub	s19,s5,s7
	fmul	s19,s19,s17			; pas droit
	; les trames
	add	x21,x19,SY_BL
	add	x22,x19,SY_BR
	add	x23,x19,SY_BV
	add	x27,x19,SY_BC
	mov	x24,SY_BLOCK
	ldr	x9,[x20,SV_POS]
	ldr	x10,[x20,SV_INC]
	ldr	x11,[x20,SV_SMP]
	ldr	x12,[x20,SV_END]
	sub	x12,x12,1			; dernier indice lisible
	ldr	x13,[x20,SV_LS]
	ldr	x14,[x20,SV_LE]
	ldr	w15,[x20,SV_MODE]
	ldr	w0,[x20,SV_REL]
	cmp	w15,3				; boucle jusqu'au relachement
	b.ne	.nom3
	cmp	w0,1
	mov	w1,0
	csel	w15,w1,w15,eq
.nom3:	ldr	s20,[x20,SV_Z1]
	ldr	s21,[x20,SV_Z2]
	ldr	s22,[x20,SV_B0]
	ldr	s23,[x20,SV_B1]
	ldr	s24,[x20,SV_A1]
	ldr	s25,[x20,SV_A2]
	ldr	w0,[x20,SV_LASTFC]
	mov	w1,13500
	cmp	w0,w1
	cset	w25,hs				; 1 : sans filtre
	sub	x13,x13,x14			; - longueur de boucle
	lsl	x13,x13,32
.s:	lsr	x1,x9,32
	cbz	w15,.once
	cmp	x1,x14
	b.lo	.read
	add	x9,x9,x13
	lsr	x1,x9,32
	b	.read
.once:	cmp	x1,x12
	b.hs	.end
.read:	add	x3,x11,x1,lsl 1
	ldrsh	w4,[x3]
	ldrsh	w5,[x3,2]
	ubfx	x6,x9,16,16
	sub	w5,w5,w4
	mul	w5,w5,w6
	add	w4,w4,w5,asr 16
	scvtf	s0,w4
	cbnz	w25,.nof
	fmul	s1,s0,s22			; b0 x
	fadd	s1,s1,s20			; y
	fmul	s2,s0,s23			; b1 x
	fmul	s3,s1,s24
	fsub	s2,s2,s3
	fadd	s20,s2,s21			; z1
	fmul	s2,s0,s22			; b2 = b0
	fmul	s3,s1,s25
	fsub	s21,s2,s3			; z2
	fmov	s0,s1
.nof:	ldr	s1,[x21]
	fmul	s2,s0,s6
	fadd	s1,s1,s2
	str	s1,[x21],4
	ldr	s1,[x22]
	fmul	s2,s0,s7
	fadd	s1,s1,s2
	str	s1,[x22],4
	ldr	s1,[x23]
	fmul	s2,s0,s16
	fadd	s1,s1,s2
	str	s1,[x23],4
	cbz	w28,.noc
	ldr	s1,[x27]
	fmul	s2,s0,s26
	fadd	s1,s1,s2
	str	s1,[x27]
	add	x27,x27,4
.noc:	fadd	s6,s6,s18
	fadd	s7,s7,s19
	add	x9,x9,x10
	subs	x24,x24,1
	b.ne	.s
	str	x9,[x20,SV_POS]
	str	s20,[x20,SV_Z1]
	str	s21,[x20,SV_Z2]
	b	.ret
.end:	str	wzr,[x20,SV_STATE]
	b	.ret
.kill:	str	wzr,[x20,SV_STATE]
.ret:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x20 = voix, w0 = coupure (cents absolus) : coefficients du filtre (biquad
; passe-bas ; a0 = 1 ; b2 = b0) ; >= 13500 : sans filtre
sf_filter_set:
	prologue
	mov	w1,13500
	cmp	w0,w1
	b.hs	.ret
	sxtw	x0,w0
	movl	x1,8176
	bl	ak_cents_mul			; milli-Hz
	movl	x1,20000000
	cmp	x0,x1
	csel	x0,x1,x0,hi
	; angle sur la table : f x 1024 / 44100, 8 bits de fraction
	lsl	x0,x0,18
	movl	x1,44100000
	udiv	x0,x0,x1
	lsr	x1,x0,8
	and	x0,x0,255
	and	x1,x1,1023
	adr	x2,ak_sin1024
	ldrsh	w3,[x2,x1,lsl 1]
	add	x4,x1,1
	and	x4,x4,1023
	ldrsh	w4,[x2,x4,lsl 1]
	sub	w4,w4,w3
	mul	w4,w4,w0
	add	w3,w3,w4,asr 8			; sinus
	add	x4,x1,256
	and	x4,x4,1023
	ldrsh	w5,[x2,x4,lsl 1]
	add	x4,x4,1
	and	x4,x4,1023
	ldrsh	w6,[x2,x4,lsl 1]
	sub	w6,w6,w5
	mul	w6,w6,w0
	add	w5,w5,w6,asr 8			; cosinus
	scvtf	s0,w3
	scvtf	s1,w5
	movl	w0,0x38000000			; 2^-15
	fmov	s2,w0
	fmul	s0,s0,s2			; sin
	fmul	s1,s1,s2			; cos
	; resonance : Q = 0,7071 x 10^(cB / 200) (generateur 9)
	ldr	w0,[x20,SV_Q]
	cmp	w0,0
	csel	w0,wzr,w0,lt
	mov	w1,960
	cmp	w0,w1
	csel	w0,w1,w0,gt
	mov	w1,1993
	mul	w0,w0,w1
	mov	w1,100
	udiv	w0,w0,w1
	sxtw	x0,w0
	mov	x1,0x10000
	stp	s0,s1,[sp,-16]!
	bl	ak_cents_mul
	ldp	s0,s1,[sp],16
	scvtf	s2,w0				; Q x 65536
	movl	w0,0x37800000			; 2^-16
	fmov	s3,w0
	fmul	s2,s2,s3			; Q
	movl	w0,0x3F3504F3			; 0,7071 : Q minimal
	fmov	s3,w0
	fmul	s2,s2,s3
	fadd	s3,s2,s2			; 2 Q
	fdiv	s4,s0,s3			; alpha
	fmov	s5,1.0
	fadd	s6,s5,s4			; a0
	fsub	s7,s5,s1			; 1 - cos
	fdiv	s7,s7,s6			; b1
	fmov	s17,0.5
	fmul	s16,s7,s17			; b0 = b2
	fadd	s18,s1,s1
	fdiv	s18,s18,s6
	fneg	s18,s18				; a1
	fsub	s19,s5,s4
	fdiv	s19,s19,s6			; a2
	str	s16,[x20,SV_B0]
	str	s7,[x20,SV_B1]
	str	s18,[x20,SV_A1]
	str	s19,[x20,SV_A2]
.ret:	epilogue

; ---------------------------------------------------------------------------
;  Reverberation (huit filtres en peigne, quatre passe-tout, de chaque cote)
; ---------------------------------------------------------------------------
RV_NC		= 8
RV_NA		= 4
RVL_POS		= 0		; ligne : position, longueur, memoire du filtre, donnees
RVL_LEN		= 4
RVL_ST		= 8
RVL_BUF		= 16
RVL_SIZE	= 24
RV_FEEDBACK	= 0x3F570A3D	; 0,84
RV_DAMP		= 0x3E4CCCCD	; 0,2
RV_DAMP1	= 0x3F4CCCCD	; 0,8
RV_INGAIN	= 0x3C75C28F	; 0,015
RV_WET		= 0x3F800000	; 1

RV_LINES	= 0
RV_MEM		= RVL_SIZE * (RV_NC + RV_NA) * 2

; -> x0 = reverberation (0 : pas de memoire)
rv_new:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x1,rv_sizes
	mov	x2,0
	mov	x3,0
.t:	ldrh	w4,[x1,x2,lsl 1]
	add	x3,x3,x4
	add	x2,x2,1
	cmp	x2,RV_NC+RV_NA
	b.lo	.t
	lsl	x3,x3,1				; (deux cotes)
	add	x3,x3,(RV_NC+RV_NA)*2*23	; (ecart stereo)
	lsl	x3,x3,2				; flottants
	add	x0,x3,RV_MEM
	bl	ak_alloc
	cbz	x0,.out
	mov	x19,x0
	add	x20,x0,RV_MEM			; donnees
	mov	x21,0				; ligne
	adr	x22,rv_sizes
.l:	mov	x2,RVL_SIZE
	mul	x2,x2,x21
	add	x2,x19,x2
	mov	x3,RV_NC+RV_NA
	udiv	x4,x21,x3
	msub	x5,x4,x3,x21			; numero dans le cote
	ldrh	w5,[x22,x5,lsl 1]
	mov	x6,23
	mul	x4,x4,x6
	add	x5,x5,x4			; (droite : 23 de plus)
	str	w5,[x2,RVL_LEN]
	str	x20,[x2,RVL_BUF]
	add	x20,x20,x5,lsl 2
	add	x21,x21,1
	cmp	x21,(RV_NC+RV_NA)*2
	b.lo	.l
	mov	x0,x19
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x19 = synthe : l'envoi (SY_BV) repasse dans SY_BL, SY_BR
rv_block:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldr	x21,[x19,SY_RVB]
	cbz	x21,.ret
	sub	sp,sp,SY_BLOCK*8		; sortie gauche, droite
	movl	w0,RV_INGAIN
	fmov	s16,w0
	add	x0,x19,SY_BV
	mov	x1,SY_BLOCK
.in:	ldr	s0,[x0]
	fmul	s0,s0,s16
	str	s0,[x0],4
	subs	x1,x1,1
	b.ne	.in
	mov	x22,sp				; cote
	mov	x23,0
.side:	mov	x0,x22
	mov	x1,SY_BLOCK*4
	bl	ak_memzero
	mov	x24,RV_NC			; peignes
	movl	w0,RV_FEEDBACK
	fmov	s17,w0
	movl	w0,RV_DAMP
	fmov	s18,w0
	movl	w0,RV_DAMP1
	fmov	s19,w0
.comb:	ldr	w2,[x21,RVL_POS]
	ldr	w3,[x21,RVL_LEN]
	ldr	s1,[x21,RVL_ST]
	ldr	x4,[x21,RVL_BUF]
	add	x5,x19,SY_BV
	mov	x6,x22
	mov	x7,SY_BLOCK
.c:	ldr	s2,[x4,x2,lsl 2]		; y
	fmul	s3,s2,s19
	fmul	s1,s1,s18
	fadd	s1,s1,s3			; memoire amortie
	ldr	s3,[x5],4
	fmul	s4,s1,s17
	fadd	s3,s3,s4
	str	s3,[x4,x2,lsl 2]
	ldr	s3,[x6]
	fadd	s3,s3,s2
	str	s3,[x6],4
	add	x2,x2,1
	cmp	x2,x3
	csel	x2,xzr,x2,hs
	subs	x7,x7,1
	b.ne	.c
	str	w2,[x21,RVL_POS]
	str	s1,[x21,RVL_ST]
	add	x21,x21,RVL_SIZE
	subs	x24,x24,1
	b.ne	.comb
	mov	x24,RV_NA			; passe-tout
.all:	ldr	w2,[x21,RVL_POS]
	ldr	w3,[x21,RVL_LEN]
	ldr	x4,[x21,RVL_BUF]
	mov	x6,x22
	mov	x7,SY_BLOCK
	fmov	s5,0.5
.a:	ldr	s1,[x4,x2,lsl 2]		; b
	ldr	s0,[x6]				; x
	fsub	s2,s1,s0			; sortie
	fmul	s3,s1,s5
	fadd	s3,s3,s0
	str	s3,[x4,x2,lsl 2]
	str	s2,[x6],4
	add	x2,x2,1
	cmp	x2,x3
	csel	x2,xzr,x2,hs
	subs	x7,x7,1
	b.ne	.a
	str	w2,[x21,RVL_POS]
	add	x21,x21,RVL_SIZE
	subs	x24,x24,1
	b.ne	.all
	add	x22,x22,SY_BLOCK*4
	add	x23,x23,1
	cmp	x23,2
	b.lo	.side
	; mix
	movl	w0,RV_WET
	fmov	s16,w0
	add	x0,x19,SY_BL
	add	x1,x19,SY_BR
	mov	x2,sp
	add	x3,sp,SY_BLOCK*4
	mov	x4,SY_BLOCK
.m:	ldr	s0,[x2],4
	fmul	s0,s0,s16
	ldr	s1,[x0]
	fadd	s1,s1,s0
	str	s1,[x0],4
	ldr	s0,[x3],4
	fmul	s0,s0,s16
	ldr	s1,[x1]
	fadd	s1,s1,s0
	str	s1,[x1],4
	subs	x4,x4,1
	b.ne	.m
	add	sp,sp,SY_BLOCK*8
.ret:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

rv_sizes	dh 1116,1188,1277,1356,1422,1491,1557,1617, 556,441,341,225
		align 4

; ---------------------------------------------------------------------------
;  Chorus : une ligne a retard modulee, en opposition a gauche et a droite
; ---------------------------------------------------------------------------
CH_POS		= 0		; ecriture
CH_PHASE	= 4		; LFO
CH_IDLE		= 8		; blocs restants apres le dernier envoi
CH_DL		= 12		; retards du bloc precedent (24.8)
CH_DR		= 16
CH_BUF		= 32
CH_LEN		= 2048		; ~46 ms (puissance de 2)
CH_SIZE		= CH_BUF + CH_LEN * 4 + 4
CH_DELAY	= 882		; 20 ms
CH_DEPTH	= 220		; +- 5 ms
CH_LFO		= 2493000	; 0,4 Hz par bloc de 64 trames

; x19 = synthe : l'envoi (SY_BC) repasse dans SY_BL, SY_BR
ch_block:
	prologue
	stp	x20,x21,[sp,-16]!
	ldr	x20,[x19,SY_CHO]
	cbz	x20,.ret
	ldr	w0,[x19,SY_CACT]
	cbz	w0,.idle
	str	wzr,[x19,SY_CACT]
	mov	w0,40				; (tout le retard, puis on s'arrete)
	str	w0,[x20,CH_IDLE]
.idle:	ldr	w0,[x20,CH_IDLE]
	cbz	w0,.ret
	sub	w0,w0,1
	str	w0,[x20,CH_IDLE]
	ldr	w0,[x20,CH_PHASE]		; retards du bloc (24.8)
	movl	w1,CH_LFO
	add	w0,w0,w1
	str	w0,[x20,CH_PHASE]
	lsr	w0,w0,22
	adr	x1,ak_sin1024
	ldrsh	w0,[x1,x0,lsl 1]
	mov	w1,CH_DEPTH
	mul	w0,w0,w1
	asr	w0,w0,7				; 24.8
	movl	w1,CH_DELAY shl 8
	add	w2,w1,w0			; gauche
	sub	w3,w1,w0			; droite
	ldr	w4,[x20,CH_DL]
	ldr	w5,[x20,CH_DR]
	cbnz	w4,.have
	mov	w4,w2				; (premier bloc)
	mov	w5,w3
.have:	str	w2,[x20,CH_DL]
	str	w3,[x20,CH_DR]
	sub	w2,w2,w4
	asr	w2,w2,6				; pas par trame
	sub	w3,w3,w5
	asr	w3,w3,6
	ldr	w6,[x20,CH_POS]
	add	x7,x20,CH_BUF
	add	x8,x19,SY_BC
	add	x9,x19,SY_BL
	add	x10,x19,SY_BR
	mov	x11,SY_BLOCK
	movl	w12,CH_LEN-1
	movl	w0,0x3B800000			; 2^-8
	fmov	s5,w0
.s:	ldr	w0,[x8],4
	str	w0,[x7,x6,lsl 2]
	sub	w0,w4,w6,lsl 8			; gauche : position lue
	neg	w0,w0
	bl	.tap
	ldr	s1,[x9]
	fadd	s1,s1,s0
	str	s1,[x9],4
	sub	w0,w5,w6,lsl 8			; droite
	neg	w0,w0
	bl	.tap
	ldr	s1,[x10]
	fadd	s1,s1,s0
	str	s1,[x10],4
	add	w4,w4,w2
	add	w5,w5,w3
	add	w6,w6,1
	and	w6,w6,w12
	subs	x11,x11,1
	b.ne	.s
	str	w6,[x20,CH_POS]
.ret:	ldp	x20,x21,[sp],16
	epilogue
; w0 = position 24.8 (modulo la ligne) -> s0 = echantillon interpole
.tap:	and	w13,w0,255
	lsr	w0,w0,8
	and	w0,w0,w12
	add	w14,w0,1
	and	w14,w14,w12
	ldr	s0,[x7,x0,lsl 2]
	ldr	s1,[x7,x14,lsl 2]
	fsub	s1,s1,s0
	scvtf	s2,w13
	fmul	s2,s2,s5
	fmul	s1,s1,s2
	fadd	s0,s0,s1
	ret

; ---------------------------------------------------------------------------
;  Rendu
; ---------------------------------------------------------------------------

; x19 = synthe (verrou pris) : un bloc dans SY_BL, SY_BR
sy_block:
	prologue
	stp	x20,x21,[sp,-16]!
	ldr	x0,[x19,SY_FM]
	cbnz	x0,.fm
	add	x0,x19,SY_BL
	movl	x1,SY_BLOCK*16
	bl	ak_memzero
	add	x20,x19,SY_VOICES
	mov	x21,SY_NV
.v:	ldr	w0,[x20,SV_STATE]
	cbz	w0,.n
	bl	sf_voice_block
.n:	add	x20,x20,SV_SIZE
	subs	x21,x21,1
	b.ne	.v
	bl	rv_block
	bl	ch_block
	b	.ret
.fm:	sub	sp,sp,SY_BLOCK*4
	mov	x1,sp
	mov	x2,SY_BLOCK
	bl	ak_fm_render
	mov	x1,sp
	add	x2,x19,SY_BL
	add	x3,x19,SY_BR
	mov	x4,SY_BLOCK
	movl	w0,0x38000000			; 2^-15
	fmov	s1,w0
.c:	ldrsh	w0,[x1],2
	scvtf	s0,w0
	fmul	s0,s0,s1
	str	s0,[x2],4
	ldrsh	w0,[x1],2
	scvtf	s0,w0
	fmul	s0,s0,s1
	str	s0,[x3],4
	subs	x4,x4,1
	b.ne	.c
	add	sp,sp,SY_BLOCK*4
.ret:	ldp	x20,x21,[sp],16
	epilogue

; x0 = synthe, x1 = gauche, x2 = droite (flottants, -1..1), x3 = trames -> 0 :
; ECRIT les trames suivantes
ak_synth_render_f32:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
.l:	cbz	x22,.done
	ldr	x4,[x19,SY_BPOS]
	cmp	x4,SY_BLOCK
	b.lo	.have
	mov	x0,x19
	bl	fm_lock
	bl	sy_block
	mov	x0,x19
	bl	fm_unlock
	mov	x4,0
.have:	mov	x5,SY_BLOCK
	sub	x5,x5,x4
	cmp	x5,x22
	csel	x5,x5,x22,lo
	add	x6,x19,SY_BL
	add	x6,x6,x4,lsl 2
	add	x7,x19,SY_BR
	add	x7,x7,x4,lsl 2
	add	x4,x4,x5
	str	x4,[x19,SY_BPOS]
	sub	x22,x22,x5
.c:	ldr	w0,[x6],4
	str	w0,[x20],4
	ldr	w0,[x7],4
	str	w0,[x21],4
	subs	x5,x5,1
	b.ne	.c
	b	.l
.done:	mov	x0,0
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = synthe, x1 = sortie s16 stereo, x2 = trames -> 0
ak_synth_render:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,SY_BLOCK*8
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
.l:	cbz	x21,.done
	mov	x22,SY_BLOCK
	cmp	x21,x22
	csel	x22,x21,x22,lo
	mov	x0,x19
	mov	x1,sp
	add	x2,sp,SY_BLOCK*4
	mov	x3,x22
	bl	ak_synth_render_f32
	mov	x1,sp
	add	x2,sp,SY_BLOCK*4
	mov	x3,x22
	movl	w0,0x47000000			; 32768
	fmov	s1,w0
	mov	w4,32767
	mov	w5,-32768
.c:	ldr	s0,[x1],4
	fmul	s0,s0,s1
	fcvtns	w0,s0
	cmp	w0,w4
	csel	w0,w4,w0,gt
	cmp	w0,w5
	csel	w0,w5,w0,lt
	strh	w0,[x20],2
	ldr	s0,[x2],4
	fmul	s0,s0,s1
	fcvtns	w0,s0
	cmp	w0,w4
	csel	w0,w4,w0,gt
	cmp	w0,w5
	csel	w0,w5,w0,lt
	strh	w0,[x20],2
	subs	x3,x3,1
	b.ne	.c
	sub	x21,x21,x22
	b	.l
.done:	mov	x0,0
	add	sp,sp,SY_BLOCK*8
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include 'ak_sftab.inc'
include 'ak_gmbank.inc'
