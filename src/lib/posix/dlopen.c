/*
 * dlopen.c -- le chargeur d'objets partages de libposix (docs/POSIX.md « dlopen », conception :
 * docs/PYTHON.md §2.5 bis) : dlopen, dlsym, dlclose, dlerror, dladdr, dl_iterate_phdr,
 * __tls_get_addr. Les programmes restent statiques (exec ne charge rien) ; un programme qui veut
 * charger des modules est un « hote » :
 *
 *  - il se relie avec gcc -rdynamic (posix.specs) : ld lui donne une table de symboles dynamiques
 *    (.dynsym, .hash, .dynamic, trouves par _DYNAMIC), qui survit a strip, et y met tout ce que
 *    libposix, newlib, libm et libgcc peuvent fournir (dlhost.ld ; dlhost-cxx.ld pour libstdc++) :
 *    un module ne porte pas sa libc, il prend celle de l'hote (un seul malloc, un seul errno, une
 *    seule table de descripteurs) ;
 *  - chaque thread a, apres le bloc TLS du programme, une reserve de 32 Ko pour les variables
 *    __thread des modules (tls.c) : chaque module y recoit un decalage fixe de TP, TLSDESC et
 *    TPREL64 se resolvent a ce decalage (modele « statique », sans allocation paresseuse).
 *
 * Un module est un ET_DYN d'AArch64 (gcc -specs=posix.specs -fPIC -shared) : ses PT_LOAD sont
 * lus dans une region de l'arene de mmap, ses relocations faites tout de suite (RELATIVE, ABS64,
 * GLOB_DAT, JUMP_SLOT, TLSDESC, TPREL64, DTPMOD64, DTPREL64, IRELATIVE ; pas de liaison
 * paresseuse), puis chaque segment recoit ses droits (et PT_GNU_RELRO la lecture seule). Ses
 * DT_NEEDED sont charges avant lui (chemin complet, ou cherche dans LD_LIBRARY_PATH, son
 * DT_RUNPATH / DT_RPATH avec $ORIGIN, puis /usr/lib ; jamais SD0:/lib, le dossier des kits).
 * Symboles : l'hote d'abord, puis les modules RTLD_GLOBAL, puis le module et ses dependances.
 * Ses tables de deroulement sont inscrites aupres de libgcc (son derouleur, unwind-dw2-fde, ne
 * passe pas par dl_iterate_phdr) : une exception C++ traverse l'hote et les modules. Ses
 * .init_array tournent au chargement, ses .fini_array a exit (__cxa_atexit, sous l'adresse du
 * module ; ses propres __cxa_atexit utilisent son __dso_handle, dlmod.o). dlclose compte les
 * references et ne decharge jamais.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include "posix_internal.h"

#ifndef R_AARCH64_IRELATIVE
#define R_AARCH64_IRELATIVE	1032
#endif

extern const Elf64_Ehdr __ehdr_start __attribute__ ((weak));	/* (posix.ld) */
extern Elf64_Dyn _DYNAMIC[] __attribute__ ((weak));		/* (gcc -rdynamic) */
extern int __cxa_atexit (void (*) (void *), void *, void *);
extern void __register_frame_info (const void *, void *) __attribute__ ((weak));

#define DL_SEARCH_DEFAULT	"/usr/lib"
#define DL_MAX_DEPS		64

struct dl_mod
{
	struct dl_mod *next;			/* ordre de chargement (l'hote d'abord) */
	char *path;				/* le chemin trouve (dli_fname, dlpi_name) */
	unsigned long base;			/* adresse de chargement - adresses du fichier */
	unsigned long lo, hi;			/* la region projetee */
	Elf64_Phdr *phdr;
	unsigned phnum;
	const Elf64_Sym *sym;
	const char *str;
	const Elf64_Word *hash;			/* DT_HASH */
	const uint32_t *gnuhash;		/* DT_GNU_HASH */
	unsigned long nsym;
	const Elf64_Dyn *dyn;
	struct dl_mod **deps;			/* DT_NEEDED, dans l'ordre */
	int ndeps;
	int refs;
	int global;				/* RTLD_GLOBAL (l'hote : toujours) */
	int ready;				/* constructeurs passes */
	unsigned long tls_off;			/* decalage de TP de son bloc TLS */
	unsigned long tls_mem, tls_file;
	const void *tls_image;
	size_t tls_modid;			/* 0 : pas de PT_TLS */
	void (**fini) (void);
	unsigned long nfini;
	void (*fini1) (void);			/* DT_FINI */
	void *eh_obj[8];			/* `struct object' de libgcc (6 mots) */
};

static struct dl_mod s_host;
static struct dl_mod *s_mods = 0, **s_tail = &s_mods;
static unsigned long long s_adds = 1;
static int s_hostInit;

/* le verrou : recursif (un constructeur de module peut appeler dlopen) */
static volatile unsigned s_lock;
static volatile int s_owner;
static int s_depth;

static void dl_lock (void)
{
	int me = __posix_owner_id ();
	if (s_owner == me)
	{
		s_depth++;
		return;
	}
	__posix_lock (&s_lock);
	s_owner = me;
	s_depth = 1;
}

static void dl_unlock (void)
{
	if (--s_depth == 0)
	{
		s_owner = 0;
		__posix_unlock (&s_lock);
	}
}

/* ---- dlerror : par thread ---- */
static __thread char t_err[256];
static __thread int t_hasErr;

static void dl_fail (const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (t_err, sizeof t_err, fmt, ap);
	va_end (ap);
	t_hasErr = 1;
}

char *dlerror (void)
{
	if (!t_hasErr)
		return 0;
	t_hasErr = 0;
	return t_err;
}

/* ---- tables de symboles ---- */
static unsigned long gnu_count (const uint32_t *h)
{
	uint32_t nb = h[0], symoff = h[1], bloom = h[2];
	const uint32_t *buckets = (const uint32_t *) ((const uint64_t *) (h + 4) + bloom);
	const uint32_t *chain = buckets + nb;
	uint32_t last = 0;
	for (uint32_t i = 0; i < nb; i++)
		if (buckets[i] > last)
			last = buckets[i];
	if (last < symoff)
		return symoff;
	while ((chain[last - symoff] & 1) == 0)
		last++;
	return last + 1;
}

static void read_dynamic (struct dl_mod *m)
{
	unsigned long b = m->base;
	for (const Elf64_Dyn *d = m->dyn; d->d_tag != DT_NULL; d++)
		switch (d->d_tag)
		{
		case DT_SYMTAB: m->sym = (const Elf64_Sym *) (b + d->d_un.d_ptr); break;
		case DT_STRTAB: m->str = (const char *) (b + d->d_un.d_ptr); break;
		case DT_HASH: m->hash = (const Elf64_Word *) (b + d->d_un.d_ptr); break;
		case DT_GNU_HASH: m->gnuhash = (const uint32_t *) (b + d->d_un.d_ptr); break;
		}
	if (m->hash)
		m->nsym = m->hash[1];
	else if (m->gnuhash)
		m->nsym = gnu_count (m->gnuhash);
}

static uint32_t sysv_hash (const char *s)
{
	uint32_t h = 0;
	while (*s)
	{
		h = (h << 4) + (unsigned char) *s++;
		uint32_t g = h & 0xF0000000u;
		if (g)
			h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

static uint32_t gnu_hash (const char *s)
{
	uint32_t h = 5381;
	while (*s)
		h = h * 33 + (unsigned char) *s++;
	return h;
}

static int sym_ok (const Elf64_Sym *s)
{
	int bind = ELF64_ST_BIND (s->st_info), type = ELF64_ST_TYPE (s->st_info);
	if (s->st_shndx == SHN_UNDEF)
		return 0;
	if (bind != STB_GLOBAL && bind != STB_WEAK && bind != STB_GNU_UNIQUE)
		return 0;
	if (ELF64_ST_VISIBILITY (s->st_other) == STV_HIDDEN || ELF64_ST_VISIBILITY (s->st_other) == STV_INTERNAL)
		return 0;
	return type == STT_NOTYPE || type == STT_OBJECT || type == STT_FUNC || type == STT_COMMON
		|| type == STT_TLS || type == STT_GNU_IFUNC;
}

/* le symbole defini `name' de m, ou 0 */
static const Elf64_Sym *mod_lookup (const struct dl_mod *m, const char *name, uint32_t hs, uint32_t hg)
{
	if (m->sym == 0 || m->str == 0)
		return 0;
	if (m->gnuhash)
	{
		const uint32_t *h = m->gnuhash;
		uint32_t nb = h[0], symoff = h[1], bloom = h[2], shift = h[3];
		const uint64_t *bl = (const uint64_t *) (h + 4);
		uint64_t w = bl[(hg / 64) % bloom];
		if (((w >> (hg % 64)) & (w >> ((hg >> shift) % 64)) & 1) == 0)
			return 0;
		const uint32_t *buckets = (const uint32_t *) (bl + bloom), *chain = buckets + nb;
		uint32_t i = buckets[hg % nb];
		if (i < symoff)
			return 0;
		for (;; i++)
		{
			uint32_t c = chain[i - symoff];
			if ((c | 1) == (hg | 1) && strcmp (m->str + m->sym[i].st_name, name) == 0
			    && sym_ok (&m->sym[i]))
				return &m->sym[i];
			if (c & 1)
				return 0;
		}
	}
	if (m->hash)
	{
		Elf64_Word nb = m->hash[0];
		const Elf64_Word *buckets = m->hash + 2, *chain = buckets + nb;
		for (Elf64_Word i = buckets[hs % nb]; i != STN_UNDEF; i = chain[i])
			if (strcmp (m->str + m->sym[i].st_name, name) == 0 && sym_ok (&m->sym[i]))
				return &m->sym[i];
	}
	return 0;
}

/* `name' dans l'ordre de recherche : l'hote, les modules globaux, puis le groupe de `self'
 * (lui et ses dependances, en largeur). *where : le module qui le definit. */
static const Elf64_Sym *group_lookup (struct dl_mod *self, const char *name, uint32_t hs, uint32_t hg,
				      struct dl_mod **where);

static const Elf64_Sym *lookup (struct dl_mod *self, const char *name, struct dl_mod **where)
{
	uint32_t hs = sysv_hash (name), hg = gnu_hash (name);
	for (struct dl_mod *m = s_mods; m; m = m->next)
		if (m->global)
		{
			const Elf64_Sym *s = mod_lookup (m, name, hs, hg);
			if (s)
			{
				*where = m;
				return s;
			}
		}
	return self ? group_lookup (self, name, hs, hg, where) : 0;
}

/* self et ses dependances, en largeur */
static const Elf64_Sym *group_lookup (struct dl_mod *self, const char *name, uint32_t hs, uint32_t hg,
				      struct dl_mod **where)
{
	struct dl_mod *queue[DL_MAX_DEPS * 2];
	int qn = 0;
	queue[qn++] = self;
	for (int i = 0; i < qn; i++)
	{
		const Elf64_Sym *s = mod_lookup (queue[i], name, hs, hg);
		if (s)
		{
			*where = queue[i];
			return s;
		}
		for (int k = 0; k < queue[i]->ndeps && qn < (int) (sizeof queue / sizeof queue[0]); k++)
		{
			int seen = 0;
			for (int j = 0; j < qn; j++)
				if (queue[j] == queue[i]->deps[k])
					seen = 1;
			if (!seen)
				queue[qn++] = queue[i]->deps[k];
		}
	}
	return 0;
}

/* ---- l'hote ---- */
static int host_init (void)
{
	if (s_hostInit)
		return _DYNAMIC != 0;
	s_hostInit = 1;
	s_host.path = (char *) __posix_exe_path ();
	s_host.base = 0;
	s_host.global = 1;
	s_host.refs = 1;
	s_host.ready = 1;
	if (&__ehdr_start != 0)
	{
		s_host.phdr = (Elf64_Phdr *) ((const char *) &__ehdr_start + __ehdr_start.e_phoff);
		s_host.phnum = __ehdr_start.e_phnum;
		for (unsigned i = 0; i < s_host.phnum; i++)
			if (s_host.phdr[i].p_type == PT_LOAD)
			{
				unsigned long e = s_host.phdr[i].p_vaddr + s_host.phdr[i].p_memsz;
				if (s_host.lo == 0 || s_host.phdr[i].p_vaddr < s_host.lo)
					s_host.lo = s_host.phdr[i].p_vaddr;
				if (e > s_host.hi)
					s_host.hi = e;
			}
			else if (s_host.phdr[i].p_type == PT_TLS)
				s_host.tls_modid = 1;
	}
	unsigned long size, prog;
	__posix_tls_reserve (&size, &prog);
	s_host.tls_off = prog;
	if (_DYNAMIC != 0)
	{
		s_host.dyn = _DYNAMIC;
		read_dynamic (&s_host);
	}
	*s_tail = &s_host;
	s_tail = &s_host.next;
	return _DYNAMIC != 0;
}

/* ---- TLS des modules ---- */
static unsigned long s_tlsNext;			/* premier decalage libre de la reserve */
static struct dl_mod *s_tlsMods[256];
static size_t s_tlsCount = 2;			/* prochain modid (0 : aucun, 1 : le programme) */

/* tls.c, sous le verrou de la liste des threads : un thread qui nait recoit les images */
void __posix_dl_tls_copy (unsigned long tp)
{
	for (size_t i = 2; i < s_tlsCount; i++)
	{
		struct dl_mod *m = s_tlsMods[i];
		if (m == 0)
			continue;
		memcpy ((char *) tp + m->tls_off, m->tls_image, m->tls_file);
		memset ((char *) tp + m->tls_off + m->tls_file, 0, m->tls_mem - m->tls_file);
	}
}

static int tls_place (struct dl_mod *m, const Elf64_Phdr *p)
{
	unsigned long size, prog;
	unsigned long start = __posix_tls_reserve (&size, &prog);
	if (size == 0)
	{
		dl_fail ("%s: variables __thread, mais le programme n'a pas de reserve TLS (gcc -rdynamic)", m->path);
		return -1;
	}
	if (s_tlsNext == 0)
		s_tlsNext = start;
	unsigned long a = p->p_align ? p->p_align : 1;
	if (a > 64)
	{
		dl_fail ("%s: alignement TLS de %lu octets (64 au plus)", m->path, a);
		return -1;
	}
	unsigned long off = (s_tlsNext + a - 1) & ~(a - 1);
	if (off + p->p_memsz > start + size || s_tlsCount >= sizeof s_tlsMods / sizeof s_tlsMods[0])
	{
		dl_fail ("%s: reserve TLS des modules epuisee (%lu octets demandes, %lu libres)", m->path,
			 (unsigned long) p->p_memsz, start + size - s_tlsNext);
		return -1;
	}
	s_tlsNext = off + p->p_memsz;
	m->tls_off = off;
	m->tls_mem = p->p_memsz;
	m->tls_file = p->p_filesz;
	m->tls_image = (const void *) (m->base + p->p_vaddr);
	return 0;
}

/* apres les relocations : l'image dans chaque thread vivant, et le module dans la liste que les
 * threads a naitre recopient */
static void tls_install (struct dl_mod *m)
{
	__posix_thread_list_lock ();
	for (struct __posix_thread *t = __posix_thread_list_first (); t; t = t->next)
	{
		char *p = (char *) __posix_thread_tp (t) + m->tls_off;
		memcpy (p, m->tls_image, m->tls_file);
		memset (p + m->tls_file, 0, m->tls_mem - m->tls_file);
	}
	m->tls_modid = s_tlsCount;
	s_tlsMods[s_tlsCount++] = m;
	__posix_thread_list_unlock ();
}

/* le resolveur de TLSDESC du modele statique : le decalage est dans le descripteur */
__asm__ (".text\n"
	 ".p2align 2\n"
	 ".type __posix_dl_tlsdesc_static, %function\n"
	 "__posix_dl_tlsdesc_static:\n"
	 "	ldr x0, [x0, #8]\n"
	 "	ret\n"
	 ".size __posix_dl_tlsdesc_static, . - __posix_dl_tlsdesc_static\n");
extern char __posix_dl_tlsdesc_static[];

/* le modele « general dynamic » (__tls_get_addr, rare sur AArch64 : TLSDESC le remplace) */
typedef struct { unsigned long ti_module, ti_offset; } tls_index;
void *__tls_get_addr (tls_index *ti)
{
	unsigned long tp;
	__asm__ volatile ("mrs %0, tpidr_el0" : "=r" (tp));
	unsigned long off = ti->ti_module == 1 ? s_host.tls_off
		: (ti->ti_module < s_tlsCount && s_tlsMods[ti->ti_module] ? s_tlsMods[ti->ti_module]->tls_off : 0);
	return (void *) (tp + off + ti->ti_offset);
}

/* ---- relocations ---- */
static int relocate (struct dl_mod *m, const Elf64_Rela *r, unsigned long size)
{
	unsigned long n = size / sizeof *r;
	for (unsigned long i = 0; i < n; i++, r++)
	{
		unsigned long *where = (unsigned long *) (m->base + r->r_offset);
		unsigned type = ELF64_R_TYPE (r->r_info), si = ELF64_R_SYM (r->r_info);
		const Elf64_Sym *s = 0;
		struct dl_mod *def = m;
		unsigned long S = 0;
		if (type == R_AARCH64_NONE)
			continue;
		if (type == R_AARCH64_RELATIVE)
		{
			*where = m->base + r->r_addend;
			continue;
		}
		if (type == R_AARCH64_IRELATIVE)
		{
			*where = ((unsigned long (*) (void)) (m->base + r->r_addend)) ();
			continue;
		}
		if (si != 0)
		{
			const Elf64_Sym *ref = &m->sym[si];
			const char *name = m->str + ref->st_name;
			if (ELF64_ST_BIND (ref->st_info) == STB_LOCAL)
				s = ref;
			else
				s = lookup (m, name, &def);
			if (s == 0)
			{
				if (ELF64_ST_BIND (ref->st_info) != STB_WEAK)
				{
					dl_fail ("%s: symbole introuvable : %s", m->path, name);
					return -1;
				}
				def = 0;
			}
			else if (ELF64_ST_TYPE (s->st_info) == STT_GNU_IFUNC && type != R_AARCH64_TLSDESC)
				S = ((unsigned long (*) (void)) (def->base + s->st_value)) ();
			else if (ELF64_ST_TYPE (s->st_info) != STT_TLS)
				S = def->base + s->st_value;
		}
		switch (type)
		{
		case R_AARCH64_ABS64:
		case R_AARCH64_GLOB_DAT:
		case R_AARCH64_JUMP_SLOT:
			*where = s ? S + r->r_addend : 0;
			break;
		case R_AARCH64_TLS_TPREL64:
		case R_AARCH64_TLSDESC:
		{
			unsigned long off = 0;
			if (si == 0)
				off = m->tls_off + r->r_addend;
			else if (s)
				off = def->tls_off + s->st_value + r->r_addend;
			if (type == R_AARCH64_TLSDESC)
			{
				where[0] = (unsigned long) __posix_dl_tlsdesc_static;
				where[1] = off;
			}
			else
				*where = off;
			break;
		}
		case R_AARCH64_TLS_DTPMOD64:
			if (si == 0 || def == m)
				*where = (unsigned long) -1;	/* (fixe apres tls_install : fix_dtpmod) */
			else
				*where = def == &s_host ? 1 : (def ? def->tls_modid : 0);
			break;
		case R_AARCH64_TLS_DTPREL64:
			*where = (s ? s->st_value : 0) + r->r_addend;
			break;
		default:
			dl_fail ("%s: relocation %u inconnue", m->path, type);
			return -1;
		}
	}
	return 0;
}

/* les DTPMOD64 du module lui-meme ne sont connus qu'apres tls_install */
static void fix_dtpmod (struct dl_mod *m, const Elf64_Rela *r, unsigned long size)
{
	for (unsigned long i = 0; i < size / sizeof *r; i++, r++)
		if (ELF64_R_TYPE (r->r_info) == R_AARCH64_TLS_DTPMOD64)
		{
			unsigned long *where = (unsigned long *) (m->base + r->r_offset);
			if (*where == (unsigned long) -1)
				*where = m->tls_modid;
		}
}

/* ---- recherche d'un fichier ---- */
static int try_path (const char *dir, size_t n, const char *name, const char *origin, char *out)
{
	char d[PATH_MAX];
	if (n >= sizeof d)
		return 0;
	memcpy (d, dir, n);
	d[n] = 0;
	char *o = strstr (d, "$ORIGIN");
	if (o == 0)
		o = strstr (d, "${ORIGIN}");
	if (o)
	{
		if (origin == 0)
			return 0;
		char t[PATH_MAX];
		size_t skip = o[1] == '{' ? 9 : 7;
		if (snprintf (t, sizeof t, "%.*s%s%s", (int) (o - d), d, origin, o + skip) >= (int) sizeof t)
			return 0;
		strcpy (d, t);
	}
	if (snprintf (out, PATH_MAX, "%s/%s", d, name) >= PATH_MAX)
		return 0;
	return access (out, R_OK) == 0;
}

static int search_list (const char *list, const char *name, const char *origin, char *out)
{
	while (list && *list)
	{
		const char *e = strchr (list, ':');
		/* (un volume, « SD0:/x » : le ':' suivi de '/' fait partie du chemin) */
		while (e && e[1] == '/' && e > list && e[-1] != '/')
		{
			const char *v = e;
			while (v > list && v[-1] != ':')
				v--;
			int vol = 1;
			for (const char *c = v; c < e; c++)
				if (*c == '/')
					vol = 0;
			if (!vol)
				break;
			e = strchr (e + 1, ':');
		}
		size_t n = e ? (size_t) (e - list) : strlen (list);
		if (n && try_path (list, n, name, origin, out))
			return 1;
		list = e ? e + 1 : 0;
	}
	return 0;
}

/* le dossier de m (pour $ORIGIN) */
static void dirname_of (const char *path, char *out)
{
	const char *s = strrchr (path, '/');
	if (s == 0)
		strcpy (out, ".");
	else
	{
		size_t n = (size_t) (s - path);
		memcpy (out, path, n ? n : 1);
		out[n ? n : 1] = 0;
	}
}

static int find_file (const char *name, struct dl_mod *from, char *out)
{
	if (strchr (name, '/') || strchr (name, ':'))
	{
		if (realpath (name, out) == 0)
		{
			strncpy (out, name, PATH_MAX - 1);
			out[PATH_MAX - 1] = 0;
		}
		return access (out, R_OK) == 0;
	}
	char origin[PATH_MAX];
	const char *runpath = 0, *rpath = 0;
	if (from && from != &s_host && from->dyn)
	{
		dirname_of (from->path, origin);
		for (const Elf64_Dyn *d = from->dyn; d->d_tag != DT_NULL; d++)
			if (d->d_tag == DT_RUNPATH)
				runpath = from->str + d->d_un.d_val;
			else if (d->d_tag == DT_RPATH)
				rpath = from->str + d->d_un.d_val;
	}
	if (rpath && !runpath && search_list (rpath, name, origin, out))
		return 1;
	if (search_list (getenv ("LD_LIBRARY_PATH"), name, 0, out))
		return 1;
	if (runpath && search_list (runpath, name, origin, out))
		return 1;
	return search_list (DL_SEARCH_DEFAULT, name, 0, out);
}

/* ---- chargement ---- */
static int read_at (int fd, void *buf, size_t n, off_t off)
{
	size_t got = 0;
	while (got < n)
	{
		ssize_t r = pread (fd, (char *) buf + got, n - got, off + (off_t) got);
		if (r <= 0)
			return -1;
		got += (size_t) r;
	}
	return 0;
}

static int seg_prot (unsigned f)
{
	return ((f & PF_R) ? PROT_READ : 0) | ((f & PF_W) ? PROT_WRITE : 0) | ((f & PF_X) ? PROT_EXEC : 0);
}

/* les droits d'une page : ceux de tous les segments qui la touchent */
static int page_prot (const struct dl_mod *m, unsigned long page)
{
	int p = 0;
	for (unsigned i = 0; i < m->phnum; i++)
		if (m->phdr[i].p_type == PT_LOAD)
		{
			unsigned long a = (m->base + m->phdr[i].p_vaddr) & ~(POSIX_PAGE - 1);
			unsigned long b = m->base + m->phdr[i].p_vaddr + m->phdr[i].p_memsz;
			if (page >= a && page < b)
				p |= seg_prot (m->phdr[i].p_flags);
		}
	return p;
}

static void protect (const struct dl_mod *m)
{
	for (unsigned i = 0; i < m->phnum; i++)
	{
		const Elf64_Phdr *p = &m->phdr[i];
		if (p->p_type != PT_LOAD || p->p_memsz == 0)
			continue;
		unsigned long a = (m->base + p->p_vaddr) & ~(POSIX_PAGE - 1);
		unsigned long b = (m->base + p->p_vaddr + p->p_memsz + POSIX_PAGE - 1) & ~(POSIX_PAGE - 1);
		int own = seg_prot (p->p_flags);
		mprotect ((void *) a, POSIX_PAGE, page_prot (m, a));
		if (b - a > POSIX_PAGE)
		{
			mprotect ((void *) (b - POSIX_PAGE), POSIX_PAGE, page_prot (m, b - POSIX_PAGE));
			if (b - a > 2 * POSIX_PAGE)
				mprotect ((void *) (a + POSIX_PAGE), b - a - 2 * POSIX_PAGE, own);
		}
	}
	for (unsigned i = 0; i < m->phnum; i++)		/* PT_GNU_RELRO : lecture seule, pages entieres */
		if (m->phdr[i].p_type == PT_GNU_RELRO)
		{
			unsigned long a = (m->base + m->phdr[i].p_vaddr + POSIX_PAGE - 1) & ~(POSIX_PAGE - 1);
			unsigned long b = (m->base + m->phdr[i].p_vaddr + m->phdr[i].p_memsz) & ~(POSIX_PAGE - 1);
			if (b > a)
				mprotect ((void *) a, b - a, PROT_READ);
		}
}

static void run_fini (void *arg)
{
	struct dl_mod *m = (struct dl_mod *) arg;
	for (unsigned long i = m->nfini; i > 0; i--)
	{
		void (*f) (void) = m->fini[i - 1];
		if (f != 0 && f != (void (*) (void)) -1)
			f ();
	}
	if (m->fini1)
		m->fini1 ();
}

/* le debut de .eh_frame par .eh_frame_hdr (PT_GNU_EH_FRAME) */
static const void *eh_frame_of (const struct dl_mod *m)
{
	for (unsigned i = 0; i < m->phnum; i++)
		if (m->phdr[i].p_type == PT_GNU_EH_FRAME)
		{
			const unsigned char *h = (const unsigned char *) (m->base + m->phdr[i].p_vaddr);
			if (h[0] != 1)
				return 0;
			const unsigned char *p = h + 4;
			switch (h[1])
			{
			case 0x1b:			/* DW_EH_PE_pcrel | DW_EH_PE_sdata4 */
				return p + *(const int32_t *) p;
			case 0x03:			/* DW_EH_PE_udata4 */
				return (const void *) (m->base + *(const uint32_t *) p);
			case 0x04:			/* DW_EH_PE_udata8 */
				return (const void *) (m->base + *(const uint64_t *) p);
			}
			return 0;
		}
	return 0;
}

static struct dl_mod *load (const char *name, struct dl_mod *from, int depth);

static struct dl_mod *find_loaded (const char *path)
{
	for (struct dl_mod *m = s_mods; m; m = m->next)
		if (m != &s_host && strcmp (m->path, path) == 0)
			return m;
	return 0;
}

static struct dl_mod *map_file (const char *path)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0)
	{
		dl_fail ("%s: ne s'ouvre pas", path);
		return 0;
	}
	Elf64_Ehdr eh;
	struct dl_mod *m = 0;
	Elf64_Phdr *ph = 0;
	if (read_at (fd, &eh, sizeof eh, 0) != 0 || memcmp (eh.e_ident, ELFMAG, SELFMAG) != 0)
	{
		dl_fail ("%s: pas un fichier ELF", path);
		goto out;
	}
	if (eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_machine != EM_AARCH64 || eh.e_type != ET_DYN
	    || eh.e_phentsize != sizeof (Elf64_Phdr) || eh.e_phnum == 0)
	{
		dl_fail ("%s: pas un objet partage ELF64 d'AArch64 (ET_DYN)", path);
		goto out;
	}
	ph = (Elf64_Phdr *) malloc (eh.e_phnum * sizeof *ph);
	m = (struct dl_mod *) calloc (1, sizeof *m);
	if (ph == 0 || m == 0 || read_at (fd, ph, eh.e_phnum * sizeof *ph, (off_t) eh.e_phoff) != 0)
	{
		dl_fail ("%s: en-tetes illisibles", path);
		goto fail;
	}
	m->phdr = ph;
	m->phnum = eh.e_phnum;
	m->path = strdup (path);
	unsigned long lo = ~0UL, hi = 0;
	for (unsigned i = 0; i < m->phnum; i++)
		if (ph[i].p_type == PT_LOAD)
		{
			if (ph[i].p_vaddr < lo)
				lo = ph[i].p_vaddr;
			if (ph[i].p_vaddr + ph[i].p_memsz > hi)
				hi = ph[i].p_vaddr + ph[i].p_memsz;
		}
	if (hi == 0)
	{
		dl_fail ("%s: aucun segment PT_LOAD", path);
		goto fail;
	}
	lo &= ~(POSIX_PAGE - 1);
	hi = (hi + POSIX_PAGE - 1) & ~(POSIX_PAGE - 1);
	void *r = mmap (0, hi - lo, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (r == MAP_FAILED)
	{
		dl_fail ("%s: pas de memoire pour %lu octets", path, hi - lo);
		goto fail;
	}
	m->base = (unsigned long) r - lo;
	m->lo = (unsigned long) r;
	m->hi = (unsigned long) r + (hi - lo);
	for (unsigned i = 0; i < m->phnum; i++)
		if (ph[i].p_type == PT_LOAD && ph[i].p_filesz
		    && read_at (fd, (void *) (m->base + ph[i].p_vaddr), ph[i].p_filesz, (off_t) ph[i].p_offset) != 0)
		{
			dl_fail ("%s: lecture courte", path);
			munmap (r, hi - lo);
			goto fail;
		}
		else if (ph[i].p_type == PT_DYNAMIC)
			m->dyn = (const Elf64_Dyn *) (m->base + ph[i].p_vaddr);
	/* les en-tetes, la ou dl_iterate_phdr les montre : dans l'image si un PT_LOAD les couvre */
	for (unsigned i = 0; i < m->phnum; i++)
		if (ph[i].p_type == PT_LOAD && eh.e_phoff >= ph[i].p_offset
		    && eh.e_phoff + eh.e_phnum * sizeof *ph <= ph[i].p_offset + ph[i].p_filesz)
		{
			m->phdr = (Elf64_Phdr *) (m->base + ph[i].p_vaddr + (eh.e_phoff - ph[i].p_offset));
			free (ph);
			ph = 0;
			break;
		}
	if (m->dyn == 0)
	{
		dl_fail ("%s: pas de PT_DYNAMIC", path);
		munmap (r, hi - lo);
		goto fail;
	}
	read_dynamic (m);
	close (fd);
	return m;
fail:
	if (m)
	{
		free (m->path);
		free (m);
	}
	free (ph);
	m = 0;
out:
	close (fd);
	return 0;
}

static struct dl_mod *load (const char *name, struct dl_mod *from, int depth)
{
	char path[PATH_MAX];
	if (depth > 32)
	{
		dl_fail ("%s: dependances trop profondes", name);
		return 0;
	}
	if (!find_file (name, from, path))
	{
		dl_fail ("%s: introuvable", name);
		return 0;
	}
	struct dl_mod *m = find_loaded (path);
	if (m)
	{
		if (!m->ready)
		{
			dl_fail ("%s: dependance circulaire", path);
			return 0;
		}
		m->refs++;
		return m;
	}
	m = map_file (path);
	if (m == 0)
		return 0;
	*s_tail = m;					/* (visible tout de suite : les dependances circulaires) */
	s_tail = &m->next;
	m->refs = 1;

	const Elf64_Rela *rela = 0, *jmprel = 0;
	unsigned long relasz = 0, jmpsz = 0, initsz = 0;
	void (**init) (void) = 0;
	void (*init1) (void) = 0;
	int nneed = 0;
	for (const Elf64_Dyn *d = m->dyn; d->d_tag != DT_NULL; d++)
		switch (d->d_tag)
		{
		case DT_NEEDED: nneed++; break;
		case DT_RELA: rela = (const Elf64_Rela *) (m->base + d->d_un.d_ptr); break;
		case DT_RELASZ: relasz = d->d_un.d_val; break;
		case DT_JMPREL: jmprel = (const Elf64_Rela *) (m->base + d->d_un.d_ptr); break;
		case DT_PLTRELSZ: jmpsz = d->d_un.d_val; break;
		case DT_INIT: init1 = (void (*) (void)) (m->base + d->d_un.d_ptr); break;
		case DT_INIT_ARRAY: init = (void (**) (void)) (m->base + d->d_un.d_ptr); break;
		case DT_INIT_ARRAYSZ: initsz = d->d_un.d_val / sizeof (void *); break;
		case DT_FINI: m->fini1 = (void (*) (void)) (m->base + d->d_un.d_ptr); break;
		case DT_FINI_ARRAY: m->fini = (void (**) (void)) (m->base + d->d_un.d_ptr); break;
		case DT_FINI_ARRAYSZ: m->nfini = d->d_un.d_val / sizeof (void *); break;
		case DT_REL: case DT_TEXTREL:
			dl_fail ("%s: relocations REL ou de texte (recompiler avec -fPIC)", m->path);
			goto bad;
		}
	if (nneed > DL_MAX_DEPS)
	{
		dl_fail ("%s: plus de %d DT_NEEDED", m->path, DL_MAX_DEPS);
		goto bad;
	}
	m->deps = (struct dl_mod **) calloc ((size_t) nneed + 1, sizeof *m->deps);
	for (const Elf64_Dyn *d = m->dyn; d->d_tag != DT_NULL; d++)
		if (d->d_tag == DT_NEEDED)
		{
			struct dl_mod *dep = load (m->str + d->d_un.d_val, m, depth + 1);
			if (dep == 0)
			{
				char why[sizeof t_err];
				snprintf (why, sizeof why, "%s", t_err);
				dl_fail ("%s: %.200s", m->path, why);
				goto bad;
			}
			m->deps[m->ndeps++] = dep;
		}
	for (unsigned i = 0; i < m->phnum; i++)
		if (m->phdr[i].p_type == PT_TLS && m->phdr[i].p_memsz && tls_place (m, &m->phdr[i]) != 0)
			goto bad;
	if ((rela && relocate (m, rela, relasz) != 0) || (jmprel && relocate (m, jmprel, jmpsz) != 0))
		goto bad;
	if (m->tls_mem)
	{
		tls_install (m);
		if (rela)
			fix_dtpmod (m, rela, relasz);
		if (jmprel)
			fix_dtpmod (m, jmprel, jmpsz);
	}
	protect (m);
	const void *eh = eh_frame_of (m);
	if (eh && __register_frame_info)
		__register_frame_info (eh, m->eh_obj);
	s_adds++;
	__cxa_atexit (run_fini, m, m);			/* (avant ses constructeurs : apres leurs atexit) */
	m->ready = 1;
	if (init1)
		init1 ();
	for (unsigned long i = 0; i < initsz; i++)
		if (init[i] != 0 && init[i] != (void (*) (void)) -1)
			init[i] ();
	return m;
bad:
	/* (la region reste projetee : un module a moitie lie n'est jamais rendu, il est oublie) */
	for (struct dl_mod **pp = &s_mods; *pp; pp = &(*pp)->next)
		if (*pp == m)
		{
			*pp = m->next;
			if (s_tail == &m->next)
				s_tail = pp;
			break;
		}
	m->path[0] = 0;
	return 0;
}

/* ---- l'interface ---- */
void *dlopen (const char *file, int mode)
{
	dl_lock ();
	int dyn = host_init ();
	void *h = 0;
	if (file == 0)
		h = &s_host;
	else if (!dyn)
		dl_fail ("dlopen %s: le programme n'est pas un hote de modules (le relier avec gcc -rdynamic, "
			 "docs/POSIX.md)", file);
	else if (mode & RTLD_NOLOAD)
	{
		char path[PATH_MAX];
		struct dl_mod *m = find_file (file, 0, path) ? find_loaded (path) : 0;
		if (m)
		{
			m->refs++;
			if (mode & RTLD_GLOBAL)
				m->global = 1;
		}
		h = m;
	}
	else
	{
		struct dl_mod *m = load (file, 0, 0);
		if (m && (mode & RTLD_GLOBAL))
			m->global = 1;
		h = m;
	}
	dl_unlock ();
	return h;
}

static int valid (struct dl_mod *h)
{
	for (struct dl_mod *m = s_mods; m; m = m->next)
		if (m == h)
			return 1;
	return 0;
}

static void *sym_addr (struct dl_mod *def, const Elf64_Sym *s)
{
	if (ELF64_ST_TYPE (s->st_info) == STT_TLS)
	{
		unsigned long tp;
		__asm__ volatile ("mrs %0, tpidr_el0" : "=r" (tp));
		return (void *) (tp + def->tls_off + s->st_value);
	}
	if (ELF64_ST_TYPE (s->st_info) == STT_GNU_IFUNC)
		return (void *) ((unsigned long (*) (void)) (def->base + s->st_value)) ();
	return (void *) (def->base + s->st_value);
}

static struct dl_mod *mod_of (const void *a)
{
	unsigned long x = (unsigned long) a;
	for (struct dl_mod *m = s_mods; m; m = m->next)
		if (x >= m->lo && x < m->hi)
			return m;
	return 0;
}

void *dlsym (void *handle, const char *name)
{
	dl_lock ();
	host_init ();
	void *r = 0;
	struct dl_mod *def = 0;
	const Elf64_Sym *s = 0;
	if (handle == RTLD_DEFAULT || handle == (void *) &s_host)
		s = lookup (0, name, &def);
	else if (handle == RTLD_NEXT)
	{
		/* apres l'objet de l'appelant, dans l'ordre de chargement */
		struct dl_mod *me = mod_of (__builtin_return_address (0));
		uint32_t hs = sysv_hash (name), hg = gnu_hash (name);
		for (struct dl_mod *m = me ? me->next : 0; m && s == 0; m = m->next)
			if ((s = mod_lookup (m, name, hs, hg)) != 0)
				def = m;
	}
	else if (valid ((struct dl_mod *) handle))
	{
		/* le module et ses dependances (la portee du module, sans l'hote) */
		s = group_lookup ((struct dl_mod *) handle, name, sysv_hash (name), gnu_hash (name), &def);
	}
	else
	{
		dl_fail ("dlsym: poignee invalide");
		dl_unlock ();
		return 0;
	}
	if (s)
		r = sym_addr (def, s);
	else
		dl_fail ("dlsym: %s introuvable", name);
	dl_unlock ();
	return r;
}

int dlclose (void *handle)
{
	dl_lock ();
	int r = 0;
	struct dl_mod *m = (struct dl_mod *) handle;
	if (m == &s_host)
		;
	else if (!valid (m) || m->refs <= 0)
	{
		dl_fail ("dlclose: poignee invalide");
		r = -1;
	}
	else
		m->refs--;			/* (jamais decharge : ses pointeurs restent valables) */
	dl_unlock ();
	return r;
}

int dladdr (const void *a, Dl_info *info)
{
	memset (info, 0, sizeof *info);
	dl_lock ();
	host_init ();
	struct dl_mod *m = mod_of (a);
	if (m == 0)
	{
		dl_unlock ();
		return 0;
	}
	info->dli_fname = m->path;
	info->dli_fbase = (void *) m->lo;
	/* le symbole qui contient l'adresse, sinon le plus proche en dessous */
	unsigned long x = (unsigned long) a, best = 0;
	const Elf64_Sym *bs = 0;
	int inside = 0;
	for (unsigned long i = 1; i < m->nsym; i++)
	{
		const Elf64_Sym *s = &m->sym[i];
		int t = ELF64_ST_TYPE (s->st_info);
		if (s->st_shndx == SHN_UNDEF || (t != STT_FUNC && t != STT_OBJECT && t != STT_NOTYPE))
			continue;
		unsigned long v = m->base + s->st_value;
		if (v > x)
			continue;
		int in = x < v + s->st_size;
		if ((in && !inside) || (in == inside && v > best) || bs == 0)
		{
			best = v;
			bs = s;
			inside = in;
		}
	}
	if (bs)
	{
		info->dli_sname = m->str + bs->st_name;
		info->dli_saddr = (void *) best;
	}
	dl_unlock ();
	return 1;
}

int dl_iterate_phdr (int (*fn) (struct dl_phdr_info *, size_t, void *), void *data)
{
	dl_lock ();
	host_init ();
	int r = 0;
	unsigned long tp;
	__asm__ volatile ("mrs %0, tpidr_el0" : "=r" (tp));
	for (struct dl_mod *m = s_mods; m && r == 0; m = m->next)
	{
		if (m->phdr == 0 || (m != &s_host && !m->ready))
			continue;
		struct dl_phdr_info i;
		memset (&i, 0, sizeof i);
		i.dlpi_addr = m->base;
		i.dlpi_name = m == &s_host ? "" : m->path;
		i.dlpi_phdr = m->phdr;
		i.dlpi_phnum = (Elf64_Half) m->phnum;
		i.dlpi_adds = s_adds;
		i.dlpi_tls_modid = m->tls_modid;
		i.dlpi_tls_data = m->tls_modid && tp ? (void *) (tp + m->tls_off) : 0;
		r = fn (&i, sizeof i, data);
	}
	dl_unlock ();
	return r;
}
