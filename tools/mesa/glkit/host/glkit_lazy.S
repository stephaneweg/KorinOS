/* glkit_lazy.S -- le premier appel d'une fonction de GlKit (libglkit.a) : w17 = rang du relais
 * (glkit_stubs.S). Garde les arguments (x0-x8, q0-q7), lie le kit (glkit_load), puis saute a la
 * vraie fonction comme si de rien n'etait. */
	.text
	.global glkit_lazy
	.type	glkit_lazy, %function
	.balign 4
glkit_lazy:
	stp	x29, x30, [sp, -240]!
	mov	x29, sp
	stp	x0, x1, [sp, 16]
	stp	x2, x3, [sp, 32]
	stp	x4, x5, [sp, 48]
	stp	x6, x7, [sp, 64]
	stp	x8, x17, [sp, 80]
	stp	q0, q1, [sp, 96]
	stp	q2, q3, [sp, 128]
	stp	q4, q5, [sp, 160]
	stp	q6, q7, [sp, 192]
	bl	glkit_load
	ldp	x0, x1, [sp, 16]
	ldp	x2, x3, [sp, 32]
	ldp	x4, x5, [sp, 48]
	ldp	x6, x7, [sp, 64]
	ldp	x8, x17, [sp, 80]
	ldp	q0, q1, [sp, 96]
	ldp	q2, q3, [sp, 128]
	ldp	q4, q5, [sp, 160]
	ldp	q6, q7, [sp, 192]
	ldp	x29, x30, [sp], 240
	adrp	x16, glkit_slots
	add	x16, x16, :lo12:glkit_slots
	ldr	x16, [x16, w17, uxtw 3]
	br	x16
