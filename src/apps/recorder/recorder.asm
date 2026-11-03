; magneto - magnetophone : enregistrer (micro ou sortie du mixeur), ecouter,
; sauver (SD0:/enregistrement.wav, .mp3 ou .opus : format choisi dans la
; liste deroulante, encodeurs d'AudioKit ak_enc_*), vumetre.
; Argument "--test [source] [wav|mp3|opus]" : enregistre 2,5 s de la sortie
; principale (boucle, ou la source donnee : 256 + n boucle de la sortie n,
; 512 + n entree n ; docs/SON.md 4.2), sauve (WAV par defaut) et quitte
; (test automatique). Opus n'est propose que si l'encodeur d'AudioKit existe.
include '../../inc/armpi.inc'
include '../../inc/sound.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'

WIN_W	= 540
WIN_H	= 250
MAX_SEC	= 30
BUF_MAX	= SND_RATE * 4 * MAX_SEC
BTN_Y	= 150
BTN_H	= 34
METER_Y	= 104

RCD_MP3_BPS	= 128000		; debit du MP3 (bits/s)
RCD_CHUNK	= 4096			; trames par ak_enc_write
RCD_FMT_WAV	= 0			; formats : element de la liste deroulante
RCD_FMT_MP3	= 1
RCD_FMT_OPUS	= 2

ST_IDLE	= 0
ST_REC	= 1
ST_PLAY	= 2

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	mov	x19,x0				; arguments
	movl	x0,BUF_MAX
	bl	malloc
	adr	x1,buf
	str	x0,[x1]
	cbz	x0,quit
	bl	probe_opus
	cbz	x19,.gui
	ldrb	w0,[x19]			; "--test" ?
	cmp	w0,'-'
	b.eq	auto_test
.gui:	adr	x0,gui
	mov	x1,1
	str	x1,[x0]
	adr	x0,ui_win				; variables de l'interface
	bl	ui_init
	mov	x0,WIN_W
	mov	x1,WIN_H
	adr	x2,title
	bl	tr_x2
	bl	ui_window
	tbnz	x0,63,quit
	bl	build_fmt
	bl	draw
.loop:	bl	tick
	mov	x0,20
	bl	ui_event
	cbz	x0,.loop
	cmp	x0,WEV_CLOSE
	b.eq	quit
	mov	x21,x0				; (type de l'evenement)
	mov	x0,0
	bl	ui_w_event			; le widget du format d'abord
	cbz	x2,.nw
	adr	x2,w_fmt
	ldr	x2,[x2]
	cmp	x0,x2
	b.ne	.loop
	cmp	x1,UN_CHANGE
	b.ne	.loop
	bl	ui_w_get_value
	tbnz	x0,63,.loop
	adr	x1,fmt
	str	x0,[x1]
	b	.loop
.nw:	cmp	x21,WEV_MOUSE
	b.ne	.loop
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,1
	b.ne	.loop
	mov	x20,0				; quel bouton ?
.b:	cmp	x20,5
	b.hs	.loop
	mov	x0,20
	mov	x1,104
	mul	x1,x1,x20
	add	x0,x0,x1
	mov	x1,BTN_Y
	mov	x2,96
	mov	x3,BTN_H
	bl	ui_hit
	cbnz	x0,.hit
	add	x20,x20,1
	b	.b
.hit:	cmp	x20,0
	b.eq	.rec
	cmp	x20,1
	b.eq	.src
	cmp	x20,2
	b.eq	.stop
	cmp	x20,3
	b.eq	.play
	adr	x0,msg				; l'encodage peut prendre un moment
	adr	x1,s_encoding
	str	x1,[x0]
	bl	draw
	bl	save_rec
	b	.redraw
.rec:	bl	start_rec
	b	.redraw
.src:	adr	x0,source
	ldr	x1,[x0]
	eor	x1,x1,1
	str	x1,[x0]
	b	.redraw
.stop:	bl	stop_all
	b	.redraw
.play:	bl	start_play
.redraw:
	bl	draw
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; --- mode test : boucle 2,5 s -> SD0:/enregistrement.wav (.mp3, .opus) -------
auto_test:
	mov	x0,x19				; format : dernier mot (wav, mp3, opus)
	bl	last_word
	mov	x20,x0
	adr	x1,w_mp3
	bl	word_is
	mov	x2,RCD_FMT_MP3
	cbnz	x0,.setf
	mov	x0,x20
	adr	x1,w_opus
	bl	word_is
	mov	x2,RCD_FMT_OPUS
	cbnz	x0,.setf
	mov	x2,RCD_FMT_WAV
.setf:	adr	x0,fmt
	str	x2,[x0]
	adr	x0,m_fmt
	bl	puts
	adr	x0,fmt
	ldr	x0,[x0]
	adr	x1,fmt_names
	ldr	x0,[x1,x0,lsl 3]
	bl	puts
	adr	x0,m_opus
	bl	puts
	adr	x0,opus_ok
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,fmt
	ldr	x0,[x0]
	cmp	x0,RCD_FMT_OPUS
	b.ne	.fmtok
	adr	x0,opus_ok
	ldr	x0,[x0]
	cbnz	x0,.fmtok
	adr	x0,m_noopus			; encodeur Opus absent : on s'arrete
	bl	puts
	b	quit
.fmtok:
	mov	x0,REC_SRC_MIC			; sans micro : E_NOSYS attendu sous QEMU
	bl	ak_rec_open
	mov	x20,x0
	adr	x0,m_mic
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	tbnz	x20,63,.loopsrc
	mov	x0,x20
	bl	ak_rec_close
.loopsrc:
	mov	x0,1500				; laisser sontest jouer
	sys	SYS_SLEEP
	mov	x1,REC_SRC_LOOP			; source donnee apres --test ?
	add	x0,x19,6
.sp:	ldrb	w2,[x0]
	cmp	w2,' '
	b.ne	.num
	add	x0,x0,1
	b	.sp
.num:	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.src
	bl	parse_dec
	mov	x1,x0
.src:	adr	x0,source
	str	x1,[x0]
	bl	start_rec
	adr	x0,state
	ldr	x0,[x0]
	cmp	x0,ST_REC
	b.ne	quit
	sys	SYS_TIME_MS
	add	x21,x0,2500
.t:	bl	tick
	mov	x0,20
	sys	SYS_SLEEP
	sys	SYS_TIME_MS
	cmp	x0,x21
	b.lo	.t
	bl	stop_all
	bl	save_rec
	adr	x0,m_saved
	bl	puts
	adr	x0,len
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_peak
	bl	puts
	adr	x0,peak_max
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,m_head			; « Sauve dans ... » ou l'erreur
	bl	puts
	adr	x0,msg
	ldr	x0,[x0]
	bl	puts
	adr	x0,m_size
	bl	puts
	adr	x0,path				; taille du fichier
	adr	x1,fstat_buf
	bl	f_stat
	mov	x1,x0
	adr	x0,fstat_buf
	ldr	x0,[x0]
	tbz	x1,63,.sz
	mov	x0,x1
.sz:	bl	print_dec
	adr	x0,m_nl
	bl	puts
	b	quit

; x0 = chaine -> x0 = debut de son dernier mot (fin : chaine vide)
last_word:
	mov	x1,x0
	mov	w3,' '
.l:	ldrb	w2,[x0]
	cbz	w2,.e
	cmp	w3,' '
	b.hi	.n
	cmp	w2,' '
	b.ls	.n
	mov	x1,x0				; un mot commence
.n:	mov	w3,w2
	add	x0,x0,1
	b	.l
.e:	mov	x0,x1
	ret

; x0 = mot (fini par NUL ou un blanc), x1 = mot attendu -> x0 = 1 si egaux
word_is:
.l:	ldrb	w2,[x1],1
	ldrb	w3,[x0],1
	cbz	w2,.end
	cmp	w2,w3
	b.eq	.l
	mov	x0,0
	ret
.end:	cmp	w3,' '
	cset	x0,ls
	ret

; l'encodeur Opus d'AudioKit existe-t-il ? (essai sans fichier) -> opus_ok
probe_opus:
	prologue
	adr	x0,null_write
	mov	x1,0
	mov	x2,0
	mov	x3,AKF_OPUS
	movl	x4,SND_RATE
	mov	x5,2
	mov	x6,0
	bl	ak_enc_open_io
	tbnz	x0,63,.out			; AKE_FORMAT : pas encore
	bl	ak_enc_close
	adr	x0,opus_ok
	mov	x1,1
	str	x1,[x0]
.out:	epilogue

; ecriture de l'essai : (ctx, tampon, n) -> n, rien n'est garde
null_write:
	mov	x0,x2
	ret

; la liste deroulante du format (en haut a droite)
build_fmt:
	prologue
	adr	x4,dd_fmt2
	adr	x0,opus_ok
	ldr	x0,[x0]
	cbz	x0,.d
	adr	x4,dd_fmt3
.d:	mov	x0,WIN_W-20-170-FRAME_CAP_W	; (a gauche des boutons de la fenetre)
	mov	x1,14
	mov	x2,170
	mov	x3,0
	bl	ui_dropdown
	adr	x1,w_fmt
	str	x0,[x1]
	cbz	x0,.out
	adr	x1,fmt
	ldr	x1,[x1]
	bl	ui_w_set_value
.out:	epilogue

; Demarre l'enregistrement depuis la source choisie
start_rec:
	prologue
	bl	stop_all
	adr	x0,source
	ldr	x0,[x0]
	bl	ak_rec_open
	adr	x1,msg
	tbz	x0,63,.ok
	adr	x2,s_nomic			; E_NOSYS : pas de micro
	cmn	x0,38
	b.eq	.set
	adr	x2,s_busy
.set:	str	x2,[x1]
	b	.out
.ok:	adr	x2,rec
	str	x0,[x2]
	adr	x2,s_recording
	str	x2,[x1]
	adr	x0,len
	str	xzr,[x0]
	adr	x0,peak_max
	str	xzr,[x0]
	adr	x0,state
	mov	x1,ST_REC
	str	x1,[x0]
.out:	epilogue

stop_all:
	prologue
	str	x19,[sp,-16]!
	adr	x0,state
	ldr	x19,[x0]
	str	xzr,[x0]
	cmp	x19,ST_REC
	b.ne	.p
	adr	x0,rec
	ldr	x0,[x0]
	bl	ak_rec_close
	adr	x0,msg
	adr	x1,s_stopped
	str	x1,[x0]
.p:	cmp	x19,ST_PLAY
	b.ne	.out
	adr	x0,stream
	ldr	x0,[x0]
	bl	ak_close
.out:	ldr	x19,[sp],16
	epilogue

start_play:
	prologue
	bl	stop_all
	adr	x0,len
	ldr	x0,[x0]
	cbz	x0,.out
	movl	x0,SND_RATE
	mov	x1,2
	mov	x2,16
	mov	x3,0
	bl	ak_open
	tbnz	x0,63,.out
	adr	x1,stream
	str	x0,[x1]
	adr	x0,ppos
	str	xzr,[x0]
	adr	x0,state
	mov	x1,ST_PLAY
	str	x1,[x0]
	adr	x0,msg
	adr	x1,s_playing
	str	x1,[x0]
.out:	epilogue

; Travail periodique : lire la capture / alimenter la lecture
tick:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,state
	ldr	x0,[x0]
	cmp	x0,ST_REC
	b.eq	.rec
	cmp	x0,ST_PLAY
	b.eq	.play
	b	.out
.rec:	adr	x0,len
	ldr	x19,[x0]
	movl	x2,BUF_MAX
	sub	x2,x2,x19
	cbz	x2,.full
	adr	x1,buf
	ldr	x1,[x1]
	add	x1,x1,x19
	mov	x20,x1
	adr	x0,rec
	ldr	x0,[x0]
	bl	ak_rec_read
	cmp	x0,0
	b.le	.meter
	add	x19,x19,x0
	adr	x1,len
	str	x19,[x1]
	mov	x1,x0				; crete des echantillons lus
	mov	x0,x20
	bl	peak_of
	adr	x1,peak
	str	x0,[x1]
	adr	x1,peak_max
	ldr	x2,[x1]
	cmp	x0,x2
	csel	x0,x0,x2,hi
	str	x0,[x1]
.meter:	bl	draw_meter
	b	.out
.full:	bl	stop_all
	bl	draw
	b	.out
.play:	adr	x0,ppos
	ldr	x19,[x0]
	adr	x0,len
	ldr	x2,[x0]
	sub	x2,x2,x19
	cbz	x2,.ended
	adr	x1,buf
	ldr	x1,[x1]
	add	x1,x1,x19
	mov	x20,x1
	adr	x0,stream
	ldr	x0,[x0]
	bl	ak_write
	cmp	x0,0
	b.le	.out
	add	x19,x19,x0
	adr	x1,ppos
	str	x19,[x1]
	mov	x1,x0
	mov	x0,x20
	bl	peak_of
	adr	x1,peak
	str	x0,[x1]
	bl	draw_meter
	b	.out
.ended:	adr	x0,stream			; tout est ecrit : fin quand vide
	ldr	x0,[x0]
	sub	sp,sp,32
	mov	x1,sp
	bl	ak_status
	ldr	x0,[sp]
	add	sp,sp,32
	cbnz	x0,.out
	bl	stop_all
	adr	x0,msg
	adr	x1,s_stopped
	str	x1,[x0]
	bl	draw
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = echantillons s16, x1 = octets -> x0 = crete absolue
peak_of:
	mov	x2,0
	lsr	x1,x1,1
	mov	x3,0
.p:	cmp	x3,x1
	b.hs	.e
	ldrsh	w4,[x0,x3,lsl 1]
	cmp	w4,0
	cneg	w4,w4,lt
	cmp	x4,x2
	csel	x2,x4,x2,hi
	add	x3,x3,1
	b	.p
.e:	mov	x0,x2
	ret

; Encode l'enregistrement (PCM s16 stereo 44,1 kHz) au format choisi :
; SD0:/enregistrement.wav, .mp3 (128 kbit/s) ou .opus (ak_enc_*) ; msg
save_rec:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,fmt				; chemin : base + extension du format
	ldr	x21,[x0]
	adr	x0,path
	strb	wzr,[x0]
	adr	x1,p_base
	bl	str_cat
	adr	x1,fmt_exts
	ldr	x1,[x1,x21,lsl 3]
	bl	str_cat
	adr	x0,path
	adr	x1,fmt_akf
	ldr	x1,[x1,x21,lsl 3]
	movl	x2,SND_RATE
	mov	x3,2
	adr	x4,fmt_bps
	ldr	x4,[x4,x21,lsl 3]
	bl	ak_enc_open
	tbnz	x0,63,.fail
	mov	x19,x0				; x19 = encodeur
	adr	x0,len
	ldr	x20,[x0]
	lsr	x20,x20,2			; trames
	adr	x0,buf
	ldr	x21,[x0]
.w:	cbz	x20,.close
	mov	x22,RCD_CHUNK
	cmp	x20,x22
	csel	x22,x20,x22,lo
	mov	x0,x19
	mov	x1,x21
	mov	x2,x22
	bl	ak_enc_write
	tbnz	x0,63,.wfail
	sub	x20,x20,x22
	add	x21,x21,x22,lsl 2
	b	.w
.wfail:	mov	x0,x19
	bl	ak_enc_close
	b	.fail
.close:	mov	x0,x19
	bl	ak_enc_close
	tbnz	x0,63,.fail
	adr	x0,msgbuf			; « Sauve dans <chemin> »
	strb	wzr,[x0]
	adr	x1,s_saved
	bl	tr_x1
	bl	str_cat
	adr	x1,path
	bl	str_cat
	adr	x0,msg
	adr	x1,msgbuf
	str	x1,[x0]
	b	.out
.fail:	adr	x0,msg
	adr	x1,s_savefail
	str	x1,[x0]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,gui				; pas de fenetre (mode test)
	ldr	x0,[x0]
	cbz	x0,.nogui
	mov	x0,0
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,WIN_H
	jeton	w4,TJ_FACE
	bl	ui_fill
	adr	x0,s_head
	bl	tr_x0
	mov	x1,20
	mov	x2,34
	jeton	w3,TJ_TEXTE1
	mov	x4,20
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	adr	x0,s_src_mic			; source
	bl	tr_x0
	adr	x1,source
	ldr	x1,[x1]
	cbz	x1,.s
	adr	x0,s_src_loop
	bl	tr_x0
.s:	mov	x1,20
	mov	x2,62
	jeton	w3,TJ_TEXTE2
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	adr	x0,msg
	ldr	x0,[x0]
	bl	tr_x0
	mov	x1,20
	mov	x2,86
	jeton	w3,TJ_TEXTE1
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	adr	x0,s_format			; « Format » devant la liste deroulante
	bl	tr_x0
	mov	x1,WIN_W-20-170-62-FRAME_CAP_W
	mov	x2,36
	jeton	w3,TJ_TEXTE2
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x19,0				; boutons
.b:	cmp	x19,5
	b.hs	.m
	mov	x0,20
	mov	x1,104
	mul	x1,x1,x19
	add	x0,x0,x1
	mov	x1,BTN_Y
	mov	x2,96
	mov	x3,BTN_H
	adr	x4,btn_labels
	ldr	x4,[x4,x19,lsl 3]
	bl	tr_x4
	cmp	x19,0
	cset	x5,eq
	bl	ui_button
	add	x19,x19,1
	b	.b
.m:	mov	x0,0				; widgets (format)
	bl	ui_w_draw
	bl	draw_meter
	bl	ui_update_all
.nogui:	ldp	x19,x20,[sp],16
	epilogue

; vumetre + duree
draw_meter:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	adr	x0,gui
	ldr	x0,[x0]
	cbz	x0,.nogui
	mov	x0,20
	mov	x1,METER_Y
	mov	x2,WIN_W-40
	mov	x3,16
	jeton	w4,TJ_SURFACE
	bl	ui_fill
	mov	x0,20
	mov	x1,METER_Y
	mov	x2,WIN_W-40
	mov	x3,16
	jeton	w4,TJ_LIGNE_FORTE
	bl	ui_frame
	adr	x0,peak				; largeur = crete * (W-44) / 32768
	ldr	x0,[x0]
	mov	x1,WIN_W-44
	mul	x0,x0,x1
	lsr	x2,x0,15
	mov	x0,22
	mov	x1,METER_Y+2
	mov	x3,12
	movl	w4,0x0040B060
	bl	ui_fill
	mov	x0,0				; duree enregistree
	mov	x1,BTN_Y+BTN_H+8
	mov	x2,WIN_W
	mov	x3,30
	jeton	w4,TJ_FACE
	bl	ui_fill
	adr	x0,len
	ldr	x0,[x0]
	movl	x1,SND_RATE*4/10		; dixiemes de seconde
	udiv	x19,x0,x1
	mov	x1,10
	udiv	x0,x19,x1
	mov	x1,sp
	bl	fmt_dec
	mov	w1,','
	strb	w1,[x0],1
	mov	x1,10
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19
	add	w2,w2,'0'
	strb	w2,[x0],1
	adr	x1,s_sec
	bl	str_cat
	mov	x0,sp
	mov	x1,20
	mov	x2,BTN_Y+BTN_H+28
	jeton	w3,TJ_TEXTE2
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x0,0
	mov	x1,METER_Y
	mov	x2,WIN_W
	mov	x3,WIN_H-METER_Y
	bl	ui_update
.nogui:	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue


include '../../inc/tr.inc'

segment readable writeable
kit_data

title		db 'Magn',0xC3,0xA9,'tophone',0
s_head		db 'Magn',0xC3,0xA9,'tophone',0
s_src_mic	db 'Source : micro (USB ou I2S)',0
s_src_loop	db 'Source : sortie du mixeur (boucle)',0
s_ready		db 'Pr',0xC3,0xAA,'t.',0
s_recording	db 'Enregistrement en cours...',0
s_stopped	db 'Arr',0xC3,0xAA,'t',0xC3,0xA9,'.',0
s_playing	db 'Lecture...',0
s_saved		db 'Sauv',0xC3,0xA9,' dans ',0
s_encoding	db 'Encodage et ',0xC3,0xA9,'criture...',0
s_format	db 'Format',0
dd_fmt2		db 'WAV|MP3 128 kbit/s',0
dd_fmt3		db 'WAV|MP3 128 kbit/s|Opus',0
p_base		db 'SD0:/enregistrement',0
e_wav		db '.wav',0
e_mp3		db '.mp3',0
e_opus		db '.opus',0
n_wav		db 'WAV',0
n_mp3		db 'MP3',0
n_opus		db 'Opus',0
w_mp3		db 'mp3',0
w_opus		db 'opus',0
s_savefail	db 'Erreur d',0x27,0xC3,0xA9,'criture du fichier',0
s_nomic		db 'Aucun micro d',0xC3,0xA9,'tect',0xC3,0xA9,' (essayez la source ',0xC2,0xAB,' boucle ',0xC2,0xBB,')',0
s_busy		db 'Enregistreur d',0xC3,0xA9,'j',0xC3,0xA0,' utilis',0xC3,0xA9,0
s_sec		db ' s',0
b_rec		db 'Enreg.',0			; (« Enregistrer » : Save dans res/lang)
b_src		db 'Source',0
b_stop		db 'Arr',0xC3,0xAA,'t',0
b_play		db 0xC3,0x89,'couter',0
b_save		db 'Sauver',0
m_mic		db 'magneto : ak_rec_open(micro) = ',0
m_saved		db 'magneto : enregistrement sauve, octets ',0
m_peak		db ', crete ',0
m_fmt		db 'magneto : format ',0
m_opus		db ', encodeur Opus ',0
m_noopus	db 'magneto : encodeur Opus absent (AKE_FORMAT), rien enregistre',10,0
m_head		db 'magneto : ',0
m_size		db ', fichier (octets) ',0
m_nl		db 10,0
		align 8
btn_labels	dd b_rec, b_src, b_stop, b_play, b_save
fmt_akf		dd AKF_WAV, AKF_MP3, AKF_OPUS
fmt_bps		dd 0, RCD_MP3_BPS, 0
fmt_exts	dd e_wav, e_mp3, e_opus
fmt_names	dd n_wav, n_mp3, n_opus
fmt		dd RCD_FMT_WAV
opus_ok		dd 0
w_fmt		dd 0
fstat_buf	dd 0, 0
msg		dd s_ready
buf		dd 0
len		dd 0
ppos		dd 0
state		dd 0
gui		dd 0
source		dd 0
stream		dd 0
rec		dd 0
peak		dd 0
peak_max	dd 0
path		rb 64
msgbuf		rb 128
ui_data
