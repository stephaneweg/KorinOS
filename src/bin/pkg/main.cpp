//
// pkg -- les paquets d'ARMPI depuis le terminal (docs/PAQUETS.md, port de user/BinUtils/pkg.cpp d'Onyx) :
//
//   pkg list [-a] [filtre]       les paquets installes (-a : tout le depot)
//   pkg info <nom>               un paquet : ses versions, sa taille, ses besoins, son mode
//   pkg add <nom>...             installer (et ce qu'il lui faut) ; deja la : dit, rien de fait
//   pkg add --force <nom>...     le reinstaller meme s'il est la, meme version (fichiers abimes ou effaces ;
//                                aussi s'il tourne : le fermer apres)
//   pkg add <fichier.opk>        installer un fichier de paquet (hors depot)
//   pkg delete [-p] <nom>...     retirer (-p : les reglages changes aussi)
//   pkg update <nom>... | -a     mettre a jour ceux-la (-a : tous, sauf ceux en mode never)
//   pkg upgrade                  un tour de pkgd : les paquets auto mis a jour, les autres listes
//   pkg check                    relire l'index : les mises a jour disponibles
//   pkg mode <nom> manual|auto|never
//   pkg commit                   les paquets etages mis en place (init2, au demarrage) ; redemarre si le
//                                noyau, les modules ou le firmware ont change
//   -r <depot>                   un autre depot (URL ou dossier) pour cette commande
//
// Code de sortie : 0 fait, 1 rien a faire ou avertissement, 2 erreur, 3 mauvaise ligne de commande.
//
#include "pkglib.h"
#include <signal.h>

using namespace pkg;

struct Out : Report
{
	char *log = 0; int logN = 0;			// (commit : ce qu'il a dit, pour /var/pkg/commit.log)
	void say (const char *s) override
	{
		printf ("%s\n", s); fflush (stdout);
		if (log) { int l = (int) strlen (s); if (logN + l + 2 < 4096) { memcpy (log + logN, s, l); logN += l; log[logN++] = '\n'; } }
	}
};

static Manager *M;
static Out R;

static int need_index (void)
{
	int rc = M->refresh (R);
	if (rc != OK && !M->haveIndex) return rc;
	return OK;
}
static void pad (const char *s, int w) { int n = printf ("%s", s); while (n++ < w) putchar (' '); }

static int cmd_list (int ac, char **av)
{
	bool all = false; const char *filter = 0;
	for (int i = 0; i < ac; i++) { if (eq (av[i], "-a")) all = true; else filter = av[i]; }
	if (all) { if (need_index () != OK) return 2; } else M->load_cached_quiet ();
	int shown = 0;
	if (all)
		for (int i = 0; i < M->index.n; i++)
		{
			const Pkg &p = M->index.p[i];
			if (*p.part) continue;					// (une partie : avec son paquet)
			if (filter && !strstr (p.name, filter) && !strstr (p.title, filter) && !strstr (p.category, filter)) continue;
			Inst *in = M->db.find (p.name);
			pad (p.name, 18); pad (p.version, 13);
			if (!in) printf ("-          %s\n", p.title);
			else if (M->update_for (*in)) printf (TR ("%-10s %s (installé : %s)\n"), TR ("plus récent"), p.title, in->version ());
			else printf ("%-10s %s\n", TR ("installé"), p.title);
			shown++;
		}
	else
		for (int i = 0; i < M->db.n; i++)
		{
			Inst &in = *M->db.v[i];
			if (*M->part_of (in.name) || (filter && !strstr (in.name, filter))) continue;
			pad (in.name, 18); pad (in.version (), 13); pad (in.mode (), 8);
			const Pkg *u = M->haveIndex ? M->update_for (in) : 0;
			if (M->staged (in.name)) fputs (TR (" (étagé : redémarrer pour finir)"), stdout);
			else if (u) printf (TR (" (%s disponible)"), u->version);
			printf ("\n"); shown++;
		}
	if (!all)						// etages, pas encore installes
	{
		DIR *d = opendir (PKG_STAGE); struct dirent *e;
		while (d && (e = readdir (d)) != 0)
		{
			int l = (int) strlen (e->d_name);
			if (l < 5 || strcmp (e->d_name + l - 4, ".ini")) continue;
			e->d_name[l - 4] = 0;
			if (M->db.find (e->d_name) || (filter && !strstr (e->d_name, filter))) continue;
			char ip[300]; snprintf (ip, sizeof ip, PKG_STAGE "/%s.ini", e->d_name);
			Ini st; st.load (ip);
			pad (e->d_name, 18); pad (st.get ("package", "version", "?"), 13); fputs (TR ("(étagé : redémarrer pour finir)\n"), stdout); shown++;
		}
		if (d) closedir (d);
	}
	printf (TR (shown > 1 ? "%d paquets\n" : "%d paquet\n"), shown);
	return OK;
}

static int cmd_info (const char *name)
{
	M->load_cached_quiet ();
	const Pkg *p = M->haveIndex ? M->index.find (name) : 0;
	Inst *in = M->db.find (name);
	if (!p && !in) { printf (TR ("%s : paquet inconnu\n"), name); return 2; }
	printf ("%s -- %s\n", name, p ? p->title : in->ini.get ("package", "title", name));
	if (p) printf (TR ("  %s\n  catégorie %s, par %s\n  dans le dépôt : %s, %llu Ko (%llu Ko installé)\n  besoins : %s\n"),
		       p->summary, p->category, p->author, p->version, (p->size + 1023) / 1024, (p->installed + 1023) / 1024, p->needs);
	if (in)
	{
		int nf = 0; for (int i = 0; i < in->ini.n; i++) if (eq (in->ini.kv[i].sec, "files")) nf++;
		printf (TR ("  installé : %s, %d fichiers, mises à jour : %s%s\n"), in->version (), nf, in->mode (), in->required () ? TR (" (fait partie du système)") : "");
	}
	else fputs (TR ("  pas installé\n"), stdout);
	return OK;
}

static bool g_force;		// pkg add --force : les paquets nommes reinstalles meme s'ils sont la
static const char *g_named;	// (le paquet nomme : lui seul est force, pas ses besoins)

static int install_list (const Pkg **list, int n)
{
	int rc = OK;
	for (int i = 0; i < n; i++)
	{
		Inst *in = M->db.find (list[i]->name);
		if (in) R.sayf ("%s %s -> %s ...", list[i]->name, in->version (), list[i]->version);
		else R.sayf ("%s %s ...", list[i]->name, list[i]->version);
		int r = M->install (*list[i], R, g_force && g_named && eq (list[i]->name, g_named));
		if (r != OK) rc = r;
	}
	return rc;
}

static int cmd_add (int ac, char **av)
{
	int rc = OK; bool any = false;
	for (int i = 0; i < ac; i++)
		if (eq (av[i], "--force") || eq (av[i], "-f")) g_force = true;
	for (int i = 0; i < ac; i++)
	{
		if (eq (av[i], "--force") || eq (av[i], "-f")) continue;
		int l = (int) strlen (av[i]);
		if (l > 4 && !strcmp (av[i] + l - 4, ".opk"))			// un fichier de paquet
		{
			int r = M->install_file (av[i], 0, 0, R); if (r != OK) rc = r; any = true; continue;
		}
		Inst *in = M->db.find (av[i]);
		if (in && !in->missing && !g_force) { printf (TR ("%s est déjà installé (%s)\n"), av[i], in->version ()); if (rc == OK) rc = E_ALREADY; continue; }
		if (!M->haveIndex && need_index () != OK) return 2;
		const Pkg *list[32]; int err = 0;
		int n = M->resolve (av[i], list, 32, &err, R);
		if (!n) { rc = err ? err : E_NOTFOUND; continue; }
		g_named = av[i];
		int r = install_list (list, n); if (r != OK) rc = r; any = true;
		g_named = 0;
	}
	return rc == OK ? 0 : rc == E_ALREADY && !any ? 1 : rc == E_ALREADY ? 0 : 2;
}

static int cmd_delete (int ac, char **av)
{
	bool purge = false; int rc = 0;
	for (int i = 0; i < ac; i++)
	{
		if (eq (av[i], "-p")) { purge = true; continue; }
		int r = M->remove (av[i], purge, R);
		if (r == E_NOTINST) { if (!rc) rc = 1; } else if (r != OK) rc = 2;
	}
	return rc;
}

// update : ceux nommes, ou (-a) tous ceux qui ont une mise a jour (pas "never") ; upgrade : les "auto"
static int cmd_update (int ac, char **av, bool upgrade)
{
	if (need_index () != OK) return 2;
	bool all = upgrade;
	for (int i = 0; i < ac; i++) if (eq (av[i], "-a")) all = true;
	const Pkg *list[128]; int n = 0, rc = 0, manual = 0;
	if (all)
		for (int i = 0; i < M->db.n && n < 128; i++)
		{
			Inst &in = *M->db.v[i];
			const Pkg *u = M->update_for (in);
			if (!u || M->staged (in.name) || eq (in.mode (), "never")) continue;
			if (upgrade && !eq (in.mode (), "auto")) { printf (TR ("%s %s -> %s disponible (manuel)\n"), in.name, in.version (), u->version); manual++; continue; }
			list[n++] = u;
		}
	else
		for (int i = 0; i < ac && n < 128; i++)
		{
			Inst *in = M->db.find (av[i]);
			if (!in) { printf (TR ("%s n'est pas installé\n"), av[i]); rc = 1; continue; }
			const Pkg *u = M->update_for (*in);
			if (!u) { printf (TR ("%s est à jour (%s)\n"), av[i], in->version ()); continue; }
			list[n++] = u;
		}
	// leurs besoins d'abord ; celui qui attend des tables plus grandes attend le systeme (et son redemarrage)
	const Pkg *full[160]; int fn = 0; int abi = abi_level ();
	for (int i = 0; i < n; i++)
	{
		Need nd[16]; int nn = parse_needs (list[i]->needs, nd, 16); bool later = false;
		for (int k = 0; k < nn; k++) if (eq (nd[k].name, "abi") && abi < atoi (nd[k].ver)) later = true;
		if (later) { printf (TR ("%s %s attend la mise à jour du système (puis un redémarrage)\n"), list[i]->name, list[i]->version); continue; }
		for (int k = 0; k < nn; k++)
		{
			if (eq (nd[k].name, "abi")) continue;
			Inst *in = M->db.find (nd[k].name);
			if (in && vcmp (in->version (), nd[k].ver) >= 0) continue;
			const Pkg *q = M->index.find (nd[k].name);
			bool dup = false; for (int j = 0; j < fn; j++) if (full[j] == q) dup = true;
			for (int j = i; j < n; j++) if (list[j] == q) dup = true;
			if (q && !dup && fn < 160) full[fn++] = q;
		}
		bool dup = false; for (int j = 0; j < fn; j++) if (full[j] == list[i]) dup = true;
		if (!dup && fn < 160) full[fn++] = list[i];
	}
	if (!fn) { if (!manual) fputs (TR ("tout est à jour\n"), stdout); return rc ? rc : 1; }
	return install_list (full, fn) == OK ? rc : 2;
}

static int cmd_check (void)
{
	if (need_index () != OK) return 2;
	int k = 0;
	for (int i = 0; i < M->db.n; i++)
	{
		const Pkg *u = M->update_for (*M->db.v[i]);
		if (u) { printf ("%-18s %s -> %s  (%s)\n", M->db.v[i]->name, M->db.v[i]->version (), u->version, M->db.v[i]->mode ()); k++; }
	}
	if (k) printf (TR (k > 1 ? "%d mises à jour disponibles\n" : "%d mise à jour disponible\n"), k); else fputs (TR ("tout est à jour\n"), stdout);
	return k ? 0 : 1;
}

static int cmd_commit (void)
{
	// ce qu'il a fait, garde dans /var/pkg/commit.log (au demarrage, ses lignes passent avant qu'on les lise)
	static char said[4096]; R.log = said; R.logN = 0;
	M->commit (R);
	R.log = 0;
	if (R.logN)
	{
		static char old[8192], out[8192 + 4096 + 96];
		int n = 0; int f = open (PKG_COMMIT_LOG, O_RDONLY);
		if (f >= 0) { n = (int) read (f, old, sizeof old - 1); close (f); if (n < 0) n = 0; }
		int keep = n > 6000 ? n - 6000 : 0;
		while (keep > 0 && keep < n && old[keep - 1] != '\n') keep++;
		time_t t = time (0); struct tm *tm = localtime (&t);
		int o = snprintf (out, sizeof out, "%.*s%04d-%02d-%02d %02d:%02d:%02d pkg commit\n", n - keep, old + keep,
				  tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
		memcpy (out + o, said, (size_t) R.logN); o += R.logN;
		write_file (PKG_COMMIT_LOG, out, (u64) o);
	}
	if (M->kernelChanged) { fputs (TR ("le système de démarrage a changé : redémarrage\n"), stdout); fflush (stdout); reboot_now (); }
	return 0;
}

// ---- pour Logiciels (pkgman.app et le volet de Réglages, src/apps/pkgman/pkgui.cpp) : des lignes à lire par eux ------
//
//   pkg ui-list                     l'état : « I » (l'index : lu, signature bonne, sa date), puis « R » par paquet
//   pkg ui-job check                relire le dépôt
//   pkg ui-job install NOM...       installer ou mettre à jour (et ce qu'il leur faut ; les apps d'abord, le système à la fin)
//   pkg ui-job remove NOM...        retirer
//   pkg ui-job mode NOM MODE        manual, auto ou never
//
// Un travail écrit sa progression : « C nom » (le paquet en cours), « P pct », « D nom » (fini), « X nom » (échec),
// « M texte » (ce qu'il dit), puis « E code » (OK, E_*). Les champs sont séparés par des tabulations. Un travail à la
// fois (/tmp/pkgjob.pid) ; Logiciels fermé, le travail va au bout (SIGPIPE ignoré, la sortie est perdue).
static void ui_field (const char *s)
{
	putchar ('\t');
	for (; s && *s; s++) putchar (*s == '\t' || *s == '\n' || *s == '\r' ? ' ' : *s);
}
static void ui_line (char tag, const char *s) { putchar (tag); ui_field (s); putchar ('\n'); fflush (stdout); }

struct UiReport : Report
{
	int pct = -1;
	void say (const char *s) override { ui_line ('M', s); }
	void step (const char *, u64 done, u64 total) override
	{
		int p = total ? (int) (done * 100 / total) : 0;
		if (p > 100) p = 100;
		if (p != pct) { pct = p; printf ("P\t%d\n", p); fflush (stdout); }
	}
};

// (comme le snapshot de pkgman : un paquet installé, ou du dépôt, une ligne ; les parties restent avec leur paquet)
static int cmd_ui_list (void)
{
	Manager &m = *M;
	m.load_cached_quiet ();
	printf ("I\t%d\t%d", m.haveIndex ? 1 : 0, m.verified ? 1 : 0); ui_field (m.index.date); putchar ('\n');
	struct Row
	{
		char name[40], title[48], summary[120], category[24], inst[24], avail[24], mode[8];
		u64 size, installed;
		bool isInst, hasUpdate, required, restart, staged;
	};
	int cap = m.index.n + m.db.n + 1, n = 0;
	Row *rows = (Row *) calloc ((size_t) cap, sizeof (Row));
	if (!rows) return 2;
	for (int i = 0; i < m.db.n; i++)
	{
		Inst &in = *m.db.v[i];
		if (*m.part_of (in.name)) continue;
		Row &r = rows[n++];
		cpy (r.name, in.name, sizeof r.name);
		cpy (r.title, TR (in.ini.get ("package", "title", in.name)), sizeof r.title);	// (traduits : lang/en.txt)
		cpy (r.summary, TR (in.ini.get ("package", "summary")), sizeof r.summary);
		cpy (r.category, in.ini.get ("package", "category"), sizeof r.category);
		cpy (r.inst, in.version (), sizeof r.inst); cpy (r.mode, in.mode (), sizeof r.mode);
		r.isInst = true; r.required = in.required (); r.staged = m.staged (in.name);
		r.restart = eq (in.ini.get ("package", "restart", "0"), "1");
	}
	for (int i = 0; i < m.index.n; i++)
	{
		const Pkg &p = m.index.p[i];
		if (*p.part) continue;
		Row *r = 0;
		for (int k = 0; k < n; k++) if (eq (rows[k].name, p.name)) r = &rows[k];
		if (!r) { r = &rows[n++]; cpy (r->name, p.name, sizeof r->name); cpy (r->mode, "manual", sizeof r->mode); }
		cpy (r->title, TR (p.title), sizeof r->title);
		if (p.summary[0]) cpy (r->summary, TR (p.summary), sizeof r->summary);
		cpy (r->category, p.category, sizeof r->category);
		cpy (r->avail, p.version, sizeof r->avail);
		r->size = p.size; r->installed = p.installed; r->restart = p.restart; r->required = p.required || r->required;
		for (int k = 0; k < m.index.n; k++)
			if (eq (m.index.p[k].part, p.name)) { r->size += m.index.p[k].size; r->installed += m.index.p[k].installed; }
		Inst *in = r->isInst ? m.db.find (p.name) : 0;
		r->hasUpdate = in && m.update_for (*in) && !r->staged;
	}
	for (int i = 0; i < n; i++)
	{
		const Row &r = rows[i];
		putchar ('R');
		ui_field (r.name); ui_field (r.title); ui_field (r.summary); ui_field (r.category);
		ui_field (r.inst); ui_field (r.avail); ui_field (r.mode);
		printf ("\t%llu\t%llu\t", r.size, r.installed);
		// i installé, u mise à jour, q fait partie du système, r redémarrage, s étagé, n étagé et plus récent qu'installé
		if (r.isInst) putchar ('i');
		if (r.hasUpdate) putchar ('u');
		if (r.required) putchar ('q');
		if (r.restart) putchar ('r');
		if (r.staged) putchar ('s');
		if (r.staged && r.avail[0] && vcmp (r.avail, r.inst) > 0) putchar ('n');
		putchar ('-');
		putchar ('\n');
	}
	free (rows);
	fflush (stdout);
	return OK;
}

#define UI_LOCK "/tmp/pkgjob.pid"
static int cmd_ui_job (int ac, char **av)
{
	signal (SIGPIPE, SIG_IGN);
	if (ac < 1) return 3;
	{
		int pid = 0;
		FILE *f = fopen (UI_LOCK, "r");
		if (f) { if (fscanf (f, "%d", &pid) != 1) pid = 0; fclose (f); }
		if (pid > 0 && pid != (int) getpid () && kill (pid, 0) == 0)
		{
			ui_line ('M', TR ("Un autre travail sur les paquets est en cours."));
			printf ("E\t%d\n", E_RUNNING); fflush (stdout);
			return 1;
		}
		f = fopen (UI_LOCK, "w");
		if (f) { fprintf (f, "%d\n", (int) getpid ()); fclose (f); }
	}
	Manager &m = *M;
	UiReport r;
	const char *kind = av[0];
	char **names = av + 1; int nn = ac - 1;
	int rc = OK;
	if (eq (kind, "check"))
	{
		ui_line ('C', "index");
		rc = m.refresh (r);
	}
	else if (eq (kind, "install"))
	{
		m.load_cached_quiet ();
		const Pkg *list[96]; int n = 0;
		for (int i = 0; i < nn; i++)
		{
			Inst *in = m.db.find (names[i]);
			const Pkg *p = m.index.find (names[i]);
			if (!p) continue;
			if (in) { if (n < 96) list[n++] = p; continue; }
			int err = 0; const Pkg *sub[32];
			int k = m.resolve (names[i], sub, 32, &err, r);
			for (int j = 0; j < k && n < 96; j++)
			{
				bool dup = false;
				for (int q = 0; q < n; q++) if (list[q] == sub[j]) dup = true;
				if (!dup) list[n++] = sub[j];
			}
			if (!k) { rc = err ? err : E_NOTFOUND; ui_line ('X', names[i]); }
		}
		for (int i = 1; i < n; i++)			// les apps d'abord, le système à la fin
			for (int j = i; j > 0 && list[j - 1]->restart && !list[j]->restart; j--) { const Pkg *t = list[j]; list[j] = list[j - 1]; list[j - 1] = t; }
		bool self = false;
		for (int i = 0; i < n; i++)
		{
			ui_line ('C', list[i]->name); r.pct = -1;
			int x = m.install (*list[i], r);
			if (x != OK) { rc = x; ui_line ('X', list[i]->name); }
			else if (eq (list[i]->name, "pkgman")) self = true;
			ui_line ('D', list[i]->name);
		}
		// le gestionnaire s'est mis à jour : le service pkgd repart dans sa nouvelle version (la fenêtre se relance)
		if (self && proc_kill ("pkgd")) { usleep (300000); launch ("/sys/pkgd"); }
	}
	else if (eq (kind, "remove"))
	{
		m.load_cached_quiet ();
		for (int i = 0; i < nn; i++)
		{
			ui_line ('C', names[i]);
			int x = m.remove (names[i], false, r);
			if (x != OK) { rc = x; ui_line ('X', names[i]); }
			ui_line ('D', names[i]);
		}
	}
	else if (eq (kind, "mode") && nn == 2)
	{
		if (!m.set_mode (names[0], names[1])) rc = E_NOTINST;
	}
	else rc = 3;
	m.refresh_desktop ();
	printf ("E\t%d\n", rc); fflush (stdout);
	unlink (UI_LOCK);
	return rc == OK ? 0 : 2;
}

static int run (const char *c, char **rest, int nr)
{
	Manager &m = *M;
	if (eq (c, "ui-list")) return cmd_ui_list ();
	if (eq (c, "ui-job")) return cmd_ui_job (nr, rest);
	if (eq (c, "list")) return cmd_list (nr, rest);
	if (eq (c, "info") && nr == 1) return cmd_info (rest[0]);
	if (eq (c, "add") && nr) return cmd_add (nr, rest);
	if ((eq (c, "delete") || eq (c, "remove")) && nr) return cmd_delete (nr, rest);
	if (eq (c, "update") && nr) return cmd_update (nr, rest, false);
	if (eq (c, "upgrade")) return cmd_update (0, 0, true);
	if (eq (c, "check")) return cmd_check ();
	if (eq (c, "mode") && nr == 2)
	{
		if (!eq (rest[1], "manual") && !eq (rest[1], "auto") && !eq (rest[1], "never")) { fputs (TR ("le mode : manual, auto ou never\n"), stdout); return 3; }
		if (!m.set_mode (rest[0], rest[1])) { printf (TR ("%s n'est pas installé\n"), rest[0]); return 1; }
		printf ("%s : %s\n", rest[0], rest[1]); return 0;
	}
	if (eq (c, "commit")) return cmd_commit ();
	printf (TR ("pkg : commande inconnue ou nom manquant : %s (pkg seul : l'aide)\n"), c);
	return 3;
}

int main (int argc, char **argv)
{
	int a0 = 1;
	const char *repo = 0;
	for (;;)	// (avant la commande : -r DEPOT, --root RACINE, dans n'importe quel ordre)
	{
		if (argc - a0 >= 2 && eq (argv[a0], "-r")) { repo = argv[a0 + 1]; a0 += 2; continue; }
		if (argc - a0 >= 2 && eq (argv[a0], "--root")) { pkg_set_root (argv[a0 + 1]); a0 += 2; continue; }
		if (argc - a0 >= 1 && !strncmp (argv[a0], "root=", 5)) { pkg_set_root (argv[a0] + 5); a0 += 1; continue; }
		break;
	}
	static Manager *mp = new Manager;	// (après --root : le Manager lit /etc/pkg et la base de cette racine)
	Manager &m = *mp; M = &m;
	if (repo) cpy (m.repo, repo, sizeof m.repo);
	if (argc - a0 < 1)
	{
		fputs (TR ("usage : pkg list [-a] [filtre] | info NOM | add [--force] NOM... | delete [-p] NOM... |\n"
			"        update NOM... | update -a | upgrade | check | mode NOM manual|auto|never | commit\n"
			"        [-r DEPOT] avant la commande : un autre dépôt\n"
			"        [--root RACINE] avant la commande : le système à gérer (osinstall : /media/tgt)\n"), stdout);
		return 3;
	}
	int rc = run (argv[a0], argv + a0 + 1, argc - a0 - 1);
	m.refresh_desktop ();
	return rc;
}
