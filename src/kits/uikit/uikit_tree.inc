; ============================================================================
;  kits/uikit_tree.inc - UIKit : l'arborescence (UT_TREE, Onyx treeview.h)
;
;  Noeuds rangés en ordre prefixe : les elements (texte, donnee) et, en
;  parallele (UW_COLS), 16 octets par noeud : id (stable), id du parent (-1 :
;  racine), profondeur, drapeaux (bit 0 deplie, bit 1 a des enfants).
;  ui_tree_add rend l'id ; la valeur est l'id choisi (-1 : aucun). Clic sur
;  le triangle ou double-clic : deplier / replier ; double-clic sur une
;  feuille, Entree : UN_ACTIVATE ; haut / bas ; droite deplie, gauche replie
;  ou remonte au parent ; molette ; barre de defilement (enfant).
;  Champs : MIN prochain id, ANCHOR capacite du tableau des noeuds, POS
;  premiere ligne montree, HOT ligne survolee, AUX la barre.
; ============================================================================

TN_ID		= 0
TN_PARENT	= 4
TN_DEPTH	= 8
TN_FLAGS	= 12
UK_TREE_IND	= 18			; retrait par niveau

; x0..x3 = rectangle -> poignee
ui_tree:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_TREE
	bl	uk_w_new
	cbz	x0,.out
	mov	x19,x0
	mov	x0,-1
	str	x0,[x19,UW_VALUE]
	str	x0,[x19,UW_HOT]
	bl	uk_met
	ldr	x2,[x0,MT_SB]
	mov	x0,0
	mov	x1,0
	mov	x3,1
	mov	x4,0
	bl	ui_scrollbar
	cbz	x0,.lay
	str	x19,[x0,UW_PARENT]
	str	x0,[x19,UW_AUX]
.lay:	mov	x0,x19
	bl	uk_tr_layout
	mov	x0,x19
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = arbre, x1 = id -> x0 = son rang (-1 : inconnu) (feuille)
uk_tr_index:
	ldr	x2,[x0,UW_COLS]
	ldr	x3,[x0,UW_NITEMS]
	mov	x0,0
.l:	cmp	x0,x3
	b.hs	.no
	add	x4,x2,x0,lsl 4
	ldrsw	x4,[x4,TN_ID]
	cmp	x4,x1
	b.eq	.ret
	add	x0,x0,1
	b	.l
.no:	mov	x0,-1
.ret:	ret

; x0 = arbre, x1 = rang -> x0 = noeud (adresse) (feuille)
uk_tr_node:
	ldr	x0,[x0,UW_COLS]
	add	x0,x0,x1,lsl 4
	ret

; x0 = arbre, x1 = rang -> x0 = rang visible suivant (ses descendants sautes
; s'il est replie ; x0 = nombre de noeuds a la fin) (feuille)
uk_tr_next:
	ldr	x2,[x0,UW_COLS]
	ldr	x3,[x0,UW_NITEMS]
	add	x4,x2,x1,lsl 4
	ldr	w5,[x4,TN_FLAGS]
	ldr	w6,[x4,TN_DEPTH]
	add	x0,x1,1
	tbnz	w5,0,.ret			; deplie : le suivant
.s:	cmp	x0,x3
	b.hs	.ret
	add	x4,x2,x0,lsl 4
	ldr	w7,[x4,TN_DEPTH]
	cmp	w7,w6
	b.ls	.ret
	add	x0,x0,1
	b	.s
.ret:	ret

; x0 = arbre, x1 = ligne visible -> x0 = son rang (-1 : au-dela) ;
; ligne -1 -> x0 = nombre de lignes visibles
uk_tr_row:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0				; rang
	mov	x22,0				; ligne
.l:	ldr	x0,[x19,UW_NITEMS]
	cmp	x21,x0
	b.hs	.end
	cmp	x22,x20
	b.eq	.got
	mov	x0,x19
	mov	x1,x21
	bl	uk_tr_next
	mov	x21,x0
	add	x22,x22,1
	b	.l
.got:	mov	x0,x21
	b	.out
.end:	mov	x0,-1
	tbz	x20,63,.out
	mov	x0,x22
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = arbre, x1 = rang -> x0 = sa ligne visible (-1 : cache sous un parent
; replie)
uk_tr_line_of:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0
	mov	x22,0
.l:	ldr	x0,[x19,UW_NITEMS]
	cmp	x21,x0
	b.hs	.no
	cmp	x21,x20
	b.eq	.got
	b.hi	.no
	mov	x0,x19
	mov	x1,x21
	bl	uk_tr_next
	mov	x21,x0
	add	x22,x22,1
	b	.l
.got:	mov	x0,x22
	b	.out
.no:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = arbre : barre de defilement placee, plage = lignes visibles
uk_tr_layout:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,UW_AUX]
	cbz	x1,.s
	str	x1,[sp,8]
	bl	uk_met
	ldr	x3,[x0,MT_SB]
	ldr	x1,[sp,8]
	ldp	w4,w5,[x19,UW_X]
	ldp	w6,w7,[x19,UW_W]
	add	x4,x4,x6
	sub	x4,x4,x3
	sub	x4,x4,2
	add	x5,x5,1
	sub	x7,x7,2
	stp	w4,w5,[x1,UW_X]
	str	w3,[x1,UW_W]
	str	w7,[x1,UW_H]
.s:	mov	x0,x19
	bl	uk_tr_sync
	ldr	x19,[sp],16
	epilogue

uk_tr_sync:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x1,-1
	bl	uk_tr_row
	mov	x20,x0				; lignes visibles
	mov	x0,x19
	bl	uk_ls_rows
	sub	x2,x20,x0
	cmp	x2,0
	csel	x2,xzr,x2,lt
	ldr	x3,[x19,UW_POS]
	cmp	x3,x2
	csel	x3,x2,x3,gt
	cmp	x3,0
	csel	x3,xzr,x3,lt
	str	x3,[x19,UW_POS]
	ldr	x1,[x19,UW_AUX]
	cbz	x1,.out
	str	xzr,[x1,UW_MIN]
	str	x20,[x1,UW_MAX]
	str	x0,[x1,UW_PAGE]
	str	x3,[x1,UW_VALUE]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = arbre, x1 = id du parent (-1 : racine), x2 = libelle, x3 = donnee
; -> x0 = id du noeud (-1 : echec)
ui_tree_add:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x24,-1
	cbz	x19,.out
	mov	x0,x19				; place : apres les descendants du parent
	bl	uk_items_grow
	cbz	x0,.out
	ldr	x0,[x19,UW_ICAP]		; tableau des noeuds a la meme capacite
	ldr	x1,[x19,UW_ANCHOR]
	cmp	x1,x0
	b.hs	.cap
	lsl	x0,x0,4
	bl	uk_alloc
	cbz	x0,.out
	mov	x23,x0
	ldr	x1,[x19,UW_COLS]
	ldr	x2,[x19,UW_NITEMS]
	lsl	x2,x2,4
	bl	uk_memcpy
	ldr	x0,[x19,UW_COLS]
	bl	uk_free
	str	x23,[x19,UW_COLS]
	ldr	x0,[x19,UW_ICAP]
	str	x0,[x19,UW_ANCHOR]
.cap:	ldr	x23,[x19,UW_NITEMS]		; rang d'insertion (a la fin : racine)
	mov	x4,0				; profondeur
	tbnz	x20,63,.ins
	mov	x0,x19
	mov	x1,x20
	bl	uk_tr_index
	tbnz	x0,63,.out
	mov	x5,x0
	ldr	x6,[x19,UW_COLS]
	add	x7,x6,x5,lsl 4
	ldr	w8,[x7,TN_FLAGS]		; le parent a des enfants
	orr	w8,w8,2
	str	w8,[x7,TN_FLAGS]
	ldr	w4,[x7,TN_DEPTH]
	add	x23,x5,1
.d:	ldr	x0,[x19,UW_NITEMS]
	cmp	x23,x0
	b.hs	.dd
	add	x7,x6,x23,lsl 4
	ldr	w8,[x7,TN_DEPTH]
	cmp	w8,w4
	b.ls	.dd
	add	x23,x23,1
	b	.d
.dd:	add	x4,x4,1
.ins:	str	x4,[sp,-16]!
	mov	x0,x21
	bl	uk_strdup
	cbz	x0,.pop
	mov	x24,x0
	ldr	x2,[x19,UW_NITEMS]		; les suivants montent d'un rang
	sub	x2,x2,x23
	lsl	x2,x2,4
	ldr	x1,[x19,UW_ITEMS]
	add	x1,x1,x23,lsl 4
	add	x0,x1,16
	str	x2,[sp,8]
	bl	uk_memcpy_up
	ldr	x2,[sp,8]
	ldr	x1,[x19,UW_COLS]
	add	x1,x1,x23,lsl 4
	add	x0,x1,16
	bl	uk_memcpy_up
	ldr	x1,[x19,UW_ITEMS]
	add	x1,x1,x23,lsl 4
	stp	x24,x22,[x1]
	ldr	x1,[x19,UW_COLS]
	add	x1,x1,x23,lsl 4
	ldr	x24,[x19,UW_MIN]		; id neuf
	add	x0,x24,1
	str	x0,[x19,UW_MIN]
	str	w24,[x1,TN_ID]
	str	w20,[x1,TN_PARENT]
	ldr	x0,[sp]
	str	w0,[x1,TN_DEPTH]
	str	wzr,[x1,TN_FLAGS]
	ldr	x0,[x19,UW_NITEMS]
	add	x0,x0,1
	str	x0,[x19,UW_NITEMS]
	mov	x0,x19
	bl	uk_tr_sync
.pop:	add	sp,sp,16
.out:	mov	x0,x24
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = arbre, x1 = id, x2 = 1 deplie / 0 replie
ui_tree_expand:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	str	x2,[sp,8]
	bl	uk_tr_index
	tbnz	x0,63,.out
	mov	x1,x0
	mov	x0,x19
	bl	uk_tr_node
	ldr	w1,[x0,TN_FLAGS]
	and	w1,w1,not 1
	ldr	x2,[sp,8]
	cmp	x2,0
	cset	w2,ne
	orr	w1,w1,w2
	str	w1,[x0,TN_FLAGS]
	mov	x0,x19
	bl	uk_tr_sync
	mov	x0,x19
	bl	uk_w_refresh
.out:	ldr	x19,[sp],16
	epilogue

; x0 = arbre, x1 = id -> x0 = libelle (0 : inconnu), x1 = donnee
ui_tree_get:
	prologue
	str	x0,[sp,-16]!
	bl	uk_tr_index
	mov	x1,x0
	ldr	x0,[sp],16
	tbnz	x1,63,.no
	bl	ui_list_get
	b	.out
.no:	mov	x0,0
	mov	x1,0
.out:	epilogue

; x0 = arbre : plus de noeud
ui_tree_clear:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	ui_list_clear
	mov	x0,x19
	bl	uk_tr_sync
	ldr	x19,[sp],16
	epilogue

; x0 = arbre, x1 = rang : le change de place : deplie/replie (x2 = 1) ou
; choisi (x2 = 0) -> x0 = code
uk_tr_toggle:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	uk_tr_node
	ldr	w1,[x0,TN_FLAGS]
	eor	w1,w1,1
	str	w1,[x0,TN_FLAGS]
	mov	x0,x19
	bl	uk_tr_sync
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; x0 = arbre, x1 = rang : choisi et rendu visible -> x0 = UN_CHANGE (0 : deja)
uk_tr_select:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	uk_tr_node
	ldrsw	x1,[x0,TN_ID]
	ldr	x2,[x19,UW_VALUE]
	str	x1,[x19,UW_VALUE]
	cmp	x1,x2
	cset	x20,ne				; (x20 : change ?)
	ldr	x1,[x19,UW_VALUE]		; visible : ligne entre POS et POS + lignes
	mov	x0,x19
	bl	uk_tr_index
	mov	x1,x0
	mov	x0,x19
	bl	uk_tr_line_of
	tbnz	x0,63,.d
	str	x0,[sp,-16]!
	mov	x0,x19
	bl	uk_ls_rows
	ldr	x2,[sp],16
	ldr	x1,[x19,UW_POS]
	cmp	x2,x1
	b.ge	.b
	str	x2,[x19,UW_POS]
	b	.s
.b:	add	x3,x1,x0
	cmp	x2,x3
	b.lt	.s
	sub	x2,x2,x0
	add	x2,x2,1
	str	x2,[x19,UW_POS]
.s:	mov	x0,x19
	bl	uk_tr_sync
.d:	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
	cmp	x20,0
	csel	x0,xzr,x0,eq
	ldp	x19,x20,[sp],16
	epilogue

uk_tr_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_SURFACE
	bl	ui_fill
	bl	uk_met
	ldr	x24,[x0,MT_ROW]
	mov	x0,x19
	bl	uk_w_focused
	mov	x27,x0
	ldr	x25,[x19,UW_POS]		; premiere ligne
	mov	x0,x19
	mov	x1,x25
	bl	uk_tr_row
	mov	x26,x0				; rang
	mov	x28,x21				; y
.r:	tbnz	x26,63,.out
	ldr	x0,[x19,UW_NITEMS]
	cmp	x26,x0
	b.hs	.out
	add	x0,x28,x24
	add	x1,x21,x23
	cmp	x0,x1
	b.gt	.out
	mov	x0,x19				; fond : choisi, survole
	mov	x1,x26
	bl	uk_tr_node
	ldrsw	x1,[x0,TN_ID]
	ldr	x2,[x19,UW_VALUE]
	jet	w9,TJ_TEXTE1
	str	x9,[sp,-16]!
	cmp	x1,x2
	b.ne	.hot
	jet	w5,TJ_ACCENT_CLAIR
	cbz	x27,.box
	jet	w5,TJ_ACCENT
	jet	w9,TJ_ACCENT_INK
	str	x9,[sp]
	b	.box
.hot:	ldr	x2,[x19,UW_HOT]
	cmp	x25,x2
	b.ne	.txt
	jet	w5,TJ_CREUX
.box:	mov	w6,w5
	add	x0,x20,6
	mov	x1,x28
	sub	x2,x22,24
	mov	x3,x24
	mov	x4,5
	bl	ui_rbox
.txt:	mov	x0,x19				; triangle, libelle
	mov	x1,x26
	bl	uk_tr_node
	ldr	w2,[x0,TN_DEPTH]
	mov	x3,UK_TREE_IND
	mul	x2,x2,x3
	add	x2,x2,x20
	add	x2,x2,10			; x du triangle
	ldr	w3,[x0,TN_FLAGS]
	str	x2,[sp,8]
	tbz	w3,1,.label
	adr	x0,uk_s_tri_r			; « ▸ » replie, « ▾ » deplie
	tbz	w3,0,.tri
	adr	x0,uk_s_chev
.tri:	mov	x1,x2
	mov	x2,x28
	mov	x3,x24
	ldr	x4,[sp]
	stp	x0,x1,[sp,-16]!
	bl	uk_font_r
	mov	x5,x0
	ldp	x0,x1,[sp],16
	mov	x6,0
	mov	x7,13
	bl	uk_text_box
.label:	ldr	x0,[x19,UW_ITEMS]
	add	x0,x0,x26,lsl 4
	ldr	x0,[x0]
	ldr	x1,[sp,8]
	add	x1,x1,16
	mov	x2,x28
	mov	x3,x24
	ldr	x4,[sp]
	stp	x0,x1,[sp,-16]!
	bl	uk_met
	ldr	x7,[x0,MT_PX]
	bl	uk_font_r
	mov	x5,x0
	ldp	x0,x1,[sp],16
	add	x6,x20,x22
	sub	x6,x6,x1
	sub	x6,x6,24
	bl	uk_text_box
	add	sp,sp,16
	mov	x0,x19
	mov	x1,x26
	bl	uk_tr_next
	mov	x26,x0
	add	x28,x28,x24
	add	x25,x25,1
	b	.r
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris : triangle (deplier), clic (choisir), double-clic, survol
uk_tr_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x23,x4
	mov	x22,-1				; ligne sous le point
	cmp	x20,3
	b.eq	.hot
	stp	x2,x3,[sp,-16]!
	bl	uk_met
	ldr	x1,[x0,MT_ROW]
	ldp	x2,x3,[sp],16
	udiv	x22,x3,x1
	ldr	x0,[x19,UW_POS]
	add	x22,x22,x0
	mov	x0,x19
	mov	x1,x22
	bl	uk_tr_row
	mov	x24,x0				; rang
	tbz	x24,63,.in
	mov	x22,-1
.in:	cmp	x20,1
	b.ne	.hot
	tbnz	x24,63,.z
	mov	x0,x19
	mov	x1,x24
	bl	uk_tr_node
	ldr	w1,[x0,TN_FLAGS]
	tbz	w1,1,.pick			; a des enfants : triangle ou double-clic
	ldr	w2,[x0,TN_DEPTH]
	mov	x3,UK_TREE_IND
	mul	x2,x2,x3
	add	x2,x2,10
	sub	x3,x21,x2
	cmp	x3,16
	b.lo	.tog
	cmp	x23,2
	b.ne	.pick
.tog:	mov	x0,x19
	mov	x1,x24
	bl	uk_tr_toggle
	b	.z
.pick:	mov	x0,x19
	mov	x1,x24
	bl	uk_tr_select
	cmp	x23,2
	b.ne	.out
	mov	x0,UN_ACTIVATE
	b	.out
.hot:	ldr	x0,[x19,UW_HOT]
	cmp	x0,x22
	b.eq	.z
	str	x22,[x19,UW_HOT]
	mov	x0,x19
	bl	uk_w_refresh
.z:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; clavier : haut, bas, droite (deplie), gauche (replie, parent), Entree
uk_tr_key:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x1,[x19,UW_VALUE]		; rang et ligne du choisi
	bl	uk_tr_index
	mov	x21,x0
	cmp	w20,KEY_DOWN
	b.eq	.down
	cmp	w20,KEY_UP
	b.eq	.up
	tbnz	x21,63,.no
	mov	x0,x19
	mov	x1,x21
	bl	uk_tr_node
	mov	x22,x0
	ldr	w1,[x22,TN_FLAGS]
	cmp	w20,KEY_RIGHT
	b.ne	.l
	tbz	w1,1,.z
	tbnz	w1,0,.z
	b	.tog
.l:	cmp	w20,KEY_LEFT
	b.ne	.e
	tbz	w1,0,.par			; replie, ou va au parent
	tbnz	w1,1,.tog
.par:	ldrsw	x1,[x22,TN_PARENT]
	tbnz	x1,63,.z
	mov	x0,x19
	bl	uk_tr_index
	mov	x1,x0
	mov	x0,x19
	bl	uk_tr_select
	b	.out
.e:	cmp	w20,KEY_ENTER
	b.ne	.no
	tbnz	w1,1,.tog
	mov	x0,UN_ACTIVATE
	b	.out
.tog:	mov	x0,x19
	mov	x1,x21
	bl	uk_tr_toggle
	b	.z
.down:	mov	x0,0				; le suivant visible (ou le premier)
	tbnz	x21,63,.sel
	mov	x0,x19
	mov	x1,x21
	bl	uk_tr_next
	ldr	x1,[x19,UW_NITEMS]
	cmp	x0,x1
	b.hs	.z
	b	.sel
.up:	tbnz	x21,63,.z			; le precedent visible
	mov	x0,x19
	mov	x1,x21
	bl	uk_tr_line_of
	cbz	x0,.z
	sub	x1,x0,1
	mov	x0,x19
	bl	uk_tr_row
.sel:	ldr	x1,[x19,UW_NITEMS]
	cmp	x0,x1
	b.hs	.z
	mov	x1,x0
	mov	x0,x19
	bl	uk_tr_select
	b	.out
.z:	mov	x0,0
	b	.out
.no:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

uk_tr_wheel:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x2,3
	mul	x1,x1,x2
	ldr	x2,[x19,UW_POS]
	sub	x2,x2,x1
	str	x2,[x19,UW_POS]
	bl	uk_tr_sync
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

uk_tr_child:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x1,UW_VALUE]
	str	x0,[x19,UW_POS]
	mov	x0,x19
	bl	uk_tr_sync
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

uk_s_tri_r:	db 0xE2,0x96,0xB8,0			; « ▸ »
		align 4
