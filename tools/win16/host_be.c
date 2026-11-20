/* host_be.c -- win16 sur l'hôte (WSL), pour les essais sans Korin : l'affichage de be.h en mémoire.
 *
 * Les fenêtres sont des tampons de pixels ; le texte est tracé en blocs (une case par caractère) et noté sur
 * stderr. Quand le programme attend sans rien à faire, la première fenêtre reçoit « fermer » (après W16_IDLE
 * attentes, 3 par défaut) ; chaque fenêtre est écrite en PPM dans W16_SHOT (dossier) à sa fermeture.
 * Construit par tools/win16/host.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "be.h"

#define MAX_WIN 16
struct hwin { int used, w, h; uint32_t *px; char title[160]; int shot; };
static struct hwin wins[MAX_WIN];
static int idle, idle_max = 3, shots;

int be_init (void)
{
	if (getenv ("W16_IDLE"))
		idle_max = atoi (getenv ("W16_IDLE"));
	return 1;
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
void be_window_resize (long h, int ww, int hh) { (void) h; (void) ww; (void) hh; }

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

int be_text_width (const char *s) { return 7 * (int) strlen (s); }
int be_font_height (void) { return 16; }
int be_font_ascent (void) { return 12; }

int be_text (long h, int x, int y, const char *s, uint32_t color, int cx, int cy, int cw, int ch)
{
	struct hwin *w = get (h);
	fprintf (stderr, "[hôte] texte (%d, %d) : %s\n", x, y, s);
	if (!w)
		return 0;
	for (int i = 0; s[i]; i++)
	{
		if (s[i] == ' ')
			continue;
		for (int yy = y + 3; yy < y + 13; yy++)
			for (int xx = x + i * 7 + 1; xx < x + i * 7 + 6; xx++)
				if (xx >= cx && yy >= cy && xx < cx + cw && yy < cy + ch && xx < w->w && yy < w->h)
					w->px[yy * w->w + xx] = color;
	}
	return be_text_width (s);
}

uint32_t be_theme (int what)
{
	static const uint32_t t[] = { 0xf4f6f8, 0x1a1c20, 0x9aa0a8, 0x3d7bd9 };
	return t[what & 3];
}

int be_wait (int ms, struct be_event *ev)
{
	memset (ev, 0, sizeof *ev);
	if (ms == 0)
		return 0;
	if (++idle < idle_max)
		return 0;
	idle = 0;
	for (int i = 0; i < MAX_WIN; i++)
		if (wins[i].used)
		{
			fprintf (stderr, "[hôte] « fermer » envoyé à la fenêtre %d\n", i + 1);
			ev->type = BE_CLOSE;
			ev->win = i + 1;
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

void be_fatal (const char *msg) { (void) msg; }
