//
// sndtest.cpp -- Morpho sur l'hôte : le son (sound.h) rendu dans des fichiers WAV, par le synthé d'AudioKit compilé pour
// l'hôte (host_ak.c, la banque FM). tools/morpho/run.sh sound DOSSIER : music-<humeur>.wav, sfx.wav (chaque bruitage,
// dans l'ordre des SX_*, une seconde et demie chacun), et le niveau de crête de chacun.
//
#include <stdio.h>
#include <vector>
#include <unistd.h>
extern "C" {
long ak_synth_new (void *sf, long flags);
long ak_synth_free (long s);
long ak_synth_midi (long s, long ch, long cmd, long a, long b);
long ak_synth_render (long s, short *out, long n);
}
static std::vector<short> g_pcm;
static volatile bool g_record = true;
extern "C" {
long ak_open (long, long, long, long) { return 1; }
long ak_close (long) { return 0; }
long ak_write_all (long, long buf, long bytes) { if (g_record) g_pcm.insert (g_pcm.end (), (short *) buf, (short *) buf + bytes / 2); else usleep (1); return bytes; }
}
// (les relais de audiokit_abi.h, sur le synthé compilé ici)
#define ak_synth_new ak_synth_new_abi
#define ak_synth_free ak_synth_free_abi
#define ak_synth_midi ak_synth_midi_abi
#define ak_synth_render ak_synth_render_abi
#define ak_open ak_open_abi
#define ak_close ak_close_abi
#define ak_write_all ak_write_all_abi
#include "../../src/apps/morpho/sound.h"
#undef ak_synth_new
#undef ak_synth_free
#undef ak_synth_midi
#undef ak_synth_render
#undef ak_open
#undef ak_close
#undef ak_write_all
long g_on[16];
extern "C" {
long ak_synth_new_abi (long a, long b) { return ak_synth_new ((void *) a, b); }
long ak_synth_free_abi (long a) { return ak_synth_free (a); }
long ak_synth_midi_abi (long a, long b, long c, long d, long e) { if (c == 0x90 && e > 0 && b >= 0 && b < 16) g_on[b]++; return ak_synth_midi (a, b, c, d, e); }
long ak_synth_render_abi (long a, long b, long c) { return ak_synth_render (a, (short *) b, c); }
long ak_open_abi (long a, long b, long c, long d) { return ak_open (a, b, c, d); }
long ak_close_abi (long a) { return ak_close (a); }
long ak_write_all_abi (long a, long b, long c) { return ak_write_all (a, b, c); }
}
using namespace mo;

static void wav (const char *path, const std::vector<short> &p, size_t from, size_t to)
{
	FILE *f = fopen (path, "wb");
	if (!f) return;
	unsigned n = (unsigned) ((to - from) * 2);
	unsigned hdr[11] = { 0x46464952, 36 + n, 0x45564157, 0x20746D66, 16, 0x00020001, 44100, 44100 * 4, 0x00100004, 0x61746164, n };
	fwrite (hdr, 4, 11, f);
	fwrite (&p[from], 2, to - from, f);
	fclose (f);
	int peak = 0; double sum = 0;
	for (size_t i = from; i < to; i++) { int a = abs (p[i]); if (a > peak) peak = a; sum += (double) a * a; }
	printf ("%-28s %5.1f s  crête %5.1f %%  moyenne %5.1f %%\n", path, (double) (to - from) / 2 / 44100, peak * 100.0 / 32767, sqrt (sum / (double) (to - from)) * 100 / 32767);
}
static void waitFrames (Sound &S, long long f) { long long t = S.clock + f; while (S.clock < t) usleep (100); }

int main (int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	char path[512];
	Sound S;
	S.want = MU_NONE;
	S.open ();
	while (!S.syn) usleep (1000);
	static const char *const MOODS[] = { "", "jour", "nuit", "triste", "oeuf", "metamorphose" };
	for (int m = MU_DAY; m <= MU_EVOLVE; m++)
	{
		S.want = m;
		waitFrames (S, 44100 * 2);		// (le fondu)
		size_t a = g_pcm.size ();
		waitFrames (S, 44100 * 20);
		snprintf (path, sizeof path, "%s/music-%s.wav", dir, MOODS[m]);
		wav (path, g_pcm, a, g_pcm.size ());
		printf ("   notes : mélodie %ld, nappe %ld, basse %ld, harpe %ld (20 s)\n", g_on[0], g_on[1], g_on[2], g_on[3]);
		for (long &x : g_on) x = 0;
		S.want = MU_NONE;
		waitFrames (S, 44100 * 2);
	}
	static const char *const NAMES[] = { "joie", "triste", "bouchee", "rebond", "attrape", "crotte", "metamorphose", "eclosion", "beurk",
					     "bain", "propre", "ronron", "craquement", "clic", "cri", "ronflement", "malade", "miam", "reveil",
					     "endormi", "refus", "depart" };
	size_t a0 = g_pcm.size ();
	for (int i = 0; i <= SX_GONE; i++)
	{
		size_t a = g_pcm.size ();
		S.play (i, 0);
		waitFrames (S, i == SX_EVOLVE || i == SX_GONE ? 44100 * 4 : 44100 * 2);
		snprintf (path, sizeof path, "%s/sfx-%02d-%s.wav", dir, i, NAMES[i]);
		wav (path, g_pcm, a, g_pcm.size ());
	}
	snprintf (path, sizeof path, "%s/sfx.wav", dir);
	wav (path, g_pcm, a0, g_pcm.size ());
	S.close ();
	return 0;
}
