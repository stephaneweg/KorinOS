//
// help -- le lecteur d'aide de Korin OS : un fichier Markdown montre sur UIKit, dans le theme du bureau.
//
//   help                      le sommaire : les apps qui ont une aide, puis le guide du developpeur
//   help <fichier.md>         ce fichier (la barre de menus l'ouvre par son menu Aide :
//                             SD0:/apps/<nom>.app/help_<langue>.md)
//
// Une app pose son aide a cote de son executable : help_fr.md, help_en.md, et ses images (PNG, BMP) dans son dossier
// (help/ par exemple). Le lecteur prend celui de la langue du systeme, sinon l'autre. Les liens :
//   #ancre                    un titre de la page (minuscules, espaces en « - »)
//   fichier.md, ../x/y.md     une autre page, relative a celle-ci (fichier_fr.md absent : fichier_en.md, et l'inverse)
//   help:<app>                l'aide d'une app ; app:<app> la lance ; http(s):// : Jet
// La fenetre : precedent, suivant, sommaire, la table des matieres (les titres) a gauche, la recherche (Ctrl+F, F3),
// la page a droite ; chaque bloc de code a son bouton Copier. Le Markdown lu : markdown.h.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <dirent.h>
#include <spawn.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <uikit.h>
#include "markdown.h"

#define TR(s)	((const char *) ui_tr ((long) (s)))

extern char **environ;

static struct ui_vars ui;
static const unsigned *s_jet;
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE, J_BARRE, J_DOCK,
       J_TITRE_ACTIF, J_TITRE_INACTIF, J_CONTOUR, J_SOMBRE };
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline bool dark () { return s_jet[J_SOMBRE] & 1; }

static const char *HELP_DIR = "SD0:/apps/help.app";
enum { TOPH = 48, SIDEW = 230, SBW = 12, CAP = FRAME_CAP_W + 8, MAXW = 860 };
enum { M_OPEN = 1, M_CLOSE, M_COPYPAGE = 10, M_FIND, M_FINDNEXT, M_BACK = 20, M_FWD, M_HOME, M_API,
       M_BIGGER = 30, M_SMALLER, M_NORMAL, M_TOC };

// ---- la page mise en page -------------------------------------------------------------------------------------------
// role d'un morceau : sa police et sa couleur
enum { R_BODY, R_HEAD, R_QUOTE, R_CODE, R_NUM, R_TH, R_ALT };
struct Frag
{
	int x, y, w, h, base, px;		// y, h : la ligne ; base : ligne de base
	std::string s;
	int style, role, link;
	int clip;				// > 0 : largeur max (lignes de code coupees par « … »)
};
enum { D_CODEBG, D_QUOTEBAR, D_HR, D_IMAGE, D_THBG, D_HLINE, D_VLINE, D_BULLET, D_FRAME, D_TCODE };
struct Deco { int kind, x, y, w, h; long a; };
struct TLine { int y, h; std::string text; };	// une ligne de texte (recherche)
struct Head { int level; std::string text, slug; int y; };
struct CopyBtn { int x, y, w, h, block; };

static std::vector<md::Block> g_blocks;
static std::vector<std::string> g_links;	// cibles des liens de la page
static std::vector<Frag> g_frags;
static std::vector<Deco> g_decos;
static std::vector<TLine> g_tlines;
static std::vector<Head> g_heads;
static std::vector<CopyBtn> g_copies;
static std::map<std::string, long> g_images;	// images chargees (chemin -> image UIKit)
static int g_docH;				// hauteur de la page
static int g_layW = -1;				// largeur de la derniere mise en page
static int g_zoom = 100;			// taille du texte, en %

static std::string g_path;			// la page ouverte ("" : le sommaire)
static std::string g_dir;			// son dossier (liens et images relatifs)
static std::string g_raw;			// son texte
static std::string g_title;

// ---- la fenetre ---------------------------------------------------------------------------------------------------
static long g_view, g_bar, g_toc, g_find, g_bBack, g_bFwd, g_bHome, g_bToc, g_tb;
static int g_ww, g_wh, g_vw, g_vh;		// fenetre, vue
static int s_top;				// defilement de la vue
static bool g_showToc = true;
static int g_hoverLink = -1, g_hoverCopy = -1;
static int g_copied = -1;			// bloc copie (« Copie ») ...
static long g_copiedUntil;			// ... jusqu'a
static int g_hit = -1;				// ligne trouvee par la recherche
static int g_pendLink = -1, g_pendCopy = -1;	// (fait par la boucle, pas dans ui_w_event)

struct Hist { std::string path; int top; };
static std::vector<Hist> g_hist;
static int g_hpos = -1;

static inline long sys_call (long n, long a = 0, long b = 0, long c = 0)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x8) : "memory");
	return x0;
}
static long now_ms () { return sys_call (0x19); }

static int px_of (int base) { return base * g_zoom / 100; }
static long font_for (int style, int role)
{
	if (role == R_CODE || (style & md::S_CODE)) return (style & md::S_BOLD) ? ui_font_get (UFN_MONOBOLD) : (long) ui.font_m;
	bool b = (style & md::S_BOLD) || role == R_HEAD || role == R_TH;
	bool i = (style & md::S_ITALIC) || role == R_ALT;
	if (b && i) return ui_font_get (UFN_BOLDITALIC);
	if (b) return (long) ui.font_b;
	if (i) return ui_font_get (UFN_ITALIC);
	return (long) ui.font;
}
static int text_w (const std::string &s, int px, long f) { return (int) ui_text_width ((long) s.c_str (), px, f); }

static bool exists (const std::string &p)
{
	FILE *f = fopen (p.c_str (), "rb");
	if (!f) return false;
	fclose (f);
	return true;
}
static bool read_file (const std::string &p, std::string &out)
{
	FILE *f = fopen (p.c_str (), "rb");
	if (!f) return false;
	char buf[8192];
	size_t n;
	out.clear ();
	while ((n = fread (buf, 1, sizeof buf, f)) > 0) out.append (buf, n);
	fclose (f);
	return true;
}
static std::string lang () { const char *l = (const char *) ui_lang (); return l && *l ? l : "fr"; }
static void spawn (const char *path, const char *arg)
{
	char *argv[3] = { (char *) path, arg && *arg ? (char *) arg : 0, 0 };
	pid_t pid;
	posix_spawn (&pid, path, 0, 0, argv, environ);
}

// ---- les fichiers ---------------------------------------------------------------------------------------------------

// fichier_fr.md absent : fichier_en.md (et l'inverse) ; fichier.md absent : fichier_<langue>.md
static std::string localized (const std::string &p)
{
	if (p.empty () || exists (p)) return p;
	std::string L = lang ();
	size_t k = p.rfind ('_');
	if (k != std::string::npos && p.size () == k + 6 && p.compare (k + 3, 3, ".md") == 0)
	{
		std::string stem = p.substr (0, k + 1);
		for (const char *l : { L.c_str (), "en", "fr" })
			if (exists (stem + l + ".md")) return stem + l + ".md";
		return p;
	}
	if (p.size () > 3 && p.compare (p.size () - 3, 3, ".md") == 0)
	{
		std::string stem = p.substr (0, p.size () - 3) + "_";
		for (const char *l : { L.c_str (), "en", "fr" })
			if (exists (stem + l + ".md")) return stem + l + ".md";
	}
	return p;
}
// l'aide d'une app : SD0:/apps/<nom>.app/help_<langue>.md ("" : aucune)
static std::string app_help (const std::string &app)
{
	std::string p = localized ("SD0:/apps/" + app + ".app/help_" + lang () + ".md");
	return exists (p) ? p : "";
}
// chemin relatif a la page -> chemin complet (« .. » et « . » resolus)
static std::string resolve (const std::string &rel)
{
	if (rel.find (":/") != std::string::npos || rel[0] == '/') return rel;
	std::string full = g_dir + "/" + rel;
	size_t c = full.find (":/");
	std::string head = c == std::string::npos ? "" : full.substr (0, c + 2);
	std::string rest = c == std::string::npos ? full : full.substr (c + 2);
	std::vector<std::string> parts;
	size_t a = 0;
	while (a <= rest.size ())
	{
		size_t b = rest.find ('/', a);
		if (b == std::string::npos) b = rest.size ();
		std::string s = rest.substr (a, b - a);
		if (s == "..") { if (!parts.empty ()) parts.pop_back (); }
		else if (!s.empty () && s != ".") parts.push_back (s);
		a = b + 1;
	}
	std::string o = head;
	for (size_t i = 0; i < parts.size (); i++) { if (i) o += '/'; o += parts[i]; }
	return o;
}
// l'image d'un chemin (gardee jusqu'a la page suivante ; 0 : illisible)
static long image_of (const std::string &src)
{
	std::string p = resolve (src);
	auto it = g_images.find (p);
	if (it != g_images.end ()) return it->second;
	long img = ui_image_load ((long) p.c_str ());
	g_images[p] = img;
	return img;
}

// ---- le sommaire ----------------------------------------------------------------------------------------------------
// les apps de SD0:/apps qui ont une aide, par nom, puis le guide du developpeur
static std::string index_doc ()
{
	struct App { std::string name, help, desc; };
	std::vector<App> apps;
	if (DIR *d = opendir ("SD0:/apps"))
	{
		while (struct dirent *e = readdir (d))
		{
			std::string n = e->d_name;
			if (n.size () < 5 || n.compare (n.size () - 4, 4, ".app") != 0) continue;
			n.resize (n.size () - 4);
			std::string h = app_help (n);
			if (h.empty ()) continue;
			App a;
			a.name = n;
			a.help = h;
			std::string man;
			if (read_file ("SD0:/apps/" + n + ".app/app.txt", man))
			{
				size_t s = 0;
				while (s < man.size ())
				{
					size_t t = man.find ('\n', s);
					if (t == std::string::npos) t = man.size ();
					std::string l = man.substr (s, t - s);
					s = t + 1;
					size_t eq = l.find ('=');
					if (eq == std::string::npos || l[0] == '#') continue;
					std::string k = l.substr (0, eq), v = l.substr (eq + 1);
					while (!k.empty () && (k.back () == ' ' || k.back () == '\t')) k.pop_back ();
					while (!v.empty () && (v[0] == ' ' || v[0] == '\t')) v.erase (0, 1);
					while (!v.empty () && (v.back () == '\r' || v.back () == ' ')) v.pop_back ();
					if (k == "name" && a.name == n) a.name = TR (v.c_str ());
					else if (k == "description" && a.desc.empty ()) a.desc = v;
				}
			}
			apps.push_back (a);
		}
		closedir (d);
	}
	std::sort (apps.begin (), apps.end (), [] (const App &x, const App &y) { return strcasecmp (x.name.c_str (), y.name.c_str ()) < 0; });
	bool fr = lang () == "fr";
	std::string o = std::string ("# ") + TR ("Aide de Korin OS") + "\n\n";
	o += TR ("Chaque application qui a une aide l'ouvre par le menu **Aide** de la barre de menus. Les voici toutes.");
	o += "\n\n## ";
	o += TR ("Applications");
	o += "\n\n";
	for (const App &a : apps)
	{
		o += "- [" + a.name + "](" + a.help + ")";
		if (fr && !a.desc.empty ()) o += " : " + a.desc;		// (les manifestes sont en francais)
		o += "\n";
	}
	if (apps.empty ()) o += std::string (TR ("Aucune application n'a d'aide.")) + "\n";
	o += "\n## ";
	o += TR ("Pour les d\xC3\xA9veloppeurs");
	o += "\n\n- [";
	o += TR ("Guide du d\xC3\xA9veloppeur : l'API du syst\xC3\xA8me en C et C++");
	o += "](" + localized (std::string (HELP_DIR) + "/api/index_" + lang () + ".md") + ")\n";
	return o;
}

// ---- la mise en page ------------------------------------------------------------------------------------------------

static int g_pad = 28;
static int line_h (int px) { return px * 160 / 100; }

// une ligne de texte finie : pour la recherche
static void tline (int y, int h, int from)
{
	TLine t;
	t.y = y;
	t.h = h;
	for (size_t i = (size_t) from; i < g_frags.size (); i++)
	{
		if (!t.text.empty ()) t.text += ' ';
		t.text += g_frags[i].s;
	}
	if (!t.text.empty ()) g_tlines.push_back (t);
}

// les morceaux d'un texte, coupes en lignes dans [x0, x0 + width) a partir de y -> bas du texte
static int flow (const std::vector<md::Run> &runs, int x0, int width, int y, int px, int role)
{
	int lh = line_h (px);
	int x = x0, top = y, first = (int) g_frags.size ();
	bool any = false;
	auto newline = [&] ()
	{
		tline (top, lh, first);
		first = (int) g_frags.size ();
		top += lh;
		x = x0;
	};
	for (const md::Run &r : runs)
	{
		if (r.style & md::S_BREAK) { newline (); any = false; continue; }
		long f = font_for (r.style, role);
		int fpx = (r.style & md::S_CODE) ? px - 1 : px;
		int pad = (r.style & md::S_CODE) ? 4 : 0;
		int spw = text_w (" ", fpx, f);
		size_t i = 0;
		const std::string &s = r.text;
		while (i < s.size ())
		{
			bool sp = false;
			while (i < s.size () && s[i] == ' ') { sp = true; i++; }
			if (i >= s.size ()) { if (sp && x > x0) x += spw; break; }
			size_t j = i;
			while (j < s.size () && s[j] != ' ') j++;
			std::string word = s.substr (i, j - i);
			i = j;
			int ww = text_w (word, fpx, f) + 2 * pad;
			int lead = sp && x > x0 ? spw : 0;
			if (x > x0 && x + lead + ww > x0 + width) { newline (); lead = 0; }
			x += lead;
			// un mot plus large que la ligne : coupe ou il depasse
			while (ww > width && word.size () > 1)
			{
				long px1 = 0;
				long nb = uikit_text_hit (word.c_str (), fpx, f, width - (x - x0) - 2 * pad, &px1);
				if (nb < 1) nb = 1;
				std::string part = word.substr (0, (size_t) nb);
				Frag fr = { x, top, text_w (part, fpx, f) + 2 * pad, lh, top + lh / 2 + px * 37 / 100, fpx, part, r.style, role, r.link, 0 };
				g_frags.push_back (fr);
				newline ();
				word = word.substr ((size_t) nb);
				ww = text_w (word, fpx, f) + 2 * pad;
			}
			// meme style que le morceau d'avant, sur la meme ligne : il s'allonge
			Frag *pv = (int) g_frags.size () > first ? &g_frags.back () : 0;
			if (pv && pv->style == r.style && pv->link == r.link && pv->role == role && pv->x + pv->w + lead == x)
			{
				pv->s += (lead ? " " : "") + word;
				pv->w = text_w (pv->s, fpx, f) + 2 * pad;	// (mesure du tout : la somme des mots derive)
				x = pv->x + pv->w;
				any = true;
				continue;
			}
			else
			{
				Frag fr = { x, top, ww, lh, top + lh / 2 + px * 37 / 100, fpx, word, r.style, role, r.link, 0 };
				g_frags.push_back (fr);
			}
			x += ww;
			any = true;
		}
	}
	if (any || (int) g_frags.size () > first) { newline (); }
	return top;
}

static int flow_text (const std::string &text, int x0, int width, int y, int px, int role)
{
	std::vector<md::Run> runs = md::inlines (text, g_links);
	return flow (runs, x0, width, y, px, role);
}

static void deco (int k, int x, int y, int w, int h, long a = 0) { g_decos.push_back ({ k, x, y, w, h, a }); }

static void layout (int vw)
{
	g_frags.clear (); g_decos.clear (); g_tlines.clear (); g_heads.clear (); g_copies.clear (); g_links.clear ();
	g_layW = vw;
	g_pad = vw < 520 ? 16 : 28;
	int width = vw - 2 * g_pad - SBW;
	if (width > MAXW) width = MAXW;
	if (width < 120) width = 120;
	int X = g_pad, y = 20;
	int body = px_of (15), indentW = px_of (24);
	for (size_t bi = 0; bi < g_blocks.size (); bi++)
	{
		const md::Block &b = g_blocks[bi];
		const md::Block *prev = bi ? &g_blocks[bi - 1] : 0;
		int x0 = X + b.indent * indentW, w0 = width - b.indent * indentW;
		switch (b.kind)
		{
		case md::HEAD:
		{
			static const int S[6] = { 28, 22, 18, 16, 15, 14 };
			int px = px_of (S[b.level - 1]);
			if (bi) y += b.level <= 2 ? px_of (18) : px_of (10);
			g_heads.push_back ({ b.level, md::plain (b.text), md::slug (b.text), y - 8 });
			y = flow_text (b.text, X, width, y, px, R_HEAD);
			if (b.level <= 2) { deco (D_HLINE, X, y + 4, width, 1); y += 10; }
			y += px_of (6);
			break;
		}
		case md::PARA:
			y = flow_text (b.text, x0, w0, y, body, R_BODY) + px_of (10);
			break;
		case md::ITEM:
		{
			int ix = X + b.level * indentW, mw = px_of (22);
			int lh = line_h (body);
			if (b.ordered)
			{
				std::string num = std::to_string (b.number) + ".";
				long f = font_for (0, R_NUM);
				int nw = text_w (num, body, f);
				g_frags.push_back ({ ix + mw - 6 - nw, y, nw, lh, y + lh / 2 + body * 37 / 100, body, num, 0, R_NUM, -1, 0 });
			}
			else deco (D_BULLET, ix + mw / 2 - 6, y + lh / 2, 0, 0, b.level);
			y = flow_text (b.text, ix + mw, width - b.level * indentW - mw, y, body, R_BODY);
			bool nextItem = bi + 1 < g_blocks.size () && (g_blocks[bi + 1].kind == md::ITEM || g_blocks[bi + 1].indent > 0);
			y += nextItem ? px_of (4) : px_of (12);
			break;
		}
		case md::QUOTE:
		{
			int y0 = y;
			y = flow_text (b.text, x0 + 18, w0 - 18, y + 4, body, R_QUOTE) + 4;
			deco (D_QUOTEBAR, x0, y0, w0, y - y0);
			y += px_of (10);
			(void) prev;
			break;
		}
		case md::CODE:
		{
			int px = px_of (13), lh = px * 150 / 100, pad = 12;
			int bw = text_w (TR ("Copier"), px_of (12), (long) ui.font) + 20, bh = px_of (12) + 12;
			int n = 1;
			for (char c : b.code) if (c == '\n') n++;
			int h = n * lh + 2 * pad, y0 = y;
			deco (D_CODEBG, x0, y0, w0, h, (long) bi);
			size_t a = 0;
			int ly = y0 + pad;
			while (a <= b.code.size ())
			{
				size_t e = b.code.find ('\n', a);
				if (e == std::string::npos) e = b.code.size ();
				std::string l = b.code.substr (a, e - a);
				// (tabulations : quatre espaces)
				for (size_t k; (k = l.find ('\t')) != std::string::npos;) l.replace (k, 1, "    ");
				if (!l.empty ())
				{
					g_frags.push_back ({ x0 + pad, ly, w0 - 2 * pad, lh, ly + lh / 2 + px * 37 / 100, px, l, 0, R_CODE, -1, ly < y + 6 + bh ? w0 - pad - bw - 12 : w0 - 2 * pad });
					g_tlines.push_back ({ ly, lh, l });
				}
				ly += lh;
				a = e + 1;
			}
			g_copies.push_back ({ x0 + w0 - bw - 6, y0 + 6, bw, bh, (int) bi });
			y = y0 + h + px_of (14);
			break;
		}
		case md::HR:
			y += px_of (8);
			deco (D_HR, X, y, width, 1);
			y += px_of (16);
			break;
		case md::IMAGE:
		{
			long img = image_of (b.code);
			long ih = 0, pix = 0;
			long iw = img ? uikit_image_size (img, &ih, &pix) : 0;
			if (img && iw > 0 && ih > 0)
			{
				int dw = (int) iw * g_zoom / 100, dh = (int) ih * g_zoom / 100;
				if (dw > w0) { dh = (int) ((long) dh * w0 / dw); dw = w0; }
				deco (D_IMAGE, x0, y, dw, dh, img);
				y += dh + px_of (6);
				if (!b.text.empty ()) y = flow_text (b.text, x0, w0, y, px_of (13), R_ALT);
				y += px_of (10);
			}
			else y = flow_text ("[" + b.text + "] (" + b.code + ")", x0, w0, y, body, R_ALT) + px_of (10);
			break;
		}
		case md::TABLE:
		{
			size_t nc = 0;
			for (auto &r : b.rows) nc = std::max (nc, r.size ());
			if (!nc) break;
			int cpad = 10, px = px_of (14);
			std::vector<int> nat (nc, 40);
			for (size_t r = 0; r < b.rows.size (); r++)
				for (size_t c = 0; c < b.rows[r].size (); c++)
				{
					long f = font_for (0, r == 0 && b.header ? R_TH : R_BODY);
					int tw = text_w (md::plain (b.rows[r][c]), px, f) + 2 * cpad + 4;
					nat[c] = std::max (nat[c], tw);
				}
			int total = 0;
			for (int v : nat) total += v;
			std::vector<int> cw = nat;
			if (total > w0)
			{
				// trop large : chaque colonne a sa part, les etroites gardent leur largeur
				int fair = w0 / (int) nc, rest = w0, wide = 0;
				for (size_t c = 0; c < nc; c++) { if (nat[c] <= fair) rest -= nat[c]; else wide += nat[c]; }
				for (size_t c = 0; c < nc; c++) if (nat[c] > fair) cw[c] = std::max (60, (int) ((long) nat[c] * rest / std::max (1, wide)));
				total = 0;
				for (int v : cw) total += v;
			}
			int y0 = y;
			for (size_t r = 0; r < b.rows.size (); r++)
			{
				bool th = r == 0 && b.header;
				int cx = x0, bottom = y + line_h (px) + 2 * 6;
				size_t d0 = g_decos.size ();
				if (th) deco (D_THBG, x0, y, total, 0);
				for (size_t c = 0; c < nc; c++)
				{
					if (c < b.rows[r].size ())
						bottom = std::max (bottom, flow_text (b.rows[r][c], cx + cpad, cw[c] - 2 * cpad, y + 6, px, th ? R_TH : R_BODY) + 6);
					cx += cw[c];
				}
				if (th) g_decos[d0].h = bottom - y;
				y = bottom;
				deco (D_HLINE, x0, y, total, 1);
			}
			deco (D_FRAME, x0, y0, total, y - y0);
			int cx = x0;
			for (size_t c = 0; c + 1 < nc; c++) { cx += cw[c]; deco (D_VLINE, cx, y0, 1, y - y0); }
			y += px_of (14);
			break;
		}
		}
	}
	g_docH = y + 40;
}

// ---- le dessin ------------------------------------------------------------------------------------------------------

static unsigned color_of (const Frag &f)
{
	if (f.link >= 0) return dark () ? jet (J_ACCENT_CLAIR) : jet (J_ACCENT);
	switch (f.role)
	{
	case R_QUOTE: return jet (J_TEXTE2);
	case R_ALT: return jet (J_TEXTE3);
	case R_NUM: return jet (J_TEXTE2);
	default: return jet (J_TEXTE1);
	}
}

static void view_paint (long, long w, long h)
{
	unsigned bg = jet (J_SURFACE), code = mix (bg, jet (J_TEXTE1), dark () ? 24 : 14), line = jet (J_LIGNE);
	ui_fill (0, 0, w, h, bg);
	int top = s_top, bot = s_top + (int) h;
	auto vis = [&] (int y, int hh) { return y + hh >= top && y <= bot; };
	for (const Deco &d : g_decos)
	{
		if (!vis (d.y, d.h + 12)) continue;
		int y = d.y - top;
		switch (d.kind)
		{
		case D_CODEBG: ui_rbox (d.x, y, d.w, d.h, 8, code, code); break;
		case D_QUOTEBAR:
			ui_rbox (d.x, y, d.w, d.h, 6, mix (bg, jet (J_ACCENT), 18), mix (bg, jet (J_ACCENT), 18));
			ui_fill (d.x, y, 4, d.h, jet (J_ACCENT));
			break;
		case D_HR: case D_HLINE: ui_fill (d.x, y, d.w, 1, line); break;
		case D_VLINE: ui_fill (d.x, y, 1, d.h, line); break;
		case D_FRAME: ui_frame (d.x, y, d.w + 1, d.h + 1, line); break;
		case D_THBG: ui_fill (d.x, y, d.w, d.h, code); break;
		case D_IMAGE: ui_image_draw (d.a, d.x, y, d.w, d.h); break;
		case D_BULLET:
		{
			unsigned c = jet (J_TEXTE2);
			if (d.a == 0) ui_rbox (d.x, y - 3, 6, 6, 3, c, c);
			else if (d.a == 1) { ui_rbox (d.x, y - 3, 6, 6, 3, c, c); ui_rbox (d.x + 1, y - 2, 4, 4, 2, bg, bg); }
			else ui_fill (d.x + 1, y - 2, 4, 4, c);
			break;
		}
		}
	}
	if (g_hit >= 0 && g_hit < (int) g_tlines.size ())
	{
		const TLine &t = g_tlines[g_hit];
		unsigned c = mix (bg, jet (J_ALERTE), 70);
		ui_rbox (g_pad - 8, t.y - top, g_vw - 2 * g_pad + 16 - SBW, t.h, 4, c, c);
	}
	for (const Frag &f : g_frags)
	{
		if (!vis (f.y, f.h)) continue;
		int y = f.y - top, base = f.base - top;
		long font = font_for (f.style, f.role);
		unsigned c = color_of (f);
		if ((f.style & md::S_CODE) && f.role != R_CODE)
		{
			unsigned cb = mix (bg, jet (J_TEXTE1), dark () ? 24 : 14);
			ui_rbox (f.x, y + 3, f.w, f.h - 6, 4, cb, cb);
			ui_text ((long) f.s.c_str (), f.x + 4, base, c, f.px, font, 0);
		}
		else if (f.clip > 0) ui_text_fit ((long) f.s.c_str (), f.x, base, c, f.px, font, f.clip);
		else ui_text ((long) f.s.c_str (), f.x, base, c, f.px, font, 0);
		if (f.link >= 0 && f.link == g_hoverLink) ui_fill (f.x, base + 2, f.w, 1, c);
	}
	for (size_t i = 0; i < g_copies.size (); i++)
	{
		const CopyBtn &b = g_copies[i];
		if (!vis (b.y, b.h)) continue;
		bool done = g_copied == b.block, hot = g_hoverCopy == (int) i;
		unsigned c = done ? jet (J_SUCCES) : hot ? jet (J_ACCENT) : mix (code, jet (J_TEXTE1), 30);
		unsigned ink = done || hot ? (unsigned) ui_ink (c) & 0xFFFFFF : jet (J_TEXTE1);
		const char *t = TR (done ? "Copi\xC3\xA9" : "Copier");
		int px = px_of (12);
		long f = (long) ui.font;
		ui_rbox (b.x, b.y - top, b.w, b.h, b.h / 2, c, c);
		ui_text ((long) t, b.x + (b.w - text_w (t, px, f)) / 2, b.y - top + b.h / 2 + px * 37 / 100, ink, px, f, 0);
	}
}

// ---- la vue : defilement, souris, clavier ---------------------------------------------------------------------------

static void clamp ()
{
	int mx = g_docH - g_vh;
	if (s_top > mx) s_top = mx;
	if (s_top < 0) s_top = 0;
}
static void bar_sync ()
{
	clamp ();
	ui_w_set_range (g_bar, 0, std::max (0, g_docH - g_vh), g_vh, 40);
	ui_w_set_value (g_bar, s_top);
	ui_w_show (g_bar, g_docH > g_vh);
}
static void scroll_to (int top) { s_top = top; bar_sync (); ui_view_dirty (g_view); }

static int link_at (int x, int y)
{
	for (const Frag &f : g_frags)
		if (f.link >= 0 && x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h) return f.link;
	return -1;
}
static int copy_at (int x, int y)
{
	for (size_t i = 0; i < g_copies.size (); i++)
	{
		const CopyBtn &b = g_copies[i];
		if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return (int) i;
	}
	return -1;
}

static bool s_down, s_drag;
static int s_pressY, s_pressTop, s_pressLink, s_pressCopy;
static long view_mouse (long, long kind, long mx, long my, long info)
{
	int x = (int) mx, y = (int) my + s_top;
	int l = kind == 3 ? -1 : link_at (x, y), c = kind == 3 ? -1 : copy_at (x, y);
	if (kind == 1 && (info >> 8 & 1))
	{
		s_down = true; s_drag = false; s_pressY = (int) my; s_pressTop = s_top; s_pressLink = l; s_pressCopy = c;
	}
	else if (kind == 0 && s_down)			// (au toucher : glisser fait defiler)
	{
		int dy = (int) my - s_pressY;
		if (s_drag || dy > 8 || dy < -8) { s_drag = true; scroll_to (s_pressTop - dy); return 0; }
	}
	else if (kind == 2)
	{
		if (s_down && !s_drag)
		{
			if (l >= 0 && l == s_pressLink) g_pendLink = l;
			else if (c >= 0 && c == s_pressCopy) g_pendCopy = g_copies[c].block;
		}
		s_down = false;
	}
	if (l != g_hoverLink || c != g_hoverCopy)
	{
		g_hoverLink = l; g_hoverCopy = c;
		ui_cursor (l >= 0 || c >= 0 ? UC_HAND : UC_ARROW);
		ui_view_dirty (g_view);
	}
	return 0;
}
static long view_wheel (long, long steps)
{
	scroll_to (s_top - (int) steps * 3 * line_h (px_of (15)));
	return 0;
}

static void go_back ();
static void go_forward ();
static void zoom (int z);
static long view_key (long, long code, long ch, long mods)
{
	int page = g_vh - 40, step = line_h (px_of (15));
	bool alt = mods & UI_MOD_ALT, ctrl = mods & UI_MOD_CTRL;
	switch (code)
	{
	case 0x51: scroll_to (s_top + step); return 0;			// bas
	case 0x52: scroll_to (s_top - step); return 0;			// haut
	case 0x4E: case 0x2C: scroll_to (s_top + page); return 0;	// page suivante, espace
	case 0x4B: scroll_to (s_top - page); return 0;			// page precedente
	case 0x4A: scroll_to (0); return 0;				// debut
	case 0x4D: scroll_to (g_docH); return 0;			// fin
	case 0x50: if (alt) { go_back (); return 0; } break;		// Alt+gauche
	case 0x4F: if (alt) { go_forward (); return 0; } break;		// Alt+droite
	case 0x2A: go_back (); return 0;				// retour arriere
	}
	if (ctrl && (ch == '+' || ch == '=')) { zoom (g_zoom + 10); return 0; }
	if (ctrl && ch == '-') { zoom (g_zoom - 10); return 0; }
	if (ctrl && ch == '0') { zoom (100); return 0; }
	return -1;
}

// ---- l'ouverture d'une page -----------------------------------------------------------------------------------------

static void fill_toc ()
{
	ui_list_clear (g_toc);
	for (size_t i = 0; i < g_heads.size (); i++)
	{
		const Head &h = g_heads[i];
		if (h.level > 3) continue;
		std::string t = h.level == 1 ? "#" + h.text : h.level == 2 ? h.text : "    " + h.text;
		ui_list_add (g_toc, (long) t.c_str (), (long) i);
	}
	if (g_showToc && g_ww >= 640) ui_w_draw (g_toc);
}
static void relayout ()
{
	layout (g_vw);
	bar_sync ();
	ui_view_dirty (g_view);
}
static void build_menu ();
static void update_buttons ()
{
	ui_w_enable (g_bBack, g_hpos > 0);
	ui_w_enable (g_bFwd, g_hpos + 1 < (int) g_hist.size ());
	build_menu ();
}

static void show (const std::string &path, const std::string &anchor, int top)
{
	for (auto &kv : g_images) if (kv.second) ui_image_free (kv.second);
	g_images.clear ();
	g_path = path;
	if (path.empty ())
	{
		g_raw = index_doc ();
		g_dir = HELP_DIR;
	}
	else
	{
		if (!read_file (path, g_raw))
			g_raw = std::string ("# ") + TR ("Page introuvable") + "\n\n`" + path + "`\n";
		size_t k = path.rfind ('/');
		g_dir = k == std::string::npos ? "" : path.substr (0, k);
	}
	g_blocks = md::parse (g_raw);
	g_title.clear ();
	for (const md::Block &b : g_blocks) if (b.kind == md::HEAD) { g_title = md::plain (b.text); break; }
	std::string wt = g_title.empty () ? std::string (TR ("Aide")) : g_title + " - " + TR ("Aide");
	ui_title ((long) wt.c_str ());
	g_hit = -1;
	g_hoverLink = g_hoverCopy = -1;
	layout (g_vw);
	fill_toc ();
	s_top = top;
	if (!anchor.empty ())
		for (const Head &h : g_heads) if (h.slug == anchor) { s_top = h.y; break; }
	bar_sync ();
	ui_view_dirty (g_view);
}
// une page de plus dans l'historique
static void open_page (const std::string &path, const std::string &anchor = "")
{
	if (g_hpos >= 0) g_hist[(size_t) g_hpos].top = s_top;
	g_hist.resize ((size_t) (g_hpos + 1));
	g_hist.push_back ({ path, 0 });
	g_hpos++;
	show (path, anchor, 0);
	if (!anchor.empty ()) g_hist[(size_t) g_hpos].top = s_top;
	update_buttons ();
}
static void go_back ()
{
	if (g_hpos <= 0) return;
	g_hist[(size_t) g_hpos].top = s_top;
	g_hpos--;
	show (g_hist[(size_t) g_hpos].path, "", g_hist[(size_t) g_hpos].top);
	update_buttons ();
}
static void go_forward ()
{
	if (g_hpos + 1 >= (int) g_hist.size ()) return;
	g_hist[(size_t) g_hpos].top = s_top;
	g_hpos++;
	show (g_hist[(size_t) g_hpos].path, "", g_hist[(size_t) g_hpos].top);
	update_buttons ();
}
static void zoom (int z)
{
	g_zoom = std::max (70, std::min (200, z));
	int anchor = g_docH ? (int) ((long) s_top * 1000 / g_docH) : 0;
	layout (g_vw);
	s_top = (int) ((long) anchor * g_docH / 1000);
	bar_sync ();
	ui_view_dirty (g_view);
}

static void follow (const std::string &target)
{
	if (target.empty ()) return;
	if (target[0] == '#')
	{
		for (const Head &h : g_heads) if (h.slug == target.substr (1)) { scroll_to (h.y); return; }
		return;
	}
	if (target.compare (0, 7, "http://") == 0 || target.compare (0, 8, "https://") == 0)
	{
		spawn ("SD0:/apps/jet.app/main", target.c_str ());
		return;
	}
	if (target.compare (0, 4, "app:") == 0)
	{
		std::string p = "SD0:/apps/" + target.substr (4) + ".app/main";
		spawn (p.c_str (), "");
		return;
	}
	if (target.compare (0, 5, "help:") == 0)
	{
		std::string p = app_help (target.substr (5));
		open_page (p.empty () ? "SD0:/apps/" + target.substr (5) + ".app/help_" + lang () + ".md" : p);	// (absente : « Page introuvable »)
		return;
	}
	std::string path = target, anchor;
	size_t h = path.find ('#');
	if (h != std::string::npos) { anchor = path.substr (h + 1); path.resize (h); }
	if (path.empty ()) { follow ("#" + anchor); return; }
	open_page (localized (resolve (path)), anchor);
}

// ---- la recherche ---------------------------------------------------------------------------------------------------

static bool contains_ci (const std::string &h, const char *n)
{
	size_t ln = strlen (n);
	if (!ln) return false;
	for (size_t i = 0; i + ln <= h.size (); i++)
	{
		size_t k = 0;
		while (k < ln && tolower ((unsigned char) h[i + k]) == tolower ((unsigned char) n[k])) k++;
		if (k == ln) return true;
	}
	return false;
}
static void find_next ()
{
	const char *q = (const char *) ui_w_get_text (g_find);
	if (!q || !*q) return;
	int n = (int) g_tlines.size ();
	for (int k = 1; k <= n; k++)
	{
		int i = (g_hit + k) % n;
		if (i < 0) i += n;
		if (contains_ci (g_tlines[i].text, q))
		{
			g_hit = i;
			const TLine &t = g_tlines[i];
			if (t.y < s_top + 20 || t.y + t.h > s_top + g_vh - 20) s_top = t.y - g_vh / 3;
			bar_sync ();
			ui_view_dirty (g_view);
			return;
		}
	}
	g_hit = -1;
	ui_view_dirty (g_view);
}

// ---- la fenetre -----------------------------------------------------------------------------------------------------

static void place (int w, int h)
{
	g_ww = w; g_wh = h;
	bool toc = g_showToc && w >= 640;
	int sx = toc ? SIDEW : 0;
	ui_w_move (g_tb, 0, 0, w, TOPH);
	int fw = std::min (220, w - CAP - 300);
	ui_w_show (g_find, fw >= 100);
	if (fw >= 100) ui_w_move (g_find, w - CAP - fw, 9, fw, 30);
	ui_w_show (g_toc, toc);
	if (toc) ui_w_move (g_toc, 0, TOPH, SIDEW, h - TOPH);
	g_vw = w - sx; g_vh = h - TOPH;
	ui_w_move (g_view, sx, TOPH, g_vw, g_vh);
	ui_w_move (g_bar, w - SBW, TOPH, SBW, g_vh);
	if (g_vw != g_layW) layout (g_vw);
	bar_sync ();
	ui_view_dirty (g_view);
}
static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	place ((int) w, (int) h);
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();
	return 0;
}
static void toggle_toc ()
{
	g_showToc = !g_showToc;
	on_resize (ui.w, ui.h);
	build_menu ();
}

static void build_menu ()
{
	static char tt[2][1400];			// (deux tampons : UIKit retient la traduction d'un menu par son adresse)
	static int w;
	char *t = tt[w ^= 1];
	snprintf (t, sizeof tt[0],
		"MFichier\n" "I%d\tOuvrir...\tCtrl+O\n" "-\n" "I%d\tFermer\tCtrl+W\n"
		"M\xC3\x89" "dition\n" "I%d\tCopier la page (Markdown)\n" "-\n" "I%d\tRechercher...\tCtrl+F\n" "I%d\tOccurrence suivante\tF3\n"
		"MAller\n" "I%d%s\tPr\xC3\xA9" "c\xC3\xA9" "dent\n" "I%d%s\tSuivant\n" "-\n" "I%d\tSommaire de l'aide\n" "I%d\tGuide du d\xC3\xA9" "veloppeur\n"
		"MPr\xC3\xA9" "sentation\n" "I%d\tTexte plus grand\n" "I%d\tTexte plus petit\n" "I%d\tTaille normale\n" "-\n" "I%d%s\tTable des mati\xC3\xA8" "res\n",
		M_OPEN, M_CLOSE, M_COPYPAGE, M_FIND, M_FINDNEXT,
		M_BACK, g_hpos > 0 ? "" : "d", M_FWD, g_hpos + 1 < (int) g_hist.size () ? "" : "d", M_HOME, M_API,
		M_BIGGER, M_SMALLER, M_NORMAL, M_TOC, g_showToc ? "c" : "");
	ui_menu ((long) t);
}
static bool command (int id)
{
	switch (id)
	{
	case M_OPEN:
	{
		char p[256];
		ui_file_filter ((long) "*.md;*.txt");
		std::string dir = g_dir.empty () ? std::string (HELP_DIR) : g_dir;
		if (ui_file_open ((long) dir.c_str (), (long) p, sizeof p)) open_page (p);
		break;
	}
	case M_CLOSE: return false;
	case M_COPYPAGE: ui_clip_set ((long) g_raw.c_str (), (long) g_raw.size ()); break;
	case M_FIND: ui_w_focus (g_find); break;
	case M_FINDNEXT: find_next (); break;
	case M_BACK: go_back (); break;
	case M_FWD: go_forward (); break;
	case M_HOME: open_page (""); break;
	case M_API: open_page (localized (std::string (HELP_DIR) + "/api/index_" + lang () + ".md")); break;
	case M_BIGGER: zoom (g_zoom + 10); break;
	case M_SMALLER: zoom (g_zoom - 10); break;
	case M_NORMAL: zoom (100); break;
	case M_TOC: toggle_toc (); break;
	}
	return true;
}
static void control (long w, long code)
{
	if (w == g_bar && code == UN_CHANGE) { s_top = (int) ui_w_get_value (w); ui_view_dirty (g_view); }
	else if (w == g_bBack && code == UN_ACTIVATE) go_back ();
	else if (w == g_bFwd && code == UN_ACTIVATE) go_forward ();
	else if (w == g_bHome && code == UN_ACTIVATE) open_page ("");
	else if (w == g_bToc && code == UN_ACTIVATE) toggle_toc ();
	else if (w == g_find && code == UN_ACTIVATE) find_next ();
	else if (w == g_find && code == UN_CHANGE) g_hit = -1;
	else if (w == g_toc && code == UN_CHANGE)
	{
		long row = ui_w_get_value (g_toc), data = 0;
		if (row >= 0)
		{
			uikit_list_get (g_toc, row, &data);
			if (data >= 0 && data < (long) g_heads.size ()) scroll_to (g_heads[(size_t) data].y);
		}
	}
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = (int) scr[0] - 16, wh = (int) (scr[1] - scr[3] - scr[2]) - 24;
	if (ww > 980) ww = 980;
	if (wh > 680) wh = 680;
	long win = ui_window_ex ((scr[0] - ww) / 2, scr[3] + 10, ww, wh, (long) TR ("Aide"), WF_RESIZABLE);
	if (win <= 0) return 1;
	ui_min_size (420, 300);
	ui_on_resize ((long) on_resize, 0);

	g_tb = ui_toolbar (0, 0, ww, TOPH);
	ui_w_style (g_tb, UTBS_NOLINE);
	g_bBack = ui_toolbar_add (g_tb, (long) "\xE2\x86\x90", 0, 0);		// (glyphes de tools/icons)
	ui_w_tip (g_bBack, (long) TR ("Pr\xC3\xA9" "c\xC3\xA9" "dent (Alt+Gauche)"));
	g_bFwd = ui_toolbar_add (g_tb, (long) "\xE2\x86\x92", 0, 0);
	ui_w_tip (g_bFwd, (long) TR ("Suivant (Alt+Droite)"));
	g_bToc = ui_toolbar_add (g_tb, (long) "\xE2\x89\xA1", 0, UTB_SEP);
	ui_w_tip (g_bToc, (long) TR ("Table des mati\xC3\xA8res"));
	g_bHome = ui_pushbutton (0, 0, 0, 0, (long) TR ("Sommaire"), UB_GHOST);
	ui_w_set_parent (g_bHome, g_tb);
	{
		// (apres les trois boutons de la barre)
		long bx = 8 + 3 * 36 + 12;
		ui_w_move (g_bHome, bx, 8, text_w (TR ("Sommaire"), 13, (long) ui.font) + 28, 32);
	}
	g_find = ui_textbox (0, 0, 200, 30, 0);
	ui_textbox_hint (g_find, (long) TR ("Rechercher (Ctrl+F)"));
	ui_w_set_parent (g_find, g_tb);
	g_toc = ui_list (0, 0, SIDEW, 100, UL_SIDEBAR);
	g_view = ui_view (0, 0, 100, 100, UVW_FOCUS);
	ui_view_on (g_view, (long) view_paint, (long) view_mouse, (long) view_key, (long) view_wheel);
	g_bar = ui_scrollbar (0, 0, SBW, 100, 0);

	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	g_vw = (int) ui.w - SIDEW;
	g_vh = (int) ui.h - TOPH;
	std::string first = argc > 1 && argv[1][0] ? localized (argv[1]) : "";
	open_page (first);
	place ((int) ui.w, (int) ui.h);
	ui_w_focus (g_view);
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();

	bool run = true;
	while (run)
	{
		long t = ui_event (200);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) run = command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) run = command ((int) id);
			}
		}
		else ui_w_idle ();
		if (g_pendLink >= 0)
		{
			int l = g_pendLink;
			g_pendLink = -1;
			if (l < (int) g_links.size ()) follow (g_links[(size_t) l]);
		}
		if (g_pendCopy >= 0)
		{
			const md::Block &b = g_blocks[(size_t) g_pendCopy];
			ui_clip_set ((long) b.code.c_str (), (long) b.code.size ());
			g_copied = g_pendCopy;
			g_copiedUntil = now_ms () + 1500;
			g_pendCopy = -1;
			ui_view_dirty (g_view);
		}
		if (g_copied >= 0 && now_ms () >= g_copiedUntil) { g_copied = -1; ui_view_dirty (g_view); }
		ui_view_flush ();
	}
	return 0;
}
