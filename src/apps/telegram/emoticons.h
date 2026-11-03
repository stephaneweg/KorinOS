//
// emoticons.h -- the Telegram app's emoticons, in the way of the old messengers: the emoji Telegram's
// text carries (and the typed ":)", ";)", "(Y)", "<3"... of MSN's days) drawn as little pictures, by
// UIKit's anti-aliased shapes at any size -- no picture file, no other licence, and DejaVu has none of the emoji. An
// emoji with no picture of its own here is drawn as a small round badge; the joiners and variation selectors are skipped.
// The same pictures as Onyx, whatever the theme: a smiley stays yellow.
//
//   int id, len;
//   if ((id = emo_at (text, n, &len, true)) >= 0) { emo_draw (id, x, y, 18); text += len; }
//
// MIT licence.
//
#ifndef TG_EMOTICONS_H
#define TG_EMOTICONS_H

#include "look.h"

enum
{
	EMO_SMILE, EMO_GRIN, EMO_WINK, EMO_SAD, EMO_CRY, EMO_TONGUE, EMO_SURPRISED, EMO_ANGRY, EMO_COOL, EMO_LAUGH,
	EMO_LOVE, EMO_KISS, EMO_CONFUSED, EMO_NEUTRAL, EMO_BLUSH, EMO_THINK, EMO_HEART, EMO_BROKEN, EMO_THUMBUP,
	EMO_THUMBDOWN, EMO_STAR, EMO_SUN, EMO_COFFEE, EMO_ROSE, EMO_GIFT, EMO_PARTY, EMO_FIRE, EMO_CLAP, EMO_OK,
	EMO_PRAY, EMO_SLEEP, EMO_SICK,
	EMO_COUNT,			// (the picker's)
	EMO_OTHER = EMO_COUNT		// an emoji without a picture here: a badge
};

struct EmoInfo { unsigned cp; const char *text; const char *name; };	// the emoji sent, a typed form, its name

static const EmoInfo emo_info[EMO_COUNT] = {
	{ 0x1F642, ":)", TRN ("Smile") }, { 0x1F600, ":D", TRN ("Big smile") }, { 0x1F609, ";)", TRN ("Wink") }, { 0x1F641, ":(", TRN ("Sad") },
	{ 0x1F622, ":'(", TRN ("Crying") }, { 0x1F61B, ":P", TRN ("Tongue out") }, { 0x1F62E, ":O", TRN ("Surprised") }, { 0x1F620, ":@", TRN ("Angry") },
	{ 0x1F60E, "(H)", TRN ("Cool") }, { 0x1F602, 0, TRN ("Tears of joy") }, { 0x1F60D, 0, TRN ("In love") }, { 0x1F618, ":*", TRN ("Kiss") },
	{ 0x1F615, ":S", TRN ("Confused") }, { 0x1F610, ":|", TRN ("Neutral") }, { 0x1F633, ":$", TRN ("Embarrassed") }, { 0x1F914, 0, TRN ("Thinking") },
	{ 0x2764, "<3", TRN ("Heart") }, { 0x1F494, "</3", TRN ("Broken heart") }, { 0x1F44D, "(Y)", TRN ("Thumbs up") }, { 0x1F44E, "(N)", TRN ("Thumbs down") },
	{ 0x2B50, "(*)", TRN ("Star") }, { 0x2600, "(#)", TRN ("Sunshine") }, { 0x2615, "(C)", TRN ("Coffee") }, { 0x1F339, "(F)", TRN ("Rose") },
	{ 0x1F381, "(G)", TRN ("Gift") }, { 0x1F389, 0, TRN ("Party") }, { 0x1F525, 0, TRN ("Fire") }, { 0x1F44F, 0, TRN ("Applause") },
	{ 0x1F44C, 0, TRN ("OK") }, { 0x1F64F, 0, TRN ("Thank you") }, { 0x1F634, 0, TRN ("Sleepy") }, { 0x1F922, 0, TRN ("Sick") } };

// More code points drawn as one of the pictures.
static int emo_of_cp (unsigned cp)
{
	for (int i = 0; i < EMO_COUNT; i++) if (emo_info[i].cp == cp) return i;
	switch (cp)
	{
	case 0x263A: case 0x1F60A: case 0x1F607: case 0x1F917: case 0x1F643: return EMO_SMILE;
	case 0x1F603: case 0x1F604: case 0x1F601: case 0x1F606: case 0x1F605: case 0x1F929: case 0x1F973: return EMO_GRIN;
	case 0x1F61C: return EMO_WINK;
	case 0x2639: case 0x1F61E: case 0x1F614: case 0x1F61F: case 0x1F625: case 0x1F613: case 0x1F629: case 0x1F62B: return EMO_SAD;
	case 0x1F62D: case 0x1F97A: return EMO_CRY;
	case 0x1F61D: case 0x1F92A: return EMO_TONGUE;
	case 0x1F62F: case 0x1F632: case 0x1F631: case 0x1F628: case 0x1F630: case 0x1F92F: return EMO_SURPRISED;
	case 0x1F621: case 0x1F624: case 0x1F92C: case 0x1F47F: return EMO_ANGRY;
	case 0x1F923: return EMO_LAUGH;
	case 0x1F970: case 0x1F63B: return EMO_LOVE;
	case 0x1F617: case 0x1F619: case 0x1F61A: case 0x1F48B: return EMO_KISS;
	case 0x1F612: case 0x1F644: case 0x1F928: return EMO_CONFUSED;
	case 0x1F611: case 0x1F636: case 0x1F60F: return EMO_NEUTRAL;
	case 0x1F92D: case 0x1F648: return EMO_BLUSH;
	case 0x2665: case 0x1F496: case 0x1F497: case 0x1F493: case 0x1F495: case 0x1F49E: case 0x1F498: case 0x1F9E1:
	case 0x1F49B: case 0x1F49A: case 0x1F499: case 0x1F49C: case 0x1F5A4: case 0x1F90D: case 0x1F90E: case 0x2763: return EMO_HEART;
	case 0x1F31F: case 0x2728: case 0x1F320: case 0x2B51: return EMO_STAR;
	case 0x1F31E: case 0x1F324: case 0x26C5: return EMO_SUN;
	case 0x1F375: case 0x1F37A: case 0x1F377: return EMO_COFFEE;
	case 0x1F33A: case 0x1F337: case 0x1F338: case 0x1F490: case 0x1F33C: case 0x1F33B: return EMO_ROSE;
	case 0x1F38A: case 0x1F382: case 0x1F388: case 0x1F386: case 0x1F387: return EMO_PARTY;
	case 0x1F4A5: return EMO_FIRE;
	case 0x1F64C: case 0x1F44B: return EMO_CLAP;
	case 0x2705: case 0x2714: case 0x1F197: return EMO_OK;
	case 0x1F62A: case 0x1F971: case 0x1F4A4: return EMO_SLEEP;
	case 0x1F92E: case 0x1F637: case 0x1F912: case 0x1F915: return EMO_SICK;
	}
	return -1;
}

// An emoji's range: what the font cannot draw (a badge when it has no picture here).
static bool emo_range (unsigned cp)
{
	return (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) ||
	       (cp >= 0x1F000 && cp <= 0x1F2FF) || cp == 0x2764 || cp == 0x203C || cp == 0x2049;
}
// What is skipped after (or between the parts of) an emoji: variation selectors, the joiner, skin tones.
static bool emo_mod (unsigned cp) { return cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || cp == 0x20E3 || (cp >= 0x1F3FB && cp <= 0x1F3FF); }

static unsigned emo_u8 (const char *s, int n, int *len)
{
	const unsigned char *p = (const unsigned char *) s;
	unsigned c = p[0];
	int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
	if (k > n) k = 1;
	unsigned cp = k == 1 ? c : k == 2 ? c & 0x1F : k == 3 ? c & 0x0F : c & 0x07;
	for (int i = 1; i < k; i++) { if ((p[i] & 0xC0) != 0x80) { *len = 1; return c; } cp = (cp << 6) | (p[i] & 0x3F); }
	*len = k;
	return cp;
}

// An emoticon at s (n bytes left)? -> its id (EMO_OTHER: an emoji badge) and *len the bytes it takes
// (with its modifiers and a whole ZWJ sequence); -1 none. typed: ":)" and the rest count too.
static int emo_at (const char *s, int n, int *len, bool typed)
{
	if (n <= 0) return -1;
	int k;
	unsigned cp = emo_u8 (s, n, &k);
	if (cp >= 0x80 && (emo_range (cp) || emo_of_cp (cp) >= 0))
	{
		int id = emo_of_cp (cp);
		if (id < 0) id = EMO_OTHER;
		int o = k;
		for (;;)					// modifiers, and what a joiner joins
		{
			if (o >= n) break;
			int m;
			unsigned c2 = emo_u8 (s + o, n - o, &m);
			if (emo_mod (c2)) { o += m; if (c2 == 0x200D && o < n) { emo_u8 (s + o, n - o, &m); o += m; } continue; }
			if (cp >= 0x1F1E6 && cp <= 0x1F1FF && c2 >= 0x1F1E6 && c2 <= 0x1F1FF) { o += m; cp = 0; continue; }	// (a flag: two letters)
			break;
		}
		*len = o;
		return id;
	}
	if (!typed) return -1;
	for (int i = 0; i < EMO_COUNT; i++)
	{
		const char *t = emo_info[i].text;
		if (!t) continue;
		int tl = 0;
		while (t[tl]) tl++;
		if (tl > n) continue;
		bool eq = true;
		for (int j = 0; j < tl && eq; j++)
		{
			char a = s[j], b = t[j];
			if (a >= 'a' && a <= 'z' && b >= 'A' && b <= 'Z') a = (char) (a - 32);
			eq = a == b;
		}
		if (!eq) continue;
		// ":O" and the like only as words of their own (not inside a URL "http://...")
		if (tl < n && s[tl] != ' ' && s[tl] != '\n' && s[tl] != '.' && s[tl] != ',' && s[tl] != '!' && s[tl] != '?' && s[tl] != ')') continue;
		*len = tl;
		return i;
	}
	return -1;
}

// ---- drawing ----------------------------------------------------------------------------------------

static inline float dcos (int a) { return fcos (((a % 360 + 360) % 360) * 64 / 360); }
static inline float dsin (int a) { return fsin (((a % 360 + 360) % 360) * 64 / 360); }

// A face: the disc, its shading and outline.
static void emo_face (float cx, float cy, float r, unsigned face, unsigned rim)
{
	vp_disc (cx, cy, r + 0.6f, rim);
	vp_disc (cx, cy, r, face);
	vp_disc (cx, cy + r * 0.18f, r * 0.86f, mix (face, mix (face, 0xE07800, 60), 110));
	vp_ellipse (cx - r * 0.25f, cy - r * 0.45f, r * 0.5f, r * 0.3f, mix (face, 0xFFFFFF, 140));
}
static void emo_eyes (float cx, float cy, float r, unsigned ink, bool winkRight = false)
{
	float ex = r * 0.36f, ey = cy - r * 0.18f;
	vp_ellipse (cx - ex, ey, r * 0.11f + 0.4f, r * 0.2f + 0.4f, ink);
	if (!winkRight) vp_ellipse (cx + ex, ey, r * 0.11f + 0.4f, r * 0.2f + 0.4f, ink);
	else vp_arc (cx + ex, ey + r * 0.1f, r * 0.16f, 20, 160, r * 0.09f + 0.6f, ink);
}
static void emo_mouth (float cx, float cy, float r, int a0, int a1, float rr, unsigned ink, float dy = 0.12f)
{
	vp_arc (cx, cy + r * dy, r * rr, a0, a1, r * 0.09f + 0.6f, ink);
}
static void emo_heart (float cx, float cy, float s, unsigned c, unsigned rim, bool broken = false)
{
	float r = s * 0.26f;
	for (int pass = 0; pass < 2; pass++)
	{
		float g = pass ? 0 : 0.8f;
		unsigned k = pass ? c : rim;
		vp_disc (cx - r * 0.95f, cy - r * 0.55f, r + g, k);
		vp_disc (cx + r * 0.95f, cy - r * 0.55f, r + g, k);
		float tri[6] = { cx - r * 1.9f - g, cy - r * 0.3f, cx + r * 1.9f + g, cy - r * 0.3f, cx, cy + r * 2.0f + g };
		vp_poly (tri, 3, k);
	}
	vp_ellipse (cx - r * 1.0f, cy - r * 0.9f, r * 0.45f, r * 0.3f, mix (c, 0xFFFFFF, 150));
	if (broken)
	{
		float zig[8] = { cx + 0.4f, cy - r * 1.4f, cx - r * 0.5f, cy - r * 0.2f, cx + r * 0.4f, cy + r * 0.5f, cx - r * 0.1f, cy + r * 2.0f };
		for (int i = 0; i < 3; i++) vp_line (zig[i * 2], zig[i * 2 + 1], zig[i * 2 + 2], zig[i * 2 + 3], s * 0.09f + 0.6f, 0xFFFFFF);
	}
}
static void emo_thumb (float x, float y, float s, bool up)
{
	unsigned skin = 0xF5C26B, rim = 0xB57A1E;
	float sy = up ? 1.0f : -1.0f, my = y + s * 0.5f;
	for (int pass = 0; pass < 2; pass++)
	{
		float g = pass ? 0 : 0.7f;
		unsigned k = pass ? skin : rim;
		vp_rrect (x + s * 0.3f - g, my - s * 0.12f * sy - (up ? 0 : s * 0.3f) - g, s * 0.55f + 2 * g, s * 0.42f + 2 * g, s * 0.1f, k);
		vp_rrect (x + s * 0.4f - g, my - sy * s * 0.42f - (up ? 0 : s * 0.22f) - g, s * 0.17f + 2 * g, s * 0.36f + 2 * g, s * 0.08f, k);
	}
	vp_rrect (x + s * 0.1f, my - s * 0.12f * sy - (up ? 0 : s * 0.3f), s * 0.18f, s * 0.42f, s * 0.04f, 0x3C6EB4);
}

// Emoticon id drawn in the s x s box at (x, y).
static void emo_draw (int id, int x, int y, int s)
{
	float r = s * 0.46f, cx = x + s * 0.5f, cy = y + s * 0.5f;
	unsigned yel = 0xFFD43B, rim = 0xC27C0E, ink = 0x4A3008, red = 0xE8364F;
	switch (id)
	{
	case EMO_HEART: emo_heart (cx, cy, (float) s, red, 0x9E1328); return;
	case EMO_BROKEN: emo_heart (cx, cy, (float) s, red, 0x9E1328, true); return;
	case EMO_THUMBUP: emo_thumb ((float) x, (float) y, (float) s, true); return;
	case EMO_THUMBDOWN: emo_thumb ((float) x, (float) y, (float) s, false); return;
	case EMO_STAR:
	{
		// (a five-pointed star, not pair-impair safe as one outline at this size: its ten points)
		float pts[20];
		for (int i = 0; i < 10; i++)
		{
			float rr = i & 1 ? r * 0.45f : r * 1.02f;
			pts[i * 2] = cx + rr * dcos (90 + i * 36);
			pts[i * 2 + 1] = cy - rr * dsin (90 + i * 36) + s * 0.06f;
		}
		for (int i = 0; i < 10; i++) vp_line (pts[i * 2], pts[i * 2 + 1], pts[(i * 2 + 2) % 20], pts[(i * 2 + 3) % 20], 1.3f, 0xC48A00);
		vp_poly (pts, 10, 0xFFCC1A);
		return;
	}
	case EMO_SUN:
		for (int i = 0; i < 8; i++)
		{
			int a = i * 45;
			vp_line (cx + r * 0.6f * dcos (a), cy - r * 0.6f * dsin (a), cx + r * dcos (a), cy - r * dsin (a), s * 0.08f + 0.5f, 0xF59E0B);
		}
		emo_face (cx, cy, r * 0.58f, 0xFFD43B, 0xD98A00);
		return;
	case EMO_COFFEE:
		vp_rrect (x + s * 0.18f, y + s * 0.38f, s * 0.5f, s * 0.46f, s * 0.08f, 0x7A4A24);
		vp_arc (x + s * 0.7f, y + s * 0.58f, s * 0.12f, -90, 90, s * 0.07f + 0.5f, 0x7A4A24);
		vp_rrect (x + s * 0.22f, y + s * 0.38f, s * 0.42f, s * 0.08f, 1, 0xC89A6A);
		vp_arc (x + s * 0.36f, y + s * 0.26f, s * 0.08f, 90, 270, 1.1f, 0x9A9A9A);
		vp_arc (x + s * 0.5f, y + s * 0.2f, s * 0.08f, 90, 270, 1.1f, 0x9A9A9A);
		return;
	case EMO_ROSE:
		vp_line (cx, cy, cx - s * 0.05f, y + s * 0.95f, s * 0.07f + 0.5f, 0x2E8B3A);
		vp_ellipse (cx + s * 0.14f, cy + s * 0.2f, s * 0.14f, s * 0.07f, 0x3BA34A);
		vp_disc (cx, y + s * 0.34f, s * 0.26f, 0xA3122A);
		vp_disc (cx, y + s * 0.32f, s * 0.2f, 0xE0304A);
		vp_arc (cx, y + s * 0.33f, s * 0.1f, 0, 300, 1.1f, 0x8E0B20);
		return;
	case EMO_GIFT:
		vp_rrect (x + s * 0.12f, y + s * 0.36f, s * 0.76f, s * 0.52f, 1.5f, 0x2F7BD8);
		vp_rrect (x + s * 0.08f, y + s * 0.28f, s * 0.84f, s * 0.16f, 0, 0x4A93F0);
		vp_rrect (cx - s * 0.07f, y + s * 0.28f, s * 0.14f, s * 0.6f, 0, 0xF4C430);
		vp_ellipse (cx - s * 0.15f, y + s * 0.2f, s * 0.14f, s * 0.09f, 0xF4C430);
		vp_ellipse (cx + s * 0.15f, y + s * 0.2f, s * 0.14f, s * 0.09f, 0xF4C430);
		return;
	case EMO_PARTY:
	{
		float cone[6] = { x + s * 0.12f, y + s * 0.9f, x + s * 0.42f, y + s * 0.3f, x + s * 0.7f, y + s * 0.62f };
		vp_poly (cone, 3, 0xF59E0B);
		vp_line (x + s * 0.27f, y + s * 0.6f, x + s * 0.47f, y + s * 0.72f, 1.4f, 0xE8364F);
		static const unsigned conf[5] = { 0xE8364F, 0x2F7BD8, 0x3BA34A, 0xA855F7, 0xF4C430 };
		for (int i = 0; i < 5; i++) vp_disc (x + s * (0.55f + 0.08f * i), y + s * (0.12f + 0.1f * (i % 3)), s * 0.05f + 0.4f, conf[i]);
		return;
	}
	case EMO_FIRE:
	{
		float f1[10] = { cx, y + s * 0.05f, cx + r * 0.85f, cy + r * 0.2f, cx + r * 0.6f, y + s * 0.95f, cx - r * 0.6f, y + s * 0.95f, cx - r * 0.85f, cy + r * 0.1f };
		vp_poly (f1, 5, 0xF0582A);
		float f2[8] = { cx, cy - r * 0.2f, cx + r * 0.45f, cy + r * 0.5f, cx, y + s * 0.95f, cx - r * 0.45f, cy + r * 0.5f };
		vp_poly (f2, 4, 0xFFC93C);
		return;
	}
	case EMO_CLAP: case EMO_PRAY: case EMO_OK:
	{
		unsigned skin = 0xF5C26B, srim = 0xB57A1E;
		if (id == EMO_OK)
		{
			vp_arc (cx - r * 0.25f, cy - r * 0.05f, r * 0.31f, 0, 360, r * 0.2f, srim);
			vp_arc (cx - r * 0.25f, cy - r * 0.05f, r * 0.31f, 0, 360, r * 0.12f, skin);
			for (int i = 0; i < 3; i++) vp_rrect (cx + r * (0.05f + 0.25f * i), cy - r * 0.95f + i * r * 0.1f, r * 0.22f, r * 0.9f, r * 0.1f, skin);
			vp_rrect (cx - r * 0.2f, cy + r * 0.2f, r * 0.9f, r * 0.7f, r * 0.25f, skin);
			return;
		}
		vp_ellipse (cx - r * 0.28f, cy + r * 0.05f, r * 0.36f, r * 0.8f, srim);
		vp_ellipse (cx + r * 0.28f, cy + r * 0.05f, r * 0.36f, r * 0.8f, srim);
		vp_ellipse (cx - r * 0.28f, cy + r * 0.05f, r * 0.3f, r * 0.74f, skin);
		vp_ellipse (cx + r * 0.28f, cy + r * 0.05f, r * 0.3f, r * 0.74f, skin);
		if (id == EMO_CLAP)
		{
			vp_line (x + s * 0.1f, y + s * 0.18f, x + s * 0.2f, y + s * 0.28f, 1.2f, 0xF59E0B);
			vp_line (x + s * 0.9f, y + s * 0.18f, x + s * 0.8f, y + s * 0.28f, 1.2f, 0xF59E0B);
		}
		return;
	}
	case EMO_OTHER:
		vp_disc (cx, cy, r * 0.86f, 0x9AA5B1);
		vp_disc (cx, cy, r * 0.74f, 0xE7EBEF);
		vp_disc (cx - r * 0.28f, cy - r * 0.15f, r * 0.1f + 0.4f, 0x6B7682);
		vp_disc (cx + r * 0.28f, cy - r * 0.15f, r * 0.1f + 0.4f, 0x6B7682);
		vp_arc (cx, cy + r * 0.05f, r * 0.35f, 200, 340, 1.1f, 0x6B7682);
		return;
	}
	// the faces
	unsigned face = yel;
	if (id == EMO_ANGRY) { face = 0xF26B4B; rim = 0xA8321A; }
	if (id == EMO_SICK) { face = 0xA6D86E; rim = 0x5E8F2A; ink = 0x2E4A12; }
	if (id == EMO_BLUSH) face = 0xFFC94A;
	emo_face (cx, cy, r, face, rim);
	switch (id)
	{
	case EMO_SMILE: emo_eyes (cx, cy, r, ink); emo_mouth (cx, cy, r, 200, 340, 0.55f, ink, 0.0f); break;
	case EMO_GRIN:
	{
		emo_eyes (cx, cy, r, ink);
		vp_ellipse (cx, cy + r * 0.32f, r * 0.5f, r * 0.3f, ink);
		vp_rrect (cx - r * 0.6f, cy + r * 0.02f, r * 1.2f, r * 0.26f, 0, face);
		vp_ellipse (cx, cy + r * 0.27f, r * 0.36f, r * 0.08f, 0xFFFFFF);
		break;
	}
	case EMO_WINK: emo_eyes (cx, cy, r, ink, true); emo_mouth (cx, cy, r, 200, 340, 0.55f, ink, 0.0f); break;
	case EMO_SAD: emo_eyes (cx, cy, r, ink); emo_mouth (cx, cy, r, 30, 150, 0.42f, ink, 0.65f); break;
	case EMO_CRY:
		emo_eyes (cx, cy, r, ink); emo_mouth (cx, cy, r, 30, 150, 0.42f, ink, 0.65f);
		vp_ellipse (cx - r * 0.5f, cy + r * 0.25f, r * 0.13f + 0.4f, r * 0.26f, 0x4AA8F0);
		vp_ellipse (cx + r * 0.5f, cy + r * 0.25f, r * 0.13f + 0.4f, r * 0.26f, 0x4AA8F0);
		break;
	case EMO_TONGUE:
		emo_eyes (cx, cy, r, ink); emo_mouth (cx, cy, r, 200, 340, 0.5f, ink, 0.0f);
		vp_ellipse (cx + r * 0.08f, cy + r * 0.56f, r * 0.2f, r * 0.22f, 0xE0546A);
		break;
	case EMO_SURPRISED:
		emo_eyes (cx, cy, r, ink);
		vp_ellipse (cx, cy + r * 0.42f, r * 0.2f, r * 0.24f, ink);
		break;
	case EMO_ANGRY:
		emo_eyes (cx, cy, r, ink); emo_mouth (cx, cy, r, 30, 150, 0.4f, ink, 0.7f);
		vp_line (cx - r * 0.6f, cy - r * 0.55f, cx - r * 0.15f, cy - r * 0.35f, r * 0.1f + 0.6f, ink);
		vp_line (cx + r * 0.6f, cy - r * 0.55f, cx + r * 0.15f, cy - r * 0.35f, r * 0.1f + 0.6f, ink);
		break;
	case EMO_COOL:
		vp_rrect (cx - r * 0.82f, cy - r * 0.38f, r * 0.72f, r * 0.42f, r * 0.15f, 0x1E2430);
		vp_rrect (cx + r * 0.1f, cy - r * 0.38f, r * 0.72f, r * 0.42f, r * 0.15f, 0x1E2430);
		vp_line (cx - r * 0.2f, cy - r * 0.3f, cx + r * 0.2f, cy - r * 0.3f, r * 0.1f + 0.5f, 0x1E2430);
		vp_line (cx - r * 0.65f, cy - r * 0.3f, cx - r * 0.45f, cy - r * 0.3f, 0.9f, 0xA0A8B4);
		emo_mouth (cx, cy, r, 210, 330, 0.5f, ink, 0.05f);
		break;
	case EMO_LAUGH:
		vp_arc (cx - r * 0.36f, cy - r * 0.15f, r * 0.17f, 20, 160, r * 0.09f + 0.6f, ink);
		vp_arc (cx + r * 0.36f, cy - r * 0.15f, r * 0.17f, 20, 160, r * 0.09f + 0.6f, ink);
		vp_ellipse (cx, cy + r * 0.35f, r * 0.5f, r * 0.3f, ink);
		vp_rrect (cx - r * 0.6f, cy + r * 0.05f, r * 1.2f, r * 0.26f, 0, face);
		vp_ellipse (cx - r * 0.72f, cy + r * 0.05f, r * 0.14f, r * 0.28f, 0x4AA8F0);
		vp_ellipse (cx + r * 0.72f, cy + r * 0.05f, r * 0.14f, r * 0.28f, 0x4AA8F0);
		break;
	case EMO_LOVE:
		emo_heart (cx - r * 0.36f, cy - r * 0.18f, r * 0.62f, red, 0x9E1328);
		emo_heart (cx + r * 0.36f, cy - r * 0.18f, r * 0.62f, red, 0x9E1328);
		emo_mouth (cx, cy, r, 200, 340, 0.5f, ink, 0.05f);
		break;
	case EMO_KISS:
		emo_eyes (cx, cy, r, ink);
		vp_arc (cx, cy + r * 0.35f, r * 0.13f, 270, 450, r * 0.09f + 0.6f, ink);
		vp_arc (cx, cy + r * 0.6f, r * 0.13f, 270, 450, r * 0.09f + 0.6f, ink);
		emo_heart (cx + r * 0.65f, cy + r * 0.45f, r * 0.45f, red, 0x9E1328);
		break;
	case EMO_CONFUSED:
	{
		emo_eyes (cx, cy, r, ink);
		float w[8] = { cx - r * 0.45f, cy + r * 0.45f, cx - r * 0.15f, cy + r * 0.32f, cx + r * 0.15f, cy + r * 0.5f, cx + r * 0.45f, cy + r * 0.36f };
		for (int i = 0; i < 3; i++) vp_line (w[i * 2], w[i * 2 + 1], w[i * 2 + 2], w[i * 2 + 3], r * 0.09f + 0.6f, ink);
		break;
	}
	case EMO_NEUTRAL:
		emo_eyes (cx, cy, r, ink);
		vp_line (cx - r * 0.38f, cy + r * 0.42f, cx + r * 0.38f, cy + r * 0.42f, r * 0.09f + 0.6f, ink);
		break;
	case EMO_BLUSH:
		emo_eyes (cx, cy, r, ink);
		vp_ellipse (cx - r * 0.55f, cy + r * 0.2f, r * 0.2f, r * 0.12f, mix (face, 0xF0607A, 170));
		vp_ellipse (cx + r * 0.55f, cy + r * 0.2f, r * 0.2f, r * 0.12f, mix (face, 0xF0607A, 170));
		emo_mouth (cx, cy, r, 220, 320, 0.3f, ink, 0.25f);
		break;
	case EMO_THINK:
		emo_eyes (cx, cy, r, ink);
		vp_line (cx - r * 0.1f, cy + r * 0.42f, cx + r * 0.4f, cy + r * 0.32f, r * 0.09f + 0.6f, ink);
		vp_rrect (cx - r * 0.75f, cy + r * 0.45f, r * 0.6f, r * 0.32f, r * 0.15f, 0xF5C26B);
		break;
	case EMO_SLEEP:
	{
		vp_arc (cx - r * 0.36f, cy - r * 0.25f, r * 0.17f, 200, 340, r * 0.08f + 0.6f, ink);
		vp_arc (cx + r * 0.36f, cy - r * 0.25f, r * 0.17f, 200, 340, r * 0.08f + 0.6f, ink);
		vp_ellipse (cx, cy + r * 0.42f, r * 0.13f, r * 0.16f, ink);
		float zz[8] = { cx + r * 0.45f, y + s * 0.02f, cx + r * 0.85f, y + s * 0.02f, cx + r * 0.45f, y + s * 0.2f, cx + r * 0.85f, y + s * 0.2f };
		for (int i = 0; i < 3; i++) vp_line (zz[i * 2], zz[i * 2 + 1], zz[i * 2 + 2], zz[i * 2 + 3], 1.1f, 0x2F7BD8);
		break;
	}
	case EMO_SICK:
		emo_eyes (cx, cy, r, ink); emo_mouth (cx, cy, r, 30, 150, 0.4f, ink, 0.65f);
		break;
	}
}

#endif
