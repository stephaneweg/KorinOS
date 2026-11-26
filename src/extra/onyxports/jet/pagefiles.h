// pagefiles.h -- des fichiers donnés à la page (26/11/2026) : commun à la fenêtre de Jet (main.cpp) et aux apps web
// (webapp.cpp). Le WebKit de Jet n'a ni glisser-déposer (ENABLE_DRAG_SUPPORT 0) ni images dans son presse-papiers (du
// texte seulement) : Ctrl+V d'une image ou de fichiers copiés, et des fichiers déposés depuis Fichiers (WEV_DROP), sont
// donnés à la page par engine_give_files (un collage ou un dépôt simulés, en JavaScript).
#pragma once

#include "engine.h"
#include <uikit.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <zlib.h>

// le type MIME d'un nom de fichier (par l'extension ; inconnu : application/octet-stream)
static inline const char *pf_mime (const char *name)
{
	static const char *const t[][2] = {
		{ "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" }, { "gif", "image/gif" },
		{ "webp", "image/webp" }, { "bmp", "image/bmp" }, { "svg", "image/svg+xml" }, { "ico", "image/x-icon" },
		{ "pdf", "application/pdf" }, { "zip", "application/zip" }, { "json", "application/json" },
		{ "txt", "text/plain" }, { "md", "text/markdown" }, { "csv", "text/csv" }, { "html", "text/html" },
		{ "htm", "text/html" }, { "css", "text/css" }, { "js", "text/javascript" }, { "xml", "application/xml" },
		{ "c", "text/x-c" }, { "h", "text/x-c" }, { "cpp", "text/x-c++" }, { "py", "text/x-python" },
		{ "rs", "text/x-rust" }, { "sh", "text/x-shellscript" }, { "mp3", "audio/mpeg" }, { "wav", "audio/wav" },
		{ "ogg", "audio/ogg" }, { "mp4", "video/mp4" }, { "webm", "video/webm" },
		{ "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
		{ "xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
	};
	const char *dot = strrchr (name, '.');
	if (dot && !strchr (dot, '/'))
		for (auto &e : t)
			if (!strcasecmp (dot + 1, e[0]))
				return e[1];
	return "application/octet-stream";
}

// un fichier entier -> false : illisible, ou plus grand que max
static inline bool pf_read (const char *path, std::string &out, unsigned long max)
{
	FILE *f = fopen (path, "rb");
	if (!f) return false;
	out.clear ();
	char b[65536];
	size_t n;
	while ((n = fread (b, 1, sizeof b, f)) > 0) {
		out.append (b, n);
		if (out.size () > max) { fclose (f); return false; }
	}
	fclose (f);
	return true;
}

// des pixels 0x00RRGGBB -> un PNG (RVB, 8 bits ; zlib)
static inline void pf_png_chunk (std::string &o, const char *type, const std::string &data)
{
	uint32_t n = (uint32_t) data.size ();
	unsigned char len[4] = { (unsigned char) (n >> 24), (unsigned char) (n >> 16), (unsigned char) (n >> 8), (unsigned char) n };
	o.append ((const char *) len, 4);
	std::string td = std::string (type, 4) + data;
	o += td;
	uLong c = crc32 (0, (const Bytef *) td.data (), (uInt) td.size ());
	unsigned char cb[4] = { (unsigned char) (c >> 24), (unsigned char) (c >> 16), (unsigned char) (c >> 8), (unsigned char) c };
	o.append ((const char *) cb, 4);
}

static inline bool pf_png (const uint32_t *px, long w, long h, std::string &o)
{
	if (w <= 0 || h <= 0) return false;
	std::string raw;
	raw.reserve ((size_t) (w * 3 + 1) * (size_t) h);
	for (long y = 0; y < h; y++) {
		raw += '\0';	// (aucun filtre)
		for (long x = 0; x < w; x++) {
			uint32_t v = px[y * w + x];
			raw += (char) (v >> 16); raw += (char) (v >> 8); raw += (char) v;
		}
	}
	uLongf zn = compressBound ((uLong) raw.size ());
	std::string z (zn, '\0');
	if (compress2 ((Bytef *) &z[0], &zn, (const Bytef *) raw.data (), (uLong) raw.size (), 6) != Z_OK) return false;
	z.resize (zn);
	o.assign ("\x89PNG\r\n\x1a\n", 8);
	unsigned char ih[13] = { (unsigned char) (w >> 24), (unsigned char) (w >> 16), (unsigned char) (w >> 8), (unsigned char) w,
				 (unsigned char) (h >> 24), (unsigned char) (h >> 16), (unsigned char) (h >> 8), (unsigned char) h,
				 8, 2, 0, 0, 0 };
	pf_png_chunk (o, "IHDR", std::string ((const char *) ih, 13));
	pf_png_chunk (o, "IDAT", z);
	pf_png_chunk (o, "IEND", "");
	return true;
}

// des chemins (un par ligne) donnés à la page -> 1 donnés, 0 aucun fichier lisible, -1 trop gros ; why : le message
static inline int pf_give_paths (const char *list, int x, int y, bool drop, std::string &why)
{
	std::vector<std::string> names, types, data;
	unsigned long total = 0;
	for (const char *l = list; l && *l;) {
		const char *e = strchr (l, '\n');
		std::string p = e ? std::string (l, (size_t) (e - l)) : std::string (l);
		l = e ? e + 1 : nullptr;
		while (!p.empty () && (p.back () == '\r' || p.back () == ' ')) p.pop_back ();
		if (p.empty () || p.back () == '/') continue;	// (un dossier : pas un fichier)
		std::string d;
		if (!pf_read (p.c_str (), d, (unsigned long) ENGINE_GIVE_MAX - total)) {
			if (total + d.size () > (unsigned long) ENGINE_GIVE_MAX) { why = "trop gros (32 Mo au plus)"; return -1; }
			continue;
		}
		total += d.size ();
		size_t s = p.rfind ('/');
		names.push_back (s == std::string::npos ? p : p.substr (s + 1));
		types.push_back (pf_mime (p.c_str ()));
		data.push_back (std::move (d));
	}
	if (names.empty ()) { why = "aucun fichier lisible"; return 0; }
	std::vector<const char *> n, t;
	std::vector<const unsigned char *> d;
	std::vector<unsigned long> s;
	for (size_t i = 0; i < names.size (); i++) {
		n.push_back (names[i].c_str ());
		t.push_back (types[i].c_str ());
		d.push_back ((const unsigned char *) data[i].data ());
		s.push_back ((unsigned long) data[i].size ());
	}
	if (!engine_give_files ((int) names.size (), n.data (), t.data (), d.data (), s.data (), x, y, drop)) {
		why = "trop gros (32 Mo au plus)";
		return -1;
	}
	return 1;
}

// Ctrl+V dans la page : une image (copiée par une capture, Dessin...) ou des fichiers (copiés dans Fichiers) donnés à
// la page -> true ; false : rien de tel, le collage ordinaire du texte (WebKit) se fait
static inline bool pf_paste (std::string &why)
{
	long rank = -1;
	long n = uikit_call3 ((long (*) ()) ui_clip_take, (long) "image\0files\0\0", 0, 0, 0, &rank, 0);
	if (n <= 0) return false;
	if (rank == 0) {	// une image : ses pixels en PNG
		long h = 0, bytes = 0;
		long w = uikit_call3 ((long (*) ()) ui_clip_get_image, 0, 0, 0, 0, &h, &bytes);
		if (w <= 0 || h <= 0 || bytes < w * h * 4) return false;
		std::vector<uint32_t> px ((size_t) (w * h));
		uikit_call3 ((long (*) ()) ui_clip_get_image, (long) px.data (), (long) px.size () * 4, 0, 0, 0, 0);
		std::string png;
		if (!pf_png (px.data (), w, h, png)) return false;
		const char *nm = "image.png", *ty = "image/png";
		const unsigned char *d = (const unsigned char *) png.data ();
		unsigned long s = (unsigned long) png.size ();
		if (!engine_give_files (1, &nm, &ty, &d, &s, 0, 0, false)) { why = "image trop grande"; return true; }
		return true;
	}
	std::vector<char> b ((size_t) n + 1, 0);	// des fichiers : leurs chemins, un par ligne
	long got = uikit_call3 ((long (*) ()) ui_clip_get_files, (long) b.data (), (long) b.size () - 1, 0, 0, 0, 0);
	if (got <= 0) return false;
	b[(size_t) got < b.size () ? (size_t) got : b.size () - 1] = 0;
	pf_give_paths (b.data (), 0, 0, false, why);
	return true;
}

// WEV_DROP sur la page (x, y : dans la page) : des fichiers déposés -> true ; false : pas des fichiers
static inline bool pf_drop (int x, int y, std::string &why)
{
	long kind = 0;
	long n = uikit_drop_get (nullptr, 0, &kind);
	if (n <= 0 || kind != UDT_FILES) return false;
	std::vector<char> b ((size_t) n + 1, 0);
	uikit_drop_get (b.data (), (long) b.size (), &kind);
	pf_give_paths (b.data (), x, y, true, why);
	return true;
}
