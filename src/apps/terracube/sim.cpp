// sim.cpp -- TerraCube : les liquides et les circuits, pas à pas (sim.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "sim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <deque>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "items.h"

namespace tc {
namespace sim {

void (*soundHook) (int, int, int, int, int) = nullptr;
void (*blastHook) (double, double, double, float) = nullptr;
void (*dropHook) (int, int, int, int) = nullptr;
void (*pushHook) (int, int, int, int) = nullptr;
bool (*occupiedHook) (int, int, int) = nullptr;

namespace {

struct P {
	int x, y, z;
};
inline bool operator== (const P &a, const P &b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool operator!= (const P &a, const P &b) { return !(a == b); }
struct PH {
	size_t operator() (const P &p) const
	{
		return (size_t) ((uint32_t) p.x * 73856093u ^ (uint32_t) p.y * 19349663u ^ (uint32_t) p.z * 83492791u);
	}
};
inline P nb (P p, int d) { return { p.x + DX[d], p.y + DY[d], p.z + DZ[d] }; }
const int HORIZ[4] = { D_EAST, D_WEST, D_SOUTH, D_NORTH };

World *W;
long g_tick = 0;
double g_acc = 0;
float g_day = 1;
double g_px, g_py = -999, g_pz;		// le joueur (les plaques)
std::multimap<long, P> g_sched;		// les pas programmés
std::unordered_map<P, long, PH> g_schedAt;
std::deque<P> g_updates;		// les mises à jour en attente (tout de suite)
std::unordered_set<P, PH> g_pending;
int g_quiet = 0;			// (pendant un déplacement : les changements ne préviennent pas)
std::unordered_set<P, PH> g_netDone;	// les fils déjà calculés depuis le dernier changement
std::unordered_map<P, std::vector<long>, PH> g_torchHist;	// (la torche qui grille : trop de changements)
std::unordered_map<P, long, PH> g_torchBurnt;
std::unordered_map<P, int, PH> g_fuse;	// la mèche des dynamites allumées (pas)

int get (P p) { return W->get (p.x, p.y, p.z); }
int meta (P p) { return W->meta (p.x, p.y, p.z); }
bool loaded (P p) { return p.y >= 0 && p.y < CH && W->loaded (p.x, p.z); }
void setB (P p, int b, int m = 0)
{
	if (loaded (p)) W->set (p.x, p.y, p.z, b, m);
}
void setM (P p, int m) { W->setMeta (p.x, p.y, p.z, m); }
void sound (int s, int arg, P p)
{
	if (soundHook) soundHook (s, arg, p.x, p.y, p.z);
}

void schedule (P p, int delay)
{
	if (g_schedAt.count (p)) return;
	long t = g_tick + std::max (1, delay);
	g_schedAt[p] = t;
	g_sched.insert ({ t, p });
}

void update (P p)
{
	if (p.y < 0 || p.y >= CH) return;
	if (g_pending.insert (p).second) g_updates.push_back (p);
}

// ---- les sortes de blocs ------------------------------------------------------------------------------------------

// un bloc plein qui conduit (qui peut être alimenté) ; les pistons, l'observateur, le bloc de cuivre non
bool conductor (int b) { return isOpaque (b) && b != PISTON && b != STICKY_PISTON && b != OBSERVER && b != COPPER_BLOCK; }
// un bloc sur lequel poser du fil, une plaque, un répéteur
bool supportTop (int b) { return info (b).shape == SH_CUBE && isSolid (b); }
// un bloc contre lequel poser une torche, un levier, un bouton
bool supportSide (int b) { return info (b).shape == SH_CUBE && isOpaque (b); }
// cassé par un liquide ou un piston (les plantes, le fil, les petits composants)
bool fragile (int b)
{
	int s = info (b).shape;
	return s == SH_CROSS || (s == SH_SPECIAL && b != PISTON_HEAD);
}
bool immovable (int b, int m)
{
	return b == BEDROCK || b == OBSIDIAN || b == FURNACE || b == PISTON_HEAD || ((b == PISTON || b == STICKY_PISTON) && (m & M_ON));
}

void notifyAround (P p, bool skipWires = false);

// un bloc qui ne tient plus : enlevé (ce qui tombe va au joueur proche, main.cpp)
void drop (P p)
{
	int b = get (p);
	setB (p, AIR);
	Mining mi = mining (b);
	if (dropHook && mi.drop) dropHook (mi.drop, p.x, p.y, p.z);
}

// ---- la puissance -------------------------------------------------------------------------------------------------

bool wireConnects (P q, int h);

// le fil en q pointe-t-il vers h ? (un fil seul pointe partout ; relié d'un seul côté, tout droit)
bool wirePoints (P q, int h)
{
	int cons = 0, n = 0;
	for (int k : HORIZ)
		if (wireConnects (q, k)) cons |= 1 << k, n++;
	if (n == 0) return true;
	if (n == 1) {
		int k = __builtin_ctz ((unsigned) cons);
		return h == k || h == opposite (k);
	}
	return (cons >> h) & 1;
}

// la puissance faible qu'un composant en q donne vers d (de q vers la case alimentée)
int emitted (P q, int d)
{
	int b = get (q), m = meta (q);
	switch (b) {
	case COPPER_BLOCK: return 15;
	case LEVER: case STONE_BUTTON: case WOOD_BUTTON: case STONE_PLATE: case WOOD_PLATE: return (m & M_ON) ? 15 : 0;
	case COPPER_TORCH: return !(m & M_ON) && d != dirOf (m) ? 15 : 0;
	case REPEATER: return (m & M_POW) && d == dirOf (m) ? 15 : 0;
	case COMPARATOR: return d == dirOf (m) ? m >> 4 : 0;
	case OBSERVER: return (m & M_ON) && d == opposite (dirOf (m)) ? 15 : 0;
	case DAYLIGHT_SENSOR: return m >> 4;
	case WIRE:
		if (d == D_UP) return 0;
		return d == D_DOWN || wirePoints (q, d) ? (m & 15) : 0;
	}
	return 0;
}

// la puissance forte : celle qui traverse un bloc plein jusqu'au fil voisin
int strongEmitted (P q, int d)
{
	int b = get (q), m = meta (q);
	switch (b) {
	case LEVER: case STONE_BUTTON: case WOOD_BUTTON: return (m & M_ON) && d == dirOf (m) ? 15 : 0;
	case STONE_PLATE: case WOOD_PLATE: return (m & M_ON) && d == D_DOWN ? 15 : 0;
	case COPPER_TORCH: return !(m & M_ON) && d == D_UP ? 15 : 0;
	case REPEATER: case COMPARATOR: case OBSERVER: return emitted (q, d);
	}
	return 0;
}

int blockStrong (P p)
{
	int best = 0;
	for (int d = 0; d < 6; d++) best = std::max (best, strongEmitted (nb (p, d), opposite (d)));
	return best;
}

// un bloc plein : alimenté fort, ou faible par du fil qui pointe dedans
int blockPower (P p)
{
	int best = blockStrong (p);
	for (int d = 0; d < 6 && best < 15; d++) {
		P q = nb (p, d);
		if (get (q) == WIRE) best = std::max (best, emitted (q, opposite (d)));
	}
	return best;
}

// ce qui arrive en p par sa face d, pour un composant (lampe, piston, répéteur…)
int powerFrom (P p, int d)
{
	P q = nb (p, d);
	int b = get (q);
	if (conductor (b)) return blockPower (q);
	return emitted (q, opposite (d));
}

int inputPower (P p, int skip = -1)
{
	int best = 0;
	for (int d = 0; d < 6 && best < 15; d++)
		if (d != skip) best = std::max (best, powerFrom (p, d));
	return best;
}

// ---- le fil -------------------------------------------------------------------------------------------------------

// un composant vers lequel le fil se tourne (h : du fil vers lui)
bool connectsTo (int b, int m, int h)
{
	switch (b) {
	case COPPER_BLOCK: case COPPER_TORCH: case LEVER: case STONE_BUTTON: case WOOD_BUTTON: case STONE_PLATE: case WOOD_PLATE:
	case DAYLIGHT_SENSOR: return true;
	case REPEATER: case COMPARATOR: return dirOf (m) == h || dirOf (m) == opposite (h);
	case OBSERVER: return dirOf (m) == h;	// (son dos vers le fil)
	}
	return false;
}

bool wireConnects (P q, int h)
{
	P s = nb (q, h);
	int b = get (s);
	if (b == WIRE || connectsTo (b, meta (s), h)) return true;
	if (!isOpaque (b)) return get (nb (s, D_DOWN)) == WIRE;			// (il descend)
	return !isOpaque (get (nb (q, D_UP))) && get (nb (s, D_UP)) == WIRE;	// (il monte)
}

void wireLinks (P q, std::vector<P> &out)
{
	bool upBlocked = isOpaque (get (nb (q, D_UP)));
	for (int h : HORIZ) {
		P s = nb (q, h);
		int b = get (s);
		if (b == WIRE) out.push_back (s);
		else if (!isOpaque (b)) {
			P d = nb (s, D_DOWN);
			if (get (d) == WIRE) out.push_back (d);
		} else if (!upBlocked) {
			P u = nb (s, D_UP);
			if (get (u) == WIRE) out.push_back (u);
		}
	}
}

int wireInput (P q)
{
	int best = 0;
	for (int d = 0; d < 6 && best < 15; d++) {
		P s = nb (q, d);
		int b = get (s);
		if (b == WIRE) continue;
		best = std::max (best, conductor (b) ? blockStrong (s) : emitted (s, opposite (d)));
	}
	return best;
}

// tout le réseau de fil qui touche start : sa puissance, de proche en proche (15 aux entrées, - 1 par bloc)
void wireNet (P start)
{
	if (g_netDone.count (start)) return;
	std::vector<P> nodes = { start };
	std::unordered_map<P, int, PH> idx;
	idx[start] = 0;
	std::vector<std::vector<int>> adj;
	for (size_t i = 0; i < nodes.size (); i++) {
		std::vector<P> l;
		wireLinks (nodes[i], l);
		adj.emplace_back ();
		for (P &n : l) {
			if (!loaded (n)) continue;
			auto it = idx.find (n);
			if (it == idx.end ()) {
				if (nodes.size () >= 8192) continue;
				it = idx.emplace (n, (int) nodes.size ()).first;
				nodes.push_back (n);
			}
			adj[i].push_back (it->second);
		}
	}
	size_t N = nodes.size ();
	std::vector<int> pw (N);
	std::vector<std::vector<int>> bucket (16);
	for (size_t i = 0; i < N; i++) {
		g_netDone.insert (nodes[i]);
		pw[i] = wireInput (nodes[i]);
		if (pw[i] > 0) bucket[pw[i]].push_back ((int) i);
	}
	for (int lv = 15; lv > 1; lv--)
		for (size_t k = 0; k < bucket[lv].size (); k++) {
			int i = bucket[lv][k];
			if (pw[i] != lv) continue;
			for (int j : adj[i])
				if (pw[j] < lv - 1) pw[j] = lv - 1, bucket[lv - 1].push_back (j);
		}
	std::vector<P> changed;
	g_quiet++;
	for (size_t i = 0; i < N; i++)
		if ((meta (nodes[i]) & 15) != pw[i]) {
			setM (nodes[i], pw[i]);
			changed.push_back (nodes[i]);
		}
	g_quiet--;
	for (P &c : changed) notifyAround (c, true);
}

// ---- les composants -----------------------------------------------------------------------------------------------

bool torchWants (P p)
{
	auto it = g_torchBurnt.find (p);
	if (it != g_torchBurnt.end ()) {
		if (g_tick < it->second) return false;
		g_torchBurnt.erase (it);
	}
	P s = nb (p, dirOf (meta (p)));
	return !(conductor (get (s)) && blockPower (s) > 0);
}

int repeaterIn (P p)
{
	int f = dirOf (meta (p));
	return powerFrom (p, opposite (f));
}

bool repeaterLocked (P p)
{
	int f = dirOf (meta (p));
	for (int h : HORIZ) {
		if (h == f || h == opposite (f)) continue;
		P q = nb (p, h);
		int b = get (q);
		if ((b == REPEATER || b == COMPARATOR) && emitted (q, opposite (h)) > 0) return true;
	}
	return false;
}

int compOut (P p)
{
	int m = meta (p), f = dirOf (m);
	int rear = powerFrom (p, opposite (f)), side = 0;
	for (int h : HORIZ) {
		if (h == f || h == opposite (f)) continue;
		P q = nb (p, h);
		int b = get (q);
		if (b == WIRE || b == REPEATER || b == COMPARATOR || b == COPPER_BLOCK) side = std::max (side, emitted (q, opposite (h)));
	}
	if (m & M_ON) return std::max (0, rear - side);	// soustraction
	return rear >= side ? rear : 0;
}

bool pistonPowered (P p) { return inputPower (p, dirOf (meta (p))) > 0; }

bool playerOn (P p)
{
	if (occupiedHook) return occupiedHook (p.x, p.y, p.z);
	return g_px + 0.3 > p.x && g_px - 0.3 < p.x + 1 && g_pz + 0.3 > p.z && g_pz - 0.3 < p.z + 1 && g_py >= p.y - 0.01 && g_py < p.y + 0.6;
}

void prime (P p, int fuse)
{
	setM (p, meta (p) | M_ON);
	g_fuse[p] = fuse;
	g_schedAt.erase (p);	// (une mèche plus courte remplace l'ancienne)
	schedule (p, std::min (fuse, 5));
	sound (SS_FUSE, 0, p);
}

void changed (P p);

void explode (P p)
{
	g_fuse.erase (p);
	setB (p, AIR);
	sound (SS_EXPLODE, 0, p);
	if (blastHook) blastHook (p.x + 0.5, p.y + 0.5, p.z + 0.5, 4.0f);
	const float R = 3.6f;
	std::vector<P> hit;
	std::vector<P> chain;
	g_quiet++;
	for (int dy = -4; dy <= 4; dy++)
		for (int dz = -4; dz <= 4; dz++)
			for (int dx = -4; dx <= 4; dx++) {
				float r = sqrtf ((float) (dx * dx + dy * dy + dz * dz));
				if (r > R) continue;
				P q = { p.x + dx, p.y + dy, p.z + dz };
				if (!loaded (q)) continue;
				int b = get (q);
				if (b == AIR || b == BEDROCK || b == OBSIDIAN || isLiquid (b)) continue;
				if (r > R - 1.3f && (rand () & 1)) continue;
				if (b == TNT) {
					chain.push_back (q);
					continue;
				}
				W->set (q.x, q.y, q.z, AIR);
				hit.push_back (q);
			}
	g_quiet--;
	for (P &q : hit) changed (q);
	for (P &q : chain)
		if (!(meta (q) & M_ON)) prime (q, 10 + rand () % 20);
}

void liquidTick (P p);
bool lavaMeetsWater (P p);
void extend (P p);
void retract (P p);

void playNote (P p)
{
	sound (SS_NOTE, meta (p) & 31, p);
}

// une mise à jour : le bloc regarde ses voisins
void onUpdate (P p)
{
	int b = get (p), m = meta (p);
	switch (b) {
	case WATER: case LAVA:
		if (b == LAVA && lavaMeetsWater (p)) return;
		schedule (p, b == WATER ? 5 : 30);
		break;
	case WIRE:
		if (!supportTop (get (nb (p, D_DOWN)))) {
			drop (p);
			return;
		}
		wireNet (p);
		break;
	case COPPER_TORCH: {
		if (!supportSide (get (nb (p, dirOf (m))))) {
			drop (p);
			return;
		}
		if (torchWants (p) != !(m & M_ON)) schedule (p, 2);
		break;
	}
	case LEVER: case STONE_BUTTON: case WOOD_BUTTON:
		if (!supportSide (get (nb (p, dirOf (m))))) drop (p);
		break;
	case STONE_PLATE: case WOOD_PLATE: case DAYLIGHT_SENSOR:
		if (!supportTop (get (nb (p, D_DOWN)))) drop (p);
		else if (b == DAYLIGHT_SENSOR) schedule (p, 20);
		break;
	case REPEATER: {
		if (!supportTop (get (nb (p, D_DOWN)))) {
			drop (p);
			return;
		}
		bool lock = repeaterLocked (p);
		if (lock != !!(m & M_LOCK)) {
			setM (p, lock ? m | M_LOCK : m & ~M_LOCK);
			m = meta (p);
		}
		if (lock) break;
		if ((repeaterIn (p) > 0) != !!(m & M_POW)) schedule (p, 2 * (((m >> 3) & 3) + 1));
		break;
	}
	case COMPARATOR:
		if (!supportTop (get (nb (p, D_DOWN)))) {
			drop (p);
			return;
		}
		if (compOut (p) != (m >> 4)) schedule (p, 2);
		break;
	case SIGNAL_LAMP: {
		bool on = inputPower (p) > 0;
		if (on && !(m & M_ON)) setM (p, m | M_ON);
		else if (!on && (m & M_ON)) schedule (p, 4);
		break;
	}
	case PISTON: case STICKY_PISTON:
		if ((m & M_ON) && get (nb (p, dirOf (m))) != PISTON_HEAD) {	// (la tête cassée)
			setM (p, m & ~M_ON);
			return;
		}
		if (pistonPowered (p) != !!(m & M_ON)) schedule (p, 1);
		break;
	case PISTON_HEAD: {
		P base = nb (p, opposite (dirOf (m)));
		int bb = get (base), bm = meta (base);
		if (!((bb == PISTON || bb == STICKY_PISTON) && (bm & M_ON) && dirOf (bm) == dirOf (m))) setB (p, AIR);
		break;
	}
	case NOTE_BLOCK: {
		bool pw = inputPower (p) > 0;
		if (pw && !(m & M_POW)) {
			setM (p, m | M_POW);
			playNote (p);
		} else if (!pw && (m & M_POW))
			setM (p, m & ~M_POW);
		break;
	}
	case TNT:
		if (!(m & M_ON) && inputPower (p) > 0) prime (p, 80);
		break;
	}
}

// un pas programmé
void onTick (P p)
{
	int b = get (p), m = meta (p);
	switch (b) {
	case WATER: case LAVA: liquidTick (p); break;
	case COPPER_TORCH: {
		bool want = torchWants (p), lit = !(m & M_ON);
		if (want == lit) break;
		if (want) {	// (se rallumer : trop souvent en peu de temps, elle grille un moment)
			auto &h = g_torchHist[p];
			h.push_back (g_tick);
			while (!h.empty () && h.front () < g_tick - 60) h.erase (h.begin ());
			if (h.size () >= 8) {
				g_torchBurnt[p] = g_tick + 160;
				h.clear ();
				schedule (p, 161);
				sound (SS_FIZZ, 0, p);
				break;
			}
		}
		setM (p, want ? m & ~M_ON : m | M_ON);
		break;
	}
	case REPEATER: {
		if (m & M_LOCK) break;
		bool in = repeaterIn (p) > 0, on = m & M_POW;
		if (on && !in) setM (p, m & ~M_POW);
		else if (!on) {
			setM (p, m | M_POW);
			if (!in) schedule (p, 2 * (((m >> 3) & 3) + 1));
		}
		break;
	}
	case COMPARATOR: {
		int o = compOut (p);
		if (o != (m >> 4)) setM (p, (m & 15) | o << 4);
		break;
	}
	case SIGNAL_LAMP:
		if ((m & M_ON) && inputPower (p) == 0) setM (p, m & ~M_ON);
		break;
	case PISTON: case STICKY_PISTON: {
		bool pw = pistonPowered (p), ext = m & M_ON;
		if (pw && !ext) extend (p);
		else if (!pw && ext) retract (p);
		break;
	}
	case STONE_BUTTON: case WOOD_BUTTON:
		if (m & M_ON) {
			setM (p, m & ~M_ON);
			sound (SS_CLICK, 0, p);
		}
		break;
	case STONE_PLATE: case WOOD_PLATE:
		if (m & M_ON) {
			if (playerOn (p)) schedule (p, 10);
			else {
				setM (p, m & ~M_ON);
				sound (SS_CLICK, 0, p);
			}
		}
		break;
	case OBSERVER:
		if (!(m & M_ON)) {
			setM (p, m | M_ON);
			schedule (p, 2);
		} else
			setM (p, m & ~M_ON);
		break;
	case DAYLIGHT_SENSOR: {
		int lvl = W->skyLit (p.x, p.y, p.z) ? (int) lroundf (15.0f * g_day) : 0;
		if (m & M_ON) lvl = 15 - lvl;
		if (lvl != (m >> 4)) setM (p, (m & 15) | lvl << 4);
		schedule (p, 20);
		break;
	}
	case TNT: {
		if (!(m & M_ON)) break;
		auto it = g_fuse.find (p);
		int left = it == g_fuse.end () ? 0 : it->second - 5;
		if (left <= 0) {
			explode (p);
			break;
		}
		g_fuse[p] = left;
		setM (p, m ^ 16);	// (elle clignote)
		schedule (p, std::min (left, 5));
		break;
	}
	}
}

void processUpdates (int budget)
{
	while (!g_updates.empty () && budget-- > 0) {
		P p = g_updates.front ();
		g_updates.pop_front ();
		g_pending.erase (p);
		if (loaded (p)) onUpdate (p);
	}
}

// les voisins d'un changement (et, pour un circuit, les voisins des blocs pleins voisins : la puissance les traverse)
void notifyAround (P p, bool skipWires)
{
	for (int d = 0; d < 6; d++) {
		P q = nb (p, d);
		int b = get (q);
		if (!(skipWires && b == WIRE)) update (q);
		if (conductor (b))
			for (int e = 0; e < 6; e++) {
				P r = nb (q, e);
				if (r != p && !(skipWires && get (r) == WIRE)) update (r);
			}
	}
}

void onChangeImpl (P p, int oldB, int newB)
{
	// un observateur qui regarde p voit le changement
	for (int d = 0; d < 6; d++) {
		P q = nb (p, d);
		if (get (q) == OBSERVER && dirOf (meta (q)) == opposite (d) && !(meta (q) & M_ON)) schedule (q, 2);
	}
	bool act = isActive (oldB) || isActive (newB);
	for (int d = 0; d < 6 && !act; d++) act = isActive (get (nb (p, d)));
	if (!act) return;
	if (newB != WIRE || oldB != newB) g_netDone.clear ();
	update (p);
	if (isCircuit (oldB) || isCircuit (newB) || conductor (oldB) != conductor (newB)) notifyAround (p);
	else
		for (int d = 0; d < 6; d++) update (nb (p, d));
}

void changed (P p) { onChangeImpl (p, PISTON, get (p)); }

void onChangeCb (int x, int y, int z, int oldB, int newB)
{
	if (g_quiet) return;
	onChangeImpl ({ x, y, z }, oldB, newB);
}

// un tronçon fait ou relu : ce qui était en cours reprend
void onLoadCb (Chunk *c)
{
	for (int y = 0; y < CH; y++)
		for (int z = 0; z < CW; z++)
			for (int x = 0; x < CW; x++) {
				int b = c->at (x, y, z);
				if (!isActive (b)) continue;
				P p = { c->cx * CW + x, y, c->cz * CW + z };
				int m = c->meta (x, y, z);
				if (isLiquid (b)) {
					if (m) schedule (p, b == WATER ? 5 : 30);	// (un liquide qui coulait)
				} else if (b == TNT && (m & M_ON))
					g_fuse[p] = 40, schedule (p, 5);
				else if (b == DAYLIGHT_SENSOR || ((b == STONE_BUTTON || b == WOOD_BUTTON || b == STONE_PLATE || b == WOOD_PLATE || b == OBSERVER) && (m & M_ON)))
					schedule (p, 10);
				else if (b != WIRE && b != GLUE_BLOCK && b != COPPER_BLOCK)
					update (p);
			}
}

// ---- les liquides -------------------------------------------------------------------------------------------------

// la lave qui touche de l'eau : obsidienne (une source) ou pavés
bool lavaMeetsWater (P p)
{
	for (int d = 0; d < 6; d++) {
		if (d == D_DOWN) continue;
		if (get (nb (p, d)) == WATER) {
			setB (p, meta (p) == 0 ? OBSIDIAN : COBBLE);
			sound (SS_FIZZ, 0, p);
			return true;
		}
	}
	return false;
}

// où un liquide de la sorte b peut aller (de l'air, une plante, un écoulement du même)
bool passable (P q, int b)
{
	if (!loaded (q)) return false;
	int qb = get (q);
	return qb == AIR || fragile (qb) || (qb == b && meta (q) != 0);
}

bool passableDown (P q, int b) { return passable (q, b) || (loaded (q) && get (q) == b); }

int holeDist (P q, int b, int depth, int from, int maxD)
{
	if (passableDown (nb (q, D_DOWN), b)) return depth;
	if (depth >= maxD) return 99;
	int best = 99;
	for (int h : HORIZ) {
		if (h == from) continue;
		P r = nb (q, h);
		if (passable (r, b)) best = std::min (best, holeDist (r, b, depth + 1, opposite (h), maxD));
	}
	return best;
}

void flowInto (P q, int b, int nm)
{
	int qb = get (q);
	if (qb == AIR || fragile (qb) || qb == b) {
		if (qb == b && meta (q) == nm) return;
		if (fragile (qb)) {
			Mining mi = mining (qb);
			if (dropHook && mi.drop) dropHook (mi.drop, q.x, q.y, q.z);
		}
		setB (q, b, nm);
	} else if (b == WATER && qb == LAVA) {	// l'eau qui coule sur la lave
		setB (q, meta (q) == 0 ? OBSIDIAN : COBBLE);
		sound (SS_FIZZ, 0, q);
	}
}

void liquidTick (P p)
{
	int b = get (p), m = meta (p);
	bool water = b == WATER;
	if (!water && lavaMeetsWater (p)) return;
	int drop = water ? 1 : 2;
	if (m != 0) {
		// un écoulement : son niveau suit ses voisins (il tarit sans eux)
		int want = 99;
		bool fall = get (nb (p, D_UP)) == b;
		if (fall) want = 0;
		else {
			int sources = 0;
			for (int h : HORIZ) {
				P q = nb (p, h);
				if (get (q) != b) continue;
				int qm = meta (q);
				if (qm == 0) sources++;
				want = std::min (want, ((qm & 8) ? 0 : (qm & 7)) + drop);
			}
			// deux sources d'eau côte à côte au-dessus d'un sol (ou d'une source) : une source de plus
			if (water && sources >= 2) {
				P dn = nb (p, D_DOWN);
				int bd = get (dn);
				if (isSolid (bd) || (bd == WATER && meta (dn) == 0)) want = 0;
			}
		}
		if (want >= 8) {
			setB (p, AIR);
			return;
		}
		int nm = fall ? 8 : want;
		if (nm != m) {
			setB (p, b, nm);
			m = nm;
		}
	}
	// couler : vers le bas d'abord
	int eff = (m & 8) ? 0 : (m & 7);
	P dn = nb (p, D_DOWN);
	int bd = get (dn);
	if (loaded (dn) && bd != b && (bd == AIR || fragile (bd) || bd == WATER || bd == LAVA)) {
		if (bd == WATER && !water) {	// la lave qui tombe dans l'eau : de la pierre
			setB (dn, STONE);
			sound (SS_FIZZ, 0, dn);
			return;
		}
		if (bd == LAVA && water) {
			flowInto (dn, b, 8);
			return;
		}
		flowInto (dn, b, 8);
		return;
	}
	if (bd == b && meta (dn) != 0 && m == 0) {	// (une source au-dessus d'un écoulement le remplit)
		flowInto (dn, b, 8);
		return;
	}
	if (bd == b && m != 0) return;	// (sur son propre liquide : ne s'étale pas)
	// s'étaler : vers les trous les plus proches (4 blocs pour l'eau, 2 pour la lave), sinon partout
	int next = eff + drop;
	if (next >= 8) return;
	int maxD = water ? 4 : 2, best = 99, mask = 0;
	for (int h : HORIZ) {
		P q = nb (p, h);
		if (!passable (q, b) && !(loaded (q) && water && get (q) == LAVA)) continue;
		int d = passable (q, b) ? holeDist (q, b, 1, opposite (h), maxD) : 99;
		if (d < best) best = d, mask = 1 << h;
		else if (d == best) mask |= 1 << h;
	}
	for (int h : HORIZ) {
		if (!((mask >> h) & 1)) continue;
		P q = nb (p, h);
		int qb = get (q);
		if (qb == b) {
			int qm = meta (q);
			if (qm == 0 || (qm & 8) || (qm & 7) <= next) continue;
		}
		flowInto (q, b, next);
	}
}

// ---- les pistons --------------------------------------------------------------------------------------------------

// ce qu'un piston déplace : la ligne devant lui, et ce que la colle entraîne (12 blocs au plus)
bool structure (P piston, P start, int dir, std::vector<P> &move, std::vector<P> &destroy)
{
	std::vector<P> todo = { start };
	std::unordered_set<P, PH> in;
	while (!todo.empty ()) {
		P c = todo.back ();
		todo.pop_back ();
		while (true) {
			if (!loaded (c)) return false;
			int b = get (c), m = meta (c);
			if (b == AIR || in.count (c)) break;
			if (c == piston) return false;
			if (isLiquid (b) || fragile (b)) {
				destroy.push_back (c);
				break;
			}
			if (immovable (b, m)) return false;
			in.insert (c);
			move.push_back (c);
			if (move.size () > 12) return false;
			if (b == GLUE_BLOCK)
				for (int e = 0; e < 6; e++) {
					if (e == dir) continue;
					P r = nb (c, e);
					int rb = get (r);
					if (rb == AIR || r == piston || in.count (r) || isLiquid (rb) || fragile (rb) || immovable (rb, meta (r))) continue;
					todo.push_back (r);
				}
			c = nb (c, dir);
		}
	}
	return true;
}

void doMove (const std::vector<P> &move, const std::vector<P> &destroy, int dir)
{
	struct Mv {
		P p;
		int b, m;
	};
	std::vector<Mv> data;
	for (const P &s : move) data.push_back ({ s, get (s), meta (s) });
	std::vector<std::pair<P, int>> fuses;
	for (auto &d : data) {
		auto it = g_fuse.find (d.p);
		if (it != g_fuse.end ()) fuses.push_back ({ nb (d.p, dir), it->second }), g_fuse.erase (it);
	}
	g_quiet++;
	for (const P &s : destroy) {
		int b = get (s);
		Mining mi = mining (b);
		if (!isLiquid (b) && dropHook && mi.drop) dropHook (mi.drop, s.x, s.y, s.z);
		W->set (s.x, s.y, s.z, AIR);
	}
	for (auto &d : data) W->set (d.p.x, d.p.y, d.p.z, AIR);
	for (auto &d : data) {
		P t = nb (d.p, dir);
		W->set (t.x, t.y, t.z, d.b, d.m);
		if (pushHook) pushHook (t.x, t.y, t.z, dir);
	}
	g_quiet--;
	for (auto &f : fuses) g_fuse[f.first] = f.second, schedule (f.first, 5);
	g_netDone.clear ();
	for (const P &s : destroy) changed (s);
	for (auto &d : data) changed (d.p), changed (nb (d.p, dir));
	// un observateur déplacé envoie une impulsion (les machines qui avancent seules en vivent)
	for (auto &d : data)
		if (d.b == OBSERVER) {
			P t = nb (d.p, dir);
			if (get (t) == OBSERVER && !(meta (t) & M_ON)) schedule (t, 2);
		}
}

void extend (P p)
{
	int m = meta (p), d = dirOf (m);
	P front = nb (p, d);
	std::vector<P> mv, ds;
	if (!structure (p, front, d, mv, ds)) return;
	doMove (mv, ds, d);
	g_quiet++;
	W->set (front.x, front.y, front.z, PISTON_HEAD, d | (get (p) == STICKY_PISTON ? M_ON : 0));
	W->setMeta (p.x, p.y, p.z, m | M_ON);
	g_quiet--;
	if (pushHook) pushHook (front.x, front.y, front.z, d);
	changed (front);
	changed (p);
	sound (SS_PISTON_OUT, 0, p);
}

void retract (P p)
{
	int m = meta (p), d = dirOf (m);
	P front = nb (p, d);
	g_quiet++;
	if (get (front) == PISTON_HEAD) W->set (front.x, front.y, front.z, AIR);
	W->setMeta (p.x, p.y, p.z, m & ~M_ON);
	g_quiet--;
	changed (front);
	changed (p);
	if (get (p) == STICKY_PISTON) {	// la colle ramène le bloc d'en face
		P s = nb (front, d);
		int b = get (s);
		if (loaded (s) && b != AIR && !isLiquid (b) && !fragile (b) && !immovable (b, meta (s))) {
			std::vector<P> mv, ds;
			if (structure (p, s, opposite (d), mv, ds)) doMove (mv, ds, opposite (d));
		}
	}
	sound (SS_PISTON_IN, 0, p);
}

void tickOnce ()
{
	g_tick++;
	// une plaque sous le joueur (ou une créature : main.cpp appelle press)
	P under = { (int) floor (g_px), (int) floor (g_py + 0.01), (int) floor (g_pz) };
	press (under.x, under.y, under.z);
	int budget = 20000;
	while (!g_sched.empty () && g_sched.begin ()->first <= g_tick && budget-- > 0) {
		P p = g_sched.begin ()->second;
		g_sched.erase (g_sched.begin ());
		g_schedAt.erase (p);
		if (loaded (p)) onTick (p);
		processUpdates (4000);
	}
	processUpdates (20000);
}

} // namespace

void press (int x, int y, int z)
{
	P under = { x, y, z };
	int ub = get (under);
	if ((ub == STONE_PLATE || ub == WOOD_PLATE) && !(meta (under) & M_ON)) {
		setM (under, meta (under) | M_ON);
		schedule (under, 20);
		sound (SS_CLICK, 1, under);
	}
}

void attach (World *w)
{
	W = w;
	w->onChange = onChangeCb;
	w->onLoad = onLoadCb;
	g_sched.clear (), g_schedAt.clear (), g_updates.clear (), g_pending.clear (), g_netDone.clear ();
	g_torchHist.clear (), g_torchBurnt.clear (), g_fuse.clear ();
	g_acc = 0;
}

void step (double dt, float daylight, double px, double py, double pz)
{
	g_day = daylight;
	g_px = px, g_py = py, g_pz = pz;
	g_acc += dt;
	if (g_acc > 0.25) g_acc = 0.25;
	for (int n = 0; g_acc >= 0.05 && n < 5; n++) {
		g_acc -= 0.05;
		tickOnce ();
	}
	processUpdates (20000);
}

void runTicks (int n)
{
	processUpdates (100000);
	for (int i = 0; i < n; i++) tickOnce ();
}

long now () { return g_tick; }
int pending () { return (int) g_sched.size (); }

bool use (int x, int y, int z, int held)
{
	P p = { x, y, z };
	int b = get (p), m = meta (p);
	switch (b) {
	case LEVER:
		setM (p, m ^ M_ON);
		sound (SS_CLICK, (m & M_ON) ? 0 : 1, p);
		return true;
	case STONE_BUTTON: case WOOD_BUTTON:
		if (!(m & M_ON)) {
			setM (p, m | M_ON);
			schedule (p, b == STONE_BUTTON ? 20 : 30);
			sound (SS_CLICK, 1, p);
		}
		return true;
	case REPEATER:
		setM (p, (m & ~(3 << 3)) | ((((m >> 3) & 3) + 1) & 3) << 3);
		sound (SS_CLICK, 0, p);
		return true;
	case COMPARATOR:
		setM (p, m ^ M_ON);
		sound (SS_CLICK, 0, p);
		return true;
	case NOTE_BLOCK:
		setM (p, (m & ~31) | (((m & 31) + 1) % 25));
		playNote (p);
		return true;
	case DAYLIGHT_SENSOR:
		setM (p, m ^ M_ON);
		g_schedAt.erase (p);
		schedule (p, 1);
		sound (SS_CLICK, 0, p);
		return true;
	case TNT:
		if (held == COPPER_TORCH && !(m & M_ON)) {
			prime (p, 80);
			return true;
		}
		return false;
	}
	return false;
}

int placeMeta (int b, int x, int y, int z, int face, float yaw, float pitch)
{
	P p = { x, y, z };
	bool below = supportTop (get (nb (p, D_DOWN)));
	float yr = yaw * (float) M_PI / 180.0f, pr = pitch * (float) M_PI / 180.0f;
	float fx = sinf (yr) * cosf (pr), fy = sinf (pr), fz = -cosf (yr) * cosf (pr);
	int hdir = fabsf (fx) > fabsf (fz) ? (fx > 0 ? D_EAST : D_WEST) : (fz > 0 ? D_SOUTH : D_NORTH);
	int look = fabsf (fy) > std::max (fabsf (fx), fabsf (fz)) ? (fy > 0 ? D_UP : D_DOWN) : hdir;
	switch (b) {
	case WIRE: case STONE_PLATE: case WOOD_PLATE: case DAYLIGHT_SENSOR: return below ? 0 : -1;
	case REPEATER: case COMPARATOR: return below ? hdir : -1;
	case COPPER_TORCH: {
		if (face == D_DOWN || face < 0) return -1;
		int s = opposite (face);
		return supportSide (get (nb (p, s))) ? s : -1;
	}
	case LEVER: case STONE_BUTTON: case WOOD_BUTTON: {
		if (face < 0) return -1;
		int s = opposite (face);
		return supportSide (get (nb (p, s))) ? s : -1;
	}
	case PISTON: case STICKY_PISTON: return opposite (look);	// (la tête vers le joueur)
	case OBSERVER: return look;					// (il regarde là où regarde le joueur)
	case PISTON_HEAD: return -1;
	}
	return 0;
}

int broken (int x, int y, int z, int b, int m)
{
	P p = { x, y, z };
	if ((b == PISTON || b == STICKY_PISTON) && (m & M_ON)) {
		P h = nb (p, dirOf (m));
		if (get (h) == PISTON_HEAD) setB (h, AIR);
	}
	if (b == PISTON_HEAD) {
		P base = nb (p, opposite (dirOf (m)));
		int bb = get (base);
		if (bb == PISTON || bb == STICKY_PISTON) {
			setB (base, AIR);
			return bb;
		}
	}
	g_fuse.erase (p);
	return 0;
}

// ---- expliquer (pour les agents et les scripts) -------------------------------------------------------------------

static const char *const DIRN[6] = { "east", "west", "up", "down", "south", "north" };

std::string stateText (int b, int m)
{
	char t[160];
	t[0] = 0;
	switch (b) {
	case WATER: case LAVA:
		snprintf (t, sizeof t, "%s", m == 0 ? "source" : (m & 8) ? "falling" : (std::string ("flowing level ") + std::to_string (m & 7)).c_str ());
		break;
	case WIRE: snprintf (t, sizeof t, "power %d", m & 15); break;
	case COPPER_TORCH: snprintf (t, sizeof t, "attached %s, %s", DIRN[dirOf (m)], (m & M_ON) ? "off (its support is powered)" : "lit"); break;
	case LEVER: snprintf (t, sizeof t, "attached %s, %s", DIRN[dirOf (m)], (m & M_ON) ? "on" : "off"); break;
	case STONE_BUTTON: case WOOD_BUTTON: snprintf (t, sizeof t, "attached %s, %s", DIRN[dirOf (m)], (m & M_ON) ? "pressed" : "released"); break;
	case STONE_PLATE: case WOOD_PLATE: snprintf (t, sizeof t, "%s", (m & M_ON) ? "pressed" : "released"); break;
	case REPEATER:
		snprintf (t, sizeof t, "output %s, delay %d, %s%s", DIRN[dirOf (m)], ((m >> 3) & 3) + 1, (m & M_POW) ? "on" : "off", (m & M_LOCK) ? ", locked" : "");
		break;
	case COMPARATOR: snprintf (t, sizeof t, "output %s, %s mode, output %d", DIRN[dirOf (m)], (m & M_ON) ? "subtract" : "compare", m >> 4); break;
	case OBSERVER: snprintf (t, sizeof t, "watching %s (output %s), %s", DIRN[dirOf (m)], DIRN[opposite (dirOf (m))], (m & M_ON) ? "pulsing" : "idle"); break;
	case PISTON: case STICKY_PISTON: snprintf (t, sizeof t, "pushes %s, %s", DIRN[dirOf (m)], (m & M_ON) ? "extended" : "retracted"); break;
	case PISTON_HEAD: snprintf (t, sizeof t, "facing %s%s", DIRN[dirOf (m)], (m & M_ON) ? ", sticky" : ""); break;
	case SIGNAL_LAMP: snprintf (t, sizeof t, "%s", (m & M_ON) ? "lit" : "off"); break;
	case NOTE_BLOCK: snprintf (t, sizeof t, "note %d%s", m & 31, (m & M_POW) ? ", powered" : ""); break;
	case DAYLIGHT_SENSOR: snprintf (t, sizeof t, "%s, output %d", (m & M_ON) ? "inverted" : "normal", m >> 4); break;
	case TNT: snprintf (t, sizeof t, "%s", (m & M_ON) ? "lit (about to explode)" : "idle"); break;
	}
	return t;
}

std::string explain (int x, int y, int z)
{
	P p = { x, y, z };
	int b = get (p), m = meta (p);
	std::string o = std::string (info (b).name) + (stateText (b, m).empty () ? "" : " (" + stateText (b, m) + ")") + "\n";
	auto nbName = [&] (int d) {
		P q = nb (p, d);
		int qb = get (q);
		std::string s = std::string (DIRN[d]) + " (" + std::to_string (q.x) + "," + std::to_string (q.y) + "," + std::to_string (q.z) + "): " + info (qb).name;
		std::string st = stateText (qb, meta (q));
		if (!st.empty ()) s += " [" + st + "]";
		return s;
	};
	if (!loaded (p)) return o + "chunk not loaded\n";
	if (b == WIRE) {
		o += "power " + std::to_string (m & 15) + "; source input (not counting other wire) " + std::to_string (wireInput (p)) + "\n";
		std::string con, pts;
		for (int h : HORIZ) {
			if (wireConnects (p, h)) con += std::string (con.empty () ? "" : ", ") + DIRN[h];
			if (wirePoints (p, h)) pts += std::string (pts.empty () ? "" : ", ") + DIRN[h];
		}
		o += "connects: " + (con.empty () ? std::string ("nothing (a lone dot: it points everywhere)") : con) + "; points into: " + pts + " and down\n";
		o += "it weakly powers the block below and the blocks it points into; a weakly powered block powers components (lamp, piston, repeater, torch on it), never other wire\n";
		for (int d = 0; d < 6; d++) {
			P q = nb (p, d);
			int qb = get (q);
			if (qb == WIRE) continue;
			int v = conductor (qb) ? blockStrong (q) : emitted (q, opposite (d));
			if (v) o += "  gets " + std::to_string (v) + " from " + nbName (d) + (conductor (qb) ? " (strongly powered block)" : "") + "\n";
			else if (conductor (qb) && blockPower (q)) o += "  no input from " + nbName (d) + ": only weakly powered (wire ignores weak power)\n";
		}
		if (!supportTop (get (nb (p, D_DOWN)))) o += "  WARNING: no solid block below: the wire breaks\n";
		return o;
	}
	bool comp = b == SIGNAL_LAMP || b == NOTE_BLOCK || b == TNT;	// (des composants qui sont aussi des blocs pleins)
	if (conductor (b) && !comp) {
		o += "solid block: strongly powered " + std::to_string (blockStrong (p)) + ", powered (weak or strong) " + std::to_string (blockPower (p)) + "\n";
		for (int d = 0; d < 6; d++) {
			P q = nb (p, d);
			int s = strongEmitted (q, opposite (d)), w = get (q) == WIRE ? emitted (q, opposite (d)) : 0;
			if (s) o += "  strongly by " + nbName (d) + ": " + std::to_string (s) + "\n";
			else if (w) o += "  weakly by " + nbName (d) + " (wire pointing in): " + std::to_string (w) + "\n";
		}
		if (blockPower (p) && !blockStrong (p)) o += "  only weak power: it drives adjacent components but no wire\n";
		return o;
	}
	switch (b) {
	case REPEATER: {
		int f = dirOf (m);
		o += "input (its back, " + std::string (DIRN[opposite (f)]) + "): " + std::to_string (repeaterIn (p)) + " from " + nbName (opposite (f)) + "\n";
		o += std::string ("locked by a powered repeater or comparator on its side: ") + (repeaterLocked (p) ? "yes" : "no") + "\n";
		o += "output: 15 into " + nbName (f) + " when on (it strongly powers a solid block there)\n";
		break;
	}
	case COMPARATOR: {
		int f = dirOf (m);
		o += "rear input: " + std::to_string (powerFrom (p, opposite (f))) + " from " + nbName (opposite (f)) + "\n";
		for (int h : HORIZ)
			if (h != f && h != opposite (f)) o += "side " + nbName (h) + " gives " + std::to_string (emitted (nb (p, h), opposite (h))) + " (only wire, repeater, comparator, copper block count)\n";
		o += "output now " + std::to_string (compOut (p)) + " into " + nbName (f) + "\n";
		break;
	}
	case COPPER_TORCH: {
		P s = nb (p, dirOf (m));
		o += "support " + nbName (dirOf (m)) + ": powered " + std::to_string (conductor (get (s)) ? blockPower (s) : 0) + " -> the torch should be " + (torchWants (p) ? "lit" : "off") + "\n";
		o += "when lit it gives 15 to all its neighbours except its support, and strongly powers the block above it\n";
		break;
	}
	case PISTON: case STICKY_PISTON: case SIGNAL_LAMP: case NOTE_BLOCK: case TNT: {
		int skip = b == PISTON || b == STICKY_PISTON ? dirOf (m) : -1;
		for (int d = 0; d < 6; d++) {
			if (d == skip) {
				o += "  " + nbName (d) + ": ignored (a piston is never powered through its face)\n";
				continue;
			}
			int v = powerFrom (p, d);
			o += "  " + nbName (d) + ": gives " + std::to_string (v) + "\n";
		}
		if (comp) o += "it is also a solid block: powered " + std::to_string (blockPower (p)) + " (strongly " + std::to_string (blockStrong (p)) + ")\n";
		if (b == PISTON || b == STICKY_PISTON) {
			o += std::string ("powered: ") + (pistonPowered (p) ? "yes" : "no") + " (wire powers it only if it points into it; a lever on the piston itself works)\n";
			if (!(m & M_ON)) {
				std::vector<P> mv, ds;
				bool can = structure (p, nb (p, dirOf (m)), dirOf (m), mv, ds);
				o += std::string ("it could push: ") + (can ? std::to_string (mv.size ()) + " block(s)" : "no (more than 12 blocks, or an immovable block: bedrock, obsidian, furnace, an extended piston)") + "\n";
			}
		}
		break;
	}
	default:
		o += "power here: " + std::to_string (powerAt (x, y, z)) + "\n";
	}
	return o;
}

std::vector<Change> test (int x1, int y1, int z1, int x2, int y2, int z2, int ticks, bool use_, int ux, int uy, int uz)
{
	if (x1 > x2) std::swap (x1, x2);
	if (y1 > y2) std::swap (y1, y2);
	if (z1 > z2) std::swap (z1, z2);
	std::vector<Change> out;
	long n = (long) (x2 - x1 + 1) * (y2 - y1 + 1) * (z2 - z1 + 1);
	if (n <= 0 || n > 32768) return out;
	std::vector<int> bm ((size_t) n);
	auto idx = [&] (int x, int y, int z) { return (size_t) (((y - y1) * (z2 - z1 + 1) + (z - z1)) * (x2 - x1 + 1) + (x - x1)); };
	auto scan = [&] (int tick) {
		for (int y = y1; y <= y2; y++)
			for (int z = z1; z <= z2; z++)
				for (int x = x1; x <= x2; x++) {
					int b = W->get (x, y, z), m = W->meta (x, y, z), v = b << 8 | m;
					int &old = bm[idx (x, y, z)];
					if (tick >= 0 && v != old && out.size () < 4000) out.push_back ({ tick, x, y, z, b, m, powerAt (x, y, z) });
					old = v;
				}
	};
	processUpdates (100000);
	scan (-1);
	if (use_) {
		use (ux, uy, uz, 0);
		processUpdates (100000);
		scan (0);
	}
	for (int t = 1; t <= ticks; t++) {
		tickOnce ();
		scan (t);
	}
	return out;
}

int powerAt (int x, int y, int z)
{
	P p = { x, y, z };
	int b = get (p);
	if (b == WIRE) return meta (p) & 15;
	if (conductor (b)) return blockPower (p);
	int best = 0;
	for (int d = 0; d < 6; d++) best = std::max (best, emitted (p, d));
	if (b == SIGNAL_LAMP || b == PISTON || b == STICKY_PISTON || b == NOTE_BLOCK) best = inputPower (p);
	return best;
}

} // namespace sim
} // namespace tc
