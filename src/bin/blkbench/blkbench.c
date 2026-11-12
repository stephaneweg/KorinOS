/* blkbench - débit du service bloc de la carte, sans système de fichiers
 * (diagnostic, lecture seule), comme blkbench.asm :
 *	blkbench [périphérique]	(sd0 par défaut, sd0p1 : celui de fatfs)
 * affiche les threads du service et leur cœur, puis lit blk.<périphérique> à
 * partir du secteur 200 000 par commandes de 1 à 256 secteurs, le pilote
 * dormant jusqu'à la fin du DMA, puis la sondant (attente active de 8 000 us,
 * laissée ainsi à la fin, comme l'asm) ; affiche ms, Ko/s et le temps par
 * commande dans le pilote (préparation, commande, données, sommeils), puis la
 * charge des cœurs. Une erreur : « blkbench : erreur » et le résultat, code 1.
 * Le nom est la ligne brute du noyau (krt_line), recopié derrière « blk. »
 * sans limite dans l'asm (20 octets de place) ; ici 500. */
#include "korinrt.h"

#define PROBE 8000   /* attente active de la seconde passe (us) */
#define FIRST 200000 /* premier secteur lu */
#define BUFSZ (256 * 512)
#define BLK_INFO 1
#define BLK_ATTACH 2
#define BLK_READ 3
#define BLK_STATS 7
/* src/inc/armpi.inc (adresses du Pi) */
#define BUF_LOW (1ull << 62)
#ifndef BUF_VA_BASE /* (aussi dans korinrt.h) */
#define BUF_VA_BASE 0x6800000000ull
#define BUF_SLOT 0x4000000ull
#endif

static long chan;
static u64 msg[64];
static u8 thl[16 * 64] __attribute__((aligned(8)));
static u64 info[10];
static char blk_name[512] = "blk.sd0";

static NORETURN void fail_err(long e)
{
	ak_puts("blkbench : erreur ");
	print_dec(e);
	ak_puts("\n");
	_exit(1);
}

/* attente active en us (-1 : inchangée) -> sommeils, compteurs dans msg */
static long stats(long wait)
{
	msg[0] = BLK_STATS;
	msg[1] = (u64)wait;
	return korin_sys2(SYS_SVC_CALL, chan, msg);
}

/* pid : ses threads et leur cœur (coeurs) */
static void cores(long pid)
{
	long n = korin_sys3(SYS_THREAD_LIST, pid, thl, 16);
	for (long i = 0; i < n; i++) {
		u8 *e = thl + i * 64;
		ak_puts((const char *)e + 40);
		ak_puts(" : coeur ");
		print_dec((long)*(u64 *)(e + 24));
		ak_puts("\n");
	}
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

/* us au total -> les us par commande (par_cmd) */
static void per_cmd(u64 us)
{
	u64 n = msg[1] ? msg[1] : 1;
	print_dec((long)(us / n));
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	/* secteurs par commande, secteurs lus */
	static const u32 sizes[] = {1, 2048, 8, 4096, 16, 4096, 32, 8192, 64, 16384, 256, 16384, 0};
	if (krt_line && *krt_line) {
		char *d = blk_name + 4;
		const char *s = krt_line;
		while ((*d++ = *s++))
			;
	}
	long r = korin_sys2(SYS_SVC_OPEN, blk_name, 1000);
	if (r < 0)
		fail_err(r);
	chan = r;
	msg[0] = BLK_INFO;
	r = korin_sys2(SYS_SVC_CALL, chan, msg);
	if (r < 0)
		fail_err(r);
	long pid = (long)msg[4]; /* pid du service */
	cores(pid);
	r = korin_sys1(SYS_BUF_CREATE, BUFSZ | BUF_LOW);
	if (r < 0)
		fail_err(r);
	long buf = r;
	korin_sys2(SYS_BUF_GRANT, buf, pid);
	msg[0] = BLK_ATTACH;
	msg[1] = (u64)buf;
	msg[2] = BUFSZ;
	r = korin_sys2(SYS_SVC_CALL, chan, msg);
	if (r < 0)
		fail_err(r);
	u64 addr = BUF_VA_BASE + (u64)buf * BUF_SLOT; /* le tampon */
	for (long wait = 0;; wait = PROBE) {
		ak_puts(wait ? "-- le pilote sonde la fin du DMA\n" : "-- le pilote dort jusqu'a la fin du DMA\n");
		for (int s = 0; sizes[s]; s += 2) {
			u64 per = sizes[s], total = sizes[s + 1], left = total;
			stats(wait);
			long t = sys_time_ms();
			u64 sector = FIRST;
			int more;
			do {
				msg[0] = BLK_READ;
				msg[1] = sector;
				msg[2] = per;
				msg[3] = addr;
				r = korin_sys2(SYS_SVC_CALL, chan, msg);
				if (r < 0)
					fail_err(r);
				sector += per;
				more = left > per; /* (subs, b.hi) */
				left -= per;
			} while (more);
			t = sys_time_ms() - t;
			print_dec((long)per);
			ak_puts(" secteurs par commande : ");
			print_dec(t);
			ak_puts(" ms, ");
			print_dec((long)(500 * total / (u64)(t + 1)));
			ak_puts(" Ko/s\n");
			long sleeps = stats(-1);
			ak_puts("  ");
			print_dec((long)msg[1]);
			ak_puts(" commandes, en us par commande : preparation ");
			per_cmd(msg[2]);
			ak_puts(", reponse ");
			per_cmd(msg[3]);
			ak_puts(", donnees ");
			per_cmd(msg[4]);
			ak_puts(" ; sommeils ");
			print_dec(sleeps);
			ak_puts("\n");
		}
		loads();
		if (wait)
			break;
	}
	_exit(0);
}
