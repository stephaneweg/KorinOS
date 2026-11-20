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

// ==== le dessin =======================================================================================================
static void panel (Img &im, int x, int y, int w, int h, int r, int a = 242)
{
	bool dk = darkTheme ();
	// l'ombre portée, douce
	for (int i = 3; i >= 1; i--) im.rbox (x - i + 1, y + i * 2 - 1, w + 2 * i - 2, h + 2 * i - 1, r + i, 0, 0, dk ? 34 : 16);
	unsigned s = tok (TJ_SURFACE);
	im.rbox (x, y, w, h, r, s, ga::mix (s, tok (TJ_FACE), 90), a, tok (TJ_LINE));
}
static void pill (Img &im, int x, int y, int w, int h, unsigned bg, int a = 256) { im.rbox (x, y, w, h, h / 2, bg, bg, a); }

void Hud::draw (Img &im, const Pet &p, const Actor &a, const Camera &cam, const Light &L)
{
	w = im.w; h = im.h;
	k = clampf (fminf ((float) w / 960, (float) h / 660), 0.72f, 1.45f);
	auto K = [&] (float v) { return (int) (v * k + 0.5f); };
	nh = 0;
	auto addHit = [&] (int act, int x, int y, int ww, int hh, bool on) { if (nh < 32) hits[nh++] = { act, x, y, ww, hh, on }; };
	unsigned T1 = tok (TJ_TEXT1), T2 = tok (TJ_TEXT2), T3 = tok (TJ_TEXT3), ACC = tok (TJ_ACCENT);
	char b[256], b2[128];
	int m = K (14);
	bool egg = p.g.stage == ST_EGG;

	// ---- les particules (sous l'interface)
	for (const Particle &q : a.parts)
	{
		if (q.kind == P_BUBBLE) continue;
		float sx, sy, d;
		if (!cam.project (q.p, sx, sy, &d)) continue;
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
			int px = (int) (10 * sc + (1 - life) * 9 * sc);
			ga::text ((int) sx + 1, (int) sy + 1, "z", 0x203040, px, true);
			ga::text ((int) sx, (int) sy, "z", 0xFFFFFF, px, true);
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
	// l'éclair blanc
	if (a.flash > 0.01f) im.rect (0, 0, w, h, 0xFFFFFF, (int) (a.flash * 220));

	// ---- la bulle au-dessus de la tête : ce qu'il lui faut
	int need = p.worst ();
	if (need >= 0 && a.act == A_IDLE && !p.asleep && !a.ballOn)
	{
		float sx, sy;
		if (cam.project (a.out.top, sx, sy))
		{
			float bob = sinf (t * 3) * 3 * k;
			int R = K (17);
			float cy = sy - K (26) + bob;
			unsigned bg = tok (TJ_SURFACE);
			im.disc (sx + 2, cy + 3, (float) R + 1, 0, 40);
			im.disc (sx, cy, (float) R + 1.5f, tok (TJ_LINE));
			im.disc (sx, cy, (float) R, bg);
			float tri[6] = { sx - R * 0.35f, cy + R * 0.8f, sx + R * 0.15f, cy + R * 0.8f, sx - R * 0.3f, cy + R * 1.45f };
			im.poly (tri, 3, bg);
			int ic = need == N_FOOD ? I_FOOD : need == N_FUN ? I_FUN : need == N_ENERGY ? I_ENERGY : need == N_CLEAN ? (p.poops >= 2 ? I_POOP : I_CLEAN) : I_HEALTH;
			unsigned c = need == N_CLEAN && p.poops >= 2 ? 0x8A5A30 : NEED_C[need];
			if (need == N_HEALTH && p.sick) c = 0x47BE78;
			icon (im, ic, sx, cy, (float) R * 0.62f, c, t);
			if (p.sick && need == N_HEALTH) { im.disc (sx + R * 0.7f, cy - R * 0.7f, (float) R * 0.32f, tok (TJ_DANGER)); ga::textC ((int) (sx + R * 0.7f), (int) (cy - R * 0.7f + R * 0.22f), "!", 0xFFFFFF, K (11), true); }
		}
	}

	// ---- la carte de l'animal
	{
		int cw = K (244), x = m, y = m;
		int ch = egg || p.gone ? K (92) : K (214);
		panel (im, x, y, cw, ch, K (14));
		addHit (H_INFO, x, y, cw, ch, true);
		ga::text (x + K (16), y + K (30), p.name, T1, K (19), true);
		int nx = x + K (16) + ga::textW (p.name, K (19), true) + K (8);
		const char *sn = p.gone ? TR ("Parti") : stageName (p.g.stage);
		int pw = ga::textW (sn, K (11), true) + K (16);
		pill (im, nx, y + K (16), pw, K (19), ga::mix (tok (TJ_SURFACE), ACC, 50));
		ga::text (nx + K (8), y + K (30), sn, ga::mix (ACC, T1, 60), K (11), true);
		if (p.speed > 1)
		{
			snprintf (b, sizeof b, "×%d", p.speed);
			int sw = ga::textW (b, K (11), true) + K (12);
			pill (im, x + cw - sw - K (12), y + K (16), sw, K (19), ga::mix (tok (TJ_SURFACE), tok (TJ_WARNING), 70));
			ga::text (x + cw - sw - K (6), y + K (30), b, T1, K (11), true);
		}
		formatAge (p.age, b2, sizeof b2);
		if (egg) snprintf (b, sizeof b, TR ("Génération %d · éclosion bientôt"), p.generation);
		else snprintf (b, sizeof b, TR ("%s · génération %d"), b2, p.generation);
		ga::text (x + K (16), y + K (50), b, T2, K (12), false);
		if (egg || p.gone)
		{
			// la barre de l'éclosion
			int bx = x + K (16), by = y + K (64), bw = cw - K (32), bh = K (10);
			im.rbox (bx, by, bw, bh, bh / 2, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
			float f = p.gone ? 1 : sat (p.stageProgress ());
			if (f > 0.02f) im.rbox (bx, by, (int) ((float) bw * f), bh, bh / 2, ga::shade (ACC, 300), ACC);
		}
		else
		{
			static const char *const NAMES[N_COUNT] = { "Satiété", "Bonheur", "Énergie", "Propreté", "Santé" };
			static const int ICONS[N_COUNT] = { I_FOOD, I_FUN, I_ENERGY, I_CLEAN, I_HEALTH };
			im.rect (x + K (14), y + K (62), cw - K (28), 1, tok (TJ_LINE));
			for (int i = 0; i < N_COUNT; i++)
			{
				int ry = y + K (72) + i * K (27);
				float v = p.st[i];
				bool low = v < 25;
				unsigned c = NEED_C[i];
				im.disc ((float) (x + K (28)), (float) (ry + K (11)), (float) K (11), ga::mix (tok (TJ_SURFACE), c, 50));
				icon (im, ICONS[i], (float) (x + K (28)), (float) (ry + K (11)), (float) K (7), c, t);
				ga::text (x + K (46), ry + K (10), TR (NAMES[i]), T2, K (11), false);
				int bx = x + K (46), by = ry + K (14), bw = cw - K (46) - K (16), bh = K (7);
				im.rbox (bx, by, bw, bh, bh / 2, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
				int fw = (int) ((float) bw * v / 100);
				unsigned fc = low ? tok (TJ_DANGER) : c;
				int al = low ? (int) (180 + 76 * sinf (t * 6)) : 256;
				if (fw > bh / 2) im.rbox (bx, by, fw, bh, bh / 2, ga::shade (fc, 290), fc, al);
				snprintf (b, sizeof b, "%d", (int) (v + 0.5f));
				ga::textR (x + cw - K (16), ry + K (10), b, low ? tok (TJ_DANGER) : T3, K (10), true);
			}
			if (p.sick)
			{
				snprintf (b, sizeof b, "%s", TR ("Malade"));
				int sw = ga::textW (b, K (10), true) + K (14);
				pill (im, x + cw - sw - K (14) - K (24), y + K (72) + 4 * K (27) - K (3), sw, K (15), tok (TJ_DANGER));
				ga::text (x + cw - sw - K (14) - K (24) + K (7), y + K (72) + 4 * K (27) + K (8), b, 0xFFFFFF, K (10), true);
			}
		}
	}

	// ---- la barre des soins
	if (!p.gone)
	{
		struct B { int act, icon; const char *label; bool on; unsigned c; };
		bool can = p.canAct ();
		bool awake = can && !p.asleep;
		B bs[7] = {
			{ H_FEED, I_BOWL, "Nourrir", awake, 0x45A8E6 },
			{ H_PLAY, I_BALL, a.ballOn ? "Arrêter" : "Jouer", awake, 0 },
			{ H_CLEAN, I_SOAP, "Laver", can, 0x45A8E6 },
			{ H_HEAL, I_PILL, "Soigner", awake, 0 },
			{ H_LAMP, p.lampOff ? I_SUN : I_MOON, p.lampOff ? "Allumer" : "Coucher", can, 0x6C7BD8 },
			{ p.asleep ? H_WAKE : H_CARESS, p.asleep ? I_ZZZ : I_HAND, p.asleep ? "Réveiller" : "Câliner", can, 0xEC5B8F },
			{ H_INFO, I_INFO, "Fiche", true, T2 },
		};
		const int N = 7;
		float kb = k;
		int bd0 = 54, gap0 = 16, pad0 = 16;
		int need0 = N * bd0 + (N - 1) * gap0 + 2 * pad0;
		if ((float) need0 * kb > (float) (w - 2 * m)) kb = (float) (w - 2 * m) / (float) need0;
		auto KB = [&] (float v) { return (int) (v * kb + 0.5f); };
		int bd = KB ((float) bd0), gap = KB ((float) gap0), pad = KB ((float) pad0);
		int bw = N * bd + (N - 1) * gap + 2 * pad, bh = bd + KB (34);
		int bx = (w - bw) / 2, by = h - m - bh;
		panel (im, bx, by, bw, bh, KB (20));
		for (int i = 0; i < N; i++)
		{
			const B &q = bs[i];
			int x = bx + pad + i * (bd + gap), y = by + KB (10);
			float cx = (float) x + (float) bd / 2, cy = (float) y + (float) bd / 2;
			bool hov = hover == q.act && q.on;
			bool sel = (q.act == H_FEED && feedOpen) || (q.act == H_INFO && infoOpen) || (q.act == H_PLAY && a.ballOn);
			unsigned bg = sel ? ga::mix (tok (TJ_SURFACE), ACC, 70) : hov ? tok (TJ_HOVER) : tok (TJ_RAISED);
			int al = q.on ? 256 : 110;
			im.disc (cx, cy + 1.5f, (float) bd / 2 + 0.5f, 0, darkTheme () ? 60 : 26);
			im.disc (cx, cy, (float) bd / 2, sel ? ACC : tok (TJ_LINE), al);
			im.disc (cx, cy, (float) bd / 2 - 1.2f, bg, al);
			float s = (float) bd * 0.27f * (hov ? 1.08f : 1);
			unsigned ic = q.c ? q.c : T2;
			if (q.icon == I_MOON)
			{
				im.disc (cx, cy, s * 0.8f, 0x6C7BD8, al);
				im.disc (cx + s * 0.36f, cy - s * 0.25f, s * 0.68f, bg, al);
			}
			else if (q.on) icon (im, q.icon, cx, cy, s, ic, t);
			else { icon (im, q.icon, cx, cy, s, ic, t); im.disc (cx, cy, (float) bd / 2 - 1.2f, bg, 150); }
			ga::textC ((int) cx, by + bh - KB (11), TR (q.label), q.on ? (hov ? T1 : T2) : T3, KB (11), hov);
			addHit (q.act, x - gap / 2, by, bd + gap, bh, q.on);
		}
		// le choix du repas
		if (feedOpen && awake)
		{
			int tw = KB (104), th = KB (118), tg = KB (10);
			int pw = 3 * tw + 2 * tg + 2 * KB (12), ph = th + KB (24);
			int px = bx + pad + bd / 2 - KB (12) - tw / 2, py = by - ph - KB (10);
			if (px < m) px = m;
			panel (im, px, py, pw, ph, KB (16), 250);
			static const char *const FN[3] = { "Repas", "Fruit", "Friandise" };
			static const char *const FH[3] = { "+ satiété", "+ santé", "+ bonheur" };
			static const int FI[3] = { I_BOWL, I_APPLE, I_CAKE };
			for (int i = 0; i < 3; i++)
			{
				int x = px + KB (12) + i * (tw + tg), y = py + KB (12);
				bool hov = hover == H_MEAL + i;
				im.rbox (x, y, tw, th, KB (12), hov ? tok (TJ_HOVER) : tok (TJ_RAISED), hov ? tok (TJ_HOVER) : tok (TJ_RAISED), 256, tok (TJ_LINE));
				icon (im, FI[i], (float) (x + tw / 2), (float) (y + KB (40)), (float) KB (20), 0x45A8E6, t);
				ga::textC (x + tw / 2, y + KB (84), TR (FN[i]), T1, KB (12), true);
				ga::textC (x + tw / 2, y + KB (102), TR (FH[i]), T3, KB (10), false);
				addHit (H_MEAL + i, x, y, tw, th, true);
			}
		}
		// ce qu'il se passe en bas : il dort, on joue
		const char *hint = 0;
		if (p.asleep) hint = p.lampOff ? TR ("Chut… il dort. Zzz") : TR ("Il dort : éteignez la lumière pour qu'il se repose mieux.");
		else if (a.ballOn) { snprintf (b2, sizeof b2, TR ("Cliquez dans la prairie pour lancer la balle · attrapées : %d"), a.catches); hint = b2; }
		else if (egg && a.act != A_HATCH) hint = TR ("L'œuf frémit… il va bientôt éclore.");
		if (hint)
		{
			int tw2 = ga::textW (hint, K (12), false) + K (28);
			int hx = (w - tw2) / 2, hy = by - K (40);
			if (!(feedOpen && awake)) { panel (im, hx, hy, tw2, K (30), K (15), 235); ga::textC (w / 2, hy + K (20), hint, T2, K (12), false); }
		}
	}

	// ---- la fiche
	if (infoOpen)
	{
		int pw = K (300), x = w - m - pw, y = m + K (40);
		int ph = K (420);
		if (y + ph > h - m - K (100)) ph = h - m - K (100) - y;
		if (ph < K (260)) ph = K (260);
		if (x < m + K (250)) x = w - m - pw;
		panel (im, x, y, pw, ph, K (16), 250);
		addHit (H_NONE, x, y, pw, ph, true);
		int cx = x + K (18), cy = y + K (34);
		ga::text (cx, cy, p.name, T1, K (20), true);
		// fermer
		{
			int bx2 = x + pw - K (36), by2 = y + K (12);
			bool hov = hover == H_CLOSE;
			im.disc ((float) (bx2 + K (12)), (float) (by2 + K (12)), (float) K (12), hov ? tok (TJ_HOVER) : tok (TJ_RAISED));
			float c0 = (float) (bx2 + K (12)), c1 = (float) (by2 + K (12)), r = (float) K (5);
			im.line (c0 - r, c1 - r, c0 + r, c1 + r, 1.6f * k, T2); im.line (c0 - r, c1 + r, c0 + r, c1 - r, 1.6f * k, T2);
			addHit (H_CLOSE, bx2, by2, K (24), K (24), true);
		}
		speciesName (p.g, b, sizeof b);
		ga::text (cx, cy + K (20), b, T3, K (12), false);
		cy += K (44);
		im.rect (cx, cy, pw - K (36), 1, tok (TJ_LINE));
		cy += K (22);
		auto row = [&] (const char *k1, const char *v) {
			ga::text (cx, cy, k1, T2, K (12), false);
			ga::textR (x + pw - K (18), cy, v, T1, K (12), true);
			cy += K (24);
		};
		row (TR ("Stade"), p.gone ? TR ("Parti") : stageName (p.g.stage));
		formatAge (p.age, b2, sizeof b2); row (TR ("Âge"), b2);
		if (!p.gone && p.g.stage < ST_ELDER)
		{
			formatDur ((double) (p.stageLen () - p.stageAge) / (double) p.speed, b2, sizeof b2);
			row (TR ("Prochain stade dans"), b2);
		}
		else if (!p.gone) { formatDur ((double) (p.stageLen () - p.stageAge) / (double) p.speed, b2, sizeof b2); row (TR ("Encore"), b2); }
		if (!egg)
		{
			row (TR ("Caractère"), traitName (p.g.trait));
			float kg = (p.g.stage <= ST_BABY ? 0.8f : p.g.stage == ST_CHILD ? 2.5f : p.g.stage == ST_TEEN ? 5 : 9) * p.g.size * (0.75f + p.fat);
			snprintf (b2, sizeof b2, TR ("%.1f kg"), (double) kg); row (TR ("Poids"), b2);
		}
		snprintf (b2, sizeof b2, "%d", p.generation); row (TR ("Génération"), b2);
		// les soins : des étoiles
		{
			ga::text (cx, cy, TR ("Soins reçus"), T2, K (12), false);
			int n = (int) (p.care () * 5 + 0.5f) - p.mistakes / 2;
			n = clampi (n, 0, 5);
			for (int i = 0; i < 5; i++) star (im, (float) (x + pw - K (18) - K (8) - (4 - i) * K (17)), (float) (cy - K (4)), (float) K (7), i < n ? 0xF0BE2E : tok (TJ_SUNKEN), 256, 0);
			cy += K (28);
		}
		// le chemin des stades
		im.rect (cx, cy - K (10), pw - K (36), 1, tok (TJ_LINE));
		cy += K (10);
		ga::text (cx, cy, TR ("Évolution"), T2, K (12), false);
		cy += K (18);
		int step = (pw - K (36)) / (ST_COUNT - 1);
		for (int i = 0; i < ST_COUNT; i++)
		{
			float dx = (float) (cx + i * step), dy = (float) (cy + K (6));
			if (i < ST_COUNT - 1) im.line (dx, dy, dx + (float) step, dy, 2 * k, i < p.g.stage ? ACC : tok (TJ_LINE));
			bool done = i <= p.g.stage;
			im.disc (dx, dy, (float) K (i == p.g.stage ? 7 : 5), done ? ACC : tok (TJ_SUNKEN));
			if (i == p.g.stage) im.disc (dx, dy, (float) K (3), 0xFFFFFF);
		}
		cy += K (30);
		ga::textC (cx + (pw - K (36)) / 2, cy, stageName (p.g.stage), T3, K (10), false);
		// renommer
		{
			int rw = K (130), rh = K (30), rx = x + (pw - rw) / 2, ry = y + ph - rh - K (16);
			bool hov = hover == H_RENAME;
			im.rbox (rx, ry, rw, rh, rh / 2, hov ? tok (TJ_HOVER) : tok (TJ_RAISED), hov ? tok (TJ_HOVER) : tok (TJ_RAISED), 256, tok (TJ_LINE));
			ga::textC (rx + rw / 2, ry + K (20), TR ("Renommer…"), T1, K (12), true);
			addHit (H_RENAME, rx, ry, rw, rh, true);
		}
	}

	// ---- les messages
	{
		int y = m + K (6);
		for (int i = 0; i < nt; i++)
		{
			const Toast &q = toasts[i];
			float in = smooth (0, 0.25f, q.t), out = 1 - smooth (q.life - 0.5f, q.life, q.t);
			float al = fminf (in, out);
			int tw = ga::textW (q.text, K (13), true) + K (36) + (q.icon >= 0 ? K (22) : 0);
			int x = (w - tw) / 2;
			int yy = y - (int) ((1 - in) * (float) K (12));
			int A = (int) (al * 245);
			for (int s = 3; s >= 1; s--) im.rbox (x - s + 1, yy + s * 2 - 1, tw + 2 * s - 2, K (34) + 2 * s - 1, K (17) + s, 0, 0, (int) (al * 14));
			im.rbox (x, yy, tw, K (34), K (17), tok (TJ_SURFACE), tok (TJ_SURFACE), A, tok (TJ_LINE));
			int tx = x + K (18);
			if (q.icon >= 0) { icon (im, q.icon, (float) (tx + K (8)), (float) (yy + K (17)), (float) K (8), q.icon < N_COUNT ? NEED_C[q.icon] : ACC, t); tx += K (22); }
			if (al > 0.5f) ga::text (tx, yy + K (22), q.text, T1, K (13), true);
			y += K (42);
		}
	}

	// ---- les cartes des grands moments
	if (card)
	{
		im.rect (0, 0, w, h, darkTheme () ? 0x000000 : 0x10141C, (int) (smooth (0, 0.3f, cardT) * 110));
		int cw = (int) fminf ((float) K (440), (float) (w - 2 * m));
		int lines = 1; for (const char *s = cardText; *s; s++) if (*s == '\n') lines++;
		int ch = K (150) + lines * K (22) + K (56);
		int x = (w - cw) / 2, y = (h - ch) / 2 - (int) ((1 - smooth (0, 0.3f, cardT)) * (float) K (20));
		panel (im, x, y, cw, ch, K (20), 252);
		addHit (H_NONE, 0, 0, w, h, true);
		// l'emblème
		float ex = (float) (x + cw / 2), ey = (float) (y + K (46));
		im.glow (ex, ey, (float) K (44), ga::shade (ACC, 60));
		im.disc (ex, ey, (float) K (28), ga::mix (tok (TJ_SURFACE), ACC, 60));
		int ic = card == C_GONE ? I_STAR : card == C_HATCHED || card == C_WELCOME ? I_EGG : card == C_BACK ? I_FUN : I_STAR;
		icon (im, ic, ex, ey, (float) K (15), card == C_EVOLVED || card == C_GONE ? 0xF0BE2E : ic == I_EGG ? 0xF4EEDF : 0xEC5B8F, t);
		if (ic == I_EGG) { im.disc (ex - K (5), ey, (float) K (3), 0xEC5B8F); im.disc (ex + K (5), ey + K (8), (float) K (3), 0x45A8E6); }
		ga::textC (x + cw / 2, y + K (106), cardTitle, T1, K (20), true);
		int ly = y + K (136);
		const char *s = cardText;
		while (*s)
		{
			const char *e = strchr (s, '\n');
			int n = e ? (int) (e - s) : (int) strlen (s);
			char ln[200]; snprintf (ln, sizeof ln, "%.*s", n, s);
			ga::textC (x + cw / 2, ly, ln, T2, K (13), false);
			ly += K (22);
			s += n; if (*s) s++;
		}
		int bw2 = K (170), bh2 = K (36), bx2 = x + (cw - bw2) / 2, by2 = y + ch - bh2 - K (18);
		int act = card == C_GONE ? H_NEWEGG : H_CARD;
		bool hov = hover == act;
		unsigned bc = hov ? tok (TJ_ACCENT_LIGHT) : ACC;
		if (darkTheme () && hov) bc = tok (TJ_ACCENT_VIVID);
		im.rbox (bx2, by2, bw2, bh2, bh2 / 2, ga::shade (bc, 280), bc);
		ga::textC (bx2 + bw2 / 2, by2 + K (23), card == C_GONE ? TR ("Couver l'œuf") : TR ("Continuer"), (unsigned) ui_ink (bc) & 0xFFFFFF, K (13), true);
		addHit (act, bx2, by2, bw2, bh2, true);
	}
	(void) L;
}

}	// namespace mo
