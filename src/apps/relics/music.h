/*
 * music.h -- la musique de Relics : une piste par monde et une pour le titre, tendues et pressantes (mineur
 * harmonique, hijaz, phrygien ; ostinato en doubles croches, basse qui pousse, batterie, un theme au cuivre ou au
 * hautbois), jouees sur le synthetiseur General MIDI d'AudioKit (la SoundFont du systeme, sinon sa banque FM).
 *
 * Un thread a lui fait tout : il charge le synthe (la fenetre n'attend pas la SoundFont), compte les pas au point
 * pres, rend et ecrit le flux. Le jeu ne fait que dire ce qu'il veut entendre (mu_play, mu_hold) ; les bruitages
 * restent aux voix FM de gamesound.h. Rien n'est lu : les pistes sont ecrites ici.
 *
 * Une piste : 2 mesures d'entree (basse, ostinato, la batterie qui monte), puis 16 mesures en boucle, A (le theme)
 * et B (le contre-chant), avec un roulement a la fin de chaque partie. Les notes sont des degres de la gamme de la
 * piste : 'h' la tonique, 'a' .. 'g' en dessous, 'i' .. 'u' au-dessus ; '-' tient, '.' se tait (une lettre : une
 * croche pour le theme, une double croche pour la basse, l'ostinato et la batterie).
 */
#ifndef RELICS_MUSIC_H
#define RELICS_MUSIC_H

#include <pthread.h>
#include <unistd.h>

#define MU_RATE		44100
#define MU_BLOCK	512

enum { MC_BASS, MC_OST, MC_LEAD, MC_PAD, MC_DRUM = 9 };

struct mtrack {
	int bpm;
	int key;				/* la tonique de la basse + 24 (touche MIDI) */
	signed char sc[7];			/* la gamme, en demi-tons */
	unsigned char prog[4];			/* basse, ostinato, theme, nappe (General MIDI) */
	unsigned char vol[4];			/* (controleur 7) */
	signed char oct[4];			/* l'octave de chaque partie, depuis key */
	unsigned char dk[4];			/* la batterie : grosse caisse, caisse claire, charleston, percussion */
	const char *chords;			/* 16 degrees, un par mesure */
	const char *bass;			/* 16 pas : R r (la fondamentale), o (l'octave), 5 (la quinte), - . */
	const char *ost[2];			/* 16 pas, A et B : 1 3 5 (l'accord), 8 (l'octave), . */
	const char *drum[4];			/* 16 pas : X (fort), x, . */
	const char *lead;			/* 128 croches (16 mesures) */
};

/* les gammes */
#define SC_HMIN		{ 0, 2, 3, 5, 7, 8, 11 }	/* mineur harmonique */
#define SC_HIJAZ	{ 0, 1, 4, 5, 7, 8, 10 }	/* phrygien dominant */
#define SC_PHRYG	{ 0, 1, 3, 5, 7, 8, 10 }	/* phrygien */

static const struct mtrack mu_tracks[] = {
	/* 0 le titre : la mineur, cordes et cuivres */
	{ 138, 57, SC_HMIN, { 33, 48, 61, 49 }, { 100, 74, 92, 52 }, { -2, 0, 1, 0 }, { 36, 38, 42, 47 },
	  "0055334455330044",
	  "R.rrR.rrR.rrR.or",
	  { "8531853185318535", "1358535813585358" },
	  { "X...x...X...x.x.", "....X.......X...", "x.x.x.x.x.x.x.x.", "................" },
	  "l--hj-l-" "o---n-l-" "m--hj-m-" "q---o-m-" "k--hk-m-" "o---m-k-" "l-n-p-n-" "p-o-n-l-"
	  "j-j-jhj-" "m-----j-" "k-k-kmk-" "o-----m-" "l-l-ljl-" "o-----l-" "n-n-p-n-" "p---n---" },
	/* 1 la jungle : re mineur, marimba, toms et congas */
	{ 152, 50, SC_HMIN, { 32, 12, 56, 48 }, { 104, 86, 88, 40 }, { -2, 1, 1, 0 }, { 36, 47, 70, 63 },
	  "0054003433005544",
	  "R..r..r.R..r.ror",
	  { "1.51.58.1.53.85.", "1515858513138585" },
	  { "X..x..x.X..x..x.", "....X.......X...", "xxxxxxxxxxxxxxxx", "..x..x.x..x..xx." },
	  "l.l.l.jk" "l-o-l---" "m.m.m.lk" "p-n-l-i-" "o.o.o.qp" "o-l-j---" "k-m-o-r-" "p---n---"
	  "r---o-m-" "k-----m-" "q---o-l-" "o-----l-" "q---m-j-" "o---m---" "n-p-s-p-" "n-------" },
	/* 2 le desert : hijaz en mi, pizzicati et hautbois, tambourin et darbouka */
	{ 144, 52, SC_HIJAZ, { 33, 45, 68, 48 }, { 100, 92, 90, 44 }, { -2, 0, 1, 0 }, { 36, 64, 54, 63 },
	  "0011006133661100",
	  "R.rR..r.R.rR..ro",
	  { "1.3.5.31.3.5.83.", "1358531313585313" },
	  { "X..x..x.X.....x.", "....X..x....X...", "..x...x...x...x.", "x.x.x.xxx.x.x.xx" },
	  "h-i-j---" "l-k-j-h-" "i-k-m---" "m-n-m-k-" "o---q-o-" "o-n-o-l-" "k---n-k-" "m-k-i-k-"
	  "r---o-m-" "o-----k-" "p---n-k-" "n-----p-" "p---m-k-" "m-----i-" "j-k-j-i-" "h-------" },
	/* 3 la pyramide : hijaz en re, cordes, trombone et choeur */
	{ 136, 50, SC_HIJAZ, { 33, 48, 57, 52 }, { 104, 80, 96, 50 }, { -2, 0, 0, 1 }, { 36, 47, 42, 41 },
	  "0101331066330011",
	  "R.rrR.rrR.rrR.rr",
	  { "1315131513151315", "8585858513131313" },
	  { "X.......X.....x.", "....X.......X...", "xxxxxxxxxxxxxxxx", "..............xx" },
	  "h---j-l-" "m---k-i-" "l---j-h-" "i-k-m-p-" "o---m-k-" "m-l-k-j-" "k---i---" "j-i-h---"
	  "n---p-k-" "n-o-p-n-" "o---m-k-" "m-----k-" "q---o-l-" "o-----q-" "p---m-k-" "i-k-m-n-" },
	/* 4 la glace : si mineur, celesta, cordes et verre frotte */
	{ 156, 47, SC_HMIN, { 38, 8, 48, 92 }, { 100, 90, 84, 50 }, { -2, 2, 2, 1 }, { 36, 38, 42, 81 },
	  "0055334434053644",
	  "R.rrR.rrR.rrR.oo",
	  { "8531853185318531", "1358135813581358" },
	  { "X...X...X...X..x", "....X.......X...", "x.x.x.x.x.x.x.x.", "x..............." },
	  "l--l--k-" "j--h----" "j--j--h-" "f--h--j-" "k--k--j-" "d--f--h-" "i--g--e-" "i--j--k-"
	  "o---m---" "n---l---" "o--l--j-" "m--j--o-" "o--m--k-" "n--k--i-" "l--i--g-" "i-------" },
	/* 5 le volcan : mi phrygien, guitares saturees, double grosse caisse, choeur */
	{ 170, 40, SC_PHRYG, { 34, 30, 29, 52 }, { 104, 78, 86, 54 }, { -1, 0, 2, 2 }, { 36, 38, 51, 0 },
	  "0011002133661121",
	  "R.rrR.rrR.rrRror",
	  { "1.11.11.1.11.15.", "1111111111115555" },
	  { "X.xxX.xxX.xxX.xx", "....X.......X...", "x.x.x.x.x.x.x.x.", "................" },
	  "h-i-j-l-" "l---j-h-" "i-i-k-m-" "m---k-i-" "o-o-q-o-" "q-p-o---" "n---l-j-" "k---i---"
	  "r---o-m-" "o-p-o-m-" "p---n-k-" "n-m-k---" "p---m-k-" "m-n-p---" "q---n-l-" "p-o-m-k-" },
};
enum { MU_TITLE = 0, MU_NTRACK = sizeof mu_tracks / sizeof mu_tracks[0] };

/* ce que le jeu veut (ecrit par lui, lu par le thread) */
static volatile int mu_want = -1;		/* la piste, -1 : rien */
static volatile int mu_bpm_add;			/* (plus vite au fil des niveaux d'un monde) */
static volatile int mu_held;			/* 1 : la musique attend (la pause, le son coupe) */
static volatile int mu_quit;
static pthread_t mu_tid;
static int mu_thread;

/* l'etat du thread */
static long mu_syn, mu_out;
static int mu_cur = -1;				/* la piste qui joue */
static long mu_step;				/* le pas (double croche) depuis le debut, -32 : l'entree */
static long mu_left;				/* les trames jusqu'au pas suivant */
static float mu_gain, mu_gain_to;		/* le fondu */
static unsigned char mu_on[16][8];		/* les touches tenues, par canal */
static unsigned char mu_non[16];

static void mu_midi (int ch, int cmd, int a, int b) { ak_synth_midi (mu_syn, ch, cmd, a, b); }

static void mu_note (int ch, int k, int v)
{
	if (k < 0 || k > 127) return;
	mu_midi (ch, 0x90, k, v);
	if (mu_non[ch] < 8) mu_on[ch][mu_non[ch]++] = (unsigned char) k;
}

static void mu_off (int ch)
{
	for (int i = 0; i < mu_non[ch]; i++) mu_midi (ch, 0x80, mu_on[ch][i], 0);
	mu_non[ch] = 0;
}

static void mu_all_off (void) { for (int c = 0; c < 16; c++) mu_off (c); }

/* le degre d (0 : la tonique) de la gamme de t, depuis la touche base */
static int mu_deg (const struct mtrack *t, int d, int base)
{
	int o = d >= 0 ? d / 7 : (d - 6) / 7;
	return base + 12 * o + t->sc[d - o * 7];
}

static void mu_setup (int n)
{
	const struct mtrack *t = &mu_tracks[n];
	static const unsigned char pan[4] = { 64, 44, 80, 64 }, rev[4] = { 10, 30, 44, 70 };
	ak_synth_reset (mu_syn);
	for (int c = 0; c < 16; c++) mu_non[c] = 0;
	for (int c = 0; c < 4; c++)
	{
		mu_midi (c, 0xC0, t->prog[c], 0);
		mu_midi (c, 0xB0, 7, t->vol[c]);
		mu_midi (c, 0xB0, 10, pan[c]);
		mu_midi (c, 0xB0, 91, rev[c]);
	}
	mu_midi (MC_DRUM, 0xB0, 7, 100);
	mu_midi (MC_DRUM, 0xB0, 91, 24);
	mu_cur = n;
	mu_step = -32;
	mu_left = 0;
}

/* un pas : une double croche */
static void mu_tick (void)
{
	const struct mtrack *t = &mu_tracks[mu_cur];
	long st = mu_step++;
	int intro = st < 0;
	long s = intro ? st + 32 : st;
	int bar = intro ? 0 : (int) (s / 16 % 16), i = (int) (s % 16), sec = bar >= 8;
	int last = intro ? s >= 16 : (bar == 7 || bar == 15);	/* la mesure du roulement */
	int deg = intro ? 0 : t->chords[bar] - '0';
	int b0 = t->key - 24 + 12 * t->oct[MC_BASS];

	/* la batterie */
	mu_off (MC_DRUM);
	if (i == 0 && !intro && (bar == 0 || bar == 8)) mu_note (MC_DRUM, 49, 110);
	if (last && i >= 8)
	{
		static const unsigned char roll[8] = { 38, 38, 50, 50, 47, 45, 43, 41 };
		mu_note (MC_DRUM, intro && s < 16 ? 38 : roll[i - 8], 70 + (i - 8) * 7);
		if (t->drum[0][i] != '.') mu_note (MC_DRUM, t->dk[0], 112);
	}
	else
		for (int d = 0; d < 4; d++)
		{
			char c = t->drum[d][i];
			if (c == '.' || !t->dk[d]) continue;
			if (intro && s < 16 && d == 1) continue;		/* (l'entree : sans caisse claire au debut) */
			mu_note (MC_DRUM, t->dk[d], c == 'X' ? 118 : d == 2 ? 64 + (i % 4 == 0) * 16 : 92);
		}

	/* la basse */
	char bc = t->bass[i];
	if (bc != '-')
	{
		mu_off (MC_BASS);
		int k = mu_deg (t, deg, b0);
		if (bc == 'R') mu_note (MC_BASS, k, 116);
		else if (bc == 'r') mu_note (MC_BASS, k, 92);
		else if (bc == 'o') mu_note (MC_BASS, k + 12, 100);
		else if (bc == '5') mu_note (MC_BASS, mu_deg (t, deg + 4, b0), 96);
	}

	/* l'ostinato */
	mu_off (MC_OST);
	char oc = t->ost[sec][i];
	if (oc != '.')
	{
		int base = t->key + 12 * t->oct[MC_OST];
		int k = oc == '1' ? mu_deg (t, deg, base) : oc == '3' ? mu_deg (t, deg + 2, base) : oc == '5' ? mu_deg (t, deg + 4, base) : mu_deg (t, deg + 7, base);
		mu_note (MC_OST, k, i % 4 == 0 ? 104 : 78);
	}

	/* la nappe : l'accord de la mesure, tenu */
	if (i == 0 && !intro)
	{
		mu_off (MC_PAD);
		int base = t->key + 12 * t->oct[MC_PAD];
		for (int k = 0; k < 3; k++) mu_note (MC_PAD, mu_deg (t, deg + 2 * k, base), 58);
	}

	/* le theme, a la croche */
	if (!intro && !(i & 1))
	{
		char c = t->lead[bar * 8 + i / 2];
		if (c != '-')
		{
			mu_off (MC_LEAD);
			if (c >= 'a' && c <= 'u') mu_note (MC_LEAD, mu_deg (t, c - 'h', t->key + 12 * t->oct[MC_LEAD]), i % 8 == 0 ? 110 : 96);
		}
	}
}

static void *mu_main (void *arg)
{
	(void) arg;
	long s = ak_synth_new (0, 0);
	if (s <= 0) return 0;
	long out = ak_open (MU_RATE, 2, 16, 0);
	if (out < 0) { ak_synth_free (s); return 0; }
	mu_syn = s;
	mu_out = out;
	static short buf[MU_BLOCK * 2];
	while (!mu_quit)
	{
		int want = mu_held ? mu_cur : mu_want;
		if (want != mu_cur || mu_held) mu_gain_to = 0;
		else mu_gain_to = 1;
		/* le fondu fini : on change de piste */
		if (mu_gain <= 0.001f && want != mu_cur)
		{
			mu_all_off ();
			if (want >= 0) mu_setup (want); else mu_cur = -1;
		}
		int run = mu_cur >= 0 && !(mu_held && mu_gain <= 0.001f);
		long done = 0;
		while (done < MU_BLOCK)
		{
			if (run && mu_left <= 0)
			{
				mu_tick ();
				int bpm = mu_tracks[mu_cur].bpm + mu_bpm_add;
				mu_left = (long) MU_RATE * 60 / (bpm * 4);
			}
			long n = MU_BLOCK - done;
			if (run && n > mu_left) n = mu_left;
			ak_synth_render (s, (long) (buf + 2 * done), n);
			if (run) mu_left -= n;
			done += n;
		}
		/* le gain, en rampe sur le bloc : 0,15 s pour monter ou descendre */
		float g = mu_gain, step = 1.0f / (MU_RATE * 0.15f);
		for (int f = 0; f < MU_BLOCK; f++)
		{
			if (g < mu_gain_to) { g += step; if (g > mu_gain_to) g = mu_gain_to; }
			else if (g > mu_gain_to) { g -= step; if (g < mu_gain_to) g = mu_gain_to; }
			float k = g * 0.7f;
			buf[2 * f] = (short) (buf[2 * f] * k);
			buf[2 * f + 1] = (short) (buf[2 * f + 1] * k);
		}
		if (mu_gain > 0 && g <= 0.001f && mu_held) mu_all_off ();
		mu_gain = g;
		ak_write_all (out, (long) buf, sizeof buf);
	}
	ak_close (out);
	ak_synth_free (s);
	return 0;
}

static inline void mu_open (void) { mu_thread = pthread_create (&mu_tid, 0, mu_main, 0) == 0; }
static inline void mu_close (void) { mu_quit = 1; if (mu_thread) pthread_join (mu_tid, 0); }

/* la piste voulue (-1 : rien), un peu plus vite de bpm */
static inline void mu_play (int n, int bpm) { mu_want = n < MU_NTRACK ? n : -1; mu_bpm_add = bpm; }
static inline void mu_hold (int on) { mu_held = on; }

#endif
