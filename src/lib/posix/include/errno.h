/*
 * errno.h -- celui de newlib, plus les codes que newlib reserve a __LINUX_ERRNO_EXTENSIONS__ et
 * dont les ports ont besoin (memes numeros que newlib) : ESHUTDOWN (le module errno de CPython,
 * asyncore de ses essais). docs/POSIX.md.
 */
#ifndef _POSIX_ERRNO_H
#define _POSIX_ERRNO_H
#include_next <errno.h>

#ifndef ESHUTDOWN
#define ESHUTDOWN 110		/* Can't send after socket shutdown */
#endif

#endif
