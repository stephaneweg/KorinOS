/*
 * vid.c -- l'image : le moteur dessine en 8 bits, armpi_present la convertit dans le tampon d'EmuKit (0x00RRGGBB).
 * D'apres vid_null.c et vid_x.c d'id (GPL). Le menu Video et View > Video choisissent parmi trois tailles.
 */
#include "quakedef.h"
#include "d_local.h"
#include "armpi.h"

extern void M_Print (int cx, int cy, char *str);
extern void M_PrintWhite (int cx, int cy, char *str);
extern void M_DrawCharacter (int cx, int line, int num);
extern void M_Menu_Options_f (void);

static const struct { int w, h; const char *name; } res[ARMPI_NRES] =
{
	{ 320, 200, "320 x 200" }, { 640, 480, "640 x 480" }, { 800, 600, "800 x 600" }
};
static byte vid_buffer[ARMPI_MAXW * ARMPI_MAXH];
static short zbuffer[ARMPI_MAXW * ARMPI_MAXH];
static byte surfcache[8 * 1024 * 1024];
static unsigned xpal[256];
static int cursor;
static cvar_t vid_res = { "_vid_res", "0", true };

unsigned short d_8to16table[256];
unsigned d_8to24table[256];

void VID_SetPalette (unsigned char *p)
{
	int i;
	for (i = 0; i < 256; i++, p += 3) xpal[i] = p[0] << 16 | p[1] << 8 | p[2];
}
void VID_ShiftPalette (unsigned char *p) { VID_SetPalette (p); }

static void set_size (int w, int h)
{
	vid.width = vid.conwidth = w;
	vid.height = vid.conheight = h;
	vid.rowbytes = vid.conrowbytes = w;
	vid.buffer = vid.conbuffer = vid_buffer;
	vid.aspect = 1.0;		/* (EmuKit garde des pixels carres) */
	d_pzbuffer = zbuffer;
	D_FlushCaches ();		/* (les surfaces ne doivent plus pointer dans l'ancien cache) */
	D_InitCaches (surfcache, sizeof surfcache);
	vid.recalc_refdef = 1;
}

static void menu_draw (void)
{
	int i;
	M_Print (8 * 8, 32, "Video size");
	for (i = 0; i < ARMPI_NRES; i++)
		(i == (int) vid_res.value ? M_PrintWhite : M_Print) (10 * 8, 48 + i * 8, (char *) res[i].name);
	M_DrawCharacter (8 * 8, 48 + cursor * 8, 12 + ((int) (realtime * 4) & 1));
}
static void menu_key (int key)
{
	switch (key)
	{
	case K_ESCAPE: S_LocalSound ("misc/menu1.wav"); M_Menu_Options_f (); break;
	case K_UPARROW: S_LocalSound ("misc/menu1.wav"); if (--cursor < 0) cursor = ARMPI_NRES - 1; break;
	case K_DOWNARROW: S_LocalSound ("misc/menu1.wav"); if (++cursor >= ARMPI_NRES) cursor = 0; break;
	case K_ENTER: S_LocalSound ("misc/menu2.wav"); armpi_video_set (cursor); break;
	}
}

void VID_Init (unsigned char *palette)
{
	Cvar_RegisterVariable (&vid_res);
	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.numpages = 1;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *) vid.colormap + 2048));
	set_size (res[0].w, res[0].h);
	VID_SetPalette (palette);
	vid_menudrawfn = menu_draw;
	vid_menukeyfn = menu_key;
}
void VID_Shutdown (void) {}
void VID_Update (vrect_t *rects) {}
int VID_SetMode (int m, unsigned char *p) { return 1; }
void VID_HandlePause (qboolean p) {}
void D_BeginDirectRect (int x, int y, byte *b, int w, int h) {}
void D_EndDirectRect (int x, int y, int w, int h) {}

void armpi_video_set (int mode) { Cvar_SetValue ("_vid_res", mode); }

/* entre deux images : la taille voulue (menu ou config.cfg) */
void armpi_video_apply (void)
{
	int m = (int) vid_res.value;
	if (m < 0 || m >= ARMPI_NRES || (int) vid.width == res[m].w) return;
	set_size (res[m].w, res[m].h);
	Con_CheckResize ();
	Con_Clear_f ();
}

#if ARMPI_ASM
void qa_present (unsigned *px, long pitch, const byte *s, int rowbytes, int w, int h, const unsigned *pal);
void armpi_present (unsigned *px, long pitch)
{
	qa_present (px, pitch, vid.buffer, vid.rowbytes, vid.width, vid.height, xpal);
}
#else
void armpi_present (unsigned *px, long pitch)
{
	int x, y, w = vid.width, h = vid.height;
	const byte *s = vid.buffer;
	for (y = 0; y < h; y++, px = (unsigned *) ((byte *) px + pitch), s += vid.rowbytes)
		for (x = 0; x < w; x++) px[x] = xpal[s[x]];
}
#endif
