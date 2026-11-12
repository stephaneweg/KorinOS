/*
 * fcntl.h -- newlib's, plus Linux's file seals (fcntl F_ADD_SEALS / F_GET_SEALS on a memfd_create or
 * shm_open descriptor: kapi v76, docs/POSIX-PLAN.md §14; WebKit's IPC::Semaphore uses them).
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
#ifndef _POSIX_FCNTL_H
#define _POSIX_FCNTL_H

#include_next <fcntl.h>

#ifndef F_ADD_SEALS
#define F_ADD_SEALS	1033		/* = Linux */
#define F_GET_SEALS	1034
#define F_SEAL_SEAL	0x0001
#define F_SEAL_SHRINK	0x0002
#define F_SEAL_GROW	0x0004
#define F_SEAL_WRITE	0x0008
#endif

/* file.c : posix_fallocate (agrandit par ftruncate), posix_fadvise (conseil ignore) */
#ifndef POSIX_FADV_NORMAL
#define POSIX_FADV_NORMAL	0		/* = Linux */
#define POSIX_FADV_RANDOM	1
#define POSIX_FADV_SEQUENTIAL	2
#define POSIX_FADV_WILLNEED	3
#define POSIX_FADV_DONTNEED	4
#define POSIX_FADV_NOREUSE	5
#endif
#ifdef __cplusplus
extern "C" {
#endif
int posix_fallocate (int __fd, off_t __off, off_t __len);
int posix_fadvise (int __fd, off_t __off, off_t __len, int __advice);
#ifdef __cplusplus
}
#endif

#endif /* _POSIX_FCNTL_H */
