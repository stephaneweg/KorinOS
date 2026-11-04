/*
 * armgl.h -- OpenGL ES sur le GPU, sans EGL : un contexte, une surface hors ecran, et les fonctions GL
 * par armgl_proc. Bibliotheque statique (docs/MESA.md).
 */
#ifndef _ARMGL_H
#define _ARMGL_H

#ifdef __cplusplus
extern "C" {
#endif

#define ARMGL_NONE	0	/* pas de GPU (QEMU, no_gpu, GPU eteint) : a l'appelant de rester sur le CPU */
#define ARMGL_GPU	1
#define ARMGL_DRY	2	/* a blanc : Mesa et son compilateur tournent, les jobs ne partent pas */

/* flags, essais sans GPU : le mode a blanc en memoire ordinaire, ou sur les vrais tampons sous 1 Go */
#define ARMGL_F_DRY	1
#define ARMGL_F_DRY_BUF	2
#define ARMGL_F_V71	4	/* avec un mode a blanc : Mesa se croit sur le V3D 7.1 du Pi 5 */
int armgl_init (int flags);

/* contexte OpenGL ES <major>.<minor> ; share : un autre contexte ou NULL */
void *armgl_context (int major, int minor, void *share);
/* surface hors ecran RGBA8 avec profondeur et stencil ; c'est le tampon par defaut de GL */
void *armgl_surface (int width, int height);
/* surface dans une fenetre d'ARMPI, zone x, y, l, h de son contenu : le GPU y rend directement, Elegant
 * compose sans recopie. win : ui_vars.win. NULL : pas de memoire basse ou fenetre refusee */
void *armgl_window (long win, int x, int y, int width, int height);
/* la surface d'une fenetre rendue : elle ne doit plus etre le tampon courant */
void armgl_window_free (void *surface);
/* la zone montree de cette surface, dans la fenetre win (une fenetre redimensionnee, ou une autre fenetre) ;
 * le GL y dessine en coordonnees 0, 0, l, h, comme dans une surface de cette taille */
void armgl_window_zone (void *surface, long win, int x, int y, int width, int height);
/* surface sur <n> images en memoire basse et cachee, que le processeur lit ensuite : <px> recoit leurs adresses.
 * bytes : la place de chacune. Pour un programme qui garde ses tampons (n64emu : EmuKit) */
void *armgl_buffers (void **px, int n, unsigned long bytes, int width, int height, int pitch);
/* l'image <i> devient la cible ; ce que le processeur y a ecrit est vide avant que le GPU y touche */
void armgl_buffer_bind (void *surface, int i);
/* attend le GPU, et le processeur relira la memoire */
void armgl_buffer_done (void *surface);
int armgl_make_current (void *context, void *surface);
/* attend le GPU et montre l'image (la premiere attache la surface) ; le contenu du tampon suivant n'est pas garanti : tout redessiner */
int armgl_swap (void *surface);
/* temps moyens par image (ms) : attente du GPU, cache, rattachement */
void armgl_times (void *surface, double *finish, double *cache, double *attach);
void armgl_times_reset (void *surface);
/* une fonction GL, contexte courant ou non */
void *armgl_proc (const char *name);
/* attend le GPU (glFinish) */
void armgl_finish (void);
/* recopie la surface, ligne du haut d'abord, en 0xAARRGGBB (l'ordre d'une surface d'Elegant) */
int armgl_read (void *surface, void *pixels, int pitch);
/* une texture UIF R G B A faite ailleurs comme cible de Mesa : la zone -> un nom, le nom -> une EGLImage */
unsigned armgl_import (void *va, unsigned phys, unsigned bytes);
void *armgl_image (unsigned name, int width, int height, int stride);
void armgl_image_free (void *image);
/* 1 : un job a ete refuse ou n'a pas fini : a l'appelant de repasser sur le CPU */
int armgl_lost (void);
long armgl_jobs (void);
/* adresse physique du premier tampon du pilote (essais) */
unsigned armgl_first_phys (void);

#ifdef __cplusplus
}
#endif

#endif
