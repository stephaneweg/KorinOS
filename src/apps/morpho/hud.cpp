//
// hud.cpp -- Morpho : l'interface 2D (hud.h).
//
#include "hud.h"
#include "tr.h"

namespace mo {

using ga::Img;

// ==== le thème ========================================================================================================
static const unsigned *J () { return (const unsigned *) ui_tokens (); }
static unsigned tok (int off) { return J ()[off / 4] & 0xFFFFFF; }
static bool darkTheme () { return J ()[TJ_DARK / 4] != 0; }
// les couleurs des besoins (du jeu : les mêmes dans les deux thèmes)
static const unsigned NEED_C[N_COUNT] = { 0xF2994A, 0xEC5B8F, 0xF0BE2E, 0x45A8E6, 0x47BE78 };

void formatDur (double s, char *out, int cap)
{
	long v = (long) s;
	if (v >= 86400) snprintf (out, (size_t) cap, TR ("%ld j %ld h"), v / 86400, (v % 86400) / 3600);
	else if (v >= 3600) snprintf (out, (size_t) cap, TR ("%ld h %02ld"), v / 3600, (v % 3600) / 60);
	else if (v >= 60) snprintf (out, (size_t) cap, TR ("%ld min"), v / 60);
	else snprintf (out, (size_t) cap, TR ("%ld s"), v < 0 ? 0 : v);
}
void formatAge (double s, char *out, int cap) { formatDur (s, out, cap); }

// ==== les icônes ======================================================================================================
enum Icon { I_FOOD, I_FUN, I_ENERGY, I_CLEAN, I_HEALTH, I_BOWL, I_BALL, I_SOAP, I_PILL, I_BULB, I_MOON, I_HAND, I_INFO,
	    I_APPLE, I_CAKE, I_STAR, I_EGG, I_POOP, I_ZZZ, I_SUN };

static void heart (Img &im, float cx, float cy, float s, unsigned c, int a = 256)
{
	float xy[64];
	for (int i = 0; i < 32; i++)
	{
		float t = (float) i / 32 * 2 * PI, st = sinf (t);
		xy[i * 2] = cx + s * 16 * st * st * st / 17;
		xy[i * 2 + 1] = cy - s * (13 * cosf (t) - 5 * cosf (2 * t) - 2 * cosf (3 * t) - cosf (4 * t)) / 17;
	}
	im.poly (xy, 32, c, a);
}
static void star (Img &im, float cx, float cy, float s, unsigned c, int a = 256, float rot = 0, int pts = 5)
{
	float xy[40];
	for (int i = 0; i < pts * 2; i++)
	{
		float an = rot + (float) i / (float) (pts * 2) * 2 * PI - PI / 2, r = (i & 1) ? s * 0.45f : s;
		xy[i * 2] = cx + cosf (an) * r; xy[i * 2 + 1] = cy + sinf (an) * r;
	}
	im.poly (xy, pts * 2, c, a);
}
static void drop (Img &im, float cx, float cy, float s, unsigned c, int a = 256)
{
	float xy[48]; int n = 0;
	xy[n++] = cx; xy[n++] = cy - s;
	for (int i = 0; i <= 18; i++)
	{
		float an = -0.45f + (float) i / 18 * (PI + 0.9f);
		xy[n++] = cx + cosf (an) * s * 0.62f; xy[n++] = cy + s * 0.3f + sinf (an) * s * 0.62f;
	}
	im.poly (xy, n / 2, c, a);
}

static void icon (Img &im, int id, float cx, float cy, float s, unsigned c, float t)
{
	unsigned w = 0xFFFFFF;
	switch (id)
	{
	case I_FOOD: case I_APPLE:
		im.disc (cx - s * 0.26f, cy + s * 0.08f, s * 0.5f, id == I_APPLE ? 0xE5483C : c);
		im.disc (cx + s * 0.26f, cy + s * 0.08f, s * 0.5f, id == I_APPLE ? 0xE5483C : c);
		im.disc (cx, cy + s * 0.25f, s * 0.48f, id == I_APPLE ? 0xE5483C : c);
		im.line (cx, cy - s * 0.35f, cx + s * 0.08f, cy - s * 0.78f, s * 0.12f, 0x7A5432);
		im.ellipse (cx + s * 0.32f, cy - s * 0.62f, s * 0.24f, s * 0.12f, 0x58B04A);
		im.disc (cx - s * 0.3f, cy - s * 0.05f, s * 0.13f, w, 140);
		break;
	case I_FUN: heart (im, cx, cy + s * 0.05f, s * 0.95f, c); im.disc (cx - s * 0.35f, cy - s * 0.25f, s * 0.14f, w, 130); break;
	case I_ENERGY:
	{
		float xy[] = { cx + s * 0.15f, cy - s * 0.95f, cx - s * 0.55f, cy + s * 0.15f, cx - s * 0.05f, cy + s * 0.15f,
			       cx - s * 0.2f, cy + s * 0.95f, cx + s * 0.55f, cy - s * 0.2f, cx + s * 0.05f, cy - s * 0.2f };
		im.poly (xy, 6, c);
		break;
	}
	case I_CLEAN: drop (im, cx, cy, s * 0.95f, c); im.disc (cx - s * 0.22f, cy + s * 0.2f, s * 0.12f, w, 150); break;
	case I_HEALTH:
		im.rbox ((int) (cx - s * 0.28f), (int) (cy - s * 0.85f), (int) (s * 0.56f), (int) (s * 1.7f), (int) (s * 0.14f), c, c);
		im.rbox ((int) (cx - s * 0.85f), (int) (cy - s * 0.28f), (int) (s * 1.7f), (int) (s * 0.56f), (int) (s * 0.14f), c, c);
		break;
	case I_BOWL:
	{
		float xy[] = { cx - s * 0.95f, cy - s * 0.05f, cx + s * 0.95f, cy - s * 0.05f, cx + s * 0.6f, cy + s * 0.7f, cx - s * 0.6f, cy + s * 0.7f };
		for (int i = 0; i < 5; i++) im.disc (cx - s * 0.55f + (float) i * s * 0.27f, cy - s * 0.12f - (float) (i & 1) * s * 0.12f, s * 0.2f, 0xB0773F);
		im.poly (xy, 4, c);
		im.rect ((int) (cx - s * 0.95f), (int) (cy - s * 0.12f), (int) (s * 1.9f), (int) (s * 0.16f) + 1, ga::shade (c, 300));
		break;
	}
	case I_BALL:
		im.disc (cx, cy, s * 0.85f, 0xF2C230);
		im.ring (cx, cy, s * 0.55f, s * 0.2f, 0xE5483C);
		im.disc (cx, cy, s * 0.24f, 0x3D8BE0);
		im.disc (cx - s * 0.32f, cy - s * 0.35f, s * 0.14f, w, 150);
		break;
	case I_SOAP:
		im.ring (cx - s * 0.3f, cy + s * 0.25f, s * 0.5f, s * 0.12f, c);
		im.ring (cx + s * 0.42f, cy - s * 0.15f, s * 0.36f, s * 0.11f, c);
		im.ring (cx - s * 0.05f, cy - s * 0.62f, s * 0.24f, s * 0.09f, c);
		im.disc (cx - s * 0.48f, cy + s * 0.05f, s * 0.1f, c, 180);
		break;
	case I_PILL:
	{
		float a = -0.7f, ca = cosf (a), sa = sinf (a);
		float x0 = cx - ca * s * 0.5f, y0 = cy - sa * s * 0.5f, x1 = cx + ca * s * 0.5f, y1 = cy + sa * s * 0.5f;
		im.line (x0, y0, cx, cy, s * 0.62f, 0xE5483C);
		im.line (cx, cy, x1, y1, s * 0.62f, 0xF4F4F0);
		im.disc (x0 + s * 0.05f, y0 - s * 0.16f, s * 0.09f, w, 160);
		break;
	}
	case I_BULB:
		im.glow (cx, cy - s * 0.15f, s * 1.3f, 0x302808);
		im.disc (cx, cy - s * 0.2f, s * 0.62f, 0xFFD94A);
		im.rbox ((int) (cx - s * 0.3f), (int) (cy + s * 0.3f), (int) (s * 0.6f), (int) (s * 0.5f), (int) (s * 0.12f), 0x9AA3AE, 0x6E7782);
		im.disc (cx - s * 0.2f, cy - s * 0.4f, s * 0.14f, w, 170);
		break;
	case I_MOON: im.disc (cx, cy, s * 0.8f, c); break;	// (le croissant : l'appelant y découpe la couleur du fond)
	case I_HAND: heart (im, cx - s * 0.3f, cy + s * 0.15f, s * 0.6f, c); heart (im, cx + s * 0.38f, cy - s * 0.35f, s * 0.45f, ga::shade (c, 300)); break;
	case I_INFO:
		im.ring (cx, cy, s * 0.82f, s * 0.15f, c);
		im.disc (cx, cy - s * 0.4f, s * 0.13f, c);
		im.rbox ((int) (cx - s * 0.11f), (int) (cy - s * 0.15f), (int) (s * 0.22f) + 1, (int) (s * 0.68f), (int) (s * 0.1f), c, c);
		break;
	case I_CAKE:
		im.rbox ((int) (cx - s * 0.6f), (int) (cy + s * 0.05f), (int) (s * 1.2f), (int) (s * 0.7f), (int) (s * 0.1f), 0xF2C230, 0xD99A20);
		im.ellipse (cx, cy, s * 0.72f, s * 0.38f, 0xFFB8D0);
		im.ellipse (cx, cy - s * 0.3f, s * 0.48f, s * 0.3f, 0xFFC8DC);
		im.disc (cx, cy - s * 0.66f, s * 0.2f, 0xE02040);
		break;
	case I_STAR: star (im, cx, cy, s, c, 256, t); break;
	case I_EGG: im.ellipse (cx, cy + s * 0.1f, s * 0.62f, s * 0.82f, c); im.disc (cx - s * 0.2f, cy - s * 0.2f, s * 0.14f, w, 150); break;
	case I_POOP:
		im.ellipse (cx, cy + s * 0.5f, s * 0.8f, s * 0.32f, c);
		im.ellipse (cx, cy + s * 0.05f, s * 0.58f, s * 0.28f, ga::shade (c, 280));
		im.ellipse (cx, cy - s * 0.35f, s * 0.36f, s * 0.24f, ga::shade (c, 300));
		break;
	case I_ZZZ: ga::textC ((int) cx, (int) (cy + s * 0.45f), "Zz", c, (int) (s * 1.3f), true); break;
	case I_SUN:
		for (int i = 0; i < 8; i++) { float an = (float) i / 8 * 2 * PI; im.line (cx + cosf (an) * s * 0.6f, cy + sinf (an) * s * 0.6f, cx + cosf (an) * s * 0.95f, cy + sinf (an) * s * 0.95f, s * 0.14f, 0xF0BE2E); }
		im.disc (cx, cy, s * 0.45f, 0xF0BE2E);
		break;
	}
}

// ==== l'état ==========================================================================================================
void Hud::toast (const char *s, int ic)
{
	if (nt == 4) { memmove (&toasts[0], &toasts[1], sizeof toasts[0] * 3); nt = 3; }
	Toast &x = toasts[nt++];
	snprintf (x.text, sizeof x.text, "%s", s);
	x.t = 0; x.life = 4.5f; x.icon = ic;
}
void Hud::showCard (int c, const char *title, const char *text)
{
	card = c; cardT = 0;
	snprintf (cardTitle, sizeof cardTitle, "%s", title);
	snprintf (cardText, sizeof cardText, "%s", text);
}
void Hud::update (float dt)
{
	t += dt;
	for (int i = 0; i < nt; )
	{
		toasts[i].t += dt;
		if (toasts[i].t > toasts[i].life) { memmove (&toasts[i], &toasts[i + 1], sizeof toasts[0] * (size_t) (nt - i - 1)); nt--; }
		else i++;
	}
	if (card) cardT += dt;
	infoT = clampf (infoT + (infoOpen ? dt : -dt) * 5, 0, 1);
}
int Hud::hit (int x, int y) const
{
	for (int i = nh - 1; i >= 0; i--)
	{
		const Hit &q = hits[i];
		if (q.on && x >= q.x && y >= q.y && x < q.x + q.w && y < q.y + q.h) return q.act;
	}
	return H_NONE;
}
bool Hud::over (int x, int y) const
{
	for (int i = 0; i < nh; i++)
	{
		const Hit &q = hits[i];
		if (x >= q.x && y >= q.y && x < q.x + q.w && y < q.y + q.h) return true;
	}
	return false;
}

// ==== la mise en page =================================================================================================
// Le châssis : l'en-tête en haut (le nom, la fiche), la vue 3D encadrée au milieu, puis les jauges et la rangée des soins.
// Rien ne mord sur la vue, sauf ce qui parle de l'animal (messages, bulle) et le tiroir de la fiche.
static const int MARGIN = 12;
void Hud::layout (int W, int H)
{
	w = W; h = H;
	portrait = H > W * 1.12f && W < 760;
	if (portrait)
	{
		// en hauteur : l'en-tête, la vue, les jauges en deux colonnes, les soins en grille de 3 × 2
		compact = W < 400 || H < 560;
		k = clampf ((float) W / 460, 0.85f, 1.25f);
	}
	else
	{
		compact = W < 700 || H < 500;
		k = clampf (fminf ((float) W / 960, (float) H / 660), 0.85f, 1.3f);
	}
	auto K = [&] (float v) { return (int) (v * k + 0.5f); };
	int head = portrait ? K (58) : compact ? K (40) : K (58);
	gCols = portrait ? 2 : 5; gRows = portrait ? 3 : 1;
	gH = compact ? K (30) : portrait ? K (46) : K (50);
	int gauges = gRows * gH + (gRows - 1) * K (8);
	dCols = portrait ? 3 : 6; dRows = portrait ? 2 : 1;
	bH = portrait ? K (38) : compact ? K (46) : K (38);
	int dock = portrait ? 1 + K (12) * 2 + dRows * bH + (dRows - 1) * K (8) : compact ? K (64) : K (60);
	vx = MARGIN; vy = head;
	vw = W - 2 * MARGIN;
	vh = H - head - K (10) - gauges - K (10) - dock;
	if (vh < 80) vh = 80;
	gy = vy + vh + K (10);
	dy = gy + gauges + K (10);
}

// ==== le dessin =======================================================================================================
static unsigned cardFill () { return darkTheme () ? ga::mix (tok (TJ_SURFACE), tok (TJ_RAISED), 150) : tok (TJ_SURFACE); }
static void panel (Img &im, int x, int y, int w, int h, int r, int a = 250)
{
	bool dk = darkTheme ();
	for (int i = 3; i >= 1; i--) im.rbox (x - i + 1, y + i * 2 - 1, w + 2 * i - 2, h + 2 * i - 1, r + i, 0, 0, dk ? 40 : 18);
	unsigned s = cardFill ();
	im.rbox (x, y, w, h, r, s, s, a, tok (TJ_LINE));
}
static void pill (Img &im, int x, int y, int w, int h, unsigned bg, int a = 256) { im.rbox (x, y, w, h, h / 2, bg, bg, a); }
// ce qui est dans la vue (toujours sombre) : un verre fumé, quel que soit le thème
static const unsigned GLASS = 0x252931, GLASS_LINE = 0x3A3F49, GLASS_TEXT = 0xECEEF2, GLASS_DIM = 0xA8AFBA;
static void glassPill (Img &im, int x, int y, int w, int h, int a = 236)
{
	for (int i = 2; i >= 1; i--) im.rbox (x - i + 1, y + i * 2 - 1, w + 2 * i - 2, h + 2 * i - 1, h / 2 + i, 0, 0, 50);
	im.rbox (x, y, w, h, h / 2, GLASS, GLASS, a, GLASS_LINE);
}
// les coins arrondis de la vue : le châssis repeint au-delà du rayon (anticrénelé), puis un liseré
static void frameView (Img &im, int x, int y, int w, int h, int r, unsigned outer, unsigned line)
{
	for (int j = 0; j < r; j++)
		for (int i = 0; i < r; i++)
		{
			float dx = (float) (r - i) - 0.5f, dy = (float) (r - j) - 0.5f;
			float d = sqrtf (dx * dx + dy * dy);
			float out = clampf (d - (float) r + 0.5f, 0, 1);		// hors du coin
			float edge = clampf (1 - fabsf (d - (float) r + 1), 0, 1);	// le liseré
			int px[4] = { x + i, x + w - 1 - i, x + i, x + w - 1 - i }, py[4] = { y + j, y + j, y + h - 1 - j, y + h - 1 - j };
			for (int c = 0; c < 4; c++)
			{
				if (edge > 0) im.put (px[c], py[c], line, (int) (edge * 256));
				if (out > 0) im.put (px[c], py[c], outer, (int) (out * 256));
			}
		}
	im.rect (x + r, y, w - 2 * r, 1, line); im.rect (x + r, y + h - 1, w - 2 * r, 1, line);
	im.rect (x, y + r, 1, h - 2 * r, line); im.rect (x + w - 1, y + r, 1, h - 2 * r, line);
}

void Hud::draw (Img &im, const Pet &p, const Actor &a, const Camera &cam, const Light &L)
{
	(void) L;
	auto K = [&] (float v) { return (int) (v * k + 0.5f); };
	nh = 0;
	auto addHit = [&] (int act, int x, int y, int ww, int hh, bool on) { if (nh < 32) hits[nh++] = { act, x, y, ww, hh, on }; };
	unsigned T1 = tok (TJ_TEXT1), T2 = tok (TJ_TEXT2), T3 = tok (TJ_TEXT3), ACC = tok (TJ_ACCENT);
	unsigned CH = darkTheme () ? tok (TJ_SURFACE) : tok (TJ_FACE);			// le châssis
	unsigned CARD = cardFill (), LINE = tok (TJ_LINE);
	char b[256], b2[128];
	bool egg = p.g.stage == ST_EGG;
	const int R = K (14);

	// ---- le châssis autour de la vue, ses coins arrondis
	im.rect (0, 0, w, vy, CH); im.rect (0, vy + vh, w, h - vy - vh, CH);
	im.rect (0, vy, vx, vh, CH); im.rect (vx + vw, vy, w - vx - vw, vh, CH);
	frameView (im, vx, vy, vw, vh, R, CH, darkTheme () ? 0x2C313A : ga::mix (LINE, tok (TJ_LINE_STRONG), 128));

	// ---- dans la vue : les particules, l'éclair, la bulle
	im.clip (vx + 1, vy + 1, vx + vw - 1, vy + vh - 1);
	for (const Particle &q : a.parts)
	{
		if (q.kind == P_BUBBLE) continue;
		float sx, sy, d;
		if (!cam.project (q.p, sx, sy, &d)) continue;
		sx += (float) vx; sy += (float) vy;
		float sc = k * clampf (3.2f / d, 0.45f, 1.7f);
		float life = q.life / q.max;
		int al = (int) (256 * clampf (life * 2.5f, 0, 1));
		switch (q.kind)
		{
		case P_HEART: heart (im, sx, sy, 9 * sc * q.size * (1.15f - 0.3f * life), q.col, al); break;
		case P_NOTE:
			im.disc (sx - 3 * sc, sy + 5 * sc, 4 * sc, q.col, al);
			im.line (sx + 0.5f * sc, sy + 5 * sc, sx + 0.5f * sc, sy - 8 * sc, 1.6f * sc, q.col, al);
			im.line (sx + 0.5f * sc, sy - 8 * sc, sx + 6 * sc, sy - 5 * sc, 2 * sc, q.col, al);
			break;
		case P_STAR: star (im, sx, sy, 7 * sc * q.size, q.col, al, q.spin * (1 - life) * 3); break;
		case P_SPARK: im.glow (sx, sy, 10 * sc * q.size, ga::shade (q.col, 140)); im.disc (sx, sy, 2 * sc, q.col, al); break;
		case P_Z:
		{
			if (!inView ((int) sx, (int) sy - 10)) break;
			int px = (int) (10 * sc + (1 - life) * 9 * sc);
			ga::text ((int) sx, (int) sy, "z", ga::mix (0x0E1015, 0xDDE6FF, al), px, true);
			break;
		}
		case P_DROP: drop (im, sx, sy, 5 * sc * q.size, q.col, al); break;
		case P_CRUMB: im.disc (sx, sy, 2.2f * sc * q.size, q.col, al); break;
		case P_SHELL:
		{
			float r = 7 * sc * q.size, an = q.spin * (1 - life) * 6;
			float xy[6] = { sx + cosf (an) * r, sy + sinf (an) * r, sx + cosf (an + 2.3f) * r * 0.8f, sy + sinf (an + 2.3f) * r * 0.8f, sx + cosf (an + 4.1f) * r * 0.9f, sy + sinf (an + 4.1f) * r * 0.9f };
			im.poly (xy, 3, q.col, al);
			break;
		}
		case P_STINK:
		{
			float ph = (1 - life) * 6;
			for (int s = 0; s < 3; s++)
			{
				float y0 = sy + (float) s * 5 * sc, x0 = sx + sinf (ph + (float) s) * 4 * sc;
				im.line (x0, y0, sx + sinf (ph + (float) s + 1) * 4 * sc, y0 - 5 * sc, 1.6f * sc, q.col, al * 2 / 3);
			}
			break;
		}
		default: break;
		}
	}
	if (a.flash > 0.01f) im.rect (vx, vy, vw, vh, 0xFFFFFF, (int) (a.flash * 200));
	int need = p.worst ();
	if (need >= 0 && a.act == A_IDLE && !p.asleep && !a.ballOn && infoT < 0.5f)
	{
		float sx, sy;
		if (cam.project (a.out.top, sx, sy))
		{
			sx += (float) vx; sy += (float) vy;
			float bob = sinf (t * 3) * 3 * k;
			int Rb = K (16);
			float cy = sy - K (24) + bob;
			float top = (float) (vy + K (12) + (nt ? K (40) : 0) + Rb);	// (sous les messages, dans la vue)
			if (cy < top) { cy = top; sx += (float) K (46); }
			im.disc (sx, cy + 2, (float) Rb + 2, 0, 70);
			im.disc (sx, cy, (float) Rb + 1, GLASS_LINE);
			im.disc (sx, cy, (float) Rb, GLASS);
			float tri[6] = { sx - Rb * 0.35f, cy + Rb * 0.82f, sx + Rb * 0.15f, cy + Rb * 0.82f, sx - Rb * 0.32f, cy + Rb * 1.42f };
			im.poly (tri, 3, GLASS);
			int ic = need == N_FOOD ? I_FOOD : need == N_FUN ? I_FUN : need == N_ENERGY ? I_ENERGY : need == N_CLEAN ? (p.poops >= 2 ? I_POOP : I_CLEAN) : I_HEALTH;
			unsigned c = need == N_CLEAN && p.poops >= 2 ? 0xB07A48 : NEED_C[need];
			icon (im, ic, sx, cy, (float) Rb * 0.6f, c, t);
			if (p.sick && need == N_HEALTH) im.disc (sx + Rb * 0.72f, cy - Rb * 0.72f, (float) Rb * 0.28f, tok (TJ_DANGER));
		}
	}
	im.noClip ();

	// ---- dans la vue : les messages en haut, ce qui se passe en bas
	{
		int y = vy + K (12);
		for (int i = 0; i < nt && i < 2; i++)
		{
			const Toast &q = toasts[i];
			float in = smooth (0, 0.25f, q.t), out = 1 - smooth (q.life - 0.5f, q.life, q.t);
			float al = fminf (in, out);
			int th = K (32);
			int tw = ga::textW (q.text, K (13), true) + K (32) + (q.icon >= 0 ? K (20) : 0);
			int x = vx + (vw - tw) / 2, yy = y - (int) ((1 - in) * (float) K (10));
			glassPill (im, x, yy, tw, th, (int) (al * 236));
			int tx = x + K (16);
			if (q.icon >= 0) { icon (im, q.icon, (float) (tx + K (7)), (float) (yy + th / 2), (float) K (7), q.icon < N_COUNT ? NEED_C[q.icon] : ACC, t); tx += K (20); }
			if (al > 0.4f) ga::text (tx, yy + K (21), q.text, ga::mix (GLASS, GLASS_TEXT, (int) (al * 256)), K (13), true);
			y += K (40);
		}
		const char *hint = 0;
		if (p.gone) hint = 0;
		else if (p.asleep) hint = p.lampOff ? TR ("Chut… il dort. Zzz") : TR ("Il dort : éteignez la lumière pour qu'il se repose mieux.");
		else if (a.ballOn) { snprintf (b2, sizeof b2, TR ("Cliquez dans la scène pour lancer la balle · attrapées : %d"), a.catches); hint = b2; }
		else if (egg && a.act != A_HATCH) hint = TR ("L'œuf frémit… il va bientôt éclore.");
		if (hint && !(feedOpen && !compact))
		{
			int tw = ga::textW (hint, K (12), false) + K (28), th = K (28);
			if (tw > vw - K (20)) tw = vw - K (20);
			int x = vx + (vw - tw) / 2, y2 = vy + vh - th - K (12);
			glassPill (im, x, y2, tw, th, 220);
			ga::textC (vx + vw / 2, y2 + K (19), hint, GLASS_DIM, K (12), false);
		}
	}

	// ---- l'en-tête : le nom, le stade, le caractère ; la fiche à droite (avant les boutons de la fenêtre)
	{
		int x = MARGIN + K (6);
		int base = portrait ? K (35) : K (27);
		int nsz = compact ? 16 : portrait ? 19 : 18;
		ga::text (x, base, p.name, T1, K ((float) nsz), true);
		int nx = x + ga::textW (p.name, K ((float) nsz), true) + K (10);
		const char *sn = p.gone ? TR ("Parti") : stageName (p.g.stage);
		int pw = ga::textW (sn, K (11), true) + K (18), ph = K (20);
		pill (im, nx, base - K (15), pw, ph, ga::mix (CH, ACC, darkTheme () ? 70 : 40));
		ga::text (nx + K (9), base - K (1), sn, darkTheme () ? tok (TJ_ACCENT_VIVID) : tok (TJ_ACCENT_DARK), K (11), true);
		nx += pw + K (8);
		if (p.speed > 1)
		{
			snprintf (b, sizeof b, "×%d", p.speed);
			int sw = ga::textW (b, K (11), true) + K (14);
			pill (im, nx, base - K (15), sw, ph, ga::mix (CH, tok (TJ_WARNING), 60));
			ga::text (nx + K (7), base - K (1), b, T1, K (11), true);
		}
		// la fiche : avec son nom s'il y a la place, sinon son icône seule
		int bh = K (32), bwFull = ga::textW (TR ("Fiche"), K (13), true) + K (44);
		int right = w - FRAME_CAP_W - MARGIN;
		bool label = !compact && right - bwFull > nx + K (8) + (portrait ? ga::textW (traitName (p.g.trait), K (11), true) + K (26) : 0);
		int bw = label ? bwFull : bh;
		if (portrait && p.g.stage != ST_EGG && !p.gone)
		{
			// (le caractère, en pastille neutre, s'il reste de la place)
			const char *tn = traitName (p.g.trait);
			int tw = ga::textW (tn, K (11), true) + K (18);
			if (nx + tw < right - bw - K (8))
			{
				im.rbox (nx, base - K (15), tw, ph, ph / 2, CARD, CARD, 256, LINE);
				ga::text (nx + K (9), base - K (1), tn, T2, K (11), true);
				nx += tw + K (8);
			}
		}
		else if (!compact && !portrait)
		{
			formatAge (p.age, b2, sizeof b2);
			if (egg) snprintf (b, sizeof b, TR ("Génération %d · éclosion bientôt"), p.generation);
			else snprintf (b, sizeof b, "%s · %s · %s %d", traitName (p.g.trait), b2, TR ("génération"), p.generation);
			im.disc ((float) (x + K (5)), (float) (base + K (14)), (float) K (4), T3);
			ga::text (x + K (14), base + K (18), b, T3, K (12), false);
		}
		int bx = right - bw, by = (vy - bh) / 2;
		if (bx > nx)
		{
			bool hov = hover == H_INFO, sel = infoOpen;
			unsigned bg = sel ? ga::mix (CARD, ACC, 60) : hov ? tok (TJ_HOVER) : CARD;
			im.rbox (bx, by, bw, bh, K (8), bg, bg, 256, sel ? ACC : LINE);
			float icx = (float) (bx + (label ? K (18) : bw / 2)), icy = (float) (by + bh / 2);
			// (une feuille au coin plié)
			float fw = (float) K (6), fh = (float) K (8);
			float sheet[10] = { icx - fw, icy - fh, icx + fw * 0.35f, icy - fh, icx + fw, icy - fh * 0.45f, icx + fw, icy + fh, icx - fw, icy + fh };
			im.poly (sheet, 5, T2);
			im.rect ((int) (icx - fw * 0.55f), (int) (icy - fh * 0.15f), (int) (fw * 1.1f), 1 + (int) k, CARD);
			im.rect ((int) (icx - fw * 0.55f), (int) (icy + fh * 0.35f), (int) (fw * 1.1f), 1 + (int) k, CARD);
			if (label) ga::text (bx + K (32), by + K (21), TR ("Fiche"), T1, K (13), true);
			addHit (H_INFO, bx, by, bw, bh, true);
		}
	}

	// ---- les jauges, en cartes sous la vue
	{
		int gh = gH;
		int total = vw > K (900) ? K (900) : vw, gx = vx + (vw - total) / 2;
		static const char *const NAMES[N_COUNT] = { "Satiété", "Bonheur", "Énergie", "Propreté", "Santé" };
		static const int ICONS[N_COUNT] = { I_FOOD, I_FUN, I_ENERGY, I_CLEAN, I_HEALTH };
		auto bar = [&] (int x, int y, int bw, float v, unsigned c, bool pulse) {
			int bh = K (6);
			im.rbox (x, y, bw, bh, bh / 2, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
			int fw = (int) ((float) bw * clampf (v, 0, 100) / 100);
			int al = pulse ? (int) (170 + 86 * sinf (t * 6)) : 256;
			if (fw >= bh) im.rbox (x, y, fw, bh, bh / 2, c, c, al);
		};
		if (egg || p.gone)
		{
			int gy = this->gy;
			im.rbox (gx, gy, total, gh, K (10), CARD, CARD, 256, LINE);
			float f = p.gone ? 1 : sat (p.stageProgress ());
			if (!compact)
			{
				icon (im, I_EGG, (float) (gx + K (20)), (float) (gy + K (17)), (float) K (8), 0xF4EEDF, t);
				ga::text (gx + K (34), gy + K (22), p.gone ? TR ("Parti") : TR ("Éclosion"), T1, K (12), true);
				snprintf (b, sizeof b, "%d %%", (int) (f * 100));
				ga::textR (gx + total - K (14), gy + K (22), b, ACC, K (12), true);
				bar (gx + K (14), gy + K (32), total - K (28), f * 100, ACC, false);
			}
			else bar (gx + K (12), gy + gh / 2 - K (3), total - K (24), f * 100, ACC, false);
		}
		else
		{
			int gap = K (8), cw = (total - (gCols - 1) * gap) / gCols;
			for (int i = 0; i < N_COUNT; i++)
			{
				int x = gx + (i % gCols) * (cw + gap), gy = this->gy + (i / gCols) * (gh + gap);
				float v = p.st[i];
				bool low = v < 25 || (i == N_HEALTH && p.sick);
				unsigned c = NEED_C[i], vc = low ? tok (TJ_DANGER) : c;
				im.rbox (x, gy, cw, gh, K (10), CARD, CARD, 256, low ? ga::mix (LINE, tok (TJ_DANGER), 140) : LINE);
				if (compact)
				{
					icon (im, ICONS[i], (float) (x + K (13)), (float) (gy + gh / 2), (float) K (6), c, t);
					bar (x + K (24), gy + gh / 2 - K (3), cw - K (32), v, vc, low);
					continue;
				}
				icon (im, ICONS[i], (float) (x + K (17)), (float) (gy + K (17)), (float) K (7), c, t);
				const char *nm = TR (NAMES[i]);
				bool roomy = cw > K (128);
				if (roomy || ga::textW (nm, K (12), true) < cw - K (70)) ga::text (x + K (30), gy + K (22), nm, T1, K (12), true);
				if (i == N_HEALTH && p.sick) snprintf (b, sizeof b, "%s", TR ("Malade"));
				else snprintf (b, sizeof b, "%d %%", (int) (v + 0.5f));
				ga::textR (x + cw - K (12), gy + K (22), b, vc, K (12), true);
				bar (x + K (12), gy + K (32), cw - K (24), v, vc, low);
			}
		}
		addHit (H_NONE, gx, gy, total, dy - gy, true);
	}

	// ---- la rangée des soins
	if (!p.gone)
	{
		im.rect (0, dy, w, 1, LINE);
		struct B { int act, icon; const char *label; bool on; unsigned c; };
		bool can = p.canAct (), awake = can && !p.asleep;
		B bs[6] = {
			{ H_FEED, I_BOWL, "Nourrir", awake, 0x45A8E6 },
			{ H_PLAY, I_BALL, a.ballOn ? "Arrêter" : "Jouer", awake, 0 },
			{ H_CLEAN, I_SOAP, "Laver", can, 0x45A8E6 },
			{ H_HEAL, I_PILL, "Soigner", awake, 0 },
			{ H_LAMP, p.lampOff ? I_SUN : I_MOON, p.lampOff ? "Allumer" : "Coucher", can, 0x8C98F0 },
			{ p.asleep ? H_WAKE : H_CARESS, p.asleep ? I_ZZZ : I_HAND, p.asleep ? "Réveiller" : "Câliner", can, 0xEC5B8F },
		};
		const int N = 6;
		int bh = bH, gap = K (8), pad = K (14), isz = K (8);
		int bwid[N], bxs[N], bys[N], tot = 0;
		bool icons = compact, centered = false;
		if (portrait)
		{
			// une grille de 3 × 2, sur toute la largeur ; icône et texte centrés ensemble
			int cw = (w - 2 * MARGIN - (dCols - 1) * gap) / dCols;
			for (int i = 0; i < N; i++)
			{
				bwid[i] = cw; bxs[i] = MARGIN + (i % dCols) * (cw + gap);
				bys[i] = dy + 1 + K (12) + (i / dCols) * (bh + gap);
			}
			centered = true;
			if (!icons) for (int i = 0; i < N; i++) if (isz * 2 + K (8) + ga::textW (TR (bs[i].label), K (13), true) > cw - K (12)) icons = true;
		}
		else
		{
			for (int i = 0; i < N; i++) { bwid[i] = pad * 2 + isz * 2 + K (8) + ga::textW (TR (bs[i].label), K (13), true); tot += bwid[i] + (i ? gap : 0); }
			if (tot > w - 2 * MARGIN) icons = true;
			if (icons)
			{
				bh = K (46); tot = 0;
				for (int i = 0; i < N; i++) { bwid[i] = bh; tot += bh + (i ? gap : 0); }
			}
			int x0 = (w - tot) / 2, y0 = dy + 1 + (h - dy - 1 - bh) / 2;
			for (int i = 0; i < N; i++) { bxs[i] = x0; bys[i] = y0; x0 += bwid[i] + gap; }
		}
		int feedX = bxs[0], y = bys[0];
		for (int i = 0; i < N; i++)
		{
			const B &q = bs[i];
			int x = bxs[i], y = bys[i];
			bool hov = hover == q.act && q.on;
			bool sel = (q.act == H_FEED && feedOpen) || (q.act == H_PLAY && a.ballOn) || (q.act == H_LAMP && p.lampOff);
			unsigned bg = sel ? ga::mix (CARD, ACC, darkTheme () ? 80 : 50) : hov ? tok (TJ_HOVER) : CARD;
			int al = q.on ? 256 : 120;
			im.rbox (x, y, bwid[i], bh, K (icons ? 12 : 8), bg, bg, al, sel ? ACC : LINE);
			int lw = ga::textW (TR (q.label), K (13), true);
			int lx = centered ? x + (bwid[i] - (isz * 2 + K (8) + lw)) / 2 : x + pad;		// (le début de l'icône)
			float icx = icons ? (float) x + (float) bwid[i] / 2 : (float) (lx + isz), icy = (float) y + (float) bh / 2;
			float s = (float) (icons ? K (12) : isz) * (hov ? 1.08f : 1);
			unsigned ic = q.c ? q.c : T2;
			if (q.icon == I_MOON) { im.disc (icx, icy, s * 0.8f, ic, al); im.disc (icx + s * 0.36f, icy - s * 0.25f, s * 0.68f, bg, 256); }
			else icon (im, q.icon, icx, icy, s, ic, t);
			if (!q.on) im.rbox (x + 1, y + 1, bwid[i] - 2, bh - 2, K (8), bg, bg, 140);
			if (!icons) ga::text (lx + isz * 2 + K (8), y + bh / 2 + K (5), TR (q.label), q.on ? T1 : T3, K (13), true);
			addHit (q.act, x, y, bwid[i], bh, q.on);
		}
		// le choix du repas : au-dessus de « Nourrir »
		if (feedOpen && awake)
		{
			static const char *const FN[3] = { "Repas", "Fruit", "Friandise" };
			static const char *const FH[3] = { "+ satiété", "+ santé", "+ bonheur" };
			static const int FI[3] = { I_BOWL, I_APPLE, I_CAKE };
			int tw = K (112), th = K (52), tg = K (6), pp = K (8);
			if (3 * tw + 2 * tg + 2 * pp > w - 2 * MARGIN) tw = (w - 2 * MARGIN - 2 * tg - 2 * pp) / 3;
			int pw = 3 * tw + 2 * tg + 2 * pp, phh = th + 2 * pp;
			int px = feedX, py = y - phh - K (8);
			if (px + pw > w - MARGIN) px = w - MARGIN - pw;
			if (px < MARGIN) px = MARGIN;
			panel (im, px, py, pw, phh, K (12));
			addHit (H_NONE, px, py, pw, phh, true);		// (le fond d'abord : hit cherche du dernier au premier)
			for (int i = 0; i < 3; i++)
			{
				int tx = px + pp + i * (tw + tg), ty = py + pp;
				bool hov = hover == H_MEAL + i;
				if (hov) im.rbox (tx, ty, tw, th, K (8), tok (TJ_HOVER), tok (TJ_HOVER));
				icon (im, FI[i], (float) (tx + K (20)), (float) (ty + th / 2), (float) K (10), 0x45A8E6, t);
				ga::text (tx + K (38), ty + K (22), TR (FN[i]), T1, K (13), true);
				ga::text (tx + K (38), ty + K (39), TR (FH[i]), T3, K (11), false);
				snprintf (b, sizeof b, "%d", i + 1);
				ga::textR (tx + tw - K (8), ty + K (22), b, T3, K (10), false);
				addHit (H_MEAL + i, tx, ty, tw, th, true);
			}
		}
	}

	// ---- la fiche : un tiroir qui glisse depuis la droite de la vue
	if (infoT > 0.01f)
	{
		float e = smooth (0, 1, infoT);
		int dw = compact ? vw : (int) fminf ((float) K (320), (float) vw);
		int x = vx + vw - (int) ((float) dw * e), y = vy, dh = vh;
		im.clip (vx, vy, vx + vw, vy + vh);
		for (int i = 6; i >= 1; i--) im.rect (x - i, y, 1, dh, 0, 18 + 6 * (6 - i));
		unsigned F = tok (TJ_SURFACE);
		im.rbox (x, y, dw, dh, R, F, F, 256, darkTheme () ? 0x2C313A : LINE);
		im.rect (x, y, R, dh, F); im.rect (x, y, 1, dh, darkTheme () ? 0x2C313A : LINE);
		im.noClip ();
		addHit (H_NONE, x, y, dw, dh, true);
		int cx = x + K (20), cy = y + K (36);
		ga::text (cx, cy, p.name, T1, K (19), true);
		{
			int bx2 = x + dw - K (38), by2 = y + K (14);
			bool hov = hover == H_CLOSE;
			im.disc ((float) (bx2 + K (13)), (float) (by2 + K (13)), (float) K (13), hov ? tok (TJ_HOVER) : CARD);
			float c0 = (float) (bx2 + K (13)), c1 = (float) (by2 + K (13)), r = (float) K (5);
			im.line (c0 - r, c1 - r, c0 + r, c1 + r, 1.6f * k, T2); im.line (c0 - r, c1 + r, c0 + r, c1 - r, 1.6f * k, T2);
			addHit (H_CLOSE, bx2, by2, K (26), K (26), true);
		}
		speciesName (p.g, b, sizeof b);
		ga::text (cx, cy + K (20), b, T3, K (12), false);
		cy += K (40);
		im.rect (cx, cy, dw - K (40), 1, LINE);
		cy += K (24);
		int rowH = vh < K (420) ? K (21) : K (25);
		auto row = [&] (const char *k1, const char *v) {
			ga::text (cx, cy, k1, T2, K (12), false);
			ga::textR (x + dw - K (20), cy, v, T1, K (12), true);
			cy += rowH;
		};
		row (TR ("Stade"), p.gone ? TR ("Parti") : stageName (p.g.stage));
		formatAge (p.age, b2, sizeof b2); row (TR ("Âge"), b2);
		if (!p.gone)
		{
			formatDur ((double) (p.stageLen () - p.stageAge) / (double) p.speed, b2, sizeof b2);
			row (p.g.stage < ST_ELDER ? TR ("Prochain stade dans") : TR ("Encore"), b2);
		}
		if (!egg)
		{
			row (TR ("Caractère"), traitName (p.g.trait));
			float kg = (p.g.stage <= ST_BABY ? 0.8f : p.g.stage == ST_CHILD ? 2.5f : p.g.stage == ST_TEEN ? 5 : 9) * p.g.size * (0.75f + p.fat);
			snprintf (b2, sizeof b2, TR ("%.1f kg"), (double) kg); row (TR ("Poids"), b2);
		}
		snprintf (b2, sizeof b2, "%d", p.generation); row (TR ("Génération"), b2);
		{
			ga::text (cx, cy, TR ("Soins reçus"), T2, K (12), false);
			int n = clampi ((int) (p.care () * 5 + 0.5f) - p.mistakes / 2, 0, 5);
			for (int i = 0; i < 5; i++) star (im, (float) (x + dw - K (20) - K (7) - (4 - i) * K (17)), (float) (cy - K (4)), (float) K (7), i < n ? 0xF0BE2E : tok (TJ_SUNKEN), 256, 0);
			cy += rowH + K (4);
		}
		im.rect (cx, cy - K (10), dw - K (40), 1, LINE);
		cy += K (10);
		ga::text (cx, cy, TR ("Évolution"), T2, K (12), false);
		cy += K (18);
		int step = (dw - K (48)) / (ST_COUNT - 1);
		for (int i = 0; i < ST_COUNT; i++)
		{
			float ddx = (float) (cx + K (4) + i * step), ddy = (float) (cy + K (6));
			if (i < ST_COUNT - 1) im.line (ddx, ddy, ddx + (float) step, ddy, 2 * k, i < p.g.stage ? ACC : LINE);
			im.disc (ddx, ddy, (float) K (i == p.g.stage ? 7 : 5), i <= p.g.stage ? ACC : tok (TJ_SUNKEN));
			if (i == p.g.stage) im.disc (ddx, ddy, (float) K (3), 0xFFFFFF);
		}
		cy += K (30);
		ga::textC (cx + (dw - K (40)) / 2, cy, stageName (p.g.stage), T3, K (11), false);
		{
			int rw = K (140), rh = K (32), rx = x + (dw - rw) / 2, ry = y + dh - rh - K (16);
			if (ry > cy + K (10))
			{
				bool hov = hover == H_RENAME;
				im.rbox (rx, ry, rw, rh, K (8), hov ? tok (TJ_HOVER) : CARD, hov ? tok (TJ_HOVER) : CARD, 256, LINE);
				ga::textC (rx + rw / 2, ry + K (21), TR ("Renommer…"), T1, K (12), true);
				addHit (H_RENAME, rx, ry, rw, rh, true);
			}
		}
	}

	// ---- les cartes des grands moments
	if (card)
	{
		im.rect (0, 0, w, h, 0x000000, (int) (smooth (0, 0.3f, cardT) * (darkTheme () ? 130 : 90)));
		int cw = (int) fminf ((float) K (400), (float) (w - 2 * MARGIN));
		int lines = 1; for (const char *s = cardText; *s; s++) if (*s == '\n') lines++;
		int chh = K (150) + lines * K (22) + K (56);
		int x = (w - cw) / 2, y = (h - chh) / 2 - (int) ((1 - smooth (0, 0.3f, cardT)) * (float) K (16));
		panel (im, x, y, cw, chh, K (16), 256);
		im.rbox (x + K (24), y, cw - K (48), K (3), K (1), ACC, ACC);
		addHit (H_NONE, 0, 0, w, h, true);
		float ex = (float) (x + cw / 2), ey = (float) (y + K (48));
		im.glow (ex, ey, (float) K (44), ga::shade (ACC, 50));
		im.disc (ex, ey, (float) K (28), ga::mix (CARD, ACC, 60));
		int ic = card == C_GONE ? I_STAR : card == C_HATCHED || card == C_WELCOME ? I_EGG : card == C_BACK ? I_FUN : I_STAR;
		icon (im, ic, ex, ey, (float) K (15), card == C_EVOLVED || card == C_GONE ? 0xF0BE2E : ic == I_EGG ? 0xF4EEDF : 0xEC5B8F, t);
		if (ic == I_EGG) { im.disc (ex - K (5), ey, (float) K (3), 0xEC5B8F); im.disc (ex + K (5), ey + K (8), (float) K (3), 0x45A8E6); }
		ga::textC (x + cw / 2, y + K (106), cardTitle, T1, K (19), true);
		int ly = y + K (136);
		const char *s = cardText;
		while (*s)
		{
			const char *e2 = strchr (s, '\n');
			int n = e2 ? (int) (e2 - s) : (int) strlen (s);
			char ln[200]; snprintf (ln, sizeof ln, "%.*s", n, s);
			ga::textC (x + cw / 2, ly, ln, T2, K (13), false);
			ly += K (22);
			s += n; if (*s) s++;
		}
		int bw2 = K (170), bh2 = K (36), bx2 = x + (cw - bw2) / 2, by2 = y + chh - bh2 - K (18);
		int act = card == C_GONE ? H_NEWEGG : H_CARD;
		bool hov = hover == act;
		unsigned bc = hov ? (darkTheme () ? tok (TJ_ACCENT_VIVID) : tok (TJ_ACCENT_DARK)) : ACC;
		im.rbox (bx2, by2, bw2, bh2, K (8), bc, bc);
		ga::textC (bx2 + bw2 / 2, by2 + K (23), card == C_GONE ? TR ("Couver l'œuf") : TR ("Continuer"), (unsigned) ui_ink (bc) & 0xFFFFFF, K (13), true);
		addHit (act, bx2, by2, bw2, bh2, true);
	}
}

}	// namespace mo
