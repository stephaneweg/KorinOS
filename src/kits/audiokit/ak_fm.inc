; ============================================================================
;  kits/audiokit/ak_fm.inc - synthese FM d'AudioKit (docs/SON.md 9, docs/KITS.md)
;
;  Des voix, chacune une onde simple (carre, sinus, triangle, scie, bruit) ou un
;  instrument FM a deux operateurs (a la maniere de l'OPL2 : AKFI_*), en
;  arithmetique entiere : d'apres fmsynth.h d'Onyx (code de l'utilisateur), memes
;  calculs au bit pres pour une voix au centre (tools/audiotest/fmref.cpp).
;  L'enveloppe d'un operateur travaille sur une ATTENUATION en 1/256 d'octave
;  (6,02 dB / 256 ; 4096 unites = 96 dB = silence), en virgule fixe 16.16.
;
;  Un synthe (ak_fm_new) est un bloc de memoire a soi ; ses fonctions se
;  protegent par un verrou (une attente courte : un rendu tient 64 trames) :
;  un thread peut rendre (ak_fm_render, ak_fm_live) pendant qu'un autre joue.
; ============================================================================

; --- operateur (32 bits) ------------------------------------------------------
FOP_PHASE	= 0
FOP_MULT2	= 4		; multiplicateur x 2
FOP_STAGE	= 8		; EG_*
FOP_ATT		= 12		; attenuation 16.16
FOP_ATTK	= 16		; attaque : facteur exponentiel 16.16 par trame
FOP_DECINC	= 20		; declin : unites 16.16 par trame
FOP_RELINC	= 24		; relachement
FOP_SUSATT	= 28		; niveau de maintien (unites)
FOP_LEVATT	= 32		; niveau de l'operateur (unites)
FOP_WAVE	= 36		; 0 sinus, 1 demi, 2 absolu, 3 impulsions
FOP_SUS		= 40		; 1 : tenu
FOP_TREM	= 44
FOP_VIB		= 48
FOP_SIZE	= 64

EG_OFF		= 0
EG_ATTACK	= 1
EG_DECAY	= 2
EG_SUSTAIN	= 3
EG_RELEASE	= 4

; --- voix ----------------------------------------------------------------------
FMV_ON		= 0		; touche enfoncee
FMV_PHASE	= 4
FMV_INC		= 8		; pas de phase (32 bits)
FMV_WAVE	= 12		; AKW_*
FMV_GAIN	= 16		; 0..65535 (FM : le volume)
FMV_TARGET	= 20
FMV_NOISE	= 24		; registre a decalage du bruit
FMV_NOISEV	= 28
FMV_FEEDBACK	= 32
FMV_CONN	= 36		; 1 additif
FMV_FB1		= 40		; deux dernieres sorties du modulateur
FMV_FB2		= 44
FMV_HASFM	= 48
FMV_GL		= 52		; panoramique : 0..256 de chaque cote
FMV_GR		= 56
FMV_OP0		= 64		; modulateur
FMV_OP1		= FMV_OP0 + FOP_SIZE	; porteuse
FMV_SIZE	= FMV_OP1 + FOP_SIZE

; --- synthe --------------------------------------------------------------------
AKFM_LOCK	= 0
AKFM_NV		= 8		; nombre de voix
AKFM_LFOT	= 16		; phases : tremolo 3,7 Hz, vibrato 6,1 Hz (32 bits)
AKFM_LFOV	= 20
AKFM_LIVE	= 24		; thread de sortie (ak_fm_live) : son bloc
AKFM_VOICES	= 64

FM_ATT_MAX	= 4096		; 96 dB
FM_RATE		= 44100
FM_PIECE	= 64		; trames rendues sous le verrou

; ---------------------------------------------------------------------------
;  Verrou (x0 = synthe ; ne touche que x9-x11)
; ---------------------------------------------------------------------------
fm_lock:
	add	x9,x0,AKFM_LOCK
.t:	ldaxr	w10,[x9]
	cbnz	w10,.w
	mov	w10,1
	stxr	w11,w10,[x9]
	cbnz	w11,.t
	ret
.w:	clrex
	yield
	b	.t

fm_unlock:
	add	x9,x0,AKFM_LOCK
	stlr	wzr,[x9]
	ret

; ---------------------------------------------------------------------------
;  Creation
; ---------------------------------------------------------------------------

; x0 = voix (1..64 ; 0 : 16) -> synthe, < 0 : erreur
ak_fm_new:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbnz	x19,.n
	mov	x19,16
.n:	cmp	x19,AK_FM_MAXV
	b.hi	.inval
	mov	x0,FMV_SIZE
	mul	x0,x0,x19
	add	x0,x0,AKFM_VOICES
	bl	ak_alloc
	cbz	x0,.nomem
	str	x19,[x0,AKFM_NV]
	add	x1,x0,AKFM_VOICES
	mov	x2,0
.v:	cmp	x2,x19
	b.hs	.out
	movl	w3,0xACE1			; (bruit : une graine par voix)
	add	w3,w3,w2
	str	w3,[x1,FMV_NOISE]
	mov	w3,256
	str	w3,[x1,FMV_GL]
	str	w3,[x1,FMV_GR]
	add	x1,x1,FMV_SIZE
	add	x2,x2,1
	b	.v
.inval:	mov	x0,AKE_INVAL
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = synthe -> 0 (arrete ak_fm_live s'il tourne)
ak_fm_free:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
if ~ defined AKHOST
	ldr	x1,[x19,AKFM_LIVE]
	cbz	x1,.f
	mov	x1,-1
	bl	ak_fm_live
.f:
end if
	mov	x0,x19
	bl	ak_free
.out:	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; x0 = synthe, x1 = voix -> x2 = voix (Z mis) ; Z baisse si le numero est mauvais
fm_voice:
	ldr	x2,[x0,AKFM_NV]
	cmp	x1,x2
	b.hs	.bad
	mov	x2,FMV_SIZE
	mul	x2,x2,x1
	add	x2,x2,AKFM_VOICES
	add	x2,x0,x2
	cmp	xzr,xzr
	ret
.bad:	mov	x2,1
	cmp	x2,0
	ret

; ---------------------------------------------------------------------------
;  Instruments
; ---------------------------------------------------------------------------

; x0 = rang (0..15) -> w0 = facteur 16.16 d'attaque par trame : 0..100 % en
; 2826 ms au rang 1, moitie moins par rang (15 : instantane)
fm_attack_k:
	cbz	w0,.ret
	cmp	w0,15
	b.hs	.inst
	sub	w1,w0,1
	movl	x0,543949
	lsl	x0,x0,x1
	movl	x1,124637
	udiv	x0,x0,x1
	cmp	x0,0
	cinc	x0,x0,eq
	ret
.inst:	mov	w0,-1
.ret:	ret

; x0 = rang -> w0 = unites 16.16 par trame : 96 dB en 39280 ms au rang 1, moitie
; moins par rang (0 : jamais)
fm_linear_inc:
	cbz	w0,.ret
	sub	w1,w0,1
	mov	x0,FM_ATT_MAX
	lsl	x0,x0,16
	lsl	x0,x0,x1
	mov	x1,1000
	mul	x0,x0,x1
	movl	x1,39280*FM_RATE
	udiv	x0,x0,x1
.ret:	ret

; x0 = operateur (FOP_*), x1 = parametres (AKFO_*), x2 = 1 si la voix n'est
; pas deja en FM
fm_op_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cbz	x2,.keep
	str	wzr,[x19,FOP_STAGE]
	movl	w0,FM_ATT_MAX shl 16
	str	w0,[x19,FOP_ATT]
.keep:	ldrb	w0,[x20,AKFO_MULT]
	and	w0,w0,15
	adr	x1,fm_mult2
	ldrb	w0,[x1,x0]
	str	w0,[x19,FOP_MULT2]
	ldrb	w0,[x20,AKFO_LEVEL]
	and	w0,w0,63
	lsl	w0,w0,5				; 0,75 dB par pas
	str	w0,[x19,FOP_LEVATT]
	ldrb	w0,[x20,AKFO_SUSTAIN]
	and	w0,w0,15
	cmp	w0,15
	lsl	w0,w0,7				; 3 dB par pas
	mov	w1,FM_ATT_MAX
	csel	w0,w1,w0,eq
	str	w0,[x19,FOP_SUSATT]
	ldrb	w0,[x20,AKFO_ATTACK]
	and	w0,w0,15
	bl	fm_attack_k
	str	w0,[x19,FOP_ATTK]
	ldrb	w0,[x20,AKFO_DECAY]
	and	w0,w0,15
	bl	fm_linear_inc
	str	w0,[x19,FOP_DECINC]
	ldrb	w0,[x20,AKFO_RELEASE]
	and	w0,w0,15
	bl	fm_linear_inc
	str	w0,[x19,FOP_RELINC]
	ldrb	w0,[x20,AKFO_WAVE]
	and	w0,w0,3
	str	w0,[x19,FOP_WAVE]
	ldrb	w0,[x20,AKFO_FLAGS]
	and	w1,w0,AKFF_SUSTAINED
	str	w1,[x19,FOP_SUS]
	and	w1,w0,AKFF_TREMOLO
	str	w1,[x19,FOP_TREM]
	and	w1,w0,AKFF_VIBRATO
	str	w1,[x19,FOP_VIB]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = synthe, x1 = voix, x2 = instrument (AKFI_SIZE) -> 0 / < 0
ak_fm_instrument:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x2
	bl	fm_voice
	b.ne	.inval
	cbz	x21,.inval
	mov	x20,x2
	mov	x0,x19
	bl	fm_lock
	ldr	w22,[x20,FMV_WAVE]
	cmp	w22,AKW_FM
	cset	x22,ne
	add	x0,x20,FMV_OP0
	add	x1,x21,AKFI_OP0
	mov	x2,x22
	bl	fm_op_set
	add	x0,x20,FMV_OP1
	add	x1,x21,AKFI_OP1
	mov	x2,x22
	bl	fm_op_set
	ldrb	w0,[x21,AKFI_FEEDBACK]
	and	w0,w0,7
	str	w0,[x20,FMV_FEEDBACK]
	ldrb	w0,[x21,AKFI_CONN]
	and	w0,w0,1
	str	w0,[x20,FMV_CONN]
	mov	w0,1
	str	w0,[x20,FMV_HASFM]
	mov	x0,x19
	bl	fm_unlock
	mov	x0,0
	b	.out
.inval:	mov	x0,AKE_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = instrument (AKFI_SIZE, rempli), x1 = les 26 octets d'un instrument FM Song
; (.FMS : paires operateur 1, operateur 2) -> 0
ak_fm_from_fms:
	adr	x2,fm_fms_map
	mov	x3,0				; operateur
.o:	mov	x4,AKFO_SIZE
	mul	x4,x4,x3
	add	x4,x0,x4			; destination
	mov	x5,0
.f:	add	x7,x2,x5,lsl 1
	ldrb	w6,[x7]				; champ P_*
	ldrb	w7,[x7,1]			; masque
	add	x8,x6,x3
	ldrb	w8,[x1,x8]
	and	w8,w8,w7
	strb	w8,[x4,x5]
	add	x5,x5,1
	cmp	x5,8
	b.lo	.f
	mov	w6,0				; drapeaux
	add	x7,x1,x3
	ldrb	w8,[x7,24]			; P_EGT
	cbz	w8,.a
	orr	w6,w6,AKFF_SUSTAINED
.a:	ldrb	w8,[x7,14]			; P_AM
	cbz	w8,.v
	orr	w6,w6,AKFF_TREMOLO
.v:	ldrb	w8,[x7,12]			; P_VIB
	cbz	w8,.k
	orr	w6,w6,AKFF_VIBRATO
.k:	ldrb	w8,[x7,18]			; P_KSR
	cbz	w8,.s
	orr	w6,w6,AKFF_KSR
.s:	strb	w6,[x4,AKFO_FLAGS]
	add	x3,x3,1
	cmp	x3,2
	b.lo	.o
	ldrb	w6,[x1,22]			; P_FB
	and	w6,w6,7
	strb	w6,[x0,AKFI_FEEDBACK]
	ldrb	w6,[x1,23]			; P_CON
	and	w6,w6,1
	strb	w6,[x0,AKFI_CONN]
	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  Jeu
; ---------------------------------------------------------------------------

; x0 = synthe, x1 = voix, x2 = frequence (milli-Hz), x3 = AKW_*, x4 = volume
; 0..255 -> 0 / < 0 : la note sonne jusqu'a ak_fm_stop
ak_fm_start:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	bl	fm_voice
	b.ne	.inval
	mov	x20,x2
	cmp	x23,0
	csel	x23,xzr,x23,lt
	cmp	x23,255
	mov	x0,255
	csel	x23,x0,x23,gt
	movl	x0,20000000			; 20 kHz au plus
	cmp	x21,x0
	csel	x21,x0,x21,hi
	mov	x0,x19
	bl	fm_lock
	lsl	x0,x21,32			; pas de phase
	movl	x1,1000*FM_RATE
	udiv	x0,x0,x1
	str	w0,[x20,FMV_INC]
	cmp	x22,AKW_FM
	b.ne	.plain
	ldr	w0,[x20,FMV_HASFM]
	cbz	w0,.plain
	ldr	w0,[x20,FMV_WAVE]		; touche enfoncee : les enveloppes repartent
	cmp	w0,AKW_FM
	movl	w1,FM_ATT_MAX shl 16
	b.eq	.fm
	str	w1,[x20,FMV_OP0+FOP_ATT]
	str	w1,[x20,FMV_OP1+FOP_ATT]
.fm:	mov	w0,AKW_FM
	str	w0,[x20,FMV_WAVE]
	mov	w0,257
	mul	w0,w0,w23
	str	w0,[x20,FMV_GAIN]
	str	w0,[x20,FMV_TARGET]
	mov	w0,EG_ATTACK
	str	w0,[x20,FMV_OP0+FOP_STAGE]
	str	w0,[x20,FMV_OP1+FOP_STAGE]
	str	wzr,[x20,FMV_OP0+FOP_PHASE]
	str	wzr,[x20,FMV_OP1+FOP_PHASE]
	ldr	w0,[x20,FMV_OP0+FOP_ATT]
	cmp	w0,w1
	csel	w0,w1,w0,hi
	str	w0,[x20,FMV_OP0+FOP_ATT]
	ldr	w0,[x20,FMV_OP1+FOP_ATT]
	cmp	w0,w1
	csel	w0,w1,w0,hi
	str	w0,[x20,FMV_OP1+FOP_ATT]
	str	wzr,[x20,FMV_FB1]
	str	wzr,[x20,FMV_FB2]
	b	.on
.plain:	ldr	w0,[x20,FMV_WAVE]
	cmp	w0,AKW_FM
	b.ne	.p1
	str	wzr,[x20,FMV_OP0+FOP_STAGE]
	str	wzr,[x20,FMV_OP1+FOP_STAGE]
	str	wzr,[x20,FMV_GAIN]
.p1:	cmp	x22,AKW_NOISE
	mov	x0,AKW_SQUARE
	csel	x22,x0,x22,hi
	str	w22,[x20,FMV_WAVE]
	mov	w0,257
	mul	w0,w0,w23
	str	w0,[x20,FMV_TARGET]
	ldr	w0,[x20,FMV_ON]
	cbnz	w0,.on
	ldr	w0,[x20,FMV_GAIN]
	cbnz	w0,.on
	str	wzr,[x20,FMV_PHASE]
.on:	mov	w0,1
	str	w0,[x20,FMV_ON]
	mov	x0,x19
	bl	fm_unlock
	mov	x0,0
	b	.out
.inval:	mov	x0,AKE_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x2 = voix : touche relachee
fm_key_off:
	str	wzr,[x2,FMV_ON]
	ldr	w3,[x2,FMV_WAVE]
	cmp	w3,AKW_FM
	b.eq	.fm
	str	wzr,[x2,FMV_TARGET]
	ret
.fm:	mov	w4,EG_RELEASE
	ldr	w3,[x2,FMV_OP0+FOP_STAGE]
	cbz	w3,.c
	str	w4,[x2,FMV_OP0+FOP_STAGE]
.c:	ldr	w3,[x2,FMV_OP1+FOP_STAGE]
	cbz	w3,.ret
	str	w4,[x2,FMV_OP1+FOP_STAGE]
.ret:	ret

; x0 = synthe, x1 = voix (-1 : toutes) -> 0 : les relachements s'entendent
ak_fm_stop:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	fm_lock
	ldr	x5,[x19,AKFM_NV]
	add	x2,x19,AKFM_VOICES
	mov	x6,0
.v:	cmp	x6,x5
	b.hs	.done
	tbnz	x20,63,.off
	cmp	x6,x20
	b.ne	.n
.off:	bl	fm_key_off
.n:	add	x2,x2,FMV_SIZE
	add	x6,x6,1
	b	.v
.done:	mov	x0,x19
	bl	fm_unlock
	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

; x0 = synthe -> 0 : tout se tait d'un coup
ak_fm_silence:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	fm_lock
	ldr	x5,[x19,AKFM_NV]
	add	x2,x19,AKFM_VOICES
.v:	cbz	x5,.done
	str	wzr,[x2,FMV_ON]
	str	wzr,[x2,FMV_GAIN]
	str	wzr,[x2,FMV_TARGET]
	str	wzr,[x2,FMV_OP0+FOP_STAGE]
	str	wzr,[x2,FMV_OP1+FOP_STAGE]
	add	x2,x2,FMV_SIZE
	sub	x5,x5,1
	b	.v
.done:	mov	x0,x19
	bl	fm_unlock
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; x2 = voix -> x3 = 1 si elle sonne
fm_voice_active:
	ldr	w3,[x2,FMV_WAVE]
	cmp	w3,AKW_FM
	b.ne	.p
	ldr	w3,[x2,FMV_OP0+FOP_STAGE]
	ldr	w4,[x2,FMV_OP1+FOP_STAGE]
	orr	w3,w3,w4
	b	.t
.p:	ldr	w3,[x2,FMV_ON]
	ldr	w4,[x2,FMV_GAIN]
	orr	w3,w3,w4
.t:	cmp	w3,0
	cset	x3,ne
	ret

; x0 = synthe -> 1 tant qu'une voix sonne (touche enfoncee ou relachement)
ak_fm_active:
	ldr	x5,[x0,AKFM_NV]
	add	x2,x0,AKFM_VOICES
	mov	x6,x30
.v:	cbz	x5,.no
	bl	fm_voice_active
	cbnz	x3,.yes
	add	x2,x2,FMV_SIZE
	sub	x5,x5,1
	b	.v
.yes:	mov	x0,1
	mov	x30,x6
	ret
.no:	mov	x0,0
	mov	x30,x6
	ret

; x0 = synthe, x1 = voix, x2 = panoramique 0 (gauche) .. 128 (centre) .. 255 -> 0
ak_fm_pan:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	bl	fm_voice
	b.ne	.inval
	mov	x3,256				; lois lineaires, centre = 256 des deux cotes
	cmp	x20,128
	b.lo	.left
	mov	x4,256
	mov	x5,255
	sub	x5,x5,x20
	lsl	x5,x5,8
	mov	x6,127
	udiv	x5,x5,x6			; droite pleine : gauche 0
	str	w5,[x2,FMV_GL]
	str	w4,[x2,FMV_GR]
	b	.ok
.left:	lsl	x5,x20,1
	str	w3,[x2,FMV_GL]
	str	w5,[x2,FMV_GR]
.ok:	mov	x0,0
	b	.out
.inval:	mov	x0,AKE_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = synthe, x1 = voix, x2 = frequence (milli-Hz) -> 0 : la note glisse
; (pas de phase change, enveloppes intactes : molette de hauteur)
ak_fm_pitch:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x2
	bl	fm_voice
	b.ne	.inval
	movl	x0,20000000
	cmp	x19,x0
	csel	x19,x0,x19,hi
	lsl	x0,x19,32
	movl	x1,1000*FM_RATE
	udiv	x0,x0,x1
	str	w0,[x2,FMV_INC]
	mov	x0,0
	b	.out
.inval:	mov	x0,AKE_INVAL
.out:	ldr	x19,[sp],16
	epilogue

; x0 = synthe, x1 = voix, x2 = volume 0..255 -> 0 : volume d'une note qui sonne
ak_fm_gain:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x2
	bl	fm_voice
	b.ne	.inval
	mov	w0,257
	mul	w0,w0,w19
	str	w0,[x2,FMV_TARGET]
	ldr	w1,[x2,FMV_WAVE]
	cmp	w1,AKW_FM
	b.ne	.ok
	str	w0,[x2,FMV_GAIN]
.ok:	mov	x0,0
	b	.out
.inval:	mov	x0,AKE_INVAL
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Rendu
; ---------------------------------------------------------------------------

; x9 = operateur -> w10 = attenuation courante (unites), une trame de plus ;
; ne touche que w10-w13
fm_env:
	ldr	w11,[x9,FOP_STAGE]
	ldr	w10,[x9,FOP_ATT]
	cmp	w11,EG_ATTACK
	b.ne	.nattack
	ldr	w12,[x9,FOP_ATTK]
	cmn	w12,1
	b.ne	.a1
	mov	w10,0
	b	.a2
.a1:	cbz	w12,.a2
	umull	x12,w10,w12
	lsr	x12,x12,16
	add	w12,w12,1
	sub	w10,w10,w12
.a2:	mov	w12,0x10000
	cmp	w10,w12
	b.gt	.clip
	mov	w10,0
	mov	w11,EG_DECAY
	str	w11,[x9,FOP_STAGE]
	b	.clip
.nattack:
	cmp	w11,EG_DECAY
	b.ne	.ndecay
	ldr	w12,[x9,FOP_DECINC]
	add	w10,w10,w12
	ldr	w12,[x9,FOP_SUSATT]
	cmp	w12,w10,lsr 16
	b.hi	.clip
	lsl	w10,w12,16
	mov	w11,EG_SUSTAIN
	str	w11,[x9,FOP_STAGE]
	b	.clip
.ndecay:
	cmp	w11,EG_SUSTAIN
	b.ne	.nsus
	ldr	w12,[x9,FOP_SUS]
	cbnz	w12,.clip
	b	.rel
.nsus:	cmp	w11,EG_RELEASE
	b.ne	.off
.rel:	ldr	w12,[x9,FOP_RELINC]
	add	w10,w10,w12
.clip:	lsr	w12,w10,16
	cmp	w12,FM_ATT_MAX
	b.hs	.max
	str	w10,[x9,FOP_ATT]
	mov	w10,w12
	ret
.max:	movl	w10,FM_ATT_MAX shl 16
	str	w10,[x9,FOP_ATT]
	cmp	w11,EG_SUSTAIN
	b.lo	.m2
	str	wzr,[x9,FOP_STAGE]
.m2:	mov	w10,FM_ATT_MAX
	ret
.off:	mov	w10,FM_ATT_MAX
	ret

; x9 = operateur, w10 = phase, w11 = attenuation -> w10 = sortie
; (x24 = ak_sin1024, x25 = ak_exp256 ; ne touche que w10-w13)
fm_wave:
	lsr	w12,w10,22			; 1024 pas
	ldrsh	w13,[x24,x12,lsl 1]
	ldr	w10,[x9,FOP_WAVE]
	cbz	w10,.amp
	cmp	w10,1
	b.ne	.w2
	cmp	w12,512				; demi-sinus
	csel	w13,w13,wzr,lo
	b	.amp
.w2:	cmp	w13,0
	cneg	w13,w13,lt
	cmp	w10,2
	b.eq	.amp
	tst	w12,256				; impulsions
	csel	w13,w13,wzr,eq
.amp:	cmp	w11,FM_ATT_MAX
	b.hs	.zero
	and	w12,w11,255
	ldrh	w12,[x25,x12,lsl 1]
	lsr	w11,w11,8
	lsr	w12,w12,w11
	mul	w10,w13,w12
	asr	w10,w10,16
	ret
.zero:	mov	w10,0
	ret

; x20 = voix, w21 = tremolo, w22 = vibrato -> w0 = sortie
fm_sample:
	mov	x8,x30
	ldr	w14,[x20,FMV_INC]
	lsr	w14,w14,1
	; modulateur (avec retour)
	add	x9,x20,FMV_OP0
	ldr	w15,[x9,FOP_MULT2]
	mul	w15,w15,w14
	ldr	w0,[x9,FOP_VIB]
	cbz	w0,.m1
	lsr	w0,w15,8
	mul	x0,x0,x22
	asr	x0,x0,8
	add	w15,w15,w0
.m1:	bl	fm_env
	ldr	w0,[x9,FOP_LEVATT]
	add	w11,w10,w0
	ldr	w0,[x9,FOP_TREM]
	cbz	w0,.m2
	add	w11,w11,w21
.m2:	ldr	w10,[x9,FOP_PHASE]
	ldr	w0,[x20,FMV_FEEDBACK]
	cbz	w0,.m3
	ldr	w1,[x20,FMV_FB1]
	ldr	w2,[x20,FMV_FB2]
	add	w1,w1,w2
	mov	w2,12
	sub	w2,w2,w0
	asr	w1,w1,w2
	add	w10,w10,w1,lsl 22
.m3:	bl	fm_wave
	mov	w16,w10				; mo
	ldr	w0,[x9,FOP_PHASE]
	add	w0,w0,w15
	str	w0,[x9,FOP_PHASE]
	ldr	w0,[x20,FMV_FB1]
	str	w0,[x20,FMV_FB2]
	str	w16,[x20,FMV_FB1]
	; porteuse
	add	x9,x20,FMV_OP1
	ldr	w15,[x9,FOP_MULT2]
	mul	w15,w15,w14
	ldr	w0,[x9,FOP_VIB]
	cbz	w0,.c1
	lsr	w0,w15,8
	mul	x0,x0,x22
	asr	x0,x0,8
	add	w15,w15,w0
.c1:	bl	fm_env
	ldr	w0,[x9,FOP_LEVATT]
	add	w11,w10,w0
	ldr	w0,[x9,FOP_TREM]
	cbz	w0,.c2
	add	w11,w11,w21
.c2:	ldr	w10,[x9,FOP_PHASE]
	ldr	w1,[x20,FMV_CONN]
	cbnz	w1,.c3
	add	w10,w10,w16,lsl 19		; FM : +-4 periodes a plein niveau
.c3:	bl	fm_wave
	ldr	w0,[x9,FOP_PHASE]
	add	w0,w0,w15
	str	w0,[x9,FOP_PHASE]
	mov	w0,w10
	ldr	w1,[x20,FMV_CONN]
	cbz	w1,.ret
	add	w0,w0,w16
	asr	w0,w0,1
.ret:	mov	x30,x8
	ret

; x20 = voix -> w0 = sortie ; enveloppe, phase, bruit (onde simple)
fm_plain:
	ldr	w0,[x20,FMV_GAIN]
	ldr	w1,[x20,FMV_TARGET]
	cmp	w0,w1
	b.eq	.e
	b.hi	.dn
	add	w0,w0,300
	cmp	w0,w1
	csel	w0,w1,w0,hi
	b	.e0
.dn:	add	w2,w1,300
	cmp	w0,w2
	sub	w0,w0,300
	csel	w0,w0,w1,hi
.e0:	str	w0,[x20,FMV_GAIN]
.e:	ldr	w2,[x20,FMV_PHASE]		; p (avant le pas)
	ldr	w3,[x20,FMV_INC]
	add	w3,w2,w3
	str	w3,[x20,FMV_PHASE]
	ldr	w4,[x20,FMV_WAVE]
	cmp	w4,AKW_NOISE
	b.ne	.s
	cmp	w3,w2				; une nouvelle valeur par periode
	b.hs	.n
	ldr	w5,[x20,FMV_NOISE]
	and	w6,w5,1
	neg	w6,w6
	movl	w7,0xB400
	and	w6,w6,w7
	eor	w5,w6,w5,lsr 1
	str	w5,[x20,FMV_NOISE]
	and	w5,w5,0xFFFF
	sub	w5,w5,8,lsl 12
	str	w5,[x20,FMV_NOISEV]
.n:	ldr	w0,[x20,FMV_NOISEV]
	ret
.s:	mov	w2,w3				; (la phase apres le pas)
	cmp	w4,AKW_SINE
	b.ne	.t
	lsr	w0,w2,22
	ldrsh	w0,[x24,x0,lsl 1]
	ret
.t:	cmp	w4,AKW_TRIANGLE
	b.ne	.saw
	lsr	w0,w2,15
	tst	w0,0x10000
	b.ne	.t2
	sub	w0,w0,8,lsl 12
	ret
.t2:	movl	w1,98303
	sub	w0,w1,w0
	ret
.saw:	cmp	w4,AKW_SAW
	b.ne	.sq
	lsr	w0,w2,16
	sub	w0,w0,8,lsl 12
	ret
.sq:	mov	w0,24000			; carre (un peu moins fort)
	mov	w1,-24000
	tst	w2,0x80000000
	csel	w0,w0,w1,eq
	ret

; x0 = synthe, x1 = sortie s16 stereo, x2 = trames : ECRIT les trames (x1 += ...)
; sans verrou ; x19 = synthe. Garde x19.
fm_render_piece:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	str	x28,[sp,-16]!
	mov	x26,x1
	mov	x27,x2
	adr	x24,ak_sin1024
	adr	x25,ak_exp256
.f:	cbz	x27,.done
	; LFO : tremolo 1 dB (42 unites) a 3,7 Hz, vibrato +-7 cents a 6,1 Hz
	ldr	w0,[x19,AKFM_LFOT]
	movl	w1,360353
	add	w0,w0,w1
	str	w0,[x19,AKFM_LFOT]
	lsr	w0,w0,16
	movl	w1,65535
	sub	w1,w1,w0
	tst	w0,0x8000
	csel	w0,w0,w1,eq
	mov	w1,42
	mul	w0,w0,w1
	lsr	w21,w0,15
	ldr	w0,[x19,AKFM_LFOV]
	movl	w1,594096
	add	w0,w0,w1
	str	w0,[x19,AKFM_LFOV]
	lsr	w0,w0,16
	movl	w1,65535
	sub	w1,w1,w0
	tst	w0,0x8000
	csel	w0,w0,w1,eq
	asr	w0,w0,6
	sub	w0,w0,256
	mov	w1,7
	mul	w0,w0,w1
	mov	w1,17
	sdiv	w22,w0,w1
	sxtw	x22,w22
	; les voix
	mov	w23,0				; gauche
	mov	w28,0				; droite
	ldr	x17,[x19,AKFM_NV]
	add	x20,x19,AKFM_VOICES
.v:	cbz	x17,.mix
	ldr	w0,[x20,FMV_WAVE]
	cmp	w0,AKW_FM
	b.ne	.pl
	ldr	w0,[x20,FMV_OP0+FOP_STAGE]
	ldr	w1,[x20,FMV_OP1+FOP_STAGE]
	orr	w0,w0,w1
	cbz	w0,.nv
	bl	fm_sample
	b	.gain
.pl:	ldr	w0,[x20,FMV_ON]
	ldr	w1,[x20,FMV_GAIN]
	orr	w0,w0,w1
	cbz	w0,.nv
	bl	fm_plain
.gain:	ldr	w1,[x20,FMV_GAIN]
	lsr	w1,w1,1
	mul	w0,w0,w1
	asr	w0,w0,15
	ldr	w1,[x20,FMV_GL]
	mul	w1,w0,w1
	add	w23,w23,w1,asr 8
	ldr	w1,[x20,FMV_GR]
	mul	w1,w0,w1
	add	w28,w28,w1,asr 8
.nv:	add	x20,x20,FMV_SIZE
	sub	x17,x17,1
	b	.v
.mix:	asr	w23,w23,2			; 4 voix pleines avant saturation
	asr	w28,w28,2
	mov	w0,32767
	mov	w1,-32768
	cmp	w23,w0
	csel	w23,w0,w23,gt
	cmp	w23,w1
	csel	w23,w1,w23,lt
	cmp	w28,w0
	csel	w28,w0,w28,gt
	cmp	w28,w1
	csel	w28,w1,w28,lt
	strh	w23,[x26],2
	strh	w28,[x26],2
	sub	x27,x27,1
	b	.f
.done:	ldr	x28,[sp],16
	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; x0 = synthe, x1 = sortie s16 stereo, x2 = trames -> 0 : les trames suivantes
; ECRITES dans la sortie (hors ligne ; a ne pas melanger avec ak_fm_live)
ak_fm_render:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
.p:	cbz	x21,.done
	mov	x22,FM_PIECE
	cmp	x21,x22
	csel	x22,x21,x22,lo
	mov	x0,x19
	bl	fm_lock
	mov	x0,x19
	mov	x1,x20
	mov	x2,x22
	bl	fm_render_piece
	mov	x0,x19
	bl	fm_unlock
	add	x20,x20,x22,lsl 2
	sub	x21,x21,x22
	b	.p
.done:	mov	x0,0
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = note 0..11 (C..B), x1 = octave -> milli-Hz (C4 = 60, comme ak_note_key)
ak_note_octave_mhz:
	add	x1,x1,1
	mov	x2,12
	mul	x1,x1,x2
	add	x0,x0,x1
	; (suite)
; x0 = touche MIDI -> sa frequence en milli-Hz (69 : la4 = 440000)
ak_note_mhz:
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x0,127
	mov	x1,127
	csel	x0,x1,x0,gt
	sub	x0,x0,69
	mov	x1,100
	mul	x0,x0,x1			; cents depuis le la4
	movl	x1,440000
	; (suite)
; x0 = cents (signes), x1 = base -> base * 2^(cents / 1200), arrondi
ak_cents_mul:
	mov	x2,1200
	movl	x3,1200*1024			; (positif : division entiere vers le bas)
	add	x3,x3,x0
	udiv	x4,x3,x2
	msub	x5,x4,x2,x3			; 0..1199
	sub	x4,x4,1024			; octaves
	adr	x6,ak_cents1200
	ldr	w5,[x6,x5,lsl 2]		; 2.30
	mul	x5,x5,x1
	mov	x6,30
	sub	x6,x6,x4			; decalage final
	cmp	x6,1
	b.lt	.up
	cmp	x6,63
	b.gt	.zero
	mov	x7,1
	sub	x3,x6,1
	lsl	x7,x7,x3
	add	x5,x5,x7
	lsr	x0,x5,x6
	ret
.up:	neg	x6,x6
	lsl	x0,x5,x6
	ret
.zero:	mov	x0,0
	ret

fm_mult2	db 1,2,4,6,8,10,12,14,16,18,20,20,24,24,30,30
; ak_fm_from_fms : pour chaque champ AKFO_* : le champ P_* de FM Song, son masque
fm_fms_map	db 0,15, 2,15, 4,15, 6,15, 8,63, 10,3, 16,15, 20,3
		align 4
include 'ak_fmtab.inc'
		align 4
