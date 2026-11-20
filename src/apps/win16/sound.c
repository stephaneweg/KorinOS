/* sound.c -- win16, la machine DOS : le son (docs/etude/WIN16.md §9.5).
 *
 * - le haut-parleur : le canal 2 du PIT et les bits 0-1 du port 61h (SOUND, PLAY, BEEP de QuickBASIC) ;
 * - l'OPL3 (AdLib en 388h-38Bh ; Sound Blaster en 220h-223h et 228h-229h) : Nuked OPL3 (nuked/, LGPL-2.1+), avec
 *   ses deux minuteries, que les programmes lisent pour trouver la carte ;
 * - la Sound Blaster 16 en 220h, IRQ 5, DMA 1 et 5 (DSP 4.05 : sortie directe, DMA 8 et 16 bits, simple et
 *   auto-init, haute vitesse, stéréo de la SB Pro, commandes Bxh/Cxh de la SB16 ; mélangeur 224h/225h) et les deux
 *   contrôleurs DMA 8237 (00h-0Fh, C0h-DFh, pages 81h-8Fh).
 * Un fil (be_pcm_start) mélange le tout à 44,1 kHz stéréo dans un flux d'AudioKit ; il démarre au premier son. Les
 * écritures du programme passent au fil sous un verrou, à l'heure de Korin ; la fin d'un bloc de DMA lève l'IRQ 5 du
 * côté du processeur (sound_poll). */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "dos.h"
#include "be.h"
#include "nuked/opl3.h"

#define RATE 44100

static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
static int started;

static uint64_t now_us (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (uint64_t) t.tv_sec * 1000000u + (uint64_t) t.tv_nsec / 1000;
}

static void fill (int16_t *buf, int frames);

static void start (void)
{
	if (!started)
		started = be_pcm_start (fill) ? 1 : -1;
}

/* ======================================================================== OPL3 */
static opl3_chip opl;
static uint16_t opl_index[2];
static uint8_t t1_val, t2_val, t_ctrl, t_flags;
static uint64_t t1_start, t2_start, isa_us;

/* le temps des minuteries : l'heure de Korin, plus 2 µs par lecture du port d'état (une lecture sur le bus ISA :
 * les programmes attendent ainsi les 80 µs de leur détection, bien plus vite dans l'émulateur) */
static void opl_timers (void)
{
	uint64_t t = now_us () + isa_us;
	if ((t_ctrl & 1) && !(t_ctrl & 0x40) && t - t1_start >= (uint64_t) (256 - t1_val) * 80)
		t_flags |= 0xc0;
	if ((t_ctrl & 2) && !(t_ctrl & 0x20) && t - t2_start >= (uint64_t) (256 - t2_val) * 320)
		t_flags |= 0xa0;
}

static uint8_t opl_status (void)
{
	isa_us += 2;
	opl_timers ();
	return t_flags;
}

static void opl_write (int bank, uint8_t v)
{
	uint16_t reg = (uint16_t) (opl_index[bank] | bank << 8);
	if (w16_trace > 2 && (reg & 0xf0) == 0xb0 && (v & 0x20))
		w16_log ("OPL : note en %03x (%02x)\n", reg, v);
	if (!bank)
		switch (reg)
		{
		case 0x02: t1_val = v; return;
		case 0x03: t2_val = v; return;
		case 0x04:
			if (v & 0x80)
			{
				t_flags = 0;
				return;
			}
			if ((v & 1) && !(t_ctrl & 1))
				t1_start = now_us () + isa_us;
			if ((v & 2) && !(t_ctrl & 2))
				t2_start = now_us () + isa_us;
			t_ctrl = v;
			t_flags &= (uint8_t) ~((v & 0x40 ? 0x40 : 0) | (v & 0x20 ? 0x20 : 0));
			if (!(t_flags & 0x60))
				t_flags = 0;
			return;
		}
	start ();
	pthread_mutex_lock (&lk);
	OPL3_WriteRegBuffered (&opl, reg, v);
	pthread_mutex_unlock (&lk);
}

/* ======================================================================== DMA 8237 */
struct dmach { uint16_t base_addr, base_count, addr, count; uint8_t page, mode, masked; };
static struct dmach dma[8];
static uint8_t dma_ff[2], dma_status[2];

/* un octet (ch < 4) ou un mot (ch >= 4) du canal ; 0 et fin si masqué ou terminé */
static int dma_read (int ch, uint8_t *out, int n)
{
	struct dmach *d = &dma[ch];
	if (d->masked)
		return 0;
	uint32_t lin = ch < 4 ? ((uint32_t) d->page << 16 | d->addr) : ((uint32_t) (d->page & 0xfe) << 16 | (uint32_t) d->addr << 1);
	memcpy (out, lin_ptr (lin), (size_t) n);
	d->addr++;
	if (d->count-- == 0)
	{
		dma_status[ch >> 2] |= (uint8_t) (1 << (ch & 3));
		if (d->mode & 0x10)	/* auto-init */
		{
			d->addr = d->base_addr;
			d->count = d->base_count;
		}
		else
			d->masked = 1;
	}
	return 1;
}

static int dma_reg (uint16_t port, int *ctl, int *reg)
{
	if (port < 0x10)
	{
		*ctl = 0;
		*reg = port;
		return 1;
	}
	if (port >= 0xc0 && port <= 0xdf)
	{
		*ctl = 1;
		*reg = (port - 0xc0) >> 1;
		return 1;
	}
	return 0;
}

static const uint8_t page_port[8] = { 0x87, 0x83, 0x81, 0x82, 0x8f, 0x8b, 0x89, 0x8a };

static void dma_out (int ctl, int reg, uint8_t v)
{
	if (reg < 8)
	{
		struct dmach *d = &dma[ctl * 4 + (reg >> 1)];
		uint16_t *b = (reg & 1) ? &d->base_count : &d->base_addr, *c = (reg & 1) ? &d->count : &d->addr;
		*b = dma_ff[ctl] ? (uint16_t) ((*b & 0xff) | v << 8) : (uint16_t) ((*b & 0xff00) | v);
		*c = *b;
		dma_ff[ctl] ^= 1;
		return;
	}
	switch (reg)
	{
	case 0x0a: dma[ctl * 4 + (v & 3)].masked = (v & 4) != 0; break;
	case 0x0b: dma[ctl * 4 + (v & 3)].mode = v; break;
	case 0x0c: dma_ff[ctl] = 0; break;
	case 0x0d: dma_ff[ctl] = 0; for (int i = 0; i < 4; i++) dma[ctl * 4 + i].masked = 1; break;
	case 0x0e: for (int i = 0; i < 4; i++) dma[ctl * 4 + i].masked = 0; break;
	case 0x0f: for (int i = 0; i < 4; i++) dma[ctl * 4 + i].masked = (v >> i) & 1; break;
	}
}

static uint8_t dma_in (int ctl, int reg)
{
	if (reg < 8)
	{
		struct dmach *d = &dma[ctl * 4 + (reg >> 1)];
		uint16_t v = (reg & 1) ? d->count : d->addr;
		uint8_t r = dma_ff[ctl] ? (uint8_t) (v >> 8) : (uint8_t) v;
		dma_ff[ctl] ^= 1;
		return r;
	}
	if (reg == 0x08)
	{
		uint8_t s = dma_status[ctl];
		dma_status[ctl] = 0;
		return s;
	}
	return 0xff;
}

/* ======================================================================== Sound Blaster 16 */
static uint8_t outq[64];
static int outq_n, outq_head;
static uint8_t cmd, params[4];
static int need, have;
static uint8_t mixer[256], mixer_index, test_reg;
static int reset_hi, speaker_on;
/* la lecture en cours */
static int playing, auto_init, bits16, stereo, is_signed, paused;
static uint32_t rate = 22050, block_len = 0x7ff, left;
static uint32_t frac;		/* (position fractionnaire, 16.16) */
static int16_t cur_l, cur_r;
static uint8_t direct_dac = 0x80;
static volatile int irq8, irq16;
static volatile unsigned irq_count;	/* (blocs finis : le processeur lève l'IRQ 5 pour chacun) */

static void out_push (uint8_t v)
{
	if (outq_n < 64)
		outq[(outq_head + outq_n++) % 64] = v;
}

static void sb_reset (void)
{
	outq_n = 0;
	have = need = 0;
	playing = 0;
	paused = 0;
	out_push (0xaa);
	memset (mixer, 0, sizeof mixer);
	mixer[0x22] = mixer[0x04] = mixer[0x26] = 0xee;
	mixer[0x30] = mixer[0x31] = mixer[0x32] = mixer[0x33] = mixer[0x34] = mixer[0x35] = 0xf8;
	mixer[0x80] = 0x02;	/* IRQ 5 */
	mixer[0x81] = 0x22;	/* DMA 1 et 5 */
}

static int nparams (uint8_t c)
{
	switch (c)
	{
	case 0x10: case 0x40: case 0x38: case 0xe0: case 0xe4: return 1;
	case 0x14: case 0x16: case 0x17: case 0x24: case 0x41: case 0x42: case 0x48: case 0x74: case 0x75: case 0x76:
	case 0x77: case 0x80: return 2;
	}
	if (c >= 0xb0 && c <= 0xcf)
		return 3;
	return 0;
}

static void play (int is16, int autoi, uint32_t len)
{
	/* (appelée sous le verrou, par dsp_write) */
	bits16 = is16;
	auto_init = autoi;
	block_len = len;
	left = len;
	playing = 1;
	paused = 0;
	start ();
}

static void dsp_command (void)
{
	switch (cmd)
	{
	case 0x10: direct_dac = params[0]; start (); break;
	case 0x14: case 0x16: case 0x17:
		stereo = (mixer[0x0e] & 2) != 0;
		is_signed = 0;
		play (0, 0, (uint32_t) (params[0] | params[1] << 8) + 1);
		break;
	case 0x1c: case 0x1e: case 0x1f: case 0x90:
		stereo = (mixer[0x0e] & 2) != 0;
		is_signed = 0;
		play (0, 1, block_len);
		break;
	case 0x91:
		stereo = (mixer[0x0e] & 2) != 0;
		is_signed = 0;
		play (0, 0, block_len);
		break;
	case 0x20: out_push (0x80); break;
	case 0x40: rate = 1000000 / (256 - params[0]); if (mixer[0x0e] & 2) rate /= 2; break;
	case 0x41: case 0x42: rate = (uint32_t) (params[0] << 8 | params[1]); break;
	case 0x48: block_len = (uint32_t) (params[0] | params[1] << 8) + 1; break;
	case 0xd0: case 0xd5: paused = 1; break;
	case 0xd4: case 0xd6: paused = 0; break;
	case 0xd1: speaker_on = 1; break;
	case 0xd3: speaker_on = 0; break;
	case 0xd8: out_push (speaker_on ? 0xff : 0x00); break;
	case 0xd9: case 0xda: auto_init = 0; break;
	case 0xe0: out_push ((uint8_t) ~params[0]); break;
	case 0xe1: out_push (4); out_push (5); break;	/* (DSP 4.05 : Sound Blaster 16) */
	case 0xe3: for (const char *s = "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992."; *s; s++) out_push ((uint8_t) *s); out_push (0); break;
	case 0xe4: test_reg = params[0]; break;
	case 0xe8: out_push (test_reg); break;
	case 0xf2: irq8 = 1; mixer[0x82] |= 1; irq_count++; break;
	case 0xf3: irq16 = 1; mixer[0x82] |= 2; irq_count++; break;
	default:
		if (cmd >= 0xb0 && cmd <= 0xcf)
		{
			/* SB16 : Bxh 16 bits, Cxh 8 bits ; bit 2 auto-init ; mode : bit 4 signé, bit 5 stéréo */
			stereo = (params[0] & 0x20) != 0;
			is_signed = (params[0] & 0x10) != 0;
			play (cmd < 0xc0, (cmd & 4) != 0, (uint32_t) (params[1] | params[2] << 8) + 1);
		}
		break;
	}
}

static void dsp_write (uint8_t v)
{
	if (need)
	{
		params[have++] = v;
		if (have == need)
		{
			need = 0;
			dsp_command ();
		}
		return;
	}
	cmd = v;
	have = 0;
	need = nparams (v);
	if (!need)
		dsp_command ();
}

/* ======================================================================== les ports */
int sound_in (uint16_t port, uint8_t *v)
{
	int ctl, reg;
	if (dma_reg (port, &ctl, &reg))
	{
		*v = dma_in (ctl, reg);
		return 1;
	}
	for (int i = 0; i < 8; i++)
		if (port == page_port[i])
		{
			*v = dma[i].page;
			return 1;
		}
	switch (port)
	{
	case 0x388: case 0x228: case 0x220: case 0x38a: case 0x222:
		*v = opl_status ();
		return 1;
	case 0x389: case 0x38b: case 0x221: case 0x223: case 0x229:
		*v = 0xff;
		return 1;
	case 0x224: *v = mixer_index; return 1;
	case 0x225: *v = mixer[mixer_index]; return 1;
	case 0x22a:
		if (outq_n)
		{
			*v = outq[outq_head];
			outq_head = (outq_head + 1) % 64;
			outq_n--;
		}
		else
			*v = 0xff;
		return 1;
	case 0x22c: *v = 0x7f; return 1;	/* (prêt à recevoir) */
	case 0x22e:
		irq8 = 0;
		mixer[0x82] &= (uint8_t) ~1;
		*v = outq_n ? 0xff : 0x7f;
		return 1;
	case 0x22f:
		irq16 = 0;
		mixer[0x82] &= (uint8_t) ~2;
		*v = 0xff;
		return 1;
	}
	return 0;
}

int sound_out (uint16_t port, uint8_t v)
{
	int ctl, reg;
	if (dma_reg (port, &ctl, &reg))
	{
		pthread_mutex_lock (&lk);
		dma_out (ctl, reg, v);
		pthread_mutex_unlock (&lk);
		return 1;
	}
	for (int i = 0; i < 8; i++)
		if (port == page_port[i])
		{
			dma[i].page = v;
			return 1;
		}
	switch (port)
	{
	case 0x388: case 0x220: case 0x228: opl_index[0] = v; return 1;
	case 0x38a: case 0x222: opl_index[1] = v; return 1;
	case 0x389: case 0x221: case 0x229: opl_write (0, v); return 1;
	case 0x38b: case 0x223: opl_write (1, v); return 1;
	case 0x224: mixer_index = v; return 1;
	case 0x225:
		if (mixer_index == 0)
		{
			memset (mixer, 0, 0x80);
			mixer[0x22] = mixer[0x04] = mixer[0x26] = 0xee;
		}
		else if (mixer_index != 0x80 && mixer_index != 0x81 && mixer_index != 0x82)
			mixer[mixer_index] = v;
		return 1;
	case 0x226:
		if (v & 1)
			reset_hi = 1;
		else if (reset_hi)
		{
			reset_hi = 0;
			pthread_mutex_lock (&lk);
			sb_reset ();
			pthread_mutex_unlock (&lk);
		}
		return 1;
	case 0x22c:
		pthread_mutex_lock (&lk);
		dsp_write (v);
		pthread_mutex_unlock (&lk);
		return 1;
	}
	return 0;
}

/* ======================================================================== le mélange (fil du son) */
static uint32_t spk_phase;

static int vol4 (uint8_t r, int right) { return right ? (r & 0x0f) : (r >> 4); }	/* (0..15, SB Pro) */

/* l'échantillon suivant de la lecture par DMA */
static void sb_next (void)
{
	if (!playing || paused)
		return;
	int ch = bits16 ? 5 : 1, n = bits16 ? 2 : 1;
	for (int side = 0; side < (stereo ? 2 : 1); side++)
	{
		uint8_t b[2] = { 0, 0 };
		if (!dma_read (ch, b, n))
		{
			playing = 0;
			return;
		}
		int16_t s;
		if (bits16)
			s = (int16_t) (b[0] | b[1] << 8);
		else
			s = (int16_t) (((int) b[0] - (is_signed ? 0 : 128)) << 8);
		if (bits16 && !is_signed)
			s = (int16_t) (s ^ 0x8000);
		if (side == 0)
			cur_l = cur_r = s;
		else
			cur_r = s;
	}
	if (--left == 0)
	{
		if (bits16)
		{
			irq16 = 1;
			mixer[0x82] |= 2;
		}
		else
		{
			irq8 = 1;
			mixer[0x82] |= 1;
		}
		irq_count++;
		if (auto_init)
			left = block_len;
		else
			playing = 0;
	}
}

static void fill (int16_t *buf, int frames)
{
	uint32_t reload = bios_pit2_reload ();
	int spk = (bios_port61 () & 3) == 3 && reload > 1;
	uint32_t spk_step = spk ? (uint32_t) ((1193182ull << 32) / reload / RATE >> 16) : 0;
	pthread_mutex_lock (&lk);
	int master_l = vol4 (mixer[0x22], 0), master_r = vol4 (mixer[0x22], 1);
	int fm_l = vol4 (mixer[0x26], 0), fm_r = vol4 (mixer[0x26], 1);
	int vo_l = vol4 (mixer[0x04], 0), vo_r = vol4 (mixer[0x04], 1);
	for (int i = 0; i < frames; i++)
	{
		int16_t o[2];
		OPL3_GenerateResampled (&opl, o);
		/* la Sound Blaster, ramenée de sa fréquence à 44,1 kHz (tenue d'échantillon) */
		frac += (uint32_t) (((uint64_t) rate << 16) / RATE);
		while (frac >= 0x10000)
		{
			frac -= 0x10000;
			sb_next ();
		}
		int dl = playing ? cur_l : ((int) direct_dac - 128) << 8, dr = playing ? cur_r : ((int) direct_dac - 128) << 8;
		if (!speaker_on && !playing)
			dl = dr = 0;
		int l = o[0] * fm_l / 15 + dl * vo_l / 15, r = o[1] * fm_r / 15 + dr * vo_r / 15;
		if (spk)
		{
			spk_phase += spk_step;
			int v = (spk_phase & 0x8000) ? 5000 : -5000;
			l += v;
			r += v;
		}
		l = l * master_l / 15;
		r = r * master_r / 15;
		buf[i * 2] = (int16_t) (l > 32767 ? 32767 : l < -32768 ? -32768 : l);
		buf[i * 2 + 1] = (int16_t) (r > 32767 ? 32767 : r < -32768 ? -32768 : r);
	}
	pthread_mutex_unlock (&lk);
}

/* le côté du processeur : les IRQ de fin de bloc, le haut-parleur qui se met à sonner */
void sound_poll (void)
{
	static unsigned seen;
	if (irq_count != seen)
	{
		seen = irq_count;
		pic_raise (5);
		if (w16_trace > 2)
			w16_log ("Sound Blaster : IRQ 5 (fin de bloc)\n");
	}
	if (!started && (bios_port61 () & 3) == 3)
		start ();
}

void sound_init (void)
{
	OPL3_Reset (&opl, RATE);
	sb_reset ();
	outq_n = 0;
	for (int i = 0; i < 8; i++)
		dma[i].masked = 1;
}
