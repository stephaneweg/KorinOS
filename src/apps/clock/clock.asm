; horloge - horloge analogique anti-crenelee + heure numerique, et les alarmes
; Pas d'horloge temps reel materielle : heure = decalage regle (boutons, garde
; dans SD0:/etc/clock.cfg en secondes) + temps ecoule depuis le demarrage.
; Rendu : couverture calculee par distance au segment / au cercle (flottants).
; Onglet Alarmes : la liste de SD0:/etc/alarms.txt (src/inc/alarms.inc, le
; format d'Onyx), ajouter, modifier, activer, supprimer ; chaque changement est
; ecrit puis publie (korin.clock.alarms) : clockd (src/sys/clockd) les sonne,
; meme l'Horloge fermee, et ses changements (report, arret) reviennent ici.
; Fenetre de taille fixe ; agrandie (mode poche d'Elegant, WEV_RESIZE :
; docs/MODE-POCHE.md), le cadran reste centre (g_ox, g_oy, ajoutes par
; blend_px, numeral et les rectangles) et la page Alarmes suit ses ancres
; (ui_anchor).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'
include '../../kits/buskit/buskit.inc'

WIN_W	= 400
WIN_H	= 484
CX	= 200
CY	= 200
RAD	= 150
TOP	= 40				; sous les onglets
BTN_Y	= 428
BTN_W	= 96
BTN_H	= 34
BTN_X0	= 44
M	= 16				; marge de l'onglet Alarmes
LIST_Y	= 84
LIST_H	= 300
ROW_Y	= 394				; les boutons de l'onglet Alarmes
DLG_W	= 420
DLG_H	= 340

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so (Essayer)
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so
include '../../inc/gamesnd.inc'
include '../../inc/alarms.inc'

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
	mov	x5,0
	bl	ui_window_ex
	tbnz	x0,63,quit
	bl	load_cfg
	bl	al_load
	adr	x0,al_topic			; clockd a change le fichier
	mov	x1,0
	bl	bus_subscribe
	cmp	x0,0
	csel	x0,x0,xzr,gt
	adr	x1,g_sub
	str	x0,[x1]
	bl	make_widgets
	adr	x0,on_resized			; autre taille : ancres, puis la page
	bl	ui_on_resize
	mov	x0,0
	bl	page_show
	sys	SYS_TIME_MS
	mov	x1,1000
	udiv	x0,x0,x1
	mul	x0,x0,x1
	add	x19,x0,1000			; prochaine seconde pleine
.loop:	sys	SYS_TIME_MS
	subs	x0,x19,x0
	b.le	.tick
	bl	ui_event
	cbz	x0,.loop
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_THEME			; autre theme : la page redessinee
	b.ne	.bus
	adr	x0,g_page
	ldr	x0,[x0]
	bl	page_show
	b	.loop
.bus:	cmp	x0,WEV_BUS
	b.ne	.wid
	adr	x0,ui_ev			; les alarmes ont change : relues
	ldr	w0,[x0,BEV_SUB]
	adr	x1,g_sub
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.loop
	bl	al_load
	bl	alarms_refresh
	b	.loop
.wid:	mov	x21,x0				; (le type)
	cmp	x21,WEV_KEY			; Alarmes : Suppr supprime
	b.ne	.we
	adr	x0,g_page
	ldr	x0,[x0]
	cbz	x0,.we
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.we
	ldr	w1,[x0,EV_CODE]
	cmp	w1,KEY_DELETE
	b.ne	.we
	bl	alarm_delete
	b	.loop
.we:	mov	x0,0
	bl	ui_w_event
	cbz	x1,.raw
	bl	on_widget
	b	.loop
.raw:	cbnz	x2,.loop			; (pris par un widget)
	adr	x0,g_page
	ldr	x0,[x0]
	cbnz	x0,.loop
	cmp	x21,WEV_MOUSE
	b.ne	.loop
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cmp	w1,1
	b.ne	.loop
	mov	x20,0				; bouton 0..2
.btn:	bl	btn_rect
	bl	ui_hit
	cbnz	x0,.press
	add	x20,x20,1
	cmp	x20,3
	b.lo	.btn
	b	.loop
.press:	adr	x0,adjust
	ldr	x1,[x0,x20,lsl 3]
	adr	x2,offset
	ldr	x3,[x2]
	add	x3,x3,x1
	movl	x4,86400
	sdiv	x5,x3,x4			; modulo 24 h, positif
	msub	x3,x5,x4,x3
	cmp	x3,0
	b.ge	.sto
	add	x3,x3,x4
.sto:	str	x3,[x2]
	bl	save_cfg
	bl	draw_clock
	b	.loop
.tick:	add	x19,x19,1000
	adr	x0,g_page
	ldr	x0,[x0]
	cbnz	x0,.atick
	bl	draw_clock
	b	.loop
.atick:	bl	al_now				; Alarmes : une fois par minute
	adr	x1,g_minute
	ldr	x2,[x1]
	cmp	x0,x2
	b.eq	.loop
	bl	alarms_refresh
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; x20 = bouton -> x0..x3 = rectangle
btn_rect:
	adr	x4,g_ox
	ldp	x4,x5,[x4]
	mov	x0,BTN_W+12
	mul	x0,x0,x20
	add	x0,x0,BTN_X0
	add	x0,x0,x4
	add	x1,x5,BTN_Y
	mov	x2,BTN_W
	mov	x3,BTN_H
	ret

draw_buttons:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,0
.b:	bl	btn_rect
	adr	x4,labels
	ldr	x4,[x4,x20,lsl 3]
	mov	x5,0
	bl	ui_button
	add	x20,x20,1
	cmp	x20,3
	b.lo	.b
	mov	x0,0
	adr	x1,g_oy
	ldr	x1,[x1]
	add	x1,x1,BTN_Y
	adr	x2,ui_w
	ldr	x2,[x2]
	mov	x3,BTN_H
	bl	ui_update
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = secondes de la journee
now_seconds:
	prologue
	sys	SYS_TIME_MS
	mov	x1,1000
	udiv	x0,x0,x1
	adr	x1,offset
	ldr	x1,[x1]
	add	x0,x0,x1
	movl	x1,86400
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	epilogue

; ---------------------------------------------------------------------------
draw_clock:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	bl	now_seconds
	mov	x19,x0
	; fond + cadran (sous les onglets)
	mov	x0,0
	mov	x1,TOP
	adr	x2,ui_w
	ldr	x2,[x2]
	adr	x3,g_oy
	ldr	x3,[x3]
	add	x3,x3,BTN_Y-4-TOP
	jeton	w4,TJ_FACE
	bl	ui_fill
	mov	x0,RAD+8
	movl	w1,0x00303A46
	bl	disc
	mov	x0,RAD+2
	movl	w1,0x00FFFFFF
	bl	disc
	; graduations
	mov	x20,0
.tk:	scvtf	s0,w20
	adr	x0,k_six_deg
	ldr	s1,[x0]
	fmul	s0,s0,s1			; angle
	mov	x0,x20
	mov	x1,5
	udiv	x2,x0,x1
	msub	x2,x2,x1,x0
	cbz	x2,.hour
	fmov	s4,1.0				; minute : fine
	mov	x0,RAD-10
	mov	x1,RAD-2
	movl	w2,0x008A96A6
	b	.dr
.hour:	fmov	s4,2.5
	mov	x0,RAD-22
	mov	x1,RAD-2
	movl	w2,0x00222B36
.dr:	bl	radial
	add	x20,x20,1
	cmp	x20,60
	b.lo	.tk
	; chiffres 12, 3, 6, 9
	adr	x0,s_12
	mov	x1,CX-14
	mov	x2,CY-RAD+52
	bl	numeral
	adr	x0,s_3
	mov	x1,CX+RAD-46
	mov	x2,CY+9
	bl	numeral
	adr	x0,s_6
	mov	x1,CX-7
	mov	x2,CY+RAD-32
	bl	numeral
	adr	x0,s_9
	mov	x1,CX-RAD+32
	mov	x2,CY+9
	bl	numeral
	; aiguilles
	mov	x0,3600				; heures : (s mod 43200) / 43200 tours
	movl	x1,43200
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19
	scvtf	s0,x2
	scvtf	s1,x1
	fdiv	s0,s0,s1
	bl	turn_to_rad
	fmov	s4,4.0
	mov	x0,-14
	mov	x1,RAD*52/100
	movl	w2,0x00222B36
	bl	radial
	mov	x1,3600				; minutes
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19
	scvtf	s0,x2
	scvtf	s1,x1
	fdiv	s0,s0,s1
	bl	turn_to_rad
	fmov	s4,2.75
	mov	x0,-18
	mov	x1,RAD*78/100
	movl	w2,0x00222B36
	bl	radial
	mov	x1,60				; secondes
	udiv	x2,x19,x1
	msub	x2,x2,x1,x19
	scvtf	s0,x2
	scvtf	s1,x1
	fdiv	s0,s0,s1
	bl	turn_to_rad
	fmov	s4,1.0
	mov	x0,-26
	mov	x1,RAD*88/100
	movl	w2,0x00D9534F
	bl	radial
	mov	x0,7				; axe
	movl	w1,0x00D9534F
	bl	disc
	mov	x0,3
	movl	w1,0x00FFFFFF
	bl	disc
	; heure numerique
	sub	sp,sp,32
	mov	x0,sp
	mov	x1,3600
	udiv	x1,x19,x1
	bl	two_digits
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,60
	udiv	x2,x19,x1
	mov	x3,60
	udiv	x4,x2,x3
	msub	x1,x4,x3,x2
	bl	two_digits
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,60
	udiv	x2,x19,x1
	msub	x1,x2,x1,x19
	bl	two_digits
	mov	x0,sp
	mov	x1,30
	adr	x2,ui_font_b
	bl	ui_text_width
	adr	x1,ui_w
	ldr	x1,[x1]
	sub	x1,x1,x0
	lsr	x1,x1,1
	mov	x0,sp
	adr	x2,g_oy
	ldr	x2,[x2]
	add	x2,x2,CY+RAD+50
	jeton	w3,TJ_TEXTE1
	mov	x4,30
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	add	sp,sp,32
	mov	x0,0				; (la note : son fond d'abord, jusqu'en bas)
	adr	x1,g_oy
	ldr	x1,[x1]
	add	x1,x1,BTN_Y+BTN_H+2
	adr	x2,ui_w
	ldp	x2,x3,[x2]
	sub	x3,x3,x1
	jeton	w4,TJ_FACE
	bl	ui_fill
	adr	x0,s_note
	bl	tr0
	adr	x1,g_ox
	ldp	x1,x2,[x1]
	add	x1,x1,24
	add	x2,x2,BTN_Y+BTN_H+16
	jeton	w3,TJ_TEXTE2
	mov	x4,11
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	bl	ui_update_all
	ldp	d8,d9,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = x, x2 = ligne de base : chiffre du cadran
numeral:
	adr	x3,g_ox
	ldp	x3,x4,[x3]
	add	x1,x1,x3
	add	x2,x2,x4
	movl	w3,0x00222B36
	mov	x4,22
	adr	x5,ui_font_b
	mov	x6,0
	b	ui_text

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

; x2 = texte -> sa traduction ; les autres registres gardes
tr2:
	prologue
	str	x0,[sp,-16]!
	mov	x0,x2
	bl	tr0
	mov	x2,x0
	ldr	x0,[sp],16
	epilogue

; x0 = destination, x1 = 0..99 -> x0 avance, 0 final
two_digits:
	mov	x6,10
	udiv	x7,x1,x6
	msub	x8,x7,x6,x1
	add	w7,w7,'0'
	add	w8,w8,'0'
	strb	w7,[x0],1
	strb	w8,[x0],1
	strb	wzr,[x0]
	ret

; s0 = fraction de tour -> s0 = angle en radians
turn_to_rad:
	adr	x0,k_two_pi
	ldr	s1,[x0]
	fmul	s0,s0,s1
	ret

; s0 = angle (0 = midi, sens horaire), x0 = rayon interieur (peut etre <0),
; x1 = rayon exterieur, w2 = couleur, s4 = demi-epaisseur
radial:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	str	x2,[sp,-16]!
	fmov	s11,s4
	bl	sincos				; s0 = sin, s1 = cos
	fmov	s8,s0
	fmov	s9,s1
	mov	x0,CX
	scvtf	s10,x0
	mov	x0,CY
	scvtf	s5,x0
	scvtf	s6,x19				; A = centre + r_int * (sin, -cos)
	fmul	s0,s8,s6
	fadd	s0,s10,s0
	fmul	s1,s9,s6
	fsub	s1,s5,s1
	scvtf	s6,x20				; B = centre + r_ext * (sin, -cos)
	fmul	s2,s8,s6
	fadd	s2,s10,s2
	fmul	s3,s9,s6
	fsub	s3,s5,s3
	fmov	s4,s11
	ldr	x0,[sp],16
	bl	aa_segment
	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = rayon, w1 = couleur : disque centre sur (CX, CY), bord anti-crenele
disc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	scvtf	s8,x19				; rayon
	fmov	s9,0.5
	fadd	s8,s8,s9			; rayon + 0,5
	mov	x21,CY
	sub	x21,x21,x19
	sub	x21,x21,1
.y:	mov	x0,CY
	add	x0,x0,x19
	add	x0,x0,1
	cmp	x21,x0
	b.gt	.done
	mov	x22,CX
	sub	x22,x22,x19
	sub	x22,x22,1
.x:	mov	x0,CX
	add	x0,x0,x19
	add	x0,x0,1
	cmp	x22,x0
	b.gt	.ny
	mov	x0,CX
	sub	x0,x22,x0
	scvtf	s0,x0
	mov	x0,CY
	sub	x0,x21,x0
	scvtf	s1,x0
	fmul	s0,s0,s0
	fmadd	s0,s1,s1,s0
	fsqrt	s0,s0
	fsub	s0,s8,s0			; couverture = r + 0,5 - d
	mov	x0,x22
	mov	x1,x21
	mov	w2,w20
	bl	blend_px
	add	x22,x22,1
	b	.x
.ny:	add	x21,x21,1
	b	.y
.done:	ldp	d8,d9,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; s0,s1 = A ; s2,s3 = B ; s4 = demi-epaisseur ; w0 = couleur
; Couverture = demi-epaisseur + 0,5 - distance au segment (extremites rondes)
aa_segment:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	d8,d9,[sp,-16]!
	stp	d10,d11,[sp,-16]!
	stp	d12,d13,[sp,-16]!
	stp	d14,d15,[sp,-16]!
	mov	w19,w0
	fmov	s8,s0				; Ax
	fmov	s9,s1				; Ay
	fsub	s10,s2,s0			; ABx
	fsub	s11,s3,s1			; ABy
	fmov	s12,0.5
	fadd	s12,s4,s12			; demi-epaisseur + 0,5
	fmul	s13,s10,s10
	fmadd	s13,s11,s11,s13			; |AB|^2
	fmov	s14,1.0
	fcmp	s13,s14
	b.ge	.lok
	fmov	s13,1.0
.lok:	; boite englobante
	fminnm	s0,s8,s2
	fmaxnm	s5,s8,s2
	fminnm	s1,s9,s3
	fmaxnm	s6,s9,s3
	fsub	s0,s0,s12
	fadd	s5,s5,s12
	fsub	s1,s1,s12
	fadd	s6,s6,s12
	fcvtms	x20,s0				; x min
	fcvtps	x21,s5				; x max
	fcvtms	x22,s1				; y min
	fcvtps	x23,s6				; y max
.y:	cmp	x22,x23
	b.gt	.done
	mov	x24,x20
.x:	cmp	x24,x21
	b.gt	.ny
	scvtf	s0,x24
	scvtf	s1,x22
	fsub	s0,s0,s8			; AP
	fsub	s1,s1,s9
	fmul	s2,s0,s10			; t = AP.AB / |AB|^2, borne a [0,1]
	fmadd	s2,s1,s11,s2
	fdiv	s2,s2,s13
	fmov	s3,wzr
	fmaxnm	s2,s2,s3
	fminnm	s2,s2,s14
	fmsub	s0,s2,s10,s0			; AP - t*AB
	fmsub	s1,s2,s11,s1
	fmul	s0,s0,s0
	fmadd	s0,s1,s1,s0
	fsqrt	s0,s0
	fsub	s0,s12,s0
	mov	x0,x24
	mov	x1,x22
	mov	w2,w19
	bl	blend_px
	add	x24,x24,1
	b	.x
.ny:	add	x22,x22,1
	b	.y
.done:	ldp	d14,d15,[sp],16
	ldp	d12,d13,[sp],16
	ldp	d10,d11,[sp],16
	ldp	d8,d9,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, w2 = couleur, s0 = couverture (bornee a [0,1] ici)
; Melange avec la surface. N'utilise que s0, s1, s2, s3 et x0-x7.
blend_px:
	fmov	s1,wzr
	fcmp	s0,s1
	b.le	.ret
	adr	x3,g_ox				; (le cadran centre)
	ldp	x4,x5,[x3]
	add	x0,x0,x4
	add	x1,x1,x5
	adr	x3,ui_w
	ldp	x4,x5,[x3]
	cmp	x0,x4
	b.hs	.ret
	cmp	x1,x5
	b.hs	.ret
	adr	x3,ui_surf
	ldr	x3,[x3]
	lsl	x4,x4,2
	madd	x3,x1,x4,x3
	add	x3,x3,x0,lsl 2
	fmov	s1,1.0
	fcmp	s0,s1
	b.lt	.mix
	str	w2,[x3]
	ret
.mix:	mov	w4,256				; alpha sur 0..256
	scvtf	s1,w4
	fmul	s0,s0,s1
	fcvtzs	w4,s0
	ldr	w5,[x3]
	mov	w6,0
	mov	w7,16
.ch:	lsrv	w0,w2,w7			; fg
	and	w0,w0,0xFF
	lsrv	w1,w5,w7			; bg
	and	w1,w1,0xFF
	sub	w0,w0,w1
	mul	w0,w0,w4
	asr	w0,w0,8
	add	w0,w0,w1
	lslv	w0,w0,w7
	orr	w6,w6,w0
	subs	w7,w7,8
	b.pl	.ch
	str	w6,[x3]
.ret:	ret

; s0 = angle (radians, 0..2pi) -> s0 = sin, s1 = cos (series, |erreur| < 3e-4)
sincos:
	adr	x0,k_pi
	ldr	s1,[x0]
	fsub	s0,s0,s1			; x dans [-pi, pi] : sin(a) = -sin(x)
	fmul	s2,s0,s0			; x^2
	; sin x = x(1 - x2/6(1 - x2/20(1 - x2/42(1 - x2/72(1 - x2/110)))))
	adr	x0,k_sin
	ldr	s3,[x0,16]			; 1/110
	fmov	s4,1.0
	fmul	s5,s2,s3
	fsub	s5,s4,s5
	ldr	s3,[x0,12]			; 1/72
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0,8]			; 1/42
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0,4]			; 1/20
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0]				; 1/6
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	fmul	s6,s5,s0			; sin x
	; cos x = 1 - x2/2(1 - x2/12(1 - x2/30(1 - x2/56(1 - x2/90(1 - x2/132)))))
	adr	x0,k_cos
	ldr	s3,[x0,20]
	fmul	s5,s2,s3
	fsub	s5,s4,s5
	ldr	s3,[x0,16]
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0,12]
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0,8]
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0,4]
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5
	ldr	s3,[x0]
	fmul	s5,s5,s3
	fmul	s5,s5,s2
	fsub	s5,s4,s5			; cos x
	fneg	s0,s6
	fneg	s1,s5
	ret

; --- configuration -------------------------------------------------------------
load_cfg:
	prologue
	str	x19,[sp,-16]!
	adr	x0,p_cfg
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,cfgbuf
	mov	x2,31
	sys	SYS_READ
	mov	x2,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x2,0
	b.le	.out
	adr	x1,cfgbuf
	mov	x3,0
	mov	x4,0
.d:	cmp	x4,x2
	b.hs	.sto
	ldrb	w5,[x1,x4]
	sub	w5,w5,'0'
	cmp	w5,9
	b.hi	.sto
	mov	x6,10
	madd	x3,x3,x6,x5
	add	x4,x4,1
	b	.d
.sto:	movl	x6,86400
	udiv	x5,x3,x6
	msub	x3,x5,x6,x3
	adr	x0,offset
	str	x3,[x0]
.out:	ldr	x19,[sp],16
	epilogue

save_cfg:
	prologue
	str	x19,[sp,-16]!
	adr	x0,offset
	ldr	x0,[x0]
	adr	x1,cfgbuf
	bl	fmt_dec
	mov	w1,10
	strb	w1,[x0],1
	adr	x1,cfgbuf
	sub	x19,x0,x1
	adr	x0,p_cfg
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x2,x19
	mov	x19,x0
	adr	x1,cfgbuf
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
.out:	ldr	x19,[sp],16
	epilogue



; ---------------------------------------------------------------------------
;  Les onglets
; ---------------------------------------------------------------------------

; les widgets : les onglets, puis ceux de la page Alarmes (caches)
make_widgets:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,12				; (a gauche : la droite du haut reste
	mov	x1,6				; aux boutons du cadre)
	mov	x2,0
	mov	x3,0
	adr	x4,s_tabs
	bl	ui_segmented
	adr	x1,w_seg
	str	x0,[x1]
	mov	x0,M				; la prochaine alarme
	mov	x1,48
	mov	x2,WIN_W-2*M
	mov	x3,24
	adr	x4,s_empty
	mov	x5,ULB_BOLD
	bl	ui_label
	adr	x1,w_next
	str	x0,[x1]
	mov	x0,M				; la liste
	mov	x1,LIST_Y
	mov	x2,WIN_W-2*M
	mov	x3,LIST_H
	mov	x4,UL_HEADER or UL_NOSORT
	bl	ui_list
	adr	x1,w_list
	str	x0,[x1]
	mov	x19,x0
	mov	x0,x19
	adr	x1,s_c_time
	mov	x2,72
	mov	x3,0
	bl	ui_list_column
	mov	x0,x19
	adr	x1,s_c_name
	mov	x2,100
	mov	x3,0
	bl	ui_list_column
	mov	x0,x19
	adr	x1,s_c_rep
	mov	x2,104
	mov	x3,0
	bl	ui_list_column
	mov	x0,x19
	adr	x1,s_c_state
	mov	x2,92
	mov	x3,0
	bl	ui_list_column
	mov	x0,M				; les boutons
	mov	x1,ROW_Y
	mov	x2,84
	mov	x3,32
	adr	x4,s_b_add
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_add
	str	x0,[x1]
	mov	x0,M+88
	mov	x1,ROW_Y
	mov	x2,84
	mov	x3,32
	adr	x4,s_b_edit
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_edit
	str	x0,[x1]
	mov	x0,M+176
	mov	x1,ROW_Y
	mov	x2,100
	mov	x3,32
	adr	x4,s_b_off
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_toggle
	str	x0,[x1]
	mov	x0,M+280
	mov	x1,ROW_Y
	mov	x2,WIN_W-M-(M+280)
	mov	x3,32
	adr	x4,s_b_del
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_del
	str	x0,[x1]
	mov	x0,M				; ce que fait clockd
	mov	x1,ROW_Y+40
	mov	x2,WIN_W-2*M
	mov	x3,40
	adr	x4,s_note2
	mov	x5,ULB_DIM or ULB_WRAP
	bl	ui_label
	adr	x1,w_note
	str	x0,[x1]
	bl	set_anchors
	ldp	x19,x20,[sp],16
	epilogue

; (ui_on_resize) x0 = largeur, x1 = hauteur : le cadran centre, la page
; redessinee (les widgets de la page Alarmes ont suivi leurs ancres)
on_resized:
	prologue
	sub	x0,x0,WIN_W
	sub	x1,x1,WIN_H
	cmp	x0,0
	csel	x0,x0,xzr,gt
	cmp	x1,0
	csel	x1,x1,xzr,gt
	lsr	x0,x0,1
	lsr	x1,x1,1
	adr	x2,g_ox
	stp	x0,x1,[x2]
	adr	x0,g_page
	ldr	x0,[x0]
	bl	page_show
	epilogue

; les ancres de la page Alarmes (taille de conception : WIN_W x WIN_H)
set_anchors:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,anchors
	adr	x20,g_aw
.a:	ldrb	w1,[x19],1
	cmp	w1,0xFF
	b.eq	.out
	ldr	x0,[x20],8
	bl	ui_anchor
	b	.a
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = page (0 l'horloge, 1 les alarmes) : montree
page_show:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,g_page
	str	x19,[x0]
	adr	x0,w_seg
	ldr	x0,[x0]
	mov	x1,x19
	bl	ui_w_set_value
	cmp	x19,0
	cset	x20,ne
	adr	x21,g_aw			; les widgets de la page Alarmes
	mov	x22,0
.w:	ldr	x0,[x21,x22,lsl 3]
	mov	x1,x20
	bl	ui_w_show
	add	x22,x22,1
	cmp	x22,AW_N
	b.lo	.w
	mov	x0,0				; le fond sous les onglets
	mov	x1,TOP
	adr	x2,ui_w
	ldp	x2,x3,[x2]
	sub	x3,x3,TOP
	jeton	w4,TJ_FACE
	bl	ui_fill
	cbnz	x19,.al
	bl	draw_buttons
	mov	x0,0
	bl	ui_w_draw
	bl	draw_clock
	b	.out
.al:	bl	alarms_refresh
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1 = code (ui_w_event)
on_widget:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,w_seg
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.list
	cmp	x20,UN_CHANGE
	b.ne	.out
	bl	ui_w_get_value
	cmp	x0,1
	cset	x0,eq
	adr	x1,g_page
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.out
	bl	page_show
	b	.out
.list:	adr	x0,w_list
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.add
	bl	ui_w_get_value			; la ligne choisie : son alarme
	tbnz	x0,63,.ub
	mov	x1,x0
	adr	x0,w_list
	ldr	x0,[x0]
	bl	ui_list_get
	cbz	x0,.ub
	adr	x2,g_sel_id
	str	x1,[x2]
.ub:	bl	update_buttons
	cmp	x20,UN_ACTIVATE			; double-clic : modifier
	b.ne	.out
	b	.edit
.add:	cmp	x20,UN_ACTIVATE
	b.ne	.out
	adr	x0,w_add
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.ed
	mov	x0,-1
	bl	alarm_edit
	b	.out
.ed:	adr	x0,w_edit
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.tg
.edit:	adr	x0,g_sel_id
	ldr	x0,[x0]
	tbnz	x0,63,.out
	bl	alarm_edit
	b	.out
.tg:	adr	x0,w_toggle
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.del
	bl	alarm_toggle
	b	.out
.del:	adr	x0,w_del
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.out
	bl	alarm_delete
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  La liste
; ---------------------------------------------------------------------------

; x0 = numero d'alarme -> x0 = cle de tri (l'heure ; invalides a la fin)
sort_key:
	adr	x1,al_tab
	mov	x2,AL_SIZE
	madd	x1,x0,x2,x1
	ldrb	w2,[x1,AL_VALID]
	mov	x0,9999
	cbz	w2,.r
	ldrb	w2,[x1,AL_HH]
	ldrb	w3,[x1,AL_MM]
	mov	x0,60
	madd	x0,x2,x0,x3
.r:	ret

; g_ord <- les numeros d'alarme, dans l'ordre des heures (insertion : stable)
sort_alarms:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x19,g_ord
	adr	x0,al_n
	ldr	x20,[x0]
	mov	x0,0
.i:	cmp	x0,x20
	b.hs	.s
	str	x0,[x19,x0,lsl 3]
	add	x0,x0,1
	b	.i
.s:	mov	x21,1
.o:	cmp	x21,x20
	b.hs	.out
	ldr	x22,[x19,x21,lsl 3]		; k
	mov	x0,x22
	bl	sort_key
	mov	x23,x0				; sa cle
	mov	x24,x21				; j
.j:	cbz	x24,.put
	sub	x0,x24,1
	ldr	x0,[x19,x0,lsl 3]
	bl	sort_key
	cmp	x0,x23
	b.ls	.put
	sub	x0,x24,1
	ldr	x1,[x19,x0,lsl 3]
	str	x1,[x19,x24,lsl 3]
	mov	x24,x0
	b	.j
.put:	str	x22,[x19,x24,lsl 3]
	add	x21,x21,1
	b	.o
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la page Alarmes refaite : la liste, la prochaine alarme, les boutons
alarms_refresh:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	al_now
	mov	x19,x0
	adr	x1,g_minute
	str	x0,[x1]
	adr	x0,g_page			; (l'onglet Horloge : plus tard)
	ldr	x0,[x0]
	cbz	x0,.out
	bl	sort_alarms
	adr	x0,w_list
	ldr	x0,[x0]
	bl	ui_list_clear
	mov	x20,0
	mov	x21,-1				; la ligne de l'alarme choisie
.r:	adr	x0,al_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.sel
	adr	x0,g_ord
	ldr	x0,[x0,x20,lsl 3]
	adr	x22,al_tab
	mov	x1,AL_SIZE
	madd	x22,x0,x1,x22
	mov	x0,x22
	mov	x1,x19
	bl	row_text
	adr	x0,w_list
	ldr	x0,[x0]
	adr	x1,g_row
	ldrsw	x2,[x22,AL_ID]
	bl	ui_list_add
	ldrsw	x0,[x22,AL_ID]
	adr	x1,g_sel_id
	ldr	x1,[x1]
	cmp	x0,x1
	csel	x21,x20,x21,eq
	add	x20,x20,1
	b	.r
.sel:	tbz	x21,63,.set
	mov	x0,-1
	adr	x1,g_sel_id
	str	x0,[x1]
	b	.nt
.set:	adr	x0,w_list
	ldr	x0,[x0]
	mov	x1,x21
	bl	ui_w_set_value
.nt:	bl	next_text
	adr	x0,w_next
	ldr	x0,[x0]
	adr	x1,g_line
	bl	ui_w_set_text
	bl	update_buttons
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = texte a traduire -> x0 apres
put_tr:
	prologue
	str	x0,[sp,-16]!
	mov	x0,x1
	bl	tr_x0
	mov	x1,x0
	ldr	x0,[sp],16
	bl	str_cat
	epilogue

; x0 = destination, x1 = alarme -> x0 apres son nom (« Alarme » sans nom)
put_label:
	add	x1,x1,AL_LABEL
	ldrb	w2,[x1]
	cbnz	w2,str_cat
	adr	x1,s_alarm
	b	put_tr

; x0 = destination, x1 = jours (bits) -> x0 apres « Une fois », « En semaine »,
; ou « Lun, Mer, Ven »
repeat_text:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	and	x20,x1,AL_DAYS_ALL
	mov	x19,x0
	adr	x1,s_once
	cbz	x20,.one
	adr	x1,s_daily
	cmp	x20,AL_DAYS_ALL
	b.eq	.one
	adr	x1,s_wkd
	cmp	x20,AL_WEEKDAYS
	b.eq	.one
	adr	x1,s_wke
	cmp	x20,AL_WEEKEND
	b.eq	.one
	mov	x21,0				; les jours
	mov	x22,0				; un deja ecrit
.d:	lsr	x0,x20,x21
	tbz	x0,0,.n
	mov	x0,x19
	cbz	x22,.w
	adr	x1,s_comma
	bl	str_cat
.w:	adr	x1,s_days
	add	x1,x1,x21,lsl 2
	bl	put_tr
	mov	x19,x0
	mov	x22,1
.n:	add	x21,x21,1
	cmp	x21,7
	b.lo	.d
	mov	x0,x19
	b	.out
.one:	mov	x0,x19
	bl	put_tr
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = alarme, x1 = maintenant -> x0 = 1 : montree active (valide, active, pas
; une alarme d'une fois deja passee)
shown_on:
	ldrb	w2,[x0,AL_VALID]
	cbz	w2,.no
	ldrb	w2,[x0,AL_ON]
	cbz	w2,.no
	stp	x29,x30,[sp,-16]!
	bl	al_passed
	ldp	x29,x30,[sp],16
	eor	x0,x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = alarme, x1 = maintenant -> g_row : « 07:00 <tab> Nom <tab> Repetition
; <tab> Etat »
row_text:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,g_row
	ldrb	w1,[x19,AL_VALID]
	cbz	w1,.inv
	mov	x1,x19
	bl	al_fmt_hm
	b	.t1
.inv:	adr	x1,s_nohm
	bl	str_cat
.t1:	bl	put_tab
	mov	x1,x19
	bl	put_label
	bl	put_tab
	ldrb	w1,[x19,AL_DAYS]
	bl	repeat_text
	bl	put_tab
	str	x0,[sp,-16]!
	adr	x1,s_st_inv			; l'etat
	ldrb	w2,[x19,AL_VALID]
	cbz	w2,.st
	adr	x1,s_st_mis			; (une sonnerie sans reponse : dite)
	ldr	x2,[x19,AL_MISSED]
	tbz	x2,63,.st
	mov	x0,x19
	mov	x1,x20
	bl	shown_on
	adr	x1,s_st_off
	cbz	x0,.st
	adr	x1,s_st_snz
	ldr	x2,[x19,AL_SNOOZE]
	cmp	x2,x20
	b.gt	.st
	adr	x1,s_st_on
.st:	ldr	x0,[sp],16
	bl	put_tr
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fin -> une tabulation
put_tab:
	mov	w1,9
	strb	w1,[x0],1
	strb	wzr,[x0]
	ret

; g_line <- « Prochaine alarme : demain 07:00 — dans 18 h 26 min »,
; « Reportee jusqu'a 07:10 — Nom », « Aucune alarme active »
next_text:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,g_minute
	ldr	x19,[x0]			; maintenant
	mov	x0,x19
	bl	al_next_all
	mov	x20,x0				; quand
	adr	x22,g_line
	tbz	x20,63,.some
	mov	x0,x22
	adr	x1,s_none
	bl	put_tr
	b	.out
.some:	adr	x21,al_tab
	mov	x2,AL_SIZE
	madd	x21,x1,x2,x21			; l'alarme
	ldr	x0,[x21,AL_SNOOZE]
	cmp	x0,x20
	b.ne	.next
	mov	x0,x22				; reportee
	adr	x1,s_snoozed
	bl	put_tr
	bl	put_space
	mov	x1,x20
	bl	al_fmt_min_hm
	adr	x1,s_dash
	bl	str_cat
	mov	x1,x21
	bl	put_label
	b	.out
.next:	mov	x0,x22
	adr	x1,s_next
	bl	put_tr
	bl	put_space
	mov	x22,x0
	mov	x1,1440				; le jour : aujourd'hui, demain, lundi...
	udiv	x2,x20,x1
	udiv	x3,x19,x1
	sub	x3,x2,x3
	adr	x1,s_today
	cbz	x3,.dw
	adr	x1,s_tomorrow
	cmp	x3,1
	b.eq	.dw
	mov	x0,x2
	bl	al_wday
	adr	x1,w_wdays
	ldr	x1,[x1,x0,lsl 3]
.dw:	mov	x0,x22
	bl	put_tr
	bl	put_space
	mov	x1,x20
	bl	al_fmt_min_hm
	adr	x1,s_dash
	bl	str_cat
	adr	x1,s_in
	bl	put_tr
	bl	put_space
	mov	x22,x0
	sub	x20,x20,x19			; minutes d'ici la
	mov	x1,60
	udiv	x21,x20,x1			; heures
	msub	x20,x21,x1,x20
	cbz	x21,.m
	mov	x0,x21
	mov	x1,x22
	bl	fmt_dec
	adr	x1,s_h
	bl	str_cat
	mov	x22,x0
.m:	mov	x0,x20
	mov	x1,x22
	bl	fmt_dec
	adr	x1,s_min
	bl	str_cat
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

put_space:
	mov	w1,' '
	strb	w1,[x0],1
	strb	wzr,[x0]
	ret

; -> x0 = l'alarme choisie (0 : aucune)
sel_alarm:
	adr	x0,g_sel_id
	ldr	x0,[x0]
	tbnz	x0,63,.no
	b	al_find
.no:	mov	x0,0
	ret

; Modifier, Activer/Desactiver, Supprimer : avec une alarme choisie ;
; Ajouter : 20 au plus
update_buttons:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	sel_alarm
	mov	x19,x0
	cmp	x19,0
	cset	x20,ne
	adr	x0,w_edit
	ldr	x0,[x0]
	mov	x1,x20
	bl	ui_w_enable
	adr	x0,w_toggle
	ldr	x0,[x0]
	mov	x1,x20
	bl	ui_w_enable
	adr	x0,w_del
	ldr	x0,[x0]
	mov	x1,x20
	bl	ui_w_enable
	adr	x0,al_n
	ldr	x0,[x0]
	cmp	x0,AL_MAX
	cset	x1,lo
	adr	x0,w_add
	ldr	x0,[x0]
	bl	ui_w_enable
	cbz	x19,.out
	bl	al_now
	mov	x1,x0
	mov	x0,x19
	bl	shown_on
	adr	x1,s_b_off
	cbnz	x0,.t
	adr	x1,s_b_on
.t:	mov	x0,x1
	bl	tr_x0
	mov	x1,x0
	adr	x0,w_toggle
	ldr	x0,[x0]
	bl	ui_w_set_text
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Changer les alarmes : le fichier relu, change, ecrit, publie
; ---------------------------------------------------------------------------

; ecrit, publie, la page refaite
commit:
	prologue
	bl	al_save
	cbz	x0,.ok
	adr	x0,title
	bl	tr_x0
	str	x0,[sp,-16]!
	adr	x0,s_err
	bl	tr_x0
	mov	x1,x0
	ldr	x0,[sp],16
	mov	x2,UM_OK
	bl	ui_message
.ok:	bl	al_publish
	bl	alarms_refresh
	epilogue

; l'alarme choisie : activee ou desactivee
alarm_toggle:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	al_load
	bl	sel_alarm
	cbz	x0,.out
	mov	x19,x0
	bl	al_now
	mov	x20,x0
	mov	x0,x19
	mov	x1,x20
	bl	shown_on
	mov	x1,-1
	str	x1,[x19,AL_SNOOZE]
	cbz	x0,.on
	strb	wzr,[x19,AL_ON]			; desactivee
	b	.w
.on:	mov	w1,1				; activee : une alarme d'une fois
	strb	w1,[x19,AL_ON]			; prend sa prochaine heure
	mov	x1,-1
	str	x1,[x19,AL_MISSED]
	ldrb	w1,[x19,AL_DAYS]
	ands	w1,w1,AL_DAYS_ALL
	b.ne	.w
	ldrb	w0,[x19,AL_HH]
	ldrb	w1,[x19,AL_MM]
	mov	x2,x20
	bl	al_once_day
	str	x0,[x19,AL_DATE]
.w:	bl	commit
.out:	ldp	x19,x20,[sp],16
	epilogue

; l'alarme choisie : supprimee (sans question, comme Onyx)
alarm_delete:
	prologue
	adr	x0,g_sel_id
	ldr	x0,[x0]
	tbnz	x0,63,.out
	bl	al_load
	adr	x0,g_sel_id
	ldr	x0,[x0]
	bl	al_remove
	cbz	x0,.out
	mov	x0,-1
	adr	x1,g_sel_id
	str	x0,[x1]
	bl	commit
.out:	epilogue

; ---------------------------------------------------------------------------
;  Le dialogue d'une alarme
; ---------------------------------------------------------------------------

; x0, x1, x2, x3 = rectangle, x4 = texte : une etiquette du dialogue
dlg_label:
	mov	x5,0
	b	ui_label

; x0 = id (-1 : une nouvelle) : le dialogue ; OK : ecrite
alarm_edit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	adr	x20,g_edit
	tbnz	x19,63,.new
	bl	al_find
	cbz	x0,.out
	mov	x1,x0
	mov	x0,x20
	mov	x2,AL_SIZE
	bl	mem_move
	adr	x2,s_t_edit
	b	.dlg
.new:	adr	x0,al_n
	ldr	x0,[x0]
	cmp	x0,AL_MAX
	b.hs	.out
	mov	x0,x20
	bl	al_blank
	mov	w1,1
	strb	w1,[x20,AL_VALID]
	adr	x2,s_t_new
.dlg:	mov	x0,DLG_W
	mov	x1,DLG_H
	bl	tr_x2
	bl	ui_dialog_begin
	cbz	x0,.out
	mov	x21,x0
	mov	x0,M				; l'heure
	mov	x1,50
	mov	x2,90
	mov	x3,20
	adr	x4,s_l_time
	bl	dlg_label
	mov	x0,110
	mov	x1,44
	mov	x2,110
	mov	x3,30
	mov	x4,0
	mov	x5,23
	ldrb	w6,[x20,AL_HH]
	bl	ui_numeric
	adr	x1,d_hh
	str	x0,[x1]
	mov	x0,222
	mov	x1,50
	mov	x2,16
	mov	x3,20
	adr	x4,s_colon
	mov	x5,ULB_CENTER
	bl	ui_label
	mov	x0,240
	mov	x1,44
	mov	x2,110
	mov	x3,30
	mov	x4,0
	mov	x5,59
	ldrb	w6,[x20,AL_MM]
	bl	ui_numeric
	adr	x1,d_mm
	str	x0,[x1]
	mov	x0,M				; le nom
	mov	x1,90
	mov	x2,90
	mov	x3,20
	adr	x4,s_l_name
	bl	dlg_label
	mov	x0,110
	mov	x1,84
	mov	x2,DLG_W-110-M
	mov	x3,30
	mov	x4,0
	bl	ui_textbox
	adr	x1,d_name
	str	x0,[x1]
	mov	x22,x0
	mov	x0,x22
	mov	x1,0
	mov	x2,40				; (40 caracteres, comme Onyx)
	mov	x3,0
	mov	x4,0
	bl	ui_w_set_range
	mov	x0,x22
	add	x1,x20,AL_LABEL
	bl	ui_w_set_text
	adr	x0,s_alarm
	bl	tr_x0
	mov	x1,x0
	mov	x0,x22
	bl	ui_textbox_hint
	mov	x0,M				; la repetition
	mov	x1,130
	mov	x2,90
	mov	x3,20
	adr	x4,s_l_rep
	bl	dlg_label
	mov	x0,110
	mov	x1,124
	mov	x2,DLG_W-110-M
	mov	x3,30
	adr	x4,s_reps
	bl	ui_dropdown
	adr	x1,d_rep
	str	x0,[x1]
	mov	x22,x0
	ldrb	w0,[x20,AL_DAYS]
	bl	rep_kind
	mov	x1,x0
	mov	x0,x22
	bl	ui_w_set_value
	mov	x23,0				; les sept jours
.dy:	mov	x0,57
	mul	x0,x0,x23
	add	x0,x0,M
	mov	x1,164
	mov	x2,56
	mov	x3,30
	adr	x4,s_days
	add	x4,x4,x23,lsl 2
	ldrb	w5,[x20,AL_DAYS]
	lsr	w5,w5,w23
	and	w5,w5,1
	bl	ui_checkbox
	adr	x1,d_days
	str	x0,[x1,x23,lsl 3]
	add	x23,x23,1
	cmp	x23,7
	b.lo	.dy
	mov	x0,M				; le son
	mov	x1,210
	mov	x2,90
	mov	x3,20
	adr	x4,s_l_sound
	bl	dlg_label
	mov	x0,110
	mov	x1,204
	mov	x2,170
	mov	x3,30
	adr	x4,s_sounds
	bl	ui_dropdown
	adr	x1,d_snd
	str	x0,[x1]
	ldrb	w1,[x20,AL_SOUND]
	bl	ui_w_set_value
	mov	x0,290
	mov	x1,204
	mov	x2,DLG_W-290-M
	mov	x3,30
	adr	x4,s_test
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,d_test
	str	x0,[x1]
	mov	x0,110				; active
	mov	x1,248
	mov	x2,DLG_W-110-M
	mov	x3,30
	adr	x4,s_l_on
	ldrb	w5,[x20,AL_ON]
	bl	ui_switch
	adr	x1,d_on
	str	x0,[x1]
	mov	x0,x21				; OK, Annuler
	mov	x1,0
	adr	x2,s_ok
	mov	x3,1
	mov	x4,UB_DEFAULT
	mov	x5,DLG_W-12
	bl	ui_dialog_button
	mov	x5,x0
	mov	x0,x21
	mov	x1,1
	adr	x2,s_cancel
	mov	x3,0
	mov	x4,0
	bl	ui_dialog_button
	mov	x0,x21
	mov	x1,0
	bl	ui_dialog_cancel
	mov	x0,x21
	adr	x1,dlg_tick
	mov	x2,20
	bl	ui_dialog_timer
	adr	x0,d_name
	ldr	x0,[x0]
	bl	ui_w_focus
	mov	x0,x21
	adr	x1,dlg_on
	bl	ui_dialog_run
	mov	x22,x0
	cbz	x22,.end
	adr	x0,d_hh				; OK : les valeurs (avant ui_dialog_end)
	ldr	x0,[x0]
	bl	ui_w_get_value
	cmp	x0,23
	csel	x0,x0,xzr,ls
	strb	w0,[x20,AL_HH]
	adr	x0,d_mm
	ldr	x0,[x0]
	bl	ui_w_get_value
	cmp	x0,59
	csel	x0,x0,xzr,ls
	strb	w0,[x20,AL_MM]
	adr	x0,d_name
	ldr	x0,[x0]
	bl	ui_w_get_text
	mov	x1,x0
	add	x0,x20,AL_LABEL
	cbz	x1,.nn
	bl	al_label_copy
	b	.dd
.nn:	strb	wzr,[x0]
.dd:	bl	dlg_days
	strb	w0,[x20,AL_DAYS]
	adr	x0,d_snd
	ldr	x0,[x0]
	bl	ui_w_get_value
	cmp	x0,AL_SOUNDS
	csel	x0,x0,xzr,lo
	strb	w0,[x20,AL_SOUND]
	adr	x0,d_on
	ldr	x0,[x0]
	bl	ui_w_get_value
	cmp	x0,0
	cset	w0,ne
	strb	w0,[x20,AL_ON]
.end:	mov	x0,x21
	bl	ui_dialog_end
	bl	gs_close			; (le son d'Essayer)
	cbz	x22,.out
	mov	x0,x19
	bl	edit_commit
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = jours -> x0 = 0 une fois, 1 tous les jours, 2 en semaine, 3 le
; week-end, 4 jours choisis
rep_kind:
	and	x0,x0,AL_DAYS_ALL
	adr	x1,rep_masks
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	cmp	w3,w0
	b.eq	.r
	add	x2,x2,1
	cmp	x2,4
	b.lo	.c
.r:	mov	x0,x2
	ret

; -> x0 = les jours coches du dialogue
dlg_days:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
	mov	x20,0
.c:	adr	x0,d_days
	ldr	x0,[x0,x19,lsl 3]
	bl	ui_w_get_value
	cbz	x0,.n
	mov	x1,1
	lsl	x1,x1,x19
	orr	x20,x20,x1
.n:	add	x19,x19,1
	cmp	x19,7
	b.lo	.c
	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = jours : les cases du dialogue cochees ainsi
dlg_set_days:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
	mov	x20,x0
.c:	adr	x0,d_days
	ldr	x0,[x0,x19,lsl 3]
	lsr	x1,x20,x19
	and	x1,x1,1
	bl	ui_w_set_value
	add	x19,x19,1
	cmp	x19,7
	b.lo	.c
	ldp	x19,x20,[sp],16
	epilogue

; (fonction du dialogue) x0 = dialogue, x1 = widget, x2 = code : la
; repetition et les jours se suivent ; Essayer joue un tour du son
dlg_on:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	mov	x19,x1
	mov	x20,x2
	adr	x0,d_rep
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.days
	cmp	x20,UN_CHANGE
	b.ne	.out
	bl	ui_w_get_value
	cmp	x0,4
	b.hs	.out				; (jours choisis : les cases restent)
	adr	x1,rep_masks
	ldrb	w0,[x1,x0]
	bl	dlg_set_days
	b	.out
.days:	cmp	x20,UN_CHANGE
	b.ne	.test
	adr	x1,d_days
	mov	x2,0
.c:	ldr	x3,[x1,x2,lsl 3]
	cmp	x3,x19
	b.eq	.day
	add	x2,x2,1
	cmp	x2,7
	b.lo	.c
	b	.out
.day:	bl	dlg_days
	bl	rep_kind
	mov	x1,x0
	adr	x0,d_rep
	ldr	x0,[x0]
	bl	ui_w_set_value
	b	.out
.test:	cmp	x20,UN_ACTIVATE
	b.ne	.out
	adr	x0,d_name			; Entree dans le nom : OK
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.tb
	mov	x0,x21
	mov	x1,1
	bl	ui_dialog_close
	b	.out
.tb:	adr	x0,d_test
	ldr	x0,[x0]
	cmp	x19,x0
	b.ne	.out
	adr	x0,gs_fm
	ldr	x0,[x0]
	cbnz	x0,.play
	bl	gs_open
.play:	adr	x0,d_snd
	ldr	x0,[x0]
	bl	ui_w_get_value
	cmp	x0,AL_SOUNDS
	csel	x0,x0,xzr,lo
	bl	al_sound
	bl	gs_seq
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (minuteur du dialogue) le son d'Essayer avance
dlg_tick:
	prologue
	bl	gs_tick
	epilogue

; x0 = id (-1 : nouvelle) : g_edit dans le fichier relu, puis ecrit
edit_commit:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x20,g_edit
	mov	x0,-1
	str	x0,[x20,AL_SNOOZE]
	str	x0,[x20,AL_MISSED]
	str	x0,[x20,AL_DATE]
	ldrb	w0,[x20,AL_DAYS]		; une fois : sa prochaine heure
	ands	w0,w0,AL_DAYS_ALL
	b.ne	.ld
	bl	al_now
	mov	x2,x0
	ldrb	w0,[x20,AL_HH]
	ldrb	w1,[x20,AL_MM]
	bl	al_once_day
	str	x0,[x20,AL_DATE]
.ld:	bl	al_load
	tbnz	x19,63,.add
	mov	x0,x19
	bl	al_find
	cbnz	x0,.cp
.add:	adr	x0,al_n				; nouvelle : a la fin, un numero neuf
	ldr	x1,[x0]
	cmp	x1,AL_MAX
	b.hs	.out
	add	x2,x1,1
	str	x2,[x0]
	adr	x0,al_next_id
	ldr	x19,[x0]
	add	x2,x19,1
	str	x2,[x0]
	str	w19,[x20,AL_ID]
	adr	x0,al_tab
	mov	x2,AL_SIZE
	madd	x0,x1,x2,x0
.cp:	mov	x1,x20
	mov	x2,AL_SIZE
	bl	mem_move
	ldrsw	x0,[x20,AL_ID]
	adr	x1,g_sel_id
	str	x0,[x1]
	bl	commit
.out:	ldp	x19,x20,[sp],16
	epilogue

s_tabs		db 'Horloge|Alarmes',0
s_empty		db 0
s_c_time	db 'Heure',0
s_c_name	db 'Nom',0
s_c_rep		db 'R',0xC3,0xA9,'p',0xC3,0xA9,'tition',0
s_c_state	db 0xC3,0x89,'tat',0
s_b_add		db 'Ajouter',0
s_b_edit	db 'Modifier',0
s_b_on		db 'Activer',0
s_b_off		db 'D',0xC3,0xA9,'sactiver',0
s_b_del		db 'Supprimer',0
s_note2		db 'Les alarmes sonnent m',0xC3,0xAA,'me quand l',0x27,'Horloge est ferm',0xC3,0xA9,'e.',0
s_none		db 'Aucune alarme active',0
s_next		db 'Prochaine alarme :',0
s_snoozed	db 'Report',0xC3,0xA9,'e jusqu',0x27,0xC3,0xA0,0
s_in		db 'dans',0
s_today		db 'aujourd',0x27,'hui',0
s_tomorrow	db 'demain',0
s_h		db ' h ',0
s_min		db ' min',0
s_dash		db ' ',0xE2,0x80,0x94,' ',0
s_comma		db ', ',0
s_nohm		db '--:--',0
s_alarm		db 'Alarme',0
s_st_on		db 'Active',0
s_st_off	db 'D',0xC3,0xA9,'sactiv',0xC3,0xA9,'e',0
s_st_snz	db 'Report',0xC3,0xA9,'e',0
s_st_mis	db 'Manqu',0xC3,0xA9,'e',0
s_st_inv	db 'Invalide',0
s_once		db 'Une fois',0
s_daily		db 'Tous les jours',0
s_wkd		db 'En semaine',0
s_wke		db 'Le week-end',0
s_reps		db 'Une fois|Tous les jours|En semaine|Le week-end|Jours choisis',0
s_sounds	db 'Carillon|Bips|Marimba',0
s_days		db 'Lun',0,'Mar',0,'Mer',0,'Jeu',0,'Ven',0,'Sam',0,'Dim',0
s_wd0		db 'lundi',0
s_wd1		db 'mardi',0
s_wd2		db 'mercredi',0
s_wd3		db 'jeudi',0
s_wd4		db 'vendredi',0
s_wd5		db 'samedi',0
s_wd6		db 'dimanche',0
s_t_new		db 'Nouvelle alarme',0
s_t_edit	db 'Modifier l',0x27,'alarme',0
s_l_time	db 'Heure',0
s_colon		db ':',0
s_l_name	db 'Nom',0
s_l_rep		db 'R',0xC3,0xA9,'p',0xC3,0xA9,'tition',0
s_l_sound	db 'Son',0
s_test		db 'Essayer',0
s_l_on		db 'Active',0
s_ok		db 'OK',0
s_cancel	db 'Annuler',0
s_err		db 'Impossible d',0x27,0xC3,0xA9,'crire SD0:/etc/alarms.txt.',0
rep_masks	db 0, AL_DAYS_ALL, AL_WEEKDAYS, AL_WEEKEND
		align 8
w_wdays		dd s_wd0, s_wd1, s_wd2, s_wd3, s_wd4, s_wd5, s_wd6
		align 4

include '../../inc/tr.inc'

segment readable writeable
kit_data

title	db 'Horloge',0
p_cfg	db 'SD0:/etc/clock.cfg',0
s_12	db '12',0
s_3	db '3',0
s_6	db '6',0
s_9	db '9',0
s_ph	db '+1 h',0
s_pm	db '+1 min',0
s_mm	db 0xE2,0x88,0x92,'1 min',0
s_note	db 'Pas d',0x27,'horloge mat',0xC3,0xA9,'rielle : r',0xC3,0xA9,'glez l',0x27,'heure avec les boutons.',0
	align 8
labels	dd s_ph, s_pm, s_mm
adjust	dd 3600, 60, -60
offset	dd 0
	align 4
k_pi	dw 0x40490FDB			; pi
k_two_pi dw 0x40C90FDB			; 2 pi
k_six_deg dw 0x3DD67750			; 6 degres en radians
k_sin	dw 0x3E2AAAAB, 0x3D4CCCCD, 0x3CC30C31, 0x3C638E39, 0x3C14F209	; 1/6 1/20 1/42 1/72 1/110
k_cos	dw 0x3F000000, 0x3DAAAAAB, 0x3D088889, 0x3C924925, 0x3C360B61, 0x3BF83E10	; 1/2 1/12 1/30 1/56 1/90 1/132
cfgbuf	rb 32
		align 8
AW_N		= 7
anchors		db UA_LEFT or UA_RIGHT or UA_TOP	; prochaine alarme
		db UA_FILL				; liste
		db UA_LEFT or UA_BOTTOM			; Ajouter
		db UA_LEFT or UA_BOTTOM			; Modifier
		db UA_LEFT or UA_BOTTOM			; Activer
		db UA_LEFT or UA_BOTTOM			; Supprimer
		db UA_LEFT or UA_RIGHT or UA_BOTTOM	; note
		db 0xFF
		align 8
g_ox		dd 0				; decalage du cadran (centre), g_oy contigu
g_oy		dd 0
g_page		dd 0
g_sub		dd 0
g_minute	dd -1
g_sel_id	dd -1
w_seg		dd 0
g_aw:						; (les widgets de la page Alarmes)
w_next		dd 0
w_list		dd 0
w_add		dd 0
w_edit		dd 0
w_toggle	dd 0
w_del		dd 0
w_note		dd 0
d_hh		dd 0
d_mm		dd 0
d_name		dd 0
d_rep		dd 0
d_snd		dd 0
d_test		dd 0
d_on		dd 0
d_days		rb 7 * 8
g_ord		rb AL_MAX * 8
g_edit		rb AL_SIZE
g_row		rb 256
g_line		rb 256
alarms_data
gs_data
ui_data
