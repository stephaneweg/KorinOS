/* secret : le coffre en ligne de commande (docs/KEYVAULT.md). L'identité est celle du programme qui lance
 * secret (son père : un script bash ou Python, déclaré par son interprète) : un script range et relit ses propres
 * secrets, sans fichier en clair.
 *
 *   secret set NOM [VALEUR]   range (sans VALEUR : lue sur l'entrée standard)
 *   secret get NOM            écrit la valeur sur la sortie standard
 *   secret rm NOM             oublie
 *   secret ls                 les noms de ce programme
 *   secret whoami             l'identité vue par le coffre
 *   secret status             le coffre du compte : ouvert, fermé, pas encore créé
 *   secret unlock             ouvre le coffre du compte (mot de passe demandé)
 *   secret lock               ferme le coffre du compte
 *
 * Exemple dans un script : ANTHROPIC_API_KEY=$(secret get anthropic) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "../../lib/keyvault/keyvault.h"

static int usage(void)
{
	fputs("usage : secret set NOM [VALEUR] | get NOM | rm NOM | ls | whoami | status | unlock | lock\n", stderr);
	return 2;
}

static int err(long r, const char *what)
{
	char t[300] = "";
	long n = kv_error(t, sizeof t);
	if (n > 0)
		fprintf(stderr, "secret : %s : %s (%ld)\n", what, t, r);
	else
		fprintf(stderr, "secret : %s : erreur %ld\n", what, r);
	return 1;
}

static void read_hidden(char *out, size_t n, const char *prompt)
{
	struct termios old, t;
	int tty = isatty(0) && tcgetattr(0, &old) == 0;
	if (tty) {
		fputs(prompt, stderr);
		t = old;
		t.c_lflag &= ~(tcflag_t)ECHO;
		tcsetattr(0, TCSANOW, &t);
	}
	if (!fgets(out, (int)n, stdin))
		out[0] = 0;
	out[strcspn(out, "\r\n")] = 0;
	if (tty) {
		tcsetattr(0, TCSANOW, &old);
		fputc('\n', stderr);
	}
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return usage();
	const char *cmd = argv[1];
	long r;
	if (!strcmp(cmd, "status")) {
		r = kv_status();
		if (r < 0)
			return err(r, "status");
		puts(r == KVS_OPEN ? "ouvert" : r == KVS_LOCKED ? "fermé" : "pas encore créé");
		return 0;
	}
	if (!strcmp(cmd, "unlock")) {
		char pw[200];
		read_hidden(pw, sizeof pw, "Mot de passe du compte : ");
		r = kv_call_(KV_UNLOCK, 0, pw, (long)strlen(pw) + 1, 0, 0);
		memset(pw, 0, sizeof pw);
		return r < 0 ? err(r, "unlock") : 0;
	}
	if (!strcmp(cmd, "lock")) {
		r = kv_call_(KV_LOCK, 0, "", 0, 0, 0);
		return r < 0 ? err(r, "lock") : 0;
	}
	kv_parent_ = getppid(); /* (l'identité : le programme qui a lancé secret) */
	if (!strcmp(cmd, "whoami")) {
		char t[400];
		r = kv_request(KV_WHOAMI, 0, 0, t, sizeof t);
		if (r < 0)
			return err(r, "whoami");
		fputs(t, stdout);
		return 0;
	}
	if (!strcmp(cmd, "ls")) {
		static char t[KV_VALUE_MAX];
		r = kv_list(t, sizeof t);
		if (r < 0)
			return err(r, "ls");
		fputs(t, stdout);
		return 0;
	}
	if (argc < 3)
		return usage();
	const char *name = argv[2];
	if (!strcmp(cmd, "get")) {
		static char t[KV_VALUE_MAX + 1];
		r = kv_get(name, t, KV_VALUE_MAX);
		if (r < 0)
			return err(r, name);
		fwrite(t, 1, (size_t)r, stdout);
		if (isatty(1))
			fputc('\n', stdout);
		memset(t, 0, sizeof t);
		return 0;
	}
	if (!strcmp(cmd, "rm")) {
		r = kv_delete(name);
		return r < 0 ? err(r, name) : 0;
	}
	if (!strcmp(cmd, "set")) {
		static char t[KV_VALUE_MAX + 1];
		size_t n;
		if (argc >= 4) {
			n = strlen(argv[3]);
			if (n > KV_VALUE_MAX)
				return err(-34, name);
			memcpy(t, argv[3], n);
		} else if (isatty(0)) {
			read_hidden(t, sizeof t, "Valeur : ");
			n = strlen(t);
		} else {
			n = fread(t, 1, KV_VALUE_MAX + 1, stdin);
			while (n && (t[n - 1] == '\n' || t[n - 1] == '\r'))
				n--; /* (« echo clé | secret set nom » : sans la fin de ligne) */
			if (n > KV_VALUE_MAX) {
				memset(t, 0, sizeof t);
				fprintf(stderr, "secret : %s : valeur trop grande (%d octets au plus)\n", name, KV_VALUE_MAX);
				return 1;
			}
		}
		r = kv_set(name, t, (long)n);
		memset(t, 0, sizeof t);
		return r < 0 ? err(r, name) : 0;
	}
	return usage();
}
