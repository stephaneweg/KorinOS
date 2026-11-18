/* tools/mediakit/inc/errno.h : errno du kit. La newlib des chaines d'Onyx met errno dans une variable par
 * thread (_tls_errno, _Thread_local), que le kit ne peut pas avoir (pas de TLS : build.sh le refuse) ;
 * ici errno passe par __errno () (mk_libc.c), pour tout ce que le kit compile (FFmpeg compris :
 * tools/mediakit/ffmpeg.sh). */
#include_next <errno.h>
#undef errno
extern int *__errno(void);
#define errno (*__errno())
