//
// pet.cpp -- Morpho : la vie de l'animal (pet.h).
//
#include "pet.h"
#include "tr.h"
#include <stdlib.h>
#include <stddef.h>

namespace mo {

// la durée de chaque stade (secondes simulées)
static const float STAGE_LEN[ST_COUNT] = { 90, 3600, 8 * 3600, 24 * 3600, 4 * 86400, 3 * 86400 };
float Pet::stageLen () const { return STAGE_LEN[clampi (g.stage, 0, ST_COUNT - 1)]; }

void Pet::reset (uint32_t lineageSeed, int gen, const Genome *parent)
{
	int keepSpeed = speed;
	*this = Pet ();
	speed = keepSpeed > 0 ? keepSpeed : 1;
	lineage = lineageSeed;
	generation = gen;
	g = makeEgg (hash32 (lineageSeed + (uint32_t) gen * 7919u));
	if (parent)
	{
		// (l'œuf garde un peu de ses parents : leurs couleurs, mélangées aux siennes)
		g.base = mixc (g.base, parent->base, 0.45f); g.pat = mixc (g.pat, parent->pat, 0.45f);
		g.bellyC = mixc (g.bellyC, parent->bellyC, 0.4f); g.iris = mixc (g.iris, parent->iris, 0.3f);
		g.voice = lerpf (g.voice, parent->voice, 0.5f);
	}
	prev = g;
	for (int i = 0; i < N_COUNT; i++) st[i] = 80;
	st[N_HEALTH] = 100;
	fat = 0.3f;
	randomName (hash32 (lineageSeed * 31u + (uint32_t) gen), name, sizeof name);
	hist[0] = 1; histSeed[0] = g.seed;
	for (int i = 0; i < N_COUNT; i++) alarmIn[i] = 0;
}

float Pet::mood () const
{
	float m = (st[N_FUN] - 50) / 50 * 0.6f + (st[N_FOOD] - 50) / 50 * 0.2f + (st[N_HEALTH] - 60) / 40 * 0.2f;
	if (sick) m -= 0.4f;
	for (int i = 0; i < N_COUNT; i++) if (st[i] < 15) m -= 0.25f;
	return clampf (m, -1, 1);
}

int Pet::worst () const
{
	if (gone || g.stage == ST_EGG) return -1;
	if (sick) return N_HEALTH;
	int w = -1; float lo = 35;
	static const int ORDER[N_COUNT] = { N_FOOD, N_CLEAN, N_ENERGY, N_FUN, N_HEALTH };
	for (int k = 0; k < N_COUNT; k++)
	{
		int i = ORDER[k];
		float v = st[i] + (i == N_ENERGY ? 10 : 0);
		if (i == N_ENERGY && asleep) continue;
		if (v < lo) { lo = v; w = i; }
	}
	if (w < 0 && poops >= 2) w = N_CLEAN;
	return w;
}

static void clamp100 (float &v) { v = clampf (v, 0, 100); }

void Pet::step (float dt, float hour, bool offline, int *events, int &nev)
{
	auto ev = [&] (int e) { if (events && nev < 16) events[nev++] = e; };
	if (gone) return;
	age += dt; stageAge += dt;
	float h = dt / 3600.0f;
	if (g.stage == ST_EGG)
	{
		if (stageAge >= stageLen ())
		{
			prev = g;
			g = evolve (g, hash32 (g.seed + 17), 0.8f);
			stageAge = 0; careSum = 0; careN = 0;
			hist[g.stage] = 1; histSeed[g.stage] = g.seed;
			for (int i = 0; i < N_COUNT; i++) st[i] = i == N_HEALTH ? 100 : 75;
			st[N_FOOD] = 60;
			ev (EV_HATCH);
		}
		return;
	}
	float k = offline ? 0.6f : 1;			// (app fermée : plus doux)
	bool night = hour >= 21.5f || hour < 7;
	// la faim, l'ennui, la fatigue, la saleté
	float food = asleep ? 4.5f : 12;
	if (g.trait == TR_GLUTTON) food *= 1.25f;
	if (g.stage == ST_BABY) food *= 1.3f;
	st[N_FOOD] -= food * h * k;
	float fun = asleep ? 1.5f : 8;
	if (g.trait == TR_PLAYFUL) fun *= 1.3f;
	if (st[N_FOOD] < 25 || st[N_CLEAN] < 30 || sick) fun += 5;
	if (poops > 0) fun += 1.5f * (float) poops;
	st[N_FUN] -= fun * h * k;
	if (asleep)
	{
		float gain = lampOff ? 30.0f : 14.0f;
		st[N_ENERGY] += gain * h;
		if (!lampOff) st[N_FUN] -= 1.5f * h;
	}
	else
	{
		float tire = g.trait == TR_SLEEPY ? 10.5f : 8.5f;
		if (night) tire *= 1.4f;
		st[N_ENERGY] -= tire * h * k;
	}
	st[N_CLEAN] -= (3.5f + 2.5f * (float) poops) * h * k;
	// les crottes
	if (poopIn > 0)
	{
		poopIn -= dt;
		if (poopIn <= 0)
		{
			poopIn = 0;
			if (poops < 4) { poops++; st[N_CLEAN] -= 12; ev (EV_POOP); }
		}
	}
	// la santé
	float hd = 0;
	if (st[N_FOOD] < 15) hd += 6;
	if (st[N_CLEAN] < 25) hd += 4;
	if (poops >= 3) hd += 3;
	if (fat > 0.75f) hd += 2.5f;
	if (sick) hd += 5;
	if (hd == 0 && st[N_FOOD] > 45 && st[N_CLEAN] > 45 && st[N_ENERGY] > 30) hd = -3;
	st[N_HEALTH] -= hd * h * k;
	// tomber malade
	if (!sick)
	{
		float p = 0.01f + (st[N_CLEAN] < 30 ? 0.08f : 0) + (st[N_FOOD] < 20 ? 0.06f : 0) + (poops >= 2 ? 0.05f : 0)
			+ (st[N_HEALTH] < 40 ? 0.06f : 0) + (fat > 0.8f ? 0.03f : 0);
		if (g.stage == ST_ELDER) p *= 1.4f;
		if (offline) p *= 0.5f;
		if ((float) rand () / (float) RAND_MAX < p * h) { sick = true; meds = 2; ev (EV_SICK); }
	}
	// le poids revient doucement vers la normale
	fat += (0.3f - fat) * 0.02f * h;
	treats = fmaxf (0, treats - 1.5f * h);
	for (int i = 0; i < N_COUNT; i++) clamp100 (st[i]);
	if (offline && st[N_HEALTH] < 10) st[N_HEALTH] = 10;
	// le sommeil
	if (!asleep && (st[N_ENERGY] < 12 || (night && st[N_ENERGY] < 55) || (lampOff && st[N_ENERGY] < 80)))
	{
		asleep = true; ev (EV_SLEEP);
	}
	else if (asleep && (st[N_ENERGY] >= 99.5f || (!night && !lampOff && st[N_ENERGY] > 80 && hour >= 7.5f && hour < 21)))
	{
		asleep = false; ev (EV_WAKE);
		if (lampOff && !offline) {}		// (la lampe reste comme elle est : le joueur la rallume)
	}
	// les fautes : un besoin laissé au plus bas une heure
	for (int i = 0; i < N_COUNT; i++)
	{
		if (st[i] < 10) { lowFor[i] += dt; if (lowFor[i] >= 3600) { mistakes++; lowFor[i] = -1e9f; } }
		else if (st[i] > 25) lowFor[i] = 0;
	}
	// les alertes (une fois, puis pas avant un moment)
	for (int i = 0; i < N_COUNT; i++) if (alarmIn[i] > 0) alarmIn[i] -= dt;
	if (!asleep && !offline)
	{
		if (st[N_FOOD] < 22 && alarmIn[N_FOOD] <= 0) { ev (EV_HUNGRY); alarmIn[N_FOOD] = 1800; }
		if (st[N_FUN] < 22 && alarmIn[N_FUN] <= 0) { ev (EV_BORED); alarmIn[N_FUN] = 1800; }
		if (st[N_ENERGY] < 20 && alarmIn[N_ENERGY] <= 0) { ev (EV_TIRED); alarmIn[N_ENERGY] = 1800; }
	}
	// la moyenne des soins
	float c = (st[N_FOOD] + st[N_FUN] + st[N_CLEAN] + st[N_HEALTH] + fminf (st[N_ENERGY] + 30, 100)) / 500;
	careSum += c * dt; careN += dt;
	// la santé à zéro trop longtemps : il s'en va
	if (st[N_HEALTH] <= 0.01f) zeroHealthFor += dt; else zeroHealthFor = 0;
	if (zeroHealthFor > 6 * 3600) { gone = true; ev (EV_GONE); return; }
	// le stade suivant
	if (stageAge >= stageLen ())
	{
		if (g.stage == ST_ELDER) { gone = true; ev (EV_GONE); return; }
		float cr = clampf (care () - (float) mistakes * 0.05f, 0, 1);
		prev = g;
		g = evolve (g, hash32 (g.seed * 2654435761u + (uint32_t) generation), cr);
		hist[g.stage] = 1; histSeed[g.stage] = g.seed;
		stageAge = 0; careSum = 0; careN = 0; mistakes = 0;
		ev (EV_EVOLVE);
	}
}

double Pet::catchUp (int64_t now, float hour)
{
	if (lastReal <= 0 || now <= lastReal) { lastReal = now; return 0; }
	double real = (double) (now - lastReal);
	lastReal = now;
	double sim = real * speed;
	if (sim > 3 * 86400) sim = 3 * 86400;			// (au plus trois jours rattrapés)
	double done = 0;
	int evs[16]; int ne = 0;
	while (done < sim && !gone)
	{
		float dt = (float) fmin (60.0, sim - done);
		// (le stade ne change pas pendant le rattrapage : la métamorphose se voit au retour)
		if (stageAge + dt >= stageLen () && g.stage != ST_EGG) { stageAge = stageLen () - 1; }
		ne = 0;
		step (dt, hour, true, evs, ne);
		done += dt;
	}
	return done;
}

// ==== les soins =======================================================================================================
Result Pet::feed (int food)
{
	if (!canAct ()) return RS_NOTHING;
	if (asleep) return RS_ASLEEP;
	if (sick && food == FOOD_TREAT) return RS_REFUSED;
	if (st[N_FOOD] > 96 && food != FOOD_TREAT) { st[N_FUN] = fmaxf (0, st[N_FUN] - 3); return RS_FULL; }
	switch (food)
	{
	case FOOD_MEAL: st[N_FOOD] += 34; fat += 0.03f; st[N_FUN] += 2; if (poopIn <= 0) poopIn = 2400 + (float) (rand () % 3000); break;
	case FOOD_FRUIT: st[N_FOOD] += 17; st[N_HEALTH] += 3; fat += 0.005f; st[N_FUN] += 3; if (poopIn <= 0) poopIn = 4000 + (float) (rand () % 3000); break;
	default:
		st[N_FOOD] += 7; st[N_FUN] += 14; fat += 0.05f; treats += 1;
		if (treats > 3) st[N_HEALTH] -= 5;
		break;
	}
	if (st[N_FOOD] > 100) { fat += (st[N_FOOD] - 100) * 0.003f; }
	fat = clampf (fat, 0, 1);
	for (int i = 0; i < N_COUNT; i++) clamp100 (st[i]);
	return RS_OK;
}
Result Pet::play ()
{
	if (!canAct ()) return RS_NOTHING;
	if (asleep) return RS_ASLEEP;
	if (st[N_ENERGY] < 12) return RS_TOOTIRED;
	if (sick && st[N_HEALTH] < 30) return RS_REFUSED;
	st[N_FUN] += g.trait == TR_PLAYFUL ? 14 : 10; st[N_ENERGY] -= 5; st[N_FOOD] -= 2; fat = fmaxf (0, fat - 0.015f);
	for (int i = 0; i < N_COUNT; i++) clamp100 (st[i]);
	return RS_OK;
}
Result Pet::playCatch ()
{
	if (!canAct () || asleep) return RS_NOTHING;
	st[N_FUN] += g.trait == TR_PLAYFUL ? 6 : 4.5f; st[N_ENERGY] -= 1.5f; st[N_FOOD] -= 0.6f; fat = fmaxf (0, fat - 0.008f);
	for (int i = 0; i < N_COUNT; i++) clamp100 (st[i]);
	return RS_OK;
}
Result Pet::clean ()
{
	if (!canAct ()) return RS_NOTHING;
	if (poops == 0 && st[N_CLEAN] > 92) return RS_NOTHING;
	poops = 0;
	st[N_CLEAN] = 100;
	if (!asleep) st[N_FUN] -= 2;
	clamp100 (st[N_FUN]);
	return RS_OK;
}
Result Pet::heal ()
{
	if (!canAct ()) return RS_NOTHING;
	if (!sick)
	{
		if (st[N_HEALTH] > 85) { st[N_FUN] = fmaxf (0, st[N_FUN] - 8); return RS_REFUSED; }
		st[N_HEALTH] += 8; st[N_FUN] -= 4;
		for (int i = 0; i < N_COUNT; i++) clamp100 (st[i]);
		return RS_OK;
	}
	st[N_HEALTH] += 15; st[N_FUN] -= 6;
	if (--meds <= 0) { sick = false; meds = 0; }
	for (int i = 0; i < N_COUNT; i++) clamp100 (st[i]);
	return sick ? RS_OK : RS_CURED;
}
Result Pet::caress (float dt)
{
	if (!canAct ()) return RS_NOTHING;
	float k = g.trait == TR_CUDDLY ? 2.2f : 1.3f;
	st[N_FUN] += k * dt * (asleep ? 0.3f : 1);
	clamp100 (st[N_FUN]);
	return RS_OK;
}
void Pet::lamp () { lampOff = !lampOff; }
Result Pet::wake ()
{
	if (!asleep) return RS_NOTHING;
	asleep = false;
	st[N_FUN] = fmaxf (0, st[N_FUN] - 10);
	return RS_OK;
}

// ==== la sauvegarde ===================================================================================================
struct Field { const char *key; int kind; size_t off; };	// kind : 0 float, 1 int, 2 Col
#define GF(k, f) { k, 0, offsetof (Genome, f) }
#define GI(k, f) { k, 1, offsetof (Genome, f) }
#define GC(k, f) { k, 2, offsetof (Genome, f) }
static const Field GFIELDS[] = {
	GI ("seed", seed), GI ("stage", stage), GF ("size", size),
	GF ("bodyLen", bodyLen), GF ("bodyR", bodyR), GF ("bodyH", bodyH), GF ("belly", belly), GF ("hump", hump), GF ("upright", upright),
	GF ("neckLen", neckLen), GF ("neckR", neckR), GF ("headR", headR), GF ("snout", snout), GF ("headFlat", headFlat), GF ("headW", headW),
	GF ("tailLen", tailLen), GF ("tailR", tailR), GF ("tailCurl", tailCurl),
	GI ("legs", legs), GF ("legLen", legLen), GF ("legR", legR), GI ("foot", foot), GI ("arms", arms), GF ("armLen", armLen),
	GI ("eyes", eyes), GF ("eyeR", eyeR), GF ("eyeSpread", eyeSpread), GI ("eyeStalk", eyeStalk), GI ("brows", brows),
	GI ("mouth", mouth), GF ("mouthSize", mouthSize), GI ("horn", horn), GF ("hornLen", hornLen), GF ("hornCurve", hornCurve),
	GI ("ear", ear), GF ("earSize", earSize), GI ("back", back), GF ("backSize", backSize), GI ("backCount", backCount),
	GI ("wing", wing), GF ("wingSize", wingSize), GI ("tip", tip),
	GC ("base", base), GC ("bellyC", bellyC), GC ("pat", pat), GC ("accent", accent), GC ("iris", iris), GC ("membrane", membrane),
	GI ("pattern", pattern), GF ("patScale", patScale), GF ("gloss", gloss), GF ("blush", blush), GF ("voice", voice), GI ("trait", trait),
};
#undef GF
#undef GI
#undef GC

static void writeGenome (FILE *f, const char *pre, const Genome &g)
{
	for (const Field &x : GFIELDS)
	{
		const char *p = (const char *) &g + x.off;
		if (x.kind == 0) fprintf (f, "%s.%s = %.5f\n", pre, x.key, (double) *(const float *) p);
		else if (x.kind == 1) fprintf (f, "%s.%s = %u\n", pre, x.key, *(const unsigned *) p);
		else { const Col *c = (const Col *) p; fprintf (f, "%s.%s = %.4f %.4f %.4f\n", pre, x.key, (double) c->r, (double) c->g, (double) c->b); }
	}
}
static bool readGenome (const char *pre, const char *key, const char *val, Genome &g)
{
	size_t n = strlen (pre);
	if (strncmp (key, pre, n) || key[n] != '.') return false;
	for (const Field &x : GFIELDS)
		if (!strcmp (key + n + 1, x.key))
		{
			char *p = (char *) &g + x.off;
			if (x.kind == 0) *(float *) p = (float) atof (val);
			else if (x.kind == 1) *(unsigned *) p = (unsigned) strtoul (val, 0, 10);
			else { Col *c = (Col *) p; sscanf (val, "%f %f %f", &c->r, &c->g, &c->b); }
			return true;
		}
	return true;
}

bool Pet::save (const char *path) const
{
	char tmp[256]; snprintf (tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen (tmp, "w");
	if (!f) return false;
	fprintf (f, "# Morpho : l'animal de compagnie (src/apps/morpho)\n");
	fprintf (f, "name = %s\ngeneration = %d\nlineage = %u\nage = %.1f\nstageAge = %.1f\nlastReal = %lld\nspeed = %d\n", name, generation, lineage,
		 age, stageAge, (long long) lastReal, speed);
	fprintf (f, "food = %.2f\nfun = %.2f\nenergy = %.2f\nclean = %.2f\nhealth = %.2f\nfat = %.4f\n", (double) st[N_FOOD], (double) st[N_FUN],
		 (double) st[N_ENERGY], (double) st[N_CLEAN], (double) st[N_HEALTH], (double) fat);
	fprintf (f, "poops = %d\npoopIn = %.0f\nsick = %d\nmeds = %d\nasleep = %d\nlampOff = %d\ncareSum = %.2f\ncareN = %.2f\nmistakes = %d\n",
		 poops, (double) poopIn, sick, meds, asleep, lampOff, careSum, (double) careN, mistakes);
	fprintf (f, "zeroHealthFor = %.0f\ntreats = %.2f\ngone = %d\n", (double) zeroHealthFor, (double) treats, gone);
	for (int i = 0; i < ST_COUNT; i++) if (hist[i]) fprintf (f, "hist%d = %u\n", i, histSeed[i]);
	writeGenome (f, "g", g);
	writeGenome (f, "prev", prev);
	bool ok = fclose (f) == 0;
	if (ok) { remove (path); ok = rename (tmp, path) == 0; }
	return ok;
}

bool Pet::load (const char *path)
{
	FILE *f = fopen (path, "r");
	if (!f) return false;
	char line[256];
	int seen = 0;
	while (fgets (line, sizeof line, f))
	{
		char *e = strchr (line, '=');
		if (!e || line[0] == '#') continue;
		*e = 0;
		char *k = line, *v = e + 1;
		while (*k == ' ') k++;
		for (char *q = e - 1; q >= k && (*q == ' ' || *q == '\t'); q--) *q = 0;
		while (*v == ' ') v++;
		size_t L = strlen (v);
		while (L && (v[L - 1] == '\n' || v[L - 1] == '\r' || v[L - 1] == ' ')) v[--L] = 0;
		seen++;
		if (!strcmp (k, "name")) snprintf (name, sizeof name, "%s", v);
		else if (!strcmp (k, "generation")) generation = atoi (v);
		else if (!strcmp (k, "lineage")) lineage = (uint32_t) strtoul (v, 0, 10);
		else if (!strcmp (k, "age")) age = atof (v);
		else if (!strcmp (k, "stageAge")) stageAge = atof (v);
		else if (!strcmp (k, "lastReal")) lastReal = (int64_t) atoll (v);
		else if (!strcmp (k, "speed")) speed = clampi (atoi (v), 1, 600);
		else if (!strcmp (k, "food")) st[N_FOOD] = (float) atof (v);
		else if (!strcmp (k, "fun")) st[N_FUN] = (float) atof (v);
		else if (!strcmp (k, "energy")) st[N_ENERGY] = (float) atof (v);
		else if (!strcmp (k, "clean")) st[N_CLEAN] = (float) atof (v);
		else if (!strcmp (k, "health")) st[N_HEALTH] = (float) atof (v);
		else if (!strcmp (k, "fat")) fat = (float) atof (v);
		else if (!strcmp (k, "poops")) poops = atoi (v);
		else if (!strcmp (k, "poopIn")) poopIn = (float) atof (v);
		else if (!strcmp (k, "sick")) sick = atoi (v) != 0;
		else if (!strcmp (k, "meds")) meds = atoi (v);
		else if (!strcmp (k, "asleep")) asleep = atoi (v) != 0;
		else if (!strcmp (k, "lampOff")) lampOff = atoi (v) != 0;
		else if (!strcmp (k, "careSum")) careSum = atof (v);
		else if (!strcmp (k, "careN")) careN = (float) atof (v);
		else if (!strcmp (k, "mistakes")) mistakes = atoi (v);
		else if (!strcmp (k, "zeroHealthFor")) zeroHealthFor = (float) atof (v);
		else if (!strcmp (k, "treats")) treats = (float) atof (v);
		else if (!strcmp (k, "gone")) gone = atoi (v) != 0;
		else if (!strncmp (k, "hist", 4) && k[4] >= '0' && k[4] <= '9') { int i = clampi (atoi (k + 4), 0, ST_COUNT - 1); hist[i] = 1; histSeed[i] = (uint32_t) strtoul (v, 0, 10); }
		else if (!readGenome ("g", k, v, g)) readGenome ("prev", k, v, prev);
	}
	fclose (f);
	g.stage = clampi (g.stage, 0, ST_COUNT - 1);
	return seen > 10;
}

// ==== les noms ========================================================================================================
void randomName (uint32_t seed, char *out, int cap)
{
	static const char *const A[] = { "Pi", "Lou", "Mo", "Zou", "Ki", "Na", "Bou", "Ti", "No", "Ru", "Mi", "Pa", "Ko", "Lé", "Zi", "Cho",
					 "Gri", "Fla", "Nou", "Pim", "Ya", "Do", "Fi", "Ba", "Mé", "Sa", "Tou", "Bi" };
	static const char *const B[] = { "pou", "ka", "lou", "nou", "bi", "ki", "mo", "do", "lin", "zou", "pi", "ra", "ti", "nette", "chou",
					 "lo", "mi", "ko", "zi", "bou", "ny", "ri", "bo" };
	Rng r (seed);
	snprintf (out, (size_t) cap, "%s%s", A[r.irange (0, (int) (sizeof A / sizeof *A) - 1)], B[r.irange (0, (int) (sizeof B / sizeof *B) - 1)]);
}

// le nom savant : le genre vient de la pièce la plus frappante, l'espèce de la couleur et du motif
void speciesName (const Genome &g, char *out, int cap)
{
	const char *genus = "Morphus";
	if (g.stage == ST_EGG) genus = "Ovum";
	else if (g.wing == WING_INSECT) genus = "Libellus";
	else if (g.wing) genus = "Pteryx";
	else if (g.mouth == MOUTH_TRUNK) genus = "Proboscis";
	else if (g.horn == HORN_UNI) genus = "Monoceros";
	else if (g.horn == HORN_RAM) genus = "Arietus";
	else if (g.horn == HORN_PAIR) genus = "Cornutus";
	else if (g.horn == HORN_ANTENNAE) genus = "Antennarius";
	else if (g.back == BACK_SAIL) genus = "Velifer";
	else if (g.back == BACK_SPIKES) genus = "Spinosus";
	else if (g.mouth == MOUTH_BEAK) genus = "Rostratus";
	else if (g.mouth == MOUTH_MANDIBLES) genus = "Mandibulus";
	else if (g.legs >= 3) genus = "Hexapus";
	else if (g.upright > 0.5f) genus = "Bipes";
	else if (g.legs == 0) genus = "Globulus";
	else if (g.eyes != 2) genus = "Oculatus";
	// la couleur : la teinte de la robe
	const Col &c = g.base;
	float mx = fmaxf (c.r, fmaxf (c.g, c.b)), mn = fminf (c.r, fminf (c.g, c.b)), hh = 0;
	if (mx > mn)
	{
		float d = mx - mn;
		if (mx == c.r) hh = (c.g - c.b) / d / 6; else if (mx == c.g) hh = (2 + (c.b - c.r) / d) / 6; else hh = (4 + (c.r - c.g) / d) / 6;
		if (hh < 0) hh += 1;
	}
	const char *col;
	if (mx - mn < 0.12f) col = mx > 0.7f ? "albus" : "griseus";
	else if (hh < 0.04f || hh >= 0.93f) col = "ruber";
	else if (hh < 0.11f) col = "aurantius";
	else if (hh < 0.19f) col = "flavus";
	else if (hh < 0.45f) col = "viridis";
	else if (hh < 0.55f) col = "turquinus";
	else if (hh < 0.7f) col = "caeruleus";
	else if (hh < 0.8f) col = "violaceus";
	else col = "roseus";
	static const char *const PAT[PAT_COUNT] = { "", " maculatus", " striatus", " variegatus", " dorsalis", " annulatus", " punctatus" };
	snprintf (out, (size_t) cap, "%s %s%s", genus, col, PAT[clampi (g.pattern, 0, PAT_COUNT - 1)]);
}

const char *stageName (int s)
{
	static const char *const N[ST_COUNT] = { "Œuf", "Bébé", "Enfant", "Adolescent", "Adulte", "Ancien" };
	return TR (N[clampi (s, 0, ST_COUNT - 1)]);
}
const char *traitName (int t)
{
	static const char *const N[TR_COUNT] = { "Joueur", "Gourmand", "Dormeur", "Câlin", "Curieux" };
	return TR (N[clampi (t, 0, TR_COUNT - 1)]);
}

void evolutionText (const Genome &a, const Genome &b, char *out, int cap)
{
	out[0] = 0;
	int n = 0;
	auto line = [&] (const char *s) {
		if (n >= 4) return;
		size_t L = strlen (out);
		snprintf (out + L, (size_t) cap - L, "%s%s", L ? "\n" : "", TR (s));
		n++;
	};
	if (!a.wing && b.wing) line ("Des ailes apparaissent !");
	if (a.horn != b.horn && b.horn) line (b.horn == HORN_ANTENNAE ? "Des antennes poussent." : b.horn == HORN_UNI ? "Une corne pousse sur le front." : "Des cornes poussent.");
	if (a.horn && !b.horn) line ("Les cornes tombent.");
	if (a.legs != b.legs && b.legs > a.legs) line (a.legs == 0 ? "Des pattes apparaissent." : "De nouvelles pattes poussent.");
	if (a.legs > b.legs && b.legs > 0) line ("Moins de pattes, mais plus solides.");
	if (a.upright < 0.5f && b.upright > 0.5f) line ("Debout sur deux pattes !");
	if (a.upright > 0.5f && b.upright < 0.5f) line ("Retour à quatre pattes.");
	if (a.mouth != b.mouth)
		line (b.mouth == MOUTH_BEAK ? "Un bec se forme." : b.mouth == MOUTH_TRUNK ? "Une trompe s'allonge." : b.mouth == MOUTH_MANDIBLES ? "Des mandibules apparaissent." :
		      b.mouth == MOUTH_FANGS ? "De petits crocs pointent." : "Un grand sourire.");
	if (a.ear != b.ear && b.ear) line (b.ear == EAR_FINS ? "Des nageoires sur les joues." : b.ear == EAR_LONG ? "De longues oreilles." : "De nouvelles oreilles.");
	if (a.back != b.back && b.back) line (b.back == BACK_SAIL ? "Une voile sur le dos." : b.back == BACK_SPIKES ? "Des piquants sur le dos." : b.back == BACK_PLATES ? "Des plaques sur le dos." : "Des touffes sur le dos.");
	if (a.eyes != b.eyes) line (b.eyes > a.eyes ? "Des yeux en plus !" : "Un regard plus perçant.");
	if (a.pattern != b.pattern && b.pattern) line ("Une nouvelle robe.");
	if (b.tailLen > a.tailLen + 0.2f) line ("La queue s'allonge.");
	if (b.neckLen > a.neckLen + 0.15f) line ("Le cou s'allonge.");
	if (b.legLen > a.legLen + 0.12f && b.legs) line ("Les pattes grandissent.");
	if (n < 2) line ("Les couleurs changent.");
}

}	// namespace mo
