//
// bankui.h -- Budget mensuel : l'interface de la liaison bancaire (bank.h), comme l'app Windows (BankSettingsForm,
// BankLink, BankImportForm, MainForm.SyncBank). Inclus par main.cpp, après ses grilles et ses dialogues.
//
//   Banque > Synchroniser la banque (F5), ou le bouton de l'onglet Soldes et résumé : le solde réel de la banque dans le
//   suivi du mois, puis ses nouvelles opérations à classer : un poste fixe (le montant s'ajoute à « Déjà payé »), une
//   colonne des dépenses courantes, Ignorer, ou à classer plus tard ; « Retenir » garde le mot-clé pour la fois suivante.
//   Banque > Réglages… : l'application Enable Banking, la clé, la banque, l'accès, les règles retenues.
//
//   Relier le compte : la page de la banque s'ouvre dans Jet s'il est là (son retour sur http://localhost:… arrive
//   tout seul), sinon l'adresse se copie vers un autre appareil et l'adresse de retour (« code= ») se colle ici.
//   Un accès relié par l'app Windows sert tel quel : copier banque.json et la clé .pem dans HOME:/budget.
//
//   Essais : --bank-api URL (le faux serveur d'Enable Banking), --bank-follow (l'autorisation suivie sans navigateur).
//
#include <spawn.h>
#include "bank.h"
#include "../../lib/keyvault/keyvault.h"

extern char **environ;

static const char *const BANK_FILE = "HOME:/budget/banque.json";
static const char *const WEB_PROGRAM = "SD0:/apps/jet.app/main";
static bank::Settings g_bk;
static bool g_bkOk = true;			// banque.json lisible (sinon la synchronisation reste coupée)
static std::string g_bkApi;			// --bank-api
static bool g_bkFollow;				// --bank-follow
static long g_bSync;				// « Synchroniser la banque », onglet Soldes et résumé
static bool g_syncing;

// le décalage de l'heure locale (secondes, au quart d'heure), pour écrire les dates comme l'app Windows
static long bk_tz () { long d = local_seconds () - (long) time (0); return (d + (d >= 0 ? 450 : -450)) / 900 * 900; }
// Le coffre à secrets de Korin (docs/KEYVAULT.md ; le PC), sous l'identité de Budget : « bank-key », le contenu de la
// clé privée (PEM), et « bank-session », SessionId. La clé est importée du .pem de KeyPath la première fois (et de
// nouveau quand ce fichier change) ; ensuite l'app n'a plus besoin du .pem : l'utilisateur peut l'enlever de la carte
// (Budget ne l'efface jamais, c'est son fichier ; KeyPath reste dans banque.json pour l'app Windows). SessionId quitte
// banque.json (un SessionId qui y revient, d'un banque.json copié de Windows, repasse dans le coffre). Sans coffre
// (le Pi) : tout comme avant, dans banque.json et le .pem.
static int g_bkKv;				// 0 pas de coffre, 1 le coffre, 2 le coffre nous a refusés (banque.json comme avant)
// le .pem nommé par les réglages, s'il est là et autre que la copie du coffre : dans le coffre
static void bk_key_import ()
{
	std::string kf = bank::key_file (g_bk, DIR_), pem;
	if (g_bkKv != 1 || kf.empty () || !bank::read_file (kf, pem) || pem.empty () || pem == g_bk.keyPem) { std::fill (pem.begin (), pem.end (), 0); return; }
	if (pem.size () <= KV_VALUE_MAX && kv_set ("bank-key", pem.data (), (long) pem.size ()) >= 0)
	{
		std::fill (g_bk.keyPem.begin (), g_bk.keyPem.end (), 0);
		g_bk.keyPem = pem;
	}
	std::fill (pem.begin (), pem.end (), 0);
}
static bool bk_save_file ()
{
	mkdir (DIR_, 0755);
	if (g_bkKv != 1) return bank::save (BANK_FILE, g_bk, bk_tz (), g_today);
	bank::Settings c = g_bk;			// (SessionId : dans le coffre, pas dans le fichier)
	c.sessionId.clear (); c.keyPem.clear ();
	bool ok = bank::save (BANK_FILE, c, bk_tz (), g_today);
	g_bk.seen = c.seen;
	return ok;
}
static void bk_load_vault ()
{
	g_bkKv = 0;
	if (kv_status () < 0) return;
	std::vector<char> b (KV_VALUE_MAX + 1);
	long n = kv_get ("bank-key", b.data (), KV_VALUE_MAX);
	if (n < 0 && n != -2) { g_bkKv = 2; return; }
	g_bkKv = 1;
	if (n > 0) g_bk.keyPem.assign (b.data (), (size_t) n);
	std::fill (b.begin (), b.end (), 0);
	bk_key_import ();
	n = kv_get ("bank-session", b.data (), KV_VALUE_MAX);
	if (!g_bk.sessionId.empty ())		// (banque.json en a un : d'avant, ou copié de Windows, le plus récent)
	{
		if (kv_set ("bank-session", g_bk.sessionId.data (), (long) g_bk.sessionId.size ()) >= 0) bk_save_file ();
		else g_bkKv = 2;
	}
	else if (n > 0) g_bk.sessionId.assign (b.data (), (size_t) n);
	std::fill (b.begin (), b.end (), 0);
}
static void bk_load ()
{
	std::string err;
	if (bank::load (BANK_FILE, g_bk, bk_tz (), &err)) { bk_load_vault (); return; }
	g_bkOk = false;
	char m[600];
	snprintf (m, sizeof m, TR ("R\xC3\xA9glages bancaires illisibles (%s) : %s\nLa synchronisation est d\xC3\xA9sactiv\xC3\xA9" "e."), BANK_FILE, err.c_str ());
	ui_message ((long) TR ("Banque"), (long) m, UM_OK);
}
static bool bk_save ()
{
	if (g_bkKv == 1)
	{
		bk_key_import ();
		if (kv_set ("bank-session", g_bk.sessionId.data (), (long) g_bk.sessionId.size ()) < 0) g_bkKv = 2;
	}
	return bk_save_file ();
}
// un message dans la barre d'état, montré tout de suite (avant une requête qui attend la banque)
static void busy (const char *s) { status (s); ui_w_draw (g_lStatus); ui_update_all (); }
static bool web_available () { struct stat st; return stat (WEB_PROGRAM, &st) == 0; }
static bool open_web (const std::string &url)
{
	if (!web_available ()) return false;
	char *argv[] = { (char *) WEB_PROGRAM, (char *) url.c_str (), 0 };
	pid_t p;
	return posix_spawn (&p, WEB_PROGRAM, 0, 0, argv, environ) == 0;
}

// ---- les destinations -------------------------------------------------------------------------------------------
// le texte tapé -> une destination : exacte, le nom seul (« Orange », « Courses »), ou une partie du nom
static bool bk_resolve (const std::vector<std::string> &tg, const std::string &typed, std::string &out)
{
	std::string t = bank::trim (typed);
	if (t.empty ()) return false;
	for (auto &x : tg) if (!strcasecmp (x.c_str (), t.c_str ())) { out = x; return true; }
	std::string n = bank::normalize (t);
	if (n.empty ()) return false;
	for (const char *pre : { bank::LINE_PREFIX, bank::CAT_PREFIX })
		for (auto &x : tg) if (x.compare (0, strlen (pre), pre) == 0 && bank::normalize (x.substr (strlen (pre))) == n) { out = x; return true; }
	for (auto &x : tg) if (bank::normalize (x).find (n) != std::string::npos) { out = x; return true; }
	return false;
}
// pour la complétion de la grille : les noms seuls et les destinations entières
static void bk_complete (const std::vector<std::string> &tg, std::vector<std::string> &out)
{
	for (auto &x : tg)
	{
		for (const char *pre : { bank::LINE_PREFIX, bank::CAT_PREFIX }) if (x.compare (0, strlen (pre), pre) == 0) out.push_back (x.substr (strlen (pre)));
		out.push_back (x);
	}
}
// la liste déroulante de la cellule (grid.h, GChoice) : les postes fixes, puis les colonnes des dépenses courantes,
// chaque groupe sous son titre, puis Ignorer et À classer plus tard ; cur : le choix actuel, coché
static void bk_choices (const std::vector<std::string> &tg, const std::string &cur, std::vector<GChoice> &out)
{
	static const char *const titles[2] = { "Postes fixes", "D\xC3\xA9penses courantes" };
	const char *pres[2] = { bank::LINE_PREFIX, bank::CAT_PREFIX };
	for (int k = 0; k < 2; k++)
	{
		bool first = true;
		for (auto &x : tg)
		{
			if (x.compare (0, strlen (pres[k]), pres[k]) != 0) continue;
			if (first) { GChoice h; h.label = TR (titles[k]); h.group = true; out.push_back (h); first = false; }
			GChoice c; c.label = x.substr (strlen (pres[k])); c.value = x; c.on = x == cur;
			out.push_back (c);
		}
	}
	bool head = false;
	for (const char *o : { bank::IGNORE, bank::LATER })
	{
		if (!bank::in_list (tg, o)) continue;
		if (!head) { GChoice h; h.label = TR ("Autres choix"); h.group = true; out.push_back (h); head = true; }
		GChoice c; c.label = TR (o); c.value = o; c.on = cur == o;
		out.push_back (c);
	}
}

// ---- la fenêtre des opérations à classer (BankImportForm) ---------------------------------------------------------
enum { IC_DATE, IC_TEXT, IC_AMOUNT, IC_TARGET, IC_KEY, IC_REM };
struct ImpRow { bank::Tx tx; std::string target, keyword; bool remember = false, fromRule = false; };
static long g_impInfo, g_impMsg, g_impAllLater;
static int g_impAuto;
struct ImpModel : GridModel
{
	std::vector<ImpRow> r;
	std::vector<std::string> tg;
	bool can_add () override { return false; }
	int rows () override { return (int) r.size (); }
	void changed ()
	{
		int later = 0;
		for (auto &x : r) if (x.target == bank::LATER) later++;
		char b[200];
		snprintf (b, sizeof b, TR ("%d nouvelle(s) op\xC3\xA9ration(s) \xE2\x80\x94 %d reconnue(s) par vos r\xC3\xA8gles \xE2\x80\x94 %d encore \xC3\xA0 classer"), (int) r.size (), g_impAuto, later);
		if (g_impInfo) ui_w_set_text (g_impInfo, (long) b);
	}
	std::string text (int i, int c) override
	{
		const ImpRow &x = r[(size_t) i];
		switch (c)
		{
		case IC_DATE: return fmt_ddd_ddmm (x.tx.day);
		case IC_TEXT: return x.tx.display ();
		case IC_AMOUNT: return num2 (x.tx.amount);
		case IC_TARGET: return x.target;
		case IC_KEY: return x.keyword;
		}
		return "";
	}
	void set_target (int i, const std::string &t)
	{
		r[(size_t) i].target = t;
		// une destination choisie à la main est retenue par défaut (décocher « Retenir » pour un cas isolé)
		if (t != bank::LATER) r[(size_t) i].remember = true;
		changed ();
	}
	const char *set (int i, int c, const std::string &t) override
	{
		if (i >= (int) r.size ()) return 0;
		if (c == IC_KEY) { r[(size_t) i].keyword = bank::trim (t); return 0; }
		if (c == IC_TARGET)
		{
			std::string x;
			if (!bk_resolve (tg, t, x)) return TR ("Destination inconnue \xE2\x80\x94 Alt+Bas ou F4 : la liste \xE2\x80\x94 \xC3\x89" "chap pour annuler");
			set_target (i, x);
		}
		return 0;
	}
	bool checked (int i, int c) override { (void) c; return r[(size_t) i].remember; }
	void toggle (int i, int c) override { (void) c; r[(size_t) i].remember = !r[(size_t) i].remember; }
	void clear (int i, int c) override
	{
		if (c == IC_KEY) r[(size_t) i].keyword.clear ();
		else if (c == IC_REM) r[(size_t) i].remember = false;
		else if (c == IC_TARGET) { r[(size_t) i].target = bank::LATER; changed (); }
	}
	void style (int i, int c, CellStyle &s) override
	{
		const ImpRow &x = r[(size_t) i];
		if (c == IC_AMOUNT && x.tx.amount > 0) s.fg = C_GREEN;
		if (c == IC_TARGET) { if (x.target == bank::LATER) s.fg = C_RED; else if (x.target == bank::IGNORE) s.fg = C_DIM; else if (x.fromRule) s.bold = true; }
	}
	void complete (int c, std::vector<std::string> &out) override { if (c == IC_TARGET) bk_complete (tg, out); }
	bool has_choices (int c) override { return c == IC_TARGET; }
	void choices (int i, int c, std::vector<GChoice> &out) override { (void) c; if (i < (int) r.size ()) bk_choices (tg, r[(size_t) i].target, out); }
	void choose (int i, int c, const std::string &v) override { (void) c; if (i < (int) r.size ()) set_target (i, v); }
	// Menu, Maj+F10, clic droit : la liste déroulante de la destination
	void context (Grid *g, int i, int c, int wx, int wy) override
	{
		(void) c; (void) wx; (void) wy;
		if (i < 0 || i >= (int) r.size ()) return;
		g->go (i, IC_TARGET);
		g->open_drop ();
	}
	void message (const char *s) override { if (g_impMsg) ui_w_set_text (g_impMsg, (long) (s && *s ? s : " ")); }
};
static ImpModel g_mImp;
static Grid g_gImp;
static long imp_fn (long dlg, long w, long code)
{
	(void) dlg;
	if (w == g_impAllLater && code == UN_ACTIVATE)
	{
		g_gImp.cancel_edit ();
		for (auto &x : g_mImp.r) x.target = bank::LATER;
		g_mImp.changed ();
		g_gImp.dirty ();
	}
	ui_view_flush ();
	return 0;
}
static long imp_tick (long) { ui_view_flush (); return 0; }
static void drop_grid (Grid &g) { g.view = 0; for (int i = 0; i < g_ngrids; i++) if (g_grids[i] == &g) { g_grids[i] = g_grids[--g_ngrids]; break; } }

// -> true : les choix sont dans *out
static bool bk_import (Month &m, const std::vector<bank::Tx> &txs, std::vector<bank::Choice> &out)
{
	const int W = 780, c = ctl_h ();
	const int H = g_wh + 10 < 520 ? g_wh + 10 : 520;
	char title[160]; snprintf (title, sizeof title, TR ("Op\xC3\xA9rations bancaires \xC3\xA0 classer \xE2\x80\x94 %s"), m.name.c_str ());
	long dlg = ui_dialog_begin (W, H, (long) title);
	if (!dlg) return false;
	int y = 12;
	{
		const char *hint = TR ("Choisissez o\xC3\xB9 va chaque op\xC3\xA9ration : un poste fixe (le montant s'ajoute \xC3\xA0 \xC2\xAB D\xC3\xA9j\xC3\xA0 pay\xC3\xA9 \xC2\xBB), une colonne des d\xC3\xA9penses courantes, ou Ignorer. "
			"Un clic dans \xC2\xAB Classer dans \xC2\xBB (ou Alt+Bas, F4) ouvre la liste ; on peut aussi taper le nom (ex. Orange, Courses). Cochez \xC2\xAB Retenir \xC2\xBB pour que les prochaines op\xC3\xA9rations avec ce mot-cl\xC3\xA9 soient class\xC3\xA9" "es toutes seules. "
			"\xC2\xAB \xC3\x80 classer plus tard \xC2\xBB laisse l'op\xC3\xA9ration pour la prochaine synchronisation.");
		int hh = (int) ui_text_wrap ((long) hint, 0, 0, W - 32 - FRAME_CAP_W, 0, (long) ui.font, 14, 1);
		ui_label (16, y, W - 32 - FRAME_CAP_W, hh + 4, (long) hint, ULB_WRAP | ULB_DIM);
		y += hh + 8;
	}
	g_impInfo = ui_label (16, y, W - 32, 20, (long) " ", ULB_BOLD);
	y += 24;
	g_mImp = ImpModel ();
	g_mImp.tg = bank::targets (m);
	g_impAuto = 0;
	std::vector<bank::Tx> sorted = txs;
	std::stable_sort (sorted.begin (), sorted.end (), [] (const bank::Tx &a, const bank::Tx &b) { return a.day < b.day; });
	for (auto &t : sorted)
	{
		ImpRow x;
		x.tx = t;
		x.target = bank::suggest (t, g_mImp.tg, g_bk.rules, m, &x.fromRule);
		if (x.fromRule) g_impAuto++;
		x.keyword = bank::suggest_keyword (t);
		x.remember = !x.fromRule && x.target != bank::LATER;
		g_mImp.r.push_back (x);
	}
	int gh = H - y - c - 64;
	g_gImp = Grid ();
	g_gImp.cols = {
		{ "Date", 78, CK_DATE, true, false, 0 },
		{ "Op\xC3\xA9ration", 180, CK_TEXT, true, true, 0 },
		{ "Montant", 80, CK_MONEY, true, false, 0 },
		{ "Classer dans", 180, CK_TEXT, false, false, 0 },
		{ "Mot-cl\xC3\xA9", 110, CK_TEXT, false, false, 0 },
		{ "Retenir", 68, CK_CHECK, false, false, 0 } };
	grid_create (g_gImp, &g_mImp);
	g_gImp.place (16, y, W - 32, gh);
	y += gh + 4;
	g_impMsg = ui_label (16, y, W - 32, 20, (long) " ", ULB_DIM);
	g_mImp.changed ();
	// la première encore à classer, sur sa destination
	for (int i = 0; i < (int) g_mImp.r.size (); i++) if (g_mImp.r[(size_t) i].target == bank::LATER) { g_gImp.curR = i; break; }
	g_gImp.curC = IC_TARGET;
	g_gImp.ensure_visible ();
	int edge = ui_dialog_button (dlg, 0, (long) TR ("Appliquer"), 1, 0, W - 16);
	edge = ui_dialog_button (dlg, 1, (long) TR ("Annuler"), 0, 0, edge);
	int bw = text_w (TR ("Tout \xC2\xAB \xC3\xA0 classer plus tard \xC2\xBB"), 1) + 28;
	g_impAllLater = ui_pushbutton (16, H - c - 14, bw, c, (long) TR ("Tout \xC2\xAB \xC3\xA0 classer plus tard \xC2\xBB"), 0);
	(void) edge;
	ui_dialog_cancel (dlg, 0);
	ui_dialog_timer (dlg, (long) imp_tick, 100);
	ui_w_focus (g_gImp.view);
	ui_view_dirty (g_gImp.view);
	long r = ui_dialog_run (dlg, (long) imp_fn, 0);
	if (r == 1)
	{
		g_gImp.commit ();
		out.clear ();
		for (auto &x : g_mImp.r) { bank::Choice ch; ch.tx = x.tx; ch.target = x.target; ch.keyword = x.keyword; ch.remember = x.remember; out.push_back (ch); }
	}
	ui_dialog_end (dlg);
	drop_grid (g_gImp);
	g_impInfo = g_impMsg = g_impAllLater = 0;
	return r == 1;
}

// ---- relier le compte (BankLink) ----------------------------------------------------------------------------------
static long g_lkPaste, g_lkErr, g_lkOk, g_lkWeb, g_lkUrl;
static std::string g_lkState, g_lkCode, g_lkAuthUrl;
static bank::Listener *g_lkListen;
static bool g_lkFollowDone;
static bool lk_try (long dlg, const std::string &addr)
{
	std::string msg;
	if (bank::code_from (addr, g_lkState, g_lkCode, msg)) { ui_dialog_close (dlg, 1); return true; }
	ui_w_set_text (g_lkErr, (long) msg.c_str ());
	return false;
}
static long lk_fn (long dlg, long w, long code)
{
	if ((w == g_lkOk || w == g_lkPaste) && code == UN_ACTIVATE) lk_try (dlg, (const char *) ui_w_get_text (g_lkPaste));
	else if (w == g_lkWeb && code == UN_ACTIVATE && !open_web (g_lkAuthUrl))
		ui_w_set_text (g_lkErr, (long) TR ("Jet ne d\xC3\xA9marre pas : ouvrez l'adresse sur un autre appareil."));
	return 0;
}
static long lk_tick (long dlg)
{
	// essais : l'adresse du faux serveur suivie sans navigateur
	if (g_bkFollow && !g_lkFollowDone)
	{
		g_lkFollowDone = true;
		std::string back = bank::follow_once (g_lkAuthUrl);
		ui_w_set_text (g_lkPaste, (long) back.c_str ());
		lk_try (dlg, back);
		return 0;
	}
	if (g_lkListen)
	{
		std::string got = g_lkListen->poll_once ();
		if (!got.empty ()) lk_try (dlg, got);
	}
	return 0;
}
// -> true : relié (g_bk enregistré)
static bool bk_link ()
{
	bank::Client cl; cl.tz = bk_tz ();
	if (!cl.open (g_bk, DIR_, g_bkApi)) { ui_message ((long) TR ("Banque"), (long) cl.err.c_str (), UM_OK); return false; }
	busy (TR ("Connexion \xC3\xA0 Enable Banking\xE2\x80\xA6"));
	std::string bankName; long long maxAccess = 0;
	g_lkState = bank::new_state ();
	if (!cl.find_bank (g_bk.bankName, g_bk.country, bankName, maxAccess) ||
	    !cl.start_auth (bankName, g_bk.country, bank::utc_now () + maxAccess, g_bk.redirect, g_lkState, g_lkAuthUrl))
	{
		status (" ");
		ui_message ((long) TR ("Banque"), (long) cl.err.c_str (), UM_OK);
		return false;
	}
	status (" ");
	bank::Listener listen;
	bool listening = listen.start (g_bk.redirect);
	g_lkListen = listening ? &listen : 0;
	g_lkFollowDone = false;
	g_lkCode.clear ();
	bool web = web_available () && !g_bkFollow;
	const int W = 640, c = ctl_h ();
	std::string info = std::string (TR (web ? "La page de votre banque s'ouvre dans Jet." : "Ouvrez cette adresse dans un navigateur, ici ou sur un autre appareil :")) + "\n";
	int hInfo = 22;
	std::string steps = TR ("1. Connectez-vous et confirmez l'acc\xC3\xA8s en lecture (itsme ou lecteur de carte).\n"
		"2. Au retour, la banque vous envoie sur l'adresse de retour : si elle s'ouvre sur ce syst\xC3\xA8me, cette fen\xC3\xAAtre se ferme toute seule.\n"
		"Sinon, copiez l'adresse compl\xC3\xA8te de la page o\xC3\xB9 vous \xC3\xAAtes arriv\xC3\xA9 (elle contient \xC2\xAB code= \xC2\xBB) et collez-la ici :");
	int hSteps = (int) ui_text_wrap ((long) steps.c_str (), 0, 0, W - 32 - FRAME_CAP_W, 0, (long) ui.font, 14, 1);
	int H = 14 + hInfo + 4 + c + 8 + (web ? c + 8 : 0) + hSteps + 8 + c + 8 + 40 + c + 24;
	long dlg = ui_dialog_begin (W, H, (long) TR ("Autorisation de la banque"));
	if (!dlg) return false;
	int y = 14;
	ui_label (16, y, W - 32 - FRAME_CAP_W, hInfo, (long) info.c_str (), 0); y += hInfo + 4;
	g_lkUrl = ui_textbox (16, y, W - 32, c, UTF_READONLY);
	ui_w_set_text (g_lkUrl, (long) g_lkAuthUrl.c_str ());
	y += c + 8;
	g_lkWeb = 0;
	if (web) { g_lkWeb = ui_pushbutton (16, y, text_w (TR ("Ouvrir dans Jet"), 1) + 32, c, (long) TR ("Ouvrir dans Jet"), 0); y += c + 8; }
	ui_label (16, y, W - 32 - FRAME_CAP_W, hSteps + 4, (long) steps.c_str (), ULB_WRAP | ULB_DIM); y += hSteps + 8;
	int okW = text_w (TR ("Valider"), 1) + 32;
	g_lkPaste = ui_textbox (16, y, W - 32 - okW - 8, c, 0);
	g_lkOk = ui_pushbutton (W - 16 - okW, y, okW, c, (long) TR ("Valider"), 0);
	y += c + 8;
	g_lkErr = ui_label (16, y, W - 32, 40, (long) " ", ULB_WRAP | ULB_BOLD);
	ui_dialog_button (dlg, 0, (long) TR ("Annuler"), 0, 0, W - 16);
	ui_dialog_cancel (dlg, 0);
	ui_dialog_timer (dlg, (long) lk_tick, 250);
	ui_w_focus (g_lkPaste);
	if (web) open_web (g_lkAuthUrl);
	long r = ui_dialog_run (dlg, (long) lk_fn, 0);
	ui_dialog_end (dlg);
	g_lkListen = 0;
	listen.stop ();
	if (r != 1 || g_lkCode.empty ()) return false;

	busy (TR ("Ouverture de la session bancaire\xE2\x80\xA6"));
	bank::Session ses;
	if (!cl.create_session (g_lkCode, ses)) { status (" "); ui_message ((long) TR ("Banque"), (long) cl.err.c_str (), UM_OK); return false; }
	status (" ");
	if (ses.accounts.empty ()) { ui_message ((long) TR ("Banque"), (long) TR ("La banque n'a donn\xC3\xA9 acc\xC3\xA8s \xC3\xA0 aucun compte."), UM_OK); return false; }
	size_t pick = 0;
	if (ses.accounts.size () > 1)
	{
		// quel compte suivre ? (la liste au milieu de la fenêtre)
		std::string l;
		for (auto &a : ses.accounts) { l += a.label; l += '\0'; }
		int k = popup (0, g_ww / 2 - 160, g_wh / 3, l.c_str ());
		if (k < 0 || k >= (int) ses.accounts.size ()) return false;
		pick = (size_t) k;
	}
	g_bk.sessionId = ses.id;
	g_bk.validUntil = ses.validUntil ? ses.validUntil : bank::utc_now () + maxAccess;
	g_bk.accountUid = ses.accounts[pick].uid;
	g_bk.accountLabel = ses.accounts[pick].label;
	bk_save ();
	return true;
}

// ---- les réglages (BankSettingsForm) ------------------------------------------------------------------------------
enum { RC_KEY, RC_TARGET };
struct RulesModel : GridModel
{
	std::vector<bank::Rule> r;
	std::vector<std::string> tg;
	int rows () override { return (int) r.size (); }
	std::string text (int i, int c) override { return c == RC_KEY ? r[(size_t) i].keyword : r[(size_t) i].target; }
	const char *set (int i, int c, const std::string &t) override
	{
		std::string v = bank::trim (t);
		if (c == RC_TARGET && !v.empty ())
		{
			std::string x;
			if (!bk_resolve (tg, v, x)) return TR ("Destination inconnue \xE2\x80\x94 Alt+Bas ou F4 : la liste \xE2\x80\x94 \xC3\x89" "chap pour annuler");
			v = x;
		}
		if (i >= (int) r.size ()) { if (v.empty ()) return 0; r.push_back (bank::Rule ()); i = (int) r.size () - 1; }
		if (c == RC_KEY) r[(size_t) i].keyword = v; else r[(size_t) i].target = v;
		return 0;
	}
	void clear (int i, int c) override { if (i < (int) r.size ()) { if (c == RC_KEY) r[(size_t) i].keyword.clear (); else r[(size_t) i].target.clear (); } }
	void complete (int c, std::vector<std::string> &out) override { if (c == RC_TARGET) bk_complete (tg, out); }
	void delete_row (int i) override { if (i >= 0 && i < (int) r.size ()) r.erase (r.begin () + i); }
	bool has_choices (int c) override { return c == RC_TARGET; }
	void choices (int i, int c, std::vector<GChoice> &out) override { (void) c; bk_choices (tg, i < (int) r.size () ? r[(size_t) i].target : std::string (), out); }
	void choose (int i, int c, const std::string &v) override
	{
		(void) c;
		if (i >= (int) r.size ()) { r.push_back (bank::Rule ()); i = (int) r.size () - 1; }
		r[(size_t) i].target = v;
	}
	void context (Grid *g, int i, int c, int wx, int wy) override
	{
		(void) c;
		static const char items[] = "Choisir la destination\xE2\x80\xA6\0Supprimer la r\xC3\xA8gle\0";
		int k = popup (g, wx, wy, items);
		if (k == 1) { delete_row (i); g->clamp (); g->dirty (); }
		else if (k == 0) { g->go (i, RC_TARGET); g->open_drop (); }
	}
};
static RulesModel g_mRules;
static Grid g_gRules;
static long g_stForget, g_stState, g_stLink, g_stBrowse;
static bank::Settings *g_stEdit;
static void st_state ()
{
	char b[300];
	if (bank::linked (*g_stEdit))
	{
		int day = (int) ((g_stEdit->validUntil + bk_tz ()) / 86400);
		snprintf (b, sizeof b, TR ("Reli\xC3\xA9 : %s \xE2\x80\x94 acc\xC3\xA8s valable jusqu'au %s"), g_stEdit->accountLabel.c_str (), fmt_ddmmyyyy (day).c_str ());
	}
	else snprintf (b, sizeof b, "%s", TR (!g_stEdit->sessionId.empty () ? "Acc\xC3\xA8s expir\xC3\xA9 : reliez \xC3\xA0 nouveau." : "Pas encore reli\xC3\xA9."));
	ui_w_set_text (g_stState, (long) b);
}
static long st_fn (long dlg, long w, long code)
{
	// Relier et Parcourir ferment le dialogue (2, 3) : leur propre fenêtre s'ouvre, puis celle-ci revient
	if (w == g_stLink && code == UN_ACTIVATE) { ui_dialog_close (dlg, 2); return 0; }
	if (w == g_stBrowse && code == UN_ACTIVATE) { ui_dialog_close (dlg, 3); return 0; }
	if (w == g_stForget && code == UN_ACTIVATE)
	{
		g_stEdit->sessionId.clear (); g_stEdit->accountUid.clear (); g_stEdit->accountLabel.clear (); g_stEdit->validUntil = 0;
		st_state ();
	}
	ui_view_flush ();
	return 0;
}
static long st_tick (long) { ui_view_flush (); return 0; }
static void cmd_bank_settings ()
{
	if (!g_bkOk) { ui_message ((long) TR ("Banque"), (long) TR ("banque.json est illisible : corrigez-le ou retirez-le, puis relancez Budget."), UM_OK); return; }
	g_gLines.cancel_edit (); g_gSpend.cancel_edit ();
	bank::Settings s = g_bk;		// les modifications, gardées d'une ouverture à l'autre (Relier, Parcourir)
	// destinations connues : celles du mois affiché et celles du modèle
	std::vector<std::string> tg;
	{
		Month fromTpl; fromTpl.lines = g_d.tpl.lines; fromTpl.categories = g_d.tpl.categories;
		if (Month *m = cur ()) tg = bank::targets (*m);
		for (auto &x : bank::targets (fromTpl)) if (!bank::in_list (tg, x)) tg.push_back (x);
		for (auto &rl : s.rules) if (!rl.target.empty () && !bank::in_list (tg, rl.target)) tg.push_back (rl.target);
		tg.erase (std::remove (tg.begin (), tg.end (), std::string (bank::LATER)), tg.end ());
	}
	for (;;)
	{
		const int W = 700, c = ctl_h ();
		const int H = g_wh + 10 < 560 ? g_wh + 10 : 560;
		long dlg = ui_dialog_begin (W, H, (long) TR ("Banque \xE2\x80\x94 liaison Enable Banking"));
		if (!dlg) return;
		int y = 12;
		const char *hint = TR ("1. Sur enablebanking.com, cr\xC3\xA9" "ez une application (production) et t\xC3\xA9l\xC3\xA9" "chargez sa cl\xC3\xA9 priv\xC3\xA9" "e (.pem).\n"
			"2. D\xC3\xA9" "clarez dans l'application exactement la m\xC3\xAAme \xC2\xAB adresse de retour \xC2\xBB que ci-dessous.\n"
			"3. Indiquez l'identifiant et la cl\xC3\xA9, puis \xC2\xAB Relier mon compte \xC2\xBB. Un acc\xC3\xA8s reli\xC3\xA9 par l'app Windows sert tel quel : "
			"copiez banque.json et la cl\xC3\xA9 dans HOME:/budget.");
		int hh = (int) ui_text_wrap ((long) hint, 0, 0, W - 32 - FRAME_CAP_W, 0, (long) ui.font, 14, 1);
		ui_label (16, y, W - 32 - FRAME_CAP_W, hh + 4, (long) hint, ULB_WRAP | ULB_DIM);
		y += hh + 10;
		int lw = text_w (TR ("Identifiant de l'application :")) + 14, fw = W - 32 - lw;
		auto row = [&] (const char *label, const std::string &v, int w) -> long
		{
			ui_label (16, y, lw, c, (long) TR (label), 0);
			long t = ui_textbox (16 + lw, y, w, c, 0);
			ui_w_set_text (t, (long) v.c_str ());
			y += c + 6;
			return t;
		};
		long tApp = row ("Identifiant de l'application :", s.appId, fw);
		int browseW = text_w (TR ("Parcourir\xE2\x80\xA6"), 1) + 28;
		long tKey = row ("Cl\xC3\xA9 priv\xC3\xA9" "e (.pem) :", s.keyPath, fw - browseW - 8);
		long bBrowse = ui_pushbutton (W - 16 - browseW, y - c - 6, browseW, c, (long) TR ("Parcourir\xE2\x80\xA6"), 0);
		long tRedir = row ("Adresse de retour :", s.redirect, fw);
		long tBank = row ("Banque :", s.bankName, 240);
		long tCountry = row ("Pays :", s.country, 60);
		int linkW = text_w (TR ("Relier mon compte"), 1) + 28, forgetW = text_w (TR ("Oublier l'acc\xC3\xA8s"), 1) + 28;
		long bLink = ui_pushbutton (16, y, linkW, c, (long) TR ("Relier mon compte"), 0);
		g_stForget = ui_pushbutton (16 + linkW + 8, y, forgetW, c, (long) TR ("Oublier l'acc\xC3\xA8s"), 0);
		g_stState = ui_label (16 + linkW + forgetW + 24, y, W - 48 - linkW - forgetW, c, (long) " ", ULB_BOLD);
		g_stEdit = &s;
		st_state ();
		y += c + 12;
		ui_label (16, y, W - 32, 22, (long) TR ("R\xC3\xA8gles de classement retenues (Ctrl+Suppr pour en retirer une)"), ULB_BOLD);
		y += 24;
		g_mRules = RulesModel ();
		g_mRules.r = s.rules;
		std::stable_sort (g_mRules.r.begin (), g_mRules.r.end (), [] (const bank::Rule &a, const bank::Rule &b) { return strcasecmp (a.keyword.c_str (), b.keyword.c_str ()) < 0; });
		g_mRules.tg = tg;
		g_gRules = Grid ();
		g_gRules.cols = { { "Si l'op\xC3\xA9ration contient\xE2\x80\xA6", 220, CK_TEXT, false, false, 0 }, { "\xE2\x80\xA6 classer dans", 300, CK_TEXT, false, true, 0 } };
		grid_create (g_gRules, &g_mRules);
		g_gRules.place (16, y, W - 32, H - y - c - 30);
		int edge = ui_dialog_button (dlg, 0, (long) TR ("Enregistrer"), 1, 0, W - 16);
		ui_dialog_button (dlg, 1, (long) TR ("Annuler"), 0, 0, edge);
		ui_dialog_cancel (dlg, 0);
		ui_dialog_timer (dlg, (long) st_tick, 100);
		g_stLink = bLink; g_stBrowse = bBrowse;
		ui_w_focus (tApp);
		long r = ui_dialog_run (dlg, (long) st_fn, 0);
		g_gRules.commit ();
		if (r != 0)
		{
			s.appId = bank::trim ((const char *) ui_w_get_text (tApp));
			std::string k = bank::trim ((const char *) ui_w_get_text (tKey));
			if (k.size () >= 2 && k.front () == '"' && k.back () == '"') k = k.substr (1, k.size () - 2);
			s.keyPath = k;
			s.redirect = bank::trim ((const char *) ui_w_get_text (tRedir));
			s.bankName = bank::trim ((const char *) ui_w_get_text (tBank));
			s.country = bank::trim ((const char *) ui_w_get_text (tCountry));
			for (auto &ch : s.country) ch = (char) toupper ((unsigned char) ch);
			s.rules.clear ();
			for (auto &rl : g_mRules.r) if (!bank::trim (rl.keyword).empty () && !rl.target.empty ()) s.rules.push_back (rl);
		}
		ui_dialog_end (dlg);
		drop_grid (g_gRules);
		if (r == 0) return;			// Annuler : rien ne change
		if (r == 3)
		{
			char p[512]; snprintf (p, sizeof p, "%s", s.keyPath.c_str ());
			if (ui_file_open ((long) DIR_, (long) p, sizeof p) == 1) s.keyPath = p;
			continue;
		}
		if (r == 2)
		{
			if (!bank::configured (s, DIR_))
			{
				ui_message ((long) TR ("Banque"), (long) TR ("Indiquez l'identifiant de l'application et un fichier de cl\xC3\xA9 priv\xC3\xA9" "e existant."), UM_OK);
				continue;
			}
			bank::Settings keep = g_bk;
			g_bk = s;
			if (bk_link ()) s = g_bk;		// (enregistré)
			else g_bk = keep;
			continue;
		}
		g_bk = s;
		if (!bk_save ()) ui_message ((long) TR ("Banque"), (long) TR ("Enregistrement de banque.json impossible."), UM_OK);
		return;
	}
}

// ---- synchroniser (MainForm.SyncBank) -----------------------------------------------------------------------------
// le solde réel de la banque dans le suivi, puis les nouvelles opérations du mois à classer
static void cmd_bank_sync ()
{
	Month *month = cur ();
	if (g_syncing || !month) return;
	if (!g_bkOk) { cmd_bank_settings (); return; }
	if (!bank::configured (g_bk, DIR_) || !bank::linked (g_bk))
	{
		const char *msg = !bank::configured (g_bk, DIR_)
			? TR ("La banque n'est pas encore configur\xC3\xA9" "e.\n\nOuvrir les r\xC3\xA9glages \xC2\xAB Banque \xC2\xBB ?")
			: TR ("L'acc\xC3\xA8s \xC3\xA0 votre compte a expir\xC3\xA9 ou n'a pas encore \xC3\xA9t\xC3\xA9 donn\xC3\xA9.\n\nOuvrir les r\xC3\xA9glages pour relier le compte ?");
		if (ui_message ((long) TR ("Banque"), (long) msg, UM_YESNO) == 1) cmd_bank_settings ();
		month = cur ();
		if (!month || !bank::configured (g_bk, DIR_) || !bank::linked (g_bk)) return;
	}
	g_gLines.commit (); g_gSpend.commit ();
	g_gLines.cancel_edit (); g_gSpend.cancel_edit ();
	g_syncing = true;
	busy (TR ("Synchronisation avec la banque\xE2\x80\xA6"));
	bank::Client cl; cl.tz = bk_tz ();
	std::string summary = TR ("aucune nouvelle op\xC3\xA9ration"), err;
	Money bal = 0;
	int pending = 0;
	bool ok = cl.open (g_bk, DIR_, g_bkApi) && cl.balance (g_bk.accountUid, bal);
	if (ok)
	{
		bool inPeriod = g_today >= month->start && g_today < month->end;
		char q[400];
		snprintf (q, sizeof q, TR ("\xC2\xAB %s \xC2\xBB n'est pas la p\xC3\xA9riode en cours.\n\nMettre quand m\xC3\xAAme le solde actuel (%s) dans son suivi ?"), month->name.c_str (), eur (bal).c_str ());
		if (inPeriod || ui_message ((long) TR ("Banque"), (long) q, UM_YESNO) == 1) month->bank = bal;
		std::vector<bank::Tx> all, fresh;
		ok = cl.transactions (g_bk.accountUid, month->start, g_today, all);
		if (ok)
		{
			for (auto &t : all)
			{
				if (t.day < month->start || t.day >= month->end) continue;
				if (!t.booked) { pending++; continue; }
				if (!g_bk.has_seen (t.key)) fresh.push_back (t);
			}
			g_bk.lastSync = bank::utc_now ();
			if (!fresh.empty ())
			{
				status (" ");
				std::vector<bank::Choice> ch;
				summary = bk_import (*month, fresh, ch) ? bank::apply (*month, ch, g_bk) : TR ("classement annul\xC3\xA9");
			}
			if (!bk_save ()) summary += TR (" \xE2\x80\x94 banque.json pas enregistr\xC3\xA9 !");
		}
	}
	g_syncing = false;
	if (!ok)
	{
		std::string m = std::string (TR ("Synchronisation impossible : ")) + cl.err;
		status (m.c_str ());
		ui_message ((long) TR ("Synchronisation bancaire"), (long) cl.err.c_str (), UM_OK);
		return;
	}
	load_month (g_cur);
	save_now ();				// (tout de suite : son message ne remplace pas le résumé ci-dessous)
	char b[700];
	long s = local_seconds () % 86400;
	std::string note;
	if (pending > 0) { char n[160]; snprintf (n, sizeof n, TR (" \xE2\x80\x94 %d op\xC3\xA9ration(s) en attente \xC3\xA0 la banque, propos\xC3\xA9" "es une fois comptabilis\xC3\xA9" "es"), pending); note += n; }
	if (g_bk.validUntil && g_bk.validUntil < bank::utc_now () + 7 * 86400)
	{
		char n[160]; snprintf (n, sizeof n, TR (" \xE2\x80\x94 l'acc\xC3\xA8s bancaire expire le %s : renouvelez-le dans Banque > R\xC3\xA9glages"), fmt_ddmm ((int) ((g_bk.validUntil + bk_tz ()) / 86400)).c_str ());
		note += n;
	}
	snprintf (b, sizeof b, TR ("Banque synchronis\xC3\xA9" "e \xC3\xA0 %02ld:%02ld \xE2\x80\x94 solde %s \xE2\x80\x94 %s%s"), s / 3600, s / 60 % 60, eur (bal).c_str (), summary.c_str (), note.c_str ());
	status (b);
}
