/* ftw.h -- ftw, nftw (libposix, ftw.c) */
#ifndef _FTW_H
#define _FTW_H
#include <sys/stat.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FTW_F	0		/* fichier */
#define FTW_D	1		/* dossier, avant son contenu */
#define FTW_DNR	2		/* dossier illisible */
#define FTW_DP	3		/* dossier, apres son contenu (FTW_DEPTH) */
#define FTW_NS	4		/* stat a echoue */
#define FTW_SL	5		/* (pas de liens sur ARMPI) */
#define FTW_SLN	6
#define FTW_PHYS	1
#define FTW_MOUNT	2
#define FTW_DEPTH	8
#define FTW_CHDIR	4
struct FTW
{
	int base;		/* debut du nom dans le chemin */
	int level;		/* profondeur */
};
int ftw (const char *path, int (*fn) (const char *, const struct stat *, int), int fds);
int nftw (const char *path, int (*fn) (const char *, const struct stat *, int, struct FTW *), int fds, int flags);
#ifdef __cplusplus
}
#endif
#endif
