// mesh.cpp -- TerraCube : le maillage d'un tronçon (world.h, buildMesh).
//
// Seules les faces qui touchent un bloc non opaque sont gardées. Chaque sommet porte sa lumière : l'ombre de la face
// (le dessus clair, le dessous sombre), l'occlusion ambiante (les blocs qui l'entourent) et la lumière du ciel, lissée
// sur les quatre cases qui touchent le sommet (une case est au ciel si rien d'opaque n'est au-dessus d'elle).
// Le tronçon est d'abord recopié avec un bord d'une case pris aux voisins : les accès restent locaux.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "world.h"

#include <string.h>

namespace tc {

namespace {

const int PW = CW + 2;	// avec le bord

struct Pad {
	uint8_t b[CH * PW * PW];
	int8_t top[PW * PW];
	int get (int x, int y, int z) const
	{
		if (y < 0) return BEDROCK;
		if (y >= CH) return AIR;
		return b[(y * PW + z + 1) * PW + x + 1];
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
	if (s.shape == SH_LIQUID) return n.shape != SH_LIQUID;
	if (n.shape == SH_LIQUID || n.shape == SH_CROSS || n.shape == SH_NONE) return true;
	if (self == nb && self == GLASS) return false;
	return true;
}

void emitFace (std::vector<Vertex> &out, const Pad &p, int x, int y, int z, int f, int blk, float topH)
{
	int a = f >> 1, s = (f & 1) ? -1 : 1;
	int u = a == 0 ? 1 : a == 1 ? 2 : 0, v = a == 0 ? 2 : a == 1 ? 0 : 1;
	int n[3] = { 0, 0, 0 };
	n[a] = s;
	int fx = x + n[0], fy = y + n[1], fz = z + n[2];	// la case devant la face
	const BlockInfo &bi = info (blk);
	int tile = a == 1 ? (s > 0 ? bi.top : bi.bottom) : bi.side;
	bool glow = bi.glow;
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
		if (pc[1] > 0.5f) pc[1] = topH;
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
		if (a != 1 && topH < 0.99f && pc[1] > 0.5f) tv = 1 - topH;
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
					for (int y = 0; y < CH; y++) p.b[(y * PW + pz) * PW + px] = n->at (x, y, z);
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
				float topH = 1.0f;
				std::vector<Vertex> &out = bi.shape == SH_LIQUID ? water : solid;
				if (bi.shape == SH_LIQUID && p.get (x, y + 1, z) != WATER) topH = 14.0f / 16.0f;
				static const int N[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
				for (int f = 0; f < 6; f++) {
					int nb = p.get (x + N[f][0], y + N[f][1], z + N[f][2]);
					if (!faceVisible (b, nb)) continue;
					if (bi.shape == SH_LIQUID && f == 3 && nb != AIR) continue;
					emitFace (out, p, x, y, z, f, b, topH);
				}
			}
}

} // namespace tc
