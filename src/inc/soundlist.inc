; ============================================================================
;  inc/soundlist.inc - le son d'un volet : le volet du son de la barre
;  (src/sys/soundmenu) et le detail Son des reglages rapides (src/sys/quick).
;  L'etat vient du service sound (AudioKit, ak_state) : les sorties montrees
;  (les virtuelles seulement s'il n'y a qu'elles), la principale, son volume.
;
;	segment executable (apres kit_use appkit, uikit, audiokit) :
;		include '../../inc/sounddraw.inc' ; include '../../inc/soundlist.inc'
;	donnees : soundlist_data
;
;	bl ak_state ; (sl_st) ; bl sl_collect
;	bl sl_changed		; -> 1 si l'etat a change (relu)
;	sl_sx, sl_sw = curseur ; bl sl_draw_slider (x0 = y) ; bl sl_set_volume (x0 = x)
;	bl sl_draw_outputs	; x0 = y, x1 = largeur -> y apres
;	bl sl_click_outputs	; x0 = y -> 1 : nouvelle principale
;	bl sl_toggle_mute
;  Le programme inclut tr.inc.
; ============================================================================

SL_ROWH		= 26			; une sortie

; les sorties a montrer (sl_shown : leurs entrees dans l'etat), la principale
; (sl_mainp)
sl_collect:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,sl_st
	ldr	x19,[x0]
	ldr	x0,[x19,SS_GEN]
	adr	x1,sl_gen
	str	x0,[x1]
	adr	x0,sl_mainp
	str	xzr,[x0]
	mov	x21,0				; une sortie reelle ?
	add	x20,x19,SS_OUTS
	mov	x22,SS_OUT_MAX
.r:	ldr	w0,[x20,SO_ID]
	cbz	w0,.rn
	ldr	w0,[x20,SO_TYPE]
	cmp	w0,SND_OUT_VIRTUAL
	cinc	x21,x21,ne
.rn:	add	x20,x20,64
	subs	x22,x22,1
	b.ne	.r
	mov	x9,0				; montrees
	add	x20,x19,SS_OUTS
	mov	x22,SS_OUT_MAX
.o:	ldr	w0,[x20,SO_ID]
	cbz	w0,.on
	ldr	x1,[x19,SS_MAIN]
	cmp	x0,x1
	b.ne	.v
	adr	x1,sl_mainp
	str	x20,[x1]
.v:	ldr	w0,[x20,SO_TYPE]
	cmp	w0,SND_OUT_VIRTUAL
	b.ne	.add
	cbnz	x21,.on				; virtuelle cachee
.add:	adr	x1,sl_shown
	str	x20,[x1,x9,lsl 3]
	add	x9,x9,1
.on:	add	x20,x20,64
	subs	x22,x22,1
	b.ne	.o
	adr	x0,sl_nshow
	str	x9,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 si l'etat a change depuis le dernier sl_collect (il est relu)
sl_changed:
	prologue
	adr	x0,sl_st
	ldr	x0,[x0]
	cbz	x0,.non
	ldr	x0,[x0,SS_GEN]
	adr	x1,sl_gen
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.non
	bl	sl_collect
	mov	x0,1
	epilogue
.non:	mov	x0,0
	epilogue

; x0 = y : haut-parleur (x 14), curseur (sl_sx, sl_sw), valeur a droite ;
; rien sans principale
sl_draw_slider:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x0
	adr	x0,sl_mainp
	ldr	x19,[x0]
	cbz	x19,.out
	bl	ui_jetons
	mov	x21,x0
	mov	x0,14
	add	x1,x20,1
	ldr	w2,[x19,SO_MUTE]
	ldr	w3,[x21,TJ_TEXTE1]
	ldr	w4,[x21,TJ_TEXTE3]
	bl	sd_speaker
	adr	x0,sl_sx
	ldp	x0,x2,[x0]
	mov	x1,x20
	ldr	w3,[x19,SO_VOLUME]
	ldr	w4,[x19,SO_MUTE]
	bl	sd_slider
	ldr	w0,[x19,SO_VOLUME]
	adr	x1,sl_num
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	mov	w1,'%'
	strb	w1,[x0],1
	strb	wzr,[x0]
	adr	x0,sl_sx
	ldp	x1,x2,[x0]
	add	x1,x1,x2
	add	x1,x1,10
	adr	x0,sl_num
	add	x2,x20,13
	ldr	w3,[x21,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = abscisse : volume de la principale d'apres le curseur
sl_set_volume:
	prologue
	str	x19,[sp,-16]!
	adr	x1,sl_mainp
	ldr	x19,[x1]
	cbz	x19,.out
	adr	x1,sl_sx
	ldp	x1,x2,[x1]
	bl	sd_slider_value
	mov	x1,x0
	ldr	w0,[x19,SO_ID]
	ldr	w2,[x19,SO_MUTE]
	bl	ak_out_volume
	bl	sl_changed
.out:	ldr	x19,[sp],16
	epilogue

; x0 = pas (+/-) : volume de la principale (clavier)
sl_step_volume:
	prologue
	str	x19,[sp,-16]!
	adr	x1,sl_mainp
	ldr	x19,[x1]
	cbz	x19,.out
	ldr	w1,[x19,SO_VOLUME]
	add	x1,x1,x0
	cmp	x1,0
	csel	x1,xzr,x1,lt
	cmp	x1,100
	mov	x2,100
	csel	x1,x2,x1,gt
	ldr	w0,[x19,SO_ID]
	ldr	w2,[x19,SO_MUTE]
	bl	ak_out_volume
	bl	sl_changed
.out:	ldr	x19,[sp],16
	epilogue

; muet <-> son, sur la principale
sl_toggle_mute:
	prologue
	adr	x0,sl_mainp
	ldr	x3,[x0]
	cbz	x3,.out
	ldr	w0,[x3,SO_ID]
	ldr	w1,[x3,SO_VOLUME]
	ldr	w2,[x3,SO_MUTE]
	eor	x2,x2,1
	bl	ak_out_volume
	bl	sl_changed
.out:	epilogue

; -> x0 = 1 si la principale est muette (ou absente)
sl_muted:
	adr	x0,sl_mainp
	ldr	x0,[x0]
	cbz	x0,.oui
	ldr	w0,[x0,SO_MUTE]
	ret
.oui:	mov	x0,1
	ret

; x0 = y, x1 = largeur : les sorties (rond plein pour la principale)
; -> x0 = y sous la liste
sl_draw_outputs:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	mov	x24,x1
	adr	x1,sl_list_y
	str	x0,[x1]
	bl	ui_jetons
	mov	x23,x0
	adr	x0,sl_mainp
	ldr	x19,[x0]
	mov	x20,0
.r:	adr	x0,sl_nshow
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.fin
	adr	x0,sl_shown
	ldr	x22,[x0,x20,lsl 3]
	ldr	w5,[x23,TJ_LIGNE_FORTE]		; rond : plein pour la principale
	cmp	x22,x19
	b.ne	.c
	ldr	w5,[x23,TJ_ACCENT]
.c:	mov	w6,w5
	mov	x0,16
	add	x1,x21,6
	mov	x2,14
	mov	x3,14
	mov	x4,7
	bl	ui_rbox
	cmp	x22,x19
	b.eq	.t
	ldr	w5,[x23,TJ_FACE]
	mov	w6,w5
	mov	x0,18
	add	x1,x21,8
	mov	x2,10
	mov	x3,10
	mov	x4,5
	bl	ui_rbox
.t:	add	x0,x22,SO_NAME
	mov	x1,40
	add	x2,x21,18
	ldr	w3,[x23,TJ_TEXTE1]
	mov	x4,13
	adr	x5,ui_font
	cmp	x22,x19
	b.ne	.tf
	adr	x5,ui_font_b
.tf:	sub	x6,x24,56
	bl	ui_text
	add	x20,x20,1
	add	x21,x21,SL_ROWH
	b	.r
.fin:	mov	x0,x21
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = y d'un clic -> x0 = 1 si c'est une sortie (elle devient la principale)
sl_click_outputs:
	prologue
	adr	x1,sl_list_y
	ldr	x1,[x1]
	subs	x0,x0,x1
	b.lt	.non
	mov	x1,SL_ROWH
	udiv	x0,x0,x1
	adr	x1,sl_nshow
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.non
	adr	x1,sl_shown
	ldr	x0,[x1,x0,lsl 3]
	ldr	w0,[x0,SO_ID]
	bl	ak_main
	bl	sl_changed
	mov	x0,1
	epilogue
.non:	mov	x0,0
	epilogue

macro soundlist_data {
		align 8
sl_st		dd 0				; tampon d'etat de sound (ak_state)
sl_gen		dd -1
sl_mainp	dd 0				; entree de la principale (0 : aucune)
sl_nshow	dd 0
sl_sx		dd 40				; curseur : x, largeur (contigus)
sl_sw		dd 184
sl_list_y	dd 0
sl_shown	rd SS_OUT_MAX
sl_num		rb 24
}
