//
// korin_sys.cpp -- les quelques appels systeme de Korin que les apps font directement (armpi_syscall,
// common/include/armpi/syscall.h), rendus par Linux :
//
//   0x19 SYS_TIME_MS        ms depuis le demarrage (CLOCK_MONOTONIC)
//   0x1B SYS_CLOCK_GET      a = 0 : secondes UTC ; 1 : decalage de l'heure locale (s) ; 2 : secondes locales
//                           (le fuseau de Linux, heure d'ete comprise)
//   0x31, 0x32              le coffre a secrets (KV_SVC_OPEN, KV_SVC_CALL de src/lib/keyvault/keyvault.h) : a celui que
//                           l'app installe (kport_vault_handler, korin_vault.cpp), sinon -ENOSYS comme avant
//
// Le reste : -ENOSYS (-38).
//
#include "korin_port.h"
#include <time.h>

extern "C" {

long (*kport_vault_handler) (long n, long a, long b);

long kport_syscall (long n, long a, long b, long c, long d, long e, long f)
{
	(void) c; (void) d; (void) e; (void) f;
	switch (n)
	{
	case 0x19:
	{
		struct timespec t;
		clock_gettime (CLOCK_MONOTONIC, &t);
		return (long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
	}
	case 0x1B:
	{
		time_t now = time (0);
		struct tm lt;
		localtime_r (&now, &lt);
		if (a == 0) return (long) now;
		if (a == 1) return lt.tm_gmtoff;
		if (a == 2) return (long) now + lt.tm_gmtoff;
		return 0;
	}
	case 0x31: case 0x32:
		return kport_vault_handler ? kport_vault_handler (n, a, b) : -38;
	}
	return -38;
}

}
