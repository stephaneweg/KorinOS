/*
 * sys/types.h -- newlib's, plus off64_t (glibc gives it under _GNU_SOURCE or _LARGEFILE64_SOURCE ; Blender's
 * file readers use it). off_t is already 64-bit on Korin: off64_t is newlib's _off64_t.
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
/* No include guard of our own: newlib's sys/types.h has a partial mode (__need_inttypes), included again later
 * in full ; the additions are guarded on their own. */
#ifdef __need_inttypes
#include_next <sys/types.h>
#else
#include_next <sys/types.h>
#if (defined(_GNU_SOURCE) || defined(_LARGEFILE64_SOURCE)) && !defined(_POSIX_OFF64_T_DEFINED)
#define _POSIX_OFF64_T_DEFINED
typedef _off64_t off64_t;
#endif
#endif
