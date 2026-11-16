/* sys/xattr.h : attributs etendus (misc.c). Les volumes de Korin (FAT, exFAT, NTFS en lecture) n'en
   gardent pas : chaque appel rend -1, errno = ENOTSUP, comme un systeme de fichiers Linux sans xattr.
   Godot (drivers/unix/file_access_unix.cpp) s'en sert pour une marque facultative. */
#ifndef _SYS_XATTR_H
#define _SYS_XATTR_H

#include <sys/types.h>

#define XATTR_CREATE	1
#define XATTR_REPLACE	2

#ifdef __cplusplus
extern "C" {
#endif
ssize_t getxattr (const char *path, const char *name, void *value, size_t size);
ssize_t lgetxattr (const char *path, const char *name, void *value, size_t size);
ssize_t fgetxattr (int fd, const char *name, void *value, size_t size);
int setxattr (const char *path, const char *name, const void *value, size_t size, int flags);
int lsetxattr (const char *path, const char *name, const void *value, size_t size, int flags);
int fsetxattr (int fd, const char *name, const void *value, size_t size, int flags);
int removexattr (const char *path, const char *name);
int lremovexattr (const char *path, const char *name);
int fremovexattr (int fd, const char *name);
ssize_t listxattr (const char *path, char *list, size_t size);
ssize_t llistxattr (const char *path, char *list, size_t size);
ssize_t flistxattr (int fd, char *list, size_t size);
#ifdef __cplusplus
}
#endif

#endif
