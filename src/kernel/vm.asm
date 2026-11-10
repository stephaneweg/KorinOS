; ============================================================================
;  vm.asm - espaces d'adressage des process (TTBR0 + ASID)
;
;  VA 39 bits, granule 4 Ko, 3 niveaux (L1 1 Go, L2 2 Mo, L3 4 Ko).
;    L1[0..255]   : noyau, recopie de pt_l1 (entrees partagees, EL1 seulement)
;    L1[256..511] : process (0x40_0000_0000 .. 0x7F_FFFF_FFFF), pages nG + ASID
;  Le noyau voit toute la RAM en identite (memmap.asm) : une table ou une page,
;  basse ou haute, s'adresse directement par son adresse physique.
;  Regions paresseuses, fautes de page, copie sur ecriture : vmspace.asm.
; ============================================================================

USER_BASE	= 0x4000000000
USER_TOP	= 0x8000000000
USER_STACK_TOP	= 0x7FFFFF0000
USER_STACK_SIZE	= 0x4000000			; 64 Mo, peuples a la demande (VMK_STACK)

PTE_PAGE	= 3				; descripteur de page (niveau 3)
PTE_NG		= 1 shl 11
PTE_AP_EL0	= 1 shl 6			; EL0 + EL1 lecture/ecriture
PTE_AP_RO	= 3 shl 6			; EL0 + EL1 lecture seule

; attributs d'une page utilisateur
VM_RW		= PTE_PAGE or (MAIR_NORMAL shl 2) or PTE_SH_INNER or PTE_AF or PTE_NG \
		  or PTE_AP_EL0 or PTE_PXN or PTE_UXN
VM_RX		= PTE_PAGE or (MAIR_NORMAL shl 2) or PTE_SH_INNER or PTE_AF or PTE_NG \
		  or PTE_AP_RO or PTE_PXN
VM_RWX		= PTE_PAGE or (MAIR_NORMAL shl 2) or PTE_SH_INNER or PTE_AF or PTE_NG \
		  or PTE_AP_EL0 or PTE_PXN
VM_RO		= PTE_PAGE or (MAIR_NORMAL shl 2) or PTE_SH_INNER or PTE_AF or PTE_NG \
		  or PTE_AP_RO or PTE_PXN or PTE_UXN
PTE_ADDR_MASK	= 0x0000FFFFFFFFF000
PTE_SHARED	= 1 shl 55			; bit logiciel : page partagee (code d'un
						; kit, image prechargee), jamais liberee
						; par vm_destroy
PTE_COW		= 1 shl 56			; bit logiciel : copie sur ecriture (page
						; d'une image prechargee, lecture seule)

; --- PCB (une page) ----------------------------------------------------------
P_TTBR		= 0			; L1 | ASID << 48
P_L1		= 8
P_PID		= 16
P_STATE		= 24			; 0 vivant, 1 en fin de vie
P_NTHREADS	= 32
P_BRK		= 40
P_BRK_START	= 48
P_EXIT_CODE	= 56
P_LOCK		= 64
P_THREADS	= 72			; liste des threads (TH_PNEXT)
P_ZNEXT		= 80			; chainage dans la liste des zombies
P_PARENT	= 88
P_VMLOCK	= 128			; verrou des regions et des fautes (vmspace.asm)
P_VMA		= 136			; table des regions paresseuses (pages du noyau)
P_VMA_N		= 144			; regions
P_VMA_CAP	= 152			; places (32 o chacune)
P_IMAGE		= 160			; image dont le process est lance (preload.asm), 0 sinon
P_FAULTS	= 168			; pages remplies a la demande
P_COWS		= 176			; pages copiees sur ecriture
P_VM_OOM	= 184			; 1 : une faute a manque de memoire
P_HANDLES	= 192			; table des poignees d'IPC (ipc.asm : 4 pages, 0 avant la premiere)
P_HSPAWN	= 200			; poignees a donner au prochain fils (HANDLE_INHERIT : une page)
P_SIGTAB	= 208			; actions des signaux (page, signal.asm)
P_SIGRET	= 216			; retour d'un gestionnaire : fait SIG_RETURN
P_SVC_N		= 224			; appels systeme faits (syscall_dispatch ; PROC_STATS)
P_EXEC		= 232			; exec.asm : bit 0 exec en cours, bit 1 programme d'un exec
P_EXECNEW	= 240			; exec.asm : PCB de travail (nouvel espace) pendant un exec
P_ALARM		= 248			; alarm.asm : echeance de la minuterie (CNTPCT, 0 : aucune)
P_ALARM_IV	= 256			; alarm.asm : son intervalle (CNTPCT, 0 : une fois)
					; 264-407 : libre (ancienne table des descripteurs)
P_STOP		= 408			; jobs.asm : signal qui a arrete le process (0 : il tourne)
P_JOBEV		= 416			; jobs.asm : changement pas encore vu par le pere (JEV_*)
P_SID		= 424			; session (herite au lancement et au fork ; jobs.asm)
					; 432-439 : libre
P_PGID		= 440			; groupe de process (SETPGID ; herite au lancement et au fork)
P_NAME		= 448			; 32 octets
P_WIN		= 480			; 1 : a parle au serveur graphique (WS_CALL ; exec.asm)
P_EVENTS	= 488			; masque d'evenements en attente (jalon 6)
P_EVMASK	= 496
P_TEXIT		= 512			; fins des threads (thread_join) : 16 x {tid, code}
P_TEXIT_N	= 16
P_TEXIT_POS	= 768			; prochaine entree (compteur, modulo P_TEXIT_N)
P_KITS		= 776			; kits mappes (bit n : kit n, kits.asm)
P_STDIO		= 784			; bits 0-2 : entree, sortie, erreur standard dans un
					; tube de vfs (docs/FICHIERS.md, etape 4)
P_EVQ		= 792			; file d'evenements (page, ws.asm)
P_FLAGS_WS	= 800			; 1 : framebuffer mappe (serveur graphique)
P_DROITS	= 808			; 1 : programme du systeme (SD0:/sys : services, materiel)
P_DEV_VA	= 816			; prochaine adresse de registres mappes (hardware.asm)
P_DMA_VA	= 824			; prochaine adresse de memoire DMA
P_BUFS		= 832			; tampons partages mappes (bit n, ws.asm :
					; 128 bits, 2 mots)
P_SVC_TAB	= 1024			; appels faits, par numero : P_SVC_TAB_N mots (PROC_STATS)
P_SVC_TAB_N	= 128			;   (jusqu'a 2048 ; le reste de la page : libre)

; -> x0 = PCB vierge avec sa table L1 (0 si plus de memoire)
vm_create:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	pmm_alloc
	cbz	x0,.out
	mov	x19,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
	bl	pmm_alloc
	cbz	x0,.fail
	mov	x20,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
	mov	x0,x20				; L1[0..255] = noyau
	adr	x1,pt_l1
	mov	x2,256*8
	bl	mem_copy
	str	x20,[x19,P_L1]
	bl	asid_alloc			; (recycles : asid_free)
	cbz	x0,.fail_l1
	orr	x1,x20,x0,lsl 48
	str	x1,[x19,P_TTBR]
	mov	x0,x19
	b	.out
.fail_l1:
	mov	x0,x20
	bl	pmm_free
.fail:	mov	x0,x19
	bl	pmm_free
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = table, x1 = index -> x0 = table du niveau suivant (creee si absente), 0 si echec
; (sans verrou : l'entree est posee par echange atomique ; deux coeurs qui la
; creent en meme temps gardent la meme, l'autre table est rendue)
vm_next_table:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x19,x0,x1,lsl 3			; adresse de l'entree
	ldr	x0,[x19]
	tbz	x0,0,.create
.have:	movl	x1,PTE_ADDR_MASK
	and	x0,x0,x1
	b	.out
.create:
	bl	pmm_alloc
	cbz	x0,.out
	mov	x20,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
	dmb	ishst				; table a zero visible avant l'entree
	orr	x1,x20,PTE_TABLE
.cas:	ldaxr	x0,[x19]
	tbnz	x0,0,.lost
	stlxr	w2,x1,[x19]
	cbnz	w2,.cas
	mov	x0,x20
	b	.out
.lost:	clrex					; un autre l'a faite avant
	str	x0,[sp,-16]!
	mov	x0,x20
	bl	pmm_free
	ldr	x0,[sp],16
	b	.have
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = VA -> x0 = adresse de l'entree L3 (tables creees), 0 si echec
vm_pte:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x1
	ldr	x0,[x0,P_L1]
	ubfx	x1,x20,30,9
	bl	vm_next_table
	cbz	x0,.out
	ubfx	x1,x20,21,9
	bl	vm_next_table
	cbz	x0,.out
	ubfx	x1,x20,12,9
	add	x0,x0,x1,lsl 3
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = VA -> x0 = entree L3 existante (ou 0), sans rien creer
vm_lookup:
	ldr	x2,[x0,P_L1]
	movl	x5,PTE_ADDR_MASK
	ubfx	x3,x1,30,9
	ldr	x2,[x2,x3,lsl 3]
	tbz	x2,0,.none
	and	x2,x2,x5
	ubfx	x3,x1,21,9
	ldr	x2,[x2,x3,lsl 3]
	tbz	x2,0,.none
	and	x2,x2,x5
	ubfx	x3,x1,12,9
	add	x0,x2,x3,lsl 3
	ret
.none:	mov	x0,0
	ret

; x0 = PCB, x1 = debut VA, x2 = taille, x3 = attributs
; Alloue et met a zero les pages absentes ; les pages presentes recoivent les
; attributs les plus permissifs. -> x0 = 1 si succes
vm_alloc_range:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	and	x20,x1,not 0xFFF		; premiere page
	add	x21,x1,x2
	add	x21,x21,0xFFF
	and	x21,x21,not 0xFFF		; fin
	mov	x22,x3
.page:	cmp	x20,x21
	b.hs	.ok
	mov	x0,x19
	mov	x1,x20
	bl	vm_pte
	cbz	x0,.fail
	mov	x23,x0
	ldr	x1,[x23]
	tbnz	x1,0,.present
	bl	pmm_alloc
	cbz	x0,.fail
	mov	x24,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
	orr	x0,x24,x22
	str	x0,[x23]
	b	.next
.present:					; fusion : ecriture si demandee, exec si demandee
	tbnz	x22,7,.keep_ap			; AP[2] = lecture seule demandee
	and	x1,x1,not 0x80			; sinon : rendre inscriptible
.keep_ap:
	tbnz	x22,54,.store			; UXN demande : on garde l'existant
	and	x1,x1,not PTE_UXN
.store:	str	x1,[x23]
.next:	add	x20,x20,PAGE_SIZE
	b	.page
.ok:	dsb	ishst
	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = VA, x2 = taille -> x0 = 1 si toute la zone est mappee pour EL0
; (une page absente d'une region paresseuse est peuplee ici : le noyau peut
; ensuite y lire ou y ecrire sans faute fatale ; une page PROT_NONE est
; refusee)
vm_check_user:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	movl	x3,USER_BASE
	cmp	x1,x3
	b.lo	.bad
	adds	x21,x1,x2
	b.cs	.bad
	movl	x3,USER_TOP
	cmp	x21,x3
	b.hi	.bad
	and	x20,x1,not 0xFFF
.page:	cmp	x20,x21
	b.hs	.good
	mov	x0,x19
	mov	x1,x20
	bl	vm_lookup
	cbz	x0,.fill
	ldr	x0,[x0]
	tbz	x0,0,.fill
	tbz	x0,6,.bad			; presente, mais pas pour EL0
.next:	add	x20,x20,PAGE_SIZE
	b	.page
.fill:	mov	x0,x19				; region paresseuse : peuplee
	mov	x1,x20
	mov	x2,VF_WAIT
	bl	vm_fault_do
	cbz	x0,.bad
	b	.next
.good:	mov	x0,1
	b	.out
.bad:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB (0 = tables du noyau seules) : active l'espace d'adressage
vm_activate:
	cbz	x0,.kernel
	ldr	x0,[x0,P_TTBR]
	msr	ttbr0_el1,x0
	isb
	ret
.kernel:
	adr	x0,pt_l1
	msr	ttbr0_el1,x0
	isb
	ret

; x0 = PCB : libere pages et tables de la moitie utilisateur, puis L1 et PCB
vm_destroy:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x24,x0
	ldr	x0,[x24,P_EXECNEW]		; tue pendant un exec : l'espace neuf
	cbz	x0,.noexec			; (exec.asm) part aussi
	str	xzr,[x24,P_EXECNEW]
	bl	vm_destroy
.noexec:
	mov	x0,x24
	bl	ipc_proc_release		; poignees d'IPC (ipc.asm) : canaux
	mov	x0,x24				; fermes, objets rendus au dernier
	bl	img_loader_gone			; ses chargements d'images en cours
	ldr	x0,[x24,P_TTBR]			; (preload.asm) sont rendus ; son ASID
	lsr	x0,x0,48			; (plus aucun coeur ne l'emploie) : TLB
	bl	asid_free			; invalidees, numero rendu
	ldr	x19,[x24,P_L1]
	movl	x23,PTE_ADDR_MASK
	mov	x20,256				; index L1
.l1:	ldr	x0,[x19,x20,lsl 3]
	tbz	x0,0,.l1_next
	and	x21,x0,x23			; table L2
	mov	x22,0
.l2:	ldr	x0,[x21,x22,lsl 3]
	tbz	x0,0,.l2_next
	and	x0,x0,x23			; table L3 : liberer ses pages
	stp	x0,x22,[sp,-16]!
	mov	x22,x0
	mov	x1,0
.l3:	ldr	x0,[x22,x1,lsl 3]
	tbz	x0,0,.l3_next
	tbnz	x0,55,.l3_next			; PTE_SHARED : page d'un kit, gardee
	and	x0,x0,x23
	str	x1,[sp,-16]!
	bl	pmm_free
	ldr	x1,[sp],16
.l3_next:
	add	x1,x1,1
	cmp	x1,512
	b.lo	.l3
	ldp	x0,x22,[sp],16
	bl	pmm_free			; la table L3
.l2_next:
	add	x22,x22,1
	cmp	x22,512
	b.lo	.l2
	mov	x0,x21
	bl	pmm_free			; la table L2
.l1_next:
	add	x20,x20,1
	cmp	x20,512
	b.lo	.l1
	mov	x0,x19
	bl	pmm_free			; L1
	mov	x0,x24				; regions de memoire partagee : une
	bl	ishm_vma_release		; reference de moins par region
	ldr	x0,[x24,P_VMA]			; table des regions
	cbz	x0,.novma
	ldr	x1,[x24,P_VMA_CAP]
	lsr	x1,x1,7
	bl	pmm_free_n
.novma:	ldr	x0,[x24,P_IMAGE]		; image prechargee : une reference
	cbz	x0,.noimg			; de moins (rendue a la derniere)
	bl	image_unref
.noimg:	mov	x0,x24
	bl	pmm_free			; PCB
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  ASID (16 bits, TCR_EL1.AS) : une bitmap de 65 536 bits (asid_map), sous
;  asid_lock. L'ASID 0 reste aux tables du noyau. Un numero rendu (asid_free,
;  vm_destroy : plus aucun coeur ne charge cet espace) a d'abord ses TLB
;  invalidees sur tous les coeurs (TLBI ASIDE1IS, entrees et tables en cache) :
;  le process suivant qui le recoit part de TLB propres. fork puis exec en
;  consomment vite : sans recyclage, 65 535 espaces depuis le demarrage.
; ---------------------------------------------------------------------------
ASID_N		= 65536
ASID_WORDS	= ASID_N / 64

; -> x0 = ASID libre (1..65535), pris ; 0 si tous le sont
asid_alloc:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,asid_lock
	bl	spin_lock_irqsave
	mov	x19,x0
	adr	x1,asid_map
	ldr	x5,[x1]				; l'ASID 0 : jamais donne
	orr	x5,x5,1
	str	x5,[x1]
	adr	x2,asid_hint
	ldr	x3,[x2]
	mov	x4,ASID_WORDS
	mov	x20,0
.w:	and	x3,x3,ASID_WORDS-1
	ldr	x5,[x1,x3,lsl 3]
	cmn	x5,1				; mot plein ?
	b.ne	.got
	add	x3,x3,1
	subs	x4,x4,1
	b.ne	.w
	b	.unl				; (tous pris)
.got:	mvn	x6,x5				; premier bit libre
	rbit	x6,x6
	clz	x6,x6
	mov	x7,1
	lsl	x7,x7,x6
	orr	x5,x5,x7
	str	x5,[x1,x3,lsl 3]
	str	x3,[x2]
	add	x20,x6,x3,lsl 6
.unl:	adr	x0,asid_lock
	mov	x1,x19
	bl	spin_unlock_irqrestore
	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ASID (0 : rien) : TLB de cet ASID invalidees sur tous les coeurs, puis
; le numero redevient libre
asid_free:
	cbz	x0,.ret
	prologue
	str	x19,[sp,-16]!
	and	x19,x0,ASID_N-1
	lsl	x0,x19,48
	dsb	ishst
	tlbi	aside1is,x0
	dsb	ish
	isb
	adr	x0,asid_lock
	bl	spin_lock_irqsave
	mov	x1,x0
	adr	x2,asid_map
	lsr	x3,x19,6
	ldr	x4,[x2,x3,lsl 3]
	and	x5,x19,63
	mov	x6,1
	lsl	x6,x6,x5
	bic	x4,x4,x6
	str	x4,[x2,x3,lsl 3]
	adr	x0,asid_lock
	bl	spin_unlock_irqrestore
	ldr	x19,[sp],16
	epilogue
.ret:	ret

; x0 = debut, x1 = taille : rend du code fraichement ecrit executable
; (nettoie le cache D jusqu'au point d'unification, invalide le cache I)
icache_sync_range:
	add	x1,x0,x1
	bic	x0,x0,63
.line:	dc	cvau,x0
	add	x0,x0,64
	cmp	x0,x1
	b.lo	.line
	dsb	ish
	ic	ialluis
	dsb	ish
	isb
	ret

; x0 = PCB, x1 = VA, x2 = adresse physique, x3 = taille, x4 = attributs
; Mappe des pages physiques existantes (surface de fenetre...) -> x0 = 1 si ok
vm_map_phys:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	add	x22,x1,x3
	mov	x23,x4
.p:	cmp	x20,x22
	b.hs	.ok
	mov	x0,x19
	mov	x1,x20
	bl	vm_pte
	cbz	x0,.fail
	orr	x1,x21,x23
	str	x1,[x0]
	add	x20,x20,PAGE_SIZE
	add	x21,x21,PAGE_SIZE
	b	.p
.ok:	dsb	ishst
	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = VA, x2 = taille, x3 = 1 pour liberer les pages physiques
; Demappe la zone et invalide les TLB de l'ASID du process sur tous les coeurs
vm_unmap_range:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	add	x21,x1,x2
	mov	x22,x3
.p:	cmp	x20,x21
	b.hs	.flush
	mov	x0,x19
	mov	x1,x20
	bl	vm_lookup
	cbz	x0,.next
	ldr	x1,[x0]
	str	xzr,[x0]
	tbz	x1,0,.next
	cbz	x22,.next
	movl	x2,PTE_ADDR_MASK
	and	x0,x1,x2
	bl	pmm_free
.next:	add	x20,x20,PAGE_SIZE
	b	.p
.flush:	dsb	ishst
	ldr	x0,[x19,P_TTBR]
	lsr	x0,x0,48
	lsl	x0,x0,48			; ASID en bits 63:48
	tlbi	aside1is,x0
	dsb	ish
	isb
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
