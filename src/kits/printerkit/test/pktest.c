/* pktest : essai différentiel de PrinterKit (src/kits/printerkit), sans
 * bibliothèque, pour AArch64 (Korin sur le Pi) et x86-64 (Korin sur PC).
 *
 * Appelle les fonctions du kit par sa table et n'écrit que ce qui ne dépend ni
 * des adresses ni de l'heure : valeurs rendues, réglages, et sommes FNV-1a des
 * messages que le kit envoie au service d'impression et des fichiers de pages
 * qu'il écrit. Le kit en assembleur (build/lib/printerkit.so) et le kit en C
 * (build/kits/<arch>/printerkit.so) doivent donner des sorties identiques.
 *
 * Le service « print » est joué par pktest lui-même (un thread : faux printd,
 * réponses fixes, somme de chaque message reçu sauf le pid de l'appelant) :
 * il faut les droits de /sys et que printd ne tourne pas.
 *   Pi : pktest -L -o /tmp/pk.txt [-f]   (/sys/pktest lancé par init2, sortie dans
 *        le fichier ; la carte d'essai n'a pas printd dans etc/session)
 *   PC : run pktest
 * Sans droits (SVC_REGISTER refusé), seule la première partie tourne.
 *
 * Parties : « === sans service » (fonctions qui ne parlent pas au service),
 * « === faux service » (réglage, imprimantes, papiers, file, polices,
 * mesures, et un travail forgé, sans fichier), puis, avec -f, « === fichiers »
 * (vrais travaux : le fichier des pages, /tmp/pktest/<id>.opj, relu et
 * décrit enregistrement par enregistrement). Le PC n'a pas encore de service
 * de fichiers : ne comparer que ce qui précède cette ligne.
 *
 * pktest -r : un travail vers l'imprimante PDF du vrai printd (lancé par le
 * kit), sans droits : /bin/pktest -r ; le PDF (/tmp/pktest-reel.pdf)
 * relu et sommé. Jamais d'imprimante réseau ni de recherche sur le réseau.
 * pktest -s FICHIER : la taille et la somme d'un fichier (comparer deux PDF).
 *
 * Contrôles propres : « ECHEC ... », code de sortie = nombre d'échecs.
 * Construction : src/kits/printerkit/test/build.sh (dans WSL). */
#include <stddef.h>
#include <stdint.h>

/* src/inc/syscalls.inc */
#define SYS_VM_MAP 0x06
#define SYS_EXIT 0x10
#define SYS_SLEEP 0x17
#define SYS_YIELD 0x18
#define SYS_THREAD_CREATE 0x20
#define SYS_THREAD_EXIT 0x21
#define SYS_SVC_REGISTER 0x30
#define SYS_SVC_OPEN 0x31
#define SYS_SVC_CALL 0x32
#define SYS_SVC_NEXT 0x33
#define SYS_SVC_REPLY 0x34
#define SYS_PRINT 0x60
#define SYS_LIB_OPEN 0x64

#define KIT_MAGIC 0x54494B41
/* AppKit (rangs de appkit.abi) */
#define AK_F_OPEN 14
#define AK_F_READ 15
#define AK_F_WRITE 16
#define AK_F_CLOSE 18
#define AK_F_STAT 19
#define AK_F_MKDIR 21
#define AK_F_UNLINK 22
#define AK_F_PRINT 25    /* (version 26) */
#define AK_F_SPAWN 26
#define AK_F_KIT_LOAD 31 /* (version 32) */

/* printerkit.inc */
#define PRINT_DRAFT 3
#define PRINT_NORMAL 4
#define PRINT_HIGH 5
#define PRINT_PORTRAIT 0
#define PRINT_LANDSCAPE 1
#define PRINT_BOLD 1
#define PRINT_ITALIC 2
#define PRINT_IMG_ALPHA 1

#define PS_SIZE 0
#define PS_PRINTER 4
#define PS_MEDIA 68
#define PS_PAPER_W 132
#define PS_PAPER_H 136
#define PS_MARGIN_L 140
#define PS_ORIENTATION 156
#define PS_COPIES 160
#define PS_MONO 164
#define PS_QUALITY 168
#define PS_FROM 172
#define PS_TO 176
#define PS_OUTPUT 180
#define PS_BYTES 500

#define PR_NAME 0
#define PR_KIND 64
#define PR_URI 72
#define PR_MODEL 232
#define PR_COLOR 296
#define PR_COPIES 300
#define PR_DPI 304
#define PR_DEFAULT 308
#define PR_QUALITY 312
#define PR_MEDIA_DEFAULT 316
#define PR_BYTES 380
#define PF_ADDRESS 0
#define PF_NAME 32
#define PF_MODEL 96
#define PF_USABLE 160
#define PF_BYTES 164
#define PJI_ID 0
#define PJI_STATE 4
#define PJI_PAGES 8
#define PJI_PAGE 12
#define PJI_PRINTER 16
#define PJI_TITLE 80
#define PJI_MESSAGE 160
#define PJI_BYTES 256

#define WM_OP 0
#define WM_A 8
#define WM_B 16
#define WM_C 24
#define WM_D 32
#define WM_DATA 48

/* printd.inc */
#define PD_NEW 1
#define PD_SUBMIT 2
#define PD_ABORT 3
#define PD_SETUP 4
#define PD_PRINTER 5
#define PD_MEDIA 6
#define PD_ADD 7
#define PD_REMOVE 8
#define PD_DEFAULT 9
#define PD_REFRESH 10
#define PD_SCAN 11
#define PD_FOUND 12
#define PD_JOB 13
#define PD_CANCEL 14
#define PD_FORGET 15
#define PD_FONT 16
#define PD_MEASURE 17
#define PD_METRICS 18

/* rangs de printerkit.abi */
enum {
	P_SETUP_DEFAULT, P_DIALOG, P_SETUP_PAPER, P_BEGIN, P_PAGE, P_RECT, P_LINE, P_FRAME, P_PATH_MOVE,
	P_PATH_LINE, P_PATH_CURVE, P_PATH_CLOSE, P_PATH_FILL, P_PATH_STROKE, P_IMAGE_MODE, P_IMAGE, P_IMAGE_JPEG,
	P_FONT, P_TEXT, P_TEXT_WIDTH, P_FONT_METRICS, P_FONT_DATA, P_GLYPH, P_END, P_ABORT, P_PRINTERS,
	P_PRINTER_MEDIA, P_PRINTER_ADD, P_PRINTERS_FIND, P_PRINTER_REMOVE, P_PRINTER_DEFAULT, P_PRINTER_STATUS,
	P_JOBS, P_JOB_CANCEL, P_JOBS_FORGET, P_COUNT_FN
};

static const char *const fn_names[P_COUNT_FN] = {
	"print_setup_default", "print_dialog", "print_setup_paper", "print_begin", "print_page", "print_rect",
	"print_line", "print_frame", "print_path_move", "print_path_line", "print_path_curve", "print_path_close",
	"print_path_fill", "print_path_stroke", "print_image_mode", "print_image", "print_image_jpeg",
	"print_font", "print_text", "print_text_width", "print_font_metrics", "print_font_data", "print_glyph",
	"print_end", "print_abort", "print_printers", "print_printer_media", "print_printer_add",
	"print_printers_find", "print_printer_remove", "print_printer_default", "print_printer_status",
	"print_jobs", "print_job_cancel", "print_jobs_forget",
};

/* ---- appels système */

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
#define sys1(n, a) sys6((n), (long)(a), 0, 0, 0, 0, 0)
#define sys2(n, a, b) sys6((n), (long)(a), (long)(b), 0, 0, 0, 0)
#define sys3(n, a, b, c) sys6((n), (long)(a), (long)(b), (long)(c), 0, 0, 0)

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

static int seq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static void scpy(char *d, const char *s, long cap)
{
	long i = 0;
	while (s[i] && i < cap - 1) {
		d[i] = s[i];
		i++;
	}
	d[i] = 0;
}

static uint32_t ld32(const void *p)
{
	uint32_t v;
	memcpy(&v, p, 4);
	return v;
}

static void st32(void *p, uint32_t v)
{
	memcpy(p, &v, 4);
}

static long ld64(const void *p)
{
	long v;
	memcpy(&v, p, 8);
	return v;
}

/* ---- AppKit */

static long ak;
typedef long (*fn6)(long, long, long, long, long, long);
typedef long (*fn8)(long, long, long, long, long, long, long, long);

static long ak_call(int i, long a, long b, long c)
{
	if (ak < 0)
		return -38;
	const uint32_t *t = (const uint32_t *)ak;
	fn6 fn = (fn6)(ak + t[2 + i]);
	return fn(a, b, c, 0, 0, 0);
}

/* ---- sortie : un fichier (-o), sinon la sortie standard d'AppKit, sinon la
 * console du noyau */

static char pb[4096];
static long pn;
static long out_fd = -1;

static void flush(void)
{
	if (!pn)
		return;
	if (out_fd >= 0)
		ak_call(AK_F_WRITE, out_fd, (long)pb, pn);
	else if (ak_call(AK_F_PRINT, (long)pb, pn, 0) < 0)
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

/* texte entre guillemets, échappé */
static void pq(const char *s, long max)
{
	pc('"');
	for (long i = 0; i < max && s[i]; i++) {
		unsigned c = (unsigned char)s[i];
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

static void kv(const char *k, long v)
{
	pc(' ');
	ps(k);
	pc('=');
	pd(v);
}

static void ks(const char *k, const char *s, long max)
{
	pc(' ');
	ps(k);
	pc('=');
	pq(s, max);
}

static uint32_t fnv_add(uint32_t h, const void *p, long n)
{
	const unsigned char *s = p;
	for (long i = 0; i < n; i++) {
		h ^= s[i];
		h *= 16777619u;
	}
	return h;
}

static uint32_t fnv(const void *p, long n)
{
	return fnv_add(2166136261u, p, n);
}

static void kh(const char *k, uint32_t h)
{
	pc(' ');
	ps(k);
	pc('=');
	px(h, 8);
}

/* ---- contrôles */

static long checks, fails;

static void ck(int ok, const char *what)
{
	checks++;
	if (!ok) {
		fails++;
		ps("ECHEC ");
		ps(what);
		nl();
	}
}

/* ---- le kit */

static long kit;
static long called[P_COUNT_FN];

static long call(int i, long a, long b, long c, long d, long e, long f, long g, long h)
{
	const uint32_t *t = (const uint32_t *)kit;
	fn8 fn = (fn8)(kit + t[2 + i]);
	called[i]++;
	return fn(a, b, c, d, e, f, g, h);
}
#define L(x) ((long)(x))
#define C0(i) call(i, 0, 0, 0, 0, 0, 0, 0, 0)
#define C1(i, a) call(i, L(a), 0, 0, 0, 0, 0, 0, 0)
#define C2(i, a, b) call(i, L(a), L(b), 0, 0, 0, 0, 0, 0)
#define C3(i, a, b, c) call(i, L(a), L(b), L(c), 0, 0, 0, 0, 0)
#define C4(i, a, b, c, d) call(i, L(a), L(b), L(c), L(d), 0, 0, 0, 0)
#define C5(i, a, b, c, d, e) call(i, L(a), L(b), L(c), L(d), L(e), 0, 0, 0)
#define C6(i, a, b, c, d, e, f) call(i, L(a), L(b), L(c), L(d), L(e), L(f), 0, 0)
#define C7(i, a, b, c, d, e, f, g) call(i, L(a), L(b), L(c), L(d), L(e), L(f), L(g), 0)
#define C8(i, a, b, c, d, e, f, g, h) call(i, L(a), L(b), L(c), L(d), L(e), L(f), L(g), L(h))

static long open_kit(const char *name, long min)
{
	long t = sys2(SYS_LIB_OPEN, name, min);
	if (t == -2) { /* pas encore en mémoire : AppKit le lit (comme kits_open) */
		long a = sys2(SYS_LIB_OPEN, "appkit", 32);
		if (a >= 0) {
			const uint32_t *at = (const uint32_t *)a;
			long (*load)(const char *) = (long (*)(const char *))(a + at[2 + AK_F_KIT_LOAD]);
			if (load(name) == 0)
				t = sys2(SYS_LIB_OPEN, name, min);
		}
	}
	return t;
}

/* ---- le faux printd (thread du programme) */

#define LOGN 64
static struct {
	long op;
	uint32_t h;
} svlog[LOGN];
static volatile long svn, svlost;
static volatile int sv_ready;
static long sv_nextid;
static volatile int sv_newfail; /* PD_NEW refusé */
static const char *sv_dir = "/tmp/pktest";

/* le message reçu, sans le pid de l'appelant (+44) */
static uint32_t msg_hash(const unsigned char *m)
{
	uint32_t h = fnv(m, 44);
	return fnv_add(h, m + 48, 512 - 48);
}

static void put_printer(unsigned char *d, const char *name, const char *kind, const char *uri, const char *model,
			int color, int copies, int dpi, int def, uint32_t qual, const char *media)
{
	memset(d, 0, PR_BYTES);
	scpy((char *)d + PR_NAME, name, 64);
	scpy((char *)d + PR_KIND, kind, 8);
	scpy((char *)d + PR_URI, uri, 160);
	scpy((char *)d + PR_MODEL, model, 64);
	st32(d + PR_COLOR, (uint32_t)color);
	st32(d + PR_COPIES, (uint32_t)copies);
	st32(d + PR_DPI, (uint32_t)dpi);
	st32(d + PR_DEFAULT, (uint32_t)def);
	st32(d + PR_QUALITY, qual);
	scpy((char *)d + PR_MEDIA_DEFAULT, media, 64);
}

static const char *const media_pdf[3][2] = {
	{"iso_a4_210x297mm", "A4"}, {"na_letter_8.5x11in", "Lettre US"}, {"iso_a5_148x210mm", "A5"}};
static const char *const media_ipp[2][2] = {{"na_letter_8.5x11in", "Letter"}, {"iso_a4_210x297mm", "A4 (ISO)"}};

static long sv_handle(unsigned char *m)
{
	long op = ld64(m + WM_OP), a = ld64(m + WM_A), b = ld64(m + WM_B), c = ld64(m + WM_C), d = ld64(m + WM_D);
	char *data = (char *)m + WM_DATA;
	switch (op) {
	case PD_NEW: {
		if (sv_newfail)
			return -1;
		long id = ++sv_nextid;
		char *p = data;
		for (const char *s = sv_dir; *s;)
			*p++ = *s++;
		*p++ = '/';
		char t[24];
		int n = 0;
		do
			t[n++] = (char)('0' + id % 10);
		while (id /= 10);
		while (n)
			*p++ = t[--n];
		for (const char *s = ".opj"; *s;)
			*p++ = *s++;
		*p = 0;
		return sv_nextid;
	}
	case PD_SUBMIT:
		return seq(data, "refuse") ? 0 : 1;
	case PD_ABORT:
		return 0;
	case PD_SETUP: {
		/* a orientation, b copies, c qualité, d mono ; imprimante 64, papier 64 */
		char pr[64], me[64];
		scpy(pr, data, 64);
		scpy(me, data + 64, 64);
		if (!pr[0])
			scpy(pr, "PDF", 64);
		long known = 1;
		if (!me[0])
			scpy(me, "iso_a4_210x297mm", 64);
		else if (seq(me, "inconnu"))
			known = 0;
		uint32_t w = 595276, h = 841890, mg = seq(pr, "PDF") ? 0 : 4233;
		if (seq(me, "na_letter_8.5x11in"))
			w = 612000, h = 792000;
		if (a == 1) {
			uint32_t t = w;
			w = h;
			h = t;
		}
		memset(data, 0, 152);
		scpy(data, pr, 64);
		scpy(data + 64, me, 64);
		st32(data + 128, w);
		st32(data + 132, h);
		st32(data + 136, mg);
		st32(data + 140, mg + 1);
		st32(data + 144, mg + 2);
		st32(data + 148, mg + 3);
		*(long *)(m + WM_A) = b < 1 ? 1 : b;
		*(long *)(m + WM_B) = c;
		*(long *)(m + WM_C) = d;
		return known;
	}
	case PD_PRINTER:
		if (a == 0) {
			put_printer((unsigned char *)data, "PDF", "pdf", "", "Fichier PDF", 1, 1, 0, 1, 0x10,
				    "iso_a4_210x297mm");
			return 1;
		}
		if (a == 1) {
			put_printer((unsigned char *)data, "Bureau", "ipp", "ipp://10.0.0.5/ipp/print", "Laser 3000", 1, 99,
				    600, 0, 0x38, "na_letter_8.5x11in");
			return 1;
		}
		if (a == 2) {
			put_printer((unsigned char *)data, "Noir", "ipp", "ipp://10.0.0.6/ipp/print", "Mono", 0, 0, 300, 0,
				    0x28, "iso_a4_210x297mm");
			return 1;
		}
		return 0;
	case PD_MEDIA: {
		const char *const(*t)[2] = seq(data, "PDF") ? media_pdf : media_ipp;
		long n = seq(data, "PDF") ? 3 : seq(data, "Bureau") ? 2 : 0;
		if (a < 0 || a >= n)
			return 0;
		memset(data, 0, 104);
		scpy(data, t[a][0], 64);
		scpy(data + 64, t[a][1], 40);
		return 1;
	}
	case PD_ADD: {
		int ok = data[64] == 'i' && data[65] == 'p' && data[66] == 'p';
		scpy(data, ok ? "Nouvelle imprimante" : "adresse refusee : pas ipp://", 464);
		return ok;
	}
	case PD_REMOVE:
		return seq(data, "Bureau");
	case PD_DEFAULT:
		return seq(data, "Bureau") || seq(data, "PDF");
	case PD_REFRESH: {
		int ok = seq(data, "Bureau");
		scpy(data, ok ? "prete" : "hors ligne (aucune reponse)", 464);
		return ok;
	}
	case PD_SCAN:
		return 3;
	case PD_FOUND:
		if (a < 0 || a >= 3)
			return 0;
		memset(data, 0, PF_BYTES);
		scpy(data + PF_ADDRESS, a == 0 ? "10.0.0.7" : a == 1 ? "10.0.0.8" : "10.0.0.9", 32);
		scpy(data + PF_NAME, a == 0 ? "Salon" : a == 1 ? "Atelier" : "Vieille", 64);
		scpy(data + PF_MODEL, a == 2 ? "Matricielle" : "Jet 12", 64);
		st32(data + PF_USABLE, a != 2);
		return 1;
	case PD_JOB:
		if (a < 0 || a >= 2)
			return 0;
		memset(data, 0, PJI_BYTES);
		st32(data + PJI_ID, (uint32_t)(7 + a));
		st32(data + PJI_STATE, (uint32_t)(a ? 4 : 2));
		st32(data + PJI_PAGES, 3);
		st32(data + PJI_PAGE, (uint32_t)(a ? 3 : 1));
		scpy(data + PJI_PRINTER, a ? "PDF" : "Bureau", 64);
		scpy(data + PJI_TITLE, a ? "Lettre" : "Rapport annuel", 80);
		scpy(data + PJI_MESSAGE, a ? "Enregistre : HOME:/documents/Lettre.pdf" : "Envoi", 96);
		return 1;
	case PD_CANCEL:
		return a == 7;
	case PD_FORGET:
		return 0;
	case PD_FONT: {
		/* a style ; famille -> clé ; a : style à fabriquer ; données : chemin */
		if (seq(data, "Inconnue"))
			return -1;
		uint32_t h = fnv(data, slen(data)) & 0xFFF;
		long real = seq(data, "DejaVu Sans") ? 3 : seq(data, "Serif") ? 1 : 0; /* styles qui existent */
		long key = (long)h * 4 + (a & real);
		*(long *)(m + WM_A) = a & ~real;
		char nm[64];
		scpy(nm, data, 40);
		char *p = data;
#if defined(__x86_64__)
		for (const char *s = "/res/fonts/"; *s;)
#else
		for (const char *s = "/fonts/"; *s;)
#endif
			*p++ = *s++;
		for (const char *s = nm; *s; s++)
			*p++ = *s == ' ' ? '-' : *s;
		if (a & real & 1)
			for (const char *s = "-Bold"; *s;)
				*p++ = *s++;
		for (const char *s = ".ttf"; *s;)
			*p++ = *s++;
		*p = 0;
		return key;
	}
	case PD_MEASURE: {
		long n = slen(data), s = 0;
		for (long i = 0; i < n; i++)
			s += (unsigned char)data[i];
		return s * b / 1000 + n * 7 + (a & 3);
	}
	case PD_METRICS:
		if (!(a & 1))
			return 0;
		*(long *)(m + WM_A) = b * 76 / 100;
		*(long *)(m + WM_B) = b * 24 / 100;
		*(long *)(m + WM_C) = b * 117 / 100 + a % 5;
		return 1;
	}
	(void)c;
	(void)d;
	return 0;
}

static void fake_printd(long arg)
{
	(void)arg;
	static unsigned char msg[512] __attribute__((aligned(16)));
	long ch = sys2(SYS_SVC_REGISTER, "print", 0);
	sv_ready = ch >= 0 ? 1 : (int)(ch < -1000 ? -1000 : ch);
	if (ch < 0)
		sys1(SYS_THREAD_EXIT, 0);
	for (;;) {
		long n = sys3(SYS_SVC_NEXT, ch, msg, 1000);
		long op = ld64(msg + WM_OP);
		if (n <= 0 || (uint64_t)op >= 0xFFF0)
			continue;
		if (op == 99) {
			*(long *)(msg + WM_OP) = 0;
			sys2(SYS_SVC_REPLY, n, msg);
			sys1(SYS_THREAD_EXIT, 0);
		}
		long i = svn;
		if (i < LOGN) {
			svlog[i].op = op;
			svlog[i].h = msg_hash(msg);
			svn = i + 1;
		} else {
			svlost = svlost + 1;
		}
		long r = sv_handle(msg);
		*(long *)(msg + WM_OP) = r;
		sys2(SYS_SVC_REPLY, n, msg);
	}
}

static uint64_t sv_stack[4096] __attribute__((aligned(16)));

/* les messages reçus depuis la dernière fois */
static void svdump(void)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	long n = svn;
	ps("  msg");
	for (long i = 0; i < n; i++) {
		pc(' ');
		pd(svlog[i].op);
		pc(':');
		px(svlog[i].h, 8);
	}
	if (svlost) {
		kv("perdus", svlost);
		svlost = 0;
	}
	nl();
	svn = 0;
}

/* ---- données */

static unsigned char setup[600] __attribute__((aligned(8)));

static void dump_setup(const unsigned char *s)
{
	ps("  setup");
	kv("size", ld32(s + PS_SIZE));
	ks("printer", (const char *)s + PS_PRINTER, 64);
	ks("media", (const char *)s + PS_MEDIA, 64);
	kv("w", (int32_t)ld32(s + PS_PAPER_W));
	kv("h", (int32_t)ld32(s + PS_PAPER_H));
	ps(" marges=");
	for (int i = 0; i < 4; i++) {
		pd((int32_t)ld32(s + PS_MARGIN_L + 4 * i));
		pc(i < 3 ? ',' : ' ');
	}
	kv("ori", (int32_t)ld32(s + PS_ORIENTATION));
	kv("copies", (int32_t)ld32(s + PS_COPIES));
	kv("mono", (int32_t)ld32(s + PS_MONO));
	kv("qual", (int32_t)ld32(s + PS_QUALITY));
	kv("from", (int32_t)ld32(s + PS_FROM));
	kv("to", (int32_t)ld32(s + PS_TO));
	ks("out", (const char *)s + PS_OUTPUT, 256);
	kh("somme", fnv(s, PS_BYTES));
	nl();
}

static uint64_t lcg = 12345;
static uint32_t rnd(void)
{
	lcg = lcg * 6364136223846793005ULL + 1442695040888963407ULL;
	return (uint32_t)(lcg >> 33);
}

static uint32_t img_a[64 * 48];
static uint32_t img_b[300 * 200];
static uint32_t img_c[512 * 512];
static unsigned char fake_ttf[256];
static unsigned char fake_jpeg[300];
static char long_text[1200];

static void make_inputs(void)
{
	for (int y = 0; y < 48; y++)
		for (int x = 0; x < 64; x++)
			img_a[y * 64 + x] = (uint32_t)((x * 4) << 16 | (y * 5) << 8 | ((x + y) & 255));
	for (int y = 0; y < 200; y++)
		for (int x = 0; x < 300; x++) {
			uint32_t a = (uint32_t)((x + y) % 7 == 0 ? 0 : (x * 255 / 299));
			img_b[y * 300 + x] = a << 24 | (uint32_t)(y & 255) << 16 | (uint32_t)(x & 255) << 8 | 0x40;
		}
	for (int i = 0; i < 512 * 512; i++)
		img_c[i] = rnd();
	for (int i = 0; i < 256; i++)
		fake_ttf[i] = (unsigned char)(i * 7 + 3);
	for (int i = 0; i < 300; i++)
		fake_jpeg[i] = (unsigned char)(255 - i);
	/* un long texte : des « é » (2 octets) qui tombent sur la limite de 456 octets */
	long n = 0;
	for (int i = 0; i < 455; i++)
		long_text[n++] = (char)('a' + i % 26);
	for (int i = 0; i < 200; i++) {
		long_text[n++] = (char)0xC3;
		long_text[n++] = (char)0xA9;
	}
	for (int i = 0; i < 100; i++)
		long_text[n++] = 'z';
	long_text[n] = 0;
}

/* ---- 1. sans service */

static void part_noservice(void)
{
	ps("=== sans service\n");
	long out3[3] = {-1, -1, -1};
	ps("dialog(0)");
	kv("r", C2(P_DIALOG, 0, 0));
	kv("setup_paper(0)", C3(P_SETUP_PAPER, 0, 0, 0));
	kv("begin(0)", C2(P_BEGIN, 0, "x"));
	kv("page(0)", C3(P_PAGE, 0, 100, 100));
	kv("end(0)", C1(P_END, 0));
	kv("font(0)", C3(P_FONT, 0, 0, 0));
	kv("text(0)", C7(P_TEXT, 0, 0, 12000, 0, 0, "a", 0));
	kv("text_width(0)", C4(P_TEXT_WIDTH, 0, 0, 12000, "a"));
	nl();
	C1(P_ABORT, 0);
	C1(P_SETUP_DEFAULT, 0);
	/* sans page : les dessins ne font rien (aucun message, aucun fichier) */
	C6(P_RECT, 1, 0, 0, 10, 10, 0);
	C7(P_LINE, 1, 0, 0, 10, 10, 100, 0);
	C7(P_FRAME, 1, 0, 0, 10, 10, 100, 0);
	C3(P_PATH_MOVE, 1, 0, 0);
	C3(P_PATH_LINE, 1, 5, 5);
	C7(P_PATH_CURVE, 1, 1, 2, 3, 4, 5, 6);
	C1(P_PATH_CLOSE, 1);
	C2(P_PATH_FILL, 1, 0);
	C3(P_PATH_STROKE, 1, 0, 0);
	C8(P_IMAGE, 1, img_a, 64, 48, 0, 0, 72000, 54000);
	C8(P_IMAGE_JPEG, 1, fake_jpeg, 300, 10L | 10L << 32, 0, 0, 1000, 1000);
	C8(P_GLYPH, 1, 0, 12000, 0, 0, 5, 'a', 0);
	C2(P_IMAGE_MODE, 0, 0);
	/* polices de données : pas de service */
	ps("font_data");
	kv("a", C3(P_FONT_DATA, 1, fake_ttf, 100));
	kv("a2", C3(P_FONT_DATA, 1, fake_ttf, 200));
	kv("b", C3(P_FONT_DATA, 1, fake_ttf + 1, 100));
	kv("court", C3(P_FONT_DATA, 1, fake_ttf + 2, 11));
	kv("sans_job", C3(P_FONT_DATA, 0, fake_ttf + 2, 100));
	kv("sans_ttf", C3(P_FONT_DATA, 1, 0, 100));
	nl();
	ps("text (police de données, sans page)");
	kv("r", C7(P_TEXT, 1, 0, 12000, 0, 0, "abc", 0));
	kv("w", C4(P_TEXT_WIDTH, 1, 1, 12000, "abc"));
	kv("hors", C4(P_TEXT_WIDTH, 1, 9, 12000, "abc"));
	kv("neg", C4(P_TEXT_WIDTH, 1, -1, 12000, "abc"));
	kv("taille0", C7(P_TEXT, 1, 0, 0, 0, 0, "abc", 0));
	kv("nul", C7(P_TEXT, 1, 0, 1000, 0, 0, 0, 0));
	nl();
	static const long sizes[] = {12000, 0, 1, 99, 100, 7777777, -100, -1};
	for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
		ps("metrics");
		kv("taille", sizes[i]);
		C4(P_FONT_METRICS, 1, 0, sizes[i], out3);
		kv("a", out3[0]);
		kv("d", out3[1]);
		kv("l", out3[2]);
		C4(P_FONT_METRICS, 0, 0, sizes[i] + 1, out3);
		kv("a0", out3[0]);
		C4(P_FONT_METRICS, 1, 55, sizes[i] + 2, out3);
		kv("a55", out3[0]);
		nl();
	}
	C4(P_FONT_METRICS, 1, 0, 1000, 0);
}

/* ---- 2. faux service */

static const char *const pnames[] = {"PDF", "Bureau", "Noir", "Absente", ""};

static void part_service(void)
{
	ps("=== faux service\n");
	unsigned char *s = setup;
	memset(s, 0x5A, sizeof setup);
	ps("setup_default");
	kv("r", C1(P_SETUP_DEFAULT, s));
	nl();
	svdump();
	dump_setup(s);
	static const char *const med[] = {"na_letter_8.5x11in", "inconnu", 0, "iso_a5_148x210mm"};
	for (int o = 0; o < 2; o++)
		for (int i = 0; i < 4; i++) {
			ps("setup_paper");
			ks("media", med[i] ? med[i] : "(0)", 64);
			kv("ori", o);
			kv("r", C3(P_SETUP_PAPER, s, med[i], o));
			nl();
			svdump();
			dump_setup(s);
		}
	/* copies, qualité et imprimante d'avant */
	scpy((char *)s + PS_PRINTER, "Bureau", 64);
	st32(s + PS_COPIES, 0);
	st32(s + PS_QUALITY, PRINT_HIGH);
	st32(s + PS_MONO, 1);
	ps("setup_paper Bureau");
	kv("r", C3(P_SETUP_PAPER, s, 0, 0));
	nl();
	svdump();
	dump_setup(s);

	/* les imprimantes */
	static unsigned char prs[8 * PR_BYTES];
	memset(prs, 0x33, sizeof prs);
	long n = C2(P_PRINTERS, prs, 8);
	ps("printers");
	kv("n", n);
	nl();
	svdump();
	for (long i = 0; i < n; i++) {
		unsigned char *p = prs + i * PR_BYTES;
		ps("  pr");
		ks("name", (char *)p + PR_NAME, 64);
		ks("kind", (char *)p + PR_KIND, 8);
		ks("uri", (char *)p + PR_URI, 160);
		ks("model", (char *)p + PR_MODEL, 64);
		kv("color", ld32(p + PR_COLOR));
		kv("copies", ld32(p + PR_COPIES));
		kv("dpi", ld32(p + PR_DPI));
		kv("def", ld32(p + PR_DEFAULT));
		kv("qual", ld32(p + PR_QUALITY));
		ks("media", (char *)p + PR_MEDIA_DEFAULT, 64);
		kh("somme", fnv(p, PR_BYTES));
		nl();
	}
	kh("reste", fnv(prs + n * PR_BYTES, (8 - n) * PR_BYTES));
	kv("printers(2)", C2(P_PRINTERS, prs, 2));
	kv("printers(0)", C2(P_PRINTERS, prs, 0));
	nl();
	svdump();

	/* les papiers */
	for (int pi = 0; pi < 4; pi++) {
		for (long r = 0; r < 4; r++) {
			char nm[64], lb[40];
			memset(nm, 0x41, sizeof nm);
			memset(lb, 0x42, sizeof lb);
			ps("media");
			ks("pr", pnames[pi], 64);
			kv("rang", r);
			kv("r", C6(P_PRINTER_MEDIA, pnames[pi], r, nm, 64, lb, 40));
			ks("nom", nm, 64);
			ks("lib", lb, 40);
			nl();
		}
	}
	{
		char nm[8], lb[8];
		memset(nm, 0x41, sizeof nm);
		memset(lb, 0x42, sizeof lb);
		ps("media courts");
		kv("r", C6(P_PRINTER_MEDIA, "PDF", 1, nm, 5, lb, 1));
		ks("nom", nm, 8);
		kh("lib", fnv(lb, 8));
		kv("sans", C6(P_PRINTER_MEDIA, "PDF", 2, 0, 64, 0, 40));
		kv("taille0", C6(P_PRINTER_MEDIA, "PDF", 0, nm, 0, lb, 0));
		kh("nom", fnv(nm, 8));
		nl();
	}
	svdump();

	/* ajouter, retirer, par défaut, état */
	{
		char out[64];
		memset(out, 0x43, sizeof out);
		ps("add");
		kv("r", C4(P_PRINTER_ADD, "Neuve", "ipp://10.0.0.9/ipp/print", out, 64));
		ks("out", out, 64);
		kv("r2", C4(P_PRINTER_ADD, "Autre", "http://10.0.0.9", out, 64));
		ks("out", out, 64);
		kv("r3", C4(P_PRINTER_ADD, "Autre", "ipp://x", 0, 64));
		kv("r4", C4(P_PRINTER_ADD, "Autre", "ipp://x", out, 6));
		ks("out", out, 64);
		kv("r5", C4(P_PRINTER_ADD, 0, 0, out, 64));
		ks("out", out, 64);
		nl();
		svdump();
		for (int pi = 0; pi < 5; pi++) {
			ps("named");
			ks("pr", pnames[pi], 64);
			kv("remove", C1(P_PRINTER_REMOVE, pnames[pi]));
			kv("default", C1(P_PRINTER_DEFAULT, pnames[pi]));
			kv("status", C3(P_PRINTER_STATUS, pnames[pi], out, 64));
			ks("out", out, 64);
			nl();
		}
		kv("status court", C3(P_PRINTER_STATUS, "Bureau", out, 3));
		ks("out", out, 64);
		kv("status sans", C3(P_PRINTER_STATUS, "Bureau", 0, 64));
		nl();
		svdump();
	}

	/* recherche (faux service : aucun réseau), file */
	{
		static unsigned char fd[4 * PF_BYTES];
		memset(fd, 0x44, sizeof fd);
		long k = C2(P_PRINTERS_FIND, fd, 4);
		ps("find");
		kv("n", k);
		for (long i = 0; i < k; i++) {
			ks("adr", (char *)fd + i * PF_BYTES + PF_ADDRESS, 32);
			ks("nom", (char *)fd + i * PF_BYTES + PF_NAME, 64);
			ks("mod", (char *)fd + i * PF_BYTES + PF_MODEL, 64);
			kv("ok", ld32(fd + i * PF_BYTES + PF_USABLE));
		}
		kh("somme", fnv(fd, sizeof fd));
		kv("find(2)", C2(P_PRINTERS_FIND, fd, 2));
		kv("find(0)", C2(P_PRINTERS_FIND, fd, 0));
		nl();
		svdump();
		static unsigned char jb[4 * PJI_BYTES];
		memset(jb, 0x45, sizeof jb);
		k = C2(P_JOBS, jb, 4);
		ps("jobs");
		kv("n", k);
		for (long i = 0; i < k; i++) {
			unsigned char *j = jb + i * PJI_BYTES;
			kv("id", ld32(j + PJI_ID));
			kv("st", ld32(j + PJI_STATE));
			kv("pages", ld32(j + PJI_PAGES));
			kv("page", ld32(j + PJI_PAGE));
			ks("pr", (char *)j + PJI_PRINTER, 64);
			ks("titre", (char *)j + PJI_TITLE, 80);
			ks("msg", (char *)j + PJI_MESSAGE, 96);
		}
		kh("somme", fnv(jb, sizeof jb));
		kv("cancel7", C1(P_JOB_CANCEL, 7));
		kv("cancel5", C1(P_JOB_CANCEL, 5));
		kv("forget", C0(P_JOBS_FORGET));
		nl();
		svdump();
	}

	/* polices et mesures (travail forgé : 1) */
	{
		static const char *const fam[] = {0, "DejaVu Sans", "Serif", "Mono", "Inconnue"};
		long f[5][4];
		for (int i = 0; i < 5; i++) {
			ps("font");
			ks("fam", fam[i] ? fam[i] : "(0)", 64);
			for (int st = 0; st < 4; st++) {
				f[i][st] = C3(P_FONT, 1, fam[i], st | (st == 3 ? 4 : 0));
				pc(' ');
				pd(f[i][st]);
			}
			nl();
			svdump();
		}
		ps("font encore");
		kv("r", C3(P_FONT, 1, "DejaVu Sans", PRINT_BOLD));
		kv("sans_job", C3(P_FONT, 0, "DejaVu Sans", 0));
		nl();
		svdump();
		static const char *const texts[] = {"Bonjour", "", "été à la plage", "x"};
		for (int i = 0; i < 4; i++) {
			for (int k = 0; k < 3; k++) {
				long fo = k == 0 ? f[1][0] : k == 1 ? f[2][1] : 1; /* 1 : une police de données */
				ps("text");
				ks("t", texts[i], 64);
				kv("f", fo);
				kv("r", C7(P_TEXT, 1, fo, 12000 + i, 72000, 80000, texts[i], 0x123456));
				kv("w", C4(P_TEXT_WIDTH, 1, fo, 9000, texts[i]));
				nl();
				svdump();
			}
		}
		ps("text long");
		kv("r", C7(P_TEXT, 1, f[1][0], 10000, 0, 0, long_text, 0));
		kv("w", C4(P_TEXT_WIDTH, 1, f[1][1], 10000, long_text));
		nl();
		svdump();
		long out3[3];
		for (int i = 0; i < 5; i++)
			for (int st = 0; st < 2; st++) {
				long fo = f[i][st];
				out3[0] = out3[1] = out3[2] = -7;
				C4(P_FONT_METRICS, 1, fo, 12000 + st, out3);
				ps("metrics");
				kv("f", fo);
				kv("a", out3[0]);
				kv("d", out3[1]);
				kv("l", out3[2]);
				nl();
				svdump();
			}
		/* jusqu'à 32 polices */
		ps("fonts pleines");
		for (int i = 0; i < 34; i++)
			pc(' '), pd(C3(P_FONT_DATA, 1, fake_ttf + 16 + i, 50));
		kv("fam", C3(P_FONT, 1, "Mono", 2));
		nl();
		svdump();
	}

	/* un travail forgé (sans print_begin, sans fichier) : les pages vont au
	 * tampon du kit, print_end les soumet quand même */
	{
		ps("forge");
		kv("page", C3(P_PAGE, 1, 0, 0));
		C6(P_RECT, 1, 10, 20, 30, 40, 0xABCDEF12);
		kv("page2", C3(P_PAGE, 1, 612000, 792000));
		C8(P_GLYPH, 1, 0, 12000, 0, 0, 5, 'a', 0);
		kv("end", C1(P_END, 1));
		kv("end2", C1(P_END, 1));
		kv("abort", C1(P_ABORT, 1));
		nl();
		svdump();
	}
}

/* ---- 3. fichiers */

static unsigned char filebuf[4 << 20];

static long read_file(const char *path, unsigned char *buf, long cap)
{
	long fd = ak_call(AK_F_OPEN, (long)path, 1, 0);
	if (fd < 0)
		return fd;
	long n = 0;
	for (;;) {
		long r = ak_call(AK_F_READ, fd, (long)(buf + n), cap - n > 65536 ? 65536 : cap - n);
		if (r <= 0)
			break;
		n += r;
		if (n >= cap)
			break;
	}
	ak_call(AK_F_CLOSE, fd, 0, 0);
	return n;
}

/* le fichier des pages du travail id : taille, somme, enregistrements */
static void dump_opj(long id)
{
	char path[64];
	char *p = path;
	for (const char *s = sv_dir; *s;)
		*p++ = *s++;
	*p++ = '/';
	char t[24];
	int k = 0;
	do
		t[k++] = (char)('0' + id % 10);
	while (id /= 10);
	while (k)
		*p++ = t[--k];
	for (const char *s = ".opj"; *s;)
		*p++ = *s++;
	*p = 0;
	long n = read_file(path, filebuf, sizeof filebuf);
	ps("  opj");
	ks("f", path, 64);
	kv("octets", n);
	if (n < 4) {
		nl();
		return;
	}
	kh("somme", fnv(filebuf, n));
	ks("tete", (char *)filebuf, 4);
	nl();
	long o = 4;
	while (o + 8 <= n) {
		uint32_t kind = ld32(filebuf + o), len = ld32(filebuf + o + 4);
		ps("    rec");
		kv("k", kind);
		kv("n", len);
		if (o + 8 + (long)len > n) {
			ps(" (coupe)");
			nl();
			break;
		}
		const unsigned char *d = filebuf + o + 8;
		int words = len < 36 ? (int)len / 4 : 9;
		if (kind == 4 || kind == 3 || kind == 2 || kind == 9 || kind == 7 || kind == 6 || kind == 5 || kind == 1 ||
		    kind == 10)
			for (int i = 0; i < words; i++)
				pc(i ? ',' : ' '), pd((int32_t)ld32(d + 4 * i));
		kh("h", fnv(d, len));
		nl();
		o += 8 + len;
	}
	if (o != n) {
		kv("    reste", n - o);
		nl();
	}
}

static long job_begin(const char *title)
{
	long j = C2(P_BEGIN, setup, title);
	ps("begin");
	ks("t", title ? title : "(0)", 64);
	kv("ok", j != 0);
	nl();
	svdump();
	return j;
}

static void part_files(void)
{
	ps("=== fichiers (/tmp)\n");
	ak_call(AK_F_MKDIR, (long)sv_dir, 0, 0);
	unsigned char *s = setup;
	C1(P_SETUP_DEFAULT, s);
	scpy((char *)s + PS_OUTPUT, "/tmp/pktest/sortie.pdf", 256);
	svdump();
	/* 1. un travail complet */
	long j = job_begin("Essai complet");
	if (j) {
		ps("deux travaux");
		kv("r", C2(P_BEGIN, s, "autre"));
		nl();
		kv("page", C3(P_PAGE, j, 0, 0));
		long f0 = C3(P_FONT, j, "DejaVu Sans", 0);
		long f1 = C3(P_FONT, j, "DejaVu Sans", PRINT_BOLD | PRINT_ITALIC);
		long fd = C3(P_FONT_DATA, j, fake_ttf, 256);
		kv("f0", f0);
		kv("f1", f1);
		kv("fd", fd);
		nl();
		svdump();
		C6(P_RECT, j, 1000, 2000, 30000, 40000, 0xFF336699);
		C6(P_RECT, j, 1000, 2000, 0, 40000, 0);
		C6(P_RECT, j, 1000, 2000, 30000, -5, 0);
		C7(P_LINE, j, 5000, 6000, 5000, 1000, 800, 0x111111);  /* verticale */
		C7(P_LINE, j, 9000, 6000, 1000, 6000, 801, 0x222222);  /* horizontale */
		C7(P_LINE, j, 5000, 5000, 5000, 5000, 800, 0x333333);  /* un point */
		C7(P_LINE, j, 1000, 2000, 30000, 40000, 0, 0x444444);  /* en biais, épaisseur 0 */
		C7(P_LINE, j, 1000, 2000, 30000, 40000, 1500, -1);
		C7(P_FRAME, j, 10000, 10000, 50000, 30000, 600, 0x555555);
		C3(P_PATH_MOVE, j, 100, 200);
		C3(P_PATH_LINE, j, 300, 400);
		C7(P_PATH_CURVE, j, 1, 2, 3, 4, 5, 6);
		C1(P_PATH_CLOSE, j);
		C2(P_PATH_FILL, j, 0x12ABCDEF);
		C3(P_PATH_MOVE, j, 7, 8);
		C2(P_PATH_FILL, j, 0x777777); /* un seul point : rien */
		C3(P_PATH_MOVE, j, 10, 10);
		C3(P_PATH_LINE, j, 20, 20);
		C3(P_PATH_STROKE, j, 0, 0x888888);
		C3(P_PATH_MOVE, j, 10, 10);
		C3(P_PATH_LINE, j, 20, 30);
		C3(P_PATH_STROKE, j, 333, 0x999999);
		for (int i = 0; i < 9000; i++) /* plus que 8192 points */
			C3(P_PATH_LINE, j, i, -i);
		C3(P_PATH_STROKE, j, 100, 0xAAAAAA);
		C8(P_GLYPH, j, f0, 12000, 1000, 2000, 36, 'A', 0x1F2F3F4F);
		C8(P_GLYPH, j, f1 | PRINT_BOLD << 16, 12000, 1000, 2000, 37, 'B', 0);
		C8(P_GLYPH, j, 31, 12000, 1000, 2000, 37, 'B', 0); /* police inconnue */
		C8(P_GLYPH, j, fd | 0x30000, 9000, 1, 2, 3, 4, 5);
		ps("text");
		kv("r", C7(P_TEXT, j, f0, 11000, 7000, 8000, "Bonjour à tous", 0xFF00FF00));
		kv("r2", C7(P_TEXT, j, fd, 11000, 7000, 8000, "data", 0));
		kv("r3", C7(P_TEXT, j, 17, 11000, 7000, 8000, "inconnue", 0));
		nl();
		svdump();
		C2(P_IMAGE_MODE, j, 0);
		C8(P_IMAGE, j, img_a, 64, 48, 1000, 1000, 72000, 54000); /* 300 dpi : 300 x 225 voulus, k = 1 */
		C8(P_IMAGE, j, img_b, 300, 200, 1000, 1000, 24000, 16000); /* k = 3 */
		C2(P_IMAGE_MODE, j, PRINT_IMG_ALPHA);
		C8(P_IMAGE, j, img_b, 300, 200, 1000, 1000, 24000, 16000);
		C8(P_IMAGE, j, img_b, 300, 200, 1000, 1000, 2400, 16000); /* k = 3 : la hauteur décide */
		C8(P_IMAGE, j, img_b, 300, 200, 1000, 1000, 0, 16000);    /* rien */
		C8(P_IMAGE, j, 0, 300, 200, 1000, 1000, 24000, 16000);    /* rien */
		C8(P_IMAGE_JPEG, j, fake_jpeg, 300, 640L | 480L << 32, 1, 2, 3, 4);
		kv("page2", C3(P_PAGE, j, 300000, 400000));
		C2(P_IMAGE_MODE, j, 0);
		C8(P_IMAGE, j, img_c, 512, 512, 0, 0, 122880, 122880); /* gros : plus que le tampon */
		C6(P_RECT, j, 1, 1, 1, 1, 1);
		kv("page3", C3(P_PAGE, j, -1, 0));
		nl();
		long id = C1(P_END, j);
		ps("end");
		kv("id", id);
		nl();
		svdump();
		if (id > 0)
			dump_opj(id);
	}
	/* 2. pages choisies : 2 à 3 */
	st32(s + PS_FROM, 2);
	st32(s + PS_TO, 3);
	st32(s + PS_COPIES, 0);
	st32(s + PS_QUALITY, PRINT_DRAFT);
	st32(s + PS_MONO, 1);
	j = job_begin(0);
	if (j) {
		ps("pages");
		for (int i = 0; i < 5; i++) {
			pc(' ');
			pd(C3(P_PAGE, j, 1000 + i, 2000));
			C6(P_RECT, j, i, i, 10, 10, i);
		}
		long id = C1(P_END, j);
		kv("id", id);
		nl();
		svdump();
		if (id > 0)
			dump_opj(id);
	}
	/* 3. depuis la 4e page seulement */
	st32(s + PS_FROM, 4);
	st32(s + PS_TO, 0);
	j = job_begin("");
	if (j) {
		ps("pages");
		for (int i = 0; i < 6; i++)
			pc(' '), pd(C3(P_PAGE, j, 0, 0));
		long id = C1(P_END, j);
		kv("id", id);
		nl();
		svdump();
		if (id > 0)
			dump_opj(id);
	}
	/* 4. aucune page : refusé */
	st32(s + PS_FROM, 9);
	j = job_begin("vide");
	if (j) {
		C3(P_PAGE, j, 0, 0);
		ps("vide");
		kv("end", C1(P_END, j));
		nl();
		svdump();
		dump_opj(sv_nextid);
	}
	st32(s + PS_FROM, 0);
	/* 5. abandon */
	j = job_begin("abandon");
	if (j) {
		C3(P_PAGE, j, 0, 0);
		C6(P_RECT, j, 1, 2, 3, 4, 5);
		C1(P_ABORT, j);
		ps("abort");
		kv("page apres", C3(P_PAGE, j, 0, 0));
		nl();
		svdump();
		dump_opj(sv_nextid);
	}
	/* 6. soumission refusée */
	scpy((char *)s + PS_PRINTER, "refuse", 64);
	j = job_begin("refus");
	if (j) {
		C3(P_PAGE, j, 0, 0);
		ps("refus");
		kv("end", C1(P_END, j));
		nl();
		svdump();
	}
	/* 7. un réglage plus court (une version plus ancienne) */
	st32(s + PS_SIZE, 70);
	j = job_begin("court");
	if (j) {
		kv("page", C3(P_PAGE, j, 0, 0));
		long id = C1(P_END, j);
		kv("id", id);
		nl();
		svdump();
		if (id > 0)
			dump_opj(id);
	}
	st32(s + PS_SIZE, PS_BYTES);
	/* 8. le service refuse un travail */
	sv_newfail = 1;
	j = job_begin("pas de file");
	sv_newfail = 0;
	/* 9. un fichier qui ne peut s'ouvrir */
	sv_dir = "/tmp/pktest/absent/sous";
	j = job_begin("pas de fichier");
	sv_dir = "/tmp/pktest";
	if (j)
		C1(P_END, j);
}

/* ---- 4. le vrai printd (-r) */

static void part_real(void)
{
	ps("=== vrai printd (imprimante PDF)\n");
	unsigned char *s = setup;
	memset(s, 0, sizeof setup);
	ps("setup_default");
	kv("r", C1(P_SETUP_DEFAULT, s));
	nl();
	dump_setup(s);
	static unsigned char prs[12 * PR_BYTES];
	long n = C2(P_PRINTERS, prs, 12);
	ps("printers");
	kv("n", n);
	for (long i = 0; i < n; i++) {
		ks("name", (char *)prs + i * PR_BYTES + PR_NAME, 64);
		ks("kind", (char *)prs + i * PR_BYTES + PR_KIND, 8);
	}
	nl();
	for (long r = 0; r < 6; r++) {
		char nm[64], lb[40];
		ps("media");
		kv("r", C6(P_PRINTER_MEDIA, "PDF", r, nm, 64, lb, 40));
		ks("nom", nm, 64);
		ks("lib", lb, 40);
		nl();
	}
	ps("setup_paper letter paysage");
	kv("r", C3(P_SETUP_PAPER, s, "na_letter_8.5x11in", PRINT_LANDSCAPE));
	nl();
	dump_setup(s);
	C1(P_SETUP_DEFAULT, s);
	scpy((char *)s + PS_PRINTER, "PDF", 64);
	scpy((char *)s + PS_OUTPUT, "/tmp/pktest-reel.pdf", 256);
	long j = C2(P_BEGIN, s, "Essai PrinterKit");
	ps("begin");
	kv("ok", j != 0);
	nl();
	if (!j)
		return;
	C3(P_PAGE, j, 0, 0);
	long f0 = C3(P_FONT, j, "DejaVu Sans", 0);
	long f1 = C3(P_FONT, j, "DejaVu Sans", PRINT_BOLD);
	long out3[3] = {0, 0, 0};
	C4(P_FONT_METRICS, j, f0, 12000, out3);
	ps("polices");
	kv("f0", f0);
	kv("f1", f1);
	kv("a", out3[0]);
	kv("d", out3[1]);
	kv("l", out3[2]);
	kv("w", C4(P_TEXT_WIDTH, j, f0, 12000, "Bonjour à tous"));
	kv("t", C7(P_TEXT, j, f1, 20000, 72000, 100000, "PrinterKit : page d'essai", 0x202020));
	kv("t2", C7(P_TEXT, j, f0, 11000, 72000, 130000, "été, hiver, ça va", 0x4992A7));
	nl();
	C6(P_RECT, j, 72000, 150000, 200000, 50000, 0xCFE1F5);
	C7(P_FRAME, j, 72000, 150000, 200000, 50000, 1000, 0x2E6DB4);
	C7(P_LINE, j, 72000, 220000, 400000, 260000, 1500, 0x445566);
	C3(P_PATH_MOVE, j, 300000, 300000);
	C7(P_PATH_CURVE, j, 350000, 250000, 400000, 350000, 450000, 300000);
	C3(P_PATH_LINE, j, 450000, 400000);
	C1(P_PATH_CLOSE, j);
	C2(P_PATH_FILL, j, 0x88AA22);
	C2(P_IMAGE_MODE, j, PRINT_IMG_ALPHA);
	C8(P_IMAGE, j, img_b, 300, 200, 72000, 420000, 150000, 100000);
	C8(P_GLYPH, j, f0, 30000, 72000, 600000, 36, 'A', 0x000000);
	C3(P_PAGE, j, 0, 0);
	C7(P_TEXT, j, f0, 12000, 72000, 100000, "Deuxieme page", 0);
	long id = C1(P_END, j);
	ps("end");
	kv("id>0", id > 0);
	nl();
	if (id <= 0)
		return;
	/* le travail fini (60 s au plus) */
	static unsigned char jb[16 * PJI_BYTES];
	long state = -1;
	for (int t = 0; t < 600 && state != 4 && state != 5 && state != 6; t++) {
		long k = C2(P_JOBS, jb, 16);
		for (long i = 0; i < k; i++)
			if ((long)ld32(jb + i * PJI_BYTES + PJI_ID) == id)
				state = ld32(jb + i * PJI_BYTES + PJI_STATE);
		if (state != 4 && state != 5 && state != 6)
			sys1(SYS_SLEEP, 100);
	}
	ps("travail");
	kv("etat", state);
	nl();
	long k = C2(P_JOBS, jb, 16);
	for (long i = 0; i < k; i++)
		if ((long)ld32(jb + i * PJI_BYTES + PJI_ID) == id) {
			ps("  job");
			kv("pages", ld32(jb + i * PJI_BYTES + PJI_PAGES));
			ks("pr", (char *)jb + i * PJI_BYTES + PJI_PRINTER, 64);
			ks("titre", (char *)jb + i * PJI_BYTES + PJI_TITLE, 80);
			ks("msg", (char *)jb + i * PJI_BYTES + PJI_MESSAGE, 96);
			nl();
		}
	long sz = read_file("/tmp/pktest-reel.pdf", filebuf, sizeof filebuf);
	ps("pdf");
	kv("octets", sz);
	if (sz > 0)
		kh("somme", fnv(filebuf, sz));
	nl();
}

/* ---- programme */

static int has_flag(const char *line, char f)
{
	if (!line)
		return 0;
	for (long i = 0; line[i]; i++)
		if (line[i] == '-' && line[i + 1] == f && (line[i + 2] == 0 || line[i + 2] == ' '))
			return 1;
	return 0;
}

/* la valeur de « -x VALEUR » (-s : tout le reste de la ligne, espaces compris) */
static int flag_arg(const char *line, char f, char *out, long cap)
{
	if (!line)
		return 0;
	for (long i = 0; line[i]; i++)
		if (line[i] == '-' && line[i + 1] == f && line[i + 2] == ' ') {
			const char *p = line + i + 3;
			while (*p == ' ' || *p == '"') /* (bash garde les guillemets d'un nom à espaces) */
				p++;
			long n = 0;
			while (p[n] && p[n] != '"' && (p[n] != ' ' || f == 's') && n < cap - 1) {
				out[n] = p[n];
				n++;
			}
			out[n] = 0;
			return n > 0;
		}
	return 0;
}

static void run(const char *line)
{
	ak = sys2(SYS_LIB_OPEN, "appkit", 32);
	char path[256];
	/* -L ARGUMENTS : lance /sys/pktest ARGUMENTS (bash refuse un programme de
	 * /sys ; f_spawn d'AppKit le fait lancer par init2, avec ses droits) */
	for (long i = 0; line && line[i]; i++)
		if (line[i] == '-' && line[i + 1] == 'L' && line[i + 2] == ' ') {
			int have = flag_arg(line + i, 'o', path, sizeof path);
			if (have)
				ak_call(AK_F_UNLINK, (long)path, 0, 0);
			long pid = ak_call(AK_F_SPAWN, (long)"/sys/pktest", (long)(line + i + 3), 0);
			if (pid <= 0 || !have) {
				ps("lance");
				kv("pid>0", pid > 0);
				nl();
				flush();
				sys1(SYS_EXIT, pid > 0 ? 0 : 1);
			}
			/* attend la fin (« pktest fini » au bout du fichier, 5 min au plus), puis le montre */
			long n = 0;
			for (int t = 0; t < 600; t++) {
				sys1(SYS_SLEEP, 500);
				n = read_file(path, filebuf, sizeof filebuf - 1);
				if (n >= 12 && !memcmp(filebuf + n - 12, "pktest fini\n", 12))
					break;
			}
			for (long k = 0; k < n; k++)
				pc((char)filebuf[k]);
			if (n < 12 || memcmp(filebuf + n - 12, "pktest fini\n", 12))
				ps("\n(pas fini)\n");
			flush();
			sys1(SYS_EXIT, 0);
		}
	if (flag_arg(line, 'o', path, sizeof path)) {
		out_fd = ak_call(AK_F_OPEN, (long)path, 2 | 4 | 8, 0);
		if (out_fd < 0)
			out_fd = -1;
	}
	if (flag_arg(line, 's', path, sizeof path)) {
		long n = read_file(path, filebuf, sizeof filebuf);
		ps("fichier");
		ks("f", path, 256);
		kv("octets", n);
		if (n > 0)
			kh("somme", fnv(filebuf, n));
		nl();
		flush();
		sys1(SYS_EXIT, 0);
	}
	ps("pktest : PrinterKit\n");
	kit = open_kit("printerkit", 1);
	if (kit < 0) {
		ps("printerkit absent : ");
		pd(kit);
		nl();
		flush();
		sys1(SYS_EXIT, 127);
	}
	const uint32_t *t = (const uint32_t *)kit;
	ps("table magic=");
	px(t[0], 8);
	ps(" count=");
	pd(t[1]);
	nl();
	if (t[0] != KIT_MAGIC || t[1] < P_COUNT_FN) {
		ps("table invalide\n");
		flush();
		sys1(SYS_EXIT, 126);
	}
	make_inputs();
	part_noservice();
	flush();
	if (has_flag(line, 'r')) {
		part_real();
	} else {
		sys3(SYS_THREAD_CREATE, fake_printd, &sv_stack[4096], 0);
		while (!sv_ready)
			sys1(SYS_YIELD, 0);
		if (sv_ready < 0) {
			ps("pas de faux service (");
			pd(sv_ready);
			ps(") : droits de /sys, sans printd\n");
		} else {
			part_service();
			flush();
			for (int i = 0; i < P_COUNT_FN; i++)
				if (!called[i]) {
					ps("jamais appelee : ");
					ps(fn_names[i]);
					nl();
				}
			ck(svn == 0, "messages en attente");
			ps("fin : ");
			pd(checks);
			ps(" controles, ");
			pd(fails);
			ps(" echecs\n");
			flush();
			if (has_flag(line, 'f'))
				part_files();
			unsigned char m[512] = {99};
			sys2(SYS_SVC_CALL, sys2(SYS_SVC_OPEN, "print", 0), m);
		}
	}
	if (out_fd >= 0)
		ps("pktest fini\n"); /* (le lanceur -L attend cette ligne) */
	flush();
	if (out_fd >= 0)
		ak_call(AK_F_CLOSE, out_fd, 0, 0);
	sys1(SYS_EXIT, fails > 125 ? 125 : fails);
}

#if defined(__x86_64__)
__attribute__((force_align_arg_pointer, noreturn))
#else
__attribute__((noreturn))
#endif
void _start(const char *line) /* x0, rdi : la ligne d'arguments (0 : aucune) */
{
	run(line);
	for (;;)
		;
}
