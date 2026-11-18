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
//   --mcp [port]         (avec l'une ou l'autre) le pilotage par un agent dès le départ (mcp.cpp, mcptools.inc), port
//                        7311 par défaut ; --mcp-token t : le jeton demandé (sans : celui gardé, dans le coffre à
//                        secrets ou HOME:/forge/mcp.txt, mcpToken ; « - » : aucun). Sinon : Fichier > Piloter par un agent (MCP).
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
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include <uikit.h>
#include <archivekit_abi.h>
#include <netkit_abi.h>
#include <Standard_Failure.hxx>
#include "json.h"
#include "agent.h"
#include "mcp.h"
#include "model.h"
#include "render.h"
#include "../../lib/keyvault/keyvault.h"

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
int g_selStepWas = -1;			// (au début d'une commande)
long g_surf;				// la surface de la vue
int VX, VY, VW, VH;			// la zone de la vue dans le contenu
int TB_H, TL_H, ST_H, PW;		// barre, frise, état, panneau
bool g_quit;

Pick sel0 () { return g_sels.empty () ? Pick () : g_sels[0]; }

// widgets
long w_doc, w_docsub, w_mode, w_bar, w_export, w_tl, w_status, w_stats;
long p_title, p_sub, p_sec, p_name[8], p_val[8], p_state, p_hint;
long w_op, b_ok, b_cancel, w_pmode, w_paxis, b_usemax;	// la commande en cours
long e_param, b_param;				// les paramètres nommés
long e_val[3], b_apply, b_off, b_del, b_barhere;	// l'étape choisie
enum { TB_SKETCH, TB_BOX, TB_CYL, TB_PUSH, TB_FILLET, TB_CHAMFER, TB_UNION, TB_CUT, TB_MEASURE, TB_N };
long tb[TB_N];

// ---- annuler, rétablir ------------------------------------------------------------------------------------------
struct Snap {
	std::vector<Feature> feats;
	int bar;
	std::vector<std::pair<std::string, std::string>> params;
};
std::vector<Snap> g_undo, g_redo;
void pushUndo ()
{
	g_undo.push_back ({ g_doc.feats, g_doc.bar, g_doc.params });
	if (g_undo.size () > 100) g_undo.erase (g_undo.begin ());
	g_redo.clear ();
}

// ---- la commande en cours ---------------------------------------------------------------------------------------
enum Tool { T_NONE, T_BOX, T_CYL, T_PUSH, T_FILLET, T_CHAMFER, T_UNION, T_CUT, T_EXTRUDE, T_REVOLVE, T_HOLE, T_SHELL, T_PATTERN, T_MIRROR };
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
	bool mirrorFace = false;	// (symétrie) le plan est une face choisie
	Mesh ghost;
	std::string err;
	double maxR = 0;		// (congé refusé) le plus grand rayon qui passe
} g_cmd;

StepKind toolKind (Tool t)
{
	static const StepKind K[] = { ST_BOX, ST_BOX, ST_CYLINDER, ST_PUSH, ST_FILLET, ST_CHAMFER, ST_UNION, ST_CUT, ST_EXTRUDE, ST_REVOLVE, ST_HOLE, ST_SHELL, ST_PATTERN, ST_MIRROR };
	return K[t];
}
bool repeatable (StepKind k) { return k == ST_BOX || k == ST_CYLINDER || k == ST_HOLE || k == ST_SLOT || k == ST_EXTRUDE || k == ST_REVOLVE; }

enum SkTool { SK_NONE, SK_LINE, SK_RECT, SK_CIRCLE, SK_ARC, SK_DIM };
struct SkSel { int kind, i; };		// 0 élément, 1 point
struct SkMode {
	bool on = false;
	int edit = -1;			// l'étape modifiée (-1 : une nouvelle esquisse)
	Sketch s;
	SkTool tool = SK_NONE;
	std::vector<int> clicks;	// les points posés par l'outil en cours
	int chainStart = -1;		// (ligne) le premier point de la suite
	double mx = 0, my = 0;		// le pointeur, accroché, dans l'esquisse
	int snapPt = -1, hoverGeo = -1;
	std::vector<SkSel> sel;
	int dimCons = -1;		// la cote qu'on tape
	std::string dimText, message;
	std::vector<int> lastConflict;
	Camera saved;
} g_sm;
long w_sktools;
enum { SKB_H, SKB_V, SKB_COINC, SKB_PAR, SKB_PERP, SKB_TAN, SKB_EQ, SKB_CONC, SKB_ON, SKB_CONS, SKB_DEL, SKB_CANCEL, SKB_DONE, SKB_N };
long skb[SKB_N];
void skStrokes ();
void skLabels (std::vector<std::array<int, 4>> &Z, int X, int Y);
void skPanel ();

const char *toolName (Tool t)
{
	static const char *const N[] = { "", "Boîte", "Cylindre", "Pousser/tirer", "Congé", "Chanfrein", "Union", "Soustraction", "Extrusion", "Révolution", "Perçage", "Coque", "Réseau", "Symétrie" };
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

// l'accrochage : les points posés et les tailles tirées au geste tombent sur des multiples du pas (Affichage ; la
// grille a un trait tous les 10 mm). Une valeur tapée n'est pas arrondie.
double g_snap = 5;
double snapv (double v) { return std::round (v / g_snap) * g_snap; }

std::vector<uint32_t> g_back;	// le tampon de rendu (recopié dans la surface une fois l'image finie)
std::vector<std::array<int, 4>> g_prevZ, g_prevMarks;	// les zones par-dessus la vue à la dernière peinture
bool g_camDirty;		// la caméra a bougé : l'image se refait quand plus aucun événement n'attend
bool g_cmdDirty;		// le geste a changé une valeur : l'aperçu se refait de même

void renderView ()
{
	long pitch = 0, w = 0;
	uint32_t *pix = (uint32_t *) uikit_call3 ((long (*) ()) ui_surface_pixels, g_surf, 0, 0, 0, &pitch, &w);
	if (!pix) return;
	g_camDirty = false;
	g_back.resize ((size_t) VW * VH);
	g_view.ghost = g_cmd.tool != T_NONE && !g_cmd.ghost.pts.empty () ? &g_cmd.ghost : nullptr;
	g_view.ghostCut = g_cmd.f.op == OP_CUT || ((g_cmd.tool == T_PUSH || g_cmd.tool == T_EXTRUDE) && g_cmd.f.a < 0);
	skStrokes ();
	g_view.render (g_back.data (), VW, g_doc, g_pal, g_sels, g_hot);
	for (int y = 0; y < VH; y++) memcpy (pix + (size_t) y * (pitch / 4), g_back.data () + (size_t) y * VW, (size_t) VW * 4);
	ui_surface_damage (g_surf, 0, 0, 0, 0);
	char s[160];
	int tris = 0;
	for (auto &b : g_doc.bodies) tris += (int) b.mesh.triFace.size ();
	snprintf (s, sizeof s, "%s%d triangles   ·   rendu %.0f ms   ·   Accrochage %s mm   ·   mm", mcp::running () ? "Agent (MCP)   ·   " : "", tris, g_view.lastMs,
		  fmtNum (g_snap).c_str ());
	ui_w_set_text (w_stats, (long) s);
}

// les zones de la vue posées par-dessus la surface : corps, cube, boutons, trièdre, puces de cote
void paintOverlays ()
{
	const int X = VX, Y = VY;
	int cx, cy, cw, ch, bx0, by0;
	g_view.cubeRect (cx, cy, cw, ch);
	vbtnRect (0, bx0, by0);
	std::vector<std::array<int, 4>> Z = { { BP_X, BP_Y, BP_W, bodiesH () }, { cx, cy, cw, ch }, { bx0, by0, VB_W, VB_N * (VB_H + 6) }, { 0, VH - 96, 96, 96 } };
	// les puces de cote : leur place
	for (auto &c : g_cmd.chips) {
		double sx, sy;
		g_view.toScreen (c.at, sx, sy);
		std::string t = std::string (TR (c.label)) + "  " + (c.text.empty () ? fmtNum (c.val) : c.text) + "|";
		c.sw = (int) ui_text_width ((long) t.c_str (), 13, (long) ui.font) + 24;
		c.sx = imax (2, imin (VW - c.sw - 2, (int) sx + 10)), c.sy = imax (2, imin (VH - 28, (int) sy - 30));
		Z.push_back ({ c.sx, c.sy, c.sw, 26 });
	}
	auto has = [] (const std::vector<std::array<int, 4>> &v, const std::array<int, 4> &z) { return std::find (v.begin (), v.end (), z) != v.end (); };
	for (auto &z : Z)
		if (!has (g_prevZ, z)) ui_fill (X + z[0], Y + z[1], z[2], z[3], (long) 0xFF000000u);
	for (auto &m : g_prevMarks) ui_fill (X + m[0], Y + m[1], m[2], m[3], (long) 0xFF000000u);	// (étiquettes du cube, lettres du trièdre)
	std::vector<std::array<int, 4>> marks;
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
		marks.push_back ({ lx[f] - (int) tw / 2 - 2, ly[f] - 6, (int) tw + 4, 13 });
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
		marks.push_back ({ x - 5 - X, y - 9 - Y, 14, 17 });
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
		std::string v = (c.text.empty () ? fmtNum (c.val) : c.text) + (act ? "|" : "");
		ui_text ((long) v.c_str (), X + c.sx + 16 + lw, Y + c.sy + 18, jet (TJ_TEXT1), 13, (long) (c.locked ? ui.font_b : ui.font), 0);
	}
	g_prevZ = Z;
	g_prevMarks = marks;
	skLabels (Z, X, Y);
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
	bool isSk = g_selStep >= 0 && g_selStep < (int) g_doc.feats.size () && g_doc.feats[g_selStep].kind == ST_SKETCH;
	ui_w_show (b_apply, on && (fields > 0 || isSk));
	ui_w_set_text (b_apply, (long) TR (isSk ? "Modifier l'esquisse" : "Appliquer"));
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
	case ST_EXTRUDE: names[0] = "Distance", vals[0] = f.a; return 1;
	case ST_REVOLVE: names[0] = "Angle", vals[0] = f.a; return 1;
	default: return 0;
	}
}

void featSet (Feature &f, int i, double v)
{
	switch (f.kind) {
	case ST_BOX: (i == 0 ? f.a : i == 1 ? f.b : f.c) = v; break;
	case ST_CYLINDER: if (i == 0) f.a = v / 2; else f.c = v; break;
	case ST_FILLET: case ST_CHAMFER: case ST_PUSH: case ST_EXTRUDE: case ST_REVOLVE: f.a = v; break;
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
	ui_w_show (w_pmode, false);
	ui_w_show (w_paxis, false);
	ui_w_show (b_usemax, false);
	ui_w_show (e_param, false);
	ui_w_show (b_param, false);
	ui_w_show (w_sktools, false);
	for (int i = 0; i < SKB_N; i++) ui_w_show (skb[i], false);
	ui_w_show (p_hint, !g_sm.on);	// (en esquisse, ses boutons prennent le bas du panneau)
	std::string state;
	if (g_sm.on) {
		skPanel ();
		int W = (int) ui.w, H = (int) ui.h;
		ui_fill (W - PW + 1, TB_H, PW - 1, H - TB_H - ST_H, jet (TJ_FACE));
		ui_w_draw (0);
		ui_update (W - PW, TB_H, PW, H - TB_H - ST_H);
		return;
	}
	if (g_cmd.tool != T_NONE) {
		static const char *const HINT[14][3] = {
			{ "", "", "" },
			{ "Clic : le centre de la base, sur une face ou sur le sol", "On s'éloigne pour la largeur et la profondeur ; clic", "On monte pour la hauteur ; clic ou Entrée" },
			{ "Clic : le centre de la base, sur une face ou sur le sol", "On s'éloigne pour le rayon ; clic", "On monte pour la hauteur ; clic ou Entrée" },
			{ "Clic : la face plane à pousser ou à tirer", "Glisser pour la distance ; clic ou Entrée", "" },
			{ "Clic : les arêtes (Maj : plusieurs) ; Entrée", "Tapez le rayon ; Entrée", "" },
			{ "Clic : les arêtes (Maj : plusieurs) ; Entrée", "Tapez la distance ; Entrée", "" },
			{ "Clic : le corps qui reste", "Clic : le corps à joindre", "" },
			{ "Clic : le corps à garder", "Clic : le corps à retirer", "" },
			{ "Une esquisse fermée : l'étape choisie dans la frise, ou la dernière", "Glisser le long de la normale, ou tapez la distance ; Entrée", "" },
			{ "Une esquisse avec une ligne de construction (l'axe)", "Tapez l'angle ; Entrée", "" },
			{ "Clic : l'emplacement, sur une face plane", "Maj+clic : un trou de plus ; le diamètre, la profondeur (0 : traversant) ; Entrée", "" },
			{ "Clic : les faces à ouvrir (Maj : plusieurs)", "L'épaisseur des parois ; Entrée", "" },
			{ "L'étape répétée : celle choisie dans la frise, ou la dernière forme", "Le nombre et le pas (ou l'angle) ; Entrée", "" },
			{ "Le plan : la face choisie, sinon X, Y ou Z par l'origine", "Joindre (pièce symétrique) ou nouveau corps ; Entrée", "" },
		};
		ui_w_set_text (p_title, (long) TR (toolName (g_cmd.tool)));
		ui_w_set_text (p_sub, (long) TR (HINT[g_cmd.tool][imin (g_cmd.stage, 2)]));
		ui_w_set_text (p_sec, (long) TR ("Valeurs"));
		std::vector<std::pair<std::string, std::string>> rows;
		for (auto &c : g_cmd.chips) rows.push_back ({ TR (c.label), (c.text.empty () ? fmtNum (c.val) : c.text) + " mm" + (c.locked ? "  (tenue)" : "") });
		bool hasOp = g_cmd.tool == T_BOX || g_cmd.tool == T_CYL || g_cmd.tool == T_EXTRUDE || g_cmd.tool == T_REVOLVE;
		static const char *const OPS[4] = { "nouveau", "joindre", "enlever", "intersection" };
		if (hasOp) rows.push_back ({ TR ("Opération"), std::string (TR (OPS[g_cmd.f.op])) + (g_cmd.opAuto ? TR (" · auto") : "") });
		if (g_cmd.tool == T_FILLET || g_cmd.tool == T_CHAMFER) rows.push_back ({ TR ("Arêtes"), std::to_string (g_cmd.f.edges.size ()) });
		if (g_cmd.tool == T_SHELL) rows.push_back ({ TR ("Faces ouvertes"), std::to_string (g_cmd.f.faces.size ()) });
		if (g_cmd.tool == T_HOLE) rows.push_back ({ TR ("Trous"), std::to_string (g_cmd.f.pos.size () / 2) });
		if (g_cmd.tool == T_PATTERN && g_cmd.f.ref >= 0) rows.push_back ({ TR ("Répète"), g_doc.feats[g_cmd.f.ref].name });
		panelRows (rows);
		if (g_cmd.tool == T_PATTERN) {
			ui_w_show (w_pmode, true);
			ui_w_set_value (w_pmode, g_cmd.f.mode);
		}
		if (g_cmd.tool == T_PATTERN || (g_cmd.tool == T_MIRROR && !g_cmd.mirrorFace)) {
			ui_w_show (w_paxis, true);
			const double *n = g_cmd.f.frame.n;
			ui_w_set_value (w_paxis, std::fabs (n[0]) > 0.9 ? 0 : std::fabs (n[1]) > 0.9 ? 1 : 2);
		}
		if (g_cmd.tool == T_MIRROR) {
			ui_w_show (w_op, true);
			ui_w_set_value (w_op, g_cmd.f.op == OP_JOIN ? 1 : 0);
		}
		if (g_cmd.maxR > 0) {
			std::string t = std::string (TR ("Utiliser R ")) + fmtNum (g_cmd.maxR);
			ui_w_set_text (b_usemax, (long) t.c_str ());
			ui_w_show (b_usemax, true);
		}
		if (hasOp) {
			ui_w_show (w_op, true);
			ui_w_set_value (w_op, g_cmd.f.op == OP_NEW ? 0 : g_cmd.f.op == OP_JOIN ? 1 : g_cmd.f.op == OP_CUT ? 2 : 3);
		}
		ui_w_show (b_ok, true);
		ui_w_show (b_cancel, true);
		state = g_cmd.err.empty () ? "" : "⚠ " + g_cmd.err;
		if (g_cmd.maxR > 0) state += std::string (TR (" Le plus grand rayon qui passe : ")) + fmtNum (g_cmd.maxR) + " mm.";
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
		for (int i = 0; i < n; i++) {
			std::string t = fmtNum (vals[i]);
			for (auto &e : f.expr) if (e.first == i) t = e.second;	// (une formule : telle qu'écrite)
			ui_w_set_text (e_val[i], (long) t.c_str ());
		}
		showEditWidgets (n, true);
		ui_w_set_text (b_off, (long) TR (f.off ? "Activer" : "Désactiver"));
		state = f.error.empty () ? "" : "⚠ " + f.error;
	} else if (sel0 ().none ()) {
		ui_w_set_text (p_title, (long) g_doc.name.c_str ());
		ui_w_set_text (p_sub, (long) TR ("Document · mm"));
		ui_w_set_text (p_sec, (long) TR ("Document"));
		int err = 0;
		for (auto &f : g_doc.feats) err += !f.error.empty ();
		std::vector<std::pair<std::string, std::string>> rows = { { TR ("Corps"), std::to_string (liveBodies ()) },
			{ TR ("Étapes"), std::to_string (g_doc.feats.size ()) }, { TR ("En erreur"), std::to_string (err) } };
		for (auto &pr : g_doc.params) {	// les paramètres : nom, formule = valeur
			double v = 0;
			bool ok = evalExpr (g_doc, pr.second.c_str (), v);
			bool plain = evalExpr (pr.second.c_str (), v) && pr.second.find_first_of ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_") == std::string::npos;
			evalExpr (g_doc, pr.second.c_str (), v);
			rows.push_back ({ pr.first, ok ? (plain ? fmtNum (v) : pr.second + " = " + fmtNum (v)) : pr.second + " ?" });
		}
		panelRows (rows);
		ui_w_show (e_param, true);
		ui_w_show (b_param, true);
		state = g_doc.params.empty () ? TR ("Paramètres : « épaisseur = 4 », puis « épaisseur » ou « épaisseur * 2 » dans une valeur.") : "";
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
	g_cmdDirty = false;
	g_view.ghost = nullptr;
	for (int i = 0; i < TB_N; i++) ui_w_set_value (tb[i], 0);
	refreshAll ();
	hintIdle ();
}

// la boîte, le cylindre : la valeur des puces dans la forme
void cmdApplyChips ()
{
	for (auto &c : g_cmd.chips) {	// (une valeur tapée, pas encore prise par Entrée ou Tab)
		double v;
		if (!c.text.empty () && evalExpr (g_doc, c.text.c_str (), v)) c.val = v, c.locked = true;	// (le texte reste affiché : la saisie continue)
	}
	Feature &f = g_cmd.f;
	auto v = [&] (int i) { return i < (int) g_cmd.chips.size () ? g_cmd.chips[i].val : 0.0; };
	switch (g_cmd.tool) {
	case T_BOX: f.a = v (0), f.b = v (1), f.c = g_cmd.stage >= 2 ? v (2) : 1; break;
	case T_CYL: f.a = v (0) / 2, f.c = g_cmd.stage >= 2 ? v (1) : 1; break;
	case T_PUSH: f.a = v (0); break;
	case T_FILLET: case T_CHAMFER: f.a = v (0); break;
	case T_EXTRUDE: case T_REVOLVE: f.a = v (0); break;
	case T_HOLE: f.d1 = v (0), f.c = v (1); break;
	case T_SHELL: f.a = v (0); break;
	case T_PATTERN: f.count = (int) std::lround (v (0)), f.a = v (1); break;
	default: break;
	}
	if (g_cmd.tool == T_HOLE || g_cmd.tool == T_PATTERN)
		for (size_t i = 0; i < g_cmd.chips.size (); i++)
			for (int k = 0; k < 3; k++) g_cmd.chips[i].at[k] = f.frame.o[k] + (i ? 1 : 0) * 0;
	if (g_cmd.tool == T_SHELL && !f.faces.empty () && !g_cmd.chips.empty ())
		for (int k = 0; k < 3; k++) g_cmd.chips[0].at[k] = f.faces[0].pt[k];
	// extrusion, révolution : sur une face d'un corps, sortir joint et creuser enlève ; sur un plan, nouveau corps
	if (g_cmd.opAuto && (g_cmd.tool == T_EXTRUDE || g_cmd.tool == T_REVOLVE) && f.ref >= 0) {
		int b = g_doc.feats[f.ref].sk.body;
		if (b < 0 || b >= (int) g_doc.bodies.size () || g_doc.bodies[b].shape.IsNull ()) f.op = OP_NEW, f.body = -1;
		else f.op = g_cmd.tool == T_EXTRUDE && f.a < 0 ? OP_CUT : OP_JOIN, f.body = b;
	}
	if ((g_cmd.tool == T_EXTRUDE || g_cmd.tool == T_REVOLVE) && f.ref >= 0 && !g_cmd.chips.empty ()) {	// (la puce : au centre de l'esquisse)
		const Sketch &sk = g_doc.feats[f.ref].sk;
		double cx = 0, cy = 0;
		for (auto &p : sk.pts) cx += p.x, cy += p.y;
		skWorld (sk, cx / sk.pts.size (), cy / sk.pts.size (), g_cmd.chips[0].at);
		if (g_cmd.tool == T_EXTRUDE)
			for (int i = 0; i < 3; i++) g_cmd.chips[0].at[i] += sk.plane.n[i] * f.a;
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

bool g_typing;	// (l'aperçu suit la frappe : sans la recherche du rayon maximal, trop lente pour chaque touche)
void cmdRefresh ()
{
	cmdApplyChips ();
	g_cmd.maxR = 0;
	Tool tt = g_cmd.tool;
	bool ready = (tt == T_FILLET || tt == T_CHAMFER) ? !g_cmd.f.edges.empty () : tt == T_SHELL ? !g_cmd.f.faces.empty ()
		: tt == T_PATTERN ? g_cmd.f.ref >= 0 : tt == T_MIRROR ? g_cmd.f.body >= 0 : false;
	if ((tt == T_FILLET || tt == T_CHAMFER || tt == T_SHELL || tt == T_PATTERN || tt == T_MIRROR) && !ready) {
		g_cmd.ghost = Mesh ();
		g_cmd.err.clear ();
	} else if (tt == T_FILLET || tt == T_CHAMFER || tt == T_SHELL || tt == T_PATTERN || tt == T_MIRROR) {	// (l'aperçu : la vraie forme, calculée)
		Doc probe = g_doc;
		insertFeature (probe, probe.bar, g_cmd.f);
		replay (probe, 0.2);
		g_cmd.err = probe.feats[probe.bar - 1].error;
		g_cmd.ghost = Mesh ();
		int gb = tt == T_PATTERN && g_cmd.f.ref >= 0 ? g_doc.feats[g_cmd.f.ref].body : g_cmd.f.body;
		if (g_cmd.err.empty () && gb >= 0 && gb < (int) probe.bodies.size ())
			meshShape (probe.bodies[gb].shape, g_cmd.ghost, 0.2);
		if (!g_typing && !g_cmd.err.empty () && tt == T_FILLET && g_cmd.f.body >= 0 && g_cmd.f.body < (int) g_doc.bodies.size ())
			g_cmd.maxR = filletMax (g_doc.bodies[g_cmd.f.body].shape, g_cmd.f.edges, g_cmd.f.a);	// (dichotomie)
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
	g_selStepWas = g_selStep;
	g_selStep = -1;
	Feature &f = g_cmd.f;
	f.kind = toolKind (t);
	f.name = featureName (g_doc, f.kind);
	if (t <= T_CUT)
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
	if (t == T_EXTRUDE || t == T_REVOLVE) {	// l'esquisse : l'étape choisie, sinon la dernière avant la barre
		int ref = g_selStepWas >= 0 && g_selStepWas < (int) g_doc.feats.size () && g_doc.feats[g_selStepWas].kind == ST_SKETCH ? g_selStepWas : -1;
		for (int i = g_doc.bar - 1; i >= 0 && ref < 0; i--)
			if (g_doc.feats[i].kind == ST_SKETCH) ref = i;
		if (ref < 0) {
			g_cmd = Cmd ();
			status (TR ("Aucune esquisse : S pour en dessiner une."));
			return;
		}
		std::string err;
		if (skFaces (g_doc.feats[ref].sk, err).IsNull ()) {
			g_cmd = Cmd ();
			status ((std::string (TR ("L'esquisse n'a ")) + err + ".").c_str ());
			return;
		}
		double o[3], d[3];
		if (t == T_REVOLVE && !skAxis (g_doc.feats[ref].sk, o, d)) {
			g_cmd = Cmd ();
			status (TR ("Révolution : l'esquisse n'a pas d'axe (une ligne de construction : X sur une ligne)."));
			return;
		}
		f.ref = ref;
		f.kind = t == T_EXTRUDE ? ST_EXTRUDE : ST_REVOLVE;
		f.name = featureName (g_doc, f.kind);
		g_cmd.stage = 1;
		Chip c;
		c.label = t == T_EXTRUDE ? "Distance" : "Angle";
		c.val = t == T_EXTRUDE ? 10 : 360;
		g_cmd.chips.push_back (c);
	}
	if (t == T_HOLE) {
		Chip a, b;
		a.label = "Diamètre", a.val = 6;
		b.label = "Profondeur", b.val = 0;
		g_cmd.chips = { a, b };
		f.op = OP_CUT;
		Frame fr;
		if (!k.none () && k.face >= 0 && facePlane (g_doc.bodies[k.body], k.face, fr)) {
			f.frame = fr, f.body = k.body, f.pos = { 0, 0 }, g_cmd.stage = 1;
			{ FaceRef r; if (faceRef (g_doc.bodies[k.body], k.face, r)) f.faces = { r }; }	// (la face : le perçage la suit)
			for (int i = 0; i < 3; i++) f.frame.o[i] = snapv (fr.o[i]);
		}
	}
	if (t == T_SHELL) {
		Chip c;
		c.label = "Épaisseur", c.val = 2;
		g_cmd.chips = { c };
		for (auto &sl : g_sels) {
			FaceRef r;
			if (sl.face >= 0 && (f.body < 0 || f.body == sl.body) && faceRef (g_doc.bodies[sl.body], sl.face, r)) f.body = sl.body, f.faces.push_back (r);
		}
		if (!f.faces.empty ()) g_cmd.stage = 1;
	}
	if (t == T_PATTERN) {	// l'étape répétée : celle choisie dans la frise, sinon la dernière forme avant la barre
		int ref = g_selStepWas >= 0 && g_selStepWas < g_doc.bar && repeatable (g_doc.feats[g_selStepWas].kind) ? g_selStepWas : -1;
		for (int i = g_doc.bar - 1; i >= 0 && ref < 0; i--)
			if (repeatable (g_doc.feats[i].kind) && g_doc.feats[i].error.empty ()) ref = i;
		if (ref < 0) {
			g_cmd = Cmd ();
			status (TR ("Réseau : aucune forme, perçage ou extrusion à répéter."));
			return;
		}
		f.ref = ref, f.mode = 0, f.count = 3, f.a = 10;
		f.frame.n[0] = 1, f.frame.n[1] = 0, f.frame.n[2] = 0;
		int b = g_doc.feats[ref].body >= 0 ? g_doc.feats[ref].body : 0;	// (le centre du corps : l'axe d'un réseau circulaire)
		if (b < (int) g_doc.bodies.size () && !g_doc.bodies[b].mesh.pts.empty ())
			for (int i = 0; i < 3; i++) f.frame.o[i] = std::round ((g_doc.bodies[b].mesh.lo[i] + g_doc.bodies[b].mesh.hi[i]) / 2);
		Chip c, d;
		c.label = "Nombre", c.val = 3;
		d.label = "Pas", d.val = 10;
		g_cmd.chips = { c, d };
		g_cmd.stage = 1;
	}
	if (t == T_MIRROR) {	// le corps choisi (ou le premier) ; le plan : la face choisie, sinon X par l'origine
		f.body = !k.none () ? k.body : -1;
		for (size_t i = 0; i < g_doc.bodies.size () && f.body < 0; i++)
			if (!g_doc.bodies[i].shape.IsNull ()) f.body = (int) i;
		Frame fr;
		if (!k.none () && k.face >= 0 && facePlane (g_doc.bodies[k.body], k.face, fr)) f.frame = fr, g_cmd.mirrorFace = true;
		else f.frame = Frame (), f.frame.n[0] = 1, f.frame.n[2] = 0;
		f.op = OP_JOIN;
		g_cmd.stage = 1;
	}
	g_sels.clear ();
	g_cmd.active = 0;	// (le clavier : la première valeur ; Tab, la suivante)
	ui_w_focus (0);
	cmdRefresh ();
	status (TR (toolName (t)));
}

void cmdCommit ()
{
	Feature f = g_cmd.f;
	if (g_cmd.tool == T_BOX && (f.a < 0.01 || f.b < 0.01 || std::fabs (f.c) < 0.01)) return;
	if ((g_cmd.tool == T_FILLET || g_cmd.tool == T_CHAMFER) && (f.edges.empty () || !g_cmd.err.empty ())) return;
	if (g_cmd.tool == T_HOLE && (f.body < 0 || f.pos.empty () || f.d1 <= 0)) return;
	if ((g_cmd.tool == T_SHELL || g_cmd.tool == T_PATTERN || g_cmd.tool == T_MIRROR) && !g_cmd.err.empty ()) return;
	if (g_cmd.tool == T_SHELL && f.faces.empty ()) return;
	pushUndo ();
	insertFeature (g_doc, g_doc.bar, f);
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
		for (int i = 0; i < 3; i++) fr.o[i] = snapv (p[i]);
		double d = (fr.o[0] - p[0]) * fr.n[0] + (fr.o[1] - p[1]) * fr.n[1] + (fr.o[2] - p[2]) * fr.n[2];
		for (int i = 0; i < 3; i++) fr.o[i] -= d * fr.n[i];	// (resté sur le plan)
		body = k.body;
		return true;
	}
	fr = Frame ();
	double p[3];
	if (!g_view.onPlane (x, y, fr.o, fr.n, p)) return false;
	fr.o[0] = snapv (p[0]), fr.o[1] = snapv (p[1]), fr.o[2] = 0;
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
	return snapv (((x - sx) * vx + (y - sy) * vy) / l2 * 10);
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
				if (!g_cmd.chips[0].locked) g_cmd.chips[0].val = std::max (g_snap, snapv (2 * std::fabs (lx)));
				if (!g_cmd.chips[1].locked) g_cmd.chips[1].val = std::max (g_snap, snapv (2 * std::fabs (ly)));
			} else if (!g_cmd.chips[0].locked)
				g_cmd.chips[0].val = std::max (g_snap, snapv (2 * sqrt (lx * lx + ly * ly)));
			g_cmdDirty = true;
			return true;
		}
		if ((t == T_BOX || t == T_CYL) && g_cmd.stage == 2) {
			Chip &h = g_cmd.chips.back ();
			if (!h.locked) {
				double v = alongNormal (f.frame.o, f.frame.n, x, y);
				h.val = v == 0 ? (h.val < 0 ? -1 : 1) : v;
			}
			g_cmdDirty = true;
			return true;
		}
		if (t == T_EXTRUDE && g_cmd.stage == 1 && !g_cmd.chips[0].locked) {
			const Sketch &sk = g_doc.feats[f.ref].sk;
			double v = alongNormal (sk.plane.o, sk.plane.n, x, y);
			if (v != g_cmd.chips[0].val && v != 0) {
				g_cmd.chips[0].val = v;
				g_cmdDirty = true;
			}
			return true;
		}
		if (t == T_PUSH && g_cmd.stage == 1 && !g_cmd.chips[0].locked) {
			double v = alongNormal (f.face.pt, f.face.n, x, y);
			if (v != g_cmd.chips[0].val && v != 0) {
				g_cmd.chips[0].val = v;
				g_cmdDirty = true;
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
	if (t == T_EXTRUDE || t == T_REVOLVE) {
		cmdApplyChips ();
		cmdCommit ();
		return true;
	}
	if (t == T_HOLE) {	// le premier clic : la face et le premier trou ; Maj+clic : un trou de plus ; un clic : fini
		Pick k = g_view.pick (g_doc, x, y);
		Frame fr;
		if (g_cmd.stage == 0) {
			if (k.face < 0 || !facePlane (g_doc.bodies[k.body], k.face, fr)) {
				status (TR ("Perçage : une face plane."));
				return true;
			}
			double p[3];
			if (!g_view.onPlane (x, y, fr.o, fr.n, p)) return true;
			for (int i = 0; i < 3; i++) fr.o[i] = snapv (p[i]);	// (accroché)
			f.frame = fr, f.body = k.body, f.pos = { 0, 0 }, g_cmd.stage = 1;
			{ FaceRef r; if (faceRef (g_doc.bodies[k.body], k.face, r)) f.faces = { r }; }	// (la face : le perçage la suit)
			cmdRefresh ();
			return true;
		}
		if (shift) {
			double p[3];
			if (!g_view.onPlane (x, y, f.frame.o, f.frame.n, p)) return true;
			const double *n = f.frame.n, *xa = f.frame.x;
			double ya[3] = { n[1] * xa[2] - n[2] * xa[1], n[2] * xa[0] - n[0] * xa[2], n[0] * xa[1] - n[1] * xa[0] };
			double d[3] = { p[0] - f.frame.o[0], p[1] - f.frame.o[1], p[2] - f.frame.o[2] };
			f.pos.push_back (snapv (d[0] * xa[0] + d[1] * xa[1] + d[2] * xa[2]));
			f.pos.push_back (snapv (d[0] * ya[0] + d[1] * ya[1] + d[2] * ya[2]));
			cmdRefresh ();
			return true;
		}
		cmdApplyChips ();
		cmdCommit ();
		return true;
	}
	if (t == T_SHELL) {	// les faces à ouvrir (Maj : plusieurs)
		Pick k = g_view.pick (g_doc, x, y);
		FaceRef r;
		if (k.face < 0 || !faceRef (g_doc.bodies[k.body], k.face, r)) return true;
		if (f.body >= 0 && k.body != f.body) {
			status (TR ("Les faces d'un même corps."));
			return true;
		}
		f.body = k.body;
		if (!shift) f.faces.clear ();
		f.faces.push_back (r);
		g_cmd.stage = 1;
		cmdRefresh ();
		return true;
	}
	if (t == T_PATTERN || t == T_MIRROR) return true;	// (les valeurs au panneau ; Entrée ou OK)
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
// la valeur en cours de frappe : l'aperçu la suit dès qu'elle se lit (« 2* » garde l'aperçu d'avant)
void liveRefresh ()
{
	g_typing = true;
	cmdRefresh ();
	g_typing = false;
}

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
		if (!evalExpr (g_doc, c->text.c_str (), v)) {
			g_cmd.err = TR ("valeur illisible ou paramètre inconnu : ") + c->text;
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
			if (t == T_FILLET || t == T_CHAMFER || t == T_SHELL || t == T_PATTERN || t == T_MIRROR) {	// (l'aperçu d'abord : refusée s'il ne passe pas)
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
		liveRefresh ();
		return true;
	}
	if (ch > 0 && (isdigit (ch) || (ch < 128 && (isalpha (ch) || strchr (".,+-*/() _", ch))) || ch >= 0xC0)) {
		if (ch < 128) c->text += (char) ch;
		else if (ch < 0x800) c->text += (char) (0xC0 | ch >> 6), c->text += (char) (0x80 | (ch & 63));	// (é, è : en UTF-8)
		g_cmd.err.clear ();
		liveRefresh ();
		return true;
	}
	return false;
}

// ---- le mode esquisse -------------------------------------------------------------------------------------------
//
// L'esquisse se dessine sur un plan (la face plane choisie, sinon le plan XY), vue de face. Ligne (L, à la suite ;
// fermée en revenant au premier point), rectangle (R, deux coins), cercle (C, centre puis rayon), arc (A, centre,
// début, fin). Un point se pose sur un point existant à moins de 8 pixels (coïncidence), sinon au mm. Une ligne
// presque horizontale ou verticale le devient (contrainte déduite ; Maj : sans). Cote (D) : une ligne -> sa
// longueur, un cercle -> son diamètre, un arc -> son rayon, deux points -> leur distance ; la valeur se tape. Les
// contraintes s'appliquent à ce qui est choisi (panneau, ou H, V, =). Glisser un point montre ce qui bouge encore.
// Après chaque changement, PlaneGCS résout ; un conflit n'est pas appliqué (rien ne bouge) et le panneau le dit.

const char *skToolName (SkTool t)
{
	static const char *const N[] = { "", "Ligne", "Rectangle", "Cercle", "Arc", "Cote" };
	return N[t];
}

void skWorldPt (int i, double out[3]) { skWorld (g_sm.s, g_sm.s.pts[i].x, g_sm.s.pts[i].y, out); }

// une polyligne d'un élément (dans l'espace)
std::vector<double> skPoly (const SkGeo &g)
{
	std::vector<double> v;
	double p[3];
	auto add = [&] (double x, double y) { skWorld (g_sm.s, x, y, p); v.insert (v.end (), { p[0], p[1], p[2] }); };
	if (g.t == SG_LINE) {
		add (g_sm.s.pts[g.a].x, g_sm.s.pts[g.a].y);
		add (g_sm.s.pts[g.b].x, g_sm.s.pts[g.b].y);
	} else {
		const SkPoint &c = g_sm.s.pts[g.c];
		double a0 = g.t == SG_ARC ? g.a0 : 0, a1 = g.t == SG_ARC ? g.a1 : 2 * M_PI;
		int n = imax (8, (int) ((a1 - a0) / (2 * M_PI) * 72));
		for (int k = 0; k <= n; k++) {
			double a = a0 + (a1 - a0) * k / n;
			add (c.x + g.r * cos (a), c.y + g.r * sin (a));
		}
	}
	return v;
}

bool skSelHas (int kind, int i)
{
	for (auto &s : g_sm.sel) if (s.kind == kind && s.i == i) return true;
	return false;
}

bool skInConflict (int g)
{
	for (int k : g_sm.s.conflict) {
		const SkCons &c = g_sm.s.cons[k];
		if (c.g1 == g || c.g2 == g) return true;
		const SkGeo &e = g_sm.s.geos[g];
		for (int p : { e.a, e.b, e.c })
			if (p >= 0 && (c.p1 == p || c.p2 == p)) return true;
	}
	return false;
}

// les traits de l'esquisse pour la vue : libres en bleu, contraints en encre, conflit en rouge, construction en
// pointillés ; le tracé en cours ; les points
void skStrokes ()
{
	g_view.strokes.clear ();
	g_view.dots.clear ();
	if (!g_sm.on) return;
	Sketch &s = g_sm.s;
	unsigned freeCol = g_pal.dark ? (unsigned) ui_mix (jet (TJ_AXIS_Z), 0xFFFFFF, 38) : jet (TJ_AXIS_Z);
	for (size_t g = 0; g < s.geos.size (); g++) {
		const SkGeo &e = s.geos[g];
		bool sel = skSelHas (0, (int) g), hot = g_sm.hoverGeo == (int) g;
		unsigned col = e.cons ? g_pal.grid50 : skInConflict ((int) g) ? jet (TJ_DANGER)
			: (g < s.free.size () && s.free[g]) || s.dof < 0 ? freeCol : jet (TJ_TEXT1);
		if (sel) col = g_pal.accent;
		else if (hot) col = g_pal.hover;
		g_view.strokes.push_back ({ skPoly (e), col, sel || hot ? 3 : 2, e.cons });
	}
	// le tracé en cours : du dernier point au pointeur
	double cur[3];
	skWorld (s, g_sm.mx, g_sm.my, cur);
	if (!g_sm.clicks.empty () && g_sm.tool != SK_DIM) {
		const SkPoint &a = s.pts[g_sm.clicks.back ()];
		std::vector<double> v;
		double p[3];
		auto add = [&] (double x, double y) { skWorld (s, x, y, p); v.insert (v.end (), { p[0], p[1], p[2] }); };
		if (g_sm.tool == SK_LINE) add (a.x, a.y), add (g_sm.mx, g_sm.my);
		else if (g_sm.tool == SK_RECT) add (a.x, a.y), add (g_sm.mx, a.y), add (g_sm.mx, g_sm.my), add (a.x, g_sm.my), add (a.x, a.y);
		else if (g_sm.tool == SK_CIRCLE || (g_sm.tool == SK_ARC && g_sm.clicks.size () == 1)) {
			double r = std::hypot (g_sm.mx - a.x, g_sm.my - a.y);
			for (int k = 0; k <= 72; k++) add (a.x + r * cos (k * M_PI / 36), a.y + r * sin (k * M_PI / 36));
		} else if (g_sm.tool == SK_ARC) {
			const SkPoint &c = s.pts[g_sm.clicks[0]], &b = s.pts[g_sm.clicks[1]];
			double r = std::hypot (b.x - c.x, b.y - c.y), a0 = atan2 (b.y - c.y, b.x - c.x), a1 = atan2 (g_sm.my - c.y, g_sm.mx - c.x);
			while (a1 <= a0) a1 += 2 * M_PI;
			for (int k = 0; k <= 48; k++) add (c.x + r * cos (a0 + (a1 - a0) * k / 48), c.y + r * sin (a0 + (a1 - a0) * k / 48));
		}
		if (!v.empty ()) g_view.strokes.push_back ({ v, g_pal.accent, 2, false });
	}
	for (size_t i = 0; i < s.pts.size (); i++) {
		View::Dot d;
		skWorldPt ((int) i, d.p);
		d.col = (int) i == g_sm.snapPt || skSelHas (1, (int) i) ? g_pal.accent : i == 0 ? jet (TJ_AXIS_X) : jet (TJ_TEXT2);
		d.size = (int) i == g_sm.snapPt ? 3 : 2;
		g_view.dots.push_back (d);
	}
	if (g_sm.tool != SK_NONE) {	// le pointeur accroché
		View::Dot d;
		for (int i = 0; i < 3; i++) d.p[i] = cur[i];
		d.col = g_pal.accent, d.size = 2;
		g_view.dots.push_back (d);
	}
}

// les étiquettes posées sur l'esquisse : cotes (cliquables) et glyphes des contraintes
struct SkLabel {
	std::string text;
	int x, y, w;
	int cons;		// la contrainte
	bool dim;
};
std::vector<SkLabel> g_skLabels;

void skLabels (std::vector<std::array<int, 4>> &Z, int X, int Y)
{
	g_skLabels.clear ();
	if (!g_sm.on) return;
	Sketch &s = g_sm.s;
	for (size_t k = 0; k < s.cons.size (); k++) {
		const SkCons &c = s.cons[k];
		double at[2] = { 0, 0 };
		std::string t;
		bool dim = true;
		auto mid = [&] (int g) {
			const SkGeo &e = s.geos[g];
			if (e.t == SG_LINE) at[0] = (s.pts[e.a].x + s.pts[e.b].x) / 2, at[1] = (s.pts[e.a].y + s.pts[e.b].y) / 2;
			else at[0] = s.pts[e.c].x + e.r * 0.7071, at[1] = s.pts[e.c].y + e.r * 0.7071;
		};
		switch (c.t) {
		case SC_DIST:
			if (c.p2 >= 0) at[0] = (s.pts[c.p1].x + s.pts[c.p2].x) / 2, at[1] = (s.pts[c.p1].y + s.pts[c.p2].y) / 2;
			else mid (c.g1);
			t = fmtNum (c.v);
			break;
		case SC_DIST_X: case SC_DIST_Y:
			at[0] = (s.pts[c.p1].x + s.pts[c.p2].x) / 2, at[1] = (s.pts[c.p1].y + s.pts[c.p2].y) / 2;
			t = (c.t == SC_DIST_X ? "↔ " : "↕ ") + fmtNum (std::fabs (c.v));
			break;
		case SC_RADIUS:
			mid (c.g1);
			t = s.geos[c.g1].t == SG_CIRCLE ? "Ø " + fmtNum (2 * c.v) : "R " + fmtNum (c.v);
			break;
		default: {
			dim = false;
			static const char *const G[] = { "H", "V", "", "//", "90°", "T", "=", "", "", "Pt", "O", "", "" };
			t = G[c.t];
			if (t.empty ()) continue;
			if (c.g1 >= 0) mid (c.g1);
			else if (c.p1 >= 0) at[0] = s.pts[c.p1].x, at[1] = s.pts[c.p1].y;
			break;
		}
		}
		double w3[3], sx, sy;
		skWorld (s, at[0], at[1], w3);
		g_view.toScreen (w3, sx, sy);
		bool bad = std::find (s.conflict.begin (), s.conflict.end (), (int) k) != s.conflict.end ();
		bool editing = (int) k == g_sm.dimCons;
		std::string shown = editing ? g_sm.dimText + "|" : t;
		int tw = (int) ui_text_width ((long) shown.c_str (), 12, (long) ui.font) + (dim ? 14 : 10);
		int lx = imax (2, imin (VW - tw - 2, (int) sx - tw / 2 + (dim ? 0 : 10))), ly = imax (2, imin (VH - 22, (int) sy - (dim ? 22 : -4)));
		if (Z.size () >= 32) break;	// (WS_OVER_MAX)
		Z.push_back ({ lx, ly, tw, 20 });
		g_skLabels.push_back ({ t, lx, ly, tw, (int) k, dim });
		unsigned ln = bad ? jet (TJ_DANGER) : editing ? jet (TJ_ACCENT) : dim ? jet (TJ_LINE_STRONG) : jet (TJ_LINE);
		ui_fill (X + lx, Y + ly, tw, 20, (long) 0xFF000000u);
		ui_rbox (X + lx, Y + ly, tw, 20, dim ? 6 : 10, ln, ln);
		ui_rbox (X + lx + 1, Y + ly + 1, tw - 2, 18, dim ? 5 : 9, jet (TJ_SURFACE), jet (TJ_SURFACE));
		ui_text ((long) shown.c_str (), X + lx + (dim ? 7 : 5), Y + ly + 14, bad ? jet (TJ_DANGER) : jet (dim ? TJ_TEXT1 : TJ_TEXT2), 12,
			 (long) (dim ? ui.font : ui.font_b), 0);
	}
}

// le point de l'esquisse sous le pointeur (dans la vue) : un point existant à moins de 8 pixels, sinon accroché au mm
void skCursor (int x, int y, bool snapGrid = true)
{
	Sketch &s = g_sm.s;
	double p[3];
	if (!g_view.onPlane (x, y, s.plane.o, s.plane.n, p)) return;
	skLocal (s, p, g_sm.mx, g_sm.my);
	g_sm.snapPt = -1;
	double best = 8 * 8;
	for (size_t i = 0; i < s.pts.size (); i++) {
		double w[3], sx, sy;
		skWorldPt ((int) i, w);
		g_view.toScreen (w, sx, sy);
		double d = (sx - x) * (sx - x) + (sy - y) * (sy - y);
		if (d < best) best = d, g_sm.snapPt = (int) i;
	}
	if (g_sm.snapPt >= 0) g_sm.mx = s.pts[g_sm.snapPt].x, g_sm.my = s.pts[g_sm.snapPt].y;
	else if (snapGrid) g_sm.mx = snapv (g_sm.mx), g_sm.my = snapv (g_sm.my);
}

// l'élément sous le pointeur (à moins de 6 pixels) -> rang, -1 : aucun
int skGeoAt (int x, int y)
{
	Sketch &s = g_sm.s;
	int best = -1;
	double bd = 6;
	for (size_t g = 0; g < s.geos.size (); g++) {
		std::vector<double> v = skPoly (s.geos[g]);
		for (size_t k = 0; k + 5 < v.size (); k += 3) {
			double ax, ay, bx, by;
			g_view.toScreen (&v[k], ax, ay);
			g_view.toScreen (&v[k + 3], bx, by);
			double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
			double t = l2 > 0 ? std::max (0.0, std::min (1.0, ((x - ax) * dx + (y - ay) * dy) / l2)) : 0;
			double d = std::hypot (ax + dx * t - x, ay + dy * t - y);
			if (d < bd) bd = d, best = (int) g;
		}
	}
	return best;
}

// le point accroché : un point existant, ou un nouveau
int skPointHere ()
{
	if (g_sm.snapPt >= 0) return g_sm.snapPt;
	return skAddPoint (g_sm.s, g_sm.mx, g_sm.my);
}

void skRefresh ()
{
	renderView ();
	paintOverlays ();
	panelUpdate ();
}

// une contrainte de plus : résolue ; refusée (rien ne bouge) si elle contredit les autres -> vrai si gardée
// un élément réduit à rien (une ligne de longueur nulle, un cercle de rayon nul)
bool skCollapsed (const Sketch &s)
{
	for (auto &g : s.geos) {
		if (g.t == SG_LINE && std::hypot (s.pts[g.b].x - s.pts[g.a].x, s.pts[g.b].y - s.pts[g.a].y) < 1e-3) return true;
		if (g.t != SG_LINE && g.r < 1e-3) return true;
	}
	return false;
}

bool skAddCons (const SkCons &c, bool keepIfConflict = false)
{
	Sketch keep = g_sm.s;
	g_sm.s.cons.push_back (c);
	skSolve (g_sm.s);
	if (skCollapsed (g_sm.s) && !skCollapsed (keep) && !keepIfConflict) {	// (H sur une verticale : PlaneGCS l'écraserait)
		g_sm.s = keep;
		skSolve (g_sm.s);
		g_sm.message = std::string (TR ("Refusée : ")) + TR (consName (c.t)) + TR (" réduirait un élément à rien. Rien n'a bougé.");
		status (g_sm.message.c_str ());
		return false;
	}
	if (!g_sm.s.conflict.empty () && !keepIfConflict) {
		Sketch bad = g_sm.s;
		g_sm.s = keep;
		skSolve (g_sm.s);
		g_sm.lastConflict = bad.conflict;
		std::string m = std::string (TR ("Refusée : ")) + TR (consName (c.t)) + TR (" contredit ");
		for (size_t i = 0; i < bad.conflict.size (); i++)
			if (bad.conflict[i] < (int) keep.cons.size ())
				m += std::string (i ? ", " : "") + TR (consName (keep.cons[bad.conflict[i]].t));
		m += TR (". Rien n'a bougé.");
		g_sm.message = m;
		status (m.c_str ());
		return false;
	}
	g_sm.message.clear ();
	return true;
}

void skSolveNow ()
{
	skSolve (g_sm.s);
}

void skEnter (int editIndex)
{
	g_sm = SkMode ();
	g_sm.on = true;
	g_sm.edit = editIndex;
	g_sm.saved = g_view.cam;
	if (editIndex >= 0) g_sm.s = g_doc.feats[editIndex].sk;
	else {
		Pick k = sel0 ();
		Frame fr;
		if (!k.none () && k.face >= 0 && facePlane (g_doc.bodies[k.body], k.face, fr)) {
			for (int i = 0; i < 3; i++) fr.o[i] = std::round (fr.o[i]);	// (l'origine accrochée au mm, sur le plan)
			g_sm.s.plane = fr;
			g_sm.s.body = k.body;
		}
	}
	skSolve (g_sm.s);
	g_sels.clear ();
	g_selStep = -1;
	g_view.lookAlong (g_sm.s.plane.n);
	double o[3] = { g_sm.s.plane.o[0], g_sm.s.plane.o[1], g_sm.s.plane.o[2] };
	if (editIndex < 0 && g_sm.s.body < 0) g_view.cam.tx = o[0], g_view.cam.ty = o[1], g_view.cam.tz = o[2];
	g_view.dimBodies = true;
	g_sm.tool = SK_LINE;
	skRefresh ();
	status (TR ("Esquisse : L ligne, R rectangle, C cercle, A arc, D cote ; Entrée ou Terminer pour finir."));
}

void skLeave (bool keep)
{
	if (keep && (g_sm.s.geos.empty ())) keep = false;
	if (keep) {
		pushUndo ();
		Feature f;
		if (g_sm.edit >= 0) f = g_doc.feats[g_sm.edit];
		else f.kind = ST_SKETCH, f.name = featureName (g_doc, ST_SKETCH);
		f.sk = g_sm.s;
		if (g_sm.edit >= 0) g_doc.feats[g_sm.edit] = f;
		else insertFeature (g_doc, g_doc.bar, f);
	}
	g_view.cam = g_sm.saved;
	g_sm = SkMode ();
	g_view.dimBodies = false;
	g_view.strokes.clear ();
	g_view.dots.clear ();
	rebuild ();
	refreshAll ();
	status (keep ? TR ("Esquisse gardée. E : l'extruder ; Créer > Révolution autour d'une ligne de construction.") : TR ("Esquisse abandonnée."));
}

// la contrainte demandée sur ce qui est choisi
void skConstrain (SkConsType t)
{
	Sketch &s = g_sm.s;
	std::vector<int> G, Pt;
	for (auto &x : g_sm.sel) (x.kind == 0 ? G : Pt).push_back (x.i);
	auto isLine = [&] (int g) { return s.geos[g].t == SG_LINE; };
	SkCons c;
	c.t = t;
	bool ok = false;
	std::vector<SkCons> many;
	switch (t) {
	case SC_HORIZ: case SC_VERT:
		for (int g : G) if (isLine (g)) { SkCons d = c; d.g1 = g; many.push_back (d); }
		if (G.empty () && Pt.size () == 2) c.p1 = Pt[0], c.p2 = Pt[1], ok = true;
		break;
	case SC_COINCIDENT: if (Pt.size () == 2) c.p1 = Pt[0], c.p2 = Pt[1], ok = true; break;
	case SC_PARALLEL: case SC_PERP: if (G.size () == 2 && isLine (G[0]) && isLine (G[1])) c.g1 = G[0], c.g2 = G[1], ok = true; break;
	case SC_TANGENT: if (G.size () == 2 && !(isLine (G[0]) && isLine (G[1]))) c.g1 = G[0], c.g2 = G[1], ok = true; break;
	case SC_EQUAL: if (G.size () == 2 && isLine (G[0]) == isLine (G[1])) c.g1 = G[0], c.g2 = G[1], ok = true; break;
	case SC_CONCENTRIC: if (G.size () == 2 && !isLine (G[0]) && !isLine (G[1])) c.g1 = G[0], c.g2 = G[1], ok = true; break;
	case SC_POINT_ON: if (G.size () == 1 && Pt.size () == 1) c.g1 = G[0], c.p1 = Pt[0], ok = true; break;
	default: break;
	}
	if (ok) many.push_back (c);
	if (many.empty ()) {
		static const char *const NEED[] = { "une ou plusieurs lignes, ou deux points", "une ou plusieurs lignes, ou deux points", "deux points",
			"deux lignes", "deux lignes", "une ligne et un cercle ou un arc, ou deux cercles", "deux lignes, ou deux cercles", "", "", "un point et un élément", "deux cercles ou arcs" };
		std::string m = std::string (TR (consName (t))) + TR (" : choisissez ") + TR (NEED[t]) + TR (" (Maj+clic pour plusieurs).");
		status (m.c_str ());
		return;
	}
	for (auto &d : many)
		if (!skAddCons (d)) break;
	skRefresh ();
}

// une cote sur un élément ou entre deux points : créée à la valeur du dessin, puis tapée
void skDimension (int geo, int p1, int p2)
{
	Sketch &s = g_sm.s;
	SkCons c;
	if (geo >= 0 && s.geos[geo].t == SG_LINE) c.t = SC_DIST, c.p1 = s.geos[geo].a, c.p2 = s.geos[geo].b, c.v = std::hypot (s.pts[c.p2].x - s.pts[c.p1].x, s.pts[c.p2].y - s.pts[c.p1].y);
	else if (geo >= 0) c.t = SC_RADIUS, c.g1 = geo, c.v = s.geos[geo].r;
	else c.t = SC_DIST, c.p1 = p1, c.p2 = p2, c.v = std::hypot (s.pts[p2].x - s.pts[p1].x, s.pts[p2].y - s.pts[p1].y);
	c.v = std::round (c.v * 100) / 100;
	if (!skAddCons (c)) return;
	g_sm.dimCons = (int) s.cons.size () - 1;
	g_sm.dimText.clear ();
	skRefresh ();
	status (TR ("Tapez la cote, puis Entrée (Échap : garder la valeur du dessin)."));
}

// la valeur tapée d'une cote -> faux si illisible ou refusée
bool skDimCommit ()
{
	int k = g_sm.dimCons;
	g_sm.dimCons = -1;
	if (k < 0 || k >= (int) g_sm.s.cons.size () || g_sm.dimText.empty ()) {
		skRefresh ();
		return true;
	}
	double v;
	std::string t = g_sm.dimText;
	g_sm.dimText.clear ();
	if (!evalExpr (t.c_str (), v) || v <= 0) {
		status ((std::string (TR ("Valeur illisible : ")) + t).c_str ());
		skRefresh ();
		return false;
	}
	SkCons &c = g_sm.s.cons[k];
	if (c.t == SC_RADIUS && g_sm.s.geos[c.g1].t == SG_CIRCLE) v /= 2;	// (un cercle se cote par son diamètre)
	Sketch keep = g_sm.s;
	keep.cons[k].v = c.v;
	c.v = v;
	skSolve (g_sm.s);
	if (!g_sm.s.conflict.empty () || g_sm.s.failed) {
		g_sm.s = keep;
		skSolve (g_sm.s);
		status (TR ("Cote refusée : elle contredit les autres contraintes. Rien n'a bougé."));
	}
	skRefresh ();
	return true;
}

int g_skDrag = -1;			// le point qu'on glisse

void skMouse (int kind, int x, int y, int buttons, bool shift)
{
	Sketch &s = g_sm.s;
	if (kind == 0) {
		if (g_skDrag >= 0 && (buttons & 1)) {	// glisser un point : ce qui est encore libre suit
			skCursor (x, y, false);
			Sketch keep = s;
			s.pts[g_skDrag].x = g_sm.mx, s.pts[g_skDrag].y = g_sm.my;
			skSolve (s);
			if (s.failed) s = keep;
			renderView ();
			paintOverlays ();
			return;
		}
		skCursor (x, y);
		int hg = g_sm.tool == SK_NONE || g_sm.tool == SK_DIM ? skGeoAt (x, y) : -1;
		if (g_sm.snapPt >= 0) hg = -1;
		g_sm.hoverGeo = hg;
		renderView ();
		if (g_sm.tool != SK_NONE && !g_sm.clicks.empty ()) {	// la longueur, le rayon en cours
			const SkPoint &a = s.pts[g_sm.clicks.back ()];
			char m[96];
			if (g_sm.tool == SK_RECT) snprintf (m, sizeof m, "%s × %s", fmtNum (std::fabs (g_sm.mx - a.x)).c_str (), fmtNum (std::fabs (g_sm.my - a.y)).c_str ());
			else snprintf (m, sizeof m, "%s %s", g_sm.tool == SK_LINE ? "" : "R", fmtNum (std::round (std::hypot (g_sm.mx - a.x, g_sm.my - a.y) * 10) / 10).c_str ());
			status (m);
		}
		return;
	}
	if (kind == 1) {
		skCursor (x, y);
		g_skDrag = g_sm.tool == SK_NONE && g_sm.snapPt > 0 ? g_sm.snapPt : -1;	// (l'origine ne se glisse pas)
		return;
	}
	if (kind != 2) return;
	bool wasDrag = g_skDrag >= 0;
	g_skDrag = -1;
	if (!(buttons & 1)) return;
	// une cote : un clic l'ouvre pour la changer
	for (auto &l : g_skLabels)
		if (l.dim && x >= l.x && x < l.x + l.w && y >= l.y && y < l.y + 20) {
			g_sm.dimCons = l.cons;
			g_sm.dimText.clear ();
			skRefresh ();
			status (TR ("Tapez la nouvelle cote, puis Entrée."));
			return;
		}
	if (wasDrag) {
		skRefresh ();
		return;
	}
	skCursor (x, y);
	switch (g_sm.tool) {
	case SK_NONE: {	// choisir : un point, sinon un élément (Maj : plusieurs)
		SkSel k { -1, -1 };
		if (g_sm.snapPt >= 0) k = { 1, g_sm.snapPt };
		else {
			int g = skGeoAt (x, y);
			if (g >= 0) k = { 0, g };
		}
		if (!shift) g_sm.sel.clear ();
		if (k.kind >= 0) {
			bool had = false;
			for (size_t i = 0; i < g_sm.sel.size (); i++)
				if (g_sm.sel[i].kind == k.kind && g_sm.sel[i].i == k.i) g_sm.sel.erase (g_sm.sel.begin () + i), had = true;
			if (!had) g_sm.sel.push_back (k);
		}
		skRefresh ();
		return;
	}
	case SK_LINE: {
		int first = g_sm.chainStart;
		int p = skPointHere ();
		if (!g_sm.clicks.empty ()) {
			int a = g_sm.clicks.back ();
			if (a == p) return;
			int g = skAddLine (s, a, p);
			double dx = s.pts[p].x - s.pts[a].x, dy = s.pts[p].y - s.pts[a].y;
			if (!shift) {	// contraintes déduites
				SkCons c;
				c.g1 = g;
				if (std::fabs (dy) <= std::fabs (dx) * 0.07) c.t = SC_HORIZ, skAddCons (c);
				else if (std::fabs (dx) <= std::fabs (dy) * 0.07) c.t = SC_VERT, skAddCons (c);
			}
			skSolve (s);
			if (p == first) {	// revenu au premier point : le contour est fermé
				g_sm.clicks.clear ();
				g_sm.chainStart = -1;
				skRefresh ();
				return;
			}
		} else
			g_sm.chainStart = p;
		g_sm.clicks = { p };
		skRefresh ();
		return;
	}
	case SK_RECT: {
		if (g_sm.clicks.empty ()) {
			g_sm.clicks = { skPointHere () };
			skRefresh ();
			return;
		}
		int a = g_sm.clicks[0];
		double x0 = s.pts[a].x, y0 = s.pts[a].y, x1 = g_sm.mx, y1 = g_sm.my;
		if (std::fabs (x1 - x0) < 0.5 || std::fabs (y1 - y0) < 0.5) return;
		int b = skAddPoint (s, x1, y0), c = g_sm.snapPt >= 0 ? g_sm.snapPt : skAddPoint (s, x1, y1), d = skAddPoint (s, x0, y1);
		int l0 = skAddLine (s, a, b), l1 = skAddLine (s, b, c), l2 = skAddLine (s, c, d), l3 = skAddLine (s, d, a);
		for (int g : { l0, l2 }) { SkCons k; k.t = SC_HORIZ, k.g1 = g; s.cons.push_back (k); }
		for (int g : { l1, l3 }) { SkCons k; k.t = SC_VERT, k.g1 = g; s.cons.push_back (k); }
		skSolve (s);
		g_sm.clicks.clear ();
		skRefresh ();
		return;
	}
	case SK_CIRCLE: {
		if (g_sm.clicks.empty ()) {
			g_sm.clicks = { skPointHere () };
			skRefresh ();
			return;
		}
		int c = g_sm.clicks[0];
		double r = std::hypot (g_sm.mx - s.pts[c].x, g_sm.my - s.pts[c].y);
		if (r < 0.5) return;
		skAddCircle (s, c, std::round (r * 2) / 2);
		skSolve (s);
		g_sm.clicks.clear ();
		skRefresh ();
		return;
	}
	case SK_ARC: {
		int p = skPointHere ();
		g_sm.clicks.push_back (p);
		if (g_sm.clicks.size () == 3) {
			skAddArc (s, g_sm.clicks[0], g_sm.clicks[1], g_sm.clicks[2]);
			skSolve (s);
			g_sm.clicks.clear ();
		}
		skRefresh ();
		return;
	}
	case SK_DIM: {
		if (g_sm.snapPt >= 0) {	// deux points : leur distance
			if (g_sm.clicks.empty ()) {
				g_sm.clicks = { g_sm.snapPt };
				status (TR ("Cote : le second point."));
				skRefresh ();
				return;
			}
			int a = g_sm.clicks[0];
			g_sm.clicks.clear ();
			if (a != g_sm.snapPt) skDimension (-1, a, g_sm.snapPt);
			return;
		}
		int g = skGeoAt (x, y);
		if (g >= 0) skDimension (g, -1, -1);
		return;
	}
	}
}

void skSetTool (SkTool t)
{
	if (g_sm.dimCons >= 0) skDimCommit ();
	g_sm.tool = t;
	g_sm.clicks.clear ();
	g_sm.chainStart = -1;
	g_sm.sel.clear ();
	skRefresh ();
	if (t != SK_NONE) status ((std::string (TR (skToolName (t))) + TR (" : clic pour poser ; Échap pour finir l'outil.")).c_str ());
}

void skDeleteSel ()
{
	std::vector<int> G;
	for (auto &x : g_sm.sel) if (x.kind == 0) G.push_back (x.i);
	std::sort (G.rbegin (), G.rend ());
	for (int g : G) skRemoveGeo (g_sm.s, g);
	g_sm.sel.clear ();
	skSolve (g_sm.s);
	skRefresh ();
}

void skToggleConstruction ()
{
	for (auto &x : g_sm.sel)
		if (x.kind == 0) g_sm.s.geos[x.i].cons = !g_sm.s.geos[x.i].cons;
	skRefresh ();
}

// le clavier en esquisse -> vrai s'il est pris
bool skKey (int code, int ch, int mods)
{
	if (!g_sm.on || (mods & (UI_MOD_CTRL | UI_MOD_ALT))) return false;
	if (g_sm.dimCons >= 0) {	// la cote ouverte prend les chiffres
		if (code == 0x28 || code == 0x58) return skDimCommit (), true;
		if (code == 0x29) return g_sm.dimCons = -1, g_sm.dimText.clear (), skRefresh (), true;
		if (code == 0x2A) {
			if (!g_sm.dimText.empty ()) g_sm.dimText.pop_back ();
			paintOverlays ();
			return true;
		}
		if (ch > 0 && ch < 128 && (isdigit (ch) || strchr (".,+-*/() cmin", ch))) {
			g_sm.dimText += (char) ch;
			paintOverlays ();
			return true;
		}
	}
	if (code == 0x29) {	// Échap : l'outil, puis le choix
		if (!g_sm.clicks.empty ()) g_sm.clicks.clear (), g_sm.chainStart = -1, skRefresh ();
		else if (g_sm.tool != SK_NONE) skSetTool (SK_NONE);
		else g_sm.sel.clear (), skRefresh ();
		return true;
	}
	if (code == 0x28 || code == 0x58) return skLeave (true), true;	// Entrée : terminer
	if (code == 0x4C || code == 0x2A) return skDeleteSel (), true;	// Suppr
	switch (ch) {
	case 'l': case 'L': skSetTool (SK_LINE); return true;
	case 'r': case 'R': skSetTool (SK_RECT); return true;
	case 'c': case 'C': skSetTool (SK_CIRCLE); return true;
	case 'a': case 'A': skSetTool (SK_ARC); return true;
	case 'd': case 'D': skSetTool (SK_DIM); return true;
	case 'h': case 'H': skConstrain (SC_HORIZ); return true;
	case 'v': case 'V': skConstrain (SC_VERT); return true;
	case '=': skConstrain (SC_EQUAL); return true;
	case 'x': case 'X': skToggleConstruction (); return true;
	}
	return false;
}

// le panneau de l'esquisse
void skPanel ()
{
	Sketch &s = g_sm.s;
	std::string title = g_sm.edit >= 0 ? g_doc.feats[g_sm.edit].name : featureName (g_doc, ST_SKETCH);
	ui_w_set_text (p_title, (long) title.c_str ());
	const double *n = s.plane.n;
	std::string pl = std::fabs (n[2]) > 0.999 && s.body < 0 ? "plan XY" : s.body >= 0 ? "sur une face" : "plan";
	std::string state;
	if (!g_sm.message.empty ()) state = "⚠ " + g_sm.message;
	else if (s.geos.empty ()) state = TR ("Dessinez : L ligne, R rectangle, C cercle, A arc.");
	else if (s.dof == 0) state = TR ("✓ Complètement contrainte.");
	else if (s.dof > 0) state = std::to_string (s.dof) + (s.dof > 1 ? TR (" degrés de liberté : ce qui est en bleu peut encore bouger (glissez un point pour voir).") : TR (" degré de liberté : ce qui est en bleu peut encore bouger."));
	else state = TR ("Non résolue.");
	std::string sub = pl + " · " + std::to_string (s.geos.size ()) + TR (" éléments · ") + state;
	ui_w_set_text (p_sub, (long) sub.c_str ());
	std::string sec = std::string (TR ("Contraintes")) + " (" + std::to_string (s.cons.size ()) + ")";
	ui_w_set_text (p_sec, (long) sec.c_str ());
	std::vector<std::pair<std::string, std::string>> rows;
	int from = imax (0, (int) s.cons.size () - 6);
	for (int k = from; k < (int) s.cons.size (); k++) {
		const SkCons &c = s.cons[k];
		std::string v = c.t == SC_DIST || c.t == SC_DIST_X || c.t == SC_DIST_Y ? fmtNum (std::fabs (c.v)) + " mm"
			: c.t == SC_RADIUS ? (s.geos[c.g1].t == SG_CIRCLE ? "Ø " + fmtNum (2 * c.v) : "R " + fmtNum (c.v)) : "";
		rows.push_back ({ TR (consName (c.t)), v });
	}
	panelRows (rows);
	ui_w_set_text (p_state, (long) "");
	ui_w_show (w_sktools, true);
	ui_w_set_value (w_sktools, (long) g_sm.tool);
	for (int i = 0; i < SKB_N; i++) ui_w_show (skb[i], true);
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
			g_view.orbit (-dx * 0.25, dy * 0.25);
			g_camDirty = true;
			return;
		}
		if (buttons & 4) {	// milieu : déplacer
			g_view.pan (dx, dy);
			g_camDirty = true;
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
	if (g_sm.on && !overUi && kind != 3) {
		skMouse (kind, x, y, kind == 2 ? g_btn : buttons, shift);
		return;
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
		static double lastMs;
		static Pick lastK;
		double now = nowMs ();
		bool dbl = k.edge >= 0 && k == lastK && now - lastMs < 450;
		lastMs = now, lastK = k;
		if (dbl) {	// double-clic : la chaîne tangente
			std::vector<Pick> ch;
			for (int e : tangentChain (g_doc.bodies[k.body], k.edge)) ch.push_back (Pick { k.body, -1, e });
			select (ch);
			std::string m = std::to_string (ch.size ()) + TR (" arêtes tangentes choisies ; F : un congé sur toute la chaîne.");
			status (m.c_str ());
			return;
		}
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
		std::string t = (const char *) ui_w_get_text (e_val[i]);
		if (!evalExpr (g_doc, t.c_str (), v)) {
			std::string m = std::string (TR ("Valeur illisible ou paramètre inconnu : ")) + t;
			status (m.c_str ());
			return;
		}
		featSet (f, i, v);
		// un nombre seul : une valeur ; sinon une formule gardée, rejouée avec les paramètres
		bool plain = evalExpr (t.c_str (), v) && t.find_first_of ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_") == std::string::npos;
		for (size_t k = 0; k < f.expr.size (); k++)
			if (f.expr[k].first == i) f.expr.erase (f.expr.begin () + k--);
		if (!plain) f.expr.push_back ({ i, t });
	}
	pushUndo ();
	g_doc.feats[g_selStep] = f;
	rebuild ();
	refreshAll ();
	const Feature &r = g_doc.feats[g_selStep];
	std::string m = r.error.empty () ? r.name + " : " + featureDetail (r) : r.name + TR (" en erreur : ") + r.error;
	status (m.c_str ());
}

void paramSet (const char *text)
{
	std::string t = text ? text : "";
	size_t eq = t.find ('=');
	auto trim = [] (std::string x) {
		while (!x.empty () && x.front () == ' ') x.erase (0, 1);
		while (!x.empty () && x.back () == ' ') x.pop_back ();
		return x;
	};
	if (eq == std::string::npos) {
		status (TR ("Paramètre : « nom = formule », par exemple « épaisseur = 4 »."));
		return;
	}
	std::string name = trim (t.substr (0, eq)), ex = trim (t.substr (eq + 1));
	if (name.empty () || isdigit ((unsigned char) name[0]) || name.find_first_of (" +-*/(),.") != std::string::npos) {
		status (TR ("Un nom de paramètre : des lettres, des chiffres, _ (pas d'espace)."));
		return;
	}
	Doc probe = g_doc;
	size_t at = probe.params.size ();
	for (size_t i = 0; i < probe.params.size (); i++) if (probe.params[i].first == name) at = i;
	if (ex.empty ()) {	// « nom = » : retiré
		if (at < probe.params.size ()) probe.params.erase (probe.params.begin () + at);
	} else {
		if (at == probe.params.size ()) probe.params.push_back ({ name, ex });
		else probe.params[at].second = ex;
		double v;
		if (!evalExpr (probe, name.c_str (), v)) {
			status ((std::string (TR ("Formule illisible ou paramètre inconnu : ")) + ex).c_str ());
			return;
		}
	}
	pushUndo ();
	g_doc.params = probe.params;
	ui_w_set_text (e_param, (long) "");
	rebuild ();
	refreshAll ();
	status ((name + (ex.empty () ? TR (" retiré.") : " = " + ex)).c_str ());
}

void stepDelete ()
{
	if (g_selStep < 0 || g_selStep >= (int) g_doc.feats.size ()) return;
	pushUndo ();
	eraseFeature (g_doc, g_selStep);
	g_selStep = -1;
	rebuild ();
	refreshAll ();
}

void undo (bool redo)
{
	auto &from = redo ? g_redo : g_undo, &to = redo ? g_undo : g_redo;
	if (from.empty ()) return;
	to.push_back ({ g_doc.feats, g_doc.bar, g_doc.params });
	g_doc.feats = from.back ().feats;
	g_doc.bar = from.back ().bar;
	g_doc.params = from.back ().params;
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
	g_prevZ.clear (), g_prevMarks.clear ();
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
	ui_w_move (w_pmode, px, by - h - 8, pw, h);
	ui_w_move (w_paxis, px, by - 2 * h - 16, pw, h);
	ui_w_move (b_usemax, px, by - h - 8, pw, h);
	ui_w_move (e_param, px, by + h + 12, pw - 96, h);
	ui_w_move (b_param, px + pw - 88, by + h + 12, 88, h);
	ui_w_move (b_ok, px + pw - 2 * 104 - 8, by + h + 12, 104, h);
	ui_w_move (b_cancel, px + pw - 104, by + h + 12, 104, h);
	ui_w_move (b_apply, px, by, (pw - 8) / 2, h);
	ui_w_move (b_off, px + (pw - 8) / 2 + 8, by, pw - (pw - 8) / 2 - 8, h);
	ui_w_move (b_barhere, px, by + h + 8, (pw - 8) / 2, h);
	ui_w_move (b_del, px + (pw - 8) / 2 + 8, by + h + 8, pw - (pw - 8) / 2 - 8, h);
	ui_w_move (p_state, px, by + 2 * h + 24, pw, 60);
	// l'esquisse : sous la liste des contraintes, les outils, les boutons des contraintes, les actions
	int sy = y + 92 + 6 * 32 + 8, cw3 = (pw - 16) / 3;
	ui_w_move (w_sktools, px, sy, pw, h);
	for (int i = 0; i <= SKB_ON; i++) ui_w_move (skb[i], px + (i % 3) * (cw3 + 8), sy + h + 10 + (i / 3) * (h + 6), cw3, h);
	int ay = sy + h + 10 + 3 * (h + 6) + 8, cw4 = (pw - 8) / 2;
	ui_w_move (skb[SKB_CONS], px, ay, cw4, h);
	ui_w_move (skb[SKB_DEL], px + cw4 + 8, ay, pw - cw4 - 8, h);
	ui_w_move (skb[SKB_CANCEL], px, ay + h + 6, cw4, h);
	ui_w_move (skb[SKB_DONE], px + cw4 + 8, ay + h + 6, pw - cw4 - 8, h);
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
	"MFichier\n" "I13c\tPiloter par un agent (MCP)…\n" "-\n" "I9\tQuitter\tCtrl+Q\n"
	"MÉdition\n" "I10\tAnnuler\tCtrl+Z\n" "I11\tRétablir\tCtrl+Y\n" "-\n" "I12\tSupprimer l'étape\n"
	"MAffichage\n" "I20\tVue de face\tAlt+1\n" "I21\tVue de dessus\tAlt+2\n" "I22\tVue de droite\tAlt+3\n"
	"I23\tVue isométrique\tAlt+0\n" "-\n" "I24\tAjuster\tF6\n" "-\n" "I25c\tArêtes\n" "I26c\tGrille\n" "-\n" "I45c\tAccrochage 1 mm\n" "I46c\tAccrochage 5 mm\n" "I47c\tAccrochage 10 mm\n"
	"MCréer\n" "I37\tEsquisse (S)\n" "I38\tExtrusion (E)\n" "I39\tRévolution\n" "I41\tPerçage (H)\n" "-\n" "I30\tBoîte (B)\n" "I31\tCylindre\n" "-\n" "I32\tPousser/tirer (Q)\n"
	"MModifier\n" "I33\tCongé (F ; double-clic : la chaîne)\n" "I34\tChanfrein (Maj+F)\n" "-\n" "I35\tUnion\n" "I36\tSoustraction\n" "-\n" "I42\tCoque\n" "I43\tRéseau\n" "I44\tSymétrie\n"
	"MAide\n" "I40\tNaviguer dans la vue\n";

void setSnap (double v)
{
	g_snap = v;
	for (int i = 0; i < 3; i++) ui_menu_state (45 + i, (i == 0 ? 1 : i == 1 ? 5 : 10) == v ? UMS_CHECKED : 0);
	renderView ();
	status ((std::string (TR ("Accrochage : ")) + fmtNum (v) + " mm").c_str ());
}

void mcpMenu ();

void command (int id)
{
	switch (id) {
	case 9: g_quit = true; break;
	case 13: mcpMenu (); break;
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
	case 37: if (!g_sm.on) skEnter (-1); break;
	case 38: cmdStart (T_EXTRUDE); break;
	case 39: cmdStart (T_REVOLVE); break;
	case 41: cmdStart (T_HOLE); break;
	case 42: cmdStart (T_SHELL); break;
	case 43: cmdStart (T_PATTERN); break;
	case 44: cmdStart (T_MIRROR); break;
	case 40: hintIdle (); break;
	case 45: case 46: case 47: setSnap (id == 45 ? 1 : id == 46 ? 5 : 10); break;
	}
}

void control (long w, long code)
{
	if (code == UN_ACTIVATE)
		for (int i = 0; i < SKB_N; i++)
			if (w == skb[i]) {
				static const SkConsType CT[] = { SC_HORIZ, SC_VERT, SC_COINCIDENT, SC_PARALLEL, SC_PERP, SC_TANGENT, SC_EQUAL, SC_CONCENTRIC, SC_POINT_ON };
				if (i <= SKB_ON) skConstrain (CT[i]);
				else if (i == SKB_CONS) skToggleConstruction ();
				else if (i == SKB_DEL) skDeleteSel ();
				else if (i == SKB_CANCEL) skLeave (false);
				else skLeave (true);
				return;
			}
	if (code == UN_ACTIVATE || code == UN_CHANGE) {
		static const int MAP[TB_N] = { 37, 30, 31, 32, 33, 34, 35, 36, -1 };
		for (int i = 0; i < TB_N; i++)
			if (w == tb[i] && MAP[i] >= 0) {
				if (g_sm.on) {
					if (i == TB_SKETCH) skLeave (true);
				} else if (g_cmd.tool != T_NONE) cmdEnd ();
				else command (MAP[i]);
				if (ui_w_get_value (tb[i]) && g_cmd.tool == T_NONE) ui_w_set_value (tb[i], 0);
				return;
			}
	}
	if (w == w_pmode && code == UN_CHANGE) {
		g_cmd.f.mode = (int) ui_w_get_value (w_pmode);
		if (g_cmd.chips.size () > 1) {
			g_cmd.chips[1].label = g_cmd.f.mode ? "Angle" : "Pas";
			if (!g_cmd.chips[1].locked) g_cmd.chips[1].val = g_cmd.f.mode ? 360 : 10;
			if (g_cmd.f.mode && !g_cmd.chips[0].locked) g_cmd.chips[0].val = 4;
			if (g_cmd.f.mode) g_cmd.f.frame.n[0] = 0, g_cmd.f.frame.n[1] = 0, g_cmd.f.frame.n[2] = 1;	// (circulaire : autour de Z)
		}
		cmdRefresh ();
		return;
	}
	if (w == w_paxis && code == UN_CHANGE) {
		long a = ui_w_get_value (w_paxis);
		for (int i = 0; i < 3; i++) g_cmd.f.frame.n[i] = i == a;
		cmdRefresh ();
		return;
	}
	if (w == b_usemax && code == UN_ACTIVATE && g_cmd.maxR > 0 && !g_cmd.chips.empty ()) {
		g_cmd.chips[0].val = g_cmd.maxR, g_cmd.chips[0].locked = true, g_cmd.chips[0].text.clear ();
		cmdRefresh ();
		return;
	}
	if (w == b_param && code == UN_ACTIVATE) {
		paramSet ((const char *) ui_w_get_text (e_param));
		return;
	}
	if (w == w_op && code == UN_CHANGE) {
		long v = ui_w_get_value (w_op);
		g_cmd.f.op = v == 0 ? OP_NEW : v == 1 ? OP_JOIN : v == 2 ? OP_CUT : OP_INTERSECT;
		if (g_cmd.tool == T_MIRROR && g_cmd.f.op != OP_NEW) g_cmd.f.op = OP_JOIN;	// (symétrie : jointe ou nouveau corps)
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
		if (g_selStep >= 0 && g_doc.feats[g_selStep].kind == ST_SKETCH) skEnter (g_selStep);
		else stepApply ();
	} else if (w == w_sktools && code == UN_CHANGE) {
		skSetTool ((SkTool) ui_w_get_value (w_sktools));
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
	w_bar = ui_toolbar (408, (TB_H - h) / 2, imax (100, (int) ui.w - 408 - 96 - 128), 0);	// (à sa place ; ui_w_move de la barre emmène ses boutons)
	static const char *const TOOLS[TB_N] = { "Esquisse", "Boîte", "Cylindre", "Pousser", "|Congé", "Chanfrein", "|Union", "Soustraire", "|Mesurer" };
	static const char *const TIPS[TB_N] = { "Esquisse (S) sur la face choisie ou le plan XY : lignes, rectangles, cercles, arcs, contraintes (PlaneGCS) ; E l'extrude", "Boîte (B) : clic au centre, on s'éloigne, clic, on monte, clic",
		"Cylindre : clic au centre, on s'éloigne pour le rayon, clic, on monte, clic", "Pousser/tirer (Q) une face plane", "Congé (F) sur les arêtes choisies",
		"Chanfrein (Maj+F) sur les arêtes choisies", "Union : deux corps n'en font plus qu'un", "Soustraction : un corps retiré d'un autre", "Mesurer : à une étape suivante" };
	for (int i = 0; i < TB_N; i++) {
		const char *t = TOOLS[i];
		bool sep = t[0] == '|';
		tb[i] = ui_toolbar_add (w_bar, 0, (long) TR (sep ? t + 1 : t), (sep ? UTB_SEP : 0) | UTB_TOGGLE);
		ui_w_tip (tb[i], (long) TR (TIPS[i]));
		if (i == TB_MEASURE) ui_w_enable (tb[i], 0);
	}
	w_export = ui_pushbutton (0, 0, 108, h, (long) TR ("Exporter"), UB_DEFAULT);
	ui_w_enable (w_export, 0);
	ui_w_tip (w_export, (long) TR ("STEP, IGES, STL, 3MF : à l'étape 5"));
	w_tl = ui_view (0, 0, 100, 56, 0);
	ui_view_on (w_tl, (long) timelinePaint, (long) timelineMouse, 0, 0);
	p_title = ui_label (0, 0, 10, 26, (long) "", ULB_TITLE);
	p_sub = ui_label (0, 0, 10, 36, (long) "", ULB_DIM | ULB_WRAP);	// (en esquisse : l'état, sur deux lignes)
	p_sec = ui_label (0, 0, 10, 20, (long) "", ULB_BOLD);
	for (int i = 0; i < 8; i++) {
		p_name[i] = ui_label (0, 0, 10, 22, (long) "", ULB_DIM);
		p_val[i] = ui_label (0, 0, 10, 22, (long) "", ULB_RIGHT);
	}
	for (int i = 0; i < 3; i++) e_val[i] = ui_textbox (0, 0, 10, h, 0);
	w_op = ui_segmented (0, 0, 10, h, (long) TR ("Nouveau|Joindre|Enlever|Inter."));
	w_pmode = ui_segmented (0, 0, 10, h, (long) TR ("Linéaire|Circulaire"));
	w_paxis = ui_segmented (0, 0, 10, h, (long) TR ("Axe X|Axe Y|Axe Z"));
	b_usemax = ui_pushbutton (0, 0, 10, h, (long) "", 0);
	e_param = ui_textbox (0, 0, 10, h, 0);
	ui_w_tip (e_param, (long) TR ("nom = formule (« épaisseur = 4 », « largeur = épaisseur * 10 ») ; « nom = » le retire"));
	b_param = ui_pushbutton (0, 0, 10, h, (long) TR ("Définir"), 0);
	b_ok = ui_pushbutton (0, 0, 104, h, (long) TR ("OK"), UB_DEFAULT);
	b_cancel = ui_pushbutton (0, 0, 104, h, (long) TR ("Annuler"), 0);
	b_apply = ui_pushbutton (0, 0, 10, h, (long) TR ("Appliquer"), UB_DEFAULT);
	b_off = ui_pushbutton (0, 0, 10, h, (long) TR ("Désactiver"), 0);
	b_barhere = ui_pushbutton (0, 0, 10, h, (long) TR ("Barre ici"), 0);
	b_del = ui_pushbutton (0, 0, 10, h, (long) TR ("Supprimer"), 0);
	p_state = ui_label (0, 0, 10, 60, (long) "", ULB_WRAP);
	w_sktools = ui_segmented (0, 0, 10, h, (long) TR ("Choisir|Ligne|Rect.|Cercle|Arc|Cote"));
	static const char *const SKN[SKB_N] = { "Horiz. (H)", "Vert. (V)", "Coïncid.", "Parallèle", "Perpend.", "Tangente", "Égal (=)", "Concentr.", "Sur",
		"Constr. (X)", "Supprimer", "Annuler", "Terminer" };
	for (int i = 0; i < SKB_N; i++) skb[i] = ui_pushbutton (0, 0, 10, h, (long) TR (SKN[i]), i == SKB_DONE ? UB_DEFAULT : 0);
	p_hint = ui_label (0, 0, 10, 96, (long) TR ("Clic : choisir ; Maj+clic : plusieurs arêtes. Une étape de la frise : ses valeurs ; la barre bleue se glisse. Ctrl+Z : annuler."), ULB_WRAP | ULB_DIM);
	w_status = ui_label (0, 0, 10, 20, (long) "", ULB_DIM);
	w_stats = ui_label (0, 0, 10, 20, (long) "", ULB_DIM | ULB_RIGHT);
	ui_menu ((long) MENUS);
	ui_menu_state (25, UMS_CHECKED);
	ui_menu_state (26, UMS_CHECKED);
	ui_menu_state (46, UMS_CHECKED);	// (accrochage 5 mm)
	showCmdWidgets (false);
	showEditWidgets (0, false);
}

// ---- le pilotage par un agent (MCP) ------------------------------------------------------------------------------

#include "mcptools.inc"

const int MCP_PORT = 7311;
const char *const MCP_CONF = "HOME:/forge/mcp.txt";

// Le jeton gardé. Avec un coffre à secrets (le PC, docs/KEYVAULT.md) : son secret « mcp-token », sous l'identité de
// Forge ; un HOME:/forge/mcp.txt d'avant y passe la première fois, puis le fichier est effacé ; coffre refusé (la
// carte annulée) : un jeton pour cette session seulement. Sans coffre (le Pi) : HOME:/forge/mcp.txt (« token = ... »),
// comme avant. Absent : un nouveau, 32 octets de getentropy en hexadécimal.
static bool mcpInVault ()
{
	static int st = kv_status () >= 0 ? 1 : 0;
	return st;
}

static std::string mcpFileToken ()
{
	char line[256];
	FILE *f = fopen (MCP_CONF, "r");
	std::string tok;
	if (f) {
		while (fgets (line, sizeof line, f)) {
			std::string l = line;
			while (!l.empty () && (l.back () == '\n' || l.back () == '\r' || l.back () == ' ')) l.pop_back ();
			if (l.rfind ("token = ", 0) == 0) tok = l.substr (8);
			std::fill (l.begin (), l.end (), 0);
		}
		fclose (f);
		memset (line, 0, sizeof line);
	}
	return tok;
}

static std::string mcpNewToken ()
{
	unsigned char r[32];
	std::string tok;
	if (getentropy (r, sizeof r) == 0) {
		static const char H[] = "0123456789abcdef";
		for (unsigned i = 0; i < sizeof r; i++) tok += H[r[i] >> 4], tok += H[r[i] & 15];
		memset (r, 0, sizeof r);
		return tok;
	}
	// (getentropy en échec : l'ancien mélange, plutôt qu'aucun jeton)
	static const char A[] = "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
	unsigned long long x = (unsigned long long) (nowMs () * 1000) ^ ((unsigned long long) time (nullptr) << 20) ^ (unsigned long long) (size_t) &tok;
	for (int i = 0; i < 24; i++) {
		x ^= x << 13, x ^= x >> 7, x ^= x << 17;
		tok += A[x % (sizeof A - 1)];
	}
	return tok;
}

std::string mcpToken ()
{
	std::string tok;
	if (mcpInVault ()) {
		char b[256];
		long n = kv_get ("mcp-token", b, sizeof b - 1);
		if (n > 0) {
			tok.assign (b, (size_t) n);
			memset (b, 0, sizeof b);
			if (access (MCP_CONF, F_OK) == 0) unlink (MCP_CONF);	// (un fichier resté d'avant)
			return tok;
		}
		static std::string session;		// (refusé : pour cette session)
		if (n < 0 && n != -2) {
			if (session.empty ()) session = mcpNewToken ();
			return session;
		}
		tok = mcpFileToken ();			// (la migration d'un mcp.txt d'avant)
		if (tok.empty ()) tok = mcpNewToken ();
		if (kv_set ("mcp-token", tok.data (), (long) tok.size ()) >= 0) {
			unlink (MCP_CONF);
			return tok;
		}
		// (le coffre n'a pas voulu le garder : dans le fichier, comme avant)
	} else {
		tok = mcpFileToken ();
		if (!tok.empty ()) return tok;
		tok = mcpNewToken ();
	}
	mkdir ("HOME:", 0755);
	mkdir ("HOME:/forge", 0755);
	FILE *f = fopen (MCP_CONF, "w");
	if (f) {
		fprintf (f, "# Forge : le jeton du pilotage par un agent (MCP). Le changer ou effacer ce fichier invalide l'ancien.\ntoken = %s\n", tok.c_str ());
		fclose (f);
	}
	return tok;
}

std::string myAddress ()
{
	static bool open = netkit_kit_open () != 0;
	unsigned char ni[32] = { 0 };
	if (open) net_info ((long) ni);
	uint32_t ip;	// (un entier : le premier nombre de l'adresse en poids fort, comme l'applet Wi-Fi la lit)
	memcpy (&ip, ni, 4);
	char s[32];
	snprintf (s, sizeof s, "%u.%u.%u.%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255);
	return ip ? s : "ADRESSE-DE-CE-PC";
}

bool mcpStart (int port, const std::string &token, bool quiet)
{
	std::string err;
	if (!mcp::start (port, token, err)) {
		std::string m = std::string (TR ("Le pilotage n'a pas pu démarrer : ")) + err;
		if (quiet) fprintf (stderr, "forge : %s\n", m.c_str ());
		else ui_message ((long) "Forge", (long) m.c_str (), UM_OK);
		return false;
	}
	ui_menu_state (13, UMS_CHECKED);
	if (quiet) fprintf (stderr, "forge : MCP sur http://%s:%d/mcp%s\n", myAddress ().c_str (), port, token.empty () ? " (sans jeton)" : "");
	renderView ();
	return true;
}

void mcpMenu ()
{
	if (mcp::running ()) {
		std::string m = std::string (TR ("Un agent peut piloter Forge (")) + std::to_string (mcp::served ()) + TR (" messages reçus).\n\nArrêter le pilotage ?");
		if (ui_message ((long) "Forge", (long) m.c_str (), UM_YESNO) == 1) {
			mcp::stop ();
			ui_menu_state (13, 0);
			renderView ();
			status (TR ("Pilotage par un agent arrêté."));
		}
		return;
	}
	std::string tok = mcpToken ();
	if (!mcpStart (MCP_PORT, tok, false)) return;
	std::string url = "http://" + myAddress () + ":" + std::to_string (MCP_PORT) + "/mcp";
	std::string cmd = "claude mcp add --transport http forge " + url + " --header \"Authorization: Bearer " + tok + "\"";
	ui_clip_set ((long) cmd.c_str (), -1);
	std::string m = std::string (TR ("Forge attend un agent sur ")) + url + TR (".\n\nPour Claude Code, sur la machine où il tourne :\n\n") + cmd
		+ TR ("\n\nLa commande est dans le presse-papiers. Le jeton est gardé dans ")
		+ (mcpInVault () ? TR ("le coffre à secrets de Korin") : MCP_CONF)
		+ TR (" : chaque changement fait par l'agent est une étape de l'historique, qu'Annuler défait.");
	ui_message ((long) TR ("Piloter Forge par un agent"), (long) m.c_str (), UM_OK);
	status (TR ("Pilotage par un agent (MCP) : en attente."));
}

// ---- les événements ---------------------------------------------------------------------------------------------

void loop (double untilMs)
{
	while (!g_quit && (!untilMs || nowMs () < untilMs)) {
		long t = ui_event (g_camDirty || g_cmdDirty ? 0 : 50);
		if (mcp::running () && mcp::poll (mcpHandle)) continue;	// (un message d'un agent : traité entre deux événements)
		if (t <= 0 && (g_camDirty || g_cmdDirty)) {	// (la file vide : l'image, une seule pour tous les mouvements reçus)
			if (g_cmdDirty) {
				g_cmdDirty = false;
				if (g_cmd.tool != T_NONE) cmdRefresh ();	// (l'image avec)
			}
			if (g_camDirty) cameraChanged ();
			continue;
		}
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) {
			command ((int) ui_ev_field (&ui, EV_CODE));
			continue;
		}
		if (t == WEV_THEME) {
			g_prevZ.clear (), g_prevMarks.clear ();
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
		if (t == WEV_KEY && g_cmd.tool != T_NONE && !g_sm.on && !(mods & (UI_MOD_CTRL | UI_MOD_ALT))) {
			if (press && cmdKey (ui_ev_field (&ui, EV_CODE), ui_ev_field (&ui, EV_CHAR))) continue;
			if (!press) continue;
		}
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
		if (t == WEV_WHEEL && !w && g_overView) viewWheel (ui_ev_field (&ui, EV_Y));
		if (t == WEV_MOUSE && g_camDirty && ui_ev_field (&ui, EV_KIND) == 2) cameraChanged ();	// (au dernier point du pointeur)
		if (!press || taken) continue;
		int key = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR);
		if (skKey (key, ch, mods)) continue;
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
		else if ((ch == 's' || ch == 'S') && !g_sm.on) skEnter (-1);
		else if (ch == 'e' || ch == 'E') cmdStart (T_EXTRUDE);
		else if (ch == 'h' || ch == 'H') cmdStart (T_HOLE);
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
	// l'accrochage à 5 mm : le point de départ et les tailles tirées tombent sur ses multiples
	g_snap = 5;
	cmdStart (T_BOX);
	double q0[3] = { 47.3, 11.8, 8 }, q1[3] = { 56.4, 17, 8 };
	clickAt (q0);
	moveTo (q1);
	snprintf (s, sizeof s, "base (%s, %s, %s), %s × %s", fmtNum (g_cmd.f.frame.o[0]).c_str (), fmtNum (g_cmd.f.frame.o[1]).c_str (), fmtNum (g_cmd.f.frame.o[2]).c_str (),
		  fmtNum (g_cmd.chips[0].val).c_str (), g_cmd.chips.size () > 1 ? fmtNum (g_cmd.chips[1].val).c_str () : "?");
	check ("accrochage 5 mm : base (45, 10, 8), 25 × 15", g_cmd.f.frame.o[0] == 45 && g_cmd.f.frame.o[1] == 10 && g_cmd.f.frame.o[2] == 8
	       && g_cmd.chips.size () > 1 && g_cmd.chips[0].val == 25 && g_cmd.chips[1].val == 15, s);
	cmdKey (0x29, 0);
	g_snap = 1;	// (la suite : les valeurs au mm)
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
	snprintf (s, sizeof s, "avant Entrée : %s mm, aperçu %d triangles", fmtNum (g_cmd.f.a).c_str (), (int) g_cmd.ghost.triFace.size ());
	check ("l'aperçu suit la valeur tapée (sans Entrée)", std::fabs (g_cmd.f.a - 5) < 1e-9 && !g_cmd.ghost.triFace.empty (), s);
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
	snprintf (s, sizeof s, "R max %s mm", fmtNum (g_cmd.maxR).c_str ());
	check ("congé : le plus grand rayon qui passe est proposé", g_cmd.maxR > 0.5 && g_cmd.maxR < 30, s);
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
	// l'esquisse : sur le sol (plan XY), vue de dessus
	select (Pick ());
	skEnter (-1);
	g_view.cam.tx = 120, g_view.cam.ty = 10, g_view.cam.scale = 6;
	cameraChanged ();
	auto skClick = [] (double x, double y) {
		double p[3] = { x, y, 0 }, sx, sy;
		g_view.toScreen (p, sx, sy);
		viewMouse (0, (int) std::lround (sx), (int) std::lround (sy), 0, false);
		viewMouse (1, (int) std::lround (sx), (int) std::lround (sy), 1, false);
		viewMouse (2, (int) std::lround (sx), (int) std::lround (sy), 0, false);
		g_btn = 0;
	};
	auto skType = [] (const char *t) {
		for (; *t; t++) skKey (0, *t, 0);
		skKey (0x28, 0, 0);
	};
	skSetTool (SK_RECT);
	skClick (100, 0);
	skClick (140, 20);
	Sketch &S = g_sm.s;
	snprintf (s, sizeof s, "%d éléments, %d contraintes, %d degrés de liberté", (int) S.geos.size (), (int) S.cons.size (), S.dof);
	check ("esquisse : rectangle au geste (H, V déduites)", S.geos.size () == 4 && S.cons.size () == 4 && S.dof == 4, s);
	skSetTool (SK_DIM);
	skClick (120, 0);
	skType ("50");
	double w0 = std::hypot (S.pts[S.geos[0].b].x - S.pts[S.geos[0].a].x, S.pts[S.geos[0].b].y - S.pts[S.geos[0].a].y);
	snprintf (s, sizeof s, "base %s mm, %d degrés de liberté", fmtNum (w0).c_str (), S.dof);
	check ("esquisse : cote tapée 50, PlaneGCS déplace le rectangle", std::fabs (w0 - 50) < 1e-6 && S.dof == 3, s);
	skSetTool (SK_CIRCLE);
	skClick (125, 10);
	skClick (129, 10);
	skSetTool (SK_NONE);
	int nc = (int) S.cons.size ();
	SkCons bad;
	bad.t = SC_DIST, bad.p1 = S.geos[0].a, bad.p2 = S.geos[0].b, bad.v = 30;	// (la base a déjà sa cote de 50)
	skAddCons (bad);
	snprintf (s, sizeof s, "%d contraintes (avant %d) ; %s", (int) S.cons.size (), nc, g_sm.message.c_str ());
	check ("esquisse : cote contradictoire refusée, rien ne bouge", (int) S.cons.size () == nc && !g_sm.message.empty (), s);
	g_sm.sel = { { 0, 1 } };	// la ligne de droite (verticale) : horizontale, elle s'effondrerait
	skConstrain (SC_HORIZ);
	snprintf (s, sizeof s, "%d contraintes ; %s", (int) S.cons.size (), g_sm.message.c_str ());
	check ("esquisse : horizontale sur une verticale refusée", (int) S.cons.size () == nc && !g_sm.message.empty (), s);
	g_sm.sel.clear ();
	g_sm.message.clear ();
	snprintf (s, sizeof s, "%d degrés de liberté", S.dof);
	check ("esquisse : cercle ajouté (3 de plus)", S.dof == 6, s);
	skLeave (true);
	int skIdx = g_doc.bar - 1;
	double v1 = totalVolume ();
	cmdStart (T_EXTRUDE);
	type ("15");
	cmdKey (0x28, 0);
	double want = (50 * 20 - M_PI * 16) * 15 / 1000;
	snprintf (s, sizeof s, "%s : %s ; +%s (attendu +%s)", g_doc.feats.back ().name.c_str (), featureDetail (g_doc.feats.back ()).c_str (),
		  cm3 (totalVolume () - v1).c_str (), cm3 (want).c_str ());
	check ("extrusion de l'esquisse (rectangle percé), nouveau corps", g_doc.feats.back ().kind == ST_EXTRUDE && std::fabs (totalVolume () - v1 - want) < 0.001, s);
	// l'esquisse modifiée : la cote à 60, l'extrusion suit
	for (auto &c : g_doc.feats[skIdx].sk.cons) if (c.t == SC_DIST) c.v = 60;
	skSolve (g_doc.feats[skIdx].sk);
	double v2 = totalVolume ();
	rebuild ();
	refreshAll ();
	snprintf (s, sizeof s, "%s -> %s", cm3 (v2).c_str (), cm3 (totalVolume ()).c_str ());
	check ("esquisse modifiée (50 -> 60), l'extrusion suit", std::fabs (totalVolume () - v2 - 10 * 20 * 15 / 1000.0) < 0.001, s);
	// les paramètres nommés : la plaque à « ep * 2 » ; ep = 5 rend les 10 mm
	select (Pick ());
	double v10 = totalVolume ();
	paramSet ("ep = 4");
	g_selStep = 0;
	panelUpdate ();
	ui_w_set_text (e_val[2], (long) "ep * 2");
	stepApply ();
	double v8 = totalVolume ();
	g_selStep = -1;
	paramSet ("ep = 5");
	snprintf (s, sizeof s, "ep = 4 : %s ; ep = 5 : %s (10 mm : %s) ; formule « %s »", cm3 (v8).c_str (), cm3 (totalVolume ()).c_str (), cm3 (v10).c_str (),
		  g_doc.feats[0].expr.empty () ? "" : g_doc.feats[0].expr[0].second.c_str ());
	check ("paramètre nommé dans une valeur, rejoué quand il change", !g_doc.feats[0].expr.empty () && v8 < v10 - 0.1 && std::fabs (totalVolume () - v10) < 1e-6, s);
	// une boîte de 30 × 20 × 10 à l'écart (x = 200) : la coque, la symétrie, le perçage, le réseau
	{
		Feature f;
		f.kind = ST_BOX, f.name = featureName (g_doc, ST_BOX), f.op = OP_NEW;
		f.frame.o[0] = 200, f.a = 30, f.b = 20, f.c = 10;
		pushUndo ();
		insertFeature (g_doc, g_doc.bar, f);
		rebuild ();
		refreshAll ();
	}
	int bx = -1;	// (le corps neuf : celui que l'étape a créé)
	for (size_t i = 0; i < g_doc.bodyOf.size (); i++)
		if (g_doc.bodyOf[i] == g_doc.bar - 1) bx = (int) i;
	if (bx < 0) {
		check ("boîte d'essai à l'écart", false, "pas de corps");
		return 1;
	}
	auto faceN = [&] (int body, double nz) {
		for (int i = 0; i < g_doc.bodies[body].mesh.nfaces; i++) {
			FaceRef r;
			if (faceRef (g_doc.bodies[body], i, r) && r.n[2] * nz > 0.99) return i;
		}
		return -1;
	};
	double vb = totalVolume ();
	select (Pick { bx, faceN (bx, 1), -1 });
	cmdStart (T_SHELL);
	type ("2");
	cmdKey (0x28, 0);
	double shellV = bodyVolume (g_doc.bodies[bx]);
	snprintf (s, sizeof s, "%s : %s ; corps %s mm³ (attendu 2672)", g_doc.feats.back ().name.c_str (), featureDetail (g_doc.feats.back ()).c_str (), fmtNum (shellV).c_str ());
	check ("coque de 2 mm, le dessus ouvert", g_doc.feats.back ().kind == ST_SHELL && std::fabs (shellV - 2672) < 0.5, s);
	undo (false);	// (la boîte pleine, pour la suite)
	refreshAll ();
	// le perçage : Ø 4 traversant au milieu du dessus, puis un réseau de 3 au pas de 5 (le long de X)
	vb = bodyVolume (g_doc.bodies[bx]);
	select (Pick { bx, faceN (bx, 1), -1 });
	cmdStart (T_HOLE);
	type ("4");
	cmdKey (0x2B, 0);
	type ("0");
	cmdKey (0x28, 0);
	double holeV = vb - bodyVolume (g_doc.bodies[bx]);
	snprintf (s, sizeof s, "%s : %s ; -%s mm³ (attendu %s)", g_doc.feats.back ().name.c_str (), featureDetail (g_doc.feats.back ()).c_str (), fmtNum (holeV).c_str (),
		  fmtNum (M_PI * 4 * 10).c_str ());
	check ("perçage Ø 4 traversant (Tab : la valeur suivante)", g_doc.feats.back ().kind == ST_HOLE && std::fabs (holeV - M_PI * 4 * 10) < 0.5, s);
	g_selStep = g_doc.bar - 1;
	cmdStart (T_PATTERN);
	type ("3");
	cmdKey (0x2B, 0);
	type ("5");
	cmdKey (0x28, 0);
	double patV = vb - bodyVolume (g_doc.bodies[bx]);
	snprintf (s, sizeof s, "%s : %s ; -%s mm³ (attendu %s)", g_doc.feats.back ().name.c_str (), featureDetail (g_doc.feats.back ()).c_str (), fmtNum (patV).c_str (),
		  fmtNum (3 * M_PI * 4 * 10).c_str ());
	check ("réseau linéaire du perçage (3 au pas de 5)", g_doc.feats.back ().kind == ST_PATTERN && std::fabs (patV - 3 * M_PI * 4 * 10) < 1, s);
	// la symétrie par le plan YZ de l'origine : un nouveau corps, la même matière
	select (Pick { bx, -1, -1 });
	double vAll = totalVolume ();
	cmdStart (T_MIRROR);
	g_cmd.f.op = OP_NEW;
	cmdKey (0x28, 0);
	snprintf (s, sizeof s, "%s : %s ; %s -> %s", g_doc.feats.back ().name.c_str (), featureDetail (g_doc.feats.back ()).c_str (), cm3 (vAll).c_str (), cm3 (totalVolume ()).c_str ());
	check ("symétrie du corps (nouveau corps)", g_doc.feats.back ().kind == ST_MIRROR && std::fabs (totalVolume () - vAll - bodyVolume (g_doc.bodies[bx]) / 1000) < 0.001, s);
	select (Pick ());
	g_snap = 5;	// (le réglage d'origine)
	printf ("forge : %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}

} // namespace

int main (int argc, char **argv)
{
	bool test = argc > 1 && !strcmp (argv[1], "--selftest");
	g_trace = getenv ("FORGE_TRACE") != nullptr;
	int mcpPort = 0;
	std::string mcpTok;
	bool tokGiven = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--mcp")) mcpPort = i + 1 < argc && isdigit ((unsigned char) argv[i + 1][0]) ? atoi (argv[++i]) : MCP_PORT;
		else if (!strcmp (argv[i], "--mcp-token") && i + 1 < argc) mcpTok = argv[++i], tokGiven = true;
	}
	if (!uikit_open (&ui)) return 1;
	g_ark = archivekit_kit_open () != 0;
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
	if (mcpPort) mcpStart (mcpPort, tokGiven ? (mcpTok == "-" ? "" : mcpTok) : mcpToken (), true);
	if (test) {
		int r = selftest (t1 - t0);
		if (argc > 2) loop (nowMs () + atoi (argv[2]) * 1000.0);
		return r;
	}
	loop (0);
	mcp::stop ();
	return 0;
}
