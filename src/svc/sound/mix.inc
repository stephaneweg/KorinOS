; ============================================================================
;  sound/mix.inc - le thread de melange (docs/SON.md 2.2-2.3)
;
;  Toutes les 5 ms (scrutation : pas de reveil entre process), sous le verrou
;  des tables :
;  * chaque sortie : la virtuelle consomme son anneau au rythme de l'horloge ;
;    puis, tant que l'anneau a moins de LEAD_BLOCKS blocs d'avance, un bloc
;    de SND_BLOCK trames est melange (flux de cette sortie, gain du flux x
;    client, accumulateur 32 bits, saturation), donne a la capture « boucle »,
;    reechantillonne a la frequence de la sortie et ecrit avec le gain de la
;    sortie ;
;  * chaque entree (micro) : ce que le pilote a ecrit, ramene a 44,1 kHz, va
;    aux captures qui l'ecoutent ; SR_WANTED dit au pilote s'il doit ecouter.
; ============================================================================

mix_thread:
.loop:	mov	x19,0				; blocs faits dans ce tour
	bl	lock_take
	adr	x20,outs
	mov	x21,OUT_MAX
.o:	ldr	x0,[x20,OU_ID]
	cbz	x0,.on
	mov	x0,x20
	bl	out_service
	add	x19,x19,x0
.on:	add	x20,x20,OUT_SIZE
	subs	x21,x21,1
	b.ne	.o
	adr	x20,inputs
	mov	x21,IN_MAX
.i:	ldr	x0,[x20,IN_ID]
	cbz	x0,.in
	mov	x0,x20
	bl	in_service
	add	x19,x19,x0
.in:	add	x20,x20,IN_SIZE
	subs	x21,x21,1
	b.ne	.i
	bl	lock_drop
	cbnz	x19,.loop
	mov	x0,5
	sys	SYS_SLEEP
	b	.loop

; x0 = sortie -> x0 = blocs melanges
out_service:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,OU_RING]
	ldr	x0,[x19,OU_PID]
	cbnz	x0,.real
	mov	x0,x19				; virtuelle : consommation
	bl	virt_consume
.real:	mov	x21,0
.b:	cmp	x21,LEAD_BLOCKS
	b.hs	.done
	ldr	x0,[x20,SR_HEAD]
	add	x1,x20,SR_TAIL
	ldar	x1,[x1]
	sub	x0,x0,x1			; avance
	ldr	x1,[x19,OU_BLK]
	add	x0,x0,x1
	ldr	x1,[x19,OU_LEAD]
	cmp	x0,x1
	b.hi	.done
	mov	x0,x19
	bl	mix_block
	mov	x0,x19
	bl	loopback
	mov	x0,x19
	bl	out_write
	add	x21,x21,1
	b	.b
.done:	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = sortie virtuelle : son anneau se vide au rythme de l'horloge
virt_consume:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,OU_RING]
	sys	SYS_TIME_MS
	ldr	x1,[x19,OU_VT0]
	sub	x0,x0,x1
	ldr	x2,[x19,OU_RATE]
	mul	x0,x0,x2
	mov	x3,1000
	udiv	x0,x0,x3			; trames dues depuis le depart
	ldr	x4,[x19,OU_VCONS]
	subs	x5,x0,x4			; a consommer
	b.ls	.out
	str	x0,[x19,OU_VCONS]
	ldr	x6,[x20,SR_HEAD]
	ldr	x7,[x20,SR_TAIL]
	sub	x8,x6,x7
	lsr	x8,x8,2
	cmp	x5,x8
	csel	x9,x5,x8,lo
	add	x7,x7,x9,lsl 2
	add	x0,x20,SR_TAIL
	stlr	x7,[x0]
	ldr	x0,[x20,SR_PLAYED]
	add	x0,x0,x9
	str	x0,[x20,SR_PLAYED]
	subs	x5,x5,x9
	b.ls	.out
	ldr	x0,[x20,SR_LOST]
	add	x0,x0,x5
	str	x0,[x20,SR_LOST]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = sortie : un bloc (SND_BLOCK trames, 44,1 kHz) des flux qui y jouent
; -> mixbuf (s16 stereo sature) ; cretes des clients
mix_block:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	ldr	x28,[x19,OU_ID]
	adr	x20,acc
	mov	x0,x20
	mov	x1,SND_BLOCK*8
	bl	zero
	adr	x21,streams
	mov	x22,STR_MAX
.s:	ldr	x0,[x21,ST_KIND]
	cmp	x0,1
	b.ne	.sn
	ldr	x0,[x21,ST_CUR]
	cmp	x0,x28
	b.ne	.sn
	ldr	x23,[x21,ST_RING]
	add	x0,x23,SR_HEAD
	ldar	x0,[x0]
	ldr	x24,[x23,SR_TAIL]
	sub	x0,x0,x24
	lsr	x0,x0,2				; trames disponibles
	mov	x1,SND_BLOCK
	cmp	x0,x1
	csel	x25,x0,x1,lo
	ldr	x1,[x21,ST_CLI]
	adr	x26,clients
	mov	x2,CL_SIZE
	madd	x26,x1,x2,x26
	mov	x12,0				; crete
	cbz	x25,.pk
	ldr	x0,[x26,CL_VOL]			; gain : flux x client
	bl	ak_volume_curve
	ldr	x1,[x26,CL_MUTE]
	cmp	x1,0
	csel	x0,x0,xzr,eq
	ldr	x1,[x23,SR_VOLUME]
	mov	x2,256
	cmp	x1,x2
	csel	x1,x1,x2,ls
	mul	x0,x0,x1
	lsr	x27,x0,8			; 0..65536
	add	x9,x23,SR_DATA
	movl	x10,SR_STREAM_BYTES-1
	mov	x11,0
	mov	w12,0
.f:	and	x13,x24,x10
	ldr	w14,[x9,x13]
	add	x24,x24,4
	sxth	w15,w14
	asr	w16,w14,16
	mul	w15,w15,w27
	asr	w15,w15,16
	mul	w16,w16,w27
	asr	w16,w16,16
	add	x13,x20,x11,lsl 3
	ldp	w6,w7,[x13]
	add	w6,w6,w15
	add	w7,w7,w16
	stp	w6,w7,[x13]
	cmp	w15,0
	cneg	w6,w15,lt
	cmp	w6,w12
	csel	w12,w6,w12,hi
	cmp	w16,0
	cneg	w6,w16,lt
	cmp	w6,w12
	csel	w12,w6,w12,hi
	add	x11,x11,1
	cmp	x11,x25
	b.lo	.f
	add	x0,x23,SR_TAIL
	stlr	x24,[x0]
	ldr	x0,[x23,SR_PLAYED]
	add	x0,x0,x25
	str	x0,[x23,SR_PLAYED]
	cmp	x25,SND_BLOCK			; flux en retard : manque
	b.hs	.pk
	ldr	x0,[x23,SR_LOST]
	mov	x1,SND_BLOCK
	sub	x1,x1,x25
	add	x0,x0,x1
	str	x0,[x23,SR_LOST]
.pk:	str	x12,[x26,CL_PEAK]		; crete du client (vumetre)
	ldr	x1,[x21,ST_CLI]
	adr	x2,state
	ldr	x2,[x2]
	add	x2,x2,SS_CLIS
	add	x2,x2,x1,lsl 6
	str	w12,[x2,SC_PEAK]
.sn:	add	x21,x21,STR_SIZE
	subs	x22,x22,1
	b.ne	.s
	adr	x0,mixbuf			; saturation 16 bits
	mov	x1,0
	mov	w9,0xFFFF8000
	mov	w10,32767
.sat:	ldr	w6,[x20,x1,lsl 2]
	cmp	w6,w9
	csel	w6,w9,w6,lt
	cmp	w6,w10
	csel	w6,w10,w6,gt
	strh	w6,[x0,x1,lsl 1]
	add	x1,x1,1
	cmp	x1,SND_BLOCK*2
	b.lo	.sat
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = sortie : mixbuf aux captures « boucle » de cette sortie
loopback:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x21,[x0,OU_ID]
	adr	x0,main_out
	ldr	x22,[x0]
	adr	x19,streams
	mov	x20,STR_MAX
.s:	ldr	x0,[x19,ST_KIND]
	cmp	x0,2
	b.ne	.n
	ldr	x0,[x19,ST_OUT]
	cmp	x0,REC_SRC_LOOP
	b.ne	.lo
	cmp	x21,x22				; boucle : la principale
	b.eq	.push
	b	.n
.lo:	sub	x0,x0,REC_SRC_LOOP_OUT		; boucle de la sortie n
	cmp	x0,x21
	b.ne	.n
.push:	ldr	x0,[x19,ST_RING]
	adr	x1,mixbuf
	mov	x2,SND_BLOCK
	bl	cap_push
.n:	add	x19,x19,STR_SIZE
	subs	x20,x20,1
	b.ne	.s
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = anneau de capture, x1 = trames s16 stereo, x2 = trames : ajoutees
; (le reste est perdu et compte)
cap_push:
	ldr	x3,[x0,SR_HEAD]
	add	x4,x0,SR_TAIL
	ldar	x4,[x4]
	sub	x5,x3,x4
	movl	x6,SR_STREAM_BYTES
	sub	x5,x6,x5
	lsr	x5,x5,2				; place (trames)
	cmp	x2,x5
	csel	x7,x2,x5,lo
	subs	x8,x2,x7
	b.eq	.copy
	ldr	x9,[x0,SR_LOST]
	add	x9,x9,x8
	str	x9,[x0,SR_LOST]
.copy:	add	x9,x0,SR_DATA
	movl	x6,SR_STREAM_BYTES-1
.c:	cbz	x7,.done
	ldr	w10,[x1],4
	and	x11,x3,x6
	str	w10,[x9,x11]
	add	x3,x3,4
	sub	x7,x7,1
	b	.c
.done:	add	x0,x0,SR_HEAD
	stlr	x3,[x0]
	ret

; x0 = sortie : mixbuf -> anneau de la sortie (reechantillonne a sa
; frequence, gain de la sortie) ; crete de la sortie
out_write:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,OU_VOL]			; gain de la sortie
	bl	ak_volume_curve
	ldr	x1,[x19,OU_MUTE]
	cmp	x1,0
	csel	x23,x0,xzr,eq
	adr	x20,mixbuf
	mov	x21,SND_BLOCK
	ldr	x0,[x19,OU_RATE]
	movl	x1,SND_RATE_STD
	cmp	x0,x1
	b.eq	.copy
	mov	x0,x20				; autre frequence
	mov	x1,x21
	adr	x2,rsbuf
	add	x3,x19,OU_RS
	bl	ak_resample
	mov	x21,x0
	adr	x20,rsbuf
.copy:	ldr	x22,[x19,OU_RING]
	ldr	x3,[x22,SR_HEAD]
	add	x9,x22,SR_DATA
	movl	x6,SR_OUT_BYTES-1
	mov	w12,0				; crete
.c:	cbz	x21,.done
	ldr	w10,[x20],4
	sxth	w14,w10
	asr	w15,w10,16
	mul	w14,w14,w23
	asr	w14,w14,16
	mul	w15,w15,w23
	asr	w15,w15,16
	and	w10,w14,0xFFFF
	orr	w10,w10,w15,lsl 16
	and	x11,x3,x6
	str	w10,[x9,x11]
	add	x3,x3,4
	cmp	w14,0
	cneg	w14,w14,lt
	cmp	w14,w12
	csel	w12,w14,w12,hi
	sub	x21,x21,1
	b	.c
.done:	add	x0,x22,SR_HEAD
	stlr	x3,[x0]
	str	x12,[x19,OU_PEAK]
	adr	x0,outs				; crete dans le tampon d'etat
	sub	x0,x19,x0
	mov	x1,OUT_SIZE
	udiv	x0,x0,x1
	adr	x1,state
	ldr	x1,[x1]
	add	x1,x1,SS_OUTS
	add	x1,x1,x0,lsl 6
	str	w12,[x1,SO_PEAK]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = source d'une capture, x1 = entree -> x0 = 1 si la capture l'ecoute
cap_hears:
	prologue
	cbz	x0,.mic
	and	x2,x0,0xFF00
	cmp	x2,REC_SRC_INPUT
	b.ne	.non
	and	x0,x0,0xFF
	ldr	x2,[x1,IN_ID]
	cmp	x0,x2
	cset	x0,eq
	b	.out
.mic:	str	x1,[sp,-16]!
	bl	in_first
	ldr	x1,[sp],16
	cmp	x0,x1
	cset	x0,eq
	b	.out
.non:	mov	x0,0
.out:	epilogue

; x0 = entree -> x0 = 1 si des trames ont ete prises
in_service:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,IN_RING]
	mov	x21,0				; captures qui ecoutent
	adr	x22,streams
	mov	x23,STR_MAX
.w:	ldr	x0,[x22,ST_KIND]
	cmp	x0,2
	b.ne	.wn
	ldr	x0,[x22,ST_OUT]
	mov	x1,x19
	bl	cap_hears
	add	x21,x21,x0
.wn:	add	x22,x22,STR_SIZE
	subs	x23,x23,1
	b.ne	.w
	cmp	x21,0
	cset	x0,ne
	str	x0,[x20,SR_WANTED]
	add	x0,x20,SR_HEAD
	ldar	x0,[x0]
	ldr	x3,[x20,SR_TAIL]
	sub	x0,x0,x3
	lsr	x0,x0,2
	mov	x1,SND_BLOCK
	cmp	x0,x1
	csel	x21,x0,x1,lo			; trames prises
	cbz	x21,.out0
	adr	x4,inbuf			; -> inbuf
	add	x5,x20,SR_DATA
	movl	x6,SR_IN_BYTES-1
	mov	x7,x21
.c:	and	x8,x3,x6
	ldr	w9,[x5,x8]
	str	w9,[x4],4
	add	x3,x3,4
	subs	x7,x7,1
	b.ne	.c
	add	x0,x20,SR_TAIL
	stlr	x3,[x0]
	adr	x23,inbuf
	ldr	x0,[x19,IN_RATE]
	movl	x1,SND_RATE_STD
	cmp	x0,x1
	b.eq	.push
	mov	x0,x23				; -> 44,1 kHz
	mov	x1,x21
	adr	x2,rsbuf
	add	x3,x19,IN_RS
	bl	ak_resample
	mov	x21,x0
	adr	x23,rsbuf
.push:	adr	x22,streams
	mov	x24,STR_MAX
.p:	ldr	x0,[x22,ST_KIND]
	cmp	x0,2
	b.ne	.pn
	ldr	x0,[x22,ST_OUT]
	mov	x1,x19
	bl	cap_hears
	cbz	x0,.pn
	ldr	x0,[x22,ST_RING]
	mov	x1,x23
	mov	x2,x21
	bl	cap_push
.pn:	add	x22,x22,STR_SIZE
	subs	x24,x24,1
	b.ne	.p
	mov	x0,1
	b	.out
.out0:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

macro mix_data {
		align 16
acc		rb SND_BLOCK * 8		; accumulateur s32 stereo
mixbuf		rb SND_BLOCK * 4		; bloc melange (s16 stereo)
inbuf		rb SND_BLOCK * 4		; trames d'une entree
rsbuf		rb 6144 * 4			; reechantillonne (sortie <= 96 kHz,
						; entree >= 8 kHz)
}
