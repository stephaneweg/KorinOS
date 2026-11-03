/* get the executable path (libposix le deduit d'argv[0] : pas de /proc) */

#include "../fb.h"

extern const char *__posix_exe_path( void );

char *fb_hGetExePath( char *dst, ssize_t maxlen )
{
	char *p;

	strncpy( dst, __posix_exe_path( ), maxlen - 1 );
	dst[maxlen - 1] = '\0';
	p = strrchr( dst, '/' );
	if( p == dst ) /* keep the "/" rather than returning "" */
		*(p + 1) = '\0';
	else if( p )
		*p = '\0';
	else
		dst[0] = '\0';

	return dst;
}
