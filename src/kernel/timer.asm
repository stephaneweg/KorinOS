; ============================================================================
;  timer.asm - timer generique ARM, compteur physique EL1 (CNTP), PPI 30
;
;  Tick periodique a HZ sur chaque coeur (chaque coeur a son timer et sa
;  ligne PPI bancalisee). On avance CNTP_CVAL d'un intervalle fixe : pas de
;  derive meme si l'IRQ est servie en retard. Le coeur 0 tient `ticks`.
; ============================================================================

HZ		= 1000
IRQ_TIMER	= 30			; CNTPNSIRQ

; Une fois, sur le coeur 0
timer_init_global:
	mrs	x0,cntfrq_el0			; 54 MHz sur Pi 4
	mov	x1,HZ
	udiv	x0,x0,x1
	adr	x1,timer_interval
	str	x0,[x1]
	ret

; Sur chaque coeur
timer_init_cpu:
	mrs	x0,cntkctl_el1			; compteur lisible en EL0 (EL0PCTEN,
	orr	x0,x0,3				; EL0VCTEN) : pilotes en services
	msr	cntkctl_el1,x0			; (I2C logiciel du GT911)
	adr	x0,timer_interval
	ldr	x0,[x0]
	mrs	x1,cntpct_el0
	add	x1,x1,x0
	msr	cntp_cval_el0,x1
	mov	x0,1				; ENABLE, IMASK=0
	msr	cntp_ctl_el0,x0
	mov	w0,IRQ_TIMER
	b	gic_enable_irq

timer_irq:
	adr	x0,timer_interval
	ldr	x0,[x0]
	mrs	x1,cntp_cval_el0
	add	x1,x1,x0
	msr	cntp_cval_el0,x1
	this_cpu x0
	ldr	x0,[x0,PC_ID]
	cbnz	x0,.local
	adr	x0,ticks
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
.local:	b	sched_tick

; -> x0 = millisecondes depuis le demarrage (compteur materiel, tout coeur)
time_ms:
	mrs	x0,cntpct_el0
	mov	x1,1000
	mul	x0,x0,x1
	mrs	x1,cntfrq_el0
	udiv	x0,x0,x1
	ret

; x0 = microsecondes (attente active)
udelay:
	mrs	x1,cntfrq_el0
	mul	x0,x0,x1
	movl	x1,1000000
	udiv	x0,x0,x1
	mrs	x9,cntpct_el0
	add	x10,x9,x0
.w:	isb
	mrs	x9,cntpct_el0
	cmp	x9,x10
	b.lo	.w
	ret

; x0 = millisecondes (attente active)
delay_ms:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	time_ms
	add	x19,x19,x0
.w:	bl	time_ms
	cmp	x0,x19
	b.lo	.w
	ldp	x19,x20,[sp],16
	epilogue
