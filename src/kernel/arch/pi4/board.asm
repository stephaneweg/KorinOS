; ============================================================================
;  arch/pi4/board.asm - informations sur la carte (interface commune)
; ============================================================================

; -> x0 = 1 sous l'emulateur QEMU raspi4b (peripheriques absents), 0 sinon.
; Le firmware de QEMU annonce une revision < 0x50000000.
board_is_emulated:
	prologue
	bl	mbox_get_fw_rev
	movl	x1,0x50000000
	cmp	x0,x1
	cset	x0,lo
	epilogue

; x0 = coeur (1-3) : le firmware (armstub8 / QEMU) le garde dans une boucle
; qui lit 0xD8 + 8 x coeur et y saute quand l'adresse devient non nulle
SPIN_TABLE	= 0xD8
smp_wake:
	prologue
	mov	x1,SPIN_TABLE
	add	x0,x1,x0,lsl 3
	adr	x1,secondary_entry
	str	x1,[x0]
	mov	x1,8
	bl	dcache_flush_range		; lu MMU coupee
	sev
	epilogue

; x0 = base du controleur SD -> x0 = horloge de base (Hz), 0 si inconnue
; (mailbox : EMMC2 id 12, EMMC/Arasan id 1)
board_sd_clock:
	prologue
	movl	x1,SD_BASE_A
	cmp	x0,x1
	mov	x0,12
	mov	x1,1
	csel	x0,x0,x1,eq
	bl	mbox_get_clock
	epilogue
