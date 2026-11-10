; terminal - console graphique de Korin OS : un emulateur VT/xterm sur un
; pseudo-terminal (service vterm, docs/VTERM.md)
;
; Au demarrage : un pty (VT_OPEN), SD0:/bin/sh lance avec l'esclave en entree,
; sortie et erreur (sh interactif) ; le Terminal garde le maitre. Les touches,
; traduites en octets et sequences (vt_keys.inc : key_encode), sont ecrites
; sur le maitre (la discipline de ligne de vterm fait l'echo, l'edition,
; Ctrl+C -> SIGINT) ; la sortie lue sur le maitre passe a l'analyseur (vt.inc :
; grille, sequences ; vt_draw.inc : dessin) ; les reponses DSR / DA y
; repartent. Fenetre redimensionnable : nouvelle grille, TIOCSWINSZ (VT_SWINSZ :
; SIGWINCH au programme). sh fini (fin lue sur le maitre) : le Terminal se ferme.
; Les commandes internes d'avant sont dans sh (docs/SHELL.md).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../inc/vterm.inc'
include '../../kits/uikit/uikit.inc'

WIN_W		= 760
WIN_H		= 480
PAD		= 6
LH		= 18				; hauteur de ligne
FONT_PX		= 15

COL_BG		= 0x001C2026
COL_TXT		= 0x00D8DEE6
COL_CURSOR	= 0x00F0C674
MIN_W		= 320				; taille minimale de la fenetre
MIN_H		= 160
RD_MAX		= 32				; lectures du maitre par passage

macro tputs lbl* {
	adr	x0,lbl
	bl	lang_tr
	bl	term_puts
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,ui_win				; variables de l'interface
	bl	ui_init
	mov	x0,-1
	mov	x1,-1
	mov	x2,WIN_W
	mov	x3,WIN_H
	adr	x4,s_title
	mov	x5,WF_GLYPH_LIGHT or WF_BODY_DARK or WF_RESIZABLE
	bl	ui_window_ex
	tbnz	x0,63,quit
	mov	x0,MIN_W
	mov	x1,MIN_H
	bl	ui_min_size
	adr	x0,s_m100			; avance de la police mono (x100)
	mov	x1,FONT_PX
	adr	x2,ui_font_m
	bl	ui_text_width
	cmp	x0,0
	mov	x1,900
	csel	x0,x1,x0,le
	adr	x1,adv100
	str	x0,[x1]
	mov	x0,UFN_MONOBOLD			; gras : DejaVu Sans Mono Bold
	bl	ui_font_get
	adr	x1,ui_font			; (absente, ui_font_get rend la sans)
	cmp	x0,x1
	b.ne	.bold
	adr	x0,ui_font_m
.bold:	adr	x1,font_bold
	str	x0,[x1]
	bl	vt_init
	tbnz	x0,63,quit
	bl	vt_fit
	bl	pty_start
	bl	term_draw

main_loop:
	bl	pty_poll			; la sortie du programme
	cmn	x0,1
	b.eq	quit				; sh fini : le Terminal aussi
	mov	x1,15
	cbz	x0,.wait
	bl	term_draw
	mov	x1,1				; (encore de la sortie, sans doute)
.wait:	mov	x0,x1
	bl	ui_event
	cbz	x0,main_loop
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_RESIZE
	b.eq	.resize
	cmp	x0,WEV_WHEEL
	b.ne	main_loop
	adr	x0,ui_ev			; molette : 3 lignes par cran
	ldrsw	x1,[x0,EV_Y]
	mov	x2,3
	mul	x1,x1,x2
	bl	scroll_by
	bl	term_draw
	b	main_loop
.key:	bl	on_key
	b	main_loop
.resize:					; nouvelle taille : nouvelle grille
	adr	x0,ui_ev
	ldr	w1,[x0,EV_Y]
	ldr	w0,[x0,EV_X]
	bl	ui_resize
	tbnz	x0,63,main_loop
	bl	vt_fit
	bl	term_draw
	b	main_loop

quit:	mov	x0,0
	sys	SYS_EXIT

; x1 = deplacement (+ = vers le haut)
scroll_by:
	adr	x2,scroll
	ldr	x3,[x2]
	add	x3,x3,x1
	cmp	x3,0
	csel	x3,xzr,x3,lt
	adr	x4,vt				; au plus l'historique (rien sur
	ldr	x5,[x4,V_SBN]			; l'ecran alternatif)
	ldr	x6,[x4,V_ALT]
	cmp	x6,0
	csel	x4,xzr,x5,ne
	cmp	x3,x4
	csel	x3,x4,x3,gt
	str	x3,[x2]
	ret

; ---------------------------------------------------------------------------
;  Clavier : Maj+Page haut / bas font defiler la vue ; tout le reste va au
;  programme (key_encode -> bout maitre)
; ---------------------------------------------------------------------------
on_key:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.out				; relache
	ldr	w19,[x0,EV_CODE]
	ldr	w20,[x0,EV_CHAR]
	ldr	w2,[x0,EV_MODS]
	mov	w3,K_MOD_SHIFT			; (vt_keys.inc)
	tst	w2,w3
	b.eq	.prog
	cmp	w19,KEY_PGUP
	b.ne	.pgdn
	bl	page_rows
	mov	x1,x0
	bl	scroll_by
	bl	term_draw
	b	.out
.pgdn:	cmp	w19,KEY_PGDN
	b.ne	.prog
	bl	page_rows
	neg	x1,x0
	bl	scroll_by
	bl	term_draw
	b	.out
.prog:	adr	x0,ui_ev
	ldr	w2,[x0,EV_MODS]
	mov	w0,w19
	mov	w1,w20
	mov	x3,sp
	bl	key_encode
	cbz	x0,.out
	mov	x1,x0
	mov	x0,sp
	bl	pty_write
	adr	x0,scroll			; toute frappe ramene en bas
	ldr	x1,[x0]
	cbz	x1,.out
	str	xzr,[x0]
	bl	term_draw
.out:	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Le pseudo-terminal
; ---------------------------------------------------------------------------

; vterm, un pty, SD0:/bin/sh sur l'esclave ; un message s'il manque quelque chose
pty_start:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	adr	x0,s_vterm
	mov	x1,5000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.novt
	adr	x1,vtc
	str	x0,[x1]
	adr	x19,msg
	mov	x0,VT_OPEN
	str	x0,[x19]
	str	wzr,[x19,MSG_LEN]
	adr	x0,vtc
	ldr	x0,[x0]
	mov	x1,x19
	sys	SYS_SVC_CALL
	tbnz	x0,63,.novt
	ldp	x0,x20,[x19,MSG_A]		; maitre, esclave
	adr	x1,master
	str	x0,[x1]
	bl	pty_winsz			; la taille avant le shell
	stp	x20,x20,[sp]			; sh : entree, sortie, erreur = l'esclave
	str	x20,[sp,16]
	adr	x0,p_sh
	mov	x1,0
	mov	x2,sp
	bl	f_spawn_stdio
	mov	x19,x0
	adr	x2,msg				; notre copie de l'esclave : rendue (sh
	mov	x1,VT_CLOSE			; la tient ; sa fin = fin du maitre)
	stp	x1,x20,[x2]
	str	wzr,[x2,MSG_LEN]
	adr	x0,vtc
	ldr	x0,[x0]
	mov	x1,x2
	sys	SYS_SVC_CALL
	tbz	x19,63,.out
	tputs	s_nosh
	mov	x0,x19
	bl	term_dec
	mov	w0,13
	bl	term_putb
	mov	w0,10
	bl	term_putb
	b	.out
.novt:	adr	x0,master
	str	xzr,[x0]
	tputs	s_novt
.out:	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 : sortie lue (a dessiner), 0 rien, -1 : fin (le programme est parti)
pty_poll:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,0
	adr	x0,master
	ldr	x0,[x0]
	cbz	x0,.out
	mov	x19,RD_MAX
.rd:	cbz	x19,.out
	sub	x19,x19,1
	adr	x2,msg
	mov	x1,VT_READ
	stp	x1,x0,[x2]
	mov	x1,FS_DONNEES
	mov	x3,VT_NOWAIT
	stp	x1,x3,[x2,MSG_B]
	str	wzr,[x2,MSG_LEN]
	adr	x0,vtc
	ldr	x0,[x0]
	mov	x1,x2
	sys	SYS_SVC_CALL
	cmn	x0,11				; E_AGAIN : rien pour l'instant
	b.eq	.out
	tbnz	x0,63,.out
	cbz	x0,.fin
	mov	x1,x0
	adr	x0,msg
	add	x0,x0,MSG_DATA
	bl	term_write
	mov	x20,1
	adr	x0,master
	ldr	x0,[x0]
	b	.rd
.fin:	mov	x20,-1
.out:	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = octets, x1 = longueur : sur le bout maitre (ce que le programme lira)
pty_write:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x22,50				; (pty plein : 50 essais de 2 ms)
	adr	x0,master
	ldr	x0,[x0]
	cbz	x0,.out
.m:	cbz	x20,.out
	adr	x21,msg
	mov	x0,VT_WRITE
	adr	x1,master
	ldr	x1,[x1]
	stp	x0,x1,[x21]
	mov	x2,x20
	mov	x0,FS_DONNEES
	cmp	x2,x0
	csel	x2,x0,x2,hi
	str	w2,[x21,MSG_LEN]
	add	x0,x21,MSG_DATA
	mov	x1,x19
	bl	mem_move
	adr	x0,vtc
	ldr	x0,[x0]
	mov	x1,x21
	sys	SYS_SVC_CALL
	tbnz	x0,63,.out
	cbz	x0,.plein
	add	x19,x19,x0
	sub	x20,x20,x0
	b	.m
.plein:	subs	x22,x22,1
	b.eq	.out
	mov	x0,2
	sys	SYS_SLEEP
	b	.m
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine : reponse au programme (DSR, DA : vt.inc)
term_reply_s:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	str_len
	mov	x1,x0
	mov	x0,x19
	bl	pty_write
	ldr	x19,[sp],16
	epilogue

; la taille de la grille (et de la fenetre en pixels) au pty : VT_SWINSZ
; (SIGWINCH au groupe de premier plan si elle change)
pty_winsz:
	prologue
	adr	x0,master
	ldr	x0,[x0]
	cbz	x0,.out
	adr	x2,msg
	mov	x1,VT_SWINSZ
	stp	x1,x0,[x2]
	adr	x9,vt
	ldr	x0,[x9,V_ROWS]
	ldr	x1,[x9,V_COLS]
	stp	x0,x1,[x2,MSG_B]
	adr	x9,ui_w
	ldp	x0,x1,[x9]
	and	x0,x0,0xFFFF
	and	x1,x1,0xFFFF
	orr	x0,x0,x1,lsl 16
	str	x0,[x2,MSG_D]
	str	wzr,[x2,MSG_LEN]
	adr	x0,vtc
	ldr	x0,[x0]
	mov	x1,x2
	sys	SYS_SVC_CALL
.out:	epilogue

; ---------------------------------------------------------------------------
;  Sortie : tout passe par l'analyseur (vt.inc)
; ---------------------------------------------------------------------------

; -> x0 = lignes d'une page (Maj+Page haut, Maj+Page bas)
page_rows:
	adr	x0,vt
	ldr	x0,[x0,V_ROWS]
	sub	x0,x0,2
	cmp	x0,1
	b.ge	.r
	mov	x0,1
.r:	ret

; taille de la fenetre -> lignes et colonnes de la grille ; tout a redessiner ;
; la taille au pty
vt_fit:
	prologue
	adr	x9,ui_w
	ldp	x0,x1,[x9]
	sub	x0,x0,2*PAD
	cmp	x0,0
	csel	x0,xzr,x0,lt
	mov	x2,100
	mul	x0,x0,x2
	adr	x2,adv100
	ldr	x2,[x2]
	udiv	x0,x0,x2
	sub	x1,x1,2*PAD
	cmp	x1,0
	csel	x1,xzr,x1,lt
	mov	x2,LH
	udiv	x1,x1,x2
	bl	vt_resize
	adr	x0,full_redraw
	mov	x1,1
	str	x1,[x0]
	bl	pty_winsz
	epilogue

; x0 = donnees, x1 = octets
term_write:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.c:	cbz	x20,.out
	ldrb	w0,[x19],1
	bl	term_putb
	sub	x20,x20,1
	b	.c
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine
term_puts:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
.c:	ldrb	w0,[x19],1
	cbz	w0,.out
	bl	term_putb
	b	.c
.out:	ldr	x19,[sp],16
	epilogue

; x0 = valeur signee
term_dec:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	tbz	x0,63,.pos
	mov	w2,'-'
	strb	w2,[x1],1
	neg	x0,x0
.pos:	bl	fmt_dec
	mov	x0,sp
	bl	term_puts
	add	sp,sp,32
	epilogue

include 'vt.inc'
include 'vt_draw.inc'
include 'vt_keys.inc'

segment readable writeable
kit_data

ui_data
		align 8
	csi_table
	pal_data
	keys_data

s_title		db 'Terminal',0
s_m100		db 'MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM',0
s_vterm		db 'vterm',0
p_sh		db 'SD0:/bin/sh',0
s_novt		db 'Terminal : pas de pseudo-terminal (service vterm absent).',13,10,0
s_nosh		db 'Terminal : SD0:/bin/sh ne se lance pas, erreur ',0
s_da1		db 27,'[?62;22c',0		; VT220, couleurs ANSI
s_da2		db 27,'[>0;10;1c',0
s_dsr_ok	db 27,'[0n',0
	align 8
adv100	dd 0
scroll	dd 0				; vue remontee de tant de lignes (0 : en bas)
font_bold dd 0
full_redraw dd 0
lc_valid dd 0				; curseur dessine : ligne, colonne
lc_row	dd 0
lc_col	dd 0
vt_shadow dd 0
vt_sb	dd 0
vtc	dd 0				; canal de vterm
master	dd 0				; bout maitre du pty (0 : aucun)
msg	rb MSG_TAILLE
	align 16
vt	rb V_SIZE
rowp_main rb VT_MAXROWS*8
rowp_alt rb VT_MAXROWS*8
textbuf	rb VT_MAXCOLS*4+8
