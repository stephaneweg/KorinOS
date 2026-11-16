/* gdrivefs sous Linux (src/svc/gdrivefs/test/linux.c) : SVC_MAP_CALLER rend l'adresse elle-même (même process),
 * BUF_CREATE le tampon 0 (BUF_VA_BASE de l'essai : un tableau du process). */
#ifndef ARMPI_SYSCALL_TEST_H
#define ARMPI_SYSCALL_TEST_H
static inline long armpi_syscall(long n, long a, long b, long c, long d, long e, long f)
{
	(void)a; (void)c; (void)d; (void)e; (void)f;
	if (n == 0x36) /* SYS_SVC_MAP_CALLER (requête, adresse, taille, drapeaux) */
		return b;
	if (n == 0x01 || n == 0x02 || n == 0x04) /* BUF_CREATE, BUF_GRANT, BUF_FREE */
		return 0;
	return -38;
}
#endif
