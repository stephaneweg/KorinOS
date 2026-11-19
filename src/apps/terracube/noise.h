// noise.h -- TerraCube : bruit de gradient (2D et 3D) et sommes d'octaves, déterministes par la graine du monde.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <math.h>
#include <stdint.h>

namespace tc {

inline uint32_t hash32 (uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

inline uint32_t hash3 (int x, int y, int z, uint32_t seed)
{
	return hash32 ((uint32_t) x * 0x8da6b343U ^ (uint32_t) y * 0xd8163841U ^ (uint32_t) z * 0xcb1ab31fU ^ seed * 0x9e3779b9U);
}

// un nombre de [0, 1) pour une case et un sel
inline float rand01 (int x, int y, int z, uint32_t seed) { return (hash3 (x, y, z, seed) >> 8) * (1.0f / 16777216.0f); }

struct Noise {
	uint8_t perm[512];
	explicit Noise (uint32_t seed = 1) { reseed (seed); }
	void reseed (uint32_t seed)
	{
		for (int i = 0; i < 256; i++) perm[i] = (uint8_t) i;
		uint32_t s = seed * 2654435761U + 12345;
		for (int i = 255; i > 0; i--) {
			s = hash32 (s + (uint32_t) i);
			int j = (int) (s % (uint32_t) (i + 1));
			uint8_t t = perm[i];
			perm[i] = perm[j];
			perm[j] = t;
		}
		for (int i = 0; i < 256; i++) perm[256 + i] = perm[i];
	}
	static float fade (float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
	static float lerp (float a, float b, float t) { return a + (b - a) * t; }
	static float grad2 (int h, float x, float y)
	{
		switch (h & 7) {
		case 0: return x + y;
		case 1: return -x + y;
		case 2: return x - y;
		case 3: return -x - y;
		case 4: return x;
		case 5: return -x;
		case 6: return y;
		default: return -y;
		}
	}
	static float grad3 (int h, float x, float y, float z)
	{
		int k = h & 15;
		float u = k < 8 ? x : y, v = k < 4 ? y : (k == 12 || k == 14) ? x : z;
		return ((k & 1) ? -u : u) + ((k & 2) ? -v : v);
	}
	// environ [-1, 1]
	float n2 (float x, float y) const
	{
		float fx = floorf (x), fy = floorf (y);
		int X = (int) fx & 255, Y = (int) fy & 255;
		x -= fx, y -= fy;
		float u = fade (x), v = fade (y);
		int a = perm[X] + Y, b = perm[X + 1] + Y;
		return lerp (lerp (grad2 (perm[a], x, y), grad2 (perm[b], x - 1, y), u),
			     lerp (grad2 (perm[a + 1], x, y - 1), grad2 (perm[b + 1], x - 1, y - 1), u), v);
	}
	float n3 (float x, float y, float z) const
	{
		float fx = floorf (x), fy = floorf (y), fz = floorf (z);
		int X = (int) fx & 255, Y = (int) fy & 255, Z = (int) fz & 255;
		x -= fx, y -= fy, z -= fz;
		float u = fade (x), v = fade (y), w = fade (z);
		int A = perm[X] + Y, AA = perm[A] + Z, AB = perm[A + 1] + Z;
		int B = perm[X + 1] + Y, BA = perm[B] + Z, BB = perm[B + 1] + Z;
		return lerp (lerp (lerp (grad3 (perm[AA], x, y, z), grad3 (perm[BA], x - 1, y, z), u),
				   lerp (grad3 (perm[AB], x, y - 1, z), grad3 (perm[BB], x - 1, y - 1, z), u), v),
			     lerp (lerp (grad3 (perm[AA + 1], x, y, z - 1), grad3 (perm[BA + 1], x - 1, y, z - 1), u),
				   lerp (grad3 (perm[AB + 1], x, y - 1, z - 1), grad3 (perm[BB + 1], x - 1, y - 1, z - 1), u), v), w);
	}
	// somme d'octaves, ramenée vers [-1, 1]
	float fbm2 (float x, float y, int oct, float lac = 2.0f, float gain = 0.5f) const
	{
		float s = 0, a = 1, n = 0;
		for (int i = 0; i < oct; i++) {
			s += a * n2 (x, y);
			n += a;
			a *= gain;
			x *= lac, y *= lac;
		}
		return s / n;
	}
};

} // namespace tc
