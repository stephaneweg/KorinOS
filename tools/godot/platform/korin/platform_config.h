/**************************************************************************/
/*  platform_config.h                                                     */
/**************************************************************************/
/* Korin OS (newlib et libposix de Korin). */

#pragma once

#include <alloca.h>

/* libposix ne nomme pas les threads */
#define PTHREAD_NO_RENAME
