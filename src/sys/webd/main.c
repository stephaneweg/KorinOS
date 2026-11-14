/*
 * main.c -- webd, le serveur web de Korin (docs/WEB.md).
 *
 *   webd                         le service : les sites de SD0:/etc/web/sites.ini (init2 : ligne SD0:/sys/webd de
 *                                SD0:/etc/services.txt) ; sans site, il attend un « webd reload »
 *   webd --dev DOSSIER [--port N] l'instance privée de WebStudio : un site, le premier port libre à partir de N
 *                                (8080), une ligne par requête sur la sortie, « reload » sur l'entrée recharge les
 *                                pages ouvertes ; l'entrée fermée (WebStudio parti) arrête l'instance
 *   webd stop | reload | status  pilote le service (son numéro est dans /tmp/webd.pid)
 *   webd check                   lit la configuration et dit ce qu'il servirait
 *   webd site list | add NOM DOSSIER [LIAISON...] | remove NOM | enable NOM | disable NOM
 *            | bind NOM add|remove LIAISON | listing NOM 0|1
 *   webd cert list | self HÔTE [JOURS] | csr HÔTE | import HÔTE FICHIER | remove HÔTE
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "webd.h"

static volatile int want_reload, want_pages;

static void on_signal (int sig)
{
	if (sig == SIGHUP)
		want_reload = 1;
	else if (sig == SIGUSR1)
		want_pages = 1;
	else
		g_quit = 1;
}

static void signals (void)
{
	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_handler = on_signal;
	sigaction (SIGTERM, &sa, NULL);
	sigaction (SIGINT, &sa, NULL);
	sigaction (SIGHUP, &sa, NULL);
	sigaction (SIGUSR1, &sa, NULL);
	signal (SIGPIPE, SIG_IGN);
}

static void nap (int ms)
{
	struct timespec ts = { ms / 1000, (long) (ms % 1000) * 1000000 };
	nanosleep (&ts, NULL);
}

/* -> le numéro du service en marche, 0 : aucun */
static int service_pid (void)
{
	FILE *f = fopen (PID_FILE, "r");
	if (!f)
		return 0;
	int pid = 0;
	if (fscanf (f, "%d", &pid) != 1)
		pid = 0;
	fclose (f);
	if (pid <= 0 || pid == getpid () || kill (pid, 0) != 0)
		return 0;
	return pid;
}

static int usage (void)
{
	fputs ("webd " WEBD_VERSION " -- le serveur web de Korin (docs/WEB.md)\n"
		"  webd                           le service (SD0:/etc/web/sites.ini)\n"
		"  webd --dev DOSSIER [--port N]  un dossier, pour WebStudio (port libre à partir de 8080)\n"
		"  webd stop | reload | status | check\n"
		"  webd site list | add NOM DOSSIER [LIAISON...] | remove NOM | enable NOM | disable NOM\n"
		"           | bind NOM add|remove LIAISON | listing NOM 0|1\n"
		"  webd cert list | self HÔTE [JOURS] | csr HÔTE | import HÔTE FICHIER | remove HÔTE\n"
		"  LIAISON : http://*:80, https://nom.exemple, http://localhost:8081\n", stdout);
	return 2;
}

static int count_served (const struct conf *c)
{
	int n = 0;
	for (int i = 0; i < c->nsites; i++)
		n += c->sites[i].enabled && c->sites[i].nbinds > 0;
	return n;
}

/* ---- le service ---- */

static int serve (void)
{
	int other = service_pid ();
	if (other) {
		printf ("webd: déjà en marche (%d)\n", other);
		return 0;
	}
	FILE *pf = fopen (PID_FILE, "w");
	if (pf) {
		fprintf (pf, "%d\n", getpid ());
		fclose (pf);
	}
	signals ();
	log_open (0);
	webtls_init ();
	struct listener *ls = calloc (MAX_LISTEN, sizeof *ls);
	int nls = 0;
	struct conf *c = NULL;
	while (!g_quit) {
		c = calloc (1, sizeof *c);
		if (!c || !ls)
			return 1;
		if (conf_load (CONF_FILE, c, 0) < 0)
			log_msg ("pas de configuration (%s) : « webd site add » ou Réglages › Serveur web", CONF_FILE);
		c->refs = 1;			/* (après conf_load, qui remet tout à zéro) : la part du service */
		listeners_start (c, ls, &nls, 0);
		char ports[256] = "";
		for (int k = 0; k < nls; k++)
			if (ls[k].fd >= 0)
				snprintf (ports + strlen (ports), sizeof ports - strlen (ports), "%s%s:%d", *ports ? ", " : "",
					ls[k].scheme == SCHEME_HTTPS ? "https" : "http", ls[k].port);
		log_msg ("en service : %d site(s), ports %s", count_served (c), *ports ? ports : "aucun");
		want_reload = 0;
		while (!g_quit && !want_reload)
			nap (200);
		listeners_stop (ls, nls);
		conf_unref (c);
		if (want_reload)
			log_msg ("configuration relue");
	}
	log_msg ("arrêté");
	free (ls);
	unlink (PID_FILE);
	return 0;
}

/* ---- l'instance de WebStudio ---- */

static void *stdin_thread (void *arg)
{
	(void) arg;
	char line[256];
	while (fgets (line, sizeof line, stdin)) {
		if (!strncmp (line, "reload", 6))
			reload_pages ();
		else if (!strncmp (line, "quit", 4))
			break;
	}
	g_quit = 1;
	return NULL;
}

static int dev (int argc, char **argv)
{
	const char *dir = NULL;
	int port = 8080;
	for (int i = 0; i < argc; i++) {
		if (!strcmp (argv[i], "--port") && i + 1 < argc)
			port = atoi (argv[++i]);
		else if (!dir)
			dir = argv[i];
	}
	struct stat st;
	if (!dir || stat (dir, &st) != 0 || !S_ISDIR (st.st_mode)) {
		fprintf (stderr, "webd --dev : %s n'est pas un dossier\n", dir ? dir : "(rien)");
		return 2;
	}
	g_dev = 1;
	signals ();
	log_open (1);
	struct conf *c = calloc (1, sizeof *c);
	struct listener *ls = calloc (MAX_LISTEN, sizeof *ls);
	c->refs = 1;
	c->nsites = 1;
	struct site *s = &c->sites[0];
	snprintf (s->name, sizeof s->name, "dev");
	snprintf (s->root, sizeof s->root, "%s", dir);
	size_t n = strlen (s->root);
	while (n > 1 && s->root[n - 1] == '/' && s->root[n - 2] != ':')
		s->root[--n] = 0;
	strcpy (s->index[0], "index.html");
	strcpy (s->index[1], "index.htm");
	s->nindex = 2;
	s->enabled = s->listing = s->dev = 1;
	s->nbinds = 1;
	s->binds[0].scheme = SCHEME_HTTP;
	s->binds[0].port = port > 0 ? port : 8080;
	strcpy (s->binds[0].host, "*");
	int nls = 0;
	if (listeners_start (c, ls, &nls, 1) < 1) {
		fprintf (stderr, "webd --dev : aucun port libre à partir de %d\n", port);
		return 1;
	}
	log_msg ("listen %d", s->binds[0].port);
	log_msg ("http://localhost:%d/ sert %s", s->binds[0].port, s->root);
	pthread_t th;
	pthread_create (&th, NULL, stdin_thread, NULL);
	while (!g_quit) {
		nap (100);
		if (want_pages) {
			want_pages = 0;
			reload_pages ();
		}
	}
	listeners_stop (ls, nls);
	return 0;
}

/* ---- les commandes ---- */

static int ctl (const char *op)
{
	int pid = service_pid ();
	if (!strcmp (op, "status")) {
		struct conf *c = calloc (1, sizeof *c);
		int have = conf_load (CONF_FILE, c, 1) == 0;
		if (pid)
			printf ("webd en marche (%d)\n", pid);
		else
			printf ("webd arrêté\n");
		if (!have)
			printf ("pas de configuration : %s\n", CONF_FILE);
		for (int i = 0; i < c->nsites; i++) {
			struct site *s = &c->sites[i];
			printf ("%s %-20s %s\n", s->enabled ? "+" : "-", s->name, s->root);
			for (int j = 0; j < s->nbinds; j++) {
				char b[200];
				bind_format (&s->binds[j], b, sizeof b);
				printf ("      %s\n", b);
			}
		}
		free (c);
		return 0;
	}
	if (!pid) {
		printf ("webd n'est pas en marche\n");
		return !strcmp (op, "stop") ? 0 : 1;
	}
	if (!strcmp (op, "reload")) {
		kill (pid, SIGHUP);
		printf ("webd relit sa configuration\n");
		return 0;
	}
	kill (pid, SIGTERM);
	for (int i = 0; i < 50 && kill (pid, 0) == 0; i++)
		nap (100);
	printf (kill (pid, 0) == 0 ? "webd ne s'arrête pas (%d)\n" : "webd arrêté\n", pid);
	return 0;
}

static int check (void)
{
	struct conf *c = calloc (1, sizeof *c);
	if (conf_load (CONF_FILE, c, 0) < 0) {
		printf ("pas de configuration : %s\n", CONF_FILE);
		return 1;
	}
	int bad = 0;
	for (int i = 0; i < c->nsites; i++) {
		struct site *s = &c->sites[i];
		struct stat st;
		const char *why = !*s->root ? "pas de root" : stat (s->root, &st) != 0 || !S_ISDIR (st.st_mode) ?
			"le dossier n'existe pas" : !s->nbinds ? "aucune liaison" : NULL;
		printf ("%s %-20s %s%s%s\n", s->enabled ? "+" : "-", s->name, s->root, why ? " : " : "", why ? why : "");
		bad |= s->enabled && why;
		for (int j = 0; j < s->nbinds; j++) {
			char b[200], kp[300];
			bind_format (&s->binds[j], b, sizeof b);
			snprintf (kp, sizeof kp, "%s/%s.crt", CERT_DIR, strcmp (s->binds[j].host, "*") ? s->binds[j].host : "default");
			printf ("      %s%s\n", b, s->binds[j].scheme == SCHEME_HTTPS && !file_exists (kp) ?
				" (pas de certificat : un auto-signé sera créé)" : "");
		}
	}
	free (c);
	return bad;
}

static void reload_if_running (void)
{
	int pid = service_pid ();
	if (pid) {
		kill (pid, SIGHUP);
		printf ("(webd relit sa configuration)\n");
	}
}

static int site_cmd (int argc, char **argv)
{
	struct conf *c = calloc (1, sizeof *c);
	conf_load (CONF_FILE, c, 1);
	const char *op = argc > 0 ? argv[0] : "list";
	if (!strcmp (op, "list")) {
		for (int i = 0; i < c->nsites; i++)
			printf ("%s %-20s %s\n", c->sites[i].enabled ? "+" : "-", c->sites[i].name, c->sites[i].root);
		return 0;
	}
	if (argc < 2)
		return usage ();
	const char *name = argv[1];
	struct site *s = conf_find (c, name);
	if (!strcmp (op, "add")) {
		if (s || argc < 3) {
			fprintf (stderr, s ? "webd: le site %s existe déjà\n" : "webd site add NOM DOSSIER [LIAISON...]\n", name);
			return 1;
		}
		if (c->nsites >= MAX_SITES)
			return 1;
		s = &c->sites[c->nsites++];
		memset (s, 0, sizeof *s);
		snprintf (s->name, sizeof s->name, "%s", name);
		snprintf (s->root, sizeof s->root, "%s", argv[2]);
		strcpy (s->index[0], "index.html");
		strcpy (s->index[1], "index.htm");
		s->nindex = 2;
		s->enabled = 1;
		for (int i = 3; i < argc && s->nbinds < MAX_BINDS; i++)
			if (bind_parse (argv[i], &s->binds[s->nbinds]) == 0)
				s->nbinds++;
			else
				fprintf (stderr, "webd: liaison refusée : %s\n", argv[i]);
		if (!s->nbinds)
			bind_parse ("http://*:80", &s->binds[s->nbinds++]);
		if (!file_exists (s->root))
			printf ("(le dossier %s n'existe pas encore)\n", s->root);
	} else if (!s) {
		fprintf (stderr, "webd: pas de site %s\n", name);
		return 1;
	} else if (!strcmp (op, "remove")) {
		int i = (int) (s - c->sites);
		memmove (s, s + 1, (size_t) (c->nsites - i - 1) * sizeof *s);
		c->nsites--;
	} else if (!strcmp (op, "enable") || !strcmp (op, "disable"))
		s->enabled = !strcmp (op, "enable");
	else if (!strcmp (op, "listing") && argc > 2)
		s->listing = atoi (argv[2]) != 0;
	else if (!strcmp (op, "bind") && argc > 3) {
		struct bind b;
		if (bind_parse (argv[3], &b) < 0) {
			fprintf (stderr, "webd: liaison refusée : %s\n", argv[3]);
			return 1;
		}
		int at = -1;
		for (int j = 0; j < s->nbinds; j++)
			if (s->binds[j].scheme == b.scheme && s->binds[j].port == b.port && !strcmp (s->binds[j].host, b.host))
				at = j;
		if (!strcmp (argv[2], "add") && at < 0 && s->nbinds < MAX_BINDS)
			s->binds[s->nbinds++] = b;
		else if (!strcmp (argv[2], "remove") && at >= 0) {
			memmove (&s->binds[at], &s->binds[at + 1], (size_t) (s->nbinds - at - 1) * sizeof b);
			s->nbinds--;
		}
	} else
		return usage ();
	if (conf_save (CONF_FILE, c) < 0) {
		fprintf (stderr, "webd: %s ne peut pas être écrit\n", CONF_FILE);
		return 1;
	}
	printf ("%s : %s\n", CONF_FILE, op);
	reload_if_running ();
	return 0;
}

int main (int argc, char **argv)
{
	setvbuf (stdout, NULL, _IOLBF, 0);
	if (argc < 2 || !strcmp (argv[1], "serve"))
		return serve ();
	const char *op = argv[1];
	if (!strcmp (op, "--dev"))
		return dev (argc - 2, argv + 2);
	if (!strcmp (op, "stop") || !strcmp (op, "reload") || !strcmp (op, "status"))
		return ctl (op);
	if (!strcmp (op, "check"))
		return check ();
	if (!strcmp (op, "site"))
		return site_cmd (argc - 2, argv + 2);
	if (!strcmp (op, "cert"))
		return cert_command (argc - 2, argv + 2);
	if (!strcmp (op, "--version") || !strcmp (op, "version")) {
		printf ("webd " WEBD_VERSION "\n");
		return 0;
	}
	return usage ();
}
