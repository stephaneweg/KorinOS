//
// gallery.cpp -- Morpho sur l'hôte : des planches de créatures tirées au hasard (tools/morpho/run.sh gallery), pour
// juger de la variété et du rendu sans Korin. Le rendu est celui du jeu : scene.cpp et GpuKit du PC au processeur.
//
//   gallery <stade 1..5> <graine> <colonnes> <lignes> <sortie.ppm> [heure]
//
#include "../../src/apps/morpho/scene.h"
#include <stdio.h>
#include <stdlib.h>

using namespace mo;

static Genome chain (uint32_t seed, int stage)
{
	Genome g = makeEgg (seed);
	Rng r (seed * 7 + 3);
	for (int s = 1; s <= stage; s++) g = evolve (g, r.next (), r.range (0.3f, 1));
	return g;
}

int main (int argc, char **argv)
{
	int stage = argc > 1 ? atoi (argv[1]) : 4;
	uint32_t seed = argc > 2 ? (uint32_t) atoi (argv[2]) : 1;
	int cols = argc > 3 ? atoi (argv[3]) : 4, rows = argc > 4 ? atoi (argv[4]) : 3;
	const char *out = argc > 5 ? argv[5] : "gallery.ppm";
	float hour = argc > 6 ? (float) atof (argv[6]) : 11;
	const int CW = 360, CH = 300;
	int W = CW * cols, H = CH * rows;
	std::vector<uint32_t> img ((size_t) W * H, 0), cell ((size_t) CW * CH);
	Scene sc;
	sc.init ();
	std::vector<uint32_t> skin ((size_t) SKIN_W * SKIN_H);
	for (int k = 0; k < cols * rows; k++)
	{
		Genome g = chain (seed + (uint32_t) k * 101, stage);
		// (essais : un plan du corps, un nombre de têtes imposés)
		if (getenv ("MORPHO_PLAN") || getenv ("MORPHO_HEADS"))
		{
			Rng r (g.seed + 5);
			if (getenv ("MORPHO_PLAN")) g.plan = atoi (getenv ("MORPHO_PLAN"));
			if (getenv ("MORPHO_HEADS")) g.heads = atoi (getenv ("MORPHO_HEADS"));
			shapePlan (g, r, stage >= ST_ADULT);
		}
		if (stage == 0) makeShell (g, skin.data ()); else makeSkin (g, skin.data ());
		sc.setSkin (skin.data ());
		sc.begin ();
		Pose P; P.t = 1.3f + (float) k; P.mood = 0.7f; P.gaze = V3 (0.5f, 0.8f, 4); P.headYaw = 0.25f; P.flap = 0.6f;
		P.wag = 0.4f;
		CreatureOut o;
		if (stage == 0) buildEgg (g, 0.3f, 0.5f, P.t, V3 (), sc.cm, o);
		else buildCreature (g, P, sc.cm, o);
		sc.shadowAt = V3 (o.center.x, 0, o.center.z); sc.shadowR = o.footprint * 1.1f;
		float d = 1.2f + o.radius * 2.6f;
		sc.cam.at = V3 (0, o.center.y * 0.8f + 0.1f, 0);
		sc.cam.eye = sc.cam.at + norm (V3 (0.9f, 0.45f, 1.6f)) * d;
		sc.half = 0;
		sc.cam.setup (CW, CH);
		sc.light = studioLight (hour, false); keyFrom (sc.light, sc.cam);
		sc.focus = o.center; sc.ringR = clampf (o.footprint * 1.9f, 0.45f, 1.7f);
		sc.draw (cell.data (), CW, CH, CW, P.t);
		int ox = (k % cols) * CW, oy = (k / cols) * CH;
		for (int y = 0; y < CH; y++) memcpy (&img[(size_t) (oy + y) * W + ox], &cell[(size_t) y * CW], CW * 4);
		fprintf (stderr, "%d : %zu sommets, %.1f ms\n", k, sc.dl.v.size (), sc.lastMs);
	}
	FILE *f = fopen (out, "wb");
	if (!f) return 1;
	fprintf (f, "P6\n%d %d\n255\n", W, H);
	for (size_t i = 0; i < img.size (); i++) { unsigned char b[3] = { (unsigned char) (img[i] >> 16), (unsigned char) (img[i] >> 8), (unsigned char) img[i] }; fwrite (b, 1, 3, f); }
	fclose (f);
	return 0;
}
