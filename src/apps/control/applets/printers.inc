; applets/printers.inc - applet Imprimantes du panneau de configuration (printconf d'Onyx) : les imprimantes -- la PDF,
; toujours la, et celles du reseau ajoutees par leur adresse (IPP Everywhere / AirPrint : on leur demande ce qu'elles savent
; faire, SD0:/etc/printers.ini) --, celle par defaut, une page d'essai, et la file d'impression. Tout passe par
; PrinterKit (service printd, docs/IMPRESSION.md). En widgets, comme l'applet Panel.

PRA_GH		= 250			; hauteur des deux groupes du haut
PRA_NPR		= 12
PRA_NJOB	= 32
PRA_NFOUND	= 16
PRA_BTN		= 28

; x0..x3 = rectangle, x4 = texte, x5 = ULB_* -> libelle dans le groupe x24
pr_lab:
	prologue
	bl	ui_label
	str	x0,[sp,-16]!
	mov	x1,x24
	bl	ui_w_set_parent
	ldr	x0,[sp],16
	epilogue

; x0, x1 = position, x2 = largeur, x3 = texte, x4 = fonction -> bouton dans le groupe x24
pr_btn:
	prologue
	str	x4,[sp,-16]!
	mov	x4,x3
	mov	x3,PRA_BTN
	mov	x5,0
	bl	ui_pushbutton
	mov	x1,x24
	bl	ui_w_set_parent
	ldr	x1,[sp],16
	bl	dk_on
	epilogue

; x0..x3 = rectangle, x4 = fonction -> liste dans le groupe x24
pr_list:
	prologue
	str	x4,[sp,-16]!
	mov	x4,0
	bl	ui_list
	mov	x1,x24
	bl	ui_w_set_parent
	ldr	x1,[sp],16
	bl	dk_on
	epilogue

pr_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	adr	x0,ap_dessin
	adr	x1,dk_dessin
	str	x1,[x0]
	adr	x0,ap_fermer
	adr	x1,dk_fermer
	str	x1,[x0]
	adr	x0,ap_widgets
	mov	x1,1
	str	x1,[x0]
	adr	x0,ap_tick
	adr	x1,pr_tick
	str	x1,[x0]
	adr	x0,dk_ntop
	str	xzr,[x0]
	adr	x0,pr_njob
	mov	x1,-1
	str	x1,[x0]
	adr	x0,ui_w				; le volet
	ldp	x19,x20,[x0]
	sub	x19,x19,SIDE_W
	sub	x20,x20,AP_HEAD
	sub	x21,x19,30
	lsr	x21,x21,1			; x21 : largeur d'un groupe du haut
	mov	x22,SIDE_W+10			; x22 : x du groupe de gauche
	mov	x23,AP_HEAD+6			; x23 : y

	; ---- les imprimantes
	mov	x0,x22
	mov	x1,x23
	mov	x2,x21
	mov	x3,PRA_GH
	adr	x4,pr_s_printers
	bl	tr4
	bl	ui_groupbox
	bl	dk_top
	mov	x24,x0
	add	x25,x23,DK_CT
	sub	x26,x21,20			; x26 : largeur du contenu
	add	x0,x22,10
	mov	x1,x25
	mov	x2,x26
	mov	x3,96
	adr	x4,pr_on_select
	bl	pr_list
	adr	x1,pr_w_list
	str	x0,[x1]
	add	x0,x22,10
	add	x1,x25,100
	mov	x2,x26
	mov	x3,20
	adr	x4,pr_s_empty
	mov	x5,0
	bl	pr_lab
	adr	x1,pr_w_model
	str	x0,[x1]
	add	x0,x22,10
	add	x1,x25,120
	mov	x2,x26
	mov	x3,20
	adr	x4,pr_s_empty
	mov	x5,ULB_DIM
	bl	pr_lab
	adr	x1,pr_w_addr
	str	x0,[x1]
	add	x0,x22,10
	add	x1,x25,140
	mov	x2,x26
	mov	x3,20
	adr	x4,pr_s_empty
	mov	x5,ULB_DIM
	bl	pr_lab
	adr	x1,pr_w_state
	str	x0,[x1]
	add	x1,x25,166
	sub	x4,x26,18			; quatre boutons
	lsr	x4,x4,2
	mov	x25,x1
	stp	x4,xzr,[sp,-16]!
	add	x0,x22,10
	mov	x1,x25
	mov	x2,x4
	adr	x3,pr_s_default
	adr	x4,pr_on_default
	bl	pr_btn
	ldr	x2,[sp]
	add	x0,x22,16
	add	x0,x0,x2
	mov	x1,x25
	adr	x3,pr_s_test
	adr	x4,pr_on_test
	bl	pr_btn
	ldr	x2,[sp]
	add	x0,x22,22
	add	x0,x0,x2,lsl 1
	mov	x1,x25
	adr	x3,pr_s_check
	adr	x4,pr_on_check
	bl	pr_btn
	ldr	x2,[sp]
	add	x0,x2,x2,lsl 1
	add	x0,x0,x22
	add	x0,x0,28
	mov	x1,x25
	adr	x3,pr_s_remove
	adr	x4,pr_on_remove
	bl	pr_btn
	add	sp,sp,16

	; ---- ajouter une imprimante
	add	x22,x22,x21
	add	x22,x22,10
	mov	x0,x22
	mov	x1,x23
	mov	x2,x21
	mov	x3,PRA_GH
	adr	x4,pr_s_addg
	bl	tr4
	bl	ui_groupbox
	bl	dk_top
	mov	x24,x0
	add	x25,x23,DK_CT
	add	x0,x22,10
	mov	x1,x25
	mov	x2,x26
	mov	x3,40
	adr	x4,pr_s_addn
	mov	x5,ULB_WRAP
	bl	pr_lab
	add	x25,x25,44
	add	x0,x22,10
	add	x1,x25,4
	mov	x2,62
	mov	x3,20
	adr	x4,pr_s_address
	mov	x5,0
	bl	pr_lab
	add	x0,x22,74
	mov	x1,x25
	sub	x2,x26,64
	mov	x3,26
	mov	x4,0
	bl	ui_textbox
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,pr_w_naddr
	str	x0,[x1]
	adr	x1,pr_t_addr
	bl	dk_tip
	add	x25,x25,32
	add	x0,x22,10
	add	x1,x25,4
	mov	x2,62
	mov	x3,20
	adr	x4,pr_s_name
	mov	x5,0
	bl	pr_lab
	add	x0,x22,74
	mov	x1,x25
	sub	x2,x26,64
	mov	x3,26
	mov	x4,0
	bl	ui_textbox
	mov	x1,x24
	bl	ui_w_set_parent
	adr	x1,pr_w_nname
	str	x0,[x1]
	adr	x1,pr_t_name
	bl	dk_tip
	add	x25,x25,32
	add	x0,x22,74
	mov	x1,x25
	mov	x2,86
	adr	x3,pr_s_add
	adr	x4,pr_on_add
	bl	pr_btn
	add	x0,x22,166
	mov	x1,x25
	mov	x2,86
	adr	x3,pr_s_find
	adr	x4,pr_on_find
	bl	pr_btn
	add	x25,x25,34
	add	x0,x22,10
	mov	x1,x25
	mov	x2,x26
	add	x3,x23,PRA_GH-10
	sub	x3,x3,x25
	adr	x4,pr_on_found
	bl	pr_list
	adr	x1,pr_w_found
	str	x0,[x1]

	; ---- la file
	add	x23,x23,PRA_GH+6
	mov	x22,SIDE_W+10
	sub	x21,x19,20			; x21 : largeur
	add	x25,x23,0
	mov	x26,AP_HEAD+6
	add	x26,x26,x20
	sub	x26,x26,x23
	sub	x26,x26,38			; x26 : hauteur
	mov	x0,x22
	mov	x1,x23
	mov	x2,x21
	mov	x3,x26
	adr	x4,pr_s_queue
	bl	tr4
	bl	ui_groupbox
	bl	dk_top
	mov	x24,x0
	add	x0,x22,10
	add	x1,x23,DK_CT
	sub	x2,x21,150
	sub	x3,x26,DK_CT+10
	adr	x4,pr_on_job
	bl	pr_list
	adr	x1,pr_w_jobs
	str	x0,[x1]
	add	x0,x22,x21
	sub	x0,x0,130
	add	x1,x23,DK_CT
	mov	x2,120
	adr	x3,pr_s_cancelj
	adr	x4,pr_on_cancel
	bl	pr_btn
	add	x0,x22,x21
	sub	x0,x0,130
	add	x1,x23,DK_CT+34
	mov	x2,120
	adr	x3,pr_s_forget
	adr	x4,pr_on_forget
	bl	pr_btn

	; ---- la ligne d'etat
	mov	x0,x22
	add	x1,x23,x26
	add	x1,x1,6
	mov	x2,x21
	mov	x3,22
	adr	x4,pr_s_empty
	mov	x5,ULB_DIM
	bl	ui_label
	bl	dk_top
	adr	x1,pr_w_status
	str	x0,[x1]

	mov	x0,0
	bl	pr_load
	bl	pr_jobs_load
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

pr_appliquer:
	ret

; x0 = texte : la ligne d'etat, montree tout de suite
pr_status:
	prologue
	bl	ui_tr
	mov	x1,x0
	adr	x0,pr_w_status
	ldr	x0,[x0]
	bl	ui_w_set_text
	adr	x0,pr_w_status
	ldr	x0,[x0]
	bl	ui_w_draw
	epilogue

; x0 = destination, x1 = texte -> x0 = fin (le 0)
pr_cat:
.l:	ldrb	w2,[x1],1
	strb	w2,[x0]
	cbz	w2,.f
	add	x0,x0,1
	b	.l
.f:	ret

; x0 = destination, x1 = entier -> x0 = fin
pr_num:
	sub	sp,sp,32
	mov	x2,sp
	mov	x3,10
.l:	udiv	x4,x1,x3
	msub	x5,x4,x3,x1
	add	w5,w5,'0'
	strb	w5,[x2],1
	mov	x1,x4
	cbnz	x1,.l
.c:	ldrb	w5,[x2,-1]!
	strb	w5,[x0],1
	mov	x6,sp
	cmp	x2,x6
	b.hi	.c
	strb	wzr,[x0]
	add	sp,sp,32
	ret

; -> x0 = l'imprimante choisie (0 : aucune)
pr_sel:
	prologue
	adr	x0,pr_w_list
	ldr	x0,[x0]
	bl	ui_w_get_value
	adr	x1,pr_npr
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.non
	mov	x1,PR_BYTES
	adr	x2,pr_pr
	madd	x0,x0,x1,x2
	epilogue
.non:	mov	x0,0
	epilogue

; x0 = imprimante -> x0 = 1 si c'est la PDF
pr_is_pdf:
	prologue
	add	x0,x0,PR_KIND
	adr	x1,pr_s_kpdf
	bl	str_eq_kit
	epilogue

; x0, x1 = textes -> 1 si egaux
str_eq_kit:
.l:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.ne	.n
	cbnz	w2,.l
	mov	x0,1
	ret
.n:	mov	x0,0
	ret

; ce qu'on sait de l'imprimante choisie
pr_show:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	pr_sel
	mov	x19,x0
	cbz	x19,.out
	adr	x0,pr_w_model
	ldr	x0,[x0]
	add	x1,x19,PR_MODEL
	bl	ui_w_set_text
	mov	x0,x19
	bl	pr_is_pdf
	mov	x20,x0
	adr	x0,pr_s_pdfn
	bl	ui_tr
	mov	x1,x0
	cbnz	x20,.a
	add	x1,x19,PR_URI
.a:	adr	x0,pr_w_addr
	ldr	x0,[x0]
	bl	ui_w_set_text
	adr	x0,pr_ts
	strb	wzr,[x0]
	cbnz	x20,.s
	adr	x1,pr_s_colour
	ldr	w2,[x19,PR_COLOR]
	cbnz	w2,.c
	adr	x1,pr_s_bw
.c:	bl	tr_cat
	adr	x1,pr_s_comma
	bl	pr_cat
	ldr	w1,[x19,PR_DPI]
	bl	pr_num
	adr	x1,pr_s_dpi
	bl	tr_cat
	ldr	w1,[x19,PR_COPIES]
	bl	pr_num
	adr	x1,pr_s_copies
	bl	tr_cat
.s:	adr	x0,pr_w_state
	ldr	x0,[x0]
	adr	x1,pr_ts
	bl	ui_w_set_text
	mov	x0,0
	bl	ui_w_draw
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom a choisir (0 : garder) : la liste relue
pr_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,pr_w_list
	ldr	x0,[x0]
	bl	ui_w_get_value
	mov	x21,x0				; x21 : la choisie
	adr	x0,pr_pr
	mov	x1,PRA_NPR
	bl	print_printers
	adr	x1,pr_npr
	str	x0,[x1]
	adr	x0,pr_w_list
	ldr	x0,[x0]
	bl	ui_list_clear
	mov	x20,0
	adr	x22,pr_pr
.l:	adr	x0,pr_npr
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.f
	adr	x0,pr_t
	mov	x1,x22
	bl	pr_cat
	ldr	w2,[x22,PR_DEFAULT]
	cbz	w2,.a
	adr	x1,pr_s_def2
	bl	tr_cat
.a:	adr	x0,pr_w_list
	ldr	x0,[x0]
	adr	x1,pr_t
	mov	x2,x20
	bl	ui_list_add
	cbz	x19,.n
	mov	x0,x19
	mov	x1,x22
	bl	str_eq_kit
	cbz	x0,.n
	mov	x21,x20
.n:	add	x22,x22,PR_BYTES
	add	x20,x20,1
	b	.l
.f:	cmp	x21,x20
	b.lo	.k
	mov	x21,0
.k:	tbz	x21,63,.g
	mov	x21,0
.g:	adr	x0,pr_w_list
	ldr	x0,[x0]
	mov	x1,x21
	bl	ui_w_set_value
	bl	pr_show
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = etat -> x0 = son nom
pr_state:
	cmp	x0,PRINT_JOB_CANCELED
	b.ls	.o
	mov	x0,PRINT_JOB_CANCELED
.o:	adr	x1,pr_states
	ldrh	w0,[x1,x0,lsl 1]
	add	x0,x1,x0
	ret

; la file relue, si elle a change
pr_jobs_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,pr_jobn
	mov	x1,PRA_NJOB
	bl	print_jobs
	mov	x19,x0
	adr	x1,pr_njob
	ldr	x2,[x1]
	cmp	x2,x19
	b.ne	.chg
	mov	x3,PJI_BYTES
	mul	x3,x3,x19
	adr	x4,pr_jobn
	adr	x5,pr_job
.cmp:	cbz	x3,.out
	ldrb	w6,[x4],1
	ldrb	w7,[x5],1
	cmp	w6,w7
	b.ne	.chg
	sub	x3,x3,1
	b	.cmp
.chg:	adr	x1,pr_njob
	str	x19,[x1]
	adr	x0,pr_job			; (on recopie)
	adr	x1,pr_jobn
	mov	x2,PJI_BYTES*PRA_NJOB
.cp:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.cp
	adr	x0,pr_w_jobs
	ldr	x0,[x0]
	bl	ui_w_get_value
	mov	x21,x0
	adr	x0,pr_w_jobs
	ldr	x0,[x0]
	bl	ui_list_clear
	mov	x20,0
	adr	x22,pr_job
.l:	cmp	x20,x19
	b.hs	.f
	adr	x0,pr_t
	add	x1,x22,PJI_TITLE
	bl	pr_cat
	adr	x1,pr_s_dash
	bl	pr_cat
	str	x0,[sp,-16]!
	ldr	w0,[x22,PJI_STATE]
	bl	pr_state
	mov	x1,x0
	ldr	x0,[sp],16
	bl	tr_cat
	adr	x1,pr_s_paren
	bl	pr_cat
	add	x1,x22,PJI_PRINTER
	bl	pr_cat
	adr	x1,pr_s_parenf
	bl	pr_cat
	adr	x0,pr_w_jobs
	ldr	x0,[x0]
	adr	x1,pr_t
	mov	x2,x20
	bl	ui_list_add
	add	x22,x22,PJI_BYTES
	add	x20,x20,1
	b	.l
.f:	cmp	x21,x19
	b.hs	.d
	adr	x0,pr_w_jobs
	ldr	x0,[x0]
	mov	x1,x21
	bl	ui_w_set_value
.d:	adr	x0,pr_w_jobs
	ldr	x0,[x0]
	bl	ui_w_draw
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ap_tick, 4 fois par seconde : la file chaque seconde) -> 0
pr_tick:
	prologue
	adr	x0,pr_ticks
	ldr	x1,[x0]
	add	x1,x1,1
	cmp	x1,4
	csel	x1,xzr,x1,hs
	str	x1,[x0]
	cbnz	x1,.z
	bl	pr_jobs_load
.z:	mov	x0,0
	epilogue

pr_on_select:
	cmp	x1,UN_CHANGE
	b.ne	.r
	prologue
	bl	pr_show
	adr	x0,pr_s_empty
	bl	pr_status
	epilogue
.r:	ret

pr_on_job:
	cmp	x1,UN_CHANGE
	b.ne	.r
	prologue
	bl	ui_w_get_value
	adr	x1,pr_njob
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.f
	mov	x1,PJI_BYTES
	adr	x2,pr_job
	madd	x0,x0,x1,x2
	add	x0,x0,PJI_MESSAGE
	bl	pr_status
.f:	epilogue
.r:	ret

pr_on_default:
	cmp	x1,UN_ACTIVATE
	b.ne	.r
	prologue
	str	x19,[sp,-16]!
	bl	pr_sel
	cbz	x0,.f
	mov	x19,x0
	bl	print_printer_default
	cbz	x0,.f
	adr	x0,pr_nm
	mov	x1,x19
	bl	pr_cat
	adr	x0,pr_nm
	bl	pr_load
	adr	x0,pr_s_defset
	bl	pr_status
.f:	ldr	x19,[sp],16
	epilogue
.r:	ret

pr_on_remove:
	cmp	x1,UN_ACTIVATE
	b.ne	.r
	prologue
	str	x19,[sp,-16]!
	bl	pr_sel
	cbz	x0,.f
	mov	x19,x0
	bl	pr_is_pdf
	cbz	x0,.q
	adr	x0,pr_s_pdfstay
	bl	pr_status
	b	.f
.q:	adr	x0,pr_t
	adr	x1,pr_s_removeq
	bl	tr_cat
	mov	x1,x19
	bl	pr_cat
	adr	x1,pr_s_qm
	bl	pr_cat
	adr	x0,pr_s_printers
	adr	x1,pr_t
	mov	x2,UM_YESNO
	bl	ui_message
	cmp	x0,1
	b.ne	.f
	mov	x0,x19
	bl	print_printer_remove
	mov	x0,0
	bl	pr_load
	adr	x0,pr_s_removed
	bl	pr_status
.f:	ldr	x19,[sp],16
	epilogue
.r:	ret

pr_on_check:
	cmp	x1,UN_ACTIVATE
	b.ne	.r
	prologue
	str	x19,[sp,-16]!
	bl	pr_sel
	cbz	x0,.f
	mov	x19,x0
	adr	x0,pr_s_asking
	bl	pr_status
	adr	x0,pr_nm
	mov	x1,x19
	bl	pr_cat
	adr	x0,pr_nm
	adr	x1,pr_tb
	mov	x2,160
	bl	print_printer_status
	mov	x19,x0
	adr	x0,pr_t
	strb	wzr,[x0]
	cbnz	x19,.t
	adr	x1,pr_s_notready
	bl	tr_cat
.t:	adr	x1,pr_tb
	bl	pr_cat
	adr	x0,pr_nm
	bl	pr_load
	adr	x0,pr_t
	bl	pr_status
.f:	ldr	x19,[sp],16
	epilogue
.r:	ret

pr_on_add:
	cmp	x1,UN_ACTIVATE
	b.ne	.r
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,pr_w_naddr
	ldr	x0,[x0]
	bl	ui_w_get_text
	ldrb	w1,[x0]
	cbnz	w1,.a
	adr	x0,pr_s_typefirst
	bl	pr_status
	b	.f
.a:	mov	x1,x0
	adr	x0,pr_nu
	bl	pr_cat
	adr	x0,pr_w_nname
	ldr	x0,[x0]
	bl	ui_w_get_text
	mov	x1,x0
	adr	x0,pr_nm
	bl	pr_cat
	adr	x0,pr_s_asking2
	bl	pr_status
	adr	x0,pr_nm
	adr	x1,pr_nu
	adr	x2,pr_tb
	mov	x3,160
	bl	print_printer_add
	mov	x19,x0
	cbz	x19,.non
	adr	x0,pr_tb
	bl	pr_load
	adr	x0,pr_w_naddr
	ldr	x0,[x0]
	adr	x1,pr_s_empty
	bl	ui_w_set_text
	adr	x0,pr_w_nname
	ldr	x0,[x0]
	adr	x1,pr_s_empty
	bl	ui_w_set_text
	adr	x0,pr_t
	adr	x1,pr_tb
	bl	pr_cat
	adr	x1,pr_s_added
	bl	tr_cat
	b	.m
.non:	adr	x0,pr_t
	adr	x1,pr_s_notadded
	bl	tr_cat
	adr	x1,pr_tb
	bl	pr_cat
.m:	mov	x0,0
	bl	ui_w_draw
	adr	x0,pr_t
	bl	pr_status
.f:	ldp	x19,x20,[sp],16
	epilogue
.r:	ret

; x0 = rang : l'adresse et le nom d'une imprimante trouvee dans les champs
pr_found_fill:
	prologue
	str	x19,[sp,-16]!
	mov	x1,PF_BYTES
	adr	x2,pr_fnd
	madd	x19,x0,x1,x2
	adr	x0,pr_w_naddr
	ldr	x0,[x0]
	mov	x1,x19
	bl	ui_w_set_text
	adr	x0,pr_w_nname
	ldr	x0,[x0]
	add	x1,x19,PF_NAME
	bl	ui_w_set_text
	mov	x0,0
	bl	ui_w_draw
	ldr	x19,[sp],16
	epilogue

pr_on_find:
	cmp	x1,UN_ACTIVATE
	b.ne	.r
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,pr_s_searching
	bl	pr_status
	adr	x0,pr_fnd
	mov	x1,PRA_NFOUND
	bl	print_printers_find
	mov	x19,x0
	adr	x1,pr_nfnd
	str	x19,[x1]
	adr	x0,pr_w_found
	ldr	x0,[x0]
	bl	ui_list_clear
	mov	x20,0
	adr	x21,pr_fnd
.l:	cmp	x20,x19
	b.hs	.f
	adr	x0,pr_t
	add	x1,x21,PF_MODEL
	bl	pr_cat
	adr	x1,pr_s_dash
	bl	pr_cat
	mov	x1,x21
	bl	pr_cat
	ldr	w1,[x21,PF_USABLE]
	cbnz	w1,.u
	adr	x1,pr_s_notus
	bl	tr_cat
.u:	adr	x0,pr_w_found
	ldr	x0,[x0]
	adr	x1,pr_t
	mov	x2,x20
	bl	ui_list_add
	add	x21,x21,PF_BYTES
	add	x20,x20,1
	b	.l
.f:	adr	x0,pr_s_foundn
	cbnz	x19,.a
	adr	x0,pr_s_foundnone
	b	.m
.a:	cmp	x19,1
	b.ne	.m
	adr	x0,pr_w_found
	ldr	x0,[x0]
	mov	x1,0
	bl	ui_w_set_value
	mov	x0,0
	bl	pr_found_fill
	adr	x0,pr_s_foundn
.m:	bl	pr_status
	mov	x0,0
	bl	ui_w_draw
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.r:	ret

pr_on_found:
	cmp	x1,UN_CHANGE
	b.ne	.r
	prologue
	bl	ui_w_get_value
	adr	x1,pr_nfnd
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.f
	bl	pr_found_fill
.f:	epilogue
.r:	ret

pr_on_cancel:
	cmp	x1,UN_ACTIVATE
	b.ne	.r
	prologue
	adr	x0,pr_w_jobs
	ldr	x0,[x0]
	bl	ui_w_get_value
	adr	x1,pr_njob
	ldr	x1,[x1]
	cmp	x0,x1
	b.lo	.a
	adr	x0,pr_s_pickjob
	bl	pr_status
	b	.f
.a:	mov	x1,PJI_BYTES
	adr	x2,pr_job
	madd	x0,x0,x1,x2
	ldr	w0,[x0,PJI_ID]
	bl	print_job_cancel
	bl	pr_jobs_load
.f:	epilogue
.r:	ret

pr_on_forget:
	cmp	x1,UN_ACTIVATE
	b.ne	.r
	prologue
	bl	print_jobs_forget
	mov	x0,150
	sys	SYS_SLEEP
	bl	pr_jobs_load
	epilogue
.r:	ret

; --- la page d'essai : ce que l'imprimante sait faire, sur le papier --------------------------------------------------

; x0 = police, x1 = taille, x2 = x, x3 = y, x4 = texte, x5 = couleur (x19 = travail)
pr_tx:
	prologue
	bl	tr4
	mov	x6,x5
	mov	x5,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,x19
	bl	print_text
	epilogue

; x0 = x, x1 = y, x2 = l, x3 = h, x4 = couleur (x19 = travail)
pr_rc:
	mov	x5,x4
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,x19
	b	print_rect

PT_M		= 56000

pr_on_test:
	cmp	x1,UN_ACTIVATE
	b.eq	.go
	ret
.go:	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	bl	pr_sel
	cbz	x0,.fin
	mov	x28,x0				; x28 : l'imprimante
	adr	x20,pr_setup			; x20 : le reglage
	mov	x0,x20
	bl	print_setup_default
	add	x0,x20,PS_PRINTER
	mov	x1,x28
	bl	pr_cat
	mov	x0,x20
	mov	x1,0
	mov	x2,PRINT_PORTRAIT
	bl	print_setup_paper
	mov	x0,x28
	bl	pr_is_pdf
	cbz	x0,.deb
	add	x0,x20,PS_OUTPUT
	adr	x1,pr_s_testpath
	bl	pr_cat
.deb:	mov	x0,x20
	adr	x1,pr_s_testtitle
	bl	tr1
	bl	print_begin
	cbnz	x0,.jb
	adr	x0,pr_s_nospool
	bl	pr_status
	b	.fin
.jb:	mov	x19,x0				; x19 : le travail
	mov	x1,0
	mov	x2,0
	bl	print_page
	mov	x0,x19
	adr	x1,pr_s_sans
	mov	x2,0
	bl	print_font
	mov	x21,x0				; x21 : police
	mov	x0,x19
	adr	x1,pr_s_sans
	mov	x2,PRINT_BOLD
	bl	print_font
	mov	x22,x0				; x22 : gras
	mov	x0,x19
	adr	x1,pr_s_serif
	mov	x2,PRINT_ITALIC
	bl	print_font
	mov	x23,x0				; x23 : serif italique
	ldr	w24,[x20,PS_PAPER_W]
	movl	x0,PT_M*2
	sub	x24,x24,x0			; x24 : largeur utile
	movl	x25,PT_M			; x25 : y
	; le bord de ce que l'imprimante imprime
	ldr	w1,[x20,PS_MARGIN_L]
	ldr	w2,[x20,PS_MARGIN_T]
	ldr	w3,[x20,PS_PAPER_W]
	ldr	w5,[x20,PS_MARGIN_R]
	sub	x3,x3,x1
	sub	x3,x3,x5
	ldr	w4,[x20,PS_PAPER_H]
	ldr	w5,[x20,PS_MARGIN_B]
	sub	x4,x4,x2
	sub	x4,x4,x5
	mov	x0,x19
	mov	x5,500
	movl	x6,0xB0B0B0
	bl	print_frame
	mov	x0,x22
	mov	x1,26000
	movl	x2,PT_M
	movl	x17,26000
	add	x3,x25,x17
	adr	x4,pr_s_testtitle
	movl	x5,0x202020
	bl	pr_tx
	movl	x17,44000
	add	x25,x25,x17
	mov	x0,x19				; le filet
	movl	x1,PT_M
	mov	x2,x25
	add	x3,x1,x24
	mov	x4,x25
	mov	x5,1500
	movl	x6,0x4992A7
	bl	print_line
	movl	x17,22000
	add	x25,x25,x17
	adr	x0,pr_t				; les lignes de description
	adr	x1,pr_s_tp1
	bl	tr_cat
	mov	x1,x28
	bl	pr_cat
	adr	x1,pr_s_paren
	bl	pr_cat
	add	x1,x28,PR_MODEL
	bl	pr_cat
	adr	x1,pr_s_parenf
	bl	pr_cat
	mov	x0,x21
	mov	x1,11000
	movl	x2,PT_M
	mov	x3,x25
	adr	x4,pr_t
	mov	x5,0
	bl	pr_tx
	movl	x17,16000
	add	x25,x25,x17
	adr	x0,pr_t
	adr	x1,pr_s_tp2
	bl	tr_cat
	add	x1,x20,PS_MEDIA
	bl	pr_cat
	adr	x1,pr_s_comma
	bl	pr_cat
	ldr	w1,[x20,PS_PAPER_W]
	mov	x2,1000
	udiv	x1,x1,x2
	bl	pr_num
	adr	x1,pr_s_x
	bl	pr_cat
	ldr	w1,[x20,PS_PAPER_H]
	mov	x2,1000
	udiv	x1,x1,x2
	bl	pr_num
	adr	x1,pr_s_points
	bl	tr_cat
	mov	x0,x21
	mov	x1,11000
	movl	x2,PT_M
	mov	x3,x25
	adr	x4,pr_t
	mov	x5,0
	bl	pr_tx
	movl	x17,16000
	add	x25,x25,x17
	mov	x0,x21
	mov	x1,11000
	movl	x2,PT_M
	mov	x3,x25
	adr	x4,pr_s_tp3
	mov	x5,0
	bl	pr_tx
	movl	x17,30000
	add	x25,x25,x17
	; les tailles
	mov	x26,0
.sz:	adr	x0,pr_sizes
	ldrb	w27,[x0,x26]			; points
	mov	x0,1100
	mul	x0,x0,x27
	add	x25,x25,x0
	adr	x0,pr_t
	mov	x1,x27
	bl	pr_num
	adr	x1,pr_s_tp4
	bl	tr_cat
	mov	x0,x21
	mov	x1,1000
	mul	x1,x1,x27
	movl	x2,PT_M
	mov	x3,x25
	adr	x4,pr_t
	mov	x5,0
	bl	pr_tx
	mov	x0,400
	mul	x0,x0,x27
	add	x25,x25,x0
	add	x26,x26,1
	cmp	x26,5
	b.lo	.sz
	movl	x17,20000
	add	x25,x25,x17
	mov	x0,x23
	mov	x1,14000
	movl	x2,PT_M
	mov	x3,x25
	adr	x4,pr_s_tp5
	movl	x5,0x303030
	bl	pr_tx
	movl	x17,30000
	add	x25,x25,x17
	; les couleurs
	lsr	x26,x24,3			; x26 : largeur d'une case
	mov	x27,0
.col:	mul	x0,x27,x26
	movl	x1,PT_M
	add	x0,x0,x1
	mov	x1,x25
	movl	x17,6000
	sub	x2,x26,x17
	mov	x3,40000
	adr	x4,pr_cols
	ldr	w4,[x4,x27,lsl 2]
	bl	pr_rc
	mul	x2,x27,x26
	movl	x1,PT_M
	add	x2,x2,x1
	mov	x0,x21
	mov	x1,8000
	movl	x17,52000
	add	x3,x25,x17
	adr	x4,pr_colnames
	add	x4,x4,x27,lsl 3
	movl	x5,0x404040
	bl	pr_tx
	add	x27,x27,1
	cmp	x27,8
	b.lo	.col
	movl	x17,76000
	add	x25,x25,x17
	; les gris
	mov	x27,0
	lsr	x26,x24,4
.gr:	mul	x0,x27,x26
	movl	x1,PT_M
	add	x0,x0,x1
	mov	x1,x25
	add	x2,x26,200
	mov	x3,24000
	mov	x4,17
	mul	x4,x4,x27
	orr	x5,x4,x4,lsl 8
	orr	x4,x5,x4,lsl 16
	bl	pr_rc
	add	x27,x27,1
	cmp	x27,16
	b.lo	.gr
	mov	x0,x19
	movl	x1,PT_M
	mov	x2,x25
	mov	x3,x24
	mov	x4,24000
	mov	x5,500
	mov	x6,0
	bl	print_frame
	movl	x17,50000
	add	x25,x25,x17
	; les epaisseurs
	mov	x27,0
.lw:	mov	x0,x19
	movl	x1,PT_M
	mov	x2,x25
	mov	x3,45
	mul	x3,x3,x24
	mov	x4,100
	udiv	x3,x3,x4
	add	x3,x3,x1
	mov	x4,x25
	adr	x5,pr_lws
	ldrh	w5,[x5,x27,lsl 1]
	mov	x6,0
	bl	print_line
	adr	x0,pr_t				; (nombre de centiemes de point)
	adr	x1,pr_lwn
	add	x1,x1,x27,lsl 3
	bl	tr_cat
	mov	x2,47
	mul	x2,x2,x24
	mov	x3,100
	udiv	x2,x2,x3
	movl	x3,PT_M
	add	x2,x2,x3
	mov	x0,x21
	mov	x1,8000
	add	x3,x25,3000
	adr	x4,pr_t
	mov	x5,0
	bl	pr_tx
	movl	x17,14000
	add	x25,x25,x17
	add	x27,x27,1
	cmp	x27,5
	b.lo	.lw
	; les formes : un disque, un triangle
	mov	x26,80
	mul	x26,x26,x24
	mov	x0,100
	udiv	x26,x26,x0
	movl	x0,PT_M
	add	x26,x26,x0			; x26 : centre, x
	movl	x17,34000
	sub	x27,x25,x17			; x27 : centre, y
	mov	x0,x19
	movl	x17,30000
	add	x1,x26,x17
	mov	x2,x27
	bl	print_path_move
	movl	x7,16569			; (0,5523 x 30 points)
	mov	x0,x19
	movl	x17,30000
	add	x1,x26,x17
	add	x2,x27,x7
	add	x3,x26,x7
	movl	x17,30000
	add	x4,x27,x17
	mov	x5,x26
	movl	x17,30000
	add	x6,x27,x17
	bl	print_path_curve
	movl	x7,16569
	mov	x0,x19
	sub	x1,x26,x7
	movl	x17,30000
	add	x2,x27,x17
	movl	x17,30000
	sub	x3,x26,x17
	add	x4,x27,x7
	movl	x17,30000
	sub	x5,x26,x17
	mov	x6,x27
	bl	print_path_curve
	movl	x7,16569
	mov	x0,x19
	movl	x17,30000
	sub	x1,x26,x17
	sub	x2,x27,x7
	sub	x3,x26,x7
	movl	x17,30000
	sub	x4,x27,x17
	mov	x5,x26
	movl	x17,30000
	sub	x6,x27,x17
	bl	print_path_curve
	movl	x7,16569
	mov	x0,x19
	add	x1,x26,x7
	movl	x17,30000
	sub	x2,x27,x17
	movl	x17,30000
	add	x3,x26,x17
	sub	x4,x27,x7
	movl	x17,30000
	add	x5,x26,x17
	mov	x6,x27
	bl	print_path_curve
	mov	x0,x19
	bl	print_path_close
	mov	x0,x19
	movl	x1,0x4992A7
	bl	print_path_fill
	movl	x8,90000
	mov	x0,x19
	sub	x1,x26,x8
	movl	x17,30000
	add	x2,x27,x17
	bl	print_path_move
	mov	x0,x19
	movl	x17,40000
	sub	x1,x26,x17
	movl	x17,30000
	add	x2,x27,x17
	bl	print_path_line
	mov	x0,x19
	movl	x8,65000
	sub	x1,x26,x8
	movl	x17,30000
	sub	x2,x27,x17
	bl	print_path_line
	mov	x0,x19
	bl	print_path_close
	mov	x0,x19
	mov	x1,1500
	movl	x2,0xC04040
	bl	print_path_stroke
	; le bas de la page
	ldr	w25,[x20,PS_PAPER_H]
	movl	x0,PT_M+40000
	sub	x25,x25,x0
	mov	x0,x21
	mov	x1,9000
	movl	x2,PT_M
	mov	x3,x25
	adr	x4,pr_s_tp6
	movl	x5,0x606060
	bl	pr_tx
	mov	x0,x19
	bl	print_end
	mov	x19,x0
	tbz	x19,63,.ok
	adr	x0,pr_s_noqueue
	bl	pr_status
	b	.fin
.ok:	mov	x0,x28
	bl	pr_is_pdf
	adr	x1,pr_s_queued
	cbz	x0,.st
	adr	x1,pr_s_queuedpdf
.st:	mov	x0,x1
	bl	pr_status
	bl	pr_jobs_load
.fin:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

align 4
pr_titre	db 'Imprimantes',0
pr_s_printers	db 'Imprimantes',0
pr_s_empty	db 0
pr_s_default	db 'Défaut',0
pr_s_test	db 'Essai',0
pr_s_check	db 'Vérifier',0
pr_s_remove	db 'Retirer',0
pr_s_addg	db 'Ajouter une imprimante réseau',0
pr_s_addn	db 'Chercher parcourt le réseau ; ou tapez son adresse (IPP Everywhere / AirPrint : sans pilote).',0
pr_s_address	db 'Adresse',0
pr_s_name	db 'Nom',0
pr_s_add	db 'Ajouter',0
pr_s_find	db 'Chercher',0
pr_s_queue	db "File d'impression",0
pr_s_cancelj	db 'Annuler',0
pr_s_forget	db 'Effacer les finis',0
pr_t_addr	db "L'adresse IP de l'imprimante (192.168.1.30 ; sa page réseau ou son écran la montre), ou ipp://hôte:631/ipp/print",0
pr_t_name	db "Le nom montré dans le dialogue d'impression (vide : celui de l'imprimante)",0
pr_s_kpdf	db 'pdf',0
pr_s_pdfn	db 'Demande le nom du fichier à chaque impression.',0
pr_s_colour	db 'Couleur',0
pr_s_bw		db 'Noir et blanc',0
pr_s_comma	db ', ',0
pr_s_dpi	db " ppp, jusqu'à ",0
pr_s_copies	db ' exemplaires',0
pr_s_def2	db '  (par défaut)',0
pr_s_defset	db "C'est l'imprimante par défaut.",0
pr_s_pdfstay	db "L'imprimante PDF fait partie du système : elle reste.",0
pr_s_removeq	db "Retirer l'imprimante ",0
pr_s_qm		db ' ?',0
pr_s_removed	db 'Retirée.',0
pr_s_asking	db "On interroge l'imprimante...",0
pr_s_notready	db 'Pas prête : ',0
pr_s_typefirst	db "Tapez d'abord l'adresse de l'imprimante (son adresse IP, comme 192.168.1.30).",0
pr_s_asking2	db "On demande à l'imprimante ce qu'elle sait faire...",0
pr_s_added	db ' est ajoutée.',0
pr_s_notadded	db 'Pas ajoutée : ',0
pr_s_searching	db 'Recherche des imprimantes sur le réseau...',0
pr_s_notus	db '  (inutilisable)',0
pr_s_foundnone	db 'Aucune imprimante trouvée : est-elle allumée, sur le même réseau ?',0
pr_s_foundn	db "Choisissez-en une, puis Ajouter.",0
pr_s_pickjob	db "Choisissez d'abord un travail de la file.",0
pr_s_dash	db ' - ',0
pr_s_paren	db ' (',0
pr_s_parenf	db ')',0
pr_s_x		db ' x ',0
pr_s_points	db ' points.',0
pr_s_sans	db 'DejaVu Sans',0
pr_s_serif	db 'DejaVu Serif',0
pr_s_testpath	db "SD0:/home/documents/Page d'essai.pdf",0
pr_s_testtitle	db "Page d'essai de Korin OS",0
pr_s_nospool	db "La file d'impression ne peut être écrite (SD0:/var/spool/print).",0
pr_s_noqueue	db "La page d'essai n'a pu être mise en file (le service d'impression tourne-t-il ?).",0
pr_s_queued	db "La page d'essai est dans la file.",0
pr_s_queuedpdf	db "La page d'essai s'enregistre sous SD0:/home/documents/Page d'essai.pdf.",0
pr_s_tp1	db 'Imprimante : ',0
pr_s_tp2	db 'Papier : ',0
pr_s_tp3	db "Le cadre gris est le bord de ce que cette imprimante imprime.",0
pr_s_tp4	db ' pt - Portez ce vieux whisky au juge',0
pr_s_tp5	db 'Serif italique : voix ambiguë d’un cœur.',0
pr_s_tp6	db "Imprimé par le service d'impression de Korin OS (printd).",0
align 2
pr_states:
	dh	pr_s_j0-pr_states, pr_s_j1-pr_states, pr_s_j2-pr_states, pr_s_j3-pr_states
	dh	pr_s_j4-pr_states, pr_s_j5-pr_states, pr_s_j6-pr_states
pr_s_j0		db 'en attente',0
pr_s_j1		db 'préparation',0
pr_s_j2		db 'envoi',0
pr_s_j3		db 'impression',0
pr_s_j4		db 'fini',0
pr_s_j5		db 'échec',0
pr_s_j6		db 'annulé',0
pr_sizes	db 8,10,12,16,24
align 8
pr_colnames	db 'rouge',0,0,0, 'orange',0,0, 'jaune',0,0,0, 'vert',0,0,0,0
		db 'cyan',0,0,0,0, 'bleu',0,0,0,0, 'violet',0,0, 'noir',0,0,0,0
pr_lwn		db '0,25 pt',0, '0,50 pt',0, '1 pt',0,0,0,0, '2 pts',0,0,0, '4 pts',0,0,0
align 4
pr_cols		dw 0xE03030, 0xE08020, 0xE0D020, 0x30A040, 0x20A0C0, 0x3050C0, 0x8040B0, 0x000000
pr_lws		dh 250, 500, 1000, 2000, 4000
align 4

macro pr_data {
pr_w_list	dd 0
pr_w_model	dd 0
pr_w_addr	dd 0
pr_w_state	dd 0
pr_w_naddr	dd 0
pr_w_nname	dd 0
pr_w_found	dd 0
pr_w_jobs	dd 0
pr_w_status	dd 0
pr_npr		dd 0
pr_njob		dd 0
pr_nfnd		dd 0
pr_ticks	dd 0
pr_nm		rb 64
pr_nu		rb 160
pr_tb		rb 200
pr_t		rb 512
pr_ts		rb 200
pr_setup	rb PS_BYTES
align 8
pr_pr		rb PRA_NPR*PR_BYTES
pr_fnd		rb PRA_NFOUND*PF_BYTES
pr_job		rb PRA_NJOB*PJI_BYTES
pr_jobn		rb PRA_NJOB*PJI_BYTES
}
