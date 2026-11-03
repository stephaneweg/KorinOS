/* dktest : essai de DocumentKit sous ARMPI (QEMU). Les vecteurs viennent du PC (tools/gcc/test/dkvec.py) :
 *   SD0:/dkvec/list.txt     les fichiers XML reels, chacun avec sa reference .ref (expat)
 *   SD0:/dkvec/num.txt      dates, longueurs, UTF-8, CP1252 calcules par Python
 *   SD0:/dkvec/fuzz.bin     XML mal forme ou tronque, fuzz.ref : l'empreinte du lecteur C++ d'Onyx (dkref.cpp)
 * Tout tampon d'entree finit contre une page PROT_NONE : un octet lu de trop est une faute.
 * Resultat sur la console et dans SD0:/dkvec/result.txt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <documentkit_abi.h>

#define L(x) ((long) (x))
#define DIR "SD0:/dkvec/"
#define GUARD_MAX (8 * 1024 * 1024)
#define PAGE 4096

static FILE *g_res;
static int g_fail, g_checks;

static void say (const char *fmt, const char *a, long b, long c)
{
	char t[400];
	snprintf (t, sizeof t, fmt, a, b, c);
	fputs (t, stdout);
	fflush (stdout);
	if (g_res) { fputs (t, g_res); fflush (g_res); }
}
static void bad (const char *what, const char *a, long b, long c)
{
	g_fail++;
	if (g_fail < 60) { say ("ECHEC %s", what, 0, 0); say (" : %s %ld %ld\n", a ? a : "", b, c); }
}
#define CHECK(cond, what) do { g_checks++; if (!(cond)) bad (what, 0, __LINE__, 0); } while (0)
#define EQ(val, want, what) do { long v_ = (long) (val), w_ = (long) (want); g_checks++; if (v_ != w_) bad (what, 0, v_, w_); } while (0)
#define STREQ(val, want, what) do { g_checks++; if (strcmp ((val), (want))) bad (what, (val), __LINE__, 0); } while (0)

static char *g_guard;		/* GUARD_MAX lisibles, puis une page interdite */
static char *guarded (const void *d, long n)
{
	char *p = g_guard + GUARD_MAX - n;
	memcpy (p, d, n);
	return p;
}

static char *load (const char *name, long *len)
{
	FILE *f = fopen (name, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	char *b = malloc (n + 1);
	if (fread (b, 1, n, f) != (size_t) n) { fclose (f); free (b); return 0; }
	b[n] = 0;
	fclose (f);
	*len = n;
	return b;
}

/* ---- essais fixes ---------------------------------------------------------------------- */

static void t_xml (void)
{
	char o[256];
	static const char D[] =
		"<?xml version=\"1.0\"?><!-- un <commentaire> --><!DOCTYPE x>"
		"<w:doc xmlns:w=\"u:w\" w:val=\"12\" plain='a&amp;b' r:id=\"rId7\">"
		"<w:p w:val=\"false\"/>a&lt;b&#x20AC;&#233;&unknown;&<![CDATA[<raw&amp;>]]>"
		"<x:r a=\"1>2\" b='\"'><t>texte</t><u/></x:r></w:doc>";
	char *g = guarded (D, sizeof D - 1);
	long r = dk_xml_open (L (g), sizeof D - 1);
	CHECK (r > 0, "open");
	EQ (dk_xml_next (r), DKX_START, "START doc");
	CHECK (dk_xml_is (r, L ("doc")), "is sans prefixe");
	CHECK (!dk_xml_is (r, L ("do")), "is court");
	CHECK (!dk_xml_is (r, L ("docs")), "is long");
	CHECK (dk_xml_isq (r, L ("w:doc")), "isq");
	CHECK (!dk_xml_isq (r, L ("doc")), "isq sans prefixe");
	EQ (dk_xml_name (r, L (o), sizeof o), 5, "name");
	STREQ (o, "w:doc", "name");
	EQ (dk_xml_attr (r, L ("val"), L (o), sizeof o), 2, "attr sans prefixe");
	STREQ (o, "12", "attr");
	EQ (dk_xml_attr (r, L ("w:val"), L (o), sizeof o), 2, "attr prefixe");
	EQ (dk_xml_attr (r, L ("w"), L (o), sizeof o), DKE_NOTFOUND, "xmlns:w n'est pas w");
	EQ (dk_xml_attr (r, L ("xmlns:w"), L (o), sizeof o), 3, "xmlns exact");
	EQ (dk_xml_attr (r, L ("plain"), L (o), sizeof o), 3, "attr entite");
	{
		static const char q[] = "<p:sldId r:id=\"rId7\" id=\"256\"/>";
		long x = dk_xml_open (L (q), sizeof q - 1);
		char v[16];
		dk_xml_next (x);
		EQ (dk_xml_attr (x, L ("id"), L (v), sizeof v), 3, "le nom exact avant le prefixe");
		STREQ (v, "256", "id, pas r:id");
		EQ (dk_xml_attr (x, L ("r:id"), L (v), sizeof v), 4, "r:id exact");
		dk_xml_close (x);
	}
	STREQ (o, "a&b", "attr entite");
	EQ (dk_xml_attr (r, L ("plain"), L (o), 2), 3, "attr tronque : longueur complete");
	STREQ (o, "a", "attr tronque");
	EQ (dk_xml_attr (r, L ("plain"), 0, 0), 3, "attr longueur seule");
	EQ (dk_xml_attr_int (r, L ("val"), 99), 12, "attr_int");
	EQ (dk_xml_attr_int (r, L ("nope"), -5), -5, "attr_int defaut");
	EQ (dk_xml_attr_bool (r, L ("val"), 0), 1, "attr_bool");
	EQ (dk_xml_attr_bool (r, L ("nope"), 1), 1, "attr_bool defaut");
	EQ (dk_xml_attr_on (r, 0), 1, "attr_on 12");
	EQ (dk_xml_attr_on (r, L ("nope")), 1, "attr_on absent");
	EQ (dk_xml_empty (r), 0, "pas vide");
	EQ (dk_xml_next (r), DKX_START, "START p");
	EQ (dk_xml_empty (r), 1, "vide");
	EQ (dk_xml_attr_on (r, 0), 0, "attr_on false");
	EQ (dk_xml_attr_bool (r, L ("val"), 1), 0, "attr_bool false");
	EQ (dk_xml_next (r), DKX_END, "END p");
	CHECK (dk_xml_is (r, L ("p")), "END p nom");
	EQ (dk_xml_attr (r, L ("val"), L (o), sizeof o), DKE_NOTFOUND, "pas d'attribut sur END");
	EQ (dk_xml_next (r), DKX_TEXT, "TEXT");
	EQ (dk_xml_text (r, L (o), sizeof o), 18, "texte decode");
	STREQ (o, "a<b\xE2\x82\xAC\xC3\xA9&unknown;&", "texte decode");
	EQ (dk_xml_next (r), DKX_TEXT, "CDATA");
	EQ (dk_xml_text (r, L (o), sizeof o), 10, "CDATA");
	STREQ (o, "<raw&amp;>", "CDATA");
	EQ (dk_xml_next (r), DKX_START, "START r");
	EQ (dk_xml_attr (r, L ("a"), L (o), sizeof o), 3, "> entre guillemets");
	EQ (dk_xml_attr (r, L ("b"), L (o), sizeof o), 1, "guillemet");
	EQ (dk_xml_skip (r), 0, "skip");
	EQ (dk_xml_event (r), DKX_END, "skip : sur le END");
	CHECK (dk_xml_is (r, L ("r")), "skip : END r");
	EQ (dk_xml_next (r), DKX_END, "END doc");
	EQ (dk_xml_next (r), DKX_EOF, "EOF");
	EQ (dk_xml_next (r), DKX_EOF, "EOF encore");
	EQ (dk_xml_pos (r), sizeof D - 1, "pos");
	EQ (dk_xml_close (r), 0, "close");
	EQ (dk_xml_next (r), DKE_BADF, "poignee fermee");
	EQ (dk_xml_next (0), DKE_BADF, "poignee 0");
	EQ (dk_xml_next (9999), DKE_BADF, "poignee folle");

	r = dk_xml_open (L ("<a>x&#13;y&#9;&#10;&#65;&#x41;&lt;&#;&#xZ;</a>"), 43);
	dk_xml_next (r);
	EQ (dk_xml_next (r), DKX_TEXT, "references");
	dk_xml_text (r, L (o), sizeof o);
	STREQ (o, "x\ry\t\nAA<&#;", "references");
	dk_xml_close (r);

	/* cases epuisees */
	long h[70]; int n = 0;
	while (n < 70) { h[n] = dk_xml_open (L ("<a/>"), 4); if (h[n] < 0) break; n++; }
	EQ (n, 64, "64 lecteurs");
	EQ (h[n], DKE_NOMEM, "65e");
	while (n) dk_xml_close (h[--n]);

	/* echappement */
	EQ (dk_xml_esc (L ("a<&>\"\x01\tb"), -1, L (o), sizeof o), 22, "esc");
	STREQ (o, "a&lt;&amp;&gt;&quot;\tb", "esc");
	EQ (dk_xml_esc (L ("a<b"), 2, L (o), sizeof o), 4 + 1, "esc n");
	EQ (dk_xml_esc_attr (L ("a\tb\nc\r\""), -1, L (o), sizeof o), 23, "esc attr");
	STREQ (o, "a&#9;b&#10;c&#13;&quot;", "esc attr");
	memset (o, 'Z', sizeof o);
	EQ (dk_xml_esc (L ("<<"), -1, L (o), 6), 8, "esc tronque");
	CHECK (o[5] == 0 && o[6] == 'Z', "esc borne");
}

static void t_text (void)
{
	char o[64]; unsigned u[8];
	EQ (dk_utf8_encode (0x41, L (o)), 1, "enc 1");
	EQ (dk_utf8_encode (0xE9, L (o)), 2, "enc 2");
	EQ (dk_utf8_encode (0x20AC, L (o)), 3, "enc 3");
	CHECK (!memcmp (o, "\xE2\x82\xAC", 3), "enc 3");
	EQ (dk_utf8_encode (0x1F600, L (o)), 4, "enc 4");
	CHECK (!memcmp (o, "\xF0\x9F\x98\x80", 4), "enc 4");
	EQ (dk_utf8_encode (0xD800, L (o)), 3, "enc surrogate");
	CHECK (!memcmp (o, "\xEF\xBF\xBD", 3), "enc surrogate");
	EQ (dk_utf8_decode (L ("\xE2\x82\xAC"), 3, L (u)), 3, "dec");
	EQ (u[0], 0x20AC, "dec");
	EQ (dk_utf8_decode (L ("\xE2\x82"), 2, L (u)), DKE_CORRUPT, "dec coupe");
	EQ (dk_utf8_decode (L ("\xC0\x80"), 2, L (u)), DKE_CORRUPT, "dec trop long");
	EQ (dk_utf8_decode (L ("a"), 0, L (u)), 0, "dec fin");
	EQ (dk_utf8_to_cp (L ("a\xC3\xA9\xFFz"), 5, L (u), 8), 4, "to_cp");
	CHECK (u[1] == 0xE9 && u[2] == 0xFF && u[3] == 'z', "to_cp");
	EQ (dk_utf8_to_cp (L ("abc"), 3, L (u), 2), 2, "to_cp borne");
	u[0] = 0xE9; u[1] = 0x20AC;
	EQ (dk_cp_to_utf8 (L (u), 2, L (o), sizeof o), 5, "cp_to_utf8");
	EQ (dk_latin1_to_utf8 (L ("\xE9\x80"), 2, L (o), sizeof o), 4, "latin1");
	CHECK (!memcmp (o, "\xC3\xA9\xC2\x80", 5), "latin1");
	EQ (dk_cp1252_to_utf8 (L ("\x80"), 1, L (o), sizeof o), 3, "cp1252");
	STREQ (o, "\xE2\x82\xAC", "cp1252");
	EQ (dk_utf8_to_cp1252 (L ("\xE2\x82\xAC\xC3\xA9\xE4\xB8\xAD"), 8, L (o), sizeof o, '?'), 3, "vers cp1252");
	STREQ (o, "\x80\xE9?", "vers cp1252");
	EQ (dk_utf8_to_cp1252 (L ("a\xE4\xB8\xAD"), 4, L (o), sizeof o, 0), 1, "vers cp1252 retire");
	EQ (dk_utf8_to_latin1 (L ("\xE2\x82\xAC\xC3\xA9"), 5, L (o), sizeof o, '?'), 2, "vers latin1");
	STREQ (o, "?\xE9", "vers latin1");
	EQ (dk_hash (L ("abc"), 3), (0x1A47E90Bu ^ 3u), "hash FNV-1a");
	EQ (dk_hash (0, 0), 2166136261u, "hash vide");
}

static void t_len (void)
{
	char o[32];
	EQ (dk_len_parse (L ("2.54cm"), DKU_TWIP, DKU_TWIP, -1), 1440, "cm");
	EQ (dk_len_parse (L ("2.54cm"), DKU_HMM, DKU_HMM, -1), 2540, "cm hmm");
	EQ (dk_len_parse (L ("1in"), DKU_EMU, DKU_EMU, -1), 914400, "in");
	EQ (dk_len_parse (L ("12pt"), DKU_TWIP, DKU_TWIP, -1), 240, "pt");
	EQ (dk_len_parse (L ("-1,5mm"), DKU_HMM, DKU_HMM, -1), -150, "virgule, signe");
	EQ (dk_len_parse (L ("1pc"), DKU_PT, DKU_PT, -1), 12, "pc");
	EQ (dk_len_parse (L ("96px"), DKU_IN, DKU_IN, -1), 1, "px");
	EQ (dk_len_parse (L ("720"), DKU_PT, DKU_TWIP, -1), 36, "nu");
	EQ (dk_len_parse (L ("50%"), DKU_TWIP, DKU_TWIP, -7), -7, "%");
	EQ (dk_len_parse (L ("auto"), DKU_TWIP, DKU_TWIP, -7), -7, "pas de nombre");
	EQ (dk_len_parse (L ("-"), DKU_TWIP, DKU_TWIP, -7), -7, "signe seul");
	EQ (dk_len_parse (0, DKU_TWIP, DKU_TWIP, -7), -7, "nul");
	EQ (dk_len_conv (1440, DKU_TWIP, DKU_EMU), 914400, "conv");
	EQ (dk_len_conv (-1440, DKU_TWIP, DKU_IN), -1, "conv signe");
	EQ (dk_len_write (1440, DKU_TWIP, DKU_CM, 4, L (o), sizeof o), 8, "write");
	STREQ (o, "2.5400cm", "write");
	EQ (dk_len_write (-567, DKU_TWIP, DKU_IN, 2, L (o), sizeof o), 7, "write -");
	STREQ (o, "-0.39in", "write -");
	EQ (dk_len_write (19990, DKU_HMM, DKU_CM, 0, L (o), sizeof o), 4, "write 0 decimale");
	STREQ (o, "20cm", "write 0 decimale");
	EQ (dk_len_write (635, DKU_EMU, DKU_TWIP, 0, L (o), sizeof o), 1, "write sans suffixe");
	EQ (dk_color_parse (L ("#FF8000"), -1), 0xFF8000, "couleur #");
	EQ (dk_color_parse (L ("ff8000"), -1), 0xFF8000, "couleur");
	EQ (dk_color_parse (L ("80FF8000"), -1), 0xFF8000, "couleur argb");
	EQ (dk_color_parse (L ("auto"), -1), -1, "couleur auto");
	EQ (dk_color_parse (L ("#FFF"), -1), -1, "couleur courte");
	EQ (dk_color_parse (L (""), 5), 5, "couleur vide");
	EQ (dk_color_write (0xFF8000, 0, L (o)), 6, "write");
	STREQ (o, "FF8000", "write");
	EQ (dk_color_write (0xFF8000, DKC_HASH | DKC_LOWER, L (o)), 7, "write #");
	STREQ (o, "#ff8000", "write #");
	EQ (dk_color_write (0x0000AB, DKC_ARGB, L (o)), 8, "write argb");
	STREQ (o, "FF0000AB", "write argb");
}

static void t_date (void)
{
	int y, m, d;
	EQ (dk_days_from_civil (1970, 1, 1), 0, "epoque");
	EQ (dk_days_from_civil (2026, 14, 1), dk_days_from_civil (2027, 2, 1), "mois reporte");
	EQ (dk_days_from_civil (2026, 0, 1), dk_days_from_civil (2025, 12, 1), "mois 0");
	EQ (dk_days_from_civil (2026, 1, 32), dk_days_from_civil (2026, 2, 1), "jour reporte");
	dk_civil_from_days (dk_days_from_civil (2026, 10, 31), L (&y), L (&m), L (&d));
	CHECK (y == 2026 && m == 10 && d == 31, "civil");
	EQ (dk_weekday (dk_days_from_civil (2026, 10, 31)), 6, "samedi");
	EQ (dk_weekday (-1), 3, "mercredi 31/12/1969");
	EQ (dk_excel_from_civil (1900, 1, 1, 0), 1, "serie 1");
	EQ (dk_excel_from_civil (1900, 2, 28, 0), 59, "serie 59");
	EQ (dk_excel_from_civil (1900, 2, 29, 0), 60, "serie 60");
	EQ (dk_excel_from_civil (1900, 3, 1, 0), 61, "serie 61");
	EQ (dk_excel_from_civil (2026, 10, 31, 0), 46326, "serie");
	EQ (dk_excel_from_civil (2026, 10, 31, 1), 46326 - 1462, "serie 1904");
	EQ (dk_excel_from_civil (99, 1, 1, 0), dk_excel_from_civil (1999, 1, 1, 0), "annee sur 2 chiffres");
	dk_civil_from_excel (60, 0, L (&y), L (&m), L (&d));
	CHECK (y == 1900 && m == 2 && d == 29, "29 fevrier 1900");
	dk_civil_from_excel (0, 0, L (&y), L (&m), L (&d));
	CHECK (y == 1900 && m == 1 && d == 0, "serie 0");
	dk_civil_from_excel (46326 - 1462, 1, L (&y), L (&m), L (&d));
	CHECK (y == 2026 && m == 10 && d == 31, "serie 1904 retour");
}

/* un CSV : les champs attendus, separes par \x1F ; \x1E apres chaque enregistrement ; [] autour des guillemetes */
static void csv_walk (const char *data, long n, long sep, const char *want, long wsep)
{
	char *g = guarded (data, n), got[512], f[256];
	long c = dk_csv_open (L (g), n, sep), k, p = 0;
	CHECK (c > 0, "csv_open");
	if (wsep) EQ (dk_csv_sep (c), wsep, "csv separateur");
	while ((k = dk_csv_next (c)) != 0)
	{
		long l = dk_csv_text (c, L (f), sizeof f);
		EQ (l, strlen (f), "csv longueur");
		p += snprintf (got + p, sizeof got - p, (k & DKV_QUOTED) ? "[%s]" : "%s", f);
		got[p++] = (k & DKV_EOL) ? 0x1E : 0x1F;
		got[p] = 0;
	}
	got[p] = 0;
	dk_csv_close (c);
	STREQ (got, want, "csv");
}
static void t_csv (void)
{
	char o[64];
	csv_walk ("a,b\r\n1,2\n", 9, 0, "a\x1F" "b\x1E" "1\x1F" "2\x1E", ',');
	csv_walk ("\xEF\xBB\xBFx;y\n1;2\n3;4\n", 15, 0, "x\x1Fy\x1E" "1\x1F" "2\x1E" "3\x1F" "4\x1E", ';');
	csv_walk ("a\tb\tc\n1\t2\t3\n", 12, 0, "a\x1F" "b\x1F" "c\x1E" "1\x1F" "2\x1F" "3\x1E", '\t');
	csv_walk ("\"a,b\",\"c\"\"d\",\"\",\ne\"f,\"g\nh\"\n", 27, ',', "[a,b]\x1F[c\"d]\x1F[]\x1F\x1E" "e\"f\x1F[g\nh]\x1E", 0);
	csv_walk ("a,b,", 4, ',', "a\x1F" "b\x1F\x1E", 0);
	csv_walk ("a,b", 3, ',', "a\x1F" "b\x1E", 0);
	csv_walk ("\n\n", 2, ',', "\x1E\x1E", 0);
	csv_walk ("", 0, ',', "", 0);
	csv_walk ("\"non ferme", 10, ',', "[non ferme]\x1E", 0);
	csv_walk ("a\rb\n", 4, ',', "a\rb\x1E", 0);
	EQ (dk_csv_esc (L ("simple"), -1, ',', L (o), sizeof o), 6, "esc");
	EQ (dk_csv_esc (L ("a,\"b"), -1, ',', L (o), sizeof o), 7, "esc guillemets");
	STREQ (o, "\"a,\"\"b\"", "esc guillemets");
	EQ (dk_csv_esc (L ("a;b"), -1, ',', L (o), sizeof o), 3, "esc autre separateur");
	EQ (dk_csv_esc (L ("a\nb"), 3, ',', L (o), sizeof o), 5, "esc saut");

	/* aller-retour au hasard */
	static const char A[] = "ab,;\"\n\r x";
	unsigned s = 12345;
	for (int it = 0; it < 500; it++)
	{
		char fields[4][24], buf[400]; long p = 0, nf = 1 + it % 4, cnt = 0;
		for (int i = 0; i < nf; i++)
		{
			int l = 0, n = (s = s * 1103515245 + 12345) >> 16 & 15;
			for (; l < n; l++) fields[i][l] = A[((s = s * 1103515245 + 12345) >> 16) % 9];
			fields[i][l] = 0;
			if (i) buf[p++] = ',';
			if (!fields[i][0] && nf == 1) { strcpy (fields[i], "x"); }
			p += dk_csv_esc (L (fields[i]), -1, ',', L (buf + p), sizeof buf - p);
		}
		buf[p++] = '\r'; buf[p++] = '\n';
		char *g = guarded (buf, p);
		long c = dk_csv_open (L (g), p, ','), k; char f[64];
		while ((k = dk_csv_next (c)) != 0)
		{
			dk_csv_text (c, L (f), sizeof f);
			if (cnt < nf) { g_checks++; if (strcmp (f, fields[cnt])) bad ("csv aller-retour", f, it, cnt); }
			cnt++;
			if (k & DKV_EOL) break;
		}
		dk_csv_close (c);
		EQ (cnt, nf, "csv aller-retour champs");
	}
}

static void put16 (char *p, unsigned v) { p[0] = (char) v; p[1] = (char) (v >> 8); }
static void put32 (char *p, unsigned v) { put16 (p, v & 0xFFFF); put16 (p + 2, v >> 16); }
/* un ZIP minimal : les noms, sans donnees (seul le repertoire central compte) */
static long mini_zip (char *z, const char *const *names, int n)
{
	long p = 0;
	put32 (z, 0x04034B50); memset (z + 4, 0, 26); p = 30;
	long cd = p;
	for (int i = 0; i < n; i++)
	{
		int l = (int) strlen (names[i]);
		memset (z + p, 0, 46);
		put32 (z + p, 0x02014B50); put16 (z + p + 28, (unsigned) l);
		memcpy (z + p + 46, names[i], l);
		p += 46 + l;
	}
	memset (z + p, 0, 22);
	put32 (z + p, 0x06054B50); put16 (z + p + 10, (unsigned) n); put32 (z + p + 16, (unsigned) cd);
	return p + 22;
}
static void t_pkg (void)
{
	char o[200], z[1024];
	EQ (dk_doc_kind (L ("{\\rtf1\\ansi}"), 11), DKK_RTF, "rtf");
	EQ (dk_doc_kind (L ("\xD0\xCF\x11\xE0\xA1\xB1"), 6), DKK_OLE, "ole");
	EQ (dk_doc_kind (L ("hello world"), 11), DKK_UNKNOWN, "texte");
	EQ (dk_doc_kind (L ("PK"), 2), DKK_UNKNOWN, "PK");
	EQ (dk_doc_kind (L ("PK\x03\x04zz"), 6), DKK_ZIP, "ZIP coupe");
	static const char *const dn[] = { "_rels/.rels", "[Content_Types].xml", "word/document.xml" };
	static const char *const xn[] = { "xl/workbook.xml", "[Content_Types].xml" };
	static const char *const pn[] = { "[Content_Types].xml", "docProps/app.xml", "ppt/presentation.xml" };
	static const char *const nn[] = { "word/document.xml", "readme.txt" };
	long n = mini_zip (z, dn, 3);
	EQ (dk_doc_kind (L (guarded (z, n)), n), DKK_DOCX, "docx");
	n = mini_zip (z, xn, 2);
	EQ (dk_doc_kind (L (guarded (z, n)), n), DKK_XLSX, "xlsx");
	n = mini_zip (z, pn, 3);
	EQ (dk_doc_kind (L (guarded (z, n)), n), DKK_PPTX, "pptx");
	n = mini_zip (z, nn, 2);
	EQ (dk_doc_kind (L (guarded (z, n)), n), DKK_ZIP, "ZIP sans [Content_Types]");
	/* ODF : mimetype en tete, non compresse */
	static const char mt[] = "application/vnd.oasis.opendocument.spreadsheet";
	memset (z, 0, 30); put32 (z, 0x04034B50); put16 (z + 26, 8); put32 (z + 18, sizeof mt - 1);
	memcpy (z + 30, "mimetype", 8); memcpy (z + 38, mt, sizeof mt - 1);
	EQ (dk_doc_kind (L (guarded (z, 38 + sizeof mt - 1)), 38 + sizeof mt - 1), DKK_ODS, "ods");
	EQ (dk_doc_kind (L (guarded (z, 50)), 50), DKK_ZIP, "ods tronque");
	EQ (dk_doc_kind_mime (L ("application/vnd.oasis.opendocument.text"), -1), DKK_ODT, "mime odt");
	EQ (dk_doc_kind_mime (L ("application/vnd.oasis.opendocument.text-template"), -1), DKK_ODT, "mime gabarit");
	EQ (dk_doc_kind_mime (L ("application/vnd.oasis.opendocument.presentation\n"), -1), DKK_ODP, "mime odp");
	EQ (dk_doc_kind_mime (L ("application/vnd.oasis.opendocument"), -1), DKK_UNKNOWN, "mime court");
	EQ (dk_doc_kind_mime (L ("text/plain"), -1), DKK_UNKNOWN, "mime autre");
	for (long k = DKK_DOCX; k <= DKK_ODP; k++) EQ (dk_doc_kind_mime (dk_doc_mime (k), -1), k, "mime aller-retour");
	EQ (dk_doc_mime (0), 0, "mime 0");
	EQ (dk_doc_mime (DKK_RTF), 0, "mime rtf");

	dk_part_path (L ("word/document.xml"), L ("media/a.png"), L (o), sizeof o);
	STREQ (o, "word/media/a.png", "part");
	dk_part_path (L ("ppt/slides/slide1.xml"), L ("../media/i.png"), L (o), sizeof o);
	STREQ (o, "ppt/media/i.png", "part ..");
	dk_part_path (L ("a/b/c/d.xml"), L ("../../../../x.xml"), L (o), sizeof o);
	STREQ (o, "x.xml", "part trop de ..");
	dk_part_path (L ("a/d.xml"), L ("./x.xml"), L (o), sizeof o);
	STREQ (o, "a/x.xml", "part .");
	dk_part_path (L ("word/d.xml"), L ("/xl/a.xml"), L (o), sizeof o);
	STREQ (o, "xl/a.xml", "part absolue");
	EQ (dk_part_path (L ("word/d.xml"), L ("abc"), L (o), 4), 8, "part longueur");
	STREQ (o, "wor", "part borne");
	dk_rels_path (L ("word/document.xml"), L (o), sizeof o);
	STREQ (o, "word/_rels/document.xml.rels", "rels");
	dk_rels_path (L ("presentation.xml"), L (o), sizeof o);
	STREQ (o, "_rels/presentation.xml.rels", "rels racine");
	dk_rels_path (L (""), L (o), sizeof o);
	STREQ (o, "_rels/.rels", "rels paquet");

	static const char R[] = "<?xml version=\"1.0\"?><Relationships xmlns=\"u\">"
		"<Relationship Id=\"rId1\" Type=\"http://x/y/styles\" Target=\"styles.xml\"/>"
		"<Relationship Target=\"http://a.b/?x=1&amp;y=2\" Id=\"rId2\" TargetMode=\"External\" Type=\"http://x/y/hyperlink\"/>"
		"<Relationship Id=\"rId3\" Type=\"http://x/y/slideMaster\" Target=\"../m/m1.xml\"></Relationship>"
		"</Relationships>";
	char *g = guarded (R, sizeof R - 1); long gn = sizeof R - 1;
	EQ (dk_rel_count (L (g), gn), 3, "rel_count");
	EQ (dk_rel_get (L (g), gn, 0, DKR_ID, L (o), sizeof o), 4, "rel_get id");
	STREQ (o, "rId1", "rel_get id");
	dk_rel_get (L (g), gn, 1, DKR_TYPE, L (o), sizeof o);
	STREQ (o, "hyperlink", "rel_get type");
	dk_rel_get (L (g), gn, 1, DKR_TARGET, L (o), sizeof o);
	STREQ (o, "http://a.b/?x=1&y=2", "rel_get cible");
	dk_rel_get (L (g), gn, 1, DKR_MODE, L (o), sizeof o);
	STREQ (o, "External", "rel_get mode");
	EQ (dk_rel_get (L (g), gn, 0, DKR_MODE, L (o), sizeof o), 0, "rel_get sans mode");
	EQ (dk_rel_get (L (g), gn, 3, DKR_ID, L (o), sizeof o), DKE_NOTFOUND, "rel_get hors rang");
	EQ (dk_rel_get (L (g), gn, -1, DKR_ID, L (o), sizeof o), DKE_NOTFOUND, "rel_get rang negatif");
	EQ (dk_rel_target (L (g), gn, L ("rId3"), L (o), sizeof o), 11, "rel_target");
	STREQ (o, "../m/m1.xml", "rel_target");
	EQ (dk_rel_target (L (g), gn, L ("rId"), L (o), sizeof o), DKE_NOTFOUND, "rel_target court");
	EQ (dk_rel_target (L (g), gn, L ("rId33"), L (o), sizeof o), DKE_NOTFOUND, "rel_target long");
	EQ (dk_rel_by_type (L (g), gn, L ("slidemaster"), L (o), sizeof o), 11, "rel_by_type");
	EQ (dk_rel_by_type (L (g), gn, L ("hyperlink"), L (o), sizeof o), DKE_NOTFOUND, "rel_by_type externe");
	EQ (dk_rel_by_type (L (g), gn, L ("Master"), L (o), sizeof o), DKE_NOTFOUND, "rel_by_type partiel");

	STREQ ((char *) dk_env_text (DKS_RELS_CLOSE), "</Relationships>", "env");
	EQ (dk_env_text (DKS_COUNT), 0, "env hors rang");
	dk_rel_write (3, L ("slide"), L ("slides/s&1.xml"), 0, L (o), sizeof o);
	STREQ (o, "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" Target=\"slides/s&amp;1.xml\"/>", "rel_write");
	dk_rel_write (12, L ("urn:x"), L ("http://a"), 1, L (o), sizeof o);
	STREQ (o, "<Relationship Id=\"rId12\" Type=\"urn:x\" Target=\"http://a\" TargetMode=\"External\"/>", "rel_write externe");
	dk_opc_default (L ("png"), L ("image/png"), L (o), sizeof o);
	STREQ (o, "<Default Extension=\"png\" ContentType=\"image/png\"/>", "opc_default");
	dk_opc_override (L ("xl/a.xml"), L ("t/x"), L (o), sizeof o);
	STREQ (o, "<Override PartName=\"/xl/a.xml\" ContentType=\"t/x\"/>", "opc_override");
	dk_opc_override (L ("/xl/a.xml"), L ("t/x"), L (o), sizeof o);
	STREQ (o, "<Override PartName=\"/xl/a.xml\" ContentType=\"t/x\"/>", "opc_override /");
	dk_odf_entry (L ("/"), L ("t/x"), L (o), sizeof o);
	STREQ (o, "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"t/x\"/>", "odf racine");
	dk_odf_entry (L ("content.xml"), L ("text/xml"), L (o), sizeof o);
	STREQ (o, "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>", "odf");
}

/* ---- sorties bornees : aucune fonction n'ecrit au-dela de cap ---------------------------- */
static void t_bounds (void)
{
	char o[64];
	long r = dk_xml_open (L ("<a b=\"x&amp;yyyyy\">zz&#x20AC;zzz</a>"), 36);
	dk_xml_next (r);
	for (long cap = 0; cap < 12; cap++)
	{
		memset (o, 'Z', sizeof o);
		EQ (dk_xml_attr (r, L ("b"), L (o), cap), 7, "attr cap");
		CHECK (o[cap] == 'Z', "attr au-dela");
		if (cap) CHECK (strlen (o) == (cap - 1 < 7 ? cap - 1 : 7), "attr 0 final");
		memset (o, 'Z', sizeof o);
		dk_xml_name (r, L (o), cap);
		CHECK (o[cap ? 1 : 0] == (cap == 1 ? 'Z' : (cap ? 0 : 'Z')) || cap > 1, "name cap");
	}
	dk_xml_next (r);
	for (long cap = 0; cap < 12; cap++)
	{
		memset (o, 'Z', sizeof o);
		EQ (dk_xml_text (r, L (o), cap), 8, "text cap");
		CHECK (o[cap] == 'Z', "text au-dela");
	}
	dk_xml_close (r);
}

/* ---- empreinte d'un flux (la meme dans dkref.cpp) ---------------------------------------- */
static unsigned long g_h;
static void hb (const void *p, long n) { const unsigned char *b = p; for (long i = 0; i < n; i++) { g_h ^= b[i]; g_h *= 1099511628211UL; } }
static void hl (long v) { hb (&v, 8); }
static const char *const QA[] = { "val", "id", "r:id", "w:val", "name", "a:b", "xmlns", "b" };
static const char *const QN[] = { "p", "r", "t", "a" };
static char *g_tmp;
#define TMP_CAP (1024 * 1024)

static unsigned long stream_hash (const char *d, long n)
{
	g_h = 14695981039346656037UL;
	long r = dk_xml_open (L (d), n), e, steps = 0;
	if (r < 0) return 0;
	while ((e = dk_xml_next (r)) != DKX_EOF)
	{
		if (++steps > n + 16) { bad ("flux sans fin", 0, n, steps); break; }
		hl (e);
		if (e == DKX_TEXT)
		{
			long l = dk_xml_text (r, L (g_tmp), TMP_CAP);
			hl (l); if (l < TMP_CAP) hb (g_tmp, l);
			continue;
		}
		long l = dk_xml_name (r, L (g_tmp), TMP_CAP);
		hl (l); hb (g_tmp, l);
		for (int i = 0; i < 4; i++) hl (dk_xml_is (r, L (QN[i])));
		if (e != DKX_START) continue;
		hl (dk_xml_empty (r));
		for (int i = 0; i < 8; i++)
		{
			l = dk_xml_attr (r, L (QA[i]), L (g_tmp), TMP_CAP);
			hl (l < 0 ? -1 : l); if (l > 0 && l < TMP_CAP) hb (g_tmp, l);
		}
		hl (dk_xml_attr_int (r, L ("val"), -7));
		hl (dk_xml_attr_bool (r, L ("val"), 1));
	}
	dk_xml_close (r);
	return g_h;
}

/* ---- XML reel contre expat ---------------------------------------------------------------- */
typedef struct { const char *p, *e; int kind; const char *a, *b; long al, bl; } Ref;
static unsigned get32 (const char *p) { unsigned v; memcpy (&v, p, 4); return v; }
static int ref_next (Ref *r)
{
	if (r->p >= r->e) { r->kind = 0; return 0; }
	r->kind = *r->p++;
	r->al = get32 (r->p); r->p += 4; r->a = r->p; r->p += r->al;
	r->bl = get32 (r->p); r->p += 4; r->b = r->p; r->p += r->bl;
	return r->kind;
}
static void crlf (char *t, long *n)		/* comme expat : CRLF et CR -> LF */
{
	long j = 0;
	for (long i = 0; i < *n; i++)
	{
		if (t[i] == '\r') { t[j++] = '\n'; if (i + 1 < *n && t[i + 1] == '\n') i++; }
		else t[j++] = t[i];
	}
	*n = j;
}
static void norm_attr (char *t, long n) { for (long i = 0; i < n; i++) if (t[i] == '\t' || t[i] == '\n' || t[i] == '\r') t[i] = ' '; }

static void t_real (const char *file)
{
	char path[160], o[32];
	snprintf (path, sizeof path, DIR "%s", file);
	long n, rn;
	char *d = load (path, &n);
	snprintf (path, sizeof path, DIR "%s.ref", file);
	char *rf = load (path, &rn);
	if (!d || !rf) { bad ("vecteur absent", file, 0, 0); return; }
	char *g = guarded (d, n);
	char *tb = malloc (n + 16), *buf = malloc (TMP_CAP);
	long tn = 0, depth = 0, ev = 0, attrs = 0, e, f0 = g_fail;
	long r = dk_xml_open (L (g), n);
	Ref R; R.p = rf; R.e = rf + rn;
	ref_next (&R);
#define WANT(k, what) do { g_checks++; if (R.kind != (k)) { bad (what, file, R.kind, ev); goto out; } } while (0)
	while ((e = dk_xml_next (r)) != DKX_EOF)
	{
		ev++;
		if (e == DKX_TEXT)
		{
			long l = dk_xml_text (r, 0, 0);
			if (l >= n + 16 - tn) { bad ("texte long", file, l, ev); goto out; }
			if (dk_xml_text (r, L (tb + tn), n + 16 - tn) != l) bad ("text longueur", file, l, ev);
			if (depth) tn += l;
			continue;
		}
		if (tn)
		{
			crlf (tb, &tn);
			if (tn)
			{
				WANT ('T', "attendu : texte");
				g_checks++;
				if (tn != R.al || memcmp (tb, R.a, tn)) { bad ("texte different", file, tn, ev); goto out; }
				ref_next (&R);
			}
			tn = 0;
		}
		long l = dk_xml_name (r, L (buf), TMP_CAP);
		if (e == DKX_START)
		{
			WANT ('S', "attendu : START");
			g_checks++;
			if (l != R.al || memcmp (buf, R.a, l)) { bad ("nom different", buf, l, ev); goto out; }
			memcpy (buf, R.a, l); buf[l] = 0;
			char *c = strchr (buf, ':');
			if (!dk_xml_is (r, L (c ? c + 1 : buf))) bad ("is", buf, l, ev);
			if (!dk_xml_isq (r, L (buf))) bad ("isq", buf, l, ev);
			ref_next (&R);
			while (R.kind == 'A')
			{
				char nm[128]; long k = R.al < 127 ? R.al : 127;
				memcpy (nm, R.a, k); nm[k] = 0;
				long v = dk_xml_attr (r, L (nm), L (buf), TMP_CAP);
				attrs++;
				g_checks++;
				if (v < 0) { bad ("attribut absent", nm, v, ev); goto out; }
				norm_attr (buf, v);
				if (v != R.bl || memcmp (buf, R.b, v)) { bad ("attribut different", nm, v, ev); goto out; }
				ref_next (&R);
			}
			depth++;
		}
		else
		{
			WANT ('E', "attendu : END");
			g_checks++;
			if (l != R.al || memcmp (buf, R.a, l)) { bad ("END different", buf, l, ev); goto out; }
			ref_next (&R);
			depth--;
		}
	}
	WANT (0, "fin de la reference");
out:
	dk_xml_close (r);
	snprintf (o, sizeof o, "%ld", attrs);
	say ("  %s : %ld evenements", file, ev, 0);
	say (", %s attributs, ", o, 0, 0);
	say ("%s%ld echec(s)\n", "", g_fail - f0, 0);

	/* tronque en 200 points : pas de faute, le flux finit */
	long step = n / 200 + 1;
	for (long cut = 0; cut < n; cut += step)
	{
		char *t = guarded (d, cut);
		stream_hash (t, cut);
	}
	free (d); free (rf); free (tb); free (buf);
}

/* ---- mal forme : contre le lecteur C++ d'Onyx ---------------------------------------------- */
static void t_fuzz (void)
{
	long n, rn;
	char *d = load (DIR "fuzz.bin", &n), *rf = load (DIR "fuzz.ref", &rn);
	if (!d || !rf) { bad ("fuzz absent", 0, 0, 0); return; }
	const char *p = d, *e = d + n, *rp = rf;
	long k = 0, diff = 0;
	while (p < e)
	{
		unsigned l = get32 (p); p += 4;
		char *g = guarded (p, l);
		unsigned long want, h = stream_hash (g, l);
		memcpy (&want, rp, 8); rp += 8;
		g_checks++;
		if (h != want) { if (diff++ < 12) bad ("fuzz different", 0, k, l); else g_fail++; }
		p += l; k++;
	}
	say ("  fuzz : %s%ld cas, ", "", k, 0);
	say ("%s%ld differents\n", "", diff, 0);
	free (d); free (rf);
}

/* ---- nombres calcules par Python ------------------------------------------------------------- */
static int hexv (char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }
static long unhex (const char *s, char *o) { long n = 0; while (s[0] > ' ' && s[1] > ' ') { o[n++] = (char) (hexv (s[0]) << 4 | hexv (s[1])); s += 2; } return n; }
static void t_num (void)
{
	long n;
	char *d = load (DIR "num.txt", &n);
	if (!d) { bad ("num.txt absent", 0, 0, 0); return; }
	long lines = 0, rn = 0;
	char *rels = 0;
	for (char *s = strtok (d, "\n"); s; s = strtok (0, "\n"))
	{
		long a, b, c, e, f, g; int y, m, dd; char t[300], u[300], v[300], w[300], x[300];
		lines++;
		switch (s[0])
		{
		case 'D':	/* y m d jours semaine */
			sscanf (s + 2, "%ld %ld %ld %ld %ld", &a, &b, &c, &e, &f);
			EQ (dk_days_from_civil (a, b, c), e, "days_from_civil");
			EQ (dk_weekday (e), f, "weekday");
			dk_civil_from_days (e, L (&y), L (&m), L (&dd));
			if (y != a || m != b || dd != c) bad ("civil_from_days", 0, e, y * 10000 + m * 100 + dd);
			g_checks++;
			break;
		case 'X':	/* y m d serie */
			sscanf (s + 2, "%ld %ld %ld %ld", &a, &b, &c, &e);
			EQ (dk_excel_from_civil (a, b, c, 0), e, "excel_from_civil");
			EQ (dk_excel_from_civil (a, b, c, 1), e - 1462, "excel_from_civil 1904");
			dk_civil_from_excel (e, 0, L (&y), L (&m), L (&dd));
			if (y != a || m != b || dd != c) bad ("civil_from_excel", 0, e, y * 10000 + m * 100 + dd);
			g_checks++;
			break;
		case 'L':	/* texte unite nu defaut attendu */
			sscanf (s + 2, "%s %ld %ld %ld %ld", t, &a, &b, &c, &e);
			{ long v = dk_len_parse (L (t), a, b, c); g_checks++; if (v != e) bad ("len_parse", t, v, e); }
			break;
		case 'T':	/* valeur de vers attendu */
			sscanf (s + 2, "%ld %ld %ld %ld", &a, &b, &c, &e);
			EQ (dk_len_conv (a, b, c), e, "len_conv");
			break;
		case 'W':	/* valeur unite unite-ecrite decimales attendu */
			sscanf (s + 2, "%ld %ld %ld %ld %s", &a, &b, &c, &e, t);
			f = dk_len_write (a, b, c, e, L (u), sizeof u);
			EQ (f, strlen (t), "len_write");
			STREQ (u, t, "len_write");
			break;
		case 'U':	/* hexa valide points */
			sscanf (s + 2, "%s %ld %ld", u, &a, &b);
			f = unhex (u, t);
			EQ (dk_utf8_valid (L (guarded (t, f)), f), a, "utf8_valid");
			EQ (dk_utf8_count (L (guarded (t, f)), f), b, "utf8_count");
			break;
		case 'P':	/* octet point-de-code */
			sscanf (s + 2, "%ld %ld", &a, &b);
			EQ (dk_cp1252_cp (a), b, "cp1252_cp");
			EQ (dk_cp_cp1252 (b), a, "cp_cp1252");
			break;
		case 'K':	/* fichier entier, genre */
			sscanf (s + 2, "%s %ld", u, &a);
			snprintf (t, sizeof t, DIR "%s", u);
			{
				char *z = load (t, &f);
				if (!z) { bad ("fichier absent", t, 0, 0); break; }
				EQ (dk_doc_kind (L (guarded (z, f)), f), a, "doc_kind reel");
				say ("  %s : genre %ld\n", u, dk_doc_kind (L (guarded (z, f)), f), 0);
				free (z);
			}
			break;
		case 'R':	/* .rels, relations */
			sscanf (s + 2, "%s %ld", u, &a);
			snprintf (t, sizeof t, DIR "%s", u);
			free (rels);
			rels = load (t, &rn);
			if (!rels) { bad ("rels absent", t, 0, 0); break; }
			EQ (dk_rel_count (L (rels), rn), a, "rel_count reel");
			break;
		case 'r':	/* rang id type cible mode */
			sscanf (s + 2, "%ld %s %s %s %s", &a, t, u, v, w);
			dk_rel_get (L (rels), rn, a, DKR_ID, L (x), sizeof x);
			STREQ (x, t, "rel id");
			dk_rel_get (L (rels), rn, a, DKR_TYPE, L (x), sizeof x);
			STREQ (x, u, "rel type");
			dk_rel_get (L (rels), rn, a, DKR_TARGET, L (x), sizeof x);
			STREQ (x, v, "rel cible");
			dk_rel_get (L (rels), rn, a, DKR_MODE, L (x), sizeof x);
			STREQ (x[0] ? x : "-", w, "rel mode");
			g = dk_rel_target (L (rels), rn, L (t), L (x), sizeof x);
			EQ (g, strlen (v), "rel_target reel");
			break;
		case 'Q':	/* point-de-code sans octet */
			sscanf (s + 2, "%ld", &a);
			EQ (dk_cp_cp1252 (a), DKE_NOTFOUND, "cp_cp1252 absent");
			break;
		case 'C':	/* cp utf8-hexa */
			sscanf (s + 2, "%ld %s", &a, u);
			f = unhex (u, t);
			g = dk_utf8_encode (a, L (u));
			EQ (g, f, "utf8_encode");
			g_checks++;
			if (memcmp (u, t, f)) bad ("utf8_encode", 0, a, 0);
			{ unsigned cp = 0; EQ (dk_utf8_decode (L (t), f, L (&cp)), f, "utf8_decode"); EQ (cp, a, "utf8_decode"); }
			break;
		}
	}
	say ("  num.txt : %s%ld lignes\n", "", lines, 0);
	free (rels);
	free (d);
}

int main (int argc, char **argv)
{
	g_res = fopen (DIR "result.txt", "w");
	if (!documentkit_kit_open ()) { say ("documentkit absent%s\n", "", 0, 0); return 1; }
	say ("dktest : %s%d fonctions\n", "", DOCUMENTKIT_KIT_FUNCTIONS, 0);
	char *m = mmap (0, GUARD_MAX + PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED || mprotect (m + GUARD_MAX, PAGE, PROT_NONE)) { say ("pas de page de garde%s\n", "", 0, 0); return 1; }
	g_guard = m;
	if (argc > 1)		/* la garde : doit finir sur une faute */
	{
		g_guard[GUARD_MAX - 1] = 1;
		say ("garde : avant%s\n", "", 0, 0);
		say ("garde : LUE %s%ld\n", "", g_guard[GUARD_MAX], 0);
		return 2;
	}
	g_tmp = malloc (TMP_CAP);
	t_xml (); say ("xml : %s%ld echec(s)\n", "", g_fail, 0);
	t_text (); say ("texte : %s%ld\n", "", g_fail, 0);
	t_len (); say ("longueurs : %s%ld\n", "", g_fail, 0);
	t_date (); say ("dates : %s%ld\n", "", g_fail, 0);
	t_csv (); say ("csv : %s%ld\n", "", g_fail, 0);
	t_pkg (); say ("paquets : %s%ld\n", "", g_fail, 0);
	t_bounds (); say ("bornes : %s%ld\n", "", g_fail, 0);
	t_num ();
	long n;
	char *ls = load (DIR "list.txt", &n);
	if (ls) for (char *s = strtok (ls, "\n"); s; s = strtok (0, "\n")) { char *sv = strdup (s); t_real (sv); free (sv); }
	else bad ("list.txt absent", 0, 0, 0);
	t_fuzz ();
	say ("%s %ld controles, ", g_fail ? "DKTEST ECHEC :" : "DKTEST OK :", g_checks, 0);
	say ("%s%ld echecs\n", "", g_fail, 0);
	if (g_res) fclose (g_res);
	return g_fail != 0;
}
