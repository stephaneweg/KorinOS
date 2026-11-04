//
// planner.cpp -- PocketQI : l'agenda du Calendrier (planner.h), par le code du Calendrier : ses en-têtes sont inclus
// tels quels, dans ce fichier seul (leurs globales g_ev, g_cat... restent ici). Appelé par le fil du cerveau, un appel à
// la fois : le calendrier est relu à chaque fois, puisque le Calendrier peut l'avoir changé entre-temps.
//
#include "../calendar/app.h"
#include "../calendar/model.h"
#include "../calendar/agendafiles.h"
#include "planner.h"
#include <algorithm>

namespace pq {

// le calendrier relu depuis la carte ; false : pas de fichier (un calendrier vide, les catégories du Calendrier)
static bool cal_load ()
{
	g_nev = 0; g_ntk = 0; g_ncat = 0;
	g_utcOffset = (int) (sys_call (SYS_CLOCK_GET_, 1) / 60);
	char *t = cal_read_file (CAL_ICS, 0);
	if (!t) return false;
	ics_read (t, false);
	delete [] t;
	return true;
}
static void cal_default_categories ()		// (celles de main.cpp du Calendrier, s'il n'a jamais tourné)
{
	cat_add (TR ("Travail"), 0x003B82F6); cat_add (TR ("Personnel"), 0x0010B981); cat_add (TR ("Famille"), 0x00F59E0B);
	cat_add (TR ("Sport"), 0x008B5CF6); cat_add (TR ("Anniversaires"), 0x00EC4899);
}
static int cal_today () { return (int) (sys_call (SYS_CLOCK_GET_, 2) / 86400); }

std::string planner_add (int day, const std::string &title, int start, int end)
{
	if (title.empty ()) return "pas de texte";
	if (!cal_load () || g_ncat == 0) cal_default_categories ();
	Event &e = ev_new ();
	scpy (e.title, sizeof e.title, title.c_str ());
	if (start < 0) { e.allDay = true; e.start = day * 1440; e.end = e.start + 1440; }
	else
	{
		e.start = day * 1440 + start;
		e.end = end > start ? day * 1440 + end : e.start + 60;
	}
	mkdir (CAL_DIR, 0755);
	int len = 0;
	char *t = ics_write (&len);
	bool ok = save_file (CAL_ICS, t, len);
	delete [] t;
	if (!ok) return "le calendrier n'a pas pu etre ecrit";
	// l'applet agenda du bureau : ses fichiers refaits, puis prévenue (comme save () du Calendrier)
	cal_write_agenda (cal_today ());
	long ch = sys_call (SYS_SVC_OPEN_, (long) "agenda", 0);
	if (ch >= 0) { static long m[64]; m[0] = 1; sys_call (SYS_SVC_CALL_, ch, (long) m); }
	return "";
}

bool planner_list (int d0, int d1, std::vector<PlannerItem> &out)
{
	out.clear ();
	if (!cal_load ()) return true;			// (pas encore de calendrier : aucun rendez-vous)
	static Occ occ[400];
	int n = occurrences (d0, d1 + 1, occ, 400, true);
	for (int i = 0; i < n; i++)
	{
		const Event &e = g_ev[occ[i].ev];
		// une occurrence sur plusieurs jours : une fois par jour de la période qu'elle touche
		int a = occ[i].start / 1440, b = (occ[i].end - 1) / 1440;
		if (b < a) b = a;
		for (int d = a < d0 ? d0 : a; d <= b && d <= d1; d++)
		{
			PlannerItem it;
			it.day = d;
			bool timed = !e.allDay && occ[i].end - occ[i].start < 1440;
			it.start = timed && d == a ? occ[i].start % 1440 : -1;
			it.end = timed && d == a ? it.start + (occ[i].end - occ[i].start) : -1;
			it.title = e.title[0] ? e.title : "(sans titre)";
			it.place = e.place;
			out.push_back (it);
		}
	}
	std::stable_sort (out.begin (), out.end (), [] (const PlannerItem &x, const PlannerItem &y)
			  { return x.day != y.day ? x.day < y.day : x.start < y.start; });
	return true;
}

} // namespace pq

namespace pq {

int planner_today () { return cal_today (); }

// un message d'un mot à un service du système (agenda : relire ; stickies : relire), s'il tourne
void service_poke (const char *name, long op)
{
	long ch = sys_call (SYS_SVC_OPEN_, (long) name, 0);
	if (ch < 0) return;
	static long m[64];
	m[0] = op;
	sys_call (SYS_SVC_CALL_, ch, (long) m);
}

} // namespace pq
