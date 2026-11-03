; ============================================================================
;  sched.asm - threads et ordonnanceur preemptif SMP
;
;  * Chaque coeur a sa structure per-CPU (TPIDR_EL1) et sa liste circulaire
;    de threads, protegee par un verrou. Tourniquet avec quantum SLICE_TICKS.
;  * Un thread = bloc de 16 Ko : TCB en bas, pile noyau en haut.
;  * Changement de contexte facon Linux : l'IRQ sauve la trame complete sur
;    la pile du thread, puis switch_to ne sauve que x19-x30/sp (+ etat FP).
;  * FP/NEON : sauvegarde complete q0-q31/FPCR/FPSR des threads qui ont
;    utilise le FPU (drapeau TF_FP, positionne au premier piege EL0). Rien
;    ne reste "en vol" dans un coeur : compatible SMP.
;  * Le noyau est preemptible partout ou les IRQ sont autorisees ; tout
;    verrou se prend en irqsave.
;  * Priorites (TH_PRIO : 0 normal, 1 haute, 2 temps reel souple) : a chaque
;    passage, chaque thread pret gagne 1 (TH_BOOST) ; on prend le plus grand
;    PRIO_BASE(prio) + TH_BOOST, le premier dans l'ordre du tourniquet en cas
;    d'egalite, et son TH_BOOST revient a 0. Un thread prioritaire passe donc
;    devant, mais un thread normal pret finit toujours par passer (pas de
;    famine : au pire tous les PRIO_BASE(2) + 2 passages).
;  * TPIDR_EL0 (registre de thread d'EL0, TLS) : sauve et recharge a chaque
;    commutation (TH_TPIDR) ; EL0 l'ecrit lui-meme (msr), le noyau jamais.
; ============================================================================

MAX_CPUS	= 4
SLICE_TICKS	= 10			; quantum : 10 ms
WAKE_BOOST	= 32			; points d'un thread reveille (interruption, service) :
					; il passe devant les threads prets de son coeur, sans
					; les affamer
SGI_RESCHED	= 0

; --- structure per-CPU -------------------------------------------------------
PC_ID		= 0
PC_CURRENT	= 8
PC_IDLE		= 16
PC_RQ_HEAD	= 24
PC_RQ_LOCK	= 32
PC_NEED_RESCHED	= 40
PC_SLICE	= 48
PC_NTHREADS	= 56
PC_SWITCHES	= 64
PC_BUSY		= 72			; ticks passes hors idle
PC_TOTAL	= 80
PC_LOAD		= 88			; charge de la derniere seconde (%)
PC_BUSY_MARK	= 96
PC_TOTAL_MARK	= 104
PC_LAST_ELR	= 112			; derniere adresse interrompue (diag.asm)
PC_LAST_EL0	= 120			; derniere adresse EL0 interrompue
PERCPU_SIZE	= 128

; --- TCB ---------------------------------------------------------------------
TH_CTX		= 0			; x19..x30, sp (13 x 8)
TH_STATE	= 112
TH_NEXT		= 120
TH_PREV		= 128
TH_ID		= 136
TH_FLAGS	= 144
TH_WAKE		= 152			; echeance CNTPCT si TS_SLEEPING
TH_CPU		= 160
TH_PROC		= 168			; process proprietaire (jalon 3)
TH_RUNTIME	= 176			; ticks consommes
TH_NAME		= 184			; 24 octets
TH_UENTRY	= 208			; thread EL0 : point d'entree
TH_USP		= 216			;   pile utilisateur
TH_UARG		= 224			;   argument (x0)
TH_PNEXT	= 232			; chainage des threads d'un process
TH_WAIT		= 240			; adresse attendue (futex) si TS_BLOCKED
TH_EXIT_CODE	= 248
TH_FP		= 256			; q0..q31 (512) + fpcr + fpsr (16), aligne 16
TH_FP_SIZE	= 528
TH_TPIDR	= TH_FP + TH_FP_SIZE	; TPIDR_EL0 du thread (TLS d'EL0)
TH_PRIO		= TH_TPIDR + 8		; THREAD_PRIO_* (0 normal, 1 haute, 2 temps reel)
TH_BOOST	= TH_PRIO + 8		; passages attendus depuis son dernier tour
TH_KILL		= TH_BOOST + 8		; 1 : tue (thread_kill) a son retour en EL0
TH_KILL_CODE	= TH_KILL + 8		; code de sortie donne par thread_kill
TH_SIGPEND	= TH_KILL_CODE + 8	; signaux en attente (signal.asm)
TH_SIGMASK	= TH_SIGPEND + 8	; signaux masques
TH_SIGOLD	= TH_SIGMASK + 8	; masque a rendre (sigsuspend)
TH_SIGOLDF	= TH_SIGOLD + 8		; 1 : TH_SIGOLD est pose
TH_SIGFAULT	= TH_SIGOLDF + 8	; faute a livrer : son signal
TH_SIGADDR	= TH_SIGFAULT + 8	;   adresse
TH_SIGCODE	= TH_SIGADDR + 8	;   si_code
TH_ALT_SP	= TH_SIGCODE + 8	; pile des signaux, taille (contigus)
TH_ALT_SIZE	= TH_ALT_SP + 8
TCB_SIZE	= TH_ALT_SIZE + 8
PRIO_BASE_HIGH	= 4			; avance d'un thread de priorite haute
PRIO_BASE_RT	= 16			; et temps reel souple (prio 2)
THREAD_PAGES	= 4
THREAD_BLOCK	= THREAD_PAGES * PAGE_SIZE

TS_READY	= 0			; executable (ou en cours)
TS_SLEEPING	= 1
TS_BLOCKED	= 2
TS_DEAD		= 3

TF_FP		= 0			; bit : le thread utilise FP/SIMD
TF_USER		= 1			; bit : thread d'un process EL0

CPACR_FP_TRAP_EL0 = 1 shl 20		; FPEN=01 : EL1 libre, EL0 piege
CPACR_FP_ON	= 3 shl 20		; FPEN=11


; x0 = numero de coeur -> x0 = structure per-CPU
percpu_of:
	adr	x1,percpu_area
	add	x0,x1,x0,lsl 7			; PERCPU_SIZE = 128
	ret

; Initialise la structure per-CPU du coeur courant, et un TCB "idle" qui
; represente le contexte courant (pile de boot). x0 = numero de coeur
sched_init_cpu:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	percpu_of
	mov	x20,x0
	str	x19,[x20,PC_ID]
	msr	tpidr_el1,x20
	adr	x0,idle_tcbs
	mov	x1,TCB_SIZE
	madd	x0,x19,x1,x0
	str	x19,[x0,TH_CPU]
	str	x0,[x20,PC_IDLE]
	str	x0,[x20,PC_CURRENT]
	mov	x1,SLICE_TICKS
	str	x1,[x20,PC_SLICE]
	movl	x0,CPACR_FP_TRAP_EL0
	msr	cpacr_el1,x0
	isb
	ldp	x19,x20,[sp],16
	epilogue

; x0 = point d'entree, x1 = argument, x2 = nom (chaine)
; -> x0 = TCB pret mais pas encore ordonnance (0 si plus de memoire)
thread_new:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,THREAD_PAGES
	bl	pmm_alloc_n
	cbz	x0,.out
	mov	x22,x0
	mov	x1,TCB_SIZE + 15
	and	x1,x1,not 15
	bl	mem_zero
	; contexte initial : switch_to "revient" dans thread_trampoline
	stp	x19,x20,[x22,TH_CTX]		; x19 = entree, x20 = argument
	adr	x0,thread_trampoline
	str	x0,[x22,TH_CTX+88]		; x30
	add	x0,x22,THREAD_BLOCK
	str	x0,[x22,TH_CTX+96]		; sp
	adr	x0,next_tid
	bl	atomic_inc
	str	x0,[x22,TH_ID]
	add	x0,x22,TH_NAME			; nom (23 caracteres max)
	mov	x1,x21
	mov	x2,23
.name:	ldrb	w3,[x1],1
	cbz	w3,.named
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.name
.named:	mov	x0,x22
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = coeur choisi (tourniquet sur les coeurs en ligne)
pick_cpu:
	prologue
	adr	x0,next_cpu
	bl	atomic_inc
	adr	x1,cpus_online
	ldr	x1,[x1]
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	epilogue

; x0 = point d'entree, x1 = argument, x2 = nom (chaine), x3 = coeur (-1 = auto)
; -> x0 = TCB (0 si plus de memoire)
thread_create:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x3
	bl	thread_new
	cbz	x0,.out
	mov	x20,x0
	mov	x0,x19
	tbz	x0,63,.cpu
	bl	pick_cpu
.cpu:	mov	x1,x0
	mov	x0,x20
	bl	thread_enqueue
	mov	x0,x20
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = TCB, x1 = coeur : insere dans la liste du coeur et le reveille
thread_enqueue:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	str	x20,[x19,TH_CPU]
	mov	x0,x20
	bl	percpu_of
	mov	x21,x0
	add	x0,x21,PC_RQ_LOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	ldr	x1,[x21,PC_RQ_HEAD]
	cbnz	x1,.link
	str	x19,[x21,PC_RQ_HEAD]		; liste vide
	str	x19,[x19,TH_NEXT]
	str	x19,[x19,TH_PREV]
	b	.linked
.link:	ldr	x2,[x1,TH_PREV]			; queue = head->prev
	str	x19,[x2,TH_NEXT]
	str	x2,[x19,TH_PREV]
	str	x1,[x19,TH_NEXT]
	str	x19,[x1,TH_PREV]
.linked:
	ldr	x1,[x21,PC_NTHREADS]
	add	x1,x1,1
	str	x1,[x21,PC_NTHREADS]
	add	x0,x21,PC_RQ_LOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x20
	bl	cpu_kick
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = coeur : demande un passage dans l'ordonnanceur (IPI si autre coeur)
cpu_kick:
	this_cpu x1
	ldr	x2,[x1,PC_ID]
	cmp	x0,x2
	b.ne	.ipi
	mov	x2,1
	str	x2,[x1,PC_NEED_RESCHED]
	ret
.ipi:	b	gic_send_sgi_resched

; Thread neuf : on arrive ici depuis switch_to, IRQ masquees
thread_trampoline:
	msr	daifclr,2
	mov	x0,x20
	blr	x19
	; retour de la fonction du thread : fin
thread_exit:
	msr	daifset,2
	this_cpu x0
	ldr	x1,[x0,PC_CURRENT]
	mov	x2,TS_DEAD
	str	x2,[x1,TH_STATE]
	bl	schedule			; ne revient jamais
.never:	b	.never

; Cede le processeur
thread_yield:
	prologue
	mrs	x1,daif
	str	x1,[sp,-16]!
	msr	daifset,2
	bl	schedule
	ldr	x1,[sp],16
	msr	daif,x1
	epilogue

; x0 = duree en ms
thread_sleep:
	prologue
	mrs	x1,cntfrq_el0
	mul	x0,x0,x1
	mov	x1,1000
	udiv	x0,x0,x1
	mrs	x1,cntpct_el0
	add	x0,x0,x1
	this_cpu x1
	ldr	x2,[x1,PC_CURRENT]
	str	x0,[x2,TH_WAKE]
	mov	x0,x2
	mov	x1,TS_SLEEPING
	bl	thread_park
	bl	thread_yield
	epilogue

; x0 = thread (le courant), x1 = TS_SLEEPING ou TS_BLOCKED : le pose, sauf si
; proc_kill vient de le tuer depuis un autre coeur (TS_DEAD ne s'ecrase pas) ;
; l'appelant passe ensuite par schedule (x0-x3)
thread_park:
	add	x2,x0,TH_STATE
.l:	ldaxr	x3,[x2]
	cmp	x3,TS_DEAD
	b.eq	.out
	stlxr	w3,x1,[x2]
	cbnz	w3,.l
.out:	ret

; Choisit le prochain thread et bascule. Appele IRQ masquees.
schedule:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	this_cpu x19
	ldr	x20,[x19,PC_CURRENT]
	add	x0,x19,PC_RQ_LOCK
	bl	spin_lock

	mrs	x23,cntpct_el0
	mov	x0,x23				; alea : chaque commutation
	bl	rng_mix
	mov	x25,0				; meilleur candidat
	mov	x26,0				; sa note
	ldr	x21,[x19,PC_IDLE]
	ldr	x2,[x19,PC_RQ_HEAD]
	cbz	x2,.pick_idle
	mov	x22,x2				; depart = tete si on vient d'idle
	cmp	x20,x21
	b.eq	.start
	ldr	x22,[x20,TH_NEXT]		; sinon le suivant du courant
.start:	ldr	x24,[x19,PC_NTHREADS]
.scan:	cbz	x24,.chosen
	sub	x24,x24,1
	ldr	x3,[x22,TH_STATE]
	cmp	x3,TS_READY
	b.eq	.cand
	cmp	x3,TS_SLEEPING
	b.ne	.blocked
	ldr	x4,[x22,TH_WAKE]
	cmp	x23,x4
	b.lo	.skip
	str	xzr,[x22,TH_STATE]		; echeance passee : pret
	b	.cand
.blocked:
	cmp	x3,TS_BLOCKED			; futex ou join avec delai (TH_WAKE
	b.ne	.dead_check			; non nul) : echeance passee -> pret
	ldr	x4,[x22,TH_WAKE]		; (TH_WAIT reste pose : futex_wait
	cbz	x4,.skip			; y lit E_TIMEDOUT)
	cmp	x23,x4
	b.lo	.skip
	str	xzr,[x22,TH_STATE]
	b	.cand
.dead_check:
	cmp	x3,TS_DEAD
	b.ne	.skip
	cmp	x22,x20				; on tourne encore sur sa pile
	b.eq	.skip
	ldr	x5,[x22,TH_NEXT]
	mov	x0,x22
	bl	rq_unlink			; x0 = thread mort, x19 = per-CPU
	str	x5,[sp,-16]!
	ldr	x0,[x22,TH_PROC]
	cbz	x0,.no_proc
	mov	x1,x22
	bl	proc_thread_gone		; dernier thread -> process zombie
.no_proc:
	ldr	x5,[sp],16
	mov	x0,x22
	mov	x1,THREAD_PAGES
	mov	x22,x5
	bl	pmm_free_n
	ldr	x2,[x19,PC_RQ_HEAD]
	cbz	x2,.chosen
	b	.scan
.cand:	ldr	x4,[x22,TH_BOOST]		; candidat : note = base de sa
	add	x4,x4,1				; priorite + passages attendus
	str	x4,[x22,TH_BOOST]
	ldr	x5,[x22,TH_PRIO]
	cbz	x5,.note
	cmp	x5,1
	mov	x6,PRIO_BASE_HIGH
	mov	x7,PRIO_BASE_RT
	csel	x6,x6,x7,eq
	add	x4,x4,x6
.note:	cbz	x25,.take
	cmp	x4,x26				; strictement meilleur : a egalite,
	b.ls	.skip				; le premier du tourniquet
.take:	mov	x25,x22
	mov	x26,x4
.skip:	ldr	x22,[x22,TH_NEXT]
	b	.scan
.chosen:
	cbz	x25,.pick_idle
	str	xzr,[x25,TH_BOOST]
	mov	x22,x25
	b	.found
.pick_idle:
	mov	x22,x21
.found:	add	x0,x19,PC_RQ_LOCK
	bl	spin_unlock
	mov	x0,SLICE_TICKS
	str	x0,[x19,PC_SLICE]
	cmp	x22,x20
	b.eq	.same
	str	x22,[x19,PC_CURRENT]
	ldr	x0,[x19,PC_SWITCHES]
	add	x0,x0,1
	str	x0,[x19,PC_SWITCHES]
	mov	x0,x20
	mov	x1,x22
	bl	switch_to
.same:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = thread a retirer de la liste du coeur x19 (verrou tenu)
rq_unlink:
	ldr	x1,[x0,TH_NEXT]
	ldr	x2,[x0,TH_PREV]
	ldr	x3,[x19,PC_NTHREADS]
	sub	x3,x3,1
	str	x3,[x19,PC_NTHREADS]
	cbz	x3,.empty
	str	x1,[x2,TH_NEXT]
	str	x2,[x1,TH_PREV]
	ldr	x3,[x19,PC_RQ_HEAD]
	cmp	x3,x0
	b.ne	.ret
	str	x1,[x19,PC_RQ_HEAD]
.ret:	ret
.empty:	str	xzr,[x19,PC_RQ_HEAD]
	ret

; x0 = thread sortant, x1 = thread entrant
switch_to:
	stp	x19,x20,[x0,TH_CTX+0]
	stp	x21,x22,[x0,TH_CTX+16]
	stp	x23,x24,[x0,TH_CTX+32]
	stp	x25,x26,[x0,TH_CTX+48]
	stp	x27,x28,[x0,TH_CTX+64]
	stp	x29,x30,[x0,TH_CTX+80]
	mov	x2,sp
	str	x2,[x0,TH_CTX+96]
	mrs	x2,tpidr_el0			; --- registre de thread d'EL0 (TLS)
	str	x2,[x0,TH_TPIDR]
	ldr	x2,[x1,TH_TPIDR]
	msr	tpidr_el0,x2

	ldr	x2,[x0,TH_FLAGS]		; --- etat FP sortant
	tbz	x2,TF_FP,.no_save
	add	x2,x0,TH_FP
	stp	q0,q1,[x2,32*0]
	stp	q2,q3,[x2,32*1]
	stp	q4,q5,[x2,32*2]
	stp	q6,q7,[x2,32*3]
	stp	q8,q9,[x2,32*4]
	stp	q10,q11,[x2,32*5]
	stp	q12,q13,[x2,32*6]
	stp	q14,q15,[x2,32*7]
	stp	q16,q17,[x2,32*8]
	stp	q18,q19,[x2,32*9]
	stp	q20,q21,[x2,32*10]
	stp	q22,q23,[x2,32*11]
	stp	q24,q25,[x2,32*12]
	stp	q26,q27,[x2,32*13]
	stp	q28,q29,[x2,32*14]
	stp	q30,q31,[x2,32*15]
	mrs	x3,fpcr
	mrs	x4,fpsr
	add	x2,x2,512
	stp	x3,x4,[x2]
.no_save:
	ldr	x2,[x1,TH_FLAGS]		; --- etat FP entrant
	tbz	x2,TF_FP,.fp_trap
	add	x2,x1,TH_FP
	ldp	q0,q1,[x2,32*0]
	ldp	q2,q3,[x2,32*1]
	ldp	q4,q5,[x2,32*2]
	ldp	q6,q7,[x2,32*3]
	ldp	q8,q9,[x2,32*4]
	ldp	q10,q11,[x2,32*5]
	ldp	q12,q13,[x2,32*6]
	ldp	q14,q15,[x2,32*7]
	ldp	q16,q17,[x2,32*8]
	ldp	q18,q19,[x2,32*9]
	ldp	q20,q21,[x2,32*10]
	ldp	q22,q23,[x2,32*11]
	ldp	q24,q25,[x2,32*12]
	ldp	q26,q27,[x2,32*13]
	ldp	q28,q29,[x2,32*14]
	ldp	q30,q31,[x2,32*15]
	add	x2,x2,512
	ldp	x3,x4,[x2]
	msr	fpcr,x3
	msr	fpsr,x4
	movl	x3,CPACR_FP_ON
	b	.fp_set
.fp_trap:
	movl	x3,CPACR_FP_TRAP_EL0
.fp_set:
	msr	cpacr_el1,x3

	ldr	x2,[x0,TH_PROC]			; --- espace d'adressage
	ldr	x3,[x1,TH_PROC]
	cmp	x2,x3
	b.eq	.same_mm
	stp	x0,x1,[sp,-16]!
	mov	x0,x3
	str	x30,[sp,-16]!
	bl	vm_activate			; 0 = tables du noyau seules
	ldr	x30,[sp],16
	ldp	x0,x1,[sp],16
.same_mm:
	ldp	x19,x20,[x1,TH_CTX+0]
	ldp	x21,x22,[x1,TH_CTX+16]
	ldp	x23,x24,[x1,TH_CTX+32]
	ldp	x25,x26,[x1,TH_CTX+48]
	ldp	x27,x28,[x1,TH_CTX+64]
	ldp	x29,x30,[x1,TH_CTX+80]
	ldr	x2,[x1,TH_CTX+96]
	mov	sp,x2
	isb
	ret

; Appele a chaque tick, sur chaque coeur (IRQ masquees)
sched_tick:
	this_cpu x0
	ldr	x1,[x0,PC_CURRENT]
	ldr	x2,[x0,PC_IDLE]
	ldr	x3,[x0,PC_TOTAL]
	add	x3,x3,1
	str	x3,[x0,PC_TOTAL]
	mov	x4,1
	cmp	x1,x2
	b.eq	.idle				; idle : on re-examine la liste a chaque tick
	ldr	x3,[x0,PC_BUSY]
	add	x3,x3,1
	str	x3,[x0,PC_BUSY]
	ldr	x3,[x1,TH_RUNTIME]
	add	x3,x3,1
	str	x3,[x1,TH_RUNTIME]
	ldr	x3,[x0,PC_SLICE]
	subs	x3,x3,1
	str	x3,[x0,PC_SLICE]
	b.ne	.load
.idle:	str	x4,[x0,PC_NEED_RESCHED]
.load:	ldr	x3,[x0,PC_TOTAL]		; charge en % chaque seconde
	ldr	x5,[x0,PC_TOTAL_MARK]
	sub	x6,x3,x5
	cmp	x6,HZ
	b.lo	.done
	ldr	x7,[x0,PC_BUSY]
	ldr	x8,[x0,PC_BUSY_MARK]
	sub	x8,x7,x8
	mov	x9,100
	mul	x8,x8,x9
	udiv	x8,x8,x6
	str	x8,[x0,PC_LOAD]
	stp	x7,x3,[x0,PC_BUSY_MARK]
.done:	ret

; Boucle idle d'un coeur (contexte de boot devenu thread idle)
cpu_idle:
	msr	daifclr,2
.loop:	wfi
	b	.loop
