/* sndif/sndif.h - côté pilote audio, en C (repris pas à pas de src/svc/inc/sndif.inc ;
 * docs/SON.md 2) : inscription auprès du service « sound » comme sortie et / ou
 * entrée, anneau partagé (un producteur, un consommateur, sans verrou ni appel
 * système). Comme src/svc/netif pour netd.
 *
 * Pas repris : sndif_irq_bind et sndif_irq_wait (le canal DMA des pilotes du SoC
 * du Pi, pwmaudio et hdmiaudio, qui restent en asm) ; usbaudio n'en a pas.
 *
 * Ce dossier n'est pas un service (pas de main) : un pilote le compile par son
 * svc.conf (sources = ../sndif/sndif.c). */
#ifndef SNDIF_H
#define SNDIF_H

#include "korinrt.h"

/* volume de départ (1..100) de la sortie quand /etc/sound.txt n'en retient pas, donné à
 * sound à l'inscription (0 : 100 ; le sound en asm du Pi l'ignore) ; à mettre avant sndif_output */
extern u64 sndif_volume0;
/* type SND_OUT_*, fréquence, canal DMA (0 aucun, 1 n'importe lequel, 0x100 + n :
 * celui-là s'il est libre), nom -> numéro de sortie (> 0), < 0 si sound ne répond pas */
long sndif_output(u64 type, u64 rate, u64 dma, const char *name);
/* idem, type SND_IN_* -> numéro d'entrée */
long sndif_input(u64 type, u64 rate, u64 dma, const char *name);
/* destination, trames -> trames lues de l'anneau de sortie (s16 stéréo à la fréquence
 * de la sortie) ; le reste de la destination est mis à zéro (silence, compté comme manque) */
u64 sndif_read(u32 *dst, u64 frames);
/* source, trames -> trames mises dans l'anneau d'entrée (le reste est perdu) */
u64 sndif_write(const u32 *src, u64 frames);
/* 1 si une capture écoute l'entrée (allumer le micro) */
u64 sndif_wanted(void);
/* toutes les 2 s, dit à sound qu'on est là ; relancé : réinscription */
void sndif_tick(void);

#endif
