/* tpm.c : la clé de la machine du coffre, scellée dans le TPM 2.0 du PC (docs/KEYVAULT.md, étape 4 de
 * docs/etude/COFFRE.md). Le coffre (keyvault, droits de /sys) parle seul au TPM :
 *
 *   - la table ACPI « TPM2 » dit où il est ; seule l'interface CRB (Command Response Buffer, méthode de démarrage
 *     7) est prise : celle des fTPM d'Intel (PTT, le ZBook) et d'AMD ; sans elle, pas de TPM (le coffre garde la
 *     clé en fichier, /etc/keyvault/machine.key) ;
 *   - une clé primaire de stockage (ECC P-256, modèle standard) sous la hiérarchie du propriétaire, refaite à chaque
 *     démarrage (même graine, même clé) ;
 *   - un objet scellé (« keyedhash ») qui contient les 32 octets de la clé de la machine ; sa partie publique et sa
 *     partie privée (chiffrée par le TPM, inutile ailleurs) sont gardées dans /etc/keyvault/machine.tpm ;
 *   - Unseal la rend, à chaque démarrage, au coffre seul.
 *
 * Pas de lien aux PCR : Korin démarre sans Secure Boot (le PCR 7 est le même pour tout système démarré ainsi) et
 * le PCR 4 change à chaque mise à jour de korin.efi. Le TPM protège donc du disque emporté seul ; c'est le mot de
 * passe du compte qui protège de la machine emportée. Un TPM effacé (réglage du BIOS, carte mère changée) perd la
 * clé : les coffres ne s'ouvrent plus (pas de clé de secours, décision du 18/11).
 *
 * Passage d'une clé en fichier au TPM : au premier démarrage avec un TPM, la clé de machine.key est scellée telle
 * quelle (les coffres existants restent lisibles), puis le fichier est effacé.
 *
 * TPM_TCP (essai sous Linux, test/run.sh) : les commandes vont à swtpm par TCP (127.0.0.1:2321) au lieu du CRB. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include "tpm.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define TPM_ST_NO_SESSIONS 0x8001
#define TPM_ST_SESSIONS 0x8002
#define TPM_CC_CREATE_PRIMARY 0x131
#define TPM_CC_STARTUP 0x144
#define TPM_CC_CREATE 0x153
#define TPM_CC_LOAD 0x157
#define TPM_CC_UNSEAL 0x15E
#define TPM_CC_FLUSH 0x165
#define TPM_RH_OWNER 0x40000001
#define TPM_RS_PW 0x40000009
#define TPM_RC_INITIALIZE 0x100
#define TPM_ALG_AES 0x0006
#define TPM_ALG_KEYEDHASH 0x0008
#define TPM_ALG_SHA256 0x000B
#define TPM_ALG_NULL 0x0010
#define TPM_ALG_ECC 0x0023
#define TPM_ALG_CFB 0x0043
#define TPM_ECC_NIST_P256 0x0003
#define OA_FIXEDTPM (1u << 1)
#define OA_FIXEDPARENT (1u << 4)
#define OA_SENSITIVEDATAORIGIN (1u << 5)
#define OA_USERWITHAUTH (1u << 6)
#define OA_NODA (1u << 10)
#define OA_RESTRICTED (1u << 16)
#define OA_DECRYPT (1u << 17)
#define BUF 4096
#define MAGIC "KORINTPM1"

/* ---------------------------------------------------------------- le transport */

#ifdef TPM_TCP
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

static int tcp_fd = -1;

static int tpm_open(void)
{
	if (tcp_fd >= 0)
		return 1;
	const char *port = getenv("TPM_PORT");
	struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port ? (u16)atoi(port) : 2321)};
	a.sin_addr.s_addr = htonl(0x7F000001);
	tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_fd < 0 || connect(tcp_fd, (struct sockaddr *)&a, sizeof a)) {
		if (tcp_fd >= 0)
			close(tcp_fd);
		tcp_fd = -1;
		return 0;
	}
	return 1;
}

static long tpm_xfer(const u8 *cmd, size_t n, u8 *rsp, size_t cap)
{
	if (write(tcp_fd, cmd, n) != (ssize_t)n)
		return -1;
	size_t got = 0, want = 10;
	while (got < want) {
		ssize_t k = read(tcp_fd, rsp + got, cap - got);
		if (k <= 0)
			return -1;
		got += (size_t)k;
		if (got >= 6) {
			want = (size_t)rsp[2] << 24 | (size_t)rsp[3] << 16 | (size_t)rsp[4] << 8 | rsp[5];
			if (want > cap || want < 10)
				return -1;
		}
	}
	return (long)got;
}

#else /* Korin : CRB en MMIO */
#include <armpi/syscall.h>

#define SYS_DEV_MAP 0x40
#define SYS_ACPI_TABLE 0x4D
#define SYS_SLEEP 0x0B
#define SYS_TIME_MS 0x19

#define LOC_STATE 0x00
#define LOC_CTRL 0x08
#define LOC_STS 0x0C
#define CTRL_REQ 0x40
#define CTRL_STS 0x44
#define CTRL_START 0x4C
#define CTRL_CMD_SIZE 0x58
#define CTRL_CMD_LADDR 0x5C
#define CTRL_CMD_HADDR 0x60
#define CTRL_RSP_SIZE 0x64
#define CTRL_RSP_ADDR 0x68

static volatile u8 *crb;	/* les registres de la localité 0 */
static volatile u8 *cmd_buf, *rsp_buf;
static u32 cmd_size, rsp_size;
static int tried;

static u32 rd(u32 o)
{
	return *(volatile u32 *)(crb + o);
}

static void wr(u32 o, u32 v)
{
	*(volatile u32 *)(crb + o) = v;
}

static long ms(void)
{
	return armpi_syscall(SYS_TIME_MS, 0, 0, 0, 0, 0, 0);
}

/* attendre (reg & mask) == val, au plus t ms */
static int wait_reg(u32 reg, u32 mask, u32 val, long t)
{
	long end = ms() + t;
	while ((rd(reg) & mask) != val) {
		if (ms() > end)
			return 0;
		armpi_syscall(SYS_SLEEP, 1, 0, 0, 0, 0, 0);
	}
	return 1;
}

static volatile u8 *map(u64 phys, u64 size)
{
	u64 base = phys & ~0xFFFULL, off = phys - base;
	long va = armpi_syscall(SYS_DEV_MAP, (long)base, (long)((size + off + 0xFFF) & ~0xFFFULL), 0, 0, 0, 0);
	return va < 0 ? 0 : (volatile u8 *)va + off;
}

static int tpm_open(void)
{
	if (crb)
		return 1;
	if (tried)
		return 0;
	tried = 1;
	u8 t[128];
	long n = armpi_syscall(SYS_ACPI_TABLE, (long)"TPM2", 0, (long)t, sizeof t, 0, 0);
	if (n < 52)
		return 0; /* pas de TPM 2.0 annoncé */
	u64 ctrl;
	u32 method;
	memcpy(&ctrl, t + 40, 8);
	memcpy(&method, t + 48, 4);
	if (method != 7) {
		fprintf(stderr, "keyvault: TPM 2.0 à méthode de démarrage %u (seul le CRB, 7, est pris) : clé en fichier\n", method);
		return 0;
	}
	crb = map(ctrl - CTRL_REQ, 0x1000);
	if (!crb)
		return 0;
	/* la localité 0 */
	wr(LOC_CTRL, 1);
	if (!wait_reg(LOC_STS, 1, 1, 750)) {
		fprintf(stderr, "keyvault: TPM : localité 0 refusée\n");
		crb = 0;
		return 0;
	}
	cmd_size = rd(CTRL_CMD_SIZE);
	rsp_size = rd(CTRL_RSP_SIZE);
	u64 cmd_pa = (u64)rd(CTRL_CMD_LADDR) | (u64)rd(CTRL_CMD_HADDR) << 32;
	u64 rsp_pa;
	memcpy(&rsp_pa, (const void *)(crb + CTRL_RSP_ADDR), 8);
	if (cmd_size < 512 || rsp_size < 512 || cmd_size > 0x10000 || rsp_size > 0x10000) {
		fprintf(stderr, "keyvault: TPM : tampons CRB étranges (%u, %u)\n", cmd_size, rsp_size);
		crb = 0;
		return 0;
	}
	cmd_buf = map(cmd_pa, cmd_size);
	rsp_buf = rsp_pa == cmd_pa ? cmd_buf : map(rsp_pa, rsp_size);
	if (!cmd_buf || !rsp_buf) {
		crb = 0;
		return 0;
	}
	return 1;
}

static long tpm_xfer(const u8 *cmd, size_t n, u8 *rsp, size_t cap)
{
	if (n > cmd_size)
		return -1;
	wr(CTRL_REQ, 1); /* cmdReady */
	if (!wait_reg(CTRL_REQ, 1, 0, 200) || (rd(CTRL_STS) & 1))
		return -1;
	for (size_t i = 0; i < n; i++)
		cmd_buf[i] = cmd[i];
	wr(CTRL_START, 1);
	if (!wait_reg(CTRL_START, 1, 0, 120000)) /* (une clé primaire ECC : quelques centaines de ms) */
		return -1;
	u8 h[6];
	for (int i = 0; i < 6; i++)
		h[i] = rsp_buf[i];
	size_t len = (size_t)h[2] << 24 | (size_t)h[3] << 16 | (size_t)h[4] << 8 | h[5];
	if (len < 10 || len > cap || len > rsp_size)
		return -1;
	for (size_t i = 0; i < len; i++)
		rsp[i] = rsp_buf[i];
	wr(CTRL_REQ, 2); /* goIdle */
	return (long)len;
}
#endif

/* ---------------------------------------------------------------- les commandes */

struct w {
	u8 b[BUF];
	size_t n;
};

static void p8(struct w *w, u8 v) { if (w->n < BUF) w->b[w->n++] = v; }
static void p16(struct w *w, u16 v) { p8(w, (u8)(v >> 8)); p8(w, (u8)v); }
static void p32(struct w *w, u32 v) { p16(w, (u16)(v >> 16)); p16(w, (u16)v); }
static void pbytes(struct w *w, const void *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
		p8(w, ((const u8 *)s)[i]);
}

static u16 g16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static u32 g32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static void begin(struct w *w, u16 tag, u32 cc)
{
	w->n = 0;
	p16(w, tag);
	p32(w, 0); /* (taille, plus tard) */
	p32(w, cc);
}

/* la zone d'autorisation : une session de mot de passe vide */
static void pw_auth(struct w *w)
{
	p32(w, 9);
	p32(w, TPM_RS_PW);
	p16(w, 0); /* nonce */
	p8(w, 0);  /* attributs */
	p16(w, 0); /* hmac */
}

static u8 rsp[BUF];

/* envoie -> le code de réponse (0 : TPM_RC_SUCCESS), la réponse dans rsp (*len) ; -1 : transport */
static long run(struct w *w, size_t *len)
{
	w->b[2] = (u8)(w->n >> 24);
	w->b[3] = (u8)(w->n >> 16);
	w->b[4] = (u8)(w->n >> 8);
	w->b[5] = (u8)w->n;
	long r = tpm_xfer(w->b, w->n, rsp, sizeof rsp);
	OPENSSL_cleanse(w->b, w->n);
	if (r < 10)
		return -1;
	if (len)
		*len = (size_t)r;
	return (long)g32(rsp + 6);
}

static void flush(u32 h)
{
	struct w w;
	begin(&w, TPM_ST_NO_SESSIONS, TPM_CC_FLUSH);
	p32(&w, h);
	run(&w, 0);
}

/* la clé primaire de stockage (modèle ECC P-256 standard) -> son handle, 0 : échec */
static u32 primary(void)
{
	struct w w;
	begin(&w, TPM_ST_SESSIONS, TPM_CC_CREATE_PRIMARY);
	p32(&w, TPM_RH_OWNER);
	pw_auth(&w);
	p16(&w, 4); /* inSensitive : userAuth et data vides */
	p16(&w, 0);
	p16(&w, 0);
	struct w pub = {.n = 0};
	p16(&pub, TPM_ALG_ECC);
	p16(&pub, TPM_ALG_SHA256);
	p32(&pub, OA_FIXEDTPM | OA_FIXEDPARENT | OA_SENSITIVEDATAORIGIN | OA_USERWITHAUTH | OA_NODA | OA_RESTRICTED | OA_DECRYPT);
	p16(&pub, 0); /* authPolicy */
	p16(&pub, TPM_ALG_AES);
	p16(&pub, 128);
	p16(&pub, TPM_ALG_CFB);
	p16(&pub, TPM_ALG_NULL); /* schéma */
	p16(&pub, TPM_ECC_NIST_P256);
	p16(&pub, TPM_ALG_NULL); /* kdf */
	p16(&pub, 0);		 /* unique.x */
	p16(&pub, 0);		 /* unique.y */
	p16(&w, (u16)pub.n);
	pbytes(&w, pub.b, pub.n);
	p16(&w, 0); /* outsideInfo */
	p32(&w, 0); /* creationPCR */
	size_t n;
	long rc = run(&w, &n);
	if (rc != 0 || n < 14) {
		fprintf(stderr, "keyvault: TPM CreatePrimary : %#lx\n", rc);
		return 0;
	}
	return g32(rsp + 10);
}

/* scelle les 32 octets de key sous la clé primaire -> priv et pub (TPM2B, tailles comprises) */
static int seal(u32 parent, const u8 key[32], struct w *priv, struct w *pub)
{
	struct w w;
	begin(&w, TPM_ST_SESSIONS, TPM_CC_CREATE);
	p32(&w, parent);
	pw_auth(&w);
	p16(&w, 2 + 2 + 32); /* inSensitive : userAuth vide, data = la clé */
	p16(&w, 0);
	p16(&w, 32);
	pbytes(&w, key, 32);
	struct w tp = {.n = 0};
	p16(&tp, TPM_ALG_KEYEDHASH);
	p16(&tp, TPM_ALG_SHA256);
	p32(&tp, OA_FIXEDTPM | OA_FIXEDPARENT | OA_USERWITHAUTH | OA_NODA);
	p16(&tp, 0);		/* authPolicy */
	p16(&tp, TPM_ALG_NULL); /* schéma */
	p16(&tp, 0);		/* unique */
	p16(&w, (u16)tp.n);
	pbytes(&w, tp.b, tp.n);
	p16(&w, 0);
	p32(&w, 0);
	size_t n;
	long rc = run(&w, &n);
	if (rc != 0 || n < 16) {
		fprintf(stderr, "keyvault: TPM Create : %#lx\n", rc);
		return 0;
	}
	size_t o = 14; /* (en-tête, parameterSize) */
	u16 pl = g16(rsp + o);
	if (o + 2 + pl + 2 > n)
		return 0;
	priv->n = 0;
	pbytes(priv, rsp + o, 2u + pl);
	o += 2u + pl;
	u16 ul = g16(rsp + o);
	if (o + 2 + ul > n)
		return 0;
	pub->n = 0;
	pbytes(pub, rsp + o, 2u + ul);
	return 1;
}

/* charge l'objet scellé et le descelle -> 1, la clé dans out */
static int unseal(u32 parent, const u8 *priv, size_t pn, const u8 *pub, size_t un, u8 out[32])
{
	struct w w;
	begin(&w, TPM_ST_SESSIONS, TPM_CC_LOAD);
	p32(&w, parent);
	pw_auth(&w);
	pbytes(&w, priv, pn);
	pbytes(&w, pub, un);
	size_t n;
	long rc = run(&w, &n);
	if (rc != 0 || n < 14) {
		fprintf(stderr, "keyvault: TPM Load : %#lx\n", rc);
		return 0;
	}
	u32 h = g32(rsp + 10);
	begin(&w, TPM_ST_SESSIONS, TPM_CC_UNSEAL);
	p32(&w, h);
	pw_auth(&w);
	rc = run(&w, &n);
	flush(h);
	if (rc != 0 || n < 16 + 32 || g16(rsp + 14) != 32) {
		fprintf(stderr, "keyvault: TPM Unseal : %#lx\n", rc);
		OPENSSL_cleanse(rsp, sizeof rsp);
		return 0;
	}
	memcpy(out, rsp + 16, 32);
	OPENSSL_cleanse(rsp, sizeof rsp);
	return 1;
}

/* ---------------------------------------------------------------- le fichier machine.tpm */

static int save(const char *path, const struct w *priv, const struct w *pub)
{
	char tmp[220];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return 0;
	int ok = write(fd, MAGIC, 9) == 9 && write(fd, priv->b, priv->n) == (ssize_t)priv->n &&
		 write(fd, pub->b, pub->n) == (ssize_t)pub->n;
	ok = close(fd) == 0 && ok;
	if (!ok) {
		unlink(tmp);
		return 0;
	}
	unlink(path);
	return rename(tmp, path) == 0;
}

int tpm_machine_key(const char *dir, uint8_t out[32])
{
	if (!tpm_open())
		return 0;
	struct w w;
	begin(&w, TPM_ST_NO_SESSIONS, TPM_CC_STARTUP); /* (fait par le micrologiciel : TPM_RC_INITIALIZE) */
	p16(&w, 0);
	long rc = run(&w, 0);
	if (rc < 0)
		return 0; /* (le TPM ne répond pas : comme sans TPM) */
	char path[200], keyf[200];
	snprintf(path, sizeof path, "%s/machine.tpm", dir);
	snprintf(keyf, sizeof keyf, "%s/machine.key", dir);
	u32 srk = primary();
	if (!srk)
		return -5;
	int r;
	u8 f[BUF];
	int fd = open(path, O_RDONLY);
	if (fd >= 0) { /* déjà scellée : la desceller */
		ssize_t n = read(fd, f, sizeof f);
		close(fd);
		r = -22;
		if (n > 13 && !memcmp(f, MAGIC, 9)) {
			size_t o = 9, pn = 2u + g16(f + o);
			if (o + pn + 2 <= (size_t)n) {
				size_t un = 2u + g16(f + o + pn);
				if (o + pn + un <= (size_t)n)
					r = unseal(srk, f + o, pn, f + o + pn, un, out) ? 1 : -13;
			}
		}
	} else { /* la première fois : la clé en fichier (si elle existe) passe au TPM, sinon une nouvelle */
		u8 k[32];
		int from_file = 0;
		fd = open(keyf, O_RDONLY);
		if (fd >= 0) {
			from_file = read(fd, k, 32) == 32;
			close(fd);
		}
		if (!from_file && RAND_bytes(k, 32) != 1)
			r = -5;
		else {
			struct w priv, pub;
			r = seal(srk, k, &priv, &pub) && save(path, &priv, &pub) ? 1 : -5;
			if (r == 1) {
				u8 back[32];
				r = unseal(srk, priv.b, priv.n, pub.b, pub.n, back) && !CRYPTO_memcmp(back, k, 32) ? 1 : -5;
				OPENSSL_cleanse(back, 32);
			}
			if (r == 1) {
				memcpy(out, k, 32);
				if (from_file)
					unlink(keyf);
				fprintf(stderr, "keyvault: clé de la machine scellée dans le TPM%s\n",
					from_file ? " (reprise de machine.key, effacée)" : "");
			} else
				unlink(path);
		}
		OPENSSL_cleanse(k, 32);
	}
	OPENSSL_cleanse(f, sizeof f);
	flush(srk);
	return r;
}
