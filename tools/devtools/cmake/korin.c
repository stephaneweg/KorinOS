/* korin.c : la partie de libuv propre au système, pour Korin (la libuv de CMake, Utilities/cmlibuv ; ajoutée par
 * tools/devtools/cmake.sh). Comme qnx.c : la boucle par poll (posix-poll.c), l'heure par clock_gettime
 * (posix-hrtime.c), sans titre de process ni surveillance de fichiers. Korin n'a ni /proc ni charge moyenne :
 * ce qu'il ne dit pas vaut 0. Licence de libuv (MIT, Utilities/cmlibuv/LICENSE). */
#include "uv.h"
#include "internal.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern char *program_invocation_name;

void uv_loadavg(double avg[3]) {
  avg[0] = avg[1] = avg[2] = 0;
}

int uv_exepath(char* buffer, size_t* size) {
  char abspath[PATH_MAX];
  size_t len;
  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;
  if (realpath(program_invocation_name, abspath) == NULL)
    return UV__ERR(errno);
  len = strlen(abspath);
  if (len >= *size)
    len = *size - 1;
  memcpy(buffer, abspath, len);
  buffer[len] = '\0';
  *size = len;
  return 0;
}

uint64_t uv_get_free_memory(void) {
  long pages = sysconf(_SC_AVPHYS_PAGES), size = sysconf(_SC_PAGESIZE);
  return pages > 0 && size > 0 ? (uint64_t)pages * (uint64_t)size : 0;
}

uint64_t uv_get_total_memory(void) {
  long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGESIZE);
  return pages > 0 && size > 0 ? (uint64_t)pages * (uint64_t)size : 0;
}

uint64_t uv_get_constrained_memory(void) {
  return 0;
}

uint64_t uv_get_available_memory(void) {
  return uv_get_free_memory();
}

int uv_resident_set_memory(size_t* rss) {
  *rss = 0;
  return UV_ENOSYS;
}

int uv_uptime(double* uptime) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t))
    return UV__ERR(errno);
  *uptime = (double)t.tv_sec + (double)t.tv_nsec / 1e9;
  return 0;
}

int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  if (n < 1)
    n = 1;
  *cpu_infos = uv__calloc((size_t)n, sizeof(**cpu_infos));
  if (*cpu_infos == NULL)
    return UV_ENOMEM;
  for (long i = 0; i < n; i++)
    (*cpu_infos)[i].model = uv__strdup("x86_64");
  *count = (int)n;
  return 0;
}
