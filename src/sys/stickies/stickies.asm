; ============================================================================
;  stickies - les notes epinglees sur le bureau (apps/stickies d'Onyx), lance par
;  la session (SD0:/etc/session) : des cartes de papier en haut a droite de
;  l'ecran, sous toutes les fenetres. Il ne fait que lire ce que Notes ecrit :
;  SD0:/Notes/notes.ini (quelles notes sont epinglees, leurs couleurs) et les
;  notes .txt, les memes fichiers que sur Onyx (src/apps/notes/notesmodel.h).
;  Service « stickies » (op 2 : relire, op 3 : s'arreter), comme stickies_proto.h ;
;  relu aussi toutes les 30 s. Au plus 6 cartes, les notes epinglees les plus
;  recentes, chacune son titre et 6 lignes.
;  Un clic sur une carte ouvre Notes sur cette note, un clic sur la place vide ou
;  sur « +N autres » ouvre Notes. Sa place est fixe, sous la barre de menus. Il ne
;  demarre pas quand la configuration de Notes dit stickies = 0.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

W_		= 240
HDR		= 34
CX		= 12
CWD		= 216
CPAD		= 10
CBAND		= 5
CGAP		= 12
CR		= 6
TOP		= 44
LINES		= 6
LINE_H		= 17
TITLE_H		= 18
CARDS		= 6
EMPTY_H		= 62
MORE_H		= 22
NOTE_FILE	= 72
PIN_MAX		= 64
NOTE_HEAD	= 2048
HEAD_SIZE	= NOTE_HEAD + 8
NOTE_INK	= 0x002B2925
FBUF		= 32768
ALPHA		= 0x28000000
CLEAR		= 0xFF000000
OP_RELOAD	= 2
OP_QUIT		= 3
HIT_MORE	= 100

; une note epinglee
PN_FILE		= 0
PN_COL		= NOTE_FILE
PN_MOD		= NOTE_FILE + 8
PN_SIZE		= NOTE_FILE + 16
; une carte
CD_PIN		= 0				; sa note
CD_Y		= 8
CD_H		= 16
CD_TITLE	= 24				; texte, dans heads
CD_BODY		= 32
CD_SIZE		= 40

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'
include '../../inc/applet.inc'

start:
	bl	kits_open
	bl	cfg_off
	cbnz	x0,quit				; (cache par Notes)
	adr	x0,s_svc
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	stv	x0,g_svc
	adr	x0,ui_win
	bl	ui_init
	bl	ui_jetons
	stv	x0,g_tok
	adr	x0,scr				; en haut a droite, jusqu'au panel
	bl	ui_screen
	adr	x9,scr
	ldp	x0,x1,[x9]
	sub	x4,x0,W_+APPLET_X
	stv	x4,g_x
	sub	x1,x1,APPLET_Y+APPLET_BAS
	stv	x1,g_h
	bl	reload
	ldv	x0,g_x
	mov	x1,APPLET_Y
	mov	x2,W_
	ldv	x3,g_h
	adr	x4,s_title
	bl	tr_x4
	mov	x5,WF_FRAMELESS or WF_BACK or WF_NOFOCUS or WF_NOLIST or WF_ALPHA
	bl	ui_window_ex
	tbnz	x0,63,quit
	stv	x0,g_win
	bl	draw
.loop:	ldv	x0,g_svc			; Notes : relire, s'arreter
	adr	x1,msg
	mov	x2,0
	sys	SYS_SVC_NEXT
	cmp	x0,0
	b.le	.ev
	mov	x19,x0
	adr	x1,msg
	ldr	x20,[x1]
	str	xzr,[x1]
	sys	SYS_SVC_REPLY
	mov	x0,x19
	cmp	x20,OP_QUIT
	b.eq	quit
	bl	refresh
.ev:	mov	x0,1000
	bl	ui_event
	cbz	x0,.idle
	cmp	x0,WEV_THEME			; autre theme : redessinees
	b.eq	.theme
	cmp	x0,WEV_MOUSE
	b.ne	.loop
	bl	on_mouse
	b	.loop
.theme:	bl	draw
	b	.loop
.idle:	ldv	x0,g_ticks
	add	x0,x0,1
	stv	x0,g_ticks
	cmp	x0,30
	b.lo	.loop
	stv	xzr,g_ticks
	bl	cfg_off
	cbnz	x0,quit
	bl	refresh
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

refresh:
	prologue
	bl	reload
	cbz	x0,.out
	bl	draw
.out:	epilogue

; -> x0 = 1 si la configuration de Notes dit stickies = 0
cfg_off:
	prologue
	adr	x0,s_cfg
	bl	read_file
	mov	x1,x0
	mov	x0,0
	cbz	x1,.out
	adr	x2,fbuf
.l:	mov	x0,x2				; une ligne par ligne
	adr	x1,s_kst
	bl	key_value
	cbnz	x0,.val
.nl:	ldrb	w3,[x2]
	mov	x0,0
	cbz	w3,.out
	add	x2,x2,1
	cmp	w3,10
	b.ne	.nl
	b	.l
.val:	ldrb	w0,[x0]
	cmp	w0,'0'
	cset	x0,eq
.out:	epilogue

; x0 = ligne, x1 = cle -> x0 = sa valeur (0 : pas cette cle) ; garde x2
key_value:
.k:	ldrb	w3,[x1],1
	cbz	w3,.sp
	ldrb	w4,[x0],1
	cmp	w3,w4
	b.eq	.k
.no:	mov	x0,0
	ret
.sp:	ldrb	w3,[x0]
	cmp	w3,' '
	b.ne	.eq
	add	x0,x0,1
	b	.sp
.eq:	cmp	w3,'='
	b.ne	.no
	add	x0,x0,1
.v:	ldrb	w3,[x0]
	cmp	w3,' '
	b.ne	.ok
	add	x0,x0,1
	b	.v
.ok:	ret

; x0 = mot de notes.ini -> x0 = couleur (0 jaune...)
colour_of:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.c:	cmp	x20,7
	b.hs	.y
	adr	x1,s_colours
	add	x1,x1,x20,lsl 3
	mov	x0,x19
	bl	str_eq
	cbnz	x0,.got
	add	x20,x20,1
	b	.c
.got:	mov	x0,x20
	cmp	x0,6				; (« gray »)
	b.ne	.out
	mov	x0,5
	b	.out
.y:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; (une fin de section) la note cur_* s'ajoute aux epinglees, si son fichier existe
pin_flush:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	ldv	x0,cur_pin
	stv	xzr,cur_pin
	cbz	x0,.out
	ldv	x1,g_npin
	cmp	x1,PIN_MAX
	b.hs	.out
	adr	x19,pathbuf			; SD0:/Notes/<nom>
	strb	wzr,[x19]
	mov	x0,x19
	adr	x1,s_dir
	bl	str_cat
	adr	x1,cur_name
	bl	str_cat
	mov	x0,x19
	mov	x1,sp
	sys	SYS_STAT
	tbnz	x0,63,.out
	cbnz	x0,.out
	ldv	x1,g_npin
	mov	x2,PN_SIZE
	mul	x2,x1,x2
	adr	x3,g_pins
	add	x19,x3,x2
	add	x1,x1,1
	stv	x1,g_npin
	mov	x0,x19
	adr	x1,cur_name
	mov	x2,NOTE_FILE
	bl	mem_move
	ldv	x0,cur_col
	str	x0,[x19,PN_COL]
	ldv	x0,cur_mod
	str	x0,[x19,PN_MOD]
.out:	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; notes.ini lu : g_pins, les plus recentes d'abord
read_pins:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stv	xzr,g_npin
	stv	xzr,cur_pin
	adr	x0,s_ini_notes
	bl	read_file
	adr	x19,fbuf
	add	x20,x19,x0			; fin
.line:	cmp	x19,x20
	b.hs	.end
	mov	x21,x19				; debut
.e:	cmp	x19,x20
	b.hs	.le
	ldrb	w0,[x19]
	cmp	w0,10
	b.eq	.le
	add	x19,x19,1
	b	.e
.le:	mov	x22,x19				; fin de ligne : coupee, sans blancs
	add	x19,x19,1
.tr:	cmp	x22,x21
	b.ls	.cut
	ldrb	w0,[x22,-1]
	cmp	w0,' '
	b.hi	.cut
	sub	x22,x22,1
	b	.tr
.cut:	strb	wzr,[x22]
	ldrb	w0,[x21]
	cmp	w0,'['
	b.ne	.kv
	bl	pin_flush			; « [nom] »
	adr	x0,cur_name
	stv	xzr,cur_col
	stv	xzr,cur_mod
	add	x1,x21,1
	mov	x2,0
.n:	ldrb	w3,[x1,x2]
	cbz	w3,.nd
	cmp	w3,']'
	b.eq	.nd
	cmp	x2,NOTE_FILE-1
	b.hs	.nd
	strb	w3,[x0,x2]
	add	x2,x2,1
	b	.n
.nd:	strb	wzr,[x0,x2]
	b	.line
.kv:	mov	x0,x21
	adr	x1,s_kpin
	bl	key_value
	cbz	x0,.k2
	ldrb	w0,[x0]
	cmp	w0,'0'
	cset	x0,ne
	stv	x0,cur_pin
	b	.line
.k2:	mov	x0,x21
	adr	x1,s_kcol
	bl	key_value
	cbz	x0,.k3
	bl	colour_of
	stv	x0,cur_col
	b	.line
.k3:	mov	x0,x21
	adr	x1,s_kmod
	bl	key_value
	cbz	x0,.line
	bl	parse_dec
	stv	x0,cur_mod
	b	.line
.end:	bl	pin_flush
	mov	x19,1				; par date (insertion)
.s:	ldv	x0,g_npin
	cmp	x19,x0
	b.hs	.out
	mov	x20,x19
.j:	cbz	x20,.sn
	mov	x2,PN_SIZE
	mul	x0,x20,x2
	adr	x1,g_pins
	add	x21,x0,x1			; [j]
	sub	x22,x21,x2			; [j-1]
	ldr	x3,[x21,PN_MOD]
	ldr	x4,[x22,PN_MOD]
	cmp	x4,x3
	b.hs	.sn
	adr	x0,tmp_pin
	mov	x1,x21
	bl	mem_move
	mov	x0,x21
	mov	x1,x22
	mov	x2,PN_SIZE
	bl	mem_move
	mov	x0,x22
	adr	x1,tmp_pin
	mov	x2,PN_SIZE
	bl	mem_move
	sub	x20,x20,1
	b	.j
.sn:	add	x19,x19,1
	b	.s
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = empreinte, x1 = octets, x2 = taille -> x0 = empreinte
fnv:
	movl	w3,16777619
.c:	cbz	x2,.r
	ldrb	w4,[x1],1
	eor	w0,w0,w4
	mul	w0,w0,w3
	sub	x2,x2,1
	b	.c
.r:	ret

; x0 = carte (rang) : le debut de sa note, dans heads ; titre et corps reperes
read_head:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x1,HEAD_SIZE
	mul	x1,x19,x1
	adr	x20,heads
	add	x20,x20,x1			; x20 : texte
	mov	x1,CD_SIZE
	mul	x1,x19,x1
	adr	x21,g_cards
	add	x21,x21,x1			; x21 : carte
	mov	x1,PN_SIZE
	mul	x1,x19,x1
	adr	x22,g_pins
	add	x22,x22,x1
	str	x22,[x21,CD_PIN]
	adr	x0,pathbuf
	strb	wzr,[x0]
	adr	x1,s_dir
	bl	str_cat
	mov	x1,x22
	bl	str_cat
	adr	x0,pathbuf
	mov	x1,x20
	mov	x2,NOTE_HEAD
	bl	read_into
	strb	wzr,[x20,x0]
	mov	x22,x0
	ldrb	w1,[x20]			; (une marque BOM : sautee)
	cmp	w1,0xEF
	b.ne	.nobom
	ldrb	w1,[x20,1]
	cmp	w1,0xBB
	b.ne	.nobom
	mov	x0,x20
	add	x1,x20,3
	sub	x2,x22,2
	bl	mem_move
	sub	x22,x22,3
.nobom:	mov	x1,0				; \r : \n
.cr:	cmp	x1,x22
	b.hs	.title
	ldrb	w2,[x20,x1]
	cmp	w2,13
	b.ne	.crn
	mov	w2,10
	strb	w2,[x20,x1]
	add	x3,x1,1
	ldrb	w4,[x20,x3]
	cmp	w4,10
	b.ne	.crn
	mov	w4,' '
	strb	w4,[x20,x3]
.crn:	add	x1,x1,1
	b	.cr
.title:	mov	x0,x20				; premiere ligne non vide
.t:	ldrb	w1,[x0]
	cbz	w1,.ts
	cmp	w1,' '
	b.hi	.ts
	add	x0,x0,1
	b	.t
.ts:	str	x0,[x21,CD_TITLE]
.e:	ldrb	w1,[x0]
	cbz	w1,.tb
	cmp	w1,10
	b.eq	.tn
	add	x0,x0,1
	b	.e
.tn:	strb	wzr,[x0],1
.b:	ldrb	w1,[x0]				; le corps : sans blancs devant
	cmp	w1,10
	b.eq	.bs
	cmp	w1,' '
	b.ne	.tb
.bs:	add	x0,x0,1
	b	.b
.tb:	str	x0,[x21,CD_BODY]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; relit les notes -> x0 = 1 si ce qui est montre change
reload:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	read_pins
	ldv	x0,g_npin
	cmp	x0,CARDS
	mov	x1,CARDS
	csel	x19,x1,x0,hi
	stv	x19,g_ncard
	mov	x20,0
.c:	cmp	x20,x19
	b.hs	.sig
	mov	x0,x20
	bl	read_head
	add	x20,x20,1
	b	.c
.sig:	movl	w0,2166136261
	adr	x1,g_pins
	ldv	x2,g_npin
	mov	x3,PN_SIZE
	mul	x2,x2,x3
	bl	fnv
	adr	x1,heads
	mov	x2,HEAD_SIZE
	mul	x2,x19,x2
	bl	fnv
	ldv	x1,g_sig
	stv	x0,g_sig
	cmp	x0,x1
	cset	x0,ne
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Souris
; ---------------------------------------------------------------------------

; x1, x2 = point -> x0 = carte, HIT_MORE, -1 : rien
hit:
	mov	x0,-1
	cmp	x2,HDR
	b.lt	.r
	cmp	x1,CX
	b.lt	.r
	cmp	x1,CX+CWD
	b.ge	.r
	ldv	x3,g_npin
	cbnz	x3,.cards
	cmp	x2,TOP
	b.lt	.r
	cmp	x2,TOP+EMPTY_H
	b.ge	.r
	mov	x0,HIT_MORE
	ret
.cards:	ldv	x3,g_shown
	adr	x4,g_cards
	mov	x5,0
.c:	cmp	x5,x3
	b.hs	.m
	ldr	x6,[x4,CD_Y]
	ldr	x7,[x4,CD_H]
	cmp	x2,x6
	b.lt	.n
	add	x6,x6,x7
	cmp	x2,x6
	b.ge	.n
	mov	x0,x5
	ret
.n:	add	x4,x4,CD_SIZE
	add	x5,x5,1
	b	.c
.m:	ldv	x3,g_moreY
	tbnz	x3,63,.r
	cmp	x2,x3
	b.lt	.r
	add	x3,x3,MORE_H
	cmp	x2,x3
	b.ge	.r
	mov	x0,HIT_MORE
.r:	ret

on_mouse:
	prologue
	adr	x9,ui_ev
	ldr	w19,[x9,EV_KIND]
	ldrsw	x1,[x9,EV_X]
	ldrsw	x2,[x9,EV_Y]
	str	x2,[sp,-16]!
	bl	hit
	ldr	x2,[sp],16
	cmp	w19,1
	b.eq	.press
	cmp	w19,2
	b.eq	.out
	ldv	x1,g_hot
	cmp	x0,x1
	b.eq	.out
	stv	x0,g_hot
	bl	draw
	b	.out
.press:	cmp	x2,HDR
	b.lt	.out
	cmn	x0,1
	b.eq	.out
	adr	x1,s_empty
	cmp	x0,HIT_MORE
	b.eq	.spawn
	mov	x1,CD_SIZE			; Notes, sur cette note
	mul	x0,x0,x1
	adr	x1,g_cards
	add	x0,x0,x1
	ldr	x19,[x0,CD_PIN]
	adr	x0,pathbuf
	strb	wzr,[x0]
	adr	x1,s_dir
	bl	str_cat
	mov	x1,x19
	bl	str_cat
	adr	x1,pathbuf
.spawn:	adr	x0,p_notes
	sys	SYS_SPAWN
.out:	epilogue

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------

; x0 = carte (memoire), x1 = rang : place y, hauteur d'apres son texte
; (mesure) x20 = y -> x0 = hauteur
card_measure:
	prologue
	ldr	x0,[x0,CD_BODY]
	mov	x1,0
	mov	x2,0
	mov	x3,CWD-2*CPAD
	mov	w4,0
	adr	x5,ui_font
	mov	x6,13
	mov	x7,1
	bl	ui_text_wrap
	mov	x1,LINES*LINE_H
	cmp	x0,x1
	csel	x0,x1,x0,hi
	cmp	x0,LINE_H
	mov	x1,LINE_H
	csel	x0,x1,x0,lo
	add	x0,x0,CBAND+2*CPAD+TITLE_H
	epilogue

; x19 = carte (memoire), x20 = rang, x21 = jetons
draw_card:
	prologue
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	ldr	x22,[x19,CD_Y]
	ldr	x23,[x19,CD_H]
	ldr	x0,[x19,CD_PIN]
	ldr	x0,[x0,PN_COL]
	adr	x1,t_paper
	ldr	w24,[x1,x0,lsl 2]
	adr	x1,t_dot
	ldr	w25,[x1,x0,lsl 2]
	mov	x0,CX+1				; l'ombre
	add	x1,x22,3
	mov	x2,CWD
	mov	x3,x23
	mov	x4,CR+1
	movl	w5,0xC8000000
	mov	w6,w5
	bl	ui_rbox
	ldv	x0,g_hot			; survolee : un contour
	cmp	x0,x20
	b.ne	.band
	mov	x0,CX-1
	sub	x1,x22,1
	mov	x2,CWD+2
	add	x3,x23,2
	mov	x4,CR+1
	movl	w5,0x00FFFFFF
	mov	w6,w5
	bl	ui_rbox
.band:	mov	x0,CX
	mov	x1,x22
	mov	x2,CWD
	mov	x3,20
	mov	x4,CR
	mov	w5,w25
	mov	w6,w25
	bl	ui_rbox
	mov	x0,CX				; le papier
	add	x1,x22,CBAND
	mov	x2,CWD
	sub	x3,x23,CBAND
	mov	x4,CR
	mov	w5,w24
	mov	w6,w24
	bl	ui_rbox
	ldr	x0,[x19,CD_TITLE]
	ldrb	w1,[x0]
	cbnz	w1,.t
	adr	x0,s_newnote
	bl	tr_x0
.t:	mov	x1,CX+CPAD
	add	x2,x22,CBAND+CPAD+11
	movl	w3,NOTE_INK
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,CWD-2*CPAD
	bl	ui_text_fit
	add	x2,x22,CBAND+CPAD+TITLE_H	; le corps, coupe a sa place
	mov	x0,CX
	mov	x1,x2
	mov	x2,CWD
	sub	x3,x23,CBAND+2*CPAD+TITLE_H
	bl	ui_clip
	mov	w0,w24
	movl	w1,NOTE_INK
	mov	x2,215
	bl	ui_mix
	mov	w4,w0
	ldr	x0,[x19,CD_BODY]
	mov	x1,CX+CPAD
	add	x2,x22,CBAND+CPAD+TITLE_H
	mov	x3,CWD-2*CPAD
	adr	x5,ui_font
	mov	x6,13
	mov	x7,0
	bl	ui_text_wrap
	mov	x0,0
	mov	x1,0
	mov	x2,0
	mov	x3,0
	bl	ui_clip
	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	epilogue

; x0 = texte, x1 = x, x2 = y (ligne de base), w3, x4 = police, x5 = largeur max
text13:
	mov	x6,x5
	mov	x5,x4
	mov	x4,13
	b	ui_text_fit

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x21,g_tok
	mov	x0,0				; tout transparent
	mov	x1,0
	mov	x2,W_
	ldv	x3,g_h
	movl	w4,CLEAR
	bl	ui_fill
	ldr	w5,[x21,TJ_SURFACE]		; le titre
	and	w5,w5,0xFFFFFF
	movl	w0,ALPHA
	orr	w5,w5,w0
	mov	w6,w5
	mov	x0,0
	mov	x1,0
	mov	x2,W_
	mov	x3,HDR
	mov	x4,10
	bl	ui_rbox
	mov	x0,14				; une petite note jaune
	mov	x1,9
	mov	x2,16
	mov	x3,16
	mov	x4,2
	adr	x9,t_paper
	ldr	w5,[x9]
	mov	w6,w5
	bl	ui_rbox
	mov	x0,17
	mov	x1,14
	mov	x2,8
	mov	x3,1
	movl	w4,0x00A08850
	bl	ui_fill
	mov	x0,17
	mov	x1,17
	mov	x2,6
	mov	x3,1
	movl	w4,0x00A08850
	bl	ui_fill
	adr	x0,s_title
	bl	tr_x0
	mov	x1,38
	mov	x2,23
	ldr	w3,[x21,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x19,x0
	ldv	x0,g_npin
	cbz	x0,.lay
	adr	x20,textbuf
	mov	w1,'('
	strb	w1,[x20],1
	mov	x1,x20
	bl	fmt_dec
	mov	w1,')'
	strb	w1,[x0],1
	strb	wzr,[x0]
	adr	x0,textbuf
	add	x1,x19,6
	mov	x2,23
	ldr	w3,[x21,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.lay:	mov	x22,TOP				; y
	mov	x20,0
	stv	xzr,g_shown
	mov	x0,-1
	stv	x0,g_moreY
.c:	ldv	x0,g_ncard
	cmp	x20,x0
	b.hs	.more
	mov	x0,CD_SIZE
	mul	x0,x20,x0
	adr	x19,g_cards
	add	x19,x19,x0
	mov	x0,x19
	bl	card_measure
	str	x22,[x19,CD_Y]
	str	x0,[x19,CD_H]
	ldv	x1,g_h				; (plus de place : « +N autres »)
	sub	x1,x1,MORE_H
	add	x2,x22,x0
	cmp	x2,x1
	b.le	.fits
	cbnz	x20,.more
.fits:	bl	draw_card
	ldr	x0,[x19,CD_H]
	add	x22,x22,x0
	add	x22,x22,CGAP
	add	x20,x20,1
	stv	x20,g_shown
	b	.c
.more:	ldv	x0,g_npin
	cbz	x0,.none
	ldv	x1,g_shown
	cmp	x1,x0
	b.hs	.show
	sub	x19,x0,x1			; « +N autres dans Notes »
	sub	x22,x22,4
	stv	x22,g_moreY
	ldr	w5,[x21,TJ_SURFACE]
	and	w5,w5,0xFFFFFF
	movl	w0,ALPHA
	orr	w5,w5,w0
	mov	w6,w5
	mov	x0,CX
	mov	x1,x22
	mov	x2,CWD
	mov	x3,MORE_H
	mov	x4,6
	bl	ui_rbox
	adr	x20,textbuf
	mov	w1,'+'
	strb	w1,[x20],1
	mov	x0,x19
	mov	x1,x20
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x1,s_more
	bl	tr_x1
	bl	str_cat
	adr	x0,textbuf
	mov	x1,CX+8
	add	x2,x22,16
	ldr	w3,[x21,TJ_TEXTE1]
	adr	x4,ui_font
	mov	x5,CWD-16
	bl	text13
	b	.show
.none:	ldr	w5,[x21,TJ_SURFACE]		; rien d'epingle
	and	w5,w5,0xFFFFFF
	movl	w0,ALPHA
	orr	w5,w5,w0
	mov	w6,w5
	mov	x0,CX
	mov	x1,TOP
	mov	x2,CWD
	mov	x3,EMPTY_H
	mov	x4,8
	bl	ui_rbox
	adr	x0,s_nopin
	bl	tr_x0
	mov	x1,CX+14
	mov	x2,TOP+26
	ldr	w3,[x21,TJ_TEXTE1]
	adr	x4,ui_font_b
	mov	x5,CWD-28
	bl	text13
	adr	x0,s_clickn
	bl	tr_x0
	mov	x1,CX+14
	mov	x2,TOP+46
	ldr	w3,[x21,TJ_TEXTE2]
	adr	x4,ui_font
	mov	x5,CWD-28
	bl	text13
.show:	bl	ui_update_all
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

t_paper		dw 0x00FCE9A6,0x00D3EBC6,0x00CFE0F3,0x00F8D3D8,0x00E2D6F0,0x00E4E2DE
t_dot		dw 0x00E8B21F,0x0067A657,0x005284C4,0x00D9667A,0x008A68C2,0x00908C86
s_colours	db 'yellow',0,0,'green',0,0,0,'blue',0,0,0,0,'pink',0,0,0,0
		db 'purple',0,0,'grey',0,0,0,0,'gray',0,0,0,0
s_svc		db 'stickies',0
s_title		db 'Notes ',0xC3,0xA9,'pingl',0xC3,0xA9,'es',0
s_more		db ' autres dans Notes',0
s_nopin		db 'Aucune note ',0xC3,0xA9,'pingl',0xC3,0xA9,'e',0
s_clickn	db 'Cliquer pour ouvrir Notes',0
s_newnote	db 'Nouvelle note',0
s_empty		db 0
s_kst		db 'stickies',0
s_kpin		db 'pinned',0
s_kcol		db 'colour',0
s_kmod		db 'modified',0
s_dir		db 'SD0:/Notes/',0
s_ini_notes	db 'SD0:/Notes/notes.ini',0
s_cfg		db 'SD0:/apps/notes.app/config.ini',0
p_notes		db 'SD0:/apps/notes.app/main',0
		align 8

include '../../inc/tr.inc'

segment readable writeable
kit_data
		align 8
g_svc		dd -1
g_tok		dd 0
g_win		dd 0
g_x		dd 0
g_h		dd 0
g_npin		dd 0
g_ncard		dd 0
g_shown		dd 0
g_moreY		dd -1
g_sig		dd 0
g_hot		dd -1
g_ticks		dd 0
cur_pin		dd 0
cur_col		dd 0
cur_mod		dd 0
scr		rb 32
msg		rb WS_MSG
textbuf		rb 96
pathbuf		rb 128
cur_name	rb NOTE_FILE
tmp_pin		rb PN_SIZE
		align 8
g_cards		rb CARDS * CD_SIZE
g_pins		rb PIN_MAX * PN_SIZE
heads		rb CARDS * HEAD_SIZE
fbuf		rb FBUF
ui_data
