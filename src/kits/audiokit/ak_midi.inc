; ============================================================================
;  kits/audiokit/ak_midi.inc - lecture de fichiers MIDI et FM Song (ak_dec_*)
;
;  Deux formats de plus pour le decodeur generique : ils rendent du PCM flottant
;  stereo 44,1 kHz, donc ak_dec_read, ak_play, les encodeurs et les programmes
;  de lecture les prennent comme les autres.
;  * .mid (Standard MIDI File, formats 0, 1, 2) : sequenceur sur ak_synth ; la
;    fin (une passe a blanc) et la recherche (rejoue sans les notes) ;
;  * .fms (FM Song, FM Tracker d'Onyx) : 8 canaux, deux operateurs ; un rang de
;    la grille dure vitesse / 20 s.
; ============================================================================

MID_UNIT	= 1024		; trames par unite rendue
MID_TAIL	= 2*44100	; apres le dernier evenement (les relachements)

MI_SYN		= 0		; synthe
MI_DATA		= 8		; fichier en memoire
MI_NTRK		= 16
MI_DIV		= 24		; pulsations par noire
MI_TEMPO	= 32		; microsecondes par noire
MI_TICK		= 40		; pulsation du dernier point de reference
MI_FRAME	= 48		; sa trame (<< MI_FSH)
MI_FPT		= 56		; trames par pulsation (<< MI_FSH)
MI_TRK		= 64		; pistes
MI_DONE		= 72		; 1 : plus d'evenement
MI_POS		= 80		; trames rendues
MI_LEN		= 88		; octets du fichier
MI_SIZE		= 96
MI_FSH		= 16

MT_P		= 0		; prochain octet
MT_END		= 8
MT_TICK		= 16		; pulsation du prochain evenement
MT_STATUS	= 24		; statut courant
MT_SIZE		= 32

; x0 = pointeur -> x0 = nombre a longueur variable, x1 = pointeur suivant ; x2 = fin
mid_varlen:
	mov	x3,x0
	mov	x0,0
	mov	x4,4
.b:	cmp	x3,x2
	b.hs	.e
	ldrb	w5,[x3],1
	and	w6,w5,127
	orr	x0,x6,x0,lsl 7
	tbz	w5,7,.e
	subs	x4,x4,1
	b.ne	.b
.e:	mov	x1,x3
	ret

; x0 = decodeur (lecture sur "MThd") -> 0 / < 0
midi_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x1,MI_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x20,x0
	str	x20,[x19,AKD_CODEC]
	; tout le fichier en memoire
	ldr	x21,[x19,AKD_LEN]
	mov	x0,1
	lsl	x0,x0,26			; (64 Mo : un .mid ne va pas si loin)
	cmp	x21,x0
	b.hs	.bad
	str	x21,[x20,MI_LEN]
	mov	x0,x19
	add	x1,x21,16
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,MI_DATA]
	mov	x22,x0
	mov	x23,x21
.rd:	cbz	x23,.parsed
	mov	x0,x19
	mov	x1,x23
	movl	x2,AKD_IOBUF/2
	cmp	x1,x2
	csel	x1,x1,x2,lo
	bl	akd_need
	cbz	x1,.bad
	mov	x2,x1
	mov	x1,x0
	mov	x0,x22
	cmp	x2,x23
	csel	x2,x2,x23,lo
	mov	x24,x2
	bl	ak_memmove
	add	x22,x22,x24
	sub	x23,x23,x24
	mov	x0,x19
	mov	x1,x24
	bl	akd_skip
	b	.rd
.parsed:
	ldr	x22,[x20,MI_DATA]
	cmp	x21,14
	b.lo	.bad
	ldr	w0,[x22,4]			; taille de l'en-tete (grand-boutiste)
	rev	w0,w0
	ldrh	w1,[x22,10]
	rev16	w1,w1
	ldrh	w2,[x22,12]
	rev16	w2,w2
	sxth	w3,w2
	tbz	w3,31,.tpq			; SMPTE : -images/s, sous-images
	asr	w4,w3,8
	neg	w4,w4
	and	w5,w3,255
	mul	w2,w4,w5			; pulsations par seconde : 500 ms par noire
	lsr	w2,w2,1				; (tempo par defaut : pas de meta)
	mov	w3,w2
.tpq:	cbz	w2,.bad
	str	x1,[x20,MI_NTRK]
	str	x2,[x20,MI_DIV]
	cbz	w1,.bad
	mov	x0,x19
	mov	x2,MT_SIZE
	mul	x1,x1,x2
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,MI_TRK]
	mov	x1,0
	bl	ak_synth_new_mid
	tbnz	x0,63,.out
	str	x0,[x20,MI_SYN]
	adr	x0,midi_close
	str	x0,[x19,AKD_MODFREE]
	; passe a blanc : la duree
	mov	x0,x20
	bl	midi_rewind
	tbnz	x0,63,.bad
.dry:	mov	x0,x20
	mov	x1,2				; rien vers le synthe
	bl	midi_step
	cbnz	x0,.dry
	mov	x0,x20
	bl	midi_now_frame			; derniere trame d'evenement
	movl	x1,MID_TAIL
	add	x0,x0,x1
	str	x0,[x19,AKD_TOTAL]
	mov	x0,1
	str	x0,[x19,AKD_LIMIT]
	mov	x0,x20
	bl	midi_rewind
	mov	x0,AKF_MIDI
	str	x0,[x19,AKD_FMT]
	movl	x0,FM_RATE
	str	x0,[x19,AKD_RATE]
	mov	x0,2
	str	x0,[x19,AKD_CH]
	str	xzr,[x19,AKD_BITS]
	adr	x0,midi_next
	str	x0,[x19,AKD_NEXT]
	adr	x0,midi_seek
	str	x0,[x19,AKD_SEEKFN]
	mov	x0,x19
	mov	x1,MID_UNIT
	mov	x2,2
	bl	akd_planes
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = synthe du lecteur (x19 : decodeur) ; banque FM si AKS_FM est demande
ak_synth_new_mid:
	mov	x0,0
	mov	x1,0
	b	ak_synth_new

; x0 = decodeur : le synthe rendu
midi_close:
	ldr	x0,[x0,AKD_CODEC]
	ldr	x0,[x0,MI_SYN]
	b	ak_synth_free

; x0 = lecteur : au debut -> 0 / < 0
midi_rewind:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	movl	x0,500000
	str	x0,[x19,MI_TEMPO]
	str	xzr,[x19,MI_TICK]
	str	xzr,[x19,MI_FRAME]
	str	xzr,[x19,MI_DONE]
	str	xzr,[x19,MI_POS]
	mov	x0,x19
	bl	midi_fpt
	ldr	x20,[x19,MI_DATA]
	ldr	x21,[x19,MI_LEN]
	add	x21,x20,x21			; fin
	ldr	w0,[x20,4]
	rev	w0,w0
	add	x20,x20,x0
	add	x20,x20,8
	ldr	x22,[x19,MI_TRK]
	ldr	x23,[x19,MI_NTRK]
.t:	cbz	x23,.ok
	str	xzr,[x22,MT_P]			; piste vide par defaut
	str	xzr,[x22,MT_END]
	add	x0,x20,8
	cmp	x0,x21
	b.hi	.next
	ldr	w1,[x20,4]
	rev	w1,w1
	add	x2,x20,8
	add	x3,x2,x1
	cmp	x3,x21
	csel	x3,x21,x3,hi
	str	x2,[x22,MT_P]
	str	x3,[x22,MT_END]
	str	wzr,[x22,MT_STATUS]
	add	x20,x20,8
	add	x20,x20,x1
	mov	x0,x2				; premier delai
	mov	x2,x3
	bl	mid_varlen
	str	x0,[x22,MT_TICK]
	str	x1,[x22,MT_P]
.next:	add	x22,x22,MT_SIZE
	sub	x23,x23,1
	b	.t
.ok:	mov	x0,0
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = lecteur : recalcule MI_FPT
midi_fpt:
	ldr	x1,[x0,MI_TEMPO]
	movl	x2,FM_RATE
	mul	x1,x1,x2
	lsl	x1,x1,MI_FSH
	movl	x2,1000000
	ldr	x3,[x0,MI_DIV]
	mul	x2,x2,x3
	udiv	x1,x1,x2
	str	x1,[x0,MI_FPT]
	ret

; x0 = lecteur -> x0 = trame (<< MI_FSH) de la pulsation x1
midi_tick_frame:
	ldr	x2,[x0,MI_TICK]
	sub	x1,x1,x2
	ldr	x2,[x0,MI_FPT]
	mul	x1,x1,x2
	ldr	x0,[x0,MI_FRAME]
	add	x0,x0,x1
	ret

; x0 = lecteur -> x0 = trame du dernier evenement traite (entiere)
midi_now_frame:
	ldr	x0,[x0,MI_FRAME]
	lsr	x0,x0,MI_FSH
	ret

; x0 = lecteur -> x0 = piste du prochain evenement (0 : fin), x1 = sa pulsation
midi_peek:
	ldr	x2,[x0,MI_TRK]
	ldr	x3,[x0,MI_NTRK]
	mov	x0,0
	mov	x1,-1
.t:	cbz	x3,.e
	ldr	x4,[x2,MT_P]
	ldr	x5,[x2,MT_END]
	cmp	x4,x5
	b.hs	.n
	ldr	x4,[x2,MT_TICK]
	cmp	x4,x1
	b.hs	.n
	mov	x0,x2
	mov	x1,x4
.n:	add	x2,x2,MT_SIZE
	sub	x3,x3,1
	b	.t
.e:	ret

; x0 = lecteur, x1 = 0 tout jouer, 1 sans les notes, 2 rien vers le synthe
; -> x0 = 1 un evenement traite, 0 : fin
midi_step:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x24,x1
	bl	midi_peek
	cbz	x0,.fin
	mov	x20,x0
	mov	x21,x1
	mov	x0,x19				; le temps avance jusqu'a cet evenement
	bl	midi_tick_frame
	str	x0,[x19,MI_FRAME]
	str	x21,[x19,MI_TICK]
	ldr	x22,[x20,MT_P]
	ldr	x23,[x20,MT_END]
	ldrb	w0,[x22]
	tbz	w0,7,.run
	add	x22,x22,1
	cmp	w0,0xF0
	b.hs	.sys
	str	w0,[x20,MT_STATUS]
	b	.ch
.run:	ldr	w0,[x20,MT_STATUS]
	cbz	w0,.skipb			; donnees sans statut : l'octet est perdu
.ch:	and	w1,w0,0xF0
	cmp	w1,0xC0
	b.eq	.one
	cmp	w1,0xD0
	b.eq	.one
	ldrb	w3,[x22],1			; deux donnees
	ldrb	w4,[x22],1
	b	.send
.one:	ldrb	w3,[x22],1
	mov	w4,0
.send:	cmp	x24,2
	b.eq	.delta
	and	w2,w0,0xF0
	cmp	x24,1
	b.ne	.go
	cmp	w2,0x90				; recherche : ni notes
	b.eq	.delta
	cmp	w2,0x80
	b.eq	.delta
.go:	and	w1,w0,15
	ldr	x0,[x19,MI_SYN]
	bl	ak_synth_midi
	b	.delta
.sys:	cmp	w0,0xFF
	b.ne	.sysex
	ldrb	w21,[x22],1			; meta : type, longueur
	mov	x0,x22
	mov	x2,x23
	bl	mid_varlen
	mov	x22,x1
	cmp	w21,0x51
	b.ne	.m2f
	cmp	x0,3
	b.lo	.madv
	add	x1,x22,3
	cmp	x1,x23
	b.hi	.madv
	ldrb	w1,[x22]			; tempo
	ldrb	w2,[x22,1]
	ldrb	w3,[x22,2]
	lsl	w1,w1,16
	orr	w1,w1,w2,lsl 8
	orr	w1,w1,w3
	cbz	w1,.madv
	str	x1,[x19,MI_TEMPO]
	str	x0,[sp,-16]!
	mov	x0,x19
	bl	midi_fpt
	ldr	x0,[sp],16
	b	.madv
.m2f:	cmp	w21,0x2F			; fin de piste
	b.ne	.madv
	str	x23,[x20,MT_P]
	b	.ok
.madv:	add	x22,x22,x0
	b	.delta
.sysex:	mov	x0,x22				; F0, F7 : longueur, octets
	mov	x2,x23
	bl	mid_varlen
	add	x22,x1,x0
	b	.delta
.skipb:	add	x22,x22,1
.delta:	cmp	x22,x23
	b.hs	.eot
	mov	x0,x22
	mov	x2,x23
	bl	mid_varlen
	ldr	x2,[x20,MT_TICK]
	add	x2,x2,x0
	str	x2,[x20,MT_TICK]
	str	x1,[x20,MT_P]
	b	.ok
.eot:	str	x23,[x20,MT_P]
.ok:	mov	x0,1
	b	.out
.fin:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = lecteur, x1 = trame : joue les evenements d'avant cette trame ; x2 = mode
; de midi_step
midi_upto:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	lsl	x20,x1,MI_FSH
	mov	x21,x2
.e:	mov	x0,x19
	bl	midi_peek
	cbz	x0,.ret
	mov	x0,x19
	bl	midi_tick_frame
	cmp	x0,x20
	b.hs	.ret
	mov	x0,x19
	mov	x1,x21
	bl	midi_step
	b	.e
.ret:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur -> trames dans les tampons plans
midi_next:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x21,0				; trames
.b:	ldr	x1,[x20,MI_POS]
	add	x1,x1,SY_BLOCK
	mov	x0,x20
	mov	x2,0
	bl	midi_upto
	ldr	x0,[x20,MI_SYN]
	ldr	x1,[x19,AKD_PCM]
	add	x1,x1,x21,lsl 2
	ldr	x2,[x19,AKD_PCM+8]
	add	x2,x2,x21,lsl 2
	mov	x3,SY_BLOCK
	bl	ak_synth_render_f32
	ldr	x0,[x20,MI_POS]
	add	x0,x0,SY_BLOCK
	str	x0,[x20,MI_POS]
	add	x21,x21,SY_BLOCK
	cmp	x21,MID_UNIT
	b.lo	.b
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = trame -> 0 : rejoue les controleurs jusque la
midi_seek:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	ldr	x20,[x19,AKD_CODEC]
	ldr	x0,[x20,MI_SYN]
	bl	ak_synth_reset
	mov	x0,x20
	bl	midi_rewind
	mov	x0,x20
	mov	x1,x21
	mov	x2,1
	bl	midi_upto
	and	x21,x21,not (SY_BLOCK-1)
	str	x21,[x20,MI_POS]
	str	x21,[x19,AKD_POS]
	mov	x0,0
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  FM Song
; ---------------------------------------------------------------------------
FMS_CH		= 8
FMS_HEAD	= 107		; magie, titre, auteur, commentaire, motifs
FMS_INS		= 34		; nom (8) + 26 octets
FMS_MAXPAT	= 64
FMS_MAXROWS	= 256

FS_FM		= 0		; synthe FM
FS_DATA		= 8		; fichier
FS_LEN		= 16
FS_PAT		= 24		; motif courant : numero
FS_ROW		= 32		; rang
FS_NPAT		= 40
FS_POSBYTE	= 48		; motif courant : decalage dans le fichier
FS_ROWLEFT	= 56		; trames avant le rang suivant
FS_TAB		= 64		; decalage de chaque motif (dw)
FS_SIZE		= FS_TAB + FMS_MAXPAT*4

; x0 = decodeur -> 0 / < 0
fms_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,AKFI_SIZE+8
	mov	x19,x0
	mov	x1,FS_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x20,x0
	str	x20,[x19,AKD_CODEC]
	ldr	x21,[x19,AKD_LEN]
	movl	x0,FMS_HEAD+FMS_CH*FMS_INS
	cmp	x21,x0
	b.lo	.bad
	str	x21,[x20,FS_LEN]
	mov	x0,x19
	add	x1,x21,16
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,FS_DATA]
	mov	x22,x0
	mov	x23,x21
.rd:	cbz	x23,.parsed
	mov	x0,x19
	mov	x1,x23
	movl	x2,AKD_IOBUF/2
	cmp	x1,x2
	csel	x1,x1,x2,lo
	bl	akd_need
	cbz	x1,.bad
	mov	x2,x1
	mov	x1,x0
	mov	x0,x22
	cmp	x2,x23
	csel	x2,x2,x23,lo
	mov	x24,x2
	bl	ak_memmove
	add	x22,x22,x24
	sub	x23,x23,x24
	mov	x0,x19
	mov	x1,x24
	bl	akd_skip
	b	.rd
.parsed:
	ldr	x22,[x20,FS_DATA]
	ldrsh	w0,[x22,105]			; nombre de motifs
	cmp	w0,1
	b.lt	.bad
	cmp	w0,FMS_MAXPAT
	b.gt	.bad
	str	x0,[x20,FS_NPAT]
	; motifs : les decalages, la duree
	movl	x23,FMS_HEAD+FMS_CH*FMS_INS
	mov	x24,0				; duree (vingtiemes de seconde)
	mov	x4,0
	mov	x5,x0
.p:	cmp	x4,x5
	b.hs	.pe
	add	x6,x23,12
	cmp	x6,x21
	b.hi	.pshort
	ldrsh	w0,[x22,x23]
	add	x6,x23,2
	ldrsh	w1,[x22,x6]
	cmp	w0,1
	b.lt	.pshort
	cmp	w0,FMS_MAXROWS
	b.gt	.pshort
	cmp	w1,1
	mov	w6,1
	csel	w1,w6,w1,lt
	add	x6,x20,FS_TAB
	str	w23,[x6,x4,lsl 2]
	mul	w6,w0,w1
	add	x24,x24,x6
	add	x23,x23,12
	lsl	w0,w0,3
	add	x23,x23,x0
	add	x4,x4,1
	b	.p
.pshort:
	cbz	x4,.bad
	str	x4,[x20,FS_NPAT]		; (fichier coupe : ce qui est entier)
.pe:	mov	x0,x24				; 44100 / 20 = 2205 trames
	movl	x1,2205
	mul	x0,x0,x1
	movl	x1,MID_TAIL
	add	x0,x0,x1
	str	x0,[x19,AKD_TOTAL]
	mov	x0,1
	str	x0,[x19,AKD_LIMIT]
	; le synthe, les instruments
	mov	x0,FMS_CH
	bl	ak_fm_new
	tbnz	x0,63,.out
	str	x0,[x20,FS_FM]
	adr	x0,fms_close
	str	x0,[x19,AKD_MODFREE]
	mov	x23,0
.i:	mov	x0,sp
	mov	x1,FMS_INS
	mul	x1,x1,x23
	add	x1,x1,FMS_HEAD
	add	x1,x1,8				; (apres le nom)
	add	x1,x22,x1
	bl	ak_fm_from_fms
	ldr	x0,[x20,FS_FM]
	mov	x1,x23
	mov	x2,sp
	bl	ak_fm_instrument
	add	x23,x23,1
	cmp	x23,FMS_CH
	b.lo	.i
	; titre et auteur (champs fixes, espaces de fin retires)
	mov	x0,x19
	mov	x1,AKT_TITLE
	add	x2,x22,15
	mov	x3,20
	bl	fms_tag
	mov	x0,x19
	mov	x1,AKT_ARTIST
	add	x2,x22,35
	mov	x3,20
	bl	fms_tag
	mov	x0,AKF_FMS
	str	x0,[x19,AKD_FMT]
	movl	x0,FM_RATE
	str	x0,[x19,AKD_RATE]
	mov	x0,2
	str	x0,[x19,AKD_CH]
	str	xzr,[x19,AKD_BITS]
	mov	x0,1
	str	x0,[x19,AKD_PCMFMT]		; (entiers 32 bits cadres a gauche)
	adr	x0,fms_next
	str	x0,[x19,AKD_NEXT]
	adr	x0,fms_seek
	str	x0,[x19,AKD_SEEKFN]
	mov	x0,x20
	bl	fms_rewind
	mov	x0,x19
	mov	x1,MID_UNIT
	mov	x2,2
	bl	akd_planes
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.bad:	mov	x0,AKE_FORMAT
.out:	add	sp,sp,AKFI_SIZE+8
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = AKT_*, x2 = champ, x3 = octets : sans les espaces de fin
fms_tag:
.t:	cbz	x3,.ret
	sub	x4,x3,1
	ldrb	w4,[x2,x4]
	cmp	w4,' '
	b.hi	.put
	sub	x3,x3,1
	b	.t
.put:	mov	x4,0
	b	akd_tag_put
.ret:	ret

; x0 = decodeur : le synthe rendu
fms_close:
	ldr	x0,[x0,AKD_CODEC]
	ldr	x0,[x0,FS_FM]
	b	ak_fm_free

; x0 = lecteur : au debut
fms_rewind:
	str	xzr,[x0,FS_PAT]
	str	xzr,[x0,FS_ROW]
	str	xzr,[x0,FS_ROWLEFT]
	ret

; x20 = lecteur -> joue le rang courant et passe au suivant ; pose FS_ROWLEFT ;
; x0 = 1 s'il restait un rang (0 : fin de la chanson), x1 = 0 : jouer, 1 : sans son
fms_row:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x26,x1
	ldr	x0,[x20,FS_PAT]
	ldr	x1,[x20,FS_NPAT]
	cmp	x0,x1
	b.hs	.end
	add	x1,x20,FS_TAB
	ldr	w1,[x1,x0,lsl 2]
	ldr	x22,[x20,FS_DATA]
	add	x22,x22,x1			; le motif
	ldrsh	w23,[x22]			; rangs
	ldrsh	w24,[x22,2]			; vitesse
	cmp	w24,1
	mov	w0,1
	csel	w24,w0,w24,lt
	ldr	x25,[x20,FS_ROW]
	mov	x21,0				; canal
.c:	cbnz	x26,.nx
	mul	w0,w21,w23
	add	w0,w0,w25
	add	x0,x0,12
	ldr	x1,[x20,FS_DATA]
	ldr	x2,[x20,FS_LEN]
	add	x1,x1,x2
	add	x2,x22,x0
	cmp	x2,x1
	b.hs	.nx
	ldrb	w3,[x2]
	tbz	w3,7,.note
	ldr	x0,[x20,FS_FM]			; silence
	mov	x1,x21
	bl	ak_fm_stop
	b	.nx
.note:	tst	w3,7
	b.eq	.nx				; la note continue
	add	x4,x22,x21
	ldrb	w4,[x4,4]			; canal muet dans ce motif
	cbz	w4,.play
	ldr	x0,[x20,FS_FM]
	mov	x1,x21
	bl	ak_fm_stop
	b	.nx
.play:	and	w0,w3,7				; note
	adr	x1,fms_semi
	ldrb	w0,[x1,x0]
	ubfx	w1,w3,6,1
	add	w0,w0,w1			; diese
	ubfx	w1,w3,3,3			; octave
	bl	ak_note_octave_mhz
	mov	x2,x0
	ldr	x0,[x20,FS_FM]
	mov	x1,x21
	mov	x3,AKW_FM
	mov	x4,220
	bl	ak_fm_start
.nx:	add	x21,x21,1
	cmp	x21,FMS_CH
	b.lo	.c
	add	x25,x25,1			; rang suivant
	cmp	w25,w23
	b.lo	.s
	mov	x25,0
	ldr	x0,[x20,FS_PAT]
	add	x0,x0,1
	str	x0,[x20,FS_PAT]
.s:	str	x25,[x20,FS_ROW]
	movl	w0,2205
	mul	w0,w0,w24
	str	x0,[x20,FS_ROWLEFT]
	mov	x0,1
	b	.out
.end:	mov	x0,x20
	ldr	x0,[x20,FS_FM]
	mov	x1,-1
	bl	ak_fm_stop
	movl	x0,MID_TAIL			; rien de plus : du silence
	str	x0,[x20,FS_ROWLEFT]
	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = decodeur -> trames dans les tampons plans
fms_next:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,SY_BLOCK*4
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x21,0				; trames
.b:	ldr	x22,[x20,FS_ROWLEFT]
	cbnz	x22,.r
	mov	x1,0
	bl	fms_row
	ldr	x22,[x20,FS_ROWLEFT]
.r:	mov	x23,MID_UNIT
	sub	x23,x23,x21
	cmp	x23,x22
	csel	x23,x23,x22,lo
	mov	x0,SY_BLOCK
	cmp	x23,x0
	csel	x23,x23,x0,lo
	ldr	x0,[x20,FS_FM]
	mov	x1,sp
	mov	x2,x23
	bl	ak_fm_render
	ldr	x0,[x19,AKD_PCM]
	add	x0,x0,x21,lsl 2
	ldr	x1,[x19,AKD_PCM+8]
	add	x1,x1,x21,lsl 2
	mov	x2,sp
	mov	x3,x23
.c:	ldrsh	w4,[x2],2			; s16 -> 32 bits cadre a gauche
	lsl	w4,w4,16
	str	w4,[x0],4
	ldrsh	w4,[x2],2
	lsl	w4,w4,16
	str	w4,[x1],4
	subs	x3,x3,1
	b.ne	.c
	sub	x22,x22,x23
	str	x22,[x20,FS_ROWLEFT]
	add	x21,x21,x23
	cmp	x21,MID_UNIT
	b.lo	.b
	mov	x0,x21
	add	sp,sp,SY_BLOCK*4
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = trame -> 0 : les rangs d'avant passes sans son
fms_seek:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	ldr	x20,[x19,AKD_CODEC]
	ldr	x0,[x20,FS_FM]
	bl	ak_fm_silence
	mov	x0,x20
	bl	fms_rewind
	mov	x22,0				; trame atteinte
.r:	mov	x1,1
	bl	fms_row
	cbz	x0,.got
	ldr	x0,[x20,FS_ROWLEFT]
	add	x1,x22,x0
	cmp	x1,x21
	b.hi	.got
	mov	x22,x1
	str	xzr,[x20,FS_ROWLEFT]
	b	.r
.got:	sub	x0,x21,x22			; reste du rang
	ldr	x1,[x20,FS_ROWLEFT]
	subs	x1,x1,x0
	csel	x1,xzr,x1,lo
	str	x1,[x20,FS_ROWLEFT]
	str	x21,[x19,AKD_POS]
	mov	x0,0
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

fms_semi	db 0,0,2,4,5,7,9,11		; (1..7 = do..si)
		align 4
