; ============================================================================
;  threads.asm - threads des process et futex : appels 0x20-0x2A
;  (src/inc/syscalls.inc, docs/ABI.md)
;
;  * Creation : point d'entree, sommet de pile, argument ; thread_create_ex
;    donne aussi le TPIDR_EL0 initial (TLS), la priorite et le nom. Le thread
;    principal d'un process demarre avec TPIDR_EL0 = 0, priorite normale.
;  * Fin (thread_exit, thread_kill) : le code va dans l'anneau P_TEXIT du
;    process (les 16 dernieres fins, sous futex_lock), puis les threads qui
;    l'attendent (join : TS_BLOCKED, TH_WAIT = JOIN_KEY | tid) sont reveilles.
;    join lit l'anneau : une fin plus ancienne que les 16 dernieres rend
;    E_SRCH (comme un tid inconnu).
;  * thread_kill est differe : TH_KILL est pose, la cible meurt a son prochain
;    retour en EL0 (exc_return, vectors.asm), jamais au milieu du noyau
;    (requete de service en cours, verrous). Bloquee dans futex_wait ou join,
;    elle est reveillee ; endormie (sleep, svc_call...), elle meurt au reveil.
;  * Priorites (sched.asm) : THREAD_PRIO_NORMAL, _HIGH, _RT (temps reel
;    souple, reserve aux programmes de SD0:/sys : E_PERM sinon).
;  * Verrous : futex_lock, puis P_LOCK du process (jamais l'inverse).
; ============================================================================

E_SRCH		= -3
E_INTR		= -4
E_DEADLK	= -35
JOIN_KEY	= 1 shl 63			; TH_WAIT d'un join : JOIN_KEY | tid
THREAD_NAME_MAX	= 24				; TH_NAME (23 caracteres + 0)

; --- thread_create(entree, sommet de pile, argument) -> tid ------------------
sys_thread_create:
	mov	x3,0
	mov	x4,THREAD_PRIO_NORMAL
	mov	x5,0
; --- thread_create_ex(entree, sommet de pile, argument, TPIDR_EL0, priorite,
;     nom (0 : celui du process)) -> tid
sys_thread_create_ex:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,32			; nom
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x24,0				; nom : celui du process
	mov	x0,x4
	bl	prio_ok
	cbnz	x0,.out
	cbz	x5,.sans_nom
	mov	x0,x5
	mov	x1,sp
	bl	name_from_user
	tbnz	x0,63,.out
	mov	x24,sp
.sans_nom:
	sub	x3,x20,16			; la pile doit etre dans l'espace du process
	check_user x3,16,.fault
	current_proc
	mov	x0,x1
	mov	x1,x19
	and	x2,x20,not 15
	mov	x3,x21
	mov	x4,x22
	mov	x5,x23
	mov	x6,x24
	bl	proc_add_thread
	cbz	x0,.nomem
	mov	x0,x1				; tid
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,32
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = priorite demandee -> x0 = 0, E_INVAL (inconnue), E_PERM (temps reel
; sans les droits de SD0:/sys)
prio_ok:
	cmp	x0,THREAD_PRIO_MAX
	b.hi	.inval
	cmp	x0,THREAD_PRIO_RT
	b.ne	.ok
	this_cpu x1
	ldr	x1,[x1,PC_CURRENT]
	ldr	x1,[x1,TH_PROC]
	ldr	x1,[x1,P_DROITS]
	cbz	x1,.perm
.ok:	mov	x0,0
	ret
.inval:	mov	x0,E_INVAL
	ret
.perm:	mov	x0,E_PERM
	ret

; x0 = nom (EL0), x1 = tampon du noyau (THREAD_NAME_MAX) -> x0 = longueur,
; E_FAULT ; un nom trop long est tronque
name_from_user:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	mov	x2,THREAD_NAME_MAX
	bl	copy_str_from_user
	cmn	x0,-E_INVAL			; trop long : tronque
	b.ne	.ret
	strb	wzr,[x19,THREAD_NAME_MAX-1]
	mov	x0,THREAD_NAME_MAX-1
.ret:	ldr	x19,[sp],16
	epilogue

; --- thread_exit(code) ----------------------------------------------------------
sys_thread_exit:
	mov	x1,x0
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	bl	thread_record_exit
	b	thread_exit

; x0 = TCB qui finit, x1 = code : code note (TH_EXIT_CODE, anneau P_TEXIT du
; process) puis les threads qui l'attendent (join) sont reveilles
thread_record_exit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	str	x20,[x19,TH_EXIT_CODE]
	ldr	x21,[x19,TH_PROC]
	cbz	x21,.out
	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	ldr	x0,[x21,P_TEXIT_POS]
	and	x1,x0,P_TEXIT_N-1
	add	x2,x21,P_TEXIT
	add	x2,x2,x1,lsl 4
	ldr	x3,[x19,TH_ID]
	stp	x3,x20,[x2]
	add	x0,x0,1
	str	x0,[x21,P_TEXIT_POS]
	movl	x23,JOIN_KEY
	orr	x23,x23,x3
	add	x0,x21,P_LOCK
	bl	spin_lock
	ldr	x24,[x21,P_THREADS]
.t:	cbz	x24,.fin
	ldr	x0,[x24,TH_STATE]
	cmp	x0,TS_BLOCKED
	b.ne	.n
	ldr	x0,[x24,TH_WAIT]
	cmp	x0,x23
	b.ne	.n
	str	xzr,[x24,TH_WAIT]
	str	xzr,[x24,TH_STATE]		; TS_READY
	ldr	x0,[x24,TH_CPU]
	bl	cpu_kick
.n:	ldr	x24,[x24,TH_PNEXT]
	b	.t
.fin:	add	x0,x21,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sous P_LOCK) x0 = PCB, x1 = tid -> x0 = TCB vivant de ce process, 0 sinon
thread_find:
	ldr	x2,[x0,P_THREADS]
.t:	cbz	x2,.non
	ldr	x3,[x2,TH_ID]
	cmp	x3,x1
	b.ne	.n
	ldr	x3,[x2,TH_STATE]
	cmp	x3,TS_DEAD
	b.eq	.non
	mov	x0,x2
	ret
.n:	ldr	x2,[x2,TH_PNEXT]
	b	.t
.non:	mov	x0,0
	ret

; (sous futex_lock) x0 = PCB, x1 = tid -> x0 = 1 et x1 = code si sa fin est
; dans l'anneau, x0 = 0 sinon
texit_find:
	add	x2,x0,P_TEXIT
	mov	x3,P_TEXIT_N
.e:	ldr	x4,[x2]
	cmp	x4,x1
	b.eq	.oui
	add	x2,x2,16
	subs	x3,x3,1
	b.ne	.e
	mov	x0,0
	ret
.oui:	mov	x0,1
	ldr	x1,[x2,8]
	ret

; --- thread_join(tid) -> code de sortie du thread ---------------------------------
; Attend la fin d'un thread du meme process. E_SRCH : tid inconnu (ou fin
; sortie de l'anneau), E_DEADLK : soi-meme, E_INTR : l'appelant est tue.
sys_thread_join:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	current_proc
	mov	x20,x0
	mov	x21,x1
	mov	x0,E_SRCH
	cbz	x19,.out
	ldr	x1,[x20,TH_ID]
	mov	x0,E_DEADLK
	cmp	x1,x19
	b.eq	.out
.boucle:
	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,x21
	mov	x1,x19
	bl	texit_find
	cbnz	x0,.fini
	add	x0,x21,P_LOCK
	bl	spin_lock
	mov	x0,x21
	mov	x1,x19
	bl	thread_find
	mov	x23,x0
	add	x0,x21,P_LOCK
	bl	spin_unlock
	mov	x24,E_SRCH
	cbz	x23,.rendre
	mov	x24,E_INTR			; tue (thread_kill) : ne pas s'endormir
	ldr	x0,[x20,TH_KILL]
	cbnz	x0,.rendre
	movl	x0,JOIN_KEY			; attendre sa fin
	orr	x0,x0,x19
	str	x0,[x20,TH_WAIT]
	str	xzr,[x20,TH_WAKE]		; (sans delai)
	mov	x0,x20
	mov	x1,TS_BLOCKED
	bl	thread_park
	adr	x0,futex_lock
	bl	spin_unlock			; IRQ toujours masquees
	bl	schedule
	msr	daif,x22
	ldr	x0,[x20,TH_KILL]
	cbz	x0,.boucle
	mov	x0,E_INTR
	b	.out
.fini:	mov	x24,x1				; code
.rendre:
	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x24
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- thread_self() -> tid -------------------------------------------------------
sys_thread_self:
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x0,[x0,TH_ID]
	ret

; x0 = tid (0 : soi) -> x0 = TCB du process courant, 0 si inconnu ; x1 = PCB
; ; rend P_LOCK pris (IRQ masquees), DAIF d'avant dans x2
thread_lock_tid:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	current_proc
	mov	x20,x1
	add	x0,x20,P_LOCK
	bl	spin_lock_irqsave
	mov	x21,x0
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	cbz	x19,.ret
	mov	x0,x20
	mov	x1,x19
	bl	thread_find
.ret:	mov	x1,x20
	mov	x2,x21
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = PCB, x2 = DAIF (de thread_lock_tid) : rend P_LOCK
thread_unlock_tid:
	add	x0,x1,P_LOCK
	mov	x1,x2
	b	spin_unlock_irqrestore

; --- thread_set_priority(tid (0 : soi), priorite) -> ancienne priorite ----------
sys_thread_set_priority:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x1
	bl	prio_ok
	cbnz	x0,.out
	mov	x0,x19
	bl	thread_lock_tid
	mov	x19,E_SRCH
	cbz	x0,.rendre
	ldr	x19,[x0,TH_PRIO]
	str	x20,[x0,TH_PRIO]
.rendre:
	bl	thread_unlock_tid
	mov	x0,x19
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- thread_set_name(tid (0 : soi), nom) -> 0 (23 caracteres au plus) -----------
sys_thread_set_name:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	mov	x0,x1
	mov	x1,sp
	bl	name_from_user
	tbnz	x0,63,.out
	mov	x0,x19
	bl	thread_lock_tid
	mov	x19,E_SRCH
	cbz	x0,.rendre
	add	x3,x0,TH_NAME			; nom recopie, complete par des 0
	mov	x4,0
	mov	x5,0
.c:	ldrb	w6,[sp,x4]
	cbz	w6,.z
	cbnz	x5,.z
	b	.w
.z:	mov	x5,1
	mov	w6,0
.w:	strb	w6,[x3,x4]
	add	x4,x4,1
	cmp	x4,THREAD_NAME_MAX
	b.lo	.c
	mov	x19,0
.rendre:
	bl	thread_unlock_tid
	mov	x0,x19
.out:	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; --- thread_kill(tid, code) -> 0 ---------------------------------------------------
; Un thread du meme process ; soi-meme (ou tid 0) : comme thread_exit(code).
sys_thread_kill:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	current_proc
	mov	x21,x1
	cbz	x19,.soi
	ldr	x1,[x0,TH_ID]
	cmp	x1,x19
	b.eq	.soi
	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	add	x0,x21,P_LOCK
	bl	spin_lock
	mov	x0,x21
	mov	x1,x19
	bl	thread_find
	mov	x23,x0
	mov	x24,E_SRCH
	cbz	x23,.rendre
	str	x20,[x23,TH_KILL_CODE]
	dmb	ish
	mov	x0,1
	str	x0,[x23,TH_KILL]
	ldr	x0,[x23,TH_STATE]		; bloque (futex, join) ou endormi : reveille
	cmp	x0,TS_DEAD
	b.eq	.kick
	cbz	x0,.kick
	str	xzr,[x23,TH_WAIT]
	str	xzr,[x23,TH_STATE]
.kick:	ldr	x0,[x23,TH_CPU]			; (sur un autre coeur : il passe par
	bl	cpu_kick			;  exc_return au plus tard au tick)
	mov	x24,0
.rendre:
	add	x0,x21,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x24
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.soi:	mov	x0,x20
	b	sys_thread_exit

; --- SYS_PROC_RIGHTS proc_rights(pid) -> 1 : droits de SD0:/sys, 0 : non, E_SRCH --------
sys_proc_rights:
	prologue
	bl	proc_lookup
	cbz	x0,.srch
	ldr	x0,[x0,P_DROITS]
	cmp	x0,0
	cset	x0,ne
	b	.out
.srch:	mov	x0,E_SRCH
.out:	epilogue

; --- thread_list(pid (0 : soi), tampon, max) -> nombre ; entrees THL_* de 64 o -------
THREAD_LIST_MAX	= 64
sys_thread_list:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,THREAD_LIST_MAX
	cmp	x21,x0
	csel	x21,x0,x21,hi
	lsl	x1,x21,6
	check_user x20,x1,.fault
	adr	x0,proc_lock			; le process ne part pas pendant
	bl	spin_lock_irqsave		; la copie
	mov	x22,x0
	cbnz	x19,.pid
	current_proc
	mov	x23,x1
	b	.lu
.pid:	mov	x0,x19
	bl	proc_lookup
	mov	x23,x0
	mov	x0,E_SRCH
	cbz	x23,.rendre
.lu:	add	x0,x23,P_LOCK
	bl	spin_lock
	ldr	x2,[x23,P_THREADS]
	mov	x24,0				; entrees ecrites
.t:	cbz	x2,.fin
	cmp	x24,x21
	b.hs	.fin
	add	x3,x20,x24,lsl 6
	ldr	x4,[x2,TH_ID]
	str	x4,[x3,THL_TID]
	ldr	x4,[x2,TH_STATE]
	str	x4,[x3,THL_STATE]
	ldr	x4,[x2,TH_PRIO]
	str	x4,[x3,THL_PRIO]
	ldr	x4,[x2,TH_CPU]
	str	x4,[x3,THL_CPU]
	ldr	x4,[x2,TH_RUNTIME]
	str	x4,[x3,THL_RUNTIME]
	mov	x5,0
.nom:	add	x6,x2,TH_NAME
	ldrb	w4,[x6,x5]
	add	x6,x3,THL_NAME
	strb	w4,[x6,x5]
	add	x5,x5,1
	cmp	x5,THREAD_NAME_MAX
	b.lo	.nom
	add	x24,x24,1
	ldr	x2,[x2,TH_PNEXT]
	b	.t
.fin:	add	x0,x23,P_LOCK
	bl	spin_unlock
	mov	x0,x24
.rendre:
	mov	x19,x0
	adr	x0,proc_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x19
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- futex_wait(adresse, valeur attendue, attente ms (0 : sans fin)) ---------------
; -> 0 (reveille), E_AGAIN (la valeur a deja change), E_TIMEDOUT
sys_futex_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	tst	x19,3
	b.ne	.inval
	check_user x19,4,.fault
	cbz	x21,.verrou
	mov	x0,x21				; echeance (CNTPCT)
	bl	svc_echeance
	mov	x21,x0
.verrou:
	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	ldr	w1,[x19]
	cmp	w1,w20
	b.ne	.again
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x1,[x0,TH_KILL]			; tue (thread_kill) : ne pas s'endormir
	cbnz	x1,.intr
	str	x19,[x0,TH_WAIT]
	str	x21,[x0,TH_WAKE]		; 0 : sans delai (sched.asm)
	mov	x1,TS_BLOCKED
	bl	thread_park
	adr	x0,futex_lock
	bl	spin_unlock			; IRQ toujours masquees
	bl	schedule
	adr	x0,futex_lock			; reveil (TH_WAIT = 0) ou delai passe
	bl	spin_lock			; (TH_WAIT encore pose)
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x1,[x0,TH_WAIT]
	mov	x19,0
	cbz	x1,.reveil
	str	xzr,[x0,TH_WAIT]
	mov	x19,E_TIMEDOUT
.reveil:
	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x19
	b	.out
.again:	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,E_AGAIN
	b	.out
.intr:	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,E_INTR
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- futex_wake(adresse, nombre max) -> nombre de threads reveilles --------------
sys_futex_wake:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x23,0
	current_proc
	mov	x21,x1
	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	add	x0,x21,P_LOCK			; (la liste des threads ne bouge pas)
	bl	spin_lock
	ldr	x24,[x21,P_THREADS]
.th:	cbz	x24,.done
	cmp	x23,x20
	b.hs	.done
	ldr	x0,[x24,TH_STATE]
	cmp	x0,TS_BLOCKED
	b.ne	.next
	ldr	x0,[x24,TH_WAIT]
	cmp	x0,x19
	b.ne	.next
	str	xzr,[x24,TH_WAIT]
	str	xzr,[x24,TH_STATE]		; TS_READY
	ldr	x0,[x24,TH_CPU]
	bl	cpu_kick
	add	x23,x23,1
.next:	ldr	x24,[x24,TH_PNEXT]
	b	.th
.done:	add	x0,x21,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x23
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
