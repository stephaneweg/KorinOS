; ============================================================================
;  arch/common/smp.asm - demarrage des coeurs 1 a 3
;
;  Commun : piles, point d'entree (MMU coupee, EL2 ou EL1), activation de la
;  MMU partagee. La carte fournit smp_wake (x0 = coeur) : spin table du
;  firmware sur le Pi 4, appel PSCI CPU_ON sur le Pi 5.
;  Les coeurs demarrent MMU et caches coupes : tout ce qu'ils lisent avant
;  d'activer leur MMU doit avoir ete nettoye jusqu'au point de coherence.
; ============================================================================

; Coeur 0 : reveille les coeurs 1-3 et attend qu'ils soient en ligne
smp_start:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,1
.cpu:	mov	x0,THREAD_PAGES			; pile de boot / idle du coeur
	bl	pmm_alloc_low_n			; (zone basse : lue MMU coupee)
	cbz	x0,.next
	add	x0,x0,THREAD_BLOCK
	adr	x1,secondary_stacks
	str	x0,[x1,x19,lsl 3]
.next:	add	x19,x19,1
	cmp	x19,MAX_CPUS
	b.lo	.cpu

	; tout ce que les secondaires liront MMU coupee -> en RAM
	adr	x0,secondary_stacks
	mov	x1,8*MAX_CPUS
	bl	dcache_flush_range
	adr	x0,smp_boot_params
	mov	x1,SMP_PARAMS_SIZE
	bl	dcache_flush_range
	mov	x19,1				; reveil, propre a la carte
.wake:	adr	x0,secondary_stacks
	ldr	x0,[x0,x19,lsl 3]
	cbz	x0,.wnext			; pas de pile : coeur laisse de cote
	mov	x0,x19
	bl	smp_wake
.wnext:	add	x19,x19,1
	cmp	x19,MAX_CPUS
	b.lo	.wake

	movl	x20,100000000			; attente bornee
.wait:	adr	x0,cpus_online
	ldr	x0,[x0]
	cmp	x0,MAX_CPUS
	b.hs	.done
	subs	x20,x20,1
	b.ne	.wait
.done:	ldp	x19,x20,[sp],16
	epilogue

; Point d'entree des coeurs 1-3 (MMU coupee, EL2 ou EL1)
secondary_entry:
	drop_to_el1 secondary_el1
secondary_el1:
	cpu_index x19
	and	x19,x19,3
	adr	x0,secondary_stacks
	ldr	x0,[x0,x19,lsl 3]
	mov	sp,x0
	adr	x0,vector_table
	msr	vbar_el1,x0

	; meme MMU que le coeur 0 (tables partagees)
	adr	x1,smp_boot_params
	ldp	x2,x3,[x1]			; MAIR, TCR
	ldr	x4,[x1,16]			; TTBR0
	msr	mair_el1,x2
	msr	tcr_el1,x3
	msr	ttbr0_el1,x4
	isb
	tlbi	vmalle1
	dsb	ish
	ic	iallu
	dsb	ish
	isb
	mrs	x0,sctlr_el1
	movl	x1,SCTLR_M or SCTLR_C or SCTLR_I or SCTLR_UCI or SCTLR_UCT
	orr	x0,x0,x1
	msr	sctlr_el1,x0
	isb

	mov	x0,x19
	bl	sched_init_cpu
	bl	gic_init_cpu
	bl	timer_init_cpu
	adr	x0,cpus_online
	bl	atomic_inc
	b	cpu_idle

; parametres MMU publies par le coeur 0 pour les secondaires
		align 64
smp_boot_params:
		dd MAIR_VALUE, TCR_VALUE, pt_l1
SMP_PARAMS_SIZE	= $ - smp_boot_params
		align 64
secondary_stacks:
		dd 0, 0, 0, 0
