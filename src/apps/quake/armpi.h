/* armpi.h -- ce que se partagent main.c, vid.c, snd.c et in.c */
#ifndef ARMPI_QUAKE_H
#define ARMPI_QUAKE_H

#define ARMPI_MAXW 800
#define ARMPI_MAXH 600
#define ARMPI_NRES 3
#define ARMPI_RATE 44100
#define ID_RES 100		/* View : 100, 101, 102 */

void armpi_present (unsigned *px, long pitch);
void armpi_video_set (int mode);
void armpi_video_apply (void);
long armpi_audio (void *ctx, short *lr, long frames);
void armpi_key (void *ctx, long code, long down, long ch, long mods);
void armpi_mouse (void *ctx, long kind, long x, long y, long buttons);
void armpi_pad (long buttons);

#endif
