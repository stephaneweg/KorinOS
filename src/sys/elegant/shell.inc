; ============================================================================
;  elegant/shell.inc - le bus de la coquille (busd, docs/BUS.md ; contrat :
;  docs/MODE-POCHE.md). Elegant parle a busd directement (src/svc/inc/
;  buspub.inc, sans BusKit) :
;	korin.shell.mode	(garde) « desktop » ou « poche », publie des que
;				busd repond (Elegant demarre avant lui)
;	korin.shell.home,	publies par les touches systeme du mode poche ;
;	korin.shell.switch,	korin.shell.home est aussi suivi (le losange de la
;	korin.shell.quick,	barre de menus, le glyphe ‒ du cadre) : l'evenement
;	korin.shell.sleep	revient dans la file d'evenements d'Elegant (la
;				meme que celle d'un programme), lue a chaque tour
;  Un mode donne a sh_tick sa fonction pour les evenements (x0 = evenement de
;  32 octets, WEV_BUS ; 0 : aucune).
; ============================================================================

SH_RETRY	= 500			; ms entre deux essais d'ouvrir busd

; x0 = fonction des evenements du bus (0 : aucune) : a chaque tour
sh_tick:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,bp_canal
	ldr	x0,[x0]
	cbnz	x0,.ev
	sys	SYS_TIME_MS			; busd pas encore la : un essai de temps
	adr	x1,sh_try			; en temps
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.out
	add	x0,x0,SH_RETRY
	str	x0,[x1]
	mov	x0,0
	bl	bp_open
	cbz	x0,.out
	adr	x0,sh_topic_mode		; le mode, garde
	adr	x1,policy
	ldr	x1,[x1]
	ldr	x1,[x1,POL_NAME]
	mov	x2,BUS_RETAIN
	bl	bp_publish
	adr	x0,policy			; les sujets suivis par le mode
	ldr	x0,[x0]
	ldr	x0,[x0,POL_FLAGS]
	tbz	x0,0,.ev			; (POLF_POCHE)
	adr	x0,sh_topic_home
	mov	x1,0
	bl	sh_subscribe
	adr	x1,sh_sub_home
	str	x0,[x1]
	adr	x0,sh_topic_theme
	mov	x1,BUS_CURRENT
	bl	sh_subscribe
	adr	x1,sh_sub_theme
	str	x0,[x1]
.ev:	adr	x0,sh_ev			; les evenements en attente
	mov	x1,0
	sys	SYS_EV_WAIT
	cbz	x0,.out
	tbnz	x0,63,.out
	cmp	x0,WEV_BUS
	b.ne	.ev
	cbz	x19,.ev
	adr	x0,sh_ev
	blr	x19
	b	.ev
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = sujet, x1 = valeur (chaine) : publie, non gardee -> x0 = 1 si busd
; l'a pris (0 : pas de bus, a faire sans lui)
sh_publish:
	prologue
	mov	x2,0
	bl	bp_publish
	cmp	x0,0
	cset	x0,ge
	epilogue

; x0 = motif, x1 = drapeaux (BUS_CURRENT) -> x0 = abonnement (<= 0 : aucun)
sh_subscribe:
	prologue
	sub	sp,sp,512
	mov	x3,sp
	mov	x4,BUS_OP_SUBSCRIBE
	stp	x4,x1,[x3]			; op, a = drapeaux
	add	x5,x3,BUS_DATA
	mov	x6,BUS_NAME_MAX-1
.n:	ldrb	w4,[x0],1
	strb	w4,[x5],1
	cbz	w4,.call
	subs	x6,x6,1
	b.ne	.n
	strb	wzr,[x5]
.call:	adr	x0,bp_canal
	ldr	x0,[x0]
	cbz	x0,.none
	sub	x0,x0,1
	mov	x1,x3
	sys	SYS_SVC_CALL
	b	.out
.none:	mov	x0,0
.out:	add	sp,sp,512
	epilogue

include '../../svc/inc/buspub.inc'

sh_topic_mode	db 'korin.shell.mode',0
sh_topic_home	db 'korin.shell.home',0
sh_topic_switch	db 'korin.shell.switch',0
sh_topic_quick	db 'korin.shell.quick',0
sh_topic_sleep	db 'korin.shell.sleep',0
sh_topic_theme	db 'korin.theme',0
sh_val_1	db '1',0
		align 4

macro shell_data {
		align 8
bp_canal	dd 0				; canal de busd + 1 (buspub.inc)
sh_try		dd 0				; prochain essai d'ouverture (ms)
sh_sub_home	dd 0				; abonnements
sh_sub_theme	dd 0
sh_ev		rb 32
}
