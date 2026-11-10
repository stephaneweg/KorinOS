/*
 * stdlib.h -- newlib's, plus the pseudo-terminal calls of libposix (tty.c, docs/VTERM.md):
 * posix_openpt, grantpt, unlockpt, ptsname, ptsname_r.
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
#ifndef _POSIX_STDLIB_H
#define _POSIX_STDLIB_H

#include_next <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
int posix_openpt (int __flags);
int grantpt (int __fd);
int unlockpt (int __fd);
char *ptsname (int __fd);
int ptsname_r (int __fd, char *__buf, size_t __len);
#ifdef __cplusplus
}
#endif

#endif /* _POSIX_STDLIB_H */
