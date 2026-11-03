; ============================================================================
;  kits/uikit_list.inc - UIKit : la liste (UT_LIST), une ou plusieurs
;  colonnes (texte d'un element : colonnes separees par une tabulation),
;  en-tetes facultatifs, tri par clic sur un en-tete (numerique si les deux
;  champs commencent par un chiffre), colonnes redimensionnables (glisser le
;  bord d'un en-tete), selection, double-clic / Entree (UN_ACTIVATE),
;  molette, clavier (fleches, Debut, Fin, Page, premiere lettre), barre de
;  defilement superposee (un widget enfant). docs/DESIGN-ELEGANT.md 11.3.
;
;  Champs : VALUE = element choisi (-1), POS = premiere ligne montree,
;  HOT = ligne survolee, SORT = colonne triee (-1), ANCHOR = sens (1 :
;  decroissant), DRAG = colonne redimensionnee + 1, DRAG2 = x de depart,
;  AUX2 = sa largeur de depart, AUX = la barre de defilement.
; ============================================================================

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, x4 = drapeaux (UL_*) -> x0 =
; poignee (0 : echec)
ui_list:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_LIST
	bl	uk_w_new
	cbz	x0,.out
	mov	x20,x0
	str	x19,[x20,UW_STYLE]
	mov	x0,-1
	str	x0,[x20,UW_VALUE]
	str	xzr,[x20,UW_ANCHOR]
	bl	uk_met
	ldr	x0,[x0,MT_SB]
	mov	x1,0
	mov	x2,x0
	mov	x3,1
	mov	x0,0
	mov	x4,0
	bl	ui_scrollbar
	cbz	x0,.fail
	str	x20,[x0,UW_PARENT]
	str	x0,[x20,UW_AUX]
	tbz	x19,6,.lay			; UL_HSCROLL : barre horizontale, cachee
	bl	uk_met				; tant que les colonnes tiennent
	ldr	x3,[x0,MT_SB]
	mov	x0,0
	mov	x1,0
	mov	x2,1
	mov	x4,USB_HORIZONTAL
	bl	ui_scrollbar
	cbz	x0,.fail
	str	x20,[x0,UW_PARENT]
	str	x0,[x20,UW_HBAR]
	mov	w1,UWF_HIDDEN
	str	w1,[x0,UW_FLAGS]
.lay:	mov	x0,x20
	bl	uk_ls_layout
	mov	x0,x20
	b	.out
.fail:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste -> x0 = hauteur de l'en-tete (0 : sans)
uk_ls_hdr:
	ldr	x1,[x0,UW_STYLE]
	mov	x0,0
	tbz	x1,0,.ret			; UL_HEADER
	adr	x0,uk_mt
	ldr	x0,[x0,MT_HDR]
.ret:	ret

; x0 = liste -> x0 = lignes entieres visibles (>= 1)
uk_ls_rows:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	uk_met
	ldr	x20,[x0,MT_ROW]
	mov	x0,x19
	bl	uk_ls_hbar_h
	mov	x2,x0
	mov	x0,x19
	bl	uk_ls_hdr
	ldr	w1,[x19,UW_H]
	sub	x1,x1,x0
	sub	x1,x1,x2
	udiv	x0,x1,x20
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lt
	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste -> x0 = hauteur de sa barre horizontale montree, 0 sinon. Ne
; modifie que x0, x1, x3
uk_ls_hbar_h:
	ldr	x1,[x0,UW_HBAR]
	mov	x0,0
	cbz	x1,.ret
	ldr	w3,[x1,UW_FLAGS]
	tbnz	w3,0,.ret			; UWF_HIDDEN
	adr	x0,uk_mt
	ldr	x0,[x0,MT_SB]
.ret:	ret

; x0 = liste, x1 = colonne -> x0 = son x (depuis la liste, moins le decalage
; UL_HSCROLL), x1 = sa largeur (sans colonne definie : une seule, toute la
; largeur) (feuille)
uk_ls_colx:
	ldr	x2,[x0,UW_NCOLS]
	cbz	x2,.one
	ldr	x3,[x0,UW_COLS]
	mov	x4,0				; x
	mov	x5,0
.l:	cmp	x5,x1
	b.hs	.got
	add	x17,x3,x5,lsl 4
	ldr	w6,[x17]
	add	x4,x4,x6
	add	x5,x5,1
	b	.l
.got:	add	x17,x3,x1,lsl 4
	ldr	w1,[x17]
	ldr	x6,[x0,UW_HPOS]
	sub	x0,x4,x6
	ret
.one:	ldr	w1,[x0,UW_W]
	mov	x0,0
	ret

; x0 = liste -> x0 = nombre de colonnes montrees (>= 1)
uk_ls_ncols:
	ldr	x0,[x0,UW_NCOLS]
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lo
	ret

; x0 = liste : place la barre de defilement, borne la position, recopie la
; plage dans la barre
uk_ls_layout:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,UW_AUX]
	cbz	x20,.out
	bl	uk_met
	ldr	x21,[x0,MT_SB]
	mov	x0,x19
	bl	uk_ls_hdr
	mov	x22,x0
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	add	x0,x0,x2
	sub	x0,x0,x21
	sub	x0,x0,2
	add	x1,x1,x22
	add	x1,x1,1
	sub	x3,x3,x22
	sub	x3,x3,2
	stp	w0,w1,[x20,UW_X]
	str	w21,[x20,UW_W]
	str	w3,[x20,UW_H]
	ldr	x20,[x19,UW_HBAR]		; barre horizontale : au bas de la liste
	cbz	x20,.sync
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	add	x0,x0,1
	add	x1,x1,x3
	sub	x1,x1,x21
	sub	x1,x1,1
	sub	x2,x2,2
	stp	w0,w1,[x20,UW_X]
	stp	w2,w21,[x20,UW_W]
.sync:	mov	x0,x19
	bl	uk_ls_sync
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste UL_HSCROLL : la barre horizontale d'apres la largeur des colonnes
; (cachee si elles tiennent), le decalage borne, la barre verticale au-dessus
uk_ls_hsync:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,UW_HBAR]
	cbz	x20,.out
	ldr	x0,[x19,UW_NCOLS]		; largeur des colonnes
	ldr	x1,[x19,UW_COLS]
	mov	x21,0
.c:	cbz	x0,.got
	sub	x0,x0,1
	add	x17,x1,x0,lsl 4
	ldr	w2,[x17]
	add	x21,x21,x2
	b	.c
.got:	bl	uk_met
	ldr	x22,[x0,MT_SB]
	ldr	w0,[x19,UW_W]			; partie visible (la barre verticale en moins)
	sub	x0,x0,x22
	sub	x0,x0,4
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lt
	ldr	x1,[x19,UW_HPOS]		; decalage borne
	sub	x2,x21,x0
	cmp	x1,x2
	csel	x1,x2,x1,gt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	str	x1,[x19,UW_HPOS]
	str	xzr,[x20,UW_MIN]
	str	x21,[x20,UW_MAX]
	str	x0,[x20,UW_PAGE]
	str	x1,[x20,UW_VALUE]
	ldr	w3,[x20,UW_FLAGS]
	orr	w3,w3,UWF_HIDDEN
	cmp	x21,x0
	b.le	.fl
	and	w3,w3,not UWF_HIDDEN
.fl:	str	w3,[x20,UW_FLAGS]
	ldr	x20,[x19,UW_AUX]		; barre verticale
	mov	x0,x19
	bl	uk_ls_hdr
	ldr	w1,[x19,UW_H]
	sub	x1,x1,x0
	sub	x21,x1,2
	mov	x0,x19
	bl	uk_ls_hbar_h
	sub	x21,x21,x0
	str	w21,[x20,UW_H]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste : position bornee, plage de la barre = (0, elements, lignes)
uk_ls_sync:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	uk_ls_hsync
	mov	x0,x19
	bl	uk_ls_rows
	ldr	x1,[x19,UW_NITEMS]
	sub	x2,x1,x0			; position max
	cmp	x2,0
	csel	x2,xzr,x2,lt
	ldr	x3,[x19,UW_POS]
	cmp	x3,x2
	csel	x3,x2,x3,gt
	cmp	x3,0
	csel	x3,xzr,x3,lt
	str	x3,[x19,UW_POS]
	ldr	x20,[x19,UW_AUX]
	cbz	x20,.out
	str	xzr,[x20,UW_MIN]
	str	x1,[x20,UW_MAX]
	str	x0,[x20,UW_PAGE]
	str	x3,[x20,UW_VALUE]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste, x1 = element (-1 : aucun) : le choisit et le rend visible
uk_ls_select:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x2,[x19,UW_NITEMS]
	cmp	x1,x2
	sub	x3,x2,1
	csel	x1,x3,x1,ge
	cmp	x1,-1
	mov	x3,-1
	csel	x1,x3,x1,lt
	str	x1,[x19,UW_VALUE]
	mov	x20,x1
	tbnz	x20,63,.sync
	mov	x0,x19
	bl	uk_ls_rows
	ldr	x1,[x19,UW_POS]
	cmp	x20,x1
	b.ge	.below
	str	x20,[x19,UW_POS]
	b	.sync
.below:	add	x2,x1,x0
	cmp	x20,x2
	b.lt	.sync
	sub	x1,x20,x0
	add	x1,x1,1
	str	x1,[x19,UW_POS]
.sync:	mov	x0,x19
	bl	uk_ls_sync
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = numero de colonne -> x0 = debut du champ (fin : tabulation
; ou NUL ; champ absent : la fin du texte) (feuille)
uk_field_ptr:
.l:	cbz	x1,.ret
	ldrb	w2,[x0],1
	cbz	w2,.end
	cmp	w2,9
	b.ne	.l
	sub	x1,x1,1
	b	.l
.end:	sub	x0,x0,1
.ret:	ret

; x0 = texte, x1 = colonne -> x0 = le champ, copie terminee par NUL dans
; uk_fbuf (255 octets au plus)
uk_field:
	prologue
	bl	uk_field_ptr
	adr	x1,uk_fbuf
	mov	x2,0
.c:	ldrb	w3,[x0,x2]
	cbz	w3,.e
	cmp	w3,9
	b.eq	.e
	strb	w3,[x1,x2]
	add	x2,x2,1
	cmp	x2,255
	b.lo	.c
.e:	strb	wzr,[x1,x2]
	mov	x0,x1
	epilogue

; ---------------------------------------------------------------------------
;  Elements
; ---------------------------------------------------------------------------

; x0 = liste : place pour un element de plus -> x0 = 1 (0 : plus de memoire)
uk_items_grow:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,UW_NITEMS]
	ldr	x2,[x19,UW_ICAP]
	mov	x0,1
	cmp	x1,x2
	b.lo	.out
	lsl	x20,x2,1
	cmp	x20,16
	mov	x0,16
	csel	x20,x0,x20,lo
	lsl	x0,x20,4
	bl	uk_alloc
	cbz	x0,.out
	mov	x21,x0
	ldr	x1,[x19,UW_ITEMS]
	ldr	x2,[x19,UW_NITEMS]
	lsl	x2,x2,4
	bl	uk_memcpy
	ldr	x0,[x19,UW_ITEMS]
	bl	uk_free
	str	x21,[x19,UW_ITEMS]
	str	x20,[x19,UW_ICAP]
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget : rend les textes de ses elements, plus d'element
uk_items_clear:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,UW_NITEMS]
	ldr	x0,[x19,UW_KWFN]		; (liste virtuelle : pas d'elements a rendre)
	cbnz	x0,.out
.l:	cbz	x20,.out
	sub	x20,x20,1
	ldr	x0,[x19,UW_ITEMS]
	add	x17,x0,x20,lsl 4
	ldr	x0,[x17]
	bl	uk_free
	b	.l
.out:	str	xzr,[x19,UW_NITEMS]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste, x1 = texte (colonnes separees par des tabulations), x2 =
; donnee du programme -> x0 = numero de l'element (-1 : echec). Ajoute a la
; fin (ui_list_sort pour trier) ; rien n'est redessine (ui_w_draw).
ui_list_add:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,-1
	cbz	x19,.out
	bl	uk_items_grow
	cbz	x0,.out
	mov	x0,x20
	bl	uk_strdup
	cbz	x0,.out
	ldr	x1,[x19,UW_ITEMS]
	ldr	x22,[x19,UW_NITEMS]
	add	x1,x1,x22,lsl 4
	stp	x0,x21,[x1]
	add	x0,x22,1
	str	x0,[x19,UW_NITEMS]
	mov	x0,x19
	bl	uk_ls_sync
.out:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste : plus d'element (rien n'est redessine)
ui_list_clear:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	bl	uk_items_clear
	mov	x0,-1
	str	x0,[x19,UW_VALUE]
	str	x0,[x19,UW_HOT]
	str	xzr,[x19,UW_POS]
	mov	x0,x19
	bl	uk_ls_sync
.out:	ldr	x19,[sp],16
	epilogue

; x0 = liste -> x0 = nombre d'elements
ui_list_count:
	cbz	x0,.ret
	ldr	x0,[x0,UW_NITEMS]
.ret:	ret

; x0 = liste, x1 = numero -> x0 = son texte (0 : pas d'element), x1 = sa
; donnee
ui_list_get:
	cbz	x0,.no
	ldr	x2,[x0,UW_KWFN]			; virtuelle : la fonction du programme
	cbz	x2,.real
	ldr	x3,[x0,UW_NITEMS]
	cmp	x1,x3
	b.hs	.no
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	blr	x2
	mov	x1,0
	ldp	x29,x30,[sp],16
	ret
.real:
	ldr	x2,[x0,UW_NITEMS]
	cmp	x1,x2
	b.hs	.no
	ldr	x2,[x0,UW_ITEMS]
	add	x2,x2,x1,lsl 4
	ldp	x0,x1,[x2]
	ret
.no:	mov	x0,0
	mov	x1,0
	ret

; x0 = liste, x1 = numero, x2 = nouveau texte : le remplace (et redessine)
ui_list_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cbz	x19,.out
	ldr	x1,[x19,UW_NITEMS]
	cmp	x20,x1
	b.hs	.out
	mov	x0,x2
	bl	uk_strdup
	cbz	x0,.out
	ldr	x1,[x19,UW_ITEMS]
	add	x1,x1,x20,lsl 4
	ldr	x2,[x1]
	str	x0,[x1]
	mov	x0,x2
	bl	uk_free
	mov	x0,x19
	bl	uk_w_refresh
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste, x1 = numero : retire l'element (et redessine)
ui_list_remove:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cbz	x19,.out
	ldr	x1,[x19,UW_NITEMS]
	cmp	x20,x1
	b.hs	.out
	ldr	x0,[x19,UW_ITEMS]
	add	x17,x0,x20,lsl 4
	ldr	x0,[x17]
	bl	uk_free
	ldr	x0,[x19,UW_ITEMS]		; les suivants descendent
	add	x0,x0,x20,lsl 4
	add	x1,x0,16
	ldr	x2,[x19,UW_NITEMS]
	sub	x2,x2,x20
	sub	x2,x2,1
	lsl	x2,x2,4
	bl	uk_memcpy
	ldr	x0,[x19,UW_NITEMS]
	sub	x0,x0,1
	str	x0,[x19,UW_NITEMS]
	ldr	x1,[x19,UW_VALUE]		; selection : suit son element
	cmp	x1,x20
	b.lt	.sel
	b.gt	.dec
	mov	x1,-1
	b	.st
.dec:	sub	x1,x1,1
.st:	str	x1,[x19,UW_VALUE]
.sel:	mov	x0,-1
	str	x0,[x19,UW_HOT]
	mov	x0,x19
	bl	uk_ls_sync
	mov	x0,x19
	bl	uk_w_refresh
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste, x1 = titre, x2 = largeur, x3 = drapeaux (ULC_RIGHT) -> x0 =
; numero de la colonne (-1 : echec). Les colonnes se placent de gauche a
; droite ; sans colonne, une seule de toute la largeur.
ui_list_column:
	prologue
	bl	tr_x1
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x24,-1
	cbz	x19,.out
	ldr	x23,[x19,UW_NCOLS]
	add	x0,x23,1
	lsl	x0,x0,4
	bl	uk_alloc
	cbz	x0,.out
	ldr	x1,[x19,UW_COLS]
	lsl	x2,x23,4
	str	x0,[sp,-16]!
	bl	uk_memcpy
	ldr	x0,[x19,UW_COLS]
	bl	uk_free
	ldr	x0,[sp],16
	str	x0,[x19,UW_COLS]
	add	x24,x0,x23,lsl 4
	stp	w21,w22,[x24]
	mov	x0,x20
	bl	uk_strdup
	str	x0,[x24,8]
	add	x0,x23,1
	str	x0,[x19,UW_NCOLS]
	mov	x24,x23
	mov	x0,x19
	bl	uk_ls_sync
.out:	mov	x0,x24
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Tri
; ---------------------------------------------------------------------------

; x0 = champ -> x0 = 1 s'il commence par un chiffre (apres des espaces)
uk_field_isnum:
.s:	ldrb	w1,[x0],1
	cmp	w1,' '
	b.eq	.s
	sub	w1,w1,'0'
	cmp	w1,10
	cset	x0,lo
	ret

; x0 = champ -> x0 = son nombre entier (chiffres du debut ; les separateurs
; d'espace ou de point entre deux chiffres sont sautes : « 1 024 »)
uk_field_num:
	mov	x1,0
	mov	x3,10
.s:	ldrb	w2,[x0],1
	cmp	w2,' '
	b.eq	.s
.d:	sub	w2,w2,'0'
	cmp	w2,10
	b.hs	.sep
	madd	x1,x1,x3,x2
	ldrb	w2,[x0],1
	b	.d
.sep:	add	w2,w2,'0'			; separateur suivi d'un chiffre
	cmp	w2,' '
	b.eq	.nx
	cmp	w2,'.'
	b.ne	.end
.nx:	ldrb	w2,[x0]
	sub	w4,w2,'0'
	cmp	w4,10
	b.hs	.end
	add	x0,x0,1
	b	.d
.end:	mov	x0,x1
	ret

; x0, x1 = textes, x2 = colonne -> x0 = -1, 0, 1 (numerique si les deux
; champs commencent par un chiffre ; sinon ASCII sans casse)
uk_cmp_field:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x1,x2
	str	x2,[sp,-16]!
	bl	uk_field_ptr
	mov	x20,x0
	mov	x0,x19
	ldr	x1,[sp],16
	bl	uk_field_ptr
	mov	x19,x0				; x20 = a, x19 = b
	mov	x0,x20
	bl	uk_field_isnum
	cbz	x0,.str
	mov	x0,x19
	bl	uk_field_isnum
	cbz	x0,.str
	mov	x0,x20
	bl	uk_field_num
	str	x0,[sp,-16]!
	mov	x0,x19
	bl	uk_field_num
	ldr	x1,[sp],16
	cmp	x1,x0
	b.lo	.lt
	b.hi	.gt
.str:	ldrb	w0,[x20],1
	ldrb	w1,[x19],1
	cmp	w0,9
	csel	w0,wzr,w0,eq
	cmp	w1,9
	csel	w1,wzr,w1,eq
	sub	w2,w0,'A'			; minuscules
	cmp	w2,26
	b.hs	.a
	add	w0,w0,32
.a:	sub	w2,w1,'A'
	cmp	w2,26
	b.hs	.b
	add	w1,w1,32
.b:	cmp	w0,w1
	b.lo	.lt
	b.hi	.gt
	cbnz	w0,.str
	mov	x0,0
	b	.out
.lt:	mov	x0,-1
	b	.out
.gt:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste, x1 = colonne, x2 = 1 decroissant : trie les elements (tri par
; insertion, stable ; l'element choisi le reste) et redessine
ui_list_sort:
	mov	x3,0
	str	x3,[sp,-16]!
	b	uk_ls_sort_go
; x0 = liste, x1 = colonne, x2 = 1 decroissant, x3 = premier element trie
; (ceux d'avant ne bougent pas : dossiers puis fichiers)
uk_ls_sort_range:
	str	x3,[sp,-16]!
uk_ls_sort_go:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	str	x1,[x19,UW_SORT]
	str	x2,[x19,UW_ANCHOR]
	ldr	x0,[x19,UW_KWFN]		; virtuelle : le programme trie
	cbnz	x0,.draw
	mov	x20,x1				; colonne
	mov	x21,x2				; sens
	mov	x22,0				; texte de l'element choisi
	ldr	x0,[x19,UW_VALUE]
	tbnz	x0,63,.go
	ldr	x1,[x19,UW_ITEMS]
	add	x17,x1,x0,lsl 4
	ldr	x22,[x17]
.go:	ldr	x23,[x19,UW_ITEMS]
	ldr	x24,[x19,UW_NITEMS]
	ldr	x25,[x29,16]			; i = premier + 1
	add	x25,x25,1
.i:	cmp	x25,x24
	b.hs	.done
	add	x0,x23,x25,lsl 4
	ldp	x26,x27,[x0]			; cle
	sub	x28,x25,1			; j
.j:	ldr	x0,[x29,16]
	cmp	x28,x0
	b.lt	.put
	add	x17,x23,x28,lsl 4
	ldr	x0,[x17]
	mov	x1,x26
	mov	x2,x20
	bl	uk_cmp_field
	cbz	x21,.asc
	neg	x0,x0
.asc:	cmp	x0,0
	b.le	.put
	add	x0,x23,x28,lsl 4
	ldp	x1,x2,[x0]
	stp	x1,x2,[x0,16]
	sub	x28,x28,1
	b	.j
.put:	add	x0,x23,x28,lsl 4
	stp	x26,x27,[x0,16]
	add	x25,x25,1
	b	.i
.done:	cbz	x22,.draw			; l'element choisi : sa nouvelle place
	mov	x0,0
.f:	cmp	x0,x24
	b.hs	.draw
	add	x17,x23,x0,lsl 4
	ldr	x1,[x17]
	cmp	x1,x22
	b.eq	.sel
	add	x0,x0,1
	b	.f
.sel:	mov	x1,x0
	mov	x0,x19
	bl	uk_ls_select
.draw:	mov	x0,x19
	bl	uk_w_refresh
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x29,x30,[sp],16
	add	sp,sp,16			; (premier element)
	ret

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------

uk_ls_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	bl	uk_met
	mov	x28,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x20				; fond SURFACE, traits LIGNE dessus et
	mov	x1,x21				; dessous
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_SURFACE
	ldr	x5,[x19,UW_STYLE]		; (UL_SIDEBAR : CREUX, comme Fichiers)
	tbz	x5,5,.lbg
	jet	w4,TJ_CREUX
.lbg:	bl	ui_fill
	ldr	x0,[x19,UW_STYLE]		; (panneau lateral : sans traits)
	tbnz	x0,5,.nolin
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,1
	jet	w4,TJ_LIGNE
	bl	ui_fill
	mov	x0,x20
	add	x1,x21,x23
	sub	x1,x1,1
	mov	x2,x22
	mov	x3,1
	jet	w4,TJ_LIGNE
	bl	ui_fill
.nolin:	mov	x0,x19
	bl	uk_ls_hdr
	mov	x24,x0				; hauteur de l'en-tete
	cbz	x24,.rows
	mov	x0,x19
	bl	uk_ls_header
.rows:	ldr	x25,[x19,UW_POS]		; element
	add	x26,x21,x24			; y de la ligne
	mov	x0,x19				; (bas des lignes : la barre horizontale)
	bl	uk_ls_hbar_h
	sub	x27,x23,x0
.r:	ldr	x0,[x19,UW_NITEMS]
	cmp	x25,x0
	b.hs	.out
	ldr	x0,[x28,MT_ROW]
	add	x1,x26,x0
	add	x2,x21,x27
	cmp	x1,x2
	b.gt	.out
	mov	x0,x19
	mov	x1,x25
	mov	x2,x26
	bl	uk_ls_row
	ldr	x0,[x28,MT_ROW]
	add	x26,x26,x0
	add	x25,x25,1
	b	.r
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste : les en-tetes de colonnes (Sans Bold petite TEXTE3, fleche du
; tri, separateurs, trait dessous)
uk_ls_header:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	bl	uk_ls_hdr
	mov	x24,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	mov	x0,x20				; trait sous l'en-tete
	add	x1,x21,x24
	sub	x1,x1,1
	ldr	w2,[x19,UW_W]
	mov	x3,1
	jet	w4,TJ_LIGNE
	bl	ui_fill
	ldr	x22,[x19,UW_NCOLS]
	mov	x23,0
.c:	cmp	x23,x22
	b.hs	.out
	mov	x0,x19
	mov	x1,x23
	bl	uk_ls_colx
	add	x25,x20,x0			; x de la colonne
	mov	x26,x1				; largeur
	add	x0,x25,x26			; separateur a droite
	sub	x0,x0,1
	add	x1,x21,6
	mov	x2,1
	sub	x3,x24,12
	jet	w4,TJ_LIGNE
	bl	ui_fill
	ldr	x0,[x19,UW_COLS]
	add	x0,x0,x23,lsl 4
	ldr	x0,[x0,8]			; titre
	add	x1,x25,10
	mov	x2,x21
	mov	x3,x24
	jet	w4,TJ_TEXTE3
	bl	uk_bold
	mov	x5,x0
	ldr	x0,[x19,UW_COLS]
	add	x0,x0,x23,lsl 4
	ldr	x0,[x0,8]
	sub	x6,x26,30
	adr	x7,uk_mt
	ldr	x7,[x7,MT_SMALL]
	sub	x7,x7,1
	bl	uk_text_box
	ldr	x1,[x19,UW_SORT]		; colonne triee : fleche
	cmp	x1,x23
	b.ne	.n
	add	x1,x0,4
	adr	x0,uk_s_up
	ldr	x2,[x19,UW_ANCHOR]
	cbz	x2,.ar
	adr	x0,uk_s_down
.ar:	mov	x2,x21
	mov	x3,x24
	jet	w4,TJ_TEXTE2
	mov	x5,x0
	bl	uk_font_r
	mov	x6,x5
	mov	x5,x0
	mov	x0,x6
	mov	x6,0
	mov	x7,9
	bl	uk_text_box
.n:	add	x23,x23,1
	b	.c
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste -> x0 = marge droite des lignes (place de la barre si utile)
uk_ls_rmargin:
	prologue
	str	x19,[sp,-16]!
	ldr	x19,[x0,UW_AUX]
	mov	x0,6
	cbz	x19,.out
	mov	x0,x19
	bl	uk_sb_geom
	mov	x0,6
	cbz	x3,.out
	ldr	w0,[x19,UW_W]
	add	x0,x0,8
.out:	ldr	x19,[sp],16
	epilogue

; x0 = liste, x1 = element, x2 = y : une ligne
uk_ls_row:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	uk_met
	ldr	x22,[x0,MT_ROW]
	ldr	w23,[x19,UW_X]
	sxtw	x23,w23
	mov	x0,x19
	bl	uk_w_focused
	mov	x27,x0				; focus
	mov	x28,0				; 1 : texte blanc
	ldr	x0,[x19,UW_STYLE]
	tbz	x0,5,.nside			; UL_SIDEBAR : « #titre » = une section
	mov	x0,x19
	mov	x1,x20
	bl	uk_ls_text
	ldrb	w1,[x0]
	cmp	w1,'#'
	b.ne	.nside
	add	x0,x0,1
	str	x0,[sp,-16]!
	bl	uk_met
	ldr	x7,[x0,MT_SMALL]
	bl	uk_bold
	mov	x5,x0
	ldr	x0,[sp],16
	add	x1,x23,10
	mov	x2,x21
	mov	x3,x22
	jet	w4,TJ_TEXTE3
	ldr	w6,[x19,UW_W]
	sub	x6,x6,20
	bl	uk_text_box
	b	.out
.nside:	ldr	x0,[x19,UW_STYLE]		; UL_STRIPES : une ligne sur deux CREUX
	tbz	x0,3,.nstr
	tbz	x20,0,.nstr
	mov	x0,x23
	mov	x1,x21
	ldr	w2,[x19,UW_W]
	mov	x3,x22
	jet	w4,TJ_SURFACE
	jet	w5,TJ_CREUX
	mov	x6,x2
	str	x6,[sp,-16]!
	mov	w0,w4
	mov	w1,w5
	mov	x2,128
	bl	ui_mix
	mov	w4,w0
	ldr	x2,[sp],16
	mov	x0,x23
	mov	x1,x21
	mov	x3,x22
	bl	ui_fill
.nstr:	ldr	x0,[x19,UW_VALUE]
	cmp	x0,x20
	b.ne	.hot
	jet	w5,TJ_ACCENT_CLAIR		; choisi : ACCENT (focus), ACCENT_CLAIR
	cbz	x27,.box
	jet	w5,TJ_ACCENT
	mov	x28,1
	b	.box
.hot:	ldr	x0,[x19,UW_HOT]
	cmp	x0,x20
	b.ne	.text
	ldr	x0,[x19,UW_STYLE]		; (sur CREUX : SURFACE)
	tbz	x0,5,.hclr
	jet	w5,TJ_SURFACE
	b	.box
.hclr:	jet	w5,TJ_CREUX			; survol : CREUX (Givre), FACE (Noir)
	jet	w0,TJ_SOMBRE
	cbz	w0,.box
	jet	w5,TJ_FACE
.box:	str	x5,[sp,-16]!
	mov	x0,x19
	bl	uk_ls_rmargin
	ldr	x5,[sp],16
	ldr	w2,[x19,UW_W]
	sub	x2,x2,6
	sub	x2,x2,x0
	add	x0,x23,6
	mov	x1,x21
	mov	x3,x22
	mov	x4,5
	mov	w6,w5
	bl	ui_rbox
.text:	mov	x0,x19
	bl	uk_ls_ncols
	mov	x24,x0
	mov	x25,0				; colonne
.c:	cmp	x25,x24
	b.hs	.out
	mov	x0,x19
	mov	x1,x25
	bl	uk_ls_colx
	add	x26,x23,x0			; x de la colonne
	str	x1,[sp,-16]!			; [sp] = largeur
	ldr	x0,[x19,UW_STYLE]		; UL_GRID : trait LIGNE a droite
	tbz	x0,4,.ng
	add	x0,x26,x1
	sub	x0,x0,1
	mov	x1,x21
	mov	x2,1
	mov	x3,x22
	jet	w4,TJ_LIGNE
	bl	ui_fill
.ng:	ldr	x9,[x19,UW_CELLFN]		; cellule dessinee par le programme ?
	cbz	x9,.txt
	mov	x0,x19
	mov	x1,x20
	mov	x2,x25
	mov	x3,x26
	mov	x4,x21
	ldr	x5,[sp]
	mov	x6,x22
	mov	x7,x28
	blr	x9
	cbz	x0,.txt
	add	sp,sp,16
	add	x25,x25,1
	b	.c
.txt:	mov	x0,x19
	mov	x1,x20
	bl	uk_ls_text
	mov	x1,x25
	bl	uk_field
	adr	x7,uk_mt			; colonne 0 : police d'interface,
	ldr	x7,[x7,MT_PX]			; TEXTE1 ; les autres : petite, TEXTE3
	jet	w4,TJ_TEXTE1
	cbz	x25,.col
	jet	w4,TJ_TEXTE3
	adr	x7,uk_mt
	ldr	x7,[x7,MT_SMALL]
.col:	cbz	x28,.ink
	jet	w4,TJ_ACCENT_INK
.ink:	stp	x4,x7,[sp,-16]!
	cbnz	x25,.noic			; UL_ICONS : icone en colonne 0
	ldr	x0,[x19,UW_STYLE]
	tbz	x0,8,.noic
	ldr	x0,[x19,UW_ITEMS]
	add	x0,x0,x20,lsl 4
	ldr	x0,[x0,8]
	and	x0,x0,3
	add	x1,x26,8
	sub	x2,x22,15
	add	x2,x21,x2,lsr 1
	bl	uk_ls_icon
	ldr	x6,[sp,16]
	sub	x6,x6,16+22
	add	x1,x26,10+22
	b	.icd
.noic:	ldr	x6,[sp,16]
	sub	x6,x6,16			; largeur utile
	add	x1,x26,10
.icd:
	ldr	x2,[x19,UW_NCOLS]		; aligne a droite (ULC_RIGHT) ?
	cbz	x2,.left
	ldr	x2,[x19,UW_COLS]
	add	x2,x2,x25,lsl 4
	ldr	w2,[x2,4]
	tbz	w2,0,.left
	mov	x1,x7				; largeur du texte
	str	x6,[sp,-16]!
	adr	x0,uk_fbuf
	bl	uk_font_r
	mov	x2,x0
	adr	x0,uk_fbuf
	bl	ui_text_width
	ldr	x6,[sp],16
	cmp	x0,x6
	csel	x0,x6,x0,gt
	ldr	x1,[sp,16]
	add	x1,x26,x1
	sub	x1,x1,10
	sub	x1,x1,x0
.left:	mov	x2,x21
	mov	x3,x22
	ldp	x4,x7,[sp],16
	str	x1,[sp,-16]!
	bl	uk_font_r
	mov	x5,x0
	ldr	x1,[sp],16
	adr	x0,uk_fbuf
	bl	uk_text_box
	add	sp,sp,16
	add	x25,x25,1
	b	.c
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = genre (0 fichier, 1 dossier, 2 volume), x1 = x, x2 = y : icone 16 x 15
uk_ls_icon:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x19,x1
	mov	x20,x2
macro uk_ic dx*, dy*, w*, h*, t* {
	add	x0,x19,dx
	add	x1,x20,dy
	mov	x2,w
	mov	x3,h
	jet	w4,t
	bl	ui_fill
}
	cmp	x21,1
	b.eq	.dir
	b.hi	.vol
	uk_ic	2,0,12,15,TJ_TEXTE3		; fichier
	uk_ic	3,1,10,13,TJ_SURFACE
	uk_ic	10,1,3,3,TJ_CREUX
	uk_ic	5,6,6,1,TJ_LIGNE_FORTE
	uk_ic	5,8,6,1,TJ_LIGNE_FORTE
	uk_ic	5,10,4,1,TJ_LIGNE_FORTE
	b	.out
.dir:	uk_ic	0,1,7,3,TJ_ALERTE		; dossier
	uk_ic	0,3,16,11,TJ_ALERTE
	jet	w0,TJ_ALERTE
	jet	w1,TJ_SURFACE
	mov	x2,110
	bl	ui_mix
	mov	w4,w0
	add	x0,x19,1
	add	x1,x20,5
	mov	x2,14
	mov	x3,8
	bl	ui_fill
	b	.out
.vol:	uk_ic	0,3,16,10,TJ_TEXTE2		; volume
	uk_ic	1,4,14,8,TJ_CREUX
	uk_ic	1,4,14,1,TJ_LIGNE
	uk_ic	11,8,3,2,TJ_SUCCES
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Souris, clavier, molette
; ---------------------------------------------------------------------------

; x1 = genre, x2, x3 = point local, x4 = clics
uk_ls_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	cmp	x20,3
	b.ne	.nl
	mov	x24,-1				; sortie : plus de survol
	b	.sethot
.nl:	ldr	x0,[x19,UW_DRAG]		; largeur de colonne en cours
	cbz	x0,.nodrag
	cmp	x20,2
	b.ne	.resize
	str	xzr,[x19,UW_DRAG]
	b	.zero
.resize:
	sub	x24,x0,1
	ldr	x1,[x19,UW_DRAG2]
	sub	x1,x21,x1
	ldr	x2,[x19,UW_AUX2]
	add	x1,x2,x1
	cmp	x1,24
	mov	x2,24
	csel	x1,x2,x1,lt
	ldr	x2,[x19,UW_COLS]
	add	x17,x2,x24,lsl 4
	str	w1,[x17]
	mov	x0,x19
	bl	uk_ls_sync
	b	.redraw
.nodrag:
	mov	x0,x19
	bl	uk_ls_hdr
	mov	x24,x0
	cmp	x22,x24
	b.ge	.body
	cmp	x20,1				; en-tete : appui seulement
	b.ne	.zero
	ldr	x25,[x19,UW_NCOLS]
	cbz	x25,.zero
	mov	x26,0				; bord d'une colonne (+/- 4) ?
.b:	cmp	x26,x25
	b.hs	.hit
	mov	x0,x19
	mov	x1,x26
	bl	uk_ls_colx
	add	x0,x0,x1
	sub	x0,x21,x0
	cmp	x0,0
	cneg	x0,x0,mi
	cmp	x0,4
	b.gt	.bn
	add	x0,x26,1			; redimensionne la colonne x26
	str	x0,[x19,UW_DRAG]
	str	x21,[x19,UW_DRAG2]
	str	x1,[x19,UW_AUX2]
	b	.zero
.bn:	add	x26,x26,1
	b	.b
.hit:	ldr	x0,[x19,UW_STYLE]		; tri par cette colonne
	tbnz	x0,1,.zero			; UL_NOSORT
	mov	x26,0
.h:	cmp	x26,x25
	b.hs	.zero
	mov	x0,x19
	mov	x1,x26
	bl	uk_ls_colx
	add	x0,x0,x1
	cmp	x21,x0
	b.lt	.sort
	add	x26,x26,1
	b	.h
.sort:	mov	x2,0				; meme colonne : sens inverse
	ldr	x0,[x19,UW_SORT]
	cmp	x0,x26
	b.ne	.s1
	ldr	x2,[x19,UW_ANCHOR]
	eor	x2,x2,1
.s1:	ldr	x0,[x19,UW_STYLE]		; UL_USERSORT : au programme
	tbz	x0,7,.s2
	str	x26,[x19,UW_SORT]
	str	x2,[x19,UW_ANCHOR]
	mov	x0,UN_SORT
	b	.out
.s2:	mov	x0,x19
	mov	x1,x26
	bl	ui_list_sort
	mov	x0,UN_SORT
	b	.out
.body:	bl	uk_met				; ligne sous le point
	ldr	x1,[x0,MT_ROW]
	sub	x0,x22,x24
	udiv	x0,x0,x1
	ldr	x1,[x19,UW_POS]
	add	x24,x0,x1
	ldr	x1,[x19,UW_NITEMS]
	cmp	x24,x1
	b.lo	.in
	mov	x24,-1
.in:	cmp	x20,1
	b.eq	.press
	cbnz	x20,.zero			; relache
	ldr	x0,[x19,UW_STYLE]		; UL_DRAG : bouton tenu, assez loin du
	tbz	x0,2,.sethot			; point d'appui -> glisser l'element
	adr	x0,uk_capture
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.sethot
	ldr	w0,[x19,UW_FLAGS]
	tbnz	w0,4,.zero
	ldr	x0,[x19,UW_VALUE]
	tbnz	x0,63,.zero
	ldr	x1,[x19,UW_PX]
	subs	x1,x21,x1
	cneg	x1,x1,mi
	ldr	x2,[x19,UW_AUX2]
	subs	x2,x22,x2
	cneg	x2,x2,mi
	add	x1,x1,x2
	cmp	x1,UK_DRAG_PX
	b.lo	.zero
	ldr	w1,[x19,UW_FLAGS]
	orr	w1,w1,16
	str	w1,[x19,UW_FLAGS]
	mov	x1,x0				; le texte de sa premiere colonne
	mov	x0,x19
	bl	uk_ls_text
	mov	x1,0
	bl	uk_field
	mov	x1,-1
	mov	x2,UDT_TEXT
	bl	ui_drag_start
	b	.zero
.sethot:
	ldr	x0,[x19,UW_HOT]			; deplacement : survol
	cmp	x0,x24
	b.eq	.zero
	str	x24,[x19,UW_HOT]
	b	.redraw
.press:	tbnz	x24,63,.zero
	ldr	x0,[x19,UW_STYLE]		; UL_SIDEBAR : une section ne se choisit pas
	tbz	x0,5,.psel
	mov	x0,x19
	mov	x1,x24
	bl	uk_ls_text
	ldrb	w0,[x0]
	cmp	w0,'#'
	b.eq	.zero
.psel:	str	x21,[x19,UW_PX]			; point d'appui (UL_DRAG)
	str	x22,[x19,UW_AUX2]
	ldr	w0,[x19,UW_FLAGS]
	and	w0,w0,not 16
	str	w0,[x19,UW_FLAGS]
	ldr	x25,[x19,UW_VALUE]
	mov	x0,x19
	mov	x1,x24
	bl	uk_ls_select
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CONTEXT			; bouton droit : menu contextuel
	adr	x1,uk_mbtn
	ldr	x1,[x1]
	tbnz	x1,1,.out
	mov	x0,UN_ACTIVATE
	cmp	x23,2
	b.eq	.out
	mov	x0,UN_CHANGE
	cmp	x25,x24
	b.ne	.out
	b	.zero
.redraw:
	mov	x0,x19
	bl	uk_w_refresh
.zero:	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = code HID, x2 = caractere, x3 = mods -> code, -1 si non pris
uk_ls_key:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldr	x22,[x19,UW_VALUE]
	cmp	w20,KEY_ENTER
	b.ne	.nav
	mov	x0,-1
	tbnz	x22,63,.out
	mov	x0,UN_ACTIVATE
	b	.out
.nav:	ldr	x0,[x19,UW_NITEMS]
	cbz	x0,.no
	mov	x0,x19
	bl	uk_ls_rows
	mov	x2,x0				; lignes visibles
	mov	x1,x22
	cmp	w20,KEY_UP
	b.ne	.k1
	sub	x1,x22,1
	tbz	x22,63,.set
	mov	x1,0
	b	.set
.k1:	cmp	w20,KEY_DOWN
	b.ne	.k2
	add	x1,x22,1
	b	.set
.k2:	cmp	w20,KEY_HOME
	b.ne	.k3
	mov	x1,0
	b	.set
.k3:	cmp	w20,KEY_END
	b.ne	.k4
	ldr	x1,[x19,UW_NITEMS]
	sub	x1,x1,1
	b	.set
.k4:	cmp	w20,KEY_PGUP
	b.ne	.k5
	sub	x1,x22,x2
	cmp	x1,0
	csel	x1,xzr,x1,lt
	b	.set
.k5:	cmp	w20,KEY_PGDN
	b.ne	.k6
	add	x1,x22,x2
	b	.set
.k6:	cmp	w20,KEY_LEFT			; gauche, droite : les colonnes (UL_HSCROLL)
	b.eq	.hs
	cmp	w20,KEY_RIGHT
	b.ne	.k7
.hs:	ldr	x0,[x19,UW_HBAR]
	cbz	x0,.no
	ldr	x1,[x19,UW_HPOS]
	mov	x2,48
	cmp	w20,KEY_LEFT
	cneg	x2,x2,eq
	add	x1,x1,x2
	str	x1,[x19,UW_HPOS]
	mov	x0,x19
	bl	uk_ls_sync
	mov	x0,x19
	bl	uk_w_refresh
	b	.zero
.k7:	cmp	w21,' '				; premiere lettre : element suivant qui
	b.le	.no				; commence par elle
	cmp	w21,0x7F
	b.hs	.no
	orr	w21,w21,0x20			; (minuscule ASCII)
	ldr	x3,[x19,UW_NITEMS]
	mov	x4,1
.t:	cmp	x4,x3
	b.hi	.zero
	add	x1,x22,x4
	udiv	x5,x1,x3			; modulo le nombre d'elements
	msub	x1,x5,x3,x1
	stp	x1,x3,[sp,-16]!
	stp	x4,x4,[sp,-16]!
	mov	x0,x19
	bl	uk_ls_text
	ldp	x4,x5,[sp],16
	ldp	x1,x3,[sp],16
	ldrb	w5,[x0]
	orr	w5,w5,0x20
	cmp	w5,w21
	b.eq	.set
	add	x4,x4,1
	b	.t
.set:	mov	x0,x19
	bl	uk_ls_select
	mov	x0,x19
	bl	uk_w_refresh
	ldr	x0,[x19,UW_VALUE]
	cmp	x0,x22
	b.eq	.zero
	mov	x0,UN_CHANGE
	b	.out
.zero:	mov	x0,0
	b	.out
.no:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = crans : 3 lignes par cran
uk_ls_wheel:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x2,3
	mul	x1,x1,x2
	ldr	x2,[x19,UW_POS]
	sub	x2,x2,x1
	str	x2,[x19,UW_POS]
	bl	uk_ls_sync
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; x0 = liste, x1 = enfant (la barre), x2 = code : la barre a bouge
uk_ls_child:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x1,UW_VALUE]
	ldr	x2,[x19,UW_HBAR]
	cmp	x1,x2
	b.ne	.v
	str	x0,[x19,UW_HPOS]
	b	.s
.v:	str	x0,[x19,UW_POS]
.s:	mov	x0,x19
	bl	uk_ls_sync
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; x0 = liste, x1 = element -> x0 = son texte (virtuelle : la fonction du
; programme ; « » s'il n'y en a pas)
uk_ls_text:
	prologue
	ldr	x2,[x0,UW_KWFN]
	cbz	x2,.real
	blr	x2				; x0 = liste, x1 = element -> texte
	cbnz	x0,.out
	adr	x0,uk_empty
	b	.out
.real:	ldr	x2,[x0,UW_ITEMS]
	add	x2,x2,x1,lsl 4
	ldr	x0,[x2]
.out:	epilogue

; x0 = liste, x1 = nombre de lignes, x2 = fonction (x0 = liste, x1 = ligne
; -> x0 = texte de la ligne, colonnes separees par des tabulations ; valable
; jusqu'au prochain appel) : liste virtuelle (grille de donnees : le
; programme garde ses lignes ; un clic sur un en-tete rend UN_SORT, le
; programme trie et redessine)
ui_list_virtual:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	stp	x1,x2,[sp,-16]!
	bl	uk_items_clear
	ldp	x1,x2,[sp],16
	str	x2,[x19,UW_KWFN]
	str	x1,[x19,UW_NITEMS]
	ldr	x0,[x19,UW_VALUE]
	cmp	x0,x1
	b.lo	.s
	mov	x0,-1
	str	x0,[x19,UW_VALUE]
.s:	mov	x0,x19
	bl	uk_ls_sync
	mov	x0,x19
	bl	uk_w_refresh
.out:	ldr	x19,[sp],16
	epilogue

; x0 = liste, x1 = fonction (x0 = liste, x1 = ligne, x2 = colonne, x3, x4 =
; coin de la cellule dans la fenetre, x5 = largeur, x6 = hauteur, x7 = 1 si le
; fond est celui d'une ligne choisie avec le focus -> x0 = 1 : cellule dessinee,
; 0 : le texte ordinaire ; 0 : aucune)
ui_list_cell:
	cbz	x0,.ret
	str	x1,[x0,UW_CELLFN]
.ret:	ret

; x0 = liste -> x0 = colonne triee (-1 : aucune), x1 = 1 decroissant
ui_list_sorted:
	ldr	x1,[x0,UW_ANCHOR]
	ldr	x0,[x0,UW_SORT]
	ret
