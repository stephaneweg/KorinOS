/*
 * applet.cpp -- le volet « Comptes en ligne » de Réglages, installé avec oauthd (docs/OAUTH.md) :
 * /sys/applets/accounts.so, chargé par Réglages (src/apps/control/control.h, « les applets installées »).
 *
 *   * la liste des comptes OAuth d'oauthd (oauth_list : ceux de ce volet, de Fichiers « gdrive/NOM », de git
 *     « github »…) : nom, fournisseur, état ;
 *   * Ajouter… : un fournisseur connu (Google, Microsoft, GitHub), le nom du compte, le Client ID (celui de Korin,
 *     /etc/oauth/providers.ini, s'il y en a un ; pour GitHub, l'OAuth App de l'utilisateur, à créer dans Jet) et les
 *     permissions, puis la connexion ;
 *   * Se connecter : par le code d'appareil (Microsoft, GitHub : un code à taper sur un téléphone ou dans Jet) ou
 *     par le navigateur (Google : Jet s'ouvre sur la page d'accord ; adresse de retour collée pour un autre
 *     appareil) ;
 *   * Se déconnecter (oauth_forget : la configuration reste), Retirer (oauth_remove).
 * Sans libc ni libstdc++ à lui : celles de Réglages ; les fonctions oauth_* sont les relais de NetKit de Réglages
 * (relié en hôte, -rdynamic ; il appelle netkit_kit_open au démarrage). Elles attendent le réseau (20 s au plus) :
 * les appels qui le demandent sont précédés d'un message dans la ligne d'état.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <netkit_abi.h>
#include "control.h"

enum { AC_BTN = 28, AC_CT = 24, AC_TOPS = 16, AC_MAX = 64, E_AGAIN_ = -11, E_NOENT_ = -2 };

#define OAUTHD		"/sys/svc/oauthd"
#define PROVIDERS	"/etc/oauth/providers.ini"
#define JET		"/apps/jet.app/main"
#define GH_NEW_APP	"https://github.com/settings/applications/new"

/* les fournisseurs connus d'oauthd */
struct Prov
{
	const char *id, *label, *account, *scope, *note;
	int browser;				/* 1 : par le navigateur (Google limite le code d'appareil) */
};
static const Prov provs[] = {
	{ "google", "Google", "gdrive/perso", "",
	  "Google Drive : Fichiers › Volume › Se connecter à Google Drive… se sert d'un compte « gdrive/NOM ».", 1 },
	{ "microsoft", "Microsoft (Outlook, OneDrive)", "microsoft/perso", "offline_access User.Read",
	  "Ajoutez aux permissions celles du service voulu (Files.ReadWrite pour OneDrive…).", 0 },
	{ "github", "GitHub", "github", "repo workflow",
	  "git se sert du compte « github » pour pousser vers https://github.com (docs/GIT.md).", 0 },
};
enum { NPROV = sizeof provs / sizeof provs[0] };

static long tops[AC_TOPS];
static int ntop;
static long w_list, w_status, w_info;
static struct Acc { char name[64], prov[32]; int st; } accs[AC_MAX];
static int nacc;
static char buf[16384];

/* ---- les widgets ---- */

static long top (long w)
{
	if (ntop < AC_TOPS)
		tops[ntop++] = w;
	return w;
}

static long in (long g, long w)
{
	ui_w_set_parent (w, g);
	return w;
}

typedef void (*ac_fn) (long w, long code);

static long button (long g, long x, long y, long w, const char *text, ac_fn fn)
{
	long b = in (g, ui_pushbutton (x, y, w, AC_BTN, (long) TR (text), 0));
	ui_w_on (b, (long) fn, 0);
	return b;
}

static void ac_close (void)
{
	for (int i = 0; i < ntop; i++)
		ui_w_destroy (tops[i]);
	ntop = 0;
}

static void ac_draw (void) { ui_w_draw (0); }

static void status (const char *text)
{
	ui_w_set_text (w_status, (long) text);
	ui_w_draw (w_status);
}

/* le texte de la dernière erreur d'oauthd, après what */
static void fail (const char *what, long r)
{
	char e[300] = "", m[600];
	if (oauth_error ((long) e, sizeof e) < 0)
		e[0] = 0;
	snprintf (m, sizeof m, "%s (%ld)%s%s", TR (what), r, e[0] ? "\n" : "", e);
	ui_message ((long) TR ("Comptes en ligne"), (long) m, UM_OK);
}

static const Prov *prov_of (const char *id)
{
	for (int i = 0; i < NPROV; i++)
		if (!strcasecmp (provs[i].id, id))
			return &provs[i];
	return 0;
}

/* /etc/oauth/providers.ini donne-t-il un client_id (rempli) pour ce fournisseur ? */
static int korin_client (const char *id)
{
	FILE *f = fopen (PROVIDERS, "r");
	if (!f)
		return 0;
	char line[600];
	int in_sec = 0, ok = 0;
	while (fgets (line, sizeof line, f)) {
		char *s = ini_trim (line);
		if (*s == '[') {
			char *e = strchr (s, ']');
			if (e)
				*e = 0;
			in_sec = !strcasecmp (s + 1, id);
			continue;
		}
		if (!in_sec || *s == '#' || *s == ';')
			continue;
		char *eq = strchr (s, '=');
		if (!eq)
			continue;
		*eq = 0;
		char *k = ini_trim (s), *v = ini_trim (eq + 1);
		size_t n = strlen (v);
		if (!strcasecmp (k, "client_id") && n && !(n >= 2 && v[0] == '@' && v[n - 1] == '@'))
			ok = 1;
	}
	fclose (f);
	return ok;
}

static int jet_here (void) { return access (JET, F_OK) == 0; }

static void open_jet (const char *url)
{
	if (spawn (JET, url) < 0)
		ui_message ((long) TR ("Comptes en ligne"), (long) TR ("Le navigateur (Jet) est introuvable."), UM_OK);
}

/* ---- la liste ---- */

static int sel (void)
{
	long v = ui_w_get_value (w_list);
	return v >= 0 && v < nacc ? (int) v : -1;
}

static void show_info (void)
{
	int i = sel ();
	static char t[1200];
	t[0] = 0;
	if (i >= 0) {
		const Prov *p = prov_of (accs[i].prov);
		long r = oauth_info ((long) accs[i].name, (long) buf, sizeof buf - 1);
		const char *cid = "", *sc = "";
		static char cid_b[200], sc_b[300];
		if (r >= 0) {
			buf[r < (long) sizeof buf ? r : (long) sizeof buf - 1] = 0;
			for (char *l = buf; *l;) {
				char *e = l + strcspn (l, "\n");
				char sv = *e;
				*e = 0;
				if (!strncmp (l, "client_id = ", 12))
					snprintf (cid_b, sizeof cid_b, "%s", l + 12), cid = cid_b;
				else if (!strncmp (l, "scope = ", 8))
					snprintf (sc_b, sizeof sc_b, "%s", l + 8), sc = sc_b;
				*e = sv;
				l = *e ? e + 1 : e;
			}
		}
		snprintf (t, sizeof t, "%s : %s     %s : %s\n%s", TR ("Client ID"), *cid ? cid : "—", TR ("Permissions"),
			  *sc ? sc : "—", p ? TR (p->note) : "");
	}
	ui_w_set_text (w_info, (long) t);
	ui_w_draw (0);
}

static void load (const char *keep)
{
	nacc = 0;
	long r = oauth_list ((long) buf, sizeof buf - 1);
	if (r < 0) {
		fail ("Le service des comptes (oauthd) ne répond pas", r);
		r = 0;
	}
	buf[r < (long) sizeof buf ? r : (long) sizeof buf - 1] = 0;
	for (char *l = buf; *l && nacc < AC_MAX;) {
		char *e = l + strcspn (l, "\n");
		char *t1 = (char *) memchr (l, '\t', (size_t) (e - l));
		char *t2 = t1 ? (char *) memchr (t1 + 1, '\t', (size_t) (e - t1 - 1)) : 0;
		if (t1 && t2) {
			Acc *a = &accs[nacc++];
			snprintf (a->name, sizeof a->name, "%.*s", (int) (t1 - l), l);
			a->st = atoi (t1 + 1);
			snprintf (a->prov, sizeof a->prov, "%.*s", (int) (e - t2 - 1), t2 + 1);
		}
		l = *e ? e + 1 : e;
	}
	ui_list_clear (w_list);
	int k = 0;
	for (int i = 0; i < nacc; i++) {
		const Prov *p = prov_of (accs[i].prov);
		char row[300];
		snprintf (row, sizeof row, "%s\t%s\t%s", accs[i].name, p ? p->label : accs[i].prov,
			  TR (accs[i].st ? "connecté" : "à connecter"));
		ui_list_add (w_list, (long) row, i);
		if (keep && !strcmp (keep, accs[i].name))
			k = i;
	}
	ui_w_set_value (w_list, nacc ? k : -1);
	show_info ();
}

/* ---- se connecter ---- */

/* l'attente : oauth_*_poll une fois par seconde depuis le minuteur du dialogue */
static struct { const char *acc; int browser; long err, last, until, wait; } wt;

static long wt_tick (long d)
{
	long t = sys_ms ();
	if (t - wt.last < 950)
		return 0;
	wt.last = t;
	long r = wt.browser ? oauth_browser_poll ((long) wt.acc) : oauth_device_poll ((long) wt.acc);
	if (r == 0)
		ui_dialog_close (d, 1);
	else if (r != E_AGAIN_) {
		wt.err = r;
		ui_dialog_close (d, 2);
	} else if (wt.until && wt.wait) {
		char m[120];
		long left = (wt.until - t) / 1000;
		snprintf (m, sizeof m, "%s (%ld:%02ld)", TR ("En attente de l'accord…"), left / 60 > 0 ? left / 60 : 0,
			  left > 0 ? left % 60 : 0);
		ui_w_set_text (wt.wait, (long) m);
		ui_w_draw (wt.wait);
	}
	return 0;
}

/* le code d'appareil -> 1 connecté */
static int connect_device (const char *acc)
{
	status (TR ("Demande d'un code…"));
	long r = oauth_device_begin ((long) acc, (long) buf, sizeof buf - 1);
	if (r < 0) {
		status ("");
		fail ("Connexion impossible", r);
		return 0;
	}
	buf[r < (long) sizeof buf ? r : (long) sizeof buf - 1] = 0;
	char *f[5] = { 0 };
	char *p = buf;
	for (int i = 0; i < 5 && p && *p; i++) {
		f[i] = p;
		p = strchr (p, '\n');
		if (p)
			*p++ = 0;
	}
	static char ucode[64], uri[512], full[1024];
	snprintf (ucode, sizeof ucode, "%s", f[0] ? f[0] : "");
	snprintf (uri, sizeof uri, "%s", f[1] ? f[1] : "");
	snprintf (full, sizeof full, "%s", f[2] && *f[2] ? f[2] : uri);
	long secs = f[3] ? atol (f[3]) : 900;

	enum { DW = 520, DH = 270, M = 16 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("Se connecter"));
	if (!d)
		return 0;
	long y = 16;
	ui_label (M, y, DW - 2 * M, 20, (long) TR ("Sur un téléphone ou un ordinateur, ouvrez :"), 0);
	y += 26;
	long t_uri = ui_textbox (M, y, DW - 2 * M - 150, AC_BTN, UTF_READONLY);
	ui_w_set_text (t_uri, (long) uri);
	long b_jet = ui_pushbutton (DW - M - 140, y, 140, AC_BTN, (long) TR ("Ouvrir dans Jet"), 0);
	ui_w_enable (b_jet, jet_here ());
	y += 42;
	ui_label (M, y, DW - 2 * M, 20, (long) TR ("et tapez le code :"), 0);
	y += 26;
	long t_code = ui_textbox (M, y, 200, AC_BTN + 6, UTF_READONLY | UTF_MONO);
	ui_w_set_text (t_code, (long) ucode);
	long b_copy = ui_pushbutton (M + 212, y + 3, 110, AC_BTN, (long) TR ("Copier"), 0);
	y += 48;
	wt.wait = ui_label (M, y, DW - 2 * M, 20, (long) TR ("En attente de l'accord…"), ULB_DIM);
	ui_dialog_button (d, 0, (long) TR ("Annuler"), 0, 0, DW - 12);
	ui_dialog_cancel (d, 0);
	wt.acc = acc;
	wt.browser = 0;
	wt.err = 0;
	wt.last = sys_ms ();
	wt.until = wt.last + secs * 1000;
	status ("");
	/* (les boutons du dialogue : Jet et Copier, par la fonction du dialogue) */
	static long s_jet, s_copy;
	s_jet = b_jet;
	s_copy = b_copy;
	ui_dialog_timer (d, (long) wt_tick, 250);
	r = ui_dialog_run (d, (long) (long (*) (long, long, long)) [] (long, long w, long code) -> long {
		if (code != UN_ACTIVATE)
			return 0;
		if (w == s_jet)
			open_jet (full);
		else if (w == s_copy)
			ui_clip_set ((long) ucode, (long) strlen (ucode));
		return 0;
	}, 0);
	ui_dialog_end (d);
	wt.wait = 0;
	ui_w_draw (0);
	if (r == 2)
		fail ("Connexion refusée", wt.err);
	return r == 1;
}

/* le navigateur -> 1 connecté */
static int connect_browser (const char *acc)
{
	static char url[4096];
	status (TR ("Préparation de la connexion…"));
	long r = oauth_browser_begin ((long) acc, 0, (long) url, sizeof url - 1);
	status ("");
	if (r < 0) {
		fail ("Connexion impossible", r);
		return 0;
	}
	url[r < (long) sizeof url ? r : (long) sizeof url - 1] = 0;
	url[strcspn (url, "\r\n")] = 0;
	if (jet_here ())
		open_jet (url);

	enum { DW = 560, DH = 300, M = 16 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("Se connecter"));
	if (!d)
		return 0;
	long y = 16;
	ui_label (M, y, DW - 2 * M, 40, (long) TR (jet_here ()
		? "Connectez-vous dans la fenêtre de Jet, puis autorisez Korin. Cette fenêtre se fermera toute seule."
		: "Ouvrez cette adresse dans un navigateur, connectez-vous, puis autorisez Korin."), ULB_WRAP);
	y += 46;
	long t_url = ui_textbox (M, y, DW - 2 * M - 120, AC_BTN, UTF_READONLY);
	ui_w_set_text (t_url, (long) url);
	long b_copy = ui_pushbutton (DW - M - 110, y, 110, AC_BTN, (long) TR ("Copier"), 0);
	y += 44;
	ui_label (M, y, DW - 2 * M, 40, (long) TR ("Sur un autre appareil : collez ici l'adresse où il arrive ensuite "
		"(même en erreur), puis Valider l'adresse."), ULB_WRAP | ULB_DIM);
	y += 44;
	long t_back = ui_textbox (M, y, DW - 2 * M - 150, AC_BTN, 0);
	ui_textbox_hint (t_back, (long) "http://127.0.0.1:…/?code=…&state=…");
	long b_fin = ui_pushbutton (DW - M - 140, y, 140, AC_BTN, (long) TR ("Valider l'adresse"), 0);
	ui_dialog_button (d, 0, (long) TR ("Annuler"), 0, 0, DW - 12);
	ui_dialog_cancel (d, 0);
	wt.acc = acc;
	wt.browser = 1;
	wt.err = 0;
	wt.last = sys_ms ();
	wt.until = 0;
	wt.wait = 0;
	static long s_copy, s_fin, s_back, s_d;
	s_copy = b_copy;
	s_fin = b_fin;
	s_back = t_back;
	s_d = d;
	ui_dialog_timer (d, (long) wt_tick, 250);
	r = ui_dialog_run (d, (long) (long (*) (long, long, long)) [] (long, long w, long code) -> long {
		if (code != UN_ACTIVATE)
			return 0;
		if (w == s_copy)
			ui_clip_set ((long) url, (long) strlen (url));
		else if (w == s_fin || w == s_back) {
			const char *back = (const char *) ui_w_get_text (s_back);
			if (!back || !*back)
				return 0;
			long rr = oauth_browser_finish ((long) wt.acc, (long) back);
			if (rr == 0)
				ui_dialog_close (s_d, 1);
			else {
				wt.err = rr;
				ui_dialog_close (s_d, 2);
			}
		}
		return 0;
	}, 0);
	ui_dialog_end (d);
	ui_w_draw (0);
	if (r == 2)
		fail ("Connexion refusée", wt.err);
	return r == 1;
}

static void connect_acc (const char *acc, const char *prov)
{
	const Prov *p = prov_of (prov);
	int ok = p && p->browser ? connect_browser (acc) : connect_device (acc);
	load (acc);
	if (ok) {
		char m[200];
		snprintf (m, sizeof m, "%s : %s", acc, TR ("connecté."));
		status (m);
	}
}

/* ---- ajouter un compte ---- */

static struct { long prov, name, cid, scope, note, newapp; int last; } dl;

static void dl_prov (int fill)
{
	int k = (int) ui_w_get_value (dl.prov);
	if (k < 0 || k >= NPROV)
		k = 0;
	const Prov *p = &provs[k];
	int korin = korin_client (p->id);
	if (fill || k != dl.last) {
		ui_w_set_text (dl.name, (long) p->account);
		ui_w_set_text (dl.scope, (long) p->scope);
		ui_w_set_text (dl.cid, (long) "");
	}
	dl.last = k;
	ui_textbox_hint (dl.cid, (long) TR (korin ? "celui de Korin" : "à donner"));
	static char note[700];
	if (!korin && !strcmp (p->id, "github"))
		snprintf (note, sizeof note, "%s", TR ("Il faut une « OAuth App » à votre nom, une seule fois : sur GitHub, nom "
			"« Korin », page d'accueil et adresse de retour http://127.0.0.1, « Enable Device Flow » coché ; puis "
			"copiez son Client ID ici."));
	else
		snprintf (note, sizeof note, "%s", TR (p->note));
	ui_w_set_text (dl.note, (long) note);
	ui_w_enable (dl.newapp, !strcmp (p->id, "github"));
	ui_w_draw (0);
}

static long dl_fn (long d, long w, long code)
{
	if (w == dl.prov && code == UN_CHANGE)
		dl_prov (0);
	else if (w == dl.newapp && code == UN_ACTIVATE)
		open_jet (GH_NEW_APP);
	else if ((w == dl.name || w == dl.cid) && code == UN_ACTIVATE)
		ui_dialog_close (d, 1);
	return 0;
}

static int name_ok (const char *s)
{
	size_t n = strlen (s);
	if (!n || n > 63)
		return 0;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || strchr ("._-/@", *s)))
			return 0;
	return 1;
}

static void on_add (long, long)
{
	enum { DW = 560, DH = 330, M = 16, LW = 120, CH = 30 };
	static char name[64], cid[200], scope[300];
	int k = 0;
	for (;;) {
		long d = ui_dialog_begin (DW, DH, (long) TR ("Ajouter un compte"));
		if (!d)
			return;
		long y = 20, fw = DW - 2 * M - LW;
		static char items[300];
		items[0] = 0;
		for (int i = 0; i < NPROV; i++)
			snprintf (items + strlen (items), sizeof items - strlen (items), "%s%s", i ? "|" : "", provs[i].label);
		ui_label (M, y + 6, LW, 20, (long) TR ("Fournisseur"), 0);
		dl.prov = ui_dropdown (M + LW, y, fw, CH, (long) items);
		ui_w_set_value (dl.prov, k);
		y += 42;
		ui_label (M, y + 6, LW, 20, (long) TR ("Nom du compte"), 0);
		dl.name = ui_textbox (M + LW, y, fw, CH, 0);
		y += 42;
		ui_label (M, y + 6, LW, 20, (long) TR ("Client ID"), 0);
		dl.cid = ui_textbox (M + LW, y, fw - 150, CH, 0);
		dl.newapp = ui_pushbutton (DW - M - 140, y, 140, CH, (long) TR ("Créer sur GitHub…"), 0);
		ui_w_enable (dl.newapp, 0);
		y += 42;
		ui_label (M, y + 6, LW, 20, (long) TR ("Permissions"), 0);
		dl.scope = ui_textbox (M + LW, y, fw, CH, 0);
		y += 42;
		dl.note = ui_label (M, y, DW - 2 * M, 60, (long) "", ULB_DIM | ULB_WRAP);
		long b = ui_dialog_button (d, 0, (long) TR ("Ajouter et se connecter"), 1, UB_DEFAULT, DW - 12);
		ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, b);
		ui_dialog_cancel (d, 0);
		dl.last = -1;
		dl_prov (1);
		if (name[0]) {			/* (un nouvel essai : les valeurs d'avant) */
			ui_w_set_text (dl.name, (long) name);
			ui_w_set_text (dl.cid, (long) cid);
			ui_w_set_text (dl.scope, (long) scope);
		}
		ui_w_focus (dl.name);
		long r = ui_dialog_run (d, (long) dl_fn, 0);
		k = (int) ui_w_get_value (dl.prov);
		if (k < 0 || k >= NPROV)
			k = 0;
		snprintf (name, sizeof name, "%s", (const char *) ui_w_get_text (dl.name));
		snprintf (cid, sizeof cid, "%s", (const char *) ui_w_get_text (dl.cid));
		snprintf (scope, sizeof scope, "%s", (const char *) ui_w_get_text (dl.scope));
		ui_dialog_end (d);
		ui_w_draw (0);
		if (r != 1) {
			name[0] = 0;
			return;
		}
		for (char *c = cid; *c; c++)	/* (un Client ID collé avec des blancs) */
			if (*c == ' ' || *c == '\t')
				memmove (c, c + 1, strlen (c)), c--;
		if (!name_ok (name)) {
			ui_message ((long) TR ("Comptes en ligne"), (long) TR ("Le nom : 63 lettres, chiffres ou « . _ - / @ » au "
				"plus, par exemple github ou gdrive/perso."), UM_OK);
			continue;
		}
		if (oauth_status ((long) name) != E_NOENT_ &&
		    !ui_message ((long) TR ("Comptes en ligne"), (long) TR ("Ce compte existe déjà : le remplacer ?"), UM_YESNO))
			continue;
		char conf[700];
		snprintf (conf, sizeof conf, "provider = %s\n%s%s%s%s%s%s", provs[k].id, cid[0] ? "client_id = " : "", cid,
			  cid[0] ? "\n" : "", scope[0] ? "scope = " : "", scope, scope[0] ? "\n" : "");
		long sr = oauth_setup ((long) name, (long) conf);
		if (sr < 0) {
			fail (cid[0] || korin_client (provs[k].id) ? "Ce compte ne peut pas être ajouté"
								 : "Il faut un Client ID pour ce fournisseur", sr);
			continue;
		}
		break;
	}
	char acc[64];
	snprintf (acc, sizeof acc, "%s", name);
	name[0] = 0;
	load (acc);
	connect_acc (acc, provs[k].id);
}

static void on_connect (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	char acc[64], prov[32];
	snprintf (acc, sizeof acc, "%s", accs[i].name);
	snprintf (prov, sizeof prov, "%s", accs[i].prov);
	connect_acc (acc, prov);
}

static void on_list (long, long code)
{
	if (code == UN_ACTIVATE)
		on_connect (0, 0);
	else if (code == UN_CHANGE)
		show_info ();
}

static void on_forget (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	char acc[64];
	snprintf (acc, sizeof acc, "%s", accs[i].name);
	long r = oauth_forget ((long) acc);
	if (r < 0) {
		fail ("Déconnexion impossible", r);
		return;
	}
	load (acc);
	status (TR ("Déconnecté : le jeton est oublié ici (révoquez-le aussi chez le fournisseur si besoin)."));
}

static void on_remove (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	char acc[64], q[300];
	snprintf (acc, sizeof acc, "%s", accs[i].name);
	snprintf (q, sizeof q, "%s « %s » ? %s", TR ("Retirer le compte"), acc,
		  TR ("Les programmes qui s'en servent devront le reconnecter."));
	if (!ui_message ((long) TR ("Comptes en ligne"), (long) q, UM_YESNO))
		return;
	long r = oauth_remove ((long) acc);
	if (r < 0) {
		fail ("Le compte ne peut pas être retiré", r);
		return;
	}
	load (0);
	status (TR ("Compte retiré."));
}

static void on_refresh (long, long)
{
	int i = sel ();
	char acc[64] = "";
	if (i >= 0)
		snprintf (acc, sizeof acc, "%s", accs[i].name);
	load (acc[0] ? acc : 0);
}

/* ---- le volet ---- */

static void ac_open (void)
{
	ap.draw = ac_draw;
	ap.close = ac_close;
	ap.widgets = 1;
	ntop = 0;
	long x = SIDE_W + 10, y = AP_HEAD + 4;
	long pw = ui.w - SIDE_W - 20, ph = ui.h - AP_HEAD - 10;

	top (ui_label (x, y, pw, 40, (long) TR ("Les comptes en ligne de Korin (OAuth 2.0) : les programmes y prennent leur "
		"jeton d'accès, sans redemander le mot de passe."), ULB_DIM | ULB_WRAP));
	y += 44;
	long gh = ph - 44 - 100 - 30, bw = 150;
	long g = top (ui_groupbox (x, y, pw, gh, (long) TR ("Comptes")));
	long lw = pw - bw - 30;
	w_list = in (g, ui_list (x + 10, y + AC_CT, lw, gh - AC_CT - 10, UL_HEADER | UL_NOSORT));
	ui_list_column (w_list, (long) TR ("Compte"), lw - 200 - 130, 0);
	ui_list_column (w_list, (long) TR ("Fournisseur"), 200, 0);
	ui_list_column (w_list, (long) TR ("État"), 130, 0);
	ui_w_on (w_list, (long) on_list, 0);
	long bx = x + pw - bw - 10, by = y + AC_CT;
	button (g, bx, by, bw, "Ajouter…", on_add);
	button (g, bx, by + 34, bw, "Se connecter", on_connect);
	button (g, bx, by + 68, bw, "Se déconnecter", on_forget);
	button (g, bx, by + 102, bw, "Retirer", on_remove);
	button (g, bx, by + 150, bw, "Actualiser", on_refresh);
	y += gh + 6;
	w_info = top (ui_label (x, y, pw, 96, (long) "", ULB_DIM | ULB_WRAP));
	y += 100;
	w_status = top (ui_label (x, y + 2, pw, 20, (long) "", ULB_DIM));
	load (0);
}

static void ac_apply (void) {}

extern "C" {
extern const int korin_applet_abi;
extern const Applet korin_applet;
const int korin_applet_abi = APPLET_ABI;
const Applet korin_applet = { "accounts", "Comptes en ligne", "Comptes en ligne", "jet", ac_open, ac_apply, OAUTHD };
}
