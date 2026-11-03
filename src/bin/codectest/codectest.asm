; codectest - essai des formats audio d'AudioKit (docs/SON.md 9)
;   codectest <fichier> [reference.raw]
;          decode tout le fichier (ak_dec_*) : format, frequence, canaux,
;          etiquettes, trames, temps de decodage et vitesse (fois le temps
;          reel) ; avec une reference (s16 entrelace, meme format) : ecart
;          maximal et rapport signal / bruit
;   codectest -o <sortie.raw> <fichier>     ecrit le PCM decode (s16)
;   codectest -s <ms> <fichier> [ref.raw]   recherche puis decode la suite
;          (comparee a la reference a partir de la meme position)
;   codectest -p <fichier> [secondes]       ak_play : joue (5 s par defaut),
;          dit la position chaque seconde, pause 1 s, reprise, arret
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/audiokit/audiokit.inc'

FRAMES	= 4096

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so

start:
	stp	x0,x1,[sp,-16]!
	bl	kits_open
	ldp	x0,x1,[sp],16
	cbz	x0,usage
	adr	x1,argv				; decoupe aux espaces
	mov	x2,0
.sp:	ldrb	w3,[x0]
	cbz	w3,.eol
	cmp	w3,' '
	b.ne	.w
	strb	wzr,[x0],1
	b	.sp
.w:	cmp	x2,8
	b.hs	.eol
	str	x0,[x1,x2,lsl 3]
	add	x2,x2,1
.wl:	ldrb	w3,[x0]
	cbz	w3,.eol
	cmp	w3,' '
	b.eq	.sp
	add	x0,x0,1
	b	.wl
.eol:	adr	x0,argc
	str	x2,[x0]
	cbz	x2,usage
	adr	x1,argv
	ldr	x0,[x1]
	ldrb	w2,[x0]
	cmp	w2,'-'
	b.ne	.dec
	ldrb	w2,[x0,1]
	cmp	w2,'o'
	b.eq	.out
	cmp	w2,'s'
	b.eq	.seek
	cmp	w2,'p'
	b.eq	play_mode
	b	usage
.dec:	adr	x1,argv				; codectest f [ref]
	ldr	x0,[x1]
	adr	x2,f_in
	str	x0,[x2]
	ldr	x0,[x1,8]
	adr	x2,f_ref
	str	x0,[x2]
	b	decode
.out:	adr	x0,argc
	ldr	x0,[x0]
	cmp	x0,3
	b.lo	usage
	adr	x1,argv
	ldr	x0,[x1,8]
	adr	x2,f_out
	str	x0,[x2]
	ldr	x0,[x1,16]
	adr	x2,f_in
	str	x0,[x2]
	b	decode
.seek:	adr	x0,argc
	ldr	x0,[x0]
	cmp	x0,3
	b.lo	usage
	adr	x1,argv
	ldr	x0,[x1,8]
	bl	parse_dec
	adr	x1,seek_ms
	str	x0,[x1]
	adr	x1,argv
	ldr	x0,[x1,16]
	adr	x2,f_in
	str	x0,[x2]
	ldr	x0,[x1,24]
	adr	x2,f_ref
	str	x0,[x2]
	b	decode

usage:	adr	x0,s_usage
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

fail:	mov	x19,x0
	adr	x0,s_err
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,s_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

decode:
	mov	x0,FRAMES*16			; tampons : decode, reference
	bl	malloc
	cbz	x0,fail
	mov	x27,x0
	mov	x0,FRAMES*16
	bl	malloc
	cbz	x0,fail
	mov	x28,x0
	sys	SYS_TIME_MS
	mov	x24,x0
	adr	x0,f_in
	ldr	x0,[x0]
	bl	ak_dec_open
	tbnz	x0,63,fail
	mov	x19,x0
	sys	SYS_TIME_MS
	sub	x0,x0,x24
	adr	x1,t_open
	str	x0,[x1]
	mov	x0,x19
	adr	x1,info
	bl	ak_dec_info
	adr	x0,s_fmt
	bl	puts
	adr	x1,info
	ldr	x0,[x1,AKI_FORMAT]
	adr	x1,fmt_names
	ldr	x0,[x1,x0,lsl 3]
	adr	x1,fmt_names
	add	x0,x1,x0
	bl	puts
	adr	x0,s_sp
	bl	puts
	adr	x1,info
	ldr	x0,[x1,AKI_RATE]
	bl	print_dec
	adr	x0,s_hz
	bl	puts
	adr	x1,info
	ldr	x0,[x1,AKI_CHANNELS]
	bl	print_dec
	adr	x0,s_ch
	bl	puts
	adr	x1,info
	ldr	x0,[x1,AKI_DURATION]
	bl	print_dec
	adr	x0,s_ms
	bl	puts
	adr	x1,info
	ldr	x0,[x1,AKI_BITRATE]
	mov	x1,1000
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,s_kbps
	bl	puts
	mov	x20,AKT_TITLE			; etiquettes
.tag:	mov	x0,x19
	mov	x1,x20
	adr	x2,tagbuf
	mov	x3,256
	bl	ak_dec_tag
	cbz	x0,.tn
	adr	x0,tag_names
	add	x0,x0,x20,lsl 4
	bl	puts
	adr	x0,tagbuf
	bl	puts
	adr	x0,s_nl
	bl	puts
.tn:	add	x20,x20,1
	cmp	x20,AKT_ALBUM
	b.ls	.tag
	adr	x0,info
	ldr	x25,[x0,AKI_CHANNELS]
	lsl	x25,x25,1			; octets par trame
	; reference, sortie
	mov	x21,-1
	adr	x0,f_ref
	ldr	x0,[x0]
	cbz	x0,.noref
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,fail
	mov	x21,x0
.noref:	mov	x22,-1
	adr	x0,f_out
	ldr	x0,[x0]
	cbz	x0,.noout
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	bl	f_ouvrir
	tbnz	x0,63,fail
	mov	x22,x0
.noout:	adr	x0,seek_ms			; recherche
	ldr	x1,[x0]
	cbz	x1,.go
	mov	x0,x19
	bl	ak_dec_seek
	tbnz	x0,63,fail
	mov	x0,x19
	bl	ak_dec_tell
	mov	x20,x0
	adr	x0,s_seek
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,s_nl
	bl	puts
	tbnz	x21,63,.go			; la reference au meme endroit
	mov	x0,x21
	mul	x1,x20,x25
	mov	x2,0
	bl	f_position
.go:	mov	x23,0				; trames
	mov	x26,0				; ecart maximal
	fmov	d8,xzr				; signal
	fmov	d9,xzr				; bruit
	mov	x24,0				; temps de decodage (ms)
.rd:	sys	SYS_TIME_MS
	mov	x20,x0
	mov	x0,x19
	mov	x1,x27
	mov	x2,FRAMES
	bl	ak_dec_read
	mov	x2,x0
	sys	SYS_TIME_MS
	sub	x0,x0,x20
	add	x24,x24,x0
	cmp	x2,0
	b.le	.end
	add	x23,x23,x2
	mul	x20,x2,x25			; octets
	tbnz	x22,63,.nw
	mov	x0,x22
	mov	x1,x27
	mov	x2,x20
	bl	f_ecrire
.nw:	tbnz	x21,63,.rd
	mov	x0,x21				; comparaison
	mov	x1,x28
	mov	x2,x20
	bl	f_lire
	cmp	x0,0
	b.le	.rd
	lsr	x0,x0,1				; echantillons
	mov	x1,0
.cmp:	cmp	x1,x0
	b.hs	.rd
	ldrsh	w2,[x27,x1,lsl 1]
	ldrsh	w3,[x28,x1,lsl 1]
	sub	w4,w2,w3
	cmp	w4,0
	cneg	w4,w4,lt
	cmp	x4,x26
	csel	x26,x4,x26,hi
	scvtf	d0,w3
	fmadd	d8,d0,d0,d8
	sxtw	x4,w4
	scvtf	d0,x4
	fmadd	d9,d0,d0,d9
	add	x1,x1,1
	b	.cmp
.end:	mov	x20,x2
	adr	x0,s_frames
	bl	puts
	mov	x0,x23
	bl	print_dec
	tbz	x20,63,.ok
	adr	x0,s_errend
	bl	puts
	mov	x0,x20
	bl	print_dec
.ok:	adr	x0,s_nl
	bl	puts
	adr	x0,s_time
	bl	puts
	adr	x0,t_open
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,s_time2
	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,s_speed
	bl	puts
	; vitesse : duree / temps, en dixiemes
	mov	x0,10000
	mul	x0,x23,x0
	adr	x1,info
	ldr	x1,[x1,AKI_RATE]
	udiv	x0,x0,x1			; duree en dixiemes de ms
	cmp	x24,1
	csinc	x1,x24,xzr,hs
	udiv	x0,x0,x1
	bl	print_tenth
	adr	x0,s_x
	bl	puts
	tbnz	x21,63,.done
	adr	x0,s_maxd
	bl	puts
	mov	x0,x26
	bl	print_dec
	adr	x0,s_snr
	bl	puts
	fcmp	d9,0.0
	b.eq	.inf
	fdiv	d0,d8,d9
	bl	log2d
	ldr	d1,c_10log
	fmul	d0,d0,d1			; 10 log10 = log2 * 3,0103
	ldr	d1,c_ten
	fmul	d0,d0,d1
	fcvtzs	x0,d0
	bl	print_tenth
	adr	x0,s_db
	bl	puts
	b	.done
.inf:	adr	x0,s_inf
	bl	puts
.done:	mov	x0,x19
	bl	ak_dec_close
	tbnz	x22,63,.nc
	mov	x0,x22
	bl	f_fermer
.nc:	mov	x0,0
	sys	SYS_EXIT

; d0 = x > 0 -> d0 = log2(x)
log2d:
	fmov	x1,d0
	lsr	x2,x1,52
	and	x2,x2,0x7FF
	sub	x2,x2,1023			; exposant
	and	x1,x1,0xFFFFFFFFFFFFF
	orr	x1,x1,0x3FF0000000000000	; mantisse dans [1, 2[
	fmov	d1,x1
	scvtf	d0,x2
	fmov	d2,0.5
	fmov	d3,2.0
	fmov	d4,0.5
	mov	x3,30
.l:	fmul	d1,d1,d1
	fcmp	d1,d3
	b.lt	.n
	fmul	d1,d1,d2
	fadd	d0,d0,d2
.n:	fmul	d2,d2,d4
	subs	x3,x3,1
	b.ne	.l
	ret

; x0 = nombre en dixiemes (signe)
print_tenth:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	tbz	x19,63,.p
	adr	x0,s_minus
	bl	puts
	neg	x19,x19
.p:	mov	x1,10
	udiv	x0,x19,x1
	msub	x20,x0,x1,x19
	bl	print_dec
	adr	x0,s_comma
	bl	puts
	mov	x0,x20
	bl	print_dec
	ldp	x19,x20,[sp],16
	epilogue

; --- -p : lecteur ---------------------------------------------------------------
play_mode:
	adr	x0,argc
	ldr	x0,[x0]
	cmp	x0,2
	b.lo	usage
	mov	x20,5
	cmp	x0,3
	b.lo	.go
	adr	x1,argv
	ldr	x0,[x1,16]
	bl	parse_dec
	mov	x20,x0
.go:	adr	x1,argv
	ldr	x0,[x1,8]
	mov	x1,0
	bl	ak_play
	tbnz	x0,63,fail
	mov	x19,x0
	mov	x21,0				; secondes
.sec:	mov	x0,1000
	sys	SYS_SLEEP
	add	x21,x21,1
	adr	x0,s_pos
	bl	puts
	mov	x0,x19
	bl	ak_play_pos
	bl	print_dec
	adr	x0,s_state
	bl	puts
	mov	x0,x19
	bl	ak_play_state
	bl	print_dec
	adr	x0,s_nl
	bl	puts
	cmp	x21,2				; pause d'une seconde a 2 s
	b.ne	.np
	mov	x0,x19
	mov	x1,1
	bl	ak_play_pause
	mov	x0,1000
	sys	SYS_SLEEP
	adr	x0,s_pause
	bl	puts
	mov	x0,x19
	bl	ak_play_pos
	bl	print_dec
	adr	x0,s_nl
	bl	puts
	mov	x0,x19
	mov	x1,0
	bl	ak_play_pause
.np:	mov	x0,x19
	bl	ak_play_state
	cmp	x0,AKP_ENDED
	b.hs	.stop
	cmp	x21,x20
	b.lo	.sec
.stop:	mov	x0,x19
	bl	ak_play_stop
	adr	x0,s_stopped
	bl	puts
	mov	x0,0
	sys	SYS_EXIT

		align	8
c_10log		dd	3.0102999566398120
c_ten		dd	10.0
fmt_names	dd	fn0-fmt_names, fn1-fmt_names, fn2-fmt_names, fn3-fmt_names
		dd	fn4-fmt_names, fn5-fmt_names, fn6-fmt_names, fn7-fmt_names
fn0		db '?',0
fn1		db 'WAV',0
fn2		db 'FLAC',0
fn3		db 'MP3',0
fn4		db 'Vorbis',0
fn5		db 'Opus',0
fn6		db 'MIDI',0
fn7		db 'FM Song',0
tag_names	db 'titre   : ',0,0,0,0,0,0
		db 'artiste : ',0,0,0,0,0,0
		db 'album   : ',0,0,0,0,0,0
s_usage	db 'codectest <fichier> [ref.raw] | -o <sortie.raw> <fichier> | -s <ms> <fichier> [ref.raw] | -p <fichier> [s]',10,0
s_err	db 'codectest : erreur ',0
s_fmt	db 'format : ',0
s_sp	db ', ',0
s_hz	db ' Hz, ',0
s_ch	db ' canaux, ',0
s_ms	db ' ms, ',0
s_kbps	db ' kbit/s',10,0
s_frames db 'trames : ',0
s_errend db ', erreur ',0
s_time	db 'ouverture : ',0
s_time2	db ' ms, decodage : ',0
s_speed	db ' ms, vitesse : ',0
s_x	db ' x temps reel',10,0
s_maxd	db 'reference : ecart max ',0
s_snr	db ', RSB ',0
s_db	db ' dB',10,0
s_inf	db 'identique',10,0
s_seek	db 'apres la recherche : trame ',0
s_pos	db 'position (ms) ',0
s_state	db ', etat ',0
s_pause	db 'en pause, position ',0
s_stopped db 'arret',10,0
s_minus	db '-',0
s_comma	db ',',0
s_nl	db 10,0
		align	4

segment readable writeable
kit_data
		align	8
argc	dd 0
argv	rd 8
f_in	dd 0
f_ref	dd 0
f_out	dd 0
seek_ms	dd 0
t_open	dd 0
info	rb AKI_SIZE
tagbuf	rb 256
