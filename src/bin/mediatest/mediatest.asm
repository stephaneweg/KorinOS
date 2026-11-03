; ============================================================================
;  mediatest - essai de MediaKit (SD0:/lib/mediakit.so, docs/MEDIAKIT.md)
;
;	mediatest fichier [images]
;
;  Ouvre le fichier, montre ses pistes, decode toutes les images (ou les N
;  premieres) en XRGB8888 a leur taille, puis tout le son (s16 stereo
;  48 kHz) s'il est decode par MediaKit, cherche au milieu et decode une image.
;  Sortie : images decodees, images/s (decodage + conversion), trames de son,
;  temps atteint par la recherche ; « mediatest : PASS » ou « FAIL ».
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/mediakit/mediakit.inc'

MT_AUDIO_FRAMES	= 4096

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/mediakit/mediakit.abi'

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,usage
	ldrb	w0,[x19]
	cbz	w0,usage
	adr	x20,path			; chemin : le premier mot
	mov	x1,0
.cp:	ldrb	w0,[x19],1
	cmp	w0,' '
	b.ls	.cpe
	strb	w0,[x20,x1]
	add	x1,x1,1
	cmp	x1,250
	b.lo	.cp
.cpe:	strb	wzr,[x20,x1]
	mov	x21,-1				; images au plus (-1 : toutes)
	cbz	w0,.go
	mov	x0,x19
	bl	parse_dec
	cmp	x0,0
	csinv	x21,x0,xzr,gt
.go:	bl	mk_version
	mov	x22,x0
	adr	x0,m_ver
	bl	puts
	lsr	x0,x22,16
	bl	print_dec
	adr	x0,m_dot
	bl	puts
	and	x0,x22,0xFFFF
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,path
	bl	mk_open
	tbnz	x0,63,.openko
	mov	x22,x0				; poignee
	mov	x0,x22
	adr	x1,info
	bl	mk_info
	bl	show_info
	; --- les images -------------------------------------------------------
	adr	x9,info
	ldr	x0,[x9,MKI_VOK]
	cbz	x0,.audio
	ldr	x23,[x9,MKI_WIDTH]		; tampon : taille de la premiere image
	ldr	x24,[x9,MKI_HEIGHT]
	mul	x0,x23,x24
	lsl	x0,x0,2
	add	x0,x0,4095
	and	x0,x0,not 4095
	mov	x25,x0
	sys	SYS_SBRK
	tbnz	x0,63,.nomem
	mov	x26,x0
	sys	SYS_TIME_MS
	mov	x27,x0
	mov	x28,0				; images
.img:	cmp	x28,x21
	b.hs	.imgend
	mov	x0,x22
	adr	x1,frame
	bl	mk_video_next
	cmp	x0,1
	b.ne	.imgend
	adr	x9,frame			; taille de cette image (borne au tampon)
	ldr	x3,[x9,MKF_WIDTH]
	ldr	x4,[x9,MKF_HEIGHT]
	mul	x0,x3,x4
	lsl	x0,x0,2
	cmp	x0,x25
	b.hi	.imgend
	mov	x0,x22
	mov	x1,x26
	lsl	x2,x3,2
	bl	mk_video_convert
	tbnz	x0,63,.convko
	cbnz	x28,.n1
	bl	show_first			; premiere image : pts, pixel du centre
.n1:	add	x28,x28,1
	b	.img
.imgend:
	sys	SYS_TIME_MS
	sub	x27,x0,x27
	adr	x0,m_frames			; « N images en T ms : F images/s »
	bl	puts
	mov	x0,x28
	bl	print_dec
	adr	x0,m_in
	bl	puts
	mov	x0,x27
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	mov	x0,x28
	mov	x1,x27
	bl	print_rate
	adr	x0,m_fps
	bl	puts
	adr	x0,m_avg			; decodage seul (mk_stats)
	bl	puts
	mov	x0,x22
	adr	x1,stats
	bl	mk_stats
	adr	x9,stats
	ldr	x0,[x9,MKS_VDECODE_US]
	ldr	x1,[x9,MKS_VFRAMES]
	cmp	x1,1
	csinc	x1,x1,xzr,ge
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_us
	bl	puts
	cbz	x28,.fail
	; --- le son -----------------------------------------------------------
.audio:	adr	x9,info
	ldr	x0,[x9,MKI_ATRACK]
	tbnz	x0,63,.seek
	mov	x0,x22
	movl	x1,48000
	bl	mk_audio_open
	cbz	x0,.aok
	adr	x0,m_amute
	bl	puts
	b	.seek
.aok:	mov	x23,0				; trames
	sys	SYS_TIME_MS
	mov	x27,x0
.ar:	mov	x0,x22
	adr	x1,abuf
	mov	x2,MT_AUDIO_FRAMES
	mov	x3,0
	bl	mk_audio_read
	cmp	x0,0
	b.le	.aend
	add	x23,x23,x0
	b	.ar
.aend:	sys	SYS_TIME_MS
	sub	x27,x0,x27
	adr	x0,m_audio
	bl	puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_audio2
	bl	puts
	mov	x0,x27
	bl	print_dec
	adr	x0,m_msnl
	bl	puts
	cbz	x23,.fail
	; --- recherche au milieu ----------------------------------------------
.seek:	adr	x9,info
	ldr	x1,[x9,MKI_DURATION]
	lsr	x1,x1,1
	mov	x0,x22
	bl	mk_seek
	mov	x23,x0
	adr	x0,m_seek
	bl	puts
	mov	x0,x23
	tbnz	x0,63,.sk2
	mov	x1,1000
	udiv	x0,x0,x1
.sk2:	bl	print_dec
	adr	x0,m_msnl
	bl	puts
	adr	x9,info
	ldr	x0,[x9,MKI_VOK]
	cbz	x0,.close
	tbnz	x23,63,.close
	mov	x0,x22
	adr	x1,frame
	bl	mk_video_next
	cmp	x0,1
	b.ne	.fail
	adr	x0,m_after
	bl	puts
	adr	x9,frame
	ldr	x0,[x9,MKF_PTS]
	mov	x1,1000
	sdiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_msnl
	bl	puts
.close:	mov	x0,x22
	bl	mk_close
	adr	x0,m_pass
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
.convko:
	adr	x0,m_conv
	bl	puts
	b	.fail
.nomem:	adr	x0,m_nomem
	bl	puts
	b	.fail
.openko:
	mov	x19,x0
	adr	x0,m_openko
	bl	puts
	mov	x0,x19
	bl	mk_error_text
	bl	puts
	adr	x0,m_nl
	bl	puts
.fail:	adr	x0,m_fail
	bl	puts
	mov	x0,1
	sys	SYS_EXIT
usage:	adr	x0,m_usage
	bl	puts
	mov	x0,2
	sys	SYS_EXIT

; x0 = images, x1 = ms : « F,f » images/s
print_rate:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x2,10000
	mul	x0,x0,x2
	cmp	x1,1
	csinc	x1,x1,xzr,ge
	udiv	x0,x0,x1
	mov	x2,10
	udiv	x19,x0,x2
	msub	x20,x19,x2,x0
	mov	x0,x19
	bl	print_dec
	adr	x0,m_comma
	bl	puts
	mov	x0,x20
	bl	print_dec
	ldp	x19,x20,[sp],16
	epilogue

; les pistes (info) : conteneur, video, son, duree
show_info:
	prologue
	adr	x0,m_cont
	bl	puts
	adr	x0,info+MKI_CONTAINER
	bl	puts
	adr	x0,m_dur
	bl	puts
	adr	x9,info
	ldr	x0,[x9,MKI_DURATION]
	mov	x1,1000
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_msnl
	bl	puts
	adr	x9,info
	ldr	x0,[x9,MKI_VTRACK]
	tbnz	x0,63,.nov
	adr	x0,m_video
	bl	puts
	adr	x0,info+MKI_VNAME
	bl	puts
	adr	x0,m_sp
	bl	puts
	adr	x9,info
	ldr	x0,[x9,MKI_WIDTH]
	bl	print_dec
	adr	x0,m_x
	bl	puts
	adr	x9,info
	ldr	x0,[x9,MKI_HEIGHT]
	bl	print_dec
	adr	x0,m_sp
	bl	puts
	adr	x9,info
	ldr	x0,[x9,MKI_FPS1000]
	mov	x1,1000
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_ips
	bl	puts
	adr	x9,info
	ldr	x0,[x9,MKI_VOK]
	adr	x1,m_ok
	adr	x2,m_ko
	cmp	x0,0
	csel	x0,x1,x2,ne
	bl	puts
.nov:	adr	x9,info
	ldr	x0,[x9,MKI_ATRACK]
	tbnz	x0,63,.out
	adr	x0,m_sound
	bl	puts
	adr	x0,info+MKI_ANAME
	bl	puts
	adr	x0,m_sp
	bl	puts
	adr	x9,info
	ldr	x0,[x9,MKI_RATE]
	bl	print_dec
	adr	x0,m_hz
	bl	puts
	adr	x9,info
	ldr	x0,[x9,MKI_CHANNELS]
	bl	print_dec
	adr	x0,m_ch
	bl	puts
	adr	x9,info
	ldr	x0,[x9,MKI_AOK]
	adr	x1,m_ok
	adr	x2,m_ko
	cmp	x0,0
	csel	x0,x1,x2,ne
	bl	puts
.out:	epilogue

; x26 = pixels de la premiere image (x23 de large) : pts et pixel du centre
show_first:
	prologue
	adr	x0,m_first
	bl	puts
	adr	x9,frame
	ldr	x0,[x9,MKF_PTS]
	bl	print_dec
	adr	x0,m_center
	bl	puts
	adr	x9,frame
	ldr	x1,[x9,MKF_WIDTH]
	ldr	x2,[x9,MKF_HEIGHT]
	lsr	x3,x2,1				; (h / 2) * l + l / 2
	mul	x3,x3,x1
	add	x3,x3,x1,lsr 1
	ldr	w0,[x26,x3,lsl 2]
	adr	x1,hexbuf
	mov	x2,8
.hx:	ubfx	x3,x0,28,4
	lsl	w0,w0,4
	cmp	x3,10
	add	x4,x3,'0'
	add	x5,x3,'A'-10
	csel	x3,x4,x5,lo
	strb	w3,[x1],1
	subs	x2,x2,1
	b.ne	.hx
	strb	wzr,[x1]
	adr	x0,hexbuf
	bl	puts
	adr	x0,m_nl
	bl	puts
	epilogue

segment readable writeable
kit_data
		align 8
info		rb MKI_SIZE
frame		rb MKF_SIZE
stats		rb MKS_SIZE
path		rb 256
hexbuf		rb 16
abuf		rb MT_AUDIO_FRAMES * 4
m_usage		db 'usage : mediatest fichier [images]',10,0
m_ver		db 'mediatest : MediaKit ',0
m_dot		db '.',0
m_comma		db ',',0
m_nl		db 10,0
m_sp		db ' ',0
m_x		db 'x',0
m_cont		db 'mediatest : conteneur ',0
m_dur		db ', dur',0xC3,0xA9,'e ',0
m_msnl		db ' ms',10,0
m_ms		db ' ms : ',0
m_us		db ' us par image',10,0
m_video		db 'mediatest : vid',0xC3,0xA9,'o ',0
m_ips		db ' images/s',0
m_sound		db 'mediatest : son ',0
m_hz		db ' Hz ',0
m_ch		db ' canaux',0
m_ok		db ' (d',0xC3,0xA9,'cod',0xC3,0xA9,' par MediaKit)',10,0
m_ko		db ' (non d',0xC3,0xA9,'cod',0xC3,0xA9,' : AudioKit ou codec absent)',10,0
m_first		db 'mediatest : premi',0xC3,0xA8,'re image, pts ',0
m_center	db ' us, pixel du centre 0x',0
m_frames	db 'mediatest : ',0
m_in		db ' images en ',0
m_fps		db ' images/s (d',0xC3,0xA9,'codage + conversion XRGB8888)',10,0
m_avg		db 'mediatest : d',0xC3,0xA9,'codage seul ',0
m_amute		db 'mediatest : son muet (codec pour AudioKit)',10,0
m_audio		db 'mediatest : son ',0
m_audio2	db ' trames s16 st',0xC3,0xA9,'r',0xC3,0xA9,'o 48 kHz en ',0
m_seek		db 'mediatest : recherche au milieu -> ',0
m_after		db 'mediatest : image suivante, pts ',0
m_conv		db 'mediatest : conversion refus',0xC3,0xA9,'e',10,0
m_nomem		db 'mediatest : m',0xC3,0xA9,'moire insuffisante',10,0
m_openko	db 'mediatest : ouverture impossible : ',0
m_pass		db 'mediatest : PASS',10,0
m_fail		db 'mediatest : FAIL',10,0
