; ============================================================================
;  mandelbrot - Fractales : plus que le fractal browser d'Onyx, un explorateur
;  en assembleur : Mandelbrot, Julia (choix du point a la souris), Burning Ship,
;  Tricorn, Multibrot 3 a 5, Newton, Phoenix, Lyapunov (suite AB a soi),
;  attracteurs de Clifford et de De Jong, Buddhabrot.
;
;  Les calculs (kernels.inc) sont en NEON, sur tous les coeurs (les threads
;  worker). Le rendu est progressif : un point sur 8, puis 2, puis tous ; un
;  changement de vue arrete les threads (g_gen) et repart. Chaque point garde sa
;  valeur (g_mu) : changer de palette ou la faire tourner ne recalcule rien.
;  Les attracteurs et le Buddhabrot comptent les points par pixel dans g_mu, par
;  lots, et l'image se rehausse tant qu'ils tournent.
;
;  Molette ou doigt vertical : zoom, sur le point vise. Glisser : deplacer.
;  Double toucher : zoom x 2 ; bouton droit : recule. Touches : fleches, + -,
;  R reinitialiser, A animer, P palette suivante, [ ] etirer la palette.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/archivekit/archivekit.inc'

WIN_W		= 800
WIN_H		= 540
TOP		= 44				; la barre du haut
FOOT		= 38				; la barre du bas
LUT_N		= 4096
NTH		= 4				; threads au plus
ROW_MAX		= 8192				; points d'une ligne
ROW_BYTES	= (ROW_MAX+4)*4
ORB_STATE	= 128				; l'etat d'un thread (orbites, hasard)
ORB_BASE	= ORB_STATE*NTH
ORB_MAX		= 20000				; points d'une orbite (Buddhabrot)
ORB_SIZE	= ORB_MAX*16
ROWS_BASE	= ORB_BASE + ORB_SIZE*NTH	; g_work : lignes, puis couleurs
WORK_SIZE	= ROWS_BASE + ROW_BYTES*2*NTH

T_MANDEL	= 0
T_JULIA		= 1
T_SHIP		= 2
T_TRICORN	= 3
T_M3		= 4
T_M4		= 5
T_M5		= 6
T_NEWTON	= 7
T_PHOENIX	= 8
T_LYAP		= 9
T_CLIFFORD	= 10
T_DEJONG	= 11
T_BUDDHA	= 12

C_SMOOTH	= 0				; les facons de colorer
C_NEWTON	= 1
C_LYAP		= 2
C_HIST		= 3

; l'entree de ftab
FT_KERN		= 0
FT_MODE		= 4				; 32 bits
FT_CX		= 8
FT_CY		= 16
FT_SPAN		= 24
FT_ILD		= 32				; 1 / log2 (degre)
FT_ITER		= 40				; iterations de depart
FT_PAL		= 48				; palette de depart (-1 : la courante)
FT_SIZE		= 56

KEY_R		= 0x15
KEY_A		= 0x04
KEY_P		= 0x13
KEY_MINUS	= 0x2D
KEY_PLUS	= 0x2E				; (touche =)
KEY_LB		= 0x2F
KEY_RB		= 0x30
KEY_KPMINUS	= 0x56
KEY_KPPLUS	= 0x57
KEY_RIGHT	= 0x4F
KEY_LEFT	= 0x50
KEY_DOWN	= 0x51
KEY_UP		= 0x52

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'
kit_use '../../kits/archivekit/archivekit.abi'

macro ldv reg, var {
	adr	x16,var
	ldr	reg,[x16]
}
macro stv reg, var {
	adr	x16,var
	str	reg,[x16]
}
macro tok reg, t {
	adr	x16,g_tok
	ldr	x16,[x16]
	ldr	reg,[x16,t]
}
macro ld1r_d n, reg {
	ldr	d#n,[reg]
	dup	v#n#.2d,v#n#.d[0]
}

start:
	mov	x19,x0
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	adr	x0,g_scr			; la taille : ce que l'ecran permet
	bl	ui_screen
	mov	x2,WIN_W
	mov	x3,WIN_H
	tbnz	x0,63,.size
	adr	x0,g_scr
	ldp	x4,x5,[x0]
	ldp	x6,x7,[x0,16]
	sub	x4,x4,16
	sub	x5,x5,x6
	sub	x5,x5,x7
	sub	x5,x5,32
	cmp	x2,x4
	csel	x2,x4,x2,hi
	cmp	x3,x5
	csel	x3,x5,x3,hi
.size:	stp	x2,x3,[sp,-16]!
	mov	x0,-1
	mov	x1,-1
	adr	x4,s_title
	bl	tr4
	mov	x5,WF_RESIZABLE
	bl	ui_window_ex
	ldp	x20,x21,[sp],16
	tbnz	x0,63,quit
	bl	ui_jetons
	stv	x0,g_tok
	mov	x0,640
	mov	x1,400
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	bl	make_widgets
	mov	x0,0				; la memoire des threads
	movl	x1,WORK_SIZE
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,quit
	stv	x0,g_work
	bl	start_threads
	mov	x0,0
	bl	set_palette
	mov	x0,x20
	mov	x1,x21
	bl	on_resize
	mov	x0,T_MANDEL
	bl	set_type
	bl	ui_update_all
.loop:	ldv	x0,g_active
	ldv	x1,g_anim
	orr	x0,x0,x1
	mov	x1,1000
	mov	x2,40
	cmp	x0,0
	csel	x0,x1,x2,eq
	bl	ui_event
	cbz	x0,.tick
	cmp	x0,WEV_CLOSE
	b.eq	quit
	mov	x19,x0
	mov	x0,0
	bl	ui_w_event
	mov	x20,x2
	bl	on_widget
	cbnz	x20,.tick
	cmp	x19,WEV_MOUSE
	b.eq	.mouse
	cmp	x19,WEV_WHEEL
	b.eq	.wheel
	cmp	x19,WEV_KEY
	b.ne	.tick
	bl	on_key
	b	.tick
.mouse:	bl	on_mouse
	b	.tick
.wheel:	bl	on_wheel
.tick:	bl	frame
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; x0, x1 = zone d'un texte de la barre -> rien : les widgets de la fenetre
make_widgets:
	prologue
	mov	x0,10
	mov	x1,8
	mov	x2,158
	mov	x3,28
	adr	x4,s_types
	bl	ui_dropdown
	stv	x0,w_type
	mov	x0,174
	mov	x1,8
	mov	x2,116
	mov	x3,28
	adr	x4,s_pals
	bl	tr4
	bl	ui_dropdown
	stv	x0,w_pal
	mov	x0,296
	mov	x1,8
	mov	x2,120
	mov	x3,28
	adr	x4,s_marks
	bl	tr4
	bl	ui_dropdown
	stv	x0,w_mark
	mov	x0,422
	mov	x1,8
	mov	x2,62
	mov	x3,28
	adr	x4,s_reset
	mov	x5,UB_GHOST
	bl	ui_pushbutton
	stv	x0,w_reset
	mov	x0,486
	mov	x1,8
	mov	x2,68
	mov	x3,28
	adr	x4,s_anim
	mov	x5,UB_GHOST
	bl	ui_pushbutton
	stv	x0,w_anim
	mov	x0,556
	mov	x1,8
	mov	x2,52
	mov	x3,28
	adr	x4,s_png
	mov	x5,UB_GHOST
	bl	ui_pushbutton
	stv	x0,w_png
	mov	x0,0
	mov	x1,0
	mov	x2,100
	mov	x3,22
	mov	x4,0
	mov	x5,ULB_DIM
	bl	ui_label
	stv	x0,w_status
	mov	x0,0
	mov	x1,0
	mov	x2,120
	mov	x3,28
	mov	x4,16
	movl	x5,20000
	mov	x6,200
	bl	ui_numeric
	stv	x0,w_iter
	adr	x1,s_tipiter
	bl	ui_w_tip
	mov	x0,0
	mov	x1,0
	mov	x2,150
	mov	x3,28
	adr	x4,s_pick
	mov	x5,0
	bl	ui_pushbutton
	stv	x0,w_pick
	mov	x0,0
	mov	x1,0
	mov	x2,150
	mov	x3,28
	mov	x4,0
	bl	ui_textbox
	stv	x0,w_seq
	adr	x1,s_tipseq
	bl	ui_w_tip
	mov	x0,0
	mov	x1,0
	mov	x2,150
	mov	x3,28
	adr	x4,s_other
	mov	x5,0
	bl	ui_pushbutton
	stv	x0,w_other
	epilogue

; x0, x1 = taille de la fenetre : tout a sa place, l'image a la nouvelle taille
on_resize:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	stop_job
	sub	x21,x20,TOP+FOOT
	mov	x0,ROW_MAX			; (les tampons des lignes)
	cmp	x19,x0
	csel	x22,x0,x19,hi
	stv	x22,g_vw
	stv	x21,g_vh
	mov	x0,0
	mov	x1,0
	mov	x2,x19
	mov	x3,x20
	tok	w4,TJ_FACE
	bl	ui_fill
	ldv	x0,g_sf
	cbz	x0,.new
	mov	x1,0
	mov	x2,TOP
	mov	x3,x22
	mov	x4,x21
	bl	ui_surface_move
	b	.px
.new:	mov	x0,0
	mov	x1,TOP
	mov	x2,x22
	mov	x3,x21
	mov	x4,WS_SF_XRGB8888
	bl	ui_surface
	stv	x0,g_sf
.px:	ldv	x0,g_sf
	bl	ui_surface_pixels
	stv	x0,g_px
	stv	x1,g_pitch
	ldv	x0,g_mu				; les valeurs
	cbz	x0,.alloc
	ldv	x1,g_musize
	sys	SYS_VM_UNMAP
.alloc:	mul	x1,x22,x21
	lsl	x1,x1,2
	add	x1,x1,4095
	and	x1,x1,-4096
	stv	x1,g_musize
	mov	x0,0
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,quit
	stv	x0,g_mu
	ldv	x0,w_status
	mov	x1,12
	sub	x2,x20,FOOT-8
	sub	x3,x19,12+120+8+150+16
	mov	x4,22
	bl	ui_w_move
	ldv	x0,w_iter
	sub	x1,x19,128
	sub	x2,x20,FOOT-5
	mov	x3,120
	mov	x4,28
	bl	ui_w_move
	sub	x1,x19,128+158
	sub	x2,x20,FOOT-5
	mov	x3,150
	mov	x4,28
	ldv	x0,w_pick
	bl	ui_w_move
	sub	x1,x19,128+158
	sub	x2,x20,FOOT-5
	mov	x3,150
	mov	x4,28
	ldv	x0,w_seq
	bl	ui_w_move
	sub	x1,x19,128+158
	sub	x2,x20,FOOT-5
	mov	x3,150
	mov	x4,28
	ldv	x0,w_other
	bl	ui_w_move
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	ldv	x0,g_mark_ok
	cbz	x0,.out
	bl	restart
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1 = code
on_widget:
	cbz	x0,.ret
	cbz	x1,.ret
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldv	x2,w_type
	cmp	x0,x2
	b.ne	.n1
	cmp	x1,UN_CHANGE
	b.ne	.out
	bl	ui_w_get_value
	tbnz	x0,63,.out
	bl	set_type
	b	.out
.n1:	ldv	x2,w_pal
	cmp	x0,x2
	b.ne	.n2
	cmp	x1,UN_CHANGE
	b.ne	.out
	bl	ui_w_get_value
	tbnz	x0,63,.out
	bl	set_palette
	bl	repaint
	b	.out
.n2:	ldv	x2,w_mark
	cmp	x0,x2
	b.ne	.n3
	cmp	x1,UN_CHANGE
	b.ne	.out
	bl	ui_w_get_value
	cmp	x0,0
	b.le	.out
	bl	go_mark
	b	.out
.n3:	ldv	x2,w_iter
	cmp	x0,x2
	b.ne	.n4
	cmp	x1,UN_CHANGE
	b.ne	.out
	bl	ui_w_get_value
	stv	x0,g_base
	bl	restart
	b	.out
.n4:	ldv	x2,w_seq
	cmp	x0,x2
	b.ne	.n5
	cmp	x1,UN_CHANGE
	b.ne	.out
	bl	read_seq
	b	.out
.n5:	cmp	x1,UN_ACTIVATE
	b.ne	.out
	ldv	x2,w_reset
	cmp	x0,x2
	b.ne	.n6
	bl	reset_view
	b	.out
.n6:	ldv	x2,w_anim
	cmp	x0,x2
	b.ne	.n7
	ldv	x0,g_anim
	eor	x0,x0,1
	stv	x0,g_anim
	ldv	x0,w_anim
	ldv	x1,g_anim
	cmp	x1,0
	mov	x1,UB_GHOST
	mov	x2,UB_DEFAULT
	csel	x1,x1,x2,eq
	bl	ui_w_style
	ldv	x0,w_anim
	bl	ui_w_draw
	b	.out
.n7:	ldv	x2,w_png
	cmp	x0,x2
	b.ne	.n8
	bl	save_png
	b	.out
.n8:	ldv	x2,w_pick
	cmp	x0,x2
	b.ne	.n9
	bl	pick_toggle
	b	.out
.n9:	ldv	x2,w_other
	cmp	x0,x2
	b.ne	.out
	bl	other_params
.out:	ldp	x19,x20,[sp],16
	epilogue
.ret:	ret

on_key:
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.ret
	ldr	w1,[x0,EV_CODE]
	prologue
	cmp	w1,KEY_R
	b.eq	.r
	cmp	w1,KEY_A
	b.eq	.a
	cmp	w1,KEY_P
	b.eq	.pal
	cmp	w1,KEY_LB
	b.eq	.lb
	cmp	w1,KEY_RB
	b.eq	.rb
	cmp	w1,KEY_PLUS
	b.eq	.in
	cmp	w1,KEY_KPPLUS
	b.eq	.in
	cmp	w1,KEY_MINUS
	b.eq	.outz
	cmp	w1,KEY_KPMINUS
	b.eq	.outz
	mov	x2,0				; dx, dy en huitiemes de la largeur
	mov	x3,0
	cmp	w1,KEY_LEFT
	b.ne	.k1
	mov	x2,-1
.k1:	cmp	w1,KEY_RIGHT
	b.ne	.k2
	mov	x2,1
.k2:	cmp	w1,KEY_UP
	b.ne	.k3
	mov	x3,-1
.k3:	cmp	w1,KEY_DOWN
	b.ne	.k4
	mov	x3,1
.k4:	orr	x4,x2,x3
	cbz	x4,.out
	scvtf	d0,x2
	scvtf	d1,x3
	fmov	d2,0.125
	adr	x0,g_cx
	ldr	d3,[x0,16]
	fmul	d2,d2,d3
	fmul	d0,d0,d2
	fmul	d1,d1,d2
	ldp	d3,d4,[x0]
	fadd	d3,d3,d0
	fadd	d4,d4,d1
	stp	d3,d4,[x0]
	bl	restart
	b	.out
.r:	bl	reset_view
	b	.out
.a:	ldv	x0,w_anim
	mov	x1,UN_ACTIVATE
	bl	on_widget
	b	.out
.pal:	ldv	x0,g_palnum
	add	x0,x0,1
	cmp	x0,NPAL
	csel	x0,xzr,x0,hs
	str	x0,[sp,-16]!
	bl	set_palette
	ldr	x1,[sp],16
	ldv	x0,w_pal
	bl	ui_w_set_value
	ldv	x0,w_pal
	bl	ui_w_draw
	bl	repaint
	b	.out
.lb:	adr	x0,g_scale
	ldr	s0,[x0]
	fmov	s1,1.5
	fdiv	s0,s0,s1
	str	s0,[x0]
	bl	repaint
	b	.out
.rb:	adr	x0,g_scale
	ldr	s0,[x0]
	fmov	s1,1.5
	fmul	s0,s0,s1
	str	s0,[x0]
	bl	repaint
	b	.out
.in:	ldv	x0,g_vw
	lsr	x0,x0,1
	ldv	x1,g_vh
	lsr	x1,x1,1
	fmov	d0,0.5
	bl	zoom_at
	b	.out
.outz:	ldv	x0,g_vw
	lsr	x0,x0,1
	ldv	x1,g_vh
	lsr	x1,x1,1
	fmov	d0,2.0
	bl	zoom_at
.out:	epilogue
.ret:	ret

; un evenement souris : glisser, double toucher, bouton droit, choix de c
on_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	ldrsw	x19,[x0,EV_X]
	ldrsw	x20,[x0,EV_Y]
	sub	x20,x20,TOP
	stv	x19,g_mx
	stv	x20,g_my
	cmp	w1,1
	b.eq	.down
	cmp	w1,2
	b.eq	.up
	ldv	x2,g_drag			; deplacement
	cbz	x2,.out
	ldv	x3,g_lx
	ldv	x4,g_ly
	sub	x3,x19,x3
	sub	x4,x20,x4
	orr	x5,x3,x4
	cbz	x5,.out
	ldv	x5,g_moved
	cbnz	x5,.pan
	add	x6,x3,3				; (un doigt bouge toujours un peu)
	cmp	x6,6
	b.hi	.mv
	add	x6,x4,3
	cmp	x6,6
	b.ls	.out
.mv:	mov	x5,1
	stv	x5,g_moved
.pan:	stv	x19,g_lx
	stv	x20,g_ly
	scvtf	d0,x3
	scvtf	d1,x4
	adr	x0,g_dx
	ldr	d2,[x0]
	fmul	d0,d0,d2
	fmul	d1,d1,d2
	adr	x0,g_cx
	ldp	d3,d4,[x0]
	fsub	d3,d3,d0
	fsub	d4,d4,d1
	stp	d3,d4,[x0]
	bl	restart
	b	.out
.down:	tbnz	x20,63,.out
	ldv	x1,g_vh
	cmp	x20,x1
	b.ge	.out
	ldr	w1,[x0,EV_CODE]
	tbnz	w1,1,.right
	mov	x1,1
	stv	x1,g_drag
	stv	xzr,g_moved
	stv	x19,g_lx
	stv	x20,g_ly
	b	.out
.right:	mov	x0,x19
	mov	x1,x20
	fmov	d0,2.0
	bl	zoom_at
	b	.out
.up:	ldv	x1,g_drag
	cbz	x1,.out
	stv	xzr,g_drag
	ldv	x1,g_moved
	cbnz	x1,.out
	ldv	x1,g_picking			; choix de c
	cbz	x1,.dbl
	bl	pick_at
	b	.out
.dbl:	sys	SYS_TIME_MS			; deux touchers : zoom x 2
	ldv	x1,g_tap
	stv	x0,g_tap
	sub	x0,x0,x1
	cmp	x0,400
	b.hs	.out
	stv	xzr,g_tap
	mov	x0,x19
	mov	x1,x20
	fmov	d0,0.5
	bl	zoom_at
.out:	ldp	x19,x20,[sp],16
	epilogue

on_wheel:
	prologue
	adr	x0,ui_ev
	ldrsw	x1,[x0,EV_Y]
	cbz	x1,.out
	adr	x0,k_wheel			; 0.8 par cran
	ldp	d0,d1,[x0]
	cmp	x1,0
	b.gt	.w
	fmov	d0,d1
	neg	x1,x1
.w:	fmov	d1,1.0
.m:	fmul	d1,d1,d0
	subs	x1,x1,1
	b.ne	.m
	fmov	d0,d1
	ldv	x0,g_mx
	ldv	x1,g_my
	bl	zoom_at
.out:	epilogue

; x0, x1 = pixel, d0 = facteur : ce point reste sous le doigt
zoom_at:
	prologue
	adr	x2,g_cx
	ldp	d1,d2,[x2]
	ldr	d3,[x2,16]			; span
	fmul	d4,d3,d0
	adr	x3,k_min
	ldr	d5,[x3]
	fcmp	d4,d5
	b.lt	.out
	ldr	d5,[x3,8]
	fcmp	d4,d5
	b.gt	.out
	adr	x3,g_dx
	ldr	d5,[x3]
	ldv	x3,g_vw
	lsr	x3,x3,1
	sub	x0,x0,x3
	ldv	x3,g_vh
	lsr	x3,x3,1
	sub	x1,x1,x3
	scvtf	d6,x0
	scvtf	d7,x1
	fmul	d6,d6,d5			; ecart au centre
	fmul	d7,d7,d5
	fmov	d5,1.0
	fsub	d5,d5,d0
	fmul	d6,d6,d5
	fmul	d7,d7,d5
	fadd	d1,d1,d6
	fadd	d2,d2,d7
	stp	d1,d2,[x2]
	str	d4,[x2,16]
	bl	restart
.out:	epilogue

; --- les types, les vues ---------------------------------------------------------
; x0 = type : sa vue de depart
set_type:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	stop_job
	stv	x19,g_type
	stv	xzr,g_picking
	cmp	x19,T_JULIA			; Julia : son c de depart
	b.ne	.a
	adr	x0,k_jul
	ldp	d0,d1,[x0]
	adr	x0,g_cr
	stp	d0,d1,[x0]
.a:	adr	x0,ftab				; sa palette
	mov	x1,FT_SIZE
	madd	x0,x19,x1,x0
	ldr	x19,[x0,FT_PAL]
	tbnz	x19,63,.b
	mov	x0,x19
	bl	set_palette
	ldv	x0,w_pal
	mov	x1,x19
	bl	ui_w_set_value
	ldv	x0,w_pal
	bl	ui_w_draw
.b:	bl	apply_type
	bl	reset_view
	ldr	x19,[sp],16
	epilogue

; les reglages du type g_type (kernel, couleurs, iterations) ; les widgets du bas
apply_type:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x19,g_type
	adr	x20,ftab
	mov	x1,FT_SIZE
	madd	x20,x19,x1,x20
	ldrsw	x1,[x20,FT_KERN]
	adr	x2,ftab
	add	x1,x2,x1
	stv	x1,g_kern
	ldr	w1,[x20,FT_MODE]
	stv	x1,g_mode
	ldr	d0,[x20,FT_ILD]
	adr	x1,g_ild
	str	d0,[x1]
	ldr	x1,[x20,FT_ITER]
	stv	x1,g_base
	ldv	x0,w_iter
	bl	ui_w_set_value
	cmp	x19,T_PHOENIX			; les constantes de Phoenix
	b.ne	.np
	adr	x0,k_phx
	ldp	d0,d1,[x0]
	adr	x0,g_cr
	stp	d0,d1,[x0]
.np:	cmp	x19,T_JULIA
	b.eq	.pick
	cmp	x19,T_MANDEL
	b.ne	.nopick
.pick:	mov	x1,1
	b	.p1
.nopick:
	mov	x1,0
.p1:	ldv	x0,w_pick
	bl	ui_w_show
	ldv	x0,w_seq
	cmp	x19,T_LYAP
	cset	x1,eq
	bl	ui_w_show
	ldv	x0,w_other
	sub	x1,x19,T_CLIFFORD
	cmp	x1,1
	cset	x1,ls
	bl	ui_w_show
	cmp	x19,T_LYAP
	b.ne	.ns
	ldv	x0,w_seq
	adr	x1,g_seqtext
	bl	ui_w_set_text
.ns:	sub	x1,x19,T_CLIFFORD
	cmp	x1,1
	b.hi	.np2
	bl	load_params			; les constantes de l'attracteur
.np2:	adr	x2,ui_w
	ldp	x2,x3,[x2]
	mov	x0,0
	sub	x1,x3,FOOT
	mov	x3,FOOT
	tok	w4,TJ_FACE
	bl	ui_fill
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	ldp	x19,x20,[sp],16
	epilogue

; retour a la vue de depart du type
reset_view:
	prologue
	ldv	x0,g_type
	adr	x1,ftab
	mov	x2,FT_SIZE
	madd	x1,x0,x2,x1
	ldp	d0,d1,[x1,FT_CX]
	ldr	d2,[x1,FT_SPAN]
	adr	x0,g_cx
	stp	d0,d1,[x0]
	str	d2,[x0,16]
	adr	x0,g_span0
	str	d2,[x0]
	bl	default_scale
	mov	x0,1
	stv	x0,g_mark_ok
	bl	attr_fit
	bl	restart
	epilogue

default_scale:
	adr	x0,k_scale
	ldv	x1,g_mode
	ldr	s0,[x0,x1,lsl 2]
	adr	x0,g_scale
	str	s0,[x0]
	ret

; x0 = signet (1..) : sa vue
go_mark:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	x0,x0,1
	adr	x19,marks
	mov	x1,MK_SIZE
	madd	x19,x0,x1,x19
	bl	stop_job
	ldr	x20,[x19,MK_TYPE]
	stv	x20,g_type
	stv	xzr,g_picking
	bl	apply_type
	ldv	x0,w_type
	mov	x1,x20
	bl	ui_w_set_value
	ldv	x0,w_type
	bl	ui_w_draw
	ldp	d0,d1,[x19,MK_CX]
	ldr	d2,[x19,MK_SPAN]
	adr	x0,g_cx
	stp	d0,d1,[x0]
	str	d2,[x0,16]
	adr	x1,ftab
	mov	x2,FT_SIZE
	madd	x1,x20,x2,x1
	ldr	d2,[x1,FT_SPAN]
	adr	x0,g_span0
	str	d2,[x0]
	cmp	x20,T_JULIA
	b.ne	.nj
	ldp	d0,d1,[x19,MK_P0]
	adr	x0,g_cr
	stp	d0,d1,[x0]
.nj:	sub	x0,x20,T_CLIFFORD
	cmp	x0,1
	b.hi	.np
	ldp	d0,d1,[x19,MK_P0]
	ldp	d2,d3,[x19,MK_P2]
	adr	x0,g_pa
	stp	d0,d1,[x0]
	stp	d2,d3,[x0,16]
.np:	cmp	x20,T_LYAP
	b.ne	.ns
	add	x1,x19,MK_P0
	adr	x0,g_seqtext
	mov	x2,16
	bl	mem_move
	ldv	x0,w_seq
	adr	x1,g_seqtext
	bl	ui_w_set_text
	bl	parse_seq
.ns:	ldr	x0,[x19,MK_PAL]
	bl	set_palette
	ldv	x0,w_pal
	ldv	x1,g_palnum
	bl	ui_w_set_value
	ldv	x0,w_pal
	bl	ui_w_draw
	bl	default_scale
	bl	attr_fit
	ldv	x0,w_mark			; le menu revient a « Signets »
	mov	x1,0
	bl	ui_w_set_value
	ldv	x0,w_mark
	bl	ui_w_draw
	bl	restart
	ldp	x19,x20,[sp],16
	epilogue

; le bouton du bas sous Mandelbrot, Julia : choisir c
pick_toggle:
	prologue
	ldv	x0,g_type
	cmp	x0,T_MANDEL
	b.eq	.m
	mov	x0,T_MANDEL			; Julia : on repasse par Mandelbrot
	stv	x0,g_type
	ldv	x0,w_type
	mov	x1,T_MANDEL
	bl	ui_w_set_value
	ldv	x0,w_type
	bl	ui_w_draw
	bl	stop_job
	bl	apply_type
	bl	reset_view
.m:	mov	x0,1
	stv	x0,g_picking
	bl	status
	epilogue

; (x19, x20 : le pixel) c = ce point : on passe en Julia
pick_at:
	prologue
	scvtf	d0,x19
	scvtf	d1,x20
	adr	x0,g_x0
	ldp	d2,d3,[x0]
	ldr	d4,[x0,16]
	fmul	d0,d0,d4
	fmul	d1,d1,d4
	fadd	d0,d0,d2
	fadd	d1,d1,d3
	adr	x0,g_cr
	stp	d0,d1,[x0]
	stv	xzr,g_picking
	bl	stop_job
	mov	x0,T_JULIA
	stv	x0,g_type
	bl	apply_type
	ldv	x0,w_type
	mov	x1,T_JULIA
	bl	ui_w_set_value
	ldv	x0,w_type
	bl	ui_w_draw
	bl	reset_view
	epilogue

; le bouton « Autres valeurs » : le jeu de constantes suivant
other_params:
	prologue
	ldv	x0,g_pnum
	add	x0,x0,1
	stv	x0,g_pnum
	bl	load_params
	bl	reset_view
	epilogue

; g_pnum (modulo le nombre de jeux du type) -> g_pa..g_pd
load_params:
	ldv	x0,g_type
	cmp	x0,T_CLIFFORD
	adr	x1,kp_cliff
	adr	x2,kp_dejong
	csel	x1,x1,x2,eq
	ldv	x0,g_pnum
	and	x0,x0,3
	add	x1,x1,x0,lsl 5
	ldp	d0,d1,[x1]
	ldp	d2,d3,[x1,16]
	adr	x0,g_pa
	stp	d0,d1,[x0]
	stp	d2,d3,[x0,16]
	ret

; le champ de la suite AB a change
read_seq:
	prologue
	ldv	x0,w_seq
	bl	ui_w_get_text
	mov	x1,x0
	adr	x0,g_seqtext
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	cbz	w3,.d
	add	x2,x2,1
	cmp	x2,62
	b.lo	.c
	strb	wzr,[x0,x2]
.d:	bl	parse_seq
	bl	restart
	epilogue

; g_seqtext -> g_seq (0 : a, 1 : b), g_seqn ; sans lettre : AB
parse_seq:
	adr	x0,g_seqtext
	adr	x1,g_seq
	mov	x2,0
	mov	x3,0
.c:	ldrb	w4,[x0,x2]
	cbz	w4,.e
	add	x2,x2,1
	orr	w4,w4,0x20
	sub	w4,w4,'a'
	cmp	w4,1
	b.hi	.c
	strb	w4,[x1,x3]
	add	x3,x3,1
	b	.c
.e:	cbnz	x3,.s
	strb	wzr,[x1]
	mov	w4,1
	strb	w4,[x1,1]
	mov	x3,2
.s:	stv	x3,g_seqn
	ret

; --- les threads ------------------------------------------------------------------
start_threads:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,g_sys
	sys	SYS_SYSINFO
	adr	x0,g_sys
	ldr	x20,[x0]
	cmp	x20,1
	mov	x0,1
	csel	x20,x0,x20,lo
	cmp	x20,NTH
	mov	x0,NTH
	csel	x20,x0,x20,hi
	mov	x19,0
.t:	cmp	x19,x20
	b.hs	.out
	ldv	x0,g_work			; l'etat : une orbite, le hasard
	mov	x1,ORB_STATE
	madd	x1,x19,x1,x0
	sys	SYS_TIME_MS
	add	x0,x0,x19,lsl 32
	add	x0,x0,1
	movl	x2,0x9E3779B97F4A7C15
	mul	x0,x0,x2
	ldv	x1,g_work
	mov	x2,ORB_STATE
	madd	x1,x19,x2,x1
	str	x0,[x1,64]
	add	x2,x19,1			; deux orbites de depart
	scvtf	d0,x2
	adr	x2,k_orb
	ldp	d1,d2,[x2]
	fmul	d3,d0,d1
	fmul	d4,d0,d2
	fneg	d5,d3
	fneg	d6,d4
	stp	d3,d5,[x1]
	stp	d4,d6,[x1,16]
	movl	x0,65536
	bl	malloc
	cbz	x0,.out
	movl	x1,65536
	add	x1,x0,x1
	adr	x0,worker
	mov	x2,x19
	sys	SYS_THREAD_CREATE
	add	x19,x19,1
	b	.t
.out:	ldp	x19,x20,[sp],16
	epilogue

; un thread : x0 = son numero
worker:
	mov	x19,x0
.lock:	adr	x0,g_mtx
	bl	mtx_lock
.chk:	ldv	x0,g_active
	cbz	x0,.sleep
	ldv	x1,g_row
	ldv	x2,g_nrows
	cmp	x1,x2
	b.lo	.take
.sleep:	adr	x0,g_cnd
	adr	x1,g_mtx
	bl	cnd_wait
	b	.chk
.take:	add	x2,x1,1
	stv	x2,g_row
	ldv	x2,g_inflight
	add	x2,x2,1
	stv	x2,g_inflight
	mov	x20,x1
	ldv	x21,g_step
	ldv	x22,g_gen
	adr	x0,g_mtx
	bl	mtx_unlock
	ldv	x0,g_mode
	cmp	x0,C_HIST
	b.eq	.hist
	mul	x0,x20,x21
	mov	x1,x21
	bl	do_row
	b	.done
.hist:	mov	x0,x19
	ldv	x1,g_kern
	blr	x1
.done:	adr	x0,g_mtx
	bl	mtx_lock
	ldv	x2,g_inflight
	sub	x2,x2,1
	stv	x2,g_inflight
	ldv	x0,g_gen
	cmp	x0,x22
	b.ne	.other
	cbnz	x2,.chk
	ldv	x1,g_row
	ldv	x2,g_nrows
	cmp	x1,x2
	b.lo	.chk
	ldv	x1,g_step			; passe finie : 8, 2, 1
	cmp	x1,1
	b.eq	.end
	cmp	x1,2
	mov	x2,1
	mov	x3,2
	csel	x1,x2,x3,eq
	stv	x1,g_step
	stv	xzr,g_row
	bl	pass_rows
	adr	x0,g_cnd
	bl	cnd_broadcast
	b	.chk
.end:	stv	xzr,g_active
	mov	x0,1
	stv	x0,g_finished
	sys	SYS_TIME_MS
	stv	x0,g_tend
.other:	adr	x0,g_cnd			; (stop_job attend)
	bl	cnd_broadcast
	b	.chk

; (mutex pris) g_nrows selon g_step
pass_rows:
	ldv	x0,g_vh
	ldv	x1,g_step
	add	x0,x0,x1
	sub	x0,x0,1
	udiv	x0,x0,x1
	stv	x0,g_nrows
	ret

; x0 = ligne de l'image, x1 = pas ; x19 = numero du thread
do_row:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	ldv	x22,g_work
	movl	x0,ROWS_BASE
	add	x22,x22,x0
	movl	x0,ROW_BYTES*2
	madd	x22,x19,x0,x22			; les valeurs de la ligne
	ldv	x0,g_vw
	add	x1,x0,x21
	sub	x1,x1,1
	udiv	x23,x1,x21			; points
	scvtf	d1,x20
	scvtf	d2,x21
	adr	x0,g_x0
	ldp	d0,d3,[x0]
	ldr	d4,[x0,16]
	fmul	d1,d1,d4
	fadd	d1,d1,d3
	fmul	d2,d2,d4
	mov	x0,x22
	mov	x1,x23
	ldv	x2,g_maxit
	ldv	x3,g_kern
	blr	x3
	mov	x0,x22				; les couleurs
	mov	x1,x23
	movl	x2,ROW_BYTES
	add	x2,x22,x2
	bl	color_line
	movl	x2,ROW_BYTES
	add	x2,x22,x2			; les couleurs
	ldv	x3,g_vw
	ldv	x4,g_vh
	mov	x5,0				; lignes faites
	ldv	x6,g_mu
	ldv	x7,g_px
	ldv	x8,g_pitch
	mul	x9,x20,x3
	add	x6,x6,x9,lsl 2			; g_mu de cette ligne
	madd	x7,x20,x8,x7
.y:	cmp	x5,x21
	b.hs	.out
	add	x9,x20,x5
	cmp	x9,x4
	b.hs	.out
	mov	x9,0				; colonne
.x:	cmp	x9,x3
	b.hs	.nl
	udiv	x10,x9,x21
	ldr	w11,[x22,x10,lsl 2]
	ldr	w12,[x2,x10,lsl 2]
	mov	x13,x21
	sub	x14,x3,x9
	cmp	x13,x14
	csel	x13,x14,x13,hi
.r:	str	w11,[x6,x9,lsl 2]
	str	w12,[x7,x9,lsl 2]
	add	x9,x9,1
	subs	x13,x13,1
	b.ne	.r
	b	.x
.nl:	add	x6,x6,x3,lsl 2
	add	x7,x7,x8
	add	x5,x5,1
	b	.y
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; arrete le travail en cours : apres, aucun thread ne touche a l'image
stop_job:
	prologue
	adr	x0,g_mtx
	bl	mtx_lock
	stv	xzr,g_active
	ldv	x0,g_gen
	add	x0,x0,1
	stv	x0,g_gen
.w:	ldv	x0,g_inflight
	cbz	x0,.d
	adr	x0,g_cnd
	adr	x1,g_mtx
	bl	cnd_wait
	b	.w
.d:	adr	x0,g_mtx
	bl	mtx_unlock
	epilogue

; repart : la vue g_cx, g_cy, g_span
restart:
	prologue
	bl	stop_job
	adr	x0,g_cx				; la vue en pixels
	ldp	d0,d1,[x0]
	ldr	d2,[x0,16]
	ldv	x1,g_vw
	ucvtf	d3,x1
	fdiv	d4,d2,d3			; dx
	adr	x0,g_x0
	ldv	x1,g_vh
	ucvtf	d5,x1
	fmov	d6,0.5
	fmul	d3,d3,d6
	fmul	d3,d3,d4
	fmul	d5,d5,d6
	fmul	d5,d5,d4
	fsub	d0,d0,d3
	fsub	d1,d1,d5
	stp	d0,d1,[x0]
	str	d4,[x0,16]
	adr	x0,g_span0			; les iterations suivent le zoom
	ldr	d0,[x0]
	fdiv	d0,d0,d2
	fmov	d1,1.0
	fcmp	d0,d1
	b.le	.z0
	bl	log2d
	b	.z1
.z0:	fmov	d0,xzr
.z1:	adr	x0,g_zoom
	str	d0,[x0]
	ldv	x1,g_base
	ldv	x2,g_mode
	cmp	x2,C_SMOOTH
	b.ne	.it
	ucvtf	d1,x1				; base + 1/5 par doublement
	fmul	d0,d0,d1
	fmov	d2,5.0
	fdiv	d0,d0,d2
	fadd	d0,d0,d1
	fcvtzu	x1,d0
.it:	mov	x0,ORB_MAX-2
	cmp	x1,x0
	csel	x1,x0,x1,hi
	stv	x1,g_maxit
	ldv	x0,g_mode
	cmp	x0,C_HIST
	b.eq	.hist
	mov	x0,8
	stv	x0,g_step
	bl	pass_rows
	b	.go
.hist:	ldv	x0,g_mu				; on recompte
	ldv	x1,g_musize
.z:	stp	xzr,xzr,[x0],16
	subs	x1,x1,16
	b.hi	.z
	mov	x0,1
	stv	x0,g_step
	ldv	x0,g_type
	cmp	x0,T_BUDDHA
	movl	x1,4000
	movl	x2,60000
	csel	x1,x2,x1,eq
	stv	x1,g_nrows
.go:	stv	xzr,g_row
	stv	xzr,g_finished
	sys	SYS_TIME_MS
	stv	x0,g_t0
	adr	x0,g_mtx
	bl	mtx_lock
	mov	x0,1
	stv	x0,g_active
	adr	x0,g_cnd
	bl	cnd_broadcast
	adr	x0,g_mtx
	bl	mtx_unlock
	bl	status
	epilogue

; une image de la boucle (au plus une toutes les 30 ms) : les couleurs qui
; tournent, les comptes, l'avancement
frame:
	prologue
	sys	SYS_TIME_MS
	ldv	x1,g_lastf
	sub	x1,x0,x1
	cmp	x1,30
	b.lo	.flush
	stv	x0,g_lastf
	ldv	x1,g_anim
	cbz	x1,.noa
	adr	x0,g_offset
	ldr	x1,[x0]
	add	x1,x1,12
	str	x1,[x0]
	bl	repaint_now
	b	.show
.noa:	ldv	x1,g_mode
	cmp	x1,C_HIST
	b.ne	.show
	ldv	x1,g_active			; les comptes : l'image se rehausse
	ldv	x2,g_finished
	orr	x1,x1,x2
	cbz	x1,.show
	ldv	x1,g_lasth
	sub	x1,x0,x1
	cmp	x1,250
	b.lo	.show
	stv	x0,g_lasth
	bl	repaint_now
.show:	ldv	x0,g_active
	ldv	x1,g_finished
	ldv	x2,g_anim
	orr	x0,x0,x1
	orr	x0,x0,x2
	cbz	x0,.flush
	ldv	x0,g_sf
	mov	x1,0
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	ui_surface_damage
	ldv	x0,g_finished
	cbz	x0,.prog
	stv	xzr,g_finished
	ldv	x0,g_mode			; (la derniere image des comptes)
	cmp	x0,C_HIST
	b.ne	.st1
	bl	repaint
.st1:	bl	status
	b	.flush
.prog:	sys	SYS_TIME_MS			; (le temps qui passe)
	ldv	x1,g_lasts
	sub	x1,x0,x1
	cmp	x1,500
	b.lo	.flush
	stv	x0,g_lasts
	ldv	x0,g_active
	cbz	x0,.flush
	bl	status
.flush:	bl	ui_view_flush
	epilogue

; toutes les couleurs, puis a l'ecran
repaint:
	prologue
	bl	repaint_now
	ldv	x0,g_sf
	mov	x1,0
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	ui_surface_damage
	epilogue

repaint_now:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x0,g_mode
	cmp	x0,C_HIST
	b.ne	.lines
	bl	color_hist
	b	.out
.lines:	ldv	x19,g_mu
	ldv	x20,g_px
	ldv	x21,g_vh
	ldv	x22,g_vw
.y:	cbz	x21,.out
	mov	x0,x19
	mov	x1,x22
	mov	x2,x20
	bl	color_line
	add	x19,x19,x22,lsl 2
	ldv	x0,g_pitch
	add	x20,x20,x0
	sub	x21,x21,1
	b	.y
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeurs (32 bits flottants), x1 = points, x2 = pixels
color_line:
	ldv	x3,g_mode
	cmp	x3,C_NEWTON
	b.eq	color_newton
	cmp	x3,C_LYAP
	b.eq	color_lyap
	adr	x3,g_lut
	ldv	x4,g_offset
	adr	x5,g_scale
	ldr	s1,[x5]
	fmov	s2,wzr
.l:	cbz	x1,.ret
	ldr	s0,[x0],4
	fcmp	s0,s2
	b.mi	.in
	fmul	s0,s0,s1
	fcvtzu	w5,s0
	add	w5,w5,w4
	and	w5,w5,LUT_N-1
	ldr	w5,[x3,x5,lsl 2]
	str	w5,[x2],4
	sub	x1,x1,1
	b	.l
.in:	str	wzr,[x2],4
	sub	x1,x1,1
	b	.l
.ret:	ret

; racine * 1000 + iterations : un tiers de la palette par racine, plus sombre
; quand il a fallu longtemps
color_newton:
	adr	x3,g_lut
	ldv	x4,g_offset
	mov	w9,1000
	mov	w10,LUT_N/3
.l:	cbz	x1,.ret
	sub	x1,x1,1
	ldr	s0,[x0],4
	fcvtzs	w5,s0
	tbnz	w5,31,.in
	udiv	w6,w5,w9
	msub	w7,w6,w9,w5			; iterations
	mul	w6,w6,w10
	add	w6,w6,w4
	add	w6,w6,w7,lsl 3
	and	w6,w6,LUT_N-1
	ldr	w6,[x3,x6,lsl 2]
	mov	w8,240				; lumiere
	sub	w8,w8,w7,lsl 4
	cmp	w8,48
	mov	w11,48
	csel	w8,w11,w8,lt
	ubfx	w11,w6,16,8
	mul	w11,w11,w8
	lsr	w11,w11,8
	ubfx	w12,w6,8,8
	mul	w12,w12,w8
	lsr	w12,w12,8
	and	w13,w6,255
	mul	w13,w13,w8
	lsr	w13,w13,8
	orr	w11,w13,w11,lsl 16
	orr	w11,w11,w12,lsl 8
	str	w11,[x2],4
	b	.l
.in:	str	wzr,[x2],4
	b	.l
.ret:	ret

; l'exposant : negatif (stable) la palette, positif (chaos) du noir vers le bleu
color_lyap:
	adr	x3,g_lut
	ldv	x4,g_offset
	adr	x5,g_scale
	ldr	s1,[x5]
	fmov	s2,wzr
.l:	cbz	x1,.ret
	sub	x1,x1,1
	ldr	s0,[x0],4
	fcmp	s0,s2
	b.vs	.in
	b.pl	.pos
	fneg	s0,s0
	fmul	s0,s0,s1
	fcvtzu	w5,s0
	add	w5,w5,w4
	and	w5,w5,LUT_N-1
	ldr	w5,[x3,x5,lsl 2]
	str	w5,[x2],4
	b	.l
.pos:	fmov	s3,8.0				; (hors de 0..4 : x part a l'infini)
	fcmp	s0,s3
	b.gt	.in
	fmov	s3,1.0
	fmin	s0,s0,s3
	mov	w5,160
	scvtf	s3,w5
	fmul	s0,s0,s3
	fcvtzu	w5,s0
	lsr	w6,w5,2
	orr	w5,w5,w6,lsl 8
	str	w5,[x2],4
	b	.l
.in:	str	wzr,[x2],4
	b	.l
.ret:	ret

; x0 > 0 -> w0 = 256 * log2 approche
ilog2:
	clz	x1,x0
	mov	x2,63
	sub	x2,x2,x1
	lsl	x0,x0,x1
	ubfx	x0,x0,55,8
	add	x0,x0,x2,lsl 8
	ret

; les comptes de g_mu : le plus grand fait le blanc de l'echelle (logarithme)
color_hist:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	ldv	x19,g_mu
	ldv	x21,g_vh
	ldv	x22,g_vw
	mul	x20,x21,x22
	mov	x0,0				; le plus grand
	mov	x1,0
.m:	cmp	x1,x20
	b.hs	.m1
	ldr	w2,[x19,x1,lsl 2]
	cmp	x2,x0
	csel	x0,x2,x0,hi
	add	x1,x1,1
	b	.m
.m1:	add	x0,x0,1
	bl	ilog2
	cmp	x0,256
	mov	x1,256
	csel	x24,x1,x0,lo			; log du plus grand
	ldv	x20,g_px
	adr	x23,g_lut
	ldv	x25,g_offset
	mov	x26,LUT_N-1
.y:	cbz	x21,.out
	mov	x1,0
.x:	cmp	x1,x22
	b.hs	.nl
	ldr	w0,[x19,x1,lsl 2]
	cbnz	w0,.c
	str	wzr,[x20,x1,lsl 2]
	add	x1,x1,1
	b	.x
.c:	add	x0,x0,1
	mov	x3,x1
	bl	ilog2
	mov	x1,x3
	mul	x0,x0,x26
	udiv	x0,x0,x24
	ldv	x3,g_type			; (Buddhabrot : le contraste)
	cmp	x3,T_BUDDHA
	b.ne	.half
	mul	x0,x0,x0
	mul	x0,x0,x0
	lsr	x0,x0,36
.half:	lsr	x0,x0,1				; (la premiere moitie de la palette)
	mov	x3,x0
	add	x0,x0,x25
	and	x0,x0,LUT_N-1
	ldr	w0,[x23,x0,lsl 2]
	cmp	x3,512				; le fond sort du noir
	b.hs	.keep
	ubfx	w4,w0,16,8
	ubfx	w5,w0,8,8
	and	w6,w0,255
	mul	w4,w4,w3
	mul	w5,w5,w3
	mul	w6,w6,w3
	lsr	w4,w4,9
	lsr	w5,w5,9
	lsr	w6,w6,9
	orr	w0,w6,w4,lsl 16
	orr	w0,w0,w5,lsl 8
.keep:	str	w0,[x20,x1,lsl 2]
	add	x1,x1,1
	b	.x
.nl:	add	x19,x19,x22,lsl 2
	ldv	x0,g_pitch
	add	x20,x20,x0
	sub	x21,x21,1
	b	.y
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = palette : g_lut, 4096 couleurs, boucle fermee sur ses arrets
set_palette:
	stv	x0,g_palnum
	adr	x1,palettes
	add	x1,x1,x0,lsl 5			; 8 arrets
	adr	x2,g_lut
	mov	x3,0
.l:	lsr	x4,x3,9				; arret
	and	x5,x3,511			; dans l'intervalle
	ldr	w6,[x1,x4,lsl 2]
	add	x4,x4,1
	and	x4,x4,7
	ldr	w7,[x1,x4,lsl 2]
	mov	x8,512
	sub	x8,x8,x5
	ubfx	w9,w6,16,8
	ubfx	w10,w7,16,8
	mul	w9,w9,w8
	madd	w9,w10,w5,w9
	lsr	w9,w9,9
	ubfx	w10,w6,8,8
	ubfx	w11,w7,8,8
	mul	w10,w10,w8
	madd	w10,w11,w5,w10
	lsr	w10,w10,9
	and	w11,w6,255
	and	w12,w7,255
	mul	w11,w11,w8
	madd	w11,w12,w5,w11
	lsr	w11,w11,9
	orr	w9,w11,w9,lsl 16
	orr	w9,w9,w10,lsl 8
	str	w9,[x2,x3,lsl 2]
	add	x3,x3,1
	cmp	x3,LUT_N
	b.lo	.l
	ret

; x0 = destination, x1 = morceau de texte -> x0 = fin : str_cat traduit. Les espaces autour du

; morceau restent ici (un dictionnaire ne garde pas ceux d'une cle).

tr_cat:

	prologue

	stp	x19,x20,[sp,-16]!

	str	x21,[sp,-16]!

	sub	sp,sp,240

	mov	x19,x0

	mov	x20,x1

.e:	ldrb	w0,[x20]			; espaces de tete

	cmp	w0,' '

	b.ne	.d

	strb	w0,[x19],1

	add	x20,x20,1

	b	.e

.d:	mov	x21,0				; longueur sans ceux de queue

	mov	x2,0

.c:	ldrb	w0,[x20,x2]

	strb	w0,[sp,x2]

	cbz	w0,.f

	add	x2,x2,1

	cmp	w0,' '

	csel	x21,x21,x2,eq

	cmp	x2,239

	b.lo	.c

.f:	strb	wzr,[sp,x21]

	add	x20,x20,x21

	mov	x0,sp

	bl	ui_tr

	mov	x1,x0

	mov	x0,x19

	bl	str_cat

.q:	ldrb	w1,[x20],1

	strb	w1,[x0],1

	cbnz	w1,.q

	sub	x0,x0,1

	add	sp,sp,240

	ldr	x21,[sp],16

	ldp	x19,x20,[sp],16

	epilogue



; x0 = texte -> sa traduction ; x1 a x7 gardes

tr0:

	prologue

	stp	x1,x2,[sp,-16]!

	stp	x3,x4,[sp,-16]!

	stp	x5,x6,[sp,-16]!

	str	x7,[sp,-16]!

	bl	ui_tr

	ldr	x7,[sp],16

	ldp	x5,x6,[sp],16

	ldp	x3,x4,[sp],16

	ldp	x1,x2,[sp],16

	epilogue



; x1 = texte -> sa traduction ; les autres registres gardes

tr1:

	prologue

	str	x0,[sp,-16]!

	mov	x0,x1

	bl	tr0

	mov	x1,x0

	ldr	x0,[sp],16

	epilogue



; x4 = texte -> sa traduction ; les autres registres gardes

tr4:

	prologue

	str	x0,[sp,-16]!

	mov	x0,x4

	bl	tr0

	mov	x4,x0

	ldr	x0,[sp],16

	epilogue

; la ligne d'etat : zoom, iterations, temps
status:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,g_stat
	ldv	x0,g_picking
	cbz	x0,.stx
	mov	x0,x19
	adr	x1,s_pickhint
	bl	tr_cat
	b	.set
.stx:	mov	x0,x19
	adr	x1,s_zoom
	bl	tr_cat
	mov	x1,x0
	adr	x0,g_zoom			; 2^zoom
	ldr	d0,[x0]
	fmov	d1,xzr
	fcmp	d0,d1
	mov	x0,1
	b.le	.n
	fcvtzu	x2,d0
	cmp	x2,50
	mov	x3,50
	csel	x2,x3,x2,hi
	lsl	x0,x0,x2
.n:	bl	fmt_dec
	ldv	x1,g_mode
	cmp	x1,C_HIST
	b.eq	.h
	adr	x1,s_iter
	bl	tr_cat
	mov	x1,x0
	ldv	x0,g_maxit
	bl	fmt_dec
	b	.t
.h:	adr	x1,s_points
	bl	tr_cat
	mov	x1,x0
	ldv	x0,g_row
	ldv	x2,g_type
	cmp	x2,T_BUDDHA
	mov	x2,ATTR_N*2
	mov	x3,BUD_N*2
	csel	x2,x3,x2,eq
	mul	x0,x0,x2
	mov	x2,1000
	udiv	x0,x0,x2
	bl	fmt_dec
	adr	x1,s_kpts
	bl	str_cat
.t:	ldv	x1,g_active
	cbz	x1,.fin
	adr	x1,s_busy
	bl	tr_cat
	b	.set
.fin:	adr	x1,s_sep
	bl	str_cat
	mov	x20,x0
	sys	SYS_TIME_MS
	ldv	x2,g_t0
	ldv	x1,g_tend
	cmp	x1,x2
	csel	x0,x1,x0,hi
	sub	x0,x0,x2
	mov	x1,x20
	bl	fmt_dec
	adr	x1,s_ms
	bl	str_cat
.set:	ldv	x0,w_status
	mov	x1,x19
	bl	ui_w_set_text
	ldp	x19,x20,[sp],16
	epilogue

; --- PNG : ArchiveKit fait le deflate et les CRC ------------------------------------
save_png:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,s_picdir
	adr	x1,s_pngname
	adr	x2,g_path
	mov	x3,256
	bl	ui_file_save
	mov	x19,x0
	mov	x0,0
	bl	ui_w_draw
	cmp	x19,1
	b.ne	.out
	ldv	x20,g_vw
	ldv	x21,g_vh
	mov	x0,3
	mul	x0,x0,x20
	add	x22,x0,1			; octets d'une ligne
	mul	x0,x22,x21
	bl	malloc
	cbz	x0,.out
	mov	x23,x0
	mov	x1,x0				; filtre 0, RVB
	ldv	x2,g_px
	ldv	x3,g_pitch
	mov	x4,x21
.y:	cbz	x4,.yd
	strb	wzr,[x1],1
	mov	x5,0
.x:	ldr	w6,[x2,x5,lsl 2]
	lsr	w7,w6,16
	strb	w7,[x1],1
	lsr	w7,w6,8
	strb	w7,[x1],1
	strb	w6,[x1],1
	add	x5,x5,1
	cmp	x5,x20
	b.lo	.x
	add	x2,x2,x3
	sub	x4,x4,1
	b	.y
.yd:	mov	x0,x23
	mul	x1,x22,x21
	mov	x2,AR_ZLIB
	mov	x3,6
	bl	ar_deflate_new
	mov	x24,x0
	cmp	x24,0
	b.le	.err
	adr	x0,g_path
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x19,x0
	mov	x1,x19				; signature
	adr	x1,png_sig
	mov	x2,8
	sys	SYS_WRITE
	sub	sp,sp,16			; IHDR
	rev	w0,w20
	str	w0,[sp]
	rev	w0,w21
	str	w0,[sp,4]
	movl	w0,0x00000208			; 8 bits, RVB
	str	w0,[sp,8]
	str	wzr,[sp,12]
	mov	x0,x19
	adr	x1,s_ihdr
	mov	x2,sp
	mov	x3,13
	bl	png_chunk
	add	sp,sp,16
	mov	x0,x24
	bl	ar_data
	mov	x21,x0
	mov	x0,x24
	bl	ar_bytes
	mov	x3,x0
	mov	x0,x19
	adr	x1,s_idat
	mov	x2,x21
	bl	png_chunk
	mov	x0,x19
	adr	x1,s_iend
	mov	x2,0
	mov	x3,0
	bl	png_chunk
	mov	x0,x19
	sys	SYS_CLOSE
	mov	x0,x24
	bl	ar_free
	ldv	x0,w_status
	adr	x1,s_saved
	bl	tr1
	bl	ui_w_set_text
	b	.out
.err:	ldv	x0,w_status
	adr	x1,s_savefail
	bl	tr1
	bl	ui_w_set_text
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fichier, x1 = nom (4 o), x2 = donnees, x3 = octets
png_chunk:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	sub	sp,sp,16
	rev	w0,w22
	str	w0,[sp]
	mov	x0,x19
	mov	x1,sp
	mov	x2,4
	sys	SYS_WRITE
	mov	x0,x19
	mov	x1,x20
	mov	x2,4
	sys	SYS_WRITE
	mov	x0,0
	mov	x1,x20
	mov	x2,4
	bl	ar_crc32
	str	x0,[sp,8]
	cbz	x22,.nod
	mov	x0,x19
	mov	x1,x21
	mov	x2,x22
	sys	SYS_WRITE
	ldr	x0,[sp,8]
	mov	x1,x21
	mov	x2,x22
	bl	ar_crc32
	str	x0,[sp,8]
.nod:	ldr	x0,[sp,8]
	rev	w0,w0
	str	w0,[sp]
	mov	x0,x19
	mov	x1,sp
	mov	x2,4
	sys	SYS_WRITE
	add	sp,sp,16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include 'kernels.inc'

		align 8
; les types : kernel (depuis ftab), couleurs, centre, largeur, 1 / log2 (degre), iterations
macro fentry kern, mode, cx, cy, span, ild, iter, pal {
	dw	kern-ftab, mode
	dd	cx, cy, span, ild
	dd	iter, pal
}
ftab:
	fentry k_mandel, C_SMOOTH, -0.6, 0.0, 3.4, 1.0, 256, -1
	fentry k_julia, C_SMOOTH, 0.0, 0.0, 3.4, 1.0, 256, -1
	fentry k_ship, C_SMOOTH, -0.45, -0.5, 3.6, 1.0, 256, 1
	fentry k_tricorn, C_SMOOTH, -0.3, 0.0, 3.6, 1.0, 256, -1
	fentry k_m3, C_SMOOTH, 0.0, 0.0, 3.2, 0.6309297535714574, 256, -1
	fentry k_m4, C_SMOOTH, 0.0, 0.0, 3.0, 0.5, 256, -1
	fentry k_m5, C_SMOOTH, 0.0, 0.0, 3.0, 0.43067655807339306, 256, -1
	fentry k_newton, C_NEWTON, 0.0, 0.0, 4.0, 1.0, 64, 6
	fentry k_phoenix, C_SMOOTH, 0.0, 0.0, 3.2, 1.0, 400, 4
	fentry k_lyap, C_LYAP, 3.0, 3.0, 2.0, 1.0, 240, 3
	fentry k_clifford, C_HIST, 0.0, 0.0, 7.5, 1.0, 1000, 1
	fentry k_dejong, C_HIST, 0.0, 0.0, 7.0, 1.0, 1000, 5
	fentry k_buddha, C_HIST, 0.0, -0.4, 4.2, 1.0, 1000, 5

; .25, 1, 1/16, 3, seuil (2 voies chacun), epsilon de Newton
k_tab	dd 0.25, 0.25, 1.0, 1.0, 0.0625, 0.0625, 3.0, 3.0, 1.0e6, 1.0e6, 1.0e-10
; log2 (1 + t) = t (a1 + t (a2 + t (a3 + t (a4 + t a5))))
k_log	dd 1.4417403028805735, -0.7077701799616332, 0.4123442155932546, -0.19031902828119998, 0.04400468976900496
; 1 / 2 pi, 2 pi, sin : s1, s3, s5, s7, s9 sur (-pi, pi), pi / 2 (2 voies chacun)
k_sin	dd 0.15915494309189535, 0.15915494309189535, 6.283185307179586, 6.283185307179586
	dd 0.9999845756675534, 0.9999845756675534, -0.166632562538804, -0.166632562538804
	dd 0.008312374042022508, 0.008312374042022508, -0.0001931603634860011, -0.0001931603634860011
	dd 2.173132085161652e-06, 2.173132085161652e-06, 1.5707963267948966, 1.5707963267948966
k_bud	dd 4.470348358154297e-08, 2.0, 0.0625		; 3 / 2^26, 2, 1/16
k_wheel	dd 0.8, 1.25
k_min	dd 1.0e-13, 40.0
k_phx	dd 0.5667, -0.5
k_jul	dd -0.8, 0.156
k_fit	dd 1.12
k_orb	dd 0.07, 0.11
; g_scale selon la facon de colorer
k_scale	dw 28.0f, 0.0f, 900.0f, 0.0f
kp_cliff dd 1.5, -1.8, 1.6, 0.9
	dd -1.4, 1.6, 1.0, 0.7
	dd 1.7, 1.7, 0.6, 1.2
	dd -1.8, -2.0, -0.5, -0.9
kp_dejong dd 1.4, -2.3, 2.4, -2.1
	dd -2.0, -2.0, -1.2, 2.0
	dd 0.97, -1.899, 1.381, -1.506
	dd 2.01, -2.53, 1.61, -0.33

; les signets : type, palette, centre, largeur, c (Julia), a..d (attracteurs), suite (Lyapunov)
MK_TYPE		= 0
MK_PAL		= 8
MK_CX		= 16
MK_SPAN		= 32
MK_P0		= 40
MK_P2		= 56
MK_SIZE		= 72
macro mark t, p, cx, cy, span, p0, p1, p2, p3 {
	dd t, p
	dd cx, cy, span
	dd p0, p1, p2, p3
}
macro markl t, p, cx, cy, span, seq {
local a
	dd t, p
	dd cx, cy, span
a:	db seq
	rb a+32-$
}
marks:
	mark T_MANDEL, 0, -0.7436447860, 0.1318252536, 0.00035, 0.0, 0.0, 0.0, 0.0
	mark T_MANDEL, 1, 0.2925, 0.0149, 0.012, 0.0, 0.0, 0.0, 0.0
	mark T_MANDEL, 2, -0.1011, 0.9563, 0.02, 0.0, 0.0, 0.0, 0.0
	mark T_MANDEL, 3, -1.7497, 0.0, 0.03, 0.0, 0.0, 0.0, 0.0
	mark T_JULIA, 5, 0.0, 0.0, 3.2, -0.8, 0.156, 0.0, 0.0
	mark T_JULIA, 0, 0.0, 0.0, 3.2, -0.123, 0.745, 0.0, 0.0
	mark T_JULIA, 4, 0.0, 0.0, 3.2, 0.285, 0.01, 0.0, 0.0
	mark T_JULIA, 1, 0.0, 0.0, 3.2, -0.4, 0.6, 0.0, 0.0
	mark T_SHIP, 1, -1.7625, -0.0285, 0.08, 0.0, 0.0, 0.0, 0.0
	markl T_LYAP, 3, 3.0, 3.0, 2.0, <'AB',0>
	markl T_LYAP, 2, 3.7, 2.95, 0.6, <'BBBBBBAAAAAA',0>
	markl T_LYAP, 4, 3.4, 3.6, 0.8, <'AABAB',0>
	mark T_CLIFFORD, 2, 0.0, 0.0, 7.5, -1.4, 1.6, 1.0, 0.7
	mark T_CLIFFORD, 1, 0.0, 0.0, 7.5, 1.5, -1.8, 1.6, 0.9
	mark T_DEJONG, 5, 0.0, 0.0, 7.0, 1.4, -2.3, 2.4, -2.1
	mark T_BUDDHA, 1, 0.0, -0.4, 4.2, 0.0, 0.0, 0.0, 0.0

NPAL		= 8
; huit arrets par palette, la boucle se ferme
palettes:
	dw 0x000A1450, 0x00206BCB, 0x00EDFFFF, 0x00FFAA00, 0x00803000, 0x00100800, 0x00300860, 0x000A1450	; Aurore
	dw 0x00000000, 0x00500000, 0x00C02000, 0x00FF8000, 0x00FFE060, 0x00FFFFFF, 0x00FFC030, 0x00802000	; Feu
	dw 0x00001020, 0x00003060, 0x000060A0, 0x0040B0D0, 0x00C0F0F0, 0x0040B0D0, 0x000060A0, 0x00003060	; Ocean
	dw 0x00100800, 0x00402000, 0x00906010, 0x00E0B030, 0x00FFF0A0, 0x00E0B030, 0x00906010, 0x00402000	; Or
	dw 0x00000020, 0x004000C0, 0x00E000E0, 0x0000E0E0, 0x00FFFFFF, 0x00E0E000, 0x00E00080, 0x00200040	; Electrique
	dw 0x00102040, 0x00305880, 0x0070A0C8, 0x00C0E0F0, 0x00FFFFFF, 0x00C0E0F0, 0x0070A0C8, 0x00305880	; Givre
	dw 0x00FF0000, 0x00FF9000, 0x00F0F000, 0x0000D000, 0x0000C0F0, 0x000040F0, 0x008000E0, 0x00F00090	; Arc-en-ciel
	dw 0x00000000, 0x00383838, 0x00808080, 0x00D0D0D0, 0x00FFFFFF, 0x00D0D0D0, 0x00808080, 0x00383838	; Noir et blanc

s_title		db 'Fractales',0
s_types		db 'Mandelbrot|Julia|Burning Ship|Tricorn|Multibrot 3|Multibrot 4|Multibrot 5|Newton|Phoenix|Lyapunov|Clifford|De Jong|Buddhabrot',0
s_pals		db 'Aurore|Feu|Oc',0xC3,0xA9,'an|Or|',0xC3,0x89,'lectrique|Givre|Arc-en-ciel|Noir et blanc',0
s_marks		db 'Signets...|Hippocampes|',0xC3,0x89,'l',0xC3,0xA9,'phants|Spirales|Mini-ensemble|Dragon|Lapin de Douady|Cercles|',0xC3,0x89,'clair|Armada|Lyapunov AB|Zircon|Spirales AABAB|Clifford 1|Clifford 2|De Jong|Bouddha',0
s_reset		db 'D',0xC3,0xA9,'part',0
s_anim		db 'Animer',0
s_png		db 'PNG',0
s_pick		db 'Choisir c...',0
s_other		db 'Autres valeurs',0
s_tipiter	db 'It',0xC3,0xA9,'rations de d',0xC3,0xA9,'part (elles montent avec le zoom)',0
s_tipseq	db 'Suite de A et de B',0
s_pickhint	db 'Touchez un point : la fractale de Julia de ce point.',0
s_zoom		db 'Zoom ',0
s_iter		db '   It',0xC3,0xA9,'rations ',0
s_points	db '   Points ',0
s_kpts		db ' 000',0
s_busy		db '   (calcul...)',0
s_sep		db '   ',0
s_ms		db ' ms',0
s_picdir	db 'SD0:/home/pictures',0
s_pngname	db 'fractale.png',0
s_saved		db 'Image enregistr',0xC3,0xA9,'e.',0
s_savefail	db 'Image non enregistr',0xC3,0xA9,'e.',0
png_sig		db 0x89,'PNG',13,10,26,10
s_ihdr		db 'IHDR'
s_idat		db 'IDAT'
s_iend		db 'IEND'
		align 8

segment readable writeable
kit_data
		align 8
g_tok		dd 0
g_dummy		dd 0
w_type		dd 0
w_pal		dd 0
w_mark		dd 0
w_reset		dd 0
w_anim		dd 0
w_png		dd 0
w_status	dd 0
w_iter		dd 0
w_pick		dd 0
w_seq		dd 0
w_other		dd 0
g_sf		dd 0
g_px		dd 0
g_pitch		dd 0
g_mu		dd 0
g_musize	dd 0
g_work		dd 0
g_vw		dd 0
g_vh		dd 0
g_type		dd 0
g_kern		dd 0
g_mode		dd 0
g_base		dd 256
g_maxit		dd 256
g_palnum	dd 0
g_offset	dd 0
g_anim		dd 0
g_picking	dd 0
g_drag		dd 0
g_moved		dd 0
g_lx		dd 0
g_ly		dd 0
g_mx		dd 0
g_my		dd 0
g_tap		dd 0
g_t0		dd 0
g_lasth		dd 0
g_lasts		dd 0
g_lastf		dd 0
g_tend		dd 0
g_mark_ok	dd 0
g_pnum		dd 0
g_seqn		dd 2
; la vue (contigus)
g_cx		dd 0.0
g_cy		dd 0.0
g_span		dd 3.0
g_span0		dd 3.0
g_zoom		dd 0.0
; ce que les calculs lisent (contigus)
g_x0		dd 0.0
g_y0		dd 0.0
g_dx		dd 0.0
g_cr		dd -0.8
g_ci		dd 0.156
g_ild		dd 1.0
g_pa		dd -1.4, 1.6, 1.0, 0.7
; le travail
g_active	dd 0
g_finished	dd 0
g_gen		dd 0
g_row		dd 0
g_nrows		dd 0
g_step		dd 8
g_inflight	dd 0
g_mtx		dw 0, 0
g_cnd		dw 0, 0
g_scale		dw 28.0f
		align 8
g_seq		db 0, 1
		rb 62
g_seqtext	db 'AB',0
		rb 61
		align 8
g_sys		rb 80
g_scr		rb 32
g_path		rb 256
g_stat		rb 160
g_lut		rb LUT_N*4
ui_data
