/* get the executable's name (libposix le deduit d'argv[0] : pas de /proc) */

#include "../fb.h"

extern const char *__posix_exe_path( void );

char *fb_hGetExeName( char *dst, ssize_t maxlen )
{
	char *p;

	strncpy( dst, __posix_exe_path( ), maxlen - 1 );
	dst[maxlen - 1] = '\0';
	p = strrchr( dst, '/' );

	return p ? p + 1 : dst;
}
