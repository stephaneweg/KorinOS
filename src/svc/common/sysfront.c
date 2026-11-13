/* sysfront.c - voir sysfront.h (repris de la façade de src/svc/iwl/iwl.c) */
#include "sysfront.h"

static const char *prefix = "?";
static int to_stdout = -1;
static u32 log_lock;

void slog_name(const char *name)
{
	prefix = name;
}

void vslog(const char *fmt, va_list ap)
{
	char b[480];
	int n = snprintf(b, sizeof b, "%s : ", prefix);
	n += vsnprintf(b + n, sizeof b - n, fmt, ap);
	if (n > (int)sizeof b - 1)
		n = sizeof b - 1;
	if (to_stdout < 0) {
		long fl = korin_sys0(SYS_PROC_FLAGS);
		/* (sans sortie donnée par le père, la sortie standard est la console, c'est-à-dire le journal : chaque
		 * ligne y était deux fois quand init2 le lançait au bureau, et deux fois au Terminal par la façade) */
		to_stdout = fl >= 0 && (fl & 2);
	}
	mtx_lock(&log_lock);
	korin_sys2(SYS_PRINT, b, n);
	if (to_stdout)
		sys_write(1, b, n);
	mtx_unlock(&log_lock);
}

void slog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vslog(fmt, ap);
	va_end(ap);
}

int sysfront_rights(void)
{
	long pf = korin_sys0(SYS_PROC_FLAGS);
	return pf >= 0 && (pf & 0x100);
}

#define KLOG_MAX 65536
static char klog[KLOG_MAX];

static long klog_len(void)
{
	long n = korin_sys2(SYS_KLOG, klog, KLOG_MAX);
	return n < 0 ? 0 : n;
}

/* Suivi du journal. SYS_KLOG rend les 64 derniers Ko : une fois l'anneau plein, sa taille ne grandit plus et
 * une position en octets ne veut plus rien dire (d'où le Terminal muet du 13/11 : iwl, i2chid, intelgpu
 * attendaient que le journal dépasse 65 536 octets). La position est donc la dernière ligne complète déjà vue,
 * horodatage compris (« [  123.456] … »), cherchée de nouveau dans le tampon à chaque tour. */
static char anchor[256];
static long anchor_len = -1; /* -1 : pas de position, tout le tampon est nouveau */

/* début de la ligne qui finit en e (e sur le '\n') */
static long line_begin(long e)
{
	long b = e;
	while (b > 0 && klog[b - 1] != '\n')
		b--;
	return b;
}

static void anchor_set(long b, long e)
{
	long len = e - b + 1;
	if (len > (long)sizeof anchor)
		len = sizeof anchor;
	memcpy(anchor, klog + b, len);
	anchor_len = len;
}

/* l'octet qui suit la ligne de l'ancre (la dernière qui lui ressemble), ou -1 */
static long anchor_find(long n)
{
	if (anchor_len < 0)
		return -1;
	for (long e = n - 1; e >= 0; e--) {
		if (klog[e] != '\n')
			continue;
		long b = line_begin(e);
		if (b == 0 && n == KLOG_MAX)
			break; /* (première ligne coupée par l'anneau) */
		if (e - b + 1 >= anchor_len && !memcmp(klog + b, anchor, anchor_len))
			return e + 1;
		e = b;
	}
	return -1;
}

void klog_follow_start(void)
{
	long n = klog_len();
	long e = n - 1;
	while (e >= 0 && klog[e] != '\n')
		e--;
	if (e < 0) {
		anchor_len = 0; /* (journal vide : tout est nouveau, l'ancre vide est trouvée en 0) */
		return;
	}
	anchor_set(line_begin(e), e);
}

void klog_follow(void (*fn)(const char *line, long len))
{
	long n = klog_len();
	long i;
	if (anchor_len == 0)
		i = 0;
	else {
		i = anchor_find(n);
		if (i < 0) { /* (l'ancre est sortie de l'anneau : tout le tampon, sans sa première ligne coupée) */
			i = 0;
			if (n == KLOG_MAX) {
				while (i < n && klog[i] != '\n')
					i++;
				i++;
			}
		}
	}
	while (i < n) {
		long e = i;
		while (e < n && klog[e] != '\n')
			e++;
		if (e == n)
			break; /* ligne pas finie : au tour suivant */
		anchor_set(i, e);
		char *l = klog + i;
		long len = e - i + 1;
		char *p = memchr(l, ']', len < 16 ? len : 16);
		if (p && p[1] == ' ') {
			len -= p + 2 - l;
			l = p + 2;
		}
		fn(l, len);
		i = e + 1;
	}
}

/* les lignes complètes qui commencent par « <préfixe> : » */
static int seen_until;
static const char *until_text;

static void show_line(const char *l, long len)
{
	long plen = (long)strlen(prefix);
	if (len > plen + 3 && !memcmp(l, prefix, plen) && !memcmp(l + plen, " : ", 3)) {
		sys_write(1, l, len);
		if (until_text) {
			long ul = (long)strlen(until_text);
			for (long k = 0; k + ul <= len && !seen_until; k++)
				if (!memcmp(l + k, until_text, ul))
					seen_until = 1;
		}
	}
}

static void klog_show(const char *until)
{
	until_text = until;
	klog_follow(show_line);
}

int sysfront_run(const char *path, int argc, char **argv, const char *until)
{
	char args[512];
	int n = 0;
	args[0] = 0;
	for (int i = 1; i < argc && n < (int)sizeof args - 1; i++)
		n += snprintf(args + n, sizeof args - n, "%s%s", i > 1 ? " " : "", argv[i]);
	klog_follow_start();
	long pid = f_spawn(path, args);
	if (pid < 0) {
		char b[160];
		int m = snprintf(b, sizeof b, "%s : lancement de %s refusé (%ld)\n", prefix, path, pid);
		sys_write(1, b, m);
		return 1;
	}
	for (;;) {
		u64 e[8];
		int alive = korin_sys2(SYS_PROC_INFO, pid, e) == 0 && e[7] == 0;
		klog_show(until);
		if (!alive || seen_until)
			break;
		korin_sys1(SYS_SLEEP, 200);
	}
	if (!seen_until) {
		korin_sys1(SYS_SLEEP, 100);
		klog_show(0);
	}
	return 0;
}
