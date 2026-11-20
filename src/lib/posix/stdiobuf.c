/*
 * stdiobuf.c -- le tampon de chaque FILE de stdio (newlib 4.4) : __smakebuf_r et __swhatbuf_r, à la place de ceux de
 * makebuf.o de la libc.
 *
 * La newlib de Korin n'est pas construite avec HAVE_BLKSIZE : son tampon fait toujours BUFSIZ (1024 octets sur le PC),
 * quel que soit st_blksize. Un fread de gros fichier devenait un read () de 1 Ko par remplissage, autant
 * d'allers-retours avec vfs : 21,7 Mo en 7 s sous QEMU PC, contre 0,2 s avec un tampon de 64 Ko
 * (src/lib/posix/test/fbench.c, 20/11/2026). Ici, un fichier régulier reçoit un tampon de st_blksize (64 Ko,
 * stat.c) ; le reste (terminal, tube, socket) garde BUFSIZ, comme avant.
 *
 * Le reste est celui de newlib 4.4.0 (libc/stdio/makebuf.c, licence BSD de l'université de Californie) : mêmes
 * drapeaux, même repli sans mémoire. start.c y fait référence, pour que ce soit toujours celui-ci qui soit lié, et
 * jamais celui de la libc (les deux définissent les mêmes noms).
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <reent.h>

#define ASPRINTF_BUFSIZE	64	/* (_DEFAULT_ASPRINTF_BUFSIZE de newlib : le premier tampon d'asprintf) */
#define FILE_BUFSIZE_MAX	(1 << 20)

int __swhatbuf_r (struct _reent *ptr, FILE *fp, size_t *bufsize, int *couldbetty)
{
	struct stat st;

	if (fp->_file < 0 || _fstat_r (ptr, fp->_file, &st) < 0)
	{
		*couldbetty = 0;
		/* (asprintf et ses semblables : le premier tampon de leur chaîne) */
		*bufsize = (fp->_flags & __SMBF) ? ASPRINTF_BUFSIZE : BUFSIZ;
		return 0;
	}
	/* un terminal ne peut être qu'un périphérique caractère */
	*couldbetty = S_ISCHR (st.st_mode);
	if (S_ISREG (st.st_mode) && st.st_blksize > BUFSIZ)
		*bufsize = st.st_blksize > FILE_BUFSIZE_MAX ? FILE_BUFSIZE_MAX : (size_t) st.st_blksize;
	else
		*bufsize = BUFSIZ;
	/* (pas l'optimisation de fseek de newlib : elle demande _blksize, que cette newlib sans HAVE_BLKSIZE n'a pas) */
	return __SNPT;
}

void __smakebuf_r (struct _reent *ptr, FILE *fp)
{
	void *p;
	int flags;
	size_t size;
	int couldbetty;

	if (fp->_flags & __SNBF)
	{
		fp->_bf._base = fp->_p = fp->_nbuf;
		fp->_bf._size = 1;
		return;
	}
	flags = __swhatbuf_r (ptr, fp, &size, &couldbetty);
	if ((p = _malloc_r (ptr, size)) == NULL)
	{
		if (!(fp->_flags & __SSTR))
		{
			fp->_flags = (fp->_flags & ~__SLBF) | __SNBF;
			fp->_bf._base = fp->_p = fp->_nbuf;
			fp->_bf._size = 1;
		}
	}
	else
	{
		fp->_flags |= __SMBF;
		fp->_bf._base = fp->_p = (unsigned char *) p;
		fp->_bf._size = (int) size;
		if (couldbetty && _isatty_r (ptr, fp->_file))
			fp->_flags |= __SLBF;
		fp->_flags |= flags;
	}
}
