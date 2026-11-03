/* libsync.h -- un job fini au retour du service v3d : pas de descripteur de synchronisation. */
#ifndef _ARMPI_LIBSYNC_H
#define _ARMPI_LIBSYNC_H
static inline int sync_wait (int fd, int timeout) { (void) fd; (void) timeout; return 0; }
static inline int sync_accumulate (const char *name, int *fd1, int fd2) { (void) name; (void) fd1; (void) fd2; return 0; }
#endif
