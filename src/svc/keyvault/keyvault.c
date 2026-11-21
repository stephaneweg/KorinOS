/* keyvault - le coffre à secrets de Korin (docs/KEYVAULT.md, étude docs/etude/COFFRE.md). Service « keyvault »,
 * lancé par init2 en uid 0. Un programme range un secret sous un nom ; le coffre le range sous (compte, identité
 * du programme, nom), et ne le rend qu'au même programme du même compte.
 *
 * Identité d'un programme (SYS_PROC_EXE) : le chemin de son image, vérifié en comparant l'empreinte SHA-256 de
 * l'image chargée (calculée par le noyau) à celle du fichier ; pour un interprète de /etc/keyvault/interpreters.ini,
 * le chemin du script qu'il a déclaré (SYS_PROC_SET_SCRIPT). Une app de /apps peut partager ses secrets entre ses
 * programmes (« vault = folder » dans son app.txt : l'identité est alors le dossier de l'app). Hors des dossiers du
 * système, le coffre retient l'empreinte du fichier : s'il change (nouvelle compilation, ou un autre programme l'a
 * remplacé), une carte demande à l'utilisateur s'il garde ses secrets.
 *
 * Fichiers :
 *   /var/keyvault/<uid>.db        le coffre d'un compte (uid 0 : celui des services)
 *   /etc/keyvault/machine.key     la clé de la machine, sans TPM (32 octets, faite au premier usage)
 *   /etc/keyvault/machine.tpm     la clé de la machine scellée dans le TPM 2.0 (tpm.c), s'il y en a un
 *   /etc/keyvault/interpreters.ini   les interprètes dont le coffre croit le script déclaré
 *
 * Clés : chaque compte a une clé maîtresse (32 octets au hasard) ; elle est rangée enveloppée (AES-256-GCM) par la
 * clé du compte, KEK = HKDF-SHA256(Argon2id(mot de passe, sel) || clé de la machine). Il faut le mot de passe et la
 * machine (son TPM) : le disque seul ne donne rien, la machine seule non plus. Le coffre des services (uid 0) n'a
 * que la clé de la machine. Chaque secret est chiffré à part (AES-256-GCM, données associées : compte, identité,
 * nom) : un secret recopié sous un autre nom ou un autre compte ne se lit pas.
 *
 * Ouverture : à la connexion (authd lui passe le mot de passe vérifié, KV_UNLOCK) ; un compte sans mot de passe
 * s'ouvre au premier usage ; sinon, à la première demande, la carte du mot de passe (/sys/authcard en mode
 * coffre) s'ouvre, et les demandes attendent sa réponse. Sans clé de secours (décision du 18/11) : TPM effacé ou
 * mot de passe remis par un administrateur alors que le coffre est fermé, les secrets du compte sont perdus. */
#include <dirent.h>
#include <errno.h>
#include <spawn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <armpi/syscall.h>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include "../../lib/keyvault/keyvault.h"
#include "tpm.h"

typedef int64_t i64;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

/* ---- Korin : appels système, message (src/inc/syscalls.inc) */
#define SYS_SLEEP 0x0B
#define SYS_TIME_MS 0x19
#define SYS_SVC_REGISTER 0x30
#define SYS_SVC_NEXT 0x33
#define SYS_SVC_REPLY 0x34
#define SYS_SVC_MAP_CALLER 0x36
#define SYS_KILL 0x0D
#define SYS_PROC_UID 0x92
#define SYS_PROC_EXE 0x94
#define SVF_ENDS 1
#define SVC_OP_END 0xFFFF
#define SVC_OP_BIRTH 0xFFFE
#define SVC_OP_FORK 0xFFFC
#define MC_WRITE 1
#define PEXF_IMAGE 1
#define PEXF_RIGHTS 2
#define PEXF_SCRIPT 4

#define E_PERM -1
#define E_NOENT -2
#define E_IO -5
#define E_AGAIN -11
#define E_NOMEM -12
#define E_ACCES -13
#define E_INVAL -22
#define E_NOSPC -28
#define E_RANGE -34
#define E_CANCELED -125

#define DATA_MAX 464
#define VAULT_DIR "/var/keyvault"
#define ETC_DIR "/etc/keyvault"
#define MACHINE_KEY ETC_DIR "/machine.key"
#define INTERP_INI ETC_DIR "/interpreters.ini"
#define USERS_INI "/etc/users.ini"
#define SHADOW_INI "/etc/shadow.ini"
#define MAGIC "KORINKV1"
#define VAULTS 16
#define ERR_MAX 32
#define WAIT_MAX 16
#define WAIT_MS 20000
#define HASHES 64
#define REC_ID 1	/* un secret */
#define REC_PIN 2	/* l'empreinte retenue d'un programme (nom vide) */
/* Argon2id (RFC 9106, second choix recommandé : 64 Mio, 3 passes) ; gardés dans chaque coffre */
#define ARGON_MKIB 65536
#define ARGON_T 3
#define ARGON_LANES 1

struct msg {
	i64 op;
	i64 a, b, c, d;
	u32 len;
	u32 pid;
	u8 data[DATA_MAX];
};
_Static_assert(sizeof(struct msg) == 512, "message de 512 octets");

struct pex {
	u32 flags, uid;
	u8 hash[32];
	char exe[128];
	char script[128];
};
_Static_assert(sizeof(struct pex) == 296, "PEX_SIZE");

struct rec {
	int kind;
	char *id, *name;
	u8 *val;
	u32 len;
	struct rec *next;
};

struct vault {
	int used, uid, open;
	u8 mk[32];
	struct rec *recs;
};

static struct vault vaults[VAULTS];
static struct msg msg __attribute__((aligned(16)));
static long canal, req_cour;
static u32 pid_cour;
static struct {
	u32 pid;
	char text[256];
} errs[ERR_MAX];

/* la carte en cours (une à la fois) et les demandes qui l'attendent */
#define CARD_UNLOCK 1
#define CARD_APPROVE 2
static struct {
	int active, mode, uid;
	long card;
	u64 token, since;
	char id[160];
	u8 hash[32];
} card;
static long card_last; /* la réponse de la dernière carte (E_CANCELED : les demandes sont refusées) */
static struct {
	long req;
	u64 since;
	struct msg m;
} waits[WAIT_MAX];

static long ksys(long n, long a, long b, long c, long d)
{
	return armpi_syscall(n, a, b, c, d, 0, 0);
}

static u64 now_ms(void)
{
	return (u64)ksys(SYS_TIME_MS, 0, 0, 0, 0);
}

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("keyvault: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/* la dernière erreur de l'appelant (kv_error) ; rend r */
static long fail(long r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static long fail(long r, const char *fmt, ...)
{
	int k = -1;
	for (int i = 0; i < ERR_MAX && k < 0; i++)
		if (errs[i].pid == pid_cour)
			k = i;
	for (int i = 0; i < ERR_MAX && k < 0; i++)
		if (!errs[i].pid)
			k = i;
	if (k < 0)
		k = (int)(pid_cour % ERR_MAX);
	errs[k].pid = pid_cour;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(errs[k].text, sizeof errs[k].text, fmt, ap);
	va_end(ap);
	return r;
}

/* ---- tampons */

struct buf {
	u8 *p;
	size_t n, cap;
};

static int b_add(struct buf *b, const void *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		size_t c = b->cap ? b->cap : 256;
		while (c < b->n + n + 1)
			c *= 2;
		u8 *q = realloc(b->p, c);
		if (!q)
			return -1;
		b->p = q;
		b->cap = c;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
	return 0;
}

static void b_free(struct buf *b)
{
	if (b->p)
		OPENSSL_cleanse(b->p, b->cap);
	free(b->p);
	b->p = 0;
	b->n = b->cap = 0;
}

static int b_u32(struct buf *b, u32 v)
{
	u8 t[4] = {(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)};
	return b_add(b, t, 4);
}

static u32 g_u32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* ---- fichiers */

static u8 *file_read(const char *path, size_t *len)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	struct buf b = {0};
	u8 t[4096];
	ssize_t k;
	while ((k = read(fd, t, sizeof t)) > 0)
		if (b_add(&b, t, (size_t)k)) {
			close(fd);
			b_free(&b);
			return 0;
		}
	close(fd);
	OPENSSL_cleanse(t, sizeof t);
	if (k < 0) {
		b_free(&b);
		return 0;
	}
	if (!b.p)
		b_add(&b, "", 0);
	*len = b.n;
	return b.p;
}

/* écrit à côté puis renomme : jamais à moitié écrit */
static int file_save(const char *path, const void *p, size_t n)
{
	char tmp[160];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return -1;
	int ok = write(fd, p, n) == (ssize_t)n;
	ok = close(fd) == 0 && ok;
	if (!ok) {
		unlink(tmp);
		return -1;
	}
	unlink(path);
	return rename(tmp, path);
}

/* ---- crypto */

static int gcm(int enc, const u8 *k, const u8 *iv, const u8 *aad, int an, const u8 *in, int n, u8 *out, u8 *tag)
{
	EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
	if (!c)
		return 0;
	int l = 0;
	int ok = EVP_CipherInit_ex(c, EVP_aes_256_gcm(), 0, 0, 0, enc) == 1 &&
		 EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, 12, 0) == 1 && EVP_CipherInit_ex(c, 0, 0, k, iv, enc) == 1 &&
		 (an == 0 || EVP_CipherUpdate(c, 0, &l, aad, an) == 1) &&
		 (n == 0 || EVP_CipherUpdate(c, out, &l, in, n) == 1);
	if (ok && !enc)
		ok = EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag) == 1;
	ok = ok && EVP_CipherFinal_ex(c, out + (n ? l : 0), &l) == 1;
	if (ok && enc)
		ok = EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;
	EVP_CIPHER_CTX_free(c);
	return ok;
}

static int argon2id(const char *pw, const u8 *salt, u32 mkib, u32 t, u32 lanes, u8 out[32])
{
	EVP_KDF *kdf = EVP_KDF_fetch(0, "ARGON2ID", 0);
	if (!kdf)
		return 0;
	EVP_KDF_CTX *c = EVP_KDF_CTX_new(kdf);
	EVP_KDF_free(kdf);
	if (!c)
		return 0;
	/* (préfixe : Argon2 d'OpenSSL refuse un mot de passe vide, un compte peut ne pas en avoir) */
	char p[200];
	int pn = snprintf(p, sizeof p, "korin:%s", pw);
	if (pn < 0 || pn >= (int)sizeof p)
		pn = (int)sizeof p - 1;
	u32 threads = 1;
	OSSL_PARAM prm[] = {
		OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, p, (size_t)pn),
		OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (void *)salt, 16),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &t),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &mkib),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads),
		OSSL_PARAM_construct_end(),
	};
	int ok = EVP_KDF_derive(c, out, 32, prm) == 1;
	EVP_KDF_CTX_free(c);
	OPENSSL_cleanse(p, sizeof p);
	return ok;
}

static int hkdf(const u8 *ikm, size_t n, const char *info, u8 out[32])
{
	EVP_KDF *kdf = EVP_KDF_fetch(0, "HKDF", 0);
	if (!kdf)
		return 0;
	EVP_KDF_CTX *c = EVP_KDF_CTX_new(kdf);
	EVP_KDF_free(kdf);
	if (!c)
		return 0;
	OSSL_PARAM prm[] = {
		OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, (char *)"SHA256", 0),
		OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (void *)ikm, n),
		OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, (void *)info, strlen(info)),
		OSSL_PARAM_construct_end(),
	};
	int ok = EVP_KDF_derive(c, out, 32, prm) == 1;
	EVP_KDF_CTX_free(c);
	return ok;
}

/* ---- la clé de la machine : scellée dans le TPM s'il y en a un, sinon un fichier lisible par l'uid 0 seul */

static u8 machine[32];
static int machine_ok;

static int machine_key(u8 out[32])
{
	if (!machine_ok) {
		int r = tpm_machine_key(ETC_DIR, machine); /* 1 : du TPM ; 0 : pas de TPM ; < 0 : TPM qui refuse */
		if (r < 0) {
			say("le TPM refuse la clé de la machine (%d) : coffres fermés", r);
			return 0;
		}
		if (r == 0) {
			size_t n = 0;
			u8 *k = file_read(MACHINE_KEY, &n);
			if (k && n == 32)
				memcpy(machine, k, 32);
			else {
				if (k) {
					say("%s abîmée (%zu octets) : coffres fermés", MACHINE_KEY, n);
					free(k);
					return 0;
				}
				mkdir(ETC_DIR, 0700);
				if (RAND_bytes(machine, 32) != 1 || file_save(MACHINE_KEY, machine, 32)) {
					say("%s : écriture impossible", MACHINE_KEY);
					return 0;
				}
				say("clé de la machine créée (%s, sans TPM)", MACHINE_KEY);
			}
			if (k) {
				OPENSSL_cleanse(k, n);
				free(k);
			}
		}
		machine_ok = 1;
	}
	memcpy(out, machine, 32);
	return 1;
}

/* KEK du compte : HKDF(Argon2id(mot de passe) || machine) ; uid 0 : HKDF(machine) */
static int kek_of(int uid, const char *pw, const u8 *salt, u32 mkib, u32 t, u32 lanes, u8 out[32])
{
	u8 ikm[64];
	if (!machine_key(ikm + 32))
		return 0;
	int ok;
	if (uid == 0)
		ok = hkdf(ikm + 32, 32, "korin keyvault system v1", out);
	else
		ok = argon2id(pw, salt, mkib, t, lanes, ikm) && hkdf(ikm, 64, "korin keyvault user v1", out);
	OPENSSL_cleanse(ikm, sizeof ikm);
	return ok;
}

/* ---- comptes (/etc/users.ini, /etc/shadow.ini) */

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
	return s;
}

/* le nom d'un uid (1), ou l'uid d'un nom (name non vide) ; -1 : inconnu */
static int account(int uid, char *name, size_t n)
{
	if (uid == 0 && !name[0]) {
		snprintf(name, n, "system");
		return 0;
	}
	FILE *f = fopen(USERS_INI, "r");
	if (!f)
		return -1;
	char line[256], sect[64] = "";
	int found = -1;
	while (found < 0 && fgets(line, sizeof line, f)) {
		char *l = trim(line);
		if (*l == '[') {
			char *e = strchr(l, ']');
			if (e)
				*e = 0;
			snprintf(sect, sizeof sect, "%s", l + 1);
			continue;
		}
		char *eq = strchr(l, '=');
		if (!eq)
			continue;
		*eq = 0;
		if (strcmp(trim(l), "uid"))
			continue;
		int u = atoi(trim(eq + 1));
		if (name[0] ? !strcmp(sect, name) : u == uid) {
			found = u;
			if (!name[0])
				snprintf(name, n, "%s", sect);
		}
	}
	fclose(f);
	return found;
}

static int has_password(const char *name)
{
	FILE *f = fopen(SHADOW_INI, "r");
	if (!f)
		return 0;
	char line[512];
	int r = 0;
	while (fgets(line, sizeof line, f)) {
		char *l = trim(line);
		char *eq = strchr(l, '=');
		if (*l == '#' || !eq)
			continue;
		*eq = 0;
		if (!strcmp(trim(l), name))
			r = *trim(eq + 1) != 0;
	}
	fclose(f);
	OPENSSL_cleanse(line, sizeof line);
	return r;
}

/* ---- les coffres */

static struct vault *vault_of(int uid)
{
	struct vault *free_v = 0;
	for (int i = 0; i < VAULTS; i++) {
		if (vaults[i].used && vaults[i].uid == uid)
			return &vaults[i];
		if (!vaults[i].used && !free_v)
			free_v = &vaults[i];
	}
	if (!free_v) /* (tout est pris : un coffre fermé cède sa place) */
		for (int i = 0; i < VAULTS && !free_v; i++)
			if (!vaults[i].open)
				free_v = &vaults[i];
	if (!free_v)
		return 0;
	memset(free_v, 0, sizeof *free_v);
	free_v->used = 1;
	free_v->uid = uid;
	return free_v;
}

static void path_of(int uid, char *p, size_t n)
{
	snprintf(p, n, VAULT_DIR "/%d.db", uid);
}

static void recs_free(struct rec *r)
{
	while (r) {
		struct rec *n = r->next;
		if (r->val)
			OPENSSL_cleanse(r->val, r->len);
		free(r->val);
		free(r->id);
		free(r->name);
		free(r);
		r = n;
	}
}

static void vault_close(struct vault *v)
{
	recs_free(v->recs);
	v->recs = 0;
	OPENSSL_cleanse(v->mk, sizeof v->mk);
	v->open = 0;
}

static int exists(int uid)
{
	char p[64];
	path_of(uid, p, sizeof p);
	struct stat st;
	return stat(p, &st) == 0;
}

/* AAD d'un enregistrement : « uid\0id\0nom\0 » + sorte */
static int aad_of(int uid, int kind, const char *id, const char *name, u8 *out, size_t cap)
{
	int n = snprintf((char *)out, cap, "%d", uid) + 1;
	size_t a = strlen(id) + 1, b = strlen(name) + 1;
	if ((size_t)n + a + b + 1 > cap)
		return -1;
	memcpy(out + n, id, a);
	memcpy(out + n + a, name, b);
	out[n + a + b] = (u8)kind;
	return (int)(n + a + b + 1);
}

/* En-tête : MAGIC, version, mkib, t, lanes, sel (16), enveloppe : IV (12), marque (16), clé chiffrée (32) ;
 * puis le nombre d'enregistrements et, pour chacun : sorte (u32), id (u32 + octets), nom (u32 + octets),
 * longueur (u32), IV (12), marque (16), valeur chiffrée. */
struct header {
	u32 mkib, t, lanes;
	u8 salt[16], iv[12], tag[16], wmk[32];
};

static int vault_save(struct vault *v, const struct header *h)
{
	struct buf b = {0};
	int ok = !b_add(&b, MAGIC, 8) && !b_u32(&b, 1) && !b_u32(&b, h->mkib) && !b_u32(&b, h->t) && !b_u32(&b, h->lanes) &&
		 !b_add(&b, h->salt, 16) && !b_add(&b, h->iv, 12) && !b_add(&b, h->tag, 16) && !b_add(&b, h->wmk, 32);
	u32 count = 0;
	for (struct rec *r = v->recs; r; r = r->next)
		count++;
	ok = ok && !b_u32(&b, count);
	for (struct rec *r = v->recs; ok && r; r = r->next) {
		u8 iv[12], tag[16], aad[512];
		u8 *ct = malloc(r->len + 1);
		int an = aad_of(v->uid, r->kind, r->id, r->name, aad, sizeof aad);
		ok = ct && an > 0 && RAND_bytes(iv, 12) == 1 && gcm(1, v->mk, iv, aad, an, r->val, (int)r->len, ct, tag) &&
		     !b_u32(&b, (u32)r->kind) && !b_u32(&b, (u32)strlen(r->id)) && !b_add(&b, r->id, strlen(r->id)) &&
		     !b_u32(&b, (u32)strlen(r->name)) && !b_add(&b, r->name, strlen(r->name)) && !b_u32(&b, r->len) &&
		     !b_add(&b, iv, 12) && !b_add(&b, tag, 16) && !b_add(&b, ct, r->len);
		free(ct);
	}
	char p[64];
	path_of(v->uid, p, sizeof p);
	mkdir("/var", 0755);
	mkdir(VAULT_DIR, 0700);
	ok = ok && file_save(p, b.p, b.n) == 0;
	b_free(&b);
	return ok;
}

static int header_read(const u8 *f, size_t n, struct header *h)
{
	if (n < 8 + 16 + 16 + 12 + 16 + 32 + 4 || memcmp(f, MAGIC, 8) || g_u32(f + 8) != 1)
		return 0;
	h->mkib = g_u32(f + 12);
	h->t = g_u32(f + 16);
	h->lanes = g_u32(f + 20);
	memcpy(h->salt, f + 24, 16);
	memcpy(h->iv, f + 40, 12);
	memcpy(h->tag, f + 52, 16);
	memcpy(h->wmk, f + 68, 32);
	return 1;
}
#define HDR_SIZE 100

static int wrap_mk(int uid, const u8 *mk, const char *pw, struct header *h)
{
	u8 kek[32], aad[16];
	int an = snprintf((char *)aad, sizeof aad, "KV%d", uid);
	int ok = RAND_bytes(h->salt, 16) == 1 && RAND_bytes(h->iv, 12) == 1 &&
		 kek_of(uid, pw, h->salt, h->mkib, h->t, h->lanes, kek) && gcm(1, kek, h->iv, aad, an, mk, 32, h->wmk, h->tag);
	OPENSSL_cleanse(kek, sizeof kek);
	return ok;
}

/* le fichier du coffre -> son en-tête (pour le réenvelopper sans rien déchiffrer d'autre) */
static int header_of(int uid, struct header *h)
{
	char p[64];
	size_t n = 0;
	path_of(uid, p, sizeof p);
	u8 *f = file_read(p, &n);
	int ok = f && header_read(f, n, h);
	free(f);
	return ok;
}

/* ouvre le coffre avec ce mot de passe ; absent : il naît -> 0 / E_ACCES / E_IO */
static long vault_open(struct vault *v, const char *pw)
{
	char p[64];
	path_of(v->uid, p, sizeof p);
	size_t n = 0;
	u8 *f = file_read(p, &n);
	if (!f) {
		struct header h = {ARGON_MKIB, ARGON_T, ARGON_LANES, {0}, {0}, {0}, {0}};
		vault_close(v);
		if (RAND_bytes(v->mk, 32) != 1 || !wrap_mk(v->uid, v->mk, pw, &h) || !vault_save(v, &h)) {
			OPENSSL_cleanse(v->mk, 32);
			return fail(E_IO, "coffre du compte %d : création impossible", v->uid);
		}
		v->open = 1;
		say("coffre du compte %d créé", v->uid);
		return 0;
	}
	struct header h;
	if (!header_read(f, n, &h)) {
		free(f);
		return fail(E_IO, "%s : format inconnu", p);
	}
	u8 kek[32], mk[32], aad[16];
	int an = snprintf((char *)aad, sizeof aad, "KV%d", v->uid);
	if (!kek_of(v->uid, pw, h.salt, h.mkib, h.t, h.lanes, kek) || !gcm(0, kek, h.iv, aad, an, h.wmk, 32, mk, h.tag)) {
		OPENSSL_cleanse(kek, 32);
		free(f);
		return E_ACCES;
	}
	OPENSSL_cleanse(kek, 32);
	vault_close(v);
	memcpy(v->mk, mk, 32);
	OPENSSL_cleanse(mk, 32);
	/* les enregistrements */
	size_t o = HDR_SIZE;
	u32 count = o + 4 <= n ? g_u32(f + o) : 0;
	o += 4;
	struct rec **tail = &v->recs;
	int bad = 0;
	for (u32 i = 0; i < count && !bad; i++) {
		u32 kind, il, nl, vl;
		if (o + 8 > n)
			break;
		kind = g_u32(f + o);
		il = g_u32(f + o + 4);
		o += 8;
		if (il > 4096 || o + il + 4 > n)
			break;
		const u8 *id = f + o;
		o += il;
		nl = g_u32(f + o);
		o += 4;
		if (nl > 4096 || o + nl + 4 > n)
			break;
		const u8 *nm = f + o;
		o += nl;
		vl = g_u32(f + o);
		o += 4;
		if (vl > KV_VALUE_MAX || o + 28 + vl > n)
			break;
		struct rec *r = calloc(1, sizeof *r);
		if (!r)
			break;
		r->kind = (int)kind;
		r->id = strndup((const char *)id, il);
		r->name = strndup((const char *)nm, nl);
		r->val = malloc(vl + 1);
		r->len = vl;
		u8 aadr[512], tag[16];
		memcpy(tag, f + o + 12, 16);
		int arn = r->id && r->name ? aad_of(v->uid, r->kind, r->id, r->name, aadr, sizeof aadr) : -1;
		if (!r->id || !r->name || !r->val || arn < 0 ||
		    !gcm(0, v->mk, f + o, aadr, arn, f + o + 28, (int)vl, r->val, tag)) {
			say("coffre du compte %d : enregistrement %u illisible, ignoré", v->uid, i);
			recs_free(r);
		} else {
			*tail = r;
			tail = &r->next;
		}
		o += 28 + vl;
	}
	OPENSSL_cleanse(f, n);
	free(f);
	v->open = 1;
	return 0;
}

static struct rec *rec_find(struct vault *v, int kind, const char *id, const char *name)
{
	for (struct rec *r = v->recs; r; r = r->next)
		if (r->kind == kind && !strcmp(r->id, id) && !strcmp(r->name, name))
			return r;
	return 0;
}

static int rec_put(struct vault *v, int kind, const char *id, const char *name, const void *val, u32 len)
{
	struct rec *r = rec_find(v, kind, id, name);
	u8 *nv = malloc(len + 1);
	if (!nv)
		return 0;
	memcpy(nv, val, len);
	if (r) {
		OPENSSL_cleanse(r->val, r->len);
		free(r->val);
	} else {
		r = calloc(1, sizeof *r);
		if (!r || !(r->id = strdup(id)) || !(r->name = strdup(name))) {
			if (r) {
				free(r->id);
				free(r);
			}
			free(nv);
			return 0;
		}
		r->kind = kind;
		r->next = v->recs;
		v->recs = r;
	}
	r->val = nv;
	r->len = len;
	return 1;
}

static void rec_del(struct vault *v, struct rec *x)
{
	for (struct rec **p = &v->recs; *p; p = &(*p)->next)
		if (*p == x) {
			*p = x->next;
			x->next = 0;
			recs_free(x);
			return;
		}
}

static long vault_commit(struct vault *v)
{
	struct header h;
	if (!header_of(v->uid, &h))
		return fail(E_IO, "coffre du compte %d : en-tête illisible", v->uid);
	return vault_save(v, &h) ? 0 : fail(E_IO, "coffre du compte %d : écriture impossible", v->uid);
}

/* ---- identité des programmes */

static struct {
	char path[160];
	time_t mtime;
	off_t size;
	u8 hash[32];
} hcache[HASHES];
static int hnext;

static int file_sha256(const char *path, u8 out[32])
{
	struct stat st;
	if (stat(path, &st))
		return 0;
	for (int i = 0; i < HASHES; i++)
		if (hcache[i].path[0] && !strcmp(hcache[i].path, path) && hcache[i].mtime == st.st_mtime &&
		    hcache[i].size == st.st_size) {
			memcpy(out, hcache[i].hash, 32);
			return 1;
		}
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	EVP_MD_CTX *c = EVP_MD_CTX_new();
	int ok = c && EVP_DigestInit_ex(c, EVP_sha256(), 0) == 1;
	static u8 t[65536];
	ssize_t k;
	while (ok && (k = read(fd, t, sizeof t)) > 0)
		ok = EVP_DigestUpdate(c, t, (size_t)k) == 1;
	close(fd);
	unsigned l = 0;
	ok = ok && EVP_DigestFinal_ex(c, out, &l) == 1 && l == 32;
	EVP_MD_CTX_free(c);
	if (ok) {
		int i = hnext++ % HASHES;
		snprintf(hcache[i].path, sizeof hcache[i].path, "%s", path);
		hcache[i].mtime = st.st_mtime;
		hcache[i].size = st.st_size;
		memcpy(hcache[i].hash, out, 32);
	}
	return ok;
}

/* « SD0:/apps/x » ou « /apps/x » -> « /apps/x » (le chemin de l'arbre unique) */
static const char *unix_path(const char *p)
{
	const char *c = strchr(p, ':');
	if (c && c[1] == '/' && c - p < 8)
		return c + 1;
	return p;
}

static int interpreter(const char *exe)
{
	FILE *f = fopen(INTERP_INI, "r");
	if (!f)
		return 0;
	char line[256];
	int r = 0;
	while (!r && fgets(line, sizeof line, f)) {
		char *l = trim(line);
		if (*l && *l != '#' && *l != '[' && !strcmp(l, exe))
			r = 1;
	}
	fclose(f);
	return r;
}

/* les dossiers du système (seul le compte system y écrit, vfs en enforce) : pas d'empreinte retenue */
static int system_path(const char *p)
{
	static const char *const dirs[] = {"/apps/", "/bin/", "/sbin/", "/sys/", "/lib/", "/devtools/", 0};
	for (int i = 0; dirs[i]; i++)
		if (!strncmp(p, dirs[i], strlen(dirs[i])))
			return 1;
	return 0;
}

/* « vault = folder » dans l'app.txt du dossier de l'app (/apps/x.app/) -> l'identité devient ce dossier */
static void app_folder(char *id, size_t n)
{
	if (strncmp(id, "/apps/", 6))
		return;
	char *e = strstr(id + 6, ".app/");
	if (!e)
		return;
	char dir[160], txt[200];
	snprintf(dir, sizeof dir, "%.*s", (int)(e + 4 - id), id);
	snprintf(txt, sizeof txt, "%s/app.txt", dir);
	FILE *f = fopen(txt, "r");
	if (!f)
		return;
	char line[256];
	int share = 0;
	while (fgets(line, sizeof line, f)) {
		char *l = trim(line), *eq = strchr(l, '=');
		if (!eq)
			continue;
		*eq = 0;
		if (!strcmp(trim(l), "vault") && !strcmp(trim(eq + 1), "folder"))
			share = 1;
	}
	fclose(f);
	if (share)
		snprintf(id, n, "%s", dir);
}

struct ident {
	int uid;
	char id[160];	/* chemin de l'exécutable, du script ou du dossier de l'app */
	u8 hash[32];	/* empreinte du fichier (exécutable ou script) */
	int pin;	/* hors des dossiers du système : l'empreinte est retenue */
};

static long identify(long pid, struct ident *who)
{
	struct pex x;
	memset(&x, 0, sizeof x);
	long r = ksys(SYS_PROC_EXE, pid, (long)&x, 0, 0);
	if (r < 0)
		return r == -38 ? fail(E_PERM, "noyau sans SYS_PROC_EXE") : r;
	x.exe[sizeof x.exe - 1] = x.script[sizeof x.script - 1] = 0;
	if (!(x.flags & PEXF_IMAGE))
		return fail(E_PERM, "programme lancé sans image : identité inconnue");
	who->uid = (int)x.uid;
	const char *exe = unix_path(x.exe);
	if (!(x.flags & PEXF_RIGHTS)) { /* le chemin déclaré par le chargeur : vérifié sur le fichier */
		u8 h[32];
		if (!file_sha256(exe, h))
			return fail(E_PERM, "%s : fichier illisible, identité non vérifiée", exe);
		if (CRYPTO_memcmp(h, x.hash, 32))
			return fail(E_PERM, "%s ne correspond plus à son fichier (mis à jour ? relancez-le)", exe);
	}
	if (interpreter(exe)) {
		if (!(x.flags & PEXF_SCRIPT))
			return fail(E_PERM, "%s : interprète sans script, pas de coffre", exe);
		const char *s = unix_path(x.script);
		if (s[0] != '/')
			return fail(E_PERM, "%s : chemin de script relatif (%s)", exe, s);
		snprintf(who->id, sizeof who->id, "%s", s);
		if (!file_sha256(s, who->hash))
			memset(who->hash, 0, 32);
	} else {
		snprintf(who->id, sizeof who->id, "%s", exe);
		memcpy(who->hash, x.hash, 32);
	}
	who->pin = !system_path(who->id);
	app_folder(who->id, sizeof who->id);
	return 0;
}

/* ---- la carte (/sys/authcard en mode coffre) */

static long card_open(int mode, int uid, const char *account_name, const char *reason, const struct ident *who)
{
	if (card.active)
		return 0;
	u64 tok = 0;
	RAND_bytes((u8 *)&tok, sizeof tok);
	tok |= 1;
	static char args[512];
	char r2[300];
	snprintf(r2, sizeof r2, "%s", reason);
	for (char *p = r2; *p; p++)
		if (*p == '|')
			*p = '/';
	snprintf(args, sizeof args, "%llu|%s|%s|%d|keyvault", (unsigned long long)tok, r2, account_name,
		 mode == CARD_UNLOCK ? 1 : 0);
	pid_t pc = 0;
	char *argv[] = {(char *)"authcard", args, 0};
	if (posix_spawn(&pc, "/sys/authcard", 0, 0, argv, 0) != 0)
		return fail(E_NOENT, "carte du coffre : /sys/authcard introuvable");
	long c = pc;
	memset(&card, 0, sizeof card);
	card.active = 1;
	card.mode = mode;
	card.uid = uid;
	card.card = c;
	card.token = tok;
	card.since = now_ms();
	if (who) {
		snprintf(card.id, sizeof card.id, "%s", who->id);
		memcpy(card.hash, who->hash, 32);
	}
	return 0;
}

static int wait_add(void)
{
	for (int i = 0; i < WAIT_MAX; i++)
		if (!waits[i].req) {
			waits[i].req = req_cour;
			waits[i].since = now_ms();
			waits[i].m = msg;
			return 1;
		}
	return 0;
}

#define DEFER 1L << 40

/* le coffre du compte ouvert, sinon la carte : 0, DEFER (la demande attend), ou une erreur */
static long ensure_open(struct vault *v, const struct ident *who)
{
	if (v->open)
		return 0;
	char name[64] = "";
	if (v->uid == 0)
		return vault_open(v, "");
	if (account(v->uid, name, sizeof name) < 0)
		return fail(E_PERM, "compte %d inconnu (%s)", v->uid, USERS_INI);
	if (!has_password(name)) { /* (sans mot de passe : la machine seule) */
		long r = vault_open(v, "");
		if (r == E_ACCES)
			return fail(E_ACCES, "le coffre de %s ne s'ouvre pas sans mot de passe (mot de passe retiré ?)", name);
		return r;
	}
	if (card.active && card.uid != v->uid)
		return fail(E_AGAIN, "une autre carte du coffre est ouverte");
	char why[300];
	snprintf(why, sizeof why, "%s demande le coffre de %s, qui est fermé.", who ? who->id : "Un programme", name);
	long r = card_open(CARD_UNLOCK, v->uid, name, why, who);
	if (r < 0)
		return r;
	return wait_add() ? DEFER : fail(E_AGAIN, "trop de demandes en attente");
}

/* l'empreinte retenue du programme : la même, ou la carte d'accord */
static long check_pin(struct vault *v, const struct ident *who, int create)
{
	if (!who->pin)
		return 0;
	struct rec *p = rec_find(v, REC_PIN, who->id, "");
	if (!p) {
		if (create && !rec_put(v, REC_PIN, who->id, "", who->hash, 32))
			return E_NOMEM;
		return 0;
	}
	if (p->len == 32 && !CRYPTO_memcmp(p->val, who->hash, 32))
		return 0;
	if (card.active)
		return wait_add() ? DEFER : fail(E_AGAIN, "trop de demandes en attente");
	char name[64] = "", why[300];
	account(v->uid, name, sizeof name);
	snprintf(why, sizeof why, "%s a changé depuis qu'il a rangé ses secrets (nouvelle compilation ?). Lui laisser ses secrets ?",
		 who->id);
	long r = card_open(CARD_APPROVE, v->uid, name, why, who);
	if (r < 0)
		return r;
	return wait_add() ? DEFER : fail(E_AGAIN, "trop de demandes en attente");
}

/* ---- les opérations */

static int name_ok(const char *s)
{
	if (!*s || strlen(s) > KV_NAME_MAX)
		return 0;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || strchr("._-/@:", *s)))
			return 0;
	return 1;
}

static long out_to_caller(struct msg *m, const void *p, size_t n)
{
	if (m->d <= 0 || !m->c)
		return (long)n; /* (la taille seule) */
	if (n > (size_t)m->d)
		return fail(E_RANGE, "tampon trop petit (%zu octets demandés)", n);
	size_t w = n < (size_t)m->d ? n + 1 : n; /* (0 final si la place y est) */
	long va = ksys(SYS_SVC_MAP_CALLER, req_cour, m->c, (long)w, MC_WRITE);
	if (va < 0)
		return va;
	memcpy((void *)va, p, n);
	if (w > n)
		((char *)va)[n] = 0;
	return (long)n;
}

/* données : « a\0b\0c » -> les morceaux */
static int parts(struct msg *m, const char **p, int max)
{
	size_t len = m->len > DATA_MAX - 1 ? DATA_MAX - 1 : m->len;
	m->data[len] = 0;
	int k = 0;
	size_t o = 0;
	while (k < max && o <= len) {
		p[k++] = (const char *)m->data + o;
		o += strlen((const char *)m->data + o) + 1;
	}
	return k;
}

/* les opérations de service (authd, la carte, le compte lui-même) */
static long admin_op(struct msg *m, int caller_uid)
{
	const char *p[3] = {"", "", ""};
	int k = parts(m, p, 3);
	int uid = caller_uid;
	char name[64] = "";
	if (m->op == KV_CARD) {
		if (caller_uid != 0 || !card.active || (long)pid_cour != card.card || (u64)m->a != card.token)
			return E_PERM;
		struct vault *v = vault_of(card.uid);
		long res;
		if (m->b == 1)
			res = E_CANCELED;
		else if (card.mode == CARD_UNLOCK) {
			res = v ? vault_open(v, p[0]) : E_NOMEM;
			if (res == E_ACCES) {
				ksys(SYS_SLEEP, 1000, 0, 0, 0);
				return E_ACCES; /* (la carte reste) */
			}
		} else {
			res = E_NOMEM;
			if (v && v->open && rec_put(v, REC_PIN, card.id, "", card.hash, 32))
				res = vault_commit(v);
		}
		say("carte du coffre (compte %d) : %s", card.uid, res == 0 ? "accordée" : res == E_CANCELED ? "annulée" : "échec");
		card.active = 0;
		card.card = 0;
		card_last = res;
		return res == 0 ? 0 : res == E_CANCELED ? 0 : res;
	}
	if (m->op == KV_UNLOCK || m->op == KV_LOCK || m->op == KV_PASSWD || m->op == KV_SETPW) {
		if (caller_uid == 0 && k >= 1 && p[0][0]) { /* (authd : le compte nommé) */
			snprintf(name, sizeof name, "%s", p[0]);
			uid = account(0, name, sizeof name);
			if (uid < 0)
				return fail(E_NOENT, "compte inconnu : %s", name);
		} else if (m->op == KV_PASSWD || m->op == KV_SETPW)
			return E_PERM;
	}
	struct vault *v = vault_of(uid);
	if (!v)
		return fail(E_NOSPC, "trop de coffres ouverts");
	switch (m->op) {
	case KV_UNLOCK: {
		const char *pw = caller_uid == 0 && p[0][0] ? p[1] : p[0];
		long r = vault_open(v, pw);
		if (r == E_ACCES) {
			ksys(SYS_SLEEP, 1000, 0, 0, 0);
			return fail(E_ACCES, "mot de passe refusé par le coffre");
		}
		if (r == 0)
			say("coffre du compte %d ouvert", uid);
		return r;
	}
	case KV_LOCK:
		if (uid != 0)
			vault_close(v);
		return 0;
	case KV_PASSWD:
	case KV_SETPW: {
		if (!exists(uid))
			return 0; /* (pas de coffre : rien à réenvelopper) */
		const char *nw = m->op == KV_PASSWD ? p[2] : p[1];
		if (!v->open && m->op == KV_PASSWD) {
			long r = vault_open(v, p[1]);
			if (r < 0)
				return fail(r, "ancien mot de passe refusé par le coffre");
		}
		if (!v->open) { /* (mot de passe remis sans l'ancien, coffre fermé : sans clé de secours, il est vidé) */
			char path[64];
			path_of(uid, path, sizeof path);
			unlink(path);
			vault_close(v);
			say("coffre du compte %d vidé : mot de passe remis alors qu'il était fermé", uid);
			return 1;
		}
		struct header h;
		if (!header_of(uid, &h))
			return fail(E_IO, "coffre du compte %d : en-tête illisible", uid);
		if (!wrap_mk(uid, v->mk, nw, &h) || !vault_save(v, &h))
			return fail(E_IO, "coffre du compte %d : réenveloppe impossible", uid);
		say("coffre du compte %d : nouveau mot de passe", uid);
		return 0;
	}
	}
	return E_INVAL;
}

/* ---- le gestionnaire (Réglages › Coffre à secrets) */

#define MGR_GRACE_MS (5 * 60 * 1000)
#define GRANTS 8
static const char *const MANAGERS[] = {"/apps/control.app/main", "/apps/control.app", 0};
static struct {
	u32 pid;
	int uid;
	u64 until;
} grants[GRANTS];

static int granted(u32 pid, int uid)
{
	u64 now = now_ms();
	for (int i = 0; i < GRANTS; i++)
		if (grants[i].pid == pid && grants[i].uid == uid && grants[i].until > now)
			return 1;
	return 0;
}

static void grant(u32 pid, int uid)
{
	int k = 0;
	u64 now = now_ms();
	for (int i = 0; i < GRANTS; i++) {
		if (grants[i].pid == pid && grants[i].uid == uid) {
			k = i;
			break;
		}
		if (grants[i].until < grants[k].until || grants[i].until <= now)
			k = i;
	}
	grants[k].pid = pid;
	grants[k].uid = uid;
	grants[k].until = now + MGR_GRACE_MS;
}

/* le mot de passe du compte, vérifié sur l'enveloppe de son coffre (sans rien ouvrir) -> 0 / E_ACCES */
static long verify_pw(int uid, const char *pw)
{
	char name[64] = "";
	if (account(uid, name, sizeof name) < 0)
		return E_ACCES;
	if (!has_password(name))
		return 0; /* (compte sans mot de passe : la machine seule protège le coffre) */
	struct header h;
	if (!header_of(uid, &h))
		return E_ACCES;
	u8 kek[32], mk[32], aad[16];
	int an = snprintf((char *)aad, sizeof aad, "KV%d", uid);
	int ok = kek_of(uid, pw, h.salt, h.mkib, h.t, h.lanes, kek) && gcm(0, kek, h.iv, aad, an, h.wmk, 32, mk, h.tag);
	OPENSSL_cleanse(kek, sizeof kek);
	OPENSSL_cleanse(mk, sizeof mk);
	return ok ? 0 : E_ACCES;
}

static long mgr_op(struct msg *m, int caller_uid)
{
	struct ident who;
	memset(&who, 0, sizeof who);
	long r = identify(pid_cour, &who);
	if (r)
		return r;
	int ok = 0;
	for (int i = 0; MANAGERS[i]; i++)
		ok |= !strcmp(who.id, MANAGERS[i]);
	if (!ok)
		return fail(E_PERM, "gestionnaire du coffre : réservé à Réglages (%s)", who.id);
	who.uid = caller_uid;
	/* le coffre visé : celui du compte, ou (KVF_SYSTEM) celui du compte system, après l'élévation */
	int uid = caller_uid;
	if (m->b & KVF_SYSTEM) {
		if (caller_uid == 0)
			grant(pid_cour, 0);
		else if (!granted(pid_cour, 0))
			return fail(E_PERM, "secrets du système : il faut l'élévation");
		uid = 0;
	}
	struct vault *v = vault_of(uid);
	if (!v)
		return fail(E_NOSPC, "trop de coffres ouverts");
	if ((r = ensure_open(v, &who)))
		return r;
	if (m->op == KV_MGR_LIST) {
		struct buf b = {0};
		for (struct rec *x = v->recs; x; x = x->next) {
			char len[16];
			int ln = snprintf(len, sizeof len, "\t%u\n", x->len);
			if (x->kind == REC_ID && (b_add(&b, x->id, strlen(x->id)) || b_add(&b, "\t", 1) ||
						  b_add(&b, x->name, strlen(x->name)) || b_add(&b, len, (size_t)ln)))
				break;
		}
		r = out_to_caller(m, b.p ? b.p : (const u8 *)"", b.n);
		b_free(&b);
		return r;
	}
	const char *p[3] = {"", "", ""};
	parts(m, p, 3);
	if (uid != 0 && !granted(pid_cour, uid)) {
		if (!p[2][0])
			return fail(E_ACCES, "mot de passe demandé");
		if (verify_pw(uid, p[2])) {
			ksys(SYS_SLEEP, 1000, 0, 0, 0);
			OPENSSL_cleanse(m->data, sizeof m->data);
			return fail(E_ACCES, "mot de passe refusé");
		}
		grant(pid_cour, uid);
	}
	OPENSSL_cleanse(m->data + (p[2] - (const char *)m->data), strlen(p[2]));
	struct rec *x = rec_find(v, REC_ID, p[0], p[1]);
	if (!x)
		return fail(E_NOENT, "pas de secret « %s » pour %s", p[1], p[0]);
	if (m->op == KV_MGR_GET)
		return out_to_caller(m, x->val, x->len);
	say("gestionnaire : secret « %s » de %s effacé (compte %d)", p[1], p[0], uid);
	rec_del(v, x);
	return vault_commit(v);
}

static long handle(struct msg *m)
{
	long caller_uid = ksys(SYS_PROC_UID, pid_cour, 0, 0, 0);
	if (caller_uid < 0)
		return E_PERM;
	if (m->op == KV_ERROR) {
		for (int i = 0; i < ERR_MAX; i++)
			if (errs[i].pid == pid_cour)
				return out_to_caller(m, errs[i].text, strlen(errs[i].text));
		return out_to_caller(m, "", 0);
	}
	if (m->op >= KV_UNLOCK && m->op <= KV_CARD)
		return admin_op(m, (int)caller_uid);
	if (m->op >= KV_MGR_LIST && m->op <= KV_MGR_DELETE)
		return mgr_op(m, (int)caller_uid);
	struct ident who;
	memset(&who, 0, sizeof who);
	long pid = pid_cour;
	long r;
	if (m->b & KVF_PARENT) { /* /bin/secret, lancé par un script : a = son père */
		struct ident me;
		if ((r = identify(pid, &me)))
			return r;
		if (strcmp(me.id, "/bin/secret"))
			return fail(E_PERM, "KVF_PARENT : réservé à /bin/secret (%s)", me.id);
		pid = (long)m->a;
		if (ksys(SYS_PROC_UID, pid, 0, 0, 0) != caller_uid)
			return fail(E_PERM, "le père n'est pas du même compte");
	}
	if (m->b & KVF_SYSTEM) { /* l'espace partagé du compte system : ses programmes seulement */
		if (caller_uid != 0)
			return fail(E_PERM, "espace @system : réservé au compte system");
		who.uid = 0;
		snprintf(who.id, sizeof who.id, "@system");
		who.pin = 0;
	} else if ((r = identify(pid, &who)))
		return r;
	who.uid = (int)caller_uid;
	if (m->op == KV_WHOAMI) {
		char t[300];
		int n = snprintf(t, sizeof t, "uid %d\nid %s\nempreinte retenue %s\n", who.uid, who.id, who.pin ? "oui" : "non");
		return out_to_caller(m, t, (size_t)n);
	}
	struct vault *v = vault_of(who.uid);
	if (!v)
		return fail(E_NOSPC, "trop de coffres ouverts");
	if (m->op == KV_STATUS)
		return v->open ? KVS_OPEN : exists(who.uid) ? KVS_LOCKED : KVS_NONE;
	if ((r = ensure_open(v, &who)))
		return r;
	if ((r = check_pin(v, &who, m->op == KV_SET)))
		return r;
	if (m->op == KV_LIST) {
		struct buf b = {0};
		for (struct rec *x = v->recs; x; x = x->next)
			if (x->kind == REC_ID && !strcmp(x->id, who.id) && (b_add(&b, x->name, strlen(x->name)) || b_add(&b, "\n", 1)))
				break;
		r = out_to_caller(m, b.p ? b.p : (const u8 *)"", b.n);
		b_free(&b);
		return r;
	}
	size_t nl = strnlen((const char *)m->data, DATA_MAX - 1);
	char name[KV_NAME_MAX + 1];
	if (nl > KV_NAME_MAX)
		return fail(E_INVAL, "nom de secret trop long");
	memcpy(name, m->data, nl);
	name[nl] = 0;
	if (!name_ok(name))
		return fail(E_INVAL, "nom de secret invalide : « %s » (lettres, chiffres, . _ - / @ :)", name);
	struct rec *x = rec_find(v, REC_ID, who.id, name);
	switch (m->op) {
	case KV_GET:
		if (!x)
			return fail(E_NOENT, "pas de secret « %s » pour %s", name, who.id);
		return out_to_caller(m, x->val, x->len);
	case KV_DELETE:
		if (!x)
			return fail(E_NOENT, "pas de secret « %s » pour %s", name, who.id);
		rec_del(v, x);
		return vault_commit(v);
	case KV_SET: {
		if (m->d < 0 || m->d > KV_VALUE_MAX || (m->d && !m->c))
			return fail(E_INVAL, "valeur de %ld octets (64 Kio au plus)", (long)m->d);
		const void *val = "";
		if (m->d) {
			long va = ksys(SYS_SVC_MAP_CALLER, req_cour, m->c, m->d, 0);
			if (va < 0)
				return va;
			val = (const void *)va;
		}
		if (!rec_put(v, REC_ID, who.id, name, val, (u32)m->d))
			return E_NOMEM;
		return vault_commit(v);
	}
	}
	return E_INVAL;
}

/* la carte a répondu (ou est partie) : les demandes en attente sont reprises */
static void replay(void)
{
	for (int i = 0; i < WAIT_MAX; i++) {
		if (!waits[i].req)
			continue;
		long req = waits[i].req;
		waits[i].req = 0;
		msg = waits[i].m;
		req_cour = req;
		pid_cour = msg.pid;
		long r = handle(&msg);
		if (r == DEFER)
			continue; /* (une autre carte : la demande est de nouveau en attente) */
		memset(msg.data, 0, sizeof msg.data);
		msg.op = r;
		ksys(SYS_SVC_REPLY, req, (long)&msg, 0, 0);
	}
}

/* une demande qui attend depuis 20 s reçoit E_AGAIN (le client redemande) ; refusée, E_CANCELED */
static void expire(long r_all)
{
	u64 now = now_ms();
	for (int i = 0; i < WAIT_MAX; i++)
		if (waits[i].req && (r_all || now - waits[i].since > WAIT_MS)) {
			struct msg out;
			memset(&out, 0, sizeof out);
			out.op = r_all ? r_all : E_AGAIN;
			ksys(SYS_SVC_REPLY, waits[i].req, (long)&out, 0, 0);
			waits[i].req = 0;
		}
}

int main(void)
{
	canal = ksys(SYS_SVC_REGISTER, (long)KV_SERVICE, SVF_ENDS, 0, 0);
	if (canal < 0)
		return 0;
	mkdir("/var", 0755);
	mkdir(VAULT_DIR, 0700);
	for (;;) {
		int any = card.active;
		for (int i = 0; i < WAIT_MAX && !any; i++)
			any = waits[i].req != 0;
		long req = ksys(SYS_SVC_NEXT, canal, (long)&msg, any ? 1000 : 60000, 0);
		if (req < 0)
			return 1;
		expire(0);
		if (!req)
			continue;
		req_cour = req;
		u64 op = (u64)msg.op;
		if (op == SVC_OP_END) {
			for (int i = 0; i < ERR_MAX; i++)
				if (errs[i].pid == (u32)msg.a)
					errs[i].pid = 0;
			if (card.active && (long)msg.a == card.card) { /* carte fermée sans réponse : refus */
				card.active = 0;
				expire(E_CANCELED);
			}
			for (int i = 0; i < WAIT_MAX; i++) /* (un client parti n'attend plus) */
				if (waits[i].req && waits[i].m.pid == (u32)msg.a)
					waits[i].req = 0;
			continue;
		}
		if (op == SVC_OP_FORK || op == SVC_OP_BIRTH)
			continue;
		pid_cour = msg.pid;
		msg.data[DATA_MAX - 1] = 0;
		int was_card = card.active;
		long r = handle(&msg);
		if (r != DEFER) {
			OPENSSL_cleanse(msg.data, sizeof msg.data);
			msg.op = r;
			ksys(SYS_SVC_REPLY, req, (long)&msg, 0, 0);
		}
		if (was_card && !card.active) { /* la carte a répondu : la suite, ou le refus pour tous */
			if (card_last == 0)
				replay();
			else
				expire(card_last);
		}
	}
}
