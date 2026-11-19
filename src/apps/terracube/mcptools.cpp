// mcptools.cpp -- TerraCube : les outils MCP (JSON-RPC 2.0 : initialize, ping, tools/list, tools/call), exécutés dans
// la boucle du jeu comme les commandes des scripts. Les noms et descriptions des outils sont en anglais (ils
// s'adressent au modèle). Le transport (HTTP, jeton) est dans mcp.cpp.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <string>
#include <vector>
#include <archivekit_abi.h>
#include "json.h"
#include "mcp.h"
#include "render.h"

namespace tc {

namespace {

std::string jq (const std::string &s)
{
	jbuf b;
	jb_init (&b);
	jb_strn (&b, s.data (), s.size ());
	std::string r (b.p, b.n);
	jb_free (&b);
	return r;
}
std::string jn (double v)
{
	if (!std::isfinite (v)) return "null";
	char t[40];
	snprintf (t, sizeof t, "%.6g", v);
	return t;
}
struct JO {
	std::string s = "{";
	JO &raw (const char *k, const std::string &v)
	{
		if (s.size () > 1) s += ',';
		s += jq (k) + ':' + v;
		return *this;
	}
	JO &str (const char *k, const std::string &v) { return raw (k, jq (v)); }
	JO &num (const char *k, double v) { return raw (k, jn (v)); }
	JO &boo (const char *k, bool v) { return raw (k, v ? "true" : "false"); }
	std::string end () const { return s + "}"; }
};
struct JA {
	std::string s = "[";
	JA &add (const std::string &v)
	{
		if (s.size () > 1) s += ',';
		s += v;
		return *this;
	}
	std::string end () const { return s + "]"; }
};

struct ToolErr {
	std::string msg;
};
[[noreturn]] void fail (const std::string &m) { throw ToolErr { m }; }

struct Args {
	const json_t *o;
	const json_t *get (const char *k) const { return o ? json_get (o, k) : nullptr; }
	bool has (const char *k) const { return get (k) != nullptr; }
	double number (const char *k) const
	{
		const json_t *v = get (k);
		if (!v) fail (std::string ("missing argument: ") + k);
		if (v->kind != JSON_NUM && v->kind != JSON_STR) fail (std::string ("not a number: ") + k);
		char *e;
		double d = strtod (json_text (v), &e);
		if (e == json_text (v)) fail (std::string ("not a number: ") + k);
		return d;
	}
	double number (const char *k, double def) const { return has (k) ? number (k) : def; }
	int integer (const char *k) const { return (int) floor (number (k)); }
	int integer (const char *k, int def) const { return has (k) ? integer (k) : def; }
	bool boolean (const char *k, bool def) const
	{
		const json_t *v = get (k);
		return v && v->kind == JSON_BOOL ? v->b != 0 : def;
	}
	std::string text (const char *k, const char *def = "") const
	{
		const json_t *v = get (k);
		return v ? json_text (v) : def;
	}
	// [x, y, z]
	void vec (const char *k, int n, double *out) const
	{
		const json_t *v = get (k);
		if (!v || v->kind != JSON_ARR || json_len (v) != n) fail (std::string (k) + ": an array of " + std::to_string (n) + " numbers is expected");
		for (int i = 0; i < n; i++) out[i] = strtod (json_text (json_at (v, i)), nullptr);
	}
	int block (const char *k) const
	{
		const json_t *v = get (k);
		if (!v) fail (std::string ("missing argument: ") + k);
		int b = blockByName (json_text (v));
		if (b < 0) fail (std::string ("unknown block: ") + json_text (v) + " (see list_block_types)");
		return b;
	}
};

int ifloor (double v) { return (int) floor (v); }

std::string playerJson ()
{
	float d[3];
	viewDir (g.pl.yaw, g.pl.pitch, d);
	int fx = ifloor (g.pl.x), fy = ifloor (g.pl.y), fz = ifloor (g.pl.z);
	float eye[3] = { (float) g.pl.x, (float) (g.pl.y + Player::EYE), (float) g.pl.z };
	Hit h = g.world.raycast (eye, d, 8.0f);
	JO o;
	o.raw ("position", "[" + jn (g.pl.x) + "," + jn (g.pl.y) + "," + jn (g.pl.z) + "]")
		.raw ("tile", "[" + std::to_string (fx) + "," + std::to_string (fy) + "," + std::to_string (fz) + "]")
		.num ("yaw", g.pl.yaw).num ("pitch", g.pl.pitch)
		.raw ("direction", "[" + jn (d[0]) + "," + jn (d[1]) + "," + jn (d[2]) + "]")
		.boo ("flying", g.pl.fly).boo ("in_water", g.pl.inWater)
		.str ("block_below", info (g.world.get (fx, fy - 1, fz)).name);
	if (h.hit)
		o.raw ("looking_at", JO ().raw ("block_position", "[" + std::to_string (h.x) + "," + std::to_string (h.y) + "," + std::to_string (h.z) + "]")
			       .str ("block", info (h.block).name)
			       .raw ("place_position", "[" + std::to_string (h.nx) + "," + std::to_string (h.ny) + "," + std::to_string (h.nz) + "]")
			       .num ("distance", h.dist).end ());
	return o.end ();
}

std::string g_image;	// la capture de cet appel (base64)

// PNG en RGB, compressé par ArchiveKit
std::string pngOf (const std::vector<uint32_t> &px, int w, int h)
{
	static bool ark = archivekit_kit_open () != 0;
	auto put32 = [] (std::string &s, uint32_t v) { s += (char) (v >> 24), s += (char) (v >> 16), s += (char) (v >> 8), s += (char) v; };
	std::string raw;
	raw.reserve ((size_t) (w * 3 + 1) * h);
	for (int y = 0; y < h; y++) {
		raw += '\0';
		for (int x = 0; x < w; x++) {
			uint32_t c = px[(size_t) y * w + x];
			raw += (char) (c >> 16), raw += (char) (c >> 8), raw += (char) c;
		}
	}
	long z = ark ? ar_deflate_new ((long) raw.data (), (long) raw.size (), AR_ZLIB, 6) : 0;
	if (z <= 0) fail ("PNG: compression unavailable (ArchiveKit)");
	std::string out ("\x89PNG\r\n\x1a\n", 8);
	auto chunk = [&] (const char *type, const std::string &data) {
		put32 (out, (uint32_t) data.size ());
		std::string td = std::string (type, 4) + data;
		out += td;
		put32 (out, (uint32_t) ar_crc32 (0, (long) td.data (), (long) td.size ()));
	};
	std::string ih;
	put32 (ih, (uint32_t) w), put32 (ih, (uint32_t) h);
	ih += (char) 8, ih += (char) 2, ih += '\0', ih += '\0', ih += '\0';
	chunk ("IHDR", ih);
	chunk ("IDAT", std::string ((const char *) ar_data (z), (size_t) ar_bytes (z)));
	ar_free (z);
	chunk ("IEND", "");
	return out;
}

// ---- les outils ---------------------------------------------------------------------------------------------------

std::string t_info (const Args &)
{
	return JO ().str ("game", "TerraCube").str ("version", VERSION).str ("world", g.world.name).num ("seed", g.world.seed)
		.num ("hour", g.hour).boo ("time_runs", g.timeRuns).num ("render_distance_chunks", g.distance)
		.raw ("player", playerJson ())
		.str ("coordinates", "x east, y up (0 bedrock .. 127 sky), z south; sea level y=48; yaw 0 faces north (-z), 90 east (+x)")
		.num ("block_types", BLOCK_COUNT).end ();
}

std::string t_get_player (const Args &) { return playerJson (); }

std::string t_teleport (const Args &a)
{
	double p[3];
	a.vec ("position", 3, p);
	gameTeleport (p[0], p[1], p[2]);
	if (a.has ("yaw")) g.pl.yaw = (float) fmod (fmod (a.number ("yaw"), 360.0) + 360.0, 360.0);
	if (a.has ("pitch")) g.pl.pitch = (float) fmax (-89.9, fmin (89.9, a.number ("pitch")));
	if (a.has ("fly")) {
		g.pl.fly = a.boolean ("fly", g.pl.fly);
		gameFlyChanged ();
	}
	return playerJson ();
}

std::string t_look (const Args &a)
{
	if (a.has ("at")) {
		double t[3];
		a.vec ("at", 3, t);
		double dx = t[0] - g.pl.x, dy = t[1] - (g.pl.y + Player::EYE), dz = t[2] - g.pl.z;
		double hd = sqrt (dx * dx + dz * dz);
		g.pl.yaw = (float) fmod (atan2 (dx, -dz) * 180.0 / M_PI + 360.0, 360.0);
		g.pl.pitch = (float) fmax (-89.9, fmin (89.9, atan2 (dy, hd) * 180.0 / M_PI));
	} else {
		if (!a.has ("yaw") && !a.has ("pitch")) fail ("give yaw/pitch or at");
		if (a.has ("yaw")) g.pl.yaw = (float) fmod (fmod (a.number ("yaw"), 360.0) + 360.0, 360.0);
		if (a.has ("pitch")) g.pl.pitch = (float) fmax (-89.9, fmin (89.9, a.number ("pitch")));
	}
	return playerJson ();
}

std::string t_get_block (const Args &a)
{
	int x = a.integer ("x"), y = a.integer ("y"), z = a.integer ("z");
	int b = g.world.getLoaded (x, y, z);
	return JO ().num ("id", b).str ("block", info (b).name).end ();
}

std::string t_set_block (const Args &a)
{
	int x = a.integer ("x"), y = a.integer ("y"), z = a.integer ("z"), b = a.block ("block");
	if (!g.world.set (x, y, z, b)) fail ("outside the world (y from 0 to 127)");
	g.edited = true;
	return JO ().boo ("ok", true).end ();
}

std::string t_set_blocks (const Args &a)
{
	const json_t *l = a.get ("blocks");
	if (!l || l->kind != JSON_ARR) fail ("blocks: an array of {x, y, z, block} is expected");
	int n = json_len (l);
	if (n > 200000) fail ("too many blocks (200000 at most per call)");
	long done = 0, bad = 0;
	std::string firstErr;
	for (int i = 0; i < n; i++) {
		Args e { json_at (l, i) };
		try {
			int x, y, z;
			if (e.o && e.o->kind == JSON_ARR) {	// ([x, y, z, bloc] accepté aussi)
				if (json_len (e.o) != 4) fail ("[x, y, z, block] expected");
				x = (int) floor (strtod (json_text (json_at (e.o, 0)), nullptr));
				y = (int) floor (strtod (json_text (json_at (e.o, 1)), nullptr));
				z = (int) floor (strtod (json_text (json_at (e.o, 2)), nullptr));
				int b = blockByName (json_text (json_at (e.o, 3)));
				if (b < 0) fail (std::string ("unknown block: ") + json_text (json_at (e.o, 3)));
				if (g.world.set (x, y, z, b)) done++;
				else bad++;
				continue;
			}
			x = e.integer ("x"), y = e.integer ("y"), z = e.integer ("z");
			if (g.world.set (x, y, z, e.block ("block"))) done++;
			else bad++;
		} catch (const ToolErr &x) {
			bad++;
			if (firstErr.empty ()) firstErr = "item " + std::to_string (i) + ": " + x.msg;
		}
	}
	g.edited = true;
	JO o;
	o.num ("placed", (double) done).num ("rejected", (double) bad);
	if (!firstErr.empty ()) o.str ("first_error", firstErr);
	return o.end ();
}

std::string t_fill (const Args &a)
{
	double f[3], t[3];
	a.vec ("from", 3, f);
	a.vec ("to", 3, t);
	long n = 0;
	std::string e = opFill (ifloor (f[0]), ifloor (f[1]), ifloor (f[2]), ifloor (t[0]), ifloor (t[1]), ifloor (t[2]), a.block ("block"), &n,
				a.boolean ("hollow", false));
	if (!e.empty ()) fail (e);
	return JO ().num ("placed", (double) n).end ();
}

std::string t_get_blocks (const Args &a)
{
	double f[3], t[3];
	a.vec ("from", 3, f);
	a.vec ("to", 3, t);
	int x1 = ifloor (fmin (f[0], t[0])), y1 = ifloor (fmin (f[1], t[1])), z1 = ifloor (fmin (f[2], t[2]));
	int x2 = ifloor (fmax (f[0], t[0])), y2 = ifloor (fmax (f[1], t[1])), z2 = ifloor (fmax (f[2], t[2]));
	long vol = (long) (x2 - x1 + 1) * (y2 - y1 + 1) * (z2 - z1 + 1);
	if (vol > 32768) fail ("too many blocks (32768 at most); use get_heightmap for terrain");
	bool skipAir = a.boolean ("non_air_only", true);
	JA list;
	long cnt = 0;
	std::vector<int> used (BLOCK_COUNT, 0);
	if (skipAir) {
		for (int y = y1; y <= y2; y++)
			for (int z = z1; z <= z2; z++)
				for (int x = x1; x <= x2; x++) {
					int b = g.world.getLoaded (x, y, z);
					if (b == AIR) continue;
					used[b]++;
					list.add ("[" + std::to_string (x) + "," + std::to_string (y) + "," + std::to_string (z) + "," + std::to_string (b) + "]");
					cnt++;
				}
	} else {
		// les numéros en couches : y, puis z, puis x
		for (int y = y1; y <= y2; y++) {
			JA layer;
			for (int z = z1; z <= z2; z++) {
				std::string row = "[";
				for (int x = x1; x <= x2; x++) {
					int b = g.world.getLoaded (x, y, z);
					used[b]++;
					row += (x > x1 ? "," : "") + std::to_string (b);
				}
				layer.add (row + "]");
			}
			list.add (layer.end ());
		}
	}
	JO legend;
	for (int i = 0; i < BLOCK_COUNT; i++)
		if (used[i]) legend.str (std::to_string (i).c_str (), BLOCKS[i].name);
	JO o;
	o.raw ("from", "[" + std::to_string (x1) + "," + std::to_string (y1) + "," + std::to_string (z1) + "]")
		.raw ("to", "[" + std::to_string (x2) + "," + std::to_string (y2) + "," + std::to_string (z2) + "]")
		.raw ("legend", legend.end ());
	if (skipAir) o.str ("format", "[x, y, z, id] for each non-air block").num ("count", (double) cnt).raw ("blocks", list.end ());
	else o.str ("format", "layers[y - from.y][z - from.z][x - from.x] = id").raw ("layers", list.end ());
	return o.end ();
}

std::string t_get_heightmap (const Args &a)
{
	double f[2], t[2];
	a.vec ("from", 2, f);
	a.vec ("to", 2, t);
	int x1 = ifloor (fmin (f[0], t[0])), z1 = ifloor (fmin (f[1], t[1])), x2 = ifloor (fmax (f[0], t[0])), z2 = ifloor (fmax (f[1], t[1]));
	if ((long) (x2 - x1 + 1) * (z2 - z1 + 1) > 16384) fail ("too large (128 x 128 columns at most)");
	JA rows;
	std::vector<int> used (BLOCK_COUNT, 0);
	JA tops;
	for (int z = z1; z <= z2; z++) {
		std::string r = "[", s = "[";
		for (int x = x1; x <= x2; x++) {
			int y = g.world.height (x, z);
			int b = g.world.get (x, y, z);
			used[b]++;
			r += (x > x1 ? "," : "") + std::to_string (y);
			s += (x > x1 ? "," : "") + std::to_string (b);
		}
		rows.add (r + "]");
		tops.add (s + "]");
	}
	JO legend;
	for (int i = 0; i < BLOCK_COUNT; i++)
		if (used[i]) legend.str (std::to_string (i).c_str (), BLOCKS[i].name);
	return JO ().raw ("from", "[" + std::to_string (x1) + "," + std::to_string (z1) + "]").raw ("to", "[" + std::to_string (x2) + "," + std::to_string (z2) + "]")
		.str ("format", "heights[z - from.z][x - from.x] = y of the highest non-air block; top_blocks the same with its id")
		.raw ("heights", rows.end ()).raw ("top_blocks", tops.end ()).raw ("legend", legend.end ()).end ();
}

std::string t_list_blocks (const Args &)
{
	JA l;
	for (int i = 0; i < BLOCK_COUNT; i++)
		l.add (JO ().num ("id", i).str ("name", BLOCKS[i].name).boo ("solid", BLOCKS[i].solid).boo ("transparent", !BLOCKS[i].opaque).end ());
	return JO ().raw ("blocks", l.end ()).end ();
}

std::string t_post (const Args &a)
{
	std::string t = a.text ("text");
	if (t.empty ()) fail ("missing argument: text");
	gamePost (t);
	return JO ().boo ("ok", true).end ();
}

std::string t_set_time (const Args &a)
{
	double h = fmod (a.number ("hour"), 24.0);
	g.hour = (float) (h < 0 ? h + 24 : h);
	g.timeRuns = a.boolean ("running", g.timeRuns);
	return JO ().num ("hour", g.hour).boo ("time_runs", g.timeRuns).end ();
}

std::string t_screenshot (const Args &a)
{
	double eye[3];
	float yp[2] = { g.pl.yaw, g.pl.pitch };
	bool from = a.has ("eye");
	if (from) a.vec ("eye", 3, eye);
	if (a.has ("yaw")) yp[0] = (float) a.number ("yaw");
	if (a.has ("pitch")) yp[1] = (float) fmax (-89.9, fmin (89.9, a.number ("pitch")));
	if (from && a.has ("look_at")) {
		double t[3];
		a.vec ("look_at", 3, t);
		double dx = t[0] - eye[0], dy = t[1] - eye[1], dz = t[2] - eye[2];
		yp[0] = (float) (atan2 (dx, -dz) * 180.0 / M_PI);
		yp[1] = (float) fmax (-89.9, fmin (89.9, atan2 (dy, sqrt (dx * dx + dz * dz)) * 180.0 / M_PI));
	}
	std::vector<uint32_t> px;
	int w, h;
	if (!gameCapture (px, w, h, from ? eye : nullptr, (from || a.has ("yaw") || a.has ("pitch")) ? yp : nullptr)) fail ("no image (the view is not shown yet)");
	int mw = (int) fmax (64, fmin (1600, a.integer ("max_width", 960)));
	if (w > mw) {	// (réduite par moyenne de blocs)
		int nw = mw, nh = (int) ((long) h * mw / w);
		std::vector<uint32_t> o ((size_t) nw * nh);
		for (int y = 0; y < nh; y++)
			for (int x = 0; x < nw; x++) {
				int x0 = x * w / nw, x1 = std::max (x0 + 1, (x + 1) * w / nw), y0 = y * h / nh, y1 = std::max (y0 + 1, (y + 1) * h / nh);
				unsigned r = 0, gg = 0, b = 0, n = 0;
				for (int yy = y0; yy < y1; yy++)
					for (int xx = x0; xx < x1; xx++) {
						uint32_t c = px[(size_t) yy * w + xx];
						r += c >> 16 & 255, gg += c >> 8 & 255, b += c & 255, n++;
					}
				o[(size_t) y * nw + x] = (r / n) << 16 | (gg / n) << 8 | (b / n);
			}
		px.swap (o);
		w = nw, h = nh;
	}
	std::string png = pngOf (px, w, h);
	g_image = mcp::base64 ((const unsigned char *) png.data (), png.size ());
	return JO ().num ("width", w).num ("height", h).str ("note", from ? "rendered from the given eye position (the player did not move)" : "the player's view").end ();
}

std::string t_save (const Args &)
{
	std::string err;
	if (!gameSave (err)) fail (err);
	return JO ().boo ("saved", true).str ("folder", g.world.dir).end ();
}

std::string t_events (const Args &)
{
	JA l;
	static const char *K[] = { "", "broken", "placed", "touched" };
	for (auto &e : g.events)
		l.add (JO ().str ("kind", K[e.kind & 3]).raw ("position", "[" + std::to_string (e.x) + "," + std::to_string (e.y) + "," + std::to_string (e.z) + "]")
			       .str ("block", info (e.block).name).num ("face", e.face).end ());
	g.events.clear ();
	return JO ().raw ("events", l.end ()).end ();
}

typedef std::string (*ToolFn) (const Args &);
struct ToolDef {
	const char *name, *desc, *schema;
	ToolFn fn;
	bool readOnly;
};

#define S(...) #__VA_ARGS__
const ToolDef TOOLS[] = {
	{ "get_game_info", "World name, seed, time of day, the player, and the coordinate conventions. Call it first.",
	  S ({ "type" : "object", "properties" : {} }), t_info, true },
	{ "get_player", "The player's position (feet), tile, yaw/pitch, view direction, and the block the player is looking at (with the position where a block would be placed).",
	  S ({ "type" : "object", "properties" : {} }), t_get_player, true },
	{ "teleport", "Move the player (feet position, in blocks) and optionally set yaw, pitch and flying.",
	  S ({ "type" : "object", "properties" : { "position" : { "type" : "array", "items" : { "type" : "number" }, "minItems" : 3, "maxItems" : 3 },
		  "yaw" : { "type" : "number" }, "pitch" : { "type" : "number" }, "fly" : { "type" : "boolean" } }, "required" : ["position"] }),
	  t_teleport, false },
	{ "look", "Turn the player's head: yaw/pitch in degrees (yaw 0 = north -z, 90 = east +x; pitch positive = up), or at = [x, y, z] to look at a point.",
	  S ({ "type" : "object", "properties" : { "yaw" : { "type" : "number" }, "pitch" : { "type" : "number" },
		  "at" : { "type" : "array", "items" : { "type" : "number" } } } }),
	  t_look, false },
	{ "get_block", "The block at integer coordinates x, y, z (the chunk is generated if needed).",
	  S ({ "type" : "object", "properties" : { "x" : { "type" : "integer" }, "y" : { "type" : "integer" }, "z" : { "type" : "integer" } },
		  "required" : ["x", "y", "z"] }),
	  t_get_block, true },
	{ "get_blocks", "The blocks of a box (32768 blocks at most). By default only non-air blocks as [x, y, z, id] with a legend; non_air_only:false gives full layers.",
	  S ({ "type" : "object", "properties" : { "from" : { "type" : "array", "items" : { "type" : "integer" } }, "to" : { "type" : "array", "items" : { "type" : "integer" } },
		  "non_air_only" : { "type" : "boolean" } }, "required" : ["from", "to"] }),
	  t_get_blocks, true },
	{ "get_heightmap", "Terrain survey: for columns from [x, z] to [x, z] (128 x 128 at most), the height of the highest non-air block and its type.",
	  S ({ "type" : "object", "properties" : { "from" : { "type" : "array", "items" : { "type" : "integer" } }, "to" : { "type" : "array", "items" : { "type" : "integer" } } },
		  "required" : ["from", "to"] }),
	  t_get_heightmap, true },
	{ "list_block_types", "All block types: id, name, solid, transparent. Blocks can be given by id or name everywhere.",
	  S ({ "type" : "object", "properties" : {} }), t_list_blocks, true },
	{ "set_block", "Place one block (block: name such as \"stone\" or id; \"air\" removes).",
	  S ({ "type" : "object", "properties" : { "x" : { "type" : "integer" }, "y" : { "type" : "integer" }, "z" : { "type" : "integer" },
		  "block" : { "type" : ["string", "integer"] } }, "required" : ["x", "y", "z", "block"] }),
	  t_set_block, false },
	{ "set_blocks", "Place many blocks at once (200000 at most): blocks = [{x, y, z, block}, ...] or [[x, y, z, block], ...]. Use it to build structures.",
	  S ({ "type" : "object", "properties" : { "blocks" : { "type" : "array", "items" : {} } }, "required" : ["blocks"] }),
	  t_set_blocks, false },
	{ "fill", "Fill the box from [x, y, z] to [x, y, z] (both included, 1000000 blocks at most) with one block; hollow:true keeps only the shell; \"air\" clears.",
	  S ({ "type" : "object", "properties" : { "from" : { "type" : "array", "items" : { "type" : "integer" } }, "to" : { "type" : "array", "items" : { "type" : "integer" } },
		  "block" : { "type" : ["string", "integer"] }, "hollow" : { "type" : "boolean" } }, "required" : ["from", "to", "block"] }),
	  t_fill, false },
	{ "post_message", "Show a message to the player, in the chat at the bottom of the game view.",
	  S ({ "type" : "object", "properties" : { "text" : { "type" : "string" } }, "required" : ["text"] }), t_post, false },
	{ "set_time", "Set the time of day (hour 0..24: 6 sunrise, 12 noon, 18 sunset, 0 midnight) and whether time runs.",
	  S ({ "type" : "object", "properties" : { "hour" : { "type" : "number" }, "running" : { "type" : "boolean" } }, "required" : ["hour"] }),
	  t_set_time, false },
	{ "screenshot",
	  "A PNG image of the game view: the player's view, or from eye = [x, y, z] with yaw/pitch or look_at = [x, y, z] (the player does not move). Use it to check what you built.",
	  S ({ "type" : "object", "properties" : { "eye" : { "type" : "array", "items" : { "type" : "number" } }, "look_at" : { "type" : "array", "items" : { "type" : "number" } },
		  "yaw" : { "type" : "number" }, "pitch" : { "type" : "number" }, "max_width" : { "type" : "integer" } } }),
	  t_screenshot, true },
	{ "get_events", "Blocks the player broke, placed or touched (key E) since the last call; the list is then emptied.",
	  S ({ "type" : "object", "properties" : {} }), t_events, false },
	{ "save_world", "Save the world now (it is also saved when the game closes).",
	  S ({ "type" : "object", "properties" : {} }), t_save, false },
};
#undef S

std::string rpcError (const std::string &id, int code, const std::string &msg)
{
	return JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("error", JO ().num ("code", code).str ("message", msg).end ()).end ();
}

std::string toolsList ()
{
	JA a;
	for (auto &t : TOOLS) {
		json_t *sc = json_parse (t.schema, strlen (t.schema));
		jbuf b;
		jb_init (&b);
		if (sc) jb_json (&b, sc);
		std::string schema = sc ? std::string (b.p, b.n) : "{\"type\":\"object\"}";
		jb_free (&b);
		json_free (sc);
		a.add (JO ().str ("name", t.name).str ("description", t.desc).raw ("inputSchema", schema)
			       .raw ("annotations", JO ().boo ("readOnlyHint", t.readOnly).boo ("destructiveHint", false).end ()).end ());
	}
	return JO ().raw ("tools", a.end ()).end ();
}

std::string toolCall (const json_t *params)
{
	std::string name = json_gets (params, "name");
	const json_t *args = json_get (params, "arguments");
	const ToolDef *t = nullptr;
	for (auto &d : TOOLS)
		if (name == d.name) t = &d;
	g_image.clear ();
	std::string text;
	bool err = false;
	g.agentCalls++;
	if (!t) text = "unknown tool: " + name, err = true;
	else
		try {
			text = t->fn (Args { args && args->kind == JSON_OBJ ? args : nullptr });
		} catch (const ToolErr &x) {
			text = x.msg, err = true;
		} catch (const std::exception &x) {
			text = std::string ("error: ") + x.what (), err = true;
		}
	JA content;
	if (!g_image.empty () && !err) content.add (JO ().str ("type", "image").str ("data", g_image).str ("mimeType", "image/png").end ());
	content.add (JO ().str ("type", "text").str ("text", text).end ());
	g_image.clear ();
	return JO ().raw ("content", content.end ()).boo ("isError", err).end ();
}

} // namespace

std::string mcpHandle (const std::string &in)
{
	json_t *m = json_parse (in.data (), in.size ());
	if (!m) return rpcError ("null", -32700, "parse error");
	std::string out;
	const json_t *idv = json_get (m, "id");
	std::string id;
	if (idv) {
		jbuf b;
		jb_init (&b);
		jb_json (&b, idv);
		id.assign (b.p, b.n);
		jb_free (&b);
	}
	std::string method = json_gets (m, "method");
	const json_t *params = json_get (m, "params");
	if (!idv || idv->kind == JSON_NULL) {	// (notification, ou réponse : rien à rendre)
		json_free (m);
		return "";
	}
	if (method == "initialize") {
		std::string want = params ? json_gets (params, "protocolVersion") : "";
		std::string ver = want == "2025-06-18" || want == "2025-03-26" || want == "2024-11-05" ? want : "2025-06-18";
		out = JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("result", JO ().str ("protocolVersion", ver)
			.raw ("capabilities", JO ().raw ("tools", JO ().boo ("listChanged", false).end ()).end ())
			.raw ("serverInfo", JO ().str ("name", "terracube").str ("title", "TerraCube (Korin OS voxel sandbox)").str ("version", VERSION).end ())
			.str ("instructions", "TerraCube is a voxel sandbox game: a world of 1-metre blocks, x east, y up (0..127, sea level 48), z south. "
			      "Start with get_game_info; survey the ground with get_heightmap before building; build with fill and set_blocks "
			      "(blocks by name, see list_block_types); check your work with screenshot (optionally from another eye position). "
			      "The player sees every change live.")
			.end ()).end ();
	} else if (method == "ping")
		out = JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("result", "{}").end ();
	else if (method == "tools/list")
		out = JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("result", toolsList ()).end ();
	else if (method == "tools/call") {
		if (!params) out = rpcError (id, -32602, "missing params");
		else out = JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("result", toolCall (params)).end ();
	} else if (method == "resources/list" || method == "prompts/list")
		out = JO ().str ("jsonrpc", "2.0").raw ("id", id).raw ("result", method == "resources/list" ? "{\"resources\":[]}" : "{\"prompts\":[]}").end ();
	else
		out = rpcError (id, -32601, "method not found: " + method);
	json_free (m);
	return out;
}

} // namespace tc
