/* unzip - extrait, liste, essaie des archives ZIP (et ce qu'ArchiveKit lit : tar, tar.gz, gz), comme
 * unzip.asm (d'après unzip d'Info-ZIP et celui d'Onyx ; docs/OUTILS.md) :
 *
 *   unzip [-l | -v | -t | -p] [-o | -n] [-j] [-q] [-d DOSSIER] ARCHIVE [NOM ...] [-x NOM ...]
 *
 * -l liste, -v liste détaillée (méthode, taille compressée, taux, CRC), -t essai, -p vers la sortie
 * standard ; -o remplace, -n ne remplace jamais (sinon la question sur l'entrée standard : [o]ui,
 * [n]on, [t]ous, [r]ien ; y, A, N aussi ; fin de l'entrée : non) ; -j sans dossiers ; -d DOSSIER ;
 * -q silencieux ; -h l'aide (code 0) ; -P refusé. NOM : un nom, un motif (* ?) ou un dossier ; -x : les
 * noms laissés. ARCHIVE illisible et sans point : ARCHIVE.zip. Code : 0 bien, 1 un avertissement
 * (fichiers sautés, rien ne correspond), 2 une erreur, 3 une mauvaise ligne de commande.
 *
 * Comme l'asm : après ARCHIVE, seuls « -x » et « -d DOSSIER » sont des options (les autres mots en
 * « - » sont des noms) ; si ARCHIVE.zip ne s'ouvre pas non plus, l'erreur dite est celle d'ARCHIVE ;
 * un nom qui sort du dossier (« .. ») est sauté avec un message, et compte comme sauté. */
#include "korinrt.h"
#include "archivekit_import.h"
#include "../zip/zipcli.h"

static int mode = 'x', ow, junk, quiet; /* ow : 0 demander, 1 remplacer, 2 jamais */
static const char *dest, *path;
static int inexcl;
static char **pats, **excl;
static long np, nx, rc, files, skipped;
static long arc, count; /* l'archive, ses entrées */
static u8 *sel;         /* les entrées choisies */
static char pbuf[1040];

static const char M_USAGE[] =
	"unzip -- extrait, liste, essaie des archives ZIP\n"
	"usage : unzip [-l|-v|-t|-p] [-o|-n] [-j] [-q] [-d DOSSIER] ARCHIVE [NOM...] [-x NOM...]\n"
	"  -l liste  -v liste détaillée  -t essai  -p vers la sortie\n"
	"  -o remplace les fichiers  -n ne remplace jamais  -j sans dossiers  -d DOSSIER où  -q silencieux\n";

static void usage(void)
{
	out_say(M_USAGE);
}

NORETURN static void finish(int code)
{
	ar_close(arc);
	exit(code);
}

/* -l, -v : la liste */
static void do_list(void)
{
	long info[8];
	u64 size = 0, packed = 0, nfiles = 0;
	out_say("Archive : ");
	out_puts(pbuf);
	out_nl();
	out_say(mode == 'v'
			? "  Taille  Méthode  Compr.  Taux    Date      Heure  CRC-32    Nom\n"
			  "--------  ------  -------- ---- ---------- -----  --------  ---\n"
			: "   Taille     Date      Heure   Nom\n"
			  "---------  ---------- -----   ---\n");
	for (long i = 0; i < count; i++) {
		if (!sel[i])
			continue;
		ar_entry(arc, i, (long)info);
		packed += (u64)info[ARE_PACKED];
		if (!info[ARE_DIR])
			nfiles++;
		size += (u64)info[ARE_SIZE];
		if (mode != 'v') { /* -l : taille, date, nom */
			zc_put_w((u64)info[ARE_SIZE], 9);
			out_puts("  ");
			zc_put_date((u64)info[ARE_TIME]);
			out_puts("   ");
		} else { /* -v : taille, méthode, compressé, taux, date, CRC, nom */
			zc_put_w((u64)info[ARE_SIZE], 8);
			out_puts("  ");
			const char *m = "Stored";
			if (!info[ARE_DIR] && info[ARE_METHOD])
				m = info[ARE_METHOD] == 8 ? "Defl:N" : "?";
			zc_put_pad(m, 6);
			out_putc(' ');
			zc_put_w((u64)info[ARE_PACKED], 8);
			out_putc(' ');
			zc_put_w(zc_pct((u64)info[ARE_SIZE], (u64)info[ARE_PACKED]), 3);
			out_putc('%');
			out_putc(' ');
			zc_put_date((u64)info[ARE_TIME]);
			out_puts("  ");
			zc_hex8((u64)info[ARE_CRC]);
			out_puts("  ");
		}
		out_puts((const char *)info[ARE_NAME]);
		if (info[ARE_DIR])
			out_putc('/');
		out_nl();
	}
	if (mode != 'v') {
		out_puts("---------                     -------\n");
		zc_put_w(size, 9);
		zc_spaces(21);
	} else {
		out_puts("--------          -------- ----                            -------\n");
		zc_put_w(size, 8);
		zc_spaces(9);
		zc_put_w(packed, 8);
		out_putc(' ');
		zc_put_w(zc_pct(size, packed), 3);
		out_putc('%');
		zc_spaces(28);
	}
	out_putn(nfiles);
	out_say(nfiles == 1 ? " fichier" : " fichiers");
	out_nl();
	finish(0);
}

/* -t : l'essai */
static void do_test(void)
{
	long info[8], bad = 0, tested = 0;
	for (long i = 0; i < count; i++) {
		if (!sel[i])
			continue;
		ar_entry(arc, i, (long)info);
		if (info[ARE_DIR])
			continue;
		tested++;
		long r = ar_test(arc, i);
		if (!r && quiet)
			continue;
		zc_spaces(4);
		out_say("essai : ");
		zc_put_pad((const char *)info[ARE_NAME], 40);
		out_putc(' ');
		if (r) {
			bad++;
			out_puts((const char *)ar_strerror(r));
		} else
			out_puts("OK");
		out_nl();
	}
	if (bad) {
		out_say("Au moins une erreur dans ");
		out_puts(pbuf);
		out_say(" (");
		out_putn((u64)bad);
		out_say(" sur ");
		out_putn((u64)tested);
		out_say(" fichiers).\n");
		finish(2);
	}
	out_say("Aucune erreur dans les données compressées de ");
	out_puts(pbuf);
	out_puts(".\n");
	finish(0);
}

/* -p : vers la sortie standard */
static void do_pipe(void)
{
	long info[8];
	out_flush();
	for (long i = 0; i < count; i++) {
		if (!sel[i])
			continue;
		ar_entry(arc, i, (long)info);
		if (info[ARE_DIR])
			continue;
		long b = ar_load(arc, i);
		if (b < 0) {
			zc_err((const char *)info[ARE_NAME], (const char *)ar_strerror(b));
			finish(2);
		}
		const char *d = (const char *)ar_data(b);
		u64 left = (u64)ar_bytes(b);
		while (left) {
			long r = sys_write(1, d, left);
			if (r <= 0)
				break;
			d += r;
			left -= (u64)r;
		}
		ar_free(b);
	}
	finish(0);
}

/* chemin d'un fichier qui existe -> 1 : le remplacer */
static int ask(const char *p)
{
	for (;;) {
		out_say("remplacer ");
		out_puts(p);
		out_say(" ? [o]ui, [n]on, [t]ous, [r]ien : ");
		out_flush();
		char *l = rd_line(&krt_in, 0);
		if (!l)
			return 0;
		switch (l[0]) {
		case 'o':
		case 'y':
			return 1;
		case 'n':
			return 0;
		case 't':
		case 'A':
			ow = 1;
			return 1;
		case 'r':
		case 'N':
			ow = 2;
			return 0;
		}
	}
}

/* l'extraction */
static void do_extract(void)
{
	long info[8];
	if (!quiet) {
		out_say("Archive : ");
		out_puts(pbuf);
		out_nl();
	}
	rd_init(&krt_in, 0); /* les réponses : sur l'entrée standard */
	for (long i = 0; i < count; i++) {
		if (!sel[i])
			continue;
		ar_entry(arc, i, (long)info);
		char *name = (char *)info[ARE_NAME];
		long dir = info[ARE_DIR];
		if (zc_dotdot(name)) { /* « .. » : jamais hors du dossier */
			zc_err(name, "nom refusé (il sort du dossier)");
			skipped++;
			continue;
		}
		if (junk) {
			if (dir) /* -j : pas de dossiers */
				continue;
			name = path_base(name);
		}
		while (*name == '/') /* (sans / en tête) */
			name++;
		if (!*name)
			continue;
		char *out = dest ? zc_join(dest, name) : name;
		long r;
		if (dir) {
			r = ar_extract(arc, i, (long)out);
			if (r >= 0)
				continue;
			goto error;
		}
		if (path_type(out) >= 0 && ow != 1) {
			if (ow == 2 || !ask(out)) {
				skipped++;
				continue;
			}
		}
		r = ar_extract(arc, i, (long)out);
		if (r < 0)
			goto error;
		files++;
		if (!quiet) {
			zc_spaces(2);
			out_say("extrait : ");
			out_puts(out);
			out_nl();
		}
		continue;
error:
		zc_err(out, (const char *)ar_strerror(r));
		rc = 2;
	}
	if (rc == 2)
		finish(2);
	if (!quiet) {
		out_putn((u64)files);
		out_say(files == 1 ? " fichier extrait" : " fichiers extraits");
		if (skipped)
			out_say(", certains sautés");
		out_puts(".\n");
	}
	finish(skipped != 0);
}

int main(int argc, char **argv)
{
	pats = malloc((u64)argc * 8 + 8);
	excl = malloc((u64)argc * 8 + 8);
	for (int i = 1; i < argc;) {
		char *w = argv[i++];
		if (w[0] == '-' && w[1]) {
			if (path) { /* après l'archive : -x, -d */
				if (str_eq(w, "-x")) {
					inexcl = 1;
					continue;
				}
				if (str_eq(w, "-d") && i < argc) {
					dest = argv[i++];
					continue;
				}
				goto word;
			}
			for (const char *o = w + 1; *o; o++) { /* un groupe d'options */
				int c = (u8)*o;
				if (c == 'l' || c == 'v' || c == 't' || c == 'p')
					mode = c;
				else if (c == 'o')
					ow = 1;
				else if (c == 'n')
					ow = 2;
				else if (c == 'j')
					junk = 1;
				else if (c == 'q')
					quiet = 1;
				else if (c == 'd') {
					if (i < argc)
						dest = argv[i++];
					break;
				} else if (c == 'P') {
					zc_err(0, "-P : les archives chiffrées ne sont pas prises en charge");
					return 3;
				} else if (c == 'h') {
					usage();
					return 0;
				} else {
					char opt[3] = {'-', (char)c, 0}; /* « -c : option inconnue » */
					zc_err(opt, "option inconnue");
					usage();
					return 3;
				}
			}
			continue;
		}
word:
		if (!path)
			path = w;
		else if (inexcl)
			excl[nx++] = w;
		else
			pats[np++] = w;
	}
	if (!path) {
		usage();
		return 3;
	}
	/* l'archive (ARCHIVE.zip si ARCHIVE n'a pas de point) */
	strcpy(pbuf, path);
	arc = ar_open((long)pbuf);
	if (arc < 0) {
		long e = arc;
		if (!zc_has_dot(path)) {
			strcat(pbuf, ".zip");
			arc = ar_open((long)pbuf);
		}
		if (arc < 0) {
			zc_err(path, (const char *)ar_strerror(e));
			return 2;
		}
	}
	count = ar_count(arc);
	sel = malloc((u64)count + 1);
	long chosen = 0;
	for (long i = 0; i < count; i++) {
		const char *name = (const char *)ar_name(arc, i);
		int c = zc_chosen(name, pats, np) && !(nx && zc_chosen(name, excl, nx));
		sel[i] = (u8)c;
		chosen += c;
	}
	if (np && !chosen) {
		zc_err(pbuf, "rien ne correspond dans l'archive");
		return 1;
	}
	if (mode == 'l' || mode == 'v')
		do_list();
	if (mode == 't')
		do_test();
	if (mode == 'p')
		do_pipe();
	do_extract();
	return 0;
}
