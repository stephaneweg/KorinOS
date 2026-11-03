//
// sys.h -- what every part of Cosmo Zoom! takes from the system: the kits' headers, the clock, whole files, the words,
// the little key = value files (cups.ini, the save).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_SYS_H
#define CZ_SYS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <uikit.h>
#include <audiokit_abi.h>
#include <gpukit_abi.h>
#undef quad				// (newlib's BSD types: a macro named quad)

#define APPDIR "SD0:/apps/cosmozoom.app/"
#define HOMEDIR "SD0:/home/cosmozoom"
#define SAVE HOMEDIR "/save.ini"

#define TR(s) ((const char *) ui_tr ((long) (s)))
#define TRN(s) (s)

static inline unsigned clock_us ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) ((unsigned long long) t.tv_sec * 1000000u + (unsigned long long) t.tv_nsec / 1000u);
}

// the whole file, a 0 after it (malloc) -> 0: none
static char *read_file (const char *path, long *size)
{
	int f = open (path, O_RDONLY);
	if (f < 0) return 0;
	struct stat st;
	if (fstat (f, &st) != 0 || st.st_size < 0) { close (f); return 0; }
	char *b = (char *) malloc ((size_t) st.st_size + 1);
	long got = 0;
	if (b)
		while (got < st.st_size) { long r = read (f, b + got, (size_t) (st.st_size - got)); if (r <= 0) break; got += r; }
	close (f);
	if (!b) return 0;
	b[got] = 0;
	if (size) *size = got;
	return b;
}
static bool save_file (const char *path, const void *b, size_t n)
{
	int f = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (f < 0) return false;
	size_t done = 0;
	while (done < n) { long r = write (f, (const char *) b + done, n - done); if (r <= 0) break; done += (size_t) r; }
	close (f);
	return done == n;
}

// ---- key = value, [sections], # comments ---------------------------------------------------------------------------
struct Kv
{
	struct Item { char sec[16], key[64], val[160]; };
	Item *it = 0; int n = 0, cap = 0;
	void add (const char *s, const char *k, const char *v)
	{
		if (n == cap) { cap = cap ? cap * 2 : 64; it = (Item *) realloc (it, sizeof (Item) * (size_t) cap); }
		snprintf (it[n].sec, sizeof it[n].sec, "%s", s); snprintf (it[n].key, sizeof it[n].key, "%s", k); snprintf (it[n].val, sizeof it[n].val, "%s", v); n++;
	}
	void load (const char *path)
	{
		char *b = read_file (path, 0); if (!b) return;
		char sec[16] = "";
		for (char *l = b, *e; l && *l; l = e)
		{
			e = strchr (l, '\n'); if (e) *e++ = 0;
			int L = (int) strlen (l); while (L && (l[L - 1] == '\r' || l[L - 1] == ' ')) l[--L] = 0;
			while (*l == ' ') l++;
			if (!*l || *l == '#') continue;
			if (*l == '[') { char *c = strchr (l, ']'); if (c) *c = 0; snprintf (sec, sizeof sec, "%s", l + 1); continue; }
			char *q = strchr (l, '='); if (!q) continue;
			char *kend = q; while (kend > l && kend[-1] == ' ') kend--;
			*kend = 0; q++; while (*q == ' ') q++;
			add (sec, l, q);
		}
		free (b);
	}
	const char *get (const char *s, const char *k) const
	{
		for (int i = 0; i < n; i++) if (!strcmp (it[i].sec, s) && !strcmp (it[i].key, k)) return it[i].val;
		return 0;
	}
	void set (const char *s, const char *k, const char *v)
	{
		for (int i = 0; i < n; i++) if (!strcmp (it[i].sec, s) && !strcmp (it[i].key, k)) { snprintf (it[i].val, sizeof it[i].val, "%s", v); return; }
		add (s, k, v);
	}
	void save (const char *path, const char *comment) const
	{
		size_t cap2 = 256 + (size_t) n * 240; char *b = (char *) malloc (cap2); if (!b) return;
		int m = snprintf (b, cap2, "# %s\n", comment);
		for (int i = 0; i < n; i++)
		{
			bool head = true; for (int j = 0; j < i; j++) if (!strcmp (it[j].sec, it[i].sec)) { head = false; break; }
			if (!head) continue;
			m += snprintf (b + m, cap2 - (size_t) m, "[%s]\n", it[i].sec);
			for (int j = i; j < n; j++) if (!strcmp (it[j].sec, it[i].sec)) m += snprintf (b + m, cap2 - (size_t) m, "%s = %s\n", it[j].key, it[j].val);
		}
		save_file (path, b, (size_t) m); free (b);
	}
};

#endif
