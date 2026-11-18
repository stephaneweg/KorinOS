/* mediakit : ce que libvpx, dav1d, libopus et la bibliotheque av prennent a la
 * bibliotheque C, sans newlib (ses variables par thread -- errno -- demandent
 * TPIDR_EL0, que les programmes en assembleur n'ont pas ; et le kit n'a ni
 * crt0 ni tas de newlib). Compile avec -fno-builtin
 * -fno-tree-loop-distribute-patterns.
 *
 * Le tas : regions VM_MAP du noyau (peuplees a la demande, docs/ABI.md 0x06) ;
 * petits blocs (<= 64 Kio) en classes de puissances de deux taillees dans des
 * morceaux de 1 Mio (listes libres par classe, jamais rendus) ; gros blocs : une
 * region chacun, rendue par VM_UNMAP. En-tete de 16 octets devant chaque bloc.
 * Un verrou tournant : le kit peut etre appele de plusieurs threads.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "mk_sys.h"

/* ---- memoire, chaines --------------------------------------------------------------------- */

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *a = d; const unsigned char *b = s;
	if ((((uintptr_t)a | (uintptr_t)b) & 7) == 0) {
		for (; n >= 32; n -= 32, a += 32, b += 32) {
			uint64_t x0 = ((const uint64_t *)b)[0], x1 = ((const uint64_t *)b)[1];
			uint64_t x2 = ((const uint64_t *)b)[2], x3 = ((const uint64_t *)b)[3];
			((uint64_t *)a)[0] = x0; ((uint64_t *)a)[1] = x1;
			((uint64_t *)a)[2] = x2; ((uint64_t *)a)[3] = x3;
		}
		for (; n >= 8; n -= 8, a += 8, b += 8) *(uint64_t *)a = *(const uint64_t *)b;
	}
	while (n--) *a++ = *b++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d; const unsigned char *b = s;
	if (a == b || n == 0) return d;
	if (a < b || a >= b + n) return memcpy(d, s, n);
	a += n; b += n;
	while (n--) *--a = *--b;
	return d;
}

void *memset(void *d, int c, size_t n)
{
	unsigned char *a = d;
	uint64_t v = (unsigned char)c;
	v |= v << 8; v |= v << 16; v |= v << 32;
	while (n && ((uintptr_t)a & 7)) { *a++ = (unsigned char)c; n--; }
	for (; n >= 8; n -= 8, a += 8) *(uint64_t *)a = v;
	while (n--) *a++ = (unsigned char)c;
	return d;
}

int memcmp(const void *x, const void *y, size_t n)
{
	const unsigned char *a = x, *b = y;
	for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
	return 0;
}

void *memchr(const void *s, int c, size_t n)
{
	const unsigned char *a = s;
	for (; n; n--, a++) if (*a == (unsigned char)c) return (void *)a;
	return 0;
}

size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
		if (!*a) return 0;
	}
	return 0;
}

char *strchr(const char *s, int c)
{
	for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; }
}

char *strrchr(const char *s, int c)
{
	const char *r = 0;
	for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}

char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)); return r; }

char *strncpy(char *d, const char *s, size_t n)
{
	size_t i = 0;
	for (; i < n && s[i]; i++) d[i] = s[i];
	for (; i < n; i++) d[i] = 0;
	return d;
}

char *strstr(const char *h, const char *n)
{
	size_t l = strlen(n);
	if (!l) return (char *)h;
	for (; *h; h++) if (*h == *n && !strncmp(h, n, l)) return (char *)h;
	return 0;
}

int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isalpha(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || ((c | 32) >= 'a' && (c | 32) <= 'f'); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isprint(int c) { return c >= 32 && c < 127; }

long strtol(const char *s, char **end, int base)
{
	long v = 0; int neg = 0;
	while (isspace((unsigned char)*s)) s++;
	if (*s == '-' || *s == '+') neg = *s++ == '-';
	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x') { s += 2; base = 16; }
	else if (base == 0) base = s[0] == '0' ? 8 : 10;
	for (;; s++) {
		int c = (unsigned char)*s, d;
		if (isdigit(c)) d = c - '0';
		else if (isalpha(c)) d = (c | 32) - 'a' + 10;
		else break;
		if (d >= base) break;
		v = v * base + d;
	}
	if (end) *end = (char *)s;
	return neg ? -v : v;
}
unsigned long strtoul(const char *s, char **end, int base) { return (unsigned long)strtol(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, 0, 10); }

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

/* tri (libvpx, dav1d : petits tableaux) : tri par insertion, puis Shell */
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
	unsigned char *b = base, tmp[256];
	size_t gap, i, j;
	if (sz > sizeof tmp) return;
	for (gap = n / 2; gap > 0; gap /= 2)
		for (i = gap; i < n; i++) {
			memcpy(tmp, b + i * sz, sz);
			for (j = i; j >= gap && cmp(b + (j - gap) * sz, tmp) > 0; j -= gap)
				memcpy(b + j * sz, b + (j - gap) * sz, sz);
			memcpy(b + j * sz, tmp, sz);
		}
}

/* ---- sortie : rien (le kit parle par mk_log), formats minimaux ------------------------------ */

/* vsnprintf minimal : %s %c %d %i %u %x %X %p %%, largeur, 0, -, l, ll, z, .precision (s) */
int vsnprintf(char *buf, size_t size, const char *f, va_list ap)
{
	size_t o = 0;
#define PUT(ch) do { if (o + 1 < size) buf[o] = (ch); o++; } while (0)
	for (; *f; f++) {
		if (*f != '%') { PUT(*f); continue; }
		f++;
		int zero = 0, width = 0, lng = 0, left = 0, prec = -1;
		for (;; f++) { if (*f == '0') zero = 1; else if (*f == '-') left = 1; else break; }
		if (*f == '*') { width = va_arg(ap, int); f++; }
		while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
		if (*f == '.') {
			f++; prec = 0;
			if (*f == '*') { prec = va_arg(ap, int); f++; }
			while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
		}
		while (*f == 'l' || *f == 'z' || *f == 'h' || *f == 'j') { if (*f != 'h') lng = 1; f++; }
		char tmp[24]; int n = 0, neg = 0; unsigned long long v;
		switch (*f) {
		case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
			int l = 0; while (s[l] && (prec < 0 || l < prec)) l++;
			if (!left) for (; l < width; width--) PUT(' ');
			for (int i = 0; i < l; i++) PUT(s[i]);
			if (left) for (; l < width; width--) PUT(' ');
			continue; }
		case 'c': PUT((char)va_arg(ap, int)); continue;
		case '%': PUT('%'); continue;
		case 'd': case 'i': { long long x = lng ? va_arg(ap, long) : va_arg(ap, int);
			if (x < 0) { neg = 1; v = (unsigned long long)(-x); } else v = (unsigned long long)x;
			do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
			break; }
		case 'u': v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
			do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
			break;
		case 'p': lng = 1; /* fallthrough */
		case 'x': case 'X': { const char *dg = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
			v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
			do { tmp[n++] = dg[v & 15]; v >>= 4; } while (v);
			break; }
		case 'f': case 'g': case 'e': { double x = va_arg(ap, double);	/* (rare : entier) */
			if (x < 0) { neg = 1; x = -x; } v = (unsigned long long)x;
			do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
			break; }
		default: PUT('%'); PUT(*f); continue;
		}
		if (neg) { if (zero) { PUT('-'); width--; } else tmp[n++] = '-'; }
		if (!left) for (; n < width; width--) PUT(zero ? '0' : ' ');
		int w0 = n;
		while (n) PUT(tmp[--n]);
		if (left) for (; w0 < width; width--) PUT(' ');
	}
	if (size) buf[o < size ? o : size - 1] = 0;
#undef PUT
	return (int)o;
}

int snprintf(char *buf, size_t size, const char *f, ...)
{
	va_list ap; va_start(ap, f);
	int r = vsnprintf(buf, size, f, ap);
	va_end(ap);
	return r;
}

int sprintf(char *buf, const char *f, ...)
{
	va_list ap; va_start(ap, f);
	int r = vsnprintf(buf, 4096, f, ap);
	va_end(ap);
	return r;
}

/* stdio : les journaux des bibliotheques (fprintf (stderr...)) vont au journal du kit */
typedef struct { int unused; } MK_FILE;
static MK_FILE mk_stderr_file;
void *_impure_ptr;
int fprintf(void *f, const char *fmt, ...)
{
	char b[256];
	va_list ap; va_start(ap, fmt);
	int r = vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	(void)f;
	mk_log(b);
	return r;
}
int vfprintf(void *f, const char *fmt, va_list ap)
{
	char b[256];
	int r = vsnprintf(b, sizeof b, fmt, ap);
	(void)f;
	mk_log(b);
	return r;
}
int printf(const char *fmt, ...) { (void)fmt; return 0; }
int puts(const char *s) { mk_log(s); return 0; }
int fputs(const char *s, void *f) { (void)f; mk_log(s); return 0; }
int fputc(int c, void *f) { (void)c; (void)f; return c; }
int putchar(int c) { return c; }
int fflush(void *f) { (void)f; return 0; }
void *mk_stderr = &mk_stderr_file;

static int mk_errno_value;
int *__errno(void) { return &mk_errno_value; }

void abort(void)
{
	mk_log("mediakit : abort () -- fin du programme");
	mk_exit(134);
}
void exit(int c) { mk_exit(c); }
void _exit(int c) { mk_exit(c); }
void __assert_func(const char *file, int line, const char *fn, const char *e)
{
	(void)line; (void)fn; (void)e;
	mk_log("mediakit : assertion fausse");
	mk_log(file);
	abort();
}
char *getenv(const char *n) { (void)n; return 0; }

/* ---- le tas -------------------------------------------------------------------------------- */

#define HDR 16
#define MAGIC_SMALL 0x4D4B534D00000000ull	/* | classe */
#define MAGIC_BIG   0x4D4B4249474D4150ull
#define MAGIC_ALIGN 0x4D4B414C49474E00ull
#define CLASS_MIN 5				/* 32 octets */
#define CLASS_MAX 16				/* 64 Kio */
#define CHUNK (1u << 20)

static void *free_heads[CLASS_MAX + 1];
static unsigned char *chunk_cur, *chunk_end;
static volatile int heap_lock;
static size_t heap_big_bytes;			/* (diagnostic) */

static void lock(void) { while (__atomic_exchange_n(&heap_lock, 1, __ATOMIC_ACQUIRE)) mk_yield(); }
static void unlock(void) { __atomic_store_n(&heap_lock, 0, __ATOMIC_RELEASE); }

static void *vm_alloc(size_t n)
{
	long r = mk_vm_map(n);
	return r < 0 ? 0 : (void *)r;
}

void *malloc(size_t n)
{
	uint64_t *h;
	if (n > ((size_t)1 << CLASS_MAX) - HDR) {
		size_t sz = (n + HDR + 4095) & ~(size_t)4095;
		h = vm_alloc(sz);
		if (!h) return 0;
		h[0] = MAGIC_BIG;
		h[1] = sz;
		lock(); heap_big_bytes += sz; unlock();
		return h + 2;
	}
	unsigned c = CLASS_MIN;
	while (((size_t)1 << c) < n + HDR) c++;
	lock();
	h = free_heads[c];
	if (h) {
		free_heads[c] = *(void **)(h + 2);
	} else {
		size_t sz = (size_t)1 << c;
		if (!chunk_cur || chunk_cur + sz > chunk_end) {
			unsigned char *k = vm_alloc(CHUNK);
			if (!k) { unlock(); return 0; }
			chunk_cur = k;
			chunk_end = k + CHUNK;
		}
		h = (uint64_t *)chunk_cur;
		chunk_cur += sz;
	}
	unlock();
	h[0] = MAGIC_SMALL | c;
	h[1] = 0;
	return h + 2;
}

static uint64_t *hdr_of(void *p) { return (uint64_t *)p - 2; }

void free(void *p)
{
	if (!p) return;
	uint64_t *h = hdr_of(p);
	if (h[0] == MAGIC_ALIGN) { free((void *)h[1]); return; }
	if (h[0] == MAGIC_BIG) {
		lock(); heap_big_bytes -= h[1]; unlock();
		mk_vm_unmap(h, h[1]);
		return;
	}
	if ((h[0] & ~0xFFull) != MAGIC_SMALL) { mk_log("mediakit : free () d'un bloc inconnu"); return; }
	unsigned c = (unsigned)(h[0] & 0xFF);
	lock();
	*(void **)p = free_heads[c];
	free_heads[c] = h;
	unlock();
}

/* octets utilisables d'un bloc */
static size_t usable(void *p)
{
	uint64_t *h = hdr_of(p);
	if (h[0] == MAGIC_ALIGN) {
		void *o = (void *)h[1];
		return usable(o) - (size_t)((unsigned char *)p - (unsigned char *)o);
	}
	if (h[0] == MAGIC_BIG) return h[1] - HDR;
	return ((size_t)1 << (h[0] & 0xFF)) - HDR;
}

void *calloc(size_t n, size_t m)
{
	size_t t = n * m;
	if (m && t / m != n) return 0;
	void *p = malloc(t);
	if (p && t <= ((size_t)1 << CLASS_MAX))
		memset(p, 0, t);		/* (les gros blocs : regions neuves, deja a zero) */
	return p;
}

void *realloc(void *p, size_t n)
{
	if (!p) return malloc(n);
	if (!n) { free(p); return 0; }
	size_t u = usable(p);
	if (n <= u) return p;
	void *q = malloc(n);
	if (!q) return 0;
	memcpy(q, p, u);
	free(p);
	return q;
}

void *memalign(size_t al, size_t n)
{
	if (al <= HDR) return malloc(n);
	unsigned char *o = malloc(n + al + HDR);
	if (!o) return 0;
	uintptr_t a = ((uintptr_t)o + HDR + al - 1) & ~(uintptr_t)(al - 1);
	uint64_t *h = (uint64_t *)a - 2;
	h[0] = MAGIC_ALIGN;
	h[1] = (uint64_t)(uintptr_t)o;
	return (void *)a;
}

int posix_memalign(void **r, size_t al, size_t n)
{
	void *p = memalign(al, n);
	if (!p) return 12;
	*r = p;
	return 0;
}

void *aligned_alloc(size_t al, size_t n) { return memalign(al, n); }

size_t mk_heap_big(void) { return heap_big_bytes; }

/* ---- pour FFmpeg (tools/mediakit/ffmpeg.sh) --------------------------------------------------------- */
#include <time.h>

size_t strspn(const char *s, const char *a)
{
	size_t n = 0;
	while (s[n] && strchr(a, s[n])) n++;
	return n;
}
size_t strcspn(const char *s, const char *r)
{
	size_t n = 0;
	while (s[n] && !strchr(r, s[n])) n++;
	return n;
}
void *bsearch(const void *key, const void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
	const unsigned char *b = base;
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		int c = cmp(key, b + mid * sz);
		if (!c) return (void *)(b + mid * sz);
		if (c < 0) hi = mid; else lo = mid + 1;
	}
	return 0;
}

static unsigned long long mk_strtoull_raw(const char *s, char **end, int base, int *neg)
{
	unsigned long long v = 0;
	const char *start;
	*neg = 0;
	while (isspace((unsigned char)*s)) s++;
	if (*s == '-' || *s == '+') *neg = *s++ == '-';
	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x' && isxdigit((unsigned char)s[2])) { s += 2; base = 16; }
	else if (base == 0) base = s[0] == '0' ? 8 : 10;
	start = s;
	for (;; s++) {
		int c = (unsigned char)*s, d;
		if (isdigit(c)) d = c - '0';
		else if (isalpha(c)) d = (c | 32) - 'a' + 10;
		else break;
		if (d >= base) break;
		v = v * (unsigned)base + (unsigned)d;
	}
	if (end) *end = (char *)s;
	(void)start;
	return v;
}
long long strtoll(const char *s, char **end, int base)
{
	int neg; unsigned long long v = mk_strtoull_raw(s, end, base, &neg);
	return neg ? -(long long)v : (long long)v;
}
unsigned long long strtoull(const char *s, char **end, int base)
{
	int neg; unsigned long long v = mk_strtoull_raw(s, end, base, &neg);
	return neg ? -v : v;
}

/* strtod : decimal (et inf, nan), assez juste pour les options et les en-tetes que FFmpeg lit */
double strtod(const char *s, char **end)
{
	const char *p = s;
	double v = 0, sign = 1;
	int any = 0, ex = 0;
	while (isspace((unsigned char)*p)) p++;
	if (*p == '-' || *p == '+') { if (*p == '-') sign = -1; p++; }
	if ((p[0] | 32) == 'i' && (p[1] | 32) == 'n' && (p[2] | 32) == 'f') {
		p += 3;
		if ((p[0] | 32) == 'i' && (p[1] | 32) == 'n' && (p[2] | 32) == 'i' && (p[3] | 32) == 't' && (p[4] | 32) == 'y') p += 5;
		if (end) *end = (char *)p;
		return sign / 0.0;
	}
	if ((p[0] | 32) == 'n' && (p[1] | 32) == 'a' && (p[2] | 32) == 'n') {
		if (end) *end = (char *)p + 3;
		return 0.0 / 0.0;
	}
	for (; isdigit((unsigned char)*p); p++, any = 1) v = v * 10 + (*p - '0');
	if (*p == '.') {
		p++;
		for (; isdigit((unsigned char)*p); p++, any = 1) { v = v * 10 + (*p - '0'); ex--; }
	}
	if (!any) { if (end) *end = (char *)s; return 0; }
	if ((*p | 32) == 'e') {
		const char *q = p + 1;
		int en = 0, eneg = 0, ed = 0;
		if (*q == '-' || *q == '+') eneg = *q++ == '-';
		for (; isdigit((unsigned char)*q); q++, ed = 1) if (en < 10000) en = en * 10 + (*q - '0');
		if (ed) { ex += eneg ? -en : en; p = q; }
	}
	if (end) *end = (char *)p;
	{
		double scale = 1, b = 10;
		int n = ex < 0 ? -ex : ex;
		while (n) { if (n & 1) scale *= b; b *= b; n >>= 1; }
		v = ex < 0 ? v / scale : v * scale;
	}
	return sign * v;
}

/* sscanf : d i u x X o (h hh l ll z j), f e g (l), s c [ensemble], n, %%, *, largeur */
int vsscanf(const char *str, const char *f, va_list ap)
{
	const char *s = str;
	int got = 0;
	while (*f) {
		if (isspace((unsigned char)*f)) { while (isspace((unsigned char)*s)) s++; f++; continue; }
		if (*f != '%') { if (*s != *f) break; s++; f++; continue; }
		f++;
		if (*f == '%') { while (isspace((unsigned char)*s)) s++; if (*s != '%') break; s++; f++; continue; }
		int skip = 0, width = 0, len = 0;	/* len : 1 h, 2 hh, 3 l, 4 ll */
		if (*f == '*') { skip = 1; f++; }
		while (isdigit((unsigned char)*f)) width = width * 10 + (*f++ - '0');
		if (*f == 'h') { len = 1; f++; if (*f == 'h') { len = 2; f++; } }
		else if (*f == 'l') { len = 3; f++; if (*f == 'l') { len = 4; f++; } }
		else if (*f == 'z' || *f == 'j' || *f == 't') { len = 4; f++; }
		char c = *f++;
		if (c == 'n') { if (!skip) *va_arg(ap, int *) = (int)(s - str); continue; }
		if (c != 'c' && c != '[') while (isspace((unsigned char)*s)) s++;
		if (!*s) break;
		char tmp[128];
		const char *src = s;
		if (width > 0 && width < (int)sizeof tmp && c != 's' && c != 'c' && c != '[') {
			size_t k = 0;
			while (k < (size_t)width && s[k]) { tmp[k] = s[k]; k++; }
			tmp[k] = 0;
			src = tmp;
		}
		if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'o') {
			char *e;
			int base = c == 'd' || c == 'u' ? 10 : c == 'i' ? 0 : c == 'o' ? 8 : 16;
			long long v = strtoll(src, &e, base);
			if (e == src) break;
			s += e - src;
			if (!skip) {
				if (len == 1) *va_arg(ap, short *) = (short)v;
				else if (len == 2) *va_arg(ap, char *) = (char)v;
				else if (len == 3) *va_arg(ap, long *) = (long)v;
				else if (len == 4) *va_arg(ap, long long *) = v;
				else *va_arg(ap, int *) = (int)v;
				got++;
			}
		} else if (c == 'f' || c == 'e' || c == 'g' || c == 'E' || c == 'G') {
			char *e;
			double v = strtod(src, &e);
			if (e == src) break;
			s += e - src;
			if (!skip) { if (len == 3) *va_arg(ap, double *) = v; else *va_arg(ap, float *) = (float)v; got++; }
		} else if (c == 's') {
			char *d = skip ? 0 : va_arg(ap, char *);
			int k = 0;
			while (*s && !isspace((unsigned char)*s) && (!width || k < width)) { if (d) d[k] = *s; k++; s++; }
			if (d) { d[k] = 0; got++; }
		} else if (c == 'c') {
			char *d = skip ? 0 : va_arg(ap, char *);
			int k, w = width ? width : 1;
			for (k = 0; k < w && *s; k++, s++) if (d) d[k] = *s;
			if (k < w) break;
			if (d) got++;
		} else if (c == '[') {
			int neg = 0, k = 0;
			char set[256] = { 0 };
			if (*f == '^') { neg = 1; f++; }
			if (*f == ']') { set[']'] = 1; f++; }
			while (*f && *f != ']') {
				if (f[1] == '-' && f[2] && f[2] != ']') {
					for (int ch = (unsigned char)f[0]; ch <= (unsigned char)f[2]; ch++) set[ch] = 1;
					f += 3;
				} else set[(unsigned char)*f++] = 1;
			}
			if (*f == ']') f++;
			char *d = skip ? 0 : va_arg(ap, char *);
			while (*s && (set[(unsigned char)*s] != neg) && (!width || k < width)) { if (d) d[k] = *s; k++; s++; }
			if (!k) break;
			if (d) { d[k] = 0; got++; }
		} else break;
	}
	return got ? got : (*s ? 0 : -1);
}
int sscanf(const char *s, const char *f, ...)
{
	va_list ap; va_start(ap, f);
	int r = vsscanf(s, f, ap);
	va_end(ap);
	return r;
}

/* le temps : UTC (le kit n'a pas le fuseau) ; clock () en ms (CLOCKS_PER_SEC de newlib : 1000) */
extern int64_t av_now(void);
clock_t clock(void) { return (clock_t)(av_now() / 1000); }

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
	y -= m <= 2;
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}
time_t mktime(struct tm *t)
{
	int64_t y = t->tm_year + 1900 + (t->tm_mon >= 0 ? t->tm_mon / 12 : (t->tm_mon - 11) / 12);
	int m = ((t->tm_mon % 12) + 12) % 12;
	int64_t days = days_from_civil(y, (unsigned)m + 1, 1) + t->tm_mday - 1;
	return (time_t)(days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
}
struct tm *gmtime_r(const time_t *tp, struct tm *t)
{
	int64_t s = (int64_t)*tp, days = s / 86400, rem = s % 86400;
	if (rem < 0) { rem += 86400; days--; }
	t->tm_hour = (int)(rem / 3600); t->tm_min = (int)(rem / 60 % 60); t->tm_sec = (int)(rem % 60);
	t->tm_wday = (int)(((days % 7) + 11) % 7);	/* 1970-01-01 : jeudi */
	int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t y = (int64_t)yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	unsigned d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
	y += m <= 2;
	t->tm_year = (int)(y - 1900); t->tm_mon = (int)m - 1; t->tm_mday = (int)d;
	t->tm_yday = (int)(days - days_from_civil(y, 1, 1));
	t->tm_isdst = 0;
	return t;
}
struct tm *gmtime(const time_t *tp) { static struct tm t; return gmtime_r(tp, &t); }
struct tm *localtime_r(const time_t *tp, struct tm *t) { return gmtime_r(tp, t); }

/* les fichiers : le kit lit les siens par AppKit (mk_fsys) ; ceux que FFmpeg ouvrirait lui-meme
 * (av_file_map, /dev/urandom pour une graine) : absents */
int open(const char *p, int flags, ...) { (void)p; (void)flags; *__errno() = 2; return -1; }
void *fdopen(int fd, const char *m) { (void)fd; (void)m; return 0; }
size_t fread(void *b, size_t sz, size_t n, void *f) { (void)b; (void)sz; (void)n; (void)f; return 0; }
int fclose(void *f) { (void)f; return 0; }
int setvbuf(void *f, char *b, int mode, size_t sz) { (void)f; (void)b; (void)mode; (void)sz; return 0; }
