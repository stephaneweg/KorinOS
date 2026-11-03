; ============================================================================
;  fmt_ins.inc - FM Tracker : les dialogues (instrument, prereglages, infos)
;
;  L'editeur d'instrument : son nom, un prereglage, l'algorithme (FM :
;  l'operateur 1 module le 2 ; additif : on entend les deux) et la retroaction,
;  puis pour chaque operateur sa forme d'onde (quatre dessinees, un clic), son
;  enveloppe dessinee, ses glissieres et ses cases. Tester le joue ; chaque
;  changement aussi. Tout est calcule en entiers (sinus en table).
;
;  Un dialogue a sa propre boucle, modal_run : la lecture continue (tick), la
;  note d'essai s'arrete, l'onde s'anime. La fenetre principale n'est pas
;  repeinte pendant ce temps (g_in_dlg).
; ============================================================================

DI_OK		= 1
DI_CANCEL	= 2
DI_TEST		= 3
DI_LOAD		= 4
DI_SAVE		= 5
DI_PRESET	= 6
DI_NAME		= 7
DI_CTRL		= 8				; glissieres, cases, algorithme

DLG_W		= 664
DLG_H		= 530
COLW		= 308
D_YB		= 50				; rangee de l'algorithme
D_YO		= 90				; haut des operateurs
D_YS		= 202				; premieres glissieres
D_RH		= 22				; pas des glissieres
D_YC		= D_YS + 7*D_RH + 4		; cases
D_YW		= D_YC + 2*D_RH + 22		; onde de sortie
D_WH		= 52
OFF_T		= 7000				; touche relachee (1/10 ms)

; ---- sinus, ondes ----------------------------------------------------------

; table du sinus (1024 pas, 4096 = 1) par recurrence : s(n+1) = 2 cos(w) s(n) - s(n-1)
sin_init:
	adr	x0,sin_tab
	adr	x1,sin_c
	ldp	d0,d1,[x1]			; 2 cos(w), sin(w)
	ldr	d2,[x1,16]			; 4096
	fmov	d3,xzr				; s(n-1)
	fmov	d4,d1				; s(n)
	strh	wzr,[x0],2
	mov	x2,1023
.l:	fmul	d5,d4,d2
	fcvtns	w3,d5
	strh	w3,[x0],2
	fmul	d5,d4,d0
	fsub	d5,d5,d3
	fmov	d3,d4
	fmov	d4,d5
	subs	x2,x2,1
	b.ne	.l
	ret

; x0 = forme (0..3), x1 = phase (1024 = un tour) -> x0 = valeur (-4096..4096)
wave_at:
	and	x1,x1,1023
	adr	x2,sin_tab
	ldrsh	x3,[x2,x1,lsl 1]
	ands	x0,x0,3
	b.ne	.w1
	mov	x0,x3
	ret
.w1:	cmp	x0,1
	b.ne	.w2
	cmp	x3,0
	csel	x0,x3,xzr,gt
	ret
.w2:	cmp	x3,0
	cneg	x3,x3,lt
	cmp	x0,2
	b.ne	.w3
	mov	x0,x3
	ret
.w3:	and	x1,x1,511
	cmp	x1,256
	csel	x0,x3,xzr,lo
	ret

; ---- la boucle des dialogues -----------------------------------------------

; x0 = fonction (x0 = widget, x1 = code ; 0, 0 : souris hors des widgets), x1 =
; fonction d'attente (0 : aucune) -> x0 = g_m_res (Echap, case de fermeture : 0)
modal_run:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	stv	xzr,g_m_done
	stv	xzr,g_m_res
.loop:	ldv	x0,g_m_done
	cbnz	x0,.done
	mov	x0,20
	bl	ui_event
	mov	x21,x0
	bl	tick
	cbz	x20,.ev
	blr	x20
.ev:	cbnz	x21,.got
	bl	ui_w_idle
	b	.loop
.got:	adr	x22,ui_ev
	ldr	w0,[x22,EV_WIN]
	ldv	x1,ui_win
	cmp	x0,x1
	b.eq	.mine
	cmp	x21,WEV_MOUSE			; l'autre fenetre : un clic ramene le dialogue
	b.ne	.loop
	ldr	w0,[x22,EV_KIND]
	cmp	w0,1
	b.ne	.loop
	ldv	x0,ui_win
	bl	ui_raise
	b	.loop
.mine:	cmp	x21,WEV_CLOSE
	b.eq	.cancel
	mov	x0,0
	bl	ui_w_event
	cbz	x2,.free
	cbz	x1,.loop
	blr	x19
	b	.loop
.free:	cmp	x21,WEV_MOUSE
	b.ne	.key
	mov	x0,0
	mov	x1,0
	blr	x19
	b	.loop
.key:	cmp	x21,WEV_KEY
	b.ne	.loop
	ldr	w0,[x22,EV_KIND]
	cbz	w0,.loop
	ldr	w0,[x22,EV_CODE]
	cmp	w0,KEY_ESC
	b.eq	.cancel
	cmp	w0,KEY_ENTER
	b.ne	.loop
	mov	x0,1
	stv	x0,g_m_res
	stv	x0,g_m_done
	b	.loop
.cancel:
	stv	xzr,g_m_res
.done:	ldv	x0,g_m_res
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = reponse : ferme le dialogue
modal_end:
	stv	x0,g_m_res
	mov	x0,1
	stv	x0,g_m_done
	ret

; x0..x3 = rectangle, x4 = libelle (original), x5 = UB_*, x6 = id -> poignee
dlg_button:
	prologue
	str	x19,[sp,-16]!
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	stp	x5,x6,[sp,-16]!
	mov	x0,x4
	bl	ui_tr
	mov	x4,x0
	ldp	x5,x19,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	bl	ui_pushbutton
	mov	x1,0
	mov	x2,x19
	str	x0,[sp,-16]!
	bl	ui_w_on
	ldr	x0,[sp],16
	ldr	x19,[sp],16
	epilogue

; ---- l'instrument en cours (d_ins) <-> les widgets -------------------------

; x0 = rangee (0..6), x1 = octet -> x0 = position de la glissiere
to_slider:
	cmp	x0,2				; maintien, volume : attenuations a l'envers
	b.eq	.inv
	cmp	x0,4
	b.eq	.inv
	mov	x0,x1
	ret
.inv:	adr	x2,sl_hi
	ldrb	w2,[x2,x0]
	sub	x0,x2,x1
	ret

; les widgets -> d_ins
dlg_collect:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,d_ins+INS_P
	mov	x20,0				; operateur
.o:	mov	x21,0
.r:	mov	x0,x20				; glissieres
	mov	x1,7
	mul	x0,x0,x1
	add	x0,x0,x21
	adr	x1,d_sl
	ldr	x0,[x1,x0,lsl 3]
	bl	ui_w_get_value
	mov	x1,x0
	mov	x0,x21
	bl	to_slider
	adr	x1,sl_field
	ldrb	w1,[x1,x21]
	add	x1,x1,x20
	strb	w0,[x19,x1]
	add	x21,x21,1
	cmp	x21,7
	b.lo	.r
	add	x0,x20,P_WAVE			; onde
	adr	x1,d_wave
	ldr	x1,[x1,x20,lsl 3]
	strb	w1,[x19,x0]
	mov	x21,0				; cases
.c:	lsl	x0,x20,2
	add	x0,x0,x21
	adr	x1,d_chk
	ldr	x0,[x1,x0,lsl 3]
	bl	ui_w_get_value
	adr	x1,chk_field
	ldrb	w1,[x1,x21]
	add	x1,x1,x20
	strb	w0,[x19,x1]
	add	x21,x21,1
	cmp	x21,4
	b.lo	.c
	add	x20,x20,1
	cmp	x20,2
	b.lo	.o
	ldv	x0,d_fb
	bl	ui_w_get_value
	strb	w0,[x19,P_FB]
	ldv	x0,d_conn
	bl	ui_w_get_value
	strb	w0,[x19,P_CON]
	ldv	x0,d_name
	bl	ui_w_get_text
	mov	x1,x0
	adr	x0,d_ins
	mov	x2,9
	bl	str_copy
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; d_ins -> les widgets
dlg_show:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,d_ins+INS_P
	ldv	x0,d_name
	adr	x1,d_ins
	bl	ui_w_set_text
	mov	x20,0
.o:	mov	x21,0
.r:	adr	x0,sl_field
	ldrb	w0,[x0,x21]
	add	x0,x0,x20
	ldrb	w1,[x19,x0]
	mov	x0,x21
	bl	to_slider
	mov	x22,x0
	mov	x0,x20
	mov	x1,7
	mul	x0,x0,x1
	add	x0,x0,x21
	adr	x1,d_sl
	ldr	x0,[x1,x0,lsl 3]
	mov	x1,x22
	bl	ui_w_set_value
	add	x21,x21,1
	cmp	x21,7
	b.lo	.r
	add	x0,x20,P_WAVE
	ldrb	w0,[x19,x0]
	and	x0,x0,3
	adr	x1,d_wave
	str	x0,[x1,x20,lsl 3]
	mov	x21,0
.c:	adr	x0,chk_field
	ldrb	w0,[x0,x21]
	add	x0,x0,x20
	ldrb	w1,[x19,x0]
	cmp	w1,0
	cset	x22,ne
	lsl	x0,x20,2
	add	x0,x0,x21
	adr	x1,d_chk
	ldr	x0,[x1,x0,lsl 3]
	mov	x1,x22
	bl	ui_w_set_value
	add	x21,x21,1
	cmp	x21,4
	b.lo	.c
	add	x20,x20,1
	cmp	x20,2
	b.lo	.o
	ldv	x0,d_fb
	ldrb	w1,[x19,P_FB]
	and	x1,x1,7
	bl	ui_w_set_value
	ldv	x0,d_conn
	ldrb	w1,[x19,P_CON]
	and	x1,x1,1
	bl	ui_w_set_value
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; d_ins a la voix d'essai, joue
dlg_sound:
	prologue
	adr	x0,g_akins
	adr	x1,d_ins+INS_P
	bl	ak_fm_from_fms
	ldv	x0,g_fm
	mov	x1,TEST_VOICE
	adr	x2,g_akins
	bl	ak_fm_instrument
	mov	x0,1
	mov	x1,0
	ldv	x2,g_oct
	bl	make_note
	bl	note_mhz
	mov	x2,x0
	ldv	x0,g_fm
	mov	x1,TEST_VOICE
	mov	x3,AKW_FM
	mov	x4,220
	bl	ak_fm_start
	mov	x0,TEST_VOICE
	stv	x0,g_pv_voice
	sys	SYS_TIME_MS
	add	x0,x0,TEST_MS
	stv	x0,g_pv_end
	epilogue

; Tester : la note, l'onde qui la suit
dlg_play:
	prologue
	bl	dlg_sound
	mov	x0,1
	stv	x0,g_anim
	sys	SYS_TIME_MS
	stv	x0,g_anim_t0
	stv	x0,g_anim_t
	epilogue

; (attente du dialogue) l'onde suit la note
dlg_idle:
	prologue
	ldv	x0,g_anim
	cbz	x0,.o
	sys	SYS_TIME_MS
	ldv	x1,g_anim_t0
	sub	x2,x0,x1
	cmp	x2,1700
	b.ls	.go
	stv	xzr,g_anim
	bl	dlg_wave_title
	b	.draw
.go:	ldv	x1,g_anim_t
	sub	x1,x0,x1
	cmp	x1,40
	b.lo	.o
	stv	x0,g_anim_t
.draw:	bl	dlg_output_wave
	mov	x0,0
	mov	x1,D_YW-20
	mov	x2,DLG_W
	mov	x3,D_WH+20
	bl	ui_update
.o:	epilogue

; ---- dessin ----------------------------------------------------------------

; x0 = operateur -> x0 = x de sa colonne
col_x:
	mov	x1,COLW+16
	mul	x0,x0,x1
	add	x0,x0,16
	ret

; x0 = operateur : les quatre ondes
draw_waves:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,0				; forme
.k:	mov	x0,x19
	bl	col_x
	mov	x1,77
	mul	x1,x1,x20
	add	x21,x0,x1			; x
	adr	x0,d_wave
	ldr	x0,[x0,x19,lsl 3]
	cmp	x0,x20
	cset	x22,eq
	ldc	w23,c_field
	ldc	w24,c_gnum
	cbz	x22,.bx
	mov	w0,w23
	ldc	w1,c_acc
	mov	x2,50
	bl	ui_mix
	mov	w23,w0
	ldc	w24,c_acc
.bx:	mov	x1,x21
	mov	x2,D_YO+20
	mov	x3,71
	mov	x4,30
	mov	x5,5
	mov	w6,w23
	bl	sunken
	cbz	x22,.dr
	mov	x0,x21				; choisie : cadre accent
	mov	x1,D_YO+20
	mov	x2,71
	mov	x3,2
	mov	w4,w24
	bl	ui_fill
.dr:	mov	x25,-1				; y precedent
	mov	x26,0				; colonne
.i:	cmp	x26,59
	b.hi	.nk
	mov	x0,2048				; deux periodes
	mul	x1,x26,x0
	mov	x0,59
	udiv	x1,x1,x0
	mov	x0,x20
	bl	wave_at
	mov	x1,9				; amplitude
	mul	x0,x0,x1
	asr	x0,x0,12
	mov	x1,D_YO+35
	sub	x27,x1,x0			; y
	mov	x0,x27
	mov	x1,x27
	tbnz	x25,63,.sg
	cmp	x25,x27
	csel	x0,x25,x27,lt
	csel	x1,x27,x25,lt
.sg:	mov	x28,x1
	mov	x1,x0
	add	x0,x21,6
	add	x0,x0,x26
	mov	x2,2
	sub	x3,x28,x1
	add	x3,x3,2
	mov	w4,w24
	bl	ui_fill
	mov	x25,x27
	add	x26,x26,1
	b	.i
.nk:	add	x20,x20,1
	cmp	x20,4
	b.lo	.k
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0..x3 = bouts (pixels) : un trait de 2 px, couleur accent
seg_line:
	prologue
	lsl	x0,x0,4
	lsl	x1,x1,4
	lsl	x2,x2,4
	lsl	x3,x3,4
	mov	x4,32
	ldc	w5,c_acc
	bl	ui_vp_line
	epilogue

; x0 = operateur : son enveloppe (monte au volume, redescend au maintien, tenue
; ou chutant encore, puis le relachement une fois la touche lachee)
draw_env:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	bl	col_x
	mov	x20,x0				; x de la colonne
	mov	x1,x20
	mov	x2,D_YO+54
	mov	x3,COLW
	mov	x4,54
	mov	x5,5
	ldc	w6,c_field
	bl	sunken
	adr	x0,d_ins+INS_P
	add	x0,x0,x19
	ldrb	w1,[x0,P_AR]			; x21 ar, x22 dr, x23 sl, x24 rr
	and	x21,x1,15
	ldrb	w1,[x0,P_DR]
	and	x22,x1,15
	ldrb	w1,[x0,P_SL]
	and	x23,x1,15
	ldrb	w1,[x0,P_RR]
	and	x24,x1,15
	ldrb	w1,[x0,P_TL]
	and	x25,x1,63
	mov	x1,54-22			; haut = y + 8 + tl * (h - 22) / 63
	mul	x25,x25,x1
	mov	x1,63
	udiv	x25,x25,x1
	add	x25,x25,D_YO+54+8		; x25 = haut
	mov	x26,D_YO+54+54-7		; x26 = bas
	sub	x0,x26,x25			; y du maintien
	mul	x0,x0,x23
	mov	x1,15
	udiv	x0,x0,x1
	add	x23,x25,x0			; x23 = sy
	mov	x1,15				; xa, xd, xs, xr
	sub	x0,x1,x21
	mov	x2,3
	mul	x0,x0,x2
	add	x21,x20,10
	add	x21,x21,x0			; xa
	sub	x0,x1,x22
	mul	x0,x0,x2
	add	x22,x21,6
	add	x22,x22,x0			; xd
	add	x27,x22,70			; xs
	sub	x0,x1,x24
	lsl	x0,x0,2
	add	x24,x27,6
	add	x24,x24,x0			; xr
	add	x0,x20,COLW-8
	cmp	x24,x0
	csel	x24,x0,x24,gt
	mov	x28,x23				; ey : pas tenu, ca continue de chuter
	adr	x0,d_ins+INS_P+P_EGT
	ldrb	w0,[x0,x19]
	cbnz	w0,.h
	sub	x0,x26,x23
	lsr	x0,x0,1
	add	x28,x23,x0
.h:	add	x0,x20,6			; ligne de base, touche lachee
	add	x1,x26,1
	mov	x2,COLW-12
	mov	x3,1
	ldc	w4,c_gline
	bl	ui_fill
	mov	x0,x27
	mov	x1,D_YO+54+6
	mov	x2,1
	sub	x3,x26,D_YO+54+5
	ldc	w4,c_gline
	bl	ui_fill
	add	x0,x20,10
	mov	x1,x26
	mov	x2,x21
	mov	x3,x25
	bl	seg_line
	mov	x0,x21
	mov	x1,x25
	mov	x2,x22
	mov	x3,x23
	bl	seg_line
	mov	x0,x22
	mov	x1,x23
	mov	x2,x27
	mov	x3,x28
	bl	seg_line
	mov	x0,x27
	mov	x1,x28
	mov	x2,x24
	mov	x3,x26
	bl	seg_line
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = operateur, x1 = t (ms depuis l'appui ; touche lachee a 700 ms) -> x0 =
; son niveau (0..4096) : esquisse de l'enveloppe
env_at:
	adr	x2,d_ins+INS_P
	add	x2,x2,x0
	ldrb	w3,[x2,P_AR]
	and	x3,x3,15
	adr	x4,rate_a
	ldr	w3,[x4,x3,lsl 2]		; ta
	ldrb	w5,[x2,P_DR]
	and	x5,x5,15
	adr	x4,rate_d
	ldr	w5,[x4,x5,lsl 2]		; td
	ldrb	w6,[x2,P_RR]
	and	x6,x6,15
	ldr	w6,[x4,x6,lsl 2]		; tr
	ldrb	w7,[x2,P_SL]
	and	x7,x7,15
	adr	x4,sus_tab
	ldrh	w7,[x4,x7,lsl 1]		; maintien
	ldrb	w8,[x2,P_EGT]
	mov	x9,10
	mul	x1,x1,x9			; t (1/10 ms)
	mov	x9,OFF_T
	cmp	x1,x9
	csel	x10,x9,x1,hi			; held
	cmp	x10,x3
	b.hs	.after
	mov	x0,4096
	mul	x0,x0,x10
	udiv	x0,x0,x3
	b	.rel
.after:	sub	x10,x10,x3
	cmp	x10,x5
	b.hs	.sus
	mov	x0,4096
	sub	x0,x0,x7
	mul	x0,x0,x10
	udiv	x0,x0,x5
	mov	x2,4096
	sub	x0,x2,x0
	b	.rel
.sus:	mov	x0,x7
	cbnz	w8,.rel
	sub	x10,x10,x5
	cmp	x10,x6
	b.hs	.zero
	sub	x10,x6,x10
	mul	x0,x0,x10
	udiv	x0,x0,x6
.rel:	cmp	x1,x9
	b.ls	.r
	sub	x10,x1,x9
	cmp	x10,x6
	b.hs	.zero
	sub	x10,x6,x10
	mul	x0,x0,x10
	udiv	x0,x0,x6
.r:	ret
.zero:	mov	x0,0
	ret

; x0 = operateur -> x0 = amplitude (0..4096) : volume, et enveloppe en animation
op_amp:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,d_ins+INS_P+P_TL
	ldrb	w1,[x1,x19]
	and	x1,x1,63
	adr	x2,tl_tab
	ldrh	w20,[x2,x1,lsl 1]
	ldv	x0,g_anim
	cbz	x0,.o
	sys	SYS_TIME_MS
	ldv	x1,g_anim_t0
	sub	x1,x0,x1
	mov	x0,x19
	bl	env_at
	mul	x20,x20,x0
	lsr	x20,x20,12
.o:	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

; le titre de l'onde
dlg_wave_title:
	prologue
	mov	x0,16
	mov	x1,D_YW-20
	mov	x2,DLG_W-32
	mov	x3,20
	ldc	w4,c_face
	bl	ui_fill
	adr	x0,s_wave_static
	ldv	x1,g_anim
	cbz	x1,.t
	adr	x0,s_wave_anim
.t:	bl	ui_tr
	mov	x1,16
	mov	x2,D_YW-6
	ldc	w3,c_text
	bl	txtb
	epilogue

; L'onde du son sur deux periodes : l'operateur 1 (reinjecte en lui-meme) courbe
; la phase du 2 (FM) ou s'y ajoute ; chacun a son volume et, pendant Tester, au
; niveau de son enveloppe.
dlg_output_wave:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,64
	mov	x1,16
	mov	x2,D_YW
	mov	x3,DLG_W-32
	mov	x4,D_WH
	mov	x5,5
	ldc	w6,c_field
	bl	sunken
	mov	x0,22				; l'axe
	mov	x1,D_YW+D_WH/2
	mov	x2,DLG_W-44
	mov	x3,1
	ldc	w4,c_gline
	bl	ui_fill
	mov	x0,0
	bl	op_amp
	str	x0,[sp]				; a0
	mov	x0,1
	bl	op_amp
	str	x0,[sp,8]			; a1
	adr	x20,d_ins+INS_P
	ldrb	w0,[x20,P_MULT]			; m0 (0 : 1/2), en demis
	and	x0,x0,15
	lsl	x0,x0,1
	cbnz	x0,.m0
	mov	x0,1
.m0:	str	x0,[sp,16]
	ldrb	w0,[x20,P_MULT+1]
	and	x0,x0,15
	lsl	x0,x0,1
	cbnz	x0,.m1
	mov	x0,1
.m1:	str	x0,[sp,24]
	ldrb	w0,[x20,P_FB]
	and	x0,x0,7
	mov	x1,0				; profondeur de la retroaction
	cbz	x0,.f
	mov	x1,16
	lsl	x1,x1,x0
.f:	str	x1,[sp,32]
	ldrb	w21,[x20,P_CON]
	and	x21,x21,1
	ldrb	w22,[x20,P_WAVE]
	ldrb	w23,[x20,P_WAVE+1]
	mov	x24,0				; i
	mov	x25,0				; m1
	mov	x26,0				; m2
	mov	x27,-1				; y precedent
	movl	x28,(DLG_W-44)*8			; n
.l:	cmp	x24,x28
	b.hi	.e
	ldr	x0,[sp,16]			; phase de l'operateur 1
	mul	x0,x0,x24
	lsl	x0,x0,10
	udiv	x0,x0,x28
	add	x1,x25,x26			; + retroaction
	asr	x1,x1,1
	ldr	x2,[sp,32]
	mul	x1,x1,x2
	asr	x1,x1,12
	add	x1,x0,x1
	mov	x0,x22
	bl	wave_at
	ldr	x1,[sp]
	mul	x0,x0,x1
	asr	x0,x0,12			; mo
	mov	x26,x25
	mov	x25,x0
	ldr	x0,[sp,24]			; phase de l'operateur 2
	mul	x0,x0,x24
	lsl	x0,x0,10
	udiv	x1,x0,x28
	cbnz	x21,.add
	asr	x0,x25,1			; FM
	add	x1,x1,x0
	mov	x0,x23
	bl	wave_at
	ldr	x1,[sp,8]
	mul	x0,x0,x1
	asr	x0,x0,12
	b	.out
.add:	mov	x0,x23
	bl	wave_at
	ldr	x1,[sp,8]
	mul	x0,x0,x1
	asr	x0,x0,12
	add	x0,x0,x25
	asr	x0,x0,1
.out:	and	x1,x24,7
	cbnz	x1,.n
	mov	x1,D_WH/2-7			; une colonne
	mul	x0,x0,x1
	asr	x0,x0,12
	mov	x1,D_YW+D_WH/2
	sub	x19,x1,x0			; y
	mov	x0,x19
	mov	x1,x19
	tbnz	x27,63,.sg
	cmp	x27,x19
	csel	x0,x27,x19,lt
	csel	x1,x19,x27,lt
.sg:	mov	x2,x1
	mov	x1,x0
	sub	x3,x2,x1
	add	x3,x3,2
	lsr	x0,x24,3
	add	x0,x0,22
	mov	x2,2
	ldc	w4,c_acc
	bl	ui_fill
	mov	x27,x19
.n:	add	x24,x24,1
	b	.l
.e:	add	sp,sp,64
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, x2 = texte : une boite d'operateur du dessin de l'algorithme
algo_box:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x1,x19
	mov	x2,x20
	mov	x3,50
	mov	x4,22
	mov	x5,4
	ldc	w6,c_field
	bl	sunken
	mov	x0,x21
	bl	ui_tr
	add	x1,x19,12
	add	x2,x20,16
	ldc	w3,c_text
	bl	txt
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les dessins de l'algorithme
draw_algo:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,270
	mov	x1,D_YB-12
	mov	x2,170
	mov	x3,50
	ldc	w4,c_face
	bl	ui_fill
	mov	x19,276				; ax
	mov	x20,D_YB+2			; ay
	adr	x0,d_ins+INS_P+P_CON
	ldrb	w0,[x0]
	tbz	w0,0,.fm
	mov	x0,x19				; additif : les deux a la sortie
	sub	x1,x20,10
	adr	x2,s_op1
	bl	algo_box
	mov	x0,x19
	add	x1,x20,14
	adr	x2,s_op2
	bl	algo_box
	add	x0,x19,50
	add	x1,x20,1
	add	x2,x19,70
	add	x3,x20,1
	bl	seg_line
	add	x0,x19,50
	add	x1,x20,25
	add	x2,x19,70
	add	x3,x20,25
	bl	seg_line
	add	x0,x19,70
	add	x1,x20,1
	add	x2,x19,70
	add	x3,x20,25
	bl	seg_line
	add	x0,x19,70
	add	x1,x20,13
	add	x2,x19,92
	add	x3,x20,13
	bl	seg_line
	add	x1,x19,98
	b	.out
.fm:	mov	x0,x19
	mov	x1,x20
	adr	x2,s_op1
	bl	algo_box
	add	x0,x19,70
	mov	x1,x20
	adr	x2,s_op2
	bl	algo_box
	add	x0,x19,50
	add	x1,x20,11
	add	x2,x19,70
	add	x3,x20,11
	bl	seg_line
	add	x0,x19,120
	add	x1,x20,11
	add	x2,x19,136
	add	x3,x20,11
	bl	seg_line
	add	x1,x19,140
.out:	adr	x0,s_out
	add	x2,x20,16
	ldc	w3,c_text2
	bl	txt
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte original, x1 = x, x2 = base
d_label:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	bl	ui_tr
	mov	x1,x19
	mov	x2,x20
	ldc	w3,c_text
	bl	txt
	ldp	x19,x20,[sp],16
	epilogue

; tout le dialogue
dlg_paint:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,32
	mov	x0,0
	mov	x1,0
	mov	x2,DLG_W
	mov	x3,DLG_H
	ldc	w4,c_face
	bl	ui_fill
	adr	x0,s_name
	mov	x1,16
	mov	x2,12+18
	bl	d_label
	adr	x0,s_algorithm
	mov	x1,16
	mov	x2,D_YB+18
	bl	d_label
	bl	draw_algo
	adr	x0,s_feedback
	mov	x1,442
	mov	x2,D_YB+18
	bl	d_label
	adr	x20,d_ins+INS_P
	ldrb	w1,[x20,P_FB]
	and	x1,x1,7
	mov	x0,sp
	bl	fmi_put
	strb	wzr,[x0]
	mov	x0,sp
	mov	x1,624
	mov	x2,D_YB+18
	ldc	w3,c_text
	bl	txt
	mov	x19,0				; operateurs
.op:	mov	x0,x19
	bl	col_x
	mov	x21,x0
	ldrb	w0,[x20,P_CON]
	tbz	w0,0,.fm
	adr	x0,s_op_1a
	cbz	x19,.t
	adr	x0,s_op_2a
	b	.t
.fm:	adr	x0,s_op_1f
	cbz	x19,.t
	adr	x0,s_op_2f
.t:	bl	ui_tr
	mov	x1,x21
	mov	x2,D_YO+14
	ldc	w3,c_text
	bl	txtb
	mov	x0,x19
	bl	draw_waves
	mov	x0,x19
	bl	draw_env
	mov	x22,0				; rangees
.r:	adr	x0,sl_names
	ldr	w0,[x0,x22,lsl 2]
	adr	x1,sl_names
	add	x0,x0,x1
	mov	x1,x21
	mov	x2,D_RH
	mul	x2,x2,x22
	add	x2,x2,D_YS+15
	bl	d_label
	mov	x0,x19				; la valeur
	mov	x1,7
	mul	x0,x0,x1
	add	x0,x0,x22
	adr	x1,d_sl
	ldr	x0,[x1,x0,lsl 3]
	bl	ui_w_get_value
	mov	x1,x0
	cmp	x22,5				; multiplicateur : x1/2, x1, x2...
	b.ne	.v
	cbnz	x1,.vm
	adr	x0,s_half
	b	.vt
.vm:	mov	w2,'x'
	strb	w2,[sp]
	add	x0,sp,1
	bl	fmi_put
	b	.vz
.v:	mov	x0,sp
	bl	fmi_put
.vz:	strb	wzr,[x0]
	mov	x0,sp
.vt:	add	x1,x21,COLW-30
	mov	x2,D_RH
	mul	x2,x2,x22
	add	x2,x2,D_YS+15
	ldc	w3,c_text
	bl	txt
	add	x22,x22,1
	cmp	x22,7
	b.lo	.r
	add	x19,x19,1
	cmp	x19,2
	b.lo	.op
	bl	dlg_wave_title
	bl	dlg_output_wave
	adr	x0,s_test_hint
	bl	ui_tr
	mov	x1,118
	mov	x2,DLG_H-22
	ldc	w3,c_text2
	bl	txt
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	add	sp,sp,32
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---- les evenements de l'editeur -------------------------------------------

; souris hors des widgets : les ondes
dlg_click:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,1
	b.ne	.out
	ldrsw	x19,[x0,EV_X]
	ldrsw	x20,[x0,EV_Y]
	sub	x1,x20,D_YO+20
	cmp	x1,30
	b.hs	.out
	mov	x3,0
.o:	mov	x0,x3
	bl	col_x
	subs	x1,x19,x0
	b.mi	.n
	cmp	x1,COLW
	b.hs	.n
	mov	x2,77
	udiv	x1,x1,x2
	cmp	x1,3
	b.hi	.out
	adr	x0,d_wave
	str	x1,[x0,x3,lsl 3]
	bl	dlg_changed
	b	.out
.n:	add	x3,x3,1
	cmp	x3,2
	b.lo	.o
.out:	ldp	x19,x20,[sp],16
	epilogue

; une commande a change : le dessin, le son
dlg_changed:
	prologue
	bl	dlg_collect
	bl	dlg_paint
	bl	dlg_sound
	epilogue

; x0 = widget (0 : souris libre), x1 = code
dlg_on:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	cbnz	x0,.w
	bl	dlg_click
	b	.out
.w:	mov	x20,x1
	bl	ui_w_user
	mov	x19,x0
	cmp	x19,DI_CTRL
	b.ne	.btn
	cmp	x20,UN_CHANGE
	b.ne	.out
	bl	dlg_changed
	b	.out
.btn:	cmp	x20,UN_ACTIVATE
	b.ne	.out
	cmp	x19,DI_OK
	b.ne	.c
	bl	dlg_collect
	mov	x0,1
	bl	modal_end
	b	.out
.c:	cmp	x19,DI_CANCEL
	b.ne	.t
	mov	x0,0
	bl	modal_end
	b	.out
.t:	cmp	x19,DI_TEST
	b.ne	.l
	bl	dlg_collect
	bl	dlg_play
	b	.out
.l:	cmp	x19,DI_LOAD
	b.ne	.s
	adr	x0,s_ins_dir
	adr	x1,g_dlgpath
	mov	x2,256
	bl	ui_file_open
	cbz	x0,.pt
	adr	x0,g_dlgpath
	adr	x1,g_ins_tmp
	bl	read_fmi
	cbnz	x0,.ok
	adr	x0,s_instrument
	bl	ui_tr
	mov	x19,x0
	adr	x0,s_not_fmi
	bl	ui_tr
	mov	x1,x0
	mov	x0,x19
	mov	x2,UM_OK
	bl	ui_message
	b	.pt
.ok:	bl	dlg_take
	b	.pt
.s:	cmp	x19,DI_SAVE
	b.ne	.p
	bl	dlg_collect
	adr	x0,g_tmp			; nom propose
	adr	x1,d_ins
	mov	x2,9
	bl	str_copy
	adr	x0,g_tmp
	ldrb	w1,[x0]
	cbnz	w1,.nm
	adr	x1,s_instr_name
	mov	x2,9
	bl	str_copy
.nm:	adr	x0,g_tmp
	adr	x1,s_ext_fmi
	bl	ensure_ext
	adr	x0,s_ins_dir
	adr	x1,g_tmp
	adr	x2,g_dlgpath
	mov	x3,256
	bl	ui_file_save
	cbz	x0,.pt
	adr	x0,g_dlgpath
	adr	x1,s_ext_fmi
	bl	ensure_ext
	adr	x0,d_ins
	adr	x1,g_fmibuf
	bl	fmi_write
	mov	x2,x0
	adr	x0,g_dlgpath
	adr	x1,g_fmibuf
	bl	write_file
	tbz	x0,63,.pt
	adr	x0,s_instrument
	bl	ui_tr
	mov	x19,x0
	adr	x0,s_cant_write
	bl	ui_tr
	mov	x1,x0
	mov	x0,x19
	mov	x2,UM_OK
	bl	ui_message
	b	.pt
.p:	cmp	x19,DI_PRESET
	b.ne	.out
	bl	preset_dialog
.pt:	bl	dlg_paint
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; g_ins_tmp devient l'instrument en cours
dlg_take:
	prologue
	adr	x0,d_ins
	adr	x1,g_ins_tmp
	mov	x2,INS_SIZE
	bl	mem_move
	bl	dlg_show
	bl	dlg_play
	epilogue

; x0 = canal : l'editeur d'instrument
edit_instrument:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	stv	x19,g_dch
	mov	x0,x19
	bl	ins_ptr
	mov	x1,x0
	adr	x0,d_ins
	mov	x2,INS_SIZE
	bl	mem_move
	adr	x0,s_ins_of
	bl	ui_tr
	mov	x1,x0
	adr	x0,g_tmp
	add	x2,x19,1
	bl	fmt
	mov	x0,1
	stv	x0,g_in_dlg
	stv	xzr,g_anim
	mov	x0,DLG_W
	mov	x1,DLG_H
	adr	x2,g_tmp
	bl	ui_dialog_begin
	cbnz	x0,.mk
	stv	xzr,g_in_dlg
	b	.out
.mk:	stv	x0,g_dlg
	mov	x0,64				; nom
	mov	x1,12
	mov	x2,110
	mov	x3,28
	mov	x4,0
	bl	ui_textbox
	stv	x0,d_name
	mov	x1,0
	mov	x2,8
	mov	x3,0
	mov	x4,0
	bl	ui_w_set_range
	ldv	x0,d_name
	mov	x1,0
	mov	x2,DI_NAME
	bl	ui_w_on
	mov	x0,186				; prereglages, charger, enregistrer
	mov	x1,12
	mov	x2,190
	mov	x3,28
	adr	x4,s_preset_btn
	mov	x5,0
	mov	x6,DI_PRESET
	bl	dlg_button
	mov	x0,DLG_W-228
	mov	x1,12
	mov	x2,96
	mov	x3,28
	adr	x4,s_load
	mov	x5,0
	mov	x6,DI_LOAD
	bl	dlg_button
	mov	x0,DLG_W-128
	mov	x1,12
	mov	x2,112
	mov	x3,28
	adr	x4,s_save_btn
	mov	x5,0
	mov	x6,DI_SAVE
	bl	dlg_button
	adr	x0,s_additive			; algorithme
	bl	ui_tr
	mov	x1,x0
	adr	x0,g_tmp
	strb	wzr,[x0]
	mov	x19,x1
	adr	x1,s_fm_bar
	bl	str_cat
	mov	x1,x19
	bl	str_cat
	mov	x0,96
	mov	x1,D_YB
	mov	x2,170
	mov	x3,26
	adr	x4,g_tmp
	bl	ui_segmented
	stv	x0,d_conn
	mov	x1,0
	mov	x2,DI_CTRL
	bl	ui_w_on
	mov	x0,524				; retroaction
	mov	x1,D_YB+3
	mov	x2,92
	mov	x3,20
	mov	x4,0
	mov	x5,7
	mov	x6,0
	bl	ui_slider
	stv	x0,d_fb
	mov	x1,0
	mov	x2,DI_CTRL
	bl	ui_w_on
	mov	x19,0				; operateurs
.op:	mov	x0,x19
	bl	col_x
	mov	x20,x0
	mov	x21,0
.r:	add	x0,x20,92			; glissieres
	mov	x1,D_RH
	mul	x1,x1,x21
	add	x1,x1,D_YS
	mov	x2,COLW-128
	mov	x3,20
	mov	x4,0
	adr	x5,sl_hi
	ldrb	w5,[x5,x21]
	mov	x6,0
	bl	ui_slider
	mov	x22,x0
	mov	x1,7
	mul	x1,x19,x1
	add	x1,x1,x21
	adr	x2,d_sl
	str	x0,[x2,x1,lsl 3]
	mov	x1,0
	mov	x2,DI_CTRL
	bl	ui_w_on
	add	x21,x21,1
	cmp	x21,7
	b.lo	.r
	mov	x21,0
.c:	adr	x0,chk_names			; cases
	ldr	w0,[x0,x21,lsl 2]
	adr	x1,chk_names
	add	x0,x0,x1
	bl	ui_tr
	mov	x4,x0
	and	x0,x21,1
	mov	x1,150
	mul	x0,x0,x1
	add	x0,x0,x20
	lsr	x1,x21,1
	mov	x2,D_RH
	mul	x1,x1,x2
	add	x1,x1,D_YC
	mov	x2,148
	mov	x3,22
	mov	x5,0
	bl	ui_checkbox
	lsl	x1,x19,2
	add	x1,x1,x21
	adr	x2,d_chk
	str	x0,[x2,x1,lsl 3]
	mov	x1,0
	mov	x2,DI_CTRL
	bl	ui_w_on
	add	x21,x21,1
	cmp	x21,4
	b.lo	.c
	add	x19,x19,1
	cmp	x19,2
	b.lo	.op
	mov	x0,16				; boutons du bas
	mov	x1,DLG_H-42
	mov	x2,90
	mov	x3,30
	adr	x4,s_test
	mov	x5,0
	mov	x6,DI_TEST
	bl	dlg_button
	mov	x0,DLG_W-196
	mov	x1,DLG_H-42
	mov	x2,86
	mov	x3,30
	adr	x4,s_ok
	mov	x5,UB_DEFAULT
	mov	x6,DI_OK
	bl	dlg_button
	mov	x0,DLG_W-102
	mov	x1,DLG_H-42
	mov	x2,86
	mov	x3,30
	adr	x4,s_cancel
	mov	x5,0
	mov	x6,DI_CANCEL
	bl	dlg_button
	bl	dlg_show
	bl	dlg_paint
	adr	x0,dlg_on
	adr	x1,dlg_idle
	bl	modal_run
	mov	x19,x0
	ldv	x0,g_dlg
	bl	ui_dialog_end
	stv	xzr,g_in_dlg
	stv	xzr,g_anim
	ldv	x0,g_fm
	mov	x1,TEST_VOICE
	bl	ak_fm_stop
	mov	x0,-1
	stv	x0,g_pv_voice
	cbz	x19,.done
	ldv	x19,g_dch
	mov	x0,x19
	bl	ins_ptr
	adr	x1,d_ins
	mov	x2,INS_SIZE
	bl	mem_move
	mov	x0,x19
	bl	upload
	bl	set_modified
.done:	mov	x0,-1				; (le pointeur a bouge)
	stv	x0,g_hot_head
	mark	D_ALL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---- les prereglages -------------------------------------------------------

; x0, x1 = chaines -> x0 < 0, 0, > 0 : ordre sans casse
ci_cmp:
	mov	x2,0
.l:	ldrb	w3,[x0,x2]
	ldrb	w4,[x1,x2]
	sub	w5,w3,'a'
	cmp	w5,26
	b.hs	.u1
	sub	w3,w3,32
.u1:	sub	w5,w4,'a'
	cmp	w5,26
	b.hs	.u2
	sub	w4,w4,32
.u2:	cmp	w3,w4
	b.ne	.d
	cbz	w3,.e
	add	x2,x2,1
	b	.l
.e:	mov	x0,0
	ret
.d:	sub	w0,w3,w4
	sxtw	x0,w0
	ret

; les .FMI de SD0:/apps/fmtracker.app/ins, tries (au premier besoin)
load_presets:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x0,g_npreset
	cbnz	x0,.out
	adr	x0,s_ins_dir
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x20,0				; nombre
.rd:	cmp	x20,MAX_PRESETS
	b.hs	.close
	mov	x0,x19
	adr	x1,g_dirent
	bl	f_lire_rep
	cmp	x0,1
	b.ne	.close
	adr	x21,g_dirent
	ldr	x0,[x21,DIRENT_ATTR]
	tst	x0,ATTR_DIR
	b.ne	.rd
	mov	x0,x21
	bl	str_len
	cmp	x0,5
	b.lo	.rd
	sub	x1,x0,4
	ldrb	w2,[x21,x1]
	cmp	w2,'.'
	b.ne	.rd
	sub	x2,x0,1
	ldrb	w2,[x21,x2]
	orr	w2,w2,32
	cmp	w2,'i'
	b.ne	.rd
	strb	wzr,[x21,x1]			; sans l'extension
	mov	x0,PRESET_SIZE
	mul	x0,x0,x20
	adr	x1,g_presets
	add	x0,x0,x1
	mov	x1,x21
	mov	x2,PRESET_SIZE
	bl	str_copy
	add	x20,x20,1
	b	.rd
.close:	mov	x0,x19
	bl	f_fermer
	stv	x20,g_npreset
	mov	x19,1				; tri par insertion
.i:	cmp	x19,x20
	b.hs	.out
	mov	x21,x19
.j:	cbz	x21,.ni
	adr	x22,g_presets
	mov	x0,PRESET_SIZE
	mul	x0,x0,x21
	add	x22,x22,x0
	sub	x0,x22,PRESET_SIZE
	mov	x1,x22
	bl	ci_cmp
	cmp	x0,0
	b.le	.ni
	mov	x0,0				; echange
.sw:	sub	x1,x22,PRESET_SIZE
	ldrb	w2,[x22,x0]
	ldrb	w3,[x1,x0]
	strb	w3,[x22,x0]
	strb	w2,[x1,x0]
	add	x0,x0,1
	cmp	x0,PRESET_SIZE
	b.lo	.sw
	sub	x21,x21,1
	b	.j
.ni:	add	x19,x19,1
	b	.i
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero : ce prereglage dans g_ins_tmp -> x0 = 1 si lu
preset_read:
	prologue
	mov	x1,PRESET_SIZE
	mul	x0,x0,x1
	adr	x1,g_presets
	add	x1,x1,x0
	stp	x1,xzr,[sp,-16]!
	adr	x0,g_dlgpath
	strb	wzr,[x0]
	adr	x1,s_ins_dir
	bl	str_cat
	adr	x1,s_slash
	bl	str_cat
	ldp	x1,x2,[sp],16
	bl	str_cat
	adr	x1,s_ext_fmi
	bl	str_cat
	adr	x0,g_dlgpath
	adr	x1,g_ins_tmp
	bl	read_fmi
	epilogue

; (preset_dialog) x0 = widget, x1 = code
pd_on:
	prologue
	stp	x19,x20,[sp,-16]!
	cbz	x0,.out
	mov	x19,x0
	mov	x20,x1
	ldv	x0,d_list
	cmp	x19,x0
	b.ne	.btn
	cmp	x20,UN_ACTIVATE
	b.eq	.yes
	cmp	x20,UN_CHANGE
	b.ne	.out
	mov	x0,x19
	bl	ui_w_get_value
	tbnz	x0,63,.out
	bl	preset_read
	cbz	x0,.out
	adr	x0,d_ins
	adr	x1,g_ins_tmp
	mov	x2,INS_SIZE
	bl	mem_move
	bl	dlg_sound
	b	.out
.btn:	cmp	x20,UN_ACTIVATE
	b.ne	.out
	mov	x0,x19
	bl	ui_w_user
	cmp	x0,DI_OK
	b.ne	.no
.yes:	mov	x0,1
	bl	modal_end
	b	.out
.no:	mov	x0,0
	bl	modal_end
.out:	ldp	x19,x20,[sp],16
	epilogue

; la liste des prereglages : un clic l'essaie, OK le garde
preset_dialog:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	dlg_collect
	adr	x0,g_ins_keep			; pour Annuler
	adr	x1,d_ins
	mov	x2,INS_SIZE
	bl	mem_move
	bl	load_presets
	adr	x0,s_preset_title
	bl	ui_tr
	mov	x2,x0
	mov	x0,280
	mov	x1,430
	bl	ui_dialog_begin
	cbz	x0,.out
	mov	x19,x0
	mov	x0,12
	mov	x1,12
	mov	x2,256
	mov	x3,364
	mov	x4,UL_NOSORT
	bl	ui_list
	stv	x0,d_list
	mov	x20,x0
	adr	x1,s_empty
	mov	x2,230
	mov	x3,0
	bl	ui_list_column
	mov	x21,0
.l:	ldv	x0,g_npreset
	cmp	x21,x0
	b.hs	.btn
	mov	x0,PRESET_SIZE
	mul	x0,x0,x21
	adr	x1,g_presets
	add	x1,x1,x0
	mov	x0,x20
	mov	x2,x21
	bl	ui_list_add
	add	x21,x21,1
	b	.l
.btn:	mov	x0,280-192
	mov	x1,430-42
	mov	x2,86
	mov	x3,30
	adr	x4,s_ok
	mov	x5,UB_DEFAULT
	mov	x6,DI_OK
	bl	dlg_button
	mov	x0,280-98
	mov	x1,430-42
	mov	x2,86
	mov	x3,30
	adr	x4,s_cancel
	mov	x5,0
	mov	x6,DI_CANCEL
	bl	dlg_button
	mov	x0,0
	mov	x1,0
	mov	x2,280
	mov	x3,430
	ldc	w4,c_face
	bl	ui_fill
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	mov	x0,x20
	bl	ui_w_focus
	adr	x0,pd_on
	mov	x1,0
	bl	modal_run
	mov	x20,x0
	mov	x0,x19
	bl	ui_dialog_end
	cbnz	x20,.keep
	adr	x0,d_ins			; Annuler : l'instrument d'avant
	adr	x1,g_ins_keep
	mov	x2,INS_SIZE
	bl	mem_move
.keep:	bl	dlg_show
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---- les informations du morceau -------------------------------------------

; x0 = y, x1 = libelle, x2 = texte, x3 = longueur max, x4 = largeur -> champ
info_field:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	mov	x21,x3
	mov	x22,x4
	mov	x0,x1
	bl	ui_tr
	mov	x4,x0
	mov	x0,12
	add	x1,x19,4
	mov	x2,96
	mov	x3,20
	mov	x5,0
	bl	ui_label
	mov	x0,112
	mov	x1,x19
	mov	x2,x22
	mov	x3,28
	mov	x4,0
	bl	ui_textbox
	mov	x23,x0
	mov	x1,0
	mov	x2,x21
	mov	x3,0
	mov	x4,0
	bl	ui_w_set_range
	mov	x0,x23
	mov	x1,x20
	bl	ui_w_set_text
	mov	x0,x23
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = champ, x1 = dest, x2 = capacite
info_get:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	bl	ui_w_get_text
	mov	x1,x0
	mov	x0,x19
	mov	x2,x20
	bl	str_copy
	ldp	x19,x20,[sp],16
	epilogue

op_info:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	op_stop
	mov	x0,460
	mov	x1,190
	adr	x2,s_song_info
	bl	ui_dialog_begin
	cbz	x0,.out
	mov	x19,x0
	mov	x0,14
	adr	x1,s_title_l
	adr	x2,g_title
	mov	x3,20
	mov	x4,200
	bl	info_field
	mov	x20,x0
	mov	x0,14+34
	adr	x1,s_author_l
	adr	x2,g_author
	mov	x3,20
	mov	x4,200
	bl	info_field
	mov	x21,x0
	mov	x0,14+68
	adr	x1,s_comment_l
	adr	x2,g_comment
	mov	x3,50
	mov	x4,336
	bl	info_field
	mov	x22,x0
	mov	x0,x19
	mov	x1,0
	adr	x2,s_ok
	mov	x3,1
	mov	x4,UB_DEFAULT
	mov	x5,460-12
	bl	ui_dialog_button
	mov	x5,x0
	mov	x0,x19
	mov	x1,1
	adr	x2,s_cancel
	mov	x3,0
	mov	x4,0
	bl	ui_dialog_button
	mov	x0,x20
	bl	ui_w_focus
	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_run
	cbz	x0,.end
	mov	x0,x20
	adr	x1,g_title
	mov	x2,21
	bl	info_get
	mov	x0,x21
	adr	x1,g_author
	mov	x2,21
	bl	info_get
	mov	x0,x22
	adr	x1,g_comment
	mov	x2,51
	bl	info_get
	bl	set_modified
	mark	D_SIDE
.end:	mov	x0,x19
	bl	ui_dialog_end
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
