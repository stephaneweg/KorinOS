/* xms.c -- win16, la machine DOS : la mémoire étendue (XMS 3.0, comme HIMEM.SYS) et paginée (EMS 4.0, comme
 * EMM386) ; WIN16.md §9.2.
 *
 * XMS : INT 2Fh AX=4300h répond « présent », 4310h donne le point d'entrée F000:FAR_XMS (un appel lointain que la
 * boucle passe à xms_call). Les blocs sont dans l'arène, de XMS_LIN (après la HMA) à XMS_END (la mémoire vidéo de
 * la SVGA commence là) ; « verrouiller » rend leur adresse linéaire.
 * EMS : l'INT 67h passe par un faux pilote « EMMXXXX0 » (le nom que les programmes cherchent en vecteur:000Ah) ;
 * les pages logiques de 16 Ko sont à part, le cadre de 64 Ko est en D000 : montrer une page y recopie son contenu
 * (et range d'abord celle qui y était). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dos.h"

#define XMS_LIN		0x110000u
#define XMS_END		0x01000000u
#define MAX_XMS		64
#define EMS_SEG		0xd000
#define EMS_PAGES	512		/* 8 Mo */
#define MAX_EMS		64
#define EMM_SEG		0x00c0		/* le faux en-tête de pilote : 00C0:000A = « EMMXXXX0 », 00C0:0012 = le code */

/* ======================================================================== XMS */
static struct { uint32_t base, kb; int used, locks; } xb[MAX_XMS];

static uint32_t xms_largest (uint32_t *total)
{
	/* (l'espace libre entre les blocs, trié par adresse) */
	uint32_t big = 0, sum = 0, at = XMS_LIN;
	for (;;)
	{
		uint32_t next = XMS_END;
		int k = -1;
		for (int i = 0; i < MAX_XMS; i++)
			if (xb[i].used && xb[i].base >= at && xb[i].base < next)
			{
				next = xb[i].base;
				k = i;
			}
		uint32_t gap = (next - at) / 1024;
		sum += gap;
		if (gap > big)
			big = gap;
		if (k < 0)
			break;
		at = xb[k].base + xb[k].kb * 1024;
	}
	if (total)
		*total = sum;
	return big;
}

static uint32_t xms_place (uint32_t kb)
{
	uint32_t at = XMS_LIN;
	for (;;)
	{
		uint32_t next = XMS_END;
		int k = -1;
		for (int i = 0; i < MAX_XMS; i++)
			if (xb[i].used && xb[i].base >= at && xb[i].base < next)
			{
				next = xb[i].base;
				k = i;
			}
		if (next - at >= kb * 1024)
			return at;
		if (k < 0)
			return 0;
		at = xb[k].base + xb[k].kb * 1024;
	}
}

static int xms_alloc (uint32_t kb)
{
	for (int h = 1; h < MAX_XMS; h++)
		if (!xb[h].used)
		{
			uint32_t at = xms_place (kb ? kb : 1);
			if (!at)
				return -1;
			xb[h].used = 1;
			xb[h].base = at;
			xb[h].kb = kb;
			xb[h].locks = 0;
			return h;
		}
	return -2;
}

static int valid (int h) { return h > 0 && h < MAX_XMS && xb[h].used; }

static void xms_ok (void) { set16 (R_EAX, 1); set8l (R_EBX, 0); }
static void xms_err (uint8_t e) { set16 (R_EAX, 0); set8l (R_EBX, e); }

/* un « pointeur » de déplacement : poignée 0 = seg:off du mode réel, sinon le décalage dans le bloc */
static int move_addr (uint16_t h, uint32_t off, uint32_t len, uint32_t *lin)
{
	if (!h)
	{
		*lin = RM (off >> 16, off & 0xffff);
		return 1;
	}
	if (!valid (h) || off + len > xb[h].kb * 1024)
		return 0;
	*lin = xb[h].base + off;
	return 1;
}

/* une copie dans la mémoire du PC : la fenêtre de la carte vidéo (A0000-AFFFF, banques, plans) passe par la carte,
 * comme sur une vraie machine (FutureLib copie ainsi l'écran vers la XMS) */
static int in_vga (uint32_t a, uint32_t n) { return cpu_vga_on && a < 0xb0000 && a + n > 0xa0000; }

static void copy (uint32_t dst, uint32_t src, uint32_t len)
{
	if (!in_vga (dst, len) && !in_vga (src, len))
	{
		memmove (lin_ptr (dst), lin_ptr (src), len);
		return;
	}
	for (uint32_t i = 0; i < len; i++)
	{
		uint32_t s = src + i, d = dst + i;
		uint8_t v = in_vga (s, 1) ? cpu_vga_rd (s) : lin_ptr (s)[0];
		if (in_vga (d, 1))
			cpu_vga_wr (d, v);
		else
			lin_ptr (d)[0] = v;
	}
}

void xms_call (void)
{
	switch (rAH)
	{
	case 0x00:
		set16 (R_EAX, 0x0300);
		set16 (R_EBX, 0x0300);
		set16 (R_EDX, 1);
		break;
	case 0x01: case 0x02:	/* la HMA : à qui la demande */
		xms_ok ();
		break;
	case 0x03: case 0x05:
		cpu_set_a20 (1);
		xms_ok ();
		break;
	case 0x04: case 0x06:
		cpu_set_a20 (0);
		xms_ok ();
		break;
	case 0x07:
		set16 (R_EAX, (uint16_t) cpu_a20 ());
		set8l (R_EBX, 0);
		break;
	case 0x08:
	case 0x88:
	{
		uint32_t total, big = xms_largest (&total);
		if (rAH == 0x88)
		{
			cpu_set_reg (R_EAX, big);
			cpu_set_reg (R_EDX, total);
			cpu_set_reg (R_ECX, XMS_END - 1);
		}
		else
		{
			set16 (R_EAX, (uint16_t) (big > 0xffff ? 0xffff : big));
			set16 (R_EDX, (uint16_t) (total > 0xffff ? 0xffff : total));
		}
		set8l (R_EBX, 0);
		break;
	}
	case 0x09:
	case 0x89:
	{
		uint32_t kb = rAH == 0x89 ? cpu_reg (R_EDX) : rDX;
		int h = xms_alloc (kb);
		if (h < 0)
			xms_err (h == -1 ? 0xa0 : 0xa1);
		else
		{
			xms_ok ();
			set16 (R_EDX, (uint16_t) h);
		}
		break;
	}
	case 0x0a:
		if (!valid (rDX))
			xms_err (0xa2);
		else
		{
			xb[rDX].used = 0;
			xms_ok ();
		}
		break;
	case 0x0b:
	{
		uint32_t m = RM (cpu_sreg (S_DS), rSI);
		uint32_t len = rd32 (m), src, dst;
		if (!move_addr (rd16 (m + 4), rd32 (m + 6), len, &src))
		{
			xms_err (0xa3);
			break;
		}
		if (!move_addr (rd16 (m + 10), rd32 (m + 12), len, &dst))
		{
			xms_err (0xa5);
			break;
		}
		copy (dst, src, len);
		xms_ok ();
		break;
	}
	case 0x0c:
		if (!valid (rDX))
			xms_err (0xa2);
		else
		{
			xb[rDX].locks++;
			uint32_t a = xb[rDX].base;
			set16 (R_EAX, 1);
			set16 (R_EBX, (uint16_t) a);
			set16 (R_EDX, (uint16_t) (a >> 16));
		}
		break;
	case 0x0d:
		if (!valid (rDX))
			xms_err (0xa2);
		else
		{
			if (xb[rDX].locks)
				xb[rDX].locks--;
			xms_ok ();
		}
		break;
	case 0x0e:
	case 0x8e:
		if (!valid (rDX))
			xms_err (0xa2);
		else
		{
			int fr = 0;
			for (int i = 1; i < MAX_XMS; i++)
				fr += !xb[i].used;
			set16 (R_EAX, 1);
			set8h (R_EBX, (uint8_t) xb[rDX].locks);
			set8l (R_EBX, (uint8_t) fr);
			if (rAH == 0x8e)
				cpu_set_reg (R_EDX, xb[rDX].kb);
			else
				set16 (R_EDX, (uint16_t) xb[rDX].kb);
		}
		break;
	case 0x0f:
	case 0x8f:
	{
		uint32_t kb = rAH == 0x8f ? cpu_reg (R_EBX) : rBX;
		int h = rDX;
		if (!valid (h))
		{
			xms_err (0xa2);
			break;
		}
		if (kb <= xb[h].kb)
		{
			xb[h].kb = kb;
			xms_ok ();
			break;
		}
		/* (agrandir : déplacer le bloc si la place manque derrière lui) */
		uint32_t old = xb[h].base, oldkb = xb[h].kb;
		xb[h].used = 0;
		uint32_t at = xms_place (kb);
		if (!at)
		{
			xb[h].used = 1;
			xms_err (0xa0);
			break;
		}
		memmove (lin_ptr (at), lin_ptr (old), oldkb * 1024);
		xb[h].used = 1;
		xb[h].base = at;
		xb[h].kb = kb;
		xms_ok ();
		break;
	}
	case 0x10:	/* (pas de mémoire haute UMB) */
		set16 (R_EAX, 0);
		set8l (R_EBX, 0xb1);
		set16 (R_EDX, 0);
		break;
	default:
		xms_err (0x80);
		break;
	}
}

/* ======================================================================== EMS */
static uint8_t *ems_mem;		/* les pages logiques */
static int16_t page_owner[EMS_PAGES];	/* poignée de chaque page, -1 libre */
static struct { int used, n; int16_t pages[EMS_PAGES]; char name[8]; } eh[MAX_EMS];
static int16_t frame[4];		/* la page globale montrée dans chaque page physique, -1 aucune */
static int16_t saved_map[MAX_EMS][4];

static void frame_store (int p)
{
	if (frame[p] >= 0)
		memcpy (ems_mem + frame[p] * 16384, lin_ptr (RM (EMS_SEG, p * 16384)), 16384);
}

static void frame_show (int p, int g)
{
	if (frame[p] == g)
		return;
	frame_store (p);
	frame[p] = (int16_t) g;
	if (g >= 0)
		memcpy (lin_ptr (RM (EMS_SEG, p * 16384)), ems_mem + g * 16384, 16384);
}

static int free_pages (void)
{
	int n = 0;
	for (int i = 0; i < EMS_PAGES; i++)
		n += page_owner[i] < 0;
	return n;
}

static int ems_grow (int h, int n)
{
	if (n > free_pages ())
		return 0;
	for (int i = 0; i < EMS_PAGES && n > 0; i++)
		if (page_owner[i] < 0)
		{
			page_owner[i] = (int16_t) h;
			eh[h].pages[eh[h].n++] = (int16_t) i;
			memset (ems_mem + i * 16384, 0, 16384);
			n--;
		}
	return 1;
}

static void ems_shrink (int h, int n)
{
	while (eh[h].n > n)
	{
		int g = eh[h].pages[--eh[h].n];
		for (int p = 0; p < 4; p++)
			if (frame[p] == g)
				frame[p] = -1;
		page_owner[g] = -1;
	}
}

static int map (int h, int phys, int logical)
{
	if (phys < 0 || phys > 3)
		return 0x8b;
	if (logical == 0xffff || logical == -1)
	{
		frame_show (phys, -1);
		return 0;
	}
	if (logical >= eh[h].n)
		return 0x8a;
	frame_show (phys, eh[h].pages[logical]);
	return 0;
}

/* un côté de la fonction 57h (type, poignée, décalage, segment ou page) : l'octet i -> un pointeur dans les pages
 * d'EMS, ou 0 pour la mémoire du PC (conv_lin donne alors son adresse) */
static uint8_t *ems_addr (uint32_t d, uint32_t i, int *st)
{
	if (!lin_ptr (d)[0])
		return 0;
	int h = rd16 (d + 1);
	uint32_t off = rd16 (d + 3) + i, page = rd16 (d + 5) + off / 16384;
	if (h < 0 || h >= MAX_EMS || !eh[h].used)
	{
		*st = 0x83;
		return 0;
	}
	if (page >= (uint32_t) eh[h].n)
	{
		*st = 0x8a;
		return 0;
	}
	return ems_mem + eh[h].pages[page] * 16384 + off % 16384;
}

static uint32_t conv_lin (uint32_t d, uint32_t i) { return RM (rd16 (d + 5), 0) + rd16 (d + 3) + i; }

int ems_int67 (void)
{
	int h = rDX, st = 0;
	int hv = h >= 0 && h < MAX_EMS && eh[h].used;
	switch (rAH)
	{
	case 0x40: break;
	case 0x41: set16 (R_EBX, EMS_SEG); break;
	case 0x42: set16 (R_EBX, (uint16_t) free_pages ()); set16 (R_EDX, EMS_PAGES); break;
	case 0x43:
	case 0x5a:
	{
		int k = 1;
		while (k < MAX_EMS && eh[k].used)
			k++;
		if (k >= MAX_EMS)
		{
			st = 0x85;
			break;
		}
		if (rBX > free_pages ())
		{
			st = rBX > EMS_PAGES ? 0x87 : 0x88;
			break;
		}
		memset (&eh[k], 0, sizeof eh[k]);
		eh[k].used = 1;
		ems_grow (k, rBX);
		set16 (R_EDX, (uint16_t) k);
		break;
	}
	case 0x44:
		st = hv ? map (h, rAL, (int16_t) rBX) : 0x83;
		break;
	case 0x45:
		if (!hv)
			st = 0x83;
		else
		{
			ems_shrink (h, 0);
			eh[h].used = h == 0;	/* (la poignée 0, du système, reste) */
		}
		break;
	case 0x46: set8l (R_EAX, 0x40); break;
	case 0x47:
		if (!hv)
			st = 0x83;
		else
			memcpy (saved_map[h], frame, sizeof frame);
		break;
	case 0x48:
		if (!hv)
			st = 0x83;
		else
			for (int p = 0; p < 4; p++)
				frame_show (p, saved_map[h][p]);
		break;
	case 0x4b:
	{
		int n = 0;
		for (int i = 0; i < MAX_EMS; i++)
			n += eh[i].used;
		set16 (R_EBX, (uint16_t) n);
		break;
	}
	case 0x4c:
		if (!hv)
			st = 0x83;
		else
			set16 (R_EBX, (uint16_t) eh[h].n);
		break;
	case 0x4d:
	{
		uint32_t d = RM (cpu_sreg (S_ES), rDI);
		int n = 0;
		for (int i = 0; i < MAX_EMS; i++)
			if (eh[i].used)
			{
				wr16 (d + (uint32_t) n * 4, (uint16_t) i);
				wr16 (d + (uint32_t) n * 4 + 2, (uint16_t) eh[i].n);
				n++;
			}
		set16 (R_EBX, (uint16_t) n);
		break;
	}
	case 0x4e:	/* (la carte des pages : 4 mots) */
	{
		if (rAL == 3)
		{
			set8l (R_EAX, 8);
			break;
		}
		if (rAL == 0 || rAL == 2)
			memcpy (lin_ptr (RM (cpu_sreg (S_ES), rDI)), frame, sizeof frame);
		if (rAL == 1 || rAL == 2)
		{
			const int16_t *m = (const int16_t *) lin_ptr (RM (cpu_sreg (S_DS), rSI));
			for (int p = 0; p < 4; p++)
				frame_show (p, m[p] >= 0 && m[p] < EMS_PAGES ? m[p] : -1);
		}
		break;
	}
	case 0x50:
	{
		uint32_t s = RM (cpu_sreg (S_DS), rSI);
		if (!hv)
		{
			st = 0x83;
			break;
		}
		for (int i = 0; i < rCX && !st; i++)
		{
			int lg = (int16_t) rd16 (s + (uint32_t) i * 4), ph = rd16 (s + (uint32_t) i * 4 + 2);
			if (rAL == 1)
				ph = (ph - EMS_SEG) / 0x400;
			st = map (h, ph, lg);
		}
		break;
	}
	case 0x51:
		if (!hv)
			st = 0x83;
		else if (rBX > eh[h].n && !ems_grow (h, rBX - eh[h].n))
			st = 0x88;
		else
		{
			ems_shrink (h, rBX);
			set16 (R_EBX, (uint16_t) eh[h].n);
		}
		break;
	case 0x53:
		if (!hv)
			st = 0x83;
		else if (rAL == 0)
			memcpy (lin_ptr (RM (cpu_sreg (S_ES), rDI)), eh[h].name, 8);
		else
			memcpy (eh[h].name, lin_ptr (RM (cpu_sreg (S_DS), rSI)), 8);
		break;
	case 0x57:	/* copier (AL=0) ou échanger (AL=1) une zone : mémoire du PC ou pages d'EMS */
	{
		uint32_t s = RM (cpu_sreg (S_DS), rSI);
		uint32_t len = rd32 (s);
		/* (les pages montrées dans le cadre d'abord rangées, puis remontrées après) */
		for (int p = 0; p < 4; p++)
			frame_store (p);
		for (uint32_t i = 0; i < len && !st; i++)
		{
			uint8_t *a = ems_addr (s + 4, i, &st), *b = st ? 0 : ems_addr (s + 11, i, &st);
			if (st)
				break;
			uint32_t la = conv_lin (s + 4, i), lb = conv_lin (s + 11, i);
			uint8_t va = a ? *a : in_vga (la, 1) ? cpu_vga_rd (la) : lin_ptr (la)[0];
			uint8_t vb = rAL == 1 ? (b ? *b : in_vga (lb, 1) ? cpu_vga_rd (lb) : lin_ptr (lb)[0]) : 0;
			if (b)
				*b = va;
			else if (in_vga (lb, 1))
				cpu_vga_wr (lb, va);
			else
				lin_ptr (lb)[0] = va;
			if (rAL == 1)
			{
				if (a)
					*a = vb;
				else if (in_vga (la, 1))
					cpu_vga_wr (la, vb);
				else
					lin_ptr (la)[0] = vb;
			}
		}
		for (int p = 0; p < 4; p++)
			if (frame[p] >= 0)
				memcpy (lin_ptr (RM (EMS_SEG, p * 16384)), ems_mem + frame[p] * 16384, 16384);
		break;
	}
	case 0x58:
		if (rAL == 0)
			for (int p = 0; p < 4; p++)
			{
				wr16 (RM (cpu_sreg (S_ES), rDI + p * 4), (uint16_t) (EMS_SEG + p * 0x400));
				wr16 (RM (cpu_sreg (S_ES), rDI + p * 4 + 2), (uint16_t) p);
			}
		set16 (R_ECX, 4);
		break;
	default:
		st = 0x84;
		break;
	}
	set8h (R_EAX, (uint8_t) st);
	return DONE;
}

void xms_init (void)
{
	memset (xb, 0, sizeof xb);
	ems_mem = calloc (EMS_PAGES, 16384);
	for (int i = 0; i < EMS_PAGES; i++)
		page_owner[i] = -1;
	for (int p = 0; p < 4; p++)
		frame[p] = -1;
	memset (eh, 0, sizeof eh);
	eh[0].used = 1;
	/* le faux pilote d'EMS : « EMMXXXX0 » en 00C0:000A, et l'INT 67h qui saute au point d'arrêt F000:0067 */
	memcpy (lin_ptr (RM (EMM_SEG, 0x0a)), "EMMXXXX0", 8);
	static const uint8_t jmp[] = { 0xea, 0x67, 0x00, 0x00, 0xf0 };
	memcpy (lin_ptr (RM (EMM_SEG, 0x12)), jmp, sizeof jmp);
	wr16 (0x67 * 4, 0x12);
	wr16 (0x67 * 4 + 2, EMM_SEG);
}
