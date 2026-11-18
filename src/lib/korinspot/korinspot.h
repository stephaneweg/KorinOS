/*
 * korinspot.h -- librespot pour Korin, en bibliothèque C (src/lib/korinspot, tools/spotify/build.sh ;
 * docs/etude/MUSIQUE.md §1). Un compte Spotify Premium (Famille l'est).
 *
 *   ks_login (jeton)          connexion avec un jeton OAuth (portée « streaming ») : attend Spotify, hors du fil de
 *                             la fenêtre -> 0, -1 (ks_error)
 *   ks_load (uri, play, ms)   un titre « spotify:track:… »
 *   ks_play, ks_pause, ks_stop, ks_seek (ms)
 *   ks_read (pcm, trames)     le son : PCM 16 bits stéréo à 44 100 Hz (à donner à AudioKit : ak_open (44100, 2, 16, 0))
 *   ks_status (&st)           l'état (KS_*), la position, la durée, les compteurs de fin et d'indisponibilité
 */
#ifndef KORINSPOT_H
#define KORINSPOT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { KS_STOPPED, KS_LOADING, KS_PLAYING, KS_PAUSED };

struct ks_status
{
	int state;			/* KS_* */
	uint32_t position_ms, duration_ms;
	uint32_t ended;			/* + 1 à chaque titre fini */
	uint32_t unavailable;		/* + 1 à chaque titre indisponible (pays, droits) */
};

int ks_login (const char *access_token);
int ks_load (const char *uri, int play, uint32_t position_ms);
void ks_play (void);
void ks_pause (void);
void ks_stop (void);
void ks_seek (uint32_t position_ms);
size_t ks_read (int16_t *out, size_t frames);
int ks_status (struct ks_status *out);
size_t ks_error (char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
