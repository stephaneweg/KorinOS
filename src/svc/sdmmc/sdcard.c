/* sdcard.c : le protocole de la carte SD, sans rien savoir du contrôleur
 * (sdmmc.h). D'après la « Physical Layer Simplified Specification » publique
 * de la SD Association (version 9), et le pilote en asm du Pi
 * (src/svc/sd/sdhci.asm), qu'il reprend : identification, capacité par le CSD,
 * sélection, bus 4 bits, haute vitesse par CMD6, blocs multiples.
 * En plus de l'asm : CID et SCR lus et décrits, état de la carte (CMD13) après
 * une écriture et après une erreur, une reprise par morceau, retrait et remise. */
#include "sdmmc.h"

int sd_verbose;
const char *sd_tag = "sd";
static uint32_t big_lock;

static void vlog(const char *fmt, va_list ap)
{
	char buf[320];
	int n = snprintf(buf, sizeof buf, "%s : ", sd_tag);
	n += vsnprintf(buf + n, sizeof buf - n, fmt, ap);
	if (n > (int)sizeof buf - 1)
		n = sizeof buf - 1;
	korin_sys2(SYS_PRINT, buf, n);
}

void sd_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}

void sd_dbg(const char *fmt, ...)
{
	if (!sd_verbose)
		return;
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}

void sd_sleep_ms(long ms)
{
	korin_sys1(SYS_SLEEP, ms);
}

long sd_now_ms(void)
{
	return korin_sys0(SYS_TIME_MS);
}

void sd_lock(void)
{
	while (__atomic_exchange_n(&big_lock, 1, __ATOMIC_ACQUIRE))
		korin_sys0(SYS_YIELD);
}

int sd_trylock(void)
{
	return !__atomic_exchange_n(&big_lock, 1, __ATOMIC_ACQUIRE);
}

void sd_unlock(void)
{
	__atomic_store_n(&big_lock, 0, __ATOMIC_RELEASE);
}

/* ---- registres de la carte */

/* bits [hi:lo] d'un registre de 128 bits (CID, CSD) rendu en R2 (resp[0] =
 * bits 39-8) */
static uint32_t rbits(const uint32_t r[4], int hi, int lo)
{
	uint32_t v = 0;
	for (int b = hi; b >= lo; b--) {
		int n = b - 8;
		v = v << 1 | ((r[n / 32] >> (n % 32)) & 1);
	}
	return v;
}

/* R1 : bits d'erreur (OUT_OF_RANGE… ERROR, AKE_SEQ_ERROR) */
#define R1_ERRORS 0xFDF80008u
#define R1_STATE(s) (((s) >> 9) & 15)
#define R1_READY (1u << 8)
#define ST_TRAN 4

static const char *state_name(unsigned s)
{
	static const char *n[] = {"idle", "ready", "ident", "stby", "tran", "data", "rcv", "prg", "dis"};
	return s < 9 ? n[s] : "?";
}

static int cmd(struct sd_card *c, unsigned idx, uint32_t arg, unsigned rsp, struct sd_cmd *out)
{
	struct sd_cmd k;
	struct sd_cmd *x = out ? out : &k;
	if (!out)
		memset(&k, 0, sizeof k);
	x->idx = idx;
	x->arg = arg;
	x->rsp = rsp;
	c->n_cmds++;
	int r = c->host->cmd(c->host, x);
	if (r < 0)
		sd_dbg("CMD%u (%08x) : erreur %d (état %08x)\n", idx, arg, r, x->err_state);
	else if (rsp != SD_RSP_NONE)
		sd_dbg("CMD%u (%08x) -> %08x\n", idx, arg, x->resp[0]);
	return r;
}

static int acmd(struct sd_card *c, unsigned idx, uint32_t arg, unsigned rsp, struct sd_cmd *out)
{
	int r = cmd(c, 55, c->rca, SD_RSP_R1, 0);
	if (r < 0)
		return r;
	return cmd(c, idx, arg, rsp, out);
}

/* CMD13 -> statut R1, ou < 0 */
static long status(struct sd_card *c)
{
	struct sd_cmd x;
	memset(&x, 0, sizeof x);
	int r = cmd(c, 13, c->rca, SD_RSP_R1, &x);
	return r < 0 ? r : (long)x.resp[0];
}

/* attend l'état « tran », prêt aux données (après une écriture) -> 0, < 0 */
static int wait_tran(struct sd_card *c, long ms)
{
	long end = sd_now_ms() + ms;
	for (;;) {
		long s = status(c);
		if (s >= 0 && (s & R1_READY) && R1_STATE(s) == ST_TRAN)
			return 0;
		if (sd_now_ms() > end) {
			if (s < 0)
				sd_log("carte : CMD13 sans réponse (%ld)\n", s);
			else
				sd_log("carte : toujours occupée, état %s (%08lx)\n", state_name(R1_STATE(s)),
				       (unsigned long)s);
			return s < 0 ? (int)s : E_TIMEDOUT;
		}
		sd_sleep_ms(1);
	}
}

static void decode_cid(struct sd_card *c)
{
	char pnm[6];
	for (int i = 0; i < 5; i++) {
		uint32_t ch = rbits(c->cid, 103 - 8 * i, 96 - 8 * i);
		pnm[i] = ch >= 0x20 && ch < 0x7F ? (char)ch : '?';
	}
	pnm[5] = 0;
	uint32_t oid = rbits(c->cid, 119, 104);
	char o[3] = {(char)(oid >> 8), (char)oid, 0};
	if (o[0] < 0x20 || o[0] >= 0x7F)
		o[0] = '?';
	if (o[1] < 0x20 || o[1] >= 0x7F)
		o[1] = '?';
	sd_log("carte : CID fabricant %02x « %s », produit « %s » v%u.%u, série %08x, %u/%u\n",
	       rbits(c->cid, 127, 120), o, pnm, rbits(c->cid, 63, 60), rbits(c->cid, 59, 56), rbits(c->cid, 55, 24),
	       rbits(c->cid, 11, 8), 2000 + rbits(c->cid, 19, 12));
}

static void decode_csd(struct sd_card *c)
{
	unsigned st = rbits(c->csd, 127, 126);
	if (st == 1) { /* v2 : SDHC, SDXC */
		c->sectors = ((uint64_t)rbits(c->csd, 69, 48) + 1) * 1024;
	} else if (st == 0) { /* v1 : SDSC */
		uint64_t size = rbits(c->csd, 73, 62) + 1;
		size <<= rbits(c->csd, 49, 47) + 2;
		size <<= rbits(c->csd, 83, 80);
		c->sectors = size >> 9;
	} else { /* v3 : SDUC (plus de 2 To), pas géré */
		c->sectors = ((uint64_t)rbits(c->csd, 75, 48) + 1) * 1024;
		sd_log("carte : CSD de version 3 (SDUC) : capacité seulement indicative\n");
	}
	sd_log("carte : CSD v%u, %lu secteurs (%lu Mo), vitesse de base %02x, classes %03x\n", st + 1,
	       (unsigned long)c->sectors, (unsigned long)(c->sectors >> 11), rbits(c->csd, 103, 96),
	       rbits(c->csd, 95, 84));
}

/* ACMD51 : le SCR (8 octets, gros-boutiste) -> 0, < 0 */
static int read_scr(struct sd_card *c)
{
	static uint8_t scr[8] __attribute__((aligned(64)));
	struct sd_cmd x;
	memset(&x, 0, sizeof x);
	x.buf = scr;
	x.blksz = 8;
	x.blocks = 1;
	int r = acmd(c, 51, 0, SD_RSP_R1, &x);
	if (r < 0)
		return r;
	memcpy(c->scr, scr, 8);
	sd_log("carte : SCR %02x %02x %02x %02x : spécification %u%s%s, bus%s%s\n", scr[0], scr[1], scr[2], scr[3],
	       scr[0] & 15, scr[2] & 0x80 ? " (3.0)" : "", scr[2] & 0x04 ? " (4.0)" : "", scr[1] & 1 ? " 1 bit" : "",
	       scr[1] & 4 ? " 4 bits" : "");
	return 0;
}

/* CMD6 : arg, statut de 64 octets dans st -> 0, < 0 */
static int switch_func(struct sd_card *c, uint32_t arg, uint8_t *st)
{
	struct sd_cmd x;
	memset(&x, 0, sizeof x);
	x.buf = st;
	x.blksz = 64;
	x.blocks = 1;
	return cmd(c, 6, arg, SD_RSP_R1, &x);
}

/* haute vitesse (50 MHz) : mode 0 (question), puis mode 1 (bascule) */
static int high_speed(struct sd_card *c)
{
	static uint8_t st[64] __attribute__((aligned(64)));
	if ((c->scr[0] & 15) < 1) {
		sd_log("carte : spécification 1.0, pas de CMD6\n");
		return 0;
	}
	if (switch_func(c, 0x00FFFFF1, st) < 0) {
		sd_log("carte : CMD6 (question) refusée\n");
		return 0;
	}
	sd_dbg("CMD6 mode 0 : courant %u mA, groupe 1 %02x%02x\n", st[0] << 8 | st[1], st[12], st[13]);
	if (!(st[13] & 2)) {
		sd_log("carte : pas de haute vitesse (groupe 1 : %02x)\n", st[13]);
		return 0;
	}
	if (switch_func(c, 0x80FFFFF1, st) < 0 || (st[16] & 15) != 1) {
		sd_log("carte : bascule en haute vitesse refusée (%02x)\n", st[16]);
		return 0;
	}
	sd_sleep_ms(1); /* (8 cycles au moins avant la nouvelle horloge) */
	return 1;
}

int sd_card_init(struct sd_card *c, struct sd_host *h)
{
	struct sd_cmd x;
	unsigned gen = c->gen + 1;
	memset(c, 0, sizeof *c);
	c->host = h;
	c->gen = gen;
	int r = h->power_up(h);
	if (r < 0) {
		if (r == E_NOMEDIUM)
			sd_log("carte : absente\n");
		else
			sd_log("carte : contrôleur pas prêt (%d)\n", r);
		return r;
	}
	cmd(c, 0, 0, SD_RSP_NONE, 0); /* GO_IDLE_STATE */
	sd_sleep_ms(2);
	memset(&x, 0, sizeof x);
	r = cmd(c, 8, 0x1AA, SD_RSP_R7, &x); /* SEND_IF_COND : 2,7-3,6 V, motif AA */
	if (r == 0) {
		if ((x.resp[0] & 0xFFF) != 0x1AA) {
			sd_log("carte : CMD8 rend %08x au lieu de 1aa : tension refusée\n", x.resp[0]);
			return E_IO;
		}
		c->v2 = 1;
	} else {
		h->recover(h);
		sd_log("carte : pas de réponse à CMD8 : carte SD 1.x (ou pas une carte SD)\n");
	}
	/* ACMD41 jusqu'à la fin de l'initialisation (1 s d'après la spécification) */
	uint32_t arg = 0x00FF8000 | (c->v2 ? 1u << 30 : 0); /* fenêtre 2,7-3,6 V, HCS */
	long end = sd_now_ms() + 2000;
	int tries = 0;
	for (;;) {
		memset(&x, 0, sizeof x);
		tries++;
		if (cmd(c, 55, 0, SD_RSP_R1, 0) < 0 && tries == 1)
			h->recover(h);
		r = cmd(c, 41, arg, SD_RSP_R3, &x);
		if (r == 0 && (x.resp[0] & (1u << 31)))
			break;
		if (sd_now_ms() > end) {
			sd_log("carte : ACMD41 : jamais prête (%d essais, OCR %08x, %d)\n", tries, x.resp[0], r);
			return r < 0 ? r : E_TIMEDOUT;
		}
		sd_sleep_ms(10);
	}
	c->ocr = x.resp[0];
	c->hc = (c->ocr >> 30) & 1;
	sd_log("carte : prête après %d ACMD41, OCR %08x : %s%s\n", tries, c->ocr,
	       c->hc ? "SDHC/SDXC (adresse en blocs)" : "SDSC (adresse en octets)", c->ocr & (1u << 24) ? ", 1,8 V possible" : "");
	memset(&x, 0, sizeof x);
	if ((r = cmd(c, 2, 0, SD_RSP_R2, &x)) < 0) { /* ALL_SEND_CID */
		sd_log("carte : CMD2 (CID) sans réponse (%d)\n", r);
		return r;
	}
	memcpy(c->cid, x.resp, 16);
	decode_cid(c);
	memset(&x, 0, sizeof x);
	if ((r = cmd(c, 3, 0, SD_RSP_R6, &x)) < 0) { /* SEND_RELATIVE_ADDR */
		sd_log("carte : CMD3 (RCA) sans réponse (%d)\n", r);
		return r;
	}
	c->rca = x.resp[0] & 0xFFFF0000;
	memset(&x, 0, sizeof x);
	if ((r = cmd(c, 9, c->rca, SD_RSP_R2, &x)) < 0) { /* SEND_CSD */
		sd_log("carte : CMD9 (CSD) sans réponse (%d)\n", r);
		return r;
	}
	memcpy(c->csd, x.resp, 16);
	decode_csd(c);
	if ((r = cmd(c, 7, c->rca, SD_RSP_R1B, 0)) < 0) { /* SELECT_CARD */
		sd_log("carte : CMD7 (sélection) refusée (%d)\n", r);
		return r;
	}
	if (!c->hc && cmd(c, 16, 512, SD_RSP_R1, 0) < 0) /* SET_BLOCKLEN (SDSC) */
		sd_log("carte : CMD16 refusée\n");
	/* données à 25 MHz (vitesse normale) */
	if ((r = h->set_clock(h, 25000000, 0)) < 0) {
		sd_log("carte : horloge de 25 MHz refusée (%d)\n", r);
		return r;
	}
	int scr_ok = read_scr(c) == 0;
	if (!scr_ok) {
		sd_log("carte : SCR illisible : on suppose le bus 4 bits\n");
		c->scr[1] = 5;
		c->scr[0] = 2;
		h->recover(h);
	}
	c->width = 1;
	if ((h->caps & SDH_4BIT) && (c->scr[1] & 4)) {
		if (acmd(c, 6, 2, SD_RSP_R1, 0) == 0 && h->set_width(h, 4) == 0)
			c->width = 4;
		else
			sd_log("carte : bus 4 bits refusé, on reste en 1 bit\n");
	}
	if ((h->caps & SDH_HS) && high_speed(c)) {
		if (h->set_clock(h, 50000000, 1) == 0)
			c->hs = 1;
	}
	long s = status(c);
	if (s < 0 || R1_STATE(s) != ST_TRAN) {
		sd_log("carte : état inattendu après l'initialisation (%ld, %s)\n", s, s < 0 ? "-" : state_name(R1_STATE(s)));
		return s < 0 ? (int)s : E_IO;
	}
	c->ro = h->write_protected ? h->write_protected(h) : 0;
	c->ready = 1;
	/* premier bloc relu : si la haute vitesse ne passe pas, retour à 25 MHz */
	static uint8_t probe[512] __attribute__((aligned(64)));
	if (sd_read(c, 0, 1, probe, 0) < 0 && c->hs) {
		sd_log("carte : lecture ratée à 50 MHz, retour à 25 MHz\n");
		h->set_clock(h, 25000000, 0);
		c->hs = 0;
		if (sd_read(c, 0, 1, probe, 0) < 0) {
			sd_log("carte : premier bloc illisible\n");
			c->ready = 0;
			return E_IO;
		}
	}
	sd_card_describe(c);
	return 0;
}

void sd_card_describe(const struct sd_card *c)
{
	const char *d = c->host->describe ? c->host->describe(c->host) : "";
	sd_log("carte : prête, %lu secteurs, bus %d bits, %s%s%s%s\n", (unsigned long)c->sectors, c->width,
	       c->hs ? "50 MHz (haute vitesse)" : "25 MHz", c->ro ? ", protégée en écriture" : "", *d ? ", " : "", d);
}

int sd_card_check(struct sd_card *c)
{
	if (!c->host->present(c->host)) {
		if (c->ready)
			sd_log("carte : retirée\n");
		c->ready = 0;
		return 0;
	}
	return c->ready;
}

/* un morceau (n <= max_blocks) -> 0, < 0 */
static int xfer(struct sd_card *c, uint64_t lba, unsigned n, void *buf, uint64_t phys, int write)
{
	struct sd_cmd x;
	memset(&x, 0, sizeof x);
	x.buf = buf;
	x.phys = phys;
	x.blksz = 512;
	x.blocks = n;
	x.write = write;
	unsigned idx = write ? (n > 1 ? 25 : 24) : (n > 1 ? 18 : 17);
	uint64_t a = c->hc ? lba : lba << 9;
	int r = cmd(c, idx, (uint32_t)a, SD_RSP_R1, &x);
	if (r == 0 && (x.resp[0] & R1_ERRORS)) {
		sd_log("carte : CMD%u bloc %lu : statut d'erreur %08x\n", idx, (unsigned long)lba, x.resp[0]);
		r = E_IO;
	}
	if (r == 0 && write)
		r = wait_tran(c, 2000);
	return r;
}

/* après une erreur : contrôleur remis, transfert arrêté, état relu */
static void recover(struct sd_card *c, int r, unsigned idx_hint, uint64_t lba, unsigned n)
{
	struct sd_host *h = c->host;
	c->n_errors++;
	sd_log("carte : erreur %d sur %u blocs depuis %lu (CMD%u)\n", r, n, (unsigned long)lba, idx_hint);
	if (sd_verbose && h->dump)
		h->dump(h, "erreur");
	h->recover(h);
	if (!h->present(h))
		return;
	cmd(c, 12, 0, SD_RSP_R1B, 0); /* STOP_TRANSMISSION (peut échouer : déjà arrêté) */
	h->recover(h);
	long s = status(c);
	if (s >= 0)
		sd_log("carte : état après reprise : %s (%08lx)\n", state_name(R1_STATE(s)), (unsigned long)s);
	if (s >= 0 && R1_STATE(s) != ST_TRAN)
		wait_tran(c, 500);
}

static int rw(struct sd_card *c, uint64_t lba, unsigned n, void *buf, uint64_t phys, int write)
{
	if (!c->ready)
		return E_NOMEDIUM;
	if (lba + n > c->sectors || lba + n < lba)
		return E_INVAL;
	if (write && c->ro)
		return E_PERM;
	uint8_t *p = buf;
	while (n) {
		unsigned k = n < c->host->max_blocks ? n : c->host->max_blocks;
		int r = xfer(c, lba, k, p, phys, write);
		if (r < 0) {
			if (r == E_NOMEDIUM || !c->host->present(c->host)) {
				sd_card_check(c);
				return E_NOMEDIUM;
			}
			recover(c, r, write ? (k > 1 ? 25 : 24) : (k > 1 ? 18 : 17), lba, k);
			c->n_retries++;
			r = xfer(c, lba, k, p, phys, write); /* une seconde fois */
			if (r < 0) {
				recover(c, r, 0, lba, k);
				sd_log("carte : abandon après deux essais\n");
				return r == E_NOMEDIUM ? r : E_IO;
			}
		}
		c->n_blocks += k;
		lba += k;
		n -= k;
		p += (uint64_t)k * 512;
		if (phys)
			phys += (uint64_t)k * 512;
	}
	return 0;
}

int sd_read(struct sd_card *c, uint64_t lba, unsigned n, void *buf, uint64_t phys)
{
	return rw(c, lba, n, buf, phys, 0);
}

int sd_write(struct sd_card *c, uint64_t lba, unsigned n, const void *buf, uint64_t phys)
{
	return rw(c, lba, n, (void *)buf, phys, 1);
}
