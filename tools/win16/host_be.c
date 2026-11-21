/* host_be.c -- win16 sur l'hôte (WSL), pour les essais sans Korin : l'affichage de be.h en mémoire.
 *
 * Les fenêtres sont des tampons de pixels ; le texte est tracé en blocs (une case par caractère) et noté sur
 * stderr. Les événements viennent de W16_SCRIPT (« menu:ID », « click:X,Y », « down:X,Y », « move:X,Y », « up:X,Y », « key:CODE,CHAR », « keyup:CODE », « close »,
 * « wait:N », séparés par des espaces ; « win:N » choisit la fenêtre) ; script fini, quand le programme attend
 * sans rien à faire, la dernière fenêtre ouverte reçoit « fermer » (après W16_IDLE attentes, 3 par défaut).
 * Chaque fenêtre est écrite en PPM dans W16_SHOT (dossier) à sa fermeture, et sur « shot ».
 * Construit par tools/win16/host.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "be.h"

#define MAX_WIN 32
struct hwin { int used, w, h; uint32_t *px; char title[160]; };
static struct hwin wins[MAX_WIN];
static int idle, idle_max = 3, shots;
static char *script, *spos, *script_end;
static long script_win;

int be_init (void)
{
	if (getenv ("W16_IDLE"))
		idle_max = atoi (getenv ("W16_IDLE"));
	if (getenv ("W16_SCRIPT"))
	{
		script = strdup (getenv ("W16_SCRIPT"));
		spos = script;
		script_end = script + strlen (script);
	}
	return 1;
}

static long last_open (void)
{
	for (int i = MAX_WIN - 1; i >= 0; i--)
		if (wins[i].used)
			return i + 1;
	return 0;
}

long be_window_open (int x, int y, int w, int h, const char *title)
{
	(void) x; (void) y;
	for (int i = 0; i < MAX_WIN; i++)
		if (!wins[i].used)
		{
			wins[i].used = 1;
			wins[i].w = w;
			wins[i].h = h;
			wins[i].px = calloc ((size_t) w * (size_t) h, 4);
			for (int k = 0; k < w * h; k++)
				wins[i].px[k] = 0x808080;
			snprintf (wins[i].title, sizeof wins[i].title, "%s", title);
			fprintf (stderr, "[hôte] fenêtre %d « %s » %d×%d\n", i + 1, title, w, h);
			return i + 1;
		}
	return 0;
}

static struct hwin *get (long h) { return h >= 1 && h <= MAX_WIN && wins[h - 1].used ? &wins[h - 1] : 0; }

static void shot (struct hwin *w, long h)
{
	const char *dir = getenv ("W16_SHOT");
	if (!dir)
		return;
	char path[512];
	snprintf (path, sizeof path, "%s/win%ld-%d.ppm", dir, h, shots++);
	FILE *f = fopen (path, "wb");
	if (!f)
		return;
	fprintf (f, "P6\n%d %d\n255\n", w->w, w->h);
	for (int k = 0; k < w->w * w->h; k++)
	{
		uint32_t p = w->px[k];
		fputc ((int) (p >> 16) & 0xff, f);
		fputc ((int) (p >> 8) & 0xff, f);
		fputc ((int) p & 0xff, f);
	}
	fclose (f);
	fprintf (stderr, "[hôte] image : %s\n", path);
}

void be_window_close (long h)
{
	struct hwin *w = get (h);
	if (!w)
		return;
	shot (w, h);
	free (w->px);
	w->used = 0;
	fprintf (stderr, "[hôte] fenêtre %ld fermée\n", h);
}

void be_window_title (long h, const char *t) { struct hwin *w = get (h); if (w) snprintf (w->title, sizeof w->title, "%s", t); }

void be_window_resize (long h, int ww, int hh)
{
	struct hwin *w = get (h);
	if (!w)
		return;
	uint32_t *n = calloc ((size_t) ww * (size_t) hh, 4);
	for (int y = 0; y < hh && y < w->h; y++)
		memcpy (n + y * ww, w->px + y * w->w, (size_t) (ww < w->w ? ww : w->w) * 4);
	free (w->px);
	w->px = n;
	w->w = ww;
	w->h = hh;
}

uint32_t *be_pixels (long h, int *w, int *hh, int *stride)
{
	struct hwin *x = get (h);
	if (!x)
		return 0;
	*w = x->w;
	*hh = x->h;
	*stride = x->w;
	return x->px;
}

void be_present (long h, int x, int y, int w, int hh) { (void) h; (void) x; (void) y; (void) w; (void) hh; }

static int cw (int px) { return px > 0 ? (px * 7 + 6) / 13 : 7; }
int be_text_width (const char *s, int px, int style) { (void) style; return cw (px) * (int) strlen (s); }
int be_font_height (int px) { return ((px > 0 ? px : 13) * 5 + 3) / 4; }
int be_font_ascent (int px) { return ((px > 0 ? px : 13) * 15 + 8) / 16; }

int be_text_buf (uint32_t *dst, int dw, int dh, int x, int y, const char *s, uint32_t color, int cx, int cy, int ccw,
	int ch, int px, int style)
{
	int k = cw (px), hgt = be_font_height (px);
	for (int i = 0; s[i]; i++)
	{
		if (s[i] == ' ')
			continue;
		for (int yy = y + hgt / 5; yy < y + hgt - hgt / 5; yy++)
			for (int xx = x + i * k + 1; xx < x + i * k + k - 1; xx++)
				if (xx >= cx && yy >= cy && xx < cx + ccw && yy < cy + ch && xx >= 0 && yy >= 0 && xx < dw && yy < dh)
					dst[yy * dw + xx] = color;
	}
	return be_text_width (s, px, style);
}

int be_text (long h, int x, int y, const char *s, uint32_t color, int cx, int cy, int ccw, int ch, int px, int style)
{
	struct hwin *w = get (h);
	fprintf (stderr, "[hôte] texte (%d, %d) : %s\n", x, y, s);
	if (!w)
		return 0;
	return be_text_buf (w->px, w->w, w->h, x, y, s, color, cx, cy, ccw, ch, px, style);
}

uint32_t be_theme (int what)
{
	static const uint32_t t[] = { 0xf4f6f8, 0x1a1c20, 0x9aa0a8, 0x3d7bd9, 0xffffff, 0xe6e9ee, 0xd0d4da, 0xffffff,
		0x8a9099, 0xeef1f4, 0xd6e4fa, 0xe9eef6 };
	return t[what >= 0 && what < 12 ? what : 0];
}

void be_menu_set (long win, const char *text)
{
	fprintf (stderr, "[hôte] menus de la fenêtre %ld :\n", win);
	for (const char *p = text; p && *p;)
	{
		const char *nl = strchr (p, '\n');
		int n = nl ? (int) (nl - p) : (int) strlen (p);
		fprintf (stderr, "[hôte]   %.*s\n", n, p);
		p = nl ? nl + 1 : p + n;
	}
}

void be_menu_state (long win, int id, int state) { fprintf (stderr, "[hôte] menu %ld, entrée %d : état %d\n", win, id, state); }

int be_popup (long win, int x, int y, const char *labels, int w)
{
	(void) w;
	fprintf (stderr, "[hôte] menu surgissant (fenêtre %ld, %d, %d) :", win, x, y);
	for (const char *p = labels; *p; p += strlen (p) + 1)
		fprintf (stderr, " [%s]", p);
	fprintf (stderr, "\n");
	const char *e = getenv ("W16_POPUP");
	return e ? atoi (e) : -1;
}

/* le prochain mot du script ; 0 quand il est fini */
static char *next_word (void)
{
	if (!spos)
		return 0;
	/* (un « type: » rejoué laisse des 0 entre les mots : on les saute aussi) */
	while (spos < script_end && (*spos == ' ' || !*spos))
		spos++;
	if (spos >= script_end)
		return 0;
	char *w = spos;
	while (*spos && *spos != ' ')
		spos++;
	if (*spos)
		*spos++ = 0;
	return w;
}

static int script_wait;

int be_wait (int ms, struct be_event *ev)
{
	memset (ev, 0, sizeof *ev);
	/* (W16_REALTIME : une attente dure vraiment, au plus 20 ms ; pour les essais du son, au rythme réel) */
	static int realtime = -1;
	if (realtime < 0)
		realtime = getenv ("W16_REALTIME") != 0;
	if (realtime && ms > 0)
	{
		struct timespec ts = { 0, (long) (ms < 20 ? ms : 20) * 1000000L };
		nanosleep (&ts, 0);
	}
	if (script_wait > 0)
	{
		/* (une attente par appel qui attend ; un programme qui ne fait que sonder, comme l'éditeur de QuickBASIC,
		 * avance aussi : une toutes les 100 ms) */
		static uint32_t last;
		uint32_t now = be_ticks ();
		if (ms || now - last >= 100)
		{
			script_wait--;
			last = now;
		}
		return 0;
	}
	char *w;
	while ((w = next_word ()))
	{
		long win = script_win ? script_win : last_open ();
		ev->win = win;
		fprintf (stderr, "[hôte] script : %s\n", w);
		if (!strncmp (w, "menu:", 5))
		{
			ev->type = BE_MENU;
			ev->key = atoi (w + 5);
			return 1;
		}
		if (!strncmp (w, "click:", 6))
		{
			ev->type = BE_MOUSE_DOWN;
			ev->button = 1;
			sscanf (w + 6, "%d,%d", &ev->x, &ev->y);
			/* (le relâchement suit au prochain appel) */
			static char up[64];
			snprintf (up, sizeof up, "up:%d,%d", ev->x, ev->y);
			size_t l = strlen (up);
			if (spos >= script + l + 1)
			{
				spos -= l + 1;
				memcpy (spos, up, l);
				spos[l] = ' ';
			}
			return 1;
		}
		if (!strncmp (w, "down:", 5))	/* (un glisser : down:X,Y move:X,Y up:X,Y) */
		{
			ev->type = BE_MOUSE_DOWN;
			ev->button = 1;
			sscanf (w + 5, "%d,%d", &ev->x, &ev->y);
			return 1;
		}
		if (!strncmp (w, "move:", 5))
		{
			ev->type = BE_MOUSE_MOVE;
			ev->button = 1;
			sscanf (w + 5, "%d,%d", &ev->x, &ev->y);
			return 1;
		}
		if (!strncmp (w, "up:", 3))
		{
			ev->type = BE_MOUSE_UP;
			sscanf (w + 3, "%d,%d", &ev->x, &ev->y);
			return 1;
		}
		if (!strncmp (w, "keyup:", 6))
		{
			ev->type = BE_KEY_UP;
			ev->key = atoi (w + 6);
			return 1;
		}
		if (!strncmp (w, "key:", 4))
		{
			ev->type = BE_KEY_DOWN;
			sscanf (w + 4, "%d,%d", &ev->key, &ev->ch);
			return 1;
		}
		if (!strncmp (w, "type:", 5))
		{
			/* (un caractère par appel : on rejoue le reste du mot) */
			char *t = w + 5;
			ev->type = BE_KEY_DOWN;
			ev->ch = (unsigned char) t[0];
			ev->key = 0x04 + (t[0] >= 'a' && t[0] <= 'z' ? t[0] - 'a' : 0);
			if (t[1])
			{
				t[0] = ':';
				memcpy (t - 4, "type", 4);
				spos = t - 4;
				if (spos > script)
					spos[-1] = ' ';
			}
			return 1;
		}
		if (!strcmp (w, "close"))
		{
			ev->type = BE_CLOSE;
			return 1;
		}
		if (!strncmp (w, "win:", 4))
		{
			script_win = atol (w + 4);
			continue;
		}
		if (!strncmp (w, "wait:", 5))
		{
			script_wait = atoi (w + 5);
			return 0;
		}
		if (!strcmp (w, "shot"))
		{
			struct hwin *x = get (win);
			if (x)
				shot (x, win);
			continue;
		}
	}
	if (ms == 0)
	{
		/* (un programme qui ne fait que sonder sans jamais attendre : une attente par seconde) */
		static uint32_t last_idle;
		uint32_t now = be_ticks ();
		if (now - last_idle < 1000)
			return 0;
		last_idle = now;
	}
	if (++idle < idle_max)
		return 0;
	idle = 0;
	long win = last_open ();
	static int closes;
	if (win && ++closes > 6)
	{
		fprintf (stderr, "[hôte] le programme ne se ferme pas : arrêt\n");
		exit (0);
	}
	if (win)
	{
		shot (get (win), win);	/* (l'état avant « fermer ») */
		fprintf (stderr, "[hôte] « fermer » envoyé à la fenêtre %ld\n", win);
		ev->type = BE_CLOSE;
		ev->win = win;
		return 1;
	}
	return 0;
}

uint32_t be_ticks (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (uint32_t) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

int be_message (const char *title, const char *text, int type)
{
	fprintf (stderr, "[hôte] boîte de message « %s » : %s (type %d)\n", title, text, type);
	return 1;
}

int be_file_open (const char *dir, const char *filter, char *out, int cap)
{
	const char *e = getenv ("W16_OPEN");
	fprintf (stderr, "[hôte] ouvrir (%s, %s) -> %s\n", dir, filter ? filter : "*", e ? e : "annulé");
	if (!e)
		return 0;
	snprintf (out, (size_t) cap, "%s", e);
	return 1;
}

int be_file_save (const char *dir, const char *name, const char *filter, char *out, int cap)
{
	const char *e = getenv ("W16_SAVE");
	fprintf (stderr, "[hôte] enregistrer (%s, %s, %s) -> %s\n", dir, name, filter ? filter : "*", e ? e : "annulé");
	if (!e)
		return 0;
	snprintf (out, (size_t) cap, "%s", e);
	return 1;
}

/* (W16_ASK : la réponse ; sans elle, annulé) */
int be_ask (const char *title, const char *prompt, char *buf, int cap)
{
	const char *e = getenv ("W16_ASK");
	fprintf (stderr, "[hôte] question « %s » : %s -> %s\n", title, prompt, e ? e : "annulé");
	if (!e)
		return 0;
	snprintf (buf, (size_t) cap, "%s", e);
	return 1;
}

long be_color (const char *title, uint32_t rgb) { fprintf (stderr, "[hôte] couleur (%s)\n", title); return (long) rgb; }

static char clip[8192];
void be_clip_put (const char *s) { snprintf (clip, sizeof clip, "%s", s); }
int be_clip_take (char *buf, int cap) { snprintf (buf, (size_t) cap, "%s", clip); return (int) strlen (clip); }

void be_fatal (const char *msg) { (void) msg; }

/* les widgets : pas sur l'hôte (win16 dessine alors ses contrôles lui-même) */
int be_ui_metric (int what) { (void) what; return 0; }
long be_w_create (long win, int kind, int x, int y, int w, int h, const char *text, long extra)
{ (void) win; (void) kind; (void) x; (void) y; (void) w; (void) h; (void) text; (void) extra; return 0; }
void be_w_move (long wd, int x, int y, int w, int h) { (void) wd; (void) x; (void) y; (void) w; (void) h; }
void be_w_show (long wd, int s) { (void) wd; (void) s; }
void be_w_enable (long wd, int on) { (void) wd; (void) on; }
void be_w_focus (long wd) { (void) wd; }
void be_w_set_text (long wd, const char *t) { (void) wd; (void) t; }
const char *be_w_get_text (long wd) { (void) wd; return ""; }
void be_w_set_value (long wd, long v) { (void) wd; (void) v; }
long be_w_get_value (long wd) { (void) wd; return 0; }
void be_w_set_range (long wd, long a, long b, long c, long d) { (void) wd; (void) a; (void) b; (void) c; (void) d; }
void be_w_list_add (long wd, const char *t) { (void) wd; (void) t; }
void be_w_list_clear (long wd) { (void) wd; }
void be_w_draw (long wd) { (void) wd; }
void be_w_destroy (long wd) { (void) wd; }

/* le synthé : sur l'hôte, les messages sont seulement comptés et notés (W16_MIDI=1) */
static long midi_count;
int be_synth_start (void) { return 1; }
void be_synth_midi (int ch, int cmd, int a, int b)
{
	midi_count++;
	if (getenv ("W16_MIDI"))
		fprintf (stderr, "[hôte] MIDI %ld : canal %d, %02x %d %d\n", midi_count, ch, cmd, a, b);
}
void be_synth_reset (void) { if (getenv ("W16_MIDI")) fprintf (stderr, "[hôte] MIDI : tout se tait\n"); }

/* la sortie PCM sur l'hôte : un fil au rythme réel ; W16_WAV=fichier.wav l'enregistre (44,1 kHz, stéréo, 16 bits) */
#include <pthread.h>
#include <unistd.h>
static void (*pcm_fill) (int16_t *buf, int frames);

static void *pcm_main (void *arg)
{
	(void) arg;
	const char *path = getenv ("W16_WAV");
	FILE *f = path ? fopen (path, "wb") : 0;
	uint32_t total = 0;
	static int16_t buf[256 * 2];
	if (f)
		fwrite ("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x44\xac\0\0\x10\xb1\x02\0\x04\0\x10\0data\0\0\0\0", 1, 44, f);
	for (;;)
	{
		pcm_fill (buf, 256);
		if (f)
		{
			fwrite (buf, 1, sizeof buf, f);
			total += sizeof buf;
			long here = ftell (f);
			uint32_t riff = total + 36;
			fseek (f, 4, SEEK_SET);
			fwrite (&riff, 4, 1, f);
			fseek (f, 40, SEEK_SET);
			fwrite (&total, 4, 1, f);
			fseek (f, here, SEEK_SET);
			fflush (f);
		}
		usleep (256 * 1000000 / 44100);
	}
	return 0;
}

int be_pcm_start (void (*fill) (int16_t *buf, int frames))
{
	static int started;
	pthread_t t;
	if (started)
		return 1;
	pcm_fill = fill;
	started = pthread_create (&t, 0, pcm_main, 0) == 0;
	return started;
}
