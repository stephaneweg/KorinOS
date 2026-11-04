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
// La grille d'accords (chords.h), si elle en donne un pour la mesure, resserre les hauteurs selon le reglage Harmonie de
// la partie : aux temps, les notes de l'accord, entre les temps la gamme, une note etrangere a l'accord n'y restant que
// si elle vient par degre conjoint (passage, broderie) ; ou l'accord seul ; ou sa basse aux temps ; ou rien (libre).
// Sans accord, rien ne change : un morceau sans grille est compose note pour note comme avant.
//
// Trois regles d'Art Song (lues dans ARTSNG20.EXE) : le motif rythmique se joue dans l'ordre, en boucle, la couleur ne
// choisissant la duree que sans motif ; la velocite va de 127 - VR a 127 ; la hauteur bouge par degres depuis la
// precedente, la couleur donnant le pas (127 garde la note, plus sombre monte, plus clair descend).
//
// Avec une forme (form.h), le morceau se compose section par section : a chaque section nouvelle, les parties repartent
// du debut du chemin, dans l'image de la section et dans sa tonalite, la grille d'accords reprenant a sa premiere mesure,
// avec le style de la lettre s'il en a un ; une section deja venue est recopiee ; la forme finie, plus rien ne se
// compose. Sans forme, rien ne change.
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
	int prev[4];				// les dernieres touches jouees, silences compris (voice leading)
	int chordId, leadPc;			// l'accord en cours (sa mesure de depart), la basse choisie par le voice leading
	int rpos;				// le rang dans le motif rythmique
	int pos, posKey;			// la hauteur : son rang dans la tessiture, sa touche
	int gate;				// le meme rang, d'apres l'image du theme et sans les accords : negatif, la partie se tait
	float tu[TRAIL], tv[TRAIL];		// le sillage, 0..1
	int thead, tcount;
};

struct Composer
{
	Song *song; Style *style; Ifs *ifs; const Image *img;
	const ChordGrid *chords;		// 0 ou vide : pas d'accords
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
	// la forme (0 : sans), prise au depart ; la section en cours : son image, celle des hauteurs (une variation), sa
	// region, sa tonalite, sa premiere mesure, son style a part. copying : la section est une reprise, deja recopiee
	MusicForm *formIn, *form;
	int sec;
	const Image *sImg, *sVar;
	double sU, sV, sZ;
	int sKey, sBar;
	bool copying;
	const Style *sStyle;
	const Style &cur () const { return sStyle ? *sStyle : *style; }

	Composer () : song (0), style (0), ifs (0), img (0), chords (0), mask (0xAB5), root (0), scalePending (false), stylePending (false), nextMask (0), nextRoot (0), scaleAt (0), styleAt (0), seed (1),
		done (0), rec (0), recKey (0), recKeyLen (0), formIn (0), form (0), sec (0), sImg (0), sVar (0), sU (0.5), sV (0.5), sZ (1), sKey (0), sBar (0), copying (false), sStyle (0) {}
	bool ended () const { return form && done >= end (); }
	uint32_t end () const { return form ? form->length (PPQ * BEATS) : 0xFFFFFFFFu; }
	int section () const { return form ? sec : -1; }

	// ---- le journal ----
	// l'etat dont depend la composition, en octets : le style ecrit, le trace au bit pres, le numero de l'image, la grille
	enum { KEY_CAP = NPART * 300 + 400 + IFS_MAX * sizeof (Xform) + 8 + CHORDS_MAX * 8 * sizeof (int) };
	long state_key (char *o, long cap) const
	{
		long w = style->format (o, (int) (cap - 400 - IFS_MAX * sizeof (Xform) - 8 - CHORDS_MAX * 8 * sizeof (int)));
		memcpy (o + w, &ifs->n, sizeof ifs->n); w += sizeof ifs->n;
		double fr[4] = { ifs->x0, ifs->y0, ifs->x1, ifs->y1 };
		memcpy (o + w, fr, sizeof fr); w += sizeof fr;
		memcpy (o + w, &img->serial, sizeof img->serial); w += sizeof img->serial;
		memcpy (o + w, ifs->t, sizeof (Xform) * (size_t) ifs->n); w += (long) sizeof (Xform) * ifs->n;
		int len = chords ? chords->length : 0, loop = chords && chords->loop;
		memcpy (o + w, &len, sizeof len); w += sizeof len;
		memcpy (o + w, &loop, sizeof loop); w += sizeof loop;
		for (int i = 0; i < len; i++)		// (champ par champ : les octets de bourrage d'un Chord ne comptent pas)
		{
			const Chord &c = chords->c[i];
			int f[8] = { c.deg, c.acc, c.qual, c.sus, c.sev, c.ext, c.inv, c.open };
			memcpy (o + w, f, sizeof f); w += sizeof f;
		}
		return w;
	}
	void snapshot ()
	{
		rec->state (done, *style, *ifs, *img, chords);
		remember ();
	}
	void remember ()
	{
		static char b[KEY_CAP];
		recKeyLen = state_key (b, sizeof b);
		recKey = (char *) realloc (recKey, (size_t) recKeyLen);
		memcpy (recKey, b, (size_t) recKeyLen);
	}
	bool state_changed () const
	{
		static char b[KEY_CAP];
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
		for (int k = 0; k < 4; k++) { q.lastKey[k] = -1; q.lastIdx[k] = -1; q.prev[k] = -1; }
		q.chordId = q.leadPc = -1;
		q.rpos = q.pos = q.gate = 0; q.posKey = -1;
	}
	void settings (int i, uint32_t tick)
	{
		const Part &p = cur ().p[i];
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
		form = formIn && formIn->n ? formIn : 0;
		if (form) form->prepare (*img);
		sec = 0; sImg = sVar = 0; sU = sV = 0.5; sZ = 1; sKey = sBar = 0; copying = false; sStyle = 0;
		if (rec)
		{
			rec->clear ();
			rec->seed = seed; rec->mask = mask; rec->root = root; rec->length = 0;
			rec->set_form (form);
			snapshot ();
		}
		for (int i = 0; i < NPART; i++)
		{
			v[i].live = false;
			// (une partie allumee dans le style d'une lettre est effacee aussi : la forme la recompose)
			bool on = style->p[i].on;
			if (form) for (int l = 0; l < 26; l++) if (form->def[l].style && form->def[l].style->p[i].on && form->uses (l)) on = true;
			if (!on) continue;
			song->tr[i].clear ();
			if (style->p[i].on) settings (i, 0);
		}
		if (form) enter (0);
		else begin (0);
	}
	// les parties allumees partent du debut du chemin
	void begin (uint32_t tick)
	{
		Rng r (seed);
		double x = 0, y = 0;
		for (int i = 0; i < 64; i++) ifs->step (x, y, r);
		for (int i = 0; i < NPART; i++)
		{
			v[i].live = false;
			if (!cur ().p[i].on) continue;
			for (int k = cur ().p[i].phase; k > 0; k--) ifs->step (x, y, r);
			init_voice (i, x, y, tick);
		}
	}
	// ---- la forme ----
	void enter (int k)
	{
		const uint32_t barLen = PPQ * BEATS;
		sec = k;
		uint32_t at = form->start (k, barLen);
		if (k >= form->n) { for (int i = 0; i < NPART; i++) v[i].live = false; copying = true; return; }
		int l = form->sl[k], vv = form->sv[k];
		const FormLetter &d = form->def[l];
		// le style de la lettre : ses instruments, volumes et panoramiques des le debut de la section
		if (d.style != sStyle)
		{
			sStyle = d.style;
			for (int i = 0; i < NPART; i++) if (cur ().p[i].on) settings (i, at);
		}
		int first = form->first (k);
		copying = first < k;
		if (copying) { copy (first, at); return; }
		sImg = form->img (l, 0); if (!sImg) sImg = img;
		sVar = vv > 0 ? form->img (l, vv) : 0;
		sU = d.u; sV = d.v; sZ = d.zoom; sKey = d.key;
		sBar = (int) (at / barLen);
		begin (at);
	}
	// la reprise : les notes de la section, decalees (ce qui est plus long que la section la depasse comme avant)
	void copy (int from, uint32_t at)
	{
		const uint32_t barLen = PPQ * BEATS;
		uint32_t s0 = form->start (from, barLen), s1 = form->start (from + 1, barLen);
		for (int i = 0; i < NPART; i++)
		{
			v[i].live = false;
			Track &t = song->tr[i];
			int n = t.count;
			for (int k = t.find (s0); k < n; k++)
			{
				Note m = t.n[k];
				if (m.tick >= s1) break;
				if (m.kind != NK_NOTE) continue;
				m.tick = m.tick - s0 + at;
				*t.add () = m;
			}
		}
	}
	// la partie garde-t-elle ses hauteurs dans une variation ? (la basse, les accords, les percussions)
	static bool keeps (const Part &p) { return p.chan == 9 || p.harm == HARM_CHORD || p.harm == HARM_BASS; }
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
			const Part &p = cur ().p[i];
			if (!p.on) { v[i].live = false; continue; }
			if (!v[i].live) init_voice (i, x, y, styleAt);
			else if (v[i].next < styleAt) v[i].next = styleAt;
			v[i].first = true;
			settings (i, styleAt);
		}
	}
	// les touches de 0 a 127 dont la note est dans set (douze bits, do = bit 0) ; toutes pour les percussions
	static int keys (const Part &p, unsigned set, int *out)
	{
		int n = 0;
		for (int k = 0; k < 128; k++)
			if (p.chan == 9 || (set >> (k % 12) & 1)) out[n++] = k;
		return n;
	}
	// la voix k au-dessus de la plus grave, en rangs de la liste des touches : tierces empilees, serrees ou ouvertes
	static int offset (int chord, int k, int step, int perOct)
	{
		static const int SHAPE[3][4] = { { 0, 2, 4, 6 }, { 0, 4, 6, 2 }, { 0, 4, 2, 6 } };
		return SHAPE[chord][k] * step / 2 + ((chord == CHORD_OPEN1 && k == 3) || (chord == CHORD_OPEN2 && k >= 2) ? perOct : 0);
	}
	static int iabs (int x) { return x < 0 ? -x : x; }
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
		const Part &p = cur ().p[i];
		ifs->step (q.x, q.y, q.rng);
		double u, w;
		ifs->to_unit (q.x, q.y, u, w);
		// (le point dans l'image de depart : avec une forme, celui de la region de la section)
		double bu = form ? sU + (u - 0.5) / sZ : u, bw = form ? sV + (w - 0.5) / sZ : w;
		q.tu[q.thead] = (float) bu; q.tv[q.thead] = (float) bw;
		q.thead = (q.thead + 1) % TRAIL;
		if (q.tcount < TRAIL) q.tcount++;
		uint8_t c[3], cp[3];
		pixel_rgb (sImg ? sImg : img, u, w, c);
		// la hauteur : dans une variation, lue dans sa region decalee (sauf les parties qui gardent leurs notes)
		if (sVar && !keeps (p)) pixel_rgb (sVar, u, w, cp); else memcpy (cp, c, 3);
		// la duree : le motif dans l'ordre ; sans motif, les bits bas de la couleur (8, sinon 1 a 7, sinon 1 unite)
		int coef[24], nc = 0;
		for (const char *s = p.rhythm; *s; s++) if (*s > '0' && *s <= '9' && nc < 24) coef[nc++] = *s - '0';
		int cd = c[p.mapD % 3], mult;
		if (nc) { mult = coef[q.rpos % nc]; q.rpos = (q.rpos + 1) % nc; }
		else mult = cd & 8 ? 8 : cd & 7 ? cd & 7 : 1;
		uint32_t dur = (uint32_t) (p.unit * mult * PPQ / STL_PPQ);
		uint32_t at = q.next;
		q.next = at + dur;
		if (q.rng.below (100) >= p.density) { for (int k = 0; k < 4; k++) q.lastKey[k] = -1; return; }
		// la hauteur : une note de la gamme, ou de l'accord, dans la tessiture
		unsigned mask = this->mask; int root = this->root;
		if (sKey) MusicForm::transpose (sKey, mask, root);
		unsigned set = harm_abs (mask, root), chordPcs = 0;
		int stepK = 2;				// d'une voix a l'autre : une tierce de la gamme, ou la note suivante de l'accord
		bool open = false, lead = false, leadBass = false;
		int bar = (int) (at / (PPQ * BEATS)) - sBar;
		Chord hc; int cat;
		if (p.chan != 9 && p.harm != HARM_FREE && chords && chords->find (bar, hc, cat))
		{
			ChordTones ct = chord_tones (hc, mask, root);
			bool strong = at % PPQ == 0;
			int id = bar - (chords->slot (bar) - cat);
			bool change = id != q.chordId;
			q.chordId = id;
			open = hc.open;
			lead = hc.inv == CHORD_LEAD && change;	// (au premier accord de la partie, il n'y a pas d'ou venir : prev est vide)
			if (p.harm == HARM_CHORD || (p.harm == HARM_BEATS && strong)) { set = ct.pcs; stepK = 1; }
			else if (p.harm == HARM_BASS)
			{
				// la basse : celle du renversement aux temps ; en voice leading, la note de l'accord la plus proche, gardee
				// aux temps tant que l'accord dure
				if (hc.inv != CHORD_LEAD) set = strong ? 1u << ct.bass : ct.pcs;
				else if (change || q.leadPc < 0) { set = ct.pcs; leadBass = true; }
				else set = strong ? 1u << q.leadPc : ct.pcs;
				stepK = 1;
			}
			else { set = (set & ~ct.clash) | ct.pcs; chordPcs = ct.pcs; }
			if (stepK == 2) lead = false;
		}
		int all[128];
		int na = keys (p, set, all);
		if (!na) return;
		int lo = 0, hi = na - 1;
		while (lo < na && all[lo] < p.low) lo++;
		while (hi >= 0 && all[hi] > p.high) hi--;
		if (lo > hi) { lo = hi = lo < na ? lo : na - 1; }
		// (la voix du haut reste dans la tessiture, si elle est assez large)
		int perOct = harm_count (set);
		int voices = p.voices < 1 ? 1 : p.voices > 4 ? 4 : p.voices, top = 0;
		int chord = open ? CHORD_OPEN2 : p.chord % 3;
		for (int k = 0; k < voices; k++)
		{
			int d = offset (chord, k, stepK, perOct);
			if (d > top) top = d;
		}
		if (hi - top >= lo) hi -= top;
		int span = hi - lo + 1, deg;
		if (q.first)
		{
			q.first = false;
			deg = lo + cp[p.mapP % 3] * span / 256;
			if (p.start)
			{
				int best = lo;
				for (int k = lo; k <= hi; k++) if (iabs (all[k] - p.start) < iabs (all[best] - p.start)) best = k;
				deg = best;
			}
			q.gate = p.start ? deg - lo : c[p.mapP % 3] * span / 256;
		}
		else
		{
			// le pas : de -n/2 a +n/2 environ, n etant la tessiture (au plus une octave de la gamme). Trop haut, la note
			// redescend d'autant sous le haut ; sous le bas, la partie se tait, et le premier pas qui monte la ramene.
			// (Le silence se decide sur l'image du theme : une variation garde le rythme ; la hauteur part de la note jouee)
			int n = hi - lo, perScale = harm_count (mask), most = n < perScale ? n : perScale;
			q.gate = next_pos (q.gate, move (most, c[p.mapP % 3]), n);
			if (q.gate < 0) { q.posKey = -1; for (int k = 0; k < 4; k++) q.lastKey[k] = -1; return; }
			int pos = q.pos;
			if (q.posKey >= 0)
			{
				pos = 0;
				for (int k = lo; k <= hi; k++) if (iabs (all[k] - q.posKey) < iabs (all[lo + pos] - q.posKey)) pos = k - lo;
			}
			int np = next_pos (pos, move (most, cp[p.mapP % 3]), n);
			deg = lo + (np < 0 ? 0 : np > n ? n : np);
		}
		// entre les temps, une note etrangere a l'accord vient par degre conjoint de la precedente, ou rejoint l'accord
		if (chordPcs && !(chordPcs >> (all[deg] % 12) & 1) && (q.lastKey[0] < 0 || iabs (all[deg] - q.lastKey[0]) > 2))
		{
			int best = -1;
			for (int k = lo; k <= hi; k++)
				if ((chordPcs >> (all[k] % 12) & 1) && (best < 0 || iabs (k - deg) < iabs (best - deg))) best = k;
			if (best >= 0) deg = best;
		}
		// voice leading : au changement d'accord, la position ou les voix bougent le moins depuis leurs dernieres notes
		// (a egalite, la plus proche de celle que donne la couleur)
		if ((lead || leadBass) && q.prev[0] >= 0)
		{
			int best = deg, bestCost = 0x7FFFFFFF;
			for (int k = lo; k <= hi; k++)
			{
				int cost = 0;
				for (int vo = 0; vo < voices; vo++)
				{
					int d = k + offset (chord, vo, stepK, perOct);
					if (q.prev[vo] >= 0) cost += iabs (all[d < na - 1 ? d : na - 1] - q.prev[vo]);
				}
				if (cost < bestCost || (cost == bestCost && iabs (k - deg) < iabs (best - deg))) { best = k; bestCost = cost; }
			}
			deg = best;
		}
		if (leadBass) q.leadPc = all[deg] % 12;
		q.pos = deg - lo; q.posKey = all[deg];		// (le pas suivant part de la note jouee, accord compris)
		// les nuances, l'articulation
		int cv = c[p.mapV % 3];
		int vel = 127 - p.range + cv * p.range / 255;
		vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
		static const int ART[4] = { 50, 75, 90, 100 };
		int pct = p.mapA == ART_FIXED ? 90 : ART[c[(p.mapA - 1) % 3] * 4 / 256];
		uint32_t len = dur * (uint32_t) pct / 100;
		if (len < 1) len = 1;
		// les voix : tierces empilees, serrees ou ouvertes
		Track &t = song->tr[i];
		for (int k = 0; k < voices; k++)
		{
			int d = deg + offset (chord, k, stepK, perOct);
			if (d >= na) d = na - 1;
			int key = all[d];
			q.prev[k] = key;
			if (p.tie && q.lastKey[k] == key && q.lastIdx[k] >= 0 && q.lastIdx[k] < t.count && t.n[q.lastIdx[k]].key == key)
			{
				Note &m = t.n[q.lastIdx[k]];
				m.dur = at + len - m.tick;
				continue;
			}
			Note *m = t.add ();
			m->tick = at; m->dur = len; m->key = (uint8_t) key; m->vel = (uint8_t) vel; m->ch = (uint8_t) p.chan; m->kind = NK_NOTE;
			m->pu = (uint16_t) (1 + (bu < 0 ? 0 : bu > 1 ? 1 : bu) * 65534); m->pv = (uint16_t) (1 + (bw < 0 ? 0 : bw > 1 ? 1 : bw) * 65534);
			q.lastKey[k] = key; q.lastIdx[k] = t.count - 1;
		}
	}
	// le pas de la hauteur : Round (most (127 - c) / 255), au pair le plus proche comme Math.Round (sans demi : 255 est impair)
	static int move (int most, int c)
	{
		int num = most * (127 - c), q = num / 255, r = num % 255;
		if (2 * r > 255) q++; else if (2 * r < -255) q--;
		return q;
	}
	// trop haut : n - pas ; sous le bas : le pas (negatif, la partie se tait)
	static int next_pos (int at, int mv, int n) { return at + mv > n ? n - mv : at + mv < 0 ? mv : at + mv; }
	void run (uint32_t to)
	{
		if (copying) return;
		// (dans l'ordre du temps entre parties : le trace est partage mais chaque partie a son chemin)
		for (int i = 0; i < NPART; i++)
			while (v[i].live && cur ().p[i].on && v[i].next < to) step (i);
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
			if (form && sec < form->n) { uint32_t e = form->start (sec + 1, PPQ * BEATS); if (e <= b) { b = e; what = 3; } }
			run (b);
			if (!what) break;
			if (what == 1) { scalePending = false; mask = nextMask; root = nextRoot; }
			else if (what == 2) { apply_style (); styled = true; }
			else enter (sec + 1);
		}
		if (to > done) done = to;
		if (styled && rec) remember ();
	}
};

#endif
