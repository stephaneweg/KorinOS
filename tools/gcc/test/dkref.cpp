// dkref : la reference de dktest pour le XML mal forme : le lecteur C++ d'Onyx (sheet/xml.h), compile sur le PC.
//   g++ -I<dossier de xml.h recopie + core.h de dkvec.py> dkref.cpp -o dkref ; dkref fuzz.bin fuzz.ref
// Pour chaque cas, la meme empreinte que stream_hash de dktest.c.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "xml.h"
using namespace ss;

static unsigned long g_h;
static void hb (const void *p, long n) { const unsigned char *b = (const unsigned char *) p; for (long i = 0; i < n; i++) { g_h ^= b[i]; g_h *= 1099511628211UL; } }
static void hl (long v) { hb (&v, 8); }
static const char *const QA[] = { "val", "id", "r:id", "w:val", "name", "a:b", "xmlns", "b" };
static const char *const QN[] = { "p", "r", "t", "a" };


// dk_xml_attr : le nom exact d'abord, ou qu'il soit ; sinon la lecture d'Onyx (le premier, prefixe compris).
static bool attr_exact (const XmlReader &x, const char *name, Buf &o)
{
	if (x.ev != X_START || !x.at) return false;
	int k = (int) strlen (name);
	const char *q = x.at, *e = x.ate;
	while (q < e)
	{
		while (q < e && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
		const char *an = q;
		while (q < e && *q != '=' && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;
		int al = (int) (q - an);
		while (q < e && *q != '=') q++;
		if (q >= e) break;
		q++;
		while (q < e && *q != '"' && *q != '\'') q++;
		if (q >= e) break;
		char quote = *q++;
		const char *vs = q;
		while (q < e && *q != quote) q++;
		const char *ve = q;
		if (q < e) q++;
		if (al == k && !memcmp (an, name, k)) { XmlReader::decode (vs, ve, o); return true; }
	}
	return x.attr (name, o);
}

static unsigned long stream_hash (const char *d, long n)
{
	g_h = 14695981039346656037UL;
	XmlReader X (d, (int) n);
	for (int e; (e = X.next ()) != X_EOF; )
	{
		hl (e);
		if (e == X_TEXT) { hl (X.text.n); hb (X.text.b, X.text.n); continue; }
		hl (X.nml); hb (X.nm, X.nml);
		for (int i = 0; i < 4; i++) hl (X.is (QN[i]));
		if (e != X_START) continue;
		hl (X.empty);
		for (int i = 0; i < 8; i++)
		{
			Buf v;
			if (!attr_exact (X, QA[i], v)) { hl (-1); continue; }
			hl (v.n); hb (v.b, v.n);
		}
		{
			Buf v;
			bool got = attr_exact (X, "val", v);
			hl (got ? atoi (v.str ()) : -7);
			hl (got ? v.n && (v.b[0] == '1' || v.b[0] == 't' || v.b[0] == 'T') : true);
		}
	}
	return g_h;
}

int main (int argc, char **argv)
{
	FILE *f = fopen (argv[1], "rb"), *o = fopen (argv[2], "wb");
	if (!f || !o) return 1;
	std::vector<char> b;
	unsigned l;
	while (fread (&l, 4, 1, f) == 1)
	{
		b.resize (l + 1);
		if (fread (b.data (), 1, l, f) != l) return 1;
		unsigned long h = stream_hash (b.data (), l);
		fwrite (&h, 8, 1, o);
	}
	fclose (o);
	return 0;
}
