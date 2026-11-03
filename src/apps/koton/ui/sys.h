//
// ui/sys.h -- the clock and whole files, on POSIX.
//
#ifndef _koton_sys_h
#define _koton_sys_h

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace kui {

static inline unsigned long clockNs ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned long) t.tv_sec * 1000000000ul + (unsigned long) t.tv_nsec;
}
static inline unsigned ticks () { return (unsigned) (clockNs () / 10000000ul); }		// 100 Hz
static inline unsigned clockUs () { return (unsigned) (clockNs () / 1000ul); }

// a file in memory, NUL-terminated (free it) -> 0: not there, out of memory; *n: its bytes
static inline char *readFile (const char *path, unsigned *n)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0) return 0;
	struct stat st;
	char *b = fstat (fd, &st) == 0 ? (char *) malloc ((size_t) st.st_size + 1) : 0;
	long got = b ? read (fd, b, (size_t) st.st_size) : 0;
	close (fd);
	if (!b) return 0;
	if (got < 0) got = 0;
	b[got] = 0;
	*n = (unsigned) got;
	return b;
}

// -> true: all of it written
static inline bool writeFile (const char *path, const void *b, unsigned n)
{
	int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return false;
	long w = write (fd, b, n);
	return close (fd) == 0 && w == (long) n;
}

} // namespace kui

#endif
