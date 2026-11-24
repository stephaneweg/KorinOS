/*
 * korin_lvp_drm.c -- les fonctions du faux libdrm de lavapipe (lvp/libdrm/xf86drm.h) : des bouchons. lavapipe
 * n'ouvre aucun peripherique DRM ; le chargeur et la WSI de Mesa les citent sans s'en servir ici.
 */
#include <errno.h>
#include <stdlib.h>
#include "xf86drm.h"

__attribute__ ((weak)) int drmIoctl (int fd, unsigned long request, void *arg)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) drmVersionPtr drmGetVersion (int fd)
{ errno = ENODEV; return NULL; }
__attribute__ ((weak)) void drmFreeVersion (drmVersionPtr v)
{ }
__attribute__ ((weak)) int drmGetDevice2 (int fd, uint32_t flags, drmDevicePtr *device)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmGetDevice (int fd, drmDevicePtr *device)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) void drmFreeDevice (drmDevicePtr *device)
{ }
__attribute__ ((weak)) int drmGetDevices2 (uint32_t flags, drmDevicePtr devices[], int max_devices)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) void drmFreeDevices (drmDevicePtr devices[], int count)
{ }
__attribute__ ((weak)) int drmDevicesEqual (drmDevicePtr a, drmDevicePtr b)
{ return 0; }
__attribute__ ((weak)) int drmGetCap (int fd, uint64_t capability, uint64_t *value)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmPrimeFDToHandle (int fd, int prime_fd, uint32_t *handle)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmPrimeHandleToFD (int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjCreate (int fd, uint32_t flags, uint32_t *handle)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjDestroy (int fd, uint32_t handle)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjWait (int fd, uint32_t *handles, unsigned count, int64_t timeout_nsec, unsigned flags, uint32_t *first_signaled)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjImportSyncFile (int fd, uint32_t handle, int sync_file_fd)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjExportSyncFile (int fd, uint32_t handle, int *sync_file_fd)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjFDToHandle (int fd, int obj_fd, uint32_t *handle)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjHandleToFD (int fd, uint32_t handle, int *obj_fd)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjReset (int fd, const uint32_t *handles, uint32_t count)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjSignal (int fd, const uint32_t *handles, uint32_t count)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjTimelineSignal (int fd, const uint32_t *handles, uint64_t *points, uint32_t count)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjTimelineWait (int fd, uint32_t *handles, uint64_t *points, unsigned count, int64_t timeout, unsigned flags, uint32_t *first)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjQuery2 (int fd, uint32_t *handles, uint64_t *points, uint32_t count, uint32_t flags)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmSyncobjTransfer (int fd, uint32_t dst, uint64_t dst_point, uint32_t src, uint64_t src_point, uint32_t flags)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmCommandWriteRead (int fd, unsigned long index, void *data, unsigned long size)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) int drmGetDeviceFromDevId (dev_t dev_id, uint32_t flags, drmDevicePtr *device)
{ errno = ENODEV; return -ENODEV; }
__attribute__ ((weak)) char * drmGetDeviceNameFromFd2 (int fd)
{ errno = ENODEV; return NULL; }
