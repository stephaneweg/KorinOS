; ============================================================================
;  vmspace.asm - regions paresseuses, fautes de page des process, copie sur
;  ecriture, appels memoire 0x06-0x0C (VM_MAP, VM_UNMAP, VM_PROTECT,
;  VM_ADVISE, VM_QUERY, VM_STATS, VM_PAGEINFO) pour la couche POSIX (mmap,
;  munmap, mprotect, madvise)
;
;  Region (VMA) : plage [debut, fin[ reservee mais peuplee a la demande, avec
;  ses droits (PROT_*). Table triee par process (P_VMA, pages du noyau,
;  agrandie par doublement, 4096 regions au plus). Trois sortes :
;    VMK_ANON  : VM_MAP, dans l'arene 0x58_0000_0000 - 0x68_0000_0000 ;
;    VMK_BSS   : la partie d'un segment ELF au-dela du fichier (.bss) ;
;    VMK_STACK : la pile du thread principal (sauf ses 16 Ko du haut).
;  Le reste de l'espace (code et donnees du fichier, tas de sbrk, kits,
;  tampons, DMA, registres) est peuple d'avance, comme avant.
;
;  Faute de page (vectors.asm : EL0, et EL1 a une adresse EL0 pendant un
;  appel) : vm_fault_esr. Sous P_VMLOCK (spinlock irqsave : pas d'attente,
;  ni journal, ni entree / sortie ; seul pmm_lock est pris en dessous) :
;    * page absente dans une region dont les droits permettent l'acces : une
;      page a zero (zone haute de preference), mappee ; l'acces reprend ;
;    * ecriture sur une page PTE_COW (segment lecture / ecriture d'une image
;      prechargee, preload.asm ; region VM_COW, si elle permet l'ecriture) :
;      copie dans une page a soi, puis remplacement
;      par rupture avant construction (entree a 0, TLBI VAE1IS de l'ASID sur
;      tous les coeurs, nouvelle entree) ; la page de l'image n'est pas a lui
;      (PTE_SHARED) et reste ;
;    * entree deja bonne (un autre coeur l'a remplie, TLB d'avant un
;      mprotect) : TLBI de la page sur ce coeur, l'acces reprend ;
;    * sinon : echec (le process est tue, comme avant).
;  Plus de memoire libre : hors du verrou, mem_wait (pmm.asm) demande aux
;  services d'en rendre, puis la faute recommence ; d'EL0, ou avec VF_WAIT
;  (check_user, VM_POPULATE) si les IRQ sont permises, donc sans spinlock tenu.
;  Le noyau qui lit ou ecrit dans un tampon du process passe par check_user
;  (vm_check_user peuple les pages absentes des regions) ; une ecriture du
;  noyau dans une page en copie sur ecriture est resolue par la faute EL1.
;
;  TLB : absente -> valide : DSB ISHST seul (une entree invalide n'est pas
;  gardee) ; valide -> autre (unmap, protect, DONTNEED) : entree changee, puis
;  TLBI ASIDE1IS (tous les coeurs), DSB ISH, et seulement ensuite les pages
;  rendues.
; ============================================================================

; (PROT_*, VMK_*, VM_FIXED..., MADV_*, PI_*, VM_ARENA_* : src/inc/syscalls.inc)
VMA_START	= 0
VMA_END		= 8
VMA_PROT	= 16			; 32 bits
VMP_COW		= 0x100			; (dans VMA_PROT) region VMK_SHARED : VM_COW
VMA_KIND	= 20			; 32 bits
VMA_SIZE	= 32
VMA_MAX		= 4096
VMA_FIRST_CAP	= 128			; une page
STACK_EAGER	= 0x4000		; haut de la pile peuple d'avance (arguments)
; drapeaux d'une faute
VF_WRITE	= 1
VF_EXEC		= 2
VF_EL0		= 4
VF_WAIT		= 8			; plus de page : peut attendre mem_wait, si les IRQ sont permises
E_EXIST_VM	= -17
E_SRCH		= -3

VM_BASE_ATTR	= PTE_PAGE or (MAIR_NORMAL shl 2) or PTE_SH_INNER or PTE_AF or PTE_NG \
		  or PTE_PXN

; x0 = droits PROT_* -> x0 = attributs d'une page (PROT_NONE : AP = 10, EL1
; lecture seule, EL0 rien ; lecture : 11 ; ecriture : 01 ; sans EXEC : UXN)
vm_prot_attrs:
	mov	x1,x0
	movl	x0,VM_BASE_ATTR
	mov	x2,0x80
	mov	x3,0xC0
	mov	x4,PROT_READ or PROT_EXEC
	tst	x1,x4
	csel	x2,x3,x2,ne
	mov	x3,0x40
	tst	x1,PROT_WRITE
	csel	x2,x3,x2,ne
	orr	x0,x0,x2
	tst	x1,PROT_EXEC
	b.ne	.x
	orr	x0,x0,PTE_UXN
.x:	ret

; ---------------------------------------------------------------------------
;  Table des regions (sous P_VMLOCK, ou process pas encore visible)
; ---------------------------------------------------------------------------

; x0 = PCB, x1 = adresse -> x0 = region qui la contient (0 sinon)
vma_find:
	ldr	x2,[x0,P_VMA]
	ldr	x3,[x0,P_VMA_N]
	mov	x4,0
	cbz	x2,.none
.l:	cmp	x4,x3
	b.hs	.none
	add	x5,x4,x3
	lsr	x5,x5,1
	add	x6,x2,x5,lsl 5
	ldp	x7,x8,[x6]
	cmp	x1,x7
	b.lo	.left
	cmp	x1,x8
	b.hs	.right
	mov	x0,x6
	ret
.left:	mov	x3,x5
	b	.l
.right:	add	x4,x5,1
	b	.l
.none:	mov	x0,0
	ret

; x0 = PCB, x1 = adresse -> x0 = rang de la premiere region qui finit
; apres elle (= nombre de regions si aucune)
vma_lower:
	ldr	x2,[x0,P_VMA]
	ldr	x3,[x0,P_VMA_N]
	mov	x4,0
.l:	cmp	x4,x3
	b.hs	.out
	add	x5,x4,x3
	lsr	x5,x5,1
	add	x6,x2,x5,lsl 5
	ldr	x7,[x6,VMA_END]
	cmp	x7,x1
	b.hi	.left
	add	x4,x5,1
	b	.l
.left:	mov	x3,x5
	b	.l
.out:	mov	x0,x4
	ret

; x0 = PCB -> x0 = 1 si la table a de la place pour une region de plus
vma_room:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,P_VMA_N]
	ldr	x20,[x19,P_VMA_CAP]
	cmp	x0,x20
	b.lo	.ok
	lsl	x21,x20,1			; doublement (128 a la premiere)
	cbnz	x20,.cap
	mov	x21,VMA_FIRST_CAP
.cap:	cmp	x21,VMA_MAX
	b.hi	.no
	lsr	x0,x21,7			; pages (32 o par region)
	bl	pmm_alloc_n
	cbz	x0,.no
	mov	x22,x0
	ldr	x1,[x19,P_VMA]			; recopie
	ldr	x2,[x19,P_VMA_N]
	lsl	x2,x2,5
	bl	mem_copy
	ldr	x0,[x19,P_VMA]
	str	x22,[x19,P_VMA]
	str	x21,[x19,P_VMA_CAP]
	cbz	x0,.ok
	lsr	x1,x20,7
	bl	pmm_free_n
.ok:	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = rang, x2 = debut, x3 = fin, x4 = droits | sorte << 32
; (vma_insert_at5 : x5 = mot +24, l'objet d'une region VMK_SHARED ; sa
; reference passe a la region) -> x0 = 1 (0 : plus de place)
vma_insert_at:
	mov	x5,0
vma_insert_at5:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x24,x5
	bl	vma_room
	cbz	x0,.out
	ldr	x0,[x19,P_VMA]
	ldr	x1,[x19,P_VMA_N]
	mov	x2,x1				; decalage d'une place vers le haut
.mv:	cmp	x2,x20
	b.ls	.put
	sub	x3,x2,1
	add	x4,x0,x3,lsl 5
	add	x5,x0,x2,lsl 5
	ldp	x6,x7,[x4]
	stp	x6,x7,[x5]
	ldp	x6,x7,[x4,16]
	stp	x6,x7,[x5,16]
	mov	x2,x3
	b	.mv
.put:	add	x4,x0,x20,lsl 5
	stp	x21,x22,[x4]
	stp	x23,x24,[x4,16]
	add	x1,x1,1
	str	x1,[x19,P_VMA_N]
	mov	x0,1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = rang : retire cette region
vma_delete_at:
	ldr	x2,[x0,P_VMA]
	ldr	x3,[x0,P_VMA_N]
	sub	x3,x3,1
	str	x3,[x0,P_VMA_N]
.mv:	cmp	x1,x3
	b.hs	.out
	add	x4,x2,x1,lsl 5
	ldp	x5,x6,[x4,32]
	stp	x5,x6,[x4]
	ldp	x5,x6,[x4,48]
	stp	x5,x6,[x4,16]
	add	x1,x1,1
	b	.mv
.out:	ret

; x0 = PCB, x1 = adresse : une region qui la contient strictement est coupee
; en deux a cette adresse -> x0 = 1 (0 : plus de place). Memoire partagee
; (VMK_SHARED) : la seconde moitie garde l'objet, sa page de depart avance,
; une reference et une projection de plus (ipc.asm)
vma_split:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	vma_lower
	ldr	x1,[x19,P_VMA_N]
	cmp	x0,x1
	b.hs	.ok
	mov	x21,x0
	ldr	x2,[x19,P_VMA]
	add	x2,x2,x21,lsl 5
	ldr	x3,[x2,VMA_START]
	cmp	x3,x20
	b.hs	.ok				; elle commence la ou apres
	ldr	x3,[x2,VMA_END]			; [adresse, fin[ ajoutee apres elle,
	ldr	x4,[x2,VMA_PROT]		; (droits | sorte << 32)
	ldr	x5,[x2,24]
	ldrb	w6,[x2,VMA_KIND]
	cmp	w6,VMK_SHARED
	b.ne	.ins
	ldr	x6,[x2,VMA_START]		; page de l'objet : + (adresse - debut)
	sub	x6,x20,x6
	lsr	x6,x6,12
	add	x4,x4,x6,lsl 40			; (bits 8-31 de la sorte)
.ins:	mov	x0,x19
	add	x1,x21,1
	mov	x2,x20
	bl	vma_insert_at5
	cbz	x0,.out				; (plus de place : rien n'a change)
	ldr	x2,[x19,P_VMA]			; puis elle finit a l'adresse
	add	x2,x2,x21,lsl 5
	str	x20,[x2,VMA_END]
	ldrb	w6,[x2,VMA_KIND]
	cmp	w6,VMK_SHARED
	b.ne	.ok
	ldr	x0,[x2,24]
	bl	ishm_mapped
.ok:	mov	x0,1
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = debut, x2 = fin : les regions VMK_ANON de memes droits qui se
; touchent dans [debut, fin], bords compris, n'en font plus qu'une. Sans cela,
; chaque mprotect d'une reservation (un tas, du code JIT) ajoute des regions
; qui ne s'en vont jamais, jusqu'a VMA_MAX. Les tables de pages ne changent pas.
vma_merge:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	bl	vma_lower
	subs	x21,x0,1			; (celle d'avant peut finir au debut)
	csel	x21,xzr,x21,lo
.l:	ldr	x1,[x19,P_VMA_N]
	add	x2,x21,1
	cmp	x2,x1
	b.hs	.out
	ldr	x3,[x19,P_VMA]
	add	x3,x3,x21,lsl 5
	ldp	x4,x5,[x3]			; celle-ci : debut, fin
	cmp	x4,x20
	b.hs	.out
	ldp	x6,x7,[x3,32]			; la suivante : debut, fin
	cmp	x5,x6
	b.ne	.next
	ldrb	w4,[x3,VMA_KIND]
	cmp	w4,VMK_ANON
	b.ne	.next
	ldr	x4,[x3,VMA_PROT]		; droits | sorte << 32
	ldr	x5,[x3,32+VMA_PROT]
	cmp	x4,x5
	b.ne	.next
	str	x7,[x3,VMA_END]
	mov	x0,x19
	add	x1,x21,1
	bl	vma_delete_at
	b	.l
.next:	add	x21,x21,1
	b	.l
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Tables de pages
; ---------------------------------------------------------------------------

; x0 = PCB, x1 = adresse -> x0 = table L3 qui la couvre (0 si absente),
; x1 = fin du bloc de 2 Mo (ou du Go si la table L2 manque)
vm_l3_of:
	ldr	x2,[x0,P_L1]
	movl	x5,PTE_ADDR_MASK
	ubfx	x3,x1,30,9
	ldr	x2,[x2,x3,lsl 3]
	tbz	x2,0,.nol1
	and	x2,x2,x5
	ubfx	x3,x1,21,9
	ldr	x2,[x2,x3,lsl 3]
	and	x0,x2,x5
	tbnz	x2,0,.end2
	mov	x0,0
.end2:	lsr	x1,x1,21
	add	x1,x1,1
	lsl	x1,x1,21
	ret
.nol1:	mov	x0,0
	lsr	x1,x1,30
	add	x1,x1,1
	lsl	x1,x1,30
	ret

; x0 = PCB : TLB de son ASID invalides sur tous les coeurs
vm_flush_asid:
	ldr	x0,[x0,P_TTBR]
	lsr	x0,x0,48
	lsl	x0,x0,48
	dsb	ishst
	tlbi	aside1is,x0
	dsb	ish
	isb
	ret

ZAP_BATCH	= 32
; (sous P_VMLOCK) x0 = PCB, x1 = debut, x2 = fin : entrees de [debut, fin[
; mises a 0, TLB invalides, puis les pages a lui rendues (pas celles des
; images, kits, tampons : PTE_SHARED)
vm_zap_range:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,ZAP_BATCH*8
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x24,0				; pages en attente
.blk:	cmp	x20,x21
	b.hs	.end
	mov	x0,x19
	mov	x1,x20
	bl	vm_l3_of
	cmp	x1,x21
	csel	x22,x1,x21,lo			; fin de ce bloc
	cbnz	x0,.l3
	mov	x20,x22
	b	.blk
.l3:	mov	x23,x0
.pg:	cmp	x20,x22
	b.hs	.blk
	ubfx	x1,x20,12,9
	add	x2,x23,x1,lsl 3
	ldr	x3,[x2]
	add	x20,x20,PAGE_SIZE
	tbz	x3,0,.pg
	str	xzr,[x2]
	tbnz	x3,55,.pg			; PTE_SHARED : pas a lui
	movl	x4,PTE_ADDR_MASK
	and	x3,x3,x4
	str	x3,[sp,x24,lsl 3]
	add	x24,x24,1
	cmp	x24,ZAP_BATCH
	b.lo	.pg
	mov	x0,x19
	mov	x1,sp
	mov	x2,x24
	bl	vm_zap_flush
	mov	x24,0
	b	.pg
.end:	mov	x0,x19				; (TLB invalides meme sans page a rendre)
	mov	x1,sp
	mov	x2,x24
	bl	vm_zap_flush
	add	sp,sp,ZAP_BATCH*8
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = pages, x2 = nombre : TLB invalides, puis pages rendues
vm_zap_flush:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	bl	vm_flush_asid
.f:	cbz	x20,.out
	ldr	x0,[x19],8
	bl	pmm_free
	sub	x20,x20,1
	b	.f
.out:	ldp	x19,x20,[sp],16
	epilogue

; (sous P_VMLOCK) x0 = PCB, x1 = debut, x2 = fin, x3 = droits : nouvelles
; protections des pages presentes, puis TLB invalides
vm_protect_ptes:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,x3
	bl	vm_prot_attrs
	mov	x24,x0				; nouveaux attributs
.blk:	cmp	x20,x21
	b.hs	.end
	mov	x0,x19
	mov	x1,x20
	bl	vm_l3_of
	cmp	x1,x21
	csel	x22,x1,x21,lo
	cbnz	x0,.l3
	mov	x20,x22
	b	.blk
.l3:	mov	x23,x0
.pg:	cmp	x20,x22
	b.hs	.blk
	ubfx	x1,x20,12,9
	add	x2,x23,x1,lsl 3
	ldr	x3,[x2]
	add	x20,x20,PAGE_SIZE
	tbz	x3,0,.pg
	tst	x24,PTE_UXN			; page qui devient executable : ecrite
	b.ne	.attr				; peut-etre par un autre coeur, et le
	tst	x3,PTE_UXN			; Cortex-A72 a IDC = 0 (ic ialluis ne
	b.eq	.attr				; suffit pas, plus bas)
	movl	x0,PTE_ADDR_MASK
	and	x0,x3,x0
	mov	x1,PAGE_SIZE
	bl	dcache_clean_pou
.attr:	movl	x4,PTE_ADDR_MASK or (1 shl 55) or (1 shl 56)	; adresse, PTE_SHARED,
	and	x3,x3,x4			; PTE_COW gardes : une page partagee ne
	orr	x3,x3,x24			; devient jamais inscriptible
	tbz	x3,56,.st
	orr	x3,x3,1 shl 7			; (AP[2] : lecture seule)
.st:	str	x3,[x2]
	b	.pg
.end:	mov	x0,x19
	bl	vm_flush_asid
	tst	x24,PTE_UXN			; executable : caches d'instructions
	b.ne	.out
	ic	ialluis
	dsb	ish
	isb
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Fautes de page
; ---------------------------------------------------------------------------

; x0 = PCB, x1 = adresse fautive, x2 = ESR, x3 = 1 si la faute vient d'EL0
; -> x0 = 1 si resolue (l'acces peut reprendre)
vm_fault_esr:
	lsr	x4,x2,26			; classe : 0x24 / 0x25 donnees,
	and	x5,x2,0x3F			; 0x20 / 0x21 instruction
	ubfx	x6,x5,2,4			; 1 traduction, 3 permission
	cmp	x6,1
	b.eq	.kind
	cmp	x6,3
	b.ne	.no
.kind:	mov	x6,0
	cbz	x3,.el
	orr	x6,x6,VF_EL0
.el:	cmp	x4,0x20				; instruction (EL0)
	b.ne	.data
	orr	x6,x6,VF_EXEC
	b	.go
.data:	cmp	x4,0x24
	b.eq	.d
	cmp	x4,0x25
	b.ne	.no
.d:	tbnz	x2,8,.go			; CM : maintenance de cache = lecture
	tbz	x2,6,.go			; WnR
	orr	x6,x6,VF_WRITE
.go:	mov	x2,x6
	b	vm_fault_do
.no:	mov	x0,0
	ret

; x0 = PCB, x1 = adresse, x2 = drapeaux VF_* -> x0 = 1 si resolue
vm_fault_do:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	movl	x3,USER_BASE			; moitie EL0 seulement
	cmp	x20,x3
	b.lo	.no
	movl	x3,USER_TOP
	cmp	x20,x3
	b.hs	.no
.again:	add	x0,x19,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	vm_fault_locked
	mov	x23,x0
	add	x0,x19,P_VMLOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x23
	cbnz	x0,.out
	ldr	x0,[x19,P_VM_OOM]
	cbz	x0,.no
	tbnz	x21,2,.wait			; VF_EL0
	tbz	x21,3,.no			; VF_WAIT
	mrs	x0,daif
	tbnz	x0,7,.no			; (IRQ masquees : un verrou est peut-etre tenu)
.wait:	bl	mem_wait
	cbz	x0,.no
	str	xzr,[x19,P_VM_OOM]
	b	.again
.no:	mov	x0,0
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sous P_VMLOCK) x0 = PCB, x1 = adresse, x2 = drapeaux -> x0 = 1 si resolue
vm_fault_locked:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	and	x20,x1,not 0xFFF
	mov	x21,x2
	mov	x0,x19
	mov	x1,x20
	bl	vm_lookup
	cbz	x0,.absent
	mov	x22,x0				; entree
	ldr	x23,[x22]
	tbz	x23,0,.absent
	; --- page presente : acces deja permis (faute perimee) ? ------------
	ubfx	x4,x23,6,2			; AP
	tbz	x21,2,.el1			; VF_EL0
	tbnz	x21,0,.w0
	tbnz	x21,1,.x0
	tbz	x4,0,.cow			; lecture EL0 : AP = 01 ou 11
	b	.stale
.w0:	cmp	x4,1				; ecriture EL0 : AP = 01
	b.ne	.cow
	b	.stale
.x0:	tbnz	x23,54,.no			; execution EL0 : sans UXN
	tbz	x4,0,.no
	b	.stale
.el1:	tbz	x21,0,.stale			; lecture EL1 : toujours permise
	tbnz	x4,1,.cow			; ecriture EL1 : AP = 00 ou 01
.stale:	ldr	x0,[x19,P_TTBR]			; TLB perimee sur ce coeur
	lsr	x0,x0,48
	lsl	x0,x0,48
	orr	x0,x0,x20,lsr 12
	dsb	ishst
	tlbi	vale1,x0
	dsb	nsh
	isb
	b	.yes
.cow:	tbz	x21,0,.no			; ecriture sur une page PTE_COW
	tbz	x23,56,.no
	mov	x0,x19				; (une region : si elle le permet)
	mov	x1,x20
	bl	vma_find
	cbz	x0,.copie
	ldr	w1,[x0,VMA_PROT]
	tbz	w1,1,.no
.copie:	bl	pmm_alloc
	cbz	x0,.oom
	mov	x24,x0
	movl	x1,PTE_ADDR_MASK
	and	x1,x23,x1
	mov	x2,PAGE_SIZE
	bl	mem_copy
	movl	x1,PTE_ADDR_MASK or (1 shl 55) or (1 shl 56) or 0xC0
	bic	x23,x23,x1			; a lui, inscriptible
	orr	x23,x23,x24
	orr	x23,x23,PTE_AP_EL0
	tbnz	x23,54,.cnx			; executable : caches d'instructions
	mov	x0,x24
	mov	x1,PAGE_SIZE
	bl	dcache_clean_pou
	ic	ialluis
	dsb	ish
.cnx:	dmb	ishst
	str	xzr,[x22]			; rupture avant construction
	dsb	ishst
	ldr	x0,[x19,P_TTBR]
	lsr	x0,x0,48
	lsl	x0,x0,48
	orr	x0,x0,x20,lsr 12
	tlbi	vae1is,x0
	dsb	ish
	str	x23,[x22]
	dsb	ishst
	isb
	ldr	x0,[x19,P_COWS]
	add	x0,x0,1
	str	x0,[x19,P_COWS]
	b	.yes
	; --- page absente : une region paresseuse qui permet l'acces ? -------
.absent:
	mov	x0,x19
	mov	x1,x20
	bl	vma_find
	cbz	x0,.no
	ldr	w23,[x0,VMA_PROT]
	tbz	x21,0,.nw
	tbz	w23,1,.no			; ecriture : PROT_WRITE
.nw:	tbz	x21,1,.nx
	tbz	w23,2,.no			; execution : PROT_EXEC
.nx:	tst	w23,7				; PROT_NONE : rien
	b.eq	.no
	ldrb	w1,[x0,VMA_KIND]
	cmp	w1,VMK_SHARED
	b.eq	.shared
	bl	pmm_alloc
	cbz	x0,.oom
	mov	x24,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
	tbz	w23,2,.zx			; executable : caches d'instructions
	mov	x0,x24
	mov	x1,PAGE_SIZE
	bl	dcache_clean_pou
	ic	ialluis
	dsb	ish
.zx:	mov	x0,x19
	mov	x1,x20
	bl	vm_pte				; (tables creees au besoin)
	cbz	x0,.oom_free
	mov	x22,x0
	mov	x0,x23
	bl	vm_prot_attrs
	orr	x0,x0,x24
	dmb	ishst				; page a zero visible avant l'entree
	str	x0,[x22]
	dsb	ishst
	ldr	x0,[x19,P_FAULTS]
	add	x0,x0,1
	str	x0,[x19,P_FAULTS]
	b	.yes
	; --- memoire partagee : la page de l'objet (creee a zero au premier
	; acces, ipc.asm), pas a lui (PTE_SHARED) ; au-dela de l'objet : echec
.shared:
	ldr	w1,[x0,VMA_KIND]
	lsr	w1,w1,8				; page de l'objet au debut de la region
	ldr	x2,[x0,VMA_START]
	sub	x2,x20,x2
	add	x1,x1,x2,lsr 12
	ldr	x0,[x0,24]
	bl	ishm_page
	cbz	x0,.no
	mov	x24,x0
	tbz	w23,2,.sx			; executable : caches d'instructions
	mov	x1,PAGE_SIZE
	bl	dcache_clean_pou
	ic	ialluis
	dsb	ish
.sx:	mov	x0,x19
	mov	x1,x20
	bl	vm_pte
	cbz	x0,.oom
	mov	x22,x0
	and	x0,x23,7
	tbz	w23,8,.satt			; VM_COW : en lecture, copiee a l'ecriture
	and	x0,x0,not PROT_WRITE
.satt:	bl	vm_prot_attrs
	orr	x0,x0,x24
	orr	x0,x0,PTE_SHARED
	tbz	w23,8,.spte
	orr	x0,x0,PTE_COW
.spte:	dmb	ishst
	str	x0,[x22]
	dsb	ishst
	ldr	x0,[x19,P_FAULTS]
	add	x0,x0,1
	str	x0,[x19,P_FAULTS]
.yes:	mov	x0,1
	b	.out
.oom_free:
	mov	x0,x24
	bl	pmm_free
.oom:	mov	x0,1				; plus de memoire : note pour le
	str	x0,[x19,P_VM_OOM]		; message de user_fault
.no:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = debut, x2 = fin, x3 = drapeaux VF_* : chaque page absente
; d'une region peuplee (VM_POPULATE, MADV_WILLNEED, check_user) -> x0 = 1 si
; tout est la
vm_populate:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	and	x20,x1,not 0xFFF
	mov	x21,x2
	mov	x22,x3
.p:	cmp	x20,x21
	b.hs	.ok
	mov	x0,x19
	mov	x1,x20
	bl	vm_lookup
	cbz	x0,.f
	ldr	x0,[x0]
	tbnz	x0,0,.n
.f:	mov	x0,x19
	mov	x1,x20
	mov	x2,x22
	bl	vm_fault_do
	cbz	x0,.out
.n:	add	x20,x20,PAGE_SIZE
	b	.p
.ok:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Appels memoire (0x06-0x0C)
; ---------------------------------------------------------------------------

; x0 = adresse, x1 = taille (alignees) -> x0 = 1 si [x0, x0 + x1[ est dans
; l'arene de VM_MAP
vm_in_arena:
	cbz	x1,.no
	movl	x2,VM_ARENA_BASE
	cmp	x0,x2
	b.lo	.no
	adds	x3,x0,x1
	b.cs	.no
	movl	x2,VM_ARENA_END
	cmp	x3,x2
	b.hi	.no
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; (sous P_VMLOCK) x0 = PCB, x1 = debut, x2 = fin -> x0 = 1 si une region
; recouvre [debut, fin[
vma_overlaps:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	bl	vma_lower
	ldr	x1,[x19,P_VMA_N]
	cmp	x0,x1
	b.hs	.no
	ldr	x1,[x19,P_VMA]
	add	x1,x1,x0,lsl 5
	ldr	x1,[x1,VMA_START]
	cmp	x1,x20
	cset	x0,lo
	b	.out
.no:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; (sous P_VMLOCK) x0 = PCB, x1 = indication, x2 = taille -> x0 = debut
; d'un trou de l'arene (0 : aucun). L'indication si elle est libre, sinon
; le premier trou depuis le bas de l'arene.
vm_find_gap:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbz	x20,.scan
	tst	x20,0xFFF
	b.ne	.scan
	mov	x0,x20
	mov	x1,x21
	bl	vm_in_arena
	cbz	x0,.scan
	mov	x0,x19
	mov	x1,x20
	add	x2,x20,x21
	bl	vma_overlaps
	cbnz	x0,.scan
	mov	x0,x20
	b	.out
.scan:	movl	x20,VM_ARENA_BASE		; courant
	mov	x0,x19
	mov	x1,x20
	bl	vma_lower
	ldr	x1,[x19,P_VMA]
	ldr	x2,[x19,P_VMA_N]
.r:	cmp	x0,x2
	b.hs	.last
	add	x3,x1,x0,lsl 5
	ldp	x4,x5,[x3]			; debut, fin
	add	x6,x20,x21
	cmp	x4,x6
	b.hs	.last				; le trou avant elle suffit
	cmp	x5,x20
	csel	x20,x5,x20,hi
	add	x0,x0,1
	b	.r
.last:	mov	x0,x20
	mov	x1,x21
	bl	vm_in_arena
	cbz	x0,.out
	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (sous P_VMLOCK) x0 = PCB, x1 = debut, x2 = fin : regions VMK_ANON et
; VMK_SHARED de [debut, fin[ retirees (coupees aux bords), pages rendues
; (entrees effacees et TLB invalides avant que la derniere reference d'un
; objet partage ne rende les siennes) -> x0 = 1 (0 : plus de place pour
; couper)
vm_unmap_locked:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x1,x20
	bl	vma_split
	cbz	x0,.out
	mov	x0,x19
	mov	x1,x21
	bl	vma_split
	cbz	x0,.out
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	vm_zap_range
	mov	x0,x19
	mov	x1,x20
	bl	vma_lower
	mov	x22,x0
.r:	ldr	x1,[x19,P_VMA_N]
	cmp	x22,x1
	b.hs	.done
	ldr	x1,[x19,P_VMA]
	add	x1,x1,x22,lsl 5
	ldr	x2,[x1,VMA_START]
	cmp	x2,x21
	b.hs	.done
	ldrb	w2,[x1,VMA_KIND]
	cmp	w2,VMK_ANON
	b.eq	.del
	cmp	w2,VMK_SHARED
	b.ne	.skip
	ldr	x0,[x1,24]			; objet partage : sa reference
	bl	ishm_unmapped
.del:	mov	x0,x19
	mov	x1,x22
	bl	vma_delete_at
	b	.r
.skip:	add	x22,x22,1
	b	.r
.done:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_VM_MAP vm_map(adresse, taille, droits, drapeaux) -> adresse --------
; Region a zero, peuplee a la demande, dans l'arene 0x58_0000_0000 -
; 0x68_0000_0000. Adresse : indication, sauf VM_FIXED (remplace ce qui y
; est) et VM_FIXED_NOREPLACE (E_EXIST si occupe). VM_POPULATE : peuplee tout
; de suite. Droits PROT_NONE (reservation) a PROT_READ | WRITE | EXEC.
sys_vm_map:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0				; adresse
	mov	x21,x1				; taille
	mov	x22,x2				; droits
	mov	x23,x3				; drapeaux
	cbz	x21,.inval
	tst	x22,not 7
	b.ne	.inval
	movl	x0,VM_ARENA_END - VM_ARENA_BASE
	cmp	x21,x0
	b.hi	.nomem
	add	x21,x21,0xFFF
	and	x21,x21,not 0xFFF
	current_proc
	mov	x19,x1
	movl	x0,VM_FIXED or VM_FIXED_NOREPLACE
	tst	x23,x0
	b.eq	.lock
	tst	x20,0xFFF			; fixe : alignee, dans l'arene
	b.ne	.inval
	mov	x0,x20
	mov	x1,x21
	bl	vm_in_arena
	cbz	x0,.inval
.lock:	add	x0,x19,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x24,x0
	movl	x0,VM_FIXED or VM_FIXED_NOREPLACE
	tst	x23,x0
	b.eq	.gap
	tbz	x23,20,.repl			; VM_FIXED_NOREPLACE
	mov	x0,x19
	mov	x1,x20
	add	x2,x20,x21
	bl	vma_overlaps
	mov	x1,E_EXIST_VM
	cbnz	x0,.unlock_err
	b	.ins
.repl:	mov	x0,x19				; VM_FIXED : l'ancien contenu part
	mov	x1,x20
	add	x2,x20,x21
	bl	vm_unmap_locked
	mov	x1,E_NOMEM
	cbz	x0,.unlock_err
	b	.ins
.gap:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	vm_find_gap
	mov	x1,E_NOMEM
	cbz	x0,.unlock_err
	mov	x20,x0
.ins:	mov	x0,x19
	mov	x1,x20
	bl	vma_lower
	mov	x1,x0
	mov	x0,x19
	mov	x2,x20
	add	x3,x20,x21
	mov	x4,VMK_ANON
	lsl	x4,x4,32
	orr	x4,x4,x22
	bl	vma_insert_at
	mov	x1,E_NOMEM
	cbz	x0,.unlock_err
	mov	x0,x19
	mov	x1,x20
	add	x2,x20,x21
	bl	vma_merge
	add	x0,x19,P_VMLOCK
	mov	x1,x24
	bl	spin_unlock_irqrestore
	tbz	x23,15,.done			; VM_POPULATE (echec : pas une erreur)
	cbz	x22,.done
	mov	x0,x19
	mov	x1,x20
	add	x2,x20,x21
	mov	x3,VF_WAIT
	bl	vm_populate
.done:	mov	x0,x20
	b	.out
.unlock_err:
	mov	x20,x1
	add	x0,x19,P_VMLOCK
	mov	x1,x24
	bl	spin_unlock_irqrestore
	mov	x0,x20
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_VM_UNMAP vm_unmap(adresse, taille) -> 0 ----------------------------
; Retire les regions de VM_MAP de la plage (coupees aux bords ; trous
; permis), rend leurs pages. Hors de l'arene : E_INVAL.
sys_vm_unmap:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x0
	add	x21,x1,0xFFF
	and	x21,x21,not 0xFFF
	tst	x20,0xFFF
	b.ne	.inval
	mov	x0,x20
	mov	x1,x21
	bl	vm_in_arena
	cbz	x0,.inval
	current_proc
	mov	x19,x1
	add	x0,x19,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,x19
	mov	x1,x20
	add	x2,x20,x21
	bl	vm_unmap_locked
	mov	x20,x0
	add	x0,x19,P_VMLOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,0
	cbnz	x20,.out
	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_VM_PROTECT vm_protect(adresse, taille, droits) -> 0 ----------------
; La plage doit etre couverte sans trou par des regions de VM_MAP (sinon
; E_INVAL, rien ne change) ; pages presentes reprotegees, TLB invalides.
sys_vm_protect:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0
	add	x21,x1,0xFFF
	and	x21,x21,not 0xFFF
	mov	x23,x2
	tst	x23,not 7
	b.ne	.inval
	tst	x20,0xFFF
	b.ne	.inval
	mov	x0,x20
	mov	x1,x21
	bl	vm_in_arena
	cbz	x0,.inval
	add	x21,x20,x21			; fin
	current_proc
	mov	x19,x1
	add	x0,x19,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x24,x0
	mov	x0,x19				; couverture sans trou ?
	mov	x1,x20
	bl	vma_lower
	mov	x22,x20				; courant
	ldr	x1,[x19,P_VMA]
	ldr	x2,[x19,P_VMA_N]
.cov:	cmp	x22,x21
	b.hs	.covered
	cmp	x0,x2
	b.hs	.hole
	add	x3,x1,x0,lsl 5
	ldr	x4,[x3,VMA_START]
	cmp	x4,x22
	b.hi	.hole
	ldrb	w4,[x3,VMA_KIND]
	cmp	w4,VMK_ANON
	b.eq	.kind_ok
	cmp	w4,VMK_SHARED
	b.ne	.hole
.kind_ok:
	ldr	x22,[x3,VMA_END]
	add	x0,x0,1
	b	.cov
.covered:
	mov	x0,x19
	mov	x1,x20
	bl	vma_split
	cbz	x0,.nomem
	mov	x0,x19
	mov	x1,x21
	bl	vma_split
	cbz	x0,.nomem
	mov	x0,x19
	mov	x1,x20
	bl	vma_lower
	ldr	x1,[x19,P_VMA]
	ldr	x2,[x19,P_VMA_N]
.set:	cmp	x0,x2
	b.hs	.ptes
	add	x3,x1,x0,lsl 5
	ldr	x4,[x3,VMA_START]
	cmp	x4,x21
	b.hs	.ptes
	ldr	w4,[x3,VMA_PROT]		; (VM_COW reste)
	and	w4,w4,VMP_COW
	orr	w4,w4,w23
	str	w4,[x3,VMA_PROT]
	add	x0,x0,1
	b	.set
.ptes:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x23
	bl	vm_protect_ptes
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	vma_merge
	mov	x20,0
	b	.unlock
.hole:	mov	x20,E_INVAL
	b	.unlock
.nomem:	mov	x20,E_NOMEM
.unlock:
	add	x0,x19,P_VMLOCK
	mov	x1,x24
	bl	spin_unlock_irqrestore
	mov	x0,x20
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_VM_ADVISE vm_advise(adresse, taille, conseil) -> 0 -----------------
; MADV_WILLNEED (3) : peuple (E_NOMEM) ; MADV_DONTNEED (4) / MADV_FREE (8) :
; pages rendues, a zero au prochain acces (regions paresseuses seulement :
; E_INVAL sinon) ; autres conseils : rien.
sys_vm_advise:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x20,x0
	add	x21,x1,0xFFF
	and	x21,x21,not 0xFFF
	mov	x23,x2
	tst	x20,0xFFF
	b.ne	.inval
	mov	x0,0
	cbz	x21,.out
	adds	x21,x20,x21
	b.cs	.inval
	current_proc
	mov	x19,x1
	cmp	x23,MADV_WILLNEED
	b.eq	.will
	cmp	x23,MADV_DONTNEED
	b.eq	.dont
	cmp	x23,MADV_FREE
	b.eq	.dont
	mov	x0,0
	b	.out
.will:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,0
	bl	vm_populate
	cmp	x0,0
	mov	x1,E_NOMEM
	csel	x0,x1,xzr,eq
	b	.out
.dont:	add	x0,x19,P_VMLOCK			; toute la plage dans des regions ?
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,x19
	mov	x1,x20
	bl	vma_lower
	mov	x4,x20
	ldr	x1,[x19,P_VMA]
	ldr	x2,[x19,P_VMA_N]
.cov:	cmp	x4,x21
	b.hs	.zap
	cmp	x0,x2
	b.hs	.hole
	add	x3,x1,x0,lsl 5
	ldr	x5,[x3,VMA_START]
	cmp	x5,x4
	b.hi	.hole
	ldr	x4,[x3,VMA_END]
	add	x0,x0,1
	b	.cov
.zap:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	vm_zap_range
	mov	x20,0
	b	.unlock
.hole:	mov	x20,E_INVAL
.unlock:
	add	x0,x19,P_VMLOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x20
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = debut, x2 = fin -> x0 = pages presentes dans [debut, fin[,
; x1 = celles qui ne sont pas a lui (PTE_SHARED)
vm_count_range:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x23,0
	mov	x24,0
.blk:	cmp	x20,x21
	b.hs	.end
	mov	x0,x19
	mov	x1,x20
	bl	vm_l3_of
	cmp	x1,x21
	csel	x22,x1,x21,lo
	cbnz	x0,.pg
	mov	x20,x22
	b	.blk
.pg:	cmp	x20,x22
	b.hs	.blk
	ubfx	x1,x20,12,9
	ldr	x3,[x0,x1,lsl 3]
	add	x20,x20,PAGE_SIZE
	tbz	x3,0,.pg
	add	x23,x23,1
	tbz	x3,55,.pg
	add	x24,x24,1
	b	.pg
.end:	mov	x0,x23
	mov	x1,x24
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_VM_QUERY vm_query(adresse, sortie 32 o) -> 0 / 1 / E_NOMEM --------
; 0 : la region qui contient l'adresse ; 1 : la suivante au-dessus ;
; E_NOMEM : aucune. Sortie : +0 debut, +8 fin, +16 droits (32 bits),
; +20 sorte (VMK_*), +24 pages presentes (32 bits), +28 0.
sys_vm_query:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0
	mov	x21,x1
	check_user x21,32,.fault
	current_proc
	mov	x19,x1
	add	x0,x19,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,x19
	mov	x1,x20
	bl	vma_lower
	ldr	x1,[x19,P_VMA_N]
	mov	x23,E_NOMEM
	cmp	x0,x1
	b.hs	.unlock
	ldr	x1,[x19,P_VMA]
	add	x1,x1,x0,lsl 5
	ldp	x2,x3,[x1]
	ldr	x4,[x1,16]
	stp	x2,x3,[sp,-32]!			; (copie : la table peut bouger)
	str	x4,[sp,16]
	cmp	x2,x20
	cset	x23,hi				; 0 : contient, 1 : au-dessus
	mov	x0,x19
	mov	x1,x2
	mov	x2,x3
	bl	vm_count_range
	mov	x24,x0
	add	x0,x19,P_VMLOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	ldp	x2,x3,[sp]
	ldr	x4,[sp,16]
	add	sp,sp,32
	and	x4,x4,0xFFFFFFFFFF		; sorte seule (pas la page d'un objet)
	and	x4,x4,not VMP_COW
	stp	x2,x3,[x21]
	str	x4,[x21,16]
	str	w24,[x21,24]
	str	wzr,[x21,28]
	mov	x0,x23
	b	.out
.unlock:
	add	x0,x19,P_VMLOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x23
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_VM_STATS vm_stats(pid, sortie 64 o) -> 0 --------------------------
; pid 0 : soi, sinon ce process (E_SRCH s'il n'est plus la). En pages de 4 Ko :
; +0 pages a lui (copies sur ecriture comprises), +8 pages partagees (images
; prechargees, kits, tampons : PTE_SHARED), +16 octets reserves dans les
; regions paresseuses, +24 pages remplies a la demande, +32 pages copiees
; sur ecriture, +40 tables de pages, +48 pages libres en zone basse, +56 en
; zone haute.
sys_vm_stats:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x21,x1
	mov	x23,x0
	mov	x25,0				; 1 : proc_lock pris
	check_user x21,64,.fault
	cbnz	x23,.autre
	current_proc
	mov	x19,x1
	b	.lock
.autre:	adr	x0,proc_lock			; l'espace d'un process reste en place tant
	bl	spin_lock_irqsave		; qu'il est dans la table (reaper_thread)
	mov	x26,x0
	mov	x25,1
	mov	x0,x23
	bl	proc_lookup
	mov	x19,x0
	cbz	x19,.parti
.lock:	add	x0,x19,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,x19				; pages presentes de la moitie EL0
	movl	x1,USER_BASE
	movl	x2,USER_TOP
	bl	vm_count_range
	sub	x23,x0,x1
	mov	x24,x1
	mov	x0,x19				; tables (L2 et L3 de la moitie EL0)
	bl	vm_count_tables
	mov	x20,x0
	ldr	x0,[x19,P_VMA]			; octets des regions
	ldr	x1,[x19,P_VMA_N]
	mov	x2,0
.r:	cbz	x1,.rd
	ldp	x3,x4,[x0],VMA_SIZE
	sub	x4,x4,x3
	add	x2,x2,x4
	sub	x1,x1,1
	b	.r
.rd:	stp	x23,x24,[sp,-48]!
	str	x2,[sp,16]
	ldr	x0,[x19,P_FAULTS]
	ldr	x1,[x19,P_COWS]
	stp	x0,x1,[sp,24]
	str	x20,[sp,40]
	add	x0,x19,P_VMLOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	cbz	x25,.copie
	adr	x0,proc_lock
	mov	x1,x26
	bl	spin_unlock_irqrestore
.copie:	ldp	x0,x1,[sp]
	stp	x0,x1,[x21]
	ldp	x0,x1,[sp,16]
	stp	x0,x1,[x21,16]
	ldp	x0,x1,[sp,32]
	stp	x0,x1,[x21,32]
	add	sp,sp,48
	adr	x0,pmm_zone_low
	ldr	x0,[x0,Z_FREE]
	adr	x1,pmm_zone_high
	ldr	x1,[x1,Z_FREE]
	stp	x0,x1,[x21,48]
	mov	x0,0
	b	.out
.parti:	adr	x0,proc_lock
	mov	x1,x26
	bl	spin_unlock_irqrestore
	mov	x0,E_SRCH
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB -> x0 = tables L2 et L3 de la moitie EL0
vm_count_tables:
	ldr	x1,[x0,P_L1]
	movl	x5,PTE_ADDR_MASK
	mov	x0,0
	mov	x2,256
.l1:	ldr	x3,[x1,x2,lsl 3]
	tbz	x3,0,.n1
	add	x0,x0,1
	and	x3,x3,x5
	mov	x4,0
.l2:	ldr	x6,[x3,x4,lsl 3]
	tbz	x6,0,.n2
	add	x0,x0,1
.n2:	add	x4,x4,1
	cmp	x4,512
	b.lo	.l2
.n1:	add	x2,x2,1
	cmp	x2,512
	b.lo	.l1
	ret

; --- SYS_VM_PAGEINFO vm_pageinfo(adresse) -> bits --------------------------
; 1 presente, 2 partagee (PTE_SHARED : page d'une image prechargee, d'un kit,
; d'un tampon), 4 en copie sur ecriture, 8 inscriptible (EL0), 16
; executable (EL0), 32 dans une region paresseuse, 64 lisible (EL0)
sys_vm_pageinfo:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x0
	current_proc
	mov	x19,x1
	movl	x0,USER_BASE
	cmp	x20,x0
	b.lo	.zero
	movl	x0,USER_TOP
	cmp	x20,x0
	b.hs	.zero
	add	x0,x19,P_VMLOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x21,0
	mov	x0,x19
	mov	x1,x20
	bl	vma_find
	cbz	x0,.pte
	orr	x21,x21,PI_LAZY
.pte:	mov	x0,x19
	mov	x1,x20
	bl	vm_lookup
	cbz	x0,.done
	ldr	x3,[x0]
	tbz	x3,0,.done
	orr	x21,x21,PI_PRESENT
	tbz	x3,55,.s
	orr	x21,x21,PI_SHARED
.s:	tbz	x3,56,.c
	orr	x21,x21,PI_COW
.c:	ubfx	x4,x3,6,2
	tbz	x4,0,.done			; EL0 : rien
	orr	x21,x21,PI_READ
	cmp	x4,1
	b.ne	.x
	orr	x21,x21,PI_WRITE
.x:	tbnz	x3,54,.done
	orr	x21,x21,PI_EXEC
.done:	add	x0,x19,P_VMLOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x21
	b	.out
.zero:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
