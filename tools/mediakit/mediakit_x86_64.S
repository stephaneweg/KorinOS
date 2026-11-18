/* mediakit pour Korin PC (x86-64) : la table d'exportation, mk_on_stack, setjmp / longjmp
 * (mediakit.S pour le Pi). Syntaxe AT&T, System V : rdi, rsi, rdx. */
#include "kit_table.inc"

/* mk_on_stack(fonction, argument, sommet de pile) -> fonction(argument), sur la pile du kit (les
 * decodeurs veulent de la place : dav1d, libopus) ; la pile de l'appelant est retrouvee au retour
 * (rbp, garde par la fonction) */
	.text
	.global mk_on_stack
	.hidden mk_on_stack
	.balign 16
mk_on_stack:
	pushq	%rbp
	movq	%rsp, %rbp
	andq	$-16, %rdx
	movq	%rdx, %rsp
	movq	%rdi, %rax
	movq	%rsi, %rdi
	call	*%rax
	movq	%rbp, %rsp
	popq	%rbp
	ret

/* setjmp / longjmp (libvpx : erreurs des decodeurs) : rbx, rbp, r12-r15, rsp, retour (8 mots) */
	.global setjmp
	.hidden setjmp
	.balign 16
setjmp:
	movq	%rbx, 0(%rdi)
	movq	%rbp, 8(%rdi)
	movq	%r12, 16(%rdi)
	movq	%r13, 24(%rdi)
	movq	%r14, 32(%rdi)
	movq	%r15, 40(%rdi)
	leaq	8(%rsp), %rdx
	movq	%rdx, 48(%rdi)
	movq	(%rsp), %rdx
	movq	%rdx, 56(%rdi)
	xorl	%eax, %eax
	ret

	.global longjmp
	.hidden longjmp
	.balign 16
longjmp:
	movl	%esi, %eax
	testl	%eax, %eax
	jnz	1f
	incl	%eax
1:	movq	0(%rdi), %rbx
	movq	8(%rdi), %rbp
	movq	16(%rdi), %r12
	movq	24(%rdi), %r13
	movq	32(%rdi), %r14
	movq	40(%rdi), %r15
	movq	48(%rdi), %rsp
	jmp	*56(%rdi)
