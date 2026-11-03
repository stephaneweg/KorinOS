; ============================================================================
;  macros.inc - macros utilitaires communes (FASMARM, AArch64)
; ============================================================================

; movl reg, imm64 : charge une constante 64 bits quelconque (movz + movk)
macro movl reg*, val* {
	movz	reg,(val) and 0xFFFF
  if ((val) shr 16) and 0xFFFF <> 0
	movk	reg,((val) shr 16) and 0xFFFF,lsl 16
  end if
  if ((val) shr 32) and 0xFFFF <> 0
	movk	reg,((val) shr 32) and 0xFFFF,lsl 32
  end if
  if ((val) shr 48) and 0xFFFF <> 0
	movk	reg,((val) shr 48) and 0xFFFF,lsl 48
  end if
}

; Convention d'appel interne du noyau (proche AAPCS64, simplifiee) :
;   x0-x7   arguments / retour        (ecrases par l'appele)
;   x9-x15  temporaires               (ecrases par l'appele)
;   x19-x28 preserves par l'appele
;   x30     adresse de retour (lr)
; Les routines feuilles n'empilent rien ; les autres sauvent x29/x30.

; cpu_index reg : numero du coeur courant (0..3) d'apres MPIDR_EL1.
; Cortex-A72 (Pi 4) : champ Aff0 ; Cortex-A76 (Pi 5) : bit MT (24) a 1 et
; numero dans Aff1. Ne touche que reg.
macro cpu_index reg* {
	local	.aff0, .done
	mrs	reg,mpidr_el1
	tbz	reg,24,.aff0
	ubfx	reg,reg,8,8
	b	.done
.aff0:	and	reg,reg,0xFF
.done:
}

macro prologue {
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
}

macro epilogue {
	ldp	x29,x30,[sp],16
	ret
}

; ATTENTION FASMARM : tailles de donnees a la mode ARM
;   db = 8 bits, dh = 16, dw = 32, dd = 64   (idem rb/rh/rw/rd)

; Passage EL2 -> EL1h (rien si deja en EL1). Ecrase x0.
macro drop_to_el1 target* {
	local	.in_el1
	mrs	x0,CurrentEL
	ubfx	x0,x0,2,2
	cmp	x0,2
	b.ne	.in_el1
	mrs	x0,cnthctl_el2
	orr	x0,x0,3				; EL1PCTEN | EL1PCEN
	msr	cnthctl_el2,x0
	msr	cntvoff_el2,xzr
	movl	x0,1 shl 31			; HCR_EL2.RW : EL1 en AArch64
	msr	hcr_el2,x0
	movl	x0,0x30D00800			; SCTLR_EL1 : RES1, MMU off
	msr	sctlr_el1,x0
	mov	x0,0x3C5			; EL1h, DAIF masques
	msr	spsr_el2,x0
	adr	x0,target
	msr	elr_el2,x0
	eret
.in_el1:
}

; reg = structure per-CPU du coeur courant (TPIDR_EL1)
macro this_cpu reg* {
	mrs	reg,tpidr_el1
}
