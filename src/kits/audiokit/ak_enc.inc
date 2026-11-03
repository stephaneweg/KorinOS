; ============================================================================
;  kits/audiokit/ak_enc.inc - encodeur generique d'AudioKit : WAV ; MP3
;  (ak_mp3enc.inc), Opus (ak_opusenc.inc) quand ils sont la
;
;  Un encodeur (poignee opaque) recoit du PCM s16 entrelace (ak_enc_write)
;  et ecrit les octets produits par une fonction de sortie (le fichier du
;  kit par AppKit ; le semihosting sur le banc du PC). A la fermeture, le
;  module finit le flux (WAV : tailles de l'en-tete, par un repositionnement
;  au debut).
;
;  Interface d'un module (x0 = encodeur) :
;	<fmt>_enc_init	-> 0 / < 0 (pose AKN_PUSH, AKN_FINISH, son etat AKN_CODEC)
;	AKN_PUSH	(enc, pcm s16 entrelace, trames) -> 0 / < 0
;	AKN_FINISH	(enc) -> 0 / < 0
;  Sortie : akn_out(enc, octets, longueur) ; akn_rewrite(enc, position,
;  octets, longueur) (la position courante est gardee).
; ============================================================================

AKN_FMT		= 0		; AKF_*
AKN_RATE	= 8
AKN_CH		= 16
AKN_QUALITY	= 24		; debit (bits/s) ; 0 : par defaut
AKN_WRITEFN	= 32		; (contexte, octets, longueur) -> longueur ecrite
AKN_SEEKFN	= 40		; (contexte, position) -> 0 (0 : impossible)
AKN_CTX		= 48
AKN_POS		= 56		; octets ecrits
AKN_CODEC	= 64		; etat du module
AKN_PUSH	= 72
AKN_FINISH	= 80
AKN_FRAMES	= 88		; trames recues
AKN_ALLOCS	= 96		; blocs (liste, comme AKD_ALLOCS)
AKN_ERR		= 104
AKN_CLOSEFN	= 112		; (contexte) a la fermeture
AKN_BYTES	= 128

; x0 = encodeur, x1 = octets -> x0 = bloc a zero (rendu a la fermeture)
akn_alloc:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	add	x0,x1,16
	bl	ak_alloc
	cbz	x0,.out
	ldr	x1,[x19,AKN_ALLOCS]
	str	x1,[x0]
	str	x0,[x19,AKN_ALLOCS]
	add	x0,x0,16
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur, x1 = octets, x2 = longueur -> 0 / < 0
akn_out:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	cbz	x2,.ok
	ldr	x0,[x19,AKN_ERR]
	tbnz	x0,63,.out
	ldr	x0,[x19,AKN_CTX]
	ldr	x3,[x19,AKN_WRITEFN]
	blr	x3
	cmp	x0,x20
	b.ne	.err
	ldr	x0,[x19,AKN_POS]
	add	x0,x0,x20
	str	x0,[x19,AKN_POS]
.ok:	mov	x0,0
	b	.out
.err:	mov	x0,AKE_IO
	str	x0,[x19,AKN_ERR]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur, x1 = position, x2 = octets, x3 = longueur : reecrit un
; passage deja ecrit (en-tete), puis revient a la fin -> 0 / < 0
akn_rewrite:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x2
	mov	x22,x3
	ldr	x4,[x19,AKN_SEEKFN]
	cbz	x4,.no
	ldr	x0,[x19,AKN_CTX]
	blr	x4
	ldr	x0,[x19,AKN_CTX]
	mov	x1,x21
	mov	x2,x22
	ldr	x3,[x19,AKN_WRITEFN]
	blr	x3
	ldr	x0,[x19,AKN_CTX]
	ldr	x1,[x19,AKN_POS]
	ldr	x4,[x19,AKN_SEEKFN]
	blr	x4
	mov	x0,0
	b	.out
.no:	mov	x0,AKE_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ecriture (contexte, octets, longueur) -> longueur, x1 = positionnement
; (contexte, position) (0 : aucun), x2 = contexte, x3 = format AKF_*,
; x4 = frequence, x5 = canaux, x6 = debit (bits/s, 0 : par defaut)
; -> encodeur (< 0 : erreur)
ak_enc_open_io:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x23,x3
	mov	x24,x4
	mov	x25,x5
	mov	x26,x6
	sub	x0,x25,1			; 1 a 8 canaux, 8 a 192 kHz
	cmp	x0,7
	b.hi	.inval
	movl	x0,8000
	cmp	x24,x0
	b.lo	.inval
	movl	x0,192000
	cmp	x24,x0
	b.hi	.inval
	mov	x0,AKN_BYTES
	bl	ak_alloc
	cbz	x0,.nomem
	mov	x19,x0
	str	x20,[x19,AKN_WRITEFN]
	str	x21,[x19,AKN_SEEKFN]
	str	x22,[x19,AKN_CTX]
	str	x23,[x19,AKN_FMT]
	str	x24,[x19,AKN_RATE]
	str	x25,[x19,AKN_CH]
	str	x26,[x19,AKN_QUALITY]
	mov	x0,x19
	cmp	x23,AKF_WAV
	b.eq	.wav
	cmp	x23,AKF_MP3
	b.eq	.mp3
	cmp	x23,AKF_OPUS
	b.eq	.opus
	mov	x0,AKE_FORMAT
	b	.fail
.wav:	bl	wav_enc_init
	b	.res
.mp3:	bl	mp3_enc_init
	b	.res
.opus:	bl	opus_enc_init
.res:	tbnz	x0,63,.fail
	mov	x0,x19
	b	.out
.fail:	mov	x20,x0
	mov	x0,x19
	bl	akn_free
	mov	x0,x20
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.inval:	mov	x0,AKE_INVAL
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur : rend ses blocs et lui-meme
akn_free:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKN_ALLOCS]
.l:	cbz	x20,.me
	mov	x0,x20
	ldr	x20,[x20]
	bl	ak_free
	b	.l
.me:	mov	x0,x19
	bl	ak_free
	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur, x1 = PCM s16 entrelace, x2 = trames -> 0 / < 0
ak_enc_write:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,AKN_FRAMES]
	add	x0,x0,x2
	str	x0,[x19,AKN_FRAMES]
	ldr	x3,[x19,AKN_PUSH]
	mov	x0,x19
	blr	x3
	tbnz	x0,63,.out
	ldr	x0,[x19,AKN_ERR]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur : finit le flux, ferme la sortie, rend tout -> 0 / < 0
ak_enc_close:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,AKN_FINISH]
	cbz	x1,.f
	blr	x1
.f:	ldr	x20,[x19,AKN_ERR]
	ldr	x1,[x19,AKN_CLOSEFN]
	cbz	x1,.free
	ldr	x0,[x19,AKN_CTX]
	blr	x1
.free:	mov	x0,x19
	bl	akn_free
	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur -> octets ecrits
ak_enc_size:
	ldr	x0,[x0,AKN_POS]
	ret

; ---------------------------------------------------------------------------
;  WAV (PCM 16 bits)
; ---------------------------------------------------------------------------

; x0 = encodeur -> 0 / < 0
wav_enc_init:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,48
	mov	x19,x0
	adr	x0,wav_enc_push
	str	x0,[x19,AKN_PUSH]
	adr	x0,wav_enc_finish
	str	x0,[x19,AKN_FINISH]
	mov	x0,sp				; en-tete (tailles a 0 : flux)
	ldr	x1,[x19,AKN_RATE]
	ldr	x2,[x19,AKN_CH]
	mov	x3,16
	mov	x4,0
	bl	ak_wav_header
	mov	x0,x19
	mov	x1,sp
	mov	x2,44
	bl	akn_out
	add	sp,sp,48
	ldp	x19,x20,[sp],16
	epilogue

; x0 = encodeur, x1 = PCM, x2 = trames
wav_enc_push:
	ldr	x3,[x0,AKN_CH]
	lsl	x3,x3,1
	mul	x2,x2,x3
	b	akn_out

; x0 = encodeur : tailles de l'en-tete
wav_enc_finish:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,48
	mov	x19,x0
	ldr	x4,[x19,AKN_POS]
	sub	x4,x4,44
	mov	x0,sp
	ldr	x1,[x19,AKN_RATE]
	ldr	x2,[x19,AKN_CH]
	mov	x3,16
	bl	ak_wav_header
	mov	x0,x19
	mov	x1,0
	mov	x2,sp
	mov	x3,44
	bl	akn_rewrite
	add	sp,sp,48
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon (44 o), x1 = frequence, x2 = canaux, x3 = bits, x4 = octets
; de donnees -> 44 (en-tete RIFF/WAVE PCM)
ak_wav_header:
	movl	w5,0x46464952			; 'RIFF'
	str	w5,[x0]
	add	w5,w4,36
	str	w5,[x0,4]
	movl	w5,0x45564157			; 'WAVE'
	str	w5,[x0,8]
	movl	w5,0x20746D66			; 'fmt '
	str	w5,[x0,12]
	mov	w5,16
	str	w5,[x0,16]
	mov	w5,1				; PCM
	strh	w5,[x0,20]
	strh	w2,[x0,22]
	str	w1,[x0,24]
	lsr	w6,w3,3
	mul	w6,w6,w2			; octets par trame
	mul	w5,w1,w6
	str	w5,[x0,28]			; octets par seconde
	strh	w6,[x0,32]
	strh	w3,[x0,34]
	movl	w5,0x61746164			; 'data'
	str	w5,[x0,36]
	str	w4,[x0,40]
	mov	x0,44
	ret

