/* pibuild - construit un programme : pibuild [app|bin|sys|svc] dossier nom (docs/GCC.md), comme
 * pibuild.asm. Du C, du C++, du BASIC ou de l'assembleur : c'est /gcc/share/pibuild.mk, lancé
 * par make dans le dossier, qui choisit, compile, lie et installe selon le type (sans type :
 * app) : /apps/<nom>.app, /bin/<nom>, SD0:/sys/<nom> ou SD0:/sys/svc/<nom>.
 * Comme l'asm : moins de deux arguments : usage, code 2 ; trois et plus : le premier est le type
 * (16 octets au plus, pibuild.mk le vérifie), les suivants en trop sont ignorés ; le nom est coupé
 * à 240 octets ; make est lancé avec PATH=/bin:/gcc/bin pour tout environnement ; code : celui
 * de make (1 sans /gcc/bin/make). */
#include "korinrt.h"

static char a_typeopt[5 + 20] = "TYPE=app";
static char a_nameopt[5 + 244] = "NAME=";

/* destination, chaîne, octets au plus (copy) */
static void copy(char *d, const char *s, long n)
{
	for (;;) {
		char c = *s++;
		*d++ = c;
		if (!c)
			return;
		if (!--n) {
			*d = 0;
			return;
		}
	}
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		out_say("pibuild [app|bin|sys|svc|lib] dossier nom\n");
		exit(2);
	}
	int i = 1;
	if (argc >= 4) {
		copy(a_typeopt + 5, argv[1], 16); /* TYPE=type */
		i = 2;
	}
	char *dir = argv[i++]; /* -C dossier */
	copy(a_nameopt + 5, argv[i], 240); /* NAME=nom */
	char *margv[] = { "make", "-C", dir, "-f", "/gcc/share/pibuild.mk", "-j4", a_nameopt, a_typeopt, 0 };
	char *envp[] = { "PATH=/bin:/gcc/bin", 0 };
	static const long fds[3] = { 0, 1, 2 };
	long pid = f_spawn_env("SD0:/gcc/bin/make", margv, envp, fds);
	if (pid < 0) {
		out_say("pibuild : pas de /gcc/bin/make\n");
		exit(1);
	}
	exit((int)korin_sys1(SYS_WAIT, pid));
}
