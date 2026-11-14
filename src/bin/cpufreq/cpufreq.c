/* cpufreq - fréquence effective de chaque coeur (Korin PC : APERF/MPERF, mesurés chaque seconde
 * par le noyau ; docs/etude/PC-PILOTES.md, « Fréquence du processeur ») :
 *    cpufreq            une ligne par coeur : MHz effectifs de la dernière seconde, charge
 *    cpufreq charge     d'abord 2 s de calcul sur un coeur (le turbo d'un coeur), puis la même chose
 * « MHz effectifs » : la fréquence moyenne quand le coeur travaille (un coeur inactif garde celle
 * de ses derniers instants de travail). Le Pi n'a pas cet appel (CPU_LOAD : E_NOSYS) : code 1. */
#include "korinrt.h"

#define CPUS_MAX 64
#define CPU_LOAD_FREQ (1L << 32)

static u64 e[CPUS_MAX * 4];

int main(int argc, char **argv)
{
	if (argc > 1 && str_eq(argv[1], "charge")) {
		long end = korin_sys0(SYS_TIME_MS) + 2000;
		volatile u64 x = 1;
		while (korin_sys0(SYS_TIME_MS) < end)
			for (int i = 0; i < 1000000; i++)
				x = x * 6364136223846793005ULL + 1442695040888963407ULL;
	}
	long n = korin_sys2(SYS_CPU_LOAD, e, CPUS_MAX | CPU_LOAD_FREQ);
	if (n <= 0) {
		ak_puts("cpufreq : pas de mesure (CPU_LOAD absent)\n");
		_exit(1);
	}
	if (n > CPUS_MAX)
		n = CPUS_MAX;
	long top = 0;
	for (long c = 0; c < n; c++) {
		u64 *x = &e[c * 4];
		ak_puts("coeur ");
		print_dec(c);
		ak_puts(" : ");
		if (x[2]) {
			print_dec((long)x[2]);
			ak_puts(" MHz");
		} else
			ak_puts("? MHz");
		ak_puts(", charge ");
		print_dec((long)x[0]);
		ak_puts(" %\n");
		if ((long)x[2] > top)
			top = (long)x[2];
	}
	ak_puts("plus haute effective : ");
	if (top) {
		print_dec(top);
		ak_puts(" MHz");
	} else
		ak_puts("inconnue (pas d'APERF/MPERF)");
	ak_puts(" ; plus haute demandée : ");
	if (e[3]) {
		print_dec((long)e[3]);
		ak_puts(" MHz\n");
	} else
		ak_puts("inconnue (ni HWP ni PERF_CTL)\n");
	_exit(0);
}
