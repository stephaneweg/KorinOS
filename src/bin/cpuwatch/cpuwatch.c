/* cpuwatch - charge des coeurs, puis chaque thread du système, une fois, comme cpuwatch.asm :
 *    load 3 0 12 1			(% par coeur)
 *    13 42 2 18250 wifi		(pid, tid, coeur, ms consommées, nom)
 * Deux passages à quelques secondes d'écart donnent qui consomme, et où, sans occuper de coeur.
 * Comme l'asm : arguments ignorés, chaque valeur suivie d'une espace (aussi avant le nom), sortie
 * sans tampon (puts, print_dec d'AppKit), code 0. */
#include "korinrt.h"

#define TH_MAX 64
#define THL_SIZE 64

static u64 info[10];
static u64 proc1[8];
static u8 list[TH_MAX * THL_SIZE];

/* valeur, suivie d'une espace */
static void field(long v)
{
	print_dec(v);
	ak_puts(" ");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	korin_sys1(SYS_SYSINFO, info);
	ak_puts("load ");
	for (int i = 0; i < 4; i++)
		field((long)info[5 + i]);
	ak_puts("\n");
	long from = 0; /* chaque process (SYS_PROCLIST_FROM, un à la fois : pas de plafond) */
	for (;;) {
		if (korin_sys3(SYS_PROCLIST_FROM, proc1, 1, from) <= 0)
			break;
		long pid = (long)proc1[0];
		from = pid & 0xFFFF;
		long n = korin_sys3(SYS_THREAD_LIST, pid, list, TH_MAX);
		if (n <= 0)
			continue;
		for (u8 *t = list; n; n--, t += THL_SIZE) {
			field(pid);
			field(*(long *)t);         /* tid */
			field(*(long *)(t + 24));  /* coeur */
			field(*(long *)(t + 32));  /* ms */
			ak_puts((char *)t + 40);   /* nom */
			ak_puts("\n");
		}
	}
	_exit(0);
}
