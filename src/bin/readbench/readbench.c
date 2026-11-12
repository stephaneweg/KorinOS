/* readbench - débit de lecture d'un fichier (diagnostic de la carte), comme
 * readbench.asm :
 *	readbench [chemin]	(SD0:/apps/jet.app/main par défaut)
 * lit 16 Mo au plus par requêtes de 4, 16, 64 et 256 Ko, le pilote de la carte
 * (blk.sd0) dormant jusqu'à la fin du DMA, puis la sondant (attente active de
 * 8 000 us, laissée ainsi à la fin, comme l'asm) ; affiche ms et Ko/s de
 * chacune, et ce que la carte y a passé (le reste : fatfs et les changements de
 * contexte), puis la charge des cœurs. Lecture seule. Le chemin est la ligne
 * brute du noyau (krt_line), comme l'asm. Fichier absent : « readbench :
 * ouverture impossible », code 1. */
#include "korinrt.h"

#define TOTAL (16 * 1024 * 1024)
#define PROBE 8000 /* attente active de la seconde passe (us) */
#define BLK_STATS 7

static u8 buffer[262144] __attribute__((aligned(8)));
static long chan;
static u64 msg[64];
static u64 info[10];

/* attente active en us (-1 : inchangée) -> sommeils, compteurs dans msg */
static long stats(long wait)
{
	msg[0] = BLK_STATS;
	msg[1] = (u64)wait;
	return korin_sys2(SYS_SVC_CALL, chan, msg);
}

/* la charge de chaque cœur, en % (charges) */
static void loads(void)
{
	ak_puts("charge des coeurs (%) : ");
	korin_sys1(SYS_SYSINFO, info);
	for (int i = 0; i < 4; i++) {
		print_dec((long)info[5 + i]);
		ak_puts(" ");
	}
	ak_puts("\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	static const u32 sizes[] = {4096, 16384, 65536, 262144, 0};
	const char *path = krt_line && *krt_line ? krt_line : "SD0:/apps/jet.app/main";
	chan = korin_sys2(SYS_SVC_OPEN, "blk.sd0", 1000); /* (sans vérification, comme l'asm) */
	for (long wait = 0;; wait = PROBE) {
		ak_puts(wait ? "-- le pilote sonde la fin du DMA\n" : "-- le pilote dort jusqu'a la fin du DMA\n");
		for (int s = 0; sizes[s]; s++) {
			u64 size = sizes[s];
			stats(wait);
			long fd = sys_open(path, O_READ);
			if (fd < 0) {
				ak_puts("readbench : ouverture impossible\n");
				_exit(1);
			}
			long t = sys_time_ms();
			u64 n = 0;
			for (;;) {
				long r = sys_read(fd, buffer, size);
				if (r <= 0)
					break;
				n += (u64)r;
				if (n >= TOTAL)
					break;
			}
			t = sys_time_ms() - t;
			sys_close(fd);
			print_dec((long)(size >> 10));
			ak_puts(" Ko par requete : ");
			print_dec(t);
			ak_puts(" ms, ");
			print_dec((long)((n >> 10) * 1000 / (u64)(t + 1)));
			ak_puts(" Ko/s\n");
			long sleeps = stats(-1);
			ak_puts("  carte : ");
			print_dec((long)msg[1]);
			ak_puts(" commandes, ");
			print_dec((long)((msg[2] + msg[3] + msg[4]) / 1000));
			ak_puts(" ms ; sommeils ");
			print_dec(sleeps);
			ak_puts("\n");
		}
		loads();
		if (wait)
			break;
	}
	_exit(0);
}
