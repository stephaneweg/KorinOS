/*
 * stdiolock.c -- flockfile, ftrylockfile, funlockfile (POSIX) sur le verrou recursif que newlib
 * garde dans chaque FILE (_flockfile de <sys/stdio.h>) ; getgrnam_r, initgroups : une
 * seule identite (root, gid 0), comme getgrgid et getgrnam de misc.c. Ajoutes pour git (docs/GIT.md).
 */
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <sys/lock.h>

void flockfile (FILE *fp)
{
	if (!(fp->_flags & __SSTR))
		__lock_acquire_recursive (fp->_lock);
}

int ftrylockfile (FILE *fp)
{
	if (fp->_flags & __SSTR)
		return 0;
	return __lock_try_acquire_recursive (fp->_lock) ? 0 : -1;
}

void funlockfile (FILE *fp)
{
	if (!(fp->_flags & __SSTR))
		__lock_release_recursive (fp->_lock);
}

int getgrnam_r (const char *name, struct group *grp, char *buf, size_t len, struct group **res)
{
	static char *s_none[1];
	*res = 0;
	if (name == 0 || strcmp (name, "root") != 0)
		return 0;
	if (len < 6)
		return ERANGE;
	memcpy (buf, "root", 5);
	grp->gr_name = buf;
	grp->gr_passwd = buf + 4;
	grp->gr_gid = 0;
	grp->gr_mem = s_none;
	*res = grp;
	return 0;
}

int initgroups (const char *user, gid_t group)
{
	(void) user;
	(void) group;
	return 0;
}
