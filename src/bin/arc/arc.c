/* arc - archives en ligne de commande, sur ArchiveKit (docs/OUTILS.md, docs/KITS.md), comme arc.asm :
 *   arc l archive                  liste : taille, taille compressée, « d » pour un dossier, nom
 *   arc t archive                  lit tout, vérifie les sommes (code 1 s'il y a des mauvaises)
 *   arc x archive [dossier]        extrait tout (dossier courant par défaut)
 *   arc c archive [-N] fichiers    crée (format d'après l'extension : .tgz .tar.gz .tar .gz, sinon ZIP ;
 *                                  -0 stocke ... -9 ; un dossier est pris avec ce qu'il contient)
 *   arc f                          formats connus
 * Usage : code 2 ; une erreur d'ArchiveKit : « arc : chemin : erreur » (texte d'ar_strerror, non
 * traduit), code 1.
 *
 * Comme l'asm : la commande est un mot d'une lettre ; « -N » vaut pour les fichiers qui le suivent,
 * toute autre chose qui commence par « - » est un nom ; une erreur d'ajout laisse l'archive commencée. */
#include "korinrt.h"
#include "archivekit_import.h"

#define AR_FMT_ZIP 1
#define AR_FMT_TAR 2
#define AR_FMT_TGZ 3
#define AR_FMT_GZIP 4
#define ARE_NAME 0
#define ARE_SIZE 1
#define ARE_PACKED 2
#define ARE_DIR 4
#define ARFI_NAME 24
#define ARFI_EXT 40
#define ARFI_SIZE 128

static char **av;

/* erreur, chemin ou 0 : la dit, code 1 */
NORETURN static void fail(long e, const char *path)
{
	err_puts(av[0]);
	err_say(" : ");
	if (path) {
		err_puts(path);
		err_say(" : ");
	}
	err_puts((const char *)ar_strerror(e));
	err_puts("\n");
	exit(1);
}

static long open_arg(void)
{
	long a = ar_open((long)av[2]);
	if (a < 0)
		fail(a, av[2]);
	return a;
}

/* un nombre sur 12 colonnes, puis un blanc */
static void put_w(u64 v)
{
	char t[32];
	long k = 12 - (fmt_dec(v, t) - t);
	while (k-- > 0)
		out_putc(' ');
	out_puts(t);
	out_putc(' ');
}

static int do_list(void)
{
	long a = open_arg(), info[8];
	long i;
	for (i = 0; i < ar_count(a); i++) {
		ar_entry(a, i, (long)info);
		put_w((u64)info[ARE_SIZE]);
		put_w((u64)info[ARE_PACKED]);
		out_putc(info[ARE_DIR] ? 'd' : ' ');
		out_putc(' ');
		out_puts((const char *)info[ARE_NAME]);
		out_nl();
	}
	out_putn((u64)i);
	out_say(" entrees, ");
	out_puts((const char *)ar_format_name(ar_format(a)));
	out_nl();
	ar_close(a);
	return 0;
}

static int do_test(void)
{
	long a = open_arg(), i, bad = 0;
	for (i = 0; i < ar_count(a); i++) {
		long r = ar_test(a, i);
		if (!r)
			continue;
		bad++;
		out_puts((const char *)ar_name(a, i));
		out_say(" : ");
		out_puts((const char *)ar_strerror(r));
		out_nl();
	}
	out_putn((u64)i);
	out_say(" entrees lues, ");
	out_putn((u64)bad);
	out_say(" mauvaises\n");
	ar_close(a);
	return bad != 0;
}

static int do_extract(int argc)
{
	long a = open_arg();
	const char *dir = argc >= 4 ? av[3] : ".";
	long r = ar_extract_all(a, (long)dir);
	if (r < 0)
		fail(r, av[2]);
	out_putn((u64)r);
	out_say(" fichiers extraits\n");
	ar_close(a);
	return 0;
}

/* chemin -> AR_FMT_* d'après son extension (ZIP sinon) */
static long fmt_of(const char *p)
{
	if (str_ends(p, ".tgz") || str_ends(p, ".tar.gz"))
		return AR_FMT_TGZ;
	if (str_ends(p, ".tar"))
		return AR_FMT_TAR;
	if (str_ends(p, ".gz"))
		return AR_FMT_GZIP;
	return AR_FMT_ZIP;
}

static int do_create(int argc)
{
	long w = ar_create((long)av[2], fmt_of(av[2]));
	if (w < 0)
		fail(w, av[2]);
	long level = -1;
	for (int i = 3; i < argc; i++) {
		const char *f = av[i];
		if (f[0] == '-' && (unsigned)((u8)f[1] - '0') <= 9) {
			level = f[1] - '0';
			continue;
		}
		long r = ar_add_file(w, 0, (long)f, level);
		if (r < 0)
			fail(r, f);
	}
	long r = ar_finish(w);
	if (r < 0)
		fail(r, av[2]);
	out_putn((u64)r);
	out_say(" octets\n");
	ar_free(w);
	return 0;
}

static int do_formats(void)
{
	static u8 fmts[ARFI_SIZE * 8];
	long k = ar_formats((long)fmts, 8);
	for (u8 *f = fmts; k; k--, f += ARFI_SIZE) {
		out_puts((const char *)f + ARFI_NAME);
		out_say(" : ");
		out_puts((const char *)f + ARFI_EXT);
		out_nl();
	}
	return 0;
}

int main(int argc, char **argv)
{
	av = argv;
	if (argc < 2 || argv[1][1])
		goto usage;
	int c = argv[1][0];
	if (c == 'f')
		return do_formats();
	if (argc < 3)
		goto usage;
	if (c == 'l')
		return do_list();
	if (c == 't')
		return do_test();
	if (c == 'x')
		return do_extract(argc);
	if (c == 'c')
		return do_create(argc);
usage:
	err_msg("arc l|t|x|c|f archive [dossier | [-N] fichiers]");
	return 2;
}
