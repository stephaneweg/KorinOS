/* files.c -- les outils des fichiers, des volumes et des paquets de korin-mcp (lecture seule : rien n'est écrit) */
#define _GNU_SOURCE
#include "tools.h"
#include "korin.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---- chemins ---- */

/* le chemin demandé -> absolu et normalisé (HOME:/, ~ : le dossier personnel) ; 0, -1 */
static int abs_path(const char *in, char *out, size_t cap)
{
	char t[1024];
	const char *home = getenv("HOME");
	if (!home || home[0] != '/')
		home = "/home";
	if (!in || !in[0])
		in = "/";
	if (!strncmp(in, "HOME:", 5))
		snprintf(t, sizeof t, "%s/%s", home, in + 5 + (in[5] == '/'));
	else if (in[0] == '~' && (in[1] == 0 || in[1] == '/'))
		snprintf(t, sizeof t, "%s%s", home, in + 1);
	else if (in[0] != '/') {
		char cwd[512];
		if (!getcwd(cwd, sizeof cwd))
			strcpy(cwd, "/");
		snprintf(t, sizeof t, "%s/%s", cwd, in);
	} else
		snprintf(t, sizeof t, "%s", in);
	/* . et .. retirés, // fondus */
	char *parts[128];
	int np = 0;
	for (char *s = strtok(t, "/"); s; s = strtok(0, "/")) {
		if (!strcmp(s, "."))
			continue;
		if (!strcmp(s, "..")) {
			if (np)
				np--;
			continue;
		}
		if (np == 128)
			return -1;
		parts[np++] = s;
	}
	size_t n = 0;
	out[0] = 0;
	for (int i = 0; i < np; i++) {
		int k = snprintf(out + n, cap - n, "/%s", parts[i]);
		if (k < 0 || (size_t)k >= cap - n)
			return -1;
		n += (size_t)k;
	}
	if (!n)
		snprintf(out, cap, "/");
	return 0;
}

const char *secret_path(const char *p)
{
	static const char *const exact[] = { "/etc/shadow.ini", "/etc/remote.txt", "/etc/wifi.txt", "/etc/wifi.bak", 0 };
	for (int i = 0; exact[i]; i++)
		if (!strcasecmp(p, exact[i]))
			return "mots de passe du système";
	if (!strncasecmp(p, "/etc/keyvault", 13) || !strncasecmp(p, "/etc/oauth/", 11) || !strncasecmp(p, "/var/oauth", 10))
		return "coffre de clés et jetons";
	const char *b = strrchr(p, '/');
	b = b ? b + 1 : p;
	if (!strcasecmp(b, ".env") || !strcasecmp(b, ".credentials.json") || !strcasecmp(b, ".oauth_token") ||
	    strstr(b, "pkg-key") || (strstr(b, ".pem") && strstr(b, "key")))
		return "clés ou jetons";
	return 0;
}

void iso_time(long long secs, char *t)
{
	time_t s = (time_t)secs;
	struct tm tm;
	if (secs <= 0 || !gmtime_r(&s, &tm)) {
		strcpy(t, "");
		return;
	}
	strftime(t, 24, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static void mode_text(unsigned m, char *t)
{
	const char *x = "rwxrwxrwx";
	for (int i = 0; i < 9; i++)
		t[i] = m & (0400u >> i) ? x[i] : '-';
	t[9] = 0;
}

/* ---- list_dir ---- */
struct ent {
	char *name;
	struct stat st;
	int ok;
};

static int ent_cmp(const void *a, const void *b)
{
	const struct ent *x = a, *y = b;
	int dx = x->ok && S_ISDIR(x->st.st_mode), dy = y->ok && S_ISDIR(y->st.st_mode);
	if (dx != dy)
		return dy - dx;
	return strcasecmp(x->name, y->name);
}

void t_list_dir(const struct jv *a, struct result *r)
{
	char path[1024], full[1400];
	if (abs_path(json_str(a, "path", "/"), path, sizeof path)) {
		fail(r, "chemin trop long");
		return;
	}
	int hidden = json_bool(a, "hidden", 1);
	long long limit = json_int(a, "limit", 500);
	if (limit < 1)
		limit = 1;
	if (limit > 5000)
		limit = 5000;
	DIR *d = opendir(path);
	if (!d) {
		fail(r, "%s : %s", path, strerror(errno));
		return;
	}
	size_t n = 0, cap = 64;
	struct ent *e = malloc(cap * sizeof *e);
	struct dirent *de;
	while (e && (de = readdir(d))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (!hidden && de->d_name[0] == '.')
			continue;
		if (n == cap) {
			struct ent *ne = realloc(e, (cap *= 2) * sizeof *e);
			if (!ne)
				break;
			e = ne;
		}
		e[n].name = strdup(de->d_name);
		snprintf(full, sizeof full, "%s/%s", strcmp(path, "/") ? path : "", de->d_name);
		e[n].ok = stat(full, &e[n].st) == 0;
		n++;
	}
	closedir(d);
	if (!e) {
		fail(r, "mémoire");
		return;
	}
	qsort(e, n, sizeof *e, ent_cmp);
	struct jb *j = &r->j;
	jb_obj(j);
	jb_kstr(j, "path", path);
	jb_kint(j, "count", (long long)n);
	jb_kbool(j, "truncated", (long long)n > limit);
	jb_key(j, "entries");
	jb_arr(j);
	for (size_t i = 0; i < n && (long long)i < limit; i++) {
		jb_obj(j);
		jb_kstr(j, "name", e[i].name);
		if (e[i].ok) {
			int dir = S_ISDIR(e[i].st.st_mode);
			jb_kstr(j, "type", dir ? "dir" : S_ISREG(e[i].st.st_mode) ? "file" : "other");
			if (!dir)
				jb_kint(j, "size", (long long)e[i].st.st_size);
			char t[24], m[10];
			iso_time((long long)e[i].st.st_mtime, t);
			if (t[0])
				jb_kstr(j, "mtime", t);
			mode_text((unsigned)e[i].st.st_mode, m);
			jb_kstr(j, "mode", m);
		}
		jb_obj_end(j);
	}
	jb_arr_end(j);
	jb_obj_end(j);
	for (size_t i = 0; i < n; i++)
		free(e[i].name);
	free(e);
}

/* ---- read_file ---- */
#define READ_MAX (512 * 1024)
#define LINES_FILE_MAX (8 * 1024 * 1024)

void t_read_file(const struct jv *a, struct result *r)
{
	char path[1024];
	const char *in = json_str(a, "path", 0);
	if (!in) {
		fail(r, "path manque");
		return;
	}
	if (abs_path(in, path, sizeof path)) {
		fail(r, "chemin trop long");
		return;
	}
	const char *why = secret_path(path);
	if (why) {
		fail(r, "%s : fichier refusé (%s) ; korin-mcp ne lit pas les secrets", path, why);
		return;
	}
	struct stat st;
	if (stat(path, &st) != 0) {
		fail(r, "%s : %s", path, strerror(errno));
		return;
	}
	if (S_ISDIR(st.st_mode)) {
		fail(r, "%s est un dossier : list_dir", path);
		return;
	}
	long long size = (long long)st.st_size;
	const char *enc = json_str(a, "encoding", "auto");
	long long start_line = json_int(a, "start_line", 0), max_lines = json_int(a, "max_lines", 0);
	int by_lines = start_line > 0 || max_lines > 0;
	long long off = json_int(a, "offset", 0), len = json_int(a, "length", 65536);
	if (off < 0)
		off = 0;
	if (len < 0)
		len = 0;
	if (len > READ_MAX)
		len = READ_MAX;
	if (by_lines) { /* lignes : tout le fichier (8 Mo au plus) puis la tranche */
		off = 0;
		len = size < LINES_FILE_MAX ? size : LINES_FILE_MAX;
	}
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		fail(r, "%s : %s", path, strerror(errno));
		return;
	}
	char *buf = malloc((size_t)len + 1);
	if (!buf) {
		close(fd);
		fail(r, "mémoire");
		return;
	}
	long long got = 0;
	if (off && lseek(fd, (off_t)off, SEEK_SET) < 0) {
		close(fd);
		free(buf);
		fail(r, "%s : position %lld : %s", path, off, strerror(errno));
		return;
	}
	while (got < len) {
		ssize_t k = read(fd, buf + got, (size_t)(len - got));
		if (k <= 0)
			break;
		got += k;
	}
	close(fd);
	buf[got] = 0;
	const char *s = buf;
	long long n = got, first = 1, last = 0, total_lines = 0;
	if (by_lines) {
		if (start_line < 1)
			start_line = 1;
		if (max_lines <= 0 || max_lines > 5000)
			max_lines = max_lines <= 0 ? 2000 : 5000;
		long long line = 1;
		const char *b = 0, *e = buf + got;
		for (const char *p = buf; p < buf + got; p++) {
			if (line == start_line && !b)
				b = p;
			if (*p == '\n') {
				line++;
				if (b && line == start_line + max_lines) {
					e = p + 1;
					break;
				}
			}
		}
		for (const char *p = buf; p < buf + got; p++) /* (le nombre de lignes) */
			total_lines += *p == '\n';
		if (got && buf[got - 1] != '\n')
			total_lines++;
		if (!b)
			b = e = buf + got;
		s = b;
		n = e - b;
		first = start_line;
		last = start_line;
		for (const char *p = b; p < e; p++)
			last += *p == '\n';
		if (n && e[-1] == '\n')
			last--;
	}
	int text = !strcmp(enc, "text") || (!strcmp(enc, "auto") && utf8_valid((const uint8_t *)s, (size_t)n));
	if (!strcmp(enc, "auto") && !text && n) { /* une coupure au milieu d'un caractère, à la fin : encore du texte */
		long long k = n;
		while (k > 0 && n - k < 3 && ((unsigned char)s[k - 1] & 0xC0) == 0x80)
			k--;
		if (k > 0 && ((unsigned char)s[k - 1] & 0xC0) == 0xC0)
			k--;
		if (k < n && utf8_valid((const uint8_t *)s, (size_t)k)) {
			n = k;
			got = by_lines ? got : k;
			text = 1;
		}
	}
	struct jb *j = &r->j;
	jb_obj(j);
	jb_kstr(j, "path", path);
	jb_kint(j, "size", size);
	char t[24];
	iso_time((long long)st.st_mtime, t);
	if (t[0])
		jb_kstr(j, "mtime", t);
	if (by_lines) {
		jb_kint(j, "first_line", first);
		jb_kint(j, "last_line", last);
		jb_kint(j, "total_lines", total_lines);
		jb_kbool(j, "eof", last >= total_lines);
	} else {
		jb_kint(j, "offset", off);
		jb_kint(j, "length", got);
		jb_kbool(j, "eof", off + got >= size);
	}
	jb_kstr(j, "encoding", text ? "text" : "base64");
	jb_key(j, "content");
	if (text)
		jb_strn(j, s, (size_t)n);
	else
		jb_base64(j, (const uint8_t *)s, (size_t)n);
	jb_obj_end(j);
	free(buf);
}

/* ---- find_files ---- */
struct finder {
	const char *pattern, *contains;
	int fold, want; /* want : 0 tout, 1 fichiers, 2 dossiers */
	long long max, found, visited, max_visit;
	int max_depth;
	long long deadline;
	int stopped;
	struct jb *j;
};

static long long now_ms(void)
{
	return ksys0(SYS_TIME_MS);
}

static void walk(struct finder *f, char *path, size_t plen, int depth)
{
	DIR *d = opendir(path);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (f->found >= f->max || f->stopped)
			break;
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (++f->visited > f->max_visit || ((f->visited & 255) == 0 && now_ms() > f->deadline)) {
			f->stopped = 1;
			break;
		}
		size_t nl = strlen(de->d_name);
		if (plen + nl + 2 >= 1024)
			continue;
		size_t base = plen;
		if (!(plen == 1 && path[0] == '/'))
			path[plen++] = '/';
		memcpy(path + plen, de->d_name, nl + 1);
		struct stat st;
		int ok = stat(path, &st) == 0, dir = ok && S_ISDIR(st.st_mode);
		int match = 1;
		if (f->pattern)
			match = fnmatch(f->pattern, de->d_name, f->fold ? FNM_CASEFOLD : 0) == 0;
		if (match && f->contains)
			match = f->fold ? strcasestr(de->d_name, f->contains) != 0 : strstr(de->d_name, f->contains) != 0;
		if (match && (f->want == 0 || (f->want == 1 && !dir) || (f->want == 2 && dir))) {
			jb_obj(f->j);
			jb_kstr(f->j, "path", path);
			jb_kstr(f->j, "type", dir ? "dir" : "file");
			if (ok && !dir)
				jb_kint(f->j, "size", (long long)st.st_size);
			jb_obj_end(f->j);
			f->found++;
		}
		/* (les partages du réseau : seulement si la recherche part de dedans) */
		if (dir && depth < f->max_depth && strcmp(path, "/net"))
			walk(f, path, plen + nl, depth + 1);
		plen = base;
		path[plen] = 0;
	}
	closedir(d);
}

void t_find_files(const struct jv *a, struct result *r)
{
	char path[1024];
	if (abs_path(json_str(a, "root", "/"), path, sizeof path)) {
		fail(r, "chemin trop long");
		return;
	}
	struct finder f = { 0 };
	f.pattern = json_str(a, "name", 0);
	f.contains = json_str(a, "contains", 0);
	if (!f.pattern && !f.contains) {
		fail(r, "donner name (motif : *.c, main.?) ou contains (un morceau du nom)");
		return;
	}
	f.fold = !json_bool(a, "case_sensitive", 0);
	const char *type = json_str(a, "type", "any");
	f.want = !strcmp(type, "file") ? 1 : !strcmp(type, "dir") ? 2 : 0;
	f.max = json_int(a, "max_results", 200);
	if (f.max < 1 || f.max > 5000)
		f.max = 5000;
	f.max_depth = (int)json_int(a, "max_depth", 16);
	f.max_visit = 300000;
	long long tmo = json_int(a, "timeout_ms", 20000);
	if (tmo < 100 || tmo > 120000)
		tmo = 20000;
	f.deadline = now_ms() + tmo;
	f.j = &r->j;
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		fail(r, "%s : pas un dossier", path);
		return;
	}
	jb_obj(f.j);
	jb_kstr(f.j, "root", path);
	jb_key(f.j, "results");
	jb_arr(f.j);
	walk(&f, path, strlen(path), 1);
	jb_arr_end(f.j);
	jb_kint(f.j, "found", f.found);
	jb_kint(f.j, "visited", f.visited);
	jb_kbool(f.j, "complete", !f.stopped && f.found < f.max);
	if (f.stopped)
		jb_kstr(f.j, "note", "arrêtée (délai ou nombre d'entrées vues) : préciser root");
	jb_obj_end(f.j);
}

/* ---- volumes ---- */
static long fs_info(const char *name, struct kmsg *m)
{
	char svc[32];
	snprintf(svc, sizeof svc, "fs.%s", name);
	long ch = ksys2(SYS_SVC_OPEN, svc, 200);
	if (ch < 0) { /* le service d'un autre volume (la même partition : /boot) */
		memset(m, 0, sizeof *m);
		m->op = VFS_VOLINFO;
		snprintf((char *)m->data, 16, "%s", name);
		m->len = (unsigned)strlen(name) + 1;
		if (__armpi_vfs_call(m) < 0)
			return ch;
		m->data[15] = 0;
		snprintf(svc, sizeof svc, "fs.%s", (char *)m->data);
		ch = ksys2(SYS_SVC_OPEN, svc, 200);
		if (ch < 0)
			return ch;
	}
	memset(m, 0, sizeof *m);
	m->op = FS_INFO;
	return ksys2(SYS_SVC_CALL, ch, m);
}

void t_volumes(const struct jv *a, struct result *r)
{
	(void)a;
	struct kmsg m;
	memset(&m, 0, sizeof m);
	m.op = VFS_MOUNTS;
	if (__armpi_vfs_call(&m) < 0) {
		fail(r, "vfs ne répond pas");
		return;
	}
	char names[32][16], mounts[32][96];
	int n = 0;
	m.data[sizeof m.data - 1] = 0;
	const char *e = (const char *)m.data, *end = (const char *)m.data + sizeof m.data - 1;
	for (long k = m.b; k > 0 && e < end && n < 32; k--) {
		const char *pt = e + strlen(e) + 1;
		if (pt >= end)
			break;
		snprintf(names[n], 16, "%s", e);
		snprintf(mounts[n], 96, "%s", pt);
		n++;
		e = pt + strlen(pt) + 1;
	}
	struct jb *j = &r->j;
	static const char *const types[] = { "?", "FAT32", "exFAT", "RAM", "réseau", "ext4" };
	jb_obj(j);
	jb_key(j, "volumes");
	jb_arr(j);
	for (int i = 0; i < n; i++) {
		jb_obj(j);
		jb_kstr(j, "name", names[i]);
		jb_kstr(j, "mount", mounts[i]);
		struct kmsg f;
		if (fs_info(names[i], &f) >= 0) {
			jb_kstr(j, "fs", f.d >= 0 && f.d <= 5 ? types[f.d] : "?");
			jb_kint(j, "size", f.a);
			if (f.b >= 0) {
				jb_kint(j, "free", f.b);
				jb_kint(j, "used", f.a >= f.b ? f.a - f.b : 0);
				if (f.a > 0)
					jb_knum(j, "used_percent", 100.0 * (double)(f.a - f.b) / (double)f.a, 1);
			} else
				jb_kstr(j, "free", "inconnue");
			f.data[63] = 0;
			if (f.data[0])
				jb_kstr(j, "label", (char *)f.data);
			if (f.d == FS_T_RAM)
				jb_kstr(j, "note", "en mémoire : perdu au redémarrage");
			if (!strcmp(mounts[i], "/media/sd1"))
				jb_kstr(j, "note", "partition personnelle de l'utilisateur : ne jamais écrire, formater ni réparer");
		} else
			jb_kstr(j, "state", "ne répond pas");
		jb_obj_end(j);
	}
	jb_arr_end(j);
	jb_obj_end(j);
}

/* ---- packages : la base (/var/pkg/db/<nom>.ini) et le dernier index vérifié (/var/pkg/index.txt) ---- */
struct kv {
	char *sec, *key, *val;
};
struct ini {
	struct kv *v;
	int n, cap;
	char *text;
};

static void ini_free(struct ini *i)
{
	free(i->v);
	free(i->text);
	memset(i, 0, sizeof *i);
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
		*--e = 0;
	return s;
}

static int ini_load(struct ini *i, const char *path)
{
	memset(i, 0, sizeof *i);
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	size_t cap = 1 << 16, n = 0;
	char *t = malloc(cap);
	for (;;) {
		if (!t) {
			close(fd);
			return -1;
		}
		if (n + 4096 > cap) {
			char *nt = realloc(t, cap *= 2);
			if (!nt) {
				free(t);
				close(fd);
				return -1;
			}
			t = nt;
		}
		ssize_t k = read(fd, t + n, cap - n - 1);
		if (k <= 0)
			break;
		n += (size_t)k;
	}
	close(fd);
	t[n] = 0;
	i->text = t;
	char *sec = "";
	for (char *line = t; line && *line;) {
		char *nl = strchr(line, '\n');
		if (nl)
			*nl++ = 0;
		char *s = trim(line);
		if (*s == '[') {
			char *e = strchr(s, ']');
			if (e) {
				*e = 0;
				sec = s + 1;
			}
		} else if (*s && *s != '#' && *s != ';') {
			char *eq = strchr(s, '=');
			if (eq) {
				*eq = 0;
				if (i->n == i->cap) {
					int c = i->cap ? i->cap * 2 : 256;
					struct kv *nv = realloc(i->v, (size_t)c * sizeof *nv);
					if (!nv)
						break;
					i->v = nv;
					i->cap = c;
				}
				i->v[i->n++] = (struct kv){ sec, trim(s), trim(eq + 1) };
			}
		}
		line = nl;
	}
	return 0;
}

static const char *ini_get(const struct ini *i, const char *sec, const char *key)
{
	for (int k = 0; k < i->n; k++)
		if (!strcmp(i->v[k].sec, sec) && !strcmp(i->v[k].key, key))
			return i->v[k].val;
	return 0;
}

/* comme vcmp de pkglib.h : les nombres un à un */
static int vcmp(const char *a, const char *b)
{
	while (*a || *b) {
		long x = 0, y = 0;
		while (*a >= '0' && *a <= '9')
			x = x * 10 + (*a++ - '0');
		while (*b >= '0' && *b <= '9')
			y = y * 10 + (*b++ - '0');
		if (x != y)
			return x < y ? -1 : 1;
		while (*a && (*a < '0' || *a > '9'))
			a++;
		while (*b && (*b < '0' || *b > '9'))
			b++;
	}
	return 0;
}

static int name_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

void t_packages(const struct jv *a, struct result *r)
{
	const char *action = json_str(a, "action", "installed");
	const char *want = json_str(a, "name", 0);
	int upd_only = !strcmp(action, "updates");
	if (strcmp(action, "installed") && !upd_only && strcmp(action, "info") && strcmp(action, "available")) {
		fail(r, "action : installed, updates, available ou info");
		return;
	}
	if (!strcmp(action, "info") && !want) {
		fail(r, "info demande name");
		return;
	}
	struct ini idx;
	int have_idx = ini_load(&idx, "/var/pkg/index.txt") == 0;
	struct jb *j = &r->j;
	jb_obj(j);
	if (have_idx) {
		const char *d = ini_get(&idx, "repo", "date"), *bd = ini_get(&idx, "repo", "board");
		jb_key(j, "index");
		jb_obj(j);
		jb_kstr(j, "date", d ? d : "");
		if (bd)
			jb_kstr(j, "board", bd);
		jb_kstr(j, "note", "le dernier index vérifié (pkg update le rafraîchit) : rien n'est lu sur le réseau ici");
		jb_obj_end(j);
	} else
		jb_kstr(j, "index", "absent (/var/pkg/index.txt) : pkg update ne s'est jamais fait");
	if (!strcmp(action, "available")) { /* le dépôt : tout ce que l'index offre */
		jb_key(j, "packages");
		jb_arr(j);
		for (int k = 0; have_idx && k < idx.n; k++) {
			if (strcmp(idx.v[k].key, "version") || !strcmp(idx.v[k].sec, "repo"))
				continue;
			const char *s = idx.v[k].sec;
			if (want && !strcasestr(s, want))
				continue;
			char p[300];
			snprintf(p, sizeof p, "/var/pkg/db/%s.ini", s);
			struct stat st;
			jb_obj(j);
			jb_kstr(j, "name", s);
			jb_kstr(j, "version", idx.v[k].val);
			const char *ti = ini_get(&idx, s, "title"), *ca = ini_get(&idx, s, "category");
			if (ti)
				jb_kstr(j, "title", ti);
			if (ca)
				jb_kstr(j, "category", ca);
			jb_kbool(j, "installed", stat(p, &st) == 0);
			jb_obj_end(j);
		}
		jb_arr_end(j);
		jb_obj_end(j);
		if (have_idx)
			ini_free(&idx);
		return;
	}
	DIR *d = opendir("/var/pkg/db");
	char *names[1024];
	int nn = 0;
	struct dirent *de;
	while (d && (de = readdir(d)) && nn < 1024) {
		size_t l = strlen(de->d_name);
		if (l > 4 && !strcasecmp(de->d_name + l - 4, ".ini")) {
			names[nn] = strndup(de->d_name, l - 4);
			if (names[nn])
				nn++;
		}
	}
	if (d)
		closedir(d);
	qsort(names, (size_t)nn, sizeof names[0], name_cmp);
	jb_key(j, "packages");
	jb_arr(j);
	int shown = 0, updates = 0;
	for (int i = 0; i < nn; i++) {
		if (want && (!strcmp(action, "info") ? strcmp(names[i], want) : !strcasestr(names[i], want)))
			continue;
		char p[300];
		snprintf(p, sizeof p, "/var/pkg/db/%s.ini", names[i]);
		struct ini db;
		if (ini_load(&db, p))
			continue;
		const char *ver = ini_get(&db, "package", "version");
		const char *nv = have_idx ? ini_get(&idx, names[i], "version") : 0;
		int up = ver && nv && vcmp(nv, ver) > 0;
		updates += up;
		if (upd_only && !up) {
			ini_free(&db);
			continue;
		}
		jb_obj(j);
		jb_kstr(j, "name", names[i]);
		jb_kstr(j, "version", ver ? ver : "?");
		if (nv)
			jb_kstr(j, "available", nv);
		jb_kbool(j, "update", up);
		if (!strcmp(action, "info")) { /* tout [package], le nombre de fichiers, et l'entrée de l'index */
			int files = 0;
			for (int k = 0; k < db.n; k++) {
				if (!strcmp(db.v[k].sec, "package") && strcmp(db.v[k].key, "name") && strcmp(db.v[k].key, "version"))
					jb_kstr(j, db.v[k].key, db.v[k].val);
				files += !strcmp(db.v[k].sec, "files");
			}
			jb_kint(j, "files", files);
			if (have_idx) {
				jb_key(j, "repository");
				jb_obj(j);
				for (int k = 0; k < idx.n; k++)
					if (!strcmp(idx.v[k].sec, names[i]))
						jb_kstr(j, idx.v[k].key, idx.v[k].val);
				jb_obj_end(j);
			}
		} else {
			const char *ti = ini_get(&db, "package", "title"), *mo = ini_get(&db, "package", "mode");
			if (ti)
				jb_kstr(j, "title", ti);
			if (mo && strcmp(mo, "manual"))
				jb_kstr(j, "mode", mo);
		}
		jb_obj_end(j);
		shown++;
		ini_free(&db);
	}
	jb_arr_end(j);
	jb_kint(j, "count", shown);
	jb_kint(j, "updates", updates);
	if (!strcmp(action, "info") && !shown)
		jb_kstr(j, "note", "pas installé (action available : le dépôt)");
	jb_obj_end(j);
	for (int i = 0; i < nn; i++)
		free(names[i]);
	if (have_idx)
		ini_free(&idx);
}
