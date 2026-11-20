//
// creature.cpp -- Morpho : la créature construite depuis son génome. Le corps est une surface balayée le long d'une
// colonne (de la pointe de la queue au bout du museau, sections elliptiques de rayons lissés) ; les pièces (yeux,
// bouche, cornes, oreilles, dos, ailes, bout de queue) sont posées sur cette surface par un lancer de rayon contre une
// distance signée, puis suivent la tête ou le corps ; les jambes et les bras trouvent le sol ou leur cible par une
// cinématique inverse à deux os. Tout est refait à chaque image : l'animation déforme la colonne.
//
#include "creature.h"

namespace mo {

// ==== la colonne ========================================================================================================
struct Key { V3 p; float rx, ry; int tag; };
enum { K_TAILTIP, K_TAIL2, K_TAIL1, K_TAILBASE, K_HIP, K_MID, K_SH, K_NECK, K_NECKTOP, K_HEAD, K_SNOUT, K_NOSE, K_N };

struct Ring { V3 c, T, X, N; float rx, ry, s, dr; };

struct Body
{
	std::vector<Ring> r;			// les anneaux du corps (repos), de la queue au museau
	float sk[K_N];				// l'abscisse (0..1) de chaque clé
	V3 kp[K_N]; float kr[K_N];		// les clés (repos) : place, rayon moyen
	V3 hd, up;				// la tête : devant, dessus (repos)
	float headRx, headRy, snoutR;
	float minY;				// le point le plus bas (repos)
};

static Ring ringAt (const Body &b, float s)
{
	const std::vector<Ring> &r = b.r;
	if (s <= r[0].s) return r[0];
	if (s >= r.back ().s) return r.back ();
	int lo = 0, hi = (int) r.size () - 1;
	while (hi - lo > 1) { int m = (lo + hi) / 2; if (r[m].s < s) lo = m; else hi = m; }
	float t = (s - r[lo].s) / fmaxf (r[hi].s - r[lo].s, 1e-6f);
	Ring g;
	g.c = lerp (r[lo].c, r[hi].c, t); g.T = norm (lerp (r[lo].T, r[hi].T, t)); g.X = r[lo].X;
	g.N = norm (lerp (r[lo].N, r[hi].N, t)); g.rx = lerpf (r[lo].rx, r[hi].rx, t); g.ry = lerpf (r[lo].ry, r[hi].ry, t);
	g.s = s; g.dr = lerpf (r[lo].dr, r[hi].dr, t);
	return g;
}

static void layout (const Genome &g, float breathe, float t, Body &b)
{
	Key k[K_N];
	float a = g.upright * 1.22f;
	V3 T (0, sinf (a), cosf (a)), U (0, cosf (a), -sinf (a));
	float R = g.bodyR, L = g.bodyLen;
	V3 hip (0, 0, 0);
	// la queue : vers l'arrière, elle s'enroule vers le haut (tailCurl > 0) ou le bas
	float tl = fmaxf (g.tailLen, 0.04f);
	V3 tb = hip - T * (R * 0.45f);
	float el = lerpf (0.12f, -0.3f, g.upright) + g.tailCurl * 0.15f;
	V3 tp[4]; tp[0] = tb;
	for (int i = 1; i < 4; i++)
	{
		el += g.tailCurl * 0.42f;
		tp[i] = tp[i - 1] + V3 (0, sinf (el), -cosf (el)) * (tl / 3);
	}
	float tr = R * g.tailR;
	k[K_TAILBASE] = { tb, R * 0.82f, R * 0.82f * g.bodyH, K_TAILBASE };
	k[K_TAIL1] = { tp[1], lerpf (R * 0.7f, tr * 0.8f, smooth (0, 0.3f, tl)), 0, K_TAIL1 };
	k[K_TAIL2] = { tp[2], lerpf (R * 0.5f, tr * 0.55f, smooth (0, 0.3f, tl)), 0, K_TAIL2 };
	k[K_TAILTIP] = { tp[3], lerpf (R * 0.3f, tr * 0.28f, smooth (0, 0.3f, tl)), 0, K_TAILTIP };
	for (int i = K_TAILTIP; i <= K_TAIL1; i++) k[i].ry = k[i].rx;
	// le tronc ; il respire
	float br = 1 + 0.028f * breathe * sinf (t * 2.3f);
	k[K_HIP] = { hip, R * 0.95f * br, R * 0.95f * g.bodyH * br, K_HIP };
	k[K_MID] = { hip + T * (L * 0.5f) + U * (g.hump * L * 0.55f - g.belly * R * 0.2f), R * (1 + g.belly * 0.55f) * br,
		     R * (1 + g.belly * 0.7f) * g.bodyH * br, K_MID };
	k[K_SH] = { hip + T * L, R * 0.93f * br, R * 0.93f * g.bodyH * br, K_SH };
	// le cou, la tête, le museau
	float be = lerpf (0.62f, 1.32f, g.upright);
	V3 dn (0, sinf (be), cosf (be));
	V3 nb = k[K_SH].p + T * (R * 0.3f) + dn * (R * 0.22f);
	float rn = R * lerpf (0.82f, g.neckR, smooth (0, 0.22f, g.neckLen));
	k[K_NECK] = { nb, rn, rn, K_NECK };
	V3 nt = nb + dn * (g.neckLen + 0.02f);
	float rnt = lerpf (R * g.neckR, g.headR, 0.45f) * 0.86f;
	k[K_NECKTOP] = { nt, rnt, rnt, K_NECKTOP };
	V3 hd = norm (V3 (0, -0.1f - g.snout * 0.15f, 1));
	V3 hc = nt + dn * (g.headR * 0.42f) + hd * (g.headR * 0.22f);
	k[K_HEAD] = { hc, g.headR * g.headW, g.headR * g.headFlat, K_HEAD };
	float rs = g.headR * lerpf (0.66f, 0.36f, sat (g.snout / 0.45f));
	V3 sn = hc + hd * (g.headR * 0.55f + g.snout * 0.72f) - V3 (0, g.snout * 0.06f, 0);
	k[K_SNOUT] = { sn, rs * g.headW, rs * g.headFlat, K_SNOUT };
	k[K_NOSE] = { sn + hd * (rs * 0.55f), rs * 0.5f * g.headW, rs * 0.5f * g.headFlat, K_NOSE };

	// les clés trop proches se fondent (le bébé : presque une boule)
	Key kk[K_N]; int n = 0;
	int map[K_N];
	for (int i = 0; i < K_N; i++)
	{
		if (n > 0 && len (k[i].p - kk[n - 1].p) < 0.035f)
		{
			kk[n - 1].rx = fmaxf (kk[n - 1].rx, k[i].rx); kk[n - 1].ry = fmaxf (kk[n - 1].ry, k[i].ry);
			kk[n - 1].p = lerp (kk[n - 1].p, k[i].p, 0.5f);
			map[i] = n - 1;
			continue;
		}
		kk[n] = k[i]; map[i] = n; n++;
	}
	// échantillons fins (Catmull-Rom), longueur d'arc
	const int SUB = 14;
	std::vector<V3> P; std::vector<float> RX, RY, S; std::vector<int> kat (n);
	for (int i = 0; i < n - 1; i++)
	{
		const Key &k0 = kk[i > 0 ? i - 1 : 0], &k1 = kk[i], &k2 = kk[i + 1], &k3 = kk[i + 2 < n ? i + 2 : n - 1];
		kat[i] = (int) P.size ();
		for (int s = 0; s < SUB; s++)
		{
			float u = (float) s / SUB, u2 = u * u, u3 = u2 * u;
			auto cr = [&] (float a0, float a1, float a2, float a3) {
				float v = 0.5f * (2 * a1 + (a2 - a0) * u + (2 * a0 - 5 * a1 + 4 * a2 - a3) * u2 + (3 * a1 - a0 - 3 * a2 + a3) * u3);
				float lo = fminf (a1, a2), hi = fmaxf (a1, a2);
				return clampf (v, lo * 0.85f, hi * 1.08f);
			};
			V3 q = (k1.p * 2 + (k2.p - k0.p) * u + (k0.p * 2 - k1.p * 5 + k2.p * 4 - k3.p) * u2 + (k1.p * 3 - k0.p - k2.p * 3 + k3.p) * u3) * 0.5f;
			P.push_back (q); RX.push_back (cr (k0.rx, k1.rx, k2.rx, k3.rx)); RY.push_back (cr (k0.ry, k1.ry, k2.ry, k3.ry));
		}
	}
	kat[n - 1] = (int) P.size ();
	P.push_back (kk[n - 1].p); RX.push_back (kk[n - 1].rx); RY.push_back (kk[n - 1].ry);
	S.assign (P.size (), 0);
	for (size_t i = 1; i < P.size (); i++) S[i] = S[i - 1] + len (P[i] - P[i - 1]);
	float tot = fmaxf (S.back (), 1e-4f);
	for (int i = 0; i < K_N; i++) { b.sk[i] = S[kat[map[i]]] / tot; b.kp[i] = k[i].p; b.kr[i] = (k[i].rx + k[i].ry) * 0.5f; }
	// les anneaux, à pas égal (plus serrés quand le corps est court)
	int NR = clampi ((int) (tot / 0.042f), 22, 60);
	b.r.resize ((size_t) NR);
	size_t j = 0;
	for (int i = 0; i < NR; i++)
	{
		float want = tot * (float) i / (float) (NR - 1);
		while (j + 1 < P.size () - 1 && S[j + 1] < want) j++;
		float f = (want - S[j]) / fmaxf (S[j + 1] - S[j], 1e-6f);
		Ring &g2 = b.r[(size_t) i];
		g2.c = lerp (P[j], P[j + 1], f);
		g2.rx = lerpf (RX[j], RX[j + 1], f); g2.ry = lerpf (RY[j], RY[j + 1], f);
		g2.s = want / tot;
	}
	for (int i = 0; i < NR; i++)
	{
		Ring &g2 = b.r[(size_t) i];
		const Ring &p0 = b.r[(size_t) (i > 0 ? i - 1 : 0)], &p1 = b.r[(size_t) (i < NR - 1 ? i + 1 : NR - 1)];
		g2.T = norm (p1.c - p0.c);
		g2.X = V3 (1, 0, 0);
		g2.N = norm (cross (g2.T, g2.X));
		g2.dr = ((p1.rx + p1.ry) - (p0.rx + p0.ry)) * 0.5f / fmaxf (len (p1.c - p0.c), 1e-4f);
	}
	b.hd = hd; b.up = norm (cross (hd, V3 (1, 0, 0)));
	b.headRx = g.headR * g.headW; b.headRy = g.headR * g.headFlat; b.snoutR = rs;
	float mn = 1e9f;
	for (const Ring &g2 : b.r) mn = fminf (mn, g2.c.y - fabsf (g2.N.y) * g2.ry - fabsf (g2.T.y) * 0.0f);
	mn = fminf (mn, b.r[0].c.y - (b.r[0].rx + b.r[0].ry) * 0.5f);
	b.minY = mn;
}

// la distance (approchée) à la surface du corps au repos : l'union des ellipsoïdes des anneaux
static float sdf (const Body &b, const V3 &q, int *near = 0)
{
	float best = 1e9f; int bi = 0;
	for (size_t i = 0; i < b.r.size (); i++)
	{
		const Ring &g = b.r[i];
		V3 d = q - g.c;
		float rz = (g.rx + g.ry) * 0.5f;
		float ex = dot (d, g.X) / g.rx, ey = dot (d, g.N) / g.ry, ez = dot (d, g.T) / rz;
		float k = sqrtf (ex * ex + ey * ey + ez * ez);
		float m = fminf (g.rx, fminf (g.ry, rz));
		float dd = (k - 1) * m;
		if (dd < best) { best = dd; bi = (int) i; }
	}
	if (near) *near = bi;
	return best;
}
// depuis o (dedans), le long de dir : le point de la surface, sa normale, son abscisse
static bool hit (const Body &b, const V3 &o, const V3 &dir, V3 &p, V3 &n, float *s = 0)
{
	V3 d = norm (dir);
	float t0 = 0, t1 = 0.02f;
	if (sdf (b, o) > 0) { p = o; n = d; if (s) *s = 0.5f; return false; }
	int guard = 0;
	while (sdf (b, o + d * t1) < 0 && guard++ < 80) { t0 = t1; t1 += 0.02f; }
	for (int i = 0; i < 14; i++) { float m = (t0 + t1) * 0.5f; if (sdf (b, o + d * m) < 0) t0 = m; else t1 = m; }
	p = o + d * t1;
	const float e = 0.004f;
	n = norm (V3 (sdf (b, p + V3 (e, 0, 0)) - sdf (b, p - V3 (e, 0, 0)), sdf (b, p + V3 (0, e, 0)) - sdf (b, p - V3 (0, e, 0)),
		      sdf (b, p + V3 (0, 0, e)) - sdf (b, p - V3 (0, 0, e))));
	if (s) { int ni; sdf (b, p, &ni); *s = b.r[(size_t) ni].s; }
	return true;
}

// ==== la peau =========================================================================================================
static uint32_t argb (const Col &c) { return 0xFF000000u | packc (c); }

void makeSkin (const Genome &g, uint32_t *px)
{
	Body b; layout (g, 0, 0, b);
	float sHead = b.sk[K_HEAD], sNose = b.sk[K_NOSE];
	int sv = clampi ((int) (6 * g.patScale + 0.5f), 3, 10);		// (v se répète : un nombre entier de cellules)
	float su = 15 * g.patScale;
	for (int y = 0; y < SKIN_H; y++)
		for (int x = 0; x < SKIN_W; x++)
		{
			float u = ((float) x + 0.5f) / (float) SKIN_W, v = ((float) y + 0.5f) / (float) SKIN_H;
			float dv = fminf (v, 1 - v);				// 0 : la ligne du ventre, 0.5 : le dos
			float bellyW = 0.2f + g.belly * 0.2f;
			float wob = (vnoise (u * 22, v * 8, (int) g.seed & 255, 8) - 0.5f) * 0.05f;
			float bf = 1 - smooth (bellyW * 0.55f, bellyW, dv + wob);
			// (pas de ventre au bout de la queue, un menton plus clair)
			bf *= smooth (0.0f, 0.12f, u);
			Col c = mixc (g.base, g.bellyC, bf);
			float m = 0;
			switch (g.pattern)
			{
			case PAT_SPOTS:
			{
				float cu = u * su, cv = v * (float) sv;
				int iu = (int) floorf (cu), iv = (int) floorf (cv);
				float best = 9;
				for (int a = -1; a <= 1; a++)
					for (int bb = -1; bb <= 1; bb++)
					{
						int gu = iu + a, gv = ((iv + bb) % sv + sv) % sv;
						float ox = hashf (gu, gv, 11 + (int) g.seed) * 0.7f + 0.15f, oy = hashf (gu, gv, 23 + (int) g.seed) * 0.7f + 0.15f;
						float rr = 0.22f + 0.2f * hashf (gu, gv, 37 + (int) g.seed);
						float dx = cu - ((float) (iu + a) + ox), dy = cv - ((float) (iv + bb) + oy);
						float dd = sqrtf (dx * dx + dy * dy) / rr;
						if (dd < best) best = dd;
					}
				m = 1 - smooth (0.8f, 1.0f, best);
				m *= 1 - bf;
				break;
			}
			case PAT_STRIPES:
			{
				float w = (vnoise (u * 9, v * 6, 7 + (int) g.seed, 6) - 0.5f) * 1.4f;
				float sgn = sinf ((u * su * 0.55f + w) * 2 * PI);
				m = smooth (0.25f, 0.55f, sgn) * smooth (0.07f, 0.24f, dv);
				break;
			}
			case PAT_PATCHES:
			{
				float nn = vnoise (u * 9, v * 6, 3 + (int) g.seed, 6) * 0.65f + vnoise (u * 20, v * 12, 5 + (int) g.seed, 12) * 0.35f;
				m = smooth (0.5f, 0.56f, nn) * (1 - bf * 0.8f);
				break;
			}
			case PAT_SADDLE:
			{
				float nn = (vnoise (u * 16, v * 8, 9 + (int) g.seed, 8) - 0.5f) * 0.1f;
				m = smooth (0.27f, 0.4f, dv + nn) * smooth (0.04f, 0.2f, u) * (1 - smooth (sHead, sNose, u) * 0.7f);
				break;
			}
			case PAT_RINGS:
			{
				float sgn = sinf (u * su * 0.9f * 2 * PI);
				m = smooth (0.55f, 0.75f, sgn) * smooth (0.05f, 0.2f, dv) * (1 - smooth (sHead - 0.05f, sHead, u));
				break;
			}
			case PAT_SPECKLE:
			{
				int cx = x / 3, cy = y / 3;
				float h = hashf (cx, cy, 51 + (int) g.seed);
				float ox = ((float) (x % 3) - 1) / 1.5f, oy = ((float) (y % 3) - 1) / 1.5f;
				m = h > 0.8f ? (1 - smooth (0.4f, 1.0f, sqrtf (ox * ox + oy * oy))) : 0;
				m *= (1 - bf) * 0.9f;
				break;
			}
			default: break;
			}
			c = mixc (c, g.pat, m);
			// les joues (bébé)
			if (g.blush > 0)
				for (int side = 0; side < 2; side++)
				{
					float bu = (u - (sHead + (sNose - sHead) * 0.35f)) / 0.035f, bv = (v - (side ? 0.68f : 0.32f)) / 0.055f;
					float k = expf (-(bu * bu + bv * bv) * 1.5f) * g.blush * 0.5f;
					c = mixc (c, Col (1, 0.5f, 0.55f), k);
				}
			// le grain : un peu de vie
			float grain = 0.95f + 0.07f * vnoise ((float) x * 0.35f, (float) y * 0.35f, 99, SKIN_W * 35 / 100) + 0.03f * (hashf (x, y, 7) - 0.5f);
			px[y * SKIN_W + x] = argb (c * grain);
		}
}

void makeShell (const Genome &g, uint32_t *px)
{
	Col shell = mixc (g.bellyC, Col (1, 0.98f, 0.94f), 0.55f);
	for (int y = 0; y < SKIN_H; y++)
		for (int x = 0; x < SKIN_W; x++)
		{
			float u = ((float) x + 0.5f) / (float) SKIN_W, v = ((float) y + 0.5f) / (float) SKIN_H;	// u : le tour, v : du bas en haut
			Col c = shell;
			if (g.pattern == PAT_STRIPES)
			{
				float w = (vnoise (u * 12, v * 4, 3, 12) - 0.5f) * 0.12f;
				float b1 = fabsf (v - 0.42f + w), b2 = fabsf (v - 0.6f - w * 0.6f);
				c = mixc (c, g.base, 1 - smooth (0.03f, 0.05f, b1));
				c = mixc (c, g.pat, 1 - smooth (0.02f, 0.035f, b2));
			}
			else
			{
				float cu = u * 9, cv = v * 6;
				int iu = (int) floorf (cu), iv = (int) floorf (cv);
				for (int a = -1; a <= 1; a++)
					for (int bb = -1; bb <= 1; bb++)
					{
						int gu = ((iu + a) % 9 + 9) % 9, gv = iv + bb;
						float ox = hashf (gu, gv, 5) * 0.7f + 0.15f, oy = hashf (gu, gv, 6) * 0.7f + 0.15f;
						float rr = 0.18f + 0.18f * hashf (gu, gv, 8);
						float dx = cu - ((float) (iu + a) + ox), dy = (cv - ((float) (iv + bb) + oy)) * 1.4f;
						float dd = sqrtf (dx * dx + dy * dy) / rr;
						Col sc = hashf (gu, gv, 9) < 0.5f ? g.base : g.pat;
						c = mixc (c, sc, 1 - smooth (0.75f, 1.0f, dd));
					}
			}
			float grain = 0.96f + 0.05f * vnoise ((float) x * 0.4f, (float) y * 0.4f, 17, SKIN_W * 4 / 10);
			px[y * SKIN_W + x] = argb (c * grain);
		}
}

// ==== la cinématique inverse : deux os de h vers f, genou du côté de pole ================================================
static V3 ik (const V3 &h, const V3 &f, float l1, float l2, const V3 &pole)
{
	V3 d = f - h;
	float dl = len (d);
	float mx = (l1 + l2) * 0.999f;
	if (dl > mx) dl = mx;
	if (dl < 1e-4f) dl = 1e-4f;
	V3 dir = norm (d);
	float a = (l1 * l1 - l2 * l2 + dl * dl) / (2 * dl);
	float hh = sqrtf (fmaxf (l1 * l1 - a * a, 0));
	V3 pp = pole - dir * dot (pole, dir);
	if (len (pp) < 1e-4f) pp = V3 (0, 0, 1);
	return h + dir * a + norm (pp) * hh;
}

// ==== la créature =====================================================================================================
static Col dark (const Col &c, float k) { return c * k; }
static Col beakColor (const Genome &g) { return luma (g.accent) < 0.35f ? Col (0.96f, 0.64f, 0.22f) : g.accent; }

void buildCreature (const Genome &g, const Pose &P, CreatureMeshes &M, CreatureOut &out)
{
	Body b; layout (g, P.breathe, P.t, b);
	const PartScale &ps = P.ps;
	float sHip = b.sk[K_HIP], sMid = b.sk[K_MID], sSh = b.sk[K_SH], sNeck = b.sk[K_NECK], sHead = b.sk[K_HEAD];
	V3 hipP = b.kp[K_HIP], nb = b.kp[K_NECK], hc = b.kp[K_HEAD], sn = b.kp[K_SNOUT];

	// ---- les jambes d'abord : la hauteur du corps
	struct Leg { V3 h; float side; bool arm; };
	Leg legs[8]; int nl = 0;
	float legL = g.legLen * ps.legs, legR = g.legR * (0.4f + 0.6f * ps.legs);
	float footH = legR * 0.7f;
	if (g.legs > 0 && ps.legs > 0.02f)
		for (int i = 0; i < g.legs; i++)
		{
			float s;
			if (g.legs == 1) s = g.upright > 0.5f ? sHip + (sMid - sHip) * 0.15f : lerpf (sHip, sMid, 0.55f);
			else s = lerpf (sHip + (sMid - sHip) * 0.12f, sSh - (sSh - sMid) * 0.1f, (float) i / (float) (g.legs - 1));
			Ring rg = ringAt (b, s);
			for (int side = -1; side <= 1; side += 2)
			{
				Leg &l = legs[nl++];
				l.h = rg.c + rg.X * (rg.rx * 0.6f * (float) side) - rg.N * (rg.ry * 0.42f);
				l.side = (float) side; l.arm = false;
			}
		}
	float rest = -b.minY - 0.012f;
	float lift = rest;
	if (nl)
	{
		float best = 1e9f;
		for (int i = 0; i < nl; i++) best = fminf (best, legL * 0.9f + footH - legs[i].h.y);
		lift = fmaxf (best, rest);
	}
	lift = lerpf (lift, rest - 0.02f, P.lie);

	// ---- la place dans le monde
	float sz = g.size;
	float sy = P.squash, sxz = 1 / sqrtf (fmaxf (P.squash, 0.2f));
	Xf G = Xf::trans (P.pos + V3 (P.shiver * 0.012f * sinf (P.t * 61), P.hop, P.shiver * 0.008f * sinf (P.t * 47)))
	     * Xf::rotY (P.yaw) * Xf::scale (sz * sxz, sz * sy, sz * sxz) * Xf::trans (V3 (0, lift, 0));
	// la tête tourne autour de la base du cou ; la queue frétille autour de la hanche
	auto headXf = [&] (float w) {
		Xf r = Xf::rotY (P.headYaw * w) * Xf::rotX (-P.headPitch * w) * Xf::rotZ (P.headRoll * w);
		return Xf::about (nb, r);
	};
	float wagT = P.t * 7.5f;
	auto tailXf = [&] (float s) {
		float w = 1 - smooth (0, sHip, s);
		float ang = (P.wag * 0.55f * sinf (wagT + w * 2.2f) + 0.06f * sinf (P.t * 1.3f)) * w;
		return Xf::about (hipP, Xf::rotY (ang));
	};
	auto ringXf = [&] (float s) {
		float wh = smooth (sNeck - 0.02f, sHead, s);
		return G * headXf (wh) * tailXf (s);
	};
	Xf HX = G * headXf (1);

	Col tint = mixc (Col (1, 1, 1), Col (0.72f, 0.92f, 0.55f), P.sick * 0.6f);
	tint = mixc (tint, Col (0.75f, 0.66f, 0.55f), P.dirt * 0.35f);

	// ---- le corps : anneaux + bouts arrondis, texture u = s, v = le tour
	{
		Builder B (&M.body);
		B.tint = tint; B.gloss = g.gloss;
		const int SEG = 26;
		struct R2 { Ring g; float sc, off, nt; float u; };
		std::vector<R2> rr;
		const int CAP = 4;
		for (int q = CAP; q >= 1; q--)
		{
			float ph = (PI / 2) * (float) q / CAP;
			rr.push_back ({ b.r.front (), cosf (ph), -sinf (ph), -sinf (ph), 0 });
		}
		for (const Ring &x : b.r) rr.push_back ({ x, 1, 0, 0, x.s });
		for (int q = 1; q <= CAP; q++)
		{
			float ph = (PI / 2) * (float) q / CAP;
			rr.push_back ({ b.r.back (), cosf (ph), sinf (ph), sinf (ph), 1 });
		}
		int base = (int) M.body.v.size ();
		for (size_t i = 0; i < rr.size (); i++)
		{
			const R2 &q = rr[i];
			B.xf = ringXf (q.g.s);
			float rz = (q.g.rx + q.g.ry) * 0.5f;
			for (int j = 0; j <= SEG; j++)
			{
				float th = 2 * PI * (float) j / SEG, st = sinf (th), ct = cosf (th);
				V3 p = q.g.c + q.g.X * (q.g.rx * st * q.sc) - q.g.N * (q.g.ry * ct * q.sc) + q.g.T * (q.off * rz);
				V3 en = norm (q.g.X * (st / q.g.rx) - q.g.N * (ct / q.g.ry));
				V3 n = q.off != 0 ? norm (en * q.sc + q.g.T * q.nt) : norm (en - q.g.T * clampf (q.g.dr, -2.5f, 2.5f));
				B.add (p, n, Col (1, 1, 1), q.u, (float) j / SEG);
			}
		}
		B.grid (base, (int) rr.size () - 1, SEG);
	}

	Builder B (&M.parts);
	B.tint = tint;
	Builder S (&M.sheets); S.tint = tint;
	Builder A (&M.alpha); A.tint = tint;
	Col skin = g.base, skinLo = mixc (g.base, g.pat, 0.25f) * 0.86f;
	V3 X (1, 0, 0);
	V3 hd = b.hd, up = b.up;

	// ---- les jambes (la hauteur du corps est dans leurs points : G sans elle)
	Xf G0 = G * Xf::trans (V3 (0, -lift, 0));
	B.xf = G0;
	for (int i = 0; i < nl; i++)
	{
		Leg &l = legs[i];
		V3 h = l.h + V3 (0, 0, 0);
		int pair = i / 2;
		float ph = P.walkPhase + (l.side > 0 ? 0 : PI) + (float) pair * PI;
		float stride = legL * 0.32f * P.walkAmt;
		V3 hw = h + V3 (0, lift, 0);					// (la hanche, hauteur comprise)
		V3 f (h.x * 1.12f, 0, h.z + sinf (ph) * stride);
		f.y = fmaxf (0, cosf (ph)) * legL * 0.22f * P.walkAmt;
		if (P.lie > 0)
		{
			V3 fl (h.x + l.side * legL * 0.35f, 0.02f, h.z + legL * 0.45f);
			f = lerp (f, fl, P.lie);
		}
		f.y -= 0;	// (le sol est à y = 0 : le saut emporte tout, pieds compris)
		V3 ft = f + V3 (0, footH, 0);
		V3 pole = lerp (V3 (l.side * 0.25f, 0, 1), V3 (l.side, 0.3f, 0.4f), P.lie);
		V3 knee = ik (hw, ft, legL * 0.52f, legL * 0.48f, pole);
		// (si la jambe est trop courte, le pied pend sous le genou)
		V3 dk = ft - knee; if (len (dk) > legL * 0.48f) ft = knee + norm (dk) * (legL * 0.48f);
		V3 in = hw - V3 (l.side * legR * 0.9f, -legR * 0.4f, 0);
		V3 pts[4] = { in, hw, knee, ft };
		float rad[4] = { legR * 1.55f, legR * 1.3f, legR * 0.95f, legR * 0.78f };
		Col cols[4] = { skin, skin, mixc (skin, skinLo, 0.5f), skinLo };
		B.gloss = g.gloss * 0.8f;
		B.tube (pts, rad, cols, 4, 10, 4, 0, X, 3);
		// le pied
		Frame ff = frameLook (ft + V3 (0, -footH * 0.25f, legR * 0.45f), V3 (l.side * 0.08f, 0, 1), V3 (0, 1, 0));
		Col fc = g.foot == FOOT_HOOF ? dark (g.accent, 0.7f) : skinLo;
		if (g.foot == FOOT_HOOF) B.ellipsoid (ff, V3 (legR * 1.05f, footH * 1.05f, legR * 1.15f), fc, dark (fc, 0.7f), 12, 6);
		else
		{
			B.ellipsoid (ff, V3 (legR * 1.18f, footH * 0.95f, legR * 1.65f), fc, dark (fc, 0.8f), 12, 7);
			for (int t = -1; t <= 1; t++)
			{
				V3 tp = ff.at (V3 ((float) t * legR * 0.62f, -footH * 0.2f, legR * 1.35f));
				if (g.foot == FOOT_CLAW)
				{
					V3 cp[2] = { tp, tp + ff.dir (V3 ((float) t * 0.1f, -0.35f, 1)) * (legR * 1.1f) };
					float cr[2] = { legR * 0.32f, legR * 0.04f };
					Col cc[2] = { g.accent, mixc (g.accent, Col (1, 1, 1), 0.3f) };
					B.gloss = 0.7f; B.tube (cp, cr, cc, 2, 6, 3, 0, X, 1);
				}
				else B.sphere (tp, legR * 0.45f, fc, 8, 5);
			}
		}
	}

	// ---- les bras (créatures debout)
	if (g.arms && ps.arms > 0.02f)
	{
		Ring rg = ringAt (b, sSh - (sSh - sMid) * 0.25f);
		float al = g.armLen * ps.arms, ar = g.legR * 0.8f * (0.4f + 0.6f * ps.arms);
		for (int side = -1; side <= 1; side += 2)
		{
			V3 sh = rg.c + rg.X * (rg.rx * 0.78f * (float) side) + rg.N * (rg.ry * 0.05f);
			V3 shw = sh + V3 (0, lift, 0);
			V3 hand = shw + V3 ((float) side * 0.1f, -al * 0.82f, 0.1f + 0.03f * sinf (P.t * 1.7f + (float) side));
			if (P.wave > 0 && side > 0)
				hand = lerp (hand, shw + V3 (al * 0.4f + 0.06f * sinf (P.t * 11), al * 0.55f, 0.12f), P.wave);
			if (P.reach > 0)
				hand = lerp (hand, shw + V3 (-(float) side * 0.06f, -al * 0.1f, al * 0.85f), P.reach);
			B.xf = G0;
		V3 elbow = ik (shw, hand, al * 0.5f, al * 0.5f, V3 ((float) side * 0.6f, -0.2f, -1));
			V3 in = shw - V3 ((float) side * ar, 0, 0);
			V3 pts[4] = { in, shw, elbow, hand };
			float rad[4] = { ar * 1.5f, ar * 1.25f, ar * 0.95f, ar * 0.8f };
			Col cols[4] = { skin, skin, mixc (skin, skinLo, 0.4f), skinLo };
			B.gloss = g.gloss * 0.8f;
			B.tube (pts, rad, cols, 4, 9, 4, 0, X, 3);
			B.sphere (hand + norm (hand - elbow) * (ar * 0.6f), ar * 1.25f, skinLo, 10, 7);
		}
	}

	// ---- la tête : yeux, bouche, cornes, oreilles (tout suit la tête)
	B.xf = HX; S.xf = HX; A.xf = HX;
	float es = ps.eyes;
	// les yeux
	{
		struct EyeDef { V3 d; float k; };
		EyeDef E[4]; int ne = 0;
		float sp = lerpf (0.35f, 0.95f, g.eyeSpread);
		switch (g.eyes)
		{
		case 1: E[ne++] = { V3 (0, 0.42f, 0.9f), 1.35f }; break;
		case 3: E[ne++] = { V3 (0, 0.62f, 0.8f), 1.1f }; E[ne++] = { V3 (-0.72f, 0.28f, 0.66f), 0.8f }; E[ne++] = { V3 (0.72f, 0.28f, 0.66f), 0.8f }; break;
		case 4: E[ne++] = { V3 (-0.42f, 0.48f, 0.82f), 1 }; E[ne++] = { V3 (0.42f, 0.48f, 0.82f), 1 };
			E[ne++] = { V3 (-0.8f, 0.08f, 0.6f), 0.7f }; E[ne++] = { V3 (0.8f, 0.08f, 0.6f), 0.7f }; break;
		default: E[ne++] = { V3 (-sinf (sp), 0.38f, cosf (sp)), 1 }; E[ne++] = { V3 (sinf (sp), 0.38f, cosf (sp)), 1 }; break;
		}
		V3 hx = norm (cross (up, hd));
		for (int e = 0; e < ne; e++)
		{
			float R = g.eyeR * E[e].k * es;
			if (R < 0.004f) continue;
			V3 dl = hx * E[e].d.x + up * E[e].d.y + hd * E[e].d.z;
			V3 sp0, sn0;
			hit (b, hc, dl, sp0, sn0);
			V3 ec = sp0 - sn0 * (R * 0.3f);
			if (g.eyeStalk)
			{
				V3 top = sp0 + norm (sn0 + up * 0.8f) * (R * 2.2f);
				V3 pts[3] = { sp0 - sn0 * 0.02f, lerp (sp0, top, 0.5f) + hd * 0.01f, top };
				float rad[3] = { R * 0.4f, R * 0.3f, R * 0.32f };
				Col cols[3] = { skin, skin, skin };
				B.gloss = g.gloss; B.tube (pts, rad, cols, 3, 8, 3, 0, X, 1);
				ec = top + norm (sn0 + up) * (R * 0.6f);
			}
			V3 fwd = norm (hd * 0.72f + sn0 * 0.55f);
			Frame ef = frameLook (ec, fwd, up);
			// le regard : le point visé, ramené dans la pièce, retenu dans un cône
			V3 ew = HX.p (ec);
			V3 gz = HX.inv (norm (P.gaze - ew));
			// (au plus 0.5 radian du devant de l'œil : l'iris reste visible)
			float cg = clampf (dot (gz, fwd), -1, 1);
			const float MAXA = 0.5f;
			if (acosf (cg) > MAXA)
			{
				V3 ax = cross (fwd, gz);
				if (len (ax) < 1e-4f) ax = up;
				gz = norm (rotAxis (fwd, norm (ax), MAXA));
			}
			float blink = fmaxf (P.blink, 0);
			B.gloss = 0.95f;
			B.ellipsoid (ef, V3 (R, R, R), Col (0.98f, 0.98f, 0.99f), Col (0.86f, 0.86f, 0.9f), 16, 10);
			Frame irf = frameLook (ec + gz * (R * 0.8f), gz, up);
			B.gloss = 0.9f;
			B.ellipsoid (irf, V3 (R * 0.63f, R * 0.63f, R * 0.25f), mixc (g.iris, Col (0, 0, 0), 0.35f), mixc (g.iris, Col (1, 1, 1), 0.25f), 14, 7);
			Frame pf = frameLook (ec + gz * (R * 0.92f), gz, up);
			float pr = R * 0.31f * clampf (P.pupil, 0.6f, 1.5f);
			B.ellipsoid (pf, V3 (pr, pr, R * 0.2f), Col (0.03f, 0.03f, 0.05f), Col (0.06f, 0.05f, 0.08f), 12, 6);
			B.emit = 1; B.gloss = 0;
			B.sphere (ec + norm (gz * 0.9f + up * 0.48f - ef.x * 0.3f) * (R * 1.02f), R * 0.14f, Col (1, 1, 1), 8, 5);
			B.sphere (ec + norm (gz * 0.95f - up * 0.3f + ef.x * 0.35f) * (R * 1.0f), R * 0.06f, Col (1, 1, 1), 6, 4);
			B.emit = 0;
			// la paupière
			if (blink > 0.02f)
			{
				float c = clampf (blink, 0, 1);
				Frame lf = ef; lf.o = ec + ef.y * (R * 1.07f * (1 - c));
				B.gloss = g.gloss * 0.6f;
				B.ellipsoid (lf, V3 (R * 1.09f, fmaxf (R * 1.08f * c, R * 0.02f), R * 1.09f), skin, dark (skin, 0.85f), 16, 8);
				if (c > 0.7f)
				{
					V3 pts[7]; float rad[7]; Col cols[7];
					float bend = P.lidHappy > 0.5f ? 0.22f : -0.14f;
					for (int k = 0; k < 7; k++)
					{
						float a = -1.15f + 2.3f * (float) k / 6;
						pts[k] = ec + ef.x * (R * 1.1f * sinf (a)) + ef.z * (R * 1.1f * cosf (a)) + ef.y * (R * (bend * cosf (a * 1.2f) - bend * 0.3f - 0.05f));
						rad[k] = R * 0.075f; cols[k] = Col (0.12f, 0.08f, 0.1f);
					}
					B.gloss = 0.2f; B.tube (pts, rad, cols, 7, 5, 2, 0, X, 3);
				}
			}
			// les sourcils
			if (g.brows && !g.eyeStalk && ps.brows > 0.02f && E[e].d.y > 0.2f)
			{
				float side = E[e].d.x >= 0 ? 1.0f : -1.0f;
				float sad = P.mood < 0 ? -P.mood : 0;
				float ang = side * (P.anger * 0.55f - sad * 0.5f) + (E[e].d.x == 0 ? 0 : 0);
				Frame bf = ef; bf.o = ec + ef.y * (R * (1.38f + 0.15f * (P.mood > 0.6f ? P.mood - 0.6f : 0))) + ef.z * (R * 0.4f);
				V3 bx = rotAxis (ef.x, ef.z, ang), by = rotAxis (ef.y, ef.z, ang);
				bf.x = bx; bf.y = by;
				B.gloss = 0.2f;
				Col bc = dark (g.pat, 0.55f);
				B.ellipsoid (bf, V3 (R * 0.62f * ps.brows, R * 0.14f * ps.brows, R * 0.22f), bc, bc, 10, 6);
			}
		}
	}
	// la bouche
	float ms = ps.mouth;
	V3 mouthP = sn;
	if (ms > 0.02f)
	{
		float rs = b.snoutR;
		V3 hx = norm (cross (up, hd));
		V3 dirM = norm (hd * 0.78f - up * 0.6f);
		V3 pm, nm;
		hit (b, sn, dirM, pm, nm);
		mouthP = pm;
		bool smile = g.mouth == MOUTH_SMILE || g.mouth == MOUTH_FANGS || g.mouth == MOUTH_TRUNK;
		float open = P.mouthOpen;
		if (smile)
		{
			float w = 0.5f * g.mouthSize * (g.mouth == MOUTH_TRUNK ? 0.6f : 1) * ms;
			float curv = 0.28f * P.mood;
			V3 pts[9]; float rad[9]; Col cols[9];
			for (int k = 0; k < 9; k++)
			{
				float f = -1 + 2 * (float) k / 8;
				V3 d = norm (rotAxis (dirM, up, f * w) + up * (curv * f * f));
				V3 p, n; hit (b, sn, d, p, n);
				pts[k] = p + n * 0.002f;
				rad[k] = (0.0105f + 0.003f * (1 - f * f)) * g.mouthSize * fminf (1, rs * 6);
				cols[k] = Col (0.2f, 0.07f, 0.09f);
			}
			if (open < 0.35f) { B.gloss = 0.3f; B.tube (pts, rad, cols, 9, 6, 2, 0, X, 3); }
			if (open > 0.05f)
			{
				Frame mf = frameLook (pm + nm * 0.004f, nm, up);
				float mw = rs * 0.48f * g.mouthSize, mh = rs * (0.05f + 0.42f * open);
				B.gloss = 0.4f;
				B.ellipsoid (mf, V3 (mw, mh, 0.025f), Col (0.32f, 0.06f, 0.1f), Col (0.22f, 0.04f, 0.07f), 14, 7);
				Frame tf = mf; tf.o = mf.o - mf.y * (mh * 0.45f) + mf.z * 0.006f;
				B.ellipsoid (tf, V3 (mw * 0.6f, mh * 0.42f, 0.02f), Col (0.98f, 0.5f, 0.55f), Col (0.85f, 0.38f, 0.45f), 10, 5);
			}
			if (g.mouth == MOUTH_FANGS)
				for (int k = 2; k <= 6; k += 4)
				{
					V3 fp[2] = { pts[k] - nm * 0.004f, pts[k] - up * (0.05f * g.mouthSize * ms) + nm * 0.006f };
					float fr[2] = { 0.013f * g.mouthSize, 0.0015f };
					Col fc[2] = { Col (0.98f, 0.97f, 0.92f), Col (1, 1, 1) };
					B.gloss = 0.8f; B.tube (fp, fr, fc, 2, 6, 2, 0, X, 1);
				}
			// le nez
			if (g.snout > 0.1f && g.mouth != MOUTH_TRUNK)
			{
				V3 np, nn; hit (b, sn, norm (hd + up * 0.25f), np, nn);
				Frame nf = frameLook (np, nn, up);
				B.gloss = 0.9f;
				B.ellipsoid (nf, V3 (rs * 0.24f, rs * 0.17f, rs * 0.14f), Col (0.22f, 0.14f, 0.16f), Col (0.12f, 0.08f, 0.1f), 10, 6);
			}
		}
		if (g.mouth == MOUTH_BEAK)
		{
			Col bc = beakColor (g);
			float bl = 0.17f * g.mouthSize * ms;
			V3 base = sn + hd * (rs * 0.25f);
			V3 up0[3] = { base, base + hd * (rs * 0.6f + bl * 0.5f) - up * (bl * 0.02f), base + hd * (rs * 0.7f + bl) - up * (bl * 0.22f) };
			float ur[3] = { rs * 0.78f, rs * 0.45f, 0.003f };
			Col uc[3] = { bc, bc, mixc (bc, Col (1, 1, 1), 0.2f) };
			B.gloss = 0.75f; B.tube (up0, ur, uc, 3, 12, 4, 0.35f, up, 2);
			V3 hinge = sn - up * (rs * 0.35f) + hd * (rs * 0.2f);
			V3 ld = norm (rotAxis (hd, hx, open * 0.55f) - up * 0.15f);
			V3 lo[3] = { hinge, hinge + ld * (rs * 0.5f + bl * 0.4f), hinge + ld * (rs * 0.55f + bl * 0.75f) };
			float lr[3] = { rs * 0.55f, rs * 0.32f, 0.003f };
			Col lc[3] = { dark (bc, 0.85f), dark (bc, 0.85f), bc };
			B.tube (lo, lr, lc, 3, 10, 4, 0.4f, up, 2);
			mouthP = base + hd * (rs * 0.6f);
		}
		if (g.mouth == MOUTH_MANDIBLES)
			for (int side = -1; side <= 1; side += 2)
			{
				float fs = (float) side;
				V3 base = pm + hx * (fs * rs * 0.42f) + up * (rs * 0.05f);
				float sp = 0.03f + 0.05f * open;
				V3 pts[3] = { base, base + hd * 0.07f * ms + hx * (fs * sp), base + hd * 0.13f * ms - hx * (fs * 0.035f) + hx * (fs * sp * 0.6f) };
				float rad[3] = { 0.024f * g.mouthSize, 0.017f, 0.003f };
				Col cols[3] = { dark (g.accent, 0.8f), g.accent, mixc (g.accent, Col (1, 1, 1), 0.25f) };
				B.gloss = 0.8f; B.tube (pts, rad, cols, 3, 8, 4, 0, X, 3);
			}
		if (g.mouth == MOUTH_TRUNK)
		{
			V3 tp, tn; hit (b, sn, hd, tp, tn);
			float sw = 0.04f * sinf (P.t * 1.4f) + P.reach * 0.08f;
			V3 pts[4] = { tp - hd * (rs * 0.3f), tp + hd * 0.07f - up * 0.015f, tp + hd * (0.12f + sw) - up * 0.1f, tp + hd * (0.13f + sw * 1.5f) - up * (0.2f - P.reach * 0.08f) };
			for (int k = 0; k < 4; k++) pts[k] = pts[k] * 1.0f;
			float rad[4] = { rs * 0.55f, rs * 0.42f, rs * 0.34f, rs * 0.3f };
			Col cols[4] = { skin, skin, mixc (skin, skinLo, 0.5f), skinLo };
			B.gloss = g.gloss; B.tube (pts, rad, cols, 4, 10, 4, 0, X, 3);
			mouthP = pts[3];
		}
	}
	// les cornes
	float hs = ps.horn;
	if (g.horn && hs > 0.02f)
	{
		V3 hx = norm (cross (up, hd));
		float hl = g.hornLen * hs;
		Col ca = g.accent, cb = mixc (g.accent, Col (1, 1, 1), 0.25f), cdk = dark (g.accent, 0.72f);
		if (g.horn == HORN_PAIR || g.horn == HORN_ANTENNAE || g.horn == HORN_RAM)
			for (int side = -1; side <= 1; side += 2)
			{
				float fs = (float) side;
				V3 p, n;
				V3 dd = g.horn == HORN_ANTENNAE ? V3 (fs * 0.35f, 0.92f, 0.12f) : g.horn == HORN_RAM ? V3 (fs * 0.82f, 0.42f, -0.12f) : V3 (fs * 0.55f, 0.8f, -0.05f);
				hit (b, hc, hx * dd.x + up * dd.y + hd * dd.z, p, n);
				if (g.horn == HORN_PAIR)
				{
					V3 pts[6]; float rad[6]; Col cols[6];
					V3 d0 = norm (n * 0.6f + up * 0.6f - hd * 0.15f + hx * (fs * 0.12f));
					float rb = (0.028f + g.hornLen * 0.1f) * (0.4f + 0.6f * hs);
					pts[0] = p - n * 0.02f;
					for (int k = 1; k < 6; k++)
					{
						float a = g.hornCurve * (float) k * 0.28f;
						V3 d = norm (d0 * cosf (a) - hd * sinf (a) + hx * (fs * 0.06f * (float) k));
						pts[k] = pts[k - 1] + d * (hl / 5);
					}
					for (int k = 0; k < 6; k++) { float f = (float) k / 5; rad[k] = rb * (1 - f * 0.92f); cols[k] = mixc (cdk, cb, f); }
					B.gloss = 0.65f; B.tube (pts, rad, cols, 6, 10, 3, 0, X, 3);
				}
				else if (g.horn == HORN_ANTENNAE)
				{
					V3 pts[4]; float rad[4]; Col cols[4];
					float wob = 0.05f * sinf (P.t * 3.1f + fs) + P.hop * 0.3f;
					pts[0] = p - n * 0.01f;
					pts[1] = pts[0] + norm (n + up) * (hl * 0.4f);
					pts[2] = pts[1] + norm (up + hd * 0.3f + hx * (fs * 0.4f)) * (hl * 0.45f) + hd * wob;
					pts[3] = pts[2] + norm (hd * 0.6f + up * 0.3f + hx * (fs * 0.35f)) * (hl * 0.4f) + hd * wob * 1.5f;
					for (int k = 0; k < 4; k++) { rad[k] = 0.012f * (1 - 0.3f * (float) k / 3) * (0.5f + 0.5f * hs); cols[k] = mixc (skin, cdk, (float) k / 3); }
					B.gloss = 0.5f; B.tube (pts, rad, cols, 4, 6, 3, 0, X, 1);
					B.gloss = 0.9f; B.emit = 0.15f;
					B.sphere (pts[3], 0.034f * (0.4f + 0.6f * hs), g.pat, 10, 7);
					B.emit = 0;
				}
				else	// le bélier : une spirale sur le côté
				{
					V3 pts[9]; float rad[9]; Col cols[9];
					float rho0 = (0.07f + g.hornLen * 0.12f) * hs;
					V3 c0 = p - up * rho0;
					for (int k = 0; k < 9; k++)
					{
						float a = (float) k / 8 * 1.65f * PI, rho = rho0 * (1 - (float) k / 8 * 0.55f);
						pts[k] = c0 + (up * cosf (a) - hd * sinf (a)) * rho + hx * (fs * (0.012f * a + 0.02f));
						rad[k] = (0.04f + g.hornLen * 0.05f) * hs * (1 - (float) k / 8 * 0.7f);
						cols[k] = mixc (cdk, cb, (float) k / 8);
					}
					B.gloss = 0.55f; B.tube (pts, rad, cols, 9, 10, 2, 0, X, 3);
				}
			}
		else	// la licorne : une corne torsadée sur le front
		{
			V3 p, n;
			hit (b, hc, norm (up * 0.78f + hd * 0.62f), p, n);
			V3 d = norm (up * 0.55f + hd * 0.85f);
			V3 pts[8]; float rad[8]; Col cols[8];
			float rb = (0.035f + g.hornLen * 0.08f) * (0.4f + 0.6f * hs);
			for (int k = 0; k < 8; k++)
			{
				float f = (float) k / 7;
				pts[k] = p - n * 0.02f + d * (hl * 1.1f * f) + up * (g.hornCurve * 0.04f * f * f);
				rad[k] = rb * (1 - f * 0.93f) * (1 + 0.14f * sinf (f * 22));
				cols[k] = mixc (ca, cb, f);
			}
			B.gloss = 0.75f; B.tube (pts, rad, cols, 8, 10, 2, 0, X, 3);
		}
	}
	// les oreilles
	float ers = ps.ear;
	if (g.ear && ers > 0.02f)
	{
		V3 hx = norm (cross (up, hd));
		float es2 = g.earSize * ers;
		Col inner = mixc (g.bellyC, Col (1, 0.62f, 0.66f), 0.45f);
		for (int side = -1; side <= 1; side += 2)
		{
			float fs = (float) side;
			V3 p, n;
			V3 dd = g.ear == EAR_FINS ? V3 (fs * 0.95f, 0.15f, -0.25f) : g.ear == EAR_LONG ? V3 (fs * 0.42f, 0.88f, -0.2f) : V3 (fs * 0.62f, 0.72f, -0.18f);
			hit (b, hc, hx * dd.x + up * dd.y + hd * dd.z, p, n);
			float perk = 0.15f * sinf (P.t * 0.9f + fs) * 0.3f;
			switch (g.ear)
			{
			case EAR_ROUND:
			{
				Frame f = frameLook (p + n * (0.07f * es2), norm (hd * 0.85f + n * 0.15f), n);
				B.gloss = g.gloss * 0.6f;
				B.ellipsoid (f, V3 (0.1f * es2, 0.1f * es2, 0.03f * es2), skin, skinLo, 14, 8);
				Frame f2 = f; f2.o = f.o + f.z * (0.016f * es2);
				B.ellipsoid (f2, V3 (0.064f * es2, 0.064f * es2, 0.02f * es2), inner, inner, 12, 6);
				break;
			}
			case EAR_POINTY:
			{
				V3 d = norm (n * 0.75f + up * 0.45f + hx * (fs * (0.12f + perk)));
				V3 pts[3] = { p - n * 0.02f, p + d * (0.11f * es2), p + d * (0.23f * es2) - hd * 0.02f };
				float rad[3] = { 0.075f * es2, 0.05f * es2, 0.002f };
				Col cols[3] = { skin, skin, skinLo };
				B.gloss = g.gloss * 0.6f; B.tube (pts, rad, cols, 3, 10, 4, 0.7f, hd, 3);
				V3 p2[3] = { pts[0] + hd * 0.012f, pts[1] + hd * 0.014f, pts[2] - d * (0.03f * es2) + hd * 0.006f };
				float r2[3] = { 0.048f * es2, 0.032f * es2, 0.002f };
				Col c2[3] = { inner, inner, inner };
				B.tube (p2, r2, c2, 3, 8, 3, 0.75f, hd, 3);
				break;
			}
			case EAR_LONG:
			{
				float droop = 0.25f + 0.08f * sinf (P.t * 1.6f + fs) + P.hop * 0.4f - (P.mood < 0 ? P.mood * 0.3f : 0);
				V3 pts[4];
				pts[0] = p - n * 0.02f;
				pts[1] = pts[0] + norm (up + hx * (fs * 0.2f)) * (0.12f * es2);
				pts[2] = pts[1] + norm (up * (1 - droop) - hd * droop + hx * (fs * 0.3f)) * (0.13f * es2);
				pts[3] = pts[2] + norm (up * (1 - droop * 2) - hd * (droop * 1.5f) + hx * (fs * 0.3f)) * (0.12f * es2);
				float rad[4] = { 0.045f * es2, 0.06f * es2, 0.058f * es2, 0.02f * es2 };
				Col cols[4] = { skin, skin, skin, skinLo };
				B.gloss = g.gloss * 0.6f; B.tube (pts, rad, cols, 4, 10, 4, 0.68f, hd, 3);
				V3 p2[4]; float r2[4]; Col c2[4];
				for (int k = 0; k < 4; k++) { p2[k] = pts[k] + hd * 0.016f; r2[k] = rad[k] * 0.6f; c2[k] = inner; }
				B.tube (p2, r2, c2, 4, 8, 3, 0.75f, hd, 3);
				break;
			}
			default:	// les nageoires
			{
				Frame f;
				f.z = norm (-hd * 0.75f + hx * (fs * 0.6f) + up * 0.15f);
				f.y = norm (up - f.z * dot (up, f.z));
				f.x = cross (f.y, f.z);
				f.o = p + f.z * (0.07f * es2);
				S.gloss = 0.5f;
				S.ellipsoid (f, V3 (0.018f * es2, 0.085f * es2, 0.13f * es2), g.membrane, dark (g.membrane, 0.75f), 14, 8);
				break;
			}
			}
		}
	}

	// ---- le dos, la queue, les ailes : suivent le corps
	float bks = ps.back;
	if (g.back && bks > 0.02f)
	{
		float sA = lerpf (b.sk[K_TAIL1], sHip, 0.4f), sB = lerpf (sSh, sNeck, 0.6f);
		if (g.back == BACK_SAIL)
		{
			const int NS = 14;
			V3 ea[NS], eb[NS]; Col ca[NS], cb[NS];
			for (int k = 0; k < NS; k++)
			{
				float f = (float) k / (NS - 1), s = lerpf (sA, sB, f);
				Ring rg = ringAt (b, s);
				Xf x = ringXf (s);
				V3 top = rg.c + rg.N * (rg.ry * 0.96f);
				float h = g.backSize * 0.26f * bks * powf (sinf (PI * f), 0.8f) * (1 + 0.16f * sinf (f * 19));
				ea[k] = x.p (top - rg.N * 0.02f); eb[k] = x.p (top + rg.N * h - rg.T * (h * 0.25f));
				ca[k] = skin; cb[k] = mixc (g.membrane, g.pat, 0.5f);
			}
			S.xf = Xf (); S.gloss = 0.45f;
			S.sheet (ea, eb, ca, cb, NS);
			for (int k = 2; k < NS - 1; k += 3)
			{
				V3 pts[2] = { ea[k], eb[k] };
				float rad[2] = { 0.012f * g.size, 0.004f * g.size };
				Col cols[2] = { g.accent, g.accent };
				B.xf = Xf (); B.gloss = 0.5f; B.tube (pts, rad, cols, 2, 5, 2, 0, X, 3);
			}
		}
		else
			for (int k = 0; k < g.backCount; k++)
			{
				float f = ((float) k + 0.5f) / (float) g.backCount, s = lerpf (sA, sB, f);
				float prof = powf (sinf (PI * f), 0.7f) * g.backSize * bks;
				Ring rg = ringAt (b, s);
				B.xf = ringXf (s);
				V3 top = rg.c + rg.N * (rg.ry * 0.95f);
				switch (g.back)
				{
				case BACK_SPIKES:
				{
					V3 d = norm (rg.N - rg.T * 0.4f);
					V3 pts[3] = { top - rg.N * 0.03f, top + d * (0.08f * prof), top + d * (0.17f * prof) - rg.T * (0.02f * prof) };
					float rad[3] = { 0.045f * prof + 0.008f, 0.022f * prof, 0.002f };
					Col cols[3] = { dark (g.accent, 0.85f), g.accent, mixc (g.accent, Col (1, 1, 1), 0.3f) };
					B.gloss = 0.7f; B.tube (pts, rad, cols, 3, 8, 3, 0, X, 3);
					break;
				}
				case BACK_PLATES:
				{
					Frame f;
					f.y = norm (rg.N - rg.T * 0.2f); f.z = norm (rg.T - f.y * dot (rg.T, f.y)); f.x = cross (f.y, f.z);
					f.o = top + f.y * (0.05f * prof) + rg.X * ((k & 1) ? 0.015f : -0.015f);
					Col pc = g.pattern == PAT_PLAIN ? g.accent : g.pat;
					B.gloss = 0.55f;
					B.ellipsoid (f, V3 (0.014f, 0.105f * prof + 0.01f, 0.075f * prof + 0.01f), mixc (pc, Col (1, 1, 1), 0.15f), dark (pc, 0.8f), 12, 8);
					break;
				}
				default:	// les touffes
				{
					B.gloss = 0.1f;
					for (int q = 0; q < 3; q++)
					{
						V3 o = top + rg.N * (0.02f + 0.03f * prof) + rg.X * (((float) q - 1) * 0.035f * prof) + rg.T * (((float) (q & 1)) * 0.02f);
						B.sphere (o, 0.035f * prof + 0.018f, mixc (g.pat, Col (1, 1, 1), 0.1f * (float) q), 9, 6);
					}
					break;
				}
				}
			}
	}
	// le bout de la queue
	float tps = ps.tip;
	if (g.tip && tps > 0.02f && g.tailLen > 0.15f)
	{
		const Ring &r0 = b.r.front ();
		B.xf = ringXf (0); S.xf = B.xf;
		V3 tip = r0.c - r0.T * ((r0.rx + r0.ry) * 0.4f);
		switch (g.tip)
		{
		case TIP_TUFT:
			B.gloss = 0.1f;
			for (int q = 0; q < 6; q++)
			{
				float a = (float) q * 1.05f;
				V3 o = tip - r0.T * 0.03f + (r0.X * cosf (a) + r0.N * sinf (a)) * 0.03f;
				B.sphere (o, 0.045f * tps, mixc (g.pat, g.bellyC, 0.15f * (float) (q % 3)), 9, 6);
			}
			B.sphere (tip - r0.T * 0.07f, 0.05f * tps, g.pat, 9, 6);
			break;
		case TIP_CLUB:
			B.gloss = 0.6f;
			B.sphere (tip - r0.T * 0.04f, 0.075f * tps, g.accent, 12, 8);
			for (int q = 0; q < 4; q++)
			{
				float a = (float) q * PI / 2 + 0.6f;
				V3 d = norm (r0.X * cosf (a) + r0.N * sinf (a) - r0.T * 0.3f);
				V3 o = tip - r0.T * 0.04f + d * 0.06f * tps;
				V3 pts[2] = { o, o + d * 0.05f * tps };
				float rad[2] = { 0.02f * tps, 0.002f };
				Col cols[2] = { g.accent, mixc (g.accent, Col (1, 1, 1), 0.3f) };
				B.tube (pts, rad, cols, 2, 6, 2, 0, X, 1);
			}
			break;
		default:
		{
			Frame f; f.z = -r0.T; f.y = r0.N; f.x = cross (f.y, f.z);
			f.o = tip - r0.T * 0.09f * tps;
			S.gloss = 0.5f;
			S.ellipsoid (f, V3 (0.014f, 0.12f * tps, 0.13f * tps), g.membrane, dark (g.membrane, 0.7f), 14, 8);
			break;
		}
		}
	}
	// les ailes
	float wgs = ps.wing;
	if (g.wing && wgs > 0.02f)
	{
		float s = sSh - (sSh - sMid) * 0.28f;
		Ring rg = ringAt (b, s);
		Xf x = ringXf (s);
		float span = 0.55f * g.wingSize * wgs * (g.size > 0 ? 1 : 1);
		for (int side = -1; side <= 1; side += 2)
		{
			float fs = (float) side;
			V3 root = rg.c + rg.X * (rg.rx * 0.5f * fs) + rg.N * (rg.ry * 0.72f);
			float open = 0.25f + 0.75f * sat (P.flap);
			float fl = P.flap * (0.45f + 0.65f * sinf (P.t * (g.wing == WING_INSECT ? 38.0f : 11.0f)));
			V3 spanD = norm (rg.X * (fs * (0.25f + 0.75f * open)) - rg.T * (1 - 0.6f * open) + rg.N * 0.18f);
			spanD = rotAxis (spanD, rg.T, fs * fl);
			V3 wn = norm (cross (spanD, -rg.T));		// (la normale du plan de l'aile)
			if (dot (wn, rg.N) < 0) wn = -wn;
			if (g.wing == WING_FEATHER)
			{
				V3 wrist = root + spanD * (span * 0.55f);
				V3 tipP = root + spanD * span;
				V3 pts[3] = { root, wrist + wn * 0.02f, tipP };
				float rad[3] = { 0.04f, 0.03f, 0.018f };
				Col cols[3] = { skin, skin, g.membrane };
				B.xf = x; B.gloss = g.gloss; B.tube (pts, rad, cols, 3, 8, 3, 0, X, 3);
				for (int k = 0; k < 7; k++)
				{
					float f = (float) k / 6;
					V3 o = lerp (root + spanD * (span * 0.1f), tipP, f);
					V3 fd = norm (-rg.T * (1.0f - 0.3f * f) + spanD * (0.35f + 0.6f * f) - wn * 0.05f);
					float fl2 = (0.14f + 0.18f * f) * g.wingSize * wgs;
					Frame ff; ff.z = fd; ff.y = wn; ff.x = norm (cross (ff.y, ff.z)); ff.y = cross (ff.z, ff.x);
					ff.o = o + fd * (fl2 * 0.5f) - wn * (0.005f * (float) k);
					Col fc = mixc (g.membrane, g.pat, f);
					B.gloss = 0.35f;
					B.ellipsoid (ff, V3 (0.04f * (0.6f + 0.4f * f), 0.008f, fl2 * 0.5f), mixc (fc, Col (1, 1, 1), 0.1f), dark (fc, 0.85f), 10, 5);
				}
			}
			else if (g.wing == WING_MEMBRANE)
			{
				V3 elbow = root + spanD * (span * 0.42f) + wn * 0.05f;
				V3 wrist = root + spanD * (span * 0.72f);
				V3 tips[3];
				for (int k = 0; k < 3; k++)
				{
					float be2 = 0.2f + 0.55f * (float) k;
					tips[k] = wrist + norm (spanD * cosf (be2) - rg.T * sinf (be2)) * (span * 0.5f * (1 - 0.15f * (float) k));
				}
				V3 back = root - rg.T * (span * 0.35f) + wn * 0.0f;
				// la membrane
				S.xf = x; S.gloss = 0.45f;
				Col mc = g.membrane, mc2 = dark (g.membrane, 0.75f);
				V3 poly[6] = { root, wrist, tips[0], tips[1], tips[2], back };
				S.two = true;
				int c0 = S.add (lerp (root, wrist, 0.5f) - rg.T * (span * 0.2f), wn, mc);
				for (int k = 0; k < 6; k++)
				{
					int a2 = S.add (poly[k], wn, k == 0 ? mc : mc2);
					int b2 = S.add (lerp (poly[k], poly[(k + 1) % 6], 0.5f) + (k >= 1 && k < 4 ? (lerp (poly[k], poly[(k + 1) % 6], 0.5f) - wrist) * -0.12f : V3 ()), wn, mc);
					int c2 = S.add (poly[(k + 1) % 6], wn, (k + 1) % 6 == 0 ? mc : mc2);
					S.tri (c0, a2, b2); S.tri (c0, b2, c2);
				}
				S.two = false;
				// les os
				B.xf = x; B.gloss = 0.4f;
				Col bc = dark (skin, 0.8f);
				V3 arm[3] = { root, elbow, wrist };
				float ar[3] = { 0.035f, 0.026f, 0.02f };
				Col ac[3] = { skin, bc, bc };
				B.tube (arm, ar, ac, 3, 8, 3, 0, X, 3);
				for (int k = 0; k < 3; k++)
				{
					V3 fp[2] = { wrist, tips[k] };
					float fr[2] = { 0.016f, 0.005f };
					Col fc[2] = { bc, bc };
					B.tube (fp, fr, fc, 2, 6, 2, 0, X, 3);
				}
			}
			else	// les ailes d'insecte : transparentes, deux paires
			{
				A.xf = x; A.gloss = 0.9f; A.emit = 0.08f; A.alpha = 0.42f;
				for (int q = 0; q < 2; q++)
				{
					V3 d = norm (rotAxis (spanD, wn, (float) q * 0.55f * fs) - rg.T * (0.2f * (float) q));
					Frame f; f.z = d; f.y = wn; f.x = norm (cross (f.y, f.z)); f.y = cross (f.z, f.x);
					float L2 = span * (q ? 0.75f : 1.0f);
					f.o = root + d * (L2 * 0.5f) - rg.T * (0.04f * (float) q);
					Col wc = mixc (g.membrane, Col (1, 1, 1), 0.6f);
					A.ellipsoid (f, V3 (L2 * 0.2f, 0.004f, L2 * 0.5f), wc, wc, 14, 6);
				}
				A.alpha = 1; A.emit = 0;
			}
		}
	}

	// ---- ce que la scène veut savoir
	out.head = HX.p (hc);
	out.top = HX.p (hc + up * (g.headR * 1.25f + (g.horn ? g.hornLen * 0.8f : 0) + (g.ear == EAR_LONG ? 0.2f * g.earSize : 0)));
	out.mouth = HX.p (mouthP);
	out.center = G.p (b.kp[K_MID]);
	float ext = (g.bodyLen + g.tailLen * 0.6f + g.headR * 2 + g.neckLen * 0.6f) * 0.5f;
	out.radius = (ext + g.bodyR) * g.size;
	out.footprint = (ext * 0.62f + g.bodyR * 0.8f) * g.size;
}

// ==== l'œuf ===========================================================================================================
void buildEgg (const Genome &g, float wobble, float crack, float t, const V3 &pos, CreatureMeshes &M, CreatureOut &out)
{
	(void) g;				// (la coquille est dans la texture : makeShell)
	float R = 0.28f, H = 0.37f;
	// il se balance sur sa base
	float ax = wobble * 0.22f * sinf (t * 9), az = wobble * 0.15f * sinf (t * 7 + 1);
	Xf G = Xf::trans (pos) * Xf::rotZ (ax) * Xf::rotX (az) * Xf::trans (V3 (0, H, 0));
	Builder B (&M.body);
	B.xf = G; B.gloss = 0.45f;
	const int SEG = 28, RINGS = 18;
	int base = (int) M.body.v.size ();
	for (int i = 0; i <= RINGS; i++)
	{
		float ph = -PI / 2 + PI * (float) i / RINGS, sp = sinf (ph), cp = cosf (ph);
		float k = 1 - 0.16f * sp;				// (plus étroit en haut)
		for (int j = 0; j <= SEG; j++)
		{
			float th = 2 * PI * (float) j / SEG;
			V3 l (cp * cosf (th), sp, -cp * sinf (th));
			V3 p (l.x * R * k, l.y * H, l.z * R * k);
			V3 n = norm (V3 (l.x / (R * k), l.y / H + 0.16f * cp * cp / R, l.z / (R * k)));
			B.add (p, n, Col (1, 1, 1), (float) j / SEG, (float) i / RINGS);
		}
	}
	B.grid (base, RINGS, SEG);
	if (crack > 0)
	{
		Builder C (&M.parts);
		C.xf = G; C.gloss = 0.1f;
		int n = 3 + (int) (crack * 14);
		std::vector<V3> pts; std::vector<float> rad; std::vector<Col> cols;
		for (int k = 0; k <= n; k++)
		{
			float th = (float) k / 17 * 2 * PI * 0.9f + 0.4f;
			float y = 0.18f + ((k & 1) ? 0.06f : -0.05f);
			float sp = y, cp = sqrtf (1 - sp * sp), kk = 1 - 0.16f * sp;
			pts.push_back (V3 (cp * cosf (th) * R * kk * 1.01f, sp * H, -cp * sinf (th) * R * kk * 1.01f));
			rad.push_back (0.006f); cols.push_back (Col (0.25f, 0.2f, 0.18f));
		}
		C.tube (pts.data (), rad.data (), cols.data (), (int) pts.size (), 5, 1, 0, V3 (0, 1, 0), 3);
	}
	out.head = G.p (V3 (0, H * 0.3f, 0));
	out.top = G.p (V3 (0, H * 1.3f, 0));
	out.mouth = out.head;
	out.center = G.p (V3 ());
	out.radius = 0.4f; out.footprint = 0.3f;
}

}	// namespace mo
