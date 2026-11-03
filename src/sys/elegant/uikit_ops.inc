; ============================================================================
;  elegant/uikit_ops.inc - operations ajoutees pour les widgets de UIKit
;  (src/inc/ws_ext.inc) : presse-papiers, place d'une fenetre a l'ecran.
;
;  Presse-papiers du systeme (texte UTF-8).
;  Tenu par Elegant : tous les programmes graphiques lui parlent deja, il vit
;  aussi longtemps que la session, et le noyau n'a pas a garder de donnees des
;  programmes (noyau hybride : les appels 67-68 du noyau pourront partir).
;  UIKit (champs de texte : Ctrl+C, X, V) l'utilise par ui_clip_set /
;  ui_clip_get ; envoi et lecture en morceaux de WS_CLIP_PIECE octets.
; ============================================================================

; WS_OP_CLIP_SET : a decalage, b longueur totale (<= WS_CLIP_MAX), +40
; longueur du morceau ; donnees : le morceau -> 0 (E_INVAL : hors bornes)
req_clip_set:
	adr	x0,clip_buf			; tampon au premier envoi
	ldr	x0,[x0]
	cbnz	x0,.have
	mov	x0,WS_CLIP_MAX
	mov	x8,SYS_SBRK
	svc	0
	tbnz	x0,63,.nomem
	adr	x1,clip_buf
	str	x0,[x1]
.have:	adr	x9,msg
	ldr	x0,[x9,WM_A]			; decalage
	ldr	x1,[x9,WM_B]			; longueur totale
	ldr	w2,[x9,WM_LEN]			; morceau
	mov	x3,WS_CLIP_MAX
	cmp	x1,x3
	b.hi	.inval
	cmp	x2,WS_CLIP_PIECE
	b.hi	.inval
	add	x3,x0,x2
	cmp	x3,x1
	b.hi	.inval
	adr	x4,clip_len
	cbnz	x0,.copy
	str	x1,[x4]				; decalage 0 : nouveau contenu
	ldr	x5,[x4,8]			; clip_gen
	add	x5,x5,1
	str	x5,[x4,8]
.copy:	adr	x4,clip_buf
	ldr	x4,[x4]
	add	x4,x4,x0
	add	x5,x9,WM_DATA
.c:	cbz	x2,.ok
	ldrb	w6,[x5],1
	strb	w6,[x4],1
	sub	x2,x2,1
	b	.c
.ok:	mov	x0,0
	ret
.inval:	mov	x0,E_INVAL
	ret
.nomem:	mov	x0,E_NOMEM
	ret

; WS_OP_CLIP_GET : a decalage -> x0 = longueur du morceau ; b longueur
; totale, c generation ; donnees : le morceau
req_clip_get:
	adr	x9,msg
	ldr	x0,[x9,WM_A]
	adr	x4,clip_len
	ldp	x1,x3,[x4]			; longueur, generation
	stp	x1,x3,[x9,WM_B]
	subs	x2,x1,x0			; reste
	csel	x2,xzr,x2,lo
	cmp	x2,WS_CLIP_PIECE
	mov	x5,WS_CLIP_PIECE
	csel	x2,x5,x2,hi
	str	w2,[x9,WM_LEN]
	adr	x4,clip_buf
	ldr	x4,[x4]
	cbz	x4,.empty
	add	x4,x4,x0
	add	x5,x9,WM_DATA
	mov	x0,x2
.c:	cbz	x2,.ret
	ldrb	w6,[x4],1
	strb	w6,[x5],1
	sub	x2,x2,1
	b	.c
.ret:	ret
.empty:	mov	x0,0
	str	wzr,[x9,WM_LEN]
	ret

; WS_OP_WIN_POS : a id (une fenetre du programme) -> 0 ; a x, b y du contenu a
; l'ecran, c largeur, d hauteur (menus deroulants des widgets)
req_win_pos:
	prologue
	bl	req_win
	cbz	x0,.bad
	adr	x9,msg
	ldr	x1,[x0,W_X]
	ldr	x2,[x0,W_Y]
	stp	x1,x2,[x9,WM_A]
	ldr	x1,[x0,W_W]
	ldr	x2,[x0,W_H]
	stp	x1,x2,[x9,WM_C]
	mov	x0,0
	b	.out
.bad:	mov	x0,E_BADF
.out:	epilogue

; ---------------------------------------------------------------------------
;  Fenetres redimensionnables (WF_RESIZABLE)
; ---------------------------------------------------------------------------

; x0 = fenetre, x1 = largeur, x2 = hauteur : WEV_RESIZE a son programme
resize_post:
	prologue
	adr	x3,evbuf
	stp	xzr,xzr,[x3]
	stp	xzr,xzr,[x3,16]
	mov	w4,WEV_RESIZE
	str	w4,[x3]
	str	w1,[x3,EV_X]
	str	w2,[x3,EV_Y]
	mov	x1,x3
	bl	win_push
	epilogue

; x0 = fenetre, x1, x2 = curseur : appui sur son bord (droit, bas, coin)
resize_begin:
	ldr	x3,[x0,W_X]
	ldr	x4,[x0,W_W]
	add	x3,x3,x4
	cmp	x1,x3
	cset	x5,ge				; bit 0 : bord droit
	ldr	x3,[x0,W_Y]
	ldr	x4,[x0,W_H]
	add	x3,x3,x4
	cmp	x2,x3
	cset	x6,ge
	orr	x5,x5,x6,lsl 1			; bit 1 : bord bas
	cbz	x5,.ret				; (bords gauche, haut : rien)
	adr	x3,resize_win
	str	x0,[x3]
	adr	x3,resize_st
	stp	x1,x2,[x3]
	ldr	x4,[x0,W_W]
	ldr	x6,[x0,W_H]
	stp	x4,x6,[x3,16]
	str	x5,[x3,32]
.ret:	ret

; x1, x2 = curseur au relache : la nouvelle taille au programme
resize_end:
	prologue
	adr	x3,resize_win
	ldr	x0,[x3]
	str	xzr,[x3]
	adr	x3,resize_st
	ldp	x4,x5,[x3]			; depart
	ldp	x6,x7,[x3,16]			; taille
	ldr	x8,[x3,32]
	tbz	x8,0,.h
	sub	x9,x1,x4
	add	x6,x6,x9
.h:	tbz	x8,1,.clamp
	sub	x9,x2,x5
	add	x7,x7,x9
.clamp:	cmp	x6,100
	mov	x9,100
	csel	x6,x9,x6,lt
	cmp	x7,60
	mov	x9,60
	csel	x7,x9,x7,lt
	mov	x9,2048
	cmp	x6,x9
	csel	x6,x9,x6,gt
	cmp	x7,x9
	csel	x7,x9,x7,gt
	mov	x1,x6
	mov	x2,x7
	bl	resize_post
	epilogue

; x0 = fenetre : agrandie a toute la zone libre de l'ecran, ou rendue a sa
; place et sa taille d'avant (bouton agrandir)
win_maximize:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,windows			; sa place dans win_restore
	sub	x1,x19,x1
	lsr	x1,x1,WIN_SHIFT
	mov	x2,40
	adr	x20,win_restore
	madd	x20,x1,x2,x20
	mov	x0,x19
	bl	dirty_window
	ldr	x0,[x20,32]
	cbz	x0,.max
	str	xzr,[x20,32]			; rendue
	ldp	x0,x1,[x20]
	str	x0,[x19,W_X]
	str	x1,[x19,W_Y]
	ldp	x1,x2,[x20,16]
	b	.post
.max:	ldr	x0,[x19,W_X]			; agrandie
	ldr	x1,[x19,W_Y]
	stp	x0,x1,[x20]
	ldr	x0,[x19,W_W]
	ldr	x1,[x19,W_H]
	stp	x0,x1,[x20,16]
	mov	x0,1
	str	x0,[x20,32]
	bl	screen_work
	str	x0,[x19,W_X]
	str	x1,[x19,W_Y]
	mov	x1,x2
	mov	x2,x3
.post:	stp	x1,x2,[sp,-16]!
	mov	x0,x19
	bl	dirty_window
	ldp	x1,x2,[sp],16
	mov	x0,x19
	bl	resize_post
	ldp	x19,x20,[sp],16
	epilogue

;  Espaces de travail (WS_OP_DESK) : W_DESK = l'espace d'une fenetre + 1 ;
;  celles d'un autre espace sont cachees comme une fenetre reduite (bit
;  MINI_HORS_BUREAU de W_MINI : la composition, les clics, le focus sautent
;  tout W_MINI non nul).

; WS_OP_DESK : a espace (< 0 : garder), b nombre (0 : garder) -> courant |
; nombre << 8 | compteur << 16
req_desk:
	prologue
	adr	x9,msg
	ldr	x0,[x9,WM_A]
	ldr	x1,[x9,WM_B]
	bl	desk_set
	adr	x0,desk_cur
	ldr	x0,[x0]
	adr	x1,desk_count
	ldr	x1,[x1]
	orr	x0,x0,x1,lsl 8
	adr	x1,desk_gen
	ldr	x1,[x1]
	and	x1,x1,0x7FFF
	orr	x0,x0,x1,lsl 16
	epilogue

; x0 = espace (< 0 : garder), x1 = nombre (0 : garder) : les applique
desk_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x2,desk_count
	ldr	x3,[x2]
	mov	x20,x3				; (x20 : quelque chose change ?)
	cmp	x1,1
	b.lt	.cur
	cmp	x1,WS_DESK_MAX
	mov	x3,WS_DESK_MAX
	csel	x3,x1,x3,ls
	str	x3,[x2]
	adr	x4,windows			; ceux des espaces retires : sur le dernier
	mov	x5,MAX_WINDOWS
.w:	ldr	x6,[x4,W_DESK]
	cmp	x6,x3
	b.ls	.wn
	str	x3,[x4,W_DESK]
.wn:	add	x4,x4,WIN_SIZE
	subs	x5,x5,1
	b.ne	.w
.cur:	sub	x20,x20,x3			; (nombre change : non nul)
	adr	x2,desk_cur
	ldr	x4,[x2]
	cmp	x19,0
	csel	x19,x4,x19,lt
	cmp	x19,x3
	sub	x5,x3,1
	csel	x19,x5,x19,hs
	cmp	x19,x4
	b.ne	.go
	cbz	x20,.out
.go:	str	x19,[x2]
	bl	desk_apply
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = espace : le montre
desk_show:
	mov	x1,0
	b	desk_set

; les fenetres selon l'espace courant ; le focus, s'il etait ailleurs, a la
; plus en avant de celui-ci
desk_apply:
	prologue
	adr	x0,desk_gen
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	adr	x0,desk_cur
	ldr	x0,[x0]
	add	x0,x0,1
	adr	x4,windows
	mov	x5,MAX_WINDOWS
.w:	ldr	x6,[x4,W_DESK]
	ldr	x7,[x4,W_MINI]
	and	x7,x7,not MINI_HORS_BUREAU
	cbz	x6,.set
	cmp	x6,x0
	b.eq	.set
	orr	x7,x7,MINI_HORS_BUREAU
.set:	str	x7,[x4,W_MINI]
	add	x4,x4,WIN_SIZE
	subs	x5,x5,1
	b.ne	.w
	adr	x0,capture_win			; (plus de glisser vers une fenetre cachee)
	bl	desk_forget
	adr	x0,drag_win
	bl	desk_forget
	adr	x0,win_focus
	ldr	x0,[x0]
	cbz	x0,.focus
	ldr	x1,[x0,W_MINI]
	cbz	x1,.show
.focus:	bl	focus_top
.show:	bl	wins_changed
	bl	dirty_all
	epilogue

; WS_OP_WIN_DRAG : a id, fenetre du programme, bouton tenu depuis un appui dans
; son contenu : la suite du geste la deplace, comme sur la barre de titre
req_win_drag:
	prologue
	adr	x9,msg
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_PID]
	bl	win_by_id
	cbz	x0,.bad
	adr	x1,capture_win
	ldr	x2,[x1]
	cmp	x2,x0
	b.ne	.bad
	str	xzr,[x1]
	adr	x1,drag_win
	str	x0,[x1]
	adr	x1,cur_x
	ldp	x2,x3,[x1]
	ldr	x4,[x0,W_X]
	ldr	x5,[x0,W_Y]
	sub	x2,x2,x4
	sub	x3,x3,x5
	adr	x1,drag_dx
	stp	x2,x3,[x1]
	mov	x0,0
	b	.out
.bad:	mov	x0,E_INVAL
.out:	epilogue

; le rideau (WF_CURTAIN) : tant qu'une telle fenetre existe, les fenetres
; normales sont cachees (bit MINI_CURTAIN), sur tous les espaces ; le focus ne
; bouge pas, il leur revient quand le rideau tombe
curtain_apply:
	prologue
	mov	x2,0				; un rideau ?
	adr	x4,windows
	mov	x5,MAX_WINDOWS
.f:	ldr	x6,[x4,W_USED]
	cbz	x6,.fn
	ldr	x6,[x4,W_FLAGS]
	tbz	x6,WFB_CURTAIN,.fn
	mov	x2,1
.fn:	add	x4,x4,WIN_SIZE
	subs	x5,x5,1
	b.ne	.f
	adr	x0,curtain_on
	str	x2,[x0]
	adr	x4,windows
	mov	x5,MAX_WINDOWS
	mov	x3,WF_BACK or WF_TOP
.w:	ldr	x6,[x4,W_FLAGS]
	ldr	x7,[x4,W_MINI]
	and	x7,x7,not MINI_CURTAIN
	tst	x6,x3
	b.ne	.set
	orr	x7,x7,x2,lsl 2
.set:	str	x7,[x4,W_MINI]
	add	x4,x4,WIN_SIZE
	subs	x5,x5,1
	b.ne	.w
	adr	x0,capture_win
	bl	desk_forget
	adr	x0,drag_win
	bl	desk_forget
	bl	wins_changed
	bl	dirty_all
	epilogue

; x0 = variable (fenetre) : remise a 0 si sa fenetre est cachee (feuille)
desk_forget:
	ldr	x1,[x0]
	cbz	x1,.ret
	ldr	x1,[x1,W_MINI]
	cbz	x1,.ret
	str	xzr,[x0]
.ret:	ret

; x0 = entree clavier -> x0 = 1 si c'est Ctrl+Alt+Gauche ou Droite : l'espace
; voisin (en boucle)
desk_key:
	prologue
	ldr	x1,[x0,8]
	mov	x2,1
	cmp	x1,KEY_RIGHT
	b.eq	.mods
	mov	x2,-1
	cmp	x1,KEY_LEFT
	b.ne	.no
.mods:	ldr	x1,[x0,24]
	mov	x3,0x11				; (Ctrl, gauche ou droit)
	tst	x1,x3
	b.eq	.no
	mov	x3,0x44				; (Alt)
	tst	x1,x3
	b.eq	.no
	ldr	x1,[x0,16]
	cbz	x1,.yes				; (relache : avalee aussi)
	adr	x0,desk_cur
	ldp	x0,x1,[x0]			; desk_cur, desk_count
	add	x0,x0,x2
	cmp	x0,0
	sub	x2,x1,1
	csel	x0,x2,x0,lt
	cmp	x0,x1
	csel	x0,xzr,x0,hs
	bl	desk_show
.yes:	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	epilogue

; WS_OP_MAXIMIZE : a id -> 0 : comme le bouton agrandir
req_maximize:
	prologue
	bl	req_win
	cbz	x0,.bad
	ldr	x1,[x0,W_FLAGS]
	tbz	x1,WFB_RESIZABLE,.inval
	bl	win_maximize
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.bad:	mov	x0,E_BADF
.out:	epilogue

; WS_OP_RESIZE : a id, b largeur, c hauteur -> 0 ; a nouveau tampon, b / c
; decalages des cadres. Le contenu est blanc, les cadres invisibles jusqu'au
; dessin du programme ; l'ancien tampon est rendu.
req_resize:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	bl	req_win
	cbz	x0,.bad
	mov	x19,x0
	adr	x9,msg
	ldr	x21,[x9,WM_B]
	ldr	x22,[x9,WM_C]
	cmp	x21,1
	b.lt	.inval
	cmp	x22,1
	b.lt	.inval
	cmp	x21,2048
	b.gt	.inval
	cmp	x22,2048
	b.gt	.inval
	mul	x24,x21,x22			; contenu
	lsl	x24,x24,2
	mov	x23,0				; un cadre
	ldr	x0,[x19,W_FLAGS]
	tbnz	x0,WFB_FRAMELESS,.size
	add	x0,x21,2*FRAME_BORDER
	add	x1,x22,FRAME_TITLE_H+FRAME_BORDER
	mul	x23,x0,x1
	lsl	x23,x23,2
.size:	add	x0,x24,x23,lsl 1
	add	x0,x0,WD_SIZE+7			; en-tete WD_* en fin de tampon
	movl	x1,BUF_SLOT
	cmp	x0,x1
	b.hi	.inval
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	mov	x25,x0
	movl	x1,BUF_VA_BASE
	mov	x2,BUF_SLOT
	madd	x26,x25,x2,x1
	mov	x0,x26
	mov	x1,x24
	mov	x2,-1
	ldr	x3,[x19,W_FLAGS]		; (le fond : la couleur d'attente)
	tbz	x3,WFB_BACK,.white
	movl	x2,COL_DESK
	orr	x2,x2,x2,lsl 32
.white:	bl	fill64
	add	x0,x26,x24
	lsl	x1,x23,1
	movl	x2,TRANSPARENT
	orr	x2,x2,x2,lsl 32
	bl	fill64
	adr	x9,msg
	ldr	w1,[x9,WM_PID]
	mov	x0,x25
	sys	SYS_BUF_GRANT
	mov	x0,x19				; l'ancienne place est a recomposer
	bl	dirty_window
	ldr	x0,[x19,W_BUF]
	sys	SYS_BUF_FREE
	str	x21,[x19,W_W]
	str	x22,[x19,W_H]
	str	x26,[x19,W_SURF]
	str	x25,[x19,W_BUF]
	add	x0,x26,x24
	str	x0,[x19,W_CHR_A]
	add	x0,x0,x23
	str	x0,[x19,W_CHR_I]
	add	x0,x24,x23,lsl 1
	add	x0,x0,7
	and	x0,x0,not 7
	add	x0,x26,x0
	str	x0,[x19,W_DMG]
	ldr	w0,[x19,W_GEN]
	add	w0,w0,1
	str	w0,[x19,W_GEN]
	mov	x0,x19
	bl	dirty_window
	adr	x0,wins_dirty			; (rdpd, dock : la liste a change)
	mov	x1,1
	str	x1,[x0]
	ldr	x0,[x19,W_FLAGS]		; (barre, dock : autre echelle)
	tbz	x0,WFB_RESERVE,.reserved
	bl	reserve_update
.reserved:
	adr	x9,msg
	str	x25,[x9,WM_A]
	mov	x0,0
	mov	x1,0
	cbz	x23,.offs
	mov	x0,x24
	add	x1,x24,x23
.offs:	stp	x0,x1,[x9,WM_B]
	ldr	x0,[x19,W_DMG]			; d : l'en-tete WD_*
	sub	x0,x0,x26
	str	x0,[x9,WM_D]
	mov	x0,0
	b	.out
.bad:	mov	x0,E_BADF
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Formes du curseur, glisser-deposer
; ---------------------------------------------------------------------------

; WS_OP_POINTER_LOCK : a id (une fenetre du programme), b 1 : prend, 0 : rend -> 0
req_pointer_lock:
	prologue
	bl	req_win
	cbz	x0,.bad
	adr	x9,msg
	ldr	x2,[x9,WM_B]
	adr	x3,lock_win
	cbz	x2,.off
	str	x0,[x3]
	bl	pointer_dirty
	b	.ok
.off:	ldr	x4,[x3]
	cmp	x4,x0
	b.ne	.ok
	bl	pointer_unlock
.ok:	mov	x0,0
	b	.out
.bad:	mov	x0,E_BADF
.out:	epilogue

; plus de fenetre qui tienne le pointeur ; le curseur reparait
pointer_unlock:
	adr	x0,lock_win
	str	xzr,[x0]
	; (suite)
pointer_dirty:
	prologue
	adr	x0,cur_x
	ldp	x0,x1,[x0]
	sub	x0,x0,8
	sub	x1,x1,10
	mov	x2,20
	mov	x3,29
	bl	dirty
	epilogue

; WS_OP_CURSOR : a id (une fenetre du programme), b forme -> 0
req_cursor:
	prologue
	bl	req_win
	cbz	x0,.bad
	ldr	x1,[x0,W_ID]
	adr	x9,msg
	ldr	x2,[x9,WM_B]
	cmp	x2,UC_DRAG
	csel	x2,xzr,x2,eq
	cmp	x2,UC_NO
	csel	x2,xzr,x2,hi
	adr	x3,cur_req_win
	str	x1,[x3]
	adr	x3,cur_req
	str	x2,[x3]
	adr	x0,cur_x			; le curseur a redessiner
	ldp	x0,x1,[x0]
	sub	x0,x0,8
	sub	x1,x1,10
	mov	x2,20
	mov	x3,29
	bl	dirty
	mov	x0,0
	b	.out
.bad:	mov	x0,E_BADF
.out:	epilogue

; -> x0 = dessin de la forme du curseur, x1, x2 = point chaud : glisser en
; cours, bord d'une fenetre redimensionnable, forme demandee par la fenetre
; sous le pointeur ; sinon la fleche
cursor_pick:
	prologue
	str	x19,[sp,-16]!
	mov	x19,UC_ARROW
	adr	x0,drag_on
	ldr	x0,[x0]
	cbz	x0,.rs
	mov	x19,UC_DRAG
	b	.got
.rs:	adr	x0,resize_win
	ldr	x0,[x0]
	cbz	x0,.mv
	adr	x0,resize_st
	ldr	x5,[x0,32]			; ses bords
	b	.edge
.mv:	adr	x0,drag_win
	ldr	x0,[x0]
	cbz	x0,.hit
	mov	x19,UC_MOVE
	b	.got
.hit:	adr	x0,cur_x
	ldp	x0,x1,[x0]
	bl	win_hit
	cbz	x0,.got
	cmp	x1,Z_BORDER
	b.ne	.cl
	ldr	x2,[x0,W_FLAGS]
	tbz	x2,WFB_RESIZABLE,.got
	adr	x1,cur_x			; quels bords (comme resize_begin) ?
	ldp	x1,x2,[x1]
	ldr	x3,[x0,W_X]
	ldr	x4,[x0,W_W]
	add	x3,x3,x4
	cmp	x1,x3
	cset	x5,ge
	ldr	x3,[x0,W_Y]
	ldr	x4,[x0,W_H]
	add	x3,x3,x4
	cmp	x2,x3
	cset	x6,ge
	orr	x5,x5,x6,lsl 1
.edge:	mov	x19,UC_HRESIZE			; bit 0 : droit ; bit 1 : bas
	cmp	x5,1
	b.eq	.got
	mov	x19,UC_VRESIZE
	cmp	x5,2
	b.eq	.got
	mov	x19,UC_NWSE
	cmp	x5,3
	b.eq	.got
	mov	x19,UC_ARROW
	b	.got
.cl:	cmp	x1,Z_CLIENT
	b.ne	.got
	ldr	x1,[x0,W_ID]
	adr	x2,cur_req_win
	ldr	x2,[x2]
	cmp	x1,x2
	b.ne	.got
	adr	x2,cur_req
	ldr	x19,[x2]
.got:	adr	x0,cur_shape
	str	x19,[x0]
	adr	x0,cursor_shapes
	mov	x1,CURSOR_BYTES
	madd	x0,x19,x1,x0
	ldrb	w1,[x0]
	ldrb	w2,[x0,1]
	add	x0,x0,2
	ldr	x19,[sp],16
	epilogue

; WS_OP_CURSOR_GET -> x0 = forme UC_* dessinee
req_cursor_get:
	adr	x0,cur_shape
	ldr	x0,[x0]
	ret

; WS_OP_DRAG : a genre, +40 octets, donnees -> 0 (glisser en cours ; il
; finit au relache du bouton)
req_drag:
	adr	x9,msg
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_LEN]
	cmp	x1,WS_DRAG_MAX
	mov	x2,WS_DRAG_MAX
	csel	x1,x2,x1,hi
	adr	x2,drag_type
	str	x0,[x2]
	adr	x2,drag_len
	str	x1,[x2]
	adr	x2,drag_data
	add	x3,x9,WM_DATA
.c:	cbz	x1,.on
	ldrb	w4,[x3],1
	strb	w4,[x2],1
	sub	x1,x1,1
	b	.c
.on:	adr	x2,drag_on
	mov	x0,1
	str	x0,[x2]
	mov	x0,0
	ret

; WS_OP_DROP_GET : -> x0 = octets ; a genre, b octets ; donnees (le dernier
; glisser-deposer)
req_drop_get:
	adr	x9,msg
	adr	x0,drag_type
	ldr	x0,[x0]
	adr	x1,drag_len
	ldr	x1,[x1]
	stp	x0,x1,[x9,WM_A]
	str	w1,[x9,WM_LEN]
	adr	x2,drag_data
	add	x3,x9,WM_DATA
	mov	x0,x1
.c:	cbz	x1,.ret
	ldrb	w4,[x2],1
	strb	w4,[x3],1
	sub	x1,x1,1
	b	.c
.ret:	ret

; x0, x1 = curseur au relache : un glisser en cours se depose sur le
; contenu de la fenetre dessous (WEV_DROP)
drag_drop:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x2,drag_on
	ldr	x3,[x2]
	cbz	x3,.out
	str	xzr,[x2]
	mov	x19,x0
	mov	x20,x1
	bl	win_hit
	cbz	x0,.out
	cmp	x1,Z_CLIENT
	b.ne	.out
	adr	x9,evbuf
	stp	xzr,xzr,[x9]
	stp	xzr,xzr,[x9,16]
	mov	w2,WEV_DROP
	str	w2,[x9]
	ldr	x2,[x0,W_X]
	sub	x2,x19,x2
	str	w2,[x9,EV_X]
	ldr	x2,[x0,W_Y]
	sub	x2,x20,x2
	str	w2,[x9,EV_Y]
	adr	x2,drag_type
	ldr	x2,[x2]
	str	w2,[x9,16]
	adr	x2,drag_len
	ldr	x2,[x2]
	str	w2,[x9,20]
	mov	x1,x9
	bl	win_push
.out:	ldp	x19,x20,[sp],16
	epilogue

; formes du curseur (WS_OP_CURSOR, UC_*) : point chaud x, y, puis 12 x 19
; (« X » noir, « . » blanc, espace : transparent) ; 230 octets chacune
CURSOR_BYTES	= 230
cursor_shapes:
	db 0,0					; fleche
	db 'X           '
	db 'XX          '
	db 'X.X         '
	db 'X..X        '
	db 'X...X       '
	db 'X....X      '
	db 'X.....X     '
	db 'X......X    '
	db 'X.......X   '
	db 'X........X  '
	db 'X.........X '
	db 'X..........X'
	db 'X......XXXXX'
	db 'X...X..X    '
	db 'X..XX..X    '
	db 'X.X  X..X   '
	db 'XX   X..X   '
	db 'X     X..X  '
	db '      XXX   '
	db 4,9					; texte
	db ' .XX.XX.    '
	db ' ..XXX..    '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db '   .X.      '
	db ' ..XXX..    '
	db ' .XX.XX.    '
	db 5,0					; main
	db '    XX      '
	db '   X..X     '
	db '   X..X     '
	db '   X..X     '
	db '   X..XXXX  '
	db '   X..X..XX '
	db 'XX X..X..X.X'
	db 'X.XX.......X'
	db 'X..X.......X'
	db ' X.........X'
	db ' X.........X'
	db '  X........X'
	db '  X.......X '
	db '   X......X '
	db '   X.....X  '
	db '    XXXXXX  '
	db '            '
	db '            '
	db '            '
	db 6,9					; redim. horizontal
	db '            '
	db '            '
	db '            '
	db '            '
	db '            '
	db '            '
	db '  X      X  '
	db ' X.X    X.X '
	db 'X..XXXXXX..X'
	db 'X..........X'
	db 'X..XXXXXX..X'
	db ' X.X    X.X '
	db '  X      X  '
	db '            '
	db '            '
	db '            '
	db '            '
	db '            '
	db '            '
	db 5,9					; redim. vertical
	db '     X      '
	db '    X.X     '
	db '   X...X    '
	db '  X.....X   '
	db '  XXX.XXX   '
	db '    X.X     '
	db '    X.X     '
	db '    X.X     '
	db '    X.X     '
	db '    X.X     '
	db '    X.X     '
	db '    X.X     '
	db '    X.X     '
	db '    X.X     '
	db '  XXX.XXX   '
	db '  X.....X   '
	db '   X...X    '
	db '    X.X     '
	db '     X      '
	db 5,6					; attente
	db 'XXXXXXXXXX  '
	db 'X........X  '
	db ' X......X   '
	db ' X.XXXX.X   '
	db '  X.XX.X    '
	db '   X..X     '
	db '    XX      '
	db '   X..X     '
	db '  X....X    '
	db ' X..XX..X   '
	db ' X.XXXX.X   '
	db 'X.XXXXXX.X  '
	db 'XXXXXXXXXX  '
	db '            '
	db '            '
	db '            '
	db '            '
	db '            '
	db '            '
	db 5,9					; croix
	db '            '
	db '            '
	db '            '
	db '            '
	db '    .X.     '
	db '    .X.     '
	db '    .X.     '
	db '    .X.     '
	db '    .X.     '
	db 'XXXXXXXXXXX '
	db 'XXXXXXXXXXX '
	db '    .X.     '
	db '    .X.     '
	db '    .X.     '
	db '    .X.     '
	db '    .X.     '
	db '            '
	db '            '
	db '            '
	db 0,0					; glisser
	db 'X           '
	db 'XX          '
	db 'X.X         '
	db 'X..X        '
	db 'X...X       '
	db 'X....X      '
	db 'X.....X     '
	db 'X......X    '
	db 'X.......X   '
	db 'X........X  '
	db 'X.........X '
	db 'X..........X'
	db 'X......XXXXX'
	db 'X...X..X    '
	db 'X..XX.XXXXXX'
	db 'X.X  .X....X'
	db 'XX   .X....X'
	db '      X....X'
	db '      XXXXXX'
	db 5,9					; deplacer
	db '            '
	db '     X      '
	db '    X.X     '
	db '   X...X    '
	db '  XXX.XXX   '
	db '    X.X     '
	db '  X X.X X   '
	db ' XX X.X XX  '
	db 'X..XX.XX..X '
	db 'X.........X '
	db 'X..XX.XX..X '
	db ' XX X.X XX  '
	db '  X X.X X   '
	db '    X.X     '
	db '  XXX.XXX   '
	db '   X...X    '
	db '    X.X     '
	db '     X      '
	db '            '
	db 5,9					; coin nwse
	db '            '
	db '            '
	db 'XXXXXX      '
	db 'X....X      '
	db 'X...X       '
	db 'X...X       '
	db 'X..X.X      '
	db 'X.X X.X     '
	db 'XX   X.X    '
	db 'X     X.X   '
	db '       X.X X'
	db '        X.XX'
	db '         X.X'
	db '        X..X'
	db '        X...'
	db '       X...X'
	db '      X....X'
	db '      XXXXXX'
	db '            '
	db 6,9					; coin nesw
	db '            '
	db '            '
	db '      XXXXXX'
	db '      X....X'
	db '       X...X'
	db '       X...X'
	db '      X.X..X'
	db '     X.X X.X'
	db '    X.X   XX'
	db '   X.X     X'
	db 'X X.X       '
	db 'XX.X        '
	db 'X.X         '
	db 'X..X        '
	db '...X        '
	db 'X...X       '
	db 'X....X      '
	db 'XXXXXX      '
	db '            '
	db 5,9					; interdit
	db '            '
	db '            '
	db '            '
	db '   XXXXX    '
	db '  X.....X   '
	db ' X.XXXXX.X  '
	db 'X.XX  X..X.X'
	db 'X.X  X..X.X '
	db 'X.X X..X X.X'
	db 'X.XX..X  X.X'
	db 'X.X..X  XX.X'
	db ' X..X  X.X  '
	db ' X.XXXXX.X  '
	db '  X.....X   '
	db '   XXXXX    '
	db '            '
	db '            '
	db '            '
	db '            '
		align 4
