/*
 * tls.c -- webd : HTTPS par OpenSSL. Un contexte par nom d'hôte, choisi par SNI ; le certificat d'un hôte est
 * /etc/web/certs/<hôte>.crt (le certificat, puis sa chaîne) et <hôte>.key (« * » : default.crt, default.key).
 * Un hôte sans certificat en reçoit un auto-signé au démarrage (dit dans le journal).
 *
 * Commandes (webd cert ...) :
 *   list                       les certificats, leur émetteur et leur fin
 *   self HÔTE [JOURS]          un certificat auto-signé (P-256, 825 jours par défaut)
 *   csr HÔTE                   une demande (HÔTE.csr) et sa clé en attente (HÔTE.pending.key)
 *   import HÔTE FICHIER        le certificat reçu du fournisseur (et sa chaîne) pour la clé en attente, ou la clé
 *   remove HÔTE                retire le certificat et ses clés
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include "webd.h"
#include "../../lib/keyvault/keyvault.h"

int webtls_init (void)
{
	return OPENSSL_init_ssl (0, NULL) == 1 ? 0 : -1;
}

static const char *file_host (const char *host)
{
	return strcmp (host, "*") ? host : "default";
}

static void cert_path (const char *host, const char *ext, char *out, size_t n)
{
	snprintf (out, n, "%s/%s.%s", CERT_DIR, file_host (host), ext);
}

static int is_ip (const char *h)
{
	for (; *h; h++)
		if (!(*h == '.' || (*h >= '0' && *h <= '9')))
			return 0;
	return 1;
}

/* ---- les clés privées : dans le coffre à secrets (le PC, service keyvault, docs/etude/COFFRE.md), le secret
 * « tls/<nom du fichier> » de l'espace @system (webd est d'uid 0 ; « webd cert » aussi, lancé élevé par le volet
 * de Réglages), lu en mémoire ; les certificats (publics) restent des fichiers. Une clé encore en fichier
 * (/etc/web/certs/<hôte>.key, .pending.key) passe dans le coffre à sa première lecture, puis le fichier est
 * effacé ; webd range ainsi toutes celles du dossier à son chargement. Sans coffre (ou appelant d'uid autre que 0) :
 * les fichiers PEM, comme avant. */

static int vault_ok (void)
{
	static int ok = -1;
	if (ok < 0)
		ok = access ("/sys/svc/keyvault", F_OK) == 0 && kv_request (KV_STATUS, KVF_SYSTEM, 0, 0, 0) >= 0;
	return ok;
}

/* chemin d'une clé -> son nom dans le coffre : « tls/<nom du fichier> », « tls/@<hexadécimal> » s'il a un caractère
 * qu'un nom n'admet pas */
static void vault_name (const char *path, char *out, size_t n)
{
	const char *b = strrchr (path, '/');
	b = b ? b + 1 : path;
	size_t len = strlen (b);
	int raw = len <= 120;
	for (const char *p = b; *p && raw; p++)
		raw = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr ("._-:", *p);
	size_t o = (size_t) snprintf (out, n, "tls/%s", raw ? "" : "@");
	for (size_t i = 0; i < len && i < (raw ? 120 : 60) && o + 3 < n; i++) {
		if (raw)
			out[o++] = b[i];
		else
			o += (size_t) snprintf (out + o, 3, "%02x", (unsigned char) b[i]);
	}
	out[o] = 0;
}

/* la clé dans le coffre -> 0 */
static int vault_put (const char *path, EVP_PKEY *k)
{
	char name[160];
	vault_name (path, name, sizeof name);
	BIO *b = BIO_new (BIO_s_mem ());
	int r = -1;
	if (b && PEM_write_bio_PrivateKey (b, k, NULL, NULL, 0, NULL, NULL)) {
		BUF_MEM *m = NULL;
		BIO_get_mem_ptr (b, &m);
		if (m && m->length <= KV_VALUE_MAX && kv_sys_set (name, m->data, (long) m->length) >= 0)
			r = 0;
		if (m)
			OPENSSL_cleanse (m->data, m->length);
	}
	BIO_free (b);
	return r;
}

/* -> la clé du coffre, NULL si elle n'y est pas */
static EVP_PKEY *vault_read (const char *path)
{
	char name[160];
	vault_name (path, name, sizeof name);
	char *t = malloc (KV_VALUE_MAX);
	long n = t ? kv_sys_get (name, t, KV_VALUE_MAX) : -1;
	EVP_PKEY *k = NULL;
	if (n > 0) {
		BIO *b = BIO_new_mem_buf (t, (int) n);
		k = b ? PEM_read_bio_PrivateKey (b, NULL, NULL, NULL) : NULL;
		BIO_free (b);
	}
	if (t) {
		OPENSSL_cleanse (t, KV_VALUE_MAX);
		free (t);
	}
	return k;
}

static int write_key (const char *path, EVP_PKEY *k)
{
	if (vault_ok () && vault_put (path, k) == 0) {
		remove (path);			/* (une ancienne en clair) */
		return 0;
	}
	FILE *f = fopen (path, "w");
	if (!f)
		return -1;
	int ok = PEM_write_PrivateKey (f, k, NULL, NULL, 0, NULL, NULL);
	fclose (f);
	chmod (path, 0600);
	return ok ? 0 : -1;
}

static EVP_PKEY *read_key (const char *path)
{
	EVP_PKEY *k = NULL;
	FILE *f = fopen (path, "r");		/* en clair : la plus récente (ancien système, coffre absent un temps) */
	if (f) {
		k = PEM_read_PrivateKey (f, NULL, NULL, NULL);
		fclose (f);
		if (k && vault_ok () && vault_put (path, k) == 0) {	/* rangée dans le coffre, le fichier effacé */
			remove (path);
			log_msg ("clé %s rangée dans le coffre à secrets", path);
		}
		return k;
	}
	return vault_ok () ? vault_read (path) : NULL;
}

/* -> 1 si la clé existe (fichier ou coffre) */
static int key_exists (const char *path)
{
	if (file_exists (path))
		return 1;
	if (!vault_ok ())
		return 0;
	char name[160];
	vault_name (path, name, sizeof name);
	return kv_sys_get (name, NULL, 0) > 0;
}

/* la clé retirée (fichier et coffre) */
static void remove_key (const char *path)
{
	remove (path);
	if (vault_ok ()) {
		char name[160];
		vault_name (path, name, sizeof name);
		kv_sys_delete (name);
	}
}

/* les clés en fichier du dossier -> dans le coffre (read_key les y range) */
static void keys_migrate (void)
{
	if (!vault_ok ())
		return;
	DIR *d = opendir (CERT_DIR);
	if (!d)
		return;
	struct dirent *e;
	char paths[32][300];
	int n = 0;
	while (n < 32 && (e = readdir (d))) {
		size_t l = strlen (e->d_name);
		if (l > 4 && !strcmp (e->d_name + l - 4, ".key"))
			snprintf (paths[n++], sizeof paths[0], "%s/%s", CERT_DIR, e->d_name);
	}
	closedir (d);
	for (int i = 0; i < n; i++)
		EVP_PKEY_free (read_key (paths[i]));
}

/* le nom alternatif (SAN) d'un hôte */
static void san_of (const char *host, char *out, size_t n)
{
	if (!strcmp (host, "*") || !strcmp (host, "localhost"))
		snprintf (out, n, "DNS:localhost,IP:127.0.0.1");
	else if (is_ip (host))
		snprintf (out, n, "IP:%s", host);
	else
		snprintf (out, n, "DNS:%s", host);
}

static int add_ext (X509 *x, int nid, const char *value)
{
	X509V3_CTX ctx;
	X509V3_set_ctx_nodb (&ctx);
	X509V3_set_ctx (&ctx, x, x, NULL, NULL, 0);
	X509_EXTENSION *e = X509V3_EXT_conf_nid (NULL, &ctx, nid, value);
	if (!e)
		return -1;
	X509_add_ext (x, e, -1);
	X509_EXTENSION_free (e);
	return 0;
}

static int cert_self (const char *host, int days)
{
	mkdirs (CERT_DIR);
	char kp[300], cp[300], san[300];
	const char *cn = strcmp (host, "*") ? host : "localhost";
	EVP_PKEY *k = EVP_EC_gen ("P-256");
	X509 *x = X509_new ();
	BIGNUM *bn = BN_new ();
	int ok = k && x && bn;
	if (ok) {
		X509_set_version (x, 2);
		BN_rand (bn, 63, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY);
		BN_to_ASN1_INTEGER (bn, X509_get_serialNumber (x));
		X509_gmtime_adj (X509_getm_notBefore (x), -3600);
		X509_gmtime_adj (X509_getm_notAfter (x), (long) days * 86400);
		X509_set_pubkey (x, k);
		X509_NAME *nm = X509_get_subject_name (x);
		X509_NAME_add_entry_by_txt (nm, "CN", MBSTRING_UTF8, (const unsigned char *) cn, -1, -1, 0);
		X509_NAME_add_entry_by_txt (nm, "O", MBSTRING_UTF8, (const unsigned char *) "Korin webd", -1, -1, 0);
		X509_set_issuer_name (x, nm);
		san_of (host, san, sizeof san);
		ok = add_ext (x, NID_subject_alt_name, san) == 0 && add_ext (x, NID_basic_constraints, "critical,CA:FALSE") == 0 &&
			add_ext (x, NID_key_usage, "critical,digitalSignature,keyEncipherment") == 0 &&
			add_ext (x, NID_ext_key_usage, "serverAuth") == 0 && X509_sign (x, k, EVP_sha256 ()) > 0;
	}
	if (ok) {
		cert_path (host, "key", kp, sizeof kp);
		cert_path (host, "crt", cp, sizeof cp);
		FILE *f = write_key (kp, k) == 0 ? fopen (cp, "w") : NULL;
		ok = f && PEM_write_X509 (f, x);
		if (f)
			fclose (f);
	}
	BN_free (bn);
	X509_free (x);
	EVP_PKEY_free (k);
	return ok ? 0 : -1;
}

static SSL_CTX *ctx_for (const char *host, int make)
{
	char kp[300], cp[300];
	cert_path (host, "key", kp, sizeof kp);
	cert_path (host, "crt", cp, sizeof cp);
	if (file_exists (cp) && !key_exists (kp) && access ("/sys/svc/keyvault", F_OK) == 0 && !vault_ok ()) {
		log_msg ("clé de %s : dans le coffre à secrets, qui ne répond pas (webd doit être d'uid 0)", host);
		return NULL;			/* (pas de nouveau certificat à la place du sien) */
	}
	if (!file_exists (cp) || !key_exists (kp)) {
		if (!make || cert_self (host, 825) < 0) {
			log_msg ("pas de certificat pour %s (%s)", host, cp);
			return NULL;
		}
		log_msg ("certificat auto-signé créé pour %s (%s)", host, cp);
	}
	SSL_CTX *ctx = SSL_CTX_new (TLS_server_method ());
	if (!ctx)
		return NULL;
	SSL_CTX_set_min_proto_version (ctx, TLS1_2_VERSION);
	EVP_PKEY *k = read_key (kp);		/* (le coffre, ou le fichier) */
	int bad = !k || SSL_CTX_use_certificate_chain_file (ctx, cp) != 1 || SSL_CTX_use_PrivateKey (ctx, k) != 1 ||
		SSL_CTX_check_private_key (ctx) != 1;
	EVP_PKEY_free (k);
	if (bad) {
		unsigned long e = ERR_get_error ();
		char why[256];
		ERR_error_string_n (e, why, sizeof why);
		log_msg ("certificat de %s inutilisable : %s", host, why);
		ERR_clear_error ();
		SSL_CTX_free (ctx);
		return NULL;
	}
	return ctx;
}

static int sni_cb (SSL *ssl, int *alert, void *arg)
{
	(void) alert;
	struct conf *c = arg;
	const char *name = SSL_get_servername (ssl, TLSEXT_NAMETYPE_host_name);
	if (!name)
		return SSL_TLSEXT_ERR_OK;
	for (int i = 0; i < c->ncerts; i++)
		if (!strcasecmp (c->certs[i].host, name)) {
			SSL_set_SSL_CTX (ssl, c->certs[i].ctx);
			break;
		}
	return SSL_TLSEXT_ERR_OK;
}

static SSL_CTX *conf_cert (struct conf *c, const char *host)
{
	for (int i = 0; i < c->ncerts; i++)
		if (!strcmp (c->certs[i].host, host))
			return c->certs[i].ctx;
	if (c->ncerts >= (int) (sizeof c->certs / sizeof c->certs[0]))
		return NULL;
	SSL_CTX *ctx = ctx_for (host, 1);
	if (!ctx)
		return NULL;
	SSL_CTX_set_tlsext_servername_callback (ctx, sni_cb);
	SSL_CTX_set_tlsext_servername_arg (ctx, c);
	snprintf (c->certs[c->ncerts].host, sizeof c->certs[0].host, "%s", host);
	c->certs[c->ncerts++].ctx = ctx;
	return ctx;
}

/* les contextes des ports https : celui du premier hôte du port par défaut, les autres par SNI. -> 0, -1 si un port
 * https reste sans certificat */
int webtls_conf (struct conf *c, struct listener *ls, int nls)
{
	int bad = 0;
	keys_migrate ();
	for (int k = 0; k < nls; k++) {
		if (ls[k].scheme != SCHEME_HTTPS)
			continue;
		for (int i = 0; i < c->nsites; i++) {
			struct site *s = &c->sites[i];
			if (!s->enabled)
				continue;
			for (int j = 0; j < s->nbinds; j++)
				if (s->binds[j].scheme == SCHEME_HTTPS && s->binds[j].port == ls[k].port) {
					SSL_CTX *ctx = conf_cert (c, s->binds[j].host);
					if (ctx && !ls[k].ctx)
						ls[k].ctx = ctx;
				}
		}
		if (!ls[k].ctx)
			bad = 1;
	}
	return bad ? -1 : 0;
}

void webtls_free (struct conf *c)
{
	for (int i = 0; i < c->ncerts; i++)
		SSL_CTX_free (c->certs[i].ctx);
	c->ncerts = 0;
}

/* ---- webd cert ---- */

static void print_cert (const char *path, const char *host)
{
	FILE *f = fopen (path, "r");
	X509 *x = f ? PEM_read_X509 (f, NULL, NULL, NULL) : NULL;
	if (f)
		fclose (f);
	if (!x) {
		printf ("%-28s (illisible)\n", host);
		return;
	}
	char issuer[256] = "?";
	X509_NAME_get_text_by_NID (X509_get_issuer_name (x), NID_commonName, issuer, sizeof issuer);
	BIO *b = BIO_new (BIO_s_mem ());
	ASN1_TIME_print (b, X509_get0_notAfter (x));
	char end[64] = "";
	int n = BIO_read (b, end, sizeof end - 1);
	end[n > 0 ? n : 0] = 0;
	BIO_free (b);
	int self = X509_NAME_cmp (X509_get_issuer_name (x), X509_get_subject_name (x)) == 0;
	printf ("%-28s %s, jusqu'au %s\n", host, self ? "auto-signé" : issuer, end);
	X509_free (x);
}

static int cert_csr (const char *host)
{
	mkdirs (CERT_DIR);
	char kp[300], rp[300], san[300];
	snprintf (kp, sizeof kp, "%s/%s.pending.key", CERT_DIR, file_host (host));
	snprintf (rp, sizeof rp, "%s/%s.csr", CERT_DIR, file_host (host));
	EVP_PKEY *k = EVP_EC_gen ("P-256");
	X509_REQ *r = X509_REQ_new ();
	int ok = k && r;
	if (ok) {
		X509_REQ_set_version (r, 0);
		X509_REQ_set_pubkey (r, k);
		X509_NAME *nm = X509_REQ_get_subject_name (r);
		X509_NAME_add_entry_by_txt (nm, "CN", MBSTRING_UTF8, (const unsigned char *) host, -1, -1, 0);
		san_of (host, san, sizeof san);
		STACK_OF (X509_EXTENSION) *exts = sk_X509_EXTENSION_new_null ();
		X509_EXTENSION *e = X509V3_EXT_conf_nid (NULL, NULL, NID_subject_alt_name, san);
		if (e)
			sk_X509_EXTENSION_push (exts, e);
		X509_REQ_add_extensions (r, exts);
		sk_X509_EXTENSION_pop_free (exts, X509_EXTENSION_free);
		ok = X509_REQ_sign (r, k, EVP_sha256 ()) > 0 && write_key (kp, k) == 0;
	}
	if (ok) {
		FILE *f = fopen (rp, "w");
		ok = f && PEM_write_X509_REQ (f, r);
		if (f)
			fclose (f);
	}
	X509_REQ_free (r);
	EVP_PKEY_free (k);
	if (!ok)
		return -1;
	printf ("demande : %s\nà envoyer au fournisseur ; puis : webd cert import %s <certificat reçu>\n", rp, host);
	return 0;
}

static int cert_import (const char *host, const char *file)
{
	char kp[300], pk[300], cp[300];
	cert_path (host, "key", kp, sizeof kp);
	cert_path (host, "crt", cp, sizeof cp);
	snprintf (pk, sizeof pk, "%s/%s.pending.key", CERT_DIR, file_host (host));
	int pending = key_exists (pk);
	EVP_PKEY *k = read_key (pending ? pk : kp);
	if (!k) {
		fprintf (stderr, "webd: pas de clé pour %s : d'abord « webd cert csr %s »\n", host, host);
		return -1;
	}
	FILE *f = fopen (file, "r");
	if (!f) {
		EVP_PKEY_free (k);
		fprintf (stderr, "webd: %s illisible\n", file);
		return -1;
	}
	/* le certificat, puis la chaîne : tout est gardé, dans l'ordre */
	X509 *chain[16];
	int n = 0;
	while (n < 16 && (chain[n] = PEM_read_X509 (f, NULL, NULL, NULL)))
		n++;
	fclose (f);
	ERR_clear_error ();
	int ok = n > 0 && X509_check_private_key (chain[0], k) == 1;
	if (!ok)
		fprintf (stderr, "webd: %s\n", n ? "ce certificat n'est pas celui de la clé de la demande" : "aucun certificat PEM");
	if (ok) {
		FILE *o = fopen (cp, "w");
		for (int i = 0; o && i < n; i++)
			ok = ok && PEM_write_X509 (o, chain[i]);
		ok = o && ok;
		if (o)
			fclose (o);
	}
	if (ok && pending) {			/* la clé en attente devient celle de l'hôte */
		ok = write_key (kp, k) == 0;
		if (ok)
			remove_key (pk);
	}
	for (int i = 0; i < n; i++)
		X509_free (chain[i]);
	EVP_PKEY_free (k);
	if (ok)
		printf ("certificat de %s installé (%d dans la chaîne) ; il servira après « webd reload »\n", host, n);
	return ok ? 0 : -1;
}

int cert_command (int argc, char **argv)
{
	webtls_init ();
	const char *op = argc > 0 ? argv[0] : "list";
	if (!strcmp (op, "list")) {
		DIR *d = opendir (CERT_DIR);
		if (!d) {
			printf ("aucun certificat (%s)\n", CERT_DIR);
			return 0;
		}
		struct dirent *e;
		while ((e = readdir (d))) {
			size_t l = strlen (e->d_name);
			if (l > 4 && !strcmp (e->d_name + l - 4, ".crt")) {
				char host[256], path[600];
				snprintf (host, sizeof host, "%.*s", (int) (l - 4), e->d_name);
				snprintf (path, sizeof path, "%s/%s", CERT_DIR, e->d_name);
				print_cert (path, host);
			}
		}
		closedir (d);
		return 0;
	}
	if (argc < 2) {
		fprintf (stderr, "webd cert %s : il faut un nom d'hôte\n", op);
		return 2;
	}
	const char *host = argv[1];
	if (!strcmp (op, "self")) {
		int days = argc > 2 ? atoi (argv[2]) : 825;
		if (cert_self (host, days > 0 ? days : 825) < 0) {
			fprintf (stderr, "webd: certificat non créé\n");
			return 1;
		}
		char cp[300];
		cert_path (host, "crt", cp, sizeof cp);
		printf ("certificat auto-signé : %s (%d jours)\n", cp, days > 0 ? days : 825);
		return 0;
	}
	if (!strcmp (op, "csr"))
		return cert_csr (host) < 0 ? 1 : 0;
	if (!strcmp (op, "import")) {
		if (argc < 3) {
			fprintf (stderr, "webd cert import HÔTE FICHIER\n");
			return 2;
		}
		return cert_import (host, argv[2]) < 0 ? 1 : 0;
	}
	if (!strcmp (op, "remove")) {
		static const char *exts[] = { "crt", "key", "csr", "pending.key" };
		for (int i = 0; i < 4; i++) {
			char p[300];
			cert_path (host, exts[i], p, sizeof p);
			remove_key (p);		/* (un fichier ; une clé : aussi dans le coffre) */
		}
		printf ("certificat de %s retiré\n", host);
		return 0;
	}
	fprintf (stderr, "webd cert : list, self, csr, import, remove\n");
	return 2;
}
