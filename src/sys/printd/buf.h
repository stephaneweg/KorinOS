//
// buf.h -- le tampon qui grandit de printd (pngsave::Buf d'Onyx).
//
#ifndef PRINTD_BUF_H
#define PRINTD_BUF_H

#include <string.h>

// Le source est en francais : lang/en.txt, fusionne dans SD0:/res/lang (tools/lang/merge.py). Un service, sans UIKit : lang_tr d'AppKit, par son rang.
extern "C" void *__armpi_appkit (unsigned rank);
static inline const char *TR (const char *s)
{
	static const char *(*f) (const char *);
	if (!f) f = (const char *(*) (const char *)) __armpi_appkit (42);
	return f ? f (s) : s;
}

struct Buf
{
	unsigned char *b; unsigned n, cap;
	Buf () : b (0), n (0), cap (0) {}
	Buf (const Buf &) = delete;
	Buf &operator= (const Buf &) = delete;
	~Buf () { delete[] b; }
	void grow (unsigned k)
	{
		if (n + k <= cap) return;
		unsigned c = cap ? cap * 2 : 4096; while (c < n + k) c *= 2;
		unsigned char *t = new unsigned char[c];
		if (n) memcpy (t, b, n);
		delete[] b; b = t; cap = c;
	}
	void put (unsigned char c) { grow (1); b[n++] = c; }
	void put (const void *p, unsigned k) { if (!k) return; grow (k); memcpy (b + n, p, k); n += k; }
	void be32 (unsigned v) { put ((unsigned char) (v >> 24)); put ((unsigned char) (v >> 16)); put ((unsigned char) (v >> 8)); put ((unsigned char) v); }
};

#endif
