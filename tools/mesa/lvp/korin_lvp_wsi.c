/*
 * korin_lvp_wsi.c -- la présentation de la WSI de Korin (tools/mesa/wsi/wsi_common_korin.c) pour lavapipe, Vulkan
 * sur le processeur (pour essayer sous QEMU ce qui demande Vulkan : docs/etude/BLENDER.md, jalon 3).
 *
 * Avec anv, korin_wsi_present (faux libdrm, korin_i915.c) attache à la fenêtre le tampon linéaire de l'image, qui
 * est déjà une tranche d'un tampon partagé du noyau. Avec lavapipe, l'image est dans la mémoire du programme : on la
 * recopie dans un tampon partagé à nous (deux images, l'une à l'écran pendant qu'on remplit l'autre), puis la même
 * attache (WS_OP_SURFACE_ATTACH ; la première fois, BUF_GRANT à Elegant, dont le refus donne le pid) et
 * WS_OP_SURFACE_DAMAGE. Une copie par image : lent, mais lavapipe ne sert qu'aux essais.
 *
 * Copyright (c) 2026 Stéphane Wegener. MIT licence: Permission is hereby granted, free of
 * charge, to any person obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so, subject to the
 * following conditions: The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <armpi/syscall.h>

#define SYS_BUF_CREATE		0x01
#define SYS_BUF_GRANT		0x02
#define SYS_BUF_FREE		0x04
#define SYS_WS_CALL		0x51
#define WS_OP_SURFACE_ATTACH	25
#define WS_OP_SURFACE_DAMAGE	26
#define WS_SF_NOREDRAW		256
#define E_PERM			(-1)
#define BUF_VA_BASE		0x6800000000UL	/* (comme korin_i915.c : le tampon n est projeté à BASE + n * SLOT) */
#define BUF_SLOT		0x4000000UL
#define PAGE			4096UL
#define MAX_WIN			16

static long sys2 (long n, long a, long b) { return armpi_syscall (n, a, b, 0, 0, 0, 0); }

struct lvp_win {
	long window, buf;
	uint8_t *va;
	unsigned pitch, frame;
	int w, h, next;
};
static struct lvp_win wins[MAX_WIN];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static long attach (long window, long buf, int x, int y, int w, int h, unsigned pitch, long off)
{
	long msg[64];
	for (int i = 0; i < 2; i++) {
		memset (msg, 0, sizeof msg);
		msg[0] = WS_OP_SURFACE_ATTACH;
		msg[1] = window;
		msg[2] = buf;
		msg[3] = (long) x | (long) y << 32;
		msg[4] = (long) w | (long) h << 32;
		((uint32_t *) msg)[10] = 16;		/* longueur des données */
		((uint32_t *) msg)[12] = WS_SF_NOREDRAW;	/* WS_SF_XRGB8888 : B, G, R, X en mémoire */
		((uint32_t *) msg)[13] = pitch;
		msg[7] = off;
		long r = sys2 (SYS_WS_CALL, (long) msg, 0);
		if (r != E_PERM || buf < 0)
			return r;
		sys2 (SYS_BUF_GRANT, buf, msg[1]);	/* la réponse donne le pid d'Elegant */
	}
	return E_PERM;
}

/* la fenêtre et son tampon (refait si la taille change) ; verrou tenu */
static struct lvp_win *win_for (long window, int w, int h)
{
	struct lvp_win *f = NULL;
	for (int i = 0; i < MAX_WIN; i++) {
		if (wins[i].window == window && wins[i].buf >= 0 && wins[i].va)
			f = &wins[i];
	}
	if (f && f->w == w && f->h == h)
		return f;
	if (f) {
		attach (window, -1, 0, 0, 0, 0, 0, 0);
		sys2 (SYS_BUF_FREE, f->buf, 0);
		memset (f, 0, sizeof *f);
		f->buf = -1;
	} else {
		for (int i = 0; i < MAX_WIN && !f; i++)
			if (!wins[i].va)
				f = &wins[i];
		if (!f)
			return NULL;
	}
	unsigned pitch = ((unsigned) w * 4 + 63) & ~63U;
	unsigned frame = (unsigned) (((unsigned long) pitch * (unsigned) h + PAGE - 1) & ~(PAGE - 1));
	if (2UL * frame > BUF_SLOT || w <= 0 || h <= 0)
		return NULL;
	long buf = sys2 (SYS_BUF_CREATE, (long) (2UL * frame), 0);
	if (buf < 0)
		return NULL;
	f->window = window;
	f->buf = buf;
	f->va = (uint8_t *) (BUF_VA_BASE + (unsigned long) buf * BUF_SLOT);
	f->pitch = pitch;
	f->frame = frame;
	f->w = w;
	f->h = h;
	f->next = 0;
	return f;
}

__attribute__ ((visibility ("default"))) int korin_wsi_present (long window, int x, int y, int w, int h,
								 const void *va, unsigned pitch)
{
	pthread_mutex_lock (&lock);
	struct lvp_win *f = win_for (window, w, h);
	if (!f) {
		pthread_mutex_unlock (&lock);
		return -1;
	}
	uint8_t *dst = f->va + (unsigned long) f->next * f->frame;
	const uint8_t *src = va;
	for (int r = 0; r < h; r++)
		memcpy (dst + (unsigned long) r * f->pitch, src + (unsigned long) r * pitch, (unsigned long) w * 4);
	long rc = attach (window, f->buf, x, y, w, h, f->pitch, (long) f->next * f->frame);
	f->next ^= 1;
	pthread_mutex_unlock (&lock);
	if (rc >= 0) {
		long msg[64];
		memset (msg, 0, sizeof msg);
		msg[0] = WS_OP_SURFACE_DAMAGE;
		msg[1] = window;
		rc = sys2 (SYS_WS_CALL, (long) msg, 0);
	}
	return rc < 0 ? -1 : 0;
}

__attribute__ ((visibility ("default"))) void korin_wsi_detach (long window)
{
	pthread_mutex_lock (&lock);
	for (int i = 0; i < MAX_WIN; i++)
		if (wins[i].va && wins[i].window == window) {
			attach (window, -1, 0, 0, 0, 0, 0, 0);
			sys2 (SYS_BUF_FREE, wins[i].buf, 0);
			memset (&wins[i], 0, sizeof wins[i]);
		}
	pthread_mutex_unlock (&lock);
}
