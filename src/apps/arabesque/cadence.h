//
// cadence.h -- une grille d'accords d'apres une cadence et un style, comme dans Koton (src/apps/koton/engine/theory.cpp,
// cadence) : les memes styles, la meme graine, les memes tirages (le System.Random de .NET, refait au bit pres) ; port
// de Cadences.cs du port Windows, une meme graine donne la meme grille des deux cotes. Koton donne des accords en
// fondamentale absolue et qualite ; ils sont ecrits ici en degres de la gamme en cours (un bemol ou un diese s'ils n'y
// sont pas), avec leur qualite, leur septieme et leurs couleurs.
//
#ifndef ARABESQUE_CADENCE_H
#define ARABESQUE_CADENCE_H

#include "chords.h"

enum { CADENCE_STYLES = 30, CADENCE_MAX = 64 };
static const char *const CADENCE_NAMES[CADENCE_STYLES] = {
	"Auto (riche)", "Parfaite (V - I)", "Plagale (IV - I)", "Jazz (ii - V - I)", "Turnaround (I-vi-ii-V)",
	"Pop (I-V-vi-IV)", "Doo-wop (I-vi-IV-V)", "EDM (vi-IV-I-V)", "Royal road (IV-V-iii-vi)", "Pachelbel (canon)",
	"Cycle des quintes, descendant", "Blues (I-IV-V)", "Blues mineur", "Andalouse (i-♭VII-♭VI-V)", "Phrygienne / espagnole",
	"Dorienne (i-IV)", "Mixolydienne (I-♭VII)", "Médiantes chromatiques", "Backdoor (ii-♭VII7-I)", "Substitution tritonique",
	"Demi-cadence (vers V)", "Rompue (V vers vi)", "Morceau entier (auto)", "Coltrane (Giant Steps)", "Forme AABA",
	"Cycle des quintes, montant", "Baroque (cycle, V-I, tierce picarde)", "Modale / médiantes (harpe)",
	"Éolienne / épique (♭VI-♭VII-i)", "Riche (emprunts modaux)" };

// le System.Random de .NET a graine (l'algorithme soustractif de Knuth), comme NetRandom de Koton
struct NetRandom
{
	int sa[56], inext, inextp;
	static int wsub (int a, int b) { return (int) ((unsigned) a - (unsigned) b); }
	explicit NetRandom (int seed)
	{
		int subtraction = seed == (-2147483647 - 1) ? 2147483647 : (seed < 0 ? -seed : seed);
		int mj = wsub (161803398, subtraction);
		sa[55] = mj;
		int mk = 1, ii = 0;
		for (int i = 1; i < 55; i++)
		{
			if ((ii += 21) >= 55) ii -= 55;
			sa[ii] = mk;
			mk = wsub (mj, mk);
			if (mk < 0) mk += 2147483647;
			mj = sa[ii];
		}
		for (int k = 1; k < 5; k++)
			for (int i = 1; i < 56; i++)
			{
				int n = i + 30;
				if (n >= 55) n -= 55;
				sa[i] = wsub (sa[i], sa[1 + n]);
				if (sa[i] < 0) sa[i] += 2147483647;
			}
		inext = 0; inextp = 21; sa[0] = 0;
	}
	int sample_int ()
	{
		if (++inext >= 56) inext = 1;
		if (++inextp >= 56) inextp = 1;
		int r = wsub (sa[inext], sa[inextp]);
		if (r == 2147483647) r--;
		if (r < 0) r += 2147483647;
		sa[inext] = r;
		return r;
	}
	int next (int maxValue) { return maxValue <= 0 ? 0 : (int) (sample_int () * (1.0 / 2147483647) * maxValue); }
};

struct CadCell { int off, q; };

// les qualites de Koton -> qualite, sus, septieme, ajouts d'Arabesque (Chord)
static const int CAD_Q[35][4] = {
	{ 1, 0, 0, 0 }, { 2, 0, 0, 0 }, { 3, 0, 0, 0 }, { 4, 0, 0, 0 }, { 1, 1, 0, 0 }, { 1, 2, 0, 0 },
	{ 1, 0, 3, 0 }, { 2, 0, 2, 0 }, { 1, 0, 2, 0 }, { 3, 0, 2, 0 }, { 3, 0, 5, 0 },
	{ 1, 0, 4, 0 }, { 2, 0, 4, 0 }, { 1, 0, 0, EXT_9 }, { 2, 0, 0, EXT_9 }, { 1, 0, 2, EXT_9 },
	{ 1, 0, 3, EXT_9 }, { 2, 0, 2, EXT_9 }, { 1, 0, 2, EXT_B9 }, { 1, 0, 2, EXT_S9 },
	{ 1, 0, 2, EXT_9 | EXT_11 }, { 1, 0, 2, EXT_9 | EXT_13 }, { 1, 0, 3, EXT_S11 },
	{ 1, 2, 2, 0 }, { 1, 1, 2, 0 }, { 1, 2, 2, EXT_9 }, { 1, 1, 2, EXT_9 }, { 1, 2, 4, 0 }, { 1, 1, 4, 0 },
	{ 1, 2, 3, 0 }, { 1, 1, 3, 0 }, { 1, 2, 3, EXT_9 }, { 1, 1, 3, EXT_9 }, { 1, 2, 0, EXT_9 },
	{ 4, 0, 2, 0 } };

// la gamme est-elle mineure ? (sa tierce)
static bool cadence_minor (unsigned mask) { return (mask >> 3 & 1) && !(mask >> 4 & 1); }

// la suite de Koton : n cellules (decalage depuis la tonique, qualite de Koton) ; startDegree 0..6
static int cadence_koton (bool minor, int startDegree, int n, int style, int seed, CadCell *out)
{
	static const int MAJ_PCS[7] = { 0, 2, 4, 5, 7, 9, 11 }, MIN_PCS[7] = { 0, 2, 3, 5, 7, 8, 10 };
	static const int MAJ_Q[7] = { 0, 7, 7, 6, 8, 7, 9 }, MIN_Q[7] = { 1, 9, 6, 7, 8, 6, 8 };
	static const int MAJ_TONIC[3] = { 0, 11, 13 }, MIN_TONIC[3] = { 1, 12, 14 }, DOM_COLOUR[3] = { 8, 15, 21 };
	static const int NEXT[7][6] = { { 1, 2, 3, 4, 5, 6 }, { 4, 6 }, { 3, 5 }, { 0, 1, 4, 6 }, { 0, 5 }, { 1, 3, 4 }, { 0 } };
	static const int NEXT_LEN[7] = { 6, 2, 2, 4, 2, 3, 1 };
	if (n < 1) n = 1;
	if (n > CADENCE_MAX) n = CADENCE_MAX;
	NetRandom rng (seed);
	const int *pcs = minor ? MIN_PCS : MAJ_PCS, *qual = minor ? MIN_Q : MAJ_Q;
	int s0 = ((startDegree % 7) + 7) % 7;
	auto D = [&] (int deg) { int d = ((deg % 7) + 7) % 7; CadCell c = { pcs[d], qual[d] }; return c; };
	auto C = [&] (int off, int q) { CadCell c = { (off % 12 + 12) % 12, q }; return c; };
	auto triad = [] (int q) { return q == 6 || q == 8 ? 0 : q == 7 ? 1 : q == 9 ? 2 : q; };
	auto Dt = [&] (int deg) { CadCell c = D (deg); c.q = triad (c.q); return c; };
	CadCell cells[CADENCE_MAX * 2]; int nc = 0;
	auto func = [&] (const int *tail, int tn)
	{
		int degs[CADENCE_MAX * 2], nd = 0;
		degs[nd++] = s0;
		int headLen = n - tn > 1 ? n - tn : 1;
		for (int i = 1; i < headLen; i++) { int last = degs[nd - 1]; degs[nd++] = NEXT[last][rng.next (NEXT_LEN[last])]; }
		for (int i = 0; i < tn; i++) degs[nd++] = tail[i];
		int skip = nd > n ? nd - n : 0;
		nc = 0;
		for (int i = skip; i < nd; i++) cells[nc++] = D (degs[i]);
	};
	auto tile = [&] (const CadCell *p, int pn) { nc = 0; for (int i = 0; i < n; i++) cells[nc++] = p[i % pn]; };
	bool toTonic = true, triadic = false, picardy = false;
	switch (style)
	{
	case 1: { int t[] = { 4, 0 }; func (t, 2); } break;
	case 2: { int t[] = { 3, 0 }; func (t, 2); } break;
	case 3: { int t[] = { 1, 4, 0 }; func (t, 3); } break;
	case 4: { CadCell p[] = { D (0), D (5), D (1), D (4) }; tile (p, 4); } break;
	case 5: { CadCell p[] = { D (0), D (4), D (5), D (3) }; tile (p, 4); } break;
	case 6: { CadCell p[] = { D (0), D (5), D (3), D (4) }; tile (p, 4); } break;
	case 7: { CadCell p[] = { D (5), D (3), D (0), D (4) }; tile (p, 4); } break;
	case 8: { CadCell p[] = { D (3), D (4), D (2), D (5) }; tile (p, 4); } break;
	case 9: { CadCell p[] = { D (0), D (4), D (5), D (2), D (3), D (0), D (3), D (4) }; tile (p, 8); } break;
	case 10: { int d = s0; nc = 0; for (int i = 0; i < n; i++) { cells[nc++] = Dt (d); d = (d + 3) % 7; } triadic = true; } break;
	case 11: { CadCell p[] = { D (0), D (0), D (0), D (0), D (3), D (3), D (0), D (0), D (4), D (3), D (0), D (4) }; tile (p, 12); } break;
	case 12: { CadCell p[] = { C (0, 1), C (0, 1), C (0, 1), C (0, 1), C (5, 1), C (5, 1), C (0, 1), C (0, 1), C (7, 8), C (5, 1), C (0, 1), C (7, 8) }; tile (p, 12); } break;
	case 13: { CadCell p[] = { C (0, 1), C (10, 0), C (8, 0), C (7, 8) }; tile (p, 4); } break;
	case 14: { CadCell p[] = { C (0, 1), C (1, 0), C (0, 1), C (7, 8) }; tile (p, 4); } break;
	case 15: { CadCell p[] = { C (0, 1), C (5, 0) }; tile (p, 2); } break;
	case 16: { CadCell p[] = { C (0, 0), C (10, 0) }; tile (p, 2); } break;
	case 17: { CadCell p[] = { C (0, 0), C (3, 0), C (8, 0) }; tile (p, 3); } break;
	case 18: { CadCell p[] = { D (1), C (10, 8), D (0) }; tile (p, 3); } break;
	case 19: { CadCell p[] = { D (1), C (1, 8), D (0) }; tile (p, 3); } break;
	case 20: { int t[] = { 4 }; func (t, 1); toTonic = false; } break;
	case 21: { int t[] = { 4, 5 }; func (t, 2); toTonic = false; } break;
	case 22:
	{
		// une forme de chanson : deux boucles, finissant sur ii - V - (I)
		CadCell loops[5][8] = {
			{ D (0), D (4), D (5), D (3) }, { D (0), D (5), D (3), D (4) }, { D (5), D (3), D (0), D (4) },
			{ D (3), D (4), D (2), D (5) }, { D (0), D (4), D (5), D (2), D (3), D (0), D (3), D (4) } };
		int lens[5] = { 4, 4, 4, 4, 8 };
		int verse = rng.next (5), chorus = rng.next (5);
		nc = 0;
		while (nc < n)
		{
			int sec = nc < n / 2 ? verse : chorus;
			for (int i = 0; i < lens[sec] && nc < n; i++) cells[nc++] = loops[sec][i];
		}
		if (nc >= 3) { cells[nc - 3] = D (1); cells[nc - 2] = D (4); }
	} break;
	case 23: { CadCell p[] = { C (0, 6), C (3, 8), C (8, 6), C (11, 8), C (4, 6), C (7, 8) }; tile (p, 6); } break;
	case 24:
	{
		CadCell A[] = { D (0), D (5), D (1), D (4) }, B[] = { D (3), D (3), D (4), D (4) };
		const CadCell *secs[] = { A, A, B, A };
		int per = n / 4 > 1 ? n / 4 : 1;
		nc = 0;
		for (int si = 0; si < 4; si++) for (int i = 0; i < per; i++) cells[nc++] = secs[si][i % 4];
		while (nc < n) cells[nc++] = D (0);
		if (nc > n) nc = n;
	} break;
	case 25: { int d = s0; nc = 0; for (int i = 0; i < n; i++) { cells[nc++] = Dt (d); d = (d + 4) % 7; } triadic = true; } break;
	case 26:
	{
		int d = s0; nc = 0;
		for (int i = 0; i < n; i++) { cells[nc++] = Dt (d); d = (d + 3) % 7; }
		if (n >= 2) cells[n - 2] = D (4);
		triadic = true; picardy = minor;
	} break;
	case 27: { CadCell p[] = { D (0), D (5), C (3, 0), D (3) }; tile (p, 4); } break;
	case 28: { CadCell p[] = { C (0, minor ? 1 : 0), C (8, 0), C (10, 0) }; tile (p, 3); } break;
	case 29: { CadCell p[] = { D (0), D (5), C (8, 0), D (4) }; tile (p, 4); } break;
	default: { int t[] = { rng.next (2) == 0 ? 1 : 3, 4, 0 }; func (t, 3); } break;
	}
	if (toTonic && nc > 0) { CadCell t = { pcs[0], minor ? 1 : 0 }; cells[nc - 1] = t; }
	for (int i = 0; i < nc; i++)
	{
		int off = (cells[i].off % 12 + 12) % 12, q = cells[i].q;
		if (off == 0) q = triadic ? (minor ? 1 : 0) : (minor ? MIN_TONIC : MAJ_TONIC)[rng.next (3)];
		else if (q == 8) q = DOM_COLOUR[rng.next (3)];
		out[i].off = off; out[i].q = q;
	}
	if (picardy && nc > 0 && out[nc - 1].off == 0) { out[nc - 1].off = 0; out[nc - 1].q = 0; }
	return nc;
}

// la grille : n accords, un par mesure, en degres de la gamme (mask : tonique au bit 0) ; lead : voice leading -> leur nombre
static int cadence_make (unsigned mask, int startDegree, int n, int style, int seed, bool lead, Chord *out)
{
	CadCell k[CADENCE_MAX];
	int nk = cadence_koton (cadence_minor (mask), startDegree, n, style, seed, k);
	for (int i = 0; i < nk; i++)
	{
		int off = k[i].off, q = k[i].q, deg = 0, acc = 0;
		// le degre : dans la gamme, sinon un bemol de plus haut, sinon un diese de plus bas
		for (int d = 1; d <= 7 && !deg; d++) if (degree_offset (d, mask) == off) deg = d;
		for (int d = 1; d <= 7 && !deg; d++) if (degree_offset (d, mask) == (off + 1) % 12) { deg = d; acc = -1; }
		for (int d = 1; d <= 7 && !deg; d++) if (degree_offset (d, mask) == (off + 11) % 12) { deg = d; acc = 1; }
		if (!deg) deg = 1;
		q = q < 0 ? 0 : q > 34 ? 34 : q;
		Chord &c = out[i];
		memset (&c, 0, sizeof c);
		c.deg = deg; c.acc = acc; c.qual = CAD_Q[q][0]; c.sus = CAD_Q[q][1]; c.sev = CAD_Q[q][2]; c.ext = CAD_Q[q][3];
		c.inv = lead ? CHORD_LEAD : 0; c.open = false;
	}
	return nk;
}

#endif
