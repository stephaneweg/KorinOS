// fmref.cpp -- la reference de la synthese FM d'AudioKit : le scenario de src/bin/synthtest joue
// sur fmsynth.h d'Onyx (compile sur le PC) ; imprime le CRC-32 du PCM rendu.
//   g++ -O2 -I $ONYX/kernel/include -I $ONYX/user/Kits/audiokit fmref.cpp -o fmref
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "fmsynth.h"

static uint32_t crc = 0xFFFFFFFFu;
static void add (const void *p, size_t n)
{
	const unsigned char *b = (const unsigned char *) p;
	for (size_t i = 0; i < n; i++)
	{
		crc ^= b[i];
		for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
	}
}
static short buf[2 * 8192];
static void run (int n) { while (n > 0) { int k = n > 8192 ? 8192 : n; fmsynth::render (buf, k); add (buf, k * 4); n -= k; } }
static void op (kapi_fm_op &o, int a, int d, int s, int r, int l, int m, int w, int f)
{ o.attack = a; o.decay = d; o.sustain = s; o.release = r; o.level = l; o.ksl = 0; o.mult = m; o.wave = w; o.flags = f; }

int main ()
{
	kapi_fm_instrument a, b, c;
	memset (&a, 0, sizeof a); memset (&b, 0, sizeof b); memset (&c, 0, sizeof c);
	op (a.op[0], 15, 2, 1, 2, 20, 1, 0, FM_SUSTAINED); op (a.op[1], 15, 1, 2, 3, 0, 1, 0, FM_SUSTAINED); a.feedback = 3; a.connection = 0;
	op (b.op[0], 10, 4, 3, 5, 10, 3, 1, FM_TREMOLO); op (b.op[1], 12, 3, 4, 4, 3, 2, 2, FM_VIBRATO); b.feedback = 5; b.connection = 1;
	op (c.op[0], 8, 5, 15, 6, 30, 0, 3, FM_VIBRATO | FM_TREMOLO | FM_SUSTAINED); op (c.op[1], 15, 0, 0, 7, 5, 7, 0, FM_SUSTAINED); c.feedback = 7; c.connection = 0;
	fmsynth::instrument (0, &a); fmsynth::instrument (1, &b); fmsynth::instrument (2, &c);
	fmsynth::start (0, 440000, fmsynth::WAVE_FM, 220);
	run (4096);
	fmsynth::start (1, 261626, fmsynth::WAVE_FM, 180);
	fmsynth::start (2, 659255, fmsynth::WAVE_FM, 200);
	fmsynth::start (3, 220000, fmsynth::WAVE_SINE, 120);
	fmsynth::start (4, 330000, fmsynth::WAVE_TRIANGLE, 120);
	fmsynth::start (5, 110000, fmsynth::WAVE_SAW, 90);
	fmsynth::start (6, 8000000, fmsynth::WAVE_NOISE, 60);
	fmsynth::start (7, 55000, fmsynth::WAVE_SQUARE, 60);
	run (22050);
	fmsynth::stop (0);
	run (11025);
	fmsynth::stop (-1);
	run (44100);
	printf ("0x%08X\n", ~crc);
	return 0;
}
