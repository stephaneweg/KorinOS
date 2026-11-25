/* usbhid/hidparse.c - claviers et souris USB quelconques : lecture du
 * descripteur de rapport HID et décodage des rapports (repris pas à pas de
 * src/svc/inc/hid.inc).
 *
 * D'après la spécification USB HID 1.11 (items courts et longs, globaux,
 * locaux, principaux) et les tables d'usages (pages 01 bureau, 07 clavier,
 * 09 boutons). hid_parse repère, pour une fonction HID :
 *   clavier : octet des modificateurs (usages E0-E7), tableau de touches
 *             (6 en général) ou table de bits (claviers « NKRO ») ;
 *   souris  : boutons, X, Y (relatifs ou absolus : tablette, écran), molette ;
 * avec leur numéro de rapport (Report ID) quand le périphérique en a.
 * usb_report_parsed traduit chaque rapport : le clavier par usb_report_kbd
 * (format « boot » reconstitué), la souris en EV_MOUSE_*. */
#include "hid.h"

/* état de l'analyse (hp_state de hid.inc) */
static struct {
	u32 page, rsize, rcount, rid, nusage, havemin, umin, umax;
	i64 lmin, lmax;
	u32 usages[HID_MAX_USAGES];
	u32 offs[16]; /* position (bits) par numéro de rapport (& 15) */
} hp;

static u8 hid_boot[8];

/* drapeaux de l'item Input (bit 0 constante, bit 1 variable, bit 2 relatif) :
 * note les champs utiles, avance la position du rapport */
static void hid_input_item(struct hid_fn *f, u64 flags)
{
	u32 pos = hp.offs[hp.rid & 15];
	u32 rsize = hp.rsize, rcount = hp.rcount;
	if (flags & 1)
		goto advance; /* remplissage */
	/* page du premier usage (à défaut : la page globale) */
	u32 u;
	if (hp.havemin)
		u = hp.umin;
	else if (hp.nusage)
		u = hp.usages[0];
	else
		u = hp.page << 16;
	u32 page = u >> 16, first = u & 0xFFFF;
	i32 rid = (i32)hp.rid;
	if (!(flags & 2)) {
		/* tableau : touches du clavier */
		if (page != 7 || f->k_arr >= 0)
			goto advance;
		f->k_arr = (i32)pos;
		f->k_arrn = (i32)rcount;
		f->k_arrs = (i32)rsize;
		f->k_id = rid;
		f->has |= 1;
		goto advance;
	}
	if (page == 7) {
		if (rsize != 1) /* bits de clavier */
			goto advance;
		if (first == 0xE0) {
			if (f->k_mod >= 0) /* modificateurs E0-E7 */
				goto advance;
			f->k_mod = (i32)pos;
			f->k_id = rid;
		} else {
			if (f->k_bmp >= 0) /* une touche par bit */
				goto advance;
			f->k_bmp = (i32)pos;
			f->k_bmpmin = (i32)first;
			f->k_bmpn = (i32)rcount;
			f->k_id = rid;
		}
		f->has |= 1;
		goto advance;
	}
	if (page == 9) { /* boutons */
		if (f->m_btn < 0 && rsize == 1) {
			f->m_btn = (i32)pos;
			f->m_btnn = (i32)rcount;
		}
		goto advance;
	}
	if (page != 1) /* bureau : X, Y, molette */
		goto advance;
	for (u32 i = 0; i < rcount; i++) {
		u32 us; /* usage de ce champ */
		if (hp.havemin)
			us = hp.umin + i;
		else {
			if (!hp.nusage)
				goto advance;
			u32 last = hp.nusage - 1;
			us = hp.usages[i < last ? i : last];
		}
		us &= 0xFFFF;
		u32 fp = i * rsize + pos; /* position du champ */
		if (us == 0x30) {
			if (f->m_x >= 0)
				continue;
			f->m_x = (i32)fp;
			f->m_xs = (i32)rsize;
			f->m_abs = !((flags >> 2) & 1);
			f->m_xmax = (i32)hp.lmax;
			f->m_id = rid;
			f->has |= 2;
		} else if (us == 0x31) {
			if (f->m_y >= 0)
				continue;
			f->m_y = (i32)fp;
			f->m_ys = (i32)rsize;
			f->m_ymax = (i32)hp.lmax;
		} else if (us == 0x38) {
			if (f->m_w >= 0)
				continue;
			f->m_w = (i32)fp;
			f->m_ws = (i32)rsize;
		}
	}
advance:
	pos += rsize * rcount; /* taille x nombre */
	hp.offs[hp.rid & 15] = pos;
}

/* descripteur de rapport -> has (bit 0 clavier, bit 1 souris) et les champs
 * k_* / m_* ; d doit être lisible 4 octets au-delà de len (comme l'asm, qui
 * lit les données d'un item sans regarder la fin) */
void hid_parse(struct hid_fn *f, const u8 *d, u32 len)
{
	const u8 *end = d + len;
	f->k_mod = f->k_arr = f->k_bmp = -1; /* aucun champ connu */
	f->m_btn = f->m_x = f->m_y = f->m_w = -1;
	f->has = 0;
	f->ids = 0;
	memset(&hp, 0, sizeof hp);
	while (d < end) {
		u32 prefix = *d++;
		if (prefix == 0xFE) { /* item long : on le saute */
			d += d[0] + 2;
			continue;
		}
		u32 size = prefix & 3; /* données : 0, 1, 2, 4 octets */
		if (size == 3)
			size = 4;
		u64 uv = 0; /* données (non signées) */
		i64 sv = 0; /* données (signées) */
		if (size == 1) {
			uv = d[0];
			sv = (i8)d[0];
		} else if (size == 2) {
			u16 h = (u16)(d[0] | d[1] << 8);
			uv = h;
			sv = (i16)h;
		} else if (size == 4) {
			u32 w = (u32)d[0] | (u32)d[1] << 8 | (u32)d[2] << 16 | (u32)d[3] << 24;
			uv = w;
			sv = (i32)w;
		}
		d += size;
		u32 type = (prefix >> 2) & 3, tag = prefix >> 4;
		if (type == 1) { /* global */
			if (tag == 0)
				hp.page = (u32)uv; /* Usage Page */
			else if (tag == 1)
				hp.lmin = sv; /* Logical Minimum (signé) */
			else if (tag == 2)
				hp.lmax = hp.lmin < 0 ? sv : (i64)uv; /* signé si le minimum l'est */
			else if (tag == 7)
				hp.rsize = (u32)uv; /* Report Size */
			else if (tag == 8) {
				hp.rid = (u32)uv & 0xFF; /* Report ID */
				f->ids = 1;
			} else if (tag == 9)
				hp.rcount = (u32)uv; /* Report Count */
			continue;
		}
		if (type == 2) { /* local : usage complet, page << 16 | usage */
			u32 u = (u32)uv;
			if (size != 4)
				u = (u & 0xFFFF) | hp.page << 16;
			if (tag == 0) { /* Usage */
				if (hp.nusage < HID_MAX_USAGES)
					hp.usages[hp.nusage++] = u;
			} else if (tag == 1) { /* Usage Minimum */
				hp.umin = u;
				hp.havemin = 1;
			} else if (tag == 2)
				hp.umax = u; /* Usage Maximum */
			continue;
		}
		if (type != 0)
			continue; /* (réservé) */
		/* principal */
		if (tag == 8)
			hid_input_item(f, uv); /* Input */
		hp.nusage = 0;
		hp.havemin = 0;
	}
}

/* données, position (bits), taille -> valeur (bits au-delà de 64 perdus,
 * décalages modulo 64 comme lslv) */
u32 hid_rep_len;

static u64 hid_bits(const u8 *d, u64 pos, u64 n)
{
	u64 v = 0;
	if (n > 64) /* (au-delà, les bits se perdaient ; un descripteur piégé faisait tourner des milliards de fois) */
		n = 64;
	for (u64 i = 0; i < n; i++, pos++) {
		if ((pos >> 3) >= hid_rep_len) /* (hors du rapport : 0, P9) */
			break;
		v |= (u64)((d[pos >> 3] >> (pos & 7)) & 1) << (i & 63);
	}
	return v;
}

/* idem, valeur signée (extension du bit de poids fort) */
static u64 hid_sbits(const u8 *d, u64 pos, u64 n)
{
	u64 v = hid_bits(d, pos, n);
	if (n && (v >> ((n - 1) & 63) & 1))
		v |= ~(u64)0 << ((n - 1) & 63); /* négatif : bits hauts à 1 */
	return v;
}

/* fonction, rapport reçu : clavier et/ou souris */
void usb_report_parsed(struct hid_fn *f, const u8 *data)
{
	u32 rid = 0; /* numéro de rapport */
	if (f->ids)
		rid = *data++;
	/* clavier : rapport « boot » reconstitué dans hid_boot */
	if ((f->has & 1) && (u32)f->k_id == rid) {
		memset(hid_boot, 0, 8);
		if (f->k_mod >= 0)
			hid_boot[0] = (u8)hid_bits(data, (u32)f->k_mod, 8);
		u32 k = 2; /* prochain octet de touche (2..7) */
		if (f->k_arr >= 0) { /* tableau */
			for (u32 i = 0; i < (u32)f->k_arrn && k < 8; i++) {
				u64 v = hid_bits(data, (u32)(i * (u32)f->k_arrs + (u32)f->k_arr), (u32)f->k_arrs);
				if (v)
					hid_boot[k++] = (u8)v;
			}
		} else if (f->k_bmp >= 0) { /* table de bits */
			for (u32 i = 0; i < (u32)f->k_bmpn && i < 256 && k < 8; i++)
				if (hid_bits(data, (u32)(f->k_bmp + (i32)i), 1))
					hid_boot[k++] = (u8)(f->k_bmpmin + (i32)i);
		}
		usb_report_kbd(f, hid_boot);
	}
	/* souris */
	if (!(f->has & 2) || (u32)f->m_id != rid)
		return;
	u64 btn = 0;
	if (f->m_btn >= 0) {
		u32 n = (u32)f->m_btnn > 8 ? 8 : (u32)f->m_btnn;
		btn = hid_bits(data, (u32)f->m_btn, n);
	}
	/* (m_x n'est pas regardé : has & 2 n'est mis qu'avec lui, comme l'asm) */
	u64 x = f->m_abs ? hid_bits(data, (u32)f->m_x, (u32)f->m_xs) : hid_sbits(data, (u32)f->m_x, (u32)f->m_xs);
	u64 y = 0;
	if (f->m_y >= 0)
		y = f->m_abs ? hid_bits(data, (u32)f->m_y, (u32)f->m_ys) : hid_sbits(data, (u32)f->m_y, (u32)f->m_ys);
	if (!f->m_abs)
		hid_push(EV_MOUSE_REL, x, y, btn);
	else { /* 0..maximum logique -> 0..32767 */
		if (f->m_xmax)
			x = x * 32767 / (u32)f->m_xmax;
		if (f->m_ymax)
			y = y * 32767 / (u32)f->m_ymax;
		hid_push(EV_MOUSE_ABS, x, y, btn);
	}
	if (f->m_w < 0)
		return;
	u64 w = hid_sbits(data, (u32)f->m_w, (u32)f->m_ws);
	if (w)
		hid_push(EV_WHEEL, w, 0, 0);
}

/* le code figure-t-il dans les octets 2..7 du rapport ? */
static int usb_key_in_report(const u8 *rep, u8 code)
{
	for (int i = 2; i < 8; i++)
		if (rep[i] == code)
			return 1;
	return 0;
}

/* rapport clavier boot (mods, 0, 6 touches) -> EV_KEY */
void usb_report_kbd(struct hid_fn *f, const u8 *rep)
{
	if (rep[2] == 1) /* ErrorRollOver : on ignore le rapport */
		return;
	u8 *old = f->prev;
	u32 mods = rep[0], changed = mods ^ old[0];
	for (u32 b = 0; b < 8; b++)
		if (changed >> b & 1)
			hid_push(EV_KEY, 0xE0 + b, mods >> b & 1, mods); /* usages E0..E7 */
	for (int i = 2; i < 8; i++) /* relâchées : dans l'ancien, pas le nouveau */
		if (old[i] >= 4 && !usb_key_in_report(rep, old[i]))
			hid_push(EV_KEY, old[i], 0, mods);
	for (int i = 2; i < 8; i++) /* enfoncées : dans le nouveau seulement */
		if (rep[i] >= 4 && !usb_key_in_report(old, rep[i]))
			hid_push(EV_KEY, rep[i], 1, mods);
	memcpy(old, rep, 8); /* mémoriser le rapport */
}

/* rapport souris boot (boutons, dx, dy[, molette]) -> EV_MOUSE_REL */
void usb_report_mouse(const u8 *rep)
{
	hid_push(EV_MOUSE_REL, (u64)(i64)(i8)rep[1], (u64)(i64)(i8)rep[2], rep[0]);
	i64 w = (i8)rep[3];
	if (w)
		hid_push(EV_WHEEL, (u64)w, 0, 0);
}

/* rapport tablette de QEMU (boutons, x16, y16, molette) -> EV_MOUSE_ABS */
void usb_report_tablet(const u8 *rep)
{
	hid_push(EV_MOUSE_ABS, (u32)(rep[1] | rep[2] << 8), (u32)(rep[3] | rep[4] << 8), rep[0]);
	i64 w = (i8)rep[5];
	if (w)
		hid_push(EV_WHEEL, (u64)w, 0, 0);
}
