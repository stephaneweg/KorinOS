/* hda - pilote des contrôleurs Intel High Definition Audio (docs/etude/SON-PC.md), PC. Lancé par
 * hwmanager (drivers.txt : pci:04/03, pci:8086:43c8 ; argument « SSSS:BB:DD.F »), un process par
 * contrôleur. Il sert la sortie analogique du premier codec qui en a une (haut-parleurs, casque,
 * sortie de ligne ; prise casque suivie par la détection de présence) et son entrée (micro de la
 * prise), inscrites auprès de sound (sndif) :
 *  * contrôleur : remise à zéro, DSP du cAVS d'Intel sorti du chemin (capacités PP et ML), CORB et
 *    RIRB de 256 entrées en DMA (repli : commande immédiate), MSI (sinon sondage, 5 ms) ;
 *  * sortie : un flux de sortie, BDL de 4 périodes de 512 trames s16 stéréo à 44,1 kHz (ou
 *    48 kHz), chaque période jouée remplie de nouveau depuis l'anneau de sound ;
 *  * entrée : un flux d'entrée lancé quand une capture écoute (SR_WANTED), arrêté 1 s après.
 * Volume dans sound (logiciel) ; amplis du codec à 0 dB ; volume de départ modéré (50, ou
 * hda_volume=N dans /etc/options.txt). Autres options : hda_quirks=0, hda_gpio=0xMM,
 * hda_nomsi, hda_immediate (sans CORB). Rien ne sort sur les pins HDMI/DP (pas encore
 * servis) : sans sortie analogique, le pilote s'arrête (code 0). */
#include "hda.h"
#include "../sndif/sndif.h"
#include <stdarg.h>

#define NPER 4      /* périodes du BDL */
#define PER_FR 512  /* trames par période */
#define PER_B (PER_FR * 4)
#define BUF_B (NPER * PER_B)
#define SND_OUT_PWM 1 /* (src/inc/sound.inc : sortie analogique, prise jack) */
#define TAG_OUT 1
#define TAG_IN 2
/* mémoire DMA (32 Ko) */
#define D_CORB 0x0000
#define D_RIRB 0x0400
#define D_BDL_OUT 0x1000
#define D_BDL_IN 0x1100
#define D_BUF_OUT 0x2000
#define D_BUF_IN (D_BUF_OUT + BUF_B)
#define D_SIZE 0x8000

struct hda_plat plat;
int opt_quirks = 1;
unsigned opt_gpio;
static long opt_volume = 50;
static int opt_nomsi, opt_immediate;

static u8 *dma;
static u64 dma_pa;
static u32 *corb;
static u32 *rirb;
static int corb_n, rirb_n, rirb_rp;
static int immediate, timeouts, cmd_failed;
static int iss, oss, sd_out = -1, sd_in = -1;
static struct codec codecs[2];
static struct codec *cod; /* le codec servi */
static u32 unsol_q[16];
static int unsol_n;
static long irq = -1;
static u64 out_rate, in_rate;
static u16 out_fmt, in_fmt;

/* ---- registres */
static inline u8 rd8(u32 o) { return *(volatile u8 *)(plat.regs + o); }
static inline u16 rd16(u32 o) { return *(volatile u16 *)(plat.regs + o); }
static inline u32 rd32(u32 o) { return *(volatile u32 *)(plat.regs + o); }
static inline void wr8(u32 o, u8 v) { *(volatile u8 *)(plat.regs + o) = v; }
static inline void wr16(u32 o, u16 v) { *(volatile u16 *)(plat.regs + o) = v; }
static inline void wr32(u32 o, u32 v) { *(volatile u32 *)(plat.regs + o) = v; }
#define SD(n, r) (R_SD0 + 0x20 * (u32)(n) + (r))

void hd_log(const char *fmt, ...)
{
	char b[256];
	int k = snprintf(b, sizeof b, "hda %s : ", plat.where[0] ? plat.where : "?");
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b + k, sizeof b - (size_t)k, fmt, ap);
	va_end(ap);
	ak_puts(b);
}

void hd_sleep(long ms)
{
	korin_sys1(SYS_SLEEP, ms);
}

static u64 now_ms(void)
{
	return (u64)korin_sys0(SYS_TIME_MS);
}

/* attend (registre & masque) == valeur, au plus ms -> 1 si atteint */
static int wait16(u32 reg, u16 mask, u16 val, long ms)
{
	u64 end = now_ms() + (u64)ms;
	for (int spin = 0;; spin++) {
		if ((rd16(reg) & mask) == val)
			return 1;
		if (now_ms() > end)
			return 0;
		if (spin > 100)
			korin_sys0(SYS_YIELD);
	}
}

static int wait8(u32 reg, u8 mask, u8 val, long ms)
{
	u64 end = now_ms() + (u64)ms;
	for (;;) {
		if ((rd8(reg) & mask) == val)
			return 1;
		if (now_ms() > end)
			return 0;
		korin_sys0(SYS_YIELD);
	}
}

/* ---- commandes : CORB / RIRB, ou l'interface immédiate */

/* les réponses arrivées : non sollicitées mises de côté ; -> 1 et *resp si une réponse sollicitée */
static int rirb_drain(u32 *resp)
{
	int got = 0;
	int wp = rd16(R_RIRBWP) & 0xFF;
	if (rirb_rp != wp)
		wr8(R_RIRBSTS, 5); /* (effacé : le contrôleur s'arrête après RINTCNT réponses tant qu'il est levé) */
	while (rirb_rp != wp) {
		rirb_rp = (rirb_rp + 1) % rirb_n;
		u32 r = rirb[rirb_rp * 2], ex = rirb[rirb_rp * 2 + 1];
		if (ex & 0x10) {
			if (unsol_n < 16)
				unsol_q[unsol_n++] = r;
		} else if (resp) {
			*resp = r;
			got = 1;
		}
	}
	return got;
}

static u32 cmd_immediate(u32 v)
{
	if (!wait16(R_ICIS, 1, 0, 10))
		return 0xFFFFFFFF;
	wr16(R_ICIS, 2); /* (réponse précédente effacée) */
	wr32(R_ICOI, v);
	wr16(R_ICIS, 1);
	if (!wait16(R_ICIS, 3, 2, 50))
		return 0xFFFFFFFF;
	u32 r = rd32(R_ICII);
	wr16(R_ICIS, 2);
	return r;
}

u32 hd_cmd(int addr, int nid, u32 verb, u32 payload)
{
	u32 v = (u32)addr << 28 | (u32)nid << 20;
	v |= verb <= 0xF ? verb << 16 | (payload & 0xFFFF) : verb << 8 | (payload & 0xFF);
	if (immediate) {
		u32 r = cmd_immediate(v);
		if (r == 0xFFFFFFFF)
			cmd_failed++;
		return r;
	}
	int wp = (rd16(R_CORBWP) + 1) % corb_n;
	corb[wp] = v;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	wr16(R_CORBWP, (u16)wp);
	u64 end = now_ms() + 50;
	u32 r;
	for (int spin = 0;; spin++) {
		if (rirb_drain(&r)) {
			timeouts = 0;
			return r;
		}
		if (now_ms() > end)
			break;
		if (spin > 64)
			korin_sys0(SYS_YIELD);
	}
	cmd_failed++;
	if ((rd16(R_CORBRP) & 0xFF) == wp) /* commande prise par le contrôleur : le codec se tait */
		return 0xFFFFFFFF;
	if (++timeouts >= 3) {
		hd_log("le CORB ne répond plus (CORBRP %x, RIRBWP %x) : commande immédiate\n", rd16(R_CORBRP),
		       rd16(R_RIRBWP));
		wr8(R_CORBCTL, 0);
		wr8(R_RIRBCTL, 0);
		immediate = 1;
		return cmd_immediate(v);
	}
	return 0xFFFFFFFF;
}

int hd_cmd_failed(void)
{
	return cmd_failed;
}

/* ---- le contrôleur */

/* capacités étendues du cAVS d'Intel : DSP hors du chemin (PP), lien 0 allumé (ML) */
static void cavs_legacy(void)
{
	u32 off = rd16(R_LLCH);
	for (int k = 0; off && off + 0x48 <= plat.regs_size && k < 12; k++) {
		u32 h = rd32(off), id = (h >> 16) & 0xFFF;
		if (id == CAP_PP) {
			u32 v = rd32(off + PP_PPCTL);
			if (v & 0x7FFFFFFF)
				wr32(off + PP_PPCTL, 0);
			hd_log("cAVS : PPCTL %08x -> %08x (GPROCEN 0 : DSP hors du chemin)\n", v, rd32(off + PP_PPCTL));
		} else if (id == CAP_ML) {
			u32 cap = rd32(off + ML_LCAP), ctl = rd32(off + ML_LCTL);
			if (!(ctl & (1u << 16))) {
				wr32(off + ML_LCTL, ctl | 1u << 16);
				u64 end = now_ms() + 20;
				while (!(rd32(off + ML_LCTL) & (1u << 23)) && now_ms() < end)
					korin_sys0(SYS_YIELD);
			}
			hd_log("cAVS : lien 0 LCAP %08x LCTL %08x -> %08x\n", cap, ctl, rd32(off + ML_LCTL));
		}
		u32 nx = h & 0xFFFF;
		if (nx <= off)
			break;
		off = nx;
	}
}

static int controller_reset(void)
{
	wr32(R_INTCTL, 0);
	for (int i = 0; i < iss + oss; i++)
		wr8(SD(i, SD_CTL), 0);
	wr8(R_CORBCTL, 0);
	wr8(R_RIRBCTL, 0);
	wait8(R_CORBCTL, 2, 0, 10);
	wait8(R_RIRBCTL, 2, 0, 10);
	hda_plat_before_reset(&plat);
	wr16(R_STATESTS, 0x7FFF);
	wr32(R_GCTL, rd32(R_GCTL) & ~GCTL_CRST);
	u64 end = now_ms() + 100;
	while ((rd32(R_GCTL) & GCTL_CRST) && now_ms() < end)
		korin_sys0(SYS_YIELD);
	hd_sleep(1);
	wr32(R_GCTL, rd32(R_GCTL) | GCTL_CRST);
	end = now_ms() + 100;
	while (!(rd32(R_GCTL) & GCTL_CRST) && now_ms() < end)
		korin_sys0(SYS_YIELD);
	int ok = (rd32(R_GCTL) & GCTL_CRST) != 0;
	hd_sleep(2); /* (521 µs au moins avant que les codecs se présentent) */
	hda_plat_after_reset(&plat);
	if (!ok) {
		hd_log("remise à zéro : CRST reste à 0 (GCTL %08x)\n", rd32(R_GCTL));
		return -1;
	}
	for (int t = 0; t < 10 && !(rd16(R_STATESTS) & 0x7FFF); t++)
		hd_sleep(10);
	return 0;
}

static int ring_size(u32 reg, int *n)
{
	u8 cap = rd8(reg) >> 4;
	int sel = cap & 4 ? 2 : cap & 2 ? 1 : 0;
	*n = sel == 2 ? 256 : sel == 1 ? 16 : 2;
	wr8(reg, (u8)((rd8(reg) & ~3) | sel));
	return 0;
}

static void corb_start(void)
{
	corb = (u32 *)(dma + D_CORB);
	rirb = (u32 *)(dma + D_RIRB);
	u64 cp = dma_pa + D_CORB, rp = dma_pa + D_RIRB;
	wr8(R_CORBCTL, 0);
	wr32(R_CORBLBASE, (u32)cp);
	wr32(R_CORBUBASE, (u32)(cp >> 32));
	ring_size(R_CORBSIZE, &corb_n);
	wr16(R_CORBRP, 0x8000); /* remise à zéro du pointeur de lecture (QEMU ne remonte pas le bit) */
	wait16(R_CORBRP, 0x8000, 0x8000, 2);
	wr16(R_CORBRP, 0);
	wait16(R_CORBRP, 0x8000, 0, 2);
	wr16(R_CORBWP, 0);
	wr8(R_RIRBCTL, 0);
	wr32(R_RIRBLBASE, (u32)rp);
	wr32(R_RIRBUBASE, (u32)(rp >> 32));
	ring_size(R_RIRBSIZE, &rirb_n);
	wr16(R_RIRBWP, 0x8000);
	wr16(R_RINTCNT, 1);
	rirb_rp = 0;
	wr8(R_CORBCTL, 2);
	wr8(R_RIRBCTL, 2 | 1); /* DMA, interruption de réponse (les non sollicitées réveillent) */
	wr32(R_GCTL, rd32(R_GCTL) | GCTL_UNSOL);
}

/* descripteur, BDL, tampon, étiquette, format, entrée ? -> 0 */
static void stream_setup(int sd, u32 bdl_off, u32 buf_off, int tag, u16 fmt)
{
	wr8(SD(sd, SD_CTL), 0);
	wait8(SD(sd, SD_CTL), 2, 0, 10);
	wr8(SD(sd, SD_CTL), 1); /* SRST */
	wait8(SD(sd, SD_CTL), 1, 1, 10);
	wr8(SD(sd, SD_CTL), 0);
	wait8(SD(sd, SD_CTL), 1, 0, 10);
	wr8(SD(sd, SD_STS), 0x1C);
	u32 *bdl = (u32 *)(dma + bdl_off);
	for (int i = 0; i < NPER; i++) {
		u64 a = dma_pa + buf_off + (u64)i * PER_B;
		bdl[i * 4] = (u32)a;
		bdl[i * 4 + 1] = (u32)(a >> 32);
		bdl[i * 4 + 2] = PER_B;
		bdl[i * 4 + 3] = 1; /* IOC */
	}
	u64 bp = dma_pa + bdl_off;
	wr32(SD(sd, SD_BDPL), (u32)bp);
	wr32(SD(sd, SD_BDPU), (u32)(bp >> 32));
	wr32(SD(sd, SD_CBL), BUF_B);
	wr16(SD(sd, SD_LVI), NPER - 1);
	wr16(SD(sd, SD_FMT), fmt);
	wr8(SD(sd, SD_CTL + 2), (u8)(tag << 4));
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static void stream_run(int sd, int run)
{
	if (run) {
		wr32(R_INTCTL, rd32(R_INTCTL) | 1u << sd | INTCTL_GIE | INTCTL_CIE);
		wr8(SD(sd, SD_CTL), 2 | 4); /* RUN, IOCE */
	} else {
		wr8(SD(sd, SD_CTL), 0);
		wait8(SD(sd, SD_CTL), 2, 0, 10);
		wr32(R_INTCTL, rd32(R_INTCTL) & ~(1u << sd));
	}
}

/* fréquence -> format (16 bits stéréo) ; masque P_PCM -> fréquence choisie */
static u64 pick_rate(u32 mask, u16 *fmt)
{
	if (mask & (1u << 5)) {
		*fmt = 0x4011; /* 44,1 kHz */
		return 44100;
	}
	*fmt = 0x0011; /* 48 kHz */
	return 48000;
}

/* ---- options (/etc/options.txt) */

static long opt_num(const char *b, const char *key, int *found)
{
	u64 kl = strlen(key);
	for (const char *l = b; *l;) {
		if (!strncmp(l, key, kl)) {
			*found = 1;
			const char *v = l + kl;
			if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
				long x = 0;
				for (v += 2;; v++) {
					int d = *v >= '0' && *v <= '9' ? *v - '0' : (*v | 32) >= 'a' && (*v | 32) <= 'f' ? (*v | 32) - 'a' + 10 : -1;
					if (d < 0)
						break;
					x = x * 16 + d;
				}
				return x;
			}
			return (long)parse_dec(v);
		}
		while (*l && *l != '\n')
			l++;
		if (*l)
			l++;
	}
	*found = 0;
	return 0;
}

static void options_read(void)
{
	char b[2048];
	long fd = sys_open("/etc/options.txt", O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, b, sizeof b - 1);
	sys_close(fd);
	if (n <= 0)
		return;
	b[n] = 0;
	int f;
	long v = opt_num(b, "hda_volume=", &f);
	if (f && v >= 1 && v <= 100)
		opt_volume = v;
	v = opt_num(b, "hda_quirks=", &f);
	if (f)
		opt_quirks = v != 0;
	v = opt_num(b, "hda_gpio=", &f);
	if (f)
		opt_gpio = (unsigned)v & 0xFF;
	opt_num(b, "hda_nomsi", &f);
	opt_nomsi = f;
	opt_num(b, "hda_immediate", &f);
	opt_immediate = f;
}

/* ---- la boucle */

static u64 out_hw, out_last_lpib, out_written; /* octets joués (cumul), dernier LPIB, périodes écrites */
static u64 in_hw, in_last_lpib, in_read;
static int in_on;
static u64 in_off_at;
static u32 conv[PER_FR];

static void out_fill(int per)
{
	sndif_read((u32 *)(dma + D_BUF_OUT + (u64)per * PER_B), PER_FR);
}

static void out_service(void)
{
	u32 lpib = rd32(SD(sd_out, SD_LPIB)) % BUF_B;
	out_hw += (lpib + BUF_B - out_last_lpib) % BUF_B;
	out_last_lpib = lpib;
	u64 playing = out_hw / PER_B; /* période absolue en cours */
	while (out_written < playing + NPER) {
		out_fill((int)(out_written % NPER));
		out_written++;
	}
}

static void in_start(int on)
{
	if (on) {
		stream_setup(sd_in, D_BDL_IN, D_BUF_IN, TAG_IN, in_fmt);
		codec_start_in(cod, TAG_IN, in_fmt);
		in_hw = in_last_lpib = in_read = 0;
		stream_run(sd_in, 1);
		hd_log("capture ouverte (%lu Hz)\n", (unsigned long)in_rate);
	} else {
		stream_run(sd_in, 0);
		codec_start_in(cod, 0, in_fmt);
		hd_log("capture fermée\n");
	}
	in_on = on;
}

static void in_service(void)
{
	int want = sndif_wanted() != 0;
	if (want) {
		in_off_at = 0;
		if (!in_on)
			in_start(1);
	} else if (in_on) {
		if (!in_off_at)
			in_off_at = now_ms() + 1000;
		else if (now_ms() > in_off_at) {
			in_start(0);
			in_off_at = 0;
		}
	}
	if (!in_on)
		return;
	u32 lpib = rd32(SD(sd_in, SD_LPIB)) % BUF_B;
	in_hw += (lpib + BUF_B - in_last_lpib) % BUF_B;
	in_last_lpib = lpib;
	u64 done = in_hw / PER_B; /* périodes remplies par le contrôleur */
	while (in_read < done) {
		const u32 *src = (const u32 *)(dma + D_BUF_IN + (in_read % NPER) * PER_B);
		for (int i = 0; i < PER_FR; i++)
			conv[i] = src[i];
		sndif_write(conv, PER_FR);
		in_read++;
	}
}

static void irq_ack(void)
{
	u32 st = rd32(R_INTSTS);
	if (st & (1u << sd_out))
		wr8(SD(sd_out, SD_STS), 0x1C);
	if (sd_in >= 0 && (st & (1u << sd_in)))
		wr8(SD(sd_in, SD_STS), 0x1C);
	u8 rs = rd8(R_RIRBSTS);
	if (rs)
		wr8(R_RIRBSTS, rs);
}

int main(int argc, char **argv)
{
	const char *arg = argc > 1 ? argv[1] : 0;
	options_read();
	int r = hda_plat_init(arg, &plat);
	if (r == E_BUSY) {
		hd_log("contrôleur déjà servi : fin\n");
		return 0;
	}
	if (r < 0)
		return r == E_NODEV ? 0 : 1;
	u16 gcap = rd16(R_GCAP);
	oss = (gcap >> 12) & 15;
	iss = (gcap >> 8) & 15;
	hd_log("HDA %d.%d, %d flux d'entrée, %d de sortie, %s\n", rd8(R_VMAJ), rd8(R_VMIN), iss, oss,
	       gcap & 1 ? "64 bits" : "32 bits");
	if (!oss) {
		hd_log("aucun flux de sortie : fin\n");
		return 0;
	}
	long ph = 0;
	long va = korin_sys2(SYS_DMA_ALLOC, D_SIZE, &ph);
	if (va < 0) {
		hd_log("pas de mémoire DMA (%ld)\n", va);
		return 1;
	}
	dma = (u8 *)va;
	dma_pa = (u64)ph;
	if (!(gcap & 1) && (dma_pa >> 32)) {
		hd_log("mémoire DMA au-dessus de 4 Go (%lx) et contrôleur 32 bits : fin\n", (unsigned long)dma_pa);
		return 1;
	}
	if (controller_reset() < 0)
		return 1;
	if (plat.vendor == 0x8086)
		cavs_legacy();
	u16 codec_mask = rd16(R_STATESTS) & 0x7FFF;
	hd_log("codecs présents : %04x\n", codec_mask);
	if (!codec_mask) {
		hd_log("aucun codec sur le lien : fin\n");
		return 0;
	}
	if (!opt_nomsi)
		irq = hda_plat_irq(&plat);
	corb_start();
	if (opt_immediate) {
		wr8(R_CORBCTL, 0);
		wr8(R_RIRBCTL, 0);
		immediate = 1;
		hd_log("commande immédiate (hda_immediate)\n");
	}
	/* les codecs : le premier qui a une sortie analogique est servi */
	int nc = 0;
	for (int a = 0; a < 15 && nc < 2; a++) {
		if (!(codec_mask & (1u << a)))
			continue;
		if (!codec_probe(&codecs[nc], a))
			continue;
		if (!cod && codec_setup(&codecs[nc]))
			cod = &codecs[nc];
		nc++;
	}
	if (!cod) {
		hd_log("pas de sortie analogique sur ce contrôleur : fin\n");
		return 0;
	}
	if (hd_cmd_failed())
		hd_log("%d commande(s) sans réponse pendant l'énumération\n", hd_cmd_failed());
	sd_out = iss; /* premier descripteur de sortie */
	sd_in = iss && cod->cap.n ? 0 : -1;
	out_rate = pick_rate(cod->rate_mask, &out_fmt);
	in_rate = sd_in >= 0 ? pick_rate(cod->cap_rate_mask, &in_fmt) : 0;
	/* sound : sortie (et entrée) ; volume de départ modéré */
	char nm[40];
	snprintf(nm, sizeof nm, "Analogique (%s)", cod->name);
	sndif_volume0 = (u64)opt_volume;
	long id = sndif_output(SND_OUT_PWM, out_rate, 0, nm);
	if (id < 0) {
		hd_log("sound ne répond pas (%ld) : fin\n", id);
		return 1;
	}
	long in_id = -1;
	if (sd_in >= 0) {
		char inm[40];
		snprintf(inm, sizeof inm, "%s (%s)", cod->cap_dev == DEV_MIC ? "Micro" : "Entrée de ligne", cod->name);
		in_id = sndif_input(2 /* SND_IN_I2S : micro hors USB */, in_rate, 0, inm);
	}
	/* le flux de sortie : 4 périodes préremplies, puis en marche */
	stream_setup(sd_out, D_BDL_OUT, D_BUF_OUT, TAG_OUT, out_fmt);
	codec_start_out(cod, TAG_OUT, out_fmt);
	for (int i = 0; i < NPER; i++)
		out_fill(i);
	out_written = NPER;
	stream_run(sd_out, 1);
	hd_log("en service : sortie %ld « %s », %lu Hz%s, volume de départ %ld ; entrée %ld ; %s\n", id, nm,
	       (unsigned long)out_rate, immediate ? ", commande immédiate" : "", opt_volume, in_id,
	       irq >= 0 ? "interruption MSI" : "sondage");
	u64 next_jack = now_ms() + 500, next_check = now_ms() + 2000, last_hw = 0;
	int stalls = 0;
	for (;;) {
		if (irq >= 0)
			korin_sys2(SYS_IRQ_WAIT, irq, 5);
		else
			hd_sleep(4);
		irq_ack();
		if (!immediate)
			rirb_drain(0);
		if (unsol_n) {
			for (int i = 0; i < unsol_n; i++)
				codec_unsol(cod, unsol_q[i]);
			unsol_n = 0;
		}
		out_service();
		if (sd_in >= 0)
			in_service();
		u64 t = now_ms();
		if (t >= next_jack) {
			next_jack = t + 500;
			codec_jacks(cod, 0);
		}
		if (t >= next_check) { /* le DMA avance-t-il ? */
			next_check = t + 2000;
			if (out_hw == last_hw) {
				if (++stalls == 1)
					hd_log("le flux de sortie n'avance pas (LPIB %x, CTL %02x, STS %02x)\n",
					       rd32(SD(sd_out, SD_LPIB)), rd8(SD(sd_out, SD_CTL)), rd8(SD(sd_out, SD_STS)));
			} else
				stalls = 0;
			last_hw = out_hw;
		}
		sndif_tick();
	}
}
