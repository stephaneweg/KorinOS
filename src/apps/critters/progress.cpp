//
// progress.cpp -- Critters' progress (progress.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "progress.h"
#include <stdio.h>
#include <string.h>

namespace critters {

static const char *const SETTINGS = "settings";

// A whole non-negative number (at most 9 digits, spaces around allowed) -> -1 if it does not read
static int read_num (const char *s)
{
	if (!s) return -1;
	while (*s == ' ' || *s == '\t') s++;
	int v = 0, n = 0;
	while (*s >= '0' && *s <= '9') { if (++n > 9) return -1; v = v * 10 + (*s++ - '0'); }
	while (*s == ' ' || *s == '\t') s++;
	return n && !*s ? v : -1;
}
static void put_num (gkv *kv, const char *section, const char *key, int v)
{
	char b[16];
	snprintf (b, sizeof b, "%d", v);
	gkv_set (kv, section, key, b);
}

void progress_section (const char *path, bool shipped, char *out, int cap)
{
	const char *base = path;
	for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\' || *p == ':') base = p + 1;
	int n = (int) strlen (base);
	if (n > 6)
	{
		const char *e = base + n - 6;
		char low[7];
		for (int i = 0; i < 6; i++) low[i] = (char) (e[i] >= 'A' && e[i] <= 'Z' ? e[i] + 32 : e[i]);
		low[6] = 0;
		if (!strcmp (low, ".level")) n -= 6;
	}
	snprintf (out, (size_t) cap, "%s%.*s", shipped ? "" : "user.", n, base);
}

Best progress_get (const gkv *kv, const char *section)
{
	Best b;
	b.solved = read_num (gkv_get (kv, section, "solved", 0)) == 1;
	b.saved = read_num (gkv_get (kv, section, "saved", 0));
	b.timeSec = read_num (gkv_get (kv, section, "time", 0));
	return b;
}

bool progress_won (gkv *kv, const char *section, int saved, int timeSec)
{
	Best b = progress_get (kv, section);
	bool better = false;
	if (!b.solved) gkv_set (kv, section, "solved", "1");
	if (b.saved < 0 || saved > b.saved) { put_num (kv, section, "saved", saved); better = true; }
	if (b.timeSec < 0 || timeSec < b.timeSec) { put_num (kv, section, "time", timeSec); better = true; }
	return b.solved && better;
}

bool progress_open (const gkv *kv, const char *const *chain, int n, int k)
{
	if (k <= 0) return true;
	if (k >= n) return false;
	return progress_get (kv, chain[k - 1]).solved;
}

const char *progress_setting (const gkv *kv, const char *key, const char *def) { return gkv_get (kv, SETTINGS, key, def); }
void progress_set_setting (gkv *kv, const char *key, const char *value) { gkv_set (kv, SETTINGS, key, value); }

}
