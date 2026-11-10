/*
 * setjmp.h -- newlib's, plus sigsetjmp / siglongjmp (libposix, docs/POSIX.md) : newlib ne les
 * donne qu'a Cygwin et RTEMS. Memes macros que les leurs : le masque des signaux est garde apres
 * le jmp_buf quand savemask n'est pas nul (sigprocmask), et remis par siglongjmp.
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
#ifndef _POSIX_SETJMP_H
#define _POSIX_SETJMP_H

#include_next <setjmp.h>
#include <signal.h>

#ifndef _SAVEMASK
typedef _JBTYPE sigjmp_buf[_JBLEN + 1 + ((sizeof (_JBTYPE) + sizeof (sigset_t) - 1) / sizeof (_JBTYPE))];

#define _SAVEMASK	_JBLEN
#define _SIGMASK	(_JBLEN + 1)

#define sigsetjmp(env, savemask) \
	__extension__ ({ \
		sigjmp_buf *__sjb = &(env); \
		((*__sjb)[_SAVEMASK] = (savemask), \
		 sigprocmask (SIG_SETMASK, 0, (sigset_t *) ((*__sjb) + _SIGMASK)), \
		 setjmp (*__sjb)); \
	})

#define siglongjmp(env, val) \
	__extension__ ({ \
		sigjmp_buf *__sjb = &(env); \
		((((*__sjb)[_SAVEMASK]) ? sigprocmask (SIG_SETMASK, (sigset_t *) ((*__sjb) + _SIGMASK), 0) : 0), \
		 longjmp (*__sjb, (val))); \
	})

#ifndef _setjmp
#define _setjmp(env)		sigsetjmp ((env), 0)
#define _longjmp(env, val)	siglongjmp ((env), (val))
#endif
#endif

#endif
