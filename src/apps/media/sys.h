//
// sys.h -- what every part of Media Player takes from the system: the kits' headers, the clock, whole files, a folder
// read, a file read by pieces (POSIX: "SD0:/..." paths are taken as they are), a lock.
//
#ifndef _media_sys_h
#define _media_sys_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <uikit.h>
#include <mediakit.h>
#include <mediakit_abi.h>
#include <audiokit_abi.h>

namespace media {

typedef long long i64;
typedef unsigned long long u64;

enum { SOUND_RATE = 44100 };			// AudioKit's streams, MIDI's frames

// the time in hundredths of a second (Onyx's ticks: 45 = a double click)
static inline unsigned ticks ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 100 + t.tv_nsec / 10000000);
}
static inline i64 now_ms ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (i64) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static inline void sleep_ms (int ms) { usleep ((useconds_t) ms * 1000); }

static inline char *sdup (const char *s) { size_t n = strlen (s ? s : ""); char *d = (char *) malloc (n + 1); memcpy (d, s ? s : "", n + 1); return d; }
static inline void scopy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
// trim spaces at both ends, control characters (a tab: a space)
static inline void tidy (char *s)
{
	int k = 0, n = (int) strlen (s);
	for (int i = 0; i < n; i++) { unsigned char c = (unsigned char) s[i]; if (c == '\t' || c == '\n' || c == '\r') c = ' '; if (c >= 32) s[k++] = (char) c; }
	s[k] = 0;
	while (k > 0 && s[k - 1] == ' ') s[--k] = 0;
	int a = 0; while (s[a] == ' ') a++;
	if (a) memmove (s, s + a, (size_t) (k - a + 1));
}
// a text from a file: what is not UTF-8 taken as Latin-1
static inline void latin1_fix (char *s, int cap)
{
	bool ok = true;
	for (const unsigned char *p = (const unsigned char *) s; *p && ok; )
	{
		int more = *p < 0x80 ? 0 : *p >= 0xF0 ? 3 : *p >= 0xE0 ? 2 : *p >= 0xC0 ? 1 : -1;
		if (more < 0) ok = false;
		p++;
		for (int i = 0; i < more && ok; i++, p++) if ((*p & 0xC0) != 0x80) ok = false;
	}
	if (ok) return;
	char *t = (char *) malloc ((size_t) cap); int k = 0;
	if (!t) return;
	for (const unsigned char *p = (const unsigned char *) s; *p && k + 2 < cap; p++)
	{
		if (*p < 0x80) t[k++] = (char) *p;
		else { t[k++] = (char) (0xC0 | *p >> 6); t[k++] = (char) (0x80 | (*p & 63)); }
	}
	t[k] = 0; scopy (s, t, cap); free (t);
}
static inline unsigned long long now_stamp ()
{	// yyyymmddhhmm (sorts as time)
	time_t t = time (0); struct tm m; localtime_r (&t, &m);
	return (((((unsigned long long) (m.tm_year + 1900) * 100 + m.tm_mon + 1) * 100 + m.tm_mday) * 100 + m.tm_hour) * 100 + m.tm_min);
}
static inline const char *ext_of (const char *path)
{
	const char *d = 0;
	for (const char *p = path; *p; p++) { if (*p == '.') d = p; else if (*p == '/') d = 0; }
	return d ? d + 1 : "";
}
static inline bool ext_is (const char *path, const char *e) { return !strcasecmp (ext_of (path), e); }

// ---- files ---------------------------------------------------------------------------------------------
static inline void make_dir (const char *p) { mkdir (p, 0777); }
// the whole file, a 0 after it (malloc) -> 0: none, empty or larger than max
static inline char *read_all (const char *path, unsigned max, unsigned *size = 0)
{
	int f = open (path, O_RDONLY);
	if (f < 0) return 0;
	struct stat st;
	if (fstat (f, &st) != 0 || st.st_size <= 0 || (u64) st.st_size > max) { close (f); return 0; }
	char *b = (char *) malloc ((size_t) st.st_size + 1);
	long got = 0;
	if (b)
		while (got < st.st_size) { long r = read (f, b + got, (size_t) (st.st_size - got)); if (r <= 0) break; got += r; }
	close (f);
	if (!b || got <= 0) { free (b); return 0; }
	b[got] = 0;
	if (size) *size = (unsigned) got;
	return b;
}
static inline bool save_all (const char *path, const void *b, size_t n)
{
	int f = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (f < 0) return false;
	size_t done = 0;
	while (done < n) { long r = write (f, (const char *) b + done, n - done); if (r <= 0) break; done += (size_t) r; }
	close (f);
	return done == n;
}
// a file read by pieces
struct Src
{
	int h; u64 pos, size;
	Src () : h (-1), pos (0), size (0) {}
	~Src () { close (); }
	bool open (const char *path)
	{
		h = ::open (path, O_RDONLY);
		if (h < 0) return false;
		struct stat st; size = fstat (h, &st) == 0 ? (u64) st.st_size : 0; pos = 0;
		return true;
	}
	void close () { if (h >= 0) ::close (h); h = -1; }
	size_t read (void *buf, size_t n)
	{
		size_t got = 0;
		while (got < n) { long r = ::read (h, (char *) buf + got, n - got); if (r <= 0) break; got += (size_t) r; }
		pos += got;
		return got;
	}
	bool seek (u64 p)
	{
		if (p > size) p = size;
		if (p == pos) return true;
		if (lseek (h, (off_t) p, SEEK_SET) < 0) return false;
		pos = p; return true;
	}
};
// a folder's entries
struct DirReader
{
	DIR *d; char dir[300]; char name[256]; bool isDir; u64 size;
	DirReader () : d (0), isDir (false), size (0) {}
	~DirReader () { close (); }
	bool open (const char *path) { scopy (dir, path, sizeof dir); d = opendir (path); return d != 0; }
	void close () { if (d) closedir (d); d = 0; }
	bool next ()
	{
		struct dirent *e = d ? readdir (d) : 0;
		if (!e) return false;
		scopy (name, e->d_name, sizeof name);
		char p[560]; snprintf (p, sizeof p, "%s/%s", dir, name);
		struct stat st; bool ok = stat (p, &st) == 0;
		isDir = ok && S_ISDIR (st.st_mode); size = ok ? (u64) st.st_size : 0;
		return true;
	}
};

// (the window's words: a thread other than the window's does not call it)
static inline const char *TR (const char *s) { return (const char *) ui_tr ((long) s); }

} // namespace media

#endif
