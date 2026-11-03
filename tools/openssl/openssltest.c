/*
 * openssltest.c -- l'essai d'OpenSSL sous ARMPI (docs/OPENSSL.md), groupe par groupe comme
 * posixtest (le telnet d'essai coupe au bout de 60 s) :
 *
 *     openssltest crypto               hachages, chiffrements, signatures, vecteurs connus
 *     openssltest tls                  poignees de main TLS 1.2 et 1.3 en memoire
 *     openssltest threads              quatre threads, un meme SSL_CTX
 *     openssltest net [hote [port]]    vrai HTTPS, certificat verifie par /etc/ca-bundle
 *     openssltest speed                debits, pour comparer QEMU et le Pi
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/provider.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

static int s_ok, s_bad;

static void check (int cond, const char *name)
{
	if (cond)
	{
		s_ok++;
		printf ("ok    %s\n", name);
	}
	else
	{
		s_bad++;
		printf ("ECHEC %s\n", name);
		ERR_print_errors_fp (stdout);
	}
	fflush (stdout);
}

static int hex_is (const unsigned char *b, size_t n, const char *hex)
{
	char s[2 * EVP_MAX_MD_SIZE + 1];
	if (n > EVP_MAX_MD_SIZE || strlen (hex) != 2 * n)
		return 0;
	for (size_t i = 0; i < n; i++)
		sprintf (s + 2 * i, "%02x", b[i]);
	return strcmp (s, hex) == 0;
}

static int digest_is (const char *alg, const char *msg, const char *hex)
{
	unsigned char md[EVP_MAX_MD_SIZE];
	size_t n = sizeof md;
	return EVP_Q_digest (NULL, alg, NULL, msg, strlen (msg), md, &n) && hex_is (md, n, hex);
}

/* ---- crypto ---- */

/* chiffre puis dechiffre ; authentifie : un octet change doit faire echouer le dechiffrement */
static int aead_round (const char *alg)
{
	unsigned char key[32], iv[12], tag[16], ct[64], pt[64];
	const unsigned char aad[] = "armpi";
	const unsigned char msg[] = "Le chiffrement authentifie sous ARMPI.";
	int n, m, ok = 0;
	EVP_CIPHER *c = EVP_CIPHER_fetch (NULL, alg, NULL);
	EVP_CIPHER_CTX *x = EVP_CIPHER_CTX_new ();
	if (c == NULL || x == NULL || RAND_bytes (key, sizeof key) != 1 || RAND_bytes (iv, sizeof iv) != 1)
		goto out;
	if (!EVP_EncryptInit_ex2 (x, c, key, iv, NULL) || !EVP_EncryptUpdate (x, NULL, &n, aad, sizeof aad)
	    || !EVP_EncryptUpdate (x, ct, &n, msg, sizeof msg) || !EVP_EncryptFinal_ex (x, ct + n, &m)
	    || !EVP_CIPHER_CTX_ctrl (x, EVP_CTRL_AEAD_GET_TAG, sizeof tag, tag))
		goto out;
	for (int alter = 0; alter < 2; alter++)
	{
		ct[3] ^= alter;
		if (!EVP_DecryptInit_ex2 (x, c, key, iv, NULL) || !EVP_DecryptUpdate (x, NULL, &n, aad, sizeof aad)
		    || !EVP_DecryptUpdate (x, pt, &n, ct, sizeof msg)
		    || !EVP_CIPHER_CTX_ctrl (x, EVP_CTRL_AEAD_SET_TAG, sizeof tag, tag))
			goto out;
		int r = EVP_DecryptFinal_ex (x, pt + n, &m);
		if (alter ? r > 0 : r <= 0 || memcmp (pt, msg, sizeof msg) != 0)
			goto out;
	}
	ok = 1;
out:
	ERR_clear_error ();
	EVP_CIPHER_CTX_free (x);
	EVP_CIPHER_free (c);
	return ok;
}

static int sign_round (EVP_PKEY *k, const char *md)
{
	const unsigned char msg[] = "un message a signer";
	unsigned char sig[8192];
	size_t n = sizeof sig;
	EVP_MD_CTX *x = EVP_MD_CTX_new ();
	int ok = x != NULL && EVP_DigestSignInit_ex (x, NULL, md, NULL, NULL, k, NULL) == 1
		&& EVP_DigestSign (x, sig, &n, msg, sizeof msg) == 1
		&& EVP_DigestVerifyInit_ex (x, NULL, md, NULL, NULL, k, NULL) == 1
		&& EVP_DigestVerify (x, sig, n, msg, sizeof msg) == 1;
	if (ok)
	{
		sig[n / 2] ^= 1;
		ok = EVP_DigestVerifyInit_ex (x, NULL, md, NULL, NULL, k, NULL) == 1
			&& EVP_DigestVerify (x, sig, n, msg, sizeof msg) != 1;
	}
	ERR_clear_error ();
	EVP_MD_CTX_free (x);
	return ok;
}

static int x25519_agree (void)
{
	unsigned char a[32], b[32];
	size_t na = sizeof a, nb = sizeof b;
	EVP_PKEY *ka = EVP_PKEY_Q_keygen (NULL, NULL, "X25519"), *kb = EVP_PKEY_Q_keygen (NULL, NULL, "X25519");
	EVP_PKEY_CTX *ca = EVP_PKEY_CTX_new (ka, NULL), *cb = EVP_PKEY_CTX_new (kb, NULL);
	int ok = ka && kb && ca && cb && EVP_PKEY_derive_init (ca) == 1 && EVP_PKEY_derive_set_peer (ca, kb) == 1
		&& EVP_PKEY_derive (ca, a, &na) == 1 && EVP_PKEY_derive_init (cb) == 1
		&& EVP_PKEY_derive_set_peer (cb, ka) == 1 && EVP_PKEY_derive (cb, b, &nb) == 1
		&& na == 32 && nb == 32 && memcmp (a, b, 32) == 0;
	EVP_PKEY_CTX_free (ca);
	EVP_PKEY_CTX_free (cb);
	EVP_PKEY_free (ka);
	EVP_PKEY_free (kb);
	return ok;
}

static int mlkem_round (void)
{
	unsigned char ct[2048], s1[64], s2[64];
	size_t nc = sizeof ct, n1 = sizeof s1, n2 = sizeof s2;
	EVP_PKEY *k = EVP_PKEY_Q_keygen (NULL, NULL, "ML-KEM-768");
	EVP_PKEY_CTX *x = EVP_PKEY_CTX_new_from_pkey (NULL, k, NULL);
	int ok = k && x && EVP_PKEY_encapsulate_init (x, NULL) == 1 && EVP_PKEY_encapsulate (x, ct, &nc, s1, &n1) == 1
		&& EVP_PKEY_decapsulate_init (x, NULL) == 1 && EVP_PKEY_decapsulate (x, s2, &n2, ct, nc) == 1
		&& n1 == n2 && memcmp (s1, s2, n1) == 0;
	EVP_PKEY_CTX_free (x);
	EVP_PKEY_free (k);
	return ok;
}

static void group_crypto (void)
{
	check (strncmp (OpenSSL_version (OPENSSL_VERSION), "OpenSSL 3.5", 11) == 0, OpenSSL_version (OPENSSL_VERSION));

	check (digest_is ("SHA256", "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256");
	check (digest_is ("SHA512", "abc", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
		"2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"), "SHA-512");
	check (digest_is ("SHA3-256", "abc", "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"), "SHA3-256");
	check (digest_is ("SHA1", "abc", "a9993e364706816aba3e25717850c26c9cd0d89d"), "SHA-1");
	check (digest_is ("MD5", "abc", "900150983cd24fb0d6963f7d28e17f72"), "MD5");

	unsigned char mac[EVP_MAX_MD_SIZE];
	unsigned int macn = sizeof mac;
	check (HMAC (EVP_sha256 (), "Jefe", 4, (const unsigned char *) "what do ya want for nothing?", 28, mac, &macn) != NULL
		&& hex_is (mac, macn, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"), "HMAC-SHA-256 (RFC 4231)");

	unsigned char dk[32];
	check (PKCS5_PBKDF2_HMAC ("password", 8, (const unsigned char *) "salt", 4, 4096, EVP_sha256 (), 32, dk) == 1
		&& hex_is (dk, 32, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"), "PBKDF2-SHA-256");

	/* AES-128 en bloc (FIPS 197, annexe C.1) */
	static const unsigned char aes_key[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	static const unsigned char aes_pt[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
	unsigned char aes_ct[32];
	int n = 0;
	EVP_CIPHER_CTX *x = EVP_CIPHER_CTX_new ();
	check (x && EVP_EncryptInit_ex2 (x, EVP_aes_128_ecb (), aes_key, NULL, NULL) == 1 && EVP_CIPHER_CTX_set_padding (x, 0) == 1
		&& EVP_EncryptUpdate (x, aes_ct, &n, aes_pt, 16) == 1 && n == 16
		&& hex_is (aes_ct, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"), "AES-128 (FIPS 197)");
	EVP_CIPHER_CTX_free (x);

	check (aead_round ("AES-256-GCM"), "AES-256-GCM, aller-retour, tag faux refuse");
	check (aead_round ("CHACHA20-POLY1305"), "ChaCha20-Poly1305, aller-retour, tag faux refuse");

	unsigned char r1[32], r2[32];
	check (RAND_bytes (r1, 32) == 1 && RAND_bytes (r2, 32) == 1 && memcmp (r1, r2, 32) != 0, "RAND_bytes (getentropy)");

	EVP_PKEY *k = EVP_PKEY_Q_keygen (NULL, NULL, "RSA", (size_t) 2048);
	check (k != NULL && sign_round (k, "SHA256"), "RSA-2048 : cle, signature, verification");
	EVP_PKEY_free (k);
	k = EVP_PKEY_Q_keygen (NULL, NULL, "EC", "P-256");
	check (k != NULL && sign_round (k, "SHA256"), "ECDSA P-256 : cle, signature, verification");
	EVP_PKEY_free (k);
	k = EVP_PKEY_Q_keygen (NULL, NULL, "ED25519");
	check (k != NULL && sign_round (k, NULL), "Ed25519 : cle, signature, verification");
	EVP_PKEY_free (k);
	check (x25519_agree (), "X25519 : meme secret des deux cotes");
	check (mlkem_round (), "ML-KEM-768 : encapsulation, decapsulation");

	/* no-module : le fournisseur « legacy » est dans libcrypto, sans dlopen */
	OSSL_PROVIDER *leg = OSSL_PROVIDER_load (NULL, "legacy"), *def = OSSL_PROVIDER_load (NULL, "default");
	check (leg != NULL && def != NULL, "fournisseur legacy charge sans dlopen");
	check (digest_is ("MD4", "abc", "a448017aaf21d8525fc10ae87aa6729d"), "MD4 (legacy)");
	OSSL_PROVIDER_unload (leg);
	OSSL_PROVIDER_unload (def);
}

/* ---- TLS en memoire ---- */

/* un certificat auto-signe pour « armpi.test », valable une heure autour de maintenant */
static X509 *make_cert (EVP_PKEY **key)
{
	EVP_PKEY *k = EVP_PKEY_Q_keygen (NULL, NULL, "EC", "P-256");
	X509 *c = X509_new ();
	X509_NAME *name = c ? X509_get_subject_name (c) : NULL;
	X509V3_CTX v3;
	X509_EXTENSION *ext;
	if (k == NULL || name == NULL)
		return NULL;
	X509_set_version (c, X509_VERSION_3);
	ASN1_INTEGER_set (X509_get_serialNumber (c), 1);
	X509_gmtime_adj (X509_getm_notBefore (c), -3600);
	X509_gmtime_adj (X509_getm_notAfter (c), 3600);
	X509_NAME_add_entry_by_txt (name, "CN", MBSTRING_ASC, (const unsigned char *) "armpi.test", -1, -1, 0);
	X509_set_issuer_name (c, name);
	X509_set_pubkey (c, k);
	X509V3_set_ctx_nodb (&v3);
	X509V3_set_ctx (&v3, c, c, NULL, NULL, 0);
	ext = X509V3_EXT_conf_nid (NULL, &v3, NID_subject_alt_name, "DNS:armpi.test");
	X509_add_ext (c, ext, -1);
	X509_EXTENSION_free (ext);
	ext = X509V3_EXT_conf_nid (NULL, &v3, NID_basic_constraints, "critical,CA:TRUE");
	X509_add_ext (c, ext, -1);
	X509_EXTENSION_free (ext);
	if (!X509_sign (c, k, EVP_sha256 ()))
		return NULL;
	*key = k;
	return c;
}

static SSL_CTX *server_ctx (X509 *cert, EVP_PKEY *key, int version)
{
	SSL_CTX *c = SSL_CTX_new (TLS_server_method ());
	if (c == NULL || SSL_CTX_use_certificate (c, cert) != 1 || SSL_CTX_use_PrivateKey (c, key) != 1
	    || SSL_CTX_set_min_proto_version (c, version) != 1 || SSL_CTX_set_max_proto_version (c, version) != 1)
		return NULL;
	return c;
}

static SSL_CTX *client_ctx (X509 *trusted, int version)
{
	SSL_CTX *c = SSL_CTX_new (TLS_client_method ());
	if (c == NULL || SSL_CTX_set_min_proto_version (c, version) != 1 || SSL_CTX_set_max_proto_version (c, version) != 1)
		return NULL;
	SSL_CTX_set_verify (c, SSL_VERIFY_PEER, NULL);
	if (trusted != NULL && X509_STORE_add_cert (SSL_CTX_get_cert_store (c), trusted) != 1)
		return NULL;
	return c;
}

/* la poignee de main : un seul thread, chaque cote avance jusqu'a attendre l'autre */
static int handshake (SSL *cli, SSL *srv)
{
	int cd = 0, sd = 0;
	for (int i = 0; i < 32 && !(cd && sd); i++)
	{
		if (!cd)
		{
			int r = SSL_do_handshake (cli);
			if (r == 1)
				cd = 1;
			else if (SSL_get_error (cli, r) != SSL_ERROR_WANT_READ)
				return 0;
		}
		if (!sd)
		{
			int r = SSL_do_handshake (srv);
			if (r == 1)
				sd = 1;
			else if (SSL_get_error (srv, r) != SSL_ERROR_WANT_READ)
				return 0;
		}
	}
	return cd && sd;
}

static int make_pair (SSL_CTX *cc, SSL_CTX *sc, const char *host, SSL **cli, SSL **srv)
{
	BIO *bc, *bs;
	*cli = SSL_new (cc);
	*srv = SSL_new (sc);
	if (*cli == NULL || *srv == NULL || BIO_new_bio_pair (&bc, 0, &bs, 0) != 1)
		return 0;
	SSL_set_bio (*cli, bc, bc);
	SSL_set_bio (*srv, bs, bs);
	SSL_set_connect_state (*cli);
	SSL_set_accept_state (*srv);
	SSL_set_tlsext_host_name (*cli, host);
	return SSL_set1_host (*cli, host) == 1;
}

static int echo_big (SSL *from, SSL *to)
{
	enum { LEN = 16384 };
	unsigned char *out = malloc (2 * LEN), *in = out + LEN;
	int got = 0, ok = out != NULL && RAND_bytes (out, LEN) == 1 && SSL_write (from, out, LEN) == LEN;
	while (ok && got < LEN)
	{
		int r = SSL_read (to, in + got, LEN - got);
		if (r <= 0)
			ok = 0;
		else
			got += r;
	}
	ok = ok && memcmp (in, out, LEN) == 0;
	free (out);
	return ok;
}

static void tls_round (X509 *cert, EVP_PKEY *key, int version, const char *vname, const char *cipher)
{
	char name[96];
	SSL *cli, *srv;
	SSL_CTX *sc = server_ctx (cert, key, version), *cc = client_ctx (cert, version);
	int paired = sc && cc && make_pair (cc, sc, "armpi.test", &cli, &srv);
	snprintf (name, sizeof name, "%s : poignee de main, certificat verifie", vname);
	check (paired && handshake (cli, srv) && SSL_get_verify_result (cli) == X509_V_OK, name);
	if (paired)
	{
		snprintf (name, sizeof name, "%s : %s", vname, SSL_get_cipher_name (cli));
		check (strcmp (SSL_get_version (cli), vname) == 0 && strcmp (SSL_get_cipher_name (cli), cipher) == 0, name);
		snprintf (name, sizeof name, "%s : SSL_write / SSL_read, 16 Ko dans chaque sens", vname);
		check (echo_big (cli, srv) && echo_big (srv, cli), name);
		SSL_free (cli);
		SSL_free (srv);
	}
	SSL_CTX_free (cc);
	SSL_CTX_free (sc);
}

static void group_tls (void)
{
	EVP_PKEY *key = NULL, *other_key = NULL;
	X509 *cert = make_cert (&key), *other = make_cert (&other_key);
	check (cert != NULL && other != NULL, "certificats auto-signes");
	if (cert == NULL || other == NULL)
		return;
	tls_round (cert, key, TLS1_3_VERSION, "TLSv1.3", "TLS_AES_256_GCM_SHA384");
	tls_round (cert, key, TLS1_2_VERSION, "TLSv1.2", "ECDHE-ECDSA-AES256-GCM-SHA384");

	/* ce qui doit echouer : un autre certificat, un autre nom */
	SSL *cli, *srv;
	SSL_CTX *sc = server_ctx (cert, key, TLS1_3_VERSION), *cc = client_ctx (other, TLS1_3_VERSION);
	check (make_pair (cc, sc, "armpi.test", &cli, &srv) && !handshake (cli, srv)
		&& SSL_get_verify_result (cli) == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT, "certificat inconnu refuse");
	ERR_clear_error ();
	SSL_free (cli);
	SSL_free (srv);
	SSL_CTX_free (cc);
	cc = client_ctx (cert, TLS1_3_VERSION);
	check (make_pair (cc, sc, "autre.test", &cli, &srv) && !handshake (cli, srv)
		&& SSL_get_verify_result (cli) == X509_V_ERR_HOSTNAME_MISMATCH, "mauvais nom d'hote refuse");
	ERR_clear_error ();
	SSL_free (cli);
	SSL_free (srv);
	SSL_CTX_free (cc);
	SSL_CTX_free (sc);
	X509_free (cert);
	X509_free (other);
	EVP_PKEY_free (key);
	EVP_PKEY_free (other_key);
}

/* ---- threads : ce que partagent les vrais programmes ---- */

static X509 *s_cert;
static EVP_PKEY *s_key;
static SSL_CTX *s_cc, *s_sc;

static void *worker (void *arg)
{
	long ok = 1;
	for (int i = 0; i < 25 && ok; i++)
	{
		SSL *cli = NULL, *srv = NULL;
		unsigned char r[32];
		const char *step = NULL;
		if (RAND_bytes (r, sizeof r) != 1)
			step = "RAND_bytes";
		else if (!make_pair (s_cc, s_sc, "armpi.test", &cli, &srv))
			step = "make_pair";
		else if (!handshake (cli, srv))
			step = "handshake";
		else if (!echo_big (cli, srv) || !echo_big (srv, cli))
			step = "echo_big";
		if (step != NULL)
		{
			printf ("      connexion %d : %s\n", i, step);
			ERR_print_errors_fp (stdout);
			ok = 0;
		}
		SSL_free (cli);
		SSL_free (srv);
	}
	return (void *) ok;
}

static void group_threads (void)
{
	pthread_t t[4];
	s_cert = make_cert (&s_key);
	s_sc = server_ctx (s_cert, s_key, TLS1_3_VERSION);
	s_cc = client_ctx (s_cert, TLS1_3_VERSION);
	check (s_cert && s_sc && s_cc, "un SSL_CTX pour tous");
	int started = 1;
	for (int i = 0; i < 4; i++)
		started &= pthread_create (&t[i], NULL, worker, NULL) == 0;
	check (started, "pthread_create x 4");
	long all = 1;
	for (int i = 0; i < 4; i++)
	{
		void *v = NULL;
		pthread_join (t[i], &v);
		all &= (long) v;
	}
	check (all, "25 connexions par thread, sans erreur");
}

/* ---- le reseau : un vrai serveur, le magasin de la carte ---- */

/* sur un socket non bloquant : attend ce que demande OpenSSL, 1 si on peut reessayer */
static int wait_io (SSL *s, int r)
{
	struct pollfd p = { .fd = SSL_get_fd (s) };
	switch (SSL_get_error (s, r))
	{
	case SSL_ERROR_WANT_READ:	p.events = POLLIN; break;
	case SSL_ERROR_WANT_WRITE:	p.events = POLLOUT; break;
	default:			return 0;
	}
	return poll (&p, 1, 15000) > 0;
}

static void https_get (SSL_CTX *c, const char *host, const char *port, int nonblocking)
{
	char target[300], name[300];
	const char *mode = nonblocking ? "non bloquant" : "bloquant";
	snprintf (target, sizeof target, "%s:%s", host, port);
	BIO *b = BIO_new_connect (target);
	int connected = b != NULL && BIO_do_connect (b) == 1;
	snprintf (name, sizeof name, "%s : BIO_do_connect (getaddrinfo, TCP)", mode);
	check (connected, name);
	SSL *s = SSL_new (c);
	if (!connected || s == NULL)
		return;
	SSL_set_bio (s, b, b);
	SSL_set_tlsext_host_name (s, host);
	SSL_set1_host (s, host);
	if (nonblocking)
		BIO_set_nbio (b, 1);
	int r;
	while ((r = SSL_connect (s)) != 1 && wait_io (s, r))
		;
	snprintf (name, sizeof name, "%s : SSL_connect, certificat verifie", mode);
	check (r == 1 && SSL_get_verify_result (s) == X509_V_OK, name);
	if (r == 1)
	{
		printf ("      %s, %s\n", SSL_get_version (s), SSL_get_cipher_name (s));
		X509 *peer = SSL_get1_peer_certificate (s);
		if (peer != NULL)
		{
			X509_NAME_oneline (X509_get_subject_name (peer), name, sizeof name);
			printf ("      %s\n", name);
			X509_free (peer);
		}
		char req[400], buf[512];
		int n = snprintf (req, sizeof req, "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
		while ((r = SSL_write (s, req, n)) <= 0 && wait_io (s, r))
			;
		snprintf (name, sizeof name, "%s : SSL_write de la requete", mode);
		check (r == n, name);
		long total = 0;
		int first = 1;
		for (;;)
		{
			r = SSL_read (s, buf, sizeof buf - 1);
			if (r <= 0)
			{
				if (wait_io (s, r))
					continue;
				break;
			}
			if (first)
			{
				buf[r] = 0;
				char *e = strstr (buf, "\r\n");
				if (e != NULL)
					*e = 0;
				printf ("      %s\n", buf);
				snprintf (name, sizeof name, "%s : SSL_read de la reponse", mode);
				check (strncmp (buf, "HTTP/1.", 7) == 0, name);
				first = 0;
			}
			total += r;
		}
		printf ("      %ld octets\n", total);
		snprintf (name, sizeof name, "%s : fin propre de la connexion", mode);
		check (total > 0 && SSL_get_error (s, r) == SSL_ERROR_ZERO_RETURN, name);
	}
	SSL_free (s);
}

static void group_net (const char *host, const char *port)
{
	char name[300];
	SSL_CTX *c = SSL_CTX_new (TLS_client_method ());
	check (c != NULL && SSL_CTX_set_default_verify_paths (c) == 1, "SSL_CTX_set_default_verify_paths");
	if (c == NULL)
		return;
	STACK_OF (X509_OBJECT) *objs = X509_STORE_get1_objects (SSL_CTX_get_cert_store (c));
	snprintf (name, sizeof name, "/etc/ca-bundle : %d certificats racine", objs ? sk_X509_OBJECT_num (objs) : 0);
	check (objs != NULL && sk_X509_OBJECT_num (objs) > 100, name);
	sk_X509_OBJECT_pop_free (objs, X509_OBJECT_free);
	SSL_CTX_set_verify (c, SSL_VERIFY_PEER, NULL);
	https_get (c, host, port, 0);
	https_get (c, host, port, 1);
	SSL_CTX_free (c);
}

/* ---- debits ---- */

static double now_s (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void speed_digest (const char *alg)
{
	static unsigned char buf[65536], md[EVP_MAX_MD_SIZE];
	double t = now_s (), d;
	size_t n, mb = 0;
	do
	{
		n = sizeof md;
		EVP_Q_digest (NULL, alg, NULL, buf, sizeof buf, md, &n);
		mb++;
	}
	while ((d = now_s () - t) < 1.0);
	printf ("%-20s %8.1f Mo/s\n", alg, mb * (double) sizeof buf / d / 1e6);
}

static void speed_cipher (const char *alg)
{
	static unsigned char buf[65536], out[65536 + 32], key[32], iv[16];
	int n;
	EVP_CIPHER *c = EVP_CIPHER_fetch (NULL, alg, NULL);
	EVP_CIPHER_CTX *x = EVP_CIPHER_CTX_new ();
	double t = now_s (), d;
	size_t mb = 0;
	do
	{
		EVP_EncryptInit_ex2 (x, c, key, iv, NULL);
		EVP_EncryptUpdate (x, out, &n, buf, sizeof buf);
		mb++;
	}
	while ((d = now_s () - t) < 1.0);
	printf ("%-20s %8.1f Mo/s\n", alg, mb * (double) sizeof buf / d / 1e6);
	EVP_CIPHER_CTX_free (x);
	EVP_CIPHER_free (c);
}

static void group_speed (void)
{
	speed_digest ("SHA256");
	speed_digest ("SHA512");
	speed_cipher ("AES-128-GCM");
	speed_cipher ("AES-256-CBC");
	speed_cipher ("CHACHA20-POLY1305");

	EVP_PKEY *key;
	X509 *cert = make_cert (&key);
	SSL_CTX *sc = server_ctx (cert, key, TLS1_3_VERSION), *cc = client_ctx (cert, TLS1_3_VERSION);
	double t = now_s (), d;
	int n = 0;
	do
	{
		SSL *cli, *srv;
		make_pair (cc, sc, "armpi.test", &cli, &srv);
		handshake (cli, srv);
		SSL_free (cli);
		SSL_free (srv);
		n++;
	}
	while ((d = now_s () - t) < 2.0);
	printf ("%-20s %8.1f /s\n", "poignee TLS 1.3", n / d);
}

int main (int argc, char **argv)
{
	const char *g = argc > 1 ? argv[1] : "crypto";
	double t = now_s ();
	if (strcmp (g, "crypto") == 0)
		group_crypto ();
	else if (strcmp (g, "tls") == 0)
		group_tls ();
	else if (strcmp (g, "threads") == 0)
		group_threads ();
	else if (strcmp (g, "net") == 0)
		group_net (argc > 2 ? argv[2] : "example.com", argc > 3 ? argv[3] : "443");
	else if (strcmp (g, "speed") == 0)
		group_speed ();
	else
	{
		printf ("openssltest crypto | tls | threads | net [hote [port]] | speed\n");
		return 2;
	}
	printf ("%s : %d reussis, %d echecs, %.2f s\n", g, s_ok, s_bad, now_s () - t);
	return s_bad != 0;
}
