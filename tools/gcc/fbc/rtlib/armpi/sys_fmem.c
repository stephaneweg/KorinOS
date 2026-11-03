/* fre() function */

#include "../fb.h"

FBCALL size_t fb_GetMemAvail( int mode )
{
	return sysconf( _SC_AVPHYS_PAGES ) * sysconf( _SC_PAGESIZE );
}
