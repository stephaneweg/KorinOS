; ============================================================================
;  music - lecteur de musique : les fichiers audio d'un dossier (WAV, FLAC,
;  MP3, Ogg), joues par le lecteur d'AudioKit (ak_play, un thread du
;  process). Dossier par defaut SD0:/home/music (vide : SD0:/res/audiotest) ;
;  « Dossier… » : ui_folder_choose. Double-clic ou Lecture : joue ; Pause /
;  Reprise, Arret, Suivant ; glissiere de position (recherche au relache),
;  glissiere de volume ; morceau suivant a la fin.
;  Argument : un dossier (facultatif).
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'

MUS_W		= 600
MUS_H		= 410
MUS_M		= 16
MUS_LIST_Y	= 46
MUS_LIST_H	= 180
MUS_POS_Y	= 330
MUS_BTN_Y	= 366
MUS_BTN_W	= 100
MUS_TICK	= 250			; rafraichissement de la position (ms)
MUS_PATH	= 260

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so

start:
	stp	x0,x1,[sp,-16]!
	bl	kits_open
	ldp	x0,x1,[sp],16
	mov	x19,x0				; argument : un dossier
	adr	x0,ui_win
	bl	ui_init
	mov	x0,MUS_W
	mov	x1,MUS_H
	adr	x2,s_title
	bl	tr2
	bl	ui_window
	tbnz	x0,63,quit
	bl	ui_jetons
	adr	x1,jetons
	str	x0,[x1]
	bl	build
	adr	x0,dir				; dossier : l'argument, ou celui par defaut
	adr	x1,p_music
	cbz	x19,.d
	ldrb	w2,[x19]
	cbz	w2,.d
	mov	x1,x19
.d:	strb	wzr,[x0]
	bl	str_cat
	bl	load_dir
	cbnz	x0,.paint
	cbnz	x19,.paint			; (dossier donne : on le garde)
	adr	x0,dir				; vide : les fichiers d'essai
	strb	wzr,[x0]
	adr	x1,p_test
	bl	str_cat
	bl	load_dir
.paint:	bl	paint
.loop:	bl	maybe_tick
	mov	x0,100
	bl	ui_event
	cbnz	x0,.ev
	bl	ui_w_idle			; (infobulles)
	b	.loop
.ev:	cmp	x0,WEV_CLOSE
	b.eq	quit
	mov	x22,x0				; x22 = type de l'evenement
	mov	x0,0
	bl	ui_w_event
	cbz	x0,.rel
	cbz	x1,.rel
	bl	on_widget			; x0 = widget, x1 = code
.rel:	cmp	x22,WEV_MOUSE			; relache apres un glisser de la position
	b.ne	.loop
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,2
	b.ne	.loop
	adr	x0,dragging
	ldr	x1,[x0]
	cbz	x1,.loop
	str	xzr,[x0]
	bl	seek_slider
	b	.loop

quit:	bl	stop_player
	mov	x0,0
	sys	SYS_EXIT

; --- les widgets -------------------------------------------------------------
build:
	prologue
	mov	x0,MUS_M			; dossier
	mov	x1,14
	mov	x2,MUS_W-2*MUS_M-140-FRAME_CAP_W
	mov	x3,22
	adr	x4,s_empty
	mov	x5,ULB_DIM
	bl	ui_label
	adr	x1,w_dirlbl
	str	x0,[x1]
	mov	x0,MUS_W-MUS_M-120-FRAME_CAP_W	; (a gauche des boutons de la fenetre)
	mov	x1,10
	mov	x2,120
	mov	x3,0
	adr	x4,b_dir
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_dir
	str	x0,[x1]
	mov	x0,MUS_M			; les fichiers
	mov	x1,MUS_LIST_Y
	mov	x2,MUS_W-2*MUS_M
	mov	x3,MUS_LIST_H
	mov	x4,0
	bl	ui_list
	adr	x1,w_list
	str	x0,[x1]
	mov	x0,MUS_M			; titre, artiste, album, format
	mov	x1,MUS_LIST_Y+MUS_LIST_H+10
	mov	x2,MUS_W-2*MUS_M
	mov	x3,22
	adr	x4,s_hint
	mov	x5,ULB_BOLD
	bl	ui_label
	adr	x1,w_ltitle
	str	x0,[x1]
	mov	x0,MUS_M
	mov	x1,MUS_LIST_Y+MUS_LIST_H+34
	mov	x2,MUS_W-2*MUS_M
	mov	x3,20
	adr	x4,s_empty
	mov	x5,0
	bl	ui_label
	adr	x1,w_lartist
	str	x0,[x1]
	mov	x0,MUS_M
	mov	x1,MUS_LIST_Y+MUS_LIST_H+56
	mov	x2,MUS_W-2*MUS_M
	mov	x3,20
	adr	x4,s_empty
	mov	x5,0
	bl	ui_label
	adr	x1,w_lalbum
	str	x0,[x1]
	mov	x0,MUS_M
	mov	x1,MUS_LIST_Y+MUS_LIST_H+78
	mov	x2,MUS_W-2*MUS_M
	mov	x3,20
	adr	x4,s_empty
	mov	x5,ULB_DIM
	bl	ui_label
	adr	x1,w_linfo
	str	x0,[x1]
	mov	x0,MUS_M			; position (secondes)
	mov	x1,MUS_POS_Y
	mov	x2,MUS_W-2*MUS_M-130
	mov	x3,28
	mov	x4,0
	mov	x5,0
	mov	x6,0
	bl	ui_slider
	adr	x1,w_pos
	str	x0,[x1]
	mov	x0,MUS_W-MUS_M-120
	mov	x1,MUS_POS_Y
	mov	x2,120
	mov	x3,28
	adr	x4,s_time0
	mov	x5,ULB_RIGHT
	bl	ui_label
	adr	x1,w_time
	str	x0,[x1]
	mov	x0,MUS_M			; boutons
	mov	x1,MUS_BTN_Y
	mov	x2,MUS_BTN_W
	mov	x3,0
	adr	x4,b_play
	mov	x5,UB_DEFAULT
	bl	ui_pushbutton
	adr	x1,w_play
	str	x0,[x1]
	mov	x0,MUS_M+(MUS_BTN_W+8)
	mov	x1,MUS_BTN_Y
	mov	x2,MUS_BTN_W
	mov	x3,0
	adr	x4,b_pause
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_pause
	str	x0,[x1]
	mov	x0,MUS_M+2*(MUS_BTN_W+8)
	mov	x1,MUS_BTN_Y
	mov	x2,MUS_BTN_W
	mov	x3,0
	adr	x4,b_stop
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_stop
	str	x0,[x1]
	mov	x0,MUS_M+3*(MUS_BTN_W+8)
	mov	x1,MUS_BTN_Y
	mov	x2,MUS_BTN_W
	mov	x3,0
	adr	x4,b_next
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_next
	str	x0,[x1]
	mov	x0,MUS_W-MUS_M-130		; volume (0..100)
	mov	x1,MUS_BTN_Y
	mov	x2,130
	mov	x3,28
	mov	x4,0
	mov	x5,100
	adr	x6,volume
	ldr	x6,[x6]
	bl	ui_slider
	adr	x1,w_vol
	str	x0,[x1]
	adr	x1,s_volume
	bl	ui_w_tip
	epilogue

paint:
	prologue
	mov	x0,0
	mov	x1,0
	mov	x2,MUS_W
	mov	x3,MUS_H
	adr	x4,jetons
	ldr	x4,[x4]
	ldr	w4,[x4,TJ_FACE]
	bl	ui_fill
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	epilogue

; x0 = widget, x1 = code (rendus par ui_w_event)
on_widget:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,w_list			; liste : double-clic, Entree
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.n1
	cmp	x20,UN_ACTIVATE
	b.ne	.out
	mov	x0,x19
	bl	ui_w_get_value
	bl	play_row
	b	.out
.n1:	adr	x0,w_pos			; position : recherche
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.n2
	cmp	x20,UN_CHANGE
	b.ne	.out
	cmp	x22,WEV_MOUSE			; glisser : au relache (boucle principale)
	b.ne	.kseek
	adr	x0,dragging
	mov	x1,1
	str	x1,[x0]
	b	.out
.kseek:	bl	seek_slider			; clavier, molette : tout de suite
	b	.out
.n2:	adr	x0,w_vol			; volume
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.n3
	cmp	x20,UN_CHANGE
	b.ne	.out
	mov	x0,x19
	bl	ui_w_get_value
	adr	x1,volume
	str	x0,[x1]
	bl	apply_volume
	b	.out
.n3:	cmp	x20,UN_ACTIVATE			; les boutons
	b.ne	.out
	adr	x0,w_play
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.n4
	adr	x0,player			; en pause : reprise
	ldr	x0,[x0]
	cbz	x0,.pl
	adr	x0,paused
	ldr	x0,[x0]
	cbz	x0,.pl
	bl	toggle_pause
	b	.out
.pl:	adr	x0,w_list			; sinon l'element choisi (ou le premier)
	ldr	x0,[x0]
	bl	ui_w_get_value
	cmp	x0,0
	csel	x0,xzr,x0,lt
	bl	play_row
	b	.out
.n4:	adr	x0,w_pause
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.n5
	bl	toggle_pause
	b	.out
.n5:	adr	x0,w_stop
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.n6
	bl	stop_player
	b	.out
.n6:	adr	x0,w_next
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.n7
	adr	x0,w_list			; suivant (apres le dernier : le premier)
	ldr	x0,[x0]
	bl	ui_list_count
	cbz	x0,.out
	mov	x2,x0
	adr	x1,cur
	ldr	x0,[x1]
	adr	x1,player
	ldr	x1,[x1]
	cbnz	x1,.nx
	adr	x0,w_list
	ldr	x0,[x0]
	str	x2,[sp,-16]!
	bl	ui_w_get_value
	ldr	x2,[sp],16
.nx:	add	x0,x0,1
	cmp	x0,x2
	csel	x0,xzr,x0,ge
	bl	play_row
	b	.out
.n7:	adr	x0,w_dir
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.out
	adr	x0,dir				; choix d'un dossier
	adr	x1,tmp
	mov	x2,MUS_PATH
	bl	ui_folder_choose
	cbz	x0,.repaint
	bl	stop_player
	adr	x0,dir
	strb	wzr,[x0]
	adr	x1,tmp
	bl	str_cat
	bl	load_dir
.repaint:
	bl	paint
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- le dossier --------------------------------------------------------------
; remplit la liste avec les fichiers audio de « dir » -> x0 = nombre
load_dir:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,w_list
	ldr	x0,[x0]
	bl	ui_list_clear
	adr	x0,tmp				; « Dossier : ... »
	adr	x1,s_dirlbl
	strb	wzr,[x0]
	bl	tr_cat
	adr	x1,dir
	bl	str_cat
	adr	x0,w_dirlbl
	ldr	x0,[x0]
	adr	x1,tmp
	bl	ui_w_set_text
	adr	x0,dir
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.count
	mov	x19,x0
.next:	mov	x0,x19
	adr	x1,dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.close
	adr	x0,dirent
	ldr	x1,[x0,DIRENT_ATTR]
	tst	x1,ATTR_DIR
	b.ne	.next
	adr	x20,exts			; extension connue ?
.ext:	ldrb	w0,[x20]
	cbz	w0,.next
	adr	x0,dirent
	mov	x1,x20
	bl	str_ends
	cbnz	x0,.add
.skip:	ldrb	w0,[x20],1
	cbnz	w0,.skip
	b	.ext
.add:	adr	x0,w_list
	ldr	x0,[x0]
	adr	x1,dirent
	mov	x2,0
	bl	ui_list_add
	b	.next
.close:	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,w_list			; par nom
	ldr	x0,[x0]
	mov	x1,0
	mov	x2,0
	bl	ui_list_sort
.count:	adr	x0,w_list
	ldr	x0,[x0]
	bl	ui_list_count
	mov	x19,x0
	adr	x0,s_hint
	cbnz	x19,.t
	adr	x0,s_nofiles
.t:	bl	ui_tr
	bl	set_title
	adr	x0,w_list
	ldr	x0,[x0]
	mov	x1,0
	cbz	x19,.v
	bl	ui_w_set_value
.v:	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; --- le lecteur ---------------------------------------------------------------
; x0 = numero dans la liste : le joue (hors liste : arret)
play_row:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	stop_player
	tbnz	x19,63,.out
	adr	x0,w_list
	ldr	x0,[x0]
	bl	ui_list_count
	cmp	x19,x0
	b.hs	.out
	adr	x0,w_list
	ldr	x0,[x0]
	mov	x1,x19
	bl	ui_list_get
	mov	x1,x0				; nom du fichier (garde)
	adr	x0,name
	strb	wzr,[x0]
	bl	str_cat
	adr	x0,path
	adr	x1,dir
	adr	x2,name
	bl	path_join
	adr	x0,w_list
	ldr	x0,[x0]
	mov	x1,x19
	bl	ui_w_set_value
	adr	x0,path
	mov	x1,0
	bl	ak_play
	tbnz	x0,63,.err
	adr	x1,player
	str	x0,[x1]
	mov	x20,x0
	adr	x1,cur
	str	x19,[x1]
	adr	x1,paused
	str	xzr,[x1]
	adr	x1,last_sec
	mov	x2,-1
	str	x2,[x1]
	bl	apply_volume
	adr	x0,w_pause
	ldr	x0,[x0]
	adr	x1,b_pause
	bl	tr1
	bl	ui_w_set_text
	mov	x0,x20				; format, etiquettes
	bl	ak_play_decoder
	mov	x21,x0
	adr	x1,info
	bl	ak_dec_info
	mov	x0,x21				; titre (sinon le nom du fichier)
	mov	x1,AKT_TITLE
	adr	x2,tagbuf
	mov	x3,256
	bl	ak_dec_tag
	adr	x1,tagbuf
	cmp	x0,0
	b.gt	.ti
	adr	x1,name
.ti:	mov	x0,x1
	bl	set_title
	mov	x0,x21
	mov	x1,AKT_ARTIST
	adr	x2,w_lartist
	bl	show_tag
	mov	x0,x21
	mov	x1,AKT_ALBUM
	adr	x2,w_lalbum
	bl	show_tag
	bl	show_info
	adr	x0,info				; glissiere : 0..duree (s)
	ldr	x2,[x0,AKI_DURATION]
	cmp	x2,0
	csel	x2,xzr,x2,lt
	mov	x1,1000
	udiv	x2,x2,x1
	adr	x0,w_pos
	ldr	x0,[x0]
	mov	x1,0
	mov	x3,0
	mov	x4,1
	bl	ui_w_set_range
	adr	x0,w_pos
	ldr	x0,[x0]
	mov	x1,0
	bl	ui_w_set_value
	bl	update_pos
	b	.out
.err:	mov	x19,x0				; erreur d'ak_play
	adr	x0,s_e_other
	cmn	x19,-AKE_FORMAT
	b.ne	.e2
	adr	x0,s_e_format
	b	.e3
.e2:	cmn	x19,-AKE_CORRUPT
	b.ne	.e3
	adr	x0,s_e_corrupt
.e3:	bl	ui_tr
	bl	set_title
	adr	x0,w_lartist
	ldr	x0,[x0]
	adr	x1,name
	bl	ui_w_set_text
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = AKT_*, x2 = &etiquette : l'etiquette (ou rien)
show_tag:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x2
	adr	x2,tagbuf
	mov	x3,256
	bl	ak_dec_tag
	adr	x1,tagbuf
	cmp	x0,0
	b.gt	.s
	adr	x1,s_empty
.s:	ldr	x0,[x19]
	bl	ui_w_set_text
	ldr	x19,[sp],16
	epilogue

; « MP3, 44100 Hz, 2 canaux, 128 kbit/s, 03:25 » d'apres info
show_info:
	prologue
	str	x19,[sp,-16]!
	adr	x1,info
	ldr	x0,[x1,AKI_FORMAT]
	cmp	x0,AKF_FMS
	csel	x0,xzr,x0,hi
	adr	x1,fmt_names
	ldr	x2,[x1,x0,lsl 3]
	add	x1,x1,x2
	adr	x0,tmp
	strb	wzr,[x0]
	bl	str_cat
	adr	x1,s_sep
	bl	str_cat
	mov	x1,x0
	adr	x2,info
	ldr	x0,[x2,AKI_RATE]
	bl	fmt_dec
	adr	x1,s_hz
	bl	str_cat
	mov	x1,x0
	adr	x2,info
	ldr	x0,[x2,AKI_CHANNELS]
	bl	fmt_dec
	adr	x1,s_ch
	bl	tr_cat
	mov	x19,x0
	adr	x2,info
	ldr	x0,[x2,AKI_BITRATE]
	cbz	x0,.dur
	mov	x1,1000
	udiv	x0,x0,x1
	mov	x1,x19
	bl	fmt_dec
	adr	x1,s_kbps
	bl	str_cat
	mov	x19,x0
.dur:	adr	x2,info
	ldr	x0,[x2,AKI_DURATION]
	mov	x1,x19
	bl	fmt_time
	adr	x0,w_linfo
	ldr	x0,[x0]
	adr	x1,tmp
	bl	ui_w_set_text
	ldr	x19,[sp],16
	epilogue

; x0 = texte de l'etiquette du titre
set_title:
	prologue
	mov	x1,x0
	adr	x0,w_ltitle
	ldr	x0,[x0]
	bl	ui_w_set_text
	epilogue

stop_player:
	prologue
	adr	x1,player
	ldr	x0,[x1]
	cbz	x0,.out
	str	xzr,[x1]
	bl	ak_play_stop
	adr	x0,dragging
	str	xzr,[x0]
	adr	x0,w_pos
	ldr	x0,[x0]
	mov	x1,0
	bl	ui_w_set_value
	adr	x0,w_time
	ldr	x0,[x0]
	adr	x1,s_time0
	bl	ui_w_set_text
	adr	x0,w_pause
	ldr	x0,[x0]
	adr	x1,b_pause
	bl	tr1
	bl	ui_w_set_text
.out:	epilogue

toggle_pause:
	prologue
	adr	x0,player
	ldr	x0,[x0]
	cbz	x0,.out
	adr	x2,paused
	ldr	x1,[x2]
	eor	x1,x1,1
	str	x1,[x2]
	bl	ak_play_pause
	adr	x1,b_pause
	adr	x0,paused
	ldr	x0,[x0]
	cbz	x0,.t
	adr	x1,b_resume
.t:	adr	x0,w_pause
	ldr	x0,[x0]
	bl	tr1
	bl	ui_w_set_text
.out:	epilogue

; x0 = destination, x1 = morceau de texte -> x0 = fin : str_cat traduit. Les espaces autour du
; morceau restent ici (un dictionnaire ne garde pas ceux d'une cle).
tr_cat:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	sub	sp,sp,240
	mov	x19,x0
	mov	x20,x1
.e:	ldrb	w0,[x20]			; espaces de tete
	cmp	w0,' '
	b.ne	.d
	strb	w0,[x19],1
	add	x20,x20,1
	b	.e
.d:	mov	x21,0				; longueur sans ceux de queue
	mov	x2,0
.c:	ldrb	w0,[x20,x2]
	strb	w0,[sp,x2]
	cbz	w0,.f
	add	x2,x2,1
	cmp	w0,' '
	csel	x21,x21,x2,eq
	cmp	x2,239
	b.lo	.c
.f:	strb	wzr,[sp,x21]
	add	x20,x20,x21
	mov	x0,sp
	bl	ui_tr
	mov	x1,x0
	mov	x0,x19
	bl	str_cat
.q:	ldrb	w1,[x20],1
	strb	w1,[x0],1
	cbnz	w1,.q
	sub	x0,x0,1
	add	sp,sp,240
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> sa traduction ; x1 a x7 gardes
tr0:
	prologue
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	stp	x5,x6,[sp,-16]!
	str	x7,[sp,-16]!
	bl	ui_tr
	ldr	x7,[sp],16
	ldp	x5,x6,[sp],16
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	epilogue

; x1 = texte -> sa traduction ; les autres registres gardes
tr1:
	prologue
	str	x0,[sp,-16]!
	mov	x0,x1
	bl	tr0
	mov	x1,x0
	ldr	x0,[sp],16
	epilogue

; x2 = texte -> sa traduction ; les autres registres gardes
tr2:
	prologue
	str	x0,[sp,-16]!
	mov	x0,x2
	bl	tr0
	mov	x2,x0
	ldr	x0,[sp],16
	epilogue

; volume de la glissiere (0..100, courbe a l'oreille) -> lecteur (0..256)
apply_volume:
	prologue
	adr	x0,player
	ldr	x0,[x0]
	cbz	x0,.out
	adr	x0,volume
	ldr	x0,[x0]
	bl	ak_volume_curve
	lsr	x1,x0,8
	adr	x0,player
	ldr	x0,[x0]
	bl	ak_play_volume
.out:	epilogue

; position de la glissiere -> ak_play_seek
seek_slider:
	prologue
	adr	x0,player
	ldr	x0,[x0]
	cbz	x0,.out
	adr	x0,w_pos
	ldr	x0,[x0]
	bl	ui_w_get_value
	mov	x1,1000
	mul	x1,x0,x1
	adr	x0,player
	ldr	x0,[x0]
	bl	ak_play_seek
	adr	x0,last_sec
	mov	x1,-1
	str	x1,[x0]
.out:	epilogue

; toutes les MUS_TICK ms : fin du morceau, position
maybe_tick:
	prologue
	sys	SYS_TIME_MS
	adr	x1,last_tick
	ldr	x2,[x1]
	sub	x2,x0,x2
	cmp	x2,MUS_TICK
	b.lo	.out
	str	x0,[x1]
	adr	x0,player
	ldr	x0,[x0]
	cbz	x0,.out
	bl	ak_play_state
	cmp	x0,AKP_ENDED
	b.eq	.ended
	cmp	x0,AKP_ERROR
	b.eq	.error
	bl	update_pos
	b	.out
.ended:	adr	x0,cur				; le suivant (apres le dernier : arret)
	ldr	x0,[x0]
	add	x0,x0,1
	bl	play_row
	b	.out
.error:	bl	stop_player
	adr	x0,s_e_play
	bl	ui_tr
	bl	set_title
.out:	epilogue

; position entendue -> glissiere et « mm:ss / mm:ss » (une fois par seconde)
update_pos:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,dragging			; pendant un glisser : rien
	ldr	x0,[x0]
	cbnz	x0,.out
	adr	x0,player
	ldr	x0,[x0]
	cbz	x0,.out
	bl	ak_play_pos
	mov	x19,x0
	mov	x1,1000
	udiv	x20,x19,x1
	adr	x1,last_sec
	ldr	x2,[x1]
	cmp	x2,x20
	b.eq	.out
	str	x20,[x1]
	adr	x0,w_pos
	ldr	x0,[x0]
	mov	x1,x20
	bl	ui_w_set_value
	mov	x0,x19
	adr	x1,tbuf
	bl	fmt_time
	adr	x1,s_slash
	bl	str_cat
	mov	x1,x0
	adr	x2,info
	ldr	x0,[x2,AKI_DURATION]
	bl	fmt_time
	adr	x0,w_time
	ldr	x0,[x0]
	adr	x1,tbuf
	bl	ui_w_set_text
.out:	ldp	x19,x20,[sp],16
	epilogue

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

segment readable writeable
kit_data
		align 8
jetons		dd 0
w_dirlbl	dd 0
w_dir		dd 0
w_list		dd 0
w_ltitle	dd 0
w_lartist	dd 0
w_lalbum	dd 0
w_linfo		dd 0
w_pos		dd 0
w_time		dd 0
w_play		dd 0
w_pause		dd 0
w_stop		dd 0
w_next		dd 0
w_vol		dd 0
player		dd 0
cur		dd 0
paused		dd 0
dragging	dd 0
volume		dd 80
last_sec	dd -1
last_tick	dd 0
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
s_title		db 'Musique',0
p_music		db 'SD0:/home/music',0
p_test		db 'SD0:/res/audiotest',0
exts		db '.wav',0,'.flac',0,'.mp3',0,'.ogg',0,'.oga',0,'.opus',0,'.mid',0,'.midi',0,'.fms',0,0
s_empty		db 0
s_dirlbl	db 'Dossier : ',0
s_hint		db 'Double-cliquez un morceau pour l',0x27,0xC3,0xA9,'couter',0
s_nofiles	db 'Aucun fichier audio dans ce dossier (WAV, FLAC, MP3, Ogg, MIDI, FM Song)',0
s_e_format	db 'Format non reconnu ou non pris en charge (Ogg Opus : pas encore)',0
s_e_corrupt	db 'Fichier invalide ou ab',0xC3,0xAE,'m',0xC3,0xA9,0
s_e_other	db 'Lecture impossible',0
s_e_play	db 'Erreur pendant la lecture',0
s_time0		db '00:00 / 00:00',0
s_slash		db ' / ',0
s_sep		db ', ',0
s_hz		db ' Hz, ',0
s_ch		db ' canaux, ',0
s_kbps		db ' kbit/s, ',0
s_volume	db 'Volume',0
b_dir		db 'Dossier',0xE2,0x80,0xA6,0
b_play		db 'Lecture',0
b_pause		db 'Pause',0
b_resume	db 'Reprise',0
b_stop		db 'Arr',0xC3,0xAA,'t',0
b_next		db 'Suivant',0
		align 8
info		rb AKI_SIZE
dir		rb MUS_PATH
path		rb MUS_PATH*2
tmp		rb MUS_PATH*2
name		rb MUS_PATH
tbuf		rb 64
tagbuf		rb 256
dirent		rb DIRENT_BYTES
ui_data
