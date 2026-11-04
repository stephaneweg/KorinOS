//
// pkg -- les paquets d'ARMPI depuis le terminal (docs/PAQUETS.md, port de user/BinUtils/pkg.cpp d'Onyx) :
//
//   pkg list [-a] [filtre]       les paquets installes (-a : tout le depot)
//   pkg info <nom>               un paquet : ses versions, sa taille, ses besoins, son mode
//   pkg add <nom>...             installer (et ce qu'il lui faut) ; deja la : dit, rien de fait
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

using namespace pkg;

struct Out : Report
{
	char *log = 0; int logN = 0;			// (commit : ce qu'il a dit, pour SD0:/var/pkg/commit.log)
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

static int install_list (const Pkg **list, int n)
{
	int rc = OK;
	for (int i = 0; i < n; i++)
	{
		Inst *in = M->db.find (list[i]->name);
		if (in) R.sayf ("%s %s -> %s ...", list[i]->name, in->version (), list[i]->version);
		else R.sayf ("%s %s ...", list[i]->name, list[i]->version);
		int r = M->install (*list[i], R);
		if (r != OK) rc = r;
	}
	return rc;
}

static int cmd_add (int ac, char **av)
{
	int rc = OK; bool any = false;
	for (int i = 0; i < ac; i++)
	{
		int l = (int) strlen (av[i]);
		if (l > 4 && !strcmp (av[i] + l - 4, ".opk"))			// un fichier de paquet
		{
			int r = M->install_file (av[i], 0, 0, R); if (r != OK) rc = r; any = true; continue;
		}
		Inst *in = M->db.find (av[i]);
		if (in && !in->missing) { printf (TR ("%s est déjà installé (%s)\n"), av[i], in->version ()); if (rc == OK) rc = E_ALREADY; continue; }
		if (!M->haveIndex && need_index () != OK) return 2;
		const Pkg *list[32]; int err = 0;
		int n = M->resolve (av[i], list, 32, &err, R);
		if (!n) { rc = err ? err : E_NOTFOUND; continue; }
		int r = install_list (list, n); if (r != OK) rc = r; any = true;
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
	// ce qu'il a fait, garde dans SD0:/var/pkg/commit.log (au demarrage, ses lignes passent avant qu'on les lise)
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

static int run (const char *c, char **rest, int nr)
{
	Manager &m = *M;
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
	static Manager m; M = &m;
	int a0 = 1;
	if (argc >= 3 && eq (argv[1], "-r")) { cpy (m.repo, argv[2], sizeof m.repo); a0 = 3; }
	if (argc - a0 < 1)
	{
		fputs (TR ("usage : pkg list [-a] [filtre] | info NOM | add NOM... | delete [-p] NOM... |\n"
			"        update NOM... | update -a | upgrade | check | mode NOM manual|auto|never | commit\n"
			"        [-r DEPOT] avant la commande : un autre dépôt\n"), stdout);
		return 3;
	}
	int rc = run (argv[a0], argv + a0 + 1, argc - a0 - 1);
	m.refresh_desktop ();
	return rc;
}
