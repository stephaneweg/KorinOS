/*
 * sys/features.h -- la surcouche de libposix sur le <sys/features.h> de newlib (docs/POSIX.md ;
 * reprise d'Onyx).
 *
 * newlib prend ses macros d'options POSIX (_POSIX_THREADS, _POSIX_TIMERS,
 * _POSIX_MONOTONIC_CLOCK...) dans cet en-tete et ne les definit que pour quelques cibles
 * hebergees : libposix les implemente, donc cette surcouche (premiere sur le chemin : -isystem
 * build/posix/include) tire l'en-tete de newlib (renomme _newlib_features.h par la chaine
 * aarch64-onyx-elf), puis dit ce qu'ARMPI offre. La surcouche de la chaine elle-meme (qui
 * definirait __onyx__) est ecartee par sa garde.
 *
 * ---------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ---------------------------------------------------------------------------------------------
 */
#ifndef _POSIX_SYS_FEATURES_H
#define _POSIX_SYS_FEATURES_H
#ifndef _ONYX_TC_SYS_FEATURES_H
#define _ONYX_TC_SYS_FEATURES_H			/* (celle de la chaine : pas __onyx__) */
#endif

#include <sys/_newlib_features.h>

/* La cible : un environnement POSIX statique sur ARMPI (appels systeme, AppKit, NetKit). */
#ifndef __armpi__
#define __armpi__		1
#endif
#define __ARMPI_POSIX__		1		/* les en-tetes de libposix sont utilises */

#ifndef _POSIX_VERSION
#define _POSIX_VERSION			200809L
#endif
#define _POSIX_THREADS			200809L
#define _POSIX_THREAD_ATTR_STACKSIZE	200809L
#define _POSIX_THREAD_ATTR_STACKADDR	200809L
#define _POSIX_THREAD_SAFE_FUNCTIONS	200809L
#define _POSIX_THREAD_PRIORITY_SCHEDULING 200809L
#define _POSIX_THREAD_CPUTIME		200809L
#define _POSIX_TIMEOUTS			200809L
#define _POSIX_READER_WRITER_LOCKS	200809L
#define _POSIX_SPIN_LOCKS		200809L
#define _POSIX_BARRIERS			200809L
#define _POSIX_SEMAPHORES		200809L
#define _POSIX_TIMERS			200809L	/* clock_gettime, nanosleep (timer_create: ENOSYS) */
#define _POSIX_MONOTONIC_CLOCK		200809L
#define _POSIX_CLOCK_SELECTION		200809L
#define _POSIX_CPUTIME			200809L
#define _POSIX_PRIORITY_SCHEDULING	200809L	/* sched_* (one policy: the kernel's) */
#define _POSIX_SPAWN			200809L
#define _POSIX_MAPPED_FILES		200809L	/* (copies en memoire : mman.c) */
#define _POSIX_FSYNC			200809L
#define _POSIX_NO_TRUNC			1
#define _UNIX98_THREAD_MUTEX_ATTRIBUTES	1

#endif /* _POSIX_SYS_FEATURES_H */
