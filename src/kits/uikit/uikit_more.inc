; ============================================================================
;  kits/uikit_more.inc - UIKit : calendrier, selecteur de date, palette,
;  selecteur de couleur et son dialogue, arborescence
;
;	UT_CAL		calendrier d'un mois (Onyx calendar.cpp) : valeur = date
;			(annee << 16 | mois << 8 | jour)
;	UT_DATE		selecteur de date (Onyx DatePicker) : un champ
;			« AAAA-MM-JJ » qui ouvre un calendrier dessous
;	UT_PALETTE	grille de 8 x 5 couleurs (Onyx colorpick.cpp)
;	UT_COLOR	selecteur de couleur : une pastille qui ouvre la palette
;	UT_TREE		arborescence (Onyx treeview.h) : noeuds a id stable,
;			deplier / replier, selection, double-clic, clavier
;  ui_color_dialog : dialogue de couleur (palette, rouge / vert / bleu).
; ============================================================================

; ---------------------------------------------------------------------------
;  Dates
; ---------------------------------------------------------------------------

; x0 = annee, x1 = mois (1..12) -> x0 = jours du mois (feuille)
uk_month_days:
	cmp	x1,2
	b.ne	.n
	mov	x2,28				; fevrier : bissextile ?
	and	x3,x0,3
	cbnz	x3,.f
	mov	x4,100
	udiv	x5,x0,x4
	msub	x5,x5,x4,x0
	cbnz	x5,.leap
	mov	x4,400
	udiv	x5,x0,x4
	msub	x5,x5,x4,x0
	cbnz	x5,.f
.leap:	mov	x2,29
.f:	mov	x0,x2
	ret
.n:	adr	x2,uk_mdays
	sub	x1,x1,1
	ldrb	w0,[x2,x1]
	ret
uk_mdays:	db 31,28,31,30,31,30,31,31,30,31,30,31
		align 4

; x0 = annee, x1 = mois, x2 = jour -> x0 = jour de la semaine (0 lundi .. 6
; dimanche) (Sakamoto ; feuille)
uk_weekday:
	adr	x3,uk_wd_t
	sub	x4,x1,1
	ldrb	w4,[x3,x4]
	cmp	x1,3
	b.hs	.y
	sub	x0,x0,1
.y:	add	x5,x0,x0,lsr 2			; y + y/4 - y/100 + y/400
	mov	x6,100
	udiv	x6,x0,x6
	sub	x5,x5,x6
	mov	x6,400
	udiv	x6,x0,x6
	add	x5,x5,x6
	add	x5,x5,x4
	add	x5,x5,x2
	mov	x6,7				; 0 dimanche -> lundi d'abord
	udiv	x7,x5,x6
	msub	x5,x7,x6,x5
	add	x5,x5,6
	udiv	x7,x5,x6
	msub	x0,x7,x6,x5
	ret
uk_wd_t:	db 0,3,2,5,0,3,5,1,4,6,2,4
		align 4

; x0 = date (a << 16 | m << 8 | j), x1 = tampon -> « AAAA-MM-JJ » (feuille)
uk_date_text:
	prologue
	lsr	x2,x0,16			; annee : 4 chiffres
	mov	x3,1000
	mov	x5,4
.y:	udiv	x4,x2,x3
	add	w6,w4,'0'
	strb	w6,[x1],1
	msub	x2,x4,x3,x2
	mov	x6,10
	udiv	x3,x3,x6
	subs	x5,x5,1
	b.ne	.y
	mov	w6,'-'
	strb	w6,[x1],1
	ubfx	x2,x0,8,8
	bl	.two
	mov	w6,'-'
	strb	w6,[x1],1
	and	x2,x0,0xFF
	bl	.two
	strb	wzr,[x1]
	epilogue
.two:	mov	x3,10				; (x2 : deux chiffres)
	udiv	x4,x2,x3
	add	w6,w4,'0'
	strb	w6,[x1],1
	msub	x4,x4,x3,x2
	add	w6,w4,'0'
	strb	w6,[x1],1
	ret

; ---------------------------------------------------------------------------
;  Calendrier
; ---------------------------------------------------------------------------

UK_CAL_HEAD	= 30			; ligne du mois
UK_CAL_WD	= 22			; ligne des jours de la semaine

; x0..x3 = rectangle (0 : 7 x 34 sur 30 + 22 + 6 x 30), x4 = date ->
; poignee (valeur : la date choisie)
ui_calendar:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x4
	cbnz	x2,.w
	mov	x2,7*34
.w:	cbnz	x3,.h
	mov	x3,UK_CAL_HEAD+UK_CAL_WD+6*30
.h:	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_CAL
	bl	uk_w_new
	cbz	x0,.out
	str	x19,[x0,UW_VALUE]
	and	x1,x19,not 0xFF			; mois montre
	str	x1,[x0,UW_AUX2]
	mov	x1,-1
	str	x1,[x0,UW_HOT]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = calendrier, x1 = date d'aujourd'hui (entouree ; 0 : aucune)
; x0 = calendrier, x1 = date : montree et choisie (son mois suit)
ui_calendar_show:
	str	x1,[x0,UW_VALUE]
	and	x1,x1,not 0xFF
	str	x1,[x0,UW_AUX2]
	ret

ui_calendar_today:
	cbz	x0,.ret
	str	x1,[x0,UW_AUX]
	b	uk_w_refresh
.ret:	ret

uk_cal_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x0
	ldp	w20,w21,[x19,UW_X]
	sxtw	x20,w20
	sxtw	x21,w21
	ldp	w22,w23,[x19,UW_W]
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,x23
	jet	w4,TJ_SURFACE
	bl	ui_fill
	ldr	x24,[x19,UW_AUX2]		; mois montre
	lsr	x25,x24,16			; annee
	ubfx	x26,x24,8,8			; mois
	adr	x0,uk_month_names		; titre : « octobre 2026 » (TR)
	sub	x1,x26,1
.mn:	cbz	x1,.mnd
	ldrb	w2,[x0],1
	cbnz	w2,.mn
	sub	x1,x1,1
	b	.mn
.mnd:	bl	ui_tr
	mov	x1,x0
	mov	x0,sp
.cp:	ldrb	w2,[x1],1
	cbz	w2,.cpd
	strb	w2,[x0],1
	b	.cp
.cpd:	mov	w2,' '
	strb	w2,[x0],1
	mov	x1,x0
	mov	x0,x25
	bl	fmt_dec
	strb	wzr,[x0]
	bl	uk_bold
	mov	x5,x0
	mov	x0,sp
	mov	x1,x22
	mov	x2,x5
	stp	x5,x5,[sp,-16]!
	mov	x1,14
	bl	ui_text_width
	ldp	x5,x6,[sp],16
	sub	x1,x22,x0
	add	x1,x20,x1,lsr 1
	mov	x0,sp
	mov	x2,x21
	mov	x3,UK_CAL_HEAD
	jet	w4,TJ_TEXTE1
	mov	x6,0
	mov	x7,14
	bl	uk_text_box
	adr	x0,uk_s_prev			; « ‹ » et « › »
	add	x1,x20,10
	bl	uk_cal_arrow
	adr	x0,uk_s_next
	add	x1,x20,x22
	sub	x1,x1,20
	bl	uk_cal_arrow
	mov	x27,7				; largeur d'une case
	udiv	x27,x22,x27
	sub	x28,x23,UK_CAL_HEAD+UK_CAL_WD	; hauteur d'une case
	mov	x0,6
	udiv	x28,x28,x0
	mov	x24,0				; jours de la semaine (TR)
	adr	x26,uk_s_wdays
.wd:	cmp	x24,7
	b.hs	.days
	mov	x0,x26
	bl	ui_tr
	str	x0,[sp,56]
	mov	x1,12
	bl	uk_font_r
	mov	x2,x0
	ldr	x0,[sp,56]
	bl	ui_text_width
	sub	x1,x27,x0
	lsr	x1,x1,1
	madd	x1,x24,x27,x1
	add	x1,x20,x1
	ldr	x0,[sp,56]
	add	x2,x21,UK_CAL_HEAD
	mov	x3,UK_CAL_WD
	jet	w4,TJ_TEXTE3
	str	x1,[sp,48]
	bl	uk_font_r
	mov	x5,x0
	ldr	x1,[sp,48]
	ldr	x0,[sp,56]
	mov	x6,0
	mov	x7,12
	bl	uk_text_box
.ws:	ldrb	w0,[x26],1
	cbnz	w0,.ws
	add	x24,x24,1
	b	.wd
.days:	ldr	x24,[x19,UW_AUX2]		; le 1er : sa case
	lsr	x0,x24,16
	ubfx	x1,x24,8,8
	mov	x2,1
	bl	uk_weekday
	mov	x25,x0				; case du 1er
	lsr	x0,x24,16
	ubfx	x1,x24,8,8
	bl	uk_month_days
	mov	x26,x0				; jours
	mov	x24,1				; jour
.d:	cmp	x24,x26
	b.hi	.out
	add	x0,x25,x24
	sub	x0,x0,1				; case 0..41
	mov	x1,7
	udiv	x2,x0,x1
	msub	x3,x2,x1,x0			; colonne x3, ligne x2
	mul	x3,x3,x27
	add	x3,x20,x3
	mul	x2,x2,x28
	add	x2,x2,x21
	add	x2,x2,UK_CAL_HEAD+UK_CAL_WD
	stp	x2,x3,[sp,32]			; [sp+32] y, [sp+40] x
	ldr	x0,[x19,UW_AUX2]		; cette date
	orr	x0,x0,x24
	str	x0,[sp,24]
	jet	w4,TJ_TEXTE1
	str	x4,[sp,16]
	ldr	x1,[x19,UW_VALUE]		; choisie : disque ACCENT, texte blanc
	cmp	x0,x1
	b.ne	.hot
	jet	w5,TJ_ACCENT
	jet	w4,TJ_ACCENT_INK
	str	x4,[sp,16]
	b	.box
.hot:	ldr	x1,[x19,UW_HOT]			; survolee : CREUX
	cmp	x0,x1
	b.ne	.today
	jet	w5,TJ_CREUX
.box:	mov	w6,w5
	ldp	x1,x0,[sp,32]
	add	x0,x0,2
	add	x1,x1,2
	sub	x2,x27,4
	sub	x3,x28,4
	mov	x4,6
	bl	ui_rbox
.today:	ldr	x0,[sp,24]			; aujourd'hui : entouree ACCENT
	ldr	x1,[x19,UW_AUX]
	cmp	x0,x1
	b.ne	.num
	ldp	x1,x0,[sp,32]
	add	x0,x0,2
	add	x1,x1,2
	sub	x2,x27,4
	sub	x3,x28,4
	jet	w4,TJ_ACCENT
	bl	ui_frame
.num:	mov	x0,x24				; le numero, centre
	adr	x1,uk_numbuf
	bl	fmt_dec
	strb	wzr,[x0]
	bl	uk_met
	ldr	x1,[x0,MT_PX]
	str	x1,[sp,8]
	bl	uk_font_r
	mov	x2,x0
	ldr	x1,[sp,8]
	adr	x0,uk_numbuf
	bl	ui_text_width
	sub	x1,x27,x0
	ldr	x2,[sp,40]
	add	x1,x2,x1,lsr 1
	str	x1,[sp]
	bl	uk_font_r
	mov	x5,x0
	ldr	x1,[sp]
	adr	x0,uk_numbuf
	ldr	x2,[sp,32]
	mov	x3,x28
	ldr	x4,[sp,16]
	mov	x6,0
	ldr	x7,[sp,8]
	bl	uk_text_box
	add	x24,x24,1
	b	.d
.out:	add	sp,sp,64
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (uk_cal_draw) x0 = fleche, x1 = x : dans la ligne du mois, TEXTE2
uk_cal_arrow:
	prologue
	stp	x0,x1,[sp,-16]!
	bl	uk_bold
	mov	x5,x0
	ldp	x0,x1,[sp],16
	mov	x2,x21
	mov	x3,UK_CAL_HEAD
	jet	w4,TJ_TEXTE2
	mov	x6,0
	mov	x7,16
	bl	uk_text_box
	epilogue

; x0 = calendrier, x1 = -1 / +1 : mois montre precedent / suivant
uk_cal_month:
	ldr	x2,[x0,UW_AUX2]
	lsr	x3,x2,16
	ubfx	x4,x2,8,8
	add	x4,x4,x1
	cbnz	x4,.n13
	mov	x4,12
	sub	x3,x3,1
.n13:	cmp	x4,13
	b.ne	.st
	mov	x4,1
	add	x3,x3,1
.st:	lsl	x3,x3,16
	orr	x3,x3,x4,lsl 8
	str	x3,[x0,UW_AUX2]
	b	uk_w_refresh

; x0 = calendrier, x1, x2 = point local -> x0 = date de la case (0 : aucune)
uk_cal_at:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	subs	x22,x2,UK_CAL_HEAD+UK_CAL_WD
	b.lt	.no
	ldp	w3,w4,[x19,UW_W]
	mov	x5,7
	udiv	x5,x3,x5			; largeur d'une case
	sub	x4,x4,UK_CAL_HEAD+UK_CAL_WD
	mov	x6,6
	udiv	x6,x4,x6			; hauteur
	udiv	x20,x1,x5			; colonne
	cmp	x20,7
	b.hs	.no
	udiv	x21,x22,x6			; ligne
	cmp	x21,6
	b.hs	.no
	mov	x0,7
	madd	x20,x21,x0,x20			; case
	ldr	x21,[x19,UW_AUX2]
	lsr	x0,x21,16
	ubfx	x1,x21,8,8
	mov	x2,1
	bl	uk_weekday
	sub	x20,x20,x0
	add	x20,x20,1			; jour
	cmp	x20,1
	b.lt	.no
	lsr	x0,x21,16
	ubfx	x1,x21,8,8
	bl	uk_month_days
	cmp	x20,x0
	b.gt	.no
	orr	x0,x21,x20
	b	.out
.no:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris : fleches du mois, survol, clic d'un jour (double : UN_ACTIVATE)
uk_cal_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x4
	cmp	x20,3
	b.ne	.at
	mov	x0,-1
	b	.hot
.at:	cmp	x3,UK_CAL_HEAD			; ligne du mois : les fleches
	b.ge	.cell
	cmp	x20,1
	b.ne	.z
	ldr	w4,[x19,UW_W]
	mov	x1,-1
	cmp	x2,30
	b.lt	.mo
	sub	x4,x4,30
	cmp	x2,x4
	b.lt	.z
	mov	x1,1
.mo:	mov	x0,x19
	bl	uk_cal_month
	b	.z
.cell:	mov	x0,x19
	mov	x1,x2
	mov	x2,x3
	bl	uk_cal_at
	cmp	x20,1
	b.ne	.hot0
	cbz	x0,.z
	ldr	x1,[x19,UW_VALUE]
	str	x0,[x19,UW_VALUE]
	mov	x22,x1
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_ACTIVATE
	cmp	x21,2
	b.eq	.out
	mov	x0,UN_CHANGE
	b	.out
.hot0:	cbz	x0,.hm
	b	.hot
.hm:	mov	x0,-1
.hot:	ldr	x1,[x19,UW_HOT]
	cmp	x0,x1
	b.eq	.z
	str	x0,[x19,UW_HOT]
	mov	x0,x19
	bl	uk_w_refresh
.z:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; clavier : fleches (jour, semaine), Page (mois), Entree (UN_ACTIVATE)
uk_cal_key:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	w1,KEY_ENTER
	b.ne	.k
	mov	x0,UN_ACTIVATE
	b	.out
.k:	mov	x20,0				; jours a ajouter
	cmp	w1,KEY_LEFT
	b.ne	.k1
	mov	x20,-1
.k1:	cmp	w1,KEY_RIGHT
	b.ne	.k2
	mov	x20,1
.k2:	cmp	w1,KEY_UP
	b.ne	.k3
	mov	x20,-7
.k3:	cmp	w1,KEY_DOWN
	b.ne	.k4
	mov	x20,7
.k4:	cmp	w1,KEY_PGUP
	b.eq	.pm
	cmp	w1,KEY_PGDN
	b.eq	.pp
	cbz	x20,.no
	ldr	x0,[x19,UW_VALUE]		; jour +/- x20, dans le mois voisin
	and	x2,x0,0xFF
	add	x2,x2,x20
	cmp	x2,1
	b.ge	.nlo
	mov	x0,x19				; avant le 1er : mois d'avant
	mov	x1,-1
	bl	uk_cal_shift
	ldr	x0,[x19,UW_VALUE]
	lsr	x1,x0,16
	ubfx	x3,x0,8,8
	str	x2,[sp,-16]!
	mov	x0,x1
	mov	x1,x3
	bl	uk_month_days
	ldr	x2,[sp],16
	add	x2,x2,x0
	b	.set
.nlo:	stp	x0,x2,[sp,-16]!
	lsr	x0,x0,16
	ldr	x1,[sp]
	ubfx	x1,x1,8,8
	bl	uk_month_days
	ldp	x1,x2,[sp],16
	cmp	x2,x0
	b.le	.set
	sub	x2,x2,x0			; apres le dernier : mois d'apres
	str	x2,[sp,-16]!
	mov	x0,x19
	mov	x1,1
	bl	uk_cal_shift
	ldr	x2,[sp],16
.set:	ldr	x0,[x19,UW_VALUE]
	and	x0,x0,not 0xFF
	orr	x0,x0,x2
	str	x0,[x19,UW_VALUE]
	and	x0,x0,not 0xFF			; le mois montre suit
	str	x0,[x19,UW_AUX2]
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
	b	.out
.pm:	mov	x1,-1
	b	.pg
.pp:	mov	x1,1
.pg:	mov	x0,x19
	bl	uk_cal_month
	mov	x0,0
	b	.out
.no:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = calendrier, x1 = -1 / +1 : la date choisie passe au mois voisin (jour
; inchange ici ; uk_cal_key le corrige)
uk_cal_shift:
	ldr	x2,[x0,UW_VALUE]
	lsr	x3,x2,16
	ubfx	x4,x2,8,8
	add	x4,x4,x1
	cbnz	x4,.n13
	mov	x4,12
	sub	x3,x3,1
.n13:	cmp	x4,13
	b.ne	.st
	mov	x4,1
	add	x3,x3,1
.st:	and	x2,x2,0xFF
	orr	x2,x2,x3,lsl 16
	orr	x2,x2,x4,lsl 8
	str	x2,[x0,UW_VALUE]
	ret

; ---------------------------------------------------------------------------
;  Selecteur de date
; ---------------------------------------------------------------------------

; x0..x3 = rectangle (largeur 0 : 140, hauteur 0 : un controle), x4 = date
; -> poignee (valeur : la date)
ui_datepicker:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x4
	cbnz	x2,.w
	mov	x2,140
.w:	mov	x5,0
	mov	x6,0
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_DATE
	mov	x5,0
	bl	uk_new_sized
	cbz	x0,.out
	str	x19,[x0,UW_VALUE]
.out:	ldr	x19,[sp],16
	epilogue

uk_dp_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,UW_VALUE]		; le texte : la date
	adr	x1,uk_numbuf
	bl	uk_date_text
	mov	x0,x19
	adr	x1,uk_numbuf
	mov	x2,-1
	bl	uk_set_text
	mov	x0,x19				; dessin d'une liste deroulante
	bl	uk_dd_draw
	ldp	x19,x20,[sp],16
	epilogue

UK_DP_W		= 7*34+24		; dialogue de date
UK_DP_H		= UK_CAL_HEAD+UK_CAL_WD+6*30+8+24+12

; x0 = selecteur : le calendrier sous lui -> x0 = UN_CHANGE si une autre date
; est choisie
uk_dp_open:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,UW_VALUE]
	ldr	x2,[x19,UW_AUX]
	bl	ui_date_popup
	tbnz	x0,63,.zero
	ldr	x1,[x19,UW_VALUE]
	cmp	x0,x1
	b.eq	.zero
	str	x0,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
	b	.out
.zero:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = widget, x1 = date, x2 = aujourd'hui : le calendrier dans une fenetre
; modale sous ce widget (un clic choisit le jour ; double-clic, Entree ou OK
; valident) -> x0 = jour choisi (-1 : Annuler, Echap). Pour un champ de date a
; soi (ui_datepicker ne sait ni etre vide, ni se taper)
ui_date_popup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1				; date
	mov	x21,x2				; aujourd'hui
	mov	x22,x0
	bl	uk_met
	ldr	x2,[x0,MT_CTL]
	add	x2,x2,UK_DP_H
	mov	x0,x22
	mov	x1,UK_DP_W
	adr	x3,uk_s_pickdate
	bl	uk_dialog_under
	cbz	x0,.fail
	mov	x20,x0
	mov	x0,(UK_DP_W-7*34)/2
	mov	x1,8
	mov	x2,0
	mov	x3,0
	mov	x4,x19
	bl	ui_calendar
	str	x0,[x20,DG_LIST]
	str	x21,[x0,UW_AUX]			; (aujourd'hui)
	mov	x21,x0
	mov	x0,x20
	bl	uk_dlg_bar
	mov	x0,x20
	mov	x1,0
	adr	x2,uk_s_ok
	mov	x3,1
	mov	x4,UB_DEFAULT
	mov	x5,UK_DP_W-12
	bl	ui_dialog_button
	mov	x5,x0
	mov	x0,x20
	mov	x1,1
	adr	x2,uk_s_cancel
	mov	x3,0
	mov	x4,0
	bl	ui_dialog_button
	mov	x0,x21
	bl	ui_w_focus
	adr	x1,uk_dp_fn
	str	x1,[x20,DG_FN]
	mov	x0,x20
	mov	x1,0
	bl	ui_dialog_run
	mov	x22,x0
	ldr	x21,[x21,UW_VALUE]
	mov	x0,x20
	bl	ui_dialog_end
	cbz	x22,.fail
	mov	x0,x21
	b	.out
.fail:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (dialogue de date) x0 = dialogue, x1 = widget, x2 = code
uk_dp_fn:
	ldr	x3,[x0,DG_LIST]
	cmp	x1,x3
	b.ne	uk_dlg_buttons_fn
	cmp	x2,UN_ACTIVATE
	b.ne	.ret
	mov	x1,1
	b	ui_dialog_close
.ret:	ret

; x0 = selecteur, x1 = aujourd'hui (entoure dans son calendrier)
ui_datepicker_today:
	cbz	x0,.ret
	str	x1,[x0,UW_AUX]
.ret:	ret

; souris : un appui ouvre le calendrier (survol : comme une liste)
uk_dp_mouse:
	cmp	x1,1
	b.ne	uk_dd_hover
	b	uk_dp_open
; clavier : Espace, Entree, Alt+bas
uk_dp_key:
	cmp	w2,' '
	b.eq	uk_dp_open
	cmp	w1,KEY_ENTER
	b.eq	uk_dp_open
	mov	x0,-1
	ret

; ---------------------------------------------------------------------------
;  Palette, selecteur de couleur
; ---------------------------------------------------------------------------

UK_PAL_COLS	= 8
UK_PAL_ROWS	= 5
uk_palette_colors:
	dw 0x000000,0x404040,0x808080,0xA0A0A0,0xC0C0C0,0xE0E0E0,0xF4F4F4,0xFFFFFF
	dw 0x7F0000,0xC00000,0xFF0000,0xFF6060,0xFF9F40,0xFFC000,0xFFE060,0xFFFF80
	dw 0x004000,0x008000,0x30A46C,0x60C060,0x90E090,0x00A0A0,0x40E0E0,0xB0F0F0
	dw 0x000080,0x0040C0,0x2F74E0,0x60A0FF,0xA0C8FF,0x6040C0,0x9060E0,0xC0A0FF
	dw 0x602000,0x8A5A2B,0xC08040,0xE0B080,0x800040,0xC00080,0xFF40A0,0xFFB0D0

; x0..x3 = rectangle (0 : 8 x 22, 5 x 22), x4 = couleur -> palette
ui_palette:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x4
	cbnz	x2,.w
	mov	x2,UK_PAL_COLS*22
.w:	cbnz	x3,.h
	mov	x3,UK_PAL_ROWS*22
.h:	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_PALETTE
	bl	uk_w_new
	cbz	x0,.out
	str	x19,[x0,UW_VALUE]
	mov	x1,-1
	str	x1,[x0,UW_HOT]
.out:	ldr	x19,[sp],16
	epilogue

uk_pal_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	jet	w4,TJ_SURFACE
	bl	ui_fill
	ldr	w21,[x19,UW_W]
	mov	x0,UK_PAL_COLS
	udiv	x21,x21,x0			; case
	ldr	w22,[x19,UW_H]
	mov	x0,UK_PAL_ROWS
	udiv	x22,x22,x0
	mov	x20,0
.c:	cmp	x20,UK_PAL_COLS*UK_PAL_ROWS
	b.hs	.out
	mov	x0,UK_PAL_COLS
	udiv	x1,x20,x0
	msub	x0,x1,x0,x20
	ldp	w2,w3,[x19,UW_X]
	sxtw	x2,w2
	sxtw	x3,w3
	madd	x23,x0,x21,x2			; x
	madd	x24,x1,x22,x3			; y
	adr	x0,uk_palette_colors
	ldr	w5,[x0,x20,lsl 2]
	ldr	x0,[x19,UW_VALUE]		; choisie : contour ACCENT de 2 px
	cmp	w0,w5
	b.ne	.sw
	stp	x5,x5,[sp,-16]!
	mov	x0,x23
	mov	x1,x24
	mov	x2,x21
	mov	x3,x22
	mov	x4,5
	jet	w5,TJ_ACCENT
	mov	w6,w5
	bl	ui_rbox
	ldp	x5,x6,[sp],16
.sw:	ldr	x0,[x19,UW_HOT]			; survolee : contour TEXTE2
	cmp	x0,x20
	b.ne	.sw2
	stp	x5,x5,[sp,-16]!
	add	x0,x23,1
	add	x1,x24,1
	sub	x2,x21,2
	sub	x3,x22,2
	mov	x4,4
	jet	w5,TJ_TEXTE2
	mov	w6,w5
	bl	ui_rbox
	ldp	x5,x6,[sp],16
.sw2:	mov	w6,w5				; la pastille (contour LIGNE)
	stp	x5,x5,[sp,-16]!
	add	x0,x23,3
	add	x1,x24,3
	sub	x2,x21,6
	sub	x3,x22,6
	mov	x4,3
	jet	w5,TJ_LIGNE
	mov	w6,w5
	bl	ui_rbox
	ldp	x5,x6,[sp],16
	mov	w6,w5
	add	x0,x23,4
	add	x1,x24,4
	sub	x2,x21,8
	sub	x3,x22,8
	mov	x4,2
	bl	ui_rbox
	add	x20,x20,1
	b	.c
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; souris : survol, clic (double : UN_ACTIVATE)
uk_pal_mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x5,-1
	cmp	x20,3
	b.eq	.hot
	ldp	w6,w7,[x19,UW_W]
	mov	x8,UK_PAL_COLS
	udiv	x6,x6,x8
	mov	x8,UK_PAL_ROWS
	udiv	x7,x7,x8
	udiv	x2,x2,x6
	udiv	x3,x3,x7
	cmp	x2,UK_PAL_COLS
	b.hs	.hot
	cmp	x3,UK_PAL_ROWS
	b.hs	.hot
	mov	x8,UK_PAL_COLS
	madd	x5,x3,x8,x2
	cmp	x20,1
	b.ne	.hot
	adr	x0,uk_palette_colors
	ldr	w0,[x0,x5,lsl 2]
	str	x0,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_ACTIVATE
	cmp	x4,2
	b.eq	.out
	mov	x0,UN_CHANGE
	b	.out
.hot:	ldr	x0,[x19,UW_HOT]
	cmp	x0,x5
	b.eq	.z
	str	x5,[x19,UW_HOT]
	mov	x0,x19
	bl	uk_w_refresh
.z:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0..x3 = rectangle (0 : 44 x un controle), x4 = couleur -> selecteur de
; couleur (valeur : 0x00RRGGBB)
ui_colorpicker:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x4
	cbnz	x2,.w
	mov	x2,44
.w:	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,UT_COLOR
	mov	x5,0
	mov	x6,0
	bl	uk_new_sized
	cbz	x0,.out
	str	x19,[x0,UW_VALUE]
.out:	ldr	x19,[sp],16
	epilogue

uk_cp_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldp	w0,w1,[x19,UW_X]
	ldp	w2,w3,[x19,UW_W]
	jet	w4,TJ_FACE
	bl	ui_fill
	ldp	w0,w1,[x19,UW_X]		; contour LIGNE_FORTE (survol : ACCENT)
	ldp	w2,w3,[x19,UW_W]
	mov	x4,6
	jet	w5,TJ_LIGNE_FORTE
	ldr	w6,[x19,UW_FLAGS]
	tbz	w6,2,.c
	jet	w5,TJ_ACCENT
.c:	mov	w6,w5
	bl	ui_rbox
	ldp	w0,w1,[x19,UW_X]		; la couleur
	ldp	w2,w3,[x19,UW_W]
	add	x0,x0,3
	add	x1,x1,3
	sub	x2,x2,6
	sub	x3,x3,6
	mov	x4,4
	ldr	x5,[x19,UW_VALUE]
	and	w5,w5,0x00FFFFFF
	mov	w6,w5
	bl	ui_rbox
	ldp	x19,x20,[sp],16
	epilogue

; x0 = selecteur : le dialogue de couleur -> UN_CHANGE si une autre couleur
; est choisie
uk_cp_open:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,uk_s_color
	ldr	x1,[x19,UW_VALUE]
	bl	ui_color_dialog
	tbnz	x0,63,.zero
	ldr	x1,[x19,UW_VALUE]
	cmp	x0,x1
	b.eq	.zero
	str	x0,[x19,UW_VALUE]
	mov	x0,x19
	bl	uk_w_refresh
	mov	x0,UN_CHANGE
	b	.out
.zero:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

uk_cp_mouse:
	cmp	x1,1
	b.ne	uk_dd_hover
	b	uk_cp_open
uk_cp_key:
	cmp	w2,' '
	b.eq	uk_cp_open
	cmp	w1,KEY_ENTER
	b.eq	uk_cp_open
	mov	x0,-1
	ret

; x0 = titre, x1 = couleur de depart -> x0 = couleur choisie (0x00RRGGBB ;
; -1 : Annuler) : palette, rouge / vert / bleu, apercu
ui_color_dialog:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	mov	x2,x0
	mov	x0,440
	mov	x1,316
	bl	ui_dialog_begin
	cbz	x0,.fail
	mov	x19,x0
	mov	x0,UK_DLG_PAD			; palette
	mov	x1,UK_DLG_PAD
	mov	x2,0
	mov	x3,0
	mov	x4,x20
	bl	ui_palette
	str	x0,[x19,DG_LIST]
	mov	x21,0				; trois glissieres : rouge, vert, bleu
.s:	cmp	x21,3
	b.hs	.prev
	mov	x0,UK_DLG_PAD
	mov	x1,36
	mul	x1,x1,x21
	add	x1,x1,UK_DLG_PAD+UK_PAL_ROWS*22+12
	mov	x2,UK_PAL_COLS*22
	mov	x3,28
	mov	x4,0
	mov	x5,255
	mov	x6,2				; (octet de la couleur : 2 - x21)
	sub	x6,x6,x21
	lsl	x6,x6,3
	lsr	x6,x20,x6
	and	x6,x6,0xFF
	bl	ui_slider
	add	x1,x19,DG_SL			; (DG_SL.. : trois glissieres)
	str	x0,[x1,x21,lsl 3]
	add	x21,x21,1
	b	.s
.prev:	mov	x0,x19
	bl	uk_dlg_bar
	str	x20,[x19,DG_KIND]		; couleur courante
	mov	x0,x19
	bl	uk_cd_preview
	mov	x0,x19
	mov	x1,0
	adr	x2,uk_s_ok
	mov	x3,1
	mov	x4,UB_DEFAULT
	mov	x5,440-16
	bl	ui_dialog_button
	mov	x5,x0
	mov	x0,x19
	mov	x1,1
	adr	x2,uk_s_cancel
	mov	x3,0
	mov	x4,0
	bl	ui_dialog_button
	mov	x0,x19
	adr	x1,uk_cd_fn
	bl	ui_dialog_run
	mov	x21,x0
	ldr	x22,[x19,DG_KIND]
	mov	x0,x19
	bl	ui_dialog_end
	mov	x0,-1
	cbz	x21,.out
	mov	x0,x22
	b	.out
.fail:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (dialogue de couleur) x0 = dialogue : l'apercu de la couleur courante et
; son code « #RRGGBB »
uk_cd_preview:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,UK_DLG_PAD+UK_PAL_COLS*22+24
	mov	x1,UK_DLG_PAD
	mov	x2,440-UK_DLG_PAD-UK_PAL_COLS*22-24-UK_DLG_PAD
	mov	x3,UK_PAL_ROWS*22
	mov	x4,8
	jet	w5,TJ_LIGNE_FORTE
	mov	w6,w5
	bl	ui_rbox
	mov	x0,UK_DLG_PAD+UK_PAL_COLS*22+25
	mov	x1,UK_DLG_PAD+1
	mov	x2,440-UK_DLG_PAD-UK_PAL_COLS*22-24-UK_DLG_PAD-2
	mov	x3,UK_PAL_ROWS*22-2
	mov	x4,7
	ldr	x5,[x19,DG_KIND]
	mov	w6,w5
	bl	ui_rbox
	adr	x0,uk_numbuf			; « #RRGGBB »
	mov	w1,'#'
	strb	w1,[x0],1
	ldr	x2,[x19,DG_KIND]
	mov	x3,20
.h:	lsr	x4,x2,x3
	and	x4,x4,15
	cmp	x4,10
	add	x5,x4,'0'
	add	x6,x4,'A'-10
	csel	x5,x6,x5,hs
	strb	w5,[x0],1
	subs	x3,x3,4
	b.pl	.h
	strb	wzr,[x0]
	mov	x0,UK_DLG_PAD+UK_PAL_COLS*22+24
	mov	x1,UK_DLG_PAD+UK_PAL_ROWS*22+12
	mov	x2,140
	mov	x3,28
	jet	w4,TJ_FACE
	bl	ui_fill
	bl	uk_font_m
	mov	x5,x0
	adr	x0,uk_numbuf
	mov	x1,UK_DLG_PAD+UK_PAL_COLS*22+24
	mov	x2,UK_DLG_PAD+UK_PAL_ROWS*22+12
	mov	x3,28
	jet	w4,TJ_TEXTE1
	mov	x6,0
	mov	x7,15
	bl	uk_text_box
	bl	ui_update_all
	ldr	x19,[sp],16
	epilogue

; (dialogue de couleur) x0 = dialogue, x1 = widget, x2 = code
uk_cd_fn:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	x2,UN_CHANGE
	b.eq	.ch
	cmp	x2,UN_ACTIVATE
	b.ne	.out
	ldr	x3,[x19,DG_LIST]		; double-clic dans la palette : OK
	cmp	x1,x3
	b.ne	.out
	ldr	x0,[x1,UW_VALUE]
	str	x0,[x19,DG_KIND]
	mov	x0,x19
	mov	x1,1
	bl	ui_dialog_close
	b	.out
.ch:	ldr	x3,[x19,DG_LIST]		; la palette : les glissieres suivent
	cmp	x1,x3
	b.ne	.sl
	ldr	x20,[x1,UW_VALUE]
	str	x20,[x19,DG_KIND]
	mov	x3,0
.ps:	cmp	x3,3
	b.hs	.prev
	add	x0,x19,DG_SL
	ldr	x0,[x0,x3,lsl 3]
	mov	x4,2
	sub	x4,x4,x3
	lsl	x4,x4,3
	lsr	x1,x20,x4
	and	x1,x1,0xFF
	str	x3,[sp,-16]!
	bl	ui_w_set_value
	ldr	x3,[sp],16
	add	x3,x3,1
	b	.ps
.sl:	mov	x20,0				; une glissiere : la couleur
	mov	x3,0
.sv:	cmp	x3,3
	b.hs	.sd
	add	x0,x19,DG_SL
	ldr	x0,[x0,x3,lsl 3]
	ldr	x0,[x0,UW_VALUE]
	lsl	x20,x20,8
	orr	x20,x20,x0
	add	x3,x3,1
	b	.sv
.sd:	str	x20,[x19,DG_KIND]
.prev:	mov	x0,x19
	bl	uk_cd_preview
.out:	ldp	x19,x20,[sp],16
	epilogue

; souris d'un widget a menu : survol (UWF_HOVER), sortie
uk_dd_hover:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	w2,[x19,UW_FLAGS]
	orr	w3,w2,UWF_HOVER
	cmp	x1,3
	b.ne	.st
	and	w3,w2,not UWF_HOVER
.st:	cmp	w2,w3
	b.eq	.z
	str	w3,[x19,UW_FLAGS]
	mov	x0,x19
	bl	uk_w_refresh
.z:	mov	x0,0
	ldr	x19,[sp],16
	epilogue

uk_month_names:	db 'janvier',0,'f',0xC3,0xA9,'vrier',0,'mars',0,'avril',0,'mai',0,'juin',0
		db 'juillet',0,'ao',0xC3,0xBB,'t',0,'septembre',0,'octobre',0,'novembre',0
		db 'd',0xC3,0xA9,'cembre',0
uk_s_wdays:	db 'L',0,'M',0,'M',0,'J',0,'V',0,'S',0,'D',0
uk_s_prev:	db 0xE2,0x80,0xB9,0			; « ‹ »
uk_s_next:	db 0xE2,0x80,0xBA,0			; « › »
		align 4
