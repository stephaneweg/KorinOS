//
// pocketqi/main.cpp -- PocketQI, l'assistante de Korin OS (le PocketQI de l'utilisateur, github.com/stephaneweg/PocketQI,
// porté depuis Onyx : user/Apps/pocketqi). On lui écrit une question au clavier ; un LLM (Mistral, Groq en secours, ou un
// serveur à soi), nourri des manifestes des applications installées (skills.h), répond et peut ouvrir une application ;
// sans service, la réponse vient des manifestes seuls. Pas d'écoute : ni micro ni mot d'éveil (la voix viendra plus tard).
//
//   pocketqi                  une fenêtre ordinaire
//   pocketqi --popup <x>      la bulle de la barre de menus (menubar : un clic sur l'icône de PocketQI), sous l'icône
//                             d'abscisse x ; un second clic la ferme (une seconde bulle ferme la première et s'arrête :
//                             RAM0:/pocketqi.pid)
//
// La fenêtre : le visage de Pocket (core/pocket_visage.c, ses deux yeux, aux couleurs du thème) et son état en haut, la
// conversation, le champ et Envoyer en bas ; tant que la conversation est vide, trois questions à essayer. Le fil du
// cerveau (brain.h) parle à la fenêtre par sa file d'événements, relue à chaque tour de boucle.
//
// Touches : Entrée envoie, Page préc. / Page suiv. font défiler, Échap ferme la bulle, Ctrl+N : nouvelle conversation.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uikit.h>
#include <printerkit.h>
#include "brain.h"
#include "voice.h"
#include "core/pocket_visage.h"
#define TR(s)	((const char *) ui_tr ((long) (s)))

using namespace pq;

enum { HEAD_H = 72, FOOT_H = 52, SBW = 12, PX = 13, PAD = 12, GAP = 8, BPAD = 9, BTN_W = 92, CTRL_H = 32 };
enum { POP_W = 380, POP_H = 520, WIN_W = 440, WIN_H = 600, MIN_W = 340, MIN_H = 400 };
enum { M_NEW = 1, M_SETTINGS, M_QUIT };

static struct ui_vars ui;
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE, J_BARRE, J_DOCK,
       J_TITRE_ACTIF, J_TITRE_INACTIF, J_CONTOUR, J_SOMBRE, J_MENU };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static unsigned mix (unsigned a, unsigned b, int n) { return (unsigned) ui_mix (a, b, n); }

static inline long sys_call (long n, long a = 0, long b = 0)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x8) : "memory");
	return x0;
}
enum { SYS_GETPID_ = 0x11, SYS_KILL_ = 0x14, SYS_PROCLIST_ = 0x15 };

// ---- l'état --------------------------------------------------------------------------------------------------------
enum { L_USER, L_POCKET, L_NOTE, L_ERROR };
struct Line { int kind; std::string text; };

static Brain    g_brain;
static Speaker  g_voice;			// la voix (voice.h) : les réponses dites, si réglé ou demandé
static int      g_mood;				// la dernière humeur : l'émotion de la voix
static std::vector<Line> g_lines;
static bool     g_popup;
static int      g_ww, g_wh;
static int      g_popX, g_popY;			// la bulle, sur l'écran
static long     g_vface, g_vchat, g_sbar, g_entry, g_send, g_bset;
static int      g_scroll, g_total;		// px défilés, hauteur du contenu
static bool     g_stick = true;			// suivre la dernière ligne
static std::string g_status;
static pocket_visage g_face;
static std::vector<uint32_t> g_faceImg;
static unsigned g_faceLast, g_talkUntil;
static const char *const SUGGEST[3] = { "Comment écrire une lettre ?", "Comment changer le thème du bureau ?", "Ouvre le tableur" };
static int      g_chipY[3], g_chipH[3];		// les questions à essayer (conversation vide)

static void repaint ();
static void send_text (const std::string &t);
static void print_doc (const std::string &title, const std::string &text);

static void status_from_config ()
{
	Config c;
	c.load ();
	std::string s = c.first ();
	g_status = s == "local" ? TR ("Hors ligne : réponses tirées des manifestes") : s == "url" ? TR ("Serveur local") :
		   s == "mistral" ? "Mistral" : "Groq";
}

// ---- le visage -----------------------------------------------------------------------------------------------------
static long face_paint (long view, long, long)
{
	long w = 0, h = 0;
	uint32_t *px = uikit_canvas_pixels (view, &w, &h);
	if (!px || w <= 0 || h <= 0) return 0;
	g_faceImg.resize ((size_t) (w * h));
	pocket_visage_image_argb (&g_face, g_faceImg.data (), (int) w, (int) h, (int) w);
	for (long i = 0; i < w * h; i++) px[i] = g_faceImg[(size_t) i] & 0xFFFFFF;
	// les coins arrondis, dans la couleur de la fenêtre
	const int r = 10;
	unsigned bg = g_popup ? jet (J_MENU) : jet (J_FACE);
	for (int y = 0; y < r; y++)
		for (int x = 0; x < r; x++)
		{
			int dx = r - x, dy = r - y;
			if (dx * dx + dy * dy <= r * r) continue;
			px[y * w + x] = bg; px[y * w + w - 1 - x] = bg;
			px[(h - 1 - y) * w + x] = bg; px[(h - 1 - y) * w + w - 1 - x] = bg;
		}
	return 0;
}
static void face_tick ()
{
	unsigned now = now_ms ();
	if (now - g_faceLast < 40) return;			// (25 images par seconde au plus)
	float dt = (float) (now - g_faceLast) / 1000.0f;
	g_faceLast = now;
	int etat = g_voice.talking () || (int) (g_talkUntil - now) > 0 ? VISAGE_PARLE : g_brain.running () ? VISAGE_REFLECHIT : VISAGE_VEILLE;
	if (g_face.etat != etat) pocket_visage_etat (&g_face, etat);
	pocket_visage_step (&g_face, dt > 0.25f ? 0.25f : dt);
	ui_view_dirty (g_vface);
}

// ---- la conversation -----------------------------------------------------------------------------------------------
static long font_of (bool bold) { return bold ? (long) ui.font_b : (long) ui.font; }
static int bubble_max (int width) { int w = (width - 2 * PAD) * 4 / 5; return w < 140 ? 140 : w; }

// un texte (paragraphes séparés par « \n ») mesuré dans la largeur w -> hauteur ; *tw : la largeur utile
static int measure (const std::string &t, int w, long font, int *tw)
{
	int mw = 0;
	size_t p = 0;
	while (p <= t.size () && mw < w)
	{
		size_t e = t.find ('\n', p);
		if (e == std::string::npos) e = t.size ();
		std::string par = t.substr (p, e - p);
		p = e + 1;
		int one = (int) ui_text_width ((long) par.c_str (), PX, font);
		mw = one > mw ? one : mw;
	}
	*tw = mw < w ? mw : w;
	return (int) ui_text_wrap ((long) t.c_str (), 0, 0, w, 0, font, PX, 1);	// (ui_text_wrap coupe aussi aux « \n »)
}
static void draw_text (const std::string &t, int x, int y, int w, unsigned col, long font)
{
	ui_text_wrap ((long) t.c_str (), x, y, w, col, font, PX, 0);
}

// la hauteur d'une ligne de la conversation (sans GAP)
static int line_h (const Line &l, int width, int *bw)
{
	int tw;
	if (l.kind == L_NOTE || l.kind == L_ERROR) { int h = measure (l.text, width - 2 * PAD, font_of (false), &tw); *bw = tw; return h; }
	int h = measure (l.text, bubble_max (width) - 2 * BPAD, font_of (false), &tw);
	*bw = tw + 2 * BPAD + 2;			// (+2 : ui_text_wrap coupe une ligne de largeur juste égale)
	return h + 2 * BPAD - 4;
}
static int view_h () { return g_wh - HEAD_H - FOOT_H; }
static void clamp_scroll ()
{
	int most = g_total - view_h ();
	if (most < 0) most = 0;
	if (g_stick || g_scroll > most) g_scroll = most;
	if (g_scroll < 0) g_scroll = 0;
	g_stick = g_scroll >= most;
	ui_w_set_range (g_sbar, 0, g_total > view_h () ? g_total : view_h (), view_h (), 40);
	ui_w_set_value (g_sbar, g_scroll);
}
static void relayout ()
{
	int width = g_ww - SBW, y = PAD, bw;
	for (auto &l : g_lines) y += line_h (l, width, &bw) + GAP;
	g_total = y + PAD;
	clamp_scroll ();
	ui_view_dirty (g_vchat);
}

static long chat_paint (long, long wl, long hl)
{
	int width = (int) wl, height = (int) hl;
	unsigned field = jet (J_SURFACE), text = jet (J_TEXTE1), dim = mix (field, text, 150);
	ui_fill (0, 0, width, height, field);
	if (g_lines.empty ())
	{
		// l'accueil, et trois questions à essayer
		int y = 26;
		ui_text ((long) TR ("Bonjour ! Pose-moi une question sur Korin OS"), PAD, y, text, PX, font_of (true), 0);
		ui_text ((long) TR ("ou sur une de ses applications."), PAD, y + 18, text, PX, font_of (true), 0);
		y += 42;
		for (int i = 0; i < 3; i++)
		{
			const char *s = TR (SUGGEST[i]);
			int tw = (int) ui_text_width ((long) s, PX, font_of (false));
			if (tw > width - 2 * PAD - 24) tw = width - 2 * PAD - 24;
			g_chipY[i] = y; g_chipH[i] = 30;
			ui_rbox (PAD, y, tw + 24, 30, 15, mix (field, jet (J_ACCENT), 40), mix (field, jet (J_ACCENT), 120));
			ui_text_fit ((long) s, PAD + 12, y + 20, jet (J_ACCENT_FONCE), PX, font_of (false), tw + 2);
			y += 38;
		}
		return 0;
	}
	int y = PAD - g_scroll, bw;
	for (auto &l : g_lines)
	{
		int h = line_h (l, width, &bw);
		if (y + h >= 0 && y < height)
		{
			if (l.kind == L_NOTE) draw_text (l.text, (width - bw) / 2, y, width - 2 * PAD, dim, ui_font_get (UFN_ITALIC));
			else if (l.kind == L_ERROR) draw_text (l.text, PAD, y, width - 2 * PAD, jet (J_DANGER), font_of (false));
			else if (l.kind == L_USER)
			{
				int x = width - PAD - bw;
				unsigned bg = jet (J_ACCENT);			// la bulle de l'utilisateur : l'accent, texte blanc
				ui_rbox (x, y, bw, h, 10, bg, bg);
				draw_text (l.text, x + BPAD, y + BPAD - 3, bw - 2 * BPAD, 0xFFFFFF, font_of (false));
			}
			else
			{
				unsigned bg = jet (J_ELEVE);
				ui_rbox (PAD, y, bw, h, 10, bg, jet (J_LIGNE));
				draw_text (l.text, PAD + BPAD, y + BPAD - 3, bw - 2 * BPAD, text, font_of (false));
			}
		}
		y += h + GAP;
	}
	return 0;
}
static long chat_mouse (long, long kind, long, long y, long)
{
	if (kind != 1 || !g_lines.empty () || g_brain.running ()) return 0;
	for (int i = 0; i < 3; i++)
		if (y >= g_chipY[i] && y < g_chipY[i] + g_chipH[i]) { send_text (TR (SUGGEST[i])); break; }
	return 0;
}
static void scroll_by (int dy)
{
	g_stick = false;
	g_scroll += dy;
	clamp_scroll ();
	ui_view_dirty (g_vchat);
}
static long chat_wheel (long, long n) { scroll_by ((int) -n * 40); return 0; }

static void add_line (int kind, const std::string &t)
{
	g_lines.push_back (Line { kind, t });
	g_stick = true;
	relayout ();
}

// ---- envoyer -------------------------------------------------------------------------------------------------------
static void send_text (const std::string &t0)
{
	std::string t = trim (t0);
	if (t.empty () || g_brain.running ()) return;
	g_voice.stop ();
	if (!g_brain.ask (t)) return;
	add_line (L_USER, t);
	ui_w_set_text (g_entry, (long) "");
	ui_w_focus (g_entry);
	g_status = TR ("Je réfléchis...");
	repaint ();
}
static void pump ()
{
	bool any = false;
	std::string verr = g_voice.error ();
	if (!verr.empty ()) add_line (L_ERROR, TR ("Voix : ") + verr);
	for (auto &e : g_brain.take ())
	{
		switch (e.kind)
		{
		case BE_SAY:
			add_line (L_POCKET, e.text);
			g_talkUntil = now_ms () + 1500;
			{
				Config c;
				c.load ();
				if (c.voice || g_brain.aloud_asked ()) g_voice.say (e.text, g_brain.english (), g_mood);
			}
			break;
		case BE_NOTE: add_line (L_NOTE, e.text); break;
		case BE_ERROR: add_line (L_ERROR, e.text); break;
		case BE_MOOD: pocket_visage_humeur (&g_face, e.mood, e.intensity); g_mood = e.mood; break;
		case BE_PRINT: repaint (); print_doc (e.text, e.body); any = true; break;
		case BE_DONE: status_from_config (); any = true; break;
		}
	}
	if (any) repaint ();
}

// ---- imprimer (ce qu'un outil a demandé : notes, agenda, liste de courses) ---------------------------------------------
// la fenêtre d'impression du système, puis le texte en pages : le titre en gras, les lignes coupées à la largeur
static void print_doc (const std::string &title, const std::string &text)
{
	if (!printerkit_kit_open ()) { add_line (L_ERROR, TR ("Le service d'impression n'est pas installé.")); return; }
	PrintSetup ps;
	print_setup_default ((long) &ps);
	PrintDialogInfo di = { sizeof di, title.c_str (), 0, 0, 0, 0, 0 };
	if (g_popup) ui_dialog_at (g_popX + 4, g_popY + 40);		// (au-dessus de la bulle, qui est au-dessus de tout)
	bool go = print_dialog ((long) &ps, (long) &di);
	ui_w_draw (0);
	if (!go) { add_line (L_NOTE, TR ("Impression annulée")); return; }
	long j = print_begin ((long) &ps, (long) title.c_str ());
	if (!j) { add_line (L_ERROR, TR ("La file d'impression ne peut pas être écrite.")); return; }
	long f = print_font (j, (long) "DejaVu Sans", 0), fb = print_font (j, (long) "DejaVu Sans", PRINT_BOLD);
	if (fb < 0) fb = f;
	const long M = print_mpt (54), SZ = print_mpt (11), TSZ = print_mpt (16), LH = print_mpt (15.5f);
	long ml = ps.margin_l > M ? ps.margin_l : M, mt = ps.margin_t > M ? ps.margin_t : M;
	long mr = ps.margin_r > M ? ps.margin_r : M, mb = ps.margin_b > M ? ps.margin_b : M;
	long w = ps.paper_w - ml - mr, bottom = ps.paper_h - mb;
	// les lignes à imprimer, coupées aux mots
	std::vector<std::string> lines;
	size_t p = 0;
	while (p <= text.size ())
	{
		size_t e = text.find ('\n', p);
		if (e == std::string::npos) e = text.size ();
		std::string par = text.substr (p, e - p), cur;
		p = e + 1;
		size_t q = 0;
		while (q < par.size ())
		{
			size_t sp = par.find (' ', q);
			if (sp == std::string::npos) sp = par.size ();
			std::string wd = par.substr (q, sp - q), tryit = cur.empty () ? wd : cur + " " + wd;
			q = sp + 1;
			if (!cur.empty () && print_text_width (j, f, SZ, (long) tryit.c_str ()) > w) { lines.push_back (cur); cur = wd; }
			else cur = tryit;
		}
		lines.push_back (cur);
		if (e == text.size ()) break;
	}
	size_t li = 0;
	int page = 0;
	while (li < lines.size () || page == 0)
	{
		page++;
		bool on = print_page (j, 0, 0) != 0;		// (hors des pages choisies : rien n'est dessiné, on avance quand même)
		long y = mt;
		if (page == 1)
		{
			y += TSZ;
			if (on) print_text (j, fb, TSZ, ml, y, (long) title.c_str (), 0x000000);
			y += LH;
		}
		for (; li < lines.size () && y + LH <= bottom; li++)
		{
			y += LH;
			if (on && !lines[li].empty ()) print_text (j, f, SZ, ml, y, (long) lines[li].c_str (), 0x000000);
		}
		if (on)
		{
			std::string n = std::to_string (page);
			long tw = print_text_width (j, f, print_mpt (9), (long) n.c_str ());
			print_text (j, f, print_mpt (9), (ps.paper_w - tw) / 2, ps.paper_h - mb / 2, (long) n.c_str (), 0x707070);
		}
	}
	if (print_end (j) < 0) add_line (L_ERROR, TR ("Le document n'a pas pu être mis dans la file d'impression."));
	else add_line (L_NOTE, TR ("Envoyé à l'impression : ") + title);
}

// ---- les réglages --------------------------------------------------------------------------------------------------
static void settings ()
{
	Config c;
	c.load ();
	const int TOP = g_popup ? 16 : 40;			// (dans la bulle, le dialogue est sans cadre : pas de titre)
	const int W = 480, H = 520 - 40 + TOP, LX = 16, FX = 170, FW = W - FX - 16;
	if (g_popup) ui_dialog_at (g_popX + g_ww - W > 4 ? g_popX + g_ww - W : 4, g_popY + 40);	// (au-dessus de la bulle, qui est au-dessus de tout)
	long d = ui_dialog_begin (W, H, (long) TR ("Réglages de PocketQI"));
	if (!d) return;
	int y = TOP;
	ui_label (LX, y, W - 32, 22, (long) TR ("Qui répond aux questions :"), ULB_BOLD);
	y += 26;
	static const char *const P[4] = { "mistral", "groq", "url", "local" };
	const char *const L[4] = { TR ("Mistral (clé d'API)"), TR ("Groq (clé d'API)"), TR ("Un serveur compatible OpenAI (adresse)"),
				  TR ("Personne : seulement les manifestes, hors ligne") };
	long r[4];
	std::string cur = c.first ();
	for (int i = 0; i < 4; i++) { r[i] = ui_radio (LX + 8, y, W - 48, 24, (long) L[i], 1); if (cur == P[i]) ui_w_set_value (r[i], 1); y += 26; }
	y += 8;
	struct { const char *label; std::string *val; long flags; long w; } F[6] = {
		{ "Clé Mistral", &c.mistralKey, UTF_PASSWORD, 0 }, { "Clé Groq", &c.groqKey, UTF_PASSWORD, 0 },
		{ "Adresse du serveur", &c.url, 0, 0 }, { "Clé du serveur", &c.urlKey, UTF_PASSWORD, 0 },
		{ "Modèle (facultatif)", &c.model, 0, 0 }, { "Ville (météo)", &c.city, 0, 0 } };
	for (auto &f : F)
	{
		ui_label (LX, y + 5, FX - LX - 8, 22, (long) TR (f.label), 0);
		f.w = ui_textbox (FX, y, FW, CTRL_H - 2, f.flags);
		ui_w_set_text (f.w, (long) f.val->c_str ());
		y += CTRL_H + 6;
	}
	ui_textbox_hint (F[2].w, (long) "https://.../v1/chat/completions");
	ui_textbox_hint (F[5].w, (long) TR ("quand aucune ville n'est nommée"));
	long cv = ui_checkbox (LX, y, W - 32, 24, (long) TR ("Lire les réponses à voix haute (voix de Mistral, clé Mistral)"), 0);
	ui_w_set_value (cv, c.voice ? 1 : 0);
	y += 30;
	ui_label (LX, y + 2, W - 32, 36, (long) TR ("Les clés restent sur la carte (SD0:/home/pocketqi) et ne partent que vers leur service."), ULB_DIM | ULB_WRAP);
	long e = ui_dialog_button (d, 0, (long) "OK", 1, UB_DEFAULT, W - 12);
	ui_dialog_button (d, 1, (long) "Annuler", 0, 0, e);
	ui_dialog_cancel (d, 0);
	int res = (int) ui_dialog_run (d, 0, 0);
	if (res == 1)
	{
		for (int i = 0; i < 4; i++) if (ui_w_get_value (r[i])) c.provider = P[i];
		for (auto &f : F) *f.val = trim ((const char *) ui_w_get_text (f.w));
		c.voice = ui_w_get_value (cv) != 0;
		if (!c.voice) g_voice.stop ();
	}
	for (auto &f : F) ui_w_set_text (f.w, (long) "");		// (les clés ne restent pas dans les champs)
	ui_dialog_end (d);
	ui_w_draw (0);
	if (res == 1 && !c.save ()) ui_message ((long) "PocketQI", (long) TR ("Les réglages n'ont pas pu être enregistrés."), 0);
	status_from_config ();
	repaint ();
	ui_w_focus (g_entry);				// (la question suivante se tape tout de suite)
}

// ---- la fenêtre ----------------------------------------------------------------------------------------------------
static void lay_out ()
{
	int w = g_ww, h = g_wh;
	ui_w_move (g_vface, PAD, 8, 104, HEAD_H - 16);
	if (g_bset) ui_w_move (g_bset, w - PAD - 84, (HEAD_H - 28) / 2, 84, 28);
	ui_w_move (g_vchat, 0, HEAD_H, w - SBW, h - HEAD_H - FOOT_H);
	ui_w_move (g_sbar, w - SBW, HEAD_H, SBW, h - HEAD_H - FOOT_H);
	int y = h - FOOT_H + (FOOT_H - CTRL_H) / 2;
	ui_w_move (g_entry, PAD, y, w - 2 * PAD - BTN_W - GAP, CTRL_H);
	ui_w_move (g_send, w - PAD - BTN_W, y, BTN_W, CTRL_H);
	relayout ();
}
static void repaint ()
{
	unsigned bg = g_popup ? jet (J_MENU) : jet (J_FACE);
	if (g_popup) { ui_ombre (g_ww, g_wh); ui_rbox (4, 4, g_ww - 8, g_wh - 8, 8, bg, bg); }
	else ui_fill (0, 0, g_ww, g_wh, bg);
	// le nom et l'état, à droite du visage ; la fenêtre laisse libres les boutons du cadre (FRAME_CAP_W)
	int x = PAD + 104 + 12, right = g_popup ? g_ww - PAD - 84 - 8 : g_ww - FRAME_CAP_W - 8;
	if (right > x + 20)
	{
		ui_text ((long) "PocketQI", x, HEAD_H / 2 - 4, jet (J_TEXTE1), 15, font_of (true), right - x);
		ui_text_fit ((long) g_status.c_str (), x, HEAD_H / 2 + 14, jet (J_TEXTE2), 12, font_of (false), right - x);
	}
	ui_fill (0, HEAD_H - 1, g_ww, 1, jet (J_LIGNE));
	ui_fill (0, g_wh - FOOT_H, g_ww, 1, jet (J_LIGNE));
	lay_out ();
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();
}
static void on_resize (long w, long h) { g_ww = (int) w; g_wh = (int) h; repaint (); }

static void build_menu ()
{
	static char t[256];
	snprintf (t, sizeof t, "MPocketQI\nI%d\tNouvelle conversation\tCtrl+N\nI%d\tR\xC3\xA9glages...\n-\nI%d\t%s\n", M_NEW, M_SETTINGS, M_QUIT,
		  g_popup ? "Fermer" : "Quitter");
	ui_menu ((long) t);
}
static bool command (long id)
{
	switch (id)
	{
	case M_NEW: if (!g_brain.running ()) { g_voice.stop (); g_lines.clear (); g_brain.forget (); relayout (); repaint (); } break;
	case M_SETTINGS: settings (); break;
	case M_QUIT: return false;
	}
	return true;
}
static void control (long w, long code)
{
	if (code == UN_ACTIVATE && (w == g_send || w == g_entry)) send_text ((const char *) ui_w_get_text (g_entry));
	else if (code == UN_ACTIVATE && w == g_bset) settings ();
	else if (w == g_sbar && code == UN_CHANGE) { g_scroll = (int) ui_w_get_value (g_sbar); g_stick = false; clamp_scroll (); ui_view_dirty (g_vchat); }
}

// ---- la bulle : une seule à la fois ---------------------------------------------------------------------------------
// une autre bulle vivante : fermée -> true (celle-ci s'arrête aussi : c'était le second clic)
static bool other_popup ()
{
	static const char *const PID = "RAM0:/pocketqi.pid";
	std::string t;
	long me = sys_call (SYS_GETPID_);
	if (read_file (PID, t))
	{
		long other = atol (t.c_str ());
		static unsigned char p[64 * 64];
		long n = sys_call (SYS_PROCLIST_, (long) p, 64);
		for (long i = 0; i < n && other > 0 && other != me; i++)
		{
			unsigned char *e = p + i * 64;
			if (*(int *) e == other && !strcmp ((char *) e + 24, "pocketqi"))
			{
				sys_call (SYS_KILL_, other);
				unlink (PID);
				return true;
			}
		}
	}
	int f = open (PID, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (f >= 0) { std::string s = std::to_string (me); write (f, s.data (), s.size ()); close (f); }
	return false;
}

int main (int argc, char **argv)
{
	int iconX = -1;
	for (int i = 1; i < argc; i++)
		if (!strcmp (argv[i], "--popup")) { g_popup = true; if (i + 1 < argc) iconX = atoi (argv[++i]); }
	if (!uikit_open (&ui)) return 1;
	if (g_popup && other_popup ()) return 0;
	s_jet = (const unsigned *) ui_jetons ();
	audiokit_kit_open ();
	g_brain.set_english (!strcmp ((const char *) ui_lang (), "en"));
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	long win;
	if (g_popup)
	{
		// sous la barre de menus, aligné sur l'icône, dans l'écran (comme soundmenu)
		int w = POP_W, h = POP_H;
		if (h > scr[1] - scr[3] - 8) h = (int) (scr[1] - scr[3] - 8);
		int x = iconX >= 0 ? iconX + 24 - w : (int) scr[0] - w - 4;
		if (x > scr[0] - w - 4) x = (int) scr[0] - w - 4;
		if (x < 4) x = 4;
		g_popX = x; g_popY = (int) scr[3] + 2;
		win = ui_window_ex (x, scr[3] + 2, w, h, (long) "PocketQI", WF_FRAMELESS | WF_TOP | WF_NOLIST | WF_ALPHA);
	}
	else
	{
		int h = WIN_H;
		if (h > scr[1] - scr[2] - scr[3] - 16) h = (int) (scr[1] - scr[2] - scr[3] - 16);
		win = ui_window_ex (-1, -1, WIN_W, h, (long) "PocketQI", WF_RESIZABLE);
		ui_min_size (MIN_W, MIN_H);
	}
	if (win <= 0) return 1;
	g_ww = (int) ui.w; g_wh = (int) ui.h;

	pocket_visage_init (&g_face, now_ms () * 2654435761u + 1);
	g_face.couleur = jet (J_ACCENT);
	g_face.fond = jet (J_SOMBRE);
	g_faceLast = now_ms ();

	g_vface = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_vface, (long) face_paint, 0, 0, 0);
	g_vchat = ui_view (0, 0, 10, 10, 0);			// (sans focus : le champ le garde, Page préc. / suiv. passent par la boucle)
	ui_view_on (g_vchat, (long) chat_paint, (long) chat_mouse, 0, (long) chat_wheel);
	g_sbar = ui_scrollbar (0, 0, SBW, 10, 0);
	g_entry = ui_textbox (0, 0, 10, CTRL_H, 0);
	ui_textbox_hint (g_entry, (long) TR ("Écris ta question..."));
	g_send = ui_pushbutton (0, 0, BTN_W, CTRL_H, (long) "Envoyer", UB_DEFAULT);
	if (g_popup) g_bset = ui_pushbutton (0, 0, 84, 28, (long) TR ("Réglages"), UB_GHOST);
	if (!g_popup) ui_on_resize ((long) on_resize, 0);
	build_menu ();
	status_from_config ();
	repaint ();
	ui_w_focus (g_entry);

	bool run = true;
	while (run)
	{
		long t = ui_event (40);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) run = command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			long w2 = uikit_w_event (&code, &taken);
			if (code) control (w2, code);
			if (press && !taken)
			{
				int k = ui_ev_field (&ui, EV_CODE);
				long id = ui_menu_key (0);
				if (id >= 0) run = command (id);
				else if (k == KEY_ESC && g_popup) run = false;
				else if (k == 0x4B) scroll_by (-view_h () + 40);	// Page préc.
				else if (k == 0x4E) scroll_by (view_h () - 40);		// Page suiv.
			}
		}
		else ui_w_idle ();
		pump ();
		face_tick ();
		ui_view_flush ();
	}
	g_brain.stop ();
	g_voice.stop ();
	for (int i = 0; i < 50 && g_voice.talking (); i++) usleep (20000);	// (la sortie du son refermée avant de partir)
	if (g_popup) unlink ("RAM0:/pocketqi.pid");
	return 0;
}
