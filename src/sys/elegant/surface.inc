; ============================================================================
;  elegant/surface.inc - surface attachee a une fenetre (docs/DESIGN-ELEGANT.md
;  17) : le programme cree un tampon partage (BUF_CREATE), dit a Elegant que
;  le contenu de sa fenetre (ou une zone de ce contenu) est ce tampon
;  (WS_OP_SURFACE_ATTACH), puis signale les rectangles changes
;  (WS_OP_SURFACE_DAMAGE). Elegant compose directement depuis la surface
;  (XRGB8888 ou RGB565), sans recopie par le programme dans le tampon de la
;  fenetre : lecteur video, emulateurs, programmes SDL portes, Jet.
;
;  Une entree SF_* par place de fenetre (surfaces, indexee comme windows).
;  Le tampon est mappe chez Elegant (BUF_MAP : le programme l'y autorise par
;  BUF_GRANT) et rendu (BUF_FREE) au detachement ou a la fin de la fenetre.
; ============================================================================

SF_ADDR		= 0			; premier pixel chez Elegant (0 : pas de surface)
SF_BUF		= 8			; numero du tampon partage
SF_X		= 16			; zone dans le contenu (32 bits chacun)
SF_Y		= 20
SF_W		= 24
SF_H		= 28
SF_PITCH	= 32			; octets par ligne
SF_FMT		= 40			; WS_SF_*
SF_SIZE		= 64
SF_MAX_SIDE	= 4096
E_PERM		= -1

; x0 = fenetre -> x0 = son entree dans surfaces
surf_entry:
	adr	x1,windows
	sub	x0,x0,x1
	mov	x1,WIN_SIZE
	udiv	x0,x0,x1
	adr	x1,surfaces
	add	x0,x1,x0,lsl 6			; * SF_SIZE
	ret

; x0 = fenetre : son entree remise a zero (fenetre neuve, sans surface)
surf_clear:
	prologue
	bl	surf_entry
	stp	xzr,xzr,[x0]
	stp	xzr,xzr,[x0,16]
	stp	xzr,xzr,[x0,32]
	stp	xzr,xzr,[x0,48]
	epilogue

; x0 = fenetre : detache sa surface ; le tampon est demappe chez Elegant s'il
; ne sert plus a aucune autre fenetre
surf_detach:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	surf_entry
	ldr	x1,[x0,SF_ADDR]
	cbz	x1,.out
	ldr	x19,[x0,SF_BUF]
	str	xzr,[x0,SF_ADDR]
	adr	x1,surfaces			; encore attache ailleurs ?
	mov	x20,MAX_WINDOWS
.s:	ldr	x2,[x1,SF_ADDR]
	cbz	x2,.n
	ldr	x2,[x1,SF_BUF]
	cmp	x2,x19
	b.eq	.out
.n:	add	x1,x1,SF_SIZE
	subs	x20,x20,1
	b.ne	.s
	mov	x0,x19
	sys	SYS_BUF_FREE
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre -> x0 = entree, x1 = largeur, x2 = hauteur visibles de la
; surface (zone bornee au contenu ; <= 0 : rien a montrer)
surf_visible:
	mov	x3,x0
	ldr	x4,[x3,W_W]
	ldr	x5,[x3,W_H]
	adr	x1,windows
	sub	x0,x3,x1
	mov	x1,WIN_SIZE
	udiv	x0,x0,x1
	adr	x1,surfaces
	add	x0,x1,x0,lsl 6
	ldr	w6,[x0,SF_X]
	ldr	w7,[x0,SF_Y]
	ldr	w1,[x0,SF_W]
	ldr	w2,[x0,SF_H]
	sub	x4,x4,x6
	sub	x5,x5,x7
	cmp	x1,x4
	csel	x1,x4,x1,gt
	cmp	x2,x5
	csel	x2,x5,x2,gt
	ret

; -> x0 = la fenetre la plus en avant dont la surface couvre toute la zone
; clip_r (0 : aucune) : le fond et ce qui est dessous n'ont pas a etre dessines,
; ni son cadre ni son contenu (draw_framed ne fait que la surface et les boutons)
surf_cover:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x19,win_z
	adr	x0,win_count
	ldr	x20,[x0]
	mov	x22,0
	mov	x24,0
.layer:	mov	x21,0
.w:	cmp	x21,x20
	b.hs	.lnext
	ldr	x23,[x19,x21,lsl 3]
	add	x21,x21,1
	ldr	x0,[x23,W_MINI]
	cbnz	x0,.w
	mov	x0,x23
	bl	win_layer
	cmp	x0,x22
	b.ne	.w
	mov	x0,x23
	bl	surf_visible
	ldr	x3,[x0,SF_ADDR]
	cbz	x3,.w
	cmp	x1,0
	b.le	.w
	cmp	x2,0
	b.le	.w
	ldr	x3,[x23,W_X]			; la surface a l'ecran
	add	x3,x3,x6
	ldr	x4,[x23,W_Y]
	add	x4,x4,x7
	add	x1,x1,x3
	add	x2,x2,x4
	adr	x5,clip_r
	ldp	x8,x9,[x5]
	ldp	x10,x11,[x5,16]
	cmp	x8,x3
	b.lt	.w
	cmp	x9,x4
	b.lt	.w
	cmp	x10,x1
	b.gt	.w
	cmp	x11,x2
	b.gt	.w
	mov	x24,x23
	b	.w
.lnext:	add	x22,x22,1
	cmp	x22,3
	b.lo	.layer
	mov	x0,x24
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre, x1, x2 = origine de son contenu (coordonnees du tampon
; arriere) : dessine sa surface, decoupee par le clip
draw_surface:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x1
	mov	x22,x2
	bl	surf_visible
	ldr	x19,[x0,SF_ADDR]
	cbz	x19,.out
	cmp	x1,0
	b.le	.out
	cmp	x2,0
	b.le	.out
	mov	x4,x1
	mov	x5,x2
	ldr	w2,[x0,SF_X]
	ldr	w3,[x0,SF_Y]
	add	x2,x2,x21
	add	x3,x3,x22
	ldr	x1,[x0,SF_PITCH]
	ldr	x20,[x0,SF_FMT]
	mov	x0,x19
	cbnz	x20,.c565
	bl	bb_blit				; XRGB8888 : copie opaque
	b	.out
.c565:	bl	bb_blit565
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = source RGB565, x1 = pas source (octets), x2 = x, x3 = y (tampon
; arriere), x4 = largeur, x5 = hauteur : copie convertie en XRGB8888,
; decoupee par le clip
bb_blit565:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x0,x2
	mov	x1,x3
	mov	x2,x4
	mov	x3,x5
	bl	clip_rect
	cmp	x2,0
	b.le	.out
	cmp	x3,0
	b.le	.out
	sub	x4,x0,x21			; decalage dans la source
	sub	x5,x1,x22
	madd	x19,x5,x20,x19
	add	x19,x19,x4,lsl 1
	adr	x5,bb
	ldp	x5,x6,[x5]
	madd	x5,x1,x6,x5
	add	x5,x5,x0,lsl 2
.row:	mov	x7,x5
	mov	x8,x19
	mov	x9,x2
.px:	ldrh	w10,[x8],2
	ubfx	w11,w10,11,5			; rouge : 5 -> 8 bits
	lsl	w12,w11,3
	orr	w11,w12,w11,lsr 2
	ubfx	w12,w10,5,6			; vert : 6 -> 8 bits
	lsl	w13,w12,2
	orr	w12,w13,w12,lsr 4
	and	w13,w10,31			; bleu : 5 -> 8 bits
	lsl	w14,w13,3
	orr	w13,w14,w13,lsr 2
	lsl	w11,w11,16
	orr	w11,w11,w12,lsl 8
	orr	w11,w11,w13
	str	w11,[x7],4
	subs	x9,x9,1
	b.ne	.px
	add	x5,x5,x6
	add	x19,x19,x20
	subs	x3,x3,1
	b.ne	.row
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (rdpd, WS_OP_WIN_READ) x0 = fenetre, x1 = tampon de l'appelant (le
; rectangle lu, pas = largeur * 4), x2 = x, x3 = y (dans le contenu),
; x4 = largeur, x5 = hauteur : la surface par-dessus le contenu recopie
surf_read_over:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	adr	x6,bb				; garder bb, bb_pitch, le clip
	ldp	x20,x21,[x6]
	adr	x6,clip_r
	ldp	x22,x23,[x6]
	ldp	x24,x7,[x6,16]
	str	x7,[sp,-16]!
	stp	x2,x3,[x6]			; clip = le rectangle (coordonnees du
	add	x7,x2,x4			; contenu)
	add	x8,x3,x5
	stp	x7,x8,[x6,16]
	lsl	x7,x4,2				; bb = tampon - (y * pas + x * 4)
	msub	x1,x3,x7,x1
	sub	x1,x1,x2,lsl 2
	adr	x6,bb
	stp	x1,x7,[x6]
	mov	x0,x19
	mov	x1,0
	mov	x2,0
	bl	draw_surface
	adr	x6,bb
	stp	x20,x21,[x6]
	ldr	x7,[sp],16
	adr	x6,clip_r
	stp	x22,x23,[x6]
	stp	x24,x7,[x6,16]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; WS_OP_SURFACE_ATTACH : a id, b tampon (-1 : detacher), c x | y << 32 (zone
; dans le contenu), d largeur | hauteur << 32 ; donnees : +0 format (WS_SF_*,
; 32 bits), +4 pas (32 bits, 0 : largeur * octets du pixel), +8 decalage du
; premier pixel dans le tampon (64 bits) -> 0 ; reponse a = pid d'Elegant
; (E_PERM : le programme l'autorise par BUF_GRANT et recommence). Rattacher
; le meme tampon change seulement la zone, le format, le pas, le decalage
; (double tampon dans un seul tampon partage).
req_surface_attach:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	bl	req_win
	cbz	x0,.badf
	mov	x19,x0
	adr	x9,msg
	ldr	x20,[x9,WM_B]			; tampon
	ldr	x0,[x9,WM_C]
	mov	w21,w0				; x
	lsr	x22,x0,32			; y
	ldr	x0,[x9,WM_D]
	mov	w23,w0				; largeur
	lsr	x24,x0,32			; hauteur
	ldr	w25,[x9,WM_DATA]		; format
	ldr	w26,[x9,WM_DATA+4]		; pas
	ldr	x27,[x9,WM_DATA+8]		; decalage
	and	w25,w25,0xFF			; (sans WS_SF_NOREDRAW)
	sys	SYS_GETPID			; (reponse : le pid d'Elegant)
	adr	x9,msg
	str	x0,[x9,WM_A]
	mov	x28,0
	cmn	x20,1
	b.eq	.detach
	cmp	x25,WS_SF_RGB565
	b.hi	.inval
	mov	x28,4				; octets du pixel
	cbz	x25,.bpp
	mov	x28,2
.bpp:	cmp	x23,1
	b.lt	.inval
	cmp	x24,1
	b.lt	.inval
	cmp	x23,SF_MAX_SIDE
	b.gt	.inval
	cmp	x24,SF_MAX_SIDE
	b.gt	.inval
	cmp	x21,SF_MAX_SIDE
	b.hi	.inval
	cmp	x22,SF_MAX_SIDE
	b.hi	.inval
	mul	x0,x23,x28			; pas : au moins une ligne
	cbnz	x26,.pitch
	mov	x26,x0
.pitch:	cmp	x26,x0
	b.lo	.inval
	movl	x1,BUF_SLOT
	cmp	x26,x1
	b.hi	.inval
	cmp	x27,x1
	b.hs	.inval
	sub	x2,x24,1			; dernier octet lu : decalage + pas * (h - 1)
	madd	x2,x2,x26,x27			; + largeur * octets
	add	x2,x2,x0
	cmp	x2,x1
	b.hi	.inval
	str	x2,[sp,-16]!
	mov	x0,x19				; deja attache a ce tampon ?
	bl	surf_entry
	ldr	x1,[x0,SF_ADDR]
	cbz	x1,.map
	ldr	x1,[x0,SF_BUF]
	cmp	x1,x20
	b.eq	.mapped
	mov	x0,x19				; un autre : detache d'abord
	bl	surf_detach
.map:	mov	x0,x20
	sys	SYS_BUF_MAP
	tbnz	x0,63,.mapko
.mapped:
	movl	x1,BUF_VA_BASE			; (adresse du tampon chez Elegant)
	mov	x2,BUF_SLOT
	madd	x25,x20,x2,x1
	ldr	x0,[sp],16			; le tampon couvre-t-il la zone ?
	add	x0,x25,x0
	sub	x0,x0,1
	sys	SYS_VM_PAGEINFO
	tbz	x0,0,.small			; PI_PRESENT
	mov	x0,x19
	bl	surf_entry
	add	x1,x25,x27
	adr	x9,msg				; seul le decalage change, et WS_SF_NOREDRAW ?
	ldr	w2,[x9,WM_DATA]
	mov	x28,0
	tbz	w2,8,.store
	ldr	x3,[x0,SF_ADDR]
	cbz	x3,.store
	ldp	w3,w4,[x0,SF_X]
	ldp	w5,w6,[x0,SF_W]
	ldr	x7,[x0,SF_PITCH]
	eor	w3,w3,w21
	eor	w4,w4,w22
	eor	w5,w5,w23
	eor	w6,w6,w24
	eor	x7,x7,x26
	orr	w3,w3,w4
	orr	w3,w3,w5
	orr	w3,w3,w6
	orr	x3,x3,x7
	cbnz	x3,.store
	mov	x28,1
.store:	str	x1,[x0,SF_ADDR]
	str	x20,[x0,SF_BUF]
	stp	w21,w22,[x0,SF_X]
	stp	w23,w24,[x0,SF_W]
	str	x26,[x0,SF_PITCH]
	and	w2,w2,0xFF
	str	x2,[x0,SF_FMT]
	b	.show
.small:	mov	x0,x19				; tampon trop petit : refuse
	bl	surf_entry
	str	x20,[x0,SF_BUF]
	str	x25,[x0,SF_ADDR]
	mov	x0,x19
	bl	surf_detach
	b	.inval
.mapko:	add	sp,sp,16
	b	.out				; (E_PERM : pas autorise)
.detach:
	mov	x0,x19
	bl	surf_detach
.show:	ldr	w0,[x19,W_GEN]			; (rdpd : le contenu a change)
	add	w0,w0,1
	str	w0,[x19,W_GEN]
	cbnz	x28,.ok				; (WS_SF_NOREDRAW : DAMAGE suit)
	ldr	x0,[x19,W_MINI]
	cbnz	x0,.ok
	ldr	x0,[x19,W_X]			; tout le contenu a recomposer
	ldr	x1,[x19,W_Y]
	ldr	x2,[x19,W_W]
	ldr	x3,[x19,W_H]
	bl	dirty
.ok:	mov	x0,0
	b	.out
.badf:	mov	x0,E_BADF
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; WS_OP_SURFACE_DAMAGE : a id, b nombre de rectangles (0 : toute la surface) ;
; donnees : x, y, largeur, hauteur (16 bits chacun) par rectangle, dans la
; surface (WS_DAMAGE_MAX au plus) -> 0 : ce sont les zones sales du compositeur
req_surface_damage:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	req_win
	cbz	x0,.badf
	mov	x19,x0
	bl	surf_visible
	ldr	x2,[x0,SF_ADDR]
	cbz	x2,.inval
	ldr	w3,[x19,W_GEN]			; (rdpd : le contenu a change)
	add	w3,w3,1
	str	w3,[x19,W_GEN]
	ldr	x3,[x19,W_MINI]
	cbnz	x3,.ok
	cmp	x1,0
	b.le	.ok
	cmp	x2,0
	b.le	.ok
	mov	x21,x1				; largeur, hauteur visibles
	mov	x22,x2
	ldr	x3,[x19,W_X]			; x23, x24 = origine de la surface a l'ecran
	ldr	w4,[x0,SF_X]
	add	x23,x3,x4
	ldr	x3,[x19,W_Y]
	ldr	w4,[x0,SF_Y]
	add	x24,x3,x4
	adr	x9,msg
	ldr	x20,[x9,WM_B]
	cbnz	x20,.list
	mov	x0,x23				; toute la surface
	mov	x1,x24
	mov	x2,x21
	mov	x3,x22
	bl	dirty
	b	.ok
.list:	cmp	x20,WS_DAMAGE_MAX
	mov	x0,WS_DAMAGE_MAX
	csel	x20,x0,x20,hi
	adr	x19,msg+WM_DATA
.r:	ldrh	w0,[x19]			; x, y, l, h bornes a la surface visible
	ldrh	w1,[x19,2]
	ldrh	w2,[x19,4]
	ldrh	w3,[x19,6]
	add	x19,x19,8
	cmp	x0,x21
	b.hs	.nx
	cmp	x1,x22
	b.hs	.nx
	sub	x4,x21,x0
	cmp	x2,x4
	csel	x2,x4,x2,hi
	sub	x4,x22,x1
	cmp	x3,x4
	csel	x3,x4,x3,hi
	add	x0,x0,x23
	add	x1,x1,x24
	bl	dirty
.nx:	subs	x20,x20,1
	b.ne	.r
.ok:	mov	x0,0
	b	.out
.badf:	mov	x0,E_BADF
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
