/* tools/mediakit/inc/stdio.h : stdout et stderr du kit (journal : mk_log, mk_libc.c) au lieu des variables
 * par thread de la newlib des chaines d'Onyx (_tls_stderr...), que le kit ne peut pas avoir. */
#include_next <stdio.h>
#undef stdin
#undef stdout
#undef stderr
extern void *mk_stderr;
#define stdin ((FILE *) 0)
#define stdout ((FILE *) mk_stderr)
#define stderr ((FILE *) mk_stderr)
