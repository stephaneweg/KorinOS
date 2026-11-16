/*
 * applet.cpp -- le volet « Partage de fichiers » de Réglages, installé avec le serveur SMB (smbd) :
 * /sys/applets/shares.so, chargé par Réglages (src/apps/control/control.h, « les applets installées »).
 *
 *   * en tête : le serveur (en marche ou arrêté, Démarrer, Arrêter) et « Démarrer avec Korin » (la ligne
 *     /sys/smbd de /etc/services.txt, lancée par init2) ;
 *   * Dossiers partagés : nom, type, dossier, accès ; Ajouter, Modifier, Retirer. Un partage : le dossier, le
 *     type (SMB, le réseau Windows ; FTP plus tard) et, pour SMB, le nom sous lequel il se voit (\\korin\Nom) ;
 *     lecture seule ou non. Écrits dans /etc/shares.ini (src/sys/smbd/shares.h, commun aux types) ;
 *   * Compte d'accès : le nom et le mot de passe que Windows demande (/etc/smbd.ini, [users]) ;
 *   * la ligne d'état.
 * smbd relit shares.ini et smbd.ini quand ils changent : pas besoin de le relancer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <armpi/syscall.h>
#include "control.h"
#include "../shares.h"

enum { SH_BTN = 28, SH_CT = 24, SH_TOPS = 16, SYS_KILL_ = 0x14 };

#define SMBD		"/sys/smbd"		/* le service (lancé par init2 : sa sortie va au journal) */
#define SERVICES	"/etc/services.txt"
#define SHARES		"/etc/shares.ini"
#define SMBD_INI	"/etc/smbd.ini"

static long tops[SH_TOPS];
static int ntop;
static long w_state, w_start, w_stop, w_boot, w_list, w_user, w_pw, w_status;
static struct share_ent shares[SHARES_MAX];
static int nshares;
static long last_pid = -1;

/* ---- les widgets ---- */

static long top (long w)
{
	if (ntop < SH_TOPS)
		tops[ntop++] = w;
	return w;
}

static long in (long g, long w)
{
	ui_w_set_parent (w, g);
	return w;
}

typedef void (*sh_fn) (long w, long code);

static long button (long g, long x, long y, long w, const char *text, sh_fn fn)
{
	long b = in (g, ui_pushbutton (x, y, w, SH_BTN, (long) TR (text), 0));
	ui_w_on (b, (long) fn, 0);
	return b;
}

static void sh_close (void)
{
	for (int i = 0; i < ntop; i++)
		ui_w_destroy (tops[i]);
	ntop = 0;
}

static void sh_draw (void) { ui_w_draw (0); }

static void status (const char *text)
{
	ui_w_set_text (w_status, (long) text);
	ui_w_draw (w_status);
}

/* ---- le serveur ---- */

static long service_pid (void) { return proc_find ("smbd", 0); }

static int boot_get (void)
{
	FILE *f = fopen (SERVICES, "r");
	if (!f)
		return 0;
	char line[256];
	int on = 0;
	size_t n = strlen (SMBD);
	while (fgets (line, sizeof line, f))
		if (!strncmp (line, SMBD, n) && (line[n] == '\n' || line[n] == '\r' || line[n] == 0 || line[n] == ' '))
			on = 1;
	fclose (f);
	return on;
}

static int boot_set_raw (int on);

/* l'élévation le temps d'écrire dans /etc (docs/etude/MULTI-UTILISATEUR.md §2.6) */
static int elevated (int (*fn) (void *), void *arg)
{
	unsigned me;
	if (ap_elevate (TR ("Réglages veut modifier le partage de fichiers."), &me)) {
		status (TR ("Autorisation refusée."));
		return -2;
	}
	int r = fn (arg);
	ap_unelevate (me);
	return r;
}

static int boot_set_fn (void *a) { return boot_set_raw (*(int *) a); }
static int boot_set (int on) { return elevated (boot_set_fn, &on); }

static int boot_set_raw (int on)
{
	static char buf[8192];
	size_t n = 0;
	FILE *f = fopen (SERVICES, "r");
	char line[256];
	while (f && fgets (line, sizeof line, f)) {
		const char *l = line[0] == '#' ? line + 1 : line;
		if (!strncmp (l, SMBD, strlen (SMBD)) || strstr (line, "le partage de fichiers (docs/SMB.md)"))
			continue;			/* (la ligne et son commentaire : réécrits plus bas) */
		size_t k = strlen (line);
		if (n + k + 1 >= sizeof buf)
			break;
		memcpy (buf + n, line, k);
		n += k;
	}
	if (f)
		fclose (f);
	if (n && buf[n - 1] != '\n')
		buf[n++] = '\n';
	if (on)
		n += (size_t) snprintf (buf + n, sizeof buf - n,
			"# le partage de fichiers (docs/SMB.md) : Réglages › Partage de fichiers\n%s\n", SMBD);
	f = fopen (SERVICES, "w");
	if (!f)
		return -1;
	size_t w = fwrite (buf, 1, n, f);
	return fclose (f) == 0 && w == n ? 0 : -1;
}

static void state (void)
{
	long pid = service_pid ();
	if (pid == last_pid)
		return;
	last_pid = pid;
	ui_w_set_text (w_state, (long) TR (pid ? "Le serveur est en marche." : "Le serveur est arrêté."));
	ui_w_enable (w_start, !pid);
	ui_w_enable (w_stop, pid != 0);
	ui_w_draw (0);
}

static void on_start (long, long)
{
	spawn (SMBD, "");
	for (int i = 0; i < 30 && !service_pid (); i++)
		sys_sleep (100);
	state ();
	status (TR (service_pid () ? "Serveur démarré." : "Le serveur n'a pas démarré (voir le journal)."));
}

static void on_stop (long, long)
{
	long pid = 0;
	while ((pid = proc_find ("smbd", pid)) != 0)
		armpi_syscall (SYS_KILL_, pid, 0, 0, 0, 0, 0);
	for (int i = 0; i < 20 && service_pid (); i++)
		sys_sleep (100);
	state ();
	status (TR ("Serveur arrêté."));
}

static void on_boot (long w, long)
{
	int on = ui_w_get_value (w) != 0;
	if (boot_set (on) < 0)
		status (TR ("/etc/services.txt ne peut pas être écrit."));
	else
		status (TR (on ? "Le serveur démarrera avec Korin." : "Le serveur ne démarrera plus avec Korin."));
}

/* ---- les partages ---- */

static void load (int keep)
{
	nshares = shares_load (SHARES, shares, SHARES_MAX);
	ui_list_clear (w_list);
	for (int i = 0; i < nshares; i++) {
		char row[1300];
		snprintf (row, sizeof row, "%s\t%s\t%s\t%s", shares[i].name, shares[i].type == SHARE_FTP ? "FTP" : "SMB",
			shares[i].path, TR (shares[i].readonly ? "lecture seule" : "lecture et écriture"));
		ui_list_add (w_list, (long) row, i);
	}
	if (keep < 0 || keep >= nshares)
		keep = 0;
	ui_w_set_value (w_list, nshares ? keep : -1);
}

static int shares_save_fn (void *) { return shares_save (SHARES, shares, nshares); }

static int save (int keep, const char *done)
{
	int sr = elevated (shares_save_fn, 0);
	if (sr == -2) {
		load (keep);
		return -1;
	}
	if (sr < 0) {
		status (TR ("/etc/shares.ini ne peut pas être écrit."));
		load (keep);
		return -1;
	}
	load (keep);
	status (TR (done));
	ui_w_draw (0);
	return 0;
}

static int sel (void)
{
	long v = ui_w_get_value (w_list);
	return v >= 0 && v < nshares ? (int) v : -1;
}

/* le dialogue d'un partage */
static struct { long type, name, path, browse, ro, note; } dl;

static void dl_type (void)
{
	int smb = ui_w_get_value (dl.type) == SHARE_SMB;
	ui_w_enable (dl.name, smb);
	ui_w_set_text (dl.note, (long) TR (smb
		? "Vu depuis Windows : \\\\nom-du-PC\\Nom, avec le compte d'accès ci-dessous."
		: "FTP : ce dossier sera servi par le serveur FTP (à venir) ; pas de nom à donner."));
	ui_w_draw (0);
}

static long dl_fn (long d, long w, long code)
{
	if ((w == dl.path || w == dl.name) && code == UN_ACTIVATE)
		ui_dialog_close (d, 1);			/* (Entrée dans un champ : Enregistrer) */
	else if (w == dl.browse && code == UN_ACTIVATE) {
		static char p[SHARE_PATH];
		snprintf (p, sizeof p, "%s", (const char *) ui_w_get_text (dl.path));
		if (!p[0])
			strcpy (p, "HOME:/");
		if (ui_folder_choose ((long) p, (long) p, sizeof p) == 1) {
			ui_w_set_text (dl.path, (long) p);
			/* (un nom proposé d'après le dossier, s'il n'y en a pas) */
			const char *n = (const char *) ui_w_get_text (dl.name);
			if (!n || !*n) {
				const char *b = strrchr (p, '/');
				b = b && b[1] ? b + 1 : p;
				char nm[SHARE_NAME];
				snprintf (nm, sizeof nm, "%s", b);
				char *c = strchr (nm, ':');
				if (c)
					*c = 0;
				ui_w_set_text (dl.name, (long) nm);
			}
		}
	} else if (w == dl.type && code == UN_CHANGE)
		dl_type ();
	return 0;
}

/* e : le partage à remplir (ses valeurs de départ) -> 1 : OK */
static int share_dialog (const char *title, struct share_ent *e)
{
	enum { DW = 520, DH = 300, M = 16, LW = 110, CH = 30 };
	long d = ui_dialog_begin (DW, DH, (long) TR (title));
	if (!d)
		return 0;
	long y = 20, fw = DW - 2 * M - LW;
	ui_label (M, y + 6, LW, 20, (long) TR ("Type"), 0);
	dl.type = ui_dropdown (M + LW, y, fw, CH, (long) TR ("SMB (réseau Windows)|FTP (bientôt)"));
	ui_w_set_value (dl.type, e->type);
	y += 42;
	ui_label (M, y + 6, LW, 20, (long) TR ("Dossier"), 0);
	dl.path = ui_textbox (M + LW, y, fw - 104, CH, 0);
	ui_w_set_text (dl.path, (long) e->path);
	dl.browse = ui_pushbutton (DW - M - 96, y, 96, CH, (long) TR ("Choisir…"), 0);
	y += 42;
	ui_label (M, y + 6, LW, 20, (long) TR ("Nom du partage"), 0);
	dl.name = ui_textbox (M + LW, y, fw, CH, 0);
	ui_w_set_text (dl.name, (long) e->name);
	ui_textbox_hint (dl.name, (long) TR ("Korin, Musique, Documents…"));
	y += 42;
	dl.ro = ui_checkbox (M + LW, y, fw, 24, (long) TR ("Lecture seule"), e->readonly);
	y += 34;
	dl.note = ui_label (M, y, DW - 2 * M, 40, (long) "", ULB_DIM | ULB_WRAP);
	long b = ui_dialog_button (d, 0, (long) TR ("Enregistrer"), 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, b);
	ui_dialog_cancel (d, 0);
	dl_type ();
	ui_w_focus (dl.path);
	long r = ui_dialog_run (d, (long) dl_fn, 0);
	e->type = (int) ui_w_get_value (dl.type) == SHARE_FTP ? SHARE_FTP : SHARE_SMB;
	snprintf (e->path, sizeof e->path, "%s", (const char *) ui_w_get_text (dl.path));
	snprintf (e->name, sizeof e->name, "%s", (const char *) ui_w_get_text (dl.name));
	e->readonly = ui_w_get_value (dl.ro) != 0;
	ui_dialog_end (d);
	ui_w_draw (0);
	return r == 1;
}

/* -> 0 si le partage convient (except : son rang dans la liste, -1 : nouveau), sinon le message */
static const char *share_check (struct share_ent *e, int except)
{
	size_t n = strlen (e->path);
	while (n > 1 && e->path[n - 1] == '/' && e->path[n - 2] != ':')
		e->path[--n] = 0;
	if (!e->path[0])
		return "Choisissez le dossier à partager.";
	if (access (e->path, F_OK) != 0)
		return "Ce dossier n'existe pas.";
	if (e->type == SHARE_FTP && !e->name[0]) {	/* (FTP : un nom de section d'après le dossier) */
		const char *b = strrchr (e->path, '/');
		snprintf (e->name, sizeof e->name, "%s", b && b[1] ? b + 1 : "ftp");
	}
	if (!share_name_ok (e->name))
		return "Ce nom ne convient pas (pas de \\ / : * ? \" < > | [ ] =).";
	for (int i = 0; i < nshares; i++)
		if (i != except && !strcasecmp (shares[i].name, e->name))
			return "Un partage porte déjà ce nom.";
	return 0;
}

static void edit (int i)
{
	struct share_ent e;
	if (i >= 0)
		e = shares[i];
	else {
		memset (&e, 0, sizeof e);
		if (nshares >= SHARES_MAX) {
			status (TR ("Trop de partages."));
			return;
		}
	}
	for (;;) {
		if (!share_dialog (i < 0 ? "Nouveau partage" : "Modifier le partage", &e))
			return;
		const char *bad = share_check (&e, i);
		if (!bad)
			break;
		ui_message ((long) TR ("Partage de fichiers"), (long) TR (bad), UM_OK);
	}
	if (i < 0) {
		shares[nshares++] = e;
		save (nshares - 1, "Partage ajouté.");
	} else {
		shares[i] = e;
		save (i, "Partage modifié.");
	}
}

static void on_add (long, long) { edit (-1); }

static void on_edit (long, long)
{
	int i = sel ();
	if (i >= 0)
		edit (i);
}

static void on_list (long, long code)
{
	if (code == UN_ACTIVATE)
		on_edit (0, 0);
}

static void on_remove (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	char q[300];
	snprintf (q, sizeof q, "%s « %s » ? %s", TR ("Ne plus partager"), shares[i].name, TR ("Ses fichiers ne sont pas touchés."));
	if (!ui_message ((long) TR ("Partage de fichiers"), (long) q, UM_YESNO))
		return;
	memmove (&shares[i], &shares[i + 1], (size_t) (nshares - i - 1) * sizeof shares[0]);
	nshares--;
	save (i, "Partage retiré.");
}

/* ---- le compte ---- */

static void on_account (long, long)
{
	char user[64], pw[128], old_u[64], old_p[128];
	snprintf (user, sizeof user, "%s", (const char *) ui_w_get_text (w_user));
	snprintf (pw, sizeof pw, "%s", (const char *) ui_w_get_text (w_pw));
	int had = smbd_account_get (SMBD_INI, old_u, sizeof old_u, old_p, sizeof old_p);
	if (!user[0] || strpbrk (user, "=[]#;\\/ ")) {
		status (TR ("Donnez un nom de compte (sans espace ni = [ ] # ; \\ /)."));
		return;
	}
	if (!pw[0]) {
		if (!had) {
			status (TR ("Donnez un mot de passe."));
			return;
		}
		snprintf (pw, sizeof pw, "%s", old_p);	/* (vide : le mot de passe enregistré gardé) */
	}
	struct acct { const char *u, *p; } ac = { user, pw };
	int ar = elevated ([] (void *a) -> int { struct acct *c = (struct acct *) a; return smbd_account_set (SMBD_INI, c->u, c->p); }, &ac);
	if (ar == -2)
		return;
	if (ar < 0) {
		status (TR ("/etc/smbd.ini ne peut pas être écrit."));
		return;
	}
	ui_w_set_text (w_pw, (long) "");
	status (TR ("Compte enregistré : Windows le demandera à la connexion."));
	ui_w_draw (0);
}

/* chaque seconde : l'état du serveur */
static int sh_tick (void)
{
	state ();
	return 0;
}

/* ---- le volet ---- */

static void sh_open (void)
{
	ap.draw = sh_draw;
	ap.close = sh_close;
	ap.widgets = 1;
	ap.tick = sh_tick;
	ntop = 0;
	last_pid = -1;
	long x = SIDE_W + 10, y = AP_HEAD + 4;
	long pw = ui.w - SIDE_W - 20, ph = ui.h - AP_HEAD - 10;

	/* le serveur */
	w_state = top (ui_label (x, y + 4, 230, 20, (long) "", ULB_BOLD));
	w_start = top (ui_pushbutton (x + 236, y, 96, SH_BTN, (long) TR ("Démarrer"), 0));
	ui_w_on (w_start, (long) on_start, 0);
	w_stop = top (ui_pushbutton (x + 338, y, 96, SH_BTN, (long) TR ("Arrêter"), 0));
	ui_w_on (w_stop, (long) on_stop, 0);
	w_boot = top (ui_checkbox (x + 450, y + 4, pw - 450, 20, (long) TR ("Démarrer avec Korin"), boot_get ()));
	ui_w_on (w_boot, (long) on_boot, 0);
	y += 38;

	/* les dossiers partagés */
	long gh = ph - 38 - 150 - 30, bw = 128;
	long g = top (ui_groupbox (x, y, pw, gh, (long) TR ("Dossiers partagés")));
	long lw = pw - bw - 30;
	w_list = in (g, ui_list (x + 10, y + SH_CT, lw, gh - SH_CT - 10, UL_HEADER | UL_NOSORT));
	ui_list_column (w_list, (long) TR ("Nom"), 120, 0);
	ui_list_column (w_list, (long) TR ("Type"), 64, 0);
	ui_list_column (w_list, (long) TR ("Dossier"), lw - 120 - 64 - 130, 0);
	ui_list_column (w_list, (long) TR ("Accès"), 130, 0);
	ui_w_on (w_list, (long) on_list, 0);
	long bx = x + pw - bw - 10, by = y + SH_CT;
	button (g, bx, by, bw, "Ajouter…", on_add);
	button (g, bx, by + 34, bw, "Modifier…", on_edit);
	button (g, bx, by + 68, bw, "Retirer", on_remove);
	y += gh + 6;

	/* le compte d'accès */
	gh = 150;
	g = top (ui_groupbox (x, y, pw, gh, (long) TR ("Compte d'accès")));
	in (g, ui_label (x + 10, y + SH_CT, pw - 20, 36, (long) TR ("Le nom et le mot de passe que Windows demande pour "
		"ouvrir les partages SMB (pas un compte de Korin)."), ULB_DIM | ULB_WRAP));
	char u[64], p[128];
	smbd_account_get (SMBD_INI, u, sizeof u, p, sizeof p);
	in (g, ui_label (x + 10, y + SH_CT + 46, 110, 20, (long) TR ("Nom"), 0));
	w_user = in (g, ui_textbox (x + 120, y + SH_CT + 40, 200, SH_BTN, 0));
	ui_w_set_text (w_user, (long) u);
	in (g, ui_label (x + 10, y + SH_CT + 84, 110, 20, (long) TR ("Mot de passe"), 0));
	w_pw = in (g, ui_textbox (x + 120, y + SH_CT + 78, 200, SH_BTN, UTF_PASSWORD));
	ui_textbox_hint (w_pw, (long) TR (p[0] ? "inchangé" : "à choisir"));
	button (g, x + 340, y + SH_CT + 78, 160, "Enregistrer le compte", on_account);
	y += gh + 4;

	/* la ligne d'état */
	w_status = top (ui_label (x, y + 2, pw, 20, (long) TR ("Les changements valent tout de suite, sans relancer le serveur."),
		ULB_DIM));

	load (0);
	state ();
}

static void sh_apply (void) {}

extern "C" {
extern const int korin_applet_abi;
extern const Applet korin_applet;
const int korin_applet_abi = APPLET_ABI;
const Applet korin_applet = { "shares", "Partages", "Partage de fichiers", "files", sh_open, sh_apply, SMBD };
}
