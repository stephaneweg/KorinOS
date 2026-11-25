/* gdrivefs - le Google Drive de l'utilisateur vu comme un volume de Korin, lu en flux et écrit
 * (docs/GDRIVE.md, docs/etude/GDRIVE.md). Un service de fichiers comme smbfs (protocole de src/inc/fs.inc), lancé
 * par vfs au montage :
 *
 *   mount /net/net1 gdrive:perso gdrivefs     (source : gdrive:NOM -> le compte OAuth « gdrive/NOM » d'oauthd)
 *
 * Réglages facultatifs, sans aucun secret, dans HOME:/gdrive/drives.ini, section [NOM] :
 *
 *   [perso]
 *   oauth = gdrive/perso                    le compte d'oauthd (par défaut gdrive/NOM)
 *   root = root                             l'identifiant du dossier monté (par défaut tout le Drive)
 *   cache = 64M                             le cache des blocs, en mémoire (8M au moins)
 *   ttl = 60                                secondes de validité d'une liste de dossier
 *   api_url = https://www.googleapis.com    (essais : un faux serveur, en http sur le réseau local seulement)
 *   readonly = 1                            en lecture seule (par défaut : lu et écrit)
 *   spool = HOME:/gdrive/spool              le tampon d'envoi des fichiers ouverts pour écrire
 *
 * Le jeton d'accès vient d'oauthd (oauth_token de NetKit, docs/OAUTH.md) ; il n'est jamais écrit. Rien n'est
 * lu ni demandé au réseau pendant le montage (vfs attend alors ce service) : tout se fait à la première requête.
 *
 * Chemins -> identifiants : depuis la racine, par les listes des dossiers (files.list, pages suivies), gardées
 * ttl secondes pour lister et pour un fichier au bout d'un chemin, plus longtemps pour traverser un dossier ;
 * stat est servi par la liste du parent. Lecture en flux : blocs alignés d'1 Mo (alt=media avec Range) dans un
 * cache LRU partagé par tous les fichiers ouverts, lié à l'identifiant et à la version (modifiedTime,
 * md5Checksum) vue à l'ouverture ; une lecture qui suit la précédente fait demander les blocs suivants (2, 4, 8,
 * puis 16 Mo en une requête : chaque requête paie l'attente de Google), un saut loin remet l'avance à un bloc.
 * Un fichier n'est jamais téléchargé en entier. HTTP/1.1 sur une connexion gardée ouverte (TLS d'OpenSSL,
 * /etc/ca-bundle), refaite si le serveur l'a fermée ; une lecture coupée est redemandée après 1, 2, 4 s.
 *
 * Écrire (version 2, plus bas : « écrire ») : un fichier ouvert pour écrire est tenu dans le tampon d'envoi et
 * envoyé à Drive en entier à sa fermeture ; mkdir, renommer, déplacer, dater ; effacer met à la corbeille.
 * Google Docs, Sheets, Slides, raccourcis, formulaires... cachés (les exports viendront) ; deux fichiers du même
 * nom dans un dossier : « rapport (2).pdf »... dans l'ordre de création ; un « / » dans un nom devient « ∕ »
 * (U+2215). Un seul thread : une requête à la fois (un bloc qui arrive, un envoi, font attendre les autres). */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>
#include <armpi/syscall.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <netkit_abi.h>

typedef int64_t i64;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

/* ---- Korin : appels système, message, protocole (src/inc/syscalls.inc, fs.inc, armpi.inc) */
#define SYS_BUF_CREATE 0x01
#define SYS_BUF_GRANT 0x02
#define SYS_BUF_FREE 0x04
#define SYS_PROC_INFO 0x1E
#define SYS_SVC_REGISTER 0x30
#define SYS_SVC_NEXT 0x33
#define SYS_SVC_REPLY 0x34
#define SYS_SVC_MAP_CALLER 0x36
#define SVF_ENDS 1
#define SVF_BIRTHS 2
#define SVC_OP_END 0xFFFF
#define SVC_OP_BIRTH 0xFFFE
#define SVC_OP_FORK 0xFFFC
#define SVC_PID_KERNEL 0x7FFFFFFF
#define MC_WRITE 1
#ifndef BUF_VA_BASE /* (l'essai sous Linux met le sien) */
#define BUF_VA_BASE 0x6800000000ULL
#endif
#define BUF_SLOT 0x4000000ULL

#define FS_OPEN 1
#define FS_STAT 2
#define FS_MKDIR 3
#define FS_UNLINK 4
#define FS_UTIME 5
#define FS_RENAME 6
#define FS_READ 10
#define FS_WRITE 11
#define FS_CLOSE 12
#define FS_READDIR 13
#define FS_BUFFER 14
#define FS_READ_BUF 15
#define FS_WRITE_BUF 16
#define FS_READ_DIRECT 17
#define FS_CACHE 18
#define FS_INFO 19
#define FS_SYNC 25
#define FS_SEEK 26
#define FS_POS_CUR -1
#define FS_DATA_MAX 464
#define FS_BUFFER_SIZE 65536
#define FS_DIRECT_MAX 0x100000
#define FS_T_NET 4 /* (comme smbfs : un volume réseau) */
#define FS_CC_ON 1
#define FS_CC_OFF 2
#define FS_CC_DROP 3

#define O_K_READ 1
#define O_K_WRITE 2
#define O_K_CREATE 4
#define O_K_TRUNC 8
#define O_K_APPEND 16
#define ATTR_DIR 0x10
#define ATTR_FILE 0x20
#define ATTR_SYS 0x100
#define ATTR_LIB 0x200

/* erreurs de Korin : numéros de Linux (newlib a les siens) */
#define E_NOENT -2
#define E_IO -5
#define E_BADF -9
#define E_AGAIN -11
#define E_NOMEM -12
#define E_ACCES -13
#define E_NOTDIR -20
#define E_ISDIR -21
#define E_EXIST -17
#define E_INVAL -22
#define E_MFILE -24
#define E_FBIG -27
#define E_NOSPC -28
#define E_NAMETOOLONG -36
#define E_NOTEMPTY -39
#define E_TIMEDOUT -110
#define E_CONNREFUSED -111
#define RX_EOF -1000 /* (interne : connexion fermée par le serveur) */

struct msg {
	i64 op;
	i64 a, b, c, d;
	u32 len;
	u32 pid;
	u8 data[464];
};
_Static_assert(sizeof(struct msg) == 512, "message de 512 octets");

static long ksys(long n, long a, long b, long c, long d)
{
	return armpi_syscall(n, a, b, c, d, 0, 0);
}

/* ---- réglages */

#ifndef DRIVES_INI
#define DRIVES_INI "HOME:/gdrive/drives.ini"
#endif
#define DEFAULT_API "https://www.googleapis.com"
#define FOLDER_MIME "application/vnd.google-apps.folder"
#define GAPPS_MIME "application/vnd.google-apps."
#define BLOCK (1u << 20)	 /* un bloc du cache, aligné */
#define PREFETCH_MAX 32 /* threads de lecture anticipée au plus (réglage threads) */
#define RA_MAX 16		 /* blocs demandés d'un coup au plus (lecture anticipée) */
#define CACHE_MIN (8u << 20)	 /* (au moins deux fois l'avance) */
#define LIST_MAX 256		 /* listes de dossiers gardées */
#define HTTP_IDLE 30000		 /* ms sans rien recevoir : abandon */
#define KEEP_IDLE 120000	 /* ms : une connexion inutilisée depuis plus longtemps est fermée */
#define JSON_MAX (32u << 20)	 /* une page de files.list au plus */
#define BACKOFF_MAX 3		 /* nouveaux essais après 1, 2, 4 s */
#define ID_MAX 128
#define VER_MAX 96
#define UPLOAD_CHUNK (8u << 20) /* un morceau d'envoi (multiple de 256 Ko, comme le veut Drive) */
#define NAME_MAX_ 512		/* un nom de Drive (UTF-8) */
#ifndef DEFAULT_SPOOL
#define DEFAULT_SPOOL "HOME:/gdrive/spool"
#endif
#define NONE ((u64)-1)

/* ---- état */

#define OUV_MAX 128
#define OWN_MAX 8
#define TP_MAX 48

struct dent { /* une entrée d'un dossier ouvert */
	char *name;
	u64 size;
	u64 attr;
};

struct ouvert {
	int used, isdir;
	struct dent *ents;
	int nents;
	i64 pos;
	u64 size;
	char id[ID_MAX], ver[VER_MAX];
	u64 next; /* fin de la lecture précédente (NONE : aucune) */
	int ra;	  /* blocs demandés à la prochaine absence (1, 2, 4) */
	u32 owners[OWN_MAX];
	/* ouvert pour écrire : le contenu dans un fichier du tampon d'envoi (spool), envoyé à Drive à la fermeture */
	int wr, fd, dirty, append;
	char parent[ID_MAX];
	char name[NAME_MAX_];
	char spool[300];
};

static struct ouvert ouverts[OUV_MAX];
static struct {
	i64 pid, num;
} tampons[TP_MAX];
static struct msg msg __attribute__((aligned(16)));
static long canal, req_cour, pid_vfs;
static char volume[32], source[256];

/* la configuration (lue à la première requête) */
static char drive[64], account[96], root_id[ID_MAX] = "root", api_url[256] = DEFAULT_API;
static u64 cache_bytes = 64u << 20; /* (au moins deux fois l'avance : 16 Mo) */
static long list_ttl_ms = 60000;
static int conf_read, readonly, cache_set;
static int pf_threads = 4, pf_chunk = 8, pf_ahead; /* lecture anticipée : threads, blocs par requête, blocs d'avance */
static char spool_dir[200] = DEFAULT_SPOOL;

/* le jeton d'accès (en mémoire seulement) */
static char token[4096];
static int have_token, kit_ok;

/* compteurs (FS_CACHE, essais) */
static struct {
	u64 hits, misses, fetches, fetched, evictions, lists, connects, retries, uploads, uploaded, netretries, prefetched, waits;
} st;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "gdrivefs %s: ", volume);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static void scpy(char *d, const char *s, size_t n)
{
	if (!n)
		return;
	size_t i = 0;
	for (; s && s[i] && i + 1 < n; i++)
		d[i] = s[i];
	d[i] = 0;
}

static long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ---- tampon extensible (comme oauthd) */

struct buf {
	char *p;
	size_t n, cap;
};

static int b_grow(struct buf *b, size_t more)
{
	if (b->n + more + 1 <= b->cap)
		return 0;
	size_t c = b->cap ? b->cap * 2 : 1024;
	while (c < b->n + more + 1)
		c *= 2;
	char *q = realloc(b->p, c);
	if (!q)
		return -1;
	b->p = q;
	b->cap = c;
	return 0;
}

static int b_add(struct buf *b, const void *s, size_t n)
{
	if (b_grow(b, n))
		return -1;
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
	return 0;
}
static int b_str(struct buf *b, const char *s) { return b_add(b, s, strlen(s)); }
static void b_free(struct buf *b)
{
	free(b->p);
	b->p = 0;
	b->n = b->cap = 0;
}

/* encodage d'URL (paramètres) */
static int b_enc(struct buf *b, const char *s)
{
	static const char hex[] = "0123456789ABCDEF";
	for (const u8 *p = (const u8 *)s; *p; p++) {
		u8 c = *p;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
		    c == '_' || c == '~') {
			if (b_add(b, &c, 1))
				return -1;
		} else {
			char t[3] = {'%', hex[c >> 4], hex[c & 15]};
			if (b_add(b, t, 3))
				return -1;
		}
	}
	return 0;
}

static int hexv(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* ---- JSON (comme oauthd : les champs d'un objet, plus le parcours d'un tableau d'objets) */

static const char *json_skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	return p;
}

/* fin d'une valeur JSON (chaîne, nombre, objet, tableau, mot) */
static const char *json_skip(const char *p)
{
	p = json_skip_ws(p);
	if (*p == '"') {
		for (p++; *p && *p != '"'; p++)
			if (*p == '\\' && p[1])
				p++;
		return *p ? p + 1 : p;
	}
	if (*p == '{' || *p == '[') {
		int depth = 0;
		for (; *p; p++) {
			if (*p == '"') {
				p = json_skip(p) - 1;
				continue;
			}
			if (*p == '{' || *p == '[')
				depth++;
			else if ((*p == '}' || *p == ']') && --depth == 0)
				return p + 1;
		}
		return p;
	}
	while (*p && *p != ',' && *p != '}' && *p != ']')
		p++;
	return p;
}

/* la valeur de « key » dans l'objet qui commence en j (pas plus profond) ; 0 : absente */
static const char *json_find(const char *j, const char *key)
{
	if (!j)
		return 0;
	const char *p = json_skip_ws(j);
	if (*p != '{')
		return 0;
	p++;
	size_t kn = strlen(key);
	for (;;) {
		p = json_skip_ws(p);
		if (*p != '"')
			return 0;
		const char *ks = p + 1;
		const char *ke = json_skip(p) - 1;
		if (ke < ks)
			return 0;
		p = json_skip_ws(ke + 1);
		if (*p != ':')
			return 0;
		p = json_skip_ws(p + 1);
		if ((size_t)(ke - ks) == kn && !memcmp(ks, key, kn))
			return p;
		p = json_skip_ws(json_skip(p));
		if (*p != ',')
			return 0;
		p++;
	}
}

static void u8put(struct buf *b, unsigned u)
{
	char t[4];
	int n;
	if (u < 0x80) {
		t[0] = (char)u;
		n = 1;
	} else if (u < 0x800) {
		t[0] = (char)(0xC0 | u >> 6);
		t[1] = (char)(0x80 | (u & 63));
		n = 2;
	} else if (u < 0x10000) {
		t[0] = (char)(0xE0 | u >> 12);
		t[1] = (char)(0x80 | ((u >> 6) & 63));
		t[2] = (char)(0x80 | (u & 63));
		n = 3;
	} else {
		t[0] = (char)(0xF0 | u >> 18);
		t[1] = (char)(0x80 | ((u >> 12) & 63));
		t[2] = (char)(0x80 | ((u >> 6) & 63));
		t[3] = (char)(0x80 | (u & 63));
		n = 4;
	}
	b_add(b, t, (size_t)n);
}

/* la chaîne qui commence en v (malloc, à libérer) ; 0 : pas une chaîne */
static char *json_strv(const char *v)
{
	if (!v || *v != '"')
		return 0;
	struct buf o = {0};
	if (b_add(&o, "", 0))
		return 0;
	for (v++; *v && *v != '"'; v++) {
		if (*v != '\\') {
			b_add(&o, v, 1);
			continue;
		}
		v++;
		switch (*v) {
		case 'n': b_add(&o, "\n", 1); break;
		case 't': b_add(&o, "\t", 1); break;
		case 'r': case 'b': case 'f': break;
		case 'u': {
			unsigned u = 0;
			for (int i = 1; i <= 4; i++)
				u = u * 16 + (unsigned)(hexv(v[i]) < 0 ? 0 : hexv(v[i]));
			for (int i = 0; i < 4 && v[1]; i++)
				v++;
			if (u >= 0xD800 && u < 0xDC00 && v[1] == '\\' && v[2] == 'u') {
				unsigned lo = 0;
				for (int i = 3; i <= 6; i++)
					lo = lo * 16 + (unsigned)(hexv(v[i]) < 0 ? 0 : hexv(v[i]));
				if (lo >= 0xDC00 && lo < 0xE000) {
					u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
					v += 6;
				}
			}
			if (u)
				u8put(&o, u);
			break;
		}
		case 0: v--; break;
		default: b_add(&o, v, 1);
		}
	}
	return o.p;
}

static char *json_str(const char *j, const char *key)
{
	return json_strv(json_find(j, key));
}

/* un nombre (Drive écrit les int64 entre guillemets) ; def : absent */
static u64 json_u64(const char *j, const char *key, u64 def)
{
	const char *v = json_find(j, key);
	if (!v)
		return def;
	if (*v == '"')
		v++;
	return *v >= '0' && *v <= '9' ? strtoull(v, 0, 10) : def;
}

/* « 2026-11-16T10:20:30.123Z » -> secondes UTC (0 : illisible) */
static i64 parse_time(const char *s)
{
	int y, mo, d, h, mi, se;
	if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6 || mo < 1 || mo > 12)
		return 0;
	/* jours depuis 1970 (algorithme « days from civil ») */
	y -= mo <= 2;
	i64 era = (y >= 0 ? y : y - 399) / 400;
	i64 yoe = y - era * 400;
	i64 doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	i64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	i64 days = era * 146097 + doe - 719468;
	return days * 86400 + h * 3600 + mi * 60 + se;
}

/* ---- configuration (HOME:/gdrive/drives.ini) */

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
	return s;
}

/* « 32M », « 512K », « 1G », « 33554432 » -> octets (0 : illisible) */
static u64 parse_size(const char *s)
{
	char *e;
	u64 v = strtoull(s, &e, 10);
	switch (*e) {
	case 'k': case 'K': return v << 10;
	case 'm': case 'M': return v << 20;
	case 'g': case 'G': return v << 30;
	case 0: return v;
	default: return 0;
	}
}

struct url {
	int tls;
	char host[256];
	int port;
	char path[512]; /* (préfixe des chemins de l'API, sans « / » final) */
};
static struct url api;

/* en clair : pour les essais seulement (un faux serveur sur le réseau local), comme oauthd */
static int private_host(const char *h)
{
	if (!strcmp(h, "localhost"))
		return 1;
	unsigned b0, b1, b2, b3;
	char extra;
	if (sscanf(h, "%u.%u.%u.%u%c", &b0, &b1, &b2, &b3, &extra) != 4 || b0 > 255 || b1 > 255 || b2 > 255 || b3 > 255)
		return 0;
	u32 ip = b0 << 24 | b1 << 16 | b2 << 8 | b3;
	return (ip >> 24) == 127 || (ip >> 24) == 10 || (ip >> 16) == 0xC0A8 || (ip >> 20) == 0xAC1;
}

static int url_parse(const char *s, struct url *u)
{
	memset(u, 0, sizeof *u);
	if (!strncasecmp(s, "https://", 8)) {
		u->tls = 1;
		u->port = 443;
		s += 8;
	} else if (!strncasecmp(s, "http://", 7)) {
		u->port = 80;
		s += 7;
	} else
		return -1;
	size_t hl = strcspn(s, ":/?");
	if (!hl || hl >= sizeof u->host)
		return -1;
	memcpy(u->host, s, hl);
	u->host[hl] = 0;
	s += hl;
	if (*s == ':') {
		u->port = atoi(s + 1);
		s += 1 + strspn(s + 1, "0123456789");
	}
	if (u->port <= 0 || u->port > 65535 || (*s && *s != '/'))
		return -1;
	scpy(u->path, s, sizeof u->path);
	size_t n = strlen(u->path);
	while (n && u->path[n - 1] == '/')
		u->path[--n] = 0;
	if (!u->tls && !private_host(u->host))
		return -2;
	/* le jeton (Bearer) ne part que chez Google : *.googleapis.com, ou un serveur d'essai du réseau local ;
	 * drives.ini est dans le dossier de l'utilisateur, un programme l'aurait fait envoyer ailleurs (D18, E63) */
	size_t hn = strlen(u->host);
	const char *g = "googleapis.com";
	int google = !strcasecmp(u->host, g) || (hn > 15 && !strcasecmp(u->host + hn - 15, ".googleapis.com"));
	if (!google && !private_host(u->host))
		return -2;
	return 0;
}

/* la source (« gdrive:NOM ») et la section [NOM] de drives.ini ; une fois, à la première requête */
static long load_conf(void)
{
	if (conf_read)
		return 0;
	if (strncasecmp(source, "gdrive:", 7) || !source[7]) {
		say("source « %s » : il faut gdrive:NOM (le compte OAuth gdrive/NOM, oauth browser gdrive/NOM)", source);
		return E_INVAL;
	}
	scpy(drive, source + 7, sizeof drive);
	snprintf(account, sizeof account, "gdrive/%s", drive);
	FILE *f = fopen(DRIVES_INI, "r");
	if (f) {
		char line[600], sec[80] = "";
		while (fgets(line, sizeof line, f)) {
			char *s = trim(line);
			if (!*s || *s == '#' || *s == ';')
				continue;
			if (*s == '[') {
				char *e = strchr(s, ']');
				if (e)
					*e = 0;
				scpy(sec, trim(s + 1), sizeof sec);
				continue;
			}
			char *eq = strchr(s, '=');
			if (strcasecmp(sec, drive) || !eq)
				continue;
			*eq = 0;
			char *k = trim(s), *v = trim(eq + 1);
			char *c = strpbrk(v, ";#"); /* (commentaire en fin de ligne) */
			if (c && c > v && (c[-1] == ' ' || c[-1] == '\t')) {
				*c = 0;
				v = trim(v);
			}
			if (!strcasecmp(k, "oauth") && *v)
				scpy(account, v, sizeof account);
			else if (!strcasecmp(k, "root") && *v)
				scpy(root_id, v, sizeof root_id);
			else if (!strcasecmp(k, "api_url") && *v)
				scpy(api_url, v, sizeof api_url);
			else if (!strcasecmp(k, "cache")) {
				u64 b = parse_size(v);
				if (b) {
					cache_bytes = b;
					cache_set = 1;
				}
			} else if (!strcasecmp(k, "ttl") && *v)
				list_ttl_ms = atol(v) * 1000;
			else if (!strcasecmp(k, "readonly"))
				readonly = *v == '1' || !strcasecmp(v, "yes") || !strcasecmp(v, "oui") || !strcasecmp(v, "true");
			else if (!strcasecmp(k, "threads") && *v)
				pf_threads = atoi(v);
			else if (!strcasecmp(k, "chunk") && *v)
				pf_chunk = (int)(parse_size(v) / BLOCK);
			else if (!strcasecmp(k, "ahead") && *v)
				pf_ahead = (int)(parse_size(v) / BLOCK);
			else if (!strcasecmp(k, "spool") && *v)
				scpy(spool_dir, v, sizeof spool_dir);
		}
		fclose(f);
	}
	/* lecture anticipée : 0 (aucune) à PREFETCH_MAX threads, l'avance par défaut threads x chunk ; sans cache donné, le
	 * cache grandit avec elle (deux fois l'avance, plus une requête : l'avance n'en prend que la moitié au plus) */
	if (pf_threads < 0)
		pf_threads = 0;
	if (pf_threads > PREFETCH_MAX)
		pf_threads = PREFETCH_MAX;
	if (pf_chunk < 1)
		pf_chunk = 1;
	if (pf_chunk > RA_MAX)
		pf_chunk = RA_MAX;
	if (pf_ahead <= 0)
		pf_ahead = pf_threads * pf_chunk;
	if (!cache_set && (u64)(2 * pf_ahead + RA_MAX) * BLOCK > cache_bytes)
		cache_bytes = (u64)(2 * pf_ahead + RA_MAX) * BLOCK;
	if (cache_bytes < CACHE_MIN)
		cache_bytes = CACHE_MIN;
	int r = url_parse(api_url, &api);
	if (r) {
		say("api_url « %s » refusée (%s)", api_url,
		    r == -2 ? "https seulement, sauf un serveur d'essai sur le réseau local" : "adresse invalide");
		return E_INVAL;
	}
	conf_read = 1;
	return 0;
}

/* ---- le jeton d'accès (oauthd, par NetKit) ; sous tok_mx (les threads d'avance s'en servent aussi) */

static pthread_mutex_t tok_mx = PTHREAD_MUTEX_INITIALIZER;

static long token_get_(void)
{
	if (have_token)
		return 0;
	if (!kit_ok) {
		if (!netkit_kit_open()) {
			say("NetKit introuvable (lib/netkit.so)");
			return E_IO;
		}
		kit_ok = 1;
	}
	long r = oauth_token((long)account, (long)token, (long)sizeof token);
	if (r < 0) {
		char e[256] = "";
		if (oauth_error((long)e, (long)sizeof e) < 0)
			e[0] = 0;
		if (r == E_ACCES)
			say("compte %s : il faut se connecter (oauth browser %s)%s%s", account, account, e[0] ? " : " : "", e);
		else
			say("compte %s : pas de jeton (%ld)%s%s", account, r, e[0] ? " : " : "", e);
		token[0] = 0;
		return r == E_ACCES ? E_ACCES : r == E_TIMEDOUT ? E_TIMEDOUT : E_IO;
	}
	token[(size_t)r < sizeof token ? (size_t)r : sizeof token - 1] = 0;
	size_t n = strlen(token);
	while (n && (token[n - 1] == '\n' || token[n - 1] == '\r' || token[n - 1] == ' '))
		token[--n] = 0;
	if (!n) {
		say("compte %s : jeton vide", account);
		return E_ACCES;
	}
	have_token = 1;
	return 0;
}

static long token_get(void)
{
	pthread_mutex_lock(&tok_mx);
	long r = token_get_();
	pthread_mutex_unlock(&tok_mx);
	return r;
}

/* le jeton refusé par Google : redemandé à oauthd à la prochaine requête */
static void token_forget(void)
{
	pthread_mutex_lock(&tok_mx);
	have_token = 0;
	pthread_mutex_unlock(&tok_mx);
}

/* ---- HTTP/1.1 : une connexion gardée ouverte (keep-alive), refaite une fois si le serveur l'a fermée */

static SSL_CTX *ssl_ctx;
/* une connexion ; « cn » est celle du thread courant (la boucle du service : cn_main ; chaque thread d'avance : la
 * sienne, conn_new) */
struct conn {
	int fd;
	SSL *ssl;
	struct buf rx; /* reçu, pas encore lu (de rpos à rx.n) */
	size_t rpos;
	long last; /* fin de la dernière réponse (ms) */
};
static struct conn cn_main = {.fd = -1};
static __thread struct conn *cn_cur = &cn_main;
#define cn (*cn_cur)
static pthread_mutex_t net_mx = PTHREAD_MUTEX_INITIALIZER; /* (getaddrinfo, le contexte TLS) */

static struct conn *conn_new(void)
{
	struct conn *c = calloc(1, sizeof *c);
	if (c)
		c->fd = -1;
	return c;
}

static SSL_CTX *tls_ctx_(void)
{
	if (!ssl_ctx) {
		ssl_ctx = SSL_CTX_new(TLS_client_method());
		if (!ssl_ctx)
			return 0;
		SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
		SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, 0);
		SSL_CTX_set_default_verify_paths(ssl_ctx); /* (/etc/ca-bundle, docs/OPENSSL.md) */
	}
	return ssl_ctx;
}

static SSL_CTX *tls_ctx(void)
{
	pthread_mutex_lock(&net_mx);
	SSL_CTX *c = tls_ctx_();
	pthread_mutex_unlock(&net_mx);
	return c;
}

static void conn_close(void)
{
	if (cn.ssl) {
		SSL_shutdown(cn.ssl); /* (non bloquant : close_notify envoyé si possible) */
		SSL_free(cn.ssl);
		cn.ssl = 0;
	}
	if (cn.fd >= 0)
		close(cn.fd);
	cn.fd = -1;
	cn.rx.n = cn.rpos = 0;
}

static int wait_fd(int fd, short ev, long deadline)
{
	long left = deadline - now_ms();
	if (left <= 0)
		return 0;
	struct pollfd p = {fd, ev, 0};
	return poll(&p, 1, (int)(left > 200 ? 200 : left)) >= 0;
}

static int conn_open(long *dl)
{
	struct addrinfo hints, *res = 0;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	char ps[8];
	snprintf(ps, sizeof ps, "%d", api.port);
	pthread_mutex_lock(&net_mx);
	int gr = getaddrinfo(api.host, ps, &hints, &res);
	pthread_mutex_unlock(&net_mx);
	if (gr != 0 || !res) {
		say("serveur %s introuvable (réseau ?)", api.host);
		return E_IO;
	}
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
		freeaddrinfo(res);
		if (fd >= 0)
			close(fd);
		say("le serveur %s ne répond pas (port %d)", api.host, api.port);
		return E_CONNREFUSED;
	}
	freeaddrinfo(res);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	cn.fd = fd;
	cn.rx.n = cn.rpos = 0;
	if (api.tls) {
		SSL_CTX *c = tls_ctx();
		if (!c || !(cn.ssl = SSL_new(c))) {
			conn_close();
			return E_NOMEM;
		}
		SSL_set_fd(cn.ssl, fd);
		SSL_set_tlsext_host_name(cn.ssl, api.host);
		SSL_set1_host(cn.ssl, api.host);
		for (;;) {
			ERR_clear_error();
			int k = SSL_connect(cn.ssl);
			if (k == 1)
				break;
			int e = SSL_get_error(cn.ssl, k);
			if ((e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) && now_ms() < *dl) {
				wait_fd(fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, *dl);
				continue;
			}
			long v = SSL_get_verify_result(cn.ssl);
			int r = E_IO;
			if (v != X509_V_OK) {
				say("certificat de %s refusé : %s", api.host, X509_verify_cert_error_string(v));
				r = E_ACCES;
			} else {
				say("poignée de main TLS avec %s impossible", api.host);
				r = now_ms() >= *dl ? E_TIMEDOUT : E_IO;
			}
			conn_close();
			return r;
		}
	}
	st.connects++;
	return 0;
}

/* une connexion gardée qui ne servira plus : le serveur l'a fermée (lisible au repos), ou trop vieille */
static int conn_stale(void)
{
	if (cn.rpos < cn.rx.n || now_ms() - cn.last > KEEP_IDLE)
		return 1;
	struct pollfd p = {cn.fd, POLLIN, 0};
	return poll(&p, 1, 0) > 0;
}

static int c_send(const char *p, size_t n, long *dl)
{
	size_t off = 0;
	while (off < n) {
		long k;
		if (cn.ssl) {
			ERR_clear_error();
			k = SSL_write(cn.ssl, p + off, (int)(n - off));
			if (k <= 0) {
				int e = SSL_get_error(cn.ssl, (int)k);
				if ((e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) && now_ms() < *dl) {
					wait_fd(cn.fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, *dl);
					continue;
				}
				return E_IO;
			}
		} else {
			k = (long)send(cn.fd, p + off, n - off, 0);
			if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) && now_ms() < *dl) {
				wait_fd(cn.fd, POLLOUT, *dl);
				continue;
			}
			if (k <= 0)
				return E_IO;
		}
		off += (size_t)k;
	}
	return 0;
}

/* -> octets reçus, 0 : fermée par le serveur, < 0 : erreur ; dl repoussé à chaque arrivée */
static long c_recv(void *p, size_t n, long *dl)
{
	for (;;) {
		if (cn.ssl) {
			ERR_clear_error();
			int k = SSL_read(cn.ssl, p, (int)(n > 0x40000000 ? 0x40000000 : n));
			if (k > 0) {
				*dl = now_ms() + HTTP_IDLE;
				return k;
			}
			int e = SSL_get_error(cn.ssl, k);
			if (e == SSL_ERROR_ZERO_RETURN)
				return 0;
			if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
				if (now_ms() >= *dl)
					return E_TIMEDOUT;
				wait_fd(cn.fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, *dl);
				continue;
			}
			if (e == SSL_ERROR_SYSCALL && !ERR_peek_error())
				return 0; /* (fermée sans close_notify) */
			return E_IO;
		}
		long k = (long)recv(cn.fd, p, n, 0);
		if (k > 0) {
			*dl = now_ms() + HTTP_IDLE;
			return k;
		}
		if (k == 0)
			return 0;
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			if (now_ms() >= *dl)
				return E_TIMEDOUT;
			wait_fd(cn.fd, POLLIN, *dl);
			continue;
		}
		return E_IO;
	}
}

static long rx_fill(long *dl)
{
	if (cn.rpos == cn.rx.n)
		cn.rx.n = cn.rpos = 0;
	if (cn.rx.cap < cn.rx.n + 16384 + 1 && b_grow(&cn.rx, 16384))
		return E_NOMEM;
	long k = c_recv(cn.rx.p + cn.rx.n, cn.rx.cap - cn.rx.n - 1, dl);
	if (k > 0)
		cn.rx.n += (size_t)k;
	return k;
}

/* une ligne (sans « \r\n ») -> sa longueur, < 0 : erreur (RX_EOF : fermée) */
static int rx_line(char *out, size_t max, long *dl)
{
	for (;;) {
		size_t av = cn.rx.n - cn.rpos;
		char *s = cn.rx.p ? cn.rx.p + cn.rpos : 0;
		char *nl = av ? memchr(s, '\n', av) : 0;
		if (nl) {
			size_t l = (size_t)(nl - s);
			cn.rpos += l + 1;
			if (l && s[l - 1] == '\r')
				l--;
			if (l >= max)
				l = max - 1;
			memcpy(out, s, l);
			out[l] = 0;
			return (int)l;
		}
		if (av > 16384)
			return E_IO;
		long k = rx_fill(dl);
		if (k <= 0)
			return k ? (int)k : RX_EOF;
	}
}

/* n octets du corps, ajoutés à out */
static int rx_body(struct buf *out, size_t n, long *dl)
{
	if (b_grow(out, n))
		return E_NOMEM;
	size_t av = cn.rx.n - cn.rpos, c = av < n ? av : n;
	if (c)
		memcpy(out->p + out->n, cn.rx.p + cn.rpos, c);
	out->n += c;
	cn.rpos += c;
	n -= c;
	while (n) {
		long k = c_recv(out->p + out->n, n, dl);
		if (k <= 0)
			return k ? (int)k : E_IO;
		out->n += (size_t)k;
		n -= (size_t)k;
	}
	out->p[out->n] = 0;
	return 0;
}

/* des en-têtes de la réponse : Location (session d'envoi), la fin de Range (308 : octets reçus, -1 : aucun) */
struct hres {
	char loc[1024];
	long long rng;
};

/* une requête (GET, POST, PATCH, PUT ; path : depuis la racine du serveur, corps de blen octets) -> statut HTTP
 * (< 0 : erreur), le corps de la réponse dans out (au plus max octets : au-delà, la connexion est coupée,
 * E_FBIG) ; *reused : la connexion servait déjà */
static int http_once(const char *method, const char *path, const char *extra, const void *body, size_t blen,
		     struct buf *out, size_t max, struct hres *h, int *reused)
{
	long dl = now_ms() + HTTP_IDLE;
	if (cn.fd >= 0 && conn_stale())
		conn_close();
	*reused = cn.fd >= 0;
	int r;
	if (cn.fd < 0 && (r = conn_open(&dl)) < 0)
		return r;
	char hp[300];
	if (api.port == (api.tls ? 443 : 80))
		scpy(hp, api.host, sizeof hp);
	else
		snprintf(hp, sizeof hp, "%s:%d", api.host, api.port);
	struct buf req = {0};
	b_str(&req, method);
	b_str(&req, " ");
	b_str(&req, path);
	b_str(&req, " HTTP/1.1\r\nHost: ");
	b_str(&req, hp);
	b_str(&req, "\r\nUser-Agent: KorinOS-gdrivefs/2\r\nAccept: */*\r\nAccept-Encoding: identity\r\n"
		    "Authorization: Bearer ");
	pthread_mutex_lock(&tok_mx);
	b_str(&req, token);
	pthread_mutex_unlock(&tok_mx);
	b_str(&req, "\r\n");
	if (strcmp(method, "GET")) {
		char cl[48];
		snprintf(cl, sizeof cl, "Content-Length: %zu\r\n", blen);
		b_str(&req, cl);
	}
	if (extra)
		b_str(&req, extra);
	int ok = !b_str(&req, "\r\n");
	r = ok ? c_send(req.p, req.n, &dl) : E_NOMEM;
	if (req.p)
		memset(req.p, 0, req.n); /* (le jeton) */
	b_free(&req);
	if (r >= 0 && blen)
		r = c_send(body, blen, &dl);
	if (r < 0)
		goto fail;

	char line[2048];
	int status, closing, chunked;
	long long clen;
	do { /* (« 100 Continue » : la suivante) */
		r = rx_line(line, sizeof line, &dl);
		if (r < 0)
			goto fail;
		if (strncmp(line, "HTTP/1.", 7) || strlen(line) < 12) {
			r = E_IO;
			goto fail;
		}
		closing = line[7] == '0';
		status = atoi(line + 9);
		chunked = 0;
		clen = -1;
		if (h) {
			h->loc[0] = 0;
			h->rng = -1;
		}
		for (;;) {
			r = rx_line(line, sizeof line, &dl);
			if (r < 0)
				goto fail;
			if (!r)
				break;
			char *v = strchr(line, ':');
			if (!v)
				continue;
			*v++ = 0;
			v += strspn(v, " \t");
			if (!strcasecmp(line, "Content-Length"))
				clen = strtoll(v, 0, 10);
			else if (!strcasecmp(line, "Transfer-Encoding"))
				chunked = strstr(v, "chunked") != 0 || strstr(v, "Chunked") != 0;
			else if (!strcasecmp(line, "Connection"))
				closing = !strncasecmp(v, "close", 5) ? 1 : !strncasecmp(v, "keep-alive", 10) ? 0 : closing;
			else if (h && !strcasecmp(line, "Location"))
				scpy(h->loc, v, sizeof h->loc);
			else if (h && !strcasecmp(line, "Range") && !strncasecmp(v, "bytes=", 6) && strchr(v, '-'))
				h->rng = strtoll(strchr(v, '-') + 1, 0, 10);
		}
	} while (status >= 100 && status < 200);

	out->n = 0;
	if (b_grow(out, 0)) {
		r = E_NOMEM;
		goto fail;
	}
	out->p[0] = 0;
	if (status == 204 || status == 304) {
		/* pas de corps */
	} else if (chunked) {
		for (;;) {
			r = rx_line(line, sizeof line, &dl);
			if (r < 0)
				goto fail;
			unsigned long long n = strtoull(line, 0, 16);
			if (!n) {
				do /* (en-têtes de fin) */
					r = rx_line(line, sizeof line, &dl);
				while (r > 0);
				if (r < 0)
					goto fail;
				break;
			}
			if (out->n + n > max) {
				r = E_FBIG;
				goto fail;
			}
			if ((r = rx_body(out, (size_t)n, &dl)) < 0)
				goto fail;
			if ((r = rx_line(line, sizeof line, &dl)) != 0) {
				r = r < 0 ? r : E_IO;
				goto fail;
			}
		}
	} else if (clen >= 0) {
		if ((u64)clen > max) {
			r = E_FBIG;
			goto fail;
		}
		if ((r = rx_body(out, (size_t)clen, &dl)) < 0)
			goto fail;
	} else { /* jusqu'à la fermeture */
		closing = 1;
		for (;;) {
			size_t av = cn.rx.n - cn.rpos;
			if (out->n + av > max) {
				r = E_FBIG;
				goto fail;
			}
			if ((r = rx_body(out, av, &dl)) < 0)
				goto fail;
			long k = rx_fill(&dl);
			if (!k)
				break;
			if (k < 0) {
				r = (int)k;
				goto fail;
			}
		}
	}
	if (closing)
		conn_close();
	cn.last = now_ms();
	return status;
fail:
	conn_close();
	return r == RX_EOF ? E_IO : r;
}

/* une requête ; une connexion gardée que le serveur avait fermée est refaite, une fois */
static int http_req(const char *method, const char *path, const char *extra, const void *body, size_t blen,
		    struct buf *out, size_t max, struct hres *h)
{
	int reused, r = http_once(method, path, extra, body, blen, out, max, h, &reused);
	if (r < 0 && reused && r != E_FBIG) {
		st.retries++;
		r = http_once(method, path, extra, body, blen, out, max, h, &reused);
	}
	return r;
}

/* ---- l'API Drive */

/* le message d'erreur de Google ({"error": {"message": ..., "errors": [{"reason": ...}]}}) */
static void api_error(const char *body, char *reason, size_t rn, char *text, size_t tn)
{
	reason[0] = text[0] = 0;
	const char *e = json_find(body, "error");
	if (!e)
		return;
	char *m = json_str(e, "message");
	if (m) {
		scpy(text, m, tn);
		free(m);
	}
	const char *a = json_find(e, "errors");
	if (a && *a == '[') {
		char *r = json_str(json_skip_ws(a + 1), "reason");
		if (r) {
			scpy(reason, r, rn);
			free(r);
		}
	}
	if (!reason[0]) { /* (sans « errors » : « status » ou le code de l'erreur en mot) */
		char *r = json_str(e, "status");
		if (r) {
			scpy(reason, r, rn);
			free(r);
		}
	}
}

/* une requête à l'API (path : après api_url, ou depuis la racine du serveur si raw : une session d'envoi), avec
 * le jeton : 401 -> jeton redemandé et un nouvel essai ; 429, 403 rateLimitExceeded et 5xx -> nouveaux essais
 * après 1, 2, 4 s -> statut 2xx (416 ; 308 si h), sinon une erreur de Korin */
static long api_call(const char *method, const char *path, int raw, const char *extra, const void *body, size_t blen,
		     struct buf *out, size_t max, struct hres *h)
{
	int again = 0, backoff = 0;
	struct buf full = {0};
	if (!raw) {
		b_str(&full, api.path);
		if (b_str(&full, path))
			return E_NOMEM;
		path = full.p;
	}
	for (;;) {
		long r = token_get();
		if (r < 0) {
			b_free(&full);
			return r;
		}
		int s = http_req(method, path, extra, body, blen, out, max, h);
		if (s >= 0 && !((s >= 200 && s < 300) || s == 416 || (h && s == 308)))
			goto refused;
		/* le réseau a lâché (connexion coupée, refusée, sans réponse) : une lecture (GET) se redemande sans risque,
		 * sur une connexion neuve, après 1, 2, 4 s (une copie de plusieurs Go en traverse) */
		if (s < 0 && s != E_FBIG && s != E_NOMEM && !strcmp(method, "GET") && backoff < BACKOFF_MAX) {
			say("%.90s : %s, nouvel essai dans %u s", path,
			    s == E_TIMEDOUT ? "pas de réponse (délai dépassé)" : "échec du réseau", 1u << backoff);
			conn_close();
			sleep(1u << backoff);
			backoff++;
			st.netretries++;
			continue;
		}
		if (s < 0) {
			if (s == E_FBIG)
				say("%.90s : réponse plus grande que demandé (%zu octets au plus) : coupée", path, max);
			else
				say("%.90s : %s, abandon", path, s == E_TIMEDOUT ? "pas de réponse (délai dépassé)" : "échec du réseau");
			b_free(&full);
			return s == E_FBIG ? E_IO : s;
		}
		b_free(&full);
		return s;
	refused:;
		char reason[64], text[256];
		api_error(out->p, reason, sizeof reason, text, sizeof text);
		if (s == 401 && !again) { /* (jeton expiré ou révoqué : oauthd le refait s'il le peut) */
			again = 1;
			token_forget();
			continue;
		}
		int rate = s == 429 || s >= 500 ||
			   (s == 403 && (!strcmp(reason, "rateLimitExceeded") || !strcmp(reason, "userRateLimitExceeded")));
		if (rate && backoff < BACKOFF_MAX) {
			sleep(1u << backoff);
			backoff++;
			continue;
		}
		say("%s %.90s : HTTP %d%s%s%s%s", method, path, s, reason[0] ? " " : "", reason, text[0] ? " : " : "", text);
		b_free(&full);
		if (s == 401) {
			token_forget();
			say("compte %s : jeton refusé par Google (oauth browser %s pour se reconnecter)", account, account);
			return E_ACCES;
		}
		if (s == 404 || s == 410)
			return E_NOENT;
		if (rate && s < 500) /* (quota : plus tard) */
			return E_AGAIN;
		if (s == 403 && !strcmp(reason, "storageQuotaExceeded"))
			return E_NOSPC;
		if (s == 403)
			return E_ACCES;
		if (s == 400)
			return E_INVAL;
		return E_IO;
	}
}

static long api_get(const char *path, const char *range, struct buf *out, size_t max)
{
	char extra[96] = "";
	if (range)
		snprintf(extra, sizeof extra, "Range: bytes=%s\r\n", range);
	return api_call("GET", path, 0, extra[0] ? extra : 0, 0, 0, out, max, 0);
}

/* ---- les dossiers : listes gardées list_ttl_ms */

struct ent {
	char *name; /* nom montré (doublons suffixés, « / » remplacé) */
	char *id;
	char *ver; /* version des blocs : modifiedTime, md5Checksum */
	char *created;
	u64 size;
	i64 mtime;
	int isdir;
};

struct dirlist {
	char *id;
	long loaded;
	u64 lru;
	struct ent *ents;
	int n;
};
static struct dirlist lists[LIST_MAX];
static u64 list_clock;

/* le nœud d'un chemin (copié : les listes peuvent être refaites) */
struct node {
	char id[ID_MAX], ver[VER_MAX];
	u64 size;
	i64 mtime;
	int isdir;
};

static void ents_free(struct ent *e, int n)
{
	for (int i = 0; i < n; i++) {
		free(e[i].name);
		free(e[i].id);
		free(e[i].ver);
		free(e[i].created);
	}
	free(e);
}

static void list_free(struct dirlist *d)
{
	free(d->id);
	ents_free(d->ents, d->n);
	memset(d, 0, sizeof *d);
}

static int ent_cmp_dup(const void *a, const void *b)
{
	const struct ent *x = a, *y = b;
	int c = strcmp(x->name, y->name);
	if (!c)
		c = strcmp(x->created ? x->created : "", y->created ? y->created : "");
	if (!c)
		c = strcmp(x->id, y->id);
	return c;
}

static int ent_cmp_name(const void *a, const void *b)
{
	return strcmp(((const struct ent *)a)->name, ((const struct ent *)b)->name);
}

static int name_used(struct ent *e, int n, const char *name)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(e[i].name, name))
			return 1;
	return 0;
}

/* « rapport.pdf », 2 -> « rapport (2).pdf » ; un dossier ou sans extension : « Photos (2) » */
static char *suffixed(const char *name, int k, int isdir)
{
	const char *dot = isdir ? 0 : strrchr(name, '.');
	if (dot && (dot == name || strlen(dot) > 12 || strchr(dot, ' ')))
		dot = 0;
	size_t base = dot ? (size_t)(dot - name) : strlen(name);
	size_t n = strlen(name) + 16;
	char *s = malloc(n);
	if (s)
		snprintf(s, n, "%.*s (%d)%s", (int)base, name, k, dot ? dot : "");
	return s;
}

/* un nom de Drive -> un nom de Korin : « / » -> « ∕ » ; vide, « . », « .. » : « _ » devant */
static char *korin_name(const char *s)
{
	struct buf b = {0};
	if (!*s || !strcmp(s, ".") || !strcmp(s, ".."))
		b_str(&b, "_");
	for (; *s; s++) {
		if (*s == '/')
			b_str(&b, "\xE2\x88\x95");
		else
			b_add(&b, s, 1);
	}
	return b.p;
}

/* les éléments de « files » d'une page de files.list, ajoutés à *pe */
static int parse_files(const char *body, struct ent **pe, int *pn, int *cap)
{
	const char *a = json_find(body, "files");
	if (!a || *a != '[')
		return 0;
	const char *p = json_skip_ws(a + 1);
	while (*p == '{') {
		const char *end = json_skip(p);
		char *mime = json_str(p, "mimeType");
		int isdir = mime && !strcmp(mime, FOLDER_MIME);
		int hidden = mime && !isdir && !strncmp(mime, GAPPS_MIME, strlen(GAPPS_MIME));
		free(mime);
		char *id = hidden ? 0 : json_str(p, "id");
		char *name = id ? json_str(p, "name") : 0;
		if (id && name) {
			if (*pn == *cap) {
				int c = *cap ? *cap * 2 : 64;
				struct ent *q = realloc(*pe, (size_t)c * sizeof *q);
				if (!q) {
					free(id);
					free(name);
					return E_NOMEM;
				}
				*pe = q;
				*cap = c;
			}
			struct ent *e = &(*pe)[(*pn)++];
			memset(e, 0, sizeof *e);
			e->id = id;
			e->name = korin_name(name);
			e->isdir = isdir;
			e->size = isdir ? 0 : json_u64(p, "size", 0);
			char *mt = json_str(p, "modifiedTime");
			char *md5 = json_str(p, "md5Checksum");
			e->mtime = parse_time(mt);
			char v[VER_MAX];
			snprintf(v, sizeof v, "%s/%s", mt ? mt : "", md5 ? md5 : "");
			e->ver = strdup(v);
			e->created = json_str(p, "createdTime");
			free(mt);
			free(md5);
			free(name);
			if (!e->name || !e->ver)
				return E_NOMEM;
		} else {
			free(id);
			free(name);
		}
		p = json_skip_ws(end);
		if (*p != ',')
			break;
		p = json_skip_ws(p + 1);
	}
	return 0;
}

/* la liste d'un dossier depuis Drive (toutes les pages), doublons nommés -> 0 ou une erreur */
static long list_load(const char *id, struct ent **pe, int *pn)
{
	struct ent *e = 0;
	int n = 0, cap = 0;
	char *page = 0;
	struct buf body = {0};
	long r = 0;
	char q[ID_MAX * 2 + 64], qi[ID_MAX * 2];
	size_t k = 0;
	for (const char *s = id; *s && k + 2 < sizeof qi; s++) { /* (« ' » et « \ » échappés) */
		if (*s == '\'' || *s == '\\')
			qi[k++] = '\\';
		qi[k++] = *s;
	}
	qi[k] = 0;
	snprintf(q, sizeof q, "'%s' in parents and trashed=false", qi);
	do {
		struct buf path = {0};
		b_str(&path, "/drive/v3/files?q=");
		b_enc(&path, q);
		b_str(&path, "&fields=");
		b_enc(&path, "nextPageToken,files(id,name,mimeType,size,modifiedTime,md5Checksum,createdTime)");
		b_str(&path, "&pageSize=1000");
		if (page) {
			b_str(&path, "&pageToken=");
			b_enc(&path, page);
		}
		free(page);
		page = 0;
		if (!path.p) {
			r = E_NOMEM;
			break;
		}
		r = api_get(path.p, 0, &body, JSON_MAX);
		b_free(&path);
		if (r < 0)
			break;
		if (r != 200) {
			r = E_IO;
			break;
		}
		r = parse_files(body.p, &e, &n, &cap);
		if (r < 0)
			break;
		page = json_str(body.p, "nextPageToken");
		if (page && !*page) {
			free(page);
			page = 0;
		}
	} while (page);
	free(page);
	b_free(&body);
	if (r < 0) {
		ents_free(e, n);
		return r;
	}
	st.lists++;
	/* doublons : le premier créé garde le nom, les suivants « (2) », « (3) »... (sans prendre un nom existant) */
	if (n > 1) {
		qsort(e, (size_t)n, sizeof *e, ent_cmp_dup);
		for (int i = 0; i < n;) {
			int j = i + 1;
			while (j < n && !strcmp(e[j].name, e[i].name))
				j++;
			for (int m = i + 1, k2 = 2; m < j; m++, k2++) {
				char *s = 0;
				for (;; k2++) {
					s = suffixed(e[i].name, k2, e[m].isdir);
					if (!s || !name_used(e, n, s))
						break;
					free(s);
				}
				if (!s) {
					ents_free(e, n);
					return E_NOMEM;
				}
				free(e[m].name);
				e[m].name = s;
			}
			i = j;
		}
		qsort(e, (size_t)n, sizeof *e, ent_cmp_name);
	}
	*pe = e;
	*pn = n;
	return 0;
}

/* la liste d'un dossier : gardée, ou demandée si absente ou trop vieille -> 0 (*out) ou une erreur */
static long dir_get_(const char *id, struct dirlist **out, int any)
{
	struct dirlist *d = 0, *lru = &lists[0];
	for (int i = 0; i < LIST_MAX; i++) {
		if (lists[i].id && !strcmp(lists[i].id, id)) {
			d = &lists[i];
			break;
		}
		if (!lists[i].id || (lru->id && lists[i].lru < lru->lru))
			lru = &lists[i];
	}
	if (d && (any || now_ms() - d->loaded < list_ttl_ms)) {
		d->lru = ++list_clock;
		*out = d;
		return 0;
	}
	struct ent *e;
	int n;
	long r = list_load(id, &e, &n);
	if (r < 0) {
		if (d && (r == E_IO || r == E_TIMEDOUT || r == E_CONNREFUSED)) { /* (hors ligne : l'ancienne liste) */
			d->lru = ++list_clock;
			*out = d;
			return 0;
		}
		if (d)
			list_free(d);
		return r;
	}
	if (!d) {
		d = lru;
		list_free(d);
		d->id = strdup(id);
		if (!d->id) {
			ents_free(e, n);
			return E_NOMEM;
		}
	} else {
		ents_free(d->ents, d->n);
	}
	d->ents = e;
	d->n = n;
	d->loaded = now_ms();
	d->lru = ++list_clock;
	*out = d;
	return 0;
}

static long dir_get(const char *id, struct dirlist **out)
{
	return dir_get_(id, out, 0);
}

/* un nom dans une liste : exact, sinon sans la casse s'il n'y en a qu'un */
static struct ent *dir_find(struct dirlist *d, const char *name, size_t len)
{
	struct ent *ci = 0;
	int nci = 0;
	for (int i = 0; i < d->n; i++) {
		const char *s = d->ents[i].name;
		if (strlen(s) != len)
			continue;
		if (!memcmp(s, name, len))
			return &d->ents[i];
		if (!strncasecmp(s, name, len)) {
			ci = &d->ents[i];
			nci++;
		}
	}
	return nci == 1 ? ci : 0;
}

/* chemin du volume (« a/b ») -> le nœud ; E_NOENT, E_NOTDIR, ou une erreur du réseau */
static long resolve(const char *path, struct node *nd)
{
	memset(nd, 0, sizeof *nd);
	scpy(nd->id, root_id, sizeof nd->id);
	nd->isdir = 1;
	const char *p = path;
	for (;;) {
		while (*p == '/')
			p++;
		if (!*p)
			return 0;
		size_t len = strcspn(p, "/");
		if (len == 1 && *p == '.') {
			p += len;
			continue;
		}
		if (len == 2 && p[0] == '.' && p[1] == '.')
			return E_INVAL;
		if (!nd->isdir)
			return E_NOTDIR;
		/* traverser un dossier : sa liste, même plus vieille que ttl (seul l'identifiant sert) ; relue seulement si le
		 * nom y manque, ou pour un fichier au bout (sa taille et sa version doivent être à jour). Ouvrir un dossier
		 * ne recharge ainsi que lui, pas tous ceux du chemin (Fichiers fait un stat par fichier). */
		struct dirlist *d;
		long r = dir_get_(nd->id, &d, 1);
		if (r < 0)
			return r;
		struct ent *e = dir_find(d, p, len);
		int last = !p[len] || !p[len + strspn(p + len, "/")];
		if ((!e || (last && !e->isdir)) && now_ms() - d->loaded >= list_ttl_ms) {
			if ((r = dir_get(nd->id, &d)) < 0)
				return r;
			e = dir_find(d, p, len);
		}
		if (!e)
			return E_NOENT;
		scpy(nd->id, e->id, sizeof nd->id);
		scpy(nd->ver, e->ver, sizeof nd->ver);
		nd->size = e->size;
		nd->mtime = e->mtime;
		nd->isdir = e->isdir;
		p += len;
	}
}

/* ---- le cache des blocs (LRU, partagé par tous les fichiers ouverts) et la lecture anticipée en parallèle
 *
 * Une lecture qui suit la précédente fait aller chercher la suite d'avance par pf_threads threads (réglage threads, 4 par défaut), chacun sur sa
 * connexion HTTPS : jusqu'à pf_ahead blocs devant la lecture (ahead), par requêtes de pf_chunk blocs (chunk). Pendant que l'une
 * attend Google (souvent des centaines de ms), les autres reçoivent : le flux ne retombe plus à zéro entre deux
 * requêtes, et plusieurs connexions additionnent leur débit. Un bloc demandé est réservé dans le cache (fill) :
 * jamais demandé deux fois, et la lecture qui en a besoin attend qu'il arrive. La boucle du service reste seule à
 * traiter les requêtes ; le cache, le jeton et la résolution du nom sont sous verrou. */

#include <pthread.h>


struct blk {
	int used, fill; /* fill : demandé, pas encore arrivé */
	char id[ID_MAX], ver[VER_MAX];
	u64 index; /* position / BLOCK */
	u32 len;
	u64 lru;
	u8 *data;
};
static struct blk *blks;
static int nblks;
static u64 blk_clock;
static __thread struct buf media; /* (le corps d'une requête alt=media, gardé d'une fois à l'autre ; un par thread) */
static pthread_mutex_t cache_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cache_cv = PTHREAD_COND_INITIALIZER;

static int cache_init(void)
{
	if (blks)
		return 0;
	nblks = (int)(cache_bytes / BLOCK);
	blks = calloc((size_t)nblks, sizeof *blks);
	return blks ? 0 : E_NOMEM;
}

static void cache_drop(void)
{
	pthread_mutex_lock(&cache_mx);
	for (int i = 0; i < nblks; i++) {
		if (blks[i].fill) /* (en route : laissé à son thread) */
			continue;
		free(blks[i].data);
		memset(&blks[i], 0, sizeof blks[i]);
	}
	pthread_mutex_unlock(&cache_mx);
	b_free(&media);
}

/* (sous cache_mx) */
static struct blk *blk_find(const char *id, const char *ver, u64 index)
{
	for (int i = 0; i < nblks; i++) {
		struct blk *b = &blks[i];
		if (b->used && b->index == index && !strcmp(b->id, id) && !strcmp(b->ver, ver))
			return b;
	}
	return 0;
}

static int blk_count(void)
{
	int c = 0;
	pthread_mutex_lock(&cache_mx);
	for (int i = 0; i < nblks; i++)
		c += blks[i].used && !blks[i].fill;
	pthread_mutex_unlock(&cache_mx);
	return c;
}

/* (sous cache_mx) une place : libre, sinon la moins récente qui n'est pas en route */
static struct blk *blk_slot(void)
{
	struct blk *v = 0;
	for (int i = 0; i < nblks; i++) {
		if (blks[i].fill)
			continue;
		if (!blks[i].used) {
			v = &blks[i];
			break;
		}
		if (!v || blks[i].lru < v->lru)
			v = &blks[i];
	}
	if (!v)
		return 0;
	if (v->used)
		st.evictions++;
	v->used = 0;
	if (!v->data && !(v->data = malloc(BLOCK)))
		return 0;
	return v;
}

/* (sous cache_mx) réserve les blocs first.. (count au plus, jusqu'au premier déjà là ou en route, sans passer la
 * fin du fichier) -> nombre réservé */
static int blk_reserve(const char *id, const char *ver, u64 size, u64 first, int count)
{
	int n = 0;
	for (; n < count; n++) {
		u64 b = first + (u64)n;
		if (b * BLOCK >= size || blk_find(id, ver, b))
			break;
		struct blk *k = blk_slot();
		if (!k)
			break;
		scpy(k->id, id, sizeof k->id);
		scpy(k->ver, ver, sizeof k->ver);
		k->index = b;
		k->len = 0;
		k->lru = ++blk_clock;
		k->used = k->fill = 1;
	}
	return n;
}

/* (sous cache_mx) les réservations first..first+count-1 abandonnées */
static void blk_release(const char *id, const char *ver, u64 first, int count)
{
	for (int i = 0; i < count; i++) {
		struct blk *k = blk_find(id, ver, first + (u64)i);
		if (k && k->fill)
			k->used = k->fill = 0;
	}
	pthread_cond_broadcast(&cache_cv);
}

/* les blocs réservés first..first+count-1, en une requête (Range ; la connexion du thread appelant) -> 0, 1 : fin
 * du fichier, < 0 : erreur ; les réservations sont remplies, ou rendues en cas d'échec */
static long fetch_range(const char *id, const char *ver, u64 size, u64 first, int count)
{
	u64 a = first * BLOCK, z = (first + (u64)count) * BLOCK;
	if (z > size)
		z = size;
	long s = 1;
	if (a >= z)
		goto release;
	char path[ID_MAX * 3 + 64], range[64];
	struct buf p = {0};
	b_str(&p, "/drive/v3/files/");
	b_enc(&p, id);
	b_str(&p, "?alt=media");
	if (!p.p) {
		s = E_NOMEM;
		goto release;
	}
	scpy(path, p.p, sizeof path);
	b_free(&p);
	snprintf(range, sizeof range, "%llu-%llu", (unsigned long long)a, (unsigned long long)(z - 1));
	for (int t = 0;; t++) {
		s = api_get(path, range, &media, (size_t)(z - a));
		if (s < 0)
			goto release;
		if (s != 206 || media.n == z - a)
			break;
		/* (plus court que demandé : pris tel quel, ce serait la fin du fichier, une copie tronquée sans erreur) */
		say("%s : %zu octets reçus pour %llu demandés (%s)%s", id, media.n, (unsigned long long)(z - a), range,
		    t < BACKOFF_MAX ? ", redemandé" : "");
		if (t >= BACKOFF_MAX) {
			s = E_IO;
			goto release;
		}
		conn_close();
		__atomic_add_fetch(&st.netretries, 1, __ATOMIC_RELAXED);
	}
	if (s == 416 || !media.n) {
		s = 1;
		goto release;
	}
	if (s == 200 && a) { /* (Range ignoré : le début du fichier) */
		say("%s : le serveur ignore Range", id);
		s = E_IO;
		goto release;
	}
	pthread_mutex_lock(&cache_mx);
	st.fetches++;
	st.fetched += media.n;
	int filled = 0;
	for (u64 off = 0, b = first; off < media.n && b < first + (u64)count; off += BLOCK, b++) {
		struct blk *k = blk_find(id, ver, b);
		if (!k || !k->fill) /* (réservation perdue : vidage du cache) */
			continue;
		size_t l = media.n - off < BLOCK ? media.n - off : BLOCK;
		memcpy(k->data, media.p + off, l);
		k->len = (u32)l;
		k->fill = 0;
		filled++;
	}
	blk_release(id, ver, first, count); /* (le reste, au-delà de la fin reçue) */
	pthread_mutex_unlock(&cache_mx);
	return 0;
release:
	pthread_mutex_lock(&cache_mx);
	blk_release(id, ver, first, count);
	pthread_mutex_unlock(&cache_mx);
	return s;
}

/* ---- les threads de lecture anticipée */

struct pf_job {
	char id[ID_MAX], ver[VER_MAX];
	u64 size, first;
	int count;
};
static struct pf_job pf_q[PREFETCH_MAX];
static int pf_n, pf_busy, pf_started;
static pthread_cond_t pf_cv = PTHREAD_COND_INITIALIZER; /* (avec cache_mx) */

static struct conn *conn_new(void);

static void *pf_thread(void *arg)
{
	(void)arg;
	cn_cur = conn_new(); /* sa connexion à lui */
	if (!cn_cur)
		return 0;
	pthread_mutex_lock(&cache_mx);
	for (;;) {
		while (!pf_n) {
			struct timespec t;
			clock_gettime(CLOCK_REALTIME, &t);
			t.tv_sec += KEEP_IDLE / 1000;
			if (pthread_cond_timedwait(&pf_cv, &cache_mx, &t) && !pf_n && cn.fd >= 0) {
				pthread_mutex_unlock(&cache_mx); /* (au repos : la connexion fermée) */
				conn_close();
				pthread_mutex_lock(&cache_mx);
			}
		}
		struct pf_job j = pf_q[0];
		memmove(pf_q, pf_q + 1, (size_t)--pf_n * sizeof *pf_q);
		pf_busy++;
		pthread_mutex_unlock(&cache_mx);
		long r = fetch_range(j.id, j.ver, j.size, j.first, j.count);
		pthread_mutex_lock(&cache_mx);
		pf_busy--;
		if (r >= 0)
			st.prefetched += (u64)j.count;
		pthread_cond_broadcast(&cache_cv);
	}
	return 0;
}

/* (sous cache_mx) la suite d'une lecture à la suite demandée d'avance, à partir du bloc from */
static void prefetch(struct ouvert *o, u64 from)
{
	int ahead = pf_ahead < nblks / 2 ? pf_ahead : nblks / 2, chunk = pf_chunk < ahead ? pf_chunk : ahead;
	if (chunk < 1)
		return;
	if (!pf_started) {
		pf_started = 1;
		for (int i = 0; i < pf_threads; i++) {
			pthread_t th;
			pthread_attr_t at;
			pthread_attr_init(&at);
			pthread_attr_setstacksize(&at, 256 * 1024);
			if (pthread_create(&th, &at, pf_thread, 0) == 0)
				pthread_detach(th);
			pthread_attr_destroy(&at);
		}
	}
	u64 end = from + (u64)ahead, last = (o->size + BLOCK - 1) / BLOCK;
	if (end > last)
		end = last;
	for (u64 b = from; b < end && pf_n + pf_busy < pf_threads;) {
		if (blk_find(o->id, o->ver, b)) { /* (déjà là ou en route) */
			b++;
			continue;
		}
		int c = blk_reserve(o->id, o->ver, o->size, b, (int)(end - b < (u64)chunk ? end - b : (u64)chunk));
		if (!c)
			break;
		struct pf_job *j = &pf_q[pf_n++];
		scpy(j->id, o->id, sizeof j->id);
		scpy(j->ver, o->ver, sizeof j->ver);
		j->size = o->size;
		j->first = b;
		j->count = c;
		pthread_cond_signal(&pf_cv);
		b += (u64)c;
	}
}

/* lit jusqu'à n octets à pos, par le cache -> octets, < 0 : erreur */
static long read_at(struct ouvert *o, u8 *dst, u64 n, i64 pos)
{
	if (pos < 0)
		return E_INVAL;
	if ((u64)pos >= o->size || !n)
		return 0;
	if (n > o->size - (u64)pos)
		n = o->size - (u64)pos;
	if (o->wr) { /* (ouvert pour écrire : le tampon d'envoi) */
		ssize_t k = pread(o->fd, dst, (size_t)n, (off_t)pos);
		return k < 0 ? E_IO : (long)k;
	}
	long r = cache_init();
	if (r < 0)
		return r;
	/* l'avance : une lecture près de la fin de la précédente (à un bloc près) la garde, une autre la remet à 1 */
	int seq = o->next != NONE && (u64)pos + BLOCK > o->next && (u64)pos < o->next + BLOCK;
	if (!seq)
		o->ra = 1;
	u64 done = 0;
	pthread_mutex_lock(&cache_mx);
	while (done < n) {
		u64 p = (u64)pos + done, b = p / BLOCK;
		struct blk *k = blk_find(o->id, o->ver, b);
		if (k && k->fill) { /* en route (lecture anticipée) : attendu */
			st.waits++;
			pthread_cond_wait(&cache_cv, &cache_mx);
			continue;
		}
		if (k) {
			st.hits++;
		} else {
			st.misses++;
			int count = 1;
			if (seq) { /* 2, 4, 8, puis 16 blocs d'un coup (au plus la moitié du cache) */
				o->ra = o->ra * 2 > RA_MAX ? RA_MAX : o->ra * 2;
				count = o->ra;
			}
			if (count > nblks / 2)
				count = nblks / 2;
			count = blk_reserve(o->id, o->ver, o->size, b, count);
			if (!count) {
				r = E_NOMEM;
				break;
			}
			if (seq) /* (la suite, par les threads, pendant que celle-ci arrive) */
				prefetch(o, b + (u64)count);
			pthread_mutex_unlock(&cache_mx);
			r = fetch_range(o->id, o->ver, o->size, b, count);
			pthread_mutex_lock(&cache_mx);
			if (r < 0)
				break;
			if (r > 0 || !(k = blk_find(o->id, o->ver, b)) || k->fill)
				break;
		}
		k->lru = ++blk_clock;
		u64 off = p - b * BLOCK;
		if (off >= k->len)
			break;
		u64 c = k->len - off < n - done ? k->len - off : n - done;
		memcpy(dst + done, k->data + off, c);
		done += c;
		if (k->len < BLOCK && (b + 1) * BLOCK < o->size) /* (fichier plus court que vu dans la liste) */
			break;
		if (seq && (p + c) % BLOCK == 0) /* (un bloc fini : l'avance suit) */
			prefetch(o, b + 1);
	}
	pthread_mutex_unlock(&cache_mx);
	if (r < 0 && !done)
		return r;
	o->next = (u64)pos + done;
	return (long)done;
}

/* ---- écrire (version 2) : créer, modifier, mkdir, effacer (à la corbeille), renommer, dater
 *
 * Un fichier ouvert pour écrire est tenu dans un fichier du tampon d'envoi (spool, HOME:/gdrive/spool) : copié
 * de Drive à l'ouverture (sauf O_TRUNC), lu et écrit là, puis envoyé en entier à la fermeture (ou FS_SYNC) par
 * une session d'envoi « resumable » (morceaux de 8 Mo, reprise au dernier octet reçu après une coupure). Un
 * fichier créé existe sur Drive dès l'ouverture (vide). Effacer met à la corbeille de Drive (30 jours). */

/* une liste gardée oubliée (le dossier a changé) */
static void list_drop(const char *id)
{
	for (int i = 0; i < LIST_MAX; i++)
		if (lists[i].id && !strcmp(lists[i].id, id))
			list_free(&lists[i]);
}

/* un nom de Korin -> le nom sur Drive (« ∕ » redevient « / ») */
static long drive_name(const char *s, size_t len, char *out, size_t max)
{
	size_t k = 0;
	for (size_t i = 0; i < len; i++) {
		char c = s[i];
		if (i + 2 < len && !memcmp(s + i, "\xE2\x88\x95", 3)) {
			c = '/';
			i += 2;
		}
		if (k + 1 >= max)
			return E_NAMETOOLONG;
		out[k++] = c;
	}
	out[k] = 0;
	return 0;
}

/* une chaîne JSON (avec ses guillemets) */
static int b_json(struct buf *b, const char *s)
{
	b_str(b, "\"");
	for (const u8 *p = (const u8 *)s; *p; p++) {
		char t[8];
		if (*p == '"' || *p == '\\') {
			t[0] = '\\';
			t[1] = (char)*p;
			b_add(b, t, 2);
		} else if (*p < 0x20) {
			snprintf(t, sizeof t, "\\u%04x", *p);
			b_str(b, t);
		} else {
			b_add(b, p, 1);
		}
	}
	return b_str(b, "\"");
}

/* le dossier parent d'un chemin, le nom sur Drive du dernier élément, et cet élément -> 1 trouvé (*nd), 0 absent,
 * < 0 une erreur (la racine : E_ACCES) */
static long locate(const char *path, struct node *par, struct node *nd, char *name, size_t nmax)
{
	size_t n = strlen(path);
	while (n && path[n - 1] == '/')
		n--;
	size_t b = n;
	while (b && path[b - 1] != '/')
		b--;
	const char *leaf = path + b;
	size_t ll = n - b;
	if (!ll)
		return E_ACCES;
	if ((ll == 1 && leaf[0] == '.') || (ll == 2 && leaf[0] == '.' && leaf[1] == '.'))
		return E_INVAL;
	long r = drive_name(leaf, ll, name, nmax);
	if (r < 0)
		return r;
	char pp[FS_DATA_MAX];
	if (b >= sizeof pp)
		return E_NAMETOOLONG;
	memcpy(pp, path, b);
	pp[b] = 0;
	if ((r = resolve(pp, par)) < 0)
		return r;
	if (!par->isdir)
		return E_NOTDIR;
	struct dirlist *d;
	if ((r = dir_get(par->id, &d)) < 0)
		return r;
	struct ent *e = dir_find(d, leaf, ll);
	if (!e)
		return 0;
	memset(nd, 0, sizeof *nd);
	scpy(nd->id, e->id, sizeof nd->id);
	scpy(nd->ver, e->ver, sizeof nd->ver);
	nd->size = e->size;
	nd->mtime = e->mtime;
	nd->isdir = e->isdir;
	return 1;
}

/* une requête de métadonnées (corps JSON) -> 0, ou une erreur ; la réponse dans *out si donné */
static long meta_req(const char *method, const char *path, const char *json, struct buf *out)
{
	struct buf tmp = {0};
	long s = api_call(method, path, 0, "Content-Type: application/json; charset=UTF-8\r\n", json, strlen(json),
			  out ? out : &tmp, 1u << 20, 0);
	b_free(&tmp);
	return s < 0 ? s : 0;
}

static const char *mime_of(const char *name)
{
	static const char *const t[][2] = {
		{"txt", "text/plain"}, {"md", "text/markdown"}, {"csv", "text/csv"}, {"html", "text/html"},
		{"htm", "text/html"}, {"css", "text/css"}, {"js", "text/javascript"}, {"json", "application/json"},
		{"xml", "application/xml"}, {"pdf", "application/pdf"}, {"zip", "application/zip"},
		{"gz", "application/gzip"}, {"tar", "application/x-tar"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
		{"png", "image/png"}, {"gif", "image/gif"}, {"webp", "image/webp"}, {"svg", "image/svg+xml"},
		{"bmp", "image/bmp"}, {"mp3", "audio/mpeg"}, {"ogg", "audio/ogg"}, {"wav", "audio/wav"},
		{"flac", "audio/flac"}, {"mp4", "video/mp4"}, {"mkv", "video/x-matroska"}, {"webm", "video/webm"},
		{"doc", "application/msword"},
		{"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
		{"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
		{"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
		{"odt", "application/vnd.oasis.opendocument.text"},
		{"ods", "application/vnd.oasis.opendocument.spreadsheet"},
	};
	const char *dot = strrchr(name, '.');
	if (dot && dot != name)
		for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
			if (!strcasecmp(dot + 1, t[i][0]))
				return t[i][1];
	return "application/octet-stream";
}

/* un fichier ou un dossier créé dans parent -> 0 (id), ou une erreur */
static long drive_create(const char *parent, const char *name, const char *mime, char *id, size_t idmax)
{
	struct buf j = {0}, out = {0};
	b_str(&j, "{\"name\":");
	b_json(&j, name);
	b_str(&j, ",\"mimeType\":");
	b_json(&j, mime);
	b_str(&j, ",\"parents\":[");
	b_json(&j, parent);
	if (b_str(&j, "]}"))
		return E_NOMEM;
	long r = meta_req("POST", "/drive/v3/files?fields=id", j.p, &out);
	b_free(&j);
	if (r >= 0) {
		char *s = json_str(out.p, "id");
		if (s && *s && strlen(s) < idmax)
			scpy(id, s, idmax);
		else
			r = E_IO;
		free(s);
	}
	b_free(&out);
	list_drop(parent);
	return r;
}

/* PATCH des métadonnées d'un élément (query : « &addParents=... » ou rien) */
static long drive_patch(const char *id, const char *query, const char *json)
{
	struct buf p = {0};
	b_str(&p, "/drive/v3/files/");
	b_enc(&p, id);
	b_str(&p, "?fields=id");
	if (b_str(&p, query ? query : ""))
		return E_NOMEM;
	long r = meta_req("PATCH", p.p, json, 0);
	b_free(&p);
	return r;
}

/* un dossier vraiment vide (Docs et raccourcis, cachés ici, comptent) -> 1, 0, ou une erreur */
static long drive_empty(const char *id)
{
	char q[ID_MAX * 2 + 64], qi[ID_MAX * 2];
	size_t k = 0;
	for (const char *s = id; *s && k + 2 < sizeof qi; s++) {
		if (*s == '\'' || *s == '\\')
			qi[k++] = '\\';
		qi[k++] = *s;
	}
	qi[k] = 0;
	snprintf(q, sizeof q, "'%s' in parents and trashed=false", qi);
	struct buf p = {0}, out = {0};
	b_str(&p, "/drive/v3/files?q=");
	b_enc(&p, q);
	b_str(&p, "&fields=");
	b_enc(&p, "files(id)");
	if (b_str(&p, "&pageSize=1"))
		return E_NOMEM;
	long r = api_get(p.p, 0, &out, 1u << 20);
	b_free(&p);
	if (r >= 0) {
		const char *a = json_find(out.p, "files");
		r = !a || *json_skip_ws(a + 1) != '{';
	}
	b_free(&out);
	return r;
}

/* à la corbeille */
static long drive_trash(const char *id)
{
	return drive_patch(id, 0, "{\"trashed\":true}");
}

/* le contenu du tampon d'envoi envoyé à Drive (session « resumable ») -> 0, ou une erreur */
static long upload(struct ouvert *o)
{
	struct buf p = {0}, out = {0};
	struct hres h;
	char extra[300];
	snprintf(extra, sizeof extra,
		 "Content-Type: application/json; charset=UTF-8\r\nX-Upload-Content-Type: %s\r\n"
		 "X-Upload-Content-Length: %llu\r\n",
		 mime_of(o->name), (unsigned long long)o->size);
	b_str(&p, "/upload/drive/v3/files/");
	b_enc(&p, o->id);
	if (b_str(&p, "?uploadType=resumable&fields=id"))
		return E_NOMEM;
	long s = api_call("PATCH", p.p, 0, extra, "{}", 2, &out, 1u << 20, &h);
	b_free(&p);
	if (s < 0) {
		b_free(&out);
		return s;
	}
	struct url lu;
	if (!h.loc[0] || url_parse(h.loc, &lu) || strcasecmp(lu.host, api.host) || lu.port != api.port ||
	    lu.tls != api.tls) {
		say("%s : pas de session d'envoi (Location « %.80s »)", o->name, h.loc);
		b_free(&out);
		return E_IO;
	}
	u8 *chunk = o->size ? malloc(UPLOAD_CHUNK) : 0;
	if (o->size && !chunk) {
		b_free(&out);
		return E_NOMEM;
	}
	u64 off = 0;
	int tries = 0;
	long r = E_IO;
	for (;;) {
		u64 n = o->size - off > UPLOAD_CHUNK ? UPLOAD_CHUNK : o->size - off;
		if (n) {
			ssize_t k = pread(o->fd, chunk, (size_t)n, (off_t)off);
			if (k != (ssize_t)n) {
				say("%s : tampon d'envoi illisible", o->name);
				break;
			}
			snprintf(extra, sizeof extra, "Content-Range: bytes %llu-%llu/%llu\r\n", (unsigned long long)off,
				 (unsigned long long)(off + n - 1), (unsigned long long)o->size);
		} else {
			snprintf(extra, sizeof extra, "Content-Range: bytes */%llu\r\n", (unsigned long long)o->size);
		}
		s = api_call("PUT", lu.path, 1, extra, chunk, (size_t)n, &out, 1u << 20, &h);
		if (s < 0 && s != E_NOENT && s != E_ACCES && s != E_NOSPC && tries < BACKOFF_MAX) {
			/* (coupé : où en est le serveur ?) */
			snprintf(extra, sizeof extra, "Content-Range: bytes */%llu\r\n", (unsigned long long)o->size);
			s = api_call("PUT", lu.path, 1, extra, 0, 0, &out, 1u << 20, &h);
			tries++;
		}
		if (s == 200 || s == 201) {
			r = 0;
			break;
		}
		if (s == 308) {
			u64 got = h.rng >= 0 ? (u64)h.rng + 1 : 0;
			if (got <= off && n && ++tries > BACKOFF_MAX)
				break;
			off = got;
			continue;
		}
		r = s < 0 ? s : E_IO;
		break;
	}
	free(chunk);
	b_free(&out);
	if (r < 0)
		say("%s : envoi à Drive impossible (%ld)", o->name, r);
	st.uploads += r == 0;
	st.uploaded += r == 0 ? o->size : 0;
	return r;
}

/* un fichier ouvert pour écrire : envoyé s'il a changé -> 0, ou une erreur */
static long wr_flush(struct ouvert *o)
{
	if (!o->wr || !o->dirty)
		return 0;
	long r = upload(o);
	if (!r)
		o->dirty = 0;
	list_drop(o->parent);
	return r;
}

/* les dossiers du tampon d'envoi (« HOME:/gdrive/spool ») */
static void mkdirs(const char *dir)
{
	char p[sizeof spool_dir];
	scpy(p, dir, sizeof p);
	for (char *s = p + 1; *s; s++)
		if (*s == '/' && s[-1] != ':') {
			*s = 0;
			mkdir(p, 0755);
			*s = '/';
		}
	mkdir(p, 0755);
}

/* ouvrir pour écrire (k : la place dans ouverts) -> 0, ou une erreur */
static long open_wr(struct ouvert *o, int k, const char *path, long flags)
{
	if (readonly)
		return E_ACCES;
	struct node par, nd;
	char name[NAME_MAX_];
	long r = locate(path, &par, &nd, name, sizeof name);
	if (r == E_ACCES) /* (la racine) */
		return E_ISDIR;
	if (r < 0)
		return r;
	if (r == 1 && nd.isdir)
		return E_ISDIR;
	if (!r && !(flags & O_K_CREATE))
		return E_NOENT;
	memset(o, 0, sizeof *o);
	o->fd = -1;
	if (!r) {
		if ((r = drive_create(par.id, name, mime_of(name), nd.id, sizeof nd.id)) < 0)
			return r;
		nd.size = 0;
	}
	scpy(o->id, nd.id, sizeof o->id);
	scpy(o->ver, nd.ver, sizeof o->ver);
	scpy(o->parent, par.id, sizeof o->parent);
	scpy(o->name, name, sizeof o->name);
	mkdirs(spool_dir);
	snprintf(o->spool, sizeof o->spool, "%s/%s-%d.tmp", spool_dir, volume, k);
	o->fd = open(o->spool, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (o->fd < 0) {
		say("tampon d'envoi %s : impossible de le créer", o->spool);
		return E_IO;
	}
	o->next = NONE;
	o->ra = 1;
	if ((flags & O_K_TRUNC) && nd.size) {
		o->dirty = 1;
	} else if (nd.size) { /* (le contenu actuel, copié par le cache) */
		u8 *b = malloc(BLOCK);
		r = b ? 0 : E_NOMEM;
		o->size = nd.size;
		for (u64 pos = 0; !r && pos < nd.size;) {
			long g = read_at(o, b, BLOCK, (i64)pos);
			if (g <= 0 || pwrite(o->fd, b, (size_t)g, (off_t)pos) != g)
				r = g < 0 ? g : E_IO;
			pos += g > 0 ? (u64)g : 0;
		}
		free(b);
		if (r < 0) {
			close(o->fd);
			unlink(o->spool);
			memset(o, 0, sizeof *o);
			return r;
		}
	}
	o->size = (flags & O_K_TRUNC) ? 0 : nd.size;
	o->append = (flags & O_K_APPEND) != 0;
	o->pos = o->append ? (i64)o->size : 0;
	o->wr = 1;
	return 0;
}

static long write_at(struct ouvert *o, const u8 *src, u64 n, i64 pos)
{
	if (!o->wr)
		return E_BADF;
	if (pos < 0)
		return E_INVAL;
	if (!n)
		return 0;
	ssize_t k = pwrite(o->fd, src, (size_t)n, (off_t)pos);
	if (k < 0)
		return errno == ENOSPC ? E_NOSPC : E_IO;
	o->dirty = 1;
	if ((u64)pos + (u64)k > o->size)
		o->size = (u64)pos + (u64)k;
	return (long)k;
}

/* 3 mkdir(chemin) */
static long op_mkdir(struct msg *m)
{
	if (readonly)
		return E_ACCES;
	m->data[sizeof m->data - 1] = 0;
	struct node par, nd;
	char name[NAME_MAX_], id[ID_MAX];
	long r = locate((char *)m->data, &par, &nd, name, sizeof name);
	if (r < 0)
		return r == E_ACCES ? E_EXIST : r;
	if (r == 1)
		return E_EXIST;
	return drive_create(par.id, name, FOLDER_MIME, id, sizeof id);
}

/* 4 unlink(chemin) : un fichier, ou un dossier vide, à la corbeille */
static long op_unlink(struct msg *m)
{
	if (readonly)
		return E_ACCES;
	m->data[sizeof m->data - 1] = 0;
	struct node par, nd;
	char name[NAME_MAX_];
	long r = locate((char *)m->data, &par, &nd, name, sizeof name);
	if (r < 0)
		return r;
	if (!r)
		return E_NOENT;
	if (nd.isdir) {
		if ((r = drive_empty(nd.id)) < 0)
			return r;
		if (!r)
			return E_NOTEMPTY;
	}
	r = drive_trash(nd.id);
	list_drop(par.id);
	if (nd.isdir)
		list_drop(nd.id);
	return r;
}

/* 6 renommer(ancien, 0, nouveau, 0) : nom et dossier ; un fichier de ce nom est remplacé (à la corbeille) */
static long op_rename(struct msg *m)
{
	if (readonly)
		return E_ACCES;
	m->data[sizeof m->data - 1] = 0;
	const char *a = (char *)m->data;
	const char *b = a + strlen(a) + 1;
	if ((const u8 *)b >= m->data + sizeof m->data)
		return E_INVAL;
	struct node pa, na, pb, nb;
	char name_a[NAME_MAX_], name_b[NAME_MAX_];
	long r = locate(a, &pa, &na, name_a, sizeof name_a);
	if (r <= 0)
		return r ? r : E_NOENT;
	r = locate(b, &pb, &nb, name_b, sizeof name_b);
	if (r < 0)
		return r == E_ACCES ? E_EXIST : r;
	if (r == 1 && strcmp(nb.id, na.id)) {
		if (nb.isdir || na.isdir)
			return E_EXIST;
		if ((r = drive_trash(nb.id)) < 0)
			return r;
	}
	struct buf j = {0}, q = {0};
	b_str(&j, "{\"name\":");
	b_json(&j, name_b);
	b_str(&j, "}");
	if (strcmp(pa.id, pb.id)) {
		b_str(&q, "&addParents=");
		b_enc(&q, pb.id);
		b_str(&q, "&removeParents=");
		b_enc(&q, pa.id);
	}
	r = j.p ? drive_patch(na.id, q.p, j.p) : E_NOMEM;
	b_free(&j);
	b_free(&q);
	list_drop(pa.id);
	list_drop(pb.id);
	return r;
}

/* 5 dater(b secondes UTC ; chemin) */
static long op_utime(struct msg *m)
{
	if (readonly)
		return E_ACCES;
	m->data[sizeof m->data - 1] = 0;
	struct node par, nd;
	char name[NAME_MAX_];
	long r = locate((char *)m->data, &par, &nd, name, sizeof name);
	if (r <= 0)
		return r == E_ACCES ? 0 : r ? r : E_NOENT; /* (la racine : rien à dater) */
	time_t t = (time_t)m->b;
	struct tm tm;
	gmtime_r(&t, &tm);
	char j[96];
	snprintf(j, sizeof j, "{\"modifiedTime\":\"%04d-%02d-%02dT%02d:%02d:%02d.000Z\"}", tm.tm_year + 1900,
		 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	r = drive_patch(nd.id, 0, j);
	list_drop(par.id);
	return r;
}

/* ---- propriétaires et tampons (comme smbfs et src/svc/common/fsowner.c) */

static long proprietaire(struct msg *m)
{
	long pid = m->pid, d = m->d;
	if (!d)
		return pid;
	if (pid == pid_vfs)
		return d;
	if (pid == SVC_PID_KERNEL)
		return pid;
	static u8 pl[64] __attribute__((aligned(8)));
	if (ksys(SYS_PROC_INFO, pid, (long)pl, 0, 0) != 0)
		return pid;
	if (memcmp(pl + 24, "vfs", 4))
		return pid;
	if (ksys(0x1D, pid, 0, 0, 0) != 1) /* (SYS_PROC_RIGHTS : le nom se choisit au lancement, vfs a les droits) */
		return pid;
	pid_vfs = pid;
	return d;
}

/* F4 : une op par chemin (1-6) seulement de vfs (transmise, ou elle-même), du noyau, de l'uid 0 ou d'un process
 * aux droits de /sys (comme fs_path_ok de src/svc/common/fsowner.c) */
#define FSP_REQ_INFO 0x97
#define FSP_RIGHTS 0x1D
#define FSP_UID 0x92
static int fsp_rights(long pid)
{
	return pid > 0 && ksys(FSP_RIGHTS, pid, 0, 0, 0) == 1;
}
static int path_ok(long req, const struct msg *m)
{
	u64 op = (u64)m->op;
	if (!(op >= 1 && op <= 9) && op != 18)
		return 1;
	long pid = m->pid;
	if (!pid || pid == SVC_PID_KERNEL || (pid_vfs && pid == pid_vfs))
		return 1;
	long info[4];
	long r = ksys(FSP_REQ_INFO, req, (long)info, 0, 0);
	if (r == -38)
		return 1; /* (le Pi : pas d'uid) */
	if (r == 0 && info[1])
		return info[1] == pid_vfs || fsp_rights(info[1]);
	if (fsp_rights(pid))
		return 1;
	return r == 0 ? info[2] == 0 : ksys(FSP_UID, pid, 0, 0, 0) == 0;
}

static int own_has(struct ouvert *o, u32 pid)
{
	for (int i = 0; i < OWN_MAX; i++)
		if (o->owners[i] == pid)
			return 1;
	return 0;
}

static int own_add(struct ouvert *o, u32 pid)
{
	if (own_has(o, pid))
		return 1;
	for (int i = 0; i < OWN_MAX; i++)
		if (!o->owners[i]) {
			o->owners[i] = pid;
			return 1;
		}
	return 0;
}

static int own_count(struct ouvert *o)
{
	int c = 0;
	for (int i = 0; i < OWN_MAX; i++)
		c += o->owners[i] != 0;
	return c;
}

/* -> 0, ou l'erreur de l'envoi d'un fichier écrit (le tampon d'envoi est jeté quand même) */
static long ouvert_free(struct ouvert *o)
{
	long r = 0;
	if (o->wr) {
		r = wr_flush(o);
		close(o->fd);
		unlink(o->spool);
	}
	for (int i = 0; i < o->nents; i++)
		free(o->ents[i].name);
	free(o->ents);
	memset(o, 0, sizeof *o);
	return r;
}

/* le fichier ouvert « a » si l'appelant y a droit, sinon 0 (*err) */
static struct ouvert *ouvert_de(struct msg *m, long *err)
{
	u64 num = (u64)m->a;
	if (num >= OUV_MAX || !ouverts[num].used) {
		*err = E_INVAL;
		return 0;
	}
	struct ouvert *o = &ouverts[num];
	u32 pid = m->pid;
	if (pid && pid != SVC_PID_KERNEL && !own_has(o, pid)) {
		*err = E_BADF;
		return 0;
	}
	return o;
}

static int tampon_entree(long pid)
{
	for (int i = 0; i < TP_MAX; i++)
		if (tampons[i].pid == pid)
			return i;
	return -1;
}

static u8 *tampon_de(struct msg *m)
{
	int i = m->pid ? tampon_entree(m->pid) : -1;
	return i < 0 ? 0 : (u8 *)(uintptr_t)(BUF_VA_BASE + (u64)tampons[i].num * BUF_SLOT);
}

static long op_tampon(struct msg *m)
{
	long pid = m->pid;
	if (!pid || pid == SVC_PID_KERNEL)
		return E_INVAL;
	int i = tampon_entree(pid);
	if (i < 0) {
		i = tampon_entree(0);
		if (i < 0)
			return E_NOMEM;
		long num = ksys(SYS_BUF_CREATE, FS_BUFFER_SIZE, 0, 0, 0);
		if (num < 0)
			return num;
		tampons[i].pid = pid;
		tampons[i].num = num;
		ksys(SYS_BUF_GRANT, num, pid, 0, 0);
	}
	m->a = tampons[i].num;
	return 0;
}

static void fin_pid(long pid)
{
	if (pid == pid_vfs)
		pid_vfs = 0;
	if (!pid)
		return;
	for (int k = 0; k < OUV_MAX; k++) {
		struct ouvert *o = &ouverts[k];
		if (!o->used)
			continue;
		for (int i = 0; i < OWN_MAX; i++)
			if (o->owners[i] == (u32)pid)
				o->owners[i] = 0;
		if (!own_count(o))
			ouvert_free(o);
	}
	int i = tampon_entree(pid);
	if (i >= 0) {
		ksys(SYS_BUF_FREE, tampons[i].num, 0, 0, 0);
		tampons[i].pid = tampons[i].num = 0;
	}
}

static void fork_pid(long child, long parent)
{
	if (!child || !parent)
		return;
	for (int k = 0; k < OUV_MAX; k++)
		if (ouverts[k].used && own_has(&ouverts[k], (u32)parent))
			own_add(&ouverts[k], (u32)child);
}

/* ---- opérations */

/* 1 ouvrir(a drapeaux ; chemin) -> a canal, b numéro, c attributs, d taille */
static long op_open(struct msg *m)
{
	long extras = m->c & (ATTR_SYS | ATTR_LIB);
	long flags = m->a;
	m->data[sizeof m->data - 1] = 0;
	int k = -1;
	for (int i = 0; i < OUV_MAX && k < 0; i++)
		if (!ouverts[i].used)
			k = i;
	if (k < 0)
		return E_MFILE;
	struct node nd;
	struct ouvert *o = &ouverts[k];
	long r;
	if (flags & (O_K_WRITE | O_K_CREATE | O_K_TRUNC | O_K_APPEND)) {
		if ((r = open_wr(o, k, (char *)m->data, flags)) < 0)
			return r;
		goto opened;
	}
	if ((r = resolve((char *)m->data, &nd)) < 0)
		return r;
	memset(o, 0, sizeof *o);
	if (nd.isdir) {
		struct dirlist *d;
		if ((r = dir_get(nd.id, &d)) < 0)
			return r;
		o->ents = calloc((size_t)(d->n ? d->n : 1), sizeof *o->ents);
		if (!o->ents)
			return E_NOMEM;
		for (int i = 0; i < d->n; i++) {
			struct dent *x = &o->ents[o->nents];
			x->name = strdup(d->ents[i].name);
			x->size = d->ents[i].size;
			x->attr = d->ents[i].isdir ? ATTR_DIR : ATTR_FILE;
			if (x->name)
				o->nents++;
		}
		o->isdir = 1;
	} else {
		o->size = nd.size;
		scpy(o->id, nd.id, sizeof o->id);
		scpy(o->ver, nd.ver, sizeof o->ver);
		o->next = NONE;
		o->ra = 1;
	}
opened:
	o->used = 1;
	own_add(o, (u32)proprietaire(m));
	m->a = (u32)canal;
	m->b = k;
	m->c = (o->isdir ? ATTR_DIR : ATTR_FILE) | extras;
	m->d = o->isdir ? 0 : (i64)o->size;
	return 0;
}

/* 2 stat(chemin) -> b taille, c attributs, d date */
static long op_stat(struct msg *m)
{
	m->data[sizeof m->data - 1] = 0;
	struct node nd;
	long r = resolve((char *)m->data, &nd);
	if (r < 0)
		return r;
	m->b = nd.isdir ? 0 : (i64)nd.size;
	m->c = nd.isdir ? ATTR_DIR : ATTR_FILE;
	m->d = nd.mtime;
	return 0;
}

static i64 pos_of(struct msg *m, struct ouvert *o)
{
	return m->b == FS_POS_CUR ? o->pos : m->b;
}

static void pos_after(struct msg *m, struct ouvert *o, long done, i64 pos)
{
	if (done < 0)
		return;
	pos += done;
	m->d = pos;
	if (m->b == FS_POS_CUR)
		o->pos = pos;
}

static struct ouvert *fichier_de(struct msg *m, long *err)
{
	struct ouvert *o = ouvert_de(m, err);
	if (o && o->isdir) {
		*err = E_INVAL;
		return 0;
	}
	return o;
}

/* 10 lire(a, b position, c octets) -> octets (données), d position après */
static long op_read(struct msg *m)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	i64 pos = pos_of(m, o);
	u64 n = (u64)m->c > FS_DATA_MAX ? FS_DATA_MAX : (u64)m->c;
	long r = read_at(o, m->data, n, pos);
	m->len = r > 0 ? (u32)r : 0;
	pos_after(m, o, r, pos);
	return r;
}

/* 15 : au début du tampon partagé de l'appelant */
static long op_read_buf(struct msg *m)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	u8 *buf = tampon_de(m);
	if (!buf || (u64)m->c > FS_BUFFER_SIZE)
		return E_INVAL;
	i64 pos = pos_of(m, o);
	long r = read_at(o, buf, (u64)m->c, pos);
	pos_after(m, o, r, pos);
	return r;
}

/* 17 lire_d(a, b position, c octets, d adresse) : dans la mémoire de l'appelant */
static long op_read_direct(struct msg *m)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	if ((u64)m->c > FS_DIRECT_MAX)
		return E_INVAL;
	long va = ksys(SYS_SVC_MAP_CALLER, req_cour, m->d, m->c, MC_WRITE);
	if (va < 0)
		return va;
	i64 pos = pos_of(m, o);
	long r = read_at(o, (u8 *)va, (u64)m->c, pos); /* (démappé par SVC_REPLY) */
	pos_after(m, o, r, pos);
	return r;
}

/* 11 écrire(a, b position, c octets, données) -> octets, b taille, d position après */
static long op_write(struct msg *m)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	if ((u64)m->c > FS_DATA_MAX)
		return E_INVAL;
	i64 pos = o->append ? (i64)o->size : pos_of(m, o);
	long r = write_at(o, m->data, (u64)m->c, pos);
	if (r < 0)
		return r;
	pos_after(m, o, r, pos);
	m->b = (i64)o->size;
	return r;
}

/* 16 : depuis le début du tampon partagé de l'appelant -> octets, b taille */
static long op_write_buf(struct msg *m)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	u8 *buf = tampon_de(m);
	if (!buf || (u64)m->c > FS_BUFFER_SIZE)
		return E_INVAL;
	i64 pos = o->append ? (i64)o->size : pos_of(m, o);
	long r = write_at(o, buf, (u64)m->c, pos);
	if (r < 0)
		return r;
	pos_after(m, o, r, pos);
	m->b = (i64)o->size;
	return r;
}

/* 25 sync : les fichiers écrits envoyés à Drive -> a fichiers ouverts */
static long op_sync(struct msg *m)
{
	long r = 0, n = 0;
	for (int k = 0; k < OUV_MAX; k++) {
		if (!ouverts[k].used)
			continue;
		n++;
		long e = wr_flush(&ouverts[k]);
		if (e < 0 && !r)
			r = e;
	}
	m->a = n;
	return r;
}

/* 26 position(a, b décalage, c origine) -> position ; un saut loin remet l'avance à un bloc */
static long op_seek(struct msg *m)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	if ((u64)m->c > 2)
		return E_INVAL;
	i64 base = m->c == 1 ? o->pos : m->c == 2 ? (i64)o->size : 0;
	i64 p = base + m->b;
	if (p < 0)
		return E_INVAL;
	if (o->next != NONE && ((u64)p + BLOCK <= o->next || (u64)p >= o->next + BLOCK)) {
		o->ra = 1;
		o->next = NONE;
	}
	o->pos = p;
	m->b = p;
	return p;
}

/* 13 lire_rep(a, b rang) -> 1 (nom[256], taille, attributs) ou 0 */
static long op_readdir(struct msg *m)
{
	long err;
	struct ouvert *o = ouvert_de(m, &err);
	if (!o)
		return err;
	if (!o->isdir)
		return E_INVAL;
	u64 rang = (u64)m->b;
	if (rang >= (u64)o->nents)
		return 0;
	struct dent *e = &o->ents[rang];
	memset(m->data, 0, 272);
	snprintf((char *)m->data, 256, "%s", e->name);
	u64 *t = (u64 *)(m->data + 256);
	t[0] = e->size;
	t[1] = e->attr;
	m->len = 272;
	return 1;
}

/* 12 fermer(a) */
static long op_close(struct msg *m)
{
	u64 num = (u64)m->a;
	if (num >= OUV_MAX || !ouverts[num].used)
		return E_INVAL;
	struct ouvert *o = &ouverts[num];
	u32 pid = m->pid;
	if (!pid || pid == SVC_PID_KERNEL)
		return ouvert_free(o);
	if (!own_has(o, pid))
		return E_BADF;
	for (int i = 0; i < OWN_MAX; i++)
		if (o->owners[i] == pid)
			o->owners[i] = 0;
	return own_count(o) ? 0 : ouvert_free(o);
}

/* 18 cache(a : 0 lire, FS_CC_*) -> a succès, b défauts, c blocs de 64 Ko en mémoire, d remplacés */
static long op_cache(struct msg *m)
{
	if (m->a == FS_CC_DROP || m->a == FS_CC_OFF)
		cache_drop();
	m->a = (i64)st.hits;
	m->b = (i64)st.misses;
	m->c = (i64)blk_count() * (BLOCK / 65536);
	m->d = (i64)st.evictions;
	return 0;
}

/* 19 info -> a octets, b libres (-1 : sans limite), c octets par bloc, d FS_T_NET ; données : étiquette */
static struct {
	long at;
	u64 total;
	i64 free_;
	char label[120];
} info;

static long op_info(struct msg *m)
{
	if (!info.at || now_ms() - info.at > 60000) {
		struct buf body = {0};
		long r = api_get("/drive/v3/about?fields=storageQuota,user", 0, &body, 1u << 20);
		if (r == 200) {
			const char *q = json_find(body.p, "storageQuota");
			u64 limit = json_u64(q, "limit", NONE), usage = json_u64(q, "usage", 0);
			char *mail = json_str(json_find(body.p, "user"), "emailAddress");
			info.total = limit != NONE ? limit : usage;
			info.free_ = limit != NONE ? (i64)(limit > usage ? limit - usage : 0) : -1;
			snprintf(info.label, sizeof info.label, "Google Drive (%s)", mail ? mail : drive);
			free(mail);
			info.at = now_ms();
		} else if (!info.at) {
			info.total = 0;
			info.free_ = -1;
			snprintf(info.label, sizeof info.label, "Google Drive (%s)", drive);
		}
		b_free(&body);
	}
	m->a = (i64)info.total;
	m->b = info.free_;
	m->c = 4096;
	m->d = FS_T_NET;
	int n = snprintf((char *)m->data, 120, "%s", info.label);
	m->len = (u32)(n + 1);
	return 0;
}

/* une requête -> le résultat (msg.op) */
static long handle(struct msg *m)
{
	u64 op = (u64)m->op;
	switch (op) {
	case FS_CLOSE: return op_close(m);
	case FS_BUFFER: return op_tampon(m);
	case FS_SYNC: return op_sync(m);
	case FS_WRITE: return op_write(m);
	case FS_WRITE_BUF: return op_write_buf(m);
	case FS_READ: return op_read(m);
	case FS_READ_BUF: return op_read_buf(m);
	case FS_READ_DIRECT: return op_read_direct(m);
	case FS_SEEK: return op_seek(m);
	case FS_READDIR: return op_readdir(m);
	case FS_CACHE: return op_cache(m);
	}
	long r = load_conf();
	if (r < 0)
		return r;
	switch (op) {
	case FS_OPEN: return op_open(m);
	case FS_STAT: return op_stat(m);
	case FS_INFO: return op_info(m);
	case FS_MKDIR: return op_mkdir(m);
	case FS_UNLINK: return op_unlink(m);
	case FS_UTIME: return op_utime(m);
	case FS_RENAME: return op_rename(m);
	default: return E_INVAL;
	}
}

int main(int argc, char **argv)
{
	/* « <volume> <source> » (argv[0] est peut-être le programme) */
	int s = -1;
	for (int i = 0; i < argc; i++)
		if (!strncasecmp(argv[i], "gdrive:", 7))
			s = i;
	if (s < 1) {
		fprintf(stderr, "gdrivefs : lancé par vfs (mount /net/net1 gdrive:NOM gdrivefs), docs/etude/GDRIVE.md\n");
		return 1;
	}
	scpy(volume, argv[s - 1], sizeof volume);
	scpy(source, argv[s], sizeof source);
	char name[48];
	snprintf(name, sizeof name, "fs.%s", volume);
	for (char *p = name; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p += 32;
	canal = ksys(SYS_SVC_REGISTER, (long)name, SVF_ENDS | SVF_BIRTHS, 0, 0);
	if (canal < 0) {
		say("enregistrement de %s impossible (%ld)", name, canal);
		return 1;
	}
	for (;;) {
		long req = ksys(SYS_SVC_NEXT, canal, (long)&msg, 60000, 0);
		if (req < 0)
			return 1;
		if (!req) { /* (au repos : une connexion inutilisée depuis longtemps est fermée) */
			if (cn.fd >= 0 && now_ms() - cn.last > KEEP_IDLE)
				conn_close();
			continue;
		}
		req_cour = req;
		u64 op = (u64)msg.op;
		if (op == SVC_OP_END) {
			fin_pid(msg.a);
			continue;
		}
		if (op == SVC_OP_FORK) {
			fork_pid(msg.a, msg.b);
			continue;
		}
		if (op == SVC_OP_BIRTH)
			continue;
		if (!path_ok(req, &msg)) {
			msg.op = -13; /* (E_ACCES) */
			ksys(SYS_SVC_REPLY, req, (long)&msg, 0, 0);
			continue;
		}
		long r = handle(&msg);
		if (op == FS_CLOSE && !msg.pid) /* (avis du noyau : sans réponse) */
			continue;
		msg.op = r;
		ksys(SYS_SVC_REPLY, req, (long)&msg, 0, 0);
	}
}
