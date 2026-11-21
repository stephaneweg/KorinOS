/*
 * elf.c -- liblinux : le chargeur des .so de Linux x86-64 (linux.h, docs/etude/WIDEVINE.md § 4.2).
 *
 * Ce que fait ld-linux-x86-64.so.2 pour une seule bibliothèque, sans ld-linux : les segments
 * PT_LOAD dans une réservation d'un seul tenant (les écarts entre segments gardés), les relocations
 * RELA (RELATIVE, 64, GLOB_DAT, JUMP_SLOT, IRELATIVE) liées tout de suite (pas de liaison
 * paresseuse), les imports pris dans la glibc de compatibilité (glibc.c) plutôt que dans
 * libc.so.6, puis les protections des segments et les constructeurs. Les versions des symboles
 * (memcpy@GLIBC_2.14) sont ignorées : un nom, une fonction.
 *
 * Depuis le 21/11 (docs/etude/LINUX-HOST.md, jalon 1) : aussi un exécutable (ET_EXEC à ses adresses, par la
 * zone basse de VM_FIXED du noyau, ou PIE), lu par morceaux dans son fichier (linux_load_file), avec ses
 * relocations COPY ; son TLS « local exec » est posé par exec.c, son __libc_start_main est ici.
 *
 * Pas encore : TLS « initial exec » d'un module (TPOFF64 : refusé, avec leur nombre), plusieurs
 * bibliothèques liées entre elles (DT_NEEDED autre que la glibc : seulement nommées), dlopen depuis
 * le module.
 */
#define _GNU_SOURCE	/* (dl_iterate_phdr, pthread_setname_np) */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "linux.h"

/* ---- ELF64 (les seuls champs servis) ---- */
typedef struct
{
	unsigned char e_ident[16];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint64_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} lx_ehdr;

typedef struct
{
	uint32_t p_type, p_flags;
	uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} lx_phdr;

typedef struct
{
	uint32_t st_name;
	unsigned char st_info, st_other;
	uint16_t st_shndx;
	uint64_t st_value, st_size;
} lx_sym;

typedef struct
{
	uint64_t r_offset, r_info;
	int64_t r_addend;
} lx_rela;

typedef struct
{
	int64_t d_tag;
	uint64_t d_val;
} lx_dyn;

#define ET_EXEC		2
#define ET_DYN		3
#define EM_X86_64	62
#define PT_LOAD		1
#define PT_DYNAMIC	2
#define PT_INTERP	3
#define PT_PHDR		6
#define PT_TLS		7
#define PT_GNU_STACK	0x6474e551
#define PT_GNU_EH_FRAME	0x6474e550
#define PT_GNU_RELRO	0x6474e552
#define PF_X		1
#define PF_W		2
#define PF_R		4

#define DT_NULL		0
#define DT_NEEDED	1
#define DT_PLTRELSZ	2
#define DT_HASH		4
#define DT_STRTAB	5
#define DT_SYMTAB	6
#define DT_RELA		7
#define DT_RELASZ	8
#define DT_INIT		12
#define DT_FINI		13
#define DT_SONAME	14
#define DT_JMPREL	23
#define DT_INIT_ARRAY	25
#define DT_FINI_ARRAY	26
#define DT_INIT_ARRAYSZ	27
#define DT_FINI_ARRAYSZ	28
#define DT_PREINIT_ARRAY	32
#define DT_PREINIT_ARRAYSZ	33
#define DT_GNU_HASH	0x6ffffef5

#define STB_LOCAL	0
#define STB_WEAK	2
#define SHN_UNDEF	0

#define R_X86_64_NONE		0
#define R_X86_64_64		1
#define R_X86_64_COPY		5
#define R_X86_64_GLOB_DAT	6
#define R_X86_64_JUMP_SLOT	7
#define R_X86_64_RELATIVE	8
#define R_X86_64_DTPMOD64	16
#define R_X86_64_DTPOFF64	17
#define R_X86_64_TPOFF64	18
#define R_X86_64_IRELATIVE	37

#define PAGE		4096UL
#define PAGE_DOWN(x)	((x) & ~(PAGE - 1))
#define PAGE_UP(x)	(((x) + PAGE - 1) & ~(PAGE - 1))

/* __cxa_atexit du module : glibc.c les range ici quand le dso_handle est celui d'un module */
struct linux_atexit
{
	void (*fn) (void *);
	void *arg;
	struct linux_atexit *next;
};

struct linux_module
{
	char name[256];			/* le chemin donné à linux_load (celui de /proc/self/maps) */
	lx_phdr *ph;			/* copie des en-têtes de programme (/proc/self/maps) */
	int phnum;
	int tls_id;			/* 0 : pas de TLS ; sinon le numéro de module de __tls_get_addr */
	void *eh_frame;			/* .eh_frame déclaré au dérouleur de libgcc (exceptions C++) */
	unsigned char *base;		/* adresse de chargement (vaddr 0 du fichier) */
	unsigned char *area;		/* la réservation : base + lo (première page des PT_LOAD) */
	size_t span;			/* octets réservés depuis area */
	const lx_dyn *dyn;
	const char *strtab;
	const lx_sym *symtab;
	unsigned long nsyms;
	uint64_t init, fini, init_array, init_arraysz, fini_array, fini_arraysz;
	uint64_t preinit_array, preinit_arraysz;
	int exe;			/* un exécutable (LINUX_LOAD_EXE) : son TLS est statique (exec.c) */
	uint64_t entry, phdr;		/* (exécutable) e_entry, adresse des en-têtes en mémoire */
	uint64_t stack_size;		/* (exécutable) PT_GNU_STACK, 0 : rien demandé */
	const void *tls_init;		/* (exécutable) PT_TLS : image, tailles, alignement */
	uint64_t tls_filesz, tls_memsz, tls_align, tls_span;
	unsigned char *stubs;		/* les fonctions des imports absents (LINUX_LOAD_STUBS) */
	size_t stubs_size;
	struct linux_atexit *atexit;
};

static void linux_register (struct linux_module *m);
void linux_module_finalize (void *dso);
/* ce qu'une image dit d'elle-même, avant tout chargement */
struct lx_image
{
	const unsigned char *file;
	size_t size;
	const lx_ehdr *eh;
	const lx_phdr *ph;
	uint64_t lo, hi;		/* étendue des PT_LOAD, en vaddr arrondies aux pages */
	const lx_phdr *dynph, *tlsph;
	uint64_t phdr;			/* PT_PHDR (0 : pas de segment) */
	uint64_t stack_size;		/* PT_GNU_STACK */
	int interp;			/* PT_INTERP : un exécutable dynamique (ET_EXEC ou PIE) */
};

static void set_err (char *err, size_t n, const char *fmt, ...)
{
	if (!err || !n)
		return;
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (err, n, fmt, ap);
	va_end (ap);
}

/* image : les avail premiers octets d'un fichier de size octets (tout le fichier, ou ses en-têtes :
   linux_load_file) */
static int parse_image_part (struct lx_image *im, const void *image, size_t avail, size_t size, char *err, size_t errlen)
{
	memset (im, 0, sizeof *im);
	im->file = image;
	im->size = size;
	if (avail < sizeof (lx_ehdr) || memcmp (image, "\177ELF", 4))
		return set_err (err, errlen, "pas un fichier ELF"), -1;
	im->eh = image;
	if (im->eh->e_ident[4] != 2 || im->eh->e_ident[5] != 1)
		return set_err (err, errlen, "ELF 64 bits petit-boutiste attendu"), -1;
	if (im->eh->e_machine != EM_X86_64)
		return set_err (err, errlen, "machine %u : x86-64 (62) attendu", im->eh->e_machine), -1;
	if (im->eh->e_type != ET_DYN && im->eh->e_type != ET_EXEC)
		return set_err (err, errlen, "type %u : une bibliothèque partagée (ET_DYN) ou un exécutable attendu",
				im->eh->e_type), -1;
	if (im->eh->e_phentsize != sizeof (lx_phdr) || im->eh->e_phoff + (uint64_t) im->eh->e_phnum * sizeof (lx_phdr) > avail)
		return set_err (err, errlen, "en-têtes de programme hors du fichier"), -1;
	im->ph = (const lx_phdr *) (im->file + im->eh->e_phoff);
	im->lo = UINT64_MAX;
	for (int i = 0; i < im->eh->e_phnum; i++)
	{
		const lx_phdr *p = &im->ph[i];
		if (p->p_type == PT_DYNAMIC)
			im->dynph = p;
		else if (p->p_type == PT_TLS)
			im->tlsph = p;
		else if (p->p_type == PT_PHDR)
			im->phdr = p->p_vaddr;
		else if (p->p_type == PT_GNU_STACK)
			im->stack_size = p->p_memsz;
		else if (p->p_type == PT_INTERP)
			im->interp = 1;
		if (p->p_type != PT_LOAD)
			continue;
		if (p->p_offset + p->p_filesz > size || p->p_filesz > p->p_memsz)
			return set_err (err, errlen, "segment %d hors du fichier", i), -1;
		if (PAGE_DOWN (p->p_vaddr) < im->lo)
			im->lo = PAGE_DOWN (p->p_vaddr);
		if (PAGE_UP (p->p_vaddr + p->p_memsz) > im->hi)
			im->hi = PAGE_UP (p->p_vaddr + p->p_memsz);
	}
	if (im->lo == UINT64_MAX)
		return set_err (err, errlen, "aucun segment PT_LOAD"), -1;
	if (!im->dynph)
		return set_err (err, errlen, "pas de segment PT_DYNAMIC (exécutable statique : pas servi)"), -1;
	return 0;
}

static int parse_image (struct lx_image *im, const void *image, size_t size, char *err, size_t errlen)
{
	return parse_image_part (im, image, size, size, err, errlen);
}

/* vaddr du fichier -> pointeur dans le fichier lui-même (pour linux_check, sans chargement) */
static const void *file_ptr (const struct lx_image *im, uint64_t vaddr)
{
	for (int i = 0; i < im->eh->e_phnum; i++)
	{
		const lx_phdr *p = &im->ph[i];
		if (p->p_type == PT_LOAD && vaddr >= p->p_vaddr && vaddr < p->p_vaddr + p->p_filesz)
			return im->file + p->p_offset + (vaddr - p->p_vaddr);
	}
	return 0;
}

/* les tables dynamiques, par une fonction qui traduit une vaddr en pointeur */
struct lx_dynamic
{
	const char *strtab;
	const lx_sym *symtab;
	const uint32_t *hash, *gnu_hash;
	const lx_rela *rela, *jmprel;
	uint64_t relasz, pltrelsz;
	uint64_t init, fini, init_array, init_arraysz, fini_array, fini_arraysz;
	uint64_t preinit_array, preinit_arraysz;
	uint64_t soname;
	unsigned long nsyms;
};

typedef const void *(*lx_xlate) (void *ctx, uint64_t vaddr);

static unsigned long count_syms (const uint32_t *hash, const uint32_t *gnu)
{
	if (hash)
		return hash[1];		/* nchain */
	if (!gnu)
		return 0;
	/* DT_GNU_HASH : le plus grand indice des chaînes, plus un */
	uint32_t nbuckets = gnu[0], symoffset = gnu[1], bloom_size = gnu[2];
	const uint32_t *buckets = gnu + 4 + bloom_size * 2;	/* (bloom de mots de 64 bits) */
	const uint32_t *chain = buckets + nbuckets;
	uint32_t last = 0;
	for (uint32_t i = 0; i < nbuckets; i++)
		if (buckets[i] > last)
			last = buckets[i];
	if (last < symoffset)
		return symoffset;
	while (!(chain[last - symoffset] & 1))
		last++;
	return last + 1;
}

static void read_dynamic (struct lx_dynamic *d, const lx_dyn *dyn, lx_xlate x, void *ctx)
{
	memset (d, 0, sizeof *d);
	for (; dyn->d_tag != DT_NULL; dyn++)
	{
		uint64_t v = dyn->d_val;
		switch (dyn->d_tag)
		{
		case DT_STRTAB: d->strtab = x (ctx, v); break;
		case DT_SYMTAB: d->symtab = x (ctx, v); break;
		case DT_HASH: d->hash = x (ctx, v); break;
		case DT_GNU_HASH: d->gnu_hash = x (ctx, v); break;
		case DT_RELA: d->rela = x (ctx, v); break;
		case DT_RELASZ: d->relasz = v; break;
		case DT_JMPREL: d->jmprel = x (ctx, v); break;
		case DT_PLTRELSZ: d->pltrelsz = v; break;
		case DT_INIT: d->init = v; break;
		case DT_FINI: d->fini = v; break;
		case DT_INIT_ARRAY: d->init_array = v; break;
		case DT_INIT_ARRAYSZ: d->init_arraysz = v; break;
		case DT_FINI_ARRAY: d->fini_array = v; break;
		case DT_FINI_ARRAYSZ: d->fini_arraysz = v; break;
		case DT_PREINIT_ARRAY: d->preinit_array = v; break;
		case DT_PREINIT_ARRAYSZ: d->preinit_arraysz = v; break;
		case DT_SONAME: d->soname = v + 1; break;	/* (+1 : 0 = absent) */
		}
	}
	d->nsyms = count_syms (d->hash, d->gnu_hash);
}

static const void *xlate_file (void *ctx, uint64_t vaddr)
{
	return file_ptr (ctx, vaddr);
}

static const void *xlate_loaded (void *ctx, uint64_t vaddr)
{
	return (unsigned char *) ctx + vaddr;
}

/* ---- les imports absents : une petite fonction par nom, qui appelle linux_missing (nom) ---- */
/*   48 bf <nom:8>      mov rdi, nom
 *   48 b8 <fn:8>       mov rax, linux_missing
 *   ff e0              jmp rax                                                              */
#define STUB_SIZE	24

static void *make_stub (struct linux_module *m, unsigned idx, const char *name)
{
	unsigned char *s = m->stubs + (size_t) idx * STUB_SIZE;
	uint64_t n = (uint64_t) (uintptr_t) name, f = (uint64_t) (uintptr_t) linux_missing;
	s[0] = 0x48; s[1] = 0xbf;
	memcpy (s + 2, &n, 8);
	s[10] = 0x48; s[11] = 0xb8;
	memcpy (s + 12, &f, 8);
	s[20] = 0xff; s[21] = 0xe0;
	s[22] = 0xcc; s[23] = 0xcc;
	return s;
}

/* ---- relocations ---- */
struct reloc_state
{
	struct linux_module *m;
	struct lx_dynamic *d;
	int flags;
	unsigned nstubs;
	unsigned missing, tls;
	char *err;
	size_t errlen;
};

static int resolve (struct reloc_state *st, uint32_t symi, uint64_t *out)
{
	const lx_sym *s = &st->d->symtab[symi];
	const char *name = st->d->strtab + s->st_name;
	if (s->st_shndx != SHN_UNDEF)
	{
		*out = (uint64_t) (uintptr_t) (st->m->base + s->st_value);
		return 0;
	}
	void *p = linux_glibc_lookup (name);
	if (p)
	{
		*out = (uint64_t) (uintptr_t) p;
		return 0;
	}
	if ((s->st_info >> 4) == STB_WEAK)
	{
		*out = 0;
		return 0;
	}
	if ((st->flags & LINUX_LOAD_STUBS) && st->m->stubs && st->nstubs * STUB_SIZE < st->m->stubs_size)
	{
		fprintf (stderr, "liblinux : %s : import absent, remplacé par un piège : %s\n", st->m->name, name);
		*out = (uint64_t) (uintptr_t) make_stub (st->m, st->nstubs++, name);
		return 0;
	}
	if (!st->missing++)
		set_err (st->err, st->errlen, "import absent : %s", name);
	return -1;
}

static int apply (struct reloc_state *st, const lx_rela *r, uint64_t bytes)
{
	unsigned char *base = st->m->base;
	for (uint64_t i = 0; i < bytes / sizeof (lx_rela); i++)
	{
		uint32_t type = (uint32_t) r[i].r_info, symi = (uint32_t) (r[i].r_info >> 32);
		uint64_t *where = (uint64_t *) (base + r[i].r_offset), s = 0;
		if ((unsigned char *) where < st->m->area || (unsigned char *) where + 8 > st->m->area + st->m->span)
			return set_err (st->err, st->errlen, "relocation hors du module"), -1;
		switch (type)
		{
		case R_X86_64_NONE:
			break;
		case R_X86_64_RELATIVE:
			*where = (uint64_t) (uintptr_t) base + r[i].r_addend;
			break;
		case R_X86_64_64:
		case R_X86_64_GLOB_DAT:
		case R_X86_64_JUMP_SLOT:
			if (resolve (st, symi, &s))
				break;
			*where = s + (type == R_X86_64_64 ? r[i].r_addend : 0);
			break;
		case R_X86_64_IRELATIVE:
		{
			uint64_t (*resolver) (void) = (uint64_t (*) (void)) (base + r[i].r_addend);
			*where = resolver ();
			break;
		}
		case R_X86_64_DTPMOD64:
			/* (le module lui-même : ses variables de thread ; un symbole d'un autre module : pas servi) */
			if (symi && st->d->symtab[symi].st_shndx == SHN_UNDEF)
			{
				st->tls++;
				break;
			}
			*where = (uint64_t) st->m->tls_id;
			break;
		case R_X86_64_DTPOFF64:
			*where = (symi ? st->d->symtab[symi].st_value : 0) + r[i].r_addend;
			break;
		case R_X86_64_TPOFF64:
			/* « initial exec » : un décalage fixe sous %fs ; servi pour les variables d'un exécutable
			   (son bloc finit à %fs : exec.c), pas pour un module */
			if (st->m->exe && st->m->tls_span && (!symi || st->d->symtab[symi].st_shndx != SHN_UNDEF))
			{
				*where = (symi ? st->d->symtab[symi].st_value : 0) + (uint64_t) r[i].r_addend - st->m->tls_span;
				break;
			}
			st->tls++;
			break;
		case R_X86_64_COPY:
		{
			/* une donnée de la libc recopiée dans l'exécutable (stdout, environ…) : sa valeur, prise dans
			   la glibc de compatibilité (le symbole de l'exécutable est la copie elle-même) */
			const lx_sym *sy = &st->d->symtab[symi];
			const char *name = st->d->strtab + sy->st_name;
			const void *src = linux_glibc_lookup (name);
			if ((unsigned char *) where + sy->st_size > st->m->area + st->m->span)
				return set_err (st->err, st->errlen, "relocation hors du module"), -1;
			if (src)
				memcpy (where, src, sy->st_size);
			else if (!strstr (name, "environ"))	/* (environ : posé par __libc_start_main) */
			{
				if (st->flags & LINUX_LOAD_STUBS)
					fprintf (stderr, "liblinux : %s : donnée absente, mise à zéro : %s\n", st->m->name, name);
				else if (!st->missing++)
					set_err (st->err, st->errlen, "donnée absente : %s", name);
			}
			break;
		}
		default:
			return set_err (st->err, st->errlen, "relocation de type %u non servie", type), -1;
		}
	}
	return 0;
}

static int count_imports (const struct lx_dynamic *d, const lx_rela *r, uint64_t bytes)
{
	int n = 0;
	for (uint64_t i = 0; i < bytes / sizeof (lx_rela); i++)
		if ((uint32_t) (r[i].r_info >> 32) && d->symtab[r[i].r_info >> 32].st_shndx == SHN_UNDEF)
			n++;
	return n;
}

static int prot_of (uint32_t f)
{
	return ((f & PF_R) ? PROT_READ : 0) | ((f & PF_W) ? PROT_WRITE : 0) | ((f & PF_X) ? PROT_EXEC : 0);
}

/* ---- variables de thread des modules (PT_TLS, modèle « general dynamic » : __tls_get_addr) ----
   Un bloc par thread et par module, fait à la première demande depuis l'image du module (.tdata, puis des
   zéros) ; le numéro de module (DTPMOD64) indexe s_tls. (Les blocs d'un thread fini ne sont pas rendus.) */
#define MAX_TLS 16
static struct
{
	const unsigned char *init;
	size_t filesz, memsz, align;
} s_tls[MAX_TLS + 1];
static __thread void *s_tls_block[MAX_TLS + 1];

static int tls_add (const void *init, size_t filesz, size_t memsz, size_t align)
{
	for (int i = 1; i <= MAX_TLS; i++)
		if (!s_tls[i].memsz)
		{
			s_tls[i].init = init;
			s_tls[i].filesz = filesz;
			s_tls[i].align = align < 16 ? 16 : align;
			__atomic_store_n (&s_tls[i].memsz, memsz, __ATOMIC_RELEASE);
			return i;
		}
	return 0;
}

static void tls_remove (int id)
{
	if (id > 0 && id <= MAX_TLS)
		__atomic_store_n (&s_tls[id].memsz, 0, __ATOMIC_RELEASE);
}

/* l'argument de __tls_get_addr : {numéro de module, décalage} */
void *linux_tls_get_addr (const uint64_t *ti)
{
	uint64_t id = ti[0];
	if (id < 1 || id > MAX_TLS || !s_tls[id].memsz)
	{
		fprintf (stderr, "liblinux : __tls_get_addr d'un module inconnu (%llu)\n", (unsigned long long) id);
		abort ();
	}
	void *b = s_tls_block[id];
	if (!b)
	{
		size_t a = s_tls[id].align, n = (s_tls[id].memsz + a - 1) & ~(a - 1);
		if (posix_memalign (&b, a, n))
		{
			fprintf (stderr, "liblinux : TLS d'un module : mémoire\n");
			abort ();
		}
		memcpy (b, s_tls[id].init, s_tls[id].filesz);
		memset ((char *) b + s_tls[id].filesz, 0, n - s_tls[id].filesz);
		s_tls_block[id] = b;
	}
	return (char *) b + ti[1];
}

/* ---- exceptions C++ : le .eh_frame du module (par PT_GNU_EH_FRAME) déclaré au dérouleur de libgcc ---- */
extern void __register_frame (void *);
extern void __deregister_frame (void *);

static void *eh_frame_of (const unsigned char *hdr)
{
	/* .eh_frame_hdr : version 1, codage du pointeur, …, puis le pointeur vers .eh_frame */
	if (hdr[0] != 1)
		return 0;
	unsigned enc = hdr[1];
	const unsigned char *p = hdr + 4;
	int64_t v;
	switch (enc & 0x0f)
	{
	case 0x0b: v = *(const int32_t *) p; break;	/* sdata4 */
	case 0x03: v = *(const uint32_t *) p; break;	/* udata4 */
	case 0x04: case 0x0c: v = *(const int64_t *) p; break;	/* udata8, sdata8 */
	default: return 0;
	}
	switch (enc & 0x70)
	{
	case 0x00: return (void *) (uintptr_t) v;		/* absolu */
	case 0x10: return (void *) (p + v);			/* pcrel */
	case 0x30: return (void *) (hdr + v);			/* datarel */
	}
	return 0;
}

/* le chargement : im décrit l'image ; ses segments sont copiés de im.file (fd < 0) ou lus dans fd */
static struct linux_module *load_common (struct lx_image *im_, int fd, const char *name, int flags,
					 char *err, size_t errlen)
{
	struct lx_image im = *im_;
	int exe = (flags & LINUX_LOAD_EXE) != 0;
	if (im.eh->e_type == ET_EXEC && !exe)
		return set_err (err, errlen, "un exécutable (ET_EXEC) : linuxhost run"), (struct linux_module *) 0;
	struct linux_module *m = calloc (1, sizeof *m);
	if (!m)
		return set_err (err, errlen, "mémoire"), (struct linux_module *) 0;
	snprintf (m->name, sizeof m->name, "%s", name ? name : "?");
	m->span = im.hi - im.lo;
	m->exe = exe;
	/* une réservation d'un seul tenant, écrite d'abord, protégée ensuite ; un ET_EXEC à ses adresses
	   (la zone basse de VM_FIXED du noyau du PC : sous l'hôte, lié à 0x4000_0000) */
	unsigned char *area = im.eh->e_type == ET_EXEC
		? mmap ((void *) (uintptr_t) im.lo, m->span, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0)
		: mmap (0, m->span, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area == MAP_FAILED)
	{
		if (im.eh->e_type == ET_EXEC)
			set_err (err, errlen, "mmap de [%#llx, %#llx) à cette adresse : %s (noyau sans la zone basse de"
				 " VM_FIXED, ou hôte lié trop bas)", (unsigned long long) im.lo, (unsigned long long) im.hi,
				 strerror (errno));
		else
			set_err (err, errlen, "mmap de %zu octets : %s", m->span, strerror (errno));
		free (m);
		return 0;
	}
	m->area = area;
	m->base = im.eh->e_type == ET_EXEC ? (unsigned char *) 0 : area - im.lo;
	for (int i = 0; i < im.eh->e_phnum; i++)
	{
		const lx_phdr *p = &im.ph[i];
		if (p->p_type != PT_LOAD)
			continue;
		if (fd < 0)
		{
			memcpy (m->base + p->p_vaddr, im.file + p->p_offset, p->p_filesz);
			continue;
		}
		for (uint64_t done = 0; done < p->p_filesz;)
		{
			uint64_t want = p->p_filesz - done;
			ssize_t k = pread (fd, m->base + p->p_vaddr + done, want > (64u << 20) ? (64u << 20) : want,
					   (off_t) (p->p_offset + done));
			if (k <= 0)
			{
				set_err (err, errlen, "lecture du segment %d : %s", i, k < 0 ? strerror (errno) : "fin du fichier");
				munmap (area, m->span);
				free (m);
				return 0;
			}
			done += (uint64_t) k;
		}
	}
	if (exe)
	{
		m->entry = (uint64_t) (uintptr_t) m->base + im.eh->e_entry;
		/* les en-têtes en mémoire : PT_PHDR, sinon dans le premier segment s'il couvre le début du fichier */
		m->phdr = im.phdr ? (uint64_t) (uintptr_t) m->base + im.phdr : 0;
		for (int i = 0; !m->phdr && i < im.eh->e_phnum; i++)
			if (im.ph[i].p_type == PT_LOAD && im.ph[i].p_offset == 0)
				m->phdr = (uint64_t) (uintptr_t) m->base + im.ph[i].p_vaddr + im.eh->e_phoff;
		m->stack_size = im.stack_size;
		if (im.tlsph && im.tlsph->p_memsz)
		{
			uint64_t a = im.tlsph->p_align ? im.tlsph->p_align : 1;
			m->tls_init = m->base + im.tlsph->p_vaddr;
			m->tls_filesz = im.tlsph->p_filesz;
			m->tls_memsz = im.tlsph->p_memsz;
			m->tls_align = a;
			m->tls_span = (im.tlsph->p_memsz + a - 1) & ~(a - 1);	/* (variante II : le bloc finit à %fs) */
		}
	}
	else if (im.tlsph && im.tlsph->p_memsz)
	{
		m->tls_id = tls_add (m->base + im.tlsph->p_vaddr, im.tlsph->p_filesz, im.tlsph->p_memsz, im.tlsph->p_align);
		if (!m->tls_id)
		{
			set_err (err, errlen, "trop de modules avec des variables de thread");
			goto fail;
		}
	}
	m->phnum = im.eh->e_phnum;
	m->ph = malloc (sizeof (lx_phdr) * (size_t) m->phnum);
	if (!m->ph)
	{
		set_err (err, errlen, "mémoire");
		goto fail;
	}
	memcpy (m->ph, im.ph, sizeof (lx_phdr) * (size_t) m->phnum);
	m->dyn = (const lx_dyn *) (m->base + im.dynph->p_vaddr);
	struct lx_dynamic d;
	read_dynamic (&d, m->dyn, xlate_loaded, m->base);
	if (!d.strtab || !d.symtab)
	{
		set_err (err, errlen, "tables dynamiques incomplètes");
		goto fail;
	}
	m->strtab = d.strtab;
	m->symtab = d.symtab;
	m->nsyms = d.nsyms;
	m->init = d.init;
	m->fini = d.fini;
	m->init_array = d.init_array;
	m->init_arraysz = d.init_arraysz;
	m->fini_array = d.fini_array;
	m->fini_arraysz = d.fini_arraysz;
	m->preinit_array = d.preinit_array;
	m->preinit_arraysz = d.preinit_arraysz;
	if (flags & LINUX_LOAD_STUBS)
	{
		int n = count_imports (&d, d.rela, d.relasz) + count_imports (&d, d.jmprel, d.pltrelsz);
		if (n)
		{
			m->stubs_size = PAGE_UP ((size_t) n * STUB_SIZE);
			m->stubs = mmap (0, m->stubs_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (m->stubs == MAP_FAILED)
				m->stubs = 0, m->stubs_size = 0;
		}
	}
	struct reloc_state st = { m, &d, flags, 0, 0, 0, err, errlen };
	if (apply (&st, d.rela, d.relasz) || apply (&st, d.jmprel, d.pltrelsz))
		goto fail;
	if (st.missing)
	{
		if (st.missing > 1)
		{
			size_t l = err ? strlen (err) : 0;
			if (err && l + 1 < errlen)
				snprintf (err + l, errlen - l, " (et %u autres : linuxhost --check)", st.missing - 1);
		}
		goto fail;
	}
	if (st.tls)
	{
		set_err (err, errlen, "%u relocations de TLS « initial exec » ou d'un autre module : pas servies", st.tls);
		goto fail;
	}
	if (m->stubs)
		mprotect (m->stubs, m->stubs_size, PROT_READ | PROT_EXEC);
	/* les protections des segments (le code n'est plus inscriptible) */
	for (int i = 0; i < im.eh->e_phnum; i++)
	{
		const lx_phdr *p = &im.ph[i];
		if (p->p_type != PT_LOAD)
			continue;
		uint64_t a = PAGE_DOWN (p->p_vaddr), b = PAGE_UP (p->p_vaddr + p->p_memsz);
		/* (une page partagée par deux segments garde l'écriture : celle du suivant ou du précédent) */
		int prot = prot_of (p->p_flags);
		for (int j = 0; j < im.eh->e_phnum; j++)
		{
			const lx_phdr *q = &im.ph[j];
			if (j == i || q->p_type != PT_LOAD)
				continue;
			if (PAGE_DOWN (q->p_vaddr) < b && PAGE_UP (q->p_vaddr + q->p_memsz) > a)
				prot |= prot_of (q->p_flags);
		}
		mprotect (m->base + a, b - a, prot);
	}
	for (int i = 0; i < m->phnum; i++)
		if (m->ph[i].p_type == PT_GNU_EH_FRAME)
		{
			m->eh_frame = eh_frame_of (m->base + m->ph[i].p_vaddr);
			if (m->eh_frame)
				__register_frame (m->eh_frame);
		}
	linux_register (m);
	if (!(flags & (LINUX_LOAD_NOINIT | LINUX_LOAD_EXE)))	/* (un exécutable : par __libc_start_main) */
	{
		/* comme ld-linux : DT_INIT, puis DT_INIT_ARRAY, avec (argc, argv, envp) */
		static char *argv[] = { "linuxhost", 0 };
		static char *envp[] = { 0 };
		if (m->init)
			((void (*) (int, char **, char **)) (m->base + m->init)) (1, argv, envp);
		uint64_t *ia = (uint64_t *) (m->base + m->init_array);
		for (uint64_t i = 0; m->init_array && i < m->init_arraysz / 8; i++)
			if (ia[i] && ia[i] != (uint64_t) -1)
				((void (*) (int, char **, char **)) (uintptr_t) ia[i]) (1, argv, envp);
	}
	return m;
fail:
	munmap (area, m->span);
	if (m->stubs)
		munmap (m->stubs, m->stubs_size);
	tls_remove (m->tls_id);
	free (m->ph);
	free (m);
	return 0;
}

struct linux_module *linux_load (const void *image, size_t size, const char *name, int flags,
				 char *err, size_t errlen)
{
	struct lx_image im;
	if (parse_image (&im, image, size, err, errlen))
		return 0;
	return load_common (&im, -1, name, flags, err, errlen);
}

/* le fichier lu par morceaux : ses en-têtes, puis chaque segment à sa place (Claude Code : 381 Mo, pas de
   copie entière en mémoire) ; les tables dynamiques sont dans les segments */
#include <fcntl.h>
#include <sys/stat.h>

struct linux_module *linux_load_file (const char *path, int flags, char *err, size_t errlen)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0)
		return set_err (err, errlen, "%s", strerror (errno)), (struct linux_module *) 0;
	struct stat sb;
	unsigned char *hdr = 0;
	struct linux_module *m = 0;
	if (fstat (fd, &sb) != 0)
	{
		set_err (err, errlen, "%s", strerror (errno));
		goto out;
	}
	size_t avail = sb.st_size < 65536 ? (size_t) sb.st_size : 65536;
	hdr = malloc (avail ? avail : 1);
	if (!hdr || pread (fd, hdr, avail, 0) != (ssize_t) avail)
	{
		set_err (err, errlen, "lecture des en-têtes");
		goto out;
	}
	struct lx_image im;
	if (parse_image_part (&im, hdr, avail, (size_t) sb.st_size, err, errlen))
		goto out;
	m = load_common (&im, fd, path, flags, err, errlen);
out:
	free (hdr);
	close (fd);
	return m;
}

/* ---- exécutables : ce que exec.c lit, et __libc_start_main ---- */
void linux_exe_info (struct linux_module *m, struct linux_exe_info *x)
{
	memset (x, 0, sizeof *x);
	x->entry = m->entry;
	x->phdr = m->phdr;
	x->phnum = m->phnum;
	x->stack_size = m->stack_size;
	x->tls_init = m->tls_init;
	x->tls_filesz = m->tls_filesz;
	x->tls_memsz = m->tls_memsz;
	x->tls_align = m->tls_align;
	x->tls_span = m->tls_span;
}

static struct linux_module *s_exe;	/* l'exécutable lancé (linux_exe_start) */

void linux_exe_start (struct linux_module *m)
{
	s_exe = m;
}

/* atexit de libposix : comme _dl_fini pour l'exécutable, après ses __cxa_atexit (glibc.c les range dans
   le module) : DT_FINI_ARRAY à rebours, DT_FINI */
static void exe_fini (void)
{
	struct linux_module *m = s_exe;
	if (!m)
		return;
	linux_module_finalize (m->area);
	uint64_t *fa = (uint64_t *) (m->base + m->fini_array);
	for (uint64_t i = m->fini_array ? m->fini_arraysz / 8 : 0; i > 0; i--)
		if (fa[i - 1] && fa[i - 1] != (uint64_t) -1)
			((void (*) (void)) (uintptr_t) fa[i - 1]) ();
	if (m->fini)
		((void (*) (void)) (m->base + m->fini)) ();
	fflush (0);
}

/* glibc : __libc_start_main (main, argc, argv, init, fini, rtld_fini, stack_end), appelée par _start ;
   glibc 2.34 et après passe init = fini = NULL et lance elle-même les constructeurs de l'exécutable */
typedef void (*lx_init_fn) (int, char **, char **);

int linux_libc_start_main (int (*main_fn) (int, char **, char **), int argc, char **argv, lx_init_fn init,
			   void (*fini) (void), void (*rtld_fini) (void), void *stack_end)
{
	(void) rtld_fini;
	(void) stack_end;
	char **envp = argv + argc + 1;
	struct linux_module *m = s_exe;
	if (m)
	{
		static const char *const env_names[] = { "environ", "__environ", "_environ" };
		for (int i = 0; i < 3; i++)
		{
			char ***e = linux_sym (m, env_names[i]);
			if (e)
				*e = envp;
		}
		atexit (exe_fini);
	}
	if (fini)
		atexit (fini);
	if (init)
		init (argc, argv, envp);
	else if (m)
	{
		uint64_t *pa = (uint64_t *) (m->base + m->preinit_array);
		for (uint64_t i = 0; m->preinit_array && i < m->preinit_arraysz / 8; i++)
			((lx_init_fn) (uintptr_t) pa[i]) (argc, argv, envp);
		if (m->init)
			((lx_init_fn) (m->base + m->init)) (argc, argv, envp);
		uint64_t *ia = (uint64_t *) (m->base + m->init_array);
		for (uint64_t i = 0; m->init_array && i < m->init_arraysz / 8; i++)
			if (ia[i] && ia[i] != (uint64_t) -1)
				((lx_init_fn) (uintptr_t) ia[i]) (argc, argv, envp);
	}
	exit (main_fn (argc, argv, envp));
}

void *linux_sym (struct linux_module *m, const char *name)
{
	for (unsigned long i = 1; i < m->nsyms; i++)
	{
		const lx_sym *s = &m->symtab[i];
		if (s->st_shndx == SHN_UNDEF || (s->st_info >> 4) == STB_LOCAL)
			continue;
		if (!strcmp (m->strtab + s->st_name, name))
			return m->base + s->st_value;
	}
	return 0;
}

void *linux_base (struct linux_module *m)
{
	return m->base;
}

/* glibc.c : __cxa_atexit et __cxa_finalize d'un module (dso_handle dans [base, base + span)) */
int linux_module_atexit (void (*fn) (void *), void *arg, void *dso);
void linux_module_finalize (void *dso);

#define MAX_MODULES	16
static struct linux_module *s_modules[MAX_MODULES];

static struct linux_module *module_of (void *p)
{
	for (int i = 0; i < MAX_MODULES; i++)
	{
		struct linux_module *m = s_modules[i];
		if (m && (unsigned char *) p >= m->area && (unsigned char *) p < m->area + m->span)
			return m;
	}
	return 0;
}

/* les plages des modules chargés, déclarées au noyau : leurs syscall directs reviennent à linux_syscall
   (capture.c) ; au plus 4 modules capturés */
static void capture_update (void)
{
	uint64_t r[8];
	int n = 0;
	for (int i = 0; i < MAX_MODULES && n < 4; i++)
		if (s_modules[i])
		{
			r[2 * n] = (uint64_t) (uintptr_t) s_modules[i]->area;
			r[2 * n + 1] = r[2 * n] + s_modules[i]->span;
			n++;
		}
	int e = linux_capture_set (r, n);
	if (e < 0)
		fprintf (stderr, "liblinux : capture de syscall refusée par le noyau (%d) : les syscall directs des modules"
			 " ne seront pas traduits\n", e);
}

/* (à la fin de linux_load réussi, avant les constructeurs, qui peuvent déjà faire des syscall directs) */
static void linux_register (struct linux_module *m)
{
	for (int i = 0; i < MAX_MODULES; i++)
		if (!s_modules[i])
		{
			s_modules[i] = m;
			break;
		}
	capture_update ();
}

int linux_module_atexit (void (*fn) (void *), void *arg, void *dso)
{
	struct linux_module *m = module_of (dso);
	if (!m)
		return -1;	/* (pas un module : glibc.c le donne à l'atexit du programme) */
	struct linux_atexit *a = malloc (sizeof *a);
	if (!a)
		return -1;
	a->fn = fn;
	a->arg = arg;
	a->next = m->atexit;
	m->atexit = a;
	return 0;
}

void linux_module_finalize (void *dso)
{
	struct linux_module *m = module_of (dso);
	if (!m)
		return;
	while (m->atexit)
	{
		struct linux_atexit *a = m->atexit;
		m->atexit = a->next;
		a->fn (a->arg);
		free (a);
	}
}

void linux_unload (struct linux_module *m)
{
	if (!m)
		return;
	linux_module_finalize (m->area);
	uint64_t *fa = (uint64_t *) (m->base + m->fini_array);
	for (uint64_t i = m->fini_array ? m->fini_arraysz / 8 : 0; i > 0; i--)
		if (fa[i - 1] && fa[i - 1] != (uint64_t) -1)
			((void (*) (void)) (uintptr_t) fa[i - 1]) ();
	if (m->fini)
		((void (*) (void)) (m->base + m->fini)) ();
	for (int i = 0; i < MAX_MODULES; i++)
		if (s_modules[i] == m)
			s_modules[i] = 0;
	capture_update ();
	if (m->eh_frame)
		__deregister_frame (m->eh_frame);
	tls_remove (m->tls_id);
	munmap (m->area, m->span);
	if (m->stubs)
		munmap (m->stubs, m->stubs_size);
	free (m->ph);
	free (m);
}

/* ---- /proc/self/maps au format de Linux (syscall.c le sert aux modules) ----
   Les segments de chaque module avec son chemin (la partie au-delà du fichier : anonyme, comme Linux),
   ceux du programme hôte sous le nom de la libc de Linux (la glibc de compatibilité y vit : les fonctions
   que le module importe tombent donc dans « libc.so.6 », comme sous Linux), et la pile. */
#include <link.h>

struct maps_out
{
	char *buf;
	size_t cap, len;
};

static void maps_line (struct maps_out *o, uint64_t lo, uint64_t hi, int prot, uint64_t off, int file, uint64_t inode,
		       const char *path)
{
	if (hi <= lo || o->len >= o->cap)
		return;
	char perm[5] = { prot & PF_R ? 'r' : '-', prot & PF_W ? 'w' : '-', prot & PF_X ? 'x' : '-', 'p', 0 };
	int n = snprintf (o->buf + o->len, o->cap - o->len, "%08llx-%08llx %s %08llx %s %-10llu",
			  (unsigned long long) lo, (unsigned long long) hi, perm, (unsigned long long) off,
			  file ? "08:01" : "00:00", (unsigned long long) inode);
	if (n < 0)
		return;
	o->len += (size_t) n < o->cap - o->len ? (size_t) n : o->cap - o->len;
	/* (Linux aligne le chemin sur la colonne 73) */
	n = snprintf (o->buf + o->len, o->cap - o->len, "%*s%s\n", path && *path ? (int) (73 - (n < 73 ? n : 72)) : 0, "",
		      path ? path : "");
	if (n > 0)
		o->len += (size_t) n < o->cap - o->len ? (size_t) n : o->cap - o->len;
}

static int maps_host (struct dl_phdr_info *in, size_t size, void *arg)
{
	(void) size;
	struct maps_out *o = arg;
	if (in->dlpi_name && *in->dlpi_name)	/* (le programme hôte seulement : nom vide) */
		return 0;
	for (int i = 0; i < in->dlpi_phnum; i++)
	{
		const ElfW (Phdr) *p = &in->dlpi_phdr[i];
		if (p->p_type != PT_LOAD)
			continue;
		uint64_t lo = PAGE_DOWN (in->dlpi_addr + p->p_vaddr), hi = PAGE_UP (in->dlpi_addr + p->p_vaddr + p->p_memsz);
		maps_line (o, lo, hi, (int) p->p_flags, PAGE_DOWN (p->p_offset), 1, 3940652,
			   "/usr/lib/x86_64-linux-gnu/libc.so.6");
	}
	return 1;
}

size_t linux_maps (char *buf, size_t cap)
{
	struct maps_out o = { buf, cap, 0 };
	dl_iterate_phdr (maps_host, &o);
	for (int k = 0; k < MAX_MODULES; k++)
	{
		struct linux_module *m = s_modules[k];
		if (!m)
			continue;
		for (int i = 0; i < m->phnum; i++)
		{
			const lx_phdr *p = &m->ph[i];
			if (p->p_type != PT_LOAD)
				continue;
			uint64_t lo = (uint64_t) (uintptr_t) m->base + PAGE_DOWN (p->p_vaddr);
			uint64_t fend = (uint64_t) (uintptr_t) m->base + PAGE_UP (p->p_vaddr + p->p_filesz);
			uint64_t hi = (uint64_t) (uintptr_t) m->base + PAGE_UP (p->p_vaddr + p->p_memsz);
			maps_line (&o, lo, fend, (int) p->p_flags, PAGE_DOWN (p->p_offset), 1, 2621447 + (uint64_t) k, m->name);
			maps_line (&o, fend, hi, (int) p->p_flags, 0, 0, 0, "");
		}
	}
	/* la pile du thread : autour de l'adresse d'une variable locale */
	uint64_t sp = (uint64_t) (uintptr_t) &o;
	maps_line (&o, PAGE_DOWN (sp) - 0x100000, PAGE_UP (sp) + 0x10000, PF_R | PF_W, 0, 0, 0, "[stack]");
	if (o.len < cap)
		buf[o.len] = 0;
	return o.len;
}

/* ---- linux_check : le rapport sans exécution ---- */
static int raw_syscalls (const struct lx_image *im)
{
	int n = 0;
	for (int i = 0; i < im->eh->e_phnum; i++)
	{
		const lx_phdr *p = &im->ph[i];
		if (p->p_type != PT_LOAD || !(p->p_flags & PF_X))
			continue;
		const unsigned char *c = im->file + p->p_offset;
		for (uint64_t j = 0; j + 1 < p->p_filesz; j++)
			if (c[j] == 0x0f && c[j + 1] == 0x05)	/* (octets 0f 05 : syscall, ou hasard des données) */
				n++;
	}
	return n;
}

static void check_relocs (const struct lx_dynamic *d, const lx_rela *r, uint64_t bytes, FILE *out, int verbose,
			  int *imports, int *missing, int *tls, int *other)
{
	for (uint64_t i = 0; r && i < bytes / sizeof (lx_rela); i++)
	{
		uint32_t type = (uint32_t) r[i].r_info, symi = (uint32_t) (r[i].r_info >> 32);
		if (type == R_X86_64_DTPMOD64 || type == R_X86_64_DTPOFF64 || type == R_X86_64_TPOFF64)
			(*tls)++;
		else if (type != R_X86_64_NONE && type != R_X86_64_RELATIVE && type != R_X86_64_64 && type != R_X86_64_GLOB_DAT
			 && type != R_X86_64_JUMP_SLOT && type != R_X86_64_IRELATIVE && type != R_X86_64_COPY)
			(*other)++;
		if (!symi || d->symtab[symi].st_shndx != SHN_UNDEF)
			continue;
		const char *name = d->strtab + d->symtab[symi].st_name;
		int weak = (d->symtab[symi].st_info >> 4) == STB_WEAK;
		int ok = linux_glibc_lookup (name) != 0;
		(*imports)++;
		if (!ok && !weak)
			(*missing)++;
		if (verbose || (!ok && !weak))
			fprintf (out, "  %-8s %s\n", ok ? "servi" : weak ? "faible" : "ABSENT", name);
	}
}

int linux_check (const void *image, size_t size, FILE *out, int verbose)
{
	struct lx_image im;
	char err[160];
	if (parse_image (&im, image, size, err, sizeof err))
	{
		fprintf (out, "refusé : %s\n", err);
		return -1;
	}
	const lx_dyn *dyn = (const lx_dyn *) (im.file + im.dynph->p_offset);
	struct lx_dynamic d;
	read_dynamic (&d, dyn, xlate_file, &im);
	if (!d.strtab || !d.symtab)
	{
		fprintf (out, "refusé : tables dynamiques incomplètes\n");
		return -1;
	}
	fprintf (out, "ELF64 x86-64 %s, [%#llx, %#llx), %llu Ko en mémoire, %lu symboles\n",
		 im.eh->e_type == ET_EXEC ? "ET_EXEC (exécutable à adresse fixe)" : im.interp ? "ET_DYN (exécutable PIE)" : "ET_DYN",
		 (unsigned long long) im.lo, (unsigned long long) im.hi, (unsigned long long) ((im.hi - im.lo) / 1024), d.nsyms);
	if (im.tlsph)
		fprintf (out, "PT_TLS : %llu octets (%llu initialisés), alignement %llu\n", (unsigned long long) im.tlsph->p_memsz,
			 (unsigned long long) im.tlsph->p_filesz, (unsigned long long) im.tlsph->p_align);
	if (im.stack_size)
		fprintf (out, "PT_GNU_STACK : pile de %llu Ko demandée\n", (unsigned long long) (im.stack_size / 1024));
	if (d.soname)
		fprintf (out, "SONAME %s\n", d.strtab + d.soname - 1);
	for (const lx_dyn *p = dyn; p->d_tag != DT_NULL; p++)
		if (p->d_tag == DT_NEEDED)
			fprintf (out, "NEEDED %s\n", d.strtab + p->d_val);
	int imports = 0, missing = 0, tls = 0, other = 0;
	check_relocs (&d, d.rela, d.relasz, out, verbose, &imports, &missing, &tls, &other);
	check_relocs (&d, d.jmprel, d.pltrelsz, out, verbose, &imports, &missing, &tls, &other);
	fprintf (out, "imports : %d, absents : %d\n", imports, missing);
	fprintf (out, "TLS : %s, %d relocations\n", im.tlsph ? "PT_TLS" : "aucun segment", tls);
	if (other)
		fprintf (out, "relocations d'autres types : %d (refusées)\n", other);
	fprintf (out, "octets 0f 05 (syscall direct possible) dans le code : %d\n", raw_syscalls (&im));
	unsigned long exported = 0;
	for (unsigned long i = 1; i < d.nsyms; i++)
		if (d.symtab[i].st_shndx != SHN_UNDEF && (d.symtab[i].st_info >> 4) != STB_LOCAL)
		{
			exported++;
			if (verbose)
				fprintf (out, "  exporte %s\n", d.strtab + d.symtab[i].st_name);
		}
	fprintf (out, "symboles exportés : %lu\n", exported);
	return missing;
}
