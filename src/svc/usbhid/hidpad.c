/* usbhid/hidpad.c - manettes USB HID, pour usbgamepad (repris pas à pas de
 * src/svc/inc/hidpad.inc) : lecture du descripteur de rapport (usages 01:04
 * Joystick, 01:05 Gamepad) et traduction de chaque rapport en EV_PAD_RAW vers
 * hidmanager, qui applique le mapping (src/inc/pad.inc). Rien n'est interprété
 * ici : les boutons restent numérotés dans l'ordre du rapport.
 *
 * Pas essayé sur une vraie manette (QEMU n'en émule aucune), comme l'asm. */
#include "hid.h"

#define PD_AX_MAX 6 /* X, Y, Z, Rx, Ry, Rz */

static i32 pd_btn_pos, pd_btn_n, pd_hat_pos, pd_hat_min;
u32 pd_vidpid;
static u8 pd_id;
static struct {
	i32 pos, size, min, max;
} pd_ax[PD_AX_MAX];

/* 8 directions -> bits : 1 haut, 2 bas, 4 gauche, 8 droite */
static const u8 pd_hat_tab[8] = {1, 9, 8, 10, 2, 6, 4, 5};
/* X lx, Y ly, Z rx, Rz ry ; Rx et Ry : des gâchettes (4 : aucun rang) */
static const u8 pd_ax_of[PD_AX_MAX] = {0, 1, 2, 4, 4, 3};

/* descripteur de rapport -> 1 : c'est une manette */
int pad_parse(const u8 *d, u32 len)
{
	const u8 *end = d + len;
	pd_btn_pos = pd_hat_pos = -1;
	for (int i = 0; i < PD_AX_MAX; i++)
		pd_ax[i].pos = -1;
	u32 page = 0, size = 0, count = 0, pos = 0;
	int is_pad = 0;
	u64 usages = 0; /* usages locaux, un octet chacun, le dernier en bas */
	i32 lmin = 0, lmax = 0;
	while (d < end) {
		u32 b = *d++;
		if (b == 0xFE)
			break; /* (élément long : aucun chez une manette) */
		u32 n = b & 3;
		if (n == 3)
			n = 4;
		u64 v = 0;
		for (u32 i = 0; i < n; i++)
			v |= (u64)d[i] << (i * 8);
		d += n;
		switch (b & 0xFC) {
		case 0x04: /* Usage Page */
			page = (u32)v;
			break;
		case 0x14: /* Logical Minimum (signé) */
			lmin = (i32)(u32)v;
			if (n == 1)
				lmin = (i8)v;
			else if (n == 2)
				lmin = (i16)v;
			break;
		case 0x24: /* Logical Maximum */
			lmax = (i32)(u32)v;
			break;
		case 0x74: /* Report Size */
			size = (u32)v;
			break;
		case 0x94: /* Report Count */
			count = (u32)v;
			break;
		case 0x84: /* Report ID : les rapports numérotés ont l'octet du numéro */
			pd_id = (u8)v;
			pos = 8; /* (sans tenir de position par rapport, comme l'asm) */
			break;
		case 0x08: /* Usage */
			usages = usages << 8 | v;
			if (page == 1 && ((v & 0xFF) == 4 || (v & 0xFF) == 5))
				is_pad = 1;
			break;
		case 0x80: { /* Input */
			u32 bits = size * count;
			if (v & 1)
				; /* constante : du remplissage */
			else if (page == 9) { /* les boutons */
				if (pd_btn_pos < 0) {
					pd_btn_pos = (i32)pos;
					pd_btn_n = (i32)count;
				}
			} else if (page == 1) { /* usages, le dernier déclaré en bas */
				for (u32 k = count; k;) {
					k--;
					u32 fp = k * size + pos;
					u32 u = (u32)(usages & 0xFF);
					usages >>= 8;
					if (u == 0x39) { /* chapeau */
						pd_hat_pos = (i32)fp;
						pd_hat_min = lmin;
						continue;
					}
					u -= 0x30;
					if (u >= PD_AX_MAX)
						continue;
					pd_ax[u].pos = (i32)fp;
					pd_ax[u].size = (i32)size;
					pd_ax[u].min = lmin;
					pd_ax[u].max = lmax;
				}
			}
			pos += bits;
			usages = 0;
			break;
		}
		case 0xA0: /* Collection, Output, Feature : les usages locaux tombent */
		case 0x90:
		case 0xB0:
			usages = 0;
			break;
		}
	}
	return is_pad;
}

/* rapport, position, bits -> valeur (non signée, décalages modulo 32 comme lsl w) */
static u32 pad_bits(const u8 *r, u32 pos, u32 n)
{
	u32 v = 0;
	if (n > 32)
		n = 32;
	for (u32 i = 0; i < n; i++) {
		u32 b = pos + i;
		if ((b >> 3) >= hid_rep_len) /* (hors du rapport : 0, P9) */
			break;
		v |= (u32)((r[b >> 3] >> (b & 7)) & 1) << (i & 31);
	}
	return v;
}

/* un rapport -> EV_PAD_RAW (boutons bruts, chapeau en bits 28..31 ; axes lx, ly,
 * rx, ry sur 16 bits chacun, -1000..1000 ; le modèle vendeur << 16 | produit) */
void pad_report(const u8 *data)
{
	if (pd_id && data[0] != pd_id) /* un rapport qui n'est pas le nôtre */
		return;
	u32 raw = 0;
	if (pd_btn_pos >= 0) {
		u32 n = (u32)pd_btn_n > 28 ? 28 : (u32)pd_btn_n;
		raw = pad_bits(data, (u32)pd_btn_pos, n);
	}
	if (pd_hat_pos >= 0) {
		u32 h = pad_bits(data, (u32)pd_hat_pos, 4) - (u32)pd_hat_min;
		if (h <= 7)
			raw |= (u32)pd_hat_tab[h] << 28;
	}
	u64 axes = 0;
	for (int i = 0; i < PD_AX_MAX; i++) {
		u32 rank = pd_ax_of[i];
		if (rank >= 4 || pd_ax[i].pos < 0)
			continue;
		i32 v = (i32)pad_bits(data, (u32)pd_ax[i].pos, (u32)pd_ax[i].size);
		i32 range = (i32)((u32)pd_ax[i].max - (u32)pd_ax[i].min);
		if (!range)
			continue;
		v = (i32)((u32)(v - pd_ax[i].min) * 2000u); /* (32 bits, comme mul w) */
		v = (range == -1 ? (i32)(0u - (u32)v) : v / range) - 1000; /* (sdiv : -2^31 / -1 sans faute) */
		axes |= (u64)((u32)v & 0xFFFF) << (rank * 16);
	}
	hid_push(EV_PAD_RAW, raw, axes, pd_vidpid);
}
