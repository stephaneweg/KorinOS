; ============================================================================
;  jobs.asm - controle des taches : process arretes, reprise, sessions
;  (docs/ABI.md, docs/TERMINAL-ET-FORK.md, etape 6)
;
;  Arret : SIGSTOP, et SIGTSTP / SIGTTIN / SIGTTOU sans gestionnaire, arretent
;  tout le process (P_STOP = le signal). L'arret se decide a l'envoi
;  (sys_sig_send, si un thread ne masque pas le signal), sinon a la livraison
;  (le signal etait masque, puis demasque). Chaque thread s'arrete a son
;  retour en EL0 (exc_return -> sig_stopped) : TS_BLOCKED, TH_WAIT = STOP_KEY,
;  hors des files de l'ordonnanceur. Un thread qui tourne en EL0 sur un autre
;  coeur y passe tout de suite (cpu_kick) ; un thread endormi ou bloque dans
;  le noyau (requete a un service, sleep, futex...) finit son appel, puis
;  s'arrete avant de rendre la main au programme : aucune instruction d'EL0
;  ne tourne plus. Le pere est averti des l'arret decide (SIGCHLD, WAIT).
;  Reprise : SIGCONT, a l'envoi, meme masque ou ignore (sig_cont_proc) ; les
;  arrets en attente sont jetes (et un arret jette un SIGCONT en attente) ;
;  le SIGCONT est ensuite livre comme les autres (gestionnaire eventuel).
;  SIGKILL : proc_kill passe aussi les threads arretes en TS_DEAD.
;  Groupe orphelin (aucun membre n'a de pere vivant dans un autre groupe de
;  la meme session) : SIGTSTP, SIGTTIN, SIGTTOU par defaut y sont jetes,
;  comme Linux ; a la fin d'un process, un groupe qui devient orphelin avec
;  un membre arrete recoit SIGHUP puis SIGCONT (job_orphans, faucheur).
;  Pere : P_JOBEV = JEV_STOP | signal, ou JEV_CONT, lu (et efface) par
;  SYS_WAIT etendu (WUNTRACED, WCONTINUED) ; SIGCHLD s'il a un gestionnaire
;  sans SA_NOCLDSTOP.
;  Sessions : P_SID (herite) ; setsid = SETPGID(-1) ; getsid = GETPGID(pid |
;  GETPGID_SID). Le terminal de controle est tenu par vterm (docs/VTERM.md).
;
;  Verrous : proc_lock, puis futex_lock, puis P_LOCK (comme ailleurs).
; ============================================================================

SIGHUP_K	= 1
SIGTSTP_K	= 18
SIGCONT_K	= 19
SIGTTIN_K	= 21
SIGTTOU_K	= 22
SIG_STOPS	= (1 shl SIGSTOP) or (1 shl SIGTSTP_K) or (1 shl SIGTTIN_K) or (1 shl SIGTTOU_K)
STOP_KEY	= 1 shl 62			; TH_WAIT d'un thread arrete (bit 63 a 0 :
						; sig_raise le reveille, il se rendort)
JEV_STOP	= 0x100				; P_JOBEV : arrete (| signal)
JEV_CONT	= 0x200				; P_JOBEV : repris
SIGF_TTY	= 16				; bit du signal de SIG_SEND (SIGF_TTY_MASK)
GETPGID_SID	= 62				; bit du pid de GETPGID (GETPGID_SID_MASK)

; x0 = groupe -> x0 = 1 s'il est orphelin : aucun membre vivant n'a de pere
; vivant dans un autre groupe de la meme session (table lue sans verrou,
; comme le signal a un groupe)
pg_orphan:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.s:	adr	x0,proc_cap
	ldar	x0,[x0]
	cmp	x20,x0
	b.hs	.oui
	adr	x0,proc_tab
	ldr	x0,[x0]
	lsl	x1,x20,5
	ldr	x21,[x0,x1]			; (PT_PCB)
	add	x20,x20,1
	cbz	x21,.s
	ldr	x1,[x21,P_PGID]
	cmp	x1,x19
	b.ne	.s
	ldr	x1,[x21,P_STATE]
	cbnz	x1,.s
	ldr	x0,[x21,P_PARENT]
	cbz	x0,.s
	bl	proc_lookup
	cbz	x0,.s
	ldr	x1,[x0,P_STATE]
	cbnz	x1,.s
	ldr	x1,[x0,P_PGID]
	cmp	x1,x19
	b.eq	.s
	ldr	x1,[x0,P_SID]
	ldr	x2,[x21,P_SID]
	cmp	x1,x2
	b.ne	.s
	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = signal (SIG_STOPS) : le process s'arrete (P_STOP), ses
; threads passent par exc_return ; le pere est averti au premier arret
sig_stop_proc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x20,SIGSTOP			; groupe orphelin : arrets du terminal jetes
	b.eq	.go
	ldr	x0,[x19,P_PGID]
	bl	pg_orphan
	cbnz	x0,.out
.go:	mov	x22,0				; 1 : avertir le pere
	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x21,x0
	add	x0,x19,P_LOCK
	bl	spin_lock
	ldr	x0,[x19,P_STATE]
	cbnz	x0,.rendre
	ldr	x0,[x19,P_STOP]
	cbnz	x0,.kick
	str	x20,[x19,P_STOP]
	orr	x0,x20,JEV_STOP
	str	x0,[x19,P_JOBEV]
	mov	x22,1
	dmb	ish
.kick:	ldr	x2,[x19,P_THREADS]
.t:	cbz	x2,.rendre
	add	x3,x2,TH_SIGPEND		; un SIGCONT en attente : jete
.c:	ldaxr	x4,[x3]
	bic	x4,x4,1 shl SIGCONT_K
	stlxr	w5,x4,[x3]
	cbnz	w5,.c
	ldr	x0,[x2,TH_STATE]
	cmp	x0,TS_DEAD
	b.eq	.n
	str	x2,[sp,-16]!
	ldr	x0,[x2,TH_CPU]
	bl	cpu_kick
	ldr	x2,[sp],16
.n:	ldr	x2,[x2,TH_PNEXT]
	b	.t
.rendre:
	add	x0,x19,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	mov	x1,x21
	bl	spin_unlock_irqrestore
	cbz	x22,.out
	mov	x0,x19
	bl	job_notify
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB : SIGCONT envoye ; arrets en attente jetes ; arrete : il repart
; (ses threads arretes redeviennent prets), le pere est averti
sig_cont_proc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x22,0
	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x21,x0
	add	x0,x19,P_LOCK
	bl	spin_lock
	ldr	x0,[x19,P_STATE]
	cbnz	x0,.rendre
	ldr	x20,[x19,P_STOP]
	cbz	x20,.drop
	str	xzr,[x19,P_STOP]
	mov	x0,JEV_CONT
	str	x0,[x19,P_JOBEV]
	mov	x22,1
	dmb	ish
.drop:	ldr	x2,[x19,P_THREADS]
.t:	cbz	x2,.rendre
	add	x3,x2,TH_SIGPEND		; arrets en attente : jetes
	movl	x6,SIG_STOPS
.c:	ldaxr	x4,[x3]
	bic	x4,x4,x6
	stlxr	w5,x4,[x3]
	cbnz	w5,.c
	cbz	x20,.n
	ldr	x0,[x2,TH_STATE]		; arrete (sig_stopped) : pret
	cmp	x0,TS_BLOCKED
	b.ne	.n
	ldr	x0,[x2,TH_WAIT]
	movl	x1,STOP_KEY
	cmp	x0,x1
	b.ne	.n
	str	xzr,[x2,TH_WAIT]
	str	xzr,[x2,TH_STATE]
	str	x2,[sp,-16]!
	ldr	x0,[x2,TH_CPU]
	bl	cpu_kick
	ldr	x2,[sp],16
.n:	ldr	x2,[x2,TH_PNEXT]
	b	.t
.rendre:
	add	x0,x19,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	mov	x1,x21
	bl	spin_unlock_irqrestore
	cbz	x22,.out
	mov	x0,x19
	bl	job_notify
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (exc_return, IRQ masquees, retour en EL0) le process est arrete : le thread
; attend la reprise (P_STOP = 0), sa fin (TH_KILL) ou celle du process
; (proc_kill le passe en TS_DEAD : il ne revient pas)
sig_stopped:
	prologue
	stp	x19,x20,[sp,-16]!
	this_cpu x0
	ldr	x19,[x0,PC_CURRENT]
	ldr	x20,[x19,TH_PROC]
.l:	adr	x0,futex_lock
	bl	spin_lock
	ldr	x0,[x20,P_STOP]
	cbz	x0,.fin
	ldr	x0,[x19,TH_KILL]
	cbnz	x0,.fin
	ldr	x0,[x20,P_STATE]
	cbnz	x0,.fin
	movl	x0,STOP_KEY
	str	x0,[x19,TH_WAIT]
	str	xzr,[x19,TH_WAKE]
	mov	x0,x19
	mov	x1,TS_BLOCKED
	bl	thread_park
	adr	x0,futex_lock
	bl	spin_unlock
	bl	schedule
	adr	x0,futex_lock
	bl	spin_lock
	str	xzr,[x19,TH_WAIT]
	adr	x0,futex_lock
	bl	spin_unlock
	b	.l
.fin:	adr	x0,futex_lock
	bl	spin_unlock
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB d'un fils arrete ou repris : son pere est averti. Ses threads qui
; attendent ce fils (SYS_WAIT) repartent ; SIGCHLD s'il a un gestionnaire
; sans SA_NOCLDSTOP. (Sous proc_lock : le pere ne part pas pendant ce temps.)
job_notify:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldr	x20,[x0,P_PID]
	ldr	x19,[x0,P_PARENT]
	cbz	x19,.out
	adr	x0,proc_lock
	bl	spin_lock_irqsave
	mov	x23,x0
	mov	x0,x19
	bl	proc_lookup
	mov	x19,x0
	cbz	x19,.plibre
	adr	x0,futex_lock
	bl	spin_lock
	add	x0,x19,P_LOCK
	bl	spin_lock
	ldr	x0,[x19,P_STATE]
	cbnz	x0,.rendre
	movl	x0,WAIT_KEY			; ceux qui attendent ce fils
	orr	x20,x20,x0
	ldr	x21,[x19,P_THREADS]
	mov	x24,0				; thread pour SIGCHLD
	mov	x22,0				; a defaut, le premier vivant
.t:	cbz	x21,.sig
	ldr	x0,[x21,TH_STATE]
	cmp	x0,TS_DEAD
	b.eq	.n
	cmp	x22,0
	csel	x22,x21,x22,eq
	cbnz	x24,.w
	ldr	x1,[x21,TH_SIGMASK]
	tbnz	x1,SIGCHLD,.w
	mov	x24,x21
.w:	cmp	x0,TS_BLOCKED
	b.ne	.n
	ldr	x0,[x21,TH_WAIT]
	cmp	x0,x20
	b.ne	.n
	str	xzr,[x21,TH_WAIT]
	str	xzr,[x21,TH_STATE]		; TS_READY
	ldr	x0,[x21,TH_CPU]
	bl	cpu_kick
.n:	ldr	x21,[x21,TH_PNEXT]
	b	.t
.sig:	cmp	x24,0
	csel	x24,x22,x24,eq
	cbz	x24,.rendre
	ldr	x0,[x19,P_SIGTAB]		; un gestionnaire, sans SA_NOCLDSTOP ?
	cbz	x0,.rendre
	mov	x1,SIGCHLD
	mov	x2,SGA_SIZE
	madd	x0,x1,x2,x0
	ldp	x1,x2,[x0,SGA_HANDLER]
	cmp	x1,1
	b.ls	.rendre
	tbnz	x2,0,.rendre			; (SA_NOCLDSTOP)
	mov	x0,x24
	mov	x1,SIGCHLD
	bl	sig_raise
.rendre:
	add	x0,x19,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	bl	spin_unlock
.plibre:
	adr	x0,proc_lock
	mov	x1,x23
	bl	spin_unlock_irqrestore
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = thread choisi par sys_sig_send, x1 = signal (sous futex_lock et P_LOCK)
; -> x0 = 1 : arret par defaut, non masque : rien en attente, l'appelant
; arrete le process (sig_stop_proc) ; 0 : signal mis en attente (sig_raise),
; sauf SIGCONT sans gestionnaire (deja fait par sig_cont_proc : le mettre en
; attente reveillerait un thread endormi, un sleep finirait trop tot)
job_raise:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x20,SIGCONT_K
	b.ne	.stops
	ldr	x0,[x19,TH_PROC]
	ldr	x0,[x0,P_SIGTAB]
	cbz	x0,.rien
	mov	x2,SGA_SIZE
	madd	x0,x20,x2,x0
	ldr	x0,[x0,SGA_HANDLER]
	cmp	x0,1
	b.hi	.raise
.rien:	mov	x0,0
	b	.out
.stops:	mov	x2,1
	lsl	x2,x2,x20
	movl	x3,SIG_STOPS
	tst	x3,x2
	b.eq	.raise
	ldr	x3,[x19,TH_SIGMASK]
	tst	x3,x2
	b.ne	.raise
	ldr	x0,[x19,TH_PROC]		; action par defaut ?
	ldr	x0,[x0,P_SIGTAB]
	cbz	x0,.stop
	mov	x2,SGA_SIZE
	madd	x0,x20,x2,x0
	ldr	x0,[x0,SGA_HANDLER]
	cbnz	x0,.raise
.stop:	mov	x0,1
	b	.out
.raise:	mov	x0,x19
	mov	x1,x20
	bl	sig_raise
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- sig_send(pid > 0, 0, signal | 1 << SIGF_TTY) : signal d'un terminal
; (vterm) pour un lecteur ou un ecrivain en arriere-plan (SIGTTIN, SIGTTOU) ->
; 0 : envoye a son groupe ; 1 : il ignore ou masque le signal (rien envoye) ;
; 2 : son groupe est orphelin (rien envoye ; le terminal rend E_IO)
sig_send_tty:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	and	x21,x2,0xFF
	cmp	x21,SIGTTIN_K
	b.eq	.ok
	cmp	x21,SIGTTOU_K
	b.ne	.inval
.ok:	cmp	x0,0
	b.le	.inval
	bl	proc_lookup
	cbz	x0,.srch
	mov	x19,x0
	mov	x20,1
	add	x0,x19,P_LOCK			; ignore, ou masque par tous ses threads ?
	bl	spin_lock_irqsave
	mov	x22,x0
	ldr	x0,[x19,P_SIGTAB]
	cbz	x0,.mask
	mov	x1,SGA_SIZE
	madd	x0,x21,x1,x0
	ldr	x0,[x0,SGA_HANDLER]
	cmp	x0,1
	b.eq	.rendre
.mask:	ldr	x2,[x19,P_THREADS]
.t:	cbz	x2,.rendre			; (tous le masquent)
	ldr	x3,[x2,TH_STATE]
	cmp	x3,TS_DEAD
	b.eq	.n
	ldr	x3,[x2,TH_SIGMASK]
	lsr	x3,x3,x21
	tbz	x3,0,.send
.n:	ldr	x2,[x2,TH_PNEXT]
	b	.t
.send:	mov	x20,0
.rendre:
	add	x0,x19,P_LOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	cbnz	x20,.res
	ldr	x19,[x19,P_PGID]
	mov	x0,x19
	bl	pg_orphan
	mov	x20,2
	cbnz	x0,.res
	neg	x0,x19
	mov	x1,0
	mov	x2,x21
	bl	sys_sig_send
	cmp	x0,0				; (personne n'a recu : comme ignore,
	cset	x20,ne				;  le client ne redemande pas sans fin)
.res:	mov	x0,x20
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.srch:	mov	x0,E_SRCH
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- setsid() (SETPGID(-1)) -> la session (= pid) : une session et un groupe
; neufs, sans terminal de controle (vterm l'apprend par TIOCSCTTY) ; E_PERM si
; l'appelant mene deja un groupe
sys_setsid:
	current_proc
	ldr	x0,[x1,P_PID]
	ldr	x2,[x1,P_PGID]
	cmp	x2,x0
	b.eq	.perm
	str	x0,[x1,P_SID]
	str	x0,[x1,P_PGID]
	ret
.perm:	mov	x0,E_PERM
	ret

; --- getsid(pid (0 : soi)) (GETPGID(pid | 1 << GETPGID_SID)) -> session
sys_getsid:
	prologue
	and	x0,x0,0xFFFFFFFF
	cbz	x0,.self
	bl	proc_lookup
	cbz	x0,.srch
	b	.got
.self:	current_proc
	mov	x0,x1
.got:	ldr	x0,[x0,P_SID]
	b	.out
.srch:	mov	x0,E_SRCH
.out:	epilogue

; x0 = pid -> x0 = 1 si un process vivant, arrete, est dans le groupe x0
pg_stopped:
	adr	x1,proc_cap
	ldar	x1,[x1]
	adr	x2,proc_tab
	ldr	x2,[x2]
	mov	x3,0
.s:	cmp	x3,x1
	b.hs	.non
	lsl	x4,x3,5
	ldr	x4,[x2,x4]
	add	x3,x3,1
	cbz	x4,.s
	ldr	x5,[x4,P_PGID]
	cmp	x5,x0
	b.ne	.s
	ldr	x5,[x4,P_STATE]
	cbnz	x5,.s
	ldr	x5,[x4,P_STOP]
	cbz	x5,.s
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = groupe : orphelin avec un membre arrete -> SIGHUP puis SIGCONT a tous
job_orphan_hup:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	bl	pg_stopped
	cbz	x0,.out
	mov	x0,x19
	bl	pg_orphan
	cbz	x0,.out
	neg	x0,x19
	mov	x1,0
	mov	x2,SIGHUP_K
	bl	sys_sig_send
	neg	x0,x19
	mov	x1,0
	mov	x2,SIGCONT_K
	bl	sys_sig_send
.out:	ldr	x19,[sp],16
	epilogue

; x0 = PCB d'un process fini (faucheur, retire de la table) : son groupe et
; ceux de ses fils peuvent devenir orphelins (POSIX : SIGHUP, SIGCONT)
job_orphans:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x19,[x0,P_PID]
	ldr	x0,[x0,P_PGID]
	bl	job_orphan_hup
	mov	x20,0
.s:	adr	x0,proc_cap
	ldar	x0,[x0]
	cmp	x20,x0
	b.hs	.out
	adr	x0,proc_tab
	ldr	x0,[x0]
	lsl	x1,x20,5
	ldr	x21,[x0,x1]
	add	x20,x20,1
	cbz	x21,.s
	ldr	x0,[x21,P_PARENT]
	cmp	x0,x19
	b.ne	.s
	ldr	x0,[x21,P_PGID]
	bl	job_orphan_hup
	b	.s
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB d'un fils (sous futex_lock ; SYS_WAIT etendu, x20 = options WEX_*)
; -> x0 = WR_STOPPED / WR_CONTINUED (evenement efface), x1 = statut ; 0 :
; rien. Pour le pere seulement (son PCB tient tant qu'il attend).
wait_event:
	this_cpu x2
	ldr	x2,[x2,PC_CURRENT]
	ldr	x2,[x2,TH_PROC]
	ldr	x2,[x2,P_PID]
	ldr	x3,[x0,P_PARENT]
	cmp	x2,x3
	b.ne	.non
	ldr	x3,[x0,P_JOBEV]
	tbz	x3,8,.cont			; (JEV_STOP)
	tbz	x20,WEXB_UNTRACED,.non
	str	xzr,[x0,P_JOBEV]
	and	x1,x3,0xFF
	mov	x0,WR_STOPPED
	ret
.cont:	tbz	x3,9,.non			; (JEV_CONT)
	tbz	x20,WEXB_CONTINUED,.non
	str	xzr,[x0,P_JOBEV]
	mov	x1,SIGCONT_K
	mov	x0,WR_CONTINUED
	ret
.non:	mov	x0,0
	ret
