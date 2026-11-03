; ============================================================================
;  arch/pi5/mmu.asm - MMU EL1 du Pi 5, granule 4 Ko, VA 39 bits (T0SZ=25)
;
;  Mappage identite :
;    0 .. ram_end            Normal WB cacheable (blocs de 2 Mo)
;    ram_end .. 1 Go         Normal non cacheable (memoire du VideoCore)
;    0x10_0000_0000 + 1 Go   Device (SD, PCIe)          L1[64]
;    0x10_4000_0000 + 1 Go   Device (peripheriques, GIC) L1[65]
;    0x1F_0000_0000 + 1 Go   Device (RP1)                L1[124]
;  IPS = 40 bits (adresses du Pi 5). NON TESTE SUR MATERIEL.
; ============================================================================

TCR_VALUE	= 25 or (1 shl 8) or (1 shl 10) or (3 shl 12) or (1 shl 23) \
		  or (2 shl 32) or (1 shl 36)
SCTLR_M		= 1 shl 0
SCTLR_C		= 1 shl 2
SCTLR_I		= 1 shl 12
SCTLR_UCI	= 1 shl 26	; EL0 : DC CVAC/CIVAC permis (Elegant nettoie son tampon avant le DMA)
SCTLR_UCT	= 1 shl 15	; EL0 : lecture de CTR_EL0 permise (__clear_cache de libgcc : code genere, JIT)

; x0 = fin de la RAM ARM (multiple de 2 Mo)
mmu_init:
	adr	x1,pt_l1
	adr	x2,pt_l2_low
	orr	x3,x2,PTE_TABLE
	str	x3,[x1]				; L1[0] -> 0..1 Go

	movl	x5,BLK_NORMAL			; 0..1 Go : RAM, puis non cacheable
	movl	x6,BLK_NC
	mov	x7,0
	mov	x8,512
.low:	cmp	x7,x0
	csel	x9,x5,x6,lo
	orr	x9,x9,x7
	str	x9,[x2],8
	add	x7,x7,0x200000
	subs	x8,x8,1
	b.ne	.low

	movl	x5,BLK_DEVICE			; peripheriques : blocs de 1 Go
	movl	x9,PERI_REGION_AXI
	orr	x9,x9,x5
	str	x9,[x1,(PERI_REGION_AXI shr 30)*8]
	movl	x9,PERI_REGION_SOC
	orr	x9,x9,x5
	str	x9,[x1,(PERI_REGION_SOC shr 30)*8]
	movl	x9,PERI_REGION_RP1
	orr	x9,x9,x5
	str	x9,[x1,(PERI_REGION_RP1 shr 30)*8]

	dsb	sy				; activation
	movl	x0,MAIR_VALUE
	msr	mair_el1,x0
	movl	x0,TCR_VALUE
	msr	tcr_el1,x0
	adr	x0,pt_l1
	msr	ttbr0_el1,x0
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
	ret
