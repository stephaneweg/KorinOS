//
// show.h -- the slide show, full screen (a frameless window over everything: ui_dialog_at): each slide's layers
// composited at the screen's size, its effects played click by click (an object appearing, fading, flying in, wiped,
// zoomed, floating in; growing, pulsing, spinning; going out) -- each a layer's matrix, opacity or clip changing,
// nothing drawn again --, the transitions between slides (fade, push, wipe, cover, uncover, split, zoom, dissolve: the
// two slides as two pictures). The keys: Space / Enter / Right / Down / Page Down / a click: on; Left / Up / Page Up /
// Backspace / a right click: back; Home, End; a number then Enter: that slide; B / W: a black / white screen; Esc: the
// end. The presenter's console (Show > Presenter View): the slide, the next one, the notes large, the time, on the
// one screen.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_show_h
#define _slides_show_h

#include "app.h"

namespace sl {

static inline float ease (float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t < 0.5f ? 2 * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) / 2; }

// ---- the effects' timeline -----------------------------------------------------------------------------------------
// The effects of a slide in steps (a click starts one; With / After previous follow in it); each effect's start
// and end in ms from its step's start.
struct FxTime { int step; int t0, t1; int np; int pstep[24], pt0[24], pt1[24]; };	// np: a text by paragraph, its paragraphs' times
// A text animated by paragraph: how many paragraphs (the non-empty ones; 0: as a whole)
static int fx_paras (const Slide &s, const Anim &a)
{
	if (!a.byPara || a.cls == AC_EMPHASIS) return 0;
	const Object *o = s.by_id (a.obj); if (!o || o->tb.p.n < 2) return 0;
	int n = 0; for (int i = 0; i < o->tb.p.n; i++) if (o->tb.p[i]->len) n++;
	return n > 1 ? imin (n, 24) : 0;
}
static int fx_plan (const Slide &s, FxTime *ft)
{
	int step = 0, prevStart = 0, prevEnd = 0, n = 0;
	for (int i = 0; i < s.anim.n && i < 128; i++)
	{
		const Anim &a = s.anim[i];
		if (a.start == ST_CLICK && i > 0) { step++; prevStart = prevEnd = 0; }
		int t0 = a.start == ST_AFTER ? prevEnd + a.delay : a.start == ST_WITH ? prevStart + a.delay : a.delay;
		if (a.start == ST_CLICK) t0 = a.delay;
		int dur = a.fx == FX_APPEAR ? 1 : imax (1, a.dur);
		int np = fx_paras (s, a);
		ft[i].np = np;
		ft[i].step = step; ft[i].t0 = t0; ft[i].t1 = t0 + dur;
		if (np)
		{
			// paragraph by paragraph: each on a click (the effect started on one), else each after the one before
			for (int k = 0; k < np; k++)
			{
				if (k)
				{
					if (a.start == ST_CLICK) { step++; prevStart = prevEnd = 0; t0 = 0; }
					else t0 = prevEnd;
				}
				ft[i].pstep[k] = step; ft[i].pt0[k] = t0; ft[i].pt1[k] = t0 + dur;
				prevStart = t0; if (t0 + dur > prevEnd) prevEnd = t0 + dur;
			}
			ft[i].t1 = ft[i].pt1[np - 1];
		}
		else { prevStart = t0; if (ft[i].t1 > prevEnd) prevEnd = ft[i].t1; }
		n = step + 1;
	}
	// the first effect "On click"? then step 0 waits for a click; else it plays at once
	return n;
}
// The progress of a part of an effect at (step, ms): -1 not begun, 0..1 playing, 2 done
static inline float fx_progress (int st, int t0, int t1, int step, int ms)
{
	if (st > step) return -1;
	if (st < step) return 2;
	return ms < t0 ? -1 : ms >= t1 ? 2 : (float) (ms - t0) / (t1 - t0);
}
// A text's non-empty paragraphs: their tops and bottoms (hmm, on the slide)
static int fx_bands (const Object *o, float *top, float *bot, int max)
{
	TextLayout L;
	layout_object (g_deck, *const_cast<Object *> (o), L);
	float y0 = o->y + o->tb.inset[1] + text_top (*o, L);
	int n = 0;
	for (int i = 0; i < L.npp && n < max; i++)
		if (o->tb.p[i]->len) { top[n] = y0 + L.pp[i].top; bot[n] = y0 + L.pp[i].bottom; n++; }
	return n;
}
// an object's top-left in px at scale sc (the slide's own px)
static inline float vx_of (const Object *o, float sc) { return o->x * sc; }
static inline float vy_of (const Object *o, float sc) { return o->y * sc; }
// The moves of the objects at (step, ms into it): a hidden entrance, an effect in progress, an exit done.
static int fx_moves (const Slide &s, const FxTime *ft, int step, int ms, float sc, float sx, float sy, Move *mv, int max)
{
	int n = 0;
	for (int i = 0; i < s.anim.n && n < max; i++)
	{
		const Anim &a = s.anim[i];
		const Object *o = s.by_id (a.obj); if (!o) continue;
		// its progress: -1 not begun, 0..1 playing, 2 done
		float t;
		if (ft[i].step > step) t = -1;
		else if (ft[i].step < step) t = 2;
		else t = ms < ft[i].t0 ? -1 : ms >= ft[i].t1 ? 2 : (float) (ms - ft[i].t0) / (ft[i].t1 - ft[i].t0);
		Move m = move_of (o->id);
		bool found = false;
		for (int k = 0; k < n; k++) if (mv[k].id == o->id) { m = mv[k]; found = true; break; }
		if (ft[i].np)
		{
			// by paragraph: the text shown (an entrance) or hidden (an exit) a paragraph after the other, each one
			// wiped from its top to its bottom
			float top[24], bot[24]; int nb = imin (fx_bands (o, top, bot, 24), ft[i].np);
			float X = sx + vx_of (o, sc), Y = sy + vy_of (o, sc), W = o->w * sc, H = o->h * sc;
			float edge = (float) o->y; bool begun = false, all = nb > 0;
			for (int k = 0; k < nb; k++)
			{
				float tk = fx_progress (ft[i].pstep[k], ft[i].pt0[k], ft[i].pt1[k], step, ms);
				if (tk > 1) { edge = bot[k]; begun = true; continue; }
				all = false;
				if (tk >= 0) { edge = top[k] + (bot[k] - top[k]) * ease (tk); begun = true; }
				break;
			}
			float cut = Y + (edge - o->y) * sc;
			if (a.cls == AC_ENTRANCE)
			{
				if (!begun) m.alpha = 0;
				else if (!all) { m.clip[0] = (int) X - 4; m.clip[1] = (int) Y - 4; m.clip[2] = (int) W + 8; m.clip[3] = imax (1, (int) (cut - Y) + 4); }
			}
			else
			{
				if (all) m.alpha = 0;
				else if (begun) { m.clip[0] = (int) X - 4; m.clip[1] = (int) cut; m.clip[2] = (int) W + 8; m.clip[3] = imax (1, (int) (Y + H - cut) + 4); }
			}
			if (found) { for (int k = 0; k < n; k++) if (mv[k].id == o->id) mv[k] = m; }
			else mv[n++] = m;
			continue;
		}
		if (a.cls == AC_ENTRANCE)
		{
			if (t < 0) m.alpha = 0;
			else if (t <= 1)
			{
				float e = ease (t);
				float ox = vx_of (o, sc), oy = vy_of (o, sc), ow = o->w * sc, oh = o->h * sc;
				switch (a.fx)
				{
				case FX_APPEAR: break;
				case FX_FADE: m.alpha = (int) (255 * t); break;
				case FX_FLY:
				{
					// (from: the side named, past the screen's edge)
					if (a.dir == DIR_RIGHT) m.dx = (1 - e) * (g_deck.sw * sc + 2 * sx - ox + 10);
					else if (a.dir == DIR_UP) m.dy = -(1 - e) * (oy + oh + sy + 10);
					else if (a.dir == DIR_LEFT) m.dx = -(1 - e) * (ox + ow + sx + 10);
					else m.dy = (1 - e) * (g_deck.sh * sc + 2 * sy - oy + 10);
					break;
				}
				case FX_WIPE:
				{
					int X = (int) (sx + ox), Y = (int) (sy + oy), W = (int) ceilf (ow), H = (int) ceilf (oh);
					if (a.dir == DIR_RIGHT) { int w = (int) (W * e); m.clip[0] = X + W - w; m.clip[1] = Y - 4; m.clip[2] = imax (1, w); m.clip[3] = H + 8; }
					else if (a.dir == DIR_UP) { int h = (int) (H * e); m.clip[0] = X - 4; m.clip[1] = Y; m.clip[2] = W + 8; m.clip[3] = imax (1, h); }
					else if (a.dir == DIR_DOWN || a.dir == DIR_NONE) { int h = (int) (H * e); m.clip[0] = X - 4; m.clip[1] = Y + H - h; m.clip[2] = W + 8; m.clip[3] = imax (1, h); }
					else { int w = (int) (W * e); m.clip[0] = X - 4; m.clip[1] = Y - 4; m.clip[2] = imax (1, w + 4); m.clip[3] = H + 8; }
					break;
				}
				case FX_ZOOM: m.k = 0.3f + 0.7f * e; m.alpha = (int) (255 * fminf (1, t * 1.5f)); break;
				case FX_FLOAT: m.dy = (1 - e) * oh * 0.4f + (1 - e) * 40; m.alpha = (int) (255 * t); break;
				case FX_GROW: m.k = 0.1f + 0.9f * e; break;
				case FX_SPIN: m.rot = (int) (-360 * (1 - e)); m.alpha = (int) (255 * t); break;
				default: m.alpha = (int) (255 * t); break;
				}
			}
		}
		else if (a.cls == AC_EMPHASIS)
		{
			if (t >= 0 && t <= 1)
			{
				switch (a.fx)
				{
				case FX_SPIN: m.rot = (int) (360 * ease (t)); break;
				case FX_GROW: m.k = 1 + 0.18f * sinf (t * 3.14159265f); break;
				default: m.k = 1 + 0.09f * sinf (t * 6.2831853f * 1.0f) * (t < 0.5f ? 1 : 1); break;	// (pulse)
				}
			}
			else if (t > 1 && a.fx == FX_GROW) m.k = 1;
		}
		else
		{
			if (t > 1) m.alpha = 0;
			else if (t >= 0)
			{
				float e = ease (t);
				switch (a.fx)
				{
				case FX_FLY: m.dy = e * (g_deck.sh * sc + 2 * sy - o->y * sc + 10); break;
				case FX_ZOOM: m.k = 1 - 0.7f * e; m.alpha = (int) (255 * (1 - t)); break;
				case FX_SPIN: m.rot = (int) (360 * e); m.alpha = (int) (255 * (1 - t)); break;
				case FX_APPEAR: m.alpha = 0; break;
				default: m.alpha = (int) (255 * (1 - t)); break;
				}
			}
		}
		if (found) { for (int k = 0; k < n; k++) if (mv[k].id == o->id) mv[k] = m; }
		else mv[n++] = m;
	}
	return n;
}

// ---- the show ---------------------------------------------------------------------------------------------------------
struct Show
{
	Compositor Cs;
	int W, H;
	unsigned *tgt;				// the window's pixels
	float sc, sx, sy;
	bool presenter;
	unsigned startT;
	// the events of the frameless window
	long keyQ[16]; int keyN; bool click, rclick, closed;
	void pump (int ms)
	{
		long t = ui_event (ms);
		if (t <= 0 || ui_ev_field (&ui, EV_WIN) != (int) ui.win) return;
		if (t == WEV_CLOSE) { closed = true; return; }
		if (t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0)
		{
			int hid = ui_ev_field (&ui, EV_CODE);
			if (hid >= 0xE0) return;
			long k = key_code (hid, ui_ev_field (&ui, EV_CHAR));
			if (hid >= 0x1E && hid <= 0x27) k = hid == 0x27 ? '0' : '1' + (hid - 0x1E);	// (the digits, whatever the layout)
			if (k && keyN < 16) keyQ[keyN++] = k;
		}
		if (t == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1)
		{
			int b = ui_ev_field (&ui, EV_CODE);
			if (b & 2) rclick = true; else click = true;
		}
	}
	long next_key () { if (!keyN) return 0; long k = keyQ[0]; for (int i = 1; i < keyN; i++) keyQ[i - 1] = keyQ[i]; keyN--; return k; }
	// the slides shown (the hidden ones skipped)
	int next_shown (int i, int dir) { i += dir; while (i >= 0 && i < g_deck.slides.n && g_deck.slides[i]->hidden) i += dir; return i; }

	void place (int w, int h)
	{
		sc = fminf ((float) w / g_deck.sw, (float) h / g_deck.sh);
		sx = floorf ((w - g_deck.sw * sc) / 2); sy = floorf ((h - g_deck.sh * sc) / 2);
	}
	// A slide's frame at its state (step, ms) composited into tgt.
	void compose (int slide, int step, int ms, bool blank, unsigned blankC)
	{
		if (blank) { for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) tgt[(long) y * W + x] = blankC; return; }
		Slide &s = *g_deck.slides[slide];
		Frame F; Cs.frame++;
		build_frame (Cs, g_deck, s, slide, sc, false, F);
		FxTime *ft = new FxTime[128]; fx_plan (s, ft);
		Move mv[128]; int n = fx_moves (s, ft, step, ms, sc, sx, sy, mv, 128);
		delete[] ft;
		composite_frame (F, tgt, W, H, W, sx, sy, 0x000000, mv, n);
		Cs.sweep (6);
	}
	void present () { ui_update_all (); }
	// The slide as it ends (every effect done) / begins, into a texture for a transition.
	CTex *snapshot (int slide, int step, int ms)
	{
		compose (slide, step, ms, false, 0);
		unsigned *p = (unsigned *) malloc ((size_t) W * H * 4);
		if (!p) return 0;
		for (long i = 0; i < (long) W * H; i++) p[i] = tgt[i] | 0xFF000000u;
		return tex_new (p, W, H);
	}
	// The transition from texture A to B: t 0..1.
	void transition_frame (CTex *A, CTex *B, const Transition &tr, float t)
	{
		float e = ease (t);
		CLayer L[3]; int n = 0;
		auto layer = [&] (CTex *tx, float dx, float dy, float k, int alpha, int cx, int cy, int cw, int ch) -> CLayer & {
			CLayer &g = L[n++]; layer_init (g, tx);
			if (k == 1) g.m.translate (dx, dy);
			else { g.m.translate (W / 2.0f + dx, H / 2.0f + dy); g.m.scale (k, k); g.m.translate (-W / 2.0f, -H / 2.0f); }
			g.opacity = (unsigned) alpha; g.clip[0] = cx; g.clip[1] = cy; g.clip[2] = cw; g.clip[3] = ch;
			return g;
		};
		int dir = tr.dir ? tr.dir : DIR_LEFT;
		float ux = dir == DIR_LEFT ? 1 : dir == DIR_RIGHT ? -1 : 0, uy = dir == DIR_UP ? 1 : dir == DIR_DOWN ? -1 : 0;
		// (DIR_LEFT: the new slide comes from the right, going left)
		switch (tr.type)
		{
		case TR_FADE: case TR_DISSOLVE: layer (A, 0, 0, 1, 255, 0, 0, 0, 0); layer (B, 0, 0, 1, (int) (255 * e), 0, 0, 0, 0); break;
		case TR_PUSH: layer (A, -ux * W * e, -uy * H * e, 1, 255, 0, 0, 0, 0); layer (B, ux * W * (1 - e), uy * H * (1 - e), 1, 255, 0, 0, 0, 0); break;
		case TR_COVER: layer (A, 0, 0, 1, 255, 0, 0, 0, 0); layer (B, ux * W * (1 - e), uy * H * (1 - e), 1, 255, 0, 0, 0, 0); break;
		case TR_UNCOVER: layer (B, 0, 0, 1, 255, 0, 0, 0, 0); layer (A, -ux * W * e, -uy * H * e, 1, 255, 0, 0, 0, 0); break;
		case TR_WIPE:
		{
			layer (A, 0, 0, 1, 255, 0, 0, 0, 0);
			int w = (int) (W * e), h = (int) (H * e);
			if (dir == DIR_LEFT) layer (B, 0, 0, 1, 255, W - w, 0, imax (1, w), H);
			else if (dir == DIR_RIGHT) layer (B, 0, 0, 1, 255, 0, 0, imax (1, w), H);
			else if (dir == DIR_UP) layer (B, 0, 0, 1, 255, 0, H - h, W, imax (1, h));
			else layer (B, 0, 0, 1, 255, 0, 0, W, imax (1, h));
			break;
		}
		case TR_SPLIT: { layer (A, 0, 0, 1, 255, 0, 0, 0, 0); int w = (int) (W * e); layer (B, 0, 0, 1, 255, (W - w) / 2, 0, imax (1, w), H); break; }
		case TR_ZOOM: layer (A, 0, 0, 1 + 0.15f * e, (int) (255 * (1 - e)), 0, 0, 0, 0); layer (B, 0, 0, 0.6f + 0.4f * e, (int) (255 * e), 0, 0, 0, 0); break;
		default: layer (B, 0, 0, 1, 255, 0, 0, 0, 0); break;
		}
		c_composite (tgt, W, H, W, L, n, 0x000000);
	}

	// ---- the presenter's console ----------------------------------------------------------------------------------
	void text (Surf &cv, int x, int y, const char *s, int px, unsigned c, bool bold = false)
	{
		mask_begin (); surf_text (cv, bold ? UFN_BOLD : UFN_SANS, (float) x, y + px, s, px, c); mask_end ();
	}
	int text_w (const char *s, int px, bool bold = false) { return (int) ui_text_width ((long) s, px, face_font (bold ? UFN_BOLD : UFN_SANS)); }
	void console (int slide, int step, int ms, int steps, bool blank, unsigned blankC, int next)
	{
		unsigned BG = 0x1A1C20, BG2 = 0x282B30, FG = 0xECECEC, FG2 = 0x969AA0, PEACH = 0xF0A86E;
		for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) tgt[(long) y * W + x] = BG;
		// the current slide, at the left
		int cw = W * 59 / 100, ch = cw * g_deck.sh / g_deck.sw, cx = 24, cy = 72;
		if (cy + ch > H * 62 / 100) { ch = H * 62 / 100 - cy; cw = ch * g_deck.sw / g_deck.sh; }
		float osc = sc, osx = sx, osy = sy;
		sc = (float) cw / g_deck.sw; sx = (float) cx; sy = (float) cy;
		if (blank) { for (int y = cy; y < cy + ch; y++) for (int x = cx; x < cx + cw; x++) tgt[(long) y * W + x] = blankC; }
		else
		{
			Slide &s = *g_deck.slides[slide];
			Frame F; Cs.frame++;
			build_frame (Cs, g_deck, s, slide, sc, false, F);
			FxTime *ft = new FxTime[128]; fx_plan (s, ft);
			Move mv[128]; int n = fx_moves (s, ft, step, ms, sc, sx, sy, mv, 128);
			delete[] ft;
			composite_frame (F, tgt, W, H, W, sx, sy, 0, mv, n);
			// (composite_frame clears the whole target: the console's background drawn again round the slide)
			for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (x < cx || x >= cx + cw || y < cy || y >= cy + ch) tgt[(long) y * W + x] = BG;
		}
		Surf cv; cv.px = tgt; cv.w = W; cv.h = H; cv.stride = W; cv.pm = false; cv.clip_all ();
		// the next slide
		int nx = cx + cw + 24, nw = W - nx - 24, nh = nw * g_deck.sh / g_deck.sw;
		text (cv, nx, cy - 4, TR ("Next"), 14, FG2, true);
		if (next >= 0 && next < g_deck.slides.n)
		{
			unsigned *px = (unsigned *) malloc ((size_t) nw * nh * 4);
			Cs.frame++;
			if (px) flatten_slide (Cs, g_deck, *g_deck.slides[next], next, px, nw, nh, nw);
			if (px) for (int y = 0; y < nh && cy + 18 + y < H; y++) for (int x = 0; x < nw; x++) tgt[(long) (cy + 18 + y) * W + nx + x] = px[y * nw + x];
			free (px);
		}
		else text (cv, nx, cy + 30, TR ("The end of the show"), 16, FG2);
		// the steps still to come on this slide
		Slide &s = *g_deck.slides[slide];
		char t[160];
		int ey = cy + 18 + nh + 26;
		if (s.anim.n) { snprintf (t, sizeof t, TR ("Effects: %d of %d clicks played"), imin (step, steps), steps); text (cv, nx, ey, t, 13, FG2); }
		// the top bar: where we are, the time, the clock
		for (int y = 0; y < 48; y++) for (int x = 0; x < W; x++) tgt[(long) y * W + x] = BG2;
		int shown = 0, at = 0; for (int i = 0; i < g_deck.slides.n; i++) if (!g_deck.slides[i]->hidden) { shown++; if (i <= slide) at++; }
		snprintf (t, sizeof t, TR ("Slide %d of %d"), at, shown); text (cv, 20, 13, t, 20, FG, true);
		const char *sec = section_name (slide); if (sec[0]) { snprintf (t, sizeof t, "\xC2\xB7  %s", sec); text (cv, 30 + text_w (TR ("Slide 00 of 00"), 20, true), 16, t, 15, FG2); }
		unsigned el = (ticks () - startT) / 100;
		snprintf (t, sizeof t, "%02u:%02u:%02u", el / 3600, el / 60 % 60, el % 60);
		text (cv, (W - text_w (t, 28, true)) / 2, 8, t, 28, FG, true);
		{ time_t now = time (0); struct tm *m = localtime (&now); if (m) { snprintf (t, sizeof t, "%02d:%02d", m->tm_hour, m->tm_min); text (cv, W - 20 - text_w (t, 20, true), 13, t, 20, FG2, true); } }
		for (int x = 0; x < W * at / imax (1, shown); x++) for (int y = 48; y < 52; y++) tgt[(long) y * W + x] = PEACH;
		// the notes
		int ny = cy + ch + 28, nh2 = H - ny - 20;
		for (int y = ny; y < ny + nh2; y++) for (int x = 20; x < W - 20; x++) tgt[(long) y * W + x] = BG2;
		text (cv, 32, ny + 8, TR ("Notes"), 13, FG2, true);
		Buf nb; s.notes.text_utf8 (nb);
		int ly = ny + 32, px = H >= 900 ? 22 : 18;
		const char *p = nb.str ();
		while (*p && ly + px < ny + nh2)
		{
			// a line: the words that fit
			const char *e = p, *cut = 0;
			while (*e && *e != '\n')
			{
				char line[512]; int n = (int) (e - p) + 1; if (n > 510) break;
				memcpy (line, p, n); line[n] = 0;
				if (text_w (line, px) > W - 80) break;
				if (*e == ' ') cut = e;
				e++;
			}
			if (*e && *e != '\n' && cut) e = cut;
			char line[512]; int n = imin ((int) (e - p), 510); memcpy (line, p, n); line[n] = 0;
			text (cv, 36, ly, line, px, FG);
			ly += px + px / 2;
			p = e; while (*p == ' ') p++; if (*p == '\n') p++;
		}
		text (cv, 24, H - 18, TR ("Esc: end   \xC2\xB7   Space / \xE2\x86\x92: next   \xC2\xB7   \xE2\x86\x90: back   \xC2\xB7   B / W: black / white screen   \xC2\xB7   number + Enter: go to a slide"), 12, FG2);
		sc = osc; sx = osx; sy = osy;
	}
	static const char *section_name (int slide) { for (int k = slide; k >= 0; k--) if (g_deck.slides[k]->section[0]) return g_deck.slides[k]->section; return ""; }

	// ---- the loop ------------------------------------------------------------------------------------------------------
	void run (int from, bool presenterView)
	{
		if (!g_deck.slides.n) return;
		presenter = presenterView;
		long scr[4] = { 0, 0, 0, 0 };
		ui_screen ((long) scr);
		ui_dialog_at (0, 0);
		long dlg = ui_dialog_begin (scr[0], scr[1], (long) "Slides");
		if (!dlg) return;
		W = (int) ui.w; H = (int) ui.h; tgt = ui.surf;
		keyN = 0; click = rclick = closed = false;
		place (W, H);
		startT = ticks ();
		FxTime *ftp = new FxTime[128]; FxTime *ft = ftp;
		int slide = from; if (g_deck.slides[slide]->hidden) slide = next_shown (slide, 1);
		if (slide < 0 || slide >= g_deck.slides.n) slide = next_shown (-1, 1);
		if (slide < 0 || slide >= g_deck.slides.n) { delete[] ftp; ui_dialog_end (dlg); return; }
		int steps = fx_plan (*g_deck.slides[slide], ft);
		bool firstAuto = g_deck.slides[slide]->anim.n && g_deck.slides[slide]->anim[0].start != ST_CLICK;
		int step = firstAuto ? 0 : -1;			// -1: the slide shown, its first click's effects not begun
		unsigned stepT = ticks (), slideDoneT = 0;
		bool blank = false; unsigned blankC = 0; int typed = 0;
		bool redraw = true, ended = false;
		for (;;)
		{
			pump (redraw ? 0 : 10);
			if (closed) break;
			unsigned now = ticks ();
			int ms = (int) (now - stepT) * 10;
			long k = next_key ();
			bool next = false, back = false;
			if (click) { click = false; next = true; }
			if (rclick) { rclick = false; back = true; }
			if (k == K_ESC) break;
			if (k == ' ' || k == K_ENTER || k == K_RIGHT || k == K_DOWN || k == K_PGDN || k == 'n' || k == 'N')
			{
				if (k == K_ENTER && typed > 0) { int target = -1, shownI = 0; for (int i = 0; i < g_deck.slides.n; i++) if (!g_deck.slides[i]->hidden && ++shownI == typed) target = i; typed = 0; if (target >= 0) { slide = target; steps = fx_plan (*g_deck.slides[slide], ft); step = 99; stepT = now; ended = false; redraw = true; } continue; }
				next = true;
			}
			if (k == K_LEFT || k == K_UP || k == K_PGUP || k == K_BACK || k == 'p' || k == 'P') back = true;
			if (k >= '0' && k <= '9') { typed = typed * 10 + (int) (k - '0'); continue; }
			if (k == 'b' || k == 'B' || k == '.') { blank = !blank; blankC = 0; redraw = true; }
			if (k == 'w' || k == 'W' || k == ',') { blank = !blank; blankC = 0xFFFFFF; redraw = true; }
			if (k == K_HOME) { slide = next_shown (-1, 1); steps = fx_plan (*g_deck.slides[slide], ft); step = -1; stepT = now; ended = false; redraw = true; }
			if (k == K_END) { int l = next_shown (g_deck.slides.n, -1); if (l >= 0) { slide = l; steps = fx_plan (*g_deck.slides[slide], ft); step = 99; ended = false; redraw = true; } }
			if (blank && (next || back)) { blank = false; redraw = true; next = back = false; }
			if (ended)
			{
				if (next || k) break;
				if (back) { ended = false; step = 99; redraw = true; }
				else continue;
			}
			// the step playing: its end
			int stepEnd = 0;
			if (step >= 0 && step < steps) for (int i = 0; i < g_deck.slides[slide]->anim.n; i++) { if (ft[i].step == step && ft[i].t1 > stepEnd && !ft[i].np) stepEnd = ft[i].t1; for (int k = 0; k < ft[i].np; k++) if (ft[i].pstep[k] == step && ft[i].pt1[k] > stepEnd) stepEnd = ft[i].pt1[k]; }
			bool playing = step >= 0 && step < steps && ms < stepEnd + 20;
			if (next)
			{
				if (playing) { stepT = now - (unsigned) (stepEnd / 10 + 3); redraw = true; }		// (the effect finished at once)
				else if (step + 1 < steps) { step++; stepT = now; redraw = true; }
				else
				{
					int ns = next_shown (slide, 1);
					if (ns >= g_deck.slides.n) { ended = true; draw_end (); continue; }
					go_to (slide, step, ns, &steps, ft);
					slide = ns; stepT = ticks (); slideDoneT = 0;
					step = g_deck.slides[slide]->anim.n && g_deck.slides[slide]->anim[0].start != ST_CLICK ? 0 : -1;
					redraw = true;
				}
			}
			else if (back)
			{
				int ps = next_shown (slide, -1);
				if (ps >= 0) { slide = ps; steps = fx_plan (*g_deck.slides[slide], ft); step = 99; stepT = now; redraw = true; }
			}
			// an automatic step (After previous chains are timed inside a step); the slide going on by itself
			if (!playing && step + 1 < steps && step >= 0 && g_deck.slides[slide]->anim[first_of (*g_deck.slides[slide], ft, step + 1)].start != ST_CLICK) { step++; stepT = now; redraw = true; }
			Slide &cs = *g_deck.slides[slide];
			bool allDone = step >= steps - 1 && !playing;
			if (allDone && cs.tr.after >= 0)
			{
				if (!slideDoneT) slideDoneT = now;
				if ((int) (now - slideDoneT) * 10 >= cs.tr.after)
				{
					int ns = next_shown (slide, 1);
					if (ns >= g_deck.slides.n) { ended = true; draw_end (); continue; }
					go_to (slide, step, ns, &steps, ft);
					slide = ns; stepT = ticks (); slideDoneT = 0;
					step = g_deck.slides[slide]->anim.n && g_deck.slides[slide]->anim[0].start != ST_CLICK ? 0 : -1;
					redraw = true;
				}
			}
			if (playing || redraw || presenter)
			{
				int tms = (int) (ticks () - stepT) * 10;
				if (presenter) { static unsigned lastP; if (!(playing || redraw) && now - lastP < 100) continue; lastP = now; console (slide, step, tms, steps, blank, blankC, next_shown (slide, 1)); }
				else compose (slide, step, tms, blank, blankC);
				present ();
				redraw = false;
			}
		}
		delete[] ftp;
		Cs.drop_all ();
		ui_dialog_end (dlg);
	}
	static int first_of (const Slide &s, const FxTime *ft, int step) { for (int i = 0; i < s.anim.n; i++) { for (int k = 1; k < ft[i].np; k++) if (ft[i].pstep[k] == step) return i; if (ft[i].step == step) return i; } return 0; }
	// From one slide (as it ends) to the next (as it begins): its transition played.
	void go_to (int from, int step, int to, int *steps, FxTime *ft)
	{
		const Transition &tr = g_deck.slides[to]->tr;
		*steps = fx_plan (*g_deck.slides[to], ft);
		if (tr.type == TR_NONE || presenter) return;
		(void) step;
		CTex *A = snapshot (from, 99, 1 << 20);
		int st0 = g_deck.slides[to]->anim.n && g_deck.slides[to]->anim[0].start != ST_CLICK ? 0 : -1;
		CTex *B = snapshot (to, st0, 0);
		unsigned t0 = ticks ();
		int dur = imax (100, tr.dur);
		while (A && B)
		{
			pump (0);
			float t = (float) ((ticks () - t0) * 10) / dur;
			if (t > 1) t = 1;
			transition_frame (A, B, tr, t);
			present ();
			if (t >= 1 || keyN || click || closed) break;
		}
		tex_free (A); tex_free (B);
	}
	void draw_end ()
	{
		for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) tgt[(long) y * W + x] = 0;
		Surf cv; cv.px = tgt; cv.w = W; cv.h = H; cv.stride = W; cv.pm = false; cv.clip_all ();
		const char *t = TR ("The end of the show. A click or a key: back.");
		text (cv, (W - text_w (t, 18)) / 2, H / 2 - 12, t, 18, 0xA0A0A0);
		present ();
	}
};

} // namespace sl

#endif
