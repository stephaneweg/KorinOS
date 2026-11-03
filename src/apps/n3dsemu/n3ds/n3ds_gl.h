//
// n3ds/n3ds_gl.h -- le GPU de la 3DS sur Mesa : les images que le coeur enregistre (Machine::gpuDraw) dessinees par
// OpenGL ES 3.0. Sans GPU (QEMU), n3ds_gl_setup ne branche rien et le rendu logiciel du coeur fait tout.
//
#ifndef _n3ds_gl_h
#define _n3ds_gl_h

#include "n3ds/n3ds.h"

// -> 1 : le GPU dessine ; 0 : rendu logiciel
int n3ds_gl_setup (n3ds::Machine *m);
void n3ds_gl_quit (n3ds::Machine *m);
extern int n3ds_gl_dry;

#endif
