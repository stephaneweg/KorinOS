/*
 * SDL_armpi.h -- ce que les trois pilotes d'ARMPI se partagent (docs/SDL.md).
 */
#ifndef SDL_armpi_h_
#define SDL_armpi_h_

#define ARMPI_PADS 4			/* PAD_MAX de src/inc/pad.inc */

/* un evenement WEV_PAD, recu par la boucle de la video, pour le pilote de manettes */
void ARMPI_PadEvent (int pad, unsigned buttons, const short axes[4], unsigned model);

#endif
