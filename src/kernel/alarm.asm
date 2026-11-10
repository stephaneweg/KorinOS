; ============================================================================
;  alarm.asm - la minuterie des process (SYS_SETITIMER, 0x6F, docs/ABI.md) :
;  ITIMER_REAL, SIGALRM a echeance (alarm, setitimer de libposix).
;
;  Par process (PCB) : P_ALARM, l'echeance (CNTPCT ; 0 : aucune), et
;  P_ALARM_IV, l'intervalle (CNTPCT ; 0 : une seule fois). Le thread noyau
;  « alarmes » dort jusqu'a la plus proche echeance (ALARM_SOMMEIL ms au
;  plus), parcourt la table des process (sans verrou, comme sig_send pour un
;  groupe), et pour une echeance passee : rearme ou efface (echange atomique :
;  un SETITIMER entre-temps gagne), puis SIGALRM par sys_sig_send (le process
;  retrouve par son pid). SETITIMER le reveille (TH_WAKE = 0, comme le
;  faucheur).
;  fork : le fils n'a pas de minuterie (PCB neuf) ; exec : elle reste (meme
;  PCB), comme le veut POSIX.
; ============================================================================

SIGALRM		= 14
ALARM_SOMMEIL	= 100			; ms au plus entre deux passages

; x0 = microsecondes -> x0 = periodes du compteur (sans debordement)
us_ticks:
	mrs	x1,cntfrq_el0
	movl	x2,1000000
	udiv	x3,x0,x2			; secondes
	msub	x4,x3,x2,x0			; reste en us
	mul	x3,x3,x1
	mul	x4,x4,x1
	udiv	x4,x4,x2
	add	x0,x3,x4
	ret

; x0 = periodes du compteur -> x0 = microsecondes
ticks_us:
	mrs	x1,cntfrq_el0
	movl	x2,1000000
	udiv	x3,x0,x1			; secondes
	msub	x4,x3,x1,x0			; reste en periodes
	mul	x3,x3,x2
	mul	x4,x4,x2
	udiv	x4,x4,x1
	add	x0,x3,x4
	ret

; --- setitimer(quelle (0 : ITIMER_REAL), valeur us (0 : arreter ; -1 : lire
; seulement), intervalle us, ancienne (16 o {valeur us, intervalle us} ; 0 :
; aucune)) -> 0 ; E_INVAL (autre minuterie), E_FAULT
sys_setitimer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	cbnz	x0,.inval
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	tbnz	x20,63,.inval
	cbz	x21,.lu
	check_user x21,16,.fault
.lu:	current_proc
	mov	x22,x1
	cbz	x22,.inval
	mov	x25,0				; nouvelle echeance, intervalle
	mov	x26,0
	cmn	x19,1
	b.eq	.calc
	cbz	x19,.calc
	tbnz	x19,63,.inval
	mov	x0,x20
	bl	us_ticks
	mov	x26,x0
	mov	x0,x19
	bl	us_ticks
	cmp	x0,0
	csinc	x0,x0,xzr,ne			; (au moins une periode)
	mrs	x1,cntpct_el0
	add	x25,x1,x0
.calc:	add	x0,x22,P_LOCK
	bl	spin_lock_irqsave
	mov	x24,x0
	mrs	x23,cntpct_el0
	ldr	x2,[x22,P_ALARM]		; l'ancienne : ce qui reste
	ldr	x3,[x22,P_ALARM_IV]
	mov	x4,0
	cbz	x2,.vieille
	subs	x4,x2,x23
	b.hi	.vieille
	mov	x4,1				; (echue, pas encore envoyee)
.vieille:
	cmn	x19,1
	b.eq	.garde
	str	x26,[x22,P_ALARM_IV]
	add	x0,x22,P_ALARM
	stlr	x25,[x0]
.garde:	mov	x19,x4
	mov	x20,x3
	add	x0,x22,P_LOCK
	mov	x1,x24
	bl	spin_unlock_irqrestore
	cbz	x25,.rendu
	adr	x0,alarm_tcb			; le thread des alarmes : a revoir
	ldr	x0,[x0]
	cbz	x0,.rendu
	str	xzr,[x0,TH_WAKE]
	ldr	x0,[x0,TH_CPU]
	bl	cpu_kick
.rendu:	cbz	x21,.ok
	mov	x0,x19
	bl	ticks_us
	str	x0,[x21]
	mov	x0,x20
	bl	ticks_us
	str	x0,[x21,8]
.ok:	mov	x0,0
	b	.out
.fault:	mov	x0,E_FAULT
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Thread noyau « alarmes »
alarm_thread:
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	adr	x1,alarm_tcb
	str	x0,[x1]
.loop:	bl	alarm_passe
	bl	thread_sleep
	b	.loop

; un passage : les echeances passees sonnent -> x0 = ms a dormir
alarm_passe:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,-1				; la plus proche echeance
	mov	x19,0				; emplacement
.s:	adr	x0,proc_cap
	ldar	x0,[x0]
	cmp	x19,x0
	b.hs	.fin
	adr	x0,proc_tab
	ldr	x0,[x0]
	lsl	x1,x19,5
	ldr	x20,[x0,x1]			; (PT_PCB, lu sans verrou)
	add	x19,x19,1
	cbz	x20,.s
	ldr	x2,[x20,P_ALARM]
	cbz	x2,.s
	mrs	x3,cntpct_el0
	cmp	x2,x3
	b.hi	.futur
	ldr	x0,[x20,P_PID]
	bl	alarm_sonner
	cbz	x0,.s
	mov	x2,x0
.futur:	cmp	x2,x21
	csel	x21,x2,x21,lo
	b	.s
.fin:	mov	x0,ALARM_SOMMEIL
	cmn	x21,1
	b.eq	.out
	mrs	x1,cntpct_el0
	subs	x0,x21,x1
	csel	x0,xzr,x0,lo
	mov	x1,1000
	mul	x0,x0,x1
	mrs	x1,cntfrq_el0
	udiv	x0,x0,x1
	add	x0,x0,1
	cmp	x0,ALARM_SOMMEIL
	mov	x1,ALARM_SOMMEIL
	csel	x0,x1,x0,hi
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid dont l'echeance est passee : rearmee (intervalle) ou effacee, puis
; SIGALRM -> x0 = sa nouvelle echeance (0 : aucune)
alarm_sonner:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,0				; 1 : a envoyer
	mov	x22,0				; nouvelle echeance
	adr	x0,proc_lock			; (le PCB tient sous ce verrou)
	bl	spin_lock_irqsave
	mov	x20,x0
	mov	x0,x19
	bl	proc_lookup
	cbz	x0,.rendre
	ldr	x1,[x0,P_STATE]
	cbnz	x1,.rendre
	add	x5,x0,P_ALARM
	ldr	x4,[x0,P_ALARM_IV]
.cas:	ldaxr	x2,[x5]
	cbz	x2,.libre
	mrs	x3,cntpct_el0
	cmp	x2,x3
	b.hi	.pas_due
	mov	x6,0
	cbz	x4,.pose
	add	x6,x2,x4			; la suivante (en retard : depuis
	cmp	x6,x3				; maintenant)
	b.hi	.pose
	add	x6,x3,x4
.pose:	stlxr	w7,x6,[x5]
	cbnz	w7,.cas
	mov	x22,x6
	mov	x21,1
	b	.rendre
.pas_due:
	clrex
	mov	x22,x2
	b	.rendre
.libre:	clrex
.rendre:
	adr	x0,proc_lock
	mov	x1,x20
	bl	spin_unlock_irqrestore
	cbz	x21,.out
	mov	x0,x19
	mov	x1,0
	mov	x2,SIGALRM
	bl	sys_sig_send
.out:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

		align 8
alarm_tcb	dd 0,0				; le thread des alarmes
