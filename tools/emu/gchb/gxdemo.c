/*
 * gxdemo.c -- un programme GameCube a nous (pas de libogc ni de devkitPPC : gcc-powerpc-linux-gnu d'Ubuntu,
 * tools/emu/gchb/build.sh), pour essayer gcemu sur les chemins d'un jeu sans jeu commercial :
 *
 *   1. l'ecran du choix 50 / 60 Hz comme un jeu PAL : le VI en PAL (625 lignes), une image dessinee par le
 *      processeur dans le XFB (deux bandes, celle du choix en clair), la manette lue par le SI (gauche / droite :
 *      le choix, A ou Start : valider ; sans manette, 60 Hz au bout de 3 s) ;
 *   2. le changement de mode du VI (NTSC 525 lignes pour 60 Hz), comme VIDEO_Configure ;
 *   3. le GX par la FIFO (le write-gather pipe) : effacement, triangles colores, un carre texture (RGB565, la
 *      texture change a chaque image), plusieurs configurations du TEV (1 a 4 etages : des programmes neufs a
 *      compiler pendant la partie), copie de l'EFB vers une texture puis vers le XFB, « draw done » attendu au PE
 *      (la machine attend le GX, qui attend le lecteur GL) ;
 *   4. le son par le DMA de l'AI (une onde carree, sans DSP) ;
 *   5. une ligne d'etat dans la memoire (0x80001000 : magie, images, mode, choix) que tools/emu/ref_gc lit.
 *
 * Tout par sondage (MSR.EE reste a 0) : les vecteurs d'exception de gcemu (HLE) sont des rfi.
 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;

#define R16(a) (*(volatile u16 *) (a))
#define R32(a) (*(volatile u32 *) (a))
#define WGP8 (*(volatile u8 *) 0xCC008000)
#define WGP16 (*(volatile u16 *) 0xCC008000)
#define WGP32 (*(volatile u32 *) 0xCC008000)
#define WGPF (*(volatile float *) 0xCC008000)

enum { VI = 0xCC002000, CP = 0xCC000000, PE = 0xCC001000, PI = 0xCC003000, DSP = 0xCC005000, SI = 0xCC006400,
       AI = 0xCC006C00 };
enum { W = 640, H = 480 };

/* la memoire : physiques (le GX et le VI), vues par 0x80000000 (cache) ou 0xC0000000 (sans cache) */
#define FIFO_PA 0x00200000u
#define FIFO_SIZE 0x00040000u
#define XFB_PA 0x00300000u		/* 640 x 480 YUYV : 600 Ko */
#define TEX_PA 0x00400000u		/* 64 x 64 RGB565 : 8 Ko */
#define COPY_PA 0x00410000u		/* l'EFB copie en texture, 128 x 128 RGB565 */
#define SND_PA 0x00420000u		/* 32 Ko de son */
#define STATUS ((volatile u32 *) 0xC0001000)

static inline void sync (void) { __asm__ volatile ("sync" ::: "memory"); }

/* le cache de donnees vers la memoire (dcbf), pour le GX et le VI qui lisent la memoire */
static void flush (u32 va, u32 n)
{
	for (u32 a = va & ~31u; a < va + n; a += 32) __asm__ volatile ("dcbf 0,%0" :: "r" (a) : "memory");
	sync ();
}

/* ---- le GX ---- */
static void bp (u32 v) { WGP8 = 0x61; WGP32 = v; }
static void cp (u8 reg, u32 v) { WGP8 = 0x08; WGP8 = reg; WGP32 = v; }
static void xf (u16 addr, u32 v) { WGP8 = 0x10; WGP32 = addr; WGP32 = v; }
static void xff (u16 addr, int n, const float *v)
{
	WGP8 = 0x10;
	WGP32 = (u32) (n - 1) << 16 | addr;
	for (int i = 0; i < n; i++) WGPF = v[i];
}
static void gx_flush (void) { for (int i = 0; i < 32; i++) WGP8 = 0; sync (); }	/* (des NOP : le pipe vide) */
static u32 fbits (float f) { union { float f; u32 u; } x; x.f = f; return x.u; }

static void fifo_init (void)
{
	R16 (CP + 0x02) = 0;				/* le GP arrete pendant la mise en place */
	R32 (PI + 0x0C) = FIFO_PA;
	R32 (PI + 0x10) = FIFO_PA + FIFO_SIZE;
	R32 (PI + 0x14) = FIFO_PA;
	R16 (CP + 0x20) = FIFO_PA & 0xFFFF; R16 (CP + 0x22) = FIFO_PA >> 16;
	R16 (CP + 0x24) = (FIFO_PA + FIFO_SIZE - 32) & 0xFFFF; R16 (CP + 0x26) = (FIFO_PA + FIFO_SIZE - 32) >> 16;
	R16 (CP + 0x34) = FIFO_PA & 0xFFFF; R16 (CP + 0x36) = FIFO_PA >> 16;
	R16 (CP + 0x38) = FIFO_PA & 0xFFFF; R16 (CP + 0x3A) = FIFO_PA >> 16;
	R16 (CP + 0x02) = 0x11;			/* GP lit, lie au processeur */
}

static void gx_setup (void)
{
	/* les sommets : position (3 flottants), couleur 0 (RGBA8), coordonnee 0 (2 flottants), en direct */
	cp (0x50, 1u << 9 | 1u << 13);
	cp (0x60, 1);
	cp (0x70, 1 | 4 << 1 | 1u << 13 | 5u << 14 | 1u << 21 | 4u << 22 | 1u << 30);
	cp (0x80, 0);
	cp (0x90, 0x80000000u);
	cp (0x30, 0); cp (0x40, 0);			/* les index des matrices : 0 */
	xf (0x1018, 0); xf (0x1019, 0);
	static const float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
	xff (0x0000, 12, id);				/* la matrice de position 0 : l'identite */
	/* la projection orthographique : x 0..640, y 0..480, z 0..-1 */
	float pr[7] = { 2.0f / W, -1.0f, -2.0f / H, 1.0f, -1.0f, -1.0f, 0 };
	WGP8 = 0x10; WGP32 = 6u << 16 | 0x1020;
	for (int i = 0; i < 6; i++) WGPF = pr[i];
	WGP32 = 1;					/* orthographique */
	float vp[6] = { W / 2, -H / 2, 16777215.0f, W / 2 + 342, H / 2 + 342, 16777215.0f };
	xff (0x101A, 6, vp);
	xf (0x1009, 1);					/* une couleur */
	xf (0x100E, 1); xf (0x1010, 1);			/* couleur 0 : celle du sommet, sans lumiere */
	xf (0x103F, 1);					/* une coordonnee de texture */
	xf (0x1040, 5u << 7);				/* tex0 -> coordonnee 0, ST */
	xf (0x1050, 0);
	bp (0x20u << 24 | 342 | 342u << 12);		/* le ciseau */
	bp (0x21u << 24 | (H - 1 + 342) | (u32) (W - 1 + 342) << 12);
	bp (0x59u << 24 | 171 | 171u << 10);
	bp (0x40u << 24 | 0);				/* pas de Z */
	bp (0x41u << 24 | 1u << 3 | 1u << 4);		/* couleur et alpha ecrits, pas de melange */
	bp (0x43u << 24 | 0);				/* RGB8_Z24 */
	bp (0xF3u << 24 | 7u << 16 | 7u << 19);	/* alpha : toujours */
	for (u32 i = 0; i < 4; i++)			/* les tables d'echange : l'identite */
	{
		bp ((0xF6u + 2 * i) << 24 | 0 | 1u << 2);
		bp ((0xF7u + 2 * i) << 24 | 2 | 3u << 2);
	}
	/* la texture 0 : 64 x 64 RGB565 a TEX_PA, lineaire, repetee */
	bp (0x80u << 24 | 1 | 1u << 2 | 1u << 4 | 4u << 5);
	bp (0x84u << 24 | 0);
	bp (0x88u << 24 | 63 | 63u << 10 | 4u << 20);
	bp (0x8Cu << 24 | 0 | 3u << 15 | 3u << 18);
	bp (0x94u << 24 | TEX_PA >> 5);
}

/* nst etages : TEX * RAS, puis des melanges avec les constantes ; variante : un etage de plus ou de moins */
static void tev (int nst, int textured, int variant)
{
	bp (0x00u << 24 | (u32) (textured ? 1 : 0) | 1u << 4 | (u32) (nst - 1) << 10);
	for (int s = 0; s < nst; s++)
	{
		u32 a, b, c, d;
		if (s == 0) { a = 15; b = textured ? 8 : 10; c = textured ? 10 : 15; d = 15; }	/* TEXC*RASC ou RASC */
		else { a = 0; b = (u32) (12 + (s + variant) % 3); c = 7; d = 15; }		/* PREV et une constante */
		if (variant & 1 && s == nst - 1) d = 0;
		bp ((0xC0u + 2 * (u32) s) << 24 | d | c << 4 | b << 8 | a << 12 | 1u << 19);
		bp ((0xC1u + 2 * (u32) s) << 24 | (s ? 0u : 5u) << 4 | 7u << 7 | 7u << 10 | 7u << 13 | 1u << 19);
		/* l'ordre : la carte 0 et la coordonnee 0 pour l'etage 0, la couleur 0 */
		if (!(s & 1)) bp ((0x28u + (u32) s / 2) << 24 | (s == 0 && textured ? 1u << 6 : 0) | 0u << 7 | 7u << 12 | 0u << 15 | 0u << 19);
	}
	/* les constantes (K0..K2) : registres de couleur, type constante */
	bp (0xE0u << 24 | 1u << 23 | 0x40 | 0x20u << 12);
	bp (0xE1u << 24 | 1u << 23 | 0x80 | 0x10u << 12);
	bp (0xE2u << 24 | 1u << 23 | 0x20 | 0x60u << 12);
	bp (0xE3u << 24 | 1u << 23 | 0x30 | 0x30u << 12);
}

static void vtx (float x, float y, u32 rgba, float s, float t)
{
	WGPF = x; WGPF = y; WGPF = -0.5f;
	WGP32 = rgba;
	WGPF = s; WGPF = t;
}

/* les images de la texture : un damier qui glisse */
static void tex_fill (int frame)
{
	volatile u16 *t = (volatile u16 *) (0x80000000u | TEX_PA);
	for (int ty = 0; ty < 16; ty++)		/* RGB565 : des tuiles de 4 x 4 */
		for (int tx = 0; tx < 16; tx++)
			for (int y = 0; y < 4; y++)
				for (int x = 0; x < 4; x++)
				{
					int px = tx * 4 + x + frame, py = ty * 4 + y;
					u16 c = ((px >> 3) ^ (py >> 3)) & 1 ? 0xF800 | (u16) (frame & 63) << 5 : 0x001F;
					t[((ty * 16 + tx) * 16) + y * 4 + x] = c;
				}
	flush (0x80000000u | TEX_PA, 64 * 64 * 2);
}

static void efb_copy (int toXfb, u32 pa, int w, int h, int clear)
{
	bp (0x49u << 24 | 0);
	bp (0x4Au << 24 | (u32) (w - 1) | (u32) (h - 1) << 10);
	bp (0x4Bu << 24 | pa >> 5);
	bp (0x4Du << 24 | (u32) (toXfb ? w * 2 : w * 2) >> 5);
	bp (0x4Eu << 24 | 0x100);
	bp (0x52u << 24 | 3 | (toXfb ? 1u << 14 : 4u << 4) | (clear ? 1u << 11 : 0));
}

static void draw_done (void)
{
	bp (0x45u << 24 | 2);
	gx_flush ();
	for (int guard = 0; guard < 10000000; guard++)	/* le PE dit « fini » */
		if (R16 (PE + 0x0A) & 8) break;
	R16 (PE + 0x0A) = 8 | 2;
}

static void gx_frame (int f)
{
	tex_fill (f);
	bp (0x4Fu << 24 | 0xFF00 | (u32) (f & 0x3F));	/* effacement : fond qui change */
	bp (0x50u << 24 | 0x2000 | 0x40);
	bp (0x51u << 24 | 0xFFFFFF);
	/* une configuration du TEV toutes les 90 images (12 en tout), puis on recommence */
	int k = (f / 90) % 12;
	tev (1 + k % 4, 1, k / 4);
	/* le carre texture qui tourne (deux triangles) */
	float cx = 320, cy = 240, r = 150;
	int a = f % 64;
	static const float cs[16] = { 1, .92f, .71f, .38f, 0, -.38f, -.71f, -.92f, -1, -.92f, -.71f, -.38f, 0, .38f, .71f, .92f };
	float c = cs[(a >> 2) & 15], s = cs[((a >> 2) + 12) & 15];
	float x[4], y[4];
	static const float qx[4] = { -1, 1, 1, -1 }, qy[4] = { -1, -1, 1, 1 };
	for (int i = 0; i < 4; i++) { x[i] = cx + r * (qx[i] * c - qy[i] * s); y[i] = cy + r * (qx[i] * s + qy[i] * c); }
	WGP8 = 0x90; WGP16 = 6;
	vtx (x[0], y[0], 0xFFFFFFFF, 0, 0); vtx (x[1], y[1], 0xFFFFFFFF, 2, 0); vtx (x[2], y[2], 0xFFFFFFFF, 2, 2);
	vtx (x[0], y[0], 0xFFFFFFFF, 0, 0); vtx (x[2], y[2], 0xFFFFFFFF, 2, 2); vtx (x[3], y[3], 0xFFFFFFFF, 0, 2);
	/* des triangles colores sans texture : une autre configuration */
	tev (1, 0, 0);
	WGP8 = 0x90; WGP16 = 3 * 40;
	for (int i = 0; i < 40; i++)
	{
		float bx = 20 + (float) ((i * 37 + f * 3) % 600), by = 20 + (float) ((i * 53) % 440);
		u32 col = 0xFF0000FFu >> (i % 3 * 8) | 0x80u;
		vtx (bx, by, col, 0, 0); vtx (bx + 20, by, col, 0, 0); vtx (bx + 10, by + 18, col, 0, 0);
	}
	/* un coin de l'EFB vers une texture (comme un jeu fait ses reflets), puis l'image vers le XFB */
	efb_copy (0, COPY_PA, 128, 128, 0);
	efb_copy (1, XFB_PA, W, H, 1);
	draw_done ();
}

/* ---- le VI ---- */
static void vi_mode (int pal)
{
	R16 (VI + 0x02) = 2;				/* remise a zero, comme VIDEO_Configure */
	R16 (VI + 0x02) = 0;
	R16 (VI + 0x00) = (u16) (H << 4 | 6);		/* 480 lignes actives */
	R16 (VI + 0x48) = (u16) ((W / 16) << 8 | (W * 2 / 32));
	R32 (VI + 0x1C) = 0x10000000u | XFB_PA >> 5;
	R32 (VI + 0x24) = 0x10000000u | XFB_PA >> 5;
	R16 (VI + 0x02) = (u16) ((pal ? 1u : 0u) << 8 | 4 | 1);	/* format, non entrelace, en marche */
}

/* une demi-image : la ligne du VI repasse en arriere */
static void vi_wait (void)
{
	u32 last = R16 (VI + 0x2C) & 0x7FF;
	for (int guard = 0; guard < 50000000; guard++)
	{
		u32 l = R16 (VI + 0x2C) & 0x7FF;
		if (l < last) return;
		last = l;
	}
}

/* ---- le XFB par le processeur : l'ecran du choix ---- */
static void xfb_fill (int choice, int blink)
{
	volatile u32 *p = (volatile u32 *) (0x80000000u | XFB_PA);
	/* YUYV : Y0 U Y1 V */
	const u32 dim = 0x40804080u, on = blink ? 0xA060A0C0u : 0xC040C0D0u, bg = 0x10801080u;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W / 2; x++)
		{
			u32 c = bg;
			if (y > 180 && y < 300)
			{
				if (x > 40 && x < 150) c = choice == 0 ? on : dim;	/* 50 Hz a gauche */
				else if (x > 170 && x < 280) c = choice == 1 ? on : dim;	/* 60 Hz a droite */
			}
			p[y * (W / 2) + x] = c;
		}
	flush (0x80000000u | XFB_PA, W * H * 2);
}

/* ---- la manette (SI, canal 0) ---- */
static u32 pad_read (void)
{
	R32 (SI + 0x00) = 0x00400300;			/* la commande de lecture */
	R32 (SI + 0x30) = 0x00F70280;			/* sondage du canal 0 */
	R32 (SI + 0x38) = 0x80000000u;		/* envoi */
	return R32 (SI + 0x04) >> 16;			/* boutons (Start 0x1000, A 0x0100, gauche 1, droite 2) */
}

/* ---- le son : DMA de l'AI ---- */
static void snd_start (void)
{
	volatile u16 *s = (volatile u16 *) (0x80000000u | SND_PA);
	for (int i = 0; i < 0x8000 / 2; i += 2) { u16 v = (i / 2 / 50) & 1 ? 0x1000 : 0xF000; s[i] = v; s[i + 1] = v; }
	flush (0x80000000u | SND_PA, 0x8000);
	R32 (AI + 0x00) = 1u << 6;			/* AICR : DMA a 32 kHz */
	R16 (DSP + 0x30) = SND_PA >> 16;
	R16 (DSP + 0x32) = SND_PA & 0xFFFF;
	R16 (DSP + 0x36) = 0x8000 | (0x8000 / 32);
}

void main (void)
{
	STATUS[0] = 0x47584430;				/* « GXD0 » */
	STATUS[1] = 0; STATUS[2] = 0; STATUS[3] = 0;
	/* 1. l'ecran du choix, en PAL */
	vi_mode (1);
	snd_start ();
	int choice = 1, frames = 0, prev = 0;
	for (;;)
	{
		xfb_fill (choice, (frames >> 4) & 1);
		vi_wait ();
		u32 b = pad_read ();
		if ((b & 1) && !(prev & 1)) choice = 0;
		if ((b & 2) && !(prev & 2)) choice = 1;
		prev = (int) b;
		frames++;
		STATUS[1] = (u32) frames; STATUS[2] = 1; STATUS[3] = (u32) choice;
		if (b & 0x1100) break;
		if (frames >= 150) break;			/* 3 s a 50 Hz : 60 Hz par defaut */
	}
	/* 2. le mode choisi */
	vi_mode (choice == 0);
	/* 3. le GX */
	fifo_init ();
	gx_setup ();
	gx_flush ();
	for (int f = 0;; f++)
	{
		gx_frame (f);
		vi_wait ();
		STATUS[1] = (u32) (frames + f); STATUS[2] = 3;
	}
}
