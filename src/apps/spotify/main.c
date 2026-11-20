/*
 * main.c -- le lanceur des apps web de Korin (Spotify ; Netflix, Disney+ plus tard) : le site dans sa propre fenêtre,
 * sur le moteur de Jet, avec le module Widevine de Google (docs/etude/MUSIQUE.md, docs/etude/WIDEVINE.md). Spotify :
 * le lecteur web de Spotify (open.spotify.com), connecté avec le compte de l'utilisateur, sans Client ID.
 *
 * Ce programme est le même pour chaque app web : il prend le nom de son dossier (/apps/<nom>.app/main) et devient
 * Jet en mode app (execv : le même process, donc la même app pour le panneau) :
 *   /apps/jet.app/main --app=<nom> [--gpu | --nogpu] [adresse]
 * Le reste (le titre, l'adresse de départ, les domaines gardés, le lecteur) est dans /apps/<nom>.app/webapp.txt ; la
 * fenêtre, la session (HOME/<nom>/webkit), la proposition du module Widevine et la carte de lecture réduite sont dans
 * Jet (src/extra/onyxports/jet/webapp.cpp).
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

int main (int argc, char **argv)
{
	static char name[64], opt[80];
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
	/* (--gpu, --nogpu et une adresse en https passent à Jet) */
	char *args[8];
	int n = 0;
	args[n++] = (char *) JET;
	args[n++] = opt;
	for (int i = 1; i < argc && n < 6; i++)
		if (!strcmp (argv[i], "--gpu") || !strcmp (argv[i], "--nogpu") || !strncmp (argv[i], "https://", 8))
			args[n++] = argv[i];
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
