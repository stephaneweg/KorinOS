/* armpi/syscall.h -- sur Linux, les appels systeme de Korin que les apps font elles-memes passent par kport_syscall
 * (korin_sys.cpp) : l'horloge (SYS_TIME_MS, SYS_CLOCK_GET) et le coffre a secrets (src/lib/keyvault/keyvault.h :
 * KV_SVC_OPEN, KV_SVC_CALL), qui n'existe que pour l'app qui l'installe (Budget : korin_vault.cpp, libsecret) ; pour
 * les autres, il repond « pas de service » (-ENOSYS), et Koton garde ses cles d'API dans HOME:/koton/settings.json. */
#ifndef KPORT_ARMPI_SYSCALL_H
#define KPORT_ARMPI_SYSCALL_H
#ifdef __cplusplus
extern "C"
#endif
long kport_syscall (long n, long a, long b, long c, long d, long e, long f);
static inline long armpi_syscall (long n, long a, long b, long c, long d, long e, long f)
{
	return kport_syscall (n, a, b, c, d, e, f);
}
#endif
