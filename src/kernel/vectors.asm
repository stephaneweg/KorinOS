; ============================================================================
;  vectors.asm - table des vecteurs d'exception EL1 (VBAR_EL1)
;
;  16 entrees de 128 octets : {EL1 SP_EL0, EL1 SP_ELx, EL0 AArch64, EL0 AArch32}
;  x {Sync, IRQ, FIQ, SError}. Le noyau tourne en EL1h (SP_ELx).
;    4 : sync EL1   -> faute noyau (ou acces a une adresse EL0 dans un appel)
;    5 : IRQ EL1    -> irq_entry
;    8 : sync EL0   -> appel systeme (SVC), piege FP, faute de page (resolue
;                      par vmspace.asm, sinon le process est tue)
;    9 : IRQ EL0    -> irq_entry
; ============================================================================

; Trame d'exception (272 octets, alignee 16)
FR_X0		= 0		; x0..x29 par paires : FR_X0 + 8*n
FR_X8		= 64
FR_X30		= 240
FR_SP_EL0	= 248
FR_ELR		= 256
FR_SPSR		= 264
FRAME_SIZE	= 272

EC_SVC64	= 0x15
EC_FP		= 0x07
EC_DABT_EL1	= 0x25
EC_DABT_EL0	= 0x24
EC_IABT_EL0	= 0x20

macro save_frame {
	sub	sp,sp,FRAME_SIZE
	stp	x0,x1,[sp,16*0]
	stp	x2,x3,[sp,16*1]
	stp	x4,x5,[sp,16*2]
	stp	x6,x7,[sp,16*3]
	stp	x8,x9,[sp,16*4]
	stp	x10,x11,[sp,16*5]
	stp	x12,x13,[sp,16*6]
	stp	x14,x15,[sp,16*7]
	stp	x16,x17,[sp,16*8]
	stp	x18,x19,[sp,16*9]
	stp	x20,x21,[sp,16*10]
	stp	x22,x23,[sp,16*11]
	stp	x24,x25,[sp,16*12]
	stp	x26,x27,[sp,16*13]
	stp	x28,x29,[sp,16*14]
	mrs	x21,sp_el0
	stp	x30,x21,[sp,FR_X30]
	mrs	x21,elr_el1
	mrs	x22,spsr_el1
	stp	x21,x22,[sp,FR_ELR]
}

		align 2048
vector_table:
rept 16 n:0 {
		align 128
  if n = 4
	b	el1_sync_entry
  else if n = 5 | n = 9
	b	irq_entry
  else if n = 8
	b	el0_sync_entry
  else
	mov	x0,n
	b	exc_fatal
  end if
}

irq_entry:
	save_frame
	bl	rng_irq				; alea : instant, adresse interrompue
	this_cpu x0				; (diagnostic : derniere adresse
	str	x21,[x0,PC_LAST_ELR]		;  interrompue, et en EL0)
	tst	x22,0xF
	b.ne	.k
	str	x21,[x0,PC_LAST_EL0]
.k:
	mov	x0,sp
	bl	irq_dispatch
	b	exc_return

; Sortie commune : preemption eventuelle, puis restauration de la trame
exc_return:
	this_cpu x0
	ldr	x1,[x0,PC_NEED_RESCHED]
	cbz	x1,.kill
	str	xzr,[x0,PC_NEED_RESCHED]
	bl	schedule
.kill:	ldr	x1,[sp,FR_SPSR]			; retour en EL0 d'un thread tue par
	tst	x1,0xF				; thread_kill (threads.asm) : il
	b.ne	.restore			; meurt ici, hors de tout appel
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x1,[x0,TH_KILL]
	cbz	x1,.stop
	ldr	x1,[x0,TH_KILL_CODE]
	bl	thread_record_exit
	b	thread_exit
.stop:	ldr	x1,[x0,TH_PROC]			; process arrete (jobs.asm) : attendre
	ldr	x1,[x1,P_STOP]			; la reprise ici, avant le programme
	cbz	x1,.sig
	bl	sig_stopped
	b	exc_return
.sig:	ldr	x1,[x0,TH_SIGPEND]		; un signal a livrer (signal.asm)
	cbz	x1,.restore
	ldr	x2,[x0,TH_SIGMASK]
	bic	x1,x1,x2
	cbz	x1,.restore
	msr	daifclr,2
	mov	x0,sp
	bl	sig_deliver
	msr	daifset,2
	b	exc_return
.restore:
	ldp	x21,x22,[sp,FR_ELR]
	msr	elr_el1,x21
	msr	spsr_el1,x22
	ldp	x30,x21,[sp,FR_X30]
	msr	sp_el0,x21
	ldp	x28,x29,[sp,16*14]
	ldp	x26,x27,[sp,16*13]
	ldp	x24,x25,[sp,16*12]
	ldp	x22,x23,[sp,16*11]
	ldp	x20,x21,[sp,16*10]
	ldp	x18,x19,[sp,16*9]
	ldp	x16,x17,[sp,16*8]
	ldp	x14,x15,[sp,16*7]
	ldp	x12,x13,[sp,16*6]
	ldp	x10,x11,[sp,16*5]
	ldp	x8,x9,[sp,16*4]
	ldp	x6,x7,[sp,16*3]
	ldp	x4,x5,[sp,16*2]
	ldp	x2,x3,[sp,16*1]
	ldp	x0,x1,[sp,16*0]
	add	sp,sp,FRAME_SIZE
	eret

; --- exceptions synchrones venant d'EL0 -------------------------------------
el0_sync_entry:
	save_frame
	mrs	x19,esr_el1
	lsr	x20,x19,26			; classe d'exception
	cmp	x20,EC_SVC64
	b.eq	.svc
	cmp	x20,EC_FP
	b.eq	.fp
	cmp	x20,EC_DABT_EL0			; faute de page : region paresseuse,
	b.eq	.pf				; copie sur ecriture (vmspace.asm)
	cmp	x20,EC_IABT_EL0
	b.ne	.bad
.pf:	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x0,[x0,TH_PROC]
	mrs	x1,far_el1
	mov	x2,x19
	mov	x3,1
	bl	vm_fault_esr
	cbnz	x0,.resolved
.bad:	mov	x0,x19
	bl	sig_fault			; un gestionnaire ? (signal.asm)
	cbnz	x0,.resolved
	mov	x0,x19
	bl	user_fault			; ne revient pas
.resolved:
	b	exc_return
.svc:	msr	daifclr,2			; appel systeme preemptible
	mov	x0,sp
	bl	syscall_dispatch
	msr	daifset,2
	b	exc_return
.fp:	bl	fp_first_use
	b	exc_return

; Premier usage FP/SIMD par un thread EL0 : registres mis a zero (pas de fuite
; d'un process a l'autre), drapeau TF_FP, FPU deverrouille pour EL0.
fp_first_use:
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x1,[x0,TH_FLAGS]
	orr	x1,x1,1 shl TF_FP
	str	x1,[x0,TH_FLAGS]
	movi	v0.16b,0
	movi	v1.16b,0
	movi	v2.16b,0
	movi	v3.16b,0
	movi	v4.16b,0
	movi	v5.16b,0
	movi	v6.16b,0
	movi	v7.16b,0
	movi	v8.16b,0
	movi	v9.16b,0
	movi	v10.16b,0
	movi	v11.16b,0
	movi	v12.16b,0
	movi	v13.16b,0
	movi	v14.16b,0
	movi	v15.16b,0
	movi	v16.16b,0
	movi	v17.16b,0
	movi	v18.16b,0
	movi	v19.16b,0
	movi	v20.16b,0
	movi	v21.16b,0
	movi	v22.16b,0
	movi	v23.16b,0
	movi	v24.16b,0
	movi	v25.16b,0
	movi	v26.16b,0
	movi	v27.16b,0
	movi	v28.16b,0
	movi	v29.16b,0
	movi	v30.16b,0
	movi	v31.16b,0
	msr	fpcr,xzr
	msr	fpsr,xzr
	movl	x0,CPACR_FP_ON
	msr	cpacr_el1,x0
	isb
	ret

; x0 = ESR : faute d'un process -> message, process tue
user_fault:
	mov	x19,x0
	this_cpu x0
	ldr	x20,[x0,PC_CURRENT]
	ldr	x21,[x20,TH_PROC]
	adr	x0,msg_ufault
	bl	kputs
	ldr	x0,[x21,P_PID]
	bl	kput_dec
	mov	w0,' '
	bl	kputc
	add	x0,x21,P_NAME
	bl	kputs
	adr	x0,msg_ufault2
	bl	kputs
	mov	x0,x19
	bl	kput_hex
	adr	x0,msg_elr
	bl	kputs
	mrs	x0,elr_el1
	bl	kput_hex
	adr	x0,msg_far
	bl	kputs
	mrs	x0,far_el1
	bl	kput_hex
	mov	w0,10
	bl	kputc
	ldr	x0,[x21,P_VM_OOM]		; faute par manque de memoire ?
	cbz	x0,.k
	adr	x0,msg_ufault_oom
	bl	kputs
.k:	mov	x0,x21
	mov	x1,-11				; "SIGSEGV"
	bl	proc_kill
	b	thread_exit

; --- exceptions synchrones en EL1 -------------------------------------------
el1_sync_entry:
	save_frame
	mrs	x19,esr_el1
	lsr	x20,x19,26
	cmp	x20,EC_DABT_EL1			; acces noyau a une adresse EL0 invalide
	b.ne	.fatal				; pendant un appel systeme : on tue le
	mrs	x0,far_el1			; process, pas le noyau
	movl	x1,USER_BASE
	cmp	x0,x1
	b.lo	.fatal
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x1,[x0,TH_FLAGS]
	tbz	x1,TF_USER,.fatal
	ldr	x0,[x0,TH_PROC]			; page pas encore peuplee, copie sur
	mrs	x1,far_el1			; ecriture : resolue, l'acces reprend
	mov	x2,x19
	mov	x3,0
	bl	vm_fault_esr
	cbz	x0,.kill
	b	exc_return.restore
.kill:	msr	daifclr,2
	mov	x0,x19
	bl	user_fault
.fatal:	mov	x0,4
	b	exc_fatal

; x0 = numero d'entree
exc_fatal:
	msr	daifset,0xF
	mov	x19,x0
	adr	x0,con_ready			; diagnostic visible meme sous la GUI
	mov	x1,1
	str	x1,[x0]
	adr	x0,con_lock			; ne pas rester bloque sur un verrou tenu
	str	wzr,[x0]
	adr	x0,msg_exc
	bl	kputs
	mov	x0,x19
	bl	kput_hex
	adr	x0,msg_esr
	bl	kputs
	mrs	x0,esr_el1
	bl	kput_hex
	adr	x0,msg_elr
	bl	kputs
	mrs	x0,elr_el1
	bl	kput_hex
	adr	x0,msg_far
	bl	kputs
	mrs	x0,far_el1
	bl	kput_hex
	mov	w0,10
	bl	kputc
.halt:	wfe
	b	.halt

msg_exc		db 10,'*** EXCEPTION NOYAU vecteur=',0
msg_esr		db 10,'    ESR=',0
msg_elr		db '  ELR=',0
msg_far		db '  FAR=',0
msg_ufault	db '[pid ',0
msg_ufault2	db '] faute, process tu',0xC3,0xA9,'. ESR=',0
msg_ufault_oom	db '    (plus de m',0xC3,0xA9,'moire pour une page)',10,0
		align 4
