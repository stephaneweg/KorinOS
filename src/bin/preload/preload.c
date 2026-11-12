/* preload - images des programmes (docs/ABI.md, « Images des exécutables »), comme preload.asm :
 *   preload                  liste les images : sorte (épinglée / passagère, droits), taille du
 *                            fichier, pages, références, chemin
 *   preload <chemin>...      épingle l'image de chaque programme (SYS_PRELOAD d'AppKit : lue une
 *                            fois, gardée tant que le fichier ne change pas ; unload la retire)
 * Comme l'asm : les mots de la ligne d'arguments, coupés aux espaces seulement ; une ligne vide ou
 * d'espaces ne fait rien (code 0) ; seule une ligne absente donne la liste ; une erreur :
 * « : erreur -N », code 1, et on continue ; chaque image épinglée envoie la notification
 * « Préchargement » (rien sans notifyd). Sortie sans tampon (puts d'AppKit). */
#include "korinrt.h"

#define LIST_MAX 128
#define IML_FSIZE 0
#define IML_PAGES 8
#define IML_REFS 24
#define IML_KIND 40
#define IML_PATH 48
#define IML_SIZE 128
#define IML_PIN 1
#define IML_SYS 8

static u8 list[LIST_MAX * IML_SIZE];

static void list_images(void)
{
	long n = korin_sys2(SYS_IMAGE_LIST, list, LIST_MAX);
	if (n < 0) {
		lang_puts("preload : liste des images refusée\n");
		_exit(1);
	}
	if (!n) {
		lang_puts("preload : aucune image\n");
		return;
	}
	lang_puts("sorte       droits  Ko    pages  process  chemin\n");
	for (u8 *e = list; n; n--, e += IML_SIZE) {
		u64 kind = *(u64 *)(e + IML_KIND);
		lang_puts(kind & IML_PIN ? "epinglee  " : "passagere ");
		ak_puts(kind & IML_SYS ? "  sys   " : "        ");
		print_dec((long)(*(u64 *)(e + IML_FSIZE) >> 10));
		ak_puts("  ");
		print_dec(*(long *)(e + IML_PAGES));
		ak_puts("  ");
		print_dec(*(long *)(e + IML_REFS) - 1);
		ak_puts("  ");
		ak_puts((char *)e + IML_PATH);
		ak_puts("\n");
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int code = 0;
	if (!krt_line) {
		list_images();
		_exit(0);
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
		long r = ksys(SYS_PRELOAD, (long)w, 0, 0, 0, 0, 0);
		lang_puts("preload : ");
		ak_puts(w);
		if (r < 0) {
			lang_puts(" : erreur ");
			print_dec(r);
			ak_puts("\n");
			code = 1;
		} else {
			lang_puts(" : image épinglée\n");
			preload_notify(w);
		}
		if (!sep)
			break;
		p++;
	}
	_exit(code);
}
