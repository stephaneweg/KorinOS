; ============================================================================
;  lib.inc - petite bibliotheque des applications (a inclure dans le segment
;  de code). Routines AAPCS64 : x0-x7 arguments, x19-x28 preserves.
; ============================================================================

; x0 = chaine terminee par 0 -> console noyau
puts:
	mov	x1,x0
.len:	ldrb	w2,[x1],1
	cbnz	w2,.len
	sub	x1,x1,x0
	sub	x1,x1,1
if defined APPKIT_STDIO
	b	f_print				; (AppKit : sur la sortie standard)
else
	sys	SYS_PRINT
	ret
end if

; x0 = valeur non signee, x1 = tampon -> x0 = fin (sur le 0 final)
fmt_dec:
	sub	sp,sp,32
	mov	x2,sp
	mov	x6,sp
	mov	x3,10
.div:	udiv	x4,x0,x3
	msub	x5,x4,x3,x0
	add	w5,w5,'0'
	strb	w5,[x2],1
	mov	x0,x4
	cbnz	x0,.div
.rev:	ldrb	w5,[x2,-1]!
	strb	w5,[x1],1
	cmp	x2,x6
	b.ne	.rev
	strb	wzr,[x1]
	mov	x0,x1
	add	sp,sp,32
	ret

; x0 = valeur signee -> console
print_dec:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	tbz	x0,63,.pos
	mov	w2,'-'
	strb	w2,[x1],1
	neg	x0,x0
.pos:	bl	fmt_dec
	mov	x0,sp
	bl	puts
	add	sp,sp,32
	epilogue

; x0 = destination, x1 = source -> x0 = fin (sur le 0 final)
str_cat:
.c:	ldrb	w2,[x1],1
	strb	w2,[x0]
	cbz	w2,.e
	add	x0,x0,1
	b	.c
.e:	ret

; x0 = taille -> x0 = bloc (memoire du tas, jamais rendue), 0 si echec
malloc:
	add	x0,x0,15
	and	x0,x0,not 15
	sys	SYS_SBRK
	tbnz	x0,63,.fail
	ret
.fail:	mov	x0,0
	ret
