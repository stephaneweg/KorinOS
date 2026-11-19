// mesh.cpp -- TerraCube : le maillage d'un tronçon (world.h, buildMesh).
//
// Seules les faces qui touchent un bloc non opaque sont gardées. Chaque sommet porte sa lumière : l'ombre de la face
// (le dessus clair, le dessous sombre), l'occlusion ambiante (les blocs qui l'entourent) et la lumière du ciel, lissée
// sur les quatre cases qui touchent le sommet (une case est au ciel si rien d'opaque n'est au-dessus d'elle).
// Le tronçon est d'abord recopié avec un bord d'une case pris aux voisins : les accès restent locaux.
// Les liquides ont la hauteur de leur niveau, moyennée aux coins (les pentes d'un écoulement). Les formes à part
// (le fil, la torche, le levier, les répéteurs, la tête du piston…) sont des boîtes décrites posées au sol ou
// tournées vers le nord, puis tournées vers leur direction (des quarts de tour : les faces restent dans le bon sens).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "world.h"

#include <math.h>
#include <string.h>

namespace tc {

namespace {

const int PW = CW + 2;	// avec le bord

struct Pad {
	uint8_t b[CH * PW * PW];
	uint8_t m[CH * PW * PW];
	int8_t top[PW * PW];
	int get (int x, int y, int z) const
	{
		if (y < 0) return BEDROCK;
		if (y >= CH) return AIR;
		return b[(y * PW + z + 1) * PW + x + 1];
	}
	int meta (int x, int y, int z) const
	{
		if (y < 0 || y >= CH) return 0;
		return m[(y * PW + z + 1) * PW + x + 1];
	}
	bool sky (int x, int y, int z) const { return y > top[(z + 1) * PW + x + 1]; }
};

// tuile -> coin haut gauche, en 1/16 de texel (atlas de 16 × 16 tuiles de 16 px : 4096 × 4096 unités) ; un coin
// de tuile est rentré de 1/16 de texel, que l'échantillonnage au plus proche ne prenne jamais la tuile voisine
inline int tileU (int t) { return (t & 15) * 256; }
inline int tileV (int t) { return (t >> 4) * 256; }
inline uint16_t texel (int base, float t)
{
	int k = (int) (t * 256.0f + 0.5f);
	return (uint16_t) (base + (k < 1 ? 1 : k > 255 ? 255 : k));
}

const float FACE_SHADE[6] = { 0.80f, 0.80f, 1.00f, 0.55f, 0.68f, 0.68f };	// +x -x +y -y +z -z
const float AO[4] = { 0.50f, 0.68f, 0.84f, 1.0f };

inline bool occludes (int b) { return isOpaque (b); }

// la face visible si le voisin n'est pas opaque ; deux blocs découpés pareils (verre contre verre) se cachent
inline bool faceVisible (int self, int nb)
{
	if (nb == AIR) return true;
	if (isOpaque (nb)) return false;
	const BlockInfo &s = info (self), &n = info (nb);
	if (s.shape == SH_LIQUID) return nb != self;
	if (n.shape == SH_LIQUID || n.shape == SH_CROSS || n.shape == SH_NONE || n.shape == SH_SPECIAL) return true;
	if (self == nb && self == GLASS) return false;
	return true;
}

// topH : la hauteur du dessus à chaque coin ([x][z], 0 ou 1) ; tile, glow : la tuile de la face, sa propre lumière
void emitFace (std::vector<Vertex> &out, const Pad &p, int x, int y, int z, int f, int tile, bool glow, const float topH[4])
{
	int a = f >> 1, s = (f & 1) ? -1 : 1;
	int u = a == 0 ? 1 : a == 1 ? 2 : 0, v = a == 0 ? 2 : a == 1 ? 0 : 1;
	int n[3] = { 0, 0, 0 };
	n[a] = s;
	int fx = x + n[0], fy = y + n[1], fz = z + n[2];	// la case devant la face
	static const int ORD_POS[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	static const int ORD_NEG[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
	const int (*ord)[2] = s > 0 ? ORD_POS : ORD_NEG;
	Vertex q[4];
	int aos[4];
	for (int k = 0; k < 4; k++) {
		int i = ord[k][0], j = ord[k][1];
		float pc[3];
		pc[a] = s > 0 ? 1.0f : 0.0f;
		pc[u] = (float) i;
		pc[v] = (float) j;
		float th = topH[(int) pc[0] * 2 + (int) pc[2]];
		if (pc[1] > 0.5f) pc[1] = th;
		// occlusion et ciel : les trois cases autour du sommet, dans la couche devant la face
		int du[3] = { 0, 0, 0 }, dv[3] = { 0, 0, 0 };
		du[u] = i ? 1 : -1;
		dv[v] = j ? 1 : -1;
		int b1 = p.get (fx + du[0], fy + du[1], fz + du[2]);
		int b2 = p.get (fx + dv[0], fy + dv[1], fz + dv[2]);
		int b3 = p.get (fx + du[0] + dv[0], fy + du[1] + dv[1], fz + du[2] + dv[2]);
		int s1 = occludes (b1), s2 = occludes (b2), s3 = occludes (b3);
		int ao = s1 && s2 ? 0 : 3 - (s1 + s2 + s3);
		aos[k] = ao;
		float sky = 0;
		int cnt = 0;
		auto skyAt = [&] (int cx, int cy, int cz, int b) {
			if (occludes (b)) return;
			cnt++;
			if (p.sky (cx, cy, cz)) sky += 1.0f;
		};
		skyAt (fx, fy, fz, p.get (fx, fy, fz));
		skyAt (fx + du[0], fy + du[1], fz + du[2], b1);
		skyAt (fx + dv[0], fy + dv[1], fz + dv[2], b2);
		if (!(s1 && s2)) skyAt (fx + du[0] + dv[0], fy + du[1] + dv[1], fz + du[2] + dv[2], b3);
		float sk = cnt ? sky / cnt : 0.0f;
		float l = FACE_SHADE[f] * AO[ao] * (0.22f + 0.78f * sk);
		if (glow) l = 1.0f;
		Vertex &vx = q[k];
		vx.x = (uint16_t) ((x + pc[0]) * 16.0f + 0.5f);
		vx.y = (uint16_t) ((y + pc[1]) * 16.0f + 0.5f);
		vx.z = (uint16_t) ((z + pc[2]) * 16.0f + 0.5f);
		vx.light = (uint8_t) (l * 255.0f + 0.5f);
		vx.glow = glow ? 255 : 0;
		// texels : les faces de côté gardent le haut de la tuile en haut
		float tu, tv;
		if (a == 0) tu = s > 0 ? 1 - pc[2] : pc[2], tv = 1 - pc[1];
		else if (a == 2) tu = s > 0 ? pc[0] : 1 - pc[0], tv = 1 - pc[1];
		else tu = pc[0], tv = pc[2];
		if (a != 1 && th < 0.99f && pc[1] > 0.5f) tv = 1 - th;
		vx.u = texel (tileU (tile), tu);
		vx.v = texel (tileV (tile), tv);
	}
	// la diagonale qui suit l'occlusion (sinon un dégradé en travers)
	if (aos[0] + aos[2] < aos[1] + aos[3]) {
		Vertex t = q[0];
		q[0] = q[1], q[1] = q[2], q[2] = q[3], q[3] = t;
	}
	out.insert (out.end (), q, q + 4);
}

void emitCross (std::vector<Vertex> &out, const Pad &p, int x, int y, int z, int blk)
{
	const BlockInfo &bi = info (blk);
	float l = (0.22f + 0.78f * (p.sky (x, y, z) ? 1.0f : 0.0f)) * 0.92f;
	uint8_t L = (uint8_t) (l * 255.0f);
	int u0 = tileU (bi.side), v0 = tileV (bi.side);
	// deux plans en diagonale, chacun dans les deux sens
	static const float D[2][4] = { { 0, 0, 1, 1 }, { 0, 1, 1, 0 } };
	for (int d = 0; d < 2; d++)
		for (int side = 0; side < 2; side++) {
			float ax = D[d][0], az = D[d][1], bx = D[d][2], bz = D[d][3];
			if (side) {
				float t = ax;
				ax = bx, bx = t;
				t = az, az = bz, bz = t;
			}
			Vertex q[4];
			float px[4] = { ax, bx, bx, ax }, py[4] = { 0, 0, 1, 1 }, pz[4] = { az, bz, bz, az };
			float tu[4] = { 0, 1, 1, 0 }, tv[4] = { 1, 1, 0, 0 };
			for (int k = 0; k < 4; k++) {
				q[k].x = (uint16_t) ((x + px[k]) * 16);
				q[k].y = (uint16_t) ((y + py[k]) * 16);
				q[k].z = (uint16_t) ((z + pz[k]) * 16);
				q[k].light = L;
				q[k].glow = 0;
				q[k].u = texel (u0, tu[k]);
				q[k].v = texel (v0, tv[k]);
			}
			out.insert (out.end (), q, q + 4);
		}
}

// ---- les formes à part --------------------------------------------------------------------------------------------

// un quart de tour : la matrice qui envoie la direction de référence vers d
struct Rot {
	int r[3][3];
	void apply (const float in[3], float out[3]) const
	{
		for (int i = 0; i < 3; i++) out[i] = r[i][0] * (in[0] - 8) + r[i][1] * (in[1] - 8) + r[i][2] * (in[2] - 8) + 8;
	}
	int face (int f) const	// la face f (de référence) une fois tournée
	{
		float n[3] = { 0, 0, 0 }, o[3];
		n[f >> 1] = (f & 1) ? -1.0f : 1.0f;
		for (int i = 0; i < 3; i++) o[i] = r[i][0] * n[0] + r[i][1] * n[1] + r[i][2] * n[2];
		for (int i = 0; i < 3; i++)
			if (o[i] > 0.5f) return i * 2;
			else if (o[i] < -0.5f) return i * 2 + 1;
		return f;
	}
};

const Rot IDENT = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };

// le bas (le support au sol) vers s
Rot downTo (int s)
{
	switch (s) {
	case 2: return { { { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, -1 } } };	// le plafond
	case 0: return { { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } } };	// +x
	case 1: return { { { 0, 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 } } };	// -x
	case 4: return { { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } } };	// +z
	case 5: return { { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } } };	// -z
	}
	return IDENT;
}

// le nord (-z) vers h (un quart de tour autour de y)
Rot northTo (int h)
{
	switch (h) {
	case 4: return { { { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } } };
	case 0: return { { { 0, 0, -1 }, { 0, 1, 0 }, { 1, 0, 0 } } };
	case 1: return { { { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 } } };
	}
	return IDENT;
}

struct Shape {
	std::vector<Vertex> &out;
	int bx, by, bz;		// le bloc (dans le tronçon)
	float light;
	int glow;		// 0 à 255
	Rot rot;
	const float *sideUV = nullptr, *topUV = nullptr;	// (sinon : la place dans la boîte) en texels {u0, v0, u1, v1}
	// une boîte de référence en 1/16 de bloc ; tiles : par face de référence (-1 : pas cette face) ; shx, shz : le
	// haut de la boîte décalé (un manche penché)
	void box (float x0, float y0, float z0, float x1, float y1, float z1, const int tiles[6], float shx = 0, float shz = 0)
	{
		float lo[3] = { x0, y0, z0 }, hi[3] = { x1, y1, z1 };
		for (int f = 0; f < 6; f++) {
			if (tiles[f] < 0) continue;
			int a = f >> 1, s = (f & 1) ? -1 : 1;
			int u = a == 0 ? 1 : a == 1 ? 2 : 0, v = a == 0 ? 2 : a == 1 ? 0 : 1;
			static const int ORD_POS[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
			static const int ORD_NEG[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
			const int (*ord)[2] = s > 0 ? ORD_POS : ORD_NEG;
			int rf = rot.face (f);
			static const float SHADE[6] = { 0.80f, 0.80f, 1.00f, 0.55f, 0.68f, 0.68f };
			float l = glow ? 1.0f : light * SHADE[rf];
			int tile = tiles[f];
			for (int k = 0; k < 4; k++) {
				float c[3];
				c[a] = s > 0 ? hi[a] : lo[a];
				c[u] = ord[k][0] ? hi[u] : lo[u];
				c[v] = ord[k][1] ? hi[v] : lo[v];
				float tu, tv;	// (les texels : la place dans la boîte, comme les faces d'un cube)
				if (a == 0) tu = s > 0 ? 16 - c[2] : c[2], tv = 16 - c[1];
				else if (a == 2) tu = s > 0 ? c[0] : 16 - c[0], tv = 16 - c[1];
				else tu = c[0], tv = c[2];
				const float *uv = a == 1 ? topUV : sideUV;
				if (uv) {	// (la face entière sur un morceau donné de la tuile)
					float fu = (float) ord[k][a == 0 ? 1 : 0], fv = a == 1 ? (float) ord[k][1] : (c[1] >= hi[1] - 0.001f ? 0.0f : 1.0f);
					if (a == 0) fu = (s > 0) ? 1 - (c[2] > lo[2] + 0.001f ? 1.0f : 0.0f) : (c[2] > lo[2] + 0.001f ? 1.0f : 0.0f);
					else if (a == 2) fu = (s > 0) ? (c[0] > lo[0] + 0.001f ? 1.0f : 0.0f) : 1 - (c[0] > lo[0] + 0.001f ? 1.0f : 0.0f);
					else fu = c[0] > lo[0] + 0.001f ? 1.0f : 0.0f, fv = c[2] > lo[2] + 0.001f ? 1.0f : 0.0f;
					tu = uv[0] + (uv[2] - uv[0]) * fu, tv = uv[1] + (uv[3] - uv[1]) * fv;
				}
				if (c[1] >= hi[1] - 0.001f) c[0] += shx, c[2] += shz;
				float w[3];
				rot.apply (c, w);
				Vertex vx;
				vx.x = (uint16_t) lroundf (bx * 16 + w[0]);
				vx.y = (uint16_t) lroundf (by * 16 + w[1]);
				vx.z = (uint16_t) lroundf (bz * 16 + w[2]);
				vx.light = (uint8_t) (l * 255.0f + 0.5f);
				vx.glow = (uint8_t) glow;
				vx.u = texel (tileU (tile), tu / 16.0f);
				vx.v = texel (tileV (tile), tv / 16.0f);
				out.push_back (vx);
			}
		}
	}
	void all (int t, int tl[6]) { for (int i = 0; i < 6; i++) tl[i] = t; }
};

// la hauteur d'un liquide dans sa case (1 : le même liquide au-dessus)
float liquidH (const Pad &p, int x, int y, int z, int b)
{
	if (p.get (x, y + 1, z) == b) return 1.0f;
	int m = p.meta (x, y, z);
	if (m == 0 || (m & 8)) return 14.0f / 16.0f;
	return (8 - (m & 7)) / 8.0f * (14.0f / 16.0f);
}

// la hauteur à un coin : la moyenne des quatre cases qui le touchent (une source pèse plus ; l'air tire vers le bas)
float cornerH (const Pad &p, int x, int y, int z, int b)
{
	float sum = 0, w = 0;
	for (int dz = -1; dz <= 0; dz++)
		for (int dx = -1; dx <= 0; dx++) {
			int cx = x + dx, cz = z + dz, nb = p.get (cx, y, cz);
			if (nb == b) {
				if (p.get (cx, y + 1, cz) == b) return 1.0f;
				float h = liquidH (p, cx, y, cz, b);
				float k = h >= 0.8f ? 10.0f : 1.0f;
				sum += h * k, w += k;
			} else if (!isSolid (nb))
				sum += 0, w += 1;
		}
	return w > 0 ? sum / w : 14.0f / 16.0f;
}

void emitSpecial (std::vector<Vertex> &out, const Pad &p, int x, int y, int z, int b, int m)
{
	Shape sh { out, x, y, z, (0.22f + 0.78f * (p.sky (x, y, z) ? 1.0f : 0.0f)), 0, IDENT };
	int t[6];
	auto one = [&] (int tile) { sh.all (tile, t); };
	static const float TORCH_SIDE[4] = { 7, 4, 9, 15 }, TORCH_TOP[4] = { 7, 5, 9, 7 }, SMALL_SIDE[4] = { 7, 4, 9, 9 };
	auto torch = [&] (float x0, float z0, float y0, bool lit, float shz) {	// une petite torche (2 × 5)
		int tt[6];
		sh.all (lit ? T_TORCH_ON : T_TORCH_OFF, tt);
		int g = sh.glow;
		sh.glow = lit ? 255 : 0;
		sh.sideUV = SMALL_SIDE, sh.topUV = TORCH_TOP;
		sh.box (x0, y0, z0, x0 + 2, y0 + 5, z0 + 2, tt, 0, shz);
		sh.sideUV = sh.topUV = nullptr;
		sh.glow = g;
	};
	switch (b) {
	case WIRE: {
		int pw = m & 15;
		int tile = pw ? T_WIRE_ON : T_WIRE_OFF;
		sh.glow = pw ? 60 + pw * 13 : 0;
		int tt[6] = { -1, -1, tile, -1, -1, -1 };
		// les liaisons (comme sim.cpp : à côté, en descendant, en montant)
		static const int H[4] = { 0, 1, 4, 5 };
		static const int HX[6] = { 1, -1, 0, 0, 0, 0 }, HZ[6] = { 0, 0, 0, 0, 1, -1 };
		bool upBlocked = isOpaque (p.get (x, y + 1, z));
		int cons = 0, n = 0, ups = 0;
		for (int h : H) {
			int sx = x + HX[h], sz = z + HZ[h];
			int sb = p.get (sx, y, sz), smt = p.meta (sx, y, sz);
			bool c = sb == WIRE;
			if (!c) {
				switch (sb) {
				case COPPER_BLOCK: case COPPER_TORCH: case LEVER: case STONE_BUTTON: case WOOD_BUTTON: case STONE_PLATE:
				case WOOD_PLATE: case DAYLIGHT_SENSOR: c = true; break;
				case REPEATER: case COMPARATOR: c = dirOf (smt) == h || dirOf (smt) == (h ^ 1); break;
				case OBSERVER: c = dirOf (smt) == h; break;
				}
			}
			if (!c && !isOpaque (sb)) c = p.get (sx, y - 1, sz) == WIRE;
			if (!c && isOpaque (sb) && !upBlocked && p.get (sx, y + 1, sz) == WIRE) c = true, ups |= 1 << h;
			if (c) cons |= 1 << h, n++;
		}
		if (n == 1) cons |= 1 << (__builtin_ctz ((unsigned) cons) ^ 1);	// (d'un seul côté : tout droit)
		if (n == 0) cons = 0x33;					// (seul : une croix)
		sh.box (5, 0, 5, 11, 1, 11, tt);
		for (int h : H) {
			if (!((cons >> h) & 1)) continue;
			sh.rot = northTo (h);
			sh.box (5, 0, 0, 11, 1, 5, tt);
			if ((ups >> h) & 1) {	// le long du bloc d'à côté
				int tv[6] = { -1, -1, -1, -1, tile, -1 };
				sh.box (5, 0, 0, 11, 16, 1, tv);
			}
			sh.rot = IDENT;
		}
		return;
	}
	case COPPER_TORCH: {
		bool lit = !(m & M_ON);
		int s = dirOf (m);
		one (lit ? T_TORCH_ON : T_TORCH_OFF);
		sh.glow = lit ? 255 : 0;
		sh.sideUV = TORCH_SIDE, sh.topUV = TORCH_TOP;
		if (s == 3) sh.box (7, 0, 7, 9, 10, 9, t);
		else {	// contre un mur : penchée, loin de lui
			sh.rot = northTo (s);
			sh.box (7, 3, 1, 9, 13, 3, t, 0, 3);
		}
		return;
	}
	case LEVER: {
		sh.rot = downTo (dirOf (m));
		one (T_COBBLE);
		sh.box (5, 0, 4, 11, 3, 12, t);
		one (T_LEVER);
		sh.box (7, 2, 7, 9, 11, 9, t, 0, (m & M_ON) ? 4 : -4);
		return;
	}
	case STONE_BUTTON: case WOOD_BUTTON:
		sh.rot = downTo (dirOf (m));
		one (b == STONE_BUTTON ? T_STONE : T_PLANKS);
		sh.box (5, 0, 6, 11, (m & M_ON) ? 1 : 2, 10, t);
		return;
	case STONE_PLATE: case WOOD_PLATE:
		one (b == STONE_PLATE ? T_STONE : T_PLANKS);
		sh.box (1, 0, 1, 15, (m & M_ON) ? 1 : 2, 15, t);
		return;
	case REPEATER: case COMPARATOR: {
		sh.rot = northTo (dirOf (m));
		bool on = b == REPEATER ? (m & M_POW) : (m >> 4) > 0;
		int tt[6] = { T_STONE, T_STONE, b == REPEATER ? (on ? T_REPEATER_ON : T_REPEATER_OFF) : (on ? T_COMPARATOR_ON : T_COMPARATOR_OFF), T_STONE, T_STONE, T_STONE };
		sh.box (0, 0, 0, 16, 2, 16, tt);
		if (b == REPEATER) {
			int dl = (m >> 3) & 3;
			torch (7, 2, 2, on, 0);
			if (m & M_LOCK) {
				one (T_BEDROCK);
				sh.box (2, 2, 6 + 2 * dl, 14, 4, 8 + 2 * dl, t);
			} else
				torch (7, 6 + 2 * dl, 2, on, 0);
		} else {
			torch (3, 11, 2, on, 0);
			torch (11, 11, 2, on, 0);
			torch (7, 2, 2, (m & M_ON) != 0, 0);
		}
		return;
	}
	case DAYLIGHT_SENSOR: {
		int tt[6] = { T_PLANKS, T_PLANKS, (m & M_ON) ? T_DAYLIGHT_INV : T_DAYLIGHT, T_PLANKS, T_PLANKS, T_PLANKS };
		sh.box (0, 0, 0, 16, 6, 16, tt);
		return;
	}
	case PISTON_HEAD: {
		sh.rot = downTo (dirOf (m) ^ 1);	// (le haut de référence vers la direction de la tête)
		int tt[6] = { T_PISTON_SIDE, T_PISTON_SIDE, (m & M_ON) ? T_STICKY_FACE : T_PISTON_FACE, T_PLANKS, T_PISTON_SIDE, T_PISTON_SIDE };
		sh.box (0, 12, 0, 16, 16, 16, tt);
		one (T_PLANKS);
		sh.box (6, 0, 6, 10, 12, 10, t);
		return;
	}
	case PISTON: case STICKY_PISTON: {	// (sorti : la base raccourcie et le bout du bras)
		sh.rot = downTo (dirOf (m) ^ 1);
		int tt[6] = { T_PISTON_SIDE, T_PISTON_SIDE, T_PISTON_INNER, T_PISTON_BACK, T_PISTON_SIDE, T_PISTON_SIDE };
		sh.box (0, 0, 0, 16, 12, 16, tt);
		one (T_PLANKS);
		sh.box (6, 12, 6, 10, 16, 10, t);
		return;
	}
	}
}

// la tuile d'une face d'un cube selon son état (observateur, piston, lampe, dynamite)
int cubeTile (int b, int m, int f, bool &glow)
{
	const BlockInfo &bi = info (b);
	glow = bi.glow;
	switch (b) {
	case OBSERVER: {
		int d = dirOf (m);
		return f == d ? T_OBSERVER_FACE : f == (d ^ 1) ? ((m & M_ON) ? T_OBSERVER_BACK_ON : T_OBSERVER_BACK) : T_OBSERVER_SIDE;
	}
	case PISTON: case STICKY_PISTON: {
		int d = dirOf (m);
		return f == d ? (b == PISTON ? T_PISTON_FACE : T_STICKY_FACE) : f == (d ^ 1) ? T_PISTON_BACK : T_PISTON_SIDE;
	}
	case SIGNAL_LAMP:
		glow = (m & M_ON) != 0;
		return glow ? T_LAMP_ON : T_LAMP_OFF;
	case TNT:
		if (m & 16) {
			glow = true;
			return T_TNT_LIT;
		}
		break;
	}
	return f == 2 ? bi.top : f == 3 ? bi.bottom : bi.side;
}

} // namespace

void buildMesh (const World &w, const Chunk &c, std::vector<Vertex> &solid, std::vector<Vertex> &water)
{
	static Pad p;
	// le tronçon au milieu, un bord pris aux voisins (absents : de l'air)
	memset (p.b, AIR, sizeof p.b);
	memset (p.top, -1, sizeof p.top);
	for (int dz = -1; dz <= 1; dz++)
		for (int dx = -1; dx <= 1; dx++) {
			const Chunk *n = dx || dz ? w.find (c.cx + dx, c.cz + dz) : &c;
			if (!n) continue;
			int x0 = dx < 0 ? CW - 1 : 0, x1 = dx > 0 ? 0 : CW - 1;
			int z0 = dz < 0 ? CW - 1 : 0, z1 = dz > 0 ? 0 : CW - 1;
			for (int z = z0; z <= z1; z++)
				for (int x = x0; x <= x1; x++) {
					int px = x + dx * CW + 1, pz = z + dz * CW + 1;
					p.top[pz * PW + px] = n->top[z * CW + x];
					for (int y = 0; y < CH; y++) {
						p.b[(y * PW + pz) * PW + px] = n->at (x, y, z);
						p.m[(y * PW + pz) * PW + px] = n->meta (x, y, z);
					}
				}
		}
	int maxY = 0;
	for (int i = 0; i < CW * CW; i++) maxY = maxY > c.top[i] ? maxY : c.top[i];
	maxY = maxY + 12 < CH ? maxY + 12 : CH;	// (au-dessus du plus haut bloc opaque : des plantes, de l'eau, des feuilles)
	// le vrai plus haut bloc non vide
	for (int y = CH - 1; y >= maxY; y--) {
		bool any = false;
		for (int i = 0; i < CW * CW && !any; i++) any = c.b[y * CW * CW + i] != AIR;
		if (any) {
			maxY = y + 1;
			break;
		}
	}
	for (int y = 0; y < maxY; y++)
		for (int z = 0; z < CW; z++)
			for (int x = 0; x < CW; x++) {
				int b = c.at (x, y, z);
				if (b == AIR) continue;
				const BlockInfo &bi = info (b);
				if (bi.shape == SH_CROSS) {
					emitCross (solid, p, x, y, z, b);
					continue;
				}
				int m = c.meta (x, y, z);
				if (bi.shape == SH_SPECIAL || ((b == PISTON || b == STICKY_PISTON) && (m & M_ON))) {
					emitSpecial (solid, p, x, y, z, b, m);
					continue;
				}
				float topH[4] = { 1, 1, 1, 1 };
				std::vector<Vertex> &out = bi.shape == SH_LIQUID ? water : solid;
				if (bi.shape == SH_LIQUID && p.get (x, y + 1, z) != b)
					for (int i = 0; i < 2; i++)
						for (int j = 0; j < 2; j++) topH[i * 2 + j] = cornerH (p, x + i, y, z + j, b);
				static const int N[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
				for (int f = 0; f < 6; f++) {
					int nb = p.get (x + N[f][0], y + N[f][1], z + N[f][2]);
					bool vis = faceVisible (b, nb);
					// (un liquide plus bas qu'un bloc plein : son dessus se voit ; le côté d'un plus haut voisin du même, non)
					if (!vis && bi.shape == SH_LIQUID && f == 2 && topH[0] + topH[1] + topH[2] + topH[3] < 3.99f && nb != b) vis = true;
					if (!vis) continue;
					if (bi.shape == SH_LIQUID && f == 3 && nb != AIR) continue;
					bool glow;
					int tile = cubeTile (b, m, f, glow);
					emitFace (out, p, x, y, z, f, tile, glow, topH);
				}
			}
}

} // namespace tc
