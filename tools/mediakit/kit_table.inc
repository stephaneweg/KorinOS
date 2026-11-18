/* tools/mediakit/kit_table.inc : la table d'exportation de MediaKit (docs/KITS.md), commune a
 * mediakit.S (Pi) et mediakit_x86_64.S (PC) : 'AKIT', nombre, decalages ; ajout seul : ne jamais
 * retirer, deplacer ni renommer une ligne (la meme liste que src/kits/mediakit/mediakit.abi) */
	.section .text.kit_table,"ax"
	.global kit_table
	.balign 4
kit_table:
	.long	0x54494B41
	.long	15
	.long	mk_version - kit_table
	.long	mk_open - kit_table
	.long	mk_close - kit_table
	.long	mk_info - kit_table
	.long	mk_track - kit_table
	.long	mk_select - kit_table
	.long	mk_video_next - kit_table
	.long	mk_video_convert - kit_table
	.long	mk_video_read - kit_table
	.long	mk_audio_open - kit_table
	.long	mk_audio_read - kit_table
	.long	mk_audio_packet - kit_table
	.long	mk_seek - kit_table
	.long	mk_stats - kit_table
	.long	mk_error_text - kit_table

