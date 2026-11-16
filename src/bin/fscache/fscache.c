/* fscache - le cache de données de fatfs (compteurs, essais avec et sans) :
 *	fscache [volume] [on|off|drop]
 * comme fscache.asm : la ligne d'arguments brute du noyau (krt_line), pas argv : « on », « off »
 * ou « drop » seul vise sd0 ; sinon le premier mot est le volume (mis en minuscules) et ce qui suit
 * la première espace doit être exactement on, off ou drop (une deuxième espace : l'usage, sur la
 * sortie, code 1). off : coupe et vide ; drop : vide ; on : remet. Sortie sans tampon (puts,
 * print_dec d'AppKit), comme l'asm. */
#include "korinrt.h"

/* mot -> FS_CC_*, 0 s'il n'est pas on, off ni drop */
static long word_op(const char *w)
{
	if (str_eq(w, "on"))
		return FS_CC_ON;
	if (str_eq(w, "off"))
		return FS_CC_OFF;
	if (str_eq(w, "drop"))
		return FS_CC_DROP;
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	static char name[300] = "fs.root";
	static u64 msg[MSG_SIZE / 8];
	const char *l = krt_line;
	long op = 0;
	if (l && *l) {
		op = word_op(l); /* « fscache off » : / */
		if (!op) {
			char *o = name + 3;
			for (;;) {
				u8 c = (u8)*l++;
				if (!c) {
					*o = 0;
					break;
				}
				if (c == ' ') {
					*o = 0;
					op = word_op(l);
					if (!op) {
						ak_puts("fscache [volume] [on|off|drop]\n");
						_exit(1);
					}
					break;
				}
				if ((u8)(c - 'A') <= 25) /* (vfs : en minuscules) */
					c += 32;
				*o++ = (char)c;
			}
		}
	}
	if (name[3] == '/') { /* un point de montage (« /media/usb0 ») : son nom interne */
		vu_name(name + 3);
		str_cat(name + 3, vu_vol);
	}
	long r = korin_sys2(SYS_SVC_OPEN, name, 1000);
	if (r < 0)
		goto err;
	msg[0] = FS_CACHE;
	msg[1] = (u64)op;
	r = korin_sys2(SYS_SVC_CALL, r, msg);
	if (r < 0)
		goto err;
	lang_puts("succes ");
	print_dec((long)msg[MSG_A / 8]);
	lang_puts(", defauts ");
	print_dec((long)msg[MSG_B / 8]);
	lang_puts(", en memoire ");
	print_dec((long)(msg[MSG_C / 8] >> 4));
	lang_puts(" Mo, remplaces ");
	print_dec((long)msg[MSG_D / 8]);
	ak_puts("\n");
	_exit(0);
err:
	lang_puts("fscache : erreur ");
	print_dec(r);
	ak_puts("\n");
	_exit(1);
}
