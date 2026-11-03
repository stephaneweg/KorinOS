; voronoy - le fond d'ecran du bureau Elegant (SD0:/sys/voronoy, lance par la
; session, comme le voronoy d'Onyx) : Voronoi « facettes douces »
; (docs/DESIGN-ELEGANT.md 5.1) : environ une cellule pour 28 000 pixels, germes
; espaces (rejet), couleurs de la palette du theme, plus clair en haut et plus
; sombre en bas, lisere clair le long des aretes. Fenetre sans cadre, sous
; toutes les autres. Relance : nouvelles cellules.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

NMAX	= 64
HORS_ECRAN	= 20				; germes un peu hors de l'ecran

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,quit
	adr	x0,scr
	ldp	x2,x3,[x0]
	mov	x0,0
	mov	x1,0
	adr	x4,title
	bl	tr_x4
	mov	x5,WF_FRAMELESS or WF_BACK or WF_NOFOCUS or WF_NOLIST
	bl	ui_window_ex
	tbnz	x0,63,quit
	bl	palette
	bl	seeds
	bl	paint
	bl	ui_update_all
.loop:	movl	x0,600000
	bl	ui_event
	cmp	x0,WEV_SCREEN
	b.eq	.screen
	cmp	x0,WEV_CLOSE
	b.ne	.loop
	b	quit
.screen:					; nouvelle resolution : nouvelles cellules
	adr	x0,scr
	bl	ui_screen
	tbnz	x0,63,.loop
	adr	x0,scr
	ldp	x0,x1,[x0]
	bl	ui_resize
	bl	seeds
	bl	paint
	bl	ui_update_all
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

; x0 = n -> x0 = hasard dans [0, n[
rand_n:
	prologue
	str	x0,[sp,-16]!
	bl	rand
	ldr	x1,[sp],16
	lsr	x0,x0,1
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	epilogue

; pal <- 5 couleurs : celles de Givre ou d'Onyx Noir
palette:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	ui_jetons
	ldr	w0,[x0,TJ_SOMBRE]
	adr	x1,pal_givre
	cbz	w0,.copy
	adr	x1,pal_noir
.copy:	adr	x2,pal
	ldp	x3,x4,[x1]
	stp	x3,x4,[x2]
	ldr	w3,[x1,16]
	str	w3,[x2,16]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; germes : nombre selon l'ecran, distance minimale entre eux, couleurs
seeds:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sys	SYS_TIME_MS
	movl	x1,0x9E3779B97F4A7C15
	eor	x0,x0,x1
	orr	x0,x0,1
	adr	x1,rng
	str	x0,[x1]
	adr	x0,ui_w
	ldp	x21,x22,[x0]			; largeur, hauteur
	mul	x0,x21,x22			; N = arrondi(l x h / 28 000), 8 au moins
	mov	x1,14000
	add	x0,x0,x1
	mov	x1,28000
	udiv	x23,x0,x1
	cmp	x23,8
	mov	x1,8
	csel	x23,x1,x23,lo
	cmp	x23,NMAX
	mov	x1,NMAX
	csel	x23,x1,x23,hi
	adr	x0,nseeds
	str	x23,[x0]
	mul	x0,x21,x22			; distance minimale² = 0,62² x l x h / N
	mov	x1,3844				; (0,62² = 3844 / 10 000)
	mul	x0,x0,x1
	mov	x1,10000
	udiv	x0,x0,x1
	udiv	x24,x0,x23
	mov	x19,0				; germe
.s:	cmp	x19,x23
	b.hs	.out
	mov	x25,200				; essais
.try:	add	x0,x21,2*HORS_ECRAN
	bl	rand_n
	sub	x26,x0,HORS_ECRAN			; x
	add	x0,x22,2*HORS_ECRAN
	bl	rand_n
	sub	x20,x0,HORS_ECRAN			; y
	sub	x25,x25,1
	cbz	x25,.take			; (trop d'essais : tant pis)
	mov	x9,0				; loin des germes deja places ?
	adr	x10,seed_x
	adr	x11,seed_y
.chk:	cmp	x9,x19
	b.hs	.take
	ldr	x0,[x10,x9,lsl 3]
	sub	x0,x26,x0
	ldr	x1,[x11,x9,lsl 3]
	sub	x1,x20,x1
	mul	x0,x0,x0
	madd	x0,x1,x1,x0
	cmp	x0,x24
	b.lo	.try
	add	x9,x9,1
	b	.chk
.take:	adr	x10,seed_x
	str	x26,[x10,x19,lsl 3]
	adr	x11,seed_y
	str	x20,[x11,x19,lsl 3]
	mov	x0,5				; couleur : une des 5 de la palette
	bl	rand_n
	adr	x1,pal
	ldr	w0,[x1,x0,lsl 2]
	adr	x1,seed_c
	str	w0,[x1,x19,lsl 2]
	mov	x1,134				; lisere : a peine plus clair
	bl	ui_tone
	adr	x1,seed_e
	str	w0,[x1,x19,lsl 2]
	add	x19,x19,1
	b	.s
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; peint le champ : couleur du germe le plus proche, nuancee selon la ligne ;
; lisere quand le pixel est a moins d'un pixel de la mediatrice des deux
; germes les plus proches : (d2² - d1²)² < 4 |g2 - g1|²
paint:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	adr	x0,ui_w
	ldp	x21,x22,[x0]			; largeur, hauteur
	adr	x0,ui_surf
	ldr	x24,[x0]			; pixel courant
	adr	x0,nseeds
	ldr	x23,[x0]
	mov	x20,0				; y
.y:	cmp	x20,x22
	b.hs	.out
	bl	row_colours			; couleurs des cellules pour cette ligne
	mov	x19,0				; x
.x:	cmp	x19,x21
	b.hs	.ynext
	mov	x25,-1				; d1², germe 1
	mov	x26,0
	mov	x27,-1				; d2², germe 2
	mov	x28,0
	mov	x9,0
	adr	x10,seed_x
	adr	x11,seed_y
.k:	ldr	x0,[x10,x9,lsl 3]
	sub	x0,x19,x0
	ldr	x1,[x11,x9,lsl 3]
	sub	x1,x20,x1
	mul	x0,x0,x0
	madd	x0,x1,x1,x0
	cmp	x0,x25
	b.hs	.second
	mov	x27,x25				; ancien premier -> second
	mov	x28,x26
	mov	x25,x0
	mov	x26,x9
	b	.kn
.second:
	cmp	x0,x27
	b.hs	.kn
	mov	x27,x0
	mov	x28,x9
.kn:	add	x9,x9,1
	cmp	x9,x23
	b.lo	.k
	sub	x0,x27,x25			; (d2² - d1²)²
	mul	x0,x0,x0
	ldr	x1,[x10,x26,lsl 3]		; 4 |g2 - g1|²
	ldr	x2,[x10,x28,lsl 3]
	sub	x1,x1,x2
	ldr	x2,[x11,x26,lsl 3]
	ldr	x3,[x11,x28,lsl 3]
	sub	x2,x2,x3
	mul	x1,x1,x1
	madd	x1,x2,x2,x1
	lsl	x1,x1,2
	adr	x2,row_c
	cmp	x0,x1
	b.hs	.fill
	adr	x2,seed_e
.fill:	ldr	w0,[x2,x26,lsl 2]
	str	w0,[x24],4
	add	x19,x19,1
	b	.x
.ynext:	add	x20,x20,1
	b	.y
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (paint) x20 = ligne : row_c <- couleur de chaque cellule a cette hauteur,
; ton(c, 136 - 30 y / h) (sombre : 128 - 24 y / h)
row_colours:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x20
	bl	ui_jetons
	ldr	w0,[x0,TJ_SOMBRE]
	mov	x1,30
	mov	x2,136
	cbz	w0,.lvl
	mov	x1,24
	mov	x2,128
.lvl:	mul	x1,x1,x21
	adr	x3,ui_h
	ldr	x3,[x3]
	udiv	x1,x1,x3
	sub	x22,x2,x1			; niveau
	mov	x19,0
	adr	x0,nseeds
	ldr	x20,[x0]
.c:	cmp	x19,x20
	b.hs	.out
	adr	x0,seed_c
	ldr	w0,[x0,x19,lsl 2]
	mov	x1,x22
	bl	ui_tone
	adr	x1,row_c
	str	w0,[x1,x19,lsl 2]
	add	x19,x19,1
	b	.c
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

pal_givre	dw 0x00D6DEE9, 0x00DCE3ED, 0x00D1DAE6, 0x00E1E7EF, 0x00CDD7E4
pal_noir	dw 0x001A1E26, 0x001D222B, 0x00181C23, 0x0020252F, 0x001B2029
title		db 'Fond',0
		align 4

include '../../inc/tr.inc'

segment readable writeable
kit_data
ui_data
		align 8
rng		dd 0
nseeds		dd 0
scr		rb 32
pal		rw 5
		align 8
seed_x		rd NMAX
seed_y		rd NMAX
seed_c		rw NMAX				; couleur de la cellule
seed_e		rw NMAX				; son lisere
row_c		rw NMAX				; couleurs de la ligne courante
