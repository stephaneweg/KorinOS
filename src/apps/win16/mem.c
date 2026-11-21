/* mem.c -- win16 : la LDT émulée, les sélecteurs et le tas global (GlobalAlloc), dans la mémoire émulée.
 *
 * Même rôle que wine/ldt.c et krnl386/global.c de winevdm, mais les bases des segments sont des adresses de la
 * mémoire émulée (w16_mem), pas des pointeurs de l'hôte. */
#include <string.h>
#include <stdio.h>
#include "w16.h"

/* ---- la LDT ---- */
static uint8_t sel_used[LDT_ENTRIES];
#define FIRST_SEL 16	/* (les premiers restent libres : sélecteurs « nuls » des programmes) */

void sel_set (uint16_t sel, uint32_t base, uint32_t size, int code)
{
	uint8_t *d = lin_ptr (LDT_LIN + (sel & ~7u));
	uint32_t limit = size ? size - 1 : 0;
	int gran = 0;
	if (limit > 0xfffff)
	{
		limit >>= 12;
		gran = 1;
	}
	d[0] = (uint8_t) limit;
	d[1] = (uint8_t) (limit >> 8);
	d[2] = (uint8_t) base;
	d[3] = (uint8_t) (base >> 8);
	d[4] = (uint8_t) (base >> 16);
	/* présent, DPL 3, segment ; code lisible (0x1a) ou données inscriptibles (0x12) */
	d[5] = (uint8_t) (0x80 | 0x60 | (code ? 0x1a : 0x12));
	d[6] = (uint8_t) (((limit >> 16) & 0x0f) | (gran ? 0x80 : 0));
	d[7] = (uint8_t) (base >> 24);
	sel_used[SEL_INDEX (sel)] = 1;
}

/* un sélecteur pour [base, base + size) ; au-delà de 64 Ko, des sélecteurs consécutifs (pas de 64 Ko, comme
 * Windows : __AHINCR = 8) */
uint16_t sel_alloc (uint32_t base, uint32_t size, int code)
{
	int count = (int) ((size + 0xffff) >> 16);
	if (count < 1)
		count = 1;
	for (int i = FIRST_SEL; i + count <= LDT_ENTRIES; i++)
	{
		int ok = 1;
		for (int k = 0; k < count && ok; k++)
			ok = !sel_used[i + k];
		if (!ok)
			continue;
		for (int k = 0; k < count; k++)
			sel_set (SEL (i + k), base + ((uint32_t) k << 16), size - ((uint32_t) k << 16), code);
		return SEL (i);
	}
	w16_fatal ("plus de sélecteurs libres");
}

void sel_free (uint16_t sel)
{
	int i = SEL_INDEX (sel);
	if (i < FIRST_SEL || i >= LDT_ENTRIES)
		return;
	sel_used[i] = 0;
	memset (lin_ptr (LDT_LIN + (uint32_t) i * 8), 0, 8);
}

int w16_realmode;

int sel_valid (uint16_t sel)
{
	if (w16_realmode)	/* (la machine DOS : un « sélecteur » est un paragraphe) */
		return 1;
	return SEL_INDEX (sel) < LDT_ENTRIES && sel_used[SEL_INDEX (sel)];
}

uint32_t sel_base (uint16_t sel)
{
	if (w16_realmode)
		return (uint32_t) sel << 4;
	const uint8_t *d = lin_ptr (LDT_LIN + (sel & ~7u));
	return d[2] | d[3] << 8 | d[4] << 16 | (uint32_t) d[7] << 24;
}

uint32_t sel_limit (uint16_t sel)
{
	const uint8_t *d = lin_ptr (LDT_LIN + (sel & ~7u));
	uint32_t l = d[0] | d[1] << 8 | (d[6] & 0x0f) << 16;
	return (d[6] & 0x80) ? (l << 12) | 0xfff : l;
}

/* ---- le tas linéaire : premier bloc libre assez grand, blocs alignés sur 16 octets ---- */
struct blk { uint32_t lin, size; int used; };
#define MAX_BLK 8192
static struct blk blks[MAX_BLK];
static int nblk;

uint32_t lin_alloc (uint32_t size)
{
	size = (size + 15) & ~15u;
	if (!size)
		size = 16;
	for (int i = 0; i < nblk; i++)
	{
		if (blks[i].used || blks[i].size < size)
			continue;
		if (blks[i].size > size && nblk < MAX_BLK)
		{
			memmove (&blks[i + 2], &blks[i + 1], (size_t) (nblk - i - 1) * sizeof blks[0]);
			nblk++;
			blks[i + 1].lin = blks[i].lin + size;
			blks[i + 1].size = blks[i].size - size;
			blks[i + 1].used = 0;
			blks[i].size = size;
		}
		blks[i].used = 1;
		memset (lin_ptr (blks[i].lin), 0, size);
		return blks[i].lin;
	}
	return 0;
}

void lin_free (uint32_t lin)
{
	for (int i = 0; i < nblk; i++)
	{
		if (blks[i].lin != lin || !blks[i].used)
			continue;
		blks[i].used = 0;
		if (i + 1 < nblk && !blks[i + 1].used)	/* fusion avec le suivant, puis le précédent */
		{
			blks[i].size += blks[i + 1].size;
			memmove (&blks[i + 1], &blks[i + 2], (size_t) (nblk - i - 2) * sizeof blks[0]);
			nblk--;
		}
		if (i > 0 && !blks[i - 1].used)
		{
			blks[i - 1].size += blks[i].size;
			memmove (&blks[i], &blks[i + 1], (size_t) (nblk - i - 1) * sizeof blks[0]);
			nblk--;
		}
		return;
	}
}

static uint32_t lin_size (uint32_t lin)
{
	for (int i = 0; i < nblk; i++)
		if (blks[i].lin == lin && blks[i].used)
			return blks[i].size;
	return 0;
}

static uint32_t gsize[LDT_ENTRIES];	/* taille demandée de chaque bloc global (par son premier sélecteur) */
static uint8_t gdisc[LDT_ENTRIES];	/* jeté (GlobalDiscard) : la poignée reste, GlobalLock rend NULL */

int global_discarded (uint16_t sel) { return sel_valid (sel) && gdisc[SEL_INDEX (sel)]; }
void global_set_discarded (uint16_t sel, int on) { if (sel_valid (sel)) gdisc[SEL_INDEX (sel)] = (uint8_t) on; }

/* (comme Windows et Wine : les blocs globaux vont par 32 octets, et GlobalSize rend la taille arrondie ; le tas
   de la bibliothèque C de VBRUN300 pose sa fin d'après GlobalSize et la manque si elle est impaire) */
#define GROUND(n) (((n) + 31u) & ~31u)

uint16_t global_alloc (uint32_t size, int code)
{
	size = GROUND (size ? size : 1);
	uint32_t lin = lin_alloc (size ? size : 1);
	if (!lin)
		return 0;
	uint16_t sel = sel_alloc (lin, size ? size : 1, code);
	gsize[SEL_INDEX (sel)] = size ? size : 1;
	gdisc[SEL_INDEX (sel)] = 0;
	return sel;
}

void global_free (uint16_t sel)
{
	if (!sel_valid (sel) || !gsize[SEL_INDEX (sel)])
		return;
	uint32_t lin = sel_base (sel), size = gsize[SEL_INDEX (sel)];
	int count = (int) ((size + 0xffff) >> 16);
	for (int k = 0; k < (count ? count : 1); k++)
		sel_free ((uint16_t) (sel + (k << 3)));
	gsize[SEL_INDEX (sel)] = 0;
	gdisc[SEL_INDEX (sel)] = 0;
	lin_free (lin);
}

uint32_t global_size (uint16_t sel)
{
	if (!sel_valid (sel))
		return 0;
	return gsize[SEL_INDEX (sel)] ? gsize[SEL_INDEX (sel)] : sel_limit (sel) + 1;
}

/* change la taille d'un bloc global en gardant son sélecteur (les suivants, s'il en faut plus, doivent être libres) */
int global_realloc (uint16_t sel, uint32_t size)
{
	int i = SEL_INDEX (sel);
	if (!sel_valid (sel) || !gsize[i])
		return 0;
	size = GROUND (size ? size : 1);
	uint32_t old = gsize[i];
	int oc = (int) ((old + 0xffff) >> 16), nc = (int) ((size + 0xffff) >> 16);
	if (!oc) oc = 1;
	if (!nc) nc = 1;
	for (int k = oc; k < nc; k++)
		if (i + k >= LDT_ENTRIES || sel_used[i + k])
			return 0;
	uint32_t lin = lin_alloc (size);
	if (!lin)
		return 0;
	uint32_t olin = sel_base (sel);
	memcpy (lin_ptr (lin), lin_ptr (olin), old < size ? old : size);
	lin_free (olin);
	int code = (lin_ptr (LDT_LIN + (uint32_t) i * 8)[5] & 0x08) != 0;
	for (int k = nc; k < oc; k++)
		sel_free (SEL (i + k));
	for (int k = 0; k < nc; k++)
		sel_set (SEL (i + k), lin + ((uint32_t) k << 16), size - ((uint32_t) k << 16), code);
	gsize[i] = size;
	cpu_reload_segments ();
	return 1;
}

/* un sélecteur de plus sur le même bloc (alias) : code ou données */
uint16_t sel_alias (uint16_t sel, int code)
{
	return sel_alloc (sel_base (sel), sel_limit (sel) + 1, code);
}

int sel_is_code (uint16_t sel)
{
	return (lin_ptr (LDT_LIN + (sel & ~7u))[5] & 0x08) != 0;
}

void mem_init (void)
{
	memset (sel_used, 0, sizeof sel_used);
	nblk = 1;
	blks[0].lin = HEAP_LIN;
	blks[0].size = W16_MEM_SIZE - HEAP_LIN;
	blks[0].used = 0;
}
