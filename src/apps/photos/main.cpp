//
// main.cpp -- Photos, le port d'user/Apps/photos d'Onyx (docs/photos/README.md d'Onyx) sur l'UIKit d'ARMPI, sans pont : toutes
// les images de SD0:/home/pictures, du DCIM de chaque volume et des dossiers ajoutes a la main :
//   * les photos par jour, les plus recentes d'abord (EXIF, sinon le nom du fichier), selection, favoris, la frise des
//     annees, la taille des vignettes, la recherche (nom, date, appareil, album, description) ;
//   * a gauche Toutes les photos, Favoris, Ajouts recents, les albums, les dossiers suivis ;
//   * une photo en grand (viewer.h), l'edition (editor.h), Mail, Paint, le PDF, le diaporama (share.h).
// La bibliotheque vit dans SD0:/home/photos ; les dossiers sont relus par un fil (lib.h), les vignettes faites par un autre
// (thumbs.h). « photos SD0:/x.jpg » ouvre cette photo.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <initializer_list>
#include <archivekit_abi.h>
#include "share.h"

namespace photos {

static int g_mode;				// 0 la bibliotheque, 1 une photo, 2 l'edition
static bool g_paintAll;
static void show_part (long w, bool on) { ui_w_show (w, on); }
static void layout_parts ()
{
	int W = (int) ui.w, H = (int) ui.h;
	bool lib = g_mode == 0;
	show_part (g_vViewer, g_mode == 1); show_part (g_vEditor, g_mode == 2);
	show_part (g_vTb, lib); show_part (g_side, lib); show_part (g_vGrid, lib); show_part (g_search, lib);
	int sw = W < 620 ? 0 : SIDE_W;
	show_part (g_side, lib && sw);
	if (lib)
	{
		int s = W > 900 ? 236 : W > 640 ? 170 : 110;
		g_searchX = W - s - 14 - FRAME_CAP_W;
		ui_w_move (g_vTb, 0, 0, W, TB_H);
		ui_w_move (g_search, g_searchX, 10, s, 30);
		if (sw) ui_w_move (g_side, 0, TB_H, sw, H - TB_H);
		g_grid->left = sw; g_grid->top = TB_H; g_grid->width = W - sw; g_grid->height = H - TB_H;
		ui_w_move (g_vGrid, sw, TB_H, W - sw, H - TB_H);
	}
	g_view->width = W; g_view->height = H; g_edit->width = W; g_edit->height = H;
	ui_w_move (g_vViewer, 0, 0, W, H); ui_w_move (g_vEditor, 0, 0, W, H);
	g_view->disp.free_ (); g_edit->viewDirty = true;
	g_grid->relayout ();
}
static void refresh_all ()
{
	g_grid->relayout ();
	side_build ();
	for (long v : { g_vTb, g_vGrid, g_vViewer, g_vEditor }) ui_view_dirty (v);
}
static void paint_all ()
{
	g_paintAll = false;
	layout_parts ();
	ui_fill (0, 0, ui.w, ui.h, C_BG);
	refresh_all ();
	ui_w_draw (0); ui_update_all ();
}
static void lib_changed ()
{
	counts ();
	if (g_src == SRC_ALBUM && (g_srcArg < 0 || g_srcArg >= g_lib.albums.n)) { g_src = SRC_ALBUMS; }
	if (g_src == SRC_FOLDER && (g_srcArg < 0 || g_srcArg >= g_lib.nroots)) { g_src = SRC_ALL; }
	int viewPi = !g_view->hidden ? g_view->pi : -1;			// (une photo montree la reste quand la liste bouge)
	build_list ();
	if (viewPi >= 0)
	{
		int at = -1; for (int i = 0; i < g_list.n; i++) if (g_list[i] == viewPi) at = i;
		if (at >= 0) g_view->pos = at;
	}
	refresh_all ();
}
static void show_source (int src, int arg)
{
	if (!g_view->hidden) close_viewer ();
	g_src = src; g_srcArg = arg;
	g_grid->sy = 0;
	lib_changed ();
}
static void open_viewer (int listPos)
{
	if (listPos < 0 || listPos >= g_list.n) return;
	g_view->hidden = false; g_mode = 1; g_paintAll = true;
	g_view->show (listPos);
	g_th.pause = false;
}
static void close_viewer ()
{
	if (g_view->pos >= 0) g_grid->scroll_to_pos (g_view->pos);
	g_view->hidden = true; g_view->full.free_ (); g_view->disp.free_ ();
	g_mode = 0; g_paintAll = true;
}
static void open_editor (int pi)
{
	const Pix *have = !g_view->hidden && g_view->pi == pi ? &g_view->full : 0;
	if (!g_edit->open (pi, have)) return;
	g_edit->hidden = false; g_mode = 2; g_paintAll = true;
}
static void close_editor ()
{
	g_edit->hidden = true; g_edit->close_ ();
	g_mode = !g_view->hidden ? 1 : 0; g_paintAll = true;
	if (!g_view->hidden) g_view->show (g_view->pos);
}
static void act_add_folder ()
{
	char p[300];
	if (!ui_folder_choose ((long) "SD0:/home", (long) p, sizeof p)) return;
	int k = (int) strlen (p); while (k > 1 && p[k - 1] == '/' && p[k - 2] != ':') p[--k] = 0;
	for (int i = 0; i < g_lib.nroots; i++)
		if (ipfx (p, g_lib.roots[i]) && (p[strlen (g_lib.roots[i])] == 0 || p[strlen (g_lib.roots[i])] == '/'))
		{ char q[400]; snprintf (q, sizeof q, TR ("%s is watched already (in %s)."), p, g_lib.roots[i]); msg (q); return; }
	if (g_lib.nroots >= 16) { msg (TR ("Photos watches 16 folders at most.")); return; }
	scpy (g_lib.added[g_lib.nadded++], p, sizeof g_lib.added[0]);
	g_lib.save_added (); g_lib.load_roots ();
	g_lib.start_scan ();
	for (int i = 0; i < g_lib.nroots; i++) if (ieq (g_lib.roots[i], p)) { show_source (SRC_FOLDER, i); return; }
	lib_changed ();
}
static void on_lib_change () { lib_changed (); }
static void on_thumb () { ui_view_dirty (g_vGrid); if (!g_view->hidden) ui_view_dirty (g_vViewer); }
static void on_full (FullDone *d) { g_view->full_came (d); }

// « photos SD0:/x.jpg » : cette photo en grand (dans la bibliotheque, ou ajoutee le temps de la voir)
static void open_path (const char *p)
{
	int k = g_lib.find (p);
	if (k < 0)
	{
		PicInfo pi; struct stat st;
		if (stat (p, &st) != 0 || !pic_info (p, pi)) { msg (TR ("This picture cannot be read.")); return; }
		Photo ph; photo_from (ph, p, (unsigned) st.st_size, pi, now_local ()); ph.alive = true;
		g_lib.ph.push (ph); g_lib.reindex (); k = g_lib.ph.n - 1;	// (pas enregistree si son dossier n'est pas suivi)
	}
	g_src = SRC_ALL; g_query[0] = 0; lib_changed ();
	for (int i = 0; i < g_list.n; i++) if (g_list[i] == k) { open_viewer (i); return; }
}

// ---- les vues : la souris se traite hors de ui_w_event (les dialogues et les menus s'y ouvrent) ------------------------------------
static struct { long view; int kind, x, y, info, wheel; bool on; } g_me;
static long any_mouse (long me, long kind, long x, long y, long info) { g_me.view = me; g_me.kind = (int) kind; g_me.x = (int) x; g_me.y = (int) y; g_me.info = (int) info; g_me.wheel = 0; g_me.on = true; return 0; }
static long any_wheel (long me, long n) { g_me.view = me; g_me.kind = -1; g_me.wheel = (int) n; g_me.on = true; return 0; }
static void mouse_now ()
{
	static int bl, br;
	if (!g_me.on) return;
	g_me.on = false;
	int k = g_me.kind, mx = g_me.x, my = g_me.y, wheel = g_me.wheel;
	static int lx, ly;
	if (k == -1) { mx = lx; my = ly; }
	else { lx = mx; ly = my; }
	if (k == 1) { int b = g_me.info >> 8 & 255; bl = b & 1; br = (b >> 1) & 1; g_mods = g_me.info >> 16 & 255; }
	else if (k == 2) { bl = 0; br = 0; }
	else if (k == 3) { mx = -1; my = -1; }
	long v = g_me.view;
	if (v == g_vTb) tb_mouse (mx, my, bl);
	else if (v == g_vGrid) g_grid->onMouse (mx, my, bl, br, wheel);
	else if (v == g_vViewer) g_view->onMouse (mx, my, bl, br, wheel);
	else if (v == g_vEditor) g_edit->onMouse (mx, my, bl);
}
static long grid_paint (long me, long, long) { begin_paint (me); g_grid->paint (); return 0; }
static long viewer_paint (long me, long, long) { begin_paint (me); g_view->paint (); return 0; }
static long editor_paint (long me, long, long) { begin_paint (me); g_edit->paint (); return 0; }

// ---- les menus ---------------------------------------------------------------------------------------------------------------------
enum { C_SLIDE = 1, C_ADDFOLDER, C_RESCAN, C_MAIL, C_PDF, C_FIND, C_SELALL, C_SELNONE, C_ALL, C_FAVS, C_RECENT, C_ALBUMS, C_BIGGER, C_SMALLER, C_EDIT, C_ROT, C_FAV, C_ALBUM, C_NEWALBUM, C_DEL, C_PRINT };
static void menus ()
{
	static char b[2000]; int n = 0;
	auto M = [&] (const char *t) { n += snprintf (b + n, sizeof b - n, "M%s\n", t); };
	auto I = [&] (int id, const char *t, const char *key) { n += snprintf (b + n, sizeof b - n, "I%d\t%s%s%s\n", id, t, key[0] ? "\t" : "", key); };
	auto S = [&] () { n += snprintf (b + n, sizeof b - n, "-\n"); };
	M (TR ("File"));
	I (C_SLIDE, TR ("Slideshow"), "F5"); S ();
	I (C_ADDFOLDER, TR ("Add a Folder..."), ""); I (C_RESCAN, TR ("Look for New Photos"), ""); S ();
	I (C_MAIL, TR ("Send by Mail..."), ""); I (C_PDF, TR ("Export as a PDF..."), ""); I (C_PRINT, TR ("Print..."), "Ctrl+P");
	M (TR ("Edit"));
	I (C_FIND, TR ("Find..."), "Ctrl+F"); I (C_SELALL, TR ("Select All"), "Ctrl+A"); I (C_SELNONE, TR ("Select None"), "");
	M (TR ("View"));
	I (C_ALL, TR ("All Photos"), ""); I (C_FAVS, TR ("Favourites"), ""); I (C_RECENT, TR ("Recently Added"), ""); I (C_ALBUMS, TR ("Albums"), ""); S ();
	I (C_BIGGER, TR ("Bigger Thumbnails"), ""); I (C_SMALLER, TR ("Smaller Thumbnails"), "");
	M (TR ("Photo"));
	I (C_EDIT, TR ("Edit..."), ""); I (C_ROT, TR ("Rotate to the Left"), ""); I (C_FAV, TR ("Favourite"), ""); I (C_ALBUM, TR ("Add to an Album..."), ""); I (C_NEWALBUM, TR ("New Album..."), ""); S ();
	I (C_DEL, TR ("Move to the Trash"), "");
	ui_menu ((long) b);
}
static void command (int id)
{
	bool v = !g_view->hidden;
	switch (id)
	{
	case C_SLIDE: act_slideshow (0); break;
	case C_ADDFOLDER: act_add_folder (); break;
	case C_RESCAN: g_lib.start_scan (); refresh_all (); break;
	case C_MAIL: if (v) act_mail (g_view->pi); else act_mail (); break;
	case C_PDF: act_pdf (g_src == SRC_ALBUM && !g_selN ? g_srcArg : -1); break;
	case C_PRINT: act_print (-1); break;
	case C_FIND: ui_w_focus (g_search); break;
	case C_SELALL: for (int i = 0; i < g_list.n; i++) sel_set (g_list[i], true); refresh_all (); break;
	case C_SELNONE: sel_clear (); refresh_all (); break;
	case C_ALL: show_source (SRC_ALL); break;
	case C_FAVS: show_source (SRC_FAV); break;
	case C_RECENT: show_source (SRC_RECENT); break;
	case C_ALBUMS: show_source (SRC_ALBUMS); break;
	case C_BIGGER: g_tile = g_tile + 24 > 220 ? 220 : g_tile + 24; refresh_all (); break;
	case C_SMALLER: g_tile = g_tile - 24 < 72 ? 72 : g_tile - 24; refresh_all (); break;
	case C_EDIT: if (v) open_editor (g_view->pi); else { Vec<int> c; if (chosen (c) == 1) open_editor (c[0]); free (c.a); } break;
	case C_ROT: if (v) act_rotate (g_view->pi); else act_rotate (); break;
	case C_FAV: if (v) act_favourite (g_view->pi); else act_favourite (); break;
	case C_ALBUM: if (v) act_add_to_album (g_view->pi, 300, 60); else act_add_to_album (-1, 300, 60); break;
	case C_NEWALBUM: act_new_album (); break;
	case C_DEL: if (v) act_delete (g_view->pi); else act_delete (); break;
	}
}
static void control (long w, long code)
{
	if (w == g_side)
	{
		if (code == UN_CHANGE || code == UN_ACTIVATE) side_chosen ();
		else if (code == UN_CONTEXT)
		{
			long i = ui_w_get_value (g_side);
			if (i >= 0 && i < g_nside) side_menu (g_sideItems[i].id, ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y));
		}
	}
}
static void key (int hid, int ch, int mods)
{
	if (hid >= 0xE0) return;				// (un modificateur seul)
	long k = key_code (hid, ch, mods);
	if (g_mode == 2) { g_edit->key (k); return; }
	if (g_mode == 1) { g_view->key (k); return; }
	if (ui_w_get_focus () == g_search) { if (hid == HID_ESC) ui_w_set_text (g_search, (long) ""); return; }
	if (hid == HID_ESC && g_selN) { sel_clear (); refresh_all (); return; }
	if (k == KEY_DEL && g_selN) { act_delete (); return; }
	g_grid->key (k);
}
static void tick ()
{
	static char lastQ[200]; static unsigned qT;
	unsigned now = ticks ();
	const char *q = (const char *) ui_w_get_text (g_search);
	if (strcmp (lastQ, q)) { scpy (lastQ, q, sizeof lastQ); qT = now ? now : 1; }
	if (qT && now - qT > 25) { qT = 0; scpy (g_query, lastQ, sizeof g_query); if (g_src == SRC_ALBUMS && g_query[0]) g_src = SRC_ALL; g_grid->sy = 0; lib_changed (); }
	static bool wasScanning; if (g_lib.scanning != wasScanning) { wasScanning = g_lib.scanning; ui_view_dirty (g_vGrid); if (!wasScanning) g_th.set_backlog (); }
	static int lastDone = -1; static unsigned doneT;	// (la barre des vignettes : quelques fois par seconde)
	if (g_th.blDone != lastDone && now - doneT > 30) { lastDone = g_th.blDone; doneT = now; ui_view_dirty (g_vGrid); }
	static unsigned noteT; if (g_note[0] && now - g_noteT > 600 && noteT != g_noteT) { noteT = g_noteT; ui_view_dirty (g_vGrid); }
	if (g_lib.dirty && !g_lib.scanning) { static unsigned dT; if (!dT) dT = now; if (now - dT > 300) { dT = 0; g_lib.save (); } }
}
static void on_resize (long, long) { paint_all (); }

static int run (int argc, char **argv)
{
	if (!uikit_open (&ui) || !archivekit_kit_open ()) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/photos.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	mkdirs (PH_PICTURES); mkdirs (PH_DIR);

	g_grid = new Grid; g_view = new Viewer; g_edit = new Editor;
	g_lib.load ();
	g_lib.onChange = on_lib_change;
	g_th.lib = &g_lib; g_th.onLoaded = on_thumb; g_th.onFull = on_full;

	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int w = scr[0] - 8 < 1000 ? (int) scr[0] - 8 : 1000, h = (int) (scr[1] - scr[3] - (scr[2] > 96 ? scr[2] : 96) - 32); if (h > 640) h = 640;
	ui_window_ex (0, scr[3], w, h, (long) TR ("Photos"), WF_RESIZABLE);
	ui_min_size (500, 380);
	ui_on_resize ((long) on_resize, 0);

	g_vGrid = ui_view (0, 0, 10, 10, UVW_FOCUS); ui_view_on (g_vGrid, (long) grid_paint, (long) any_mouse, 0, (long) any_wheel);
	g_side = ui_list (0, 0, 10, 10, UL_SIDEBAR); ui_list_column (g_side, (long) "", SIDE_W - 4, 0); ui_list_cell (g_side, (long) side_cell);
	g_vTb = ui_view (0, 0, 10, 10, 0); ui_view_on (g_vTb, (long) tb_paint, (long) any_mouse, 0, 0);
	g_search = ui_textbox (0, 0, 100, 30, 0); ui_textbox_hint (g_search, (long) TR ("Search: a name, a date...")); ui_w_set_range (g_search, 0, 190, 0, 0); ui_w_set_parent (g_search, g_vTb);
	g_vViewer = ui_view (0, 0, 10, 10, UVW_FOCUS); ui_view_on (g_vViewer, (long) viewer_paint, (long) any_mouse, 0, (long) any_wheel);
	g_vEditor = ui_view (0, 0, 10, 10, UVW_FOCUS); ui_view_on (g_vEditor, (long) editor_paint, (long) any_mouse, 0, (long) any_wheel);
	menus ();

	counts ();
	build_list ();
	paint_all ();
	g_th.start ();
	g_th.set_backlog ();			// (ceux qu'on connait : tout de suite ; les nouveaux a la fin de la lecture des dossiers)
	g_lib.start_scan ();
	if (argc > 1 && argv[1][0]) open_path (argv[1]);

	for (;;)
	{
		long t = ui_event (g_lib.scanning || g_th.blDone < g_th.blTotal ? 10 : 30);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR), mods = ui_ev_field (&ui, EV_MODS);
			long code = 0, taken = 0, wd = uikit_w_event (&code, &taken);
			if (code) control (wd, code);
			if (press && !taken) { long id = ui_menu_key (0); if (id >= 0) command ((int) id); else key (hid, ch, mods); }
		}
		else ui_w_idle ();
		mouse_now ();
		scan_pump (g_lib);
		g_th.pump ();
		tick ();
		if (g_paintAll) paint_all (); else ui_view_flush ();
	}

	g_th.quit ();
	g_lib.quit ();
	if (g_lib.dirty) g_lib.save ();
	return 0;
}

} // namespace photos

int main (int argc, char **argv) { return photos::run (argc, argv); }
