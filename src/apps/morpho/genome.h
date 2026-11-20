//
// genome.h -- Morpho : le génome de la créature (sa forme, ses pièces, ses couleurs, son caractère), tiré au hasard à
// l'éclosion puis à chaque stade (evolve), d'après le génome d'avant et les soins reçus. Tout est paramétrique : la
// créature est construite à partir de ces nombres (creature.cpp), rien n'est modelé à la main.
//
#ifndef MORPHO_GENOME_H
#define MORPHO_GENOME_H
#include "m3d.h"
#include <stdio.h>

namespace mo {

enum Stage { ST_EGG, ST_BABY, ST_CHILD, ST_TEEN, ST_ADULT, ST_ELDER, ST_COUNT };
enum Mouth { MOUTH_SMILE, MOUTH_BEAK, MOUTH_FANGS, MOUTH_MANDIBLES, MOUTH_TRUNK, MOUTH_COUNT };
enum Horn { HORN_NONE, HORN_PAIR, HORN_ANTENNAE, HORN_UNI, HORN_RAM, HORN_COUNT };
enum Ear { EAR_NONE, EAR_ROUND, EAR_POINTY, EAR_LONG, EAR_FINS, EAR_COUNT };
enum Back { BACK_NONE, BACK_SPIKES, BACK_PLATES, BACK_SAIL, BACK_TUFTS, BACK_COUNT };
enum Foot { FOOT_PAW, FOOT_HOOF, FOOT_CLAW, FOOT_COUNT };
enum Wing { WING_NONE, WING_FEATHER, WING_MEMBRANE, WING_INSECT, WING_COUNT };
enum Tip { TIP_NONE, TIP_TUFT, TIP_CLUB, TIP_FIN, TIP_COUNT };
enum Pattern { PAT_PLAIN, PAT_SPOTS, PAT_STRIPES, PAT_PATCHES, PAT_SADDLE, PAT_RINGS, PAT_SPECKLE, PAT_COUNT };
enum Trait { TR_PLAYFUL, TR_GLUTTON, TR_SLEEPY, TR_CUDDLY, TR_CURIOUS, TR_COUNT };

struct Genome
{
	uint32_t seed;
	int stage;
	float size;			// échelle d'ensemble
	// le corps : une colonne de la queue au museau
	float bodyLen, bodyR, bodyH, belly, hump, upright;
	float neckLen, neckR, headR, snout, headFlat, headW;
	float tailLen, tailR, tailCurl;
	// les membres
	int legs; float legLen, legR; int foot;
	int arms; float armLen;
	// la face
	int eyes; float eyeR, eyeSpread; int eyeStalk; int brows;
	int mouth; float mouthSize;
	int horn; float hornLen, hornCurve;
	int ear; float earSize;
	// le dos, les ailes, le bout de la queue
	int back; float backSize; int backCount;
	int wing; float wingSize;
	int tip;
	// la peau
	Col base, bellyC, pat, accent, iris, membrane;
	int pattern; float patScale, gloss, blush;
	// le caractère
	float voice; int trait;
};

static inline float lerpg (float a, float b, float t) { return a + (b - a) * t; }

// les couleurs : une harmonie autour d'une teinte de base
static inline void drawPalette (Genome &g, Rng &r, float vivid)
{
	float h = r.f ();
	float s = clampf (r.range (0.42f, 0.82f) * (0.75f + 0.35f * vivid), 0.25f, 0.92f);
	float v = clampf (r.range (0.68f, 0.95f) * (0.86f + 0.16f * vivid), 0.5f, 0.97f);
	g.base = hsv (h, s, v);
	// le ventre : plus clair, moins saturé, parfois crème
	g.bellyC = r.chance (0.35f) ? hsv (r.range (0.08f, 0.14f), r.range (0.12f, 0.3f), 0.97f)
				   : hsv (h + r.range (-0.04f, 0.04f), s * r.range (0.25f, 0.55f), clampf (v + 0.15f, 0, 0.98f));
	// le motif : analogue, complémentaire, triade, ou plus foncé
	static const float W[4] = { 3, 2, 1.5f, 2.5f };
	float ph;
	switch (r.pick (W, 4))
	{
	case 0: ph = h + (r.chance (0.5f) ? 0.09f : -0.09f); break;
	case 1: ph = h + 0.5f; break;
	case 2: ph = h + (r.chance (0.5f) ? 0.33f : -0.33f); break;
	default: ph = h; break;
	}
	float pv = ph == h ? v * r.range (0.45f, 0.62f) : clampf (v * r.range (0.6f, 1.05f), 0.25f, 0.97f);
	g.pat = hsv (ph, clampf (s * r.range (0.9f, 1.25f), 0, 0.95f), pv);
	// les cornes, griffes, bec : ivoire, brun, noir, or, ou la couleur du motif
	static const unsigned ACC[6] = { 0xF2E6C8, 0x5A4030, 0x2A2630, 0xE8B848, 0xD8D0E0, 0xF0A040 };
	g.accent = r.chance (0.25f) ? g.pat : hexc (ACC[r.irange (0, 5)]);
	// les yeux : une couleur vive, souvent opposée
	g.iris = hsv (r.chance (0.6f) ? h + 0.5f + r.range (-0.1f, 0.1f) : r.f (), r.range (0.55f, 0.9f), r.range (0.55f, 0.9f));
	g.membrane = hsv (ph + r.range (-0.05f, 0.05f), clampf (s * 0.9f, 0, 0.9f), clampf (v * 0.95f, 0, 0.95f));
	g.gloss = r.range (0.15f, 0.7f);
}

// les couleurs changent un peu (ou beaucoup) d'un stade au suivant
static inline void mutatePalette (Genome &g, Rng &r, float vivid)
{
	if (r.chance (0.2f)) { drawPalette (g, r, vivid); return; }			// une toute nouvelle robe
	float dh = r.chance (0.55f) ? r.range (-0.12f, 0.12f) : 0;
	auto shift = [&] (Col &c, float k) {
		// (la teinte tourne : le plus simple, d'un mélange vers la même couleur décalée)
		float mx = fmaxf (c.r, fmaxf (c.g, c.b)), mn = fminf (c.r, fminf (c.g, c.b));
		float v = mx, s = mx > 0 ? (mx - mn) / mx : 0, h = 0;
		if (mx > mn)
		{
			float d = mx - mn;
			if (mx == c.r) h = (c.g - c.b) / d / 6; else if (mx == c.g) h = (2 + (c.b - c.r) / d) / 6; else h = (4 + (c.r - c.g) / d) / 6;
		}
		c = hsv (h + dh * k, clampf (s * (0.92f + 0.16f * vivid), 0.05f, 0.95f), clampf (v * (0.95f + 0.07f * vivid), 0.2f, 0.98f));
	};
	shift (g.base, 1); shift (g.bellyC, 1); shift (g.membrane, 1);
	if (r.chance (0.35f)) shift (g.pat, r.range (-2, 3)); else shift (g.pat, 1);
	if (r.chance (0.3f)) g.iris = hsv (r.f (), r.range (0.55f, 0.9f), r.range (0.55f, 0.9f));
}

static inline Genome makeEgg (uint32_t seed)
{
	Genome g = Genome ();
	Rng r (seed);
	g.seed = seed; g.stage = ST_EGG; g.size = 0.62f;
	drawPalette (g, r, 0.6f);
	g.pattern = r.chance (0.7f) ? PAT_SPOTS : PAT_STRIPES;
	g.patScale = r.range (0.8f, 1.3f);
	g.voice = r.range (0.8f, 1.35f);
	g.trait = r.irange (0, TR_COUNT - 1);
	g.eyes = 2; g.eyeR = 0.1f;
	return g;
}

// le stade suivant : care 0..1 (la moyenne des soins pendant le stade)
static inline Genome evolve (const Genome &p, uint32_t seed, float care)
{
	Genome g = p;
	Rng r (seed);
	g.seed = seed;
	g.stage = p.stage + 1;
	float vivid = care;						// (bien soigné : des couleurs franches)
	float keep = 0.55f;						// une pièce garde sa forme d'un stade à l'autre
	auto tweak = [&] (float &v, float a, float b, float amount) { v = clampf (v + r.range (-amount, amount) * (b - a), a, b); };
	switch (g.stage)
	{
	case ST_BABY:	// une boule, de grands yeux, presque rien d'autre
		g.size = 0.5f;
		g.bodyLen = r.range (0.04f, 0.16f); g.bodyR = r.range (0.3f, 0.36f); g.bodyH = r.range (0.92f, 1.08f);
		g.belly = r.range (0, 0.12f); g.hump = 0; g.upright = r.chance (0.35f) ? r.range (0.6f, 0.9f) : 0;
		g.neckLen = 0; g.neckR = 0.9f; g.headR = r.range (0.3f, 0.36f); g.snout = r.range (0, 0.05f);
		g.headFlat = r.range (0.88f, 1.0f); g.headW = r.range (1.0f, 1.12f);
		g.tailLen = r.range (0.05f, 0.25f); g.tailR = r.range (0.25f, 0.45f); g.tailCurl = r.range (-0.2f, 0.6f);
		g.legs = r.chance (0.6f) ? 1 : 0; g.legLen = 0.16f; g.legR = r.range (0.07f, 0.09f); g.foot = FOOT_PAW;
		g.arms = 0; g.armLen = 0.15f;
		g.eyes = r.chance (0.88f) ? 2 : 1; g.eyeR = r.range (0.11f, 0.14f); g.eyeSpread = r.range (0.35f, 0.6f);
		g.eyeStalk = 0; g.brows = 0;
		g.mouth = MOUTH_SMILE; g.mouthSize = r.range (0.8f, 1.1f);
		g.horn = HORN_NONE; g.ear = r.chance (0.35f) ? EAR_ROUND : EAR_NONE; g.earSize = r.range (0.5f, 0.8f);
		g.back = BACK_NONE; g.wing = WING_NONE; g.tip = TIP_NONE;
		g.pattern = r.chance (0.6f) ? PAT_PLAIN : (r.chance (0.5f) ? PAT_SPOTS : PAT_SADDLE);
		g.patScale = r.range (0.8f, 1.3f); g.blush = r.range (0.5f, 1);
		if (r.chance (0.5f)) mutatePalette (g, r, vivid);
		// (des couleurs tendres)
		g.base = mixc (g.base, Col (1, 1, 1), 0.18f); g.pat = mixc (g.pat, Col (1, 1, 1), 0.1f);
		break;
	case ST_CHILD:	// des pattes, une queue, les premières oreilles
		g.size = 0.68f;
		g.bodyLen = r.range (0.25f, 0.55f); g.bodyR = r.range (0.24f, 0.32f); g.bodyH = r.range (0.9f, 1.15f);
		g.belly = r.range (0, 0.25f); g.hump = r.range (-0.02f, 0.1f);
		if (p.upright > 0.5f ? r.chance (0.8f) : r.chance (0.2f)) g.upright = r.range (0.65f, 1); else g.upright = 0;
		g.neckLen = r.range (0.04f, 0.18f); g.neckR = r.range (0.6f, 0.85f); g.headR = r.range (0.24f, 0.3f);
		g.snout = r.range (0, 0.16f); g.headFlat = r.range (0.82f, 1.02f); g.headW = r.range (0.92f, 1.1f);
		g.tailLen = r.range (0.15f, 0.6f); g.tailR = r.range (0.25f, 0.55f); g.tailCurl = r.range (-0.5f, 0.8f);
		g.legs = g.upright > 0.5f ? 1 : (r.chance (0.75f) ? 2 : 1); g.legLen = r.range (0.22f, 0.36f); g.legR = r.range (0.06f, 0.085f);
		g.foot = r.chance (0.7f) ? FOOT_PAW : r.irange (0, FOOT_COUNT - 1);
		g.arms = g.upright > 0.5f ? 1 : 0; g.armLen = r.range (0.2f, 0.32f);
		if (r.chance (0.12f)) g.eyes = r.chance (0.5f) ? 1 : 3;
		g.eyeR = r.range (0.085f, 0.11f); tweak (g.eyeSpread, 0.25f, 0.75f, 0.3f);
		g.eyeStalk = r.chance (0.1f); g.brows = r.chance (0.4f);
		g.mouth = r.chance (0.65f) ? MOUTH_SMILE : (r.chance (0.6f) ? MOUTH_BEAK : MOUTH_FANGS); g.mouthSize = r.range (0.8f, 1.15f);
		g.horn = r.chance (0.3f) ? (r.chance (0.6f) ? HORN_ANTENNAE : HORN_UNI) : HORN_NONE; g.hornLen = r.range (0.12f, 0.22f); g.hornCurve = r.range (-0.5f, 0.8f);
		if (!(p.ear && r.chance (keep))) g.ear = r.chance (0.6f) ? r.irange (1, EAR_COUNT - 1) : EAR_NONE;
		g.earSize = r.range (0.6f, 1.0f);
		g.back = r.chance (0.25f) ? (r.chance (0.5f) ? BACK_TUFTS : BACK_SPIKES) : BACK_NONE; g.backSize = r.range (0.4f, 0.7f); g.backCount = r.irange (3, 6);
		g.wing = WING_NONE; g.tip = r.chance (0.3f) ? r.irange (1, TIP_COUNT - 1) : TIP_NONE;
		if (!(p.pattern && r.chance (keep))) g.pattern = r.irange (0, PAT_COUNT - 1);
		g.patScale = r.range (0.7f, 1.4f); g.blush = p.blush * 0.6f;
		mutatePalette (g, r, vivid);
		break;
	case ST_TEEN:	// le corps s'allonge, les pièces prennent leur forme
	case ST_ADULT:	// tout pousse ; des ailes, parfois
	{
		bool ad = g.stage == ST_ADULT;
		g.size = ad ? 1.0f : 0.84f;
		tweak (g.bodyLen, 0.35f, 1.15f, 0.5f); g.bodyLen = fmaxf (g.bodyLen, ad ? 0.5f : 0.4f);
		tweak (g.bodyR, 0.2f, 0.36f, 0.6f); tweak (g.bodyH, 0.82f, 1.28f, 0.5f);
		tweak (g.belly, 0, 0.4f, 0.5f); tweak (g.hump, -0.06f, 0.28f, 0.5f);
		if (r.chance (0.2f)) g.upright = g.upright > 0.5f ? 0 : r.range (0.65f, 1);
		tweak (g.neckLen, 0.04f, ad ? 0.6f : 0.4f, 0.6f); tweak (g.neckR, 0.45f, 0.85f, 0.5f);
		tweak (g.headR, 0.17f, 0.3f, 0.6f); tweak (g.snout, 0, 0.42f, 0.6f);
		tweak (g.headFlat, 0.72f, 1.1f, 0.5f); tweak (g.headW, 0.82f, 1.2f, 0.5f);
		tweak (g.tailLen, 0.1f, ad ? 1.3f : 0.9f, 0.6f); tweak (g.tailR, 0.2f, 0.65f, 0.5f); tweak (g.tailCurl, -1, 1, 0.5f);
		if (g.upright > 0.5f) { g.legs = 1; g.arms = 1; }
		else
		{
			static const float WL[3] = { 1, 5, 1.4f };
			if (!r.chance (keep) || g.legs < 1) g.legs = 1 + r.pick (WL, 3);
			g.arms = 0;
		}
		tweak (g.legLen, 0.28f, ad ? 0.72f : 0.55f, 0.6f); g.legLen = fmaxf (g.legLen, ad ? 0.36f : 0.3f);
		tweak (g.legR, 0.045f, 0.12f, 0.5f);
		if (!r.chance (keep)) g.foot = r.irange (0, FOOT_COUNT - 1);
		tweak (g.armLen, 0.25f, 0.5f, 0.6f);
		if (r.chance (ad ? 0.12f : 0.08f)) { static const float WE[4] = { 1, 10, 1.5f, 1 }; g.eyes = 1 + r.pick (WE, 4); }
		g.eyeR = r.range (ad ? 0.06f : 0.07f, ad ? 0.095f : 0.1f); tweak (g.eyeSpread, 0.2f, 0.85f, 0.4f);
		if (r.chance (0.12f)) g.eyeStalk = !g.eyeStalk;
		if (r.chance (0.3f)) g.brows = !g.brows;
		if (!r.chance (keep)) g.mouth = r.irange (0, MOUTH_COUNT - 1);
		tweak (g.mouthSize, 0.75f, 1.3f, 0.4f);
		if (!(g.horn && r.chance (keep))) g.horn = r.chance (ad ? 0.7f : 0.55f) ? r.irange (1, HORN_COUNT - 1) : HORN_NONE;
		tweak (g.hornLen, 0.15f, ad ? 0.45f : 0.3f, 0.6f); g.hornLen = fmaxf (g.hornLen, ad ? 0.2f : 0.14f); tweak (g.hornCurve, -1, 1, 0.6f);
		if (!(g.ear && r.chance (keep))) g.ear = r.chance (0.6f) ? r.irange (1, EAR_COUNT - 1) : EAR_NONE;
		tweak (g.earSize, 0.6f, 1.35f, 0.5f);
		if (!(g.back && r.chance (keep))) g.back = r.chance (ad ? 0.65f : 0.5f) ? r.irange (1, BACK_COUNT - 1) : BACK_NONE;
		tweak (g.backSize, 0.45f, ad ? 1.25f : 0.9f, 0.6f); g.backCount = r.irange (4, 9);
		if (ad) g.wing = r.chance (0.2f + 0.3f * care) ? r.irange (1, WING_COUNT - 1) : WING_NONE;
		tweak (g.wingSize, 0.7f, 1.3f, 1);
		if (!(g.tip && r.chance (keep))) g.tip = r.chance (0.5f) ? r.irange (1, TIP_COUNT - 1) : TIP_NONE;
		if (!(g.pattern && r.chance (keep))) g.pattern = r.irange (0, PAT_COUNT - 1);
		tweak (g.patScale, 0.6f, 1.5f, 0.5f);
		tweak (g.gloss, 0.1f, 0.85f, 0.4f);
		g.blush = 0;
		mutatePalette (g, r, vivid);
		break;
	}
	case ST_ELDER:	// le grand âge : plus pâle, un peu voûté, plus rond
		g.size = 0.97f;
		g.hump = clampf (g.hump + 0.08f, -0.1f, 0.35f); g.belly = clampf (g.belly + 0.08f, 0, 0.45f);
		g.brows = 1;
		g.base = mixc (g.base, Col (0.82f, 0.82f, 0.8f), 0.3f);
		g.pat = mixc (g.pat, Col (0.8f, 0.8f, 0.78f), 0.25f);
		g.bellyC = mixc (g.bellyC, Col (0.95f, 0.95f, 0.93f), 0.3f);
		break;
	default: break;
	}
	return g;
}

// le mélange de deux génomes (la métamorphose) : les nombres glissent, les pièces changent à mi-chemin
static inline Genome blend (const Genome &a, const Genome &b, float t)
{
	Genome g = t < 0.5f ? a : b;
	g.size = lerpg (a.size, b.size, t);
#define L(f) g.f = lerpg (a.f, b.f, t)
	L (bodyLen); L (bodyR); L (bodyH); L (belly); L (hump); L (upright); L (neckLen); L (neckR); L (headR); L (snout);
	L (headFlat); L (headW); L (tailLen); L (tailR); L (tailCurl); L (eyeR); L (eyeSpread); L (mouthSize);
	L (gloss); L (blush); L (patScale);
#undef L
	g.base = mixc (a.base, b.base, t); g.bellyC = mixc (a.bellyC, b.bellyC, t); g.pat = mixc (a.pat, b.pat, t);
	g.accent = mixc (a.accent, b.accent, t); g.iris = mixc (a.iris, b.iris, t); g.membrane = mixc (a.membrane, b.membrane, t);
	return g;
}

}	// namespace mo
#endif
