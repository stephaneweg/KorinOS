/*
 * glkit.c -- le chargeur de GlKit, dans le kit (docs/MESA.md « GlKit ») : Mesa et armgl, lies en
 * objet partage a 0 (glkit.ld), puis inscrits comme un kit d'ARMPI. Le noyau mappe le code une
 * fois pour tous les process et recopie les variables dans chacun ; glkit_init, appele par
 * libglkit.a dans chaque process, fait ce que ferait un editeur de liens dynamique :
 *
 *  * R_AARCH64_RELATIVE : base du kit + addend ;
 *  * symboles : ceux du kit, sinon ceux que le programme donne (sa libc, libposix, libstdc++,
 *    libgcc : la table de libglkit.a, par nom) ;
 *  * TLS : les variables par thread du kit vivent dans une zone du programme (glkit_tls_area, a
 *    un decalage fixe de TPIDR_EL0) ; les descripteurs (R_AARCH64_TLSDESC) rendent ce decalage ;
 *  * puis les constructeurs (.init_array).
 *
 * Avant la fin de glkit_init, aucune fonction de la libc : le code d'ici ne passe que par des
 * adresses relatives au compteur ordinal (-fvisibility=hidden, rien dans la GOT).
 */
#include <stdint.h>
#include <stddef.h>

#define H __attribute__ ((visibility ("hidden")))

typedef struct { uint64_t off, info; int64_t add; } rela_t;
typedef struct { uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size; } sym_t;

/* (glkit.ld) */
extern char __kit_base[] H;
extern const rela_t __rela_dyn_start[] H, __rela_dyn_end[] H, __rela_plt_start[] H, __rela_plt_end[] H;
extern const sym_t __dynsym_start[] H;
extern const char __dynstr_start[] H;
extern const uint32_t __hash_start[] H;
extern void (*const __init_start[]) (void) H;
extern void (*const __init_end[]) (void) H;
extern void glkit_tlsdesc_static (void) H;
void _mesa_glapi_set_dispatch (void *);		/* (Mesa, shared-glapi/core.c) */

/* les besoins par thread du kit (PT_TLS : taille, alignement), donnes par build-glkit.sh */
#ifndef GLKIT_TLS_SIZE
#error GLKIT_TLS_SIZE
#endif
#ifndef GLKIT_TLS_ALIGN
#error GLKIT_TLS_ALIGN
#endif

void *__dso_handle H = &__dso_handle;

/* un symbole donne par le programme : adresse (kind 0) ou decalage TLS depuis TPIDR_EL0 (kind 1) */
struct glkit_import { const char *name; uintptr_t value; long kind; };

enum { R_ABS64 = 257, R_GLOB_DAT = 1025, R_JUMP_SLOT = 1026, R_RELATIVE = 1027,
	R_TLS_TPREL64 = 1030, R_TLSDESC = 1031 };

H int glkit_ready;
H char glkit_msg[160];
static const struct glkit_import *s_imp;
static long s_nimp;
static long s_tls_off;

static int str_eq (const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static void msg (const char *a, const char *b)
{
	unsigned n = 0;
	for (; a && *a && n < sizeof glkit_msg - 1; a++)
		glkit_msg[n++] = *a;
	for (; b && *b && n < sizeof glkit_msg - 1; b++)
		glkit_msg[n++] = *b;
	glkit_msg[n] = 0;
}

static const struct glkit_import *import (const char *name, long kind)
{
	for (long i = 0; i < s_nimp; i++)
		if (s_imp[i].kind == kind && str_eq (s_imp[i].name, name))
			return &s_imp[i];
	return 0;
}

/* adresse d'un symbole : celui du kit, ou celui du programme ; *ok = 0 s'il manque */
static uintptr_t sym_addr (uint32_t i, int *ok)
{
	const sym_t *s = &__dynsym_start[i];
	if (s->shndx != 0)
		return (uintptr_t) __kit_base + s->value;
	const char *name = __dynstr_start + s->name;
	const struct glkit_import *m = import (name, 0);
	if (m != 0 && m->value != 0)
		return m->value;
	if ((s->info >> 4) == 2)		/* STB_WEAK : absent permis */
		return 0;
	msg ("glkit : symbole absent du programme : ", name);
	*ok = 0;
	return 0;
}

/* decalage TLS depuis TPIDR_EL0 : variable du kit (dans glkit_tls_area) ou du programme */
static uintptr_t sym_tls (uint32_t i, int *ok)
{
	if (i == 0)
		return s_tls_off;
	const sym_t *s = &__dynsym_start[i];
	if (s->shndx != 0)
		return s_tls_off + s->value;
	const char *name = __dynstr_start + s->name;
	const struct glkit_import *m = import (name, 1);
	if (m != 0)
		return m->value;
	msg ("glkit : variable par thread absente du programme : ", name);
	*ok = 0;
	return 0;
}

static int relocate (const rela_t *r, const rela_t *end)
{
	uintptr_t base = (uintptr_t) __kit_base;
	int ok = 1;
	for (; r < end && ok; r++)
	{
		uint64_t *w = (uint64_t *) (base + r->off);
		uint32_t t = (uint32_t) r->info, s = (uint32_t) (r->info >> 32);
		switch (t)
		{
		case R_RELATIVE:
			*w = base + r->add;
			break;
		case R_ABS64:
		case R_GLOB_DAT:
		case R_JUMP_SLOT:
			*w = sym_addr (s, &ok) + r->add;
			break;
		case R_TLS_TPREL64:
			*w = sym_tls (s, &ok) + r->add;
			break;
		case R_TLSDESC:
			w[0] = (uintptr_t) glkit_tlsdesc_static;
			w[1] = sym_tls (s, &ok) + r->add;
			break;
		default:
			msg ("glkit : relocation inconnue", 0);
			ok = 0;
		}
	}
	return ok;
}

/* 0 : version (= nombre de fonctions de la table) */
H long glkit_version (void)
{
	return 4;
}

/* 2 : l'adresse d'une fonction ou variable exportee par le kit (gl*, armgl_*...), 0 si aucune
 * (table de hachage SysV de l'objet partage ; utilisable avant glkit_init) */
H void *glkit_sym (const char *name)
{
	uint32_t h = 0, g;
	for (const unsigned char *p = (const unsigned char *) name; *p; p++)
	{
		h = (h << 4) + *p;
		g = h & 0xF0000000u;
		if (g)
			h ^= g >> 24;
		h &= ~g;
	}
	uint32_t nbucket = __hash_start[0];
	const uint32_t *bucket = &__hash_start[2], *chain = &__hash_start[2 + nbucket];
	for (uint32_t i = bucket[h % nbucket]; i != 0; i = chain[i])
	{
		const sym_t *s = &__dynsym_start[i];
		if (s->shndx != 0 && str_eq (__dynstr_start + s->name, name))
			return __kit_base + s->value;
	}
	return 0;
}

/* 1 : glkit_init(imports, nombre, decalage de glkit_tls_area depuis TPIDR_EL0, sa taille) ->
 * 0, ou -1 (texte : glkit_error). Une fois par process (libglkit.a le garde par un verrou). */
H long glkit_init (const struct glkit_import *imp, long n, long tls_off, long tls_size)
{
	if (glkit_ready)
		return 0;
	/* (la zone est alignee sur 64 octets par libglkit.a : assez pour GLKIT_TLS_ALIGN) */
	if (tls_size < GLKIT_TLS_SIZE || GLKIT_TLS_ALIGN > 64)
	{
		msg ("glkit : zone par thread du programme trop petite (libglkit.a trop ancienne)", 0);
		return -1;
	}
	s_imp = imp;
	s_nimp = n;
	s_tls_off = tls_off;
	if (!relocate (__rela_dyn_start, __rela_dyn_end) || !relocate (__rela_plt_start, __rela_plt_end))
		return -1;
	glkit_ready = 1;
	for (void (*const *f) (void) = __init_start; f < __init_end; f++)
		(*f) ();
	/* la table de distribution de GL du thread : celle qui ne fait rien (sa valeur initiale,
	 * qu'une zone par thread mise a zero n'a pas) ; un autre thread la recoit a son
	 * eglMakeCurrent / armgl_make_current */
	_mesa_glapi_set_dispatch (0);
	return 0;
}

/* 3 : le texte de la derniere erreur */
H const char *glkit_error (void)
{
	return glkit_msg;
}
