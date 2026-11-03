//
// fmsout.h -- l'export en FM Song (.fms, le format de FM Tracker) : la sequence ramenee a ce que permet ce format.
//
// Huit canaux, une seule note a la fois par canal, un instrument par canal, pas de nuances, au plus 64 motifs de 256
// lignes, et une ligne dure un multiple entier de 50 ms. D'ou :
//  * chaque partie a un canal, et ceux qui restent vont aux parties qui jouent des accords ; au-dela de huit parties, ou
//    quand un canal est pris, des notes tombent : le resultat dit combien ;
//  * la grille est celle des notes si un multiple de 50 ms en approche le temps a 4 % pres (le tempo bouge alors un peu) ;
//    sinon la grille est de 50 ms et le tempo reste juste, chaque note a sa place a 25 ms pres ;
//  * l'instrument vient des .FMI de FM Tracker, choisi d'apres le programme General MIDI, la meme table que la banque FM
//    d'AudioKit (tools/audiotest/gen_gmbank.py) ; pour les percussions, d'apres la touche la plus jouee.
//
#ifndef ARABESQUE_FMSOUT_H
#define ARABESQUE_FMSOUT_H

#include "song.h"

#ifndef FMI_DIR
#define FMI_DIR "SD0:/apps/fmtracker.app/ins/"
#endif

enum { FMS_CH = 8, FMS_MAXPAT = 64, FMS_MAXROWS = 256, FMS_INSSZ = 26 };

static const char FMI_GM[] =
	"PIANO PIANO BELPIANO BELPIANO ELPIANO ELPIANO HARPSI HARPSI "
	"CELESTA VIBRA BELLS VIBRA MARIMBA XYLOFONE BELLS BANJO "
	"ORGAN ORGAN ORGAN PIPES ORGAN ACCORDN HARMONCA ACCORDN "
	"GUITAR GUITAR ELGUITAR ELGUITAR ELGUITAR ELGUITAR ELGUITAR HARP "
	"BASSHARP POPBASS POPBASS POPBASS SLAPBAS SLAPBAS POPBASS SLAPBAS "
	"VIOLIN ALTOVIOL CELLO CONTRAB STRINGS HARP HARP TOM "
	"STRINGS STRINGS STRINGS STRINGS FLUTE FLUTE SYNTH1 BRASS "
	"TRUMPET TROMBONE TUBA TRUMPET FREHORN BRASS SFTBRASS SFTBRASS "
	"SAXAFONE SAXAFONE TENSAX TENSAX OBOE ENGLHORN BASSOON CLARINET "
	"PICCOLO FLUTE FLUTE FLUTE PIPES FLUTE FLUTE FLUTE "
	"SQUARE SYNTH2 SYNTH1 SYNTH1 ELGUITAR SYNTH1 SYNTH2 SYNTH2 "
	"SYNTH1 STRINGS SYNTH2 STRINGS STRINGS SFTBRASS STRINGS SYNTH1 "
	"MOON BELLS MARS MOON ALIEN MOON MARS MOON "
	"SITAR BANJO BANJO HARP VIBRA BAGPIPES VIOLIN OBOE "
	"BELLS TINCAN TINCAN LOGDRUM TOM TOM SYNDRUM CYMBAL "
	"SCRATCH NOISEON NOISEIN CHIRP DRIP DRILL NOISEIN LASER1 ";

// touches 35..81 : l'instrument et la note qu'il joue
struct FmiDrum { const char *name; int key; };
static const FmiDrum FMI_DRUM[47] = {
	{ "BDRUM1", 40 }, { "BDRUM2", 40 }, { "CLAVES", 72 }, { "SNARE1", 60 }, { "BRUSH", 72 }, { "SNARE2", 60 }, { "TOM", 41 }, { "HIHAT", 84 },
	{ "TOM", 45 }, { "HIHAT", 84 }, { "TOM", 48 }, { "HIHAT", 80 }, { "TOM", 52 }, { "TOM", 55 }, { "CYMBAL", 72 }, { "TOM", 59 },
	{ "CYMBAL", 84 }, { "CYMBAL", 64 }, { "TRIANGLE", 84 }, { "TINCAN", 84 }, { "CYMBAL", 79 }, { "TINCAN", 72 }, { "CYMBAL", 69 }, { "CHIRP", 60 },
	{ "CYMBAL", 88 }, { "LOGDRUM", 72 }, { "LOGDRUM", 67 }, { "LOGDRUM", 64 }, { "LOGDRUM", 60 }, { "LOGDRUM", 55 }, { "TOM", 60 }, { "TOM", 55 },
	{ "TINCAN", 76 }, { "TINCAN", 69 }, { "BRUSH", 84 }, { "BRUSH", 88 }, { "CHIRP", 84 }, { "CHIRP", 76 }, { "CLAVES", 84 }, { "CLAVES", 79 },
	{ "CLAVES", 91 }, { "LOGDRUM", 84 }, { "LOGDRUM", 79 }, { "CHIRP", 72 }, { "CHIRP", 67 }, { "TRIANGLE", 96 }, { "TRIANGLE", 91 } };

static void fmi_gm_name (int program, char *out)
{
	const char *s = FMI_GM;
	for (int i = 0; i < program; i++) { while (*s != ' ') s++; s++; }
	int k = 0;
	while (s[k] != ' ' && k < 8) { out[k] = s[k]; k++; }
	out[k] = 0;
}
// le .FMI : deux lignes, puis des paires dans cet ordre, puis « feedback,connexion »
static bool fmi_load (const char *name, uint8_t *p)
{
	static const int AT[12] = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 24 };
	char path[128];
	snprintf (path, sizeof path, FMI_DIR "%s.FMI", name);
	char *b = read_all (path);
	if (!b) return false;
	memset (p, 0, FMS_INSSZ);
	const char *s = b; int line = 0;
	while (*s)
	{
		const char *e = s;
		while (*e && *e != '\n') e++;
		if (line >= 2 && line < 14)
		{
			char *m; long u = strtol (s, &m, 10);
			long v = *m == ',' ? strtol (m + 1, 0, 10) : 0;
			p[AT[line - 2]] = (uint8_t) u; p[AT[line - 2] + 1] = (uint8_t) v;
		}
		else if (line == 14)
		{
			char *m; long u = strtol (s, &m, 10);
			p[22] = (uint8_t) u; p[23] = (uint8_t) (*m == ',' ? strtol (m + 1, 0, 10) : 0);
		}
		line++;
		s = *e ? e + 1 : e;
	}
	free (b);
	return line >= 15;
}
static void fmi_default (uint8_t *p)		// (FM Tracker absent : une sinusoide simple)
{
	static const uint8_t D[FMS_INSSZ] = { 15, 15, 0, 3, 7, 3, 6, 6, 63, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1 };
	memcpy (p, D, FMS_INSSZ);
}

static int fms_note_byte (int key)
{
	static const uint8_t N[12] = { 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7 }, S[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
	while (key < 12) key += 12;
	while (key >= 108) key -= 12;
	int oct = key / 12 - 1;
	return S[key % 12] * 64 + oct * 8 + N[key % 12];
}
static uint32_t gcd32 (uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }

struct FmsResult { int notes, dropped, patterns, speed, parts; bool regular, truncated; double tempo; };

static void pad (uint8_t *&o, const char *s, int n)
{
	int l = (int) strlen (s); if (l > n) l = n;
	memcpy (o, s, (size_t) l); memset (o + l, ' ', (size_t) (n - l));
	o += n;
}

static bool fms_export (const Song &song, const Style &st, const char *path, const char *title, FmsResult &res)
{
	memset (&res, 0, sizeof res);
	res.tempo = song.tempo;
	// la grille
	uint32_t g = 0, last = 0;
	for (int i = 0; i < NPART; i++)
		for (int k = 0; k < song.tr[i].count; k++)
		{
			const Note &m = song.tr[i].n[k];
			if (m.kind != NK_NOTE) continue;
			g = gcd32 (m.tick, g);
			if (m.tick + m.dur > last) last = m.tick + m.dur;
		}
	if (!last) return false;
	if (!g) g = PPQ / 4;
	const double Q = PPQ, beat = 60.0 / song.tempo;	// secondes par noire
	double rowSec = beat * g / Q, bestErr = 1; int bestSub = 0, bestSpeed = 0;
	for (int sub = 1; sub <= 6; sub++)
	{
		double s = rowSec * 20 / sub;
		int r = (int) (s + 0.5);
		if (r < 1) continue;
		double e = (r > s ? r - s : s - r) / s;
		if (e < bestErr - 1e-9) { bestErr = e; bestSub = sub; bestSpeed = r; }
	}
	int speed; double rowsPerTick;
	res.regular = bestErr <= 0.04;
	if (res.regular) { speed = bestSpeed; rowsPerTick = (double) bestSub / g; res.tempo = 60.0 / (speed * 0.05 * Q * rowsPerTick); }
	else { speed = 1; rowsPerTick = beat / Q * 20; }
	res.speed = speed;
	int rowsPerBar = (int) (rowsPerTick * Q * BEATS + 0.5);
	int rpp = FMS_MAXROWS;
	if (rowsPerBar > 0 && rowsPerBar <= FMS_MAXROWS) rpp = FMS_MAXROWS / rowsPerBar * rowsPerBar;
	long total = (long) (last * rowsPerTick + 0.5) + 2;
	if (total > (long) rpp * FMS_MAXPAT) { total = (long) rpp * FMS_MAXPAT; res.truncated = true; }
	int npat = (int) ((total + rpp - 1) / rpp);
	total = (long) npat * rpp;
	// les canaux : un par partie, puis ceux qui restent aux accords
	int want[NPART], got[NPART], first[NPART], nparts = 0;
	for (int i = 0; i < NPART; i++)
	{
		const Track &t = song.tr[i];
		want[i] = got[i] = 0; first[i] = 0;
		int tail[8], nt = 0;
		for (int k = 0; k < t.count; k++)
		{
			const Note &m = t.n[k];
			if (m.kind != NK_NOTE) continue;
			int j = 0;
			while (j < nt) { if (tail[j] <= (int) m.tick) tail[j] = tail[--nt]; else j++; }
			if (nt < 8) tail[nt++] = (int) (m.tick + m.dur);
			if (nt > want[i]) want[i] = nt;
		}
		if (want[i]) nparts++;
	}
	res.parts = nparts;
	int ch = 0;
	for (int i = 0; i < NPART && ch < FMS_CH; i++) if (want[i]) { got[i] = 1; ch++; }
	for (bool more = true; more && ch < FMS_CH; )
	{
		more = false;
		for (int i = 0; i < NPART && ch < FMS_CH; i++) if (got[i] && got[i] < want[i]) { got[i]++; ch++; more = true; }
	}
	int nextc = 0;
	for (int i = 0; i < NPART; i++) { first[i] = nextc; nextc += got[i]; }
	// les notes dans la grille
	uint8_t *cells = (uint8_t *) calloc ((size_t) total, FMS_CH);
	uint8_t ins[FMS_CH][FMS_INSSZ]; char iname[FMS_CH][9];
	for (int c = 0; c < FMS_CH; c++) { fmi_default (ins[c]); strcpy (iname[c], "SINUS"); }
	for (int i = 0; i < NPART; i++)
	{
		const Track &t = song.tr[i];
		if (!want[i]) continue;
		int program = st.p[i].patch, keys[128]; memset (keys, 0, sizeof keys);
		bool drums = false, progSeen = false;
		for (int k = 0; k < t.count; k++)
		{
			const Note &m = t.n[k];
			if (m.kind == NK_PROGRAM && !progSeen) { program = m.key; progSeen = true; }
			if (m.kind == NK_NOTE) { keys[m.key]++; if (m.ch == 9) drums = true; }
		}
		char nm[9];
		if (drums)
		{
			int b = 35;
			for (int k = 35; k <= 81; k++) if (keys[k] > keys[b]) b = k;
			strcpy (nm, FMI_DRUM[b - 35].name);
		}
		else fmi_gm_name (program, nm);
		int ends[FMS_CH];
		for (int c = 0; c < got[i]; c++)
		{
			int cc = first[i] + c;
			ends[c] = -1;
			if (fmi_load (nm, ins[cc])) strcpy (iname[cc], nm);
		}
		for (int k = 0; k < t.count; k++)
		{
			const Note &m = t.n[k];
			if (m.kind != NK_NOTE) continue;
			res.notes++;
			long r = (long) (m.tick * rowsPerTick + 0.5), e = (long) ((m.tick + m.dur) * rowsPerTick + 0.5);
			if (e <= r) e = r + 1;
			if (!got[i] || r >= total) { res.dropped++; continue; }
			int c = -1;
			for (int j = 0; j < got[i]; j++) if (ends[j] <= r) { c = j; break; }
			if (c < 0) { res.dropped++; continue; }
			ends[c] = (int) e;
			int key = m.key;
			if (drums) key = key >= 35 && key <= 81 ? FMI_DRUM[key - 35].key : 60;
			uint8_t *col = cells + (first[i] + c) * total;
			col[r] = (uint8_t) fms_note_byte (key);
			if (e < total) col[e] = 128;
		}
	}
	// le fichier
	long size = 107 + FMS_CH * 34 + npat * (12L + FMS_CH * rpp);
	uint8_t *b = (uint8_t *) malloc ((size_t) size), *o = b;
	memcpy (o, "fm-song-project", 15); o += 15;
	pad (o, title, 20); pad (o, "Arabesque", 20); pad (o, "", 50);
	*o++ = (uint8_t) npat; *o++ = (uint8_t) (npat >> 8);
	for (int c = 0; c < FMS_CH; c++) { pad (o, iname[c], 8); memcpy (o, ins[c], FMS_INSSZ); o += FMS_INSSZ; }
	for (int p = 0; p < npat; p++)
	{
		*o++ = (uint8_t) rpp; *o++ = (uint8_t) (rpp >> 8);
		*o++ = (uint8_t) speed; *o++ = (uint8_t) (speed >> 8);
		memset (o, 0, FMS_CH); o += FMS_CH;
		for (int c = 0; c < FMS_CH; c++) { memcpy (o, cells + c * total + (long) p * rpp, (size_t) rpp); o += rpp; }
	}
	res.patterns = npat;
	bool ok = write_all (path, b, size);
	free (b); free (cells);
	return ok;
}

#endif
