; applets/display.inc - applet Ecran du panneau de configuration : la resolution
; de l'ecran, changee a chaud (elegant/screen.inc) comme displayconf d'Onyx. La
; liste vient d'Elegant : sa table, les modes de l'ecran d'apres son EDID, la
; taille actuelle. Appliquer : l'ecran change tout de suite, puis « Garder
; cette resolution ? » : sans reponse en 15 s, ou sur Revenir, la taille d'avant
; revient (Elegant le fait aussi seul, panneau mort ou ecran noir). Gardee :
; « ecran=LxH » dans SD0:/etc/options.txt, lu par le noyau au demarrage.

EC_MAX		= WS_SM_N
EC_LABEL	= 32			; un libelle
EC_KEEP_MS	= 15000			; le temps de repondre
EC_WAIT_MS	= 17000			; ... puis Elegant revient de lui-meme
EC_DLG_W	= 440
EC_DLG_H	= 132

; x0 = op, x1..x3 = a, b, c -> x0 = resultat (reponse dans ec_msg)
ec_call:
	prologue
	mov	x4,x0
	adr	x0,ec_msg
	str	x4,[x0,WM_OP]
	stp	x1,x2,[x0,WM_A]
	str	x3,[x0,WM_C]
	str	wzr,[x0,WM_LEN]
	sys	SYS_WS_CALL
	epilogue

; x0 = fin de chaine, x1 = nombre -> x0 = apres les chiffres (0 final mis)
ec_dec:
	mov	x2,1
	mov	x3,10
.p:	mul	x4,x2,x3
	cmp	x4,x1
	b.hi	.d
	mov	x2,x4
	b	.p
.d:	udiv	x4,x1,x2
	msub	x1,x4,x2,x1
	add	w4,w4,'0'
	strb	w4,[x0],1
	udiv	x2,x2,x3
	cbnz	x2,.d
	strb	wzr,[x0]
	ret

; x0 = fin de chaine, x1 = chaine -> x0 = sur le 0 final
ec_str:
	ldrb	w2,[x1],1
	strb	w2,[x0]
	cbz	w2,.ret
	add	x0,x0,1
	b	ec_str
.ret:	ret

; x0 = fin de chaine, x1 = largeur, x2 = hauteur : « LxH » -> x0 = sur le 0 final
ec_size:
	prologue
	str	x2,[sp,-16]!
	bl	ec_dec
	mov	w1,'x'
	strb	w1,[x0],1
	ldr	x1,[sp],16
	bl	ec_dec
	epilogue

ec_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,ec_n
	str	xzr,[x0]
	mov	x0,WS_OP_SCREEN_MODES
	mov	x1,0
	mov	x2,0
	mov	x3,0
	bl	ec_call
	cmp	x0,0
	b.le	.head
	cmp	x0,EC_MAX
	mov	x1,EC_MAX
	csel	x0,x1,x0,hi
	adr	x1,ec_n
	str	x0,[x1]
	adr	x9,ec_msg
	ldp	x1,x2,[x9,WM_C]			; taille actuelle
	adr	x3,ec_cur
	stp	x1,x2,[x3]
	add	x1,x9,WM_DATA			; les modes
	adr	x2,ec_modes
	lsl	x0,x0,3
.cp:	ldr	x3,[x1],8
	str	x3,[x2],8
	subs	x0,x0,8
	b.ne	.cp
.head:	mov	x0,0
	adr	x1,ec_s_note
	bl	fm_debut
	adr	x0,ec_s_titre
	bl	fm_ligne
	mov	x19,0
.r:	adr	x0,ec_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.fin
	adr	x20,ec_labels			; le libelle
	add	x20,x20,x19,lsl 5
	adr	x21,ec_modes
	add	x21,x21,x19,lsl 3
	mov	x0,x20
	ldrh	w1,[x21,WSM_W]
	ldrh	w2,[x21,WSM_H]
	bl	ec_size
	ldr	w2,[x21,WSM_FLAGS]
	adr	x1,ec_s_native
	tbnz	w2,1,.suf			; WSMF_NATIVE
	adr	x1,ec_s_edid
	tbnz	w2,0,.suf			; WSMF_EDID
	b	.row
.suf:	bl	tr_cat
.row:	mov	x0,x19
	bl	ec_checked
	mov	x2,x0
	mov	x0,0
	mov	x1,x20
	bl	fm_item
	add	x19,x19,1
	b	.r
.fin:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero de mode -> x0 = 1 si c'est la taille actuelle
ec_checked:
	adr	x1,ec_modes
	add	x1,x1,x0,lsl 3
	ldrh	w2,[x1,WSM_W]
	ldrh	w3,[x1,WSM_H]
	adr	x1,ec_cur
	ldp	x4,x5,[x1]
	cmp	x2,x4
	ccmp	x3,x5,0,eq
	cset	x0,eq
	ret

; la liste revient sur la taille actuelle
ec_recheck:
	prologue
	str	x19,[sp,-16]!
	mov	x0,0
	mov	x1,-1
	bl	fm_choisir
	mov	x19,0
.r:	adr	x0,ec_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	mov	x0,x19
	bl	ec_checked
	add	x19,x19,1
	cbz	x0,.r
	mov	x0,0
	sub	x1,x19,1
	bl	fm_choisir
.out:	ldr	x19,[sp],16
	epilogue

; Appliquer : la taille choisie tout de suite, gardee si l'utilisateur le dit
ec_appliquer:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,0
	bl	fm_valeur
	tbnz	x0,63,.out
	adr	x1,ec_modes
	add	x1,x1,x0,lsl 3
	ldrh	w19,[x1,WSM_W]
	ldrh	w20,[x1,WSM_H]
	adr	x0,ec_cur			; deja a cette taille : rien a confirmer
	ldp	x0,x1,[x0]
	cmp	x0,x19
	ccmp	x1,x20,0,eq
	b.eq	.save
	mov	x0,WS_OP_SCREEN_SET
	mov	x1,x19
	mov	x2,x20
	movl	x3,EC_WAIT_MS
	bl	ec_call
	adr	x1,ec_s_refus
	tbnz	x0,63,.msg
	bl	ec_confirm
	mov	x1,x0
	cmp	x1,1
	b.eq	.keep
	mov	x0,WS_OP_SCREEN_KEEP		; (Revenir, ou plus de temps)
	mov	x2,0
	mov	x3,0
	bl	ec_call
	bl	ec_recheck
	adr	x1,ec_s_retour
	b	.msg
.keep:	mov	x0,WS_OP_SCREEN_KEEP
	mov	x2,0
	mov	x3,0
	bl	ec_call
	adr	x0,ec_cur
	stp	x19,x20,[x0]
.save:	adr	x0,ec_value
	mov	x1,x19
	mov	x2,x20
	bl	ec_size
	adr	x0,p_options
	adr	x1,ec_k_ecran
	adr	x2,ec_value
	bl	ini_set
	adr	x1,ec_s_fait
.msg:	adr	x0,ap_msg
	str	x1,[x0]
	bl	ap_fit
.out:	ldp	x19,x20,[sp],16
	epilogue

; « Garder cette resolution ? » -> x0 = 1 Garder, 0 Revenir (ou le temps ecoule)
ec_confirm:
	prologue
	str	x19,[sp,-16]!
	sys	SYS_TIME_MS
	adr	x1,ec_t0
	str	x0,[x1]
	adr	x0,ec_left
	str	xzr,[x0]
	mov	x0,EC_DLG_W
	mov	x1,EC_DLG_H
	adr	x2,ec_titre
	bl	ui_dialog_begin
	cbz	x0,.out
	mov	x19,x0
	bl	ec_text
	mov	x0,16				; la question
	mov	x1,14
	mov	x2,EC_DLG_W-32
	mov	x3,24
	adr	x4,ec_line
	mov	x5,0
	bl	ui_label
	adr	x1,ec_w_label
	str	x0,[x1]
	mov	x0,16				; le temps qui reste
	mov	x1,44
	mov	x2,EC_DLG_W-32
	mov	x3,10
	movl	x4,EC_KEEP_MS
	movl	x5,EC_KEEP_MS
	bl	ui_progress
	adr	x1,ec_w_bar
	str	x0,[x1]
	mov	x0,x19
	mov	x1,0
	adr	x2,ec_s_garder
	mov	x3,1
	mov	x4,UB_DEFAULT
	mov	x5,EC_DLG_W-12
	bl	ui_dialog_button
	mov	x5,x0
	mov	x0,x19
	mov	x1,1
	adr	x2,ec_s_revenir
	mov	x3,0
	mov	x4,0
	bl	ui_dialog_button
	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_cancel
	mov	x0,x19
	adr	x1,ec_tick
	mov	x2,200
	bl	ui_dialog_timer
	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_run
	mov	x1,x0
	mov	x0,x19
	mov	x19,x1
	bl	ui_dialog_end
	mov	x0,x19
.out:	ldr	x19,[sp],16
	epilogue

; (minuteur du dialogue) x0 = dialogue
ec_tick:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	sys	SYS_TIME_MS
	adr	x1,ec_t0
	ldr	x1,[x1]
	sub	x20,x0,x1
	movl	x0,EC_KEEP_MS
	subs	x20,x0,x20			; reste
	b.le	.fin
	adr	x0,ec_w_bar
	ldr	x0,[x0]
	mov	x1,x20
	bl	ui_w_set_value
	bl	ec_text
	cbz	x0,.draw
	adr	x0,ec_w_label
	ldr	x0,[x0]
	adr	x1,ec_line
	bl	ui_w_set_text
.draw:	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	b	.out
.fin:	mov	x0,x19
	mov	x1,0
	bl	ui_dialog_close
.out:	ldp	x19,x20,[sp],16
	epilogue

; ec_line <- la question, secondes qui restent -> x0 = 1 si elle a change
ec_text:
	prologue
	sys	SYS_TIME_MS
	adr	x1,ec_t0
	ldr	x1,[x1]
	sub	x0,x0,x1
	movl	x1,EC_KEEP_MS+999
	subs	x1,x1,x0
	csel	x1,xzr,x1,lt
	mov	x2,1000
	udiv	x1,x1,x2
	adr	x2,ec_left
	ldr	x3,[x2]
	mov	x0,0
	cmp	x1,x3
	b.eq	.out
	str	x1,[x2]
	str	x1,[sp,-16]!
	adr	x0,ec_line
	adr	x1,ec_s_q1
	bl	tr_cat
	ldr	x1,[sp],16
	bl	ec_dec
	adr	x1,ec_s_q2
	bl	ec_str
	mov	x0,1
.out:	epilogue

ec_titre	db 0xC3,0x89,'cran',0
ec_s_titre	db 'R',0xC3,0xA9,'solution',0
ec_s_note	db 'Le natif est le plus net.',0
ec_s_native	db ' (natif)',0
ec_s_edid	db ' (',0xC3,0xA9,'cran)',0
ec_s_fait	db 'Gard',0xC3,0xA9,'e, aussi au prochain d',0xC3,0xA9,'marrage.',0
ec_s_retour	db 'La r',0xC3,0xA9,'solution d',0x27,'avant est revenue.',0
ec_s_refus	db 'Cette r',0xC3,0xA9,'solution est refus',0xC3,0xA9,'e.',0
ec_s_garder	db 'Garder',0
ec_s_revenir	db 'Revenir',0
ec_s_q1		db 'Garder cette r',0xC3,0xA9,'solution ? Retour dans ',0
ec_s_q2		db ' s.',0
ec_k_ecran	db 'ecran',0
	align 4


macro ec_data {
		align 8
ec_n		dd 0				; modes
ec_cur		rd 2				; taille actuelle
ec_t0		dd 0				; (confirmation) depart, ms
ec_left		dd 0				; secondes affichees
ec_w_label	dd 0
ec_w_bar	dd 0
ec_modes	rb EC_MAX * WSM_SIZE
ec_labels	rb EC_MAX * EC_LABEL
ec_value	rb EC_LABEL
ec_line		rb 96
ec_msg		rb WS_MSG
}
