//
// config.h -- PocketQI : les réglages, SD0:/home/pocketqi/config.ini (menu PocketQI > Réglages...). Les clés d'API n'en
// sortent que vers leur service, dans l'en-tête Authorization (brain.h) : jamais au journal, à l'écran, ni dans un paquet.
//
//   provider              mistral, groq, url, local ; vide : le premier qui a sa clé
//   mistral_key, groq_key les clés d'API
//   url, url_key          un serveur compatible OpenAI (.../v1/chat/completions) et sa clé
//   model                 facultatif
//   city                  la ville de la météo quand aucune n'est nommée (outil weather, tools.h)
//   weather_url           facultatif : un serveur à la place d'Open-Meteo (/v1/search et /v1/forecast), pour les essais
//
#ifndef POCKETQI_CONFIG_H
#define POCKETQI_CONFIG_H

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "skills.h"

namespace pq {

static const char *const HOME_DIR = "SD0:/home/pocketqi";
static const char *const APP_DIR = "SD0:/apps/pocketqi.app";

struct Config
{
	std::string provider;
	std::string mistralKey, groqKey, url, urlKey, model;
	std::string city, weatherUrl;
	std::string path () const { return std::string (HOME_DIR) + "/config.ini"; }
	void load ()
	{
		*this = Config ();
		std::string t;
		if (!read_file (path (), t)) return;
		size_t p = 0;
		while (p < t.size ())
		{
			size_t e = t.find ('\n', p);
			if (e == std::string::npos) e = t.size ();
			std::string l = trim (t.substr (p, e - p));
			p = e + 1;
			if (l.empty () || l[0] == '#' || l[0] == ';' || l[0] == '[') continue;
			size_t eq = l.find ('=');
			if (eq == std::string::npos) continue;
			std::string k = trim (l.substr (0, eq)), v = trim (l.substr (eq + 1));
			if (k == "provider") provider = v;
			else if (k == "mistral_key") mistralKey = v;
			else if (k == "groq_key") groqKey = v;
			else if (k == "url") url = v;
			else if (k == "url_key") urlKey = v;
			else if (k == "model") model = v;
			else if (k == "city") city = v;
			else if (k == "weather_url") weatherUrl = v;
		}
	}
	bool save () const
	{
		mkdir ("SD0:/home", 0755);
		mkdir (HOME_DIR, 0755);
		std::string t = "# PocketQI (src/apps/pocketqi/README.md) : ce fichier garde les cles d'API ; il ne sort pas de la carte\n"
				"[pocketqi]\nprovider = " + provider + "\nmistral_key = " + mistralKey + "\ngroq_key = " + groqKey +
				"\nurl = " + url + "\nurl_key = " + urlKey + "\nmodel = " + model + "\ncity = " + city + "\n";
		if (!weatherUrl.empty ()) t += "weather_url = " + weatherUrl + "\n";
		int f = open (path ().c_str (), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (f < 0) return false;
		bool ok = write (f, t.data (), t.size ()) == (ssize_t) t.size ();
		close (f);
		std::fill (t.begin (), t.end (), 0);
		return ok;
	}
	// le service à demander d'abord : "url", "mistral", "groq", ou "local"
	std::string first () const
	{
		if (provider == "local") return "local";
		if (provider == "url" && !url.empty ()) return "url";
		if (provider == "mistral" && !mistralKey.empty ()) return "mistral";
		if (provider == "groq" && !groqKey.empty ()) return "groq";
		if (!url.empty ()) return "url";
		if (!mistralKey.empty ()) return "mistral";
		if (!groqKey.empty ()) return "groq";
		return "local";
	}
};

} // namespace pq

#endif
