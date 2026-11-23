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
#include <Standard_Version.hxx>
#include "json.h"
#include "agent.h"
#include "mcp.h"
#include "model.h"
#include "render.h"
#include "../../lib/keyvault/keyvault.h"

using namespace forge;

#define TR(s) ((const char *) ui_tr ((long) (s)))
const char *forge::trUi (const char *s) { return TR (s); }

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
long b_ctarget, b_ctools, w_cop, c_keep;		// (combiner) cible, outils, type, conserver les outils
long e_param, b_param;				// les paramètres nommés
long e_val[3], b_apply, b_off, b_del, b_barhere;	// l'étape choisie
enum { TB_SKETCH, TB_BOX, TB_CYL, TB_PUSH, TB_FILLET, TB_CHAMFER, TB_COMBINE, TB_MEASURE, TB_N };
long tb[TB_N];

// ---- annuler, rétablir ------------------------------------------------------------------------------------------
struct Snap {
	std::vector<Feature> feats;
	int bar;
	std::vector<std::pair<std::string, std::string>> params;
};
std::vector<Snap> g_undo, g_redo;
bool g_dirty;				// changé depuis l'ouverture ou le dernier enregistrement
std::string g_path;			// le fichier du document ("" : pas encore enregistré)
void pushUndo ()
{
	g_dirty = true;
	g_undo.push_back ({ g_doc.feats, g_doc.bar, g_doc.params });
	if (g_undo.size () > 100) g_undo.erase (g_undo.begin ());
	g_redo.clear ();
}

// ---- la commande en cours ---------------------------------------------------------------------------------------
enum Tool { T_NONE, T_BOX, T_CYL, T_PUSH, T_FILLET, T_CHAMFER, T_COMBINE, T_EXTRUDE, T_REVOLVE, T_HOLE, T_SHELL, T_PATTERN, T_MIRROR };
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
	double corner[3] = { 0, 0, 0 };	// (boîte) le premier coin cliqué ; le coin opposé est de son côté sx, sy
	int sx = 1, sy = 1;
	int pick = 0;			// (combiner) le clic dans la vue choisit : 0 rien, 1 la cible, 2 les outils
	int pickBody = -1;		// (combiner) la cible et les outils d'avant le bouton (Échap y revient)
	std::vector<int> pickTools;
} g_cmd;

StepKind toolKind (Tool t)
{
	static const StepKind K[] = { ST_BOX, ST_BOX, ST_CYLINDER, ST_PUSH, ST_FILLET, ST_CHAMFER, ST_COMBINE, ST_EXTRUDE, ST_REVOLVE, ST_HOLE, ST_SHELL, ST_PATTERN, ST_MIRROR };
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
	static const char *const N[] = { "", "Boîte", "Cylindre", "Pousser/tirer", "Congé", "Chanfrein", "Combiner", "Extrusion", "Révolution", "Perçage", "Coque", "Réseau", "Symétrie" };
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
	g_view.ghostCut = (g_cmd.f.op == OP_CUT && g_cmd.tool != T_COMBINE) || ((g_cmd.tool == T_PUSH || g_cmd.tool == T_EXTRUDE) && g_cmd.f.a < 0);
	skStrokes ();
	g_view.render (g_back.data (), VW, g_doc, g_pal, g_sels, g_hot);
	for (int y = 0; y < VH; y++) memcpy (pix + (size_t) y * (pitch / 4), g_back.data () + (size_t) y * VW, (size_t) VW * 4);
	ui_surface_damage (g_surf, 0, 0, 0, 0);
	char s[160];
	int tris = 0;
	for (auto &b : g_doc.bodies) tris += (int) b.mesh.triFace.size ();
	snprintf (s, sizeof s, TR ("%s%d triangles   ·   rendu %.0f ms   ·   Accrochage %s mm   ·   mm"), mcp::running () ? "Agent (MCP)   ·   " : "", tris, g_view.lastMs,
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
		ui_w_set_text (p_name[i], (long) (on ? TR (rows[i].first.c_str ()) : ""));
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
	ui_w_show (b_ctarget, false);
	ui_w_show (b_ctools, false);
	ui_w_show (w_cop, false);
	ui_w_show (c_keep, false);
	ui_w_set_text (b_ok, (long) TR (g_cmd.tool == T_COMBINE ? "Confirmer" : "OK"));
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
		static const char *const HINT[13][3] = {
			{ "", "", "" },
			{ "Clic : un coin de la base, sur une face ou sur le sol", "Clic : le coin opposé (la diagonale)", "On monte pour la hauteur ; clic ou Entrée" },
			{ "Clic : le centre de la base, sur une face ou sur le sol", "On s'éloigne pour le rayon ; clic", "On monte pour la hauteur ; clic ou Entrée" },
			{ "Clic : la face plane à pousser ou à tirer", "Glisser pour la distance ; clic ou Entrée", "" },
			{ "Clic : les arêtes (Maj : plusieurs) ; Entrée", "Tapez le rayon ; Entrée", "" },
			{ "Clic : les arêtes (Maj : plusieurs) ; Entrée", "Tapez la distance ; Entrée", "" },
			{ "Cible ou Outils, puis les corps dans la vue", "Cible ou Outils, puis les corps dans la vue", "" },
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
		for (auto &c : g_cmd.chips) rows.push_back ({ TR (c.label), (c.text.empty () ? fmtNum (c.val) : c.text) + " mm" + (c.locked ? TR ("  (tenue)") : "") });
		bool hasOp = g_cmd.tool == T_BOX || g_cmd.tool == T_CYL || g_cmd.tool == T_EXTRUDE || g_cmd.tool == T_REVOLVE;
		static const char *const OPS[4] = { "nouveau", "joindre", "enlever", "intersection" };
		if (hasOp) rows.push_back ({ TR ("Opération"), std::string (TR (OPS[g_cmd.f.op])) + (g_cmd.opAuto ? TR (" · auto") : "") });
		if (g_cmd.tool == T_FILLET || g_cmd.tool == T_CHAMFER) rows.push_back ({ TR ("Arêtes"), std::to_string (g_cmd.f.edges.size ()) });
		if (g_cmd.tool == T_SHELL) rows.push_back ({ TR ("Faces ouvertes"), std::to_string (g_cmd.f.faces.size ()) });
		if (g_cmd.tool == T_HOLE) rows.push_back ({ TR ("Trous"), std::to_string (g_cmd.f.pos.size () / 2) });
		if (g_cmd.tool == T_PATTERN && g_cmd.f.ref >= 0) rows.push_back ({ TR ("Répète"), g_doc.feats[g_cmd.f.ref].name });
		if (g_cmd.tool == T_COMBINE) {	// (les noms : à droite des boutons Cible et Outils)
			auto bname = [] (int b) { return b >= 0 && b < (int) g_doc.bodies.size () ? g_doc.bodies[b].name : std::string ("?"); };
			std::string tn;
			for (int b : g_cmd.f.tools) tn += (tn.empty () ? "" : ", ") + bname (b);
			rows.push_back ({ "", g_cmd.pick == 1 ? TR ("cliquez un corps…") : g_cmd.f.body >= 0 ? bname (g_cmd.f.body) : TR ("aucune") });
			rows.push_back ({ "", g_cmd.pick == 2 ? (tn.empty () ? std::string (TR ("cliquez des corps…")) : tn + " …") : tn.empty () ? TR ("aucun") : tn });
			ui_w_set_text (b_ctarget, (long) TR (g_cmd.pick == 1 ? "▸ Cible" : "Cible"));
			ui_w_set_text (b_ctools, (long) TR (g_cmd.pick == 2 ? "▸ Outils" : "Outils"));
			ui_w_show (b_ctarget, true);
			ui_w_show (b_ctools, true);
			ui_w_show (w_cop, true);
			ui_w_set_value (w_cop, g_cmd.f.op == OP_CUT ? 1 : g_cmd.f.op == OP_INTERSECT ? 2 : 0);
			ui_w_show (c_keep, true);
			ui_w_set_value (c_keep, g_cmd.f.keep ? 1 : 0);
		}
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
		if (g_sels.size () > 1) in.title = std::to_string (g_sels.size ()) + (k.edge >= 0 ? TR (" arêtes") : TR (" faces")), in.sub = b.name;
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
	if (g_cmd.tool == T_FILLET || g_cmd.tool == T_CHAMFER || g_cmd.tool == T_COMBINE) return;
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
	case T_BOX: {	// (le centre de la base : entre le premier coin et le coin opposé, aussi pour une taille tapée)
		f.a = v (0), f.b = v (1), f.c = g_cmd.stage >= 2 ? v (2) : 1;
		if (g_cmd.stage < 1) break;
		const double *n = f.frame.n, *xa = f.frame.x;
		double ya[3] = { n[1] * xa[2] - n[2] * xa[1], n[2] * xa[0] - n[0] * xa[2], n[0] * xa[1] - n[1] * xa[0] };
		for (int k = 0; k < 3; k++) f.frame.o[k] = g_cmd.corner[k] + g_cmd.sx * f.a / 2 * xa[k] + g_cmd.sy * f.b / 2 * ya[k];
		break;
	}
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
		: tt == T_PATTERN ? g_cmd.f.ref >= 0 : tt == T_MIRROR ? g_cmd.f.body >= 0 : tt == T_COMBINE ? g_cmd.f.body >= 0 && !g_cmd.f.tools.empty () : false;
	bool probed = tt == T_FILLET || tt == T_CHAMFER || tt == T_SHELL || tt == T_PATTERN || tt == T_MIRROR || tt == T_COMBINE;
	if (tt == T_COMBINE) {		// (la cible et les outils : montrés choisis)
		g_sels.clear ();
		if (g_cmd.f.body >= 0) g_sels.push_back (Pick { g_cmd.f.body, -1, -1 });
		for (int b : g_cmd.f.tools) g_sels.push_back (Pick { b, -1, -1 });
	}
	if (probed && !ready) {
		g_cmd.ghost = Mesh ();
		g_cmd.err.clear ();
	} else if (probed) {	// (l'aperçu : la vraie forme, calculée)
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
	if (t <= T_COMBINE)
		ui_w_set_value (tb[t == T_BOX ? TB_BOX : t == T_CYL ? TB_CYL : t == T_PUSH ? TB_PUSH : t == T_FILLET ? TB_FILLET : t == T_CHAMFER ? TB_CHAMFER
			: TB_COMBINE], 1);
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
	if (t == T_COMBINE) {	// (le corps choisi : la cible, puis les outils ; sinon la cible d'abord)
		f.op = OP_JOIN;
		g_cmd.stage = 1;
		if (!k.none ()) f.body = k.body, g_cmd.pick = 2;
		else g_cmd.pick = 1;
		g_cmd.pickBody = f.body;
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
			status (TR (("L'esquisse n'a " + err + ".").c_str ()));
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
	if (g_cmd.tool == T_COMBINE && (f.body < 0 || f.tools.empty () || !g_cmd.err.empty ())) {
		status (TR (f.body < 0 ? "Combiner : choisissez la cible." : f.tools.empty () ? "Combiner : choisissez au moins un outil." : "Combiner : l'opération ne passe pas."));
		return;
	}
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
			const double *c0 = t == T_BOX ? g_cmd.corner : f.frame.o;	// (boîte : depuis le premier coin ; cylindre : depuis le centre)
			if (!g_view.onPlane (x, y, c0, f.frame.n, p)) return true;
			double d[3] = { p[0] - c0[0], p[1] - c0[1], p[2] - c0[2] };
			const double *n = f.frame.n, *xa = f.frame.x;
			double ya[3] = { n[1] * xa[2] - n[2] * xa[1], n[2] * xa[0] - n[0] * xa[2], n[0] * xa[1] - n[1] * xa[0] };
			double lx = d[0] * xa[0] + d[1] * xa[1] + d[2] * xa[2], ly = d[0] * ya[0] + d[1] * ya[1] + d[2] * ya[2];
			if (t == T_BOX) {	// (coin à coin : le coin opposé sous le pointeur)
				g_cmd.sx = lx < 0 ? -1 : 1, g_cmd.sy = ly < 0 ? -1 : 1;
				if (!g_cmd.chips[0].locked) g_cmd.chips[0].val = std::max (g_snap, snapv (std::fabs (lx)));
				if (!g_cmd.chips[1].locked) g_cmd.chips[1].val = std::max (g_snap, snapv (std::fabs (ly)));
			} else if (!g_cmd.chips[0].locked)
				g_cmd.chips[0].val = std::max (g_snap, snapv (2 * sqrt (lx * lx + ly * ly)));
			g_cmdDirty = true;
			return true;
		}
		if ((t == T_BOX || t == T_CYL) && g_cmd.stage == 2) {
			Chip &h = g_cmd.chips.back ();
			if (!h.locked) {
				// boîte : le long de l'arête verticale du coin opposé (le coin suit le pointeur) ; cylindre : de l'axe
				double far[3];
				const double *n = f.frame.n, *xa = f.frame.x;
				double ya[3] = { n[1] * xa[2] - n[2] * xa[1], n[2] * xa[0] - n[0] * xa[2], n[0] * xa[1] - n[1] * xa[0] };
				for (int k = 0; k < 3; k++) far[k] = t == T_BOX ? f.frame.o[k] + g_cmd.sx * f.a / 2 * xa[k] + g_cmd.sy * f.b / 2 * ya[k] : f.frame.o[k];
				double v = alongNormal (far, f.frame.n, x, y);
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
			for (int k = 0; k < 3; k++) g_cmd.corner[k] = f.frame.o[k];
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
	if (t == T_COMBINE) {	// le clic choisit selon le bouton pressé : la cible, ou un outil de plus (un outil recliqué s'en va)
		Pick k = g_view.pick (g_doc, x, y);
		if (k.none ()) return true;
		auto &tl = f.tools;
		auto in = std::find (tl.begin (), tl.end (), k.body);
		if (g_cmd.pick == 1) {
			f.body = k.body;
			if (in != tl.end ()) tl.erase (in);
			g_cmd.pick = tl.empty () ? 2 : 0;	// (puis les outils, s'il n'y en a pas encore)
			g_cmd.pickBody = f.body, g_cmd.pickTools = tl;
		} else if (g_cmd.pick == 2) {
			if (k.body == f.body) {
				status (TR ("La cible ne peut pas être aussi un outil."));
				return true;
			}
			if (in != tl.end ()) tl.erase (in);
			else tl.push_back (k.body);
		} else {
			status (TR ("Combiner : le bouton Cible ou Outils, puis les corps."));
			return true;
		}
		cmdRefresh ();
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
	if (code == 0x29 && g_cmd.tool == T_COMBINE && g_cmd.pick) {	// Échap : le choix en cours annulé (la commande reste)
		g_cmd.f.body = g_cmd.pickBody, g_cmd.f.tools = g_cmd.pickTools, g_cmd.pick = 0;
		cmdRefresh ();
		status (TR ("Choix annulé."));
		return true;
	}
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
			if (t == T_FILLET || t == T_CHAMFER || t == T_SHELL || t == T_PATTERN || t == T_MIRROR || t == T_COMBINE) {	// (l'aperçu d'abord : refusée s'il ne passe pas)
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
	std::string pl = std::fabs (n[2]) > 0.999 && s.body < 0 ? TR ("plan XY") : s.body >= 0 ? TR ("sur une face") : TR ("plan");
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
bool g_cubeMenu;	// (un clic droit sur le cube de vue : son menu, cubeMenu, après l'événement)

// La projection, comme « Perspective avec faces orthographiques » de Fusion : g_perspPref est le choix de
// l'utilisateur (gardé dans forge.ini) ; une vue de face (une face du cube, Alt+1/2/3) passe en orthographique pour
// un temps (g_orthoFace), et tourner la vue ou revenir à l'isométrique (Maison, Alt+0) rend la perspective.
bool g_perspPref = true, g_orthoFace;

void viewFace (int k)	// 0..5 : une face du cube ; 6 : l'isométrique de départ
{
	g_view.standard (k);
	g_orthoFace = k != 6 && g_perspPref;
	g_view.persp = g_perspPref && !g_orthoFace;
	if (g_orthoFace) status (TR ("Vue de face, orthographique : tourner la vue rend la perspective"));
}
bool g_dragged, g_overView;

// genre : 0 déplacement, 1 appui, 2 relâché, 3 sortie ; x, y dans la vue ; boutons : 1 gauche, 2 droit, 4 milieu
bool g_trace;
void viewMouse (int kind, int x, int y, int buttons, bool shift)
{
	if (g_trace) fprintf (stderr, "forge : souris %d (%d, %d) boutons %d, commande %d étape %d\n", kind, x, y, buttons, (int) g_cmd.tool, g_cmd.stage);
	if (kind == 1) {
		g_downX = x, g_downY = y, g_btn = buttons, g_dragged = false;
		g_mx = x, g_my = y;
		if (buttons & 4) g_view.panFrom (x, y);	// (le point pris reste sous le pointeur)
	}
	if (kind == 0) {
		int dx = x - g_mx, dy = y - g_my;
		g_mx = x, g_my = y;
		if (buttons & 2) {	// droit : tourner
			if (std::abs (x - g_downX) + std::abs (y - g_downY) > 3) g_dragged = true;
			if (g_orthoFace && g_dragged) g_orthoFace = false, g_view.persp = g_perspPref;	// (hors de la vue de face : la perspective revient)
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
	int cbx, cby, cbw, cbh;
	g_view.cubeRect (cbx, cby, cbw, cbh);
	bool inCube = x >= cbx && x < cbx + cbw && y >= cby && y < cby + cbh;
	bool overUi = (x >= BP_X && x < BP_X + BP_W && y >= BP_Y && y < BP_Y + bodiesH ()) || g_view.cubeFaceAt (x, y) >= 0 || inCube;
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
		if ((g_btn & 2) && !g_dragged && inCube) {	// clic droit sur le cube : son menu (ouvert après l'événement)
			g_cubeMenu = true;
			return;
		}
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
				if (i == 0) viewFace (6), g_view.fit (g_doc);
				else if (i == 1) g_view.fit (g_doc);
				else if (i == 2) g_view.edges = !g_view.edges, ui_menu_state (25, g_view.edges ? UMS_CHECKED : 0);
				else g_view.grid = !g_view.grid, ui_menu_state (26, g_view.grid ? UMS_CHECKED : 0);
				cameraChanged ();
				return;
			}
		}
		int f = g_view.cubeFaceAt (x, y);
		if (f >= 0) {
			viewFace (f);
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

// Une étape : une icône (son genre), le nom et le résumé en infobulle au survol. Clic : ses valeurs au panneau ;
// clic droit : Modifier…, Supprimer (avec ce qui en dépend, après confirmation), Supprimer à partir d'ici.
const int CHIP_W = 34, CHIP_GAP = 6, ICON = 20;
int CHIP_X0 = 16;			// (moins quand la frise défile : la barre reste en vue)
bool g_barDrag;
int g_tlHover = -1;			// l'étape sous le pointeur (son infobulle est montrée)
int barX (int bar) { return CHIP_X0 + bar * (CHIP_W + CHIP_GAP) - CHIP_GAP / 2 - 1; }
void timelineScroll (int w)
{
	CHIP_X0 = 16;
	int room = w - 110;			// (à droite : le nombre d'étapes)
	int need = 16 + (int) g_doc.feats.size () * (CHIP_W + CHIP_GAP) + 8;
	if (need > room) CHIP_X0 = 16 - imin (need - room, imax (0, barX (g_doc.bar) + CHIP_W - room + 16));
}

// les icônes : des traits sur une grille de 24 (polylignes, arcs), lissés en ICON × ICON, une fois par genre
struct IconLine { std::vector<double> p; };
std::vector<IconLine> iconLines (const Feature &f)
{
	std::vector<IconLine> L;
	auto line = [&] (std::initializer_list<double> v) { L.push_back ({ std::vector<double> (v) }); };
	auto arc = [&] (double cx, double cy, double rx, double ry, double a0, double a1) {	// (degrés, y vers le bas)
		IconLine l;
		int n = imax (6, (int) (std::fabs (a1 - a0) / 10));
		for (int k = 0; k <= n; k++) {
			double a = (a0 + (a1 - a0) * k / n) * M_PI / 180;
			l.p.push_back (cx + rx * cos (a)), l.p.push_back (cy + ry * sin (a));
		}
		L.push_back (l);
	};
	switch (f.kind) {
	case ST_BOX:
		line ({ 12, 3, 20, 7.5, 20, 16.5, 12, 21, 4, 16.5, 4, 7.5, 12, 3 });
		line ({ 4, 7.5, 12, 12, 20, 7.5 });
		line ({ 12, 12, 12, 21 });
		break;
	case ST_CYLINDER:
		arc (12, 6, 7, 2.6, 0, 360);
		line ({ 5, 6, 5, 18 });
		line ({ 19, 6, 19, 18 });
		arc (12, 18, 7, 2.6, 0, 180);
		break;
	case ST_SKETCH:
		line ({ 4, 18, 16, 6, 19, 9, 7, 21, 4, 18 });
		line ({ 4, 18, 3, 21.5, 7, 21 });
		line ({ 14, 8, 17, 11 });
		break;
	case ST_EXTRUDE:
		line ({ 4, 16, 20, 16, 20, 21, 4, 21, 4, 16 });
		line ({ 12, 13, 12, 3 });
		line ({ 8, 7, 12, 3, 16, 7 });
		break;
	case ST_PUSH:
		line ({ 4, 16, 20, 16, 20, 21, 4, 21, 4, 16 });
		line ({ 12, 2.5, 12, 12.5 });
		line ({ 9, 5.5, 12, 2.5, 15, 5.5 });
		line ({ 9, 9.5, 12, 12.5, 15, 9.5 });
		break;
	case ST_FILLET:
		line ({ 5, 3, 5, 12 });
		arc (12, 12, 7, 7, 180, 90);
		line ({ 12, 19, 21, 19 });
		break;
	case ST_CHAMFER:
		line ({ 5, 3, 5, 12, 12, 19, 21, 19 });
		break;
	case ST_COMBINE: case ST_CUT: {
		Op o = f.kind == ST_CUT ? OP_CUT : f.op == OP_NEW ? OP_JOIN : f.op;
		arc (9, 12, 6.5, 6.5, 0, 360);
		arc (15, 12, 6.5, 6.5, 0, 360);
		if (o == OP_CUT) line ({ 16, 12, 20, 12 });			// (la soustraction : un moins dans l'outil)
		else if (o == OP_INTERSECT) line ({ 12, 8, 12, 16 });		// (l'intersection : la part commune)
		else line ({ 3, 12, 21, 12 }), line ({ 12, 3.5, 12, 20.5 });	// (l'union : un plus)
		break;
	}
	case ST_HOLE:
		arc (12, 12, 8, 8, 0, 360);
		arc (12, 12, 3.5, 3.5, 0, 360);
		break;
	case ST_SLOT:
		line ({ 8, 7.5, 16, 7.5 });
		line ({ 8, 16.5, 16, 16.5 });
		arc (8, 12, 4.5, 4.5, 90, 270);
		arc (16, 12, 4.5, 4.5, -90, 90);
		break;
	case ST_REVOLVE:
		arc (12, 12, 8, 8, -60, 240);
		line ({ 13, 3.5, 16, 5.1, 13.5, 8 });
		line ({ 12, 9, 12, 15 });
		break;
	case ST_SHELL:
		line ({ 4, 5, 4, 20, 20, 20, 20, 5 });
		line ({ 8, 5, 8, 16, 16, 16, 16, 5 });
		line ({ 4, 5, 8, 5 });
		line ({ 16, 5, 20, 5 });
		break;
	case ST_PATTERN:
		for (int k = 0; k < 3; k++) {
			double x = 3 + k * 7;
			line ({ x, 9, x + 4.5, 9, x + 4.5, 14.5, x, 14.5, x, 9 });
		}
		break;
	case ST_MIRROR:
		for (double y = 2; y < 22; y += 4) line ({ 12, y, 12, y + 2 });
		line ({ 9.5, 6, 9.5, 18, 3, 18, 9.5, 6 });
		line ({ 14.5, 6, 14.5, 18, 21, 18, 14.5, 6 });
		break;
	default:
		arc (12, 12, 7, 7, 0, 360);
		break;
	}
	return L;
}

// la couverture (0..255) de chaque pixel : la distance au trait le plus proche, un trait de 1,7 unité
std::vector<uint8_t> iconMask (const Feature &f)
{
	std::vector<IconLine> L = iconLines (f);
	std::vector<uint8_t> m ((size_t) ICON * ICON, 0);
	const double u = 24.0 / ICON, hw = 0.85;
	for (int py = 0; py < ICON; py++)
		for (int px = 0; px < ICON; px++) {
			double x = (px + 0.5) * u, y = (py + 0.5) * u, best = 1e9;
			for (auto &l : L)
				for (size_t k = 0; k + 3 < l.p.size (); k += 2) {
					double ax = l.p[k], ay = l.p[k + 1], bx = l.p[k + 2], by = l.p[k + 3];
					double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
					double t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
					t = t < 0 ? 0 : t > 1 ? 1 : t;
					double ex = ax + t * dx - x, ey = ay + t * dy - y;
					best = std::min (best, ex * ex + ey * ey);
				}
			double a = (hw - sqrt (best)) / u + 0.5;
			m[(size_t) py * ICON + px] = (uint8_t) (a <= 0 ? 0 : a >= 1 ? 255 : a * 255);
		}
	return m;
}

// l'icône posée à (x, y) sur le fond bg : par rangées de pixels de même couverture
void drawIcon (const Feature &f, int x, int y, unsigned bg, unsigned ink)
{
	static std::vector<std::pair<std::string, std::vector<uint8_t>>> cache;	// (le genre et, pour combiner, l'opération)
	Op o = f.kind == ST_COMBINE ? (f.op == OP_NEW ? OP_JOIN : f.op) : OP_NEW;
	std::string key = std::to_string ((int) f.kind) + "/" + std::to_string ((int) o);
	const std::vector<uint8_t> *m = nullptr;
	for (auto &c : cache) if (c.first == key) m = &c.second;
	if (!m) {
		cache.push_back ({ key, iconMask (f) });
		m = &cache.back ().second;
	}
	for (int py = 0; py < ICON; py++)
		for (int px = 0; px < ICON;) {
			uint8_t a = (*m)[(size_t) py * ICON + px];
			int run = 1;
			while (px + run < ICON && (*m)[(size_t) py * ICON + px + run] == a) run++;
			if (a) ui_fill (x + px, y + py, run, 1, (long) ui_mix (bg, ink, a == 255 ? 256 : a));
			px += run;
		}
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
		bool on = (int) i == g_selStep, after = (int) i >= g_doc.bar, bad = !f.error.empty (), hot = (int) i == g_tlHover;
		unsigned bg = on ? jet (TJ_ACCENT_LIGHT) : hot ? (unsigned) ui_mix (jet (TJ_SURFACE), jet (TJ_ACCENT_LIGHT), 128) : jet (TJ_SURFACE);
		unsigned ln = bad ? jet (TJ_DANGER) : on ? jet (TJ_ACCENT) : jet (TJ_LINE);
		ui_rbox (x, y, CHIP_W, ch, 7, ln, ln);
		ui_rbox (x + 1 + bad, y + 1 + bad, CHIP_W - 2 - 2 * bad, ch - 2 - 2 * bad, 6, bg, bg);
		int tc = after || f.off ? TJ_TEXT3 : on ? TJ_ACCENT_DARK : TJ_TEXT1;
		drawIcon (f, x + (CHIP_W - ICON) / 2, y + (ch - ICON) / 2, bg, jet (tc));
		if (f.off) ui_fill (x + 6, y + ch / 2, CHIP_W - 12, 1, jet (TJ_TEXT3));	// (désactivée : barrée)
		if (bad) ui_fill (x + CHIP_W - 9, y + 4, 5, 5, jet (TJ_DANGER));
	}
	int bx = barX (g_doc.bar);
	ui_fill (bx, 6, 3, (int) h - 12, jet (TJ_ACCENT));
	ui_fill (bx - 4, 3, 11, 5, jet (TJ_ACCENT));
	char s[64];
	snprintf (s, sizeof s, TR ("%d étapes"), (int) g_doc.feats.size ());
	long tw = ui_text_width ((long) s, 12, (long) ui.font);
	ui_fill (w - 104, 1, 104, h - 1, jet (TJ_FACE));
	ui_text ((long) s, w - 16 - tw, h / 2 + 5, jet (TJ_TEXT3), 12, (long) ui.font, 0);
}

int timelineAt (int x)
{
	if (x < CHIP_X0) return -1;
	int i = (x - CHIP_X0) / (CHIP_W + CHIP_GAP);
	if (i >= (int) g_doc.feats.size () || (x - CHIP_X0) % (CHIP_W + CHIP_GAP) >= CHIP_W) return -1;
	return i;
}

void timelineTip (int i)
{
	if (i == g_tlHover) return;
	g_tlHover = i;
	ui_view_dirty (w_tl);
	ui_view_flush ();
	if (i < 0) {
		ui_tip_at (0, 0, 0);
		return;
	}
	const Feature &f = g_doc.feats[i];
	std::string t = f.name + " · " + featureDetail (f);
	if (f.off) t += TR (" · désactivée");
	if (i >= g_doc.bar) t += TR (" · après la barre");
	if (!f.error.empty ()) t += " · ⚠ " + f.error;
	ui_tip_at (CHIP_X0 + i * (CHIP_W + CHIP_GAP), VY + VH - 26, (long) t.c_str ());
}

void stepDelete ();
void stepDeleteFrom (int at);
void stepEdit (int i);
int g_tlMenu = -1;

void timelineMenu ()
{
	int i = g_tlMenu;
	g_tlMenu = -1;
	if (i < 0 || i >= (int) g_doc.feats.size ()) return;
	char t[256];
	int n = 0;
	for (const char *l : { "Modifier…", "Supprimer", "Supprimer à partir d'ici" }) {
		const char *s = TR (l);
		size_t k = strlen (s);
		if (n + k + 2 > sizeof t) break;
		memcpy (t + n, s, k + 1);
		n += (int) k + 1;
	}
	t[n] = 0;
	long r = ui_popup_local (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), (long) t, 240);
	if (r == 0) stepEdit (i);
	else if (r == 1) stepDelete ();
	else if (r == 2) stepDeleteFrom (i);
}

long timelineMouse (long, long kind, long x, long, long info)
{
	if (kind == 3) {		// (le pointeur sorti : plus d'infobulle)
		timelineTip (-1);
		return 0;
	}
	if (g_cmd.tool != T_NONE || g_sm.on) return 0;
	if (kind == 0 && !g_barDrag) timelineTip (timelineAt ((int) x));
	if (kind == 1 && std::abs ((int) x - barX (g_doc.bar) - 1) <= 7 && !((info >> 8) & 2)) {	// la barre : glissée
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
	int i = timelineAt ((int) x);
	g_selStep = i;
	g_sels.clear ();
	refreshAll ();
	if (i >= 0 && ((info >> 8) & 2)) {	// clic droit : le menu de l'étape
		timelineTip (-1);
		g_tlMenu = i;	// (le menu s'ouvre après l'événement : timelineMenu, dans la boucle)
	}
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

// l'étape choisie supprimée ; si d'autres en dépendent, la liste est demandée d'abord : elles partent avec elle, une
// forme jointe à un corps disparu devient un nouveau corps, un outil disparu sort de Combiner
void stepDelete ()
{
	if (g_selStep < 0 || g_selStep >= (int) g_doc.feats.size ()) return;
	Removal plan = planRemoval (g_doc, g_selStep);
	if (plan.erased.size () > 1 || !plan.renewed.empty () || !plan.trimmed.empty ()) {
		char head[200];
		snprintf (head, sizeof head, TR ("D'autres étapes dépendent de « %s » :"), g_doc.feats[g_selStep].name.c_str ());
		std::string m = std::string (head) + "\n";
		for (int j : plan.erased) if (j != g_selStep) m += "  · " + g_doc.feats[j].name + TR (" : supprimée") + "\n";
		for (int j : plan.renewed) m += "  · " + g_doc.feats[j].name + TR (" : devient un nouveau corps") + "\n";
		for (int j : plan.trimmed) m += "  · " + g_doc.feats[j].name + TR (" : perd un outil") + "\n";
		m += TR ("Supprimer quand même ?");
		long ok = ui_message ((long) TR ("Supprimer l'étape"), (long) m.c_str (), UM_OKCANCEL);
		ui_view_dirty (w_tl);
		ui_w_draw (0);
		if (ok != 1) {
			status (TR ("Suppression annulée."));
			return;
		}
	}
	pushUndo ();
	std::string name = g_doc.feats[g_selStep].name;
	applyRemoval (g_doc, plan);
	g_selStep = -1;
	rebuild ();
	refreshAll ();
	char s[200];
	snprintf (s, sizeof s, TR ("%s supprimée (%d étapes en tout). Ctrl+Z : annuler."), name.c_str (), (int) plan.erased.size ());
	status (s);
}

// l'étape et toutes celles qui la suivent
void stepDeleteFrom (int at)
{
	if (at < 0 || at >= (int) g_doc.feats.size ()) return;
	int n = (int) g_doc.feats.size () - at;
	if (n > 1) {
		char m[240];
		snprintf (m, sizeof m, TR ("Supprimer « %s » et les %d étapes qui la suivent ?"), g_doc.feats[at].name.c_str (), n - 1);
		long ok = ui_message ((long) TR ("Supprimer à partir d'ici"), (long) m, UM_OKCANCEL);
		ui_view_dirty (w_tl);
		ui_w_draw (0);
		if (ok != 1) {
			status (TR ("Suppression annulée."));
			return;
		}
	}
	pushUndo ();
	g_doc.feats.resize (at);
	g_doc.bar = imin (g_doc.bar, at);
	g_selStep = -1;
	rebuild ();
	refreshAll ();
	char s[120];
	snprintf (s, sizeof s, TR ("%d étapes supprimées. Ctrl+Z : annuler."), n);
	status (s);
}

// « Modifier… » : une esquisse s'ouvre ; une autre étape montre ses valeurs au panneau, la première prête à taper
void stepEdit (int i)
{
	if (i < 0 || i >= (int) g_doc.feats.size ()) return;
	if (g_doc.feats[i].kind == ST_SKETCH) {
		skEnter (i);
		return;
	}
	g_selStep = i;
	g_sels.clear ();
	refreshAll ();
	const char *names[3];
	double vals[3];
	if (featFields (g_doc.feats[i], names, vals) > 0) ui_w_focus (e_val[0]);
	else status (TR ("Cette étape n'a pas de valeur à changer : supprimez-la et refaites-la."));
}

void undo (bool redo)
{
	auto &from = redo ? g_redo : g_undo, &to = redo ? g_undo : g_redo;
	if (from.empty ()) return;
	to.push_back ({ g_doc.feats, g_doc.bar, g_doc.params });
	g_dirty = true;
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

// ---- les fichiers ----------------------------------------------------------------------------------------------
//
// Un document .forge : l'historique en JSON (docfile.cpp). Nouveau, Ouvrir, Enregistrer, Enregistrer sous ; avant de
// quitter ou de remplacer un document changé, la question d'enregistrer.

void docLabels ()
{
	ui_w_set_text (w_doc, (long) g_doc.name.c_str ());
	std::string sub = g_path.empty () ? std::string (TR ("Pas enregistré · mm")) : g_path.substr (g_path.find_last_of ('/') + 1) + " · mm";
	if (g_dirty) sub = "● " + sub;
	ui_w_set_text (w_docsub, (long) sub.c_str ());
	ui_w_draw (w_doc);
	ui_w_draw (w_docsub);
}

// un document neuf dans la fenêtre : la commande, l'esquisse, l'historique d'annulation et les choix repartent de zéro
void docInstall (const Doc &d, const std::string &path)
{
	if (g_sm.on) skLeave (false);
	if (g_cmd.tool != T_NONE) cmdEnd ();
	g_doc = d;
	g_path = path;
	g_dirty = false;
	g_undo.clear (), g_redo.clear ();
	g_sels.clear ();
	g_hidden.clear ();
	g_selStep = -1;
	rebuild ();
	g_view.fit (g_doc);
	refreshAll ();
	docLabels ();
}

bool docSaveAs ();
bool docSaveTo (const std::string &path)
{
	std::string err = docSave (g_doc, path);
	if (!err.empty ()) {
		std::string m = std::string (TR ("Le document n'a pas été enregistré : ")) + TR (err.c_str ()) + ".";
		ui_message ((long) TR ("Enregistrer"), (long) m.c_str (), UM_OK);
		ui_view_dirty (w_tl);
		ui_w_draw (0);
		return false;
	}
	g_path = path;
	g_dirty = false;
	docLabels ();
	status ((std::string (TR ("Enregistré : ")) + path).c_str ());
	return true;
}

bool docSaveCmd () { return g_path.empty () ? docSaveAs () : docSaveTo (g_path); }

bool docSaveAs ()
{
	char path[1024];
	std::string name = g_doc.name.empty () ? std::string (TR ("Sans titre")) : g_doc.name;
	name += ".forge";
	std::string dir = g_path.empty () ? std::string ("HOME:/") : g_path.substr (0, g_path.find_last_of ('/') + 1);
	ui_file_filter ((long) "*.forge");
	long ok = ui_file_save ((long) dir.c_str (), (long) name.c_str (), (long) path, (long) sizeof path);
	ui_file_filter (0);
	ui_view_dirty (w_tl);
	ui_w_draw (0);
	if (ok != 1 || !path[0]) return false;
	std::string p = path;
	if (p.size () < 6 || p.compare (p.size () - 6, 6, ".forge")) p += ".forge";
	std::string base = p.substr (p.find_last_of ('/') + 1);
	g_doc.name = base.substr (0, base.size () - 6);	// (le nom du document : celui du fichier)
	return docSaveTo (p);
}

// un document changé : l'enregistrer ? -> faux si l'utilisateur annule (rien ne se fait alors)
bool docKeepChanges ()
{
	if (!g_dirty) return true;
	char m[300];
	snprintf (m, sizeof m, TR ("Enregistrer les modifications de « %s » ?"), g_doc.name.c_str ());
	long r = ui_message ((long) TR ("Forge"), (long) m, UM_YESNOCANCEL);
	ui_view_dirty (w_tl);
	ui_w_draw (0);
	if (r == 1) return docSaveCmd ();
	return r == 2;		// (Non : sans enregistrer ; Annuler : rien)
}

void docNew ()
{
	if (!docKeepChanges ()) return;
	Doc d;
	d.name = TR ("Sans titre");
	docInstall (d, "");
	status (TR ("Nouveau document : B une boîte, S une esquisse."));
}

bool docOpenPath (const std::string &path)
{
	Doc d;
	std::string err = docLoad (d, path);
	if (!err.empty ()) {
		std::string m = path + " : " + TR (err.c_str ()) + ".";
		ui_message ((long) TR ("Ouvrir"), (long) m.c_str (), UM_OK);
		ui_view_dirty (w_tl);
		ui_w_draw (0);
		return false;
	}
	if (d.name.empty ()) {
		std::string base = path.substr (path.find_last_of ('/') + 1);
		d.name = base.size () > 6 ? base.substr (0, base.size () - 6) : base;
	}
	docInstall (d, path);
	int err2 = 0;
	for (auto &f : g_doc.feats) err2 += !f.error.empty ();
	char s[200];
	snprintf (s, sizeof s, TR ("%s : %d étapes, %d en erreur."), g_doc.name.c_str (), (int) g_doc.feats.size (), err2);
	status (s);
	return true;
}

void docOpen ()
{
	if (!docKeepChanges ()) return;
	char path[1024];
	std::string dir = g_path.empty () ? std::string ("HOME:/") : g_path.substr (0, g_path.find_last_of ('/') + 1);
	ui_file_filter ((long) "*.forge");
	long ok = ui_file_open ((long) dir.c_str (), (long) path, (long) sizeof path);
	ui_file_filter (0);
	ui_view_dirty (w_tl);
	ui_w_draw (0);
	if (ok == 1 && path[0]) docOpenPath (path);
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
	ui_w_move (b_ctarget, px, y + 88, pw / 2 - 8, h);	// (combiner : sur les deux premières lignes, le nom à droite)
	ui_w_move (b_ctools, px, y + 88 + 32, pw / 2 - 8, h);
	ui_w_move (w_cop, px, y + 92 + 2 * 32 + 6, pw, h);
	ui_w_move (c_keep, px, y + 92 + 3 * 32 + 12, pw, 24);
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
	"MFichier\n" "I1\tNouveau\tCtrl+N\n" "I2\tOuvrir…\tCtrl+O\n" "-\n" "I3\tEnregistrer\tCtrl+S\n" "I4\tEnregistrer sous…\tCtrl+Maj+S\n" "-\n"
	"I13c\tPiloter par un agent (MCP)…\n" "-\n" "I9\tQuitter\tCtrl+Q\n"
	"MÉdition\n" "I10\tAnnuler l'étape\tCtrl+Z\n" "I11\tRétablir l'étape\tCtrl+Y\n" "-\n" "I12\tSupprimer l'étape\n"
	"MAffichage\n" "I20\tVue de face\tAlt+1\n" "I21\tVue de dessus\tAlt+2\n" "I22\tVue de droite\tAlt+3\n"
	"I23\tVue isométrique\tAlt+0\n" "-\n" "I24\tAjuster\tF6\n" "-\n" "I25c\tArêtes\n" "I26c\tGrille\n" "-\n" "I27c\tPerspective\tAlt+5\n" "I28c\tOrthographique\tAlt+6\n" "-\n" "I45c\tAccrochage 1 mm\n" "I46c\tAccrochage 5 mm\n" "I47c\tAccrochage 10 mm\n"
	"MCréer\n" "I37\tEsquisse (S)\n" "I38\tExtrusion (E)\n" "I39\tRévolution\n" "I41\tPerçage (H)\n" "-\n" "I30\tBoîte (B)\n" "I31\tCylindre\n" "-\n" "I32\tPousser/tirer (Q)\n"
	"MModifier\n" "I33\tCongé (F ; double-clic : la chaîne)\n" "I34\tChanfrein (Maj+F)\n" "-\n" "I35\tCombiner\n" "-\n" "I42\tCoque\n" "I43\tRéseau\n" "I44\tSymétrie\n"
	"MAide\n" "I40\tNaviguer dans la vue\n";

void setSnap (double v)
{
	g_snap = v;
	for (int i = 0; i < 3; i++) ui_menu_state (45 + i, (i == 0 ? 1 : i == 1 ? 5 : 10) == v ? UMS_CHECKED : 0);
	renderView ();
	status ((std::string (TR ("Accrochage : ")) + fmtNum (v) + " mm").c_str ());
}

// les réglages de Forge, gardés d'une fois sur l'autre : HOME:/forge/forge.ini (« clé = valeur ») ; pour l'instant la
// projection de la vue (« projection = perspective » par défaut, ou « orthographic »)
const char *const SETTINGS = "HOME:/forge/forge.ini";

std::vector<std::pair<std::string, std::string>> readSettings ()
{
	std::vector<std::pair<std::string, std::string>> kv;
	FILE *f = fopen (SETTINGS, "r");
	if (!f) return kv;
	char line[512];
	while (fgets (line, sizeof line, f)) {
		std::string l = line;
		while (!l.empty () && (l.back () == '\n' || l.back () == '\r')) l.pop_back ();
		size_t e = l.find ('=');
		if (l.empty () || l[0] == '#' || e == std::string::npos) continue;
		std::string k = l.substr (0, e), v = l.substr (e + 1);
		while (!k.empty () && k.back () == ' ') k.pop_back ();
		while (!v.empty () && v[0] == ' ') v.erase (0, 1);
		kv.push_back ({ k, v });
	}
	fclose (f);
	return kv;
}

void writeSetting (const std::string &key, const std::string &val)
{
	auto kv = readSettings ();
	bool found = false;
	for (auto &e : kv)
		if (e.first == key) e.second = val, found = true;
	if (!found) kv.push_back ({ key, val });
	mkdir ("HOME:", 0755);
	mkdir ("HOME:/forge", 0755);
	FILE *f = fopen (SETTINGS, "w");
	if (!f) return;
	fprintf (f, "# Forge : les réglages (gardés d'une fois sur l'autre)\n");
	for (auto &e : kv) fprintf (f, "%s = %s\n", e.first.c_str (), e.second.c_str ());
	fclose (f);
}

void loadSettings ()
{
	for (auto &e : readSettings ())
		if (e.first == "projection") g_view.persp = g_perspPref = e.second != "orthographic";
}

// la projection de la vue : perspective ou orthographique ; save : la garder pour la prochaine fois
void setProjection (bool persp, bool save)
{
	g_view.persp = g_perspPref = persp;
	g_orthoFace = false;
	ui_menu_state (27, persp ? UMS_CHECKED : 0);
	ui_menu_state (28, persp ? 0 : UMS_CHECKED);
	if (save) writeSetting ("projection", persp ? "perspective" : "orthographic");
	cameraChanged ();
	status (persp ? TR ("Vue en perspective") : TR ("Vue orthographique"));
}

// le clic droit sur le cube de vue : la projection (le choix gardé : coché même pendant une vue de face, qui est
// orthographique pour un temps), la vue de départ, cadrer
void cubeMenu ()
{
	g_cubeMenu = false;
	std::string t;
	auto add = [&] (const char *l, bool on) {
		if (on) t += '\x02';
		t += strcmp (l, "-") ? TR (l) : l;
		t += '\0';
	};
	add ("Perspective", g_perspPref);
	add ("Orthographique", !g_perspPref);
	add ("-", false);
	add ("Vue isométrique", false);
	add ("Ajuster", false);
	t += '\0';
	long r = ui_popup_local (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), (long) t.c_str (), 220);
	if (r == 0 || r == 1) setProjection (r == 0, true);
	else if (r == 3) viewFace (6), g_view.fit (g_doc), cameraChanged ();
	else if (r == 4) g_view.fit (g_doc), cameraChanged ();
}

void mcpMenu ();

void command (int id)
{
	switch (id) {
	case 9: if (docKeepChanges ()) g_quit = true; break;
	case 1: docNew (); break;
	case 2: docOpen (); break;
	case 3: docSaveCmd (); break;
	case 4: docSaveAs (); break;
	case 13: mcpMenu (); break;
	case 10: undo (false); break;
	case 11: undo (true); break;
	case 12: stepDelete (); break;
	case 20: viewFace (3); cameraChanged (); break;
	case 21: viewFace (4); cameraChanged (); break;
	case 22: viewFace (0); cameraChanged (); break;
	case 23: viewFace (6); cameraChanged (); break;
	case 24: g_view.fit (g_doc); cameraChanged (); break;
	case 25: g_view.edges = !g_view.edges; ui_menu_state (25, g_view.edges ? UMS_CHECKED : 0); cameraChanged (); break;
	case 26: g_view.grid = !g_view.grid; ui_menu_state (26, g_view.grid ? UMS_CHECKED : 0); cameraChanged (); break;
	case 27: setProjection (true, true); break;
	case 28: setProjection (false, true); break;
	case 30: cmdStart (T_BOX); break;
	case 31: cmdStart (T_CYL); break;
	case 32: cmdStart (T_PUSH); break;
	case 33: cmdStart (T_FILLET); break;
	case 34: cmdStart (T_CHAMFER); break;
	case 35: cmdStart (T_COMBINE); break;
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
		static const int MAP[TB_N] = { 37, 30, 31, 32, 33, 34, 35, -1 };
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
	if (g_cmd.tool == T_COMBINE && (w == b_ctarget || w == b_ctools) && code == UN_ACTIVATE) {	// le choix dans la vue : la cible ou les outils
		int want = w == b_ctarget ? 1 : 2;
		if (g_cmd.pick == want) g_cmd.pick = 0;	// (le bouton repressé : le choix fini)
		else {
			if (!g_cmd.pick) g_cmd.pickBody = g_cmd.f.body, g_cmd.pickTools = g_cmd.f.tools;
			g_cmd.pick = want;
		}
		cmdRefresh ();
		status (TR (g_cmd.pick == 1 ? "Cliquez le corps cible (Échap : annuler le choix)." : g_cmd.pick == 2 ? "Cliquez les corps outils ; un outil recliqué s'en va (Échap : annuler le choix)." : "Confirmer applique l'opération."));
		return;
	}
	if (w == w_cop && code == UN_CHANGE) {
		long v = ui_w_get_value (w_cop);
		g_cmd.f.op = v == 1 ? OP_CUT : v == 2 ? OP_INTERSECT : OP_JOIN;
		cmdRefresh ();
		return;
	}
	if (w == c_keep && (code == UN_CHANGE || code == UN_ACTIVATE)) {
		g_cmd.f.keep = ui_w_get_value (c_keep) != 0;
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
	w_docsub = ui_label (0, 0, 10, 16, (long) TR ("Exemple · mm"), ULB_DIM);	// (puis le fichier : docLabels)
	w_mode = ui_segmented (0, 0, 220, h, (long) TR ("Conception|Fabrication"));
	ui_w_tip (w_mode, (long) TR ("Fabrication (G-code GRBL) : à une étape suivante"));
	TB_H = h + 24;
	w_bar = ui_toolbar (408, (TB_H - h) / 2, imax (100, (int) ui.w - 408 - 96 - 128), 0);	// (à sa place ; ui_w_move de la barre emmène ses boutons)
	static const char *const TOOLS[TB_N] = { "Esquisse", "Boîte", "Cylindre", "Pousser", "|Congé", "Chanfrein", "|Combiner", "|Mesurer" };
	static const char *const TIPS[TB_N] = { "Esquisse (S) sur la face choisie ou le plan XY : lignes, rectangles, cercles, arcs, contraintes (PlaneGCS) ; E l'extrude", "Boîte (B) : clic à un coin, clic au coin opposé, on monte, clic",
		"Cylindre : clic au centre, on s'éloigne pour le rayon, clic, on monte, clic", "Pousser/tirer (Q) une face plane", "Congé (F) sur les arêtes choisies",
		"Chanfrein (Maj+F) sur les arêtes choisies", "Combiner : une cible et des outils, en union, soustraction ou intersection", "Mesurer : à une étape suivante" };
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
	b_ctarget = ui_pushbutton (0, 0, 10, h, (long) TR ("Cible"), 0);
	ui_w_tip (b_ctarget, (long) TR ("Puis cliquez le corps cible dans la vue"));
	b_ctools = ui_pushbutton (0, 0, 10, h, (long) TR ("Outils"), 0);
	ui_w_tip (b_ctools, (long) TR ("Puis cliquez un ou plusieurs corps outils dans la vue"));
	w_cop = ui_segmented (0, 0, 10, h, (long) TR ("Union|Soustraction|Intersection"));
	c_keep = ui_checkbox (0, 0, 10, 24, (long) TR ("Conserver les outils"), 0);
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
	ui_menu_state (g_perspPref ? 27 : 28, UMS_CHECKED);	// (la projection gardée : loadSettings)
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
		if (t == WEV_CLOSE) {
			if (untilMs || docKeepChanges ()) break;	// (un document changé : l'enregistrer d'abord ?)
			continue;
		}
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
		if (g_tlMenu >= 0) timelineMenu ();
		// la vue n'a pas de widget : sa souris est lue brute (aussi hors d'elle pendant un glisser)
		int ex = ui_ev_field (&ui, EV_X) - VX, ey = ui_ev_field (&ui, EV_Y) - VY;
		bool inView = ex >= 0 && ey >= 0 && ex < VW && ey < VH;
		if (t == WEV_MOUSE) g_overView = inView;
		if (t == WEV_MOUSE && !w && (inView || g_btn)) {
			int kind = ui_ev_field (&ui, EV_KIND), b = ui_ev_field (&ui, EV_CODE) & 0xFF;
			viewMouse (kind, ex, ey, b, shift);
			if (kind == 2) g_btn = 0;
			if (g_cubeMenu) cubeMenu ();
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
	if (g_cmdDirty) g_cmdDirty = false, cmdRefresh ();	// (comme la boucle quand plus rien n'attend)
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
	// une boîte au geste, posée sur la plaque : clic à un coin, clic au coin opposé (20 × 10), on monte (5), clic
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
	check ("accrochage 5 mm : coin (45, 10, 8), 10 × 5, centre (50, 12,5)", g_cmd.corner[0] == 45 && g_cmd.corner[1] == 10 && g_cmd.corner[2] == 8
	       && g_cmd.chips.size () > 1 && g_cmd.chips[0].val == 10 && g_cmd.chips[1].val == 5 && g_cmd.f.frame.o[0] == 50 && g_cmd.f.frame.o[1] == 12.5, s);
	cmdKey (0x29, 0);
	g_snap = 1;	// (la suite : les valeurs au mm)
	double v0 = totalVolume ();
	cmdStart (T_BOX);
	double c[3] = { 35, 7, 8 }, e[3] = { 55, 17, 8 };	// (coin à coin : 20 × 10, centrée en (45, 12))
	clickAt (c);
	moveTo (e);
	clickAt (e);
	double up[3] = { 55, 17, 14 };	// (6 mm au-dessus du coin opposé : le coin suit le pointeur)
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
	// combiner : une barre de 10 × 4 × 40 à travers le bord de la boîte, soustraite en gardant l'outil (-400 mm³)
	{
		Feature f;
		f.kind = ST_BOX, f.name = featureName (g_doc, ST_BOX), f.op = OP_NEW;
		f.frame.o[0] = 200, f.frame.o[1] = 8, f.a = 10, f.b = 4, f.c = 40;
		pushUndo ();
		insertFeature (g_doc, g_doc.bar, f);
		rebuild ();
		refreshAll ();
	}
	int tbody = (int) g_doc.bodies.size () - 1;
	double vT = bodyVolume (g_doc.bodies[bx]);
	select (Pick { bx, -1, -1 });
	cmdStart (T_COMBINE);
	bool pickTools = g_cmd.pick == 2 && g_cmd.f.body == bx;
	g_cmd.f.tools = { tbody };
	g_cmd.f.op = OP_CUT, g_cmd.f.keep = true;
	cmdKey (0x28, 0);
	double cutV = vT - bodyVolume (g_doc.bodies[bx]);
	snprintf (s, sizeof s, "%s : %s ; -%s mm³ (attendu 400), outil %s", g_doc.feats.back ().name.c_str (), featureDetail (g_doc.feats.back ()).c_str (), fmtNum (cutV).c_str (),
		  g_doc.bodies[tbody].shape.IsNull () ? "consommé" : "gardé");
	check ("combiner : soustraction, outil conservé", pickTools && g_doc.feats.back ().kind == ST_COMBINE && std::fabs (cutV - 400) < 0.5 && !g_doc.bodies[tbody].shape.IsNull (), s);
	undo (false);
	cmdStart (T_COMBINE);	// Échap pendant le choix des outils : le choix annulé, la commande reste
	g_cmd.f.body = bx, g_cmd.pick = 2, g_cmd.pickBody = bx, g_cmd.pickTools.clear (), g_cmd.f.tools = { tbody };
	cmdKey (0x29, 0);
	check ("combiner : Échap annule le choix, pas la commande", g_cmd.tool == T_COMBINE && g_cmd.pick == 0 && g_cmd.f.tools.empty (), "");
	cmdKey (0x29, 0);
	// le fichier : le document enregistré puis relu et rejoué donne la même matière
	{
		const char *tmp = "/tmp/forge-selftest.forge";
		std::string e = docSave (g_doc, tmp);
		Doc d2;
		if (e.empty ()) e = docLoad (d2, tmp);
		double v2 = 0;
		if (e.empty ()) {
			replay (d2, 0.2);
			for (auto &bd : d2.bodies) v2 += bodyVolume (bd) / 1000;
		}
		snprintf (s, sizeof s, "%s%d étapes, %s -> %s", e.empty () ? "" : (e + " ; ").c_str (), (int) d2.feats.size (), cm3 (totalVolume ()).c_str (), cm3 (v2).c_str ());
		check ("enregistré puis rouvert (.forge)", e.empty () && d2.feats.size () == g_doc.feats.size () && d2.bar == g_doc.bar && std::fabs (v2 - totalVolume ()) < 1e-3, s);
		unlink (tmp);
	}
	// la suppression avec ce qui dépend : boîte, congé, boîte jointe ; la première supprimée -> la seconde seule, nouveau corps
	{
		Doc d;
		Feature f;
		f.kind = ST_BOX, f.name = "Boîte 1", f.op = OP_NEW, f.a = 20, f.b = 20, f.c = 10;
		d.feats.push_back (f);
		Feature fl;
		fl.kind = ST_FILLET, fl.name = "Congé 1", fl.body = 0, fl.a = 2, fl.edges = { EdgeRef { { 10, 10, 5 }, { 0, 0, 1 } } };
		d.feats.push_back (fl);
		Feature g = f;
		g.name = "Boîte 2", g.op = OP_JOIN, g.body = 0, g.frame.o[0] = 15, g.c = 5;
		d.feats.push_back (g);
		d.bar = 3;
		Removal plan = planRemoval (d, 0);
		applyRemoval (d, plan);
		replay (d, 0.2);
		snprintf (s, sizeof s, "%d supprimées, %d nouveau corps ; reste %d étape(s), %d corps", (int) plan.erased.size (), (int) plan.renewed.size (), (int) d.feats.size (), (int) d.bodies.size ());
		check ("supprimer une étape et ce qui en dépend", plan.erased.size () == 2 && plan.renewed.size () == 1 && d.feats.size () == 1 && d.feats[0].op == OP_NEW
		       && d.bodies.size () == 1 && std::fabs (bodyVolume (d.bodies[0]) - 20 * 20 * 5) < 0.5, s);
	}
	// la perspective (une vue à part, 400 × 300) : projection, rayon, plan, découpage au plan proche, cadrage, choix
	{
		View v;
		v.resize (400, 300);
		v.grid = false;
		v.persp = true;
		v.cam.tx = v.cam.ty = v.cam.tz = 0, v.cam.scale = 4;
		v.standard (6);
		double r[3], u[3], f[3], e[3], D = v.dist (), F = v.focal ();
		v.cam.basis (r, u, f);
		v.eye (e);
		double sx, sy, sx2, sy2, p1[3], p2[3];
		for (int i = 0; i < 3; i++) p1[i] = 10 * r[i], p2[i] = 10 * r[i] - f[i] * D / 2;	// (10 mm à droite, au point visé puis à mi-chemin de l'œil)
		v.toScreen (p1, sx, sy);
		v.toScreen (p2, sx2, sy2);
		snprintf (s, sizeof s, "œil à %.1f mm ; (%.2f, %.2f) et (%.2f, %.2f), attendus (240, 150) et (280, 150)", D, sx, sy, sx2, sy2);
		check ("perspective : projection d'un point connu (la même échelle au point visé, double à mi-chemin)",
		       std::fabs (sx - 240) < 1e-6 && std::fabs (sy - 150) < 1e-6 && std::fabs (sx2 - 280) < 1e-6 && std::fabs (sy2 - 150) < 1e-6 && std::fabs (D * 4 - F) < 1e-6, s);
		// aller-retour : le rayon du pixel où tombe un point repasse par lui (à un pixel près à sa profondeur)
		double worst = 0, wo = 0;
		bool behind = true;
		static const double PTS[4][3] = { { 12, -7, 5 }, { -30, 25, -10 }, { 3, 3, 40 }, { 0, 0, 0 } };
		for (auto &q : PTS) {
			v.toScreen (q, sx, sy);
			double o[3], dir[3], w[3];
			v.ray ((int) floor (sx), (int) floor (sy), o, dir);
			for (int i = 0; i < 3; i++) w[i] = q[i] - o[i];
			double t = w[0] * dir[0] + w[1] * dir[1] + w[2] * dir[2], d2 = 0;
			for (int i = 0; i < 3; i++) d2 += (w[i] - dir[i] * t) * (w[i] - dir[i] * t);
			worst = std::max (worst, sqrt (d2) / (t / F));	// (en pixels à cette profondeur)
			wo = std::max (wo, std::fabs (o[0] - e[0]) + std::fabs (o[1] - e[1]) + std::fabs (o[2] - e[2]));
			behind = behind && t > 0;
		}
		snprintf (s, sizeof s, "écart %.2f px, origine à %.1e mm de l'œil", worst, wo);
		check ("perspective : toScreen puis ray repasse par le point", worst < 0.75 && wo < 1e-9 && behind, s);
		// le sol sous un pixel ; un plan derrière l'œil n'est pas trouvé
		double g[3] = { 12, -7, 0 }, z0[3] = { 0, 0, 0 }, nz[3] = { 0, 0, 1 }, out[3], back[3], nf[3] = { f[0], f[1], f[2] };
		for (int i = 0; i < 3; i++) back[i] = e[i] - f[i] * 10;
		v.toScreen (g, sx, sy);
		bool okp = v.onPlane ((int) floor (sx), (int) floor (sy), z0, nz, out);
		double dg = std::hypot (out[0] - g[0], out[1] - g[1]) * F / D;
		bool behindNone = !v.onPlane (200, 150, back, nf, out);
		snprintf (s, sizeof s, "écart %.2f px ; plan derrière l'œil : %s", dg, behindNone ? "refusé" : "trouvé !");
		check ("perspective : onPlane (le sol sous le pointeur)", okp && dg < 1.5 && behindNone, s);
		// un triangle à cheval sur le plan proche (un sommet derrière l'œil) : dessiné devant seulement ; un triangle tout
		// derrière : rien (une division sans découpage le montrerait à l'envers)
		Doc none;
		std::vector<unsigned> base ((size_t) 400 * 300), img ((size_t) 400 * 300);
		v.render (base.data (), 400, none, g_pal, {}, Pick ());
		auto ghostPixels = [&] (const double tri[3][3], bool &allFront) {
			Mesh m;
			for (int k = 0; k < 3; k++)
				for (int i = 0; i < 3; i++) m.pts.push_back ((float) tri[k][i]), m.nrm.push_back (0);
			m.tris = { 0, 1, 2 };
			m.edgeFirst = { 0 };
			v.ghost = &m, v.ghostCut = true;
			v.render (img.data (), 400, none, g_pal, {}, Pick ());
			v.ghost = nullptr;
			double n[3], a[3], b[3];
			for (int i = 0; i < 3; i++) a[i] = tri[1][i] - tri[0][i], b[i] = tri[2][i] - tri[0][i];
			n[0] = a[1] * b[2] - a[2] * b[1], n[1] = a[2] * b[0] - a[0] * b[2], n[2] = a[0] * b[1] - a[1] * b[0];
			int cnt = 0;
			allFront = true;
			for (int y = 0; y < 300; y++)
				for (int x = 0; x < 400; x++)
					if (img[(size_t) y * 400 + x] != base[(size_t) y * 400 + x]) {
						cnt++;
						double o[3], dir[3];
						v.ray (x, y, o, dir);
						double dn = dir[0] * n[0] + dir[1] * n[1] + dir[2] * n[2];
						double t = dn ? ((tri[0][0] - o[0]) * n[0] + (tri[0][1] - o[1]) * n[1] + (tri[0][2] - o[2]) * n[2]) / dn : -1;
						if (t <= 0) allFront = false;
					}
			return cnt;
		};
		double cross[3][3], hidden[3][3];
		for (int i = 0; i < 3; i++) {
			cross[0][i] = e[i] - f[i] * 20 + u[i] * 40;	// (20 mm derrière l'œil, plus haut)
			cross[1][i] = 30 * r[i] - 10 * u[i];
			cross[2][i] = -30 * r[i] - 10 * u[i];
			hidden[0][i] = e[i] - f[i] * 50 + r[i] * 30;
			hidden[1][i] = e[i] - f[i] * 50 - r[i] * 30 + u[i] * 20;
			hidden[2][i] = e[i] - f[i] * 80 - u[i] * 30;
		}
		bool front1, front2;
		int n1 = ghostPixels (cross, front1), n2 = ghostPixels (hidden, front2);
		snprintf (s, sizeof s, "à cheval : %d pixels, %s ; derrière : %d pixels", n1, front1 ? "tous devant l'œil" : "certains derrière !", n2);
		check ("perspective : triangle à cheval sur le plan proche découpé, rien de ce qui est derrière l'œil", n1 > 1000 && front1 && n2 == 0, s);
		// une boîte 40 × 30 × 20 : cadrée entière dans les vues types, dans les deux projections ; puis le choix sous le
		// pointeur (le centre des faces vues, le milieu des arêtes vues)
		Doc d;
		Feature bx;
		bx.kind = ST_BOX, bx.name = "Boîte 1", bx.op = OP_NEW, bx.a = 40, bx.b = 30, bx.c = 20;
		d.feats.push_back (bx);
		d.bar = 1;
		replay (d, 0.2);
		int outside = 0, views = 0;
		for (int mode = 0; mode < 2; mode++)
			for (int k : { 6, 3, 4, 0 }) {
				v.persp = mode == 0;
				v.standard (k);
				v.fit (d);
				views++;
				for (int c = 0; c < 8; c++) {
					double q[3] = { c & 1 ? 20.0 : -20.0, c & 2 ? 15.0 : -15.0, c & 4 ? 20.0 : 0.0 };
					v.toScreen (q, sx, sy);
					outside += sx < 0 || sy < 0 || sx > 400 || sy > 300;
				}
			}
		snprintf (s, sizeof s, "%d coins hors de l'image sur %d", outside, views * 8);
		check ("cadrer (fit) : la boîte entière dans l'image, perspective et orthographique", !d.bodies.empty () && outside == 0, s);
		v.persp = true;
		v.standard (6);
		v.fit (d);
		v.render (img.data (), 400, d, g_pal, {}, Pick ());
		int facesOk = 0, facesSeen = 0, edgesOk = 0;
		if (!d.bodies.empty ()) {
			const Body &B = d.bodies[0];
			v.eye (e);
			for (int i = 0; i < B.mesh.nfaces; i++) {
				FaceRef fr;
				if (!faceRef (B, i, fr)) continue;
				double vd = (fr.pt[0] - e[0]) * fr.n[0] + (fr.pt[1] - e[1]) * fr.n[1] + (fr.pt[2] - e[2]) * fr.n[2];
				if (vd >= 0) continue;	// (de dos)
				facesSeen++;
				v.toScreen (fr.pt, sx, sy);
				Pick k = v.pick (d, (int) sx, (int) sy);
				facesOk += k.body == 0 && k.face == i;
			}
			for (int i = 0; i < B.mesh.nedges; i++) {
				EdgeRef er = edgeRef (B, i);
				v.toScreen (er.mid, sx, sy);
				Pick k = v.pick (d, (int) sx, (int) sy);
				edgesOk += k.edge == i;	// (les 3 cachées : la face devant, ou une arête vue tout près)
			}
		}
		snprintf (s, sizeof s, "faces vues %d/%d, arêtes prises à leur milieu %d/12 (attendu : les 9 vues)", facesOk, facesSeen, edgesOk);
		check ("perspective : la face et l'arête sous le pointeur", facesSeen == 3 && facesOk == 3 && edgesOk == 9, s);
		// zoomer vers le pointeur et déplacer : le point sous le pointeur (au plan du point visé) y reste
		Doc empty;
		v.render (img.data (), 400, empty, g_pal, {}, Pick ());	// (rien sous le pointeur : le plan du point visé)
		v.cam.basis (r, u, f);
		double T[3] = { v.cam.tx, v.cam.ty, v.cam.tz }, P1[3], P2[3];
		v.onPlane (300, 80, T, f, P1);
		v.zoomAt (300, 80, 1.18 * 1.18);
		v.toScreen (P1, sx, sy);
		v.cam.basis (r, u, f);
		double T2[3] = { v.cam.tx, v.cam.ty, v.cam.tz };
		v.onPlane (100, 200, T2, f, P2);
		v.panFrom (100, 200);
		v.pan (25, -15);
		v.toScreen (P2, sx2, sy2);
		snprintf (s, sizeof s, "zoom : (%.2f, %.2f) pour (300,5, 80,5) ; déplacement : (%.2f, %.2f) pour (125,5, 185,5)", sx, sy, sx2, sy2);
		check ("perspective : zoom vers le pointeur, déplacement", std::fabs (sx - 300.5) < 0.05 && std::fabs (sy - 80.5) < 0.05 && std::fabs (sx2 - 125.5) < 0.05 && std::fabs (sy2 - 185.5) < 0.05, s);
	}
	// une face du cube cliquée : orthographique pour un temps ; tourner la vue rend la perspective ; Maison aussi
	{
		g_view.standard (6);
		cameraChanged ();
		int lx[6], ly[6];
		bool vis[6];
		g_view.cubeLabels (lx, ly, vis);
		int fc = vis[3] ? 3 : 0;	// (AVANT, vue dans l'isométrique)
		viewMouse (1, lx[fc], ly[fc], 1, false);
		viewMouse (2, lx[fc], ly[fc], 0, false);
		g_btn = 0;
		bool face = !g_view.persp && g_orthoFace && g_perspPref;
		viewMouse (1, 400, 300, 2, false);
		viewMouse (0, 440, 300, 2, false);
		viewMouse (2, 440, 300, 0, false);
		g_btn = 0;
		bool orbit = g_view.persp && !g_orthoFace;
		command (22);	// (Alt+3, vue de droite)
		bool alt3 = !g_view.persp;
		command (23);	// (Alt+0, l'isométrique)
		bool iso = g_view.persp;
		snprintf (s, sizeof s, "face %s, après rotation %s, Alt+3 %s, Alt+0 %s", face ? "ortho" : "persp", orbit ? "persp" : "ortho", alt3 ? "ortho" : "persp", iso ? "persp" : "ortho");
		check ("cube de vue : une face en orthographique, tourner ou Maison rend la perspective", face && orbit && alt3 && iso, s);
		g_view.fit (g_doc);
	}
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
	std::string mcpTok, openArg;
	bool tokGiven = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--mcp")) mcpPort = i + 1 < argc && isdigit ((unsigned char) argv[i + 1][0]) ? atoi (argv[++i]) : MCP_PORT;
		else if (!strcmp (argv[i], "--mcp-token") && i + 1 < argc) mcpTok = argv[++i], tokGiven = true;
		else if (argv[i][0] != '-' && !test) openArg = argv[i];	// (un document .forge, ouvert depuis Fichiers)
	}
	if (!uikit_open (&ui)) return 1;
	{	// (sa table en dernier : elle l'emporte sur le dictionnaire commun, où Réseau, Annuler, Terminer… ont un autre sens)
		char lang[80];
		snprintf (lang, sizeof lang, "/apps/forge.app/lang/%s.txt", (const char *) ui_lang ());
		ui_tr_load ((long) lang);
	}
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
	if (!test) loadSettings ();	// (l'essai part des réglages d'origine : perspective)
	buildWindow ();
	relayout ();
	g_view.fit (g_doc);
	repaint ();
	panelUpdate ();
	hintIdle ();
	if (!openArg.empty ()) docOpenPath (openArg);
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
