/*
 * av_sound_armpi.c -- la sortie son du moteur media de WebKit (MediaPlayerPrivateOnyx -> bibliotheque
 * av d'Onyx, av_audio_kapi : -DAV_KAPI_SOUND) sur AudioKit d'ARMPI (docs/WEBKIT.md, docs/SON.md) :
 * un flux PCM s16 stereo 44,1 kHz (ak_open), ecrit sans attendre (ak_write), son remplissage par
 * ak_space. Les noms kapi_sound_* sont ceux qu'attend av_player.c (glue/include/appkit/appkit.h).
 *
 * AudioKit (SD0:/lib/audiokit.so) : un kit en assembleur, fonctions AAPCS64 ; rangs de
 * src/kits/audiokit/audiokit.abi.
 */
#include <armpi/kit.h>
#include "appkit/appkit.h"

enum { AK_OPEN = 0, AK_WRITE = 1, AK_SPACE = 3, AK_PENDING = 4, AK_CLOSE = 7, AK_LATENCY = 10, AK_MIN = 11 };

static long (*ak_open) (long rate, long channels, long bits, long output);
static long (*ak_write) (long stream, const void *buf, long bytes);
static long (*ak_space) (long stream);
static long (*ak_close) (long stream);
static long (*ak_latency) (long stream, long bytes);
static long s_stream = -1;

static int ak_bind (void)
{
	static int tried, ok;
	if (!tried)
	{
		void *k = armpi_kit_open ("audiokit", AK_MIN);
		tried = 1;
		if (k != 0)
		{
			ak_open = (long (*) (long, long, long, long)) armpi_kit_fn (k, AK_OPEN);
			ak_write = (long (*) (long, const void *, long)) armpi_kit_fn (k, AK_WRITE);
			ak_space = (long (*) (long)) armpi_kit_fn (k, AK_SPACE);
			ak_close = (long (*) (long)) armpi_kit_fn (k, AK_CLOSE);
			ak_latency = (long (*) (long, long)) armpi_kit_fn (k, AK_LATENCY);
			ok = ak_open && ak_write && ak_space && ak_close && ak_latency;
		}
	}
	return ok;
}

int kapi_sound_acquire (void)
{
	if (s_stream >= 0)
		return 1;
	if (!ak_bind ())
		return 0;
	long s = ak_open (SOUND_RATE, 2, 16, 0);	/* (sortie 0 : celle du process) */
	if (s < 0)
		return 0;
	s_stream = s;
	return 1;
}

int kapi_sound_config (int frames, int blocks)
{
	/* l'avance du flux : blocs x trames x 4 octets (bornee par AudioKit a 4096..65536) */
	long bytes = (long) frames * blocks * 4;
	if (s_stream >= 0)
		ak_latency (s_stream, bytes);
	return frames * blocks;
}

void kapi_sound_status (unsigned *rate, unsigned *free_frames, unsigned *other)
{
	long space = s_stream >= 0 ? ak_space (s_stream) : 0;
	if (rate) *rate = SOUND_RATE;
	if (free_frames) *free_frames = space > 0 ? (unsigned) (space / 4) : 0;
	if (other) *other = 0;
}

int kapi_sound_write (const short *frames, unsigned n)
{
	if (s_stream < 0)
		return 0;
	long r = ak_write (s_stream, frames, (long) n * 4);
	return r > 0 ? (int) (r / 4) : 0;
}

void kapi_sound_release (void)
{
	if (s_stream >= 0)
	{
		ak_close (s_stream);
		s_stream = -1;
	}
}
