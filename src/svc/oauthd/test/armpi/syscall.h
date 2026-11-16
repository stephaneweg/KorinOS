/* oauthd sous Linux (src/svc/oauthd/test/linux.c) : SVC_MAP_CALLER rend l'adresse elle-même (même process). */
#ifndef ARMPI_SYSCALL_TEST_H
#define ARMPI_SYSCALL_TEST_H
static inline long armpi_syscall(long n, long a, long b, long c, long d, long e, long f)
{
	(void)a; (void)c; (void)d; (void)e; (void)f;
	if (n == 0x36) /* SYS_SVC_MAP_CALLER (requête, adresse, taille, drapeaux) */
		return b;
	return -38;
}
#endif
