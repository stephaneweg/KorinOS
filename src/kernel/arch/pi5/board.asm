; ============================================================================
;  arch/pi5/board.asm - informations et services de la carte (Pi 5)
; ============================================================================

; -> x0 = 0 : pas d'emulateur du Pi 5
board_is_emulated:
	mov	x0,0
	ret

; x0 = coeur (1-3) : demarrage par le firmware (BL31), appel PSCI CPU_ON.
; Le coeur part en secondary_entry (MMU coupee, EL2 ou EL1).
PSCI_CPU_ON		= 0xC4000003		; convention SMC64
smp_wake:
	prologue
	lsl	x1,x0,8				; MPIDR cible : coeur dans Aff1 (Cortex-A76)
	movl	x0,PSCI_CPU_ON
	adr	x2,secondary_entry
	mov	x3,0				; contexte (inutilise)
	smc	0
	epilogue

; x0 = base du controleur SD -> x0 = horloge de base (Hz)
board_sd_clock:
	movl	x0,SD_CLOCK_HZ
	ret
