; ============================================================================
;  mapcaller.asm - un service voit la memoire de l'appelant (0x36-0x38,
;  docs/ABI.md ; programmes de SD0:/sys)
;
;  Pendant une requete (etat 2 de svc.asm), SVC_MAP_CALLER mappe une zone de
;  l'appelant dans une fenetre du service, a raison d'une entree de mc_table
;  par mappage : pages amenees (fautes, copie sur ecriture rompue) et epinglees.
;  La fenetre a sa place fixe : MC_WIN + numero d'entree x MC_SLOT.
;
;  Epingler, c'est prendre une reference a la page (pmm_ref) et poser PTE_PIN
;  chez l'appelant : un munmap ou sa mort rendent la sienne (pmm_put), sans
;  rien attendre ; la page reste tant que l'entree garde la sienne, rendue a
;  la fin (pmm_put). PTE_PIN dit a fork de recopier la page plutot que de la
;  partager (un service y ecrit peut-etre) ; il retombe a la fin si
;  l'appelant la mappe toujours et qu'aucune autre entree n'est a lui. Une
;  ecriture de l'appelant sur une page epinglee en copie sur ecriture la
;  copie : le service garde l'ancienne. Une page PTE_SHARED (kit, tampon,
;  fenetre d'un service : le mappage en chaine) n'est pas a lui : on ne la
;  touche pas (bit 0 de la liste).
;
;  Fin : SVC_UNMAP_CALLER, SVC_REPLY de la requete, ou fin du service.
;  mc_table est sous mc_lock (apres svc_lock) ; le travail sur les pages se
;  fait hors du verrou, MC_USED a 2 : le faucheur attend (mc_quiesce) avant de
;  detruire l'espace de l'appelant, puis les entrees oublient ce process.
; ============================================================================

MC_MAX		= 16
MC_SLOT		= 0x200000
MC_BYTES	= 0x100000		; un mappage : 1 Mo au plus
MC_WIN		= 0x7400000000		; L1[464] : fenetres
MC_WRITE	= 1			; (drapeau) le service y ecrira

MC_USED		= 0			; 0 libre, 1 pret, 2 en travail
MC_OWNER	= 8			; PCB du service
MC_REQ		= 16			; numero de la requete
MC_CALLER	= 24			; PCB de l'appelant (0 : parti)
MC_USER		= 32			; sa premiere page
MC_PAGES	= 40			; pages epinglees
MC_LOCAL	= 48			; adresse dans le service
MC_LIST		= 56			; page du noyau : physique + bit 0 (pas a lui)
MC_SIZE		= 64

; (sous svc_lock) x0 = appelant, x1 = service, x2 = requete, x3 = premiere
; page, x4 = pages, x5 = liste -> x0 = entree (MC_USED = 2), 0 si tout est pris
mc_new:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x5
	adr	x0,mc_lock
	bl	spin_lock_irqsave
	mov	x24,x0
	adr	x0,mc_table
	mov	x1,0
.f:	ldr	x2,[x0,MC_USED]
	cbz	x2,.got
	add	x0,x0,MC_SIZE
	add	x1,x1,1
	cmp	x1,MC_MAX
	b.lo	.f
	mov	x0,0
	b	.unl
.got:	stp	x20,x21,[x0,MC_OWNER]
	stp	x19,x22,[x0,MC_CALLER]
	str	xzr,[x0,MC_PAGES]
	movl	x2,MC_WIN
	mov	x3,MC_SLOT
	madd	x2,x1,x3,x2
	stp	x2,x23,[x0,MC_LOCAL]
	mov	x2,2
	str	x2,[x0,MC_USED]
	adr	x1,mc_count
	ldr	x2,[x1]
	add	x2,x2,1
	str	x2,[x1]
.unl:	mov	x19,x0
	adr	x0,mc_lock
	mov	x1,x24
	bl	spin_unlock_irqrestore
	mov	x0,x19
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree, x1 = 1 ecriture, x2 = pages voulues : amene, epingle et mappe
; -> x0 = 1 ; 0 : MC_PAGES dit ce qui est fait
mc_pin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x27,x1
	mov	x22,x2
	ldp	x23,x20,[x19,MC_CALLER]		; appelant, adresse
	ldr	x24,[x19,MC_OWNER]
	ldr	x25,[x19,MC_LIST]
	mov	x21,0				; page
.page:	cmp	x21,x22
	b.hs	.ok
	mov	x26,0				; fautes tentees
.try:	add	x0,x23,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x28,x0
	mov	x0,x23
	mov	x1,x20
	bl	vm_lookup
	cbz	x0,.absent
	ldr	x1,[x0]
	tbz	x1,0,.absent
	tbz	x1,6,.refus			; (AP[1] : page du noyau)
	cbz	x27,.pris
	tbnz	x1,7,.absent			; lecture seule, ou copie sur ecriture
.pris:	movl	x2,PTE_ADDR_MASK
	and	x3,x1,x2
	orr	x4,x3,1				; PTE_SHARED : pas a lui
	tbnz	x1,55,.liste
	stp	x0,x1,[sp,-16]!			; a lui : une reference, PTE_PIN
	str	x3,[sp,-16]!
	mov	x0,x3
	bl	pmm_ref
	mov	x4,x0
	ldr	x3,[sp],16
	ldp	x0,x1,[sp],16
	cbz	x4,.refus			; (compteur plein)
	orr	x1,x1,PTE_PIN
	str	x1,[x0]
	mov	x4,x3
.liste:	str	x4,[x25,x21,lsl 3]
	add	x1,x21,1
	str	x1,[x19,MC_PAGES]
	mov	x26,x3
	add	x0,x23,P_VMLOCK
	mov	x1,x28
	bl	spin_unlock_irqrestore
	add	x0,x24,P_VMLOCK			; chez le service
	bl	spin_lock_irqsave
	mov	x28,x0
	mov	x0,x24
	ldr	x1,[x19,MC_LOCAL]
	add	x1,x1,x21,lsl 12
	mov	x2,x26
	mov	x3,PAGE_SIZE
	movl	x4,VM_RO or PTE_SHARED
	cbz	x27,.map
	movl	x4,VM_RW or PTE_SHARED
.map:	bl	vm_map_phys
	mov	x26,x0
	add	x0,x24,P_VMLOCK
	mov	x1,x28
	bl	spin_unlock_irqrestore
	cbz	x26,.fail
	add	x20,x20,PAGE_SIZE
	add	x21,x21,1
	b	.page
.absent:
	add	x0,x23,P_VMLOCK
	mov	x1,x28
	bl	spin_unlock_irqrestore
	cbnz	x26,.fail			; (la faute n'y a rien change)
	mov	x26,1
	mov	x0,x23
	mov	x1,x20
	mov	x2,VF_EL0
	cbz	x27,.flt
	mov	x2,VF_EL0 or VF_WRITE
.flt:	bl	vm_fault_do
	cbnz	x0,.try
	b	.fail
.refus:	add	x0,x23,P_VMLOCK
	mov	x1,x28
	bl	spin_unlock_irqrestore
.fail:	mov	x0,0
	b	.out
.ok:	mov	x0,1
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree, x1 = 1 demapper chez le service (0 : il disparait) : rend tout
mc_free:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,mc_lock
	bl	spin_lock_irqsave
	mov	x1,x0
	mov	x0,2				; (mc_quiesce attend)
	str	x0,[x19,MC_USED]
	ldr	x21,[x19,MC_CALLER]
	adr	x0,mc_lock
	bl	spin_unlock_irqrestore
	ldr	x22,[x19,MC_PAGES]
	cbz	x20,.pages
	ldr	x23,[x19,MC_OWNER]
	add	x0,x23,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x24,x0
	mov	x0,x23
	ldr	x1,[x19,MC_LOCAL]
	lsl	x2,x22,12
	mov	x3,0
	bl	vm_unmap_range
	add	x0,x23,P_VMLOCK
	mov	x1,x24
	bl	spin_unlock_irqrestore
.pages:	mov	x20,0
.p:	cmp	x20,x22
	b.hs	.done
	ldr	x23,[x19,MC_LIST]
	ldr	x23,[x23,x20,lsl 3]
	tbnz	x23,0,.n			; pas a lui
	cbz	x21,.free			; plus d'appelant : a nous de la rendre
	add	x0,x21,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x24,x0
	ldr	x1,[x19,MC_USER]
	add	x1,x1,x20,lsl 12
	mov	x0,x21
	bl	vm_lookup
	cbz	x0,.parti
	ldr	x1,[x0]
	tbz	x1,0,.parti
	tbz	x1,58,.parti
	movl	x2,PTE_ADDR_MASK
	and	x2,x1,x2
	cmp	x2,x23
	b.ne	.parti
	stp	x0,x1,[sp,-16]!			; il la mappe toujours : PTE_PIN
	mov	x0,x19				; retombe, sauf si une autre entree
	mov	x1,x21				; est a lui (elle l'epingle peut-etre
	bl	mc_autre			; aussi)
	mov	x2,x0
	ldp	x0,x1,[sp],16
	cbnz	x2,.parti
	movl	x2,PTE_PIN
	bic	x1,x1,x2
	str	x1,[x0]
.parti:	add	x0,x21,P_VMLOCK			; (munmap ou copie pendant le
	mov	x1,x24				;  mappage : rien a changer)
	bl	spin_unlock_irqrestore
.free:	mov	x0,x23				; la reference de l'entree
	bl	pmm_put
.n:	add	x20,x20,1
	b	.p
.done:	ldr	x0,[x19,MC_LIST]
	bl	pmm_free
	adr	x0,mc_lock
	bl	spin_lock_irqsave
	mov	x1,x0
	str	xzr,[x19,MC_USED]
	adr	x2,mc_count
	ldr	x3,[x2]
	sub	x3,x3,1
	str	x3,[x2]
	adr	x0,mc_lock
	bl	spin_unlock_irqrestore
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree, x1 = appelant -> x0 = 1 si une autre entree en usage est a
; lui (lecture sans mc_lock : au pire, PTE_PIN reste, fork recopie la page)
mc_autre:
	adr	x2,mc_table
	mov	x3,0
.e:	cmp	x2,x0
	b.eq	.n
	ldr	x4,[x2,MC_USED]
	cbz	x4,.n
	ldr	x4,[x2,MC_CALLER]
	cmp	x4,x1
	b.eq	.oui
.n:	add	x2,x2,MC_SIZE
	add	x3,x3,1
	cmp	x3,MC_MAX
	b.lo	.e
	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; x0 = numero de requete, x1 = PCB du service : ses mappages sont finis
; (SVC_REPLY ; numero 0 : tous ceux du service, qui disparait)
mc_release:
	adr	x2,mc_count
	ldr	x2,[x2]
	cbz	x2,.ret
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x21,mc_table
	mov	x22,0
.e:	ldr	x0,[x21,MC_USED]
	cbz	x0,.n
	ldr	x0,[x21,MC_OWNER]
	cmp	x0,x20
	b.ne	.n
	mov	x1,0
	cbz	x19,.fr
	ldr	x0,[x21,MC_REQ]
	cmp	x0,x19
	b.ne	.n
	mov	x1,1
.fr:	mov	x0,x21
	bl	mc_free
.n:	add	x21,x21,MC_SIZE
	add	x22,x22,1
	cmp	x22,MC_MAX
	b.lo	.e
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.ret:	ret

; x0 = PCB dont l'espace va etre detruit : attend le travail en cours sur ses
; pages, puis les entrees ne le connaissent plus (faucheur)
mc_quiesce:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
.again:	adr	x0,mc_lock
	bl	spin_lock_irqsave
	mov	x1,x0
	adr	x2,mc_table
	mov	x3,0
	mov	x20,0
.e:	ldr	x4,[x2,MC_USED]
	cbz	x4,.n
	ldr	x5,[x2,MC_CALLER]
	cmp	x5,x19
	b.ne	.n
	cmp	x4,2
	b.eq	.busy
	str	xzr,[x2,MC_CALLER]
	b	.n
.busy:	mov	x20,1
.n:	add	x2,x2,MC_SIZE
	add	x3,x3,1
	cmp	x3,MC_MAX
	b.lo	.e
	adr	x0,mc_lock
	bl	spin_unlock_irqrestore
	cbz	x20,.out
	mov	x0,1
	bl	thread_sleep
	b	.again
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = adresse dans une fenetre du service courant -> x0 = entree, 0 sinon
mc_of_local:
	mov	x4,x0
	current_proc
	movl	x2,MC_WIN
	subs	x3,x4,x2
	b.lo	.non
	lsr	x3,x3,21
	cmp	x3,MC_MAX
	b.hs	.non
	adr	x0,mc_table
	mov	x2,MC_SIZE
	madd	x0,x3,x2,x0
	ldr	x2,[x0,MC_USED]
	cmp	x2,1
	b.ne	.non
	ldr	x2,[x0,MC_OWNER]
	cmp	x2,x1
	b.ne	.non
	ret
.non:	mov	x0,0
	ret

; --- svc_map_caller(requete, adresse, taille, drapeaux) -> adresse ici ---------
sys_svc_map_caller:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	and	x22,x3,MC_WRITE
	mov	x25,0
	mov	x27,0
	cbz	x21,.inval
	movl	x0,MC_BYTES
	cmp	x21,x0
	b.hi	.inval
	movl	x0,USER_BASE
	cmp	x20,x0
	b.lo	.fault
	adds	x1,x20,x21
	b.cs	.fault
	movl	x0,USER_TOP
	cmp	x1,x0
	b.hi	.fault
	current_proc
	mov	x23,x1
	ldr	x0,[x23,P_DROITS]
	cbz	x0,.perm
	sub	x0,x19,1
	cmp	x0,SR_MAX
	b.hs	.inval
	adr	x1,svc_requetes
	mov	x2,SR_SIZE
	madd	x24,x0,x2,x1
	bl	pmm_alloc
	cbz	x0,.nomem
	mov	x25,x0
	svc_prendre
	mov	x26,E_INVAL
	ldr	x0,[x24,SR_ETAT]
	cmp	x0,2
	b.ne	.unlock
	ldr	x0,[x24,SR_SVC]			; par le process du service
	bl	svc_entree
	ldr	x0,[x0,SV_PID]
	ldr	x1,[x23,P_PID]
	mov	x26,E_PERM
	cmp	x0,x1
	b.ne	.unlock
	mov	x26,E_PIPE
	ldr	x0,[x24,SR_PID]
	cbz	x0,.unlock			; appelant disparu
	movl	x1,SVC_PID_NOYAU
	cmp	x0,x1
	b.eq	.unlock
	bl	ws_proc_of			; (SR_PID le garde : svc_proc_exit)
	cbz	x0,.unlock
	ldr	x1,[x0,P_STATE]
	cbnz	x1,.unlock
	mov	x1,x23
	mov	x2,x19
	and	x3,x20,not 0xFFF
	add	x4,x20,x21
	add	x4,x4,0xFFF
	and	x4,x4,not 0xFFF
	sub	x4,x4,x3
	lsr	x4,x4,12
	mov	x5,x25
	bl	mc_new
	mov	x27,x0
	mov	x26,E_BUSY
.unlock:
	svc_rendre
	cbz	x27,.err
	add	x2,x20,x21
	add	x2,x2,0xFFF
	and	x2,x2,not 0xFFF
	and	x0,x20,not 0xFFF
	sub	x2,x2,x0
	lsr	x2,x2,12
	mov	x0,x27
	mov	x1,x22
	bl	mc_pin
	cbz	x0,.undo
	dmb	ish
	mov	x0,1
	str	x0,[x27,MC_USED]
	ldr	x0,[x27,MC_LOCAL]
	and	x1,x20,0xFFF
	add	x0,x0,x1
	b	.out
.undo:	mov	x0,x27
	mov	x1,1
	bl	mc_free
	mov	x0,E_FAULT
	b	.out
.err:	mov	x0,x25
	bl	pmm_free
	mov	x0,x26
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.fault:	mov	x0,E_FAULT
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- svc_unmap_caller(adresse rendue par map_caller) -> 0 ----------------------
sys_svc_unmap_caller:
	prologue
	bl	mc_of_local
	cbz	x0,.inval
	mov	x1,1
	bl	mc_free
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	epilogue

; --- svc_caller_phys(adresse, tableau, places) -> pages : leurs adresses -------
; physiques, pour le DMA (les pages du mappage, une par place)
sys_svc_caller_phys:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	bl	mc_of_local
	cbz	x0,.inval
	ldr	x1,[x0,MC_PAGES]
	cmp	x20,x1
	csel	x20,x20,x1,lo
	lsl	x2,x20,3
	cbz	x2,.vide
	mov	x3,x0
	str	x3,[sp,-16]!
	check_user x19,x2,.faultp
	ldr	x3,[sp],16
	ldr	x0,[x3,MC_LIST]
	mov	x1,0
.c:	ldr	x2,[x0,x1,lsl 3]
	and	x2,x2,not 1
	str	x2,[x19,x1,lsl 3]
	add	x1,x1,1
	cmp	x1,x20
	b.lo	.c
.vide:	mov	x0,x20
	b	.out
.faultp:
	add	sp,sp,16
	mov	x0,E_FAULT
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

		align 8
mc_lock		dd 0
mc_count	dd 0
