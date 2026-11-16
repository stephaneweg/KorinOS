/* oauth - les comptes OAuth 2.0 du système depuis le shell (docs/OAUTH.md) : les fonctions oauth_* de
 * NetKit, qui passent par le service oauthd.
 *
 *   oauth list
 *   oauth setup NOM clé=valeur...      (sans clé : lues sur l'entrée, une par ligne)
 *   oauth info NOM
 *   oauth status NOM
 *   oauth login NOM                    code d'appareil : affiche le code, attend l'accord
 *   oauth browser NOM [PORT]           affiche l'adresse à ouvrir, attend le retour du navigateur
 *   oauth finish NOM ADRESSE|CODE      l'adresse de retour collée (navigateur sur un autre appareil)
 *   oauth token NOM                    le jeton d'accès (curl -H "Authorization: Bearer $(oauth token NOM)")
 *   oauth forget NOM | remove NOM */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netkit_abi.h>

#define E_AGAIN -11
#define OUT_MAX 16384

static char out[OUT_MAX];

static int usage(void)
{
	fputs("oauth list | setup NOM clé=valeur... | info NOM | status NOM | login NOM | browser NOM [PORT]\n"
	      "      | finish NOM ADRESSE | token NOM | forget NOM | remove NOM   (docs/OAUTH.md)\n",
	      stderr);
	return 2;
}

static int fail(long r)
{
	char e[300];
	if (oauth_error((long)e, sizeof e) > 0)
		fprintf(stderr, "oauth : %s\n", e);
	else
		fprintf(stderr, "oauth : erreur %ld\n", r);
	return 1;
}

/* attend la fin d'une connexion (appareil ou navigateur), une demande par seconde */
static int wait_for(const char *name, int browser)
{
	for (;;) {
		long r = browser ? oauth_browser_poll((long)name) : oauth_device_poll((long)name);
		if (r == 0) {
			printf("%s : connecté.\n", name);
			return 0;
		}
		if (r != E_AGAIN)
			return fail(r);
		sleep(1);
	}
}

int main(int argc, char **argv)
{
	if (!netkit_kit_open()) {
		fputs("oauth : /lib/netkit.so absent\n", stderr);
		return 1;
	}
	if (argc < 2)
		return usage();
	const char *cmd = argv[1], *name = argc > 2 ? argv[2] : 0;
	long r;
	if (!strcmp(cmd, "list")) {
		if ((r = oauth_list((long)out, sizeof out)) < 0)
			return fail(r);
		static const char *const ST[] = {"à connecter", "connecté", "connecté (jeton valable)"};
		for (char *l = out; *l;) {
			char *e = l + strcspn(l, "\n");
			char *t1 = memchr(l, '\t', (size_t)(e - l));
			char *t2 = t1 ? memchr(t1 + 1, '\t', (size_t)(e - t1 - 1)) : 0;
			if (t1 && t2) {
				int st = atoi(t1 + 1);
				printf("%-28.*s %-12.*s %s\n", (int)(t1 - l), l, (int)(e - t2 - 1), t2 + 1,
				       st >= 0 && st <= 2 ? ST[st] : "?");
			}
			l = *e ? e + 1 : e;
		}
		return 0;
	}
	if (!name)
		return usage();
	if (!strcmp(cmd, "setup")) {
		static char conf[OUT_MAX];
		size_t n = 0;
		if (argc > 3) {
			for (int i = 3; i < argc; i++)
				n += (size_t)snprintf(conf + n, n < sizeof conf ? sizeof conf - n : 0, "%s\n", argv[i]);
		} else
			n = fread(conf, 1, sizeof conf - 1, stdin);
		if (n >= sizeof conf)
			return fprintf(stderr, "oauth : configuration trop longue\n"), 1;
		conf[n] = 0;
		if ((r = oauth_setup((long)name, (long)conf)) < 0)
			return fail(r);
		printf("%s : configuré.\n", name);
		return 0;
	}
	if (!strcmp(cmd, "info")) {
		if ((r = oauth_info((long)name, (long)out, sizeof out)) < 0)
			return fail(r);
		fputs(out, stdout);
		return 0;
	}
	if (!strcmp(cmd, "status")) {
		if ((r = oauth_status((long)name)) < 0)
			return fail(r);
		puts(r == 2 ? "connecté (jeton valable)" : r == 1 ? "connecté" : "à connecter");
		return r ? 0 : 3;
	}
	if (!strcmp(cmd, "login")) {
		if ((r = oauth_device_begin((long)name, (long)out, sizeof out)) < 0)
			return fail(r);
		char *f[5] = {0};
		char *p = out;
		for (int i = 0; i < 5 && p && *p; i++) {
			f[i] = p;
			p = strchr(p, '\n');
			if (p)
				*p++ = 0;
		}
		printf("Sur un téléphone ou un ordinateur, ouvrez :\n  %s\net tapez le code :  %s\n", f[1] ? f[1] : "?",
		       f[0] ? f[0] : "?");
		if (f[2] && *f[2])
			printf("(ou directement : %s)\n", f[2]);
		printf("En attente (%s s au plus)...\n", f[3] ? f[3] : "?");
		fflush(stdout);
		return wait_for(name, 0);
	}
	if (!strcmp(cmd, "browser")) {
		long port = argc > 3 ? atol(argv[3]) : 0;
		if ((r = oauth_browser_begin((long)name, port, (long)out, sizeof out)) < 0)
			return fail(r);
		printf("Ouvrez cette adresse dans un navigateur de cette machine :\n%s\n", out);
		printf("(sur un autre appareil : copiez l'adresse où il arrive ensuite, même en erreur, puis\n"
		       " oauth finish %s 'ADRESSE')\nEn attente...\n",
		       name);
		fflush(stdout);
		return wait_for(name, 1);
	}
	if (!strcmp(cmd, "finish")) {
		if (argc < 4)
			return usage();
		if ((r = oauth_browser_finish((long)name, (long)argv[3])) < 0)
			return fail(r);
		printf("%s : connecté.\n", name);
		return 0;
	}
	if (!strcmp(cmd, "token")) {
		if ((r = oauth_token((long)name, (long)out, sizeof out)) < 0)
			return fail(r);
		puts(out);
		return 0;
	}
	if (!strcmp(cmd, "forget") || !strcmp(cmd, "remove")) {
		r = cmd[0] == 'f' ? oauth_forget((long)name) : oauth_remove((long)name);
		if (r < 0)
			return fail(r);
		printf("%s : %s.\n", name, cmd[0] == 'f' ? "jetons oubliés" : "retiré");
		return 0;
	}
	return usage();
}
