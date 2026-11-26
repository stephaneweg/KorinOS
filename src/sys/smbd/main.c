/* smbd - le serveur de fichiers SMB de Korin (docs/SMB.md) : des dossiers de Korin partagés vers
 * Windows (« \\korin\Korin »), macOS et Linux, en SMB 2.0.2 à 3.1.1, NTLMv2 et signature.
 *
 *   smbd [-c FICHIER] [-p PORT] [-v]
 *
 * Le protocole (négociation, session NTLMv2 dans SPNEGO, signature, chiffrement de SMB3) est le
 * moteur de serveur de libsmb2 ; smbd tient la configuration, les comptes, les partages (ici) et les
 * fichiers (fs.c). Configuration : /etc/smbd.ini (/etc/smbd.ini sous Korin) :
 *
 *   [smbd]
 *   port = 445
 *   name = KORIN               nom du serveur
 *   workgroup = WORKGROUP
 *   [users]
 *   stephane = motdepasse      un compte : nom = mot de passe (celui que Windows demande)
 *                              (avec le coffre à secrets, le PC : « stephane = », le mot de passe est le
 *                              secret « smbd/stephane » de l'espace @system ; shares.h)
 *   [shares]
 *   Korin = HOME:/          un partage : nom = dossier [, readonly] (ancienne forme, gardée)
 *
 * Les partages se règlent plutôt dans /etc/shares.ini (shares.h), que tient le volet « Partage de
 * fichiers » de Réglages (avec le compte de [users]) : smbd y prend ceux de type smb. Les deux
 * fichiers sont relus quand ils changent (au plus une fois par seconde) : pas besoin de relancer.
 *
 * Lancé par init2 (/etc/services.txt) ou à la main ; il écrit au journal. Un seul thread : la
 * boucle de libsmb2 sert toutes les connexions (select). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#include "smbd.h"
#include "shares.h"
#include "../../lib/keyvault/keyvault.h"

struct config conf;
static struct tree trees[SMBD_MAX_TREES];
static uint32_t next_tid = 1;
static const char *conf_path = SMBD_FILE;
static const char *shares_path = SHARES_FILE;
static time_t seen_conf, seen_shares, last_check;

/* ---- journal */

void log_msg(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("smbd : ", stdout);
	vprintf(fmt, ap);
	fputc('\n', stdout);
	fflush(stdout);
	va_end(ap);
}

void log_dbg(const char *fmt, ...)
{
	if (!conf.verbose)
		return;
	va_list ap;
	va_start(ap, fmt);
	fputs("smbd : ", stdout);
	vprintf(fmt, ap);
	fputc('\n', stdout);
	fflush(stdout);
	va_end(ap);
}

/* ---- configuration */

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
	return s;
}

static void copy(char *dst, const char *src, size_t n)
{
	snprintf(dst, n, "%s", src);
}

/* smbd.ini réécrit avec « compte = » (sans mot de passe) dans [users] -> 0 ou -1 */
static int conf_strip_passwords(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	size_t cap = 65536, n = 0;
	char *out = malloc(cap), line[2048], t[2048], section[64] = "";
	int r = out ? 0 : -1;
	while (!r && fgets(line, sizeof line, f)) {
		snprintf(t, sizeof t, "%s", line);
		char *s = trim(t), *eq;
		if (*s == '[') {
			char *e = strchr(s, ']');
			if (e)
				*e = 0;
			copy(section, trim(s + 1), sizeof section);
		} else if (*s && *s != '#' && *s != ';' && !strcasecmp(section, "users") && (eq = strchr(s, '='))) {
			*eq = 0;
			snprintf(line, sizeof line, "%s =\n", trim(s));
		}
		size_t k = strlen(line);
		if (n + k >= cap)
			r = -1; /* (trop grand : laissé tel quel) */
		else {
			memcpy(out + n, line, k);
			n += k;
		}
	}
	fclose(f);
	memset(line, 0, sizeof line);
	memset(t, 0, sizeof t);
	if (!r) {
		f = fopen(path, "w");
		r = f && fwrite(out, 1, n, f) == n ? 0 : -1;
		if (f && fclose(f))
			r = -1;
	}
	if (out)
		memset(out, 0, n);
	free(out);
	return r;
}

/* les comptes lus depuis users0 et le coffre à secrets (shares.h) : un mot de passe encore dans smbd.ini y passe
 * (puis smbd.ini est réécrit sans eux), un vide en vient. Coffre présent mais muet, ou pas de tel secret : le
 * compte sans mot de passe est retiré (jamais de session sans mot de passe) ; sans coffre : comme avant */
static void conf_vault(const char *path, int users0)
{
	if (!smbd_vault_present())
		return;
	int ok = smbd_vault_ok(), plain = 0, failed = 0, j = users0;
	for (int i = users0; i < conf.nusers; i++) {
		struct user *u = &conf.users[i];
		char name[160];
		smbd_vault_name(name, sizeof name, u->name);
		if (u->password[0]) {
			if (ok && kv_sys_set(name, u->password, (long)strlen(u->password)) >= 0)
				plain = 1;
			else
				failed = 1;
		} else {
			long r = ok ? kv_sys_get(name, u->password, (long)sizeof u->password - 1) : -1;
			if (r <= 0) {
				log_msg("compte « %s » sans mot de passe (coffre à secrets : %ld) : ignoré", u->name, r);
				memset(u->password, 0, sizeof u->password);
				continue;
			}
			u->password[r] = 0;
		}
		if (j != i)
			conf.users[j] = *u;
		j++;
	}
	for (int i = j; i < conf.nusers; i++)
		memset(&conf.users[i], 0, sizeof conf.users[i]);
	conf.nusers = j;
	if (plain && !failed && !conf_strip_passwords(path))
		log_msg("mots de passe des comptes rangés dans le coffre à secrets");
}

int conf_load(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	int users0 = conf.nusers;
	char line[2048], section[64] = "";
	while (fgets(line, sizeof line, f)) {
		char *s = trim(line);
		if (!*s || *s == '#' || *s == ';')
			continue;
		if (*s == '[') {
			char *e = strchr(s, ']');
			if (e)
				*e = 0;
			copy(section, trim(s + 1), sizeof section);
			continue;
		}
		char *eq = strchr(s, '=');
		if (!eq)
			continue;
		*eq = 0;
		char *k = trim(s), *v = trim(eq + 1);
		if (!strcasecmp(section, "smbd")) {
			if (!strcasecmp(k, "port"))
				conf.port = atoi(v);
			else if (!strcasecmp(k, "name"))
				copy(conf.name, v, sizeof conf.name);
			else if (!strcasecmp(k, "workgroup"))
				copy(conf.workgroup, v, sizeof conf.workgroup);
			else if (!strcasecmp(k, "label"))
				copy(conf.label, v, sizeof conf.label);
			else if (!strcasecmp(k, "verbose"))
				conf.verbose = atoi(v);
		} else if (!strcasecmp(section, "users")) {
			if (conf.nusers < SMBD_MAX_USERS && *k) {
				struct user *u = &conf.users[conf.nusers++];
				copy(u->name, k, sizeof u->name);
				copy(u->password, v, sizeof u->password);
			}
		} else if (!strcasecmp(section, "shares")) {
			if (conf.nshares < SMBD_MAX_SHARES && *k && *v) {
				struct share *sh = &conf.shares[conf.nshares++];
				copy(sh->name, k, sizeof sh->name);
				char *c = strchr(v, ',');
				if (c) {
					*c = 0;
					sh->readonly = strstr(c + 1, "readonly") != 0;
				}
				copy(sh->path, trim(v), sizeof sh->path);
				size_t n = strlen(sh->path); /* (sans « / » final, sauf « / » et « /media/sd1/ ») */
				while (n > 1 && sh->path[n - 1] == '/' && sh->path[n - 2] != ':')
					sh->path[--n] = 0;
			}
		}
	}
	fclose(f);
	memset(line, 0, sizeof line);
	conf_vault(path, users0);
	return 0;
}

static time_t mtime_of(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 ? st.st_mtime : 0;
}

/* smbd.ini puis shares.ini (type smb) ; le port et le nom de la ligne de commande gardés */
static int cli_port, cli_verbose;

static void conf_reload(void)
{
	conf.nshares = conf.nusers = 0;
	conf.port = 445;
	conf.verbose = 0;
	copy(conf.name, "KORIN", sizeof conf.name);
	copy(conf.workgroup, "WORKGROUP", sizeof conf.workgroup);
	copy(conf.label, "Korin", sizeof conf.label);
	conf_load(conf_path);
	static struct share_ent se[SHARES_MAX];
	int n = shares_load(shares_path, se, SHARES_MAX);
	for (int i = 0; i < n && conf.nshares < SMBD_MAX_SHARES; i++) {
		if (se[i].type != SHARE_SMB || conf_share(se[i].name))
			continue;
		struct share *sh = &conf.shares[conf.nshares++];
		copy(sh->name, se[i].name, sizeof sh->name);
		copy(sh->path, se[i].path, sizeof sh->path);
		sh->readonly = se[i].readonly;
	}
	if (cli_port)
		conf.port = cli_port;
	conf.verbose |= cli_verbose;
	seen_conf = mtime_of(conf_path);
	seen_shares = mtime_of(shares_path);
}

void conf_check(void)
{
	time_t now = time(NULL);
	if (now == last_check)
		return;
	last_check = now;
	if (mtime_of(conf_path) == seen_conf && mtime_of(shares_path) == seen_shares)
		return;
	conf_reload();
	log_msg("configuration relue : %d partage%s, %d compte%s", conf.nshares, conf.nshares > 1 ? "s" : "",
		conf.nusers, conf.nusers > 1 ? "s" : "");
}

struct share *conf_share(const char *name)
{
	for (int i = 0; i < conf.nshares; i++)
		if (!strcasecmp(conf.shares[i].name, name))
			return &conf.shares[i];
	return 0;
}

struct user *conf_user(const char *name)
{
	for (int i = 0; i < conf.nusers; i++)
		if (!strcasecmp(conf.users[i].name, name))
			return &conf.users[i];
	return 0;
}

/* ---- arbres */

static struct tree *tree_find(struct smb2_context *smb2, uint32_t tid)
{
	for (int i = 0; i < SMBD_MAX_TREES; i++)
		if (trees[i].ctx == smb2 && trees[i].tid == tid) {
			struct tree *t = &trees[i];
			conf_check();
			t->share = t->ipc ? 0 : conf_share(t->name);
			return t->ipc || t->share ? t : 0; /* (partage retiré depuis : plus d'accès) */
		}
	return 0;
}

struct tree *tree_of(struct smb2_context *smb2)
{
	uint32_t tid;
	if (smb2_get_tree_id_for_pdu(smb2, NULL, &tid) < 0)
		return 0;
	return tree_find(smb2, tid);
}

struct share *tree_share(struct smb2_context *smb2, uint32_t tid)
{
	struct tree *t = tree_find(smb2, tid);
	return t ? t->share : 0;
}

/* (libsmb2 relit la requête après le gestionnaire : la réponse d'erreur est faite par lui, avec ce
 * code, quand le gestionnaire rend -1 ; tools/libsmb2/korin.patch) */
int reply_error(struct smb2_context *smb2, int cmd, uint32_t status)
{
	(void)smb2;
	(void)cmd;
	smb2_set_server_status(status);
	return -1;
}

/* ---- gestionnaires de session */

static int on_authorize(struct smb2_server *s, struct smb2_context *smb2, const char *user, const char *domain,
			const char *workstation)
{
	(void)s;
	conf_check();
	struct user *u = user ? conf_user(user) : 0;
	if (!u) {
		log_msg("session refusée : compte inconnu « %s » (%s, poste %s)", user ? user : "",
			domain ? domain : "", workstation ? workstation : "");
		return -1;
	}
	smb2_set_user(smb2, user);
	smb2_set_password(smb2, u->password);
	if (domain)
		smb2_set_domain(smb2, domain);
	log_dbg("authentification de « %s » (domaine %s, poste %s)", user, domain ? domain : "",
		workstation ? workstation : "");
	return 0;
}

static int on_session(struct smb2_server *s, struct smb2_context *smb2)
{
	(void)s;
	log_msg("session ouverte (dialecte %04x)", smb2_get_dialect(smb2));
	return 0;
}

static int on_logoff(struct smb2_server *s, struct smb2_context *smb2)
{
	(void)s;
	log_dbg("fin de session");
	fs_close_all(smb2);
	return 0;
}

static int on_tree_connect(struct smb2_server *s, struct smb2_context *smb2, struct smb2_tree_connect_request *req,
			   struct smb2_tree_connect_reply *rep)
{
	(void)s;
	/* « \\serveur\partage » en UTF-16 -> le nom du partage */
	char *unc = req->path ? (char *)smb2_utf16_to_utf8(req->path, req->path_length / 2) : 0;
	const char *name = unc ? unc : "";
	const char *b = strrchr(name, '\\');
	if (b)
		name = b + 1;
	struct share *sh = 0;
	int ipc = !strcasecmp(name, "IPC$");
	conf_check();
	if (!ipc && !(sh = conf_share(name))) {
		log_msg("partage inconnu « %s »", name);
		free(unc);
		return reply_error(smb2, SMB2_TREE_CONNECT, ST_BAD_NETWORK_NAME);
	}
	struct tree *t = 0;
	for (int i = 0; i < SMBD_MAX_TREES && !t; i++)
		if (!trees[i].ctx)
			t = &trees[i];
	if (!t) {
		free(unc);
		return reply_error(smb2, SMB2_TREE_CONNECT, ST_ACCESS_DENIED);
	}
	t->ctx = smb2;
	t->tid = next_tid++;
	if (!next_tid)
		next_tid = 1;
	t->share = sh;
	t->ipc = ipc;
	copy(t->name, sh ? sh->name : "IPC$", sizeof t->name);
	memset(rep, 0, sizeof *rep);
	if (ipc) {
		rep->share_type = SMB2_SHARE_TYPE_PIPE;
		rep->maximal_access = 0x001F00A9;
	} else {
		rep->share_type = SMB2_SHARE_TYPE_DISK;
		rep->maximal_access = sh->readonly ? 0x001200A9 : 0x001F01FF;
	}
	log_msg("connexion au partage « %s »%s", name, sh && sh->readonly ? " (lecture seule)" : "");
	free(unc);
	struct smb2_pdu *pdu = smb2_cmd_tree_connect_reply_async(smb2, rep, t->tid, NULL, NULL);
	if (pdu)
		smb2_queue_pdu(smb2, pdu);
	return 1;
}

static int on_tree_disconnect(struct smb2_server *s, struct smb2_context *smb2, const uint32_t tid)
{
	(void)s;
	fs_close_tree(smb2, tid);
	for (int i = 0; i < SMBD_MAX_TREES; i++)
		if (trees[i].ctx == smb2 && trees[i].tid == tid)
			trees[i].ctx = 0;
	return 0;
}

static int on_echo(struct smb2_server *s, struct smb2_context *smb2)
{
	(void)s;
	(void)smb2;
	return 0;
}

static int on_cancel(struct smb2_server *s, struct smb2_context *smb2)
{
	(void)s;
	(void)smb2;
	return 1; /* (pas de réponse à CANCEL) */
}

static int on_destroy(struct smb2_server *s, struct smb2_context *smb2)
{
	(void)s;
	fs_close_all(smb2);
	for (int i = 0; i < SMBD_MAX_TREES; i++)
		if (trees[i].ctx == smb2)
			trees[i].ctx = 0;
	const char *e = smb2_get_error(smb2);
	log_dbg("connexion fermée%s%s", e && *e ? " : " : "", e && *e ? e : "");
	return 0;
}

static void on_connection(struct smb2_context *smb2, void *cb_data)
{
	(void)cb_data;
	/* les réponses de fichiers et d'infos sont encodées par fs.c (octets bruts) */
	smb2_set_passthrough(smb2, 1);
	smb2_set_sign(smb2, 1); /* signature exigée (E70 S8) : un client qui ne signe pas est refusé */
	log_dbg("nouvelle connexion");
}

static struct smb2_server_request_handlers handlers = {
	.destruction_event = on_destroy,
	.authorize_user = on_authorize,
	.session_established = on_session,
	.logoff_cmd = on_logoff,
	.tree_connect_cmd = on_tree_connect,
	.tree_disconnect_cmd = on_tree_disconnect,
	.create_cmd = fs_create,
	.close_cmd = fs_close,
	.flush_cmd = fs_flush,
	.read_cmd = fs_read,
	.write_cmd = fs_write,
	.lock_cmd = fs_lock,
	.ioctl_cmd = fs_ioctl,
	.cancel_cmd = on_cancel,
	.echo_cmd = on_echo,
	.query_directory_cmd = fs_query_directory,
	.change_notify_cmd = fs_change_notify,
	.query_info_cmd = fs_query_info,
	.set_info_cmd = fs_set_info,
};

static void usage(void)
{
	fputs("usage : smbd [-c FICHIER] [-s FICHIER] [-p PORT] [-v]\n"
	      "  -c FICHIER   la configuration et les comptes (/etc/smbd.ini)\n"
	      "  -s FICHIER   les dossiers partagés (/etc/shares.ini)\n"
	      "  -p PORT      le port TCP (445)\n"
	      "  -v           le détail des requêtes au journal\n",
	      stderr);
}

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-c") && i + 1 < argc)
			conf_path = argv[++i];
		else if (!strcmp(argv[i], "-s") && i + 1 < argc)
			shares_path = argv[++i];
		else if (!strcmp(argv[i], "-p") && i + 1 < argc)
			cli_port = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-v"))
			cli_verbose = 1;
		else {
			usage();
			return 2;
		}
	}
	conf_reload();
	/* (sans partage ni compte, il attend : le volet de Réglages les ajoute, relus au vol) */
	if (!conf.nshares)
		log_msg("aucun partage : Réglages › Partage de fichiers (ou %s)", shares_path);
	if (!conf.nusers)
		log_msg("aucun compte : Réglages › Partage de fichiers (ou [users] de %s)", conf_path);
	signal(SIGPIPE, SIG_IGN);

	static struct smb2_server server;
	memset(&server, 0, sizeof server);
	server.port = (uint16_t)conf.port;
	server.handlers = &handlers;
	server.signing_enabled = 1;
	server.allow_anonymous = 0;
	server.max_transact_size = server.max_read_size = server.max_write_size = 0x100000;
	copy(server.hostname, conf.name, sizeof server.hostname);
	copy(server.domain, conf.workgroup, sizeof server.domain);
	/* un GUID stable par nom de serveur (Windows le garde par connexion) */
	uint32_t h = 2166136261u;
	for (const char *p = conf.name; *p; p++)
		h = (h ^ (uint8_t)toupper((unsigned char)*p)) * 16777619u;
	memcpy(server.guid, "KorinSMB", 8);
	for (int i = 0; i < 8; i++)
		server.guid[8 + i] = (uint8_t)(h >> (i * 4));

	for (int i = 0; i < conf.nshares; i++)
		log_msg("partage « %s » -> %s%s", conf.shares[i].name, conf.shares[i].path,
			conf.shares[i].readonly ? " (lecture seule)" : "");
	log_msg("en écoute sur le port %d (%s, %d compte%s)", conf.port, conf.name, conf.nusers,
		conf.nusers > 1 ? "s" : "");
	for (;;) {
		int r = smb2_serve_port(&server, 16, on_connection, NULL);
		log_msg("boucle du serveur finie (%d), reprise", r);
		sleep(1);
	}
}
