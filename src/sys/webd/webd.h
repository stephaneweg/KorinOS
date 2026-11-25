/*
 * webd.h -- le serveur web de Korin (docs/WEB.md) : les structures partagées par les sources.
 *
 * Un seul programme : le service (les sites de /etc/web/sites.ini), l'instance privée de WebStudio (--dev), et
 * les commandes qui le pilotent (stop, reload, status, check, site, cert). C sur libposix, TLS par OpenSSL.
 */
#ifndef WEBD_H
#define WEBD_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <openssl/ssl.h>

#define WEBD_VERSION	"0.1.0"
#define WEBD_SERVER	"Korin-webd/" WEBD_VERSION
#define CONF_DIR	"/etc/web"
#define CONF_FILE	CONF_DIR "/sites.ini"
#define CERT_DIR	CONF_DIR "/certs"
#define PID_FILE	"/var/run/webd.pid"	/* (à l'uid 0 : dans /tmp, n'importe qui y mettait un pid que « webd stop »
					 * élevé tuait, S9) */
#define LOG_FILE	"/var/log/webd.log"
#define LOG_MAX		(2 * 1024 * 1024)	/* au-delà : webd.log devient webd.log.old */

#define MAX_SITES	64
#define MAX_BINDS	16
#define MAX_INDEX	4
#define MAX_LISTEN	32
#define MAX_CONNS	128			/* connexions servies en même temps (un thread chacune) */

enum { SCHEME_HTTP, SCHEME_HTTPS };

struct bind {
	int scheme;
	int port;
	char host[128];				/* en minuscules ; « * » : tout nom */
};

struct site {
	char name[64];
	char root[512];				/* sans « / » final */
	char index[MAX_INDEX][64];
	int nindex;
	struct bind binds[MAX_BINDS];
	int nbinds;
	int enabled;
	int listing;				/* liste d'un dossier sans page d'index */
	int dev;				/* instance de WebStudio : rechargement, pas de cache */
};

/* un certificat chargé (SNI) : le nom d'hôte, son contexte */
struct cert {
	char host[128];
	SSL_CTX *ctx;
};

struct conf {
	struct site sites[MAX_SITES];
	int nsites;
	struct cert certs[MAX_BINDS * 4];
	int ncerts;
	int refs;				/* connexions qui s'en servent encore (+1 tant qu'elle est en service) */
};

struct listener {
	int port;
	int scheme;
	int fd;
	SSL_CTX *ctx;				/* https : le contexte par défaut (puis SNI) */
	struct conf *conf;
	pthread_t th;
	volatile int stop;
};

/* ---- conf.c ---- */
int conf_load (const char *file, struct conf *c, int quiet);
int conf_save (const char *file, const struct conf *c);
int bind_parse (const char *s, struct bind *b);
void bind_format (const struct bind *b, char *out, size_t n);
struct site *conf_find (struct conf *c, const char *name);

/* ---- server.c ---- */
extern volatile int g_quit;
extern volatile unsigned g_reload_gen;	/* --dev : chaque enregistrement de WebStudio l'augmente */
extern pthread_mutex_t g_reload_mu;
extern pthread_cond_t g_reload_cv;
extern int g_dev;
extern int g_dev_lan; /* --dev --lan : l'aperçu ouvert au réseau local */
void reload_pages (void);
int listeners_start (struct conf *c, struct listener *ls, int *nls, int dev_port_auto);
void listeners_stop (struct listener *ls, int nls);
void conf_unref (struct conf *c);
void conf_ref (struct conf *c);

/* ---- tls.c ---- */
int webtls_init (void);
int webtls_conf (struct conf *c, struct listener *ls, int nls);
void webtls_free (struct conf *c);
int cert_command (int argc, char **argv);

/* ---- util.c ---- */
const char *mime_type (const char *path);
void http_date (time_t t, char *out, size_t n);
int url_decode_path (const char *in, char *out, size_t n);
void html_escape (const char *in, char *out, size_t n);
void log_open (int to_stdout);
void log_request (const char *site, const char *method, const char *path, int status, long long bytes, long ms,
	const char *note);
void log_msg (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
long now_ms (void);
int mkdirs (const char *path);
int file_exists (const char *path);

#endif
