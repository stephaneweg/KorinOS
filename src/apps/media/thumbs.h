//
// thumbs.h -- the videos' frames shown in the library (Onyx's Apps/media/thumbs.h): a frame from about a tenth of the
// video (at most a minute in), decoded by a thread of its own with a MediaKit handle of its own, cropped to 16:9 and
// brought to BASE_W x BASE_H; kept on the card as a JPEG (SD0:/home/media/thumbs/<key>.jpg) so the next start shows them
// at once. Each size asked for is made once and kept (a small cache). A video MediaKit cannot decode (H.264...) gets a
// frame drawn from its name.
//
//   g_thumbs.draw (videoIndex, x, y, w, h, radius, bg);   (a placeholder until it is there)
//
#ifndef _media_thumbs_h
#define _media_thumbs_h

#include "covers.h"
#include "jpegsave.h"

namespace media {

#define THUMB_DIR "SD0:/home/media/thumbs"

// A frame of the video at about `atMs` -> w x h pixels 0x00RRGGBB (new unsigned[]), or 0. `stop`: give up (the window
// plays a video: the thumbnails wait).
static unsigned *video_frame_at (const char *path, int atMs, int *ow, int *oh, volatile bool *stop)
{
	long h = mk_open ((long) path);
	if (h <= 0) return 0;
	struct mk_info in; struct mk_frame fr;
	unsigned *out = 0;
	if (mk_info (h, (long) &in) == 0 && in.vok && in.width > 0 && in.height > 0)
	{
		i64 target = (i64) atMs * 1000;
		if (target > 0 && mk_seek (h, target) < 0) { mk_close (h); h = mk_open ((long) path); if (h <= 0) return 0; target = 0; }	// (no index: the start)
		int frames = 0; bool got = false;
		while (!(stop && *stop) && frames < 90)
		{
			if (mk_video_next (h, (long) &fr) != 1) break;
			frames++; got = true;
			if (fr.pts >= target - 50000) break;
		}
		if (got && !(stop && *stop))
		{	// (at most 640 wide: a frame for a small picture)
			int w = (int) in.width, hh = (int) in.height;
			if (w > 640) { hh = hh * 640 / w; w = 640; }
			w &= ~1; hh &= ~1;
			out = new unsigned[(size_t) w * hh];
			if (mk_video_convert (h, (long) out, w * 4, w, hh) != 0) { delete[] out; out = 0; }
			else { *ow = w; *oh = hh; }
		}
	}
	mk_close (h);
	return out;
}

class Thumbs
{
public:
	enum { BASE_W = 384, BASE_H = 216, NBASE = 64, NSCALED = 96, QN = 64 };
	VideoLib *lib;
	bool loaded;					// the window's: something came, to redraw
	volatile bool busy;				// a video plays: the thread waits
	Thumbs () : lib (0), loaded (false), busy (false), m_tick (0), m_qn (0), m_quit (false), m_have (false), m_ndone (0)
	{ memset (m_base, 0, sizeof m_base); memset (m_sc, 0, sizeof m_sc); pthread_mutex_init (&m_lk, 0); }

	void start () { m_have = pthread_create (&m_tid, 0, thread_main, this) == 0; }
	void quit () { m_quit = true; busy = true; if (m_have) pthread_join (m_tid, 0); m_have = false; }
	void set_library (VideoLib *l)
	{
		pthread_mutex_lock (&m_lk); lib = l; m_qn = 0; pthread_mutex_unlock (&m_lk);
		for (int i = 0; i < NBASE; i++) if (m_base[i].pending) m_base[i].key = 0, m_base[i].pending = false;
	}
	unsigned key_of (int vi) const { const Video &x = lib->v[vi]; return (hash_str (x.path) ^ (unsigned) x.size * 2654435761u) | 1; }

	// the frame of video vi at (x, y), w x h (16:9 looks best), its corners rounded over bg
	void draw (int vi, int x, int y, int w, int h, int radius, unsigned bg)
	{
		if (!lib || vi < 0 || vi >= lib->n || w <= 0 || h <= 0) return;
		const unsigned *px = scaled (key_of (vi), vi, w, h);
		if (px)
			for (int j = 0; j < h; j++)
			{
				int yy = y + j; if (yy < 0 || yy >= g_ph) continue;
				uint32_t *row = g_px + (long) yy * g_pw;
				for (int i = 0; i < w; i++) { int xx = x + i; if (xx >= 0 && xx < g_pw) row[xx] = px[j * w + i]; }
			}
		else ui_rbox (x, y, w, h, 0, 0x2A3040, 0x141820);	// (loading) a dark band
		round_corners (x, y, w, h, radius, bg);
	}
	// the window's turn: the frames the thread made
	void pump ()
	{
		for (;;)
		{
			pthread_mutex_lock (&m_lk);
			bool any = m_ndone > 0; Done d = m_done[0];
			if (any) { m_ndone--; memmove (m_done, m_done + 1, sizeof (Done) * m_ndone); }
			pthread_mutex_unlock (&m_lk);
			if (!any) return;
			Base *b = base_of (d.key);
			if (b && b->pending) { b->pending = false; b->px = d.px; d.px = 0; }
			delete[] d.px;
			loaded = true;
		}
	}

private:
	struct Base { unsigned key; unsigned *px; unsigned use; bool pending; };
	struct Scaled { unsigned key; int w, h; unsigned *px; unsigned use; };
	struct Req { unsigned key; char path[300]; char title[120]; int atMs; bool playable; };
	struct Done { unsigned key; unsigned *px; };
	Base m_base[NBASE]; Scaled m_sc[NSCALED]; unsigned m_tick;
	pthread_mutex_t m_lk;
	Req m_q[QN]; int m_qn;
	pthread_t m_tid; volatile bool m_quit; bool m_have;
	Done m_done[QN]; int m_ndone;

	Base *base_of (unsigned key) { for (int i = 0; i < NBASE; i++) if (m_base[i].key == key) return &m_base[i]; return 0; }
	Base *base_slot ()
	{
		int best = -1;
		for (int i = 0; i < NBASE; i++) { if (!m_base[i].key) return &m_base[i]; if (!m_base[i].pending && (best < 0 || m_base[i].use < m_base[best].use)) best = i; }
		if (best < 0) return 0;
		Base &b = m_base[best]; delete[] b.px;
		for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == b.key) { delete[] m_sc[i].px; memset (&m_sc[i], 0, sizeof m_sc[i]); }
		memset (&b, 0, sizeof b); return &b;
	}
	const unsigned *scaled (unsigned key, int vi, int w, int h)
	{
		for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == key && m_sc[i].w == w && m_sc[i].h == h && m_sc[i].px) { m_sc[i].use = ++m_tick; return m_sc[i].px; }
		Base *b = base_of (key);
		if (!b)
		{
			b = base_slot (); if (!b) return 0;
			b->key = key; b->use = ++m_tick; b->pending = true;
			ask (key, lib->v[vi]);
		}
		b->use = ++m_tick;
		if (!b->px) return 0;
		int k = -1; unsigned old = ~0u;
		for (int i = 0; i < NSCALED; i++) { if (!m_sc[i].px) { k = i; break; } if (m_sc[i].use < old) { old = m_sc[i].use; k = i; } }
		Scaled &s = m_sc[k]; delete[] s.px;
		s.key = key; s.w = w; s.h = h; s.use = ++m_tick; s.px = new unsigned[(unsigned) w * h];
		fit_cover (b->px, BASE_W, BASE_H, s.px, w, h);
		return s.px;
	}
	void ask (unsigned key, const Video &x)
	{
		pthread_mutex_lock (&m_lk);
		if (m_qn < QN)
		{
			Req &r = m_q[m_qn++]; r.key = key; scopy (r.path, x.path, sizeof r.path); scopy (r.title, x.title, sizeof r.title); r.playable = x.playable;
			int at = x.durMs / 10; if (at > 60000) at = 60000; if (x.durMs > 0 && at < 1000 && x.durMs > 2000) at = 1000;
			r.atMs = at;
		}
		pthread_mutex_unlock (&m_lk);
	}
	static void *thread_main (void *p) { ((Thumbs *) p)->run (); return 0; }
	void run ()
	{
		while (!m_quit)
		{
			if (busy) { sleep_ms (100); continue; }
			Req r; bool have = false;
			pthread_mutex_lock (&m_lk);
			if (m_qn > 0) { r = m_q[m_qn - 1]; m_qn--; have = true; }		// (the latest asked first: what is in sight)
			pthread_mutex_unlock (&m_lk);
			if (!have) { sleep_ms (40); continue; }
			Done d = { r.key, make (r) };
			if (!d.px) continue;
			pthread_mutex_lock (&m_lk);
			if (m_ndone < QN) m_done[m_ndone++] = d; else delete[] d.px;
			pthread_mutex_unlock (&m_lk);
		}
	}
	unsigned *make (const Req &r)
	{
		char cp[64]; snprintf (cp, sizeof cp, THUMB_DIR "/%08x.jpg", r.key);
		unsigned *px = new unsigned[BASE_W * BASE_H];
		unsigned n = 0; unsigned char *b = (unsigned char *) read_all (cp, 2u << 20, &n);
		if (b)
		{
			int w = 0, h = 0; unsigned *im = image_decode (b, (int) n, &w, &h);
			free (b);
			if (im && w == BASE_W && h == BASE_H) { for (int i = 0; i < BASE_W * BASE_H; i++) px[i] = im[i] & 0xFFFFFF; free (im); return px; }
			free (im);
		}
		int w = 0, h = 0;
		unsigned *f = r.playable && mediakit_kit_open () ? video_frame_at (r.path, r.atMs, &w, &h, &busy) : 0;
		if (f)
		{
			fit_cover (f, w, h, px, BASE_W, BASE_H);
			delete[] f;
			unsigned jn = 0; unsigned char *jpg = jpeg_encode (px, BASE_W, BASE_H, 85, &jn);
			if (jpg) { make_dir (LIB_DIR); make_dir (THUMB_DIR); save_all (cp, jpg, jn); delete[] jpg; }
			return px;
		}
		if (busy && r.playable) { delete[] px; requeue (r); return 0; }	// (stopped for a video: asked again later)
		generated (r.title, px);
		return px;
	}
	void requeue (const Req &r) { pthread_mutex_lock (&m_lk); if (m_qn < QN) { memmove (m_q + 1, m_q, sizeof (Req) * m_qn); m_q[0] = r; m_qn++; } pthread_mutex_unlock (&m_lk); }
	// a frame drawn from the name (a video not decoded here): a gradient, a film's perforations, a play mark
	static void generated (const char *title, unsigned *px)
	{
		static const unsigned PAL[6][2] = { { 0x22304A, 0x5A7AA8 }, { 0x3A2440, 0xA05A8C }, { 0x203C38, 0x4AA08C }, { 0x40301C, 0xC08A48 }, { 0x2C2C34, 0x7A7A8C }, { 0x182838, 0x3C82B4 } };
		const unsigned *c = PAL[hash_str (title) % 6];
		for (int y = 0; y < BASE_H; y++)
		{
			unsigned col = mix (c[1], c[0], y * 256 / BASE_H);
			for (int x = 0; x < BASE_W; x++) px[y * BASE_W + x] = col;
		}
		for (int k = 0; k < 12; k++)
			for (int y = 0; y < 12; y++) for (int x = 0; x < 18; x++) { px[(10 + y) * BASE_W + 12 + k * 32 + x] = c[0]; px[(BASE_H - 22 + y) * BASE_W + 12 + k * 32 + x] = c[0]; }
		for (int y = -18; y <= 18; y++)				// (the play mark)
			for (int x = -11; x <= 19; x++) if (x * 18 + 11 * 18 < (18 - (y < 0 ? -y : y)) * 30) px[(BASE_H / 2 + y) * BASE_W + BASE_W / 2 + x] = 0xF0F0F0;
	}
};

} // namespace media

#endif
