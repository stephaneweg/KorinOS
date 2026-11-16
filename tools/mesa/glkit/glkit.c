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

#if defined(__x86_64__)
/* Korin PC (tools/mesa/build-glkit-pc.sh) : les memes, numeros de x86-64 ; et le modele « general
 * dynamic » (DTPMOD64 / DTPOFF64 + __tls_get_addr), celui de gcc -fPIC sur x86-64 */
enum { R_ABS64 = 1, R_GLOB_DAT = 6, R_JUMP_SLOT = 7, R_RELATIVE = 8, R_DTPMOD64 = 16, R_DTPOFF64 = 17,
	R_TLS_TPREL64 = 18, R_TLSDESC = 36, R_IRELATIVE = 37 };
/* les « modules » TLS : 1 = le kit (dans glkit_tls_area), 2 = un decalage deja compte depuis le pointeur
 * de thread (une variable de la libc du programme) */
enum { TLS_MOD_KIT = 1, TLS_MOD_ABS = 2 };
#else
enum { R_ABS64 = 257, R_GLOB_DAT = 1025, R_JUMP_SLOT = 1026, R_RELATIVE = 1027,
	R_TLS_TPREL64 = 1030, R_TLSDESC = 1031 };
#endif

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

/* Remplaçants des fonctions qu'un programme relié à une libglkit.a plus ancienne ne donne pas (celles du cache
 * de shaders d'iris, 16/11) : « pas disponible » (-1, ou un numéro d'erreur pour celles qui le rendent), le cache
 * se coupe de lui-même ; un programme relié à nouveau donne les vraies. Appelés après glkit_init seulement. */
static uintptr_t s_fseek, s_clearerr;
static long fb_fail (void) { return -1; }
static long fb_enosys (void) { return 38; }	/* ENOSYS : posix_fallocate */
static void fb_none (void) { }
static int fb_getpwuid_r (unsigned uid, void *pwd, char *buf, unsigned long n, void **res)
{
	(void) uid; (void) pwd; (void) buf; (void) n;
	*res = 0;
	return 38;
}
static void fb_rewind (void *f)
{
	if (s_fseek)
		((int (*) (void *, long, int)) s_fseek) (f, 0, 0);
	if (s_clearerr)
		((void (*) (void *)) s_clearerr) (f);
}

/* (sans table de pointeurs : elle aurait besoin des relocations que l'on est en train de faire) */
static uintptr_t fallback (const char *name)
{
	const struct glkit_import *m;
	if (str_eq (name, "strtoul"))		/* unsigned long = unsigned long long (LP64) */
		return (m = import ("strtoull", 0)) ? m->value : 0;
	if (str_eq (name, "rewind")) {
		s_fseek = (m = import ("fseek", 0)) ? m->value : 0;
		s_clearerr = (m = import ("clearerr", 0)) ? m->value : 0;
		return (uintptr_t) fb_rewind;
	}
	if (str_eq (name, "getpwuid_r"))
		return (uintptr_t) fb_getpwuid_r;
	if (str_eq (name, "posix_fallocate"))
		return (uintptr_t) fb_enosys;
	if (str_eq (name, "rewinddir"))
		return (uintptr_t) fb_none;
	if (str_eq (name, "flock") || str_eq (name, "ftruncate") || str_eq (name, "fileno") || str_eq (name, "dirfd")
	    || str_eq (name, "fstatat") || str_eq (name, "rmdir") || str_eq (name, "utime")
	    || str_eq (name, "getrandom") || str_eq (name, "setenv"))
		return (uintptr_t) fb_fail;
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
	uintptr_t f = fallback (name);
	if (f)
		return f;
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
#if defined(__x86_64__)
		case R_IRELATIVE:
			*w = ((uintptr_t (*) (void)) (base + r->add)) ();
			break;
		case R_DTPMOD64:	/* le kit lui-meme (s = 0, ou un symbole du kit) ou une variable du programme */
			*w = s == 0 || __dynsym_start[s].shndx != 0 ? TLS_MOD_KIT : TLS_MOD_ABS;
			break;
		case R_DTPOFF64:
			if (s == 0 || __dynsym_start[s].shndx != 0)
				*w = (s ? __dynsym_start[s].value : 0) + r->add;
			else
				*w = sym_tls (s, &ok) + r->add;
			break;
#endif
		default:
			msg ("glkit : relocation inconnue", 0);
			ok = 0;
		}
	}
	return ok;
}

#if defined(__x86_64__)
/* le modele « general dynamic » : appele par le code du kit (cache, lie dans le kit, pas pris au programme) */
typedef struct { uintptr_t module, offset; } tls_index;
H void *__tls_get_addr (tls_index *ti)
{
	uintptr_t tp = (uintptr_t) __builtin_thread_pointer ();
	return (void *) (tp + (ti->module == TLS_MOD_KIT ? s_tls_off : 0) + ti->offset);
}
#endif

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
