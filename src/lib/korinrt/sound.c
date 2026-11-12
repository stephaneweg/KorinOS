/* sound.c : lot « sound » (son et médias : play, soundctl, volume, sontest, synthtest,
 * codectest, mediatest), docs/BIN-C.md. */
#include "korinrt.h"

/* La boucle de découpe de play.asm et codectest.asm (pas t_ligne_coupee) : la ligne est coupée
 * sur place aux espaces (seulement les espaces, sans guillemets) ; au plus max mots (au-delà, la
 * suite de la ligne reste collée au dernier mot gardé... sauf l'espace qui le termine). */
long krt_split_spaces(char *s, char **words, long max)
{
	long n = 0;
	if (!s)
		return 0;
	for (;;) {
		while (*s == ' ')
			*s++ = 0;
		if (!*s || n >= max)
			break;
		words[n++] = s;
		while (*s && *s != ' ')
			s++;
		if (!*s)
			break;
	}
	return n;
}

/* proc_find de src/inc/procfind.inc : SYS_PROCLIST_FROM par paquets de 16 entrées (64 o : pid,
 * nom à +24, 32 o comparés au plus) ; after : pid après lequel chercher (0 : depuis le début) */
long proc_find(const char *name, long after)
{
	u8 lot[16 * 64];
	long from = after & 0xFFFF;
	for (;;) {
		long n = korin_sys3(SYS_PROCLIST_FROM, lot, 16, from);
		if (n <= 0)
			return 0;
		u8 *e = lot;
		for (long k = n; k; k--, e += 64) {
			from = (long)(*(u64 *)e & 0xFFFF); /* (la suite : après cet emplacement) */
			const u8 *a = (const u8 *)name, *b = e + 24;
			int i;
			for (i = 0; i < 32; i++) {
				if (a[i] != b[i])
					break;
				if (!a[i])
					return (long)*(u64 *)e;
			}
			if (i == 32)
				return (long)*(u64 *)e;
		}
		if (e != lot + 16 * 64) /* paquet plein : il y a peut-être une suite */
			return 0;
	}
}
