/* limits.h : celui de newlib (et de GCC), plus les limites POSIX qu'il n'a pas (docs/POSIX.md).
   Pas de garde autour de #include_next : le syslimits.h de GCC revient ici par son propre
   #include_next, et doit atteindre celui de newlib. */
#include_next <limits.h>

#ifndef SSIZE_MAX
#define SSIZE_MAX __LONG_MAX__		/* ssize_t est un long (AArch64) */
#endif
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif
#ifndef _POSIX_ARG_MAX
#define _POSIX_ARG_MAX 4096		/* le minimum de POSIX pour ARG_MAX (LLVM) */
#endif
#ifndef CHARCLASS_NAME_MAX
#define CHARCLASS_NAME_MAX 14		/* le nom d'une classe [:alpha:] (regcomp, regex/) */
#endif
#ifndef RE_DUP_MAX
#define RE_DUP_MAX 255			/* {n,m} au plus (regcomp) */
#endif
