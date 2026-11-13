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
		int redirected = fl >= 0 && (fl & 2);
		int desktop = korin_sys1(SYS_WS_SIGNAL, 0) != -19;
		to_stdout = redirected || desktop;
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

/* les lignes complètes du journal depuis *from qui commencent par « <préfixe> : » (heure ôtée) */
static int seen_until;

static void klog_show(long *from, const char *until)
{
	long n = klog_len();
	if (n < *from)
		*from = 0;
	long plen = (long)strlen(prefix);
	long i = *from;
	while (i < n) {
		long e = i;
		while (e < n && klog[e] != '\n')
			e++;
		if (e == n)
			break;
		char *l = klog + i;
		long len = e - i + 1;
		char *p = memchr(l, ']', len < 16 ? len : 16);
		if (p && p[1] == ' ') {
			len -= p + 2 - l;
			l = p + 2;
		}
		if (len > plen + 3 && !memcmp(l, prefix, plen) && !memcmp(l + plen, " : ", 3)) {
			sys_write(1, l, len);
			if (until) {
				long ul = (long)strlen(until);
				for (long k = 0; k + ul <= len && !seen_until; k++)
					if (!memcmp(l + k, until, ul))
						seen_until = 1;
			}
		}
		i = e + 1;
	}
	*from = i;
}

int sysfront_run(const char *path, int argc, char **argv, const char *until)
{
	char args[512];
	int n = 0;
	args[0] = 0;
	for (int i = 1; i < argc && n < (int)sizeof args - 1; i++)
		n += snprintf(args + n, sizeof args - n, "%s%s", i > 1 ? " " : "", argv[i]);
	long from = klog_len();
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
		klog_show(&from, until);
		if (!alive || seen_until)
			break;
		korin_sys1(SYS_SLEEP, 200);
	}
	if (!seen_until) {
		korin_sys1(SYS_SLEEP, 100);
		klog_show(&from, 0);
	}
	return 0;
}
