; ============================================================================
;  gamepause.inc - ce qui est commun aux jeux en plein ecran (sur gamefb.inc et
;  gameart.inc) : le menu de pause et la manette.
;
;  Un meme comportement pour tous les jeux :
;    Start (manette) ou Echap (clavier), ou le bouton || en haut de l'image :
;      le jeu s'arrete, le menu s'ouvre : Reprendre, Recommencer, Quitter ;
;    dans le menu : croix ou fleches, A / Entree / Espace / clic validant ;
;      B, Start ou Echap : retour au jeu.
;  La perte du focus pendant une partie ouvre aussi le menu.
;
;  Le jeu inclut ce fichier apres gameart.inc ; gp_data avant gf_data. Apres
;  ga_init (hors de ui_canvas_begin) :
;    gp_init(x0 = pause, x1 = reprise, x2 = nouvelle partie, x3, x4 = coin du
;            bouton || dans l'image logique, x3 < 0 : pas de bouton)
;    pause : le jeu s'arrete -> x0 = 1 si une partie etait en cours ; reprise :
;    la partie repart (si elle etait arretee) ; x28 garde pour les trois.
;  En tete de la fonction d'evenements du jeu : bl gp_event ; cbnz x0 -> fini.
;  Pour WEV_PAD, gp_event a deja calcule gp_now (boutons PAD_*) et gp_new (les
;  boutons qui viennent d'etre appuyes) des manettes 1..3 : le jeu les lit ; la
;  manette 0 (le clavier mappe par HIDManager, deja arrive en touches) ne
;  parvient pas au jeu. Essai sous QEMU, sans manette : fasmarm -d GP_PAD0=1
;  (le clavier compte alors comme une manette : a b x y, Entree = Start).
;  gf_run dessine le menu par-dessus l'image du jeu (gp_draw).
; ============================================================================

GP_N		= 3			; Reprendre, Recommencer, Quitter
GP_PW		= 300			; panneau
GP_IW		= 236			; une entree
GP_IH		= 40
GP_IS		= 48			; d'une entree a la suivante
GP_PH		= 92+GP_N*GP_IS+30
GP_PX		= (GF_W-GP_PW)/2
GP_PY		= (GF_H-GP_PH)/2
GP_IX		= (GF_W-GP_IW)/2
GP_IY		= GP_PY+78
GP_BS		= 28			; le bouton ||

GP_K_ENTER	= 0x28
GP_K_ESC	= 0x29
GP_K_SPACE	= 0x2C
GP_K_DOWN	= 0x51
GP_K_UP		= 0x52

; x0 = pause, x1 = reprise, x2 = nouvelle partie, x3, x4 = bouton ||
gp_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x5,gp_fn
	stp	x0,x1,[x5]
	str	x2,[x5,16]
	adr	x5,gp_bx
	stp	x3,x4,[x5]
	ldv	x0,gf_cv
	bl	ui_canvas_begin
	adr	x19,gp_txt
	adr	x20,gp_tx
	mov	x21,0
.t:	ldrh	w0,[x20]
	adr	x1,gp_s_base
	add	x0,x1,x0
	ldrb	w1,[x20,2]
	ldrb	w2,[x20,3]
	adr	x3,ui_font
	adr	x4,ui_font_b
	cmp	x2,0
	csel	x2,x4,x3,ne
	ldr	w3,[x20,4]
	ldr	w4,[x20,8]
	bl	ga_text
	str	x0,[x19],8
	add	x20,x20,12
	add	x21,x21,1
	cmp	x21,GP_NTX
	b.lo	.t
	bl	ui_canvas_end
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; le menu s'ouvre (la partie s'arrete d'abord)
gp_open:
	prologue
	adr	x0,gp_on
	ldr	x1,[x0]
	cbnz	x1,.out
	adr	x0,gp_fn
	ldr	x0,[x0]
	blr	x0
	bl	gp_show
.out:	epilogue

; le menu se montre (le jeu est deja arrete)
gp_show:
	prologue
	mov	x0,1
	stv	x0,gp_on
	stv	xzr,gp_sel
	stv	x0,gf_redraw
	stv	xzr,gp_down
	bl	gp_blip
	epilogue

; retour au jeu
gp_close:
	prologue
	stv	xzr,gp_on
	mov	x0,1
	stv	x0,gf_redraw
	adr	x0,gp_fn
	ldr	x0,[x0,8]
	blr	x0
	epilogue

; x0 = entree : elle agit
gp_do:
	prologue
	cmp	x0,1
	b.lo	.resume
	b.eq	.new
	mov	x0,1
	stv	x0,gf_quit
	b	.out
.new:	stv	xzr,gp_on
	mov	x0,1
	stv	x0,gf_redraw
	adr	x0,gp_fn
	ldr	x0,[x0,16]
	blr	x0
	b	.out
.resume:
	bl	gp_close
.out:	epilogue

; x0 = +1 / -1 : la selection bouge
gp_move:
	prologue
	ldv	x1,gp_sel
	add	x1,x1,x0
	cmp	x1,0
	mov	x2,GP_N-1
	csel	x1,x2,x1,lt
	cmp	x1,GP_N
	csel	x1,xzr,x1,ge
	stv	x1,gp_sel
	mov	x0,1
	stv	x0,gf_redraw
	bl	gp_blip
	epilogue

gp_blip:
	prologue
	mov	x0,0
	mov	x1,880
	mov	x2,25
	mov	x3,AKW_SINE
	mov	x4,70
	bl	gs_later
	epilogue

; x0, x1 = point logique -> x0 = entree sous le point, -1 : aucune
gp_hit:
	mov	x2,GP_IX
	sub	x2,x0,x2
	cmp	x2,GP_IW
	b.hs	.no
	mov	x2,GP_IY
	subs	x2,x1,x2
	b.lo	.no
	mov	x3,GP_IS
	udiv	x0,x2,x3
	msub	x3,x0,x3,x2
	cmp	x3,GP_IH
	b.hs	.no
	cmp	x0,GP_N
	b.hs	.no
	ret
.no:	mov	x0,-1
	ret

; x0 = type d'evenement -> x0 = 1 s'il a servi au menu (le jeu l'ignore)
gp_event:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	x19,WEV_PAD
	b.ne	.np
	adr	x0,ui_ev			; les fronts, pour le jeu aussi
	ldr	w1,[x0,EV_KIND]
 if defined GP_PAD0				; (essai sous QEMU : fasmarm -d GP_PAD0=1,
	cbnz	w1,.real			;  le clavier mappe compte comme une manette)
	mov	w1,1
	str	w1,[x0,EV_KIND]
.real:
 else
	cbz	w1,.used			; (manette 0 : le clavier, deja en touches)
 end if
	ldr	w2,[x0,WEV_PAD_BUTTONS]
	ldv	x3,gp_now
	bic	x4,x2,x3
	stv	x2,gp_now
	stv	x4,gp_new
	ldv	x0,gp_on
	cbnz	x0,.mpad
	tst	x4,PAD_START
	b.eq	.pass
	bl	gp_open
	b	.used
.mpad:	tst	x4,PAD_START
	b.ne	.pb
	tst	x4,PAD_B
	b.eq	.pa
.pb:	bl	gp_close
	b	.used
.pa:	tst	x4,PAD_A
	b.eq	.pu
	ldv	x0,gp_sel
	bl	gp_do
	b	.used
.pu:	tst	x4,PAD_UP
	b.eq	.pd
	mov	x0,-1
	bl	gp_move
	b	.used
.pd:	tst	x4,PAD_DOWN
	b.eq	.used
	mov	x0,1
	bl	gp_move
	b	.used
.np:	cmp	x19,WEV_KEY
	b.ne	.nk
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	ldr	w2,[x0,EV_CODE]
	ldv	x0,gp_on
	cbnz	x0,.mkey
	cbz	w1,.pass
	cmp	w2,GP_K_ESC
	b.ne	.pass
	bl	gp_open
	b	.used
.mkey:	cbz	w1,.used
	cmp	w2,GP_K_ESC
	b.ne	.ke
	bl	gp_close
	b	.used
.ke:	cmp	w2,GP_K_ENTER
	b.eq	.kv
	cmp	w2,GP_K_SPACE
	b.ne	.ku
.kv:	ldv	x0,gp_sel
	bl	gp_do
	b	.used
.ku:	cmp	w2,GP_K_UP
	b.ne	.kd
	mov	x0,-1
	bl	gp_move
	b	.used
.kd:	cmp	w2,GP_K_DOWN
	b.ne	.used
	mov	x0,1
	bl	gp_move
	b	.used
.nk:	cmp	x19,WEV_MOUSE
	b.ne	.nm
	bl	gf_mouse
	mov	x20,x0
	mov	x2,x1
	adr	x3,ui_ev
	ldr	w4,[x3,EV_KIND]
	ldv	x0,gp_on
	cbnz	x0,.mm
	cmp	w4,1				; un appui sur le bouton ||
	b.ne	.pass
	ldr	w5,[x3,EV_CODE]
	tbz	w5,0,.pass
	ldv	x6,gp_bx
	tbnz	x6,63,.pass
	sub	x6,x20,x6
	cmp	x6,GP_BS
	b.hs	.pass
	ldv	x6,gp_by
	sub	x6,x2,x6
	cmp	x6,GP_BS
	b.hs	.pass
	bl	gp_open
	b	.used
.mm:	mov	x0,x20				; dans le menu : survol, appui, relache
	mov	x1,x2
	bl	gp_hit
	mov	x20,x0
	tbnz	x20,63,.mo
	ldv	x1,gp_sel
	cmp	x1,x20
	b.eq	.mo
	stv	x20,gp_sel
	mov	x1,1
	stv	x1,gf_redraw
.mo:	adr	x3,ui_ev
	ldr	w4,[x3,EV_KIND]
	cmp	w4,1
	b.ne	.mr
	ldr	w5,[x3,EV_CODE]
	tbz	w5,0,.used
	add	x0,x20,1			; l'entree appuyee (+1, 0 : dehors)
	stv	x0,gp_down
	b	.used
.mr:	cmp	w4,2
	b.ne	.used
	ldv	x0,gp_down
	stv	xzr,gp_down
	cbz	x0,.used
	sub	x0,x0,1
	cmp	x0,x20
	b.ne	.used
	bl	gp_do
	b	.used
.nm:	cmp	x19,WEV_FOCUS
	b.ne	.nf
	adr	x0,ui_ev
	stv	xzr,gp_now			; (les boutons tenus sont oublies)
	ldr	w1,[x0,EV_KIND]
	cbnz	w1,.pass
	ldv	x0,gp_on
	cbnz	x0,.pass
	adr	x0,gp_fn			; perdu en pleine partie : le menu
	ldr	x0,[x0]
	blr	x0
	cbz	x0,.pass
	bl	gp_show
	b	.pass
.nf:	cmp	x19,WEV_MENU
	b.ne	.pass
	ldv	x0,gp_on
	cbnz	x0,.used
.pass:	mov	x0,0
	b	.out
.used:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; dans gf_frame : l'image s'assombrit, le panneau et ses entrees ; sinon le bouton ||
gp_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,gf_frame
	stv	x0,gf_dst
	ldv	x0,gp_on
	cbnz	x0,.menu
	ldv	x19,gp_bx
	tbnz	x19,63,.out
	ldv	x20,gp_by
	mov	x0,x19
	mov	x1,x20
	mov	x2,GP_BS
	mov	x3,GP_BS
	mov	x4,8
	movl	w5,0x00303A50
	movl	w6,0x00181E2C
	mov	x7,150
	movl	w8,0x00687890
	bl	ga_panel
	add	x0,x19,9
	add	x1,x20,8
	mov	x2,4
	mov	x3,GP_BS-16
	movl	w4,0x00E8EEF8
	bl	gf_rect
	add	x0,x19,GP_BS-13
	add	x1,x20,8
	mov	x2,4
	mov	x3,GP_BS-16
	movl	w4,0x00E8EEF8
	bl	gf_rect
	b	.out
.menu:	adr	x0,gf_frame			; tout a 3/8, un peu bleui
	movl	x1,GF_W*GF_H*4
	movi	v4.16b,0x06
.dim:	ldp	q0,q1,[x0]
	ushr	v2.16b,v0.16b,2
	ushr	v0.16b,v0.16b,3
	add	v0.16b,v0.16b,v2.16b
	add	v0.16b,v0.16b,v4.16b
	ushr	v3.16b,v1.16b,2
	ushr	v1.16b,v1.16b,3
	add	v1.16b,v1.16b,v3.16b
	add	v1.16b,v1.16b,v4.16b
	stp	q0,q1,[x0],32
	subs	x1,x1,32
	b.hi	.dim
	mov	x0,GP_PX+4			; ombre, panneau
	mov	x1,GP_PY+8
	mov	x2,GP_PW
	mov	x3,GP_PH
	mov	x4,22
	mov	w5,0
	mov	w6,0
	mov	x7,110
	mov	w8,0
	bl	ga_panel
	mov	x0,GP_PX
	mov	x1,GP_PY
	mov	x2,GP_PW
	mov	x3,GP_PH
	mov	x4,22
	movl	w5,0x00262E44
	movl	w6,0x00141926
	mov	x7,245
	movl	w8,0x00586888
	bl	ga_panel
	adr	x0,gp_txt
	ldr	x0,[x0,GPT_TITLE*8]
	mov	x1,GF_W/2
	mov	x2,GP_PY+22
	bl	ga_show
	mov	x19,0
.it:	mov	x0,GP_IS
	mul	x20,x19,x0
	add	x20,x20,GP_IY
	ldv	x21,gp_sel
	cmp	x21,x19
	b.ne	.plain
	mov	x0,GP_IX
	mov	x1,x20
	mov	x2,GP_IW
	mov	x3,GP_IH
	mov	x4,GP_IH/2
	movl	w5,0x005A9CFF
	movl	w6,0x002F68E0
	mov	x7,256
	movl	w8,0x00A8C8FF
	bl	ga_panel
	adr	x0,gp_txt
	add	x0,x0,GPT_ON*8
	b	.lbl
.plain:	mov	x0,GP_IX
	mov	x1,x20
	mov	x2,GP_IW
	mov	x3,GP_IH
	mov	x4,GP_IH/2
	movl	w5,0x00303A52
	movl	w6,0x00262E44
	mov	x7,256
	mov	w8,0
	bl	ga_panel
	adr	x0,gp_txt
	add	x0,x0,GPT_OFF*8
.lbl:	ldr	x0,[x0,x19,lsl 3]
	ldr	w1,[x0,4]
	mov	x2,GP_IH
	sub	x2,x2,x1
	add	x2,x20,x2,asr 1
	mov	x1,GF_W/2
	bl	ga_show
	add	x19,x19,1
	cmp	x19,GP_N
	b.lo	.it
	adr	x2,gp_txt
	ldr	x2,[x2,GPT_HINT*8]
	mov	x0,GF_W/2
	mov	x1,GP_PY+GP_PH-26
	bl	ga_ablit_c
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

	align 4
; textes : dh decalage, db corps, gras, dw deux couleurs
macro gp_txe s, px, bold, c1, c2 {
	dh	s-gp_s_base
	db	px, bold
	dw	c1, c2
}
GPT_TITLE	= 0
GPT_ON		= 1
GPT_OFF		= GPT_ON+GP_N
GPT_HINT	= GPT_OFF+GP_N
GP_NTX		= GPT_HINT+1
gp_tx:
	gp_txe	gp_s_pause,28,1,0x00FFFFFF,0x00C8D8F0
	gp_txe	gp_s_resume,17,1,0x00FFFFFF,0x00F0F4FF
	gp_txe	gp_s_new,17,1,0x00FFFFFF,0x00F0F4FF
	gp_txe	gp_s_quit,17,1,0x00FFFFFF,0x00F0F4FF
	gp_txe	gp_s_resume,17,0,0x00C8D0E0,0x00A8B0C4
	gp_txe	gp_s_new,17,0,0x00C8D0E0,0x00A8B0C4
	gp_txe	gp_s_quit,17,0,0x00C8D0E0,0x00A8B0C4
	gp_txe	gp_s_hint,12,0,0x008890A8,0x00788098
gp_s_base:
gp_s_pause	db 'Pause',0
gp_s_resume	db 'Reprendre',0
gp_s_new	db 'Recommencer',0
gp_s_quit	db 'Quitter',0
gp_s_hint	db 'A : valider    B : retour',0
	align 4

macro gp_data {
		align 8
gp_fn		rd 3			; pause, reprise, nouvelle partie
gp_bx		rd 1
gp_by		rd 1
gp_on		rd 1
gp_sel		rd 1
gp_down		rd 1
gp_now		rd 1
gp_new		rd 1
gp_txt		rd GP_NTX
}
