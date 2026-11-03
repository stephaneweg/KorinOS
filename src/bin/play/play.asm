; play - joue un fichier audio par le lecteur d'AudioKit (docs/SON.md 3 et 9)
;   play <fichier> [--volume 0..100] [--seek ms] [--output n]
;   WAV, FLAC, MP3, Ogg Vorbis (Ogg Opus : quand AudioKit le decodera).
;   Affiche le format, la duree, titre / artiste / album, puis la position
;   (mm:ss / mm:ss) une fois par seconde jusqu'a la fin.
;   Code de sortie : 0 si tout a ete joue, 1 sinon.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/audiokit/audiokit.inc'

PLY_ARGS	= 16			; mots au plus sur la ligne de commande

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
.w:	cmp	x2,PLY_ARGS
	b.hs	.eol
	str	x0,[x1,x2,lsl 3]
	add	x2,x2,1
.wl:	ldrb	w3,[x0]
	cbz	w3,.eol
	cmp	w3,' '
	b.eq	.sp
	add	x0,x0,1
	b	.wl
.eol:	mov	x19,x2				; x19 = nombre de mots
	mov	x20,0				; x20 = mot courant
	mov	x21,0				; x21 = fichier
	mov	x22,100				; x22 = volume (0..100)
	mov	x23,0				; x23 = recherche (ms)
	mov	x24,0				; x24 = sortie
.arg:	cmp	x20,x19
	b.hs	.args_done
	adr	x1,argv
	ldr	x25,[x1,x20,lsl 3]
	add	x20,x20,1
	ldrb	w2,[x25]
	cmp	w2,'-'
	b.eq	.opt
	cbnz	x21,usage			; un seul fichier
	mov	x21,x25
	b	.arg
.opt:	cmp	x20,x19				; toute option prend une valeur
	b.hs	usage
	adr	x1,argv
	ldr	x26,[x1,x20,lsl 3]
	add	x20,x20,1
	mov	x0,x25
	adr	x1,o_volume
	bl	str_eq
	cbnz	x0,.vol
	mov	x0,x25
	adr	x1,o_seek
	bl	str_eq
	cbnz	x0,.seek
	mov	x0,x25
	adr	x1,o_output
	bl	str_eq
	cbz	x0,usage
	mov	x0,x26
	bl	parse_dec
	mov	x24,x0
	b	.arg
.vol:	mov	x0,x26
	bl	parse_dec
	mov	x22,x0
	cmp	x22,100
	b.hi	usage
	b	.arg
.seek:	mov	x0,x26
	bl	parse_dec
	mov	x23,x0
	b	.arg
.args_done:
	cbz	x21,usage

	; --- lecture --------------------------------------------------------------
	mov	x0,x21
	mov	x1,x24
	bl	ak_play
	tbnz	x0,63,open_fail
	mov	x19,x0				; x19 = lecteur
	mov	x0,x22				; volume : courbe a l'oreille, 0..256
	bl	ak_volume_curve
	lsr	x1,x0,8
	mov	x0,x19
	bl	ak_play_volume
	cbz	x23,.info
	mov	x0,x19
	mov	x1,x23
	bl	ak_play_seek
.info:	mov	x0,x19				; format, duree, etiquettes
	bl	ak_play_decoder
	mov	x20,x0				; x20 = decodeur
	adr	x1,info
	bl	ak_dec_info
	adr	x0,s_fmt
	bl	lang_puts
	adr	x1,info
	ldr	x0,[x1,AKI_FORMAT]
	cmp	x0,AKF_FMS
	csel	x0,xzr,x0,hi
	adr	x1,fmt_names
	ldr	x0,[x1,x0,lsl 3]
	adr	x1,fmt_names
	add	x0,x1,x0
	bl	puts
	adr	x0,s_sep
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
	bl	lang_puts
	adr	x1,info
	ldr	x0,[x1,AKI_BITRATE]
	cbz	x0,.nobr
	adr	x0,s_sep
	bl	puts
	adr	x1,info
	ldr	x0,[x1,AKI_BITRATE]
	mov	x1,1000
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,s_kbps
	bl	puts
.nobr:	adr	x0,s_nl
	bl	puts
	adr	x0,s_dur
	bl	lang_puts
	adr	x1,info
	ldr	x0,[x1,AKI_DURATION]
	adr	x1,tbuf
	bl	fmt_time
	adr	x0,tbuf
	bl	puts
	adr	x0,s_nl
	bl	puts
	mov	x21,AKT_TITLE			; etiquettes presentes
.tag:	mov	x0,x20
	mov	x1,x21
	adr	x2,tagbuf
	mov	x3,256
	bl	ak_dec_tag
	cmp	x0,0
	b.le	.tn
	adr	x0,tag_names
	add	x0,x0,x21,lsl 4
	bl	lang_puts
	adr	x0,tagbuf
	bl	puts
	adr	x0,s_nl
	bl	puts
.tn:	add	x21,x21,1
	cmp	x21,AKT_ALBUM
	b.ls	.tag

	; --- position chaque seconde jusqu'a la fin -------------------------------
	adr	x1,info
	ldr	x22,[x1,AKI_DURATION]		; x22 = duree (ms, -1 : inconnue)
.sec:	mov	x0,1000
	sys	SYS_SLEEP
	mov	x0,x19
	bl	ak_play_state
	mov	x21,x0				; x21 = etat
	cmp	x21,AKP_ENDED
	b.eq	.ended
	cmp	x21,AKP_ERROR
	b.eq	.error
	mov	x0,x19
	bl	ak_play_pos
	adr	x1,tbuf
	bl	fmt_time
	adr	x1,s_slash
	bl	str_cat
	mov	x1,x0
	mov	x0,x22
	bl	fmt_time
	adr	x1,s_nl
	bl	str_cat
	adr	x0,tbuf
	bl	puts
	b	.sec
.ended:	mov	x0,x19				; derniere position : la duree
	bl	ak_play_stop
	adr	x0,s_end
	bl	lang_puts
	mov	x0,0
	sys	SYS_EXIT
.error:	mov	x0,x19
	bl	ak_play_stop
	adr	x0,s_decerr
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

; ak_play a echoue : x0 = erreur
open_fail:
	mov	x19,x0
	adr	x0,s_err
	bl	lang_puts
	cmn	x19,-AKE_FORMAT
	b.ne	.n1
	adr	x0,s_e_format
	b	.msg
.n1:	cmn	x19,-AKE_CORRUPT
	b.ne	.n2
	adr	x0,s_e_corrupt
	b	.msg
.n2:	cmn	x19,-AKE_IO
	b.ne	.n3
	adr	x0,s_e_io
	b	.msg
.n3:	cmn	x19,-AKE_NOMEM
	b.ne	.n4
	adr	x0,s_e_nomem
	b	.msg
.n4:	adr	x0,s_e_other
.msg:	bl	lang_puts
	adr	x0,s_paren
	bl	lang_puts
	mov	x0,x19
	bl	print_dec
	adr	x0,s_paren2
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

usage:	adr	x0,s_usage
	bl	lang_puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = duree (ms, < 0 : inconnue), x1 = tampon -> x0 = fin (« mm:ss », NUL)
fmt_time:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	tbz	x0,63,.known
	mov	w2,'-'
	strb	w2,[x19],1
	strb	w2,[x19],1
	mov	w2,':'
	strb	w2,[x19],1
	mov	w2,'-'
	strb	w2,[x19],1
	strb	w2,[x19],1
	b	.end
.known:	mov	x1,1000
	udiv	x0,x0,x1			; secondes
	mov	x1,60
	udiv	x20,x0,x1			; minutes
	msub	x2,x20,x1,x0			; secondes restantes
	str	x2,[sp,-16]!
	cmp	x20,10
	b.hs	.m
	mov	w3,'0'
	strb	w3,[x19],1
.m:	mov	x0,x20
	mov	x1,x19
	bl	fmt_dec
	mov	x19,x0
	mov	w3,':'
	strb	w3,[x19],1
	ldr	x2,[sp],16
	mov	x1,10
	udiv	x3,x2,x1
	msub	x4,x3,x1,x2
	add	w3,w3,'0'
	add	w4,w4,'0'
	strb	w3,[x19],1
	strb	w4,[x19],1
.end:	strb	wzr,[x19]
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

		align	8
fmt_names	dd	fn0-fmt_names, fn1-fmt_names, fn2-fmt_names, fn3-fmt_names
		dd	fn4-fmt_names, fn5-fmt_names, fn6-fmt_names, fn7-fmt_names
fn0		db '?',0
fn1		db 'WAV',0
fn2		db 'FLAC',0
fn3		db 'MP3',0
fn4		db 'Ogg Vorbis',0
fn5		db 'Ogg Opus',0
fn6		db 'MIDI',0
fn7		db 'FM Song',0
tag_names	db 'titre   : ',0,0,0,0,0,0
		db 'artiste : ',0,0,0,0,0,0
		db 'album   : ',0,0,0,0,0,0
o_volume	db '--volume',0
o_seek		db '--seek',0
o_output	db '--output',0
s_usage		db 'usage : play <fichier> [--volume 0..100] [--seek ms] [--output n]',10
		db '        (WAV, FLAC, MP3, Ogg Vorbis)',10,0
s_err		db 'play : ',0
s_e_format	db 'format non reconnu ou non pris en charge (WAV, FLAC, MP3, Ogg Vorbis ; Ogg Opus pas encore)',0
s_e_corrupt	db 'fichier invalide ou abime',0
s_e_io		db 'lecture impossible',0
s_e_nomem	db 'memoire insuffisante',0
s_e_other	db 'lecture impossible (fichier absent, ou pas de son ?)',0
s_paren		db ' [erreur ',0
s_paren2	db ']',10,0
s_decerr	db 'play : erreur pendant la lecture',10,0
s_fmt		db 'format : ',0
s_sep		db ', ',0
s_hz		db ' Hz, ',0
s_ch		db ' canaux',0
s_kbps		db ' kbit/s',0
s_dur		db 'duree  : ',0
s_slash		db ' / ',0
s_end		db 'fin',10,0
s_nl		db 10,0
		align	4

segment readable writeable
kit_data
		align	8
argv	rd PLY_ARGS
info	rb AKI_SIZE
tbuf	rb 64
tagbuf	rb 256
