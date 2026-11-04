//
// planner.h -- PocketQI : l'agenda du Calendrier, lu et écrit par le code du Calendrier lui-même (planner.cpp inclut
// src/apps/calendar/model.h et agendafiles.h) : SD0:/apps/calendar.app/calendar.ics, puis agenda.txt et reminders.txt
// pour l'applet agenda du bureau, prévenue comme le fait le Calendrier. Le Calendrier ouvert relit le fichier changé.
//
// Les jours sont des numéros de jour (0 = 1970-01-01), les heures des minutes dans le jour.
//
#ifndef POCKETQI_PLANNER_H
#define POCKETQI_PLANNER_H

#include <string>
#include <vector>

namespace pq {

struct PlannerItem
{
	int day;				// le jour de cette occurrence
	int start, end;				// minutes dans le jour ; -1 : toute la journée
	std::string title, place;
};

// un rendez-vous ajouté (start, end : minutes, -1 sans heure ; end -1 : une heure après start) -> "" ou pourquoi pas
std::string planner_add (int day, const std::string &title, int start, int end);
// les rendez-vous des jours d0 à d1 compris, dans l'ordre (répétitions comprises) ; false : le calendrier illisible
bool planner_list (int d0, int d1, std::vector<PlannerItem> &out);
// aujourd'hui (l'horloge locale du système, comme le Calendrier)
int planner_today ();
// un message d'un mot à un service du système, s'il tourne (agenda, stickies : 1 et 2 = relire)
void service_poke (const char *name, long op);

} // namespace pq

#endif
