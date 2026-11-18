// weather.h -- la météo d'Open-Meteo (géocodage, prévisions) en HTTP par NetKit
#ifndef WEATHER_H
#define WEATHER_H

#define WX_DAYS		4

// le lieu choisi : ce que l'utilisateur a écrit, et ce que le géocodage a trouvé
struct wx_place
{
	char city[96];			// tel qu'écrit (« Liège »)
	char country[64];		// tel qu'écrit (« Belgique », « BE » ou vide)
	char name[96];			// trouvé (« Liège »)
	char country_name[64];		// trouvé (« Belgique »)
	char code[4];			// « BE »
	double lat, lon;
	int found;			// 1 : lat, lon valent
};

// le temps qu'il fait, et les jours qui viennent
struct wx_data
{
	int valid;
	double temp, feels, wind;
	int humidity, code, is_day;
	char time[24];			// « 2026-11-17T20:45 », heure du lieu
	int ndays;
	char day[WX_DAYS][12];		// « 2026-11-17 »
	int dcode[WX_DAYS];
	double tmax[WX_DAYS], tmin[WX_DAYS];
};

enum
{
	WX_OK = 0,
	WX_ERR_NONET = -1,		// pas d'adresse réseau
	WX_ERR_DNS = -2,		// nom du serveur inconnu
	WX_ERR_CONNECT = -3,		// connexion refusée, délai
	WX_ERR_HTTP = -4,		// réponse illisible ou en erreur
	WX_ERR_NOTFOUND = -5,		// ville introuvable (dans ce pays)
};

// cherche place->city (dans place->country) : remplit name, country_name, code, lat, lon
int wx_geocode (struct wx_place *place, const char *lang);
// le temps à place->lat, place->lon
int wx_forecast (const struct wx_place *place, struct wx_data *out);

// la date « AAAA-MM-JJ » -> jour de la semaine (0 lundi), -1 si illisible
int wx_weekday (const char *date);

#endif
