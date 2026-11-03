; ============================================================================
;  diag.asm - diagnostic (diag_dump ; la touche F12 du clavier du noyau, qui
;  le declenchait, est partie avec l'USB du noyau) : ecrit dans le journal
;  du noyau (liaison serie, klog) l'etat des verrous et de chaque thread de
;  chaque coeur : nom, etat, pile d'appels du noyau (adresses de retour, par
;  les chainages x29) ; avec les adresses de quelques fonctions pour s'y
;  retrouver. Sert a trouver qui bloque quoi.
; ============================================================================

DIAG_DEPTH	= 8

diag_dump:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,dg_titre
	bl	kputs
	adr	x19,dg_verrous			; verrous : nom, valeur
.v:	ldr	x0,[x19]
	cbz	x0,.fn
	add	x0,x19,x0
	bl	kputs
	ldr	x0,[x19,8]
	add	x0,x19,x0
	ldr	w0,[x0]
	bl	kput_hex
	mov	w0,10
	bl	kputc
	add	x19,x19,16
	b	.v
.fn:	adr	x0,dg_temps			; heure du noyau (ms)
	bl	kputs
	bl	time_ms
	bl	kput_dec
	mov	w0,10
	bl	kputc
	adr	x0,dg_fb			; framebuffer (adresse, pas)
	bl	kputs
	adr	x0,fb_base
	ldr	x0,[x0]
	bl	kput_hex
	mov	w0,' '
	bl	kputc
	adr	x0,fb_pitch
	ldr	x0,[x0]
	bl	kput_dec
	mov	w0,10
	bl	kputc
	adr	x0,dg_mbox			; firmware : echanges sans reponse
	bl	kputs
	adr	x0,mbox_muet
	ldr	x0,[x0]
	bl	kput_dec
	mov	w0,10
	bl	kputc
	adr	x19,svc_requetes		; requetes aux services (svc.asm)
	mov	x20,0
.q:	cmp	x20,SR_MAX
	b.hs	.fn2
	ldr	x0,[x19,SR_ETAT]
	cbz	x0,.qn
	adr	x0,dg_req
	bl	kputs
	mov	x0,x20
	bl	kput_dec
	adr	x0,dg_qetat
	bl	kputs
	ldr	x0,[x19,SR_ETAT]
	bl	kput_dec
	adr	x0,dg_qsvc
	bl	kputs
	ldr	x0,[x19,SR_SVC]
	bl	svc_entree
	bl	kputs
	adr	x0,dg_qpid
	bl	kputs
	ldr	x0,[x19,SR_PID]
	bl	kput_dec
	adr	x0,dg_qop
	bl	kputs
	ldr	x0,[x19,SR_MSG]
	bl	kput_dec
	mov	w0,10
	bl	kputc
.qn:	add	x19,x19,SR_SIZE
	add	x20,x20,1
	b	.q
.fn2:	adr	x19,dg_fonctions		; reperes : adresses de fonctions
.f:	ldr	x0,[x19]
	cbz	x0,.cpus
	add	x0,x19,x0
	bl	kputs
	ldr	x0,[x19,8]
	add	x0,x19,x0
	bl	kput_hex
	mov	w0,10
	bl	kputc
	add	x19,x19,16
	b	.f
.cpus:	mov	x19,0				; threads, coeur par coeur
.c:	cmp	x19,MAX_CPUS
	b.hs	.out
	mov	x0,x19
	bl	percpu_of
	ldr	x20,[x0,PC_RQ_HEAD]
	ldr	x24,[x0,PC_CURRENT]
	str	x0,[sp,-16]!			; dernieres adresses interrompues
	adr	x0,dg_elr
	bl	kputs
	mov	x0,x19
	bl	kput_dec
	mov	w0,' '
	bl	kputc
	ldr	x0,[sp]
	ldr	x0,[x0,PC_LAST_ELR]
	bl	kput_hex
	mov	w0,' '
	bl	kputc
	ldr	x0,[sp],16
	ldr	x0,[x0,PC_LAST_EL0]
	bl	kput_hex
	mov	w0,10
	bl	kputc
	cbz	x20,.cn
	mov	x21,x20
.t:	adr	x0,dg_th
	bl	kputs
	mov	x0,x19
	bl	kput_dec
	mov	w0,' '
	bl	kputc
	add	x0,x21,TH_NAME
	bl	kputs
	adr	x0,dg_etat
	bl	kputs
	ldr	x0,[x21,TH_STATE]
	bl	kput_dec
	adr	x0,dg_prio
	bl	kputs
	ldr	x0,[x21,TH_PRIO]
	bl	kput_dec
	cmp	x21,x24
	b.ne	.st
	adr	x0,dg_courant			; en cours : pas de contexte sauve
	bl	kputs
	b	.nx
.st:	adr	x0,dg_pile
	bl	kputs
	ldr	x0,[x21,TH_CTX+88]		; x30 sauve
	bl	kput_hex
	ldr	x22,[x21,TH_CTX+80]		; x29 sauve : chainage des cadres
	mov	x23,DIAG_DEPTH
.fr:	cbz	x22,.nx
	cbz	x23,.nx
	and	x0,x22,0xF
	cbnz	x0,.nx
	movl	x0,0x40000000			; (pile du noyau : memoire identite)
	cmp	x22,x0
	b.hs	.nx
	mov	w0,' '
	bl	kputc
	ldr	x0,[x22,8]
	bl	kput_hex
	ldr	x22,[x22]
	sub	x23,x23,1
	b	.fr
.nx:	mov	w0,10
	bl	kputc
	ldr	x21,[x21,TH_NEXT]
	cmp	x21,x20
	b.ne	.t
.cn:	add	x19,x19,1
	b	.c
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

dg_titre	db 10,'=== diagnostic (F12) ===',10,0
dg_th		db 'coeur ',0
dg_etat		db ' etat ',0
dg_prio		db ' prio ',0
dg_courant	db ' (en cours)',0
dg_pile		db ' pile ',0
dg_elr		db 'interrompu (noyau, EL0) coeur ',0
dg_temps	db 'temps (ms) ',0
dg_fb		db 'framebuffer ',0
dg_req		db 'requete ',0
dg_qetat	db ' etat ',0
dg_mbox		db 'Mailbox sans reponse : ',0
dg_qsvc		db ' service ',0
dg_qpid		db ' pid ',0
dg_qop		db ' op ',0
		align 8
; entrees : decalage du nom, decalage de la cible (depuis l'entree)
dg_verrous:
	dd dg_n_kits - $, kits_mutex - $ + 8
	dd dg_n_ws - $, ws_lock - $ + 8
	dd dg_n_buf - $, buf_mutex - $ + 8
	dd 0, 0
dg_fonctions:
	dd dg_f1 - $, mutex_lock - $ + 8
	dd dg_f2 - $, thread_yield - $ + 8
	dd dg_f3 - $, thread_sleep - $ + 8
	dd dg_f4 - $, sys_ws_call - $ + 8
	dd dg_f5 - $, sys_ev_wait - $ + 8
	dd dg_f6 - $, sys_ws_next - $ + 8
	dd dg_f7 - $, sys_spawn_image - $ + 8
	dd dg_f8 - $, sys_svc_call - $ + 8
	dd dg_f9 - $, sys_kit_register - $ + 8
	dd dg_f10 - $, sys_lib_open - $ + 8
	dd 0, 0
dg_n_kits	db 'kits_mutex ',0
dg_n_ws		db 'ws_lock    ',0
dg_n_buf	db 'buf_mutex  ',0
dg_f1		db 'mutex_lock   ',0
dg_f2		db 'thread_yield ',0
dg_f3		db 'thread_sleep ',0
dg_f4		db 'sys_ws_call  ',0
dg_f5		db 'sys_ev_wait  ',0
dg_f6		db 'sys_ws_next  ',0
dg_f7		db 'spawn_image  ',0
dg_f8		db 'svc_call     ',0
dg_f9		db 'kit_register ',0
dg_f10		db 'lib_open     ',0
		align 4
