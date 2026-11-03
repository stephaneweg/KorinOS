; ============================================================================
;  mmu.asm - MMU EL1, granule 4 Ko, VA 39 bits (T0SZ=25), 3 niveaux
;
;  Etape 1 : mappage identite via TTBR0, blocs de 2 Mo.
;    0 .. ram_end          Normal WB cacheable (code, donnees, piles)
;    ram_end .. 1 Go       Normal non cacheable (memoire VideoCore, framebuffer)
;    0xFC000000 .. 4 Go    Device nGnRnE (peripheriques, PCIe, GIC)
;  Plus tard : noyau en TTBR1 (moitie haute), TTBR0 par process + ASID.
; ============================================================================

; MAIR_*, PTE_*, BLK_* : voir inc/mmu.inc (architecture ARMv8)

; TCR_EL1 : T0SZ=25, walks cacheables inner-shareable, TG0=4K, EPD1 (TTBR1 off),
;           IPS=36 bits (xHCI a 0x6_0000_0000), AS=1 (ASID 16 bits)
TCR_VALUE	= 25 or (1 shl 8) or (1 shl 10) or (3 shl 12) or (1 shl 23) \
		  or (1 shl 32) or (1 shl 36)

SCTLR_M		= 1 shl 0
SCTLR_C		= 1 shl 2
SCTLR_I		= 1 shl 12
SCTLR_UCI	= 1 shl 26	; EL0 : DC CVAC/CIVAC permis (Elegant nettoie son tampon avant le DMA)
SCTLR_UCT	= 1 shl 15	; EL0 : lecture de CTR_EL0 permise (__clear_cache de libgcc : code genere, JIT)

PERI_LOW	= 0xFC000000	; debut de la zone peripheriques mappee

; x0 = fin de la RAM ARM (multiple de 2 Mo) : tables, fenetre PCIe, MMU active
mmu_init:
	prologue
	bl	mmu_init_map
	epilogue

mmu_init_map:
	adr	x1,pt_l1
	adr	x2,pt_l2_low
	orr	x3,x2,PTE_TABLE
	str	x3,[x1]				; L1[0] -> 0..1 Go
	adr	x4,pt_l2_high
	orr	x3,x4,PTE_TABLE
	str	x3,[x1,3*8]			; L1[3] -> 3..4 Go

	; --- 0..1 Go : RAM ARM cacheable, au-dela non cacheable --------------
	movl	x5,BLK_NORMAL
	movl	x6,BLK_NC
	mov	x7,0				; adresse physique courante
	mov	x8,512
.low:	cmp	x7,x0
	csel	x9,x5,x6,lo
	orr	x9,x9,x7
	str	x9,[x2],8
	add	x7,x7,0x200000
	subs	x8,x8,1
	b.ne	.low

	; --- 3..4 Go : peripheriques uniquement ------------------------------
	movl	x5,BLK_DEVICE
	movl	x7,0xC0000000
	movl	x10,PERI_LOW
	mov	x8,512
.high:	cmp	x7,x10
	csel	x9,x5,xzr,hs
	cbz	x9,.skip
	orr	x9,x9,x7
.skip:	str	x9,[x4],8
	add	x7,x7,0x200000
	subs	x8,x8,1
	b.ne	.high

	; --- activation ------------------------------------------------------
	dsb	sy
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
