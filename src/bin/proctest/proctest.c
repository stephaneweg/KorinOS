/* proctest - essai du nombre de process (le noyau n'a plus de plafond :
 * src/kernel/proc.asm), comme proctest.asm :
 *   proctest [N]	lance N fois « sleep 60 » (100 par défaut, 2000 au plus),
 *			compte les process (SYS_PROCLIST_FROM), les arrête (kill),
 *			attend chacun (wait), puis vérifie qu'un pid neuf n'est
 *			aucun des anciens.
 * Comme l'asm : N vient de la ligne brute du noyau (krt_line) lue par
 * parse_dec d'AppKit ; 0, négatif ou pas un nombre : 100. Sortie par puts et
 * print_dec d'AppKit (sans tampon), code 0. */
#include "korinrt.h"

#define N_MAX 2000
#define CHUNK 64

static long pids[N_MAX];
static u8 procs[CHUNK * 64];

/* -> process vivants, *pid_max = plus grand pid vu (SYS_PROCLIST_FROM) */
static long count_procs(u64 *pid_max)
{
	long alive = 0;
	u64 from = 0, max = 0;
	for (;;) {
		long n = korin_sys3(SYS_PROCLIST_FROM, procs, CHUNK, from);
		if (n <= 0)
			break;
		alive += n;
		for (long i = 0; i < n; i++) {
			u64 pid = *(u64 *)(procs + i * 64);
			if (pid > max)
				max = pid;
			from = pid & 0xFFFF; /* (emplacement de départ : celui du dernier, comme l'asm) */
		}
		if (n != CHUNK)
			break;
	}
	*pid_max = max;
	return alive;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long n = 100;
	if (krt_line && krt_line[0]) {
		long v = (long)parse_dec(krt_line);
		if (v > 0)
			n = v;
		if ((u64)n > N_MAX)
			n = N_MAX;
	}
	long started = 0;
	while (started < n) {
		long pid = ksys(SYS_SPAWN, (long)"SD0:/bin/sleep", (long)"60", 0, 0, 0, 0);
		if (pid <= 0) {
			ak_puts("proctest : lancement refuse, code ");
			print_dec(pid);
			ak_puts("\n");
			break;
		}
		pids[started++] = pid;
	}
	ak_puts("proctest : lances ");
	print_dec(started);
	ak_puts("\n");
	u64 max;
	long alive = count_procs(&max);
	ak_puts("proctest : process vivants ");
	print_dec(alive);
	ak_puts(", plus grand pid ");
	print_dec((long)max);
	ak_puts("\n");
	for (long i = 0; i < started; i++)
		korin_sys1(SYS_KILL, pids[i]);
	long killed = 0; /* attendus avec le code -9 (kill) */
	for (long i = 0; i < started; i++)
		if (korin_sys1(SYS_WAIT, pids[i]) == -9)
			killed++;
	ak_puts("proctest : arretes et attendus (code -9) ");
	print_dec(killed);
	ak_puts(" sur ");
	print_dec(started);
	ak_puts("\n");
	long fresh = ksys(SYS_SPAWN, (long)"SD0:/bin/true", 0, 0, 0, 0, 0); /* un pid neuf */
	ak_puts("proctest : pid neuf ");
	print_dec(fresh);
	long i;
	for (i = 0; i < started; i++)
		if (pids[i] == fresh)
			break;
	if (i < started)
		ak_puts(", DEJA DONNE : un pid a resservi\n");
	else
		ak_puts(", jamais donne avant : bon\n");
	korin_sys1(SYS_WAIT, fresh);
	_exit(0);
}
