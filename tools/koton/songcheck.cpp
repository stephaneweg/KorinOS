//
// songcheck.cpp -- le banc des morceaux de Koton, sur le PC (tools/koton/make_demos.py) : le vrai moteur de Koton
// (model, generateurs, compile) charge chaque .kson et l'aplatit en notes comme le lecteur. Par piste : les notes, leur
// etendue, la fin ; pour les pistes d'instrument : la part des notes dans la gamme, et des notes tenues sur un temps
// fort qui sont dans l'accord du moment. Les blocs des generateurs (greffons) ne sont pas rendus ici : il leur faut
// leur process.
//
//   sh tools/koton/songcheck.sh fichier.kson...      (WSL)
//
#include "engine/compile.h"
#include "engine/model.h"
#include <stdio.h>
#include <stdlib.h>

using namespace kt;

static char *readAll (const char *path, unsigned long *len)
{
	FILE *f = fopen (path, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	char *b = (char *) malloc (n + 1);
	*len = fread (b, 1, n, f);
	b[*len] = 0;
	fclose (f);
	return b;
}

// la gamme de la tonalite (majeure ou mineure naturelle), en classes de hauteur
static bool inScale (const Key &k, int midi)
{
	static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mnr[7] = { 0, 2, 3, 5, 7, 8, 10 };
	static const int letter[7] = { 0, 2, 4, 5, 7, 9, 11 };
	int tonic = (letter[k.tonicLetter] + k.accidental + 12) % 12;
	int pc = ((midi - tonic) % 12 + 12) % 12;
	const int *s = k.mode == 1 ? mnr : maj;
	for (int i = 0; i < 7; i++) if (s[i] == pc) return true;
	return false;
}

int main (int argc, char **argv)
{
	int bad = 0;
	for (int a = 1; a < argc; a++)
	{
		unsigned long len = 0;
		char *text = readAll (argv[a], &len);
		if (!text) { printf ("%s : illisible\n", argv[a]); bad++; continue; }
		Project p;
		char err[200];
		if (!loadProject (text, len, p, err, sizeof err)) { printf ("%s : %s\n", argv[a], err); bad++; free (text); continue; }
		free (text);
		CompiledSong *cs = compileSong (p, 48000);
		printf ("== %s : %d pistes, %.0f temps, %.0f s\n", argv[a], p.tracks.size (), p.totalBeats (),
		        cs ? (double) cs->totalSamples () / 48000 : 0);
		for (int t = 0; cs && t < cs->tracks.size (); t++)
		{
			const CTrack &ct = cs->tracks[t];
			const Track &src = p.tracks[ct.srcIndex];
			int ons = 0, lo = 999, hi = -1, last = 0, scale = 0, strong = 0, strongIn = 0;
			for (int e = 0; e < ct.events.size (); e++)
			{
				const CEvent &ev = ct.events[e];
				if (ev.kind != EV_ON) { if (ev.slice > last) last = ev.slice; continue; }
				ons++;
				if (ev.note < lo) lo = ev.note;
				if (ev.note > hi) hi = ev.note;
				if (inScale (p.key, ev.note)) scale++;
				if (!ct.drum && ev.slice % CSPB == 0)		// sur un temps : dans l'accord ?
				{
					int r, q, inv;
					if (chordAt (p, (double) ev.slice / CSPB, &r, &q, &inv))
					{
						strong++;
						int tones[16], n = chordNotes (r, 4, q, 0, false, tones);
						for (int k = 0; k < n; k++) if ((tones[k] - ev.note) % 12 == 0) { strongIn++; break; }
					}
				}
			}
			bool gen = false;
			for (int i = 0; i < src.items.size (); i++) if (src.items[i].module && src.items[i].module->kind == M_GENERATOR) gen = true;
			printf ("  %-12s %s%5d notes", src.name.c (), ct.silent ? "(harmonie) " : ct.drum ? "(batterie)  " : "            ", ons);
			if (ons) printf ("  %3d-%3d  fin %6.1f", lo, hi, (double) last / CSPB);
			if (ons && !ct.drum && !ct.silent) printf ("  gamme %3d%%  accord %3d%%", 100 * scale / ons, strong ? 100 * strongIn / strong : 100);
			if (gen) printf ("  (generateur : rendu par son greffon)");
			else if (!ons && !ct.silent) { printf ("  VIDE"); bad++; }
			printf ("\n");
		}
		delete cs;
	}
	printf (bad ? "songcheck : %d problemes\n" : "songcheck : bon\n", bad);
	return bad ? 1 : 0;
}
