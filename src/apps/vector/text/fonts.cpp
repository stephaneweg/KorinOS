//
// fonts.cpp -- the fonts of the card without opening them whole: Skia's directory manager reads every file at start, a
// minute on a slow card. Here a font's "name" and "OS/2" tables are read by themselves, and a face is only made when a
// text asks for it. Table layouts from the OpenType specification.
//
#include "fonts.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_empty.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <mutex>
#include <thread>

namespace vr {

namespace {
struct FontFile { std::string path, family; int weight = 400, width = 5; bool italic = false; int index = 0; sk_sp<SkTypeface> face; };

std::vector<FontFile> s_fonts;
bool s_scanned;

unsigned u16 (const uint8_t *p) { return p[0] << 8 | p[1]; }
unsigned u32 (const uint8_t *p) { return (unsigned) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// (the file is mapped: on this system stdio's seeks cost a thousand times more than touching the pages)
struct Mem { const uint8_t *p; size_t n; };
bool read_at (const Mem &f, long off, void *buf, size_t n)
{
	if ((size_t) off + n > f.n) return false;
	memcpy (buf, f.p + off, n);
	return true;
}

std::string utf16be (const uint8_t *p, unsigned n)
{
	std::string o;
	for (unsigned i = 0; i + 1 < n; i += 2)
	{
		unsigned c = u16 (p + i);
		if (c < 0x80) o += (char) c;
		else if (c < 0x800) { o += (char) (0xC0 | c >> 6); o += (char) (0x80 | (c & 63)); }
		else { o += (char) (0xE0 | c >> 12); o += (char) (0x80 | ((c >> 6) & 63)); o += (char) (0x80 | (c & 63)); }
	}
	return o;
}

bool scan_face (const Mem &f, long start, FontFile &ff)
{
	uint8_t hd[12];
	if (!read_at (f, start, hd, 12)) return false;
	unsigned tag = u32 (hd);
	if (tag != 0x00010000 && tag != 0x4F54544F && tag != 0x74727565) return false;
	unsigned n = u16 (hd + 4);
	if (n > 256) return false;
	uint8_t dir[256 * 16];
	if (!read_at (f, start + 12, dir, n * 16)) return false;
	long nameOff = 0, os2Off = 0;
	unsigned nameLen = 0;
	for (unsigned i = 0; i < n; i++)
	{
		const uint8_t *e = dir + i * 16;
		if (!memcmp (e, "name", 4)) { nameOff = u32 (e + 8); nameLen = u32 (e + 12); }
		else if (!memcmp (e, "OS/2", 4)) os2Off = u32 (e + 8);
	}
	if (!nameOff || nameLen > 1 << 20) return false;
	std::vector<uint8_t> nm (nameLen);
	if (!read_at (f, nameOff, nm.data (), nameLen)) return false;
	unsigned cnt = u16 (nm.data () + 2), strs = u16 (nm.data () + 4);
	std::string fam, typo;
	for (unsigned i = 0; i < cnt && 6 + (i + 1) * 12 <= nameLen; i++)
	{
		const uint8_t *r = nm.data () + 6 + i * 12;
		unsigned plat = u16 (r), lang = u16 (r + 4), id = u16 (r + 6), len = u16 (r + 8), off = u16 (r + 10);
		if ((id != 1 && id != 16) || plat != 3 || strs + off + len > nameLen) continue;
		bool english = (lang & 0x3FF) == 9;
		std::string s = utf16be (nm.data () + strs + off, len);
		if (id == 1 && (fam.empty () || english)) fam = s;
		if (id == 16 && (typo.empty () || english)) typo = s;
	}
	ff.family = !typo.empty () ? typo : fam;
	if (ff.family.empty ()) return false;
	if (os2Off)
	{
		uint8_t o[64];
		if (read_at (f, os2Off, o, 64))
		{
			ff.weight = u16 (o + 4);
			ff.width = u16 (o + 6);
			ff.italic = (u16 (o + 62) & 1) != 0;
		}
	}
	return true;
}

void scan_dir (const char *dir)
{
	DIR *d = opendir (dir);
	if (!d) return;
	while (dirent *e = readdir (d))
	{
		size_t l = strlen (e->d_name);
		if (l < 5) continue;
		const char *x = e->d_name + l - 4;
		if (strcasecmp (x, ".ttf") && strcasecmp (x, ".otf") && strcasecmp (x, ".ttc")) continue;
		std::string path = std::string (dir) + e->d_name;
		sk_sp<SkData> data = SkData::MakeFromFileName (path.c_str ());
		if (!data) continue;
		Mem f { (const uint8_t *) data->data (), data->size () };
		uint8_t hd[16];
		if (read_at (f, 0, hd, 16) && !memcmp (hd, "ttcf", 4))
		{
			unsigned n = u32 (hd + 8);
			for (unsigned i = 0; i < n && i < 64; i++)
			{
				uint8_t o[4];
				FontFile ff;
				ff.path = path; ff.index = (int) i;
				if (read_at (f, 12 + i * 4, o, 4) && scan_face (f, u32 (o), ff)) s_fonts.push_back (ff);
			}
		}
		else
		{
			FontFile ff;
			ff.path = path;
			if (scan_face (f, 0, ff)) s_fonts.push_back (ff);
		}
	}
	closedir (d);
}

std::mutex s_scan;
void scan ()
{
	std::lock_guard<std::mutex> l (s_scan);
	if (s_scanned) return;
	s_scanned = true;
	scan_dir ("SD0:/fonts/");
	scan_dir ("SD0:/home/fonts/");
	std::sort (s_fonts.begin (), s_fonts.end (), [] (const FontFile &a, const FontFile &b) { return a.family < b.family; });
}

sk_sp<SkFontMgr> &mgr ()
{
	static sk_sp<SkFontMgr> m = SkFontMgr_New_Custom_Empty ();
	return m;
}
sk_sp<SkTypeface> face_of (FontFile &f)
{
	if (!f.face) f.face = mgr ()->makeFromFile (f.path.c_str (), f.index);
	return f.face;
}
bool same_name (const std::string &a, const char *b) { return !strcasecmp (a.c_str (), b); }

// the CSS generic families and the usual names, on what the card has
const char *alias (const char *n)
{
	static const struct { const char *a, *b; } T[] = {
		{"sans-serif","DejaVu Sans"},{"sans","DejaVu Sans"},{"system-ui","DejaVu Sans"},{"Arial","DejaVu Sans"},{"Helvetica","DejaVu Sans"},{"Verdana","DejaVu Sans"},
		{"serif","DejaVu Serif"},{"Times New Roman","DejaVu Serif"},{"Times","DejaVu Serif"},{"Georgia","DejaVu Serif"},
		{"monospace","DejaVu Sans Mono"},{"Courier New","DejaVu Sans Mono"},{"Courier","DejaVu Sans Mono"} };
	for (auto &t : T) if (!strcasecmp (n, t.a)) return t.b;
	return n;
}
}

sk_sp<SkTypeface> font_ui ()
{
	static sk_sp<SkTypeface> f = mgr ()->makeFromFile ("SD0:/fonts/DejaVuSans.ttf", 0);
	return f;
}
void fonts_scan_async () { std::thread ([] { scan (); }).detach (); }

std::vector<std::string> font_families ()
{
	scan ();
	std::vector<std::string> o;
	for (auto &f : s_fonts) if (o.empty () || o.back () != f.family) o.push_back (f.family);
	return o;
}

sk_sp<SkTypeface> font_match (const char *family, int weight, int width, bool italic)
{
	scan ();
	const char *want = alias (family);
	FontFile *best = nullptr;
	int bestScore = 1 << 30;
	for (auto &f : s_fonts)
	{
		if (!same_name (f.family, want)) continue;
		int d = std::abs (f.weight - weight) + std::abs (f.width - width) * 50 + (f.italic != italic ? 1000 : 0);
		if (d < bestScore) { bestScore = d; best = &f; }
	}
	return best ? face_of (*best) : nullptr;
}

sk_sp<SkTypeface> font_for_char (uint32_t cp, int weight, bool italic)
{
	scan ();
	FontFile *best = nullptr;
	int bestScore = 1 << 30;
	// (DejaVu first: the family the card always has)
	for (int pass = 0; pass < 2; pass++)
	{
		for (auto &f : s_fonts)
		{
			if ((pass == 0) != !strncmp (f.family.c_str (), "DejaVu Sans", 11)) continue;
			int d = std::abs (f.weight - weight) + (f.italic != italic ? 1000 : 0);
			if (d >= bestScore) continue;
			sk_sp<SkTypeface> t = face_of (f);
			if (!t) continue;
			SkUnichar u = (SkUnichar) cp;
			uint16_t g = 0;
			t->unicharsToGlyphs (SkSpan<const SkUnichar> (&u, 1), SkSpan<uint16_t> (&g, 1));
			if (g) { bestScore = d; best = &f; }
		}
		if (best) break;
	}
	return best ? best->face : nullptr;
}

}
