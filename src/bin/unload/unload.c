/* unload - retire les images d'un programme (docs/ABI.md, « Images des exécutables »), comme
 * unload.asm :
 *   unload <chemin>...       le prochain lancement relit le fichier ; les process déjà lancés
 *                            gardent la leur
 * Comme l'asm : les mots de la ligne d'arguments, coupés aux espaces seulement (pas de
 * guillemets) ; une ligne absente ou vide : usage (code 1) ; une ligne d'espaces : rien (code 0) ;
 * une erreur : « : erreur -N », code 1, et on continue. Sortie sans tampon (puts d'AppKit). */
#include "korinrt.h"

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int code = 0;
	if (!krt_line || !*krt_line) {
		lang_puts("usage : unload <chemin>...\n");
		_exit(1);
	}
	u64 n = (u64)str_len(krt_line) + 1;
	char *p = malloc(n);
	memcpy(p, krt_line, n);
	for (;;) { /* chaque mot de la ligne */
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		char *w = p;
		while (*p && *p != ' ')
			p++;
		char sep = *p;
		*p = 0;
		long r = ksys(SYS_UNLOAD, (long)w, 0, 0, 0, 0, 0);
		lang_puts("unload : ");
		ak_puts(w);
		if (r < 0) {
			lang_puts(" : erreur ");
			print_dec(r);
			ak_puts("\n");
			code = 1;
		} else {
			lang_puts(" : ");
			print_dec(r);
			lang_puts(" image(s) oubliée(s)\n");
		}
		if (!sep)
			break;
		p++;
	}
	_exit(code);
}
