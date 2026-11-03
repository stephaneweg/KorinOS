//
// tiles.h -- the drawing cut in 256 x 256 tiles at the view's zoom, painted by worker threads, kept in a cache. The main
// thread never waits: it shows what is ready, and asks again when the workers say more is. Only the main thread calls
// UIKit; the workers touch nothing but tile memory and the immutable scene.
//
#ifndef _vector_tiles_h
#define _vector_tiles_h

#include "scene.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace vr {

enum { TILE = 256 };

struct ViewState
{
	double zoom = 1;			// device px per page px
	double ox = 0, oy = 0;			// the page's origin in the view (device px)
	uint32_t desk = 0x808080;		// around the page
	bool outline = false;			// contours only
};

class TileCache
{
public:
	explicit TileCache (int workers, size_t budget = 64u << 20);
	~TileCache ();
	void set_scene (std::shared_ptr<const Scene> s, const vg::Box *damage);	// damage in page px; nullptr: everything
	// paints the view into px (w x h, stride in pixels); tiles missing are asked for; -> true when complete
	bool compose (const ViewState &v, uint32_t *px, int w, int h, int stride);
	bool pending ();			// workers have something to give
	int rendered () const { return nRendered.load (); }
	void wait_idle ();			// (for tests and the bench)

private:
	struct Tile
	{
		uint32_t *px = nullptr;
		int64_t key = 0;
		std::atomic<int> state { 0 };		// 0 queued, 1 painting, 2 ready
		unsigned gen = 0;
		std::list<Tile *>::iterator lru;
		int tx = 0, ty = 0;
	};
	struct Job { Tile *t; double dist; };

	int workers;
	size_t budget;
	std::vector<std::thread> pool;
	std::mutex mx;
	std::condition_variable cv, idle;
	std::vector<Job> queue;
	std::unordered_map<int64_t, Tile *> map;
	std::list<Tile *> lru;
	std::vector<uint32_t *> freePx;
	std::vector<Tile *> doomed;
	std::shared_ptr<const Scene> scene;
	ViewState tileView;
	unsigned gen = 1;
	double zoomOf = 0;
	bool stop = false;
	int active = 0;
	std::atomic<int> nRendered { 0 };
	std::atomic<bool> fresh { false };
	size_t total = 0;

	void worker ();
	void paint (Tile *t, const Scene &s, const ViewState &v, uint32_t *buf);
	void evict ();
	void drop_all ();
};

// the same drawing in one go, for export and tests: page px * scale
bool render_page (const Scene &s, double scale, uint32_t *px, int w, int h, uint32_t background, bool transparent);

}

#endif
