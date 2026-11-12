/* zip - fait ou change des archives ZIP (sur ArchiveKit), comme zip.asm (d'après zip d'Info-ZIP et
 * celui d'Onyx ; docs/OUTILS.md) :
 *
 *   zip [-r] [-0 .. -9] [-j] [-k] [-q] [-p DOSSIER] ARCHIVE CHEMIN ...    ajoute (ou remplace)
 *   zip -d ARCHIVE NOM ...                                                 retire des entrées
 *
 * -r les dossiers avec ce qu'ils contiennent ; -0 stocke .. -9 (6 par défaut) ; -j sans dossiers ;
 * -k garde un nom que l'archive a déjà ; -p DOSSIER dans ce dossier de l'archive ; -q silencieux ;
 * -h l'aide (code 0). NOM (avec -d) : un nom, un motif (* ?) ou un dossier. Un chemin relatif est
 * rangé tel qu'il est donné ; un chemin avec son volume, d'après sa dernière partie. Une archive qui
 * existe est refaite dans ARCHIVE.tmp, qui prend sa place. ARCHIVE sans point et absente : ARCHIVE.zip.
 * Code : 0 bien, 1 rien de fait, 2 une erreur, 3 une mauvaise ligne de commande (l'aide sur la
 * sortie standard).
 *
 * Comme l'asm : les options ne sont lues qu'avant ARCHIVE (après, « -x » est un chemin) ; « -p » sans
 * mot après est ignoré, et termine son groupe ; un chemin absent : « zip : chemin : introuvable » et
 * la suite continue ; l'archive elle-même n'est jamais ajoutée. */
#include "korinrt.h"
#include "archivekit_import.h"
#include "../zip/zipcli.h"

struct plan {
	const char *name, *path;
	long dir;
};

static int rec, junk, keep, quiet, del;
static long level = 6;
static const char *prefix = "", *path;
static char **items;
static long ni;
static long old;    /* l'archive qui existe (0 : aucune) */
static u8 *sel;     /* -d : les entrées retirées */
static char *normp; /* le nom de l'archive, comme une entrée */
static char *pre;   /* -p, normé */
static struct plan *plan;
static long pn, pcap, skipped;
static char pbuf[1040], tbuf[1048];

static const char M_USAGE[] =
	"zip -- fait ou change des archives ZIP\n"
	"usage : zip [-r] [-0..-9] [-j] [-k] [-q] [-p DOSSIER] ARCHIVE CHEMIN...\n"
	"        zip -d ARCHIVE NOM...\n"
	"  -r avec ce que les dossiers contiennent  -0 stocke .. -9 le mieux  -j sans dossiers\n"
	"  -k garde les noms déjà présents  -p DOSSIER dans ce dossier de l'archive  -d retire  -q silencieux\n";

/* erreur, chemin : « zip : chemin : erreur », code 2 */
NORETURN static void fail(long e, const char *p)
{
	zc_err(p, (const char *)ar_strerror(e));
	exit(2);
}

static void usage(void)
{
	out_say(M_USAGE);
}

/* nom, chemin, 1 si dossier : une entrée du plan (sauf l'archive elle-même, et avec -k un nom que
 * l'archive a déjà) */
static void plan_add(const char *name, const char *p, long dir)
{
	if (!dir && zc_streqi(zc_norm(p), normp))
		return;
	if (keep && old && ar_find(old, (long)name) >= 0) {
		skipped++;
		return;
	}
	if (pn >= pcap) { /* plus de place : le double */
		pcap *= 2;
		struct plan *v = malloc((u64)pcap * sizeof *v);
		mem_move(v, plan, (u64)pn * sizeof *v);
		plan = v;
	}
	plan[pn].name = name;
	plan[pn].path = p;
	plan[pn].dir = dir;
	pn++;
}

static int in_plan(const char *name)
{
	for (long i = 0; i < pn; i++)
		if (zc_streqi(plan[i].name, name))
			return 1;
	return 0;
}

static void walk(const char *p, const char *name);

/* dossier, nom de base dans l'archive : chacun de ses enfants, par walk */
static void walk_children(const char *dir, const char *base)
{
	struct dirent e;
	long fd = sys_open(dir, O_READ);
	if (fd < 0)
		return;
	while (sys_readdir(fd, &e) == 1) {
		if (e.name[0] == '.' && (!e.name[1] || (e.name[1] == '.' && !e.name[2])))
			continue;
		char *full = zc_join(dir, e.name);
		walk(full, zc_join(base, e.name));
	}
	sys_close(fd);
}

/* chemin, nom dans l'archive : au plan (un dossier : avec -r, ce qu'il contient) */
static void walk(const char *p, const char *name)
{
	int t = path_type(p);
	if (t < 0)
		return;
	if (!t) {
		plan_add(name, p, 0);
		return;
	}
	if (!junk)
		plan_add(name, p, 1);
	if (rec)
		walk_children(p, junk ? pre : name);
}

/* L'archive refaite : les anciennes entrées gardées (sans celles que -d retire ou que le plan
 * remplace), puis le plan. Dans ARCHIVE.tmp si elle existait, qui prend ensuite sa place. */
static void rewrite(void)
{
	long info[8], r;
	const char *where;
	strcpy(tbuf, pbuf);
	if (old)
		strcat(tbuf, ".tmp");
	long w = ar_create((long)tbuf, AR_FMT_ZIP);
	if (w < 0)
		fail(w, tbuf);
	if (old) {
		long n = ar_count(old);
		for (long i = 0; i < n; i++) {
			if (del) {
				if (sel[i])
					continue;
			} else if (!keep && in_plan((const char *)ar_name(old, i)))
				continue;
			ar_entry(old, i, (long)info);
			ar_set_time(w, info[ARE_TIME]);
			if (info[ARE_DIR]) {
				r = ar_add_dir(w, info[ARE_NAME]);
				if (r < 0)
					goto werr;
				continue;
			}
			long b = ar_load(old, i);
			if (b < 0) {
				r = b;
				goto werr;
			}
			long data = ar_data(b), bytes = ar_bytes(b);
			r = ar_add_mem(w, info[ARE_NAME], data, bytes, info[ARE_METHOD] ? level : 0);
			ar_free(b);
			if (r < 0)
				goto werr;
		}
	}
	ar_set_time(w, 0); /* (les nouveaux dossiers : l'heure courante) */
	for (long i = 0; i < pn; i++) {
		if (plan[i].dir)
			r = ar_add_dir(w, (long)plan[i].name);
		else
			r = ar_add_file(w, (long)plan[i].name, (long)plan[i].path, level);
		if (r < 0) {
			where = plan[i].path; /* (un fichier du plan) */
			goto abandon;
		}
	}
	r = ar_finish(w);
	if (r < 0)
		goto werr;
	ar_free(w);
	if (!old)
		return;
	ar_close(old); /* la copie prend la place de l'archive */
	old = 0;
	sys_unlink(pbuf);
	if (vfs_rename(tbuf, pbuf) < 0) {
		zc_err(tbuf, "impossible de remplacer l'archive : la nouvelle est ici");
		exit(2);
	}
	return;
werr:
	where = pbuf; /* (l'ancienne archive, ou l'écriture) */
abandon:
	ar_free(w);
	sys_unlink(tbuf);
	fail(r, where);
}

/* -d : retirer des entrées */
static int do_delete(void)
{
	long info[8];
	long n = ar_count(old), removed = 0;
	sel = malloc((u64)n + 1);
	for (long i = 0; i < n; i++) {
		ar_entry(old, i, (long)info);
		int c = zc_chosen((const char *)info[ARE_NAME], items, ni);
		sel[i] = (u8)c;
		if (!c)
			continue;
		removed++;
		if (quiet)
			continue;
		out_say("suppression : ");
		out_puts((const char *)info[ARE_NAME]);
		if (info[ARE_DIR])
			out_putc('/');
		out_nl();
	}
	if (!removed) {
		zc_err(pbuf, "rien ne correspond dans l'archive");
		return 1;
	}
	rewrite();
	if (!quiet) {
		out_putn((u64)removed);
		out_say(removed == 1 ? " entrée supprimée, " : " entrées supprimées, ");
		u64 left = (u64)(n - removed);
		out_putn(left);
		out_say(left <= 1 ? " restante.\n" : " restantes.\n");
	}
	return 0;
}

static int do_add(void)
{
	pcap = 64;
	plan = malloc(64 * sizeof *plan);
	pre = zc_norm(prefix);
	for (long k = 0; k < ni; k++) {
		const char *p = items[k];
		int t = path_type(p);
		if (t < 0) {
			zc_err(p, "introuvable");
			continue;
		}
		int vol = 0; /* avec son volume (« SD0:/... ») */
		for (int i = 0; i < 6 && p[i]; i++)
			if (p[i] == ':') {
				vol = 1;
				break;
			}
		char *nm = zc_norm(p);
		if (!nm[0]) {
			if (t) /* « . », « SD0:/ » : ce qu'il contient */
				walk_children(p, pre);
			continue;
		}
		walk(p, zc_join(pre, (junk || vol) ? path_base(nm) : nm));
	}
	if (!pn) {
		zc_err(pbuf, skipped ? "rien à faire (les noms existent : -k)" : "rien à faire");
		return 1;
	}
	if (!quiet) /* ce qui sera fait */
		for (long i = 0; i < pn; i++) {
			const char *m = "ajout : ";
			if (old && ar_find(old, (long)plan[i].name) >= 0)
				m = "mise à jour : ";
			zc_spaces(2);
			out_say(m);
			out_puts(plan[i].name);
			if (plan[i].dir)
				out_putc('/');
			out_nl();
		}
	rewrite();
	if (quiet)
		return 0;
	long a = ar_open((long)pbuf); /* le bilan, relu dans l'archive */
	if (a < 0)
		return 0;
	long info[8], n = ar_count(a);
	u64 size = 0, packed = 0;
	for (long i = 0; i < n; i++) {
		ar_entry(a, i, (long)info);
		size += (u64)info[ARE_SIZE];
		packed += (u64)info[ARE_PACKED];
	}
	ar_close(a);
	out_puts(pbuf);
	out_say(" : ");
	out_putn((u64)n);
	out_say(" entrées, ");
	zc_put_size(size);
	out_puts(" -> ");
	zc_put_size(packed);
	out_puts(" (");
	out_putn(zc_pct(size, packed));
	out_say(" % gagnés).\n");
	return 0;
}

int main(int argc, char **argv)
{
	items = malloc((u64)argc * 8 + 8);
	for (int i = 1; i < argc;) {
		char *w = argv[i++];
		if (w[0] == '-' && w[1] && !path) {
			for (const char *o = w + 1; *o; o++) { /* un groupe d'options */
				int c = (u8)*o;
				if (c == 'r')
					rec = 1;
				else if (c == 'j')
					junk = 1;
				else if (c == 'k')
					keep = 1;
				else if (c == 'q')
					quiet = 1;
				else if (c == 'd')
					del = 1;
				else if (c == 'p') {
					if (i < argc)
						prefix = argv[i++];
					break;
				} else if (c == 'h') {
					usage();
					return 0;
				} else if ((unsigned)(c - '0') <= 9)
					level = c - '0';
				else {
					char opt[3] = {'-', (char)c, 0}; /* « -c : option inconnue » */
					zc_err(opt, "option inconnue");
					usage();
					return 3;
				}
			}
			continue;
		}
		if (!path)
			path = w;
		else
			items[ni++] = w;
	}
	if (!path || !ni) {
		usage();
		return 3;
	}
	/* l'archive : ARCHIVE.zip si elle n'a pas d'extension et n'existe pas */
	strcpy(pbuf, path);
	if (!zc_has_dot(path_base(pbuf)) && path_type(pbuf) < 0)
		strcat(pbuf, ".zip");
	normp = zc_norm(pbuf);
	if (path_type(pbuf) >= 0) {
		old = ar_open((long)pbuf);
		if (old < 0)
			fail(old, pbuf);
		if (ar_format(old) != AR_FMT_ZIP) {
			zc_err(pbuf, "seul le format ZIP s'écrit ici");
			return 2;
		}
	} else if (del) {
		zc_err(pbuf, "pas d'archive de ce nom");
		return 2;
	}
	return del ? do_delete() : do_add();
}
