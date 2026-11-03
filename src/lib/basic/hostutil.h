//
// hostutil.h -- what the programs that host Onyx BASIC on ARMPI share (/bin/basic, QBasic, QBStudio, GPIO Lab, Turtle
// Quest): text between BASIC's Latin-1 and the system's UTF-8, Onyx's "SD:/" paths, whole files, clocks, starting a
// program, the kits as BASIC sees them (#import).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _bas_hostutil_h
#define _bas_hostutil_h

#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <armpi/kit.h>

extern char **environ;

static inline int bas_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void bas_cpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; if (cap > 0) d[i] = 0; }

// Latin-1 text as UTF-8, for the kits.
struct bas_utf8
{
	char *s;
	explicit bas_utf8 (const char *t)
	{
		int n = bas_len (t);
		s = new char[n * 2 + 1];
		int k = 0;
		for (int i = 0; i < n; i++)
		{
			unsigned char c = (unsigned char) t[i];
			if (c < 0x80) s[k++] = (char) c;
			else { s[k++] = (char) (0xC0 | c >> 6); s[k++] = (char) (0x80 | (c & 63)); }
		}
		s[k] = 0;
	}
	~bas_utf8 () { delete [] s; }
	bas_utf8 (const bas_utf8 &) = delete;
	bas_utf8 &operator= (const bas_utf8 &) = delete;
};
// UTF-8 text as Latin-1 ('?' out of it).
static inline void bas_latin1 (char *d, int cap, const char *s)
{
	int k = 0;
	while (*s && k < cap - 1)
	{
		unsigned char c = (unsigned char) *s++;
		if (c < 0x80) { d[k++] = (char) c; continue; }
		int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
		unsigned v = c & (0x3F >> more);
		while (more-- && (*s & 0xC0) == 0x80) v = v << 6 | (*s++ & 63);
		d[k++] = v < 256 ? (char) v : '?';
	}
	d[k] = 0;
}

// A BASIC program's path as ARMPI knows it: Onyx's SD: is the card, SD0:.
static inline void bas_path (char *d, int cap, const char *s)
{
	if (s[0] == 'S' && s[1] == 'D' && s[2] == ':')
	{
		if (!strncmp (s + 3, "/tmp", 4) && (s[7] == '/' || !s[7])) { snprintf (d, cap, "%s", s + 3); return; }	// (/tmp is the RAM's)
		snprintf (d, cap, "SD0:%s", s + 3);
		return;
	}
	bas_cpy (d, s, cap);
}

static inline unsigned bas_ms ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static inline unsigned bas_us ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 1000000 + t.tv_nsec / 1000);
}

// A whole file as a new[] buffer, 0 at its end; 0: it cannot be read.
static inline char *bas_load (const char *path, int *len)
{
	*len = 0;
	int f = open (path, O_RDONLY);
	if (f < 0) return 0;
	struct stat st;
	if (fstat (f, &st) != 0 || (st.st_mode & S_IFMT) == S_IFDIR) { close (f); return 0; }
	long n = st.st_size;
	char *b = new char[n + 1];
	long got = 0;
	while (got < n) { long r = read (f, b + got, n - got); if (r <= 0) break; got += r; }
	close (f);
	b[got] = 0; *len = (int) got;
	return b;
}
static inline bool bas_save (const char *path, const char *d, int n)
{
	int f = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (f < 0) return false;
	int done = 0;
	while (done < n) { long r = write (f, d + done, n - done); if (r <= 0) break; done += (int) r; }
	close (f);
	return done == n;
}
// The index-th name of a folder (a '/' after a folder's), '.' names left out -> its length, 0: no more.
static inline int bas_list_dir (const char *dir, int index, char *o, int cap)
{
	o[0] = 0;
	DIR *d = opendir (dir);
	if (!d) return 0;
	struct dirent *e; int i = 0;
	while ((e = readdir (d)))
	{
		if (e->d_name[0] == '.') continue;
		if (i++ == index)
		{
			bas_cpy (o, e->d_name, cap);
			char full[320]; snprintf (full, sizeof full, "%s/%s", dir, e->d_name);
			struct stat s;
			if (stat (full, &s) == 0 && (s.st_mode & S_IFMT) == S_IFDIR) { int n = bas_len (o); if (n < cap - 1) { o[n] = '/'; o[n + 1] = 0; } }
			break;
		}
	}
	closedir (d);
	return bas_len (o);
}

// A program started with its arguments cut at the blanks ("..." keeps blanks); out >= 0: on the descriptor
// `fd` of the child (fd 1: the output, and the errors with it) -> its pid, 0: it could not start.
static inline pid_t bas_spawn (const char *path, const char *args, int out, int fd)
{
	static char buf[1024]; char *argv[32]; int n = 0;
	bas_cpy (buf, args ? args : "", sizeof buf);
	argv[n++] = (char *) path;
	for (char *p = buf; *p && n < 31; )
	{
		while (*p == ' ') p++;
		if (!*p) break;
		char q = *p == '"' ? '"' : ' ';
		if (q == '"') p++;
		argv[n++] = p;
		while (*p && *p != q) p++;
		if (*p) *p++ = 0;
	}
	argv[n] = 0;
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	if (out >= 0)
	{
		posix_spawn_file_actions_adddup2 (&fa, out, fd);
		if (fd == 1) posix_spawn_file_actions_adddup2 (&fa, out, 2);
	}
	pid_t p = 0;
	int r = posix_spawn (&p, path, &fa, 0, argv, environ);
	posix_spawn_file_actions_destroy (&fa);
	return r ? 0 : p;
}

// A notification on the desktop (service "notify", inc/notify.inc), Latin-1 text -> false: no notifyd.
static inline long bas_svc (long n, long a, long b)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x8) : "memory", "cc");
	return x0;
}
static inline bool bas_notify (const char *title, const char *text)
{
	long ch = bas_svc (0x31, (long) "notify", 0);		// SYS_SVC_OPEN, without waiting
	if (ch < 0) return false;
	static char msg[512];
	bas_utf8 t (title), m (text);
	memset (msg, 0, 48);
	msg[0] = 1;						// op 1
	int n = snprintf (msg + 48, 464, "%s", t.s) + 1;
	if (n < 464) snprintf (msg + 48 + n, 464 - n, "%s", m.s);
	bas_svc (0x32, ch, (long) msg);				// SYS_SVC_CALL
	return true;
}

// PLAYNOTE's "C4", "F#3" -> the MIDI key, -1: not a note.
static inline int bas_note_key (const char *s)
{
	static const signed char semi[7] = { 9, 11, 0, 2, 4, 5, 7 };	// A..G
	char c = *s >= 'a' ? (char) (*s - 32) : *s;
	if (c < 'A' || c > 'G') return -1;
	int k = semi[c - 'A']; s++;
	if (*s == '#') { k++; s++; }
	else if (*s == 'b') { k--; s++; }
	bool neg = *s == '-'; if (neg) s++;
	if (*s < '0' || *s > '9') return -1;
	int o = *s++ - '0';
	if (*s) return -1;
	k += ((neg ? -o : o) + 1) * 12;
	return k < 0 || k > 127 ? -1 : k;
}
// ---- the kits: #import name reads SD0:/lib/<name>.bi (tools/kits/abi2bas.py), its table is opened as any kit's --------
static inline char *bas_kit_source (const char *name, int *len)
{
	*len = 0;
	for (const char *c = name; *c; c++) if (*c == '/' || *c == ':' || *c == '.') return 0;
	char path[80]; snprintf (path, sizeof path, "SD0:/lib/%s.bi", name);
	return bas_load (path, len);
}
static inline void *const *bas_kit_open (const char *name, int minVersion, char *why, int cap)
{
	for (const char *c = name; *c; c++) if (*c == '/' || *c == ':' || *c == '.') { bas_cpy (why, "not a kit's name", cap); return 0; }
	void *k = armpi_kit_open (name, (unsigned) (minVersion > 0 ? minVersion : 0));
	if (!k) { bas_cpy (why, "it is not in SD0:/lib, or the one there is older", cap); return 0; }
	unsigned n = armpi_kit_version (k);
	void **t = new void *[n];				// (a program's kits stay open to its end)
	for (unsigned i = 0; i < n; i++) t[i] = armpi_kit_fn (k, i);
	return t;
}

#endif
