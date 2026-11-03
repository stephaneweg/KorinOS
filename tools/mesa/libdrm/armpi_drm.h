/* armpi_drm.h -- l'ouverture du winsys (armpi_drm.c). */
#ifndef _ARMPI_DRM_H
#define _ARMPI_DRM_H
#define ARMPI_DRM_DRY		1	/* memoire ordinaire, jobs comptes et pas lances */
#define ARMPI_DRM_DRY_BUF	2	/* vrais tampons non caches sous 1 Go, jobs comptes et pas lances */
/* mode : 0 ou ARMPI_DRM_DRY* -> le faux descripteur du pilote ; -1 : pas de GPU */
int armpi_drm_open (int mode);
/* 1 : un job a ete refuse ou n'a pas fini : le GPU ne sert plus */
int armpi_drm_lost (void);
/* jobs soumis depuis le debut */
long armpi_drm_jobs (void);
unsigned armpi_drm_first_phys (void);
unsigned armpi_drm_import (void *va, unsigned phys, unsigned bytes);
/* une image en memoire basse et cachee, a nous : son adresse ; name : son nom pour GEM_OPEN */
void *armpi_drm_buffer (unsigned long bytes, unsigned *name);
/* vide et invalide le cache sur cette zone */
void armpi_drm_flush (void *va, unsigned long bytes);
/* deux images dans une surface d'Elegant attachee a la fenetre <id> ; names : leurs noms pour GEM_OPEN */
void *armpi_drm_window (long id, int x, int y, int width, int height, unsigned names[2], unsigned *pitch);
void armpi_drm_window_free (void *window);
void armpi_drm_zone (void *window, long id, int x, int y, int width, int height);
int armpi_drm_present (void *window, int image);
void armpi_drm_times (void *window, double *cache, double *attach);
void armpi_drm_times_reset (void *window);
#endif
