/* AudioKit en C : synthé MIDI (réécriture de ak_synth.inc ; voir ak_synth.h).
 *
 * MIDI : note, contrôleurs 1, 6, 7, 10, 11, 38, 64, 91, 93, 100, 101, 120, 121,
 * 123, programme, molette de hauteur (étendue par le RPN 0), banque (CC 0).
 * Les calculs entiers suivent les registres de l'asm (32 bits : u32/s32,
 * débordements compris) ; le flottant suit l'ordre de ses instructions (pas
 * d'opération fusionnée dans ce module). Une SoundFont abîmée est lue comme
 * l'asm la lit : aucun indice du fichier n'est borné (comme l'asm). */
#include "ak_synth.h"
#include "ak_fm.h"

#define G_INSTRUMENT 41
#define G_KEYRANGE 43
#define G_VELRANGE 44
#define G_SAMPLEID 53
#define SF_NGEN 61

/* lectures dans le fichier (adresses quelconques, comme l'asm) */
static inline const u8 *at(const u8 *b, u64 off)
{
	return (const u8 *)((uintptr_t)b + off);
}
static inline u32 rd16(const u8 *p)
{
	return ak_rd16le(p);
}
static inline u32 rd32(const u8 *p)
{
	return ak_rd32le(p);
}
/* s16 rangé dans une table de générateurs (ldrsh) */
static inline s32 gs(const s16 *t, u64 i)
{
	return t[i];
}

/* ak_concave[i] (ldrsh) : au-delà de la table, les octets qui suivent dans
 * l'asm (ak_gm_prog, ak_gm_drum, ak_gm_ins) ; plus loin : 0 (l'asm lirait son
 * code : écart voulu, 0 en C) */
static s32 concave(u64 i)
{
	if (i < 128)
		return ak_gmtab.concave[i];
	if (i < sizeof ak_gmtab / 2) {
		s16 v;
		__builtin_memcpy(&v, (const u8 *)&ak_gmtab + 2 * i, 2);
		return v;
	}
	return 0;
}

static inline struct sy_chan *chan_of(struct aksy *s, u64 i)
{
	return &s->chan[i];
}
static inline u64 chan_index(struct aksy *s, struct sy_chan *c)
{
	return (u64)(c - s->chan);
}

/* ---------------------------------------------------------------------------
 *  Création
 * --------------------------------------------------------------------------- */

static void *rv_new(void);
#define CH_LEN 2048
#define CH_SIZE (32 + CH_LEN * 4 + 4)

static void sy_channels_reset(struct aksy *s)
{
	for (int i = 0; i < SY_NCH; i++) {
		struct sy_chan *c = &s->chan[i];
		c->prog = 0;
		c->bank = 0;
		c->vol = 100;
		c->expr = 127;
		c->pan = 64;
		c->bend = 0;
		c->range = 200;
		c->sust = 0;
		c->mod = 0;
		c->rpn = 0x3FFF;
		c->rev = 40;
		c->cho = 0;
		c->preset = 0;
	}
}

/* SoundFont (0 : celle du système, ak_sf_default), AKS_* -> synthé, < 0 */
long ak_synth_new(struct aksf *sf, long flags)
{
	if (!sf && !(flags & AKS_FM))
		sf = (struct aksf *)ak_sf_default();
	if (flags & AKS_FM)
		sf = 0;
	struct aksy *s = ak_alloc(SY_SIZE);
	if (!s)
		return AKE_NOMEM;
	s->sf = sf;
	s->bpos = SY_BLOCK;
	s->gain = 0.5f;
	sy_channels_reset(s);
	if (!sf) {
		long fm = ak_fm_new(SY_NFM);
		if (fm < 0) {
			ak_free(s);
			return fm;
		}
		s->fm = (struct akfm *)fm;
	} else if (!(flags & AKS_NOFX)) {
		s->rvb = rv_new();	      /* (0 : sans réverbération) */
		s->cho = ak_alloc(CH_SIZE); /* (0 : sans chorus) */
	}
	return (long)s;
}

long ak_synth_free(struct aksy *s)
{
	if (!s)
		return 0;
	if (s->fm)
		ak_fm_free(s->fm);
	ak_free(s->rvb);
	ak_free(s->cho);
	ak_free(s);
	return 0;
}

/* toutes les voix coupées */
static void sy_hush(struct aksy *s)
{
	if (s->fm) {
		ak_fm_silence(s->fm);
		for (int i = 0; i < SY_NFM; i++)
			s->fmv[i].state = 0;
	}
	for (int i = 0; i < SY_NV; i++)
		s->voices[i].state = 0;
}

long ak_synth_reset(struct aksy *s)
{
	fm_lock(s);
	sy_hush(s);
	sy_channels_reset(s);
	fm_unlock(s);
	return 0;
}

long ak_synth_voices(struct aksy *s)
{
	long n = 0;
	if (s->fm) {
		struct akfm *fm = s->fm;
		for (u64 i = 0; i < fm->nv; i++)
			n += fm_voice_active(&fm->voices[i]);
		return n;
	}
	for (int i = 0; i < SY_NV; i++)
		n += s->voices[i].state;
	return n;
}

/* ---------------------------------------------------------------------------
 *  Banque FM
 * --------------------------------------------------------------------------- */

/* touche -> milli-Hz, molette comprise */
static long fmb_freq(struct sy_chan *c, u32 key)
{
	u32 w = (key - 69) * 100;
	w += (u32)((s32)((u32)c->bend * c->range) >> 13);
	return ak_cents_mul((s32)w, 440000);
}

/* vélocité -> volume 0..255 */
static u32 fmb_gain(struct sy_chan *c, u32 vel)
{
	u32 w = vel * c->vol * c->expr / 16129; /* 0..127 */
	w = w * w / 127;
	return w << 1;
}

static u32 fmb_pan(struct sy_chan *c)
{
	u32 p = c->pan << 1;
	return p > 255 ? 255 : p;
}

static void fmb_note_on(struct aksy *s, struct sy_chan *c, u32 key, u32 vel)
{
	u64 ch = chan_index(s, c);
	u32 played = key, ins;
	if (ch == 9) { /* percussions */
		if (key < 35 || key > 81)
			return;
		ins = ak_gmtab.drum[key - 35][0];
		played = ak_gmtab.drum[key - 35][1];
	} else
		ins = ak_gmtab.prog[c->prog];
	/* une voix : libre, sinon la plus ancienne relâchée, sinon la plus ancienne */
	struct akfm *fm = s->fm;
	u64 best = (u64)-1, i;
	u32 age = 0xFFFFFFFF, brel = 0;
	for (i = 0; i < SY_NFM; i++) {
		if (!fm_voice_active(&fm->voices[i]))
			break;
		u32 r = s->fmv[i].state == 3;
		u32 a = s->fmv[i].age;
		if (r > brel || (r == brel && a < age)) {
			best = i;
			age = a;
			brel = r;
		}
	}
	if (i == SY_NFM)
		i = best;
	/* (best = -1 : jamais, il faudrait 32 voix d'âge 0xFFFFFFFF ; l'asm
	 * écrirait 16 octets avant SY_FMV : même adresse ici) */
	struct sy_fmv *v = (struct sy_fmv *)((u8 *)s + SY_FMV + (i << 4));
	v->chan = (u8)ch;
	v->key = (u8)key;
	v->played = (u8)played;
	v->vel = (u8)vel;
	v->state = 1;
	s->age++;
	v->age = (u32)s->age;
	if (v->ins != ins + 1) {
		v->ins = ins + 1;
		ak_fm_instrument(fm, (long)i, ak_gmtab.ins[ins]);
	}
	ak_fm_pan(fm, (long)i, fmb_pan(c));
	long f = fmb_freq(c, played);
	ak_fm_start(fm, (long)i, f, AKW_FM, fmb_gain(c, vel));
}

static void fmb_note_off(struct aksy *s, struct sy_chan *c, u32 key)
{
	u64 ch = chan_index(s, c);
	for (u64 i = 0; i < SY_NFM; i++) {
		struct sy_fmv *v = &s->fmv[i];
		if (v->state != 1 || v->chan != ch || v->key != key)
			continue;
		if (c->sust) {
			v->state = 2;
			continue;
		}
		v->state = 3;
		ak_fm_stop(s->fm, (long)i);
	}
}

/* molette, volume, panoramique aux voix qui sonnent */
static void fmb_refresh(struct aksy *s, struct sy_chan *c)
{
	u64 ch = chan_index(s, c);
	for (u64 i = 0; i < SY_NFM; i++) {
		struct sy_fmv *v = &s->fmv[i];
		if (!v->state || v->state == 3 || v->chan != ch)
			continue;
		ak_fm_pitch(s->fm, (long)i, fmb_freq(c, v->played));
		ak_fm_pan(s->fm, (long)i, fmb_pan(c));
		ak_fm_gain(s->fm, (long)i, fmb_gain(c, v->vel));
	}
}

/* état à relâcher : 2 pédale, 1 toutes */
static void fmb_release(struct aksy *s, struct sy_chan *c, u32 min)
{
	u64 ch = chan_index(s, c);
	for (u64 i = 0; i < SY_NFM; i++) {
		struct sy_fmv *v = &s->fmv[i];
		if (!v->state || v->state == 3 || v->state < min || v->chan != ch)
			continue;
		v->state = 3;
		ak_fm_stop(s->fm, (long)i);
	}
}

/* ---------------------------------------------------------------------------
 *  Aiguillage des moteurs
 * --------------------------------------------------------------------------- */

static void sf_note_on(struct aksy *s, struct sy_chan *c, u32 key, u32 vel, struct aksf *sf);
static void sv_release(struct sy_voice *v);

static void sy_note_on(struct aksy *s, struct sy_chan *c, u32 key, u32 vel)
{
	if (s->sf)
		sf_note_on(s, c, key, vel, s->sf);
	else
		fmb_note_on(s, c, key, vel);
}

static void sy_note_off(struct aksy *s, struct sy_chan *c, u32 key)
{
	if (!s->sf) {
		fmb_note_off(s, c, key);
		return;
	}
	u64 ch = chan_index(s, c);
	u32 sust = c->sust;
	for (int i = 0; i < SY_NV; i++) {
		struct sy_voice *v = &s->voices[i];
		if (!v->state || v->rel || v->chan != ch || v->key != key)
			continue;
		if (sust)
			v->rel = 2;
		else
			sv_release(v);
	}
}

static void sy_refresh(struct aksy *s, struct sy_chan *c)
{
	if (!s->sf)
		fmb_refresh(s, c);
}

static void sy_pedal_up(struct aksy *s, struct sy_chan *c)
{
	if (!s->sf) {
		fmb_release(s, c, 2);
		return;
	}
	u64 ch = chan_index(s, c);
	for (int i = 0; i < SY_NV; i++) {
		struct sy_voice *v = &s->voices[i];
		if (v->state && v->rel == 2 && v->chan == ch)
			sv_release(v);
	}
}

static void sy_all_off(struct aksy *s, struct sy_chan *c)
{
	if (!s->sf) {
		fmb_release(s, c, 1);
		return;
	}
	u64 ch = chan_index(s, c);
	for (int i = 0; i < SY_NV; i++) {
		struct sy_voice *v = &s->voices[i];
		if (v->state && v->rel != 1 && v->chan == ch)
			sv_release(v);
	}
}

static void sy_channel_cut(struct aksy *s, struct sy_chan *c)
{
	if (!s->sf) {
		fmb_release(s, c, 1);
		return;
	}
	u64 ch = chan_index(s, c);
	for (int i = 0; i < SY_NV; i++)
		if (s->voices[i].chan == ch)
			s->voices[i].state = 0;
}

static void sy_control(struct aksy *s, struct sy_chan *c, u32 cc, u32 val)
{
	switch (cc) {
	case 1:
		c->mod = val;
		break;
	case 7:
		c->vol = val;
		sy_refresh(s, c);
		break;
	case 10:
		c->pan = val;
		sy_refresh(s, c);
		break;
	case 11:
		c->expr = val;
		sy_refresh(s, c);
		break;
	case 64:
		c->sust = val >= 64;
		if (!c->sust)
			sy_pedal_up(s, c);
		break;
	case 0:
		c->bank = val;
		c->preset = 0;
		break;
	case 6: /* RPN 0 : étendue de la molette (demi-tons) */
		if (!c->rpn)
			c->range = c->range % 100 + val * 100;
		break;
	case 38: /* (cents) */
		if (!c->rpn)
			c->range = c->range / 100 * 100 + val;
		break;
	case 91:
		c->rev = val;
		break;
	case 93:
		c->cho = val;
		break;
	case 100:
		c->rpn = (c->rpn & 0x3F80) | val;
		break;
	case 101:
		c->rpn = (c->rpn & 0x7F) | (val << 7);
		break;
	case 120:
		sy_channel_cut(s, c);
		break;
	case 121:
		c->bend = 0;
		c->mod = 0;
		c->expr = 127;
		c->sust = 0;
		sy_pedal_up(s, c);
		sy_refresh(s, c);
		break;
	case 123:
		sy_all_off(s, c);
		break;
	}
}

/* synthé, canal 0..15, commande (0x80 fin de note, 0x90 note, 0xB0
 * contrôleur, 0xC0 programme, 0xE0 molette ; quatre bits bas ignorés),
 * données -> 0 */
long ak_synth_midi(struct aksy *s, u64 ch, u64 cmd, u64 d1, u64 d2)
{
	u32 op = (u32)cmd & 0xF0, a = (u32)d1 & 127, b = (u32)d2 & 127;
	if (ch >= SY_NCH)
		return 0;
	struct sy_chan *c = chan_of(s, ch);
	fm_lock(s);
	if (op == 0x90 && b)
		sy_note_on(s, c, a, b);
	else if (op == 0x90 || op == 0x80)
		sy_note_off(s, c, a);
	else if (op == 0xB0)
		sy_control(s, c, a, b);
	else if (op == 0xC0) {
		c->prog = a;
		c->preset = 0;
	} else if (op == 0xE0) {
		c->bend = (s32)((a | (b << 7)) - 8192);
		sy_refresh(s, c);
	}
	fm_unlock(s);
	return 0;
}

/* ---------------------------------------------------------------------------
 *  SoundFont : lecture du fichier
 * --------------------------------------------------------------------------- */

static void sf_chunk(struct aksf *sf, u32 id, u64 size, const u8 *data)
{
	switch (id) {
	case 0x4D414E49: { /* 'INAM' */
		u64 n = size < 63 ? size : 63;
		for (u64 i = 0; i < n; i++)
			sf->name[i] = (char)data[i];
		break;
	}
	case 0x6C706D73: /* 'smpl' */
		sf->smpl = data;
		sf->nsmpl = size >> 1;
		break;
	case 0x72646870: /* 'phdr' */
		sf->phdr = data;
		sf->nphdr = size / 38 - 1;
		break;
	case 0x67616270: /* 'pbag' */
		sf->pbag = data;
		break;
	case 0x6E656770: /* 'pgen' */
		sf->pgen = data;
		break;
	case 0x646F6D70: /* 'pmod' */
		sf->pmod = data;
		break;
	case 0x646F6D69: /* 'imod' */
		sf->imod = data;
		break;
	case 0x74736E69: /* 'inst' */
		sf->inst = data;
		break;
	case 0x67616269: /* 'ibag' */
		sf->ibag = data;
		break;
	case 0x6E656769: /* 'igen' */
		sf->igen = data;
		break;
	case 0x72646873: /* 'shdr' */
		sf->shdr = data;
		sf->nshdr = size / 46;
		break;
	}
}

long sf_parse(const void *buf, u64 n)
{
	const u8 *f = buf;
	u64 end = (uintptr_t)f + n;
	if (n < 12 || rd32(f) != 0x46464952 || rd32(f + 8) != 0x6B626673) /* 'RIFF' 'sfbk' */
		return AKE_CORRUPT;
	struct aksf *sf = ak_alloc(SF_BYTES);
	if (!sf)
		return AKE_NOMEM;
	u64 p = (uintptr_t)f + 12;
	while (p + 8 <= end) {
		u32 id = rd32((const u8 *)p);
		u64 size = rd32((const u8 *)p + 4);
		if (id == 0x5453494C) { /* 'LIST' */
			u32 t = rd32((const u8 *)p + 8);
			if (t == 0x4F464E49 || t == 0x61746473 || t == 0x61746470) { /* INFO sdta pdta */
				u64 le = p + size + 8; /* fin de la liste (non bornée, comme l'asm) */
				u64 q = p + 12;
				while (q + 8 <= le) {
					u32 sz = rd32((const u8 *)q + 4);
					sf_chunk(sf, rd32((const u8 *)q), sz, (const u8 *)(q + 8));
					q += ((u64)sz + 1) & ~(u64)1;
					q += 8;
				}
				p = (le + 1) & ~(u64)1;
				continue;
			}
		}
		p += (size + 1) & ~(u64)1;
		p += 8;
	}
	if (!sf->smpl || !sf->phdr || !sf->shdr || !sf->igen || !sf->inst || !sf->ibag || !sf->pgen || !sf->pbag) {
		ak_free(sf);
		return AKE_CORRUPT;
	}
	return (long)sf;
}

long ak_sf_open_mem(const void *buf, u64 n)
{
	return sf_parse(buf, n);
}

long ak_sf_name(struct aksf *sf)
{
	return (long)sf->name;
}

long ak_sf_presets(struct aksf *sf)
{
	return (long)sf->nphdr;
}

/* SoundFont, numéro, tampon AKSP_SIZE -> 0 / < 0 */
long ak_sf_preset(struct aksf *sf, u64 i, void *out)
{
	if (i >= sf->nphdr)
		return AKE_INVAL;
	const u8 *h = at(sf->phdr, 38 * i);
	u8 *o = out;
	u32 v = rd16(h + 22);
	__builtin_memcpy(o + AKSP_BANK, &v, 4);
	v = rd16(h + 20);
	__builtin_memcpy(o + AKSP_PROGRAM, &v, 4);
	for (int k = 0; k < 20; k++)
		o[AKSP_NAME + k] = h[k];
	o[AKSP_NAME + 20] = 0;
	return 0;
}

/* ---------------------------------------------------------------------------
 *  SoundFont : note
 * --------------------------------------------------------------------------- */

/* -> preset + 1 (0 : aucun) */
static u32 sf_find_preset(struct aksy *s, struct sy_chan *c)
{
	if (c->preset)
		return c->preset;
	struct aksf *sf = s->sf;
	u32 prog = c->prog, bank = c->bank;
	if (chan_index(s, c) == 9) /* canal 10 : les percussions (banque 128) */
		bank = 128;
	const u8 *h = sf->phdr;
	u64 n = sf->nphdr;
	u32 r = 0, fb0 = 0, fbd = 0; /* replis : même programme, banque 0 ; premier de la banque */
	u64 i;
	for (i = 0; i < n; i++, h = at(h, 38)) {
		u32 hp = rd16(h + 20), hb = rd16(h + 22);
		if (hb == bank) {
			if (hp == prog) {
				r = (u32)i + 1;
				goto st;
			}
			if (!fbd)
				fbd = (u32)i + 1;
		} else if (!hb && hp == prog)
			fb0 = (u32)i + 1;
	}
	if (bank == 128) /* (percussions : un kit d'abord) */
		fb0 = fbd;
	r = fb0;
st:
	c->preset = r;
	return r;
}

/* générateurs -> t[oper] = montant (l'ordre du fichier : le dernier gagne) */
static void sf_apply(const u8 *g, u64 n, s16 *t)
{
	for (; n; n--, g = at(g, 4)) {
		u32 o = rd16(g);
		if (o < SF_NGEN)
			t[o] = (s16)rd16(g + 2);
	}
}

/* -> 1 si la zone convient (toutes les étendues) */
static int sf_zone_ok(const u8 *g, u64 n, u32 key, u32 vel)
{
	for (; n; n--, g = at(g, 4)) {
		u32 o = rd16(g);
		u32 x = o == G_KEYRANGE ? key : o == G_VELRANGE ? vel : 0x100;
		if (x != 0x100 && (x < g[2] || x > g[3]))
			return 0;
	}
	return 1;
}

static u32 sf_cc_value(struct sy_chan *c, u32 cc)
{
	switch (cc) {
	case 7:
		return c->vol;
	case 10:
		return c->pan;
	case 11:
		return c->expr;
	case 1:
		return c->mod;
	case 91:
		return c->rev;
	case 93:
		return c->cho;
	}
	return 0;
}

/* modulateurs de la SoundFont vus à l'attaque (sources vélocité, touche,
 * contrôleur ; sans source de quantité ; vers l'atténuation, la coupure,
 * enveloppe -> coupure) ; mask : modulateurs qui les masquent */
static void sf_mods(struct aksy *s, struct sy_chan *c, u32 key, u32 vel, const u8 *m, u64 n, const u8 *mask,
		    u64 nmask)
{
	for (; n; n--, m = at(m, 10)) {
		u32 dst = rd16(m + 2);
		if (dst != 48 && dst != 8 && dst != 11)
			continue;
		u32 amt = rd16(m + 6);
		if (amt & 0xFF)
			continue;
		u32 src = rd16(m);
		const u8 *k = mask;
		u64 j;
		for (j = nmask; j; j--, k = at(k, 10))
			if (rd16(k) == src && rd16(k + 2) == dst && rd16(k + 6) == amt)
				break;
		if (j)
			continue; /* masqué */
		u32 isvel = 0, v;
		if (src & 0x80)
			v = sf_cc_value(c, src & 127);
		else if ((src & 127) == 2) {
			v = vel;
			isvel = 1;
		} else if ((src & 127) == 3)
			v = key;
		else
			continue;
		if (src & 0x100) /* sens */
			v = 127 - v;
		u32 shape = (src >> 10) & 63, w; /* forme : 16.16 sur 0..1 */
		if (shape == 1) {
			w = (u32)concave(127 - v);
			w = (w << 16) / 960;
		} else if (shape == 2) {
			w = 960 - (u32)concave(v);
			w = (w << 16) / 960;
		} else if (shape == 3)
			w = (u32)(v >= 64) << 16;
		else
			w = (v << 16) / 127;
		if (src & 0x200) /* bipolaire */
			w = (w << 1) - 0x10000;
		s64 x = (s64)(s32)w * (s64)(s16)rd16(m + 4);
		u32 d = (u32)(x >> 16);
		if (dst == 48) {
			s->matt = (s32)((u32)s->matt + d);
			if (isvel)
				s->mvel = isvel;
		} else if (dst == 8)
			s->mfc = (s32)((u32)s->mfc + d);
		else
			s->mme = (s32)((u32)s->mme + d);
	}
}

/* timecents -> blocs (1 au moins) */
static u32 sf_tc_blocks(s32 tc)
{
	u64 x = (u64)ak_cents_mul(tc, 689 * 16); /* blocs par seconde, 12.4 */
	x = (x + 8) >> 4;
	if (!x)
		x = 1;
	if (x > 0x7FFFFF)
		x = 0x7FFFFF;
	return (u32)x;
}

static void sf_env_init(struct sy_env *e, s32 delay, s32 attack, s32 hold, s32 decay, u32 sus, s32 release)
{
	e->sus = sus;
	u32 b = sf_tc_blocks(delay);
	if (b <= 1) /* (délai minimal : aucun) */
		b = 0;
	e->delay = b;
	e->count = b;
	b = 0x10000 / sf_tc_blocks(attack);
	e->atts = b ? b : 1;
	b = sf_tc_blocks(hold);
	e->hold = b <= 1 ? 0 : b;
	b = 0x10000 / sf_tc_blocks(decay);
	e->decs = b ? b : 1;
	b = 0x10000 / sf_tc_blocks(release);
	e->rels = b ? b : 1;
	e->lvl = 0;
	e->stage = e->count ? ES_DELAY : ES_ATTACK;
}

/* un bloc de plus -> niveau ; *stage = étape */
static u32 sf_env_step(struct sy_env *e, u32 *stage)
{
	u32 st = e->stage, l = e->lvl;
	switch (st) {
	case ES_DELAY:
		if (e->count-- >= 2) /* (subs, b.hi) */
			goto out;
		st = ES_ATTACK;
		break;
	case ES_ATTACK:
		l += e->atts;
		if (l < 0x10000)
			goto out;
		l = 0x10000;
		e->count = e->hold;
		st = e->hold ? ES_HOLD : ES_DECAY;
		break;
	case ES_HOLD:
		if (e->count-- >= 2)
			goto out;
		st = ES_DECAY;
		break;
	case ES_DECAY:
		l -= e->decs;
		if (l & 0x80000000)
			l = 0;
		if ((s32)l > (s32)e->sus)
			goto out;
		l = e->sus;
		st = ES_SUSTAIN;
		break;
	case ES_RELEASE:
		if ((s32)l > (s32)e->rels) {
			l -= e->rels;
			goto out;
		}
		l = 0;
		st = ES_DONE;
		break;
	default:
		goto out;
	}
	e->stage = st;
out:
	e->lvl = l;
	*stage = st;
	return l;
}

/* relâchement */
static void sv_release(struct sy_voice *v)
{
	v->rel = 1;
	v->me.stage = ES_RELEASE;
	u32 old = v->ve.stage;
	v->ve.stage = ES_RELEASE;
	if (old > ES_ATTACK)
		return;
	/* (attaque : niveau linéaire en amplitude -> linéaire en dB : 96 dB = 16
	 * octaves ; log2 approché par morceaux) */
	u32 l = v->ve.lvl;
	if (!l) {
		v->ve.lvl = 0;
		return;
	}
	u32 z = ak_clz32(l);
	u32 e = 31 - z;
	u32 fr = ((l << z) << 1) >> 16;
	u32 lg = fr + (e << 16) - 0x100000;
	s32 d = (s32)lg >> 4;
	u32 r = 0x10000 + (u32)d;
	v->ve.lvl = (r & 0x80000000) ? 0 : r;
}

/* -> voix libre (sinon la plus ancienne des relâchées, sinon la plus ancienne) */
static struct sy_voice *sf_voice_alloc(struct aksy *s)
{
	struct sy_voice *best = 0;
	u32 brel = 0, age = 0xFFFFFFFF;
	for (int i = 0; i < SY_NV; i++) {
		struct sy_voice *v = &s->voices[i];
		if (!v->state)
			return v;
		u32 r = v->rel == 1;
		if (r > brel || (r == brel && v->age < age)) {
			best = v;
			brel = r;
			age = v->age;
		}
	}
	return best;
}

/* valeurs par défaut des générateurs (SoundFont 2.04, 8.1.3) */
static const s16 sf_defaults[SF_NGEN] = {
	0, 0, 0, 0, 0, 0, 0, 0, 13500, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, -12000, 0, -12000, 0, -12000, -12000, -12000, -12000, 0, -12000, 0,
	0, -12000, -12000, -12000, -12000, 0, -12000, 0, 0, 0, 0, 0, 0, 0, -1, -1,
	0, 0, 0, 0, 0, 0, 0, 0, 100, 0, -1, 0, 0};

/* décalage d'une borne : ig[a] + pg[a] + (ig[b] + pg[b]) << 15 (32 bits) */
static s64 sf_offs(const s16 *pg, const s16 *ig, int a, int b)
{
	u32 w = (u32)(gs(ig, a) + gs(pg, a));
	u32 h = (u32)(gs(ig, b) + gs(pg, b)) << 15;
	return (s32)(w + h);
}

static void sf_voice_start(struct aksy *s, struct sy_chan *c, u32 key, u32 vel, const s16 *pg, const s16 *ig,
			   struct aksf *sf)
{
#define GEN(n) ((s32)((u32)gs(ig, n) + (u32)gs(pg, n)))
	u64 sid = (u16)ig[G_SAMPLEID];
	if (sid >= sf->nshdr)
		return;
	const u8 *sh = at(sf->shdr, 46 * sid); /* en-tête de l'échantillon */
	struct sy_voice *v = sf_voice_alloc(s);
	s->age++;
	u32 age = (u32)s->age;
	/* classe exclusive (générateur 57) : coupe les autres du canal */
	u64 ch = chan_index(s, c);
	u32 ex = (u32)GEN(57);
	if (ex)
		for (int i = 0; i < SY_NV; i++) {
			struct sy_voice *o = &s->voices[i];
			if (o->state && o->excl == ex && o->chan == ch)
				o->state = 0;
		}
	__builtin_memset(v, 0, SV_SIZE);
	v->chan = (u32)ch;
	v->excl = ex;
	v->key = key;
	v->vel = vel;
	v->age = age;
	/* touche et vélocité effectives (générateurs 46, 47) */
	u32 ekey = ig[46] >= 0 ? (u32)ig[46] : key;
	u32 evel = ig[47] >= 0 ? (u32)ig[47] : vel;
	/* bornes : décalages (0..3, 4, 12, 45, 50 : additifs) */
	u64 ns = sf->nsmpl;
	u64 x = (u64)rd32(sh + 20) + (u64)sf_offs(pg, ig, 0, 4);
	if (x >= ns)
		return;
	v->pos = x << 32;
	x = (u64)rd32(sh + 24) + (u64)sf_offs(pg, ig, 1, 12);
	v->end = x > ns ? ns : x;
	v->ls = (u64)rd32(sh + 28) + (u64)sf_offs(pg, ig, 2, 45);
	v->le = (u64)rd32(sh + 32) + (u64)sf_offs(pg, ig, 3, 50);
	u32 mode = 0; /* boucle vide : pas de boucle */
	if (v->le > v->ls) {
		mode = (u16)ig[54] & 3;
		if (mode == 2)
			mode = 0;
	}
	v->mode = mode;
	v->smp = sf->smpl;
	/* hauteur : (touche - racine) x accord + grossier + fin + correction */
	u32 root = ig[58] >= 0 ? (u32)ig[58] : sh[40];
	u32 w = (ekey - root) * (u32)GEN(56);
	w += (u32)GEN(51) * 100;
	w += (u32)gs(ig, 52);
	w += (u32)gs(pg, 52);
	w += (u32)(s32)(s8)sh[41];
	v->cents = (s32)w;
	v->rate = (u32)(((u64)rd32(sh + 36) << 20) / FM_RATE);
	/* modulations (générateurs additifs) */
	v->ml2p = GEN(5);
	v->vl2p = GEN(6);
	v->me2p = GEN(7);
	v->fc = (s32)((u32)GEN(8) + (u32)s->mfc);
	v->q = GEN(9);
	v->ml2f = GEN(10);
	v->me2f = (s32)((u32)GEN(11) + (u32)s->mme);
	v->ml2v = GEN(13);
	v->csend = GEN(15);
	v->rsend = GEN(16);
	v->pan = GEN(17);
	/* atténuation : générateur 48 x 0,4 (usage des SoundFont) + vélocité */
	u32 att = (u32)ak_sdivw((s32)((u32)GEN(48) << 1), 5);
	att += (u32)s->matt;
	if (!s->mvel) /* (la SoundFont remplace le modulateur par défaut) */
		att += (u32)concave(evel);
	s32 q = v->q < 0 ? 0 : v->q; /* un filtre résonant perd la moitié de sa résonance */
	att += (u32)(q >> 1);
	v->att = (s32)att;
	/* LFO */
	v->mldel = sf_tc_blocks(GEN(21));
	v->mlinc = (u32)ak_cents_mul(GEN(22), 5096414);
	v->vldel = sf_tc_blocks(GEN(23));
	v->vlinc = (u32)ak_cents_mul(GEN(24), 5096414);
	/* enveloppe de modulation (25..32) */
	u32 k60 = 60 - key;
	s32 hold = (s32)((u32)GEN(27) + (u32)GEN(31) * k60);
	s32 decay = (s32)((u32)GEN(28) + (u32)GEN(32) * k60);
	s32 sv = 1000 - GEN(29);
	if (sv < 0)
		sv = 0;
	u32 sus = ((u32)sv << 16) / 1000;
	s32 rel = GEN(30), del = GEN(25);
	/* attaque plus courte à forte vélocité : x (145 - v) / 144 */
	u32 a = 145 - vel;
	u32 z = ak_clz32(a);
	u32 lg = 31 - z;
	u32 fr = (a << ((z + 1) & 31)) >> 22; /* 10 bits */
	lg = lg * 1200 + ((fr * 1200) >> 10); /* 1200 log2 (145 - v), droite entre les octaves */
	lg -= 8604;			      /* 1200 log2 144 */
	s32 atk = (s32)((u32)GEN(26) + lg);
	sf_env_init(&v->me, del, atk, hold, decay, sus, rel);
	/* enveloppe de volume (33..40) */
	hold = (s32)((u32)GEN(35) + (u32)GEN(39) * k60);
	decay = (s32)((u32)GEN(36) + (u32)GEN(40) * k60);
	sv = GEN(37);
	if (sv < 0)
		sv = 0;
	sv = (s32)(960 - (u32)sv);
	if (sv < 0)
		sv = 0;
	sus = ((u32)sv << 16) / 960;
	sf_env_init(&v->ve, GEN(33), GEN(34), hold, decay, sus, GEN(38));
	v->lastfc = 0x7FFFFFFF;
	v->state = 1; /* (la voix sonne : dernière écriture) */
#undef GEN
}

static void sf_note_on(struct aksy *s, struct sy_chan *c, u32 key, u32 vel, struct aksf *sf)
{
	s16 pg[64], ig[64];
	u32 p = sf_find_preset(s, c);
	if (!p)
		return;
	const u8 *ph = at(sf->phdr, 38 * (u64)(p - 1));
	u64 pz0 = rd16(ph + 24), pend = rd16(ph + 24 + 38);
	const u8 *pglob = 0, *pmg = 0;
	u64 pgn = 0, pmgn = 0;
	for (u64 pz = pz0; pz < pend; pz++) {
		const u8 *b = at(sf->pbag, pz << 2);
		u64 m0 = rd16(b + 2), m1 = rd16(b + 6);
		u64 pmln = m1 > m0 ? m1 - m0 : 0;
		const u8 *pml = at(sf->pmod, 10 * m0);
		u64 g0 = rd16(b), g1 = rd16(b + 4);
		if (g1 <= g0)
			continue;
		const u8 *gens = at(sf->pgen, g0 << 2);
		u64 ng = g1 - g0;
		const u8 *last = at(gens, (ng << 2) - 4);
		if (rd16(last) != G_INSTRUMENT) {
			if (pz == pz0) { /* pas d'instrument : zone globale */
				pglob = gens;
				pgn = ng;
				pmg = pml;
				pmgn = pmln;
			}
			continue;
		}
		if (!sf_zone_ok(gens, ng, key, vel))
			continue;
		__builtin_memset(pg, 0, 128); /* pg : global puis zone */
		sf_apply(pglob, pgn, pg);
		sf_apply(gens, ng, pg);
		const u8 *ih = at(sf->inst, 22 * (u64)rd16(last + 2));
		u64 iz0 = rd16(ih + 20), iend = rd16(ih + 20 + 22);
		const u8 *iglob = 0, *img = 0;
		u64 ign = 0, imgn = 0;
		for (u64 iz = iz0; iz < iend; iz++) {
			const u8 *ib = at(sf->ibag, iz << 2);
			u64 k0 = rd16(ib + 2), k1 = rd16(ib + 6);
			u64 imln = k1 > k0 ? k1 - k0 : 0;
			const u8 *iml = at(sf->imod, 10 * k0);
			u64 h0 = rd16(ib), h1 = rd16(ib + 4);
			if (h1 <= h0)
				continue;
			const u8 *ig4 = at(sf->igen, h0 << 2);
			u64 n = h1 - h0;
			if (rd16(at(ig4, (n << 2) - 4)) != G_SAMPLEID) {
				if (iz == iz0) {
					iglob = ig4;
					ign = n;
					img = iml;
					imgn = imln;
				}
				continue;
			}
			if (!sf_zone_ok(ig4, n, key, vel))
				continue;
			/* ig : valeurs par défaut, global, zone */
			__builtin_memcpy(ig, sf_defaults, SF_NGEN * 2);
			sf_apply(iglob, ign, ig);
			sf_apply(ig4, n, ig);
			s->mfc = 0; /* modulateurs de cette voix */
			s->mme = 0;
			s->matt = 0;
			s->mvel = 0;
			sf_mods(s, c, key, vel, pmg, pmgn, pml, pmln);
			sf_mods(s, c, key, vel, pml, pmln, 0, 0);
			sf_mods(s, c, key, vel, img, imgn, iml, imln);
			sf_mods(s, c, key, vel, iml, imln, 0, 0);
			sf_voice_start(s, c, key, vel, pg, ig, sf);
		}
	}
}

/* ---------------------------------------------------------------------------
 *  SoundFont : rendu d'une voix
 * --------------------------------------------------------------------------- */

/* phase 32 bits -> triangle -65536..65536 */
static s32 sf_tri(u32 ph)
{
	u32 w = ph >> 14;
	if (w < 0x20000)
		return (s32)(w - 0x10000); /* montée */
	return (s32)(0x30000 - w);
}

static const float F_2M15 = 0x1p-15f, F_2M16 = 0x1p-16f;

/* coupure (cents absolus) : coefficients du filtre (biquad passe-bas ; a0 = 1 ;
 * b2 = b0) ; >= 13500 : sans filtre */
static void sf_filter_set(struct sy_voice *v, u32 fc)
{
	if (fc >= 13500)
		return;
	u64 f = (u64)ak_cents_mul((s32)fc, 8176); /* milli-Hz */
	if (f > 20000000)
		f = 20000000;
	/* angle sur la table : f x 1024 / 44100, 8 bits de fraction */
	f = (f << 18) / 44100000;
	u32 fr = (u32)(f & 255);
	u64 i = (f >> 8) & 1023;
	s32 a = ak_sin1024[i], b = ak_sin1024[(i + 1) & 1023];
	s32 sn = (s32)((u32)a + (u32)((s32)((u32)(b - a) * fr) >> 8));
	u64 j = (i + 256) & 1023;
	a = ak_sin1024[j];
	b = ak_sin1024[(j + 1) & 1023];
	s32 cs = (s32)((u32)a + (u32)((s32)((u32)(b - a) * fr) >> 8));
	float s0 = (float)sn * F_2M15, c1 = (float)cs * F_2M15;
	/* résonance : Q = 0,7071 x 10^(cB / 200) (générateur 9) */
	s32 q = v->q;
	if (q < 0)
		q = 0;
	if (q > 960)
		q = 960;
	q = (s32)((u32)q * 1993 / 100);
	s32 qm = (s32)ak_cents_mul(q, 0x10000);
	float s2 = (float)qm * F_2M16;
	s2 = s2 * ak_u2f(0x3F3504F3); /* 0,7071 : Q minimal */
	float s3 = s2 + s2;
	float al = s0 / s3;
	float a0 = 1.0f + al;
	float b1 = (1.0f - c1) / a0;
	float b0 = b1 * 0.5f;
	float a1 = (c1 + c1) / a0;
	a1 = ak_fneg(a1);
	float a2 = (1.0f - al) / a0;
	v->b0 = b0;
	v->b1 = b1;
	v->a1 = a1;
	v->a2 = a2;
}

static void sf_voice_block(struct aksy *s, struct sy_voice *v)
{
	u32 st, mst;
	u32 menv = sf_env_step(&v->me, &mst); /* enveloppe de modulation */
	u32 lvl = sf_env_step(&v->ve, &st);   /* niveau de volume */
	if (st == ES_DONE || (st >= ES_DECAY && !lvl)) { /* maintien à -96 dB : fini */
		v->state = 0;
		return;
	}
	/* LFO */
	s32 mlfo = 0, vlfo = 0;
	if (v->mldel)
		v->mldel--;
	else {
		v->mlph += v->mlinc;
		mlfo = sf_tri(v->mlph);
	}
	if (v->vldel)
		v->vldel--;
	else {
		v->vlph += v->vlinc;
		vlfo = sf_tri(v->vlph);
	}
	struct sy_chan *c = &s->chan[v->chan];
	/* atténuation (centibels) */
	u32 w = (u32)v->att;
	w += (u32)concave(c->vol);
	w += (u32)concave(c->expr);
	w += (u32)((s32)((u32)v->ml2v * (u32)mlfo) >> 16);
	if (st > ES_ATTACK)
		w += (u32)(((u64)(0x10000 - lvl) * 960) >> 16);
	s32 att = (s32)w;
	if (att < 0)
		att = 0;
	if (att >= 1440) /* muette */
		att = 1440;
	/* 10^(-cB / 200) = 2^(-cB x 19,93 / 1200) */
	s32 cents = -(s32)((u32)att * 1993 / 100);
	float g = (float)(s32)ak_cents_mul(cents, 1L << 24); /* gain 8.24 */
	if (st <= ES_ATTACK) {
		if (st == ES_DELAY)
			g = 0.0f;
		else {
			float l = (float)(s32)lvl; /* attaque : le niveau en amplitude */
			l = l * F_2M16;
			g = g * l;
		}
	}
	g = g * 0x1p-39f; /* gain 8.24, échantillon 15 bits -> -1..1 */
	g = g * s->gain;
	/* panoramique : loi du sinus */
	s32 pn = v->pan;
	pn += ak_sdivw((s32)(((u32)c->pan - 64) * 1000), 127);
	pn += 500;
	if (pn < 0)
		pn = 0;
	if (pn > 1000)
		pn = 1000;
	u32 pa = ((u32)pn << 8) / 1000; /* 0..256 sur le quart de période */
	float rr = (float)ak_sin1024[pa] * F_2M15;
	float ll = (float)ak_sin1024[256 - pa] * F_2M15;
	float gl = g * ll, gr = g * rr;
	float pgl = v->gl, pgr = v->gr; /* gains du bloc précédent */
	v->gl = gl;
	v->gr = gr;
	/* envoi de réverbération : générateur 16 (0,1 %) + CC 91 */
	s32 rs = (s32)((u32)v->rsend + c->rev * 200 / 127);
	if (rs < 0)
		rs = 0;
	float grv = (float)rs * ak_u2f(0x3A83126F);
	grv = grv * g;
	/* envoi de chorus : générateur 15 (0,1 %) + CC 93 */
	s32 cs = (s32)((u32)v->csend + c->cho * 200 / 127);
	if (cs < 0)
		cs = 0;
	if (cs)
		s->cact = 1;
	float gch = (float)cs * ak_u2f(0x3A83126F);
	gch = gch * g;
	/* hauteur (cents) */
	w = (u32)v->cents;
	w += (u32)((s32)((u32)v->me2p * menv) >> 16);
	w += (u32)((s32)((u32)v->ml2p * (u32)mlfo) >> 16);
	u32 t = (u32)v->vl2p + c->mod * 50 / 127; /* molette de modulation : 50 cents au plus */
	w += (u32)((s32)(t * (u32)vlfo) >> 16);
	w += (u32)((s32)((u32)c->bend * c->range) >> 13);
	v->inc = (u64)ak_cents_mul((s32)w, v->rate) << 12;
	/* filtre */
	w = (u32)v->fc;
	w += (u32)((s32)((u32)v->me2f * menv) >> 16);
	w += (u32)((s32)((u32)v->ml2f * (u32)mlfo) >> 16);
	s32 fc = (s32)w;
	if (fc < 1500)
		fc = 1500;
	if (fc != v->lastfc) {
		v->lastfc = fc;
		sf_filter_set(v, (u32)fc);
	}
	/* pentes des gains */
	float dl = (gl - pgl) * 0x1p-6f;
	float dr = (gr - pgr) * 0x1p-6f;
	/* les trames */
	float *bl = s->bl, *br = s->br, *bv = s->bv, *bc = s->bc;
	u64 pos = v->pos, inc = v->inc, last = v->end - 1, le = v->le;
	const u8 *smp = v->smp;
	u32 mode = v->mode;
	if (mode == 3 && v->rel == 1) /* boucle jusqu'au relâchement */
		mode = 0;
	float z1 = v->z1, z2 = v->z2, b0 = v->b0, b1 = v->b1, a1 = v->a1, a2 = v->a2;
	int nofilt = (u32)v->lastfc >= 13500;
	u64 back = (v->ls - le) << 32; /* - longueur de boucle */
	for (int k = 0; k < SY_BLOCK; k++) {
		u64 i = pos >> 32;
		if (mode) {
			if (i >= le) {
				pos += back;
				i = pos >> 32;
			}
		} else if (i >= last) {
			v->state = 0;
			return;
		}
		const u8 *sp = at(smp, i << 1);
		s32 x0 = (s16)rd16(sp), x1 = (s16)rd16(sp + 2);
		u32 fr = (u32)(pos >> 16) & 0xFFFF;
		s32 xi = (s32)((u32)x0 + (u32)((s32)((u32)(x1 - x0) * fr) >> 16));
		float x = (float)xi;
		if (!nofilt) {
			float y = x * b0;
			y = y + z1;
			float t2 = x * b1;
			float t3 = y * a1;
			t2 = t2 - t3;
			z1 = t2 + z2;
			t2 = x * b0;
			t3 = y * a2;
			z2 = t2 - t3;
			x = y;
		}
		bl[k] = bl[k] + x * pgl;
		br[k] = br[k] + x * pgr;
		bv[k] = bv[k] + x * grv;
		if (cs) {
			*bc = *bc + x * gch;
			bc++;
		}
		pgl = pgl + dl;
		pgr = pgr + dr;
		pos += inc;
	}
	v->pos = pos;
	v->z1 = z1;
	v->z2 = z2;
}

/* ---------------------------------------------------------------------------
 *  Réverbération (huit filtres en peigne, quatre passe-tout, de chaque côté)
 * --------------------------------------------------------------------------- */
#define RV_NC 8
#define RV_NA 4
#define RVL_SIZE 24
#define RV_MEM (RVL_SIZE * (RV_NC + RV_NA) * 2)

struct rv_line {
	u32 pos;  /* 0 */
	u32 len;  /* 4 */
	float st; /* 8 mémoire du filtre */
	u32 pad;
	float *buf; /* 16 */
};
_Static_assert(sizeof(struct rv_line) == RVL_SIZE, "rv_line");

static const u16 rv_sizes[RV_NC + RV_NA] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617, 556, 441, 341, 225};

/* -> réverbération (0 : pas de mémoire) */
static void *rv_new(void)
{
	u64 n = 0;
	for (int i = 0; i < RV_NC + RV_NA; i++)
		n += rv_sizes[i];
	n <<= 1; /* (deux côtés) */
	n += (RV_NC + RV_NA) * 2 * 23; /* (écart stéréo) */
	n <<= 2;
	struct rv_line *l = ak_alloc(n + RV_MEM);
	if (!l)
		return 0;
	float *d = (float *)((u8 *)l + RV_MEM);
	for (u64 i = 0; i < (RV_NC + RV_NA) * 2; i++) {
		u64 side = i / (RV_NC + RV_NA);
		u32 len = rv_sizes[i % (RV_NC + RV_NA)] + (u32)(side * 23); /* (droite : 23 de plus) */
		l[i].len = len;
		l[i].buf = d;
		d += len;
	}
	return l;
}

/* l'envoi (bv) repasse dans bl, br */
static void rv_block(struct aksy *s)
{
	struct rv_line *l = s->rvb;
	if (!l)
		return;
	float out[2][SY_BLOCK];
	const float ing = ak_u2f(0x3C75C28F), fb = ak_u2f(0x3F570A3D), damp = ak_u2f(0x3E4CCCCD),
		    damp1 = ak_u2f(0x3F4CCCCD);
	for (int i = 0; i < SY_BLOCK; i++)
		s->bv[i] = s->bv[i] * ing;
	for (int side = 0; side < 2; side++) {
		float *o = out[side];
		__builtin_memset(o, 0, sizeof out[0]);
		for (int k = 0; k < RV_NC; k++, l++) { /* peignes */
			u64 p = l->pos, len = l->len;
			float st = l->st;
			float *b = l->buf;
			for (int i = 0; i < SY_BLOCK; i++) {
				float y = b[p];
				float t = y * damp1;
				st = st * damp;
				st = st + t; /* mémoire amortie */
				b[p] = s->bv[i] + st * fb;
				o[i] = o[i] + y;
				p++;
				if (p >= len)
					p = 0;
			}
			l->pos = (u32)p;
			l->st = st;
		}
		for (int k = 0; k < RV_NA; k++, l++) { /* passe-tout */
			u64 p = l->pos, len = l->len;
			float *b = l->buf;
			for (int i = 0; i < SY_BLOCK; i++) {
				float bb = b[p], x = o[i];
				float y = bb - x;
				b[p] = bb * 0.5f + x;
				o[i] = y;
				p++;
				if (p >= len)
					p = 0;
			}
			l->pos = (u32)p;
		}
	}
	for (int i = 0; i < SY_BLOCK; i++) { /* mélange (RV_WET = 1) */
		s->bl[i] = s->bl[i] + out[0][i] * 1.0f;
		s->br[i] = s->br[i] + out[1][i] * 1.0f;
	}
}

/* ---------------------------------------------------------------------------
 *  Chorus : une ligne à retard modulée, en opposition à gauche et à droite
 * --------------------------------------------------------------------------- */
#define CH_DELAY 882 /* 20 ms */
#define CH_DEPTH 220 /* +- 5 ms */
#define CH_LFO 2493000

struct chorus {
	u32 pos;   /* 0 écriture */
	u32 phase; /* 4 LFO */
	u32 idle;  /* 8 blocs restants après le dernier envoi */
	u32 dl;	   /* 12 retards du bloc précédent (24.8) */
	u32 dr;	   /* 16 */
	u32 pad[3];
	float buf[CH_LEN + 1]; /* 32 */
};
_Static_assert(sizeof(struct chorus) == CH_SIZE, "chorus");

/* position 24.8 (modulo la ligne) -> échantillon interpolé */
static float ch_tap(const float *b, u32 w)
{
	u32 fr = w & 255;
	u32 i = (w >> 8) & (CH_LEN - 1), j = (i + 1) & (CH_LEN - 1);
	float s0 = b[i], s1 = b[j] - s0;
	float f = (float)(s32)fr * 0x1p-8f;
	s1 = s1 * f;
	return s0 + s1;
}

static void ch_block(struct aksy *s)
{
	struct chorus *h = s->cho;
	if (!h)
		return;
	if (s->cact) {
		s->cact = 0;
		h->idle = 40; /* (tout le retard, puis on s'arrête) */
	}
	if (!h->idle)
		return;
	h->idle--;
	h->phase += CH_LFO;
	s32 m = (s32)((u32)ak_sin1024[h->phase >> 22] * CH_DEPTH) >> 7;
	u32 l = (CH_DELAY << 8) + (u32)m, r = (CH_DELAY << 8) - (u32)m;
	u32 l0 = h->dl, r0 = h->dr;
	if (!l0) { /* (premier bloc) */
		l0 = l;
		r0 = r;
	}
	h->dl = l;
	h->dr = r;
	u32 dl = (u32)((s32)(l - l0) >> 6), dr = (u32)((s32)(r - r0) >> 6); /* pas par trame */
	u32 p = h->pos;
	for (int i = 0; i < SY_BLOCK; i++) {
		__builtin_memcpy(&h->buf[p], &s->bc[i], 4);
		s->bl[i] = s->bl[i] + ch_tap(h->buf, (p << 8) - l0);
		s->br[i] = s->br[i] + ch_tap(h->buf, (p << 8) - r0);
		l0 += dl;
		r0 += dr;
		p = (p + 1) & (CH_LEN - 1);
	}
	h->pos = p;
}

/* ---------------------------------------------------------------------------
 *  Rendu
 * --------------------------------------------------------------------------- */

/* (verrou pris) : un bloc dans bl, br */
static void sy_block(struct aksy *s)
{
	if (s->fm) {
		s16 t[SY_BLOCK * 2];
		ak_fm_render(s->fm, t, SY_BLOCK);
		for (int i = 0; i < SY_BLOCK; i++) {
			s->bl[i] = (float)t[2 * i] * F_2M15;
			s->br[i] = (float)t[2 * i + 1] * F_2M15;
		}
		return;
	}
	__builtin_memset(s->bl, 0, SY_BLOCK * 16);
	for (int i = 0; i < SY_NV; i++)
		if (s->voices[i].state)
			sf_voice_block(s, &s->voices[i]);
	rv_block(s);
	ch_block(s);
}

/* synthé, gauche, droite (flottants, -1..1), trames -> 0 : ÉCRIT les trames
 * suivantes */
long ak_synth_render_f32(struct aksy *s, float *l, float *r, u64 frames)
{
	while (frames) {
		u64 b = s->bpos;
		if (b >= SY_BLOCK) {
			fm_lock(s);
			sy_block(s);
			fm_unlock(s);
			b = 0;
		}
		u64 n = SY_BLOCK - b;
		if (n >= frames)
			n = frames;
		s->bpos = b + n;
		frames -= n;
		__builtin_memcpy(l, s->bl + b, n * 4);
		__builtin_memcpy(r, s->br + b, n * 4);
		l += n;
		r += n;
	}
	return 0;
}

static s16 to_s16(float f)
{
	s32 v = ak_fcvtns_w(f * 32768.0f);
	if (v > 32767)
		v = 32767;
	if (v < -32768)
		v = -32768;
	return (s16)v;
}

/* synthé, sortie s16 stéréo, trames -> 0 */
long ak_synth_render(struct aksy *s, s16 *out, u64 frames)
{
	float l[SY_BLOCK], r[SY_BLOCK];
	while (frames) {
		u64 n = frames < SY_BLOCK ? frames : SY_BLOCK;
		ak_synth_render_f32(s, l, r, n);
		for (u64 i = 0; i < n; i++) {
			*out++ = to_s16(l[i]);
			*out++ = to_s16(r[i]);
		}
		frames -= n;
	}
	return 0;
}
