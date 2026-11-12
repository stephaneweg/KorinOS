/* AudioKit : décodeur Vorbis I (ak_vorbis.c, d'après ak_vorbis.inc) ; les
 * fonctions qu'appellent l'Ogg (ak_ogg.c) et l'API par paquets (ak_dec.c,
 * vb_init_pkt est déclaré dans ak.h). Noms des étiquettes de l'asm. */
#ifndef AK_VORBIS_H
#define AK_VORBIS_H

#include "ak.h"

/* décodeur -> 0 / AKE_NOMEM : bloc d'état Vorbis rangé dans le bloc de l'Ogg
 * (OG_STATE de d->codec) ; si d->codec vaut 0 (API par paquets), dans d->codec */
long vb_new(struct akd *d);
/* décodeur -> bloc d'état Vorbis (d->codec en mode 2, sinon OG_STATE de l'Ogg) */
void *vb_state(struct akd *d);
/* décodeur : oublie le bloc précédent (recherche) */
void vb_reset(struct akd *d);
/* décodeur, paquet d'en-tête (identification, commentaires, configuration),
 * octets -> 0 / < 0 (AKE_CORRUPT, AKE_FORMAT, AKE_NOMEM) ; l'identification
 * remplit d->ch, d->rate, d->bitrate, d->fmt, d->pcmfmt et, hors mode 2,
 * OG_PREROLL de l'Ogg (= bs1) */
long vb_header(struct akd *d, const u8 *pkt, u64 n);
/* décodeur, paquet audio, octets -> trames rendues dans d->pcm (0 : premier
 * bloc, paquet vide ou paquet d'en-tête ; < 0 : AKE_CORRUPT) */
long vb_packet(struct akd *d, const u8 *pkt, u64 n);

#endif
