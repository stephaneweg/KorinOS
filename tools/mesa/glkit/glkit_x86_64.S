/* glkit_x86_64.S -- GlKit du PC (Korin x86-64, tools/mesa/build-glkit-pc.sh) : la meme table
 * d'exportation que glkit.S (src/kits/glkit/glkit.abi ; ajout seul : ne jamais retirer, deplacer ni
 * renommer une ligne), et le resolveur des descripteurs TLS de x86-64 (-mtls-dialect=gnu2). */
	.section .text.kit_table,"ax"
	.globl	kit_table
	.p2align 2
kit_table:
	.long	0x54494B41
	.long	4
	.long	glkit_version - kit_table
	.long	glkit_init - kit_table
	.long	glkit_sym - kit_table
	.long	glkit_error - kit_table

/* rax = adresse du descripteur -> rax = decalage de la variable depuis le pointeur de thread, deja
 * calcule par glkit_init dans le second mot ; aucun autre registre touche (contrat des descripteurs) */
	.text
	.globl	glkit_tlsdesc_static
	.hidden	glkit_tlsdesc_static
	.type	glkit_tlsdesc_static, @function
	.p2align 4
glkit_tlsdesc_static:
	movq	8(%rax), %rax
	ret
	.section .note.GNU-stack,"",@progbits
