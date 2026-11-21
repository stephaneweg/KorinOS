/*
 * glibc_libc.c -- liblinux : la glibc de compatibilité, ce qu'un exécutable comme Claude Code (Bun) importe
 * en plus des modules (docs/etude/LINUX-HOST.md, 21/11) : maths, ctype et caractères larges, locale « C »,
 * stdio, chaînes, environnement, gettext, dl*, divers. Sans structure à traduire, ou presque : ce qui touche
 * aux fichiers, aux process, aux signaux et aux sockets passe par syscall.c (structures de Linux).
 *
 * Les FILE * sont ceux de newlib (stdin, stdout, stderr de glibc.c le sont) : les fonctions de stdio sont
 * les mêmes. Les numéros d'erreur rendus dans errno : ceux de Linux (lx_set_errno_from_korin).
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <inttypes.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "linux.h"
#include "glibc_internal.h"

/* ---- la mémoire rendue à l'appelant, qu'il libère par free () : celle de son malloc quand l'exécutable en
   définit un (Bun : mimalloc ; sous Linux, la glibc elle-même appelle alors celui-là) ; linux_exe_start le pose */
void *(*lx_umalloc) (size_t) = malloc;
void *(*lx_urealloc) (void *, size_t) = realloc;
void (*lx_ufree) (void *) = free;

char *lx_ustrdup (const char *s)
{
	if (!s)
		return 0;
	size_t n = strlen (s) + 1;
	char *d = lx_umalloc (n);
	if (d)
		memcpy (d, s, n);
	else
		*lx_errno_ptr () = 12;
	return d;
}

/* getdelim, getline (pas dans la newlib de la chaîne) */
static ssize_t lx_getdelim (char **line, size_t *cap, int delim, FILE *f)
{
	if (!line || !cap)
	{
		*lx_errno_ptr () = 22;
		return -1;
	}
	size_t n = 0;
	for (;;)
	{
		if (!*line || n + 2 > *cap)
		{
			size_t nc = *cap < 120 ? 120 : *cap * 2;
			char *p = lx_urealloc (*line, nc);
			if (!p)
			{
				*lx_errno_ptr () = 12;
				return -1;
			}
			*line = p;
			*cap = nc;
		}
		int c = getc (f);
		if (c == EOF)
			break;
		(*line)[n++] = (char) c;
		if (c == delim)
			break;
	}
	(*line)[n] = 0;
	return n ? (ssize_t) n : -1;
}
static ssize_t lx_getline (char **l, size_t *c, FILE *f) { return lx_getdelim (l, c, '\n', f); }

/* ---- locale « C » : les noms de glibc (__newlocale…) ; nl_langinfo avec les nl_item de glibc ---- */
static int s_locale;
#define LX_LOCALE ((void *) &s_locale)

static void *lx_newlocale (int mask, const char *name, void *base) { (void) mask; (void) name; (void) base; return LX_LOCALE; }
static void *lx_duplocale (void *l) { (void) l; return LX_LOCALE; }
static void lx_freelocale (void *l) { (void) l; }

static char *lx_setlocale (int cat, const char *name)
{
	(void) cat;
	(void) name;
	return (char *) "C";	/* (« C.UTF-8 », « » : la seule « C ») */
}

static char *lx_nl_langinfo (int item)
{
	switch (item)
	{
	case 14: return (char *) "ANSI_X3.4-1968";	/* CODESET */
	case 0x10000: return (char *) ".";		/* RADIXCHAR */
	case 0x10001: return (char *) "";		/* THOUSEP */
	case 0x2028: return (char *) "%a %b %e %H:%M:%S %Y";	/* D_T_FMT */
	case 0x2029: return (char *) "%m/%d/%y";	/* D_FMT */
	case 0x202A: return (char *) "%H:%M:%S";	/* T_FMT */
	}
	return (char *) "";
}

static char *lx_nl_langinfo_l (int item, void *l) { (void) l; return lx_nl_langinfo (item); }
static int lx_strcoll_l (const char *a, const char *b, void *l) { (void) l; return strcmp (a, b); }
static size_t lx_strxfrm_l (char *d, const char *s, size_t n, void *l) { (void) l; return strxfrm (d, s, n); }
static int lx_wcscoll_l (const wchar_t *a, const wchar_t *b, void *l) { (void) l; return wcscmp (a, b); }
static size_t lx_wcsxfrm_l (wchar_t *d, const wchar_t *s, size_t n, void *l) { (void) l; return wcsxfrm (d, s, n); }
static double lx_strtod_l (const char *s, char **e, void *l) { (void) l; return strtod (s, e); }
static float lx_strtof_l (const char *s, char **e, void *l) { (void) l; return strtof (s, e); }
static unsigned long lx_wctype_l (const char *n, void *l) { (void) l; return (unsigned long) wctype (n); }
static int lx_iswctype_l (wint_t c, unsigned long t, void *l) { (void) l; return iswctype (c, (wctype_t) t); }
static wint_t lx_towlower_l (wint_t c, void *l) { (void) l; return towlower (c); }
static wint_t lx_towupper_l (wint_t c, void *l) { (void) l; return towupper (c); }
static size_t lx_wcsftime_l (wchar_t *s, size_t n, const wchar_t *f, const void *t, void *l)
{
	(void) f; (void) t; (void) l;
	if (n)
		s[0] = 0;
	return 0;
}
static int lx_strcoll (const char *a, const char *b) { return strcmp (a, b); }

/* gettext : pas de catalogue */
static char *lx_gettext (const char *m) { return (char *) m; }
static char *lx_dgettext (const char *d, const char *m) { (void) d; return (char *) m; }
static char *lx_bind_textdomain_codeset (const char *d, const char *c) { (void) d; return (char *) c; }

/* ---- chaînes, erreurs, signaux (numéros de Linux) ---- */
static char *lx_strerror_l (int e, void *l) { (void) l; return strerror (linux_errno_korin (e)); }
static int lx_xpg_strerror_r (int e, char *buf, size_t n)
{
	if (n)
	{
		strncpy (buf, strerror (linux_errno_korin (e)), n - 1);
		buf[n - 1] = 0;
	}
	return 0;
}

static const char *const s_signames[32] = {
	"Unknown signal 0", "Hangup", "Interrupt", "Quit", "Illegal instruction", "Trace/breakpoint trap",
	"Aborted", "Bus error", "Floating point exception", "Killed", "User defined signal 1",
	"Segmentation fault", "User defined signal 2", "Broken pipe", "Alarm clock", "Terminated",
	"Stack fault", "Child exited", "Continued", "Stopped (signal)", "Stopped", "Stopped (tty input)",
	"Stopped (tty output)", "Urgent I/O condition", "CPU time limit exceeded", "File size limit exceeded",
	"Virtual timer expired", "Profiling timer expired", "Window changed", "I/O possible", "Power failure",
	"Bad system call",
};
static char *lx_strsignal (int s)
{
	static __thread char buf[32];
	if (s >= 0 && s < 32)
		return (char *) s_signames[s];
	snprintf (buf, sizeof buf, "Real-time signal %d", s - 34);
	return buf;
}
static int lx_sigrtmin (void) { return 34; }
static int lx_sigrtmax (void) { return 64; }

/* ---- environnement, sortie, temps ---- */
static char *lx_secure_getenv (const char *n) { return getenv (n); }
static int lx_setenv (const char *n, const char *v, int o) { return setenv (n, v, o) < 0 ? (int) lx_set_errno_from_korin () : 0; }
static int lx_unsetenv (const char *n) { return unsetenv (n) < 0 ? (int) lx_set_errno_from_korin () : 0; }
/* LINUX_TRACE_EXIT=1 : qui termine le process (code, chaîne des rbp) */
static void trace_exit (const char *how, int code)
{
	if (!getenv ("LINUX_TRACE_EXIT"))
		return;
	char line[400];
	int n = snprintf (line, sizeof line, "liblinux : %s (%d), cadres :", how, code);
	const unsigned long long *fp = __builtin_frame_address (0);
	for (int i = 0; i < 14 && fp && ((uintptr_t) fp & 7) == 0 && (uintptr_t) fp > 0x10000 && n < (int) sizeof line - 24; i++)
	{
		n += snprintf (line + n, sizeof line - (size_t) n, " %#llx", fp[1]);
		fp = (const unsigned long long *) (uintptr_t) fp[0];
	}
	snprintf (line + n, sizeof line - (size_t) n, "\n");
	write (2, line, strlen (line));
}
static void lx_exit (int c) { trace_exit ("exit", c); exit (c); }
static void lx__exit (int c) { trace_exit ("_exit", c); _exit (c); }

/* quick_exit : les fonctions de at_quick_exit (à rebours), puis _exit, sans atexit */
static void (*s_quick[32]) (void);
static int s_nquick;
static int lx_at_quick_exit (void (*f) (void), void *d)
{
	(void) d;
	if (s_nquick >= 32)
		return -1;
	s_quick[s_nquick++] = f;
	return 0;
}
static int lx_at_quick_exit2 (void (*f) (void)) { return lx_at_quick_exit (f, 0); }
static void lx_quick_exit (int code)
{
	trace_exit ("quick_exit", code);
	while (s_nquick > 0)
		s_quick[--s_nquick] ();
	fflush (0);
	_exit (code);
}

static char *s_tzname[2] = { (char *) "UTC", (char *) "UTC" };
static long s_timezone;
static int s_daylight;
static void lx_tzset (void) { }

/* clock () : CLOCKS_PER_SEC de glibc = 1 000 000 */
static long lx_clock (void)
{
	struct timespec t;
#ifdef CLOCK_PROCESS_CPUTIME_ID
	if (clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &t) != 0)
#endif
		clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000L + t.tv_nsec / 1000;
}

static const char *lx_gnu_get_libc_version (void) { return "2.39"; }

static size_t lx_confstr (int name, char *buf, size_t n)
{
	const char *v = name == 0 ? "/bin" : 0;	/* _CS_PATH */
	if (!v)
	{
		*lx_errno_ptr () = 22;
		return 0;
	}
	if (n)
		snprintf (buf, n, "%s", v);
	return strlen (v) + 1;
}

static int lx_get_nprocs (void)
{
	long n = sysconf (_SC_NPROCESSORS_ONLN);
	return n < 1 ? 1 : (int) n;
}

/* struct sysinfo de Linux (112 octets) */
int lx_sysinfo_fill (void *out)
{
	memset (out, 0, 112);
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	long *l = out;
	l[0] = t.tv_sec;					/* uptime */
	long pages = sysconf (_SC_PHYS_PAGES), avail = sysconf (_SC_AVPHYS_PAGES);
	l[4] = pages > 0 ? pages * 4096 : 4L << 30;		/* totalram */
	l[5] = avail > 0 ? avail * 4096 : l[4] / 2;		/* freeram */
	((uint16_t *) out)[40] = 1;				/* procs (u16 au décalage 80) */
	((uint32_t *) out)[26] = 1;				/* mem_unit (décalage 104) */
	return 0;
}

static int lx_mallopt (int p, int v) { (void) p; (void) v; return 1; }
static int lx_backtrace (void **b, int n) { (void) b; (void) n; return 0; }
static char **lx_backtrace_symbols (void *const *b, int n)
{
	(void) b;
	size_t sz = (size_t) (n > 0 ? n : 1) * sizeof (char *);
	char **r = lx_umalloc (sz);
	if (r)
		memset (r, 0, sz);
	return r;
}
static int lx_getpriority (int w, unsigned who) { (void) w; (void) who; return 0; }
static long lx_fdelt_chk (long d)
{
	if (d < 0 || d >= 1024)
		linux_missing ("__fdelt_chk : descripteur hors de fd_set");
	return d / 64;
}
static void lx_longjmp_chk (jmp_buf b, int v) { longjmp (b, v); }

/* fnmatch : FNM_PATHNAME, FNM_NOESCAPE de glibc (1, 2) vers ceux de newlib */
static int lx_fnmatch (const char *p, const char *s, int f)
{
	int k = 0;
	if (f & 1) k |= FNM_PATHNAME;
	if (f & 2) k |= FNM_NOESCAPE;
	if (f & 4) k |= FNM_PERIOD;
#ifdef FNM_CASEFOLD
	if (f & 16) k |= FNM_CASEFOLD;
#endif
	return fnmatch (p, s, k) == 0 ? 0 : 1;
}

static char *lx_realpath (const char *p, char *out)
{
	char buf[4096];
	char *r = realpath (p, out ? out : buf);
	if (!r)
	{
		lx_set_errno_from_korin ();
		return 0;
	}
	return out ? r : lx_ustrdup (buf);
}

static int lx_vasprintf (char **out, const char *fmt, va_list ap)
{
	va_list c;
	va_copy (c, ap);
	int n = vsnprintf (0, 0, fmt, c);
	va_end (c);
	*out = n < 0 ? 0 : lx_umalloc ((size_t) n + 1);
	if (!*out)
		return -1;
	return vsnprintf (*out, (size_t) n + 1, fmt, ap);
}

static int lx_asprintf (char **out, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	int r = lx_vasprintf (out, fmt, ap);
	va_end (ap);
	return r;
}

static char *lx_mkdtemp (char *t) { char *r = mkdtemp (t); if (!r) lx_set_errno_from_korin (); return r; }
static int lx_mkostemp (char *t, int fl) { (void) fl; int r = mkstemp (t); return r < 0 ? (int) lx_set_errno_from_korin () : r; }
static char *lx_ctermid (char *s) { if (s) { strcpy (s, "/dev/tty"); return s; } return (char *) "/dev/tty"; }

static int lx_fscanf (FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	int r = vfscanf (f, fmt, ap);
	va_end (ap);
	return r;
}

static FILE *lx_fdopen (int fd, const char *m) { FILE *f = fdopen (fd, m); if (!f) lx_set_errno_from_korin (); return f; }
static FILE *lx_popen (const char *c, const char *m) { FILE *f = popen (c, m); if (!f) lx_set_errno_from_korin (); return f; }

/* ---- le nom du programme (données) ---- */
static char *s_prog_short = (char *) "";
static char *s_prog_name = (char *) "";

void lx_libc_set_progname (const char *argv0)
{
	if (!argv0)
		return;
	s_prog_name = (char *) argv0;
	const char *b = strrchr (argv0, '/');
	s_prog_short = (char *) (b ? b + 1 : argv0);
}

const struct lx_entry lx_table_libc[] =
{
	/* maths (libm) : les mêmes */
	LX_F (acos), LX_F (acosf), LX_F (acosh), LX_F (asin), LX_F (asinf), LX_F (asinh), LX_F (atan), LX_F (atan2),
	LX_F (atan2f), LX_F (atanf), LX_F (atanh), LX_F (cos), LX_F (cosf), LX_F (cosh), LX_F (exp2), LX_F (expf),
	LX_F (expm1), LX_F (frexpf), LX_F (hypot), LX_F (hypotf), LX_F (ldexpl), LX_F (log10), LX_F (log1p),
	LX_F (logf), LX_F (lrint), LX_F (nextafter), LX_F (pow), LX_F (powf), LX_F (sin), LX_F (sincosf), LX_F (sinf),
	LX_F (sinh), LX_F (tan), LX_F (tanf), LX_F (tanh), LX_F (tanhf), LX_F (strtof), LX_F (strtold), LX_F (div),
	LX_F (fmod), LX_F (sqrt), LX_F (cbrt), LX_F (trunc), LX_F (log2f), LX_F (exp2f), LX_F (fmodf), LX_F (sqrtf),
	/* ctype, caractères larges */
	LX_F (isalnum), LX_F (isalpha), LX_F (islower), LX_F (isupper), LX_F (isspace), LX_F (isxdigit),
	LX_F (isdigit), LX_F (isprint), LX_F (iswprint), LX_F (iswspace), LX_F (btowc), LX_F (wctob), LX_F (wcwidth),
	LX_F (wcscmp), LX_F (wmemcpy), LX_F (wmemmove), LX_F (wmemset), LX_F (putwc), LX_F (wcsxfrm),
	/* locale */
	LX_W ("__newlocale", lx_newlocale), LX_W ("__duplocale", lx_duplocale), LX_W ("duplocale", lx_duplocale),
	LX_W ("__freelocale", lx_freelocale), LX_W ("setlocale", lx_setlocale), LX_W ("nl_langinfo", lx_nl_langinfo),
	LX_W ("__nl_langinfo_l", lx_nl_langinfo_l), LX_W ("nl_langinfo_l", lx_nl_langinfo_l),
	LX_W ("__strcoll_l", lx_strcoll_l), LX_W ("__strxfrm_l", lx_strxfrm_l), LX_W ("__wcscoll_l", lx_wcscoll_l),
	LX_W ("__wcsxfrm_l", lx_wcsxfrm_l), LX_W ("__strtod_l", lx_strtod_l), LX_W ("__strtof_l", lx_strtof_l),
	LX_W ("__wctype_l", lx_wctype_l), LX_W ("__iswctype_l", lx_iswctype_l), LX_W ("__towlower_l", lx_towlower_l),
	LX_W ("__towupper_l", lx_towupper_l), LX_W ("__wcsftime_l", lx_wcsftime_l), LX_W ("strcoll", lx_strcoll),
	LX_W ("strerror_l", lx_strerror_l), LX_W ("__xpg_strerror_r", lx_xpg_strerror_r),
	LX_W ("gettext", lx_gettext), LX_W ("dgettext", lx_dgettext),
	LX_W ("bind_textdomain_codeset", lx_bind_textdomain_codeset),
	/* stdio : les FILE de newlib */
	LX_F (clearerr), LX_F (feof), LX_F (ferror), LX_F (fgetc), LX_F (fileno), LX_F (fread), LX_F (fseek),
	LX_F (fseeko), LX_W ("fseeko64", fseeko), LX_F (ftell), LX_F (ftello), LX_W ("ftello64", ftello),
	LX_F (setvbuf), LX_F (rewind), LX_F (putc), LX_W ("getdelim", lx_getdelim), LX_W ("getline", lx_getline), LX_F (vdprintf), LX_F (dprintf),
	LX_W ("fdopen", lx_fdopen), LX_W ("popen", lx_popen), LX_F (pclose), LX_W ("__isoc99_fscanf", lx_fscanf),
	LX_W ("__isoc99_vfscanf", vfscanf), LX_W ("__isoc99_vscanf", vscanf), LX_W ("ctermid", lx_ctermid),
	LX_F (setbuf), LX_F (getchar), LX_F (fputws), LX_W ("asprintf", lx_asprintf), LX_W ("vasprintf", lx_vasprintf), LX_F (vfscanf),
	/* chaînes, signaux */
	LX_F (memrchr), LX_W ("strsignal", lx_strsignal), LX_W ("__libc_current_sigrtmin", lx_sigrtmin),
	LX_W ("__libc_current_sigrtmax", lx_sigrtmax), LX_W ("fnmatch", lx_fnmatch), LX_F (strtok_r), LX_F (stpcpy),
	LX_F (strchrnul), LX_F (strcasestr), LX_F (memmem), LX_F (strsep),
	/* environnement, sortie */
	LX_W ("exit", lx_exit), LX_W ("_Exit", lx__exit), LX_W ("_exit", lx__exit), LX_W ("__cxa_at_quick_exit", lx_at_quick_exit), LX_W ("at_quick_exit", lx_at_quick_exit2),
	LX_W ("quick_exit", lx_quick_exit), LX_W ("secure_getenv", lx_secure_getenv),
	LX_W ("setenv", lx_setenv), LX_W ("unsetenv", lx_unsetenv), LX_F (system), LX_W ("mkdtemp", lx_mkdtemp),
	LX_W ("mkostemp", lx_mkostemp), LX_W ("realpath", lx_realpath), LX_F (atexit), LX_F (atoll), LX_F (llabs),
	LX_F (strtoimax), LX_F (strtoumax),
	LX_W ("program_invocation_short_name", &s_prog_short), LX_W ("program_invocation_name", &s_prog_name),
	/* temps */
	LX_W ("tzname", s_tzname), LX_W ("__tzname", s_tzname), LX_W ("__timezone", &s_timezone),
	LX_W ("timezone", &s_timezone), LX_W ("__daylight", &s_daylight), LX_W ("daylight", &s_daylight),
	LX_W ("tzset", lx_tzset), LX_W ("clock", lx_clock),
	/* système, divers */
	LX_W ("gnu_get_libc_version", lx_gnu_get_libc_version), LX_W ("confstr", lx_confstr),
	LX_W ("get_nprocs", lx_get_nprocs), LX_W ("get_nprocs_conf", lx_get_nprocs), LX_W ("sysinfo", lx_sysinfo_fill),
	LX_W ("mallopt", lx_mallopt), LX_W ("backtrace", lx_backtrace), LX_W ("backtrace_symbols", lx_backtrace_symbols),
	LX_W ("getpriority", lx_getpriority), LX_W ("__fdelt_chk", lx_fdelt_chk), LX_W ("__longjmp_chk", lx_longjmp_chk),
};
const size_t lx_table_libc_n = sizeof lx_table_libc / sizeof lx_table_libc[0];
