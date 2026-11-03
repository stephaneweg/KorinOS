// midiref.cpp -- la reference du synthe MIDI d'AudioKit : un .mid rendu sur le PC par MeltySynth (le
// synthe de Koton d'Onyx, user/Apps/koton/synth), meme SoundFont ; s16 stereo 44,1 kHz brut.
// tools/audiotest/midicmp.py compare ensuite les deux rendus.
//   g++ -O2 -I $ONYX/user/Apps/koton midiref.cpp $ONYX/user/Apps/koton/synth/*.cpp -o midiref
//   midiref a.sf2 a.mid sortie.raw
#include "synth/meltysynth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <algorithm>

struct Ev { unsigned long long tick; int order; unsigned char st, d1, d2; int tempo; };

static unsigned be (const unsigned char *p, int n) { unsigned v = 0; while (n--) v = v << 8 | *p++; return v; }
static unsigned vlq (const unsigned char *&p) { unsigned v = 0; for (;;) { unsigned char c = *p++; v = v << 7 | (c & 127); if (!(c & 128)) return v; } }
static std::vector<unsigned char> slurp (const char *n)
{
	FILE *f = fopen (n, "rb"); std::vector<unsigned char> b;
	if (!f) { perror (n); exit (1); }
	fseek (f, 0, SEEK_END); b.resize (ftell (f)); fseek (f, 0, SEEK_SET);
	if (fread (b.data (), 1, b.size (), f) != b.size ()) exit (1);
	fclose (f);
	return b;
}

int main (int argc, char **argv)
{
	if (argc < 4) return 1;
	auto sfb = slurp (argv[1]); auto mid = slurp (argv[2]);
	char err[128];
	ms::SoundFont *sf = ms::soundfont_load (sfb.data (), sfb.size (), err, sizeof err);
	if (!sf) { puts (err); return 1; }
	ms::SynthSettings st; st.sampleRate = 44100;
	ms::Synthesizer syn (sf, st);
	const unsigned char *d = mid.data ();
	int ntrk = be (d + 10, 2), div = be (d + 12, 2);
	std::vector<Ev> ev; int order = 0;
	const unsigned char *p = d + 8 + be (d + 4, 4);
	for (int t = 0; t < ntrk; t++)
	{
		unsigned len = be (p + 4, 4); const unsigned char *q = p + 8, *e = q + len; p = e;
		unsigned long long tick = 0; unsigned char run = 0;
		while (q < e)
		{
			tick += vlq (q);
			unsigned char c = *q;
			if (c & 128) { q++; if (c < 0xF0) run = c; } else c = run;
			if (c == 0xFF)
			{
				unsigned char ty = *q++; unsigned n = vlq (q);
				if (ty == 0x51) ev.push_back ({ tick, order++, 0xFF, 0, 0, (int) be (q, 3) });
				q += n;
			}
			else if (c == 0xF0 || c == 0xF7) { unsigned n = vlq (q); q += n; }
			else if ((c & 0xF0) == 0xC0 || (c & 0xF0) == 0xD0) { ev.push_back ({ tick, order++, c, q[0], 0, 0 }); q++; }
			else { ev.push_back ({ tick, order++, c, q[0], q[1], 0 }); q += 2; }
		}
	}
	std::stable_sort (ev.begin (), ev.end (), [] (const Ev &a, const Ev &b) { return a.tick < b.tick; });
	FILE *o = fopen (argv[3], "wb");
	static float l[64], r[64]; static short s[128];
	double spt = 500000.0 * 44100 / 1e6 / div, pos = 0; unsigned long long ltick = 0, done = 0;
	auto render = [&] (unsigned long long upto)
	{
		while (done < upto)
		{
			syn.render (l, r, 64);
			for (int i = 0; i < 64; i++)
			{
				float a = l[i] * 32768, b = r[i] * 32768;
				s[i * 2] = (short) (a > 32767 ? 32767 : a < -32768 ? -32768 : a);
				s[i * 2 + 1] = (short) (b > 32767 ? 32767 : b < -32768 ? -32768 : b);
			}
			fwrite (s, 2, 128, o); done += 64;
		}
	};
	for (const Ev &e : ev)
	{
		pos += (e.tick - ltick) * spt; ltick = e.tick;
		render ((unsigned long long) pos);
		if (e.st == 0xFF) { spt = e.tempo * 44100.0 / 1e6 / div; continue; }
		syn.processMidiMessage (e.st & 15, e.st & 0xF0, e.d1, e.d2);
	}
	render ((unsigned long long) pos + 2 * 44100);
	fclose (o);
	return 0;
}
