; ============================================================================
;  kits/audiokit/ak_wav.inc - WAV (RIFF/WAVE) : PCM 8 (non signe), 16, 24,
;  32 bits, flottant 32 bits, WAVE_FORMAT_EXTENSIBLE ; etiquettes LIST/INFO
; ============================================================================

WAV_UNIT	= 1024		; trames par unite rendue

WV_BA		= 0		; octets par trame
WV_BYTES	= 8		; octets par echantillon
WV_FLOAT	= 16
WV_SIZE		= 32

; x0 = decodeur (lecture sur "RIFF") -> 0 / < 0
wav_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x22,0				; format (0 : pas encore de 'fmt ')
	mov	x0,x19
	mov	x1,12
	bl	akd_skip
.chunk:	mov	x0,x19
	mov	x1,8
	bl	akd_need
	cmp	x1,8
	b.lo	.bad
	ldr	w20,[x0]			; identifiant
	ldr	w21,[x0,4]			; taille
	movl	w2,0x20746D66			; 'fmt '
	cmp	w20,w2
	b.eq	.fmt
	movl	w2,0x5453494C			; 'LIST'
	cmp	w20,w2
	b.eq	.list
	movl	w2,0x61746164			; 'data'
	cmp	w20,w2
	b.eq	.data
.next:	add	x1,x21,8
	and	x2,x21,1
	add	x1,x1,x2
	mov	x0,x19
	bl	akd_skip
	b	.chunk
.fmt:	cmp	x21,16
	b.lo	.bad
	mov	x0,x19
	add	x1,x21,8
	bl	akd_need
	add	x2,x21,8
	cmp	x1,x2
	b.lo	.bad
	ldrh	w22,[x0,8]			; format
	ldrh	w23,[x0,10]			; canaux
	ldr	w24,[x0,12]			; frequence
	ldrh	w25,[x0,20]			; octets par trame
	ldrh	w26,[x0,22]			; bits
	movl	w2,0xFFFE			; WAVE_FORMAT_EXTENSIBLE : le sous-format
	cmp	w22,w2
	b.ne	.next
	cmp	x21,40
	b.lo	.bad
	ldrh	w22,[x0,32]
	b	.next
.list:	cmp	x21,4
	b.lo	.next
	mov	x0,x19
	add	x1,x21,8
	movl	x2,65536
	cmp	x1,x2
	csel	x1,x1,x2,lo
	bl	akd_need
	add	x2,x21,8
	cmp	x1,x2
	csel	x1,x1,x2,lo
	mov	x2,x0
	mov	x0,x19
	bl	wav_info
	b	.next
.data:	cbz	x22,.bad
	ldr	x0,[x19,AKD_OFF]
	add	x0,x0,8
	str	x0,[x19,AKD_DATA0]
	add	x1,x0,x21
	ldr	x2,[x19,AKD_LEN]
	cmp	x21,0				; taille 0 ou trop grande (flux) : le reste
	csel	x1,x2,x1,eq
	cmp	x1,x2
	csel	x1,x2,x1,hi
	str	x1,[x19,AKD_DATAEND]
	str	x0,[x19,AKD_OFF]
	; format accepte ?
	sub	w0,w23,1
	cmp	w0,7
	b.hi	.unsup
	cbz	w24,.unsup
	mov	x20,0				; flottant ?
	cmp	w22,3
	b.ne	.int
	cmp	w26,32
	b.ne	.unsup
	mov	x20,1
	b	.fok
.int:	cmp	w22,1
	b.ne	.unsup
	cmp	w26,8
	b.eq	.fok
	cmp	w26,16
	b.eq	.fok
	cmp	w26,24
	b.eq	.fok
	cmp	w26,32
	b.ne	.unsup
.fok:	lsr	w0,w26,3
	mul	w1,w0,w23
	cmp	w25,w1
	b.ne	.unsup
	mov	x0,x19
	mov	x1,WV_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x19,AKD_CODEC]
	str	x25,[x0,WV_BA]
	lsr	w1,w26,3
	str	x1,[x0,WV_BYTES]
	str	x20,[x0,WV_FLOAT]
	mov	x0,AKF_WAV
	str	x0,[x19,AKD_FMT]
	str	x24,[x19,AKD_RATE]
	str	x23,[x19,AKD_CH]
	str	x26,[x19,AKD_BITS]
	mul	x0,x24,x25
	lsl	x0,x0,3
	str	x0,[x19,AKD_BITRATE]
	ldr	x0,[x19,AKD_DATAEND]
	ldr	x1,[x19,AKD_DATA0]
	sub	x0,x0,x1
	udiv	x0,x0,x25
	str	x0,[x19,AKD_TOTAL]
	mov	x0,AKD_S32
	cbz	x20,.pf
	mov	x0,AKD_F32
.pf:	str	x0,[x19,AKD_PCMFMT]
	adr	x0,wav_next
	str	x0,[x19,AKD_NEXT]
	adr	x0,wav_seek
	str	x0,[x19,AKD_SEEKFN]
	mov	x0,x19
	mov	x1,WAV_UNIT
	mov	x2,x23
	bl	akd_planes
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.unsup:	mov	x0,AKE_FORMAT
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = octets, x2 = morceau 'LIST' (en-tete compris) :
; INAM, IART, IPRD
wav_info:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	add	x21,x2,x1			; fin
	ldr	w0,[x2,8]
	movl	w1,0x4F464E49			; 'INFO'
	cmp	w0,w1
	b.ne	.out
	add	x20,x2,12
.s:	add	x0,x20,8
	cmp	x0,x21
	b.hi	.out
	ldr	w0,[x20]
	ldr	w22,[x20,4]
	add	x2,x20,8
	add	x3,x2,x22
	cmp	x3,x21
	b.hi	.out
	movl	w1,0x4D414E49			; INAM
	mov	x4,AKT_TITLE
	cmp	w0,w1
	b.eq	.put
	movl	w1,0x54524149			; IART
	mov	x4,AKT_ARTIST
	cmp	w0,w1
	b.eq	.put
	movl	w1,0x44525049			; IPRD
	mov	x4,AKT_ALBUM
	cmp	w0,w1
	b.ne	.nx
.put:	mov	x1,x4
	mov	x0,x19
	mov	x3,x22
	mov	x4,3
	bl	akd_tag_put
.nx:	add	x20,x20,8
	add	x20,x20,x22
	and	x0,x22,1
	add	x20,x20,x0
	b	.s
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur -> trames dans les tampons plans (0 : fin)
wav_next:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	ldr	x21,[x20,WV_BA]
	ldr	x0,[x19,AKD_DATAEND]
	ldr	x1,[x19,AKD_OFF]
	subs	x0,x0,x1
	b.le	.end
	udiv	x22,x0,x21			; trames restantes
	mov	x0,WAV_UNIT
	cmp	x22,x0
	csel	x22,x22,x0,lo
	cbz	x22,.end
	mov	x0,x19
	mul	x1,x22,x21
	bl	akd_need
	udiv	x1,x1,x21
	cmp	x22,x1
	csel	x22,x22,x1,lo
	cbz	x22,.end
	mov	x23,x0				; donnees
	ldr	x24,[x20,WV_BYTES]
	ldr	x8,[x19,AKD_CH]
	mov	x7,0				; canal
.ch:	cmp	x7,x8
	b.hs	.done
	add	x0,x19,AKD_PCM
	ldr	x2,[x0,x7,lsl 3]		; destination
	mul	x1,x7,x24
	add	x1,x23,x1			; source
	mov	x3,x22
	cmp	x24,2
	b.eq	.b16
	cmp	x24,3
	b.eq	.b24
	cmp	x24,4
	b.eq	.b32
.b8:	ldrb	w4,[x1]
	sub	w4,w4,128
	lsl	w4,w4,24
	str	w4,[x2],4
	add	x1,x1,x21
	subs	x3,x3,1
	b.ne	.b8
	b	.nch
.b16:	ldrh	w4,[x1]
	lsl	w4,w4,16
	str	w4,[x2],4
	add	x1,x1,x21
	subs	x3,x3,1
	b.ne	.b16
	b	.nch
.b24:	ldrb	w4,[x1]
	ldrb	w5,[x1,1]
	ldrb	w6,[x1,2]
	lsl	w4,w4,8
	orr	w4,w4,w5,lsl 16
	orr	w4,w4,w6,lsl 24
	str	w4,[x2],4
	add	x1,x1,x21
	subs	x3,x3,1
	b.ne	.b24
	b	.nch
.b32:	ldr	w4,[x1]				; (entier ou flottant : tel quel)
	str	w4,[x2],4
	add	x1,x1,x21
	subs	x3,x3,1
	b.ne	.b32
.nch:	add	x7,x7,1
	b	.ch
.done:	mov	x0,x19
	mul	x1,x22,x21
	bl	akd_skip
	mov	x0,x22
	b	.out
.end:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = trame -> 0
wav_seek:
	ldr	x2,[x0,AKD_CODEC]
	ldr	x2,[x2,WV_BA]
	ldr	x3,[x0,AKD_DATA0]
	madd	x3,x1,x2,x3
	str	x3,[x0,AKD_OFF]
	str	x1,[x0,AKD_POS]
	mov	x0,0
	ret
