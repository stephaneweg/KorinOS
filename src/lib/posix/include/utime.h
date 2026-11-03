/* utime : newlib ne le declare pas (libposix, stat.c) */
#ifndef _POSIX_UTIME_H
#define _POSIX_UTIME_H
#include_next <utime.h>
#ifdef __cplusplus
extern "C" {
#endif
int utime (const char *path, const struct utimbuf *times);
#ifdef __cplusplus
}
#endif
#endif