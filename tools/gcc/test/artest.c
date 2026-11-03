/* artest.c -- l'essai d'ArchiveKit sous ARMPI (tools/gcc/test/artest.py le prepare, le lance sous
 * QEMU et verifie ses fichiers avec Python). Sur la carte :
 *   /atest/in      archives faites sur le PC : chacune listee, lue, verifiee ; rapport E <arch> <nom> <taille> <crc>
 *   /atest/tree    un dossier a mettre dans des archives
 *   /atest/out     ce qu'ARMPI ecrit, pour que Python le relise
 *   /atest/report.txt
 * Puis : vecteurs en aller-retour, fichiers tronques ou abimes (aucune faute), debits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <archivekit_abi.h>

#define L(p) ((long) (p))
static FILE *rep;
static int fails, checks;

static void say (const char *f, const char *a, long b, long c)
{
	char t[400];
	snprintf (t, sizeof t, f, a, b, c);
	fputs (t, stdout);
	fflush (stdout);
	if (rep) { fputs (t, rep); fflush (rep); }
}
#define CHECK(c, what, v) do { checks++; if (!(c)) { fails++; say ("FAIL %s : %ld (ligne %ld)\n", what, (long) (v), __LINE__); } } while (0)

static unsigned rs = 12345;
static unsigned rnd (void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

static const char *words[] = { "<w:p>", "<w:r>", "<w:t>", "</w:t>", "</w:r>", "</w:p>", "le ", "la ", "archive ", "compression ",
	"deflate ", "bureau ", "document ", "feuille ", "cellule ", "style=\"", "\" ", "12345 ", "0.25 ", "\n", "Bonjour ", "monde " };
enum { K_ZERO, K_TEXT, K_RAND, K_MIX, K_RUN, KINDS };
static void gen (unsigned char *b, long n, int kind)
{
	long i = 0;
	rs = 777 + kind;
	switch (kind)
	{
	case K_ZERO: memset (b, 0, n); break;
	case K_RAND: for (; i < n; i++) b[i] = (unsigned char) rnd (); break;
	case K_TEXT:
		while (i < n)
		{
			const char *w = words[rnd () % (sizeof words / sizeof *words)];
			while (*w && i < n) b[i++] = (unsigned char) *w++;
		}
		break;
	case K_MIX:	/* des morceaux de texte, de hasard, de zeros */
		while (i < n)
		{
			long k = 1 + rnd () % 40000, t = rnd () % 3;
			for (; k && i < n; k--, i++)
				b[i] = t == 0 ? (unsigned char) rnd () : t == 1 ? (unsigned char) "abcdefgh"[(i >> 3) & 7] : 0;
		}
		break;
	default:	/* courtes repetitions a toutes les distances */
		while (i < n)
		{
			long d = 1 + rnd () % 33000, l = 3 + rnd () % 300;
			if (d > i) { b[i++] = (unsigned char) rnd (); continue; }
			for (; l && i < n; l--, i++) b[i] = b[i - d];
		}
	}
}

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void save (const char *path, const void *p, long n)
{
	FILE *f = fopen (path, "wb");
	CHECK (f != 0, path, 0);
	if (!f) return;
	if (n) CHECK ((long) fwrite (p, 1, n, f) == n, path, n);
	fclose (f);
}
static unsigned char *load (const char *path, long *n)
{
	FILE *f = fopen (path, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END);
	*n = ftell (f);
	fseek (f, 0, SEEK_SET);
	unsigned char *b = malloc (*n + 1);
	if (b && fread (b, 1, *n, f) != (size_t) *n) { free (b); b = 0; }
	fclose (f);
	return b;
}

/* --- vecteurs ---------------------------------------------------------------------------------- */
static void one (const unsigned char *src, long n, int wrap, int level, unsigned char *cmp, unsigned char *back, long cap)
{
	long c = ar_deflate (L (src), n, L (cmp), cap, wrap, level);
	CHECK (c >= 0, "ar_deflate", c);
	if (c < 0) return;
	long s = ar_inflate_size (L (cmp), c, wrap);
	CHECK (s == n, "ar_inflate_size", s);
	long r = ar_inflate (L (cmp), c, L (back), cap, wrap == 0 ? 0 : 3);
	CHECK (r == n, "ar_inflate", r);
	CHECK (n == 0 || memcmp (src, back, n) == 0, "aller-retour", n);
	if (n > 0 && r == n)
	{
		long small = ar_inflate (L (cmp), c, L (back), n - 1, wrap);
		CHECK (small == AR_E_SPACE, "sortie trop petite", small);
	}
	CHECK (ar_deflate (L (src), n, L (cmp), 1, wrap, level) == AR_E_SPACE || c <= 1, "deflate : sortie trop petite", n);
}

static void vectors (void)
{
	static const long sizes[] = { 0, 1, 2, 3, 4, 7, 8, 9, 255, 256, 257, 258, 259, 1000, 32767, 32768, 32769, 65535, 65536,
				      65537, 131072, 200000, 1000000, 5000000 };
	long cap = ar_deflate_bound (5000000, AR_GZIP);
	unsigned char *src = malloc (5000000), *cmp = malloc (cap), *back = malloc (cap);
	say ("vecteurs%s %ld %ld\n", "", 0, 0);
	/* sommes */
	CHECK (ar_crc32 (0, L ("123456789"), 9) == 0xCBF43926, "crc32", ar_crc32 (0, L ("123456789"), 9));
	CHECK (ar_adler32 (1, L ("Wikipedia"), 9) == 0x11E60398, "adler32", ar_adler32 (1, L ("Wikipedia"), 9));
	CHECK (ar_crc32 (ar_crc32 (0, L ("1234"), 4), L ("56789"), 5) == 0xCBF43926, "crc32 en deux fois", 0);
	for (int k = 0; k < KINDS; k++)
	{
		gen (src, 5000000, k);
		for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++)
		{
			long n = sizes[i];
			for (int w = 0; w < 3; w++)
				for (int lv = 0; lv <= 9; lv++)
				{
					if (n > 200000 && lv != 1 && lv != 6 && !(lv == 9 && k == K_TEXT)) continue;
					if (n > 200000 && w && lv != 6) continue;
					one (src, n, w, lv, cmp, back, cap);
				}
		}
		long c6 = ar_deflate (L (src), 5000000, L (cmp), cap, AR_RAW, 6);
		say ("  genre %s : 5 Mo -> %ld octets (niveau 6) (%ld)\n", "", c6, k);
		if (k == K_RAND) CHECK (c6 <= 5000000 + 5000000 / 6000 + 16, "incompressible", c6);
	}
	/* les niveaux valables et les autres */
	CHECK (ar_deflate (L (src), 10, L (cmp), cap, 0, 10) == AR_E_INVAL, "niveau 10", 0);
	CHECK (ar_deflate (L (src), 10, L (cmp), cap, 3, 6) == AR_E_INVAL, "enveloppe 3", 0);
	CHECK (ar_deflate (L (src), 10, L (cmp), cap, 0, -1) > 0, "niveau -1", 0);
	/* tampons du kit */
	gen (src, 300000, K_TEXT);
	long b = ar_deflate_new (L (src), 300000, AR_ZLIB, 6);
	CHECK (b > 0, "ar_deflate_new", b);
	long t = ar_inflate_new (ar_data (b), ar_bytes (b), AR_AUTO, 0);
	CHECK (t > 0 && ar_bytes (t) == 300000 && !memcmp ((void *) ar_data (t), src, 300000), "ar_inflate_new", t);
	CHECK (((char *) ar_data (t))[300000] == 0, "0 final", 0);
	ar_free (t);
	t = ar_inflate_new (ar_data (b), ar_bytes (b), AR_ZLIB, 300000);
	CHECK (t > 0 && ar_bytes (t) == 300000, "ar_inflate_new avec taille", t);
	ar_free (t);
	/* pour Python */
	save ("/atest/out/text.bin", src, 300000);
	long cz = ar_deflate (L (src), 300000, L (cmp), cap, AR_ZLIB, 9);
	save ("/atest/out/text.zlib", cmp, cz);
	cz = ar_deflate (L (src), 300000, L (cmp), cap, AR_GZIP, 6);
	save ("/atest/out/text.gzip", cmp, cz);
	cz = ar_deflate (L (src), 300000, L (cmp), cap, AR_RAW, 1);
	save ("/atest/out/text.raw", cmp, cz);
	CHECK (ar_free (b) == 0, "ar_free", 0);
	CHECK (ar_free (0) == 0, "ar_free(0)", 0);
	CHECK (ar_free (-5) == AR_E_INVAL, "ar_free(-5)", 0);
	free (src); free (cmp); free (back);
}

/* --- ZIP en memoire ------------------------------------------------------------------------------ */
static void zipmem (void)
{
	static const char mt[] = "application/vnd.oasis.opendocument.text";
	long n = 600000;
	unsigned char *a = malloc (n), *c = malloc (n), *o = malloc (n + 1);
	gen (a, n, K_TEXT);
	gen (c, n, K_RAND);
	say ("zip en memoire%s %ld %ld\n", "", 0, 0);
	long w = ar_create_mem (AR_FMT_ZIP);
	CHECK (w > 0, "ar_create_mem", w);
	ar_set_time (w, 1790000000);
	CHECK (ar_add_mem (w, L ("mimetype"), L (mt), sizeof mt - 1, 0) == 0, "add mimetype", 0);
	CHECK (ar_add_mem (w, L ("[Content_Types].xml"), L ("<Types/>"), 8, 6) == 0, "add ct", 0);
	CHECK (ar_add_mem (w, L ("word/document.xml"), L (a), n, 6) == 0, "add doc", 0);
	CHECK (ar_add_mem (w, L ("media/random.bin"), L (c), n, 6) == 0, "add hasard", 0);
	CHECK (ar_add_mem (w, L ("vide.txt"), 0, 0, 6) == 0, "add vide", 0);
	CHECK (ar_add_mem (w, L ("caf\xC3\xA9/\xC3\xA9t\xC3\xA9.txt"), L ("x"), 1, 9) == 0, "add utf8", 0);
	CHECK (ar_add_dir (w, L ("dossier")) == 0, "add dir", 0);
	long z = ar_finish (w);
	CHECK (z > 0, "ar_finish", z);
	CHECK (ar_bytes (w) == z, "ar_bytes", z);
	unsigned char *zp = (unsigned char *) ar_data (w);
	CHECK (!memcmp (zp + 30, "mimetype", 8) && !memcmp (zp + 38, mt, sizeof mt - 1) && zp[8] == 0, "mimetype en tete", 0);
	save ("/atest/out/mem.zip", zp, z);
	long r = ar_open_mem (L (zp), z);
	CHECK (r > 0, "ar_open_mem", r);
	CHECK (ar_format (r) == AR_FMT_ZIP, "format", ar_format (r));
	CHECK (ar_count (r) == 7, "count", ar_count (r));
	long i = ar_find (r, L ("[Content_Types].xml"));
	CHECK (i == 1, "find", i);
	CHECK (ar_find (r, L ("WORD/DOCUMENT.XML")) == 2, "find sans casse", 0);
	CHECK (ar_find (r, L ("absent")) < 0, "find absent", 0);
	CHECK (ar_entry_size (r, i) == 8, "taille", 0);
	CHECK (!strcmp ((char *) ar_name (r, i), "[Content_Types].xml"), "nom", 0);
	CHECK (ar_read (r, i, L (o), 8) == 8 && !memcmp (o, "<Types/>", 8), "read", 0);
	CHECK (ar_read (r, i, L (o), 7) == AR_E_SPACE, "read trop petit", 0);
	CHECK (ar_read (r, 2, L (o), n) == n && !memcmp (o, a, n), "read doc", 0);
	CHECK (ar_read (r, 3, L (o), n) == n && !memcmp (o, c, n), "read hasard", 0);
	long inf[8];
	CHECK (ar_entry (r, 3, L (inf)) == 0, "entry", 0);
	CHECK (inf[ARE_METHOD / 8] == 0 && inf[ARE_PACKED / 8] == n, "hasard stocke", inf[ARE_PACKED / 8]);
	CHECK (ar_entry (r, 2, L (inf)) == 0 && inf[ARE_METHOD / 8] == 8 && inf[ARE_PACKED / 8] < n / 2, "doc comprime", 0);
	CHECK (inf[ARE_TIME / 8] >= 1790000000 && inf[ARE_TIME / 8] <= 1790000002, "date", inf[ARE_TIME / 8]);
	CHECK (ar_entry (r, 6, L (inf)) == 0 && inf[ARE_DIR / 8] == 1, "dossier", 0);
	CHECK (ar_read (r, 99, L (o), n) == AR_E_INVAL, "indice", 0);
	long b = ar_load (r, 2);
	CHECK (b > 0 && ar_bytes (b) == n && !memcmp ((void *) ar_data (b), a, n), "load", b);
	ar_free (b);
	for (long k = 0; k < ar_count (r); k++) CHECK (ar_test (r, k) == 0, "test", k);
	CHECK (ar_close (r) == 0, "close", 0);
	CHECK (ar_free (w) == 0, "free", 0);
	free (a); free (c); free (o);
}

/* --- sur la carte ---------------------------------------------------------------------------------- */
static void one_arc (int fmt, const char *path, const unsigned char *d, long n)
{
	long w = ar_create (L (path), fmt);
	CHECK (w > 0, path, w);
	if (w < 0) return;
	ar_set_time (w, 1790000000);
	if (fmt == AR_FMT_GZIP)
		CHECK (ar_add_file (w, 0, L ("/atest/tree/a.txt"), 6) == 0, "gz add", 0);
	else
	{
		char ln[300];
		memset (ln, 'n', 150); memcpy (ln + 150, "/", 1); memset (ln + 151, 'm', 80); strcpy (ln + 231, ".txt");
		char lln[300];
		memset (lln, 'q', 200); strcpy (lln + 200, ".txt");
		CHECK (ar_add_mem (w, L ("hello.txt"), L ("hello\n"), 6, 6) == 0, "add hello", 0);
		CHECK (ar_add_mem (w, L ("sub/data.bin"), L (d), n, -1) == 0, "add data", 0);
		CHECK (ar_add_mem (w, L (ln), L ("prefixe"), 7, 6) == 0, "add prefixe", 0);
		CHECK (ar_add_mem (w, L (lln), L ("long"), 4, 6) == 0, "add long", 0);
		CHECK (ar_add_dir (w, L ("vide")) == 0, "add dir", 0);
		long r = ar_add_file (w, L ("arbre"), L ("/atest/tree"), 6);
		CHECK (r == 0, "add arbre", r);
		r = ar_add_file (w, 0, L ("/atest/tree/a.txt"), 0);
		CHECK (r == 0, "add file", r);
	}
	long z = ar_finish (w);
	CHECK (z > 0, "finish", z);
	ar_free (w);
	/* relu ici */
	long a = ar_open (L (path));
	CHECK (a > 0, "reouvrir", a);
	if (a < 0) return;
	CHECK (ar_format (a) == fmt, "format relu", ar_format (a));
	if (fmt != AR_FMT_GZIP)
	{
		long i = ar_find (a, L ("sub/data.bin"));
		CHECK (i >= 0, "find", i);
		unsigned char *o = malloc (n);
		CHECK (ar_read (a, i, L (o), n) == n && !memcmp (o, d, n), "relu", 0);
		free (o);
		long e = ar_extract (a, i, L ("/atest/ext/x/y/data.bin"));
		CHECK (e == n, "extract", e);
		long fn;
		unsigned char *f = load ("/atest/ext/x/y/data.bin", &fn);
		CHECK (f && fn == n && !memcmp (f, d, n), "extrait", fn);
		free (f);
	}
	for (long k = 0; k < ar_count (a); k++) CHECK (ar_test (a, k) == 0, "test", k);
	char dest[64];
	snprintf (dest, sizeof dest, "/atest/ext/f%d", fmt);
	long nf = ar_extract_all (a, L (dest));
	CHECK (nf > 0, "extract_all", nf);
	say ("  %s : %ld octets, %ld fichiers extraits\n", path, z, nf);
	ar_close (a);
	CHECK (ar_probe (L (path)) == fmt, "probe", ar_probe (L (path)));
}

static void oncard (void)
{
	long n = 3000000;
	unsigned char *d = malloc (n);
	gen (d, n, K_MIX);
	say ("archives sur la carte%s %ld %ld\n", "", 0, 0);
	save ("/atest/out/data.bin", d, n);
	mkdir ("/atest/ext", 0777);
	one_arc (AR_FMT_ZIP, "/atest/out/card.zip", d, n);
	one_arc (AR_FMT_TAR, "/atest/out/card.tar", d, n);
	one_arc (AR_FMT_TGZ, "/atest/out/card.tgz", d, n);
	one_arc (AR_FMT_GZIP, "/atest/out/card.gz", d, n);
	/* un gros gz : passe par le fichier */
	free (d);
	long nf = ar_formats (0, 0);
	CHECK (nf == 4, "formats", nf);
	char fi[4][ARFI_SIZE];
	CHECK (ar_formats (L (fi), 4) == 4, "formats 2", 0);
	CHECK (!strcmp (fi[2] + ARFI_NAME, "TAR.GZ"), "nom de format", 0);
	say ("  formats : %s %ld %ld\n", fi[0] + ARFI_EXT, 0, 0);
	CHECK (ar_format_name (AR_FMT_TAR) && !strcmp ((char *) ar_format_name (AR_FMT_TAR), "TAR"), "format_name", 0);
	CHECK (ar_format_name (9) == 0, "format_name 9", 0);
	say ("  strerror : %s %ld %ld\n", (char *) ar_strerror (AR_E_CORRUPT), 0, 0);
}

/* --- les archives du PC ----------------------------------------------------------------------------- */
static void foreign (void)
{
	say ("archives du PC%s %ld %ld\n", "", 0, 0);
	DIR *d = opendir ("/atest/in");
	CHECK (d != 0, "opendir", 0);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		if (e->d_name[0] == '.') continue;
		char p[300];
		snprintf (p, sizeof p, "/atest/in/%s", e->d_name);
		long a = ar_open (L (p));
		char line[700];
		if (a < 0)
		{
			snprintf (line, sizeof line, "A %s erreur %ld\n", e->d_name, a);
			fputs (line, rep); fflush (rep);
			continue;
		}
		snprintf (line, sizeof line, "A %s %ld %ld\n", e->d_name, ar_format (a), ar_count (a));
		fputs (line, rep);
		for (long i = 0; i < ar_count (a); i++)
		{
			long inf[8];
			ar_entry (a, i, L (inf));
			long b = inf[ARE_DIR / 8] ? 0 : ar_load (a, i);
			if (b < 0)
				snprintf (line, sizeof line, "E %s %s erreur %ld\n", e->d_name, (char *) inf[ARE_NAME / 8], b);
			else
				snprintf (line, sizeof line, "E %s %s %ld %08lx %ld %ld %ld\n", e->d_name, (char *) inf[ARE_NAME / 8],
					  b ? ar_bytes (b) : 0, b ? (unsigned long) ar_crc32 (0, ar_data (b), ar_bytes (b)) : 0ul,
					  inf[ARE_TIME / 8], inf[ARE_DIR / 8], inf[ARE_METHOD / 8]);
			fputs (line, rep);
			if (b > 0) ar_free (b);
		}
		fflush (rep);
		ar_close (a);
		/* et par la memoire */
		long fn;
		unsigned char *f = load (p, &fn);
		if (f)
		{
			long m = ar_open_mem (L (f), fn);
			long cnt = m > 0 ? ar_count (m) : -1, bad = 0;
			for (long i = 0; m > 0 && i < cnt; i++) if (ar_test (m, i)) bad++;
			snprintf (line, sizeof line, "M %s %ld %ld\n", e->d_name, cnt, bad);
			fputs (line, rep);
			if (m > 0) ar_close (m);
			free (f);
		}
	}
	closedir (d);
}

/* --- fichiers abimes ------------------------------------------------------------------------------- */
static long fuzz_ok, fuzz_err;
static void try_arc (const unsigned char *b, long n, unsigned char *o, long cap)
{
	long a = ar_open_mem (L (b), n);
	if (a < 0) { fuzz_err++; return; }
	long c = ar_count (a);
	if (c > 200) c = 200;
	for (long i = 0; i < c; i++)
	{
		long r = ar_read (a, i, L (o), cap);
		if (r < 0) fuzz_err++; else fuzz_ok++;
		if (i < 3)
		{
			long inf[8];
			ar_entry (a, i, L (inf));
			if (ar_test (a, i) < 0) fuzz_err++;
			(void) ar_name (a, i);
		}
	}
	ar_close (a);
}

static void fuzz_one (const char *what, const unsigned char *b, long n, int rounds)
{
	long cap = 4000000;
	unsigned char *m = malloc (n + 64), *o = malloc (cap);
	fuzz_ok = fuzz_err = 0;
	/* tronque */
	for (int i = 0; i < 200; i++)
	{
		long k = i < 100 ? (long) (rnd () % (n ? n : 1)) : n - 1 - (i - 100) * (n > 3000 ? 7 : 1);
		if (k < 0) k = 0;
		memcpy (m, b, k);
		try_arc (m, k, o, cap);
	}
	/* abime */
	for (int i = 0; i < rounds; i++)
	{
		memcpy (m, b, n);
		int flips = 1 + rnd () % 8;
		for (int f = 0; f < flips; f++)
		{
			long at = (rnd () & 1) ? (long) (rnd () % (n ? n : 1)) : n - 1 - (long) (rnd () % (n < 4000 ? n : 4000));
			m[at] = rnd () % 4 == 0 ? 0xFF : (unsigned char) rnd ();
		}
		try_arc (m, n, o, cap);
	}
	say ("  %s : %ld lectures bonnes, %ld erreurs rendues\n", what, fuzz_ok, fuzz_err);
	free (m); free (o);
}

static void fuzz_inflate (void)
{
	long n = 200000, cap = ar_deflate_bound (n, AR_GZIP);
	unsigned char *a = malloc (n), *c = malloc (cap), *m = malloc (cap), *o = malloc (n + 16);
	long good = 0, bad = 0;
	gen (a, n, K_MIX);
	for (int w = 0; w < 3; w++)
	{
		long z = ar_deflate (L (a), n, L (c), cap, w, 6);
		for (int i = 0; i < 1500; i++)
		{
			memcpy (m, c, z);
			long k = z;
			if (i % 3 == 0) k = rnd () % z;
			else for (int f = 1 + rnd () % 6; f; f--) m[rnd () % z] = (unsigned char) rnd ();
			long r = ar_inflate (L (m), k, L (o), n + 16, w);
			if (r < 0) bad++; else good++;
			r = ar_inflate_size (L (m), k, w);
			if (r < 0) bad++; else good++;
		}
	}
	/* du hasard pur */
	for (int i = 0; i < 3000; i++)
	{
		long k = 1 + rnd () % 3000;
		for (long j = 0; j < k; j++) m[j] = (unsigned char) rnd ();
		long r = ar_inflate (L (m), k, L (o), n + 16, 0);
		if (r < 0) bad++; else good++;
	}
	say ("  inflate abime%s : %ld passes, %ld erreurs\n", "", good, bad);
	free (a); free (c); free (m); free (o);
}

static void fuzz (void)
{
	say ("fichiers tronques ou abimes%s %ld %ld\n", "", 0, 0);
	fuzz_inflate ();
	static const char *files[] = { "/atest/out/mem.zip", "/atest/out/card.tar", "/atest/in/python.zip", "/atest/in/z64.zip",
				       "/atest/in/gnu.tar", "/atest/in/pax.tar", "/atest/in/python.tgz", "/atest/in/python.gz", 0 };
	for (int i = 0; files[i]; i++)
	{
		long n;
		unsigned char *f = load (files[i], &n);
		if (!f) { say ("  %s : absent %ld %ld\n", files[i], 0, 0); continue; }
		if (n > 300000)
		{	/* (assez pour tout parcourir vite) */
			long a = ar_open_mem (L (f), n);
			CHECK (a > 0, files[i], a);
			if (a > 0) ar_close (a);
			fuzz_one (files[i], f, n, 60);
		}
		else fuzz_one (files[i], f, n, 400);
		free (f);
	}
}

/* les memes fichiers abimes, ouverts sur la carte : l'entree passe par if_pull */
static void fuzz_file (void)
{
	static const char *files[] = { "/atest/out/mem.zip", "/atest/in/python.zip", "/atest/in/python.tgz", "/atest/in/python.gz", "/atest/in/gnu.tar", 0 };
	long good = 0, bad = 0;
	for (int i = 0; files[i]; i++)
	{
		long n;
		unsigned char *f = load (files[i], &n);
		if (!f) continue;
		unsigned char *o = malloc (2000000);
		for (int r = 0; r < 12; r++)
		{
			long k = n;
			if (r & 1) k = rnd () % n;
			else for (int j = 1 + rnd () % 8; j; j--) f[rnd () % n] ^= (unsigned char) (1 + rnd () % 255);
			save ("/atest/fz.bin", f, k);
			long a = ar_open (L ("/atest/fz.bin"));
			if (a < 0) { bad++; continue; }
			for (long e = 0; e < ar_count (a) && e < 20; e++)
			{
				if (ar_read (a, e, L (o), 2000000) < 0) bad++; else good++;
				if (ar_extract (a, e, L ("/atest/fz.out")) < 0) bad++; else good++;
			}
			ar_close (a);
		}
		free (f); free (o);
	}
	say ("  sur la carte%s : %ld bonnes, %ld erreurs\n", "", good, bad);
}

/* rien ne doit rester : 64 Gio d'adresses, un etat de deflate ou un bloc de noms perdu a chaque tour se verrait */
static void leaks (void)
{
	long n;
	unsigned char *f = load ("/atest/out/mem.zip", &n);
	CHECK (f != 0, "mem.zip", 0);
	if (!f) return;
	say ("fuites%s %ld %ld\n", "", 0, 0);
	for (int i = 0; i < 70000; i++)
	{
		long a = ar_open_mem (L (f), n);
		if (a < 0) { CHECK (0, "ar_open_mem en boucle", i); break; }
		ar_close (a);
		a = ar_open_mem (L (f), n - 3);		/* une fois sur deux en erreur */
		if (a > 0) ar_close (a);
	}
	for (int i = 0; i < 100000; i++)
	{
		long b = ar_deflate_new (L ("abcabcabcabc"), 12, AR_GZIP, 6);
		if (b < 0) { CHECK (0, "ar_deflate_new en boucle", i); break; }
		long w = ar_create_mem (AR_FMT_ZIP);
		ar_add_mem (w, L ("a"), ar_data (b), ar_bytes (b), 6);
		ar_finish (w);
		long c = ar_inflate_new (ar_data (b), ar_bytes (b), AR_AUTO, 0);
		if (c < 0) { CHECK (0, "ar_inflate_new en boucle", i); break; }
		ar_free (c);
		ar_free (w);
		ar_free (b);
	}
	free (f);
}

/* --- debits ---------------------------------------------------------------------------------------- */
static void speed (void)
{
	long n = 16000000, cap = ar_deflate_bound (n, AR_RAW);
	unsigned char *a = malloc (n), *c = malloc (cap), *o = malloc (n);
	say ("debits (QEMU)%s %ld %ld\n", "", 0, 0);
	static const int kinds[] = { K_TEXT, K_MIX, K_RAND };
	static const char *names[] = { "texte", "melange", "hasard" };
	for (int k = 0; k < 3; k++)
	{
		gen (a, n, kinds[k]);
		double t = now ();
		long crc = ar_crc32 (0, L (a), n);
		double t2 = now ();
		long ad = ar_adler32 (1, L (a), n);
		double t3 = now ();
		char line[200];
		snprintf (line, sizeof line, "  %s : crc %.0f Mo/s, adler %.0f Mo/s (%lx %lx)\n", names[k], n / 1e6 / (t2 - t), n / 1e6 / (t3 - t2), crc, ad);
		say ("%s", line, 0, 0);
		for (int lv = 0; lv <= 9; lv += (lv == 0 ? 1 : lv == 1 ? 5 : 3))
		{
			t = now ();
			long z = ar_deflate (L (a), n, L (c), cap, AR_RAW, lv);
			t2 = now ();
			long r = ar_inflate (L (c), z, L (o), n, AR_RAW);
			t3 = now ();
			CHECK (r == n && !memcmp (a, o, n), "debit", r);
			snprintf (line, sizeof line, "  %s niveau %d : %ld -> %ld (%.1f %%), deflate %.2f Mo/s, inflate %.1f Mo/s\n", names[k], lv, n, z, 100.0 * z / n, n / 1e6 / (t2 - t), n / 1e6 / (t3 - t2));
			say ("%s", line, 0, 0);
		}
	}
	free (a); free (c); free (o);
}

int main (void)
{
	mkdir ("/atest", 0777);
	mkdir ("/atest/out", 0777);
	rep = fopen ("/atest/report.txt", "w");
	if (!archivekit_kit_open ())
	{
		say ("kit absent%s %ld %ld\n", "", 0, 0);
		return 1;
	}
	say ("artest : debut%s %ld %ld\n", "", 0, 0);
	vectors ();
	zipmem ();
	oncard ();
	foreign ();
	fuzz ();
	fuzz_file ();
	leaks ();
	speed ();
	say ("artest : termine, %s%ld verifications, %ld echecs\n", "", checks, fails);
	fclose (rep);
	return fails != 0;
}
