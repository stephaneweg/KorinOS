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
