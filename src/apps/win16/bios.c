/* bios.c -- win16, la machine DOS : le BIOS (zone de données en 40:00, INT 10h-1Ah), le PIC 8259, le PIT 8253, le
 * contrôleur du clavier, le CMOS, les ports d'E/S, la fenêtre de Korin et ses événements (docs/etude/WIN16.md §9.2).
 *
 * Les IRQ 0 et 1 passent par du vrai code 16 bits en F000:1000 (INT 8 appelle l'INT 1Ch, l'INT 9 lit le port 60h) :
 * un programme qui les détourne et les chaîne marche comme sur un PC. Le temps vient de l'horloge de Korin. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "dos.h"
#include "be.h"
#include "../../lib/basic/basfont.h"

long bios_win;
int dos_closed;			/* la fenêtre a été fermée par l'utilisateur */
static char win_title[160] = "DOS";

#define BDA(o)	(0x400u + (o))

void set16 (int r, uint16_t v) { cpu_set_reg (r, (cpu_reg (r) & 0xffff0000u) | v); }
void set8l (int r, uint8_t v) { cpu_set_reg (r, (cpu_reg (r) & 0xffffff00u) | v); }
void set8h (int r, uint8_t v) { cpu_set_reg (r, (cpu_reg (r) & 0xffff00ffu) | (uint32_t) v << 8); }
void set_cf (int on) { cpu_set_flags (on ? cpu_flags () | 1 : cpu_flags () & ~1u); }
void set_zf (int on) { cpu_set_flags (on ? cpu_flags () | 0x40 : cpu_flags () & ~0x40u); }

static uint64_t now_ns (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (uint64_t) t.tv_sec * 1000000000ull + (uint64_t) t.tv_nsec;
}
static uint64_t t0;

/* ======================================================================== PIC 8259 (maître 20h, esclave A0h) */
struct pic { uint8_t irr, imr, isr, base, icw, read_isr; };
static struct pic pic[2] = { { 0, 0xb8, 0, 0x08, 0, 0 }, { 0, 0x8f, 0, 0x70, 0, 0 } };

void pic_raise (int irq)
{
	if (irq >= 8)
	{
		pic[1].irr |= (uint8_t) (1 << (irq - 8));
		pic[0].irr |= 4;
	}
	else
		pic[0].irr |= (uint8_t) (1 << irq);
}

static int pic_best (struct pic *p)
{
	uint8_t req = p->irr & ~p->imr;
	for (int i = 0; i < 8; i++)
	{
		if (p->isr & (1 << i))
			return -1;	/* (une IRQ de même priorité ou plus haute est en service) */
		if (req & (1 << i))
			return i;
	}
	return -1;
}

/* la prochaine IRQ à servir -> son vecteur (et elle passe en service), -1 sinon */
int pic_pending (void)
{
	int i = pic_best (&pic[0]);
	if (i < 0)
		return -1;
	if (i == 2)
	{
		int j = pic_best (&pic[1]);
		if (j < 0)
		{
			pic[0].irr &= ~4;
			return -1;
		}
		pic[1].irr &= (uint8_t) ~(1 << j);
		if (!pic[1].irr)
			pic[0].irr &= ~4;
		pic[1].isr |= (uint8_t) (1 << j);
		pic[0].isr |= 4;
		return pic[1].base + j;
	}
	pic[0].irr &= (uint8_t) ~(1 << i);
	pic[0].isr |= (uint8_t) (1 << i);
	return pic[0].base + i;
}

static void pic_out (int n, int port1, uint8_t v)
{
	struct pic *p = &pic[n];
	if (!port1)
	{
		if (v & 0x10)	/* ICW1 */
		{
			p->icw = (uint8_t) (8 | (v & 1) | (v & 2 ? 0 : 2));	/* (ICW2, ICW3 en cascade, ICW4 si demandé) */
			p->imr = 0;
			p->isr = 0;
			return;
		}
		if ((v & 0x18) == 0x08)	/* OCW3 */
		{
			if (v & 2)
				p->read_isr = v & 1;
			return;
		}
		if ((v & 0xe0) == 0x20)	/* EOI non spécifique */
		{
			for (int i = 0; i < 8; i++)
				if (p->isr & (1 << i))
				{
					p->isr &= (uint8_t) ~(1 << i);
					break;
				}
		}
		else if ((v & 0xe0) == 0x60)	/* EOI spécifique */
			p->isr &= (uint8_t) ~(1 << (v & 7));
		return;
	}
	if (p->icw)
	{
		/* (icw : bit 0 ICW4 attendu, bit 1 ICW3 attendu (en cascade), bits 4+ : le rang du suivant) */
		int step = p->icw >> 4;
		if (step == 0)
			p->base = v & 0xf8;
		step++;
		if (step == 1 && !(p->icw & 2))
			step++;
		if (step == 2 && !(p->icw & 1))
			step++;
		p->icw = step >= 3 ? 0 : (uint8_t) ((p->icw & 11) | step << 4);
		return;
	}
	p->imr = v;
}

/* ======================================================================== PIT 8253 */
#define PIT_HZ 1193182ull
struct pitch { uint32_t reload; uint8_t mode, access, hi_next, rhi_next, latched; uint16_t latch; uint64_t start; };
static struct pitch pit[3];
static uint64_t irq0_done;
static uint8_t port61;

static uint64_t pit_counts (void) { return (now_ns () - t0) * PIT_HZ / 1000000000ull; }

static uint16_t pit_value (int c)
{
	uint32_t r = pit[c].reload ? pit[c].reload : 65536;
	return (uint16_t) (r - (uint32_t) ((pit_counts () - pit[c].start) % r));
}

static int pit_out2 (void)
{
	uint32_t r = pit[2].reload ? pit[2].reload : 65536;
	return (uint32_t) ((pit_counts () - pit[2].start) % r) < r / 2;
}

static void pit_update (void)
{
	uint32_t r = pit[0].reload ? pit[0].reload : 65536;
	uint64_t due = (pit_counts () - pit[0].start) / r;
	if (due > irq0_done + 4)
		irq0_done = due - 1;	/* (en retard : on ne rattrape pas tout) */
	if (due > irq0_done && !(pic[0].irr & 1))
	{
		irq0_done++;
		pic_raise (0);
	}
}

static void pit_out (int port, uint8_t v)
{
	if (port == 3)
	{
		int c = v >> 6;
		if (c == 3)
			return;
		if (!(v & 0x30))
		{
			pit[c].latched = 1;
			pit[c].latch = pit_value (c);
			pit[c].rhi_next = 0;
			return;
		}
		pit[c].access = (v >> 4) & 3;
		pit[c].mode = (v >> 1) & 7;
		pit[c].hi_next = 0;
		pit[c].rhi_next = 0;
		return;
	}
	struct pitch *p = &pit[port];
	if (p->access == 1 || (p->access == 3 && !p->hi_next))
	{
		p->reload = (p->reload & 0xff00) | v;
		if (p->access == 3)
		{
			p->hi_next = 1;
			return;
		}
	}
	else
	{
		p->reload = (p->reload & 0x00ff) | (uint32_t) v << 8;
		p->hi_next = 0;
	}
	p->start = pit_counts ();
	if (port == 0)
		irq0_done = 0;
}

static uint8_t pit_in (int port)
{
	struct pitch *p = &pit[port];
	uint16_t v = p->latched ? p->latch : pit_value (port);
	int hi = p->access == 2 || (p->access == 3 && p->rhi_next);
	if (p->access == 3)
		p->rhi_next ^= 1;
	if (p->latched && (p->access != 3 || !p->rhi_next))
		p->latched = 0;
	return hi ? (uint8_t) (v >> 8) : (uint8_t) v;
}

/* ======================================================================== clavier */
/* code HID (positions du clavier US) -> code de balayage du jeu 1 ; 0x100 : précédé de E0 */
static uint16_t hid_scan (int k)
{
	static const uint8_t t[0x68] = {
		[0x04] = 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,
		0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c,	/* a..z */
		[0x1e] = 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,	/* 1..0 */
		[0x28] = 0x1c, 0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a, 0x1b, 0x2b, 0x2b, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35,
		0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,	/* Verr.Maj, F1..F12 */
		[0x47] = 0x46, [0x53] = 0x45, [0x55] = 0x37, [0x56] = 0x4a, [0x57] = 0x4e,
		[0x59] = 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49, 0x52, 0x53,	/* pavé 1..9, 0, . */
		[0x64] = 0x56,
	};
	switch (k)
	{
	case 0x49: return 0x152; case 0x4a: return 0x147; case 0x4b: return 0x149; case 0x4c: return 0x153;
	case 0x4d: return 0x14f; case 0x4e: return 0x151; case 0x4f: return 0x14d; case 0x50: return 0x14b;
	case 0x51: return 0x150; case 0x52: return 0x148; case 0x54: return 0x135; case 0x58: return 0x11c;
	case 0xe0: return 0x1d; case 0xe1: return 0x2a; case 0xe2: return 0x38; case 0xe4: return 0x11d;
	case 0xe5: return 0x36; case 0xe6: return 0x138;
	}
	return k >= 0 && k < 0x68 ? t[k] : 0;
}

/* les octets du clavier en attente (port 60h), avec le caractère que Korin a donné (AZERTY compris) */
#define KQ 64
static struct { uint8_t code, ascii; } kq[KQ];
static int kq_head, kq_n;
static uint8_t kbd_latch, kbd_latch_ascii, kbd_obf, kbd_cmd;

static void kq_push (uint8_t code, uint8_t ascii)
{
	if (kq_n >= KQ)
		return;
	kq[(kq_head + kq_n) % KQ].code = code;
	kq[(kq_head + kq_n) % KQ].ascii = ascii;
	kq_n++;
}

static void kbd_event (const struct be_event *e)
{
	uint16_t s = hid_scan (e->key);
	w16_log ("DOS : touche %d (car. %d) %s -> %03x\n", e->key, e->ch, e->type == BE_KEY_UP ? "relâchée" : "appuyée", s);
	if (!s)
		return;
	uint8_t ascii = 0;
	if (e->type == BE_KEY_DOWN && e->ch > 0)
		ascii = e->ch < 128 ? (uint8_t) e->ch : e->ch < 256 ? basLatin1To437[e->ch] : (uint8_t) '?';
	if (s & 0x100)
		kq_push (0xe0, 0);
	kq_push ((uint8_t) ((s & 0x7f) | (e->type == BE_KEY_UP ? 0x80 : 0)), ascii);
}

static void kbd_deliver (void)
{
	if (kbd_obf || !kq_n || (pic[0].isr & 2) || (pic[0].irr & 2))
		return;
	kbd_latch = kq[kq_head].code;
	kbd_latch_ascii = kq[kq_head].ascii;
	kq_head = (kq_head + 1) % KQ;
	kq_n--;
	kbd_obf = 1;
	pic_raise (1);
}

static int kbd_put (uint16_t key)
{
	uint16_t head = rd16 (BDA (0x1a)), tail = rd16 (BDA (0x1c));
	uint16_t start = rd16 (BDA (0x80)), end = rd16 (BDA (0x82));
	uint16_t next = (uint16_t) (tail + 2 >= end ? start : tail + 2);
	if (next == head)
		return 0;
	wr16 (0x400u + tail, key);
	wr16 (BDA (0x1c), next);
	return 1;
}

int kbd_get (int remove, uint16_t *key)
{
	uint16_t head = rd16 (BDA (0x1a)), tail = rd16 (BDA (0x1c));
	if (head == tail)
		return 0;
	*key = rd16 (0x400u + head);
	if (remove)
	{
		uint16_t start = rd16 (BDA (0x80)), end = rd16 (BDA (0x82));
		wr16 (BDA (0x1a), (uint16_t) (head + 2 >= end ? start : head + 2));
	}
	return 1;
}

/* l'INT 9 : AL = l'octet lu au port 60h -> le tampon du BIOS (codes de touches du BIOS AT) */
static void kbd_int9 (uint8_t code)
{
	static int e0;
	static const char us[] = "\0\0331234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
	static const char us_s[] = "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
	uint8_t *fl = lin_ptr (BDA (0x17));
	if (code == 0xe0 || code == 0xe1)
	{
		e0 = 1;
		return;
	}
	if (code == 0xfa || code == 0xfe)
		return;
	int rel = code & 0x80, c = code & 0x7f, ext = e0;
	e0 = 0;
	uint8_t bit = c == 0x2a ? 2 : c == 0x36 ? 1 : c == 0x1d ? 4 : c == 0x38 ? 8 : 0;
	if (bit && !(ext && c == 0x2a))
	{
		if (rel)
			fl[0] &= (uint8_t) ~bit;
		else
			fl[0] |= bit;
		return;
	}
	if (rel)
		return;
	if (c == 0x3a) { fl[0] ^= 0x40; return; }
	if (c == 0x45) { fl[0] ^= 0x20; return; }
	if (c == 0x46) { fl[0] ^= 0x10; return; }
	int shift = (fl[0] & 3) != 0, ctrl = (fl[0] & 4) != 0, alt = (fl[0] & 8) != 0;
	uint8_t host = kbd_latch_ascii;
	uint16_t key = 0;
	if (c >= 0x3b && c <= 0x44)
		key = (uint16_t) ((alt ? c + 0x2d : ctrl ? c + 0x23 : shift ? c + 0x19 : c) << 8);
	else if (c == 0x57 || c == 0x58)
		key = (uint16_t) ((alt ? c + 0x34 : ctrl ? c + 0x32 : shift ? c + 0x30 : c + 0x2e) << 8);
	else if (c >= 0x47 && c <= 0x53 && c != 0x4a && c != 0x4e && (ext || !(fl[0] & 0x20) || shift))
	{
		static const uint8_t cx[0x0d] = { 0x77, 0x8d, 0x84, 0, 0x73, 0, 0x74, 0, 0x75, 0x91, 0x76, 0x92, 0x93 };
		if (c == 0x4c)
			return;
		key = (uint16_t) ((ctrl && cx[c - 0x47] ? cx[c - 0x47] : alt && ext ? c + 0x50 : c) << 8 | (ext ? 0xe0 : 0));
		if (c == 0x52 && !ctrl && !alt)
			fl[0] ^= 0x80;
	}
	else if (alt && !(host && !isalnum (host) && host != ' '))
	{
		/* (Alt + touche ; AltGr d'un clavier français garde son caractère) */
		if (c >= 0x02 && c <= 0x0d)
			key = (uint16_t) ((c + 0x76) << 8);
		else
			key = (uint16_t) (c << 8);
	}
	else if (ctrl)
	{
		char l = c < (int) sizeof us ? us[c] : 0;
		if (l >= 'a' && l <= 'z')
			key = (uint16_t) (c << 8 | (l - 'a' + 1));
		else if (c == 0x1c)
			key = 0x1c0a;
		else if (c == 0x0e)
			key = 0x0e7f;
		else if (c == 0x1a || c == 0x2b || c == 0x1b)
			key = (uint16_t) (c << 8 | (c == 0x1a ? 0x1b : c == 0x2b ? 0x1c : 0x1d));
		else
			return;
	}
	else if (c == 0x0f && shift)
		key = 0x0f00;
	else
	{
		uint8_t a = host;
		if (!a && c < (int) sizeof us)
		{
			a = (uint8_t) (shift ? us_s[c] : us[c]);
			if ((fl[0] & 0x40) && isalpha (a))
				a = (uint8_t) (shift ? tolower (a) : toupper (a));
		}
		if (c == 0x1c)
			a = 0x0d;
		if (c == 0x0e)
			a = 0x08;
		if (c == 0x01)
			a = 0x1b;
		key = (uint16_t) ((ext && c == 0x1c ? 0xe0 : c) << 8 | a);
		if (ext && c == 0x35)
			key = 0xe02f;
	}
	kbd_put (key);
}

/* ======================================================================== ports */
static uint8_t cmos_index;

static uint8_t bcd (int v) { return (uint8_t) ((v / 10) << 4 | (v % 10)); }

static uint8_t cmos_read (void)
{
	time_t t = time (0);
	struct tm *tm = localtime (&t);
	uint32_t ext = (W16_MEM_SIZE >> 10) - 1024;
	switch (cmos_index & 0x7f)
	{
	case 0x00: return bcd (tm->tm_sec);
	case 0x02: return bcd (tm->tm_min);
	case 0x04: return bcd (tm->tm_hour);
	case 0x06: return bcd (tm->tm_wday + 1);
	case 0x07: return bcd (tm->tm_mday);
	case 0x08: return bcd (tm->tm_mon + 1);
	case 0x09: return bcd (tm->tm_year % 100);
	case 0x32: return bcd (19 + tm->tm_year / 100);
	case 0x0a: return 0x26;
	case 0x0b: return 0x02;
	case 0x0d: return 0x80;
	case 0x10: return 0x40;	/* (une disquette de 1,44 Mo) */
	case 0x14: return 0x2f;
	case 0x15: return 0x80;
	case 0x16: return 0x02;
	case 0x17: case 0x30: return (uint8_t) ext;
	case 0x18: case 0x31: return (uint8_t) (ext >> 8);
	}
	return 0;
}

static uint32_t io_in (uint16_t port, int size)
{
	(void) size;
	switch (port)
	{
	case 0x20: return pic[0].read_isr ? pic[0].isr : pic[0].irr;
	case 0x21: return pic[0].imr;
	case 0xa0: return pic[1].read_isr ? pic[1].isr : pic[1].irr;
	case 0xa1: return pic[1].imr;
	case 0x40: case 0x41: case 0x42: return pit_in (port - 0x40);
	case 0x60:
		kbd_obf = 0;
		return kbd_latch;
	case 0x61:
		return (uint32_t) (port61 & 0x0f) | ((pit_counts () / 18) & 1) << 4 | (uint32_t) pit_out2 () << 5;
	case 0x64: return 0x1c | kbd_obf;
	case 0x71: return cmos_read ();
	case 0x92: return cpu_a20 () ? 2 : 0;
	case 0x201: return 0xff;	/* (pas de manette) */
	}
	if ((port >= 0x3b0 && port <= 0x3df))
		return vga_in (port);
	static uint8_t seen[0x400];
	if (port < 0x400 && !seen[port])
	{
		seen[port] = 1;
		w16_log ("DOS : port %03xh lu (rien)\n", port);
	}
	return 0xff;
}

static void io_out (uint16_t port, uint32_t v32, int size)
{
	(void) size;
	uint8_t v = (uint8_t) v32;
	switch (port)
	{
	case 0x20: case 0x21: pic_out (0, port & 1, v); return;
	case 0xa0: case 0xa1: pic_out (1, port & 1, v); return;
	case 0x40: case 0x41: case 0x42: case 0x43: pit_out (port - 0x40, v); return;
	case 0x61: port61 = v; return;
	case 0x60:
		if (kbd_cmd == 0xd1)
			cpu_set_a20 ((v & 2) != 0);
		else
			kq_push (0xfa, 0);	/* (une commande du clavier : « reçu ») */
		kbd_cmd = 0;
		return;
	case 0x64:
		kbd_cmd = v;
		if (v == 0xdd || v == 0xdf)
			cpu_set_a20 (v == 0xdf);
		return;
	case 0x70: cmos_index = v; return;
	case 0x71: return;
	case 0x80: return;
	case 0x92: cpu_set_a20 ((v & 2) != 0); return;
	}
	if (port >= 0x3b0 && port <= 0x3df)
	{
		vga_out (port, v);
		return;
	}
	static uint8_t seen[0x400];
	if (port < 0x400 && !seen[port])
	{
		seen[port] = 1;
		w16_log ("DOS : port %03xh écrit (%02x, rien)\n", port, v);
	}
}

/* ======================================================================== la fenêtre et le temps */
void bios_set_title (const char *title)
{
	snprintf (win_title, sizeof win_title, "%s", title);
	if (bios_win)
		be_window_title (bios_win, win_title);
}

static uint64_t last_frame, last_events;

static void handle_event (const struct be_event *e)
{
	switch (e->type)
	{
	case BE_CLOSE:
		dos_closed = 1;
		w16_exit = 1;
		w16_exit_code = 0;
		break;
	case BE_KEY_DOWN:
	case BE_KEY_UP:
		kbd_event (e);
		break;
	}
}

static void frame (void)
{
	if (!bios_win)
	{
		bios_win = be_window_open (-1, -1, vga_width, vga_height, win_title);
		if (!bios_win)
			return;
	}
	vga_render ();
}

/* appelée souvent par la boucle : le PIT, le clavier, l'image (60 Hz), les événements (toutes les 10 ms) */
void bios_poll (int force)
{
	uint64_t t = now_ns ();
	pit_update ();
	if (force || t - last_events > 10000000ull)
	{
		last_events = t;
		struct be_event e;
		for (int n = 0; n < 32 && be_wait (0, &e); n++)
			handle_event (&e);
	}
	if (force || t - last_frame > 16666666ull)
	{
		last_frame = t;
		frame ();
	}
	kbd_deliver ();
}

/* rien à faire avant la prochaine interruption : attendre un événement au plus ms (ou le prochain tic) */
void bios_idle (int ms)
{
	frame ();
	uint32_t r = pit[0].reload ? pit[0].reload : 65536;
	uint64_t next = t0 + (pit[0].start + (irq0_done + 1) * r) * 1000000000ull / PIT_HZ;
	uint64_t t = now_ns ();
	int wait = next > t ? (int) ((next - t) / 1000000ull) : 0;
	if (wait > ms)
		wait = ms;
	struct be_event e;
	if (be_wait (wait > 0 ? wait : 1, &e))
		handle_event (&e);
	bios_poll (0);
}

/* ======================================================================== les interruptions du BIOS */
static void tick (void)
{
	uint32_t t = rd32 (BDA (0x6c)) + 1;
	if (t >= 0x1800b0)
	{
		t = 0;
		lin_ptr (BDA (0x70))[0] = 1;
	}
	wr32 (BDA (0x6c), t);
}

void bios_far (int n)
{
	switch (n)
	{
	case FAR_TICK:
		tick ();
		break;
	case FAR_KBD:
		kbd_int9 (rAL);
		break;
	}
}

static int idle_polls;

static int int16 (void)
{
	uint16_t k;
	switch (rAH)
	{
	case 0x00:
	case 0x10:
		if (!kbd_get (1, &k))
			return WAIT;
		idle_polls = 0;
		if (rAH == 0x00 && (k & 0xff) == 0xe0)
			k &= 0xff00;
		if (rAH == 0x00 && (k >> 8) == 0xe0)
			k = (uint16_t) (0x1c00 | (k & 0xff));
		set16 (R_EAX, k);
		return DONE;
	case 0x01:
	case 0x11:
		if (kbd_get (0, &k))
		{
			idle_polls = 0;
			if (rAH == 0x01 && (k & 0xff) == 0xe0)
				k &= 0xff00;
			set16 (R_EAX, k);
			set_zf (0);
		}
		else
		{
			set_zf (1);
			if (++idle_polls > 20)	/* (un programme qui attend en sondant : laisser souffler Korin) */
			{
				idle_polls = 0;
				bios_idle (2);
			}
		}
		return DONE;
	case 0x02:
		set8l (R_EAX, lin_ptr (BDA (0x17))[0]);
		return DONE;
	case 0x12:
		set8l (R_EAX, lin_ptr (BDA (0x17))[0]);
		set8h (R_EAX, lin_ptr (BDA (0x18))[0]);
		return DONE;
	case 0x05:
		set8l (R_EAX, kbd_put (rCX) ? 0 : 1);
		return DONE;
	case 0x0a:
		set16 (R_EBX, 0x41ab);
		return DONE;
	}
	return DONE;
}

static int int15 (void)
{
	set_cf (0);
	switch (rAH)
	{
	case 0x24:
		if (rAL <= 1)
			cpu_set_a20 (rAL);
		else if (rAL == 2)
			set8l (R_EAX, (uint8_t) cpu_a20 ());
		else
			set16 (R_EBX, 3);
		set8h (R_EAX, 0);
		return DONE;
	case 0x4f:	/* (interception du clavier : garder la touche) */
		set_cf (1);
		return DONE;
	case 0x86:	/* attendre CX:DX µs */
	{
		uint64_t us = (uint32_t) rCX << 16 | rDX, end = now_ns () + us * 1000;
		while (now_ns () < end && !w16_exit)
			bios_idle ((int) ((end - now_ns ()) / 1000000) + 1);
		return DONE;
	}
	case 0x88:	/* la mémoire étendue : celle de XMS, pas encore */
		set16 (R_EAX, 0);
		return DONE;
	case 0xc0:
		cpu_set_sreg (S_ES, ROM_SEG);
		set16 (R_EBX, 0xe6f5);
		set8h (R_EAX, 0);
		return DONE;
	}
	set8h (R_EAX, 0x86);
	set_cf (1);
	return DONE;
}

static int int1a (void)
{
	time_t t = time (0);
	struct tm *tm = localtime (&t);
	set_cf (0);
	switch (rAH)
	{
	case 0x00:
		set16 (R_ECX, (uint16_t) (rd32 (BDA (0x6c)) >> 16));
		set16 (R_EDX, (uint16_t) rd32 (BDA (0x6c)));
		set8l (R_EAX, lin_ptr (BDA (0x70))[0]);
		lin_ptr (BDA (0x70))[0] = 0;
		break;
	case 0x01:
		wr32 (BDA (0x6c), (uint32_t) rCX << 16 | rDX);
		break;
	case 0x02:
		set8h (R_ECX, bcd (tm->tm_hour));
		set8l (R_ECX, bcd (tm->tm_min));
		set8h (R_EDX, bcd (tm->tm_sec));
		set8l (R_EDX, 0);
		break;
	case 0x04:
		set8h (R_ECX, bcd (19 + tm->tm_year / 100));
		set8l (R_ECX, bcd (tm->tm_year % 100));
		set8h (R_EDX, bcd (tm->tm_mon + 1));
		set8l (R_EDX, bcd (tm->tm_mday));
		break;
	}
	return DONE;
}

/* -> DONE, WAIT, ou -1 : ce n'est pas un vecteur du BIOS */
int bios_int (int vec)
{
	switch (vec)
	{
	case 0x10: return vga_int10 ();
	case 0x11: set16 (R_EAX, rd16 (BDA (0x10))); return DONE;
	case 0x12: set16 (R_EAX, rd16 (BDA (0x13))); return DONE;
	case 0x13: set8h (R_EAX, 1); set_cf (1); return DONE;	/* (pas de disque du BIOS) */
	case 0x14: set8h (R_EAX, 0x80); return DONE;
	case 0x15: return int15 ();
	case 0x16: return int16 ();
	case 0x17: set8h (R_EAX, 0x90); return DONE;	/* (imprimante prête ; rien n'est imprimé) */
	case 0x1a: return int1a ();
	case 0x05: case 0x01: case 0x03: case 0x04: case 0x1b: case 0x18: case 0x19:
		return DONE;
	}
	return -1;
}

/* ======================================================================== mise en place */
static void put_bytes (uint16_t seg, uint16_t off, const uint8_t *b, int n)
{
	memcpy (lin_ptr (RM (seg, off)), b, (size_t) n);
}

static void set_vec (int v, uint16_t seg, uint16_t off)
{
	wr16 ((uint32_t) v * 4, off);
	wr16 ((uint32_t) v * 4 + 2, seg);
}

void bios_init (void)
{
	t0 = now_ns ();
	/* la ROM : des IRET partout, puis le code des IRQ */
	memset (lin_ptr (RM (ROM_SEG, 0)), 0xcf, 0x10000);
	static const uint8_t irq0[] = { 0x50, 0x9a, FAR_TICK & 0xff, FAR_TICK >> 8, 0x00, 0xf0, 0xcd, 0x1c, 0xb0, 0x20, 0xe6, 0x20, 0x58, 0xcf };
	static const uint8_t irq1[] = { 0x50, 0xe4, 0x60, 0x9a, FAR_KBD & 0xff, FAR_KBD >> 8, 0x00, 0xf0, 0xb0, 0x20, 0xe6, 0x20, 0x58, 0xcf };
	static const uint8_t irqm[] = { 0x50, 0xb0, 0x20, 0xe6, 0x20, 0x58, 0xcf };
	static const uint8_t irqs[] = { 0x50, 0xb0, 0x20, 0xe6, 0xa0, 0xe6, 0x20, 0x58, 0xcf };
	put_bytes (ROM_SEG, ROM_CODE, irq0, sizeof irq0);
	put_bytes (ROM_SEG, ROM_CODE + 0x20, irq1, sizeof irq1);
	put_bytes (ROM_SEG, ROM_CODE + 0x40, irqm, sizeof irqm);
	put_bytes (ROM_SEG, ROM_CODE + 0x50, irqs, sizeof irqs);
	static const uint8_t cfg[] = { 0x08, 0x00, 0xfc, 0x01, 0x00, 0x60, 0, 0, 0, 0 };
	put_bytes (ROM_SEG, 0xe6f5, cfg, sizeof cfg);
	memcpy (lin_ptr (RM (ROM_SEG, 0xfff5)), "11/20/26", 8);
	lin_ptr (RM (ROM_SEG, 0xfffe))[0] = 0xfc;
	/* les polices de la ROM vidéo (code page 437) */
	memcpy (lin_ptr (RM (VROM_SEG, FONT16_OFF)), basFont16, sizeof basFont16);
	memcpy (lin_ptr (RM (VROM_SEG, FONT14_OFF)), basFont14, sizeof basFont14);
	memcpy (lin_ptr (RM (VROM_SEG, FONT8_OFF)), basFont8, sizeof basFont8);
	memcpy (lin_ptr (RM (ROM_SEG, 0xfa6e)), basFont8, 128 * 8);
	lin_ptr (RM (VROM_SEG, 0))[0] = 0x55;
	lin_ptr (RM (VROM_SEG, 0))[1] = 0xaa;
	/* la table des vecteurs : chaque vecteur sur son point d'arrêt, sauf les IRQ */
	for (int v = 0; v < 256; v++)
		set_vec (v, ROM_SEG, (uint16_t) v);
	set_vec (0x08, ROM_SEG, ROM_CODE);
	set_vec (0x09, ROM_SEG, ROM_CODE + 0x20);
	for (int v = 0x0a; v <= 0x0f; v++)
		set_vec (v, ROM_SEG, ROM_CODE + 0x40);
	for (int v = 0x70; v <= 0x77; v++)
		set_vec (v, ROM_SEG, ROM_CODE + 0x50);
	set_vec (0x1c, ROM_SEG, ROM_CODE + 0x60);
	set_vec (0x1b, ROM_SEG, ROM_CODE + 0x60);
	set_vec (0x1d, 0, 0);
	set_vec (0x1e, 0, 0);
	set_vec (0x1f, VROM_SEG, FONT8_OFF + 128 * 8);
	set_vec (0x43, VROM_SEG, FONT8_OFF);
	for (int v = 0x60; v <= 0x67; v++)	/* (vecteurs des utilisateurs : nuls, comme au démarrage) */
		set_vec (v, 0, 0);
	for (int v = 0x78; v < 0x100; v++)
		set_vec (v, 0, 0);
	/* la zone de données du BIOS */
	memset (lin_ptr (0x400), 0, 0x100);
	wr16 (BDA (0x10), 0x0026);	/* x87, souris PS/2, écran couleur 80 colonnes */
	wr16 (BDA (0x13), 640);
	wr16 (BDA (0x1a), 0x1e);
	wr16 (BDA (0x1c), 0x1e);
	wr16 (BDA (0x80), 0x1e);
	wr16 (BDA (0x82), 0x3e);
	lin_ptr (BDA (0x96))[0] = 0x10;	/* (clavier étendu) */
	{
		time_t t = time (0);
		struct tm *tm = localtime (&t);
		wr32 (BDA (0x6c), (uint32_t) ((tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec) * 1193182ull / 65536));
	}
	cpu_io_in = io_in;
	cpu_io_out = io_out;
	cpu_set_a20 (0);
	vga_init ();
}
