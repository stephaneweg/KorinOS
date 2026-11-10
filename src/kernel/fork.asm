; ============================================================================
;  fork.asm - SYS_FORK (0x6A, docs/ABI.md ; etude docs/TERMINAL-ET-FORK.md,
;  section 4.1) : le process courant est recopie dans un fils.
;
;  Copie immediate (decision du 10/11/2026, copie sur ecriture ensuite) :
;    * chaque page privee de la moitie EL0 est recopiee dans une page neuve,
;      memes attributs (code : cache D nettoye, cache I invalide) ;
;    * une page PTE_SHARED est mappee telle quelle, avec la reference de son
;      objet : image prechargee (P_IMAGE, IM_REFS), code d'un kit (P_KITS, les
;      kits ne sont jamais rendus), memoire partagee (une reference par region
;      VMK_SHARED, ISH_MAPS), tampons partages (P_BUFS, BF_REFS : remappes par
;      buf_map_into). Une page epinglee par mapcaller (PTE_SHARED pose sur une
;      page a lui) n'appartient a aucun objet : elle est recopiee. Ce qui la
;      distingue : l'adresse physique (code du kit, page du modele de l'image
;      a la meme adresse), ou PTE_COW dans une region VMK_SHARED en VM_COW ;
;    * fenetres de chargement d'images (LOADER_WIN), tampons (refaits par
;      buf_map_into), framebuffer, mapcaller, registres, DMA (L1[416..495]) :
;      pas recopies ;
;    * table des regions, poignees d'IPC (memes numeros, iobj_ref), actions
;      des signaux recopiees ; tas (P_BRK), nom, kits, P_STDIO, P_SIGRET ;
;      P_PARENT = le pere ; file d'evenements, fenetre, fins de threads,
;      services, compteurs : a zero ; P_HSPAWN : pas recopie.
;  Un seul thread : copie du thread appelant (trame de l'appel en haut de son
;  bloc, x0 = 0 ; TPIDR_EL0, FP/NEON si TF_FP, priorite, nom, masque et pile
;  des signaux ; rien en attente). Il part de exc_return (fork_child_entry).
;  Avis SVC_OP_FORK aux services SVF_NAISSANCES (vfs) avant qu'il soit en file.
;
;  Refuse (E_PERM) a un process qui a des droits sur le materiel (P_DROITS :
;  services, IRQ, registres, DMA, ecran) ; E_NOMEM : le fils a moitie fait
;  est rendu, une attente de memoire (mem_wait), un second essai.
;
;  Verrous, dans cet ordre : kits_mutex, buf_mutex, puis P_VMLOCK du pere
;  (spinlock : la copie se fait IRQ masquees ; les autres threads du pere
;  continuent, leurs fautes attendent la fin de la copie). Avant : P_LOCK du
;  pere (tas, signaux), la table des poignees (feuille), img_lock ; apres,
;  sans rien tenir : proc_register, svc_lock (avis).
; ============================================================================

FORK_L1_SKIP_LO	= BUF_VA_BASE shr 30		; 416 : tampons, chargements, ecran,
FORK_L1_SKIP_HI	= (DMA_VA_BASE + MAT_FENETRE) shr 30	; 496 : mapcaller, registres, DMA
FORK_L1_KITS	= KIT_VA_BASE shr 30		; 320

; --- SYS_FORK fork() -> pid du fils (pere), 0 (fils) ----------------------------
sys_fork:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	current_proc
	mov	x20,x0				; thread appelant
	mov	x19,x1				; pere
	ldr	x0,[x19,P_DROITS]		; droits sur le materiel : refuse
	ldr	x1,[x19,P_DEV_VA]
	orr	x0,x0,x1
	ldr	x1,[x19,P_DMA_VA]
	orr	x0,x0,x1
	ldr	x1,[x19,P_FLAGS_WS]
	orr	x0,x0,x1
	cbnz	x0,.perm
	mov	x26,1				; essais apres manque de memoire
.again:	bl	vm_create
	cbz	x0,.oom
	mov	x21,x0				; fils
	mov	x0,x19
	mov	x1,x21
	bl	fork_fields
	cbz	x0,.abort
	mov	x0,x19
	mov	x1,x21
	bl	fork_handles
	cbz	x0,.abort
	adr	x0,kits_mutex
	bl	mutex_lock
	adr	x0,buf_mutex
	bl	mutex_lock
	ldr	x0,[x19,P_KITS]			; (pose sous kits_mutex)
	str	x0,[x21,P_KITS]
	add	x0,x19,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,x19
	mov	x1,x21
	bl	fork_space
	mov	x23,x0
	add	x0,x19,P_VMLOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	cbz	x23,.unl
	mov	x22,0				; tampons partages : remappes chez
.buf:	lsr	x0,x22,6			; le fils (BF_REFS, P_BUFS)
	add	x0,x19,x0,lsl 3
	ldr	x0,[x0,P_BUFS]
	lsr	x0,x0,x22
	tbz	x0,0,.bufn
	mov	x0,x21
	mov	x1,x22
	bl	buf_map_into
	cbnz	x0,.bufn
	mov	x23,0
	b	.unl
.bufn:	add	x22,x22,1
	cmp	x22,BUF_MAX
	b.lo	.buf
.unl:	adr	x0,buf_mutex
	bl	mutex_unlock
	adr	x0,kits_mutex
	bl	mutex_unlock
	cbz	x23,.abort
	ic	ialluis				; code recopie : caches I de tous
	dsb	ish				; les coeurs (cache D deja nettoye)
	isb
	mov	x0,x20
	mov	x1,x21
	bl	fork_thread
	cbz	x0,.abort
	mov	x23,x0				; TCB du fils (pas encore en file)
	mov	x0,x21
	bl	proc_register
	cbz	x0,.abort_th
	mov	x24,x0				; pid du fils
	ldr	x25,[x19,P_PID]
	str	x25,[x21,P_PARENT]
	mov	x0,x24				; avis aux services avant qu'il tourne
	mov	x1,x25
	bl	svc_fork_avis
	cbz	x0,.avis
	adr	x0,msg_fork_avis		; (file pleine : le fils n'a peut-etre
	bl	kputs				;  pas les bouts de tubes du pere)
.avis:	add	x0,x21,P_LOCK			; le thread dans le fils
	bl	spin_lock_irqsave
	mov	x1,x0
	ldr	x0,[x21,P_STATE]		; (tue entre-temps : le thread meurt
	cbz	x0,.vivant			;  aussitot, le fils passe au faucheur)
	mov	x0,TS_DEAD
	str	x0,[x23,TH_STATE]
.vivant:
	str	xzr,[x23,TH_PNEXT]
	str	x23,[x21,P_THREADS]
	mov	x0,1
	str	x0,[x21,P_NTHREADS]
	add	x0,x21,P_LOCK
	bl	spin_unlock_irqrestore
	bl	pick_cpu
	mov	x1,x0
	mov	x0,x23
	bl	thread_enqueue
	mov	x0,x24
	b	.out
.abort_th:
	mov	x0,x23
	mov	x1,THREAD_PAGES
	bl	pmm_free_n
.abort:	mov	x0,x21
	bl	fork_abort
	cbz	x26,.oom
	sub	x26,x26,1
	bl	mem_wait			; des services rendent de la memoire ?
	cbnz	x0,.again
.oom:	mov	x0,E_NOMEM
	b	.out
.perm:	mov	x0,E_PERM
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pere, x1 = fils (pas encore visible) : nom, tas, kits, entrees /
; sorties, retour des signaux, image (une reference), actions des signaux
; -> x0 = 1 (0 : plus de memoire)
fork_fields:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldp	x0,x1,[x19,P_NAME]
	stp	x0,x1,[x20,P_NAME]
	ldp	x0,x1,[x19,P_NAME+16]
	stp	x0,x1,[x20,P_NAME+16]
	ldr	x0,[x19,P_STDIO]
	str	x0,[x20,P_STDIO]
	ldr	x0,[x19,P_IMAGE]		; image : une reference de plus (rendue
	cbz	x0,.img				; par vm_destroy)
	str	x0,[x20,P_IMAGE]
	bl	img_ref
.img:	mov	x21,0				; table des actions : une page neuve
	ldr	x0,[x19,P_SIGTAB]
	cbz	x0,.lock
	bl	pmm_alloc
	cbz	x0,.out
	mov	x21,x0
.lock:	add	x0,x19,P_LOCK			; tas, actions (sous P_LOCK : sbrk,
	bl	spin_lock_irqsave		; SIG_ACTION)
	mov	x22,x0
	ldp	x0,x1,[x19,P_BRK]		; P_BRK, P_BRK_START
	stp	x0,x1,[x20,P_BRK]
	ldr	x0,[x19,P_SIGRET]
	str	x0,[x20,P_SIGRET]
	cbz	x21,.unl
	ldr	x1,[x19,P_SIGTAB]		; (posee entre-temps ? jamais retiree
	mov	x0,x21				;  d'un process vivant)
	mov	x2,PAGE_SIZE
	bl	mem_copy
	str	x21,[x20,P_SIGTAB]
.unl:	add	x0,x19,P_LOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pere, x1 = fils : table des poignees d'IPC recopiee aux memes numeros,
; une reference de plus par objet (sa liste de lancement P_HSPAWN : non)
; -> x0 = 1 (0 : plus de memoire)
fork_handles:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x0,[x19,P_HANDLES]
	cbz	x0,.ok
	mov	x0,IHT_PAGES
	bl	pmm_alloc_n
	cbz	x0,.out
	mov	x21,x0
	mov	x1,IHT_PAGES * 4096
	bl	mem_zero
	ldr	x19,[x19,P_HANDLES]		; (une table ne quitte son process
	mov	x0,x19				;  qu'a vm_destroy)
	bl	spin_lock_irqsave		; (verrou en tete de table)
	mov	x22,x0
	mov	x23,1
.h:	add	x1,x19,x23,lsl 4
	ldr	x0,[x1,IHE_OBJ]
	cbz	x0,.hn
	ldr	w2,[x1,IHE_TAG]
	add	x1,x21,x23,lsl 4
	str	x0,[x1,IHE_OBJ]
	str	w2,[x1,IHE_TAG]
	bl	iobj_ref			; (x2-x4)
.hn:	add	x23,x23,1
	cmp	x23,IHT_MAX
	b.lo	.h
	mov	x0,x19
	mov	x1,x22
	bl	spin_unlock_irqrestore
	str	x21,[x20,P_HANDLES]
.ok:	mov	x0,1
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sous P_VMLOCK du pere, kits_mutex et buf_mutex) x0 = pere, x1 = fils :
; pages de la moitie EL0 (copiees ou partagees, en-tete du fichier), puis la
; table des regions -> x0 = 1 (0 : plus de memoire ; ce qui est fait reste au
; fils, que vm_destroy rend)
fork_space:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x21,[x19,P_L1]
	movl	x28,PTE_ADDR_MASK
	mov	x22,256				; L1 de la moitie EL0
.l1:	cmp	x22,FORK_L1_SKIP_LO
	b.lo	.t1
	cmp	x22,FORK_L1_SKIP_HI
	b.lo	.n1
.t1:	ldr	x0,[x21,x22,lsl 3]
	tbz	x0,0,.n1
	and	x23,x0,x28			; table L2 du pere
	mov	x24,0
.l2:	ldr	x0,[x23,x24,lsl 3]
	tbz	x0,0,.n2
	and	x25,x0,x28			; table L3 du pere
	mov	x1,0				; (une table sans page : pas recopiee)
.e:	ldr	x2,[x25,x1,lsl 3]
	tbnz	x2,0,.has
	add	x1,x1,1
	cmp	x1,512
	b.lo	.e
	b	.n2
.has:	mov	x0,x20				; la table L3 du fils
	lsl	x1,x22,30
	orr	x1,x1,x24,lsl 21
	bl	vm_pte				; (entree 0)
	cbz	x0,.fail
	mov	x26,x0
	mov	x27,0
.l3:	ldr	x2,[x25,x27,lsl 3]
	tbz	x2,0,.n3
	tbz	x2,55,.copie			; a lui : recopiee
	lsl	x1,x22,30			; PTE_SHARED : a qui ?
	orr	x1,x1,x24,lsl 21
	orr	x1,x1,x27,lsl 12
	mov	x0,x19
	bl	fork_shared_owned
	ldr	x2,[x25,x27,lsl 3]
	cbz	x0,.copie			; epinglee par mapcaller : a lui
	str	x2,[x26,x27,lsl 3]		; page d'un objet : telle quelle
	b	.n3
.copie:	str	x2,[sp,-16]!
	bl	pmm_alloc
	ldr	x2,[sp],16
	cbz	x0,.fail
	str	x0,[sp,-16]!
	and	x1,x2,x28
	stp	x2,x0,[sp,-16]!
	mov	x2,PAGE_SIZE
	bl	mem_copy
	ldp	x2,x0,[sp],16
	tbnz	x2,54,.nx			; executable : cache D au point
	mov	x1,PAGE_SIZE			; d'unification (cache I : sys_fork)
	bl	dcache_clean_pou
	ldr	x2,[x25,x27,lsl 3]
.nx:	ldr	x0,[sp],16
	movl	x1,PTE_ADDR_MASK or PTE_SHARED
	bic	x2,x2,x1
	orr	x2,x2,x0
	str	x2,[x26,x27,lsl 3]
.n3:	add	x27,x27,1
	cmp	x27,512
	b.lo	.l3
.n2:	add	x24,x24,1
	cmp	x24,512
	b.lo	.l2
.n1:	add	x22,x22,1
	cmp	x22,512
	b.lo	.l1
	dsb	ishst				; tables du fils visibles
	ldr	x1,[x19,P_VMA]			; regions : meme table ; une
	cbz	x1,.ok				; reference par region VMK_SHARED
	ldr	x21,[x19,P_VMA_CAP]
	lsr	x0,x21,7
	bl	pmm_alloc_n
	cbz	x0,.fail
	mov	x22,x0
	ldr	x1,[x19,P_VMA]
	ldr	x23,[x19,P_VMA_N]
	lsl	x2,x23,5
	bl	mem_copy
	str	x22,[x20,P_VMA]
	str	x21,[x20,P_VMA_CAP]
	mov	x24,0
.v:	cmp	x24,x23
	b.hs	.vd
	add	x0,x22,x24,lsl 5
	add	x24,x24,1
	ldrb	w1,[x0,VMA_KIND]
	cmp	w1,VMK_SHARED
	b.ne	.v
	ldr	x0,[x0,24]
	bl	ishm_mapped
	b	.v
.vd:	str	x23,[x20,P_VMA_N]
.ok:	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sous P_VMLOCK) x0 = PCB, x1 = adresse d'une page PTE_SHARED -> x0 = 1 si
; elle est a un objet (code d'un kit, memoire partagee, page du modele de son
; image), 0 si c'est une page a lui epinglee par mapcaller
fork_shared_owned:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	vm_lookup
	ldr	x0,[x0]
	movl	x1,PTE_ADDR_MASK
	and	x21,x0,x1			; adresse physique
	mov	x22,x0				; entree
	lsr	x0,x20,30
	cmp	x0,FORK_L1_KITS
	b.ne	.region
	ubfx	x0,x20,26,4			; kit : code du kit (KT_RX) ?
	adr	x1,kit_table
	mov	x2,KT_SIZE
	madd	x1,x0,x2,x1
	ldr	x2,[x1,KT_RX]
	ldr	x3,[x1,KT_RX_SIZE]
	cmp	x21,x2
	b.lo	.no
	add	x2,x2,x3
	cmp	x21,x2
	b.hs	.no
	b	.yes
.region:
	mov	x0,x19				; memoire partagee ?
	mov	x1,x20
	bl	vma_find
	cbz	x0,.image
	ldrb	w1,[x0,VMA_KIND]
	cmp	w1,VMK_SHARED
	b.ne	.image
	ldr	w1,[x0,VMA_PROT]		; VM_COW : sans PTE_COW, c'est sa copie
	tbz	w1,8,.yes			; (VMP_COW)
	tbnz	x22,56,.yes
	b	.no
.image:	ldr	x0,[x19,P_IMAGE]		; page du modele, a la meme adresse ?
	cbz	x0,.no
	ldr	x0,[x0,IM_TPL]
	mov	x1,x20
	bl	vm_lookup
	cbz	x0,.no
	ldr	x0,[x0]
	tbz	x0,0,.no
	movl	x1,PTE_ADDR_MASK
	and	x0,x0,x1
	cmp	x0,x21
	b.ne	.no
.yes:	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = thread appelant (courant), x1 = fils -> x0 = TCB de son thread (pas
; en file, pas dans le fils ; 0 : plus de memoire)
fork_thread:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,fork_child_entry
	mov	x1,0
	add	x2,x19,TH_NAME
	bl	thread_new
	cbz	x0,.out
	mov	x21,x0
	movl	x22,THREAD_BLOCK-FRAME_SIZE	; la trame de l'appel, en haut du bloc
	add	x0,x21,x22
	add	x1,x19,x22
	mov	x2,FRAME_SIZE
	bl	mem_copy
	add	x0,x21,x22
	str	xzr,[x0,FR_X0]			; fork() -> 0 dans le fils
	str	x0,[x21,TH_CTX+96]		; switch_to : sp = la trame,
	adr	x0,fork_child_entry		; retour dans fork_child_entry
	str	x0,[x21,TH_CTX+88]		; (IRQ masquees jusqu'a l'eret)
	str	x20,[x21,TH_PROC]
	mrs	x0,tpidr_el0
	str	x0,[x21,TH_TPIDR]
	ldr	x0,[x19,TH_PRIO]
	str	x0,[x21,TH_PRIO]
	ldr	x0,[x19,TH_SIGMASK]
	str	x0,[x21,TH_SIGMASK]
	ldr	x0,[x19,TH_ALT_SP]		; pile des signaux
	str	x0,[x21,TH_ALT_SP]
	ldr	x0,[x19,TH_ALT_SIZE]
	str	x0,[x21,TH_ALT_SIZE]
	ldr	x1,[x19,TH_FLAGS]
	mov	x0,1 shl TF_USER
	tbz	x1,TF_FP,.flags
	orr	x0,x0,1 shl TF_FP		; FP/NEON : l'etat vivant du pere
	add	x2,x21,TH_FP			; (le noyau n'y touche jamais ; une
	stp	q0,q1,[x2,32*0]			;  preemption le sauve et le rend)
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
.flags:	str	x0,[x21,TH_FLAGS]
	mov	x0,x21
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Premier passage du thread d'un fils : switch_to y revient, IRQ masquees,
; sp = la trame recopiee. Sortie comme un appel systeme (signaux, kill).
fork_child_entry:
	b	exc_return

; x0 = fils a moitie fait (jamais enregistre, sans thread) : tout est rendu
fork_abort:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	sig_proc_exit			; actions des signaux
	ldr	x0,[x19,P_BUFS]			; tampons partages
	ldr	x1,[x19,P_BUFS+8]
	orr	x0,x0,x1
	cbz	x0,.vm
	adr	x0,buf_mutex
	bl	mutex_lock
	mov	x20,0
.b:	mov	x0,x19
	mov	x1,x20
	mov	x2,0				; (son espace part avec lui)
	bl	buf_release
	add	x20,x20,1
	cmp	x20,BUF_MAX
	b.lo	.b
	adr	x0,buf_mutex
	bl	mutex_unlock
.vm:	mov	x0,x19				; poignees, pages, regions, image,
	bl	vm_destroy			; ASID, PCB
	ldp	x19,x20,[sp],16
	epilogue

msg_fork_avis	db '[fork] file des services pleine : avis perdu',10,0
		align 4
