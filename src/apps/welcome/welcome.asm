; bienvenue - fenetre d'accueil : presentation, echo du clavier, bouton Fermer
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W	= 560
WIN_H	= 340
ID_FERMER = 1				; entree « Fermer » du menu Fichier
KEY_W	= 0x1A				; code HID de W

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
	adr	x4,title
	bl	tr_x4
	mov	x5,WF_GLYPH_LIGHT
	bl	ui_window_ex
	tbnz	x0,63,quit
	adr	x0,menus			; menus dans la barre de menus globale
	bl	ui_menu
	bl	draw
.loop:	mov	x0,1000
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_MENU
	b.eq	.menu
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cmp	x0,WEV_KEY
	b.eq	.key
	b	.loop
.mouse:	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,1				; appui
	b.ne	.loop
	mov	x0,WIN_W-130
	mov	x1,WIN_H-52
	mov	x2,110
	mov	x3,34
	bl	ui_hit
	cbnz	x0,quit
	b	.loop
.menu:	adr	x0,ui_ev			; choix dans la barre de menus
	ldr	w1,[x0,EV_CODE]
	cmp	w1,ID_FERMER
	b.eq	quit
	b	.loop
.key:	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.loop			; relache
	ldr	w2,[x0,EV_MODS]			; Ctrl+W : Fermer (le raccourci du menu
	mov	w3,0x11				; est traite ici)
	tst	w2,w3
	b.eq	.chr
	ldr	w2,[x0,EV_CODE]
	cmp	w2,KEY_W
	b.eq	quit
.chr:	ldr	w1,[x0,EV_CHAR]
	cbz	w1,.loop
	adr	x2,typed
	adr	x3,typed_len
	ldr	x4,[x3]
	cmp	w1,8				; Retour arriere : efface le dernier caractere
	b.eq	.bksp
	cmp	w1,' '				; autres codes de controle : ignores
	b.lo	.loop
	b	.add
.bksp:	cbz	x4,.loop
.back:	sub	x4,x4,1				; remonter les octets de suite UTF-8 (10xxxxxx)
	cbz	x4,.st
	ldrb	w5,[x2,x4]
	and	w5,w5,0xC0
	cmp	w5,0x80
	b.eq	.back
	b	.st
.add:						; ajoute le caractere (UTF-8)
	cmp	x4,40
	b.lo	.room
	mov	x4,0
.room:	cmp	w1,0x80
	b.hs	.u2
	strb	w1,[x2,x4]
	add	x4,x4,1
	b	.st
.u2:	cmp	w1,0x800
	b.hs	.u3
	lsr	w5,w1,6
	orr	w5,w5,0xC0
	strb	w5,[x2,x4]
	add	x4,x4,1
	and	w5,w1,0x3F
	orr	w5,w5,0x80
	strb	w5,[x2,x4]
	add	x4,x4,1
	b	.st
.u3:	lsr	w5,w1,12
	orr	w5,w5,0xE0
	strb	w5,[x2,x4]
	add	x4,x4,1
	ubfx	w5,w1,6,6
	orr	w5,w5,0x80
	strb	w5,[x2,x4]
	add	x4,x4,1
	and	w5,w1,0x3F
	orr	w5,w5,0x80
	strb	w5,[x2,x4]
	add	x4,x4,1
.st:	strb	wzr,[x2,x4]
	str	x4,[x3]
	bl	draw_typed
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

draw:
	prologue
	mov	x0,0				; bandeau
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,70
	jeton	w4,TJ_ACCENT
	bl	ui_fill
	adr	x0,s_head
	mov	x1,24
	mov	x2,46
	movl	w3,0x00FFFFFF
	mov	x4,26
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	adr	x0,s_l1
	bl	tr_x0
	mov	x1,24
	mov	x2,108
	jeton	w3,TJ_TEXTE1
	mov	x4,16
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	adr	x0,s_l2
	bl	tr_x0
	mov	x1,24
	mov	x2,134
	jeton	w3,TJ_TEXTE1
	mov	x4,16
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	adr	x0,s_l3
	bl	tr_x0
	mov	x1,24
	mov	x2,160
	jeton	w3,TJ_TEXTE2
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	adr	x0,s_l4
	bl	tr_x0
	mov	x1,24
	mov	x2,206
	jeton	w3,TJ_TEXTE1
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x0,WIN_W-130
	mov	x1,WIN_H-52
	mov	x2,110
	mov	x3,34
	adr	x4,s_close
	bl	tr_x4
	mov	x5,1
	bl	ui_button
	bl	draw_typed
	bl	ui_update_all
	epilogue

draw_typed:
	prologue
	mov	x0,24
	mov	x1,218
	mov	x2,WIN_W-48
	mov	x3,32
	jeton	w4,TJ_SURFACE
	bl	ui_fill
	mov	x0,24
	mov	x1,218
	mov	x2,WIN_W-48
	mov	x3,32
	jeton	w4,TJ_LIGNE_FORTE
	bl	ui_frame
	adr	x0,typed
	mov	x1,32
	mov	x2,240
	jeton	w3,TJ_TEXTE1
	mov	x4,16
	adr	x5,ui_font_m
	mov	x6,0
	bl	ui_text
	mov	x0,24
	mov	x1,218
	mov	x2,WIN_W-48
	mov	x3,32
	bl	ui_update
	epilogue


include '../../inc/tr.inc'

segment readable writeable
kit_data

title	db 'Bienvenue sur Korin OS',0
s_head	db 'Korin OS',0
s_l1	db 'Un syst',0xC3,0xA8,'me 64 bits pour Raspberry Pi 4,',0
s_l2	db 0xC3,0xA9,'crit enti',0xC3,0xA8,'rement en assembleur AArch64 (FASMARM).',0
s_l3	db 'SMP 4 coeurs, pr',0xC3,0xA9,'emptif, process isol',0xC3,0xA9,'s, FAT32, TrueType maison.',0
s_l4	db 'Tapez quelque chose :',0
s_close	db 'Fermer',0
menus	db 'MFichier',10,'I1',9,'Fermer',9,'Ctrl+W',10,0
	align 8
typed_len dd 0
typed	rb 64
ui_data
