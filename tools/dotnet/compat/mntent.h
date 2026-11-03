/* mntent.h -- pas de /proc/mounts sur ARMPI : la liste des montages est vide */
#ifndef _ARMPI_COMPAT_MNTENT_H
#define _ARMPI_COMPAT_MNTENT_H

#include <stdio.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>

#define MNTOPT_RO	"ro"

struct mntent
{
	char *mnt_fsname, *mnt_dir, *mnt_type, *mnt_opts;
	int mnt_freq, mnt_passno;
};

static inline FILE *setmntent (const char *f, const char *m) { (void) f; (void) m; return 0; }
static inline struct mntent *getmntent_r (FILE *f, struct mntent *e, char *b, int n)
{
	(void) f; (void) e; (void) b; (void) n;
	return 0;
}
static inline int endmntent (FILE *f) { (void) f; return 1; }

#endif
