; ============================================================================
;  elegant/remote.inc - les fenetres une a une, pour rdpd (docs/RESEAU.md 5.4,
;  etape 12 ; le client Onyx Remote montre chaque fenetre dans une fenetre du
;  PC) :
;	WS_OP_WIN_LIST	toutes les fenetres dans l'ordre d'affichage, avec les
;			generations du contenu et du cadre, l'etat, le clavier
;	WS_OP_WIN_READ	un rectangle du contenu ou d'une copie du cadre (ou du
;			fond : WS_WIN_DESKTOP) dans le tampon de l'appelant
;	WS_OP_WIN_MOVE	placer une fenetre (deplacee sur le PC)
;	WS_OP_WIN_CLOSE	fermer comme la case de fermeture (WEV_CLOSE)
;  WS_OP_RAISE (core.inc) passe une fenetre devant. Le tampon de l'appelant
;  est celui de WS_OP_SNAPSHOT (snapshot.inc : de la taille de l'ecran, cree
;  au premier appel) : un meme programme ne se sert pas des deux a la fois.
; ============================================================================

; WS_OP_WIN_LIST : a premiere -> x0 = nombre de fenetres ; msg a = id de la
; fenetre qui recoit le clavier (0 : aucune) ; donnees : WE_PER_MSG entrees
; WE_* a partir de la premiere, dans l'ordre d'affichage (couches fond,
; normale, dessus ; du fond vers l'avant), reduites comprises
req_win_list:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	bl	key_target
	mov	x25,x0				; fenetre au clavier
	adr	x9,msg
	ldr	x21,[x9,WM_A]			; premiere
	adr	x22,msg+WM_DATA			; entree suivante
	mov	x19,0				; nombre
	mov	x23,0				; couche
.layer:	mov	x24,0				; place dans win_z
.w:	adr	x0,win_count
	ldr	x0,[x0]
	cmp	x24,x0
	b.hs	.lnext
	adr	x0,win_z
	ldr	x20,[x0,x24,lsl 3]
	add	x24,x24,1
	mov	x0,x20
	bl	win_layer
	cmp	x0,x23
	b.ne	.w
	sub	x0,x19,x21			; dans la page demandee ?
	add	x19,x19,1
	cmp	x0,WE_PER_MSG
	b.hs	.w
	ldr	x0,[x20,W_ID]
	str	w0,[x22,WE_ID]
	ldr	x0,[x20,W_PID]
	str	w0,[x22,WE_PID]
	ldr	x0,[x20,W_X]
	strh	w0,[x22,WE_X]
	ldr	x0,[x20,W_Y]
	strh	w0,[x22,WE_Y]
	ldr	x0,[x20,W_W]
	strh	w0,[x22,WE_W]
	ldr	x0,[x20,W_H]
	strh	w0,[x22,WE_H]
	ldr	x0,[x20,W_FLAGS]
	str	w0,[x22,WE_FLAGS]
	ldr	x1,[x20,W_MINI]			; etat : cachee, au clavier, reduite,
	cmp	x1,0				; son espace de travail
	cset	w0,ne
	lsl	w0,w0,1
	cmp	x20,x25
	cinc	w0,w0,eq
	and	x1,x1,MINI_REDUITE
	orr	x0,x0,x1,lsl 2
	ldr	x1,[x20,W_DESK]
	orr	x0,x0,x1,lsl WES_DESK
	str	w0,[x22,WE_STATE]
	ldr	w0,[x20,W_GEN]
	str	w0,[x22,WE_GEN]
	ldr	w0,[x20,W_CGEN]
	str	w0,[x22,WE_CGEN]
	add	x0,x22,WE_TITLE
	add	x1,x20,W_TITLE
	mov	x2,WE_SIZE-WE_TITLE
	bl	copy_title
	add	x22,x22,WE_SIZE
	b	.w
.lnext:	add	x23,x23,1
	cmp	x23,WIN_LAYERS
	b.lo	.layer
	adr	x9,msg
	mov	x0,0				; fenetre au clavier
	cbz	x25,.k
	ldr	x0,[x25,W_ID]
.k:	str	x0,[x9,WM_A]
	sub	x0,x22,x9			; longueur des donnees
	sub	x0,x0,WM_DATA
	str	w0,[x9,WM_LEN]
	mov	x0,x19
	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre, x1 = tampon (pas : x4 * 4), x2, x3 = coin, x4, x5 = taille du
; rectangle de son contenu : les boutons du cadre par-dessus, comme a l'ecran
; (draw_framed). Le client dessine le cadre sous le contenu : sans eux, il ne
; les montrerait pas. Pixels du cadre tout ou rien (feuille)
win_read_caps:
	ldr	x6,[x0,W_FLAGS]
	tbnz	x6,WFB_FRAMELESS,.ret
	ldr	x8,[x0,W_CHR_A]
	cbz	x8,.ret
	ldr	x7,[x0,W_W]
	add	x9,x7,2*FRAME_BORDER		; pas de l'image du cadre
	lsl	x9,x9,2
	mov	x10,FRAME_TITLE_H		; x8 : le point (0, 0) du contenu
	madd	x8,x10,x9,x8
	add	x8,x8,FRAME_BORDER*4
	sub	x10,x7,FRAME_CAP_MARGIN		; la zone, dans le rectangle
	sub	x11,x10,FRAME_CAP_W
	mov	x12,FRAME_CAP_MARGIN
	mov	x13,FRAME_CAP_MARGIN+FRAME_CAP_H
	add	x14,x2,x4
	add	x15,x3,x5
	cmp	x11,x2
	csel	x11,x2,x11,lt
	cmp	x10,x14
	csel	x10,x14,x10,gt
	cmp	x12,x3
	csel	x12,x3,x12,lt
	cmp	x13,x15
	csel	x13,x15,x13,gt
.y:	cmp	x12,x13
	b.ge	.ret
	mov	x14,x11
.x:	cmp	x14,x10
	b.ge	.ny
	madd	x15,x12,x9,x8
	ldr	w15,[x15,x14,lsl 2]
	lsr	w16,w15,24			; transparence
	cmp	w16,128
	b.hs	.nx
	sub	x6,x12,x3
	sub	x7,x14,x2
	madd	x6,x6,x4,x7
	and	w15,w15,0xFFFFFF
	str	w15,[x1,x6,lsl 2]
.nx:	add	x14,x14,1
	b	.x
.ny:	add	x12,x12,1
	b	.y
.ret:	ret

; WS_OP_WIN_READ : a id (WS_WIN_DESKTOP : le fond et les fenetres de la
; couche du fond, composes), b partie (0 contenu et boutons du cadre, 3 contenu
; seul, 1 cadre actif, 2 cadre inactif : image l + 2 bords, h + titre + bord),
; c x | y << 32,
; d l | h << 32 -> x0 = 0 (< 0 : erreur) ; le rectangle est recopie dans le
; tampon de l'appelant, ligne apres ligne (pas : l * 4) ; msg a = generation
; du contenu, b = du cadre, c = taille du tampon (octets), d = son numero
; (buf_map). l = 0 : rien de copie (le tampon et les generations).
req_win_read:
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
	ldr	x20,[x9,WM_A]			; id
	ldr	x21,[x9,WM_B]			; partie (x23 : 3, sans les boutons)
	cmp	x21,3
	cset	x23,eq
	csel	x21,xzr,x21,eq
	ldr	x0,[x9,WM_C]
	mov	w24,w0				; x
	lsr	x25,x0,32			; y
	ldr	x0,[x9,WM_D]
	mov	w26,w0				; l
	lsr	x27,x0,32			; h
	ldr	x0,[x19,SC_NUM]			; reponse : le tampon, sa taille
	str	x0,[x9,WM_D]
	adr	x0,scr_w
	ldp	x0,x1,[x0]
	mul	x22,x0,x1
	lsl	x22,x22,2
	str	x22,[x9,WM_C]
	stp	xzr,xzr,[x9,WM_A]
	str	wzr,[x9,WM_LEN]
	mul	x0,x26,x27			; le rectangle tient dans le tampon ?
	lsl	x0,x0,2
	cmp	x0,x22
	b.hi	.inval
	movl	x0,WS_WIN_DESKTOP
	cmp	x20,x0
	b.eq	.desk
	mov	x0,x20
	mov	x1,-1
	bl	win_by_id
	cbz	x0,.badf
	mov	x28,x0
	adr	x9,msg
	ldr	w0,[x28,W_GEN]
	ldr	w1,[x28,W_CGEN]
	stp	x0,x1,[x9,WM_A]
	cbz	x26,.ok
	cbz	x27,.ok
	ldr	x1,[x28,W_W]			; partie : image, largeur, hauteur
	ldr	x2,[x28,W_H]
	ldr	x0,[x28,W_SURF]
	cbz	x21,.part
	cmp	x21,2
	b.hi	.inval
	ldr	x3,[x28,W_FLAGS]
	tbnz	x3,WFB_FRAMELESS,.inval
	add	x1,x1,2*FRAME_BORDER
	add	x2,x2,FRAME_TITLE_H+FRAME_BORDER
	ldr	x0,[x28,W_CHR_A]
	cmp	x21,1
	b.eq	.part
	ldr	x0,[x28,W_CHR_I]
.part:	add	x3,x24,x26			; dedans ?
	cmp	x3,x1
	b.hi	.inval
	add	x3,x25,x27
	cmp	x3,x2
	b.hi	.inval
	lsl	x6,x1,2				; pas de l'image
	madd	x0,x25,x6,x0			; source : (x, y)
	add	x0,x0,x24,lsl 2
	ldr	x7,[x19,SC_ADR]			; destination
	lsl	x8,x26,2			; pas : l * 4
	str	x27,[sp,-16]!			; (hauteur : la surface)
.row:	mov	x9,x0
	mov	x10,x7
	mov	x11,x26
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
.nx:	add	x0,x0,x6
	add	x7,x7,x8
	subs	x27,x27,1
	b.ne	.row
	ldr	x27,[sp],16
	cbnz	x21,.ok				; contenu : sa surface attachee par-dessus
	mov	x0,x28
	ldr	x1,[x19,SC_ADR]
	mov	x2,x24
	mov	x3,x25
	mov	x4,x26
	mov	x5,x27
	bl	surf_read_over
	cbnz	x23,.ok
	mov	x0,x28
	ldr	x1,[x19,SC_ADR]
	mov	x2,x24
	mov	x3,x25
	mov	x4,x26
	mov	x5,x27
	bl	win_read_caps
	b	.ok
	; --- le fond : compose dans le tampon de l'appelant (le tampon arriere
	; et le clip changes le temps du dessin)
.desk:	cbz	x26,.ok
	cbz	x27,.ok
	cbnz	x21,.inval
	adr	x0,scr_w
	ldp	x0,x1,[x0]
	add	x2,x24,x26
	cmp	x2,x0
	b.hi	.inval
	add	x2,x25,x27
	cmp	x2,x1
	b.hi	.inval
	adr	x0,bb				; garder bb, bb_pitch, le clip
	ldp	x1,x2,[x0]
	stp	x1,x2,[sp,-16]!
	adr	x0,clip_r
	ldp	x1,x2,[x0]
	ldp	x3,x4,[x0,16]
	stp	x1,x2,[sp,-16]!
	stp	x3,x4,[sp,-16]!
	lsl	x2,x26,2			; bb = tampon - (y * pas + x * 4)
	ldr	x1,[x19,SC_ADR]
	msub	x1,x25,x2,x1
	sub	x1,x1,x24,lsl 2
	adr	x0,bb
	stp	x1,x2,[x0]
	adr	x0,clip_r
	add	x3,x24,x26
	add	x4,x25,x27
	stp	x24,x25,[x0]
	stp	x3,x4,[x0,16]
	pol_call POL_BACK
	mov	x23,0				; les fenetres du fond, dans l'ordre
.dw:	adr	x0,win_count
	ldr	x0,[x0]
	cmp	x23,x0
	b.hs	.ddone
	adr	x0,win_z
	ldr	x20,[x0,x23,lsl 3]
	add	x23,x23,1
	ldr	x0,[x20,W_MINI]
	cbnz	x0,.dw
	mov	x0,x20
	bl	win_layer
	cbnz	x0,.dw
	mov	x0,x20
	bl	draw_window
	b	.dw
.ddone:	adr	x0,clip_r
	ldp	x3,x4,[sp],16
	ldp	x1,x2,[sp],16
	stp	x1,x2,[x0]
	stp	x3,x4,[x0,16]
	ldp	x1,x2,[sp],16
	adr	x0,bb
	stp	x1,x2,[x0]
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

; WS_OP_WIN_MOVE : a id, b x, c y (le contenu, coordonnees de l'ecran) ->
; x0 = 0 ; fenetres normales seulement (ni fond ni dessus) ; la barre de
; titre reste sous la barre de menus, comme en la tirant
req_win_move:
	prologue
	str	x19,[sp,-16]!
	adr	x9,msg
	ldr	x0,[x9,WM_A]
	mov	x1,-1
	bl	win_by_id
	cbz	x0,.badf
	mov	x19,x0
	ldr	x1,[x19,W_FLAGS]
	tst	x1,WF_BACK or WF_TOP
	b.ne	.inval
	mov	x0,x19
	bl	dirty_window
	adr	x9,msg
	ldr	x2,[x9,WM_B]
	ldr	x3,[x9,WM_C]
	ldr	x1,[x19,W_FLAGS]
	tbnz	x1,WFB_FRAMELESS,.set
	adr	x4,area_top
	ldr	x4,[x4]
	add	x4,x4,FRAME_TITLE_H
	cmp	x3,x4
	csel	x3,x4,x3,lt
.set:	str	x2,[x19,W_X]
	str	x3,[x19,W_Y]
	mov	x0,x19
	bl	dirty_window
	mov	x0,0
	b	.out
.badf:	mov	x0,E_BADF
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldr	x19,[sp],16
	epilogue

; WS_OP_WIN_CLOSE : a id (fenetre de n'importe quel programme) -> x0 = 0 ;
; son programme recoit WEV_CLOSE, comme un clic sur la case de fermeture
req_win_close:
	prologue
	adr	x9,msg
	ldr	x0,[x9,WM_A]
	mov	x1,-1
	bl	win_by_id
	cbz	x0,.badf
	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w2,WEV_CLOSE
	str	w2,[x1]
	bl	win_push
	mov	x0,0
	b	.out
.badf:	mov	x0,E_BADF
.out:	epilogue
