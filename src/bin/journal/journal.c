/* journal - copie le journal du noyau dans SD0:/journal.txt (diagnostic sur matériel réel,
 * sans liaison série). Écrit tout de suite. Lancé d'un shell (sortie donnée par le père : Terminal,
 * console, tube), il s'arrête là. Lancé sans sortie (SD0:/etc/startup.txt, comme journal.asm), il
 * écrit de nouveau après 20 s, puis après 60 s (messages USB tardifs). Les arguments sont ignorés ;
 * code 0, 1 si l'écriture a échoué.
 * (Avant le 13/11, il attendait toujours 20 s avant d'écrire, et 60 s avant de finir : tapé au
 * Terminal, il semblait bloqué, et un Ctrl+C avant 20 s ne laissait rien d'écrit.) */
#include "korinrt.h"

#define LOG_MAX 65536

static char logbuf[LOG_MAX];

/* journal -> fichier (save) */
static int save(void)
{
	long n = korin_sys2(SYS_KLOG, logbuf, LOG_MAX);
	if (n < 0)
		return 1;
	long fd = sys_open("SD0:/journal.txt", O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0) {
		lang_puts("journal : écriture impossible\n");
		return 1;
	}
	long w = sys_write(fd, logbuf, (u64)n);
	sys_close(fd);
	if (w != n) {
		lang_puts("journal : écriture impossible\n");
		return 1;
	}
	lang_puts("journal : SD0:/journal.txt écrit\n");
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int r = save();
	long fl = korin_sys0(SYS_PROC_FLAGS);
	if (fl >= 0 && (fl & 2)) /* (sortie donnée par le père : lancé d'un shell) */
		_exit(r);
	sys_sleep(20000);
	save();
	sys_sleep(40000);
	save();
	_exit(0);
}
