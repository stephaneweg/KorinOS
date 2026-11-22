/* armpi/syscall.h -- sur Linux, pas d'appel systeme de Korin : le coffre a secrets (src/lib/keyvault/keyvault.h)
 * repond « pas de service » (-ENOSYS), et Koton garde ses cles d'API dans HOME:/koton/settings.json. */
#ifndef KPORT_ARMPI_SYSCALL_H
#define KPORT_ARMPI_SYSCALL_H
static inline long armpi_syscall (long n, long a, long b, long c, long d, long e, long f)
{
	(void) n; (void) a; (void) b; (void) c; (void) d; (void) e; (void) f;
	return -38;
}
#endif
