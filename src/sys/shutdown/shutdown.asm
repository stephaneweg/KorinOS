; shutdown - le dialogue de fin de session (SD0:/sys/shutdown, lance par le
; bouton d'alimentation du dock ; comme le shutdown d'Onyx) : Redemarrer,
; Eteindre, Annuler (ou Echap, ou la case de fermeture). Les deux premiers
; demandent a init2 (INIT_OP_REBOOT), qui laisse finir les ecritures. Deja
; ouvert : sa fenetre revient devant.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

W		= 500
H		= 150
BTN_W		= 104
BTN_H		= 30
BTN_PAS		= 112

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	bl	deja_ouvert
	cbnz	x0,quit
	adr	x0,scr				; au milieu de l'ecran, un peu plus haut
	bl	ui_screen
	mov	x0,-1
	mov	x1,-1
	adr	x2,scr
	ldp	x2,x3,[x2]
	cbz	x2,.win
	sub	x0,x2,W
	asr	x0,x0,1
	sub	x1,x3,H
	asr	x1,x1,1
	sub	x1,x1,40
.win:	mov	x2,W
	mov	x3,H
	adr	x4,s_titre
	bl	tr_x4
	mov	x5,WF_MODAL or WF_NOLIST	; un dialogue (en mode poche : centre,
	bl	ui_window_ex			; sur un voile), hors des listes
	tbnz	x0,63,quit
	mov	x0,18				; la question
	mov	x1,12
	mov	x2,W-36
	mov	x3,20
	adr	x4,s_question
	mov	x5,ULB_BOLD
	bl	ui_label
	mov	x0,18
	mov	x1,38
	mov	x2,W-36
	mov	x3,20
	adr	x4,s_note
	mov	x5,ULB_DIM
	bl	ui_label
	adr	x1,w_note
	str	x0,[x1]
	mov	x0,W-3*BTN_PAS
	mov	x1,H-44
	mov	x2,BTN_W
	mov	x3,BTN_H
	adr	x4,s_redemarrer
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_redemarrer
	str	x0,[x1]
	mov	x0,W-2*BTN_PAS
	mov	x1,H-44
	mov	x2,BTN_W
	mov	x3,BTN_H
	adr	x4,s_eteindre
	mov	x5,UB_DEFAULT
	bl	ui_pushbutton
	adr	x1,w_eteindre
	str	x0,[x1]
	mov	x0,W-1*BTN_PAS
	mov	x1,H-44
	mov	x2,BTN_W
	mov	x3,BTN_H
	adr	x4,s_annuler
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_annuler
	str	x0,[x1]
	bl	draw
.loop:	mov	x0,1000
	bl	ui_event
	cbz	x0,.idle
	cmp	x0,WEV_CLOSE
	b.eq	quit
	mov	x19,x0
	mov	x0,0
	bl	ui_w_event
	cmp	x1,UN_ACTIVATE
	b.eq	.btn
	cbnz	x2,.loop
	cmp	x19,WEV_KEY			; Echap
	b.ne	.loop
	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	cbz	w0,.loop
	ldr	w0,[x9,EV_CODE]
	cmp	w0,KEY_ESC
	b.eq	quit
	b	.loop
.idle:	bl	ui_w_idle
	b	.loop
.btn:	adr	x1,w_annuler
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	quit
	adr	x1,w_redemarrer
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.reboot
	adr	x1,w_eteindre
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.loop
	mov	x0,INIT_REBOOT_HALT
	adr	x1,s_extinction
	bl	tr_x1
	bl	finir
	b	.loop
.reboot:
	mov	x0,0
	adr	x1,s_redemarrage
	bl	tr_x1
	bl	finir
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; la fenetre : le fond, le trait au-dessus des boutons, les widgets
draw:
	prologue
	bl	ui_jetons
	str	x0,[sp,-16]!
	mov	x0,0
	mov	x1,0
	mov	x2,W
	mov	x3,H
	ldr	x4,[sp]
	ldr	w4,[x4,TJ_FACE]
	bl	ui_fill
	ldr	x4,[sp],16
	ldr	w4,[x4,TJ_LIGNE]
	mov	x0,12
	mov	x1,H-56
	mov	x2,W-24
	mov	x3,1
	bl	ui_fill
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	epilogue

; x0 = drapeaux de INIT_OP_REBOOT, x1 = texte : le dit, le demande a init2 ;
; ne revient que si init2 n'a rien fait (le texte dit alors l'echec)
finir:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,w_note
	ldr	x0,[x0]
	bl	ui_w_set_text
	bl	draw
	mov	x0,400				; (le temps de le voir)
	sys	SYS_SLEEP
	adr	x0,s_init2
	mov	x1,1000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.ko
	adr	x1,msg
	mov	x2,INIT_OP_REBOOT
	stp	x2,x19,[x1]
	str	wzr,[x1,WM_LEN]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.ko
	mov	x0,5000				; (le Pi s'arrete pendant ce temps)
	sys	SYS_SLEEP
.ko:	adr	x0,w_note
	ldr	x0,[x0]
	adr	x1,s_echec
	bl	tr_x1
	bl	ui_w_set_text
	bl	draw
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 si un autre shutdown tourne (sa fenetre vient devant)
deja_ouvert:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sys	SYS_GETPID
	mov	x19,x0
	mov	x21,0				; un autre process de ce nom
.p:	adr	x0,s_nom			; (procfind.inc)
	mov	x1,x21
	bl	proc_find
	cbz	x0,.non
	mov	x21,x0
	cmp	x0,x19
	b.eq	.p
	mov	x22,x0
.autre:	mov	x19,0				; ses fenetres : la premiere
.page:	mov	x0,x19
	adr	x1,wlist
	bl	ui_winlist
	cmp	x0,x19
	b.le	.oui
	sub	x20,x0,x19
	cmp	x20,WL_PER_MSG
	mov	x1,WL_PER_MSG
	csel	x20,x1,x20,gt
	adr	x2,wlist
.e:	cbz	x20,.np
	ldr	w3,[x2,WL_PID]
	cmp	x3,x22
	b.eq	.raise
	add	x2,x2,WL_SIZE
	sub	x20,x20,1
	b	.e
.np:	add	x19,x19,WL_PER_MSG
	b	.page
.raise:	ldr	x0,[x2,WL_ID]
	bl	ui_raise
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_nom		db 'shutdown',0
s_init2		db 'init2',0
s_titre		db 0xC3,0x89,'teindre',0
s_question	db 'Voulez-vous ',0xC3,0xA9,'teindre Korin OS ?',0
s_note		db 'Les documents ouverts ne sont pas enregistr',0xC3,0xA9,'s automatiquement.',0
s_redemarrer	db 'Red',0xC3,0xA9,'marrer',0
s_eteindre	db 0xC3,0x89,'teindre',0
s_annuler	db 'Annuler',0
s_redemarrage	db 'Red',0xC3,0xA9,'marrage...',0
s_extinction	db 'Extinction...',0
s_echec		db 'init2 ne r',0xC3,0xA9,'pond pas : le Pi tourne toujours.',0
		align 4

include '../../inc/tr.inc'

		align 4
include '../../inc/procfind.inc'		; proc_find

segment readable writeable
kit_data
ui_data
		align 8
scr		rb 32
w_note		dd 0
w_redemarrer	dd 0
w_eteindre	dd 0
w_annuler	dd 0
wlist		rb WL_PER_MSG * WL_SIZE
msg		rb 512
