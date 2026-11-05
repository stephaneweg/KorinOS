; ============================================================================
;  elegant/snapshot.inc - l'ecran compose pour vncd et screenshot
;  (docs/RESEAU.md 5.4) : op WS_OP_SNAPSHOT (WS_OP_SCREEN du document : le
;  nom etait deja pris par la taille de l'ecran).
;
;  Chaque recomposition note sa zone et une generation (snap_hist, 32
;  dernieres). Un appelant (4 au plus) recoit un tampon partage de la
;  taille de l'ecran (cree au premier appel, accorde a son pid) ; il donne la
;  generation qu'il a deja : on recopie du tampon arriere (tel qu'a l'ecran,
;  curseur compris) l'union des zones changees depuis, et on rend la
;  nouvelle generation et ce rectangle. Generation 0 : tout l'ecran.
; ============================================================================

SNAP_HIST	= 32
SN_GEN		= 0
SN_X0		= 8
SN_Y0		= 16
SN_X1		= 24
SN_Y1		= 32
SN_SIZE		= 40
SNAP_CLI	= 4
SC_PID		= 0
SC_NUM		= 8
SC_ADR		= 16
SC_SIZE		= 24

; (compose, apres present) x0, x1, x2, x3 = union des zones recomposees
; (x0, y0, x1, y1), a l'ecran : une generation de plus
snap_record:
	mov	x9,x0
	mov	x10,x1
	mov	x11,x2
	mov	x12,x3
	adr	x0,snap_gen
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	and	x2,x1,SNAP_HIST-1
	mov	x3,SN_SIZE
	adr	x4,snap_hist
	madd	x4,x2,x3,x4
	str	x1,[x4,SN_GEN]
	stp	x9,x10,[x4,SN_X0]
	stp	x11,x12,[x4,SN_X1]
	ret

; WS_OP_SNAPSHOT : b = generation deja vue -> x0 = 1 si copie (0 : rien de
; neuf) ; a largeur, b hauteur, c generation, d numero du tampon (buf_map) ;
; donnees : x0, y0, x1, y1 de la zone recopiee (64 bits chacun)
req_snapshot:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x9,msg
	ldr	w0,[x9,WM_PID]
	bl	snap_client
	tbnz	x0,63,.out
	mov	x19,x0				; son entree
	adr	x9,msg
	adr	x0,scr_w
	ldp	x1,x2,[x0]
	stp	x1,x2,[x9,WM_A]
	ldr	x0,[x19,SC_NUM]
	str	x0,[x9,WM_D]
	ldr	x20,[x9,WM_B]			; deja vue
	adr	x0,snap_gen
	ldr	x21,[x0]
	str	x21,[x9,WM_C]
	mov	w0,32
	str	w0,[x9,WM_LEN]
	cmp	x20,x21
	b.eq	.rien
	; zone : union des generations > vue (ou tout l'ecran)
	mov	x1,0				; x0, y0 : l'ecran
	mov	x2,0
	adr	x0,scr_w
	ldp	x3,x4,[x0]
	cbz	x20,.copie
	sub	x0,x21,x20			; trop ancienne : tout
	cmp	x0,SNAP_HIST
	b.hs	.copie
	mov	x1,0x7FFFFFFF			; union vide
	mov	x2,0x7FFFFFFF
	mov	x3,0
	mov	x4,0
	add	x5,x20,1
.u:	cmp	x5,x21
	b.hi	.copie
	and	x6,x5,SNAP_HIST-1
	mov	x7,SN_SIZE
	adr	x8,snap_hist
	madd	x8,x6,x7,x8
	ldp	x6,x7,[x8,SN_X0]
	cmp	x6,x1
	csel	x1,x6,x1,lt
	cmp	x7,x2
	csel	x2,x7,x2,lt
	ldp	x6,x7,[x8,SN_X1]
	cmp	x6,x3
	csel	x3,x6,x3,gt
	cmp	x7,x4
	csel	x4,x7,x4,gt
	add	x5,x5,1
	b	.u
.copie:	adr	x9,msg
	stp	x1,x2,[x9,WM_DATA]
	stp	x3,x4,[x9,WM_DATA+16]
	sub	x3,x3,x1			; largeur, hauteur
	sub	x4,x4,x2
	cmp	x3,0
	b.le	.vide
	cmp	x4,0
	b.le	.vide
	adr	x5,bb				; source : tampon arriere
	ldp	x5,x6,[x5]
	madd	x5,x2,x6,x5
	add	x5,x5,x1,lsl 2
	adr	x0,scr_w			; destination : pas de largeur * 4
	ldr	x8,[x0]
	lsl	x8,x8,2
	ldr	x7,[x19,SC_ADR]
	madd	x7,x2,x8,x7
	add	x7,x7,x1,lsl 2
.row:	mov	x9,x5
	mov	x10,x7
	mov	x11,x3
.p4:	cmp	x11,4
	b.lo	.p1
	ldp	x12,x13,[x9],16
	stp	x12,x13,[x10],16
	sub	x11,x11,4
	b	.p4
.p1:	cbz	x11,.nx
	ldr	w12,[x9],4
	str	w12,[x10],4
	sub	x11,x11,1
	b	.p1
.nx:	add	x5,x5,x6
	add	x7,x7,x8
	subs	x4,x4,1
	b.ne	.row
	mov	x0,1
	b	.out
.vide:	adr	x9,msg
.rien:	stp	xzr,xzr,[x9,WM_DATA]
	stp	xzr,xzr,[x9,WM_DATA+16]
	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; WS_OP_WIN_THUMB : a id, b largeur | hauteur << 32, c decalage -> x0 = 0 ;
; la vignette (pas : largeur * 4) au decalage c du tampon de l'appelant, son
; contenu au-dela de la vignette est perdu (il sert de brouillon) ; msg c =
; taille du tampon, d = son numero. Le contenu (et la surface attachee, comme
; WIN_READ partie 3) est recopie au debut du tampon, puis reduit sur place par
; moyenne de chaque bloc : le commutateur du mode poche (src/sys/switcher).
req_win_thumb:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	adr	x9,msg
	ldr	w0,[x9,WM_PID]
	bl	snap_client
	tbnz	x0,63,.out
	mov	x19,x0				; son entree (SC_*)
	adr	x9,msg
	ldr	x0,[x9,WM_B]
	mov	w23,w0				; largeur, hauteur voulues
	lsr	x24,x0,32
	ldr	x25,[x9,WM_C]			; decalage
	ldr	x0,[x19,SC_NUM]			; reponse : le tampon, sa taille
	str	x0,[x9,WM_D]
	adr	x0,scr_w
	ldp	x0,x1,[x0]
	mul	x22,x0,x1
	lsl	x22,x22,2
	str	x22,[x9,WM_C]
	str	wzr,[x9,WM_LEN]
	ldr	x0,[x9,WM_A]
	mov	x1,-1
	bl	win_by_id
	cbz	x0,.badf
	mov	x28,x0
	ldr	x20,[x28,W_W]			; contenu borne a l'ecran
	ldr	x21,[x28,W_H]
	adr	x0,scr_w
	ldp	x0,x1,[x0]
	cmp	x20,x0
	csel	x20,x0,x20,hi
	cmp	x21,x1
	csel	x21,x1,x21,hi
	cbz	x23,.inval			; 1..512, pas plus que le contenu
	cbz	x24,.inval
	cmp	x23,512
	b.hi	.inval
	cmp	x24,512
	b.hi	.inval
	cmp	x23,x20
	csel	x23,x20,x23,hi
	cmp	x24,x21
	csel	x24,x21,x24,hi
	mul	x0,x23,x24			; la vignette tient au decalage ?
	add	x0,x25,x0,lsl 2
	cmp	x0,x22
	b.hi	.inval
	tst	x25,3
	b.ne	.inval
	ldr	x0,[x28,W_SURF]			; contenu -> debut du tampon (pas l * 4)
	ldr	x1,[x28,W_W]
	lsl	x1,x1,2
	ldr	x7,[x19,SC_ADR]
	mov	x2,x21
.row:	mov	x9,x0
	mov	x10,x7
	mov	x11,x20
.px:	ldr	w12,[x9],4
	str	w12,[x10],4
	subs	x11,x11,1
	b.ne	.px
	add	x0,x0,x1
	add	x7,x7,x20,lsl 2
	subs	x2,x2,1
	b.ne	.row
	mov	x0,x28				; sa surface par-dessus
	ldr	x1,[x19,SC_ADR]
	mov	x2,0
	mov	x3,0
	mov	x4,x20
	mov	x5,x21
	bl	surf_read_over
	; reduction : chaque pixel de la vignette = moyenne de son bloc (sur
	; place : un pixel ecrit est toujours avant ce qui reste a lire)
	ldr	x26,[x19,SC_ADR]		; x26 : source, x27 : destination (au
	mov	x27,x26				; debut, puis au decalage)
	mov	x2,0				; ty
.ty:	cmp	x2,x24
	b.hs	.ok
	mul	x3,x2,x21			; sy0, sy1
	udiv	x3,x3,x24
	add	x4,x2,1
	mul	x4,x4,x21
	udiv	x4,x4,x24
	cmp	x4,x3
	csinc	x4,x4,x3,hi
	mov	x5,0				; tx
.tx:	cmp	x5,x23
	b.hs	.tyn
	mul	x6,x5,x20			; sx0, sx1
	udiv	x6,x6,x23
	add	x7,x5,1
	mul	x7,x7,x20
	udiv	x7,x7,x23
	cmp	x7,x6
	csinc	x7,x7,x6,hi
	mov	x10,0				; sommes : rouge, vert, bleu
	mov	x11,0
	mov	x12,0
	mov	x8,x3
.by:	cmp	x8,x4
	b.hs	.bdone
	mul	x13,x8,x20
	add	x13,x13,x6
	add	x13,x26,x13,lsl 2
	sub	x14,x7,x6
.bx:	ldr	w15,[x13],4
	ubfx	x16,x15,16,8
	add	x10,x10,x16
	ubfx	x16,x15,8,8
	add	x11,x11,x16
	and	x16,x15,0xFF
	add	x12,x12,x16
	subs	x14,x14,1
	b.ne	.bx
	add	x8,x8,1
	b	.by
.bdone:	sub	x13,x4,x3			; pixels du bloc
	sub	x14,x7,x6
	mul	x13,x13,x14
	udiv	x10,x10,x13
	udiv	x11,x11,x13
	udiv	x12,x12,x13
	lsl	x10,x10,16
	orr	x10,x10,x11,lsl 8
	orr	x10,x10,x12
	mul	x13,x2,x23
	add	x13,x13,x5
	str	w10,[x27,x13,lsl 2]
	add	x5,x5,1
	b	.tx
.tyn:	add	x2,x2,1
	b	.ty
.ok:	cbz	x25,.ok0			; au decalage demande : recopiee depuis
	mul	x0,x23,x24			; la fin (les deux se chevauchent)
.mv:	sub	x0,x0,1
	ldr	w1,[x27,x0,lsl 2]
	add	x2,x27,x25
	str	w1,[x2,x0,lsl 2]
	cbnz	x0,.mv
.ok0:	mov	x0,0
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

; x0 = pid -> x0 = son entree (tampon cree au premier appel), < 0 si erreur
snap_client:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x20,snap_cli
	mov	x1,SNAP_CLI
	mov	x21,0				; place libre
.c:	ldr	x2,[x20,SC_PID]
	cmp	x2,x19
	b.eq	.trouve
	cbnz	x2,.n
	cbnz	x21,.n
	mov	x21,x20
.n:	add	x20,x20,SC_SIZE
	subs	x1,x1,1
	b.ne	.c
	cbz	x21,.plein
	adr	x0,scr_w			; un tampon de la taille de l'ecran
	ldp	x0,x1,[x0]
	mul	x0,x0,x1
	lsl	x0,x0,2
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	mov	x22,x0
	sys	SYS_BUF_MAP
	tbnz	x0,63,.libere
	str	x0,[x21,SC_ADR]
	mov	x0,x22
	mov	x1,x19
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.libere
	str	x22,[x21,SC_NUM]
	str	x19,[x21,SC_PID]
	mov	x0,x21
	b	.out
.libere:
	str	x0,[sp,-16]!
	mov	x0,x22
	sys	SYS_BUF_FREE
	ldr	x0,[sp],16
	b	.out
.trouve:
	mov	x0,x20
	b	.out
.plein:	mov	x0,-12				; (E_NOMEM)
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid termine : son tampon est rendu
snap_exit:
	prologue
	str	x19,[sp,-16]!
	adr	x19,snap_cli
	mov	x1,SNAP_CLI
.c:	ldr	x2,[x19,SC_PID]
	cmp	x2,x0
	b.ne	.n
	cbz	x2,.n
	str	xzr,[x19,SC_PID]
	stp	x0,x1,[sp,-16]!
	ldr	x0,[x19,SC_NUM]
	sys	SYS_BUF_FREE
	ldp	x0,x1,[sp],16
.n:	add	x19,x19,SC_SIZE
	subs	x1,x1,1
	b.ne	.c
	ldr	x19,[sp],16
	epilogue

; l'ecran a change de taille : tampons rendus (refaits au prochain appel, a la
; nouvelle taille, numero dans la reponse), tout est a recopier
snap_reset:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,snap_cli
	mov	x20,SNAP_CLI
.c:	ldr	x0,[x19,SC_PID]
	cbz	x0,.n
	str	xzr,[x19,SC_PID]
	ldr	x0,[x19,SC_NUM]
	sys	SYS_BUF_FREE
.n:	add	x19,x19,SC_SIZE
	subs	x20,x20,1
	b.ne	.c
	adr	x0,snap_gen			; (les zones gardees sont perimees)
	ldr	x1,[x0]
	add	x1,x1,SNAP_HIST
	str	x1,[x0]
	ldp	x19,x20,[sp],16
	epilogue

macro snapshot_data {
		align 8
snap_gen	dd 0
snap_cli	rb SNAP_CLI * SC_SIZE
snap_hist	rb SNAP_HIST * SN_SIZE
}
