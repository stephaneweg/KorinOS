//
// song.h -- la sequence d'Arabesque : dix pistes, une par partie, et le fichier MIDI standard (lu en formats 0 et 1, ecrit
// en format 1). Une piste tient ses evenements dans l'ordre : des notes, et les changements d'instrument, de volume et de
// panoramique, qui sont des evenements comme les autres (une composition peut changer de style en route).
//
#ifndef ARABESQUE_SONG_H
#define ARABESQUE_SONG_H

#include "style.h"

static const int PPQ = 480, STL_PPQ = 120, BEATS = 4;	// mesures a 4/4
enum { NK_NOTE, NK_PROGRAM, NK_CC };

struct Note { uint32_t tick, dur; uint8_t key, vel, ch, kind; };	// NK_PROGRAM : key ; NK_CC : key = controleur, vel = valeur

struct Track
{
	Note *n; int count, cap;
	Track () : n (0), count (0), cap (0) {}
	Note *add ()
	{
		if (count == cap)
		{
			cap = cap ? cap * 2 : 4096;
			n = (Note *) realloc (n, sizeof (Note) * (size_t) cap);
		}
		return n + count++;
	}
	void clear () { count = 0; }
	void ctl (uint32_t tick, int ch, int kind, int a, int b)
	{
		Note *m = add ();
		m->tick = tick; m->dur = 0; m->ch = (uint8_t) ch; m->kind = (uint8_t) kind; m->key = (uint8_t) a; m->vel = (uint8_t) b;
	}
	int notes () const { int c = 0; for (int i = 0; i < count; i++) if (n[i].kind == NK_NOTE) c++; return c; }
	// le premier evenement a partir d'une pulsation
	int find (uint32_t tick) const
	{
		int a = 0, b = count;
		while (a < b) { int m = (a + b) / 2; if (n[m].tick < tick) a = m + 1; else b = m; }
		return a;
	}
};

struct Song
{
	Track tr[NPART];
	int tempo;
	char copyright[120];
	Song () : tempo (120) { copyright[0] = 0; }
	void clear () { for (int i = 0; i < NPART; i++) tr[i].clear (); }
	uint32_t length () const
	{
		uint32_t e = 0;
		for (int i = 0; i < NPART; i++)
			for (int k = 0; k < tr[i].count; k++) { uint32_t t = tr[i].n[k].tick + tr[i].n[k].dur; if (t > e) e = t; }
		return e;
	}
	bool empty () const { for (int i = 0; i < NPART; i++) if (tr[i].count) return false; return true; }
};

// ---- ecrire -----------------------------------------------------------------------------------------------------------
struct Out
{
	uint8_t *b; long n, cap;
	Out () : b (0), n (0), cap (0) {}
	~Out () { free (b); }
	void c (int v) { if (n == cap) { cap = cap ? cap * 2 : 65536; b = (uint8_t *) realloc (b, (size_t) cap); } b[n++] = (uint8_t) v; }
	void w16 (unsigned v) { c ((int) (v >> 8)); c ((int) v); }
	void w32 (unsigned v) { c ((int) (v >> 24)); c ((int) (v >> 16)); c ((int) (v >> 8)); c ((int) v); }
	void vlq (unsigned v)
	{
		uint8_t t[5]; int k = 0;
		t[k++] = v & 0x7F;
		while (v >>= 7) t[k++] = (uint8_t) (v & 0x7F) | 0x80;
		while (k) c (t[--k]);
	}
	void str (const void *s, int l) { for (int i = 0; i < l; i++) c (((const uint8_t *) s)[i]); }
	void meta (unsigned delta, int type, const void *s, int l) { vlq (delta); c (0xFF); c (type); vlq ((unsigned) l); str (s, l); }
};

struct MEv { uint32_t tick; uint32_t seq; uint8_t st, a, b; };
static int mev_cmp (const void *pa, const void *pb)
{
	const MEv *x = (const MEv *) pa, *y = (const MEv *) pb;
	if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

static bool song_save_midi (const Song &s, const Style &st, const char *path)
{
	Out o;
	int ntr = 0;
	for (int i = 0; i < NPART; i++) if (s.tr[i].count) ntr++;
	o.str ("MThd", 4); o.w32 (6); o.w16 (1); o.w16 ((unsigned) ntr + 1); o.w16 (PPQ);
	// la piste des tempos
	o.str ("MTrk", 4);
	long lenAt = o.n; o.w32 (0);
	uint8_t ts[4] = { 4, 2, 24, 8 }; o.meta (0, 0x58, ts, 4);
	unsigned us = 60000000u / (unsigned) (s.tempo > 0 ? s.tempo : 120);
	uint8_t tp[3] = { (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us }; o.meta (0, 0x51, tp, 3);
	if (s.copyright[0]) o.meta (0, 0x02, s.copyright, (int) strlen (s.copyright));
	o.meta (0, 0x2F, "", 0);
	unsigned tl = (unsigned) (o.n - lenAt - 4);
	o.b[lenAt] = (uint8_t) (tl >> 24); o.b[lenAt + 1] = (uint8_t) (tl >> 16); o.b[lenAt + 2] = (uint8_t) (tl >> 8); o.b[lenAt + 3] = (uint8_t) tl;
	for (int i = 0; i < NPART; i++)
	{
		const Track &t = s.tr[i];
		if (!t.count) continue;
		MEv *e = (MEv *) malloc (sizeof (MEv) * (size_t) t.count * 2);
		int ne = 0; uint32_t seq = 0;
		for (int k = 0; k < t.count; k++)
		{
			const Note &m = t.n[k];
			if (m.kind == NK_PROGRAM) e[ne++] = MEv { m.tick, seq++ * 2 + 1, (uint8_t) (0xC0 | m.ch), m.key, 0 };
			else if (m.kind == NK_CC) e[ne++] = MEv { m.tick, seq++ * 2 + 1, (uint8_t) (0xB0 | m.ch), m.key, m.vel };
			else
			{
				e[ne++] = MEv { m.tick, seq++ * 2 + 1, (uint8_t) (0x90 | m.ch), m.key, m.vel };
				e[ne++] = MEv { m.tick + m.dur, seq++ * 2, (uint8_t) (0x80 | m.ch), m.key, 64 };	// (avant les notes du meme instant)
			}
		}
		qsort (e, (size_t) ne, sizeof (MEv), mev_cmp);
		o.str ("MTrk", 4);
		lenAt = o.n; o.w32 (0);
		if (st.p[i].name[0]) o.meta (0, 0x03, st.p[i].name, (int) strlen (st.p[i].name));
		uint32_t last = 0; int run = 0;
		for (int k = 0; k < ne; k++)
		{
			o.vlq (e[k].tick - last); last = e[k].tick;
			if (e[k].st != run) { o.c (e[k].st); run = e[k].st; }
			o.c (e[k].a);
			if ((e[k].st & 0xF0) != 0xC0) o.c (e[k].b);
		}
		free (e);
		o.meta (0, 0x2F, "", 0);
		tl = (unsigned) (o.n - lenAt - 4);
		o.b[lenAt] = (uint8_t) (tl >> 24); o.b[lenAt + 1] = (uint8_t) (tl >> 16); o.b[lenAt + 2] = (uint8_t) (tl >> 8); o.b[lenAt + 3] = (uint8_t) tl;
	}
	return write_all (path, o.b, o.n);
}

// ---- lire -------------------------------------------------------------------------------------------------------------
static unsigned be32 (const uint8_t *p) { return (unsigned) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static unsigned be16 (const uint8_t *p) { return (unsigned) p[0] << 8 | p[1]; }
static bool rd_vlq (const uint8_t *b, unsigned end, unsigned *p, unsigned *v)
{
	*v = 0;
	for (int i = 0; i < 4; i++) { if (*p >= end) return false; uint8_t c = b[(*p)++]; *v = *v << 7 | (c & 0x7F); if (!(c & 0x80)) return true; }
	return false;
}

// Format 1 : une partie par piste qui a des notes ; format 0 : une partie par canal. Les dix premieres seulement.
// Les parties lues sont eteintes : composer ne les ecrase pas, on y ajoute d'autres parties.
static bool song_load_midi (Song &s, Style &st, const char *path)
{
	long size = 0;
	uint8_t *b = (uint8_t *) read_all (path, &size);
	if (!b) return false;
	unsigned n = (unsigned) size;
	if (n < 14 || memcmp (b, "MThd", 4)) { free (b); return false; }
	unsigned hl = be32 (b + 4), fmt = be16 (b + 8), ntr = be16 (b + 10), div = be16 (b + 12);
	if (!div || (div & 0x8000)) { free (b); return false; }
	s.clear (); s.tempo = 120; s.copyright[0] = 0;
	st.reset ();
	int used = 0;
	unsigned p = 8 + hl;
	struct Open { int idx; } open[16][128];
	bool tempoSeen = false;
	for (unsigned t = 0; t < ntr && p + 8 <= n; t++)
	{
		unsigned len = be32 (b + p + 4), q = p + 8, end = q + len > n ? n : q + len;
		int map[16]; for (int c = 0; c < 16; c++) map[c] = -1;
		int single = -1;
		char tname[32]; tname[0] = 0;
		for (int c = 0; c < 16; c++) for (int k = 0; k < 128; k++) open[c][k].idx = -1;
		unsigned tick = 0; uint8_t rs = 0;
		while (q < end)
		{
			unsigned d;
			if (!rd_vlq (b, end, &q, &d) || q >= end) break;
			tick += d;
			uint32_t at = (uint32_t) ((uint64_t) tick * PPQ / div);
			uint8_t c = b[q];
			if (c == 0xFF)
			{
				if (q + 2 > end) break;
				uint8_t type = b[q + 1]; q += 2;
				unsigned ml;
				if (!rd_vlq (b, end, &q, &ml) || q + ml > end) break;
				if (type == 0x51 && ml == 3 && !tempoSeen) { unsigned us = (unsigned) b[q] << 16 | b[q + 1] << 8 | b[q + 2]; if (us) { s.tempo = (int) (60000000u / us); tempoSeen = true; } }
				if (type == 0x03 && !tname[0]) { unsigned k = ml < 31 ? ml : 31; memcpy (tname, b + q, k); tname[k] = 0; }
				if (type == 0x02 && !s.copyright[0]) { unsigned k = ml < 119 ? ml : 119; memcpy (s.copyright, b + q, k); s.copyright[k] = 0; }
				q += ml;
				continue;
			}
			if (c == 0xF0 || c == 0xF7) { q++; unsigned ml; if (!rd_vlq (b, end, &q, &ml)) break; q += ml; continue; }
			if (c & 0x80) { rs = c; q++; } else if (!rs) break; else c = rs;
			int nd = (c & 0xF0) == 0xC0 || (c & 0xF0) == 0xD0 ? 1 : 2;
			if (q + (unsigned) nd > end) break;
			uint8_t d1 = b[q], d2 = nd > 1 ? b[q + 1] : 0;
			q += (unsigned) nd;
			int ch = c & 15, ty = c & 0xF0;
			bool on = ty == 0x90 && d2, off = ty == 0x80 || (ty == 0x90 && !d2);
			if (!on && !off && ty != 0xC0 && !(ty == 0xB0 && (d1 == 7 || d1 == 10))) continue;
			// la partie de cet evenement
			int part = fmt == 0 ? map[ch] : single;
			if (part < 0)
			{
				if (!on && !(ty == 0xC0 || ty == 0xB0)) continue;
				if (used >= NPART) continue;
				part = used++;
				if (fmt == 0) map[ch] = part; else single = part;
				st.p[part].chan = ch;
				strcpy (st.p[part].name, tname);
			}
			Track &tr = s.tr[part];
			if (ty == 0xC0) { tr.ctl (at, ch, NK_PROGRAM, d1, 0); if (at == 0) st.p[part].patch = d1; continue; }
			if (ty == 0xB0)
			{
				tr.ctl (at, ch, NK_CC, d1, d2);
				if (at == 0) { if (d1 == 7) st.p[part].vol = d2; else st.p[part].pan = d2; }
				continue;
			}
			if (off)
			{
				int k = open[ch][d1 & 127].idx;
				if (k >= 0) { tr.n[k].dur = at > tr.n[k].tick ? at - tr.n[k].tick : 1; open[ch][d1 & 127].idx = -1; }
				continue;
			}
			Note *m = tr.add ();
			m->tick = at; m->dur = PPQ; m->key = d1; m->vel = d2; m->ch = (uint8_t) ch; m->kind = NK_NOTE;
			open[ch][d1 & 127].idx = (int) (m - tr.n);
		}
		p += 8 + len;
	}
	free (b);
	return true;
}

// le volume, le panoramique ou l'instrument de depart d'une partie, change apres coup
static void song_retouch (Song &s, int part, int kind, int a, int b)
{
	Track &t = s.tr[part];
	for (int k = 0; k < t.count && t.n[k].tick == 0; k++)
	{
		Note &m = t.n[k];
		if (m.kind != kind) continue;
		if (kind == NK_PROGRAM) m.key = (uint8_t) a;
		else if (m.key == a) m.vel = (uint8_t) b;
	}
}

#endif
