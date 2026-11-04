; ============================================================================
;  kits/demokit/demokit.asm - DemoKit : un kit d'exemple -> SD0:/lib/demokit.so
;  Trois fonctions AAPCS64 ; une variable par process (le segment inscriptible
;  du kit est recopie dans chaque process qui l'ouvre).
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header

segment readable executable
include 'demokit.abi'				; la table d'exportation

; x0, x1 -> x0 = x0 + x1 (et un appel de plus)
dk_add:
	adr	x2,dk_calls
	ldr	x3,[x2]
	add	x3,x3,1
	str	x3,[x2]
	add	x0,x0,x1
	ret

; -> x0 = appels de dk_add par ce process
dk_count:
	adr	x0,dk_calls
	ldr	x0,[x0]
	ret

; x0 = chaine : ses lettres a..z en majuscules, sur place -> x0 = longueur
dk_upper:
	mov	x2,x0
	mov	x0,0
.c:	ldrb	w1,[x2,x0]
	cbz	w1,.e
	sub	w3,w1,'a'
	cmp	w3,25
	b.hi	.n
	sub	w1,w1,32
	strb	w1,[x2,x0]
.n:	add	x0,x0,1
	b	.c
.e:	ret

segment readable writeable
dk_calls	dd 0				; (dd : 64 bits avec FASMARM)
