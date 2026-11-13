/*
 * gpukit_gl.h -- GpuKit par le GPU sur Korin PC (docs/etude/PC-PILOTES.md « GPU Intel, étape 3 ») : libgpukit_gl.a
 * (gkgl.c), liée avec libglkit.a. Un kit ne peut pas porter Mesa (il lui faut la libc du programme) : c'est le
 * programme qui donne à GpuKit son rendu par GlKit.
 *
 *     gpukit_kit_open ();
 *     gk_gl_attach ();          // 1 : gk_draw, gk_texture... passent par le GPU Intel ; 0 : le processeur
 *
 * Sans GPU Intel (service intelgpu absent, QEMU) : rien n'est attaché, GpuKit dessine au processeur comme le Pi
 * sans GPU. KORIN_GPUKIT=soft : attaché quand même, sur softpipe (essais du chemin GL sous QEMU, lent).
 * Le contexte GL est celui du thread qui appelle gk_gl_attach : gk_draw doit être appelé depuis ce thread.
 */
#ifndef GPUKIT_GL_H
#define GPUKIT_GL_H
#ifdef __cplusplus
extern "C" {
#endif

long gk_gl_attach (void);
/* ce qui rend (texte), après gk_gl_attach */
const char *gk_gl_describe (void);

/* la table que GpuKit reçoit par gk_debug (GK_DBG_BACKEND, table, 0) : même disposition que gpukit.c */
#ifndef GK_DBG_BACKEND
#define GK_DBG_BACKEND (4)		/* (gpukit.inc) */
#endif
#define GK_BACKEND_VERSION 1
struct gk_backend {
	long version;
	long (*texture) (long t, long px, long w, long h, long pitch);
	long (*texture_rect) (long t, long x, long y, long w, long h, long px, long pitch);
	long (*draw) (long frame, long v, long n, long b, long nb, long texs);
	long (*info) (long *out);
};

#ifdef __cplusplus
}
#endif
#endif
