; ============================================================================
;  exec.asm - SYS_EXEC (0x6B, docs/ABI.md ; etude docs/TERMINAL-ET-FORK.md,
;  section 4.4) : le process courant change de programme dans le meme PCB.
;
;  Meme pid, meme pere, meme groupe, memes poignees d'IPC (memes numeros) :
;  les services ne voient rien changer (fichiers, bouts de tubes et de pty,
;  sockets, dossier courant restent a ce pid). Change : l'espace d'adressage.
;
;  Deux temps :
;   1. avant le point de non-retour, rien n'est touche : parametres recopies,
;      image trouvee (img_find_ref : celle du chemin resolu et de la taille,
;      construite par AppKit comme pour un lancement) ou ELF recopie (option
;      no_preload : EXP_IMAGE), nouvel espace construit dans un PCB de
;      travail (vm_create, img_clone ou elf_load, stack_setup, arguments).
;      Une erreur : le PCB de travail est rendu, le process continue ;
;   2. ensuite (rien ne peut plus echouer) : les autres threads meurent
;      (exec_seuls : comme thread_kill, ou tout de suite s'ils dorment) et
;      quittent tous les coeurs ; leurs requetes aux services sont
;      abandonnees (svc_orphelins), les mappages de mapcaller finis
;      (mc_quiesce), les chargements d'images en cours rendus ; les champs de
;      l'espace (TTBR et ASID neuf, L1, regions, image) sont echanges avec le
;      PCB de travail, qui part avec l'ancien espace (vm_destroy : pages,
;      regions de memoire partagee, image, ASID et ses TLB sur tous les
;      coeurs). Rendus aussi : tampons partages mappes (P_BUFS), kits mappes
;      (P_KITS : le nouveau programme les rouvre), liste de lancement
;      (P_HSPAWN), file d'evenements (videe) ; fenetres : si le process a
;      parle au serveur graphique (P_WIN, pose par WS_CALL), le service ws
;      recoit l'avis de fin SVC_OP_FIN de ce pid (Elegant ferme ses fenetres,
;      comme a la fin d'un process). Signaux : gestionnaires remis par defaut,
;      ignores gardes, masque et signaux en attente gardes ; pile de signaux
;      et retour (P_SIGRET) oublies.
;  Le thread qui reste repart de la trame de l'appel : entree du programme,
;  pile neuve, x0 = la ligne d'arguments (comme au lancement), le reste a 0,
;  TPIDR_EL0 = 0. Nom du process et du thread : EXP_NOM.
;
;  Poignees : toutes gardees ; les marques sont effacees, puis celles de la
;  liste EXP_MARQUES posees (libposix y met ses descripteurs de canaux et de
;  memoire partagee et l'objet de ses arguments, AppKit son etat : la couche
;  POSIX du nouveau programme les retrouve par HANDLE_LIST). Une poignee
;  encore marquee 0x80000000 (arguments d'un exec ou d'un lancement que
;  l'ancien programme n'a pas lus) et absente de la liste est fermee.
;  Entrees / sorties : EXP_STDIN.. (0 : inchangee, -1 : aucune) ; l'avis
;  SVC_OP_EXEC (a = b = pid, c, d, donnees : les trois) va a vfs, P_STDIO
;  suit.
;
;  Refus : E_PERM pour un process qui a des droits sur le materiel (comme
;  fork : P_DROITS, registres, DMA, ecran) ; exec n'accorde jamais les droits
;  de SD0:/sys (EXP_OPTIONS bit 0 : E_PERM ; AppKit refuse un programme de
;  SD0:/sys, libposix se replie alors sur un lancement). E_BUSY : un autre
;  exec en cours dans ce process. E_NOENT : pas d'image de ce chemin (AppKit
;  la construit et recommence). E_NOEXEC : ELF refuse.
;
;  Verrous : P_LOCK (etat), futex_lock puis P_LOCK (threads), svc_lock,
;  mc_lock, P_VMLOCK (echange), kits_mutex, buf_mutex, la table des poignees
;  (feuille), ws_lock. Jamais deux a la fois, sauf futex_lock -> P_LOCK.
; ============================================================================

EXEC_EN_COURS	= 1			; (P_EXEC) un exec est en cours
EXEC_FAIT	= 2			; (P_EXEC) programme lance par exec (PROC_EXEC)
E_NOEXEC	= -8
E_BUSY_EX	= -16

; pile de sys_exec
EXL_NOM		= 0			; 32 o
EXL_STDIO	= 32			; 24 o
EXL_A_NOM	= 56			; 1 : EXP_NOM donne
EXL_ENTREE	= 64
EXL_BRK		= 72
EXL_PAGES	= 80			; pages de la copie de l'ELF (0 : image)
EXL_ARGS_VA	= 88			; ligne d'arguments chez lui (0 : aucune)
EXL_PILE	= 96			; sommet de sa pile
EXL_CHEMIN	= 112			; IM_PATH_MAX o
EXL_SIZE	= EXL_CHEMIN + IM_PATH_MAX

; --- SYS_EXEC exec(chemin, taille, params) -> (nouveau programme) -------------
sys_exec:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,EXL_SIZE
	mov	x19,x0				; chemin
	mov	x20,x1				; taille
	mov	x21,x2				; params
	mov	x24,0				; ligne d'arguments (page)
	mov	x25,0				; marques (page)
	mov	x26,0				; leur nombre
	mov	x27,0				; PCB de travail
	mov	x28,0				; image (reference) ou copie de l'ELF
	str	xzr,[sp,EXL_PAGES]
	current_proc
	mov	x23,x0				; thread appelant
	mov	x22,x1				; process
	ldr	x0,[x22,P_DROITS]		; droits sur le materiel : refuse
	ldr	x1,[x22,P_DEV_VA]
	orr	x0,x0,x1
	ldr	x1,[x22,P_DMA_VA]
	orr	x0,x0,x1
	ldr	x1,[x22,P_FLAGS_WS]
	orr	x0,x0,x1
	cbnz	x0,.perm
	check_user x21,EXP_SIZE,.fault
	ldr	x0,[x21,EXP_OPTIONS]
	tbnz	x0,0,.perm

	; --- parametres, dans le noyau -----------------------------------------
	strb	wzr,[sp,EXL_NOM]
	str	xzr,[sp,EXL_A_NOM]
	ldr	x0,[x21,EXP_NOM]
	cbz	x0,.nonom
	add	x1,sp,EXL_NOM
	mov	x2,32
	bl	copy_str_from_user
	tbnz	x0,63,.rendre
	mov	x0,1
	str	x0,[sp,EXL_A_NOM]
.nonom:	ldp	x0,x1,[x21,EXP_STDIN]
	stp	x0,x1,[sp,EXL_STDIO]
	ldr	x0,[x21,EXP_STDERR]
	str	x0,[sp,EXL_STDIO+16]
	ldr	x0,[x21,EXP_ARGS]
	cbz	x0,.noargs
	bl	pmm_alloc
	cbz	x0,.nomem
	mov	x24,x0
	ldr	x0,[x21,EXP_ARGS]
	mov	x1,x24
	mov	x2,ARGS_MAX
	bl	copy_str_from_user
	tbnz	x0,63,.rendre
.noargs:
	ldr	x26,[x21,EXP_NMARQUES]
	cbz	x26,.nomarq
	cmp	x26,IPC_INHERIT_MAX
	b.hi	.inval
	ldr	x9,[x21,EXP_MARQUES]
	lsl	x10,x26,3
	check_user x9,x10,.fault
	bl	pmm_alloc
	cbz	x0,.nomem
	mov	x25,x0
	ldr	x1,[x21,EXP_MARQUES]
	lsl	x2,x26,3
	bl	mem_copy
	mov	x0,x22
	mov	x1,x25
	mov	x2,x26
	bl	exec_marques_ok
	cbz	x0,.badf
.nomarq:

	; --- l'image -------------------------------------------------------------
	ldr	x0,[x21,EXP_IMAGE]
	cbnz	x0,.copie
	mov	x0,x19
	add	x1,sp,EXL_CHEMIN
	mov	x2,IM_PATH_MAX
	bl	copy_str_from_user
	tbnz	x0,63,.rendre
	add	x0,sp,EXL_CHEMIN		; une reference pour nous
	mov	x1,x20
	mov	x2,0
	bl	img_find_ref
	cbz	x0,.noent
	mov	x28,x0
	b	.espace
.copie:	cmp	x20,64				; (option no_preload) l'ELF recopie
	b.lo	.inval
	movl	x0,SPAWN_IMAGE_MAX
	cmp	x20,x0
	b.hi	.inval
	ldr	x9,[x21,EXP_IMAGE]
	check_user x9,x20,.fault
	add	x0,x20,0xFFF
	lsr	x0,x0,12
	str	x0,[sp,EXL_PAGES]
	bl	pmm_alloc_n
	cbz	x0,.nomem_p
	mov	x28,x0
	ldr	x1,[x21,EXP_IMAGE]
	mov	x2,x20
	bl	mem_copy
	mov	x0,x28
	mov	x1,x20
	bl	elf_check
	cbz	x0,.noexec

	; --- le nouvel espace, dans un PCB de travail -------------------------------
.espace:
	bl	vm_create
	cbz	x0,.nomem
	mov	x27,x0
	ldr	x0,[sp,EXL_PAGES]
	cbnz	x0,.elf
	str	x28,[x27,P_IMAGE]		; (la reference passe au PCB de travail)
	mov	x1,x28
	mov	x28,0
	mov	x0,x27
	bl	img_clone			; pages partagees, copie sur ecriture
	cbz	x0,.nomem
	ldr	x1,[x27,P_IMAGE]
	ldr	x0,[x1,IM_ENTRY]
	str	x0,[sp,EXL_ENTREE]
	ldr	x0,[x1,IM_BRK]
	str	x0,[sp,EXL_BRK]
	b	.pile
.elf:	mov	x0,x27
	mov	x1,x28
	mov	x2,x20
	bl	elf_load
	cbz	x0,.noexec
	str	x1,[sp,EXL_BRK]
	ldr	x0,[x28,24]			; e_entry
	str	x0,[sp,EXL_ENTREE]
	mov	x0,x28				; copie rendue
	ldr	x1,[sp,EXL_PAGES]
	bl	pmm_free_n
	mov	x28,0
	str	xzr,[sp,EXL_PAGES]
.pile:	mov	x0,x27
	bl	stack_setup
	cbz	x0,.nomem
	ldr	x0,[sp,EXL_BRK]
	add	x0,x0,0xFFF
	and	x0,x0,not 0xFFF
	str	x0,[sp,EXL_BRK]
	movl	x0,USER_STACK_TOP		; arguments en haut de pile
	str	x0,[sp,EXL_PILE]
	str	xzr,[sp,EXL_ARGS_VA]
	cbz	x24,.code
	sub	x3,x0,ARGS_MAX
	str	x3,[sp,EXL_PILE]
	str	x3,[sp,EXL_ARGS_VA]
	mov	x2,0				; longueur (bornee) + 0 final
.alen:	ldrb	w1,[x24,x2]
	add	x2,x2,1
	cbz	w1,.acopy
	cmp	x2,ARGS_MAX-1
	b.lo	.alen
.acopy:	mov	x0,x27
	mov	x1,x24
	bl	vm_copy_to_user
.code:	ic	ialluis				; code neuf : caches I de tous les coeurs
	dsb	ish
	isb

	; --- point de non-retour : un seul exec a la fois ---------------------------
	add	x0,x22,P_LOCK
	bl	spin_lock_irqsave
	mov	x1,x0
	ldr	x2,[x22,P_EXEC]
	ldr	x3,[x22,P_STATE]
	orr	x3,x3,x2
	tbnz	x3,0,.occupe			; (en fin de vie, ou exec en cours)
	orr	x2,x2,EXEC_EN_COURS
	str	x2,[x22,P_EXEC]
	str	x27,[x22,P_EXECNEW]		; (tue en route : vm_destroy le rend)
	add	x0,x22,P_LOCK
	bl	spin_unlock_irqrestore
	mov	x0,x22
	mov	x1,x23
	bl	exec_seuls			; les autres threads meurent
	cbz	x0,.fin_de_vie
	ldr	x0,[x22,P_PID]			; leurs requetes : sans appelant
	bl	svc_orphelins
	mov	x0,x22				; mapcaller : plus rien sur ses pages
	bl	mc_quiesce
	mov	x0,x22				; chargements d'images en cours
	bl	img_loader_gone

	; --- echange des espaces -------------------------------------------------------
	add	x0,x22,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x9,x0
	ldp	x0,x1,[x22,P_TTBR]		; P_TTBR, P_L1
	ldp	x2,x3,[x27,P_TTBR]
	stp	x2,x3,[x22,P_TTBR]
	stp	x0,x1,[x27,P_TTBR]
	ldp	x0,x1,[x22,P_VMA]		; P_VMA, P_VMA_N
	ldp	x2,x3,[x27,P_VMA]
	stp	x2,x3,[x22,P_VMA]
	stp	x0,x1,[x27,P_VMA]
	ldp	x0,x1,[x22,P_VMA_CAP]		; P_VMA_CAP, P_IMAGE
	ldp	x2,x3,[x27,P_VMA_CAP]
	stp	x2,x3,[x22,P_VMA_CAP]
	stp	x0,x1,[x27,P_VMA_CAP]
	stp	xzr,xzr,[x22,P_FAULTS]		; P_FAULTS, P_COWS
	str	xzr,[x22,P_VM_OOM]
	ldr	x0,[x22,P_TTBR]			; l'espace neuf, ici (ASID neuf : TLB
	msr	ttbr0_el1,x0			;  propres ; l'ancien est invalide sur
	isb					;  tous les coeurs par asid_free)
	add	x0,x22,P_VMLOCK
	mov	x1,x9
	bl	spin_unlock_irqrestore

	; --- tas, signaux ------------------------------------------------------------
	add	x0,x22,P_LOCK
	bl	spin_lock_irqsave
	mov	x9,x0
	ldr	x0,[sp,EXL_BRK]
	stp	x0,x0,[x22,P_BRK]		; P_BRK, P_BRK_START
	str	xzr,[x22,P_SIGRET]
	str	xzr,[x22,P_EXECNEW]
	ldr	x0,[x22,P_SIGTAB]		; gestionnaires -> defaut ; ignores
	cbz	x0,.sigok			; gardes
	mov	x1,1				; (de 1 a SIG_MAX - 1)
.sig:	mov	x2,SGA_SIZE
	madd	x2,x1,x2,x0
	ldr	x3,[x2,SGA_HANDLER]
	cmp	x3,1
	b.ls	.sign
	stp	xzr,xzr,[x2,SGA_HANDLER]
	str	xzr,[x2,SGA_MASK]
.sign:	add	x1,x1,1
	cmp	x1,SIG_MAX
	b.lo	.sig
.sigok:	add	x0,x22,P_LOCK
	mov	x1,x9
	bl	spin_unlock_irqrestore

	; --- kits, tampons partages : ceux de l'ancien programme --------------------
	adr	x0,kits_mutex
	bl	mutex_lock
	str	xzr,[x22,P_KITS]
	adr	x0,kits_mutex
	bl	mutex_unlock
	ldr	x0,[x22,P_BUFS]
	ldr	x1,[x22,P_BUFS+8]
	orr	x0,x0,x1
	cbz	x0,.bufok
	adr	x0,buf_mutex
	bl	mutex_lock
	mov	x9,0
.buf:	str	x9,[sp,-16]!
	mov	x0,x22
	mov	x1,x9
	mov	x2,0				; (l'ancien espace part entier)
	bl	buf_release
	ldr	x9,[sp],16
	add	x9,x9,1
	cmp	x9,BUF_MAX
	b.lo	.buf
	adr	x0,buf_mutex
	bl	mutex_unlock
.bufok:	mov	x0,x27				; l'ancien espace : pages, regions,
	bl	vm_destroy			; image, ASID (TLB), PCB de travail
	mov	x27,0
	add	x0,x22,P_HSPAWN			; liste de lancement de l'ancien
	mov	x1,0
	bl	ixchg
	bl	ilist_release
	bl	ipc_reap
	mov	x0,x22				; marques des poignees
	mov	x1,x25
	mov	x2,x26
	bl	exec_marquer
	mov	x0,x22				; file d'evenements, fenetres
	bl	exec_ws

	; --- thread, nom, entrees / sorties ------------------------------------------
	msr	tpidr_el0,xzr
	str	xzr,[x23,TH_TPIDR]
	str	xzr,[x23,TH_ALT_SP]		; pile des signaux oubliee
	str	xzr,[x23,TH_ALT_SIZE]
	str	xzr,[x23,TH_SIGFAULT]
	str	xzr,[x23,TH_SIGOLDF]
	ldr	x0,[sp,EXL_A_NOM]
	cbz	x0,.stdio
	add	x0,x22,P_NAME
	add	x1,sp,EXL_NOM
	mov	x2,31
	bl	str_ncopy
	add	x0,x23,TH_NAME
	add	x1,sp,EXL_NOM
	mov	x2,THREAD_NAME_MAX-1
	bl	str_ncopy
.stdio:	mov	x0,x22
	add	x1,sp,EXL_STDIO
	bl	exec_stdio
	add	x0,x22,P_LOCK
	bl	spin_lock_irqsave
	mov	x1,x0
	mov	x0,EXEC_FAIT
	str	x0,[x22,P_EXEC]
	add	x0,x22,P_LOCK
	bl	spin_unlock_irqrestore

	; --- trame de l'appel : le nouveau programme ----------------------------------
	movl	x0,THREAD_BLOCK-FRAME_SIZE
	add	x0,x23,x0
	mov	x1,FR_ELR			; x0..x30, sp_el0 a zero
	bl	mem_zero
	movl	x0,THREAD_BLOCK-FRAME_SIZE
	add	x0,x23,x0
	ldr	x1,[sp,EXL_PILE]
	str	x1,[x0,FR_X30+8]		; sp_el0
	ldr	x1,[sp,EXL_ENTREE]
	stp	x1,xzr,[x0,FR_ELR]		; ELR, SPSR = EL0t, IRQ permises
	ldr	x19,[sp,EXL_ARGS_VA]		; x0 = la ligne (rendu par
	b	.rendre_ok			;  syscall_dispatch dans FR_X0)

.fin_de_vie:					; tue pendant l'attente (exit d'un
	str	xzr,[x22,P_EXECNEW]		; autre thread) : rien n'est fait
	mov	x19,E_INTR
	b	.libere
.occupe:
	add	x0,x22,P_LOCK
	bl	spin_unlock_irqrestore
	mov	x19,E_BUSY_EX
	b	.libere
.noexec:
	mov	x19,E_NOEXEC
	b	.libere
.nomem_p:
	str	xzr,[sp,EXL_PAGES]
.nomem:	mov	x19,E_NOMEM
	b	.libere
.noent:	mov	x19,E_NOENT
	b	.libere
.badf:	mov	x19,E_BADF
	b	.libere
.inval:	mov	x19,E_INVAL
	b	.libere
.fault:	mov	x19,E_FAULT
	b	.libere
.perm:	mov	x19,E_PERM
	b	.libere
.rendre:					; x0 = erreur de copy_str_from_user
	mov	x19,x0
.libere:					; erreur : le process reste intact
	cbz	x27,.l1
	mov	x0,x27				; (rend aussi l'image)
	bl	vm_destroy
.l1:	cbz	x28,.rendre_ok
	ldr	x1,[sp,EXL_PAGES]
	mov	x0,x28
	cbz	x1,.l2
	bl	pmm_free_n			; copie de l'ELF
	b	.rendre_ok
.l2:	bl	image_unref			; reference de l'image
.rendre_ok:
	cbz	x24,.r1
	mov	x0,x24
	bl	pmm_free
.r1:	cbz	x25,.r2
	mov	x0,x25
	bl	pmm_free
.r2:	mov	x0,x19
	add	sp,sp,EXL_SIZE
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = thread qui reste (le courant) : les autres meurent. Comme
; thread_kill (TH_KILL : mort au retour en EL0 ; bloques dans futex, join,
; wait, IPC : reveilles, ils y vont) ; endormis (sleep, requete a un service,
; evenements) : morts tout de suite (TS_DEAD, comme proc_kill : ni verrou ni
; attente IPC a eux). Attend que l'ordonnanceur de leur coeur les ait rendus
; (P_NTHREADS = 1 : plus aucun coeur ne charge l'ancien espace pour eux)
; -> x0 = 1, 0 si le process finit (exit d'un autre thread)
exec_seuls:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.tour:	adr	x0,futex_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	add	x0,x19,P_LOCK
	bl	spin_lock
	ldr	x0,[x19,P_STATE]
	cbnz	x0,.mort
	ldr	x21,[x19,P_THREADS]
.t:	cbz	x21,.fin
	cmp	x21,x20
	b.eq	.n
	str	xzr,[x21,TH_KILL_CODE]
	dmb	ish
	mov	x0,1
	str	x0,[x21,TH_KILL]
	ldr	x0,[x21,TH_STATE]
	cmp	x0,TS_BLOCKED
	b.ne	.dort
	str	xzr,[x21,TH_WAIT]		; reveille : il voit TH_KILL
	str	xzr,[x21,TH_STATE]
	b	.kick
.dort:	cmp	x0,TS_SLEEPING
	b.ne	.kick
	mov	x0,TS_DEAD
	str	x0,[x21,TH_STATE]
.kick:	ldr	x0,[x21,TH_CPU]
	bl	cpu_kick
.n:	ldr	x21,[x21,TH_PNEXT]
	b	.t
.fin:	ldr	x21,[x19,P_NTHREADS]
	add	x0,x19,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	cmp	x21,1
	b.ls	.ok
	mov	x0,1
	bl	thread_sleep
	b	.tour
.ok:	mov	x0,1
	b	.out
.mort:	add	x0,x19,P_LOCK
	bl	spin_unlock
	adr	x0,futex_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = liste {poignee, marque} (noyau), x2 = nombre -> x0 = 1 si
; chaque poignee est ouverte
exec_marques_ok:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	ldr	x19,[x0,P_HANDLES]
	mov	x0,0
	cbz	x19,.out
	mov	x0,x19
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x9,0
	mov	x10,1
.e:	cmp	x9,x21
	b.hs	.unl
	add	x1,x20,x9,lsl 3
	ldr	w1,[x1]
	add	x9,x9,1
	sub	x2,x1,1
	cmp	x2,IHT_MAX-1
	b.hs	.non
	add	x2,x19,x1,lsl 4
	ldr	x2,[x2,IHE_OBJ]
	cbnz	x2,.e
.non:	mov	x10,0
.unl:	mov	x0,x19
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x10
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB (un seul thread), x1 = liste {poignee, marque}, x2 = nombre :
; les poignees marquees 0x80000000 hors de la liste sont fermees, les autres
; marques effacees, puis celles de la liste posees
exec_marquer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	ldr	x19,[x0,P_HANDLES]
	cbz	x19,.out
.ferme:	mov	x0,x19				; une a la fois (rendue hors du verrou)
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x23,0				; objet a rendre
	mov	x9,1
.h:	cmp	x9,IHT_MAX
	b.hs	.hf
	add	x10,x19,x9,lsl 4
	ldr	x0,[x10,IHE_OBJ]
	cbz	x0,.hn
	ldr	w1,[x10,IHE_TAG]
	tbz	w1,31,.efface
	mov	x11,0				; dans la liste ?
.l:	cmp	x11,x21
	b.hs	.pas
	add	x2,x20,x11,lsl 3
	ldr	w2,[x2]
	add	x11,x11,1
	cmp	x2,x9
	b.ne	.l
	b	.efface
.pas:	mov	x23,x0
	str	xzr,[x10,IHE_OBJ]
	str	wzr,[x10,IHE_TAG]
	b	.hf
.efface:
	str	wzr,[x10,IHE_TAG]
.hn:	add	x9,x9,1
	b	.h
.hf:	cbnz	x23,.rend
	mov	x9,0				; plus rien a fermer : la liste
.m:	cmp	x9,x21
	b.hs	.mf
	add	x10,x20,x9,lsl 3
	ldp	w1,w2,[x10]
	add	x9,x9,1
	sub	x3,x1,1
	cmp	x3,IHT_MAX-1
	b.hs	.m
	add	x3,x19,x1,lsl 4
	ldr	x4,[x3,IHE_OBJ]
	cbz	x4,.m
	str	w2,[x3,IHE_TAG]
	b	.m
.mf:	mov	x0,x19
	mov	x1,x22
	bl	spin_unlock_irqrestore
	b	.out
.rend:	mov	x0,x19
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x23
	bl	iobj_unref
	bl	ipc_reap
	b	.ferme
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB : file d'evenements videe ; s'il a parle au serveur graphique
; (P_WIN), avis de fin de ce pid au service ws (ses fenetres sont fermees)
exec_ws:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,WS_MSG
	mov	x19,x0
	ws_lock_take
	ldr	x0,[x19,P_EVQ]
	cbz	x0,.vide
	ldr	x1,[x0]				; queue (lecture) = tete (ecriture)
	str	x1,[x0,8]
.vide:	str	xzr,[x19,P_EVENTS]
	ws_lock_drop
	ldr	x0,[x19,P_WIN]
	cbz	x0,.out
	str	xzr,[x19,P_WIN]
	adr	x0,ws_server
	ldr	x0,[x0]
	cbz	x0,.out
	mov	x0,sp
	mov	x1,WS_MSG
	bl	mem_zero
	movl	x0,SVC_OP_FIN
	ldr	x1,[x19,P_PID]
	stp	x0,x1,[sp]			; op, a = pid (b = code : 0)
	adr	x0,ws_canal
	ldr	x0,[x0]
	mov	x1,sp
	bl	svc_poster_k
	cbz	x0,.out
	adr	x0,msg_exec_ws			; (file pleine : fenetres gardees)
	bl	kputs
.out:	add	sp,sp,WS_MSG
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = entree, sortie, erreur (3 x 8 o, noyau ; 0 : inchangee, -1 :
; aucune) : P_STDIO, puis l'avis SVC_OP_EXEC a vfs s'il y a un changement
exec_stdio:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,WS_MSG
	mov	x19,x0
	mov	x20,x1
	ldr	x0,[x19,P_STDIO]
	mov	x21,0				; un changement ?
	mov	x2,0
.s:	ldr	x3,[x20,x2,lsl 3]
	cbz	x3,.sn
	mov	x21,1
	mov	x4,1
	lsl	x4,x4,x2
	bic	x0,x0,x4
	cmn	x3,1
	b.eq	.sn
	orr	x0,x0,x4
.sn:	add	x2,x2,1
	cmp	x2,3
	b.lo	.s
	and	x0,x0,7
	str	x0,[x19,P_STDIO]
	cbz	x21,.out
	adr	x0,exec_nom_vfs
	bl	svc_par_nom
	tbnz	x0,63,.out
	mov	x22,x0
	mov	x0,sp
	mov	x1,WS_MSG
	bl	mem_zero
	movl	x0,SVC_OP_EXEC
	ldr	x1,[x19,P_PID]
	stp	x0,x1,[sp]			; op, a
	ldp	x2,x3,[x20]
	stp	x1,x2,[sp,16]			; b = pid, c = entree
	str	x3,[sp,32]			; d = sortie
	ldr	x2,[x20,16]
	str	x2,[sp,48]			; donnees : erreur
	mov	w2,8
	str	w2,[sp,40]
	mov	x0,x22
	mov	x1,sp
	bl	svc_poster_k
	cbz	x0,.out
	adr	x0,msg_exec_vfs
	bl	kputs
.out:	add	sp,sp,WS_MSG
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

exec_nom_vfs	db 'vfs',0
msg_exec_ws	db '[exec] file des services pleine : avis a ws perdu',10,0
msg_exec_vfs	db '[exec] file des services pleine : avis a vfs perdu',10,0
		align 4
