/*
 * armpi_mucompat.h -- force dans chaque source de MuPDF (tools/pdf/mupdf.mk : -include) : le <sys/types.h> de newlib
 * definit "quad" comme "quad_t" (reste de BSD), or fz_stext_char a un membre "quad". Comme dans la construction d'Onyx.
 */
#ifndef ARMPI_MUCOMPAT_H
#define ARMPI_MUCOMPAT_H
#include <sys/types.h>
#include <time.h>
#undef quad
#endif
