; ============================================================================
;  switcher - le commutateur d'apps du mode poche (SD0:/sys/switcher, lance par
;  SD0:/etc/session.poche ; docs/MODE-POCHE.md, DESIGN-ETUDES-ONYX.md 4).
;
;  Il dort sur le bus (korin.shell.switch) et s'ouvre a sa publication :
;  Alt+Tab, Super+Tab (Elegant), le compteur de la barre de menus. Valeurs :
;	1, alt		ouvrir (deja ouvert : l'app suivante)
;	alt-prev	ouvrir (deja ouvert : l'app precedente)
;	commit		Alt relache apres Alt+Tab : passer a l'app choisie
;
;  Une fenetre plein ecran au-dessus de tout (sans cadre, WF_TOP, WF_GRAB,
;  WF_ALPHA) : un voile TEXTE1 a 50 %, puis une carte par app (fenetre a cadre
;  normale, hors dialogues et accueil), la plus recente d'abord ; la vignette
;  (240 x 150 au plus, proportions gardees) est reduite une fois a
;  l'ouverture par Elegant (WS_OP_WIN_THUMB, depuis le contenu des fenetres :
;  aucune app ne redessine). La choisie au depart : l'app d'avant (la
;  premiere si l'accueil est devant) ; anneau d'accent de 2 px.
;  Clavier : Tab, fleches -> suivante / precedente ; Entree passe ; Suppr ferme
;  l'app choisie ; Echap annule. Doigt ou souris : toucher une carte passe a
;  elle, toucher son × la ferme, glisser horizontalement fait defiler (plus de
;  trois apps), toucher ailleurs annule.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/buskit/buskit.inc'

NMAX		= 16			; apps montrees
TW		= 240			; vignette
TH		= 150
LBL_H		= 40			; sous la vignette : icone, nom, ×
CARD_H		= TH + LBL_H
GAP		= 24
STEP		= TW + GAP
VIS		= 3			; cartes visibles a la fois
CARD_R		= 10
VEIL_A		= 0x80			; transparence du voile (50 %)
DRAG_MIN	= 40			; px : au-dela, un glisser
; une app (IT_SIZE octets)
IT_ID		= 0
IT_PID		= 8
IT_TW		= 16			; taille de sa vignette
IT_TH		= 24
IT_STATE	= 32			; WES_*
IT_TITLE	= 40			; 32 octets
IT_NAME		= 72			; nom du process (l'icone), 32 octets
IT_SIZE		= 104
THUMB_BYTES	= TW * TH * 4
WEF_SKIP	= WF_FRAMELESS or WF_BACK or WF_TOP or WF_NOLIST or WF_MODAL or WF_HOME

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so

start:
	bl	kits_open
	adr	x0,s_svc			; un seul commutateur
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x0,ui_win
	bl	ui_init
	movl	x0,NMAX*THUMB_BYTES		; les vignettes
	add	x0,x0,15
	and	x0,x0,not 15
	sys	SYS_SBRK
	tbnz	x0,63,quit
	adr	x1,thumbs
	str	x0,[x1]
	adr	x0,s_topic
	mov	x1,0
	bl	bus_subscribe
	cmp	x0,0
	b.le	quit
	adr	x1,bsub
	str	x0,[x1]
	; --- ferme : attendre le bus
.idle:	adr	x0,bev
	mov	x1,60000
	bl	bus_wait
	cmp	x0,WEV_BUS
	b.ne	.idle
	adr	x0,bev
	bl	is_ours
	cbz	x0,.idle
	cmp	x1,2				; (commit : rien d'ouvert)
	b.eq	.idle
	bl	open
	cbz	x0,.idle
	; --- ouvert
.loop:	mov	x0,1000
	bl	ui_event
	cbz	x0,.loop
	cmp	x0,WEV_BUS
	b.eq	.bus
	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cmp	x0,WEV_WINLIST
	b.eq	.list
	cmp	x0,WEV_THEME
	b.eq	.redraw
	b	.loop
.bus:	adr	x0,ui_ev
	bl	is_ours
	cbz	x0,.loop
	cmp	x1,2
	b.eq	.go
	cmp	x1,1
	b.eq	.prev
	b	.next
.key:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cbz	w0,.loop			; relache
	ldr	w0,[x9,EV_CODE]
	cmp	w0,KEY_ESC
	b.eq	.cancel
	cmp	w0,KEY_ENTER
	b.eq	.go
	cmp	w0,KEY_DELETE
	b.eq	.kill
	cmp	w0,KEY_RIGHT
	b.eq	.next
	cmp	w0,KEY_DOWN
	b.eq	.next
	cmp	w0,KEY_LEFT
	b.eq	.prev
	cmp	w0,KEY_UP
	b.eq	.prev
	cmp	w0,KEY_TAB
	b.ne	.loop
	ldr	w0,[x9,EV_MODS]
	mov	w1,0x22				; (Maj : celle d'avant)
	tst	w0,w1
	b.ne	.prev
.next:	mov	x0,1
	bl	sel_move
	b	.redraw
.prev:	mov	x0,-1
	bl	sel_move
	b	.redraw
.mouse:	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	ldrsw	x1,[x9,EV_X]
	ldrsw	x2,[x9,EV_Y]
	cmp	w0,1
	b.ne	.mrel
	adr	x0,press_x			; appui : retenu
	stp	x1,x2,[x0]
	mov	x1,1
	adr	x0,pressed
	str	x1,[x0]
	b	.loop
.mrel:	cmp	w0,2
	b.ne	.loop
	adr	x0,pressed
	ldr	x3,[x0]
	cbz	x3,.loop
	str	xzr,[x0]
	adr	x0,press_x
	ldr	x3,[x0]
	sub	x4,x1,x3			; glisser horizontal ?
	cmp	x4,DRAG_MIN
	b.gt	.dright
	cmn	x4,DRAG_MIN
	b.lt	.dleft
	mov	x0,x1				; un toucher
	mov	x1,x2
	bl	hit
	tbnz	x0,63,.cancel
	adr	x2,k_sel
	str	x0,[x2]
	cbnz	x1,.kill
	b	.go
.dright:					; vers la droite : les precedentes
	mov	x0,STEP/2			; (une carte par demi-carte glissee)
	sdiv	x0,x4,x0
	cmp	x0,1
	csinc	x0,x0,xzr,ge
	neg	x0,x0
	bl	scroll_by
	b	.redraw
.dleft:	neg	x4,x4
	mov	x0,STEP/2
	sdiv	x0,x4,x0
	cmp	x0,1
	csinc	x0,x0,xzr,ge
	bl	scroll_by
	b	.redraw
.list:	bl	collect				; une app fermee, une ouverte
	adr	x0,k_n
	ldr	x0,[x0]
	cbz	x0,.cancel
	bl	sel_fix
.redraw:
	bl	draw
	b	.loop
.kill:	bl	close_sel			; l'app choisie fermee (la liste suivra :
	b	.loop				; WEV_WINLIST)
.go:	adr	x0,k_sel				; passer a l'app choisie
	ldr	x0,[x0]
	adr	x1,items
	mov	x2,IT_SIZE
	madd	x1,x0,x2,x1
	ldr	x0,[x1,IT_ID]
	bl	ui_raise
.cancel:
	bl	ui_close
	adr	x0,opened
	str	xzr,[x0]
	b	.idle
quit:	mov	x0,0
	sys	SYS_EXIT

; x0 = evenement du bus -> x0 = 1 si korin.shell.switch, x1 = 0 suivante,
; 1 precedente, 2 commit
is_ours:
	ldr	w1,[x0,BEV_SUB]
	adr	x2,bsub
	ldr	x2,[x2]
	cmp	x1,x2
	b.ne	.no
	add	x2,x0,BEV_DATA
	ldrb	w3,[x2]
	mov	x1,2
	cmp	w3,'c'				; commit
	b.eq	.yes
	mov	x1,1
	ldrb	w3,[x2,3]			; alt-prev
	cmp	w3,'-'
	b.eq	.yes
	mov	x1,0
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; ouvre le commutateur -> x0 = 1 si ouvert (0 : aucune app)
open:
	prologue
	str	x19,[sp,-16]!
	bl	collect
	adr	x0,k_n
	ldr	x19,[x0]
	cbz	x19,.none
	mov	x0,0				; la choisie : l'app d'avant ; la premiere
	cmp	x19,1				; si l'accueil est devant
	b.eq	.s
	adr	x1,items
	ldr	x2,[x1,IT_STATE]
	tst	x2,WES_MINI
	b.ne	.s
	mov	x0,1
.s:	adr	x1,k_sel
	str	x0,[x1]
	adr	x1,scroll
	str	xzr,[x1]
	bl	sel_fix
	adr	x0,scr
	bl	ui_screen
	mov	x0,0
	mov	x1,0
	adr	x2,scr
	ldp	x2,x3,[x2]
	adr	x4,s_title
	mov	x5,WF_FRAMELESS or WF_TOP or WF_GRAB or WF_ALPHA or WF_NOLIST or WF_LISTEN
	bl	ui_window_ex
	tbnz	x0,63,.none
	adr	x0,opened
	mov	x1,1
	str	x1,[x0]
	adr	x0,pressed
	str	xzr,[x0]
	bl	draw
	mov	x0,1
	b	.out
.none:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; la liste des apps (WS_OP_WIN_LIST : du fond vers l'avant ; gardee de
; l'avant vers le fond), leurs vignettes -> n
collect:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,0				; premiere entree de la page
	mov	x20,0				; apps trouvees (de l'arriere vers l'avant)
.page:	adr	x9,wmsg
	mov	x0,WS_OP_WIN_LIST
	str	x0,[x9,WM_OP]
	str	x19,[x9,WM_A]
	str	wzr,[x9,WM_LEN]
	mov	x0,x9
	sys	SYS_WS_CALL
	cmp	x0,x19
	b.le	.rev
	sub	x21,x0,x19
	cmp	x21,WE_PER_MSG
	mov	x1,WE_PER_MSG
	csel	x21,x1,x21,gt
	add	x19,x19,x21
	adr	x22,wmsg+WM_DATA
.e:	cbz	x21,.page
	ldr	w0,[x22,WE_FLAGS]
	movl	w1,WEF_SKIP
	tst	w0,w1
	b.ne	.en
	cmp	x20,NMAX
	b.hs	.en
	adr	x23,items_tmp
	mov	x0,IT_SIZE
	madd	x23,x20,x0,x23
	ldr	w0,[x22,WE_ID]
	str	x0,[x23,IT_ID]
	ldr	w0,[x22,WE_PID]
	str	x0,[x23,IT_PID]
	ldr	w0,[x22,WE_STATE]
	str	x0,[x23,IT_STATE]
	ldrh	w0,[x22,WE_W]			; vignette : 240 x 150 au plus, en
	ldrh	w1,[x22,WE_H]			; proportion
	mov	x2,TW
	mov	x3,TH
	mul	x4,x0,x3			; l * TH > h * TW : la largeur limite
	mul	x5,x1,x2
	cmp	x4,x5
	b.le	.hlim
	mul	x3,x1,x2
	udiv	x3,x3,x0
	b	.tsz
.hlim:	mul	x2,x0,x3
	cbz	x1,.tsz
	udiv	x2,x2,x1
.tsz:	cmp	x2,1
	csinc	x2,x2,xzr,ge
	cmp	x3,1
	csinc	x3,x3,xzr,ge
	stp	x2,x3,[x23,IT_TW]
	add	x0,x23,IT_TITLE
	add	x1,x22,WE_TITLE
	mov	x2,32
	bl	copy_n
	ldr	x0,[x23,IT_PID]			; le nom du process : l'icone
	adr	x1,pinfo
	sys	SYS_PROC_INFO
	add	x0,x23,IT_NAME
	strb	wzr,[x0]
	adr	x1,pinfo+24
	mov	x2,32
	bl	copy_n
	add	x20,x20,1
.en:	add	x22,x22,WE_SIZE
	sub	x21,x21,1
	b	.e
.rev:	adr	x0,k_n				; de l'avant vers l'arriere
	str	x20,[x0]
	mov	x21,0
.r:	cmp	x21,x20
	b.hs	.thumbs
	sub	x0,x20,1
	sub	x0,x0,x21
	mov	x2,IT_SIZE
	adr	x1,items_tmp
	madd	x1,x0,x2,x1
	adr	x0,items
	madd	x0,x21,x2,x0
	mov	x3,IT_SIZE/8
.cp:	ldr	x4,[x1],8
	str	x4,[x0],8
	subs	x3,x3,1
	b.ne	.cp
	add	x21,x21,1
	b	.r
.thumbs:					; les vignettes : une fois, ici
	mov	x21,0
.t:	cmp	x21,x20
	b.hs	.out
	adr	x23,items
	mov	x0,IT_SIZE
	madd	x23,x21,x0,x23
	adr	x9,wmsg
	mov	x0,WS_OP_WIN_THUMB
	str	x0,[x9,WM_OP]
	ldr	x0,[x23,IT_ID]
	str	x0,[x9,WM_A]
	ldp	x0,x1,[x23,IT_TW]
	orr	x0,x0,x1,lsl 32
	str	x0,[x9,WM_B]
	str	xzr,[x9,WM_C]
	str	wzr,[x9,WM_LEN]
	mov	x0,x9
	sys	SYS_WS_CALL
	cbnz	x0,.tn
	adr	x9,wmsg				; le tampon d'Elegant chez nous
	ldr	x0,[x9,WM_D]
	adr	x1,snap_num
	ldr	x2,[x1]
	cmp	x0,x2
	b.eq	.mapped
	str	x0,[x1]
	sys	SYS_BUF_MAP
	tbnz	x0,63,.tn
	adr	x1,snap_adr
	str	x0,[x1]
.mapped:
	adr	x0,snap_adr
	ldr	x1,[x0]
	adr	x0,thumbs
	ldr	x0,[x0]
	movl	x2,THUMB_BYTES
	madd	x0,x21,x2,x0
	ldp	x2,x3,[x23,IT_TW]
	mul	x2,x2,x3
.c4:	ldr	w3,[x1],4
	str	w3,[x0],4
	subs	x2,x2,1
	b.ne	.c4
	b	.tnext
.tn:	stp	xzr,xzr,[x23,IT_TW]		; (pas de vignette)
.tnext:	add	x21,x21,1
	b	.t
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source, x2 = taille (0 compris) (feuille)
copy_n:
	sub	x2,x2,1
.c:	cbz	x2,.f
	ldrb	w3,[x1],1
	cbz	w3,.f
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c
.f:	strb	wzr,[x0]
	ret

; x0 = +1 / -1 : la choisie avance (en boucle), visible
sel_move:
	prologue
	adr	x1,k_n
	ldr	x2,[x1]
	cbz	x2,.out
	adr	x1,k_sel
	ldr	x3,[x1]
	add	x3,x3,x0
	cmp	x3,0
	b.ge	.hi
	sub	x3,x2,1
.hi:	cmp	x3,x2
	csel	x3,xzr,x3,hs
	str	x3,[x1]
	bl	sel_fix
.out:	epilogue

; la choisie dans la liste, et visible (scroll)
sel_fix:
	adr	x0,k_n
	ldr	x2,[x0]
	adr	x0,k_sel
	ldr	x3,[x0]
	cmp	x3,x2
	b.lo	.vis
	sub	x3,x2,1
	cmp	x3,0
	csel	x3,xzr,x3,lt
	str	x3,[x0]
.vis:	adr	x0,scroll
	ldr	x4,[x0]
	cmp	x3,x4
	csel	x4,x3,x4,lo
	add	x5,x4,VIS-1
	cmp	x3,x5
	b.ls	.st
	sub	x4,x3,VIS-1
.st:	str	x4,[x0]
	ret

; x0 = cartes : defilement (borne)
scroll_by:
	adr	x1,scroll
	ldr	x2,[x1]
	add	x2,x2,x0
	adr	x3,k_n
	ldr	x3,[x3]
	subs	x3,x3,VIS
	csel	x3,xzr,x3,lt
	cmp	x2,x3
	csel	x2,x3,x2,gt
	cmp	x2,0
	csel	x2,xzr,x2,lt
	str	x2,[x1]
	adr	x1,k_sel				; la choisie suit si elle sort
	ldr	x3,[x1]
	cmp	x3,x2
	csel	x3,x2,x3,lo
	add	x4,x2,VIS-1
	cmp	x3,x4
	csel	x3,x4,x3,hi
	str	x3,[x1]
	ret

; -> x0 = gauche de la premiere carte visible, x1 = haut des cartes,
; x2 = cartes visibles (feuille)
layout:
	adr	x3,k_n
	ldr	x2,[x3]
	adr	x3,scroll
	ldr	x3,[x3]
	sub	x2,x2,x3
	cmp	x2,VIS
	mov	x4,VIS
	csel	x2,x4,x2,gt
	mov	x4,STEP				; largeur : x2 * STEP - GAP
	mul	x4,x2,x4
	sub	x4,x4,GAP
	adr	x5,ui_w
	ldr	x0,[x5]
	sub	x0,x0,x4
	asr	x0,x0,1
	adr	x5,ui_h
	ldr	x1,[x5]
	sub	x1,x1,CARD_H
	asr	x1,x1,1
	ret

; x0, x1 = point -> x0 = app touchee (-1 : aucune), x1 = 1 sur son ×
hit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	layout
	mov	x21,x1				; haut des cartes
	mov	x5,0
.c:	cmp	x5,x2
	b.hs	.none
	mov	x6,STEP
	madd	x6,x5,x6,x0			; gauche de la carte
	add	x7,x6,TW
	cmp	x19,x6
	b.lt	.n
	cmp	x19,x7
	b.ge	.n
	cmp	x20,x21
	b.lt	.n
	add	x8,x21,CARD_H
	cmp	x20,x8
	b.ge	.n
	adr	x3,scroll
	ldr	x3,[x3]
	add	x0,x5,x3
	mov	x1,0				; le × : bas droit (36 x LBL_H)
	sub	x7,x7,36
	cmp	x19,x7
	b.lt	.out
	add	x8,x21,TH
	cmp	x20,x8
	cset	x1,ge
	b	.out
.n:	add	x5,x5,1
	b	.c
.none:	mov	x0,-1
	mov	x1,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ferme l'app choisie (WEV_CLOSE a son programme, comme son ×)
close_sel:
	prologue
	adr	x0,k_sel
	ldr	x0,[x0]
	adr	x1,items
	mov	x2,IT_SIZE
	madd	x1,x0,x2,x1
	adr	x9,wmsg
	mov	x0,WS_OP_WIN_CLOSE
	str	x0,[x9,WM_OP]
	ldr	x0,[x1,IT_ID]
	str	x0,[x9,WM_A]
	str	wzr,[x9,WM_LEN]
	mov	x0,x9
	sys	SYS_WS_CALL
	epilogue

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	bl	ui_jetons
	mov	x26,x0
	movl	w4,0x0014171C			; voile : TEXTE1 de Givre (encre sombre,
						; les deux themes), a 50 %
	orr	w4,w4,VEIL_A shl 24
	mov	x0,0
	mov	x1,0
	adr	x2,ui_w
	ldr	x2,[x2]
	adr	x3,ui_h
	ldr	x3,[x3]
	bl	ui_fill
	bl	layout
	mov	x21,x0				; x
	mov	x22,x1				; y
	mov	x23,x2				; cartes visibles
	mov	x24,0
.card:	cmp	x24,x23
	b.hs	.arrows
	adr	x0,scroll
	ldr	x0,[x0]
	add	x19,x24,x0			; l'app
	adr	x20,items
	mov	x0,IT_SIZE
	madd	x20,x19,x0,x20
	mov	x0,STEP
	madd	x25,x24,x0,x21			; gauche de la carte
	adr	x0,k_sel				; choisie : anneau d'accent de 2 px
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.body
	sub	x0,x25,2
	sub	x1,x22,2
	mov	x2,TW+4
	mov	x3,CARD_H+4
	mov	x4,CARD_R+2
	ldr	w5,[x26,TJ_ACCENT]
	mov	w6,w5
	bl	ui_rbox
.body:	mov	x0,x25				; la carte
	mov	x1,x22
	mov	x2,TW
	mov	x3,CARD_H
	mov	x4,CARD_R
	ldr	w5,[x26,TJ_SURFACE]
	mov	w6,w5
	bl	ui_rbox
	mov	x0,x25				; fond de la vignette
	mov	x1,x22
	mov	x2,x20
	mov	x3,x19
	bl	draw_thumb
	add	x0,x20,IT_NAME			; icone 20
	add	x1,x25,10
	add	x2,x22,TH+10
	mov	x3,20
	bl	ui_icone
	add	x0,x20,IT_TITLE			; nom 14
	add	x1,x25,38
	add	x2,x22,TH+25
	ldr	w3,[x26,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,TW-38-36
	bl	ui_text
	adr	x0,s_croix			; ×
	add	x1,x25,TW-26
	add	x2,x22,TH+27
	ldr	w3,[x26,TJ_TEXTE2]
	mov	x4,18
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	add	x24,x24,1
	b	.card
.arrows:					; d'autres a gauche, a droite : un point
	adr	x0,scroll
	ldr	x0,[x0]
	cbz	x0,.ra
	sub	x0,x21,18
	add	x1,x22,CARD_H/2-4
	mov	x2,8
	mov	x3,8
	mov	x4,4
	ldr	w5,[x26,TJ_SURFACE]
	mov	w6,w5
	bl	ui_rbox
.ra:	adr	x0,scroll
	ldr	x0,[x0]
	add	x0,x0,x23
	adr	x1,k_n
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.upd
	mov	x0,STEP
	mul	x0,x0,x23
	add	x0,x0,x21
	sub	x0,x0,GAP
	add	x0,x0,10
	add	x1,x22,CARD_H/2-4
	mov	x2,8
	mov	x3,8
	mov	x4,4
	ldr	w5,[x26,TJ_SURFACE]
	mov	w6,w5
	bl	ui_rbox
.upd:	bl	ui_update_all
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = coin de la carte, x2 = app, x3 = son rang : sa vignette centree dans
; TW x TH (coins du haut arrondis), sur le fond CREUX
draw_thumb:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	jeton	w4,TJ_CREUX			; le fond de la zone
	mov	x0,x19
	add	x1,x20,CARD_R
	mov	x2,TW
	mov	x3,TH-CARD_R
	bl	ui_fill
	ldp	x23,x24,[x21,IT_TW]		; la vignette
	cbz	x23,.out
	adr	x0,thumbs
	ldr	x9,[x0]
	movl	x0,THUMB_BYTES
	madd	x9,x22,x0,x9			; source
	mov	x0,TW				; centree
	sub	x0,x0,x23
	add	x10,x19,x0,lsr 1		; x a l'ecran
	mov	x0,TH
	sub	x0,x0,x24
	add	x11,x20,x0,lsr 1		; y
	adr	x0,ui_surf			; destination : la fenetre
	ldr	x12,[x0]
	adr	x0,ui_w
	ldr	x13,[x0]			; pas (pixels)
	mov	x14,0				; ligne
.row:	cmp	x14,x24
	b.hs	.out
	add	x15,x11,x14			; y ecran
	sub	x0,x15,x20			; coins du haut : retrait
	mov	x16,0
	cmp	x0,CARD_R
	b.hs	.ins
	adr	x1,inset
	ldrb	w16,[x1,x0]
.ins:	mov	x17,0				; colonne
.col:	cmp	x17,x23
	b.hs	.nrow
	add	x0,x10,x17			; x ecran
	sub	x1,x0,x19			; dans la carte : hors des coins ?
	cmp	x1,x16
	b.lo	.ncol
	mov	x2,TW
	sub	x2,x2,x16
	cmp	x1,x2
	b.hs	.ncol
	madd	x1,x14,x23,x17
	ldr	w2,[x9,x1,lsl 2]
	and	w2,w2,0x00FFFFFF		; (opaque)
	madd	x1,x15,x13,x0
	str	w2,[x12,x1,lsl 2]
.ncol:	add	x17,x17,1
	b	.col
.nrow:	add	x14,x14,1
	b	.row
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

inset		db 10,7,5,4,3,2,1,1,0,0		; coins de rayon 10, ligne par ligne
s_svc		db 'switcher',0
s_topic		db 'korin.shell.switch',0
s_title		db 'Commutateur',0
s_croix		db 0xC3,0x97,0			; U+00D7
		align 8

segment readable writeable
kit_data
		align 8
bsub		dd 0
opened		dd 0
k_n		dd 0				; apps
k_sel		dd 0				; la choisie
scroll		dd 0				; premiere carte visible
pressed		dd 0
press_x		dd 0				; press_x, press_y contigus
press_y		dd 0
thumbs		dd 0				; NMAX vignettes (sbrk)
snap_num	dd -1				; tampon d'Elegant (WIN_THUMB), chez nous
snap_adr	dd 0
scr		rb 32
bev		rb 32
pinfo		rb 64
wmsg		rb 512
items		rb NMAX * IT_SIZE
items_tmp	rb NMAX * IT_SIZE
ui_data
