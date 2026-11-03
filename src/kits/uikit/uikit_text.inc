; ============================================================================
;  kits/uikit_text.inc - UIKit : le champ de texte (UT_TEXT), le texte des
;  widgets (ui_w_set_text / ui_w_get_text), le presse-papiers du systeme.
;
;  Champ : une ligne UTF-8 ; curseur (VALUE, en octets), ancre de la
;  selection (ANCHOR), defilement horizontal (POS, en pixels), indication
;  (AUX2) ; clic, glisser, double-clic (un mot), Maj + fleches, Debut, Fin,
;  Ctrl + fleches (mot), Ctrl+A, C, X, V, Retour arriere, Suppr ; mot de
;  passe (UTF_PASSWORD : des « • »), lecture seule (UTF_READONLY), police
;  mono (UTF_MONO). Entree : UN_ACTIVATE ; un changement : UN_CHANGE.
;  Longueur max : MAX (0 : 4096 octets). docs/DESIGN-ELEGANT.md 11.2.
;
;  Presse-papiers : par clipd, sinon tenu par Elegant (WS_OP_CLIP_SET / GET,
;  elegant/clip.inc) : uikit_clip.inc.
; ============================================================================

UK_TEXT_MAX	= 4096
MOD_CTRL	= 0x11
MOD_SHIFT	= 0x22
HID_A		= 0x04
HID_C		= 0x06
HID_V		= 0x19
HID_X		= 0x1B

; ---------------------------------------------------------------------------
;  Presse-papiers
; ---------------------------------------------------------------------------

; x0 = texte, x1 = octets (-1 : jusqu'au NUL) -> x0 = 0 (< 0 : erreur)
uk_ws_clip_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmn	x20,1
	b.ne	.len
	bl	uk_strlen
	mov	x20,x0
.len:	mov	x0,WS_CLIP_MAX
	cmp	x20,x0
	csel	x20,x0,x20,hi
	mov	x21,0				; decalage
.piece:	sub	x22,x20,x21
	cmp	x22,WS_CLIP_PIECE
	mov	x0,WS_CLIP_PIECE
	csel	x22,x0,x22,hi
	adr	x9,uk_msg
	stp	x21,x20,[x9,WM_A]
	str	w22,[x9,WM_LEN]
	add	x0,x9,WM_DATA
	add	x1,x19,x21
	mov	x2,x22
	bl	uk_memcpy
	mov	x0,WS_OP_CLIP_SET
	bl	uk_call
	tbnz	x0,63,.out
	add	x21,x21,x22
	cmp	x21,x20
	b.lo	.piece
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon, x1 = taille du tampon -> x0 = octets lus (texte termine par
; NUL dans le tampon ; < 0 : erreur)
uk_ws_clip_get:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	sub	x20,x1,1			; (place du NUL)
	mov	x21,0
	tbnz	x20,63,.err
.piece:	cmp	x21,x20
	b.hs	.end
	adr	x9,uk_msg
	str	x21,[x9,WM_A]
	mov	x0,WS_OP_CLIP_GET
	bl	uk_call
	tbnz	x0,63,.err
	cbz	x0,.end
	sub	x2,x20,x21
	cmp	x0,x2
	csel	x2,x2,x0,hi
	mov	x22,x2
	add	x0,x19,x21
	adr	x1,uk_msg+WM_DATA
	bl	uk_memcpy
	add	x21,x21,x22
	adr	x9,uk_msg			; fin du contenu ?
	ldr	x0,[x9,WM_B]
	cmp	x21,x0
	b.lo	.piece
.end:	strb	wzr,[x19,x21]
	mov	x0,x21
	b	.out
.err:	cbz	x19,.out
	tbnz	x20,63,.out
	strb	wzr,[x19]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include 'uikit_clip.inc'

; ---------------------------------------------------------------------------
;  Texte d'un widget
; ---------------------------------------------------------------------------

; x0 = widget, x1 = texte (0 : vide), x2 = octets (-1 : jusqu'au NUL) : le
; garde dans son tampon (UW_TEXT, termine par NUL) -> x0 = 1 (0 : memoire)
uk_set_text:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x20,uk_empty
	cbz	x1,.e
	mov	x20,x1
.e:	mov	x21,x2
	cmn	x21,1
	b.ne	.n
	mov	x0,x20
	bl	uk_strlen
	mov	x21,x0
.n:	mov	x0,x19
	add	x1,x21,1
	bl	uk_text_room
	cbz	x0,.out
	ldr	x0,[x19,UW_TEXT]
	mov	x1,x20
	mov	x2,x21
	bl	uk_memcpy
	strb	wzr,[x0]
	str	x21,[x19,UW_TLEN]
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1 = octets necessaires (NUL compris) : agrandit son tampon
; -> x0 = 1 (0 : plus de memoire)
uk_text_room:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x0,[x19,UW_TCAP]
	cmp	x20,x0
	b.ls	.ok
	lsl	x21,x20,1			; (de la marge)
	cmp	x21,64
	mov	x0,64
	csel	x21,x0,x21,lo
	mov	x0,x21
	bl	uk_alloc
	cbz	x0,.out
	ldr	x1,[x19,UW_TEXT]
	ldr	x2,[x19,UW_TLEN]
	cbz	x1,.new
	add	x2,x2,1
	str	x0,[sp,-16]!
	bl	uk_memcpy
	ldr	x0,[x19,UW_TEXT]
	bl	uk_free
	ldr	x0,[sp],16
.new:	str	x0,[x19,UW_TEXT]
	str	x21,[x19,UW_TCAP]
.ok:	mov	x0,1
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1 = texte : son texte (champ, etiquette, bouton...), redessine
ui_w_set_text:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	ldr	w2,[x19,UW_TYPE]		; liste modifiable : son champ
	cmp	w2,UT_COMBO
	b.ne	.own
	ldr	x19,[x19,UW_AUX2]
	cbz	x19,.out
	mov	x0,x19
.own:
	mov	x2,-1
	bl	uk_set_text
	ldr	x0,[x19,UW_TLEN]		; champ : curseur a la fin, sans selection
	str	x0,[x19,UW_VALUE]
	str	x0,[x19,UW_ANCHOR]
	str	xzr,[x19,UW_POS]
	ldr	w0,[x19,UW_TYPE]		; texte multiligne : au debut
	cmp	w0,UT_AREA
	b.ne	.draw
	str	xzr,[x19,UW_VALUE]
	str	xzr,[x19,UW_ANCHOR]
	str	xzr,[x19,UW_DRAG]
	mov	x0,x19
	bl	uk_undo_clear
	mov	x0,x19
	bl	uk_ta_sync
.draw:	mov	x0,x19
	bl	uk_w_refresh
.out:	ldr	x19,[sp],16
	epilogue

; x0 = widget -> x0 = longueur de son texte en octets
ui_w_text_len:
	cbz	x0,.ret
	ldr	w1,[x0,UW_TYPE]
	cmp	w1,UT_COMBO
	b.ne	.own
	ldr	x0,[x0,UW_AUX2]
	cbz	x0,.ret
.own:	ldr	x0,[x0,UW_TLEN]
.ret:	ret

; x0 = widget -> x0 = son texte (tampon du kit, valable jusqu'au prochain
; changement ; jamais 0)
ui_w_get_text:
	cbz	x0,.e
	ldr	w1,[x0,UW_TYPE]			; liste modifiable : son champ
	cmp	w1,UT_COMBO
	b.ne	.own
	ldr	x0,[x0,UW_AUX2]
	cbz	x0,.e
.own:
	ldr	x0,[x0,UW_TEXT]
	cbz	x0,.e
	ret
.e:	adr	x0,uk_empty
	ret

; ---------------------------------------------------------------------------
;  Champ de texte
; ---------------------------------------------------------------------------

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, x4 = drapeaux (UTF_*) -> x0 =
; poignee
ui_textbox:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_TEXT
	bl	uk_w_new
	cbz	x0,.out
	mov	x20,x0
	str	x19,[x20,UW_STYLE]
	mov	x1,0
	mov	x2,0
	bl	uk_set_text
	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = champ, x1 = indication (montree quand il est vide)
ui_textbox_hint:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	mov	x0,x1
	bl	uk_strdup
	mov	x20,x0
	ldr	x0,[x19,UW_AUX2]
	bl	uk_free
	str	x20,[x19,UW_AUX2]
	mov	x0,x19
	bl	uk_w_refresh
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = champ, x1 = debut, x2 = fin (octets ; -1 : la fin du texte) : choisit
; ce texte (curseur a la fin)
ui_textbox_select:
	prologue
	cbz	x0,.out
	ldr	x3,[x0,UW_TLEN]
	cmp	x1,x3
	csel	x1,x3,x1,hi
	cmp	x2,x3
	csel	x2,x3,x2,hi
	str	x1,[x0,UW_ANCHOR]
	str	x2,[x0,UW_VALUE]
	bl	uk_w_refresh
.out:	epilogue

; x0 = champ -> x0 = debut, x1 = fin de la selection (octets)
ui_textbox_selection:
	ldr	x1,[x0,UW_VALUE]
	ldr	x2,[x0,UW_ANCHOR]
	cmp	x1,x2
	csel	x0,x1,x2,lo
	csel	x1,x2,x1,lo
	ret

; x0 = champ -> x0 = police, x1 = taille px
uk_tb_font:
	prologue
	stp	x0,x0,[sp,-16]!
	bl	uk_met
	ldr	x1,[x0,MT_PX]
	ldr	x2,[sp]
	ldr	x3,[x2,UW_PX]			; taille du programme (texte multiligne)
	cbz	x3,.d
	mov	x1,x3
.d:	ldr	x2,[x2,UW_STYLE]
	add	sp,sp,16
	tbnz	x2,2,.m				; UTF_MONO
	bl	uk_font_r
	b	.out
.m:	bl	uk_font_m
.out:	epilogue

; x0 = champ -> x0 = texte montre (le texte, ou des « • » pour un mot de passe)
uk_tb_disp:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,UW_TEXT]
	ldr	x1,[x19,UW_STYLE]
	tbz	x1,0,.out			; UTF_PASSWORD
	ldr	x0,[x19,UW_TLEN]		; 3 octets par point de code
	mov	x1,0
	adr	x2,uk_pwbuf
	ldr	x3,[x19,UW_TEXT]
.l:	cmp	x1,x0
	b.hs	.end
	ldrb	w4,[x3,x1]
	add	x1,x1,1
	and	w4,w4,0xC0
	cmp	w4,0x80
	b.eq	.l
	adr	x5,uk_pwbuf+UK_PW_MAX-4
	cmp	x2,x5
	b.hs	.end
	mov	w4,0xE2				; « • » U+2022
	strb	w4,[x2]
	mov	w4,0x80
	strb	w4,[x2,1]
	mov	w4,0xA2
	strb	w4,[x2,2]
	add	x2,x2,3
	b	.l
.end:	strb	wzr,[x2]
	adr	x0,uk_pwbuf
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = champ, x1 = decalage dans le texte -> x0 = decalage dans le texte
; montre (feuille)
uk_tb_to_disp:
	ldr	x2,[x0,UW_STYLE]
	tbz	x2,0,.same
	ldr	x2,[x0,UW_TEXT]
	mov	x3,0
	mov	x0,0
.l:	cmp	x3,x1
	b.hs	.ret
	ldrb	w4,[x2,x3]
	add	x3,x3,1
	and	w4,w4,0xC0
	cmp	w4,0x80
	b.eq	.l
	add	x0,x0,3
	b	.l
.same:	mov	x0,x1
.ret:	ret

; x0 = champ, x1 = decalage dans le texte montre -> x0 = decalage dans le
; texte (feuille)
uk_tb_from_disp:
	ldr	x2,[x0,UW_STYLE]
	tbz	x2,0,.same
	mov	x3,3
	udiv	x1,x1,x3			; points de code
	ldr	x2,[x0,UW_TEXT]
	ldr	x5,[x0,UW_TLEN]
	mov	x0,0
.l:	cmp	x0,x5
	b.hs	.ret
	ldrb	w4,[x2,x0]
	and	w4,w4,0xC0
	cmp	w4,0x80
	b.eq	.c
	cbz	x1,.ret
	sub	x1,x1,1
.c:	add	x0,x0,1
	b	.l
.same:	mov	x0,x1
.ret:	ret

; x0 = champ, x1 = decalage dans le texte -> x0 = x en pixels depuis le
; debut du texte
uk_tb_xof:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	uk_tb_to_disp
	mov	x20,x0
	mov	x0,x19
	bl	uk_tb_disp
	str	x0,[sp,-16]!
	mov	x0,x19
	bl	uk_tb_font
	mov	x3,x0
	mov	x2,x1
	ldr	x0,[sp],16
	mov	x1,x20
	bl	ui_text_prefix
	ldp	x19,x20,[sp],16
	epilogue

; x0 = champ : defilement pour que le curseur soit visible
uk_tb_scroll:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,UW_VALUE]
	bl	uk_tb_xof
	ldr	w1,[x19,UW_W]			; largeur utile
	sub	x1,x1,22
	ldr	x2,[x19,UW_POS]
	sub	x3,x0,x2
	cmp	x3,0
	b.ge	.r
	mov	x2,x0
	b	.st
.r:	cmp	x3,x1
	b.le	.st
	sub	x2,x0,x1
.st:	str	x2,[x19,UW_POS]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = champ -> x0 = x du debut de son texte (10 ; UTF_RIGHT : colle a droite
; tant qu'il tient)
uk_tb_x0:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,UW_STYLE]
	mov	x0,10
	tbz	x1,8,.out
	ldr	x1,[x19,UW_TLEN]
	mov	x0,x19
	bl	uk_tb_xof
	ldr	w1,[x19,UW_W]
	sub	x1,x1,10
	sub	x0,x1,x0
	cmp	x0,10
	mov	x1,10
	csel	x0,x1,x0,lt
.out:	ldr	x19,[sp],16
	epilogue

uk_tb_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x19
	bl	uk_w_focused
	mov	x24,x0
	mov	x0,x19
	bl	uk_w_enabled
	mov	x25,x0
	jet	w5,TJ_LIGNE_FORTE		; contour : LIGNE_FORTE, ACCENT (focus)
	cbz	x24,.c
	jet	w5,TJ_ACCENT
.c:	ldr	x0,[x19,UW_STYLE]		; UTF_ERROR : DANGER, focus ou non
	tbz	x0,9,.c1
	jet	w5,TJ_DANGER
.c1:	mov	w6,w5
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	mov	x4,8
	bl	ui_rbox
	jet	w5,TJ_SURFACE			; interieur : SURFACE, FACE (grise)
	cbnz	x25,.i
	jet	w5,TJ_FACE
.i:	mov	w6,w5
	add	x0,x20,1
	add	x1,x21,1
	sub	x2,x22,2
	sub	x3,x23,2
	mov	x4,7
	bl	ui_rbox
	add	x0,x20,8			; zone du texte
	add	x1,x21,2
	sub	x2,x22,16
	sub	x3,x23,4
	bl	uk_clip_push
	cbz	x24,.noscr			; (sans focus : le defilement reste)
	mov	x0,x19
	bl	uk_tb_scroll
.noscr:
	mov	x0,x19
	bl	uk_tb_x0
	add	x26,x20,x0
	ldr	x0,[x19,UW_POS]
	sub	x26,x26,x0			; x du debut du texte
	ldr	x0,[x19,UW_TLEN]
	cbz	x0,.hint
	mov	x0,x19				; selection
	bl	ui_textbox_selection
	cmp	x0,x1
	b.eq	.text
	str	x1,[sp,-16]!
	mov	x1,x0
	mov	x0,x19
	bl	uk_tb_xof
	mov	x22,x0
	ldr	x1,[sp],16
	mov	x0,x19
	bl	uk_tb_xof
	sub	x2,x0,x22
	add	x0,x26,x22
	add	x1,x21,4
	sub	x3,x23,8
	jet	w4,TJ_ACCENT_CLAIR
	cbnz	x24,.sc
	jet	w4,TJ_CREUX
.sc:	bl	ui_fill
	ldr	w22,[x19,UW_W]
.text:	mov	x0,x19
	bl	uk_tb_disp
	mov	x22,x0
	mov	x0,x19
	bl	uk_tb_font
	mov	x5,x0
	mov	x7,x1
	mov	x0,x22
	mov	x1,x26
	mov	x2,x21
	mov	x3,x23
	jet	w4,TJ_TEXTE1
	cbnz	x25,.tc
	jet	w4,TJ_TEXTE3
.tc:	mov	x6,0
	bl	uk_text_box
	b	.caret
.hint:	ldr	x0,[x19,UW_AUX2]		; vide : l'indication
	cbz	x0,.caret
	str	x0,[sp,-16]!
	mov	x0,x19
	bl	uk_tb_font
	mov	x5,x0
	mov	x7,x1
	ldr	x0,[sp],16
	add	x1,x20,10
	mov	x2,x21
	mov	x3,x23
	jet	w4,TJ_TEXTE3
	mov	x6,0
	bl	uk_text_box
.caret:	cbz	x24,.out			; curseur : 1 x 16, ACCENT
	ldr	x0,[x19,UW_STYLE]
	tbnz	x0,1,.out			; (lecture seule : sans curseur)
	ldr	x1,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_tb_xof
	add	x0,x26,x0
	sub	x1,x23,16
	asr	x1,x1,1
	add	x1,x21,x1
	mov	x2,1
	mov	x3,16
	jet	w4,TJ_ACCENT
	bl	ui_fill
.out:	bl	uk_clip_pop
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = champ, x1 = x local -> x0 = decalage dans le texte sous ce point
uk_tb_hit:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	uk_tb_x0
	ldr	x2,[x19,UW_POS]
	add	x20,x20,x2
	sub	x20,x20,x0
	cmp	x20,0
	csel	x20,xzr,x20,lt
	mov	x0,x19
	bl	uk_tb_disp
	str	x0,[sp,-16]!
	mov	x0,x19
	bl	uk_tb_font
	mov	x2,x0
	ldr	x0,[sp],16
	mov	x3,x20
	bl	ui_text_hit
	mov	x1,x0
	mov	x0,x19
	bl	uk_tb_from_disp
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = decalage, x2 = longueur -> x0 = debut du point de code
; suivant (feuille)
uk_u8_next:
	cmp	x1,x2
	b.hs	.ret
	add	x1,x1,1
.l:	cmp	x1,x2
	b.hs	.ret
	ldrb	w3,[x0,x1]
	and	w3,w3,0xC0
	cmp	w3,0x80
	b.ne	.ret
	add	x1,x1,1
	b	.l
.ret:	mov	x0,x1
	ret

; x0 = texte, x1 = decalage -> x0 = debut du point de code precedent (feuille)
uk_u8_prev:
	cbz	x1,.ret
	sub	x1,x1,1
.l:	cbz	x1,.ret
	ldrb	w3,[x0,x1]
	and	w3,w3,0xC0
	cmp	w3,0x80
	b.ne	.ret
	sub	x1,x1,1
	b	.l
.ret:	mov	x0,x1
	ret

; w0 = octet -> x0 = 1 s'il fait partie d'un mot (lettre, chiffre, _, UTF-8)
uk_wordch:
	cmp	w0,0x80
	b.hs	.y
	cmp	w0,'_'
	b.eq	.y
	sub	w1,w0,'0'
	cmp	w1,10
	b.lo	.y
	orr	w1,w0,0x20
	sub	w1,w1,'a'
	cmp	w1,26
	b.lo	.y
	mov	x0,0
	ret
.y:	mov	x0,1
	ret

; x0 = texte, x1 = decalage, x2 = longueur, x3 = 1 vers la droite -> x0 =
; bord de mot (Ctrl + fleches, double-clic)
uk_word_edge:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbz	x3,.left
.r1:	cmp	x20,x21				; droite : les separateurs, puis le mot
	b.hs	.out
	ldrb	w0,[x19,x20]
	bl	uk_wordch
	cbnz	x0,.r2
	add	x20,x20,1
	b	.r1
.r2:	cmp	x20,x21
	b.hs	.out
	ldrb	w0,[x19,x20]
	bl	uk_wordch
	cbz	x0,.out
	add	x20,x20,1
	b	.r2
.left:	cbz	x20,.out			; gauche : les separateurs, puis le mot
	sub	x0,x20,1
	ldrb	w0,[x19,x0]
	bl	uk_wordch
	cbnz	x0,.l2
	sub	x20,x20,1
	b	.left
.l2:	cbz	x20,.out
	sub	x0,x20,1
	ldrb	w0,[x19,x0]
	bl	uk_wordch
	cbz	x0,.out
	sub	x20,x20,1
	b	.l2
.out:	mov	x0,x20
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = genre, x2, x3 = point local, x4 = clics
uk_tb_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x4
	cmp	x20,1
	b.eq	.press
	cbnz	x20,.zero			; relache, sortie
	adr	x0,uk_capture			; deplacement : bouton tenu ?
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.zero
	mov	x0,x19
	mov	x1,x2
	bl	uk_tb_hit
	str	x0,[x19,UW_VALUE]
	b	.draw
.press:	mov	x0,x19
	mov	x1,x2
	bl	uk_tb_hit
	str	x0,[x19,UW_VALUE]
	str	x0,[x19,UW_ANCHOR]
	cmp	x21,2				; double-clic : le mot
	b.ne	.draw
	mov	x22,x0
	ldr	x0,[x19,UW_TEXT]
	mov	x1,x22
	ldr	x2,[x19,UW_TLEN]
	mov	x3,0
	bl	uk_word_edge
	str	x0,[x19,UW_ANCHOR]
	ldr	x0,[x19,UW_TEXT]
	mov	x1,x22
	ldr	x2,[x19,UW_TLEN]
	mov	x3,1
	bl	uk_word_edge
	str	x0,[x19,UW_VALUE]
.draw:	mov	x0,x19
	bl	uk_w_refresh
.zero:	mov	x0,0
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = champ : efface la selection -> x0 = 1 s'il y en avait une
uk_tb_del_sel:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	ui_textbox_selection
	cmp	x0,x1
	b.eq	.none
	mov	x2,x1
	mov	x1,x0
	mov	x0,x19
	bl	uk_tb_delete
	mov	x0,1
	b	.out
.none:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = champ, x1 = debut, x2 = fin : retire ces octets ; curseur au debut
uk_tb_delete:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	stp	x1,x2,[sp,-16]!			; (annuler : les octets retires)
	sub	x3,x2,x1
	ldr	x2,[x19,UW_TEXT]
	add	x2,x2,x1
	mov	x1,2
	bl	uk_undo_rec
	ldp	x1,x2,[sp],16
	ldr	x3,[x19,UW_TEXT]
	ldr	x4,[x19,UW_TLEN]
	sub	x5,x2,x1			; octets retires
	add	x0,x3,x1
	add	x1,x3,x2
	sub	x2,x4,x2
	add	x2,x2,1				; (NUL compris)
	str	x5,[sp,-16]!
	bl	uk_memcpy
	ldr	x5,[sp],16
	ldr	x4,[x19,UW_TLEN]
	sub	x4,x4,x5
	str	x4,[x19,UW_TLEN]
	str	x20,[x19,UW_VALUE]
	str	x20,[x19,UW_ANCHOR]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = champ, x1 = texte, x2 = octets : insere au curseur (la selection
; remplacee) -> x0 = 1 (0 : trop long)
uk_tb_insert:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	uk_tb_del_sel
	ldr	x0,[x19,UW_MAX]			; longueur max (0 : selon le type)
	cbnz	x0,.m
	mov	x0,UK_TEXT_MAX
	ldr	w1,[x19,UW_TYPE]
	cmp	w1,UT_AREA
	b.ne	.m
	mov	x0,UK_AREA_MAX
.m:	ldr	x1,[x19,UW_TLEN]
	add	x1,x1,x21
	cmp	x1,x0
	b.hi	.no
	mov	x0,x19
	add	x1,x1,1
	bl	uk_text_room
	cbz	x0,.no
	ldr	x3,[x19,UW_TEXT]		; la fin monte
	ldr	x22,[x19,UW_VALUE]
	add	x1,x3,x22
	add	x0,x1,x21
	ldr	x2,[x19,UW_TLEN]
	sub	x2,x2,x22
	add	x2,x2,1
	bl	uk_memcpy_up
	ldr	x0,[x19,UW_TEXT]
	add	x0,x0,x22
	mov	x1,x20
	mov	x2,x21
	bl	uk_memcpy
	ldr	x0,[x19,UW_TLEN]
	add	x0,x0,x21
	str	x0,[x19,UW_TLEN]
	add	x0,x22,x21
	str	x0,[x19,UW_VALUE]
	str	x0,[x19,UW_ANCHOR]
	mov	x0,x19				; (annuler : les octets inseres)
	mov	x1,1
	ldr	x2,[x19,UW_TEXT]
	add	x2,x2,x22
	mov	x4,x22
	mov	x3,x21
	bl	uk_undo_rec_at
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = champ : copie la selection dans le presse-papiers (mot de passe : non)
uk_tb_copy:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,UW_STYLE]
	tbnz	x0,0,.out
	mov	x0,x19
	bl	ui_textbox_selection
	sub	x1,x1,x0
	cbz	x1,.out
	ldr	x2,[x19,UW_TEXT]
	add	x0,x2,x0
	bl	ui_clip_set
.out:	ldr	x19,[sp],16
	epilogue

; x1 = code HID, x2 = caractere, x3 = mods -> code (-1 : non pris)
uk_tb_key:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	ldr	x23,[x19,UW_STYLE]		; bit 1 : lecture seule
	mov	x24,0				; code
	mov	w9,MOD_CTRL
	tst	w22,w9
	b.eq	.nav
	cmp	w20,HID_A			; Ctrl+A : tout
	b.ne	.c1
	str	xzr,[x19,UW_ANCHOR]
	ldr	x0,[x19,UW_TLEN]
	str	x0,[x19,UW_VALUE]
	b	.draw
.c1:	cmp	w20,HID_C
	b.ne	.c2
	mov	x0,x19
	bl	uk_tb_copy
	b	.zero
.c2:	cmp	w20,HID_X
	b.ne	.c3
	tbnz	x23,1,.zero
	mov	x0,x19
	bl	uk_tb_copy
	ldr	x0,[x19,UW_STYLE]
	tbnz	x0,0,.zero			; (mot de passe : ni copie, ni coupe)
	mov	x0,x19
	bl	uk_tb_del_sel
	cbz	x0,.zero
	b	.changed
.c3:	cmp	w20,HID_V
	b.ne	.nav
	tbnz	x23,1,.zero
	adr	x0,uk_pastebuf			; une ligne : arret a la fin de ligne
	mov	x1,UK_PASTE_MAX
	bl	ui_clip_get
	cmp	x0,0
	b.le	.zero
	mov	x2,0
	adr	x1,uk_pastebuf
.pl:	cmp	x2,x0
	b.hs	.pins
	ldrb	w3,[x1,x2]
	cmp	w3,10
	b.eq	.pins
	cmp	w3,13
	b.eq	.pins
	add	x2,x2,1
	b	.pl
.pins:	mov	x0,x19
	bl	uk_tb_insert
	b	.changed
.nav:	ldr	x1,[x19,UW_VALUE]
	ldr	x0,[x19,UW_TEXT]
	ldr	x2,[x19,UW_TLEN]
	cmp	w20,KEY_LEFT
	b.ne	.n1
	mov	w9,MOD_CTRL
	tst	w22,w9
	b.eq	.lch
	mov	x3,0
	bl	uk_word_edge
	b	.move
.lch:	mov	w9,MOD_SHIFT
	tst	w22,w9			; sans Maj, une selection : son debut
	b.ne	.lc
	str	x0,[sp,-16]!
	mov	x0,x19
	bl	ui_textbox_selection
	mov	x2,x0
	ldr	x0,[sp],16
	cmp	x2,x1
	b.eq	.lc0
	mov	x0,x2
	b	.move
.lc0:	ldr	x1,[x19,UW_VALUE]
.lc:	bl	uk_u8_prev
	b	.move
.n1:	cmp	w20,KEY_RIGHT
	b.ne	.n2
	mov	w9,MOD_CTRL
	tst	w22,w9
	b.eq	.rch
	mov	x3,1
	bl	uk_word_edge
	b	.move
.rch:	mov	w9,MOD_SHIFT
	tst	w22,w9
	b.ne	.rc
	stp	x0,x2,[sp,-16]!
	mov	x0,x19
	bl	ui_textbox_selection
	mov	x3,x0
	mov	x4,x1
	ldp	x0,x2,[sp],16
	cmp	x3,x4
	b.eq	.rc0
	mov	x0,x4
	b	.move
.rc0:	ldr	x1,[x19,UW_VALUE]
.rc:	bl	uk_u8_next
	b	.move
.n2:	cmp	w20,KEY_HOME
	b.ne	.n3
	mov	x0,0
	b	.move
.n3:	cmp	w20,KEY_END
	b.ne	.n4
	mov	x0,x2
	b	.move
.n4:	cmp	w20,KEY_ENTER
	b.ne	.n5
	mov	x24,UN_ACTIVATE
	b	.zero2
.n5:	tbnz	x23,1,.ro			; lecture seule : rien a modifier
	cmp	w20,KEY_BACKSPACE
	b.ne	.n6
	mov	x0,x19
	bl	uk_tb_del_sel
	cbnz	x0,.changed
	ldr	x1,[x19,UW_VALUE]
	cbz	x1,.zero
	ldr	x0,[x19,UW_TEXT]
	bl	uk_u8_prev
	mov	x1,x0
	ldr	x2,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_tb_delete
	b	.changed
.n6:	cmp	w20,KEY_DELETE
	b.ne	.n7
	mov	x0,x19
	bl	uk_tb_del_sel
	cbnz	x0,.changed
	ldr	x1,[x19,UW_VALUE]
	ldr	x2,[x19,UW_TLEN]
	cmp	x1,x2
	b.hs	.zero
	ldr	x0,[x19,UW_TEXT]
	bl	uk_u8_next
	mov	x2,x0
	ldr	x1,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_tb_delete
	b	.changed
.n7:	cmp	w21,' '				; un caractere : insere (UTF-8)
	b.lo	.no
	cmp	w21,0x7F
	b.eq	.no
	mov	w9,MOD_CTRL
	tst	w22,w9
	b.ne	.no
	mov	w0,w21
	adr	x1,uk_chbuf
	bl	uk_utf8_put
	mov	x2,x0
	adr	x1,uk_chbuf
	mov	x0,x19
	bl	uk_tb_insert
	cbz	x0,.zero
	b	.changed
.ro:	cmp	w21,' '
	b.hs	.zero
	b	.no
.move:	str	x0,[x19,UW_VALUE]		; Maj : la selection s'etend
	mov	w9,MOD_SHIFT
	tst	w22,w9
	b.ne	.draw
	str	x0,[x19,UW_ANCHOR]
	b	.draw
.changed:
	mov	x24,UN_CHANGE
.draw:	mov	x0,x19
	bl	uk_w_refresh
.zero2:	mov	x0,x24
	b	.out
.zero:	mov	x0,x24
	b	.draw
.no:	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = point de code, x1 = destination -> x0 = octets ecrits (1 a 4)
uk_utf8_put:
	cmp	w0,0x80
	b.hs	.u2
	strb	w0,[x1]
	mov	x0,1
	ret
.u2:	cmp	w0,0x800
	b.hs	.u3
	lsr	w2,w0,6
	orr	w2,w2,0xC0
	strb	w2,[x1]
	and	w2,w0,0x3F
	orr	w2,w2,0x80
	strb	w2,[x1,1]
	mov	x0,2
	ret
.u3:	cmp	w0,0x10000
	b.hs	.u4
	lsr	w2,w0,12
	orr	w2,w2,0xE0
	strb	w2,[x1]
	ubfx	w2,w0,6,6
	orr	w2,w2,0x80
	strb	w2,[x1,1]
	and	w2,w0,0x3F
	orr	w2,w2,0x80
	strb	w2,[x1,2]
	mov	x0,3
	ret
.u4:	lsr	w2,w0,18
	orr	w2,w2,0xF0
	strb	w2,[x1]
	ubfx	w2,w0,12,6
	orr	w2,w2,0x80
	strb	w2,[x1,1]
	ubfx	w2,w0,6,6
	orr	w2,w2,0x80
	strb	w2,[x1,2]
	and	w2,w0,0x3F
	orr	w2,w2,0x80
	strb	w2,[x1,3]
	mov	x0,4
	ret
