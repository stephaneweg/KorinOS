//
// actor.cpp -- Morpho : l'animal dans son studio (actor.h).
//
#include "actor.h"
#include <stdlib.h>

namespace mo {

static float frand () { return (float) rand () / (float) RAND_MAX; }
static float wrapA (float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static float approach (float v, float to, float rate, float dt) { float k = 1 - expf (-rate * dt); return v + (to - v) * k; }
static V3 fwdOf (float yaw) { return V3 (sinf (yaw), 0, cosf (yaw)); }

void Actor::reset (const Pet &p)
{
	*this = Actor ();
	pos = V3 (0, 0, 0); yaw = 0.25f;
	lookCamFor = 3; lookAt = V3 (0, 0.8f, 4);
	shown = p.g; from = p.prev; to = p.g;
	for (int i = 0; i < p.poops; i++) poops.push_back ({ V3 (-0.5f + 0.35f * (float) i, 0, -0.6f + 0.1f * (float) i), 10, 1 });
	if (p.gone) act = A_GONE;
	skinDirty = true;
}

void Actor::emit (int kind, const V3 &at, int n, unsigned col, float spread)
{
	for (int i = 0; i < n; i++)
	{
		Particle q;
		q.kind = kind; q.col = col;
		q.p = at + V3 ((frand () - 0.5f) * spread, (frand () - 0.5f) * spread * 0.5f, (frand () - 0.5f) * spread);
		q.spin = (frand () - 0.5f) * 4;
		switch (kind)
		{
		case P_HEART: case P_NOTE: q.v = V3 ((frand () - 0.5f) * 0.25f, 0.45f + frand () * 0.25f, 0); q.max = 1.6f; q.size = 1; break;
		case P_Z: q.v = V3 (0.12f, 0.22f, 0); q.max = 2.6f; q.size = 1; break;
		case P_STAR: case P_SPARK: { float a = frand () * 2 * PI, s = 0.6f + frand () * 1.2f; q.v = V3 (cosf (a) * s, 0.4f + frand () * 1.2f, sinf (a) * s); q.max = 0.9f + frand () * 0.6f; q.size = 0.6f + frand () * 0.8f; break; }
		case P_DROP: { float a = frand () * 2 * PI; q.v = V3 (cosf (a) * 1.4f, 0.8f + frand (), sinf (a) * 1.4f); q.max = 0.8f; q.size = 0.7f; break; }
		case P_CRUMB: { float a = frand () * 2 * PI; q.v = V3 (cosf (a) * 0.6f, 0.6f + frand () * 0.6f, sinf (a) * 0.6f); q.max = 0.7f; q.size = 0.6f; break; }
		case P_SHELL: { float a = frand () * 2 * PI; q.v = V3 (cosf (a) * 1.3f, 1.4f + frand () * 1.4f, sinf (a) * 1.3f); q.max = 1.3f; q.size = 0.8f + frand () * 0.8f; break; }
		case P_STINK: q.v = V3 (0, 0.18f, 0); q.max = 1.8f; q.size = 1; break;
		case P_BUBBLE: q.v = V3 ((frand () - 0.5f) * 0.3f, 0.35f + frand () * 0.3f, (frand () - 0.5f) * 0.3f); q.max = 1.4f + frand (); q.size = 0.6f + frand (); break;
		default: q.v = V3 (); q.max = 1; q.size = 1; break;
		}
		q.life = q.max;
		if (parts.size () < 400) parts.push_back (q);
	}
}

void Actor::start (int a, Pet &p)
{
	act = a; actT = 0;
	switch (a)
	{
	case A_EAT: case A_MEDICINE:
	{
		walking = false;
		V3 f = fwdOf (yaw);
		V3 m = out.mouth;
		foodAt = V3 (m.x, 0, m.z) + f * (0.05f + 0.06f * shown.size);
		foodLeft = 1;
		break;
	}
	case A_PLAY:
		// (la balle tombe du ciel devant lui, et rebondit)
		ballOn = true; ballHeld = false; ballIdle = 0; catches = 0; misses = 0; playT = 0; jumpT = -1; ballBounces = 0;
		ball = pos + fwdOf (yaw) * 0.7f + V3 (0, 1.2f, 0); ballV = V3 ((frand () - 0.5f) * 1.2f, 0, (frand () - 0.5f) * 1.2f);
		break;
	case A_EVOLVE:
		from = p.prev; to = p.g; morph = 0; walking = false;
		sound (SND_EVOLVE);
		break;
	case A_HATCH:
		from = p.prev; to = p.g; walking = false; hatchCrack = 0.3f;
		break;
	case A_HAPPY: sound (SND_HAPPY); break;
	case A_REFUSE: sound (SND_REFUSE); break;
	case A_BATH: sound (SND_SPLASH); break;
	case A_GONE: ballOn = false; break;
	default: break;
	}
}

// le joueur relance la balle : vers le point du sol visé (from : l'œil, pour la frapper si on clique dessus)
void Actor::throwBall (const V3 &from_, const V3 &to_)
{
	if (!ballOn) return;
	V3 dst = to_;
	float r = sqrtf (dst.x * dst.x + dst.z * dst.z);
	if (r > 1.3f) dst = dst * (1.3f / r);
	if (ballHeld) { ballHeld = false; ball += V3 (0, 0.08f, 0); }
	V3 d (dst.x - ball.x, 0, dst.z - ball.z);
	float dl = len (d);
	ballV = (dl > 0.01f ? d * (clampf (dl * 1.3f, 0.9f, 2.4f) / dl) : V3 ()) + V3 (0, fmaxf (ballV.y, 4.0f), 0);
	ballIdle = 0;
	sound (SND_BOUNCE);
	(void) from_;
}
bool Actor::hitBall (const V3 &o, const V3 &d) const
{
	if (!ballOn) return false;
	V3 oc = ball - o;
	float t = dot (oc, d);
	return t > 0 && len (oc - d * t) < 0.16f;
}
// un clic sur la balle : elle file, en s'éloignant de l'œil
void Actor::smackBall (const V3 &d)
{
	if (!ballOn) return;
	if (ballHeld) { ballHeld = false; ball += V3 (0, 0.08f, 0); }
	V3 h = norm (V3 (d.x, 0, d.z) + V3 (0.0001f, 0, 0));
	ballV = h * 2.0f + V3 (0, 4.6f, 0);
	ballIdle = 0;
	emit (P_SPARK, ball, 4, 0xFFFFFF, 0.05f);
	sound (SND_CATCH);
}

bool Actor::clickPoop (const V3 &o, const V3 &d)
{
	for (size_t i = 0; i < poops.size (); i++)
	{
		V3 c = poops[i].p + V3 (0, 0.06f, 0);
		V3 oc = c - o;
		float t = dot (oc, d);
		if (t < 0) continue;
		float dd = len (oc - d * t);
		if (dd < 0.14f) return true;
	}
	return false;
}

bool Actor::hitPet (const V3 &o, const V3 &d) const
{
	if (act == A_GONE) return false;
	V3 cs[2] = { out.center, out.head };
	float rs[2] = { out.radius * 0.7f, shown.headR * shown.size * 1.3f };
	for (int i = 0; i < 2; i++)
	{
		V3 oc = cs[i] - o;
		float t = dot (oc, d);
		if (t < 0) continue;
		if (len (oc - d * t) < rs[i]) return true;
	}
	return false;
}

void Actor::update (float dt, Pet &p, const V3 &camEye, const V3 *cursor)
{
	t += dt; actT += dt;
	nsnd = 0;
	const Genome &G = p.g;
	bool egg = G.stage == ST_EGG || (act == A_HATCH && actT < 2.2f);
	bool sleeping = p.asleep && (act == A_IDLE);
	float size = shown.size;

	// ---- le génome dessiné : la métamorphose, le poids
	if (act == A_EVOLVE && morph >= 0)
	{
		float m = smooth (0, 1, morph);
		shown = blend (from, to, m);
		float s = fabsf (morph - 0.5f) * 2;
		const Genome &a = from, &b = to;
		ps = PartScale ();
		if (a.horn != b.horn) ps.horn = s;
		if (a.ear != b.ear) ps.ear = s;
		if (a.back != b.back || a.backCount != b.backCount) ps.back = s;
		if (a.wing != b.wing) ps.wing = s;
		if (a.tip != b.tip) ps.tip = s;
		if (a.legs != b.legs || a.foot != b.foot) ps.legs = s;
		if (a.arms != b.arms) ps.arms = s;
		if (a.eyes != b.eyes || a.eyeStalk != b.eyeStalk) ps.eyes = fmaxf (s, 0.25f);
		if (a.mouth != b.mouth) ps.mouth = s;
		if (a.brows != b.brows) ps.brows = s;
		if ((a.heads < 1 ? 1 : a.heads) != (b.heads < 1 ? 1 : b.heads)) ps.heads = s;
		if (a.plan != b.plan && (a.plan == PLAN_SHELL || b.plan == PLAN_SHELL)) ps.shell = s;
		skinT -= dt;
		if (skinT <= 0) { skinDirty = true; skinT = 0.12f; }
	}
	else if (act == A_HATCH && actT < 2.2f) { shown = from; ps = PartScale (); }
	else
	{
		if (shown.stage != G.stage || shown.seed != G.seed) skinDirty = true;
		shown = G; ps = PartScale ();
	}
	if (!egg)
	{
		float f = p.fat - 0.3f;
		shown.belly = clampf (shown.belly + f * 0.55f, 0, 0.7f);
		shown.bodyR *= 1 + f * 0.22f;
	}

	// ---- les humeurs, lissées
	float moodT = p.mood ();
	if (act == A_HAPPY || act == A_CARESS) moodT = 1;
	if (act == A_REFUSE) moodT = -0.6f;
	if (act == A_MEDICINE && actT > 0.7f && actT < 2) moodT = -0.7f;
	if (act == A_PLAY) moodT = fmaxf (moodT, 0.7f);
	if (act == A_EAT && actT > 2.4f) moodT = fmaxf (moodT, 0.8f);
	moodS = approach (moodS, moodT, 3, dt);
	lieS = approach (lieS, sleeping ? 1 : 0, 2.2f, dt);
	sickS = approach (sickS, p.sick ? 1 : 0, 1.5f, dt);

	// ---- le cerveau
	V3 camFlat = V3 (camEye.x - pos.x, 0, camEye.z - pos.z);
	float wantSpeed = 0;
	V3 look = camEye;
	bool lookFree = true;
	Pose P;
	P.t = t;
	float hop = 0, squash = 1, wave = 0, reach = 0, flap = 0, mouth = 0, anger = 0, shiver = sickS * 0.5f, extraPitch = 0, roll = 0;
	float lid = 0; bool happyEyes = false;
	if (p.st[N_ENERGY] < 25 && !sleeping) lid = 0.38f;
	if (moodS < -0.3f) lid = fmaxf (lid, 0.22f);
	if (p.st[N_FUN] < 18) anger = 0.45f;
	switch (act)
	{
	case A_IDLE:
		if (egg) break;
		if (sleeping)
		{
			walking = false;
			lookFree = false; extraPitch = -0.28f;
			if (fmodf (t, 1.7f) < dt) { emit (P_Z, out.top, 1, 0xFFFFFF, 0.05f); }
			break;
		}
		nextThink -= dt;
		if (nextThink <= 0)
		{
			float r = frand ();
			float sadK = moodS < -0.3f ? 1.8f : 1;
			if (r < 0.42f / sadK)
			{
				float a = frand () * 2 * PI, d = sqrtf (frand ()) * (0.15f + 0.3f * fmaxf (size, 0.5f));	// (il reste sur son socle)
				target = V3 (cosf (a) * d, 0, sinf (a) * d);
				walking = len (target - pos) > 0.25f;
			}
			else if (r < 0.75f) lookCamFor = 2.5f + frand () * 3;
			else { float a = frand () * 2 * PI; lookAt = pos + V3 (cosf (a) * 3, 0.3f + frand (), sinf (a) * 3); lookCamFor = 0; }
			nextThink = (2.5f + frand () * 4) * sadK * (G.trait == TR_CURIOUS ? 0.6f : 1);
			if (moodS > 0.75f && frand () < 0.25f) start (A_HAPPY, p);
		}
		if (walking) wantSpeed = (G.legs ? 0.42f : 0.28f) * fmaxf (size, 0.5f) * (moodS < -0.3f ? 0.6f : 1);
		break;
	case A_EAT: case A_MEDICINE:
	{
		lookFree = false; look = foodAt;
		bool med = act == A_MEDICINE;
		float bites[3] = { 0.6f, 1.35f, 2.1f };
		int nb = med ? 1 : 3;
		extraPitch = -0.1f;
		for (int i = 0; i < nb; i++)
		{
			float u = actT - bites[i];
			if (u > -0.25f && u < 0.3f)
			{
				float k = u < 0 ? 1 + u / 0.25f : 1 - u / 0.3f;
				mouth = fmaxf (mouth, k);
				extraPitch -= 0.18f * k;
				reach = fmaxf (reach, k);
			}
			if (u >= 0 && u < dt)
			{
				foodLeft = fmaxf (0, foodLeft - 1.0f / (float) nb);
				emit (P_CRUMB, foodAt + V3 (0, 0.08f, 0), 5, med ? 0xF0F0F0 : food == 1 ? 0xE04040 : food == 2 ? 0xFFC0D8 : 0xA06A3A, 0.1f);
				sound (SND_EAT);
			}
		}
		float end = med ? 2.6f : 3.3f;
		if (!med && actT > 2.5f) mouth = 0.15f * (0.5f + 0.5f * sinf (actT * 18));		// (il mâche)
		if (med && actT > 0.8f && actT < 2.2f) { anger = 0.7f; shiver = 0.6f; mouth = 0.1f; happyEyes = false; lid = 0.5f; if (actT - dt < 0.9f && actT >= 0.9f) sound (SND_YUCK); }
		if (actT > end)
		{
			if (med) { emit (P_STAR, out.top, 6, 0x9CF0A0, 0.2f); act = A_IDLE; }
			else if (food == FOOD_TREAT) { start (A_HAPPY, p); emit (P_HEART, out.top, 3, 0xFF5A8A, 0.15f); }
			else { act = A_IDLE; emit (P_HEART, out.top, 1, 0xFF5A8A, 0.1f); sound (SND_YUM); }
		}
		break;
	}
	case A_REFUSE:
		lookFree = false;
		look = camEye;
		roll = 0;
		P.headYaw = 0;
		anger = 0.3f;
		if (actT > 1.3f) act = A_IDLE;
		break;
	case A_HAPPY:
	{
		float u = actT / 0.75f;
		hop = fmaxf (0, sinf (u * PI)) * (0.18f + 0.06f * size) * (u < 2 ? 1 : 0);
		squash = 1 + 0.1f * sinf (u * PI * 2);
		flap = 1; wave = 1; happyEyes = hop > 0.05f;
		if (fmodf (actT, 0.5f) < dt) emit (P_NOTE, out.top, 1, 0xFFD85A, 0.15f);
		if (actT > 1.6f) act = A_IDLE;
		break;
	}
	case A_BATH:
	{
		lookFree = false;
		if (actT < 1.8f)
		{
			if (fmodf (actT, 0.1f) < dt) emit (P_BUBBLE, out.center + V3 (0, out.radius * 0.3f, 0), 2, 0xFFFFFF, out.radius * 1.4f);
			happyEyes = true; lid = 0.9f;
		}
		else
		{
			float u = actT - 1.8f;
			roll = sinf (u * 28) * 0.35f * (1 - u);
			squash = 1 + sinf (u * 28) * 0.05f * (1 - u);
			if (u < dt * 2) { emit (P_DROP, out.center, 14, 0x8CD0FF, out.radius); sound (SND_CLEAN); }
		}
		if (actT > 2.8f) { emit (P_SPARK, out.top, 8, 0xFFFFFF, out.radius); act = A_IDLE; }
		break;
	}
	case A_CARESS:
		lookFree = false;
		happyEyes = true;
		roll = sinf (t * 2.2f) * 0.16f;
		squash = 1 - 0.03f * sinf (t * 4);
		if (stroking) strokeT = 0; else strokeT += dt;
		if (fmodf (t, 0.38f) < dt) emit (P_HEART, out.top, 1, 0xFF5A8A, 0.25f);
		if (fmodf (t, 1.1f) < dt) sound (SND_PURR);
		if (strokeT > 0.7f) act = A_IDLE;
		break;
	case A_PLAY:
	{
		// la balle rebondit sans fin dans l'arène ; il court sous elle et saute pour l'attraper
		const float R = 0.09f, ARENA = 1.3f, GR = 7.0f;
		playT += dt; ballIdle += dt;
		look = ball; lookFree = false;
		if (!ballHeld)
		{
			ballV.y -= GR * dt;
			ball += ballV * dt;
			if (ball.y < R)
			{
				ball.y = R;
				if (ballV.y < 0)
				{
					ballV.y = -ballV.y * 0.86f;
					if (ballV.y < 2.8f) ballV.y = 2.8f + frand () * 0.6f;	// (elle rebondit toujours)
					ballBounces++;
					sound (SND_BOUNCE);
				}
			}
			// les bords de l'arène
			float r = sqrtf (ball.x * ball.x + ball.z * ball.z);
			if (r > ARENA)
			{
				V3 n = V3 (ball.x, 0, ball.z) * (1 / r);
				float vn = dot (ballV, n);
				if (vn > 0) ballV -= n * (vn * 1.95f);
				ball.x = n.x * ARENA; ball.z = n.z * ARENA;
			}
			// (jamais immobile au sol : une vitesse de côté entre deux bornes)
			V3 hv (ballV.x, 0, ballV.z);
			float hs = len (hv);
			if (hs < 0.35f) { float an = frand () * 2 * PI; hv = V3 (cosf (an), 0, sinf (an)) * 0.45f; }
			else if (hs > 1.5f) hv = hv * (1.5f / hs);
			ballV.x = hv.x; ballV.z = hv.z;
			ballSpin += len (hv) * dt / R;
			// où elle va retomber : il court là-bas
			float tl = (ballV.y + sqrtf (fmaxf (0, ballV.y * ballV.y + 2 * GR * (ball.y - R)))) / GR;
			V3 land (ball.x + ballV.x * tl, 0, ball.z + ballV.z * tl);
			float lr = sqrtf (land.x * land.x + land.z * land.z);
			if (lr > ARENA) land = land * (ARENA / lr);
			V3 mouthFlat (out.mouth.x, 0, out.mouth.z), bodyFlat (pos.x, 0, pos.z);
			float mouthAhead = len (mouthFlat - bodyFlat);
			float reach = 0.22f + 0.1f * size;
			float dBall = len (V3 (ball.x, 0, ball.z) - mouthFlat);
			if (jumpT < 0)
			{
				V3 to = land - norm (land - bodyFlat + V3 (0.0001f, 0, 0)) * mouthAhead;
				walking = len (to - bodyFlat) > 0.06f;
				target = to;
				wantSpeed = (G.legs ? 1.7f : 1.1f) * fmaxf (size, 0.6f);
				// le saut : la balle tout près, à portée de saut, qui descend (ou presque)
				float jumpReach = 0.32f + 0.38f * size;
				float above = ball.y - out.mouth.y;
				if (dBall < reach * 1.6f && above > -0.12f && above < jumpReach && ballV.y < 1.5f)
				{
					jumpT = 0; jumpH = clampf (above + 0.08f, 0.06f, jumpReach); jumpDone = false;
					// (réussir : plus souvent joueur, en forme ; moins souvent gros ou fatigué)
					float chance = 0.55f + (G.trait == TR_PLAYFUL ? 0.15f : 0) + 0.2f * (p.st[N_ENERGY] / 100) - (p.fat > 0.6f ? 0.15f : 0);
					jumpHit = frand () < clampf (chance, 0.35f, 0.9f);
				}
			}
			else
			{
				// en l'air
				jumpT += dt;
				const float JT = 0.55f;
				hop = fmaxf (0, sinf (PI * jumpT / JT)) * jumpH;
				squash = jumpT < 0.08f ? 0.88f : 1.06f;
				mouth = smooth (0, 0.22f, jumpT) * (1 - smooth (0.3f, 0.5f, jumpT));
				flap = 1; walking = false;
				if (!jumpDone && jumpT > JT * 0.42f)
				{
					jumpDone = true;
					if (jumpHit && dBall < reach * 2.0f)
					{
						ballHeld = true; holdT = 0; catches++;
						p.playCatch ();
						sound (SND_CATCH);
						emit (P_STAR, ball, 7, 0xFFE070, 0.12f);
					}
					else
					{
						// raté : la balle lui rebondit sur la tête
						V3 away = norm (V3 (ball.x - out.head.x, 0, ball.z - out.head.z) + V3 (0.0001f, 0, 0));
						ballV = away * 1.5f + V3 (0, 3.8f, 0);
						misses++;
						sound (SND_MISS);
					}
				}
				if (jumpT > JT) { jumpT = -1; hop = 0; }
			}
			// (personne ne joue avec lui depuis longtemps, ou il est épuisé : la partie s'arrête)
			if ((ballIdle > 40 && playT > 30) || p.st[N_ENERGY] < 8) { ballOn = false; act = A_IDLE; jumpT = -1; }
		}
		else
		{
			// il la tient, content, puis la relance en l'air
			holdT += dt;
			ball = out.mouth + fwdOf (yaw) * (0.05f + R) + V3 (0, -0.02f, 0);
			if (jumpT >= 0) { jumpT += dt; hop = fmaxf (0, sinf (PI * jumpT / 0.55f)) * jumpH; if (jumpT > 0.55f) jumpT = -1; }
			happyEyes = holdT > 0.15f && holdT < 0.9f; flap = 1;
			look = camEye;
			if (holdT > 1.1f)
			{
				ballHeld = false;
				float an = yaw + (frand () - 0.5f) * 2.2f;
				ballV = V3 (sinf (an), 0, cosf (an)) * (0.6f + frand () * 0.6f) + V3 (0, 4.2f, 0);
				ball += V3 (0, 0.08f, 0);
				sound (SND_BOUNCE);
			}
		}
		break;
	}
	case A_EVOLVE:
	{
		lookFree = false; look = camEye;
		if (actT < 1.2f) { hop = smooth (0, 1.2f, actT) * 0.35f; yaw += dt * 6 * smooth (0, 1.2f, actT); }
		else if (actT < 3.6f)
		{
			hop = 0.35f + 0.04f * sinf (actT * 6); yaw += dt * 6;
			float m0 = morph;
			morph = (actT - 1.2f) / 2.4f;
			if (m0 < 0.5f && morph >= 0.5f) { flash = 1; emit (P_STAR, out.center, 24, 0xFFF4C0, out.radius * 1.5f); }
			if (fmodf (actT, 0.15f) < dt) emit (P_SPARK, out.center, 2, 0xFFFFFF, out.radius * 2);
			happyEyes = true;
		}
		else
		{
			if (morph >= 0) { morph = -1; skinDirty = true; }
			float u = (actT - 3.6f) / 1.4f;
			hop = 0.35f * (1 - smooth (0, 0.5f, u)) + fmaxf (0, sinf ((u - 0.5f) * 2 * PI)) * 0.1f * (u > 0.5f ? 1 : 0);
			squash = u > 0.35f && u < 0.55f ? 0.85f : 1;
			float wantYaw = atan2f (camFlat.x, camFlat.z);
			yaw += wrapA (wantYaw - yaw) * fminf (1, dt * 5);
			wave = 1; flap = 1;
			if (actT > 5) { act = A_IDLE; emit (P_HEART, out.top, 3, 0xFF5A8A, 0.2f); sound (SND_HAPPY); }
		}
		break;
	}
	case A_HATCH:
		if (actT < 2.2f) { hatchCrack = 0.3f + actT / 2.2f * 0.7f; if (fmodf (actT, 0.55f) < dt) sound (SND_CRACK); }
		else
		{
			if (actT - dt < 2.2f) { flash = 0.8f; emit (P_SHELL, pos + V3 (0, 0.35f, 0), 16, 0xF4EEDF, 0.2f); sound (SND_HATCH); skinDirty = true; }
			float u = actT - 2.2f;
			hop = fmaxf (0, sinf (u / 0.6f * PI)) * 0.2f * (u < 0.6f ? 1 : 0);
			squash = u < 0.6f ? 1.1f : 1;
			happyEyes = u > 0.2f && u < 0.8f;
			look = camEye; lookFree = false;
			if (actT > 3.4f) { act = A_IDLE; sound (SND_HAPPY); emit (P_HEART, out.top, 2, 0xFF5A8A, 0.2f); }
		}
		break;
	default: break;
	}
	if (stroking && act != A_CARESS && act != A_EVOLVE && act != A_HATCH && !egg && act != A_EAT && act != A_MEDICINE) { start (A_CARESS, p); strokeT = 0; }

	// ---- marcher
	if (walking && !egg)
	{
		V3 d = target - pos; d.y = 0;
		float dist = len (d);
		if (dist < 0.06f) walking = false;
		else
		{
			float want = atan2f (d.x, d.z);
			float diff = wrapA (want - yaw);
			yaw += clampf (diff, -3.5f * dt, 3.5f * dt);
			float k = fabsf (diff) > 1.2f ? 0.25f : 1;			// (il tourne d'abord)
			speedNow = approach (speedNow, wantSpeed * k, 6, dt);
			pos += fwdOf (yaw) * (speedNow * dt);
		}
	}
	if (!walking) speedNow = approach (speedNow, 0, 8, dt);
	// à l'arrêt, il se tourne vers nous
	if (!walking && !egg && (act == A_IDLE || act == A_PLAY || act == A_CARESS) && !sleeping)
	{
		float want = atan2f (camFlat.x, camFlat.z);
		float diff = wrapA (want - yaw);
		if (fabsf (diff) > 0.6f || (act != A_IDLE)) yaw += clampf (diff, -1.6f * dt, 1.6f * dt);
	}
	float r = sqrtf (pos.x * pos.x + pos.z * pos.z);
	if (r > 1.5f) pos = pos * (1.5f / r);

	// ---- la tête regarde
	if (lookFree)
	{
		if (cursor) look = *cursor;
		else if (lookCamFor > 0) { lookCamFor -= dt; look = camEye; }
		else look = lookAt;
	}
	V3 hd = look - out.head;
	float wantYaw = clampf (wrapA (atan2f (hd.x, hd.z) - yaw), -0.8f, 0.8f);
	float wantPitch = clampf (atan2f (hd.y, sqrtf (hd.x * hd.x + hd.z * hd.z)), act == A_EAT || act == A_MEDICINE ? -0.3f : -0.45f, 0.45f);
	if (!lookFree && act == A_REFUSE) { wantYaw = sinf (actT * 14) * 0.5f; }
	if (sleeping || lieS > 0.5f) { wantYaw = 0.25f; wantPitch = 0; }
	lookYaw = approach (lookYaw, wantYaw, 6, dt);
	lookPitch = approach (lookPitch, wantPitch, 6, dt);

	// ---- les clignements
	nextBlink -= dt;
	if (nextBlink <= 0) { blinkT = 0.16f; nextBlink = 2 + frand () * 4; }
	float blink = 0;
	if (blinkT > 0) { blinkT -= dt; blink = 1; }
	blink = fmaxf (blink, lid);
	if (happyEyes) blink = 1;
	if (lieS > 0.5f) blink = 1;
	blinkS = blink >= 1 ? 1 : approach (blinkS, blink, 25, dt);

	// ---- la pose
	wagS = approach (wagS, act == A_CARESS || act == A_PLAY ? 1.0f : clampf (moodS, 0, 1) * 0.6f, 3, dt);
	flapS = approach (flapS, flap, 4, dt);
	P.breathe = sleeping ? 2.2f : 1;
	P.headYaw = lookYaw;
	P.headPitch = lookPitch + extraPitch;
	P.headRoll = roll;
	P.gaze = look;
	P.blink = blinkS;
	P.lidHappy = happyEyes ? 1 : 0;
	P.mouthOpen = fmaxf (mouth, act == A_HAPPY ? 0.5f : 0);
	if (act == A_IDLE && moodS > 0.6f && !sleeping) P.mouthOpen = fmaxf (P.mouthOpen, 0.0f);
	P.mood = moodS;
	P.anger = anger;
	P.wag = wagS;
	float stride = fmaxf (shown.legLen * shown.size * 0.55f, 0.06f);
	static float phase = 0;
	phase += dt * speedNow / stride * 2.2f;
	P.walkPhase = phase;
	P.walkAmt = clampf (speedNow / fmaxf (0.3f * fmaxf (size, 0.5f), 0.05f), 0, 1);
	if (!shown.legs && shown.plan != PLAN_SERPENT && speedNow > 0.02f) { hop += fabsf (sinf (phase)) * 0.06f * size; squash *= 1 + 0.07f * sinf (phase * 2); }
	P.hop = hop;
	P.squash = squash * (sleeping ? 1 - 0.02f * sinf (t * 1.5f) : 1);
	P.wave = act == A_HAPPY || (act == A_EVOLVE && actT > 3.6f) ? wave : 0;
	P.reach = reach;
	P.flap = flapS;
	P.shiver = shiver;
	P.lie = lieS;
	P.sick = sickS;
	P.pupil = (G.stage <= ST_CHILD ? 1.2f : 1.0f) * (anger > 0.4f ? 0.75f : 1) * (moodS > 0.6f ? 1.12f : 1);
	P.dirt = clampf ((60 - p.st[N_CLEAN]) / 60, 0, 1);
	P.pos = pos; P.yaw = yaw;
	P.ps = ps;
	if (sleeping) P.t = t * 0.6f;
	pose = P;

	// ---- les crottes : celles de l'animal, posées derrière lui
	while ((int) poops.size () < p.poops)
	{
		V3 b = pos - fwdOf (yaw) * (0.3f + 0.25f * size) + V3 ((frand () - 0.5f) * 0.2f, 0, (frand () - 0.5f) * 0.2f);
		float rr = sqrtf (b.x * b.x + b.z * b.z); if (rr > 1.2f) b = b * (1.2f / rr);
		poops.push_back ({ b, 0, 0 });
		sound (SND_POP);
	}
	while ((int) poops.size () > p.poops)
	{
		emit (P_SPARK, poops.back ().p + V3 (0, 0.06f, 0), 8, 0xFFFFFF, 0.1f);
		poops.pop_back ();
	}
	for (Poop &q : poops)
	{
		q.age += dt; q.scale = smooth (0, 0.4f, q.age);
		if (fmodf (q.age + q.p.x * 3, 2.2f) < dt) emit (P_STINK, q.p + V3 (0, 0.12f, 0), 1, 0x9CB060, 0.05f);
	}
	if (p.st[N_CLEAN] < 30 && !egg && fmodf (t, 1.4f) < dt) emit (P_STINK, out.top, 1, 0x9CB060, 0.2f);

	// ---- les particules
	for (size_t i = 0; i < parts.size (); )
	{
		Particle &q = parts[i];
		q.life -= dt;
		if (q.life <= 0) { parts[i] = parts.back (); parts.pop_back (); continue; }
		q.p += q.v * dt;
		if (q.kind == P_STAR || q.kind == P_SPARK || q.kind == P_DROP || q.kind == P_CRUMB || q.kind == P_SHELL) { q.v.y -= 3.2f * dt; if (q.p.y < 0.01f) { q.p.y = 0.01f; q.v = q.v * 0.3f; } }
		if (q.kind == P_HEART || q.kind == P_NOTE || q.kind == P_Z) q.v.x = sinf (q.life * 4 + q.spin) * 0.15f;
		i++;
	}
	flash = fmaxf (0, flash - dt * 1.6f);
	if (act == A_IDLE && p.gone) act = A_GONE;
}

// ==== les objets ======================================================================================================
static void buildFood (Builder &B, int kind, const V3 &at, float left, float t)
{
	Frame f; f.o = at;
	if (kind == FOOD_MEAL)
	{
		// la gamelle et ses croquettes
		const int N = 20;
		V3 pts[N + 1]; float rad[N + 1]; Col cols[N + 1];
		for (int i = 0; i <= N; i++)
		{
			float a = (float) i / N * 2 * PI;
			pts[i] = at + V3 (cosf (a) * 0.13f, 0.045f, sinf (a) * 0.13f);
			rad[i] = 0.035f; cols[i] = Col (0.25f, 0.52f, 0.85f);
		}
		B.gloss = 0.7f; B.tube (pts, rad, cols, N + 1, 8, 1, 0, V3 (0, 1, 0), 0);
		Frame b0 = f; b0.o = at + V3 (0, 0.025f, 0);
		B.ellipsoid (b0, V3 (0.14f, 0.03f, 0.14f), Col (0.3f, 0.58f, 0.9f), Col (0.2f, 0.42f, 0.7f), 16, 6);
		int n = (int) (left * 14 + 0.5f);
		B.gloss = 0.3f;
		for (int i = 0; i < n; i++)
		{
			float a = (float) i * 2.4f, d = 0.03f + 0.07f * hashf (i, 1, 2);
			B.sphere (at + V3 (cosf (a) * d, 0.065f + 0.015f * hashf (i, 2, 2), sinf (a) * d), 0.022f, hsv (0.07f, 0.6f, 0.45f + 0.15f * hashf (i, 3, 2)), 7, 4);
		}
	}
	else if (kind == FOOD_FRUIT)
	{
		float s = 0.4f + 0.6f * left;
		B.gloss = 0.85f;
		Frame a = f; a.o = at + V3 (0, 0.075f * s, 0);
		B.ellipsoid (a, V3 (0.08f * s, 0.072f * s, 0.08f * s), Col (0.95f, 0.22f, 0.18f), Col (0.65f, 0.08f, 0.1f), 14, 9);
		V3 st[2] = { a.o + V3 (0, 0.06f * s, 0), a.o + V3 (0.01f, 0.1f * s, 0) };
		float sr[2] = { 0.006f, 0.005f }; Col sc[2] = { Col (0.4f, 0.28f, 0.15f), Col (0.4f, 0.28f, 0.15f) };
		B.gloss = 0.1f; B.tube (st, sr, sc, 2, 5, 1, 0, V3 (1, 0, 0), 2);
		Frame l; l.o = a.o + V3 (0.03f, 0.1f * s, 0); l.z = norm (V3 (1, 0.4f, 0)); l.y = norm (V3 (-0.4f, 1, 0)); l.x = cross (l.y, l.z);
		B.gloss = 0.4f; B.ellipsoid (l, V3 (0.018f, 0.004f, 0.035f), Col (0.35f, 0.7f, 0.25f), Col (0.25f, 0.55f, 0.2f), 8, 4);
	}
	else if (kind == FOOD_TREAT)
	{
		float s = 0.35f + 0.65f * left;
		// le petit gâteau : la caissette, la crème, la cerise
		const int N = 16;
		V3 pts[2] = { at + V3 (0, 0.0f, 0), at + V3 (0, 0.07f * s, 0) };
		float rad[2] = { 0.055f * s, 0.07f * s };
		Col cols[2] = { Col (0.95f, 0.75f, 0.25f), Col (1, 0.85f, 0.4f) };
		B.gloss = 0.4f; B.tube (pts, rad, cols, 2, N, 1, 0, V3 (1, 0, 0), 1);
		for (int i = 0; i < 3; i++)
		{
			Frame c; c.o = at + V3 (0, (0.08f + 0.035f * (float) i) * s, 0);
			float rr = (0.07f - 0.018f * (float) i) * s;
			B.gloss = 0.6f; B.ellipsoid (c, V3 (rr, rr * 0.55f, rr), Col (1, 0.78f, 0.88f), Col (0.95f, 0.6f, 0.75f), 14, 7);
		}
		B.gloss = 0.95f; B.sphere (at + V3 (0, 0.19f * s, 0), 0.022f * s, Col (0.9f, 0.08f, 0.15f), 10, 7);
	}
	(void) t;
}

void Actor::build (CreatureMeshes &M, Mesh &props, Mesh &propsSheets, Mesh &propsAlpha)
{
	(void) propsSheets;
	bool egg = shown.stage == ST_EGG || (act == A_HATCH && actT < 2.2f);
	if (act != A_GONE)
	{
		if (egg)
		{
			float wob = act == A_HATCH ? 0.5f + actT * 0.45f : (fmodf (t, 5.0f) < 1.2f ? 0.35f : 0.04f);
			buildEgg (shown, wob, act == A_HATCH ? hatchCrack : hatchCrack, t, pos, M, out);
		}
		else buildCreature (shown, pose, M, out);
	}
	Builder B (&props);
	if ((act == A_EAT || act == A_MEDICINE) && foodLeft > 0.01f)
	{
		if (act == A_MEDICINE)
		{
			V3 a = foodAt + V3 (-0.04f, 0.03f, 0), b = foodAt + V3 (0.04f, 0.03f, 0);
			V3 p1[2] = { a, foodAt + V3 (0, 0.03f, 0) }, p2[2] = { foodAt + V3 (0, 0.03f, 0), b };
			float rr[2] = { 0.026f, 0.026f };
			Col c1[2] = { Col (0.95f, 0.3f, 0.3f), Col (0.95f, 0.3f, 0.3f) }, c2[2] = { Col (0.97f, 0.97f, 0.95f), Col (0.97f, 0.97f, 0.95f) };
			B.gloss = 0.9f; B.tube (p1, rr, c1, 2, 12, 1, 0, V3 (0, 1, 0), 1); B.tube (p2, rr, c2, 2, 12, 1, 0, V3 (0, 1, 0), 2);
		}
		else buildFood (B, food, foodAt, foodLeft, t);
	}
	if (ballOn)
	{
		// la balle : des bandes de couleur qui tournent
		Frame f; f.o = ball;
		V3 dir = norm (V3 (ballV.x, 0, ballV.z) + V3 (0.001f, 0, 0));
		V3 ax = norm (cross (V3 (0, 1, 0), dir));
		f.x = rotAxis (V3 (1, 0, 0), ax, ballSpin); f.y = rotAxis (V3 (0, 1, 0), ax, ballSpin); f.z = rotAxis (V3 (0, 0, 1), ax, ballSpin);
		const float R = 0.09f;
		int base = (int) props.v.size ();
		const int SEG = 16, RN = 10;
		B.gloss = 0.7f;
		for (int i = 0; i <= RN; i++)
		{
			float ph = -PI / 2 + PI * (float) i / RN;
			for (int j = 0; j <= SEG; j++)
			{
				float th = 2 * PI * (float) j / SEG;
				V3 l (cosf (ph) * cosf (th), sinf (ph), -cosf (ph) * sinf (th));
				int band = (int) floorf ((th + 0.3f) / (2 * PI) * 6) % 3;
				Col c = fabsf (l.y) > 0.85f ? Col (1, 1, 1) : band == 0 ? Col (0.95f, 0.25f, 0.25f) : band == 1 ? Col (1, 0.85f, 0.2f) : Col (0.2f, 0.55f, 0.95f);
				B.add (f.at (l * R), f.dir (l), c);
			}
		}
		B.grid (base, RN, SEG);
	}
	for (const Poop &q : poops)
	{
		float s = q.scale;
		if (s < 0.01f) continue;
		Col c (0.45f, 0.3f, 0.18f);
		B.gloss = 0.55f;
		for (int i = 0; i < 3; i++)
		{
			Frame f; f.o = q.p + V3 (0.005f * (float) i, (0.025f + 0.035f * (float) i) * s, 0);
			float rr = (0.075f - 0.02f * (float) i) * s;
			B.ellipsoid (f, V3 (rr, rr * 0.55f, rr), c * (1.0f + 0.08f * (float) i), c * 0.75f, 12, 6);
		}
		Frame tip; tip.o = q.p + V3 (0.01f, 0.13f * s, 0);
		B.ellipsoid (tip, V3 (0.018f * s, 0.03f * s, 0.018f * s), c * 1.15f, c, 8, 5);
	}
	// les bulles du bain
	Builder A (&propsAlpha);
	A.gloss = 1; A.emit = 0.25f; A.alpha = 0.35f;
	for (const Particle &q : parts)
		if (q.kind == P_BUBBLE)
		{
			float k = q.life / q.max;
			A.sphere (q.p, 0.035f * q.size * (0.6f + 0.4f * (1 - k)), Col (0.85f, 0.95f, 1), 10, 6);
		}
}

}	// namespace mo
