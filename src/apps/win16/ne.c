/* ne.c -- win16 : le chargeur des exécutables NE (Windows 3.x).
 *
 * D'après krnl386/ne_module.c et ne_segment.c de winevdm (Wine, LGPL-2.1+) : segments, données automatiques
 * (DGROUP = taille + tas local + pile), relocations en chaîne, table des points d'entrée, ressources. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "w16.h"

struct w16_task task;

static uint16_t u16 (const uint8_t *p) { return (uint16_t) (p[0] | p[1] << 8); }
static uint32_t u32 (const uint8_t *p) { return u16 (p) | (uint32_t) u16 (p + 2) << 16; }

/* la table des points d'entrée : ordinal -> segment, décalage */
#define MAX_ENTRY 2048
static struct { uint8_t seg; uint16_t off; } entries[MAX_ENTRY];

static void parse_entries (const uint8_t *ne, uint16_t enttab, uint16_t entlen)
{
	const uint8_t *p = ne + enttab, *end = p + entlen;
	int ord = 1;
	while (p < end && *p)
	{
		int count = p[0], seg = p[1];
		p += 2;
		for (int i = 0; i < count; i++, ord++)
		{
			if (seg == 0)
				continue;	/* (paquet vide : seulement des ordinaux sautés ; pas d'octets) */
			if (seg == 0xff)
			{
				if (ord < MAX_ENTRY)
				{
					entries[ord].seg = p[3];
					entries[ord].off = u16 (p + 4);
				}
				p += 6;
			}
			else
			{
				if (ord < MAX_ENTRY)
				{
					entries[ord].seg = (uint8_t) (seg == 0xfe ? 0 : seg);
					entries[ord].off = u16 (p + 1);
				}
				p += 3;
			}
		}
	}
}

static char modnames[64][16];

static uint32_t import_target (int modref, int ord_or_name, int by_name, const uint8_t *ne, uint16_t impnam)
{
	const char *mn = modnames[modref - 1];
	int mod = api_find_module (mn);
	if (mod < 0)
		w16_fatal ("bibliothèque %s.DLL pas encore prise en charge", mn);
	int ord = ord_or_name;
	if (by_name)
	{
		char name[64];
		int n = ne[impnam + ord_or_name];
		memcpy (name, ne + impnam + ord_or_name + 1, (size_t) n < sizeof name - 1 ? (size_t) n : sizeof name - 1);
		name[n < 63 ? n : 63] = 0;
		ord = api_find_name (mod, name);
		if (ord < 0)
			w16_fatal ("fonction %s.%s inconnue", mn, name);
	}
	return api_entry (mod, ord);
}

static void apply_relocs (int segno, const uint8_t *rel, const uint8_t *ne, uint16_t impnam, uint32_t seg_size)
{
	uint16_t sel = task.segs[segno];
	uint32_t base = sel_base (sel);
	int count = u16 (rel);
	rel += 2;
	for (int i = 0; i < count; i++, rel += 8)
	{
		int atype = rel[0], rtype = rel[1];
		uint16_t off = u16 (rel + 2), t1 = u16 (rel + 4), t2 = u16 (rel + 6);
		uint32_t target;
		switch (rtype & 3)
		{
		case 0:	/* interne */
			if (t1 == 0xff)
			{
				if (t2 >= MAX_ENTRY || !entries[t2].seg)
					w16_fatal ("point d'entrée %d introuvable", t2);
				target = MAKESEGPTR (task.segs[entries[t2].seg], entries[t2].off);
			}
			else
				target = MAKESEGPTR (task.segs[t1], t2);
			break;
		case 1:
			target = import_target (t1, t2, 0, ne, impnam);
			break;
		case 2:
			target = import_target (t1, t2, 1, ne, impnam);
			break;
		default:	/* correction du système (flottants émulés) : rien, le processeur a son x87 */
			continue;
		}
		int additive = rtype & 4;
		uint16_t o = off;
		int guard = 0;
		for (;;)
		{
			if (o >= seg_size)
				break;
			uint32_t at = base + o;
			uint16_t next = rd16 (at);
			switch (atype)
			{
			case 0:	/* octet bas */
				lin_ptr (at)[0] = (uint8_t) (additive ? lin_ptr (at)[0] + OFFSETOF (target) : OFFSETOF (target));
				break;
			case 2:	/* sélecteur */
				wr16 (at, additive ? (uint16_t) (rd16 (at) + SELECTOROF (target)) : SELECTOROF (target));
				break;
			case 3:	/* pointeur long seg:off */
				if (additive)
					wr16 (at, (uint16_t) (rd16 (at) + OFFSETOF (target)));
				else
					wr16 (at, OFFSETOF (target));
				wr16 (at + 2, SELECTOROF (target));
				break;
			case 5:	/* décalage */
				wr16 (at, additive ? (uint16_t) (rd16 (at) + OFFSETOF (target)) : OFFSETOF (target));
				break;
			default:
				w16_fatal ("relocation de type %d pas prise en charge", atype);
			}
			if (additive || next == 0xffff || ++guard > 65536)
				break;
			o = next;
		}
	}
}

int ne_load (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f)
		return 0;
	fseek (f, 0, SEEK_END);
	long size = ftell (f);
	fseek (f, 0, SEEK_SET);
	uint8_t *d = malloc ((size_t) size + 16);
	if (!d || fread (d, 1, (size_t) size, f) != (size_t) size)
	{
		fclose (f);
		return 0;
	}
	fclose (f);
	task.file = d;
	task.file_size = (size_t) size;
	snprintf (task.path, sizeof task.path, "%s", path);
	if (size < 0x40 || d[0] != 'M' || d[1] != 'Z')
		w16_fatal ("%s n'est pas un exécutable", path);
	uint32_t neo = u32 (d + 0x3c);
	if (neo + 0x40 > (uint32_t) size || d[neo] != 'N' || d[neo + 1] != 'E')
		w16_fatal ("%s n'est pas un programme Windows 16 bits (NE)", path);
	const uint8_t *ne = d + neo;
	uint16_t enttab = u16 (ne + 4), entlen = u16 (ne + 6), flags = u16 (ne + 0x0c), autodata = u16 (ne + 0x0e);
	uint16_t heap = u16 (ne + 0x10), stack = u16 (ne + 0x12);
	uint16_t ip = u16 (ne + 0x14), cs = u16 (ne + 0x16), sp = u16 (ne + 0x18), ss = u16 (ne + 0x1a);
	uint16_t nseg = u16 (ne + 0x1c), nmod = u16 (ne + 0x1e);
	uint16_t segtab = u16 (ne + 0x22), rsrctab = u16 (ne + 0x24), resnam = u16 (ne + 0x26);
	uint16_t modref = u16 (ne + 0x28), impnam = u16 (ne + 0x2a);
	int align = u16 (ne + 0x32);
	if (flags & 0x8000)
		w16_fatal ("%s est une bibliothèque (DLL), pas un programme", path);
	if (ne[0x36] != 2 && ne[0x36] != 0)
		w16_fatal ("%s n'est pas un programme Windows (système %d)", path, ne[0x36]);
	if (nseg > 254)
		w16_fatal ("trop de segments");
	memcpy (task.modname, ne + resnam + 1, ne[resnam] < 15 ? ne[resnam] : 15);
	task.rsrc_off = rsrctab != u16 (ne + 0x26) ? neo + rsrctab : 0;
	for (int i = 0; i < nmod && i < 64; i++)
	{
		uint16_t o = u16 (ne + modref + 2 * i);
		int n = ne[impnam + o];
		memcpy (modnames[i], ne + impnam + o + 1, n < 15 ? (size_t) n : 15);
		modnames[i][n < 15 ? n : 15] = 0;
	}
	parse_entries (ne, enttab, entlen);
	/* les segments */
	uint32_t sizes[256];
	for (int s = 1; s <= nseg; s++)
	{
		const uint8_t *st = ne + segtab + 8 * (s - 1);
		uint32_t sector = u16 (st), len = u16 (st + 2), sflags = u16 (st + 4), minalloc = u16 (st + 6);
		uint32_t pos = sector << align;
		if (sector && !len)
			len = 0x10000;
		if (!minalloc)
			minalloc = 0x10000;
		if (minalloc < len)
			minalloc = len;
		uint32_t msize = minalloc;
		if (s == autodata)
		{
			msize += heap + stack;
			if (s == ss && sp == 0)
				task.sp = (uint16_t) (msize > 0xfffe ? 0xfffe : msize & ~1u);
			if (msize > 0x10000)
				msize = 0x10000;
		}
		uint16_t sel = global_alloc (msize, !(sflags & 1));
		if (!sel)
			w16_fatal ("plus de mémoire pour le segment %d", s);
		task.segs[s] = sel;
		sizes[s] = msize;
		if (sector && len)
		{
			if (pos + len > (uint32_t) size)
				w16_fatal ("segment %d hors du fichier", s);
			memcpy (lin_ptr (sel_base (sel)), d + pos, len);
		}
		w16_log ("segment %d : %s, %u octets, sélecteur %04x\n", s, (sflags & 1) ? "données" : "code", msize, sel);
	}
	task.nsegs = nseg;
	for (int s = 1; s <= nseg; s++)
	{
		const uint8_t *st = ne + segtab + 8 * (s - 1);
		uint32_t sector = u16 (st), len = u16 (st + 2), sflags = u16 (st + 4);
		if (!sector || !(sflags & 0x100))
			continue;
		if (!len)
			len = 0x10000;
		uint32_t rel = (sector << align) + len;
		if (rel + 2 > (uint32_t) size)
			continue;
		apply_relocs (s, d + rel, ne, impnam, sizes[s]);
	}
	task.cs = task.segs[cs];
	task.ip = ip;
	task.ss = task.segs[ss];
	if (sp)
		task.sp = sp;
	task.hinstance = autodata ? task.segs[autodata] : 0;
	task.heap_size = heap;
	task.stack_size = stack;
	return 1;
}

/* une ressource : type et nom numériques (0x8000 | n) ou chaîne (non : pas encore) -> données dans le fichier */
const char *ne_resource_find (int type, uint32_t name, uint32_t *size)
{
	if (!task.rsrc_off)
		return 0;
	const uint8_t *r = task.file + task.rsrc_off;
	int shift = u16 (r);
	const uint8_t *p = r + 2;
	while (u16 (p))
	{
		uint16_t t = u16 (p), n = u16 (p + 2);
		const uint8_t *ent = p + 8;
		if ((t & 0x8000) && (t & 0x7fff) == type)
			for (int i = 0; i < n; i++, ent += 12)
			{
				uint16_t id = u16 (ent + 6);
				if ((id & 0x8000) && (id & 0x7fff) == (name & 0x7fff))
				{
					*size = (uint32_t) u16 (ent + 2) << shift;
					return (const char *) task.file + ((uint32_t) u16 (ent) << shift);
				}
			}
		p += 8 + 12 * n;
	}
	return 0;
}
