/*
 * xf86drm.h -- le faux libdrm d'ARMPI (docs/MESA.md). Mesa ne parle a Linux que par ces fonctions :
 * les remplacer suffit a brancher le pilote sur le service v3d.
 */
#ifndef _XF86DRM_H
#define _XF86DRM_H

#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include "drm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DRM_NODE_PRIMARY	0
#define DRM_NODE_RENDER		2
#define DRM_NODE_MAX		3
#define DRM_BUS_PLATFORM	2
#define DRM_CLOEXEC		O_CLOEXEC
#define DRM_RDWR		O_RDWR

typedef struct _drmVersion {
	int version_major, version_minor, version_patchlevel;
	int name_len;
	char *name;
	int date_len;
	char *date;
	int desc_len;
	char *desc;
} drmVersion, *drmVersionPtr;

int drmIoctl (int fd, unsigned long request, void *arg);
int drmSyncobjCreate (int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy (int fd, uint32_t handle);
int drmSyncobjWait (int fd, uint32_t *handles, unsigned count, int64_t timeout_nsec, unsigned flags,
		    uint32_t *first_signaled);
int drmSyncobjImportSyncFile (int fd, uint32_t handle, int sync_file_fd);
int drmSyncobjExportSyncFile (int fd, uint32_t handle, int *sync_file_fd);
/* u_sync_provider.c seulement (Vulkan) : jamais appelees par v3d */
int drmSyncobjFDToHandle (int fd, int obj_fd, uint32_t *handle);
int drmSyncobjHandleToFD (int fd, uint32_t handle, int *obj_fd);
int drmSyncobjReset (int fd, const uint32_t *handles, uint32_t count);
int drmSyncobjSignal (int fd, const uint32_t *handles, uint32_t count);
int drmSyncobjTimelineSignal (int fd, const uint32_t *handles, uint64_t *points, uint32_t count);
int drmSyncobjTimelineWait (int fd, uint32_t *handles, uint64_t *points, unsigned count, int64_t timeout,
			    unsigned flags, uint32_t *first);
int drmSyncobjQuery2 (int fd, uint32_t *handles, uint64_t *points, uint32_t count, uint32_t flags);
int drmSyncobjTransfer (int fd, uint32_t dst, uint64_t dst_point, uint32_t src, uint64_t src_point, uint32_t flags);
int drmGetCap (int fd, uint64_t capability, uint64_t *value);
int drmPrimeFDToHandle (int fd, int prime_fd, uint32_t *handle);
int drmPrimeHandleToFD (int fd, uint32_t handle, uint32_t flags, int *prime_fd);
drmVersionPtr drmGetVersion (int fd);
void drmFreeVersion (drmVersionPtr v);

/* la vue du processeur sur un BO (a la place du mmap de Linux) */
void *armpi_v3d_bo_map (uint32_t handle);

#ifdef __cplusplus
}
#endif

#endif
