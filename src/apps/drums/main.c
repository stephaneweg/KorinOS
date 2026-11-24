/*
 * main.c -- Boîte à rythmes (Korin OS, UIKit, AudioKit)
 *
 * Un séquenceur pas à pas de percussions : des pistes prises parmi les 61 percussions du General MIDI
 * et du GS (notes 27 à 87 : grosses caisses, caisses, charlestons, toms, cymbales, peaux latines,
 * bois et métaux, effets), signature (3/4, 4/4, 5/4, 6/8, 12/8), 1 à 8 mesures, accents, swing,
 * muet et solo, niveau par piste, motifs tout faits, enregistrement (.drum), export MIDI (.mid).
 *
 * La sortie est du MIDI : chaque pas envoie ses notes (0x90) sur le canal 10 (celui des percussions)
 * au synthé General MIDI d'AudioKit, qui les joue avec la SoundFont du système ; le kit de batterie
 * (banque 128 de la SoundFont) se choisit par un changement de programme (0xC0).
 *
 * Deux threads : l'interface (celui-ci) et le thread du son, qui avance le séquenceur à la trame
 * près, rend le synthé et alimente le flux. Le synthé MIDI n'a pas de verrou : g_lock le protège,
 * ainsi que la liste des pistes quand elle change de forme.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <uikit.h>
#include <audiokit_abi.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

#define MAX_TRACKS	64
#define MAX_BARS	8
#define MAX_STEPS	(24 * MAX_BARS)	/* 12/8 : 24 doubles croches par mesure */
#define CHUNK		64		/* trames rendues d'un coup par le thread du son */
#define DRUM_CH		9		/* canal 10 du General MIDI : les percussions */
#define RING		64		/* pas joués, gardés pour la tête de lecture */
#define MAX_KITS	32
#define FADE_MS		220		/* extinction du voyant d'une piste */
#define VEL_NORMAL	92
#define VEL_ACCENT	127
#define NOTE_FIRST	27
#define NOTE_LAST	87

/* ---------------------------------------------------------------- les percussions */

/* familles : leur couleur vient du thème (0 : un mélange, voir fam_color) */
struct family { const char *name; int tone; };
static const struct family g_fams[] = {
	{ "Grosses caisses",		TJ_DANGER },
	{ "Caisses claires et claps",	TJ_WARNING },
	{ "Charlestons",		TJ_SUCCESS },
	{ "Toms",			0 },
	{ "Cymbales",			TJ_ACCENT },
	{ "Peaux latines",		0 },
	{ "Secoués et grelots",		0 },
	{ "Bois et métaux",		0 },
	{ "Effets et bruitages",	0 },
};
#define N_FAMS	((int) (sizeof g_fams / sizeof g_fams[0]))
enum { F_KICK, F_SNARE, F_HAT, F_TOM, F_CYM, F_LATIN, F_SHAKE, F_WOOD, F_FX };

/* les notes 27 à 87, dans l'ordre (GM : 35 à 81 ; GS : 27 à 34 et 82 à 87) */
struct drum { const char *name; int fam; };
static const struct drum g_drums[NOTE_LAST - NOTE_FIRST + 1] = {
	{ "High Q", F_FX },			/* 27 */
	{ "Slap", F_SNARE },
	{ "Scratch poussé", F_FX },
	{ "Scratch tiré", F_FX },
	{ "Baguettes", F_FX },
	{ "Clic carré", F_FX },
	{ "Clic de métronome", F_FX },
	{ "Cloche de métronome", F_FX },
	{ "Grosse caisse acoustique", F_KICK },	/* 35 */
	{ "Grosse caisse", F_KICK },
	{ "Rimshot", F_SNARE },
	{ "Caisse claire", F_SNARE },
	{ "Clap", F_SNARE },
	{ "Caisse claire électrique", F_SNARE },/* 40 */
	{ "Tom basse grave", F_TOM },
	{ "Charleston fermé", F_HAT },
	{ "Tom basse aigu", F_TOM },
	{ "Charleston au pied", F_HAT },
	{ "Tom grave", F_TOM },			/* 45 */
	{ "Charleston ouvert", F_HAT },
	{ "Tom médium grave", F_TOM },
	{ "Tom médium aigu", F_TOM },
	{ "Crash 1", F_CYM },
	{ "Tom aigu", F_TOM },			/* 50 */
	{ "Ride 1", F_CYM },
	{ "Cymbale chinoise", F_CYM },
	{ "Cloche de ride", F_CYM },
	{ "Tambourin", F_SHAKE },
	{ "Splash", F_CYM },			/* 55 */
	{ "Cloche", F_WOOD },
	{ "Crash 2", F_CYM },
	{ "Vibraslap", F_WOOD },
	{ "Ride 2", F_CYM },
	{ "Bongo aigu", F_LATIN },		/* 60 */
	{ "Bongo grave", F_LATIN },
	{ "Conga aiguë étouffée", F_LATIN },
	{ "Conga aiguë ouverte", F_LATIN },
	{ "Conga grave", F_LATIN },
	{ "Timbale aiguë", F_LATIN },		/* 65 */
	{ "Timbale grave", F_LATIN },
	{ "Agogo aigu", F_WOOD },
	{ "Agogo grave", F_WOOD },
	{ "Cabasa", F_SHAKE },
	{ "Maracas", F_SHAKE },			/* 70 */
	{ "Sifflet court", F_FX },
	{ "Sifflet long", F_FX },
	{ "Güiro court", F_FX },
	{ "Güiro long", F_FX },
	{ "Claves", F_WOOD },			/* 75 */
	{ "Wood-block aigu", F_WOOD },
	{ "Wood-block grave", F_WOOD },
	{ "Cuíca étouffée", F_FX },
	{ "Cuíca ouverte", F_FX },
	{ "Triangle étouffé", F_WOOD },		/* 80 */
	{ "Triangle ouvert", F_WOOD },
	{ "Shaker", F_SHAKE },
	{ "Grelots", F_SHAKE },
	{ "Arbre à cloches", F_WOOD },
	{ "Castagnettes", F_WOOD },		/* 85 */
	{ "Surdo étouffé", F_LATIN },
	{ "Surdo ouvert", F_LATIN },
};

static const struct drum *drum_of (int note)
{
	if (note < NOTE_FIRST || note > NOTE_LAST) note = 36;
	return &g_drums[note - NOTE_FIRST];
}

enum {
	N_KICK = 36, N_RIM = 37, N_SNARE = 38, N_CLAP = 39, N_HHC = 42, N_HHO = 46, N_TOML = 45, N_TOMH = 50,
	N_CRASH = 49, N_RIDE = 51, N_BELL = 56, N_MARACAS = 70,
};

/* les rangées, dans l'ordre d'un batteur : grosses caisses, caisses, charlestons, toms (de l'aigu au grave),
 * cymbales, puis peaux latines, secoués, bois et métaux, effets ; une rangée par percussion du kit */
static const int g_order[] = {
	36, 35,					/* grosses caisses */
	38, 40, 37, 39, 28,			/* caisses claires et claps */
	42, 44, 46,				/* charlestons */
	50, 48, 47, 45, 43, 41,			/* toms */
	49, 57, 51, 59, 53, 55, 52,		/* cymbales */
	60, 61, 62, 63, 64, 65, 66, 86, 87,	/* peaux latines */
	54, 69, 70, 82, 83,			/* secoués et grelots */
	56, 67, 68, 75, 76, 77, 80, 81, 58, 84, 85,	/* bois et métaux */
	71, 72, 73, 74, 78, 79, 31, 33, 34, 32, 27, 29, 30,	/* effets */
};
#define N_ORDER	((int) (sizeof g_order / sizeof g_order[0]))

/* la vue compacte montre toujours la batterie de base, puis ce qui joue */
static const int g_core[] = { N_KICK, N_SNARE, N_RIM, N_CLAP, N_HHC, N_HHO, N_TOMH, N_TOML, N_CRASH, N_RIDE };
#define N_CORE	((int) (sizeof g_core / sizeof g_core[0]))

/* signatures : pas (doubles croches) par mesure, pas par temps */
struct signature { const char *name; int num, den, spb, group; };
static const struct signature g_sigs[] = {
	{ "3/4", 3, 4, 12, 4 },
	{ "4/4", 4, 4, 16, 4 },
	{ "5/4", 5, 4, 20, 4 },
	{ "6/8", 6, 8, 12, 6 },
	{ "12/8", 12, 8, 24, 6 },
};
#define N_SIGS	((int) (sizeof g_sigs / sizeof g_sigs[0]))
#define SIG_44	1

/* motifs tout faits (4/4, une mesure, recopiée dans chacune) : « x » une note, « X » un accent */
struct prow { int note; const char *pat; };
struct preset { const char *name; int tempo, swing; struct prow rows[10]; };

static const struct preset g_presets[] = {
	{ "Rock", 112, 0, {
		{ N_KICK, "X.......x.x....." }, { N_SNARE, "....X.......X..." }, { N_HHC, "X.x.x.x.X.x.x.x." },
		{ N_HHO, "................" }, { N_TOML, "..............xx" }, { N_TOMH, "................" },
		{ N_CRASH, "X..............." }, { N_RIDE, "................" } } },
	{ "Pop", 100, 0, {
		{ N_KICK, "X.....x.x......." }, { N_SNARE, "....X.......X..." }, { N_CLAP, "....x.......x..." },
		{ N_HHC, "X.x.X.x.X.x.X.x." }, { N_HHO, "................" }, { N_CRASH, "................" },
		{ 69, ".x.x.x.x.x.x.x.x" } } },
	{ "Funk", 98, 18, {
		{ N_KICK, "X..x..x...x..x.." }, { N_SNARE, "....X..x.x..X..." }, { N_HHC, "XxxxXxxxXxxxXxx." },
		{ N_HHO, "...............x" }, { N_BELL, "x.......x......." }, { N_CRASH, "................" } } },
	{ "Disco", 120, 0, {
		{ N_KICK, "X...X...X...X..." }, { N_CLAP, "....X.......X..." }, { N_HHC, "x...x...x...x..." },
		{ N_HHO, "..x...x...x...x." }, { 54, "....x.......x..." }, { N_MARACAS, "xxxxxxxxxxxxxxxx" } } },
	{ "House", 124, 0, {
		{ N_KICK, "X...X...X...X..." }, { N_CLAP, "....x.......x..." }, { N_HHC, ".x.x.x.x.x.x.x.x" },
		{ N_HHO, "..X...X...X...X." }, { N_RIDE, "x.x.x.x.x.x.x.x." }, { 82, "xxxxxxxxxxxxxxxx" } } },
	{ "Hip-hop", 90, 28, {
		{ N_KICK, "X......xx.x....." }, { N_SNARE, "....X.......X..." }, { N_RIM, "..........x....." },
		{ N_HHC, "x.x.x.x.x.x.x.xx" }, { N_HHO, "................" } } },
	{ "Bossa nova", 140, 0, {
		{ N_KICK, "x..xx..xx..xx..x" }, { N_RIM, "x..x..x...x..x.." }, { N_HHC, "x.x.x.x.x.x.x.x." },
		{ 69, "XxxxXxxxXxxxXxxx" } } },
	{ "Salsa", 96, 0, {
		{ 75, "x..x..x...x.x..." }, { N_BELL, "X.x.X.x.X.xxX.x." }, { 64, "......xx......xx" },
		{ 63, "....x.......x..." }, { 62, "x.......x......." }, { N_KICK, "...x.......x...." },
		{ N_MARACAS, "x.x.x.x.x.x.x.x." } } },
	{ "Reggae", 76, 30, {
		{ N_KICK, "........X......." }, { N_RIM, "........X......." }, { N_HHC, "x.X.x.X.x.X.x.X." },
		{ N_HHO, "..............x." } } },
	{ "Électro", 128, 0, {
		{ N_KICK, "X..x..X...X..x.." }, { N_CLAP, "....X.......X..." }, { N_HHC, "xxXxxxXxxxXxxxXx" },
		{ N_TOMH, "......x.......x." }, { N_BELL, "...x.......x...." }, { 76, "..x.....x....x.." } } },
};
#define N_PRESETS	((int) (sizeof g_presets / sizeof g_presets[0]))

/* ---------------------------------------------------------------- le rythme */

struct track
{
	int note;			/* percussion (27..87) */
	int level;			/* 0..100 */
	unsigned char mute, solo;
	unsigned char cell[MAX_STEPS];	/* 0 rien, 1 note, 2 accent */
};

struct song
{
	int tempo;			/* noires par minute */
	int swing;			/* 0..50 : retard des pas impairs, en % d'un pas */
	int sig;			/* g_sigs[] */
	int bars;			/* 1..MAX_BARS */
	int kit;			/* programme MIDI du kit (0 Standard, 25 808/909...) */
	int volume;			/* 0..100 */
	int ntracks;
	struct track t[MAX_TRACKS];
};

static struct song g;

static int spb (void) { return g_sigs[g.sig].spb; }
static int grp (void) { return g_sigs[g.sig].group; }
static int song_steps (void) { return spb () * g.bars; }

/* ---------------------------------------------------------------- interface : état */

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }
static unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static unsigned tone (unsigned c, int t) { return (unsigned) ui_tone (c, t) & 0xFFFFFF; }
static unsigned ink_on (unsigned c) { return (unsigned) ui_ink (c) & 0xFFFFFF; }

static long g_grid, g_play, g_lcd, g_tempo, g_swing, g_sigsel, g_bars, g_kitsel, g_presetsel, g_vol, g_status;
static int g_modified;
static char g_path[512];

static int g_nkits;
static int g_kit_prog[MAX_KITS];
static char g_kit_names[MAX_KITS][24];

/* étiquettes dessinées au-dessus des réglages de la barre */
struct cap { long widget; const char *text; int x, y; };
static struct cap g_caps[10];
static int g_ncaps;
static int g_bar_x, g_bar_y, g_bar_w, g_bar_h;

/* ---------------------------------------------------------------- son : état partagé */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static long g_synth = -1, g_stream = -1;
static pthread_t g_audio;
static volatile int g_audio_run;
static volatile int g_playing;		/* le séquenceur avance */
static volatile int g_start_req;	/* l'interface demande le départ au pas 0 */
static long g_written;			/* trames écrites dans le flux */
static double g_next;			/* trame du prochain pas */
static int g_step;			/* prochain pas à jouer */
static unsigned char g_held[128];	/* note en cours (relâchée avant d'être rejouée) */
static struct { volatile long frame; volatile int step; } g_ring[RING];
static volatile unsigned g_ring_head;

static unsigned now_ms (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static int any_solo (void)
{
	for (int i = 0; i < g.ntracks; i++)
		if (g.t[i].solo) return 1;
	return 0;
}

static int audible (const struct track *t, int solo) { return !t->mute && (!solo || t->solo); }

static int velocity (const struct track *t, int c)
{
	int v = (c == 2 ? VEL_ACCENT : VEL_NORMAL) * t->level / 100;
	return v < 1 ? 1 : v > 127 ? 127 : v;
}

/* une note (verrou pris) */
static void note_locked (int note, int vel)
{
	if (g_synth < 0) return;
	if (g_held[note]) ak_synth_midi (g_synth, DRUM_CH, 0x80, note, 0);
	ak_synth_midi (g_synth, DRUM_CH, 0x90, note, vel);
	g_held[note] = 1;
}

static void preview_note (int note, int vel)
{
	pthread_mutex_lock (&g_lock);
	note_locked (note, vel);
	pthread_mutex_unlock (&g_lock);
}

static void preview (int i, int c) { preview_note (g.t[i].note, velocity (&g.t[i], c ? c : 1)); }

static void send_kit (void)
{
	pthread_mutex_lock (&g_lock);
	if (g_synth >= 0) ak_synth_midi (g_synth, DRUM_CH, 0xC0, g.kit, 0);
	pthread_mutex_unlock (&g_lock);
}

static void apply_volume (void)
{
	if (g_stream >= 0) ak_volume (g_stream, g.volume * 256 / 100);
}

/* les notes d'un pas (thread du son, verrou pris) */
static void trigger_step (int s, long frame)
{
	int solo = any_solo ();
	for (int i = 0; i < g.ntracks; i++)
	{
		const struct track *t = &g.t[i];
		if (t->cell[s] && audible (t, solo)) note_locked (t->note, velocity (t, t->cell[s]));
	}
	unsigned h = g_ring_head;
	g_ring[h % RING].step = s;
	g_ring[h % RING].frame = frame;
	g_ring_head = h + 1;
}

static void *audio_main (void *arg)
{
	(void) arg;
	static int16_t buf[CHUNK * 2];
	while (g_audio_run)
	{
		if (ak_space (g_stream) < CHUNK * 4) { usleep (1000); continue; }
		pthread_mutex_lock (&g_lock);
		if (g_start_req) { g_start_req = 0; g_playing = 1; g_step = 0; g_next = (double) g_written; }
		if (g_playing)
			while (g_next < (double) (g_written + CHUNK))
			{
				if (g_step >= song_steps ()) g_step = 0;	/* le rythme a raccourci */
				trigger_step (g_step, (long) g_next);
				/* un pas = une double croche ; le swing allonge les pas pairs, raccourcit les impairs */
				double len = SND_RATE * 60.0 / (g.tempo * 4.0), sw = g.swing / 100.0;
				g_next += (g_step & 1) ? len * (1.0 - sw) : len * (1.0 + sw);
				g_step = (g_step + 1) % song_steps ();
			}
		ak_synth_render (g_synth, (long) buf, CHUNK);
		pthread_mutex_unlock (&g_lock);
		ak_write (g_stream, (long) buf, CHUNK * 4);
		g_written += CHUNK;
	}
	return 0;
}

static int audio_open (void)
{
	if (!audiokit_kit_open ()) return 0;
	g_synth = ak_synth_new (0, 0);		/* SoundFont du système, sinon banque FM */
	if (g_synth < 0) return 0;
	g_stream = ak_open (SND_RATE, 2, 16, 0);
	if (g_stream < 0) { ak_synth_free (g_synth); g_synth = -1; return 0; }
	ak_latency (g_stream, 8192);		/* ~46 ms d'avance : le pas s'entend tout de suite */
	for (int i = 0; i < RING; i++) g_ring[i].frame = -1;
	g_audio_run = 1;
	if (pthread_create (&g_audio, 0, audio_main, 0) != 0) { g_audio_run = 0; return 0; }
	return 1;
}

static void audio_close (void)
{
	if (g_audio_run) { g_audio_run = 0; pthread_join (g_audio, 0); }
	if (g_stream >= 0) ak_close (g_stream);
	if (g_synth >= 0) ak_synth_free (g_synth);
	g_stream = g_synth = -1;
}

/* le pas qu'on entend maintenant (-1 : arrêt) */
static int heard_step (void)
{
	if (!g_playing || g_stream < 0) return -1;
	long pos = ak_position (g_stream), best_f = -1;
	int best = -1;
	for (int i = 0; i < RING; i++)
	{
		long f = g_ring[i].frame;
		if (f >= 0 && f <= pos && f > best_f) { best_f = f; best = g_ring[i].step; }
	}
	return best < song_steps () ? best : -1;
}

/* les kits de batterie de la SoundFont (banque 128) */
static void load_kits (void)
{
	long sf = ak_sf_default ();
	g_nkits = 0;
	if (sf > 0)
	{
		long n = ak_sf_presets (sf);
		for (long i = 0; i < n && g_nkits < MAX_KITS; i++)
		{
			unsigned char p[AKSP_SIZE] __attribute__ ((aligned (8)));
			if (ak_sf_preset (sf, i, (long) p) < 0) continue;
			int bank = *(int32_t *) (p + AKSP_BANK), prog = *(int32_t *) (p + AKSP_PROGRAM);
			if (bank != 128) continue;
			int k = g_nkits++;
			while (k > 0 && g_kit_prog[k - 1] > prog)	/* rangés par programme */
			{
				g_kit_prog[k] = g_kit_prog[k - 1];
				memcpy (g_kit_names[k], g_kit_names[k - 1], sizeof g_kit_names[k]);
				k--;
			}
			g_kit_prog[k] = prog;
			snprintf (g_kit_names[k], sizeof g_kit_names[k], "%.*s", 20, (const char *) (p + AKSP_NAME));
		}
	}
	if (!g_nkits)
	{
		g_nkits = 1;
		g_kit_prog[0] = 0;
		snprintf (g_kit_names[0], sizeof g_kit_names[0], "%s", TR ("Standard"));
	}
}

static int kit_index (int prog)
{
	for (int i = 0; i < g_nkits; i++)
		if (g_kit_prog[i] == prog) return i;
	return 0;
}

/* ---------------------------------------------------------------- rythme : opérations */

static void update_title (void);
static void sync_widgets (void);
static void clamp_scroll (void);
static int g_scroll;

static int g_page;			/* la mesure montrée */
static int g_follow = 1;		/* la grille suit la lecture */

static int track_of (int note)
{
	for (int i = 0; i < g.ntracks; i++)
		if (g.t[i].note == note) return i;
	return -1;
}

static void track_init (struct track *t, int note)
{
	memset (t, 0, sizeof *t);
	t->note = note;
	t->level = 100;
}

/* toutes les rangées, dans l'ordre de g_order, vides */
static void song_fill_tracks (struct song *s)
{
	s->ntracks = N_ORDER;
	for (int i = 0; i < N_ORDER; i++) track_init (&s->t[i], g_order[i]);
}

static void song_reset (void)
{
	pthread_mutex_lock (&g_lock);
	memset (&g, 0, sizeof g);
	g.tempo = 110;
	g.sig = SIG_44;
	g.bars = 1;
	g.kit = g_nkits ? g_kit_prog[0] : 0;
	g.volume = 80;
	song_fill_tracks (&g);
	pthread_mutex_unlock (&g_lock);
	g_page = 0;
	g_scroll = 0;
}

static void touch (void)
{
	if (!g_modified) { g_modified = 1; update_title (); }
	ui_view_dirty (g_grid);
}

static void clear_all (void)
{
	for (int i = 0; i < g.ntracks; i++) memset (g.t[i].cell, 0, sizeof g.t[i].cell);
}

/* recopie la mesure b dans toutes les suivantes */
static void dup_bar (int b)
{
	int n = spb ();
	for (int i = 0; i < g.ntracks; i++)
		for (int k = b + 1; k < g.bars; k++)
			memcpy (g.t[i].cell + k * n, g.t[i].cell + b * n, n);
	touch ();
}

static void clear_bar (int b)
{
	int n = spb ();
	for (int i = 0; i < g.ntracks; i++) memset (g.t[i].cell + b * n, 0, n);
	touch ();
}

/* nouvelle signature : chaque mesure garde ses premiers pas */
static void set_signature (int sig)
{
	if (sig == g.sig || sig < 0 || sig >= N_SIGS) return;
	int on = g_sigs[g.sig].spb, nn = g_sigs[sig].spb, m = on < nn ? on : nn;
	pthread_mutex_lock (&g_lock);
	for (int i = 0; i < g.ntracks; i++)
	{
		unsigned char t[MAX_STEPS];
		memset (t, 0, sizeof t);
		for (int b = 0; b < g.bars; b++) memcpy (t + b * nn, g.t[i].cell + b * on, m);
		memcpy (g.t[i].cell, t, sizeof t);
	}
	g.sig = sig;
	pthread_mutex_unlock (&g_lock);
}

/* nouveau nombre de mesures : les mesures ajoutées reprennent les premières */
static void set_bars (int n)
{
	n = n < 1 ? 1 : n > MAX_BARS ? MAX_BARS : n;
	if (n == g.bars) return;
	int s = spb ();
	pthread_mutex_lock (&g_lock);
	for (int i = 0; i < g.ntracks; i++)
	{
		for (int k = g.bars; k < n; k++) memcpy (g.t[i].cell + k * s, g.t[i].cell + (k % g.bars) * s, s);
		if (n < g.bars) memset (g.t[i].cell + n * s, 0, (size_t) (MAX_STEPS - n * s));
	}
	g.bars = n;
	pthread_mutex_unlock (&g_lock);
	if (g_page >= n) g_page = n - 1;
}

static void load_preset (int k)
{
	const struct preset *p = &g_presets[k];
	pthread_mutex_lock (&g_lock);
	int bars = g.bars;
	g.sig = SIG_44;
	clear_all ();
	for (int r = 0; r < 10 && p->rows[r].pat; r++)
	{
		int i = track_of (p->rows[r].note);
		if (i < 0) continue;
		for (int s = 0; s < 16 * bars; s++)
		{
			char ch = p->rows[r].pat[s % 16];
			g.t[i].cell[s] = ch == 'X' ? 2 : ch == 'x' ? 1 : 0;
		}
	}
	g.tempo = p->tempo;
	g.swing = p->swing;
	pthread_mutex_unlock (&g_lock);
	g_scroll = 0;
	sync_widgets ();
	touch ();
}

/* un motif au hasard qui sonne : grosse caisse sur les temps forts, caisse claire en contretemps... */
static int chance (int pct) { return rand () % 100 < pct; }

static void random_pattern (void)
{
	int kick = track_of (N_KICK), snare = track_of (N_SNARE), hhc = track_of (N_HHC), hho = track_of (N_HHO);
	int ride = track_of (N_RIDE), crash = track_of (N_CRASH), clap = track_of (N_CLAP), rim = track_of (N_RIM);
	int toml = track_of (N_TOML), tomh = track_of (N_TOMH);
	clear_all ();
	int n = song_steps (), bar = spb (), gr = grp ();
	int use_ride = ride >= 0 && chance (30);
	int hat = use_ride ? ride : hhc;
	for (int s = 0; s < n; s++)
	{
		int i = s % bar, beat = i / gr, on_beat = i % gr == 0, last_bar = s / bar == g.bars - 1;
		struct track *t = g.t;
		if (i == 0) t[kick].cell[s] = 2;
		else if (on_beat && beat * 2 == bar / gr) t[kick].cell[s] = chance (80);
		else if ((i & 1) == 0) t[kick].cell[s] = chance (on_beat ? 35 : 18);
		else t[kick].cell[s] = chance (7);
		if (on_beat && (beat & 1)) t[snare].cell[s] = 2;
		else t[snare].cell[s] = chance (6);
		if (clap >= 0 && on_beat && (beat & 1) && chance (40)) t[clap].cell[s] = 1;
		if ((i & 1) == 0) t[hat].cell[s] = on_beat ? 2 : 1;
		else t[hat].cell[s] = chance (30);
		if (!use_ride && (i % gr) == gr / 2 && chance (15)) { t[hho].cell[s] = 1; t[hhc].cell[s] = 0; }
		if (last_bar && i >= bar - gr && chance (45))		/* un roulement à la fin */
		{
			int tt = chance (50) ? toml : tomh;
			if (tt >= 0) t[tt].cell[s] = 1; else t[snare].cell[s] = 1;
		}
		if (rim >= 0 && chance (5)) t[rim].cell[s] = 1;
	}
	if (crash >= 0 && chance (60)) g.t[crash].cell[0] = 2;
	touch ();
}

static void shift_pattern (int dir)
{
	int n = song_steps ();
	for (int i = 0; i < g.ntracks; i++)
	{
		unsigned char t[MAX_STEPS];
		for (int s = 0; s < n; s++) t[(s + dir + n) % n] = g.t[i].cell[s];
		memcpy (g.t[i].cell, t, n);
	}
	touch ();
}

static int track_empty (int i)
{
	for (int s = 0; s < MAX_STEPS; s++)
		if (g.t[i].cell[s]) return 0;
	return 1;
}

/* ---------------------------------------------------------------- fichiers .drum */

static int save_file (const char *path)
{
	FILE *f = fopen (path, "w");
	if (!f) return 0;
	int n = song_steps ();
	fprintf (f, "# Boîte à rythmes de Korin OS\n");
	fprintf (f, "tempo = %d\nswing = %d\nsignature = %s\nbars = %d\nsteps = %d\nkit = %d\nvolume = %d\n",
		 g.tempo, g.swing, g_sigs[g.sig].name, g.bars, n, g.kit, g.volume);
	for (int i = 0; i < g.ntracks; i++)
	{
		const struct track *t = &g.t[i];
		if (track_empty (i) && t->level == 100 && !t->mute && !t->solo) continue;	/* rangée par défaut */
		char pat[MAX_STEPS + 1];
		for (int s = 0; s < n; s++) pat[s] = g.t[i].cell[s] == 2 ? 'X' : g.t[i].cell[s] ? 'x' : '.';
		pat[n] = 0;
		fprintf (f, "track = %d %d %d %d %s\n", g.t[i].note, g.t[i].level, g.t[i].mute, g.t[i].solo, pat);
	}
	return fclose (f) == 0;
}

static int load_file (const char *path)
{
	FILE *f = fopen (path, "r");
	if (!f) return 0;
	static struct song s;
	memset (&s, 0, sizeof s);
	s.tempo = 110; s.sig = SIG_44; s.bars = 0; s.kit = 0; s.volume = 80;
	song_fill_tracks (&s);
	char line[512];
	int ok = 0, steps = 0;
	while (fgets (line, sizeof line, f))
	{
		int v, note, lvl, mu, so;
		char word[256];
		if (sscanf (line, "tempo = %d", &v) == 1) { s.tempo = v < 40 ? 40 : v > 240 ? 240 : v; ok = 1; }
		else if (sscanf (line, "swing = %d", &v) == 1) s.swing = v < 0 ? 0 : v > 50 ? 50 : v;
		else if (sscanf (line, "signature = %15s", word) == 1)
		{
			for (int k = 0; k < N_SIGS; k++)
				if (!strcmp (word, g_sigs[k].name)) s.sig = k;
		}
		else if (sscanf (line, "bars = %d", &v) == 1) s.bars = v < 1 ? 1 : v > MAX_BARS ? MAX_BARS : v;
		else if (sscanf (line, "steps = %d", &v) == 1) steps = v;
		else if (sscanf (line, "kit = %d", &v) == 1) s.kit = v & 127;
		else if (sscanf (line, "volume = %d", &v) == 1) s.volume = v < 0 ? 0 : v > 100 ? 100 : v;
		else if ((sscanf (line, "track = %d %d %d %d %255s", &note, &lvl, &mu, &so, word) == 5 ||
			  sscanf (line, "row = %d %d %d %d %255s", &note, &lvl, &mu, &so, word) == 5))
		{
			struct track *t = 0;
			for (int i = 0; i < s.ntracks; i++)
				if (s.t[i].note == note) t = &s.t[i];
			if (!t) continue;		/* note hors du kit */
			t->level = lvl < 0 ? 0 : lvl > 100 ? 100 : lvl;
			t->mute = mu != 0;
			t->solo = so != 0;
			memset (t->cell, 0, sizeof t->cell);
			for (int i = 0; i < MAX_STEPS && word[i]; i++) t->cell[i] = word[i] == 'X' ? 2 : word[i] == 'x' ? 1 : 0;
		}
	}
	fclose (f);
	if (!ok) return 0;
	if (!s.bars) s.bars = steps >= g_sigs[s.sig].spb ? steps / g_sigs[s.sig].spb : 1;
	if (s.bars > MAX_BARS) s.bars = MAX_BARS;
	for (int i = 0; i < s.ntracks; i++)	/* rien au-delà de la fin */
		memset (s.t[i].cell + s.bars * g_sigs[s.sig].spb, 0, (size_t) (MAX_STEPS - s.bars * g_sigs[s.sig].spb));
	pthread_mutex_lock (&g_lock);
	g = s;
	pthread_mutex_unlock (&g_lock);
	g_page = 0;
	g_scroll = 0;
	return 1;
}

/* ---------------------------------------------------------------- export MIDI (.mid, format 0) */

struct mev { unsigned tick; unsigned char st, d1, d2, order; };

static int mev_cmp (const void *a, const void *b)
{
	const struct mev *x = a, *y = b;
	if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
	return x->order - y->order;		/* les fins de note avant les notes du même instant */
}

struct bytes { unsigned char *p; int n, cap; };

static void put (struct bytes *b, int c)
{
	if (b->n >= b->cap)
	{
		int cap = b->cap ? b->cap * 2 : 1024;
		unsigned char *np = realloc (b->p, (size_t) cap);
		if (!np) return;			/* plus de mémoire : l'octet est perdu */
		b->p = np;
		b->cap = cap;
	}
	b->p[b->n++] = (unsigned char) c;
}

static void put_vlq (struct bytes *b, unsigned v)
{
	unsigned char t[5];
	int n = 0;
	t[n++] = v & 0x7F;
	while (v >>= 7) t[n++] = 0x80 | (v & 0x7F);
	while (n) put (b, t[--n]);
}

static int export_midi (const char *path, int repeats)
{
	const int ppq = 96, step_ticks = ppq / 4;
	int solo = any_solo (), n = 0, steps = song_steps ();
	struct mev *ev = malloc (sizeof *ev * (size_t) repeats * steps * g.ntracks * 2 + 1);
	if (!ev) return 0;
	for (int k = 0; k < repeats; k++)
		for (int s = 0; s < steps; s++)
		{
			unsigned t = (unsigned) ((k * steps + s) * step_ticks);
			if (s & 1) t += (unsigned) (step_ticks * g.swing / 100);
			for (int i = 0; i < g.ntracks; i++)
			{
				const struct track *tr = &g.t[i];
				int c = tr->cell[s];
				if (!c || !audible (tr, solo)) continue;
				ev[n++] = (struct mev) { t, 0x90 | DRUM_CH, (unsigned char) tr->note, (unsigned char) velocity (tr, c), 1 };
				ev[n++] = (struct mev) { t + step_ticks / 2, 0x80 | DRUM_CH, (unsigned char) tr->note, 0, 0 };
			}
		}
	qsort (ev, n, sizeof *ev, mev_cmp);

	struct bytes tr = { 0 };
	static const char name[] = "Boite a rythmes";
	put (&tr, 0); put (&tr, 0xFF); put (&tr, 0x03); put (&tr, sizeof name - 1);	/* nom de la piste */
	for (const char *c = name; *c; c++) put (&tr, *c);
	unsigned us = 60000000u / (unsigned) g.tempo;					/* tempo */
	put (&tr, 0); put (&tr, 0xFF); put (&tr, 0x51); put (&tr, 3);
	put (&tr, us >> 16); put (&tr, (us >> 8) & 0xFF); put (&tr, us & 0xFF);
	const struct signature *sg = &g_sigs[g.sig];					/* signature */
	put (&tr, 0); put (&tr, 0xFF); put (&tr, 0x58); put (&tr, 4);
	put (&tr, sg->num); put (&tr, sg->den == 8 ? 3 : 2); put (&tr, sg->den == 8 ? 36 : 24); put (&tr, 8);
	put (&tr, 0); put (&tr, 0xC0 | DRUM_CH); put (&tr, g.kit);			/* le kit */
	put (&tr, 0); put (&tr, 0xB0 | DRUM_CH); put (&tr, 7); put (&tr, g.volume * 127 / 100);
	unsigned last = 0;
	for (int i = 0; i < n; i++)
	{
		put_vlq (&tr, ev[i].tick - last);
		last = ev[i].tick;
		put (&tr, ev[i].st); put (&tr, ev[i].d1); put (&tr, ev[i].d2);
	}
	unsigned end = (unsigned) (repeats * steps * step_ticks);			/* fin : la dernière mesure entière */
	put_vlq (&tr, end > last ? end - last : 0);
	put (&tr, 0xFF); put (&tr, 0x2F); put (&tr, 0);
	free (ev);

	FILE *f = fopen (path, "wb");
	if (!f) { free (tr.p); return 0; }
	static const unsigned char hdr[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k' };
	fwrite (hdr, 1, sizeof hdr, f);
	unsigned char len[4] = { (unsigned char) (tr.n >> 24), (unsigned char) (tr.n >> 16), (unsigned char) (tr.n >> 8), (unsigned char) tr.n };
	fwrite (len, 1, 4, f);
	fwrite (tr.p, 1, tr.n, f);
	free (tr.p);
	return fclose (f) == 0;
}

/* ---------------------------------------------------------------- la grille (vue dessinée) */

/* géométrie, recalculée à chaque dessin et lue par la souris */
static int g_head, gx0, gy0, g_cw, g_ch, g_beat_gap, g_ruler, g_pad, g_tabs_h, g_hdr_h, g_room;
static int gw_view, gh_view;
static int g_hover_r = -1, g_hover_s = -1;
static int g_heard = -1;			/* pas entendu (tête de lecture) */
static unsigned g_hit_ms[MAX_TRACKS];		/* dernier coup entendu par piste (voyants) */
static int g_compact;				/* vue compacte : la batterie de base et ce qui joue */
static unsigned char g_folded[N_FAMS];		/* familles repliées */

enum { DRAG_NONE, DRAG_PAINT, DRAG_LEVEL, DRAG_SCROLL };
static int g_drag, g_drag_val, g_drag_row, g_drag_y0, g_drag_scroll0;

/* zones cliquables de l'en-tête de la grille */
enum { HOT_PAGE = 0, HOT_FOLLOW = 20, HOT_DUP, HOT_CLEARBAR, HOT_ALL, HOT_COMPACT };
struct hot { int x, y, w, h, id; };
static struct hot g_hot[24];
static int g_nhot;

/* les lignes de la liste : en-têtes de famille et rangées */
struct entry { int fam; int track; };		/* track < 0 : l'en-tête de la famille fam */
static struct entry g_ent[MAX_TRACKS + N_FAMS];
static int g_nent;
/* lignes dessinées (pour la souris) */
static int g_shown_y[MAX_TRACKS + N_FAMS], g_shown_e[MAX_TRACKS + N_FAMS], g_nshown;

static int cell_x (int i) { return gx0 + i * g_cw + (i / grp ()) * g_beat_gap; }

static int is_core (int note)
{
	for (int k = 0; k < N_CORE; k++)
		if (g_core[k] == note) return 1;
	return 0;
}

static int track_shown (int i) { return !g_compact || is_core (g.t[i].note) || !track_empty (i); }

static void build_entries (void)
{
	g_nent = 0;
	for (int f = 0; f < N_FAMS; f++)
	{
		int first = 1;
		for (int i = 0; i < g.ntracks; i++)
		{
			if (drum_of (g.t[i].note)->fam != f || !track_shown (i)) continue;
			if (first) { g_ent[g_nent++] = (struct entry) { f, -1 }; first = 0; }
			if (!g_folded[f]) g_ent[g_nent++] = (struct entry) { f, i };
		}
	}
}

static int entry_h (int e) { return g_ent[e].track < 0 ? g_hdr_h : g_ch; }

static int max_scroll (void)
{
	int room = g_room, e = g_nent;
	while (e > 0 && room - entry_h (e - 1) >= 0) room -= entry_h (--e);
	return e;
}

static void clamp_scroll (void)
{
	int m = max_scroll ();
	if (g_scroll > m) g_scroll = m;
	if (g_scroll < 0) g_scroll = 0;
}

static void layout_grid (int w, int h)
{
	gw_view = w; gh_view = h;
	g_pad = 14;
	g_tabs_h = 34;
	g_ruler = 26;
	g_hdr_h = 24;
	g_head = w * 25 / 100;
	if (g_head < 210) g_head = 210;
	if (g_head > 280) g_head = 280;
	gx0 = g_pad + g_head;
	gy0 = g_pad + g_tabs_h + g_ruler;
	int n = spb (), groups = n / grp ();
	g_beat_gap = n <= 16 ? 8 : 6;
	int avail = w - gx0 - g_pad - 10 - (groups - 1) * g_beat_gap;
	g_cw = avail / n;
	if (g_cw < 10) g_cw = 10;
	build_entries ();
	g_room = h - gy0 - g_pad;
	int ctl = (int) ui_metric (UMT_CTL);
	int minh = ctl > 28 ? ctl : 28, maxh = minh + 10, rows = 0, hdrs = 0;
	for (int e = 0; e < g_nent; e++) g_ent[e].track < 0 ? hdrs++ : rows++;
	g_ch = rows ? (g_room - hdrs * g_hdr_h) / rows : minh;	/* la vue compacte remplit la hauteur */
	if (g_ch < minh) g_ch = minh;
	if (g_ch > maxh) g_ch = maxh;
	clamp_scroll ();
}

/* parties de l'en-tête d'une rangée : voyant, nom, niveau, M, S */
static int head_level_x (void) { return g_pad + g_head - 16 - 2 * 24 - 8 - 40; }
static int head_mute_x (void) { return g_pad + g_head - 16 - 2 * 24 + 2; }
static int head_solo_x (void) { return g_pad + g_head - 16 - 24 + 4; }

/* couleur d'une famille : un jeton du thème, ou le mélange de deux (violet, orange, sarcelle...) */
static unsigned fam_color (int f)
{
	switch (f)
	{
	case F_TOM:	return mix (jet (TJ_DANGER), jet (TJ_ACCENT), 150);
	case F_LATIN:	return mix (jet (TJ_DANGER), jet (TJ_WARNING), 128);
	case F_SHAKE:	return mix (jet (TJ_SUCCESS), jet (TJ_WARNING), 128);
	case F_WOOD:	return mix (jet (TJ_SUCCESS), jet (TJ_ACCENT), 128);
	case F_FX:	return mix (jet (TJ_ACCENT), jet (TJ_TEXT3), 150);
	default:	return jet (g_fams[f].tone);
	}
}

static unsigned track_color (int i) { return fam_color (drum_of (g.t[i].note)->fam); }

static void chip (int x, int y, int w, int h, const char *t, int on, unsigned col, int size)
{
	unsigned bg = on ? col : jet (TJ_SUNKEN);
	unsigned ink = on ? ink_on (col) : jet (TJ_TEXT2);
	if (on) ui_rbox (x, y, w, h, 6, bg, bg);
	else
	{
		ui_rbox (x, y, w, h, 6, jet (TJ_LINE), jet (TJ_LINE));
		ui_rbox (x + 1, y + 1, w - 2, h - 2, 5, bg, bg);
	}
	int tw = (int) ui_text_width ((long) t, size, (long) ui.font_b);
	ui_text ((long) t, x + (w - tw) / 2, y + h / 2 + size / 2 - 1, ink, size, (long) ui.font_b, 0);
}

/* une pastille cliquable ; rend son bord droit */
static int hot_chip_sz (int x, int y, int h, const char *t, int on, unsigned col, int id, int minw, int size)
{
	int w = (int) ui_text_width ((long) t, size, (long) ui.font_b) + 20;
	if (w < minw) w = minw;
	chip (x, y, w, h, t, on, col, size);
	if (g_nhot < (int) (sizeof g_hot / sizeof g_hot[0])) g_hot[g_nhot++] = (struct hot) { x, y, w, h, id };
	return x + w;
}

static int hot_chip (int x, int y, int h, const char *t, int on, unsigned col, int id, int minw)
{
	return hot_chip_sz (x, y, h, t, on, col, id, minw, 12);
}

/* l'en-tête d'une famille : flèche, nom, nombre de rangées qui jouent, un filet */
static void paint_family (int f, int y, unsigned surf)
{
	unsigned col = fam_color (f);
	int cy = y + g_hdr_h / 2, x = g_pad + 6;
	int32_t pt[6];
	if (g_folded[f])	/* ▸ */
	{
		pt[0] = x * 16; pt[1] = (cy - 5) * 16; pt[2] = x * 16; pt[3] = (cy + 5) * 16; pt[4] = (x + 7) * 16; pt[5] = cy * 16;
	}
	else			/* ▾ */
	{
		pt[0] = (x - 1) * 16; pt[1] = (cy - 3) * 16; pt[2] = (x + 9) * 16; pt[3] = (cy - 3) * 16; pt[4] = (x + 4) * 16; pt[5] = (cy + 3) * 16;
	}
	ui_vp_poly ((long) pt, 3, jet (TJ_TEXT2));
	const char *name = TR (g_fams[f].name);
	int end = (int) ui_text ((long) name, x + 16, cy + 5, col, 12, (long) ui.font_b, 0);
	int used = 0, total = 0;
	for (int i = 0; i < g.ntracks; i++)
		if (drum_of (g.t[i].note)->fam == f) { total++; used += !track_empty (i); }
	char t[48];
	snprintf (t, sizeof t, used ? TR ("%d sur %d en jeu") : TR ("%d instruments"), used ? used : total, total);
	end = (int) ui_text ((long) t, end + 10, cy + 5, jet (TJ_TEXT3), 11, (long) ui.font, 0);
	ui_fill (end + 10, cy, gw_view - g_pad - 14 - (end + 10), 1, mix (surf, col, 70));
}

static long grid_paint (long v, long w, long h)
{
	(void) v;
	layout_grid ((int) w, (int) h);
	unsigned face = jet (TJ_FACE), surf = jet (TJ_SURFACE), line = jet (TJ_LINE), acc = jet (TJ_ACCENT);
	int dark = s_jet[TJ_DARK / 4] & 1;
	ui_fill (0, 0, w, h, face);
	ui_rbox (0, 0, w, h, 12, line, line);			/* la carte et son contour */
	ui_rbox (1, 1, w - 2, h - 2, 11, surf, surf);

	unsigned now = now_ms ();
	int solo = any_solo (), n = spb (), gr = grp (), base = g_page * n;
	int heard_bar = g_heard >= 0 ? g_heard / n : -1, heard_i = g_heard >= 0 && heard_bar == g_page ? g_heard % n : -1;
	unsigned off_a = mix (surf, jet (TJ_SUNKEN), dark ? 200 : 230);
	unsigned off_b = mix (surf, jet (TJ_SUNKEN), dark ? 120 : 140);

	/* en-tête : les mesures (onglets), suivre la lecture, recopier, effacer */
	g_nhot = 0;
	int ty = g_pad, th = 26;
	ui_text ((long) TR ("Mesures"), g_pad + 4, ty + 18, jet (TJ_TEXT2), 13, (long) ui.font_b, 0);
	int x = gx0;
	for (int b = 0; b < g.bars; b++)
	{
		char t[8];
		snprintf (t, sizeof t, "%d", b + 1);
		int r = hot_chip (x, ty, th, t, b == g_page, acc, HOT_PAGE + b, 38);
		if (b == heard_bar)				/* la mesure qui joue : un point */
			ui_vp_disc ((x + (r - x) / 2) * 16, (ty + th + 4) * 16, 40, b == g_page ? acc : jet (TJ_TEXT2));
		x = r + 6;
	}
	int rx = (int) w - g_pad - 10;
	const char *lab_clear = TR ("Effacer la mesure"), *lab_dup = TR ("Recopier dans les suivantes"), *lab_follow = TR ("Suivre la lecture");
	int wc = (int) ui_text_width ((long) lab_clear, 12, (long) ui.font_b) + 20;
	int wd = (int) ui_text_width ((long) lab_dup, 12, (long) ui.font_b) + 20;
	int wf = (int) ui_text_width ((long) lab_follow, 12, (long) ui.font_b) + 20;
	int cx = rx - wc;
	if (cx > x + 12) hot_chip (cx, ty, th, lab_clear, 0, acc, HOT_CLEARBAR, 0);
	if (g.bars > 1 && g_page < g.bars - 1 && cx - 8 - wd > x + 12) hot_chip (cx - 8 - wd, ty, th, lab_dup, 0, acc, HOT_DUP, 0);
	cx -= 8 + wd;
	if (g.bars > 1 && cx - 8 - wf > x + 12) hot_chip (cx - 8 - wf, ty, th, lab_follow, g_follow, acc, HOT_FOLLOW, 0);

	/* règle : la vue (tous, compacte), puis les numéros des temps et des pas */
	int ry = g_pad + g_tabs_h;
	int vx = hot_chip_sz (g_pad + 2, ry - 2, 21, TR ("Tous"), !g_compact, acc, HOT_ALL, 0, 11);
	hot_chip_sz (vx + 4, ry - 2, 21, TR ("Compact"), g_compact, acc, HOT_COMPACT, 0, 11);
	int nbeats = n / gr;
	for (int b = 0; b < nbeats; b++)
	{
		int bx = cell_x (b * gr), bw = gr * g_cw - 3;
		int playing_beat = heard_i >= 0 && heard_i / gr == b;
		char t[8];
		snprintf (t, sizeof t, "%d", b + 1);
		ui_text ((long) t, bx + 2, ry + 15, playing_beat ? acc : jet (TJ_TEXT2), 13, (long) ui.font_b, 0);
		for (int k = 1; k < gr && g_cw >= 18; k++)
		{
			int i = b * gr + k;
			snprintf (t, sizeof t, ".%d", k + 1);
			ui_text ((long) t, cell_x (i) + 2, ry + 15, heard_i == i ? acc : jet (TJ_TEXT3), 11, (long) ui.font, 0);
		}
		ui_fill (bx, ry + 21, bw, 2, playing_beat ? acc : line);
	}

	/* les lignes visibles */
	g_nshown = 0;
	int y = gy0, bottom = gy0 + g_room;
	for (int e = g_scroll; e < g_nent && y + entry_h (e) <= bottom; e++)
	{
		g_shown_y[g_nshown] = y;
		g_shown_e[g_nshown++] = e;
		y += entry_h (e);
	}

	/* colonne entendue : un bandeau derrière les cases (par morceaux, entre les en-têtes) */
	if (heard_i >= 0)
	{
		unsigned band = mix (surf, acc, dark ? 60 : 44);
		for (int k = 0; k < g_nshown; )
		{
			if (g_ent[g_shown_e[k]].track < 0) { k++; continue; }
			int y0 = g_shown_y[k], y1 = y0;
			while (k < g_nshown && g_ent[g_shown_e[k]].track >= 0) { y1 = g_shown_y[k] + g_ch; k++; }
			ui_rbox (cell_x (heard_i) - 2, y0 - 2, g_cw + 1, y1 - y0, 6, band, band);
		}
	}

	int cell_h = g_ch - 6, cell_w = g_cw - 3;
	int radius = cell_h < cell_w ? cell_h / 4 : cell_w / 4;
	if (radius > 6) radius = 6;
	for (int k = 0; k < g_nshown; k++)
	{
		const struct entry *en = &g_ent[g_shown_e[k]];
		int ry2 = g_shown_y[k];
		if (en->track < 0) { paint_family (en->fam, ry2, surf); continue; }
		int i = en->track, cy = ry2 + cell_h / 2;
		struct track *tr = &g.t[i];
		unsigned col = track_color (i);
		int on_air = audible (tr, solo);

		/* en-tête : voyant qui s'allume au coup, nom, niveau, muet, solo */
		int fade = 0;
		if (g_hit_ms[i] && now - g_hit_ms[i] < FADE_MS) fade = 256 - (int) ((now - g_hit_ms[i]) * 256 / FADE_MS);
		unsigned led = mix (mix (surf, col, on_air ? 110 : 50), tone (col, 165), fade);
		if (fade) ui_vp_disc ((g_pad + 9) * 16, cy * 16 + 8, 7 * 16 + fade * 3 / 16, mix (surf, col, fade / 3));
		ui_vp_disc ((g_pad + 9) * 16, cy * 16 + 8, 5 * 16, led);
		int lx = head_level_x ();
		int fs = g_ch >= 34 ? 14 : 13, used = !track_empty (i);
		ui_text_fit ((long) TR (drum_of (tr->note)->name), g_pad + 22, cy + fs / 2 - 1,
			     !on_air ? jet (TJ_TEXT3) : used ? jet (TJ_TEXT1) : jet (TJ_TEXT2), fs, (long) ui.font, lx - (g_pad + 22) - 6);
		int lw = 40;					/* niveau : une piste et sa part remplie */
		ui_rbox (lx, cy - 2, lw, 5, 2, jet (TJ_SUNKEN), jet (TJ_SUNKEN));
		int fill = lw * tr->level / 100;
		unsigned lc = on_air ? col : jet (TJ_TEXT3);
		if (fill > 0) ui_rbox (lx, cy - 2, fill < 5 ? 5 : fill, 5, 2, lc, lc);
		int chh = cell_h < 22 ? cell_h : 22;
		chip (head_mute_x (), cy - chh / 2, 22, chh, "M", tr->mute, jet (TJ_WARNING), 11);
		chip (head_solo_x (), cy - chh / 2, 22, chh, "S", tr->solo, acc, 11);

		/* les cases de la mesure montrée */
		for (int s = 0; s < n; s++)
		{
			int cx2 = cell_x (s), c = tr->cell[base + s];
			unsigned bg = ((s / gr) & 1) ? off_b : off_a;
			if (!c) ui_rbox (cx2, ry2, cell_w, cell_h, radius, bg, bg);
			else
			{
				unsigned on = c == 2 ? col : mix (bg, col, 175);
				if (!on_air) on = mix (bg, on, 90);
				if (s == heard_i && on_air) on = tone (on, 160);	/* la case qui sonne s'éclaire */
				ui_rbox (cx2, ry2, cell_w, cell_h, radius, tone (on, 140), on);
				if (c == 2)					/* accent : un point au centre */
				{
					int d = cell_h < cell_w ? cell_h : cell_w;
					ui_vp_disc (cx2 * 16 + cell_w * 8, ry2 * 16 + cell_h * 8, d * 16 / 7, ink_on (on));
				}
			}
			if (i == g_hover_r && s == g_hover_s && !g_drag) ui_frame (cx2, ry2, cell_w, cell_h, jet (TJ_LINE_STRONG));
		}
	}

	/* barre de défilement, quand les lignes débordent */
	if (max_scroll () > 0)
	{
		int sx = (int) w - g_pad - 4, sy = gy0, sh = g_room - 6;
		int thumb = sh * g_nshown / (g_nent ? g_nent : 1);
		if (thumb < 24) thumb = 24;
		int ty2 = sy + (sh - thumb) * g_scroll / max_scroll ();
		ui_rbox (sx, sy, 6, sh, 3, jet (TJ_SUNKEN), jet (TJ_SUNKEN));
		unsigned tc = g_drag == DRAG_SCROLL ? acc : jet (TJ_LINE_STRONG);
		ui_rbox (sx, ty2, 6, thumb, 3, tc, tc);
	}
	return 0;
}

/* la ligne sous y (-1 : aucune) */
static int shown_at (int y)
{
	for (int k = 0; k < g_nshown; k++)
		if (y >= g_shown_y[k] && y < g_shown_y[k] + entry_h (g_shown_e[k])) return g_shown_e[k];
	return -1;
}

static int hit_cell (int x, int y, int *r, int *s)
{
	if (x < gx0) return 0;
	int e = shown_at (y);
	if (e < 0 || g_ent[e].track < 0) return 0;
	for (int k = 0; k < spb (); k++)
		if (x >= cell_x (k) && x < cell_x (k) + g_cw)
		{
			*r = g_ent[e].track; *s = k;
			return 1;
		}
	return 0;
}

static void set_level_from_x (int i, int x)
{
	int v = (x - head_level_x ()) * 100 / 40;
	v = v < 0 ? 0 : v > 100 ? 100 : v;
	v = (v + 2) / 5 * 5;
	if (v != g.t[i].level) { g.t[i].level = v; touch (); }
}

static void set_page (int p)
{
	if (p < 0 || p >= g.bars || p == g_page) return;
	g_page = p;
	ui_view_dirty (g_grid);
}

static void set_compact (int on)
{
	g_compact = on;
	g_scroll = 0;
	ui_menu_state (41, on ? 0 : UMS_CHECKED);
	ui_menu_state (42, on ? UMS_CHECKED : 0);
	ui_view_dirty (g_grid);
}

static void track_menu (int i, int x, int y)
{
	char buf[256];
	int n = 0;
	const char *items[] = { "Écouter", "Vider la rangée", g.t[i].mute ? "Rétablir le son" : "Rendre muette",
				g.t[i].solo ? "Enlever le solo" : "Jouer seule", "Niveau à 100 %" };
	for (int k = 0; k < 5; k++)
		n += snprintf (buf + n, sizeof buf - n, "%s%s", (k == 1 && track_empty (i)) ? "\x01" : "", TR (items[k])) + 1;
	buf[n] = 0;
	switch (ui_popup_local (x, y, (long) buf, 210))
	{
	case 0: preview (i, 1); break;
	case 1: memset (g.t[i].cell, 0, sizeof g.t[i].cell); touch (); break;
	case 2: g.t[i].mute ^= 1; touch (); break;
	case 3: g.t[i].solo ^= 1; touch (); break;
	case 4: g.t[i].level = 100; touch (); break;
	}
}

static long grid_mouse (long v, long kind, long x, long y, long info)
{
	(void) v;
	int r, s, buttons = (int) ((info >> 8) & 0xFF), mods = (int) (info >> 16);
	if (kind == 1)
	{
		/* les pastilles de l'en-tête */
		for (int k = 0; k < g_nhot; k++)
		{
			const struct hot *h = &g_hot[k];
			if (x < h->x || x >= h->x + h->w || y < h->y || y >= h->y + h->h) continue;
			if (h->id < HOT_FOLLOW) { set_page (h->id - HOT_PAGE); if (g_playing) g_follow = 0; }
			else if (h->id == HOT_FOLLOW) { g_follow ^= 1; ui_view_dirty (g_grid); }
			else if (h->id == HOT_DUP) dup_bar (g_page);
			else if (h->id == HOT_CLEARBAR) clear_bar (g_page);
			else if (h->id == HOT_ALL) set_compact (0);
			else if (h->id == HOT_COMPACT) set_compact (1);
			return 0;
		}
		/* la barre de défilement */
		if (max_scroll () > 0 && x >= gw_view - g_pad - 10 && y >= gy0)
		{
			g_drag = DRAG_SCROLL; g_drag_y0 = (int) y; g_drag_scroll0 = g_scroll;
			ui_view_dirty (g_grid);
			return 0;
		}
		if (hit_cell ((int) x, (int) y, &r, &s))
		{
			int c = g.t[r].cell[g_page * spb () + s];
			if ((buttons & 2) || (mods & UI_MOD_SHIFT))	/* clic droit, Maj : accent */
				g_drag_val = c == 2 ? 1 : 2;
			else
				g_drag_val = c ? 0 : 1;
			g.t[r].cell[g_page * spb () + s] = (unsigned char) g_drag_val;
			if (g_drag_val && !g_playing) preview (r, g_drag_val);
			g_drag = DRAG_PAINT;
			touch ();
			return 0;
		}
		int e = shown_at ((int) y);
		if (e < 0 || x < g_pad || x >= gx0) return 0;
		if (g_ent[e].track < 0)				/* en-tête de famille : replier, déplier */
		{
			g_folded[g_ent[e].fam] ^= 1;
			build_entries ();
			clamp_scroll ();
			ui_view_dirty (g_grid);
			return 0;
		}
		int i = g_ent[e].track;
		if (buttons & 2) track_menu (i, (int) x, (int) y);
		else if (x >= head_solo_x () && x < head_solo_x () + 22) { g.t[i].solo ^= 1; touch (); }
		else if (x >= head_mute_x () && x < head_mute_x () + 22) { g.t[i].mute ^= 1; touch (); }
		else if (x >= head_level_x () - 4 && x < head_level_x () + 44)
		{
			g_drag = DRAG_LEVEL; g_drag_row = i;
			set_level_from_x (i, (int) x);
		}
		else preview (i, 1);				/* le nom : écouter */
		return 0;
	}
	if (kind == 2) { g_drag = DRAG_NONE; ui_view_dirty (g_grid); return 0; }
	if (kind == 3) { g_hover_r = g_hover_s = -1; ui_view_dirty (g_grid); return 0; }
	/* mouvement */
	if (g_drag == DRAG_LEVEL) { set_level_from_x (g_drag_row, (int) x); return 0; }
	if (g_drag == DRAG_SCROLL)
	{
		int ns = g_drag_scroll0 + (int) (y - g_drag_y0) * g_nent / (g_room > 0 ? g_room : 1);
		if (ns != g_scroll) { g_scroll = ns; clamp_scroll (); ui_view_dirty (g_grid); }
		return 0;
	}
	if (hit_cell ((int) x, (int) y, &r, &s))
	{
		unsigned char *c = &g.t[r].cell[g_page * spb () + s];
		if (g_drag == DRAG_PAINT && *c != g_drag_val)
		{
			*c = (unsigned char) g_drag_val;
			if (g_drag_val && !g_playing) preview (r, g_drag_val);
			touch ();
		}
		if (r != g_hover_r || s != g_hover_s) { g_hover_r = r; g_hover_s = s; ui_view_dirty (g_grid); }
	}
	else if (g_hover_r >= 0) { g_hover_r = g_hover_s = -1; ui_view_dirty (g_grid); }
	return 0;
}

static long grid_wheel (long v, long steps)
{
	(void) v;
	int old = g_scroll;
	g_scroll -= (int) steps * 2;
	clamp_scroll ();
	if (g_scroll != old) ui_view_dirty (g_grid);
	return 0;
}

static long grid_key (long v, long code, long ch, long mods) { (void) v; (void) code; (void) ch; (void) mods; return -1; }

/* ---------------------------------------------------------------- bouton Lecture (vue ronde) */

static int g_play_hover;

static long play_paint (long v, long w, long h)
{
	(void) v;
	ui_fill (0, 0, w, h, jet (TJ_SURFACE));
	int d = (int) (w < h ? w : h), cx = (int) w / 2, cy = (int) h / 2;
	unsigned acc = g_playing ? jet (TJ_DANGER) : jet (TJ_ACCENT);
	if (g_play_hover) acc = tone (acc, 150);
	ui_vp_disc (cx * 16, cy * 16, d * 8, tone (acc, 110));
	ui_vp_disc (cx * 16, cy * 16, d * 8 - 24, acc);
	unsigned ink = ink_on (acc);
	int u = d / 6;
	if (g_playing)				/* carré : arrêt */
		ui_rbox (cx - u, cy - u, 2 * u, 2 * u, 3, ink, ink);
	else					/* triangle : lecture */
	{
		int32_t pt[6] = { (cx - u + 2) * 16, (cy - u - 2) * 16, (cx - u + 2) * 16, (cy + u + 2) * 16, (cx + u + 4) * 16, cy * 16 };
		ui_vp_poly ((long) pt, 3, ink);
	}
	return 0;
}

static void toggle_play (void);

static long play_mouse (long v, long kind, long x, long y, long info)
{
	(void) v; (void) x; (void) y; (void) info;
	if (kind == 1) toggle_play ();
	else if (kind == 0 && !g_play_hover) { g_play_hover = 1; ui_view_dirty (g_play); }
	else if (kind == 3) { g_play_hover = 0; ui_view_dirty (g_play); }
	return 0;
}

/* ---------------------------------------------------------------- barre de réglages */

static void set_lcd (int step)
{
	char t[16];
	if (step < 0) snprintf (t, sizeof t, "-.-.-");
	else
	{
		int i = step % spb ();
		snprintf (t, sizeof t, "%d.%d.%d", step / spb () + 1, i / grp () + 1, i % grp () + 1);
	}
	ui_w_set_text (g_lcd, (long) t);
}

static void set_status (void)
{
	char t[200];
	snprintf (t, sizeof t, TR ("%s  ·  %d BPM  ·  %s  ·  %d mesure(s)  ·  swing %d %%"),
		  g_kit_names[kit_index (g.kit)], g.tempo, g_sigs[g.sig].name, g.bars, g.swing);
	ui_w_set_text (g_status, (long) t);
}

static void paint_bar (int show)
{
	if (g_bar_w <= 0 || g_bar_h <= 0) return;		/* pas encore placée */
	unsigned line = jet (TJ_LINE), surf = jet (TJ_SURFACE);
	ui_rbox (g_bar_x, g_bar_y, g_bar_w, g_bar_h, 12, line, line);
	ui_rbox (g_bar_x + 1, g_bar_y + 1, g_bar_w - 2, g_bar_h - 2, 11, surf, surf);
	for (int i = 0; i < g_ncaps; i++)
	{
		char t[64];
		const char *s = TR (g_caps[i].text);
		if (g_caps[i].widget == g_swing) { snprintf (t, sizeof t, TR ("Swing  %d %%"), g.swing); s = t; }
		else if (g_caps[i].widget == g_vol) { snprintf (t, sizeof t, TR ("Volume  %d %%"), g.volume); s = t; }
		ui_text ((long) s, g_caps[i].x, g_caps[i].y, jet (TJ_TEXT2), 11, (long) ui.font_b, 0);
	}
	if (show)
	{
		ui_update (g_bar_x, g_bar_y, g_bar_w, g_bar_h);
		for (int i = 0; i < g_ncaps; i++) ui_w_draw (g_caps[i].widget);
		ui_w_draw (g_lcd);
		ui_view_dirty (g_play);
	}
}

/* place les réglages en lignes qui s'enroulent ; rend la hauteur de la barre */
static int layout_bar (int x0, int y0, int w)
{
	int ctl = (int) ui_metric (UMT_CTL);
	struct { long wd; const char *cap; int w; } it[] = {
		{ g_tempo,	"Tempo (BPM)",	116 },
		{ g_swing,	"Swing",	116 },
		{ g_sigsel,	"Signature",	86 },
		{ g_bars,	"Mesures",	104 },
		{ g_kitsel,	"Kit",		150 },
		{ g_presetsel,	"Motif",	136 },
		{ g_vol,	"Volume",	110 },
	};
	int n = (int) (sizeof it / sizeof it[0]);
	int pad = 14, cap_h = 18, row_h = cap_h + ctl + 10, gap = 12;
	int play_d = ctl + 18, lcd_w = 96;
	int left = x0 + pad + play_d + 12 + lcd_w + gap + 4;
	int x = left, y = y0 + pad - 2, right = x0 + w - pad;
	g_ncaps = 0;
	for (int i = 0; i < n; i++)
	{
		if (x + it[i].w > right && x > left) { x = left; y += row_h; }
		ui_w_move (it[i].wd, x, y + cap_h, it[i].w, ctl);
		g_caps[g_ncaps++] = (struct cap) { it[i].wd, it[i].cap, x + 2, y + 12 };
		x += it[i].w + gap;
	}
	int h = (y + row_h) - y0 + pad - 8;
	int mid = y0 + h / 2;
	ui_w_move (g_play, x0 + pad, mid - play_d / 2, play_d, play_d);
	ui_w_move (g_lcd, x0 + pad + play_d + 12, mid - ctl / 2 - 2, lcd_w, ctl + 4);
	return h;
}

/* ---------------------------------------------------------------- fenêtre */

static long on_resize (long w, long h)
{
	int m = 16;
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	/* en-tête : titre et résumé (le coin haut droit reste aux boutons de la fenêtre) */
	ui_text ((long) TR ("Boîte à rythmes"), m + 2, 30, jet (TJ_TEXT1), 20, (long) ui.font_b, 0);
	int tw = (int) ui_text_width ((long) TR ("Boîte à rythmes"), 20, (long) ui.font_b);
	ui_w_move (g_status, m + tw + 18, 14, w - (m + tw + 18) - FRAME_CAP_W - 16, 22);
	g_bar_x = m; g_bar_y = 44; g_bar_w = (int) w - 2 * m;
	g_bar_h = layout_bar (g_bar_x, g_bar_y, g_bar_w);
	int gy = g_bar_y + g_bar_h + 12;
	ui_w_move (g_grid, m, gy, w - 2 * m, h - gy - m);
	paint_bar (0);
	ui_w_draw (0);
	ui_view_dirty (g_grid);
	ui_view_dirty (g_play);
	ui_update_all ();
	return 0;
}

static void update_title (void)
{
	char t[600];
	const char *base = g_path[0] ? strrchr (g_path, '/') : 0;
	base = base ? base + 1 : g_path[0] ? g_path : TR ("Sans titre");
	snprintf (t, sizeof t, "%s%s — %s", g_modified ? "• " : "", base, TR ("Boîte à rythmes"));
	ui_title ((long) t);
}

/* recopie le rythme dans les réglages (après un motif, une ouverture) */
static void sync_widgets (void)
{
	ui_w_set_value (g_tempo, g.tempo);
	ui_w_set_value (g_swing, g.swing);
	ui_w_set_value (g_sigsel, g.sig);
	ui_w_set_value (g_bars, g.bars);
	ui_w_set_value (g_kitsel, kit_index (g.kit));
	ui_w_set_value (g_vol, g.volume);
	send_kit ();
	apply_volume ();
	set_status ();
	paint_bar (1);
	ui_view_dirty (g_grid);
}

static void toggle_play (void)
{
	pthread_mutex_lock (&g_lock);
	if (g_playing) g_playing = 0;
	else { g_start_req = 1; g_playing = 1; }
	pthread_mutex_unlock (&g_lock);
	if (!g_playing) { g_heard = -1; set_lcd (-1); }
	else g_follow = 1;
	ui_menu_state (20, g_playing ? UMS_CHECKED : 0);
	ui_view_dirty (g_play);
	ui_view_dirty (g_grid);
}

static void set_tempo (int t)
{
	g.tempo = t < 40 ? 40 : t > 240 ? 240 : t;
	ui_w_set_value (g_tempo, g.tempo);
	set_status ();
	touch ();
}

/* ---------------------------------------------------------------- commandes */

enum {
	M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_EXPORT,
	M_CLEAR = 10, M_RANDOM, M_LEFT, M_RIGHT, M_DUP, M_CLEARBAR,
	M_PLAY = 20, M_FASTER, M_SLOWER, M_PREVBAR, M_NEXTBAR,
	M_VIEW_ALL = 41, M_VIEW_COMPACT, M_UNFOLD, M_FOLD,
	M_ABOUT = 50,
};

static const char g_menu_text[] =
	"MFichier\n"
	"I1\tNouveau\tCtrl+N\n"
	"I2\tOuvrir…\tCtrl+O\n"
	"-\n"
	"I3\tEnregistrer\tCtrl+S\n"
	"I4\tEnregistrer sous…\tCtrl+Maj+S\n"
	"-\n"
	"I5\tExporter en MIDI…\tCtrl+E\n"
	"MÉdition\n"
	"I10\tEffacer tout le rythme\n"
	"I15\tEffacer la mesure\n"
	"I14\tRecopier la mesure dans les suivantes\tCtrl+D\n"
	"-\n"
	"I11\tRythme au hasard\tCtrl+R\n"
	"-\n"
	"I12\tDécaler à gauche\tCtrl+Gauche\n"
	"I13\tDécaler à droite\tCtrl+Droite\n"
	"MPrésentation\n"
	"I41c\tTous les instruments du kit\n"
	"I42\tVue compacte\n"
	"-\n"
	"I43\tDéplier toutes les familles\n"
	"I44\tReplier toutes les familles\n"
	"MLecture\n"
	"I20\tLecture / Arrêt\tEspace\n"
	"-\n"
	"I21\tPlus vite\tCtrl+Haut\n"
	"I22\tPlus lent\tCtrl+Bas\n"
	"-\n"
	"I23\tMesure précédente\tPage préc.\n"
	"I24\tMesure suivante\tPage suiv.\n"
	"M*\n"
	"I50\tÀ propos de Boîte à rythmes\n";

static int cmd_save_as (void);

/* dossier proposé par les dialogues : celui du fichier, sinon les modèles, sinon le dossier personnel */
static const char *start_dir (void)
{
	static char d[512];
	if (g_path[0])
	{
		snprintf (d, sizeof d, "%s", g_path);
		char *sl = strrchr (d, '/');
		if (sl && sl != d) { *sl = 0; return d; }
	}
	if (access ("/home/public/DrumMachine", F_OK) == 0) return "/home/public/DrumMachine";
	return "HOME:/";
}

static int cmd_save (void)
{
	if (!g_path[0]) return cmd_save_as ();
	if (!save_file (g_path))
	{
		ui_message ((long) TR ("Boîte à rythmes"), (long) TR ("Impossible d'enregistrer le fichier."), UM_OK);
		return 0;
	}
	g_modified = 0;
	update_title ();
	return 1;
}

static int cmd_save_as (void)
{
	char p[512];
	ui_file_filter ((long) "Rythmes|*.drum");
	if (!ui_file_save ((long) start_dir (), (long) (g_path[0] ? strrchr (g_path, '/') + 1 : "rythme.drum"), (long) p, sizeof p))
		return 0;
	snprintf (g_path, sizeof g_path, "%s", p);
	return cmd_save ();
}

/* 1 : on peut continuer (enregistré, ou abandon voulu) */
static int confirm_discard (void)
{
	if (!g_modified) return 1;
	long r = ui_message ((long) TR ("Boîte à rythmes"), (long) TR ("Enregistrer les changements du rythme ?"), UM_YESNOCANCEL);
	if (r == 1) return cmd_save ();
	return r == 2;
}

static void open_path (const char *p)
{
	if (!load_file (p))
	{
		ui_message ((long) TR ("Boîte à rythmes"), (long) TR ("Ce fichier n'est pas un rythme lisible."), UM_OK);
		return;
	}
	snprintf (g_path, sizeof g_path, "%s", p);
	g_modified = 0;
	ui_w_set_value (g_presetsel, -1);
	sync_widgets ();
	update_title ();
}

static void cmd_export (void)
{
	char rep[16] = "4";
	if (!ui_ask ((long) TR ("Exporter en MIDI"), (long) TR ("Combien de fois répéter le rythme ?"), (long) rep, sizeof rep))
		return;
	int n = atoi (rep);
	if (n < 1) n = 1;
	if (n > 256) n = 256;
	char name[128], p[512];
	const char *base = g_path[0] ? strrchr (g_path, '/') + 1 : "rythme.drum";
	snprintf (name, sizeof name, "%s", base);
	char *dot = strrchr (name, '.');
	if (dot) *dot = 0;
	strncat (name, ".mid", sizeof name - strlen (name) - 1);
	ui_file_filter ((long) "MIDI|*.mid");
	if (!ui_file_save ((long) start_dir (), (long) name, (long) p, sizeof p)) return;
	if (!export_midi (p, n))
		ui_message ((long) TR ("Exporter en MIDI"), (long) TR ("Impossible d'écrire le fichier MIDI."), UM_OK);
}

static int command (int id)
{
	switch (id)
	{
	case M_NEW:
		if (!confirm_discard ()) break;
		song_reset ();
		g_path[0] = 0;
		g_modified = 0;
		ui_w_set_value (g_presetsel, -1);
		sync_widgets ();
		update_title ();
		break;
	case M_OPEN:
	{
		if (!confirm_discard ()) break;
		char p[512];
		ui_file_filter ((long) "Rythmes|*.drum|Tous|*");
		if (ui_file_open ((long) start_dir (), (long) p, sizeof p)) open_path (p);
		break;
	}
	case M_SAVE: cmd_save (); break;
	case M_SAVEAS: cmd_save_as (); break;
	case M_EXPORT: cmd_export (); break;
	case M_CLEAR: clear_all (); touch (); break;
	case M_CLEARBAR: clear_bar (g_page); break;
	case M_DUP: dup_bar (g_page); break;
	case M_RANDOM: random_pattern (); break;
	case M_LEFT: shift_pattern (-1); break;
	case M_RIGHT: shift_pattern (1); break;
	case M_PLAY: toggle_play (); break;
	case M_FASTER: set_tempo (g.tempo + 5); break;
	case M_SLOWER: set_tempo (g.tempo - 5); break;
	case M_PREVBAR: g_follow = 0; set_page (g_page - 1); break;
	case M_NEXTBAR: g_follow = 0; set_page (g_page + 1); break;
	case M_VIEW_ALL: set_compact (0); break;
	case M_VIEW_COMPACT: set_compact (1); break;
	case M_UNFOLD: case M_FOLD:
		memset (g_folded, id == M_FOLD, sizeof g_folded);
		g_scroll = 0;
		ui_view_dirty (g_grid);
		break;
	case M_ABOUT:
		ui_message ((long) TR ("Boîte à rythmes"),
			    (long) TR ("Boîte à rythmes 1.0\n\nUn séquenceur de percussions : les 61 instruments du kit, 1 à 8 mesures. "
				       "Ses notes partent en MIDI sur le canal 10 vers le synthé General MIDI "
				       "d'AudioKit ; un rythme s'exporte en fichier MIDI."), UM_OK);
		break;
	case -2:	/* fermeture */
		return confirm_discard ();
	}
	return 0;
}

static void on_widget (long w, long code)
{
	if (code != UN_CHANGE && code != UN_ACTIVATE) return;
	if (w == g_tempo) { g.tempo = (int) ui_w_get_value (g_tempo); set_status (); touch (); }
	else if (w == g_swing) { g.swing = (int) ui_w_get_value (g_swing); set_status (); paint_bar (1); touch (); }
	else if (w == g_vol) { g.volume = (int) ui_w_get_value (g_vol); apply_volume (); paint_bar (1); touch (); }
	else if (w == g_sigsel)
	{
		set_signature ((int) ui_w_get_value (g_sigsel));
		set_status ();
		touch ();
	}
	else if (w == g_bars)
	{
		set_bars ((int) ui_w_get_value (g_bars));
		set_status ();
		touch ();
	}
	else if (w == g_kitsel)
	{
		long i = ui_w_get_value (g_kitsel);
		if (i >= 0 && i < g_nkits)
		{
			g.kit = g_kit_prog[i];
			send_kit ();
			set_status ();
			touch ();
			if (!g_playing) preview_note (N_KICK, VEL_NORMAL);
		}
	}
	else if (w == g_presetsel)
	{
		long i = ui_w_get_value (g_presetsel);
		if (i >= 0 && i < N_PRESETS) load_preset ((int) i);
	}
}

/* ---------------------------------------------------------------- boucle */

static void tick_playhead (void)
{
	int s = heard_step ();
	if (s == g_heard) return;
	g_heard = s;
	if (s >= 0)
	{
		unsigned now = now_ms ();
		int solo = any_solo ();
		for (int i = 0; i < g.ntracks; i++)
			if (g.t[i].cell[s] && audible (&g.t[i], solo)) g_hit_ms[i] = now ? now : 1;
		if (g_follow && s / spb () != g_page) g_page = s / spb ();
	}
	set_lcd (s);
	ui_view_dirty (g_grid);
}

static int fading (void)
{
	unsigned now = now_ms ();
	for (int i = 0; i < g.ntracks; i++)
		if (g_hit_ms[i] && now - g_hit_ms[i] < FADE_MS + 20) return 1;
	return 0;
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_tokens ();
	char lang[64];
	snprintf (lang, sizeof lang, "/apps/drums.app/lang-%s.txt", (const char *) ui_lang ());
	ui_tr_load ((long) lang);
	srand (now_ms ());

	if (ui_window_ex (-1, -1, 1240, 760, (long) TR ("Boîte à rythmes"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (760, 520);
	int sound = audio_open ();
	load_kits ();
	song_reset ();

	/* les réglages */
	g_status = ui_label (0, 0, 10, 10, (long) "", ULB_DIM);
	g_play = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_play, (long) play_paint, (long) play_mouse, 0, 0);
	ui_w_tip (g_play, (long) TR ("Lecture / Arrêt (Espace)"));
	g_lcd = ui_lcd (0, 0, 10, 10, (long) "-.-.-");
	ui_w_tip (g_lcd, (long) TR ("Mesure, temps et pas joués"));
	g_tempo = ui_numeric (0, 0, 10, 10, 40, 240, g.tempo);
	g_swing = ui_slider (0, 0, 10, 10, 0, 50, g.swing);
	ui_w_tip (g_swing, (long) TR ("Retarde les contretemps pour un jeu plus souple"));
	char sigs[64] = "";
	for (int i = 0; i < N_SIGS; i++)
	{
		if (i) strcat (sigs, "|");
		strcat (sigs, g_sigs[i].name);
	}
	g_sigsel = ui_dropdown (0, 0, 10, 10, (long) sigs);
	ui_w_tip (g_sigsel, (long) TR ("Signature : temps par mesure"));
	g_bars = ui_numeric (0, 0, 10, 10, 1, MAX_BARS, g.bars);
	ui_w_tip (g_bars, (long) TR ("Nombre de mesures du rythme"));
	char kits[MAX_KITS * 26] = "";
	for (int i = 0; i < g_nkits; i++)
	{
		if (i) strcat (kits, "|");
		strcat (kits, g_kit_names[i]);
	}
	g_kitsel = ui_dropdown (0, 0, 10, 10, (long) kits);
	ui_w_tip (g_kitsel, (long) TR ("Kit de batterie de la SoundFont (programme MIDI du canal 10)"));
	char presets[512] = "";
	for (int i = 0; i < N_PRESETS; i++)
	{
		if (i) strcat (presets, "|");
		strcat (presets, TR (g_presets[i].name));
	}
	g_presetsel = ui_dropdown (0, 0, 10, 10, (long) presets);
	ui_w_tip (g_presetsel, (long) TR ("Charger un rythme tout fait"));
	g_vol = ui_slider (0, 0, 10, 10, 0, 100, g.volume);
	g_grid = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g_grid, (long) grid_paint, (long) grid_mouse, (long) grid_key, (long) grid_wheel);
	ui_w_tip (g_grid, (long) TR ("Clic : une note · clic droit ou Maj+clic : un accent · glisser pour peindre · clic sur un nom : l'écouter · clic droit : son menu"));

	ui_menu ((long) g_menu_text);
	ui_on_resize ((long) on_resize, 0);

	if (argc > 1) open_path (argv[1]);
	else { load_preset (0); ui_w_set_value (g_presetsel, 0); g_modified = 0; }
	sync_widgets ();
	on_resize (ui.w, ui.h);
	update_title ();
	if (!sound)
		ui_message ((long) TR ("Boîte à rythmes"), (long) TR ("Le son n'a pas pu s'ouvrir : le rythme se compose mais ne s'entend pas."), UM_OK);

	for (;;)
	{
		long t = ui_event (g_playing || fading () ? 12 : 200);
		if (t == WEV_CLOSE) { if (command (-2)) break; continue; }
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_THEME) { s_jet = (const unsigned *) ui_tokens (); on_resize (ui.w, ui.h); }
		else if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) on_widget (w, code);
			if (t == WEV_KEY && !taken && ui_ev_field (&ui, EV_KIND) == 1)
			{
				long id = ui_menu_key (0);
				int k = ui_ev_field (&ui, EV_CODE), mods = ui_ev_field (&ui, EV_MODS);
				if (id >= 0) command ((int) id);
				else if (k == 0x2C || ui_ev_field (&ui, EV_CHAR) == ' ') toggle_play ();	/* Espace */
				else if (k == KEY_PGUP) command (M_PREVBAR);
				else if (k == KEY_PGDN) command (M_NEXTBAR);
				else if ((mods & UI_MOD_CTRL) && k == KEY_UP) command (M_FASTER);
				else if ((mods & UI_MOD_CTRL) && k == KEY_DOWN) command (M_SLOWER);
				else if ((mods & UI_MOD_CTRL) && k == KEY_LEFT) command (M_LEFT);
				else if ((mods & UI_MOD_CTRL) && k == KEY_RIGHT) command (M_RIGHT);
			}
		}
		else ui_w_idle ();
		tick_playhead ();
		if (fading ()) ui_view_dirty (g_grid);
		ui_view_flush ();
	}
	audio_close ();
	return 0;
}
