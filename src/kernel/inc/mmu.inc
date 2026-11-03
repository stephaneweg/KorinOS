; ============================================================================
;  mmu.inc - descripteurs de tables de pages ARMv8 (granule 4 Ko) et
;  attributs memoire, communs a toutes les cartes (vm.asm, arch/*/mmu.asm)
; ============================================================================

MAIR_DEVICE	= 0		; index 0 : Device-nGnRnE
MAIR_NORMAL	= 1		; index 1 : Normal WB/WA inner+outer
MAIR_NC		= 2		; index 2 : Normal non cacheable
MAIR_VALUE	= 0x00 or (0xFF shl 8) or (0x44 shl 16)

PTE_VALID	= 1
PTE_TABLE	= 3
PTE_AF		= 1 shl 10
PTE_SH_INNER	= 3 shl 8
PTE_PXN		= 1 shl 53
PTE_UXN		= 1 shl 54

BLK_NORMAL	= PTE_VALID or (MAIR_NORMAL shl 2) or PTE_SH_INNER or PTE_AF or PTE_UXN
BLK_NC		= PTE_VALID or (MAIR_NC shl 2) or PTE_SH_INNER or PTE_AF or PTE_UXN or PTE_PXN
BLK_DEVICE	= PTE_VALID or (MAIR_DEVICE shl 2) or PTE_AF or PTE_UXN or PTE_PXN

