; ============================================================================
;  clipboard - le widget du presse-papiers (apps/clipboard d'Onyx), lance par le
;  bouton du panel : une carte en bas a droite de l'ecran, au-dessus du panel.
;  La liste de l'anneau de clipd, la plus recente en tete : le genre (une pastille),
;  une ligne (une image : sa taille seule), l'heure, sa croix. Un clic met le
;  curseur sur la ligne (Ctrl+V la colle), la croix la retire, la corbeille
;  retire tout. Fleches et Suppr au clavier. Se ferme sur Echap, Entree, quand
;  une autre fenetre prend le clavier, ou par un second clic sur le bouton du
;  panel (service « clipwidget » : un second clipboard le ferme et s'arrete).
;  Client de clipd (inc/clip.inc) : relit l'anneau quand son numero de serie
;  change, 4 fois par seconde tant que la carte est ouverte.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../inc/clip.inc'

CW		= 300				; largeur
ROW		= 30
HEAD		= 42
PAD		= 8
EDGE		= 4				; ombre
MARGIN		= 12				; du bord droit
LIFT		= 8				; au-dessus du panel
XW		= 30				; la croix : a droite de la ligne
TXT_X		= 44
OP_WM		= 512

; une ligne
IT_ID		= 0
IT_CUR		= 8
IT_HM		= 16
IT_DATA		= 24				; donnees de CLIP_INFO (CI_*)
IT_SIZE		= IT_DATA + 464

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'

macro ldv reg, var {
	adr	x16,var
	ldr	reg,[x16]
}
macro stv reg, var {
	adr	x16,var
	str	reg,[x16]
}

start:
	bl	kits_open
	adr	x0,s_me				; deja ouvert : on le ferme
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbz	x0,63,.alone
	adr	x0,s_me
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x1,msg
	mov	x2,1
	str	x2,[x1]
	sys	SYS_SVC_CALL
	b	quit
.alone:	stv	x0,g_me
	adr	x0,ui_win
	bl	ui_init
	bl	ui_jetons
	stv	x0,g_tok
	adr	x0,scr
	bl	ui_screen
	mov	x0,-1				; (l'anneau ne peut qu'avoir change)
	stv	x0,g_serial
	bl	read_ring
	bl	open_win
	tbnz	x0,63,quit
	mov	x0,1
	mov	x1,1
	bl	ui_min_size
	bl	draw
.loop:	ldv	x0,g_me				; un second clipboard : fermer
	adr	x1,msg
	mov	x2,0
	sys	SYS_SVC_NEXT
	cmp	x0,0
	b.le	.ev
	adr	x1,msg
	str	xzr,[x1]
	sys	SYS_SVC_REPLY
	b	quit
.ev:	mov	x0,250
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_FOCUS
	b.eq	.focus
	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cbnz	x0,.loop
.poll:	bl	refresh
	b	.loop
.focus:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cbz	w0,quit
	b	.loop
.key:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cbz	w0,.loop
	ldr	w0,[x9,EV_CODE]
	cmp	w0,KEY_ESC
	b.eq	quit
	cmp	w0,KEY_ENTER
	b.eq	quit
	bl	on_key
	b	.poll
.mouse:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	ldrsw	x1,[x9,EV_X]
	ldrsw	x2,[x9,EV_Y]
	cmp	w0,1
	b.eq	.press
	mov	x0,x1
	mov	x1,x2
	bl	on_move
	b	.loop
.press:	mov	x0,x1
	mov	x1,x2
	bl	on_press
	b	.poll
quit:	mov	x0,0
	sys	SYS_EXIT

; x0 = op, x1 = a -> x0 = resultat ; le message reste dans msg
clipd:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,s_svc
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	adr	x1,msg
	str	x19,[x1,WM_OP]
	str	x20,[x1,WM_A]
	sys	SYS_SVC_CALL
.out:	ldp	x19,x20,[sp],16
	epilogue

; l'anneau, s'il a change -> x0 = 1 si on l'a relu
read_ring:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,CLIP_STATE
	mov	x1,0
	bl	clipd
	mov	x19,x0				; (pas de service : -1, liste vide)
	ldv	x1,g_serial
	cmp	x0,x1
	b.ne	.read
	mov	x0,0
	b	.out
.read:	stv	x19,g_serial
	mov	x19,0
	adr	x20,g_items
.i:	cmp	x19,CLIP_RING
	b.hs	.done
	mov	x0,CLIP_INFO
	mov	x1,x19
	bl	clipd
	cmp	x0,1
	b.ne	.done
	adr	x1,msg
	ldr	x2,[x1,WM_A]
	str	x2,[x20,IT_ID]
	ldr	x2,[x1,WM_B]
	str	x2,[x20,IT_CUR]
	ldr	x2,[x1,WM_C]
	str	x2,[x20,IT_HM]
	add	x0,x20,IT_DATA
	add	x1,x1,WM_DATA
	mov	x2,464
	bl	mem_move
	add	x19,x19,1
	movl	x0,IT_SIZE
	add	x20,x20,x0
	b	.i
.done:	stv	x19,g_n
	ldv	x0,g_hot
	cmp	x0,x19
	b.lt	.ok
	mov	x0,-1
	stv	x0,g_hot
.ok:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = hauteur de la carte pour g_n lignes
card_h:
	ldv	x0,g_n
	cmp	x0,1
	mov	x1,1
	csel	x0,x1,x0,lo
	mov	x1,ROW
	mul	x0,x0,x1
	add	x0,x0,HEAD+PAD+EDGE*2
	ret

; x0 = hauteur -> x0, x1 = coin : en bas a droite, au-dessus du panel
card_pos:
	mov	x2,x0
	adr	x3,scr
	ldp	x0,x1,[x3]			; ecran
	ldr	x3,[x3,16]			; reserve en bas
	sub	x0,x0,CW+MARGIN
	sub	x1,x1,x3
	sub	x1,x1,LIFT
	sub	x1,x1,x2
	ret

open_win:
	prologue
	str	x19,[sp,-16]!
	bl	card_h
	mov	x19,x0
	bl	card_pos
	mov	x2,CW
	mov	x3,x19
	adr	x4,s_title
	bl	tr_x4
	mov	x5,WF_FRAMELESS or WF_TOP or WF_NOLIST or WF_ALPHA
	bl	ui_window_ex
	ldr	x19,[sp],16
	stv	x0,g_win
	epilogue

; l'anneau a change : relu, la carte suit le nombre de lignes
refresh:
	prologue
	str	x19,[sp,-16]!
	ldv	x19,g_n
	bl	read_ring
	cbz	x0,.out
	ldv	x0,g_n
	cmp	x0,x19
	b.eq	.draw
	bl	card_h
	mov	x19,x0
	mov	x1,x0
	mov	x0,CW
	bl	ui_resize
	mov	x0,x19
	bl	card_pos
	adr	x2,wmsg
	mov	x3,WS_OP_WIN_PLACE
	str	x3,[x2,WM_OP]
	ldv	x3,g_win
	str	x3,[x2,WM_A]
	stp	x0,x1,[x2,WM_B]
	str	wzr,[x2,WM_LEN]
	mov	x0,x2
	sys	SYS_WS_CALL
.draw:	bl	draw
.out:	ldr	x19,[sp],16
	epilogue

; x0, x1 = point -> x0 = ligne (-1 : aucune)
row_at:
	cmp	x1,HEAD+EDGE
	b.lt	.no
	sub	x1,x1,HEAD+EDGE
	mov	x2,ROW
	udiv	x1,x1,x2
	ldv	x2,g_n
	cmp	x1,x2
	b.hs	.no
	mov	x0,x1
	ret
.no:	mov	x0,-1
	ret

on_move:
	prologue
	bl	row_at
	ldv	x1,g_hot
	cmp	x0,x1
	b.eq	.out
	stv	x0,g_hot
	bl	draw
.out:	epilogue

; x0 = ligne -> x0 = sa memoire
item_at:
	movl	x1,IT_SIZE
	mul	x0,x0,x1
	adr	x1,g_items
	add	x0,x0,x1
	ret

on_press:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	sub	x2,x19,CW-EDGE-34		; la corbeille
	cmp	x2,26
	b.hs	.row
	sub	x2,x20,EDGE+8
	cmp	x2,26
	b.hs	.row
	ldv	x0,g_n
	cbz	x0,.out
	mov	x0,CLIP_CLEAR
	mov	x1,0
	bl	clipd
	b	.out
.row:	mov	x0,x19
	mov	x1,x20
	bl	row_at
	tbnz	x0,63,.out
	bl	item_at
	ldr	x1,[x0,IT_ID]
	mov	x0,CLIP_CURSOR
	cmp	x19,CW-EDGE-XW
	b.lt	.send
	mov	x0,CLIP_DELETE
.send:	bl	clipd
.out:	ldp	x19,x20,[sp],16
	epilogue

; w0 = touche : fleches, Suppr
on_key:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	mov	x20,-1				; ligne du curseur
	mov	x0,0
.f:	ldv	x1,g_n
	cmp	x0,x1
	b.hs	.fd
	str	x0,[sp,-16]!
	bl	item_at
	ldr	x1,[x0,IT_CUR]
	ldr	x0,[sp],16
	cbz	x1,.fn
	mov	x20,x0
.fn:	add	x0,x0,1
	b	.f
.fd:	ldv	x1,g_n
	cbz	x1,.out
	cmp	w19,KEY_DELETE
	b.eq	.del
	cmp	w19,KEY_UP
	b.eq	.up
	cmp	w19,KEY_DOWN
	b.ne	.out
	add	x0,x20,1
	sub	x1,x1,1
	cmp	x0,x1
	csel	x0,x1,x0,gt
	b	.set
.up:	subs	x0,x20,1
	csel	x0,xzr,x0,lt
.set:	bl	item_at
	ldr	x1,[x0,IT_ID]
	mov	x0,CLIP_CURSOR
	bl	clipd
	b	.out
.del:	tbnz	x20,63,.out
	mov	x0,x20
	bl	item_at
	ldr	x1,[x0,IT_ID]
	mov	x0,CLIP_DELETE
	bl	clipd
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = genre -> w0 = couleur de sa pastille
kind_colour:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x1,s_image
	bl	str_eq
	movl	w1,0x004EA05C
	cbnz	x0,.got
	mov	x0,x19
	adr	x1,s_url
	bl	str_eq
	movl	w1,0x002878B4
	cbnz	x0,.got
	mov	x0,x19
	adr	x1,s_rtf
	bl	str_eq
	movl	w1,0x003C60B0
	cbnz	x0,.got
	movl	w1,0x00E2B45C
	ldrb	w2,[x19]
	cmp	w2,'f'
	b.eq	.got
	movl	w1,0x0060687A
.got:	mov	w0,w1
	ldr	x19,[sp],16
	epilogue

; x0 = genre, x1 = x, x2 = y : la pastille de 18 px et son dessin en blanc
kind_icon:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	kind_colour
	mov	w5,w0
	mov	w6,w0
	mov	x0,x20
	mov	x1,x21
	mov	x2,18
	mov	x3,18
	mov	x4,4
	bl	ui_rbox
	movl	w22,0x00FFFFFF
	ldrb	w0,[x19]
	cmp	w0,'i'				; image : montagne
	b.eq	.img
	cmp	w0,'f'				; fichiers : dossier
	b.eq	.dir
	cmp	w0,'u'				; lien : un anneau
	b.eq	.url
	mov	x19,0				; texte : trois lignes
.ln:	add	x0,x20,4
	mov	x1,4
	mul	x1,x19,x1
	add	x1,x1,x21
	add	x1,x1,5
	mov	x2,10
	cmp	x19,2
	b.ne	.lw
	mov	x2,6
.lw:	mov	x3,2
	mov	w4,w22
	bl	ui_fill
	add	x19,x19,1
	cmp	x19,3
	b.lo	.ln
	b	.out
.img:	mov	x19,0				; (triangle, marche par marche)
.tr:	mov	x0,5
	sub	x0,x0,x19
	add	x0,x0,x20
	add	x0,x0,3
	add	x1,x19,7
	add	x1,x1,x21
	lsl	x2,x19,1
	add	x2,x2,2
	mov	x3,1
	mov	w4,w22
	bl	ui_fill
	add	x19,x19,1
	cmp	x19,6
	b.lo	.tr
	add	x0,x20,12
	add	x1,x21,4
	mov	x2,3
	mov	x3,3
	mov	w4,w22
	bl	ui_fill
	b	.out
.dir:	add	x0,x20,3
	add	x1,x21,6
	mov	x2,12
	mov	x3,8
	mov	w4,w22
	bl	ui_fill
	add	x0,x20,3
	add	x1,x21,4
	mov	x2,6
	mov	x3,2
	mov	w4,w22
	bl	ui_fill
	b	.out
.url:	add	x0,x20,4
	add	x1,x21,4
	mov	x2,10
	mov	x3,10
	mov	w4,w22
	bl	ui_frame
	add	x0,x20,4
	add	x1,x21,8
	mov	x2,10
	mov	x3,2
	mov	w4,w22
	bl	ui_fill
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, w2 = encre : la petite croix de 9 px
draw_x:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	mov	x22,0
.c:	add	x0,x19,x22
	add	x1,x20,x22
	mov	x2,2
	mov	x3,1
	mov	w4,w21
	bl	ui_fill
	mov	x0,7
	sub	x0,x0,x22
	add	x0,x0,x19
	add	x1,x20,x22
	mov	x2,2
	mov	x3,1
	mov	w4,w21
	bl	ui_fill
	add	x22,x22,1
	cmp	x22,8
	b.lo	.c
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, w2 = encre : la corbeille
draw_bin:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	w21,w2
	add	x0,x19,5			; poignee
	add	x1,x20,0
	mov	x2,6
	mov	x3,2
	mov	w4,w21
	bl	ui_fill
	mov	x0,x19				; couvercle
	add	x1,x20,2
	mov	x2,16
	mov	x3,2
	mov	w4,w21
	bl	ui_fill
	add	x0,x19,2			; cuve
	add	x1,x20,5
	mov	x2,12
	mov	x3,12
	mov	w4,w21
	bl	ui_frame
	add	x0,x19,5
	add	x1,x20,7
	mov	x2,2
	mov	x3,8
	mov	w4,w21
	bl	ui_fill
	add	x0,x19,9
	add	x1,x20,7
	mov	x2,2
	mov	x3,8
	mov	w4,w21
	bl	ui_fill
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur 0..99, x1 = tampon -> x0 = fin (deux chiffres)
two_digits:
	mov	x2,10
	udiv	x3,x0,x2
	msub	x4,x3,x2,x0
	add	w3,w3,'0'
	add	w4,w4,'0'
	strb	w3,[x1]
	strb	w4,[x1,1]
	add	x0,x1,2
	ret

; x0 = ligne, x1 = y : une ligne de la carte
draw_row:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	item_at
	mov	x21,x0
	ldv	x23,g_tok
	ldr	x0,[x21,IT_CUR]
	cbz	x0,.hot
	ldr	w5,[x23,TJ_ACCENT_CLAIR]	; le curseur
	ldr	w0,[x23,TJ_SOMBRE]
	cbz	w0,.cf
	ldr	w0,[x23,TJ_MENU]
	ldr	w1,[x23,TJ_ACCENT]
	mov	x2,90
	bl	ui_mix
	mov	w5,w0
.cf:	mov	w6,w5
	mov	x0,EDGE+6
	mov	x1,x20
	mov	x2,CW-2*EDGE-12
	mov	x3,ROW-2
	mov	x4,6
	bl	ui_rbox
	b	.body
.hot:	ldv	x0,g_hot
	cmp	x0,x19
	b.ne	.body
	ldr	w0,[x23,TJ_MENU]
	ldr	w1,[x23,TJ_TEXTE1]
	mov	x2,24
	bl	ui_mix
	mov	w5,w0
	mov	w6,w0
	mov	x0,EDGE+6
	mov	x1,x20
	mov	x2,CW-2*EDGE-12
	mov	x3,ROW-2
	mov	x4,6
	bl	ui_rbox
.body:	add	x22,x21,IT_DATA
	mov	x0,x22
	mov	x1,EDGE+10
	add	x2,x20,5
	bl	kind_icon
	adr	x24,linebuf
	strb	wzr,[x24]
	mov	x0,x22
	adr	x1,s_image
	bl	str_eq
	cbz	x0,.tx
	mov	x0,x24				; « Image  320 x 200 »
	adr	x1,s_limage
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	ldr	w0,[x22,CI_W]
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x1,s_x
	bl	str_cat
	mov	x1,x0
	ldr	w0,[x22,CI_H]
	bl	fmt_dec
	strb	wzr,[x0]
	b	.fit
.tx:	mov	x0,x24
	add	x1,x22,CI_PREVIEW
	bl	str_cat
	mov	x19,x0
	mov	x0,x22
	adr	x1,s_filescut
	bl	str_eq
	cbz	x0,.emp
	mov	x0,x19
	adr	x1,s_lcut
	bl	tr_x1
	bl	str_cat
.emp:	ldrb	w0,[x24]
	cbnz	w0,.fit
	mov	x0,x24
	adr	x1,s_lempty
	bl	tr_x1
	bl	str_cat
.fit:	adr	x0,timebuf			; l'heure
	mov	x1,x0
	ldr	x2,[x21,IT_HM]
	lsr	x0,x2,8
	bl	two_digits
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,x0
	ldr	x0,[x21,IT_HM]
	and	x0,x0,0xFF
	bl	two_digits
	strb	wzr,[x0]
	adr	x0,timebuf
	mov	x1,11
	adr	x2,ui_font
	bl	ui_text_width
	mov	x19,x0				; largeur de l'heure
	mov	x0,x24				; le texte
	mov	x1,TXT_X
	add	x2,x20,19
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,CW-EDGE-XW-TXT_X-8
	sub	x6,x6,x19
	bl	ui_text_fit
	adr	x0,timebuf
	mov	x1,CW-EDGE-XW
	sub	x1,x1,x19
	add	x2,x20,19
	ldr	w3,[x23,TJ_TEXTE3]
	mov	x4,11
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x0,CW-EDGE-XW+10		; la croix
	add	x1,x20,10
	ldr	w2,[x23,TJ_TEXTE2]
	bl	draw_x
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x21,g_tok
	mov	x0,0				; (apres un changement de taille)
	mov	x1,0
	adr	x2,ui_w
	ldp	x2,x3,[x2]
	movl	w4,0xFF000000
	bl	ui_fill
	adr	x0,ui_w
	ldp	x0,x1,[x0]
	bl	ui_ombre
	ldr	w4,[x21,TJ_MENU]
	mov	x0,EDGE
	mov	x1,EDGE
	adr	x2,ui_w
	ldp	x2,x3,[x2]
	sub	x2,x2,EDGE*2
	sub	x3,x3,EDGE*2
	bl	ui_fill
	mov	x0,EDGE+12			; la pince, le titre, le compte
	mov	x1,EDGE+11
	mov	x2,14
	mov	x3,18
	ldr	w4,[x21,TJ_TEXTE1]
	bl	ui_frame
	mov	x0,EDGE+15
	mov	x1,EDGE+9
	mov	x2,8
	mov	x3,4
	ldr	w4,[x21,TJ_TEXTE1]
	bl	ui_fill
	adr	x0,s_title
	bl	tr_x0
	mov	x1,EDGE+36
	mov	x2,EDGE+26
	ldr	w3,[x21,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x19,x0				; fin du titre
	adr	x1,timebuf
	ldv	x0,g_n
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x1,s_of
	bl	str_cat
	adr	x0,timebuf
	add	x1,x19,8
	mov	x2,EDGE+26
	ldr	w3,[x21,TJ_TEXTE3]
	mov	x4,11
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x0,CW-EDGE-29			; la corbeille
	mov	x1,EDGE+12
	ldr	w2,[x21,TJ_TEXTE3]
	ldv	x3,g_n
	cbz	x3,.bin
	ldr	w2,[x21,TJ_TEXTE2]
.bin:	bl	draw_bin
	mov	x0,EDGE+10
	mov	x1,EDGE+HEAD-5
	mov	x2,CW-2*EDGE-20
	mov	x3,1
	ldr	w4,[x21,TJ_LIGNE]
	bl	ui_fill
	ldv	x19,g_n
	cbnz	x19,.rows
	adr	x0,s_nothing			; rien de copie
	bl	tr_x0
	mov	x1,13
	adr	x2,ui_font
	bl	ui_text_width
	mov	x1,CW
	sub	x1,x1,x0
	lsr	x1,x1,1
	adr	x0,s_nothing
	bl	tr_x0
	mov	x2,EDGE+HEAD+19
	ldr	w3,[x21,TJ_TEXTE3]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	b	.show
.rows:	mov	x19,0
.r:	ldv	x0,g_n
	cmp	x19,x0
	b.hs	.show
	mov	x1,ROW
	mul	x1,x19,x1
	add	x1,x1,EDGE+HEAD
	mov	x0,x19
	bl	draw_row
	add	x19,x19,1
	b	.r
.show:	bl	ui_update_all
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_me		db 'clipwidget',0
s_svc		db 'clipboard',0
s_image		db 'image',0
s_url		db 'url',0
s_rtf		db 'rtf',0
s_filescut	db 'files-cut',0
s_title		db 'Presse-papiers',0
s_of		db ' / 10',0
s_x		db ' x ',0
s_limage	db 'Image  ',0
s_lcut		db '  (coup',0xC3,0xA9,')',0
s_lempty	db '(vide)',0
s_nothing	db 'Rien de copi',0xC3,0xA9,0
		align 8

include '../../inc/tr.inc'

segment readable writeable
kit_data
		align 8
g_me		dd -1
g_tok		dd 0
g_win		dd 0
g_serial	dd 0
g_n		dd 0
g_hot		dd -1
scr		rb 32
timebuf		rb 32
msg		rb WS_MSG
wmsg		rb WS_MSG
linebuf		rb CI_PREVIEW_MAX + 64
		align 8
g_items		rb CLIP_RING * IT_SIZE
ui_data
