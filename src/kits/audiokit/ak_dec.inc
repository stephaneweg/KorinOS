; ============================================================================
;  kits/audiokit/ak_dec.inc - decodeur generique d'AudioKit
;
;  Un decodeur (poignee opaque) lit une SOURCE d'octets : un tampon en
;  memoire, ou des fonctions de lecture / positionnement (le fichier du kit,
;  ouvert par AppKit). Le format est reconnu aux premiers octets ; le module
;  du format decode une unite (trame MP3 ou FLAC, paquet Ogg, bloc WAV) dans
;  des tampons PLANS (un par canal, flottant 32 bits ou entier 32 bits cadre
;  a gauche), que ak_dec_read entrelace en s16.
;  L'API par paquets (ak_codec_*, pour MediaKit) prend les memes modules,
;  sans source : le demultiplexeur de l'appelant donne les paquets.
; ============================================================================

; --- le decodeur ----------------------------------------------------------------
AKD_FMT		= 0		; AKF_*
AKD_RATE	= 8
AKD_CH		= 16
AKD_TOTAL	= 24		; trames en tout (-1 : inconnu)
AKD_POS		= 32		; trames rendues
AKD_BITRATE	= 40
AKD_BITS	= 48		; bits de la source (0 : compresse)
AKD_MODE	= 56		; 0 memoire, 1 fonctions, 2 paquets (ak_codec_*)
AKD_BASE	= 64		; memoire : debut ; fonctions : tampon
AKD_LEN		= 72		; taille du flux
AKD_OFF		= 80		; position de lecture
AKD_BUFOFF	= 88		; fonctions : position du debut du tampon
AKD_BUFLEN	= 96		; fonctions : octets valides dans le tampon
AKD_IOPOS	= 104		; fonctions : position du fichier
AKD_IOREAD	= 112		; (contexte, tampon, octets) -> octets lus
AKD_IOSEEK	= 120		; (contexte, position) -> 0
AKD_IOCTX	= 128
AKD_CODEC	= 136		; etat du module
AKD_NEXT	= 144		; (dec) -> trames decodees (0 : fin, < 0 : erreur)
AKD_SEEKFN	= 152		; (dec, trame) -> 0 (pose AKD_POS, AKD_SKIP) ; 0 : impossible
AKD_ALLOCS	= 160		; blocs pris par akd_alloc (liste)
AKD_PCMFMT	= 168		; AKD_F32 / AKD_S32
AKD_PCMN	= 176		; trames dans les tampons plans
AKD_PCMI	= 184		; trames deja rendues
AKD_SKIP	= 192		; trames a sauter (avance d'Opus, recherche)
AKD_DATA0	= 200		; debut des donnees audio
AKD_DATAEND	= 208		; fin des donnees audio (sans ID3v1)
AKD_EOF		= 216
AKD_LIMIT	= 224		; 1 : ne pas rendre plus de AKD_TOTAL trames
AKD_PKTFN	= 232		; (dec, paquet, octets) -> trames (API par paquets)
AKD_RESETFN	= 240		; (dec) : oublie l'historique (API par paquets)
AKD_IOCLOSE	= 248		; (contexte) : a la fermeture (0 : rien)
AKD_PCM		= 256		; 8 pointeurs : tampons plans
AKD_TAGS	= 320		; 3 x 256 o : titre, artiste, album (UTF-8)
AKD_MODFREE	= 1088		; (dec) : avant de rendre la memoire (0 : rien)
AKD_BYTES	= 1152
AKD_F32		= 0
AKD_S32		= 1
AKD_IOBUF	= 262144	; tampon de lecture (mode fonctions)

; ---------------------------------------------------------------------------
;  Memoire du decodeur
; ---------------------------------------------------------------------------

; x0 = decodeur, x1 = octets -> x0 = bloc a zero (rendu avec le decodeur), 0
akd_alloc:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	add	x0,x1,16
	bl	ak_alloc
	cbz	x0,.out
	ldr	x1,[x19,AKD_ALLOCS]
	str	x1,[x0]
	str	x0,[x19,AKD_ALLOCS]
	add	x0,x0,16
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur : rend tous ses blocs et lui-meme
akd_free:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,AKD_MODFREE]
	cbz	x1,.m0
	blr	x1
.m0:	ldr	x20,[x19,AKD_ALLOCS]
.l:	cbz	x20,.me
	mov	x0,x20
	ldr	x20,[x20]
	bl	ak_free
	b	.l
.me:	ldr	x0,[x19,AKD_MODE]
	cmp	x0,1
	b.ne	.d
	ldr	x0,[x19,AKD_BASE]
	bl	ak_free
	ldr	x1,[x19,AKD_IOCLOSE]
	cbz	x1,.d
	ldr	x0,[x19,AKD_IOCTX]
	blr	x1
.d:	mov	x0,x19
	bl	ak_free
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = trames, x2 = canaux : tampons plans (AKD_PCM)
; -> x0 = 0, < 0 si echec
akd_planes:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	add	x20,x1,4			; (marge pour NEON)
	lsl	x20,x20,2
	mov	x21,x2
	mov	x22,0
.c:	cmp	x22,x21
	b.hs	.ok
	mov	x0,x19
	mov	x1,x20
	bl	akd_alloc
	cbz	x0,.nomem
	add	x1,x19,AKD_PCM
	str	x0,[x1,x22,lsl 3]
	add	x22,x22,1
	b	.c
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.ok:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Source d'octets
; ---------------------------------------------------------------------------

; x0 = decodeur, x1 = octets voulus -> x0 = adresse de la position courante,
; x1 = octets contigus disponibles (>= voulus, sauf en fin de flux)
; Ne garde que x19-x28.
akd_need:
	ldr	x2,[x0,AKD_MODE]
	cbnz	x2,.io
	ldr	x2,[x0,AKD_BASE]
	ldr	x3,[x0,AKD_OFF]
	ldr	x4,[x0,AKD_LEN]
	subs	x1,x4,x3
	csel	x1,xzr,x1,lt
	add	x0,x2,x3
	ret
.io:	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	movl	x2,AKD_IOBUF
	cmp	x1,x2
	csel	x20,x1,x2,lo			; voulus (au plus le tampon)
	ldr	x2,[x19,AKD_OFF]
	ldr	x3,[x19,AKD_BUFOFF]
	ldr	x4,[x19,AKD_BUFLEN]
	subs	x5,x2,x3			; place dans le tampon
	b.lt	.reset
	cmp	x5,x4
	b.gt	.reset
	sub	x21,x4,x5			; disponibles
	cmp	x21,x20
	b.hs	.have
	ldr	x0,[x19,AKD_BASE]		; ramene le reste au debut
	add	x1,x0,x5
	mov	x2,x21
	bl	ak_memmove
	ldr	x0,[x19,AKD_OFF]
	str	x0,[x19,AKD_BUFOFF]
	str	x21,[x19,AKD_BUFLEN]
	b	.fill
.reset:	ldr	x0,[x19,AKD_IOCTX]		; hors du tampon : repositionner
	ldr	x1,[x19,AKD_OFF]
	ldr	x2,[x19,AKD_IOSEEK]
	blr	x2
	ldr	x0,[x19,AKD_OFF]
	str	x0,[x19,AKD_BUFOFF]
	str	x0,[x19,AKD_IOPOS]
	str	xzr,[x19,AKD_BUFLEN]
.fill:	ldr	x21,[x19,AKD_BUFLEN]		; lire jusqu'a avoir assez
	cmp	x21,x20
	b.hs	.have0
	ldr	x0,[x19,AKD_BUFOFF]
	add	x0,x0,x21
	ldr	x1,[x19,AKD_IOPOS]
	cmp	x0,x1
	b.eq	.rd
	mov	x22,x0				; le fichier n'est pas la ou il faut
	ldr	x0,[x19,AKD_IOCTX]
	mov	x1,x22
	ldr	x2,[x19,AKD_IOSEEK]
	blr	x2
	str	x22,[x19,AKD_IOPOS]
.rd:	ldr	x0,[x19,AKD_IOCTX]
	ldr	x1,[x19,AKD_BASE]
	add	x1,x1,x21
	movl	x2,AKD_IOBUF
	sub	x2,x2,x21
	ldr	x3,[x19,AKD_IOREAD]
	blr	x3
	cmp	x0,0
	b.le	.have0
	ldr	x1,[x19,AKD_BUFLEN]
	add	x1,x1,x0
	str	x1,[x19,AKD_BUFLEN]
	ldr	x1,[x19,AKD_IOPOS]
	add	x1,x1,x0
	str	x1,[x19,AKD_IOPOS]
	b	.fill
.have0:	mov	x5,0
	ldr	x21,[x19,AKD_BUFLEN]
.have:	ldr	x0,[x19,AKD_BASE]
	ldr	x2,[x19,AKD_OFF]
	ldr	x3,[x19,AKD_BUFOFF]
	sub	x5,x2,x3
	add	x0,x0,x5
	ldr	x4,[x19,AKD_BUFLEN]
	sub	x1,x4,x5
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = octets : avance
akd_skip:
	ldr	x2,[x0,AKD_OFF]
	add	x2,x2,x1
	str	x2,[x0,AKD_OFF]
	ret

; x0 = decodeur, x1 = position
akd_seek:
	str	x1,[x0,AKD_OFF]
	ret

; x0 = destination, x1 = source, x2 = octets (recouvrement permis)
ak_memmove:
	cbz	x2,.ret
	cmp	x0,x1
	b.hi	.back
.f:	cmp	x2,16
	b.lo	.fb
	ldr	q0,[x1],16
	str	q0,[x0],16
	sub	x2,x2,16
	b	.f
.fb:	cbz	x2,.ret
	ldrb	w3,[x1],1
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.fb
.back:	add	x0,x0,x2
	add	x1,x1,x2
.b:	cbz	x2,.ret
	ldrb	w3,[x1,-1]!
	strb	w3,[x0,-1]!
	sub	x2,x2,1
	b	.b
.ret:	ret

; x0 = destination, x1 = octets : a zero
ak_memzero:
	cbz	x1,.ret
	tst	x0,15
	b.ne	.b
	cmp	x1,16
	b.lo	.b
	movi	v0.4s,0
.q:	cmp	x1,16
	b.lo	.b
	str	q0,[x0],16
	sub	x1,x1,16
	b	.q
.b:	cbz	x1,.ret
	strb	wzr,[x0],1
	sub	x1,x1,1
	b	.b
.ret:	ret

; ---------------------------------------------------------------------------
;  Ouverture
; ---------------------------------------------------------------------------

; x0 = tampon, x1 = octets -> decodeur (< 0 : erreur AKE_*)
ak_dec_open_mem:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x0,AKD_BYTES
	bl	ak_alloc
	cbz	x0,.nomem
	mov	x19,x0
	str	x20,[x19,AKD_BASE]
	str	x21,[x19,AKD_LEN]
	b	akd_open_common
.nomem:	mov	x0,AKE_NOMEM
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = lecture (contexte, tampon, octets) -> octets, x1 = positionnement
; (contexte, position), x2 = contexte, x3 = taille du flux -> decodeur
ak_dec_open_io:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	mov	x0,AKD_BYTES
	bl	ak_alloc
	cbz	x0,.nomem
	mov	x19,x0
	movl	x0,AKD_IOBUF+64
	bl	ak_alloc
	cbz	x0,.nomem2
	str	x0,[x19,AKD_BASE]
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	str	x0,[x19,AKD_IOREAD]
	str	x1,[x19,AKD_IOSEEK]
	str	x2,[x19,AKD_IOCTX]
	str	x3,[x19,AKD_LEN]
	mov	x0,1
	str	x0,[x19,AKD_MODE]
	b	akd_open_common
.nomem2: mov	x0,x19
	bl	ak_free
.nomem:	add	sp,sp,32
	mov	x0,AKE_NOMEM
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (suite commune des ouvertures : x19 = decodeur ; meme pile)
akd_open_common:
	ldr	x0,[x19,AKD_LEN]
	str	x0,[x19,AKD_DATAEND]
	mov	x0,-1
	str	x0,[x19,AKD_TOTAL]
	mov	x0,x19
	bl	akd_start
	tbnz	x0,63,.fail
	mov	x0,x19
	b	.out
.fail:	mov	x20,x0
	mov	x0,x19
	bl	akd_free
	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur : reconnait le format et ouvre son module -> 0 / < 0
akd_start:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x22,0				; 1 : ID3v2 vu
.again:	mov	x0,x19
	mov	x1,64
	bl	akd_need
	cmp	x1,12
	b.lo	.unknown
	mov	x20,x0
	ldr	w2,[x20]
	movl	w3,0x00334449			; 'ID3' : etiquettes ID3v2, puis l'audio
	and	w4,w2,0xFFFFFF
	cmp	w4,w3
	b.eq	.id3
	movl	w3,0x46464952			; 'RIFF' ... 'WAVE'
	cmp	w2,w3
	b.ne	.nriff
	ldr	w4,[x20,8]
	movl	w3,0x45564157
	cmp	w4,w3
	b.ne	.unknown
	mov	x0,x19
	bl	wav_open
	b	.out
.nriff:	movl	w3,0x43614C66			; 'fLaC'
	cmp	w2,w3
	b.ne	.nflac
	mov	x0,x19
	bl	flac_open
	b	.out
.nflac:	movl	w3,0x5367674F			; 'OggS'
	cmp	w2,w3
	b.ne	.nogg
	mov	x0,x19
	bl	ogg_open
	b	.out
.nogg:	movl	w3,0x6468544D			; 'MThd'
	cmp	w2,w3
	b.ne	.nmid
	mov	x0,x19
	bl	midi_open
	b	.out
.nmid:	movl	w3,0x732D6D66			; 'fm-s' : 'fm-song-project'
	cmp	w2,w3
	b.ne	.nfms
	mov	x0,x19
	bl	fms_open
	b	.out
.nfms:	ldrb	w3,[x20]			; synchro MP3 (11 bits a 1)
	cmp	w3,0xFF
	b.ne	.mp3s
	ldrb	w3,[x20,1]
	and	w3,w3,0xE0
	cmp	w3,0xE0
	b.ne	.mp3s
.mp3:	mov	x0,x19
	bl	mp3_open
	b	.out
.mp3s:	cbnz	x22,.mp3			; apres ID3v2 : MP3 sans doute (synchro cherchee)
.unknown:
	mov	x0,AKE_FORMAT
	b	.out
.id3:	ldrb	w0,[x20,6]			; taille "synchsafe" (4 x 7 bits)
	ldrb	w1,[x20,7]
	ldrb	w2,[x20,8]
	ldrb	w3,[x20,9]
	lsl	w0,w0,21
	orr	w0,w0,w1,lsl 14
	orr	w0,w0,w2,lsl 7
	orr	w0,w0,w3
	add	x21,x0,10
	ldrb	w1,[x20,5]			; pied de page
	tst	w1,0x10
	b.eq	.nofoot
	add	x21,x21,10
.nofoot: mov	x0,x19				; lire les etiquettes (les 64 premiers Ko)
	mov	x1,x21
	movl	x2,65536
	cmp	x1,x2
	csel	x1,x1,x2,lo
	bl	akd_need
	cmp	x1,x21
	csel	x1,x1,x21,lo
	mov	x2,x0
	mov	x0,x19
	bl	akd_id3v2
	mov	x0,x19
	mov	x1,x21
	bl	akd_skip
	mov	x22,1
	b	.again
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Lecture, informations, recherche, fermeture
; ---------------------------------------------------------------------------

; x0 = decodeur, x1 = tampon, x2 = trames -> trames rendues (s16 entrelace,
; canaux de la source) ; 0 : fin ; < 0 : erreur
ak_dec_read:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,0				; rendues
.loop:	cmp	x22,x21
	b.hs	.done
	ldr	x0,[x19,AKD_PCMN]
	ldr	x1,[x19,AKD_PCMI]
	subs	x23,x0,x1			; disponibles
	b.ne	.have
	ldr	x0,[x19,AKD_EOF]
	cbnz	x0,.done
	ldr	x2,[x19,AKD_NEXT]
	mov	x0,x19
	blr	x2
	cmp	x0,0
	b.gt	.got
	mov	x1,1
	str	x1,[x19,AKD_EOF]
	b.eq	.done
	cbnz	x22,.done
	b	.out				; erreur
.got:	str	x0,[x19,AKD_PCMN]
	ldr	x1,[x19,AKD_SKIP]		; trames a sauter
	cmp	x1,x0
	csel	x2,x1,x0,lo
	str	x2,[x19,AKD_PCMI]
	sub	x1,x1,x2
	str	x1,[x19,AKD_SKIP]
	b	.loop
.have:	sub	x0,x21,x22
	cmp	x23,x0
	csel	x23,x23,x0,lo
	ldr	x0,[x19,AKD_LIMIT]		; fin donnee par le conteneur
	cbz	x0,.conv
	ldr	x0,[x19,AKD_TOTAL]
	ldr	x1,[x19,AKD_POS]
	subs	x0,x0,x1
	csel	x0,xzr,x0,lt
	cmp	x23,x0
	csel	x23,x23,x0,lo
	cbnz	x23,.conv
	mov	x0,1
	str	x0,[x19,AKD_EOF]
	ldr	x0,[x19,AKD_PCMN]
	str	x0,[x19,AKD_PCMI]
	b	.done
.conv:	ldr	x0,[x19,AKD_CH]
	lsl	x0,x0,1
	mul	x0,x0,x22
	add	x3,x20,x0
	mov	x0,x19
	ldr	x1,[x19,AKD_PCMI]
	mov	x2,x23
	mov	x4,AKC_S16
	bl	akd_emit
	ldr	x0,[x19,AKD_PCMI]
	add	x0,x0,x23
	str	x0,[x19,AKD_PCMI]
	ldr	x0,[x19,AKD_POS]
	add	x0,x0,x23
	str	x0,[x19,AKD_POS]
	add	x22,x22,x23
	b	.loop
.done:	mov	x0,x22
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = premiere trame des tampons plans, x2 = trames,
; x3 = sortie entrelacee, x4 = AKC_S16 / AKC_F32
akd_emit:
	ldr	x5,[x0,AKD_CH]
	ldr	x6,[x0,AKD_PCMFMT]
	add	x7,x0,AKD_PCM
	mov	x8,0				; canal
	movl	w9,0x47000000			; 32768.0
	fmov	s7,w9
	movl	w9,0x30000000			; 2^-31
	fmov	s6,w9
	mov	w13,32767
	mov	w14,-32768
.ch:	cmp	x8,x5
	b.hs	.ret
	ldr	x10,[x7,x8,lsl 3]
	add	x10,x10,x1,lsl 2		; source
	cbnz	x4,.f32
	add	x11,x3,x8,lsl 1			; destination s16
	lsl	x12,x5,1			; pas
	mov	x15,x2
	cbnz	x6,.s32
.lf:	cbz	x15,.next
	ldr	s0,[x10],4
	fmul	s0,s0,s7
	fcvtns	w9,s0
	cmp	w9,w13
	csel	w9,w13,w9,gt
	cmp	w9,w14
	csel	w9,w14,w9,lt
	strh	w9,[x11]
	add	x11,x11,x12
	sub	x15,x15,1
	b	.lf
.s32:	cbz	x15,.next
	ldr	w9,[x10],4
	asr	w9,w9,16
	strh	w9,[x11]
	add	x11,x11,x12
	sub	x15,x15,1
	b	.s32
.f32:	add	x11,x3,x8,lsl 2			; destination f32
	lsl	x12,x5,2
	mov	x15,x2
.lg:	cbz	x15,.next
	ldr	s0,[x10],4
	cbz	x6,.st
	fmov	w9,s0
	scvtf	s0,w9
	fmul	s0,s0,s6
.st:	str	s0,[x11]
	add	x11,x11,x12
	sub	x15,x15,1
	b	.lg
.next:	add	x8,x8,1
	b	.ch
.ret:	ret

; x0 = decodeur, x1 = tampon AKI_SIZE -> 0
ak_dec_info:
	ldr	x2,[x0,AKD_FMT]
	str	x2,[x1,AKI_FORMAT]
	ldr	x2,[x0,AKD_RATE]
	str	x2,[x1,AKI_RATE]
	ldr	x3,[x0,AKD_CH]
	str	x3,[x1,AKI_CHANNELS]
	ldr	x3,[x0,AKD_TOTAL]
	str	x3,[x1,AKI_FRAMES]
	mov	x4,-1
	tbnz	x3,63,.d
	cbz	x2,.d
	mov	x4,1000
	mul	x4,x3,x4
	udiv	x4,x4,x2
.d:	str	x4,[x1,AKI_DURATION]
	ldr	x2,[x0,AKD_BITRATE]
	str	x2,[x1,AKI_BITRATE]
	ldr	x2,[x0,AKD_POS]
	str	x2,[x1,AKI_POSITION]
	ldr	x2,[x0,AKD_BITS]
	str	x2,[x1,AKI_BITS]
	mov	x0,0
	ret

; x0 = decodeur -> x0 = position (trames)
ak_dec_tell:
	ldr	x0,[x0,AKD_POS]
	ret

; x0 = decodeur, x1 = position (ms) -> 0 / < 0
ak_dec_seek:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x2,[x19,AKD_SEEKFN]
	cbz	x2,.inval
	cmp	x1,0
	csel	x1,xzr,x1,lt
	ldr	x0,[x19,AKD_RATE]
	mul	x1,x1,x0
	mov	x0,1000
	udiv	x1,x1,x0			; trame voulue
	ldr	x0,[x19,AKD_TOTAL]
	tbnz	x0,63,.go
	cmp	x1,x0
	csel	x1,x1,x0,lo
.go:	str	xzr,[x19,AKD_PCMN]
	str	xzr,[x19,AKD_PCMI]
	str	xzr,[x19,AKD_EOF]
	str	xzr,[x19,AKD_SKIP]
	mov	x0,x19
	blr	x2
	b	.out
.inval:	mov	x0,AKE_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = AKT_*, x2 = tampon, x3 = taille -> longueur (octets,
; sans le 0 final ; 0 : absente)
ak_dec_tag:
	cmp	x1,2
	b.hi	.none
	cbz	x3,.none
	add	x0,x0,AKD_TAGS
	add	x0,x0,x1,lsl 8
	sub	x3,x3,1
	mov	x4,0
.c:	cmp	x4,x3
	b.hs	.end
	ldrb	w5,[x0,x4]
	cbz	w5,.end
	strb	w5,[x2,x4]
	add	x4,x4,1
	b	.c
.end:	strb	wzr,[x2,x4]
	mov	x0,x4
	ret
.none:	mov	x0,0
	ret

; x0 = decodeur -> 0
ak_dec_close:
	prologue
	cbz	x0,.out
	bl	akd_free
.out:	mov	x0,0
	epilogue

; ---------------------------------------------------------------------------
;  Etiquettes
; ---------------------------------------------------------------------------

; x0 = decodeur, x1 = AKT_*, x2 = texte, x3 = octets, x4 = codage
; (0 latin-1, 1 UTF-16 avec BOM, 2 UTF-16BE, 3 UTF-8) : pose l'etiquette si
; elle est encore vide (UTF-8, 255 octets au plus, s'arrete au NUL)
akd_tag_put:
	prologue
	add	x0,x0,AKD_TAGS
	add	x0,x0,x1,lsl 8
	ldrb	w5,[x0]
	cbnz	w5,.ret
	mov	x6,0				; octets ecrits
	add	x7,x2,x3			; fin
	mov	x9,1				; UTF-16 : 1 gros-boutiste
	cmp	x4,1
	b.lo	.latin
	b.eq	.bom
	cmp	x4,2
	b.eq	.u16l
.utf8:	cmp	x2,x7
	b.hs	.end
	ldrb	w5,[x2],1
	cbz	w5,.end
	cmp	x6,254
	b.hs	.end
	strb	w5,[x0,x6]
	add	x6,x6,1
	b	.utf8
.latin:	cmp	x2,x7
	b.hs	.end
	ldrb	w5,[x2],1
	cbz	w5,.end
	cmp	x6,252
	b.hs	.end
	bl	.put
	b	.latin
.bom:	add	x8,x2,2				; BOM : FF FE petit-boutiste
	cmp	x8,x7
	b.hi	.end
	ldrb	w5,[x2]
	add	x2,x2,2
	cmp	w5,0xFE
	cset	x9,eq
.u16l:	add	x8,x2,2
	cmp	x8,x7
	b.hi	.end
	ldrb	w5,[x2]
	ldrb	w8,[x2,1]
	add	x2,x2,2
	cbz	x9,.lle
	orr	w5,w8,w5,lsl 8
	b	.u16
.lle:	orr	w5,w5,w8,lsl 8
.u16:	cbz	w5,.end
	cmp	x6,250
	b.hs	.end
	lsr	w8,w5,11			; demi-codes : remplaces par '?'
	cmp	w8,0x1B
	b.ne	.bmp
	mov	w5,'?'
.bmp:	bl	.put
	b	.u16l
.end:	strb	wzr,[x0,x6]
.trim:	cbz	x6,.ret				; retire les espaces de fin (ID3v1)
	sub	x8,x6,1
	ldrb	w5,[x0,x8]
	cmp	w5,' '
	b.ne	.ret
	strb	wzr,[x0,x8]
	mov	x6,x8
	b	.trim
.ret:	epilogue
; w5 = point de code (< 0x10000) -> UTF-8 en x0 + x6 (x30 : retour local)
.put:	cmp	w5,0x80
	b.hs	.p2
	strb	w5,[x0,x6]
	add	x6,x6,1
	ret
.p2:	cmp	w5,0x800
	b.hs	.p3
	lsr	w8,w5,6
	orr	w8,w8,0xC0
	strb	w8,[x0,x6]
	add	x6,x6,1
	b	.pl
.p3:	lsr	w8,w5,12
	orr	w8,w8,0xE0
	strb	w8,[x0,x6]
	add	x6,x6,1
	lsr	w8,w5,6
	and	w8,w8,0x3F
	orr	w8,w8,0x80
	strb	w8,[x0,x6]
	add	x6,x6,1
.pl:	and	w8,w5,0x3F
	orr	w8,w8,0x80
	strb	w8,[x0,x6]
	add	x6,x6,1
	ret

; x0 = decodeur, x1 = commentaires Vorbis (sans l'en-tete du paquet),
; x2 = octets : TITLE, ARTIST, ALBUM
akd_vcomment:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	add	x21,x1,x2			; fin
	add	x0,x20,4
	cmp	x0,x21
	b.hi	.out
	ldr	w0,[x20]			; fournisseur
	add	x20,x20,4
	add	x20,x20,x0
	add	x0,x20,4
	cmp	x0,x21
	b.hi	.out
	ldr	w22,[x20],4			; commentaires
.c:	cbz	w22,.out
	sub	w22,w22,1
	add	x0,x20,4
	cmp	x0,x21
	b.hi	.out
	ldr	w23,[x20],4			; longueur
	add	x24,x20,x23
	cmp	x24,x21
	b.hi	.out
	mov	x0,x20
	mov	x1,x23
	adr	x2,s_k_title
	bl	akd_key
	mov	x3,AKT_TITLE
	cbnz	x0,.set
	mov	x0,x20
	mov	x1,x23
	adr	x2,s_k_artist
	bl	akd_key
	mov	x3,AKT_ARTIST
	cbnz	x0,.set
	mov	x0,x20
	mov	x1,x23
	adr	x2,s_k_album
	bl	akd_key
	mov	x3,AKT_ALBUM
	cbz	x0,.nx
.set:	add	x2,x20,x0			; valeur apres '='
	sub	x0,x23,x0
	mov	x1,x3
	mov	x3,x0
	mov	x0,x19
	mov	x4,3
	bl	akd_tag_put
.nx:	mov	x20,x24
	b	.c
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = "CLE=valeur", x1 = octets, x2 = cle (majuscules, 0) -> x0 = place de la
; valeur (0 : autre cle)
akd_key:
	mov	x3,0
.c:	ldrb	w4,[x2,x3]
	cbz	w4,.eq
	cmp	x3,x1
	b.hs	.no
	ldrb	w5,[x0,x3]
	sub	w6,w5,'a'
	cmp	w6,25
	b.hi	.u
	sub	w5,w5,32
.u:	cmp	w5,w4
	b.ne	.no
	add	x3,x3,1
	b	.c
.eq:	cmp	x3,x1
	b.hs	.no
	ldrb	w5,[x0,x3]
	cmp	w5,'='
	b.ne	.no
	add	x0,x3,1
	ret
.no:	mov	x0,0
	ret

; x0 = decodeur, x1 = octets disponibles, x2 = debut de l'etiquette ("ID3")
akd_id3v2:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	add	x21,x2,x1			; fin
	ldrb	w22,[x2,3]			; version (2, 3, 4)
	ldrb	w0,[x2,5]
	add	x20,x2,10
	tbz	w0,6,.frames			; en-tete etendu
	ldrb	w1,[x20]
	ldrb	w2,[x20,1]
	ldrb	w3,[x20,2]
	ldrb	w4,[x20,3]
	cmp	w22,4
	b.eq	.ss
	lsl	w1,w1,24			; 2.3 : taille sans elle-meme
	orr	w1,w1,w2,lsl 16
	orr	w1,w1,w3,lsl 8
	orr	w1,w1,w4
	add	x1,x1,4
	b	.ext
.ss:	lsl	w1,w1,21
	orr	w1,w1,w2,lsl 14
	orr	w1,w1,w3,lsl 7
	orr	w1,w1,w4
.ext:	add	x20,x20,x1
.frames:
	cmp	w22,2
	b.eq	.v22
.f:	add	x0,x20,10			; trame 2.3 / 2.4 : id 4, taille 4, drapeaux 2
	cmp	x0,x21
	b.hi	.out
	ldrb	w0,[x20]
	cbz	w0,.out				; bourrage
	ldrb	w1,[x20,4]
	ldrb	w2,[x20,5]
	ldrb	w3,[x20,6]
	ldrb	w4,[x20,7]
	cmp	w22,4
	b.eq	.fss
	lsl	w1,w1,24
	orr	w1,w1,w2,lsl 16
	orr	w1,w1,w3,lsl 8
	orr	w23,w1,w4
	b	.fsz
.fss:	lsl	w1,w1,21
	orr	w1,w1,w2,lsl 14
	orr	w1,w1,w3,lsl 7
	orr	w23,w1,w4
.fsz:	add	x24,x20,10
	add	x24,x24,x23			; trame suivante
	cmp	x24,x21
	b.hi	.out
	ldr	w0,[x20]
	movl	w1,0x32544954			; TIT2
	mov	x3,AKT_TITLE
	cmp	w0,w1
	b.eq	.txt
	movl	w1,0x31455054			; TPE1
	mov	x3,AKT_ARTIST
	cmp	w0,w1
	b.eq	.txt
	movl	w1,0x424C4154			; TALB
	mov	x3,AKT_ALBUM
	cmp	w0,w1
	b.ne	.fn
.txt:	add	x2,x20,10
	bl	.put
.fn:	mov	x20,x24
	b	.f
.v22:	add	x0,x20,6			; 2.2 : id 3, taille 3
	cmp	x0,x21
	b.hi	.out
	ldrb	w0,[x20]
	cbz	w0,.out
	ldrb	w1,[x20,3]
	ldrb	w2,[x20,4]
	ldrb	w3,[x20,5]
	lsl	w1,w1,16
	orr	w1,w1,w2,lsl 8
	orr	w23,w1,w3
	add	x24,x20,6
	add	x24,x24,x23
	cmp	x24,x21
	b.hi	.out
	ldr	w0,[x20]
	and	w0,w0,0xFFFFFF
	movl	w1,0x325454			; TT2
	mov	x3,AKT_TITLE
	cmp	w0,w1
	b.eq	.txt2
	movl	w1,0x315054			; TP1
	mov	x3,AKT_ARTIST
	cmp	w0,w1
	b.eq	.txt2
	movl	w1,0x4C4154			; TAL
	mov	x3,AKT_ALBUM
	cmp	w0,w1
	b.ne	.fn2
.txt2:	add	x2,x20,6
	bl	.put
.fn2:	mov	x20,x24
	b	.v22
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
; x2 = donnees de la trame (codage, texte), x23 = octets, x3 = AKT_*
.put:	cbz	x23,.pr
	stp	x29,x30,[sp,-16]!
	ldrb	w4,[x2]
	add	x2,x2,1
	mov	x1,x3
	sub	x3,x23,1
	mov	x0,x19
	cmp	w4,3
	b.hi	.pskip
	bl	akd_tag_put
.pskip:	ldp	x29,x30,[sp],16
.pr:	ret

; x0 = decodeur, x1 = les 128 octets de la fin du fichier : ID3v1 ("TAG")
; -> x0 = 1 s'il y en a un
akd_id3v1:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldrb	w0,[x20]
	ldrb	w1,[x20,1]
	ldrb	w2,[x20,2]
	cmp	w0,'T'
	b.ne	.no
	cmp	w1,'A'
	b.ne	.no
	cmp	w2,'G'
	b.ne	.no
	mov	x0,x19
	mov	x1,AKT_TITLE
	add	x2,x20,3
	mov	x3,30
	mov	x4,0
	bl	akd_tag_put
	mov	x0,x19
	mov	x1,AKT_ARTIST
	add	x2,x20,33
	mov	x3,30
	mov	x4,0
	bl	akd_tag_put
	mov	x0,x19
	mov	x1,AKT_ALBUM
	add	x2,x20,63
	mov	x3,30
	mov	x4,0
	bl	akd_tag_put
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

s_k_title	db 'TITLE',0
s_k_artist	db 'ARTIST',0
s_k_album	db 'ALBUM',0
		align	4

; ---------------------------------------------------------------------------
;  API par paquets (MediaKit) : docs/KITS.md
; ---------------------------------------------------------------------------

; x0 = format AKF_*, x1 = frequence, x2 = canaux, x3 = donnees du codec
; (extradata : en-tetes Xiph pour Vorbis, OpusHead, STREAMINFO pour FLAC ;
; 0 pour MP3), x4 = octets -> poignee (< 0 : erreur)
ak_codec_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x23,x3
	mov	x24,x4
	mov	x0,AKD_BYTES
	bl	ak_alloc
	cbz	x0,.nomem
	mov	x19,x0
	mov	x0,2
	str	x0,[x19,AKD_MODE]
	str	x20,[x19,AKD_FMT]
	str	x21,[x19,AKD_RATE]
	str	x22,[x19,AKD_CH]
	mov	x0,-1
	str	x0,[x19,AKD_TOTAL]
	mov	x0,x19
	mov	x1,x23
	mov	x2,x24
	cmp	x20,AKF_FLAC
	b.eq	.flac
	cmp	x20,AKF_MP3
	b.eq	.mp3
	cmp	x20,AKF_VORBIS
	b.eq	.vorbis
	cmp	x20,AKF_OPUS
	b.eq	.opus
	mov	x0,AKE_FORMAT
	b	.fail
.opus:	bl	ops_init_pkt
	b	.res
.vorbis:
	bl	vb_init_pkt
	b	.res
.mp3:	bl	mp3_init_pkt
	b	.res
.flac:	bl	flac_init_pkt
.res:	tbnz	x0,63,.fail
	mov	x0,x19
	b	.out
.fail:	mov	x20,x0
	mov	x0,x19
	bl	akd_free
	mov	x0,x20
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = poignee, x1 = paquet, x2 = octets, x3 = sortie entrelacee, x4 = trames
; au plus (place de la sortie), x5 = AKC_S16 / AKC_F32 -> trames rendues
; (0 : rien a rendre, < 0 : erreur)
ak_codec_decode:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x3
	mov	x21,x4
	mov	x22,x5
	ldr	x3,[x19,AKD_PKTFN]
	cbz	x3,.inval
	blr	x3
	cmp	x0,0
	b.le	.out
	cmp	x0,x21
	csel	x21,x0,x21,lo
	mov	x0,x19
	mov	x1,0
	mov	x2,x21
	mov	x3,x20
	mov	x4,x22
	bl	akd_emit
	mov	x0,x21
	b	.out
.inval:	mov	x0,AKE_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = poignee : oublie l'historique (apres une recherche) -> 0
ak_codec_reset:
	prologue
	ldr	x1,[x0,AKD_RESETFN]
	cbz	x1,.out
	blr	x1
.out:	mov	x0,0
	epilogue

; x0 = poignee, x1 = tampon AKI_SIZE (frequence, canaux reels : apres le
; premier paquet pour MP3) -> 0
ak_codec_info:
	b	ak_dec_info

; x0 = poignee -> 0
ak_codec_close:
	b	ak_dec_close
