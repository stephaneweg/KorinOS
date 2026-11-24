/*
 * xf86drm.h -- le faux libdrm de lavapipe (tools/mesa/lvp, configure-lvp.sh) : celui du PC (tools/mesa/libdrm-pc),
 * complete pour le chargeur de Mesa ; toutes ses fonctions sont des bouchons (korin_lvp_drm.c).
 */
#ifndef _XF86DRM_H
#define _XF86DRM_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <errno.h>
#include "drm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* (les noms des nœuds de libdrm : le pipe-loader de Gallium les cite ; aucun n'existe sous Korin) */
#define DRM_DIR_NAME		"/dev/dri"
#define DRM_PRIMARY_MINOR_NAME	"card"
#define DRM_CONTROL_MINOR_NAME	"controlD"
#define DRM_RENDER_MINOR_NAME	"renderD"
#define DRM_DEV_NAME		"%s/card%d"
#define DRM_RENDER_DEV_NAME	"%s/renderD%d"
#define DRM_NODE_PRIMARY	0
#define DRM_NODE_CONTROL	1
#define DRM_NODE_RENDER		2
#define DRM_NODE_MAX		3
#define DRM_BUS_PCI		0
#define DRM_BUS_USB		1
#define DRM_BUS_PLATFORM	2
#define DRM_BUS_HOST1X		3
#define DRM_DEVICE_GET_PCI_REVISION	(1 << 0)
#define DRM_CLOEXEC		O_CLOEXEC
#define DRM_RDWR		O_RDWR

typedef unsigned int drm_magic_t;

typedef struct _drmVersion {
	int version_major, version_minor, version_patchlevel;
	int name_len;
	char *name;
	int date_len;
	char *date;
	int desc_len;
	char *desc;
} drmVersion, *drmVersionPtr;

typedef struct _drmPciBusInfo {
	uint16_t domain;
	uint8_t bus, dev, func;
} drmPciBusInfo, *drmPciBusInfoPtr;

typedef struct _drmPciDeviceInfo {
	uint16_t vendor_id, device_id, subvendor_id, subdevice_id;
	uint8_t revision_id;
} drmPciDeviceInfo, *drmPciDeviceInfoPtr;

typedef struct _drmDevice {
	char **nodes;
	int available_nodes;
	int bustype;
	union {
		drmPciBusInfoPtr pci;
		struct { char fullname[512]; } *platform;
		struct { char fullname[512]; } *host1x;
		void *other;
	} businfo;
	union {
		drmPciDeviceInfoPtr pci;
		void *other;
	} deviceinfo;
} drmDevice, *drmDevicePtr;

int drmIoctl (int fd, unsigned long request, void *arg);
drmVersionPtr drmGetVersion (int fd);
void drmFreeVersion (drmVersionPtr v);
int drmGetDevice2 (int fd, uint32_t flags, drmDevicePtr *device);
int drmGetDevice (int fd, drmDevicePtr *device);
void drmFreeDevice (drmDevicePtr *device);
int drmGetDevices2 (uint32_t flags, drmDevicePtr devices[], int max_devices);
void drmFreeDevices (drmDevicePtr devices[], int count);
int drmDevicesEqual (drmDevicePtr a, drmDevicePtr b);
int drmGetCap (int fd, uint64_t capability, uint64_t *value);
int drmPrimeFDToHandle (int fd, int prime_fd, uint32_t *handle);
int drmPrimeHandleToFD (int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmSyncobjCreate (int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy (int fd, uint32_t handle);
int drmSyncobjWait (int fd, uint32_t *handles, unsigned count, int64_t timeout_nsec, unsigned flags,
		    uint32_t *first_signaled);
int drmSyncobjImportSyncFile (int fd, uint32_t handle, int sync_file_fd);
int drmSyncobjExportSyncFile (int fd, uint32_t handle, int *sync_file_fd);
int drmSyncobjFDToHandle (int fd, int obj_fd, uint32_t *handle);
int drmSyncobjHandleToFD (int fd, uint32_t handle, int *obj_fd);
int drmSyncobjReset (int fd, const uint32_t *handles, uint32_t count);
int drmSyncobjSignal (int fd, const uint32_t *handles, uint32_t count);
int drmSyncobjTimelineSignal (int fd, const uint32_t *handles, uint64_t *points, uint32_t count);
int drmSyncobjTimelineWait (int fd, uint32_t *handles, uint64_t *points, unsigned count, int64_t timeout,
			    unsigned flags, uint32_t *first);
int drmSyncobjQuery2 (int fd, uint32_t *handles, uint64_t *points, uint32_t count, uint32_t flags);
int drmSyncobjTransfer (int fd, uint32_t dst, uint64_t dst_point, uint32_t src, uint64_t src_point, uint32_t flags);

/* lavapipe (tools/mesa/lvp) : ce que le chargeur de Mesa (src/loader/loader.c) appelle ; des bouchons
 * (korin_lvp_drm.c) : lavapipe n'ouvre aucun peripherique DRM */
int drmCommandWriteRead (int fd, unsigned long index, void *data, unsigned long size);
int drmGetDeviceFromDevId (dev_t dev_id, uint32_t flags, drmDevicePtr *device);
char *drmGetDeviceNameFromFd2 (int fd);

#ifdef __cplusplus
}
#endif

#endif
