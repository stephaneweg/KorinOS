//
// chords.h -- la grille d'accords d'Arabesque : un accord par mesure, note par son degre dans la gamme en cours. Une
// mesure vide prolonge l'accord d'avant ; « sans accord » rend la composition libre. La grille a sa longueur et, si elle
// boucle, se repete tout le long du morceau. Memes calculs que Chords.cs du port Windows.
//
// Un accord : le degre (I a VII, altere d'un bemol ou d'un diese), la qualite (celle de la gamme, ou majeure, mineure,
// diminuee, augmentee, quinte seule), sus2 ou sus4, la septieme ou la sixte, les ajouts (9, 11, 13) et les alterations
// (b5 #5 b9 #9 #11 b13), le renversement ou le voice leading (la basse et les voix vont a la note de l'accord la plus
// proche), l'open voicing (les voix ecartees). Les parties s'en servent selon leur reglage Harmonie (compose.h).
//
#ifndef ARABESQUE_CHORDS_H
#define ARABESQUE_CHORDS_H

#include "song.h"

enum { CHORD_NC = 8 };			// deg : 0 vide (l'accord d'avant continue), 1..7, CHORD_NC sans accord
enum { CHORD_LEAD = 4 };		// inv : chaque partie rejoint l'accord par le plus court chemin
enum { EXT_9 = 1, EXT_11 = 2, EXT_13 = 4, EXT_B9 = 8, EXT_S9 = 16, EXT_S11 = 32, EXT_B13 = 64, EXT_B5 = 128, EXT_S5 = 256 };
static const char *const EXT_NAMES[9] = { "9", "11", "13", "♭9", "♯9", "♯11", "♭13", "♭5", "♯5" };
static const char *const NOTE_NAMES[12] = { "Do", "Do♯", "Ré", "Mi♭", "Mi", "Fa", "Fa♯", "Sol", "La♭", "La", "Si♭", "Si" };

struct Chord
{
	int deg, acc;				// acc : -1 bemol, 0, 1 diese
	int qual;				// 0 celle de la gamme, 1 majeur, 2 mineur, 3 diminue, 4 augmente, 5 quinte seule
	int sus;				// 0, 1 sus2, 2 sus4
	int sev;				// 0 rien, 1 septieme de la gamme, 2 7, 3 maj7, 4 6, 5 °7
	int ext;				// bits EXT_*
	int inv;				// 0 fondamentale, 1 la tierce a la basse, 2 la quinte, 3 la septieme, CHORD_LEAD
	bool open;				// open voicing : les voix des parties ecartees
	bool empty () const { return deg == 0; }
};

// un accord pose sur la gamme : ses intervalles depuis la fondamentale, ses notes (bits, do = 0), sa basse
struct ChordTones
{
	int root, bass;				// classes de hauteur absolues
	unsigned pcs;				// douze bits, do = bit 0
	unsigned clash;				// les notes de la gamme a un demi-ton d'une note de l'accord etrangere a la gamme
	int n, iv[7];				// les intervalles : fondamentale, tierce, quinte, septieme, ajouts
	int third, fifth, seventh;		// -1 absente
	void add (int v) { if (n < 7) iv[n++] = v; }
};

// la gamme en bits absolus (mask a la tonique au bit 0)
static unsigned harm_abs (unsigned mask, int root) { return ((mask << root) | (mask >> (12 - root))) & 0xFFF; }
static int harm_count (unsigned m) { int n = 0; for (int k = 0; k < 12; k++) if (m >> k & 1) n++; return n; }

// le degre -> demi-tons au-dessus de la tonique : la gamme si elle a sept notes, sinon la gamme majeure
static int degree_offset (int deg, unsigned mask)
{
	static const int MAJOR[7] = { 0, 2, 4, 5, 7, 9, 11 };
	if (harm_count (mask) == 7)
	{
		int n = 0;
		for (int k = 0; k < 12; k++) if ((mask >> k & 1) && n++ == deg - 1) return k;
	}
	return MAJOR[(deg - 1) % 7];
}

static ChordTones chord_tones (const Chord &c, unsigned mask, int root)
{
	ChordTones t;
	memset (&t, 0, sizeof t);
	int r = degree_offset (c.deg, mask) + c.acc;		// depuis la tonique
	unsigned diat = harm_count (mask) == 7 ? mask : 0xAB5;	// (les qualites « de la gamme » : la majeure, si elle n'a pas sept notes)
	auto in = [&] (int i) { return (diat >> (((r + i) % 12 + 12) % 12) & 1) != 0; };
	int third, fifth;
	switch (c.qual)
	{
	case 1: third = 4; fifth = 7; break;
	case 2: third = 3; fifth = 7; break;
	case 3: third = 3; fifth = 6; break;
	case 4: third = 4; fifth = 8; break;
	case 5: third = -1; fifth = 7; break;
	default:
		third = in (4) ? 4 : in (3) ? 3 : 4;
		fifth = in (7) ? 7 : in (6) ? 6 : in (8) ? 8 : 7;
		break;
	}
	if (c.sev == 5) { if (c.qual == 0) third = 3; fifth = 6; }	// °7 : l'accord est diminue
	if (c.sus == 1) third = 2; else if (c.sus == 2) third = 5;
	if (c.ext & EXT_B5) fifth = 6; else if (c.ext & EXT_S5) fifth = 8;
	int sev = -1;
	switch (c.sev)
	{
	case 1: sev = in (10) ? 10 : in (11) ? 11 : in (9) ? 9 : 10; break;
	case 2: sev = 10; break;
	case 3: sev = 11; break;
	case 4: case 5: sev = 9; break;
	}
	t.third = third; t.fifth = fifth; t.seventh = sev;
	t.add (0);
	if (third >= 0) t.add (third);
	t.add (fifth);
	if (sev >= 0) t.add (sev);
	if (c.ext & EXT_B9) t.add (13); else if (c.ext & EXT_S9) t.add (15); else if (c.ext & EXT_9) t.add (14);
	if (c.ext & EXT_S11) t.add (18); else if (c.ext & EXT_11) t.add (17);
	if (c.ext & EXT_B13) t.add (20); else if (c.ext & EXT_13) t.add (21);
	t.root = ((root + r) % 12 + 12) % 12;
	for (int i = 0; i < t.n; i++) t.pcs |= 1u << ((t.root + t.iv[i]) % 12);
	int bi = c.inv == 1 ? third : c.inv == 2 ? fifth : c.inv == 3 ? sev : 0;
	t.bass = (t.root + (bi < 0 ? 0 : bi)) % 12;
	unsigned scale = harm_abs (mask, root);
	for (int k = 0; k < 12; k++)
		if ((t.pcs >> k & 1) && !(scale >> k & 1)) t.clash |= (1u << ((k + 1) % 12)) | (1u << ((k + 11) % 12));
	t.clash &= ~t.pcs;
	return t;
}

// le chiffrage : « maj7 », « m7 », « ø7 », « 7(♭9) », « sus4 », « add9 »... ; minor : le « m » des accords mineurs
static void chord_suffix (const Chord &c, const ChordTones &t, bool minor, char *o, int cap)
{
	int w = 0;
	#define PUT(s) (w += snprintf (o + w, (size_t) (cap - w > 0 ? cap - w : 0), "%s", s))
	o[0] = 0;
	bool dim = t.third == 3 && t.fifth == 6, aug = t.third == 4 && t.fifth == 8;
	if (t.third < 0 && c.sus == 0) PUT ("5");
	else if (dim && t.seventh == 10) PUT ("ø");
	else if (dim) PUT ("°");
	else if (aug) PUT ("+");
	else if (t.third == 3 && minor) PUT ("m");
	switch (t.seventh)
	{
	case 10: PUT ("7"); break;
	case 11: PUT ("maj7"); break;
	case 9: PUT (c.sev == 5 || dim ? "7" : "6"); break;
	}
	if (c.sus == 1) PUT ("sus2"); else if (c.sus == 2) PUT ("sus4");
	int ext[9], ne = 0;
	for (int i = 0; i < 7; i++) if (c.ext & (1 << i)) ext[ne++] = i;
	if (!dim && !aug) { if (c.ext & EXT_B5) ext[ne++] = 7; if (c.ext & EXT_S5) ext[ne++] = 8; }
	if (ne)
	{
		if (t.seventh < 0 && ne == 1 && ext[0] < 3) { PUT ("add"); PUT (EXT_NAMES[ext[0]]); }	// (9, 11, 13)
		else
		{
			PUT ("(");
			for (int i = 0; i < ne; i++) { if (i) PUT (","); PUT (EXT_NAMES[ext[i]]); }
			PUT (")");
		}
	}
	#undef PUT
}
// en degres : « ♭VII », « ii7 », « V7/3 »
static void chord_roman (const Chord &c, unsigned mask, int root, char *o, int cap)
{
	static const char *const ROMAN[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
	static const char *const ROMAN_LO[7] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };
	o[0] = 0;
	if (c.deg == CHORD_NC) { snprintf (o, (size_t) cap, "N.C."); return; }
	if (c.deg < 1) return;
	ChordTones t = chord_tones (c, mask, root);
	char suf[48]; chord_suffix (c, t, false, suf, sizeof suf);
	int w = snprintf (o, (size_t) cap, "%s%s%s", c.acc < 0 ? "♭" : c.acc > 0 ? "♯" : "", t.third == 3 ? ROMAN_LO[c.deg - 1] : ROMAN[c.deg - 1], suf);
	if (c.inv > 0 && t.bass != t.root && w < cap) w += snprintf (o + w, (size_t) (cap - w), "/%s", c.inv == 1 ? "3" : c.inv == 2 ? "5" : "7");
	if (c.inv == CHORD_LEAD && w < cap) snprintf (o + w, (size_t) (cap - w), "↝");	// (voice leading)
}
// en notes : « Fa maj7 », « Ré m7/La »
static void chord_notes (const Chord &c, unsigned mask, int root, char *o, int cap)
{
	o[0] = 0;
	if (c.deg == CHORD_NC) { snprintf (o, (size_t) cap, "sans accord"); return; }
	if (c.deg < 1) return;
	ChordTones t = chord_tones (c, mask, root);
	char suf[48]; chord_suffix (c, t, true, suf, sizeof suf);
	int w = snprintf (o, (size_t) cap, "%s%s%s", NOTE_NAMES[t.root], suf[0] ? " " : "", suf);
	if (t.bass != t.root && w < cap) snprintf (o + w, (size_t) (cap - w), "/%s", NOTE_NAMES[t.bass]);
}

enum { CHORDS_MAX = 512 };
struct ChordGrid
{
	Chord c[CHORDS_MAX];
	int length;				// en mesures
	bool loop;
	ChordGrid () : length (0), loop (true) { memset (c, 0, sizeof c); }
	void clear () { memset (c, 0, sizeof c); length = 0; }
	void set (int bar, const Chord &x)
	{
		if (bar < 0 || bar >= CHORDS_MAX) return;
		c[bar] = x;
		if (!x.empty () && bar >= length) length = bar + 1;
	}
	// la mesure de la grille qui joue a cette mesure du morceau (la boucle comprise) ; -1 : hors de la grille
	int slot (int bar) const
	{
		if (length <= 0 || bar < 0) return -1;
		if (bar >= length) { if (!loop) return -1; bar %= length; }
		return bar;
	}
	// l'accord de cette mesure du morceau, et la case de la grille ou il est ecrit ; false : pas d'accord
	bool find (int bar, Chord &out, int &at) const
	{
		at = -1;
		for (int k = slot (bar); k >= 0; k--)
			if (!c[k].empty ())
			{
				if (c[k].deg == CHORD_NC) return false;
				out = c[k]; at = k;
				return true;
			}
		return false;
	}
	// une ligne par accord ecrit : « mesure degre alteration qualite sus septieme ajouts renversement open »
	template <class F> void format (F put) const
	{
		char b[96];
		snprintf (b, sizeof b, "chords=%d %d\r\n", length, loop ? 1 : 0); put (b);
		for (int i = 0; i < length && i < CHORDS_MAX; i++)
		{
			const Chord &x = c[i];
			if (x.empty ()) continue;
			snprintf (b, sizeof b, "chord=%d %d %d %d %d %d %d %d %d\r\n", i + 1, x.deg, x.acc, x.qual, x.sus, x.sev, x.ext, x.inv, x.open ? 1 : 0);
			put (b);
		}
	}
	// une ligne de format (la valeur, sans « chord= »)
	void parse_chord (const char *v)
	{
		int n[9] = { 0 };
		if (sscanf (v, "%d %d %d %d %d %d %d %d %d", &n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &n[6], &n[7], &n[8]) < 8) return;
		if (n[0] < 1 || n[0] > CHORDS_MAX || n[1] < 0 || n[1] > CHORD_NC) return;
		auto cl = [] (int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; };
		Chord &x = c[n[0] - 1];
		x.deg = n[1]; x.acc = cl (n[2], -1, 1); x.qual = cl (n[3], 0, 5); x.sus = cl (n[4], 0, 2);
		x.sev = cl (n[5], 0, 5); x.ext = n[6] & 511; x.inv = cl (n[7], 0, CHORD_LEAD); x.open = n[8] != 0;
	}
	void parse_header (const char *v)
	{
		int l, lp;
		int k = sscanf (v, "%d %d", &l, &lp);
		if (k >= 1) length = l < 0 ? 0 : l > CHORDS_MAX ? CHORDS_MAX : l;
		if (k >= 2) loop = lp == 1;
	}
};

#endif
