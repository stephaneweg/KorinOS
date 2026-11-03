//
// setup -- le premier demarrage de Korin OS, sur UIKit, comme Setup d'Onyx (docs/PREMIER-DEMARRAGE.md) :
//
//   1 Bienvenue         la langue : l'assistant repart dans cette langue
//   2 Region            le pays, le fuseau, le clavier (pris tout de suite, a essayer)
//   3 Affichage         la resolution (essayee, gardee ou rendue) et le theme
//   4 Wi-Fi             les reseaux autour, le mot de passe
//   5 Logiciels         les paquets du depot, a cocher, installes depuis Internet
//   6 Acces a distance  telnet, VNC, RDP, FTP
//
// SD0:/etc/session ne lance que le fond et Setup : les autres lignes y attendent (« #setup: ... »).
// « Demarrer » les rend, retire sa ligne et redemarre, pour que tout parte dans la langue et le theme choisis.
//
#include "pkglib.h"
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>
#include "uikit.h"
#include "netkit_abi.h"

using namespace pkg;

#define TR(s)	((const char *) ui_tr ((long) (s)))

enum { P_WELCOME, P_REGION, P_SCREEN, P_WIFI, P_PKG, P_REMOTE, P_N };
enum { WW = 760, HH = 520, RAIL = 196, FOOT = 52, HEAD = 74, ROWH = 46, PAD = 20 };
enum { SYS_WS_CALL_ = 0x51, WS_OP_SCREEN_SET_ = 30, WS_OP_SCREEN_KEEP_ = 31, WS_OP_SCREEN_MODES_ = 32, WS_SM_N_ = 24 };

static struct ui_vars ui;
static const unsigned *s_jet;
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static inline void rbox (int x, int y, int w, int h, int r, unsigned c) { ui_rbox (x, y, w, h, r, c, c); }
static inline long face (int bold) { return bold ? (long) ui.font_b : (long) ui.font; }
static inline void text (int x, int y, const char *s, unsigned c, int px = 13, int bold = 0, int w = 4000)
{
	ui_text_fit ((long) s, x, y + px, c, px, face (bold), w);
}

static const char *const PAGES[P_N] = { "Bienvenue", "Région et clavier", "Écran et thème", "Wi-Fi", "Logiciels", "Accès à distance" };
static const char *const SOUS[P_N] = {
	"Quelques choix et cet appareil est prêt. Tout se change ensuite dans Réglages.",
	"Le pays donne ses canaux au Wi-Fi ; le clavier change tout de suite.",
	"La résolution de l'écran et les couleurs du bureau.",
	"Pour installer des logiciels et mettre l'heure à jour. Cette étape peut être passée.",
	"Cochez ce que vous voulez installer depuis Internet.",
	"Ces services n'ont pas de mot de passe : activez seulement ce qui vous sert, sur un réseau de confiance." };
static int g_page;
static long g_rail, g_head, g_foot, g_back, g_next;

// ---- les fichiers de /etc ---------------------------------------------------------------------------
static char *lire (const char *chemin)
{
	FILE *f = fopen (chemin, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	char *b = (char *) malloc ((size_t) n + 1);
	if (b) { n = (long) fread (b, 1, (size_t) n, f); b[n] = 0; }
	fclose (f);
	return b;
}
static bool cle_de (const char *l, const char *k)
{
	size_t n = strlen (k);
	if (strncmp (l, k, n)) return false;
	l += n;
	while (*l == ' ') l++;
	return *l == '=';
}
// « cle = valeur » : remplace sa ligne, ou celle en commentaire, ou s'ajoute
static void ini_put (const char *chemin, const char *k, const char *v)
{
	char *b = lire (chemin);
	FILE *f = fopen (chemin, "wb");
	if (!f) { free (b); return; }
	bool fait = false;
	for (char *l = b; l && *l; )
	{
		char *e = strchr (l, '\n');
		size_t n = e ? (size_t) (e - l) + 1 : strlen (l);
		char sv = l[n - 1 + (e ? 0 : 1)]; (void) sv;
		char t[256]; size_t c = n < sizeof t ? n : sizeof t - 1; memcpy (t, l, c); t[c] = 0;
		if (!fait && (cle_de (t, k) || (t[0] == '#' && cle_de (t + 1, k)))) { fprintf (f, "%s=%s\n", k, v); fait = true; }
		else fwrite (l, 1, n, f);
		l += n;
	}
	if (!fait) fprintf (f, "%s=%s\n", k, v);
	fclose (f);
	free (b);
}
static const char *const F_SYSTEM = "SD0:/etc/system.ini", *const F_OPTIONS = "SD0:/etc/options.txt";

// ---- 1 Bienvenue ------------------------------------------------------------------------------------
static long g_lang;
static void langue_choisie ()
{
	const char *l = ui_w_get_value (g_lang) == 0 ? "en" : "fr";
	if (eq ((const char *) ui_lang (), l)) return;
	ini_put (F_SYSTEM, "lang", l);
	launch ("SD0:/sys/setup", "--encore");		// (un dictionnaire se lit au demarrage)
	exit (0);
}

// ---- 2 Region et clavier ----------------------------------------------------------------------------
static const char *const PAYS[][3] = { { "FR", "France", "FR" }, { "BE", "Belgique", "FR" }, { "CH", "Suisse", "FR" },
	{ "LU", "Luxembourg", "FR" }, { "CA", "Canada", "US" }, { "DE", "Allemagne", "DE" }, { "GB", "Royaume-Uni", "US" },
	{ "US", "États-Unis", "US" } };
static const char *const FUSEAUX[] = { "Europe/Paris", "Europe/London", "Europe/Lisbon", "Europe/Athens", "Europe/Moscow",
	"America/New_York", "UTC" };
enum { N_PAYS = 8, N_FUSEAUX = 7, N_KEYB = 16 };
static long g_pays, g_fuseau, g_keyb, g_essai;
static char g_keybs[N_KEYB][16]; static int g_nkeyb;

static void clavier_applique (const char *nom)
{
	long ch = sys_call (SYS_SVC_OPEN_, (long) "hid", 1000);
	if (ch < 0) return;
	char m[512]; memset (m, 0, sizeof m);
	*(long *) m = 2;					// HID_OP_KEYMAP
	cpy (m + 48, nom, 16);
	sys_call (SYS_SVC_CALL_, ch, (long) m);
	ini_put (F_OPTIONS, "keyb", nom);
}
static void clavier_liste (char *out, size_t cap)
{
	g_nkeyb = 0; out[0] = 0;
	DIR *d = opendir ("SD0:/res/keyb");
	if (!d) return;
	struct dirent *e;
	while ((e = readdir (d)) && g_nkeyb < N_KEYB)
	{
		size_t n = strlen (e->d_name);
		if (n < 5 || n > 15 || strcmp (e->d_name + n - 4, ".txt")) continue;
		cpy (g_keybs[g_nkeyb], e->d_name, n - 3);
		if (out[0]) strncat (out, "|", cap - strlen (out) - 1);
		strncat (out, g_keybs[g_nkeyb], cap - strlen (out) - 1);
		g_nkeyb++;
	}
	closedir (d);
}
static void clavier_choisi ()
{
	long v = ui_w_get_value (g_keyb);
	if (v >= 0 && v < g_nkeyb) clavier_applique (g_keybs[v]);
}
static void pays_choisi ()
{
	long v = ui_w_get_value (g_pays);
	if (v < 0 || v >= N_PAYS) return;
	ini_put (F_OPTIONS, "pays", PAYS[v][0]);
	char c[8]; snprintf (c, sizeof c, " %s ", PAYS[v][0]);
	wifi_country ((long) c);
	for (int i = 0; i < g_nkeyb; i++)
		if (eq (g_keybs[i], PAYS[v][2])) { ui_w_set_value (g_keyb, i); ui_w_draw (g_keyb); clavier_applique (g_keybs[i]); }
}

// ---- 3 Affichage ------------------------------------------------------------------------------------
static long g_res, g_essaie, g_theme;
static int g_modes[WS_SM_N_][2]; static int g_nmodes, g_curw, g_curh;
static long ws_call (char *m, long op, long a, long b, long c)
{
	memset (m, 0, 512);
	*(long *) m = op; *(long *) (m + 8) = a; *(long *) (m + 16) = b; *(long *) (m + 24) = c;
	return sys_call (SYS_WS_CALL_, (long) m, 0);
}
static void modes_liste (char *out, size_t cap)
{
	char m[512]; out[0] = 0;
	long n = ws_call (m, WS_OP_SCREEN_MODES_, 0, 0, 0);
	if (n <= 0) return;
	g_curw = (int) *(long *) (m + 24); g_curh = (int) *(long *) (m + 32);
	if (n > WS_SM_N_) n = WS_SM_N_;
	g_nmodes = (int) n;
	for (int i = 0; i < g_nmodes; i++)
	{
		unsigned short *e = (unsigned short *) (m + 48 + i * 8);
		g_modes[i][0] = e[0]; g_modes[i][1] = e[1];
		char t[24]; snprintf (t, sizeof t, "%s%d x %d", i ? "|" : "", e[0], e[1]);
		strncat (out, t, cap - strlen (out) - 1);
	}
}
static void resolution_essai ()
{
	long v = ui_w_get_value (g_res);
	if (v < 0 || v >= g_nmodes) return;
	int w = g_modes[v][0], h = g_modes[v][1];
	char m[512], t[24];
	if (w != g_curw || h != g_curh)
	{
		if (ws_call (m, WS_OP_SCREEN_SET_, w, h, 17000) < 0) return;
		long oui = ui_message ((long) TR ("Écran et thème"), (long) TR ("Garder cette résolution ?"), UM_YESNO);
		ws_call (m, WS_OP_SCREEN_KEEP_, oui == 1, 0, 0);
		if (oui != 1) return;
		g_curw = w; g_curh = h;
	}
	snprintf (t, sizeof t, "%dx%d", w, h);
	ini_put (F_OPTIONS, "ecran", t);
}
static void theme_choisi ()
{
	ini_put ("SD0:/etc/theme.txt", "theme", ui_w_get_value (g_theme) == 1 ? "Onyx Noir" : "Givre");
	ui_theme_texte (0);
	s_jet = (const unsigned *) ui_jetons ();
}

// ---- le travail, dans un thread : balayage, connexion, index, installation --------------------------
enum { T_SCAN, T_JOIN, T_INDEX, T_INSTALL };
static volatile bool s_tourne, s_fini;
static int s_travail; static volatile int s_pct; static volatile long s_rc;
static char s_msg[160];
static pthread_t s_tid;

static unsigned char s_scan[32][48]; static int s_nscan;
static char s_ssid[40], s_cle[72];

static Manager *g_M;
struct Ligne { char name[40], title[64], summary[120]; u64 size; bool coche; };
static Ligne *g_lignes; static int g_nl;
static char s_noms[64][40]; static int s_nnoms;

struct Rapport : Report
{
	void say (const char *s) override { cpy (s_msg, s, sizeof s_msg); }
	void step (const char *, u64 done, u64 total) override { s_pct = total ? (int) (done * 100 / total) : 0; }
};
static void *travail (void *)
{
	Rapport r;
	if (s_travail == T_SCAN) { long n = wifi_scan ((long) s_scan, 32); s_rc = n; s_nscan = n > 0 ? (int) n : 0; }
	else if (s_travail == T_JOIN) s_rc = wifi_join ((long) s_ssid, s_cle[0] ? (long) s_cle : 0);
	else if (s_travail == T_INDEX) s_rc = g_M->refresh (r);
	else
	{
		Manager &m = *g_M; int rc = OK;
		for (int i = 0; i < s_nnoms; i++)
		{
			int err = 0; const Pkg *sub[32];
			int k = m.resolve (s_noms[i], sub, 32, &err, r);
			if (!k) rc = err;
			for (int j = 0; j < k; j++)
			{
				snprintf (s_msg, sizeof s_msg, "%s", sub[j]->title); s_pct = 0;
				int x = m.install (*sub[j], r);
				if (x != OK) rc = x;
			}
		}
		s_rc = rc;
	}
	s_fini = true;
	return 0;
}
static bool lance (int quoi)
{
	if (s_tourne) return false;
	s_travail = quoi; s_tourne = true; s_fini = false; s_pct = 0; s_msg[0] = 0;
	if (pthread_create (&s_tid, 0, travail, 0) != 0) { s_tourne = false; return false; }
	return true;
}

// ---- 4 Wi-Fi ----------------------------------------------------------------------------------------
static long g_wlist, g_wpass, g_wjoin, g_wscan, g_wetat;
static void wifi_remplit ()
{
	ui_list_clear (g_wlist);
	for (int i = 0; i < s_nscan; i++)
	{
		unsigned char *e = s_scan[i];
		char t[64]; int n = e[32] < 32 ? e[32] : 32;
		memcpy (t, e, (size_t) n); t[n] = 0;
		if (n) ui_list_add (g_wlist, (long) t, i);
	}
	ui_w_draw (g_wlist);
}
static void wifi_connecte ()
{
	long v = ui_w_get_value (g_wlist);
	if (v < 0 || v >= ui_list_count (g_wlist)) return;
	cpy (s_ssid, (const char *) ui_list_get (g_wlist, v), sizeof s_ssid);
	cpy (s_cle, (const char *) ui_w_get_text (g_wpass), sizeof s_cle);
	if (lance (T_JOIN)) { ui_w_set_text (g_wetat, (long) TR ("Connexion...")); ui_w_draw (g_wetat); ui_w_enable (g_wjoin, 0); }
}

// ---- 5 Logiciels ------------------------------------------------------------------------------------
static long g_plist, g_pinst, g_petat;
static int s_top, s_downy, s_downtop, s_down; static bool s_drag; static bool s_liste;
static int rows_h () { return g_nl * ROWH; }
static void clamp_top (int h)
{
	int m = rows_h () - h;
	if (s_top > m) s_top = m;
	if (s_top < 0) s_top = 0;
}
static void plist_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_SURFACE));
	clamp_top ((int) h);
	if (!g_nl)
	{
		text (14, 14, TR (s_tourne ? "Lecture du dépôt..." : "Aucun paquet : vérifiez la connexion, ou continuez."), jet (J_TEXTE2));
		return;
	}
	for (int i = s_top / ROWH; i < g_nl; i++)
	{
		int y = i * ROWH - s_top;
		if (y >= h) break;
		Ligne &l = g_lignes[i];
		rbox (12, y + 13, 20, 20, 5, l.coche ? jet (J_ACCENT) : jet (J_LIGNE_FORTE));
		if (!l.coche) rbox (14, y + 15, 16, 16, 4, jet (J_SURFACE));
		else { ui_fill (17, y + 22, 3, 3, 0xFFFFFF); ui_fill (20, y + 24, 3, 3, 0xFFFFFF); ui_fill (23, y + 19, 3, 6, 0xFFFFFF); ui_fill (26, y + 17, 2, 3, 0xFFFFFF); }
		text (44, y + 6, l.title, jet (J_TEXTE1), 13, 1, (int) w - 130);
		text (44, y + 25, l.summary, jet (J_TEXTE2), 11, 0, (int) w - 130);
		char t[24]; u64 ko = l.size / 1024;
		if (ko >= 1024) snprintf (t, sizeof t, "%d Mo", (int) (ko / 1024)); else snprintf (t, sizeof t, "%d Ko", (int) ko);
		text ((int) w - 70, y + 15, t, jet (J_TEXTE2), 11);
		ui_fill (12, y + ROWH - 1, w - 24, 1, jet (J_LIGNE));
	}
}
static long plist_mouse (long, long kind, long, long my, long info)
{
	if (kind == 1 && (info >> 8 & 1)) { s_down = 1; s_downy = (int) my; s_downtop = s_top; s_drag = false; }
	else if (kind == 0 && s_down)
	{
		int dy = (int) my - s_downy;
		if (s_drag || dy > 8 || dy < -8) { s_drag = true; s_top = s_downtop - dy; ui_view_dirty (g_plist); }
	}
	else if (kind == 2 && s_down)
	{
		s_down = 0;
		int i = ((int) my + s_top) / ROWH;
		if (!s_drag && !s_tourne && i >= 0 && i < g_nl) { g_lignes[i].coche = !g_lignes[i].coche; ui_view_dirty (g_plist); }
	}
	return 0;
}
static long plist_wheel (long, long steps) { s_top -= (int) steps * ROWH; ui_view_dirty (g_plist); return 0; }
static void paquets_remplit ()
{
	Manager &m = *g_M;
	free (g_lignes); g_lignes = (Ligne *) calloc ((size_t) (m.index.n ? m.index.n : 1), sizeof (Ligne)); g_nl = 0;
	for (int i = 0; i < m.index.n; i++)
	{
		const Pkg &p = m.index.p[i];
		if (p.required || m.db.find (p.name)) continue;
		Ligne &l = g_lignes[g_nl++];
		cpy (l.name, p.name, sizeof l.name); cpy (l.title, p.title, sizeof l.title);
		cpy (l.summary, p.summary, sizeof l.summary); l.size = p.size;
	}
	s_liste = true;
	ui_view_dirty (g_plist);
}
static void paquets_installe ()
{
	s_nnoms = 0;
	for (int i = 0; i < g_nl && s_nnoms < 64; i++) if (g_lignes[i].coche) cpy (s_noms[s_nnoms++], g_lignes[i].name, 40);
	if (s_nnoms && lance (T_INSTALL)) { ui_w_enable (g_pinst, 0); ui_w_enable (g_next, 0); ui_w_enable (g_back, 0); }
}

// ---- 6 Acces a distance -----------------------------------------------------------------------------
static const char *const SERVICES[4][2] = { { "Terminal à distance (telnet, port 23)", "telnetd" }, { "Bureau à distance (VNC, port 5900)", "vncd" },
	{ "Fenêtres à distance (RDP)", "rdpd" }, { "Partage de fichiers (FTP, port 21)", "ftpd" } };
static long g_serv[4];
static bool service_actif (const char *nom)
{
	char *b = lire ("SD0:/etc/services.txt"); bool oui = false;
	char c[48]; snprintf (c, sizeof c, "SD0:/sys/svc/%s", nom);
	for (char *l = b ? strtok (b, "\n") : 0; l; l = strtok (0, "\n")) if (!strncmp (l, c, strlen (c))) oui = true;
	free (b);
	return oui;
}
static void services_ecrit ()
{
	char *b = lire ("SD0:/etc/services.txt");
	FILE *f = fopen ("SD0:/etc/services.txt", "wb");
	if (!f) { free (b); return; }
	bool vu[4] = {};
	for (char *l = b ? strtok (b, "\n") : 0; l; l = strtok (0, "\n"))
	{
		char *p = l; while (*p == '#' || *p == ' ') p++;
		int k = -1;
		for (int i = 0; i < 4; i++) { char c[48]; snprintf (c, sizeof c, "SD0:/sys/svc/%s", SERVICES[i][1]); if (!strncmp (p, c, strlen (c))) k = i; }
		if (k < 0) { fprintf (f, "%s\n", l); continue; }
		vu[k] = true;
		fprintf (f, "%s%s\n", ui_w_get_value (g_serv[k]) ? "" : "#", p);
	}
	for (int i = 0; i < 4; i++) if (!vu[i] && ui_w_get_value (g_serv[i])) fprintf (f, "SD0:/sys/svc/%s\n", SERVICES[i][1]);
	fclose (f);
	free (b);
}
// les lignes qui attendaient (« #setup: ... ») rendues, celle de Setup retiree
static void rendre (const char *chemin)
{
	char *b = lire (chemin);
	if (!b) return;
	FILE *f = fopen (chemin, "wb");
	if (f)
	{
		for (char *l = strtok (b, "\n"); l; l = strtok (0, "\n"))
		{
			if (strstr (l, "SD0:/sys/setup")) continue;
			fprintf (f, "%s\n", !strncmp (l, "#setup: ", 8) ? l + 8 : l);
		}
		fclose (f);
	}
	free (b);
}
static void finir ()
{
	services_ecrit ();
	rendre ("SD0:/etc/session");
	rendre ("SD0:/etc/preload.txt");
	reboot_now ();
}

// ---- les pages --------------------------------------------------------------------------------------
struct Item { long w; int page; };
static Item g_items[48]; static int g_nitems;
static long item (int page, long w) { g_items[g_nitems].w = w; g_items[g_nitems++].page = page; return w; }

static void montre (int p)
{
	g_page = p;
	for (int i = 0; i < g_nitems; i++) ui_w_show (g_items[i].w, g_items[i].page == p);
	ui_w_show (g_back, p > 0);
	ui_w_set_text (g_next, (long) TR (p == P_N - 1 ? "Démarrer Korin OS" : "Continuer"));
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_view_dirty (g_rail); ui_view_dirty (g_head); ui_view_dirty (g_foot);
	ui_w_draw (0); ui_view_flush (); ui_update_all ();
	if (p == P_WIFI && !s_nscan && !s_tourne) lance (T_SCAN);
	if (p == P_PKG && !s_liste && !s_tourne) lance (T_INDEX);
}
static void rail_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_CREUX));
	text (18, 18, "Korin OS", jet (J_TEXTE1), 15, 1);
	for (int i = 0; i < P_N; i++)
	{
		int y = 62 + i * 40;
		bool cur = i == g_page, fait = i < g_page;
		if (cur) rbox (8, y - 6, (int) w - 16, 32, 6, jet (J_ELEVE));
		rbox (16, y + 3, 10, 10, 5, cur || fait ? jet (fait ? J_SUCCES : J_ACCENT) : jet (J_LIGNE_FORTE));
		text (36, y, TR (PAGES[i]), jet (cur ? J_TEXTE1 : J_TEXTE2), 13, cur, (int) w - 44);
	}
}
static void head_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	text (0, 8, TR (g_page ? PAGES[g_page] : "Bienvenue dans Korin OS"), jet (J_TEXTE1), 22, 1, (int) w - FRAME_CAP_W);
	text (0, 42, TR (SOUS[g_page]), jet (J_TEXTE2), 12, 0, (int) w);
}
static void foot_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	char t[32]; snprintf (t, sizeof t, "%d / %d", g_page + 1, (int) P_N);
	text (0, 14, t, jet (J_TEXTE3), 12);
	if (s_tourne && s_travail == T_INSTALL) { char m[200]; snprintf (m, sizeof m, "%s : %d %%", s_msg, s_pct); text (50, 14, m, jet (J_TEXTE2), 12, 0, (int) w - 50); }
}
static void control (long w, long code)
{
	if (code == UN_ACTIVATE)
	{
		if (w == g_next) { if (g_page == P_N - 1) finir (); else montre (g_page + 1); }
		else if (w == g_back) montre (g_page - 1);
		else if (w == g_essaie) resolution_essai ();
		else if (w == g_wscan) { if (lance (T_SCAN)) ui_w_enable (g_wscan, 0); }
		else if (w == g_wjoin) wifi_connecte ();
		else if (w == g_pinst) paquets_installe ();
		return;
	}
	if (code != UN_CHANGE) return;
	if (w == g_lang) langue_choisie ();
	else if (w == g_pays) pays_choisi ();
	else if (w == g_keyb) clavier_choisi ();
	else if (w == g_fuseau) { long v = ui_w_get_value (w); if (v >= 0 && v < N_FUSEAUX) ini_put (F_OPTIONS, "fuseau", FUSEAUX[v]); }
	else if (w == g_theme) { theme_choisi (); montre (g_page); }
}
static void poll ()
{
	if (!s_tourne) return;
	if (!s_fini) { if (s_travail == T_INSTALL) ui_view_dirty (g_foot); return; }
	pthread_join (s_tid, 0);
	s_tourne = false;
	if (s_travail == T_SCAN)
	{
		ui_w_enable (g_wscan, 1);
		if (s_rc < 0) { ui_w_set_text (g_wetat, (long) TR ("Pas de Wi-Fi sur cet appareil.")); ui_w_draw (g_wetat); }
		wifi_remplit ();
	}
	else if (s_travail == T_JOIN)
	{
		ui_w_enable (g_wjoin, 1);
		ui_w_set_text (g_wetat, (long) TR (s_rc == 0 ? "Connecté." : "Connexion impossible : vérifiez le mot de passe."));
		ui_w_draw (g_wetat);
	}
	else if (s_travail == T_INDEX) { paquets_remplit (); ui_w_enable (g_pinst, g_nl > 0); }
	else
	{
		ui_w_set_text (g_petat, (long) TR (s_rc == OK ? "Installation terminée." : "Installation incomplète : Logiciels reprendra plus tard."));
		ui_w_draw (g_petat);
		for (int i = 0; i < g_nl; i++) g_lignes[i].coche = false;
		paquets_remplit ();
		ui_w_enable (g_pinst, 1); ui_w_enable (g_next, 1); ui_w_enable (g_back, 1);
		ui_view_dirty (g_foot);
	}
}

int main (int, char **)
{
	if (!uikit_open (&ui)) return 1;
	if (netkit_kit_open () == 0) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	static Manager m; g_M = &m;
	m.load_cached_quiet ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = (int) scr[0] < WW + 8 ? (int) scr[0] - 8 : WW, wh = (int) scr[1] < HH + 8 ? (int) scr[1] - 8 : HH;
	long win = ui_window_ex ((scr[0] - ww) / 2, (scr[1] - wh) / 2, ww, wh, (long) "Korin OS", 0);
	if (win <= 0) return 1;

	int X = RAIL + PAD, Y = HEAD + 10, W = ww - X - PAD, L = 14 + 8;
	g_rail = ui_view (0, 0, RAIL, wh, 0); ui_view_on (g_rail, (long) rail_paint, 0, 0, 0);
	g_head = ui_view (X, 8, W, HEAD - 8, 0); ui_view_on (g_head, (long) head_paint, 0, 0, 0);
	g_foot = ui_view (X, wh - FOOT, W - 320, FOOT, 0); ui_view_on (g_foot, (long) foot_paint, 0, 0, 0);
	g_back = ui_pushbutton (ww - PAD - 310, wh - FOOT + 8, 100, 32, (long) TR ("Retour"), 0);
	g_next = ui_pushbutton (ww - PAD - 200, wh - FOOT + 8, 200, 32, (long) TR ("Continuer"), UB_DEFAULT);

	item (P_WELCOME, ui_label (X, Y, W, L, (long) TR ("Langue"), ULB_DIM));
	g_lang = item (P_WELCOME, ui_segmented (X, Y + 24, 0, 0, (long) "English|Français"));
	ui_w_set_value (g_lang, eq ((const char *) ui_lang (), "en") ? 0 : 1);

	char l[512] = "";
	for (int i = 0; i < N_PAYS; i++) { if (i) strncat (l, "|", sizeof l - strlen (l) - 1); strncat (l, TR (PAYS[i][1]), sizeof l - strlen (l) - 1); }
	item (P_REGION, ui_label (X, Y, W, L, (long) TR ("Pays"), ULB_DIM));
	g_pays = item (P_REGION, ui_dropdown (X, Y + 24, 280, 32, (long) l));
	l[0] = 0;
	for (int i = 0; i < N_FUSEAUX; i++) { if (i) strncat (l, "|", sizeof l - strlen (l) - 1); strncat (l, FUSEAUX[i], sizeof l - strlen (l) - 1); }
	item (P_REGION, ui_label (X, Y + 68, W, L, (long) TR ("Fuseau horaire"), ULB_DIM));
	g_fuseau = item (P_REGION, ui_dropdown (X, Y + 92, 280, 32, (long) l));
	clavier_liste (l, sizeof l);
	item (P_REGION, ui_label (X, Y + 136, W, L, (long) TR ("Clavier"), ULB_DIM));
	g_keyb = item (P_REGION, ui_dropdown (X, Y + 160, 280, 32, (long) l));
	for (int i = 0; i < g_nkeyb; i++) if (eq (g_keybs[i], "FR")) ui_w_set_value (g_keyb, i);
	g_essai = item (P_REGION, ui_textbox (X, Y + 204, 280, 32, 0));
	ui_textbox_hint (g_essai, (long) TR ("Essayez le clavier ici"));

	modes_liste (l, sizeof l);
	item (P_SCREEN, ui_label (X, Y, W, L, (long) TR ("Résolution"), ULB_DIM));
	g_res = item (P_SCREEN, ui_dropdown (X, Y + 24, 280, 32, (long) l));
	for (int i = 0; i < g_nmodes; i++) if (g_modes[i][0] == g_curw && g_modes[i][1] == g_curh) ui_w_set_value (g_res, i);
	g_essaie = item (P_SCREEN, ui_pushbutton (X + 292, Y + 24, 120, 32, (long) TR ("Essayer"), 0));
	item (P_SCREEN, ui_label (X, Y + 80, W, L, (long) TR ("Thème"), ULB_DIM));
	g_theme = item (P_SCREEN, ui_segmented (X, Y + 104, 0, 0, (long) "Givre|Onyx Noir"));

	g_wlist = item (P_WIFI, ui_list (X, Y, W, 210, 0));
	g_wpass = item (P_WIFI, ui_textbox (X, Y + 222, W - 250, 32, UTF_PASSWORD));
	ui_textbox_hint (g_wpass, (long) TR ("Mot de passe"));
	g_wjoin = item (P_WIFI, ui_pushbutton (X + W - 240, Y + 222, 120, 32, (long) TR ("Connecter"), 0));
	g_wscan = item (P_WIFI, ui_pushbutton (X + W - 110, Y + 222, 110, 32, (long) TR ("Actualiser"), 0));
	g_wetat = item (P_WIFI, ui_label (X, Y + 262, W, L, (long) "", ULB_DIM));

	g_plist = item (P_PKG, ui_view (X, Y, W, 262, 0));
	ui_view_on (g_plist, (long) plist_paint, (long) plist_mouse, 0, (long) plist_wheel);
	g_pinst = item (P_PKG, ui_pushbutton (X, Y + 272, 200, 32, (long) TR ("Installer la sélection"), 0));
	ui_w_enable (g_pinst, 0);
	g_petat = item (P_PKG, ui_label (X + 212, Y + 278, W - 212, L, (long) "", ULB_DIM));

	for (int i = 0; i < 4; i++)
		g_serv[i] = item (P_REMOTE, ui_switch (X, Y + i * 44, W, 32, (long) TR (SERVICES[i][0]), service_actif (SERVICES[i][1])));

	montre (P_WELCOME);
	for (;;)
	{
		long t = ui_event (30);
		if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
		}
		else ui_w_idle ();
		poll ();
		ui_view_flush ();
	}
	return 0;
}
