; ============================================================================
;  signal.asm - signaux asynchrones vers les programmes, membarrier
;  (appels 0x2B-0x2F, 0x68, docs/ABI.md)
;
;  Chaque thread a ses signaux en attente (TH_SIGPEND), son masque (TH_SIGMASK)
;  et sa pile de signaux (TH_ALT_SP) ; les actions sont par process (une page,
;  P_SIGTAB, creee au premier SIG_ACTION). Numeros de newlib, 1 a 63.
;
;  Livraison : au retour en EL0 (exc_return), jamais ailleurs. sig_deliver
;  ecrit sur la pile du programme une trame comme celle de Linux arm64 (siginfo,
;  ucontext avec les registres et FP/SIMD), puis change la trame d'exception :
;  le gestionnaire recoit (signal, siginfo, ucontext), x30 = le retour donne a
;  SIG_ACTION, qui fait SIG_RETURN. SIG_RETURN reprend tout de l'ucontext : le
;  programme peut y changer les registres, sp et pc.
;
;  Arret et reprise (SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU, SIGCONT), sessions :
;  jobs.asm.
;
;  Fautes (sig_fault) : si le programme a un gestionnaire et que le signal
;  n'est pas masque, elles lui sont livrees avec l'adresse ; sinon le process
;  est tue, comme avant. Une pile illisible au moment d'ecrire la trame : tue.
;
;  Un thread endormi ou bloque est reveille s'il ne masque pas le signal :
;  SLEEP, EV_WAIT, CHAN_* et HANDLE_WAIT rendent E_INTR (EV_WAIT : 0), un futex
;  rend 0. Un SVC_CALL en cours n'est pas interrompu, ni un THREAD_JOIN : le
;  signal arrive a leur retour.
;
;  Verrous : futex_lock, puis P_LOCK (comme thread_kill).
; ============================================================================

SIGILL		= 4
SIGTRAP		= 5
SIGFPE		= 8
SIGKILL		= 9
SIGBUS		= 10
SIGSEGV		= 11
SIGSTOP		= 17
SIGCHLD		= 20
SIG_MAX		= 64
SIG_UNMASKABLE	= (1 shl SIGKILL) or (1 shl SIGSTOP)
SIG_IGNORED	= (1 shl 16) or (1 shl 19) or (1 shl 20) or (1 shl 28)
					; URG CONT CHLD WINCH (STOP TSTP TTIN TTOU :
					; arret, jobs.asm)
SIGB_ONSTACK	= 2			; bits de SA_*
SIGB_NODEFER	= 30
SIGB_RESETHAND	= 31

; action : 24 octets par signal dans P_SIGTAB
SGA_HANDLER	= 0			; 0 : defaut, 1 : ignore
SGA_FLAGS	= 8
SGA_MASK	= 16
SGA_SIZE	= 24

SGI_MEMBARRIER	= 1
MB_WAIT_MS	= 10

; x0 = TCB -> x0 = signaux en attente qu'il ne masque pas
sig_ready:
	ldr	x1,[x0,TH_SIGPEND]
	ldr	x2,[x0,TH_SIGMASK]
	bic	x0,x1,x2
	ret

; x0 = TCB, x1 = signal : le met en attente ; reveille le thread s'il ne le
; masque pas (sous futex_lock et P_LOCK de son process)
sig_raise:
	mov	x2,1
	lsl	x2,x2,x1
	add	x3,x0,TH_SIGPEND
.or:	ldaxr	x4,[x3]
	orr	x4,x4,x2
	stlxr	w5,x4,[x3]
	cbnz	w5,.or
	dmb	ish				; (sigsuspend : etat puis attente)
	ldr	x3,[x0,TH_SIGMASK]
	tst	x3,x2
	b.ne	.ret
	ldr	x3,[x0,TH_STATE]
	cmp	x3,TS_DEAD
	b.eq	.ret
	cbz	x3,.kick			; en cours : il passera par exc_return
	ldr	x3,[x0,TH_WAIT]
	tbnz	x3,63,.kick			; (thread_join : pas interrompu)
	str	xzr,[x0,TH_WAIT]
	str	xzr,[x0,TH_STATE]
.kick:	ldr	x0,[x0,TH_CPU]
	b	cpu_kick
.ret:	ret

; x0 = PCB du process fini (faucheur, code de sortie range) : SIGCHLD a son pere
; s'il a un gestionnaire. Sans gestionnaire, rien : l'action par defaut est
; d'ignorer, mais sig_raise interromprait son sommeil.
sig_child:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x0,[x0,P_PARENT]
	cbz	x0,.out
	bl	ws_proc_of			; (son PCB tient : c'est le faucheur qui
	cbz	x0,.out				;  les rend)
	mov	x19,x0
	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x20,x0
	add	x0,x19,P_LOCK
	bl	spin_lock
	ldr	x0,[x19,P_STATE]
	cbnz	x0,.rendre
	ldr	x0,[x19,P_SIGTAB]
	cbz	x0,.rendre
	mov	x1,SIGCHLD
	mov	x2,SGA_SIZE
	madd	x0,x1,x2,x0
	ldr	x0,[x0,SGA_HANDLER]
	cmp	x0,1
	b.ls	.rendre
	ldr	x2,[x19,P_THREADS]
	mov	x1,0				; le premier vivant, a defaut
.t:	cbz	x2,.got
	ldr	x3,[x2,TH_STATE]
	cmp	x3,TS_DEAD
	b.eq	.n
	cmp	x1,0
	csel	x1,x2,x1,eq
	ldr	x3,[x2,TH_SIGMASK]
	tbnz	x3,SIGCHLD,.n
	mov	x1,x2
	b	.got
.n:	ldr	x2,[x2,TH_PNEXT]
	b	.t
.got:	mov	x0,x1
	cbz	x0,.rendre
	mov	x1,SIGCHLD
	bl	sig_raise
.rendre:
	add	x0,x19,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	mov	x1,x20
	bl	spin_unlock_irqrestore
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- sig_send(pid (0 : soi ; < 0 : le groupe -pid), tid (0 : un thread qui ne
; le masque pas), signal) ; SIGCONT reprend le process, un arret par defaut
; l'arrete (jobs.asm) ; bit SIGF_TTY du signal : sig_send_tty
sys_sig_send:
	tbnz	x2,SIGF_TTY,sig_send_tty
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x21,SIG_MAX
	b.hs	.inval
	tbnz	x19,63,.groupe
	current_proc
	mov	x24,x0
	mov	x22,x1
	cbz	x19,.have
	mov	x0,x19
	bl	ws_proc_of
	cbz	x0,.srch
	mov	x22,x0
.have:	cbz	x21,.ok				; signal 0 : existe-t-il ?
	cmp	x21,SIGKILL
	b.ne	.send
	mov	x0,x22
	mov	x1,-9
	bl	proc_kill
.ok:	mov	x0,0
	b	.out
.send:	cmp	x21,SIGCONT_K			; reprise, meme masque ou ignore
	b.ne	.lk
	mov	x0,x22
	bl	sig_cont_proc
.lk:	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x23,x0
	add	x0,x22,P_LOCK
	bl	spin_lock
	mov	x19,E_SRCH
	ldr	x0,[x22,P_STATE]
	cbnz	x0,.rendre
	cbz	x20,.any
	mov	x0,x22
	mov	x1,x20
	bl	thread_find
	cbz	x0,.rendre
	b	.raise
.any:	ldr	x0,[x24,TH_PROC]		; soi-meme d'abord
	cmp	x0,x22
	b.ne	.scan
	ldr	x0,[x24,TH_SIGMASK]
	lsr	x0,x0,x21
	mov	x1,x24
	tbz	x0,0,.got
.scan:	ldr	x2,[x22,P_THREADS]
	mov	x1,0				; le premier vivant, a defaut
.t:	cbz	x2,.got
	ldr	x3,[x2,TH_STATE]
	cmp	x3,TS_DEAD
	b.eq	.n
	cmp	x1,0
	csel	x1,x2,x1,eq
	ldr	x3,[x2,TH_SIGMASK]
	lsr	x3,x3,x21
	tbnz	x3,0,.n
	mov	x1,x2
	b	.got
.n:	ldr	x2,[x2,TH_PNEXT]
	b	.t
.got:	mov	x0,x1
	cbz	x0,.rendre
.raise:	mov	x1,x21
	bl	job_raise			; (1 : arret par defaut, jobs.asm)
	mov	x19,x0
.rendre:
	add	x0,x22,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	mov	x1,x23
	bl	spin_unlock_irqrestore
	cmp	x19,1
	b.ne	.res
	mov	x0,x22
	mov	x1,x21
	bl	sig_stop_proc
	mov	x19,0
.res:	mov	x0,x19
	b	.out
.groupe:					; chaque process du groupe -pid (sans
	neg	x19,x19				; droits de SD0:/sys : les services ne
	mov	x22,0				; sont jamais touches) ; x22 : envoyes
	mov	x23,0				; emplacement
.g:	adr	x0,proc_cap
	ldar	x0,[x0]
	cmp	x23,x0
	b.hs	.gfin
	adr	x0,proc_tab
	ldr	x0,[x0]
	lsl	x1,x23,5
	ldr	x0,[x0,x1]			; (PT_PCB ; lu sans verrou : sig_send
	add	x23,x23,1			;  retrouve le process par son pid)
	cbz	x0,.g
	ldr	x1,[x0,P_PGID]
	cmp	x1,x19
	b.ne	.g
	ldr	x1,[x0,P_DROITS]
	cbnz	x1,.g
	ldr	x1,[x0,P_STATE]
	cbnz	x1,.g
	ldr	x0,[x0,P_PID]
	mov	x1,0
	mov	x2,x21
	bl	sys_sig_send
	cbnz	x0,.g
	add	x22,x22,1
	b	.g
.gfin:	mov	x0,0
	cbnz	x22,.out
.srch:	mov	x0,E_SRCH
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- setpgid(pid (0 : soi), groupe (0 : = pid)) -> 0 : groupes de process
; (docs/TERMINAL-ET-FORK.md 2.3) ; soi, ou un fils (P_PARENT) de la meme
; session ; jamais un meneur de session ; le groupe n'est pas verifie.
; pid = -1 : setsid (jobs.asm)
sys_setpgid:
	cmn	x0,1
	b.eq	sys_setsid
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	tbnz	x20,63,.inval
	current_proc
	mov	x2,x1				; (PCB de l'appelant)
	cbz	x19,.self
	mov	x0,x19
	str	x2,[sp,-16]!
	bl	ws_proc_of
	ldr	x2,[sp],16
	cbz	x0,.srch
	cmp	x0,x2
	b.eq	.set
	ldr	x3,[x0,P_PARENT]		; un fils ?
	ldr	x4,[x2,P_PID]
	cmp	x3,x4
	b.ne	.perm
	b	.set
.self:	mov	x0,x2
.set:	ldr	x1,[x0,P_PID]
	ldr	x3,[x0,P_SID]			; meneur de session : non
	cmp	x3,x1
	b.eq	.perm
	ldr	x4,[x2,P_SID]			; autre session : non
	cmp	x3,x4
	b.ne	.perm
	cmp	x20,0
	csel	x20,x1,x20,eq
	str	x20,[x0,P_PGID]
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.srch:	mov	x0,E_SRCH
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- getpgid(pid (0 : soi)) -> groupe ; bit GETPGID_SID du pid : la session
sys_getpgid:
	tbnz	x0,GETPGID_SID,sys_getsid
	prologue
	cbz	x0,.self
	bl	ws_proc_of
	cbz	x0,.srch
	b	.got
.self:	current_proc
	mov	x0,x1
.got:	ldr	x0,[x0,P_PGID]
	b	.out
.srch:	mov	x0,E_SRCH
.out:	epilogue

; --- sig_action(signal, gestionnaire (0 : defaut, 1 : ignore), SA_*, masque,
;        retour : l'adresse qui fait SIG_RETURN) -> 0 ------------------------
sys_sig_action:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	sub	x0,x19,1
	cmp	x0,SIG_MAX-1
	b.hs	.inval
	cmp	x19,SIGKILL
	b.eq	.inval
	cmp	x19,SIGSTOP
	b.eq	.inval
	current_proc
	mov	x24,x1
	ldr	x0,[x24,P_SIGTAB]
	cbnz	x0,.tab
	bl	pmm_alloc
	cbz	x0,.nomem
	mov	x1,PAGE_SIZE
	str	x0,[sp,-16]!
	bl	mem_zero
	add	x0,x24,P_LOCK			; deux threads en meme temps : une table
	bl	spin_lock_irqsave
	mov	x1,x0
	ldr	x2,[sp],16
	ldr	x3,[x24,P_SIGTAB]
	cbnz	x3,.twice
	str	x2,[x24,P_SIGTAB]
	mov	x2,0
.twice:	add	x0,x24,P_LOCK
	str	x2,[sp,-16]!
	bl	spin_unlock_irqrestore
	ldr	x0,[sp],16
	cbz	x0,.tab
	bl	pmm_free
.tab:	add	x0,x24,P_LOCK
	bl	spin_lock_irqsave
	mov	x1,x0
	ldr	x2,[x24,P_SIGTAB]
	mov	x3,SGA_SIZE
	madd	x2,x19,x3,x2
	stp	x20,x21,[x2,SGA_HANDLER]
	str	x22,[x2,SGA_MASK]
	cbz	x23,.same
	str	x23,[x24,P_SIGRET]
.same:	add	x0,x24,P_LOCK
	bl	spin_unlock_irqrestore
	mov	x0,0
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB du process termine (faucheur)
sig_proc_exit:
	ldr	x1,[x0,P_SIGTAB]
	cbz	x1,.ret
	str	xzr,[x0,P_SIGTAB]
	mov	x0,x1
	b	pmm_free
.ret:	ret

; x0 = ESR : une faute d'EL0, sous IRQ masquees -> x0 = 1 si elle sera livree
; au programme (exc_return), 0 s'il faut le tuer
sig_fault:
	lsr	x1,x0,26			; classe
	and	x2,x0,0x3F
	mov	x3,SIGSEGV
	mov	x4,1				; SEGV_MAPERR
	cmp	x1,0x24
	b.eq	.mem
	cmp	x1,0x20
	b.eq	.mem
	mov	x3,SIGILL			; ILL_ILLOPC
	cbz	x1,.go
	cmp	x1,0x0E
	b.eq	.go
	mov	x3,SIGBUS			; BUS_ADRALN
	cmp	x1,0x22
	b.eq	.go
	cmp	x1,0x26
	b.eq	.go
	mov	x3,SIGTRAP			; TRAP_BRKPT
	cmp	x1,0x3C
	b.eq	.go
	mov	x3,SIGFPE
	mov	x4,0
	cmp	x1,0x2C
	b.eq	.go
	mov	x0,0
	ret
.mem:	cmp	x2,0x21				; alignement
	b.ne	.seg
	mov	x3,SIGBUS
	b	.go
.seg:	ubfx	x2,x2,2,4
	cmp	x2,3				; permission : SEGV_ACCERR
	b.ne	.go
	mov	x4,2
.go:	this_cpu x5
	ldr	x5,[x5,PC_CURRENT]
	ldr	x6,[x5,TH_PROC]
	mov	x0,0
	ldr	x7,[x6,P_VM_OOM]		; (plus de memoire : tue, avec le message)
	cbnz	x7,.ret
	ldr	x7,[x5,TH_SIGMASK]
	lsr	x7,x7,x3
	tbnz	x7,0,.ret
	ldr	x7,[x6,P_SIGTAB]
	cbz	x7,.ret
	mov	x8,SGA_SIZE
	madd	x7,x3,x8,x7
	ldr	x7,[x7,SGA_HANDLER]
	cmp	x7,1
	b.ls	.ret
	mrs	x0,far_el1
	cmp	x1,0x24
	b.eq	.addr
	cmp	x1,0x20
	b.eq	.addr
	mrs	x0,elr_el1
.addr:	str	x0,[x5,TH_SIGADDR]
	str	x4,[x5,TH_SIGCODE]
	str	x3,[x5,TH_SIGFAULT]
	mov	x2,1
	lsl	x2,x2,x3
	add	x3,x5,TH_SIGPEND
.or:	ldaxr	x4,[x3]
	orr	x4,x4,x2
	stlxr	w6,x4,[x3]
	cbnz	w6,.or
	mov	x0,1
.ret:	ret

; x0 = trame d'exception (EL0), IRQ permises : livre un signal en attente
; (exc_return, qui recommence tant qu'il en reste)
sig_deliver:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	this_cpu x0
	ldr	x20,[x0,PC_CURRENT]
	ldr	x21,[x20,TH_PROC]
	ldr	x22,[x20,TH_SIGFAULT]		; la faute d'abord
	cbnz	x22,.pick
	mov	x0,x20
	bl	sig_ready
	cbz	x0,.out
	rbit	x0,x0
	clz	x22,x0
.pick:	mov	x1,1
	lsl	x1,x1,x22
	add	x2,x20,TH_SIGPEND
.clr:	ldaxr	x3,[x2]
	bic	x3,x3,x1
	stlxr	w4,x3,[x2]
	cbnz	w4,.clr
	add	x0,x21,P_LOCK			; son action
	bl	spin_lock_irqsave
	mov	x26,x0
	mov	x23,0
	mov	x24,0
	mov	x25,0
	ldr	x2,[x21,P_SIGTAB]
	cbz	x2,.noact
	mov	x3,SGA_SIZE
	madd	x2,x22,x3,x2
	ldp	x23,x24,[x2,SGA_HANDLER]
	ldr	x25,[x2,SGA_MASK]
	tbz	x24,SIGB_RESETHAND,.noact
	cmp	x23,1
	b.ls	.noact
	str	xzr,[x2,SGA_HANDLER]
.noact:	add	x0,x21,P_LOCK
	mov	x1,x26
	bl	spin_unlock_irqrestore
	cmp	x23,1
	b.eq	.skip
	b.hi	.run
	movl	x0,SIG_STOPS			; action par defaut : arret (jobs.asm)
	lsr	x0,x0,x22
	tbz	x0,0,.dfl
	mov	x0,x21
	mov	x1,x22
	bl	sig_stop_proc
	b	.skip
.dfl:	movl	x0,SIG_IGNORED
	lsr	x0,x0,x22
	tbnz	x0,0,.skip
.kill:	mov	x0,x21
	neg	x1,x22
	bl	proc_kill
	b	thread_exit
.bad:	adr	x0,msg_sigstack			; pile illisible
	bl	kputs
	mov	x22,SIGSEGV
	b	.kill
.skip:	ldr	x0,[x20,TH_SIGOLDF]		; rien a lancer : sigsuspend est fini
	cbz	x0,.fin
	ldr	x0,[x20,TH_SIGOLD]
	str	x0,[x20,TH_SIGMASK]
	str	xzr,[x20,TH_SIGOLDF]
	b	.fin
.run:	ldr	x26,[x19,FR_SP_EL0]		; quelle pile ?
	tbz	x24,SIGB_ONSTACK,.usual
	ldr	x0,[x20,TH_ALT_SP]
	ldr	x1,[x20,TH_ALT_SIZE]
	cbz	x1,.usual
	sub	x2,x26,x0
	cmp	x2,x1
	b.lo	.usual				; (deja dessus)
	add	x26,x0,x1
.usual:	sub	x26,x26,SGF_SIZE
	and	x26,x26,not 15
	mov	x0,x21
	mov	x1,x26
	mov	x2,SGF_SIZE
	bl	vm_check_user
	cbz	x0,.bad
	mov	x0,x26
	mov	x1,SGF_SIZE
	bl	mem_zero
	str	w22,[x26,SGF_SI+SIF_SIGNO]
	ldr	x0,[x20,TH_SIGFAULT]
	cbz	x0,.user
	ldr	x0,[x20,TH_SIGCODE]
	str	w0,[x26,SGF_SI+SIF_CODE]
	ldr	x0,[x20,TH_SIGADDR]
	str	x0,[x26,SGF_SI+SIF_ADDR]
	str	x0,[x26,SGF_UC+UCX_FAULT]
.user:	ldr	x0,[x20,TH_ALT_SP]
	ldr	x1,[x20,TH_ALT_SIZE]
	str	x0,[x26,SGF_UC+UCX_SS_SP]
	str	x1,[x26,SGF_UC+UCX_SS_SIZE]
	mov	w0,2				; SS_DISABLE
	cbz	x1,.ssf
	mov	w0,0
.ssf:	str	w0,[x26,SGF_UC+UCX_SS_FLAGS]
	ldr	x0,[x20,TH_SIGMASK]		; le masque a rendre
	ldr	x1,[x20,TH_SIGOLDF]
	cbz	x1,.savm
	ldr	x0,[x20,TH_SIGOLD]
	str	xzr,[x20,TH_SIGOLDF]
.savm:	str	x0,[x26,SGF_UC+UCX_SIGMASK]
	add	x0,x26,SGF_UC+UCX_REGS		; registres
	mov	x1,x19
	mov	x2,31*8
	bl	mem_copy
	ldr	x0,[x19,FR_SP_EL0]
	str	x0,[x26,SGF_UC+UCX_SP]
	ldp	x0,x1,[x19,FR_ELR]
	str	x0,[x26,SGF_UC+UCX_PC]
	str	x1,[x26,SGF_UC+UCX_PSTATE]
	ldr	x0,[x20,TH_FLAGS]
	tbz	x0,TF_FP,.nofp
	add	x2,x26,SGF_UC+UCX_FP		; comme fpsimd_context de Linux
	movl	w0,FPSIMD_MAGIC
	str	w0,[x2]
	mov	w0,FPSIMD_SIZE
	str	w0,[x2,4]
	mrs	x0,fpsr
	str	w0,[x2,8]
	mrs	x0,fpcr
	str	w0,[x2,12]
	add	x2,x2,16
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
.nofp:	ldr	x0,[x20,TH_SIGMASK]		; masque du gestionnaire
	orr	x0,x0,x25
	tbnz	x24,SIGB_NODEFER,.defer
	mov	x1,1
	lsl	x1,x1,x22
	orr	x0,x0,x1
.defer:	movl	x1,SIG_UNMASKABLE
	bic	x0,x0,x1
	str	x0,[x20,TH_SIGMASK]
	str	x22,[x19,FR_X0]			; gestionnaire (signal, siginfo, ucontext)
	add	x0,x26,SGF_UC
	stp	x26,x0,[x19,FR_X0+8]
	ldr	x0,[x21,P_SIGRET]
	str	x0,[x19,FR_X30]
	str	x26,[x19,FR_SP_EL0]
	str	x23,[x19,FR_ELR]
.fin:	str	xzr,[x20,TH_SIGFAULT]
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

msg_sigstack	db '[signal] pile illisible, process tu',0xC3,0xA9,10,0
		align 4

; --- sig_return() : fin d'un gestionnaire, depuis la trame qu'il a sur sa pile
; (sp du programme = la trame) ; ne rend rien : tout vient de l'ucontext
sys_sig_return:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	this_cpu x0
	ldr	x20,[x0,PC_CURRENT]
	ldr	x21,[x20,TH_PROC]
	movl	x19,THREAD_BLOCK-FRAME_SIZE	; la trame du SVC, en haut de sa pile
	add	x19,x20,x19
	ldr	x22,[x19,FR_SP_EL0]
	tst	x22,15
	b.ne	.bad
	mov	x0,x21
	mov	x1,x22
	mov	x2,SGF_SIZE
	bl	vm_check_user
	cbz	x0,.bad
	mov	x0,x19
	add	x1,x22,SGF_UC+UCX_REGS
	mov	x2,31*8
	bl	mem_copy
	ldr	x0,[x22,SGF_UC+UCX_SP]
	str	x0,[x19,FR_SP_EL0]
	ldr	x0,[x22,SGF_UC+UCX_PC]
	ldr	x1,[x22,SGF_UC+UCX_PSTATE]
	and	x1,x1,0xF0000000		; NZCV seulement : EL0t, IRQ permises
	stp	x0,x1,[x19,FR_ELR]
	ldr	x0,[x22,SGF_UC+UCX_SIGMASK]
	movl	x1,SIG_UNMASKABLE
	bic	x0,x0,x1
	str	x0,[x20,TH_SIGMASK]
	ldr	x0,[x20,TH_FLAGS]
	tbz	x0,TF_FP,.nofp
	add	x2,x22,SGF_UC+UCX_FP
	ldr	w0,[x2]
	movl	w1,FPSIMD_MAGIC
	cmp	w0,w1
	b.ne	.nofp
	ldr	w0,[x2,8]
	msr	fpsr,x0
	ldr	w0,[x2,12]
	msr	fpcr,x0
	add	x2,x2,16
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
.nofp:	ldr	x0,[x19,FR_X0]			; (syscall_dispatch le remet dans la trame)
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.bad:	mov	x0,x21
	mov	x1,-11
	bl	proc_kill
	b	thread_exit

; --- sig_mask(SIGM_*, ensemble) : masque du thread -----------------------------
;  SIGM_SET, SIGM_BLOCK, SIGM_UNBLOCK -> ancien masque ; SIGM_PENDING -> en
;  attente ; SIGM_SUSPEND : ce masque le temps de recevoir un signal -> E_INTR ;
;  SIGM_WAIT -> un signal de l'ensemble, retire de l'attente (sigwait)
sys_sig_mask:
	prologue
	stp	x19,x20,[sp,-16]!
	movl	x2,SIG_UNMASKABLE
	bic	x1,x1,x2
	this_cpu x2
	ldr	x19,[x2,PC_CURRENT]
	ldr	x2,[x19,TH_SIGMASK]
	cmp	x0,SIGM_BLOCK
	b.eq	.block
	cmp	x0,SIGM_UNBLOCK
	b.eq	.unbl
	cmp	x0,SIGM_SET
	b.eq	.set
	cmp	x0,SIGM_PENDING
	b.eq	.pend
	cmp	x0,SIGM_SUSPEND
	b.eq	.susp
	cmp	x0,SIGM_WAIT
	b.eq	.wait
	mov	x0,E_INVAL
	b	.out
.block:	orr	x1,x2,x1
	b	.set
.unbl:	bic	x1,x2,x1
.set:	str	x1,[x19,TH_SIGMASK]
	mov	x0,x2
	b	.out
.pend:	ldr	x0,[x19,TH_SIGPEND]
	b	.out
.susp:	str	x2,[x19,TH_SIGOLD]
	mov	x0,1
	str	x0,[x19,TH_SIGOLDF]
	str	x1,[x19,TH_SIGMASK]
.sl:	msr	daifset,2
	mov	x0,-1				; sans echeance : un signal le reveille
	str	x0,[x19,TH_WAKE]
	mov	x0,x19
	mov	x1,TS_SLEEPING
	bl	thread_park
	dmb	ish				; (sig_raise : attente puis etat)
	ldr	x0,[x19,TH_KILL]
	cbnz	x0,.up
	mov	x0,x19
	bl	sig_ready
	cbnz	x0,.up
	bl	schedule
	msr	daifclr,2
	b	.sl
.up:	str	xzr,[x19,TH_STATE]		; TS_READY
	msr	daifclr,2
	mov	x0,E_INTR
	b	.out
.wait:	mov	x20,x1
.wl:	ldr	x1,[x19,TH_KILL]
	cbnz	x1,.intr
	ldr	x1,[x19,TH_SIGPEND]
	ands	x1,x1,x20
	b.ne	.got
	mov	x0,1
	bl	thread_sleep
	b	.wl
.got:	rbit	x1,x1
	clz	x0,x1
	mov	x2,1
	lsl	x2,x2,x0
	add	x3,x19,TH_SIGPEND
.clr:	ldaxr	x4,[x3]
	bic	x4,x4,x2
	stlxr	w5,x4,[x3]
	cbnz	w5,.clr
	b	.out
.intr:	mov	x0,E_INTR
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- sig_altstack(sp, taille (0 : aucune), sortie 16 o : sp, taille) -> 0 ------
sys_sig_altstack:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	this_cpu x0
	ldr	x3,[x0,PC_CURRENT]
	cbz	x21,.set
	check_user x21,16,.fault
	this_cpu x0
	ldr	x3,[x0,PC_CURRENT]
	ldr	x0,[x3,TH_ALT_SP]
	ldr	x1,[x3,TH_ALT_SIZE]
	stp	x0,x1,[x21]
.set:	cmn	x19,1				; -1 : lire seulement
	b.eq	.ok
	str	x19,[x3,TH_ALT_SP]
	str	x20,[x3,TH_ALT_SIZE]
.ok:	mov	x0,0
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- membarrier(commande, drapeaux) : les coeurs qui font tourner ce process
; passent une barriere, par une interruption (comme MEMBARRIER_CMD_PRIVATE_
; EXPEDITED de Linux, et SYNC_CORE : le retour d'interruption synchronise le
; coeur) -> 0 ; QUERY (0) -> commandes connues ; REGISTER : rien a faire
sys_membarrier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,8*MAX_CPUS
	cbnz	x0,.cmd
	mov	x0,MB_KNOWN
	b	.out
.cmd:	cmp	x0,MB_REGISTER
	b.eq	.ok
	cmp	x0,MB_REGISTER_SYNC
	b.eq	.ok
	cmp	x0,MB_PRIVATE
	b.eq	.go
	cmp	x0,MB_PRIVATE_SYNC
	b.eq	.go
	mov	x0,E_INVAL
	b	.out
.go:	dmb	ish
	current_proc
	mov	x19,x1
	this_cpu x0
	ldr	x20,[x0,PC_ID]
	mov	x21,0				; coeur
	mov	x22,0				; ceux qu'on a interrompus
	adr	x23,mb_ack
.c:	cmp	x21,x20
	b.eq	.cn
	mov	x0,x21
	bl	percpu_of
	ldr	x0,[x0,PC_CURRENT]
	cbz	x0,.cn
	ldr	x0,[x0,TH_PROC]
	cmp	x0,x19
	b.ne	.cn
	ldr	x0,[x23,x21,lsl 3]
	str	x0,[sp,x21,lsl 3]
	mov	x0,1
	lsl	x0,x0,x21
	orr	x22,x22,x0
	mov	x0,x21
	mov	w1,SGI_MEMBARRIER
	bl	gic_send_sgi
.cn:	add	x21,x21,1
	cmp	x21,MAX_CPUS
	b.lo	.c
	mov	x0,MB_WAIT_MS
	bl	svc_echeance
	mov	x24,x0
.w:	cbz	x22,.done
	rbit	x0,x22
	clz	x21,x0
	ldr	x0,[x23,x21,lsl 3]
	ldr	x1,[sp,x21,lsl 3]
	cmp	x0,x1
	b.ne	.acked
	mrs	x0,cntpct_el0
	cmp	x0,x24
	b.lo	.w
.acked:	mov	x0,1
	lsl	x0,x0,x21
	bic	x22,x22,x0
	b	.w
.done:	dmb	ish
.ok:	mov	x0,0
.out:	add	sp,sp,8*MAX_CPUS
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (IRQ, SGI_MEMBARRIER) : la barriere, puis la reponse
membarrier_ipi:
	dmb	ish
	this_cpu x0
	ldr	x0,[x0,PC_ID]
	adr	x1,mb_ack
	add	x1,x1,x0,lsl 3
	ldr	x0,[x1]
	add	x0,x0,1
	stlr	x0,[x1]
	ret
