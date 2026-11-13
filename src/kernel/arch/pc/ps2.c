/* Clavier PS/2 (contrôleur i8042 ; sur le ZBook, l'EC en ACPI HPQ8002).
 * Codes du jeu 1 (traduction du contrôleur laissée active), disposition
 * AZERTY française, ou belge avec « keyb=BE » dans les options du noyau
 * (\korin\boot.cfg, « cmdline = keyb=BE »), caractères Latin-1. Seulement pour
 * le moniteur du noyau (option « monitor », ou secours quand SD0: ne vient pas) :
 * sinon le clavier est au service ps2kbd (src/svc/ps2kbd), qui lit l'i8042 par
 * IO_PORT et l'IRQ 1 par IRQ_BIND. Un seul lecteur : tant que le noyau tient le
 * contrôleur (ps2_owned), IO_PORT rend E_BUSY et la ligne ISA 1 n'est pas aux
 * pilotes ; sans ps2_init, le noyau ne touche jamais l'i8042. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/hw.h>
#include <korin/lib.h>

#include "../x86_64/cpu.h"
#include "apic.h"
#include "ps2.h"

#define DATA 0x60
#define STATUS 0x64
#define CMD 0x64

/* jeu 1 -> caractère : normal, Maj, AltGr (0 = rien) ; AZERTY de France */
static const unsigned char km_normal[0x59] = {
	[0x01] = 27, [0x02] = '&', [0x03] = 0xE9, [0x04] = '"', [0x05] = '\'', [0x06] = '(',
	[0x07] = '-', [0x08] = 0xE8, [0x09] = '_', [0x0A] = 0xE7, [0x0B] = 0xE0, [0x0C] = ')',
	[0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t', [0x10] = 'a', [0x11] = 'z', [0x12] = 'e',
	[0x13] = 'r', [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o',
	[0x19] = 'p', [0x1A] = '^', [0x1B] = '$', [0x1C] = '\n', [0x1E] = 'q', [0x1F] = 's',
	[0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
	[0x26] = 'l', [0x27] = 'm', [0x28] = 0xF9, [0x29] = 0xB2, [0x2B] = '*', [0x2C] = 'w',
	[0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = ',',
	[0x33] = ';', [0x34] = ':', [0x35] = '!', [0x37] = '*', [0x39] = ' ', [0x47] = '7',
	[0x48] = '8', [0x49] = '9', [0x4A] = '-', [0x4B] = '4', [0x4C] = '5', [0x4D] = '6',
	[0x4E] = '+', [0x4F] = '1', [0x50] = '2', [0x51] = '3', [0x52] = '0', [0x53] = '.',
	[0x56] = '<',
};
static const unsigned char km_shift[0x59] = {
	[0x01] = 27, [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
	[0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0', [0x0C] = 0xB0,
	[0x0D] = '+', [0x0E] = '\b', [0x0F] = '\t', [0x10] = 'A', [0x11] = 'Z', [0x12] = 'E',
	[0x13] = 'R', [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O',
	[0x19] = 'P', [0x1A] = 0xA8, [0x1B] = 0xA3, [0x1C] = '\n', [0x1E] = 'Q', [0x1F] = 'S',
	[0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
	[0x26] = 'L', [0x27] = 'M', [0x28] = '%', [0x2B] = 0xB5, [0x2C] = 'W', [0x2D] = 'X',
	[0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = '?', [0x33] = '.',
	[0x34] = '/', [0x35] = 0xA7, [0x37] = '*', [0x39] = ' ', [0x4A] = '-', [0x4E] = '+',
	[0x56] = '>',
};
static const unsigned char km_altgr[0x59] = {
	[0x03] = '~', [0x04] = '#', [0x05] = '{', [0x06] = '[', [0x07] = '|', [0x08] = '`',
	[0x09] = '\\', [0x0A] = '^', [0x0B] = '@', [0x0C] = ']', [0x0D] = '}', [0x1B] = 0xA4,
};

/* AZERTY belge (kbdbe de Windows, comme SD0:/res/keyb/BE.txt ; sans € : pas en
 * Latin-1) ; touches mortes : be_dead */
#define KM_PAD                                                                                     \
	[0x01] = 27, [0x0E] = '\b', [0x0F] = '\t', [0x1C] = '\n', [0x37] = '*', [0x39] = ' ',     \
	[0x4A] = '-', [0x4E] = '+'
static const unsigned char be_normal[0x59] = {
	KM_PAD, [0x02] = '&', [0x03] = 0xE9, [0x04] = '"', [0x05] = '\'', [0x06] = '(',
	[0x07] = 0xA7, [0x08] = 0xE8, [0x09] = '!', [0x0A] = 0xE7, [0x0B] = 0xE0, [0x0C] = ')',
	[0x0D] = '-', [0x10] = 'a', [0x11] = 'z', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
	[0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', [0x1A] = '^',
	[0x1B] = '$', [0x1E] = 'q', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
	[0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = 'm', [0x28] = 0xF9,
	[0x29] = 0xB2, [0x2B] = 0xB5, [0x2C] = 'w', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
	[0x30] = 'b', [0x31] = 'n', [0x32] = ',', [0x33] = ';', [0x34] = ':', [0x35] = '=',
	[0x47] = '7', [0x48] = '8', [0x49] = '9', [0x4B] = '4', [0x4C] = '5', [0x4D] = '6',
	[0x4F] = '1', [0x50] = '2', [0x51] = '3', [0x52] = '0', [0x53] = '.', [0x56] = '<',
};
static const unsigned char be_shift[0x59] = {
	KM_PAD, [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
	[0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0', [0x0C] = 0xB0,
	[0x0D] = '_', [0x10] = 'A', [0x11] = 'Z', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
	[0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P', [0x1A] = 0xA8,
	[0x1B] = '*', [0x1E] = 'Q', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
	[0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = 'M', [0x28] = '%',
	[0x29] = 0xB3, [0x2B] = 0xA3, [0x2C] = 'W', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
	[0x30] = 'B', [0x31] = 'N', [0x32] = '?', [0x33] = '.', [0x34] = '/', [0x35] = '+',
	[0x56] = '>',
};
static const unsigned char be_altgr[0x59] = {
	[0x02] = '|', [0x03] = '@', [0x04] = '#', [0x05] = '{', [0x06] = '[', [0x07] = '^',
	[0x0A] = '{', [0x0B] = '}', [0x1A] = '[', [0x1B] = ']', [0x28] = 0xB4, [0x2B] = '`',
	[0x35] = '~', [0x56] = '\\',
};
/* touches mortes du belge : code du jeu 1, couche (0 normal, 1 Maj, 2 AltGr) */
static const unsigned char be_dead[][2] = {{0x1A, 0}, {0x1A, 1}, {0x28, 2}, {0x2B, 2}, {0x35, 2}};

/* composition (Latin-1) : morte, lettres, résultats (comme SD0:/res/keyb/compose.tab) */
static const struct {
	unsigned char dead;
	const char *base;
	const unsigned char *out;
} compose_l1[] = {
	{'^', "aeiouAEIOU", (const unsigned char[]){0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB}},
	{0xA8, "aeiouyAEIOU",
	 (const unsigned char[]){0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xFF, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC}},
	{0xB4, "aeiouyAEIOUY",
	 (const unsigned char[]){0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xFD, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xDD}},
	{'`', "aeiouAEIOU", (const unsigned char[]){0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9}},
	{'~', "aonAON", (const unsigned char[]){0xE3, 0xF5, 0xF1, 0xC3, 0xD5, 0xD1}},
};

static const unsigned char *lay_normal = km_normal, *lay_shift = km_shift, *lay_altgr = km_altgr;
static int lay_be; /* 1 : belge (touches mortes) */
static unsigned char dead_pending; /* touche morte en attente (son caractère) */

#define BUF 64
static volatile unsigned char buf[BUF];
static volatile unsigned head, tail;
static int shift_l, shift_r, altgr, ctrl, caps, e0;
static volatile int owned; /* 1 : le noyau tient l'i8042 (ps2_init a réussi) */

/* options du noyau (« keyb=BE », séparées par des espaces) -> disposition ; sans
 * option, ou un autre nom : l'AZERTY de France ; rend le nom choisi */
const char *ps2_layout(const char *cmdline)
{
	for (const char *p = cmdline; p && *p;) {
		while (*p == ' ')
			p++;
		if (!strncmp(p, "keyb=", 5)) {
			const char *v = p + 5;
			if ((v[0] == 'B' || v[0] == 'b') && (v[1] == 'E' || v[1] == 'e') &&
			    (!v[2] || v[2] == ' ')) {
				lay_normal = be_normal;
				lay_shift = be_shift;
				lay_altgr = be_altgr;
				lay_be = 1;
			}
		}
		while (*p && *p != ' ')
			p++;
	}
	return lay_be ? "AZERTY belge" : "AZERTY";
}

/* morte, caractère -> composé (0 : aucun) */
static unsigned char compose(unsigned char dead, unsigned char c)
{
	for (unsigned i = 0; i < sizeof compose_l1 / sizeof compose_l1[0]; i++) {
		if (compose_l1[i].dead != dead)
			continue;
		for (unsigned j = 0; compose_l1[i].base[j]; j++)
			if ((unsigned char)compose_l1[i].base[j] == c)
				return compose_l1[i].out[j];
	}
	return 0;
}

static void push(unsigned char c)
{
	unsigned n = (head + 1) % BUF;
	if (n != tail) {
		buf[head] = c;
		head = n;
	}
}

static void key(uint8_t sc)
{
	int up = sc & 0x80;
	uint8_t code = sc & 0x7F;
	int ext = e0;
	e0 = 0;
	if (sc == 0xE0) {
		e0 = 1;
		return;
	}
	switch (code) {
	case 0x2A:
		if (!ext)
			shift_l = !up;
		return;
	case 0x36:
		shift_r = !up;
		return;
	case 0x1D:
		ctrl = !up;
		return;
	case 0x38:
		if (ext)
			altgr = !up;
		return;
	case 0x3A:
		if (!up)
			caps = !caps;
		return;
	}
	if (up || code >= sizeof km_normal)
		return;
	if (ext) {
		if (code == 0x1C)
			push('\n'); /* Entrée du pavé numérique */
		else if (code == 0x35)
			push('/');
		return;
	}
	unsigned char c;
	int sh = shift_l || shift_r;
	int layer = altgr ? 2 : sh;
	if (altgr)
		c = lay_altgr[code];
	else {
		c = sh ? lay_shift[code] : lay_normal[code];
		if (caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
			c ^= 0x20;
	}
	if (ctrl && c >= 'a' && c <= 'z')
		c = c - 'a' + 1;
	if (!c)
		return;
	int dead = 0;
	for (unsigned i = 0; lay_be && !ctrl && i < sizeof be_dead / sizeof be_dead[0]; i++)
		if (be_dead[i][0] == code && be_dead[i][1] == layer)
			dead = 1;
	unsigned char p = dead_pending;
	if (dead && !p) { /* touche morte : en attente de la suivante */
		dead_pending = c;
		return;
	}
	dead_pending = 0;
	if (p) { /* espace : la morte seule ; commande : abandonnée ; sinon composée,
		    ou la morte puis la touche */
		if (c == ' ') {
			push(p);
			return;
		}
		unsigned char r = c >= 0x20 && c != 0x7F ? compose(p, c) : 0;
		if (r) {
			push(r);
			return;
		}
		if (c >= 0x20 && c != 0x7F)
			push(p);
	}
	push(c);
}

static void kbd_irq(struct regs *r)
{
	(void)r;
	while (inb(STATUS) & 1) {
		uint8_t st = inb(STATUS);
		uint8_t v = inb(DATA);
		if (st & 0x20) /* octet de la souris */
			continue;
		key(v);
	}
}

static int wait_write(void)
{
	for (int i = 0; i < 100000; i++)
		if (!(inb(STATUS) & 2))
			return 0;
	return -1;
}

static int wait_read(void)
{
	for (int i = 0; i < 100000; i++)
		if (inb(STATUS) & 1)
			return 0;
	return -1;
}

int ps2_init(void)
{
	if (inb(STATUS) == 0xFF)
		return -1; /* pas de contrôleur */
	for (int i = 0; i < 64 && (inb(STATUS) & 1); i++)
		(void)inb(DATA);
	if (wait_write())
		return -1;
	outb(CMD, 0x20); /* lire la configuration */
	if (wait_read())
		return -1;
	uint8_t cfg = inb(DATA);
	cfg |= 1;          /* IRQ 1 */
	cfg &= ~(1 << 4);  /* horloge du clavier en marche */
	if (wait_write())
		return -1;
	outb(CMD, 0x60);
	if (wait_write())
		return -1;
	outb(DATA, cfg);
	if (wait_write())
		return -1;
	outb(CMD, 0xAE); /* port 1 actif */
	idt_set_handler(VEC_KEYBOARD, kbd_irq);
	ioapic_route_isa(1, VEC_KEYBOARD);
	owned = 1;
	return 0;
}

int ps2_owned(void)
{
	return owned;
}

/* IO_PORT (core/hw.c) : les deux ports de l'i8042 seulement (données 0x60, état et
 * commandes 0x64), pour ps2kbd ; E_BUSY tant que le noyau les tient (moniteur) */
int64_t arch_io_port(unsigned port, unsigned op, uint8_t value)
{
	if (port != DATA && port != STATUS)
		return E_PERM;
	if (owned)
		return E_BUSY;
	if (op == 0)
		return inb((uint16_t)port);
	outb((uint16_t)port, value);
	return 0;
}

int arch_getc(void)
{
	if (head == tail)
		return -1;
	unsigned char c = buf[tail];
	tail = (tail + 1) % BUF;
	return c;
}
