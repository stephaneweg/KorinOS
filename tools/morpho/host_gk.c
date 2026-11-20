/* host_gk.c -- GpuKit du PC (src/kits/gpukit/gpukit.c) compilé pour l'hôte Linux : le rastériseur du processeur de
 * gk_draw, tel que le PC sans GPU Intel le fait (tools/morpho/run.sh). Les appels système de mémoire deviennent mmap. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#define KORIN_KIT_H
#define KIT_HIDDEN
#define SYS_VM_MAP 1
#define SYS_VM_UNMAP 2
static long host_map (long len)
{
	void *p = mmap (0, (size_t) len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : (long) p;
}
static long host_unmap (long va, long len) { munmap ((void *) va, (size_t) len); return 0; }
#define korin_sys4(n, a, b, c, d) host_map ((long) (b))
#define korin_sys2(n, a, b) host_unmap ((long) (a), (long) (b))
#undef PROT_READ
#undef PROT_WRITE
#include "../../src/kits/gpukit/gpukit.c"
int gpukit_kit_open (void) { return 1; }
