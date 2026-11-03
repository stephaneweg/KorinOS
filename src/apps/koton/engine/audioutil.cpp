//
// audioutil.cpp -- see audioutil.h.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "audioutil.h"
#include "audiokit_abi.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

float soft_clip (float x)
{
	const float T = 0.75f;
	if (x > T) return T + (1.0f - T) * tanhf ((x - T) / (1.0f - T));
	if (x < -T) return -T + (1.0f - T) * tanhf ((x + T) / (1.0f - T));
	return x;
}
static inline int sat16 (int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
void f32_to_s16 (const float *l, const float *r, short *out, int frames, float gain)
{
	for (int i = 0; i < frames; i++)
	{
		out[2 * i] = (short) sat16 ((int) (soft_clip (l[i] * gain) * 32767.0f));
		out[2 * i + 1] = (short) sat16 ((int) (soft_clip (r[i] * gain) * 32767.0f));
	}
}

struct wav_file { FILE *f; int channels; };
wav_file *wav_begin (const char *path, int rate, int channels, long long frames)
{
	FILE *f = fopen (path, "wb");
	if (f == 0) return 0;
	unsigned char h[44];
	ak_wav_header ((long) h, rate, channels, 16, frames * channels * 2);
	if (fwrite (h, 1, sizeof h, f) != sizeof h) { fclose (f); return 0; }
	wav_file *w = new wav_file;
	w->f = f; w->channels = channels;
	return w;
}
int wav_write (wav_file *w, const short *frames, int n)
{
	return (int) fwrite (frames, (size_t) w->channels * 2, (size_t) n, w->f);
}
int wav_end (wav_file *w)
{
	int r = fclose (w->f) == 0 ? 0 : -1;
	delete w;
	return r;
}

int soundfont_find (const char *preferred, char *out, int cap)
{
	if (out == 0 || cap <= 0) return 0;
	out[0] = 0;
	if (preferred != 0 && preferred[0])
	{
		FILE *f = fopen (preferred, "rb");
		if (f != 0) { fclose (f); snprintf (out, (size_t) cap, "%s", preferred); return 1; }
	}
	static const char *const s_default = "SD0:/res/soundfonts/GeneralUser-GS.sf2";
	FILE *f = fopen (s_default, "rb");
	if (f != 0) { fclose (f); snprintf (out, (size_t) cap, "%s", s_default); return 1; }
	DIR *d = opendir ("SD0:/res/soundfonts");
	if (d == 0) return 0;
	int found = 0;
	while (!found)
	{
		struct dirent *e = readdir (d);
		if (e == 0) break;
		size_t n = strlen (e->d_name);
		if (n > 4 && !strcasecmp (e->d_name + n - 4, ".sf2")) { snprintf (out, (size_t) cap, "SD0:/res/soundfonts/%s", e->d_name); found = 1; }
	}
	closedir (d);
	return found;
}
