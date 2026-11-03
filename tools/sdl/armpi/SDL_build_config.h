/*
 * SDL_build_config.h -- la configuration de SDL 3 pour ARMPI, a la main : libposix sur newlib, sans dlopen,
 * sans fork. Les pilotes passent par les crochets « PRIVATE » de SDL : aucun fichier de SDL n'est retouche.
 * Ce qui n'est pas dans libposix ou newlib n'est pas declare ici : SDL prend sa propre version.
 */
#ifndef SDL_build_config_h_
#define SDL_build_config_h_

#include <SDL3/SDL_platform_defines.h>

#define SDL_PLATFORM_PRIVATE 1
#define SDL_PLATFORM_PRIVATE_NAME "ARMPI"
#define SIZEOF_VOIDP 8
#define HAVE_GCC_ATOMICS 1
#define HAVE_GCC_SYNC_LOCK_TEST_AND_SET 1

#define HAVE_LIBC 1
#define HAVE_FLOAT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MALLOC_H 1
#define HAVE_MATH_H 1
#define HAVE_MEMORY_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_WCHAR_H 1
#define HAVE_PTHREAD_NP_H 0
#undef HAVE_PTHREAD_NP_H

#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1
#define HAVE_GETENV 1
#define HAVE_SETENV 1
#define HAVE_PUTENV 1
#define HAVE_UNSETENV 1
#define HAVE_ABS 1
#define HAVE_BCOPY 1
#define HAVE_MEMSET 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_STRLEN 1
#define HAVE_STRNLEN 1
#define HAVE_STRPBRK 1
#define HAVE_INDEX 1
#define HAVE_RINDEX 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOK_R 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_STRTOLL 1
#define HAVE_STRTOULL 1
#define HAVE_STRTOD 1
#define HAVE_ATOI 1
#define HAVE_ATOF 1
#define HAVE_STRCMP 1
#define HAVE_STRNCMP 1
#define HAVE_VSSCANF 1
#define HAVE_VSNPRINTF 1
#define HAVE_ACOS 1
#define HAVE_ACOSF 1
#define HAVE_ASIN 1
#define HAVE_ASINF 1
#define HAVE_ATAN 1
#define HAVE_ATANF 1
#define HAVE_ATAN2 1
#define HAVE_ATAN2F 1
#define HAVE_CEIL 1
#define HAVE_CEILF 1
#define HAVE_COPYSIGN 1
#define HAVE_COPYSIGNF 1
#define HAVE_COS 1
#define HAVE_COSF 1
#define HAVE_EXP 1
#define HAVE_EXPF 1
#define HAVE_FABS 1
#define HAVE_FABSF 1
#define HAVE_FLOOR 1
#define HAVE_FLOORF 1
#define HAVE_FMOD 1
#define HAVE_FMODF 1
#define HAVE_ISINF 1
#define HAVE_ISNAN 1
#define HAVE_LOG 1
#define HAVE_LOGF 1
#define HAVE_LOG10 1
#define HAVE_LOG10F 1
#define HAVE_LROUND 1
#define HAVE_LROUNDF 1
#define HAVE_MODF 1
#define HAVE_MODFF 1
#define HAVE_POW 1
#define HAVE_POWF 1
#define HAVE_ROUND 1
#define HAVE_ROUNDF 1
#define HAVE_SCALBN 1
#define HAVE_SCALBNF 1
#define HAVE_SIN 1
#define HAVE_SINF 1
#define HAVE_SQRT 1
#define HAVE_SQRTF 1
#define HAVE_TAN 1
#define HAVE_TANF 1
#define HAVE_TRUNC 1
#define HAVE_TRUNCF 1
#define HAVE_NANOSLEEP 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_GMTIME_R 1
#define HAVE_LOCALTIME_R 1
#define HAVE_NL_LANGINFO 0
#undef HAVE_NL_LANGINFO
#define HAVE_SIGACTION 1
#define HAVE_SETJMP 1
#define HAVE_SYSCONF 1
#define HAVE_POLL 1
#define HAVE_ALLOCA 1
#define HAVE_GETPAGESIZE 1

/* sous-systemes */
#define SDL_LOADSO_DUMMY 1		/* tout est relie en statique */
#define SDL_THREAD_PTHREAD 1
#define SDL_THREAD_PTHREAD_RECURSIVE_MUTEX 1
#define SDL_TIMER_UNIX 1
#define SDL_TIME_UNIX 1
#define SDL_FILESYSTEM_UNIX 1
#define SDL_FSOPS_POSIX 1
#define SDL_PROCESS_DUMMY 1		/* pas de fork */
#define SDL_SENSOR_DUMMY 1
#define SDL_HAPTIC_DUMMY 1
#define SDL_HIDAPI_DISABLED 1
#define SDL_LOCALE_DUMMY 1
#define SDL_MISC_DUMMY 1
#define SDL_DIALOG_DUMMY 1
#define SDL_TRAY_DUMMY 1
#define SDL_CAMERA_DRIVER_DUMMY 1
#define SDL_GPU_DISABLED 1

/* les pilotes d'ARMPI : tools/sdl/armpi */
#define SDL_VIDEO_DRIVER_PRIVATE 1
#define SDL_VIDEO_DRIVER_DUMMY 1
#define SDL_AUDIO_DRIVER_PRIVATE 1
#define SDL_AUDIO_DRIVER_DUMMY 1
#define SDL_AUDIO_DRIVER_DISK 0
#undef SDL_AUDIO_DRIVER_DISK
#define SDL_JOYSTICK_PRIVATE 1
#define SDL_JOYSTICK_VIRTUAL 1
#define SDL_PRIVATE_GAMEPAD_DEFINITIONS

/* OpenGL ES par armgl (tools/mesa/armgl) ; pas de Vulkan, pas d'OpenGL de bureau */
#define SDL_VIDEO_OPENGL_ES2 1
#define SDL_VIDEO_RENDER_OGL_ES2 1

#endif
