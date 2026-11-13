/* screen_x86_64.c : la résolution à chaud (screen.c) sur le PC : l'EDID est inconnu (pas de
 * firmware à interroger ; le noyau refuse SYS_WS_MODE par E_IO : une seule résolution, celle du
 * GOP), et la présentation est une copie par le processeur (rien à refaire). */
#include "elegant.h"

/* -> 0 : pas d'EDID (la table d'Onyx et la taille actuelle seulement) */
const u8 *edid_read(void)
{
	return 0;
}

/* le framebuffer a pu bouger : rien à refaire sans DMA 2D */
void screen_fb_moved(void)
{
}
