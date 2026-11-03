; synthtest - essai de la synthese d'AudioKit (docs/SON.md 9)
;   synthtest              essais complets, sans fichier : FM (CRC du PCM egal a fmsynth.h
;                          d'Onyx, tools/audiotest/fmref.cpp), notes, synthe MIDI en banque
;                          FM et en SoundFont (test.sf2 : hauteur, enveloppe, molette, pedale),
;                          lecture d'un .mid et d'un .fms en memoire -> PASS / ECHEC
;   synthtest -p <fichier> [secondes]   joue un .mid ou un .fms par ak_play
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/audiokit/audiokit.inc'

FM_REF_CRC	= 0xE7C31181			; tools/audiotest/fmref.cpp
BUF_FRAMES	= 65536

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so

start:
	stp	x0,x1,[sp,-16]!
	bl	kits_open
	ldp	x0,x1,[sp],16
	cbz	x0,.all
	ldrb	w1,[x0]
	cbz	w1,.all
	bl	play_file
	mov	x0,0
	sys	SYS_EXIT
.all:	movl	x0,BUF_FRAMES*4
	bl	malloc
	adr	x1,pcm
	str	x0,[x1]
	bl	test_notes
	bl	test_fm
	bl	test_bank
	bl	test_sf2
	bl	test_mid
	bl	test_fms
	adr	x0,fails
	ldr	x19,[x0]
	adr	x0,m_end
	bl	puts
	cbnz	x19,.bad
	adr	x0,m_pass
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.bad:	adr	x0,m_fail
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = libelle, x1 = valeur montree, x2 = 1 reussi
check:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	stp	x0,x1,[sp,-16]!
	adr	x0,m_ok
	adr	x1,m_ko
	cmp	x20,0
	csel	x0,x0,x1,ne
	bl	puts
	ldp	x0,x1,[sp],16
	bl	puts
	adr	x0,m_sp
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	cbnz	x20,.out
	adr	x0,fails
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur, x1 = bas, x2 = haut -> x0 = 1 si dedans
within:
	cmp	x0,x1
	b.lt	.no
	cmp	x0,x2
	b.gt	.no
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = libelle, x1 = valeur, x2 = bas, x3 = haut
check_in:
	prologue
	stp	x0,x1,[sp,-16]!
	mov	x0,x1
	mov	x1,x2
	mov	x2,x3
	bl	within
	mov	x2,x0
	ldp	x0,x1,[sp],16
	bl	check
	epilogue

; x0 = pcm stereo, x1 = trames -> x0 = moyenne de |gauche| + |droite|
level:
	mov	x2,0
	mov	x3,x1
	cbz	x1,.e
.l:	ldrsh	w4,[x0],2
	cmp	w4,0
	cneg	w4,w4,lt
	add	x2,x2,x4
	ldrsh	w4,[x0],2
	cmp	w4,0
	cneg	w4,w4,lt
	add	x2,x2,x4
	subs	x3,x3,1
	b.ne	.l
	udiv	x0,x2,x1
	ret
.e:	mov	x0,0
	ret

; x0 = pcm stereo (canal gauche), x1 = trames, x2 = decalage bas, x3 = haut ->
; x0 = la periode (autocorrelation)
period:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x9,x2				; decalage
	mov	x10,0				; meilleur
	mov	x11,0
	sub	x12,x1,x3			; trames comparees
.lag:	cmp	x9,x3
	b.hi	.done
	mov	x4,x0
	add	x5,x0,x9,lsl 2
	mov	x6,x12
	mov	x7,0
.s:	ldrsh	w13,[x4],4
	ldrsh	w14,[x5],4
	mul	w13,w13,w14
	add	x7,x7,w13,sxtw
	subs	x6,x6,1
	b.ne	.s
	cmp	x7,x10
	b.le	.n
	mov	x10,x7
	mov	x11,x9
.n:	add	x9,x9,1
	b	.lag
.done:	mov	x0,x11
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pcm, x1 = octets : CRC-32 continue (crc)
crc_add:
	adr	x2,crc
	ldr	w3,[x2]
	movl	w6,0xEDB88320
.b:	cbz	x1,.e
	ldrb	w4,[x0],1
	eor	w3,w3,w4
	mov	x5,8
.k:	and	w4,w3,1
	neg	w4,w4
	and	w4,w4,w6
	eor	w3,w4,w3,lsr 1
	subs	x5,x5,1
	b.ne	.k
	sub	x1,x1,1
	b	.b
.e:	str	w3,[x2]
	ret

; x0 = synthe, x1 = trames : rendu ajoute au CRC
fm_run:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.l:	cbz	x20,.e
	mov	x21,8192
	cmp	x20,x21
	csel	x21,x20,x21,lo
	mov	x0,x19
	adr	x1,pcm
	ldr	x1,[x1]
	mov	x2,x21
	bl	ak_fm_render
	adr	x0,pcm
	ldr	x0,[x0]
	lsl	x1,x21,2
	bl	crc_add
	sub	x20,x20,x21
	b	.l
.e:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

test_notes:
	prologue
	adr	x0,m_notes
	bl	puts
	mov	x0,69
	bl	ak_note_mhz
	mov	x1,x0
	adr	x0,l_a4
	movl	x2,440000
	mov	x3,x2
	bl	check_in
	mov	x0,60
	bl	ak_note_mhz
	mov	x1,x0
	adr	x0,l_c4
	movl	x2,261625
	movl	x3,261627
	bl	check_in
	mov	x0,0
	mov	x1,4
	bl	ak_note_octave_mhz
	mov	x1,x0
	adr	x0,l_c4
	movl	x2,261625
	movl	x3,261627
	bl	check_in
	mov	x0,127
	bl	ak_note_mhz
	mov	x1,x0
	adr	x0,l_g9
	movl	x2,12543800
	movl	x3,12543900
	bl	check_in
	epilogue

; le scenario de tools/audiotest/fmref.cpp
test_fm:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,m_fm
	bl	puts
	mov	x0,16
	bl	ak_fm_new
	mov	x19,x0
	adr	x0,crc
	mov	w1,-1
	str	w1,[x0]
	mov	x0,x19
	mov	x1,0
	adr	x2,ins_a
	bl	ak_fm_instrument
	mov	x0,x19
	mov	x1,1
	adr	x2,ins_b
	bl	ak_fm_instrument
	mov	x0,x19
	mov	x1,2
	adr	x2,ins_c
	bl	ak_fm_instrument
	mov	x0,x19
	mov	x1,0
	movl	x2,440000
	mov	x3,AKW_FM
	mov	x4,220
	bl	ak_fm_start
	mov	x0,x19
	mov	x1,4096
	bl	fm_run
	adr	x20,starts
.st:	ldr	w1,[x20]			; voix, milli-Hz, onde, volume
	cmp	w1,99
	b.eq	.go
	ldr	w2,[x20,4]
	ldr	w3,[x20,8]
	ldr	w4,[x20,12]
	mov	x0,x19
	bl	ak_fm_start
	add	x20,x20,16
	b	.st
.go:	mov	x0,x19
	movl	x1,22050
	bl	fm_run
	mov	x0,x19
	bl	ak_fm_active
	mov	x1,x0
	adr	x0,l_active
	mov	x2,1
	mov	x3,1
	bl	check_in
	mov	x0,x19
	mov	x1,0
	bl	ak_fm_stop
	mov	x0,x19
	movl	x1,11025
	bl	fm_run
	mov	x0,x19
	mov	x1,-1
	bl	ak_fm_stop
	mov	x0,x19
	movl	x1,44100
	bl	fm_run
	adr	x0,crc
	ldr	w1,[x0]
	mvn	w1,w1
	movl	w2,FM_REF_CRC
	cmp	w1,w2
	cset	x2,eq
	adr	x0,l_crc
	bl	check
	; panoramique a droite : rien a gauche
	mov	x0,x19
	bl	ak_fm_silence
	mov	x0,x19
	mov	x1,0
	mov	x2,255
	bl	ak_fm_pan
	mov	x0,x19
	mov	x1,0
	movl	x2,440000
	mov	x3,AKW_SINE
	mov	x4,200
	bl	ak_fm_start
	mov	x0,x19
	adr	x1,pcm
	ldr	x1,[x1]
	mov	x2,4096
	bl	ak_fm_render
	adr	x0,pcm
	ldr	x0,[x0]
	mov	x1,0				; somme de |gauche|
	mov	x3,4096
.pa:	ldrsh	w2,[x0],4
	cmp	w2,0
	cneg	w2,w2,lt
	add	x1,x1,x2
	subs	x3,x3,1
	b.ne	.pa
	adr	x0,l_pan
	mov	x2,0
	mov	x3,0
	bl	check_in
	mov	x0,x19
	bl	ak_fm_free
	ldp	x19,x20,[sp],16
	epilogue

; x0 = synthe : 8192 trames dans pcm -> x0 = niveau
render_level:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,pcm
	ldr	x19,[x19]
	mov	x1,x19
	mov	x2,8192
	bl	ak_synth_render
	mov	x0,x19
	mov	x1,8192
	bl	level
	ldp	x19,x20,[sp],16
	epilogue

test_bank:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,m_bank
	bl	puts
	mov	x0,0
	mov	x1,AKS_FM
	bl	ak_synth_new
	mov	x19,x0
	mov	x0,x19
	mov	x1,0
	mov	x2,0xC0
	mov	x3,73				; flute : hauteur claire
	mov	x4,0
	bl	ak_synth_midi
	mov	x0,x19
	mov	x1,0
	mov	x2,0x90
	mov	x3,69
	mov	x4,110
	bl	ak_synth_midi
	mov	x0,x19
	bl	render_level
	mov	x0,x19
	bl	render_level
	mov	x1,x0
	adr	x0,l_on
	mov	x2,100
	movl	x3,40000
	bl	check_in
	adr	x0,pcm
	ldr	x0,[x0]
	mov	x1,4096
	mov	x2,60
	mov	x3,160
	bl	period
	mov	x1,x0
	adr	x0,l_a4p
	mov	x2,98
	mov	x3,102
	bl	check_in
	mov	x0,x19				; molette + 2 demi-tons
	mov	x1,0
	mov	x2,0xE0
	mov	x3,127
	mov	x4,127
	bl	ak_synth_midi
	mov	x0,x19
	bl	render_level
	adr	x0,pcm
	ldr	x0,[x0]
	mov	x1,4096
	mov	x2,60
	mov	x3,160
	bl	period
	mov	x1,x0
	adr	x0,l_bend
	mov	x2,87
	mov	x3,91
	bl	check_in
	mov	x0,x19				; pedale : la note reste
	mov	x1,0
	mov	x2,0xB0
	mov	x3,64
	mov	x4,127
	bl	ak_synth_midi
	mov	x0,x19
	mov	x1,0
	mov	x2,0x80
	mov	x3,69
	mov	x4,0
	bl	ak_synth_midi
	mov	x0,x19
	bl	render_level
	mov	x1,x0
	adr	x0,l_pedal
	mov	x2,100
	movl	x3,40000
	bl	check_in
	mov	x0,x19
	mov	x1,0
	mov	x2,0xB0
	mov	x3,64
	mov	x4,0
	bl	ak_synth_midi
	mov	x20,30
.dec:	mov	x0,x19
	bl	render_level
	subs	x20,x20,1
	b.ne	.dec
	mov	x1,x0
	adr	x0,l_off
	mov	x2,0
	mov	x3,50
	bl	check_in
	; percussions (canal 10) : un son
	mov	x0,x19
	mov	x1,9
	mov	x2,0x90
	mov	x3,38
	mov	x4,120
	bl	ak_synth_midi
	mov	x0,x19
	bl	render_level
	mov	x1,x0
	adr	x0,l_drum
	mov	x2,50
	movl	x3,40000
	bl	check_in
	; toute note relachee finit, meme d'un instrument de relachement 0
	mov	x0,x19
	mov	x1,9
	mov	x2,0x80
	mov	x3,38
	mov	x4,0
	bl	ak_synth_midi
	mov	x0,x19
	mov	x1,9
	mov	x2,0x90
	mov	x3,39				; BRUSH
	mov	x4,120
	bl	ak_synth_midi
	mov	x0,x19
	mov	x1,0
	mov	x2,0xC0
	mov	x3,118				; SYNDRUM
	mov	x4,0
	bl	ak_synth_midi
	mov	x0,x19
	mov	x1,0
	mov	x2,0x90
	mov	x3,60
	mov	x4,120
	bl	ak_synth_midi
	mov	x0,x19
	bl	render_level
	mov	x0,x19
	mov	x1,9
	mov	x2,0x80
	mov	x3,39
	mov	x4,0
	bl	ak_synth_midi
	mov	x0,x19
	mov	x1,0
	mov	x2,0x80
	mov	x3,60
	mov	x4,0
	bl	ak_synth_midi
	mov	x20,60
.fin:	mov	x0,x19
	bl	render_level
	subs	x20,x20,1
	b.ne	.fin
	mov	x0,x19
	bl	ak_synth_voices
	mov	x1,x0
	adr	x0,l_ended
	mov	x2,0
	mov	x3,0
	bl	check_in
	mov	x0,x19
	bl	ak_synth_free
	ldp	x19,x20,[sp],16
	epilogue

test_sf2:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,m_sf2
	bl	puts
	adr	x0,sf2_data
	movl	x1,sf2_end-sf2_data
	bl	ak_sf_open_mem
	mov	x20,x0
	mov	x1,x0
	lsr	x1,x1,63
	eor	x2,x1,1
	mov	x1,x20
	adr	x0,l_sfopen
	bl	check
	tbnz	x20,63,.out
	mov	x0,x20
	bl	ak_sf_presets
	mov	x1,x0
	adr	x0,l_presets
	mov	x2,1
	mov	x3,1
	bl	check_in
	mov	x0,x20
	mov	x1,AKS_NOFX
	bl	ak_synth_new
	mov	x19,x0
	mov	x0,x19
	mov	x1,0
	mov	x2,0x90
	mov	x3,69
	mov	x4,127
	bl	ak_synth_midi
	mov	x0,x19
	bl	render_level
	mov	x1,x0
	adr	x0,l_on
	mov	x2,3000
	movl	x3,40000
	bl	check_in
	mov	x0,x19
	bl	ak_synth_voices
	mov	x1,x0
	adr	x0,l_voices
	mov	x2,1
	mov	x3,1
	bl	check_in
	adr	x0,pcm
	ldr	x0,[x0]
	add	x0,x0,1024*4			; (apres l'attaque)
	mov	x1,4096
	mov	x2,60
	mov	x3,160
	bl	period
	mov	x1,x0
	adr	x0,l_a4p
	mov	x2,100
	mov	x3,100
	bl	check_in
	mov	x0,x19				; relachement
	mov	x1,0
	mov	x2,0x80
	mov	x3,69
	mov	x4,0
	bl	ak_synth_midi
	mov	x0,x19
	bl	render_level
	mov	x21,8
.rel:	mov	x0,x19
	bl	render_level
	subs	x21,x21,1
	b.ne	.rel
	mov	x1,x0
	adr	x0,l_off
	mov	x2,0
	mov	x3,50
	bl	check_in
	mov	x0,x19				; octave au-dessus : periode 50
	mov	x1,0
	mov	x2,0x90
	mov	x3,81
	mov	x4,100
	bl	ak_synth_midi
	mov	x0,x19
	bl	render_level
	adr	x0,pcm
	ldr	x0,[x0]
	add	x0,x0,1024*4
	mov	x1,4096
	mov	x2,30
	mov	x3,70
	bl	period
	mov	x1,x0
	adr	x0,l_a5
	mov	x2,50
	mov	x3,50
	bl	check_in
	mov	x0,x19				; molette + 2 demi-tons sur la5 : 44,5
	mov	x1,0
	mov	x2,0xE0
	mov	x3,127
	mov	x4,127
	bl	ak_synth_midi
	mov	x0,x19
	bl	render_level
	adr	x0,pcm
	ldr	x0,[x0]
	add	x0,x0,1024*4
	mov	x1,4096
	mov	x2,30
	mov	x3,70
	bl	period
	mov	x1,x0
	adr	x0,l_bend
	mov	x2,44
	mov	x3,45
	bl	check_in
	mov	x0,x19
	bl	ak_synth_free
	mov	x0,x20				; avec reverberation : la traine reste
	mov	x1,0
	bl	ak_synth_new
	mov	x19,x0
	mov	x0,x19
	mov	x1,0
	mov	x2,0x90
	mov	x3,69
	mov	x4,127
	bl	ak_synth_midi
	mov	x0,x19
	bl	render_level
	mov	x0,x19
	mov	x1,0
	mov	x2,0x80
	mov	x3,69
	mov	x4,0
	bl	ak_synth_midi
	mov	x21,2				; 370 ms : la voix est finie
.tail:	mov	x0,x19
	bl	render_level
	subs	x21,x21,1
	b.ne	.tail
	mov	x1,x0
	adr	x0,l_tail
	mov	x2,1
	movl	x3,30000
	bl	check_in
	mov	x0,x19
	bl	ak_synth_free
	mov	x0,x20				; deux synthes egaux : sans chorus, meme PCM
	mov	x1,0
	mov	x2,0
	bl	cho_diff
	mov	x1,x0
	adr	x0,l_nocho
	mov	x2,0
	mov	x3,0
	bl	check_in
	mov	x0,x20				; CC 93 sur l'un : PCM different
	mov	x1,0
	mov	x2,127
	bl	cho_diff
	mov	x1,x0
	adr	x0,l_chorus
	mov	x2,100
	movl	x3,30000
	bl	check_in
	mov	x0,x20
	bl	ak_sf_free
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = SoundFont, x1 = CC 93 du premier synthe, x2 = CC 93 du second -> x0 = moyenne des
; ecarts entre leurs rendus de la meme note (trames 8192..16384, canal gauche)
cho_diff:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x22,x2
	mov	x2,x1
	bl	cho_synth
	mov	x19,x0
	mov	x0,x21
	mov	x2,x22
	bl	cho_synth
	mov	x20,x0
	adr	x21,pcm
	ldr	x21,[x21]
	movl	x22,16384*4			; deuxieme moitie du tampon
	add	x22,x21,x22
	mov	x0,x19
	mov	x1,x21
	mov	x2,8192
	bl	ak_synth_render
	mov	x0,x20
	mov	x1,x22
	mov	x2,8192
	bl	ak_synth_render
	mov	x0,x19
	mov	x1,x21
	mov	x2,8192
	bl	ak_synth_render
	mov	x0,x20
	mov	x1,x22
	mov	x2,8192
	bl	ak_synth_render
	mov	x0,0
	mov	x1,8192
.d:	ldrsh	w2,[x21],4
	ldrsh	w3,[x22],4
	sub	w2,w2,w3
	cmp	w2,0
	cneg	w2,w2,lt
	add	x0,x0,x2
	subs	x1,x1,1
	b.ne	.d
	lsr	x21,x0,13
	mov	x0,x19
	bl	ak_synth_free
	mov	x0,x20
	bl	ak_synth_free
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = SoundFont, x2 = CC 93 -> synthe, la4 enfonce
cho_synth:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x2
	mov	x1,0
	bl	ak_synth_new
	mov	x19,x0
	mov	x1,0
	mov	x2,0xB0
	mov	x3,93
	mov	x4,x20
	bl	ak_synth_midi
	mov	x0,x19
	mov	x1,0
	mov	x2,0x90
	mov	x3,69
	mov	x4,127
	bl	ak_synth_midi
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon, x1 = octets, x2 = format attendu, x3 = duree min (ms), x4 = duree max
test_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,AKI_SIZE
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	bl	ak_dec_open_mem
	mov	x19,x0
	lsr	x1,x0,63
	eor	x2,x1,1
	mov	x1,x19
	adr	x0,l_open
	bl	check
	tbnz	x19,63,.out
	mov	x0,x19
	mov	x1,sp
	bl	ak_dec_info
	ldr	x1,[sp,AKI_FORMAT]
	adr	x0,l_format
	mov	x2,x21
	mov	x3,x21
	bl	check_in
	ldr	x1,[sp,AKI_DURATION]
	adr	x0,l_dur
	mov	x2,x22
	mov	x3,x23
	bl	check_in
	mov	x20,0				; trames lues
	mov	x21,0				; crete
	mov	x22,x19
.r:	mov	x0,x22
	adr	x1,pcm
	ldr	x1,[x1]
	mov	x2,4096
	bl	ak_dec_read
	cmp	x0,0
	b.le	.e
	add	x20,x20,x0
	mov	x24,x0
	adr	x0,pcm
	ldr	x0,[x0]
	mov	x1,x24
	bl	level
	cmp	x0,x21
	csel	x21,x0,x21,hi
	b	.r
.e:	mov	x1,x20
	adr	x0,l_frames
	mov	x2,1000
	movl	x3,10000000
	bl	check_in
	mov	x1,x21
	adr	x0,l_peak
	mov	x2,200
	movl	x3,50000
	bl	check_in
	mov	x0,x22				; recherche puis lecture : encore du son
	mov	x1,100
	bl	ak_dec_seek
	mov	x1,x0
	adr	x0,l_seek
	mov	x2,0
	mov	x3,0
	bl	check_in
	mov	x0,x22
	adr	x1,pcm
	ldr	x1,[x1]
	mov	x2,4096
	bl	ak_dec_read
	mov	x1,x0
	adr	x0,l_after
	mov	x2,1
	mov	x3,4096
	bl	check_in
	mov	x0,x22
	bl	ak_dec_close
.out:	add	sp,sp,AKI_SIZE
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

test_mid:
	prologue
	adr	x0,m_mid
	bl	puts
	adr	x0,mid_data
	mov	x1,mid_end-mid_data
	mov	x2,AKF_MIDI
	movl	x3,3500				; 1,5 s de notes + 2 s de fin
	movl	x4,3510
	bl	test_file
	epilogue

test_fms:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,m_fms
	bl	puts
	; une chanson : 8 rangs de vitesse 2 (0,1 s), une note par rang
	movl	x0,107+8*34+12+8*8
	mov	x19,x0
	bl	malloc
	mov	x20,x0
	mov	x1,x19
.z:	subs	x1,x1,1
	strb	wzr,[x20,x1]
	b.ne	.z
	adr	x1,fms_magic
	mov	x2,0
.m:	ldrb	w3,[x1,x2]
	strb	w3,[x20,x2]
	add	x2,x2,1
	cmp	x2,15
	b.lo	.m
	mov	w1,1				; un motif
	strh	w1,[x20,105]
	add	x0,x20,107			; instrument du canal 0
	adr	x1,fms_ins
	mov	x2,0
.i:	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	add	x2,x2,1
	cmp	x2,34
	b.lo	.i
	add	x0,x20,107+8*34
	mov	w1,8
	strh	w1,[x0]
	mov	w1,2
	strh	w1,[x0,2]
	adr	x1,fms_notes
	add	x0,x0,12
	mov	x2,0
.n:	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	add	x2,x2,1
	cmp	x2,8
	b.lo	.n
	mov	x0,x20
	mov	x1,x19
	mov	x2,AKF_FMS
	movl	x3,2790				; 0,8 s + 2 s de fin
	movl	x4,2810
	bl	test_file
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ligne de commande : -p <fichier> [secondes]
play_file:
	prologue
	stp	x19,x20,[sp,-16]!
	ldrb	w1,[x0]
	cmp	w1,'-'
	b.ne	.path
	add	x0,x0,3
.path:	mov	x19,x0
	mov	x20,5
.sp:	ldrb	w1,[x0],1
	cbz	w1,.go
	cmp	w1,' '
	b.ne	.sp
	sub	x1,x0,1
	strb	wzr,[x1]
	bl	parse_dec
	mov	x20,x0
.go:	mov	x0,x19
	mov	x1,0
	bl	ak_play
	mov	x19,x0
	tbnz	x0,63,.err
.t:	mov	x0,1000
	sys	SYS_SLEEP
	mov	x0,x19
	bl	ak_play_pos
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,x19
	bl	ak_play_state
	cmp	x0,AKP_ENDED
	b.hs	.end
	subs	x20,x20,1
	b.ne	.t
.end:	mov	x0,x19
	bl	ak_play_stop
	b	.out
.err:	bl	print_dec
	adr	x0,m_nl
	bl	puts
.out:	ldp	x19,x20,[sp],16
	epilogue

m_notes		db '--- notes',10,0
m_fm		db '--- synthese FM',10,0
m_bank		db '--- MIDI, banque FM',10,0
m_sf2		db '--- MIDI, SoundFont',10,0
m_mid		db '--- fichier .mid',10,0
m_fms		db '--- fichier .fms',10,0
m_ok		db '  ok    ',0
m_ko		db '  ECHEC ',0
m_sp		db ' = ',0
m_nl		db 10,0
m_end		db 'synthtest : ',0
m_pass		db 'PASS',10,0
m_fail		db 'ECHECS : ',0
l_a4		db 'note 69 (mHz)',0
l_c4		db 'do4 (mHz)',0
l_g9		db 'note 127 (mHz)',0
l_active	db 'voix actives',0
l_crc		db 'CRC egal a fmsynth.h',0
l_pan		db 'panoramique : rien a gauche',0
l_on		db 'niveau note enfoncee',0
l_a4p		db 'periode de la4 (trames)',0
l_a5		db 'periode de la5 (trames)',0
l_bend		db 'periode avec molette',0
l_pedal		db 'niveau sous la pedale',0
l_off		db 'niveau apres le relachement',0
l_drum		db 'niveau percussion',0
l_ended		db 'voix restantes apres relachement',0
l_sfopen	db 'SoundFont lue',0
l_presets	db 'presets',0
l_voices	db 'voix',0
l_tail		db 'traine de reverberation',0
l_chorus	db 'ecart avec chorus',0
l_nocho		db 'ecart sans chorus',0
l_open		db 'ouverture',0
l_format	db 'format',0
l_dur		db 'duree (ms)',0
l_frames	db 'trames',0
l_peak		db 'niveau maximal',0
l_seek		db 'recherche',0
l_after		db 'lecture apres recherche',0
fms_magic	db 'fm-song-project'
fms_ins		db 'TEST    ',15,15,1,2,5,7,3,4,20,0,0,0,0,0,0,0,1,1,0,0,0,0,3,0,1,0
fms_notes	db 33,34,35,36,37,38,39,128		; do4 ... si4, silence
		align 8
; une note d'une seconde a 120 noires/min, deux pistes (format 1)
mid_data	db 'MThd',0,0,0,6,0,1,0,2,0,96
		db 'MTrk',0,0,0,15, 0,0xFF,0x51,3,0x07,0xA1,0x20, 0,0xFF,0x2F,0, 0,0,0,0
		db 'MTrk',0,0,0,24, 0,0xC0,73, 0,0x90,60,100, 96,0x90,64,100, 96,0x80,60,0, 0,0x80,64,0
		db 96,0xFF,0x2F,0
mid_end:
		align 8
sf2_data:
		file 'test.sf2'
sf2_end:
		align 8
; instruments du scenario (fmref.cpp) : AKFI_*
ins_a	db 15,2,1,2,20,0,1,0,AKFF_SUSTAINED,0, 15,1,2,3,0,0,1,0,AKFF_SUSTAINED,0, 3,0,0,0
ins_b	db 10,4,3,5,10,0,3,1,AKFF_TREMOLO,0, 12,3,4,4,3,0,2,2,AKFF_VIBRATO,0, 5,1,0,0
ins_c	db 8,5,15,6,30,0,0,3,AKFF_VIBRATO or AKFF_TREMOLO or AKFF_SUSTAINED,0, 15,0,0,7,5,0,7,0,AKFF_SUSTAINED,0, 7,0,0,0
		align 4
starts	dw 1,261626,AKW_FM,180, 2,659255,AKW_FM,200, 3,220000,AKW_SINE,120, 4,330000,AKW_TRIANGLE,120
	dw 5,110000,AKW_SAW,90, 6,8000000,AKW_NOISE,60, 7,55000,AKW_SQUARE,60, 99,0,0,0

segment readable writeable
kit_data
pcm	dd 0
crc	dd 0
fails	dd 0
