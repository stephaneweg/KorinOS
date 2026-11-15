/*
 * applet.cpp -- le volet Serveur web de Réglages, installé avec le serveur (paquet webd) : /sys/applets/webd.so,
 * chargé par Réglages (src/apps/control/control.h, « les applets installées ») ; sans le paquet, pas de volet.
 *
 *   * en tête : le service (en marche ou arrêté, Démarrer, Arrêter) et « Démarrer avec Korin » (la ligne
 *     /sys/webd de /etc/services.txt, lancée par init2) ;
 *   * Sites : nom, dossier, liaisons, état ; Ajouter (nom, dossier, première liaison), Activer ou désactiver,
 *     Retirer, Ouvrir (dans Jet) ;
 *   * Liaisons du site choisi (schéma, hôte, port) ; Ajouter, Retirer ; le certificat de la liaison https choisie :
 *     auto-signé, demande (CSR), certificat reçu du fournisseur ;
 *   * la ligne d'état.
 * Les sites sont lus et écrits par conf.c (le même que webd) ; un service en marche relit tout de suite (SIGHUP). Les
 * certificats passent par « webd cert » (OpenSSL reste dans webd).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "control.h"
extern "C" {
#include "../webd.h"
}

enum { WB_BTN = 28, WB_CT = 24, WB_TOPS = 16, WB_OUT = 4096 };

#define WEBD		"/sys/webd"		/* le service (lancé par init2 : sa sortie va au journal) */
#define WEBD_CMD	"/bin/webd"		/* le même, pour les commandes dont on lit la sortie */
#define SERVICES	"/etc/services.txt"
#define JET		"/apps/jet.app/main"

static long tops[WB_TOPS];
static int ntop;
static long w_state, w_start, w_stop, w_boot, w_sites, w_binds, w_cert, w_status, w_toggle;
static struct conf *cf;
static char out[WB_OUT];
static int last_pid = -1;
static time_t seen_mtime;		/* sites.ini tel que lu */

static time_t conf_mtime (void)
{
	struct stat st;
	return stat (CONF_FILE, &st) == 0 ? st.st_mtime : 0;
}

/* ---- les widgets ---- */

static long top (long w)
{
	if (ntop < WB_TOPS)
		tops[ntop++] = w;
	return w;
}

static long in (long g, long w)
{
	ui_w_set_parent (w, g);
	return w;
}

typedef void (*wb_fn) (long w, long code);

static long button (long g, long x, long y, long w, const char *text, wb_fn fn)
{
	long b = in (g, ui_pushbutton (x, y, w, WB_BTN, (long) TR (text), 0));
	ui_w_on (b, (long) fn, 0);
	return b;
}

static void wb_close (void)
{
	for (int i = 0; i < ntop; i++)
		ui_w_destroy (tops[i]);
	ntop = 0;
}

static void wb_draw (void) { ui_w_draw (0); }

static void status (const char *text)
{
	ui_w_set_text (w_status, (long) text);
	ui_w_draw (w_status);
}

/* ---- le service ---- */

static int service_pid (void)
{
	FILE *f = fopen (PID_FILE, "r");
	int pid = 0;
	if (f) {
		if (fscanf (f, "%d", &pid) != 1)
			pid = 0;
		fclose (f);
	}
	return pid > 0 && kill (pid, 0) == 0 ? pid : 0;
}

/* argv (argv[0] = WEBD_CMD) -> son code de sortie ; sa sortie (et ses erreurs) dans out */
static int run (const char *const *argv)
{
	out[0] = 0;
	int p[2];
	if (pipe (p) != 0)
		return -1;
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_adddup2 (&fa, p[1], 1);
	posix_spawn_file_actions_adddup2 (&fa, p[1], 2);
	posix_spawn_file_actions_addclose (&fa, p[0]);
	pid_t pid;
	int r = posix_spawn (&pid, argv[0], &fa, NULL, (char *const *) argv, NULL);
	posix_spawn_file_actions_destroy (&fa);
	close (p[1]);
	size_t n = 0;
	if (r == 0)
		for (ssize_t k; (k = read (p[0], out + n, sizeof out - 1 - n)) > 0;)
			n += (size_t) k;
	close (p[0]);
	out[n] = 0;
	while (n && (out[n - 1] == '\n' || out[n - 1] == '\r'))
		out[--n] = 0;
	if (r != 0) {
		snprintf (out, sizeof out, "%s : %s", argv[0], strerror (r));
		return -1;
	}
	int st = 0;
	waitpid (pid, &st, 0);
	return WIFEXITED (st) ? WEXITSTATUS (st) : -1;
}

/* « Démarrer avec Korin » : la ligne /sys/webd de services.txt (sans « # ») */
static int boot_get (void)
{
	FILE *f = fopen (SERVICES, "r");
	if (!f)
		return 0;
	char line[256];
	int on = 0;
	while (fgets (line, sizeof line, f))
		if (!strncmp (line, WEBD, strlen (WEBD)) && (line[strlen (WEBD)] == '\n' || line[strlen (WEBD)] == '\r' ||
				line[strlen (WEBD)] == 0 || line[strlen (WEBD)] == ' '))
			on = 1;
	fclose (f);
	return on;
}

static int boot_set (int on)
{
	static char buf[8192];
	size_t n = 0;
	FILE *f = fopen (SERVICES, "r");
	char line[256];
	while (f && fgets (line, sizeof line, f)) {
		const char *l = line[0] == '#' ? line + 1 : line;
		if (!strncmp (l, WEBD, strlen (WEBD)) || strstr (line, "le serveur web (docs/WEB.md)"))
			continue;		/* (la ligne, et son commentaire : réécrits plus bas) */
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
		n += (size_t) snprintf (buf + n, sizeof buf - n, "# le serveur web (docs/WEB.md) : Réglages › Serveur web\n%s\n", WEBD);
	f = fopen (SERVICES, "w");
	if (!f)
		return -1;
	size_t w = fwrite (buf, 1, n, f);
	return fclose (f) == 0 && w == n ? 0 : -1;
}

/* ---- les sites ---- */

static struct site *site_sel (void)
{
	unsigned long v = (unsigned long) ui_w_get_value (w_sites);
	return v < (unsigned long) cf->nsites ? &cf->sites[v] : 0;
}

static struct bind *bind_sel (struct site *s)
{
	unsigned long v = (unsigned long) ui_w_get_value (w_binds);
	return s && v < (unsigned long) s->nbinds ? &s->binds[v] : 0;
}

static void show_cert (void)
{
	struct site *s = site_sel ();
	struct bind *b = bind_sel (s);
	if (!b || b->scheme != SCHEME_HTTPS) {
		/* (la première liaison https du site, s'il en a une) */
		b = 0;
		for (int j = 0; s && j < s->nbinds && !b; j++)
			if (s->binds[j].scheme == SCHEME_HTTPS)
				b = &s->binds[j];
	}
	char t[600];
	if (!b)
		snprintf (t, sizeof t, "%s", TR ("Pas de liaison https : pas de certificat."));
	else {
		const char *argv[] = { WEBD_CMD, "cert", "list", 0 };
		const char *fh = strcmp (b->host, "*") ? b->host : "default";
		const char *line = 0;
		if (run (argv) == 0)
			for (char *l = out; l && *l; l = strchr (l, '\n') ? strchr (l, '\n') + 1 : 0)
				if (!strncmp (l, fh, strlen (fh)) && l[strlen (fh)] == ' ') {
					line = l + strlen (fh);
					break;
				}
		if (line) {
			while (*line == ' ')
				line++;
			const char *e = strchr (line, '\n');
			snprintf (t, sizeof t, "%s %s : %.*s", TR ("Certificat de"), b->host, (int) (e ? e - line : (long) strlen (line)),
				line);
		} else
			snprintf (t, sizeof t, "%s %s : %s", TR ("Certificat de"), b->host,
				TR ("aucun ; un auto-signé sera créé au démarrage du serveur."));
	}
	ui_w_set_text (w_cert, (long) t);
}

static void fill_binds (void)
{
	struct site *s = site_sel ();
	long v = ui_w_get_value (w_binds);
	ui_list_clear (w_binds);
	for (int j = 0; s && j < s->nbinds; j++) {
		char b[200];
		bind_format (&s->binds[j], b, sizeof b);
		ui_list_add (w_binds, (long) b, j);
	}
	if (!s || v < 0 || v >= s->nbinds)
		v = 0;
	ui_w_set_value (w_binds, v);
	show_cert ();
	ui_w_set_text (w_toggle, (long) TR (s && !s->enabled ? "Activer" : "Désactiver"));
}

static void load (const char *keep)
{
	long v = ui_w_get_value (w_sites);
	conf_load (CONF_FILE, cf, 1);
	ui_list_clear (w_sites);
	for (int i = 0; i < cf->nsites; i++) {
		struct site *s = &cf->sites[i];
		char binds[300] = "", row[1200];
		for (int j = 0; j < s->nbinds; j++) {
			char b[200];
			bind_format (&s->binds[j], b, sizeof b);
			snprintf (binds + strlen (binds), sizeof binds - strlen (binds), "%s%s", j ? ", " : "", b);
		}
		snprintf (row, sizeof row, "%s\t%s\t%s\t%s", s->name, s->root, binds, TR (s->enabled ? "actif" : "désactivé"));
		ui_list_add (w_sites, (long) row, i);
		if (keep && !strcmp (keep, s->name))
			v = i;
	}
	if (v < 0 || v >= cf->nsites)
		v = 0;
	ui_w_set_value (w_sites, v);
	fill_binds ();
}

static void state (void)
{
	int pid = service_pid ();
	if (pid == last_pid)
		return;
	last_pid = pid;
	char t[120];
	snprintf (t, sizeof t, "%s", TR (pid ? "Le serveur est en marche." : "Le serveur est arrêté."));
	ui_w_set_text (w_state, (long) t);
	ui_w_enable (w_start, !pid);
	ui_w_enable (w_stop, pid != 0);
	ui_w_draw (0);
}

/* la configuration écrite, relue par le service en marche -> 0 */
static int save (const char *name, const char *done)
{
	if (conf_save (CONF_FILE, cf) < 0) {
		status (TR ("La configuration ne peut pas être écrite (/etc/web/sites.ini)."));
		return -1;
	}
	int pid = service_pid ();
	if (pid)
		kill (pid, SIGHUP);
	seen_mtime = conf_mtime ();
	load (name);
	status (TR (done));
	ui_w_draw (0);
	return 0;
}

/* ---- les actions ---- */

static void on_start (long, long)
{
	spawn (WEBD, "");
	for (int i = 0; i < 30 && !service_pid (); i++)
		sys_sleep (100);
	state ();
	status (TR (service_pid () ? "Serveur démarré." : "Le serveur n'a pas démarré (journal : /var/log/webd.log)."));
}

static void on_stop (long, long)
{
	const char *argv[] = { WEBD_CMD, "stop", 0 };
	run (argv);
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

static void on_site (long, long)
{
	fill_binds ();
	ui_w_draw (0);
}

static void on_bind (long, long)
{
	show_cert ();
	ui_w_draw (0);
}

static int name_ok (const char *n)
{
	if (!*n)
		return 0;
	for (; *n; n++)
		if (!((*n >= 'a' && *n <= 'z') || (*n >= 'A' && *n <= 'Z') || (*n >= '0' && *n <= '9') || *n == '-' ||
				*n == '_' || *n == '.'))
			return 0;
	return 1;
}

static void on_add (long, long)
{
	char name[64] = "", dir[512] = "HOME:/sites", url[200] = "http://*:80";
	if (!ui_ask ((long) TR ("Nouveau site"), (long) TR ("Nom du site (lettres, chiffres, - _ .) :"), (long) name,
			sizeof name))
		return;
	if (!name_ok (name) || conf_find (cf, name) || cf->nsites >= MAX_SITES) {
		status (TR (conf_find (cf, name) ? "Ce nom est déjà pris." : "Ce nom ne convient pas."));
		return;
	}
	mkdirs ("HOME:/sites");
	if (!ui_folder_choose ((long) dir, (long) dir, sizeof dir))
		return;
	if (!ui_ask ((long) TR ("Nouveau site"), (long) TR ("Première liaison (http://*:80, https://nom.exemple) :"), (long) url,
			sizeof url))
		return;
	struct site *s = &cf->sites[cf->nsites];
	memset (s, 0, sizeof *s);
	if (bind_parse (url, &s->binds[0]) < 0) {
		status (TR ("Liaison refusée : http:// ou https://, un nom (ou *), un port."));
		return;
	}
	snprintf (s->name, sizeof s->name, "%s", name);
	snprintf (s->root, sizeof s->root, "%s", dir);
	strcpy (s->index[0], "index.html");
	strcpy (s->index[1], "index.htm");
	s->nindex = 2;
	s->nbinds = 1;
	s->enabled = 1;
	cf->nsites++;
	save (name, "Site ajouté.");
}

static void on_toggle (long, long)
{
	struct site *s = site_sel ();
	if (!s)
		return;
	s->enabled = !s->enabled;
	save (s->name, s->enabled ? "Site activé." : "Site désactivé.");
}

static void on_remove (long, long)
{
	struct site *s = site_sel ();
	if (!s)
		return;
	char q[200];
	snprintf (q, sizeof q, "%s « %s » ? %s", TR ("Retirer le site"), s->name, TR ("Ses fichiers ne sont pas touchés."));
	if (!ui_message ((long) TR ("Serveur web"), (long) q, UM_YESNO))
		return;
	int i = (int) (s - cf->sites);
	memmove (s, s + 1, (size_t) (cf->nsites - i - 1) * sizeof *s);
	cf->nsites--;
	save (0, "Site retiré.");
}

static void on_open (long, long)
{
	struct site *s = site_sel ();
	if (!s || !s->nbinds)
		return;
	struct bind *b = &s->binds[0];
	char url[300];
	snprintf (url, sizeof url, "%s://%s:%d/", b->scheme == SCHEME_HTTPS ? "https" : "http",
		strcmp (b->host, "*") ? b->host : "localhost", b->port);
	if (spawn (JET, url) < 0)
		status (TR ("Jet ne peut pas être lancé."));
}

static void on_bind_add (long, long)
{
	struct site *s = site_sel ();
	if (!s || s->nbinds >= MAX_BINDS)
		return;
	char url[200] = "https://";
	if (!ui_ask ((long) TR ("Nouvelle liaison"), (long) TR ("Liaison (http://*:80, https://nom.exemple:443) :"), (long) url,
			sizeof url))
		return;
	if (bind_parse (url, &s->binds[s->nbinds]) < 0) {
		status (TR ("Liaison refusée : http:// ou https://, un nom (ou *), un port."));
		return;
	}
	s->nbinds++;
	save (s->name, "Liaison ajoutée.");
}

static void on_bind_remove (long, long)
{
	struct site *s = site_sel ();
	struct bind *b = bind_sel (s);
	if (!b)
		return;
	int j = (int) (b - s->binds);
	memmove (b, b + 1, (size_t) (s->nbinds - j - 1) * sizeof *b);
	s->nbinds--;
	save (s->name, "Liaison retirée.");
}

/* l'hôte de la liaison https choisie (ou de la première) -> 0 : aucune */
static const char *cert_host (void)
{
	struct site *s = site_sel ();
	struct bind *b = bind_sel (s);
	if (b && b->scheme == SCHEME_HTTPS)
		return b->host;
	for (int j = 0; s && j < s->nbinds; j++)
		if (s->binds[j].scheme == SCHEME_HTTPS)
			return s->binds[j].host;
	status (TR ("Choisissez d'abord une liaison https."));
	return 0;
}

static void cert_done (int r)
{
	int pid = service_pid ();
	if (r == 0 && pid)
		kill (pid, SIGHUP);
	status (out);
	show_cert ();
	ui_w_draw (0);
}

static void on_self (long, long)
{
	const char *h = cert_host ();
	if (!h)
		return;
	const char *argv[] = { WEBD_CMD, "cert", "self", h, 0 };
	cert_done (run (argv));
}

static void on_csr (long, long)
{
	const char *h = cert_host ();
	if (!h)
		return;
	const char *argv[] = { WEBD_CMD, "cert", "csr", h, 0 };
	int r = run (argv);
	cert_done (r);
	if (r == 0) {
		char t[700];
		snprintf (t, sizeof t, "%s\n\n%s/%s.csr\n\n%s", TR ("La demande de certificat est prête :"), CERT_DIR,
			strcmp (h, "*") ? h : "default", TR ("Envoyez-la au fournisseur, puis « Importer » le certificat reçu."));
		ui_message ((long) TR ("Demande de certificat"), (long) t, UM_OK);
	}
}

static void on_import (long, long)
{
	const char *h = cert_host ();
	if (!h)
		return;
	char file[512] = "";
	if (!ui_file_open ((long) "HOME:/", (long) file, sizeof file))
		return;
	const char *argv[] = { WEBD_CMD, "cert", "import", h, file, 0 };
	cert_done (run (argv));
}

/* chaque seconde : l'état du service, et les sites si sites.ini a changé (« webd site » dans le Terminal) */
static int wb_tick (void)
{
	state ();
	time_t m = conf_mtime ();
	if (m != seen_mtime) {
		seen_mtime = m;
		struct site *s = site_sel ();
		char keep[64];
		snprintf (keep, sizeof keep, "%s", s ? s->name : "");
		load (keep);
		ui_w_draw (0);
	}
	return 0;
}

/* ---- le volet ---- */

static void wb_open (void)
{
	if (!cf)
		cf = (struct conf *) calloc (1, sizeof *cf);
	ap.draw = wb_draw;
	ap.close = wb_close;
	ap.widgets = 1;
	ap.tick = wb_tick;
	ntop = 0;
	last_pid = -1;
	long x = SIDE_W + 10, y = AP_HEAD + 4;
	long pw = ui.w - SIDE_W - 20, ph = ui.h - AP_HEAD - 10;

	/* le service */
	w_state = top (ui_label (x, y + 4, 230, 20, (long) "", ULB_BOLD));
	w_start = top (ui_pushbutton (x + 236, y, 96, WB_BTN, (long) TR ("Démarrer"), 0));
	ui_w_on (w_start, (long) on_start, 0);
	w_stop = top (ui_pushbutton (x + 338, y, 96, WB_BTN, (long) TR ("Arrêter"), 0));
	ui_w_on (w_stop, (long) on_stop, 0);
	w_boot = top (ui_checkbox (x + 450, y + 4, pw - 450, 20, (long) TR ("Démarrer avec Korin"), boot_get ()));
	ui_w_on (w_boot, (long) on_boot, 0);
	y += 38;

	/* les sites */
	long gh = 180, bw = 128;
	long g = top (ui_groupbox (x, y, pw, gh, (long) TR ("Sites")));
	long lw = pw - bw - 30;
	w_sites = in (g, ui_list (x + 10, y + WB_CT, lw, gh - WB_CT - 10, UL_HEADER | UL_NOSORT));
	ui_list_column (w_sites, (long) TR ("Site"), 110, 0);
	ui_list_column (w_sites, (long) TR ("Dossier"), 170, 0);
	ui_list_column (w_sites, (long) TR ("Liaisons"), lw - 110 - 170 - 80, 0);
	ui_list_column (w_sites, (long) TR ("État"), 80, 0);
	ui_w_on (w_sites, (long) on_site, 0);
	long bx = x + pw - bw - 10, by = y + WB_CT;
	button (g, bx, by, bw, "Ajouter…", on_add);
	w_toggle = button (g, bx, by + 34, bw, "Désactiver", on_toggle);
	button (g, bx, by + 68, bw, "Retirer", on_remove);
	button (g, bx, by + 112, bw, "Ouvrir dans Jet", on_open);
	y += gh + 6;

	/* les liaisons et le certificat */
	gh = ph - (y - AP_HEAD) - 28;
	g = top (ui_groupbox (x, y, pw, gh, (long) TR ("Liaisons du site et certificat")));
	long half = (pw - 30) / 2;
	w_binds = in (g, ui_list (x + 10, y + WB_CT, half, gh - WB_CT - 46, 0));
	ui_w_on (w_binds, (long) on_bind, 0);
	button (g, x + 10, y + gh - 38, 110, "Ajouter…", on_bind_add);
	button (g, x + 126, y + gh - 38, 110, "Retirer", on_bind_remove);
	long cx = x + 20 + half;
	w_cert = in (g, ui_label (cx, y + WB_CT, half, gh - WB_CT - 46, (long) "", ULB_WRAP));
	long cbw = (half - 12) / 3;
	ui_w_tip (button (g, cx, y + gh - 38, cbw, "Auto-signé", on_self),
		(long) TR ("Un certificat auto-signé pour cet hôte (les navigateurs avertissent)."));
	ui_w_tip (button (g, cx + cbw + 6, y + gh - 38, cbw, "Demande…", on_csr),
		(long) TR ("Une demande de certificat (CSR) à envoyer au fournisseur."));
	ui_w_tip (button (g, cx + 2 * (cbw + 6), y + gh - 38, cbw, "Importer…", on_import),
		(long) TR ("Le certificat reçu du fournisseur (et sa chaîne), pour la dernière demande."));
	y += gh + 4;

	/* la ligne d'état */
	w_status = top (ui_label (x, y + 2, pw, 20, (long) TR ("Gestionnaires : fichiers statiques (PHP et .NET viendront)."),
		ULB_DIM));

	seen_mtime = conf_mtime ();
	load (0);
	state ();
}

static void wb_apply (void) {}

extern "C" {
extern const int korin_applet_abi;
extern const Applet korin_applet;
const int korin_applet_abi = APPLET_ABI;
const Applet korin_applet = { "webd", "Serveur web", "Serveur web", "webd", wb_open, wb_apply, WEBD };
}
