; ============================================================================
;  kits/audiokit/ak_play.inc - lecteur en arriere-plan : ak_play(chemin)
;
;  Un thread du process de l'application decode le fichier (ak_dec_*) et
;  ecrit dans un flux de sound (ak_open au format du fichier : AudioKit
;  convertit et reechantillonne). Commandes par des mots partages, lus par
;  le thread a chaque bloc (~23 ms) : pause, arret, recherche.
; ============================================================================

PL_DEC		= 0		; decodeur
PL_STREAM	= 8		; flux de sound
PL_STATE	= 16		; AKP_*
PL_PAUSE	= 24		; 1 : en pause (demande)
PL_STOP		= 32		; 1 : le thread doit finir
PL_SEEK		= 40		; recherche demandee (ms), -1 : aucune
PL_TID		= 48
PL_STACK	= 56
PL_RATE		= 64
PL_CH		= 72
PL_BUF		= 128		; PL_FRAMES trames s16 (8 canaux au plus)
PL_FRAMES	= 1024
PL_SIZE		= PL_BUF + PL_FRAMES * 16
PL_STACK_SIZE	= 65536

; x0 = chemin, x1 = sortie (0 : celle du process) -> lecteur (poignee),
; < 0 : erreur ; la lecture commence tout de suite
ak_play:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,AKI_SIZE
	mov	x22,x1
	bl	ak_dec_open
	tbnz	x0,63,.out
	mov	x20,x0
	mov	x1,sp
	bl	ak_dec_info
	ldr	x0,[sp,AKI_RATE]
	ldr	x1,[sp,AKI_CHANNELS]
	mov	x2,16
	mov	x3,x22
	bl	ak_open
	tbnz	x0,63,.fdec
	mov	x21,x0
	movl	x0,PL_SIZE
	bl	ak_alloc
	cbz	x0,.nomem
	mov	x19,x0
	str	x20,[x19,PL_DEC]
	str	x21,[x19,PL_STREAM]
	ldr	x0,[sp,AKI_RATE]
	str	x0,[x19,PL_RATE]
	ldr	x0,[sp,AKI_CHANNELS]
	str	x0,[x19,PL_CH]
	mov	x0,-1
	str	x0,[x19,PL_SEEK]
	mov	x0,AKP_PLAYING
	str	x0,[x19,PL_STATE]
	movl	x0,PL_STACK_SIZE
	bl	ak_alloc
	cbz	x0,.nomem2
	str	x0,[x19,PL_STACK]
	movl	x1,PL_STACK_SIZE
	add	x1,x0,x1
	adr	x0,ak_play_thread
	mov	x2,x19
	sys	SYS_THREAD_CREATE
	tbnz	x0,63,.nothr
	str	x0,[x19,PL_TID]
	mov	x0,x19
	b	.out
.nothr:	mov	x22,x0
	ldr	x0,[x19,PL_STACK]
	bl	ak_free
	b	.fp
.nomem2: mov	x22,AKE_NOMEM
.fp:	mov	x0,x19
	bl	ak_free
	b	.fst
.nomem:	mov	x22,AKE_NOMEM
.fst:	mov	x0,x21
	bl	ak_close
	mov	x21,x22
	b	.fd
.fdec:	mov	x21,x0
.fd:	mov	x0,x20
	bl	ak_dec_close
	mov	x0,x21
.out:	add	sp,sp,AKI_SIZE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; thread du lecteur : x0 = lecteur
ak_play_thread:
	mov	x19,x0
.loop:	ldr	x0,[x19,PL_STOP]
	cbnz	x0,.exit
	ldr	x1,[x19,PL_SEEK]
	tbnz	x1,63,.noseek
	mov	x0,-1
	str	x0,[x19,PL_SEEK]
	ldr	x0,[x19,PL_DEC]
	bl	ak_dec_seek
	mov	x0,AKP_PLAYING
	str	x0,[x19,PL_STATE]
.noseek:
	ldr	x0,[x19,PL_PAUSE]
	cbz	x0,.run
	mov	x0,AKP_PAUSED
	str	x0,[x19,PL_STATE]
	b	.idle
.run:	ldr	x0,[x19,PL_STATE]
	cmp	x0,AKP_PLAYING
	b.eq	.play
	cmp	x0,AKP_PAUSED
	b.ne	.idle				; fini ou en erreur : attendre
	mov	x0,AKP_PLAYING
	str	x0,[x19,PL_STATE]
.play:	ldr	x0,[x19,PL_DEC]
	add	x1,x19,PL_BUF
	mov	x2,PL_FRAMES
	bl	ak_dec_read
	cmp	x0,0
	b.le	.end
	ldr	x1,[x19,PL_CH]
	lsl	x1,x1,1
	mul	x21,x0,x1			; octets
	add	x20,x19,PL_BUF
.w:	cbz	x21,.loop
	ldr	x0,[x19,PL_STREAM]
	mov	x1,x20
	mov	x2,x21
	bl	ak_write
	tbnz	x0,63,.err
	add	x20,x20,x0
	sub	x21,x21,x0
	cbz	x21,.loop
	ldr	x0,[x19,PL_STOP]
	cbnz	x0,.exit
	ldr	x0,[x19,PL_SEEK]
	tbz	x0,63,.loop			; (le reste du bloc est perdu)
	mov	x0,5
	sys	SYS_SLEEP
	b	.w
.end:	mov	x22,AKP_ENDED			; fin : attendre que tout soit joue
	tbz	x0,63,.drain
	mov	x22,AKP_ERROR
.drain:	ldr	x0,[x19,PL_STOP]
	cbnz	x0,.exit
	ldr	x0,[x19,PL_SEEK]
	tbz	x0,63,.loop
	ldr	x0,[x19,PL_STREAM]
	bl	ak_pending
	cmp	x0,0
	b.le	.ended
	mov	x0,10
	sys	SYS_SLEEP
	b	.drain
.ended:	str	x22,[x19,PL_STATE]
	b	.loop
.err:	mov	x0,AKP_ERROR
	str	x0,[x19,PL_STATE]
.idle:	mov	x0,20
	sys	SYS_SLEEP
	b	.loop
.exit:	mov	x0,0
	sys	SYS_THREAD_EXIT
	b	.exit

; x0 = lecteur, x1 = 1 pause, 0 reprise -> 0
ak_play_pause:
	cmp	x1,0
	cset	x1,ne
	str	x1,[x0,PL_PAUSE]
	mov	x0,0
	ret

; x0 = lecteur, x1 = position (ms) -> 0
ak_play_seek:
	cmp	x1,0
	csel	x1,xzr,x1,lt
	str	x1,[x0,PL_SEEK]
	mov	x0,0
	ret

; x0 = lecteur -> AKP_*
ak_play_state:
	ldr	x0,[x0,PL_STATE]
	ret

; x0 = lecteur, x1 = volume 0..256 -> 0
ak_play_volume:
	ldr	x0,[x0,PL_STREAM]
	b	ak_volume

; x0 = lecteur -> son decodeur (ak_dec_info, ak_dec_tag : lecture seule)
ak_play_decoder:
	ldr	x0,[x0,PL_DEC]
	ret

; x0 = lecteur -> position entendue (ms) : decodee moins l'avance du flux
ak_play_pos:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,PL_STREAM]
	bl	ak_pending			; octets s16 stereo 44,1 kHz
	cmp	x0,0
	csel	x0,xzr,x0,lt
	lsr	x0,x0,2
	mov	x1,1000
	mul	x0,x0,x1
	movl	x1,SND_RATE_STD
	udiv	x20,x0,x1			; ms en attente
	ldr	x0,[x19,PL_DEC]
	ldr	x0,[x0,AKD_POS]
	mov	x1,1000
	mul	x0,x0,x1
	ldr	x1,[x19,PL_RATE]
	udiv	x0,x0,x1
	subs	x0,x0,x20
	csel	x0,xzr,x0,lt
	ldp	x19,x20,[sp],16
	epilogue

; x0 = lecteur : arrete le thread, ferme le flux et le fichier -> 0
ak_play_stop:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,1
	str	x0,[x19,PL_STOP]
	ldr	x0,[x19,PL_TID]
	sys	SYS_THREAD_JOIN
	ldr	x0,[x19,PL_STREAM]
	bl	ak_close
	ldr	x0,[x19,PL_DEC]
	bl	ak_dec_close
	ldr	x0,[x19,PL_STACK]
	bl	ak_free
	mov	x0,x19
	bl	ak_free
	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue
