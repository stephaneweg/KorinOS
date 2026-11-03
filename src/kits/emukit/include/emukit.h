/*
 * emukit.h -- EmuKit depuis le C et le C++ (docs/EMULATEURS.md) : le descripteur d'un emulateur,
 * champ pour champ avec EMD_* (src/kits/emukit/emukit.inc), et les boutons de la manette.
 *
 *   static struct emu_desc d = { ... };
 *   int main (int argc, char **argv) { emukit_kit_open (); return emu_run ((long) &d, argc > 1 ? (long) argv[1] : 0); }
 */
#ifndef _ARMPI_EMUKIT_H
#define _ARMPI_EMUKIT_H

#include "emukit_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

struct emu_desc
{
	const char *name, *ext;
	long w, h, maxw, maxh;
	long fps_milli, rate;
	void *ctx;
	long (*load) (void *ctx, const unsigned char *rom, long size);
	void (*frame) (void *ctx, unsigned *pixels, long pitch, long buttons, long flags);
	long (*audio) (void *ctx, short *lr, long frames);
	void (*reset) (void *ctx);
	long (*save) (void *ctx, const void **data);
	void (*restore) (void *ctx, const void *data, long size);
	const char *menu;
	void (*cmd) (void *ctx, long id);
	void (*mouse) (void *ctx, long kind, long x, long y, long buttons);
	void **bufs;
	long skip;
	const char *error;
};

/* les arguments du programme, en une chaine pour emu_run : « [--full | --thumb sortie] rom » */
static inline long emu_args (int argc, char **argv)
{
	static char s[640];
	int n = 0;
	for (int i = 1; i < argc; i++)
	{
		for (const char *p = argv[i]; *p && n < (int) sizeof s - 2; ) s[n++] = *p++;
		if (i + 1 < argc) s[n++] = ' ';
	}
	s[n] = 0;
	return n ? (long) s : 0;
}

#ifdef __cplusplus
}
#endif
#endif
