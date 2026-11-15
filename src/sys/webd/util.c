/*
 * util.c -- webd : types MIME, dates HTTP, chemins de l'URL, échappement HTML, journal.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "webd.h"

static const struct { const char *ext, *type; } mimes[] = {
	{ "html", "text/html; charset=utf-8" }, { "htm", "text/html; charset=utf-8" },
	{ "css", "text/css; charset=utf-8" }, { "js", "text/javascript; charset=utf-8" },
	{ "mjs", "text/javascript; charset=utf-8" }, { "json", "application/json" },
	{ "map", "application/json" }, { "xml", "application/xml" }, { "txt", "text/plain; charset=utf-8" },
	{ "md", "text/markdown; charset=utf-8" }, { "csv", "text/csv; charset=utf-8" },
	{ "svg", "image/svg+xml" }, { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" },
	{ "gif", "image/gif" }, { "webp", "image/webp" }, { "avif", "image/avif" }, { "ico", "image/x-icon" },
	{ "bmp", "image/bmp" }, { "mp4", "video/mp4" }, { "webm", "video/webm" }, { "ogv", "video/ogg" },
	{ "mp3", "audio/mpeg" }, { "ogg", "audio/ogg" }, { "wav", "audio/wav" }, { "flac", "audio/flac" },
	{ "m4a", "audio/mp4" }, { "woff", "font/woff" }, { "woff2", "font/woff2" }, { "ttf", "font/ttf" },
	{ "otf", "font/otf" }, { "pdf", "application/pdf" }, { "zip", "application/zip" },
	{ "wasm", "application/wasm" }, { "webmanifest", "application/manifest+json" },
};

const char *mime_type (const char *path)
{
	const char *dot = strrchr (path, '.');
	const char *slash = strrchr (path, '/');
	if (dot && (!slash || dot > slash))
		for (size_t i = 0; i < sizeof mimes / sizeof mimes[0]; i++)
			if (!strcasecmp (dot + 1, mimes[i].ext))
				return mimes[i].type;
	return "application/octet-stream";
}

void http_date (time_t t, char *out, size_t n)
{
	static const char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct tm tm;
	gmtime_r (&t, &tm);
	snprintf (out, n, "%s, %02d %s %04d %02d:%02d:%02d GMT", days[tm.tm_wday], tm.tm_mday, months[tm.tm_mon],
		tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static int hexval (int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* le chemin de l'URL (sans la requête « ? ») décodé et vérifié : commence par « / », pas de « .. », de « \ », d'octet
 * nul ni de « : » (un lecteur de Korin) ; les « // » réduits. -> 0, ou -1 s'il est refusé */
int url_decode_path (const char *in, char *out, size_t n)
{
	size_t o = 0;
	if (*in != '/')
		return -1;
	for (const char *p = in; *p && *p != '?' && *p != '#'; p++) {
		int c = (unsigned char) *p;
		if (c == '%') {
			int h = hexval (p[1]), l = h < 0 ? -1 : hexval (p[2]);
			if (h < 0 || l < 0)
				return -1;
			c = h * 16 + l;
			p += 2;
		}
		if (c == 0 || c == '\\' || c == ':')
			return -1;
		if (c == '/' && o > 0 && out[o - 1] == '/')
			continue;
		if (o + 1 >= n)
			return -1;
		out[o++] = (char) c;
	}
	out[o] = 0;
	/* « . », « .. » et les fichiers cachés (.git, .webstudio...) : refusés, sauf /.well-known/ (ACME, Let's Encrypt) */
	for (char *s = out; (s = strchr (s, '/')); s++)
		if (s[1] == '.' && strncmp (s, "/.well-known/", 13) != 0)
			return -1;
	return 0;
}

void html_escape (const char *in, char *out, size_t n)
{
	size_t o = 0;
	for (; *in && o + 7 < n; in++) {
		const char *r = NULL;
		switch (*in) {
		case '<': r = "&lt;"; break;
		case '>': r = "&gt;"; break;
		case '&': r = "&amp;"; break;
		case '"': r = "&quot;"; break;
		}
		if (r) {
			size_t l = strlen (r);
			memcpy (out + o, r, l);
			o += l;
		} else
			out[o++] = *in;
	}
	out[o] = 0;
}

long now_ms (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int file_exists (const char *path)
{
	struct stat st;
	return stat (path, &st) == 0;
}

/* crée les dossiers du chemin (le dernier compris) */
int mkdirs (const char *path)
{
	char tmp[512];
	snprintf (tmp, sizeof tmp, "%s", path);
	for (char *p = tmp + 1; *p; p++)
		if (*p == '/') {	/* (« / » aussi : refusé sur Korin, un dossier ailleurs) */
			*p = 0;
			mkdir (tmp, 0755);
			*p = '/';
		}
	if (mkdir (tmp, 0755) != 0 && errno != EEXIST && !file_exists (tmp))
		return -1;
	return 0;
}

/* ---- le journal : le service écrit dans /var/log/webd.log ; --dev, une ligne par requête sur la sortie (WebStudio
 * la lit : heure, méthode, chemin, code, octets, ms, remarque, séparés par des tabulations) ---- */

static pthread_mutex_t log_mu = PTHREAD_MUTEX_INITIALIZER;
static int log_stdout;

void log_open (int to_stdout)
{
	log_stdout = to_stdout;
	if (!to_stdout)
		mkdirs ("/var/log");
}

static void log_line (const char *line)
{
	pthread_mutex_lock (&log_mu);
	if (log_stdout) {
		fputs (line, stdout);
		fflush (stdout);
	} else {
		struct stat st;
		if (stat (LOG_FILE, &st) == 0 && st.st_size > LOG_MAX) {
			unlink (LOG_FILE ".old");
			rename (LOG_FILE, LOG_FILE ".old");
		}
		FILE *f = fopen (LOG_FILE, "a");
		if (f) {
			fputs (line, f);
			fclose (f);
		}
	}
	pthread_mutex_unlock (&log_mu);
}

void log_request (const char *site, const char *method, const char *path, int status, long long bytes, long ms,
	const char *note)
{
	char line[1200], when[32];
	time_t t = time (NULL);
	struct tm tm;
	gmtime_r (&t, &tm);
	if (log_stdout) {
		strftime (when, sizeof when, "%H:%M:%S", &tm);
		snprintf (line, sizeof line, "%s\t%s\t%s\t%d\t%lld\t%ld\t%s\n", when, method, path, status, bytes, ms,
			note ? note : "");
	} else {
		strftime (when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
		snprintf (line, sizeof line, "%s %s %s %s %d %lld %ldms%s%s\n", when, site, method, path, status, bytes, ms,
			note && *note ? " " : "", note ? note : "");
	}
	log_line (line);
}

void log_msg (const char *fmt, ...)
{
	char msg[900], line[1000], when[32];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (msg, sizeof msg, fmt, ap);
	va_end (ap);
	if (log_stdout) {
		snprintf (line, sizeof line, "# %s\n", msg);
	} else {
		time_t t = time (NULL);
		struct tm tm;
		gmtime_r (&t, &tm);
		strftime (when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
		snprintf (line, sizeof line, "%s webd: %s\n", when, msg);
		fprintf (stderr, "webd: %s\n", msg);
	}
	log_line (line);
}
