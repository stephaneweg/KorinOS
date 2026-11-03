; ============================================================================
;  mediaplayer - lecteur multimedia (video et son) : MediaKit (SD0:/lib/
;  mediakit.so : demultiplexage, VP8 / VP9 / AV1, son Opus en secours) pour
;  les images et le son, la surface attachee d'Elegant pour l'image (aucune
;  recopie : MediaKit convertit et met a l'echelle directement dans le tampon
;  partage qu'Elegant compose), AudioKit pour le son (flux PCM s16 stereo
;  44,1 kHz), synchronisation sur l'horloge du son (celle du systeme sans
;  son). Interface UIKit : fenetre redimensionnable, barre de lecture
;  (lecture / pause, ouvrir, position, temps, volume), menus, raccourcis.
;
;	mediaplayer [fichier] [loop] [autoexit]
;
;  loop : recommence a la fin ; autoexit : quitte a la fin (essais) ; a la fin
;  de chaque lecture, le bilan (images montrees, sautees, images/s) est ecrit
;  sur la sortie standard. MediaKit absent : message, le reste marche.
;  Raccourcis : Espace lecture / pause, gauche / droite -5 / +5 s, Ctrl+O,
;  Ctrl+W.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/mediakit/mediakit.inc'

MP_WIN_W	= 680
MP_WIN_H	= 440
MP_BAR_H	= 48				; barre de lecture
MP_RATE		= 44100				; son : le format des flux de sound
MP_CHUNK	= 1024				; trames par lecture de son
MP_UI_MS	= 250				; temps et position : mis a jour
MP_LATE_US	= 150000			; image en retard de plus : sautee
MP_MAX_DROP	= 4				; sautees de suite au plus
MP_STALL_MS	= 1500				; son qui n'avance plus : horloge du systeme
MP_TAIL_BYTES	= 16384				; fin : ce qui reste dans le flux (~90 ms)
MP_MK_FNS	= 15				; fonctions de MediaKit utilisees (version)
MP_ID_OPEN	= 1
MP_ID_CLOSE	= 2
MP_ID_PLAY	= 3
MP_ID_START	= 4
; rangs de la table de MediaKit (src/kits/mediakit/mediakit.abi)
MKN_OPEN	= 1
MKN_CLOSE	= 2
MKN_INFO	= 3
MKN_VNEXT	= 6
MKN_VCONV	= 7
MKN_AOPEN	= 9
MKN_AREAD	= 10
MKN_SEEK	= 12
MKN_STATS	= 13
MKN_ERRTEXT	= 14

; appel d'une fonction de MediaKit (C, AAPCS : x0-x18 changes)
macro mkcall n* {
	adr	x16,mk_fns
	ldr	x16,[x16,n*8]
	blr	x16
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'
kit_use '../../kits/audiokit/audiokit.abi'

start:
	mov	x19,x0				; arguments
	bl	kits_open
	bl	parse_args
	adr	x0,ui_win
	bl	ui_init
	mov	x0,-1
	mov	x1,-1
	mov	x2,MP_WIN_W
	mov	x3,MP_WIN_H
	adr	x4,s_app
	bl	tr4
	mov	x5,WF_RESIZABLE or WF_GLYPH_LIGHT or WF_BODY_DARK
	bl	ui_window_ex
	tbnz	x0,63,quit
	adr	x0,menus
	bl	ui_menu
	bl	ui_jetons
	adr	x1,jetons
	str	x0,[x1]
	mov	x0,360
	mov	x1,240
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	bl	make_widgets
	bl	mk_load				; MediaKit (message s'il manque)
	adr	x0,path
	ldrb	w0,[x0]
	cbz	w0,.paint
	adr	x0,mk_ok
	ldr	x0,[x0]
	cbz	x0,.paint
	bl	open_media
.paint:	bl	paint
main_loop:
	adr	x0,playing			; attente : courte en lecture
	ldr	x0,[x0]
	mov	x1,200
	cmp	x0,0
	mov	x0,4
	csel	x0,x0,x1,ne
	bl	ui_event
	cbz	x0,.tick
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MENU
	b.eq	.menu
	mov	x0,0
	bl	ui_w_event			; x0 widget, x1 code, x2 pris
	cbz	x0,.key
	cmp	x1,UN_CHANGE
	b.eq	.change
	cmp	x1,UN_ACTIVATE
	b.ne	.tick
	adr	x1,w_play
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.toggle
	adr	x1,w_open
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.open
	b	.tick
.change:
	adr	x1,w_pos
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.seek
	adr	x1,w_vol
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.tick
	bl	ui_w_get_value
	adr	x1,volume
	str	x0,[x1]
	bl	apply_volume
	b	.tick
.seek:	bl	ui_w_get_value			; position choisie (ms)
	str	x0,[sp,-16]!
	sys	SYS_TIME_MS
	adr	x1,user_ms
	str	x0,[x1]
	ldr	x0,[sp],16
	bl	seek_to
	b	.tick
.key:	cbnz	x2,.tick			; (pris par un widget)
	adr	x9,ui_ev
	ldr	w0,[x9,EV_TYPE]
	cmp	w0,WEV_KEY
	b.ne	.tick
	ldr	w0,[x9,EV_KIND]
	cbz	w0,.tick			; (relache)
	mov	x0,0
	bl	ui_menu_key			; Ctrl+O, Ctrl+W, Espace
	cmn	x0,1
	b.ne	.cmd
	adr	x9,ui_ev
	ldr	w0,[x9,EV_CODE]
	cmp	w0,0x2C				; Espace
	b.eq	.toggle
	cmp	w0,0x50				; gauche : -5 s
	mov	x1,-5000
	b.eq	.rel
	cmp	w0,0x4F				; droite : +5 s
	mov	x1,5000
	b.ne	.tick
.rel:	str	x1,[sp,-16]!
	bl	clock_now
	mov	x1,1000
	sdiv	x0,x0,x1
	ldr	x1,[sp],16
	add	x0,x0,x1
	cmp	x0,0
	csel	x0,xzr,x0,lt
	bl	seek_to
	b	.tick
.menu:	adr	x9,ui_ev
	ldr	w0,[x9,EV_CODE]
.cmd:	cmp	w0,MP_ID_CLOSE
	b.eq	quit
	cmp	w0,MP_ID_OPEN
	b.eq	.open
	cmp	w0,MP_ID_PLAY
	b.eq	.toggle
	cmp	w0,MP_ID_START
	b.ne	.tick
	mov	x0,0
	bl	seek_to
	b	.tick
.toggle:
	bl	toggle_play
	b	.tick
.open:	adr	x0,mk_ok
	ldr	x0,[x0]
	cbz	x0,.nomk
	adr	x0,s_dir
	adr	x1,path
	mov	x2,256
	bl	ui_file_open
	cbz	x0,.tick
	bl	open_media
	bl	paint
	b	.tick
.nomk:	adr	x0,s_app
	adr	x1,m_nomk
	bl	tr1
	mov	x2,UM_OK
	bl	ui_message
.tick:	bl	tick
	b	main_loop

quit:	bl	close_media
	mov	x0,0
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Arguments, MediaKit
; ---------------------------------------------------------------------------

; x19 = ligne d'arguments : le chemin (premier mot), « loop », « autoexit »
parse_args:
	prologue
	cbz	x19,.ret
.word:	ldrb	w0,[x19]
	cbz	w0,.ret
	cmp	w0,' '
	b.ne	.w
	add	x19,x19,1
	b	.word
.w:	adr	x1,s_loop			; mot connu ?
	mov	x3,0
	adr	x4,opt_loop
	bl	.is
	cbnz	x5,.next
	adr	x1,s_autoexit
	adr	x4,opt_autoexit
	bl	.is
	cbnz	x5,.next
	adr	x1,path				; sinon : le fichier
	mov	x2,0
.cp:	ldrb	w0,[x19,x2]
	cmp	w0,' '
	b.ls	.cpe
	strb	w0,[x1,x2]
	add	x2,x2,1
	cmp	x2,250
	b.lo	.cp
.cpe:	strb	wzr,[x1,x2]
.next:	ldrb	w0,[x19]			; mot suivant
	cbz	w0,.ret
	cmp	w0,' '
	b.eq	.word
	add	x19,x19,1
	b	.next
.ret:	epilogue
; x1 = mot, x4 = option : x5 = 1 (option mise a 1) si le mot de x19 est x1
.is:	mov	x2,0
.c:	ldrb	w5,[x1,x2]
	ldrb	w6,[x19,x2]
	cbz	w5,.end
	cmp	w5,w6
	b.ne	.no
	add	x2,x2,1
	b	.c
.end:	cmp	w6,' '
	b.hi	.no
	mov	x5,1
	str	x5,[x4]
	ret
.no:	mov	x5,0
	ret

; MediaKit : lib_open, sinon lu par AppKit (f_kit_load) ; table d'appels
; mk_fns ; mk_ok = 1 si present
mk_load:
	prologue
	adr	x0,s_mediakit
	mov	x1,MP_MK_FNS
	sys	SYS_LIB_OPEN
	tbz	x0,63,.ok
	cmn	x0,2				; E_NOENT : a lire
	b.ne	.out
	adr	x0,s_mediakit
	bl	f_kit_load
	adr	x0,s_mediakit
	mov	x1,MP_MK_FNS
	sys	SYS_LIB_OPEN
	tbnz	x0,63,.out
.ok:	adr	x1,mk_fns
	mov	x2,0
.f:	add	x3,x0,8
	ldr	w4,[x3,x2,lsl 2]
	add	x4,x0,x4
	str	x4,[x1,x2,lsl 3]
	add	x2,x2,1
	cmp	x2,MP_MK_FNS
	b.lo	.f
	adr	x1,mk_ok
	mov	x0,1
	str	x0,[x1]
.out:	epilogue

; ---------------------------------------------------------------------------
;  Interface
; ---------------------------------------------------------------------------

make_widgets:
	prologue
	mov	x0,0
	mov	x1,0
	mov	x2,44
	mov	x3,32
	adr	x4,s_playg
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_play
	str	x0,[x1]
	mov	x0,0
	mov	x1,0
	mov	x2,80
	mov	x3,32
	adr	x4,s_openb
	mov	x5,UB_GHOST
	bl	ui_pushbutton
	adr	x1,w_open
	str	x0,[x1]
	mov	x0,0
	mov	x1,0
	mov	x2,100
	mov	x3,28
	mov	x4,0
	mov	x5,1000
	mov	x6,0
	bl	ui_slider
	adr	x1,w_pos
	str	x0,[x1]
	mov	x0,0
	mov	x1,0
	mov	x2,100
	mov	x3,28
	adr	x4,s_time0
	mov	x5,ULB_RIGHT
	bl	ui_label
	adr	x1,w_time
	str	x0,[x1]
	mov	x0,0
	mov	x1,0
	mov	x2,90
	mov	x3,28
	mov	x4,0
	mov	x5,100
	adr	x6,volume
	ldr	x6,[x6]
	bl	ui_slider
	adr	x1,w_vol
	str	x0,[x1]
	bl	layout
	epilogue

; places des widgets et de la surface d'apres win_w, win_h
layout:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x9,win_w
	ldp	x19,x20,[x9]			; largeur, hauteur
	sub	x20,x20,MP_BAR_H-8		; y de la barre (+ 8)
	adr	x0,w_play
	ldr	x0,[x0]
	mov	x1,8
	mov	x2,x20
	mov	x3,44
	mov	x4,32
	bl	ui_w_move
	adr	x0,w_open
	ldr	x0,[x0]
	mov	x1,56
	mov	x2,x20
	mov	x3,80
	mov	x4,32
	bl	ui_w_move
	sub	x21,x19,144+8+120+8+100+8	; glissiere de position
	cmp	x21,40
	mov	x0,40
	csel	x21,x0,x21,lt
	adr	x0,w_pos
	ldr	x0,[x0]
	mov	x1,144
	add	x2,x20,2
	mov	x3,x21
	mov	x4,28
	bl	ui_w_move
	add	x22,x21,144+8			; temps
	adr	x0,w_time
	ldr	x0,[x0]
	mov	x1,x22
	add	x2,x20,2
	mov	x3,120
	mov	x4,28
	bl	ui_w_move
	add	x22,x22,128			; volume
	adr	x0,w_vol
	ldr	x0,[x0]
	mov	x1,x22
	add	x2,x20,2
	mov	x3,100
	mov	x4,28
	bl	ui_w_move
	bl	fit_surface
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; zone de l'image : la video ajustee a la zone au-dessus de la barre, centree
; (zone_x, zone_y, zone_w, zone_h) ; la surface suit (creee au premier besoin)
fit_surface:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x9,has_video
	ldr	x0,[x9]
	cbz	x0,.out
	adr	x9,win_w
	ldp	x19,x20,[x9]
	sub	x20,x20,MP_BAR_H		; zone : x19 x x20
	adr	x9,info
	ldr	x21,[x9,MKI_WIDTH]		; video : x21 x x22
	ldr	x22,[x9,MKI_HEIGHT]
	cmp	x21,1
	b.lt	.out
	cmp	x22,1
	b.lt	.out
	mul	x0,x21,x20			; vw * zh > vh * zw : la largeur limite
	mul	x1,x22,x19
	cmp	x0,x1
	b.le	.byh
	mov	x2,x19				; sw = zw, sh = vh * zw / vw
	mul	x3,x22,x19
	udiv	x3,x3,x21
	b	.even
.byh:	mov	x3,x20				; sh = zh, sw = vw * zh / vh
	mul	x2,x21,x20
	udiv	x2,x2,x22
.even:	and	x2,x2,not 1
	and	x3,x3,not 1
	cmp	x2,2
	mov	x0,2
	csel	x2,x0,x2,lt
	cmp	x3,2
	csel	x3,x0,x3,lt
	sub	x0,x19,x2			; centree
	lsr	x0,x0,1
	sub	x1,x20,x3
	lsr	x1,x1,1
	adr	x9,zone_x
	stp	x0,x1,[x9]
	stp	x2,x3,[x9,16]
	adr	x9,surf
	ldr	x9,[x9]
	cbz	x9,.new
	mov	x4,x3				; (deplacee, tampon neuf s'il le faut)
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,x9
	bl	ui_surface_move
	b	.out
.new:	mov	x4,WS_SF_XRGB8888
	bl	ui_surface
	adr	x1,surf
	str	x0,[x1]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; fonction de ui_on_resize (x0 largeur, x1 hauteur)
on_resize:
	prologue
	adr	x2,win_w
	stp	x0,x1,[x2]
	bl	layout
	bl	paint
	bl	show_held			; l'image, a la nouvelle taille
	epilogue

; fond (zone de l'image noire, barre), message, widgets
paint:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x9,win_w
	ldp	x19,x20,[x9]
	mov	x0,0
	mov	x1,0
	mov	x2,x19
	sub	x3,x20,MP_BAR_H
	mov	w4,0
	bl	ui_fill
	mov	x0,0				; barre
	sub	x1,x20,MP_BAR_H
	mov	x2,x19
	mov	x3,MP_BAR_H
	adr	x4,jetons
	ldr	x4,[x4]
	ldr	w4,[x4,TJ_FACE]
	bl	ui_fill
	adr	x0,msg				; message (sans image)
	ldr	x0,[x0]
	cbz	x0,.w
	bl	tr0
	mov	x1,15
	adr	x2,ui_font
	bl	ui_text_width
	sub	x1,x19,x0
	asr	x1,x1,1
	cmp	x1,8
	mov	x2,8
	csel	x1,x2,x1,lt
	sub	x2,x20,MP_BAR_H
	lsr	x2,x2,1
	adr	x0,msg
	ldr	x0,[x0]
	bl	tr0
	movl	w3,0x00C8CDD5
	mov	x4,15
	adr	x5,ui_font
	sub	x6,x19,16
	bl	ui_text_fit
.w:	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	ldp	x19,x20,[sp],16
	epilogue

; texte du bouton de lecture d'apres playing
play_label:
	prologue
	adr	x0,playing
	ldr	x0,[x0]
	adr	x1,s_pauseg
	adr	x2,s_playg
	cmp	x0,0
	csel	x1,x1,x2,ne
	adr	x0,w_play
	ldr	x0,[x0]
	bl	ui_w_set_text
	epilogue

; temps (« m:ss / m:ss ») et position, toutes les MP_UI_MS (pas pendant
; qu'on tire la glissiere)
ui_progress_update:
	prologue
	stp	x19,x20,[sp,-16]!
	sys	SYS_TIME_MS
	mov	x19,x0
	adr	x9,last_ui
	ldr	x1,[x9]
	sub	x1,x19,x1
	cmp	x1,MP_UI_MS
	b.lo	.out
	str	x19,[x9]
	bl	clock_now
	mov	x1,1000
	sdiv	x20,x0,x1			; ms, de 0 a la duree
	cmp	x20,0
	csel	x20,xzr,x20,lt
	adr	x9,info
	ldr	x1,[x9,MKI_DURATION]
	mov	x2,1000
	udiv	x1,x1,x2
	cmp	x20,x1
	csel	x20,x1,x20,gt
	adr	x0,timebuf
	mov	x1,x20
	bl	fmt_time
	adr	x1,s_slash
	bl	str_cat
	adr	x9,info
	ldr	x1,[x9,MKI_DURATION]
	mov	x2,1000
	udiv	x1,x1,x2
	bl	fmt_time
	adr	x0,w_time
	ldr	x0,[x0]
	adr	x1,timebuf
	bl	ui_w_set_text
	adr	x9,user_ms			; glissiere : pas pendant un glisser
	ldr	x1,[x9]
	sub	x1,x19,x1
	cmp	x1,700
	b.lo	.out
	adr	x0,w_pos
	ldr	x0,[x0]
	mov	x1,x20
	bl	ui_w_set_value
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon, x1 = ms -> x0 = fin (« m:ss »)
fmt_time:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x2,1000
	udiv	x1,x1,x2			; secondes
	mov	x2,60
	udiv	x0,x1,x2			; minutes
	msub	x20,x0,x2,x1			; secondes restantes
	mov	x1,x19
	bl	fmt_dec
	mov	w1,':'
	strb	w1,[x0],1
	mov	w1,'0'
	cmp	x20,10
	b.hs	.two
	strb	w1,[x0],1
.two:	mov	x1,x0
	mov	x0,x20
	bl	fmt_dec
	strb	wzr,[x0]
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Le fichier
; ---------------------------------------------------------------------------

; ouvre « path » (l'ancien ferme) et commence la lecture
open_media:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	close_media
	adr	x0,path
	mkcall	MKN_OPEN
	tbz	x0,63,.ok
	mkcall	MKN_ERRTEXT			; message : l'erreur
	mov	x19,x0
	adr	x0,errbuf
	adr	x1,m_openko
	bl	tr_cat
	mov	x1,x19
	bl	str_cat
	adr	x0,errbuf
	adr	x1,msg
	str	x0,[x1]
	adr	x0,s_app
	bl	tr0
	bl	ui_title
	b	.out
.ok:	adr	x1,mh
	str	x0,[x1]
	adr	x1,info
	mkcall	MKN_INFO
	adr	x9,info
	ldr	x0,[x9,MKI_VOK]
	adr	x1,has_video
	str	x0,[x1]
	adr	x1,msg				; message : son seul, ou rien
	adr	x2,m_audioonly
	cmp	x0,0
	csel	x2,xzr,x2,ne
	str	x2,[x1]
	ldr	x0,[x9,MKI_DURATION]		; glissiere : 0 .. duree (ms), pas 100 ms
	mov	x1,1000
	udiv	x2,x0,x1
	cmp	x2,1
	csinc	x2,x2,xzr,ge
	adr	x0,w_pos
	ldr	x0,[x0]
	mov	x1,0
	mov	x3,0
	mov	x4,100
	bl	ui_w_set_range
	adr	x0,titlebuf			; « Lecteur multimédia - nom »
	adr	x1,s_title
	bl	tr_cat
	adr	x1,path
	mov	x2,x1
.n:	ldrb	w3,[x1],1
	cbz	w3,.nd
	cmp	w3,'/'
	b.ne	.n
	mov	x2,x1
	b	.n
.nd:	mov	x1,x2
	bl	str_cat
	adr	x0,titlebuf
	bl	ui_title
	bl	fit_surface
	adr	x9,has_video			; pas de surface : son seul
	ldr	x0,[x9]
	cbz	x0,.snd
	adr	x0,surf
	ldr	x0,[x0]
	cbnz	x0,.snd
	adr	x1,has_video
	str	xzr,[x1]
	adr	x0,m_nosurf
	adr	x1,msg
	str	x0,[x1]
.snd:	bl	audio_start			; son (s'il est decode par MediaKit)
	adr	x9,info
	ldr	x0,[x9,MKI_ATRACK]
	tbnz	x0,63,.play
	ldr	x0,[x9,MKI_AOK]
	cbnz	x0,.play
	adr	x0,m_mute			; son pour AudioKit : muet
	bl	puts
.play:	bl	reset_counters
	sys	SYS_TIME_MS
	adr	x1,play_t0
	str	x0,[x1]
	adr	x1,playing
	mov	x0,1
	str	x0,[x1]
	adr	x1,ended
	str	xzr,[x1]
	sys	SYS_TIME_MS
	adr	x1,clk_t0
	str	x0,[x1]
	adr	x1,clk_base
	str	xzr,[x1]
	bl	play_label
.out:	ldp	x19,x20,[sp],16
	epilogue

; ferme le fichier ouvert (son, surface gardee)
close_media:
	prologue
	bl	audio_stop
	adr	x9,mh
	ldr	x0,[x9]
	cbz	x0,.out
	str	xzr,[x9]
	mkcall	MKN_CLOSE
.out:	adr	x9,playing
	str	xzr,[x9]
	adr	x9,has_video
	str	xzr,[x9]
	adr	x9,pending
	str	xzr,[x9]
	adr	x9,video_eof
	str	xzr,[x9]
	adr	x9,audio_eof
	str	xzr,[x9]
	epilogue

reset_counters:
	adr	x9,shown
	stp	xzr,xzr,[x9]			; shown, dropped
	str	xzr,[x9,16]			; drop_run
	adr	x9,pending
	str	xzr,[x9]
	adr	x9,video_eof
	str	xzr,[x9]
	adr	x9,audio_eof
	str	xzr,[x9]
	ret

; ---------------------------------------------------------------------------
;  Son
; ---------------------------------------------------------------------------

; MediaKit en s16 stereo 44,1 kHz, flux d'AudioKit au meme format
; (audio_on = 1) ; sinon horloge du systeme
audio_start:
	prologue
	bl	audio_stop
	adr	x9,mh
	ldr	x0,[x9]
	cbz	x0,.out
	adr	x9,info
	ldr	x1,[x9,MKI_ATRACK]
	tbnz	x1,63,.out
	ldr	x1,[x9,MKI_AOK]
	cbz	x1,.mute
	movl	x1,MP_RATE
	mkcall	MKN_AOPEN
	tbnz	x0,63,.out
	movl	x0,MP_RATE
	mov	x1,2
	mov	x2,16
	mov	x3,0
	bl	ak_open				; (poignee >= 0 : 0 est valable)
	tbnz	x0,63,.nosnd
	adr	x1,stream
	str	x0,[x1]
	adr	x9,audio_on
	mov	x0,1
	str	x0,[x9]
	adr	x9,a_first			; l'horloge part du premier paquet
	str	x0,[x9]
	adr	x9,a_lastpos
	stp	xzr,xzr,[x9]			; a_lastpos, a_posms
	adr	x9,audio_eof
	str	xzr,[x9]
	bl	apply_volume
	b	.out
.nosnd:	adr	x0,m_nosnd
	bl	puts
	b	.out
.mute:	mov	x0,0
.out:	epilogue

audio_stop:
	prologue
	adr	x9,audio_on			; (audio_on : le flux est ouvert)
	ldr	x0,[x9]
	cbz	x0,.out
	str	xzr,[x9]
	adr	x9,stream
	ldr	x0,[x9]
	bl	ak_close
.out:	epilogue

apply_volume:
	prologue
	adr	x9,audio_on
	ldr	x0,[x9]
	cbz	x0,.out
	adr	x9,stream
	ldr	x0,[x9]
	adr	x9,volume
	ldr	x1,[x9]
	lsl	x1,x1,8				; 0..100 -> 0..256
	mov	x2,100
	udiv	x1,x1,x2
	bl	ak_volume
.out:	epilogue

; le flux d'AudioKit garde plein (sans attendre)
feed_audio:
	prologue
	stp	x19,x20,[sp,-16]!
.more:	adr	x9,audio_on
	ldr	x0,[x9]
	cbz	x0,.out
	adr	x9,audio_eof
	ldr	x0,[x9]
	cbnz	x0,.out
	adr	x9,stream
	ldr	x0,[x9]
	bl	ak_space
	cmp	x0,MP_CHUNK*4
	b.lt	.out
	adr	x9,mh
	ldr	x0,[x9]
	adr	x1,abuf
	mov	x2,MP_CHUNK
	adr	x3,a_pts
	mkcall	MKN_AREAD
	cmp	x0,0
	b.le	.eof
	mov	x19,x0
	adr	x9,a_first			; premier paquet : base de l'horloge
	ldr	x1,[x9]
	cbz	x1,.w
	str	xzr,[x9]
	adr	x9,a_pts
	ldr	x1,[x9]
	adr	x9,a_base
	str	x1,[x9]
.w:	adr	x9,stream
	ldr	x0,[x9]
	adr	x1,abuf
	lsl	x2,x19,2
	bl	ak_write
	b	.more
.eof:	adr	x9,audio_eof
	mov	x0,1
	str	x0,[x9]
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = temps de la lecture (us) : horloge du son (premier paquet + trames
; jouees), sinon du systeme ; un son qui n'avance plus (pas de sortie)
; rend la main a l'horloge du systeme
clock_now:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x9,playing
	ldr	x0,[x9]
	cbz	x0,.frozen
	adr	x9,audio_on
	ldr	x0,[x9]
	cbz	x0,.sys
	adr	x9,a_first
	ldr	x0,[x9]
	cbnz	x0,.sys				; (rien d'ecrit encore)
	adr	x9,stream
	ldr	x0,[x9]
	bl	ak_position
	mov	x19,x0				; trames jouees
	sys	SYS_TIME_MS
	mov	x20,x0
	adr	x9,a_lastpos
	ldp	x1,x2,[x9]			; a_lastpos, a_posms
	cmp	x19,x1
	b.eq	.same
	stp	x19,x20,[x9]
	b	.aclk
.same:	adr	x3,audio_eof			; (son fini : il s'arrete, normal)
	ldr	x3,[x3]
	cbnz	x3,.aclk
	cbnz	x2,.st
	str	x20,[x9,8]
	b	.aclk
.st:	sub	x2,x20,x2
	movl	x3,MP_STALL_MS
	cmp	x2,x3
	b.lo	.aclk
	adr	x0,m_stall			; plus de son : horloge du systeme
	bl	puts
	bl	.aclk_val
	adr	x9,clk_base
	str	x0,[x9]
	adr	x9,clk_t0
	str	x20,[x9]
	bl	audio_stop
	b	.sys
.aclk:	bl	.aclk_val
	b	.out
.sys:	sys	SYS_TIME_MS
	adr	x9,clk_t0
	ldr	x1,[x9]
	sub	x0,x0,x1
	mov	x1,1000
	mul	x0,x0,x1
	adr	x9,clk_base
	ldr	x1,[x9]
	add	x0,x0,x1
	b	.out
.frozen:
	adr	x9,clk_base
	ldr	x0,[x9]
.out:	ldp	x19,x20,[sp],16
	epilogue
; x19 = trames jouees -> x0 = a_base + x19 * 1e6 / MP_RATE
.aclk_val:
	movl	x1,1000000
	mul	x0,x19,x1
	movl	x1,MP_RATE
	udiv	x0,x0,x1
	adr	x9,a_base
	ldr	x1,[x9]
	add	x0,x0,x1
	ret

; ---------------------------------------------------------------------------
;  Lecture
; ---------------------------------------------------------------------------

; le tour de la lecture : son, image due, fin, temps affiche
tick:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x9,playing
	ldr	x0,[x9]
	cbz	x0,.out
	bl	feed_audio
	adr	x9,has_video
	ldr	x0,[x9]
	cbz	x0,.end
	mov	x19,8				; au plus 8 images par tour
.img:	adr	x9,video_eof
	ldr	x0,[x9]
	cbnz	x0,.end
	adr	x9,pending
	ldr	x0,[x9]
	cbnz	x0,.due
	adr	x9,mh				; l'image suivante, decodee
	ldr	x0,[x9]
	adr	x1,frame
	mkcall	MKN_VNEXT
	cmp	x0,1
	b.eq	.got
	adr	x9,video_eof
	mov	x0,1
	str	x0,[x9]
	b	.end
.got:	adr	x9,pending
	mov	x0,1
	str	x0,[x9]
.due:	bl	clock_now
	adr	x9,frame
	ldr	x1,[x9,MKF_PTS]
	subs	x2,x0,x1			; avance de l'horloge sur l'image
	b.lt	.end				; pas encore l'heure
	movl	x3,MP_LATE_US			; tres en retard : sautee (pas plus de
	cmp	x2,x3				; MP_MAX_DROP de suite)
	b.lt	.show
	adr	x9,drop_run
	ldr	x0,[x9]
	cmp	x0,MP_MAX_DROP
	b.hs	.show
	add	x0,x0,1
	str	x0,[x9]
	adr	x9,dropped
	ldr	x0,[x9]
	add	x0,x0,1
	str	x0,[x9]
	adr	x9,pending
	str	xzr,[x9]
	subs	x19,x19,1
	b.ne	.img
	b	.end
.show:	bl	show_held
	adr	x9,drop_run
	str	xzr,[x9]
	adr	x9,shown
	ldr	x0,[x9]
	add	x0,x0,1
	str	x0,[x9]
	adr	x9,pending
	str	xzr,[x9]
.end:	bl	check_end
	bl	ui_progress_update
.out:	ldp	x19,x20,[sp],16
	epilogue

; l'image gardee par MediaKit dans la surface (a sa taille), signalee
show_held:
	prologue
	adr	x9,has_video
	ldr	x0,[x9]
	cbz	x0,.out
	adr	x9,surf
	ldr	x0,[x9]
	cbz	x0,.out
	bl	ui_surface_pixels		; x0 pixels, x1 pas, x2 l, x3 h
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	adr	x9,mh
	ldr	x0,[x9]
	cbz	x0,.out
	mkcall	MKN_VCONV
	tbnz	x0,63,.out
	adr	x9,surf
	ldr	x0,[x9]
	mov	x1,0
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	ui_surface_damage
.out:	epilogue

; fin de la lecture : images finies (ou pas d'image) et son fini
check_end:
	prologue
	adr	x9,has_video
	ldr	x0,[x9]
	cbz	x0,.a
	adr	x9,video_eof
	ldr	x0,[x9]
	cbz	x0,.out
.a:	adr	x9,audio_on
	ldr	x0,[x9]
	cbz	x0,.noa
	adr	x9,audio_eof
	ldr	x0,[x9]
	cbz	x0,.out
	adr	x9,stream			; ce qui est ecrit, joue (le dernier bloc
	ldr	x0,[x9]				; partiel de sound peut rester : a peu
	bl	ak_pending			; pres), ou la duree passee
	cmp	x0,MP_TAIL_BYTES
	b.le	.done
	bl	clock_now
	adr	x9,info
	ldr	x1,[x9,MKI_DURATION]
	cmp	x0,x1
	b.ge	.done
	b	.out
.noa:	adr	x9,has_video			; ni image ni son : rien a attendre
	ldr	x0,[x9]
	cbnz	x0,.done
	adr	x9,info
	ldr	x0,[x9,MKI_ATRACK]
	tbz	x0,63,.timed
	b	.done
.timed:	bl	clock_now			; son muet seul : jusqu'a la duree
	adr	x9,info
	ldr	x1,[x9,MKI_DURATION]
	cmp	x0,x1
	b.lt	.out
.done:	bl	end_of_media
.out:	epilogue

; fin : bilan sur la sortie standard ; loop : recommence ; autoexit : quitte
end_of_media:
	prologue
	bl	report
	adr	x9,opt_loop
	ldr	x0,[x9]
	cbz	x0,.stop
	mov	x0,0
	bl	seek_to
	b	.out
.stop:	adr	x9,opt_autoexit
	ldr	x0,[x9]
	cbnz	x0,quit
	bl	clock_now			; a l'arret : sur la fin
	adr	x9,clk_base
	str	x0,[x9]
	adr	x9,playing
	str	xzr,[x9]
	adr	x9,ended
	mov	x0,1
	str	x0,[x9]
	bl	audio_stop
	bl	play_label
	adr	x9,last_ui
	str	xzr,[x9]
	bl	ui_progress_update
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



; x4 = texte -> sa traduction ; les autres registres gardes

tr4:

	prologue

	str	x0,[sp,-16]!

	mov	x0,x4

	bl	tr0

	mov	x4,x0

	ldr	x0,[sp],16

	epilogue

; « mediaplayer : N images montrees, M sautees en T ms : F images/s ;
; decodage moyen D us »
report:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,m_rep1
	bl	puts
	adr	x9,shown
	ldr	x0,[x9]
	bl	print_dec
	adr	x0,m_rep2
	bl	puts
	adr	x9,dropped
	ldr	x0,[x9]
	bl	print_dec
	adr	x0,m_rep3
	bl	puts
	sys	SYS_TIME_MS
	adr	x9,play_t0
	ldr	x1,[x9]
	sub	x19,x0,x1
	mov	x0,x19
	bl	print_dec
	adr	x0,m_rep4
	bl	puts
	adr	x9,shown			; images/s (une decimale)
	ldr	x0,[x9]
	mov	x1,10000
	mul	x0,x0,x1
	cmp	x19,1
	csinc	x19,x19,xzr,ge
	udiv	x0,x0,x19
	mov	x1,10
	udiv	x19,x0,x1
	msub	x20,x19,x1,x0
	mov	x0,x19
	bl	print_dec
	adr	x0,m_comma
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_rep5
	bl	puts
	adr	x9,mh
	ldr	x0,[x9]
	adr	x1,stats
	mkcall	MKN_STATS
	adr	x9,stats
	ldr	x0,[x9,MKS_VDECODE_US]
	ldr	x1,[x9,MKS_VFRAMES]
	cmp	x1,1
	csinc	x1,x1,xzr,ge
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_rep6
	bl	puts
	adr	x9,audio_on
	ldr	x0,[x9]
	adr	x1,m_snd_on
	adr	x2,m_snd_off
	cmp	x0,0
	csel	x0,x1,x2,ne
	bl	puts
	sys	SYS_TIME_MS			; (lecture suivante : loop)
	adr	x9,play_t0
	str	x0,[x9]
	adr	x9,shown
	stp	xzr,xzr,[x9]
	ldp	x19,x20,[sp],16
	epilogue

; lecture / pause (a la fin : depuis le debut)
toggle_play:
	prologue
	adr	x9,mh
	ldr	x0,[x9]
	cbz	x0,.out
	adr	x9,ended
	ldr	x0,[x9]
	cbz	x0,.tg
	str	xzr,[x9]
	mov	x0,0				; finie : depuis le debut
	bl	seek_to
	adr	x9,playing
	mov	x0,1
	str	x0,[x9]
	b	.lbl
.tg:	adr	x9,playing
	ldr	x0,[x9]
	cbz	x0,.resume
	bl	clock_now			; pause : l'horloge arretee la, son coupe
	adr	x9,clk_base
	str	x0,[x9]
	adr	x9,playing
	str	xzr,[x9]
	bl	audio_stop
	b	.lbl
.resume:
	sys	SYS_TIME_MS			; reprise : horloge du systeme depuis
	adr	x9,clk_t0			; la pause, puis celle du son (le son
	str	x0,[x9]				; repart ou il en etait)
	adr	x9,playing
	mov	x0,1
	str	x0,[x9]
	bl	audio_restart
.lbl:	bl	play_label
.out:	epilogue

; son repris sans recherche : flux neuf, horloge du premier paquet lu
audio_restart:
	prologue
	adr	x9,info
	ldr	x0,[x9,MKI_AOK]
	cbz	x0,.out
	adr	x9,audio_eof
	ldr	x0,[x9]
	cbnz	x0,.out
	movl	x0,MP_RATE
	mov	x1,2
	mov	x2,16
	mov	x3,0
	bl	ak_open
	tbnz	x0,63,.out
	adr	x1,stream
	str	x0,[x1]
	adr	x9,audio_on
	mov	x0,1
	str	x0,[x9]
	adr	x9,a_first
	str	x0,[x9]
	adr	x9,a_lastpos
	stp	xzr,xzr,[x9]
	bl	apply_volume
.out:	epilogue

; x0 = ms : recherche (au point d'acces precedent), son repris la
seek_to:
	prologue
	str	x19,[sp,-16]!
	adr	x9,mh
	ldr	x1,[x9]
	cbz	x1,.out
	mov	x2,1000
	mul	x1,x0,x2
	mov	x0,x1
	adr	x9,mh
	ldr	x0,[x9]
	mkcall	MKN_SEEK
	tbnz	x0,63,.out			; (pas d'index : rien)
	mov	x19,x0				; temps atteint
	adr	x9,clk_base
	str	x19,[x9]
	sys	SYS_TIME_MS
	adr	x9,clk_t0
	str	x0,[x9]
	adr	x9,pending
	str	xzr,[x9]
	adr	x9,video_eof
	str	xzr,[x9]
	adr	x9,audio_eof
	str	xzr,[x9]
	adr	x9,drop_run
	str	xzr,[x9]
	adr	x9,ended
	str	xzr,[x9]
	adr	x9,playing
	ldr	x0,[x9]
	cbz	x0,.paused
	bl	audio_start
	b	.ui
.paused:					; en pause : l'image du point atteint
	adr	x9,has_video
	ldr	x0,[x9]
	cbz	x0,.ui
	adr	x9,mh
	ldr	x0,[x9]
	adr	x1,frame
	mkcall	MKN_VNEXT
	cmp	x0,1
	b.ne	.ui
	bl	show_held
.ui:	adr	x9,last_ui
	str	xzr,[x9]
	adr	x9,user_ms
	str	xzr,[x9]
	bl	ui_progress_update
.out:	ldr	x19,[sp],16
	epilogue

segment readable writeable
kit_data
		align 8
mk_fns		rd MP_MK_FNS			; table de MediaKit (mk_load)
mk_ok		dd 0
mh		dd 0				; poignee de MediaKit
jetons		dd 0
win_w		dd MP_WIN_W			; taille (contigus)
win_h		dd MP_WIN_H
zone_x		dd 0				; zone de l'image : x, y, l, h
zone_y		dd 0				; (contigus)
zone_w		dd 0
zone_h		dd 0
surf		dd 0				; surface attachee (UIKit)
w_play		dd 0
w_open		dd 0
w_pos		dd 0
w_time		dd 0
w_vol		dd 0
msg		dd m_nofile			; texte de la zone de l'image (0 : aucun)
volume		dd 80
opt_loop	dd 0
opt_autoexit	dd 0
playing		dd 0
ended		dd 0
has_video	dd 0
pending		dd 0				; une image decodee attend son heure
video_eof	dd 0
audio_eof	dd 0
audio_on	dd 0
stream		dd 0				; flux d'AudioKit (ouvert si audio_on)
a_first		dd 0				; 1 : la base de l'horloge pas encore lue
a_pts		dd 0
a_base		dd 0				; temps du premier paquet ecrit (us)
a_lastpos	dd 0				; a_lastpos, a_posms (contigus)
a_posms		dd 0
clk_base	dd 0				; horloge du systeme : temps (us) a clk_t0
clk_t0		dd 0
last_ui		dd 0
user_ms		dd 0				; dernier geste sur la glissiere
shown		dd 0				; shown, dropped, drop_run (contigus)
dropped		dd 0
drop_run	dd 0
play_t0		dd 0
info		rb MKI_SIZE
frame		rb MKF_SIZE
stats		rb MKS_SIZE
path		rb 260
titlebuf	rb 300
errbuf		rb 200
timebuf		rb 48
s_app		db 'Lecteur multim',0xC3,0xA9,'dia',0
s_title		db 'Lecteur multim',0xC3,0xA9,'dia - ',0
s_mediakit	db 'mediakit',0
s_dir		db 'SD0:/home/video',0
s_loop		db 'loop',0
s_autoexit	db 'autoexit',0
s_playg		db 0xE2,0x96,0xB6,0		; ▶
s_pauseg	db 0xE2,0x9D,0x9A,0xE2,0x9D,0x9A,0	; ❚❚
s_openb		db 'Ouvrir',0
s_time0		db '0:00 / 0:00',0
s_slash		db ' / ',0
m_nofile	db 'Fichier > Ouvrir (Ctrl+O) : WebM, Matroska, MP4 (VP8, VP9, AV1)',0
m_audioonly	db 'Son seul',0
m_nosurf	db 'Surface d',0x27,'Elegant indisponible (Elegant trop ancien ?)',0
m_nomk		db 'MediaKit est absent : SD0:/lib/mediakit.so (prebuilt/mediakit.so, construit par tools/mediakit/build.sh). Le lecteur ne peut pas lire de vid',0xC3,0xA9,'o sans lui.',0
m_openko	db 'Lecture impossible : ',0
m_mute		db 'mediaplayer : son pour AudioKit (pas encore de d',0xC3,0xA9,'codeur par paquets) : muet',10,0
m_nosnd		db 'mediaplayer : pas de sortie son (AudioKit) : horloge du syst',0xC3,0xA8,'me',10,0
m_stall		db 'mediaplayer : le son n',0x27,'avance plus : horloge du syst',0xC3,0xA8,'me',10,0
m_rep1		db 'mediaplayer : ',0
m_rep2		db ' images montr',0xC3,0xA9,'es, ',0
m_rep3		db ' saut',0xC3,0xA9,'es en ',0
m_rep4		db ' ms : ',0
m_comma		db ',',0
m_rep5		db ' images/s ; d',0xC3,0xA9,'codage moyen ',0
m_rep6		db ' us',0
m_snd_on	db ' ; son par AudioKit',10,0
m_snd_off	db ' ; sans son',10,0
menus		db 'MFichier',10
		db 'I1',9,'Ouvrir',0xE2,0x80,0xA6,9,'Ctrl+O',10
		db '-',10
		db 'I2',9,'Fermer',9,'Ctrl+W',10
		db 'MLecture',10
		db 'I3',9,'Lecture / pause',9,'Espace',10
		db 'I4',9,'Au d',0xC3,0xA9,'but',10,0
		align 8
abuf		rb MP_CHUNK * 4
ui_data
