; ============================================================================
;  viewer - visionneuse d'images (BMP, PNG) : les images de UIKit
;  (ui_image_load, ui_imagebox). Fenetre redimensionnable ; l'image est
;  ajustee a la fenetre, ou montree en taille reelle (menu Affichage).
;  Argument : le chemin d'une image ; Fichier > Ouvrir (Ctrl+O) : dialogue.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W		= 640
WIN_H		= 480
SB_H		= 24				; barre d'etat
ID_OUVRIR	= 1
ID_FERMER	= 2
ID_AJUSTER	= 3
ID_REELLE	= 4

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'

start:
	stp	x0,x1,[sp,-16]!
	bl	kits_open
	ldp	x0,x1,[sp],16
	mov	x19,x0				; argument : le chemin
	adr	x0,ui_win
	bl	ui_init
	mov	x0,-1
	mov	x1,-1
	mov	x2,WIN_W
	mov	x3,WIN_H
	adr	x4,s_title0
	bl	tr_x4
	mov	x5,WF_RESIZABLE
	bl	ui_window_ex
	tbnz	x0,63,quit
	adr	x0,menus
	bl	ui_menu
	bl	ui_jetons
	adr	x1,jetons
	str	x0,[x1]
	mov	x0,240
	mov	x1,180
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	mov	x0,0				; la boite d'image : tout sauf la barre
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,WIN_H-SB_H
	mov	x4,0
	mov	x5,0
	bl	ui_imagebox
	adr	x1,w_img
	str	x0,[x1]
	cbz	x19,.paint
	adr	x0,path
	mov	x1,x19
	bl	str_cat
	bl	open_path
.paint:	bl	paint
.loop:	mov	x0,1000
	bl	ui_event
	cbz	x0,.loop
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MENU
	b.eq	.menu
	cmp	x0,WEV_KEY
	b.ne	.w
	adr	x0,ui_ev			; Ctrl+O, Ctrl+W
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.w
	ldr	w2,[x0,EV_MODS]
	mov	w3,0x11
	tst	w2,w3
	b.eq	.w
	ldr	w2,[x0,EV_CODE]
	mov	w1,ID_OUVRIR
	cmp	w2,0x12
	b.eq	.cmd
	mov	w1,ID_FERMER
	cmp	w2,0x1A
	b.eq	.cmd
.w:	mov	x0,0
	bl	ui_w_event
	b	.loop
.menu:	adr	x0,ui_ev
	ldr	w1,[x0,EV_CODE]
.cmd:	cmp	w1,ID_FERMER
	b.eq	quit
	cmp	w1,ID_OUVRIR
	b.ne	.m3
	mov	x0,0
	adr	x1,path
	mov	x2,256
	bl	ui_file_open
	cbz	x0,.loop
	bl	open_path
	bl	paint
	b	.loop
.m3:	mov	x2,0				; Ajuster / Taille reelle
	cmp	w1,ID_REELLE
	b.ne	.st
	mov	x2,UIM_NOSCALE
.st:	adr	x0,w_img
	ldr	x0,[x0]
	mov	x1,x2
	bl	ui_w_style
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; ouvre l'image de « path » (l'ancienne rendue) ; titre, message
open_path:
	prologue
	adr	x0,path
	bl	ui_image_load
	cbz	x0,.err
	adr	x1,image
	ldr	x2,[x1]
	str	x0,[x1]
	mov	x0,x2
	bl	ui_image_free
	adr	x0,w_img
	ldr	x0,[x0]
	adr	x1,image
	ldr	x1,[x1]
	bl	ui_imagebox_set
	adr	x0,msg				; « largeur x hauteur »
	str	xzr,[x0]
	b	.title
.err:	adr	x0,s_err
	bl	tr_x0
	adr	x1,msg
	str	x0,[x1]
.title:	adr	x0,titlebuf			; « Visionneuse - nom »
	adr	x1,s_title
	bl	tr_x1
	bl	str_cat
	adr	x1,path
	mov	x2,x1
.n:	ldrb	w3,[x1],1
	cbz	w3,.nd
	cmp	w3,'/'
	b.ne	.n
	mov	x2,x1
	b	.n
.nd:	mov	x1,x2
	bl	str_cat
	adr	x0,titlebuf
	bl	ui_title
	epilogue

; fonction de ui_on_resize : la boite suit la fenetre
on_resize:
	prologue
	adr	x2,win_w
	stp	x0,x1,[x2]
	mov	x4,x1
	sub	x4,x4,SB_H
	mov	x3,x0
	adr	x0,w_img
	ldr	x0,[x0]
	mov	x1,0
	mov	x2,0
	bl	ui_w_move
	bl	paint
	epilogue

; l'image et la barre d'etat
paint:
	prologue
	sub	sp,sp,128
	mov	x0,0
	bl	ui_w_draw
	mov	x0,0				; barre d'etat
	adr	x9,win_w
	ldp	x2,x1,[x9]
	sub	x1,x1,SB_H
	mov	x3,SB_H
	adr	x4,jetons
	ldr	x4,[x4]
	ldr	w4,[x4,TJ_FACE]
	bl	ui_fill
	adr	x0,msg
	ldr	x1,[x0]
	cbnz	x1,.txt
	adr	x0,image			; taille de l'image
	ldr	x0,[x0]
	adr	x1,s_none
	bl	tr_x1
	cbz	x0,.txt
	bl	ui_image_size
	str	x1,[sp,-16]!
	add	x1,sp,16
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	w1,0xC3				; « × »
	strb	w1,[x0],1
	mov	w1,0x97
	strb	w1,[x0],1
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	ldr	x0,[sp],16
	bl	fmt_dec
	adr	x1,s_px
	bl	str_cat
	mov	x1,sp
.txt:	mov	x0,x1
	mov	x1,10
	adr	x9,win_h
	ldr	x2,[x9]
	sub	x2,x2,7
	adr	x3,jetons
	ldr	x3,[x3]
	ldr	w3,[x3,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	bl	ui_update_all
	add	sp,sp,128
	epilogue

include '../../inc/tr.inc'

segment readable writeable
kit_data
		align 8
jetons		dd 0
w_img		dd 0
image		dd 0
msg		dd 0
win_w		dd WIN_W			; taille (contigus)
win_h		dd WIN_H
s_title0	db 'Visionneuse',0
s_title		db 'Visionneuse - ',0
s_err		db 'Image illisible (BMP ou PNG non entrelac',0xC3,0xA9,' attendu)',0
s_none		db 'Fichier > Ouvrir (Ctrl+O)',0
s_px		db ' pixels',0
menus		db 'MFichier',10
		db 'I1',9,'Ouvrir',0xE2,0x80,0xA6,9,'Ctrl+O',10
		db '-',10
		db 'I2',9,'Fermer',9,'Ctrl+W',10
		db 'MAffichage',10
		db 'I3',9,'Ajuster ',0xC3,0xA0,' la fen',0xC3,0xAA,'tre',10
		db 'I4',9,'Taille r',0xC3,0xA9,'elle',10,0
		align 8
path		rb 260
titlebuf	rb 300
ui_data
