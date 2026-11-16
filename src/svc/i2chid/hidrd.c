/* hidrd.c - voir hidrd.h. D'après « Device Class Definition for HID 1.11 » (chapitre 6.2.2 : éléments
 * courts, globaux, locaux, principaux) et « Windows Precision Touchpad » de Microsoft (collections
 * Touch Pad, Finger, Configuration ; Input Mode). */
#include "hidrd.h"

#define MAX_USAGES 64
#define MAX_DEPTH 16
#define MAX_PUSH 4

struct globals {
	uint32_t page;
	int32_t lmin, lmax, pmin, pmax, exp;
	uint32_t unit, size, count, id;
	uint32_t lmax_u, pmax_u; /* (maximums lus sans signe) */
};

static void zero(void *p, uint32_t n)
{
	uint8_t *b = p;
	while (n--)
		*b++ = 0;
}

static int32_t sval(uint32_t v, int bytes)
{
	if (bytes == 1)
		return (int8_t)v;
	if (bytes == 2)
		return (int16_t)v;
	return (int32_t)v;
}

int hid_parse_desc(struct hid_desc *d, const uint8_t *p, uint32_t len)
{
	zero(d, sizeof *d);
	struct globals g, stack[MAX_PUSH];
	zero(&g, sizeof g);
	int sp = 0;
	uint32_t usages[MAX_USAGES];
	int nus = 0;
	uint32_t umin = 0, umax = 0;
	int has_min = 0, has_max = 0;
	uint32_t app_stack[MAX_DEPTH], coll_stack[MAX_DEPTH];
	int depth = 0;
	uint32_t app = 0, coll = 0, ncoll = 0;
	uint32_t i = 0;
	while (i < len) {
		uint8_t b = p[i++];
		if (b == 0xFE) { /* élément long : sauté */
			if (i + 2 > len)
				break;
			i += 2 + p[i];
			continue;
		}
		int sz = b & 3;
		if (sz == 3)
			sz = 4;
		if (i + (uint32_t)sz > len) {
			d->err = -1; /* coupé */
			break;
		}
		uint32_t v = 0;
		for (int k = 0; k < sz; k++)
			v |= (uint32_t)p[i + k] << (8 * k);
		i += (uint32_t)sz;
		int type = (b >> 2) & 3, tag = b >> 4;
		if (type == 1) { /* global */
			switch (tag) {
			case 0: g.page = v; break;
			case 1: g.lmin = sval(v, sz); break;
			case 2: g.lmax = sval(v, sz); g.lmax_u = v; break;
			case 3: g.pmin = sval(v, sz); break;
			case 4: g.pmax = sval(v, sz); g.pmax_u = v; break;
			case 5: g.exp = (v & 0xF) > 7 ? (int32_t)(v & 0xF) - 16 : (int32_t)(v & 0xF); break;
			case 6: g.unit = v; break;
			case 7: g.size = v; break;
			case 8: g.id = v & 0xFF; d->ids = 1; break;
			case 9: g.count = v; break;
			case 10: if (sp < MAX_PUSH) stack[sp++] = g; break;
			case 11: if (sp > 0) g = stack[--sp]; break;
			}
		} else if (type == 2) { /* local */
			uint32_t u = sz == 4 ? v : (g.page << 16 | (v & 0xFFFF));
			if (tag == 0) {
				if (nus < MAX_USAGES)
					usages[nus++] = u;
			} else if (tag == 1) {
				umin = u;
				has_min = 1;
			} else if (tag == 2) {
				umax = u;
				has_max = 1;
			}
		} else if (type == 0) { /* principal */
			if (tag == 0xA) { /* Collection */
				uint32_t u = nus ? usages[0] : (has_min ? umin : 0);
				if (depth < MAX_DEPTH) {
					app_stack[depth] = app;
					coll_stack[depth] = coll;
					depth++;
				}
				if ((v & 0xFF) == 1)
					app = u;
				else
					coll = ++ncoll;
			} else if (tag == 0xC) { /* End Collection */
				if (depth > 0) {
					depth--;
					app = app_stack[depth];
					coll = coll_stack[depth];
				}
			} else if (tag == 8 || tag == 9 || tag == 0xB) { /* Input, Output, Feature */
				int kind = tag == 8 ? HK_INPUT : tag == 9 ? HK_OUTPUT : HK_FEATURE;
				uint32_t *bits = &d->bits[kind][g.id];
				uint8_t fl = (uint8_t)((v & 1 ? HF_CONST : 0) | (v & 2 ? HF_VAR : 0) | (v & 4 ? HF_REL : 0));
				/* les maximums sans signe quand le minimum ne l'est pas (comme Linux) */
				int32_t lmax = g.lmin >= 0 ? (int32_t)g.lmax_u : g.lmax;
				int32_t pmax = g.pmin >= 0 ? (int32_t)g.pmax_u : g.pmax;
				uint32_t prev_u = 0xFFFFFFFF;
				for (uint32_t k = 0; k < g.count && k < 4096; k++) {
					uint32_t u = 0;
					if (fl & HF_VAR) {
						if (nus)
							u = usages[k < (uint32_t)nus ? k : (uint32_t)nus - 1];
						else if (has_min) {
							u = umin + k;
							if (has_max && u > umax)
								u = umax;
						}
					}
					/* un bloc qui répète le même usage (rapports du fabricant, page 0xFF00, ou de configuration :
					   ceux d'ELAN font 130, 256 et 896 octets) : un seul élément le représente ; sinon ils
					   remplissaient la table avant la partie « pavé » (ZBook, 04f3:313a, 15/11) */
					int dup = k > 0 && u == prev_u && (kind == HK_FEATURE || (u >> 16) >= 0xFF00);
					prev_u = u;
					if (!(fl & HF_CONST) && g.size && g.size <= 32 && !dup) {
						if (d->n < HID_MAX_ITEMS) {
							struct hid_item *it = &d->it[d->n++];
							it->usage = u;
							it->app = app;
							it->bit = *bits;
							it->lmin = g.lmin;
							it->lmax = lmax;
							it->pmin = g.pmin;
							it->pmax = pmax;
							it->unit_exp = g.exp;
							it->unit = g.unit;
							it->coll = (uint16_t)coll;
							it->size = (uint8_t)g.size;
							it->kind = (uint8_t)kind;
							it->flags = fl;
							it->id = (uint8_t)g.id;
						} else
							d->overflow++;
					}
					*bits += g.size;
				}
			}
			nus = 0;
			has_min = has_max = 0;
			umin = umax = 0;
		}
	}
	return d->err;
}

int hid_find(const struct hid_desc *d, int kind, uint32_t app, uint32_t usage, int rank)
{
	for (int i = 0; i < d->n; i++) {
		const struct hid_item *it = &d->it[i];
		if (it->kind == kind && it->usage == usage && (!app || it->app == app) && rank-- == 0)
			return i;
	}
	return -1;
}

int32_t hid_get(const struct hid_item *it, const uint8_t *data, uint32_t len)
{
	if (it->bit + it->size > len * 8)
		return 0;
	uint64_t v = 0;
	for (uint32_t k = 0; k < it->size; k++) {
		uint32_t b = it->bit + k;
		v |= (uint64_t)((data[b >> 3] >> (b & 7)) & 1) << k;
	}
	if (it->lmin < 0 && it->size < 32 && (v >> (it->size - 1)) & 1)
		v |= ~0ull << it->size;
	return (int32_t)v;
}

void hid_put(const struct hid_item *it, uint8_t *data, uint32_t len, int32_t v)
{
	if (it->bit + it->size > len * 8)
		return;
	for (uint32_t k = 0; k < it->size; k++) {
		uint32_t b = it->bit + k;
		uint8_t m = (uint8_t)(1u << (b & 7));
		if (((uint32_t)v >> k) & 1)
			data[b >> 3] |= m;
		else
			data[b >> 3] &= (uint8_t)~m;
	}
}

uint32_t hid_report_bytes(const struct hid_desc *d, int kind, int id)
{
	return (d->bits[kind][id & 0xFF] + 7) / 8;
}

/* taille physique d'un axe en centièmes de mm (0 : inconnue) ; unités : cm (0x11) ou pouce (0x13) */
static int32_t phys_mm100(const struct hid_item *it)
{
	int64_t v = (int64_t)it->pmax - it->pmin;
	if (v <= 0)
		return 0;
	int64_t base;
	if ((it->unit & 0xFF) == 0x11)
		base = 1000; /* 1 cm = 1000 centièmes de mm */
	else if ((it->unit & 0xFF) == 0x13)
		base = 2540;
	else
		return 0;
	v *= base;
	for (int e = it->unit_exp; e > 0; e--)
		v *= 10;
	for (int e = it->unit_exp; e < 0; e++)
		v /= 10;
	return v > 0 && v < 10000000 ? (int32_t)v : 0;
}

int hid_model_build(const struct hid_desc *d, struct hid_model *m)
{
	m->nmouse = 0;
	m->t_id = m->t_count = m->t_scan = -1;
	m->t_btn[0] = m->t_btn[1] = m->t_btn[2] = -1;
	m->nfing = 0;
	m->xmin = m->xmax = m->ymin = m->ymax = 0;
	m->xmm100 = m->ymm100 = 0;
	m->mode_item = hid_find(d, HK_FEATURE, 0, U_DIG_INPUT_MODE, 0);
	m->surface_item = hid_find(d, HK_FEATURE, 0, U_DIG_SURFACE_SWITCH, 0);
	m->button_item = hid_find(d, HK_FEATURE, 0, U_DIG_BUTTON_SWITCH, 0);
	m->max_contacts_item = hid_find(d, HK_FEATURE, 0, U_DIG_CONTACT_MAX, 0);
	int r = 0;

	/* pavé : les X de la collection Touch Pad, un par doigt (collection logique) */
	for (int i = 0; i < d->n && m->nfing < HID_MAX_FINGERS; i++) {
		const struct hid_item *x = &d->it[i];
		if (x->kind != HK_INPUT || x->app != U_DIG_TOUCHPAD || x->usage != U_GD_X || (x->flags & HF_REL))
			continue;
		if (m->t_id >= 0 && x->id != m->t_id)
			continue;
		struct hid_finger *f = &m->f[m->nfing];
		f->x = i;
		f->y = f->tip = f->conf = f->cid = -1;
		for (int j = 0; j < d->n; j++) {
			const struct hid_item *o = &d->it[j];
			if (o->kind != HK_INPUT || o->id != x->id || o->coll != x->coll || o->app != U_DIG_TOUCHPAD)
				continue;
			if (o->usage == U_GD_Y && f->y < 0)
				f->y = j;
			else if (o->usage == U_DIG_TIP && f->tip < 0)
				f->tip = j;
			else if (o->usage == U_DIG_CONFIDENCE && f->conf < 0)
				f->conf = j;
			else if (o->usage == U_DIG_CONTACT_ID && f->cid < 0)
				f->cid = j;
		}
		if (f->y < 0 || f->tip < 0)
			continue;
		if (m->t_id < 0) {
			m->t_id = x->id;
			m->xmin = x->lmin;
			m->xmax = x->lmax;
			m->ymin = d->it[f->y].lmin;
			m->ymax = d->it[f->y].lmax;
			m->xmm100 = phys_mm100(x);
			m->ymm100 = phys_mm100(&d->it[f->y]);
		}
		m->nfing++;
	}
	if (m->nfing) {
		for (int i = 0; i < d->n; i++) {
			const struct hid_item *o = &d->it[i];
			if (o->kind != HK_INPUT || o->id != m->t_id)
				continue;
			if (o->usage == U_DIG_CONTACT_COUNT && m->t_count < 0)
				m->t_count = i;
			else if (o->usage >= HU(9, 1) && o->usage <= HU(9, 3) && m->t_btn[o->usage - HU(9, 1)] < 0)
				m->t_btn[o->usage - HU(9, 1)] = i;
			else if (o->usage == U_DIG_SCAN_TIME && m->t_scan < 0)
				m->t_scan = i;
		}
		/* pas de bouton 1 : les « boutons » 2 et 3 sont alors des bits de chaque doigt (ELAN 04f3:313a du ZBook,
		   dans la collection du doigt), pas un bouton du pavé : ignorés, le toucher seul clique */
		if (m->t_btn[0] < 0)
			m->t_btn[1] = m->t_btn[2] = -1;
		r |= 2;
	}

	/* souris : chaque X relatif hors du pavé (un rapport chacun), et ce qui est dans le même rapport */
	for (int i = 0; i < d->n && m->nmouse < HID_MAX_MICE; i++) {
		const struct hid_item *x = &d->it[i];
		if (x->kind != HK_INPUT || x->usage != U_GD_X || !(x->flags & HF_REL) || x->app == U_DIG_TOUCHPAD)
			continue;
		int seen = 0;
		for (int k = 0; k < m->nmouse; k++)
			seen |= m->m[k].id == x->id;
		if (seen)
			continue;
		struct hid_mouse *ms = &m->m[m->nmouse];
		ms->id = x->id;
		ms->x = i;
		ms->y = ms->wheel = ms->pan = -1;
		for (int b = 0; b < 5; b++)
			ms->btn[b] = -1;
		for (int j = 0; j < d->n; j++) {
			const struct hid_item *o = &d->it[j];
			if (o->kind != HK_INPUT || o->id != x->id)
				continue;
			if (o->usage == U_GD_Y && ms->y < 0)
				ms->y = j;
			else if (o->usage == U_GD_WHEEL && ms->wheel < 0)
				ms->wheel = j;
			else if (o->usage == U_CON_ACPAN && ms->pan < 0)
				ms->pan = j;
			else if (o->usage >= HU(9, 1) && o->usage <= HU(9, 5) && ms->btn[o->usage - HU(9, 1)] < 0)
				ms->btn[o->usage - HU(9, 1)] = j;
		}
		if (ms->y >= 0) {
			m->nmouse++;
			r |= 1;
		}
	}
	return r;
}

int hid_decode(const struct hid_desc *d, const struct hid_model *m, const uint8_t *rep, uint32_t len,
	       struct hid_frame *fr)
{
	int id = 0;
	if (d->ids) {
		if (!len)
			return 0;
		id = rep[0];
		rep++;
		len--;
	}
	zero(fr, sizeof *fr);
	fr->scan = -1;
	if (m->nfing && id == m->t_id) {
		fr->kind = 2;
		fr->count = m->t_count >= 0 ? hid_get(&d->it[m->t_count], rep, len) : -1;
		for (int b = 0; b < 3; b++)
			if (m->t_btn[b] >= 0 && hid_get(&d->it[m->t_btn[b]], rep, len))
				fr->button |= 1 << b;
		if (m->t_scan >= 0)
			fr->scan = hid_get(&d->it[m->t_scan], rep, len);
		int n = 0;
		for (int i = 0; i < m->nfing; i++) {
			const struct hid_finger *f = &m->f[i];
			if (!hid_get(&d->it[f->tip], rep, len))
				continue;
			struct hid_contact *c = &fr->c[n++];
			c->id = f->cid >= 0 ? hid_get(&d->it[f->cid], rep, len) : i;
			c->x = hid_get(&d->it[f->x], rep, len);
			c->y = hid_get(&d->it[f->y], rep, len);
			c->conf = f->conf >= 0 ? hid_get(&d->it[f->conf], rep, len) : 1;
		}
		fr->n = n; /* (contacts posés dans ce rapport ; count : le total annoncé, mode hybride) */
		return 1;
	}
	for (int k = 0; k < m->nmouse; k++) {
		const struct hid_mouse *ms = &m->m[k];
		if (id != ms->id)
			continue;
		fr->kind = 1;
		fr->src = k;
		fr->dx = hid_get(&d->it[ms->x], rep, len);
		fr->dy = hid_get(&d->it[ms->y], rep, len);
		fr->wheel = ms->wheel >= 0 ? hid_get(&d->it[ms->wheel], rep, len) : 0;
		fr->pan = ms->pan >= 0 ? hid_get(&d->it[ms->pan], rep, len) : 0;
		for (int b = 0; b < 5; b++)
			if (ms->btn[b] >= 0 && hid_get(&d->it[ms->btn[b]], rep, len))
				fr->buttons |= 1 << b;
		return 1;
	}
	return 0;
}
