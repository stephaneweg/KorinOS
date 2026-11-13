/*
 * armgl_none.c -- armgl pour le PC tant que GlKit n'y existe pas (docs/etude/PC-PILOTES.md, « GPU Intel » et
 * « Émulateurs d'Onyx, Pi et PC ») : pas de GPU, armgl_init rend ARMGL_NONE et le programme reste sur le processeur
 * (gcemu : le GX sur la machine, les images par soft.cpp). Lié par tools/pc/apps.sh à la place de libglkit.a ;
 * à retirer quand le pilote Intel et GlKit du PC seront là (étape 4 de GPU-INTEL.md).
 */
#include <stddef.h>
#include "armgl.h"

int armgl_init (int flags) { (void) flags; return ARMGL_NONE; }
void *armgl_context (int major, int minor, void *share) { (void) major; (void) minor; (void) share; return NULL; }
void *armgl_surface (int width, int height) { (void) width; (void) height; return NULL; }
void *armgl_window (long win, int x, int y, int width, int height) { (void) win; (void) x; (void) y; (void) width; (void) height; return NULL; }
void armgl_window_free (void *surface) { (void) surface; }
int armgl_make_current (void *context, void *surface) { (void) context; (void) surface; return 0; }
int armgl_swap (void *surface) { (void) surface; return 1; }
void *armgl_proc (const char *name) { (void) name; return NULL; }
void armgl_finish (void) {}
