/*
 * sdl3table.c -- sdl2-compat cherche SDL 3 par dlopen et dlsym ; ARMPI relie tout en statique. Les symboles de
 * SDL 3 sont renommes SDL3R_* dans libSDL3r.a, car SDL 2 exporte les memes noms ; cette table les rend par nom.
 * Compile avec -Ddlopen=... -Ddlsym=... -Ddlclose=... sur sdl2_compat.c : celui-ci n'est pas retouche.
 */
#include <string.h>

/* (sdl3_syms.h sert les memes macros dans ce fichier-ci que dans sdl2-compat ; les types n'y comptent pas) */
#define SDL3_SYM(rc, fn, params, args, ret) extern char SDL3R_##fn[];
#define SDL3_SYM_RENAMED(rc, oldfn, newfn, params, args, ret) SDL3_SYM (rc, newfn, params, args, ret)
#define SDL3_SYM_VARARGS(rc, fn, params) SDL3_SYM (rc, fn, params, unused, unused)
#include "sdl3_names.h"
#undef SDL3_SYM

struct entry { const char *name; void *fn; };

#define SDL3_SYM(rc, fn, params, args, ret) { "SDL_" #fn, SDL3R_##fn },
static const struct entry table[] = {
#include "sdl3_names.h"
};

void *armpi_sdl3_open (const char *name, int flags)
{
	return (void *) table;
}

void *armpi_sdl3_sym (void *lib, const char *name)
{
	for (unsigned i = 0; i < sizeof table / sizeof table[0]; i++)
		if (!strcmp (table[i].name, name))
			return table[i].fn;
	return 0;
}

int armpi_sdl3_close (void *lib)
{
	return 0;
}
