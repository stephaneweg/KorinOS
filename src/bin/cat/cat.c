/* cat - concatène des fichiers sur la sortie : cat [fichiers] (- : entrée standard)
 * (docs/OUTILS.md, comme cat.asm : sans option ; code 1 si un fichier manque) */
#include "korinrt.h"

static u8 buf[T_READ];

/* chemin (0 : entrée standard) -> 0, 1 si inaccessible */
static int copy(const char *path)
{
	long fd = in_open(path);
	if (fd < 0) {
		err_file(path);
		return 1;
	}
	out_flush();
	for (;;) {
		long n = sys_read(fd, buf, T_READ);
		if (n <= 0)
			break;
		long w = sys_write(1, buf, (u64)n);
		if (w < 0) { /* (E_NOSPC : exit le dit, code 1) */
			if (!krt_out_err)
				krt_out_err = w;
			break;
		}
	}
	in_close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	int code = 0;
	if (argc == 1)
		copy(0);
	for (int i = 1; i < argc; i++)
		code |= copy(argv[i]);
	return code;
}
