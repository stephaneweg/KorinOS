/* sysstat - les appels système des programmes (docs/OUTILS.md, d'après sysstat.c d'Onyx), comme
 * sysstat.asm : combien chacun en fait, et lesquels. Les appels de fichiers (0x80-0x8E) passent
 * par AppKit et vfs : ils comptent ici en SVC_CALL. Compteurs du noyau : SYS_PROC_STATS.
 *
 *   sysstat                 chaque process : pid, appels par seconde (mesurés sur une seconde),
 *                           en tout
 *   sysstat <pid | nom>     un process : la même ligne, puis ses 8 appels les plus fréquents
 *
 * Comme l'asm : au plus 256 process vus (noms de 31 octets) ; un argument fait que de chiffres
 * est un pid, sinon un nom (le premier process de ce nom) ; les autres arguments sont ignorés ;
 * un appel sans nom connu s'écrit « 0x » et deux chiffres hexadécimaux. */
#include "korinrt.h"

#define PMAX 256
#define PS_PLACES 129 /* PROC_STATS : le total et 128 numéros */
#define TOP 8

struct proc {
	u64 pid;
	char name[56];
};

static u64 one[1];
static u64 t0[PMAX];
static u64 stats[PS_PLACES];
static u8 lot[16 * 64];
static struct proc procs[PMAX];
static long nprocs;

/* les noms des appels (src/inc/syscalls.inc : un nom absent ne compile pas) */
#define N(x) { SYS_##x, #x }
static const struct {
	u16 nr;
	const char *name;
} names[] = {
	N(SBRK), N(BUF_CREATE), N(BUF_GRANT), N(BUF_MAP), N(BUF_FREE), N(DMA_ALLOC), N(VM_MAP),
	N(VM_UNMAP), N(VM_PROTECT), N(VM_ADVISE), N(VM_QUERY), N(VM_STATS), N(VM_PAGEINFO),
	N(BUF_INFO), N(EXIT), N(GETPID), N(SPAWN_IMAGE), N(WAIT), N(KILL), N(PROCLIST), N(PROC_FLAGS),
	N(SLEEP), N(YIELD), N(TIME_MS), N(CLOCK_GET), N(CLOCK_SET), N(SPAWN_PRELOADED),
	N(PROC_RIGHTS), N(PROC_INFO), N(PROCLIST_FROM), N(THREAD_CREATE), N(THREAD_EXIT),
	N(FUTEX_WAIT), N(FUTEX_WAKE), N(THREAD_CREATE_EX), N(THREAD_JOIN), N(THREAD_SELF),
	N(THREAD_SET_PRIORITY), N(THREAD_SET_NAME), N(THREAD_KILL), N(THREAD_LIST), N(SIG_ACTION),
	N(SIG_SEND), N(SIG_RETURN), N(SIG_MASK), N(SIG_ALTSTACK), N(SVC_REGISTER), N(SVC_OPEN),
	N(SVC_CALL), N(SVC_NEXT), N(SVC_REPLY), N(SVC_MAP_CALLER), N(SVC_UNMAP_CALLER),
	N(SVC_CALLER_PHYS), N(SVC_SIGNAL), N(SVC_FORWARD), N(DEV_MAP), N(FW_CALL), N(MOD_LAUNCH),
	N(MOD_READ), N(BOOT_READY), N(BOOT_FILE), N(KIT_REGISTER), N(IMAGE_PRELOAD),
	N(IMAGE_FORGET), N(IRQ_BIND), N(IRQ_ACK), N(IRQ_WAIT), N(IRQ_UNBIND), N(WS_REGISTER),
	N(WS_CALL), N(WS_NEXT), N(WS_REPLY), N(WS_POST), N(WS_DISPLAY), N(EV_WAIT), N(SCREEN_INFO),
	N(CLIP_SET), N(CLIP_GET), N(WS_SIGNAL), N(WS_MODE), N(PRINT), N(KLOG), N(SYSINFO),
	N(GETRANDOM), N(LIB_OPEN), N(IMAGE_LIST), N(IMAGE_CREATE), N(IMAGE_SEAL), N(MEMBARRIER),
	N(PROC_STATS), N(HANDLE_CLOSE), N(HANDLE_INFO), N(HANDLE_WAIT), N(HANDLE_LIST),
	N(HANDLE_INHERIT), N(CHAN_CREATE), N(CHAN_SEND), N(CHAN_RECV), N(CHAN_SHUTDOWN),
	N(SHM_CREATE), N(SHM_OPEN), N(SHM_UNLINK), N(SHM_CTL), N(SHM_MAP),
};

/* -> nprocs, procs (collect) */
static void collect(void)
{
	long from = 0; /* emplacement de départ */
	nprocs = 0;
	for (;;) {
		long n = korin_sys3(SYS_PROCLIST_FROM, lot, 16, from);
		if (n <= 0)
			return;
		u8 *e = lot;
		for (; n; n--, e += 64) {
			u64 pid = *(u64 *)e;
			from = (long)(pid & 0xFFFF);
			if (nprocs >= PMAX)
				return;
			struct proc *p = &procs[nprocs];
			p->pid = pid;
			const char *s = (const char *)e + 24;
			char *d = p->name;
			for (long k = 31;;) {
				char c = *s++;
				*d++ = c;
				if (!c)
					break;
				if (!--k) {
					*d = 0;
					break;
				}
			}
			nprocs++;
		}
		if (e != lot + 16 * 64) /* (paquet plein : il y a peut-être une suite) */
			return;
	}
}

/* indice -> appels faits (< 0 : parti) */
static long total(long i)
{
	long r = korin_sys3(SYS_PROC_STATS, procs[i].pid, one, 1);
	if (r < 0)
		return r;
	return (long)one[0];
}

static void header(void)
{
	out_say("    PID   APPELS/s  APPELS EN TOUT  NOM\n");
}

/* indice, appels pendant la seconde, en tout */
static void line(long i, u64 sec, u64 all)
{
	out_putw(procs[i].pid, 7);
	out_putw(sec, 11);
	out_putw(all, 14);
	out_putc(' ');
	out_putc(' ');
	out_puts(procs[i].name);
	out_nl();
}

/* numéro d'appel : son nom en minuscules (« svc_call »), ou 0x.. */
static void put_name(u64 nr)
{
	for (u64 i = 0; i < sizeof names / sizeof names[0]; i++)
		if (names[i].nr == nr) {
			for (const char *s = names[i].name; *s; s++)
				out_putc((u8)(*s - 'A') <= 25 ? *s + 32 : *s);
			return;
		}
	static const char hex[] = "0123456789abcdef";
	out_putc('0');
	out_putc('x');
	out_putc(hex[(nr >> 4) & 15]);
	out_putc(hex[nr & 15]);
}

/* texte -> pid s'il n'a que des chiffres, 0 sinon */
static u64 num_pid(const char *s)
{
	u64 v = 0;
	if (!*s)
		return 0;
	for (; *s; s++) {
		unsigned d = (u8)*s - '0';
		if (d > 9)
			return 0;
		v = v * 10 + d;
	}
	return v;
}

static NORETURN void none(const char *arg)
{
	err_puts(krt_argv[0]);
	err_puts(": ");
	err_say("pas de process ");
	err_puts(arg);
	err_puts("\n");
	exit(1);
}

int main(int argc, char **argv)
{
	if (korin_sys3(SYS_PROC_STATS, 0, stats, 1) < 0) { /* ce noyau compte-t-il ? */
		err_msg("ce noyau ne compte pas les appels (SYS_PROC_STATS)");
		exit(1);
	}
	const char *want = argc >= 2 ? argv[1] : 0; /* le pid ou le nom voulu (0 : tous) */
	collect();
	if (!want) {
		for (long i = 0; i < nprocs; i++) /* premiers totaux */
			t0[i] = (u64)total(i);
		sys_sleep(1000);
		header();
		for (long i = 0; i < nprocs; i++) {
			long t = total(i);
			if (t < 0) /* (parti entre-temps) */
				continue;
			line(i, (u64)t - t0[i], (u64)t);
		}
		exit(0);
	}
	u64 pid = num_pid(want); /* un seul : par pid, sinon par nom */
	long i;
	for (i = 0; i < nprocs; i++)
		if (pid ? procs[i].pid == pid : str_eq(procs[i].name, want) != 0)
			break;
	if (i == nprocs)
		none(want);
	long before = total(i);
	sys_sleep(1000);
	long words = korin_sys3(SYS_PROC_STATS, procs[i].pid, stats, PS_PLACES); /* le tableau entier */
	if (words < 0)
		none(want);
	header();
	line(i, stats[0] - (u64)before, stats[0]);
	out_say("\n  les appels les plus fréquents :\n");
	u64 all = stats[0]; /* total, pour les % */
	for (int rank = 0; rank < TOP; rank++) {
		u64 best = 0, val = 0; /* le plus grand qui reste */
		for (u64 k = 1; k < (u64)words; k++)
			if (stats[k] > val) {
				val = stats[k];
				best = k;
			}
		if (!val)
			break;
		stats[best] = 0;
		out_putw(val, 13);
		out_putc(' ');
		out_putc(' ');
		out_putw(all ? val * 100 / all : 0, 3); /* (udiv par 0 : 0, comme l'asm) */
		out_puts(" %  ");
		put_name(best - 1);
		out_nl();
	}
	exit(0);
}
