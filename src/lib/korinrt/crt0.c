/* crt0.c : l'entrée d'une commande en C (docs/BIN-C.md), comme t_debut et
 * t_fin de src/inc/tool.inc.
 *
 * Le noyau entre dans _start avec la ligne d'arguments en x0 (AArch64) ou
 * rdi (x86-64), 0 s'il n'y en a pas. On ouvre AppKit (comme kits_open de
 * src/inc/kit.inc : absent ou trop ancien, message sur la console et code
 * 127), puis argv vient de l'objet donné par le père (f_start_blob : +4
 * argc, +8 nombre de variables, +16 les chaînes à la suite : argv puis
 * l'environnement), sinon de la ligne coupée comme t_ligne_coupee. */
#include "korinrt.h"

#ifndef KRT_APPKIT_MIN
#define KRT_APPKIT_MIN 1 /* (tools/bin/build_c.sh donne le nombre de fonctions d'appkit.abi) */
#endif

int krt_argc;
char **krt_argv;
long krt_envc;
char *krt_env;
const char *krt_line;

/* ligne (0 : aucune) -> argc, argv : coupée aux espaces ; un mot qui
 * commence par un guillemet va jusqu'au guillemet suivant, avec \" et \\ ;
 * argv[0] = « commande » (comme l'asm : la ligne ne porte pas le nom) */
static void split_line(const char *line)
{
	static char name[] = "commande";
	long cap = 2;
	if (line)
		for (const char *p = line; *p; p++)
			if (*p == ' ' || *p == '"')
				cap++;
	char **argv = ak_malloc((u64)(cap + 1) * 8);
	int argc = 0;
	argv[argc++] = name;
	if (line) {
		u64 n = (u64)str_len(line) + 1;
		char *s = ak_malloc(n);
		mem_move(s, line, n);
		for (;;) {
			while (*s == ' ')
				s++;
			if (!*s)
				break;
			if (*s != '"') {
				argv[argc++] = s;
				while (*s && *s != ' ')
					s++;
				if (!*s)
					break;
				*s++ = 0;
				continue;
			}
			s++; /* entre guillemets, recopié sur place */
			argv[argc++] = s;
			char *o = s;
			for (;;) {
				char c = *s;
				if (!c)
					break;
				s++;
				if (c == '"')
					break;
				if (c == '\\' && (*s == '"' || *s == '\\'))
					c = *s++;
				*o++ = c;
			}
			*o = 0; /* (guillemet fermé : mot suivant ; fin de la ligne : fini) */
		}
	}
	argv[argc] = 0;
	krt_argc = argc;
	krt_argv = argv;
}

static void start_args(const char *line)
{
	krt_line = line;
	/* d'abord les descripteurs d'AppKit d'avant un exec : f_start_blob efface les marques des
	   poignées (SYS_HANDLE_LIST), dont EXEC_FC_TAG que fc_exec_resume cherche */
	f_exec_resume();
	u8 *blob = f_start_blob();
	if (!blob) {
		split_line(line);
		return;
	}
	u32 argc = *(u32 *)(blob + 4);
	krt_envc = *(u32 *)(blob + 8);
	char *s = (char *)blob + 16;
	char **argv = ak_malloc((u64)(argc + 1) * 8);
	for (u32 i = 0; i < argc; i++) {
		argv[i] = s;
		while (*s++)
			;
	}
	argv[argc] = 0;
	krt_env = s; /* l'environnement suit argv */
	krt_argc = (int)argc;
	krt_argv = argv;
}

const char *getenv(const char *name)
{
	char *s = krt_env;
	if (!s)
		return 0;
	for (long i = 0; i < krt_envc; i++) {
		const char *a = name, *b = s;
		while (*a && *a == *b)
			a++, b++;
		if (!*a && *b == '=')
			return b + 1;
		while (*s++)
			;
	}
	return 0;
}

NORETURN void _exit(int code)
{
	for (;;)
		korin_sys1(SYS_EXIT, (long)code);
}

NORETURN void exit(int code)
{
	out_flush();
	_exit(code);
}

/* kits_open de src/inc/kit.inc : AppKit, puis les kits importés par la commande
 * (src/bin/<nom>/bin.conf « import = netkit » : tools/bin/build_c.sh écrit
 * krt_kits et krt_kits_min ; un kit pas encore en mémoire est lu par
 * f_kit_load, puis rouvert) ; absent ou trop ancien : message sur la console
 * du noyau et code 127 */
extern const char *const krt_kits[] __attribute__((weak)); /* (absents : 0) */
extern const long krt_kits_min[] __attribute__((weak));

static NORETURN void kit_fail(const char *name)
{
	static const char msg[] = "kit absent ou trop ancien : SD0:/lib/";
	korin_sys2(SYS_PRINT, msg, sizeof msg - 1);
	korin_sys2(SYS_PRINT, name, strlen(name));
	korin_sys2(SYS_PRINT, "\n", 1);
	_exit(127);
}

static void kits_open(void)
{
	if (korin_sys2(SYS_LIB_OPEN, "appkit", KRT_APPKIT_MIN) < 0)
		kit_fail("appkit");
	if (!krt_kits)
		return;
	for (int i = 0; krt_kits[i]; i++) {
		long r = korin_sys2(SYS_LIB_OPEN, krt_kits[i], krt_kits_min[i]);
		if (r == E_NOENT) {
			f_kit_load(krt_kits[i]);
			r = korin_sys2(SYS_LIB_OPEN, krt_kits[i], krt_kits_min[i]);
		}
		if (r < 0)
			kit_fail(krt_kits[i]);
	}
}

#if defined(__x86_64__)
__attribute__((force_align_arg_pointer))
#endif
NORETURN void _start(const char *line)
{
	kits_open();
	start_args(line);
	exit(main(krt_argc, krt_argv));
}
