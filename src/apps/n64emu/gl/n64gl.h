/*
 * n64gl.h -- le rendu N64 par OpenGL ES 3.0 (docs/EMULATEURS.md). Le coeur reste en assembleur : il ne fait
 * que deposer sommets et lots (n64_gl.inc). Ce qui parle a GL est ici, pour etre rejoue sur le PC
 * (tools/emu/n64gl_replay.c, llvmpipe) comme sur le Pi (n64gl_armpi.c, armgl, v3d).
 *
 * Ces structures sont lues telles quelles en memoire de l'emulateur : memes tailles que GV_, GB_, TX_ et le
 * bloc de gpu_frame (n64emu.asm).
 */
#ifndef _N64GL_H
#define _N64GL_H
#include <stdint.h>

#define N64GL_MAX_TEX	256

struct n64gl_vert {			/* 32 octets */
	float x, y, z, w;		/* espace de decoupe */
	float s, t;			/* coordonnees du RDP, en texels, avant le decalage de la tuile */
	uint8_t r, g, b, a;		/* l'ombrage ; le brouillard du microcode est dans a */
	uint8_t unused[4];
};

struct n64gl_batch {			/* 80 octets */
	uint32_t first, count;
	int32_t tex0;			/* M_TEXS, -1 : pas lue */
	uint32_t flags;			/* GF_ de gfx_flags */
	uint32_t combH, combL;		/* G_SETCOMBINE */
	uint32_t omH, omL;		/* G_SETOTHERMODE */
	uint32_t prim, env, fog, blend;	/* R G B A, poids fort d'abord */
	int32_t tex1;
	uint32_t tile0, wrap0;		/* sl | tl << 12 | shifts << 24 | shiftt << 28 ; repli << 13, << 15 */
	uint32_t tile1, wrap1;
	uint32_t sc0, sc1;		/* ciseaux : x | y << 16 */
	uint32_t primLod;
};

struct n64gl_tex {			/* 32 octets */
	const uint32_t *px;		/* 0xAARRGGBB */
	int32_t w, h;
	uint64_t key;
	uint32_t lastUse;
	uint8_t dirty, pad[3];
};

struct n64gl_frame {
	void *px;			/* la cible (n64gl_armpi.c) */
	int32_t w, h, stride;
	uint32_t clear;			/* 0x00RRGGBB */
	int32_t gw, gh;			/* taille de l'image N64 */
	const struct n64gl_vert *v;
	int32_t nv, nb;
	const struct n64gl_batch *b;
	struct n64gl_tex *tex;
};

/* la table que recoit le coeur : n64emu.asm (gl_start) */
struct n64gl_api {
	long (*init) (void **bufs, long w, long h, long pitch);		/* 1 : Mesa sert ; bufs : les 3 images */
	long (*frame) (const struct n64gl_frame *f);			/* 0 : le GPU est perdu, plus de Mesa */
	void (*load) (const char *rom);					/* les programmes deja vus pour cette ROM */
	void (*command) (long n);					/* 1 : filtre de la console ou du GPU */
	long nosave;							/* 1 : l'emulateur n'ecrit pas de .sav */
	const void *rom;						/* --bench : sans fenetre */
	long romsize;
	long (*tick) (long frame, const uint32_t *px);			/* apres chaque image ; 0 : fini */
};

/* n64gl.c */
#define N64GL_F3PT	1		/* filtre a trois points en shader (sinon : bilineaire du TMU) */
int n64gl_start (void *(*proc) (const char *), int w, int h);		/* contexte courant ; 0 : refus */
void n64gl_render (const struct n64gl_frame *f);
void n64gl_filter (int on);
int n64gl_keys_load (const char *path);					/* programmes compiles d'avance -> nombre */
void n64gl_stats (int *programs, int *compiled, int *skipped);

/* pour les bancs d'essai : un lot donne, le texte des shaders ; clef : 4 mots */
void n64gl_batch_key (const struct n64gl_batch *b, uint32_t key[4]);
int n64gl_fs_source (const uint32_t key[4], char *out, int size);
#endif
