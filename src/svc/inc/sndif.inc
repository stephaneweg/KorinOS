; ============================================================================
;  svc/inc/sndif.inc - cote pilote audio (pwmaudio, hdmiaudio, i2smic,
;  usbaudio ; docs/SON.md 2) : inscription aupres du service " sound "
;  comme sortie et / ou entree, anneau partage (un producteur, un
;  consommateur, sans verrou ni appel systeme), canal DMA donne par sound.
;  Comme svc/inc/netif.inc pour netd.
;
;	segment executable : include '../inc/sndif.inc' (apres inc/sound.inc)
;	segment writeable  : sndif_data
;
;  sndif_output(x0 type SND_OUT_*, x1 frequence, x2 canal DMA : 0 aucun,
;    1 n'importe lequel, 0x100 + n : celui-la s'il est libre, x3 nom)
;    -> x0 = numero de sortie (> 0), < 0 si sound ne repond pas ;
;    sif_out_ring (anneau mappe), sif_out_dma (canal, -1 : aucun)
;  sndif_input(idem, type SND_IN_*) -> x0 = numero d'entree ; sif_in_ring,
;    sif_in_dma
;  sndif_read(x0 destination, x1 trames) -> x0 = trames lues de l'anneau de
;    sortie (s16 stereo a la frequence de la sortie) ; le reste de la
;    destination est mis a zero (silence, compte comme manque)
;  sndif_write(x0 source, x1 trames) -> x0 = trames mises dans l'anneau
;  sndif_irq_bind : prend l'interruption du canal de sif_out_dma. A appeler
;    avant de lancer le DMA : TI_INTEN seulement si sif_irq n'est pas nul (sinon
;    l'interruption, sur une ligne tenue par un autre, ne s'effacerait jamais)
;  sndif_irq_wait(x0 = registres du canal) : attend la fin d'un tampon (TI_INTEN
;    dans ses blocs de controle) ; 5 ms au plus tant qu'aucune interruption
;    n'est venue, comme un sommeil (no_irq, ligne prise par un autre canal)
;    d'entree (le reste est perdu)
;  sndif_wanted -> x0 = 1 si une capture ecoute l'entree (allumer le micro)
;  sndif_tick : toutes les 2 s, dit a sound qu'on est la ; s'il a ete
;    relance, se reinscrit (nouvel anneau, meme canal DMA si possible)
; ============================================================================

; x0 = type, x1 = frequence, x2 = DMA, x3 = nom -> x0 = sortie
sndif_output:
	prologue
	adr	x4,sif_out
	stp	x0,x1,[x4,SIF_TYPE]
	stp	x2,x3,[x4,SIF_DMAREQ]
	mov	x0,x4
	movl	x1,60000			; (sound peut demarrer apres le pilote)
	bl	sif_register
	epilogue

; x0 = type, x1 = frequence, x2 = DMA, x3 = nom -> x0 = entree
sndif_input:
	prologue
	adr	x4,sif_in
	stp	x0,x1,[x4,SIF_TYPE]
	stp	x2,x3,[x4,SIF_DMAREQ]
	mov	x0,x4
	movl	x1,60000
	bl	sif_register
	epilogue

; x0 = bloc (sif_out / sif_in), x1 = attente (ms) -> x0 = numero, < 0 si echec
sif_register:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	str	xzr,[x19,SIF_RING]
	adr	x0,sif_s_sound
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	adr	x1,sif_canal
	str	x0,[x1]
	adr	x20,sif_msg
	mov	x0,SND_OP_DRV_OUTPUT
	adr	x1,sif_in
	cmp	x19,x1
	b.ne	.op
	mov	x0,SND_OP_DRV_INPUT
.op:	str	x0,[x20]
	ldp	x0,x1,[x19,SIF_TYPE]
	stp	x0,x1,[x20,WM_A]
	ldr	x0,[x19,SIF_DMAREQ]
	ldr	x1,[x19,SIF_DMA]		; reinscription : le meme canal
	tbnz	x1,63,.req
	cbz	x0,.req
	add	x0,x1,0x100
.req:	str	x0,[x20,WM_C]
	ldr	x1,[x19,SIF_NAME]		; nom (31 caracteres au plus)
	add	x2,x20,WM_DATA
	mov	x3,0
.n:	ldrb	w4,[x1,x3]
	strb	w4,[x2,x3]
	cbz	w4,.nf
	add	x3,x3,1
	cmp	x3,31
	b.lo	.n
	strb	wzr,[x2,x3]
.nf:	mov	w0,32
	str	w0,[x20,WM_LEN]
	adr	x0,sif_canal
	ldr	x0,[x0]
	mov	x1,x20
	sys	SYS_SVC_CALL
	tbnz	x0,63,.out
	str	x0,[x19,SIF_ID]
	ldr	x0,[x20,WM_C]
	str	x0,[x19,SIF_DMA]
	ldr	x0,[x20,WM_B]			; l'anneau
	str	x0,[x19,SIF_BUF]
	sys	SYS_BUF_MAP
	tbnz	x0,63,.out
	str	x0,[x19,SIF_RING]
	ldr	x0,[x19,SIF_ID]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = trames -> x0 = trames lues (reste : silence)
sndif_read:
	adr	x2,sif_out
	ldr	x2,[x2,SIF_RING]
	cbz	x2,.none
	add	x3,x2,SR_HEAD
	ldar	x3,[x3]
	ldr	x4,[x2,SR_TAIL]
	sub	x5,x3,x4
	lsr	x5,x5,2
	cmp	x5,x1
	csel	x5,x5,x1,lo			; trames prises
	add	x6,x2,SR_DATA
	movl	x7,SR_OUT_BYTES-1
	mov	x8,0
.c:	cmp	x8,x5
	b.hs	.zero
	and	x9,x4,x7
	ldr	w10,[x6,x9]
	str	w10,[x0,x8,lsl 2]
	add	x4,x4,4
	add	x8,x8,1
	b	.c
.zero:	add	x9,x2,SR_TAIL
	stlr	x4,[x9]
	ldr	x9,[x2,SR_PLAYED]
	add	x9,x9,x5
	str	x9,[x2,SR_PLAYED]
	subs	x9,x1,x5
	b.eq	.ok
	ldr	x10,[x2,SR_LOST]
	add	x10,x10,x9
	str	x10,[x2,SR_LOST]
.z:	cmp	x8,x1
	b.hs	.ok
	str	wzr,[x0,x8,lsl 2]
	add	x8,x8,1
	b	.z
.ok:	mov	x0,x5
	ret
.none:	mov	x8,0
	mov	x5,0
	b	.z

; x0 = source, x1 = trames -> x0 = trames mises dans l'anneau d'entree
sndif_write:
	adr	x2,sif_in
	ldr	x2,[x2,SIF_RING]
	cbz	x2,.none
	ldr	x3,[x2,SR_HEAD]
	add	x4,x2,SR_TAIL
	ldar	x4,[x4]
	sub	x5,x3,x4
	movl	x6,SR_IN_BYTES
	sub	x5,x6,x5
	lsr	x5,x5,2
	cmp	x5,x1
	csel	x5,x5,x1,lo
	add	x6,x2,SR_DATA
	movl	x7,SR_IN_BYTES-1
	mov	x8,0
.c:	cmp	x8,x5
	b.hs	.done
	ldr	w10,[x0,x8,lsl 2]
	and	x9,x3,x7
	str	w10,[x6,x9]
	add	x3,x3,4
	add	x8,x8,1
	b	.c
.done:	add	x9,x2,SR_HEAD
	stlr	x3,[x9]
	subs	x9,x1,x5
	b.eq	.ok
	ldr	x10,[x2,SR_LOST]
	add	x10,x10,x9
	str	x10,[x2,SR_LOST]
.ok:	mov	x0,x5
	ret
.none:	mov	x0,0
	ret

; -> x0 = 1 si une capture ecoute l'entree
sndif_wanted:
	adr	x0,sif_in
	ldr	x0,[x0,SIF_RING]
	cbz	x0,.ret
	ldr	x0,[x0,SR_WANTED]
.ret:	ret

SIF_WAIT_POLL	= 5			; ms : sans interruption
SIF_WAIT_IRQ	= 40			; ms : au-dela d'un tampon, en cas d'oubli

sndif_irq_bind:
	prologue
	adr	x0,sif_out_dma
	ldr	x0,[x0]
	adr	x1,sif_lines
	ldrb	w0,[x1,x0]
	str	x0,[sp,-16]!
	mov	x1,-1
	mov	x2,0
	sys	SYS_IRQ_BIND
	ldr	x1,[sp],16
	tbnz	x0,63,.out
	adr	x0,sif_irq
	str	x1,[x0]
.out:	epilogue

; x0 = registres du canal
sndif_irq_wait:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,sif_irq
	ldr	x0,[x0]
	cbnz	x0,.irq
	mov	x0,SIF_WAIT_POLL
	sys	SYS_SLEEP
	b	.out
.irq:	adr	x1,sif_wait
	ldr	x1,[x1]
	cmp	x1,0
	mov	x2,SIF_WAIT_POLL
	csel	x1,x2,x1,eq
	sys	SYS_IRQ_WAIT
	adr	x2,sif_wait
	cmp	x0,1
	mov	x1,SIF_WAIT_IRQ
	mov	x3,SIF_WAIT_POLL
	csel	x1,x1,x3,eq
	str	x1,[x2]
	b.ne	.out
	ldr	w0,[x19]			; DMA_CS relu tel quel : INT et END
	str	w0,[x19]			; (a 1) s'effacent, ACTIVE reste
.out:	ldr	x19,[sp],16
	epilogue

; A appeler dans la boucle du pilote : toutes les 2 s, « je suis la » a
; sound ; s'il ne connait plus nos numeros (relance), reinscription
sndif_tick:
	prologue
	str	x19,[sp,-16]!
	sys	SYS_TIME_MS
	adr	x1,sif_next
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.out
	add	x0,x0,2000
	str	x0,[x1]
	adr	x19,sif_out
	bl	sif_alive
	adr	x19,sif_in
	bl	sif_alive
.out:	ldr	x19,[sp],16
	epilogue

; x19 = bloc : DRV_ALIVE, reinscription si sound ne le connait plus
sif_alive:
	prologue
	ldr	x0,[x19,SIF_RING]
	cbnz	x0,.ask
	ldr	x0,[x19,SIF_NAME]		; jamais inscrit (ou echec) : rien
	cbz	x0,.out
	b	.again
.ask:	adr	x2,sif_msg
	mov	x0,SND_OP_DRV_ALIVE
	str	x0,[x2]
	ldr	x0,[x19,SIF_ID]
	adr	x1,sif_in
	cmp	x19,x1
	b.ne	.a
	orr	x0,x0,SND_ID_INPUT
.a:	str	x0,[x2,WM_A]
	str	wzr,[x2,WM_LEN]
	adr	x0,sif_canal
	ldr	x0,[x0]
	mov	x1,x2
	sys	SYS_SVC_CALL
	tbz	x0,63,.out
.again:	ldr	x0,[x19,SIF_RING]		; l'ancien anneau n'est plus servi
	cbz	x0,.reg
	ldr	x0,[x19,SIF_BUF]
	sys	SYS_BUF_FREE
.reg:	mov	x0,x19
	mov	x1,0
	bl	sif_register
.out:	epilogue

sif_s_sound	db 'sound',0
sif_lines	db 112, 113, 114, 115, 116, 117, 118	; ligne du GIC, canaux 0-6
		db 119, 119, 120, 120			; 7-8 et 9-10 : une ligne pour deux
		db 121, 122, 123, 124			; 11-14 (BCM2711)
		align 4

SIF_TYPE	= 0
SIF_RATE	= 8
SIF_DMAREQ	= 16
SIF_NAME	= 24
SIF_ID		= 32
SIF_RING	= 40			; anneau mappe (0 : pas inscrit)
SIF_DMA		= 48			; canal donne (-1 : aucun)
SIF_BUF		= 56			; numero du tampon de l'anneau
SIF_SIZE	= 64

macro sndif_data {
		align 8
sif_canal	dd 0
sif_next	dd 0
sif_irq		dd 0				; ligne du canal DMA (0 : aucune)
sif_wait	dd 0				; attente en ms (sndif_irq_wait)
sif_out		dd 0, 0, 0, 0, 0, 0, -1, 0
sif_in		dd 0, 0, 0, 0, 0, 0, -1, 0
sif_out_ring	= sif_out + SIF_RING
sif_out_dma	= sif_out + SIF_DMA
sif_in_ring	= sif_in + SIF_RING
sif_in_dma	= sif_in + SIF_DMA
sif_msg		rb 512
}
