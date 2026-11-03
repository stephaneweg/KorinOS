; ============================================================================
;  surfacetest - essai de la surface attachee d'Elegant (WS_OP_SURFACE_ATTACH,
;  WS_OP_SURFACE_DAMAGE ; UIKit ui_surface_*, docs/DESIGN-ELEGANT.md 17).
;
;	surfacetest [secondes] [rgb565]
;
;  Une fenetre, un texte dessine par UIKit dans son contenu, et une surface
;  animee (degrade qui defile, barre blanche qui avance) composee par Elegant
;  depuis le tampon du programme. Trois phases : zone 320 x 240, zone deplacee
;  et reduite (meme tampon, ui_surface_move), zone agrandie (tampon neuf).
;  Sortie : images ecrites par seconde (images signalees a Elegant ; il en
;  compose au plus une par tour de sa boucle), « surfacetest : PASS / FAIL ».
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

ST_WIN_W	= 400
ST_WIN_H	= 300
ST_TOP		= 40			; la surface sous la ligne de texte

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'

start:
	mov	x19,x0				; arguments
	bl	kits_open
	mov	x20,5				; secondes par defaut
	mov	x21,WS_SF_XRGB8888
	cbz	x19,.args_done
.arg:	ldrb	w0,[x19]			; mots : un nombre, ou « rgb565 »
	cbz	w0,.args_done
	cmp	w0,' '
	b.ne	.word
	add	x19,x19,1
	b	.arg
.word:	sub	w1,w0,'0'
	cmp	w1,9
	b.hi	.fmt
	mov	x0,x19
	bl	parse_dec
	cmp	x0,1
	csinc	x20,x0,xzr,ge
	b	.skip
.fmt:	cmp	w0,'r'
	b.ne	.skip
	mov	x21,WS_SF_RGB565
.skip:	ldrb	w0,[x19]
	cbz	w0,.args_done
	cmp	w0,' '
	b.eq	.arg
	add	x19,x19,1
	b	.skip
.args_done:
	adr	x0,fmt
	str	x21,[x0]
	adr	x0,ui_win
	bl	ui_init
	mov	x0,ST_WIN_W
	mov	x1,ST_WIN_H
	adr	x2,s_title
	bl	ui_window
	tbnz	x0,63,.nowin
	mov	x0,0				; fond, texte
	mov	x1,0
	mov	x2,ST_WIN_W
	mov	x3,ST_WIN_H
	movl	w4,0x00303846
	bl	ui_fill
	adr	x0,s_label
	adr	x1,fmt
	ldr	x1,[x1]
	cbz	x1,.lab
	adr	x0,s_label565
.lab:	mov	x1,12
	mov	x2,26
	movl	w3,0x00F0F0F0
	mov	x4,15
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	bl	ui_update_all
	mov	x0,16				; phase 0 : 320 x 240
	mov	x1,ST_TOP
	mov	x2,320
	mov	x3,240
	adr	x4,fmt
	ldr	x4,[x4]
	bl	ui_surface
	adr	x1,surf
	str	x0,[x1]
	cbz	x0,.nosurf
	adr	x0,m_start
	bl	puts
	sys	SYS_TIME_MS
	mov	x22,x0				; debut
	mov	x23,0				; t : images
	mov	x24,0				; phase
	mov	x0,1000
	mul	x25,x20,x0			; duree (ms)
.frame:	bl	draw_frame
	adr	x0,surf
	ldr	x0,[x0]
	mov	x1,0
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	ui_surface_damage
	add	x23,x23,1
	mov	x0,0				; evenements (sans attente)
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	.done
	sys	SYS_YIELD
	sys	SYS_TIME_MS
	sub	x26,x0,x22			; ecoule
	cmp	x26,x25
	b.hs	.done
	mov	x0,3				; phase = ecoule * 3 / duree
	mul	x0,x26,x0
	udiv	x0,x0,x25
	cmp	x0,x24
	b.eq	.frame
	mov	x24,x0
	bl	next_phase
	cbz	x0,.movefail
	b	.frame
.done:	sys	SYS_TIME_MS
	sub	x26,x0,x22
	adr	x0,m_frames			; « N images en T ms : F images/s »
	bl	puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_in
	bl	puts
	mov	x0,x26
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	mov	x0,1000				; images/s (une decimale)
	mov	x1,10
	mul	x0,x0,x1
	mul	x0,x23,x0
	cmp	x26,1
	csinc	x26,x26,xzr,ge
	udiv	x0,x0,x26
	mov	x1,10
	udiv	x19,x0,x1
	msub	x20,x19,x1,x0
	mov	x0,x19
	bl	print_dec
	adr	x0,m_dot
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_fps
	bl	puts
	adr	x0,m_pass
	bl	puts
	adr	x0,surf
	ldr	x0,[x0]
	bl	ui_surface_free
	mov	x0,0
	sys	SYS_EXIT
.movefail:
	adr	x0,m_movefail
	b	.fail
.nosurf:
	adr	x0,m_nosurf
	b	.fail
.nowin:	adr	x0,m_nowin
.fail:	bl	puts
	adr	x0,m_fail
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x24 = phase (1, 2) : la zone change (1 : deplacee et reduite, meme tampon ;
; 2 : agrandie, tampon neuf) ; le fond du contenu redessine -> x0 = 1 si reussi
next_phase:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,0				; fond (la zone d'avant ne doit pas rester)
	mov	x1,ST_TOP
	mov	x2,ST_WIN_W
	mov	x3,ST_WIN_H-ST_TOP
	movl	w4,0x00303846
	bl	ui_fill
	mov	x0,0
	mov	x1,ST_TOP
	mov	x2,ST_WIN_W
	mov	x3,ST_WIN_H-ST_TOP
	bl	ui_update
	adr	x0,surf
	ldr	x0,[x0]
	cmp	x24,1
	b.ne	.p2
	mov	x1,56
	mov	x2,ST_TOP+30
	mov	x3,240
	mov	x4,180
	bl	ui_surface_move
	adr	x1,m_phase1
	b	.rep
.p2:	mov	x1,8
	mov	x2,ST_TOP
	mov	x3,384
	mov	x4,ST_WIN_H-ST_TOP-8
	bl	ui_surface_move
	adr	x1,m_phase2
.rep:	mov	x19,x0
	mov	x0,x1
	bl	puts
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; une image dans la surface : degrade qui defile (t = x23), barre blanche
draw_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,surf
	ldr	x0,[x0]
	bl	ui_surface_pixels		; x0 pixels, x1 pas, x2 l, x3 h
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	lsl	x9,x23,2			; barre : x = 4t mod l
	udiv	x10,x9,x21
	msub	x9,x10,x21,x9
	adr	x0,fmt
	ldr	x8,[x0]
	mov	x10,0				; y
.y:	madd	x11,x10,x20,x19			; ligne
	add	w12,w10,w23,lsl 1		; vert = y + 2t
	and	w12,w12,255
	mov	x13,0				; x
.x:	add	w14,w13,w23			; rouge = x + t
	and	w14,w14,255
	eor	w15,w13,w10			; bleu = (x ^ y) + 3t
	add	w15,w15,w23
	add	w15,w15,w23,lsl 1
	and	w15,w15,255
	sub	x16,x13,x9			; dans la barre (6 px) ?
	cmp	x16,6
	b.hs	.col
	mov	w14,255
	mov	w12,255
	mov	w15,255
.col:	cbnz	x8,.c565
	lsl	w16,w14,16
	orr	w16,w16,w12,lsl 8
	orr	w16,w16,w15
	str	w16,[x11],4
	b	.nx
.c565:	lsr	w16,w14,3
	lsl	w16,w16,11
	lsr	w17,w12,2
	orr	w16,w16,w17,lsl 5
	orr	w16,w16,w15,lsr 3
	strh	w16,[x11],2
.nx:	sub	x16,x13,x9			; (barre : vert garde par le pixel suivant)
	cmp	x16,6
	b.lo	.keep
	b	.inc
.keep:	add	w12,w10,w23,lsl 1
	and	w12,w12,255
.inc:	add	x13,x13,1
	cmp	x13,x21
	b.lo	.x
	add	x10,x10,1
	cmp	x10,x22
	b.lo	.y
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

segment readable writeable
kit_data
		align 8
surf		dd 0
fmt		dd 0
s_title		db 'Surface attach',0xC3,0xA9,'e',0
s_label		db 'Surface attach',0xC3,0xA9,'e XRGB8888 (Elegant)',0
s_label565	db 'Surface attach',0xC3,0xA9,'e RGB565 (Elegant)',0
m_start		db 'surfacetest : surface attach',0xC3,0xA9,'e, animation',10,0
m_phase1	db 'surfacetest : zone d',0xC3,0xA9,'plac',0xC3,0xA9,'e (m',0xC3,0xAA,'me tampon)',10,0
m_phase2	db 'surfacetest : zone agrandie (tampon neuf)',10,0
m_frames	db 'surfacetest : ',0
m_in		db ' images en ',0
m_ms		db ' ms : ',0
m_dot		db ',',0
m_fps		db ' images/s',10,0
m_pass		db 'surfacetest : PASS',10,0
m_fail		db 'surfacetest : FAIL',10,0
m_nowin		db 'surfacetest : pas de fen',0xC3,0xAA,'tre (Elegant ?)',10,0
m_nosurf	db 'surfacetest : ui_surface a ',0xC3,0xA9,'chou',0xC3,0xA9,' (Elegant ou UIKit trop anciens ?)',10,0
m_movefail	db 'surfacetest : ui_surface_move a ',0xC3,0xA9,'chou',0xC3,0xA9,10,0
		align 8
ui_data
