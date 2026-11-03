//
// tiles.cpp -- see tiles.h. A tile's key holds its position only: changing the zoom starts a new generation, and the old
// tiles go. compose () shows the old picture scaled meanwhile: main.cpp keeps it.
//
#include "tiles.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"
#include <algorithm>
#include <cstring>

namespace vr {

static int64_t key_of (int x, int y) { return (int64_t) x << 32 | (uint32_t) y; }

TileCache::TileCache (int n, size_t b) : workers (n), budget (b)
{
	for (int i = 0; i < workers; i++) pool.emplace_back ([this] { worker (); });
}
TileCache::~TileCache ()
{
	{
		std::lock_guard<std::mutex> l (mx);
		stop = true;
	}
	cv.notify_all ();
	for (auto &t : pool) t.join ();
	for (auto &p : map) { free (p.second->px); delete p.second; }
	for (auto *t : doomed) { free (t->px); delete t; }
	for (auto *p : freePx) free (p);
}

void TileCache::paint (Tile *, const Scene &s, const ViewState &v, uint32_t *buf)
{
	(void) s; (void) v; (void) buf;
}

static void paint_tile (const Scene &s, const ViewState &v, int tx, int ty, uint32_t *buf)
{
	SkImageInfo ii = SkImageInfo::MakeN32Premul (TILE, TILE);
	sk_sp<SkSurface> surf = SkSurfaces::WrapPixels (ii, buf, TILE * 4);
	SkCanvas *c = surf->getCanvas ();
	c->clear (SkColorSetRGB ((v.desk >> 16) & 255, (v.desk >> 8) & 255, v.desk & 255));
	c->translate ((float) -(tx * (double) TILE), (float) -(ty * (double) TILE));
	c->scale ((float) v.zoom, (float) v.zoom);
	SkRect page = SkRect::MakeWH ((float) s.width, (float) s.height);
	SkPaint pg;
	pg.setColor (SkColorSetRGB ((s.page >> 16) & 255, (s.page >> 8) & 255, s.page & 255));
	c->drawRect (page, pg);
	if (v.outline) draw_outlines (c, s); else draw_scene (c, s);
}

void TileCache::worker ()
{
	for (;;)
	{
		Tile *t = nullptr;
		std::shared_ptr<const Scene> sc;
		ViewState v;
		unsigned g;
		{
			std::unique_lock<std::mutex> l (mx);
			cv.wait (l, [this] { return stop || !queue.empty (); });
			if (stop) return;
			// the nearest to the middle
			size_t best = 0;
			for (size_t i = 1; i < queue.size (); i++) if (queue[i].dist < queue[best].dist) best = i;
			t = queue[best].t;
			queue[best] = queue.back ();
			queue.pop_back ();
			t->state = 1;
			sc = scene; v = tileView; g = gen;
			active++;
		}
		uint32_t *buf = (uint32_t *) malloc (TILE * TILE * 4);
		paint_tile (*sc, v, t->tx, t->ty, buf);
		{
			std::lock_guard<std::mutex> l (mx);
			active--;
			if (g == t->gen && t->gen == gen)
			{
				if (t->px) freePx.push_back (t->px);
				t->px = buf;
				t->state = 2;
				nRendered++;
				fresh = true;
			}
			else
			{
				free (buf);
				t->state = 3;		// (a newer change: asked again by the next compose)
				if (t->gen != gen) t->key = 0;
			}
			for (size_t i = 0; i < doomed.size (); )
				if (doomed[i]->state != 1) { free (doomed[i]->px); delete doomed[i]; doomed[i] = doomed.back (); doomed.pop_back (); }
				else i++;
			if (queue.empty () && active == 0) idle.notify_all ();
		}
	}
}

bool TileCache::pending ()
{
	return fresh.exchange (false);
}

void TileCache::wait_idle ()
{
	std::unique_lock<std::mutex> l (mx);
	idle.wait (l, [this] { return queue.empty () && active == 0; });
}

void TileCache::drop_all ()
{
	queue.clear ();
	for (auto &p : map)
	{
		Tile *t = p.second;
		if (t->state == 1) doomed.push_back (t);
		else { free (t->px); delete t; }
	}
	map.clear ();
	lru.clear ();
	total = 0;
}

void TileCache::set_scene (std::shared_ptr<const Scene> s, const vg::Box *damage)
{
	std::lock_guard<std::mutex> l (mx);
	bool all = !damage || !scene;
	scene = s;
	if (all)
	{
		// the tiles stay shown until replaced: only their generation goes stale
		gen++;
		queue.clear ();
		for (auto &p : map) { p.second->gen = gen; if (p.second->state != 1) p.second->state = 3; }
		return;
	}
	if (damage->empty ()) return;
	double z = tileView.zoom, k = z / TILE;
	int x0 = (int) std::floor (damage->x0 * k), y0 = (int) std::floor (damage->y0 * k);
	int x1 = (int) std::floor ((damage->x1 + 1 / z) * k), y1 = (int) std::floor ((damage->y1 + 1 / z) * k);
	// (a stale tile keeps its pixels and shows them until the new ones come)
	gen++;
	queue.clear ();
	for (auto &p : map)
	{
		Tile *t = p.second;
		bool hit = t->tx >= x0 && t->tx <= x1 && t->ty >= y0 && t->ty <= y1;
		t->gen = gen;
		if (t->state == 1) continue;
		if (hit || t->state != 2) t->state = 3;
	}
}

void TileCache::evict ()
{
	while (total > budget && !lru.empty ())
	{
		Tile *t = lru.back ();
		if (t->state == 1) break;
		lru.pop_back ();
		map.erase (key_of (t->tx, t->ty));
		free (t->px);
		delete t;
		total -= TILE * TILE * 4;
	}
}

bool TileCache::compose (const ViewState &v, uint32_t *px, int w, int h, int stride)
{
	std::lock_guard<std::mutex> l (mx);
	if (!scene) return true;
	if (v.zoom != tileView.zoom || v.desk != tileView.desk || v.outline != tileView.outline) { drop_all (); gen++; }
	tileView = v;
	bool complete = true, added = false;
	// the page's origin in tile space: tiles sit on the page's grid
	int ox = (int) std::floor (v.ox), oy = (int) std::floor (v.oy);
	int tx0 = (int) std::floor ((0.0 - ox) / TILE), ty0 = (int) std::floor ((0.0 - oy) / TILE);
	int tx1 = (int) std::floor ((w - 1.0 - ox) / TILE), ty1 = (int) std::floor ((h - 1.0 - oy) / TILE);
	double cx = (w / 2.0 - ox) / TILE, cy = (h / 2.0 - oy) / TILE;
	for (int ty = ty0; ty <= ty1; ty++)
		for (int tx = tx0; tx <= tx1; tx++)
		{
			int64_t k = key_of (tx, ty);
			auto f = map.find (k);
			Tile *t = nullptr;
			if (f != map.end ())
			{
				t = f->second;
				lru.splice (lru.begin (), lru, t->lru);
			}
			else
			{
				t = new Tile ();
				t->key = k; t->tx = tx; t->ty = ty; t->gen = gen;
				lru.push_front (t);
				t->lru = lru.begin ();
				map[k] = t;
				total += TILE * TILE * 4;
				t->state = 3;
			}
			if (t->state == 3 || t->gen != gen)
			{
				t->gen = gen;
				t->state = 0;
				queue.push_back ({ t, std::hypot (tx + 0.5 - cx, ty + 0.5 - cy) });
				added = true;
			}
			if (t->state != 2) complete = false;
			if (t->px)
			{
				int dx = ox + tx * TILE, dy = oy + ty * TILE;
				int sx = dx < 0 ? -dx : 0, sy = dy < 0 ? -dy : 0;
				int cw = std::min (TILE - sx, w - (dx + sx)), ch = std::min (TILE - sy, h - (dy + sy));
				for (int y = 0; y < ch; y++)
					memcpy (px + (long) (dy + sy + y) * stride + dx + sx, t->px + (long) (sy + y) * TILE + sx, (size_t) cw * 4);
			}
		}
	if (added) cv.notify_all ();
	evict ();
	return complete;
}

bool render_page (const Scene &s, double scale, uint32_t *px, int w, int h, uint32_t bg, bool transparent)
{
	SkImageInfo ii = SkImageInfo::MakeN32Premul (w, h);
	sk_sp<SkSurface> surf = SkSurfaces::WrapPixels (ii, px, (size_t) w * 4);
	if (!surf) return false;
	SkCanvas *c = surf->getCanvas ();
	c->clear (transparent ? SK_ColorTRANSPARENT : SkColorSetRGB ((bg >> 16) & 255, (bg >> 8) & 255, bg & 255));
	c->scale ((float) scale, (float) scale);
	draw_scene (c, s);
	return true;
}

}
