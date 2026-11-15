//
// Forge -- la CAO de Korin (docs/DESIGN-CAO.md) : OpenCASCADE pour les solides, PlaneGCS pour les esquisses ; l'interface
// de 3DForge d'Onyx (clic-déplacer-clic, cotes sur le dessin, frise) façon Fusion et Shapr3D.
//
// Étape 1 (§14) : la fenêtre (barre, vue, frise, panneau de propriétés, barre d'état), la vue 3D (faces, arêtes,
// grille, trièdre, cube de vue ; tourner, déplacer, zoomer vers le pointeur, vues types, cadrer), le survol et le choix
// d'une face, d'une arête ou d'un corps avec leurs mesures, le panneau flottant des corps. La pièce d'exemple est
// l'équerre de 3DForge, faite par OpenCASCADE. Les outils (formes, esquisse, congés...) viennent aux étapes suivantes.
//
// La vue est rendue par le processeur dans une surface d'UIKit (render.cpp), en attendant le chemin GPU (GlKit,
// tools/mesa/armgl/glview.h). Ce qui est posé sur elle (corps, boutons de vue, étiquettes du cube et du trièdre) est
// dessiné par UIKit dans le contenu de la fenêtre, sous la surface, et passe par-dessus par ui_surface_overlay ; aucun
// widget sous la vue (un ui_view a son propre tampon, sans la transparence) : la souris y est lue brute.
//
//   forge                la pièce d'exemple
//   forge --selftest     les mêmes, puis des vérifications écrites sur la sortie (essais sous QEMU) ; code 0 si tout passe
//
// C++ sur UIKit, appels ui_* directs, couleurs par les jetons du thème (aucune en dur, sauf la palette des corps, qui
// est une donnée du document).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <uikit.h>
#include "model.h"
#include "render.h"

using namespace forge;

#define TR(s) ((const char *) ui_tr ((long) (s)))

namespace {

struct ui_vars ui;
const unsigned *s_jet;
unsigned jet (int off) { return s_jet[off / 4] & 0xFFFFFF; }
int imin (int a, int b) { return a < b ? a : b; }
int imax (int a, int b) { return a > b ? a : b; }

Doc g_doc;
View g_view;
Palette g_pal;
Pick g_sel, g_hot;
int g_selStep = -1;			// l'étape choisie dans la frise (-1 : aucune)
long g_surf;				// la surface de la vue
int VX, VY, VW, VH;			// la zone de la vue dans le contenu
int TB_H, TL_H, ST_H, PW;		// barre, frise, état, panneau
bool g_quit;

// widgets
long w_doc, w_docsub, w_mode, w_bar, w_export, w_tl, w_status, w_stats;
long p_title, p_sub, p_sec, p_name[8], p_val[8], p_state, p_hint;

// ---- le panneau flottant des corps et les boutons de la vue (dessinés dans la vue, par-dessus la surface) --------
const int BP_X = 12, BP_Y = 12, BP_W = 232, BP_ROW = 30;
const char *const VBTN[] = { "Maison", "Ajuster", "Arêtes", "Grille" };
const int VB_N = 4, VB_W = 76, VB_H = 30;

int bodiesH () { return 40 + BP_ROW * (int) g_doc.bodies.size (); }
void vbtnRect (int i, int &x, int &y)
{
	int cx, cy, cw, ch;
	g_view.cubeRect (cx, cy, cw, ch);
	x = cx + (cw - VB_W) / 2;
	y = cy + ch + 8 + i * (VB_H + 6);
}

// les couleurs de la vue, d'après les jetons (docs/DESIGN-CAO.md §10.3)
void palette ()
{
	s_jet = (const unsigned *) ui_tokens ();
	bool dark = s_jet[TJ_DARK / 4] != 0;
	Palette &p = g_pal;
	p.dark = dark;
	p.bg = dark ? (unsigned) ui_mix (jet (TJ_FACE), jet (TJ_SUNKEN), 115) : (unsigned) ui_mix (jet (TJ_SURFACE), jet (TJ_SUNKEN), 128);
	p.grid = (unsigned) ui_mix (p.bg, jet (TJ_LINE_STRONG), 72);
	p.grid50 = (unsigned) ui_mix (p.bg, jet (TJ_LINE_STRONG), 154);
	p.accent = jet (TJ_ACCENT);
	p.hover = dark ? jet (TJ_ACCENT_VIVID) : (unsigned) ui_mix (jet (TJ_ACCENT), 0xFFFFFF, 97);
	p.edge = dark ? 0x0A0B0D : 0x14171C;	// (presque noir : le trait des arêtes, mêlé à la couleur du corps)
	p.axis[0] = jet (TJ_AXIS_X), p.axis[1] = jet (TJ_AXIS_Y), p.axis[2] = jet (TJ_AXIS_Z);
	p.cubeFace = jet (TJ_RAISED), p.cubeLine = jet (TJ_LINE_STRONG), p.cubeHot = jet (TJ_ACCENT_LIGHT);
}

// ---- la vue -----------------------------------------------------------------------------------------------------

void renderView ()
{
	long pitch = 0, w = 0;
	uint32_t *pix = (uint32_t *) uikit_call3 ((long (*) ()) ui_surface_pixels, g_surf, 0, 0, 0, &pitch, &w);
	if (!pix) return;
	g_view.render (pix, (int) (pitch / 4), g_doc, g_pal, g_sel, g_hot);
	ui_surface_damage (g_surf, 0, 0, 0, 0);
	char s[160];
	int tris = 0;
	for (auto &b : g_doc.bodies) tris += (int) b.mesh.triFace.size ();
	snprintf (s, sizeof s, "%d triangles   ·   rendu %.0f ms   ·   Grille 10 mm   ·   mm", tris, g_view.lastMs);
	ui_w_set_text (w_stats, (long) s);
}

// les zones de la vue qui passent par-dessus la surface
void overlays ()
{
	int cx, cy, cw, ch, bx, by;
	g_view.cubeRect (cx, cy, cw, ch);
	vbtnRect (0, bx, by);
	uint16_t r[16] = {
		(uint16_t) (VX + BP_X), (uint16_t) (VY + BP_Y), (uint16_t) BP_W, (uint16_t) bodiesH (),		// les corps
		(uint16_t) (VX + cx), (uint16_t) (VY + cy), (uint16_t) cw, (uint16_t) ch,			// le cube
		(uint16_t) (VX + bx), (uint16_t) (VY + by), (uint16_t) VB_W, (uint16_t) (VB_N * (VB_H + 6)),	// les boutons
		(uint16_t) VX, (uint16_t) (VY + VH - 80), 80, 80,						// le trièdre
	};
	ui_surface_overlay ((long) r, 4);
}

// ce qui est posé sur la vue, dessiné dans le contenu, sous la surface : invisible autour (octet haut 255), montré par
// ui_surface_overlay ; les zones redessinées sont montrées par ui_update
void paintOverlays ()
{
	const int X = VX, Y = VY;
	int cx, cy, cw, ch, bx0, by0;
	g_view.cubeRect (cx, cy, cw, ch);
	vbtnRect (0, bx0, by0);
	const int Z[4][4] = { { BP_X, BP_Y, BP_W, bodiesH () }, { cx, cy, cw, ch }, { bx0, by0, VB_W, VB_N * (VB_H + 6) }, { 0, VH - 80, 80, 80 } };
	for (auto &z : Z) ui_fill (X + z[0], Y + z[1], z[2], z[3], (long) 0xFF000000u);
	// les corps
	ui_rbox (X + BP_X, Y + BP_Y, BP_W, bodiesH (), 10, jet (TJ_LINE), jet (TJ_LINE));
	ui_rbox (X + BP_X + 1, Y + BP_Y + 1, BP_W - 2, bodiesH () - 2, 9, jet (TJ_SURFACE), jet (TJ_SURFACE));
	ui_text ((long) TR ("Corps"), X + BP_X + 14, Y + BP_Y + 25, jet (TJ_TEXT1), 13, (long) ui.font_b, 0);
	char n[16];
	snprintf (n, sizeof n, "%d", (int) g_doc.bodies.size ());
	ui_text ((long) n, X + BP_X + 62, Y + BP_Y + 25, jet (TJ_TEXT3), 13, (long) ui.font, 0);
	for (size_t i = 0; i < g_doc.bodies.size (); i++) {
		const Body &b = g_doc.bodies[i];
		int y = Y + BP_Y + 36 + (int) i * BP_ROW, x = X + BP_X;
		bool chosen = g_sel.body == (int) i && g_sel.face < 0 && g_sel.edge < 0;
		if (chosen) ui_fill (x + 6, y, BP_W - 12, BP_ROW - 2, jet (TJ_ACCENT_LIGHT));
		unsigned bg = chosen ? jet (TJ_ACCENT_LIGHT) : jet (TJ_SURFACE);
		// l'œil : rempli si le corps est montré
		ui_rbox (x + 14, y + 9, 14, 10, 5, jet (TJ_TEXT2), jet (TJ_TEXT2));
		ui_rbox (x + 15, y + 10, 12, 8, 4, b.visible ? jet (TJ_TEXT2) : bg, b.visible ? jet (TJ_TEXT2) : bg);
		ui_fill (x + 36, y + 8, 12, 12, bodyColor (b.color, g_pal.dark));
		ui_text ((long) b.name.c_str (), x + 56, y + 19, jet (b.visible ? TJ_TEXT1 : TJ_TEXT3), 13, (long) ui.font, BP_W - 130);
		Info in;
		bodyInfo (b, in);
		for (auto &r : in.rows)
			if (r.first == "Volume") {
				long tw = ui_text_width ((long) r.second.c_str (), 12, (long) ui.font);
				ui_text ((long) r.second.c_str (), x + BP_W - 12 - tw, y + 19, jet (TJ_TEXT3), 12, (long) ui.font, 0);
			}
	}
	// les étiquettes du cube de vue
	static const char *const CL[6] = { "DROITE", "GAUCHE", "ARRIÈRE", "AVANT", "DESSUS", "DESSOUS" };
	int lx[6], ly[6];
	bool vis[6];
	g_view.cubeLabels (lx, ly, vis);
	for (int f = 0; f < 6; f++) {
		if (!vis[f]) continue;
		long tw = ui_text_width ((long) TR (CL[f]), 10, (long) ui.font_b);
		unsigned face = f == g_view.hotCube ? g_pal.cubeHot : g_pal.cubeFace;	// (le texte sur la couleur de la face)
		ui_fill (X + lx[f] - tw / 2 - 2, Y + ly[f] - 6, tw + 4, 13, face);
		ui_text ((long) TR (CL[f]), X + lx[f] - tw / 2, Y + ly[f] + 4, jet (TJ_TEXT2), 10, (long) ui.font_b, 0);
	}
	// les boutons de la vue
	for (int i = 0; i < VB_N; i++) {
		int x, y;
		vbtnRect (i, x, y);
		x += X, y += Y;
		bool on = (i == 2 && g_view.edges) || (i == 3 && g_view.grid);
		ui_rbox (x, y, VB_W, VB_H, 8, jet (TJ_LINE), jet (TJ_LINE));
		unsigned face = on ? jet (TJ_ACCENT_LIGHT) : jet (TJ_SURFACE);
		ui_rbox (x + 1, y + 1, VB_W - 2, VB_H - 2, 7, face, face);
		long tw = ui_text_width ((long) TR (VBTN[i]), 12, (long) ui.font);
		ui_text ((long) TR (VBTN[i]), x + (VB_W - tw) / 2, y + 20, jet (on ? TJ_ACCENT_DARK : TJ_TEXT1), 12, (long) ui.font, 0);
	}
	// les lettres du trièdre
	double r[3], u[3], f[3];
	g_view.cam.basis (r, u, f);
	static const char *const AX[3] = { "X", "Y", "Z" };
	static const int TJA[3] = { TJ_AXIS_X, TJ_AXIS_Y, TJ_AXIS_Z };
	for (int a = 0; a < 3; a++) {
		int x = X + 44 + (int) (r[a] * 38), y = Y + VH - 44 - (int) (u[a] * 38);
		ui_text ((long) AX[a], x - 4, y + 5, jet (TJA[a]), 12, (long) ui.font_b, 0);
	}
	for (auto &z : Z) ui_update (X + z[0], Y + z[1], z[2], z[3]);
}

// ---- le panneau de propriétés -----------------------------------------------------------------------------------

void panelShow (const Info &in, const char *section, const char *state, unsigned stateCol)
{
	ui_w_set_text (p_title, (long) in.title.c_str ());
	ui_w_set_text (p_sub, (long) in.sub.c_str ());
	ui_w_set_text (p_sec, (long) (in.rows.empty () ? "" : section));
	for (int i = 0; i < 8; i++) {
		bool on = i < (int) in.rows.size ();
		ui_w_set_text (p_name[i], (long) (on ? in.rows[i].first.c_str () : ""));
		ui_w_set_text (p_val[i], (long) (on ? in.rows[i].second.c_str () : ""));
	}
	ui_w_set_text (p_state, (long) (state ? state : ""));
	(void) stateCol;
}

void panelUpdate ()
{
	Info in;
	if (g_selStep >= 0 && g_selStep < (int) g_doc.steps.size ()) {
		const Step &s = g_doc.steps[g_selStep];
		in.title = s.name;
		in.sub = "Étape " + std::to_string (g_selStep + 1) + " de l'historique";
		in.rows.push_back ({ "Valeurs", s.detail });
		panelShow (in, TR ("Étape"), 0, 0);
	} else if (g_sel.none ()) {
		in.title = g_doc.name;
		in.sub = TR ("Document · mm");
		in.rows.push_back ({ "Corps", std::to_string (g_doc.bodies.size ()) });
		in.rows.push_back ({ "Étapes", std::to_string (g_doc.steps.size ()) });
		in.rows.push_back ({ "Unité", "mm" });
		in.rows.push_back ({ "Précision", "0,01 mm" });
		panelShow (in, TR ("Document"), 0, 0);
	} else {
		const Body &b = g_doc.bodies[g_sel.body];
		if (g_sel.face >= 0) faceInfo (b, g_sel.face, in);
		else if (g_sel.edge >= 0) edgeInfo (b, g_sel.edge, in);
		else bodyInfo (b, in);
		const char *st = 0;
		if (g_sel.face < 0 && g_sel.edge < 0) st = in.solidOk ? TR ("✓ Solide fermé : prêt à fabriquer, à imprimer ou à exporter") : TR ("Pas un solide fermé");
		panelShow (in, TR ("Mesures"), st, 0);
	}
	ui_w_draw (0);
}

std::string pickName (const Pick &k)
{
	if (k.none ()) return "";
	Info in;
	const Body &b = g_doc.bodies[k.body];
	if (k.face >= 0) faceInfo (b, k.face, in);
	else if (k.edge >= 0) edgeInfo (b, k.edge, in);
	else return b.name;
	std::string s = in.title;
	for (auto &r : in.rows)
		if (r.first == "Diamètre") s += " · Ø " + r.second;
	return s;
}

void status (const char *s) { ui_w_set_text (w_status, (long) s); ui_w_draw (w_status); }

void hintIdle ()
{
	status (TR ("Clic : choisir. Bouton droit : tourner ; milieu : déplacer ; molette : zoomer."));
}

void select (const Pick &k)
{
	g_sel = k;
	g_selStep = -1;
	renderView ();
	paintOverlays ();
	ui_view_dirty (w_tl);
	ui_view_flush ();
	panelUpdate ();
}

void cameraChanged ()
{
	renderView ();
	paintOverlays ();
}

// ---- la souris sur la vue ---------------------------------------------------------------------------------------

int g_mx, g_my, g_downX, g_downY, g_btn;
bool g_dragged, g_overView;

// genre : 0 déplacement, 1 appui, 2 relâché, 3 sortie ; x, y dans la vue ; boutons : 1 gauche, 2 droit, 4 milieu
void viewMouse (int kind, int x, int y, int buttons)
{
	if (kind == 1) {		// appui
		g_downX = (int) x, g_downY = (int) y, g_btn = buttons, g_dragged = false;
		g_mx = (int) x, g_my = (int) y;
		return;
	}
	if (kind == 0) {		// déplacement
		int dx = (int) x - g_mx, dy = (int) y - g_my;
		g_mx = (int) x, g_my = (int) y;
		if (buttons & 2) {	// droit : tourner
			if (std::abs ((int) x - g_downX) + std::abs ((int) y - g_downY) > 3) g_dragged = true;
			g_view.orbit (-dx * 0.4, dy * 0.4);
			cameraChanged ();
			return;
		}
		if (buttons & 4) {	// milieu : déplacer
			g_view.pan (dx, dy);
			cameraChanged ();
			return;
		}
		if (buttons) return;
		// survol : le cube, sinon la pièce
		int hc = g_view.cubeFaceAt ((int) x, (int) y);
		Pick k = hc >= 0 ? Pick () : g_view.pick (g_doc, (int) x, (int) y);
		if (hc != g_view.hotCube || k != g_hot) {
			g_view.hotCube = hc;
			g_hot = k;
			renderView ();
			if (!k.none ()) {
				std::string s = pickName (k) + TR ("   ·   clic : la choisir");
				status (s.c_str ());
			} else
				hintIdle ();
		}
		return;
	}
	if (kind == 2) {		// relâché
		if (!(g_btn & 1) || g_dragged) return;
		int x0 = (int) x, y0 = (int) y;
		// les corps : l'œil montre ou cache, la ligne choisit le corps
		if (x0 >= BP_X && x0 < BP_X + BP_W && y0 >= BP_Y && y0 < BP_Y + bodiesH ()) {
			int i = (y0 - BP_Y - 36) / BP_ROW;
			if (y0 >= BP_Y + 36 && i >= 0 && i < (int) g_doc.bodies.size ()) {
				if (x0 < BP_X + 32) {
					g_doc.bodies[i].visible = !g_doc.bodies[i].visible;
					if (!g_doc.bodies[i].visible && g_sel.body == i) g_sel = Pick ();
					select (g_sel);
				} else {
					Pick k;
					k.body = i;
					select (k);
				}
			}
			return;
		}
		// les boutons de la vue
		for (int i = 0; i < VB_N; i++) {
			int bx, by;
			vbtnRect (i, bx, by);
			if (x0 >= bx && x0 < bx + VB_W && y0 >= by && y0 < by + VB_H) {
				if (i == 0) g_view.standard (6), g_view.fit (g_doc);
				else if (i == 1) g_view.fit (g_doc);
				else if (i == 2) g_view.edges = !g_view.edges, ui_menu_state (25, g_view.edges ? UMS_CHECKED : 0);
				else g_view.grid = !g_view.grid, ui_menu_state (26, g_view.grid ? UMS_CHECKED : 0);
				cameraChanged ();
				return;
			}
		}
		// le cube : la face vers soi
		int f = g_view.cubeFaceAt (x0, y0);
		if (f >= 0) {
			g_view.standard (f);
			cameraChanged ();
			return;
		}
		select (g_view.pick (g_doc, x0, y0));
		return;
	}
	if (kind == 3 && (!g_hot.none () || g_view.hotCube >= 0)) {	// sortie
		g_hot = Pick ();
		g_view.hotCube = -1;
		renderView ();
		hintIdle ();
	}
	return;
}

void viewWheel (long steps)
{
	g_view.zoomAt (g_mx, g_my, steps > 0 ? 1.18 : 1 / 1.18);
	cameraChanged ();
	return;
}

bool viewKey (long code)
{
	if (code == 0x29) {	// Échap : rien de choisi
		select (Pick ());
		hintIdle ();
		return true;
	}
	return false;
}

// ---- la frise ---------------------------------------------------------------------------------------------------

const int CHIP_W = 96, CHIP_GAP = 6, CHIP_X0 = 16;

void timelinePaint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_fill (0, 0, w, 1, jet (TJ_LINE));
	int y = 10, ch = (int) h - 20;
	for (size_t i = 0; i < g_doc.steps.size (); i++) {
		int x = CHIP_X0 + (int) i * (CHIP_W + CHIP_GAP);
		bool on = (int) i == g_selStep;
		unsigned bg = on ? jet (TJ_ACCENT_LIGHT) : jet (TJ_SURFACE);
		ui_rbox (x, y, CHIP_W, ch, 7, jet (on ? TJ_ACCENT : TJ_LINE), jet (on ? TJ_ACCENT : TJ_LINE));
		ui_rbox (x + 1, y + 1, CHIP_W - 2, ch - 2, 6, bg, bg);
		ui_text ((long) g_doc.steps[i].name.c_str (), x + 10, y + ch / 2 + 5, jet (on ? TJ_ACCENT_DARK : TJ_TEXT1), 12, (long) ui.font, CHIP_W - 14);
	}
	// la barre de reconstruction, après la dernière étape
	int bx = CHIP_X0 + (int) g_doc.steps.size () * (CHIP_W + CHIP_GAP) + 2;
	ui_fill (bx, 6, 2, (int) h - 12, jet (TJ_ACCENT));
	ui_fill (bx - 4, 4, 10, 4, jet (TJ_ACCENT));
	char s[64];
	snprintf (s, sizeof s, "%d étapes", (int) g_doc.steps.size ());
	long tw = ui_text_width ((long) s, 12, (long) ui.font);
	ui_text ((long) s, w - 16 - tw, h / 2 + 5, jet (TJ_TEXT3), 12, (long) ui.font, 0);
}

long timelineMouse (long, long kind, long x, long, long)
{
	if (kind != 1) return 0;
	int i = ((int) x - CHIP_X0) / (CHIP_W + CHIP_GAP);
	if (x < CHIP_X0 || i >= (int) g_doc.steps.size ()) i = -1;
	g_selStep = i;
	g_sel = Pick ();
	renderView ();
	paintOverlays ();
	ui_view_dirty (w_tl);
	ui_view_flush ();
	panelUpdate ();
	return 0;
}

// ---- la mise en page --------------------------------------------------------------------------------------------

void relayout ()
{
	int W = (int) ui.w, H = (int) ui.h, h = (int) ui_metric (UMT_CTL);
	TB_H = h + 24, TL_H = 56, ST_H = 26, PW = W >= 1200 ? 320 : 260;
	VX = 0, VY = TB_H, VW = imax (200, W - PW), VH = imax (160, H - TB_H - TL_H - ST_H);
	ui_w_move (w_doc, 16, 8, 150, 20);
	ui_w_move (w_docsub, 16, 28, 150, 16);
	ui_w_move (w_mode, 172, (TB_H - h) / 2, 220, h);
	ui_w_move (w_export, W - 96 - 120, (TB_H - h) / 2, 108, h);
	ui_w_move (w_tl, 0, VY + VH, VW, TL_H);
	int px = W - PW + 20, pw = PW - 40, y = TB_H + 18;
	ui_w_move (p_title, px, y, pw, 26);
	ui_w_move (p_sub, px, y + 26, pw, 18);
	ui_w_move (p_sec, px, y + 62, pw, 20);
	for (int i = 0; i < 8; i++) {
		ui_w_move (p_name[i], px, y + 90 + i * 28, pw / 2, 22);
		ui_w_move (p_val[i], px + pw / 2, y + 90 + i * 28, pw - pw / 2, 22);
	}
	ui_w_move (p_state, px, y + 90 + 8 * 28 + 10, pw, 40);
	ui_w_move (p_hint, px, H - ST_H - 120, pw, 100);
	ui_w_move (w_status, 12, H - ST_H + 3, W * 3 / 5, ST_H - 6);
	ui_w_move (w_stats, W * 3 / 5, H - ST_H + 3, W - W * 3 / 5 - 12, ST_H - 6);
	if (g_surf) ui_surface_move (g_surf, VX, VY, VW, VH);
	else g_surf = ui_surface (VX, VY, VW, VH, WS_SF_XRGB8888);
	g_view.resize (VW, VH);
	overlays ();
}

void repaint ()
{
	int W = (int) ui.w, H = (int) ui.h;
	ui_fill (0, 0, W, H, jet (TJ_FACE));
	ui_fill (0, TB_H - 1, W, 1, jet (TJ_LINE));
	ui_fill (W - PW, TB_H, PW, H - TB_H - ST_H, jet (TJ_FACE));
	ui_fill (W - PW, TB_H, 1, H - TB_H - ST_H, jet (TJ_LINE));
	ui_fill (0, H - ST_H, W, ST_H, jet (TJ_BAR));
	ui_fill (0, H - ST_H, W, 1, jet (TJ_LINE));
	ui_w_draw (0);
	paintOverlays ();
	ui_update_all ();
	renderView ();
}

long onResize (long, long)
{
	relayout ();
	repaint ();
	return 0;
}

// ---- les menus --------------------------------------------------------------------------------------------------

const char MENUS[] =
	"MFichier\n" "I9\tQuitter\tCtrl+Q\n"
	"MAffichage\n" "I20\tVue de face\tAlt+1\n" "I21\tVue de dessus\tAlt+2\n" "I22\tVue de droite\tAlt+3\n"
	"I23\tVue isométrique\tAlt+0\n" "-\n" "I24\tAjuster\tF6\n" "-\n" "I25c\tArêtes\n" "I26c\tGrille\n"
	"MAide\n" "I40\tNaviguer dans la vue\n";

void command (int id)
{
	switch (id) {
	case 9: g_quit = true; break;
	case 20: g_view.standard (3); cameraChanged (); break;
	case 21: g_view.standard (4); cameraChanged (); break;
	case 22: g_view.standard (0); cameraChanged (); break;
	case 23: g_view.standard (6); cameraChanged (); break;
	case 24: g_view.fit (g_doc); cameraChanged (); break;
	case 25: g_view.edges = !g_view.edges; ui_menu_state (25, g_view.edges ? UMS_CHECKED : 0); cameraChanged (); break;
	case 26: g_view.grid = !g_view.grid; ui_menu_state (26, g_view.grid ? UMS_CHECKED : 0); cameraChanged (); break;
	case 40: hintIdle (); break;
	}
}

void buildWindow ()
{
	palette ();
	int h = (int) ui_metric (UMT_CTL);
	w_doc = ui_label (0, 0, 10, 20, (long) g_doc.name.c_str (), ULB_BOLD);
	w_docsub = ui_label (0, 0, 10, 16, (long) TR ("Exemple · mm"), ULB_DIM);
	w_mode = ui_segmented (0, 0, 220, h, (long) TR ("Conception|Fabrication"));
	ui_w_tip (w_mode, (long) TR ("Fabrication (G-code GRBL) : à une étape suivante"));
	TB_H = h + 24;
	w_bar = ui_toolbar (408, (TB_H - h) / 2, imax (100, (int) ui.w - 408 - 96 - 128), 0);	// (à sa place : ses boutons ne suivent pas ui_w_move)
	static const char *const TOOLS[] = { "Esquisse", "Boîte", "Cylindre", "Extruder", "|Congé", "Chanfrein", "|Union", "Soustraire", "|Mesurer" };
	for (const char *t : TOOLS) {
		bool sep = t[0] == '|';
		long b = ui_toolbar_add (w_bar, 0, (long) TR (sep ? t + 1 : t), sep ? UTB_SEP : 0);
		ui_w_enable (b, 0);
		ui_w_tip (b, (long) TR ("Les outils arrivent aux étapes suivantes (docs/DESIGN-CAO.md §14)"));
	}
	w_export = ui_pushbutton (0, 0, 108, h, (long) TR ("Exporter"), UB_DEFAULT);
	ui_w_enable (w_export, 0);
	w_tl = ui_view (0, 0, 100, 56, 0);
	ui_view_on (w_tl, (long) timelinePaint, (long) timelineMouse, 0, 0);
	p_title = ui_label (0, 0, 10, 26, (long) "", ULB_TITLE);
	p_sub = ui_label (0, 0, 10, 18, (long) "", ULB_DIM);
	p_sec = ui_label (0, 0, 10, 20, (long) "", ULB_BOLD);
	for (int i = 0; i < 8; i++) {
		p_name[i] = ui_label (0, 0, 10, 22, (long) "", ULB_DIM);
		p_val[i] = ui_label (0, 0, 10, 22, (long) "", ULB_RIGHT);
	}
	p_state = ui_label (0, 0, 10, 40, (long) "", ULB_WRAP);
	p_hint = ui_label (0, 0, 10, 100, (long) TR ("Clic : choisir une face, une arête ; dans Corps, un corps. Échap : rien. Le cube : une vue type. Les outils de modélisation arrivent aux étapes suivantes."), ULB_WRAP | ULB_DIM);
	w_status = ui_label (0, 0, 10, 20, (long) "", ULB_DIM);
	w_stats = ui_label (0, 0, 10, 20, (long) "", ULB_DIM | ULB_RIGHT);
	ui_menu ((long) MENUS);
	ui_menu_state (25, UMS_CHECKED);
	ui_menu_state (26, UMS_CHECKED);
}

// ---- l'essai (--selftest) ---------------------------------------------------------------------------------------

int fails;
void check (const char *what, bool ok, const std::string &detail)
{
	printf ("%s %s%s%s\n", ok ? "ok   " : "ECHEC", what, detail.empty () ? "" : " : ", detail.c_str ());
	fflush (stdout);
	if (!ok) fails++;
}

double nowMs ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

int selftest (double buildMs, double meshMs)
{
	char s[200];
	const Body &b = g_doc.bodies[0];
	Info in;
	bodyInfo (b, in);
	std::string vol;
	for (auto &r : in.rows) if (r.first == "Volume") vol = r.second;
	snprintf (s, sizeof s, "%d faces, %d arêtes, %d triangles, volume %s, construite %.0f ms, maillée %.0f ms", b.mesh.nfaces, b.mesh.nedges, (int) b.mesh.triFace.size (), vol.c_str (), buildMs, meshMs);
	check ("pièce d'exemple (OpenCASCADE)", in.solidOk && b.mesh.nfaces == 24 && vol == "63,99 cm³", s);
	snprintf (s, sizeof s, "%d × %d px en %.0f ms", VW, VH, g_view.lastMs);
	check ("rendu de la vue", g_view.lastMs > 0, s);
	// une face : le premier pixel de la vue qui en montre une, choisi comme par un clic
	Pick k;
	int fx = -1, fy = -1;
	for (int y = VH / 3; y < VH && k.face < 0; y += 7)
		for (int x = VW / 3; x < VW * 2 / 3 && k.face < 0; x += 7) {
			Pick t = g_view.pick (g_doc, x, y);
			if (t.face >= 0) k = t, fx = x, fy = y;
		}
	select (k);
	snprintf (s, sizeof s, "en (%d, %d) : %s", fx, fy, pickName (k).c_str ());
	check ("choix d'une face", k.face >= 0, s);
	// une arête : le milieu projeté d'une arête vue
	Pick e;
	for (int y = 0; y < VH && e.edge < 0; y += 3)
		for (int x = 0; x < VW && e.edge < 0; x += 3) {
			Pick t = g_view.pick (g_doc, x, y);
			if (t.edge >= 0) e = t;
		}
	check ("choix d'une arête", e.edge >= 0, pickName (e));
	// les vues types et le cadrage
	g_view.standard (4);
	g_view.fit (g_doc);
	double t0 = nowMs ();
	renderView ();
	snprintf (s, sizeof s, "rendu %.0f ms (mesuré %.0f ms)", g_view.lastMs, nowMs () - t0);
	check ("vue de dessus, ajustée", true, s);
	int cx, cy, cw, ch;
	g_view.cubeRect (cx, cy, cw, ch);
	check ("cube de vue : la face du dessus au centre", g_view.cubeFaceAt (cx + cw / 2, cy + ch / 2) == 4, "");
	g_view.standard (6);
	g_view.fit (g_doc);
	g_view.zoomAt (VW / 2, VH / 2, 1.5);
	g_view.orbit (20, 5);
	cameraChanged ();
	check ("tourner et zoomer", g_view.cam.scale > 0, "");
	select (Pick ());
	printf ("forge : %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}

// la boucle des événements (untilMs : jusqu'à ce moment, 0 : jusqu'à Quitter)
void loop (double untilMs)
{
	while (!g_quit && (!untilMs || nowMs () < untilMs)) {
		long t = ui_event (50);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command ((int) ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_THEME) {
			palette ();
			repaint ();
			ui_view_dirty (w_tl);
			ui_view_flush ();
		} else if (t > 0) {
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			// la vue n'a pas de widget : sa souris est lue brute (aussi hors d'elle pendant un glisser)
			int ex = ui_ev_field (&ui, EV_X) - VX, ey = ui_ev_field (&ui, EV_Y) - VY;
			bool inView = ex >= 0 && ey >= 0 && ex < VW && ey < VH;
			if (t == WEV_MOUSE) g_overView = inView;
			if (t == WEV_MOUSE && !w && (inView || g_btn)) {
				int kind = ui_ev_field (&ui, EV_KIND), b = ui_ev_field (&ui, EV_CODE) & 0xFF;
				viewMouse (kind, ex, ey, b);
				if (kind == 2) g_btn = 0;
			} else if (t == WEV_MOUSE && !inView && (!g_hot.none () || g_view.hotCube >= 0))
				viewMouse (3, ex, ey, 0);
			if (t == WEV_WHEEL && !w && g_overView) viewWheel (ui_ev_field (&ui, EV_Y));	// (au dernier point du pointeur)
			if (press && !taken && viewKey (ui_ev_field (&ui, EV_CODE))) continue;
			if (press) {
				long id = ui_menu_key (0);
				if (id >= 0) command ((int) id);
			}
		} else
			ui_w_idle ();
	}
}

} // namespace

int main (int argc, char **argv)
{
	bool test = argc > 1 && !strcmp (argv[1], "--selftest");
	if (!uikit_open (&ui)) return 1;
	double t0 = nowMs ();
	std::string err;
	if (!buildSample (g_doc, err)) {
		printf ("forge : la pièce d'exemple n'a pas pu être faite (%s)\n", err.c_str ());
		return 1;
	}
	double t1 = nowMs ();
	for (auto &b : g_doc.bodies) meshBody (b, 0.05);
	double t2 = nowMs ();
	long scr[4] = { 1280, 800, 0, 0 };
	ui_screen ((long) scr);
	int ww = imin (1600, (int) scr[0] - 16), wh = imin (1000, (int) (scr[1] - scr[3]) - imax ((int) scr[2], 60) - 16);
	if (ui_window_ex (8, scr[3] + 4, ww, wh, (long) "Forge", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (800, 520);
	ui_on_resize ((long) onResize, 0);
	buildWindow ();
	relayout ();
	g_view.fit (g_doc);
	repaint ();
	panelUpdate ();
	hintIdle ();
	if (test) {
		int r = selftest (t1 - t0, t2 - t1);
		if (argc < 3) return r;		// (forge --selftest N : la fenêtre reste N secondes, pour une capture)
		loop (nowMs () + atoi (argv[2]) * 1000.0);
		return r;
	}
	loop (0);
	return 0;
}
