/*
 * appkit/appkit.h -- (ARMPI) ce que la bibliotheque media d'Onyx (src/extra/onyx-libs/av, compilee
 * avec -DAV_POSIX -DAV_KAPI_SOUND) prend a l'en-tete appkit.h d'Onyx : la sortie son du noyau
 * d'Onyx (kapi_sound_*). Sur ARMPI ces fonctions sont un flux PCM d'AudioKit
 * (tools/webkit/glue/av_sound_armpi.c) : le code d'Onyx reste tel quel.
 * Et ce que gpucomp d'Onyx (src/extra/onyx-libs/gpucomp) en prend : le GPU du noyau d'Onyx
 * (kapi gpu_*, v53-v72), sur ARMPI GpuKit (tools/webkit/glue/gpucomp_armpi.c), l'horloge, et
 * les threads de GPC_F_ASYNC (absents : gpucomp compose alors sur le thread appelant).
 */
#ifndef ARMPI_GLUE_APPKIT_H
#define ARMPI_GLUE_APPKIT_H

#ifdef __cplusplus
extern "C" {
#endif

#define SOUND_RATE 44100			/* la frequence du mixeur de sound (docs/SON.md) */

/* prend la sortie -> 1 (0 : pas de son) */
int kapi_sound_acquire (void);
/* taille des blocs, nombre de blocs -> latence en trames */
int kapi_sound_config (int frames, int blocks);
/* frequence, trames acceptees maintenant, 0 */
void kapi_sound_status (unsigned *rate, unsigned *free_frames, unsigned *other);
/* trames s16 stereo ecrites (sans attendre) */
int kapi_sound_write (const short *frames, unsigned n);
void kapi_sound_release (void);

/* ---- le GPU (memes structures que kapi d'Onyx, src/kits/gpukit/gpukit.inc) ---- */
struct kapi_gpu_vertex3
{
	float x, y, z, w;
	float s, t;
	unsigned char r, g, b, a;
	unsigned char r2, g2, b2, a2;
};
struct kapi_gpu_batch
{
	unsigned first, count;
	int texture;
	unsigned flags;
	float matrix[16];
};
struct kapi_gpu_frame
{
	unsigned *pixels;
	int w, h, stride;
	unsigned clear;
	unsigned flags;
};
#define KAPI_GPU_Z_ALWAYS	7
#define KAPI_GPU_B_NOZWRITE	(1u << 3)
#define KAPI_GPU_B_BLEND(m)	(((m) & 15) << 8)
#define KAPI_GPU_BLEND_NONE	0
#define KAPI_GPU_BLEND_ALPHA	1
#define KAPI_GPU_BLEND_ADD	2
#define KAPI_GPU_BLEND_MUL	3
#define KAPI_GPU_BLEND_PREMUL	4
#define KAPI_GPU_BLEND_MULCOL	5
#define KAPI_GPU_BLEND_UNDER	6
#define KAPI_GPU_BLEND_SCREEN	7
#define KAPI_GPU_BLEND_PLUS	8
#define KAPI_GPU_BLEND_RSUB	9
#define KAPI_GPU_BLEND_LIGHTEN	10
#define KAPI_GPU_BLEND_DSTIN	11
#define KAPI_GPU_BLEND_DSTOUT	12
#define KAPI_GPU_B_LINEAR	(1u << 12)
#define KAPI_GPU_B_WRAP_S(m)	(((m) & 3) << 13)
#define KAPI_GPU_B_WRAP_T(m)	(((m) & 3) << 15)
#define KAPI_GPU_WRAP_CLAMP	1
#define KAPI_GPU_B_NOMATRIX	(1u << 17)
#define KAPI_GPU_F_KEEP		(1u << 0)
#define KAPI_GPU_F_ALPHA	(1u << 1)
#define KAPI_WAIT_FOREVER	0xFFFFFFFFu

int kapi_abi_version (void);
unsigned kapi_clock_us (void);
int kapi_gpu_info (char *buf, unsigned cap);
int kapi_gpu_texture (int handle, const unsigned *pixels, int w, int h, int stride);
int kapi_gpu_texture_rect (int handle, int x, int y, int w, int h, const unsigned *pixels, int stride);
int kapi_gpu_render (const struct kapi_gpu_frame *f, const struct kapi_gpu_vertex3 *v, unsigned nv,
		     const struct kapi_gpu_batch *b, unsigned nb);
void *kapi_gpu_vbuf (unsigned bytes);
int kapi_event_create (int manual_reset, int initial);
int kapi_event_set (int h);
int kapi_event_wait (int h, unsigned timeout_ms);
int kapi_thread_create (int (*fn) (void *), void *arg, unsigned stack_size, const char *name);
int kapi_thread_join (int tid, unsigned timeout_ms, int *code);
int kapi_sync_close (int h);

#ifdef __cplusplus
}
#endif

#endif
