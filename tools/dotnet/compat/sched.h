/* sched.h -- celui de libposix, plus les ensembles de coeurs de taille variable de glibc */
#ifndef _ARMPI_COMPAT_SCHED_H
#define _ARMPI_COMPAT_SCHED_H

#include_next <sched.h>
#include <stdlib.h>
#include <string.h>

#define CPU_ALLOC_SIZE(n)	sizeof (cpu_set_t)
#define CPU_ALLOC(n)		((cpu_set_t *) calloc (1, sizeof (cpu_set_t)))
#define CPU_FREE(s)		free (s)
#define CPU_ZERO_S(n, s)	memset ((s), 0, (n))
#define CPU_SET_S(c, n, s)	CPU_SET ((c), (s))
#define CPU_ISSET_S(c, n, s)	CPU_ISSET ((c), (s))
#define CPU_COUNT_S(n, s)	CPU_COUNT (s)

#endif
