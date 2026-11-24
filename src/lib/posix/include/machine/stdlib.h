/*
 * machine/stdlib.h -- newlib's (an empty place holder), plus alloca under _GNU_SOURCE in strict mode.
 *
 * newlib's stdlib.h declares alloca only #ifndef __STRICT_ANSI__ (so not with -std=c11 or -std=c++17) ; glibc
 * declares it as soon as _GNU_SOURCE or _DEFAULT_SOURCE is set (OpenColorIO, Blender expect it). Done here and not
 * in libposix's stdlib.h: in C++, <cstdlib> reaches newlib's stdlib.h by #include_next, past libposix's directory,
 * but newlib's stdlib.h includes <machine/stdlib.h> from the start of the search path, so this file is always seen.
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
#ifndef _POSIX_MACHINE_STDLIB_H
#define _POSIX_MACHINE_STDLIB_H

#include_next <machine/stdlib.h>

#if defined(__STRICT_ANSI__) && (defined(_GNU_SOURCE) || defined(_DEFAULT_SOURCE) || defined(_BSD_SOURCE))
#include <alloca.h>
#endif

#endif /* _POSIX_MACHINE_STDLIB_H */
