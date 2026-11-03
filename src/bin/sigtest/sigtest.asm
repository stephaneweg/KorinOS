; ============================================================================
;  sigtest - essai des signaux du noyau (src/kernel/signal.asm, docs/ABI.md)
;  et de membarrier, sans libposix. Chaque essai affiche son nom s'il echoue ;
;  sortie : « sigtest : PASS / FAIL (n) ».
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

ST_SIG		= 30			; (SIGUSR1 de newlib)
ST_CHLD		= 20			; (SIGCHLD de newlib)
SIGILL		= 4
SIGSEGV		= 11
SIGFPE		= 8
E_INTR		= -4
BAD_ADDR	= 0x10			; hors de l'espace du programme
ALT_SIZE	= 16384
TH_STACK	= 32768
TH_NORMAL	= 0

macro check cond*, name* {
	b.#cond	@f
	adr	x0,name
	bl	failed
@@:
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	bl	kits_open
	adr	x20,seen			; compteurs
	sys	SYS_THREAD_SELF
	adr	x1,main_tid
	str	x0,[x1]

	; --- un gestionnaire, le signal envoye a soi --------------------------
	mov	x0,ST_SIG
	adr	x1,on_count
	mov	x2,0
	mov	x3,0
	adr	x4,sig_return
	sys	SYS_SIG_ACTION
	cmp	x0,0
	check	eq,t_action
	mov	x0,0
	mov	x1,0
	mov	x2,ST_SIG
	sys	SYS_SIG_SEND
	ldr	x0,[x20,S_COUNT]
	cmp	x0,1
	check	eq,t_self
	ldr	x0,[x20,S_ARG]
	cmp	x0,ST_SIG
	check	eq,t_arg

	; --- masque : en attente, puis livre au demasquage --------------------
	mov	x0,SIGM_BLOCK
	mov	x1,1 shl ST_SIG
	sys	SYS_SIG_MASK
	mov	x0,0
	mov	x1,0
	mov	x2,ST_SIG
	sys	SYS_SIG_SEND
	ldr	x0,[x20,S_COUNT]
	cmp	x0,1
	check	eq,t_masked
	mov	x0,SIGM_PENDING
	mov	x1,0
	sys	SYS_SIG_MASK
	tst	x0,1 shl ST_SIG
	check	ne,t_pending
	mov	x0,SIGM_UNBLOCK
	mov	x1,1 shl ST_SIG
	sys	SYS_SIG_MASK
	ldr	x0,[x20,S_COUNT]
	cmp	x0,2
	check	eq,t_unmask

	; --- faute : adresse, registres et pc changes par l'ucontext -----------
	mov	x0,SIGSEGV
	adr	x1,on_segv
	mov	x2,0
	mov	x3,0
	adr	x4,sig_return
	sys	SYS_SIG_ACTION
	mov	x19,0x55
	mov	x1,BAD_ADDR
	movi	v8.16b,0x5A			; (le gestionnaire les ecrase)
	ldr	x0,[x1]				; faute : l'instruction est sautee
	ldr	x0,[x20,S_ADDR]
	cmp	x0,BAD_ADDR
	check	eq,t_addr
	cmp	x19,0x66			; x19 change par le gestionnaire
	check	eq,t_regs
	umov	w0,v8.b[3]
	cmp	w0,0x5A
	check	eq,t_fp

	; --- instruction illegale ----------------------------------------------
	mov	x0,SIGILL
	adr	x1,on_ill
	mov	x2,0
	mov	x3,0
	adr	x4,sig_return
	sys	SYS_SIG_ACTION
	dw	0				; (UDF)
	ldr	x0,[x20,S_ILL]
	cmp	x0,1
	check	eq,t_ill

	; --- debordement de pile : sur la pile de signaux ----------------------
	adr	x0,alt_stack
	mov	x1,ALT_SIZE
	mov	x2,0
	sys	SYS_SIG_ALTSTACK
	mov	x0,SIGSEGV
	adr	x1,on_stack
	mov	x2,SA_ONSTACK
	mov	x3,0
	adr	x4,sig_return
	sys	SYS_SIG_ACTION
	mov	x21,sp
	mov	x0,BAD_ADDR
	mov	sp,x0
	str	xzr,[sp]			; faute : la pile est perdue
	mov	sp,x21
	ldr	x0,[x20,S_ALT]
	cmp	x0,1
	check	eq,t_alt

	; --- signal vers un thread precis, sommeil et attente interrompus -----
	str	xzr,[x20,S_LAST]
	adr	x0,worker
	adr	x1,th_stack+TH_STACK
	mov	x2,0
	sys	SYS_THREAD_CREATE
	mov	x22,x0
	tbnz	x0,63,.nothr
.ready:	ldr	x0,[x20,S_READY]		; il tourne
	cbz	x0,.ready
	mov	x0,0
	mov	x1,x22
	mov	x2,ST_SIG
	sys	SYS_SIG_SEND
	cmp	x0,0
	check	eq,t_tid
.tok:	ldr	x0,[x20,S_TID]
	cbz	x0,.tok
	cmp	x0,x22
	check	eq,t_tid2
	mov	x0,0				; pas un thread inconnu
	movl	x1,99999
	mov	x2,ST_SIG
	sys	SYS_SIG_SEND
	cmp	x0,-3
	check	eq,t_srch

	; --- membarrier, un thread tourne sur un autre coeur -------------------
	mov	x0,MB_QUERY
	mov	x1,0
	sys	SYS_MEMBARRIER
	mov	x1,MB_KNOWN
	cmp	x0,x1
	check	eq,t_mbq
	mov	x0,MB_REGISTER
	mov	x1,0
	sys	SYS_MEMBARRIER
	cmp	x0,0
	check	eq,t_mbr
	mov	x21,1000
.mb:	mov	x0,MB_PRIVATE
	mov	x1,0
	sys	SYS_MEMBARRIER
	cmp	x0,0
	check	eq,t_mb
	subs	x21,x21,1
	b.ne	.mb
	mov	x0,1				; le thread s'arrete de tourner
	str	x0,[x20,S_STOP]

	; --- SIGM_SUSPEND : son signal, envoye par le thread ------------------
	mov	x0,SIGM_BLOCK
	mov	x1,1 shl ST_SIG
	sys	SYS_SIG_MASK
	mov	x21,x0				; ancien masque
	mov	x0,1
	str	x0,[x20,S_GO]
	mov	x0,SIGM_SUSPEND
	mov	x1,0
	sys	SYS_SIG_MASK
	cmp	x0,E_INTR
	check	eq,t_susp
	ldr	x0,[x20,S_COUNT]
	cmp	x0,4				; (2 + le thread + celui-ci)
	check	eq,t_susp2
	mov	x0,SIGM_SET			; masque revenu : encore bloque
	mov	x1,x21
	sys	SYS_SIG_MASK
	tst	x0,1 shl ST_SIG
	check	ne,t_susp3

	; --- sommeil interrompu ------------------------------------------------
	mov	x0,2
	str	x0,[x20,S_GO]
	movl	x0,5000
	sys	SYS_SLEEP
	cmp	x0,E_INTR
	check	eq,t_sleep

	; --- SIGM_WAIT ----------------------------------------------------------
	mov	x0,SIGM_BLOCK
	mov	x1,1 shl ST_SIG
	sys	SYS_SIG_MASK
	mov	x0,0
	mov	x1,0
	mov	x2,ST_SIG
	sys	SYS_SIG_SEND
	mov	x0,SIGM_WAIT
	mov	x1,1 shl ST_SIG
	sys	SYS_SIG_MASK
	cmp	x0,ST_SIG
	check	eq,t_wait

	; --- SIGCHLD : avec gestionnaire, puis sans (rien ne doit l'interrompre) --
	mov	x0,SIGM_UNBLOCK
	mov	x1,1 shl ST_SIG
	sys	SYS_SIG_MASK
	mov	x0,ST_CHLD
	adr	x1,on_count
	mov	x2,0
	mov	x3,0
	adr	x4,sig_return
	sys	SYS_SIG_ACTION
	str	xzr,[x20,S_COUNT]
	str	xzr,[x20,S_ARG]
	adr	x0,p_fils
	mov	x1,0
	sys	SYS_SPAWN
	tbnz	x0,63,.nofils
	sys	SYS_WAIT
	mov	x19,50
.cw:	ldr	x0,[x20,S_COUNT]
	cbnz	x0,.cok
	mov	x0,10
	sys	SYS_SLEEP
	subs	x19,x19,1
	b.ne	.cw
.cok:	ldr	x0,[x20,S_ARG]
	cmp	x0,ST_CHLD
	check	eq,t_chld
	mov	x0,ST_CHLD
	mov	x1,0
	mov	x2,0
	mov	x3,0
	adr	x4,sig_return
	sys	SYS_SIG_ACTION
	adr	x0,p_fils
	mov	x1,0
	sys	SYS_SPAWN
	tbnz	x0,63,.nofils
	mov	x19,x0
	mov	x0,300
	sys	SYS_SLEEP
	cmp	x0,E_INTR
	check	ne,t_chld2
	mov	x0,x19
	sys	SYS_WAIT
	b	.end
.nofils:
	adr	x0,t_chld
	bl	failed
	b	.end
.nothr:	adr	x0,t_thread
	bl	failed
.end:	adr	x0,m_pass
	ldr	x19,[x20,S_FAILS]
	cbz	x19,.say
	adr	x0,m_fail
.say:	bl	puts
	mov	x0,x19
	sys	SYS_EXIT

; (gestionnaires : signal, siginfo, ucontext ; retour par sig_return, x30)
on_count:
	adr	x9,seen
	str	x0,[x9,S_ARG]
	mov	x1,1
	add	x2,x9,S_COUNT
.a:	ldaxr	x3,[x2]
	add	x3,x3,x1
	stlxr	w4,x3,[x2]
	cbnz	w4,.a
	sys	SYS_THREAD_SELF
	adr	x9,seen
	str	x0,[x9,S_LAST]
	ret

on_segv:
	adr	x9,seen
	ldr	x3,[x1,SIF_ADDR]
	str	x3,[x9,S_ADDR]
	mov	x3,0x66
	str	x3,[x2,UCX_REGS+19*8]
	ldr	x3,[x2,UCX_PC]			; apres l'instruction fautive
	add	x3,x3,4
	str	x3,[x2,UCX_PC]
	movi	v8.16b,0x11
	ret

on_ill:
	adr	x9,seen
	mov	x3,1
	str	x3,[x9,S_ILL]
	ldr	x3,[x2,UCX_PC]
	add	x3,x3,4
	str	x3,[x2,UCX_PC]
	ret

on_stack:
	adr	x9,seen
	adr	x3,alt_stack			; sur la pile de signaux ?
	mov	x4,sp
	sub	x4,x4,x3
	cmp	x4,ALT_SIZE
	b.hs	.no
	mov	x3,1
	str	x3,[x9,S_ALT]
.no:	ldr	x3,[x2,UCX_PC]
	add	x3,x3,4
	str	x3,[x2,UCX_PC]
	ret

sig_return:
	sys	SYS_SIG_RETURN

; le thread d'essai : tourne en attendant, puis envoie les signaux demandes
worker:
	adr	x20,seen
	mov	x0,1
	str	x0,[x20,S_READY]
.spin:	ldr	x0,[x20,S_LAST]			; signal recu ici ?
	cbz	x0,.spin
	str	x0,[x20,S_TID]
.turn:	ldr	x0,[x20,S_STOP]
	cbz	x0,.turn
.go1:	ldr	x0,[x20,S_GO]
	cbz	x0,.go1
	mov	x0,5
	sys	SYS_SLEEP
	mov	x0,0				; le thread principal
	mov	x2,ST_SIG
	adr	x3,main_tid
	ldr	x1,[x3]
	sys	SYS_SIG_SEND
.go2:	ldr	x0,[x20,S_GO]
	cmp	x0,2
	b.ne	.go2
	mov	x0,20
	sys	SYS_SLEEP
	mov	x0,0
	adr	x3,main_tid
	ldr	x1,[x3]
	mov	x2,ST_SIG
	sys	SYS_SIG_SEND
	mov	x0,0
	sys	SYS_THREAD_EXIT

; x0 = nom de l'essai
failed:
	prologue
	str	x0,[sp,-16]!
	adr	x0,m_failed
	bl	puts
	ldr	x0,[sp],16
	bl	puts
	adr	x0,m_nl
	bl	puts
	adr	x1,seen
	ldr	x0,[x1,S_FAILS]
	add	x0,x0,1
	str	x0,[x1,S_FAILS]
	epilogue

segment readable writeable
kit_data
		align 8
S_COUNT		= 0
S_ARG		= 8
S_ADDR		= 16
S_ILL		= 24
S_ALT		= 32
S_READY		= 40
S_LAST		= 48
S_TID		= 56
S_STOP		= 64
S_GO		= 72
S_FAILS		= 80
seen		rb 96
main_tid	dd 0
m_failed	db 'sigtest : echec : ',0
m_nl		db 10,0
m_pass		db 'sigtest : PASS',10,0
m_fail		db 'sigtest : FAIL',10,0
t_action	db 'sig_action',0
t_self		db 'signal a soi',0
t_arg		db 'argument du gestionnaire',0
t_masked	db 'signal masque livre',0
t_pending	db 'signal masque pas en attente',0
t_unmask	db 'demasquage',0
t_addr		db 'adresse de la faute',0
t_regs		db 'registre change par ucontext',0
t_fp		db 'FP rendu apres le gestionnaire',0
t_ill		db 'instruction illegale',0
t_alt		db 'pile de signaux',0
t_thread	db 'thread_create',0
t_tid		db 'envoi a un thread',0
t_tid2		db 'signal recu par le bon thread',0
t_srch		db 'thread inconnu',0
t_mbq		db 'membarrier query',0
t_mbr		db 'membarrier register',0
t_mb		db 'membarrier private',0
t_susp		db 'sigsuspend',0
t_susp2		db 'sigsuspend : gestionnaire',0
t_susp3		db 'sigsuspend : masque rendu',0
t_sleep		db 'sommeil interrompu',0
t_wait		db 'sigwait',0
t_chld		db 'SIGCHLD',0
t_chld2		db 'SIGCHLD sans gestionnaire',0
p_fils		db 'SD0:/bin/echo',0
		align 16
alt_stack	rb ALT_SIZE
		align 16
th_stack	rb TH_STACK
