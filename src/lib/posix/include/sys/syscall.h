/*
 * sys/syscall.h -- syscall () de Linux arm64 pour le code porte : les seuls numeros servis par
 * libposix (misc.c), le reste rend ENOSYS. Les appels d'ARMPI : <armpi/...>, jamais ceux-ci.
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
#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#define __NR_mbind	235
#define __NR_get_mempolicy	236
#define __NR_gettid	178
#define __NR_getrandom	278
#define __NR_membarrier	283
#define SYS_gettid	__NR_gettid
#define SYS_getrandom	__NR_getrandom
#define SYS_membarrier	__NR_membarrier

#ifdef __cplusplus
extern "C" {
#endif

long syscall (long, ...);

#ifdef __cplusplus
}
#endif

#endif
