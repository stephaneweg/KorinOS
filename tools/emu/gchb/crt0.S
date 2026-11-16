/* crt0.S -- l'entree d'un DOL de tools/emu/gchb : une pile, r2 et r13 a 0 (pas de petites donnees), main */
	.section .text.start, "ax"
	.globl _start
_start:
	lis 1, 0x816F
	ori 1, 1, 0xFFF0
	li 2, 0
	li 13, 0
	bl main
1:	b 1b
