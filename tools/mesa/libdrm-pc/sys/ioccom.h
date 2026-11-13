/* sys/ioccom.h -- drm.h le cherche hors de Linux. Les numeros restent ceux de Linux : drmIoctl les trie. */
#ifndef _SYS_IOCCOM_H
#define _SYS_IOCCOM_H
#define _IOC(dir, type, nr, size)	(((dir) << 30) | ((size) << 16) | ((type) << 8) | (nr))
#define _IO(type, nr)			_IOC (0U, (type), (nr), 0U)
#define _IOW(type, nr, t)		_IOC (1U, (type), (nr), (unsigned) sizeof (t))
#define _IOR(type, nr, t)		_IOC (2U, (type), (nr), (unsigned) sizeof (t))
#define _IOWR(type, nr, t)		_IOC (3U, (type), (nr), (unsigned) sizeof (t))
#endif
