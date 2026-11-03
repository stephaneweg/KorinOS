; ============================================================================
;  multiwin - plusieurs fenetres non modales dans un programme (UIKit,
;  kits/uikit/uikit_window.inc) : une seule boucle d'evenements, chaque fenetre
;  a ses widgets, son focus, sa taille, ses menus et sa fermeture. Le programme
;  finit quand la derniere fenetre est fermee.
;
;  Dans chaque fenetre : un bandeau dessine a la main (sa couleur, son numero),
;  un compteur, un champ de texte, une horloge que la boucle met a jour dans
;  toutes les fenetres quelle que soit la courante, et les boutons :
;  Nouvelle fenetre, Compter, Compter dans la fenetre 1 (un widget d'une autre
;  fenetre), Message (dialogue modal), Ouvrir (selecteur de fichiers).
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

MAX_WINS	= 8
WIN_W		= 420
WIN_H		= 280
BAND_H		= 44
; un emplacement de fenetre
SL_WIN		= 0			; poignee (0 : libre)
SL_NUM		= 8			; numero
SL_COUNT	= 16			; compteur
SL_LCOUNT	= 24			; etiquette du compteur
SL_LTIME	= 32			; etiquette de l'horloge
SL_TEXT		= 40			; champ de texte
SL_BTN		= 48			; boutons (BTN_N)
BTN_N		= 5
SL_SIZE		= SL_BTN+BTN_N*8
; actions (donnee des boutons, entrees de menu)
ACT_NEW		= 1
ACT_COUNT	= 2
ACT_COUNT1	= 3
ACT_MESSAGE	= 4
ACT_OPEN	= 5
ACT_CLOSE	= 6

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	bl	new_win
.loop:	mov	x0,1000
	bl	ui_event
	cbnz	x0,.ev
	bl	tick				; une seconde : l'horloge de chaque fenetre
	b	.loop
.ev:	mov	x19,x0
	cmp	x19,WEV_CLOSE
	b.eq	.close
	cmp	x19,WEV_MENU
	b.eq	.menu
	mov	x0,0				; routage : par EV_WIN
	bl	ui_menu_key
	tbz	x0,63,.act
	mov	x0,0
	bl	ui_w_event
	cmp	x1,UN_ACTIVATE
	b.ne	.loop
	mov	x20,x0
	bl	ui_w_user
	cbz	x0,.loop
	mov	x1,x0
	mov	x0,x20
	bl	ui_w_window
	b	.do
.close:	adr	x0,ui_ev			; la case de fermeture d'une fenetre
	ldr	w0,[x0,EV_WIN]
	bl	close_win
	b	.end
.menu:	adr	x0,ui_ev
	ldr	w0,[x0,EV_CODE]
.act:	mov	x1,x0
	adr	x0,ui_ev
	ldr	w0,[x0,EV_WIN]
.do:	bl	action
.end:	bl	ui_window_count
	cbnz	x0,.loop
	mov	x0,0
	sys	SYS_EXIT

; x0 = fenetre, x1 = action
action:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x1
	bl	slot_of
	mov	x19,x0
	cbz	x19,.out
	cmp	x20,ACT_NEW
	b.ne	.c
	bl	new_win
	b	.out
.c:	cmp	x20,ACT_COUNT
	b.ne	.c1
	mov	x0,x19
	bl	count_in
	b	.out
.c1:	cmp	x20,ACT_COUNT1
	b.ne	.m
	adr	x0,slots			; le compteur de la fenetre 1 (si elle vit)
	bl	count_in
	b	.out
.m:	cmp	x20,ACT_MESSAGE
	b.ne	.o
	ldr	x0,[x19,SL_WIN]			; le dialogue bloque tout le programme
	bl	ui_window_use
	mov	x20,x0
	adr	x0,s_title
	adr	x1,s_modal
	mov	x2,UM_OK
	bl	ui_message
	mov	x0,x20
	bl	ui_window_use
	b	.out
.o:	cmp	x20,ACT_OPEN
	b.ne	.x
	adr	x0,s_filters
	bl	ui_file_filter
	mov	x0,0
	adr	x1,path
	mov	x2,256
	bl	ui_file_open
	cbz	x0,.out
	ldr	x0,[x19,SL_TEXT]		; le chemin choisi dans le champ de cette
	adr	x1,path				; fenetre
	bl	ui_w_set_text
	b	.out
.x:	cmp	x20,ACT_CLOSE
	b.ne	.out
	ldr	x0,[x19,SL_WIN]
	bl	close_win
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre -> x0 = son emplacement (0 : aucun)
slot_of:
	adr	x1,slots
	mov	x2,MAX_WINS
.l:	ldr	x3,[x1,SL_WIN]
	cmp	x3,x0
	b.eq	.yes
	add	x1,x1,SL_SIZE
	subs	x2,x2,1
	b.ne	.l
	mov	x0,0
	ret
.yes:	mov	x0,x1
	ret

; x0 = fenetre : fermee, widgets detruits, emplacement libre
close_win:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	slot_of
	cbz	x0,.close
	str	xzr,[x0,SL_WIN]
.close:	mov	x0,x19
	bl	ui_window_close
	ldr	x19,[sp],16
	epilogue

; x0 = emplacement : compteur + 1, dans sa fenetre
count_in:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,SL_WIN]
	cbz	x0,.out
	ldr	x0,[x19,SL_COUNT]
	add	x0,x0,1
	str	x0,[x19,SL_COUNT]
	mov	x0,x19
	bl	show_count
.out:	ldr	x19,[sp],16
	epilogue

; x0 = emplacement : le texte de son compteur (widget redessine dans sa fenetre)
show_count:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,buf
	adr	x1,s_counter
	bl	str_cat
	mov	x1,x0
	ldr	x0,[x19,SL_COUNT]
	bl	fmt_dec
	strb	wzr,[x0]
	ldr	x0,[x19,SL_LCOUNT]
	adr	x1,buf
	bl	ui_w_set_text
	ldr	x19,[sp],16
	epilogue

; une seconde de plus : l'horloge de toutes les fenetres (widgets de fenetres
; qui ne sont pas la courante)
tick:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,seconds
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	adr	x19,slots
	mov	x20,MAX_WINS
.l:	ldr	x0,[x19,SL_WIN]
	cbz	x0,.n
	adr	x0,buf
	adr	x1,s_time
	bl	str_cat
	mov	x1,x0
	adr	x0,seconds
	ldr	x0,[x0]
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	w1,'s'
	strb	w1,[x0],1
	strb	wzr,[x0]
	ldr	x0,[x19,SL_LTIME]
	adr	x1,buf
	bl	ui_w_set_text
.n:	add	x19,x19,SL_SIZE
	subs	x20,x20,1
	b.ne	.l
	ldp	x19,x20,[sp],16
	epilogue

; (fenetre courante) le bandeau : sa couleur, son numero, sa taille
draw_band:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	ui_jetons
	mov	x20,x0
	ldr	w0,[x20,TJ_ACCENT]		; une teinte par fenetre
	ldr	x1,[x19,SL_NUM]
	and	x1,x1,3
	lsl	x1,x1,4
	add	x1,x1,96
	bl	ui_tone
	mov	w4,w0
	mov	x0,0
	mov	x1,0
	adr	x2,ui_w
	ldr	x2,[x2]
	mov	x3,BAND_H
	bl	ui_fill
	adr	x0,buf
	adr	x1,s_title
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	ldr	x0,[x19,SL_NUM]
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,buf
	mov	x1,14
	mov	x2,29
	mov	w3,0x00FFFFFF
	mov	x4,18
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	ldp	x19,x20,[sp],16
	epilogue

; (new_win) x19 = emplacement, x20 = rang du bouton, x1 = texte, x2 = action
add_button:
	prologue
	stp	x21,x22,[sp,-16]!
	mov	x21,x1
	mov	x22,x2
	mov	x0,16
	mov	x1,BAND_H+12+64
	mov	x2,36
	mul	x2,x20,x2
	add	x1,x1,x2
	mov	x2,240
	mov	x3,0
	mov	x4,x21
	mov	x5,0
	bl	ui_pushbutton
	add	x1,x19,SL_BTN
	str	x0,[x1,x20,lsl 3]
	mov	x1,0
	mov	x2,x22
	bl	ui_w_on
	add	x20,x20,1
	ldp	x21,x22,[sp],16
	epilogue

; ouvre une fenetre de plus, dans un emplacement libre
new_win:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,slots
	mov	x20,MAX_WINS
.f:	ldr	x0,[x19,SL_WIN]
	cbz	x0,.got
	add	x19,x19,SL_SIZE
	subs	x20,x20,1
	b.ne	.f
	b	.out
.got:	adr	x0,serial
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	str	x1,[x19,SL_NUM]
	str	xzr,[x19,SL_COUNT]
	adr	x0,buf
	adr	x1,s_title
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	ldr	x0,[x19,SL_NUM]
	bl	fmt_dec
	strb	wzr,[x0]
	mov	x0,-1
	mov	x1,-1
	mov	x2,WIN_W
	mov	x3,WIN_H+96
	adr	x4,buf
	mov	x5,WF_RESIZABLE
	bl	ui_window_ex
	tbnz	x0,63,.out
	str	x0,[x19,SL_WIN]
	mov	x0,320				; taille, fonction de cette fenetre
	mov	x1,300
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	adr	x0,menu_main			; ses menus : la premiere, les autres
	ldr	x1,[x19,SL_NUM]
	cmp	x1,1
	b.eq	.mn
	adr	x0,menu_other
.mn:	bl	ui_menu
	bl	ui_jetons			; le fond suit le theme
	ldr	w4,[x0,TJ_FACE]
	mov	x0,0
	mov	x1,0
	adr	x2,ui_w
	ldp	x2,x3,[x2]
	bl	ui_fill
	mov	x0,x19
	bl	draw_band
	mov	x0,16				; le compteur, l'horloge, le champ
	mov	x1,BAND_H+12
	mov	x2,180
	mov	x3,24
	adr	x4,s_empty
	mov	x5,ULB_BOLD
	bl	ui_label
	str	x0,[x19,SL_LCOUNT]
	mov	x0,212
	mov	x1,BAND_H+12
	mov	x2,180
	mov	x3,24
	adr	x4,s_empty
	mov	x5,ULB_DIM
	bl	ui_label
	str	x0,[x19,SL_LTIME]
	mov	x0,16
	mov	x1,BAND_H+44
	mov	x2,WIN_W-32
	mov	x3,30
	mov	x4,0
	bl	ui_textbox
	str	x0,[x19,SL_TEXT]
	mov	x20,0				; les boutons
	adr	x1,s_b_new
	mov	x2,ACT_NEW
	bl	add_button
	adr	x1,s_b_count
	mov	x2,ACT_COUNT
	bl	add_button
	adr	x1,s_b_count1
	mov	x2,ACT_COUNT1
	bl	add_button
	adr	x1,s_b_msg
	mov	x2,ACT_MESSAGE
	bl	add_button
	adr	x1,s_b_open
	mov	x2,ACT_OPEN
	bl	add_button
	mov	x0,x19
	bl	show_count
	ldr	x0,[x19,SL_TEXT]
	bl	ui_w_focus
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
.out:	ldp	x19,x20,[sp],16
	epilogue

; (ui_on_resize) x0 = largeur, x1 = hauteur : la fenetre de l'evenement est la
; courante ; le champ suit la largeur
on_resize:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x0
	adr	x0,ui_win
	ldr	x0,[x0]
	bl	slot_of
	mov	x19,x0
	cbz	x19,.out
	bl	ui_jetons
	ldr	w4,[x0,TJ_FACE]
	mov	x0,0
	mov	x1,0
	adr	x2,ui_w
	ldp	x2,x3,[x2]
	bl	ui_fill
	mov	x0,x19
	bl	draw_band
	ldr	x0,[x19,SL_TEXT]
	mov	x1,16
	mov	x2,BAND_H+44
	sub	x3,x20,32
	mov	x4,30
	bl	ui_w_move
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
.out:	ldp	x19,x20,[sp],16
	epilogue

segment readable writeable
kit_data
		align 8
slots		rb MAX_WINS*SL_SIZE
serial		dd 0
seconds		dd 0
s_title		db 'Fen',0xC3,0xAA,'tre',0
s_empty		db 0
s_counter	db 'Compteur : ',0
s_time		db 'Ouverte depuis ',0
s_b_new		db 'Nouvelle fen',0xC3,0xAA,'tre',0
s_b_count	db 'Compter',0
s_b_count1	db 'Compter dans la fen',0xC3,0xAA,'tre 1',0
s_b_msg		db 'Message (modal)',0
s_b_open	db 'Ouvrir un fichier',0xE2,0x80,0xA6,0
s_modal		db 'Ce dialogue bloque toutes les fen',0xC3,0xAA,'tres du programme : un clic sur l',0x27,'une d',0x27,'elles le ram',0xC3,0xA8,'ne devant.',0
s_filters	db 'Textes|*.txt;*.ini|Images|*.png;*.bmp|Tous les fichiers|*',0
menu_main	db 'MFen',0xC3,0xAA,'tres',10
		db 'I1',9,'Nouvelle fen',0xC3,0xAA,'tre',9,'Ctrl+N',10
		db 'I2',9,'Compter',9,'Ctrl+K',10
		db 'I6',9,'Fermer',9,'Ctrl+W',10,0
menu_other	db 'MAutre',10
		db 'I2',9,'Compter ici',9,'Ctrl+K',10
		db 'I6',9,'Fermer',9,'Ctrl+W',10,0
		align 8
buf		rb 300
path		rb 256
ui_data
