//
// host.cpp -- Morpho sur l'hôte (Linux), sans Korin : le vrai main.cpp (sans sa boucle), UIKit et AudioKit en
// bouchons, le texte par FreeType avec les polices de Korin, GpuKit du PC au processeur (host_gk.c). Une image du jeu
// après un scénario (tools/morpho/run.sh shot) :
//
//   MORPHO_ARGS="stage=4 hour=11 new"   la ligne de commande du jeu
//   MORPHO_SECS=4                       le temps joué avant l'image (images de 1/30 s)
//   MORPHO_DO="1:feed 1.2:meal 3:info"  des gestes à des instants (s) : feed play clean heal lamp caress info meal
//                                       fruit treat close card throw wake menu (la barre des soins, la fiche...)
//   MORPHO_THEME=light                  le thème Givre (l'app force Onyx Noir : celui-ci par défaut)
//   MORPHO_W=960 MORPHO_H=660           la taille de la fenêtre ; MORPHO_MOUSE=x,y : le pointeur (survol)
//

#include "../../src/apps/morpho/main.cpp"

#include <ft2build.h>
#include FT_FREETYPE_H

extern "C" {
// ---- les bouchons qui comptent ----
int uikit_kit_open (void) { return 1; }
int audiokit_kit_open (void) { return 0; }
int gpukit_kit_open (void);
long ui_init (long v) { memset ((void *) v, 0, sizeof (struct ui_vars)); return 1; }
long ui_screen (long p) { long *s = (long *) p; s[0] = 1280; s[1] = 800; return 0; }
long ui_window_ex (long x, long y, long w, long h, long t, long f)
{
	(void) x; (void) y; (void) t; (void) f;
	ui.w = w; ui.h = h; ui.win = 1;
	ui.surf = (uint32_t *) calloc ((size_t) (w * h), 4);
	return 1;
}
long ui_tr (long s) { return s; }
static unsigned s_tok[TJ_SIZE / 4];
long ui_tokens (void) { return (long) s_tok; }
long ui_ink (long c)
{
	unsigned v = (unsigned) c;
	int l = (int) (((v >> 16) & 255) * 299 + ((v >> 8) & 255) * 587 + (v & 255) * 114) / 1000;
	return l > 150 ? 0x14171C : 0xFFFFFF;
}
long ui_ask (long a, long b, long c, long d) { (void) a; (void) b; (void) c; (void) d; return 0; }
long ui_message (long a, long b, long c) { (void) a; (void) b; (void) c; return 0; }
}

static FT_Library h_ft;
static FT_Face h_face[2];
static FT_Face face_of (long f)
{
	if (!h_ft)
	{
		const char *dir = getenv ("MORPHO_FONTS") ? getenv ("MORPHO_FONTS") : "sdcard/fonts";
		char p[512];
		FT_Init_FreeType (&h_ft);
		snprintf (p, sizeof p, "%s/Inter-Regular.ttf", dir);
		if (FT_New_Face (h_ft, p, 0, &h_face[0])) fprintf (stderr, "police absente : %s\n", p);
		snprintf (p, sizeof p, "%s/Inter-SemiBold.ttf", dir);
		if (FT_New_Face (h_ft, p, 0, &h_face[1])) h_face[1] = h_face[0];
	}
	return h_face[f == (long) ui.font_b ? 1 : 0];
}
static unsigned utf8_next (const unsigned char **ps)
{
	const unsigned char *p = *ps;
	unsigned c = *p++;
	if (c >= 0xF0) { c = (c & 7) << 18 | (p[0] & 63u) << 12 | (p[1] & 63u) << 6 | (p[2] & 63u); p += 3; }
	else if (c >= 0xE0) { c = (c & 15) << 12 | (p[0] & 63u) << 6 | (p[1] & 63u); p += 2; }
	else if (c >= 0xC0) { c = (c & 31) << 6 | (p[0] & 63u); p += 1; }
	*ps = p;
	return c;
}
static long h_text (const char *s, long x, long base, long c, long px, long f, int draw)
{
	FT_Face face = face_of (f);
	if (!face) return 0;
	FT_Set_Pixel_Sizes (face, 0, (FT_UInt) px);
	long pen = 0;
	for (const unsigned char *p = (const unsigned char *) s; *p; )
	{
		unsigned ch = utf8_next (&p);
		if (FT_Load_Char (face, ch, draw ? FT_LOAD_RENDER : FT_LOAD_DEFAULT)) continue;
		FT_GlyphSlot g = face->glyph;
		if (draw)
		{
			FT_Bitmap *bm = &g->bitmap;
			long gx = x + pen + g->bitmap_left, gy = base - g->bitmap_top;
			for (unsigned r = 0; r < bm->rows; r++)
				for (unsigned q = 0; q < bm->width; q++)
				{
					long X = gx + q, Y = gy + r;
					unsigned a = bm->buffer[r * bm->pitch + q];
					if (!a || X < 0 || Y < 0 || X >= ui.w || Y >= ui.h) continue;
					uint32_t *d = ui.surf + Y * ui.w + X;
					*d = ga::mix (*d, (uint32_t) c, (int) (a + (a >> 7)));
				}
		}
		pen += g->advance.x >> 6;
	}
	return pen;
}
extern "C" {
long ui_text_width (long s, long px, long f) { return h_text ((const char *) s, 0, 0, 0, px, f, 0); }
long ui_text (long s, long x, long base, long c, long px, long f, long fl) { (void) fl; h_text ((const char *) s, x, base, c, px, f, 1); return 0; }
}

static void tokens (bool dark)
{
	static const unsigned GIVRE[24] = {
		0x00F5F6F8, 0x00FFFFFF, 0x00ECEEF2, 0x00FFFFFF, 0x00FFFFFF, 0x00E2E5EA, 0x00B8BFC9, 0x0014171C, 0x004C5563, 0x005F6877, 0x002563D8, 0x00E4ECFB,
		0x001D4FB3, 0x002563D8, 0x00D93A3F, 0x001E8A5A, 0x00C4861C, 0x00F7F8FA, 0x00F5F6F8, 0x00E9EDF2, 0x00F5F6F8, 0xC4000000, 0, 0x00FFFFFF };
	static const unsigned NOIR[24] = {
		0x0015171B, 0x001B1E23, 0x00101216, 0x00262A31, 0x00262A31, 0x002A2E36, 0x00525966, 0x00ECEEF2, 0x00B0B6C0, 0x008B929D, 0x002F72DC, 0x001C2F4D,
		0x002560C0, 0x007DB4F2, 0x00D93A3F, 0x0046A758, 0x00F1B33B, 0x0015171B, 0x001B1E23, 0x001F2228, 0x0015171B, 0x40000000, 1, 0x00262A31 };
	memcpy (s_tok, dark ? NOIR : GIVRE, sizeof (unsigned) * 24);
	s_tok[TJ_ACCENT_INK / 4] = 0xFFFFFF;
	s_tok[TJ_HOVER / 4] = dark ? 0x30353E : 0xE9EEF6;
}

static int actOf (const char *s)
{
	static const struct { const char *n; int a; } T[] = {
		{ "feed", H_FEED }, { "play", H_PLAY }, { "clean", H_CLEAN }, { "heal", H_HEAL }, { "lamp", H_LAMP }, { "caress", H_CARESS },
		{ "info", H_INFO }, { "meal", H_MEAL }, { "fruit", H_FRUIT }, { "treat", H_TREAT }, { "close", H_CLOSE }, { "card", H_CARD },
		{ "wake", H_WAKE }, { "newegg", H_NEWEGG } };
	for (auto &x : T) if (!strcmp (s, x.n)) return x.a;
	return -1;
}

int main (int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "shot.ppm";
	tokens (!(getenv ("MORPHO_THEME") && !strcmp (getenv ("MORPHO_THEME"), "light")));	// (l'app force Onyx Noir)
	uikit_open (&ui);
	gpukit_kit_open ();
	int W = getenv ("MORPHO_W") ? atoi (getenv ("MORPHO_W")) : 960, H = getenv ("MORPHO_H") ? atoi (getenv ("MORPHO_H")) : 660;
	ui_window_ex (0, 0, W, H, 0, 0);
	// la ligne de commande du jeu
	char args[512]; snprintf (args, sizeof args, "morpho %s", getenv ("MORPHO_ARGS") ? getenv ("MORPHO_ARGS") : "new hour=11");
	for (char *q = args; *q; q++) if (*q == '_') *q = ' ';		// (les essais : « _ » pour une espace)
	char *av[32]; int ac = 0;
	for (char *p = strtok (args, " "); p && ac < 31; p = strtok (0, " ")) av[ac++] = p;
	app_init (ac, av);
	if (getenv ("MORPHO_ZOOM")) g_camZoom = (float) atof (getenv ("MORPHO_ZOOM"));
	if (getenv ("MORPHO_YAW")) g_camYaw = (float) atof (getenv ("MORPHO_YAW"));
	if (getenv ("MORPHO_HALF")) { g_scene.fixedSize = true; g_scene.half = atoi (getenv ("MORPHO_HALF")); }
	float secs = getenv ("MORPHO_SECS") ? (float) atof (getenv ("MORPHO_SECS")) : 3;
	// les gestes
	struct Do { float at; char what[32]; bool done; } dos[32]; int nd = 0;
	if (getenv ("MORPHO_DO"))
	{
		char b[512]; snprintf (b, sizeof b, "%s", getenv ("MORPHO_DO"));
		for (char *q = b; *q; q++) if (*q == '_') *q = ' ';
		for (char *p = strtok (b, " "); p && nd < 32; p = strtok (0, " "))
		{
			char *c = strchr (p, ':'); if (!c) continue;
			*c = 0; dos[nd].at = (float) atof (p); snprintf (dos[nd].what, sizeof dos[nd].what, "%s", c + 1); dos[nd].done = false; nd++;
		}
	}
	if (getenv ("MORPHO_MOUSE")) { int x = 0, y = 0; sscanf (getenv ("MORPHO_MOUSE"), "%d,%d", &x, &y); g_mx = x; g_my = y; }
	const float dt = 1.0f / 30;
	int frames = (int) (secs / dt);
	double sum = 0;
	for (int f = 0; f < frames; f++)
	{
		float tt = (float) f * dt;
		for (int i = 0; i < nd; i++)
			if (!dos[i].done && tt >= dos[i].at)
			{
				dos[i].done = true;
				int a = actOf (dos[i].what);
				if (a >= 0) action (a);
				else if (!strncmp (dos[i].what, "click=", 6))
				{
					// (un vrai clic : déplacement, appui, relâchement, par onMouse)
					int x = 0, y = 0; sscanf (dos[i].what + 6, "%d,%d", &x, &y);
					onMouse (0, x, y, 0); onMouse (1, x, y, 1); onMouse (2, x, y, 0);
					fprintf (stderr, "clic (%d, %d) : interface %d, repas ouvert %d, acte %d\n", x, y, g_hud.hit (x, y), g_hud.feedOpen, g_act.act);
				}
				else if (!strcmp (dos[i].what, "throw")) g_act.throwBall (g_scene.cam.eye + V3 (0, -0.4f, -0.5f), V3 (0.6f, 0, 0.4f));
				else if (!strcmp (dos[i].what, "stroke")) g_caressHold = 3;
				else if (!strcmp (dos[i].what, "evolve")) { g_pet.stageAge = g_pet.stageLen () - 0.01; }
				else if (!strcmp (dos[i].what, "sick")) { g_pet.sick = true; g_pet.meds = 2; }
				else if (!strcmp (dos[i].what, "poop")) { g_pet.poops++; }
				else if (!strcmp (dos[i].what, "hungry")) { g_pet.st[N_FOOD] = 12; g_pet.st[N_FUN] = 30; }
				else if (!strcmp (dos[i].what, "sleep")) { g_pet.asleep = true; }
				else if (!strcmp (dos[i].what, "gone")) { g_pet.gone = true; onPetEvent (EV_GONE); }
			}
		if (g_mx >= 0) g_hud.hover = g_hud.hit (g_mx, g_my);
		struct timespec a0, a1; clock_gettime (CLOCK_MONOTONIC, &a0);
		app_frame (dt, dt);
		if (getenv ("MORPHO_TRACE") && f % 15 == 0) fprintf (stderr, "t=%.1f act=%d actT=%.2f ball=%d held=%d (%.2f %.2f %.2f) pos=(%.2f %.2f) fun=%.0f att=%d rat=%d saut=%.2f\n", (double) tt, g_act.act, (double) g_act.actT, g_act.ballOn, g_act.ballHeld, (double) g_act.ball.x, (double) g_act.ball.y, (double) g_act.ball.z, (double) g_act.pos.x, (double) g_act.pos.z, (double) g_pet.st[N_FUN], g_act.catches, g_act.misses, (double) g_act.jumpT);
		clock_gettime (CLOCK_MONOTONIC, &a1);
		sum += (double) (a1.tv_sec - a0.tv_sec) * 1000 + (double) (a1.tv_nsec - a0.tv_nsec) / 1e6;
	}
	fprintf (stderr, "%d images, %.1f ms par image (3D : %.1f ms, %zu sommets, demi-taille %d)\n", frames, frames ? sum / frames : 0, (double) g_scene.lastMs, g_scene.dl.v.size (), g_scene.half);
	FILE *f = fopen (out, "wb");
	if (!f) return 1;
	fprintf (f, "P6\n%d %d\n255\n", W, H);
	for (int i = 0; i < W * H; i++) { uint32_t v = ui.surf[i]; unsigned char b[3] = { (unsigned char) (v >> 16), (unsigned char) (v >> 8), (unsigned char) v }; fwrite (b, 1, 3, f); }
	fclose (f);
	return 0;
}
