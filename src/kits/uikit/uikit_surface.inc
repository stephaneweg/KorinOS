; ============================================================================
;  kits/uikit_surface.inc - UIKit : surface attachee a la fenetre (version 172 ;
;  docs/DESIGN-ELEGANT.md 17, ops WS_OP_SURFACE_ATTACH / WS_OP_SURFACE_DAMAGE).
;  Des pixels a soi dans un tampon partage (XRGB8888 ou RGB565) qu'Elegant
;  compose directement dans une zone du contenu de la fenetre : le programme
;  ecrit ses images (video, emulateur) puis signale les rectangles changes,
;  sans recopie dans le tampon de la fenetre ni ui_update.
;
;	mov x0,X ; mov x1,Y ; mov x2,L ; mov x3,H ; mov x4,WS_SF_XRGB8888
;	bl ui_surface			; -> poignee (0 : echec)
;	bl ui_surface_pixels		; -> x0 pixels, x1 pas, x2 l, x3 h
;	... dessin ...
;	mov x1,0 ; mov x2,0 ; mov x3,0 ; mov x4,0
;	bl ui_surface_damage		; (l <= 0 : toute la surface)
; ============================================================================

US_BUF		= 0			; numero du tampon partage
US_ADDR		= 8			; pixels chez le programme
US_X		= 16			; zone dans le contenu
US_Y		= 24
US_W		= 32
US_H		= 40
US_PITCH	= 48			; octets par ligne
US_FMT		= 56			; WS_SF_*
US_CAP		= 64			; octets du tampon
US_WIN		= 72			; fenetre (id d'Elegant)
US_SRC		= 80			; tampon d'ailleurs + 1 (0 : les pixels de la surface)
US_SRC_PITCH	= 88			; son pas
US_SRC_OFF	= 96			; decalage de son premier pixel
US_SIZE		= 112
US_MAX_SIDE	= 4096

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur (dans le contenu de la fenetre
; courante), x4 = format (WS_SF_XRGB8888 / WS_SF_RGB565) -> x0 = surface
; (0 : echec : taille, format, plus de tampon partage, Elegant ancien)
ui_surface:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	cmp	x23,WS_SF_RGB565
	b.hi	.fail
	cmp	x21,1
	b.lt	.fail
	cmp	x22,1
	b.lt	.fail
	cmp	x21,US_MAX_SIDE
	b.gt	.fail
	cmp	x22,US_MAX_SIDE
	b.gt	.fail
	mov	x0,US_SIZE
	bl	uk_alloc
	cbz	x0,.fail
	mov	x24,x0
	stp	x19,x20,[x24,US_X]
	stp	x21,x22,[x24,US_W]
	str	x23,[x24,US_FMT]
	stp	xzr,xzr,[x24,US_SRC]
	str	xzr,[x24,US_SRC_OFF]
	bl	uk_win
	str	x0,[x24,US_WIN]
	mov	x0,x24
	bl	uk_sf_alloc			; tampon a la taille de la zone
	cbz	x0,.free
	mov	x0,x24
	bl	uk_sf_attach
	tbnz	x0,63,.unbuf
	mov	x0,x24
	b	.out
.unbuf:	ldr	x0,[x24,US_BUF]
	sys	SYS_BUF_FREE
.free:	mov	x0,x24
	bl	uk_free
.fail:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = surface : un tampon partage pour sa zone (US_W, US_H, US_FMT) ->
; x0 = 1 (US_BUF, US_ADDR, US_PITCH, US_CAP remplis), 0 : echec
uk_sf_alloc:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,US_W]
	ldr	x2,[x19,US_FMT]
	mov	x3,4
	cbz	x2,.bpp
	mov	x3,2
.bpp:	mul	x1,x1,x3
	add	x1,x1,15			; lignes alignees sur 16 octets
	and	x1,x1,not 15
	str	x1,[x19,US_PITCH]
	ldr	x2,[x19,US_H]
	mul	x0,x1,x2
	movl	x1,BUF_SLOT
	cmp	x0,x1
	b.hi	.fail
	str	x0,[x19,US_CAP]
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.fail
	str	x0,[x19,US_BUF]
	movl	x1,BUF_VA_BASE
	mov	x2,BUF_SLOT
	madd	x0,x0,x2,x1
	str	x0,[x19,US_ADDR]
	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = surface : WS_OP_SURFACE_ATTACH (le tampon autorise a Elegant au besoin)
; -> x0 = 0, ou erreur < 0
uk_sf_attach:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,2				; deux essais (le second apres BUF_GRANT)
.again:	adr	x9,uk_msg
	ldr	x0,[x19,US_WIN]
	str	x0,[x9,WM_A]
	ldr	x0,[x19,US_SRC]			; (ui_surface_source)
	sub	x0,x0,1
	cmn	x0,1
	b.ne	.buf
	ldr	x0,[x19,US_BUF]
.buf:	str	x0,[x9,WM_B]
	ldr	x0,[x19,US_X]
	ldr	x1,[x19,US_Y]
	orr	x0,x0,x1,lsl 32
	str	x0,[x9,WM_C]
	ldr	x0,[x19,US_W]
	ldr	x1,[x19,US_H]
	orr	x0,x0,x1,lsl 32
	str	x0,[x9,WM_D]
	ldr	x0,[x19,US_FMT]
	ldr	x1,[x19,US_SRC_PITCH]
	cbz	x1,.own
	orr	x0,x0,WS_SF_NOREDRAW		; (ailleurs : image suivante, DAMAGE suit)
	b	.pitch
.own:	ldr	x1,[x19,US_PITCH]
.pitch:	stp	w0,w1,[x9,WM_DATA]
	ldr	x0,[x19,US_SRC_OFF]
	str	x0,[x9,WM_DATA+8]		; decalage
	mov	w0,16
	str	w0,[x9,WM_LEN]
	mov	x0,WS_OP_SURFACE_ATTACH
	bl	uk_call
	cmn	x0,1				; E_PERM : autoriser Elegant (pid rendu)
	b.ne	.out
	subs	x20,x20,1
	b.eq	.out
	ldr	x0,[x19,US_BUF]
	adr	x9,uk_msg
	ldr	x1,[x9,WM_A]
	sys	SYS_BUF_GRANT
	b	.again
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = surface, x1 = tampon partage ecrit ailleurs (-1 : les pixels de la
; surface), x2 = son pas (octets), x3 = decalage de la zone dans ce tampon :
; Elegant la compose depuis lui (autorise a Elegant) ; meme tampon, meme zone :
; rien n'est recompose, ui_surface_damage le dit. Les pixels de la surface
; restent, ui_surface_move suit -> x0 = 1, 0 : echec
ui_surface_source:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.fail
	add	x1,x1,1
	stp	x1,x2,[x19,US_SRC]
	str	x3,[x19,US_SRC_OFF]
	mov	x0,x19
	bl	uk_sf_attach
	tbnz	x0,63,.fail
	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = surface -> x0 = pixels, x1 = pas (octets), x2 = largeur, x3 = hauteur
ui_surface_pixels:
	cbz	x0,.none
	ldr	x1,[x0,US_PITCH]
	ldp	x2,x3,[x0,US_W]
	ldr	x0,[x0,US_ADDR]
	ret
.none:	mov	x1,0
	mov	x2,0
	mov	x3,0
	ret

; x0 = surface, x1 = x, x2 = y, x3 = largeur, x4 = hauteur (dans la surface ;
; largeur <= 0 : toute la surface) : la zone changee, a recomposer
ui_surface_damage:
	prologue
	cbz	x0,.out
	adr	x9,uk_msg
	ldr	x5,[x0,US_WIN]
	str	x5,[x9,WM_A]
	mov	x5,0
	cmp	x3,0
	b.le	.n
	strh	w1,[x9,WM_DATA]
	strh	w2,[x9,WM_DATA+2]
	strh	w3,[x9,WM_DATA+4]
	strh	w4,[x9,WM_DATA+6]
	mov	x5,1
.n:	str	x5,[x9,WM_B]
	lsl	w5,w5,3
	str	w5,[x9,WM_LEN]
	mov	x0,WS_OP_SURFACE_DAMAGE
	bl	uk_call
.out:	epilogue

; x0 = surface, x1 = rectangles (x, y, l, h : 16 bits chacun), x2 = nombre
; (WS_DAMAGE_MAX par message ; plus : en plusieurs messages)
ui_surface_damage_list:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbz	x19,.out
.msg:	cmp	x21,0
	b.le	.out
	mov	x22,x21
	cmp	x22,WS_DAMAGE_MAX
	mov	x0,WS_DAMAGE_MAX
	csel	x22,x0,x22,hi
	adr	x9,uk_msg
	ldr	x0,[x19,US_WIN]
	str	x0,[x9,WM_A]
	str	x22,[x9,WM_B]
	lsl	w0,w22,3
	str	w0,[x9,WM_LEN]
	add	x1,x9,WM_DATA
	mov	x2,x22
.cp:	ldr	x0,[x20],8
	str	x0,[x1],8
	subs	x2,x2,1
	b.ne	.cp
	mov	x0,WS_OP_SURFACE_DAMAGE
	bl	uk_call
	sub	x21,x21,x22
	b	.msg
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = surface, x1 = x, x2 = y, x3 = largeur, x4 = hauteur : la zone change
; de place ou de taille (fenetre redimensionnee) ; un tampon neuf si l'ancien
; est trop petit (pixels perdus) -> x0 = 1, 0 : echec (surface inchangee)
ui_surface_move:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	cbz	x19,.fail
	cmp	x3,1
	b.lt	.fail
	cmp	x4,1
	b.lt	.fail
	cmp	x3,US_MAX_SIDE
	b.gt	.fail
	cmp	x4,US_MAX_SIDE
	b.gt	.fail
	ldp	x20,x21,[x19,US_X]		; ancienne zone (pour revenir)
	ldp	x22,x23,[x19,US_W]
	stp	x1,x2,[x19,US_X]
	stp	x3,x4,[x19,US_W]
	ldr	x0,[x19,US_FMT]			; la place suffit-elle ?
	mov	x5,4
	cbz	x0,.bpp
	mov	x5,2
.bpp:	mul	x5,x3,x5
	ldr	x6,[x19,US_PITCH]
	cmp	x5,x6
	b.hi	.new
	ldr	x7,[x19,US_CAP]
	mul	x6,x6,x4
	cmp	x6,x7
	b.hi	.new
	mov	x0,x19
	bl	uk_sf_attach
	tbnz	x0,63,.back
	b	.ok
.new:	ldr	x24,[x19,US_BUF]		; tampon neuf, puis l'ancien rendu
	ldr	x0,[x19,US_PITCH]
	ldr	x1,[x19,US_CAP]
	ldr	x2,[x19,US_ADDR]
	stp	x0,x1,[sp,-16]!
	stp	x2,xzr,[sp,-16]!
	mov	x0,x19
	bl	uk_sf_alloc
	cbz	x0,.old
	mov	x0,x19
	bl	uk_sf_attach
	tbnz	x0,63,.newko
	add	sp,sp,32
	mov	x0,x24
	sys	SYS_BUF_FREE
	b	.ok
.newko:	ldr	x0,[x19,US_BUF]
	sys	SYS_BUF_FREE
.old:	str	x24,[x19,US_BUF]
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	str	x0,[x19,US_PITCH]
	str	x1,[x19,US_CAP]
	str	x2,[x19,US_ADDR]
.back:	stp	x20,x21,[x19,US_X]
	stp	x22,x23,[x19,US_W]
	mov	x0,x19
	bl	uk_sf_attach
.fail:	mov	x0,0
	b	.out
.ok:	mov	x0,1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = surface : detachee de la fenetre, tampon rendu, poignee liberee
ui_surface_free:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	adr	x9,uk_msg
	ldr	x0,[x19,US_WIN]
	str	x0,[x9,WM_A]
	mov	x0,-1
	str	x0,[x9,WM_B]
	str	wzr,[x9,WM_LEN]
	mov	x0,WS_OP_SURFACE_ATTACH
	bl	uk_call
	ldr	x0,[x19,US_BUF]
	sys	SYS_BUF_FREE
	mov	x0,x19
	bl	uk_free
.out:	ldr	x19,[sp],16
	epilogue
