; cibles - petit jeu d'adresse original
; Des cibles apparaissent au hasard et retrecissent ; un clic dessus rapporte
; d'autant plus de points qu'elle est petite. Trois cibles disparues sans
; avoir ete touchees : fin de partie. Meilleur score dans SD0:/targets.sco.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W	= 560
WIN_H	= 460
PLAY_H	= 400
MAX_T	= 8
T_SIZE	= 48			; octets par cible
FRAME	= 30			; ms par image
LIVES	= 3
BTN_X	= WIN_W-130
BTN_Y	= PLAY_H+14
BTN_W	= 116
BTN_H	= 32

; cible : +0 actif, +8 x, +16 y, +24 rayon (x256), +32 rayon initial, +40 couleur
TG_ON	= 0
TG_X	= 8
TG_Y	= 16
TG_R	= 24
TG_R0	= 32
TG_COL	= 40

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,ui_win				; variables de l'interface
	bl	ui_init
	mov	x0,-1
	mov	x1,-1
	mov	x2,WIN_W
	mov	x3,WIN_H
	adr	x4,title
	bl	tr_x4
	mov	x5,WF_GLYPH_LIGHT or WF_BODY_DARK
	bl	ui_window_ex
	tbnz	x0,63,quit
	sys	SYS_TIME_MS			; graine du generateur
	movl	x1,0x9E3779B97F4A7C15
	eor	x0,x0,x1
	adr	x1,rng
	str	x0,[x1]
	bl	load_best
	bl	new_game
.loop:	mov	x0,FRAME
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MOUSE
	b.ne	.frame
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,1
	b.ne	.frame
	bl	on_click
.frame:	sys	SYS_TIME_MS			; une image toutes les FRAME ms
	adr	x1,next_frame
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.loop
	add	x0,x0,FRAME
	str	x0,[x1]
	bl	step
	bl	draw
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; -> x0 = nombre pseudo-aleatoire (xorshift64)
rand:
	adr	x1,rng
	ldr	x0,[x1]
	eor	x0,x0,x0,lsl 13
	eor	x0,x0,x0,lsr 7
	eor	x0,x0,x0,lsl 17
	str	x0,[x1]
	ret

; x0 = borne -> x0 = aleatoire dans [0, borne[
rand_below:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	rand
	lsr	x0,x0,16
	udiv	x1,x0,x19
	msub	x0,x1,x19,x0
	ldr	x19,[sp],16
	epilogue

clear_targets:
	adr	x0,targets
	mov	x1,MAX_T*T_SIZE
.z:	str	xzr,[x0],8
	subs	x1,x1,8
	b.ne	.z
	ret

new_game:
	adr	x0,targets
	mov	x1,MAX_T*T_SIZE
.z:	str	xzr,[x0],8
	subs	x1,x1,8
	b.ne	.z
	adr	x0,game
	stp	xzr,xzr,[x0]			; score, manquees
	mov	x1,1
	str	x1,[x0,16]			; en cours
	str	xzr,[x0,24]			; images depuis la derniere cible
	mov	x1,900/FRAME
	str	x1,[x0,32]			; intervalle d'apparition (images)
	ret

; une image de jeu : apparition, retrecissement, cibles manquees
step:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,game
	ldr	x0,[x19,16]
	cbz	x0,.out
	ldr	x0,[x19,24]			; apparition ?
	add	x0,x0,1
	str	x0,[x19,24]
	ldr	x1,[x19,32]
	cmp	x0,x1
	b.lo	.shrink
	str	xzr,[x19,24]
	bl	spawn
.shrink:
	adr	x20,targets
	mov	x21,0
	ldr	x0,[x19]			; vitesse : plus rapide avec le score
	lsr	x0,x0,4
	add	x22,x0,40			; (x256 par image)
	cmp	x22,160
	mov	x0,160
	csel	x22,x0,x22,hi
.t:	ldr	x0,[x20,TG_ON]
	cbz	x0,.nt
	ldr	x0,[x20,TG_R]
	sub	x0,x0,x22
	str	x0,[x20,TG_R]
	cmp	x0,4*256
	b.ge	.nt
	str	xzr,[x20,TG_ON]			; disparue sans etre touchee
	ldr	x0,[x19,8]
	add	x0,x0,1
	str	x0,[x19,8]
	cmp	x0,LIVES
	b.lo	.nt
	str	xzr,[x19,16]			; fin de partie : terrain vide
	bl	save_best
	bl	clear_targets
.nt:	add	x20,x20,T_SIZE
	add	x21,x21,1
	cmp	x21,MAX_T
	b.lo	.t
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; place une nouvelle cible dans un emplacement libre
spawn:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,targets
	mov	x20,MAX_T
.f:	ldr	x0,[x19,TG_ON]
	cbz	x0,.got
	add	x19,x19,T_SIZE
	subs	x20,x20,1
	b.ne	.f
	b	.out
.got:	mov	x0,20				; rayon 26..45
	bl	rand_below
	add	x20,x0,26
	lsl	x0,x20,8
	str	x0,[x19,TG_R]
	str	x20,[x19,TG_R0]
	mov	x0,WIN_W-2*48			; centre a l'interieur de la zone
	bl	rand_below
	add	x0,x0,48
	str	x0,[x19,TG_X]
	mov	x0,PLAY_H-2*48
	bl	rand_below
	add	x0,x0,48
	str	x0,[x19,TG_Y]
	mov	x0,6
	bl	rand_below
	adr	x1,palette
	ldr	w0,[x1,x0,lsl 2]
	str	x0,[x19,TG_COL]
	mov	x0,1
	str	x0,[x19,TG_ON]
	adr	x0,game				; accelere les apparitions
	ldr	x1,[x0,32]
	cmp	x1,12
	b.ls	.out
	sub	x1,x1,1
	str	x1,[x0,32]
.out:	ldp	x19,x20,[sp],16
	epilogue

on_click:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,game
	ldr	x0,[x0,16]
	cbnz	x0,.play
	mov	x0,BTN_X			; partie terminee : Rejouer
	mov	x1,BTN_Y
	mov	x2,BTN_W
	mov	x3,BTN_H
	bl	ui_hit
	cbz	x0,.out
	bl	new_game
	b	.out
.play:	adr	x0,ui_ev
	ldrsw	x20,[x0,EV_X]
	ldrsw	x21,[x0,EV_Y]
	adr	x19,targets			; la plus recente d'abord (dessinee au-dessus)
	add	x19,x19,(MAX_T-1)*T_SIZE
	mov	x22,MAX_T
.t:	ldr	x0,[x19,TG_ON]
	cbz	x0,.nt
	ldr	x0,[x19,TG_X]
	sub	x0,x20,x0
	ldr	x1,[x19,TG_Y]
	sub	x1,x21,x1
	mul	x0,x0,x0
	madd	x0,x1,x1,x0
	ldr	x2,[x19,TG_R]
	lsr	x2,x2,8
	mul	x3,x2,x2
	cmp	x0,x3
	b.gt	.nt
	str	xzr,[x19,TG_ON]			; touchee : points = 60 - rayon
	mov	x0,60
	sub	x0,x0,x2
	adr	x1,game
	ldr	x2,[x1]
	add	x2,x2,x0
	str	x2,[x1]
	b	.out
.nt:	sub	x19,x19,T_SIZE
	subs	x22,x22,1
	b.ne	.t
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x0,0
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,PLAY_H
	movl	w4,0x00182230
	bl	ui_fill
	adr	x19,targets
	mov	x20,MAX_T
.t:	ldr	x0,[x19,TG_ON]
	cbz	x0,.nt
	ldr	x21,[x19,TG_R]
	lsr	x21,x21,8			; anneaux : couleur, blanc, couleur, blanc
	ldr	w22,[x19,TG_COL]
	mov	x2,x21
	mov	w3,w22
	bl	tdisc
	mov	x0,x21
	mov	x1,4
	mul	x2,x0,x1
	mov	x1,5
	udiv	x2,x2,x1
	movl	w3,0x00FFFFFF
	bl	tdisc
	mov	x0,x21
	mov	x1,3
	mul	x2,x0,x1
	mov	x1,5
	udiv	x2,x2,x1
	mov	w3,w22
	bl	tdisc
	mov	x0,x21
	mov	x1,5
	udiv	x2,x0,x1
	movl	w3,0x00FFFFFF
	bl	tdisc
.nt:	add	x19,x19,T_SIZE
	subs	x20,x20,1
	b.ne	.t
	adr	x0,game
	ldr	x0,[x0,16]
	cbnz	x0,.bar
	bl	draw_game_over
.bar:	mov	x0,0
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,PLAY_H
	bl	ui_update
	bl	draw_bar
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x19 = cible, x2 = rayon, w3 = couleur : disque plein
tdisc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldr	x20,[x19,TG_X]
	ldr	x21,[x19,TG_Y]
	mov	x22,x2
	mov	w23,w3
	neg	x24,x22
.y:	cmp	x24,x22
	b.gt	.out
	mul	x0,x22,x22			; demi-largeur = racine(r^2 - dy^2)
	msub	x0,x24,x24,x0
	scvtf	d0,x0
	fsqrt	d0,d0
	fcvtzs	x2,d0
	sub	x0,x20,x2
	add	x1,x21,x24
	lsl	x2,x2,1
	add	x2,x2,1
	mov	x3,1
	mov	w4,w23
	cmp	x1,PLAY_H			; ne pas deborder sur la barre
	b.ge	.ny
	bl	ui_fill
.ny:	add	x24,x24,1
	b	.y
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

draw_game_over:
	prologue
	sub	sp,sp,64
	adr	x0,s_over
	bl	tr_x0
	mov	x1,30
	adr	x2,ui_font_b
	bl	ui_text_width
	mov	x1,WIN_W
	sub	x1,x1,x0
	lsr	x1,x1,1
	adr	x0,s_over
	bl	tr_x0
	mov	x2,PLAY_H/2-10
	movl	w3,0x00FFFFFF
	mov	x4,30
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x0,sp
	adr	x1,s_final
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	adr	x0,game
	ldr	x0,[x0]
	bl	fmt_dec
	mov	x0,sp
	mov	x1,16
	adr	x2,ui_font
	bl	ui_text_width
	mov	x1,WIN_W
	sub	x1,x1,x0
	lsr	x1,x1,1
	mov	x0,sp
	mov	x2,PLAY_H/2+24
	movl	w3,0x00C8D2DE
	mov	x4,16
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	add	sp,sp,64
	epilogue

draw_bar:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,64
	mov	x0,0
	mov	x1,PLAY_H
	mov	x2,WIN_W
	mov	x3,WIN_H-PLAY_H
	jeton	w4,TJ_FACE
	bl	ui_fill
	mov	x0,sp				; "Score 123"
	adr	x1,s_score
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	adr	x0,game
	ldr	x0,[x0]
	bl	fmt_dec
	mov	x0,sp
	mov	x1,16
	mov	x2,PLAY_H+38
	jeton	w3,TJ_TEXTE1
	mov	x4,20
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x0,sp				; "Record 456"
	adr	x1,s_best
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	adr	x0,best
	ldr	x0,[x0]
	bl	fmt_dec
	mov	x0,sp
	mov	x1,170
	mov	x2,PLAY_H+36
	jeton	w3,TJ_TEXTE2
	mov	x4,15
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x19,0				; vies restantes : pastilles
	adr	x0,game
	ldr	x20,[x0,8]
.l:	cmp	x19,LIVES
	b.hs	.btn
	mov	x0,22
	mul	x0,x0,x19
	add	x0,x0,300
	mov	x1,PLAY_H+22
	mov	x2,16
	mov	x3,16
	movl	w4,0x00D9534F
	add	x5,x19,x20
	cmp	x5,LIVES
	b.lo	.c
	movl	w4,0x00CBD3DD
.c:	bl	ui_fill
	add	x19,x19,1
	b	.l
.btn:	adr	x0,game
	ldr	x0,[x0,16]
	cbnz	x0,.upd
	mov	x0,BTN_X
	mov	x1,BTN_Y
	mov	x2,BTN_W
	mov	x3,BTN_H
	adr	x4,s_again
	bl	tr_x4
	mov	x5,1
	bl	ui_button
.upd:	mov	x0,0
	mov	x1,PLAY_H
	mov	x2,WIN_W
	mov	x3,WIN_H-PLAY_H
	bl	ui_update
	add	sp,sp,64
	ldp	x19,x20,[sp],16
	epilogue

; --- meilleur score ----------------------------------------------------------
load_best:
	prologue
	str	x19,[sp,-16]!
	adr	x0,p_score
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,buf
	mov	x2,31
	sys	SYS_READ
	mov	x2,x0
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x1,buf
	mov	x3,0
	mov	x4,0
.d:	cmp	x4,x2
	b.ge	.st
	ldrb	w5,[x1,x4]
	sub	w5,w5,'0'
	cmp	w5,9
	b.hi	.st
	mov	x6,10
	madd	x3,x3,x6,x5
	add	x4,x4,1
	b	.d
.st:	adr	x0,best
	str	x3,[x0]
.out:	ldr	x19,[sp],16
	epilogue

save_best:
	prologue
	str	x19,[sp,-16]!
	adr	x0,game
	ldr	x0,[x0]
	adr	x1,best
	ldr	x2,[x1]
	cmp	x0,x2
	b.ls	.out
	str	x0,[x1]
	adr	x1,buf
	bl	fmt_dec
	mov	w1,10
	strb	w1,[x0],1
	adr	x1,buf
	sub	x19,x0,x1
	adr	x0,p_score
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x2,x19
	mov	x19,x0
	adr	x1,buf
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
.out:	ldr	x19,[sp],16
	epilogue


include '../../inc/tr.inc'

segment readable writeable
kit_data

title	db 'Cibles',0
p_score	db 'SD0:/targets.sco',0
s_score	db 'Score ',0
s_best	db 'Record ',0
s_again	db 'Rejouer',0
s_over	db 'Partie termin',0xC3,0xA9,'e',0
s_final	db 'Score final : ',0
	align 4
palette	dw 0x00E05A55, 0x003D8BE0, 0x0045B858, 0x00E8A33A, 0x00A04BC8, 0x0029B6C6
	align 8
rng	dd 0
best	dd 0
next_frame dd 0
game	dd 0, 0, 0, 0, 0		; score, manquees, en cours, images, intervalle
targets	rb MAX_T*T_SIZE
buf	rb 32
ui_data
