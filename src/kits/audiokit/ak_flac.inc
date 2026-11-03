; ============================================================================
;  kits/audiokit/ak_flac.inc - FLAC (RFC 9639) : decodage
;
;  Metadonnees : STREAMINFO, SEEKTABLE, VORBIS_COMMENT (le reste est saute).
;  Trame : en-tete (CRC-8 verifie), sous-trames CONSTANT, VERBATIM, FIXED
;  (ordres 0-4), LPC (ordres 1-32), residu de Rice (parametres de 4 ou 5
;  bits, echappement), bits perdus ; stereo gauche/cote, cote/droite,
;  milieu/cote. 4 a 24 bits par echantillon, 1 a 8 canaux.
;  Sortie : entiers 32 bits cadres a gauche (AKD_S32).
; ============================================================================

FLC_MINBS	= 0
FLC_MAXBS	= 8
FLC_MAXFRAME	= 16
FLC_RATE	= 24
FLC_CH		= 32
FLC_BPS		= 40
FLC_SEEKTAB	= 48		; points de SEEKTABLE (18 o chacun, gros-boutistes)
FLC_NSEEK	= 56
FLC_H_BS	= 64		; en-tete de la derniere trame lue
FLC_H_RATE	= 72
FLC_H_CHA	= 80		; affectation des canaux (0-10)
FLC_H_BPS	= 88
FLC_H_NUM	= 96		; premier echantillon de la trame
FLC_H_LEN	= 104		; octets de l'en-tete (CRC-8 compris)
FLC_H_NCH	= 112
FLC_COEF	= 128		; 32 coefficients (32 bits)
FLC_SIZE	= 256
FLC_MAXBLOCK	= 65536

; x0 = decodeur (lecture sur "fLaC") -> 0 / < 0
flac_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x1,FLC_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x20,x0
	str	x20,[x19,AKD_CODEC]
	mov	x0,x19
	mov	x1,4
	bl	akd_skip
	mov	x23,0				; STREAMINFO lu ?
.blk:	mov	x0,x19
	mov	x1,4
	bl	akd_need
	cmp	x1,4
	b.lo	.bad
	ldr	w1,[x0]
	rev	w1,w1
	ubfx	w21,w1,24,7			; type
	ubfx	w24,w1,31,1			; dernier ?
	and	w22,w1,0xFFFFFF			; longueur
	cmp	w21,0
	b.eq	.si
	cmp	w21,3
	b.eq	.st
	cmp	w21,4
	b.eq	.vc
.skip:	mov	x0,x19
	add	x1,x22,4
	bl	akd_skip
	cbz	w24,.blk
	b	.meta_end
.si:	cmp	w22,34
	b.lo	.bad
	mov	x0,x19
	mov	x1,38
	bl	akd_need
	cmp	x1,38
	b.lo	.bad
	add	x1,x0,4
	mov	x0,x20
	bl	flac_streaminfo
	mov	x23,1
	b	.skip
.st:	mov	x0,x19				; SEEKTABLE : copie
	add	x1,x22,4
	bl	akd_need
	add	x2,x22,4
	cmp	x1,x2
	b.lo	.skip
	mov	x21,x0
	mov	x0,x19
	mov	x1,x22
	bl	akd_alloc
	cbz	x0,.skip
	str	x0,[x20,FLC_SEEKTAB]
	add	x1,x21,4
	mov	x2,x22
	bl	ak_memmove
	mov	x0,18
	udiv	x0,x22,x0
	str	x0,[x20,FLC_NSEEK]
	b	.skip
.vc:	mov	x0,x19				; VORBIS_COMMENT : etiquettes
	add	x1,x22,4
	movl	x2,65536
	cmp	x1,x2
	csel	x1,x1,x2,lo
	bl	akd_need
	sub	x1,x1,4
	cmp	x1,x22
	csel	x2,x1,x22,lo
	add	x1,x0,4
	mov	x0,x19
	bl	akd_vcomment
	b	.skip
.meta_end:
	cbz	x23,.bad
	ldr	x0,[x19,AKD_OFF]
	str	x0,[x19,AKD_DATA0]
	mov	x0,x19
	bl	flac_setup
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat FLAC, x1 = STREAMINFO (34 o)
flac_streaminfo:
	ldrb	w2,[x1]
	ldrb	w3,[x1,1]
	orr	w2,w3,w2,lsl 8
	str	x2,[x0,FLC_MINBS]
	ldrb	w2,[x1,2]
	ldrb	w3,[x1,3]
	orr	w2,w3,w2,lsl 8
	str	x2,[x0,FLC_MAXBS]
	ldrb	w2,[x1,7]			; taille maximale de trame (24 bits)
	ldrb	w3,[x1,8]
	ldrb	w4,[x1,9]
	lsl	w2,w2,16
	orr	w2,w2,w3,lsl 8
	orr	w2,w2,w4
	str	x2,[x0,FLC_MAXFRAME]
	ldr	x2,[x1,10]			; frequence 20, canaux 3, bits 5, total 36
	rev	x2,x2
	lsr	x3,x2,44
	str	x3,[x0,FLC_RATE]
	ubfx	x3,x2,41,3
	add	x3,x3,1
	str	x3,[x0,FLC_CH]
	ubfx	x3,x2,36,5
	add	x3,x3,1
	str	x3,[x0,FLC_BPS]
	ubfx	x3,x2,0,36
	str	x3,[x0,FLC_H_NUM]		; (total : rendu par flac_setup)
	ret

; x0 = decodeur (STREAMINFO lu) : champs du decodeur, tampons -> 0 / < 0
flac_setup:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	ldr	x0,[x20,FLC_BPS]
	cmp	x0,4
	b.lo	.unsup
	cmp	x0,24
	b.hi	.unsup
	ldr	x0,[x20,FLC_RATE]
	cbz	x0,.unsup
	mov	x0,AKF_FLAC
	str	x0,[x19,AKD_FMT]
	ldr	x0,[x20,FLC_RATE]
	str	x0,[x19,AKD_RATE]
	ldr	x0,[x20,FLC_CH]
	str	x0,[x19,AKD_CH]
	ldr	x0,[x20,FLC_BPS]
	str	x0,[x19,AKD_BITS]
	ldr	x0,[x20,FLC_H_NUM]		; total d'echantillons (0 : inconnu)
	mov	x1,-1
	cmp	x0,0
	csel	x0,x1,x0,eq
	str	x0,[x19,AKD_TOTAL]
	tbnz	x0,63,.nobr
	ldr	x1,[x19,AKD_DATAEND]		; debit moyen
	ldr	x2,[x19,AKD_DATA0]
	subs	x1,x1,x2
	b.le	.nobr
	lsl	x1,x1,3
	ldr	x2,[x20,FLC_RATE]
	mul	x1,x1,x2
	udiv	x1,x1,x0
	str	x1,[x19,AKD_BITRATE]
.nobr:	mov	x0,AKD_S32
	str	x0,[x19,AKD_PCMFMT]
	adr	x0,flac_next
	str	x0,[x19,AKD_NEXT]
	adr	x0,flac_seek
	str	x0,[x19,AKD_SEEKFN]
	adr	x0,flac_pkt
	str	x0,[x19,AKD_PKTFN]
	ldr	x1,[x20,FLC_MAXBS]
	cmp	x1,16
	b.hs	.mb
	mov	x1,FLC_MAXBLOCK
.mb:	mov	x0,x19
	mov	x2,8				; (8 plans : une trame peut en avoir plus)
	bl	akd_planes
	b	.out
.unsup:	mov	x0,AKE_FORMAT
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = donnees du codec, x2 = octets (API par paquets) :
; "fLaC" + blocs, un bloc STREAMINFO avec son en-tete, ou STREAMINFO seul
flac_init_pkt:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	mov	x1,FLC_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x20,x0
	str	x20,[x19,AKD_CODEC]
	ldr	x0,[x19,AKD_RATE]		; par defaut : les parametres donnes
	str	x0,[x20,FLC_RATE]
	ldr	x0,[x19,AKD_CH]
	str	x0,[x20,FLC_CH]
	mov	x0,16
	str	x0,[x20,FLC_BPS]
	cmp	x22,34
	b.lo	.go
	ldr	w0,[x21]
	movl	w1,0x43614C66			; 'fLaC'
	cmp	w0,w1
	b.ne	.hdr
	add	x21,x21,4
	sub	x22,x22,4
.hdr:	cmp	x22,38				; en-tete de bloc STREAMINFO ?
	b.lo	.raw
	ldrb	w0,[x21]
	and	w0,w0,0x7F
	cbnz	w0,.raw
	ldrb	w0,[x21,3]
	cmp	w0,34
	b.ne	.raw
	add	x21,x21,4
	sub	x22,x22,4
.raw:	cmp	x22,34
	b.lo	.go
	mov	x0,x20
	mov	x1,x21
	bl	flac_streaminfo
.go:	str	xzr,[x20,FLC_H_NUM]
	mov	x0,x19
	bl	flac_setup
	b	.out
.nomem:	mov	x0,AKE_NOMEM
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = paquet (une trame), x2 = octets -> trames
flac_pkt:
	prologue
	bl	flac_frame
	epilogue

; x0 = decodeur -> trames decodees (0 : fin)
flac_next:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x23,0				; octets sautes (resynchronisation)
.again:	ldr	x0,[x19,AKD_DATAEND]
	ldr	x1,[x19,AKD_OFF]
	subs	x22,x0,x1			; restants
	b.le	.end
	ldr	x1,[x20,FLC_MAXFRAME]
	add	x1,x1,64
	cmp	x1,64
	b.ne	.w
	movl	x1,AKD_IOBUF
.w:	mov	x0,x19
	bl	akd_need
	cmp	x1,x22
	csel	x22,x1,x22,lo
	cmp	x22,2
	b.lo	.end
	mov	x21,x0
	mov	x0,x19
	mov	x1,x21
	mov	x2,x22
	bl	flac_frame
	cmp	x0,0
	b.le	.bad
	mov	x24,x0
	mov	x0,x19
	bl	akd_skip			; x1 = octets de la trame
	mov	x0,x24
	b	.out
.bad:	mov	x0,x19				; trame illisible : chercher la suivante
	mov	x1,1
	bl	akd_skip
	add	x23,x23,1
	movl	x0,1048576
	cmp	x23,x0
	b.lo	.again
	mov	x0,AKE_CORRUPT
	b	.out
.end:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat FLAC, x1 = donnees, x2 = octets : lit un en-tete de trame
; -> x0 = 0 (FLC_H_* poses), < 0 si ce n'en est pas un
flac_hdr:
	cmp	x2,6
	b.lo	.bad
	ldrb	w3,[x1]
	cmp	w3,0xFF
	b.ne	.bad
	ldrb	w3,[x1,1]
	and	w4,w3,0xFE
	cmp	w4,0xF8
	b.ne	.bad
	and	w15,w3,1			; strategie : 1 taille variable
	ldrb	w3,[x1,2]
	lsr	w4,w3,4				; taille de bloc
	and	w5,w3,15			; frequence
	ldrb	w3,[x1,3]
	lsr	w6,w3,4				; canaux
	ubfx	w7,w3,1,3			; bits
	tbnz	w3,0,.bad
	cbz	w4,.bad
	cmp	w5,15
	b.eq	.bad
	cmp	w6,10
	b.hi	.bad
	cmp	w7,3
	b.eq	.bad
	; numero (UTF-8 jusqu'a 36 bits)
	add	x8,x1,4
	ldrb	w3,[x8],1
	mov	x13,x3				; valeur
	mov	x14,0				; octets de suite
	tbz	w3,7,.num
	mov	w14,0
.lead:	lsl	w16,w3,w14			; nombre de 1 de tete
	tbz	w16,7,.cnt
	add	w14,w14,1
	cmp	w14,7
	b.hi	.bad
	b	.lead
.cnt:	cmp	w14,1
	b.eq	.bad
	sub	w14,w14,1			; octets de suite
	add	w16,w14,2
	mov	w17,0xFF
	lsr	w17,w17,w16
	and	x13,x13,x17
	cmp	w14,6
	b.ne	.cont
	mov	x13,0				; 11111110 : 36 bits dans la suite
.cont:	cbz	w14,.num
	ldrb	w3,[x8],1
	and	w16,w3,0xC0
	cmp	w16,0x80
	b.ne	.bad
	and	x3,x3,0x3F
	orr	x13,x3,x13,lsl 6
	sub	w14,w14,1
	b	.cont
.num:	; taille de bloc
	cmp	w4,1
	b.ne	.b2
	mov	x4,192
	b	.bsok
.b2:	cmp	w4,5
	b.hi	.b6
	sub	w16,w4,2
	mov	x4,576
	lsl	x4,x4,x16
	b	.bsok
.b6:	cmp	w4,6
	b.ne	.b7
	ldrb	w4,[x8],1
	add	x4,x4,1
	b	.bsok
.b7:	cmp	w4,7
	b.ne	.b8
	ldrb	w16,[x8]
	ldrb	w4,[x8,1]
	add	x8,x8,2
	orr	w4,w4,w16,lsl 8
	add	x4,x4,1
	b	.bsok
.b8:	sub	w16,w4,8
	mov	x4,256
	lsl	x4,x4,x16
.bsok:	; frequence
	cbnz	w5,.r1
	ldr	x5,[x0,FLC_RATE]
	b	.rok
.r1:	cmp	w5,12
	b.lo	.rtab
	b.ne	.r13
	ldrb	w5,[x8],1
	mov	x16,1000
	mul	x5,x5,x16
	b	.rok
.r13:	ldrb	w16,[x8]
	ldrb	w3,[x8,1]
	add	x8,x8,2
	orr	w3,w3,w16,lsl 8
	cmp	w5,13
	mov	x5,x3
	b.eq	.rok
	mov	x16,10
	mul	x5,x3,x16
	b	.rok
.rtab:	adr	x16,flac_rates
	ldr	w5,[x16,x5,lsl 2]
.rok:	; bits
	cbnz	w7,.s1
	ldr	x7,[x0,FLC_BPS]
	b	.sok
.s1:	adr	x16,flac_sizes
	ldrb	w7,[x16,x7]
.sok:	; CRC-8 (x^8 + x^2 + x + 1) de l'en-tete
	sub	x16,x8,x1			; octets avant le CRC
	add	x3,x16,1
	cmp	x3,x2
	b.hi	.bad
	mov	w17,0
	mov	x3,x1
.crc:	cmp	x3,x8
	b.hs	.crcok
	ldrb	w14,[x3],1
	eor	w17,w17,w14
	mov	w14,8
.cb:	tst	w17,0x80
	lsl	w17,w17,1
	and	w17,w17,0xFF
	b.eq	.cn
	eor	w17,w17,7
.cn:	subs	w14,w14,1
	b.ne	.cb
	b	.crc
.crcok:	ldrb	w14,[x8]
	cmp	w14,w17
	b.ne	.bad
	add	x16,x16,1
	str	x16,[x0,FLC_H_LEN]
	str	x4,[x0,FLC_H_BS]
	str	x5,[x0,FLC_H_RATE]
	str	x6,[x0,FLC_H_CHA]
	str	x7,[x0,FLC_H_BPS]
	cmp	w6,8
	add	w3,w6,1
	mov	w14,2
	csel	w3,w3,w14,lo
	str	x3,[x0,FLC_H_NCH]
	cbnz	w15,.var			; taille fixe : numero de trame
	ldr	x3,[x0,FLC_MINBS]
	ldr	x14,[x0,FLC_MAXBS]
	cmp	x3,x14
	csel	x14,x4,x14,ne
	cmp	x14,16
	csel	x14,x4,x14,lo
	mul	x13,x13,x14
.var:	str	x13,[x0,FLC_H_NUM]
	cmp	x7,4
	b.lo	.bad
	cmp	x7,24
	b.hi	.bad
	mov	x0,0
	ret
.bad:	mov	x0,AKE_CORRUPT
	ret

; x0 = decodeur, x1 = trame, x2 = octets -> x0 = trames (< 0 : erreur),
; x1 = octets de la trame
flac_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x21,x1
	mov	x22,x2
	mov	x0,x20
	bl	flac_hdr
	tbnz	x0,63,.out
	ldr	x26,[x20,FLC_H_BS]
	ldr	x0,[x20,FLC_MAXBS]
	cmp	x0,16
	b.hs	.mx
	mov	x0,FLC_MAXBLOCK
.mx:	cmp	x26,x0
	b.hi	.bad
	ldr	x0,[x20,FLC_H_NCH]
	cmp	x0,8
	b.hi	.bad
	ldr	x0,[x20,FLC_H_LEN]		; lecteur apres l'en-tete
	add	x0,x21,x0
	add	x1,x21,x22
	sub	x1,x1,x0
	rb_init
	mov	x23,0				; canal
.sub:	ldr	x0,[x20,FLC_H_NCH]
	cmp	x23,x0
	b.hs	.subs_done
	add	x0,x19,AKD_PCM
	ldr	x24,[x0,x23,lsl 3]
	ldr	x25,[x20,FLC_H_BPS]
	ldr	x0,[x20,FLC_H_CHA]		; le canal de cote a un bit de plus
	cmp	x0,8
	b.lo	.nside
	cmp	x0,9
	cset	x1,eq				; 9 : cote/droite -> le canal 0
	cmp	x23,0
	cset	x2,eq
	cmp	x1,x2				; 8, 10 -> le canal 1
	cinc	x25,x25,eq
.nside:	bl	flac_sub
	tbnz	x0,63,.out
	add	x23,x23,1
	b	.sub
.subs_done:
	; decorrelation stereo
	add	x0,x19,AKD_PCM
	ldp	x1,x2,[x0]
	mov	x3,x26
	ldr	x0,[x20,FLC_H_CHA]
	cmp	x0,8
	b.eq	.ls
	cmp	x0,9
	b.eq	.sr
	cmp	x0,10
	b.ne	.scale
.ms:	ldr	w4,[x1]				; milieu
	ldr	w5,[x2]				; cote
	and	w6,w5,1
	orr	w4,w6,w4,lsl 1
	add	w6,w4,w5
	sub	w7,w4,w5
	asr	w6,w6,1
	asr	w7,w7,1
	str	w6,[x1],4
	str	w7,[x2],4
	subs	x3,x3,1
	b.ne	.ms
	b	.scale
.ls:	ldr	w4,[x1],4			; droite = gauche - cote
	ldr	w5,[x2]
	sub	w5,w4,w5
	str	w5,[x2],4
	subs	x3,x3,1
	b.ne	.ls
	b	.scale
.sr:	ldr	w4,[x1]				; gauche = cote + droite
	ldr	w5,[x2],4
	add	w4,w4,w5
	str	w4,[x1],4
	subs	x3,x3,1
	b.ne	.sr
.scale:	; cadre a gauche sur 32 bits
	ldr	x0,[x20,FLC_H_BPS]
	mov	x4,32
	sub	x4,x4,x0
	mov	x23,0
.sc:	ldr	x0,[x20,FLC_H_NCH]
	cmp	x23,x0
	b.hs	.sc_done
	add	x0,x19,AKD_PCM
	ldr	x1,[x0,x23,lsl 3]
	mov	x3,x26
	dup	v1.4s,w4
.sv:	cmp	x3,4
	b.lo	.s1
	ldr	q0,[x1]
	ushl	v0.4s,v0.4s,v1.4s
	str	q0,[x1],16
	sub	x3,x3,4
	b	.sv
.s1:	cbz	x3,.snx
	ldr	w5,[x1]
	lsl	w5,w5,w4
	str	w5,[x1],4
	sub	x3,x3,1
	b	.s1
.snx:	add	x23,x23,1
	b	.sc
.sc_done:
	; plus de canaux dans la trame que dans le flux : on rend ceux du flux
	rb_align
	mov	x1,16				; CRC-16 (non verifie)
	bl	rb_get
	rb_byteptr
	sub	x1,x0,x21			; octets de la trame
	cmp	x1,x22
	b.hi	.bad
	ldr	x2,[x19,AKD_MODE]		; (paquets : le flux peut changer)
	cmp	x2,2
	b.ne	.ok
	ldr	x2,[x20,FLC_H_NCH]
	str	x2,[x19,AKD_CH]
	ldr	x2,[x20,FLC_H_RATE]
	str	x2,[x19,AKD_RATE]
.ok:	mov	x0,x26
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Sous-trame (interne a flac_frame : x19 decodeur, x20 etat, x24 = sortie,
; x25 = bits, x26 = taille du bloc ; lecteur en registres x9-x12)
; -> x0 = 0 / < 0
flac_sub:
	stp	x29,x30,[sp,-16]!
	mov	x1,8
	bl	rb_get
	tbnz	w0,7,.bad			; bit de bourrage
	ubfx	w27,w0,1,6			; type
	tbz	w0,0,.nowaste
	bl	rb_unary			; bits perdus
	add	x28,x0,1
	sub	x25,x25,x28
	cmp	x25,1
	b.lt	.bad
	b	.type
.nowaste:
	mov	x28,0
.type:	cbz	w27,.const
	cmp	w27,1
	b.eq	.verb
	cmp	w27,8
	b.lo	.bad
	cmp	w27,12
	b.ls	.fixed
	cmp	w27,32
	b.lo	.bad
	; LPC
	sub	x27,x27,31			; ordre
	cmp	x27,x26
	b.hi	.bad
	bl	.warm
	mov	x1,4
	bl	rb_get
	cmp	x0,15
	b.eq	.bad
	add	x13,x0,1			; precision
	mov	x1,5
	bl	rb_sget
	tbnz	x0,63,.bad
	mov	x14,x0				; decalage
	add	x15,x20,FLC_COEF
	mov	x7,0
.co:	cmp	x7,x27
	b.hs	.co_done
	mov	x1,x13
	bl	rb_sget
	str	w0,[x15,x7,lsl 2]
	add	x7,x7,1
	b	.co
.co_done:
	stp	x14,xzr,[sp,-16]!
	bl	flac_residual
	ldp	x14,x15,[sp],16
	tbnz	x0,63,.bad
	bl	flac_lpc
	b	.waste
.fixed:	sub	x27,x27,8			; FIXED : ordre 0..4
	cmp	x27,x26
	b.hi	.bad
	bl	.warm
	bl	flac_residual
	tbnz	x0,63,.bad
	cbz	x27,.waste
	adr	x0,flac_fixed			; coefficients de l'ordre
	sub	x1,x27,1
	add	x0,x0,x1,lsl 4
	ldp	x1,x2,[x0]
	add	x3,x20,FLC_COEF
	stp	x1,x2,[x3]
	mov	x14,0
	bl	flac_lpc
	b	.waste
.const:	mov	x1,x25
	bl	rb_sget
	mov	x2,x26
	mov	x3,x24
.cl:	str	w0,[x3],4
	subs	x2,x2,1
	b.ne	.cl
	b	.waste
.verb:	mov	x13,0
.vl:	mov	x1,x25
	bl	rb_sget
	str	w0,[x24,x13,lsl 2]
	add	x13,x13,1
	cmp	x13,x26
	b.lo	.vl
.waste:	cbz	x28,.ok
	mov	x2,x26
	mov	x3,x24
.wl:	ldr	w0,[x3]
	lsl	w0,w0,w28
	str	w0,[x3],4
	subs	x2,x2,1
	b.ne	.wl
.ok:	mov	x0,0
	ldp	x29,x30,[sp],16
	ret
.bad:	mov	x0,AKE_CORRUPT
	ldp	x29,x30,[sp],16
	ret
; echantillons de chauffe (x27 de x25 bits)
.warm:	stp	x29,x30,[sp,-16]!
	mov	x13,0
.wa:	cmp	x13,x27
	b.hs	.wd
	mov	x1,x25
	bl	rb_sget
	str	w0,[x24,x13,lsl 2]
	add	x13,x13,1
	b	.wa
.wd:	ldp	x29,x30,[sp],16
	ret

; residu (interne) : x24 = sortie, x26 = bloc, x27 = ordre du predicteur
; -> x0 = 0 / < 0 ; residus en x24[x27..x26[
flac_residual:
	stp	x29,x30,[sp,-16]!
	mov	x1,2
	bl	rb_get
	cmp	x0,1
	b.hi	.bad
	add	x7,x0,4				; bits du parametre (4 ou 5)
	mov	x6,15
	lsl	x6,x6,x0
	add	x6,x6,x0			; echappement : 15 ou 31
	mov	x1,4
	bl	rb_get
	mov	x5,x0				; ordre de partition
	lsr	x4,x26,x5			; echantillons par partition
	lsl	x0,x4,x5
	cmp	x0,x26
	b.ne	.bad
	cmp	x4,x27
	b.lo	.bad
	add	x13,x24,x27,lsl 2		; sortie
	mov	x3,1
	lsl	x3,x3,x5			; partitions
	sub	x14,x4,x27			; la premiere : moins l'ordre
.part:	mov	x1,x7
	bl	rb_get
	mov	x15,x0				; parametre de Rice
	cmp	x15,x6
	b.eq	.esc
	cbz	x14,.pnext
.r1:	clz	x16,x9				; quotient (unaire)
	cmp	x16,x10
	b.hs	.slowq
	lsl	x9,x9,x16			; (en deux fois : un decalage de
	lsl	x9,x9,1				;  64 ne ferait rien)
	sub	x10,x10,x16
	sub	x10,x10,1
.qok:	cbz	x15,.k0
	cmp	x10,x15
	b.hs	.kr
	mov	x1,x16
	bl	rb_fill
	mov	x16,x1
.kr:	mov	x17,64
	sub	x17,x17,x15
	lsr	x0,x9,x17
	lsl	x9,x9,x15
	sub	x10,x10,x15
	lsl	x16,x16,x15
	orr	x16,x16,x0
.k0:	and	x0,x16,1			; zigzag
	neg	x0,x0
	eor	x16,x0,x16,lsr 1
	str	w16,[x13],4
	subs	x14,x14,1
	b.ne	.r1
	b	.pnext
.slowq:	bl	rb_unary
	mov	x16,x0
	add	x0,x12,8
	cmp	x11,x0
	b.hi	.bad
	b	.qok
.esc:	mov	x1,5				; echappement : n bits bruts
	bl	rb_get
	mov	x15,x0
.el:	cbz	x14,.pnext
	mov	x1,x15
	bl	rb_sget
	str	w0,[x13],4
	sub	x14,x14,1
	b	.el
.pnext:	mov	x14,x4
	subs	x3,x3,1
	b.ne	.part
	mov	x0,0
	ldp	x29,x30,[sp],16
	ret
.bad:	mov	x0,AKE_CORRUPT
	ldp	x29,x30,[sp],16
	ret

; prediction (interne) : x24 = echantillons (chauffe + residus), x26 = bloc,
; x27 = ordre, x14 = decalage, coefficients en FLC_COEF
; s[i] += (somme c[j] s[i-1-j]) >> decalage
flac_lpc:
	add	x15,x20,FLC_COEF
	mov	x13,x27				; i
	cmp	x27,8
	b.hi	.gen
	; ordre <= 8 : coefficients en registres
	ldp	w0,w1,[x15]
	ldp	w2,w3,[x15,8]
	ldp	w4,w5,[x15,16]
	ldp	w6,w7,[x15,24]
	adr	x16,.tab
	sub	x17,x27,1
	ldrsw	x17,[x16,x17,lsl 2]
	add	x16,x16,x17
	add	x8,x24,x13,lsl 2		; &s[i]
.l8:	cmp	x13,x26
	b.hs	.ret
	br	x16
.o8:	ldur	w17,[x8,-32]
	smull	x17,w17,w7
	b	.m7
.o7:	mov	x17,0
.m7:	ldur	w16,[x8,-28]
	smaddl	x17,w16,w6,x17
	b	.m6
.o6:	mov	x17,0
.m6:	ldur	w16,[x8,-24]
	smaddl	x17,w16,w5,x17
	b	.m5
.o5:	mov	x17,0
.m5:	ldur	w16,[x8,-20]
	smaddl	x17,w16,w4,x17
	b	.m4
.o4:	mov	x17,0
.m4:	ldur	w16,[x8,-16]
	smaddl	x17,w16,w3,x17
	b	.m3
.o3:	mov	x17,0
.m3:	ldur	w16,[x8,-12]
	smaddl	x17,w16,w2,x17
	b	.m2
.o2:	mov	x17,0
.m2:	ldur	w16,[x8,-8]
	smaddl	x17,w16,w1,x17
	b	.m1
.o1:	mov	x17,0
.m1:	ldur	w16,[x8,-4]
	smaddl	x17,w16,w0,x17
	asr	x17,x17,x14
	ldr	w16,[x8]
	add	w16,w16,w17
	str	w16,[x8],4
	add	x13,x13,1
	adr	x16,.tab			; (x16 : point d'entree)
	sub	x17,x27,1
	ldrsw	x17,[x16,x17,lsl 2]
	add	x16,x16,x17
	b	.l8
.tab:	dw	.o1-.tab, .o2-.tab, .o3-.tab, .o4-.tab
	dw	.o5-.tab, .o6-.tab, .o7-.tab, .o8-.tab
.gen:	cmp	x13,x26				; ordre > 8 : boucle generale
	b.hs	.ret
	add	x8,x24,x13,lsl 2
	mov	x17,0
	mov	x0,0
.gj:	ldr	w1,[x15,x0,lsl 2]
	sub	x8,x8,4
	ldr	w2,[x8]
	smaddl	x17,w1,w2,x17
	add	x0,x0,1
	cmp	x0,x27
	b.lo	.gj
	asr	x17,x17,x14
	ldr	w2,[x24,x13,lsl 2]
	add	w2,w2,w17
	str	w2,[x24,x13,lsl 2]
	add	x13,x13,1
	b	.gen
.ret:	ret

; x0 = decodeur, x1 = trame voulue -> 0 / < 0 (pose AKD_OFF, AKD_POS, AKD_SKIP)
flac_seek:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x21,x1				; cible
	ldr	x22,[x19,AKD_DATA0]		; meilleure trame : position
	mov	x23,0				;   et premier echantillon
	; SEEKTABLE : le dernier point <= cible
	ldr	x0,[x20,FLC_SEEKTAB]
	cbz	x0,.bisect
	ldr	x1,[x20,FLC_NSEEK]
.pt:	cbz	x1,.bisect
	ldr	x2,[x0]				; echantillon
	rev	x2,x2
	cmn	x2,1				; point reserve
	b.eq	.ptn
	cmp	x2,x21
	b.hi	.ptn
	cmp	x2,x23
	b.lo	.ptn
	ldr	x3,[x0,8]
	rev	x3,x3
	ldr	x4,[x19,AKD_DATA0]
	add	x22,x4,x3
	mov	x23,x2
.ptn:	add	x0,x0,18
	sub	x1,x1,1
	b	.pt
.bisect:
	; bissection sur les positions : la trame qui suit le milieu
	mov	x24,x22				; bas
	ldr	x25,[x19,AKD_DATAEND]		; haut
	mov	x26,40				; (tours au plus)
.bl:	sub	x0,x25,x24
	cmp	x0,16384
	b.lo	.found
	subs	x26,x26,1
	b.eq	.found
	add	x0,x24,x25
	lsr	x0,x0,1
	mov	x1,x0
	mov	x0,x19
	bl	flac_find			; -> x0 = position, x1 = echantillon
	tbnz	x0,63,.hi
	cmp	x1,x21
	b.hi	.hi
	cmp	x1,x23
	b.lo	.hi
	mov	x24,x0
	mov	x22,x0
	mov	x23,x1
	b	.bl
.hi:	add	x0,x24,x25
	lsr	x25,x0,1
	b	.bl
.found:	str	x22,[x19,AKD_OFF]
	sub	x0,x21,x23
	str	x0,[x19,AKD_SKIP]
	str	x21,[x19,AKD_POS]
	mov	x0,0
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = position : premiere trame valide a partir de la
; -> x0 = sa position (< 0 : aucune dans les 64 Ko), x1 = son echantillon
flac_find:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x0,x19
	bl	akd_seek
	mov	x0,x19
	mov	x1,65536
	bl	akd_need
	mov	x21,x0
	mov	x22,x1
	mov	x23,0
.s:	add	x0,x23,16
	cmp	x0,x22
	b.hs	.none
	ldrb	w0,[x21,x23]
	cmp	w0,0xFF
	b.ne	.n
	mov	x0,x20
	add	x1,x21,x23
	sub	x2,x22,x23
	bl	flac_hdr
	tbnz	x0,63,.n
	ldr	x0,[x20,FLC_H_RATE]		; meme frequence, memes bits
	ldr	x1,[x20,FLC_RATE]
	cmp	x0,x1
	b.ne	.n
	ldr	x0,[x20,FLC_H_BPS]
	ldr	x1,[x20,FLC_BPS]
	cmp	x0,x1
	b.ne	.n
	ldr	x0,[x19,AKD_OFF]
	add	x0,x0,x23
	ldr	x1,[x20,FLC_H_NUM]
	b	.out
.n:	add	x23,x23,1
	b	.s
.none:	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

		align	8
flac_fixed	dw	1, 0, 0, 0		; predicteurs fixes (ordres 1-4)
		dw	2, -1, 0, 0
		dw	3, -3, 1, 0
		dw	4, -6, 4, -1
flac_rates	dw	0, 88200, 176400, 192000, 8000, 16000, 22050, 24000
		dw	32000, 44100, 48000, 96000
flac_sizes	db	0, 8, 12, 0, 16, 20, 24, 32
		align	4
