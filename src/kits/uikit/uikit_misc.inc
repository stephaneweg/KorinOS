; ============================================================================
;  kits/uikit_misc.inc - UIKit : curseur, infobulles, glisser-deposer,
;  localisation (TR), menus (raccourcis traites par le kit, entrees grisees
;  ou cochees), italique.
; ============================================================================

UK_TIP_MS	= 600			; survol avant l'infobulle
UK_DRAG_PX	= 6			; deplacement qui commence un glisser
UK_MENU_IDS	= 64			; etats d'entrees de menu retenus

; ---------------------------------------------------------------------------
;  Forme du curseur
; ---------------------------------------------------------------------------

; x0 = forme (UC_*) : pour le contenu de la fenetre courante
ui_cursor:
	prologue
	adr	x1,uk_cur_shape
	ldr	x2,[x1]
	cmp	x2,x0
	b.eq	.out
	str	x0,[x1]
	str	x0,[sp,-16]!
	bl	uk_win
	adr	x9,uk_msg
	str	x0,[x9,WM_A]
	ldr	x0,[sp],16
	str	x0,[x9,WM_B]
	mov	x0,WS_OP_CURSOR
	bl	uk_call
.out:	epilogue

; x0 = 1 : la fenetre prend le pointeur (souris relative, WEV_M_REL), 0 : elle le rend
ui_pointer_lock:
	prologue
	str	x0,[sp,-16]!
	bl	uk_win
	adr	x9,uk_msg
	str	x0,[x9,WM_A]
	ldr	x0,[sp],16
	str	x0,[x9,WM_B]
	mov	x0,WS_OP_POINTER_LOCK
	bl	uk_call
	epilogue

; x0 = widget, x1 = forme (UC_* ; -1 : celle de son type)
ui_w_cursor:
	cbz	x0,.ret
	add	x1,x1,1
	str	x1,[x0,UW_CURSOR]
.ret:	ret

; x0 = widget (0 : aucun) -> x0 = forme du curseur sur lui
uk_w_shape:
	cbz	x0,.arrow
	ldr	x1,[x0,UW_CURSOR]
	cbz	x1,.type
	sub	x0,x1,1
	ret
.type:	ldr	w1,[x0,UW_TYPE]
	cmp	w1,UT_TEXT
	b.eq	.text
	cmp	w1,UT_AREA
	b.eq	.text
	cmp	w1,UT_SPLIT
	b.ne	.arrow
	ldp	w1,w2,[x0,UW_W]			; separateur : selon son sens
	mov	x0,UC_HRESIZE
	cmp	w2,w1
	b.gt	.ret
	mov	x0,UC_VRESIZE
.ret:	ret
.text:	ldr	x1,[x0,UW_STYLE]		; (texte en lecture seule : fleche)
	mov	x0,UC_TEXT
	tbz	x1,1,.ret
.arrow:	mov	x0,UC_ARROW
	ret

; ---------------------------------------------------------------------------
;  Infobulles
; ---------------------------------------------------------------------------

; x0 = widget, x1 = texte (0 : aucune) : son infobulle
ui_w_tip:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	mov	x20,0
	cbz	x1,.set
	mov	x0,x1
	bl	ui_tr
	bl	uk_strdup
	mov	x20,x0
.set:	ldr	x0,[x19,UW_TIP]
	bl	uk_free
	str	x20,[x19,UW_TIP]
.out:	ldp	x19,x20,[sp],16
	epilogue

; a appeler quand ui_event n'a rien rendu (attente ecoulee) : montre
; l'infobulle du widget survole depuis UK_TIP_MS (attente conseillee :
; 200 ms ; ui_w_event l'appelle aussi pour un evenement vide)
ui_w_idle:
	prologue
	str	x19,[sp,-16]!
	adr	x0,uk_mwin			; la fenetre sous la souris
	ldr	x0,[x0]
	bl	uk_route
	mov	x19,x0
	bl	uk_idle_in
	cbz	x19,.out
	mov	x0,x19
	bl	ui_window_use
.out:	ldr	x19,[sp],16
	epilogue

uk_idle_in:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,uk_tip_win			; deja montree
	ldr	x0,[x0]
	cbnz	x0,.out
	adr	x0,uk_capture			; (bouton tenu : non)
	ldr	x0,[x0]
	cbnz	x0,.out
	adr	x0,uk_hover
	ldr	x19,[x0]
	cbz	x19,.out
	ldr	x20,[x19,UW_TIP]
	cbz	x20,.out
	mov	x0,x19
	bl	uk_w_shown
	cbz	x0,.out
	sys	SYS_TIME_MS
	adr	x1,uk_hover_t
	ldr	x1,[x1]
	sub	x0,x0,x1
	cmp	x0,UK_TIP_MS
	b.lo	.out
	mov	x0,x20
	bl	uk_tip_show
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte : l'infobulle, sous le pointeur (fenetre sans cadre, au-dessus)
; x0, x1 = point de la fenetre, x2 = texte (0 : retire) : une infobulle (coin
; haut gauche a ce point), sans attendre ; une nouvelle remplace l'ancienne
ui_tip_at:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	uk_tip_hide
	cbz	x21,.out
	sub	x0,x19,12			; (uk_tip_show : pointeur + 12, 20)
	sub	x1,x20,20
	adr	x2,uk_mx
	stp	x0,x1,[x2]
	mov	x0,x21
	bl	uk_tip_show
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

uk_tip_show:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	bl	uk_met				; taille : texte + 16, ligne de 24
	ldr	x1,[x0,MT_SMALL]
	str	x1,[sp,-16]!
	bl	uk_font_r
	mov	x2,x0
	ldr	x1,[sp],16
	mov	x0,x19
	bl	ui_text_width
	add	x21,x0,16
	cmp	x21,400
	mov	x0,400
	csel	x21,x0,x21,hi
	mov	x22,24
	bl	uk_win_pos			; place : pointeur + (12, 20)
	adr	x2,uk_mx
	ldp	x2,x3,[x2]
	add	x0,x0,x2
	add	x0,x0,12
	add	x1,x1,x3
	add	x1,x1,20
	mov	x2,x21
	mov	x3,x22
	adr	x4,uk_menu_title
	mov	x5,WF_FRAMELESS or WF_TOP or WF_NOFOCUS or WF_NOLIST
	bl	uk_create
	tbnz	x0,63,.out
	mov	x23,x0
	adr	x9,uk_msg
	ldr	x24,[x9,WM_A]			; son tampon
	mov	x0,x24
	sys	SYS_BUF_MAP
	tbnz	x0,63,.close
	mov	x1,x21
	mov	x2,x22
	bl	uk_target
	mov	x0,0				; fond TEXTE1, texte SURFACE (inverse)
	mov	x1,0
	mov	x2,x21
	mov	x3,x22
	jet	w4,TJ_TEXTE1
	bl	ui_fill
	bl	uk_met
	ldr	x7,[x0,MT_SMALL]
	bl	uk_font_r
	mov	x5,x0
	mov	x0,x19
	mov	x1,8
	mov	x2,0
	mov	x3,x22
	jet	w4,TJ_SURFACE
	sub	x6,x21,16
	bl	uk_text_box
	bl	uk_target_back
	adr	x9,uk_msg			; montre
	str	x23,[x9,WM_A]
	stp	xzr,xzr,[x9,WM_B]
	str	x21,[x9,WM_D]
	str	x22,[x9,WM_DATA]
	mov	x0,WS_OP_UPDATE
	bl	uk_call
	adr	x0,uk_tip_win
	stp	x23,x24,[x0]
	b	.out
.close:	adr	x9,uk_msg
	str	x23,[x9,WM_A]
	mov	x0,WS_OP_CLOSE
	bl	uk_call
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ferme l'infobulle montree (s'il y en a une)
uk_tip_hide:
	prologue
	adr	x0,uk_tip_win
	ldp	x1,x2,[x0]
	cbz	x1,.out
	stp	xzr,xzr,[x0]
	str	x2,[sp,-16]!
	adr	x9,uk_msg
	str	x1,[x9,WM_A]
	mov	x0,WS_OP_CLOSE
	bl	uk_call
	ldr	x0,[sp],16
	sys	SYS_BUF_FREE
.out:	epilogue

; ---------------------------------------------------------------------------
;  Glisser-deposer
; ---------------------------------------------------------------------------

; x0 = donnees, x1 = octets (-1 : jusqu'au NUL ; au plus WS_DRAG_MAX), x2 =
; genre (UDT_TEXT, UDT_FILES) : commence un glisser (pendant un appui dans
; la fenetre ; il se depose au relache) -> x0 = 0
ui_drag_start:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmn	x20,1
	b.ne	.l
	bl	uk_strlen
	mov	x20,x0
.l:	cmp	x20,WS_DRAG_MAX
	mov	x0,WS_DRAG_MAX
	csel	x20,x0,x20,hi
	adr	x9,uk_msg
	str	x21,[x9,WM_A]
	str	w20,[x9,WM_LEN]
	add	x0,x9,WM_DATA
	mov	x1,x19
	mov	x2,x20
	bl	uk_memcpy
	mov	x0,WS_OP_DRAG
	bl	uk_call
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon, x1 = taille -> x0 = octets du dernier depot (texte termine
; par NUL), x1 = son genre
ui_drop_get:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,WS_OP_DROP_GET
	bl	uk_call
	tbnz	x0,63,.none
	sub	x2,x20,1
	cmp	x0,x2
	csel	x2,x0,x2,lo
	str	x2,[sp,-16]!
	mov	x0,x19
	adr	x1,uk_msg+WM_DATA
	bl	uk_memcpy
	strb	wzr,[x0]
	ldr	x0,[sp],16
	adr	x9,uk_msg
	ldr	x1,[x9,WM_A]
	b	.out
.none:	strb	wzr,[x19]
	mov	x0,0
	mov	x1,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; (ui_w_event) x19 = WEV_DROP : un texte depose sur un champ ou un texte
; multiligne y est insere au point de depot -> x0 = widget dessous (0 :
; aucun), x1 = code (UN_CHANGE : insere ; UN_DROP : au programme)
uk_on_drop:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	ldrsw	x21,[x19,EV_X]
	ldrsw	x22,[x19,EV_Y]
	mov	x0,x21
	mov	x1,x22
	ldr	w2,[x19,EV_WIN]
	bl	uk_w_at
	mov	x20,x0
	cbz	x20,.none
	ldr	w0,[x19,16]			; un texte ?
	cmp	w0,UDT_TEXT
	b.ne	.prog
	ldr	w0,[x20,UW_TYPE]
	cmp	w0,UT_TEXT
	b.eq	.ins
	cmp	w0,UT_AREA
	b.ne	.prog
.ins:	ldr	x0,[x20,UW_STYLE]
	tbnz	x0,1,.prog			; (lecture seule)
	ldr	w0,[x20,UW_STYLE]
	tbnz	w0,0,.ok			; (mot de passe : pas de souci)
.ok:	mov	x0,x20				; curseur au point de depot
	ldr	w2,[x20,UW_X]
	sxtw	x2,w2
	sub	x2,x21,x2
	ldr	w3,[x20,UW_Y]
	sxtw	x3,w3
	sub	x3,x22,x3
	ldr	w1,[x20,UW_TYPE]
	cmp	w1,UT_AREA
	b.eq	.ah
	mov	x1,x2
	bl	uk_tb_hit
	b	.at
.ah:	bl	uk_ta_hit
.at:	str	x0,[x20,UW_VALUE]
	str	x0,[x20,UW_ANCHOR]
	adr	x0,uk_pastebuf
	mov	x1,UK_PASTE_MAX
	bl	ui_drop_get
	cbz	x0,.prog
	mov	x2,x0
	adr	x1,uk_pastebuf
	mov	x0,x20
	bl	uk_tb_insert
	mov	x0,x20
	bl	ui_w_focus
	mov	x0,x20
	bl	uk_w_refresh
	mov	x0,x20
	mov	x1,UN_CHANGE
	b	.out
.prog:	mov	x0,x20
	mov	x1,UN_DROP
	b	.out
.none:	mov	x0,0
	mov	x1,0
.out:	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Localisation : TR (AppKit, lang.inc : la langue vaut pour tout programme)
; ---------------------------------------------------------------------------

; x0 = texte -> x0 = sa traduction (ou le texte)
ui_tr:
	prologue
	bl	lang_tr
	epilogue

; x0 = chemin d'un dictionnaire du programme -> x0 = 1 si lu
ui_tr_load:
	prologue
	bl	lang_load
	epilogue

; -> x0 = la langue
ui_lang:
	prologue
	bl	lang_get
	epilogue

; x4 = elements "a|b|c" -> x4 = les memes traduits, dans uk_tr_tampon (tous les autres
; registres gardes ; un seul texte en cours : a recopier avant le suivant)
uk_tr_items:
	prologue
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	stp	x5,x6,[sp,-16]!
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	cbz	x4,.fin
	mov	x19,x4
	adr	x20,uk_tr_tampon
	mov	x21,x20
.mot:	sub	sp,sp,256
	mov	x1,sp
	mov	x2,0
.c:	ldrb	w3,[x19]
	cbz	w3,.f
	cmp	w3,'|'
	b.eq	.f
	add	x19,x19,1
	cmp	x2,255
	b.hs	.c
	strb	w3,[x1,x2]
	add	x2,x2,1
	b	.c
.f:	strb	wzr,[x1,x2]
	mov	x0,sp
	bl	lang_tr
.cp:	ldrb	w3,[x0],1
	cbz	w3,.s
	adr	x2,uk_tr_tampon+UK_TR_ITEMS-2
	cmp	x21,x2
	b.hs	.cp
	strb	w3,[x21],1
	b	.cp
.s:	add	sp,sp,256
	ldrb	w3,[x19]
	cbz	w3,.end
	add	x19,x19,1
	mov	w3,'|'
	strb	w3,[x21],1
	b	.mot
.end:	strb	wzr,[x21]
	mov	x4,x20
.fin:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ldp	x5,x6,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	epilogue

; x0 = chemin -> x0 = son contenu termine par NUL (bloc du kit ; 0 : absent)
uk_read_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	mov	x1,sp
	sys	SYS_STAT
	cbnz	x0,.fail
	ldr	x20,[sp]
	add	x0,x20,1
	bl	uk_alloc
	cbz	x0,.fail
	mov	x21,x0
	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.free
	mov	x19,x0
	mov	x1,x21
	mov	x2,x20
	sys	SYS_READ
	mov	x22,x0
	mov	x0,x19
	sys	SYS_CLOSE
	tbnz	x22,63,.free
	strb	wzr,[x21,x22]
	mov	x0,x21
	b	.out
.free:	mov	x0,x21
	bl	uk_free
.fail:	mov	x0,0
.out:	add	sp,sp,16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Menus : etats des entrees, raccourcis traites par le kit
;
;  ui_menu garde le texte du programme ; ui_menu_state(id, UMS_DISABLED /
;  UMS_CHECKED) note l'etat d'une entree, et le texte est renvoye avec ses
;  marques (« I<id>d » grisee, « I<id>c » cochee : la barre de menus les
;  dessine). ui_menu_key(evenement) reconnait le raccourci d'une entree
;  active (« Ctrl+S », « Ctrl+Maj+Z », « Alt+X », « F3 ») -> son id.
; ---------------------------------------------------------------------------

; -> x0 = etats des entrees, x1 = texte envoye, de la fenetre courante
; (feuille ; uk_menu_need d'abord pour une fenetre qui n'a encore rien)
uk_menu_base:
	adr	x0,uk_menu_p
	ldr	x0,[x0]
	add	x1,x0,1
	cmp	x1,1
	b.ls	.st
	add	x1,x0,UK_MENU_IDS*16
	ret
.st:	adr	x0,uk_menu_st
	adr	x1,uk_menubuf
	ret

; les etats et le texte des menus d'une fenetre de plus, au premier besoin
uk_menu_need:
	prologue
	adr	x0,uk_menu_p
	ldr	x0,[x0]
	cmn	x0,1
	b.ne	.out
	mov	x0,UK_MENU_IDS*16+WS_MENU_MAX+8
	bl	uk_alloc
	adr	x1,uk_menu_p
	str	x0,[x1]
.out:	epilogue

; x0 = texte des menus -> x0 = le meme, ses libelles traduits (TR). Le dernier
; texte traduit est garde : un programme le renvoie souvent, inchange.
uk_menu_tr:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x0
	cbz	x19,.out
	ldrb	w0,[x19]
	cbz	w0,.out
	adr	x0,uk_menu_tr_d
	ldp	x1,x2,[x0]
	cmp	x1,x19
	b.ne	.new
	mov	x20,x2
	b	.out
.new:	mov	x0,WS_MENU_MAX
	bl	uk_alloc
	cbz	x0,.out
	mov	x21,x0
	add	x22,x21,WS_MENU_MAX-480		; (une ligne de plus tient toujours)
	adr	x1,uk_menu_tr_d
	stp	x19,x21,[x1]
	mov	x20,x21
.line:	ldrb	w0,[x19]
	cbz	w0,.end
	cmp	x21,x22
	b.hs	.end
	cmp	w0,'M'
	b.eq	.m
	cmp	w0,'I'
	b.ne	.rest
.i:	ldrb	w0,[x19],1			; I<id>, tabulation
	strb	w0,[x21],1
	cmp	w0,9
	b.ne	.i
	b	.lbl
.m:	strb	w0,[x21],1
	add	x19,x19,1
.lbl:	adr	x2,uk_fitbuf			; le libelle
	mov	x3,0
.l1:	ldrb	w0,[x19]
	cbz	w0,.l2
	cmp	w0,9
	b.eq	.l2
	cmp	w0,10
	b.eq	.l2
	cmp	x3,400
	b.hs	.l2
	strb	w0,[x2,x3]
	add	x3,x3,1
	add	x19,x19,1
	b	.l1
.l2:	strb	wzr,[x2,x3]
	mov	x0,x2
	bl	ui_tr
.l3:	ldrb	w1,[x0],1
	cbz	w1,.rest
	strb	w1,[x21],1
	b	.l3
.rest:	ldrb	w0,[x19]			; le reste de la ligne
	cbz	w0,.end
	strb	w0,[x21],1
	add	x19,x19,1
	cmp	w0,10
	b.ne	.rest
	b	.line
.end:	strb	wzr,[x21]
.out:	mov	x0,x20
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte des menus (0 ou vide : plus de menu) de la fenetre courante
; -> x0 = 0 (< 0 : erreur)
ui_menu:
	prologue
	bl	uk_menu_tr
	adr	x1,uk_menu_src
	str	x0,[x1]
	bl	uk_menu_need
	bl	uk_menu_send
	epilogue

; x0 = id, x1 = etat (UMS_DISABLED, UMS_CHECKED ; 0 : normal) : renvoie
; les menus
ui_menu_state:
	prologue
	stp	x0,x1,[sp,-16]!
	bl	uk_menu_need
	bl	uk_menu_base
	mov	x2,x0				; (id, etat) ; une place libre ou la sienne
	ldp	x0,x1,[sp],16
	mov	x3,0
	mov	x5,-1
.l:	cmp	x3,UK_MENU_IDS
	b.hs	.put
	add	x4,x2,x3,lsl 4
	ldp	x6,x7,[x4]
	cbz	x7,.free
	cmp	x6,x0
	b.eq	.set
	b	.n
.free:	cmn	x5,1
	csel	x5,x3,x5,eq
.n:	add	x3,x3,1
	b	.l
.put:	tbnz	x5,63,.send
	add	x4,x2,x5,lsl 4
.set:	orr	x1,x1,0x100			; (place prise)
	stp	x0,x1,[x4]
.send:	bl	uk_menu_send
	epilogue

; x0 = id -> x0 = son etat (UMS_*)
uk_menu_flags:
	mov	x8,x0
	mov	x9,x30
	bl	uk_menu_base
	mov	x30,x9
	mov	x2,x0
	mov	x0,x8
	mov	x3,0
.l:	cmp	x3,UK_MENU_IDS
	b.hs	.no
	add	x4,x2,x3,lsl 4
	ldp	x5,x6,[x4]
	cbz	x6,.n
	cmp	x5,x0
	b.ne	.n
	and	x0,x6,0xFF
	ret
.n:	add	x3,x3,1
	b	.l
.no:	mov	x0,0
	ret

; le texte du programme, avec les marques d'etat, a la barre de menus
uk_menu_send:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,uk_menu_src
	ldr	x19,[x0]
	mov	x0,0
	cbz	x19,.send
	bl	uk_menu_base
	mov	x20,x1
	mov	x21,x20
	add	x22,x20,WS_MENU_MAX-4
.c:	cmp	x21,x22
	b.hs	.end
	ldrb	w0,[x19],1
	strb	w0,[x21],1
	cbz	w0,.end
	cmp	w0,'I'				; « I<id> » en debut de ligne
	b.ne	.c
	sub	x1,x21,1
	cmp	x1,x20
	b.eq	.id
	ldrb	w2,[x1,-1]
	cmp	w2,10
	b.ne	.c
.id:	mov	x0,0				; l'id, recopie
.d:	ldrb	w1,[x19]
	sub	w2,w1,'0'
	cmp	w2,10
	b.hs	.did
	strb	w1,[x21],1
	add	x19,x19,1
	mov	x3,10
	madd	x0,x0,x3,x2
	cmp	x21,x22
	b.hs	.end
	b	.d
.did:	bl	uk_menu_flags			; ses marques
	tbz	x0,0,.ck
	mov	w1,'d'
	strb	w1,[x21],1
.ck:	tbz	x0,1,.c
	mov	w1,'c'
	strb	w1,[x21],1
	b	.c
.end:	strb	wzr,[x21]
	mov	x0,x20
.send:	bl	uk_menu_set
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = evenement (0 : ui_ev) -> x0 = id de l'entree dont c'est le
; raccourci (-1 : aucune ; entree grisee : -1)
ui_menu_key:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbnz	x19,.ev
	adr	x19,uk_vars
	ldr	x19,[x19]
	add	x19,x19,UV_EV
.ev:	mov	x0,-1
	ldr	w1,[x19,EV_TYPE]
	cmp	w1,WEV_KEY
	b.ne	.out
	ldr	w0,[x19,EV_WIN]			; les menus de la fenetre de la touche
	bl	uk_route
	mov	x20,x0
	mov	x0,x19
	bl	uk_menu_key_in
	cbz	x20,.out
	str	x0,[sp,-16]!
	mov	x0,x20
	bl	ui_window_use
	ldr	x0,[sp],16
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = evenement (touche) de la fenetre courante -> x0 = id, -1 : aucun
uk_menu_key_in:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x0,-1
	ldr	w1,[x19,EV_KIND]
	cbz	w1,.out
	bl	uk_menu_base			; lignes « I<id>[d][c]\t...\t<raccourci> »
	mov	x20,x1
	adr	x0,uk_menu_src
	ldr	x0,[x0]
	mov	x1,-1
	cbz	x0,.out
.line:	ldrb	w0,[x20]
	cbz	w0,.none
	cmp	w0,'I'
	b.ne	.skip
	add	x21,x20,1			; id
	mov	x22,0
.d:	ldrb	w0,[x21]
	sub	w1,w0,'0'
	cmp	w1,10
	b.hs	.did
	mov	x2,10
	madd	x22,x22,x2,x1
	add	x21,x21,1
	b	.d
.did:	cmp	w0,'d'				; grisee : sautee
	b.eq	.skip
	mov	x23,0				; le raccourci : apres la 2e tabulation
.t:	ldrb	w0,[x21],1
	cbz	w0,.skipb
	cmp	w0,10
	b.eq	.line2
	cmp	w0,9
	b.ne	.t
	add	x23,x23,1
	cmp	x23,2
	b.lo	.t
	mov	x0,x21
	mov	x1,x19
	bl	uk_shortcut_match
	cbz	x0,.skip
	mov	x0,x22
	b	.out
.line2:	mov	x20,x21
	b	.line
.skipb:	sub	x21,x21,1
	mov	x20,x21
	b	.line
.skip:	ldrb	w0,[x20],1			; ligne suivante
	cbz	w0,.none
	cmp	w0,10
	b.ne	.skip
	b	.line
.none:	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = raccourci (« Ctrl+Maj+S », « F3 »... jusqu'a la fin de ligne), x1 =
; evenement de touche -> x0 = 1 s'il y correspond (feuille)
uk_shortcut_match:
	mov	x2,0				; modificateurs demandes : 1 Ctrl, 2 Maj, 4 Alt
.p:	ldrb	w3,[x0]
	ldrb	w4,[x0,1]
	ldrb	w5,[x0,2]
	cmp	w3,'C'				; « Ctrl+ »
	b.ne	.m
	cmp	w4,'t'
	b.ne	.m
	orr	x2,x2,1
	add	x0,x0,5
	b	.p
.m:	cmp	w3,'M'				; « Maj+ »
	b.ne	.a
	cmp	w4,'a'
	b.ne	.a
	cmp	w5,'j'
	b.ne	.a
	orr	x2,x2,2
	add	x0,x0,4
	b	.p
.a:	cmp	w3,'A'				; « Alt+ »
	b.ne	.key
	cmp	w4,'l'
	b.ne	.key
	orr	x2,x2,4
	add	x0,x0,4
	b	.p
.key:	ldr	w6,[x1,EV_MODS]			; modificateurs de l'evenement
	mov	x7,0
	mov	w8,0x11
	tst	w6,w8
	b.eq	.k1
	orr	x7,x7,1
.k1:	mov	w8,0x22
	tst	w6,w8
	b.eq	.k2
	orr	x7,x7,2
.k2:	mov	w8,0x44
	tst	w6,w8
	b.eq	.k3
	orr	x7,x7,4
.k3:	cmp	x7,x2
	b.ne	.no
	ldr	w6,[x1,EV_CODE]			; touche (code HID)
	cmp	w3,'F'				; « F1 » .. « F12 »
	b.ne	.letter
	sub	w4,w4,'0'
	cmp	w4,10
	b.hs	.letter
	sub	w5,w5,'0'
	cmp	w5,10
	b.hs	.f1
	mov	w7,10
	madd	w4,w4,w7,w5
.f1:	add	w4,w4,0x3A-1
	cmp	w6,w4
	b.ne	.no
	b	.yes
.letter:					; une lettre ou un chiffre (la position :
	orr	w3,w3,0x20			; code HID de la disposition US)
	sub	w4,w3,'a'
	cmp	w4,26
	b.hs	.digit
	add	w4,w4,4
	b	.cmp
.digit:	sub	w4,w3,'1'
	cmp	w4,9
	b.hs	.zero
	add	w4,w4,0x1E
	b	.cmp
.zero:	cmp	w3,'0'
	b.ne	.no
	mov	w4,0x27
.cmp:	cmp	w6,w4
	b.ne	.no
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  Polices : les trois du programme et les autres DejaVu de SD0:/fonts,
;  chargees au premier besoin dans le kit
; ---------------------------------------------------------------------------

UK_NFONTS	= 10

; x0 = police (UFN_SANS, UFN_BOLD, UFN_MONO : celles du programme ;
; UFN_ITALIC, UFN_BOLDITALIC, UFN_MONOBOLD, UFN_SERIF, UFN_SERIFBOLD,
; UFN_SERIFITALIC, UFN_SERIFBOLDITALIC : lues au premier besoin) -> x0 =
; police pour ui_text (absente : la reguliere)
ui_font_get:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	x19,UK_NFONTS
	b.hs	.reg
	cmp	x19,3
	b.hs	.kit
	adr	x0,uk_vars			; celles du programme
	ldr	x0,[x0]
	add	x0,x0,UV_FONT
	mov	x1,UI_FONT_SIZE
	madd	x0,x19,x1,x0
	b	.out
.kit:	adr	x20,uk_font_st			; 0 inconnue, 1 chargee, 2 absente
	ldrb	w0,[x20,x19]
	cbnz	w0,.known
	mov	w0,2
	strb	w0,[x20,x19]
	adr	x0,uk_font_paths		; son chemin (le x19 - 3 eme)
	sub	x1,x19,3
.p:	cbz	x1,.load
	ldrb	w2,[x0],1
	cbnz	w2,.p
	sub	x1,x1,1
	b	.p
.load:	sub	x1,x19,3
	adr	x2,uk_fonts
	mov	x3,UI_FONT_SIZE
	madd	x1,x1,x3,x2
	bl	ui_load_font
	cbz	x0,.known
	mov	w0,1
	strb	w0,[x20,x19]
.known:	ldrb	w0,[x20,x19]
	cmp	w0,1
	b.ne	.reg
	sub	x1,x19,3
	adr	x0,uk_fonts
	mov	x3,UI_FONT_SIZE
	madd	x0,x1,x3,x0
	b	.out
.reg:	bl	uk_font_r
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = police italique (ui_font_get(UFN_ITALIC))
ui_font_italic:
	mov	x0,3
	b	ui_font_get

uk_font_paths:	db 'SD0:/fonts/DejaVuSans-Oblique.ttf',0
		db 'SD0:/fonts/DejaVuSans-BoldOblique.ttf',0
		db 'SD0:/fonts/DejaVuSansMono-Bold.ttf',0
		db 'SD0:/fonts/DejaVuSerif.ttf',0
		db 'SD0:/fonts/DejaVuSerif-Bold.ttf',0
		db 'SD0:/fonts/DejaVuSerif-Italic.ttf',0
		db 'SD0:/fonts/DejaVuSerif-BoldItalic.ttf',0
		align 4
