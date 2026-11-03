; ============================================================================
;  elegant/menu.inc - menus des applications, pour la barre de menus globale
;  (comme Onyx : EL_OP_MENU_SET, GetActiveMenu, SendMenuCommand)
;
;  Un menu par application, comme sur Mac : celui de la premiere fenetre du
;  programme qui en a un (menu_owner), quelle que soit la fenetre active, et
;  le titre de sa premiere fenetre (win_app). Une fenetre garde son texte de
;  menus (WS_MENU_MAX octets au plus ; menu_specs : un emplacement par place
;  de fenetre). Le programme l'envoie en morceaux (WS_OP_MENU_SET : b decalage,
;  c longueur totale, +40 longueur du morceau) ; le dernier morceau le publie.
;  La barre de menus le lit (WS_OP_MENU_GET, meme decoupage) et renvoie les
;  choix (WS_OP_MENU_COMMAND : WEV_MENU a la fenetre active, WEV_CLOSE a la
;  premiere pour Quitter ; rien pendant un dialogue modal).
;
;  Numero de serie : chaque envoi donne au menu une generation unique
;  (W_MGEN) ; MENU_GET avance le numero quand la generation du menu de
;  l'application active n'est plus celle vue la derniere fois (autre
;  application, menu remplace, titre change).
; ============================================================================

; x0 = fenetre -> x0 = son emplacement de menu (WS_MENU_MAX octets)
menu_spec_of:
	adr	x1,windows
	sub	x0,x0,x1
	lsr	x0,x0,WIN_SHIFT
	adr	x1,menu_specs
	mov	x2,WS_MENU_MAX
	madd	x0,x0,x2,x1
	ret

; -> x0 = fenetre active, si c'est une fenetre normale visible (0 : aucune)
menu_active:
	adr	x0,win_focus
	ldr	x0,[x0]
	cbz	x0,.ret
	ldr	x1,[x0,W_MINI]
	cbnz	x1,.none
	ldr	x1,[x0,W_FLAGS]
	mov	x2,WF_BACK or WF_TOP or WF_NOFOCUS
	tst	x1,x2
	b.ne	.none
.ret:	ret
.none:	mov	x0,0
	ret

; x0 = fenetre : nouvelle generation de son menu
menu_new_gen:
	adr	x1,menu_gen
	ldr	x2,[x1]
	add	x2,x2,1
	str	x2,[x1]
	str	x2,[x0,W_MGEN]
	ret

; x0 = fenetre dont le titre a change : la barre relira son menu
menu_title_changed:
	ldr	x1,[x0,W_MLEN]
	cbnz	x1,menu_new_gen
	ret

; WS_OP_MENU_SET : a id, b decalage, c longueur totale, +40 longueur du
; morceau, donnees : le morceau -> 0 ; le dernier morceau publie le menu
; (c = 0 : plus de menu)
req_menu_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	req_win
	cbz	x0,.bad
	mov	x19,x0
	adr	x9,msg
	ldr	x20,[x9,WM_B]			; decalage
	ldr	x21,[x9,WM_C]			; longueur totale
	ldr	w22,[x9,WM_LEN]			; morceau
	cmp	x21,WS_MENU_MAX
	b.hi	.inval
	cmp	x22,WS_MENU_PIECE
	b.hi	.inval
	cmp	x20,x21
	b.hi	.inval
	sub	x0,x21,x20
	cmp	x22,x0
	b.hi	.inval
	cbnz	x20,.copy
	str	xzr,[x19,W_MLEN]		; premier morceau : menu incomplet
.copy:	mov	x0,x19
	bl	menu_spec_of
	add	x0,x0,x20
	adr	x1,msg+WM_DATA
	mov	x2,0
.c:	cmp	x2,x22
	b.hs	.end
	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	add	x2,x2,1
	b	.c
.end:	add	x0,x20,x22
	cmp	x0,x21
	b.ne	.ok				; d'autres morceaux suivent
	str	x21,[x19,W_MLEN]
	mov	x0,x19
	bl	menu_new_gen
.ok:	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.bad:	mov	x0,E_BADF
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre -> x0 = la premiere ouverte de son programme, listee : son titre
; est le nom de l'application dans la barre, quelle que soit la fenetre active
win_app:
	ldr	x1,[x0,W_PID]
	mov	x6,x0
	ldr	x7,[x0,W_ID]
	adr	x2,windows
	mov	x3,MAX_WINDOWS
.w:	ldr	x4,[x2,W_USED]
	cbz	x4,.n
	ldr	x4,[x2,W_PID]
	cmp	x4,x1
	b.ne	.n
	ldr	x4,[x2,W_FLAGS]
	tst	x4,WF_NOLIST
	b.ne	.n
	ldr	x4,[x2,W_ID]
	cmp	x4,x7
	b.hs	.n
	mov	x7,x4
	mov	x6,x2
.n:	add	x2,x2,WIN_SIZE
	subs	x3,x3,1
	b.ne	.w
	mov	x0,x6
	ret

; x0 = fenetre -> x0 = la premiere de son programme qui a un menu (elle-meme
; si aucune)
menu_owner:
	ldr	x1,[x0,W_PID]
	mov	x6,x0
	mov	x7,-1
	adr	x2,windows
	mov	x3,MAX_WINDOWS
.w:	ldr	x4,[x2,W_USED]
	cbz	x4,.n
	ldr	x4,[x2,W_PID]
	cmp	x4,x1
	b.ne	.n
	ldr	x4,[x2,W_MLEN]
	cbz	x4,.n
	ldr	x4,[x2,W_ID]
	cmp	x4,x7
	b.hs	.n
	mov	x7,x4
	mov	x6,x2
.n:	add	x2,x2,WIN_SIZE
	subs	x3,x3,1
	b.ne	.w
	mov	x0,x6
	ret

; WS_OP_MENU_GET : a decalage -> x0 = numero de serie (0 : pas de fenetre
; active ou pas de menu) ; b id de la fenetre, c longueur totale, d longueur
; du morceau ; donnees : titre (WS_MENU_TITLE octets, NUL), puis le morceau
; (WS_MENU_GPIECE octets au plus, a partir du decalage)
req_menu_get:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x9,msg
	ldr	x20,[x9,WM_A]			; decalage
	bl	menu_active
	cbz	x0,.none
	bl	menu_owner
	mov	x19,x0
	ldr	x0,[x19,W_MLEN]
	cbz	x0,.none
	ldr	x0,[x19,W_MGEN]			; autre fenetre, autre menu : nouveau
	adr	x1,menu_last_gen		; numero
	ldr	x2,[x1]
	cmp	x0,x2
	b.eq	.same
	str	x0,[x1]
	adr	x1,menu_serial
	ldr	x2,[x1]
	add	x2,x2,1
	str	x2,[x1]
.same:	mov	x0,x19
	bl	menu_spec_of
	mov	x21,x0
	ldr	x1,[x19,W_MLEN]
	cmp	x20,x1				; morceau : [decalage, decalage + 336[
	csel	x20,x1,x20,hi
	sub	x22,x1,x20
	mov	x3,WS_MENU_GPIECE
	cmp	x22,x3
	csel	x22,x3,x22,hi
	adr	x9,msg
	ldr	x0,[x19,W_ID]
	stp	x0,x1,[x9,WM_B]			; id, longueur totale
	str	x22,[x9,WM_D]
	mov	x0,x19				; titre : celui de l'application
	bl	win_app
	add	x1,x0,W_TITLE
	add	x0,x9,WM_DATA
	mov	x2,WS_MENU_TITLE
	bl	copy_title
	adr	x0,msg+WM_DATA+WS_MENU_TITLE	; morceau
	add	x1,x21,x20
	mov	x2,0
.c:	cmp	x2,x22
	b.hs	.done
	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	add	x2,x2,1
	b	.c
.done:	adr	x0,menu_serial
	ldr	x0,[x0]
	b	.out
.none:	adr	x1,menu_last_gen
	str	xzr,[x1]
	adr	x9,msg
	stp	xzr,xzr,[x9,WM_B]
	str	xzr,[x9,WM_D]
	strb	wzr,[x9,WM_DATA]
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; WS_OP_MENU_COMMAND : a numero de l'entree (-1 : Quitter), b numero de serie
; vu par la barre (0 : sans controle) -> 0 ; E_AGAIN si le menu a change
; entre-temps. La fenetre active recoit WEV_MENU (EV_CODE = numero) ; la
; premiere du programme recoit WEV_CLOSE pour Quitter (comme sa case de
; fermeture). Pendant un dialogue modal, rien.
req_menu_command:
	prologue
	str	x19,[sp,-16]!
	bl	menu_active
	mov	x19,x0
	cbz	x19,.bad
	ldr	x0,[x19,W_FLAGS]
	mov	x1,0
	tbnz	x0,WFB_MODAL,.done
	adr	x9,msg
	ldr	x0,[x9,WM_B]
	cbz	x0,.go
	adr	x1,menu_serial
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.stale
	mov	x0,x19
	bl	menu_owner
	ldr	x0,[x0,W_MGEN]
	adr	x1,menu_last_gen
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.stale
.go:	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w0,WEV_MENU
	adr	x9,msg
	ldr	x2,[x9,WM_A]
	cmn	x2,1
	b.ne	.ev
	mov	w0,WEV_CLOSE
	mov	x2,0
	stp	x0,x1,[sp,-16]!			; Quitter : a l'application
	str	x2,[sp,-16]!
	mov	x0,x19
	bl	win_app
	mov	x19,x0
	ldr	x2,[sp],16
	ldp	x0,x1,[sp],16
.ev:	str	w0,[x1]
	str	w2,[x1,EV_CODE]
	mov	x0,x19
	bl	win_push
.done:	mov	x0,0
	b	.out
.stale:	mov	x0,E_AGAIN
	b	.out
.bad:	mov	x0,E_BADF
.out:	ldr	x19,[sp],16
	epilogue
