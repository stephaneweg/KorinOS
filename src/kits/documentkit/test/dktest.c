/* dktest : essai différentiel de DocumentKit (src/kits/documentkit), sans
 * bibliothèque, pour AArch64 (Korin sur le Pi) et x86-64 (Korin sur PC).
 *
 * Appelle chaque fonction du kit avec des entrées variées et n'écrit que ce
 * qui ne dépend ni des adresses ni de l'heure : résultats, octets sortis
 * (échappés), sommes FNV-1a des grosses sorties. Le kit en assembleur et le
 * kit en C doivent donner des sorties identiques, octet pour octet :
 *     dktest > /media/sd1/dk-asm.txt   (puis avec le kit en C, puis sur le PC)
 * Contrôles propres (règle de sortie à la snprintf, rien d'écrit au-delà de
 * cap, allers-retours des dates) : « ECHEC ... » et code de sortie = nombre
 * d'échecs.
 *
 * Construction : src/kits/documentkit/test/build.sh (dans WSL). */
#include <stddef.h>
#include <stdint.h>

/* src/inc/syscalls.inc */
#define SYS_EXIT 0x10
#define SYS_PRINT 0x60
#define SYS_LIB_OPEN 0x64

#define KIT_MAGIC 0x54494B41
#define AK_F_PRINT 25    /* AppKit : f_print (version 26) */
#define AK_F_KIT_LOAD 31 /* AppKit : f_kit_load (version 32) */

/* ---- appels système (comme src/kits/common/korin_kit.h) */

#if defined(__aarch64__)
static inline long sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
	return x0;
}
#elif defined(__x86_64__)
static inline long sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	register long r9 __asm__("r9") = f;
	long ret;
	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
			 : "rcx", "r11", "memory");
	return ret;
}
#else
#error "architecture non prise en charge"
#endif
#define sys2(n, a, b) sys6((n), (long)(a), (long)(b), 0, 0, 0, 0)

/* le compilateur peut les appeler lui-même */
void *memset(void *d, int c, size_t n)
{
	volatile unsigned char *a = d;
	while (n--)
		*a++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
	volatile unsigned char *a = d;
	const unsigned char *b = s;
	while (n--)
		*a++ = *b++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	if (a < b)
		while (n--)
			*a++ = *b++;
	else
		while (n--)
			a[n] = b[n];
	return d;
}

int memcmp(const void *x, const void *y, size_t n)
{
	const unsigned char *a = x, *b = y;
	for (; n; n--, a++, b++)
		if (*a != *b)
			return *a - *b;
	return 0;
}

static long slen(const char *s)
{
	long n = 0;
	while (s[n])
		n++;
	return n;
}

/* ---- sortie : AppKit (sortie standard, se redirige) s'il est là, sinon la
 * console du noyau */

typedef long (*fn_print)(const char *, long);
static fn_print f_print;
static char pb[4096];
static long pn;

static void flush(void)
{
	if (!pn)
		return;
	if (!f_print || f_print(pb, pn) < 0)
		sys2(SYS_PRINT, pb, pn);
	pn = 0;
}

static void pc(char c)
{
	if (pn == (long)sizeof pb)
		flush();
	pb[pn++] = c;
}

static void ps(const char *s)
{
	while (*s)
		pc(*s++);
}

static void pu(uint64_t v)
{
	char b[24];
	int i = 0;
	do
		b[i++] = (char)('0' + v % 10);
	while (v /= 10);
	while (i)
		pc(b[--i]);
}

static void pd(long v)
{
	if (v < 0) {
		pc('-');
		pu(-(uint64_t)v);
	} else {
		pu((uint64_t)v);
	}
}

static void px(uint64_t v, int digits)
{
	for (int i = digits - 1; i >= 0; i--)
		pc("0123456789abcdef"[(v >> (i * 4)) & 15]);
}

/* octets entre guillemets, échappés */
static void pq(const void *p, long n)
{
	const unsigned char *s = p;
	pc('"');
	for (long i = 0; i < n; i++) {
		unsigned c = s[i];
		if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') {
			pc((char)c);
		} else {
			pc('\\');
			pc('x');
			px(c, 2);
		}
	}
	pc('"');
}

static void nl(void)
{
	pc('\n');
}

static uint32_t fnv(const void *p, long n)
{
	const unsigned char *s = p;
	uint32_t h = 2166136261u;
	for (long i = 0; i < n; i++) {
		h ^= s[i];
		h *= 16777619u;
	}
	return h;
}

static uint32_t fnv_mix(uint32_t h, uint64_t v)
{
	for (int i = 0; i < 8; i++) {
		h ^= (uint32_t)(v >> (i * 8)) & 0xFF;
		h *= 16777619u;
	}
	return h;
}

static long fails, checks, calls;

static void ck(int ok, const char *what)
{
	checks++;
	if (ok)
		return;
	fails++;
	ps("ECHEC ");
	ps(what);
	nl();
}

/* ---- le kit */

enum {
	DK_XML_OPEN, DK_XML_CLOSE, DK_XML_NEXT, DK_XML_EVENT, DK_XML_EMPTY, DK_XML_IS, DK_XML_ISQ,
	DK_XML_NAME, DK_XML_ATTR, DK_XML_ATTR_INT, DK_XML_ATTR_BOOL, DK_XML_ATTR_ON, DK_XML_TEXT,
	DK_XML_SKIP, DK_XML_POS, DK_XML_ESC, DK_XML_ESC_ATTR,
	DK_UTF8_ENCODE, DK_UTF8_DECODE, DK_UTF8_VALID, DK_UTF8_COUNT, DK_UTF8_TO_CP, DK_CP_TO_UTF8,
	DK_LATIN1_TO_UTF8, DK_CP1252_TO_UTF8, DK_UTF8_TO_LATIN1, DK_UTF8_TO_CP1252, DK_CP1252_CP,
	DK_CP_CP1252,
	DK_LEN_PARSE, DK_LEN_CONV, DK_LEN_WRITE, DK_COLOR_PARSE, DK_COLOR_WRITE,
	DK_DAYS_FROM_CIVIL, DK_CIVIL_FROM_DAYS, DK_WEEKDAY, DK_EXCEL_FROM_CIVIL, DK_CIVIL_FROM_EXCEL,
	DK_CSV_OPEN, DK_CSV_CLOSE, DK_CSV_NEXT, DK_CSV_TEXT, DK_CSV_SEP, DK_CSV_ESC,
	DK_DOC_KIND, DK_DOC_KIND_MIME, DK_DOC_MIME, DK_PART_PATH, DK_RELS_PATH, DK_REL_COUNT,
	DK_REL_GET, DK_REL_TARGET, DK_REL_BY_TYPE, DK_ENV_TEXT, DK_REL_WRITE, DK_OPC_DEFAULT,
	DK_OPC_OVERRIDE, DK_ODF_ENTRY, DK_HASH,
	DK_COUNT
};

static const char *const fn_names[DK_COUNT] = {
	"dk_xml_open", "dk_xml_close", "dk_xml_next", "dk_xml_event", "dk_xml_empty", "dk_xml_is",
	"dk_xml_isq", "dk_xml_name", "dk_xml_attr", "dk_xml_attr_int", "dk_xml_attr_bool",
	"dk_xml_attr_on", "dk_xml_text", "dk_xml_skip", "dk_xml_pos", "dk_xml_esc", "dk_xml_esc_attr",
	"dk_utf8_encode", "dk_utf8_decode", "dk_utf8_valid", "dk_utf8_count", "dk_utf8_to_cp",
	"dk_cp_to_utf8", "dk_latin1_to_utf8", "dk_cp1252_to_utf8", "dk_utf8_to_latin1",
	"dk_utf8_to_cp1252", "dk_cp1252_cp", "dk_cp_cp1252", "dk_len_parse", "dk_len_conv",
	"dk_len_write", "dk_color_parse", "dk_color_write", "dk_days_from_civil", "dk_civil_from_days",
	"dk_weekday", "dk_excel_from_civil", "dk_civil_from_excel", "dk_csv_open", "dk_csv_close",
	"dk_csv_next", "dk_csv_text", "dk_csv_sep", "dk_csv_esc", "dk_doc_kind", "dk_doc_kind_mime",
	"dk_doc_mime", "dk_part_path", "dk_rels_path", "dk_rel_count", "dk_rel_get", "dk_rel_target",
	"dk_rel_by_type", "dk_env_text", "dk_rel_write", "dk_opc_default", "dk_opc_override",
	"dk_odf_entry", "dk_hash",
};

typedef long (*fn6)(long, long, long, long, long, long);
static long kit;
static uint32_t kit_count;
static long called[DK_COUNT];

static long call(int i, long a, long b, long c, long d, long e, long f)
{
	const uint32_t *t = (const uint32_t *)kit;
	fn6 fn = (fn6)(kit + t[2 + i]);
	calls++;
	called[i]++;
	return fn(a, b, c, d, e, f);
}
#define L(x) ((long)(x))
#define C1(i, a) call(i, L(a), 0, 0, 0, 0, 0)
#define C2(i, a, b) call(i, L(a), L(b), 0, 0, 0, 0)
#define C3(i, a, b, c) call(i, L(a), L(b), L(c), 0, 0, 0)
#define C4(i, a, b, c, d) call(i, L(a), L(b), L(c), L(d), 0, 0)
#define C5(i, a, b, c, d, e) call(i, L(a), L(b), L(c), L(d), L(e), 0)
#define C6(i, a, b, c, d, e, f) call(i, L(a), L(b), L(c), L(d), L(e), L(f))

static long open_kit(const char *name, long min)
{
	long t = sys2(SYS_LIB_OPEN, name, min);
	if (t == -2) { /* pas encore en mémoire : AppKit le lit (comme kits_open) */
		long ak = sys2(SYS_LIB_OPEN, "appkit", 32);
		if (ak >= 0) {
			const uint32_t *at = (const uint32_t *)ak;
			long (*load)(const char *) = (long (*)(const char *))(ak + at[2 + AK_F_KIT_LOAD]);
			if (load(name) == 0)
				t = sys2(SYS_LIB_OPEN, name, min);
		}
	}
	return t;
}

/* ---- sortie à la snprintf : appel complet, puis petites places */

#define BIG 4096
#define GUARD 32
static unsigned char ob[BIG + GUARD];
static unsigned char ref[BIG + GUARD];

static void fill(void)
{
	memset(ob, 0xAA, sizeof ob);
}

static int intact(long from, long to)
{
	for (long i = from; i < to; i++)
		if (ob[i] != 0xAA)
			return 0;
	return 1;
}

/* fi, arguments (out en a[op], cap en a[op + 1]) -> imprime le résultat et la
 * sortie ; contrôle les places 0, 1, 2, 7 et out = 0 */
static long sinkt(const char *label, int fi, long a0, long a1, long a2, long a3, long a4, long a5, int op)
{
	long a[6] = {a0, a1, a2, a3, a4, a5};
	fill();
	a[op] = L(ob);
	a[op + 1] = BIG;
	long r = call(fi, a[0], a[1], a[2], a[3], a[4], a[5]);
	ps(label);
	ps(" = ");
	pd(r);
	if (r >= 0) {
		long n = r < BIG ? r : BIG - 1;
		pc(' ');
		if (n <= 200) {
			pq(ob, n);
		} else {
			ps("len=");
			pd(n);
			ps(" fnv=");
			px(fnv(ob, n), 8);
		}
		ck(ob[n] == 0, "sinkt : 0 final");
		ck(intact(n + 1, BIG + GUARD), "sinkt : rien après le 0");
		memcpy(ref, ob, (size_t)n + 1);
	} else {
		ck(intact(0, BIG + GUARD), "sinkt : erreur sans écriture");
	}
	static const long caps[4] = {0, 1, 2, 7};
	for (int c = 0; c < 4; c++) {
		fill();
		a[op + 1] = caps[c];
		long r2 = call(fi, a[0], a[1], a[2], a[3], a[4], a[5]);
		ck(r2 == r, "sinkt : même longueur pour toute place");
		long n = r >= 0 ? (r + 1 < caps[c] ? r + 1 : caps[c]) : 0;
		if (n > 0) {
			ck(memcmp(ob, ref, (size_t)n - 1) == 0, "sinkt : début de la sortie");
			ck(ob[n - 1] == 0, "sinkt : 0 final (place courte)");
		}
		ck(intact(n, BIG + GUARD), "sinkt : au-delà de cap");
		if (caps[c] == 7) {
			ps(" | cap7 ");
			pq(ob, n);
		}
	}
	a[op] = 0;
	a[op + 1] = 10;
	ck(call(fi, a[0], a[1], a[2], a[3], a[4], a[5]) == r, "sinkt : out = 0");
	a[op] = L(ob);
	a[op + 1] = -5;
	fill();
	ck(call(fi, a[0], a[1], a[2], a[3], a[4], a[5]) == r && intact(0, BIG + GUARD), "sinkt : cap < 0");
	nl();
	return r;
}

/* ---- XML */

/* un document : tous les événements, nom, texte, attributs */
static void xml_dump(const char *label, const char *doc, long n, const char *const *attrs, const char *isname)
{
	if (n < 0)
		n = slen(doc);
	ps("xml ");
	ps(label);
	ps(" n=");
	pd(n);
	long h = C2(DK_XML_OPEN, doc, n);
	ps(" h=");
	pd(h);
	nl();
	if (h < 0)
		return;
	ck(C1(DK_XML_EVENT, h) == 0 && C1(DK_XML_POS, h) == 0, "xml : état de départ");
	for (int k = 0; k < 300; k++) {
		long ev = C1(DK_XML_NEXT, h);
		ps("  ");
		pd(ev);
		ck(C1(DK_XML_EVENT, h) == ev, "xml : dk_xml_event");
		ps(" pos=");
		pd(C1(DK_XML_POS, h));
		ps(" e=");
		pd(C1(DK_XML_EMPTY, h));
		fill();
		long r = C3(DK_XML_NAME, h, ob, 48);
		ps(" nm=");
		pd(r);
		pq(ob, r < 47 ? r : 47);
		ck(C3(DK_XML_NAME, h, ob, 0) == r, "xml : nom, cap 0");
		if (isname) {
			ps(" is=");
			pd(C2(DK_XML_IS, h, isname));
			pd(C2(DK_XML_ISQ, h, isname));
		}
		fill();
		r = C3(DK_XML_TEXT, h, ob, BIG);
		ps(" t=");
		pd(r);
		if (r > 0) {
			if (r <= 80) {
				pq(ob, r);
			} else {
				ps(" fnv=");
				px(fnv(ob, r), 8);
			}
			fill();
			long r2 = C3(DK_XML_TEXT, h, ob, 5);
			long n5 = r + 1 < 5 ? r + 1 : 5;
			ck(r2 == r && ob[n5 - 1] == 0 && intact(n5, 64), "xml : texte, cap 5");
		}
		if (ev == 1 && attrs) {
			for (int i = 0; attrs[i]; i++) {
				fill();
				long ra = C4(DK_XML_ATTR, h, attrs[i], ob, 40);
				ps(" [");
				ps(attrs[i]);
				ps("]=");
				pd(ra);
				if (ra >= 0)
					pq(ob, ra < 39 ? ra : 39);
				ps(",");
				pd(C3(DK_XML_ATTR_INT, h, attrs[i], -777));
				ps(",");
				pd(C3(DK_XML_ATTR_BOOL, h, attrs[i], 5));
				ps(",");
				pd(C2(DK_XML_ATTR_ON, h, attrs[i]));
			}
			ps(" on0=");
			pd(C2(DK_XML_ATTR_ON, h, 0));
		}
		nl();
		if (ev <= 0)
			break;
	}
	ck(C1(DK_XML_NEXT, h) == 0, "xml : EOF reste EOF");
	ck(C1(DK_XML_CLOSE, h) == 0, "xml : close");
	ck(C1(DK_XML_CLOSE, h) == -9, "xml : close deux fois");
}

/* dk_xml_skip : avance jusqu'au START n° k, passe, montre la suite */
static void xml_skip_t(const char *label, const char *doc, int k)
{
	long h = C2(DK_XML_OPEN, doc, slen(doc));
	ps("skip ");
	ps(label);
	ps(" k=");
	pd(k);
	int seen = 0;
	long ev;
	while ((ev = C1(DK_XML_NEXT, h)) > 0)
		if (ev == 1 && ++seen == k)
			break;
	ps(" at=");
	pd(C1(DK_XML_POS, h));
	ps(" r=");
	pd(C1(DK_XML_SKIP, h));
	ps(" pos=");
	pd(C1(DK_XML_POS, h));
	ps(" ev=");
	pd(C1(DK_XML_EVENT, h));
	fill();
	long r = C3(DK_XML_NAME, h, ob, 40);
	pq(ob, r < 39 ? r : 39);
	ps(" next=");
	pd(C1(DK_XML_NEXT, h));
	ps(" skip2=");
	pd(C1(DK_XML_SKIP, h));
	ps(" pos=");
	pd(C1(DK_XML_POS, h));
	nl();
	C1(DK_XML_CLOSE, h);
}

static const char *const at_common[] = {"id", "r:id", "val", "w:val", "a", "b", "xmlns", "c", "", "x:y", 0};

static void test_xml(void)
{
	ps("== xml\n");
	xml_dump("simple", "<?xml version=\"1.0\"?>\n<a x=\"1\"><b/>text<c id='2'>t&amp;u</c></a>", -1, at_common, "b");
	xml_dump("empty", "", 0, at_common, 0);
	xml_dump("textonly", "hello world", -1, 0, 0);
	xml_dump("lt-end", "abc<", -1, at_common, "");
	xml_dump("close-end", "<a></", -1, 0, "a");
	xml_dump("open-unterm", "<a b=\"1", -1, at_common, "a");
	xml_dump("close-unterm", "<a></a", -1, 0, "a");
	xml_dump("slash-eof", "<a/", -1, at_common, "a");
	xml_dump("empty-sp", "<a  />< b/><c\t/>", -1, at_common, "a");
	xml_dump("prefix", "<w:p w:val=\"on\" r:id=\"rId3\" id=\"x\"><w:r/><text:p/></w:p>", -1, at_common, "p");
	xml_dump("prefix2", "<w:p w:val=\"7\" r:id=\"rId3\" xmlns:id=\"ns\" xmlns:val=\"q\"/>", -1, at_common, "w:p");
	xml_dump("prefix3", "<a:b:c x:y:z=\"1\" y=\"2\"/>", -1, at_common, "b:c");
	xml_dump("noprefix", "<p val=\"off\"/><p val=\"of\"/><p val=\"no\"/><p val=\"n\"/><p val=\"on\"/>"
		 "<p val=\"0\"/><p val=\"false\"/><p val=\"F\"/><p val=\"yes\"/><p val=\"\"/><p/><p val=\"T\"/>",
		 -1, at_common, "p");
	xml_dump("ints", "<i a=\" -42 \" b=\"+7\" c=\"12abc\" val=\"abc\" id=\"99999999999999999999999\"/>"
		 "<i a=\"\t\n\r-0\" b=\"--3\" c=\"-\" val=\"&#51;&#x34;\" id=\"123456789012345678901234567890123456789012\"/>",
		 -1, at_common, "i");
	xml_dump("bools", "<b a=\"1\" b=\"t\" c=\"True\" val=\"0\" id=\"\" r:id=\"x\"/>", -1, at_common, 0);
	xml_dump("quotes", "<q a='x\"y' b=\"p'q\" c=\"g>h\" val='&apos;&quot;'>after</q>", -1, at_common, "q");
	xml_dump("weird-attrs", "<w a b=\"1\" c = \"2\" d=x \"3\" =\"4\" val=\"5/>", -1, at_common, 0);
	xml_dump("noeq", "<w a \"1\" b=\"2\"/>", -1, at_common, 0);
	xml_dump("noquote", "<w a=1 b=\"2\" c=3>", -1, at_common, 0);
	xml_dump("unterm-val", "<w a=\"1 b=\"2\"", -1, at_common, 0);
	xml_dump("comments", "<!-- c1 --><a><!----><!-- -- --><!-- a->b -->x<!--unterminated", -1, 0, 0);
	xml_dump("comment-short", "<!-", -1, 0, 0);
	xml_dump("comment-short2", "<!--", -1, 0, 0);
	xml_dump("comment-short3", "<!--->", -1, 0, 0);
	xml_dump("decl", "<?pi a?><!DOCTYPE x [ <!ENTITY e \"v\"> ]><a/><?unterm", -1, 0, 0);
	xml_dump("cdata", "<a><![CDATA[x<y&amp;]]]]><![CDATA[]]><![CDATA[a]b]]></a>", -1, 0, 0);
	xml_dump("cdata-unterm", "<a><![CDATA[abcdef", -1, 0, 0);
	xml_dump("cdata-unterm2", "<![CDATA[ab", -1, 0, 0);
	xml_dump("cdata-unterm3", "<![CDATA[a", -1, 0, 0);
	xml_dump("cdata-short", "<![CDAT", -1, 0, 0);
	xml_dump("cdata-bad", "<![CDATX[a]]>b>c", -1, 0, 0);
	xml_dump("cdata-text", "<![CDATA[raw &amp;]]>&amp;<x/>", -1, 0, 0);
	xml_dump("entities", "<e>&lt;&gt;&amp;&quot;&apos;&#65;&#x41;&#X42;&#x1F600;&#0;&#xD800;&#1114111;"
		 "&#1114112;&#99999999999;&#xFFFFFFFFFF;&unknown;&noend &; &#; &#x; &#12ab; &#xg; &abcdefghij;"
		 "&abcdefghijk; &lt &am;&ampx;&quo;&quott;&aposx;&LT;&Lt;&gt;</e>", -1, 0, 0);
	xml_dump("entities-attr", "<e a=\"&lt;&#9;&#10;&#xA0;&bogus;&\" val=\"&#x31;\"/>", -1, at_common, 0);
	xml_dump("amp-end", "<e>a&", -1, 0, 0);
	xml_dump("amp-end2", "a&lt", -1, 0, 0);
	xml_dump("amp-semi-end", "a&lt;", -1, 0, 0);
	xml_dump("whitespace-names", "<a\nb=\"1\"\r\n\tc='2'></a\t><b\r/></b\n>", -1, at_common, "a");
	xml_dump("nested", "<r><a><b><c/></b><d>t</d></a><e/></r>", -1, 0, "a");
	xml_dump("gt-in-text", "a>b</x>", -1, 0, 0);
	xml_dump("lone-lt-bang", "<!", -1, 0, 0);
	xml_dump("lone-lt-q", "<?", -1, 0, 0);
	xml_dump("lone-lt", "<", -1, at_common, "");
	xml_dump("partial-len", "<a>hello</a>", 6, 0, "a");
	{
		static char big[3000];
		long n = 0;
		const char *head = "<doc><p a=\"";
		for (const char *s = head; *s; s++)
			big[n++] = *s;
		for (int i = 0; i < 300; i++)
			big[n++] = (char)('a' + i % 26);
		big[n++] = '"';
		big[n++] = '>';
		for (int i = 0; i < 2000; i++)
			big[n++] = (i % 37 == 0) ? '&' : (char)(' ' + i % 90);
		const char *tail = "</p></doc>";
		for (const char *s = tail; *s; s++)
			big[n++] = *s;
		big[n] = 0;
		static const char *const at_a[] = {"a", 0};
		xml_dump("big", big, n, at_a, "p");
	}

	xml_skip_t("nested-a", "<r><a><b><c/></b><d>t</d></a><e/></r>", 2);
	xml_skip_t("nested-c", "<r><a><b><c/></b><d>t</d></a><e/></r>", 4);
	xml_skip_t("root", "<r><a><b><c/></b><d>t</d></a><e/></r>tail", 1);
	xml_skip_t("unterm", "<r><a><b>", 2);
	xml_skip_t("none", "<r/>", 5);

	/* poignées : inconnues, d'un autre genre, toutes prises */
	ps("handles");
	static const long bad[] = {0, -1, 65, 66, 1000, (long)0x8000000000000000ull};
	for (int i = 0; i < 6; i++) {
		pc(' ');
		pd(C1(DK_XML_NEXT, bad[i]));
		pd(C1(DK_XML_CLOSE, bad[i]));
		pd(C1(DK_XML_EVENT, bad[i]));
		pd(C1(DK_XML_EMPTY, bad[i]));
		pd(C2(DK_XML_IS, bad[i], "a"));
		pd(C2(DK_XML_ISQ, bad[i], "a"));
		pd(C3(DK_XML_NAME, bad[i], ob, 8));
		pd(C4(DK_XML_ATTR, bad[i], "a", ob, 8));
		pd(C3(DK_XML_ATTR_INT, bad[i], "a", 3));
		pd(C3(DK_XML_ATTR_BOOL, bad[i], "a", 3));
		pd(C2(DK_XML_ATTR_ON, bad[i], "a"));
		pd(C3(DK_XML_TEXT, bad[i], ob, 8));
		pd(C1(DK_XML_SKIP, bad[i]));
		pd(C1(DK_XML_POS, bad[i]));
		pd(C1(DK_CSV_NEXT, bad[i]));
		pd(C1(DK_CSV_CLOSE, bad[i]));
		pd(C3(DK_CSV_TEXT, bad[i], ob, 8));
		pd(C1(DK_CSV_SEP, bad[i]));
	}
	nl();
	ps("open-neg ");
	pd(C2(DK_XML_OPEN, "x", -1));
	pc(' ');
	pd(C3(DK_CSV_OPEN, "x", -1, 0));
	nl();
	long hs[70];
	ps("slots");
	for (int i = 0; i < 66; i++) {
		hs[i] = (i & 1) ? C3(DK_CSV_OPEN, "a,b", 3, 0) : C2(DK_XML_OPEN, "<a/>", 4);
		pc(' ');
		pd(hs[i]);
	}
	nl();
	ps("kinds ");
	pd(C1(DK_XML_NEXT, hs[1])); /* un CSV pour le XML */
	pc(' ');
	pd(C1(DK_CSV_NEXT, hs[0])); /* un XML pour le CSV */
	pc(' ');
	pd(C1(DK_CSV_CLOSE, hs[0]));
	pc(' ');
	pd(C1(DK_XML_CLOSE, hs[1]));
	nl();
	ps("close");
	for (int i = 0; i < 64; i++) {
		pc(' ');
		pd((i & 1) ? C1(DK_CSV_CLOSE, hs[i]) : C1(DK_XML_CLOSE, hs[i]));
	}
	nl();
	ps("reuse ");
	long h1 = C2(DK_XML_OPEN, "<z/>", 4);
	long h2 = C3(DK_CSV_OPEN, "z", 1, 0);
	pd(h1);
	pc(' ');
	pd(h2);
	pc(' ');
	pd(C1(DK_XML_CLOSE, h1));
	pd(C1(DK_CSV_CLOSE, h2));
	nl();
}

static void test_esc(void)
{
	ps("== esc\n");
	static const char *const s[] = {"", "plain", "a&b<c>d\"e'f", "tab\there\nnl\rcr", "\x01\x02\x1f\x7f\x80\xff",
					"&amp;", "\"\"\"", 0};
	for (int i = 0; s[i]; i++) {
		sinkt("esc", DK_XML_ESC, L(s[i]), -1, 0, 0, 0, 0, 2);
		sinkt("esc_attr", DK_XML_ESC_ATTR, L(s[i]), -1, 0, 0, 0, 0, 2);
	}
	sinkt("esc n=3", DK_XML_ESC, L("a<b>c"), 3, 0, 0, 0, 0, 2);
	sinkt("esc n=0", DK_XML_ESC, L("a<b>c"), 0, 0, 0, 0, 0, 2);
	sinkt("esc_attr n=-7", DK_XML_ESC_ATTR, L("\t\n"), -7, 0, 0, 0, 0, 2);
	static char all[257]; /* 256 octets, puis un 0 : « all + 1 » est aussi une chaîne */
	for (int i = 0; i < 256; i++)
		all[i] = (char)i;
	sinkt("esc all", DK_XML_ESC, L(all), 256, 0, 0, 0, 0, 2);
	sinkt("esc_attr all", DK_XML_ESC_ATTR, L(all), 256, 0, 0, 0, 0, 2);
	sinkt("esc all strlen", DK_XML_ESC, L(all + 1), -1, 0, 0, 0, 0, 2);
}

/* ---- UTF-8 */

static void test_utf8(void)
{
	ps("== utf8\n");
	static const long cps[] = {0, 0x41, 0x7F, 0x80, 0xE9, 0x7FF, 0x800, 0xFFF, 0xD7FF, 0xD800, 0xDBFF,
				   0xDC00, 0xDFFF, 0xE000, 0xFFFD, 0xFFFF, 0x10000, 0x1F600, 0x10FFFF,
				   0x110000, 0x7FFFFFFF, -1, (long)0x100000041, (long)0xFFFFFFFF80000000ull};
	for (unsigned i = 0; i < sizeof cps / sizeof cps[0]; i++) {
		fill();
		long r = C2(DK_UTF8_ENCODE, cps[i], ob);
		ps("encode ");
		px((uint64_t)cps[i], 16);
		ps(" = ");
		pd(r);
		pc(' ');
		pq(ob, r);
		ck(intact(r, 16), "encode : sans 0 final");
		nl();
	}
	static const char *const seq[] = {"A", "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xC0\x80", "\xC1\xBF",
					  "\xC2\x80", "\xDF\xBF", "\xE0\x80\x80", "\xE0\x9F\xBF", "\xE0\xA0\x80",
					  "\xED\x9F\xBF", "\xED\xA0\x80", "\xED\xBF\xBF", "\xEE\x80\x80", "\xEF\xBF\xBF",
					  "\xF0\x80\x80\x80", "\xF0\x8F\xBF\xBF", "\xF0\x90\x80\x80", "\xF4\x8F\xBF\xBF",
					  "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xF8\x88\x80\x80\x80", "\xFF",
					  "\x80", "\xBF", "\xC3", "\xE2\x82", "\xF0\x9F\x98", "\xC3\x41", "\xE2\x41\xAC",
					  "\xF0\x9F\x41\x80", "\xC3\xC3", 0};
	for (int i = 0; seq[i]; i++) {
		long n = slen(seq[i]);
		uint32_t cp = 0xDEADBEEF;
		ps("decode ");
		pq(seq[i], n);
		ps(" = ");
		pd(C3(DK_UTF8_DECODE, seq[i], n, &cp));
		pc(' ');
		px(cp, 8);
		ps(" short=");
		pd(C3(DK_UTF8_DECODE, seq[i], n - 1, &cp));
		ps(" null=");
		pd(C3(DK_UTF8_DECODE, seq[i], n, 0));
		ps(" valid=");
		pd(C2(DK_UTF8_VALID, seq[i], n));
		ps(" count=");
		pd(C2(DK_UTF8_COUNT, seq[i], n));
		nl();
	}
	ps("decode n<=0 ");
	pd(C3(DK_UTF8_DECODE, "A", 0, 0));
	pd(C3(DK_UTF8_DECODE, "A", -1, 0));
	ps(" valid ");
	pd(C2(DK_UTF8_VALID, "\xFF", 0));
	pd(C2(DK_UTF8_VALID, "\xFF", -3));
	ps(" count ");
	pd(C2(DK_UTF8_COUNT, "abc", 0));
	pd(C2(DK_UTF8_COUNT, "abc", -1));
	nl();

	/* tous les débuts de 1 et 2 octets, et 4 suites par octet de tête */
	static const uint8_t tails[8] = {0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0};
	static const uint8_t tails2[6] = {0x80, 0xBF, 0xC0, 0x9F, 0xA0, 0x8F};
	uint32_t h = 2166136261u, hv = h, hc = h;
	uint8_t b[4];
	for (int a0 = 0; a0 < 256; a0++)
		for (int a1 = 0; a1 < 256; a1++)
			for (int a2 = 0; a2 < 8; a2++)
				for (int a3 = 0; a3 < 6; a3++) {
					b[0] = (uint8_t)a0;
					b[1] = (uint8_t)a1;
					b[2] = (uint8_t)(a2 == 7 ? 0xBF : tails[a2] | (a1 & 0x30));
					b[3] = tails2[a3];
					uint32_t cp = 0x12345678;
					long r = C3(DK_UTF8_DECODE, b, 4, &cp);
					h = fnv_mix(h, (uint64_t)r);
					h = fnv_mix(h, cp);
					if (a3 == 0) {
						hv = fnv_mix(hv, (uint64_t)C2(DK_UTF8_VALID, b, 3));
						hc = fnv_mix(hc, (uint64_t)C2(DK_UTF8_COUNT, b, 3));
					}
				}
	ps("decode sweep fnv=");
	px(h, 8);
	ps(" valid=");
	px(hv, 8);
	ps(" count=");
	px(hc, 8);
	nl();

	static const char mix[] = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xFF\x80z\xED\xA0\x80\xC3";
	long mn = (long)sizeof mix - 1;
	static uint32_t cpo[40];
	for (long cap = -1; cap <= 16; cap += (cap < 4 ? 1 : 6)) {
		memset(cpo, 0xEE, sizeof cpo);
		long r = C4(DK_UTF8_TO_CP, mix, mn, cpo, cap);
		ps("to_cp cap=");
		pd(cap);
		ps(" = ");
		pd(r);
		for (long i = 0; i < r && i < 40; i++) {
			pc(' ');
			px(cpo[i], 4);
		}
		ck(r < 0 || r >= 40 || cpo[r] == 0xEEEEEEEE, "to_cp : au-delà");
		nl();
	}
	ps("to_cp n<=0 ");
	pd(C4(DK_UTF8_TO_CP, mix, 0, cpo, 10));
	pd(C4(DK_UTF8_TO_CP, mix, -2, cpo, 10));
	nl();

	static const uint32_t cpa[] = {0x41, 0xE9, 0x20AC, 0x1F600, 0xD800, 0x110000, 0, 0xFFFFFFFF, 0x7F, 0x80};
	sinkt("cp_to_utf8", DK_CP_TO_UTF8, L(cpa), 10, 0, 0, 0, 0, 2);
	sinkt("cp_to_utf8 3", DK_CP_TO_UTF8, L(cpa), 3, 0, 0, 0, 0, 2);
	sinkt("cp_to_utf8 0", DK_CP_TO_UTF8, L(cpa), 0, 0, 0, 0, 0, 2);
	sinkt("cp_to_utf8 -1", DK_CP_TO_UTF8, L(cpa), -1, 0, 0, 0, 0, 2);
	{
		static uint32_t many[0x400];
		for (int i = 0; i < 0x400; i++)
			many[i] = (uint32_t)i * 0x463u;
		sinkt("cp_to_utf8 many", DK_CP_TO_UTF8, L(many), 0x400, 0, 0, 0, 0, 2);
	}

	static char all[256];
	for (int i = 0; i < 256; i++)
		all[i] = (char)i;
	sinkt("latin1_to_utf8 all", DK_LATIN1_TO_UTF8, L(all), 256, 0, 0, 0, 0, 2);
	sinkt("cp1252_to_utf8 all", DK_CP1252_TO_UTF8, L(all), 256, 0, 0, 0, 0, 2);
	sinkt("latin1_to_utf8 hi", DK_LATIN1_TO_UTF8, L(all + 0x7E), 8, 0, 0, 0, 0, 2);
	sinkt("cp1252_to_utf8 hi", DK_CP1252_TO_UTF8, L(all + 0x7E), 40, 0, 0, 0, 0, 2);
	sinkt("cp1252_to_utf8 0", DK_CP1252_TO_UTF8, L(all + 0x80), 0, 0, 0, 0, 0, 2);
	sinkt("latin1_to_utf8 -1", DK_LATIN1_TO_UTF8, L(all + 0x80), -1, 0, 0, 0, 0, 2);

	static const char u[] = "A\xC3\xA9\xE2\x82\xAC\xC5\x93\xC2\x81\xF0\x9F\x98\x80\xFF\xE2\x84\xA2\xC5\xB8\xC3\xBF\x00z";
	long un = (long)sizeof u - 1;
	static const long reps[] = {0, '?', 0x13F, -1};
	for (int i = 0; i < 4; i++) {
		sinkt("utf8_to_latin1", DK_UTF8_TO_LATIN1, L(u), un, 0, 0, reps[i], 0, 2);
		sinkt("utf8_to_cp1252", DK_UTF8_TO_CP1252, L(u), un, 0, 0, reps[i], 0, 2);
	}
	sinkt("utf8_to_latin1 0", DK_UTF8_TO_LATIN1, L(u), 0, 0, 0, '?', 0, 2);
	sinkt("utf8_to_cp1252 -1", DK_UTF8_TO_CP1252, L(u), -1, 0, 0, '?', 0, 2);

	ps("cp1252_cp");
	h = 2166136261u;
	for (long i = -2; i < 0x202; i++) {
		long r = C1(DK_CP1252_CP, i);
		h = fnv_mix(h, (uint64_t)r);
		if (i >= 0x7E && i <= 0xA1) {
			pc(' ');
			px((uint64_t)r, 4);
		}
	}
	ps(" fnv=");
	px(h, 8);
	nl();
	ps("cp_cp1252");
	h = 2166136261u;
	for (long i = -3; i < 0x2200; i++)
		h = fnv_mix(h, (uint64_t)C1(DK_CP_CP1252, i));
	static const long cq[] = {0, 0x7F, 0x80, 0x81, 0x8D, 0x9F, 0xA0, 0xFF, 0x100, 0x20AC, 0x2122, 0x178,
				  0x192, 0x153, 0xFFFF, -1, (long)0x10000000000020ACull};
	for (unsigned i = 0; i < sizeof cq / sizeof cq[0]; i++) {
		pc(' ');
		pd(C1(DK_CP_CP1252, cq[i]));
	}
	ps(" fnv=");
	px(h, 8);
	nl();
}

/* ---- longueurs, couleurs */

static void test_len(void)
{
	ps("== len\n");
	static const char *const ls[] = {"2.54cm", "12pt", "1in", "-1.5in", "+3mm", "   10px", "5pc", "100%",
					 "abc", "", ".5cm", "1,5cm", "3", "3.1234567cm", "99999999999999cm",
					 "1e3", "12 pt", "-", "+", "0.0000005in", "0.0000015in", "-0", "7.", ".",
					 "1.5", "10twip", "2147483648in", "2147483647in", "21474836479in",
					 "\t5cm", "5CM", "5c", "5m", "1.999999999pt", "1p", "9px%", 0};
	static const long units[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1};
	for (int i = 0; ls[i]; i++) {
		ps("parse ");
		pq(ls[i], slen(ls[i]));
		for (int u = 0; u < 11; u++) {
			pc(' ');
			pd(C4(DK_LEN_PARSE, ls[i], units[u], 3, -99));
		}
		ps(" | bare:");
		for (int u = 0; u < 11; u++) {
			pc(' ');
			pd(C4(DK_LEN_PARSE, ls[i], 0, units[u], -99));
		}
		nl();
	}
	ps("parse null ");
	pd(C4(DK_LEN_PARSE, 0, 0, 0, 1234));
	nl();

	static const long vals[] = {0, 1, -1, 2, 7, 635, 12700, -12700, 914400, -914400, 123456789,
				    9223372036854775807L, (long)0x8000000000000000ull, 1000000000000L};
	uint32_t h = 2166136261u;
	for (unsigned v = 0; v < sizeof vals / sizeof vals[0]; v++) {
		ps("conv ");
		pd(vals[v]);
		ps(" :");
		for (int a = 0; a < 11; a++)
			for (int b = 0; b < 11; b++) {
				long r = C3(DK_LEN_CONV, vals[v], units[a], units[b]);
				h = fnv_mix(h, (uint64_t)r);
				if (a == b + 1 || (a == 4 && b < 9)) {
					pc(' ');
					pd(r);
				}
			}
		nl();
	}
	ps("conv fnv=");
	px(h, 8);
	nl();

	static const long wv[] = {0, 1, -1, 12700, 914400, -914400, 360000, 1, 5, 9525, 1234567,
				  9223372036854775807L};
	static const long wu[] = {0, 3, 4, 5};
	static const long wo[] = {3, 4, 5, 6, 0, 8, 9};
	static const long wd[] = {0, 1, 2, 4, 6, 7, -1};
	char lab[64];
	for (unsigned v = 0; v < sizeof wv / sizeof wv[0]; v++)
		for (unsigned u = 0; u < 4; u++)
			for (unsigned o = 0; o < 7; o++)
				for (unsigned d = 0; d < 7; d++) {
					if ((v + u + o + d) % 3 && !(v < 3 && u == 0))
						continue; /* (un tiers des combinaisons) */
					long n = 0;
					const char *p = "len_write ";
					while (*p)
						lab[n++] = *p++;
					lab[n++] = (char)('0' + v / 10);
					lab[n++] = (char)('0' + v % 10);
					lab[n++] = (char)('0' + u);
					lab[n++] = (char)('0' + o);
					lab[n++] = (char)('0' + d);
					lab[n] = 0;
					sinkt(lab, DK_LEN_WRITE, wv[v], wu[u], wo[o], wd[d], 0, 0, 4);
				}

	ps("== color\n");
	static const char *const cs[] = {"#FF0000", "00ff00", "#0000FF", "80112233", "#80112233", "#1234567",
					 "12345", "#", "", "xyz", "#aBcDeF", "AbCdEfX", "#123456789",
					 "ggg", "#12 456", "@@@@@@", "`abcdef", "GHIJKL", "FFFFFF", 0};
	for (int i = 0; cs[i]; i++) {
		ps("color_parse ");
		pq(cs[i], slen(cs[i]));
		ps(" = ");
		pd(C2(DK_COLOR_PARSE, cs[i], -5));
		nl();
	}
	ps("color_parse null ");
	pd(C2(DK_COLOR_PARSE, 0, 77));
	nl();
	static const long cw[] = {0, 0xFF8000, 0xABCDEF, 0x123456789, -1, 0x00FFFFFF};
	for (unsigned i = 0; i < sizeof cw / sizeof cw[0]; i++)
		for (long f = 0; f < 9; f++) {
			fill();
			long r = C3(DK_COLOR_WRITE, cw[i], f, ob);
			ps("color_write ");
			px((uint64_t)cw[i], 16);
			pc(' ');
			pd(f);
			ps(" = ");
			pd(r);
			pc(' ');
			pq(ob, r + 1);
			ck(intact(r + 1, 32), "color_write : au-delà");
			nl();
		}
}

/* ---- dates */

static void test_dates(void)
{
	ps("== dates\n");
	static const long ys[] = {-4713, -400, -1, 0, 1, 4, 100, 1600, 1899, 1900, 1904, 1970, 2000, 2024,
				  2100, 9999, 1000000000L, -1000000000L};
	static const long ms[] = {-13, -1, 0, 1, 2, 3, 12, 13, 25};
	static const long ds[] = {-1, 0, 1, 28, 29, 31, 400};
	uint32_t h = 2166136261u, hx = h, hx4 = h;
	for (unsigned y = 0; y < sizeof ys / sizeof ys[0]; y++) {
		ps("civil ");
		pd(ys[y]);
		ps(" :");
		for (unsigned m = 0; m < sizeof ms / sizeof ms[0]; m++)
			for (unsigned d = 0; d < sizeof ds / sizeof ds[0]; d++) {
				long r = C3(DK_DAYS_FROM_CIVIL, ys[y], ms[m], ds[d]);
				long e0 = C4(DK_EXCEL_FROM_CIVIL, ys[y], ms[m], ds[d], 0);
				long e1 = C4(DK_EXCEL_FROM_CIVIL, ys[y], ms[m], ds[d], 7);
				h = fnv_mix(h, (uint64_t)r);
				hx = fnv_mix(hx, (uint64_t)e0);
				hx4 = fnv_mix(hx4, (uint64_t)e1);
				if (ds[d] == 1 && (ms[m] == 1 || ms[m] == 13 || ms[m] == 0)) {
					pc(' ');
					pd(r);
					pc('/');
					pd(e0);
					pc('/');
					pd(e1);
				}
			}
		nl();
	}
	ps("civil fnv=");
	px(h, 8);
	ps(" excel=");
	px(hx, 8);
	ps(" excel1904=");
	px(hx4, 8);
	nl();
	ps("leap 1900 ");
	pd(C4(DK_EXCEL_FROM_CIVIL, 1900, 2, 28, 0));
	pc(' ');
	pd(C4(DK_EXCEL_FROM_CIVIL, 1900, 2, 29, 0));
	pc(' ');
	pd(C4(DK_EXCEL_FROM_CIVIL, 1900, 3, 1, 0));
	pc(' ');
	pd(C4(DK_EXCEL_FROM_CIVIL, 0, 2, 29, 0));
	pc(' ');
	pd(C4(DK_EXCEL_FROM_CIVIL, 1899, 14, 29, 0));
	pc(' ');
	pd(C4(DK_EXCEL_FROM_CIVIL, 1900, 1, 1, 0));
	pc(' ');
	pd(C4(DK_EXCEL_FROM_CIVIL, 1900, 1, 0, 0));
	pc(' ');
	pd(C4(DK_EXCEL_FROM_CIVIL, 1904, 1, 1, 1));
	pc(' ');
	pd(C4(DK_EXCEL_FROM_CIVIL, 2024, 2, 29, 0));
	nl();
	ck(C3(DK_DAYS_FROM_CIVIL, 1970, 1, 1) == 0, "1970-01-01 = 0");
	ck(C3(DK_DAYS_FROM_CIVIL, 2000, 3, 1) == 11017, "2000-03-01 = 11017");
	ck(C1(DK_WEEKDAY, 0) == 4, "jeudi");
	ck(C4(DK_EXCEL_FROM_CIVIL, 1900, 1, 1, 0) == 1, "Excel 1900-01-01 = 1");
	ck(C4(DK_EXCEL_FROM_CIVIL, 1900, 3, 1, 0) == 61, "Excel 1900-03-01 = 61");
	ck(C4(DK_EXCEL_FROM_CIVIL, 2024, 1, 1, 0) == 45292, "Excel 2024-01-01 = 45292");
	ck(C4(DK_EXCEL_FROM_CIVIL, 1904, 1, 1, 1) == 0, "Excel 1904 : 1904-01-01 = 0");

	int32_t y, m, d;
	h = 2166136261u;
	uint32_t hw = h;
	int rt = 1;
	for (long z = -3000000; z <= 3000000; z += 997) {
		y = m = d = 0x5A5A5A5A;
		C4(DK_CIVIL_FROM_DAYS, z, &y, &m, &d);
		h = fnv_mix(h, (uint32_t)y);
		h = fnv_mix(h, (uint32_t)m);
		h = fnv_mix(h, (uint32_t)d);
		hw = fnv_mix(hw, (uint64_t)C1(DK_WEEKDAY, z));
		if (C3(DK_DAYS_FROM_CIVIL, y, m, d) != z)
			rt = 0;
	}
	ck(rt, "civil_from_days puis days_from_civil");
	ps("days sweep fnv=");
	px(h, 8);
	ps(" weekday=");
	px(hw, 8);
	nl();
	static const long zs[] = {0, -1, 59, 60, 365, 10957, 11016, 11017, -719468, -719469, 2932896,
				  9223372036854775807L, (long)0x8000000000000000ull, 1000000000000L};
	for (unsigned i = 0; i < sizeof zs / sizeof zs[0]; i++) {
		y = m = d = 0x5A5A5A5A;
		long r = C4(DK_CIVIL_FROM_DAYS, zs[i], &y, &m, &d);
		ps("days ");
		pd(zs[i]);
		ps(" = ");
		pd(r);
		pc(' ');
		pd(y);
		pc('-');
		pd(m);
		pc('-');
		pd(d);
		ps(" wd=");
		pd(C1(DK_WEEKDAY, zs[i]));
		nl();
	}
	y = m = d = 7;
	ps("days nulls ");
	pd(C4(DK_CIVIL_FROM_DAYS, 12345, 0, &m, 0));
	pc(' ');
	pd(y);
	pd(m);
	pd(d);
	pd(C4(DK_CIVIL_FROM_DAYS, 12345, &y, 0, &d));
	pc(' ');
	pd(y);
	pd(m);
	pd(d);
	pd(C4(DK_CIVIL_FROM_DAYS, 12345, 0, 0, 0));
	nl();

	static const long ex[] = {-1000, -1, 0, 1, 2, 59, 60, 61, 62, 366, 1462, 1463, 25569, 45292,
				  2958465, 1000000000L};
	for (unsigned i = 0; i < sizeof ex / sizeof ex[0]; i++)
		for (long s = 0; s < 2; s++) {
			y = m = d = 0x5A5A5A5A;
			long r = C5(DK_CIVIL_FROM_EXCEL, ex[i], s ? 3 : 0, &y, &m, &d);
			ps("excel ");
			pd(ex[i]);
			pc(' ');
			pd(s);
			ps(" = ");
			pd(r);
			pc(' ');
			pd(y);
			pc('-');
			pd(m);
			pc('-');
			pd(d);
			nl();
		}
	h = 2166136261u;
	for (long sn = -100; sn < 80000; sn += 7)
		for (long s = 0; s < 2; s++) {
			y = m = d = 0;
			C5(DK_CIVIL_FROM_EXCEL, sn, s, &y, &m, &d);
			h = fnv_mix(h, (uint32_t)y);
			h = fnv_mix(h, (uint32_t)m);
			h = fnv_mix(h, (uint32_t)d);
			if (sn > 0 && sn != 60 && C4(DK_EXCEL_FROM_CIVIL, y, m, d, s) != sn)
				rt = 0;
		}
	ck(rt, "civil_from_excel puis excel_from_civil");
	ps("excel sweep fnv=");
	px(h, 8);
	nl();
	ps("excel nulls ");
	pd(C5(DK_CIVIL_FROM_EXCEL, 60, 0, 0, 0, 0));
	pd(C5(DK_CIVIL_FROM_EXCEL, 100, 1, 0, 0, 0));
	nl();
}

/* ---- CSV */

static void csv_dump(const char *label, const char *doc, long n, long sep, long cap)
{
	if (n < 0)
		n = slen(doc);
	long h = C3(DK_CSV_OPEN, doc, n, sep);
	ps("csv ");
	ps(label);
	ps(" h=");
	pd(h);
	ps(" sep=");
	pd(C1(DK_CSV_SEP, h));
	fill();
	long r = C3(DK_CSV_TEXT, h, ob, 16);
	ps(" t0=");
	pd(r);
	ck(r == 0 && ob[0] == 0, "csv : texte avant le premier champ");
	nl();
	ps(" ");
	for (int k = 0; k < 200; k++) {
		long f = C1(DK_CSV_NEXT, h);
		pc(' ');
		pd(f);
		if (f <= 0)
			break;
		fill();
		r = C3(DK_CSV_TEXT, h, ob, cap);
		pc(':');
		pd(r);
		long nn = r < cap ? r : cap - 1;
		if (nn < 0)
			nn = 0;
		if (r <= 40)
			pq(ob, nn);
		else {
			ps("fnv=");
			px(fnv(ob, nn), 8);
		}
		ck(cap <= 0 || ob[nn] == 0, "csv : 0 final");
		ck(C3(DK_CSV_TEXT, h, 0, 0) == r, "csv : texte, cap 0");
		if (f & 2)
			ps(" /");
	}
	ck(C1(DK_CSV_NEXT, h) == 0, "csv : fin reste fin");
	nl();
	C1(DK_CSV_CLOSE, h);
}

static void test_csv(void)
{
	ps("== csv\n");
	csv_dump("simple", "a,b,c\n1,2,3\n", -1, 0, 64);
	csv_dump("trail-sep", "a,b,", -1, ',', 64);
	csv_dump("trail-sep-nl", "a,b,\n", -1, ',', 64);
	csv_dump("empty", "", 0, 0, 64);
	csv_dump("one-nl", "\n", -1, 0, 64);
	csv_dump("sep-only", ",", -1, ',', 64);
	csv_dump("quotes", "\"a,b\",\"c\"\"d\",\"\",x\"y\",\"e\"f,\"\"\"\"\n", -1, ',', 64);
	csv_dump("quote-reopen", "\"\"\"a\",\"\"x\"y\"", -1, ',', 64);
	csv_dump("quote-nl", "\"line1\nline2\",z\r\nnext", -1, ',', 64);
	csv_dump("quote-unterm", "\"abc,def", -1, ',', 64);
	csv_dump("crlf", "a;b\r\nc;d\r\n", -1, 0, 64);
	csv_dump("lone-cr", "a,b\rc,d\r", -1, ',', 64);
	csv_dump("cr-end", "x\r", -1, ',', 64);
	csv_dump("tab", "a\tb\tc\n1\t2\t3\n4\t5\t6\n", -1, 0, 64);
	csv_dump("semi-vs-comma", "a;b;c,d\ne;f;g,h\ni;j;k\n", -1, 0, 64);
	csv_dump("comma-win", "a,b;c\nd,e;f\n", -1, 0, 64);
	csv_dump("no-nl-guess", "a;b;c", -1, 0, 64);
	csv_dump("quoted-seps", "\"a;b;c\",d\n\"e;f\",g\n", -1, 0, 64);
	csv_dump("bom", "\xEF\xBB\xBFx;y\n1;2\n", -1, 0, 64);
	csv_dump("bom-only", "\xEF\xBB\xBF", -1, 0, 64);
	csv_dump("bom-short", "\xEF\xBB", -1, 0, 64);
	csv_dump("small-cap", "hello,\"wor\"\"ld\",x\n", -1, ',', 4);
	csv_dump("cap1", "hello,world\n", -1, ',', 1);
	csv_dump("sep-pipe", "a|b|c\n", -1, '|', 64);
	csv_dump("sep-quote", "a\"b\"c\n", -1, '"', 64);
	csv_dump("sep-wide", "a,b\n", -1, 0x12C, 64);
	csv_dump("sep-neg", "a,b\n", -1, -1, 64);
	csv_dump("len-cut", "a,b,c,d", 4, ',', 64);
	{
		static char big[2000];
		long n = 0;
		for (int l = 0; l < 25; l++) {
			for (int f = 0; f < 4; f++) {
				for (int i = 0; i < (l * 7 + f * 3) % 50; i++)
					big[n++] = (char)('a' + (i + l) % 26);
				big[n++] = (f == 3) ? '\n' : ';';
			}
		}
		big[n] = 0;
		csv_dump("big", big, n, 0, 64);
	}

	static const char *const es[] = {"", "plain", "a,b", "a;b", "q\"q", "nl\nx", "cr\rx", "tab\tx", "\"", 0};
	static const long sp[] = {',', ';', 9, 0x12C};
	for (int i = 0; es[i]; i++)
		for (int s = 0; s < 4; s++)
			sinkt("csv_esc", DK_CSV_ESC, L(es[i]), -1, sp[s], 0, 0, 0, 3);
	sinkt("csv_esc n=2", DK_CSV_ESC, L("a,b"), 2, ',', 0, 0, 0, 3);
	sinkt("csv_esc n=3", DK_CSV_ESC, L("a,b"), 3, ',', 0, 0, 0, 3);
	sinkt("csv_esc n=0", DK_CSV_ESC, L("a,b"), 0, ',', 0, 0, 0, 3);
}

/* ---- paquets */

static uint8_t zb[8192];
static long zn;

static void zb1(unsigned v)
{
	zb[zn++] = (uint8_t)v;
}

static void zb2(unsigned v)
{
	zb1(v & 0xFF);
	zb1(v >> 8 & 0xFF);
}

static void zb4(uint32_t v)
{
	zb2(v & 0xFFFF);
	zb2(v >> 16);
}

static void zbs(const char *s, long n)
{
	for (long i = 0; i < n; i++)
		zb1((uint8_t)s[i]);
}

struct zent {
	const char *name;
	const char *data;
	unsigned method;
	unsigned extra;
};

/* un ZIP : entrées locales, répertoire central, EOCD (+ commentaire) */
static long zip_build(const struct zent *e, int n, long comment, int break_cd)
{
	uint32_t off[16];
	zn = 0;
	for (int i = 0; i < n; i++) {
		off[i] = (uint32_t)zn;
		long nl = slen(e[i].name), dl = slen(e[i].data);
		zb4(0x04034B50);
		zb2(20);
		zb2(0);
		zb2(e[i].method);
		zb2(0);
		zb2(0);
		zb4(0);
		zb4((uint32_t)dl);
		zb4((uint32_t)dl);
		zb2((unsigned)nl);
		zb2(e[i].extra);
		zbs(e[i].name, nl);
		for (unsigned x = 0; x < e[i].extra; x++)
			zb1(0);
		zbs(e[i].data, dl);
	}
	uint32_t cd = (uint32_t)zn;
	for (int i = 0; i < n; i++) {
		long nl = slen(e[i].name);
		zb4(break_cd && i == 1 ? 0x12345678 : 0x02014B50);
		zb2(20);
		zb2(20);
		zb2(0);
		zb2(e[i].method);
		zb2(0);
		zb2(0);
		zb4(0);
		zb4(0);
		zb4(0);
		zb2((unsigned)nl);
		zb2(i == 0 ? 3 : 0); /* extra */
		zb2(i == 0 ? 2 : 0); /* commentaire */
		zb2(0);
		zb2(0);
		zb4(0);
		zb4(off[i]);
		zbs(e[i].name, nl);
		if (i == 0)
			zbs("xyzcm", 5);
	}
	uint32_t cdsz = (uint32_t)zn - cd;
	zb4(0x06054B50);
	zb2(0);
	zb2(0);
	zb2((unsigned)n);
	zb2((unsigned)n);
	zb4(cdsz);
	zb4(cd);
	zb2((unsigned)comment);
	for (long i = 0; i < comment; i++)
		zb1('C');
	return zn;
}

static void kind_t(const char *label, const void *buf, long n)
{
	ps("kind ");
	ps(label);
	ps(" n=");
	pd(n);
	ps(" = ");
	pd(C2(DK_DOC_KIND, buf, n));
	for (long k = n - 1; k >= 0 && k >= n - 3; k--) {
		pc(' ');
		pd(C2(DK_DOC_KIND, buf, k));
	}
	nl();
}

static void test_pkg(void)
{
	ps("== package\n");
	static const uint8_t ole[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
	kind_t("ole", ole, 8);
	kind_t("rtf", "{\\rtf1\\ansi}", 11);
	kind_t("rtx", "{\\rtx", 5);
	kind_t("rt4", "{\\rt", 4);
	kind_t("short", "PK", 2);
	kind_t("neg", "PK", -1);
	kind_t("text", "hello world", 11);
	zn = 0;
	zb4(0x06054B50);
	for (int i = 0; i < 18; i++)
		zb1(0);
	kind_t("emptyzip", zb, zn);
	static const struct zent odt[] = {{"mimetype", "application/vnd.oasis.opendocument.text", 0, 0},
					  {"content.xml", "<x/>", 8, 0}};
	zip_build(odt, 2, 0, 0);
	kind_t("odt", zb, zn);
	static const struct zent ods[] = {{"mimetype", "application/vnd.oasis.opendocument.spreadsheet \n", 0, 4}};
	zip_build(ods, 1, 0, 0);
	kind_t("ods-ws-extra", zb, zn);
	static const struct zent otp[] = {{"mimetype", "application/vnd.oasis.opendocument.presentation-template", 0, 0}};
	zip_build(otp, 1, 0, 0);
	kind_t("otp", zb, zn);
	static const struct zent odz[] = {{"mimetype", "application/vnd.oasis.opendocument.text", 8, 0}};
	zip_build(odz, 1, 0, 0);
	kind_t("odt-deflated", zb, zn);
	static const struct zent odx[] = {{"mimetype", "application/x-unknown", 0, 0}};
	zip_build(odx, 1, 0, 0);
	kind_t("mime-unknown", zb, zn);
	static const struct zent docx[] = {{"[Content_Types].xml", "<Types/>", 8, 0},
					   {"_rels/.rels", "<R/>", 8, 0},
					   {"word/document.xml", "<w:document/>", 8, 0}};
	zip_build(docx, 3, 0, 0);
	kind_t("docx", zb, zn);
	zip_build(docx, 3, 300, 0);
	kind_t("docx-comment", zb, zn);
	zip_build(docx, 3, 0, 1);
	kind_t("docx-brokencd", zb, zn);
	static const struct zent xlsx[] = {{"xl/workbook.xml", "<w/>", 8, 0}, {"[Content_Types].xml", "<T/>", 8, 0}};
	zip_build(xlsx, 2, 0, 0);
	kind_t("xlsx", zb, zn);
	static const struct zent pptx[] = {{"ppt/presentation.xml", "<p/>", 8, 0},
					   {"word/x.xml", "<p/>", 8, 0},
					   {"[Content_Types].xml", "<T/>", 8, 0}};
	zip_build(pptx, 3, 0, 0);
	kind_t("pptx-first", zb, zn);
	static const struct zent noct[] = {{"word/document.xml", "<w/>", 8, 0}};
	zip_build(noct, 1, 0, 0);
	kind_t("no-ct", zb, zn);
	static const struct zent wordx[] = {{"wordx/a.xml", "<w/>", 8, 0}, {"[Content_Types].xml", "<T/>", 8, 0},
					    {"xl/", "", 0, 0}};
	zip_build(wordx, 3, 0, 0);
	kind_t("wordx-then-xl", zb, zn);
	static const struct zent shortn[] = {{"xl/a", "", 0, 0}, {"[Content_Types].xml", "<T/>", 8, 0}};
	zip_build(shortn, 2, 0, 0);
	kind_t("xl-short-name", zb, zn);
	static const struct zent ctcase[] = {{"[content_types].xml", "", 0, 0}, {"ppt/x", "", 0, 0}};
	zip_build(ctcase, 2, 0, 0);
	kind_t("ct-case", zb, zn);

	ps("kind_mime");
	static const char *const mm[] = {"application/vnd.openxmlformats-officedocument.wordprocessingml.document",
					 "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet  \t\n",
					 "application/vnd.openxmlformats-officedocument.presentationml.presentation",
					 "application/vnd.oasis.opendocument.text",
					 "application/vnd.oasis.opendocument.spreadsheet-template",
					 "application/vnd.oasis.opendocument.presentation-templatex",
					 "application/vnd.oasis.opendocument.tex",
					 "application/vnd.oasis.opendocument.textx",
					 " application/vnd.oasis.opendocument.text", "", "   ", "application", 0};
	for (int i = 0; mm[i]; i++) {
		pc(' ');
		pd(C2(DK_DOC_KIND_MIME, mm[i], -1));
		pd(C2(DK_DOC_KIND_MIME, mm[i], slen(mm[i])));
		pd(C2(DK_DOC_KIND_MIME, mm[i], slen(mm[i]) > 3 ? slen(mm[i]) - 3 : 0));
	}
	nl();
	for (long k = -1; k <= 10; k++) {
		long p = C1(DK_DOC_MIME, k);
		ps("doc_mime ");
		pd(k);
		ps(" = ");
		if (p)
			pq((const char *)p, slen((const char *)p));
		else
			ps("0");
		nl();
	}
	for (long k = -1; k <= 8; k++) {
		long p = C1(DK_ENV_TEXT, k);
		ps("env_text ");
		pd(k);
		ps(" = ");
		if (p)
			pq((const char *)p, slen((const char *)p));
		else
			ps("0");
		nl();
	}
	ck(C1(DK_ENV_TEXT, (long)0x8000000000000000ull) == 0 && C1(DK_DOC_MIME, (long)0x8000000000000001ull) == 0,
	   "env_text, doc_mime : hors bornes");

	static const char *const pp[][2] = {
		{"word/document.xml", "media/a.png"}, {"word/document.xml", "../customXml/item1.xml"},
		{"word/document.xml", "/docProps/core.xml"}, {"ppt/slides/slide1.xml", "../media/image1.png"},
		{"ppt/slides/slide1.xml", "../../x.xml"}, {"ppt/slides/slide1.xml", "../../../x.xml"},
		{"ppt/slides/slide1.xml", "./a/./b.xml"}, {"document.xml", "a.xml"}, {"document.xml", "../a.xml"},
		{"", "x"}, {"a/b/", "c"}, {"a/b/", "../c"}, {"a//b/c", "../../d"}, {"a/b/c", ".hidden"},
		{"a/b/c", "..x/y"}, {"a/b/c", "."}, {"a/b/c", ".."}, {"a/b/c", "../"}, {"x", "/"}, {"x", ""},
		{0, 0}};
	for (int i = 0; pp[i][0]; i++)
		sinkt("part_path", DK_PART_PATH, L(pp[i][0]), L(pp[i][1]), 0, 0, 0, 0, 2);
	static const char *const rp[] = {"word/document.xml", "document.xml", "", "/", "a/b/c.xml", "a/", 0};
	for (int i = 0; rp[i]; i++)
		sinkt("rels_path", DK_RELS_PATH, L(rp[i]), 0, 0, 0, 0, 0, 1);
}

static const char rels[] =
	"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
	"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
	"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
	"<Relationship Id=\"rId2\" Type=\"http://x/hyperlink\" Target=\"http://a.b/?q=1&amp;r=2\" TargetMode=\"External\"/>"
	"<Relationship Id=\"rId3\" Type=\"http://x/HyperLink\" Target=\"local.xml\" TargetMode=\"Internal\"/>"
	"<!-- <Relationship Id=\"rIdC\" Type=\"x\" Target=\"comment\"/> -->"
	"<pr:Relationship pr:Id=\"rId4\" Type='http://x/slideMaster' Target='slideMasters/slideMaster1.xml'></pr:Relationship>"
	"<Relationship Id=\"rId5\" Type=\"nolast\" Target=\"&#x41;&lt;B\"/>"
	"<Relationship Id=\"rId6\" Type=\"http://x/image/\"/>"
	"<Relationship Id=\"rId7\" Type=\"http://x/slideMaster\" Target=\"second.xml\" TargetMode=\"external\"/>"
	"<Relationship Id=\"rId8\" Type=\"http://x/slideMaster\" Target=\"third.xml\" TargetMode=\"Externa\"/>"
	"<Relationshipx Id=\"rId9\" Target=\"no\"/>"
	"<Relationship Id=\"rId10\" Type=\"http://x/image\" Target=\"m/i.png\"/>"
	"<Relationship Id=\"rId1\" Type=\"http://x/dup\" Target=\"dup.xml\"/>"
	"</Relationships>";

static void test_rels(void)
{
	ps("== rels\n");
	long n = (long)sizeof rels - 1;
	ps("rel_count ");
	pd(C2(DK_REL_COUNT, rels, n));
	pc(' ');
	pd(C2(DK_REL_COUNT, rels, 300));
	pc(' ');
	pd(C2(DK_REL_COUNT, rels, 0));
	pc(' ');
	pd(C2(DK_REL_COUNT, rels, -5));
	pc(' ');
	static const char r3[] = "<Relationship/><a:Relationship/><Relationship>";
	pd(C2(DK_REL_COUNT, r3, (long)sizeof r3 - 1));
	nl();
	char lab[40];
	for (long r = -1; r <= 12; r++)
		for (long f = -1; f <= 4; f++) {
			if (r > 1 && r < 11 && (f == -1 || f == 4))
				continue;
			long k = 0;
			const char *p = "rel_get ";
			while (*p)
				lab[k++] = *p++;
			if (r < 0) {
				lab[k++] = '-';
				lab[k++] = '1';
			} else {
				if (r >= 10)
					lab[k++] = (char)('0' + r / 10);
				lab[k++] = (char)('0' + r % 10);
			}
			lab[k++] = ' ';
			if (f < 0) {
				lab[k++] = '-';
				lab[k++] = '1';
			} else {
				lab[k++] = (char)('0' + f);
			}
			lab[k] = 0;
			sinkt(lab, DK_REL_GET, L(rels), n, r, f, 0, 0, 4);
		}
	static const char *const ids[] = {"rId1", "rId2", "rId3", "rId4", "rId5", "rId6", "rId9", "rId10", "rId",
					  "rId11", "rIdC", "", "RID1", 0};
	for (int i = 0; ids[i]; i++)
		sinkt("rel_target", DK_REL_TARGET, L(rels), n, L(ids[i]), 0, 0, 0, 3);
	static const char *const ts[] = {"styles", "hyperlink", "HYPERLINK", "slideMaster", "slidemaster", "image",
					 "nolast", "", "dup", "slide", "slideMasters", 0};
	for (int i = 0; ts[i]; i++)
		sinkt("rel_by_type", DK_REL_BY_TYPE, L(rels), n, L(ts[i]), 0, 0, 0, 3);
	sinkt("rel_by_type short", DK_REL_BY_TYPE, L(rels), 400, L("styles"), 0, 0, 0, 3);
	sinkt("rel_target empty", DK_REL_TARGET, L(rels), 0, L("rId1"), 0, 0, 0, 3);
	sinkt("rel_target neg", DK_REL_TARGET, L(rels), -1, L("rId1"), 0, 0, 0, 3);

	ps("== writers\n");
	static const char *const ty[] = {"styles", "http://x/y", "a<b&c\"d", "", "tab\there", 0};
	static const char *const tg[] = {"styles.xml", "http://a.b/?q=1&r=2", "x\ny\rz", "", 0};
	static const long nums[] = {0, 1, 42, -1, 1234567890123L};
	for (int i = 0; ty[i]; i++)
		for (int j = 0; tg[j]; j++) {
			if ((i + j) % 2)
				continue;
			sinkt("rel_write", DK_REL_WRITE, nums[(i + j) % 5], L(ty[i]), L(tg[j]), 0, 0, 0, 4);
			sinkt("rel_write ext", DK_REL_WRITE, nums[(i * 3 + j) % 5], L(ty[i]), L(tg[j]), 2, 0, 0, 4);
		}
	sinkt("rel_write nulltarget", DK_REL_WRITE, 3, L("styles"), 0, 1, 0, 0, 4);
	sinkt("opc_default", DK_OPC_DEFAULT, L("png"), L("image/png"), 0, 0, 0, 0, 2);
	sinkt("opc_default esc", DK_OPC_DEFAULT, L("a\"b"), L("x<y>&\t"), 0, 0, 0, 0, 2);
	sinkt("opc_default null", DK_OPC_DEFAULT, 0, 0, 0, 0, 0, 0, 2);
	sinkt("opc_override", DK_OPC_OVERRIDE, L("word/document.xml"),
	      L("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"), 0, 0, 0, 0, 2);
	sinkt("opc_override slash", DK_OPC_OVERRIDE, L("/docProps/core.xml"), L("a&b"), 0, 0, 0, 0, 2);
	sinkt("opc_override empty", DK_OPC_OVERRIDE, L(""), 0, 0, 0, 0, 0, 2);
	sinkt("odf_entry root", DK_ODF_ENTRY, L("/"), L("application/vnd.oasis.opendocument.text"), 0, 0, 0, 0, 2);
	sinkt("odf_entry", DK_ODF_ENTRY, L("content.xml"), L("text/xml"), 0, 0, 0, 0, 2);
	sinkt("odf_entry //", DK_ODF_ENTRY, L("//"), L(""), 0, 0, 0, 0, 2);
	sinkt("odf_entry esc", DK_ODF_ENTRY, L("Pictures/a&b.png"), 0, 0, 0, 0, 0, 2);
	sinkt("odf_entry empty", DK_ODF_ENTRY, L(""), L("x"), 0, 0, 0, 0, 2);

	ps("== hash\n");
	static char all[256];
	for (int i = 0; i < 256; i++)
		all[i] = (char)i;
	static const long hn[] = {0, 1, 2, 3, 16, 255, 256, -1, -2, (long)0x8000000000000000ull};
	ps("hash");
	for (unsigned i = 0; i < sizeof hn / sizeof hn[0]; i++) {
		long r = C2(DK_HASH, all, hn[i]);
		pc(' ');
		px((uint64_t)r, 16);
		if (hn[i] >= 0)
			ck(r == (long)(fnv(all, hn[i]) ^ (uint32_t)hn[i]), "dk_hash = FNV-1a xor octets");
	}
	pc(' ');
	px((uint64_t)C2(DK_HASH, rels, (long)sizeof rels - 1), 16);
	nl();
}

static void run(void)
{
	ps("dktest : DocumentKit");
#if defined(__aarch64__)
	ps(" (aarch64)\n");
#else
	ps(" (x86_64)\n");
#endif
	long ak = sys2(SYS_LIB_OPEN, "appkit", 26);
	if (ak >= 0) {
		const uint32_t *at = (const uint32_t *)ak;
		f_print = (fn_print)(ak + at[2 + AK_F_PRINT]);
	}
	kit = open_kit("documentkit", DK_COUNT);
	if (kit < 0) {
		ps("documentkit absent : ");
		pd(kit);
		nl();
		flush();
		sys6(SYS_EXIT, 127, 0, 0, 0, 0, 0);
	}
	const uint32_t *t = (const uint32_t *)kit;
	kit_count = t[1];
	ps("table magic=");
	px(t[0], 8);
	ps(" count=");
	pd(kit_count);
	nl();
	if (t[0] != KIT_MAGIC || kit_count < DK_COUNT) {
		ps("table invalide\n");
		flush();
		sys6(SYS_EXIT, 126, 0, 0, 0, 0, 0);
	}
	test_xml();
	test_esc();
	test_utf8();
	test_len();
	test_dates();
	test_csv();
	test_pkg();
	test_rels();
	for (int i = 0; i < DK_COUNT; i++)
		if (!called[i]) {
			ps("jamais appelée : ");
			ps(fn_names[i]);
			nl();
			fails++;
		}
	ps("fin : ");
	pd(calls);
	ps(" appels, ");
	pd(checks);
	ps(" controles, ");
	pd(fails);
	ps(" echecs\n");
	flush();
	sys6(SYS_EXIT, fails > 125 ? 125 : fails, 0, 0, 0, 0, 0);
}

#if defined(__x86_64__)
__attribute__((force_align_arg_pointer, noreturn))
#else
__attribute__((noreturn))
#endif
void _start(void)
{
	run();
	for (;;)
		;
}
