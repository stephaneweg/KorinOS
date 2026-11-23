//
// uikit_gtk.cpp -- UIKit (src/kits/uikit, le kit de l'interface de Korin, en assembleur) refait sur GTK 4 pour Linux :
// les fonctions ui_* dont Koton et Arabesque se servent, avec la meme semantique (src/apps/help/data/api/uikit_fr.md),
// pour que leurs sources se compilent sans changement.
//
//   fenetre      une GtkWindow : la barre de menus (GtkPopoverMenuBar, faite du texte de ui_menu), puis le contenu,
//                un KSurf : les pixels de la fenetre (ui.surf, 0x00RRGGBB, ce que l'app dessine) en fond, et par-dessus
//                les widgets de l'app, places a la position que l'app donne (coordonnees de la fenetre, comme UIKit)
//   dessin       ui_fill, ui_frame, ui_rbox, ui_text... dans la cible courante (la fenetre, une vue, un canevas) :
//                Cairo et Pango sur le tampon de pixels lui-meme
//   widgets      les vrais widgets de GTK 4 : GtkButton, GtkDropDown, GtkEntry, GtkSpinButton, GtkScale, GtkNotebook,
//                GtkListBox, GtkLevelBar... ; une vue (ui_view) est un KSurf a pixels, dessine par l'app
//   evenements   les signaux de GTK vont dans une file ; ui_event la rend un evenement a la fois (ui.ev), ui_w_event
//                dit quel widget a change (UN_*) et appelle les fonctions des vues (dessin, souris, touches, molette)
//   dialogues    GtkAlertDialog, GtkFileDialog, GtkColorDialog, rendus synchrones par une boucle imbriquee ; ceux de
//                l'app (ui_dialog_begin, ui_dialog_button, ui_dialog_run...) : une fenetre modale de plus, comme sous
//                Korin, et la boucle de ui_dialog_run (src/kits/uikit/uk_dialog.c)
//
#include "korin_port.h"
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pango/pangocairo.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <uikit.h>

extern "C" { __thread long uikit__ret[4]; int kport_soundfont_menu; }

namespace {

enum { T_WIDGET = 0x40 };		// (notre type d'evenement : un widget a change ; ui_w_event le rend)

// ---- un tampon de pixels : la fenetre, une vue, un canevas -------------------------------------------------------------
struct Buf
{
	uint32_t *px = 0;
	int w = 0, h = 0;
	bool clipOn = false;
	int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
	cairo_surface_t *cs = 0;
	GdkTexture *tex = 0;
	bool texDirty = true;
	void alloc (int nw, int nh, uint32_t fill)
	{
		if (cs) { cairo_surface_destroy (cs); cs = 0; }
		uint32_t *n = (uint32_t *) calloc ((size_t) (nw > 0 ? nw : 1) * (size_t) (nh > 0 ? nh : 1), 4);
		for (long i = 0; i < (long) nw * nh; i++) n[i] = fill;
		free (px);
		px = n; w = nw; h = nh; clipOn = false; texDirty = true;
	}
	void release ()
	{
		if (cs) cairo_surface_destroy (cs);
		if (tex) g_object_unref (tex);
		free (px); px = 0; cs = 0; tex = 0;
	}
};

struct Win;
enum Kind { K_BUTTON, K_LABEL, K_CHECK, K_RADIO, K_DROPDOWN, K_SEGMENTED, K_TEXTBOX, K_TEXTAREA, K_SLIDER, K_NUMERIC,
	K_KNOB, K_VUMETER, K_LCD, K_LIST, K_PANEL, K_TABS, K_PAGE, K_VIEW, K_CANVAS };

struct Wid
{
	long id;
	Kind kind;
	Win *win;
	GtkWidget *gw = 0;		// (ce qui est place dans la fenetre ; 0 : panneau, page)
	GtkWidget *inner = 0;		// (l'entree, la liste, le texte...)
	GtkWidget *label = 0;		// (le libelle d'un bouton)
	int x = 0, y = 0, w = 0, h = 0;
	bool shown = true, enabled = true;
	long parent = 0;
	long fn = 0, data = 0;
	long flags = 0;
	long value = 0, vmin = 0, vmax = 100;
	std::string text, getbuf;
	std::vector<std::string> items;
	std::vector<long> itemData;
	std::vector<GtkWidget *> parts;	// (les boutons d'un controle segmente, les lignes d'une liste)
	std::vector<std::pair<std::string, int>> cols;
	GtkStringList *model = 0;
	GtkWidget *header = 0;
	// la vue, le canevas
	Buf buf;
	bool dirty = true;
	long paint = 0, mouse = 0, key = 0, wheel = 0;
	// la page d'onglets
	long tabs = 0; int pageNo = 0;
	std::vector<long> pages;
	// la souris d'une vue
	int held = 0, lastPressBtn = 0, clicks = 0; gint64 lastPress = 0; double lastX = -1e9, lastY = 0, startX = 0, startY = 0;
	bool dragIgnored = false;
	int radioGroup = 0;
};

struct MenuItem { long id; std::string label, accel; bool sep, disabled, checked, submenuStart, submenuEnd; int menu; int smods, shid; };

struct Win
{
	long id;
	GtkWidget *gw = 0, *box = 0, *menubar = 0, *surf = 0;
	Buf buf;
	long onResize = 0;
	bool presented = false, modal = false, resizable = true;
	int minw = 0, minh = 0, reqw = 0, reqh = 0;
	int pendW = -1, pendH = -1;
	long prevCurrent = 0;		// (le dialogue : la fenetre courante avant lui)
	// un dialogue de l'app (ui_dialog_button, ui_dialog_run) : ses boutons et leurs reponses, Entree, Echap, la fin
	long dlgBtn[3] = { 0, 0, 0 }, dlgResp[3] = { 0, 0, 0 }, dlgDef = 0, dlgCancel = 0, dlgRes = 0;
	bool dlgDone = false;
	long dlgTick = 0, dlgTickMs = 0;
	std::vector<long> wids;
	// les menus
	std::vector<std::string> menuTitles;
	std::vector<MenuItem> items;
	std::map<long, int> state;	// id -> UMS_*
	GSimpleActionGroup *actions = 0;
	// la souris de la fenetre
	int held = 0; bool dragIgnored = false; double sx = 0, sy = 0;
	std::string title;
};

struct Ev { int type = 0, kind = 0, x = 0, y = 0, code = 0, ch = 0, mods = 0, win = 0; long wid = 0; int un = 0; long info = 0; };

struct ui_vars *g_vars;
std::map<long, Win *> g_wins;
std::map<long, Wid *> g_wids;
long g_nextWin = 1, g_nextWid = 1000;
long g_cur;					// la fenetre courante
Buf *g_target;					// la cible des dessins (0 : la fenetre courante)
Buf *g_targetSaved;
std::deque<Ev> g_q;
Ev g_ev;					// l'evenement rendu par ui_event
int g_quiet;					// (les changements faits par l'app ne font pas d'evenement)
unsigned g_tok[32];
bool g_dark;
std::string g_filter;
std::unordered_map<std::string, std::string> g_tr;
std::vector<std::pair<std::string, std::string>> g_trPort;	// (kport_tr_add : les mots du port, devant ceux des fichiers)
std::string g_dropData; int g_dropKind;
int g_keyMods;					// les modificateurs tenus (UI_MOD_* de gauche)
long g_focusView;

Win *cur () { auto i = g_wins.find (g_cur); return i == g_wins.end () ? 0 : i->second; }
Win *win (long id) { auto i = g_wins.find (id); return i == g_wins.end () ? 0 : i->second; }
Wid *wid (long id) { auto i = g_wids.find (id); return i == g_wids.end () ? 0 : i->second; }
Buf *target () { if (g_target) return g_target; Win *w = cur (); return w ? &w->buf : 0; }
unsigned tok (int tj) { return g_tok[tj / 4] & 0xFFFFFF; }

void use_vars (Win *w)
{
	if (!g_vars) return;
	g_vars->win = w ? w->id : 0;
	g_vars->surf = w ? w->buf.px : 0;
	g_vars->w = w ? w->buf.w : 0;
	g_vars->h = w ? w->buf.h : 0;
}

void push (const Ev &e) { g_q.push_back (e); }

struct Quiet { Quiet () { g_quiet++; } ~Quiet () { g_quiet--; } };

const char *TRs (const char *s)
{
	if (!s) return "";
	auto i = g_tr.find (s);
	return i == g_tr.end () ? s : i->second.c_str ();
}

// un fichier de mots « texte = traduction » (# : commentaire) -> g_tr
bool tr_load_file (const char *fp)
{
	FILE *f = fp ? fopen (fp, "r") : 0;
	if (!f) return false;
	char line[4096];
	while (fgets (line, sizeof line, f))
	{
		line[strcspn (line, "\r\n")] = 0;
		if (!line[0] || line[0] == '#') continue;
		char *eq = strstr (line, " = ");
		if (!eq) continue;
		*eq = 0;
		g_tr[line] = eq + 3;
	}
	fclose (f);
	for (auto &x : g_trPort) g_tr[x.first] = x.second;
	return true;
}

// ---- le theme : les jetons de Givre et d'Onyx Noir (src/kits/uikit/uikit_theme.inc) -------------------------------
const unsigned JET_GIVRE[24] = { 0x00F5F6F8, 0x00FFFFFF, 0x00ECEEF2, 0x00FFFFFF, 0x00FFFFFF, 0x00E2E5EA,
	0x00B8BFC9, 0x0014171C, 0x004C5563, 0x005F6877, 0x002563D8, 0x00E4ECFB,
	0x001D4FB3, 0x002563D8, 0x00D93A3F, 0x001E8A5A, 0x00C4861C, 0x00F7F8FA,
	0x00F5F6F8, 0x00E9EDF2, 0x00F5F6F8, 0xC4000000, 0, 0x00FFFFFF };
const unsigned JET_NOIR[24] = { 0x0015171B, 0x001B1E23, 0x00101216, 0x00262A31, 0x00262A31, 0x002A2E36,
	0x00525966, 0x00ECEEF2, 0x00B0B6C0, 0x008B929D, 0x002F72DC, 0x001C2F4D,
	0x002560C0, 0x007DB4F2, 0x00D93A3F, 0x0046A758, 0x00F1B33B, 0x0015171B,
	0x001B1E23, 0x001F2228, 0x0015171B, 0x40000000, 1, 0x00262A31 };

void pick_theme ()
{
	const char *t = getenv ("KORIN_THEME");
	bool dark = false;
	if (t && *t) dark = !strncasecmp (t, "noir", 4) || !strncasecmp (t, "dark", 4) || !strncasecmp (t, "onyx", 4);
	else
	{
		GSettingsSchemaSource *src = g_settings_schema_source_get_default ();
		GSettingsSchema *sch = src ? g_settings_schema_source_lookup (src, "org.gnome.desktop.interface", TRUE) : 0;
		if (sch)
		{
			if (g_settings_schema_has_key (sch, "color-scheme"))
			{
				GSettings *s = g_settings_new ("org.gnome.desktop.interface");
				char *v = g_settings_get_string (s, "color-scheme");
				dark = v && !strcmp (v, "prefer-dark");
				g_free (v); g_object_unref (s);
			}
			g_settings_schema_unref (sch);
		}
	}
	g_dark = dark;
	const unsigned *j = dark ? JET_NOIR : JET_GIVRE;
	for (int i = 0; i < 24; i++) g_tok[i] = j[i];
	g_tok[TJ_ACCENT_INK / 4] = 0x00FFFFFF;
	g_tok[TJ_HOVER / 4] = dark ? 0x00262A31 : 0x00ECEEF2;
	g_tok[TJ_AXIS_X / 4] = 0x00E0524D; g_tok[TJ_AXIS_Y / 4] = 0x003FB950; g_tok[TJ_AXIS_Z / 4] = 0x002F72DC;
	// (les widgets de GTK suivent : le schema de couleurs du systeme, ou celui demande par KORIN_THEME)
	if (t && *t)
	{
		GtkSettings *gs = gtk_settings_get_default ();
		GParamSpec *ps = g_object_class_find_property (G_OBJECT_GET_CLASS (gs), "gtk-interface-color-scheme");
		if (ps && G_IS_PARAM_SPEC_ENUM (ps))
		{
			GEnumClass *ec = G_PARAM_SPEC_ENUM (ps)->enum_class;
			GEnumValue *v = g_enum_get_value_by_nick (ec, dark ? "dark" : "light");
			if (v) g_object_set (gs, "gtk-interface-color-scheme", v->value, NULL);
		}
		else g_object_set (gs, "gtk-application-prefer-dark-theme", dark, NULL);
	}
}

const char *CSS =
	".kport { font-family: \"Inter\", \"Adwaita Sans\", \"Cantarell\", sans-serif; font-size: 13px; }\n"
	".kport button { min-height: 0; min-width: 0; padding: 1px 6px; }\n"
	".kport entry { min-height: 0; padding-left: 6px; padding-right: 6px; }\n"
	".kport spinbutton { min-height: 0; }\n"
	".kport spinbutton > text { min-width: 0; }\n"
	".kport spinbutton > button { min-height: 0; min-width: 0; padding: 0 5px; }\n"
	".kport dropdown > button { min-height: 0; padding: 0 6px; }\n"
	".kport checkbutton { min-height: 0; padding: 0; }\n"
	".kport check, .kport radio { margin: 0 5px 0 2px; }\n"
	".kport scale { min-height: 0; padding: 0 6px; }\n"
	".kport notebook > header tab { min-height: 0; padding: 3px 5px; }\n"
	".kport list row { min-height: 0; padding: 0; }\n"
	".kport levelbar block { min-width: 1px; min-height: 1px; }\n"
	".kport .k-lcd { font-family: monospace; font-weight: bold; font-size: 17px; background-color: #0e1116; color: #7fe08f;"
	" border-radius: 4px; padding: 0 8px; }\n"
	".kport .k-title { font-weight: bold; font-size: 17px; }\n"
	".kport .k-bold { font-weight: 600; }\n"
	".kport .k-menu button { padding: 3px 12px; }\n";

// ---- le texte : Pango ----------------------------------------------------------------------------------------------------
enum { FONT_MAGIC = 0x544E464B };		// « KFNT » : en tete des blocs de police (ui.font, ui.font_b, ui.font_m...)
unsigned char g_fonts[10][UI_FONT_SIZE];
PangoContext *g_pctx;
PangoLayout *g_lay;
std::map<long, PangoFontDescription *> g_fdesc;

void font_block (unsigned char *b, int id) { memset (b, 0, UI_FONT_SIZE); *(int *) b = FONT_MAGIC; b[4] = (unsigned char) id; }
int font_id (long f)
{
	const unsigned char *b = (const unsigned char *) f;
	if (!b || *(const int *) b != FONT_MAGIC) return 0;
	return b[4];
}
PangoFontDescription *font_desc (int id, int px)
{
	long key = (long) id << 16 | (px & 0xFFFF);
	auto i = g_fdesc.find (key);
	if (i != g_fdesc.end ()) return i->second;
	static const char *const SANS = "Inter, Adwaita Sans, Cantarell, DejaVu Sans, sans-serif";
	static const char *const MONO = "DejaVu Sans Mono, Adwaita Mono, Liberation Mono, monospace";
	static const char *const SERIF = "Liberation Serif, DejaVu Serif, serif";
	PangoFontDescription *d = pango_font_description_new ();
	const char *fam = SANS;
	PangoWeight wt = PANGO_WEIGHT_NORMAL;
	PangoStyle st = PANGO_STYLE_NORMAL;
	switch (id)
	{
	case UFN_BOLD: wt = PANGO_WEIGHT_SEMIBOLD; break;
	case UFN_MONO: fam = MONO; break;
	case UFN_ITALIC: st = PANGO_STYLE_ITALIC; break;
	case UFN_BOLDITALIC: wt = PANGO_WEIGHT_SEMIBOLD; st = PANGO_STYLE_ITALIC; break;
	case UFN_MONOBOLD: fam = MONO; wt = PANGO_WEIGHT_BOLD; break;
	case UFN_SERIF: fam = SERIF; break;
	case UFN_SERIFBOLD: fam = SERIF; wt = PANGO_WEIGHT_BOLD; break;
	case UFN_SERIFITALIC: fam = SERIF; st = PANGO_STYLE_ITALIC; break;
	case UFN_SERIFBOLDITALIC: fam = SERIF; wt = PANGO_WEIGHT_BOLD; st = PANGO_STYLE_ITALIC; break;
	}
	pango_font_description_set_family (d, fam);
	pango_font_description_set_weight (d, wt);
	pango_font_description_set_style (d, st);
	pango_font_description_set_absolute_size (d, (px > 0 ? px : 13) * PANGO_SCALE);
	g_fdesc[key] = d;
	return d;
}
void text_init ()
{
	g_pctx = pango_font_map_create_context (pango_cairo_font_map_get_default ());
	cairo_font_options_t *o = cairo_font_options_create ();
	cairo_font_options_set_antialias (o, CAIRO_ANTIALIAS_GRAY);
	cairo_font_options_set_hint_style (o, CAIRO_HINT_STYLE_SLIGHT);
	cairo_font_options_set_hint_metrics (o, CAIRO_HINT_METRICS_OFF);
	pango_cairo_context_set_font_options (g_pctx, o);
	cairo_font_options_destroy (o);
	g_lay = pango_layout_new (g_pctx);
	pango_layout_set_single_paragraph_mode (g_lay, TRUE);
}
// le texte dans la mise en page -> sa largeur, sa ligne de base (pixels)
int lay_text (const char *s, int px, long font, int ellipsW, int *baseline)
{
	pango_layout_set_font_description (g_lay, font_desc (font_id (font), px));
	PangoTabArray *tabs = pango_tab_array_new_with_positions (1, TRUE, PANGO_TAB_LEFT, (px > 0 ? px : 13) * 2);
	pango_layout_set_tabs (g_lay, tabs);
	pango_tab_array_free (tabs);
	if (ellipsW > 0) { pango_layout_set_width (g_lay, ellipsW * PANGO_SCALE); pango_layout_set_ellipsize (g_lay, PANGO_ELLIPSIZE_END); }
	else { pango_layout_set_width (g_lay, -1); pango_layout_set_ellipsize (g_lay, PANGO_ELLIPSIZE_NONE); }
	if (s && !g_utf8_validate (s, -1, 0))
	{
		// (un texte coupe au milieu d'un caractere : UIKit le dessine quand meme)
		char *v = g_utf8_make_valid (s, -1);
		pango_layout_set_text (g_lay, v, -1);
		g_free (v);
	}
	else pango_layout_set_text (g_lay, s ? s : "", -1);
	PangoRectangle lg;
	pango_layout_get_extents (g_lay, 0, &lg);
	if (baseline) *baseline = PANGO_PIXELS (pango_layout_get_baseline (g_lay));
	return PANGO_PIXELS (lg.width);
}

// ---- Cairo sur un tampon -------------------------------------------------------------------------------------------------
cairo_t *cr_begin (Buf *b)
{
	if (!b || !b->px || b->w <= 0 || b->h <= 0) return 0;
	if (!b->cs) b->cs = cairo_image_surface_create_for_data ((unsigned char *) b->px, CAIRO_FORMAT_RGB24, b->w, b->h, b->w * 4);
	cairo_surface_mark_dirty (b->cs);
	cairo_t *cr = cairo_create (b->cs);
	if (b->clipOn) { cairo_rectangle (cr, b->cx0, b->cy0, b->cx1 - b->cx0, b->cy1 - b->cy0); cairo_clip (cr); }
	return cr;
}
void cr_end (Buf *b, cairo_t *cr) { cairo_destroy (cr); cairo_surface_flush (b->cs); b->texDirty = true; }
void cr_color (cairo_t *cr, unsigned c) { cairo_set_source_rgb (cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0); }
GdkRGBA rgba (unsigned c) { GdkRGBA r = { ((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, 1 }; return r; }

void fill_rect (Buf *b, long x, long y, long w, long h, unsigned c)
{
	if (!b || !b->px) return;
	long x0 = x, y0 = y, x1 = x + w, y1 = y + h;
	long bx0 = 0, by0 = 0, bx1 = b->w, by1 = b->h;
	if (b->clipOn) { bx0 = b->cx0 > 0 ? b->cx0 : 0; by0 = b->cy0 > 0 ? b->cy0 : 0; bx1 = b->cx1 < b->w ? b->cx1 : b->w; by1 = b->cy1 < b->h ? b->cy1 : b->h; }
	if (x0 < bx0) x0 = bx0;
	if (y0 < by0) y0 = by0;
	if (x1 > bx1) x1 = bx1;
	if (y1 > by1) y1 = by1;
	if (x1 <= x0 || y1 <= y0) return;
	c &= 0xFFFFFF;
	for (long yy = y0; yy < y1; yy++)
	{
		uint32_t *p = b->px + yy * b->w + x0;
		for (long xx = x0; xx < x1; xx++) *p++ = c;
	}
	b->texDirty = true;
}

// ---- le KSurf : des pixels en fond, des widgets places par-dessus ------------------------------------------------------
struct KSurf { GtkWidget parent_instance; Buf *buf; Win *win; Wid *view; };
struct KSurfClass { GtkWidgetClass parent_class; };
G_DEFINE_TYPE (KSurf, ksurf, GTK_TYPE_WIDGET)

void ksurf_init (KSurf *s) { s->buf = 0; s->win = 0; s->view = 0; }
void ksurf_dispose (GObject *o)
{
	GtkWidget *c;
	while ((c = gtk_widget_get_first_child (GTK_WIDGET (o)))) gtk_widget_unparent (c);
	G_OBJECT_CLASS (ksurf_parent_class)->dispose (o);
}
void ksurf_measure (GtkWidget *w, GtkOrientation o, int, int *mn, int *nat, int *mb, int *nb)
{
	KSurf *s = (KSurf *) w;
	int m = 0, n = 0;
	if (s->win) { m = o == GTK_ORIENTATION_HORIZONTAL ? s->win->minw : s->win->minh; n = o == GTK_ORIENTATION_HORIZONTAL ? s->win->reqw : s->win->reqh; }
	if (n < m) n = m;
	*mn = m; *nat = n; *mb = *nb = -1;
}
void ksurf_allocate (GtkWidget *w, int width, int height, int)
{
	KSurf *s = (KSurf *) w;
	for (GtkWidget *c = gtk_widget_get_first_child (w); c; c = gtk_widget_get_next_sibling (c))
	{
		if (GTK_IS_POPOVER (c)) { gtk_popover_present (GTK_POPOVER (c)); continue; }
		if (!gtk_widget_get_visible (c)) continue;
		Wid *d = (Wid *) g_object_get_data (G_OBJECT (c), "kw");
		int x = d ? d->x : 0, y = d ? d->y : 0, cw = d ? d->w : 0, ch = d ? d->h : 0;
		int mn, nat;
		if (cw <= 0) { gtk_widget_measure (c, GTK_ORIENTATION_HORIZONTAL, -1, &mn, &nat, 0, 0); cw = nat; }
		if (ch <= 0) { gtk_widget_measure (c, GTK_ORIENTATION_VERTICAL, -1, &mn, &nat, 0, 0); ch = nat; }
		// (UIKit : la place donnee, meme sous la taille minimale de GTK ; ce qui depasse est coupe)
		gtk_widget_measure (c, GTK_ORIENTATION_HORIZONTAL, -1, &mn, &nat, 0, 0);
		gtk_widget_measure (c, GTK_ORIENTATION_VERTICAL, -1, &mn, &nat, 0, 0);
		graphene_point_t pt; graphene_point_init (&pt, (float) x, (float) y);
		gtk_widget_allocate (c, cw, ch, -1, gsk_transform_translate (0, &pt));
	}
	if (s->win)
	{
		Win *wn = s->win;
		int tw = wn->pendW >= 0 ? wn->pendW : wn->buf.w, th = wn->pendW >= 0 ? wn->pendH : wn->buf.h;
		if ((width != tw || height != th) && width > 0 && height > 0)
		{
			wn->pendW = width; wn->pendH = height;
			Ev e; e.type = WEV_RESIZE; e.win = (int) wn->id; e.x = width; e.y = height;
			push (e);
		}
	}
}
void ksurf_snapshot (GtkWidget *w, GtkSnapshot *sn)
{
	KSurf *s = (KSurf *) w;
	int W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
	graphene_rect_t r;
	if (s->win)
	{
		GdkRGBA bg = rgba (tok (TJ_FACE));
		graphene_rect_init (&r, 0, 0, (float) W, (float) H);
		gtk_snapshot_append_color (sn, &bg, &r);
	}
	Buf *b = s->buf;
	if (b && b->px && b->w > 0 && b->h > 0)
	{
		if (b->texDirty || !b->tex)
		{
			if (b->tex) g_object_unref (b->tex);
			GBytes *by = g_bytes_new (b->px, (size_t) b->w * (size_t) b->h * 4);
			b->tex = gdk_memory_texture_new (b->w, b->h, GDK_MEMORY_B8G8R8X8, by, (size_t) b->w * 4);
			g_bytes_unref (by);
			b->texDirty = false;
		}
		graphene_rect_init (&r, 0, 0, (float) b->w, (float) b->h);
		gtk_snapshot_push_clip (sn, &r);
		gtk_snapshot_append_scaled_texture (sn, b->tex, GSK_SCALING_FILTER_NEAREST, &r);
		gtk_snapshot_pop (sn);
	}
	for (GtkWidget *c = gtk_widget_get_first_child (w); c; c = gtk_widget_get_next_sibling (c))
		if (!GTK_IS_NATIVE (c)) gtk_widget_snapshot_child (w, c, sn);
}
void ksurf_class_init (KSurfClass *k)
{
	G_OBJECT_CLASS (k)->dispose = ksurf_dispose;
	GtkWidgetClass *wc = GTK_WIDGET_CLASS (k);
	wc->measure = ksurf_measure;
	wc->size_allocate = ksurf_allocate;
	wc->snapshot = ksurf_snapshot;
}
void redraw (Buf *b, GtkWidget *w) { if (b) b->texDirty = true; if (w) gtk_widget_queue_draw (w); }

// ---- le clavier : GDK -> codes HID -------------------------------------------------------------------------------------
int hid_of (guint kv, guint hw)
{
	guint l = gdk_keyval_to_lower (kv);
	if (l >= 'a' && l <= 'z') return 0x04 + (int) (l - 'a');
	if (l >= '1' && l <= '9') return 0x1E + (int) (l - '1');
	switch (l)
	{
	case '0': return 0x27;
	case GDK_KEY_Return: return 0x28;
	case GDK_KEY_KP_Enter: return 0x58;
	case GDK_KEY_Escape: return 0x29;
	case GDK_KEY_BackSpace: return 0x2A;
	case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab: return 0x2B;
	case GDK_KEY_space: return 0x2C;
	case GDK_KEY_minus: return 0x2D;
	case GDK_KEY_equal: case GDK_KEY_plus: return 0x2E;
	case GDK_KEY_bracketleft: return 0x2F;
	case GDK_KEY_bracketright: return 0x30;
	case GDK_KEY_backslash: return 0x31;
	case GDK_KEY_semicolon: return 0x33;
	case GDK_KEY_apostrophe: return 0x34;
	case GDK_KEY_grave: return 0x35;
	case GDK_KEY_comma: return 0x36;
	case GDK_KEY_period: return 0x37;
	case GDK_KEY_slash: return 0x38;
	case GDK_KEY_Caps_Lock: return 0x39;
	case GDK_KEY_Print: return 0x46;
	case GDK_KEY_Scroll_Lock: return 0x47;
	case GDK_KEY_Pause: return 0x48;
	case GDK_KEY_Insert: case GDK_KEY_KP_Insert: return 0x49;
	case GDK_KEY_Home: case GDK_KEY_KP_Home: return 0x4A;
	case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: return 0x4B;
	case GDK_KEY_Delete: case GDK_KEY_KP_Delete: return 0x4C;
	case GDK_KEY_End: case GDK_KEY_KP_End: return 0x4D;
	case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: return 0x4E;
	case GDK_KEY_Right: case GDK_KEY_KP_Right: return 0x4F;
	case GDK_KEY_Left: case GDK_KEY_KP_Left: return 0x50;
	case GDK_KEY_Down: case GDK_KEY_KP_Down: return 0x51;
	case GDK_KEY_Up: case GDK_KEY_KP_Up: return 0x52;
	case GDK_KEY_KP_Divide: return 0x54;
	case GDK_KEY_KP_Multiply: return 0x55;
	case GDK_KEY_KP_Subtract: return 0x56;
	case GDK_KEY_KP_Add: return 0x57;
	case GDK_KEY_KP_0: return 0x62;
	case GDK_KEY_KP_Decimal: return 0x63;
	case GDK_KEY_Control_L: return 0xE0;
	case GDK_KEY_Shift_L: return 0xE1;
	case GDK_KEY_Alt_L: return 0xE2;
	case GDK_KEY_Super_L: return 0xE3;
	case GDK_KEY_Control_R: return 0xE4;
	case GDK_KEY_Shift_R: return 0xE5;
	case GDK_KEY_Alt_R: case GDK_KEY_ISO_Level3_Shift: return 0xE6;
	case GDK_KEY_Super_R: return 0xE7;
	}
	if (l >= GDK_KEY_F1 && l <= GDK_KEY_F12) return 0x3A + (int) (l - GDK_KEY_F1);
	if (l >= GDK_KEY_KP_1 && l <= GDK_KEY_KP_9) return 0x59 + (int) (l - GDK_KEY_KP_1);
	// (le reste, par la touche : la rangee des chiffres d'un clavier AZERTY, evdev + 8)
	if (hw >= 10 && hw <= 19) return 0x1E + (int) (hw - 10);
	if (hw == 20) return 0x2D;
	if (hw == 21) return 0x2E;
	return 0;
}
int mods_of (GdkModifierType st)
{
	int m = 0;
	if (st & GDK_CONTROL_MASK) m |= 0x01;
	if (st & GDK_SHIFT_MASK) m |= 0x02;
	if (st & GDK_ALT_MASK) m |= 0x04;
	return m;
}
int mod_bit (guint kv)
{
	switch (kv)
	{
	case GDK_KEY_Control_L: case GDK_KEY_Control_R: return 0x01;
	case GDK_KEY_Shift_L: case GDK_KEY_Shift_R: return 0x02;
	case GDK_KEY_Alt_L: case GDK_KEY_Alt_R: return 0x04;
	}
	return 0;
}

bool is_editable (GtkWidget *f)
{
	for (GtkWidget *w = f; w; w = gtk_widget_get_parent (w))
		if (GTK_IS_TEXT (w) || GTK_IS_TEXT_VIEW (w)) return true;
	return false;
}
bool in_popover (GtkWidget *f)
{
	for (GtkWidget *w = f; w; w = gtk_widget_get_parent (w)) if (GTK_IS_POPOVER (w)) return true;
	return false;
}
bool in_list (GtkWidget *f)
{
	for (GtkWidget *w = f; w; w = gtk_widget_get_parent (w)) if (GTK_IS_LIST_BOX (w)) return true;
	return false;
}

gboolean on_key (GtkEventControllerKey *c, guint kv, guint hw, GdkModifierType st, gpointer data, bool press)
{
	Win *w = (Win *) data;
	GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (w->gw));
	int mb = mod_bit (kv);
	if (mb) { if (press) g_keyMods |= mb; else g_keyMods &= ~mb; }
	int mods = mods_of (st);
	if (mb) mods = press ? (mods | mb) : (mods & ~mb);
	int hid = hid_of (kv, hw);
	if (focus && in_popover (focus)) return FALSE;
	if (focus && is_editable (focus))
	{
		// (un champ : les touches d'edition sont a lui ; Echap, les touches de fonction et les raccourcis a l'app)
		bool app = kv == GDK_KEY_Escape || (hid >= 0x3A && hid <= 0x45) || (mods & 0x04);
		if ((mods & 0x01) && hid >= 0x04 && hid <= 0x1D)
		{
			char l = (char) ('a' + hid - 0x04);
			app = !strchr ("acvxzy", l);
		}
		if (!app) return FALSE;
	}
	if (focus && in_list (focus) && (hid == 0x51 || hid == 0x52 || hid == 0x4B || hid == 0x4E || hid == 0x28)) return FALSE;
	(void) c;
	Ev e;
	e.type = WEV_KEY; e.kind = press ? 1 : 0; e.code = hid; e.mods = mods; e.win = (int) w->id;
	gunichar u = gdk_keyval_to_unicode (kv);
	e.ch = (int) u;
	if (kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter) e.ch = 13;
	// (une vue qui a le clavier : la touche est d'abord a elle)
	for (auto &p : g_wids)
	{
		Wid *v = p.second;
		if (v->kind == K_VIEW && v->win == w && v->gw && focus == v->gw && v->key) { e.wid = v->id; break; }
	}
	push (e);
	return TRUE;
}
gboolean on_key_press (GtkEventControllerKey *c, guint kv, guint hw, GdkModifierType st, gpointer d) { return on_key (c, kv, hw, st, d, true); }
void on_key_release (GtkEventControllerKey *c, guint kv, guint hw, GdkModifierType st, gpointer d) { on_key (c, kv, hw, st, d, false); }

// ---- la souris : la fenetre (fond) et les vues ---------------------------------------------------------------------------
int btn_bit (guint b) { return b == 1 ? 1 : b == 3 ? 2 : b == 2 ? 4 : 0; }

struct MouseTarget { Win *win; Wid *view; };
void mouse_ev (KSurf *s, int kind, double x, double y, int held, int mods, int clicks)
{
	Ev e;
	e.type = WEV_MOUSE; e.kind = kind; e.mods = mods;
	if (s->view)
	{
		Wid *v = s->view;
		e.win = (int) v->win->id; e.wid = v->id; e.x = (int) floor (x); e.y = (int) floor (y); e.code = held;
		e.info = (long) clicks | (long) held << 8 | (long) mods << 16;
	}
	else
	{
		e.win = (int) s->win->id; e.x = (int) floor (x); e.y = (int) floor (y); e.code = held;
	}
	push (e);
}
int *held_of (KSurf *s) { return s->view ? &s->view->held : &s->win->held; }
bool *ignored_of (KSurf *s) { return s->view ? &s->view->dragIgnored : &s->win->dragIgnored; }

void on_drag_begin (GtkGestureDrag *g, double x, double y, gpointer data)
{
	KSurf *s = (KSurf *) data;
	GtkWidget *w = GTK_WIDGET (s);
	GtkWidget *p = gtk_widget_pick (w, x, y, GTK_PICK_DEFAULT);
	if (p != w) { *ignored_of (s) = true; gtk_gesture_set_state (GTK_GESTURE (g), GTK_EVENT_SEQUENCE_DENIED); return; }
	*ignored_of (s) = false;
	gtk_gesture_set_state (GTK_GESTURE (g), GTK_EVENT_SEQUENCE_CLAIMED);
	guint b = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (g));
	int mods = mods_of (gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (g)));
	int *held = held_of (s);
	*held |= btn_bit (b);
	int clicks = 1;
	if (s->view)
	{
		Wid *v = s->view;
		gint64 now = g_get_monotonic_time ();
		if (now - v->lastPress < 400000 && fabs (x - v->lastX) < 5 && fabs (y - v->lastY) < 5 && v->lastPressBtn == (int) b) clicks = v->clicks + 1;
		v->clicks = clicks; v->lastPress = now; v->lastX = x; v->lastY = y; v->lastPressBtn = (int) b;
		v->startX = x; v->startY = y;
		if (v->flags & UVW_FOCUS) { gtk_widget_grab_focus (w); g_focusView = v->id; }
	}
	else
	{
		s->win->sx = x; s->win->sy = y;
		if (gtk_widget_get_focusable (w)) gtk_widget_grab_focus (w);
	}
	mouse_ev (s, 1, x, y, *held, mods, clicks);
}
void on_drag_update (GtkGestureDrag *g, double dx, double dy, gpointer data)
{
	KSurf *s = (KSurf *) data;
	if (*ignored_of (s)) return;
	double sx = s->view ? s->view->startX : s->win->sx, sy = s->view ? s->view->startY : s->win->sy;
	int mods = mods_of (gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (g)));
	mouse_ev (s, 0, sx + dx, sy + dy, *held_of (s), mods, 0);
}
void on_drag_end (GtkGestureDrag *g, double dx, double dy, gpointer data)
{
	KSurf *s = (KSurf *) data;
	if (*ignored_of (s)) { *ignored_of (s) = false; return; }
	double sx = s->view ? s->view->startX : s->win->sx, sy = s->view ? s->view->startY : s->win->sy;
	guint b = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (g));
	int mods = mods_of (gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (g)));
	int *held = held_of (s);
	*held &= ~btn_bit (b);
	mouse_ev (s, 2, sx + dx, sy + dy, *held, mods, 0);
}
void on_motion (GtkEventControllerMotion *m, double x, double y, gpointer data)
{
	KSurf *s = (KSurf *) data;
	if (*held_of (s)) return;			// (un glisser : drag-update le donne)
	GtkWidget *w = GTK_WIDGET (s);
	if (!s->view && gtk_widget_pick (w, x, y, GTK_PICK_DEFAULT) != w) return;
	int mods = mods_of (gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (m)));
	mouse_ev (s, 0, x, y, 0, mods, 0);
}
void on_leave (GtkEventControllerMotion *, gpointer data)
{
	KSurf *s = (KSurf *) data;
	if (s->view && !s->view->held) mouse_ev (s, 3, -1, -1, 0, 0, 0);
}
gboolean on_scroll (GtkEventControllerScroll *c, double dx, double dy, gpointer data)
{
	(void) dx;
	KSurf *s = (KSurf *) data;
	int n = dy < 0 ? (int) ceil (-dy) : -(int) ceil (dy);
	if (!n) return FALSE;
	Ev e;
	e.type = WEV_WHEEL; e.y = n; e.mods = mods_of (gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (c)));
	if (s->view)
	{
		e.win = (int) s->view->win->id; e.wid = s->view->id;
		double px = 0, py = 0;
		e.x = (int) s->view->lastX;
		(void) px; (void) py;
	}
	else e.win = (int) s->win->id;
	e.code = n;
	push (e);
	return TRUE;
}
void on_motion_track (GtkEventControllerMotion *, double x, double y, gpointer data)
{
	KSurf *s = (KSurf *) data;
	if (s->view) { s->view->lastX = x; s->view->lastY = y; }
}

GtkWidget *ksurf_new (Win *w, Wid *v)
{
	KSurf *s = (KSurf *) g_object_new (ksurf_get_type (), NULL);
	s->win = v ? 0 : w; s->view = v; s->buf = v ? &v->buf : &w->buf;
	GtkWidget *g = GTK_WIDGET (s);
	gtk_widget_set_overflow (g, GTK_OVERFLOW_HIDDEN);
	GtkGesture *d = gtk_gesture_drag_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (d), 0);
	g_signal_connect (d, "drag-begin", G_CALLBACK (on_drag_begin), s);
	g_signal_connect (d, "drag-update", G_CALLBACK (on_drag_update), s);
	g_signal_connect (d, "drag-end", G_CALLBACK (on_drag_end), s);
	gtk_widget_add_controller (g, GTK_EVENT_CONTROLLER (d));
	GtkEventController *m = gtk_event_controller_motion_new ();
	g_signal_connect (m, "motion", G_CALLBACK (on_motion), s);
	g_signal_connect (m, "leave", G_CALLBACK (on_leave), s);
	gtk_widget_add_controller (g, m);
	GtkEventController *mt = gtk_event_controller_motion_new ();
	g_signal_connect (mt, "motion", G_CALLBACK (on_motion_track), s);
	gtk_widget_add_controller (g, mt);
	GtkEventController *sc = gtk_event_controller_scroll_new ((GtkEventControllerScrollFlags) (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE));
	g_signal_connect (sc, "scroll", G_CALLBACK (on_scroll), s);
	gtk_widget_add_controller (g, sc);
	return g;
}

// ---- les fenetres ----------------------------------------------------------------------------------------------------------
gboolean on_close_request (GtkWindow *, gpointer data)
{
	Win *w = (Win *) data;
	Ev e; e.type = WEV_CLOSE; e.win = (int) w->id;
	push (e);
	return TRUE;
}

gboolean on_drop (GtkDropTarget *, const GValue *v, double x, double y, gpointer data)
{
	Win *w = (Win *) data;
	g_dropData.clear ();
	if (G_VALUE_HOLDS (v, GDK_TYPE_FILE_LIST))
	{
		for (GSList *l = (GSList *) g_value_get_boxed (v); l; l = l->next)
		{
			char *p = g_file_get_path (G_FILE (l->data));
			if (p) { if (!g_dropData.empty ()) g_dropData += "\n"; g_dropData += p; g_free (p); }
		}
		g_dropKind = UDT_FILES;
	}
	else if (G_VALUE_HOLDS (v, G_TYPE_STRING)) { g_dropData = g_value_get_string (v) ? g_value_get_string (v) : ""; g_dropKind = UDT_TEXT; }
	else return FALSE;
	Ev e; e.type = WEV_DROP; e.win = (int) w->id; e.x = (int) x; e.y = (int) y;
	push (e);
	return TRUE;
}

void soundfont_menu_cb (GObject *src, GAsyncResult *res, gpointer)
{
	GFile *f = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (src), res, 0);
	if (!f) return;
	char *p = g_file_get_path (f);
	g_object_unref (f);
	if (!p) return;
	bool ok = kport_audio_reload_soundfont (p) && kport_soundfont_save (p);
	GtkAlertDialog *a = gtk_alert_dialog_new ("%s", ok ? "SoundFont chargée." : "Cette SoundFont ne se lit pas.");
	gtk_alert_dialog_set_detail (a, p);
	Win *w = cur ();
	gtk_alert_dialog_show (a, w ? GTK_WINDOW (w->gw) : 0);
	g_object_unref (a);
	g_free (p);
}
void soundfont_menu (Win *w)
{
	GtkFileDialog *d = gtk_file_dialog_new ();
	gtk_file_dialog_set_title (d, "Choisir la SoundFont");
	GListStore *fl = g_list_store_new (GTK_TYPE_FILE_FILTER);
	GtkFileFilter *f = gtk_file_filter_new ();
	gtk_file_filter_set_name (f, "SoundFonts (*.sf2)");
	gtk_file_filter_add_suffix (f, "sf2");
	g_list_store_append (fl, f);
	gtk_file_dialog_set_filters (d, G_LIST_MODEL (fl));
	gtk_file_dialog_set_default_filter (d, f);
	g_object_unref (f); g_object_unref (fl);
	const char *sf = kport_soundfont ();
	char dir[PATH_MAX];
	snprintf (dir, sizeof dir, "%s", sf && *sf ? sf : "/usr/share/soundfonts/x");
	char *sl = strrchr (dir, '/'); if (sl) *sl = 0;
	GFile *gd = g_file_new_for_path (dir);
	gtk_file_dialog_set_initial_folder (d, gd);
	g_object_unref (gd);
	gtk_file_dialog_open (d, GTK_WINDOW (w->gw), 0, soundfont_menu_cb, 0);
	g_object_unref (d);
}

void on_action (GSimpleAction *a, GVariant *, gpointer data)
{
	Win *w = (Win *) data;
	const char *n = g_action_get_name (G_ACTION (a));
	long id = atol (n + 1);
	if (n[0] == 'x') { if (id == 1) soundfont_menu (w); return; }
	Ev e; e.type = WEV_MENU; e.code = (int) id; e.win = (int) w->id;
	push (e);
}

Win *win_new (long x, long y, long w, long h, const char *title, long flags, bool dialog)
{
	(void) x; (void) y;
	Win *n = new Win;
	n->id = g_nextWin++;
	n->reqw = (int) w; n->reqh = (int) h;
	n->resizable = (flags & WF_RESIZABLE) != 0;
	n->modal = dialog || (flags & WF_MODAL);
	n->title = title ? title : "";
	n->buf.alloc ((int) w, (int) h, tok (TJ_FACE));
	n->gw = gtk_window_new ();
	gtk_widget_add_css_class (n->gw, "kport");
	gtk_window_set_title (GTK_WINDOW (n->gw), n->title.c_str ());
	gtk_window_set_resizable (GTK_WINDOW (n->gw), n->resizable);
	n->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child (GTK_WINDOW (n->gw), n->box);
	n->surf = ksurf_new (n, 0);
	gtk_widget_set_focusable (n->surf, TRUE);
	gtk_widget_set_hexpand (n->surf, TRUE);
	gtk_widget_set_vexpand (n->surf, TRUE);
	gtk_box_append (GTK_BOX (n->box), n->surf);
	// (pas de taille par defaut : la fenetre prend la taille naturelle de son contenu, w x h sous la barre de menus et
	// le titre ; gtk_window_set_default_size compterait le titre dedans)
	if (n->modal)
	{
		Win *p = cur ();
		if (p) gtk_window_set_transient_for (GTK_WINDOW (n->gw), GTK_WINDOW (p->gw));
		gtk_window_set_modal (GTK_WINDOW (n->gw), TRUE);
	}
	else if (cur ())
	{
		// (une fenetre de plus d'Arabesque : l'accord, les reglages de l'image) devant la principale
		gtk_window_set_transient_for (GTK_WINDOW (n->gw), GTK_WINDOW (cur ()->gw));
	}
	g_signal_connect (n->gw, "close-request", G_CALLBACK (on_close_request), n);
	GtkEventController *k = gtk_event_controller_key_new ();
	gtk_event_controller_set_propagation_phase (k, GTK_PHASE_CAPTURE);
	g_signal_connect (k, "key-pressed", G_CALLBACK (on_key_press), n);
	g_signal_connect (k, "key-released", G_CALLBACK (on_key_release), n);
	gtk_widget_add_controller (n->gw, k);
	GtkDropTarget *dt = gtk_drop_target_new (G_TYPE_INVALID, GDK_ACTION_COPY);
	GType types[2] = { GDK_TYPE_FILE_LIST, G_TYPE_STRING };
	gtk_drop_target_set_gtypes (dt, types, 2);
	g_signal_connect (dt, "drop", G_CALLBACK (on_drop), n);
	gtk_widget_add_controller (n->surf, GTK_EVENT_CONTROLLER (dt));
	n->actions = g_simple_action_group_new ();
	gtk_widget_insert_action_group (n->gw, "k", G_ACTION_GROUP (n->actions));
	g_wins[n->id] = n;
	n->prevCurrent = g_cur;
	g_cur = n->id;
	use_vars (n);
	return n;
}

void wid_free (Wid *d);
void win_destroy (Win *w)
{
	std::vector<long> ids = w->wids;
	for (long id : ids) { Wid *d = wid (id); if (d) wid_free (d); }
	if (g_focusView && !wid (g_focusView)) g_focusView = 0;
	gtk_window_destroy (GTK_WINDOW (w->gw));
	w->buf.release ();
	if (w->actions) g_object_unref (w->actions);
	g_wins.erase (w->id);
	// (les evenements encore en file pour elle s'en vont)
	for (auto i = g_q.begin (); i != g_q.end (); ) if (i->win == (int) w->id) i = g_q.erase (i); else ++i;
	delete w;
}

void present_pending ()
{
	for (auto &p : g_wins)
		if (!p.second->presented) { p.second->presented = true; gtk_window_present (GTK_WINDOW (p.second->gw)); }
}

void do_resize (Win *w, int nw, int nh)
{
	w->pendW = w->pendH = -1;
	if (nw <= 0 || nh <= 0 || (nw == w->buf.w && nh == w->buf.h)) return;
	w->buf.alloc (nw, nh, tok (TJ_FACE));
	if (w->id == g_cur) use_vars (w);
	if (w->onResize)
	{
		long keep = g_cur;
		Buf *kt = g_target;
		g_target = 0;
		g_cur = w->id; use_vars (w);
		((long (*) (long, long)) w->onResize) (nw, nh);
		g_cur = keep; use_vars (cur ());
		g_target = kt;
	}
	redraw (&w->buf, w->surf);
}

// ---- les menus : le texte de ui_menu -> GMenu ---------------------------------------------------------------------------------
// « Ctrl+Maj+S », « ^O », « F5 », « Suppr » -> modificateurs (UI_MOD_*), code HID ; et l'accelerateur montre par GTK
bool parse_shortcut (const char *s, int *mods, int *hid, std::string &accel)
{
	*mods = 0; *hid = 0; accel.clear ();
	if (!s || !*s) return false;
	std::string t = s;
	std::string acc;
	if (t[0] == '^' && t.size () > 1) { *mods |= 1; acc += "<Control>"; t = t.substr (1); }
	for (;;)
	{
		size_t p = t.find ('+');
		if (p == std::string::npos || p == 0 || p + 1 >= t.size ()) break;
		std::string m = t.substr (0, p);
		if (!strcasecmp (m.c_str (), "Ctrl") || !strcasecmp (m.c_str (), "Ctl")) { *mods |= 1; acc += "<Control>"; }
		else if (!strcasecmp (m.c_str (), "Maj") || !strcasecmp (m.c_str (), "Shift")) { *mods |= 2; acc += "<Shift>"; }
		else if (!strcasecmp (m.c_str (), "Alt")) { *mods |= 4; acc += "<Alt>"; }
		else break;
		t = t.substr (p + 1);
	}
	const char *k = t.c_str ();
	std::string kn;
	if (strlen (k) == 1 && isalpha ((unsigned char) k[0])) { *hid = 0x04 + (tolower (k[0]) - 'a'); kn = std::string (1, (char) tolower (k[0])); }
	else if (strlen (k) == 1 && k[0] >= '1' && k[0] <= '9') { *hid = 0x1E + (k[0] - '1'); kn = k; }
	else if (!strcmp (k, "0")) { *hid = 0x27; kn = "0"; }
	else if ((k[0] == 'F' || k[0] == 'f') && atoi (k + 1) >= 1 && atoi (k + 1) <= 12) { *hid = 0x3A + atoi (k + 1) - 1; kn = std::string ("F") + (k + 1); }
	else if (!strcasecmp (k, "Suppr") || !strcasecmp (k, "Del") || !strcasecmp (k, "Delete")) { *hid = 0x4C; kn = "Delete"; }
	else if (!strcasecmp (k, "Entrée") || !strcasecmp (k, "Enter")) { *hid = 0x28; kn = "Return"; }
	else if (!strcasecmp (k, "Échap") || !strcasecmp (k, "Esc")) { *hid = 0x29; kn = "Escape"; }
	else if (!strcasecmp (k, "Espace") || !strcasecmp (k, "Space")) { *hid = 0x2C; kn = "space"; }
	else if (!strcasecmp (k, "Début") || !strcasecmp (k, "Home")) { *hid = 0x4A; kn = "Home"; }
	else if (!strcasecmp (k, "Fin") || !strcasecmp (k, "End")) { *hid = 0x4D; kn = "End"; }
	else if (!strcmp (k, "+")) { *hid = 0x2E; kn = "plus"; }
	else if (!strcmp (k, "-")) { *hid = 0x2D; kn = "minus"; }
	else return false;
	accel = acc + kn;
	guint kv = 0; GdkModifierType mm;
	if (!gtk_accelerator_parse (accel.c_str (), &kv, &mm)) accel.clear ();
	return true;
}

void menu_build (Win *w)
{
	GMenu *bar = g_menu_new ();
	GMenu *menu = 0, *section = 0, *sub = 0, *subSection = 0;
	std::string subTitle;
	int curMenu = -1;
	auto flush_menu = [&] () {
		if (!menu) return;
		g_menu_append_submenu (bar, TRs (w->menuTitles[(size_t) curMenu].c_str ()), G_MENU_MODEL (menu));
		g_object_unref (menu); menu = 0;
		if (section) { g_object_unref (section); section = 0; }
	};
	for (const MenuItem &it : w->items)
	{
		if (it.menu != curMenu)
		{
			flush_menu ();
			curMenu = it.menu;
			menu = g_menu_new ();
			section = g_menu_new ();
			g_menu_append_section (menu, 0, G_MENU_MODEL (section));
		}
		if (it.submenuStart) { sub = g_menu_new (); subSection = g_menu_new (); g_menu_append_section (sub, 0, G_MENU_MODEL (subSection)); subTitle = it.label; continue; }
		if (it.submenuEnd)
		{
			if (sub) { g_menu_append_submenu (section, TRs (subTitle.c_str ()), G_MENU_MODEL (sub)); g_object_unref (sub); g_object_unref (subSection); sub = subSection = 0; }
			continue;
		}
		GMenu *into = sub ? subSection : section;
		if (it.sep)
		{
			GMenu *n = g_menu_new ();
			g_menu_append_section (sub ? sub : menu, 0, G_MENU_MODEL (n));
			if (sub) { g_object_unref (subSection); subSection = n; } else { g_object_unref (section); section = n; }
			continue;
		}
		if (it.menu < 0) continue;
		char act[48];
		snprintf (act, sizeof act, "%c%ld", it.id < 0 ? 'x' : 'm', it.id < 0 ? -it.id : it.id);
		char full[64];
		snprintf (full, sizeof full, "k.%s", act);
		GMenuItem *mi = g_menu_item_new (TRs (it.label.c_str ()), full);
		if (!it.accel.empty ()) g_menu_item_set_attribute (mi, "accel", "s", it.accel.c_str ());
		g_menu_append_item (into, mi);
		g_object_unref (mi);
		int st = w->state.count (it.id) ? w->state[it.id] : 0;
		bool checkable = it.checked || (st & UMS_CHECKED) || w->state.count (it.id + 1000000);
		GAction *old = g_action_map_lookup_action (G_ACTION_MAP (w->actions), act);
		bool hasState = old && g_action_get_state_type (old);
		if (!old || hasState != checkable)
		{
			GSimpleAction *a = checkable ? g_simple_action_new_stateful (act, 0, g_variant_new_boolean ((st & UMS_CHECKED) != 0)) : g_simple_action_new (act, 0);
			g_signal_connect (a, "activate", G_CALLBACK (on_action), w);
			g_action_map_add_action (G_ACTION_MAP (w->actions), G_ACTION (a));
			g_object_unref (a);
		}
		GAction *a = g_action_map_lookup_action (G_ACTION_MAP (w->actions), act);
		g_simple_action_set_enabled (G_SIMPLE_ACTION (a), !(st & UMS_DISABLED) && !it.disabled);
		if (checkable) g_simple_action_set_state (G_SIMPLE_ACTION (a), g_variant_new_boolean ((st & UMS_CHECKED) != 0));
	}
	flush_menu ();
	if (sub) { g_object_unref (sub); g_object_unref (subSection); }
	if (w->menubar) gtk_popover_menu_bar_set_menu_model (GTK_POPOVER_MENU_BAR (w->menubar), G_MENU_MODEL (bar));
	else
	{
		w->menubar = gtk_popover_menu_bar_new_from_model (G_MENU_MODEL (bar));
		gtk_box_prepend (GTK_BOX (w->box), w->menubar);
	}
	g_object_unref (bar);
}

void menu_parse (Win *w, const char *spec)
{
	w->items.clear (); w->menuTitles.clear ();
	int menu = -1;
	const char *p = spec;
	while (p && *p)
	{
		const char *e = strchr (p, '\n');
		std::string line = e ? std::string (p, (size_t) (e - p)) : std::string (p);
		p = e ? e + 1 : 0;
		if (line.empty ()) continue;
		MenuItem it; it.id = 0; it.sep = it.disabled = it.checked = it.submenuStart = it.submenuEnd = false; it.smods = it.shid = 0;
		if (line[0] == 'M') { w->menuTitles.push_back (line[1] == '*' ? w->title : line.substr (1)); menu = (int) w->menuTitles.size () - 1; continue; }
		if (menu < 0) continue;
		it.menu = menu;
		if (line[0] == '-') it.sep = true;
		else if (line[0] == 'S') { it.submenuStart = true; it.label = line.substr (1); }
		else if (line[0] == 'E') it.submenuEnd = true;
		else if (line[0] == 'I')
		{
			const char *q = line.c_str () + 1;
			it.id = strtol (q, (char **) &q, 10);
			while (*q && *q != '\t') { if (*q == 'd') it.disabled = true; if (*q == 'c') it.checked = true; q++; }
			if (*q == '\t') q++;
			const char *t2 = strchr (q, '\t');
			it.label = t2 ? std::string (q, (size_t) (t2 - q)) : std::string (q);
			if (t2) parse_shortcut (TRs (t2 + 1), &it.smods, &it.shid, it.accel);
			if (it.checked) w->state[it.id] |= UMS_CHECKED;
		}
		else continue;
		w->items.push_back (it);
	}
	if (kport_soundfont_menu)
	{
		w->menuTitles.push_back ("Son");
		MenuItem it; it.id = -1; it.menu = (int) w->menuTitles.size () - 1; it.label = "Choisir la SoundFont..."; it.sep = it.disabled = it.checked = it.submenuStart = it.submenuEnd = false; it.smods = it.shid = 0;
		w->items.push_back (it);
	}
}

Win *menu_win ()
{
	for (auto &p : g_wins) if (!p.second->items.empty ()) return p.second;
	return 0;
}

// ---- les widgets -----------------------------------------------------------------------------------------------------------
void notify (Wid *d, int un)
{
	if (g_quiet || !d) return;
	Ev e; e.type = T_WIDGET; e.win = (int) d->win->id; e.wid = d->id; e.un = un;
	push (e);
}

bool eff_visible (Wid *d)
{
	for (int depth = 0; d && depth < 32; depth++)
	{
		if (!d->shown) return false;
		if (d->kind == K_PAGE)
		{
			Wid *t = wid (d->tabs);
			if (!t || t->value != d->pageNo) return false;
			d = t;
			continue;
		}
		d = d->parent ? wid (d->parent) : 0;
	}
	return true;
}
void refresh_visibility (Win *w)
{
	for (long id : w->wids)
	{
		Wid *d = wid (id);
		if (d && d->gw) gtk_widget_set_visible (d->gw, eff_visible (d));
	}
	gtk_widget_queue_allocate (w->surf);
}

Wid *wid_new (Kind k, long x, long y, long w, long h, GtkWidget *gw)
{
	Win *wn = cur ();
	if (!wn) { if (gw) g_object_ref_sink (gw), g_object_unref (gw); return 0; }
	Wid *d = new Wid;
	d->id = g_nextWid++;
	d->kind = k; d->win = wn;
	d->x = (int) x; d->y = (int) y; d->w = (int) w; d->h = (int) h;
	d->gw = gw;
	if (gw)
	{
		g_object_set_data (G_OBJECT (gw), "kw", d);
		gtk_widget_set_parent (gw, wn->surf);
		gtk_widget_set_overflow (gw, GTK_OVERFLOW_HIDDEN);
	}
	g_wids[d->id] = d;
	wn->wids.push_back (d->id);
	if (getenv ("KORIN_DEBUG_WIDGETS")) fprintf (stderr, "uikit: widget %ld sorte %d fenetre %ld (%ld,%ld %ldx%ld)\n", d->id, (int) k, wn->id, x, y, w, h);
	gtk_widget_queue_allocate (wn->surf);
	return d;
}

void wid_free (Wid *d)
{
	if (d->gw) gtk_widget_unparent (d->gw);
	d->buf.release ();
	for (auto &p : g_wids) if (p.second->parent == d->id) p.second->parent = 0;
	Win *w = d->win;
	for (size_t i = 0; i < w->wids.size (); i++) if (w->wids[i] == d->id) { w->wids.erase (w->wids.begin () + (long) i); break; }
	for (auto i = g_q.begin (); i != g_q.end (); ) if (i->wid == d->id) i = g_q.erase (i); else ++i;
	if (g_focusView == d->id) g_focusView = 0;
	g_wids.erase (d->id);
	delete d;
}

std::vector<std::string> split_bar (const char *s)
{
	std::vector<std::string> r;
	if (!s) return r;
	const char *p = s;
	for (;;)
	{
		const char *e = strchr (p, '|');
		r.push_back (e ? std::string (p, (size_t) (e - p)) : std::string (p));
		if (!e) break;
		p = e + 1;
	}
	return r;
}

GtkWidget *elabel (const char *s, float xalign)
{
	GtkWidget *l = gtk_label_new (s);
	gtk_label_set_ellipsize (GTK_LABEL (l), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign (GTK_LABEL (l), xalign);
	return l;
}

void on_clicked (GtkButton *, gpointer d) { notify ((Wid *) d, UN_ACTIVATE); }
void on_toggled (GtkCheckButton *b, gpointer data)
{
	Wid *d = (Wid *) data;
	d->value = gtk_check_button_get_active (b);
	if (d->kind == K_RADIO && !d->value) return;
	notify (d, UN_CHANGE);
}
void on_dd (GObject *, GParamSpec *, gpointer data)
{
	Wid *d = (Wid *) data;
	guint s = gtk_drop_down_get_selected (GTK_DROP_DOWN (d->gw));
	d->value = s == GTK_INVALID_LIST_POSITION ? -1 : (long) s;
	notify (d, UN_CHANGE);
}
void on_seg (GtkToggleButton *b, gpointer data)
{
	Wid *d = (Wid *) data;
	if (!gtk_toggle_button_get_active (b)) return;
	for (size_t i = 0; i < d->parts.size (); i++) if (d->parts[i] == GTK_WIDGET (b)) d->value = (long) i;
	notify (d, UN_CHANGE);
}
void on_entry_changed (GtkEditable *, gpointer d) { notify ((Wid *) d, UN_CHANGE); }
void on_entry_activate (GtkEntry *, gpointer d) { notify ((Wid *) d, UN_ACTIVATE); }
void on_buffer_changed (GtkTextBuffer *, gpointer d) { notify ((Wid *) d, UN_CHANGE); }
void on_range (GtkRange *r, gpointer data)
{
	Wid *d = (Wid *) data;
	long v = (long) lround (gtk_range_get_value (r));
	if (v == d->value && g_quiet == 0) return;
	d->value = v;
	notify (d, UN_CHANGE);
}
void on_spin (GtkSpinButton *s, gpointer data)
{
	Wid *d = (Wid *) data;
	d->value = gtk_spin_button_get_value_as_int (s);
	notify (d, UN_CHANGE);
}
void on_switch_page (GtkNotebook *, GtkWidget *, guint n, gpointer data)
{
	Wid *d = (Wid *) data;
	d->value = (long) n;
	refresh_visibility (d->win);
	notify (d, UN_CHANGE);
}
void on_row_selected (GtkListBox *, GtkListBoxRow *r, gpointer data)
{
	Wid *d = (Wid *) data;
	d->value = r ? gtk_list_box_row_get_index (r) : -1;
	if (r) notify (d, UN_CHANGE);
}
void on_row_activated (GtkListBox *, GtkListBoxRow *r, gpointer data)
{
	Wid *d = (Wid *) data;
	d->value = gtk_list_box_row_get_index (r);
	notify (d, UN_ACTIVATE);
}
void on_list_context (GtkGestureClick *g, int, double x, double y, gpointer data)
{
	Wid *d = (Wid *) data;
	(void) x;
	GtkListBoxRow *r = gtk_list_box_get_row_at_y (GTK_LIST_BOX (d->inner), (int) y);
	if (!r) return;
	{ Quiet q; gtk_list_box_select_row (GTK_LIST_BOX (d->inner), r); }
	d->value = gtk_list_box_row_get_index (r);
	gtk_gesture_set_state (GTK_GESTURE (g), GTK_EVENT_SEQUENCE_CLAIMED);
	notify (d, UN_CONTEXT);
}

// la molette (ui_knob) : un arc, a tirer de haut en bas
void knob_draw (GtkDrawingArea *, cairo_t *cr, int w, int h, gpointer data)
{
	Wid *d = (Wid *) data;
	double cx = w / 2.0, cy = h / 2.0, r = (w < h ? w : h) / 2.0 - 3;
	if (r < 4) return;
	double a0 = M_PI * 0.75, a1 = M_PI * 2.25;
	double f = d->vmax > d->vmin ? (double) (d->value - d->vmin) / (double) (d->vmax - d->vmin) : 0;
	cairo_set_line_width (cr, 3.5);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	cr_color (cr, tok (TJ_LINE_STRONG));
	cairo_arc (cr, cx, cy, r, a0, a1); cairo_stroke (cr);
	cr_color (cr, tok (TJ_ACCENT));
	cairo_arc (cr, cx, cy, r, a0, a0 + (a1 - a0) * f); cairo_stroke (cr);
	double a = a0 + (a1 - a0) * f;
	cr_color (cr, tok (TJ_TEXT1));
	cairo_set_line_width (cr, 2);
	cairo_move_to (cr, cx + cos (a) * r * 0.35, cy + sin (a) * r * 0.35);
	cairo_line_to (cr, cx + cos (a) * r * 0.85, cy + sin (a) * r * 0.85);
	cairo_stroke (cr);
}
void knob_set (Wid *d, long v)
{
	if (v < d->vmin) v = d->vmin;
	if (v > d->vmax) v = d->vmax;
	if (v == d->value) return;
	d->value = v;
	gtk_widget_queue_draw (d->gw);
	notify (d, UN_CHANGE);
}
void knob_begin (GtkGestureDrag *, double, double, gpointer data) { Wid *d = (Wid *) data; d->startY = (double) d->value; }
void knob_update (GtkGestureDrag *, double, double dy, gpointer data)
{
	Wid *d = (Wid *) data;
	double range = (double) (d->vmax - d->vmin);
	knob_set (d, (long) lround (d->startY - dy * range / 150.0));
}
gboolean knob_scroll (GtkEventControllerScroll *, double, double dy, gpointer data)
{
	Wid *d = (Wid *) data;
	long step = (d->vmax - d->vmin) / 50; if (step < 1) step = 1;
	knob_set (d, d->value + (dy < 0 ? step : -step));
	return TRUE;
}

GtkWidget *row_widget (Wid *d, const char *text)
{
	if (d->cols.size () <= 1 && !strchr (text, '\t'))
	{
		GtkWidget *l = elabel (text, 0);
		gtk_widget_set_margin_start (l, 6); gtk_widget_set_margin_end (l, 6);
		gtk_widget_set_margin_top (l, 2); gtk_widget_set_margin_bottom (l, 2);
		return l;
	}
	GtkWidget *b = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_margin_start (b, 6); gtk_widget_set_margin_end (b, 6);
	gtk_widget_set_margin_top (b, 2); gtk_widget_set_margin_bottom (b, 2);
	const char *p = text;
	for (size_t c = 0; ; c++)
	{
		const char *e = strchr (p, '\t');
		std::string s = e ? std::string (p, (size_t) (e - p)) : std::string (p);
		bool right = c < d->cols.size () && (d->cols[c].second & ULC_RIGHT);
		GtkWidget *l = elabel (s.c_str (), right ? 1.0f : 0.0f);
		if (c < d->cols.size () && c + 1 < d->cols.size ()) gtk_widget_set_size_request (l, d->cols[c].first.empty () ? 60 : (int) d->cols[c].second >> 8, -1);
		if (!e) gtk_widget_set_hexpand (l, TRUE);
		gtk_box_append (GTK_BOX (b), l);
		if (!e) break;
		p = e + 1;
	}
	return b;
}

void list_scroll_to (Wid *d)
{
	if (d->value < 0) return;
	GtkListBoxRow *r = gtk_list_box_get_row_at_index (GTK_LIST_BOX (d->inner), (int) d->value);
	GtkWidget *sw = gtk_widget_get_ancestor (d->inner, GTK_TYPE_SCROLLED_WINDOW);
	if (!r || !sw) return;
	graphene_rect_t b;
	if (!gtk_widget_compute_bounds (GTK_WIDGET (r), d->inner, &b)) return;
	GtkAdjustment *a = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (sw));
	double v = gtk_adjustment_get_value (a), page = gtk_adjustment_get_page_size (a);
	if (b.origin.y < v) gtk_adjustment_set_value (a, b.origin.y);
	else if (b.origin.y + b.size.height > v + page) gtk_adjustment_set_value (a, b.origin.y + b.size.height - page);
}
gboolean list_scroll_idle (gpointer data)
{
	Wid *d = wid ((long) data);
	if (d) list_scroll_to (d);
	return G_SOURCE_REMOVE;
}

void dd_setup (GtkSignalListItemFactory *, GObject *li, gpointer ell)
{
	GtkWidget *l = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (l), 0);
	if (ell) gtk_label_set_ellipsize (GTK_LABEL (l), PANGO_ELLIPSIZE_END);
	gtk_list_item_set_child (GTK_LIST_ITEM (li), l);
}
void dd_bind (GtkSignalListItemFactory *, GObject *li, gpointer)
{
	GtkWidget *l = gtk_list_item_get_child (GTK_LIST_ITEM (li));
	GtkStringObject *o = GTK_STRING_OBJECT (gtk_list_item_get_item (GTK_LIST_ITEM (li)));
	gtk_label_set_text (GTK_LABEL (l), o ? gtk_string_object_get_string (o) : "");
}

void label_style (Wid *d)
{
	GtkWidget *l = d->gw;
	long f = d->flags;
	gtk_widget_remove_css_class (l, "k-title"); gtk_widget_remove_css_class (l, "k-bold"); gtk_widget_remove_css_class (l, "dim-label");
	if (f & ULB_TITLE) gtk_widget_add_css_class (l, "k-title");
	else if (f & ULB_BOLD) gtk_widget_add_css_class (l, "k-bold");
	if (f & ULB_DIM) gtk_widget_add_css_class (l, "dim-label");
	gtk_label_set_wrap (GTK_LABEL (l), (f & ULB_WRAP) != 0);
	gtk_label_set_ellipsize (GTK_LABEL (l), (f & ULB_WRAP) ? PANGO_ELLIPSIZE_NONE : PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign (GTK_LABEL (l), (f & ULB_RIGHT) ? 1.0f : (f & ULB_CENTER) ? 0.5f : 0.0f);
	gtk_label_set_yalign (GTK_LABEL (l), (f & ULB_WRAP) ? 0.0f : 0.5f);
}
void button_style (Wid *d)
{
	gtk_widget_remove_css_class (d->gw, "suggested-action"); gtk_widget_remove_css_class (d->gw, "flat");
	if (d->flags & UB_DEFAULT) gtk_widget_add_css_class (d->gw, "suggested-action");
	if (d->flags & UB_GHOST) gtk_widget_add_css_class (d->gw, "flat");
}

std::string list_text (Wid *d, long i) { return i >= 0 && i < (long) d->items.size () ? d->items[(size_t) i] : std::string (); }

void view_paint (Wid *v)
{
	if (!v->paint || v->buf.w <= 0 || v->buf.h <= 0) { v->dirty = false; return; }
	Buf *keep = g_target;
	g_target = &v->buf;
	v->buf.clipOn = false;
	((long (*) (long, long, long)) v->paint) (v->id, v->buf.w, v->buf.h);
	g_target = keep;
	v->buf.clipOn = false;
	v->dirty = false;
	redraw (&v->buf, v->gw);
}

// ---- les dialogues : des boucles imbriquees -------------------------------------------------------------------------------
void run_until (bool *done) { while (!*done) g_main_context_iteration (0, TRUE); }
GtkWindow *parent_win () { Win *w = cur (); if (!w) w = menu_win (); return w ? GTK_WINDOW (w->gw) : 0; }

struct AlertRes { bool done; int r; };
void alert_cb (GObject *src, GAsyncResult *res, gpointer data)
{
	AlertRes *a = (AlertRes *) data;
	a->r = gtk_alert_dialog_choose_finish (GTK_ALERT_DIALOG (src), res, 0);
	a->done = true;
}
struct FileRes { bool done; char *path; int mode; };
void file_cb (GObject *src, GAsyncResult *res, gpointer data)
{
	FileRes *f = (FileRes *) data;
	GFile *g = f->mode == 0 ? gtk_file_dialog_open_finish (GTK_FILE_DIALOG (src), res, 0)
		: f->mode == 1 ? gtk_file_dialog_save_finish (GTK_FILE_DIALOG (src), res, 0)
		: gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (src), res, 0);
	if (g) { f->path = g_file_get_path (g); g_object_unref (g); }
	f->done = true;
}
// « *.mid;*.midi » ou « Chansons|*.kson;*.sq|Tous|* » -> les filtres
void set_filters (GtkFileDialog *d)
{
	if (g_filter.empty ()) return;
	GListStore *fl = g_list_store_new (GTK_TYPE_FILE_FILTER);
	std::vector<std::string> parts = split_bar (g_filter.c_str ());
	std::vector<std::pair<std::string, std::string>> kinds;
	if (parts.size () == 1) kinds.push_back ({ parts[0], parts[0] });
	else for (size_t i = 0; i + 1 < parts.size (); i += 2) kinds.push_back ({ parts[i], parts[i + 1] });
	GtkFileFilter *first = 0;
	for (auto &k : kinds)
	{
		GtkFileFilter *f = gtk_file_filter_new ();
		gtk_file_filter_set_name (f, k.first == k.second ? k.first.c_str () : (k.first + " (" + k.second + ")").c_str ());
		std::string pats = k.second;
		size_t s = 0;
		while (s <= pats.size ())
		{
			size_t e = pats.find (';', s);
			std::string pat = pats.substr (s, e == std::string::npos ? std::string::npos : e - s);
			if (pat == "*" || pat == "*.*") gtk_file_filter_add_pattern (f, "*");
			else if (pat.size () > 2 && pat[0] == '*' && pat[1] == '.') gtk_file_filter_add_suffix (f, pat.c_str () + 2);
			else if (!pat.empty ()) gtk_file_filter_add_pattern (f, pat.c_str ());
			if (e == std::string::npos) break;
			s = e + 1;
		}
		g_list_store_append (fl, f);
		if (!first) first = f;
		g_object_unref (f);
	}
	gtk_file_dialog_set_filters (d, G_LIST_MODEL (fl));
	if (first) gtk_file_dialog_set_default_filter (d, first);
	g_object_unref (fl);
}
long file_dialog (int mode, long dir, long name, long buf, long cap)
{
	GtkFileDialog *d = gtk_file_dialog_new ();
	gtk_file_dialog_set_modal (d, TRUE);
	gtk_file_dialog_set_title (d, mode == 0 ? "Ouvrir" : mode == 1 ? "Enregistrer" : "Choisir un dossier");
	if (mode != 2) set_filters (d);
	char tb[PATH_MAX];
	const char *dp = dir ? kport_path ((const char *) dir, tb, sizeof tb) : 0;
	if (dp && *dp)
	{
		GFile *g = g_file_new_for_path (dp);
		if (g_file_query_file_type (g, G_FILE_QUERY_INFO_NONE, 0) == G_FILE_TYPE_DIRECTORY) gtk_file_dialog_set_initial_folder (d, g);
		g_object_unref (g);
	}
	if (mode == 1 && name) gtk_file_dialog_set_initial_name (d, (const char *) name);
	FileRes r = { false, 0, mode };
	if (mode == 0) gtk_file_dialog_open (d, parent_win (), 0, file_cb, &r);
	else if (mode == 1) gtk_file_dialog_save (d, parent_win (), 0, file_cb, &r);
	else gtk_file_dialog_select_folder (d, parent_win (), 0, file_cb, &r);
	run_until (&r.done);
	g_object_unref (d);
	if (!r.path) return 0;
	snprintf ((char *) buf, (size_t) cap, "%s", r.path);
	g_free (r.path);
	return 1;
}

} // namespace

// ============================================================================================================================
extern "C" {

int uikit_kit_open (void) { return 1; }

long ui_init (long v)
{
	g_vars = (struct ui_vars *) v;
	static bool done;
	if (!done)
	{
		done = true;
		if (!gtk_init_check ()) { fprintf (stderr, "uikit: pas d'affichage (GTK 4 ne s'ouvre pas)\n"); return 0; }
		// (gtk_init prend la langue de l'utilisateur pour tout : les nombres restent ceux du C, comme sous Korin,
		// sinon « 0.5 » d'un .ifs se lit 0 et printf ecrit « 0,5 »)
		setlocale (LC_NUMERIC, "C");
		pick_theme ();
		GtkCssProvider *css = gtk_css_provider_new ();
		gtk_css_provider_load_from_string (css, CSS);
		gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		text_init ();
		for (int i = 0; i < 10; i++) font_block (g_fonts[i], i);
		// les mots de l'app dans la langue de l'utilisateur (Koton : ses sources sont en anglais)
		const char *lang = getenv ("LANGUAGE");
		if (!lang || !*lang) lang = getenv ("LC_ALL");
		if (!lang || !*lang) lang = getenv ("LC_MESSAGES");
		if (!lang || !*lang) lang = getenv ("LANG");
		if (lang && !strncmp (lang, "fr", 2))
		{
			char tb[PATH_MAX], app[PATH_MAX];
			snprintf (app, sizeof app, "%s/src/apps/%s/lang/fr.txt", kport_rootdir (), kport_app ());
			// (les mots du systeme, /res/lang/fr.txt, puis ceux de l'app)
			const char *files[2] = { kport_path ("/res/lang/fr.txt", tb, sizeof tb), app };
			if (kport_rootdir ()[0]) for (const char *fp : files) tr_load_file (fp);
		}
		for (auto &x : g_trPort) g_tr[x.first] = x.second;
	}
	if (g_vars)
	{
		font_block (g_vars->font, UFN_SANS);
		font_block (g_vars->font_b, UFN_BOLD);
		font_block (g_vars->font_m, UFN_MONO);
	}
	return 1;
}

long ui_tokens (void) { return (long) g_tok; }
long ui_tr (long s) { return (long) TRs ((const char *) s); }
long ui_tone (long c, long level)
{
	unsigned r = (unsigned) (c >> 16) & 255, g = (unsigned) (c >> 8) & 255, b = (unsigned) c & 255;
	auto f = [level] (unsigned v) -> unsigned { long n = level <= 128 ? (long) v * level / 128 : (long) v + (255 - (long) v) * (level - 128) / 128; return (unsigned) (n < 0 ? 0 : n > 255 ? 255 : n); };
	return (long) (f (r) << 16 | f (g) << 8 | f (b));
}
long ui_mix (long a, long b, long t)
{
	if (t < 0) t = 0;
	if (t > 256) t = 256;
	unsigned r = 0;
	for (int s = 0; s <= 16; s += 8)
	{
		long x = (a >> s) & 255, y = (b >> s) & 255;
		r |= (unsigned) ((x + (y - x) * t / 256) & 255) << s;
	}
	return (long) r;
}
long ui_ink (long bg)
{
	long l = ((bg >> 16) & 255) * 30 + ((bg >> 8) & 255) * 59 + (bg & 255) * 11;
	return l > 140 * 100 ? 0x14171C : 0xF5F6F8;
}

long ui_screen (long buf)
{
	long *s = (long *) buf;
	s[0] = 1920; s[1] = 1080; s[2] = 0; s[3] = 0;
	GdkDisplay *d = gdk_display_get_default ();
	GListModel *ms = d ? gdk_display_get_monitors (d) : 0;
	GdkMonitor *m = ms && g_list_model_get_n_items (ms) ? (GdkMonitor *) g_list_model_get_item (ms, 0) : 0;
	if (m)
	{
		GdkRectangle r;
		gdk_monitor_get_geometry (m, &r);
		s[0] = r.width; s[1] = r.height; s[3] = 32;
		g_object_unref (m);
	}
	return 0;
}

long ui_window (long w, long h, long title) { return win_new (-1, -1, w, h, (const char *) title, 0, false)->id; }
long ui_window_ex (long x, long y, long w, long h, long title, long flags) { return win_new (x, y, w, h, (const char *) title, flags, false)->id; }
long ui_window_use (long id)
{
	long prev = g_cur;
	if (win (id)) { g_cur = id; use_vars (win (id)); }
	return prev;
}
long ui_window_close (long id)
{
	Win *w = win (id ? id : g_cur);
	if (!w) return 0;
	bool wasCur = w->id == g_cur;
	win_destroy (w);
	if (wasCur) { g_cur = 0; use_vars (0); }
	return 0;
}
long ui_title (long t) { Win *w = cur (); if (w && t) { w->title = (const char *) t; gtk_window_set_title (GTK_WINDOW (w->gw), w->title.c_str ()); } return 0; }
long ui_min_size (long w, long h)
{
	Win *n = cur ();
	if (!n) return 0;
	n->minw = (int) w; n->minh = (int) h;
	gtk_widget_queue_resize (n->surf);
	return 0;
}
long ui_on_resize (long fn, long) { Win *w = cur (); if (w) w->onResize = fn; return 0; }
long ui_maximize (void)
{
	Win *w = cur ();
	if (!w) return 0;
	if (gtk_window_is_maximized (GTK_WINDOW (w->gw))) gtk_window_unmaximize (GTK_WINDOW (w->gw));
	else gtk_window_maximize (GTK_WINDOW (w->gw));
	return 0;
}
long ui_raise (long id) { Win *w = win (id); if (w) gtk_window_present (GTK_WINDOW (w->gw)); return 0; }
long ui_cursor (long shape)
{
	static const char *const N[] = { "default", "text", "pointer", "ew-resize", "ns-resize", "wait", "crosshair", "grabbing", "move", "nwse-resize", "nesw-resize", "not-allowed" };
	Win *w = cur ();
	if (w && shape >= 0 && shape < 12) gtk_widget_set_cursor_from_name (w->surf, N[shape]);
	return 0;
}
long ui_update (long, long, long, long) { Win *w = cur (); if (w) redraw (&w->buf, w->surf); return 0; }
long ui_update_all (void) { Win *w = cur (); if (w) redraw (&w->buf, w->surf); return 0; }

static gboolean timeout_cb (gpointer d) { *(bool *) d = true; return G_SOURCE_REMOVE; }

// ---- l'essai sans les mains : KORIN_SCRIPT="1500:menu:11;4000:shot:/tmp/a.png;6000:close" -------------------------
// (ms depuis le premier ui_event : menu:<id>, key:<hid>[:mods], click:x,y[,n[,3]], shot:<fichier.png> (la fenetre
// principale) ; key2:, chars2:<texte>, click2:, shot2:, press2:<texte d'un bouton> : la derniere fenetre ouverte (un
// dialogue) ; dump:, dump2: l'etat des widgets ; wset:<widget>:<v>, wact:<widget> ; close, exit)
static Win *main_win () { return g_wins.empty () ? 0 : g_wins.begin ()->second; }
static Win *last_win () { return g_wins.empty () ? 0 : g_wins.rbegin ()->second; }
// la vue qui a le clavier dans cette fenetre (0 : aucune)
static long focused_view (Win *w)
{
	if (!w) return 0;
	GtkWidget *f = gtk_root_get_focus (GTK_ROOT (w->gw));
	for (auto &p : g_wids) if (p.second->kind == K_VIEW && p.second->win == w && p.second->gw && p.second->gw == f) return p.second->id;
	return 0;
}
static void script_key (Win *w, int hid, int mods, int ch, bool toView)
{
	Ev e; e.type = WEV_KEY; e.kind = 1; e.code = hid; e.win = (int) w->id; e.mods = mods; e.ch = ch;
	if (toView) e.wid = focused_view (w);
	push (e); e.kind = 0; push (e);
}
static int script_ch (int hid)
{
	if (hid == 0x2C) return ' ';
	if (hid >= 0x04 && hid <= 0x1D) return 'a' + hid - 0x04;
	if (hid == 0x28) return 13;
	return 0;
}
static void script_click (Win *w, const char *a)
{
	// (un clic au point x,y de la fenetre, sur la vue qui est la, ou sur son fond ; ,2 : double ; ,1,3 : bouton droit)
	int x = 0, y = 0, n = 1, b = 1;
	sscanf (a, "%d,%d,%d,%d", &x, &y, &n, &b);
	int held = b == 3 ? 2 : 1;
	GtkWidget *p = gtk_widget_pick (w->surf, x, y, GTK_PICK_DEFAULT);
	KSurf *s = p && G_TYPE_CHECK_INSTANCE_TYPE (p, ksurf_get_type ()) ? (KSurf *) p : (KSurf *) w->surf;
	double lx = x, ly = y;
	if (s->view)
	{
		lx -= s->view->x; ly -= s->view->y;
		if (s->view->flags & UVW_FOCUS) { gtk_widget_grab_focus (GTK_WIDGET (s)); g_focusView = s->view->id; }
	}
	for (int i = 1; i <= n; i++) { mouse_ev (s, 1, lx, ly, held, 0, i); mouse_ev (s, 2, lx, ly, 0, 0, 0); }
}
static void shot (Win *w, const char *path)
{
	if (!w) return;
	GtkWidget *wd = w->gw;
	int W = gtk_widget_get_width (wd), H = gtk_widget_get_height (wd);
	GdkPaintable *p = gtk_widget_paintable_new (wd);
	GtkSnapshot *s = gtk_snapshot_new ();
	gdk_paintable_snapshot (p, s, W, H);
	GskRenderNode *node = gtk_snapshot_free_to_node (s);
	GskRenderer *r = gtk_native_get_renderer (GTK_NATIVE (wd));
	if (node && r)
	{
		GdkTexture *t = gsk_renderer_render_texture (r, node, 0);
		gdk_texture_save_to_png (t, path);
		g_object_unref (t);
		fprintf (stderr, "uikit: capture %s (%dx%d)\n", path, W, H);
	}
	if (node) gsk_render_node_unref (node);
	g_object_unref (p);
}
static gboolean script_step (gpointer data)
{
	std::string *st = (std::string *) data;
	const char *a = st->c_str ();
	Win *w = main_win ();
	if (!strncmp (a, "menu:", 5) && w) { Ev e; e.type = WEV_MENU; e.code = atoi (a + 5); e.win = (int) w->id; push (e); }
	else if (!strncmp (a, "key:", 4) && w)
	{
		int hid = (int) strtol (a + 4, 0, 0);
		const char *m = strchr (a + 4, ':');
		script_key (w, hid, m ? atoi (m + 1) : 0, script_ch (hid), false);
	}
	// key2:<hid>[:mods] : a la derniere fenetre ouverte (un dialogue), a sa vue qui a le clavier
	else if (!strncmp (a, "key2:", 5) && last_win ())
	{
		int hid = (int) strtol (a + 5, 0, 0);
		const char *m = strchr (a + 5, ':');
		script_key (last_win (), hid, m ? atoi (m + 1) : 0, script_ch (hid), true);
	}
	// chars2:<texte> : des caracteres tapes dans la vue qui a le clavier de la derniere fenetre
	else if (!strncmp (a, "chars2:", 7) && last_win ())
	{
		for (const char *c = a + 7; *c; c++)
		{
			int ch = (unsigned char) *c, hid = 0;
			if (ch >= 'a' && ch <= 'z') hid = 0x04 + ch - 'a';
			else if (ch >= 'A' && ch <= 'Z') hid = 0x04 + ch - 'A';
			else if (ch == ' ') hid = 0x2C;
			script_key (last_win (), hid, 0, ch, true);
		}
	}
	else if (!strncmp (a, "shot:", 5)) shot (w, a + 5);
	else if (!strncmp (a, "shot2:", 6)) shot (last_win (), a + 6);
	else if (!strcmp (a, "close") && w) { Ev e; e.type = WEV_CLOSE; e.win = (int) w->id; push (e); }
	else if (!strncmp (a, "wset:", 5))
	{
		long id = atol (a + 5);
		const char *v = strchr (a + 5, ':');
		Wid *d = wid (id);
		if (d && v) { ui_w_set_value (id, atol (v + 1)); notify (d, UN_CHANGE); }
	}
	else if (!strncmp (a, "wact:", 5)) { Wid *d = wid (atol (a + 5)); if (d) notify (d, UN_ACTIVATE); }
	// dump:, dump2: l'etat reel des widgets de la fenetre principale, de la derniere (les captures peuvent etre en retard)
	else if (!strncmp (a, "dump", 4) && (a[4] == ':' || a[4] == '2'))
	{
		Win *d = a[4] == '2' ? last_win () : w;
		if (d)
		{
			fprintf (stderr, "uikit: fenetre %ld \xC2\xAB %s \xC2\xBB, focus %ld\n", d->id, d->title.c_str (), focused_view (d));
			for (long id : d->wids)
			{
				Wid *x = wid (id);
				if (!x) continue;
				std::string t = x->kind == K_TEXTBOX ? std::string (gtk_editable_get_text (GTK_EDITABLE (x->gw))) : x->text;
				fprintf (stderr, "uikit:   %ld sorte %d %s%s (%d,%d %dx%d) %s\n", id, (int) x->kind, x->gw && gtk_widget_get_visible (x->gw) ? "visible" : "cache",
					x->gw && !gtk_widget_is_sensitive (x->gw) ? " grise" : "", x->x, x->y, x->w, x->h, t.substr (0, 60).c_str ());
			}
		}
	}
	// press2:<texte> : le bouton de ce texte dans la derniere fenetre ouverte (un dialogue)
	else if (!strncmp (a, "press2:", 7) && last_win ())
	{
		for (long id : last_win ()->wids) { Wid *d = wid (id); if (d && d->kind == K_BUTTON && d->text == a + 7) { notify (d, UN_ACTIVATE); break; } }
	}
	else if (!strncmp (a, "click:", 6) && w) script_click (w, a + 6);
	else if (!strncmp (a, "click2:", 7) && last_win ()) script_click (last_win (), a + 7);	// (la derniere fenetre)
	else if (!strcmp (a, "exit")) { fflush (0); _exit (0); }
	delete st;
	return G_SOURCE_REMOVE;
}
static void script_start ()
{
	static bool done;
	if (done) return;
	done = true;
	const char *s = getenv ("KORIN_SCRIPT");
	if (!s || !*s) return;
	std::string all = s;
	size_t p = 0;
	while (p < all.size ())
	{
		size_t e = all.find (';', p);
		std::string step = all.substr (p, e == std::string::npos ? std::string::npos : e - p);
		size_t c = step.find (':');
		if (c != std::string::npos) g_timeout_add ((guint) atoi (step.c_str ()), script_step, new std::string (step.substr (c + 1)));
		if (e == std::string::npos) break;
		p = e + 1;
	}
}

long ui_event (long ms)
{
	present_pending ();
	script_start ();
	if (g_q.empty ())
	{
		if (ms > 0)
		{
			bool fired = false;
			guint src = g_timeout_add ((guint) ms, timeout_cb, &fired);
			while (g_q.empty () && !fired) g_main_context_iteration (0, TRUE);
			if (!fired) g_source_remove (src);
		}
		else while (g_q.empty () && g_main_context_iteration (0, FALSE)) {}
	}
	if (g_q.empty ())
	{
		g_ev = Ev ();
		if (g_vars) memset (g_vars->ev, 0, 32);
		return 0;
	}
	g_ev = g_q.front ();
	g_q.pop_front ();
	if (g_vars)
	{
		int32_t *e = (int32_t *) g_vars->ev;
		e[0] = g_ev.type; e[1] = g_ev.kind; e[2] = g_ev.x; e[3] = g_ev.y; e[4] = g_ev.code; e[5] = g_ev.ch; e[6] = g_ev.mods; e[7] = g_ev.win;
		if (g_ev.type == WEV_WHEEL) e[3] = g_ev.y;
	}
	return g_ev.type;
}

long ui_w_event (long)
{
	long w = 0, code = 0, taken = 0;
	Ev &e = g_ev;
	switch (e.type)
	{
	case T_WIDGET:
		w = e.wid; code = e.un; taken = 1;
		break;
	case WEV_RESIZE:
	{
		Win *n = win (e.win);
		if (n) do_resize (n, e.x, e.y);
		taken = 1;
		break;
	}
	case WEV_MOUSE:
		if (e.wid)
		{
			Wid *v = wid (e.wid);
			if (v && v->mouse) code = ((long (*) (long, long, long, long, long)) v->mouse) (v->id, e.kind, e.x, e.y, e.info);
			w = e.wid; taken = 1;
		}
		break;
	case WEV_WHEEL:
		if (e.wid)
		{
			Wid *v = wid (e.wid);
			if (v && v->wheel) code = ((long (*) (long, long)) v->wheel) (v->id, e.y);
			w = e.wid; taken = v && v->wheel;
		}
		break;
	case WEV_KEY:
		if (e.wid && e.kind)
		{
			Wid *v = wid (e.wid);
			long r = v && v->key ? ((long (*) (long, long, long, long)) v->key) (v->id, e.code, e.ch, e.mods) : -1;
			if (r != -1) { w = e.wid; taken = 1; code = r > 0 ? r : 0; }
		}
		break;
	}
	if (w && code)
	{
		Wid *d = wid (w);
		if (d && d->fn) ((long (*) (long, long)) d->fn) (w, code);
	}
	uikit__ret[0] = w; uikit__ret[1] = code; uikit__ret[2] = taken;
	return w;
}

long ui_w_idle (void) { return 0; }

long ui_menu (long spec)
{
	Win *w = cur ();
	if (!w) return 0;
	menu_parse (w, (const char *) spec);
	menu_build (w);
	return 0;
}
long ui_menu_state (long id, long st)
{
	Win *w = menu_win ();
	if (!w) return 0;
	int old = w->state.count (id) ? w->state[id] : 0;
	w->state[id] = (int) st;
	if (st & UMS_CHECKED) w->state[id + 1000000] = 1;	// (une entree cochee une fois reste une case)
	char act[48];
	snprintf (act, sizeof act, "m%ld", id);
	GAction *a = g_action_map_lookup_action (G_ACTION_MAP (w->actions), act);
	if (!a) return 0;
	bool needState = (st & UMS_CHECKED) && !g_action_get_state_type (a);
	if (needState) { menu_build (w); return 0; }
	g_simple_action_set_enabled (G_SIMPLE_ACTION (a), !(st & UMS_DISABLED));
	if (g_action_get_state_type (a)) g_simple_action_set_state (G_SIMPLE_ACTION (a), g_variant_new_boolean ((st & UMS_CHECKED) != 0));
	(void) old;
	return 0;
}
long ui_menu_key (long)
{
	if (g_ev.type != WEV_KEY || !g_ev.kind) return -1;
	Win *w = menu_win ();
	if (!w) return -1;
	int mods = g_ev.mods & 7;
	for (const MenuItem &it : w->items)
		if (it.shid && it.shid == g_ev.code && it.smods == mods && it.id >= 0)
		{
			int st = w->state.count (it.id) ? w->state[it.id] : 0;
			if (st & UMS_DISABLED) return -1;
			return it.id;
		}
	return -1;
}

// ---- le dessin ----
long ui_fill (long x, long y, long w, long h, long c) { fill_rect (target (), x, y, w, h, (unsigned) c); return 0; }
long ui_frame (long x, long y, long w, long h, long c)
{
	Buf *b = target ();
	fill_rect (b, x, y, w, 1, (unsigned) c); fill_rect (b, x, y + h - 1, w, 1, (unsigned) c);
	fill_rect (b, x, y + 1, 1, h - 2, (unsigned) c); fill_rect (b, x + w - 1, y + 1, 1, h - 2, (unsigned) c);
	return 0;
}
long ui_pixel (long x, long y, long c) { fill_rect (target (), x, y, 1, 1, (unsigned) c); return 0; }
long ui_clip (long x, long y, long w, long h)
{
	Buf *b = target ();
	if (!b) return 0;
	if (w <= 0) { b->clipOn = false; return 0; }
	b->clipOn = true; b->cx0 = (int) x; b->cy0 = (int) y; b->cx1 = (int) (x + w); b->cy1 = (int) (y + h);
	return 0;
}
long ui_rbox (long x, long y, long w, long h, long r, long top, long bottom)
{
	Buf *b = target ();
	if (w <= 0 || h <= 0) return 0;
	if (r <= 0 && top == bottom) { fill_rect (b, x, y, w, h, (unsigned) top); return 0; }
	cairo_t *cr = cr_begin (b);
	if (!cr) return 0;
	double rr = (double) r;
	if (rr * 2 > w) rr = w / 2.0;
	if (rr * 2 > h) rr = h / 2.0;
	cairo_new_sub_path (cr);
	cairo_arc (cr, x + w - rr, y + rr, rr, -M_PI / 2, 0);
	cairo_arc (cr, x + w - rr, y + h - rr, rr, 0, M_PI / 2);
	cairo_arc (cr, x + rr, y + h - rr, rr, M_PI / 2, M_PI);
	cairo_arc (cr, x + rr, y + rr, rr, M_PI, 3 * M_PI / 2);
	cairo_close_path (cr);
	if (top == bottom) cr_color (cr, (unsigned) top);
	else
	{
		cairo_pattern_t *p = cairo_pattern_create_linear (0, (double) y, 0, (double) (y + h));
		cairo_pattern_add_color_stop_rgb (p, 0, ((top >> 16) & 255) / 255.0, ((top >> 8) & 255) / 255.0, (top & 255) / 255.0);
		cairo_pattern_add_color_stop_rgb (p, 1, ((bottom >> 16) & 255) / 255.0, ((bottom >> 8) & 255) / 255.0, (bottom & 255) / 255.0);
		cairo_set_source (cr, p);
		cairo_pattern_destroy (p);
	}
	cairo_fill (cr);
	cr_end (b, cr);
	return 0;
}
long ui_vp_disc (long cx, long cy, long r, long c)
{
	Buf *b = target ();
	cairo_t *cr = cr_begin (b);
	if (!cr) return 0;
	cr_color (cr, (unsigned) c);
	cairo_arc (cr, cx / 16.0, cy / 16.0, r / 16.0, 0, 2 * M_PI);
	cairo_fill (cr);
	cr_end (b, cr);
	return 0;
}
long ui_vp_line (long x0, long y0, long x1, long y1, long th, long c)
{
	Buf *b = target ();
	cairo_t *cr = cr_begin (b);
	if (!cr) return 0;
	cr_color (cr, (unsigned) c);
	cairo_set_line_width (cr, th / 16.0);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to (cr, x0 / 16.0, y0 / 16.0);
	cairo_line_to (cr, x1 / 16.0, y1 / 16.0);
	cairo_stroke (cr);
	cr_end (b, cr);
	return 0;
}
long ui_vp_poly (long pts, long n, long c)
{
	Buf *b = target ();
	if (n < 3) return 0;
	cairo_t *cr = cr_begin (b);
	if (!cr) return 0;
	const int32_t *p = (const int32_t *) pts;
	cr_color (cr, (unsigned) c);
	cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
	cairo_move_to (cr, p[0] / 16.0, p[1] / 16.0);
	for (long i = 1; i < n; i++) cairo_line_to (cr, p[2 * i] / 16.0, p[2 * i + 1] / 16.0);
	cairo_close_path (cr);
	cairo_fill (cr);
	cr_end (b, cr);
	return 0;
}

static long text_draw (const char *s, long x, long base, long color, long px, long font, long maxw, bool fit)
{
	Buf *b = target ();
	if (!s || !*s) return x;
	int bl = 0;
	int w = lay_text (s, (int) px, font, fit && maxw > 0 ? (int) maxw : 0, &bl);
	if (fit && maxw > 0 && w > maxw) w = (int) maxw;
	cairo_t *cr = cr_begin (b);
	if (!cr) return x + w;
	if (!fit && maxw > 0) { cairo_rectangle (cr, (double) x, 0, (double) maxw, (double) b->h); cairo_clip (cr); }
	pango_cairo_update_context (cr, g_pctx);
	cr_color (cr, (unsigned) color);
	cairo_move_to (cr, (double) x, (double) (base - bl));
	pango_cairo_show_layout (cr, g_lay);
	cr_end (b, cr);
	return x + (maxw > 0 && w > maxw ? maxw : w);
}
long ui_text (long s, long x, long base, long c, long px, long font, long maxw) { return text_draw ((const char *) s, x, base, c, px, font, maxw, false); }
long ui_text_fit (long s, long x, long base, long c, long px, long font, long maxw) { return text_draw ((const char *) s, x, base, c, px, font, maxw, true); }
long ui_text_width (long s, long px, long font) { return s && *(const char *) s ? lay_text ((const char *) s, (int) px, font, 0, 0) : 0; }
long ui_text_wrap (long s, long x, long y, long w, long c, long font, long px, long measure)
{
	Buf *b = target ();
	if (!s) return 0;
	pango_layout_set_single_paragraph_mode (g_lay, FALSE);
	lay_text ((const char *) s, (int) px, font, 0, 0);
	pango_layout_set_width (g_lay, (int) w * PANGO_SCALE);
	pango_layout_set_wrap (g_lay, PANGO_WRAP_WORD_CHAR);
	PangoRectangle lg; pango_layout_get_extents (g_lay, 0, &lg);
	int hh = PANGO_PIXELS (lg.height);
	if (!measure)
	{
		cairo_t *cr = cr_begin (b);
		if (cr) { pango_cairo_update_context (cr, g_pctx); cr_color (cr, (unsigned) c); cairo_move_to (cr, (double) x, (double) y); pango_cairo_show_layout (cr, g_lay); cr_end (b, cr); }
	}
	pango_layout_set_single_paragraph_mode (g_lay, TRUE);
	pango_layout_set_width (g_lay, -1);
	return hh;
}
long ui_font_get (long which) { return which >= 0 && which < 10 ? (long) g_fonts[which] : (long) g_fonts[0]; }
long ui_font_italic (void) { return (long) g_fonts[UFN_ITALIC]; }

// les indications de manette : « A\tchoisir\nB\tretour » -> pastilles et mots
long ui_pad_hints (long text, long x, long y, long h, long measure)
{
	const char *s = (const char *) text;
	if (!s) return 0;
	long x0 = x;
	int px = h > 18 ? 13 : 11;
	while (*s)
	{
		const char *tab = strchr (s, '\t'), *nl = strchr (s, '\n');
		if (!nl) nl = s + strlen (s);
		std::string g = tab && tab < nl ? std::string (s, (size_t) (tab - s)) : std::string (s, (size_t) (nl - s));
		std::string word = tab && tab < nl ? std::string (tab + 1, (size_t) (nl - tab - 1)) : std::string ();
		int gw = lay_text (g.c_str (), px, (long) g_fonts[UFN_BOLD], 0, 0) + 10;
		if (gw < h) gw = (int) h;
		if (!measure)
		{
			ui_rbox (x, y, gw, h, h / 2, tok (TJ_RAISED), tok (TJ_RAISED_LOW));
			int tw2 = lay_text (g.c_str (), px, (long) g_fonts[UFN_BOLD], 0, 0);
			ui_text ((long) g.c_str (), x + (gw - tw2) / 2, y + h / 2 + px / 3 + 1, tok (TJ_TEXT1), px, (long) g_fonts[UFN_BOLD], 0);
		}
		x += gw + 4;
		if (!word.empty ())
		{
			int ww = lay_text (word.c_str (), px, (long) g_fonts[0], 0, 0);
			if (!measure) ui_text ((long) word.c_str (), x, y + h / 2 + px / 3 + 1, tok (TJ_TEXT2), px, (long) g_fonts[0], 0);
			x += ww + 12;
		}
		s = *nl ? nl + 1 : nl;
	}
	return x - x0;
}

// ---- les widgets ----
long ui_pushbutton (long x, long y, long w, long h, long text, long flags)
{
	GtkWidget *b = gtk_button_new ();
	GtkWidget *l = elabel (TRs ((const char *) text), 0.5f);
	gtk_button_set_child (GTK_BUTTON (b), l);
	gtk_widget_set_focus_on_click (b, FALSE);
	gtk_widget_set_focusable (b, FALSE);
	Wid *d = wid_new (K_BUTTON, x, y, w, h, b);
	if (!d) return 0;
	d->label = l; d->flags = flags; d->text = TRs ((const char *) text);
	button_style (d);
	g_signal_connect (b, "clicked", G_CALLBACK (on_clicked), d);
	return d->id;
}
long ui_label (long x, long y, long w, long h, long text, long flags)
{
	GtkWidget *l = gtk_label_new (TRs ((const char *) text));
	gtk_widget_set_can_target (l, FALSE);
	Wid *d = wid_new (K_LABEL, x, y, w, h, l);
	if (!d) return 0;
	d->flags = flags; d->text = TRs ((const char *) text);
	label_style (d);
	return d->id;
}
long ui_lcd (long x, long y, long w, long h, long text)
{
	GtkWidget *l = gtk_label_new ((const char *) text);
	gtk_widget_add_css_class (l, "k-lcd");
	gtk_label_set_xalign (GTK_LABEL (l), 0.5f);
	gtk_widget_set_can_target (l, FALSE);
	Wid *d = wid_new (K_LCD, x, y, w, h, l);
	if (!d) return 0;
	d->text = text ? (const char *) text : "";
	return d->id;
}
static long check_new (Kind k, long x, long y, long w, long h, long label, long v)
{
	GtkWidget *c = gtk_check_button_new_with_label (TRs ((const char *) label));
	gtk_widget_set_focus_on_click (c, FALSE);
	gtk_widget_set_focusable (c, FALSE);
	Wid *d = wid_new (k, x, y, w, h, c);
	if (!d) return 0;
	d->text = TRs ((const char *) label);
	if (k == K_RADIO)
	{
		d->radioGroup = (int) v;
		for (long id : d->win->wids)
		{
			Wid *o = wid (id);
			if (o && o != d && o->kind == K_RADIO && o->radioGroup == d->radioGroup) { gtk_check_button_set_group (GTK_CHECK_BUTTON (c), GTK_CHECK_BUTTON (o->gw)); break; }
		}
	}
	else { Quiet q; gtk_check_button_set_active (GTK_CHECK_BUTTON (c), v != 0); d->value = v != 0; }
	g_signal_connect (c, "toggled", G_CALLBACK (on_toggled), d);
	return d->id;
}
long ui_checkbox (long x, long y, long w, long h, long label, long v) { return check_new (K_CHECK, x, y, w, h, label, v); }
long ui_radio (long x, long y, long w, long h, long label, long group) { return check_new (K_RADIO, x, y, w, h, label, group); }
long ui_switch (long x, long y, long w, long h, long label, long v) { return check_new (K_CHECK, x, y, w, h, label, v); }

long ui_dropdown (long x, long y, long w, long h, long items)
{
	GtkStringList *sl = gtk_string_list_new (0);
	Wid tmp; (void) tmp;
	std::vector<std::string> it = items && *(const char *) items ? split_bar ((const char *) items) : std::vector<std::string> ();
	for (auto &s : it) gtk_string_list_append (sl, TRs (s.c_str ()));
	GtkWidget *dd = gtk_drop_down_new (G_LIST_MODEL (sl), 0);
	GtkListItemFactory *f = gtk_signal_list_item_factory_new ();
	g_signal_connect (f, "setup", G_CALLBACK (dd_setup), (gpointer) 1);
	g_signal_connect (f, "bind", G_CALLBACK (dd_bind), 0);
	GtkListItemFactory *lf = gtk_signal_list_item_factory_new ();
	g_signal_connect (lf, "setup", G_CALLBACK (dd_setup), 0);
	g_signal_connect (lf, "bind", G_CALLBACK (dd_bind), 0);
	gtk_drop_down_set_factory (GTK_DROP_DOWN (dd), f);
	gtk_drop_down_set_list_factory (GTK_DROP_DOWN (dd), lf);
	g_object_unref (f); g_object_unref (lf);
	gtk_widget_set_focus_on_click (dd, FALSE);
	Wid *d = wid_new (K_DROPDOWN, x, y, w, h, dd);
	if (!d) return 0;
	d->model = sl;
	for (auto &s : it) { d->items.push_back (TRs (s.c_str ())); d->itemData.push_back ((long) d->itemData.size ()); }
	d->value = it.empty () ? -1 : 0;
	{ Quiet q; gtk_drop_down_set_selected (GTK_DROP_DOWN (dd), it.empty () ? GTK_INVALID_LIST_POSITION : 0); }
	g_signal_connect (dd, "notify::selected", G_CALLBACK (on_dd), d);
	return d->id;
}
long ui_segmented (long x, long y, long w, long h, long items)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class (box, "linked");
	Wid *d = wid_new (K_SEGMENTED, x, y, w, h, box);
	if (!d) return 0;
	std::vector<std::string> it = split_bar ((const char *) items);
	GtkWidget *first = 0;
	for (auto &s : it)
	{
		GtkWidget *b = gtk_toggle_button_new ();
		gtk_button_set_child (GTK_BUTTON (b), elabel (TRs (s.c_str ()), 0.5f));
		gtk_widget_set_hexpand (b, TRUE);
		gtk_widget_set_focus_on_click (b, FALSE);
		gtk_widget_set_focusable (b, FALSE);
		if (first) gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (b), GTK_TOGGLE_BUTTON (first)); else first = b;
		gtk_box_append (GTK_BOX (box), b);
		d->parts.push_back (b);
		d->items.push_back (TRs (s.c_str ()));
	}
	{ Quiet q; if (first) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (first), TRUE); }
	d->value = 0;
	for (GtkWidget *b : d->parts) g_signal_connect (b, "toggled", G_CALLBACK (on_seg), d);
	return d->id;
}
long ui_textbox (long x, long y, long w, long h, long flags)
{
	GtkWidget *e = gtk_entry_new ();
	Wid *d = wid_new (K_TEXTBOX, x, y, w, h, e);
	if (!d) return 0;
	d->inner = e; d->flags = flags;
	if (flags & UTF_PASSWORD) gtk_entry_set_visibility (GTK_ENTRY (e), FALSE);
	if (flags & UTF_READONLY) gtk_editable_set_editable (GTK_EDITABLE (e), FALSE);
	if (flags & UTF_RIGHT) gtk_editable_set_alignment (GTK_EDITABLE (e), 1.0f);
	if (flags & UTF_MONO) gtk_widget_add_css_class (e, "monospace");
	if (flags & UTF_ERROR) gtk_widget_add_css_class (e, "error");
	g_signal_connect (e, "changed", G_CALLBACK (on_entry_changed), d);
	g_signal_connect (e, "activate", G_CALLBACK (on_entry_activate), d);
	return d->id;
}
long ui_textbox_hint (long w, long hint) { Wid *d = wid (w); if (d && d->kind == K_TEXTBOX) gtk_entry_set_placeholder_text (GTK_ENTRY (d->inner), TRs ((const char *) hint)); return 0; }
long ui_textarea (long x, long y, long w, long h, long flags)
{
	GtkWidget *sw = gtk_scrolled_window_new ();
	GtkWidget *tv = gtk_text_view_new ();
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (tv), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (tv), 6); gtk_text_view_set_right_margin (GTK_TEXT_VIEW (tv), 6);
	gtk_text_view_set_top_margin (GTK_TEXT_VIEW (tv), 4); gtk_text_view_set_bottom_margin (GTK_TEXT_VIEW (tv), 4);
	if (flags & UTF_READONLY) gtk_text_view_set_editable (GTK_TEXT_VIEW (tv), FALSE);
	if (flags & UTF_MONO) gtk_text_view_set_monospace (GTK_TEXT_VIEW (tv), TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), tv);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_add_css_class (sw, "frame");
	Wid *d = wid_new (K_TEXTAREA, x, y, w, h, sw);
	if (!d) return 0;
	d->inner = tv; d->flags = flags;
	g_signal_connect (gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv)), "changed", G_CALLBACK (on_buffer_changed), d);
	return d->id;
}
long ui_slider (long x, long y, long w, long h, long mn, long mx, long v)
{
	bool vert = h > w && w > 0;
	GtkWidget *s = gtk_scale_new_with_range (vert ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, (double) mn, (double) (mx > mn ? mx : mn + 1), 1);
	gtk_scale_set_draw_value (GTK_SCALE (s), FALSE);
	if (vert) gtk_range_set_inverted (GTK_RANGE (s), TRUE);
	gtk_widget_set_focus_on_click (s, FALSE);
	Wid *d = wid_new (K_SLIDER, x, y, w, h, s);
	if (!d) return 0;
	d->vmin = mn; d->vmax = mx; d->value = v;
	{ Quiet q; gtk_range_set_value (GTK_RANGE (s), (double) v); }
	g_signal_connect (s, "value-changed", G_CALLBACK (on_range), d);
	return d->id;
}
long ui_numeric (long x, long y, long w, long h, long mn, long mx, long v)
{
	GtkWidget *s = gtk_spin_button_new_with_range ((double) mn, (double) (mx > mn ? mx : mn), 1);
	gtk_spin_button_set_digits (GTK_SPIN_BUTTON (s), 0);
	gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (s), TRUE);
	gtk_editable_set_width_chars (GTK_EDITABLE (s), 2);
	Wid *d = wid_new (K_NUMERIC, x, y, w, h, s);
	if (!d) return 0;
	d->vmin = mn; d->vmax = mx; d->value = v;
	{ Quiet q; gtk_spin_button_set_value (GTK_SPIN_BUTTON (s), (double) v); }
	g_signal_connect (s, "value-changed", G_CALLBACK (on_spin), d);
	return d->id;
}
long ui_knob (long x, long y, long w, long h, long mn, long mx, long v)
{
	GtkWidget *a = gtk_drawing_area_new ();
	Wid *d = wid_new (K_KNOB, x, y, w, h, a);
	if (!d) return 0;
	d->vmin = mn; d->vmax = mx > mn ? mx : mn + 1; d->value = v;
	gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (a), knob_draw, d, 0);
	GtkGesture *g = gtk_gesture_drag_new ();
	g_signal_connect (g, "drag-begin", G_CALLBACK (knob_begin), d);
	g_signal_connect (g, "drag-update", G_CALLBACK (knob_update), d);
	gtk_widget_add_controller (a, GTK_EVENT_CONTROLLER (g));
	GtkEventController *sc = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
	g_signal_connect (sc, "scroll", G_CALLBACK (knob_scroll), d);
	gtk_widget_add_controller (a, sc);
	gtk_widget_set_cursor_from_name (a, "ns-resize");
	return d->id;
}
long ui_vumeter (long x, long y, long w, long h, long mx)
{
	GtkWidget *l = gtk_level_bar_new_for_interval (0, (double) (mx > 0 ? mx : 1));
	if (h > w) { gtk_orientable_set_orientation (GTK_ORIENTABLE (l), GTK_ORIENTATION_VERTICAL); gtk_level_bar_set_inverted (GTK_LEVEL_BAR (l), TRUE); }
	gtk_level_bar_remove_offset_value (GTK_LEVEL_BAR (l), GTK_LEVEL_BAR_OFFSET_LOW);
	gtk_level_bar_remove_offset_value (GTK_LEVEL_BAR (l), GTK_LEVEL_BAR_OFFSET_HIGH);
	gtk_level_bar_remove_offset_value (GTK_LEVEL_BAR (l), GTK_LEVEL_BAR_OFFSET_FULL);
	gtk_widget_set_can_target (l, FALSE);
	Wid *d = wid_new (K_VUMETER, x, y, w, h, l);
	if (!d) return 0;
	d->vmax = mx;
	return d->id;
}
long ui_vumeter_level (long w, long level)
{
	Wid *d = wid (w);
	if (!d || d->kind != K_VUMETER) return 0;
	d->value = level;
	gtk_level_bar_set_value (GTK_LEVEL_BAR (d->gw), (double) (level < 0 ? 0 : level > d->vmax ? d->vmax : level));
	return 0;
}
long ui_list (long x, long y, long w, long h, long flags)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *sw = gtk_scrolled_window_new ();
	GtkWidget *lb = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (lb), GTK_SELECTION_SINGLE);
	gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (lb), FALSE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), lb);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), (flags & UL_HSCROLL) ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (sw, TRUE);
	gtk_widget_add_css_class (box, "frame");
	if (flags & UL_SIDEBAR) gtk_widget_add_css_class (lb, "navigation-sidebar");
	Wid *d = wid_new (K_LIST, x, y, w, h, box);
	if (!d) return 0;
	if (flags & UL_HEADER)
	{
		d->header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_margin_start (d->header, 6); gtk_widget_set_margin_end (d->header, 6);
		gtk_widget_set_margin_top (d->header, 3); gtk_widget_set_margin_bottom (d->header, 3);
		gtk_box_append (GTK_BOX (box), d->header);
		gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
	}
	gtk_box_append (GTK_BOX (box), sw);
	d->inner = lb; d->flags = flags; d->value = -1;
	g_signal_connect (lb, "row-selected", G_CALLBACK (on_row_selected), d);
	g_signal_connect (lb, "row-activated", G_CALLBACK (on_row_activated), d);
	GtkGesture *g = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (g), 3);
	g_signal_connect (g, "pressed", G_CALLBACK (on_list_context), d);
	gtk_widget_add_controller (lb, GTK_EVENT_CONTROLLER (g));
	return d->id;
}
long ui_list_column (long w, long title, long width, long flags)
{
	Wid *d = wid (w);
	if (!d || d->kind != K_LIST) return -1;
	d->cols.push_back ({ title ? (const char *) title : "", (int) (width << 8 | (flags & 255)) });
	if (d->header)
	{
		GtkWidget *l = elabel (TRs ((const char *) title), (flags & ULC_RIGHT) ? 1.0f : 0.0f);
		gtk_widget_add_css_class (l, "dim-label");
		gtk_widget_set_size_request (l, (int) width, -1);
		gtk_box_append (GTK_BOX (d->header), l);
	}
	return (long) d->cols.size () - 1;
}
long ui_list_add (long w, long text, long data)
{
	Wid *d = wid (w);
	if (!d) return -1;
	const char *s = text ? (const char *) text : "";
	if (d->kind == K_DROPDOWN)
	{
		Quiet q;
		gtk_string_list_append (d->model, TRs (s));
		d->items.push_back (TRs (s)); d->itemData.push_back (data);
		return (long) d->items.size () - 1;
	}
	if (d->kind != K_LIST) return -1;
	Quiet q;
	gtk_list_box_append (GTK_LIST_BOX (d->inner), row_widget (d, s));
	d->items.push_back (s); d->itemData.push_back (data);
	return (long) d->items.size () - 1;
}
long ui_list_set (long w, long i, long text)
{
	Wid *d = wid (w);
	if (!d || i < 0 || i >= (long) d->items.size ()) return 0;
	const char *s = text ? (const char *) text : "";
	Quiet q;
	if (d->kind == K_DROPDOWN)
	{
		guint sel = gtk_drop_down_get_selected (GTK_DROP_DOWN (d->gw));
		const char *one[2] = { TRs (s), 0 };
		gtk_string_list_splice (d->model, (guint) i, 1, one);
		gtk_drop_down_set_selected (GTK_DROP_DOWN (d->gw), sel);
		d->items[(size_t) i] = TRs (s);
		return 0;
	}
	if (d->kind != K_LIST) return 0;
	d->items[(size_t) i] = s;
	GtkListBoxRow *r = gtk_list_box_get_row_at_index (GTK_LIST_BOX (d->inner), (int) i);
	if (r) gtk_list_box_row_set_child (r, row_widget (d, s));
	return 0;
}
long ui_list_clear (long w)
{
	Wid *d = wid (w);
	if (!d) return 0;
	Quiet q;
	if (d->kind == K_DROPDOWN)
	{
		gtk_string_list_splice (d->model, 0, g_list_model_get_n_items (G_LIST_MODEL (d->model)), 0);
		d->items.clear (); d->itemData.clear (); d->value = -1;
		return 0;
	}
	if (d->kind != K_LIST) return 0;
	gtk_list_box_remove_all (GTK_LIST_BOX (d->inner));
	d->items.clear (); d->itemData.clear (); d->value = -1;
	return 0;
}
long ui_list_count (long w) { Wid *d = wid (w); return d ? (long) d->items.size () : 0; }
long ui_list_get (long w, long i)
{
	Wid *d = wid (w);
	uikit__ret[1] = 0;
	if (!d || i < 0 || i >= (long) d->items.size ()) return 0;
	d->getbuf = d->items[(size_t) i];
	uikit__ret[1] = d->itemData[(size_t) i];
	return (long) d->getbuf.c_str ();
}
long ui_panel (long x, long y, long w, long h, long flags)
{
	GtkWidget *p = 0;
	if (flags & UPN_SURFACE) { p = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0); gtk_widget_add_css_class (p, "view"); gtk_widget_set_can_target (p, FALSE); }
	Wid *d = wid_new (K_PANEL, x, y, w, h, p);
	return d ? d->id : 0;
}
long ui_tabs (long x, long y, long w, long h, long items)
{
	GtkWidget *nb = gtk_notebook_new ();
	gtk_notebook_set_scrollable (GTK_NOTEBOOK (nb), TRUE);
	gtk_widget_set_focus_on_click (nb, FALSE);
	Wid *d = wid_new (K_TABS, x, y, w, h, nb);
	if (!d) return 0;
	std::vector<std::string> it = split_bar ((const char *) items);
	Quiet q;
	for (size_t i = 0; i < it.size (); i++)
	{
		gtk_notebook_append_page (GTK_NOTEBOOK (nb), gtk_box_new (GTK_ORIENTATION_VERTICAL, 0), gtk_label_new (TRs (it[i].c_str ())));
		Wid *p = wid_new (K_PAGE, x, y, w, h, 0);
		p->tabs = d->id; p->pageNo = (int) i;
		d->pages.push_back (p->id);
		d->items.push_back (TRs (it[i].c_str ()));
	}
	d->value = 0;
	g_signal_connect (nb, "switch-page", G_CALLBACK (on_switch_page), d);
	return d->id;
}
long ui_tabs_page (long t, long n)
{
	Wid *d = wid (t);
	return d && d->kind == K_TABS && n >= 0 && n < (long) d->pages.size () ? d->pages[(size_t) n] : 0;
}
long ui_view (long x, long y, long w, long h, long flags)
{
	Win *wn = cur ();
	if (!wn) return 0;
	Wid *tmp = new Wid;		// (le KSurf de la vue a besoin de son Wid avant d'etre place)
	tmp->win = wn;
	GtkWidget *s = ksurf_new (wn, tmp);
	delete tmp;
	Wid *d = wid_new (K_VIEW, x, y, w, h, s);
	((KSurf *) s)->view = d; ((KSurf *) s)->buf = &d->buf;
	d->flags = flags;
	if (flags & UVW_FOCUS) gtk_widget_set_focusable (s, TRUE);
	d->buf.alloc ((int) (w > 0 ? w : 1), (int) (h > 0 ? h : 1), tok (TJ_SURFACE));
	return d->id;
}
long ui_view_on (long v, long paint, long mouse, long key, long wheel)
{
	Wid *d = wid (v);
	if (!d) return 0;
	d->paint = paint; d->mouse = mouse; d->key = key; d->wheel = wheel; d->dirty = true;
	return 0;
}
long ui_view_dirty (long v) { Wid *d = wid (v); if (d) d->dirty = true; return 0; }
long ui_view_flush (void)
{
	std::vector<Wid *> todo;
	for (auto &p : g_wids) if ((p.second->kind == K_VIEW) && p.second->dirty) todo.push_back (p.second);
	for (Wid *v : todo) if (wid (v->id)) view_paint (v);
	return 0;
}
long ui_canvas (long x, long y, long w, long h)
{
	Win *wn = cur ();
	if (!wn) return 0;
	Wid *tmp = new Wid; tmp->win = wn;
	GtkWidget *s = ksurf_new (wn, tmp);
	delete tmp;
	Wid *d = wid_new (K_CANVAS, x, y, w, h, s);
	((KSurf *) s)->view = d; ((KSurf *) s)->buf = &d->buf;
	gtk_widget_set_can_target (s, FALSE);
	d->buf.alloc ((int) (w > 0 ? w : 1), (int) (h > 0 ? h : 1), 0xFFFFFF);
	return d->id;
}
long ui_canvas_pixels (long c)
{
	Wid *d = wid (c);
	if (!d || (d->kind != K_CANVAS && d->kind != K_VIEW)) { uikit__ret[1] = uikit__ret[2] = 0; return 0; }
	uikit__ret[0] = (long) d->buf.px; uikit__ret[1] = d->buf.w; uikit__ret[2] = d->buf.h;
	return (long) d->buf.px;
}
long ui_canvas_begin (long c)
{
	Wid *d = wid (c);
	if (!d) return 0;
	g_targetSaved = g_target;
	g_target = &d->buf;
	d->buf.clipOn = false;
	return 0;
}
long ui_canvas_end (void)
{
	if (g_target) { g_target->clipOn = false; g_target->texDirty = true; }
	g_target = g_targetSaved; g_targetSaved = 0;
	return 0;
}

// ---- les fonctions communes ----
long ui_w_draw (long w)
{
	Wid *d = wid (w);
	if (d && (d->kind == K_VIEW || d->kind == K_CANVAS)) redraw (&d->buf, d->gw);
	if (!w) { Win *n = cur (); if (n) redraw (&n->buf, n->surf); }
	return 0;
}
long ui_w_move (long w, long x, long y, long ww, long hh)
{
	Wid *d = wid (w);
	if (!d) return 0;
	d->x = (int) x; d->y = (int) y; d->w = (int) ww; d->h = (int) hh;
	if ((d->kind == K_SLIDER || d->kind == K_VUMETER) && ww > 0 && hh > 0)
	{
		// (plus haut que large : vertical, comme a la creation)
		GtkOrientation o = hh > ww ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
		if (gtk_orientable_get_orientation (GTK_ORIENTABLE (d->gw)) != o)
		{
			gtk_orientable_set_orientation (GTK_ORIENTABLE (d->gw), o);
			if (d->kind == K_SLIDER) gtk_range_set_inverted (GTK_RANGE (d->gw), o == GTK_ORIENTATION_VERTICAL);
			else gtk_level_bar_set_inverted (GTK_LEVEL_BAR (d->gw), o == GTK_ORIENTATION_VERTICAL);
		}
	}
	if ((d->kind == K_VIEW || d->kind == K_CANVAS) && ww > 0 && hh > 0 && (ww != d->buf.w || hh != d->buf.h))
	{
		d->buf.alloc ((int) ww, (int) hh, d->kind == K_VIEW ? tok (TJ_SURFACE) : 0xFFFFFF);
		d->dirty = true;
	}
	gtk_widget_queue_allocate (d->win->surf);
	return 0;
}
long ui_w_show (long w, long on)
{
	Wid *d = wid (w);
	if (!d) return 0;
	d->shown = on != 0;
	refresh_visibility (d->win);
	return 0;
}
long ui_w_enable (long w, long on)
{
	Wid *d = wid (w);
	if (!d) return 0;
	d->enabled = on != 0;
	if (d->gw) gtk_widget_set_sensitive (d->gw, d->enabled);
	return 0;
}
long ui_w_set_parent (long w, long p)
{
	Wid *d = wid (w);
	if (!d) return 0;
	d->parent = p;
	refresh_visibility (d->win);
	return 0;
}
long ui_w_focus (long w)
{
	Wid *d = wid (w);
	if (!w || !d) { Win *n = cur (); if (n) gtk_widget_grab_focus (n->surf); return 0; }
	if (d->inner && (d->kind == K_TEXTBOX || d->kind == K_TEXTAREA || d->kind == K_LIST)) gtk_widget_grab_focus (d->inner);
	else if (d->gw) gtk_widget_grab_focus (d->gw);
	if (d->kind == K_VIEW) g_focusView = d->id;
	return 0;
}
long ui_w_on (long w, long fn, long data) { Wid *d = wid (w); if (d) { d->fn = fn; d->data = data; } return 0; }
long ui_w_user (long w) { Wid *d = wid (w); return d ? d->data : 0; }
long ui_w_tip (long w, long text)
{
	Wid *d = wid (w);
	if (d && d->gw) gtk_widget_set_tooltip_text (d->gw, text ? TRs ((const char *) text) : 0);
	return 0;
}
long ui_w_destroy (long w) { Wid *d = wid (w); if (d) { Win *n = d->win; wid_free (d); gtk_widget_queue_allocate (n->surf); } return 0; }
long ui_w_style (long w, long flags)
{
	Wid *d = wid (w);
	if (!d) return 0;
	d->flags = flags;
	if (d->kind == K_BUTTON) button_style (d);
	else if (d->kind == K_LABEL) label_style (d);
	else if (d->kind == K_TEXTBOX)
	{
		if (flags & UTF_ERROR) gtk_widget_add_css_class (d->gw, "error"); else gtk_widget_remove_css_class (d->gw, "error");
		gtk_entry_set_visibility (GTK_ENTRY (d->gw), !(flags & UTF_PASSWORD));
	}
	return 0;
}
long ui_w_get_value (long w)
{
	Wid *d = wid (w);
	if (!d) return 0;
	switch (d->kind)
	{
	case K_CHECK: case K_RADIO: return gtk_check_button_get_active (GTK_CHECK_BUTTON (d->gw));
	case K_DROPDOWN: { guint s = gtk_drop_down_get_selected (GTK_DROP_DOWN (d->gw)); return s == GTK_INVALID_LIST_POSITION ? -1 : (long) s; }
	case K_SLIDER: return (long) lround (gtk_range_get_value (GTK_RANGE (d->gw)));
	case K_NUMERIC: return gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (d->gw));
	case K_LIST: { GtkListBoxRow *r = gtk_list_box_get_selected_row (GTK_LIST_BOX (d->inner)); return r ? gtk_list_box_row_get_index (r) : -1; }
	case K_TEXTBOX: return (long) strlen (gtk_editable_get_text (GTK_EDITABLE (d->gw)));
	default: return d->value;
	}
}
long ui_w_set_value (long w, long v)
{
	Wid *d = wid (w);
	if (!d) return 0;
	Quiet q;
	switch (d->kind)
	{
	case K_CHECK: case K_RADIO: gtk_check_button_set_active (GTK_CHECK_BUTTON (d->gw), v != 0); d->value = v != 0; break;
	case K_DROPDOWN:
		gtk_drop_down_set_selected (GTK_DROP_DOWN (d->gw), v < 0 || v >= (long) d->items.size () ? GTK_INVALID_LIST_POSITION : (guint) v);
		d->value = v;
		break;
	case K_SEGMENTED:
		if (v >= 0 && v < (long) d->parts.size ()) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->parts[(size_t) v]), TRUE);
		d->value = v;
		break;
	case K_SLIDER: d->value = v; gtk_range_set_value (GTK_RANGE (d->gw), (double) v); break;
	case K_NUMERIC: d->value = v; gtk_spin_button_set_value (GTK_SPIN_BUTTON (d->gw), (double) v); break;
	case K_KNOB: d->value = v < d->vmin ? d->vmin : v > d->vmax ? d->vmax : v; gtk_widget_queue_draw (d->gw); break;
	case K_VUMETER: ui_vumeter_level (w, v); break;
	case K_LIST:
		if (v < 0 || v >= (long) d->items.size ()) gtk_list_box_unselect_all (GTK_LIST_BOX (d->inner));
		else
		{
			gtk_list_box_select_row (GTK_LIST_BOX (d->inner), gtk_list_box_get_row_at_index (GTK_LIST_BOX (d->inner), (int) v));
			g_idle_add (list_scroll_idle, (gpointer) d->id);
		}
		d->value = v;
		break;
	case K_TABS:
		d->value = v;
		gtk_notebook_set_current_page (GTK_NOTEBOOK (d->gw), (int) v);
		refresh_visibility (d->win);
		break;
	default: d->value = v;
	}
	return 0;
}
long ui_w_set_range (long w, long mn, long mx, long page, long step)
{
	Wid *d = wid (w);
	if (!d) return 0;
	(void) page;
	Quiet q;
	switch (d->kind)
	{
	case K_SLIDER: d->vmin = mn; d->vmax = mx; gtk_range_set_range (GTK_RANGE (d->gw), (double) mn, (double) (mx > mn ? mx : mn + 1)); if (step > 0) gtk_range_set_increments (GTK_RANGE (d->gw), (double) step, (double) step * 10); break;
	case K_NUMERIC: d->vmin = mn; d->vmax = mx; gtk_spin_button_set_range (GTK_SPIN_BUTTON (d->gw), (double) mn, (double) mx); break;
	case K_KNOB: d->vmin = mn; d->vmax = mx > mn ? mx : mn + 1; gtk_widget_queue_draw (d->gw); break;
	case K_VUMETER: d->vmax = mx; gtk_level_bar_set_max_value (GTK_LEVEL_BAR (d->gw), (double) (mx > 0 ? mx : 1)); break;
	case K_TEXTBOX: gtk_entry_set_max_length (GTK_ENTRY (d->gw), (int) mx); break;
	default: break;
	}
	return 0;
}
long ui_w_set_text (long w, long text)
{
	Wid *d = wid (w);
	if (!d) return 0;
	const char *s = text ? (const char *) text : "";
	Quiet q;
	switch (d->kind)
	{
	case K_LABEL: d->text = TRs (s); gtk_label_set_text (GTK_LABEL (d->gw), d->text.c_str ()); break;
	case K_LCD: d->text = s; gtk_label_set_text (GTK_LABEL (d->gw), s); break;
	case K_BUTTON: d->text = TRs (s); gtk_label_set_text (GTK_LABEL (d->label), d->text.c_str ()); break;
	case K_CHECK: case K_RADIO: d->text = TRs (s); gtk_check_button_set_label (GTK_CHECK_BUTTON (d->gw), d->text.c_str ()); break;
	case K_TEXTBOX: gtk_editable_set_text (GTK_EDITABLE (d->gw), s); break;
	case K_TEXTAREA: gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (d->inner)), s, -1); break;
	default: d->text = s;
	}
	return 0;
}
long ui_w_get_text (long w)
{
	Wid *d = wid (w);
	if (!d) return (long) "";
	switch (d->kind)
	{
	case K_TEXTBOX: d->getbuf = gtk_editable_get_text (GTK_EDITABLE (d->gw)); break;
	case K_TEXTAREA:
	{
		GtkTextBuffer *b = gtk_text_view_get_buffer (GTK_TEXT_VIEW (d->inner));
		GtkTextIter s, e;
		gtk_text_buffer_get_bounds (b, &s, &e);
		char *t = gtk_text_buffer_get_text (b, &s, &e, FALSE);
		d->getbuf = t ? t : "";
		g_free (t);
		break;
	}
	case K_DROPDOWN: case K_LIST: d->getbuf = list_text (d, ui_w_get_value (w)); break;
	case K_SEGMENTED: d->getbuf = list_text (d, d->value); break;
	default: d->getbuf = d->text;
	}
	return (long) d->getbuf.c_str ();
}
long ui_w_text_len (long w) { return (long) strlen ((const char *) ui_w_get_text (w)); }

// ---- les dialogues ----
long ui_message (long title, long text, long kind)
{
	const char *t = TRs ((const char *) text);
	GtkAlertDialog *a = gtk_alert_dialog_new ("%s", title ? TRs ((const char *) title) : "");
	gtk_alert_dialog_set_detail (a, t);
	gtk_alert_dialog_set_modal (a, TRUE);
	static const char *const OK[] = { "OK", 0 };
	static const char *const OKC[] = { "Annuler", "OK", 0 };
	static const char *const YN[] = { "Non", "Oui", 0 };
	static const char *const YNC[] = { "Annuler", "Non", "Oui", 0 };
	const char *const *b = kind == UM_OKCANCEL ? OKC : kind == UM_YESNO ? YN : kind == UM_YESNOCANCEL ? YNC : OK;
	int n = 0; while (b[n]) n++;
	gtk_alert_dialog_set_buttons (a, b);
	gtk_alert_dialog_set_cancel_button (a, 0);
	gtk_alert_dialog_set_default_button (a, n - 1);
	AlertRes r = { false, -1 };
	gtk_alert_dialog_choose (a, parent_win (), 0, alert_cb, &r);
	run_until (&r.done);
	g_object_unref (a);
	if (kind == UM_OK) return 1;
	if (kind == UM_YESNOCANCEL) return r.r == 2 ? 1 : r.r == 1 ? 2 : 0;
	return r.r == 1 ? 1 : 0;
}

struct AskState { bool done; int r; GtkWidget *win, *entry; };
static void ask_ok (GtkWidget *, gpointer d) { AskState *s = (AskState *) d; s->r = 1; s->done = true; }
static void ask_cancel (GtkWidget *, gpointer d) { AskState *s = (AskState *) d; s->r = 0; s->done = true; }
static gboolean ask_close (GtkWindow *, gpointer d) { AskState *s = (AskState *) d; s->r = 0; s->done = true; return TRUE; }
static gboolean ask_key (GtkEventControllerKey *, guint kv, guint, GdkModifierType, gpointer d)
{
	if (kv == GDK_KEY_Escape) { ask_cancel (0, d); return TRUE; }
	return FALSE;
}
long ui_ask (long title, long question, long buf, long cap)
{
	AskState s = { false, 0, 0, 0 };
	s.win = gtk_window_new ();
	gtk_widget_add_css_class (s.win, "kport");
	gtk_window_set_title (GTK_WINDOW (s.win), title ? TRs ((const char *) title) : "");
	gtk_window_set_modal (GTK_WINDOW (s.win), TRUE);
	if (parent_win ()) gtk_window_set_transient_for (GTK_WINDOW (s.win), parent_win ());
	gtk_window_set_default_size (GTK_WINDOW (s.win), 420, -1);
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_start (box, 16); gtk_widget_set_margin_end (box, 16); gtk_widget_set_margin_top (box, 14); gtk_widget_set_margin_bottom (box, 14);
	GtkWidget *q = gtk_label_new (question ? TRs ((const char *) question) : "");
	gtk_label_set_xalign (GTK_LABEL (q), 0); gtk_label_set_wrap (GTK_LABEL (q), TRUE);
	gtk_box_append (GTK_BOX (box), q);
	s.entry = gtk_entry_new ();
	gtk_editable_set_text (GTK_EDITABLE (s.entry), buf ? (const char *) buf : "");
	gtk_box_append (GTK_BOX (box), s.entry);
	GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign (row, GTK_ALIGN_END);
	GtkWidget *c = gtk_button_new_with_label ("Annuler"), *o = gtk_button_new_with_label ("OK");
	gtk_widget_add_css_class (o, "suggested-action");
	gtk_box_append (GTK_BOX (row), c); gtk_box_append (GTK_BOX (row), o);
	gtk_box_append (GTK_BOX (box), row);
	gtk_window_set_child (GTK_WINDOW (s.win), box);
	g_signal_connect (o, "clicked", G_CALLBACK (ask_ok), &s);
	g_signal_connect (c, "clicked", G_CALLBACK (ask_cancel), &s);
	g_signal_connect (s.entry, "activate", G_CALLBACK (ask_ok), &s);
	g_signal_connect (s.win, "close-request", G_CALLBACK (ask_close), &s);
	GtkEventController *k = gtk_event_controller_key_new ();
	g_signal_connect (k, "key-pressed", G_CALLBACK (ask_key), &s);
	gtk_widget_add_controller (s.win, k);
	gtk_window_present (GTK_WINDOW (s.win));
	gtk_widget_grab_focus (s.entry);
	run_until (&s.done);
	if (s.r && buf && cap > 0) snprintf ((char *) buf, (size_t) cap, "%s", gtk_editable_get_text (GTK_EDITABLE (s.entry)));
	gtk_window_destroy (GTK_WINDOW (s.win));
	return s.r;
}

long ui_file_filter (long pats) { g_filter = pats ? (const char *) pats : ""; return 0; }
long ui_file_open (long dir, long buf, long cap) { return file_dialog (0, dir, 0, buf, cap); }
long ui_file_save (long dir, long name, long buf, long cap) { return file_dialog (1, dir, name, buf, cap); }
long ui_folder_choose (long dir, long buf, long cap) { return file_dialog (2, dir, 0, buf, cap); }

struct ColorRes { bool done; GdkRGBA *c; };
static void color_cb (GObject *src, GAsyncResult *res, gpointer data)
{
	ColorRes *r = (ColorRes *) data;
	r->c = gtk_color_dialog_choose_rgba_finish (GTK_COLOR_DIALOG (src), res, 0);
	r->done = true;
}
long ui_color_dialog (long title, long color)
{
	GtkColorDialog *d = gtk_color_dialog_new ();
	gtk_color_dialog_set_title (d, title ? TRs ((const char *) title) : "Couleur");
	gtk_color_dialog_set_with_alpha (d, FALSE);
	GdkRGBA init = rgba ((unsigned) color);
	ColorRes r = { false, 0 };
	gtk_color_dialog_choose_rgba (d, parent_win (), &init, 0, color_cb, &r);
	run_until (&r.done);
	g_object_unref (d);
	if (!r.c) return -1;
	long c = (long) lround (r.c->red * 255) << 16 | (long) lround (r.c->green * 255) << 8 | (long) lround (r.c->blue * 255);
	gdk_rgba_free (r.c);
	return c;
}

long ui_dialog_begin (long w, long h, long title)
{
	Win *n = win_new (-1, -1, w, h, TRs ((const char *) title), 0, true);
	return n->id;
}
long ui_dialog_end (long d)
{
	Win *w = win (d);
	if (!w) return 0;
	long prev = w->prevCurrent;
	win_destroy (w);
	g_cur = win (prev) ? prev : (g_wins.empty () ? 0 : g_wins.begin ()->first);
	use_vars (cur ());
	return 0;
}

// ---- les dialogues de l'app (uk_dialog.c) ----
// -> le bord droit du bouton suivant ; le bouton : en bas a droite, sa largeur selon le texte (comme ui_pushbutton)
long ui_dialog_button (long dlg, long n, long text, long resp, long style, long right)
{
	Win *w = win (dlg);
	if (!w || n < 0 || n > 2) return right;
	long keep = g_cur;
	g_cur = w->id; use_vars (w);
	long ctl = ui_metric (UMT_CTL);
	const char *t = TRs ((const char *) text);
	long bw = ui_text_width ((long) t, ui_metric (UMT_PX), (long) g_fonts[UFN_SANS]) + 28;
	if (bw < 80) bw = 80;
	long b = ui_pushbutton (right - bw, w->reqh - ctl - 12, bw, ctl, (long) t, style);
	g_cur = keep; use_vars (cur ());
	w->dlgBtn[n] = b; w->dlgResp[n] = resp;
	if (style & UB_DEFAULT) w->dlgDef = b;
	return right - bw - 8;
}
long ui_dialog_close (long dlg, long res) { Win *w = win (dlg); if (w) { w->dlgRes = res; w->dlgDone = true; } return dlg; }
long ui_dialog_cancel (long dlg, long res) { Win *w = win (dlg); if (w) w->dlgCancel = res; return dlg; }
long ui_dialog_timer (long dlg, long fn, long ms) { Win *w = win (dlg); if (w) { w->dlgTick = fn; w->dlgTickMs = ms; } return dlg; }
static void dlg_buttons (Win *w, long wd, long code)
{
	if (code != UN_ACTIVATE || !wd) return;
	for (int i = 0; i < 3; i++) if (w->dlgBtn[i] == wd) { w->dlgRes = w->dlgResp[i]; w->dlgDone = true; return; }
}
// la boucle du dialogue : ses evenements seuls (la fenetre d'avant attend, GTK la bloque) ; un bouton donne sa reponse,
// Echap ou la case de fermeture celle de ui_dialog_cancel, Entree non prise le bouton par defaut
long ui_dialog_run (long dlg, long fn, long)
{
	Win *w = win (dlg);
	if (!w) return 0;
	w->dlgDone = false;
	ui_w_draw (0);
	for (;;)
	{
		if (!win (dlg)) return 0;
		if (w->dlgDone) break;
		long ms = 30000;
		if (w->dlgTick)
		{
			((long (*) (long)) w->dlgTick) (dlg);
			if (w->dlgDone) break;
			ms = w->dlgTickMs > 0 ? w->dlgTickMs : 30000;
		}
		long type = ui_event (ms);
		if (!type) continue;
		if (g_ev.win != (int) w->id)
		{
			if (type == WEV_RESIZE) ui_w_event (0);
			continue;
		}
		if (type == WEV_CLOSE) { w->dlgRes = w->dlgCancel; break; }
		long wd = ui_w_event (0), code = uikit__ret[1], taken = uikit__ret[2];
		if (code)
		{
			dlg_buttons (w, wd, code);
			if (fn) ((long (*) (long, long, long)) fn) (dlg, wd, code);
			continue;
		}
		if (taken || type != WEV_KEY || !g_ev.kind) continue;
		if (g_ev.code == 0x29) { w->dlgRes = w->dlgCancel; break; }
		if ((g_ev.code == 0x28 || g_ev.code == 0x58) && w->dlgDef)
		{
			dlg_buttons (w, w->dlgDef, UN_ACTIVATE);
			if (fn) ((long (*) (long, long, long)) fn) (dlg, w->dlgDef, UN_ACTIVATE);
		}
	}
	return w->dlgRes;
}

// ---- ce que Budget demande de plus ----
// les metriques de l'echelle « petite » de UIKit (uk_widgets.c : px, petite, ligne, controle, barre, en-tete, menu)
long ui_metric (long i)
{
	static const long MT[7] = { 13, 12, 26, 28, 10, 22, 24 };
	if (i == UMT_TOUCH) return 0;
	return i >= 0 && i < 7 ? MT[i] : -1;
}
long ui_text_prefix (long s, long n, long px, long font)
{
	const char *t = (const char *) s;
	if (!t) return 0;
	size_t len = strlen (t);
	if (n >= 0 && (size_t) n < len) len = (size_t) n;
	if (!len) return 0;
	std::string part (t, len);
	return lay_text (part.c_str (), (int) px, font, 0, 0);
}
long ui_textbox_select (long w, long a, long b)
{
	Wid *d = wid (w);
	if (d && d->kind == K_TEXTBOX) gtk_editable_select_region (GTK_EDITABLE (d->gw), (int) a, (int) b);
	return 0;
}
// le widget de la fenetre courante qui a le clavier (0 : aucun)
long ui_w_get_focus (void)
{
	Win *w = cur ();
	if (!w) return 0;
	GtkWidget *f = gtk_root_get_focus (GTK_ROOT (w->gw));
	for (GtkWidget *x = f; x; x = gtk_widget_get_parent (x))
	{
		Wid *d = (Wid *) g_object_get_data (G_OBJECT (x), "kw");
		if (d && wid (d->id) == d && d->win == w) return d->id;
	}
	return 0;
}
long ui_lang (void)
{
	const char *l = getenv ("LANGUAGE");
	if (!l || !*l) l = getenv ("LC_ALL");
	if (!l || !*l) l = getenv ("LC_MESSAGES");
	if (!l || !*l) l = getenv ("LANG");
	return (long) (l && *l && strncmp (l, "fr", 2) && strncmp (l, "C", 1) ? "en" : "fr");
}
long ui_tr_load (long path)
{
	char tb[PATH_MAX];
	return tr_load_file (kport_path ((const char *) path, tb, sizeof tb)) ? 0 : -2;
}
// ouvrir un fichier ou un dossier avec l'app du systeme (Fichier > Dossier des donnees de Budget)
long ui_assoc_open (long path) { return kport_open_external ((const char *) path) ? 0 : -2; }
void kport_tr_add (const char *from, const char *to)
{
	if (!from || !to) return;
	g_trPort.push_back ({ from, to });
	g_tr[from] = to;
}

struct PopState { bool done; int r; GtkWidget *pop; };
static void pop_closed (GtkPopover *, gpointer d) { ((PopState *) d)->done = true; }
static void pop_item (GtkButton *b, gpointer d)
{
	PopState *s = (PopState *) d;
	s->r = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (b), "idx"));
	gtk_popover_popdown (GTK_POPOVER (s->pop));
}
long ui_popup_local (long x, long y, long labels, long width)
{
	Win *w = cur ();
	if (!w || !labels) return -1;
	PopState s = { false, -1, 0 };
	s.pop = gtk_popover_new ();
	gtk_popover_set_has_arrow (GTK_POPOVER (s.pop), FALSE);
	gtk_popover_set_position (GTK_POPOVER (s.pop), GTK_POS_BOTTOM);
	gtk_widget_set_halign (s.pop, GTK_ALIGN_START);
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class (box, "k-menu");
	if (width > 0) gtk_widget_set_size_request (box, (int) width, -1);
	const char *p = (const char *) labels;
	for (int i = 0; *p; i++, p += strlen (p) + 1)
	{
		if (!strcmp (p, "-")) { gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL)); continue; }
		bool dis = (unsigned char) p[0] == 1, on = (unsigned char) p[0] == 2;	// (0x01 : grisee, 0x02 : cochee)
		GtkWidget *b = gtk_button_new ();
		std::string lb = on ? std::string ("\xE2\x9C\x93 ") + TRs (p + 1) : std::string (TRs (dis ? p + 1 : p));
		gtk_button_set_child (GTK_BUTTON (b), elabel (lb.c_str (), 0));
		gtk_widget_add_css_class (b, "flat");
		gtk_widget_set_sensitive (b, !dis);
		g_object_set_data (G_OBJECT (b), "idx", GINT_TO_POINTER (i));
		g_signal_connect (b, "clicked", G_CALLBACK (pop_item), &s);
		gtk_box_append (GTK_BOX (box), b);
	}
	gtk_popover_set_child (GTK_POPOVER (s.pop), box);
	gtk_widget_set_parent (s.pop, w->surf);
	GdkRectangle r = { (int) x, (int) y, 1, 1 };
	gtk_popover_set_pointing_to (GTK_POPOVER (s.pop), &r);
	g_signal_connect (s.pop, "closed", G_CALLBACK (pop_closed), &s);
	gtk_popover_popup (GTK_POPOVER (s.pop));
	run_until (&s.done);
	g_signal_handlers_disconnect_by_data (s.pop, &s);
	gtk_widget_unparent (s.pop);
	w->held = 0;
	return s.r;
}

// ---- le presse-papiers, le glisser-deposer ----
long ui_clip_set (long text, long n)
{
	GdkClipboard *c = gdk_display_get_clipboard (gdk_display_get_default ());
	std::string s = n < 0 ? std::string ((const char *) text) : std::string ((const char *) text, (size_t) n);
	gdk_clipboard_set_text (c, s.c_str ());
	return 0;
}
struct ClipRes { bool done; char *t; };
static void clip_cb (GObject *src, GAsyncResult *res, gpointer data)
{
	ClipRes *r = (ClipRes *) data;
	r->t = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (src), res, 0);
	r->done = true;
}
long ui_clip_get (long buf, long cap)
{
	GdkClipboard *c = gdk_display_get_clipboard (gdk_display_get_default ());
	ClipRes r = { false, 0 };
	gdk_clipboard_read_text_async (c, 0, clip_cb, &r);
	gint64 end = g_get_monotonic_time () + 2000000;
	while (!r.done && g_get_monotonic_time () < end) g_main_context_iteration (0, FALSE);
	if (!r.done || !r.t) { if (cap > 0) ((char *) buf)[0] = 0; return 0; }
	snprintf ((char *) buf, (size_t) cap, "%s", r.t);
	long n = (long) strlen ((char *) buf);
	g_free (r.t);
	return n;
}
long ui_drop_get (long buf, long cap)
{
	long n = (long) g_dropData.size ();
	if (n > cap) n = cap;
	memcpy ((void *) buf, g_dropData.data (), (size_t) n);
	uikit__ret[0] = n; uikit__ret[1] = g_dropKind;
	return n;
}

// ---- les images ----
struct Img { int w, h; uint32_t *px; };
long ui_image_decode (long data, long size)
{
	GdkPixbufLoader *l = gdk_pixbuf_loader_new ();
	bool ok = gdk_pixbuf_loader_write (l, (const guchar *) data, (gsize) size, 0);
	ok = gdk_pixbuf_loader_close (l, 0) && ok;
	GdkPixbuf *pb = ok ? gdk_pixbuf_loader_get_pixbuf (l) : 0;
	if (!pb) { g_object_unref (l); return 0; }
	Img *im = new Img;
	im->w = gdk_pixbuf_get_width (pb); im->h = gdk_pixbuf_get_height (pb);
	im->px = (uint32_t *) malloc ((size_t) im->w * (size_t) im->h * 4);
	int rs = gdk_pixbuf_get_rowstride (pb), nc = gdk_pixbuf_get_n_channels (pb);
	bool alpha = gdk_pixbuf_get_has_alpha (pb);
	const guchar *src = gdk_pixbuf_read_pixels (pb);
	for (int y = 0; y < im->h; y++)
		for (int x = 0; x < im->w; x++)
		{
			const guchar *s = src + (long) y * rs + x * nc;
			unsigned a = alpha ? s[3] : 255;
			im->px[(long) y * im->w + x] = a << 24 | (unsigned) s[0] << 16 | (unsigned) s[1] << 8 | s[2];
		}
	g_object_unref (l);
	return (long) im;
}
long ui_image_load (long path)
{
	char tb[PATH_MAX];
	gchar *data = 0; gsize n = 0;
	if (!g_file_get_contents (kport_path ((const char *) path, tb, sizeof tb), &data, &n, 0)) return 0;
	long r = ui_image_decode ((long) data, (long) n);
	g_free (data);
	return r;
}
long ui_image_size (long img)
{
	Img *im = (Img *) img;
	if (!im) { uikit__ret[1] = uikit__ret[2] = 0; return 0; }
	uikit__ret[0] = im->w; uikit__ret[1] = im->h; uikit__ret[2] = (long) im->px;
	return im->w;
}
long ui_image_free (long img) { Img *im = (Img *) img; if (im) { free (im->px); delete im; } return 0; }

} // extern "C"
