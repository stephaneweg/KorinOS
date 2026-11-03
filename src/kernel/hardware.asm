; ============================================================================
;  hardware.asm - acces au materiel pour les services (docs/NOYAU-HYBRIDE.md,
;  3.3-2) : registres mappes, memoire pour le DMA, appels au firmware.
;  Reserve aux programmes de SD0:/sys (P_DROITS) ; les autres : E_PERM.
;
;  * dev_map : seulement les zones de peripheriques de la carte (board.inc :
;    DEV_ZONE*), jamais la RAM ni le GIC ; fenetre DEV_VA du process ;
;  * dma_alloc : pages contigues, mises a zero, non cachees (ni vidage ni
;    invalidation a faire), fenetre DMA_VA ; liberees a la fin du process ;
;    bit 63 de la taille (DMA_CACHED) : cachees, a nettoyer par l'appelant
;    (DC CVAC permis en EL0 : SCTLR_EL1.UCI) avant que le DMA ne les lise
;    (le tampon arriere d'Elegant, lu par le DMA 2D de l'ecran) ;
;  * fw_call : un message « property tags » (canal 8) au firmware.
; ============================================================================

DEV_VA_BASE	= 0x7800000000		; L1[480..487] : registres mappes
DMA_VA_BASE	= 0x7A00000000		; L1[488..495] : memoire DMA
MAT_FENETRE	= 0x200000000		; 8 Go chacune
FW_MAX		= 1024			; message au firmware (octets)
E_IO		= -5

; registres : Device-nGnRnE, EL0 lecture/ecriture, jamais executables ni
; liberes (PTE_SHARED : ce ne sont pas des pages de RAM)
VM_DEV		= PTE_PAGE or (MAIR_DEVICE shl 2) or PTE_AF or PTE_NG or PTE_AP_EL0 \
		  or PTE_PXN or PTE_UXN or PTE_SHARED
; memoire DMA : normale non cachee, a lui (liberee par vm_destroy)
VM_DMA		= PTE_PAGE or (MAIR_NC shl 2) or PTE_SH_INNER or PTE_AF or PTE_NG \
		  or PTE_AP_EL0 or PTE_PXN or PTE_UXN
; (DMA_CACHED) normale cachee, nettoyee par le programme
VM_DMA_C	= PTE_PAGE or (MAIR_NORMAL shl 2) or PTE_SH_INNER or PTE_AF or PTE_NG \
		  or PTE_AP_EL0 or PTE_PXN or PTE_UXN

; -> x0 = PCB courant s'il a les droits, 0 sinon
mat_droits:
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x0,[x0,TH_PROC]
	ldr	x1,[x0,P_DROITS]
	cmp	x1,0
	csel	x0,xzr,x0,eq
	ret

; x0 = adresse physique, x1 = taille -> x0 = 1 si [x0, x0 + x1[ est dans une
; zone de peripheriques de la carte
mat_zone:
	add	x1,x0,x1
	movl	x2,DEV_ZONE1_MIN
	movl	x3,DEV_ZONE1_MAX
	cmp	x0,x2
	b.lo	.z2
	cmp	x1,x3
	b.ls	.oui
.z2:	movl	x2,DEV_ZONE2_MIN
	movl	x3,DEV_ZONE2_MAX
	cmp	x0,x2
	b.lo	.non
	cmp	x1,x3
	b.ls	.oui
.non:	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; x0 = PCB, x1 = decalage du curseur (P_DEV_VA / P_DMA_VA), x2 = base de la
; fenetre, x3 = taille (pages entieres) -> x0 = VA reservee (0 : fenetre pleine)
mat_reserve_va:
	ldr	x4,[x0,x1]
	cbnz	x4,.a
	mov	x4,x2				; premiere fois : debut de la fenetre
.a:	add	x5,x4,x3
	movl	x6,MAT_FENETRE
	add	x6,x2,x6
	cmp	x5,x6
	b.hi	.full
	str	x5,[x0,x1]
	mov	x0,x4
	ret
.full:	mov	x0,0
	ret

; --- dev_map(adresse physique, taille) -> adresse chez l'appelant ---------
sys_dev_map:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	mat_droits
	cbz	x0,.perm
	mov	x21,x0
	cbz	x20,.inval
	mov	x0,x19
	mov	x1,x20
	bl	mat_zone
	cbz	x0,.inval
	and	x22,x19,0xFFF			; decalage dans la page
	add	x20,x20,x22			; pages entieres
	add	x20,x20,0xFFF
	and	x20,x20,not 0xFFF
	bic	x19,x19,0xFFF
	adr	x0,mat_mutex
	bl	mutex_lock
	mov	x0,x21
	mov	x1,P_DEV_VA
	movl	x2,DEV_VA_BASE
	mov	x3,x20
	bl	mat_reserve_va
	cbz	x0,.nomem
	str	x0,[sp,-16]!
	mov	x1,x0
	mov	x0,x21
	mov	x2,x19
	mov	x3,x20
	movl	x4,VM_DEV
	bl	vm_map_phys
	mov	x1,x0
	ldr	x0,[sp],16
	cbz	x1,.nomem
	add	x19,x0,x22			; adresse rendue
	adr	x0,mat_mutex
	bl	mutex_unlock
	isb
	mov	x0,x19
	b	.out
.nomem:	adr	x0,mat_mutex
	bl	mutex_unlock
	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.perm:	mov	x0,E_PERM
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- dma_alloc(taille, &adresse physique) -> adresse chez l'appelant ------
; (bit 63 de la taille, DMA_CACHED : memoire cachee)
sys_dma_alloc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	movl	x2,VM_DMA			; attributs : non cachee,
	movl	x3,VM_DMA_C			; ou cachee (bit 63)
	tst	x0,1 shl 63
	csel	x2,x3,x2,ne
	str	x2,[sp,-16]!
	and	x19,x0,0x7FFFFFFFFFFFFFFF
	mov	x20,x1
	bl	mat_droits
	cbz	x0,.perm
	mov	x21,x0
	check_user x20,8,.fault
	cbz	x19,.inval
	add	x19,x19,0xFFF			; pages entieres
	lsr	x19,x19,12
	mov	x0,x19
	bl	pmm_alloc_low_n			; sous 1 Go : DMA 30 bits, VideoCore
	cbz	x0,.nomem0
	mov	x22,x0
	lsl	x1,x19,12			; a zero (et hors des caches : la
	bl	mem_zero			; vue non cachee doit les lire)
	mov	x0,x22
	lsl	x1,x19,12
	bl	dcache_flush_range
	adr	x0,mat_mutex
	bl	mutex_lock
	mov	x0,x21
	mov	x1,P_DMA_VA
	movl	x2,DMA_VA_BASE
	lsl	x3,x19,12
	bl	mat_reserve_va
	cbz	x0,.nomem
	mov	x1,x0
	str	x1,[sp,-16]!
	mov	x0,x21
	mov	x2,x22
	lsl	x3,x19,12
	ldr	x4,[sp,16]			; attributs (sous l'adresse gardee)
	bl	vm_map_phys
	mov	x1,x0
	ldr	x0,[sp],16
	cbz	x1,.nomem
	str	x22,[x20]			; adresse physique pour le pilote
	mov	x19,x0
	adr	x0,mat_mutex
	bl	mutex_unlock
	isb
	mov	x0,x19
	b	.out
.nomem:	adr	x0,mat_mutex
	bl	mutex_unlock
	mov	x0,x22
	mov	x1,x19
	bl	pmm_free_n
.nomem0:
	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
	b	.out
.perm:	mov	x0,E_PERM
.out:	add	sp,sp,16			; (attributs)
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- fw_call(message) -> 0 (le message recoit la reponse) ----------------
; (message « property tags » : +0 taille totale, +4 code, puis les tags)
sys_fw_call:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	mat_droits
	cbz	x0,.perm
	check_user x19,12,.fault
	ldr	w20,[x19]			; taille du message
	cmp	w20,12
	b.lo	.inval
	cmp	w20,FW_MAX
	b.hi	.inval
	tst	w20,3
	b.ne	.inval
	check_user x19,x20,.fault
	adr	x0,mat_mutex
	bl	mutex_lock
	adr	x0,fw_tampon
	mov	x1,x19
	mov	x2,x20
	bl	mem_copy
	adr	x0,fw_tampon
	bl	mbox_call
	mov	x1,E_IO
	cmp	x0,0
	csel	x20,x1,xzr,eq
	ldr	w2,[x19]			; reponse -> programme
	mov	x0,x19
	adr	x1,fw_tampon
	bl	mem_copy
	adr	x0,mat_mutex
	bl	mutex_unlock
	mov	x0,x20
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
	b	.out
.perm:	mov	x0,E_PERM
.out:	ldp	x19,x20,[sp],16
	epilogue

		align 8
mat_mutex	dw 0
		align 16
fw_tampon	rb FW_MAX
		align 4
