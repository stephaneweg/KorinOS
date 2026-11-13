/* gamesnd.h : petits effets sonores des jeux, sur les voix FM d'AudioKit (docs/SON.md), comme
 * src/inc/gamesnd.inc : gs_note joue une note d'une durée donnée, gs_later la joue plus tard (une
 * mélodie), gs_seq joue une table. Sans son (pas de service, sortie prise), tout reste silencieux
 * et le jeu tourne.
 *
 *	sys.conf : import = audiokit ; sources = ../common/gamesnd.c
 *	gs_open() au début, gs_tick() à chaque image, gs_close() à la fin. */
#ifndef KORIN_GAMESND_H
#define KORIN_GAMESND_H

#include "sys_consts.h"

#define GS_NV 8 /* voix */
#define GS_Q 24 /* notes en attente */

/* entrée d'une table de gs_seq (gs_ent de l'asm, 8 octets) : attente (ms), hz, durée (ms),
 * onde AKW_*, volume ; la table finit par une entrée à 0 (gs_end : hz = 0) */
struct gs_ent {
	u16 delay, hz, ms;
	u8 wave, vol;
};
#define GS_ENT(delay, hz, ms, wave, vol) { (delay), (hz), (ms), (wave), (vol) }
#define GS_END { 0, 0, 0, 0, 0 }

extern long gs_fm; /* synthé (0 : pas de son) */

void gs_open(void);  /* ouvre le synthé ; gs_fm = 0 s'il n'y a pas de son */
void gs_close(void);
void gs_note(long hz, long ms, long wave, long vol); /* volume 0..255 */
void gs_later(long delay, long hz, long ms, long wave, long vol);
void gs_seq(const struct gs_ent *t); /* toutes les notes de la table */
void gs_tick(void);   /* les notes dues partent, les notes finies s'arrêtent */
long gs_toggle(void); /* Son on/off -> 1 si le son reste actif */

/* le gain et la perte d'une partie (les quatre notes d'Onyx) */
extern const struct gs_ent gs_tab_win[];
extern const struct gs_ent gs_tab_lose[];

#endif
