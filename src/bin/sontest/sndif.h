/* sndif.h : le côté pilote audio de src/svc/inc/sndif.inc (inscription auprès du service sound
 * comme sortie ou entrée, anneau partagé), pour le faux pilote et le faux micro de sontest.
 * Seulement ce dont sontest se sert (pas d'interruption DMA : sndif_irq_*). */
#ifndef SONTEST_SNDIF_H
#define SONTEST_SNDIF_H

typedef int16_t i16;

/* un bloc d'inscription (SIF_* de sndif.inc) */
struct sif {
	long type, rate, dmareq;
	const char *name;
	long id;
	u8 *ring; /* anneau projeté (0 : pas inscrit) */
	long dma; /* canal donné (-1 : aucun) */
	long buf; /* numéro du tampon de l'anneau */
};
extern struct sif sif_out, sif_in;

/* type, fréquence, canal DMA (0 aucun, 1 n'importe lequel, 0x100 + n), nom -> numéro, < 0 */
long sndif_output(long type, long rate, long dma, const char *name);
long sndif_input(long type, long rate, long dma, const char *name);
long sndif_read(i16 *dst, long frames);        /* -> trames lues (reste : silence, manques) */
long sndif_write(const i16 *src, long frames); /* -> trames mises dans l'anneau d'entrée */
long sndif_wanted(void);                       /* 1 si une capture écoute l'entrée */
void sndif_tick(void);                         /* toutes les 2 s, « je suis là » à sound */

#endif
