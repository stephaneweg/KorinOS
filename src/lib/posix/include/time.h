/*
 * time.h -- newlib's (its clocks visible through the _POSIX_* options of <sys/features.h>), plus
 * timegm (libposix time.c). CLOCK_MONOTONIC is the ARM counter since boot, CLOCK_REALTIME
 * the kernel's UTC time; the CPU-time clocks are the monotonic one.
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
#ifndef _POSIX_TIME_H
#define _POSIX_TIME_H

#include_next <time.h>

#ifdef __cplusplus
extern "C" {
#endif
time_t timegm (struct tm *);
/* timezone, daylight de POSIX (XSI) : newlib n'a que _timezone et _daylight ; meme symbole par
   une etiquette asm (une macro casserait struct timezone de sys/time.h) */
#if __XSI_VISIBLE || __SVID_VISIBLE
extern long timezone __asm__ ("_timezone");
extern int daylight __asm__ ("_daylight");
#endif
#ifdef __cplusplus
}
#endif

#endif /* _POSIX_TIME_H */
