//
// compose.h -- la composition d'Arabesque : chaque partie suit son propre chemin sur l'image, iteration apres iteration
// du trace. Au point atteint, la couleur du pixel donne la note : une composante choisit la hauteur parmi les notes de la
// gamme dans la tessiture, une autre la duree, une autre le volume, une autre l'articulation.
//
// Toutes les parties partent de la meme graine : a phase egale, elles suivent le meme chemin, chacune a son rythme, ce
// qui donne imitations et augmentations. La phase les decale. C'est l'autosimilarite du trace qui donne les motifs.
//
// Un changement de gamme attend le temps suivant, un changement de style la mesure suivante.
//
// Pendant une composition, le journal du prereglage (rec, recipe.h) note ce qui change : la gamme et le style demandes,
// une partie effacee, et, au debut de chaque bloc, l'etat (style, trace, image) s'il differe du dernier note. Chaque
// changement est date de done, ou en etait le compositeur : le rejouer au meme point redonne les memes notes.
//
#ifndef ARABESQUE_COMPOSE_H
#define ARABESQUE_COMPOSE_H

#include "recipe.h"

enum { TRAIL = 40 };				// les derniers points de chaque partie, pour l'image
struct Voice
{
	double x, y;
	Rng rng;
	uint32_t next;				// pulsation de la prochaine note
	bool first, live;
	int lastKey[4], lastIdx[4];
	float tu[TRAIL], tv[TRAIL];		// le sillage, 0..1
	int thead, tcount;
};

struct Composer
{
	Song *song; Style *style; Ifs *ifs; const Image *img;
	unsigned mask; int root;		// la gamme en cours
	Voice v[NPART];
	// ce qui attend
	bool scalePending, stylePending;
	unsigned nextMask; int nextRoot;
	Style nextStyle;
	uint32_t scaleAt, styleAt;
	uint32_t seed;
	uint32_t done;				// tout ce qui commence avant est compose
	Recipe *rec;				// le journal, pendant une composition
	char *recKey; long recKeyLen;		// le dernier etat note

	Composer () : song (0), style (0), ifs (0), img (0), mask (0xAB5), root (0), scalePending (false), stylePending (false), nextMask (0), nextRoot (0), scaleAt (0), styleAt (0), seed (1),
		done (0), rec (0), recKey (0), recKeyLen (0) {}

	// ---- le journal ----
	// l'etat dont depend la composition, en octets : le style ecrit, le trace au bit pres, le numero de l'image
	long state_key (char *o, long cap) const
	{
		long w = style->format (o, (int) cap - 400);
		memcpy (o + w, &ifs->n, sizeof ifs->n); w += sizeof ifs->n;
		double fr[4] = { ifs->x0, ifs->y0, ifs->x1, ifs->y1 };
		memcpy (o + w, fr, sizeof fr); w += sizeof fr;
		memcpy (o + w, &img->serial, sizeof img->serial); w += sizeof img->serial;
		memcpy (o + w, ifs->t, sizeof (Xform) * (size_t) ifs->n); w += (long) sizeof (Xform) * ifs->n;
		return w;
	}
	void snapshot ()
	{
		rec->state (done, *style, *ifs, *img);
		remember ();
	}
	void remember ()
	{
		static char b[NPART * 300 + 400 + IFS_MAX * sizeof (Xform)];
		recKeyLen = state_key (b, sizeof b);
		recKey = (char *) realloc (recKey, (size_t) recKeyLen);
		memcpy (recKey, b, (size_t) recKeyLen);
	}
	bool state_changed () const
	{
		static char b[NPART * 300 + 400 + IFS_MAX * sizeof (Xform)];
		long n = state_key (b, sizeof b);
		return n != recKeyLen || memcmp (b, recKey, (size_t) n);
	}
	// une partie effacee pendant la composition
	void clear_track (int i)
	{
		song->tr[i].clear ();
		if (rec) rec->add (CH_CLEAR, done).part = i;
	}

	void init_voice (int i, double x, double y, uint32_t tick)
	{
		Voice &q = v[i];
		q.x = x; q.y = y; q.rng = Rng (seed + 1); q.next = tick; q.first = true; q.live = true;
		q.thead = q.tcount = 0;
		for (int k = 0; k < 4; k++) { q.lastKey[k] = -1; q.lastIdx[k] = -1; }
	}
	void settings (int i, uint32_t tick)
	{
		const Part &p = style->p[i];
		Track &t = song->tr[i];
		t.ctl (tick, p.chan, NK_PROGRAM, p.patch, 0);
		t.ctl (tick, p.chan, NK_CC, 7, p.vol);
		t.ctl (tick, p.chan, NK_CC, 10, p.pan);
	}
	// tout depuis le debut : les parties allumees sont effacees, les parties eteintes gardent leurs notes
	void start (uint32_t sd)
	{
		seed = sd ? sd : 1;
		scalePending = stylePending = false;
		done = 0;
		if (rec)
		{
			rec->clear ();
			rec->seed = seed; rec->mask = mask; rec->root = root; rec->length = 0;
			snapshot ();
		}
		Rng r (seed);
		double x = 0, y = 0;
		for (int i = 0; i < 64; i++) ifs->step (x, y, r);
		for (int i = 0; i < NPART; i++)
		{
			v[i].live = false;
			if (!style->p[i].on) continue;
			song->tr[i].clear ();
			for (int k = style->p[i].phase; k > 0; k--) ifs->step (x, y, r);
			init_voice (i, x, y, 0);
			settings (i, 0);
		}
	}
	void change_scale (unsigned m, int rt, uint32_t now)
	{
		nextMask = m; nextRoot = rt; scalePending = true;
		scaleAt = (now / PPQ + 1) * PPQ;
		if (rec) { Change &c = rec->add (CH_SCALE, done); c.now = now; c.mask = m; c.root = rt; }
	}
	void change_style (const Style &s, uint32_t now)
	{
		nextStyle = s; stylePending = true;
		uint32_t bar = PPQ * BEATS;
		styleAt = (now / bar + 1) * bar;
		if (rec) { Change &c = rec->add (CH_STYLE, done); c.now = now; c.style = new Style (s); }
	}
	void apply_style ()
	{
		stylePending = false;
		*style = nextStyle;
		double x = 0, y = 0; bool have = false;
		for (int i = 0; i < NPART; i++) if (v[i].live) { x = v[i].x; y = v[i].y; have = true; break; }
		if (!have) { Rng r (seed); for (int i = 0; i < 64; i++) ifs->step (x, y, r); }
		for (int i = 0; i < NPART; i++)
		{
			const Part &p = style->p[i];
			if (!p.on) { v[i].live = false; continue; }
			if (!v[i].live) init_voice (i, x, y, styleAt);
			else if (v[i].next < styleAt) v[i].next = styleAt;
			v[i].first = true;
			settings (i, styleAt);
		}
	}
	// les touches de la gamme, de 0 a 127
	int scale_keys (const Part &p, int *out) const
	{
		int n = 0;
		for (int k = 0; k < 128; k++)
			if (p.chan == 9 || (mask >> ((k - root + 120) % 12) & 1)) out[n++] = k;
		return n;
	}
	static int pixel_rgb (const Image *im, double u, double w, uint8_t *c)
	{
		int x = (int) (u * (im->w - 1)), y = (int) (w * (im->h - 1));
		uint32_t q = im->px[y * im->w + x];
		c[0] = (uint8_t) (q >> 16); c[1] = (uint8_t) (q >> 8); c[2] = (uint8_t) q;
		return 0;
	}
	void step (int i)
	{
		Voice &q = v[i];
		const Part &p = style->p[i];
		ifs->step (q.x, q.y, q.rng);
		double u, w;
		ifs->to_unit (q.x, q.y, u, w);
		q.tu[q.thead] = (float) u; q.tv[q.thead] = (float) w;
		q.thead = (q.thead + 1) % TRAIL;
		if (q.tcount < TRAIL) q.tcount++;
		uint8_t c[3];
		pixel_rgb (img, u, w, c);
		// la duree
		int coef[24], nc = 0;
		for (const char *s = p.rhythm; *s; s++) if (*s > '0' && *s <= '9') coef[nc++] = *s - '0';
		int cd = c[p.mapD % 3];
		static const int FREE[6] = { 1, 2, 3, 4, 6, 8 };
		int mult = nc ? coef[cd * nc / 256] : FREE[cd * 6 / 256];
		uint32_t dur = (uint32_t) (p.unit * mult * PPQ / STL_PPQ);
		uint32_t at = q.next;
		q.next = at + dur;
		if (q.rng.below (100) >= p.density) { for (int k = 0; k < 4; k++) q.lastKey[k] = -1; return; }
		// la hauteur : une note de la gamme dans la tessiture
		int all[128];
		int na = scale_keys (p, all);
		if (!na) return;
		int lo = 0, hi = na - 1;
		while (lo < na && all[lo] < p.low) lo++;
		while (hi >= 0 && all[hi] > p.high) hi--;
		if (lo > hi) { lo = hi = lo < na ? lo : na - 1; }
		// (la voix du haut reste dans la tessiture, si elle est assez large)
		static const int SHAPE[3][4] = { { 0, 2, 4, 6 }, { 0, 4, 6, 2 }, { 0, 4, 2, 6 } };
		int perOct = 0;
		for (int k = 0; k < 12; k++) if (mask >> k & 1) perOct++;
		int voices = p.voices < 1 ? 1 : p.voices > 4 ? 4 : p.voices, top = 0;
		for (int k = 0; k < voices; k++)
		{
			int d = SHAPE[p.chord % 3][k] + ((p.chord == CHORD_OPEN1 && k == 3) || (p.chord == CHORD_OPEN2 && k >= 2) ? perOct : 0);
			if (d > top) top = d;
		}
		if (hi - top >= lo) hi -= top;
		int span = hi - lo + 1;
		int deg = lo + c[p.mapP % 3] * span / 256;
		if (q.first)
		{
			q.first = false;
			if (p.start)
			{
				int best = lo;
				for (int k = lo; k <= hi; k++) { int a = all[k] - p.start, b = all[best] - p.start; if ((a < 0 ? -a : a) < (b < 0 ? -b : b)) best = k; }
				deg = best;
			}
		}
		// les nuances, l'articulation
		int cv = c[p.mapV % 3];
		int vel = 120 - p.range + cv * p.range / 255;
		vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
		static const int ART[4] = { 50, 75, 90, 100 };
		int pct = p.mapA == ART_FIXED ? 90 : ART[c[(p.mapA - 1) % 3] * 4 / 256];
		uint32_t len = dur * (uint32_t) pct / 100;
		if (len < 1) len = 1;
		// les voix : tierces empilees, serrees ou ouvertes
		Track &t = song->tr[i];
		for (int k = 0; k < voices; k++)
		{
			int d = deg + SHAPE[p.chord % 3][k];
			if (p.chord == CHORD_OPEN1 && k == 3) d += perOct;
			if (p.chord == CHORD_OPEN2 && k >= 2) d += perOct;
			if (d >= na) d = na - 1;
			int key = all[d];
			if (p.tie && q.lastKey[k] == key && q.lastIdx[k] >= 0 && q.lastIdx[k] < t.count && t.n[q.lastIdx[k]].key == key)
			{
				Note &m = t.n[q.lastIdx[k]];
				m.dur = at + len - m.tick;
				continue;
			}
			Note *m = t.add ();
			m->tick = at; m->dur = len; m->key = (uint8_t) key; m->vel = (uint8_t) vel; m->ch = (uint8_t) p.chan; m->kind = NK_NOTE;
			q.lastKey[k] = key; q.lastIdx[k] = t.count - 1;
		}
	}
	void run (uint32_t to)
	{
		// (dans l'ordre du temps entre parties : le trace est partage mais chaque partie a son chemin)
		for (int i = 0; i < NPART; i++)
			while (v[i].live && style->p[i].on && v[i].next < to) step (i);
	}
	// compose tout ce qui commence avant cette pulsation
	void advance (uint32_t to)
	{
		// (la fenetre a change le style, le trace ou l'image depuis le dernier bloc : l'etat est note)
		if (rec && state_changed ()) snapshot ();
		bool styled = false;
		for (;;)
		{
			uint32_t b = to; int what = 0;
			if (scalePending && scaleAt < b) { b = scaleAt; what = 1; }
			if (stylePending && styleAt < b) { b = styleAt; what = 2; }
			run (b);
			if (!what) break;
			if (what == 1) { scalePending = false; mask = nextMask; root = nextRoot; }
			else { apply_style (); styled = true; }
		}
		if (to > done) done = to;
		if (styled && rec) remember ();
	}
};

#endif
