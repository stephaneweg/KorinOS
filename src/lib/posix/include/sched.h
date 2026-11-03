/*
 * sched.h -- newlib's, plus cpu_set_t and the affinity calls (libposix misc.c). ARMPI's scheduler
 * places threads itself: an affinity reads as every core, and setting one has no effect.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#ifndef _POSIX_SCHED_H
#define _POSIX_SCHED_H

#include_next <sched.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CPU_SETSIZE	1024

typedef struct
{
	unsigned long __bits[CPU_SETSIZE / (8 * sizeof (unsigned long))];
} cpu_set_t;

#define __CPU_WORD(c)	((c) / (8 * sizeof (unsigned long)))
#define __CPU_BIT(c)	(1UL << ((c) % (8 * sizeof (unsigned long))))
#define CPU_ZERO(s)	__builtin_memset ((s), 0, sizeof (cpu_set_t))
#define CPU_SET(c, s)	((void) ((s)->__bits[__CPU_WORD (c)] |= __CPU_BIT (c)))
#define CPU_CLR(c, s)	((void) ((s)->__bits[__CPU_WORD (c)] &= ~__CPU_BIT (c)))
#define CPU_ISSET(c, s)	(((s)->__bits[__CPU_WORD (c)] & __CPU_BIT (c)) != 0)
int __posix_cpu_count (const cpu_set_t *);
#define CPU_COUNT(s)	__posix_cpu_count (s)

int sched_getaffinity (pid_t, size_t, cpu_set_t *);
int sched_setaffinity (pid_t, size_t, const cpu_set_t *);
int pthread_getaffinity_np (pthread_t, size_t, cpu_set_t *);
int pthread_setaffinity_np (pthread_t, size_t, const cpu_set_t *);

#ifdef __cplusplus
}
#endif

#endif
