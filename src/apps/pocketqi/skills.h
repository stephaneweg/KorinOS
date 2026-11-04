//
// skills.h -- PocketQI : ce que l'assistante sait du système, lu dans les manifestes des applications
// (SD0:/apps/<id>.app/app.txt, docs/ABI.md), dans data/korin.txt (le bureau lui-même) et dans data/tools/*.txt (ses
// outils, faits par elle : tools.h). Les clés d'Onyx (name,
// description, llm.<action>) plus celles de Korin (category, opens, keywords, help, run.<action>) :
//
//   name        = Letters                          le nom montré
//   category    = Bureautique                      le tiroir du lanceur
//   opens       = rtf docx odt                     les extensions qu'elle ouvre (son argument est alors un fichier)
//   description = Le traitement de texte...         une phrase : ce qu'elle fait
//   keywords    = lettre courrier document word     les mots qui la désignent, pour la trouver
//   help        = Pour enregistrer : menu Fichier > Enregistrer (Ctrl+S).    un conseil ; une ligne par conseil
//   llm.<action> = <types> : <ce qu'elle fait>      une action que l'assistante peut lancer (types : texte nombre
//                                                    date heure fichier ; [entre crochets] : facultatif)
//   run.<action> = --new $1                         sa ligne de commande ($1, $2... : les arguments) ; absente :
//                                                    les arguments dans l'ordre
//
// L'action « open » existe toujours : l'app lancée, avec un fichier si elle a « opens » (open [fichier]). Une action est
// vérifiée contre le manifeste (action connue, nombre et format des arguments) avant tout lancement.
//
#ifndef POCKETQI_SKILLS_H
#define POCKETQI_SKILLS_H

#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <vector>

extern char **environ;

namespace pq {

struct SkillAction
{
	std::string name;			// "open"
	std::vector<std::string> types;		// "fichier", "[texte]"
	std::string description, run;
};

struct Skill
{
	std::string id;				// le dossier sans .app : "letters" ; "korin" : le bureau (rien à lancer)
	std::string name, category, description, keywords, opens;
	std::vector<std::string> help;
	std::vector<SkillAction> actions;
	bool launchable = true;
	bool tool = false;			// un outil de l'assistante (data/tools, tools.h) : fait par elle, pas lancé
	std::string exe () const { return "SD0:/apps/" + id + ".app/main"; }
};

// ---- les mots ------------------------------------------------------------------------------------------------------
static std::string trim (const std::string &s)
{
	size_t a = 0, b = s.size ();
	while (a < b && (unsigned char) s[a] <= ' ') a++;
	while (b > a && (unsigned char) s[b - 1] <= ' ') b--;
	return s.substr (a, b - a);
}

// minuscules sans accents (les lettres latines courantes en UTF-8), le reste gardé
static std::string fold (const std::string &s)
{
	std::string o;
	for (size_t i = 0; i < s.size (); i++)
	{
		unsigned char c = (unsigned char) s[i];
		if (c == 0xC3 && i + 1 < s.size ())
		{
			unsigned char d = (unsigned char) s[++i] | 0x20;	// (majuscules : 0x80-0x9F -> 0xA0-0xBF)
			const char *map = "aaaaaaeceeeeiiiidnooooo/ouuuuyty";	// 0xA0..0xBF
			char m = d >= 0xA0 && d <= 0xBF ? map[d - 0xA0] : '?';
			if (d == 0xA6) o += "ae";
			else if (d == 0xB7) o += ' ';
			else o += m;
			continue;
		}
		if (c == 0xC5 && i + 1 < s.size () && ((unsigned char) s[i + 1] == 0x92 || (unsigned char) s[i + 1] == 0x93)) { o += "oe"; i++; continue; }
		o += (char) (c >= 'A' && c <= 'Z' ? c + 32 : c);
	}
	return o;
}

static std::vector<std::string> words (const std::string &s)
{
	static const char *const STOP[] = { "les", "des", "une", "est", "que", "qui", "pour", "dans", "avec", "sur", "par",
		"comment", "faire", "faut", "peux", "puis", "veux", "voudrais", "mon", "mes", "ton", "tes", "son", "ses",
		"the", "and", "how", "can", "what", "with", "this", "that", "est-ce", "quoi", "quel", "quelle", "pas",
		"plus", "bien", "elle", "lui", "leur", "nous", "vous", "moi", "toi", "aux", "tout", "tous", "fait", "ouvre",
		"ouvrir", "lance", "lancer", "open", "start", 0 };
	std::vector<std::string> v;
	std::string f = fold (s), w;
	for (size_t i = 0; i <= f.size (); i++)
	{
		unsigned char c = i < f.size () ? (unsigned char) f[i] : ' ';
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80) { w += (char) c; continue; }
		if (w.size () >= 3)
		{
			bool stop = false;
			for (int k = 0; STOP[k]; k++) if (w == STOP[k]) { stop = true; break; }
			if (!stop) v.push_back (w);
		}
		w.clear ();
	}
	return v;
}

// deux mots se valent : égaux, ou la même racine (5 lettres) pour les mots longs (« lettres » et « lettre »)
static bool word_match (const std::string &a, const std::string &b)
{
	if (a == b) return true;
	return a.size () >= 5 && b.size () >= 5 && a.compare (0, 5, b, 0, 5) == 0;
}

static int text_score (const std::vector<std::string> &q, const std::string &text)
{
	// (un mot identique compte double : « tableur » est plus près du tableur que de la « table » de vérité)
	int n = 0;
	std::vector<std::string> t = words (text);
	for (auto &a : q)
	{
		int best = 0;
		for (auto &b : t) if (word_match (a, b)) { best = a == b ? 2 : 1; if (best == 2) break; }
		n += best;
	}
	return n;
}

// ---- les manifestes ------------------------------------------------------------------------------------------------
static void skill_parse (Skill &a, const std::string &txt)
{
	size_t p = 0;
	while (p < txt.size ())
	{
		size_t e = txt.find ('\n', p);
		if (e == std::string::npos) e = txt.size ();
		std::string l = txt.substr (p, e - p);
		p = e + 1;
		size_t hash = l.find ('#');			// (comme ini_read d'applist.inc : « # » commence un commentaire)
		if (hash != std::string::npos) l.resize (hash);
		size_t eq = l.find ('=');
		if (eq == std::string::npos) continue;
		std::string k = fold (trim (l.substr (0, eq))), v = trim (l.substr (eq + 1));
		if (v.empty ()) continue;
		if (k == "name") a.name = v;
		else if (k == "category") a.category = v;
		else if (k == "description") a.description = v;
		else if (k == "keywords") a.keywords = v;
		else if (k == "opens") a.opens = v;
		else if (k == "help") a.help.push_back (v);
		else if (k.compare (0, 4, "llm.") == 0 && k.size () > 4)
		{
			SkillAction act;
			act.name = k.substr (4);
			size_t c = v.find (':');
			std::string ty = trim (v.substr (0, c));
			size_t q = 0;
			while (q < ty.size ())
			{
				size_t s = ty.find (' ', q);
				if (s == std::string::npos) s = ty.size ();
				if (s > q) act.types.push_back (fold (ty.substr (q, s - q)));
				q = s + 1;
			}
			if (c != std::string::npos) act.description = trim (v.substr (c + 1));
			bool known = false;
			for (auto &x : a.actions) if (x.name == act.name) { x.types = act.types; x.description = act.description; known = true; }
			if (!known) a.actions.push_back (act);
		}
		else if (k.compare (0, 4, "run.") == 0 && k.size () > 4)
		{
			bool known = false;
			for (auto &x : a.actions) if (x.name == k.substr (4)) { x.run = v; known = true; }
			if (!known) { SkillAction act; act.name = k.substr (4); act.run = v; a.actions.push_back (act); }
		}
	}
	// open : toujours là
	bool hasOpen = false;
	for (auto &x : a.actions) if (x.name == "open") hasOpen = true;
	if (!hasOpen && a.launchable && !a.tool)
	{
		SkillAction o;
		o.name = "open";
		if (!a.opens.empty ()) { o.types.push_back ("[fichier]"); o.description = "ouvre l'application, avec ce fichier s'il est donne"; }
		else o.description = "ouvre l'application";
		a.actions.insert (a.actions.begin (), o);
	}
	if (a.name.empty ()) a.name = a.id;
}

static bool read_file (const std::string &path, std::string &out, size_t cap = 64 * 1024)
{
	int f = open (path.c_str (), O_RDONLY);
	if (f < 0) return false;
	out.clear ();
	char b[4096];
	for (;;)
	{
		int n = (int) read (f, b, sizeof b);
		if (n <= 0) break;
		out.append (b, (size_t) n);
		if (out.size () >= cap) break;
	}
	close (f);
	return true;
}

// le bureau (data/korin.txt, posé dans SD0:/apps/pocketqi.app), puis les applications de SD0:/apps, sauf l'assistante elle-même
static std::vector<Skill> skills_scan (const std::string &home)
{
	std::vector<Skill> v;
	std::string t;
	if (read_file (home + "/korin.txt", t))
	{
		Skill k;
		k.id = "korin";
		k.launchable = false;
		skill_parse (k, t);
		v.push_back (k);
	}
	// les outils de l'assistante (data/tools/<id>.txt : weather, memo, planner, shopping), avant les apps
	struct dirent *e;
	DIR *d = opendir ((home + "/tools").c_str ());
	if (d)
	{
		std::vector<std::string> tools;
		while ((e = readdir (d)))
		{
			std::string n = e->d_name;
			if (n.size () > 4 && n.compare (n.size () - 4, 4, ".txt") == 0) tools.push_back (n.substr (0, n.size () - 4));
		}
		closedir (d);
		std::sort (tools.begin (), tools.end ());
		for (auto &id : tools)
		{
			if (!read_file (home + "/tools/" + id + ".txt", t)) continue;
			Skill s;
			s.id = id;
			s.tool = true;
			skill_parse (s, t);
			v.push_back (s);
		}
	}
	d = opendir ("SD0:/apps");
	if (!d) return v;
	std::vector<std::string> ids;
	while ((e = readdir (d)))
	{
		std::string n = e->d_name;
		if (n.size () > 4 && n.compare (n.size () - 4, 4, ".app") == 0) ids.push_back (n.substr (0, n.size () - 4));
	}
	closedir (d);
	std::sort (ids.begin (), ids.end ());
	for (auto &id : ids)
	{
		if (id == "pocketqi") continue;
		if (!read_file ("SD0:/apps/" + id + ".app/app.txt", t)) continue;
		Skill s;
		s.id = id;
		skill_parse (s, t);
		v.push_back (s);
	}
	return v;
}

static const Skill *skill_find (const std::vector<Skill> &v, const std::string &id)
{
	std::string f = fold (id);
	for (auto &s : v) if (s.id == f) return &s;
	for (auto &s : v) if (fold (s.name) == f) return &s;		// (le modèle donne parfois le nom montré)
	return 0;
}

// la pertinence d'une app pour une question (ses noms comptent plus que ses conseils)
static int skill_score (const Skill &s, const std::vector<std::string> &q)
{
	return 4 * text_score (q, s.id + " " + s.name) + 3 * text_score (q, s.keywords) + 2 * text_score (q, s.description)
	       + text_score (q, s.category);
}

// ---- les actions ---------------------------------------------------------------------------------------------------
static bool type_ok (const std::string &type, const std::string &v)
{
	std::string t = type;
	if (!t.empty () && t[0] == '[') t = t.substr (1, t.size () - 2);
	if (v.empty () || v.find ('\n') != std::string::npos) return false;
	auto digit = [] (char c) { return c >= '0' && c <= '9'; };
	if (t == "nombre") { for (char c : v) if (!digit (c) && c != '-' && c != '.' && c != ',') return false; return true; }
	if (t == "date") return v.size () == 10 && digit (v[0]) && digit (v[3]) && v[4] == '-' && v[7] == '-' && digit (v[9]);
	if (t == "heure") return v.size () == 5 && digit (v[0]) && digit (v[1]) && v[2] == ':' && digit (v[3]) && digit (v[4]);
	return true;							// texte, fichier
}

// l'appel {"app": id, "args": [action, arguments...]} vérifié ; "" s'il est bon, sinon pourquoi (pour le modèle)
static std::string action_check (const Skill &s, const std::vector<std::string> &args, const SkillAction **act)
{
	if (!s.launchable) return "\"" + s.id + "\" n'est pas une application a lancer";
	std::string a = args.empty () ? "open" : fold (args[0]);
	for (auto &x : s.actions)
	{
		if (x.name != a) continue;
		size_t need = 0;
		for (auto &t : x.types) if (t.empty () || t[0] != '[') need++;
		size_t given = args.empty () ? 0 : args.size () - 1;
		if (given < need || given > x.types.size ())
			return "l'action " + a + " de " + s.id + " prend " + std::to_string (need) + " a " + std::to_string (x.types.size ()) + " arguments";
		for (size_t i = 0; i < given; i++)
			if (!type_ok (x.types[i], args[i + 1])) return "l'argument " + std::to_string (i + 1) + " de " + s.id + " " + a + " doit etre de type " + x.types[i];
		*act = &x;
		return "";
	}
	return "l'application " + s.id + " n'a pas d'action " + a;
}

// lancée : SD0:/apps/<id>.app/main et ses arguments (run.<action>, sinon les arguments dans l'ordre) -> pid, ou -1
static int action_run (const Skill &s, const SkillAction &act, const std::vector<std::string> &args)
{
	std::vector<std::string> av;
	av.push_back (s.exe ());
	if (!act.run.empty ())
	{
		size_t q = 0;
		const std::string &r = act.run;
		while (q < r.size ())
		{
			size_t e = r.find (' ', q);
			if (e == std::string::npos) e = r.size ();
			std::string tok = r.substr (q, e - q);
			q = e + 1;
			if (tok.empty ()) continue;
			if (tok[0] == '$' && tok.size () > 1 && tok[1] >= '1' && tok[1] <= '9')
			{
				size_t i = (size_t) (tok[1] - '0');
				if (i < args.size ()) av.push_back (args[i]);	// (un argument facultatif absent : rien)
			}
			else av.push_back (tok);
		}
	}
	else for (size_t i = 1; i < args.size (); i++) av.push_back (args[i]);
	std::vector<char *> argv;
	for (auto &x : av) argv.push_back ((char *) x.c_str ());
	argv.push_back (0);
	pid_t pid;
	if (posix_spawn (&pid, av[0].c_str (), 0, 0, argv.data (), environ) != 0) return -1;
	return (int) pid;
}

} // namespace pq

#endif
