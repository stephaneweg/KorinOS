/* armpi_compat.h -- inclus partout dans CoreCLR (tools/dotnet/toolchain.cmake) : ce que le code de
 * .NET attend de glibc et que newlib n'annonce pas. ARMPI s'y fait passer pour Linux. */
#ifndef _ARMPI_COMPAT_H
#define _ARMPI_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

/* (newlib les a sous ces noms) */
struct __sFILE;
long getline (char **, unsigned long *, struct __sFILE *) __asm__ ("__getline");
long getdelim (char **, unsigned long *, int, struct __sFILE *) __asm__ ("__getdelim");

typedef unsigned short __u16;
typedef unsigned int __u32;
typedef unsigned long long __u64;

#ifdef __cplusplus
}
#endif

#define _POSIX_HOST_NAME_MAX	255
#define SSIZE_MAX		0x7FFFFFFFFFFFFFFFL
#define DT_WHT			14
#define IFF_ALLMULTI		0x200

/* (ioctl de Linux : aucun ne marche sous ARMPI, il faut seulement que le code compile) */
#define _IOC(d, t, n, s)	(((d) << 30) | ((s) << 16) | ((t) << 8) | (n))
#define _IO(t, n)		_IOC (0U, (t), (n), 0)
#define _IOW(t, n, s)		_IOC (1U, (t), (n), sizeof (s))
#define _IOR(t, n, s)		_IOC (2U, (t), (n), sizeof (s))
#define _IOWR(t, n, s)		_IOC (3U, (t), (n), sizeof (s))

#endif
