/* codec.c : les codecs HDA (docs/etude/SON-PC.md 3-4), générique d'après la norme HDA 1.0a :
 * groupe de fonctions audio, widgets, pins, chemins pin -> DAC et ADC -> pin, amplis, prise
 * casque ; plus les particularités des Realtek ALC285 de HP (décrites dans l'étude, réécrites). */
#include "hda.h"

static u32 param(struct codec *c, int nid, int p)
{
	return hd_cmd(c->addr, nid, V_GET_PARAM, (u32)p);
}

static struct widget *wid(struct codec *c, int nid)
{
	int i = nid - c->wstart;
	return i >= 0 && i < c->nw ? &c->w[i] : 0;
}

static const char *dev_name(int dev)
{
	static const char *n[16] = {"sortie de ligne", "haut-parleur", "casque", "CD", "S/PDIF", "numérique",
				    "modem (ligne)", "modem (combiné)", "entrée de ligne", "aux", "micro", "téléphone",
				    "S/PDIF (entrée)", "numérique (entrée)", "?", "autre"};
	return n[dev & 15];
}

/* liste des connexions d'un widget (formes courte et longue, plages) */
static void read_conns(struct codec *c, struct widget *w)
{
	w->nconn = 0;
	if (!(w->caps & WC_CONN))
		return;
	u32 l = param(c, w->nid, P_CONN_LEN);
	int lng = (l >> 7) & 1, n = (int)(l & 0x7F), per = lng ? 2 : 4, bits = lng ? 16 : 8;
	int prev = 0;
	for (int i = 0; i < n; i += per) {
		u32 r = hd_cmd(c->addr, w->nid, V_GET_CONN_LIST, (u32)i);
		for (int k = 0; k < per && i + k < n; k++) {
			u32 e = (r >> (k * bits)) & ((1u << bits) - 1);
			int range = (int)(e >> (bits - 1)) & 1;
			int nid = (int)(e & ((1u << (bits - 1)) - 1));
			if (range && prev) /* plage prev+1 .. nid */
				for (int x = prev + 1; x <= nid && w->nconn < MAX_CONN; x++)
					w->conn[w->nconn++] = (u8)x;
			else if (w->nconn < MAX_CONN)
				w->conn[w->nconn++] = (u8)nid;
			prev = nid;
		}
	}
}

int codec_probe(struct codec *c, int addr)
{
	memset(c, 0, sizeof *c);
	c->addr = addr;
	c->vendor = param(c, 0, P_VENDOR);
	if (c->vendor == 0xFFFFFFFF)
		return 0;
	c->rev = param(c, 0, P_REVISION);
	u32 nodes = param(c, 0, P_NODES);
	int start = (int)(nodes >> 16) & 0xFF, cnt = (int)nodes & 0xFF;
	for (int fg = start; fg < start + cnt; fg++)
		if ((param(c, fg, P_FG_TYPE) & 0xFF) == 1) {
			c->afg = (u8)fg;
			break;
		}
	u16 vv = (u16)(c->vendor >> 16), dd = (u16)c->vendor;
	if (vv == 0x10ec)
		snprintf(c->name, sizeof c->name, "ALC%x", dd);
	else if (vv == 0x8086)
		snprintf(c->name, sizeof c->name, "Intel %04x", dd);
	else if (vv == 0x10de)
		snprintf(c->name, sizeof c->name, "NVIDIA %04x", dd);
	else
		snprintf(c->name, sizeof c->name, "%04x:%04x", vv, dd);
	if (!c->afg) {
		hd_log("codec %d : %04x:%04x (%s), sans groupe audio\n", addr, vv, dd, c->name);
		return 0;
	}
	hd_cmd(c->addr, c->afg, V_SET_POWER, 0); /* D0 */
	hd_sleep(10);
	c->subsys = hd_cmd(c->addr, c->afg, V_GET_SUBSYS, 0);
	c->afg_pcm = param(c, c->afg, P_PCM);
	c->afg_amp_in = param(c, c->afg, P_AMP_IN);
	c->afg_amp_out = param(c, c->afg, P_AMP_OUT);
	c->gpio = param(c, c->afg, P_GPIO);
	nodes = param(c, c->afg, P_NODES);
	c->wstart = (u8)(nodes >> 16);
	c->nw = (u8)(nodes & 0xFF);
	if (c->nw > MAX_WIDGETS)
		c->nw = MAX_WIDGETS;
	for (int i = 0; i < c->nw; i++) {
		struct widget *w = &c->w[i];
		w->nid = (u8)(c->wstart + i);
		w->caps = param(c, w->nid, P_WCAPS);
		w->type = (u8)((w->caps >> 20) & 0xF);
		if (w->caps & WC_IN_AMP)
			w->amp_in = (w->caps & WC_AMP_OVR) ? param(c, w->nid, P_AMP_IN) : c->afg_amp_in;
		if (w->caps & WC_OUT_AMP)
			w->amp_out = (w->caps & WC_AMP_OVR) ? param(c, w->nid, P_AMP_OUT) : c->afg_amp_out;
		if (w->type == W_PIN) {
			w->pincap = param(c, w->nid, P_PINCAP);
			w->config = hd_cmd(c->addr, w->nid, V_GET_CONFIG, 0);
		}
		if (w->type == W_OUT || w->type == W_IN) {
			w->pcm = param(c, w->nid, P_PCM);
			if (!w->pcm)
				w->pcm = c->afg_pcm;
		}
		read_conns(c, w);
		if (w->caps & WC_POWER)
			hd_cmd(c->addr, w->nid, V_SET_POWER, 0);
	}
	hd_log("codec %d : %04x:%04x (%s), révision %x, sous-système %04x:%04x, AFG %02x, %d widgets (%02x..), "
	       "GPIO %d\n", addr, vv, dd, c->name, c->rev, c->subsys >> 16, c->subsys & 0xFFFF, c->afg, c->nw,
	       c->wstart, c->gpio & 0xFF);
	return 1;
}

/* ---- chemins */

/* du widget nid vers un widget du type voulu (W_OUT : DAC analogique ; ou le nid « target ») ;
 * profondeur au plus MAX_PATH ; prefer_free : DAC pas encore pris d'abord */
static int dfs(struct codec *c, int nid, int type, int target, int depth, struct path *p, int prefer_free)
{
	struct widget *w = wid(c, nid);
	if (!w || depth >= MAX_PATH)
		return 0;
	p->nid[depth] = (u8)nid;
	p->n = depth + 1;
	if (depth > 0) {
		if (target ? nid == target : (w->type == type && !(w->caps & WC_DIGITAL) && !(prefer_free && w->used)))
			return 1;
		if (w->type != W_MIXER && w->type != W_SEL) /* (seuls les mixers et sélecteurs au milieu) */
			return 0;
	}
	for (int i = 0; i < w->nconn; i++) {
		p->idx[depth] = (u8)i;
		if (dfs(c, w->conn[i], type, target, depth + 1, p, prefer_free))
			return 1;
	}
	return 0;
}

/* gain du pas « 0 dB » (offset) d'un ampli, borné au nombre de pas */
static u32 amp_0db(u32 caps)
{
	u32 off = caps & 0x7F, steps = (caps >> 8) & 0x7F;
	return off > steps ? steps : off;
}

/* ampli : out (1) ou in (0), index, ouvert à 0 dB (mute 0) ou fermé */
static void amp_set(struct codec *c, struct widget *w, int out, int index, int open)
{
	u32 caps = out ? w->amp_out : w->amp_in;
	u32 v = (out ? 0x8000 : 0x4000) | 0x3000 | (u32)(index & 15) << 8;
	if (open)
		v |= amp_0db(caps);
	else
		v |= 0x80 | amp_0db(caps);
	hd_cmd(c->addr, w->nid, V4_AMP, v);
}

/* le chemin réglé : sélecteurs, entrées des mixers (la voulue ouverte, les autres fermées), amplis */
static void path_setup(struct codec *c, const struct path *p)
{
	for (int i = 0; i < p->n; i++) {
		struct widget *w = wid(c, p->nid[i]);
		if (!w)
			continue;
		if (i + 1 < p->n) {
			int k = p->idx[i];
			if (w->type == W_MIXER) {
				if (w->caps & WC_IN_AMP)
					for (int j = 0; j < w->nconn; j++)
						amp_set(c, w, 0, j, j == k);
			} else if (w->nconn > 1)
				hd_cmd(c->addr, w->nid, V_SET_CONN_SEL, (u32)k);
			if (w->type != W_MIXER && (w->caps & WC_IN_AMP))
				amp_set(c, w, 0, k, 1);
		} else if (w->type == W_PIN && (w->caps & WC_IN_AMP))
			amp_set(c, w, 0, 0, 1); /* pin d'entrée au bout : son ampli (gain du micro) à 0 dB */
		if (w->caps & WC_OUT_AMP)
			amp_set(c, w, 1, 0, 1);
		if (w->caps & WC_POWER)
			hd_cmd(c->addr, w->nid, V_SET_POWER, 0);
	}
}

static void path_text(const struct path *p, char *b, int n)
{
	int k = 0;
	b[0] = 0;
	for (int i = 0; i < p->n && k < n - 6; i++)
		k += snprintf(b + k, (size_t)(n - k), "%s%02x", i ? " " : "", p->nid[i]);
}

/* ---- particularités des ALC285 de HP (docs/etude/SON-PC.md 4) */

static u32 coef_read(struct codec *c, int idx)
{
	hd_cmd(c->addr, 0x20, V4_COEF_INDEX, (u32)idx);
	return hd_cmd(c->addr, 0x20, V4_GET_COEF, 0) & 0xFFFF;
}

static void coef_write(struct codec *c, int idx, u32 v)
{
	hd_cmd(c->addr, 0x20, V4_COEF_INDEX, (u32)idx);
	hd_cmd(c->addr, 0x20, V4_SET_COEF, v & 0xFFFF);
}

static void coef_update(struct codec *c, int idx, u32 mask, u32 val)
{
	u32 v = coef_read(c, idx);
	u32 nv = (v & ~mask) | val;
	coef_write(c, idx, nv);
	hd_log("ALC285 : coefficient %02x : %04x -> %04x\n", idx, v, nv);
}

static void quirks(struct codec *c)
{
	if (!opt_quirks || c->vendor != 0x10ec0285)
		return;
	coef_update(c, 0x36, 1u << 13, 0); /* EAPD tenu par le pin (générique des ALC285) */
	coef_update(c, 0x10, 1u << 9, 0);
	if (c->subsys == 0x103c8870 || c->subsys == 0x103c886d) { /* ZBook Fury G8 (15,6", 17,3") */
		hd_sleep(100);
		coef_write(c, 0x65, 0); /* initialisation de l'ampli des haut-parleurs */
		hd_log("ALC285 : ZBook Fury G8 (%04x:%04x) : coefficient 65 = 0\n", c->subsys >> 16, c->subsys & 0xFFFF);
	}
	if ((c->subsys >> 16) == 0x103c && (c->gpio & 0xFF) >= 3) { /* LED muet (GPIO 2) et micro coupé (GPIO 0) */
		hd_cmd(c->addr, c->afg, V_SET_GPIO_MASK, 0x05);
		hd_cmd(c->addr, c->afg, V_SET_GPIO_DIR, 0x05);
		hd_cmd(c->addr, c->afg, V_SET_GPIO_DATA, 0);
		hd_log("ALC285 : GPIO 0 et 2 (LED) en sortie, éteints\n");
	}
}

/* ---- sorties et entrée */

int codec_setup(struct codec *c)
{
	if (!c->afg)
		return 0;
	quirks(c);
	/* pins de sortie : haut-parleurs, casque, sortie de ligne ; les pins numériques au journal */
	c->nop = 0;
	int ndig = 0;
	for (int pass = 0; pass < 2; pass++) /* casque et haut-parleurs d'abord (DAC à eux), puis les lignes */
		for (int i = 0; i < c->nw && c->nop < MAX_OPINS; i++) {
			struct widget *w = &c->w[i];
			if (w->type != W_PIN || !(w->pincap & PC_OUT))
				continue;
			int conn = (int)(w->config >> 30), dev = (int)(w->config >> 20) & 0xF;
			if (conn == 1) /* aucune connexion */
				continue;
			if (dev == DEV_SPDIF || dev == DEV_DIG_OUT || (w->caps & WC_DIGITAL)) {
				if (pass == 0) {
					ndig++;
					hd_log("pin %02x : %s (config %08x) : numérique (HDMI/DP), pas servi\n", w->nid,
					       dev_name(dev), w->config);
				}
				continue;
			}
			int prim = dev == DEV_SPEAKER || dev == DEV_HP;
			if (dev != DEV_SPEAKER && dev != DEV_HP && dev != DEV_LINE_OUT)
				continue;
			if (prim != (pass == 0))
				continue;
			struct opin *o = &c->op[c->nop];
			memset(o, 0, sizeof *o);
			if (!dfs(c, w->nid, W_OUT, 0, 0, &o->p, 1) && !dfs(c, w->nid, W_OUT, 0, 0, &o->p, 0)) {
				hd_log("pin %02x : %s : aucun DAC atteint\n", w->nid, dev_name(dev));
				continue;
			}
			o->nid = w->nid;
			o->dev = (u8)dev;
			o->jack = conn == 0 || conn == 3;
			o->sense = o->jack && (w->pincap & PC_PRESENCE) && !((w->config >> 8) & 1); /* (bit 8 : pas de détection) */
			if (dev == DEV_LINE_OUT && !o->jack)
				o->dev = DEV_SPEAKER; /* sortie de ligne fixe : des haut-parleurs */
			struct widget *dac = wid(c, o->p.nid[o->p.n - 1]);
			dac->used = 1;
			char t[48];
			path_text(&o->p, t, sizeof t);
			hd_log("pin %02x : %s%s%s%s (config %08x) -> DAC %02x (chemin %s)\n", w->nid, dev_name(o->dev),
			       o->jack ? " en prise" : " fixe", o->sense ? ", détection" : "",
			       (w->pincap & PC_EAPD) ? ", EAPD" : "", w->config, dac->nid, t);
			c->nop++;
		}
	if (!c->nop) {
		hd_log("codec %d (%s) : aucune sortie analogique%s\n", c->addr, c->name,
		       ndig ? " (HDMI/DP seulement : pas encore servi)" : "");
		return 0;
	}
	/* fréquences communes des DAC */
	c->rate_mask = 0xFFFFFFFF;
	for (int i = 0; i < c->nop; i++)
		c->rate_mask &= wid(c, c->op[i].p.nid[c->op[i].p.n - 1])->pcm;
	for (int i = 0; i < c->nop; i++)
		path_setup(c, &c->op[i].p);
	/* entrée : micro en prise, sinon micro fixe, sinon entrée de ligne ; vers un ADC analogique */
	c->cap.n = 0;
	int best = -1, bestrank = 0;
	for (int i = 0; i < c->nw; i++) {
		struct widget *w = &c->w[i];
		if (w->type != W_PIN || !(w->pincap & PC_IN) || (w->caps & WC_DIGITAL))
			continue;
		int conn = (int)(w->config >> 30), dev = (int)(w->config >> 20) & 0xF;
		if (conn == 1)
			continue;
		int rank = dev == DEV_MIC ? (conn != 2 ? 3 : 2) : dev == DEV_LINE_IN ? 1 : 0;
		if (rank > bestrank) {
			bestrank = rank;
			best = i;
		}
	}
	if (best >= 0) {
		struct widget *pin = &c->w[best];
		for (int i = 0; i < c->nw && !c->cap.n; i++) {
			struct widget *a = &c->w[i];
			if (a->type != W_IN || (a->caps & WC_DIGITAL))
				continue;
			struct path p;
			if (dfs(c, a->nid, 0, pin->nid, 0, &p, 0))
				c->cap = p;
		}
		if (c->cap.n) {
			int dev = (int)(pin->config >> 20) & 0xF, conn = (int)(pin->config >> 30);
			c->cap_pin = pin->nid;
			c->cap_dev = (u8)dev;
			c->cap_jack = conn == 0 || conn == 3;
			c->cap_rate_mask = c->w[c->cap.nid[0] - c->wstart].pcm;
			path_setup(c, &c->cap);
			u32 vref = (pin->pincap >> 8) & 0xFF;
			hd_cmd(c->addr, pin->nid, V_SET_PIN_CTL, PINCTL_IN | (dev == DEV_MIC && (vref & 0x04) ? PINCTL_VREF80 : 0));
			char t[48];
			path_text(&c->cap, t, sizeof t);
			hd_log("entrée : pin %02x (%s%s, config %08x) -> ADC %02x (chemin %s)\n", pin->nid, dev_name(dev),
			       c->cap_jack ? " en prise" : " fixe", pin->config, c->cap.nid[0], t);
		} else
			hd_log("entrée : pin %02x : aucun ADC ne l'atteint\n", pin->nid);
	}
	/* EAPD, détection de présence (événements non sollicités : étiquette = nid) */
	for (int i = 0; i < c->nop; i++) {
		struct opin *o = &c->op[i];
		struct widget *w = wid(c, o->nid);
		if (w->pincap & PC_EAPD)
			hd_cmd(c->addr, o->nid, V_SET_EAPD, 0x02);
		if (o->sense)
			hd_cmd(c->addr, o->nid, V_SET_UNSOL, 0x80 | (o->nid & 0x3F));
	}
	if (opt_gpio) { /* essai : ces GPIO en sortie, à 1 (option hda_gpio=) */
		hd_cmd(c->addr, c->afg, V_SET_GPIO_MASK, opt_gpio);
		hd_cmd(c->addr, c->afg, V_SET_GPIO_DIR, opt_gpio);
		hd_cmd(c->addr, c->afg, V_SET_GPIO_DATA, opt_gpio);
		hd_log("GPIO %02x en sortie, à 1 (hda_gpio)\n", opt_gpio);
	}
	return 1;
}

/* les DAC des chemins : flux « tag », canal 0, format */
void codec_start_out(struct codec *c, int tag, u16 fmt)
{
	for (int i = 0; i < c->nop; i++) {
		int dac = c->op[i].p.nid[c->op[i].p.n - 1];
		hd_cmd(c->addr, dac, V4_FORMAT, fmt);
		hd_cmd(c->addr, dac, V_SET_STREAM, (u32)(tag << 4));
	}
	codec_jacks(c, 1);
}

void codec_start_in(struct codec *c, int tag, u16 fmt)
{
	if (!c->cap.n)
		return;
	int adc = c->cap.nid[0];
	hd_cmd(c->addr, adc, V4_FORMAT, fmt);
	hd_cmd(c->addr, adc, V_SET_STREAM, tag ? (u32)(tag << 4) : 0);
}

static int sense(struct codec *c, struct opin *o)
{
	struct widget *w = wid(c, o->nid);
	if (w->pincap & PC_TRIGGER)
		hd_cmd(c->addr, o->nid, V_EXEC_SENSE, 0);
	u32 r = hd_cmd(c->addr, o->nid, V_GET_SENSE, 0);
	return r != 0xFFFFFFFF && (r >> 31);
}

/* présence des prises, pins allumés ou coupés : casque (ou ligne) branché -> haut-parleurs coupés */
int codec_jacks(struct codec *c, int force)
{
	int changed = force, jack_in = 0;
	for (int i = 0; i < c->nop; i++) {
		struct opin *o = &c->op[i];
		int pr = o->sense ? sense(c, o) : 1;
		if (pr != o->present || force) {
			if (o->sense && pr != o->present)
				hd_log("prise %02x (%s) : %s\n", o->nid, dev_name(o->dev), pr ? "branchée" : "débranchée");
			changed |= pr != o->present;
			o->present = (u8)pr;
		}
		if (o->sense && pr)
			jack_in = 1;
	}
	if (!changed)
		return 0;
	for (int i = 0; i < c->nop; i++) {
		struct opin *o = &c->op[i];
		int on = o->dev == DEV_SPEAKER ? !jack_in : o->present;
		u32 ctl = on ? PINCTL_OUT | (o->dev == DEV_HP ? PINCTL_HP : 0) : 0;
		hd_cmd(c->addr, o->nid, V_SET_PIN_CTL, ctl);
		if (on != o->on || force)
			hd_log("pin %02x (%s) : %s\n", o->nid, dev_name(o->dev), on ? "allumé" : "coupé");
		o->on = (u8)on;
	}
	return 1;
}

void codec_unsol(struct codec *c, u32 resp)
{
	(void)resp; /* (étiquette 31:26 : le nid ; on relit toutes les prises) */
	codec_jacks(c, 0);
}
