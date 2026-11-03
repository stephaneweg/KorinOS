//
// share.h (d'Onyx, user/Apps/photos/share.h) -- ce qu'on fait des photos : favori, album, corbeille, quart de tour (un JPEG :
// son orientation EXIF, sans toucher aux pixels), envoi par Mail (copies 1920 px jointes a un message neuf), Paint, une
// planche contact en PDF, le diaporama (plein ecran, une photo qui se fond dans la suivante).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_share_h
#define _photos_share_h

#include "editor.h"
#include "../letters/pdfwrite.h"
#include <printerkit.h>

namespace photos {
static void act_print (int one = -1);

static void status_note (const char *s) { scpy (g_note, s, sizeof g_note); g_noteT = ticks (); ui_view_dirty (g_vGrid); }
// ---- favori -------------------------------------------------------------------------------------------------------------------------
static void act_favourite (int one)
{
	Vec<int> c; if (!chosen (c, one)) { free (c.a); return; }
	bool all = true; for (int i = 0; i < c.n; i++) if (!g_lib.ph[c[i]].fav) all = false;
	for (int i = 0; i < c.n; i++) g_lib.ph[c[i]].fav = !all;
	free (c.a);
	g_lib.save ();
	lib_changed ();
}

// ---- les albums -----------------------------------------------------------------------------------------------------------------------
static void act_new_album ()
{
	char n[120];
	if (!ask_name (TR ("A new album"), TR ("Its name:"), "", n, sizeof n)) return;
	if (strchr (n, '/') || strchr (n, ':')) { msg (TR ("An album's name cannot have a / or a :.")); return; }
	int a = g_lib.album_new (n);
	Vec<int> c; chosen (c);
	for (int i = 0; i < c.n; i++) g_lib.album_add (a, g_lib.ph[c[i]].path);
	free (c.a);
	if (c.n) { char s[200]; snprintf (s, sizeof s, TR ("%d added to %s"), c.n, n); status_note (s); sel_clear (); }
	lib_changed ();
	if (!c.n) show_source (SRC_ALBUM, g_lib.album_find (n));
}
static void act_add_to_album (int one, int x, int y)
{
	Vec<int> c; if (!chosen (c, one)) { free (c.a); return; }
	if (x < 0) { x = 200; y = 100; }
	PopMenu m;
	int n = g_lib.albums.n < 13 ? g_lib.albums.n : 13;
	for (int a = 0; a < n; a++)
	{
		bool all = true; for (int i = 0; i < c.n; i++) if (!g_lib.album_has (a, g_lib.ph[c[i]].path)) all = false;
		char l[160]; snprintf (l, sizeof l, all ? "%s   \xE2\x9C\x93" : "%s", g_lib.albums[a].name);
		m.add (l, 10 + a);
	}
	if (n) m.separator ();
	m.add (TR ("New album..."), 1);
	int r = m.run (x, y);
	if (r == 1) { if (one >= 0 && !g_selN) { sel_set (one, true); act_new_album (); sel_clear (); } else act_new_album (); free (c.a); refresh_all (); return; }
	if (r >= 10)
	{
		int a = r - 10;
		bool all = true; for (int i = 0; i < c.n; i++) if (!g_lib.album_has (a, g_lib.ph[c[i]].path)) all = false;
		for (int i = 0; i < c.n; i++) { if (all) g_lib.album_remove (a, g_lib.ph[c[i]].path); else g_lib.album_add (a, g_lib.ph[c[i]].path); }
		char s[200]; snprintf (s, sizeof s, all ? TR ("Taken out of %s") : TR ("Added to %s"), g_lib.albums[a].name); status_note (s);
		if (!all && g_selN) sel_clear ();
		lib_changed ();
	}
	free (c.a);
}

// ---- la corbeille : SD0:/home/.trash (docs/FEUILLE-DE-ROUTE.md) ------------------------------------------------------------------------
#define TRASH_DIR "SD0:/home/.trash"
static bool trash_move (const char *path)
{
	mkdirs (TRASH_DIR);
	char out[400]; snprintf (out, sizeof out, TRASH_DIR "/%s", base_name (path));
	for (int k = 2; k < 1000 && file_there (out); k++) snprintf (out, sizeof out, TRASH_DIR "/%d-%s", k, base_name (path));
	if (rename (path, out) == 0) return true;
	// (un autre volume) la copie, puis l'original retire
	int len; char *b = file_read (path, &len);
	bool ok = b && save_file (out, b, (size_t) len);
	free (b);
	if (ok) ok = remove (path) == 0;
	return ok;
}
static void act_delete (int one)
{
	Vec<int> c; if (!chosen (c, one)) { free (c.a); return; }
	char q[300];
	if (c.n == 1) snprintf (q, sizeof q, TR ("Move %s to the trash?"), base_name (g_lib.ph[c[0]].path));
	else snprintf (q, sizeof q, TR ("Move these %d photos to the trash?"), c.n);
	if (!ask_yes (q)) { free (c.a); return; }
	int failed = 0;
	for (int i = 0; i < c.n; i++)
	{
		Photo &p = g_lib.ph[c[i]];
		if (!trash_move (p.path)) { failed++; continue; }
		g_th.forget (p.key ());
		g_lib.albums_path_changed (p.path, 0);
		p.alive = false;
	}
	int vpos = g_view && !g_view->hidden ? g_view->pos : -1;
	free (c.a);
	sel_clear ();
	g_lib.compact (); g_lib.save ();
	lib_changed ();
	if (failed) msg (TR ("Some photos could not be moved to the trash."));
	if (vpos >= 0) { if (!g_list.n) close_viewer (); else open_viewer (vpos < g_list.n ? vpos : g_list.n - 1); }
}

// ---- un quart de tour a gauche ----------------------------------------------------------------------------------------------------
// un JPEG avec une orientation EXIF : ce nombre change (sans perte) ; sinon les pixels tournent et sont recrits
static bool set_exif_orientation (const char *path, int o)
{
	PicInfo pi; if (!pic_info (path, pi) || !pi.exifOff) return false;
	int len; char *b = file_read (path, &len); if (!b) return false;
	unsigned char *u = (unsigned char *) b; unsigned base = pi.exifOff + 10;
	bool done = false;
	if (base + 8 < (unsigned) len)
	{
		Tiff t { u + base, (unsigned) len - base, u[base] == 'I' };
		unsigned ifd0 = t.u32 (4), c = t.u16 (ifd0);
		for (unsigned i = 0; i < c && i < 400; i++)
		{
			unsigned e = ifd0 + 2 + i * 12;
			if (t.u16 (e) == 0x0112) { unsigned at = base + e + 8; if (t.le) { u[at] = (unsigned char) o; u[at + 1] = 0; } else { u[at] = 0; u[at + 1] = (unsigned char) o; } done = true; break; }
		}
	}
	if (done) done = save_file (path, b, (size_t) len);
	free (b);
	return done;
}
static void act_rotate (int one)
{
	Vec<int> c; if (!chosen (c, one)) { free (c.a); return; }
	static const int CCW[9] = { 0, 8, 0, 6, 0, 0, 1, 0, 3 };	// 1 -> 8 -> 3 -> 6 -> 1
	for (int i = 0; i < c.n; i++)
	{
		Photo &p = g_lib.ph[c[i]];
		int o = p.orient >= 1 && p.orient <= 8 ? p.orient : 1;
		bool ok = false;
		if (CCW[o]) ok = set_exif_orientation (p.path, CCW[o]);
		if (ok) { g_th.forget (p.key ()); p.orient = (unsigned char) CCW[o]; short t = p.w; p.w = p.h; p.h = t; continue; }
		Pix full; if (!Thumbs::load_full (p.path, p.orient, full)) continue;
		rotate90 (full, 3); opaque (full);
		bool png = Editor::ends (p.path, ".png");
		unsigned n = 0; unsigned char *d = png ? pngsave::png_encode (full.px, full.w, full.h, false, &n) : (Editor::ends (p.path, ".jpg") || Editor::ends (p.path, ".jpeg")) ? pngsave::jpeg_encode (full.px, full.w, full.h, 94, &n) : 0;
		if (!d) { msg (TR ("This kind of picture cannot be turned here (open it in Paint).")); continue; }
		PicInfo pi; pic_info (p.path, pi);
		if (!png && pi.exifOff) { unsigned m; unsigned char *x = jpeg_with_exif (d, n, p.path, pi, &m); if (x) { delete[] d; d = x; n = m; } }
		g_th.forget (p.key ());
		if (save_file (p.path, d, n)) { p.size = n; p.orient = 1; p.w = (short) full.w; p.h = (short) full.h; }
		delete[] d;
	}
	free (c.a);
	g_lib.save ();
	if (g_view && !g_view->hidden) g_view->show (g_view->pos);
	lib_changed ();
}

// ---- envoyer par Mail ----------------------------------------------------------------------------------------------------------------------
static void act_mail (int one)
{
	Vec<int> c; if (!chosen (c, one)) { free (c.a); return; }
	if (c.n > 16) { msg (TR ("Mail takes 16 attachments at most: choose fewer photos.")); free (c.a); return; }
	const char *dir = PH_DIR "/outbox";
	mkdirs (dir);
	static char list[20000]; int k = 0;
	status_note (TR ("Making the copies for Mail..."));
	for (int i = 0; i < c.n; i++)
	{
		const Photo &p = g_lib.ph[c[i]];
		char out[400], name[200]; scpy (name, base_name (p.path), sizeof name); char *dot = strrchr (name, '.'); if (dot) *dot = 0;
		snprintf (out, sizeof out, "%s/%s.jpg", dir, name);
		Pix full, small;
		if (!Thumbs::load_full (p.path, p.orient, full)) continue;
		fit (full, small, 1920, 1920); full.free_ (); opaque (small);
		unsigned n = 0; unsigned char *d = pngsave::jpeg_encode (small.px, small.w, small.h, 85, &n);
		if (d && save_file (out, d, n)) k += snprintf (list + k, sizeof list - k, "%s\n", out);
		delete[] d;
	}
	free (c.a);
	char lp[200]; snprintf (lp, sizeof lp, "%s/attach.txt", dir);
	save_file (lp, list, (size_t) k);
	if (!spawn_app (MAIL_PROGRAM, "--attach", lp)) msg (TR ("Mail could not be started."));
	else status_note (TR ("Opened in Mail."));
}

static void act_share_menu (int one, int x, int y)
{
	Vec<int> c; int n = chosen (c, one); int first = n ? c[0] : -1; free (c.a);
	if (!n) return;
	PopMenu m;
	m.add (TR ("Send by Mail..."), 1);
	m.add (TR ("Open in Paint"), 4, n == 1);
	m.add (TR ("Export as a PDF..."), 5);
	m.add (TR ("Print..."), 6);
	switch (m.run (x, y))
	{
	case 1: act_mail (one); break;
	case 4: spawn_app (PAINT_PROGRAM, g_lib.ph[first].path); break;
	case 5: act_pdf (-1); break;
	case 6: act_print (one); break;
	}
}

// ---- le menu d'une photo (clic droit) -----------------------------------------------------------------------------------------------
static void photo_menu (int pi, int x, int y)
{
	bool many = g_selN > 1;
	const Photo &p = g_lib.ph[pi];
	PopMenu m;
	if (!many) m.add (TR ("Open"), 1);
	m.add (TR ("Slideshow from here"), 2);
	m.separator ();
	m.add (p.fav && !many ? TR ("Remove from the favourites") : TR ("Add to the favourites"), 3);
	m.add (TR ("Add to an album..."), 4);
	m.add (TR ("Rotate to the left"), 5);
	if (!many) m.add (TR ("Edit..."), 6);
	m.separator ();
	m.add (TR ("Send by Mail..."), 7);
	if (!many) { m.add (TR ("Open in Paint"), 10); m.add (TR ("Show in the File Viewer"), 11); }
	m.separator ();
	m.add (TR ("Move to the trash"), 12);
	int one = g_selN ? -1 : pi;
	switch (m.run (x, y))
	{
	case 1: for (int i = 0; i < g_list.n; i++) if (g_list[i] == pi) { open_viewer (i); break; } break;
	case 2: for (int i = 0; i < g_list.n; i++) if (g_list[i] == pi) { act_slideshow (i); break; } break;
	case 3: act_favourite (one); break;
	case 4: act_add_to_album (one, x, y); break;
	case 5: act_rotate (one); break;
	case 6: open_editor (pi); break;
	case 7: act_mail (one); break;
	case 10: spawn_app (PAINT_PROGRAM, p.path); break;
	case 11: { char d[300]; scpy (d, p.path, sizeof d); char *e = strrchr (d, '/'); if (e) *e = 0; spawn_app (FILES_PROGRAM, d); break; }
	case 12: act_delete (one); break;
	}
}

// ---- imprimer : chaque photo une page, aussi grande que ce que l'imprimante imprime du papier le permet, centree ----------------------
// (one : la photo montree ; sinon les photos choisies). Le dialogue est celui de PrinterKit. Une photo va au travail en JPEG.
static void act_print (int one)
{
	Vec<int> c; chosen (c, one);
	if (!c.n) { free (c.a); msg (TR ("Select the photos to print first.")); return; }
	if (!printerkit_kit_open ()) { free (c.a); msg (TR ("The print service is not installed.")); return; }
	char title[160];
	if (c.n == 1) scpy (title, base_name (g_lib.ph[c[0]].path), sizeof title); else snprintf (title, sizeof title, TR ("%d photos"), c.n);
	PrintSetup ps; print_setup_default ((long) &ps);
	{ const Photo &p = g_lib.ph[c[0]]; bool turn = p.orient >= 5; if ((turn ? p.h : p.w) > (turn ? p.w : p.h)) print_setup_paper ((long) &ps, 0, PRINT_LANDSCAPE); }
	PrintDialogInfo di = { sizeof di, title, c.n, 0, 0, 0, 0 };
	if (!print_dialog ((long) &ps, (long) &di)) { free (c.a); return; }
	long j = print_begin ((long) &ps, (long) title);
	if (j)
	{
		status_note (TR ("Preparing the pages..."));
		float l = ps.margin_l > 18000 ? ps.margin_l / 1000.0f : 18, t = ps.margin_t > 18000 ? ps.margin_t / 1000.0f : 18;
		float r = ps.margin_r > 18000 ? ps.margin_r / 1000.0f : 18, b = ps.margin_b > 18000 ? ps.margin_b / 1000.0f : 18;
		float aw = ps.paper_w / 1000.0f - l - r, ah = ps.paper_h / 1000.0f - t - b;
		for (int i = 0; i < c.n; i++)
		{
			if (!print_page (j, 0, 0)) continue;			// (pas une des pages a imprimer : meme pas lue)
			const Photo &p = g_lib.ph[c[i]];
			Pix full; if (!Thumbs::load_full (p.path, p.orient, full)) continue;
			opaque (full);
			float k = aw / full.w < ah / full.h ? aw / full.w : ah / full.h;
			float w = full.w * k, h = full.h * k;
			unsigned n = 0; unsigned char *d = pngsave::jpeg_encode (full.px, full.w, full.h, 90, &n);
			if (d) print_image_jpeg (j, (long) d, n, (long) full.w | (long) full.h << 32, print_mpt (l + (aw - w) / 2), print_mpt (t + (ah - h) / 2), print_mpt (w), print_mpt (h));
			else print_image (j, (long) full.px, full.w, full.h, print_mpt (l + (aw - w) / 2), print_mpt (t + (ah - h) / 2), print_mpt (w), print_mpt (h));
			delete[] d;
			full.free_ ();
		}
	}
	free (c.a);
	if (!j || print_end (j) < 0) msg (TR ("The photos could not be put in the print queue."));
	else status_note (TR ("In the print queue."));
}

// ---- un PDF : une planche contact ------------------------------------------------------------------------------------------------------------
// chaque page dessinee comme une image (A4, 150 dpi) dans un canevas cache : le titre, 3 x 4 photos, leurs dates
static void act_pdf (int album)
{
	Vec<int> c;
	char title[160];
	if (album >= 0)
	{
		for (int k = 0; k < g_lib.albums[album].paths.n; k++) { int i = g_lib.find (g_lib.albums[album].paths[k]); if (i >= 0 && !g_lib.ph[i].offline) c.push (i); }
		for (int i = 1; i < c.n; i++) { int t = c[i]; int j = i - 1; while (j >= 0 && g_lib.ph[c[j]].when () > g_lib.ph[t].when ()) { c[j + 1] = c[j]; j--; } c[j + 1] = t; }
		scpy (title, g_lib.albums[album].name, sizeof title);
	}
	else
	{
		chosen (c);
		if (!c.n) for (int i = 0; i < g_list.n; i++) c.push (g_list[i]);
		for (int i = 0; i < c.n / 2; i++) { int t = c[i]; c[i] = c[c.n - 1 - i]; c[c.n - 1 - i] = t; }
		scpy (title, src_title (), sizeof title);
	}
	if (!c.n) { free (c.a); msg (TR ("There is no photo to put in a PDF.")); return; }
	char out[400], def[200]; snprintf (def, sizeof def, "%s.pdf", title);
	for (char *s = def; *s; s++) if (*s == '/' || *s == ':') *s = '-';
	ui_file_filter ((long) "*.pdf");
	if (!ui_file_save ((long) "SD0:/home/documents", (long) def, (long) out, sizeof out)) { free (c.a); return; }
	if (!Editor::ends (out, ".pdf")) { int k = (int) strlen (out); if (k < 390) strcpy (out + k, ".pdf"); }
	status_note (TR ("Making the PDF..."));
	const int PW = 1240, PH = 1754, M = 90, COLS = 3, ROWS = 4;		// A4 a 150 dpi
	const float PTW = 595.3f, PTH = 841.9f;
	pdfw::Writer w;
	w.info (title, "", 0, "Photos (Korin OS)");
	long cv = ui_canvas (0, 0, PW, PH);
	if (!cv) { free (c.a); return; }
	ui_w_show (cv, 0);
	long gw, gh; uint32_t *cpx = uikit_canvas_pixels (cv, &gw, &gh);
	long sp = (long) g_px, sw = g_pw, sh = g_ph;
	int per = COLS * ROWS, pages = (c.n + per - 1) / per;
	int cellW = (PW - 2 * M - (COLS - 1) * 30) / COLS, cellH = (PH - 2 * M - 140 - (ROWS - 1) * 36) / ROWS;
	for (int pg = 0; pg < pages; pg++)
	{
		ui_canvas_begin (cv);
		g_px = cpx; g_pw = PW; g_ph = PH;
		ui_fill (0, 0, PW, PH, 0xFFFFFF);
		ui_text ((long) title, M, M + 34, 0x202428, 44, face (1), 0);
		char sub[80]; snprintf (sub, sizeof sub, c.n == 1 ? TR ("1 photo") : TR ("%d photos"), c.n);
		if (pages > 1) snprintf (sub + strlen (sub), sizeof sub - strlen (sub), TR ("  \xC2\xB7  page %d of %d"), pg + 1, pages);
		ui_text ((long) sub, M, M + 64, 0x70757C, 24, face (0), 0);
		ui_fill (M, M + 80, PW - 2 * M, 2, 0xD0D4D8);
		for (int k = 0; k < per; k++)
		{
			int i = pg * per + k; if (i >= c.n) break;
			int cx = M + (k % COLS) * (cellW + 30), cy = M + 100 + (k / COLS) * (cellH + 36);
			Pix full, f;
			const Photo &p = g_lib.ph[c[i]];
			if (Thumbs::load_full (p.path, p.orient, full)) { fit (full, f, cellW, cellH - 28, true); full.free_ (); }
			if (f.px) Thumbs::blit (f.px, cx + (cellW - f.w) / 2, cy + (cellH - 28 - f.h) / 2, f.w, f.h);
			char d[100] = ""; if (p.when ()) fmt_day (p.when (), d, sizeof d); else scpy (d, base_name (p.path), sizeof d);
			int dw = (int) ui_text_width ((long) d, 20, face (0));
			ui_text ((long) d, cx + (cellW - dw) / 2, cy + cellH - 8, 0x50555C, 20, face (0), 0);
		}
		ui_canvas_end ();
		for (long i = 0, n = (long) PW * PH; i < n; i++) cpx[i] |= 0xFF000000u;
		w.begin_page (PTW, PTH);
		w.image ((const unsigned *) cpx, PW, PH, 0, 0, PTW, PTH, true, 88);
		w.end_page ();
	}
	g_px = (uint32_t *) sp; g_pw = sw; g_ph = sh;
	ui_w_destroy (cv);
	free (c.a);
	unsigned n = 0; unsigned char *pdf = w.finish (&n);
	bool ok = pdf && save_file (out, pdf, n);
	delete[] pdf;
	if (!ok) msg (TR ("The PDF could not be written."));
	else { char s[300]; snprintf (s, sizeof s, TR ("Saved: %s"), base_name (out)); status_note (s); }
}

// ---- le diaporama ---------------------------------------------------------------------------------------------------------------------------
static FullDone *g_ssCame;			// la prochaine photo, decodee
static void (*g_ssPrevOnFull) (FullDone *);
static void ss_full (FullDone *d) { if (g_ssCame) { g_ssCame->pix.free_ (); delete g_ssCame; } g_ssCame = new FullDone; g_ssCame->index = d->index; g_ssCame->failed = d->failed; g_ssCame->pix.take (d->pix); }
static void act_slideshow (int from)
{
	Vec<int> show;
	if (g_src == SRC_ALBUMS) { free (show.a); return; }
	if (g_selN > 1) { for (int i = 0; i < g_list.n; i++) if (selected (g_list[i])) show.push (g_list[i]); from = 0; }
	else for (int i = 0; i < g_list.n; i++) show.push (g_list[i]);
	if (!show.n) { free (show.a); return; }
	if (from < 0 || from >= show.n) from = 0;
	long scr[4] = { 0, 0, 0, 0 };
	ui_screen ((long) scr);
	ui_dialog_at (0, 0);
	long dlg = ui_dialog_begin (scr[0], scr[1], (long) TR ("Photos"));
	if (!dlg) { free (show.a); return; }
	long win = ui.win;
	int W = (int) ui.w, H = (int) ui.h;
	uint32_t *fb = ui.surf;
	g_ssPrevOnFull = g_th.onFull; g_th.onFull = ss_full; g_ssCame = 0;
	Pix cur, curF, prevF; curF.alloc (W, H); prevF.alloc (W, H);
	if (!curF.px || !prevF.px) { ui_dialog_end (dlg); g_th.onFull = g_ssPrevOnFull; free (show.a); return; }
	for (int i = 0; i < W * H; i++) fb[i] = curF.px[i] = prevF.px[i] = 0;
	ui_update_all ();
	int at = from; bool paused = false, want = true, fading = false, hudShown = false;
	unsigned shownT = 0, fadeT = 0, hudT = ticks ();
	const unsigned STAY = 450, FADE = 60, HUD = 250;	// centiemes : 4,5 s par photo, 0,6 s de fondu, l'aide 2,5 s
	auto place = [&] (const Pix &p, unsigned *dst) {
		for (int i = 0; i < W * H; i++) dst[i] = 0;
		if (!p.px) return;
		float s = (float) W / p.w; if (p.h * s > H) s = (float) H / p.h;
		int w = (int) (p.w * s), h = (int) (p.h * s); if (w < 1) w = 1; if (h < 1) h = 1;
		scale_into (p.px, p.w, p.h, 0, 0, p.w, p.h, dst + (long) ((H - h) / 2) * W + (W - w) / 2, W, w, h);
	};
	auto hud = [&] () {
		char s[160]; snprintf (s, sizeof s, TR ("%d / %d%s   \xC2\xB7   Esc: back   \xC2\xB7   \xE2\x86\x90 \xE2\x86\x92   \xC2\xB7   Space: %s"), at + 1, show.n, paused ? TR ("  (paused)") : "", paused ? TR ("go on") : TR ("pause"));
		int w = tw (s) + 40;
		g_px = fb; g_pw = W; g_ph = H;
		fill_round ((W - w) / 2, H - 70, w, 36, 16, 0, 170);
		text_c ((W - w) / 2, H - 70, w, 36, s, 0xFFFFFF);
	};
	for (;;)
	{
		long e = ui_event (10);
		g_th.pump ();
		if (e == WEV_CLOSE) break;
		unsigned now = ticks ();
		int key = 0, chr = 0, click = 0;
		if (e > 0 && ui_ev_field (&ui, EV_WIN) == win)
		{
			if (e == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0) { key = ui_ev_field (&ui, EV_CODE); chr = ui_ev_field (&ui, EV_CHAR); }
			if (e == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1) click = 1;
			if (e == WEV_MOUSE) hudT = now;
		}
		if (key == HID_ESC || click) break;
		bool redraw = false;
		if (chr == ' ') { paused = !paused; hudT = now; redraw = true; shownT = now; }
		if (key == HID_RIGHT || key == HID_PGDN) { at = (at + 1) % show.n; want = true; hudT = now; }
		if (key == HID_LEFT || key == HID_PGUP) { at = (at + show.n - 1) % show.n; want = true; hudT = now; }
		if (want) { want = false; g_th.want_full (show[at]); }
		if (g_ssCame)
		{
			FullDone *d = g_ssCame; g_ssCame = 0;
			if (d->index == show[at])
			{
				unsigned *t = prevF.px; prevF.px = curF.px; curF.px = t;
				cur.free_ (); if (!d->failed) cur.take (d->pix);
				place (cur, curF.px);
				fading = true; fadeT = now; shownT = now; redraw = true;
			}
			d->pix.free_ (); delete d;
		}
		if (!paused && cur.px && !fading && now - shownT > STAY) { at = (at + 1) % show.n; want = true; shownT = now; }
		bool hudOn = now - hudT < HUD;
		if (hudOn != hudShown) { hudShown = hudOn; redraw = true; }
		if (fading || redraw)
		{
			unsigned t = now - fadeT;
			if (fading && t >= FADE) fading = false;
			if (fading) { int a = (int) (t * 256 / FADE); for (int i = 0; i < W * H; i++) fb[i] = mix (prevF.px[i], curF.px[i], a); }
			else memcpy (fb, curF.px, (size_t) W * H * 4);
			if (hudOn) hud ();
			ui_update_all ();
		}
	}
	ui_dialog_end (dlg);
	g_th.onFull = g_ssPrevOnFull;
	if (g_ssCame) { g_ssCame->pix.free_ (); delete g_ssCame; g_ssCame = 0; }
	free (show.a);
	refresh_all ();
}

} // namespace photos

#endif
