/*
 * walk.c -- rg (docs/RG.md) : le parcours des dossiers, comme le crate ignore de ripgrep 14.1 (walk.rs,
 * dir.rs) : surcharges -g/--iglob d'abord, puis .rgignore > .ignore > .gitignore > .git/info/exclude >
 * exclusions globales (le plus profond gagne ; .gitignore seulement dans un depot git, et pas au-dessus
 * de sa racine), puis les types (-t/-T), puis les fichiers caches. Les fichiers d'exclusion des dossiers
 * au-dessus d'une racine comptent aussi.
 * Iteratif, un seul dossier ouvert a la fois (64 fichiers par process sur Korin OS) ; les noms d'un
 * dossier sont tries (comme --sort path : la sortie ne depend pas de l'ordre du systeme de fichiers).
 */
#include "rg.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* ---- surcharges (-g, --iglob) ---- */
static struct gi s_ov;
static int s_ov_on, s_ov_nwl;

int overrides_init (void)
{
	if (!O.nglobs)
		return 0;
	char cwd[4096];
	if (!getcwd (cwd, sizeof cwd))
		strcpy (cwd, ".");
	gi_init (&s_ov, cwd);
	s_ov_on = 1;
	/* les -g d'abord, puis les --iglob (comme OverrideBuilder de ripgrep) */
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < O.nglobs; i++)
		{
			int ic = O.globs_icase[i] || O.glob_icase;
			if ((pass == 0) != (O.globs_icase[i] == 0))
				continue;
			s_ov.icase = ic;
			const char *e = gi_add_line (&s_ov, O.globs[i], strlen (O.globs[i]), 0);
			if (e)
			{
				rg_err ("error parsing glob '%s': %s", O.globs[i], e);
				return 2;
			}
			const char *g = O.globs[i];
			if (!(g[0] == '!'))
				s_ov_nwl++;
		}
	return 0;
}

static int ov_matched (const char *path, int is_dir)
{
	int m = gi_matched (&s_ov, path, is_dir);
	if (m == M_IGNORE)
		return M_WHITELIST;
	if (m == M_WHITELIST)
		return M_IGNORE;
	if (s_ov_nwl > 0 && !is_dir)
		return M_IGNORE;
	return M_NONE;
}

/* ---- types ---- */
struct tglob
{
	struct glob_re g;
	int sel;
};
static struct tglob *s_tg;
static int s_ntg, s_types_on, s_has_selected;
static int *s_sel_neg;

struct tdef
{
	char *name;
	char **globs;
	int n;
};
static struct tdef *s_defs;
static int s_ndefs;

static struct tdef *def_find (const char *name)
{
	for (int i = 0; i < s_ndefs; i++)
		if (strcmp (s_defs[i].name, name) == 0)
			return &s_defs[i];
	return 0;
}

static struct tdef *def_get (const char *name)
{
	struct tdef *d = def_find (name);
	if (d)
		return d;
	s_defs = xrealloc (s_defs, (s_ndefs + 1) * sizeof *s_defs);
	d = &s_defs[s_ndefs++];
	d->name = xstrdup (name);
	d->globs = 0;
	d->n = 0;
	return d;
}

static void def_add (struct tdef *d, const char *g)
{
	d->globs = xrealloc (d->globs, (d->n + 1) * sizeof *d->globs);
	d->globs[d->n++] = xstrdup (g);
}

static int cmp_def (const void *a, const void *b)
{
	return strcmp (((const struct tdef *) a)->name, ((const struct tdef *) b)->name);
}

static int cmp_str (const void *a, const void *b)
{
	return strcmp (*(char *const *) a, *(char *const *) b);
}

static int defs_load (void)
{
	for (const struct rg_type *t = rg_default_types; t->name; t++)
	{
		struct tdef *d = def_get (t->name);
		for (const char *g = t->globs; *g; g += strlen (g) + 1)
			def_add (d, g);
	}
	for (int i = 0; i < O.ntype_clears; i++)
	{
		struct tdef *d = def_find (O.type_clears[i]);
		if (d)
			d->n = 0;
	}
	for (int i = 0; i < O.ntype_adds; i++)
	{
		const char *a = O.type_adds[i];
		const char *c = strchr (a, ':');
		if (!c || c == a)
		{
			rg_err ("error parsing flag --type-add: invalid definition (format is type:glob, e.g., html:*.html)");
			return 2;
		}
		char *name = xstrndup (a, c - a);
		const char *rest = c + 1;
		if (strncmp (rest, "include:", 8) == 0)
		{
			char *list = xstrdup (rest + 8), *sv = 0;
			for (char *t = strtok_r (list, ",", &sv); t; t = strtok_r (0, ",", &sv))
			{
				struct tdef *src = def_find (t);
				if (!src)
				{
					rg_err ("unrecognized file type: %s", t);
					return 2;
				}
				int n = src->n;
				char **gl = src->globs;
				struct tdef *d = def_get (name);
				src = def_find (t);
				gl = src->globs;
				n = src->n;
				for (int j = 0; j < n; j++)
					def_add (d, gl[j]);
			}
			free (list);
		}
		else
			def_add (def_get (name), rest);
		free (name);
	}
	qsort (s_defs, s_ndefs, sizeof *s_defs, cmp_def);
	for (int i = 0; i < s_ndefs; i++)
		qsort (s_defs[i].globs, s_defs[i].n, sizeof (char *), cmp_str);
	return 0;
}

int types_init (void)
{
	if (O.mode == MODE_TYPE_LIST)
	{
		if (defs_load ())
			return 2;
		sbuf *b = &g_out;
		for (int i = 0; i < s_ndefs; i++)
		{
			if (!s_defs[i].n)
				continue;
			sb_puts (b, s_defs[i].name);
			sb_puts (b, ": ");
			for (int j = 0; j < s_defs[i].n; j++)
			{
				if (j)
					sb_puts (b, ", ");
				sb_puts (b, s_defs[i].globs[j]);
			}
			sb_putc (b, '\n');
		}
		return 0;
	}
	if (!O.ntypes)
		return 0;
	if (defs_load ())
		return 2;
	/* les selections, dans l'ordre ; « all » : tous les types */
	int nsel = 0;
	for (int i = 0; i < O.ntypes; i++)
	{
		const char *name = O.types[i];
		int lo = 0, hi = 0;
		if (strcmp (name, "all") == 0)
		{
			lo = 0;
			hi = s_ndefs;
		}
		else
		{
			struct tdef *d = def_find (name);
			if (!d || !d->n)
			{
				rg_err ("unrecognized file type: %s", name);
				return 2;
			}
			lo = d - s_defs;
			hi = lo + 1;
		}
		for (int k = lo; k < hi; k++)
		{
			s_sel_neg = xrealloc (s_sel_neg, (nsel + 1) * sizeof *s_sel_neg);
			s_sel_neg[nsel] = O.types_neg[i];
			if (!O.types_neg[i])
				s_has_selected = 1;
			for (int j = 0; j < s_defs[k].n; j++)
			{
				s_tg = xrealloc (s_tg, (s_ntg + 1) * sizeof *s_tg);
				const char *e = glob_compile (s_defs[k].globs[j], 1, 0, 0, &s_tg[s_ntg].g);
				if (e)
				{
					rg_err ("error parsing glob '%s': %s", s_defs[k].globs[j], e);
					return 2;
				}
				s_tg[s_ntg].sel = nsel;
				s_ntg++;
			}
			nsel++;
		}
	}
	s_types_on = 1;
	return 0;
}

static int types_matched (const char *name, int is_dir)
{
	if (is_dir || !s_ntg)
		return M_NONE;
	size_t n = strlen (name);
	for (int i = s_ntg - 1; i >= 0; i--)
		if (glob_match (&s_tg[i].g, name, n))
			return s_sel_neg[s_tg[i].sel] ? M_IGNORE : M_WHITELIST;
	return s_has_selected ? M_IGNORE : M_NONE;
}

/* ---- fichiers d'exclusion ---- */
struct igdir
{
	char *path;			/* le dossier, tel qu'il s'affiche (ou absolu pour un parent) */
	struct gi rgi, ign, git, excl;
	int has_git, is_parent, loaded;
};

static struct gi s_global, s_explicit;
static int s_global_on, s_explicit_on;
static int s_ignore_any;		/* quelque regle d'exclusion chargee */

static char *path_join (const char *dir, const char *name)
{
	size_t a = strlen (dir), b = strlen (name);
	char *p = xmalloc (a + b + 2);
	memcpy (p, dir, a);
	size_t k = a;
	if (a && dir[a - 1] != '/')
		p[k++] = '/';
	memcpy (p + k, name, b + 1);
	return p;
}

static int path_exists (const char *p)
{
	struct stat st;
	return stat (p, &st) == 0;
}

static void igdir_load (struct igdir *d, const char *path, int is_parent)
{
	memset (d, 0, sizeof *d);
	d->path = xstrdup (path);
	d->is_parent = is_parent;
	gi_init (&d->rgi, path);
	gi_init (&d->ign, path);
	gi_init (&d->git, path);
	gi_init (&d->excl, path);
	if (O.no_ignore)
		return;
	char *g = path_join (path, ".git");
	d->has_git = path_exists (g);
	d->rgi.icase = d->ign.icase = d->git.icase = d->excl.icase = O.ignore_file_icase;
	if (!O.no_ignore_dot)
	{
		char *f = path_join (path, ".rgignore");
		gi_add_file (&d->rgi, f);
		free (f);
		f = path_join (path, ".ignore");
		gi_add_file (&d->ign, f);
		free (f);
	}
	if (!O.no_ignore_vcs)
	{
		char *f = path_join (path, ".gitignore");
		gi_add_file (&d->git, f);
		free (f);
		if (!O.no_ignore_exclude && d->has_git)
		{
			f = path_join (g, "info/exclude");
			gi_add_file (&d->excl, f);
			free (f);
		}
	}
	free (g);
	if (d->rgi.n || d->ign.n || d->git.n || d->excl.n)
		s_ignore_any = 1;
}

static void igdir_free (struct igdir *d)
{
	free (d->path);
	gi_free (&d->rgi);
	gi_free (&d->ign);
	gi_free (&d->git);
	gi_free (&d->excl);
}

static void global_init (void)
{
	static int done;
	if (done)
		return;
	done = 1;
	char cwd[4096];
	if (!getcwd (cwd, sizeof cwd))
		strcpy (cwd, ".");
	if (!O.no_ignore && !O.no_ignore_vcs && !O.no_ignore_global)
	{
		/* core.excludesFile par defaut : $XDG_CONFIG_HOME/git/ignore, sinon $HOME/.config/git/ignore */
		gi_init (&s_global, cwd);
		const char *x = getenv ("XDG_CONFIG_HOME"), *h = getenv ("HOME");
		char *f = 0;
		if (x && *x)
			f = path_join (x, "git/ignore");
		else if (h && *h)
			f = path_join (h, ".config/git/ignore");
		if (f && gi_add_file (&s_global, f) && s_global.n)
		{
			s_global_on = 1;
			s_ignore_any = 1;
		}
		free (f);
	}
	if (!O.no_ignore && !O.no_ignore_files && O.nignore_files)
	{
		gi_init (&s_explicit, cwd);
		for (int i = 0; i < O.nignore_files; i++)
			if (!gi_add_file (&s_explicit, O.ignore_files[i]))
				rg_err ("%s: No such file or directory (os error 2)", O.ignore_files[i]);
		if (s_explicit.n)
		{
			s_explicit_on = 1;
			s_ignore_any = 1;
		}
	}
}

/* les dossiers d'exclusion : la pile du parcours (racine .. dossier courant), puis les parents de la racine */
static struct igdir **s_stack;
static int s_nstack, s_capstack;
static struct igdir *s_parents;
static int s_nparents;
static char *s_absroot;
static size_t s_rootlen;		/* longueur du chemin affiche de la racine */

static int matched_ignore (const char *path, int is_dir, const char *abs)
{
	int any_git = O.no_require_git;
	for (int i = 0; !any_git && i < s_nstack; i++)
		any_git = s_stack[i]->has_git;
	for (int i = 0; !any_git && i < s_nparents; i++)
		any_git = s_parents[i].has_git;
	int mc = M_NONE, mi = M_NONE, mg = M_NONE, me = M_NONE, saw_git = 0;
	for (int k = s_nstack + s_nparents - 1; k >= 0; k--)
	{
		struct igdir *d;
		const char *p;
		if (k >= s_nparents)
		{
			d = s_stack[k - s_nparents];
			p = path;
		}
		else
		{
			d = &s_parents[s_nparents - 1 - k];	/* du plus proche au plus lointain */
			p = abs;
		}
		if (!mc)
			mc = gi_matched (&d->rgi, p, is_dir);
		if (!mi)
			mi = gi_matched (&d->ign, p, is_dir);
		if (any_git && !saw_git && !mg)
			mg = gi_matched (&d->git, p, is_dir);
		if (any_git && !saw_git && !me)
			me = gi_matched (&d->excl, p, is_dir);
		saw_git = saw_git || d->has_git;
	}
	if (mc) return mc;
	if (mi) return mi;
	if (mg) return mg;
	if (me) return me;
	if (any_git && s_global_on)
	{
		int m = gi_matched (&s_global, path, is_dir);
		if (m)
			return m;
	}
	if (s_explicit_on)
		return gi_matched (&s_explicit, path, is_dir);
	return M_NONE;
}

/* Path::join de Rust : un chemin absolu remplace la base */
static char *rs_join (const char *a, const char *b)
{
	if (b[0] == '/')
		return xstrdup (b);
	size_t n = strlen (a), m = strlen (b);
	char *p = xmalloc (n + m + 2);
	memcpy (p, a, n);
	if (n && a[n - 1] != '/')
		p[n++] = '/';
	memcpy (p + n, b, m + 1);
	return p;
}

/* dirpath : le dossier qui contient l'entree (tel que parcouru) */
static int entry_skipped (const char *path, const char *name, int is_dir, const char *dirpath)
{
	if (path[0] == '.' && path[1] == '/')
		path += 2;		/* (Ignore::matched ote un « ./ » de tete) */
	if (s_ov_on)
	{
		int m = ov_matched (path, is_dir);
		if (m != M_NONE)
			return m == M_IGNORE;
	}
	int wl = M_NONE;
	if (s_ignore_any)
	{
		char *abs = 0;
		if (s_nparents)
		{
			/* comme dir.rs de ripgrep 14.1 : le chemin relatif au dossier de l'entree, joint au chemin absolu
			   de la racine (et non le chemin depuis la racine : un defaut de ripgrep, reproduit) */
			const char *pre = dirpath;
			if (pre[0] == '.' && pre[1] == '/')
				pre += 2;
			size_t pl = strlen (pre);
			if (strncmp (path, pre, pl) == 0)
			{
				const char *rest = path + pl;
				if (*rest == '/')
					rest++;
				abs = rs_join (s_absroot, rest);
			}
			else
				abs = rs_join (s_absroot, path);
		}
		int m = matched_ignore (path, is_dir, abs);
		free (abs);
		if (m == M_IGNORE)
			return 1;
		if (m == M_WHITELIST)
			wl = m;
	}
	if (s_types_on)
	{
		int m = types_matched (name, is_dir);
		if (m == M_IGNORE)
			return 1;
		if (m == M_WHITELIST)
			wl = m;
	}
	if (wl == M_NONE && !O.hidden && name[0] == '.')
		return 1;
	return 0;
}

/* ---- le parcours ---- */
struct ent
{
	char *name;
	unsigned char type;		/* DT_* */
};

struct frame
{
	char *dir;
	struct ent *v;
	int n, i;
	int depth;
	struct igdir ig;
};

static int cmp_ent (const void *a, const void *b)
{
	return strcmp (((const struct ent *) a)->name, ((const struct ent *) b)->name);
}

static int read_dir (const char *dir, struct ent **out, int *nout)
{
	DIR *d = opendir (dir);
	if (!d)
		return -1;
	struct ent *v = 0;
	int n = 0, cap = 0;
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		if (strcmp (e->d_name, ".") == 0 || strcmp (e->d_name, "..") == 0)
			continue;
		if (n == cap)
		{
			cap = cap ? cap * 2 : 32;
			v = xrealloc (v, cap * sizeof *v);
		}
		v[n].name = xstrdup (e->d_name);
#ifdef DT_UNKNOWN
		v[n].type = e->d_type;
#else
		v[n].type = 0;
#endif
		n++;
	}
	closedir (d);
	qsort (v, n, sizeof *v, cmp_ent);
	if (O.sort == SORT_PATH && O.sort_reverse)
		for (int i = 0, j = n - 1; i < j; i++, j--)
		{
			struct ent t = v[i];
			v[i] = v[j];
			v[j] = t;
		}
	*out = v;
	*nout = n;
	return 0;
}

/* les fichiers trouves, pour --sort modified/accessed/created (tries a la fin) */
struct found
{
	char *path;
	long long key;
	long seq;
	int explicit;
};
static struct found *s_found;
static long s_nfound, s_capfound;
static int s_matched;
int g_strip_dot;

static void handle_file (const char *path, int explicit, const struct stat *st)
{
	if (O.sort > SORT_PATH)
	{
		if (s_nfound == s_capfound)
		{
			s_capfound = s_capfound ? s_capfound * 2 : 256;
			s_found = xrealloc (s_found, s_capfound * sizeof *s_found);
		}
		struct found *f = &s_found[s_nfound];
		f->path = xstrdup (g_strip_dot && path[0] == '.' && path[1] == '/' ? path + 2 : path);
		f->seq = s_nfound;
		f->explicit = explicit;
		f->key = 0;
		struct stat st2;
		if (!st && stat (path, &st2) == 0)
			st = &st2;
		if (st)
		{
			if (O.sort == SORT_MODIFIED)
				f->key = (long long) st->st_mtime;
			else if (O.sort == SORT_ACCESSED)
				f->key = (long long) st->st_atime;
			else
				f->key = (long long) st->st_ctime;
#if defined(__linux__)
			if (O.sort == SORT_MODIFIED)
				f->key = f->key * 1000000000LL + st->st_mtim.tv_nsec;
			else if (O.sort == SORT_ACCESSED)
				f->key = f->key * 1000000000LL + st->st_atim.tv_nsec;
			else
				f->key = f->key * 1000000000LL + st->st_ctim.tv_nsec;
#endif
		}
		s_nfound++;
		return;
	}
	struct target t = { path, explicit, 0 };
	if (g_strip_dot && path[0] == '.' && path[1] == '/')
		t.path = path + 2;
	if (search_file (&t))
		s_matched = 1;
}

static int cmp_found (const void *a, const void *b)
{
	const struct found *x = a, *y = b;
	if (x->key != y->key)
		return (x->key < y->key) ? (O.sort_reverse ? 1 : -1) : (O.sort_reverse ? -1 : 1);
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* les parents deja vus (cache de ripgrep : un parent garde la base absolue de la premiere racine qui l'a
   charge, et une racine suivante herite de celle de son parent immediat) */
struct pcache
{
	char *dir, *base;
};
static struct pcache *s_pc;
static int s_npc;

static const char *pc_get (const char *dir)
{
	for (int i = 0; i < s_npc; i++)
		if (strcmp (s_pc[i].dir, dir) == 0)
			return s_pc[i].base;
	return 0;
}

static void parents_load (const char *root)
{
	s_nparents = 0;
	free (s_absroot);
	s_absroot = 0;
	if (O.no_ignore || O.no_ignore_parent)
		return;
	char *abs = realpath (*root ? root : ".", 0);
	if (!abs)
		return;
	/* les ancetres, du plus proche au plus lointain */
	char *p = xstrdup (abs);
	for (;;)
	{
		size_t n = strlen (p);
		while (n > 1 && p[n - 1] == '/')
			p[--n] = 0;
		char *sl = strrchr (p, '/');
		if (!sl)
			break;
		if (sl == p)
		{
			if (n == 1)
				break;
			p[1] = 0;		/* « / » */
		}
		else if (sl[-1] == ':' && sl[1] == 0)
			break;			/* « SD0:/ » deja fait */
		else if (sl[-1] == ':')
			sl[1] = 0;		/* « SD0:/ » */
		else
			*sl = 0;
		s_parents = xrealloc (s_parents, (s_nparents + 1) * sizeof *s_parents);
		igdir_load (&s_parents[s_nparents], p, 1);
		if (s_nparents == 0)
		{
			const char *b = pc_get (p);
			s_absroot = xstrdup (b ? b : abs);
		}
		if (!pc_get (p))
		{
			s_pc = xrealloc (s_pc, (s_npc + 1) * sizeof *s_pc);
			s_pc[s_npc].dir = xstrdup (p);
			s_pc[s_npc].base = xstrdup (abs);
			s_npc++;
		}
		s_nparents++;
		if (strcmp (p, "/") == 0 || (strlen (p) >= 2 && p[strlen (p) - 1] == '/' && p[strlen (p) - 2] == ':'))
			break;
	}
	free (p);
	if (!s_absroot)
		s_absroot = xstrdup (abs);
	free (abs);
}

static void parents_free (void)
{
	for (int i = 0; i < s_nparents; i++)
		igdir_free (&s_parents[i]);
	s_nparents = 0;
}

static void walk_dir (const char *root)
{
	parents_load (root);
	s_rootlen = strlen (root);
	struct frame *fr = 0;
	int nfr = 0, capfr = 0;
	struct ent *v;
	int n;
	if (read_dir (*root ? root : ".", &v, &n) < 0)
	{
		rg_err ("%s: %s (os error %d)", root, strerror (errno), errno);
		g_errored = 1;
		parents_free ();
		return;
	}
	capfr = 16;
	fr = xmalloc (capfr * sizeof *fr);
	fr[0].dir = xstrdup (root);
	fr[0].v = v;
	fr[0].n = n;
	fr[0].i = 0;
	fr[0].depth = 0;
	igdir_load (&fr[0].ig, root, 0);
	nfr = 1;
	s_nstack = 0;
	for (;;)
	{
		if (g_quit_now)
			break;
		struct frame *f = &fr[nfr - 1];
		if (f->i >= f->n)
		{
			for (int j = 0; j < f->n; j++)
				free (f->v[j].name);
			free (f->v);
			free (f->dir);
			igdir_free (&f->ig);
			nfr--;
			if (!nfr)
				break;
			continue;
		}
		/* la pile d'exclusion : les dossiers ouverts */
		if (s_capstack < nfr)
		{
			s_capstack = nfr + 16;
			s_stack = xrealloc (s_stack, s_capstack * sizeof *s_stack);
		}
		for (int j = 0; j < nfr; j++)
			s_stack[j] = &fr[j].ig;
		s_nstack = nfr;
		struct ent *e = &f->v[f->i++];
		int depth = f->depth + 1;
		if (O.max_depth >= 0 && depth > O.max_depth)
			continue;
		char *path = path_join (f->dir, e->name);
		struct stat st;
		int have_st = 0, is_dir = 0, is_reg = 0;
		unsigned char ty = e->type;
		if (ty == DT_UNKNOWN || ty == DT_LNK)
		{
			if (lstat (path, &st) < 0)
			{
				free (path);
				continue;
			}
			have_st = 1;
			if (S_ISLNK (st.st_mode))
			{
				if (!O.follow || stat (path, &st) < 0)
				{
					free (path);
					continue;
				}
			}
			is_dir = S_ISDIR (st.st_mode);
			is_reg = S_ISREG (st.st_mode);
		}
		else
		{
			is_dir = ty == DT_DIR;
			is_reg = ty == DT_REG;
		}
		if (entry_skipped (path, e->name, is_dir, f->dir))
		{
			free (path);
			continue;
		}
		if (is_dir)
		{
			struct ent *cv;
			int cn;
			if (read_dir (path, &cv, &cn) < 0)
			{
				rg_err ("%s: %s (os error %d)", path, strerror (errno), errno);
				g_errored = 1;
				free (path);
				continue;
			}
			if (nfr == capfr)
			{
				capfr *= 2;
				fr = xrealloc (fr, capfr * sizeof *fr);
			}
			struct frame *c = &fr[nfr++];
			c->dir = path;
			c->v = cv;
			c->n = cn;
			c->i = 0;
			c->depth = depth;
			igdir_load (&c->ig, path, 0);
			continue;
		}
		if (!is_reg)
		{
			free (path);
			continue;
		}
		if (O.max_filesize >= 0)
		{
			if (!have_st && stat (path, &st) == 0)
				have_st = 1;
			if (have_st && (long long) st.st_size > O.max_filesize)
			{
				free (path);
				continue;
			}
		}
		handle_file (path, 0, have_st ? &st : 0);
		free (path);
	}
	free (fr);
	s_nstack = 0;
	parents_free ();
}

int walk_all (void)
{
	global_init ();
	for (int i = 0; i < O.npaths && !g_quit_now; i++)
	{
		const char *p = O.paths[i];
		if (strcmp (p, "-") == 0)
		{
			struct target t = { "<stdin>", 1, 1 };
			if (O.mode == MODE_FILES)
				continue;
			if (search_file (&t))
				s_matched = 1;
			continue;
		}
		struct stat st;
		if (stat (*p ? p : ".", &st) < 0)
		{
			int e = errno;
			if (lstat (p, &st) == 0)
				continue;		/* lien casse */
			rg_err ("%s: %s (os error %d)", p, strerror (e), e);
			g_errored = 1;
			continue;
		}
		if (S_ISDIR (st.st_mode))
		{
			if (O.max_depth == 0)
				continue;
			walk_dir (p);
		}
		else
			handle_file (p, 1, &st);
	}
	if (O.sort > SORT_PATH)
	{
		qsort (s_found, s_nfound, sizeof *s_found, cmp_found);
		for (long i = 0; i < s_nfound && !g_quit_now; i++)
		{
			struct target t = { s_found[i].path, s_found[i].explicit, 0 };
			if (search_file (&t))
				s_matched = 1;
		}
	}
	return s_matched;
}
