/* AudioKit : SILK (ak_silk.c, d'après ak_silk.inc) : décodeur, entiers comme
 * la norme (RFC 6716 4.2), rééchantillonnage vers 48 kHz. */
#ifndef AK_SILK_H
#define AK_SILK_H

#include "ak.h"

struct ec_dec;
struct silk;

/* décodeur (mémoire), canaux de sortie -> état SILK (long : < 0 = AKE_NOMEM) */
long silk_new(struct akd *d, u64 cc);
/* oublie l'historique (silk_ResetDecoder) */
void silk_reset(struct silk *sk);
/* état, décodeur de plage, canaux du paquet, fréquence interne (kHz), durée du
 * paquet (ms), 1 : première trame du paquet, sorties (flottants -1..1, 48 kHz)
 * -> trames rendues (48 kHz) / < 0 */
long silk_decode(struct silk *sk, struct ec_dec *ec, u32 ch, u32 khz, u32 ms, u32 first, float *o0, float *o1);

#endif
