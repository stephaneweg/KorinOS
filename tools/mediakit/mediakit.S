/* table d'exportation de MediaKit (docs/KITS.md) : 'AKIT', nombre, decalages ;
 * ajout seul : ne jamais retirer, deplacer ni renommer une ligne
 * (la meme liste que src/kits/mediakit/mediakit.abi) */
	.section .text.kit_table,"ax"
	.global kit_table
	.balign 4
kit_table:
	.word	0x54494B41
	.word	15
	.word	mk_version - kit_table
	.word	mk_open - kit_table
	.word	mk_close - kit_table
	.word	mk_info - kit_table
	.word	mk_track - kit_table
	.word	mk_select - kit_table
	.word	mk_video_next - kit_table
	.word	mk_video_convert - kit_table
	.word	mk_video_read - kit_table
	.word	mk_audio_open - kit_table
	.word	mk_audio_read - kit_table
	.word	mk_audio_packet - kit_table
	.word	mk_seek - kit_table
	.word	mk_stats - kit_table
	.word	mk_error_text - kit_table

/* mk_on_stack(fonction, argument, sommet de pile) -> fonction(argument), sur
 * la pile du kit (les decodeurs veulent de la place : dav1d, libopus) ; la
 * pile de l'appelant est retrouvee au retour */
	.text
	.global mk_on_stack
	.hidden mk_on_stack
	.balign 4
mk_on_stack:
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	mov	x3, sp
	and	x2, x2, #~15
	mov	sp, x2
	stp	x3, xzr, [sp, -16]!
	mov	x16, x0
	mov	x0, x1
	blr	x16
	ldr	x3, [sp]
	mov	sp, x3
	ldp	x29, x30, [sp], 16
	ret

/* setjmp / longjmp (libvpx : erreurs des decodeurs) : x19-x30, sp, d8-d15 */
	.global setjmp
	.hidden setjmp
	.balign 4
setjmp:
	stp	x19, x20, [x0, 0]
	stp	x21, x22, [x0, 16]
	stp	x23, x24, [x0, 32]
	stp	x25, x26, [x0, 48]
	stp	x27, x28, [x0, 64]
	stp	x29, x30, [x0, 80]
	mov	x16, sp
	str	x16, [x0, 96]
	stp	d8, d9, [x0, 104]
	stp	d10, d11, [x0, 120]
	stp	d12, d13, [x0, 136]
	stp	d14, d15, [x0, 152]
	mov	w0, 0
	ret

	.global longjmp
	.hidden longjmp
	.balign 4
longjmp:
	ldp	x19, x20, [x0, 0]
	ldp	x21, x22, [x0, 16]
	ldp	x23, x24, [x0, 32]
	ldp	x25, x26, [x0, 48]
	ldp	x27, x28, [x0, 64]
	ldp	x29, x30, [x0, 80]
	ldr	x16, [x0, 96]
	mov	sp, x16
	ldp	d8, d9, [x0, 104]
	ldp	d10, d11, [x0, 120]
	ldp	d12, d13, [x0, 136]
	ldp	d14, d15, [x0, 152]
	cmp	w1, 0
	csinc	w0, w1, wzr, ne
	ret
