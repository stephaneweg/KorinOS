/*
 * launcher.c -- le lanceur commun des apps web de Korin (Spotify, Netflix ; Disney+ plus tard) : le site dans sa propre
 * fenêtre, sur le moteur de Jet, avec le module Widevine de Google (docs/etude/MUSIQUE.md, docs/etude/WIDEVINE.md).
 * Chaque app web (src/apps/<nom>) n'a que son main.c, qui inclut ce fichier, un webapp.txt et un app.txt.
 *
 * Ce programme est le même pour chaque app web : il prend le nom de son dossier (/apps/<nom>.app/main) et devient
 * Jet en mode app (execv : le même process, donc la même app pour le panneau) :
 *   /apps/jet.app/main --app=<nom> [--gpu | --nogpu] [adresse]
 * Le reste (le titre, l'adresse de départ, les domaines gardés, le lecteur) est dans /apps/<nom>.app/webapp.txt ;
 * « url.<langue>-<pays> = ... » (url.fr-be), puis « url.<langue> = ... » (url.fr) y donnent l'adresse de départ propre
 * à la langue (/etc/system.ini) et au pays (/etc/options.txt) des Réglages, passée à Jet par ce lanceur quand on ne
 * lui en donne pas. La fenêtre, la session (HOME/<nom>/webkit), la proposition du module
 * Widevine et la carte de lecture réduite sont dans Jet (src/extra/onyxports/jet/webapp.cpp).
 *
 * MIT License -- Copyright (c) 2026 Stéphane Wegener.
 */
#include <uikit.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define JET	"/apps/jet.app/main"

static struct ui_vars ui;

/* libposix de Korin (proc.c) : le nom du process après le prochain execve -- celui de l'app, pas « jet » : le panneau
 * montre l'icône de l'app et ramène sa fenêtre */
void korin_exec_name (const char *name);

/* le nom de l'app : « nom » de .../<nom>.app/main (argv[0]) ; « spotify » sinon */
static void app_name (const char *argv0, char *out, size_t cap)
{
	snprintf (out, cap, "spotify");
	if (!argv0) return;
	const char *e = strstr (argv0, ".app/");
	if (!e)
	{
		/* (un lancement par le panneau peut ne donner que le nom) */
		if (*argv0 && !strpbrk (argv0, "/:.") && strlen (argv0) < cap) snprintf (out, cap, "%s", argv0);
		return;
	}
	const char *b = e;
	while (b > argv0 && b[-1] != '/' && b[-1] != ':') b--;
	size_t n = (size_t) (e - b);
	if (n == 0 || n >= cap) return;
	memcpy (out, b, n);
	out[n] = 0;
}

/* la langue de l'interface (Réglages) : « lang = xx » de /etc/system.ini, comme lang_get d'AppKit ; « fr » sans
 * cette clé (la langue des sources) */
static void system_lang (char *out, size_t cap)
{
	snprintf (out, cap, "fr");
	FILE *f = fopen ("/etc/system.ini", "rb");
	if (!f) return;
	char line[256];
	while (fgets (line, sizeof line, f))
	{
		if (strncmp (line, "lang", 4) != 0) continue;
		const char *s = line + 4;
		while (*s == ' ' || *s == '\t' || *s == '=') s++;
		size_t k = 0;
		while (k + 1 < cap && k < 7 && s[k] >= 'a' && s[k] <= 'z') { out[k] = s[k]; k++; }
		if (k) out[k] = 0;
		break;
	}
	fclose (f);
}

/* le pays (Réglages, Clavier, langue et région) : « pays=XX » de /etc/options.txt, en minuscules ; « fr » sans cette
 * clé (comme le volet des Réglages) */
static void system_country (char *out, size_t cap)
{
	snprintf (out, cap, "fr");
	FILE *f = fopen ("/etc/options.txt", "rb");
	if (!f) return;
	char line[256];
	while (fgets (line, sizeof line, f))
	{
		if (strncmp (line, "pays", 4) != 0) continue;
		const char *s = line + 4;
		while (*s == ' ' || *s == '\t' || *s == '=') s++;
		size_t k = 0;
		while (k + 1 < cap && k < 3 && ((s[k] >= 'a' && s[k] <= 'z') || (s[k] >= 'A' && s[k] <= 'Z')))
		{
			out[k] = (char) (s[k] | 0x20);
			k++;
		}
		if (k == 2) out[k] = 0;
		else snprintf (out, cap, "fr");
		break;
	}
	fclose (f);
}

/* l'adresse de départ propre à une langue ou à une langue et un pays : « url.<suffixe> = ... » de
 * /apps/<nom>.app/webapp.txt (Disney+ : url.fr = .../fr-fr/home ; TF1+ : url.fr-be = .../fr-be/, sans la redirection
 * ni la question du site) -> 1 si trouvée */
static int config_url (const char *name, const char *suffix, char *out, size_t cap)
{
	char path[160], key[24];
	snprintf (path, sizeof path, "/apps/%s.app/webapp.txt", name);
	snprintf (key, sizeof key, "url.%s", suffix);
	FILE *f = fopen (path, "rb");
	if (!f) return 0;
	char line[1024];
	int found = 0;
	size_t kn = strlen (key);
	while (!found && fgets (line, sizeof line, f))
	{
		if (strncmp (line, key, kn) != 0 || (line[kn] != ' ' && line[kn] != '\t' && line[kn] != '=')) continue;
		const char *s = line + kn;
		while (*s == ' ' || *s == '\t' || *s == '=') s++;
		size_t n = strcspn (s, " \t\r\n");
		if (n == 0 || n >= cap || strncmp (s, "https://", 8) != 0) continue;
		memcpy (out, s, n);
		out[n] = 0;
		found = 1;
	}
	fclose (f);
	return found;
}

int main (int argc, char **argv)
{
	static char name[64], opt[80], lang[8], url[1024];
	app_name (argc > 0 ? argv[0] : 0, name, sizeof name);
	snprintf (opt, sizeof opt, "--app=%s", name);
	/* un process de WebKit (--onyx-webkit-process=web, network : webapp.cpp, engine_set_helper_path) : Jet tel quel,
	 * avec tous ses arguments, sous le nom de l'app */
	if (argc >= 2 && !strncmp (argv[1], "--onyx-webkit-process=", 22))
	{
		argv[0] = (char *) JET;
		korin_exec_name (name);
		execv (JET, argv);
		perror ("webapp: " JET);
		return 1;
	}
	/* (--gpu, --nogpu et une adresse en https passent à Jet ; sans adresse, celle de la langue et du pays des
	 * Réglages (url.fr-be), sinon de la langue (url.fr), si webapp.txt en a une ; sinon Jet prend url de webapp.txt) */
	char *args[8];
	int n = 0, has_url = 0;
	args[n++] = (char *) JET;
	args[n++] = opt;
	for (int i = 1; i < argc && n < 6; i++)
		if (!strcmp (argv[i], "--gpu") || !strcmp (argv[i], "--nogpu") || !strncmp (argv[i], "https://", 8))
		{
			if (argv[i][0] == 'h') has_url = 1;
			args[n++] = argv[i];
		}
	if (!has_url)
	{
		char country[4], both[16];
		system_lang (lang, sizeof lang);
		system_country (country, sizeof country);
		snprintf (both, sizeof both, "%s-%s", lang, country);
		if (config_url (name, both, url, sizeof url) || config_url (name, lang, url, sizeof url)) args[n++] = url;
	}
	args[n] = 0;
	if (access (JET, X_OK) == 0)
	{
		korin_exec_name (name);
		execv (JET, args);
	}
	perror ("webapp: " JET);
	if (uikit_open (&ui))
		ui_message ((long) name, (long) "Cette app a besoin de Jet, le navigateur de Korin (le paquet jet), avec la "
			"lecture des contenus protégés (Korin PC).", UM_OK);
	return 1;
}
