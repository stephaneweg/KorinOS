//
// tools.h -- PocketQI : ce que l'assistante fait elle-même, au lieu de seulement le décrire. Chaque outil est déclaré
// comme une application, dans un manifeste (data/tools/<id>.txt, posé dans SD0:/apps/pocketqi.app/tools : name,
// description, keywords, llm.<action>), lu par skills.h et vérifié de même ; le LLM l'appelle par un outcome
// {"app": "weather", "args": ["now", "Lyon"]}. Il est fait ici, sur le fil du cerveau, et sa réponse va dans la bulle :
//
//   weather    la météo, par Open-Meteo (gratuit, sans clé) : now [ville], day date [ville], week [ville], city ville
//   memo       les notes de Notes (SD0:/Notes, notesmodel.cpp) : add texte, list, read [mots], print [mots]
//   planner    l'agenda du Calendrier (planner.cpp) : add date texte [heure] [heure], list date [date], print date [date]
//   shopping   la liste de courses, une note de Notes : add article [quantité], remove, check, uncheck, list, clear, print
//
// Imprimer : l'outil rend un titre et un texte, la fenêtre ouvre la fenêtre d'impression du système (PrinterKit) et
// l'imprime (main.cpp) ; rien ne part à l'imprimante sans la personne.
//
// Sans LLM (réponse locale), local_tool comprend les phrases simples en français et en anglais (« météo à Lyon demain »,
// « ajoute du lait à la liste de courses », « qu'est-ce que j'ai cette semaine ? », « note : rappeler Paul ») et en fait
// le même appel.
//
#ifndef POCKETQI_TOOLS_H
#define POCKETQI_TOOLS_H

#include <math.h>
#include <stdlib.h>
#include "config.h"
#include "http.h"
#include "json.h"
#include "planner.h"
#include "skills.h"
#include "../notes/notesmodel.h"

namespace pq {

struct ToolOut
{
	std::string say;			// la réponse, dans la bulle
	std::string note;			// une ligne discrète (la source de la météo)
	std::string printTitle, printText;	// à imprimer : la fenêtre d'impression, sur le fil de la fenêtre
};

// ---- les jours -----------------------------------------------------------------------------------------------------
static int civil_days (int y, int m, int d)		// (Howard Hinnant, comme model.h du Calendrier) 0 = 1970-01-01
{
	y -= m <= 2;
	int era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned) (y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int) doe - 719468;
}
static void days_civil (int z, int &y, int &m, int &d)
{
	z += 719468;
	int era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned) (z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = (int) yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	d = (int) (doy - (153 * mp + 2) / 5 + 1);
	m = (int) (mp < 10 ? mp + 3 : mp - 9);
	y += m <= 2;
}
static int week_day (int dn) { int w = (dn + 3) % 7; return w < 0 ? w + 7 : w; }	// 0 = lundi
static int month_len (int y, int m)
{
	static const int D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	return m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : D[(m - 1) % 12];
}
static const char *const DAY_FR[7] = { "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche" };
static const char *const DAY_EN[7] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
static const char *const MONTH_FR[12] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre",
					   "octobre", "novembre", "décembre" };
static const char *const MONTH_EN[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September",
					   "October", "November", "December" };

static std::string iso_date (int dn)
{
	int y, m, d;
	days_civil (dn, y, m, d);
	char b[16];
	snprintf (b, sizeof b, "%04d-%02d-%02d", y, m, d);
	return b;
}
// "AAAA-MM-JJ" -> jour ; -1 : pas une date
static int parse_iso (const std::string &s)
{
	if (s.size () != 10 || s[4] != '-' || s[7] != '-') return -1;
	int y = atoi (s.c_str ()), m = atoi (s.c_str () + 5), d = atoi (s.c_str () + 8);
	if (y < 1900 || m < 1 || m > 12 || d < 1 || d > month_len (y, m)) return -1;
	return civil_days (y, m, d);
}
// "HH:MM" (ou "H:MM", "14h30") -> minutes ; -1 : pas une heure
static int parse_hm (const std::string &s)
{
	int h = 0, m = 0, i = 0, n = (int) s.size ();
	if (!n || s[0] < '0' || s[0] > '9') return -1;
	while (i < n && s[i] >= '0' && s[i] <= '9') h = h * 10 + (s[i++] - '0');
	if (i < n && (s[i] == ':' || s[i] == 'h' || s[i] == 'H'))
	{
		i++;
		int k = 0;
		while (i < n && s[i] >= '0' && s[i] <= '9') { m = m * 10 + (s[i++] - '0'); k++; }
		if (k != 0 && k != 2) return -1;
	}
	if (i != n || h > 23 || m > 59) return -1;
	return h * 60 + m;
}
static std::string hm_text (int min)
{
	char b[8];
	snprintf (b, sizeof b, "%d:%02d", min / 60, min % 60);
	return b;
}
// « aujourd'hui », « demain », « lundi 9 novembre » (« Monday 9 November »), l'année si ce n'est pas celle d'aujourd'hui
static std::string day_label (int dn, bool en, bool relative = true)
{
	int today = planner_today ();
	if (relative && dn == today) return en ? "today" : "aujourd'hui";
	if (relative && dn == today + 1) return en ? "tomorrow" : "demain";
	int y, m, d, ty, tm, td;
	days_civil (dn, y, m, d);
	days_civil (today, ty, tm, td);
	std::string s = std::string (en ? DAY_EN[week_day (dn)] : DAY_FR[week_day (dn)]) + " " + std::to_string (d) + " " +
			(en ? MONTH_EN[m - 1] : MONTH_FR[m - 1]);
	if (y != ty) s += " " + std::to_string (y);
	return s;
}
// les articles en tête d'un nom : « du lait », « des œufs », « de l'huile », « some milk » -> le nom
static std::string strip_article (std::string s)
{
	static const char *const ART[] = { "du ", "de la ", "de l'", "des ", "d'", "un ", "une ", "le ", "la ", "les ", "l'", "some ", "a ",
					   "an ", "the ", "de ", 0 };
	for (bool again = true; again;)
	{
		again = false;
		std::string f = fold (s);
		for (int i = 0; ART[i]; i++)
			if (f.compare (0, strlen (ART[i]), ART[i]) == 0 && f.size () > strlen (ART[i])) { s = trim (s.substr (strlen (ART[i]))); again = true; break; }
	}
	return s;
}
static std::string cap_first (const std::string &s)
{
	if (s.empty () || s[0] < 'a' || s[0] > 'z') return s;
	return std::string (1, (char) (s[0] - 32)) + s.substr (1);
}

// ---- la météo (Open-Meteo) -----------------------------------------------------------------------------------------
static std::string url_enc (const std::string &s)
{
	static const char *const H = "0123456789ABCDEF";
	std::string o;
	for (unsigned char c : s)
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_') o += (char) c;
		else { o += '%'; o += H[c >> 4]; o += H[c & 15]; }
	return o;
}
// le code WMO d'Open-Meteo, en mots
static std::string wmo_text (int c, bool en)
{
	struct W { int lo, hi; const char *fr, *en; };
	static const W T[] = {
		{ 0, 0, "ciel dégagé", "clear sky" }, { 1, 1, "plutôt dégagé", "mainly clear" }, { 2, 2, "partiellement nuageux", "partly cloudy" },
		{ 3, 3, "couvert", "overcast" }, { 45, 48, "brouillard", "fog" }, { 51, 55, "bruine", "drizzle" },
		{ 56, 57, "bruine verglaçante", "freezing drizzle" }, { 61, 61, "pluie faible", "light rain" }, { 63, 63, "pluie", "rain" },
		{ 65, 65, "forte pluie", "heavy rain" }, { 66, 67, "pluie verglaçante", "freezing rain" }, { 71, 71, "neige faible", "light snow" },
		{ 73, 73, "neige", "snow" }, { 75, 75, "forte neige", "heavy snow" }, { 77, 77, "grésil", "snow grains" },
		{ 80, 82, "averses", "showers" }, { 85, 86, "averses de neige", "snow showers" }, { 95, 95, "orage", "thunderstorm" },
		{ 96, 99, "orage avec grêle", "thunderstorm with hail" } };
	for (auto &w : T) if (c >= w.lo && c <= w.hi) return en ? w.en : w.fr;
	return en ? "unknown weather" : "temps inconnu";
}
static long num_round (const Json *j) { return j ? lround (strtod (j->text ().c_str (), 0)) : 0; }

class WeatherTool
{
public:
	WeatherTool (Config &c, bool e, volatile int *k) : cfg (c), en (e), cancel (k) {}

	ToolOut run (const std::string &act, const std::vector<std::string> &a)
	{
		ToolOut o;
		if (act == "city")
		{
			cfg.city = a.empty () ? "" : trim (a[0]);
			if (cfg.city.empty () || !cfg.save ()) o.say = en ? "I could not save your town." : "Je n'ai pas pu retenir ta ville.";
			else o.say = en ? "Noted: you live in " + cfg.city + "." : "C'est noté : tu habites à " + cfg.city + ".";
			return o;
		}
		int day = -1;
		std::string city;
		if (act == "day") { day = a.empty () ? -1 : parse_iso (a[0]); city = a.size () > 1 ? a[1] : ""; }
		else city = a.empty () ? "" : a[0];
		city = trim (city);
		if (city.empty ()) city = cfg.city;
		if (city.empty ())
		{
			o.say = en ? "Which town? Tell me where you live and I will remember it."
				   : "Pour quelle ville ? Dis-moi où tu habites, et je m'en souviendrai.";
			return o;
		}
		int today = planner_today ();
		if (act == "day" && (day < today || day > today + 6))
		{
			o.say = en ? "I only have forecasts from today to six days ahead." : "Je n'ai les prévisions que d'aujourd'hui à six jours plus tard.";
			return o;
		}
		// la ville -> ses coordonnées
		std::string base = cfg.weatherUrl;
		while (!base.empty () && base.back () == '/') base.pop_back ();
		std::string geo = (base.empty () ? std::string ("https://geocoding-api.open-meteo.com") : base) + "/v1/search?count=1&format=json&language=" +
				  (en ? "en" : "fr") + "&name=" + url_enc (city);
		std::string body, err;
		Json g;
		int code = http_get (geo, body, err, cancel);
		if (code != 200 || !json_parse (body, g)) { o.say = unreachable (code, err); return o; }
		const Json *r = g.get ("results") ? g.get ("results")->at (0) : 0;
		if (!r || !r->get ("latitude") || !r->get ("longitude"))
		{
			o.say = en ? "I cannot find the town " + city + "." : "Je ne trouve pas la ville " + city + ".";
			return o;
		}
		std::string name = r->get ("name") ? r->get ("name")->text () : city;
		std::string fc = (base.empty () ? std::string ("https://api.open-meteo.com") : base) + "/v1/forecast?latitude=" +
				 r->get ("latitude")->text () + "&longitude=" + r->get ("longitude")->text () +
				 "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
				 "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,wind_speed_10m_max"
				 "&timezone=auto&forecast_days=7";
		Json m;
		code = http_get (fc, body, err, cancel);
		if (code != 200 || !json_parse (body, m)) { o.say = unreachable (code, err); return o; }
		o.note = en ? "Weather data: Open-Meteo.com" : "Données météo : Open-Meteo.com";
		if (act == "now" || act.empty ())
		{
			const Json *c = m.get ("current");
			if (!c || !c->get ("temperature_2m")) { o.say = bad (); return o; }
			long t = num_round (c->get ("temperature_2m")), f = num_round (c->get ("apparent_temperature"));
			std::string w = wmo_text ((int) num_round (c->get ("weather_code")), en);
			long v = num_round (c->get ("wind_speed_10m"));
			o.say = en ? "In " + name + " right now: " + w + ", " + std::to_string (t) + " °C (feels like " + std::to_string (f) +
					     " °C), wind " + std::to_string (v) + " km/h."
				   : "À " + name + ", en ce moment : " + w + ", " + std::to_string (t) + " °C (ressenti " + std::to_string (f) +
					     " °C), vent " + std::to_string (v) + " km/h.";
			return o;
		}
		const Json *d = m.get ("daily");
		const Json *days = d ? d->get ("time") : 0;
		if (!days || days->kind != Json::ARR || days->items.empty ()) { o.say = bad (); return o; }
		if (act == "day")
		{
			for (size_t i = 0; i < days->items.size (); i++)
				if (parse_iso (days->items[i].text ()) == day)
				{
					o.say = (en ? "In " + name + ", " : "À " + name + ", ") + day_label (day, en) + (en ? ": " : " : ") + daily (d, i) + ".";
					return o;
				}
			o.say = en ? "I have no forecast for that day." : "Je n'ai pas de prévision pour ce jour-là.";
			return o;
		}
		o.say = en ? "Weather in " + name + ":" : "Météo à " + name + " :";
		for (size_t i = 0; i < days->items.size (); i++)
		{
			int dn = parse_iso (days->items[i].text ());
			if (dn < 0) continue;
			o.say += "\n" + cap_first (day_label (dn, en)) + (en ? ": " : " : ") + daily (d, i);
		}
		return o;
	}

private:
	Config &cfg;
	bool en;
	volatile int *cancel;

	std::string unreachable (int code, const std::string &err)
	{
		if (code == 0) return (en ? "I cannot reach the weather service (" : "Je n'arrive pas à joindre le service météo (") + err + ").";
		return en ? "The weather service answered " + std::to_string (code) + "." : "Le service météo a répondu " + std::to_string (code) + ".";
	}
	std::string bad () { return en ? "The weather service gave an answer I cannot read." : "Le service météo ne m'a pas répondu correctement."; }
	// « pluie, 8 à 14 °C, pluie 80 %, vent 30 km/h »
	std::string daily (const Json *d, size_t i)
	{
		auto at = [&] (const char *k) { const Json *a = d->get (k); return a ? a->at (i) : (const Json *) 0; };
		std::string s = wmo_text ((int) num_round (at ("weather_code")), en) + ", " + std::to_string (num_round (at ("temperature_2m_min"))) +
				(en ? " to " : " à ") + std::to_string (num_round (at ("temperature_2m_max"))) + " °C";
		if (at ("precipitation_probability_max") && at ("precipitation_probability_max")->kind == Json::NUM)
			s += (en ? ", rain " : ", pluie ") + std::to_string (num_round (at ("precipitation_probability_max"))) + " %";
		s += (en ? ", wind " : ", vent ") + std::to_string (num_round (at ("wind_speed_10m_max"))) + " km/h";
		return s;
	}
};

// ---- les notes (celles de Notes) -----------------------------------------------------------------------------------
static Notes *notes_all ()
{
	static Notes *s = new Notes;
	notes_scan (*s);
	return s;
}
static std::string note_text (const char *file)
{
	static char buf[NOTE_MAX_BYTES + 1];
	int n = notes_read (file, buf, sizeof buf);
	return n < 0 ? std::string () : std::string (buf, (size_t) n);
}
// une note écrite (une nouvelle si file est vide) -> son nom, "" : l'écriture a échoué
static std::string note_put (Notes &s, const std::string &file, const std::string &text)
{
	char name[NOTE_FILE];
	long long now = notes_now ();
	if (file.empty ()) { if (!notes_new_name (s, name, sizeof name, now)) return ""; }
	else snprintf (name, sizeof name, "%s", file.c_str ());
	if (notes_write (s, name, text.data (), (int) text.size (), now) != 0) return "";
	notes_save_ini (s);
	service_poke ("stickies", 2);			// (une note épinglée sur le bureau : Stickies la relit)
	return name;
}
// la note la plus proche de ces mots (son titre compte plus que son texte) ; -1 : aucune
static int note_find (Notes &s, const std::string &q)
{
	std::vector<std::string> w = words (q);
	int best = -1, bestScore = 0;
	for (int i = 0; i < s.count; i++)
	{
		int sc = 3 * text_score (w, s.n[i].title) + text_score (w, s.n[i].preview);
		if (sc == 0 && i < 64) sc = text_score (w, note_text (s.n[i].file));	// (le texte entier : les 64 plus récentes)
		if (sc > bestScore) { bestScore = sc; best = i; }
	}
	return best;
}

static ToolOut memo_run (const std::string &act, const std::vector<std::string> &a, bool en)
{
	ToolOut o;
	Notes &s = *notes_all ();
	std::string q = a.empty () ? "" : trim (a[0]);
	if (act == "add")
	{
		if (q.empty ()) { o.say = en ? "What should the note say?" : "Que dois-je écrire dans la note ?"; return o; }
		if (note_put (s, "", q).empty ()) o.say = en ? "The note could not be written." : "La note n'a pas pu être écrite.";
		else o.say = en ? "Noted in Notes: \"" + q + "\"." : "C'est noté dans Notes : « " + q + " ».";
		return o;
	}
	if (s.count == 0) { o.say = en ? "You have no notes yet." : "Tu n'as encore aucune note."; return o; }
	if (act == "list")
	{
		o.say = en ? "Your notes (" + std::to_string (s.count) + "):" : "Tes notes (" + std::to_string (s.count) + ") :";
		for (int i = 0; i < s.count && i < 20; i++)
			o.say += "\n- " + std::string (s.n[i].title[0] ? s.n[i].title : (en ? "(empty)" : "(vide)"));
		if (s.count > 20) o.say += en ? "\n..." : "\n…";
		return o;
	}
	// read, print : une note (des mots), ou les plus récentes
	std::vector<int> pick;
	if (!q.empty ())
	{
		int i = note_find (s, q);
		if (i < 0) { o.say = en ? "I find no note about \"" + q + "\"." : "Je ne trouve aucune note qui parle de « " + q + " »."; return o; }
		pick.push_back (i);
	}
	else for (int i = 0; i < s.count && i < (act == "print" ? NOTES_MAX : 5); i++) pick.push_back (i);
	std::string all;
	for (int i : pick)
	{
		std::string t = trim (note_text (s.n[i].file));
		if (act == "read" && t.size () > 1500) t = t.substr (0, 1500) + "…";
		if (!all.empty ()) all += "\n\n";
		all += t;
	}
	if (act == "print")
	{
		o.printTitle = pick.size () == 1 ? std::string (s.n[pick[0]].title) : std::string ("Notes");
		o.printText = all;
		o.say = en ? "Here is the print window." : "Voici la fenêtre d'impression.";
		return o;
	}
	o.say = all.empty () ? (en ? "This note is empty." : "Cette note est vide.") : all;
	if (q.empty () && s.count > 5) o.note = en ? "The five most recent notes" : "Les cinq notes les plus récentes";
	return o;
}

// ---- la liste de courses (une note de Notes) ------------------------------------------------------------------------
struct ShopItem { bool done; std::string name, qty; };

class ShoppingTool
{
public:
	explicit ShoppingTool (bool e) : en (e) {}

	ToolOut run (const std::string &act, const std::vector<std::string> &a)
	{
		ToolOut o;
		load ();
		std::string arg = a.empty () ? "" : strip_article (trim (a[0]));
		if (act == "list" || act == "print")
		{
			if (items.empty ()) { o.say = en ? "The shopping list is empty." : "La liste de courses est vide."; return o; }
			if (act == "print")
			{
				o.printTitle = title ();
				for (auto &it : items) o.printText += std::string (it.done ? "\xE2\x98\x91 " : "\xE2\x98\x90 ") + label (it) + "\n";
				o.say = en ? "Here is the print window." : "Voici la fenêtre d'impression.";
				return o;
			}
			int left = 0;
			for (auto &it : items) left += !it.done;
			o.say = en ? "The shopping list (" + std::to_string (left) + " to buy):" : "La liste de courses (" + std::to_string (left) + " à acheter) :";
			for (auto &it : items) o.say += "\n- " + label (it) + (it.done ? (en ? " (in the basket)" : " (dans le panier)") : "");
			return o;
		}
		if (act == "clear")
		{
			std::string f = fold (arg);
			bool doneOnly = f.find ("coch") != std::string::npos || f.find ("check") != std::string::npos || f.find ("achet") != std::string::npos ||
					f.find ("bought") != std::string::npos || f.find ("done") != std::string::npos;
			std::vector<ShopItem> keep;
			if (doneOnly) for (auto &it : items) if (!it.done) keep.push_back (it);
			items.swap (keep);
			o.say = save () ? (doneOnly ? (en ? "The ticked items are off the list." : "J'ai retiré les articles cochés.")
						    : (en ? "The shopping list is empty now." : "La liste de courses est vide."))
					: failed ();
			return o;
		}
		if (arg.empty ()) { o.say = en ? "Which item?" : "Quel article ?"; return o; }
		int k = find (arg);
		if (act == "add")
		{
			std::string qty = a.size () > 1 ? trim (a[1]) : "";
			if (k >= 0)
			{
				if (!qty.empty ()) items[k].qty = qty;
				bool was = items[k].done;
				items[k].done = false;
				o.say = save () ? (was ? (en ? "Back on the list: " : "Je le remets sur la liste : ") + label (items[k]) + "."
						       : (en ? "Already on the list: " : "C'est déjà sur la liste : ") + label (items[k]) + ".")
						: failed ();
				return o;
			}
			items.push_back (ShopItem { false, arg, qty });
			o.say = save () ? (en ? "Added to the shopping list: " : "J'ajoute à la liste : ") + label (items.back ()) + "." : failed ();
			return o;
		}
		if (k == -2) { o.say = (en ? "Several items match: " : "Plusieurs articles y ressemblent : ") + matches (arg) + (en ? ". Which one?" : ". Lequel ?"); return o; }
		if (k < 0) { o.say = en ? arg + " is not on the list." : arg + " n'est pas sur la liste."; return o; }
		std::string l = label (items[k]);
		if (act == "remove") { items.erase (items.begin () + k); o.say = save () ? (en ? "Removed: " : "Je retire : ") + l + "." : failed (); }
		else if (act == "check") { items[k].done = true; o.say = save () ? (en ? "Ticked: " : "Coché : ") + l + "." : failed (); }
		else if (act == "uncheck") { items[k].done = false; o.say = save () ? (en ? "Unticked: " : "Décoché : ") + l + "." : failed (); }
		return o;
	}

private:
	bool en;
	std::string file;			// la note de la liste ("" : pas encore faite)
	std::vector<ShopItem> items;

	std::string title () const { return en ? "Shopping list" : "Liste de courses"; }
	std::string failed () const { return en ? "The list could not be written." : "La liste n'a pas pu être écrite."; }
	static std::string label (const ShopItem &it) { return it.qty.empty () ? it.name : it.name + " (" + it.qty + ")"; }

	// la note dont le titre est « Liste de courses » (ou « Shopping list », « Courses ») ; ses lignes en articles
	void load ()
	{
		Notes &s = *notes_all ();
		file.clear ();
		items.clear ();
		for (int i = 0; i < s.count && file.empty (); i++)
		{
			std::string t = fold (trim (s.n[i].title));
			if (t == "liste de courses" || t == "shopping list" || t == "courses" || t == "liste des courses") file = s.n[i].file;
		}
		if (file.empty ()) return;
		std::string t = note_text (file.c_str ());
		size_t p = t.find ('\n');			// (la première ligne : le titre)
		while (p != std::string::npos && p < t.size ())
		{
			size_t e = t.find ('\n', p + 1);
			std::string l = trim (t.substr (p + 1, e == std::string::npos ? std::string::npos : e - p - 1));
			p = e;
			if (l.empty ()) continue;
			ShopItem it { false, "", "" };
			if (l.compare (0, 3, "[ ]") == 0) l = trim (l.substr (3));
			else if (l.compare (0, 3, "[x]") == 0 || l.compare (0, 3, "[X]") == 0) { it.done = true; l = trim (l.substr (3)); }
			else if (l.compare (0, 3, "\xE2\x98\x90") == 0) l = trim (l.substr (3));
			else if (l.compare (0, 3, "\xE2\x98\x91") == 0 || l.compare (0, 3, "\xE2\x9C\x93") == 0) { it.done = true; l = trim (l.substr (3)); }
			else if (l[0] == '-' || l[0] == '*') l = trim (l.substr (1));
			size_t a = l.rfind (" ("), b = l.size ();
			if (a != std::string::npos && l[b - 1] == ')') { it.qty = l.substr (a + 2, b - a - 3); l = trim (l.substr (0, a)); }
			if (l.empty ()) continue;
			it.name = l;
			items.push_back (it);
		}
	}
	bool save ()
	{
		std::string t = title () + "\n";
		if (!file.empty ())				// (le titre gardé tel qu'il est écrit)
		{
			std::string old = note_text (file.c_str ());
			size_t e = old.find ('\n');
			if (!trim (old.substr (0, e)).empty ()) t = old.substr (0, e) + "\n";
		}
		for (auto &it : items) t += std::string (it.done ? "[x] " : "[ ] ") + label (it) + "\n";
		std::string f = note_put (*notes_all (), file, t);
		if (f.empty ()) return false;
		file = f;
		return true;
	}
	// l'article : -1 aucun, -2 plusieurs, sinon son rang
	int find (const std::string &name) const
	{
		std::string f = fold (name);
		for (size_t i = 0; i < items.size (); i++) if (fold (items[i].name) == f) return (int) i;
		std::vector<std::string> w = words (name);
		int found = -1, n = 0;
		for (size_t i = 0; i < items.size (); i++)
		{
			std::string g = fold (items[i].name);
			bool hit = g.find (f) != std::string::npos || f.find (g) != std::string::npos;
			if (!hit && !w.empty ()) hit = text_score (w, items[i].name) >= (int) w.size ();
			if (hit) { found = (int) i; n++; }
		}
		return n > 1 ? -2 : found;
	}
	std::string matches (const std::string &name) const
	{
		std::string f = fold (name), s;
		for (auto &it : items)
		{
			std::string g = fold (it.name);
			if (g.find (f) != std::string::npos || f.find (g) != std::string::npos || text_score (words (name), it.name) > 0)
				s += (s.empty () ? "" : ", ") + it.name;
		}
		return s;
	}
};

// ---- l'agenda (celui du Calendrier) ----------------------------------------------------------------------------------
static std::string planner_text (int d0, int d1, bool en, bool forPrint)
{
	std::vector<PlannerItem> v;
	if (!planner_list (d0, d1, v)) return en ? "I cannot read the calendar." : "Je n'arrive pas à lire le calendrier.";
	std::string range = d0 == d1 ? day_label (d0, en) : (en ? "from " : "du ") + day_label (d0, en, false) + (en ? " to " : " au ") +
			    day_label (d1, en, false);
	if (v.empty ()) return en ? "Nothing planned " + (d0 == d1 ? range : range) + "." : "Rien de prévu " + range + ".";
	std::string s = d0 == d1 ? cap_first (range) + (en ? ", you have:" : ", tu as :") : cap_first (range) + (en ? ":" : " :");
	if (forPrint) s = cap_first (range);
	int last = -1;
	for (auto &it : v)
	{
		if (d0 != d1 && it.day != last) { s += "\n\n" + cap_first (day_label (it.day, en)); last = it.day; }
		s += "\n";
		if (d0 != d1) s += "  ";
		s += it.start < 0 ? (en ? "all day  " : "journée  ") : hm_text (it.start) + (it.end > it.start ? "-" + hm_text (it.end % 1440) : "") + "  ";
		s += it.title;
		if (!it.place.empty ()) s += ", " + it.place;
	}
	return s;
}

static ToolOut planner_run (const std::string &act, const std::vector<std::string> &a, bool en)
{
	ToolOut o;
	int d0 = a.empty () ? -1 : parse_iso (a[0]);
	if (d0 < 0) { o.say = en ? "Which day?" : "Pour quel jour ?"; return o; }
	if (act == "add")
	{
		std::string t = a.size () > 1 ? trim (a[1]) : "";
		int s = a.size () > 2 ? parse_hm (a[2]) : -1, e = a.size () > 3 ? parse_hm (a[3]) : -1;
		std::string why = planner_add (d0, t, s, e);
		if (!why.empty ()) { o.say = (en ? "The appointment could not be added (" : "Le rendez-vous n'a pas pu être ajouté (") + why + ")."; return o; }
		std::string when = day_label (d0, en) + (s < 0 ? (en ? ", all day" : ", toute la journée") : (en ? " at " : " à ") + hm_text (s));
		if (s >= 0 && e > s) when += (en ? " to " : " à ") + hm_text (e);
		if (s >= 0 && e > s && !en) when = day_label (d0, en) + " de " + hm_text (s) + " à " + hm_text (e);
		o.say = en ? "Added to the Calendar: " + t + ", " + when + "." : "C'est noté au Calendrier : " + t + ", " + when + ".";
		return o;
	}
	int d1 = a.size () > 1 ? parse_iso (a[1]) : d0;
	if (d1 < 0) d1 = d0;
	if (d1 < d0) std::swap (d0, d1);
	if (d1 - d0 > 62) d1 = d0 + 62;
	if (act == "print")
	{
		o.printTitle = en ? "Agenda" : "Agenda";
		o.printText = planner_text (d0, d1, en, true);
		o.say = en ? "Here is the print window." : "Voici la fenêtre d'impression.";
		return o;
	}
	o.say = planner_text (d0, d1, en, false);
	return o;
}

// ---- un appel d'outil ------------------------------------------------------------------------------------------------
// args : [action, arguments...], déjà vérifiés contre le manifeste (action_check)
static ToolOut tool_run (const std::string &id, const std::vector<std::string> &args, Config &cfg, bool en, volatile int *cancel)
{
	std::string act = args.empty () ? "" : fold (args[0]);
	std::vector<std::string> a (args.size () > 1 ? args.begin () + 1 : args.end (), args.end ());
	if (id == "weather") return WeatherTool (cfg, en, cancel).run (act.empty () ? "now" : act, a);
	if (id == "memo") return memo_run (act.empty () ? "list" : act, a, en);
	if (id == "planner") return planner_run (act.empty () ? "list" : act, a, en);
	if (id == "shopping") return ShoppingTool (en).run (act.empty () ? "list" : act, a);
	ToolOut o;
	o.say = en ? "I do not know that tool." : "Je ne connais pas cet outil.";
	return o;
}

// ---- les phrases, sans LLM ----------------------------------------------------------------------------------------
// Une phrase simple -> un appel d'outil (id, [action, arguments...]) ; id "say" : une question à renvoyer (args[0]).
// false : ce n'est pas une demande pour un outil (une question sur une app : la réponse locale des manifestes).
struct Tok { std::string o, f; bool used = false, elided = false; };

class LocalParser
{
public:
	LocalParser (const std::string &q, int today, bool e) : today (today), en (e) { split (q); }

	bool parse (std::string &id, std::vector<std::string> &args)
	{
		if (t.empty ()) return false;
		static const char *const HOW[] = { "comment", "how", "pourquoi", "why", "explique", "explain", 0 };
		if (in (t[0].f, HOW)) return false;
		static const char *const SHOP[] = { "courses", "shopping", "grocery", "groceries", "achats", "commissions", 0 };
		static const char *const PLAN[] = { "rendez-vous", "rdv", "agenda", "calendrier", "calendar", "appointment", "appointments",
						    "meeting", "meetings", "reunion", "schedule", "planning", "prevu", "prevus", "planned", 0 };
		static const char *const NOTE[] = { "note", "notes", "noter", "notez", "memo", "memos", "pense-bete", 0 };
		if (any (SHOP)) return shopping (id, args);
		if (any (PLAN) || has_phrase ("qu est-ce que j ai") || has_phrase ("j ai quoi") || has_phrase ("what do i have"))
			return planner (id, args);
		if (weather_words () || has ("habite") || has_phrase ("i live in") || has_phrase ("ma ville est") || has_phrase ("my town is"))
			return weather (id, args);
		if (any (NOTE) || has_phrase ("write down")) return memo (id, args);
		return false;
	}

private:
	std::vector<Tok> t;
	int today;
	bool en;

	// les mots : espaces et ponctuation coupent ; « l'agenda » -> « l' » (élidé) + « agenda » ; « aujourd'hui » reste entier
	void split (const std::string &q)
	{
		std::string w;
		auto push = [&] () {
			if (w.empty ()) return;
			Tok k;
			k.o = w;
			k.f = fold (w);
			t.push_back (k);
			w.clear ();
		};
		for (size_t i = 0; i < q.size (); i++)
		{
			unsigned char c = (unsigned char) q[i];
			bool apo = c == '\'' || (c == 0xE2 && i + 2 < q.size () && (unsigned char) q[i + 1] == 0x80 && (unsigned char) q[i + 2] == 0x99);
			if (apo)
			{
				if (fold (w) == "aujourd") { w += '\''; if (c == 0xE2) i += 2; continue; }
				w += '\'';
				if (c == 0xE2) i += 2;
				push ();
				t.back ().elided = true;
				t.back ().f = t.back ().f.substr (0, t.back ().f.size () - 1);
				continue;
			}
			// (« : » garde les heures, « / » les dates, « - » les mots composés ; « . » et « , » coupent sauf entre chiffres)
			bool digitAround = i > 0 && i + 1 < q.size () && isdigit ((unsigned char) q[i - 1]) && isdigit ((unsigned char) q[i + 1]);
			if (c == ' ' || c == '\t' || c == '\n' || c == '?' || c == '!' || c == ';' || c == '"' || c == '(' || c == ')' ||
			    ((c == ',' || c == '.') && !digitAround) || (c == ':' && !digitAround))
			{
				push ();
				if (c == ',') { Tok k; k.o = ","; k.f = ","; t.push_back (k); }
				if (c == ':' ) { Tok k; k.o = ":"; k.f = ":"; t.push_back (k); }
				continue;
			}
			if (c == 0xC2 && i + 1 < q.size () && ((unsigned char) q[i + 1] == 0xAB || (unsigned char) q[i + 1] == 0xBB || (unsigned char) q[i + 1] == 0xA0)) { push (); i++; continue; }
			w += (char) c;
		}
		push ();
	}
	static bool in (const std::string &f, const char *const *list) { for (int i = 0; list[i]; i++) if (f == list[i]) return true; return false; }
	bool any (const char *const *list) const { for (auto &k : t) if (in (k.f, list)) return true; return false; }
	int index_of (const char *const *list, size_t from = 0) const { for (size_t i = from; i < t.size (); i++) if (in (t[i].f, list)) return (int) i; return -1; }
	bool has_phrase (const char *p) const
	{
		std::string all;
		for (auto &k : t) all += k.f + " ";
		return all.find (p) != std::string::npos;
	}
	bool has (const char *w) const { for (auto &k : t) if (k.f == w) return true; return false; }
	bool starts_any (const char *const *roots) const
	{
		for (auto &k : t) for (int i = 0; roots[i]; i++) if (k.f.compare (0, strlen (roots[i]), roots[i]) == 0) return true;
		return false;
	}

	// ---- les verbes ----
	enum { V_NONE, V_ADD, V_REMOVE, V_CHECK, V_UNCHECK, V_CLEAR, V_PRINT, V_READ };
	int verb (int *at = 0) const
	{
		static const char *const PRINT[] = { "imprime", "imprimer", "imprimes", "imprimez", "print", 0 };
		static const char *const UNCHECK[] = { "decoche", "decocher", "decoches", "uncheck", "untick", 0 };
		static const char *const CHECK[] = { "coche", "cocher", "coches", "cochez", "achete", "pris", "check", "tick", "bought", "got", 0 };
		static const char *const REMOVE[] = { "retire", "retirer", "retires", "enleve", "enlever", "enleves", "supprime", "supprimer",
						      "ote", "oter", "raye", "rayer", "remove", "delete", "erase", 0 };
		static const char *const CLEAR[] = { "vide", "vider", "videz", "clear", "empty", "reset", 0 };
		static const char *const ADD[] = { "ajoute", "ajouter", "ajoutes", "ajoutez", "rajoute", "rajouter", "mets", "mettre", "met",
						   "note", "noter", "notez", "inscris", "inscrire", "ecris", "ecrire", "cree", "creer",
						   "prends", "acheter", "add", "put", "write", "create", "schedule", "book", "buy", "need", "besoin",
						   "nouvelle", "nouveau", "new", "plan", "prevois", 0 };
		static const char *const READ[] = { "lis", "lire", "montre", "montrer", "affiche", "afficher", "dis", "donne", "quels",
						    "quelles", "quel", "quelle", "read", "show", "list", "tell", "what", "liste", 0 };
		struct V { const char *const *w; int v; } order[] = { { PRINT, V_PRINT }, { UNCHECK, V_UNCHECK }, { CLEAR, V_CLEAR },
			{ REMOVE, V_REMOVE }, { CHECK, V_CHECK }, { ADD, V_ADD }, { READ, V_READ } };
		// (le premier verbe de la phrase ; « montre-moi la liste pour l'imprimer » : imprimer l'emporte sur lire)
		int bestAt = 1 << 30, best = V_NONE;
		for (auto &o : order) { int i = index_of (o.w); if (i >= 0 && i < bestAt) { bestAt = i; best = o.v; } }
		int p = index_of (PRINT);
		if (best == V_READ && p >= 0) { best = V_PRINT; bestAt = p; }
		if (at) *at = best == V_NONE ? -1 : bestAt;
		return best;
	}

	// ---- les jours et les heures ----
	static int month_of (const std::string &f)
	{
		static const char *const M[] = { "janvier", "fevrier", "mars", "avril", "mai", "juin", "juillet", "aout", "septembre", "octobre",
						 "novembre", "decembre", "january", "february", "march", "april", "may", "june", "july", "august",
						 "september", "october", "november", "december", 0 };
		for (int i = 0; M[i]; i++)
			if (f == M[i] || (f.size () >= 3 && strncmp (M[i], f.c_str (), f.size ()) == 0 && (f.back () == '.' || f.size () >= 4))) return i % 12 + 1;
		std::string g = f;
		if (!g.empty () && g.back () == '.') g.pop_back ();
		static const char *const S[] = { "jan", "janv", "fev", "fevr", "mar", "avr", "apr", "jun", "jul", "juil", "aug", "sep", "sept", "oct", "nov", "dec", 0 };
		static const int SM[] = { 1, 1, 2, 2, 3, 4, 4, 6, 7, 7, 8, 9, 9, 10, 11, 12 };
		for (int i = 0; S[i]; i++) if (g == S[i]) return SM[i];
		return 0;
	}
	static int weekday_of (const std::string &f)
	{
		static const char *const D[] = { "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche", "monday", "tuesday",
						 "wednesday", "thursday", "friday", "saturday", "sunday", 0 };
		for (int i = 0; D[i]; i++) if (f == D[i]) return i % 7;
		return -1;
	}
	static int number (const std::string &f)		// "12", "12e", "1er", "12th" -> 12 ; -1
	{
		if (f.empty () || !isdigit ((unsigned char) f[0])) return -1;
		int n = 0;
		size_t i = 0;
		while (i < f.size () && isdigit ((unsigned char) f[i])) n = n * 10 + (f[i++] - '0');
		std::string r = f.substr (i);
		if (!r.empty () && r != "e" && r != "er" && r != "eme" && r != "st" && r != "nd" && r != "rd" && r != "th") return -1;
		return n;
	}
	// un jour de ce mois-ci ou du suivant (passé : l'an prochain pour un mois donné)
	int date_of (int d, int m)
	{
		int y, mm, dd;
		days_civil (today, y, mm, dd);
		if (m == 0)
		{
			m = mm;
			if (d < dd) { m++; if (m > 12) { m = 1; y++; } }
		}
		else if (m < mm || (m == mm && d < dd)) y++;
		if (d < 1 || d > month_len (y, m)) return -1;
		return civil_days (y, m, d);
	}
	void use_before (int i)		// les petits mots devant un jour ou une heure (« le », « à », « pour »...)
	{
		static const char *const P[] = { "le", "la", "ce", "cette", "on", "the", "this", "pour", "for", "a", "at", "vers", "de", "from",
						 "jusqu", "to", "until", "des", "dès", "du", "au", "next", "prochain", "prochaine", "matin", 0 };
		while (i > 0 && !t[i - 1].used && (in (t[i - 1].f, P) || (t[i - 1].elided && (t[i - 1].f == "jusqu" || t[i - 1].f == "l" || t[i - 1].f == "d"))))
			t[--i].used = true;
	}
	// le premier jour dit (et le dernier : « cette semaine », « du 3 au 5 ») ; week : une semaine
	bool days (int &d0, int &d1)
	{
		d0 = d1 = -1;
		for (size_t i = 0; i < t.size (); i++)
		{
			const std::string &f = t[i].f;
			int d = -1, span = 0;
			size_t used = 1;
			if (f == "aujourd'hui" || f == "today" || f == "tonight" || f == "ce-soir") d = today;
			else if (f == "soir" || f == "matin" || f == "apres-midi") { if (i > 0 && (t[i - 1].f == "ce" || t[i - 1].f == "cet")) { d = today; t[i - 1].used = true; } }
			else if (f == "apres-demain") d = today + 2;
			else if (f == "apres" && i + 1 < t.size () && t[i + 1].f == "demain") { d = today + 2; used = 2; }
			else if (f == "demain" || f == "tomorrow") d = today + 1;
			else if (f == "semaine" || f == "week" || f == "week-end" || f == "weekend")
			{
				bool next = (i + 1 < t.size () && (t[i + 1].f == "prochaine" || t[i + 1].f == "suivante")) || (i > 0 && t[i - 1].f == "next");
				int monday = today - week_day (today) + (next ? 7 : 0);
				if (f == "semaine" || f == "week") { d = next ? monday : today; span = monday + 6 - d; }
				else { d = monday + 5; span = 1; if (d < today) d = today; span = monday + 6 - d; }
				if (next && i + 1 < t.size () && t[i + 1].f != "next") t[i + 1].used = true;
			}
			else if (weekday_of (f) >= 0)
			{
				int k = (weekday_of (f) - week_day (today) + 7) % 7;
				bool next = (i + 1 < t.size () && (t[i + 1].f == "prochain")) || (i > 0 && t[i - 1].f == "next");
				if (k == 0 && next) k = 7;
				d = today + k;
				if (i + 1 < t.size () && t[i + 1].f == "prochain") t[i + 1].used = true;
				// « lundi 9 novembre » : le nombre qui suit décide
				if (i + 1 < t.size () && number (t[i + 1].f) > 0)
				{
					int m = i + 2 < t.size () ? month_of (t[i + 2].f) : 0;
					int x = date_of (number (t[i + 1].f), m);
					if (x >= 0) { d = x; used = m ? 3 : 2; }
				}
			}
			else if (number (f) > 0 && number (f) <= 31)
			{
				int m = i + 1 < t.size () ? month_of (t[i + 1].f) : 0;
				bool le = i > 0 && (t[i - 1].f == "le" || t[i - 1].f == "the" || t[i - 1].f == "on" || t[i - 1].f == "du" || t[i - 1].f == "au");
				if (m) { d = date_of (number (f), m); used = 2; }
				else if (le && !(i + 1 < t.size () && (t[i + 1].f == "h" || t[i + 1].f == "heures" || t[i + 1].f == "pm" || t[i + 1].f == "am")))
					d = date_of (number (f), 0);
			}
			else if (month_of (f) && i + 1 < t.size () && number (t[i + 1].f) > 0)	// « November 12 »
			{
				d = date_of (number (t[i + 1].f), month_of (f));
				used = 2;
			}
			else if (f.size () >= 3 && f.size () <= 10 && isdigit ((unsigned char) f[0]) && f.find ('/') != std::string::npos)
			{
				int a = atoi (f.c_str ()), b = atoi (f.c_str () + f.find ('/') + 1);
				size_t s2 = f.find ('/', f.find ('/') + 1);
				if (s2 != std::string::npos)
				{
					int y = atoi (f.c_str () + s2 + 1);
					if (y < 100) y += 2000;
					if (b >= 1 && b <= 12 && a >= 1 && a <= month_len (y, b)) d = civil_days (y, b, a);
				}
				else if (b >= 1 && b <= 12) d = date_of (a, b);
			}
			else if (parse_iso (f) >= 0) d = parse_iso (f);
			if (d < 0) continue;
			for (size_t k = 0; k < used; k++) t[i + k].used = true;
			use_before ((int) i);
			if (d0 < 0) { d0 = d; d1 = d + span; }
			else { d1 = d > d0 ? d : d1; }
			i += used - 1;
		}
		return d0 >= 0;
	}
	// les heures dites (14h, 14h30, 14:30, 14 h 30, midi, 2pm) -> minutes ; deux au plus
	void hours (int &s, int &e)
	{
		s = e = -1;
		for (size_t i = 0; i < t.size (); i++)
		{
			if (t[i].used) continue;
			const std::string &f = t[i].f;
			int m = -1;
			size_t used = 1;
			if (f == "midi" || f == "noon") m = 12 * 60;
			else if (f == "minuit" || f == "midnight") m = 0;
			else if (!f.empty () && isdigit ((unsigned char) f[0]))
			{
				std::string g = f;
				bool pm = false, am = false;
				if (g.size () > 2 && (g.compare (g.size () - 2, 2, "pm") == 0 || g.compare (g.size () - 2, 2, "am") == 0))
				{ pm = g[g.size () - 2] == 'p'; am = !pm; g.resize (g.size () - 2); }
				else if (i + 1 < t.size () && (t[i + 1].f == "pm" || t[i + 1].f == "am")) { pm = t[i + 1].f == "pm"; am = !pm; used = 2; }
				if (g.size () > 6 && g.compare (g.size () - 6, 6, "heures") == 0) g.resize (g.size () - 6);
				int v = parse_hm (g);
				bool marked = g.find ('h') != std::string::npos || g.find (':') != std::string::npos || pm || am;
				if (v >= 0 && !marked && i + 1 < t.size () && (t[i + 1].f == "h" || t[i + 1].f == "heures" || t[i + 1].f == "heure"))
				{
					used = 2;
					marked = true;
					v = atoi (g.c_str ()) * 60;
					if (i + 2 < t.size () && number (t[i + 2].f) >= 0 && number (t[i + 2].f) < 60 && t[i + 2].f.size () == 2) { v += number (t[i + 2].f); used = 3; }
					if (v >= 24 * 60) v = -1;
				}
				if (v >= 0 && marked)
				{
					if (pm && v < 12 * 60) v += 12 * 60;
					if (am && v >= 12 * 60) v -= 12 * 60;
					m = v;
				}
			}
			if (m < 0) continue;
			for (size_t k = 0; k < used; k++) t[i + k].used = true;
			use_before ((int) i);
			if (s < 0) s = m;
			else if (e < 0) e = m;
			i += used - 1;
		}
	}
	// les mots restants, dans l'ordre, tels qu'écrits
	std::string rest (size_t from = 0, size_t to = std::string::npos) const
	{
		std::string s;
		for (size_t i = from; i < t.size () && i < to; i++)
		{
			if (t[i].used || t[i].f == ":") continue;
			if (t[i].f == ",") { if (!s.empty ()) s += ","; continue; }
			if (!s.empty () && s.back () != '\'' && s.back () != ',' ) s += ' ';
			else if (!s.empty () && s.back () == ',') s += ' ';
			s += t[i].o;
		}
		s = trim (s);
		while (!s.empty () && (s.back () == ',' || s.back () == '.')) s.pop_back ();
		return trim (s);
	}
	// « à la liste de courses », « dans mon agenda », « to my shopping list » : marqués
	void use_place (const char *const *nouns)
	{
		static const char *const P[] = { "la", "ma", "ta", "notre", "mon", "le", "l", "the", "my", "our", "a", "au", "aux", "sur", "dans",
						 "to", "on", "onto", "in", "into", "from", "de", "des", "du", "d", "of", "liste", "list", "pour", "for",
						 "les", 0 };
		for (size_t i = 0; i < t.size (); i++)
		{
			if (!in (t[i].f, nouns)) continue;
			t[i].used = true;
			int k = (int) i;
			while (k > 0 && in (t[k - 1].f, P)) t[--k].used = true;
			for (size_t j = i + 1; j < t.size () && (t[j].f == "list" || t[j].f == "liste"); j++) t[j].used = true;
		}
	}
	void use_verb_start ()		// le verbe en tête, et ce qui l'accompagne (« ajoute-moi », « peux-tu », « s'il te plaît »)
	{
		static const char *const F[] = { "peux-tu", "pourrais-tu", "peux", "pourrais", "tu", "veux-tu", "please", "stp", "svp", "can", "could",
						 "you", "moi", "me", "ajoute-moi", "mets-moi", "note-moi", "aussi", "also", "too", "il", "faut", "i",
						 "je", "dois", "need", "to", "j", "ai", "besoin", "s", "te", "plait", "plaît", "que", "that", "prends",
						 "please,", "pense", "penser", "rappelle", "rappelle-moi", "remind", 0 };
		for (auto &k : t)
		{
			if (k.used) continue;
			if (in (k.f, F) || k.f.compare (0, 4, "plai") == 0) k.used = true;
			else break;
		}
	}
	// les articles en tête d'un nom (« du lait », « des pommes », « une baguette ») -> le nom ; qty : « 2 litres »
	static std::string item_name (std::string s, std::string &qty)
	{
		qty.clear ();
		s = strip_article (s);
		// une quantité en tête : « 2 litres de lait », « deux baguettes », « 500 g de farine »
		static const char *const NUM[] = { "deux", "trois", "quatre", "cinq", "six", "sept", "huit", "neuf", "dix", "douze", "two",
						   "three", "four", "five", "six", "seven", "eight", "nine", "ten", "twelve", 0 };
		static const int NV[] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12 };
		size_t sp = s.find (' ');
		if (sp == std::string::npos) return s;
		std::string w = fold (s.substr (0, sp));
		int n = -1;
		if (!w.empty () && isdigit ((unsigned char) w[0])) n = atoi (w.c_str ());
		else for (int i = 0; NUM[i]; i++) if (w == NUM[i]) n = NV[i];
		if (n <= 0) return s;
		std::string r = trim (s.substr (sp + 1)), num = std::to_string (n);
		if (isdigit ((unsigned char) w[0])) num = s.substr (0, sp);
		static const char *const UNIT[] = { "kg", "g", "l", "cl", "ml", "litre", "litres", "liter", "liters", "bouteille", "bouteilles",
						    "paquet", "paquets", "boite", "boites", "pack", "packs", "douzaine", "douzaines", "bottle",
						    "bottles", "can", "cans", "pound", "pounds", "lb", "tranche", "tranches", "pot", "pots", "sachet",
						    "sachets", "kilo", "kilos", "dozen", 0 };
		size_t sp2 = r.find (' ');
		std::string u = fold (sp2 == std::string::npos ? r : r.substr (0, sp2));
		if (in (u, UNIT) && sp2 != std::string::npos)
		{
			qty = num + " " + r.substr (0, sp2);
			r = trim (r.substr (sp2 + 1));
			std::string f = fold (r);
			if (f.compare (0, 3, "de ") == 0) r = trim (r.substr (3));
			else if (f.compare (0, 2, "d'") == 0) r = trim (r.substr (2));
			else if (f.compare (0, 3, "of ") == 0) r = trim (r.substr (3));
		}
		else qty = num;
		return r;
	}

	// ---- les outils ----
	bool shopping (std::string &id, std::vector<std::string> &args)
	{
		id = "shopping";
		int at = -1, v = verb (&at);
		if (v == V_PRINT) { args = { "print" }; return true; }
		static const char *const DONE[] = { "coch", "check", "achet", "bought", "tick", 0 };
		if (v == V_CLEAR || (v == V_REMOVE && (starts_any (DONE) && (has ("articles") || has ("items") || has ("tout") || has ("all")))))
		{
			args = { "clear" };
			if (starts_any (DONE)) args.push_back ("coches");
			return true;
		}
		if (v == V_NONE || v == V_READ) { args = { "list" }; return true; }
		static const char *const N[] = { "courses", "shopping", "grocery", "groceries", "achats", "commissions", 0 };
		use_place (N);
		if (at >= 0) for (int i = 0; i <= at; i++) t[i].used = true;
		use_verb_start ();
		// les articles : séparés par « , », « et », « and »
		std::string all = rest ();
		std::vector<std::string> items;
		std::string cur;
		for (size_t i = 0; i < t.size (); i++)
		{
			if (t[i].used && t[i].f != ",") continue;
			if (t[i].f == "," || t[i].f == "et" || t[i].f == "and")
			{
				if (!trim (cur).empty ()) items.push_back (trim (cur));
				cur.clear ();
				continue;
			}
			if (!cur.empty () && cur.back () != '\'') cur += ' ';
			cur += t[i].o;
		}
		if (!trim (cur).empty ()) items.push_back (trim (cur));
		if (items.empty () || all.empty ())
		{
			id = "say";
			args = { v == V_ADD ? (en ? "What should I add to the shopping list?" : "Que dois-je ajouter à la liste de courses ?")
					    : (en ? "Which item?" : "Quel article ?") };
			return true;
		}
		static const char *const ACT[] = { "", "add", "remove", "check", "uncheck" };
		std::string qty, name = item_name (items[0], qty);
		args = { ACT[v], name };
		if (v == V_ADD && !qty.empty ()) args.push_back (qty);
		// (les autres articles : more, faits à la suite)
		for (size_t i = 1; i < items.size (); i++)
		{
			std::string q2, n2 = item_name (items[i], q2);
			more.push_back (q2.empty () || v != V_ADD ? std::vector<std::string> { ACT[v], n2 } : std::vector<std::string> { ACT[v], n2, q2 });
		}
		return true;
	}
	bool planner (std::string &id, std::vector<std::string> &args)
	{
		id = "planner";
		int at = -1, v = verb (&at);
		int d0, d1;
		bool day = days (d0, d1);
		if (v == V_PRINT || v == V_READ || v == V_NONE || v == V_CLEAR || v == V_CHECK)
		{
			if (!day) d0 = d1 = today;
			args = { v == V_PRINT ? "print" : "list", iso_date (d0) };
			if (d1 != d0) args.push_back (iso_date (d1));
			return true;
		}
		if (v != V_ADD) return false;
		int s, e;
		hours (s, e);
		static const char *const N[] = { "agenda", "calendrier", "calendar", "planning", "planner", 0 };
		use_place (N);
		if (at >= 0) for (int i = 0; i <= at; i++) t[i].used = true;
		use_verb_start ();
		std::string title = rest ();
		{
			std::string f = fold (title);
			static const char *const ART[] = { "un ", "une ", "a ", "an ", "le ", "la ", "the ", 0 };
			for (int i = 0; ART[i]; i++) if (f.compare (0, strlen (ART[i]), ART[i]) == 0) { title = trim (title.substr (strlen (ART[i]))); break; }
		}
		if (title.empty ()) title = en ? "Appointment" : "Rendez-vous";
		if (!day) { id = "say"; args = { en ? "For which day? Tell me the time too, if there is one." : "Pour quel jour ? Dis-moi aussi l'heure, s'il y en a une." }; return true; }
		args = { "add", iso_date (d0), cap_first (title) };
		if (s >= 0) args.push_back (hm_text (s).size () == 4 ? "0" + hm_text (s) : hm_text (s));
		if (s >= 0 && e > s) args.push_back (hm_text (e).size () == 4 ? "0" + hm_text (e) : hm_text (e));
		return true;
	}
	bool weather_words () const
	{
		static const char *const W[] = { "meteo", "weather", "forecast", "previsions", "prevision", "temperature", "temperatures", 0 };
		static const char *const R[] = { "pleu", "pluie", "rain", "neig", "snow", "ensoleill", "sunny", "orage", "storm", 0 };
		if (any (W) || starts_any (R)) return true;
		return has ("temps") && (has ("quel") || has ("fait") || has ("fera") || has ("faire") || has ("fait-il") || has ("fera-t-il"));
	}
	bool weather (std::string &id, std::vector<std::string> &args)
	{
		id = "weather";
		// « j'habite à Lyon », « ma ville est Lyon », « I live in Paris »
		bool live = has ("habite") || has ("live") || has_phrase ("ma ville est") || has_phrase ("my town is") || has_phrase ("my city is");
		int d0, d1;
		bool day = days (d0, d1);
		std::string city = town ();
		if (live && !city.empty () && !has ("meteo") && !has ("weather")) { args = { "city", city }; return true; }
		if (has ("semaine") || has ("week") || has_phrase ("prochains jours") || has_phrase ("next days") || has_phrase ("7 jours"))
			args = { "week" };
		else if (day && d0 != today) args = { "day", iso_date (d0) };
		else args = { "now" };
		if (!city.empty ()) args.push_back (city);
		return true;
	}
	// la ville : les mots après « à », « in », « pour », « sur » qui ne sont ni un jour ni un mot de la phrase
	std::string town () const
	{
		static const char *const PREP[] = { "a", "in", "at", "pour", "for", "sur", "est", "is", "de", "d", "of", 0 };
		static const char *const STOP[] = { "demain", "aujourd'hui", "maintenant", "ce", "cette", "semaine", "week", "today", "tomorrow",
						    "now", "this", "the", "la", "le", "les", "il", "fait", "fera", "quel", "quelle", "temps", "meteo",
						    "weather", "forecast", "previsions", "prochains", "jours", "next", "days", "chez", "moi", "home",
						    "midi", "soir", "matin", "minuit", "l", "va", "faire", "pleuvoir", "neiger", "en", "ce-soir", ",",
						    "et", "and", 0 };
		for (size_t i = 0; i + 1 < t.size (); i++)
		{
			if (!in (t[i].f, PREP)) continue;
			std::string s;
			for (size_t j = i + 1; j < t.size () && j < i + 5; j++)
			{
				if (in (t[j].f, STOP) || weekday_of (t[j].f) >= 0 || isdigit ((unsigned char) t[j].f[0])) break;
				if (!s.empty ()) s += ' ';
				s += t[j].o;
			}
			if (!s.empty ()) return s;
		}
		return "";
	}
	bool memo (std::string &id, std::vector<std::string> &args)
	{
		id = "memo";
		int at = -1, v = verb (&at);
		// « note : appeler Paul », « note que ... » : ce qui suit est la note
		int colon = -1;
		for (size_t i = 0; i < t.size (); i++) if (t[i].f == ":") { colon = (int) i; break; }
		if (v == V_PRINT || v == V_READ || v == V_NONE)
		{
			static const char *const N[] = { "note", "notes", "memo", "memos", "pense-bete", 0 };
			bool plural = has ("notes") || has ("memos") || has ("toutes") || has ("all");
			use_place (N);
			if (at >= 0) for (int i = 0; i <= at; i++) t[i].used = true;
			use_verb_start ();
			static const char *const F[] = { "mes", "my", "les", "la", "ma", "the", "toutes", "all", "me", "moi", "of", "de", "du", "sur", "about",
							 "qui", "parle", "dit", "sont", "are", "est", "is", "s", "quelles", "quels", "what", "which",
							 "ai", "j", "i", "have", "do", "toutes", "liste", "list", "lis", "lire", "read", "show", "montre", 0 };
			for (auto &k : t) if (in (k.f, F)) k.used = true;
			std::string q = rest ();
			if (v == V_PRINT) args = { "print" };
			else if (v == V_READ && (has ("lis") || has ("lire") || has ("read") || !q.empty ())) args = { "read" };
			else args = { plural ? "list" : "read" };
			if (!q.empty ()) args.push_back (q);
			if (args[0] == "read" && q.empty () && !(has ("lis") || has ("lire") || has ("read"))) args = { "list" };
			return true;
		}
		if (v != V_ADD) return false;
		std::string text;
		if (colon >= 0) text = rest ((size_t) colon + 1);
		else
		{
			for (int i = 0; i <= at; i++) t[i].used = true;
			static const char *const N[] = { "note", "notes", "memo", "pense-bete", 0 };
			use_place (N);
			use_verb_start ();
			text = rest ();
		}
		if (text.empty ()) { id = "say"; args = { en ? "What should the note say?" : "Que dois-je écrire dans la note ?" }; return true; }
		args = { "add", cap_first (text) };
		return true;
	}

public:
	std::vector<std::vector<std::string>> more;	// les appels suivants (plusieurs articles d'une phrase)
};

} // namespace pq

#endif
