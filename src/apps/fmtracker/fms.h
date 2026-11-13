//
// fms.h -- le modèle du morceau de FM Tracker et ses formats de fichiers. Repris de fms.h d'Onyx
// (user/Apps/fmtracker) : mêmes structures, mêmes formats, new / delete seulement ; ce qui allait au
// noyau d'Onyx (struct kapi_fm_instrument, fms_to_kapi) passe par AudioKit (ak_fm_from_fms, qui lit
// les 26 octets eux-mêmes, comme fmt_edit.inc de l'asm de Korin). Les règles de lecture suivent
// fmt_song.inc là où l'asm de Korin diffère (premier motif vérifié avant de rien changer, nom d'un
// .FMI après les espaces et les virgules).
//
// .FMS (« fm-song-project », FM Song pour QBasic / AdLib, 2001) : binaire, petit boutiste --
//   "fm-song-project" (15) | titre (20) | auteur (20) | commentaire (50) | motifs (s16)
//   8 x instrument : nom (8) + 26 octets de paramètres (plus bas)
//   par motif : lignes (s16) | vitesse (s16, une ligne dure vitesse / 20 s) | 8 drapeaux muet (1 = muet)
//               8 canaux x lignes octets de notes
// Un octet de note : 0 = rien de neuf (la note continue, « --- »), 128 = silence (rien d'affiché),
// sinon dièse * 64 + octave * 8 + note (1..7 = do ré mi fa sol la si).
//
// Les 26 octets d'un instrument sont des paires (opérateur 1 = modulateur, 2 = porteuse, comme les
// registres de l'OPL2 de l'AdLib) : attaque, chute, maintien, relâche, volume (atténuation), suivi du
// clavier, « vibrato d'amplitude » (bit VIB de l'OPL), « vibrato de hauteur » (bit AM), multiplicateur,
// KSR, onde ; puis rétroaction, connexion ; puis tenu (type d'enveloppe).
// .FMI (un instrument, texte) : "fm-song instrument", "NOM", puis les mêmes paires en lignes "a,b",
// mais tenu AVANT "rétroaction,connexion".
//
#ifndef _fms_h
#define _fms_h

#include <audiokit_abi.h>		// ak_note_octave_mhz

#define FMS_CH		8
#define FMS_MAXPAT	64
#define FMS_MAXROWS	256
#define FMS_CONT	0		// octet de note : rien de neuf
#define FMS_OFF		128		// octet de note : silence

enum { P_AR = 0, P_DR = 2, P_SL = 4, P_RR = 6, P_TL = 8, P_KSL = 10, P_VIB = 12, P_AM = 14,
       P_MULT = 16, P_KSR = 18, P_WAVE = 20, P_FB = 22, P_CON = 23, P_EGT = 24 };

struct FmsIns { char name[9]; unsigned char p[26]; };
struct FmsPattern { int rows, speed; unsigned char mute[FMS_CH]; unsigned char *n; };	// n[canal * rows + ligne]
struct FmsSong
{
	char title[21], author[21], comment[51];
	int npat;
	FmsPattern pat[FMS_MAXPAT];
	FmsIns ins[FMS_CH];
};

static inline void fms_copy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline int  fms_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }

// Un instrument par défaut convenable (un piano électrique doux).
static inline void fms_default_ins (FmsIns *in)
{
	static const unsigned char p[26] = { 15, 15, 1, 2, 5, 7, 3, 4, 20, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 3, 0, 1, 0 };
	fms_copy (in->name, "EPIANO", sizeof in->name);
	for (int i = 0; i < 26; i++) in->p[i] = p[i];
}

// Un motif vide (pt->n est remplacé sans être libéré : à l'appelant).
static inline bool fms_pattern_alloc (FmsPattern *pt, int rows, int speed)
{
	if (rows < 1) rows = 1;
	if (rows > FMS_MAXROWS) rows = FMS_MAXROWS;
	pt->rows = rows; pt->speed = speed < 1 ? 1 : speed;
	for (int c = 0; c < FMS_CH; c++) pt->mute[c] = 0;
	pt->n = new unsigned char[FMS_CH * rows];
	for (int i = 0; i < FMS_CH * rows; i++) pt->n[i] = FMS_CONT;
	return pt->n != 0;
}
// Change le nombre de lignes, garde les notes.
static inline void fms_pattern_resize (FmsPattern *pt, int rows)
{
	if (rows < 1) rows = 1;
	if (rows > FMS_MAXROWS) rows = FMS_MAXROWS;
	if (rows == pt->rows) return;
	unsigned char *n = new unsigned char[FMS_CH * rows];
	for (int c = 0; c < FMS_CH; c++)
		for (int r = 0; r < rows; r++) n[c * rows + r] = r < pt->rows ? pt->n[c * pt->rows + r] : FMS_CONT;
	delete [] pt->n; pt->n = n; pt->rows = rows;
}

static inline void fms_clear (FmsSong *s)
{
	for (int i = 0; i < s->npat; i++) { delete [] s->pat[i].n; s->pat[i].n = 0; }
	s->npat = 0;
}
static inline void fms_new (FmsSong *s)
{
	fms_copy (s->title, "Untitled", sizeof s->title); s->author[0] = 0; s->comment[0] = 0;
	s->npat = 1;
	fms_pattern_alloc (&s->pat[0], 64, 3);
	for (int c = 0; c < FMS_CH; c++) fms_default_ins (&s->ins[c]);
}

// ---- .FMS ----------------------------------------------------------------------------------------
static inline void fms_field (char *dst, int cap, const unsigned char *src, int n)	// complété -> sans les espaces de fin
{
	int k = 0;
	for (int i = 0; i < n && k < cap - 1; i++) dst[k++] = src[i] >= 32 ? (char) src[i] : ' ';
	while (k > 0 && dst[k - 1] == ' ') k--;
	dst[k] = 0;
}
static inline int fms_s16 (const unsigned char *p) { return (short) (p[0] | (p[1] << 8)); }

// Lit un .FMS entier ; false si ce n'en est pas un. (s doit être vide : fms_clear d'abord.) Comme
// fms_parse de fmt_song.inc : le premier motif est vérifié avant d'écrire quoi que ce soit.
static inline bool fms_parse (const unsigned char *d, int n, FmsSong *s)
{
	const char *magic = "fm-song-project";
	if (n < 107 + FMS_CH * 34) return false;
	for (int i = 0; i < 15; i++) if (d[i] != (unsigned char) magic[i]) return false;
	int npat = fms_s16 (d + 105);
	if (npat < 1 || npat > FMS_MAXPAT) return false;
	int p = 107 + FMS_CH * 34;
	if (p + 12 > n) return false;
	int rows0 = fms_s16 (d + p);
	if (rows0 < 1 || rows0 > FMS_MAXROWS) return false;
	p = 15;
	fms_field (s->title, sizeof s->title, d + p, 20); p += 20;
	fms_field (s->author, sizeof s->author, d + p, 20); p += 20;
	fms_field (s->comment, sizeof s->comment, d + p, 50); p += 50;
	p += 2;
	for (int c = 0; c < FMS_CH; c++)
	{
		fms_field (s->ins[c].name, sizeof s->ins[c].name, d + p, 8); p += 8;
		for (int i = 0; i < 26; i++) s->ins[c].p[i] = d[p + i];
		p += 26;
	}
	s->npat = 0;
	for (int t = 0; t < npat; t++)
	{
		if (p + 12 > n) break;
		int rows = fms_s16 (d + p), speed = fms_s16 (d + p + 2);
		if (rows < 1 || rows > FMS_MAXROWS) break;
		p += 4;
		FmsPattern &pt = s->pat[s->npat];
		fms_pattern_alloc (&pt, rows, speed);
		for (int c = 0; c < FMS_CH; c++) pt.mute[c] = d[p + c] ? 1 : 0;
		p += 8;
		for (int c = 0; c < FMS_CH; c++)
			for (int r = 0; r < rows; r++) pt.n[c * rows + r] = p < n ? d[p++] : FMS_CONT;
		s->npat++;
	}
	return true;
}

// Écrit le morceau ; rend la taille (o doit tenir fms_size (s) octets).
static inline int fms_size (const FmsSong *s)
{
	int n = 107 + FMS_CH * 34;
	for (int t = 0; t < s->npat; t++) n += 12 + FMS_CH * s->pat[t].rows;
	return n;
}
static inline int fms_write (const FmsSong *s, unsigned char *o)
{
	int p = 0;
	const char *magic = "fm-song-project";
	for (int i = 0; i < 15; i++) o[p++] = (unsigned char) magic[i];
	auto pad = [&] (const char *str, int w) { int l = fms_len (str); for (int i = 0; i < w; i++) o[p++] = i < l ? (unsigned char) str[i] : ' '; };
	auto s16 = [&] (int v) { o[p++] = (unsigned char) (v & 255); o[p++] = (unsigned char) ((v >> 8) & 255); };
	pad (s->title, 20); pad (s->author, 20); pad (s->comment, 50);
	s16 (s->npat);
	for (int c = 0; c < FMS_CH; c++) { pad (s->ins[c].name, 8); for (int i = 0; i < 26; i++) o[p++] = s->ins[c].p[i]; }
	for (int t = 0; t < s->npat; t++)
	{
		const FmsPattern &pt = s->pat[t];
		s16 (pt.rows); s16 (pt.speed);
		for (int c = 0; c < FMS_CH; c++) o[p++] = pt.mute[c];
		for (int c = 0; c < FMS_CH; c++) for (int r = 0; r < pt.rows; r++) o[p++] = pt.n[c * pt.rows + r];
	}
	return p;
}

// ---- .FMI ----------------------------------------------------------------------------------------
// Ordre des lignes d'un .FMI (paire dans p[]) : AR DR SL RR TL KSL VIB AM MULT KSR WAVE EGT (FB,CON)
static const int FMI_ORDER[12] = { P_AR, P_DR, P_SL, P_RR, P_TL, P_KSL, P_VIB, P_AM, P_MULT, P_KSR, P_WAVE, P_EGT };

// (fmi_skip de l'asm : les espaces et les virgules)
static inline const char *fmi_skip (const char *s) { while (*s == ' ' || *s == ',') s++; return s; }

// Comme fmi_parse de fmt_song.inc : l'instrument est écrit au fil de la lecture (le nom à la
// deuxième ligne, chaque paire à la sienne) ; false s'il manque des paires.
static inline bool fmi_parse (const char *t, FmsIns *in)
{
	int line = 0, pairs = 0;
	const char *p = t;
	while (*p && pairs < 13)
	{
		const char *e = p; while (*e && *e != '\n') e++;
		char l[96]; int k = 0;
		for (const char *q = p; q < e; q++) if (*q != '\r' && *q != '"' && k < 95) l[k++] = *q;
		l[k] = 0;
		if (line == 0) { const char *m = "fm-song instrument"; for (int i = 0; m[i]; i++) if (l[i] != m[i]) return false; }
		else if (line == 1)
		{
			fms_copy (in->name, fmi_skip (l), sizeof in->name);
			int n = fms_len (in->name); while (n > 0 && in->name[n - 1] == ' ') in->name[--n] = 0;
		}
		else if (k)
		{
			int a = 0, b = 0;
			const char *q = fmi_skip (l);
			while (*q >= '0' && *q <= '9') a = a * 10 + (*q++ - '0');
			q = fmi_skip (q);
			while (*q >= '0' && *q <= '9') b = b * 10 + (*q++ - '0');
			int f = pairs < 12 ? FMI_ORDER[pairs] : P_FB;
			in->p[f] = (unsigned char) a; in->p[f + 1] = (unsigned char) b;
			pairs++;
		}
		line++;
		p = *e ? e + 1 : e;
	}
	return pairs >= 13;
}
static inline int fmi_write (const FmsIns *in, char *o)
{
	int p = 0;
	auto str = [&] (const char *s) { for (int i = 0; s[i]; i++) o[p++] = s[i]; };
	auto num = [&] (int v) { char b[8]; int k = 0; if (!v) b[k++] = '0'; while (v) { b[k++] = (char) ('0' + v % 10); v /= 10; } while (k) o[p++] = b[--k]; };
	str ("\"fm-song instrument\"\r\n\""); str (in->name); str ("\"\r\n");
	for (int i = 0; i < 12; i++) { num (in->p[FMI_ORDER[i]]); o[p++] = ','; num (in->p[FMI_ORDER[i] + 1]); str ("\r\n"); }
	num (in->p[P_FB]); o[p++] = ','; num (in->p[P_CON]); str ("\r\n");
	o[p] = 0;
	return p;
}

// ---- notes -------------------------------------------------------------------------------------------
// La hauteur d'un octet de note, en milli-Hz : accord standard (la4 = 440 Hz, do4 = 261,63 Hz,
// tempérament égal), par la table de notes d'AudioKit. (La table de F-numbers de FM Song jouait tout
// un demi-ton plus haut ; ses numéros d'octave sont gardés.)
static inline unsigned fms_note_mhz (unsigned char v)
{
	static const int semi[8] = { 0, 0, 2, 4, 5, 7, 9, 11 };	// (1..7 = do..si)
	int note = v & 7, oct = (v >> 3) & 7, sharp = (v >> 6) & 1;
	if (note == 0 || (v & 128)) return 0;
	return (unsigned) ak_note_octave_mhz (semi[note] + sharp, oct);
}
static inline unsigned char fms_make_note (int note /*1..7*/, int sharp, int oct)
{
	if (sharp && (note == 3 || note == 7))			// mi# = fa, si# = le do suivant
	{
		sharp = 0;
		if (note == 3) note = 4; else { note = 1; if (oct < 7) oct++; }
	}
	return (unsigned char) ((sharp ? 64 : 0) + (oct & 7) * 8 + (note & 7));
}
// Une note déplacée de delta demi-tons (octaves 0..7) ; le reste est rendu tel quel.
static inline unsigned char fms_transpose (unsigned char v, int delta)
{
	static const int semi[8] = { 0, 0, 2, 4, 5, 7, 9, 11 };
	static const unsigned char note[12] = { 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7 }, sharp[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
	if ((v & 128) || (v & 7) == 0) return v;
	int p = ((v >> 3) & 7) * 12 + semi[v & 7] + ((v >> 6) & 1) + delta;
	if (p < 0) p = 0;
	if (p > 8 * 12 - 1) p = 8 * 12 - 1;
	return (unsigned char) (sharp[p % 12] * 64 + (p / 12) * 8 + note[p % 12]);
}

// "C#4" / "D-3" / "---" / "" (silence)
static inline void fms_note_text (unsigned char v, char *o)
{
	if (v & 128) { o[0] = 0; return; }			// silence
	if ((v & 7) == 0) { o[0] = o[1] = o[2] = '-'; o[3] = 0; return; }	// continue
	o[0] = "?CDEFGAB"[v & 7]; o[1] = (v & 64) ? '#' : '-'; o[2] = (char) ('0' + ((v >> 3) & 7)); o[3] = 0;
}

#endif
