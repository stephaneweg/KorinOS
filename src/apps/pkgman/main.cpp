//
// pkgman -- Logiciels, le gestionnaire de paquets d'ARMPI (docs/PAQUETS.md), port du pkgman d'Onyx sur UIKit :
//
//   Mises a jour   les paquets dont le depot a une version plus recente : une case chacun (Installer N), le systeme
//                  marque "redemarrage" ; Verifier relit l'index.
//   Installes      chaque paquet installe : sa version, son mode de mise a jour (Manuel / Auto / Jamais) et Retirer
//                  (pas le systeme).
//   Disponibles    les paquets du depot pas encore installes, ou avec une mise a jour : Installer.
//
// Le champ de recherche filtre les trois. Un travail (l'index relu, des installations, un retrait) tourne dans un
// thread, la fenetre vivante : sa progression au pied de la fenetre et sur la ligne du paquet ; un travail a la fois.
// Une mise a jour du systeme s'etage : un bandeau propose de redemarrer (`pkg commit` la met en place au demarrage).
// Au lancement le dernier index (SD0:/var/pkg/index.txt) s'affiche aussitot, puis il est relu au depot.
//
#include "pkglib.h"
#include <pthread.h>
#include <math.h>
#include "uikit.h"

using namespace pkg;

enum { ROWH = 58, FOOT = 46, TOPH = 46, ICON = 32, BANNER = 64, CAP = FRAME_CAP_W + 8, PILLS = 186 };
enum { T_UPDATES, T_INSTALLED, T_AVAILABLE };
enum { ST_NONE, ST_WAIT, ST_WORK, ST_DONE, ST_STAGED, ST_FAILED };

static struct ui_vars ui;
static const unsigned *s_jet;
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned bg) { return (unsigned) ui_ink (bg) & 0xFFFFFF; }
#define C_FACE		jet (J_FACE)
#define C_FIELD		jet (J_SURFACE)
#define C_TEXT		jet (J_TEXTE1)
#define C_DIM		jet (J_TEXTE2)
#define C_FAINT		jet (J_TEXTE3)
#define C_LINE		jet (J_LIGNE)
#define C_ACCENT	jet (J_ACCENT)
#define C_OK		jet (J_SUCCES)
#define C_BAD		jet (J_DANGER)
#define C_WARN		jet (J_ALERTE)

static inline void rbox (int x, int y, int w, int h, int r, unsigned c) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r, c, c); }
static inline long face (int bold) { return bold ? (long) ui.font_b : (long) ui.font; }
static inline int tw (const char *s, int px = 13, int bold = 0) { return (int) ui_text_width ((long) s, px, face (bold)); }
static inline void text (int x, int y, const char *s, unsigned c, int px = 13, int bold = 0, int w = 4000) { ui_text_fit ((long) s, x, y + px, c, px, face (bold), w); }

// ---- le travail, dans un thread (pkgjob.h d'Onyx) --------------------------------------------------------------------------
enum { J_CHECK, J_INSTALL, J_REMOVE, J_MODE };
struct Job
{
	int kind; char names[64][40]; int n; char mode[8];
	volatile bool running, done, cancel;
	pthread_t tid;
	char cur[40]; volatile int pct; char msg[200]; volatile int rc;
	char failed[64][40]; int nfailed;
	char finished[96][40]; volatile int nfinished;
};
static Job g_job;
static Manager *g_M;

struct JobReport : Report
{
	void say (const char *s) override { cpy (g_job.msg, s, sizeof g_job.msg); }
	void step (const char *, u64 done, u64 total) override
	{
		g_job.pct = total ? (int) (done * 100 / total) : 0;
		if (g_job.pct > 100) g_job.pct = 100;
	}
	bool cancelled () override { return g_job.cancel; }
};
static void *job_thread (void *)
{
	JobReport r;
	Manager &m = *g_M;
	int rc = OK;
	if (g_job.kind == J_CHECK)
	{
		cpy (g_job.cur, "index", sizeof g_job.cur);
		rc = m.refresh (r);
	}
	else if (g_job.kind == J_INSTALL)
	{
		const Pkg *list[96]; int n = 0;
		for (int i = 0; i < g_job.n && !g_job.cancel; i++)
		{
			Inst *in = m.db.find (g_job.names[i]);
			const Pkg *p = m.index.find (g_job.names[i]);
			if (!p) continue;
			if (in) { list[n++] = p; continue; }
			int err = 0; const Pkg *sub[32];
			int k = m.resolve (g_job.names[i], sub, 32, &err, r);
			for (int j = 0; j < k && n < 96; j++) { bool dup = false; for (int q = 0; q < n; q++) if (list[q] == sub[j]) dup = true; if (!dup) list[n++] = sub[j]; }
			if (!k) { rc = err; if (g_job.nfailed < 64) cpy (g_job.failed[g_job.nfailed++], g_job.names[i], 40); }
		}
		for (int i = 1; i < n; i++)			// les apps d'abord, le systeme a la fin
			for (int j = i; j > 0 && list[j - 1]->restart && !list[j]->restart; j--) { const Pkg *t = list[j]; list[j] = list[j - 1]; list[j - 1] = t; }
		for (int i = 0; i < n && !g_job.cancel; i++)
		{
			cpy (g_job.cur, list[i]->name, sizeof g_job.cur); g_job.pct = 0;
			int x = m.install (*list[i], r);
			if (x != OK) { rc = x; if (g_job.nfailed < 64) cpy (g_job.failed[g_job.nfailed++], list[i]->name, 40); }
			if (g_job.nfinished < 96) { cpy (g_job.finished[g_job.nfinished], list[i]->name, 40); g_job.nfinished = g_job.nfinished + 1; }
		}
		if (g_job.cancel && rc == OK) rc = E_CANCEL;
	}
	else if (g_job.kind == J_REMOVE)
	{
		for (int i = 0; i < g_job.n && !g_job.cancel; i++)
		{
			cpy (g_job.cur, g_job.names[i], sizeof g_job.cur);
			int x = m.remove (g_job.names[i], false, r);
			if (x != OK) rc = x;
		}
	}
	else if (g_job.kind == J_MODE)
	{
		if (!m.set_mode (g_job.names[0], g_job.mode)) rc = E_NOTINST;
	}
	m.refresh_desktop ();
	g_job.rc = rc;
	g_job.cur[0] = 0;
	g_job.done = true;
	return 0;
}
static bool job_start (int kind, const char *const *names, int n, const char *mode = 0)
{
	if (g_job.running) return false;
	memset (&g_job, 0, sizeof g_job);
	g_job.kind = kind; g_job.running = true;
	for (int i = 0; i < n && i < 64; i++) cpy (g_job.names[g_job.n++], names[i], 40);
	if (mode) cpy (g_job.mode, mode, sizeof g_job.mode);
	if (pthread_create (&g_job.tid, 0, job_thread, 0) != 0) { g_job.running = false; return false; }
	return true;
}

// ---- ce qui est montre : une image de l'index et de la base (refaite entre deux travaux) ---------------------------------------
struct Row
{
	char name[40], title[48], summary[120], category[24], inst[24], avail[24], mode[8];
	u64 size, installed;
	bool isInst, hasUpdate, required, restart, staged, checked;
	bool markInst, markAvail;		// cochee dans Installes (retirer) / Disponibles (installer) ; checked : Mises a jour
	int state, pct;				// pendant un travail
};
static Row *g_rows; static int g_nrows;
static int g_tab = T_UPDATES;
static int g_vis[512], g_nvis;
static char g_search[64];
static char g_status[200];
static char g_indexDate[24];
static bool g_verified, g_haveIndex;

static bool contains_ci (const char *h, const char *n)
{
	if (!*n) return true;
	for (; *h; h++)
	{
		int i = 0;
		while (n[i] && h[i] && (h[i] | 32) == (n[i] | 32)) i++;
		if (!n[i]) return true;
	}
	return false;
}
static void size_text (u64 b, char *out, int cap)
{
	if (b >= 1048576) snprintf (out, (size_t) cap, TR ("%llu,%llu Mo"), b / 1048576, (b % 1048576) * 10 / 1048576);
	else snprintf (out, (size_t) cap, TR ("%llu Ko"), (b + 1023) / 1024);
}
static void snapshot (void)
{
	Manager &m = *g_M;
	free (g_rows); g_rows = 0; g_nrows = 0;
	int cap = m.index.n + m.db.n + 1;
	g_rows = (Row *) calloc ((size_t) cap, sizeof (Row));
	auto add = [&] (const char *name) -> Row &
	{
		Row &r = g_rows[g_nrows++];
		cpy (r.name, name, sizeof r.name); cpy (r.title, name, sizeof r.title); cpy (r.mode, "manual", sizeof r.mode);
		return r;
	};
	for (int i = 0; i < m.db.n; i++)
	{
		Inst &in = *m.db.v[i];
		Row &r = add (in.name);
		cpy (r.title, in.ini.get ("package", "title", in.name), sizeof r.title);
		cpy (r.summary, in.ini.get ("package", "summary"), sizeof r.summary);
		cpy (r.category, in.ini.get ("package", "category"), sizeof r.category);
		cpy (r.inst, in.version (), sizeof r.inst); cpy (r.mode, in.mode (), sizeof r.mode);
		r.isInst = true; r.required = in.required (); r.staged = m.staged (in.name);
		r.restart = eq (in.ini.get ("package", "restart", "0"), "1");
	}
	for (int i = 0; i < m.index.n; i++)
	{
		const Pkg &p = m.index.p[i];
		Row *r = 0;
		for (int k = 0; k < g_nrows; k++) if (eq (g_rows[k].name, p.name)) r = &g_rows[k];
		if (!r) r = &add (p.name);
		cpy (r->title, p.title, sizeof r->title);
		if (p.summary[0]) cpy (r->summary, p.summary, sizeof r->summary);
		cpy (r->category, p.category, sizeof r->category);
		cpy (r->avail, p.version, sizeof r->avail);
		r->size = p.size; r->installed = p.installed; r->restart = p.restart; r->required = p.required || r->required;
		r->hasUpdate = r->isInst && vcmp (p.version, r->inst) > 0 && !r->staged;
		r->checked = r->hasUpdate && !eq (r->mode, "never");
	}
	g_haveIndex = m.haveIndex; g_verified = m.verified; cpy (g_indexDate, m.index.date, sizeof g_indexDate);
}
static bool shown (const Row &r, int tab)
{
	if (tab == T_UPDATES && !r.hasUpdate) return false;
	if (tab == T_INSTALLED && !r.isInst) return false;
	if (tab == T_AVAILABLE && ((r.isInst && !r.hasUpdate) || !r.avail[0])) return false;
	return contains_ci (r.name, g_search) || contains_ci (r.title, g_search) || contains_ci (r.category, g_search) || contains_ci (r.summary, g_search);
}
static void refilter (void)
{
	g_nvis = 0;
	for (int i = 0; i < g_nrows && g_nvis < 512; i++) if (shown (g_rows[i], g_tab)) g_vis[g_nvis++] = i;
	if (g_tab == T_UPDATES)						// le systeme d'abord
		for (int i = 1; i < g_nvis; i++) for (int j = i; j > 0 && g_rows[g_vis[j]].required && !g_rows[g_vis[j - 1]].required; j--) { int t = g_vis[j]; g_vis[j] = g_vis[j - 1]; g_vis[j - 1] = t; }
}
static int count (int tab) { int n = 0; for (int i = 0; i < g_nrows; i++) if (shown (g_rows[i], tab)) n++; return n; }
static bool can_mark (const Row &r) { return g_tab == T_INSTALLED ? !r.required : r.state == ST_NONE; }
static bool &mark_of (Row &r) { return g_tab == T_UPDATES ? r.checked : g_tab == T_INSTALLED ? r.markInst : r.markAvail; }
static int marked (const char **names, int cap)
{
	int n = 0;
	for (int i = 0; i < g_nrows; i++)
	{
		const Row &r = g_rows[i];
		bool on = g_tab == T_UPDATES ? r.hasUpdate && r.checked
			: g_tab == T_INSTALLED ? r.isInst && !r.required && r.markInst
			: r.markAvail && r.avail[0] && (!r.isInst || r.hasUpdate);
		if (!on) continue;
		if (names && n < cap) names[n] = r.name;
		n++;
	}
	return names && n > cap ? cap : n;
}
static bool all_marked (void)
{
	int can = 0;
	for (int i = 0; i < g_nvis; i++) { Row &r = g_rows[g_vis[i]]; if (!can_mark (r)) continue; can++; if (!mark_of (r)) return false; }
	return can > 0;
}
static bool any_staged (void) { for (int i = 0; i < g_nrows; i++) if (g_rows[i].staged) return true; return false; }
static Row *row_of (const char *name) { for (int i = 0; i < g_nrows; i++) if (eq (g_rows[i].name, name)) return &g_rows[i]; return 0; }

// ---- la fenetre -----------------------------------------------------------------------------------------------------------------
static long g_tabs, g_find, g_list, g_bar, g_foot, g_bCheck, g_bMain, g_bAll;
static int g_ww, g_wh, g_lw, g_lh, s_top, s_hot = -1, s_hotPart;
static bool s_down, s_scroll; static int s_pressY, s_pressTop, s_pressRow, s_pressPart;
static struct { int row, part; bool on; } g_later;
static char s_tabText[80]; static int s_tabW = 400;

static int banner () { return any_staged () && g_tab == T_UPDATES ? BANNER : 0; }
static int rows_h () { return banner () + g_nvis * ROWH + 8; }
static void clamp ()
{
	int mx = rows_h () - g_lh; if (mx < 0) mx = 0;
	if (s_top > mx) s_top = mx;
	if (s_top < 0) s_top = 0;
}
static void bar_sync ()
{
	clamp ();
	bool on = rows_h () > g_lh;
	ui_w_show (g_bar, on);
	if (on) { ui_w_set_range (g_bar, 0, rows_h (), g_lh, ROWH); ui_w_set_value (g_bar, s_top); }
}
// (les segments se lisent a la creation : de nouveaux nombres, un nouveau controle)
static void layout (int w, int h);
static void tabs_labels ()
{
	char t[80], p[3][32];
	snprintf (p[0], 32, TR ("Mises à jour (%d)"), count (T_UPDATES));
	snprintf (p[1], 32, TR ("Installés (%d)"), count (T_INSTALLED));
	snprintf (p[2], 32, TR ("Disponibles (%d)"), count (T_AVAILABLE));
	snprintf (t, sizeof t, "%s|%s|%s", p[0], p[1], p[2]);
	if (eq (t, s_tabText)) return;
	cpy (s_tabText, t, sizeof s_tabText);
	if (g_tabs) ui_w_destroy (g_tabs);
	g_tabs = ui_segmented (12, 8, 0, 30, (long) s_tabText);
	ui_w_set_value (g_tabs, g_tab);
	s_tabW = 4; for (int i = 0; i < 3; i++) s_tabW += tw (p[i], 13, 1) + 24;
	if (g_ww) { layout (g_ww, g_wh); ui_w_draw (0); }
}
static void set_status (const char *s) { cpy (g_status, TR (s), sizeof g_status); ui_view_dirty (g_foot); }
static void status_idle (void)
{
	char t[200];
	if (!g_haveIndex) cpy (t, TR ("Le dépôt n'a pas encore été lu."), sizeof t);
	else snprintf (t, sizeof t, TR ("%d installés  -  index du %s%s"), count (T_INSTALLED), g_indexDate, TR (g_verified ? ", signature bonne" : " (le dernier lu)"));
	set_status (t);
}
static void update_buttons (void)
{
	int n = marked (0, 0);
	static char t[48];
	if (g_tab == T_UPDATES) { if (n) snprintf (t, sizeof t, TR (n > 1 ? "Installer %d mises à jour" : "Installer %d mise à jour"), n); else cpy (t, TR ("Installer les mises à jour"), sizeof t); }
	else if (g_tab == T_INSTALLED) { if (n) snprintf (t, sizeof t, TR (n > 1 ? "Retirer %d paquets" : "Retirer %d paquet"), n); else cpy (t, TR ("Retirer"), sizeof t); }
	else { if (n) snprintf (t, sizeof t, TR (n > 1 ? "Installer %d paquets" : "Installer %d paquet"), n); else cpy (t, TR ("Installer"), sizeof t); }
	ui_w_set_text (g_bMain, (long) t);
	ui_w_set_text (g_bAll, (long) TR (all_marked () ? "Aucun" : "Tous"));
	ui_w_enable (g_bAll, !g_job.running);
	ui_w_enable (g_bMain, n && !g_job.running);
	ui_w_enable (g_bCheck, !g_job.running);
}
static void all_changed ()
{
	refilter (); tabs_labels (); bar_sync (); update_buttons (); ui_view_dirty (g_list);
}
static bool start_job (int kind, const char *const *names, int n, const char *mode = 0)
{
	if (g_job.running) return false;
	for (int i = 0; i < g_nrows; i++) { g_rows[i].state = ST_NONE; g_rows[i].pct = 0; }
	if (kind == J_INSTALL) for (int i = 0; i < n; i++) { Row *r = row_of (names[i]); if (r) r->state = ST_WAIT; }
	if (!job_start (kind, names, n, mode)) { set_status ("Impossible de lancer le travail."); return false; }
	return true;
}

// ---- le dessin d'une ligne -------------------------------------------------------------------------------------------------------
static void check_box (int x, int y, bool on, bool hot)
{
	unsigned l = hot ? C_ACCENT : jet (J_LIGNE_FORTE);
	rbox (x, y, 18, 18, 4, l);
	rbox (x + 1, y + 1, 16, 16, 3, on ? C_ACCENT : C_FIELD);
	if (on) text (x + 3, y + 1, "\xE2\x9C\x93", ink_on (C_ACCENT), 13, 1);
}
static void pill (int x, int y, int w, int h, const char *s, bool accent, bool hot, bool off)
{
	unsigned bg = off ? mix (C_FIELD, C_LINE, 120) : accent ? C_ACCENT : mix (C_FIELD, C_TEXT, hot ? 40 : 20);
	if (accent && hot) bg = jet (J_ACCENT_VIF);
	rbox (x, y, w, h, 7, bg);
	unsigned ink = off ? C_FAINT : accent ? ink_on (bg) : C_TEXT;
	s = TR (s);
	text (x + (w - tw (s, 13, 1)) / 2, y + (h - 16) / 2, s, ink, 13, 1);
}
static void badge (int x, int y, const char *s, unsigned bg)
{
	rbox (x, y, tw (s, 11, 1) + 12, 17, 8, bg);
	text (x + 6, y + 1, s, ink_on (bg), 11, 1);
}
static const char *MODE_L[] = { "Manuel", "Auto", "Jamais" }, *const MODE_K[] = { "manual", "auto", "never" };

static void draw_row (Row &r, int vi, int y, int rw)
{
	bool hot = vi == s_hot;
	if (hot) rbox (4, y + 1, rw - 8, ROWH - 2, 6, mix (C_FIELD, C_ACCENT, 26));
	if (vi + 1 < g_nvis) ui_fill (12, y + ROWH - 1, rw - 24, 1, C_LINE);
	unsigned dim = C_DIM;
	int x = 10;
	if (can_mark (r)) check_box (x, y + (ROWH - 18) / 2, mark_of (r), hot && s_hotPart == 1);
	x += 28;
	ui_icone ((long) r.name, x, y + (ROWH - ICON) / 2, ICON);
	x += ICON + 10;
	int maxr = g_tab == T_INSTALLED ? rw - 92 - 8 - PILLS - 12 : g_tab == T_AVAILABLE ? rw - 190 : rw - 110;
	text (x, y + 9, r.title, C_TEXT, 13, 1, maxr - x);
	int vx = x + tw (r.title, 13, 1) + 8;
	char v[64];
	if (r.hasUpdate) snprintf (v, sizeof v, "%s  \xE2\x86\x92  %s", r.inst, r.avail);
	else if (r.staged && r.avail[0] && vcmp (r.avail, r.inst) > 0) snprintf (v, sizeof v, "%s  \xE2\x86\x92  %s", r.inst, r.avail);
	else snprintf (v, sizeof v, "%s", r.isInst ? r.inst : r.avail);
	if (vx < maxr) { text (vx, y + 11, v, dim, 11, 0, maxr - vx); vx += tw (v, 11) + 8; }
	if (r.restart && (g_tab == T_UPDATES || r.staged) && vx + 70 < maxr) badge (vx, y + 10, TR (r.staged ? "étagé" : "redémarrage"), C_WARN);
	char line[180];
	if (g_tab == T_INSTALLED && r.required) snprintf (line, sizeof line, TR ("%s  -  fait partie du système"), r.category);
	else if (g_tab == T_INSTALLED && r.summary[0]) snprintf (line, sizeof line, "%s  -  %s", r.category, r.summary);
	else snprintf (line, sizeof line, "%s", r.summary[0] ? r.summary : r.category);
	text (x, y + 32, line, dim, 11, 0, maxr - x);
	auto right = [&] (const char *t0, unsigned c, int bold) { const char *t = TR (t0); text (rw - 12 - tw (t, 11, bold), y + 21, t, c, 11, bold); };
	if (r.state == ST_WORK)
	{
		rbox (rw - 170, y + 22, 110, 12, 6, mix (C_FIELD, C_TEXT, 30));
		rbox (rw - 170, y + 22, r.pct * 110 / 100, 12, 6, C_ACCENT);
		char p[8]; snprintf (p, sizeof p, "%d %%", r.pct); text (rw - 52, y + 21, p, C_ACCENT, 11, 1);
		return;
	}
	if (r.state == ST_WAIT) { right ("En attente", C_FAINT, 0); return; }
	if (r.state == ST_DONE) { right ("Installé", C_OK, 1); return; }
	if (r.state == ST_STAGED) { right ("Prêt : au redémarrage", C_WARN, 1); return; }
	if (r.state == ST_FAILED) { right ("Échec", C_BAD, 1); return; }
	char sz[24]; size_text (r.size, sz, sizeof sz);
	if (g_tab == T_UPDATES) right (sz, dim, 0);
	else if (g_tab == T_INSTALLED)
	{
		int mx = rw - 92 - 8 - PILLS;
		rbox (mx, y + 15, PILLS, 28, 14, mix (C_FIELD, C_TEXT, 26));
		for (int k = 0; k < 3; k++)
		{
			bool on = eq (r.mode, MODE_K[k]);
			if (on) rbox (mx + 2 + k * 62, y + 17, 58, 24, 12, C_ACCENT);
			else if (hot && s_hotPart == 10 + k) rbox (mx + 2 + k * 62, y + 17, 58, 24, 12, mix (C_FIELD, C_TEXT, 50));
			int w = tw (MODE_L[k], 11, on);
			text (mx + 2 + k * 62 + (58 - w) / 2, y + 21, MODE_L[k], on ? ink_on (C_ACCENT) : C_TEXT, 11, on);
		}
		if (!r.required) pill (rw - 92, y + 14, 84, 30, "Retirer", false, hot && s_hotPart == 2, g_job.running);
	}
	else
	{
		pill (rw - 112, y + 14, 104, 30, r.hasUpdate ? "Mettre à jour" : "Installer", !r.hasUpdate, hot && s_hotPart == 2, g_job.running);
		text (rw - 122 - tw (sz, 11), y + 21, sz, dim, 11);
	}
}
static void list_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_FIELD);
	int rw = (int) w - (rows_h () > g_lh ? 14 : 0);
	int y = 4 - s_top;
	if (banner ())
	{
		unsigned bg = mix (C_FIELD, C_WARN, 40);
		rbox (6, y, rw - 12, 56, 8, bg);
		text (18, y + 8, TR ("Redémarrer pour finir la mise à jour du système"), C_TEXT, 13, 1, rw - 150);
		text (18, y + 30, TR ("Elle se met en place au prochain démarrage (l'ancien noyau reste en .old)."), C_DIM, 11, 0, rw - 150);
		pill (rw - 122, y + 14, 110, 28, "Redémarrer", true, s_hot == -2, false);
		y += BANNER;
	}
	if (!g_nvis)
	{
		const char *t = TR (g_tab == T_UPDATES ? (g_haveIndex ? "Tout est à jour." : "Le dépôt n'a pas encore été lu : Vérifier.")
			      : g_tab == T_INSTALLED ? "Aucun paquet installé ne correspond." : (g_haveIndex ? "Rien de plus à installer." : "Le dépôt n'a pas encore été lu : Vérifier."));
		text (((int) w - tw (t)) / 2, y + 30, t, C_FAINT);
	}
	for (int vi = 0; vi < g_nvis; vi++, y += ROWH)
		if (y + ROWH >= 0 && y <= h) draw_row (g_rows[g_vis[vi]], vi, y, rw);
}
// les parties d'une ligne : 1 sa case, 2 son bouton, 10..12 les modes, 3 le reste
static int part_at (int vi, int x, int y)
{
	const Row &r = g_rows[g_vis[vi]];
	int rw = g_lw - (rows_h () > g_lh ? 14 : 0);
	if (x >= 6 && x < 36) return 1;
	if (g_tab == T_INSTALLED)
	{
		if (!r.required && x >= rw - 92 && x < rw - 8 && y >= 10 && y < 48) return 2;
		int mx = rw - 92 - 8 - PILLS;
		if (y >= 10 && y < 48 && x >= mx && x < mx + PILLS) return 10 + (x - mx) / 62;
	}
	if (g_tab == T_AVAILABLE && x >= rw - 112 && x < rw - 8 && y >= 10 && y < 48) return 2;
	return 3;
}
static void hit (int mx, int my, int *row, int *part)
{
	int y = my + s_top - 4, b = banner();
	int rw = g_lw - (rows_h () > g_lh ? 14 : 0);
	*row = -1; *part = 0;
	if (b && y >= 0 && y < 56) { if (mx >= rw - 122 && mx < rw - 12 && y >= 10 && y < 46) *row = -2; return; }
	if (y < b) return;
	int k = (y - b) / ROWH;
	if (k < g_nvis) { *row = k; *part = part_at (k, mx, (y - b) % ROWH); }
}
// (une boite de dialogue ne s'ouvre pas dans ui_w_event : la boucle fait le clic apres)
static long list_mouse (long, long kind, long mx, long my, long info)
{
	int row = -1, part = 0;
	if (kind != 3) hit ((int) mx, (int) my, &row, &part);
	if (kind == 1 && (info >> 8 & 1))
	{
		s_down = true; s_scroll = false; s_pressY = (int) my; s_pressTop = s_top; s_pressRow = row; s_pressPart = part;
	}
	else if (kind == 0 && s_down)				// au toucher : glisser fait defiler
	{
		int dy = (int) my - s_pressY;
		if (s_scroll || dy > 8 || dy < -8)
		{
			s_scroll = true; s_top = s_pressTop - dy;
			clamp (); ui_w_set_value (g_bar, s_top); ui_view_dirty (g_list);
			return 0;
		}
	}
	else if (kind == 2)
	{
		if (s_down && !s_scroll && row == s_pressRow && part == s_pressPart && row != -1) { g_later.on = true; g_later.row = row; g_later.part = part; }
		s_down = false;
	}
	if (row != s_hot || part != s_hotPart) { s_hot = row; s_hotPart = part; ui_view_dirty (g_list); }
	return 0;
}
static long list_wheel (long, long steps)
{
	s_top -= (int) steps * ROWH; bar_sync (); ui_view_dirty (g_list);
	return 0;
}
static void foot_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, C_FACE);
	text (0, ((int) h - 16) / 2, g_status, C_DIM, 12, 0, (int) w);
}
static void layout (int w, int h)
{
	g_ww = w; g_wh = h;
	int tw0 = s_tabW, ch = 30;
	ui_w_move (g_bAll, 12 + tw0 + 8, 8, 64, ch);
	int fx = 12 + tw0 + 8 + 64 + 8, fw = w - CAP - fx;
	if (fw > 240) fw = 240;
	ui_w_show (g_find, fw >= 90);
	if (fw >= 90) ui_w_move (g_find, w - CAP - fw, 8, fw, ch);
	g_lw = w - 24; g_lh = h - TOPH - FOOT;
	ui_w_move (g_list, 12, TOPH, g_lw, g_lh);
	ui_w_move (g_bar, 12 + g_lw - 12, TOPH, 12, g_lh);
	int bw = 210;
	ui_w_move (g_bMain, w - 12 - bw, h - FOOT + 8, bw, ch);
	ui_w_move (g_bCheck, w - 12 - bw - 8 - 100, h - FOOT + 8, 100, ch);
	ui_w_move (g_foot, 14, h - FOOT + 8, w - 14 - bw - 8 - 100 - 12 - 8, ch);
	bar_sync ();
	ui_view_dirty (g_list); ui_view_dirty (g_foot);
}
// (une surface neuve : le panel qui repart change aussi la place de la fenetre)
static long on_resize (long w, long h) { ui_fill (0, 0, w, h, C_FACE); layout ((int) w, (int) h); ui_w_draw (0); ui_view_flush (); ui_update_all (); return 0; }

static void install_names (const char *const *names, int n)
{
	if (start_job (J_INSTALL, names, n)) { set_status ("Installation..."); update_buttons (); ui_view_dirty (g_list); }
}
static void do_click (int row, int part)
{
	if (row == -2)
	{
		if (ui_message ((long) TR ("Redémarrer"), (long) TR ("Redémarrer maintenant pour finir la mise à jour du système ?"), UM_YESNO) == 1) reboot_now ();
		return;
	}
	if (row < 0 || row >= g_nvis || g_job.running) return;
	Row &r = g_rows[g_vis[row]];
	if (part == 1 || (g_tab == T_UPDATES && part == 3))
	{
		if (can_mark (r)) { mark_of (r) = !mark_of (r); update_buttons (); }
		ui_view_dirty (g_list); return;
	}
	if (g_tab == T_AVAILABLE && part == 2) { const char *n[1] = { r.name }; install_names (n, 1); return; }
	if (g_tab == T_INSTALLED && part >= 10 && part <= 12)
	{
		if (eq (r.mode, MODE_K[part - 10])) return;
		cpy (r.mode, MODE_K[part - 10], sizeof r.mode);
		const char *n[1] = { r.name }; start_job (J_MODE, n, 1, MODE_K[part - 10]);
		ui_view_dirty (g_list); return;
	}
	if (g_tab == T_INSTALLED && part == 2 && !r.required)
	{
		char q[200]; snprintf (q, sizeof q, TR ("Retirer %s %s ? (les réglages changés sont gardés)"), r.title, r.inst);
		if (ui_message ((long) TR ("Retirer"), (long) q, UM_YESNO) != 1) return;
		const char *n[1] = { r.name };
		if (start_job (J_REMOVE, n, 1)) { set_status ("Retrait..."); update_buttons (); }
	}
}
static void on_check () { if (start_job (J_CHECK, 0, 0)) { set_status ("Lecture du dépôt..."); update_buttons (); } }
static void on_main ()
{
	const char *names[64]; int n = marked (names, 64);
	if (!n) return;
	if (g_tab != T_INSTALLED) { install_names (names, n); return; }
	char q[200]; snprintf (q, sizeof q, TR (n > 1 ? "Retirer %d paquets ? (les réglages changés sont gardés)" : "Retirer %d paquet ? (les réglages changés sont gardés)"), n);
	if (ui_message ((long) TR ("Retirer"), (long) q, UM_YESNO) != 1) return;
	if (start_job (J_REMOVE, names, n)) { set_status ("Retrait..."); update_buttons (); ui_view_dirty (g_list); }
}
static void on_all ()
{
	bool on = !all_marked ();
	for (int i = 0; i < g_nvis; i++) { Row &r = g_rows[g_vis[i]]; if (can_mark (r)) mark_of (r) = on; }
	update_buttons (); ui_view_dirty (g_list);
}
static void control (long w, long code)
{
	if (w == g_tabs && code == UN_CHANGE)
	{
		int t = (int) ui_w_get_value (w);
		if (t != g_tab) { g_tab = t; s_top = 0; s_hot = -1; all_changed (); }
	}
	else if (w == g_find && code == UN_CHANGE) { cpy (g_search, (const char *) ui_w_get_text (w), sizeof g_search); s_top = 0; all_changed (); }
	else if (w == g_bar && code == UN_CHANGE) { s_top = (int) ui_w_get_value (w); ui_view_dirty (g_list); }
	else if (w == g_bCheck && code == UN_ACTIVATE) on_check ();
	else if (w == g_bMain && code == UN_ACTIVATE) on_main ();
	else if (w == g_bAll && code == UN_ACTIVATE) on_all ();
}
// a chaque tour : la progression du travail, sa fin
static void job_poll ()
{
	if (!g_job.running) return;
	if (g_job.done)
	{
		pthread_join (g_job.tid, 0);
		g_job.running = false;
		int rc = g_job.rc, kind = g_job.kind; char msg[200]; cpy (msg, g_job.msg, sizeof msg);
		char done[64][40]; int nd = 0;
		if (kind == J_INSTALL) for (int i = 0; i < g_job.n; i++) cpy (done[nd++], g_job.names[i], 40);
		g_M->db.load ();
		snapshot (); refilter ();
		for (int i = 0; i < nd; i++)
		{
			Row *r = row_of (done[i]); if (!r) continue;
			bool failed = false; for (int k = 0; k < g_job.nfailed; k++) if (eq (g_job.failed[k], done[i])) failed = true;
			r->state = failed ? ST_FAILED : r->staged ? ST_STAGED : ST_DONE;
		}
		if (kind == J_INSTALL && g_tab == T_UPDATES)		// les lignes mises a jour restent en vue
			for (int i = 0; i < nd; i++) { Row *r = row_of (done[i]); if (r && !r->hasUpdate) { int k = (int) (r - g_rows); bool in = false; for (int j = 0; j < g_nvis; j++) if (g_vis[j] == k) in = true; if (!in && g_nvis < 512) g_vis[g_nvis++] = k; } }
		// le gestionnaire s'est mis a jour : celui qui tourne est l'ancien ; le nouveau prend sa place, pkgd aussi
		for (int i = 0; i < nd; i++)
		{
			if (!eq (done[i], "pkgman")) continue;
			bool failed = false; for (int k = 0; k < g_job.nfailed; k++) if (eq (g_job.failed[k], "pkgman")) failed = true;
			if (failed) break;
			set_status ("Le gestionnaire s'est mis à jour : il redémarre..."); ui_view_flush ();
			if (proc_kill ("pkgd")) { usleep (300000); launch ("SD0:/sys/pkgd"); }
			if (launch ("SD0:/apps/pkgman.app/main", "--restart")) exit (0);
			break;
		}
		if (rc == OK && kind != J_INSTALL) status_idle ();
		else if (rc == OK) set_status (any_staged () ? "Fait. La mise à jour du système attend le redémarrage." : "Fait : les apps s'ouvrent dans leur nouvelle version au prochain lancement.");
		else if (rc == E_CANCEL) set_status ("Annulé.");
		else set_status (msg[0] ? msg : "Cela n'a pas marché.");
		tabs_labels (); bar_sync (); update_buttons (); ui_view_dirty (g_list);
		return;
	}
	static int lastPct = -1, lastFin = -1; static char lastCur[40];
	if (g_job.pct != lastPct || strcmp (lastCur, g_job.cur) || g_job.nfinished != lastFin)
	{
		lastPct = g_job.pct; lastFin = g_job.nfinished; cpy (lastCur, g_job.cur, sizeof lastCur);
		for (int k = 0; k < g_job.nfinished; k++)
		{
			Row *r = row_of (g_job.finished[k]); if (!r) continue;
			bool failed = false; for (int q = 0; q < g_job.nfailed; q++) if (eq (g_job.failed[q], r->name)) failed = true;
			r->state = failed ? ST_FAILED : r->restart ? ST_STAGED : ST_DONE;
		}
		Row *c = g_job.cur[0] ? row_of (g_job.cur) : 0;
		if (c && c->state != ST_DONE && c->state != ST_STAGED && c->state != ST_FAILED) { c->state = ST_WORK; c->pct = g_job.pct; }
		if (g_job.kind == J_INSTALL && g_job.cur[0]) { char t[200]; snprintf (t, sizeof t, TR ("Installation de %s : %d %%"), c ? c->title : g_job.cur, g_job.pct); set_status (t); }
		ui_view_dirty (g_list);
	}
}

int main (int argc, char **argv)
{
	bool restart = argc > 1 && eq (argv[1], "--restart");
	if (!restart && proc_running ("pkgman")) return 0;		// (un seul : deux travaux se gêneraient)
	if (!uikit_open (&ui)) return 1;
	for (auto &m : MODE_L) m = TR (m);
	s_jet = (const unsigned *) ui_jetons ();
	static Manager m; g_M = &m;
	m.load_cached_quiet ();
	snapshot ();
	if (argc > 1 && eq (argv[1], "--updates")) g_tab = T_UPDATES;
	else if (!count (T_UPDATES) && g_haveIndex) g_tab = T_AVAILABLE;
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = (int) scr[0] - 8, wh = (int) (scr[1] - scr[3] - scr[2]) - 32;
	if (ww > 780) ww = 780;
	if (wh > 540) wh = 540;
	long win = ui_window_ex ((scr[0] - ww) / 2, scr[3] + 8, ww, wh, (long) TR ("Logiciels"), WF_RESIZABLE);
	if (win <= 0) return 1;
	ui_min_size (640, 360);
	ui_on_resize ((long) on_resize, 0);
	s_tabText[0] = 0;
	g_bAll = ui_pushbutton (0, 0, 64, 30, (long) "Tous", 0);
	g_find = ui_textbox (0, 0, 10, 30, 0); ui_textbox_hint (g_find, (long) TR ("Rechercher")); ui_w_set_range (g_find, 0, 60, 0, 0);
	g_list = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_list, (long) list_paint, (long) list_mouse, 0, (long) list_wheel);
	g_bar = ui_scrollbar (0, 0, 12, 10, 0); ui_w_show (g_bar, 0);
	g_foot = ui_view (0, 0, 10, 10, 0); ui_view_on (g_foot, (long) foot_paint, 0, 0, 0);
	g_bCheck = ui_pushbutton (0, 0, 100, 30, (long) "Vérifier", 0);
	g_bMain = ui_pushbutton (0, 0, 210, 30, (long) "Installer", UB_DEFAULT);
	refilter (); tabs_labels ();
	layout ((int) ui.w, (int) ui.h);
	status_idle (); update_buttons ();
	ui_fill (0, 0, ui.w, ui.h, C_FACE);
	ui_w_draw (0); ui_view_flush (); ui_update_all ();
	on_check ();
	for (;;)
	{
		long t = ui_event (30);
		if (t == WEV_CLOSE) break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
			if (g_later.on) { g_later.on = false; do_click (g_later.row, g_later.part); }
		}
		else ui_w_idle ();
		job_poll ();
		ui_view_flush ();
	}
	if (g_job.running) { g_job.cancel = true; pthread_join (g_job.tid, 0); }
	return 0;
}
