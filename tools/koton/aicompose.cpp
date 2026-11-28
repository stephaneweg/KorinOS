//
// aicompose.cpp -- « Composer avec l'IA » de Koton, hors de l'OS (le vrai moteur : ai.cpp, ai_place.cpp, compile.cpp).
//
//   aicompose prompt "style" mesures "info supplementaire" [--chords-voice]   -> le prompt complet (systeme + utilisateur)
//   aicompose apply reponse.json sortie.kson sortie.mid                       -> pose la reponse de l'IA comme Koton le
//                                                                                fait, enregistre le .kson et la sequence MIDI
//
// Le rendu audio (SoundFont -> WAV -> MP3) est fait ensuite par tools/koton/render.sh.
//
#include "engine/ai.h"
#include "engine/compile.h"
#include "engine/model.h"
#include "ui/json.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace kt;

static char *readAll (const char *path, unsigned long *len)
{
	FILE *f = fopen (path, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	char *b = (char *) malloc (n + 1);
	*len = fread (b, 1, n, f); b[*len] = 0; fclose (f);
	return b;
}

static void be (FILE *f, unsigned v, int n) { for (int i = n - 1; i >= 0; i--) fputc ((v >> (8 * i)) & 255, f); }

struct Buf { unsigned char *d; int n, cap; Buf () : d (0), n (0), cap (0) {} void put (int b) { if (n == cap) { cap = cap ? cap * 2 : 4096; d = (unsigned char *) realloc (d, cap); } d[n++] = (unsigned char) b; } };
static void vlq (Buf &b, unsigned v) { unsigned char t[5]; int k = 0; t[k++] = v & 127; while (v >>= 7) t[k++] = (v & 127) | 128; while (k) b.put (t[--k]); }

static void chunk (FILE *f, const Buf &b) { fwrite ("MTrk", 1, 4, f); be (f, b.n, 4); fwrite (b.d, 1, b.n, f); }

// un fichier MIDI de format 1, 480 ticks / temps : une piste de tempo, puis une piste par piste de Koton
static bool writeMidi (const Project &p, const CompiledSong &cs, const char *path)
{
	FILE *f = fopen (path, "wb");
	if (!f) return false;
	int nTracks = 1;
	for (int t = 0; t < cs.tracks.size (); t++) if (!cs.tracks[t].silent) nTracks++;
	fwrite ("MThd", 1, 4, f); be (f, 6, 4); be (f, 1, 2); be (f, nTracks, 2); be (f, 480, 2);
	Buf tm;
	// le tempo (une fois par changement) et la mesure
	vlq (tm, 0); tm.put (0xFF); tm.put (0x58); tm.put (4); tm.put (p.timeSigNum > 0 ? p.timeSigNum : 4);
	int den = p.timeSigDen > 0 ? p.timeSigDen : 4, lg = 0; while ((1 << lg) < den) lg++;
	tm.put (lg); tm.put (24); tm.put (8);
	double last = 0;
	for (int i = 0; i < p.tempo.size (); i++)
	{
		unsigned us = (unsigned) (60000000.0 / p.tempo[i].bpm);
		vlq (tm, (unsigned) ((p.tempo[i].beat - last) * 480 + 0.5)); last = p.tempo[i].beat;
		tm.put (0xFF); tm.put (0x51); tm.put (3); tm.put (us >> 16); tm.put ((us >> 8) & 255); tm.put (us & 255);
	}
	vlq (tm, 0); tm.put (0xFF); tm.put (0x2F); tm.put (0);
	chunk (f, tm);
	int ch = 0;
	for (int t = 0; t < cs.tracks.size (); t++)
	{
		const CTrack &ct = cs.tracks[t];
		if (ct.silent) continue;
		int c = ct.drum ? 9 : ch;
		if (!ct.drum) { ch++; if (ch == 9) ch++; if (ch > 15) ch = 15; }
		const Track &src = p.tracks[ct.srcIndex];
		Buf b;
		vlq (b, 0); b.put (0xFF); b.put (3); const char *nm = src.name.c (); vlq (b, strlen (nm)); for (const char *q = nm; *q; q++) b.put (*q);
		if (!ct.drum) { vlq (b, 0); b.put (0xC0 | c); b.put (ct.program & 127); }
		vlq (b, 0); b.put (0xB0 | c); b.put (7); b.put (iclamp ((int) (src.volume * 100), 0, 127));
		vlq (b, 0); b.put (0xB0 | c); b.put (10); b.put (iclamp (64 + (int) (src.pan * 63), 0, 127));
		int at = 0;
		for (int i = 0; i < ct.events.size (); i++)
		{
			const CEvent &e = ct.events[i];
			int tick = e.slice * (480 / CSPB);
			vlq (b, tick - at); at = tick;
			b.put ((e.kind == EV_ON ? 0x90 : 0x80) | c); b.put (e.note & 127); b.put (e.kind == EV_ON ? (e.vel ? e.vel : 1) : 0);
		}
		vlq (b, 0); b.put (0xFF); b.put (0x2F); b.put (0);
		chunk (f, b);
	}
	fclose (f);
	return true;
}

int main (int argc, char **argv)
{
	if (argc >= 5 && !strcmp (argv[1], "prompt"))
	{
		Project p;
		AiRequest r; r.kind = AI_COMPOSE; r.style = argv[2]; r.measures = atoi (argv[3]); r.intention = argv[4];
		r.fullMelody = true;		// « Melodie en riff »
		r.drums = true;			// « Piste batterie »
		r.chordsVoice = false;		// « L'IA pose les accords » (les degres, la piste d'accords du moteur)
		r.english = argc > 5 && !strcmp (argv[5], "--english");
		Str sys, usr; char err[200] = "";
		if (!aiBuildPrompt (p, r, sys, usr, err, sizeof err)) { fprintf (stderr, "%s\n", err); return 1; }
		Str full; aiFullPrompt (sys, usr, full);
		fputs (full.c (), stdout); fputc ('\n', stdout);
		return 0;
	}
	if (argc >= 5 && !strcmp (argv[1], "apply"))
	{
		unsigned long len = 0; char *text = readAll (argv[2], &len);
		if (!text) { fprintf (stderr, "%s : illisible\n", argv[2]); return 1; }
		Project p; AiRequest r; r.kind = AI_COMPOSE; r.fullMelody = true; r.drums = true; r.english = false;
		char sum[300], err[300] = "";
		if (!aiCheckReply (r, text, sum, sizeof sum)) { fprintf (stderr, "reponse inutilisable : %s\n", sum); return 1; }
		fprintf (stderr, "%s\n", sum);
		if (!aiApplyReply (p, r, text, err, sizeof err)) { fprintf (stderr, "pose impossible : %s\n", err); return 1; }
		json::Writer w (true); saveProject (p, w);
		FILE *f = fopen (argv[3], "wb"); if (!f) return 1; fwrite (w.data (), 1, w.size (), f); fclose (f);
		CompiledSong *cs = compileSong (p, 48000);
		if (!cs) { fprintf (stderr, "compilation impossible\n"); return 1; }
		fprintf (stderr, "%d pistes, %.0f temps, %.1f s\n", cs->tracks.size (), p.totalBeats (), (double) cs->totalSamples () / 48000);
		if (!writeMidi (p, *cs, argv[4])) return 1;
		return 0;
	}
	fprintf (stderr, "usage : aicompose prompt \"style\" mesures \"info\" | apply reponse.json out.kson out.mid\n");
	return 2;
}
