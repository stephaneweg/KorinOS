//
// Forge -- la CAO de Korin (docs/DESIGN-CAO.md) : OpenCASCADE pour les solides, PlaneGCS pour les esquisses ; l'interface
// de 3DForge d'Onyx (clic-déplacer-clic, cotes sur le dessin, frise) façon Fusion et Shapr3D.
//
// Étape 1 (§14) : la fenêtre (barre, vue, frise, panneau de propriétés, barre d'état), la vue 3D, le survol et le
// choix d'une face, d'une arête ou d'un corps avec leurs mesures, le panneau flottant des corps.
// Étape 2 : l'historique paramétrique (history.cpp) ; les formes au geste (boîte, cylindre : clic, on s'éloigne, clic,
// on monte, clic), pousser/tirer une face, congé et chanfrein sur les arêtes choisies, union et soustraction de deux
// corps ; les puces de cote sur le dessin, qui prennent le clavier pendant le geste (formules et unités) ; l'opération
// choisie pour vous (joindre, enlever, nouveau corps), changeable ; la frise modifiable (valeurs d'une étape, la barre
// de reconstruction glissée, désactiver, supprimer, étapes en erreur) ; annuler et rétablir.
//
// La vue est rendue par le processeur dans une surface d'UIKit (render.cpp), en attendant le chemin GPU (GlKit,
// tools/mesa/armgl/glview.h). Ce qui est posé sur elle (corps, boutons de vue, étiquettes du cube et du trièdre, puces
// de cote) est dessiné par UIKit dans le contenu de la fenêtre, sous la surface, et passe par-dessus par
// ui_surface_overlay ; aucun widget sous la vue (un ui_view a son propre tampon, sans la transparence) : la souris y
// est lue brute.
//
//   forge                la pièce d'exemple
//   forge --selftest [s] les mêmes, puis des vérifications écrites sur la sortie (essais sous QEMU) ; code 0 si tout
//                        passe ; s : la fenêtre reste ouverte s secondes ensuite (gestes et captures)
//
// C++ sur UIKit, appels ui_* directs, couleurs par les jetons du thème (aucune en dur, sauf la palette des corps, qui
// est une donnée du document).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
//
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <array>
#include <string>
#include <vector>
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

double nowMs ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

Doc g_doc;
View g_view;
Palette g_pal;
std::vector<Pick> g_sels;		// ce qui est choisi (Maj+clic : plusieurs arêtes)
Pick g_hot;
std::vector<char> g_hidden;		// les corps cachés (par rang de création)
int g_selStep = -1;			// l'étape choisie dans la frise (-1 : aucune)
long g_surf;				// la surface de la vue
int VX, VY, VW, VH;			// la zone de la vue dans le contenu
int TB_H, TL_H, ST_H, PW;		// barre, frise, état, panneau
bool g_quit;

Pick sel0 () { return g_sels.empty () ? Pick () : g_sels[0]; }

// widgets
long w_doc, w_docsub, w_mode, w_bar, w_export, w_tl, w_status, w_stats;
long p_title, p_sub, p_sec, p_name[8], p_val[8], p_state, p_hint;
long w_op, b_ok, b_cancel;			// la commande en cours
long e_val[3], b_apply, b_off, b_del, b_barhere;	// l'étape choisie
enum { TB_SKETCH, TB_BOX, TB_CYL, TB_PUSH, TB_FILLET, TB_CHAMFER, TB_UNION, TB_CUT, TB_MEASURE, TB_N };
long tb[TB_N];

// ---- annuler, rétablir ------------------------------------------------------------------------------------------
struct Snap {
	std::vector<Feature> feats;
	int bar;
};
std::vector<Snap> g_undo, g_redo;
void pushUndo ()
{
	g_undo.push_back ({ g_doc.feats, g_doc.bar });
	if (g_undo.size () > 100) g_undo.erase (g_undo.begin ());
	g_redo.clear ();
}

// ---- la commande en cours ---------------------------------------------------------------------------------------
enum Tool { T_NONE, T_BOX, T_CYL, T_PUSH, T_FILLET, T_CHAMFER, T_UNION, T_CUT };
struct Chip {
	const char *label;
	double val = 0;
	bool locked = false;		// tapée : le geste ne la change plus
	std::string text;		// en train d'être tapée
	double at[3] = { 0, 0, 0 };	// où elle est posée (espace)
	int sx = 0, sy = 0, sw = 0;	// sa place à l'écran (dans la vue) au dernier dessin
};
struct Cmd {
	Tool tool = T_NONE;
	int stage = 0;
	Feature f;
	std::vector<Chip> chips;
	int active = 0;
	bool opAuto = true;
	int target = -1;		// le corps sous le premier clic (-1 : le sol)
	Mesh ghost;
	std::string err;
} g_cmd;

const char *toolName (Tool t)
{
	static const char *const N[] = { "", "Boîte", "Cylindre", "Pousser/tirer", "Congé", "Chanfrein", "Union", "Soustraction" };
	return N[t];
}

// ---- le panneau flottant des corps et les boutons de la vue -----------------------------------------------------
const int BP_X = 12, BP_Y = 12, BP_W = 232, BP_ROW = 30;
const char *const VBTN[] = { "Maison", "Ajuster", "Arêtes", "Grille" };
const int VB_N = 4, VB_W = 76, VB_H = 30;

int liveBodies ()
{
	int n = 0;
	for (auto &b : g_doc.bodies) n += !b.shape.IsNull ();
	return n;
}
int bodiesH () { return 40 + BP_ROW * imax (1, liveBodies ()); }
void vbtnRect (int i, int &x, int &y)
{
	int cx, cy, cw, ch;
	g_view.cubeRect (cx, cy, cw, ch);
	x = cx + (cw - VB_W) / 2;
	y = cy + ch + 8 + i * (VB_H + 6);
}

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
	p.danger = jet (TJ_DANGER);
}

// ---- la vue -----------------------------------------------------------------------------------------------------

void renderView ()
{
	long pitch = 0, w = 0;
	uint32_t *pix = (uint32_t *) uikit_call3 ((long (*) ()) ui_surface_pixels, g_surf, 0, 0, 0, &pitch, &w);
	if (!pix) return;
	g_view.ghost = g_cmd.tool != T_NONE && !g_cmd.ghost.pts.empty () ? &g_cmd.ghost : nullptr;
	g_view.ghostCut = g_cmd.f.op == OP_CUT || (g_cmd.tool == T_PUSH && g_cmd.f.a < 0);
	g_view.render (pix, (int) (pitch / 4), g_doc, g_pal, g_sels, g_hot);
	ui_surface_damage (g_surf, 0, 0, 0, 0);
	char s[160];
	int tris = 0;
	for (auto &b : g_doc.bodies) tris += (int) b.mesh.triFace.size ();
	snprintf (s, sizeof s, "%d triangles   ·   rendu %.0f ms   ·   Accrochage 1 mm   ·   mm", tris, g_view.lastMs);
	ui_w_set_text (w_stats, (long) s);
}

// les zones de la vue posées par-dessus la surface : corps, cube, boutons, trièdre, puces de cote
void paintOverlays ()
{
	const int X = VX, Y = VY;
	int cx, cy, cw, ch, bx0, by0;
	g_view.cubeRect (cx, cy, cw, ch);
	vbtnRect (0, bx0, by0);
	std::vector<std::array<int, 4>> Z = { { BP_X, BP_Y, BP_W, bodiesH () }, { cx, cy, cw, ch }, { bx0, by0, VB_W, VB_N * (VB_H + 6) }, { 0, VH - 80, 80, 80 } };
	// les puces de cote : leur place
	for (auto &c : g_cmd.chips) {
		double sx, sy;
		g_view.toScreen (c.at, sx, sy);
		std::string t = std::string (TR (c.label)) + "  " + (c.text.empty () ? fmtNum (c.val) : c.text);
		c.sw = (int) ui_text_width ((long) t.c_str (), 13, (long) ui.font) + 24;
		c.sx = imax (2, imin (VW - c.sw - 2, (int) sx + 10)), c.sy = imax (2, imin (VH - 28, (int) sy - 30));
		Z.push_back ({ c.sx, c.sy, c.sw, 26 });
	}
	for (auto &z : Z) ui_fill (X + z[0], Y + z[1], z[2], z[3], (long) 0xFF000000u);
	// les corps
	ui_rbox (X + BP_X, Y + BP_Y, BP_W, bodiesH (), 10, jet (TJ_LINE), jet (TJ_LINE));
	ui_rbox (X + BP_X + 1, Y + BP_Y + 1, BP_W - 2, bodiesH () - 2, 9, jet (TJ_SURFACE), jet (TJ_SURFACE));
	ui_text ((long) TR ("Corps"), X + BP_X + 14, Y + BP_Y + 25, jet (TJ_TEXT1), 13, (long) ui.font_b, 0);
	char n[16];
	snprintf (n, sizeof n, "%d", liveBodies ());
	ui_text ((long) n, X + BP_X + 62, Y + BP_Y + 25, jet (TJ_TEXT3), 13, (long) ui.font, 0);
	int row = 0;
	for (size_t i = 0; i < g_doc.bodies.size (); i++) {
		const Body &b = g_doc.bodies[i];
		if (b.shape.IsNull ()) continue;
		int y = Y + BP_Y + 36 + row++ * BP_ROW, x = X + BP_X;
		bool chosen = sel0 ().body == (int) i && sel0 ().face < 0 && sel0 ().edge < 0;
		if (chosen) ui_fill (x + 6, y, BP_W - 12, BP_ROW - 2, jet (TJ_ACCENT_LIGHT));
		unsigned bg = chosen ? jet (TJ_ACCENT_LIGHT) : jet (TJ_SURFACE);
		ui_rbox (x + 14, y + 9, 14, 10, 5, jet (TJ_TEXT2), jet (TJ_TEXT2));
		ui_rbox (x + 15, y + 10, 12, 8, 4, b.visible ? jet (TJ_TEXT2) : bg, b.visible ? jet (TJ_TEXT2) : bg);
		ui_fill (x + 36, y + 8, 12, 12, bodyColor (b.color, g_pal.dark));
		ui_text ((long) b.name.c_str (), x + 56, y + 19, jet (b.visible ? TJ_TEXT1 : TJ_TEXT3), 13, (long) ui.font, BP_W - 130);
		std::string v = fmtNum (std::round (bodyVolume (b) / 10) / 100) + " cm³";
		long tw = ui_text_width ((long) v.c_str (), 12, (long) ui.font);
		ui_text ((long) v.c_str (), x + BP_W - 12 - tw, y + 19, jet (TJ_TEXT3), 12, (long) ui.font, 0);
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
	// les puces de cote : la cote active a le clavier (trait d'accent) ; tapée, elle est tenue (gras)
	for (size_t i = 0; i < g_cmd.chips.size (); i++) {
		Chip &c = g_cmd.chips[i];
		bool act = (int) i == g_cmd.active;
		bool bad = act && !g_cmd.err.empty ();
		unsigned ln = bad ? jet (TJ_DANGER) : act ? jet (TJ_ACCENT) : jet (TJ_LINE_STRONG);
		ui_rbox (X + c.sx, Y + c.sy, c.sw, 26, 7, ln, ln);
		ui_rbox (X + c.sx + 1 + act, Y + c.sy + 1 + act, c.sw - 2 - 2 * act, 24 - 2 * act, 6, jet (TJ_SURFACE), jet (TJ_SURFACE));
		ui_text ((long) TR (c.label), X + c.sx + 10, Y + c.sy + 18, jet (TJ_TEXT3), 13, (long) ui.font, 0);
		long lw = ui_text_width ((long) TR (c.label), 13, (long) ui.font);
		std::string v = c.text.empty () ? fmtNum (c.val) : c.text + (act ? "|" : "");
		ui_text ((long) v.c_str (), X + c.sx + 16 + lw, Y + c.sy + 18, jet (TJ_TEXT1), 13, (long) (c.locked ? ui.font_b : ui.font), 0);
	}
	for (auto &z : Z) ui_update (X + z[0], Y + z[1], z[2], z[3]);
	// tout ce qui passe par-dessus la surface (WS_OVER_MAX : 32)
	std::vector<uint16_t> rr;
	for (auto &z : Z)
		for (int k = 0; k < 4; k++) rr.push_back ((uint16_t) (k == 0 ? X + z[0] : k == 1 ? Y + z[1] : z[k]));
	ui_surface_overlay ((long) rr.data (), (long) imin ((int) Z.size (), 32));
}

// ---- le panneau de propriétés -----------------------------------------------------------------------------------

void showCmdWidgets (bool on)
{
	ui_w_show (w_op, on);
	ui_w_show (b_ok, on);
	ui_w_show (b_cancel, on);
}

void showEditWidgets (int fields, bool on)
{
	for (int i = 0; i < 3; i++) ui_w_show (e_val[i], on && i < fields);
	ui_w_show (b_apply, on && fields > 0);
	ui_w_show (b_off, on);
	ui_w_show (b_del, on);
	ui_w_show (b_barhere, on);
}

void panelRows (const std::vector<std::pair<std::string, std::string>> &rows, int skipVal = 0)
{
	for (int i = 0; i < 8; i++) {
		bool on = i < (int) rows.size ();
		ui_w_set_text (p_name[i], (long) (on ? rows[i].first.c_str () : ""));
		ui_w_set_text (p_val[i], (long) (on && i >= skipVal ? rows[i].second.c_str () : ""));
	}
}

// les valeurs modifiables d'une étape : nom, valeur affichée
int featFields (const Feature &f, const char *names[3], double vals[3])
{
	switch (f.kind) {
	case ST_BOX: names[0] = "Largeur", names[1] = "Profondeur", names[2] = "Hauteur", vals[0] = f.a, vals[1] = f.b, vals[2] = f.c; return 3;
	case ST_CYLINDER: names[0] = "Diamètre", names[1] = "Hauteur", vals[0] = 2 * f.a, vals[1] = f.c; return 2;
	case ST_FILLET: names[0] = "Rayon", vals[0] = f.a; return 1;
	case ST_CHAMFER: names[0] = "Distance", vals[0] = f.a; return 1;
	case ST_PUSH: names[0] = "Distance", vals[0] = f.a; return 1;
	case ST_HOLE: names[0] = "Diamètre", names[1] = "Ø du lamage", names[2] = "Prof. du lamage", vals[0] = f.d1, vals[1] = f.d2, vals[2] = f.dep2; return 3;
	case ST_SLOT: names[0] = "Longueur", names[1] = "Largeur", vals[0] = f.a, vals[1] = f.b; return 2;
	default: return 0;
	}
}

void featSet (Feature &f, int i, double v)
{
	switch (f.kind) {
	case ST_BOX: (i == 0 ? f.a : i == 1 ? f.b : f.c) = v; break;
	case ST_CYLINDER: if (i == 0) f.a = v / 2; else f.c = v; break;
	case ST_FILLET: case ST_CHAMFER: case ST_PUSH: f.a = v; break;
	case ST_HOLE: (i == 0 ? f.d1 : i == 1 ? f.d2 : f.dep2) = v; break;
	case ST_SLOT: (i == 0 ? f.a : f.b) = v; break;
	default: break;
	}
}

void panelUpdate ()
{
	Info in;
	showCmdWidgets (false);
	showEditWidgets (0, false);
	std::string state;
	if (g_cmd.tool != T_NONE) {
		static const char *const HINT[8][3] = {
			{ "", "", "" },
			{ "Clic : le centre de la base, sur une face ou sur le sol", "On s'éloigne pour la largeur et la profondeur ; clic", "On monte pour la hauteur ; clic ou Entrée" },
			{ "Clic : le centre de la base, sur une face ou sur le sol", "On s'éloigne pour le rayon ; clic", "On monte pour la hauteur ; clic ou Entrée" },
			{ "Clic : la face plane à pousser ou à tirer", "Glisser pour la distance ; clic ou Entrée", "" },
			{ "Clic : les arêtes (Maj : plusieurs) ; Entrée", "Tapez le rayon ; Entrée", "" },
			{ "Clic : les arêtes (Maj : plusieurs) ; Entrée", "Tapez la distance ; Entrée", "" },
			{ "Clic : le corps qui reste", "Clic : le corps à joindre", "" },
			{ "Clic : le corps à garder", "Clic : le corps à retirer", "" },
		};
		ui_w_set_text (p_title, (long) TR (toolName (g_cmd.tool)));
		ui_w_set_text (p_sub, (long) TR (HINT[g_cmd.tool][imin (g_cmd.stage, 2)]));
		ui_w_set_text (p_sec, (long) TR ("Valeurs"));
		std::vector<std::pair<std::string, std::string>> rows;
		for (auto &c : g_cmd.chips) rows.push_back ({ TR (c.label), (c.text.empty () ? fmtNum (c.val) : c.text) + " mm" + (c.locked ? "  (tenue)" : "") });
		bool hasOp = g_cmd.tool == T_BOX || g_cmd.tool == T_CYL;
		static const char *const OPS[4] = { "nouveau", "joindre", "enlever", "intersection" };
		if (hasOp) rows.push_back ({ TR ("Opération"), std::string (TR (OPS[g_cmd.f.op])) + (g_cmd.opAuto ? TR (" · auto") : "") });
		if (g_cmd.tool == T_FILLET || g_cmd.tool == T_CHAMFER) rows.push_back ({ TR ("Arêtes"), std::to_string (g_cmd.f.edges.size ()) });
		panelRows (rows);
		if (hasOp) {
			ui_w_show (w_op, true);
			ui_w_set_value (w_op, g_cmd.f.op == OP_NEW ? 0 : g_cmd.f.op == OP_JOIN ? 1 : g_cmd.f.op == OP_CUT ? 2 : 3);
		}
		ui_w_show (b_ok, true);
		ui_w_show (b_cancel, true);
		state = g_cmd.err.empty () ? "" : "⚠ " + g_cmd.err;
	} else if (g_selStep >= 0 && g_selStep < (int) g_doc.feats.size ()) {
		const Feature &f = g_doc.feats[g_selStep];
		ui_w_set_text (p_title, (long) f.name.c_str ());
		std::string sub = std::string (TR ("Étape ")) + std::to_string (g_selStep + 1) + (g_selStep >= g_doc.bar ? TR (" · après la barre, pas jouée") : f.off ? TR (" · désactivée") : "");
		ui_w_set_text (p_sub, (long) sub.c_str ());
		ui_w_set_text (p_sec, (long) TR ("Valeurs"));
		const char *names[3];
		double vals[3];
		int n = featFields (f, names, vals);
		std::vector<std::pair<std::string, std::string>> rows;
		for (int i = 0; i < n; i++) rows.push_back ({ TR (names[i]), "" });
		rows.push_back ({ TR ("Résumé"), featureDetail (f) });
		panelRows (rows);
		for (int i = 0; i < n; i++) ui_w_set_text (e_val[i], (long) fmtNum (vals[i]).c_str ());
		showEditWidgets (n, true);
		ui_w_set_text (b_off, (long) TR (f.off ? "Activer" : "Désactiver"));
		state = f.error.empty () ? "" : "⚠ " + f.error;
	} else if (sel0 ().none ()) {
		ui_w_set_text (p_title, (long) g_doc.name.c_str ());
		ui_w_set_text (p_sub, (long) TR ("Document · mm"));
		ui_w_set_text (p_sec, (long) TR ("Document"));
		int err = 0;
		for (auto &f : g_doc.feats) err += !f.error.empty ();
		panelRows ({ { TR ("Corps"), std::to_string (liveBodies ()) }, { TR ("Étapes"), std::to_string (g_doc.feats.size ()) },
			     { TR ("En erreur"), std::to_string (err) }, { TR ("Unité"), "mm" }, { TR ("Précision"), "0,01 mm" } });
	} else {
		Pick k = sel0 ();
		const Body &b = g_doc.bodies[k.body];
		if (k.face >= 0) faceInfo (b, k.face, in);
		else if (k.edge >= 0) edgeInfo (b, k.edge, in);
		else bodyInfo (b, in);
		if (g_sels.size () > 1) in.title = std::to_string (g_sels.size ()) + (k.edge >= 0 ? " arêtes" : " faces"), in.sub = b.name;
		ui_w_set_text (p_title, (long) in.title.c_str ());
		ui_w_set_text (p_sub, (long) in.sub.c_str ());
		ui_w_set_text (p_sec, (long) TR ("Mesures"));
		panelRows (in.rows);
		if (k.face < 0 && k.edge < 0) state = in.solidOk ? TR ("✓ Solide fermé : prêt à fabriquer, à imprimer ou à exporter") : TR ("Pas un solide fermé");
		else if (k.edge >= 0) state = TR ("F : congé, Maj+F : chanfrein sur les arêtes choisies (Maj+clic : en ajouter)");
		else {
			Frame fr;
			if (facePlane (b, k.face, fr)) state = TR ("Q : pousser ou tirer cette face ; B : une boîte posée dessus");
		}
	}
	ui_w_set_text (p_state, (long) state.c_str ());
	int W = (int) ui.w, H = (int) ui.h;
	ui_fill (W - PW + 1, TB_H, PW - 1, H - TB_H - ST_H, jet (TJ_FACE));	// (les widgets cachés ne laissent rien)
	ui_w_draw (0);
	ui_update (W - PW, TB_H, PW, H - TB_H - ST_H);
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
void hintIdle () { status (TR ("Clic : choisir. Bouton droit : tourner ; milieu : déplacer ; molette : zoomer. B boîte, Q pousser, F congé.")); }

void refreshAll ()
{
	renderView ();
	paintOverlays ();
	ui_view_dirty (w_tl);
	ui_view_flush ();
	panelUpdate ();
}

void select (const std::vector<Pick> &k)
{
	g_sels = k;
	g_selStep = -1;
	refreshAll ();
}
void select (const Pick &k) { select (k.none () ? std::vector<Pick> () : std::vector<Pick> { k }); }

void cameraChanged ()
{
	renderView ();
	paintOverlays ();
}

// rejoue l'historique et garde ce qui peut l'être (corps cachés, choix encore valables)
void rebuild ()
{
	replay (g_doc);
	g_hidden.resize (g_doc.bodies.size (), 0);
	for (size_t i = 0; i < g_doc.bodies.size (); i++)
		if (g_hidden[i]) g_doc.bodies[i].visible = false;
	std::vector<Pick> keep;
	for (auto &k : g_sels)
		if (k.body < (int) g_doc.bodies.size () && !g_doc.bodies[k.body].shape.IsNull () && k.face < g_doc.bodies[k.body].mesh.nfaces && k.edge < g_doc.bodies[k.body].mesh.nedges)
			keep.push_back (k);
	g_sels.clear ();		// (les numéros de faces changent au rejeu : ne garder que les corps)
	for (auto &k : keep) if (k.face < 0 && k.edge < 0) g_sels.push_back (k);
	g_hot = Pick ();
}

// ---- la commande : début, gestes, cotes, fin --------------------------------------------------------------------

void cmdGhost ()
{
	g_cmd.ghost = Mesh ();
	g_cmd.err.clear ();
	if (g_cmd.tool == T_FILLET || g_cmd.tool == T_CHAMFER || g_cmd.tool == T_UNION || g_cmd.tool == T_CUT) return;
	if (g_cmd.stage == 0) return;
	TopoDS_Shape s = featureTool (g_doc, g_cmd.f);
	if (s.IsNull ()) return;
	meshShape (s, g_cmd.ghost, 0.2);
}

void cmdEnd ()
{
	g_cmd = Cmd ();
	g_view.ghost = nullptr;
	for (int i = 0; i < TB_N; i++) ui_w_set_value (tb[i], 0);
	refreshAll ();
	hintIdle ();
}

// la boîte, le cylindre : la valeur des puces dans la forme
void cmdApplyChips ()
{
	Feature &f = g_cmd.f;
	auto v = [&] (int i) { return i < (int) g_cmd.chips.size () ? g_cmd.chips[i].val : 0.0; };
	switch (g_cmd.tool) {
	case T_BOX: f.a = v (0), f.b = v (1), f.c = g_cmd.stage >= 2 ? v (2) : 1; break;
	case T_CYL: f.a = v (0) / 2, f.c = g_cmd.stage >= 2 ? v (1) : 1; break;
	case T_PUSH: f.a = v (0); break;
	case T_FILLET: case T_CHAMFER: f.a = v (0); break;
	default: break;
	}
	// l'opération choisie pour vous : sur un corps, monter joint et creuser enlève ; sur le sol, un nouveau corps
	if (g_cmd.opAuto && (g_cmd.tool == T_BOX || g_cmd.tool == T_CYL)) {
		if (g_cmd.target < 0) f.op = OP_NEW, f.body = -1;
		else f.op = f.c < 0 ? OP_CUT : OP_JOIN, f.body = g_cmd.target;
	}
	// les puces suivent la forme
	double n[3] = { f.frame.n[0], f.frame.n[1], f.frame.n[2] }, x[3] = { f.frame.x[0], f.frame.x[1], f.frame.x[2] };
	double y[3] = { n[1] * x[2] - n[2] * x[1], n[2] * x[0] - n[0] * x[2], n[0] * x[1] - n[1] * x[0] };
	const double *o = f.frame.o;
	auto put = [&] (int i, double ax, double ay, double az) {
		if (i >= (int) g_cmd.chips.size ()) return;
		for (int k = 0; k < 3; k++) g_cmd.chips[i].at[k] = o[k] + ax * x[k] + ay * y[k] + az * n[k];
	};
	if (g_cmd.tool == T_BOX) {
		put (0, 0, -f.b / 2, 0);
		put (1, f.a / 2, 0, 0);
		put (2, f.a / 2, -f.b / 2, f.c / 2);
	} else if (g_cmd.tool == T_CYL) {
		put (0, f.a, 0, 0);
		put (1, f.a, 0, f.c / 2);
	} else if (g_cmd.tool == T_PUSH && !g_cmd.chips.empty ()) {
		for (int k = 0; k < 3; k++) g_cmd.chips[0].at[k] = f.face.pt[k] + f.face.n[k] * f.a / 2;
	}
}

void cmdRefresh ()
{
	cmdApplyChips ();
	if (g_cmd.tool == T_FILLET || g_cmd.tool == T_CHAMFER) {	// (l'aperçu : la vraie forme, calculée)
		Doc probe = g_doc;
		probe.feats.insert (probe.feats.begin () + probe.bar, g_cmd.f);
		probe.bar++;
		replay (probe, 0.2);
		g_cmd.err = probe.feats[probe.bar - 1].error;
		g_cmd.ghost = Mesh ();
		if (g_cmd.err.empty () && g_cmd.f.body >= 0 && g_cmd.f.body < (int) probe.bodies.size ())
			meshShape (probe.bodies[g_cmd.f.body].shape, g_cmd.ghost, 0.2);
	} else
		cmdGhost ();
	renderView ();
	paintOverlays ();
	panelUpdate ();
}

void cmdStart (Tool t)
{
	g_cmd = Cmd ();
	g_cmd.tool = t;
	g_selStep = -1;
	Feature &f = g_cmd.f;
	f.name = featureName (g_doc, t == T_BOX ? ST_BOX : t == T_CYL ? ST_CYLINDER : t == T_PUSH ? ST_PUSH : t == T_FILLET ? ST_FILLET
		 : t == T_CHAMFER ? ST_CHAMFER : t == T_UNION ? ST_UNION : ST_CUT);
	f.kind = t == T_BOX ? ST_BOX : t == T_CYL ? ST_CYLINDER : t == T_PUSH ? ST_PUSH : t == T_FILLET ? ST_FILLET : t == T_CHAMFER ? ST_CHAMFER
		 : t == T_UNION ? ST_UNION : ST_CUT;
	ui_w_set_value (tb[t == T_BOX ? TB_BOX : t == T_CYL ? TB_CYL : t == T_PUSH ? TB_PUSH : t == T_FILLET ? TB_FILLET : t == T_CHAMFER ? TB_CHAMFER
		: t == T_UNION ? TB_UNION : TB_CUT], 1);
	Pick k = sel0 ();
	// ce qui est déjà choisi sert de départ : une face plane pour pousser/tirer, des arêtes pour un congé
	if (t == T_PUSH && !k.none () && k.face >= 0 && faceRef (g_doc.bodies[k.body], k.face, f.face)) {
		f.body = k.body;
		g_cmd.stage = 1;
		Chip c;
		c.label = "Distance";
		c.val = 5;
		g_cmd.chips.push_back (c);
	}
	if (t == T_FILLET || t == T_CHAMFER) {
		Chip c;
		c.label = t == T_FILLET ? "Rayon" : "Distance";
		c.val = 2;
		g_cmd.chips.push_back (c);
		for (auto &s : g_sels)
			if (s.edge >= 0) {
				if (f.body < 0) f.body = s.body;
				if (s.body == f.body) f.edges.push_back (edgeRef (g_doc.bodies[s.body], s.edge));
			}
		if (!f.edges.empty ()) {
			g_cmd.stage = 1;
			EdgeRef &e = f.edges[0];
			for (int i = 0; i < 3; i++) g_cmd.chips[0].at[i] = e.mid[i];
		}
	}
	if (t == T_UNION || t == T_CUT) {
		if (!k.none () && k.face < 0 && k.edge < 0) f.body = k.body, g_cmd.stage = 1;
	}
	g_sels.clear ();
	cmdRefresh ();
	status (TR (toolName (t)));
}

void cmdCommit ()
{
	Feature f = g_cmd.f;
	if (g_cmd.tool == T_BOX && (f.a < 0.01 || f.b < 0.01 || std::fabs (f.c) < 0.01)) return;
	if ((g_cmd.tool == T_FILLET || g_cmd.tool == T_CHAMFER) && (f.edges.empty () || !g_cmd.err.empty ())) return;
	pushUndo ();
	g_doc.feats.insert (g_doc.feats.begin () + g_doc.bar, f);
	g_doc.bar++;
	g_sels.clear ();
	rebuild ();
	const Feature &done = g_doc.feats[g_doc.bar - 1];
	std::string s = done.error.empty () ? f.name + " : " + featureDetail (done) : f.name + TR (" en erreur : ") + done.error;
	cmdEnd ();
	status (s.c_str ());
}

// le point sous le pointeur pour poser une forme : sur une face plane de la pièce, sinon sur le sol (z = 0)
bool basePoint (int x, int y, Frame &fr, int &body)
{
	Pick k = g_view.pick (g_doc, x, y);
	body = -1;
	if (!k.none () && k.face >= 0 && facePlane (g_doc.bodies[k.body], k.face, fr)) {
		double p[3];
		if (!g_view.onPlane (x, y, fr.o, fr.n, p)) return false;
		for (int i = 0; i < 3; i++) fr.o[i] = std::round (p[i]);	// (accrochage 1 mm)
		double d = (fr.o[0] - p[0]) * fr.n[0] + (fr.o[1] - p[1]) * fr.n[1] + (fr.o[2] - p[2]) * fr.n[2];
		for (int i = 0; i < 3; i++) fr.o[i] -= d * fr.n[i];	// (resté sur le plan)
		body = k.body;
		return true;
	}
	fr = Frame ();
	double p[3];
	if (!g_view.onPlane (x, y, fr.o, fr.n, p)) return false;
	fr.o[0] = std::round (p[0]), fr.o[1] = std::round (p[1]), fr.o[2] = 0;
	return true;
}

// la distance le long de la normale n depuis le point o, d'après le déplacement du pointeur à l'écran
double alongNormal (const double o[3], const double n[3], int x, int y)
{
	double sx, sy, ex, ey, q[3] = { o[0] + n[0] * 10, o[1] + n[1] * 10, o[2] + n[2] * 10 };
	g_view.toScreen (o, sx, sy);
	g_view.toScreen (q, ex, ey);
	double vx = ex - sx, vy = ey - sy, l2 = vx * vx + vy * vy;
	if (l2 < 4) return 0;	// (la normale vers soi : pas de geste possible, tapez la valeur)
	return std::round (((x - sx) * vx + (y - sy) * vy) / l2 * 10);
}

// la souris pendant une commande -> vrai si elle est prise
bool cmdMouse (int kind, int x, int y, int buttons, bool shift)
{
	if (g_cmd.tool == T_NONE || (buttons & 6)) return false;	// (droit, milieu : la vue)
	Feature &f = g_cmd.f;
	Tool t = g_cmd.tool;
	if (kind == 0) {		// déplacement : le geste en cours
		if ((t == T_BOX || t == T_CYL) && g_cmd.stage == 1) {
			double p[3];
			if (!g_view.onPlane (x, y, f.frame.o, f.frame.n, p)) return true;
			double d[3] = { p[0] - f.frame.o[0], p[1] - f.frame.o[1], p[2] - f.frame.o[2] };
			const double *n = f.frame.n, *xa = f.frame.x;
			double ya[3] = { n[1] * xa[2] - n[2] * xa[1], n[2] * xa[0] - n[0] * xa[2], n[0] * xa[1] - n[1] * xa[0] };
			double lx = d[0] * xa[0] + d[1] * xa[1] + d[2] * xa[2], ly = d[0] * ya[0] + d[1] * ya[1] + d[2] * ya[2];
			if (t == T_BOX) {
				if (!g_cmd.chips[0].locked) g_cmd.chips[0].val = std::max (1.0, std::round (2 * std::fabs (lx)));
				if (!g_cmd.chips[1].locked) g_cmd.chips[1].val = std::max (1.0, std::round (2 * std::fabs (ly)));
			} else if (!g_cmd.chips[0].locked)
				g_cmd.chips[0].val = std::max (1.0, std::round (2 * sqrt (lx * lx + ly * ly)));
			cmdRefresh ();
			return true;
		}
		if ((t == T_BOX || t == T_CYL) && g_cmd.stage == 2) {
			Chip &h = g_cmd.chips.back ();
			if (!h.locked) {
				double v = alongNormal (f.frame.o, f.frame.n, x, y);
				h.val = v == 0 ? (h.val < 0 ? -1 : 1) : v;
			}
			cmdRefresh ();
			return true;
		}
		if (t == T_PUSH && g_cmd.stage == 1 && !g_cmd.chips[0].locked) {
			double v = alongNormal (f.face.pt, f.face.n, x, y);
			if (v != g_cmd.chips[0].val && v != 0) {
				g_cmd.chips[0].val = v;
				cmdRefresh ();
			}
			return true;
		}
		return false;		// (le survol habituel)
	}
	if (kind != 2) return kind == 1;
	// relâché : l'étape suivante du geste
	if (t == T_BOX || t == T_CYL) {
		if (g_cmd.stage == 0) {
			if (!basePoint (x, y, f.frame, g_cmd.target)) return true;
			g_cmd.stage = 1;
			Chip a, b;
			a.label = t == T_BOX ? "Largeur" : "Diamètre";
			b.label = t == T_BOX ? "Profondeur" : "Hauteur";
			a.val = 1, b.val = t == T_BOX ? 1 : 10;
			g_cmd.chips = { a };
			if (t == T_BOX) g_cmd.chips.push_back (b);
			g_cmd.active = 0;
		} else if (g_cmd.stage == 1) {
			g_cmd.stage = 2;
			Chip h;
			h.label = "Hauteur";
			h.val = t == T_BOX ? 10 : 10;
			if (t == T_CYL) g_cmd.chips.resize (1);
			g_cmd.chips.push_back (h);
			g_cmd.active = (int) g_cmd.chips.size () - 1;
		} else {
			cmdApplyChips ();
			cmdCommit ();
			return true;
		}
		cmdRefresh ();
		return true;
	}
	if (t == T_PUSH) {
		if (g_cmd.stage == 0) {
			Pick k = g_view.pick (g_doc, x, y);
			if (k.face < 0 || !faceRef (g_doc.bodies[k.body], k.face, f.face)) {
				status (TR ("Choisissez une face plane."));
				return true;
			}
			f.body = k.body;
			g_cmd.stage = 1;
			Chip c;
			c.label = "Distance";
			c.val = 5;
			g_cmd.chips = { c };
			cmdRefresh ();
		} else {
			cmdApplyChips ();
			cmdCommit ();
		}
		return true;
	}
	if (t == T_FILLET || t == T_CHAMFER) {
		Pick k = g_view.pick (g_doc, x, y);
		if (k.edge < 0) return true;
		if (f.body >= 0 && k.body != f.body) {
			status (TR ("Les arêtes d'un même corps."));
			return true;
		}
		f.body = k.body;
		EdgeRef r = edgeRef (g_doc.bodies[k.body], k.edge);
		if (!shift) f.edges.clear ();
		bool had = false;
		for (size_t i = 0; i < f.edges.size (); i++)
			if (fabs (f.edges[i].mid[0] - r.mid[0]) + fabs (f.edges[i].mid[1] - r.mid[1]) + fabs (f.edges[i].mid[2] - r.mid[2]) < 1e-6)
				f.edges.erase (f.edges.begin () + i), had = true;
		if (!had) f.edges.push_back (r);
		for (int i = 0; i < 3; i++) g_cmd.chips[0].at[i] = f.edges.empty () ? r.mid[i] : f.edges[0].mid[i];
		g_cmd.stage = f.edges.empty () ? 0 : 1;
		cmdRefresh ();
		return true;
	}
	if (t == T_UNION || t == T_CUT) {
		Pick k = g_view.pick (g_doc, x, y);
		if (k.none ()) return true;
		if (g_cmd.stage == 0) f.body = k.body, g_cmd.stage = 1, g_sels = { Pick { k.body, -1, -1 } }, cmdRefresh ();
		else if (k.body != f.body) f.tool = k.body, cmdCommit ();
		return true;
	}
	return true;
}

// le clavier pendant une commande -> vrai s'il est pris
bool cmdKey (int code, int ch)
{
	if (g_cmd.tool == T_NONE) return false;
	if (code == 0x29) {	// Échap
		cmdEnd ();
		status (TR ("Commande annulée."));
		return true;
	}
	Chip *c = g_cmd.active < (int) g_cmd.chips.size () ? &g_cmd.chips[g_cmd.active] : nullptr;
	auto takeText = [&] () -> bool {	// la valeur tapée dans la puce active -> faux si illisible
		if (!c || c->text.empty ()) return true;
		double v;
		if (!evalExpr (c->text.c_str (), v)) {
			g_cmd.err = TR ("valeur illisible : ") + c->text;
			paintOverlays ();
			panelUpdate ();
			return false;
		}
		c->val = v, c->locked = true, c->text.clear ();
		return true;
	};
	if (code == 0x28 || code == 0x58) {	// Entrée : la valeur, puis l'étape suivante (ou la fin)
		if (!takeText ()) return true;
		Tool t = g_cmd.tool;
		if ((t == T_BOX || t == T_CYL) && g_cmd.stage == 1) {
			g_cmd.stage = 2;
			Chip h;
			h.label = "Hauteur";
			h.val = 10;
			if (t == T_CYL) g_cmd.chips.resize (1);
			g_cmd.chips.push_back (h);
			g_cmd.active = (int) g_cmd.chips.size () - 1;
			cmdRefresh ();
		} else if (g_cmd.stage >= 1) {
			if (t == T_FILLET || t == T_CHAMFER) {	// (l'aperçu de la valeur tapée d'abord : refusée s'il ne passe pas)
				cmdRefresh ();
				if (!g_cmd.err.empty ()) {
					status ((std::string (TR (toolName (t))) + " : " + g_cmd.err).c_str ());
					return true;
				}
			}
			cmdApplyChips ();
			cmdCommit ();
		}
		return true;
	}
	if (code == 0x2B) {	// Tab : la puce suivante
		if (!takeText () || g_cmd.chips.empty ()) return true;
		g_cmd.active = (g_cmd.active + 1) % (int) g_cmd.chips.size ();
		cmdRefresh ();
		return true;
	}
	if (!c) return false;
	if (code == 0x2A) {	// Retour arrière
		if (!c->text.empty ()) c->text.pop_back ();
		g_cmd.err.clear ();
		paintOverlays ();
		return true;
	}
	if (ch > 0 && ch < 128 && (isdigit (ch) || strchr (".,+-*/() cmin", ch))) {
		c->text += (char) ch;
		g_cmd.err.clear ();
		paintOverlays ();
		panelUpdate ();
		return true;
	}
	return false;
}

// ---- la souris sur la vue ---------------------------------------------------------------------------------------

int g_mx, g_my, g_downX, g_downY, g_btn;
bool g_dragged, g_overView;

// genre : 0 déplacement, 1 appui, 2 relâché, 3 sortie ; x, y dans la vue ; boutons : 1 gauche, 2 droit, 4 milieu
bool g_trace;
void viewMouse (int kind, int x, int y, int buttons, bool shift)
{
	if (g_trace) fprintf (stderr, "forge : souris %d (%d, %d) boutons %d, commande %d étape %d\n", kind, x, y, buttons, (int) g_cmd.tool, g_cmd.stage);
	if (kind == 1) {
		g_downX = x, g_downY = y, g_btn = buttons, g_dragged = false;
		g_mx = x, g_my = y;
	}
	if (kind == 0) {
		int dx = x - g_mx, dy = y - g_my;
		g_mx = x, g_my = y;
		if (buttons & 2) {	// droit : tourner
			if (std::abs (x - g_downX) + std::abs (y - g_downY) > 3) g_dragged = true;
			g_view.orbit (-dx * 0.4, dy * 0.4);
			cameraChanged ();
			return;
		}
		if (buttons & 4) {	// milieu : déplacer
			g_view.pan (dx, dy);
			cameraChanged ();
			return;
		}
	}
	// les boutons, le cube, les corps restent à eux pendant une commande
	bool overUi = (x >= BP_X && x < BP_X + BP_W && y >= BP_Y && y < BP_Y + bodiesH ()) || g_view.cubeFaceAt (x, y) >= 0;
	for (int i = 0; i < VB_N && !overUi; i++) {
		int bx, by;
		vbtnRect (i, bx, by);
		overUi = x >= bx && x < bx + VB_W && y >= by && y < by + VB_H;
	}
	if (!overUi && kind != 3 && cmdMouse (kind, x, y, kind == 2 ? g_btn : buttons, shift)) return;
	if (kind == 0) {
		if (buttons) return;
		int hc = g_view.cubeFaceAt (x, y);
		Pick k = hc >= 0 || overUi ? Pick () : g_view.pick (g_doc, x, y);
		if (hc != g_view.hotCube || k != g_hot) {
			g_view.hotCube = hc;
			g_hot = k;
			renderView ();
			if (hc != g_view.hotCube) paintOverlays ();
			if (!k.none ()) {
				std::string s = pickName (k) + TR ("   ·   clic : la choisir");
				status (s.c_str ());
			} else if (g_cmd.tool == T_NONE)
				hintIdle ();
		}
		return;
	}
	if (kind == 2) {
		if (!(g_btn & 1) || g_dragged) return;
		if (x >= BP_X && x < BP_X + BP_W && y >= BP_Y && y < BP_Y + bodiesH ()) {
			int row = (y - BP_Y - 36) / BP_ROW, r = 0;
			if (y < BP_Y + 36) return;
			for (size_t i = 0; i < g_doc.bodies.size (); i++) {
				if (g_doc.bodies[i].shape.IsNull ()) continue;
				if (r++ != row) continue;
				if (x < BP_X + 32) {
					g_hidden.resize (g_doc.bodies.size (), 0);
					g_hidden[i] = !g_hidden[i];
					g_doc.bodies[i].visible = !g_hidden[i];
					select (g_sels);
				} else
					select (Pick { (int) i, -1, -1 });
			}
			return;
		}
		for (int i = 0; i < VB_N; i++) {
			int bx, by;
			vbtnRect (i, bx, by);
			if (x >= bx && x < bx + VB_W && y >= by && y < by + VB_H) {
				if (i == 0) g_view.standard (6), g_view.fit (g_doc);
				else if (i == 1) g_view.fit (g_doc);
				else if (i == 2) g_view.edges = !g_view.edges, ui_menu_state (25, g_view.edges ? UMS_CHECKED : 0);
				else g_view.grid = !g_view.grid, ui_menu_state (26, g_view.grid ? UMS_CHECKED : 0);
				cameraChanged ();
				return;
			}
		}
		int f = g_view.cubeFaceAt (x, y);
		if (f >= 0) {
			g_view.standard (f);
			cameraChanged ();
			return;
		}
		Pick k = g_view.pick (g_doc, x, y);
		if (shift && !k.none () && !g_sels.empty () && (k.edge >= 0) == (g_sels[0].edge >= 0) && k.body == g_sels[0].body) {
			std::vector<Pick> s = g_sels;	// Maj+clic : ajouter, ou retirer
			bool had = false;
			for (size_t i = 0; i < s.size (); i++)
				if (s[i] == k) s.erase (s.begin () + i), had = true;
			if (!had) s.push_back (k);
			select (s);
		} else
			select (k);
		return;
	}
	if (kind == 3 && (!g_hot.none () || g_view.hotCube >= 0)) {
		g_hot = Pick ();
		g_view.hotCube = -1;
		renderView ();
		paintOverlays ();
		if (g_cmd.tool == T_NONE) hintIdle ();
	}
}

void viewWheel (long steps)
{
	g_view.zoomAt (g_mx, g_my, steps > 0 ? 1.18 : 1 / 1.18);
	cameraChanged ();
}

// ---- la frise ---------------------------------------------------------------------------------------------------

const int CHIP_W = 100, CHIP_GAP = 6;
int CHIP_X0 = 16;			// (moins quand la frise défile : la barre reste en vue)
bool g_barDrag;
int barX (int bar) { return CHIP_X0 + bar * (CHIP_W + CHIP_GAP) - CHIP_GAP / 2 - 1; }
void timelineScroll (int w)
{
	CHIP_X0 = 16;
	int room = w - 110;			// (à droite : le nombre d'étapes)
	int need = 16 + (int) g_doc.feats.size () * (CHIP_W + CHIP_GAP) + 8;
	if (need > room) CHIP_X0 = 16 - imin (need - room, imax (0, barX (g_doc.bar) + CHIP_W - room + 16));
}

void timelinePaint (long, long w, long h)
{
	timelineScroll ((int) w);
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_fill (0, 0, w, 1, jet (TJ_LINE));
	int y = 10, ch = (int) h - 20;
	for (size_t i = 0; i < g_doc.feats.size (); i++) {
		const Feature &f = g_doc.feats[i];
		int x = CHIP_X0 + (int) i * (CHIP_W + CHIP_GAP);
		bool on = (int) i == g_selStep, after = (int) i >= g_doc.bar, bad = !f.error.empty ();
		unsigned bg = on ? jet (TJ_ACCENT_LIGHT) : jet (TJ_SURFACE);
		unsigned ln = bad ? jet (TJ_DANGER) : on ? jet (TJ_ACCENT) : jet (TJ_LINE);
		ui_rbox (x, y, CHIP_W, ch, 7, ln, ln);
		ui_rbox (x + 1 + bad, y + 1 + bad, CHIP_W - 2 - 2 * bad, ch - 2 - 2 * bad, 6, bg, bg);
		int tc = after || f.off ? TJ_TEXT3 : on ? TJ_ACCENT_DARK : TJ_TEXT1;
		ui_text ((long) f.name.c_str (), x + 10, y + ch / 2 + 5, jet (tc), 12, (long) ui.font, CHIP_W - 20);
		if (bad) ui_fill (x + CHIP_W - 12, y + 6, 6, 6, jet (TJ_DANGER));
	}
	int bx = barX (g_doc.bar);
	ui_fill (bx, 6, 3, (int) h - 12, jet (TJ_ACCENT));
	ui_fill (bx - 4, 3, 11, 5, jet (TJ_ACCENT));
	char s[64];
	snprintf (s, sizeof s, "%d étapes", (int) g_doc.feats.size ());
	long tw = ui_text_width ((long) s, 12, (long) ui.font);
	ui_fill (w - 104, 1, 104, h - 1, jet (TJ_FACE));
	ui_text ((long) s, w - 16 - tw, h / 2 + 5, jet (TJ_TEXT3), 12, (long) ui.font, 0);
}

long timelineMouse (long, long kind, long x, long, long)
{
	if (g_cmd.tool != T_NONE) return 0;
	if (kind == 1 && std::abs ((int) x - barX (g_doc.bar) - 1) <= 7) {	// la barre : glissée
		g_barDrag = true;
		return 0;
	}
	if (g_barDrag) {
		int bar = imax (0, imin ((int) g_doc.feats.size (), ((int) x - CHIP_X0 + (CHIP_W + CHIP_GAP) / 2) / (CHIP_W + CHIP_GAP)));
		if (kind == 0 && bar != g_doc.bar) {
			g_doc.bar = bar;
			ui_view_dirty (w_tl);
			ui_view_flush ();
		}
		if (kind == 2 || kind == 3) {
			g_barDrag = false;
			pushUndo ();
			g_doc.bar = bar;
			rebuild ();
			g_selStep = -1;
			refreshAll ();
			char m[96];
			snprintf (m, sizeof m, "%s %d %s", TR ("Barre de reconstruction après l'étape"), bar, TR ("; Fin pour la remettre au bout."));
			status (m);
		}
		return 0;
	}
	if (kind != 1) return 0;
	int i = ((int) x - CHIP_X0) / (CHIP_W + CHIP_GAP);
	if (x < CHIP_X0 || i >= (int) g_doc.feats.size ()) i = -1;
	g_selStep = i;
	g_sels.clear ();
	refreshAll ();
	return 0;
}

// les valeurs de l'étape choisie, appliquées
void stepApply ()
{
	if (g_selStep < 0 || g_selStep >= (int) g_doc.feats.size ()) return;
	Feature f = g_doc.feats[g_selStep];
	const char *names[3];
	double vals[3];
	int n = featFields (f, names, vals);
	for (int i = 0; i < n; i++) {
		double v;
		const char *t = (const char *) ui_w_get_text (e_val[i]);
		if (!evalExpr (t, v)) {
			std::string m = std::string (TR ("Valeur illisible : ")) + t;
			status (m.c_str ());
			return;
		}
		featSet (f, i, v);
	}
	pushUndo ();
	g_doc.feats[g_selStep] = f;
	rebuild ();
	refreshAll ();
	const Feature &r = g_doc.feats[g_selStep];
	std::string m = r.error.empty () ? r.name + " : " + featureDetail (r) : r.name + TR (" en erreur : ") + r.error;
	status (m.c_str ());
}

void stepDelete ()
{
	if (g_selStep < 0 || g_selStep >= (int) g_doc.feats.size ()) return;
	pushUndo ();
	g_doc.feats.erase (g_doc.feats.begin () + g_selStep);
	if (g_doc.bar > g_selStep) g_doc.bar--;
	g_selStep = -1;
	rebuild ();
	refreshAll ();
}

void undo (bool redo)
{
	auto &from = redo ? g_redo : g_undo, &to = redo ? g_undo : g_redo;
	if (from.empty ()) return;
	to.push_back ({ g_doc.feats, g_doc.bar });
	g_doc.feats = from.back ().feats;
	g_doc.bar = from.back ().bar;
	from.pop_back ();
	g_selStep = -1;
	g_sels.clear ();
	rebuild ();
	refreshAll ();
	status (TR (redo ? "Rétabli." : "Annulé."));
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
	ui_w_move (p_sub, px, y + 26, pw, 36);
	ui_w_move (p_sec, px, y + 66, pw, 20);
	for (int i = 0; i < 8; i++) {
		ui_w_move (p_name[i], px, y + 92 + i * 32, pw / 2, 22);
		ui_w_move (p_val[i], px + pw / 2, y + 92 + i * 32, pw - pw / 2, 22);
		if (i < 3) ui_w_move (e_val[i], px + pw / 2, y + 88 + i * 32, pw - pw / 2, h);
	}
	int by = y + 92 + 5 * 32;
	ui_w_move (w_op, px, by, pw, h);
	ui_w_move (b_ok, px + pw - 2 * 104 - 8, by + h + 12, 104, h);
	ui_w_move (b_cancel, px + pw - 104, by + h + 12, 104, h);
	ui_w_move (b_apply, px, by, (pw - 8) / 2, h);
	ui_w_move (b_off, px + (pw - 8) / 2 + 8, by, pw - (pw - 8) / 2 - 8, h);
	ui_w_move (b_barhere, px, by + h + 8, (pw - 8) / 2, h);
	ui_w_move (b_del, px + (pw - 8) / 2 + 8, by + h + 8, pw - (pw - 8) / 2 - 8, h);
	ui_w_move (p_state, px, by + 2 * h + 24, pw, 60);
	ui_w_move (p_hint, px, H - ST_H - 110, pw, 96);
	ui_w_move (w_status, 12, H - ST_H + 3, W * 3 / 5, ST_H - 6);
	ui_w_move (w_stats, W * 3 / 5, H - ST_H + 3, W - W * 3 / 5 - 12, ST_H - 6);
	if (g_surf) ui_surface_move (g_surf, VX, VY, VW, VH);
	else g_surf = ui_surface (VX, VY, VW, VH, WS_SF_XRGB8888);
	g_view.resize (VW, VH);
}

void repaint ()
{
	int W = (int) ui.w, H = (int) ui.h;
	ui_fill (0, 0, W, H, jet (TJ_FACE));
	ui_fill (0, TB_H - 1, W, 1, jet (TJ_LINE));
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
	"MÉdition\n" "I10\tAnnuler\tCtrl+Z\n" "I11\tRétablir\tCtrl+Y\n" "-\n" "I12\tSupprimer l'étape\n"
	"MAffichage\n" "I20\tVue de face\tAlt+1\n" "I21\tVue de dessus\tAlt+2\n" "I22\tVue de droite\tAlt+3\n"
	"I23\tVue isométrique\tAlt+0\n" "-\n" "I24\tAjuster\tF6\n" "-\n" "I25c\tArêtes\n" "I26c\tGrille\n"
	"MCréer\n" "I30\tBoîte (B)\n" "I31\tCylindre\n" "-\n" "I32\tPousser/tirer (Q)\n"
	"MModifier\n" "I33\tCongé (F)\n" "I34\tChanfrein (Maj+F)\n" "-\n" "I35\tUnion\n" "I36\tSoustraction\n"
	"MAide\n" "I40\tNaviguer dans la vue\n";

void command (int id)
{
	switch (id) {
	case 9: g_quit = true; break;
	case 10: undo (false); break;
	case 11: undo (true); break;
	case 12: stepDelete (); break;
	case 20: g_view.standard (3); cameraChanged (); break;
	case 21: g_view.standard (4); cameraChanged (); break;
	case 22: g_view.standard (0); cameraChanged (); break;
	case 23: g_view.standard (6); cameraChanged (); break;
	case 24: g_view.fit (g_doc); cameraChanged (); break;
	case 25: g_view.edges = !g_view.edges; ui_menu_state (25, g_view.edges ? UMS_CHECKED : 0); cameraChanged (); break;
	case 26: g_view.grid = !g_view.grid; ui_menu_state (26, g_view.grid ? UMS_CHECKED : 0); cameraChanged (); break;
	case 30: cmdStart (T_BOX); break;
	case 31: cmdStart (T_CYL); break;
	case 32: cmdStart (T_PUSH); break;
	case 33: cmdStart (T_FILLET); break;
	case 34: cmdStart (T_CHAMFER); break;
	case 35: cmdStart (T_UNION); break;
	case 36: cmdStart (T_CUT); break;
	case 40: hintIdle (); break;
	}
}

void control (long w, long code)
{
	if (code == UN_ACTIVATE || code == UN_CHANGE) {
		static const int MAP[TB_N] = { -1, 30, 31, 32, 33, 34, 35, 36, -1 };
		for (int i = 0; i < TB_N; i++)
			if (w == tb[i] && MAP[i] >= 0) {
				if (g_cmd.tool != T_NONE) cmdEnd ();
				else command (MAP[i]);
				if (ui_w_get_value (tb[i]) && g_cmd.tool == T_NONE) ui_w_set_value (tb[i], 0);
				return;
			}
	}
	if (w == w_op && code == UN_CHANGE) {
		long v = ui_w_get_value (w_op);
		g_cmd.f.op = v == 0 ? OP_NEW : v == 1 ? OP_JOIN : v == 2 ? OP_CUT : OP_INTERSECT;
		g_cmd.opAuto = false;
		if (g_cmd.f.op != OP_NEW && g_cmd.f.body < 0) {	// (sur le sol : le corps visé est le premier)
			for (size_t i = 0; i < g_doc.bodies.size (); i++)
				if (!g_doc.bodies[i].shape.IsNull ()) { g_cmd.f.body = (int) i; break; }
		}
		cmdRefresh ();
	} else if (w == b_ok && code == UN_ACTIVATE) {
		cmdKey (0x28, 0);
	} else if (w == b_cancel && code == UN_ACTIVATE) {
		cmdKey (0x29, 0);
	} else if (w == b_apply && code == UN_ACTIVATE) {
		stepApply ();
	} else if (w == b_del && code == UN_ACTIVATE) {
		stepDelete ();
	} else if (w == b_off && code == UN_ACTIVATE && g_selStep >= 0) {
		pushUndo ();
		g_doc.feats[g_selStep].off = !g_doc.feats[g_selStep].off;
		rebuild ();
		refreshAll ();
	} else if (w == b_barhere && code == UN_ACTIVATE && g_selStep >= 0) {
		pushUndo ();
		g_doc.bar = g_selStep + 1;
		rebuild ();
		refreshAll ();
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
	static const char *const TOOLS[TB_N] = { "Esquisse", "Boîte", "Cylindre", "Pousser", "|Congé", "Chanfrein", "|Union", "Soustraire", "|Mesurer" };
	static const char *const TIPS[TB_N] = { "L'esquisse avec contraintes (PlaneGCS) : à l'étape 3", "Boîte (B) : clic au centre, on s'éloigne, clic, on monte, clic",
		"Cylindre : clic au centre, on s'éloigne pour le rayon, clic, on monte, clic", "Pousser/tirer (Q) une face plane", "Congé (F) sur les arêtes choisies",
		"Chanfrein (Maj+F) sur les arêtes choisies", "Union : deux corps n'en font plus qu'un", "Soustraction : un corps retiré d'un autre", "Mesurer : à une étape suivante" };
	for (int i = 0; i < TB_N; i++) {
		const char *t = TOOLS[i];
		bool sep = t[0] == '|';
		tb[i] = ui_toolbar_add (w_bar, 0, (long) TR (sep ? t + 1 : t), (sep ? UTB_SEP : 0) | UTB_TOGGLE);
		ui_w_tip (tb[i], (long) TR (TIPS[i]));
		if (i == TB_SKETCH || i == TB_MEASURE) ui_w_enable (tb[i], 0);
	}
	w_export = ui_pushbutton (0, 0, 108, h, (long) TR ("Exporter"), UB_DEFAULT);
	ui_w_enable (w_export, 0);
	ui_w_tip (w_export, (long) TR ("STEP, IGES, STL, 3MF : à l'étape 5"));
	w_tl = ui_view (0, 0, 100, 56, 0);
	ui_view_on (w_tl, (long) timelinePaint, (long) timelineMouse, 0, 0);
	p_title = ui_label (0, 0, 10, 26, (long) "", ULB_TITLE);
	p_sub = ui_label (0, 0, 10, 36, (long) "", ULB_DIM | ULB_WRAP);
	p_sec = ui_label (0, 0, 10, 20, (long) "", ULB_BOLD);
	for (int i = 0; i < 8; i++) {
		p_name[i] = ui_label (0, 0, 10, 22, (long) "", ULB_DIM);
		p_val[i] = ui_label (0, 0, 10, 22, (long) "", ULB_RIGHT);
	}
	for (int i = 0; i < 3; i++) e_val[i] = ui_textbox (0, 0, 10, h, 0);
	w_op = ui_segmented (0, 0, 10, h, (long) TR ("Nouveau|Joindre|Enlever|Inter."));
	b_ok = ui_pushbutton (0, 0, 104, h, (long) TR ("OK"), UB_DEFAULT);
	b_cancel = ui_pushbutton (0, 0, 104, h, (long) TR ("Annuler"), 0);
	b_apply = ui_pushbutton (0, 0, 10, h, (long) TR ("Appliquer"), UB_DEFAULT);
	b_off = ui_pushbutton (0, 0, 10, h, (long) TR ("Désactiver"), 0);
	b_barhere = ui_pushbutton (0, 0, 10, h, (long) TR ("Barre ici"), 0);
	b_del = ui_pushbutton (0, 0, 10, h, (long) TR ("Supprimer"), 0);
	p_state = ui_label (0, 0, 10, 60, (long) "", ULB_WRAP);
	p_hint = ui_label (0, 0, 10, 96, (long) TR ("Clic : choisir ; Maj+clic : plusieurs arêtes. Une étape de la frise : ses valeurs ; la barre bleue se glisse. Ctrl+Z : annuler."), ULB_WRAP | ULB_DIM);
	w_status = ui_label (0, 0, 10, 20, (long) "", ULB_DIM);
	w_stats = ui_label (0, 0, 10, 20, (long) "", ULB_DIM | ULB_RIGHT);
	ui_menu ((long) MENUS);
	ui_menu_state (25, UMS_CHECKED);
	ui_menu_state (26, UMS_CHECKED);
	showCmdWidgets (false);
	showEditWidgets (0, false);
}

// ---- les événements ---------------------------------------------------------------------------------------------

void loop (double untilMs)
{
	while (!g_quit && (!untilMs || nowMs () < untilMs)) {
		long t = ui_event (50);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) {
			command ((int) ui_ev_field (&ui, EV_CODE));
			continue;
		}
		if (t == WEV_THEME) {
			palette ();
			repaint ();
			ui_view_dirty (w_tl);
			ui_view_flush ();
			continue;
		}
		if (t <= 0) {
			ui_w_idle ();
			continue;
		}
		bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
		int mods = ui_ev_field (&ui, EV_MODS);
		bool shift = (mods & UI_MOD_SHIFT) != 0;
		long code = 0, taken = 0;
		long w = uikit_w_event (&code, &taken);
		if (code) control (w, code);
		// la vue n'a pas de widget : sa souris est lue brute (aussi hors d'elle pendant un glisser)
		int ex = ui_ev_field (&ui, EV_X) - VX, ey = ui_ev_field (&ui, EV_Y) - VY;
		bool inView = ex >= 0 && ey >= 0 && ex < VW && ey < VH;
		if (t == WEV_MOUSE) g_overView = inView;
		if (t == WEV_MOUSE && !w && (inView || g_btn)) {
			int kind = ui_ev_field (&ui, EV_KIND), b = ui_ev_field (&ui, EV_CODE) & 0xFF;
			viewMouse (kind, ex, ey, b, shift);
			if (kind == 2) g_btn = 0;
		} else if (t == WEV_MOUSE && !inView && (!g_hot.none () || g_view.hotCube >= 0))
			viewMouse (3, ex, ey, 0, false);
		if (t == WEV_WHEEL && !w && g_overView) viewWheel (ui_ev_field (&ui, EV_Y));	// (au dernier point du pointeur)
		if (!press || taken) continue;
		int key = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR);
		if (cmdKey (key, ch)) continue;
		long id = ui_menu_key (0);
		if (id >= 0) {
			command ((int) id);
			continue;
		}
		if (mods & (UI_MOD_CTRL | UI_MOD_ALT)) continue;
		if (key == 0x29) {	// Échap : rien de choisi
			select (Pick ());
			hintIdle ();
		} else if (key == 0x4C && g_selStep >= 0) stepDelete ();	// Suppr
		else if (key == 0x4D && g_doc.bar < (int) g_doc.feats.size ()) {	// Fin : la barre au bout
			pushUndo ();
			g_doc.bar = (int) g_doc.feats.size ();
			rebuild ();
			refreshAll ();
		} else if (ch == 'b' || ch == 'B') cmdStart (T_BOX);
		else if (ch == 'q' || ch == 'Q') cmdStart (T_PUSH);
		else if (ch == 'f') cmdStart (T_FILLET);
		else if (ch == 'F') cmdStart (T_CHAMFER);
	}
}

// ---- l'essai (--selftest) ---------------------------------------------------------------------------------------

int fails;
void check (const char *what, bool ok, const std::string &detail)
{
	printf ("%s %s%s%s\n", ok ? "ok   " : "ECHEC", what, detail.empty () ? "" : " : ", detail.c_str ());
	fflush (stdout);
	if (!ok) fails++;
}

double totalVolume ()
{
	double v = 0;
	for (auto &b : g_doc.bodies) v += bodyVolume (b);
	return v / 1000;
}

std::string cm3 (double v)
{
	char s[32];
	snprintf (s, sizeof s, "%.2f cm³", v);
	return s;
}

// un clic (appui, relâché) au point de l'espace p, dans la vue
void clickAt (const double p[3])
{
	double sx, sy;
	g_view.toScreen (p, sx, sy);
	viewMouse (1, (int) sx, (int) sy, 1, false);
	viewMouse (2, (int) sx, (int) sy, 0, false);
	g_btn = 0;
}
void moveTo (const double p[3])
{
	double sx, sy;
	g_view.toScreen (p, sx, sy);
	viewMouse (0, (int) sx, (int) sy, 0, false);
}
void type (const char *s)
{
	for (; *s; s++) cmdKey (0, *s);
}

int selftest (double buildMs)
{
	char s[240];
	const Body &b = g_doc.bodies[0];
	Info in;
	bodyInfo (b, in);
	snprintf (s, sizeof s, "%d faces, %d arêtes, %d triangles, volume %s, rejouée en %.0f ms", b.mesh.nfaces, b.mesh.nedges, (int) b.mesh.triFace.size (), cm3 (totalVolume ()).c_str (), buildMs);
	check ("historique de l'exemple rejoué (8 étapes)", in.solidOk && b.mesh.nfaces == 24 && std::fabs (totalVolume () - 63.99) < 0.01, s);
	snprintf (s, sizeof s, "%d × %d px en %.0f ms", VW, VH, g_view.lastMs);
	check ("rendu de la vue", g_view.lastMs > 0, s);
	// une boîte au geste, posée sur la plaque : clic au centre, on s'éloigne (20 × 10), clic, on monte (5), clic
	g_view.standard (6);
	g_view.fit (g_doc);
	cameraChanged ();
	double v0 = totalVolume ();
	cmdStart (T_BOX);
	double c[3] = { 45, 12, 8 }, e[3] = { 55, 17, 8 };
	clickAt (c);
	moveTo (e);
	clickAt (e);
	double up[3] = { 45, 12, 14 };	// (6 mm au-dessus du centre de la base)
	moveTo (up);
	double hg = g_cmd.chips.empty () ? 0 : g_cmd.chips.back ().val;
	snprintf (s, sizeof s, "hauteur %s mm", fmtNum (hg).c_str ());
	check ("geste de hauteur : on monte de 6 mm", g_cmd.stage == 2 && std::fabs (hg - 6) <= 1, s);
	// la hauteur, tapée dans la puce : elle l'emporte sur le geste
	type ("5");
	cmdKey (0x28, 0);
	const Feature &bf = g_doc.feats.back ();
	snprintf (s, sizeof s, "%s : %s ; volume %s -> %s", bf.name.c_str (), featureDetail (bf).c_str (), cm3 (v0).c_str (), cm3 (totalVolume ()).c_str ());
	check ("boîte au geste, jointe à la plaque (choisie pour vous)", g_doc.feats.size () == 9 && bf.op == OP_JOIN && std::fabs (totalVolume () - v0 - 1.0) < 0.02, s);
	// pousser/tirer : le dessus de la paroi, tapé « 2*2,5 »
	int top = -1;
	for (int i = 0; i < g_doc.bodies[0].mesh.nfaces; i++) {
		FaceRef r;
		if (faceRef (g_doc.bodies[0], i, r) && r.n[2] > 0.99 && r.pt[2] > 44) top = i;
	}
	select (Pick { 0, top, -1 });
	v0 = totalVolume ();
	cmdStart (T_PUSH);
	type ("2*2,5");
	cmdKey (0x28, 0);
	snprintf (s, sizeof s, "%s ; volume %s -> %s", featureDetail (g_doc.feats.back ()).c_str (), cm3 (v0).c_str (), cm3 (totalVolume ()).c_str ());
	check ("pousser/tirer une face (valeur tapée : 2*2,5)", g_doc.feats.back ().kind == ST_PUSH && std::fabs (totalVolume () - v0 - 2.64) < 0.02, s);
	// une étape amont modifiée : la plaque à 10 mm, tout se rejoue
	g_selStep = 0;
	panelUpdate ();
	ui_w_set_text (e_val[2], (long) "10");
	stepApply ();
	int err = 0;
	for (auto &f : g_doc.feats) err += !f.error.empty ();
	snprintf (s, sizeof s, "%d étape(s) en erreur, volume %s", err, cm3 (totalVolume ()).c_str ());
	check ("étape amont modifiée (plaque 8 -> 10), rejeu", err == 0 && totalVolume () > 70, s);
	// un congé trop grand : refusé, la puce le dit
	select (Pick ());
	int edge = -1;
	for (int i = 0; i < g_doc.bodies[0].mesh.nedges && edge < 0; i++) {
		EdgeRef r = edgeRef (g_doc.bodies[0], i);
		if (std::fabs (r.tan[0]) > 0.99 && std::fabs (r.mid[2]) < 0.01 && std::fabs (r.mid[1]) < 0.01) edge = i;	// (l'arête avant du bas)
	}
	select (Pick { 0, -1, edge });
	cmdStart (T_FILLET);
	type ("30");
	cmdKey (0x28, 0);
	std::string ferr = g_cmd.err;
	check ("congé R 30 refusé avant d'être ajouté", g_cmd.tool == T_FILLET && !ferr.empty (), ferr);
	cmdKey (0x2A, 0), cmdKey (0x2A, 0);
	g_cmd.chips[0].locked = false;
	type ("3");
	cmdKey (0x28, 0);
	snprintf (s, sizeof s, "%s, %s", g_doc.feats.back ().name.c_str (), featureDetail (g_doc.feats.back ()).c_str ());
	check ("congé R 3 sur l'arête avant", g_doc.feats.back ().kind == ST_FILLET && g_doc.feats.back ().error.empty (), s);
	// la barre de reconstruction, annuler, rétablir
	int n = (int) g_doc.feats.size ();
	double vFull = totalVolume ();
	pushUndo ();
	g_doc.bar = 3;
	rebuild ();
	double vBar = totalVolume ();
	undo (false);
	double vBack = totalVolume ();
	undo (true);
	bool barOk = std::fabs (totalVolume () - vBar) < 1e-6;
	undo (false);
	snprintf (s, sizeof s, "barre après 3 : %s ; annulé : %s ; %d étapes", cm3 (vBar).c_str (), cm3 (vBack).c_str (), n);
	check ("barre de reconstruction, annuler, rétablir", barOk && std::fabs (vBack - vFull) < 1e-6 && std::fabs (vBar - vFull) > 0.1 && g_doc.bar == n, s);
	select (Pick ());
	printf ("forge : %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}

} // namespace

int main (int argc, char **argv)
{
	bool test = argc > 1 && !strcmp (argv[1], "--selftest");
	g_trace = getenv ("FORGE_TRACE") != nullptr;
	if (!uikit_open (&ui)) return 1;
	sampleDoc (g_doc);
	double t0 = nowMs ();
	rebuild ();
	double t1 = nowMs ();
	if (g_doc.bodies.empty ()) {
		printf ("forge : la pièce d'exemple n'a pas pu être faite\n");
		return 1;
	}
	long scr[4] = { 1280, 800, 0, 0 };
	ui_screen ((long) scr);
	int ww = imin (1600, (int) scr[0] - 16), wh = imin (1000, (int) (scr[1] - scr[3]) - imax ((int) scr[2], 60) - 16);
	if (ui_window_ex (8, scr[3] + 4, ww, wh, (long) "Forge", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (800, 560);
	ui_on_resize ((long) onResize, 0);
	buildWindow ();
	relayout ();
	g_view.fit (g_doc);
	repaint ();
	panelUpdate ();
	hintIdle ();
	if (test) {
		int r = selftest (t1 - t0);
		if (argc > 2) loop (nowMs () + atoi (argv[2]) * 1000.0);
		return r;
	}
	loop (0);
	return 0;
}
