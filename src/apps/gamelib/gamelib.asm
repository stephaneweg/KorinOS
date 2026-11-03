; ============================================================================
;  gamelib - la bibliotheque de jeux, en assembleur (docs/EMULATEURS.md) : d'apres
;  la Game Library d'Onyx, sur UIKit.
;
;  A gauche les consoles, avec leur nombre de jeux ; a droite les cartes, une
;  image et un nom. Un clic choisit, un double-clic ou Entree joue : la ROM part
;  avec son emulateur, en fenetre ou, avec View > Play Full Screen, en plein
;  ecran. Clavier : les fleches, Entree. Manette : la croix, Start ou A, L et R
;  changent de console.
;
;  Dossiers : SD0:/home/roms, SD1:/roms, et ceux de SD0:/home/gamelib/folders.txt
;  (Library > Add Folder...), sous-dossiers compris.
;  Images : chaque jeu tourne quelques secondes sans etre montre, par son emulateur
;  (nesemu --thumb sortie rom), un a la fois, en arriere-plan ; gardees dans
;  SD0:/home/gamelib/thumbs. N64 et DS : le nom seul (pas d'image).
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/pad.inc'
include '../../kits/uikit/uikit.inc'

GL_W		= 980
GL_H		= 580
GL_SIDE		= 200
GL_SIDE_MIN	= 130
TW		= 160
TH		= 120
CARD_W		= 176
CARD_H		= 160
CARD_GAP	= 10
GL_HEAD		= 40		; en haut, le nom de la console (rien sous les boutons)
G_MAX		= 2000
G_PATH		= 0		; chemin (200)
G_NAME		= 200		; nom (64)
G_SYS		= 264
G_THUMB		= 272		; pixels, 0 : pas encore
G_TRIED		= 280
G_SIZE		= 288
SYS_N		= 8
S_NAME		= 0		; 24
S_EMU		= 24		; 32
S_EXT		= 56		; 40 : « .nes », suivi de 0 ; plusieurs de suite
S_THUMB		= 96		; 1 : image par l'emulateur
S_COUNT		= 104
S_ON		= 112
S_SIZE		= 120
DEPTH_MAX	= 4
TH_WAIT_MS	= 120000

ID_REFRESH	= 1
ID_ADD		= 2
ID_QUIT		= 3
ID_FULL		= 4

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
	adr	x0,ui_win
	bl	ui_init
	adr	x0,g_scr
	bl	ui_screen
	adr	x0,g_scr
	ldp	x1,x2,[x0]
	ldp	x3,x4,[x0,16]
	sub	x1,x1,8
	sub	x2,x2,x3
	sub	x2,x2,x4
	sub	x2,x2,128
	mov	x5,GL_W
	cmp	x1,x5
	csel	x2,x2,x2,al
	csel	x1,x5,x1,hi			; (au plus GL_W)
	mov	x5,GL_H
	cmp	x2,x5
	csel	x3,x5,x2,hi
	mov	x2,x1
	mov	x0,0
	mov	x1,x4
	adr	x4,s_title
	mov	x5,WF_RESIZABLE
	bl	ui_window_ex
	tbnz	x0,63,quit
	mov	x0,480
	mov	x1,320
	bl	ui_min_size
	adr	x0,on_resize
	mov	x1,0
	bl	ui_on_resize
	bl	ui_jetons
	stv	x0,g_tok
	adr	x0,s_menus
	bl	ui_menu
	adr	x0,d_home
	bl	f_mkdir
	adr	x0,d_lib
	bl	f_mkdir
	adr	x0,d_thumbs
	bl	f_mkdir
	adr	x0,d_roms
	bl	f_mkdir
	movl	x0,G_MAX*G_SIZE
	bl	alloc
	stv	x0,g_games
	cbz	x0,quit
	bl	sys_init
	; les controles
	ldv	x0,ui_w
	ldv	x1,ui_h
	bl	side_width
	mov	x19,x0
	mov	x0,0
	mov	x1,0
	mov	x2,x19
	ldv	x3,ui_h
	mov	x4,UL_SIDEBAR
	bl	ui_list
	stv	x0,g_list
	mov	x1,0
	mov	x2,x19
	mov	x0,x19
	mov	x1,0
	ldv	x2,ui_w
	sub	x2,x2,x19
	ldv	x3,ui_h
	mov	x4,UVW_FOCUS
	bl	ui_view
	stv	x0,g_view
	adr	x1,v_paint
	adr	x2,v_mouse
	adr	x3,v_key
	adr	x4,v_wheel
	bl	ui_view_on
	bl	rescan
	ldv	x0,g_view
	bl	ui_w_focus
	ldv	x0,g_view
	bl	ui_view_dirty
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
loop:	mov	x0,50
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MENU
	b.eq	.menu
	cmp	x0,WEV_PAD
	b.eq	.pad
	cmp	x0,0
	b.le	.idle
	mov	x19,0
	cmp	x0,WEV_KEY
	b.ne	.w
	adr	x1,ui_ev
	ldr	w19,[x1,EV_KIND]
.w:	mov	x0,0
	bl	ui_w_event
	mov	x20,x2				; pris
	cbz	x1,.k
	ldv	x2,g_list
	cmp	x0,x2
	b.ne	.k
	cmp	x1,UN_CHANGE
	b.ne	.k
	ldv	x0,g_list
	bl	ui_w_get_value
	bl	set_filter
.k:	cbz	x19,.tick
	cbnz	x20,.tick
	mov	x0,0
	bl	ui_menu_key
	tbnz	x0,63,.tick
	bl	command
	b	.tick
.menu:	adr	x0,ui_ev
	ldr	w0,[x0,EV_CODE]
	bl	command
	b	.tick
.pad:	bl	on_pad
	b	.tick
.idle:	bl	ui_w_idle
.tick:	bl	ui_view_flush
	bl	thumb_tick
	b	loop
quit:	mov	x0,0
	sys	SYS_EXIT

; x0 = octets -> memoire a zero
alloc:
	mov	x1,x0
	mov	x0,0
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	mov	x4,x30
	sys	SYS_VM_MAP
	mov	x30,x4
	cmp	x0,0
	csel	x0,xzr,x0,lt
	ret

; x0 = largeur de la fenetre -> x0 = largeur de la colonne
side_width:
	lsr	x1,x0,3
	add	x1,x1,x1,lsl 1			; 3/8 au plus
	mov	x0,GL_SIDE
	cmp	x1,x0
	csel	x0,x0,x1,hs
	mov	x1,GL_SIDE_MIN
	cmp	x0,x1
	csel	x0,x1,x0,lo
	ret

on_resize:				; x0 = largeur, x1 = hauteur
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	side_width
	mov	x5,x0
	stp	x5,xzr,[sp,-16]!
	ldv	x0,g_list
	mov	x1,0
	mov	x2,0
	mov	x3,x5
	mov	x4,x20
	bl	ui_w_move
	ldp	x5,x6,[sp],16
	ldv	x0,g_view
	mov	x1,x5
	mov	x2,0
	sub	x3,x19,x5
	mov	x4,x20
	bl	ui_w_move
	ldv	x0,g_view
	bl	ui_view_dirty
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Les consoles : celles dont l'emulateur est installe
; ---------------------------------------------------------------------------

sys_init:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,sys_tab
	adr	x20,g_sys
	mov	x2,S_SIZE*SYS_N
.c:	ldrb	w3,[x19],1
	strb	w3,[x20],1
	subs	x2,x2,1
	b.ne	.c
	adr	x19,g_sys
	mov	x20,SYS_N
.s:	add	x0,x19,S_EMU
	adr	x1,g_stat
	bl	f_stat
	cmp	x0,0
	cset	x0,ge
	str	x0,[x19,S_ON]
	add	x19,x19,S_SIZE
	subs	x20,x20,1
	b.ne	.s
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Les jeux
; ---------------------------------------------------------------------------

rescan:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x19,g_ng			; les images d'avant
	ldv	x20,g_games
	mov	x21,0
.f:	cmp	x21,x19
	b.hs	.z
	ldr	x0,[x20,G_THUMB]
	cbz	x0,.fn
	str	xzr,[x20,G_THUMB]
.fn:	add	x20,x20,G_SIZE
	add	x21,x21,1
	b	.f
.z:	stv	xzr,g_ng
	adr	x19,g_sys
	mov	x20,SYS_N
.c:	str	xzr,[x19,S_COUNT]
	add	x19,x19,S_SIZE
	subs	x20,x20,1
	b.ne	.c
	adr	x0,d_roms
	mov	x1,0
	bl	scan_dir
	adr	x0,d_roms1
	mov	x1,0
	bl	scan_dir
	; les dossiers ajoutes
	adr	x0,p_folders
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.done
	mov	x19,x0
	adr	x1,g_folders
	mov	x2,4095
	bl	f_lire
	mov	x20,x0
	mov	x0,x19
	bl	f_fermer
	cmp	x20,0
	b.le	.done
	adr	x19,g_folders
	strb	wzr,[x19,x20]
.l:	ldrb	w0,[x19]
	cbz	w0,.done
	mov	x20,x19
.e:	ldrb	w0,[x20]
	cbz	w0,.e1
	cmp	w0,10
	b.eq	.e0
	cmp	w0,13
	b.eq	.e0
	add	x20,x20,1
	b	.e
.e0:	strb	wzr,[x20],1
.e1:	ldrb	w0,[x19]
	cbz	w0,.e2
	mov	x0,x19
	mov	x1,0
	bl	scan_dir
.e2:	mov	x19,x20
	b	.l
.done:	bl	sort_games
	bl	side_fill
	bl	refilter
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dossier, x1 = profondeur (tout le fil sur la pile : un tampon par niveau)
scan_dir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,512+DIRENT_BYTES
	mov	x19,x0
	mov	x20,x1
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x21,x0
.e:	mov	x0,x21
	add	x1,sp,512
	bl	f_lire_rep
	cmp	x0,1
	b.ne	.fin
	add	x22,sp,512
	ldrb	w0,[x22]
	cmp	w0,'.'
	b.eq	.e
	mov	x0,sp				; le chemin
	mov	x1,x19
	mov	x2,x22
	bl	path_join
	ldr	x0,[x22,DIRENT_ATTR]
	tbz	x0,4,.file			; (attribut FAT : dossier)
	cmp	x20,DEPTH_MAX
	b.hs	.e
	mov	x0,sp
	add	x1,x20,1
	bl	scan_dir
	b	.e
.file:	mov	x0,sp
	mov	x1,x22
	bl	add_game
	b	.e
.fin:	mov	x0,x21
	bl	f_fermer
.out:	add	sp,sp,512+DIRENT_BYTES
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin, x1 = nom : une ROM d'une console installee ?
add_game:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x21,g_sys
	mov	x22,0
.s:	ldr	x0,[x21,S_ON]
	cbz	x0,.n
	add	x23,x21,S_EXT
.x:	ldrb	w0,[x23]
	cbz	w0,.n
	mov	x0,x20
	mov	x1,x23
	bl	str_ends
	cbnz	x0,.ok
	mov	x0,x23
	bl	str_len
	add	x23,x23,x0
	add	x23,x23,1
	b	.x
.n:	add	x21,x21,S_SIZE
	add	x22,x22,1
	cmp	x22,SYS_N
	b.lo	.s
	b	.out
.ok:	ldr	x0,[x21,S_COUNT]
	add	x0,x0,1
	str	x0,[x21,S_COUNT]
	ldv	x0,g_ng
	cmp	x0,G_MAX
	b.hs	.out
	mov	x1,G_SIZE
	mul	x1,x0,x1
	ldv	x2,g_games
	add	x2,x2,x1
	add	x0,x0,1
	stv	x0,g_ng
	str	x22,[x2,G_SYS]
	str	xzr,[x2,G_THUMB]
	str	xzr,[x2,G_TRIED]
	mov	x0,x2				; le chemin
	mov	x1,x19
	mov	x3,199
	bl	copy_n
	add	x0,x2,G_NAME			; le nom, sans l'extension
	mov	x1,x20
	mov	x3,63
	bl	copy_n
	mov	x4,x0
.d:	cmp	x4,x2
	b.ls	.out
	ldrb	w5,[x4,-1]!
	cmp	w5,'.'
	b.ne	.d
	strb	wzr,[x4]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source, x3 = caracteres au plus -> x0 = fin (garde x2)
copy_n:
	cbz	x3,.f
	ldrb	w4,[x1],1
	cbz	w4,.f
	strb	w4,[x0],1
	sub	x3,x3,1
	b	copy_n
.f:	strb	wzr,[x0]
	ret

; par console, puis par nom (tri par insertion : quelques centaines de jeux)
sort_games:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x19,g_games
	ldv	x20,g_ng
	mov	x21,1
.i:	cmp	x21,x20
	b.hs	.out
	mov	x22,x21
.j:	cbz	x22,.n
	mov	x0,G_SIZE
	mul	x0,x22,x0
	add	x1,x19,x0			; b
	sub	x0,x1,G_SIZE			; a
	bl	game_gt
	cbz	x2,.n
	mov	x3,G_SIZE			; echange
.x:	subs	x3,x3,8
	ldr	x4,[x0,x3]
	ldr	x5,[x1,x3]
	str	x5,[x0,x3]
	str	x4,[x1,x3]
	b.ne	.x
	sub	x22,x22,1
	b	.j
.n:	add	x21,x21,1
	b	.i
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = jeux -> x2 = 1 : a apres b (garde x0, x1)
game_gt:
	ldr	x2,[x0,G_SYS]
	ldr	x3,[x1,G_SYS]
	cmp	x2,x3
	b.ne	.d
	add	x4,x0,G_NAME
	add	x5,x1,G_NAME
.c:	ldrb	w2,[x4],1
	ldrb	w3,[x5],1
	orr	w6,w2,0x20
	orr	w7,w3,0x20
	cmp	w6,w7
	b.ne	.r
	cbnz	w2,.c
	mov	x2,0
	ret
.r:	cset	x2,hi
	ret
.d:	cset	x2,hi
	ret

; la colonne : Tous les jeux, puis chaque console installee, avec son nombre
side_fill:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x0,g_list
	bl	ui_list_clear
	adr	x0,s_all
	bl	ui_tr
	mov	x1,x0
	adr	x0,s_line
	bl	strcpy2
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	ldv	x0,g_ng
	bl	fmt_dec
	strb	wzr,[x0]
	ldv	x0,g_list
	adr	x1,s_line
	mov	x2,0
	bl	ui_list_add
	adr	x19,g_sys
	mov	x20,0
.s:	ldr	x0,[x19,S_ON]
	cbz	x0,.n
	adr	x0,s_line
	mov	x1,x19
	bl	strcpy2
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	ldr	x0,[x19,S_COUNT]
	bl	fmt_dec
	strb	wzr,[x0]
	ldv	x0,g_list
	adr	x1,s_line
	add	x2,x20,1
	bl	ui_list_add
.n:	add	x19,x19,S_SIZE
	add	x20,x20,1
	cmp	x20,SYS_N
	b.lo	.s
	ldv	x0,g_list
	ldv	x1,g_rowsel
	bl	ui_w_set_value
	ldp	x19,x20,[sp],16
	epilogue

strcpy2:				; x0 = destination, x1 = texte -> x0 = fin
	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,strcpy2
	sub	x0,x0,1
	ret

; x0 = rang dans la colonne -> filtre
set_filter:
	prologue
	stv	x0,g_rowsel
	cbz	x0,.all
	mov	x2,x0
	ldv	x0,g_list
	mov	x1,x2
	bl	ui_list_get			; x1 = donnee
	mov	x0,x1
.all:	stv	x0,g_filter
	stv	xzr,g_scroll
	stv	xzr,g_sel
	bl	refilter
	epilogue

refilter:
	ldv	x0,g_games
	ldv	x1,g_ng
	ldv	x2,g_filter
	adr	x3,g_vis
	mov	x4,0				; rang
	mov	x5,0				; montres
.l:	cmp	x4,x1
	b.hs	.f
	cbz	x2,.y
	ldr	x6,[x0,G_SYS]
	add	x6,x6,1
	cmp	x6,x2
	b.ne	.n
.y:	str	w4,[x3,x5,lsl 2]
	add	x5,x5,1
.n:	add	x0,x0,G_SIZE
	add	x4,x4,1
	b	.l
.f:	stv	x5,g_nvis
	ldv	x0,g_view
	b	ui_view_dirty

; ---------------------------------------------------------------------------
;  Les cartes
; ---------------------------------------------------------------------------

; -> x0 = colonnes (g_vw : largeur de la vue)
columns:
	ldv	x0,g_vw
	sub	x0,x0,CARD_GAP
	mov	x1,CARD_W+CARD_GAP
	udiv	x0,x0,x1
	mov	x1,1
	cmp	x0,1
	csel	x0,x1,x0,lo
	ret

; (vue, largeur, hauteur)
v_paint:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	stv	x20,g_vw
	stv	x21,g_vh
	mov	x0,0
	mov	x1,0
	mov	x2,x20
	mov	x3,x21
	ldv	x4,g_tok
	ldr	w4,[x4,TJ_FACE]
	bl	ui_fill
	ldv	x0,g_nvis
	cbnz	x0,.go
	adr	x0,s_empty
	bl	ui_tr
	mov	x1,CARD_GAP
	mov	x2,GL_HEAD+24
	ldv	x3,g_tok
	ldr	w3,[x3,TJ_TEXTE2]
	mov	x4,15
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	b	.out
.go:	bl	columns
	mov	x22,x0				; colonnes
	mov	x23,0				; carte
.c:	ldv	x0,g_nvis
	cmp	x23,x0
	b.hs	.out
	udiv	x0,x23,x22
	msub	x1,x0,x22,x23			; colonne
	mov	x2,CARD_H+CARD_GAP
	mul	x0,x0,x2
	add	x0,x0,GL_HEAD
	ldv	x2,g_scroll
	subs	x25,x0,x2			; y
	mov	x2,CARD_W+CARD_GAP
	mul	x1,x1,x2
	add	x24,x1,CARD_GAP			; x
	cmn	x25,CARD_H
	b.lt	.n
	cmp	x25,x21
	b.ge	.out
	mov	x0,x19
	mov	x1,x23
	mov	x2,x24
	mov	x3,x25
	bl	card
.n:	add	x23,x23,1
	b	.c
.out:	mov	x0,0				; l'en-tete, par-dessus les cartes
	mov	x1,0
	mov	x2,x20
	mov	x3,GL_HEAD
	ldv	x4,g_tok
	ldr	w4,[x4,TJ_FACE]
	bl	ui_fill
	adr	x0,s_all
	bl	ui_tr
	ldv	x1,g_filter
	cbz	x1,.head
	sub	x1,x1,1
	mov	x2,S_SIZE
	mul	x1,x1,x2
	adr	x0,g_sys
	add	x0,x0,x1			; (S_NAME)
.head:	mov	x1,CARD_GAP
	mov	x2,28
	ldv	x3,g_tok
	ldr	w3,[x3,TJ_TEXTE1]
	mov	x4,18
	adr	x5,ui_font_b
	sub	x6,x20,CARD_GAP+FRAME_CAP_W+FRAME_CAP_MARGIN
	bl	ui_text_fit
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = vue, x1 = rang montre, x2 = x, x3 = y
card:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	mov	x21,x3
	mov	x23,x1
	adr	x0,g_vis
	ldr	w0,[x0,x1,lsl 2]
	mov	x1,G_SIZE
	mul	x0,x0,x1
	ldv	x1,g_games
	add	x22,x1,x0			; le jeu
	ldv	x4,g_tok
	ldv	x0,g_sel
	cmp	x0,x23
	b.ne	.ns
	ldr	w5,[x4,TJ_ACCENT]
	mov	x6,x5
	b	.bx
.ns:	ldr	w5,[x4,TJ_SURFACE]
	mov	x6,x5
.bx:	mov	x0,x20
	mov	x1,x21
	mov	x2,CARD_W
	mov	x3,CARD_H
	mov	x4,8
	bl	ui_rbox
	ldr	x0,[x22,G_THUMB]
	cbnz	x0,.img
	add	x0,x20,8			; pas d'image : le nom de la console
	add	x1,x21,8
	mov	x2,TW
	mov	x3,TH
	ldv	x4,g_tok
	ldr	w4,[x4,TJ_SOMBRE]
	bl	ui_fill
	ldr	x0,[x22,G_SYS]
	mov	x1,S_SIZE
	mul	x0,x0,x1
	adr	x1,g_sys
	add	x0,x1,x0
	add	x1,x20,16
	add	x2,x21,72
	movl	w3,0xFFFFFF
	mov	x4,14
	adr	x5,ui_font
	mov	x6,TW-16
	bl	ui_text_fit
	b	.nm
.img:	mov	x0,x19
	bl	ui_canvas_pixels
	cbz	x0,.nm
	mov	x4,x0
	mov	x5,x1				; largeur
	mov	x6,x2				; hauteur
	ldr	x7,[x22,G_THUMB]
	add	x3,x21,8			; y
	mov	x8,TH
.y:	cmp	x3,0
	b.lt	.yn
	cmp	x3,x6
	b.ge	.nm
	mul	x9,x3,x5
	add	x9,x9,x20
	add	x9,x9,8
	add	x9,x4,x9,lsl 2
	mov	x10,0
.x:	ldr	w11,[x7,x10,lsl 2]
	str	w11,[x9,x10,lsl 2]
	add	x10,x10,1
	cmp	x10,TW
	b.lo	.x
.yn:	add	x7,x7,TW*4
	add	x3,x3,1
	subs	x8,x8,1
	b.ne	.y
.nm:	ldv	x3,g_tok
	ldv	x0,g_sel
	cmp	x0,x23
	b.eq	.fg
	ldr	w3,[x3,TJ_TEXTE1]
	b	.t
.fg:	movl	w3,0xFFFFFF
.t:	add	x0,x22,G_NAME
	add	x1,x20,8
	add	x2,x21,TH+26
	mov	x4,13
	adr	x5,ui_font
	mov	x6,TW
	bl	ui_text_fit
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (vue, genre, x, y, infos : clics | boutons << 8)
v_mouse:
	prologue
	cmp	x1,1
	b.ne	.out
	stp	x4,xzr,[sp,-16]!
	sub	x2,x2,CARD_GAP
	mov	x5,CARD_W+CARD_GAP
	udiv	x6,x2,x5			; colonne
	cmp	x3,GL_HEAD
	b.lt	.head
	ldv	x7,g_scroll
	add	x3,x3,x7
	sub	x3,x3,GL_HEAD
	mov	x5,CARD_H+CARD_GAP
	udiv	x7,x3,x5			; ligne
	stp	x6,x7,[sp,-16]!
	bl	columns
	ldp	x6,x7,[sp],16
	cmp	x6,x0
	b.hs	.pop
	mul	x7,x7,x0
	add	x7,x7,x6
	ldv	x0,g_nvis
	cmp	x7,x0
	b.hs	.pop
	stv	x7,g_sel
	ldv	x0,g_view
	bl	ui_view_dirty
	ldr	x4,[sp]
	and	x4,x4,0xFF
	cmp	x4,2
	b.lo	.pop
	bl	play
	b	.pop
.head:	bl	ui_window_drag			; l'en-tete deplace la fenetre
.pop:	add	sp,sp,16
.out:	mov	x0,0
	epilogue

; (vue, code HID, caractere, mods)
v_key:
	prologue
	cmp	x1,0x4F
	b.eq	.r
	cmp	x1,0x50
	b.eq	.l
	cmp	x1,0x51
	b.eq	.d
	cmp	x1,0x52
	b.eq	.u
	cmp	x1,0x28
	b.ne	.non
	bl	play
	b	.oui
.r:	mov	x0,1
	b	.m
.l:	mov	x0,-1
	b	.m
.d:	bl	columns
	b	.m
.u:	bl	columns
	neg	x0,x0
.m:	bl	move_sel
.oui:	mov	x0,1
	epilogue
.non:	mov	x0,0
	epilogue

; x0 = pas : la carte choisie bouge, reste visible
move_sel:
	prologue
	ldv	x1,g_sel
	add	x1,x1,x0
	cmp	x1,0
	csel	x1,xzr,x1,lt
	ldv	x2,g_nvis
	cbz	x2,.out
	sub	x2,x2,1
	cmp	x1,x2
	csel	x1,x2,x1,gt
	stv	x1,g_sel
	stp	x1,xzr,[sp,-16]!
	bl	columns
	ldp	x1,x2,[sp],16
	udiv	x1,x1,x0			; ligne
	mov	x2,CARD_H+CARD_GAP
	mul	x1,x1,x2			; haut
	ldv	x3,g_scroll
	cmp	x1,x3
	b.hs	.b
	stv	x1,g_scroll
	b	.d
.b:	add	x1,x1,x2
	add	x1,x1,CARD_GAP
	ldv	x4,g_vh
	sub	x1,x1,x4
	add	x1,x1,GL_HEAD
	cmp	x1,x3
	b.le	.d
	stv	x1,g_scroll
.d:	ldv	x0,g_view
	bl	ui_view_dirty
.out:	epilogue

v_wheel:				; (vue, crans)
	prologue
	mov	x2,60
	mul	x1,x1,x2
	ldv	x3,g_scroll
	subs	x3,x3,x1
	csel	x3,xzr,x3,lt
	stv	x3,g_scroll
	bl	ui_view_dirty
	mov	x0,0
	epilogue

; WEV_PAD (Elegant : cette fenetre a le clavier) : les fronts
on_pad:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,ui_ev
	ldr	w1,[x0,4]			; manette 0 : le clavier, deja arrive en touches
	cbz	w1,.out
	ldr	w19,[x0,WEV_PAD_BUTTONS]
	ldv	x1,g_padold
	stv	x19,g_padold
	bic	x20,x19,x1			; les boutons qui viennent d'etre pris
	tbz	x20,0,.a
	bl	columns
	neg	x0,x0
	bl	move_sel
.a:	tbz	x20,1,.b
	bl	columns
	bl	move_sel
.b:	tbz	x20,2,.c
	mov	x0,-1
	bl	move_sel
.c:	tbz	x20,3,.d
	mov	x0,1
	bl	move_sel
.d:	mov	x0,PAD_START or PAD_B
	tst	x20,x0
	b.eq	.e
	bl	play
.e:	tbz	x20,8,.f			; PAD_L
	mov	x0,-1
	bl	next_filter
.f:	tbz	x20,9,.out			; PAD_R
	mov	x0,1
	bl	next_filter
.out:	ldp	x19,x20,[sp],16
	epilogue

next_filter:				; x0 = -1 / 1
	prologue
	stp	x19,xzr,[sp,-16]!
	ldv	x1,g_rowsel
	adds	x19,x1,x0
	csel	x19,xzr,x19,mi
	ldv	x0,g_list
	bl	ui_list_count
	sub	x0,x0,1
	cmp	x19,x0
	csel	x19,x0,x19,gt
	ldv	x0,g_list
	mov	x1,x19
	bl	ui_w_set_value
	mov	x0,x19
	bl	set_filter
	ldp	x19,x20,[sp],16
	epilogue

; la ROM choisie, par son emulateur
play:
	prologue
	ldv	x0,g_nvis
	cbz	x0,.out
	ldv	x0,g_sel
	adr	x1,g_vis
	ldr	w0,[x1,x0,lsl 2]
	mov	x1,G_SIZE
	mul	x0,x0,x1
	ldv	x1,g_games
	add	x3,x1,x0
	ldr	x0,[x3,G_SYS]
	mov	x1,S_SIZE
	mul	x0,x0,x1
	adr	x1,g_sys
	add	x4,x1,x0			; console
	adr	x0,s_line
	ldv	x1,g_full
	cbz	x1,.p
	adr	x1,s_opt_full
	bl	strcpy2
.p:	mov	x1,x3
	bl	strcpy2
	add	x0,x4,S_EMU
	adr	x1,s_line
	bl	f_spawn
.out:	epilogue

command:				; x0 = id
	prologue
	cmp	x0,ID_QUIT
	b.eq	quit
	cmp	x0,ID_REFRESH
	b.eq	.r
	cmp	x0,ID_ADD
	b.eq	.a
	cmp	x0,ID_FULL
	b.ne	.out
	ldv	x0,g_full
	eor	x0,x0,1
	stv	x0,g_full
	mov	x2,UMS_CHECKED
	cmp	x0,0
	csel	x1,x2,xzr,ne
	mov	x0,ID_FULL
	bl	ui_menu_state
	b	.out
.r:	bl	rescan
	b	.out
.a:	adr	x0,s_add_q
	bl	ui_tr
	mov	x1,x0
	adr	x0,s_add_t
	adr	x2,g_ask
	strb	wzr,[x2]
	mov	x3,190
	bl	ui_ask
	cbz	x0,.out
	adr	x0,p_folders
	mov	x1,O_WRITE or O_CREATE or O_APPEND
	bl	f_ouvrir
	tbnz	x0,63,.out
	stp	x0,xzr,[sp,-16]!
	adr	x0,g_ask
	bl	str_len
	mov	x2,x0
	adr	x1,g_ask
	mov	w3,10
	strb	w3,[x1,x2]
	add	x2,x2,1
	ldr	x0,[sp]
	bl	f_ecrire
	ldr	x0,[sp]
	bl	f_fermer
	add	sp,sp,16
	bl	rescan
.out:	epilogue

; ---------------------------------------------------------------------------
;  Les images, en arriere-plan
; ---------------------------------------------------------------------------

; x0 = jeu -> g_tpath = le fichier de son image
thumb_path:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,g_tpath
	adr	x1,d_thumbs
	bl	strcpy2
	mov	w1,'/'
	strb	w1,[x0],1
	add	x1,x19,G_NAME
	bl	strcpy2
	adr	x1,s_raw
	bl	strcpy2
	ldr	x19,[sp],16
	epilogue

; x0 = jeu -> 1 : image lue
thumb_load:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	thumb_path
	adr	x0,g_tpath
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.non
	mov	x20,x0
	movl	x0,TW*TH*4
	bl	alloc
	cbz	x0,.non
	str	x0,[x19,G_THUMB]
	mov	x1,x0
	mov	x0,x20
	movl	x2,TW*TH*4
	bl	f_lire
	mov	x0,x20
	bl	f_fermer
	ldv	x0,g_view
	bl	ui_view_dirty
	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; un pas : l'image attendue est-elle la ? sinon le jeu suivant
thumb_tick:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x19,g_tgame
	cbz	x19,.next
	mov	x0,x19
	bl	thumb_path
	adr	x0,g_tpath
	adr	x1,g_stat
	bl	f_stat
	tbnz	x0,63,.wait
	ldv	x0,g_stat
	movl	x1,TW*TH*4
	cmp	x0,x1
	b.lo	.wait
	mov	x0,x19
	bl	thumb_load
	stv	xzr,g_tgame
	b	.out
.wait:	sys	SYS_TIME_MS
	ldv	x1,g_tstart
	sub	x0,x0,x1
	movl	x1,TH_WAIT_MS
	cmp	x0,x1
	b.lo	.out
	stv	xzr,g_tgame			; (trop long : tant pis pour celui-ci)
	b	.out
.next:	ldv	x19,g_games
	ldv	x20,g_ng
.s:	cbz	x20,.out
	ldr	x0,[x19,G_TRIED]
	cbnz	x0,.n
	mov	x0,1
	str	x0,[x19,G_TRIED]
	mov	x0,x19
	bl	thumb_load
	cbnz	x0,.out
	ldr	x0,[x19,G_SYS]
	mov	x1,S_SIZE
	mul	x0,x0,x1
	adr	x1,g_sys
	add	x20,x1,x0
	ldr	x0,[x20,S_THUMB]
	cbz	x0,.out				; (pas d'image pour cette console)
	adr	x0,s_line			; « --thumb sortie rom »
	adr	x1,s_opt_thumb
	bl	strcpy2
	adr	x1,g_tpath
	bl	strcpy2
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x19
	bl	strcpy2
	add	x0,x20,S_EMU
	adr	x1,s_line
	bl	f_spawn
	tbnz	x0,63,.out
	stv	x19,g_tgame
	sys	SYS_TIME_MS
	stv	x0,g_tstart
	b	.out
.n:	add	x19,x19,G_SIZE
	sub	x20,x20,1
	b	.s
.out:	ldp	x19,x20,[sp],16
	epilogue

macro sysent name*, emu*, ext*, th* {
	local a, b, c
a:	db	name, 0
	rb	24-($-a)
b:	db	'SD0:/apps/', emu, '.app/main', 0
	rb	32-($-b)
c:	db	ext, 0, 0
	rb	40-($-c)
	dd	th, 0, 0
}
sys_tab:
	sysent 'NES', 'nesemu', '.nes', 1
	sysent 'Super Nintendo', 'snesemu', <'.sfc',0,'.smc'>, 1
	sysent 'Game Boy', 'gbemu', <'.gb',0,'.gbc'>, 1
	sysent 'Game Boy Advance', 'gbaemu', '.gba', 1
	sysent 'Nintendo 64', 'n64emu', <'.z64',0,'.n64',0,'.v64'>, 0
	sysent 'Nintendo DS', 'ndsemu', '.nds', 0
	sysent 'GameCube', 'gcemu', <'.iso',0,'.gcm',0,'.dol'>, 0
	sysent 'Nintendo 3DS', 'n3dsemu', <'.3ds',0,'.cci',0,'.cxi',0,'.3dsx'>, 0

s_title		db 'Game Library',0
s_all		db 'All Games',0
s_empty		db 'No games yet: put ROMs in SD0:/home/roms or SD1:/roms.',0
s_opt_full	db '--full ',0
s_opt_thumb	db '--thumb ',0
s_raw		db '.raw',0
s_add_t		db 'Add Folder',0
s_add_q		db 'Folder to watch (SD1:/games):',0
d_home		db 'SD0:/home',0
d_lib		db 'SD0:/home/gamelib',0
d_thumbs	db 'SD0:/home/gamelib/thumbs',0
d_roms		db 'SD0:/home/roms',0
d_roms1		db 'SD1:/roms',0
p_folders	db 'SD0:/home/gamelib/folders.txt',0
s_menus		db 'MLibrary',10
		db 'I1',9,'Refresh',9,'F5',10
		db 'I2',9,'Add Folder...',10
		db '-',10
		db 'I3',9,'Quit',9,'Ctrl+Q',10
		db 'MView',10
		db 'I4',9,'Play Full Screen',10
		db 0
		align 4

segment readable writeable
kit_data
ui_data
		align 8
g_tok		dd 0
g_scr		rb 32
g_stat		rb 32
g_games		dd 0
g_ng		dd 0
g_list		dd 0
g_view		dd 0
g_filter	dd 0
g_rowsel	dd 0
g_sel		dd 0
g_scroll	dd 0
g_nvis		dd 0
g_vw		dd 400
g_vh		dd 300
g_full		dd 0
g_padold	dd 0
g_tgame		dd 0
g_tstart	dd 0
g_sys		rb S_SIZE*SYS_N
g_vis		rb G_MAX*4
s_line		rb 512
g_tpath		rb 320
g_ask		rb 200
g_folders	rb 4096
