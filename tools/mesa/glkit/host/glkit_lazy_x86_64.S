/* glkit_lazy_x86_64.S -- le premier appel d'une fonction de GlKit sur le PC (libglkit.a de x86-64) :
 * r11 = rang du relais (glkit_stubs.S de finish.py). Garde les arguments (rdi rsi rdx rcx r8 r9,
 * rax : nombre de registres vectoriels d'une fonction a arguments variables, xmm0-7), lie le kit
 * (glkit_load), puis saute a la vraie fonction comme si de rien n'etait. */
	.text
	.globl	glkit_lazy
	.type	glkit_lazy, @function
	.p2align 4
glkit_lazy:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$192, %rsp
	movq	%rdi, 0(%rsp)
	movq	%rsi, 8(%rsp)
	movq	%rdx, 16(%rsp)
	movq	%rcx, 24(%rsp)
	movq	%r8, 32(%rsp)
	movq	%r9, 40(%rsp)
	movq	%rax, 48(%rsp)
	movq	%r11, 56(%rsp)
	movdqu	%xmm0, 64(%rsp)
	movdqu	%xmm1, 80(%rsp)
	movdqu	%xmm2, 96(%rsp)
	movdqu	%xmm3, 112(%rsp)
	movdqu	%xmm4, 128(%rsp)
	movdqu	%xmm5, 144(%rsp)
	movdqu	%xmm6, 160(%rsp)
	movdqu	%xmm7, 176(%rsp)
	call	glkit_load
	movq	0(%rsp), %rdi
	movq	8(%rsp), %rsi
	movq	16(%rsp), %rdx
	movq	24(%rsp), %rcx
	movq	32(%rsp), %r8
	movq	40(%rsp), %r9
	movq	48(%rsp), %rax
	movq	56(%rsp), %r11
	movdqu	64(%rsp), %xmm0
	movdqu	80(%rsp), %xmm1
	movdqu	96(%rsp), %xmm2
	movdqu	112(%rsp), %xmm3
	movdqu	128(%rsp), %xmm4
	movdqu	144(%rsp), %xmm5
	movdqu	160(%rsp), %xmm6
	movdqu	176(%rsp), %xmm7
	leave
	leaq	glkit_slots(%rip), %r10
	jmpq	*(%r10,%r11,8)
	.section .note.GNU-stack,"",@progbits
