/*
 * sys/wait.h -- newlib's, plus job control (libposix proc.c, src/kernel/jobs.asm): WCONTINUED,
 * WIFCONTINUED, waitid with its idtype_t and CLD_* codes, wait3 and wait4. Statuses as Linux
 * lays them out: exit code << 8, signal number, (stop signal << 8) | 0x7f, 0xffff when continued.
 *
 * Copyright (c) 2026 Stéphane Wegener. MIT licence: Permission is hereby granted, free of
 * charge, to any person obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so, subject to the
 * following conditions: The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#ifndef _POSIX_SYS_WAIT_H
#define _POSIX_SYS_WAIT_H

#include_next <sys/wait.h>
#include <signal.h>

#ifndef WCONTINUED
#define WCONTINUED	8
#endif
#ifndef WSTOPPED
#define WSTOPPED	2			/* (waitid : comme WUNTRACED) */
#endif
#ifndef WEXITED
#define WEXITED		4
#endif
#ifndef WNOWAIT
#define WNOWAIT		0x1000000
#endif
#ifndef WIFCONTINUED
#define WIFCONTINUED(w)	((w) == 0xffff)
#endif
#ifndef WCOREDUMP
#define WCOREDUMP(w)	((w) & 0x80)
#endif

#ifndef CLD_EXITED
#define CLD_EXITED	1
#define CLD_KILLED	2
#define CLD_DUMPED	3
#define CLD_TRAPPED	4
#define CLD_STOPPED	5
#define CLD_CONTINUED	6
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { P_ALL, P_PID, P_PGID } idtype_t;

struct rusage;
pid_t wait3 (int *, int, struct rusage *);
pid_t wait4 (pid_t, int *, int, struct rusage *);
int waitid (idtype_t, id_t, siginfo_t *, int);

#ifdef __cplusplus
}
#endif

#endif /* _POSIX_SYS_WAIT_H */
