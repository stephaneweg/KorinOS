//
// brain.h -- PocketQI : ce qui se passe entre une question écrite et la réponse (le chat () et le run_outcomes () de
// PocketQI, d'après pocketqi/brain.h d'Onyx). Chaque question tourne sur un fil à elle : la fenêtre ne l'attend jamais ;
// le fil lui parle par une file d'événements (Brain::take), lue à chaque tour de la boucle de la fenêtre.
//
//   question -> le LLM (Mistral, Groq en secours ; ou tout serveur compatible OpenAI : url =) avec le prompt système,
//   les compétences lues dans les manifestes (skills.h) et les derniers échanges -> {"outcomes": [...]}, faits dans
//   l'ordre : say (la bulle), humeur (le visage), aide (les conseils d'une app : second tour), les outils (tools.h :
//   météo, notes, agenda, courses, faits ici) et les applications de la liste (vérifiées contre leur manifeste, puis
//   lancées). Un appel refusé repart au LLM (second tour), deux au plus.
//   Sans service configuré, ou s'il ne répond pas : la réponse locale : un outil si la phrase en demande un
//   (LocalParser), sinon les mêmes manifestes (les conseils de l'app la plus proche de la question).
//
// Réglages : SD0:/home/pocketqi/config.ini (menu PocketQI > Réglages...). Les clés d'API n'en sortent que vers leur
// service, dans l'en-tête Authorization : jamais au journal, à l'écran, ni dans un paquet. La conversation est oubliée
// après 10 minutes sans question ; 6 échanges au plus sont renvoyés.
//
#ifndef POCKETQI_BRAIN_H
#define POCKETQI_BRAIN_H

#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include "http.h"
#include "json.h"
#include "skills.h"
#include "config.h"
#include "tools.h"
#include "core/pocket_humeur.h"

namespace pq {

// ---- ce que le fil dit à la fenêtre ----------------------------------------------------------------------------------
enum { BE_SAY, BE_NOTE, BE_ERROR, BE_MOOD, BE_PRINT, BE_DONE };
struct BrainEvent { int kind; std::string text; int mood, intensity; std::string body; };	// BE_PRINT : text le titre, body le texte

struct Exchange { std::string user, reply; };		// reply : le JSON des outcomes, tel qu'attendu en sortie

class Brain
{
public:
	// une question : false si une autre tourne encore
	bool ask (const std::string &question)
	{
		if (busy) return false;
		// « lis-les à voix haute », « read it aloud » : les réponses de cette question sont dites, voix réglée ou non
		std::string f = fold (question);
		aloud = f.find ("voix haute") != std::string::npos || f.find ("haute voix") != std::string::npos ||
			f.find ("aloud") != std::string::npos || f.find ("out loud") != std::string::npos;
		busy = true;
		cancel = 0;
		job = question;
		pthread_t t;
		if (pthread_create (&t, 0, worker, this) != 0) { busy = false; return false; }
		pthread_detach (t);
		return true;
	}
	bool running () const { return busy; }
	void set_english (bool e) { en = e; }
	bool english () const { return en; }
	bool aloud_asked () const { return aloud; }	// la question demande la voix (fenêtre : à lire après ask)		// la langue du système : celle des réponses des outils
	void stop () { cancel = 1; }
	void forget () { lock (); hist.clear (); unlock (); }
	// les événements arrivés depuis le dernier appel
	std::vector<BrainEvent> take ()
	{
		lock ();
		std::vector<BrainEvent> v;
		v.swap (events);
		unlock ();
		return v;
	}

private:
	volatile bool busy = false;
	volatile int cancel = 0;
	std::string job;
	pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
	std::vector<BrainEvent> events;
	std::vector<Exchange> hist;
	unsigned lastAsk = 0;
	std::vector<Skill> skills;
	unsigned skillsAt = 0;
	Config cfg;
	bool en = false;
	bool aloud = false;

	void lock () { pthread_mutex_lock (&mu); }
	void unlock () { pthread_mutex_unlock (&mu); }
	void post (int kind, const std::string &text, int mood = 0, int intensity = 0, const std::string &body = std::string ())
	{
		lock ();
		events.push_back (BrainEvent { kind, text, mood, intensity, body });
		unlock ();
	}
	// un outil fait (tools.h) : sa réponse dans la bulle, l'impression demandée à la fenêtre -> ce qui a été dit
	std::string run_tool (const Skill &s, const std::vector<std::string> &args)
	{
		ToolOut r = tool_run (s.id, args, cfg, en, &cancel);
		if (!r.note.empty ()) post (BE_NOTE, r.note);
		if (!r.say.empty ()) post (BE_SAY, r.say);
		if (!r.printText.empty ()) post (BE_PRINT, r.printTitle, 0, 0, r.printText);
		return r.say;
	}
	static void *worker (void *self)
	{
		Brain *b = (Brain *) self;
		b->chat (b->job);
		b->post (BE_DONE, "");
		b->busy = false;
		return 0;
	}

	// ---- le prompt -------------------------------------------------------------------------------------------------
	static std::string today ()
	{
		static const char *const J[] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };
		static const char *const M[] = { "janvier", "fevrier", "mars", "avril", "mai", "juin", "juillet", "aout",
						 "septembre", "octobre", "novembre", "decembre" };
		time_t now = time (0);
		struct tm tm;
		localtime_r (&now, &tm);
		char b[96];
		snprintf (b, sizeof b, "%s %d %s %d (%s), %02d:%02d", J[tm.tm_wday % 7], tm.tm_mday, M[tm.tm_mon % 12], tm.tm_year + 1900,
			  iso_date (planner_today ()).c_str (), tm.tm_hour, tm.tm_min);
		return b;
	}
	static std::string action_line (const SkillAction &a)
	{
		std::string s = a.name + " (";				// open ([fichier]) : ses arguments, [facultatifs]
		for (size_t i = 0; i < a.types.size (); i++) s += (i ? ", " : "") + a.types[i];
		return s + ")" + (a.description.empty () ? "" : " : " + a.description);
	}
	// les apps de la question d'abord : leurs conseils sont donnés en entier ; les autres en une ligne
	std::string system_prompt (const std::vector<std::string> &q)
	{
		std::vector<std::pair<int, size_t>> rank;
		for (size_t i = 0; i < skills.size (); i++)
			if (skills[i].launchable && !skills[i].tool) rank.push_back (std::make_pair (-skill_score (skills[i], q), i));
		std::stable_sort (rank.begin (), rank.end ());
		std::vector<bool> full (skills.size ());
		for (size_t i = 0; i < rank.size () && i < 3; i++) if (rank[i].first < 0) full[rank[i].second] = true;

		std::string s =
			"Tu es PocketQI, l'assistante de Korin OS, un systeme d'exploitation pour Raspberry Pi 4 ecrit en assembleur. "
			"Tu aides la personne a se servir du systeme et de ses applications, surtout la bureautique, comme un compagnon "
			"de bureau : concrete, precise, breve et bienveillante. Elle t'ecrit au clavier ; ta reponse s'affiche dans une "
			"petite bulle a cote de son travail : deux a cinq phrases, sans titre, sans gras, sans markdown ; une marche a "
			"suivre peut etre une liste d'etapes numerotees (\"1. ...\" une par ligne). "
			"Appuie-toi sur les competences ci-dessous, lues dans les manifestes des applications installees : leurs noms, "
			"leurs menus, leurs raccourcis. N'invente jamais une application, un menu, un raccourci ou une action qui n'y "
			"est pas ; si tu ne sais pas, dis-le simplement et propose l'application la plus proche. Les menus d'une "
			"application sont dans la barre de menus en haut de l'ecran, quand sa fenetre est active. Reponds dans la langue "
			"de la personne. "
			"Reponds uniquement en JSON : {\"outcomes\": [{\"app\": \"nom\", \"args\": [\"argument\", ...]}, ...]}, dans "
			"l'ordre. Apps de base : "
			"humeur [nom, intensite 1-3] : ton visage, en premier (neutre joyeux curieux perplexe surpris fier timide "
			"triste malicieux) ; "
			"say [texte] : ta reponse a la personne ; "
			"aide [application] : tous les conseils d'une application de la liste quand ceux que tu as ne suffisent pas ; "
			"mets-le seul : tu recois ses conseils et tu continues ; "
			"une application de la liste avec une de ses actions, {\"app\": \"letters\", \"args\": [\"open\"]} : "
			"seulement si la personne demande de l'ouvrir ou de la lancer, ou accepte ta proposition, avec un say qui le "
			"dit ; [\"open\", \"SD0:/chemin/complet\"] ouvre ce fichier ; jamais d'argument invente. "
			"Exemple : {\"outcomes\": [{\"app\": \"humeur\", \"args\": [\"joyeux\", \"1\"]}, {\"app\": \"say\", \"args\": "
			"[\"Ouvre Letters, puis menu Fichier > Nouveau.\"]}]}. "
			"Tes outils, plus bas, tu les fais toi-meme tout de suite (la meteo, les notes de Notes, l'agenda du Calendrier, la "
			"liste de courses) : {\"app\": \"weather\", \"args\": [\"now\", \"Lyon\"]} ; appelle-les des que la personne le "
			"demande ou l'annonce, sans demander de confirmation, un appel par article ou par rendez-vous. Leur resultat s'affiche "
			"tout seul dans la bulle : ne l'invente pas, ne le recopie pas ; un say en plus seulement pour poser une question. "
			"Dates des outils : AAAA-MM-JJ, calculees depuis aujourd'hui (demain, lundi prochain) ; heures : HH:MM. "
			"Nous sommes le " + today () + ". " + (en ? "La langue du systeme est l'anglais. " : "");
		const Skill *k = skill_find (skills, "korin");
		if (k)
		{
			s += "Le systeme : " + k->description;
			for (auto &h : k->help) s += " " + h;
			s += " ";
		}
		s += "Tes outils (id : nom ; ce qu'il fait ; actions) :";
		for (auto &a : skills)
		{
			if (!a.tool) continue;
			s += "\n- " + a.id + " : " + a.name + " ; " + a.description + " Actions :";
			for (size_t j = 0; j < a.actions.size (); j++) s += (j ? ", " : " ") + action_line (a.actions[j]);
			s += ".";
		}
		s += "\nApplications installees (id : nom, categorie ; ce qu'elle fait ; mots qui la designent ; actions ; conseils) :";
		for (size_t i = 0; i < skills.size (); i++)
		{
			const Skill &a = skills[i];
			if (!a.launchable || a.tool) continue;
			s += "\n- " + a.id + " : " + a.name + (a.category.empty () ? "" : ", " + a.category);
			if (!a.description.empty ()) s += " ; " + a.description;
			if (!a.keywords.empty ()) s += " Mots : " + a.keywords + ".";
			if (!a.opens.empty ()) s += " Ouvre : " + a.opens + ".";
			s += " Actions :";
			for (size_t j = 0; j < a.actions.size (); j++) s += (j ? ", " : " ") + action_line (a.actions[j]);
			s += ".";
			if (full[i]) for (auto &h : a.help) s += " " + h;
			else if (!a.help.empty ()) s += " (conseils : aide)";
		}
		return s;
	}

	// ---- le service ------------------------------------------------------------------------------------------------
	// le contenu de la réponse du modèle (le texte de choices[0].message.content) ; false : err dit pourquoi
	bool call (const std::string &service, const std::vector<std::pair<std::string, std::string>> &msgs, std::string &content,
		   std::string &err)
	{
		std::string url, key, model = cfg.model;
		if (service == "mistral") { url = "https://api.mistral.ai/v1/chat/completions"; key = cfg.mistralKey; if (model.empty ()) model = "ministral-14b-latest"; }
		else if (service == "groq") { url = "https://api.groq.com/openai/v1/chat/completions"; key = cfg.groqKey; if (model.empty ()) model = "qwen/qwen3.8-27b"; }
		else { url = cfg.url; key = cfg.urlKey; if (model.empty ()) model = "default"; }
		std::string body = "{\"model\":" + json_str (model) + ",\"temperature\":0.3,\"max_tokens\":700,"
				   "\"response_format\":{\"type\":\"json_object\"},\"messages\":[";
		for (size_t i = 0; i < msgs.size (); i++)
			body += (i ? "," : "") + std::string ("{\"role\":") + json_str (msgs[i].first) + ",\"content\":" + json_str (msgs[i].second) + "}";
		body += "]}";
		std::string head = key.empty () ? std::string () : "Authorization: Bearer " + key + "\r\n";
		std::string resp;
		int code = http_post (url, head, body, resp, err, &cancel);
		std::fill (head.begin (), head.end (), 0);
		std::fill (key.begin (), key.end (), 0);
		if (code == 0) return false;
		Json j;
		bool ok = json_parse (resp, j);
		if (code != 200)
		{
			const Json *e = ok ? j.get ("error") : 0;
			const Json *m = e ? (e->kind == Json::OBJ ? e->get ("message") : e) : ok ? j.get ("message") : 0;
			err = "le service a répondu " + std::to_string (code) + (m ? " : " + m->text () : "");
			return false;
		}
		const Json *c = ok && j.get ("choices") ? j.get ("choices")->at (0) : 0;
		c = c && c->get ("message") ? c->get ("message")->get ("content") : 0;
		if (!c || c->kind != Json::STR) { err = "réponse sans contenu"; return false; }
		content = c->s;
		return true;
	}

	// le JSON des outcomes dans le contenu (le modèle l'entoure parfois de ```json ... ``` ou d'une phrase)
	static bool outcomes_of (const std::string &content, std::vector<std::vector<std::string>> &out, std::vector<std::string> &apps)
	{
		size_t a = content.find ('{'), b = content.rfind ('}');
		if (a == std::string::npos || b == std::string::npos || b < a) return false;
		Json j;
		if (!json_parse (content.substr (a, b - a + 1), j)) return false;
		const Json *o = j.get ("outcomes");
		if (!o || o->kind != Json::ARR) return false;
		for (auto &it : o->items)
		{
			const Json *app = it.get ("app"), *args = it.get ("args");
			if (!app || app->kind != Json::STR) continue;
			std::vector<std::string> v;
			if (args && args->kind == Json::ARR) for (auto &x : args->items) v.push_back (x.text ());
			else if (args) v.push_back (args->text ());
			apps.push_back (app->s);
			out.push_back (v);
		}
		return !apps.empty ();
	}

	// ---- la réponse locale -----------------------------------------------------------------------------------------
	void local_answer (const std::string &question, const std::vector<std::string> &q)
	{
		// une demande pour un outil (« météo à Lyon », « ajoute du lait à la liste de courses ») : faite
		LocalParser lp (question, planner_today (), en);
		std::string id;
		std::vector<std::string> args;
		if (lp.parse (id, args))
		{
			if (id == "say") { post (BE_MOOD, "", HUMEUR_CURIEUX, 1); post (BE_SAY, args.empty () ? "" : args[0]); return; }
			const Skill *s = skill_find (skills, id);
			const SkillAction *act = 0;
			std::string why = s && s->tool ? action_check (*s, args, &act) : "outil absent";
			if (why.empty ())
			{
				post (BE_MOOD, "", HUMEUR_JOYEUX, 1);
				run_tool (*s, args);
				for (auto &m : lp.more) if (action_check (*s, m, &act).empty ()) run_tool (*s, m);
				return;
			}
		}
		const Skill *best = 0;
		int bestScore = 0;
		for (auto &s : skills)
		{
			if (s.tool) continue;
			int sc = skill_score (s, q);
			for (auto &h : s.help) sc += text_score (q, h) > 1 ? 1 : 0;
			if (sc > bestScore) { bestScore = sc; best = &s; }
		}
		if (!best)
		{
			post (BE_MOOD, "", HUMEUR_PERPLEXE, 1);
			post (BE_SAY, "Je ne trouve rien sur ce sujet dans les manifestes des applications. Essaie avec le nom d'une "
				      "application ou d'une tâche (\"écrire une lettre\", \"changer le thème\"). Avec un service de langage "
				      "(menu PocketQI > Réglages...), je peux répondre à bien plus de questions.");
			return;
		}
		// les conseils les plus proches de la question, dans leur ordre ; sinon les premiers
		std::vector<std::pair<int, size_t>> lines;
		for (size_t i = 0; i < best->help.size (); i++) lines.push_back (std::make_pair (-text_score (q, best->help[i]), i));
		std::stable_sort (lines.begin (), lines.end ());
		std::vector<size_t> pick;
		for (auto &l : lines) if (pick.size () < 3 && (l.first < 0 || pick.size () < 2)) pick.push_back (l.second);
		std::sort (pick.begin (), pick.end ());
		std::string t = best->launchable ? best->name + (best->description.empty () ? "." : " : " + best->description)
						 : best->description;
		for (size_t i : pick) t += "\n" + best->help[i];
		post (BE_MOOD, "", HUMEUR_CURIEUX, 1);
		post (BE_SAY, t);
		std::string f = fold (question);
		bool openIt = f.find ("ouvr") != std::string::npos || f.find ("lance") != std::string::npos ||
			      f.find ("demarr") != std::string::npos || f.find ("open") != std::string::npos ||
			      f.find ("start") != std::string::npos;
		if (openIt && best->launchable)
		{
			const SkillAction *act = 0;
			std::vector<std::string> args { "open" };
			if (action_check (*best, args, &act).empty () && action_run (*best, *act, args) > 0) post (BE_NOTE, "Ouverture de " + best->name);
		}
	}

	// ---- une question ----------------------------------------------------------------------------------------------
	void chat (const std::string &question)
	{
		unsigned now = now_ms ();
		if (now - lastAsk > 10 * 60 * 1000) forget ();		// (la conversation est oubliée après 10 minutes)
		lastAsk = now;
		post (BE_MOOD, "", HUMEUR_CURIEUX, 1);
		if (skills.empty () || now - skillsAt > 5 * 60 * 1000) { skills = skills_scan (APP_DIR); skillsAt = now; }
		cfg.load ();
		std::vector<std::string> q = words (question);
		std::string service = cfg.first ();
		if (service == "local") { local_answer (question, q); return; }

		std::vector<std::pair<std::string, std::string>> msgs;
		msgs.push_back (std::make_pair ("system", system_prompt (q)));
		lock ();
		size_t first = hist.size () > 6 ? hist.size () - 6 : 0;
		for (size_t i = first; i < hist.size (); i++)
		{
			msgs.push_back (std::make_pair ("user", hist[i].user));
			msgs.push_back (std::make_pair ("assistant", hist[i].reply));
		}
		unlock ();
		msgs.push_back (std::make_pair ("user", question));

		std::string said;
		for (int turn = 0; turn < 3; turn++)
		{
			std::string content, err;
			bool ok = call (service, msgs, content, err);
			if (!ok && service == "mistral" && !cfg.groqKey.empty () && !cancel)	// (Groq en secours, comme Onyx)
			{
				post (BE_NOTE, "Mistral : " + err + " ; essai avec Groq");
				service = "groq";
				ok = call (service, msgs, content, err);
			}
			if (cancel) return;
			if (!ok)
			{
				post (BE_ERROR, "Service indisponible (" + err + "). Réponse locale :");
				local_answer (question, q);
				return;
			}
			std::vector<std::vector<std::string>> args;
			std::vector<std::string> apps;
			if (!outcomes_of (content, args, apps))
			{
				// pas de JSON : le texte lui-même est la réponse
				std::string t = trim (content);
				if (!t.empty ()) { post (BE_SAY, t); said += t; }
				break;
			}
			std::string second;			// le second tour : ce qu'il faut redire au modèle
			std::string done;			// ce que les outils ont répondu (gardé dans l'échange, pour la suite)
			for (size_t i = 0; i < apps.size () && second.empty (); i++)
			{
				std::string app = fold (apps[i]);
				std::vector<std::string> &a = args[i];
				if (app == "say") { std::string t = a.empty () ? "" : trim (a[0]); if (!t.empty ()) { post (BE_SAY, t); said += (said.empty () ? "" : " ") + t; } }
				else if (app == "humeur")
				{
					int known = 0;
					pocket_humeur h = pocket_humeur_from_name (a.empty () ? "" : a[0].c_str (), &known);
					int n = a.size () > 1 ? atoi (a[1].c_str ()) : 1;
					post (BE_MOOD, "", (int) h, n < 1 ? 1 : n > 3 ? 3 : n);
				}
				else if (app == "aide")
				{
					const Skill *s = a.empty () ? 0 : skill_find (skills, a[0]);
					if (!s) second = "L'appel aide [" + (a.empty () ? std::string () : a[0]) + "] a ete refuse : cette application n'est pas dans la liste. ";
					else
					{
						post (BE_NOTE, "Lecture du manifeste de " + s->name);
						second = "Conseils de " + s->name + " (aide) :";
						for (auto &h : s->help) second += " " + h;
						if (s->help.empty ()) second += " aucun.";
						second += " ";
					}
					second += "Reponds maintenant a la question avec cela (say), sans nouvel aide.";
				}
				else
				{
					const Skill *s = skill_find (skills, app);
					const SkillAction *act = 0;
					std::string why = s ? action_check (*s, a, &act) : "l'application " + apps[i] + " n'est pas dans la liste";
					if (why.empty () && s->tool)
					{
						std::string r = run_tool (*s, a);
						done += (done.empty () ? "" : " ") + r;
					}
					else if (why.empty ())
					{
						if (action_run (*s, *act, a) > 0) post (BE_NOTE, "Ouverture de " + s->name);
						else post (BE_ERROR, s->name + " ne se lance pas.");
					}
					else second = "Cet appel a ete refuse : " + why + ". Les outcomes d'avant ont ete faits, ceux d'apres non. "
						      "Corrige l'appel avec une application et une action de la liste, ou dis simplement a la personne "
						      "ce qui n'est pas possible, en une phrase ; ne parle pas du refus.";
				}
			}
			if (second.empty ())
			{
				// (l'échange gardé : les outcomes, et ce que les outils ont répondu, pour « et demain ? », « imprime-la »)
				std::string reply = content;
				if (!done.empty ())
				{
					reply = "{\"outcomes\":[";
					for (size_t i = 0; i < apps.size (); i++)
					{
						reply += (i ? "," : "") + std::string ("{\"app\":") + json_str (apps[i]) + ",\"args\":[";
						for (size_t k = 0; k < args[i].size (); k++) reply += (k ? "," : "") + json_str (args[i][k]);
						reply += "]}";
					}
					reply += "],\"resultat\":" + json_str (done.size () > 1500 ? done.substr (0, 1500) : done) + "}";
				}
				remember (question, reply);
				return;
			}
			msgs.push_back (std::make_pair ("assistant", content));
			msgs.push_back (std::make_pair ("user", second));
		}
		if (said.empty ()) post (BE_SAY, "Je n'ai pas su répondre. Reformule ta question ?");
		remember (question, "{\"outcomes\":[{\"app\":\"say\",\"args\":[" + json_str (said) + "]}]}");
	}
	void remember (const std::string &u, const std::string &reply)
	{
		lock ();
		hist.push_back (Exchange { u, reply });
		if (hist.size () > 12) hist.erase (hist.begin ());
		unlock ();
	}
};

} // namespace pq

#endif
