/* maptest - essai de SVC_MAP_CALLER (src/kernel/mapcaller.asm), avec le service
 * /sys/svc/mapsvc : lecture, écriture dans une région pas encore touchée, page en lecture
 * seule, refus ; munmap et mort de l'appelant pendant que le service tient les pages : rien ne
 * plante, aucune page ne fuit (comme maptest.asm).
 * Sortie : une ligne par échec, puis « maptest : PASS / FAIL (n) » ; codes 0, 1, 2 (service absent).
 * « maptest fils » (ligne du noyau commençant par « f ») : le fils, tenu par le service, qui se fait
 * tuer. Comme l'asm : l'essai « code lu » lit 256 octets du code du programme (ici : main). */
#include "korinrt.h"

#define OP_READ 1
#define OP_WRITE 2
#define OP_HOLD 3
#define OP_PAGES 4
#define E_FAULT_T -14
#define E_INVAL_T -22
#define ZONE 65536
#define ROUNDS 10
#define HOLD_MS 200
#define TH_STACK 16384
#define LEAK_MAX_KB 300

static long channel;
static long failures;
static long zone;
static u64 info[10];
static u8 buf[12288];
static u8 th_stack[TH_STACK] __attribute__((aligned(16)));

/* op, a, b, c -> résultat (message de 512 o sur la pile) */
static long call(long op, long a, long b, long c)
{
	long m[64];
	m[0] = op;
	m[1] = a;
	m[2] = b;
	m[3] = c;
	return korin_sys2(SYS_SVC_CALL, channel, m);
}

/* mémoire libre (Ko) */
static long free_kb(void)
{
	korin_sys1(SYS_SYSINFO, info);
	return info[1];
}

/* Ko perdus -> 0 si c'est peu */
static long leak(long kb)
{
	return kb > LEAK_MAX_KB ? kb : 0;
}

static void check(long got, long want, const char *name)
{
	if (got == want)
		return;
	failures++;
	ak_puts("maptest : ECHEC ");
	ak_puts(name);
	ak_puts(" : ");
	print_dec(got);
	ak_puts(" au lieu de ");
	print_dec(want);
	ak_puts("\n");
}

/* le fils : tenu par le service, il se fait tuer */
NORETURN static void child(void)
{
	channel = korin_sys2(SYS_SVC_OPEN, "mapsvc", 2000);
	long z = korin_sys4(SYS_VM_MAP, 0, ZONE, PROT_READ | PROT_WRITE, 0);
	call(OP_HOLD, z, ZONE, HOLD_MS);
	_exit(0);
}

/* thread : tient la zone */
static void th_call(void)
{
	call(OP_HOLD, zone, ZONE, HOLD_MS);
	korin_sys1(SYS_THREAD_EXIT, 0);
	__builtin_unreachable();
}

NORETURN static void ko(long e)
{
	ak_puts("maptest : erreur ");
	print_dec(e);
	ak_puts("\n");
	_exit(2);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (krt_line && krt_line[0] == 'f')
		child();
	long r = ksys(SYS_SPAWN, (long)"/sys/svc/mapsvc", 0, 0, 0, 0, 0); /* le service */
	if (r < 0)
		ko(r);
	r = korin_sys2(SYS_SVC_OPEN, "mapsvc", 2000);
	if (r < 0)
		ko(r);
	channel = r;

	/* lecture, à cheval sur deux pages */
	volatile u8 *p = buf + 3000;
	long sum = 0;
	for (long i = 0; i < 6000; i++) {
		p[i] = (u8)i;
		sum += i & 255;
	}
	check(call(OP_READ, (long)p, 6000, 0), sum, "lecture");

	/* écriture dans une région jamais touchée */
	volatile u8 *z = (volatile u8 *)korin_sys4(SYS_VM_MAP, 0, ZONE, PROT_READ | PROT_WRITE, 0);
	check(call(OP_WRITE, (long)z, ZONE, 0x33), ZONE, "ecriture");
	check(z[ZONE - 1], 0x33, "ecriture relue");

	/* le code : se lit, ne s'écrit pas */
	check(call(OP_READ, (long)main, 256, 0) > 0, 1, "code lu");
	check(call(OP_WRITE, (long)main, 256, 1), E_FAULT_T, "code en ecriture");

	/* refus */
	check(call(OP_READ, 0x6F00000000L, 4096, 0), E_FAULT_T, "adresse sans page"); /* rien ici */
	check(call(OP_READ, (long)z, 0x200000, 0), E_INVAL_T, "trop grand");

	/* pages : 4097 octets depuis le milieu d'une page, 2 pages */
	check(call(OP_PAGES, (long)z + 2048, 4097, 0), 2, "pages");

	/* le service mappe l'appelant, l'appelant part : la page attend le service */
	long kb0 = free_kb();
	for (int t = ROUNDS; t; t--) {
		long zz = korin_sys4(SYS_VM_MAP, 0, ZONE, PROT_READ | PROT_WRITE, 0);
		zone = zz;
		long tid = korin_sys3(SYS_THREAD_CREATE, th_call, th_stack + TH_STACK, 0);
		korin_sys1(SYS_SLEEP, 50);
		check(korin_sys2(SYS_VM_UNMAP, zz, ZONE), 0, "munmap pendant le mappage"); /* tenue par le service */
		korin_sys1(SYS_THREAD_JOIN, tid);
	}
	check(leak(kb0 - free_kb()), 0, "fuite apres munmap");

	for (int t = ROUNDS; t; t--) {
		long pid = ksys(SYS_SPAWN, (long)"/bin/maptest", (long)"fils", 0, 0, 0, 0);
		if (pid < 0)
			ko(pid);
		korin_sys1(SYS_SLEEP, 50);
		korin_sys1(SYS_KILL, pid);
		korin_sys1(SYS_WAIT, pid);
		korin_sys1(SYS_SLEEP, HOLD_MS);
	}
	korin_sys1(SYS_SLEEP, 100);
	check(leak(kb0 - free_kb()), 0, "fuite apres mort");

	/* le service répond encore */
	check(call(OP_READ, (long)buf, 16, 0) >= 0, 1, "service vivant");

	if (failures) {
		ak_puts("maptest : FAIL (");
		print_dec(failures);
		ak_puts(")\n");
		_exit(1);
	}
	ak_puts("maptest : PASS\n");
	_exit(0);
}
