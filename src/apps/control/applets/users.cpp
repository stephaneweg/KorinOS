/*
 * users.cpp -- Réglages › Utilisateurs (docs/etude/MULTI-UTILISATEUR.md, étape 6 ; docs/DESIGN-CONNEXION.md) :
 * les comptes de /etc/users.ini (nom affiché, identifiant, administrateur, mot de passe ou non), Ajouter,
 * Retirer, Mot de passe, Administrateur, et la connexion automatique (autologin de /etc/system.ini).
 *
 * Tout changement passe par la carte d'élévation d'authd (ap_elevate : le mot de passe de l'utilisateur), puis
 * par les commandes de /sbin (useradd, userdel, usermod) et par authd (AUTH_SETPW) ; Réglages reprend ensuite
 * son uid. Retirer un compte ne touche pas à son dossier.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <armpi/syscall.h>
#include "../control.h"

#define US_BTN 30
#define US_CT 28
#define ACC_MAX 32

static long us_tops[48], us_ntop;
static long w_list, w_status, w_auto;

struct acc {
	char name[32], full[64];
	long uid;
	int admin, haspw;
};
static acc accs[ACC_MAX];
static int nacc;
static char autologin[32];

static long top (long w)
{
	if (us_ntop < 48)
		us_tops[us_ntop++] = w;
	return w;
}

static void status (const char *t)
{
	ui_w_set_text (w_status, (long) t);
	ui_w_draw (w_status);
}

/* ---- lecture ---- */

static long auth_haspw (const char *name)
{
	long ch = svc_open ("auth", 1000);
	if (ch < 0)
		return 0;
	char m[512];
	memset (m, 0, sizeof m);
	*(long *) m = 10;	/* AUTH_HASPW */
	snprintf (m + 48, 400, "%s", name);
	*(unsigned *) (m + 40) = (unsigned) strlen (name) + 1;
	long r = svc_call (ch, m);
	return r < 0 ? -1 : *(long *) m;
}

static void load (void)
{
	nacc = 0;
	autologin[0] = 0;
	FILE *f = fopen ("/etc/users.ini", "r");
	char line[256];
	acc cur;
	memset (&cur, 0, sizeof cur);
	cur.uid = -1;
	int in = 0;
	for (;;) {
		char *l = f ? fgets (line, sizeof line, f) : 0;
		if (l)
			l = ini_trim (l);
		if (!l || *l == '[') {
			if (in && cur.uid >= 1000 && nacc < ACC_MAX)
				accs[nacc++] = cur;
			if (!l)
				break;
			memset (&cur, 0, sizeof cur);
			cur.uid = -1;
			char *e = strchr (l, ']');
			if (e)
				*e = 0;
			snprintf (cur.name, sizeof cur.name, "%s", l + 1);
			in = 1;
			continue;
		}
		char *eq = strchr (l, '=');
		if (!eq || !in)
			continue;
		*eq = 0;
		char *k = ini_trim (l), *v = ini_trim (eq + 1);
		if (!strcmp (k, "uid"))
			cur.uid = atol (v);
		else if (!strcmp (k, "name"))
			snprintf (cur.full, sizeof cur.full, "%s", v);
		else if (!strcmp (k, "groups"))
			cur.admin = strstr (v, "admin") != 0;
	}
	if (f)
		fclose (f);
	f = fopen (P_SYSTEM, "r");
	while (f && fgets (line, sizeof line, f)) {
		char *l = ini_trim (line), *eq = strchr (l, '=');
		if (!eq || *l == '#')
			continue;
		*eq = 0;
		if (!strcmp (ini_trim (l), "autologin"))
			snprintf (autologin, sizeof autologin, "%s", ini_trim (eq + 1));
	}
	if (f)
		fclose (f);
	for (int i = 0; i < nacc; i++)
		accs[i].haspw = (int) auth_haspw (accs[i].name);
}

static void fill (int keep)
{
	while (ui_list_count (w_list) > 0)
		ui_list_remove (w_list, 0);
	for (int i = 0; i < nacc; i++) {
		char row[200];
		const acc &a = accs[i];
		snprintf (row, sizeof row, "%s\t%s\t%s\t%s", a.full[0] ? a.full : a.name, a.name,
			  TR (a.admin ? "Administrateur" : "Standard"),
			  TR (a.haspw > 0 ? "oui" : a.haspw == 0 ? "non" : "?"));
		ui_list_add (w_list, (long) row, i);
	}
	ui_w_set_value (w_list, nacc ? (keep < nacc ? keep : nacc - 1) : -1);
	ui_w_set_value (w_auto, autologin[0] != 0);
	ui_w_draw (0);
}

static int sel (void)
{
	long v = ui_w_get_value (w_list);
	return v >= 0 && v < nacc ? (int) v : -1;
}

/* ---- changements (élevés) ---- */

/* une commande de /sbin, attendue -> son code de sortie */
static long run (const char *path, const char *args)
{
	long pid = spawn (path, args);
	if (pid < 0)
		return pid;
	return armpi_syscall (0x13, pid, 0, 0, 0, 0, 0);	/* SYS_WAIT */
}

static bool elevate (const char *why, unsigned *me)
{
	if (ap_elevate (TR (why), me)) {
		status (TR ("Autorisation refusée."));
		return false;
	}
	return true;
}

static long setpw (const char *name, const char *pw)
{
	long ch = svc_open ("auth", 1000);
	if (ch < 0)
		return ch;
	char m[512];
	memset (m, 0, sizeof m);
	*(long *) m = 8;	/* AUTH_SETPW */
	size_t n = strlen (name), p = strlen (pw);
	if (n + p + 2 > 400)
		return -22;
	memcpy (m + 48, name, n + 1);
	memcpy (m + 48 + n + 1, pw, p + 1);
	*(unsigned *) (m + 40) = (unsigned) (n + p + 2);
	long r = svc_call (ch, m);
	memset (m, 0, sizeof m);
	return r < 0 ? r : *(long *) m;
}

/* deux mots de passe -> 1 s'ils sont égaux (dans pw) */
/* son propre mot de passe (AUTH_PASSWD : l'ancien, le nouveau), sans élévation : un compte administrateur sans mot
 * de passe ne peut plus s'élever (décision du 25/11/2026), il s'en donne un ainsi -> 0 ou erreur */
static long ownpw (const char *old, const char *pw)
{
	long ch = svc_open ("auth", 1000);
	if (ch < 0)
		return ch;
	char m[512];
	memset (m, 0, sizeof m);
	*(long *) m = 4;	/* AUTH_PASSWD */
	size_t o = strlen (old), p = strlen (pw);
	if (o + p + 2 > 400)
		return -22;
	memcpy (m + 48, old, o + 1);
	memcpy (m + 48 + o + 1, pw, p + 1);
	*(unsigned *) (m + 40) = (unsigned) (o + p + 2);
	long r = svc_call (ch, m);
	memset (m, 0, sizeof m);
	return r < 0 ? r : *(long *) m;
}

static int ask_pw (const char *title, char *pw, size_t cap)
{
	char a[128] = "", b[128] = "";
	if (!ui_ask ((long) title, (long) TR ("Nouveau mot de passe (vide : aucun) :"), (long) a, sizeof a))
		return 0;
	if (!ui_ask ((long) title, (long) TR ("Encore une fois :"), (long) b, sizeof b))
		return 0;
	if (strcmp (a, b)) {
		status (TR ("Les deux mots de passe ne sont pas les mêmes."));
		return 0;
	}
	snprintf (pw, cap, "%s", a);
	memset (a, 0, sizeof a);
	memset (b, 0, sizeof b);
	return 1;
}

static void on_add (long, long)
{
	char name[40] = "", full[64] = "";
	if (!ui_ask ((long) TR ("Ajouter un compte"), (long) TR ("Identifiant (minuscules, chiffres, - et _) :"), (long) name, sizeof name) || !name[0])
		return;
	ui_ask ((long) TR ("Ajouter un compte"), (long) TR ("Nom affiché :"), (long) full, sizeof full);
	int admin = ui_message ((long) TR ("Ajouter un compte"), (long) TR ("Ce compte peut-il administrer la machine (installer, changer les réglages du système) ?"), UM_YESNO) == 1;
	char pw[128] = "";
	ask_pw (TR ("Ajouter un compte"), pw, sizeof pw);
	if (admin && !pw[0]) {	/* (décision du 25/11/2026) */
		status (TR ("Un compte administrateur a un mot de passe."));
		return;
	}
	unsigned me;
	if (!elevate ("Réglages veut ajouter un compte.", &me))
		return;
	for (char *c = full; *c; c++)
		if (*c == ' ')
			*c = '_';	/* (les arguments sont coupés aux espaces) */
	char args[200];
	snprintf (args, sizeof args, "%s%s%s%s", full[0] ? "-n " : "", full, full[0] ? " " : "", admin ? "-a " : "");
	strncat (args, name, sizeof args - strlen (args) - 1);
	long r = run ("/sbin/useradd", args);
	if (r == 0 && pw[0])
		setpw (name, pw);
	memset (pw, 0, sizeof pw);
	ap_unelevate (me);
	status (TR (r == 0 ? "Compte ajouté." : "Le compte n'a pas pu être ajouté (identifiant refusé ou déjà pris)."));
	load ();
	fill (nacc - 1);
}

static void on_remove (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	if ((unsigned) accs[i].uid == (unsigned) getuid ()) {
		status (TR ("On ne retire pas le compte de la session ouverte."));
		return;
	}
	char q[200];
	snprintf (q, sizeof q, "%s « %s » ? %s", TR ("Retirer le compte"), accs[i].name, TR ("Son dossier et ses fichiers ne sont pas touchés."));
	if (ui_message ((long) TR ("Utilisateurs"), (long) q, UM_YESNO) != 1)
		return;
	unsigned me;
	if (!elevate ("Réglages veut retirer un compte.", &me))
		return;
	long r = run ("/sbin/userdel", accs[i].name);
	if (r == 0 && !strcmp (autologin, accs[i].name))
		ini_set (P_SYSTEM, "autologin", 0);
	ap_unelevate (me);
	status (TR (r == 0 ? "Compte retiré." : "Le compte n'a pas pu être retiré."));
	load ();
	fill (0);
}

static void on_password (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	char pw[128];
	if (!ask_pw (TR ("Mot de passe"), pw, sizeof pw))
		return;
	if (accs[i].admin && !pw[0]) {
		status (TR ("Un compte administrateur a un mot de passe."));
		return;
	}
	if ((unsigned) accs[i].uid == (unsigned) getuid () && !accs[i].haspw) {	/* (le sien, sans mot de passe encore) */
		long r = ownpw ("", pw);
		memset (pw, 0, sizeof pw);
		status (TR (r == 0 ? "Mot de passe changé." : "Le mot de passe n'a pas pu être changé."));
		load ();
		fill (i);
		return;
	}
	unsigned me;
	if (!elevate ("Réglages veut changer un mot de passe.", &me))
		return;
	long r = setpw (accs[i].name, pw);
	memset (pw, 0, sizeof pw);
	ap_unelevate (me);
	status (TR (r == 0 ? "Mot de passe changé." : "Le mot de passe n'a pas pu être changé."));
	load ();
	fill (i);
}

static void on_admin (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	if (!accs[i].admin && !accs[i].haspw) {	/* (un administrateur a un mot de passe : décision du 25/11/2026) */
		status (TR ("Donnez d'abord un mot de passe à ce compte."));
		return;
	}
	unsigned me;
	if (!elevate ("Réglages veut changer les droits d'un compte.", &me))
		return;
	char args[64];
	snprintf (args, sizeof args, "%s %s", accs[i].admin ? "-A" : "-a", accs[i].name);
	long r = run ("/sbin/usermod", args);
	ap_unelevate (me);
	status (TR (r == 0 ? "Droits changés." : "Les droits n'ont pas pu être changés."));
	load ();
	fill (i);
}

static void on_auto (long w, long)
{
	int i = sel ();
	int on = (int) ui_w_get_value (w);
	if (on && i < 0) {
		ui_w_set_value (w, 0);
		return;
	}
	unsigned me;
	if (!elevate ("Réglages veut changer la connexion automatique.", &me)) {
		ui_w_set_value (w, !on);
		ui_w_draw (w);
		return;
	}
	ini_set (P_SYSTEM, "autologin", on ? accs[i].name : 0);
	ap_unelevate (me);
	status (on ? TR ("Ce compte s'ouvrira au démarrage, sans écran de connexion.") : TR ("L'écran de connexion sera montré au démarrage (sauf un seul compte sans mot de passe)."));
	load ();
	fill (i);
}

/* ---- le volet ---- */

static void us_close (void)
{
	for (long i = 0; i < us_ntop; i++)
		ui_w_destroy (us_tops[i]);
	us_ntop = 0;
}

static void us_draw (void) { ui_w_draw (0); }

static void us_open (void)
{
	ap.draw = us_draw;
	ap.close = us_close;
	ap.widgets = 1;
	us_ntop = 0;
	long x = SIDE_W + 10, y = AP_HEAD + 4;
	long pw = ui.w - SIDE_W - 20, ph = ui.h - AP_HEAD - 10;
	long gh = ph - 120, bw = 150, lw = pw - bw - 30;
	top (ui_groupbox (x, y, pw, gh, (long) TR ("Comptes")));
	w_list = top (ui_list (x + 10, y + US_CT, lw, gh - US_CT - 10, UL_HEADER | UL_NOSORT));
	ui_list_column (w_list, (long) TR ("Nom"), 140, 0);
	ui_list_column (w_list, (long) TR ("Identifiant"), 100, 0);
	ui_list_column (w_list, (long) TR ("Rôle"), 120, 0);
	ui_list_column (w_list, (long) TR ("Mot de passe"), lw - 140 - 100 - 120, 0);
	long bx = x + pw - bw - 10, by = y + US_CT;
	static const struct { const char *t; void (*fn) (long, long); } B[] = {
		{ "Ajouter…", on_add }, { "Retirer", on_remove }, { "Mot de passe…", on_password }, { "Administrateur", on_admin } };
	for (int i = 0; i < 4; i++) {
		long b = top (ui_pushbutton (bx, by + i * 36, bw, US_BTN, (long) TR (B[i].t), 0));
		ui_w_on (b, (long) B[i].fn, 0);
	}
	y += gh + 8;
	w_auto = top (ui_checkbox (x, y, pw, 22, (long) TR ("Ouvrir la session du compte choisi au démarrage (connexion automatique)"), 0));
	ui_w_on (w_auto, (long) on_auto, 0);
	y += 30;
	w_status = top (ui_label (x, y, pw, 40, (long) TR ("Les changements demandent votre mot de passe (comptes de /etc/elevate.ini)."), ULB_DIM | ULB_WRAP));
	load ();
	fill (0);
}

static void us_apply (void) {}

const Applet users_applet = { "users", "Utilisateurs", "Utilisateurs", "control", us_open, us_apply };
