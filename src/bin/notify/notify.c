/* notify - « notify [-a app] <titre> <message> » : une notification (notifyd, comme dans Onyx),
 * comme notify.asm. Un titre de plusieurs mots s'écrit entre guillemets ; -a : l'app qui l'envoie
 * (son icône, la toucher l'ouvre : réglages rapides).
 *
 * Comme l'asm : la ligne d'arguments est lue telle quelle (pas argv) : espaces seulement (pas les
 * tabulations) ; « -a » doit être suivi d'une espace, puis du nom et d'une espace (sinon : usage) ;
 * un titre entre guillemets sans guillemet fermant va jusqu'au bout (sans message) ; le message
 * est le reste de la ligne. L'usage rend 0 ; notifyd absent : 1. */
#include "korinrt.h"

static void usage(void)
{
	lang_puts("usage : notify [-a app] <titre> <message>  (titre en plusieurs mots : entre guillemets)\n");
	_exit(0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!krt_line)
		usage();
	u64 n = (u64)str_len(krt_line) + 1;
	char *p = malloc(n);
	memcpy(p, krt_line, n);
	const char *app = 0;
	while (*p == ' ')
		p++;
	if (p[0] == '-' && p[1] == 'a' && p[2] == ' ') { /* « -a app » */
		p += 3;
		while (*p == ' ')
			p++;
		app = p;
		while (*p != ' ') {
			if (!*p)
				usage();
			p++;
		}
		*p++ = 0;
	}
	while (*p == ' ') /* espaces devant */
		p++;
	if (!*p)
		usage();
	char *q, *text;
	if (*p == '"') { /* « "titre en plusieurs mots" » */
		p++;
		q = p;
		while (*q && *q != '"')
			q++;
	} else { /* un mot */
		q = p;
		while (*q && *q != ' ')
			q++;
	}
	if (!*q)
		text = ""; /* titre sans message */
	else {
		*q++ = 0;
		while (*q == ' ') /* le message : le reste */
			q++;
		text = q;
	}
	if (notify_send(p, text, app) < 0) {
		lang_puts("notify : notifyd absent (pas de bureau ?)\n");
		_exit(1);
	}
	_exit(0);
}
