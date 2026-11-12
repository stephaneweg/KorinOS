/* tee - copie l'entrée standard sur la sortie et dans des fichiers : tee [-a] [fichiers]
 * (docs/OUTILS.md), comme tee.asm : seul le premier argument peut être une option, et toute
 * option vaut -a (ajout) ; 16 fichiers au plus (les suivants sont ignorés) ; un fichier qui ne
 * s'ouvre pas est signalé, le code reste 0 ; la sortie n'est pas tamponnée. */
#include "korinrt.h"

static u8 buf[T_READ];

int main(int argc, char **argv)
{
	int i = 1, nfd = 0;
	long flags = O_WRITE | O_CREATE | O_TRUNC, fds[16];
	if (argv[1] && is_opt(argv[1])) {
		i = 2;
		flags = O_WRITE | O_CREATE | O_APPEND;
	}
	for (; i < argc && nfd < 16; i++) {
		long fd = sys_open(argv[i], flags);
		if (fd < 0)
			err_file(argv[i]);
		else
			fds[nfd++] = fd;
	}
	for (;;) {
		long n = sys_read(0, buf, T_READ);
		if (n <= 0)
			break;
		sys_write(1, buf, (u64)n);
		for (int k = 0; k < nfd; k++)
			sys_write(fds[k], buf, (u64)n);
	}
	for (int k = 0; k < nfd; k++)
		sys_close(fds[k]);
	return 0;
}
