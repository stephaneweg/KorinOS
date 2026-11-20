/* ne.c -- win16 : le chargeur des exécutables et des bibliothèques NE (Windows 3.x).
 *
 * D'après krnl386/ne_module.c et ne_segment.c de winevdm (Wine, LGPL-2.1+) : segments, données automatiques
 * (DGROUP = taille + tas local + pile), relocations en chaîne, table des points d'entrée, noms exportés,
 * ressources, DLL du programme chargées depuis son dossier (LibMain appelé). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include "w16.h"

struct w16_task task;
struct ne_module *ne_modules[MAX_MODULES];

static uint16_t u16 (const uint8_t *p) { return (uint16_t) (p[0] | p[1] << 8); }
static uint32_t u32 (const uint8_t *p) { return u16 (p) | (uint32_t) u16 (p + 2) << 16; }

static void parse_entries (struct ne_module *m, const uint8_t *ne, uint16_t enttab, uint16_t entlen)
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
				continue;	/* (paquet vide : des ordinaux sautés, sans octets) */
			if (seg == 0xff)
			{
				if (ord < NE_MAX_ENTRY)
				{
					m->ent_seg[ord] = p[3];
					m->ent_off[ord] = u16 (p + 4);
				}
				p += 6;
			}
			else
			{
				if (ord < NE_MAX_ENTRY)
				{
					m->ent_seg[ord] = (uint8_t) seg;	/* 0xfe : constante */
					m->ent_off[ord] = u16 (p + 1);
				}
				p += 3;
			}
		}
	}
}

/* l'adresse d'un point d'entrée d'un module NE chargé */
uint32_t ne_entry (struct ne_module *m, int ord)
{
	if (ord <= 0 || ord >= NE_MAX_ENTRY || !m->ent_seg[ord])
		return 0;
	if (m->ent_seg[ord] == 0xfe)
		return MAKESEGPTR (m->ent_off[ord], m->ent_off[ord]);
	return MAKESEGPTR (m->segs[m->ent_seg[ord]], m->ent_off[ord]);
}

/* un nom exporté -> ordinal (noms résidents, puis non résidents) */
int ne_find_name (struct ne_module *m, const char *name)
{
	const uint8_t *ne = m->file + m->neo;
	const uint8_t *tabs[2] = { ne + u16 (ne + 0x26), m->nrestab ? m->file + m->nrestab : 0 };
	for (int t = 0; t < 2; t++)
	{
		const uint8_t *p = tabs[t];
		if (!p)
			continue;
		p += 1 + p[0] + 2;	/* (la première entrée : le nom du module ou sa description) */
		while (*p)
		{
			int n = *p;
			if ((int) strlen (name) == n && !strncasecmp ((const char *) p + 1, name, (size_t) n))
				return u16 (p + 1 + n);
			p += 1 + n + 2;
		}
	}
	return -1;
}

struct ne_module *ne_find_module (const char *name)
{
	for (int i = 0; i < MAX_MODULES; i++)
		if (ne_modules[i] && !strcasecmp (ne_modules[i]->name, name))
			return ne_modules[i];
	return 0;
}

struct ne_module *ne_module_by_handle (uint16_t h)
{
	if (!h)
		return ne_modules[0];
	for (int i = 0; i < MAX_MODULES; i++)
		if (ne_modules[i] && (ne_modules[i]->hmodule == h || ne_modules[i]->hinstance == h
			|| (ne_modules[i]->hinstance | 7) == (h | 7) || (ne_modules[i]->hmodule | 7) == (h | 7)))
			return ne_modules[i];
	return 0;
}

/* les données (DS) du module dont le code contient cette adresse : pour appeler une procédure de rappel */
uint16_t ne_ds_for (uint32_t target)
{
	uint16_t s = SELECTOROF (target) | 7;
	for (int i = 0; i < MAX_MODULES; i++)
	{
		struct ne_module *m = ne_modules[i];
		if (!m)
			continue;
		for (int k = 1; k <= m->nsegs; k++)
			if ((m->segs[k] | 7) == s)
				return m->hinstance;
	}
	return task.hinstance;
}

/* cherche un fichier sans tenir compte de la casse dans un dossier -> chemin complet */
int ne_find_file (const char *dir, const char *name, char *out, size_t cap)
{
	DIR *d = opendir (dir && *dir ? dir : ".");
	if (!d)
		return 0;
	struct dirent *e;
	int ok = 0;
	while ((e = readdir (d)))
		if (!strcasecmp (e->d_name, name))
		{
			snprintf (out, cap, "%s/%s", dir && *dir ? dir : ".", e->d_name);
			ok = 1;
			break;
		}
	closedir (d);
	return ok;
}

static uint32_t import_target (struct ne_module *m, int modref, int ord_or_name, int by_name, const uint8_t *ne,
	uint16_t impnam)
{
	const char *mn = m->modnames[modref - 1];
	char name[64] = "";
	if (by_name)
	{
		int n = ne[impnam + ord_or_name];
		memcpy (name, ne + impnam + ord_or_name + 1, (size_t) n < sizeof name - 1 ? (size_t) n : sizeof name - 1);
		name[n < 63 ? n : 63] = 0;
	}
	int mod = api_find_module (mn);
	if (mod >= 0)
	{
		int ord = by_name ? api_find_name (mod, name) : ord_or_name;
		if (ord < 0)
		{
			fprintf (stderr, "win16 : fonction %s.%s inconnue\n", mn, name);
			return 0;
		}
		return api_entry (mod, ord);
	}
	struct ne_module *dll = ne_find_module (mn);
	if (!dll)
		w16_fatal ("bibliothèque %s.DLL introuvable", mn);
	int ord = by_name ? ne_find_name (dll, name) : ord_or_name;
	uint32_t t = ord > 0 ? ne_entry (dll, ord) : 0;
	if (!t)
		fprintf (stderr, "win16 : %s.%s (%d) introuvable\n", mn, name, ord);
	return t;
}

static void apply_relocs (struct ne_module *m, int segno, const uint8_t *rel, const uint8_t *ne, uint16_t impnam)
{
	uint16_t sel = m->segs[segno];
	uint32_t base = sel_base (sel), seg_size = m->sizes[segno];
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
				target = ne_entry (m, t2);
				if (!target)
					w16_fatal ("%s : point d'entrée %d introuvable", m->name, t2);
			}
			else
				target = MAKESEGPTR (m->segs[t1], t2);
			break;
		case 1:
			target = import_target (m, t1, t2, 0, ne, impnam);
			break;
		case 2:
			target = import_target (m, t1, t2, 1, ne, impnam);
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
				wr16 (at, additive ? (uint16_t) (rd16 (at) + OFFSETOF (target)) : OFFSETOF (target));
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

static struct ne_module *load (const char *path, int is_main);

struct ne_module *ne_load_library (const char *name)
{
	char base[64], file[600];
	const char *b = strrchr (name, '\\');
	b = b ? b + 1 : name;
	snprintf (base, sizeof base, "%s", b);
	char *dot = strchr (base, '.');
	char mod[64];
	snprintf (mod, sizeof mod, "%.*s", dot ? (int) (dot - base) : (int) strlen (base), base);
	struct ne_module *m = ne_find_module (mod);
	if (m)
	{
		m->refs++;
		return m;
	}
	if (!dot)
		strncat (base, ".DLL", sizeof base - strlen (base) - 1);
	if (!ne_find_file (task.dir, base, file, sizeof file))
		return 0;
	return load (file, 0);
}

static struct ne_module *load (const char *path, int is_main)
{
	FILE *f = fopen (path, "rb");
	if (!f)
		return 0;
	fseek (f, 0, SEEK_END);
	long size = ftell (f);
	fseek (f, 0, SEEK_SET);
	uint8_t *d = calloc (1, (size_t) size + 16);
	if (!d || fread (d, 1, (size_t) size, f) != (size_t) size)
	{
		fclose (f);
		free (d);
		return 0;
	}
	fclose (f);
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
	if (!align)
		align = 9;
	if (is_main && (flags & 0x8000))
		w16_fatal ("%s est une bibliothèque (DLL), pas un programme", path);
	if (ne[0x36] != 2 && ne[0x36] != 0)
		w16_fatal ("%s n'est pas un programme Windows (système %d)", path, ne[0x36]);
	if (nseg > 254)
		w16_fatal ("trop de segments");
	int slot = -1;
	for (int i = 0; i < MAX_MODULES && slot < 0; i++)
		if (!ne_modules[i])
			slot = i;
	if (slot < 0)
		w16_fatal ("trop de modules");
	struct ne_module *m = calloc (1, sizeof *m);
	ne_modules[slot] = m;
	m->file = d;
	m->size = (size_t) size;
	m->neo = neo;
	m->refs = 1;
	m->is_dll = (flags & 0x8000) != 0;
	snprintf (m->path, sizeof m->path, "%s", path);
	memcpy (m->name, ne + resnam + 1, ne[resnam] < 15 ? ne[resnam] : 15);
	m->rsrc_off = rsrctab != resnam ? neo + rsrctab : 0;
	m->nrestab = u32 (ne + 0x2c);
	m->hmodule = global_alloc (64, 0);	/* (la « base de données du module » : rien dedans, une poignée) */
	wr16 (sel_base (m->hmodule), 0x454e);	/* « NE » */
	for (int i = 0; i < nmod && i < 64; i++)
	{
		uint16_t o = u16 (ne + modref + 2 * i);
		int n = ne[impnam + o];
		memcpy (m->modnames[i], ne + impnam + o + 1, n < 15 ? (size_t) n : 15);
		m->modnames[i][n < 15 ? n : 15] = 0;
	}
	parse_entries (m, ne, enttab, entlen);
	/* les DLL dont il dépend, d'abord (chargées et initialisées avant lui, comme Windows) */
	for (int i = 0; i < nmod && i < 64; i++)
		if (api_find_module (m->modnames[i]) < 0 && !ne_find_module (m->modnames[i]))
		{
			if (!ne_load_library (m->modnames[i]))
				w16_fatal ("bibliothèque %s.DLL introuvable (dossier du programme)", m->modnames[i]);
		}
	/* les segments */
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
			msize += heap + (m->is_dll ? 0 : stack);
			if (!m->is_dll && s == ss && sp == 0)
				m->sp = (uint16_t) (msize > 0xfffe ? 0xfffe : msize & ~1u);
			if (msize > 0x10000)
				msize = 0x10000;
		}
		uint16_t sel = global_alloc (msize, !(sflags & 1));
		if (!sel)
			w16_fatal ("plus de mémoire pour le segment %d", s);
		m->segs[s] = sel;
		m->sizes[s] = msize;
		if (sector && len)
		{
			if (pos + len > (uint32_t) size)
				w16_fatal ("segment %d hors du fichier", s);
			if (sflags & 0x0008)
			{
				/* segment itéré (NSITER) : des enregistrements « répétitions, octets, données » */
				const uint8_t *p = d + pos, *e = d + pos + len;
				uint32_t o = 0;
				while (p + 4 <= e)
				{
					uint16_t reps = u16 (p), n = u16 (p + 2);
					p += 4;
					for (int k = 0; k < reps && o + n <= msize; k++, o += n)
						memcpy (lin_ptr (sel_base (sel) + o), p, n);
					p += n;
				}
			}
			else
				memcpy (lin_ptr (sel_base (sel)), d + pos, len);
		}
		w16_log ("%s, segment %d : %s, %u octets, sélecteur %04x\n", m->name, s, (sflags & 1) ? "données" : "code",
			msize, sel);
	}
	m->nsegs = nseg;
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
		apply_relocs (m, s, d + rel, ne, impnam);
	}
	m->cs = cs ? m->segs[cs] : 0;
	m->ip = ip;
	m->ss = ss ? m->segs[ss] : 0;
	if (sp)
		m->sp = sp;
	m->hinstance = autodata ? m->segs[autodata] : m->hmodule;
	m->autodata = autodata;
	m->heap_size = heap;
	m->stack_size = stack;
	if (m->is_dll && m->cs)
	{
		/* LibMain : DI = instance, DS = données, CX = taille du tas, ES:SI = ligne de commande (vide) */
		uint16_t regs[4] = { m->hinstance, heap, 0, 0 };
		uint32_t r = call16_regs (MAKESEGPTR (m->cs, m->ip), autodata ? m->hinstance : 0, regs);
		w16_log ("%s : LibMain = %u\n", m->name, r & 0xffff);
		if (!(r & 0xffff))
			fprintf (stderr, "win16 : %s refuse de s'initialiser\n", m->name);
	}
	return m;
}

int ne_load (const char *path)
{
	struct ne_module *m = load (path, 1);
	if (!m)
		return 0;
	/* (le programme est chargé après ses DLL : il doit être le module 0 pour les recherches par défaut) */
	for (int i = 1; i < MAX_MODULES; i++)
		if (ne_modules[i] == m)
		{
			ne_modules[i] = ne_modules[0];
			ne_modules[0] = m;
		}
	task.main = m;
	task.cs = m->cs;
	task.ip = m->ip;
	task.ss = m->ss;
	task.sp = m->sp;
	task.hinstance = m->hinstance;
	task.hmodule = m->hmodule;
	task.heap_size = m->heap_size;
	task.stack_size = m->stack_size;
	snprintf (task.modname, sizeof task.modname, "%s", m->name);
	return 1;
}

/* ---- ressources ---- */
/* un type ou un nom de ressource : numéro (sélecteur 0 : MAKEINTRESOURCE, ou « #123 ») ou chaîne */
static int rsrc_match (const uint8_t *table, uint16_t id, uint32_t want)
{
	if (!SELECTOROF (want))
		return (id & 0x8000) && (id & 0x7fff) == OFFSETOF (want);
	const char *s = far_ptr (want);
	if (s[0] == '#')
		return (id & 0x8000) && (id & 0x7fff) == (uint16_t) atoi (s + 1);
	if (id & 0x8000)
		return 0;
	const uint8_t *n = table + id;
	return (int) strlen (s) == n[0] && !strncasecmp ((const char *) n + 1, s, n[0]);
}

/* -> poignée de ressource (index + 1 dans la table du module, avec le module en haut) ou 0 */
uint32_t ne_resource_find_ex (struct ne_module *m, uint32_t type, uint32_t name, uint32_t *off, uint32_t *size)
{
	if (!m || !m->rsrc_off)
		return 0;
	const uint8_t *r = m->file + m->rsrc_off;
	int shift = u16 (r);
	const uint8_t *p = r + 2;
	while (u16 (p))
	{
		uint16_t t = u16 (p), n = u16 (p + 2);
		const uint8_t *ent = p + 8;
		if (rsrc_match (r, t, type))
			for (int i = 0; i < n; i++, ent += 12)
				if (rsrc_match (r, u16 (ent + 6), name))
				{
					*off = (uint32_t) u16 (ent) << shift;
					*size = (uint32_t) u16 (ent + 2) << shift;
					return (uint32_t) (ent - m->file);
				}
		p += 8 + 12 * n;
	}
	return 0;
}

const char *ne_resource_find (int type, uint32_t name, uint32_t *size)
{
	uint32_t off;
	if (!ne_resource_find_ex (task.main, (uint32_t) type, name, &off, size))
		return 0;
	return (const char *) task.main->file + off;
}
