; ============================================================================
;  proc.asm - process EL0 : chargement ELF64, threads utilisateur, fin de vie
;
;  Un process = un PCB (vm.asm) + ses threads (liste TH_PNEXT).
;  Fin d'un process : tous ses threads passent TS_DEAD ; le dernier thread
;  recupere par un ordonnanceur place le process dans la liste des zombies,
;  vide par le thread noyau "faucheur" (liberation memoire hors IRQ).
; ============================================================================

; Table des process (proc_tab) : un emplacement de PT_SIZE octets par process
; vivant, dans des pages du noyau ; pleine, elle double (la memoire seule
; limite le nombre de process ; l'ancienne table n'est pas rendue : un lecteur
; sans verrou peut encore la lire). pid = generation << 16 | (emplacement + 1) :
; un emplacement repris porte un autre pid (generation + 1), un ancien pid ne
; designe jamais un autre process.
PT_PCB		= 0			; PCB vivant (0 : libre)
PT_PID		= 8			; pid du process, ou du dernier termine ici
PT_CODE		= 16			; son code de sortie (wait)
PT_GEN		= 24			; generation du prochain pid
PT_SIZE		= 32
PROC_SLOTS_0	= 128			; premiere table : une page
PROC_SLOTS_MAX	= 0xFFFF		; (pid : 16 bits d'emplacement, 15 de generation)
ARGS_MAX	= 1024			; ligne d'arguments d'un process

ET_EXEC		= 2
EM_AARCH64	= 183
PT_LOAD		= 1
PF_X		= 1
PF_W		= 2

; x0 = image ELF en memoire, x1 = taille, x2 = nom, x3 = ligne d'arguments
; (chaine noyau, 0 si aucune : recopiee en haut de la pile EL0, son adresse
; est passee dans x0), x4 = image prechargee (preload.asm, 0 : aucune ; sa
; reference passe au process, rendue par vm_destroy ou ici en cas d'echec ;
; x0 et x1 sont alors ignores), x5 : inutilise (ancien tube du noyau : vfs le
; tient, docs/FICHIERS.md, etape 5), x6 = droits (P_DROITS : 1 pour un
; programme de SD0:/sys), x7 = entrees / sorties standard du fils (3 x 8 o,
; noyau : poignees de tubes de vfs, 0 : celle du pere, -1 : aucune ; x7 = 0 :
; toutes heritees ; docs/FICHIERS.md, etape 4) : recopiees dans l'avis de
; naissance, P_STDIO en garde le masque
; -> x0 = PID (0 si echec)
proc_spawn_elf:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,32
	stp	x4,x5,[sp]
	stp	x6,x7,[sp,16]
	mov	x19,x0				; image
	mov	x20,x1				; taille
	mov	x21,x2				; nom
	mov	x26,x3
	cbnz	x4,.hdr_ok			; (image prechargee : deja controlee)
	mov	x0,x19
	mov	x1,x20
	bl	elf_check
	cbz	x0,.bad
.hdr_ok:
	bl	vm_create
	cbz	x0,.bad
	mov	x22,x0				; PCB
	ldr	x0,[sp]				; la reference de l'image est a lui
	str	x0,[x22,P_IMAGE]
	str	xzr,[sp]
	mov	x0,x22				; nom
	add	x0,x0,P_NAME
	mov	x1,x21
	mov	x2,31
	bl	str_ncopy
	ldr	x0,[sp,16]			; droits
	str	x0,[x22,P_DROITS]
	this_cpu x0				; masque des entrees / sorties de vfs :
	ldr	x0,[x0,PC_CURRENT]		; bit i = 1 si l'entree i du fils est
	ldr	x0,[x0,TH_PROC]			; un tube (donnee par le pere, ou
	cbz	x0,.sio_p			; heritee de lui)
	ldr	x0,[x0,P_STDIO]
.sio_p:	ldr	x1,[sp,24]
	cbz	x1,.sio_ok
	mov	x2,0
.sio:	ldr	x3,[x1,x2,lsl 3]
	cbz	x3,.sio_n			; 0 : comme le pere
	mov	x4,1
	lsl	x4,x4,x2
	bic	x0,x0,x4
	cmn	x3,1				; -1 : aucune (console)
	b.eq	.sio_n
	orr	x0,x0,x4
.sio_n:	add	x2,x2,1
	cmp	x2,3
	b.lo	.sio
.sio_ok:
	and	x0,x0,7
	str	x0,[x22,P_STDIO]

	; --- segments : charges, ou repris d'une image prechargee ------------
	ldr	x1,[x22,P_IMAGE]
	cbz	x1,.load
	mov	x0,x22
	bl	img_clone			; pages partagees, copie sur ecriture
	cbz	x0,.bad_vm
	ldr	x1,[x22,P_IMAGE]
	ldr	x25,[x1,IM_BRK]
	ldr	x24,[x1,IM_ENTRY]
	b	.ph_done
.load:	mov	x0,x22
	mov	x1,x19
	mov	x2,x20
	bl	elf_load
	cbz	x0,.bad_vm
	mov	x25,x1				; fin de la plus haute (-> brk)
	ldr	x24,[x19,24]			; e_entry
.ph_done:

	; --- pile (16 Ko du haut tout de suite, le reste a la demande), tas ---
	mov	x0,x22
	bl	stack_setup
	cbz	x0,.bad_vm
	add	x25,x25,0xFFF
	and	x25,x25,not 0xFFF
	str	x25,[x22,P_BRK]
	str	x25,[x22,P_BRK_START]
	ic	ialluis				; code neuf : caches I de tous les coeurs
	dsb	ish
	isb

	; --- poignees d'IPC donnees par le pere (HANDLE_INHERIT, ipc.asm) ---
	mov	x0,x22
	bl	ipc_spawn_give

	; --- enregistrement + thread principal --------------------------------
	mov	x0,x22
	bl	proc_register
	cbz	x0,.bad_vm
	this_cpu x1				; avis de naissance (fils, pere) avant
	ldr	x1,[x1,PC_CURRENT]		; le premier thread : vfs copie le
	ldr	x1,[x1,TH_PROC]			; dossier du pere
	cbz	x1,.pere
	ldr	x1,[x1,P_PID]
.pere:	str	x1,[x22,P_PARENT]		; (0 : lance par le noyau)
	ldr	x2,[sp,24]			; (entrees / sorties du fils)
	bl	svc_naissance
	movl	x23,USER_STACK_TOP		; arguments en haut de pile
	mov	x21,0
	cbz	x26,.no_args
	sub	x23,x23,ARGS_MAX
	mov	x0,x26				; longueur (bornee) + 0 final
	mov	x2,0
.alen:	ldrb	w1,[x0,x2]
	add	x2,x2,1
	cbz	w1,.acopy
	cmp	x2,ARGS_MAX-1
	b.lo	.alen
.acopy:	mov	x0,x22
	mov	x1,x26
	mov	x3,x23
	bl	vm_copy_to_user
	mov	x21,x23
.no_args:
	mov	x0,x22
	mov	x1,x24				; entree
	mov	x2,x23
	mov	x3,x21
	mov	x4,0				; thread principal : TPIDR_EL0 = 0,
	mov	x5,0				; priorite normale, nom du process
	mov	x6,0
	bl	proc_add_thread
	cbz	x0,.bad_vm			; (le PCB reste enregistre : TODO nettoyage)
	ldr	x0,[x22,P_PID]
	b	.out

.bad_vm:
	mov	x0,x22				; (rend aussi l'image prechargee)
	bl	vm_destroy
	b	.bad0
.bad:	ldr	x0,[sp]				; image prechargee pas encore donnee
	cbz	x0,.bad0
	bl	image_unref
.bad0:	mov	x0,0
.out:	add	sp,sp,32
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = image, x1 = taille -> x0 = 1 si c'est un ELF64 AArch64 executable
elf_check:
	cmp	x1,64
	b.lo	.no
	ldr	w2,[x0]
	movl	w3,0x464C457F			; 0x7F 'E' 'L' 'F'
	cmp	w2,w3
	b.ne	.no
	ldrb	w2,[x0,4]			; ELFCLASS64
	cmp	w2,2
	b.ne	.no
	ldrh	w2,[x0,16]
	cmp	w2,ET_EXEC
	b.ne	.no
	ldrh	w2,[x0,18]
	cmp	w2,EM_AARCH64
	b.ne	.no
	ldr	x2,[x0,32]			; table des segments dans l'image
	ldrh	w3,[x0,56]
	mov	x4,56
	madd	x2,x3,x4,x2
	cmp	x2,x1
	b.hi	.no
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = PCB, x1 = image ELF (controlee : elf_check), x2 = taille : segments
; PT_LOAD. La partie lue du fichier est peuplee et copiee ; le reste du
; segment (.bss) est une region paresseuse (VMK_BSS : a zero, peuplee a la
; premiere touche), peuplee d'avance si elle chevaucherait une autre.
; -> x0 = 1 si succes, x1 = fin du plus haut segment
elf_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x22,x0
	mov	x19,x1
	mov	x20,x2
	ldr	x23,[x19,32]			; e_phoff
	ldrh	w24,[x19,56]			; e_phnum
	add	x23,x19,x23
	movl	x25,USER_BASE			; plus haute adresse chargee (-> brk)
.ph:	cbz	w24,.ok
	ldr	w0,[x23]			; p_type
	cmp	w0,PT_LOAD
	b.ne	.ph_next
	ldr	x1,[x23,16]			; p_vaddr
	ldr	x2,[x23,40]			; p_memsz
	ldr	x3,[x23,32]			; p_filesz
	cmp	x3,x2
	b.hi	.bad
	movl	x4,USER_BASE			; le segment doit etre dans la zone EL0
	cmp	x1,x4
	b.lo	.bad
	adds	x4,x1,x2
	b.cs	.bad
	movl	x5,USER_STACK_TOP - USER_STACK_SIZE
	cmp	x4,x5
	b.hi	.bad
	cmp	x4,x25
	csel	x25,x4,x25,hi
	ldr	x5,[x23,8]			; p_offset + p_filesz dans l'image
	adds	x5,x5,x3
	b.cs	.bad
	cmp	x5,x20
	b.hi	.bad
	ldr	w5,[x23,4]			; p_flags -> PROT_*
	mov	x26,PROT_READ
	tbz	w5,1,.nw			; PF_W
	orr	x26,x26,PROT_WRITE
.nw:	tbz	w5,0,.nx			; PF_X
	orr	x26,x26,PROT_EXEC
.nx:	add	x27,x1,x3			; fin de la partie lue, a la page
	add	x27,x27,0xFFF			; (debut de la partie paresseuse)
	and	x27,x27,not 0xFFF
	add	x28,x4,0xFFF			; fin du segment, a la page
	and	x28,x28,not 0xFFF
	cmp	x27,x28
	csel	x27,x28,x27,hi
	; partie lue (et la premiere page entamee) : peuplee, copiee
	cmp	x27,x1
	b.ls	.lazy
	mov	x0,x26
	bl	vm_prot_attrs
	mov	x3,x0
	ldr	x1,[x23,16]
	sub	x2,x27,x1
	mov	x0,x22
	bl	vm_alloc_range
	cbz	x0,.bad
	ldr	x1,[x23,8]			; p_offset
	ldr	x2,[x23,32]			; p_filesz
	mov	x0,x22
	ldr	x3,[x23,16]
	add	x1,x19,x1
	bl	vm_copy_to_user
.lazy:	cmp	x28,x27				; .bss : region paresseuse
	b.ls	.ph_next
	mov	x0,x22
	mov	x1,x27
	mov	x2,x28
	bl	vma_overlaps
	cbnz	x0,.eager
	mov	x0,x22
	mov	x1,x27
	bl	vma_lower
	mov	x1,x0
	mov	x0,x22
	mov	x2,x27
	mov	x3,x28
	mov	x4,VMK_BSS
	lsl	x4,x4,32
	orr	x4,x4,x26
	bl	vma_insert_at
	cbnz	x0,.ph_next
.eager:	mov	x0,x26				; (chevauchement : peuplee d'avance)
	bl	vm_prot_attrs
	mov	x3,x0
	mov	x0,x22
	mov	x1,x27
	sub	x2,x28,x27
	bl	vm_alloc_range
	cbz	x0,.bad
.ph_next:
	add	x23,x23,56
	sub	w24,w24,1
	b	.ph
.ok:	mov	x0,1
	mov	x1,x25
	b	.out
.bad:	mov	x0,0
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB : pile du thread principal, [USER_STACK_TOP - USER_STACK_SIZE,
; USER_STACK_TOP[ : 16 Ko du haut peuples (arguments, premiers appels), le
; reste en region paresseuse (VMK_STACK) -> x0 = 1 si succes
stack_setup:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	movl	x1,USER_STACK_TOP - STACK_EAGER
	mov	x2,STACK_EAGER
	movl	x3,VM_RW
	bl	vm_alloc_range
	cbz	x0,.out
	mov	x0,x19
	movl	x1,USER_STACK_TOP - USER_STACK_SIZE
	movl	x2,USER_STACK_TOP - STACK_EAGER
	bl	vma_overlaps
	cbnz	x0,.eager
	mov	x0,x19
	movl	x1,USER_STACK_TOP - USER_STACK_SIZE
	bl	vma_lower
	mov	x1,x0
	mov	x0,x19
	movl	x2,USER_STACK_TOP - USER_STACK_SIZE
	movl	x3,USER_STACK_TOP - STACK_EAGER
	movl	x4,(VMK_STACK shl 32) or PROT_READ or PROT_WRITE
	bl	vma_insert_at
	cbnz	x0,.out
.eager:	mov	x0,x19
	movl	x1,USER_STACK_TOP - USER_STACK_SIZE
	movl	x2,USER_STACK_SIZE - STACK_EAGER
	movl	x3,VM_RW
	bl	vm_alloc_range
.out:	ldr	x19,[sp],16
	epilogue

; x0 = PCB, x1 = source (noyau), x2 = taille, x3 = VA destination (deja mappee)
vm_copy_to_user:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
.page:	cbz	x21,.done
	mov	x0,x19
	mov	x1,x22
	bl	vm_lookup
	cbz	x0,.done
	ldr	x0,[x0]
	movl	x1,PTE_ADDR_MASK
	and	x0,x0,x1
	and	x1,x22,0xFFF
	add	x0,x0,x1			; adresse physique de destination
	mov	x2,PAGE_SIZE
	sub	x2,x2,x1			; place restante dans la page
	cmp	x2,x21
	csel	x2,x2,x21,lo
	mov	x1,x20
	add	x20,x20,x2
	add	x22,x22,x2
	sub	x21,x21,x2
	mov	x3,x0
	stp	x2,x3,[sp,-16]!
	bl	mem_copy
	ldp	x1,x0,[sp],16			; (taille, debut) -> nettoyage cache D
	bl	dcache_clean_pou
	b	.page
.done:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = debut, x1 = taille : nettoie le cache D jusqu'au point d'unification
dcache_clean_pou:
	add	x1,x0,x1
	bic	x0,x0,63
.line:	dc	cvau,x0
	add	x0,x0,64
	cmp	x0,x1
	b.lo	.line
	dsb	ish
	ret

; (proc_lookup, proc_slot) x2 <- l'emplacement du pid x0, sinon vers lbl
macro bl_slot lbl {
	and	x1,x0,0xFFFF
	cbz	x1,lbl
	sub	x1,x1,1
	adr	x2,proc_cap
	ldar	x2,[x2]				; (la taille avant la table : proc_grow)
	cmp	x1,x2
	b.hs	lbl
	adr	x2,proc_tab
	ldr	x2,[x2]
	add	x2,x2,x1,lsl 5
	ldr	x1,[x2,PT_PID]
	cmp	x1,x0
	b.ne	lbl
}

; x0 = pid -> x0 = PCB vivant (0 : aucun) ; ne touche que x0-x2. Sans verrou :
; qui garde le PCB au-dela prend proc_lock (le faucheur le retire sous ce verrou)
proc_lookup:
	bl_slot	.none
	ldr	x0,[x2,PT_PCB]
	ret
.none:	mov	x0,0
	ret

; x0 = pid -> x0 = son emplacement (vivant ou termine : PT_CODE), 0 si le pid
; n'y est plus ; ne touche que x0-x2
proc_slot:
	bl_slot	.none
	mov	x0,x2
	ret
.none:	mov	x0,0
	ret


; x0 = PCB -> x0 = PID, 0 si la memoire manque. Les emplacements sont pris en
; tournant (un pid libere attend un tour avant que son emplacement resserve,
; sous un autre pid)
proc_register:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,proc_lock
	bl	spin_lock_irqsave
	mov	x20,x0
.retry:	adr	x0,proc_cap
	ldr	x21,[x0]
	adr	x0,proc_tab
	ldr	x22,[x0]
	adr	x4,proc_next
	ldr	x2,[x4]
	cmp	x2,x21
	csel	x2,xzr,x2,hs
	mov	x5,x21
.slot:	cbz	x5,.grow
	add	x6,x22,x2,lsl 5
	ldr	x3,[x6,PT_PCB]
	cbz	x3,.free
	add	x2,x2,1
	cmp	x2,x21
	csel	x2,xzr,x2,hs
	sub	x5,x5,1
	b	.slot
.grow:	bl	proc_grow
	cbnz	x0,.retry
	mov	x19,0
	b	.out
.free:	ldr	x3,[x6,PT_GEN]			; pid : generation, emplacement + 1
	add	x7,x3,1
	and	x7,x7,0x7FFF
	str	x7,[x6,PT_GEN]
	add	x8,x2,1
	orr	x8,x8,x3,lsl 16
	str	x8,[x6,PT_PID]
	str	xzr,[x6,PT_CODE]
	str	x8,[x19,P_PID]
	dmb	ishst
	str	x19,[x6,PT_PCB]
	add	x2,x2,1
	str	x2,[x4]
	mov	x19,x8
.out:	adr	x0,proc_lock
	mov	x1,x20
	bl	spin_unlock_irqrestore
	mov	x0,x19
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sous proc_lock) la table double (premiere : PROC_SLOTS_0) -> x0 = 1, 0 si
; la memoire manque ou PROC_SLOTS_MAX est atteint
proc_grow:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,proc_cap
	ldr	x19,[x0]			; ancienne taille
	lsl	x20,x19,1
	cbnz	x19,.max
	mov	x20,PROC_SLOTS_0
.max:	mov	x0,PROC_SLOTS_MAX
	cmp	x20,x0
	csel	x20,x0,x20,hi
	cmp	x20,x19
	b.ls	.fail
	lsl	x21,x20,5			; octets, puis pages
	add	x0,x21,4095
	lsr	x0,x0,12
	bl	pmm_alloc_n
	cbz	x0,.fail
	mov	x22,x0
	add	x1,x21,4095
	and	x1,x1,0xFFFFFFFFFFFFF000
	bl	mem_zero
	mov	x0,x22				; les emplacements deja la
	adr	x1,proc_tab
	ldr	x1,[x1]
	lsl	x2,x19,5
	bl	mem_copy
	dmb	ish
	adr	x0,proc_tab			; la table, puis sa taille : qui lit la
	str	x22,[x0]			; nouvelle taille lit la nouvelle table
	adr	x0,proc_cap
	stlr	x20,[x0]
	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = entree EL0, x2 = pile EL0, x3 = argument, x4 = TPIDR_EL0
; initial, x5 = priorite (THREAD_PRIO_*), x6 = nom (chaine du noyau, 0 : celui
; du process) -> x0 = TCB (0 si echec ; a ne plus lire : le thread tourne
; peut-etre deja), x1 = son tid
proc_add_thread:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x25,x4
	mov	x26,x5
	adr	x0,user_thread_start
	mov	x1,0
	add	x2,x19,P_NAME
	cbz	x6,.nom
	mov	x2,x6
.nom:	bl	thread_new
	cbz	x0,.out
	mov	x23,x0
	str	x23,[x23,TH_CTX+8]		; argument du trampoline = le TCB lui-meme
	str	x20,[x23,TH_UENTRY]
	str	x21,[x23,TH_USP]
	str	x22,[x23,TH_UARG]
	str	x19,[x23,TH_PROC]
	str	x25,[x23,TH_TPIDR]		; (charge par switch_to au premier tour)
	str	x26,[x23,TH_PRIO]
	this_cpu x0				; le masque des signaux s'herite
	ldr	x0,[x0,PC_CURRENT]
	ldr	x0,[x0,TH_SIGMASK]
	str	x0,[x23,TH_SIGMASK]
	mov	x0,1 shl TF_USER
	str	x0,[x23,TH_FLAGS]

	add	x0,x19,P_LOCK			; chainage dans le process
	bl	spin_lock_irqsave
	mov	x24,x0
	ldr	x0,[x19,P_STATE]
	cbnz	x0,.dying
	ldr	x0,[x19,P_THREADS]
	str	x0,[x23,TH_PNEXT]
	str	x23,[x19,P_THREADS]
	ldr	x0,[x19,P_NTHREADS]
	add	x0,x0,1
	str	x0,[x19,P_NTHREADS]
	add	x0,x19,P_LOCK
	mov	x1,x24
	bl	spin_unlock_irqrestore

	ldr	x24,[x23,TH_ID]			; (lu avant : le thread peut finir et
	bl	pick_cpu			;  etre libere des qu'il est en file)
	mov	x1,x0
	mov	x0,x23
	bl	thread_enqueue
	mov	x0,x23
	mov	x1,x24
	b	.out
.dying:	add	x0,x19,P_LOCK			; process en fin de vie : refus
	mov	x1,x24
	bl	spin_unlock_irqrestore
	mov	x0,x23
	mov	x1,THREAD_PAGES
	bl	pmm_free_n
	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Premier passage d'un thread EL0 : x0 = son TCB. Ne revient pas.
user_thread_start:
	msr	daifset,2
	add	x1,x0,THREAD_BLOCK		; pile noyau vide pour les futures exceptions
	mov	sp,x1
	ldr	x1,[x0,TH_UENTRY]
	msr	elr_el1,x1
	ldr	x1,[x0,TH_USP]
	msr	sp_el0,x1
	msr	spsr_el1,xzr			; EL0t, interruptions autorisees
	ldr	x0,[x0,TH_UARG]
	mov	x1,0
	mov	x2,0
	mov	x3,0
	mov	x4,0
	mov	x5,0
	mov	x6,0
	mov	x7,0
	mov	x8,0
	mov	x9,0
	mov	x10,0
	mov	x11,0
	mov	x12,0
	mov	x13,0
	mov	x14,0
	mov	x15,0
	mov	x16,0
	mov	x17,0
	mov	x18,0
	mov	x19,0
	mov	x20,0
	mov	x21,0
	mov	x22,0
	mov	x23,0
	mov	x24,0
	mov	x25,0
	mov	x26,0
	mov	x27,0
	mov	x28,0
	mov	x29,0
	mov	x30,0
	eret

; x0 = PCB, x1 = code de sortie : tue tous les threads du process
proc_kill:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	add	x0,x19,P_LOCK
	bl	spin_lock_irqsave
	mov	x20,x0
	ldr	x0,[x19,P_STATE]
	cbnz	x0,.unlock			; deja en fin de vie
	mov	x0,1
	str	x0,[x19,P_STATE]
	str	x21,[x19,P_EXIT_CODE]
	ldr	x22,[x19,P_THREADS]
.th:	cbz	x22,.unlock
	mov	x0,TS_DEAD
	str	x0,[x22,TH_STATE]
	ldr	x0,[x22,TH_CPU]
	bl	cpu_kick
	ldr	x22,[x22,TH_PNEXT]
	b	.th
.unlock:
	add	x0,x19,P_LOCK
	mov	x1,x20
	bl	spin_unlock_irqrestore
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = TCB recupere (appele par l'ordonnanceur, IRQ masquees) :
; le thread quitte la liste du process (sa memoire va etre rendue : la
; laisser chainee faisait parcourir de la memoire liberee a futex_wake et au
; reveil des services), puis le compte des threads baisse
proc_thread_gone:
	add	x2,x0,P_LOCK
	mov	w4,1
.pl:	ldaxr	w3,[x2]
	cbnz	w3,.pl
	stxr	w3,w4,[x2]
	cbnz	w3,.pl
	add	x3,x0,P_THREADS			; x3 : le lien qui pointe sur lui
.u:	ldr	x5,[x3]
	cbz	x5,.ud
	cmp	x5,x1
	b.eq	.unl
	add	x3,x5,TH_PNEXT
	b	.u
.unl:	ldr	x5,[x1,TH_PNEXT]
	str	x5,[x3]
.ud:	stlr	wzr,[x2]
	add	x2,x0,P_NTHREADS
.dec:	ldaxr	x3,[x2]
	sub	x3,x3,1
	stlxr	w4,x3,[x2]
	cbnz	w4,.dec
	cbnz	x3,.ret
	adr	x2,zombie_lock			; dernier thread : process zombie
	mov	w4,1
.lock:	ldaxr	w3,[x2]
	cbnz	w3,.lock
	stxr	w3,w4,[x2]
	cbnz	w3,.lock
	adr	x3,zombie_list
	ldr	x4,[x3]
	str	x4,[x0,P_ZNEXT]
	str	x0,[x3]
	stlr	wzr,[x2]
.ret:	ret

; Thread noyau : libere les process termines
reaper_thread:
.loop:	mov	x0,20
	bl	thread_sleep
	bl	mem_watch
.pop:	adr	x0,zombie_lock
	bl	spin_lock_irqsave
	mov	x1,x0
	adr	x2,zombie_list
	ldr	x19,[x2]
	cbz	x19,.empty
	ldr	x3,[x19,P_ZNEXT]
	str	x3,[x2]
.empty:	adr	x0,zombie_lock
	bl	spin_unlock_irqrestore
	cbz	x19,.loop
	mov	x0,x19				; hooks (fenetres, fichiers...) et avis
	bl	proc_on_exit			; de fin AVANT de rendre le pid : un
						; spawn ne peut pas reprendre ce pid
						; et poster sa naissance avant la fin
	adr	x0,proc_lock			; retrait de la table des process
	bl	spin_lock_irqsave
	mov	x20,x0
	ldr	x0,[x19,P_PID]
	bl	proc_slot
	cbz	x0,.retire
	ldr	x2,[x19,P_EXIT_CODE]		; code de sortie conserve pour wait()
	str	x2,[x0,PT_CODE]
	str	xzr,[x0,PT_PCB]
.retire:
	adr	x0,proc_lock
	mov	x1,x20
	bl	spin_unlock_irqrestore
	mov	x0,x19				; son pere : wait a de quoi repondre
	bl	sig_child
	mov	x0,x19				; (ses pages encore mappees par un
	bl	mc_quiesce			;  service : mapcaller.asm)
	mov	x0,x19
	bl	vm_destroy
	b	.pop

; x0 = destination, x1 = source, x2 = taille max (hors 0 final)
str_ncopy:
.c:	cbz	x2,.end
	ldrb	w3,[x1],1
	cbz	w3,.end
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c
.end:	strb	wzr,[x0]
	ret
