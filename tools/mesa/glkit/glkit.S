/* glkit.S -- table d'exportation de GlKit (docs/KITS.md, docs/MESA.md « GlKit ») : 'AKIT', nombre,
 * decalages ; ajout seul : ne jamais retirer, deplacer ni renommer une ligne (la meme liste que
 * src/kits/glkit/glkit.abi). */
	.section .text.kit_table,"ax"
	.global kit_table
	.balign 4
kit_table:
	.word	0x54494B41
	.word	4
	.word	glkit_version - kit_table
	.word	glkit_init - kit_table
	.word	glkit_sym - kit_table
	.word	glkit_error - kit_table

/* le resolveur des descripteurs TLS (R_AARCH64_TLSDESC) : x0 = adresse du descripteur ->
 * x0 = decalage de la variable depuis TPIDR_EL0, deja calcule par glkit_init dans le second mot.
 * Les variables par thread du kit sont dans celles du programme (glkit_tls_area de libglkit.a,
 * a un decalage fixe de TPIDR_EL0 : programme statique) ; celles de la libc du programme
 * (_tls_errno...) aussi. Rien d'autre n'est touche : c'est le contrat des descripteurs. */
	.text
	.global glkit_tlsdesc_static
	.hidden glkit_tlsdesc_static
	.type	glkit_tlsdesc_static, %function
	.balign 4
glkit_tlsdesc_static:
	ldr	x0, [x0, 8]
	ret
