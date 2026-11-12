/* journal - copie le journal du noyau dans SD0:/journal.txt (diagnostic sur matériel réel,
 * sans liaison série), comme journal.asm. Lancé depuis SD0:/etc/startup.txt : écrit après 20 s,
 * puis de nouveau après 60 s (messages USB tardifs). Les arguments sont ignorés ; code 0. */
#include "korinrt.h"

#define LOG_MAX 65536

static char logbuf[LOG_MAX];

/* journal -> fichier (save) */
static void save(void)
{
	long n = korin_sys2(SYS_KLOG, logbuf, LOG_MAX);
	if (n < 0)
		return;
	long fd = sys_open("SD0:/journal.txt", O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0) {
		lang_puts("journal : écriture impossible\n");
		return;
	}
	sys_write(fd, logbuf, (u64)n);
	sys_close(fd);
	lang_puts("journal : SD0:/journal.txt écrit\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	sys_sleep(20000);
	save();
	sys_sleep(40000);
	save();
	_exit(0);
}
