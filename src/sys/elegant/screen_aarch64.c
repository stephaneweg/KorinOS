/* screen_aarch64.c : ce que la résolution à chaud (screen.c) ne fait que sur le Pi
 * (screen.inc) : le premier bloc EDID de l'écran par la boîte aux lettres du firmware, et
 * l'adresse de bus du framebuffer pour le DMA 2D (present_aarch64.c). */
#include "elegant.h"

#define EDID_MSG 160 /* message au firmware : un bloc EDID */
#define EDID_DATA 28 /* les 128 octets du bloc dans le message */

static u32 scr_fw[EDID_MSG / 4] __attribute__((aligned(16)));

/* -> le premier bloc EDID de l'écran (dans scr_fw), 0 : pas de réponse (QEMU, écran muet) */
const u8 *edid_read(void)
{
	for (int i = 0; i < EDID_MSG / 4; i++)
		scr_fw[i] = 0;
	scr_fw[0] = EDID_MSG;
	scr_fw[2] = 0x00030020; /* GET_EDID_BLOCK, bloc 0 */
	scr_fw[3] = 136;
	if (korin_sys1(SYS_FW_CALL, scr_fw))
		return 0;
	if (scr_fw[1] != 0x80000000u)
		return 0;
	if (!(scr_fw[4] & 0x80000000u) || scr_fw[6]) /* étiquette traitée, statut 0 */
		return 0;
	/* signature 00 FF..FF 00 */
	if (scr_fw[EDID_DATA / 4] != 0xFFFFFF00u || scr_fw[EDID_DATA / 4 + 1] != 0x00FFFFFFu)
		return 0;
	return (const u8 *)scr_fw + EDID_DATA;
}

/* le framebuffer a pu bouger (info : SYS_WS_MODE) : son adresse de bus pour le DMA 2D */
void screen_fb_moved(void)
{
	if (!pres_mode)
		return;
	fb_bus = phys_bus(info[4]);
	if (!fb_bus)
		pres_mode = 0; /* (framebuffer hors de portée du DMA) */
}
