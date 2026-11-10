/*
 * mman.c -- mmap / munmap / mprotect / madvise (libposix, docs/POSIX.md ; repris du repli d'Onyx).
 *
 * Le noyau d'ARMPI projette de la memoire anonyme a la demande (appels VM_MAP, VM_UNMAP,
 * VM_PROTECT, VM_ADVISE, VM_PAGEINFO : docs/ABI.md 0x06-0x0C) :
 *  - anonyme : une region a zero, peuplee a la demande, dans l'arene du noyau (reservations de
 *    plusieurs Go, MAP_FIXED dans l'arene, PROT_EXEC, protections appliquees) ; MAP_SHARED
 *    anonyme est prive (pas de fork) ;
 *  - un fichier, MAP_PRIVATE (ou MAP_SHARED en lecture seule) : une copie lue par pread dans une
 *    region anonyme, puis protegee comme demande ; MAP_SHARED en ecriture : ENOTSUP ;
 *  - un objet de memoire partagee (memfd_create, shm_open : ipc.c) : MAP_SHARED = SHM_MAP (les
 *    pages de l'objet, les memes dans chaque process) ; MAP_PRIVATE = une copie ;
 *  - munmap, mprotect, madvise : ceux du noyau ; mincore : VM_PAGEINFO page par page ;
 *    msync, mlock : sans effet.
 * Sur un noyau sans ces appels (E_NOSYS), repli d'Onyx : des blocs du tas (ni MAP_FIXED, ni plus
 * de 256 Mo, ni PROT_EXEC, protections non appliquees).
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "posix_internal.h"

static inline unsigned long page_up (unsigned long n) { return (n + POSIX_PAGE - 1) & ~(POSIX_PAGE - 1); }

/* ---- the fallback (a kernel without VM_MAP): heap blocks ---- */
struct heapmap
{
	struct heapmap *next;
	unsigned long start, len;
};
static struct heapmap *s_heapmaps;
static volatile unsigned s_heapLock;
void __posix_mman_fork (int phase) { __posix_fork_lock (&s_heapLock, phase); }


static void *heap_map (unsigned long len)
{
	struct heapmap *m = (struct heapmap *) malloc (sizeof *m);
	void *p = memalign (POSIX_PAGE, len);
	if (m == 0 || p == 0)
	{
		free (m);
		free (p);
		errno = ENOMEM;
		return MAP_FAILED;
	}
	memset (p, 0, len);
	m->start = (unsigned long) p;
	m->len = len;
	__posix_lock (&s_heapLock);
	m->next = s_heapmaps;
	s_heapmaps = m;
	__posix_unlock (&s_heapLock);
	return p;
}

static struct heapmap *heap_find (unsigned long a, int unlink)
{
	__posix_lock (&s_heapLock);
	for (struct heapmap **pp = &s_heapmaps; *pp; pp = &(*pp)->next)
	{
		struct heapmap *m = *pp;
		if (a >= m->start && a < m->start + m->len)
		{
			if (unlink && a == m->start)
				*pp = m->next;
			__posix_unlock (&s_heapLock);
			return m;
		}
	}
	__posix_unlock (&s_heapLock);
	return 0;
}

#define ARMPI_E_NOSYS	(-38)			/* (codes d'ARMPI : numeros de Linux) */
static int s_noVm;					/* noyau sans VM_MAP : repli sur le tas */

static void *heap_anon (unsigned long len, int prot, int flags)
{
	if (prot & PROT_EXEC)				/* (heap memory never runs) */
	{
		errno = ENOTSUP;
		return MAP_FAILED;
	}
	if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))
	{
		errno = ENOMEM;
		return MAP_FAILED;
	}
	if (len > (256UL << 20))		/* (a reservation: the heap cannot hold it) */
	{
		errno = ENOMEM;
		return MAP_FAILED;
	}
	return heap_map (len);
}

static void *anon_map (void *addr, unsigned long len, int prot, int flags)
{
	if (!s_noVm)
	{
		long r = __armpi_sys6 (SYS_VM_MAP, addr, len, prot,
				       flags & (MAP_FIXED | MAP_FIXED_NOREPLACE | MAP_POPULATE | MAP_NORESERVE), 0, 0);
		if (r != ARMPI_E_NOSYS)
		{
			if (r < 0 && r > -4096)
			{
				errno = __armpi_errno (r);
				return MAP_FAILED;
			}
			return (void *) r;
		}
		s_noVm = 1;
	}
	return heap_anon (len, prot, flags);
}


void *mmap (void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	if (len == 0 || (off & (POSIX_PAGE - 1)) != 0 || off < 0)
	{
		errno = EINVAL;
		return MAP_FAILED;
	}
	unsigned long n = page_up (len);
	int type = flags & MAP_TYPE;
	if (type != MAP_SHARED && type != MAP_PRIVATE)
	{
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (flags & MAP_ANONYMOUS)
		return anon_map (addr, n, prot, flags);

	/* a file */
	struct __posix_ofd *d = __posix_fd_get (fd);
	if (d == 0)
		return MAP_FAILED;
	void *p = MAP_FAILED;
	if (d->type == POSIX_FD_SHM)			/* memfd, shm_open (ipc.c) */
	{
		if (s_noVm)
			errno = ENODEV;
		else if (type == MAP_SHARED && (prot & PROT_WRITE) && (d->flags & O_ACCMODE) == O_RDONLY)
			errno = EACCES;
		else
			p = __posix_shm_mmap (d, addr, n, prot, flags, (long) off);
	}
	else if (type == MAP_SHARED && (prot & PROT_WRITE))
		errno = ENOTSUP;
	else if (d->type != POSIX_FD_FILE)
		errno = ENODEV;
	else if ((d->flags & O_ACCMODE) == O_WRONLY)
		errno = EACCES;
	else
	{
		struct stat st;
		if (!s_noVm && fstat (fd, &st) == 0)
			p = __posix_file_share (d, &st, addr, n, prot, flags, (long) off);
		if (p != MAP_FAILED)
			goto done;
		p = anon_map (addr, n, PROT_READ | PROT_WRITE, flags & ~MAP_NORESERVE);
		if (p != MAP_FAILED)
		{
			size_t got = 0;
			while (got < len)
			{
				ssize_t r = __posix_pread (d, (char *) p + got, len - got, off + (off_t) got);
				if (r <= 0)
					break;
				got += (size_t) r;
			}
			if (!s_noVm && prot != (PROT_READ | PROT_WRITE))
				__armpi_sys3 (SYS_VM_PROTECT, p, n, prot);
		}
	}
done:
	__posix_fd_put (d);
	return p;
}

int munmap (void *addr, size_t len)
{
	unsigned long a = (unsigned long) addr;
	if ((a & (POSIX_PAGE - 1)) != 0 || len == 0)
		return POSIX_ERR (EINVAL);
	struct heapmap *m = heap_find (a, 1);
	if (m)
	{
		if (m->start == a)
		{
			free ((void *) m->start);
			free (m);
		}
		return 0;				/* (a part of a heap mapping: kept) */
	}
	if (s_noVm)
		return 0;
	return __armpi_ret (__armpi_sys2 (SYS_VM_UNMAP, a, page_up (len))) < 0 ? -1 : 0;
}

int mprotect (void *addr, size_t len, int prot)
{
	unsigned long a = (unsigned long) addr;
	if ((a & (POSIX_PAGE - 1)) != 0)
		return POSIX_ERR (EINVAL);
	if (s_noVm || heap_find (a, 0))
		return (prot & PROT_EXEC) ? POSIX_ERR (ENOTSUP) : 0;	/* (tas : pas appliquee) */
	return __armpi_ret (__armpi_sys3 (SYS_VM_PROTECT, a, page_up (len), prot)) < 0 ? -1 : 0;
}

int madvise (void *addr, size_t len, int advice)
{
	unsigned long a = (unsigned long) addr;
	if ((a & (POSIX_PAGE - 1)) != 0)
		return POSIX_ERR (EINVAL);
	if (advice == MADV_DONTDUMP || advice == MADV_DODUMP)
		return 0;
	if (heap_find (a, 0))
	{
		if (advice == MADV_DONTNEED)
			memset (addr, 0, len);
		return 0;
	}
	if (s_noVm || (advice != MADV_WILLNEED && advice != MADV_DONTNEED && advice != MADV_FREE))
		return 0;
	return __armpi_ret (__armpi_sys3 (SYS_VM_ADVISE, a, page_up (len), advice)) < 0 ? -1 : 0;
}

int posix_madvise (void *addr, size_t len, int advice)
{
	return madvise (addr, len, advice) == 0 ? 0 : errno;
}

int mincore (void *addr, size_t len, unsigned char *vec)
{
	unsigned long a = (unsigned long) addr;
	if ((a & (POSIX_PAGE - 1)) != 0)
		return POSIX_ERR (EINVAL);
	unsigned long pages = page_up (len) / POSIX_PAGE;
	for (unsigned long i = 0; i < pages; i++)
	{
		long r = s_noVm ? 1 : __armpi_sys1 (SYS_VM_PAGEINFO, a + i * POSIX_PAGE);
		vec[i] = (r == ARMPI_E_NOSYS) ? 1 : (r > 0 ? (unsigned char) (r & 1) : 0);	/* (bit 0 : PI_PRESENT) */
	}
	return 0;
}

int msync (void *addr, size_t len, int flags) { (void) addr; (void) len; (void) flags; return 0; }
int mlock (const void *addr, size_t len) { (void) addr; (void) len; return 0; }
int munlock (const void *addr, size_t len) { (void) addr; (void) len; return 0; }
int mlockall (int flags) { (void) flags; return 0; }
int munlockall (void) { return 0; }

int getpagesize (void) { return (int) POSIX_PAGE; }

/* newlib has memalign, aligned_alloc and malloc_usable_size; POSIX's own: */
int posix_memalign (void **out, size_t align, size_t size)
{
	if (align < sizeof (void *) || (align & (align - 1)) != 0)
		return EINVAL;
	void *p = memalign (align, size ? size : 1);
	if (p == 0)
		return ENOMEM;
	*out = p;
	return 0;
}
