// console.cpp -- TerraCube : la console de commandes (game.h, consoleExec).
//
// La même pour le joueur (touche T ou /, en bas de la vue), les scripts (game.command(texte)) et l'agent IA
// (outil run_command). Une commande par ligne, le « / » du début est facultatif ; un texte qui n'est pas une
// commande est un message. « help » donne la liste. Une ligne du protocole des scripts (« world.setBlock(…) »)
// est aussi acceptée.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sstream>

namespace tc {

const char *modeName (int m) { return m == MODE_SURVIVAL ? "survival" : "creative"; }
const char *weatherName (int w)
{
	static const char *N[] = { "clear", "rain", "snow", "storm" };
	return N[w & 3];
}

namespace {

std::vector<std::string> words (const std::string &s)
{
	std::vector<std::string> w;
	std::istringstream in (s);
	std::string t;
	while (in >> t) w.push_back (t);
	return w;
}

bool is (const std::string &a, std::initializer_list<const char *> names)
{
	for (const char *n : names)
		if (!strcasecmp (a.c_str (), n)) return true;
	return false;
}

bool number (const std::string &s, double &v)
{
	char *e;
	v = strtod (s.c_str (), &e);
	return e != s.c_str () && *e == 0;
}

std::string fmtHour (float h)
{
	char t[16];
	snprintf (t, sizeof t, "%02d:%02d", (int) h, (int) ((h - (int) h) * 60));
	return t;
}

// une coordonnée : un nombre, ou « ~ » / « ~n » (relative au joueur)
bool coord (const std::string &s, double base, double &v)
{
	if (!s.empty () && s[0] == '~') {
		double d = 0;
		if (s.size () > 1 && !number (s.substr (1), d)) return false;
		v = base + d;
		return true;
	}
	return number (s, v);
}

const char *HELP[] = {
	"Commandes (le / est facultatif) :",
	"  time <heure>|day|noon|sunset|night|midnight|stop|run  -- l'heure (0 à 24), le temps qui passe",
	"  weather clear|rain|snow|storm [secondes] · weather cycle on|off  -- la météo",
	"  mode creative|survival  -- le mode de jeu (aussi : gamemode, c, s)",
	"  tp <x> <y> <z> | tp spawn  -- se déplacer (~ : relatif au joueur)",
	"  give <objet> [nombre] · clear · inv  -- l'inventaire (survie)",
	"  recipes [mot] · craft <recette> [fois]  -- la fabrication (survie)",
	"  music on|off|<0..100> · sound on|off|<0..100>  -- la musique, les effets sonores",
	"  summon <créature> [nombre] [x y z] · kill <créature>|all · mobs [on|off]  -- les créatures (chicken, pig, wolf, rodeur, spider)",
	"  heal · feed · spawn set  -- la santé, la faim, le point de départ (survie)",
	"  fly · distance <2..16> · seed · save · info · say <texte>",
	"  et toute ligne du protocole des scripts : world.setBlocks(0,60,0,9,60,9,glass)",
};

} // namespace

std::vector<std::string> consoleExec (const std::string &input)
{
	std::vector<std::string> out;
	std::string line = input;
	while (!line.empty () && (line.back () == ' ' || line.back () == '\r' || line.back () == '\n')) line.pop_back ();
	while (!line.empty () && line[0] == ' ') line.erase (0, 1);
	if (line.empty ()) return out;
	bool slash = line[0] == '/';
	if (slash) line.erase (0, 1);
	std::vector<std::string> w = words (line);
	if (w.empty ()) return out;
	const std::string &c = w[0];
	auto say = [&] (const std::string &s) { out.push_back (s); };
	double v;

	if (is (c, { "help", "aide", "?" })) {
		for (const char *h : HELP) say (h);
	} else if (is (c, { "time", "heure" })) {
		std::string a = w.size () > 1 ? w[1] : "";
		if (a == "set" && w.size () > 2) a = w[2];
		if (a.empty () || a == "query") say ("heure : " + fmtHour (g.hour) + (g.timeRuns ? " (le temps passe)" : " (arrêté)"));
		else if (is (a, { "stop", "pause" })) g.timeRuns = false, say ("le temps est arrêté à " + fmtHour (g.hour));
		else if (is (a, { "run", "start" })) g.timeRuns = true, say ("le temps passe");
		else {
			double h = -1;
			if (is (a, { "day", "jour", "sunrise", "matin" })) h = a == "sunrise" || a == "matin" ? 6.2 : 8;
			else if (is (a, { "noon", "midi" })) h = 12;
			else if (is (a, { "sunset", "soir" })) h = 18;
			else if (is (a, { "night", "nuit" })) h = 21;
			else if (is (a, { "midnight", "minuit" })) h = 0;
			else if (a == "add" && w.size () > 2 && number (w[2], v)) h = g.hour + v;
			else if (number (a, v)) h = v;
			else if (a.find (':') != std::string::npos) {
				int hh, mm;
				if (sscanf (a.c_str (), "%d:%d", &hh, &mm) == 2) h = hh + mm / 60.0;
			}
			if (h < 0 && h != -1) h = fmod (h, 24) + 24;
			if (h == -1) say ("time : heure inconnue « " + a + " » (un nombre de 0 à 24, ou day, noon, sunset, night, midnight)");
			else {
				g.hour = (float) fmod (h, 24.0);
				say ("heure : " + fmtHour (g.hour));
			}
		}
	} else if (is (c, { "weather", "meteo", "météo" })) {
		std::string a = w.size () > 1 ? w[1] : "";
		if (a.empty () || a == "query") say (std::string ("météo : ") + weatherName (g.weather) + (g.weatherCycle ? " (elle change d'elle-même)" : " (fixe)"));
		else if (a == "cycle") {
			bool on = w.size () < 3 || is (w[2], { "on", "1", "oui" });
			g.weatherCycle = on;
			say (on ? "la météo change d'elle-même" : "la météo reste comme elle est");
		} else {
			int wt = is (a, { "clear", "sun", "soleil", "beau" }) ? W_CLEAR : is (a, { "rain", "pluie" }) ? W_RAIN
				 : is (a, { "snow", "neige" }) ? W_SNOW : is (a, { "storm", "thunder", "orage" }) ? W_STORM : -1;
			if (wt < 0) say ("weather : clear, rain, snow ou storm");
			else {
				double secs = w.size () > 2 && number (w[2], v) ? v : 0;
				gameSetWeather (wt, secs);
				say (std::string ("météo : ") + weatherName (wt) + (secs > 0 ? " pendant " + std::to_string ((int) secs) + " s" : ""));
			}
		}
	} else if (is (c, { "mode", "gamemode", "gm" })) {
		std::string a = w.size () > 1 ? w[1] : "";
		int m = is (a, { "creative", "créatif", "creatif", "c", "0" }) ? MODE_CREATIVE : is (a, { "survival", "survie", "s", "1" }) ? MODE_SURVIVAL : -1;
		if (a.empty ()) say (std::string ("mode : ") + modeName (g.mode));
		else if (m < 0) say ("mode : creative ou survival");
		else {
			gameSetMode (m);
			say (std::string ("mode : ") + modeName (m) + (m == MODE_SURVIVAL ? " (les blocs se ramassent, I : inventaire et fabrication)" : " (tous les blocs, B : la liste)"));
		}
	} else if (is (c, { "tp", "teleport" })) {
		double x, y, z;
		if (w.size () == 2 && w[1] == "spawn") {
			int h = g.world.height (0, 0);
			gameTeleport (0.5, h + 1, 0.5);
			say ("retour au point de départ");
		} else if (w.size () == 4 && coord (w[1], g.pl.x, x) && coord (w[2], g.pl.y, y) && coord (w[3], g.pl.z, z)) {
			gameTeleport (x, y, z);
			char t[96];
			snprintf (t, sizeof t, "téléporté en %.1f %.1f %.1f", x, y, z);
			say (t);
		} else say ("tp <x> <y> <z> (~ : relatif) ou tp spawn");
	} else if (is (c, { "give", "donner" })) {
		int it = w.size () > 1 ? itemByName (w[1].c_str ()) : -1;
		int n = w.size () > 2 && number (w[2], v) ? (int) v : 1;
		if (it < 0) say ("give <objet> [nombre] : objet inconnu (recipes, ou les noms de list_block_types)");
		else if (g.mode != MODE_SURVIVAL) say ("give : en mode créatif tout est déjà là (mode survival)");
		else {
			n = std::max (1, std::min (n, 64 * Inventory::N));
			int left = g.inv.add (it, n);
			say ("donné : " + std::to_string (n - left) + " × " + itemInfo (it).label + (left ? " (inventaire plein)" : ""));
		}
	} else if (is (c, { "clear" })) {
		g.inv.clear ();
		g.fuel = 0;
		say ("inventaire vidé");
	} else if (is (c, { "inv", "inventory", "inventaire" })) {
		int k = 0;
		for (int i = 0; i < Inventory::N; i++)
			if (!g.inv.s[i].empty ()) {
				const Stack &st = g.inv.s[i];
				ItemInfo ii = itemInfo (st.item);
				std::string l = (i < 9 ? "barre " + std::to_string (i + 1) : "sac " + std::to_string (i - 8)) + " : " + ii.label + " × " + std::to_string (st.count);
				if (ii.durability) l += " (usure " + std::to_string (st.dur) + "/" + std::to_string (ii.durability) + ")";
				say (l);
				k++;
			}
		if (!k) say ("l'inventaire est vide");
	} else if (is (c, { "recipes", "recettes" })) {
		std::string f = w.size () > 1 ? w[1] : "";
		int i = 0;
		for (auto &r : recipes ()) {
			i++;
			std::string l = std::to_string (i) + ". " + r.name + " → " + std::to_string (r.outN) + " × " + itemInfo (r.out).label + " : " +
					(r.station == ST_FURNACE ? std::string (itemInfo (r.in[0].item).label) + " au four" : recipeText (r)) +
					(r.station == ST_WORKBENCH ? " (établi)" : "");
			if (!f.empty () && l.find (f) == std::string::npos) continue;
			say (l);
		}
	} else if (is (c, { "craft", "fabriquer" })) {
		const Recipe *r = w.size () > 1 ? recipeByName (w[1].c_str ()) : nullptr;
		int n = w.size () > 2 && number (w[2], v) ? (int) v : 1;
		if (!r) say ("craft <recette> [fois] : recette inconnue (recipes)");
		else if (g.mode != MODE_SURVIVAL) say ("craft : seulement en mode survie");
		else {
			bool wb, fu;
			gameStations (wb, fu);
			std::string err;
			int done = craft (*r, std::max (1, n), g.inv, g.fuel, wb, fu, err);
			say ("fabriqué : " + std::to_string (done * r->outN) + " × " + itemInfo (r->out).label + (err.empty () ? "" : " (" + err + ")"));
		}
	} else if (is (c, { "music", "musique", "sound", "son", "sfx" })) {
		bool mus = is (c, { "music", "musique" });
		bool &on = mus ? g.musicOn : g.sfxOn;
		float &vol = mus ? g.musicVol : g.sfxVol;
		std::string a = w.size () > 1 ? w[1] : "";
		if (is (a, { "on", "1", "oui" })) on = true;
		else if (is (a, { "off", "0", "non" })) on = false;
		else if (number (a, v)) vol = (float) std::max (0.0, std::min (100.0, v)) / 100.0f, on = vol > 0;
		gameAudioChanged ();
		say (std::string (mus ? "musique : " : "effets sonores : ") + (on ? "oui" : "non") + ", volume " + std::to_string ((int) (vol * 100 + 0.5f)));
	} else if (is (c, { "fly", "voler" })) {
		if (g.mode == MODE_SURVIVAL) say ("fly : seulement en mode créatif");
		else {
			g.pl.fly = !g.pl.fly;
			gameFlyChanged ();
			say (g.pl.fly ? "vol" : "à pied");
		}
	} else if (is (c, { "distance" })) {
		if (w.size () > 1 && number (w[1], v)) g.distance = std::max (2, std::min (16, (int) v));
		say ("distance de vue : " + std::to_string (g.distance) + " tronçons");
	} else if (is (c, { "seed", "graine" })) {
		say ("graine : " + std::to_string (g.world.seed));
	} else if (is (c, { "save", "sauver" })) {
		std::string err;
		say (gameSave (err) ? "monde sauvé" : "sauvegarde impossible : " + err);
	} else if (is (c, { "spawn" })) {
		if (w.size () > 1 && is (w[1], { "set", "ici", "here" })) {
			g.spawnX = g.pl.x, g.spawnY = g.pl.y, g.spawnZ = g.pl.z;
			say ("point de départ : ici (on y revient en mourant)");
		} else {
			if (g.spawnY >= 0) gameTeleport (g.spawnX, g.spawnY, g.spawnZ);
			else gameTeleport (0.5, g.world.height (0, 0) + 1, 0.5);
			say ("retour au point de départ");
		}
	} else if (is (c, { "summon", "invoquer" })) {
		int type = w.size () > 1 ? entityByName (w[1].c_str ()) : -1;
		if (type < 0) say ("summon : chicken, pig, wolf, rodeur ou spider");
		else {
			int n = w.size () > 2 && number (w[2], v) ? std::max (1, std::min (32, (int) v)) : 1;
			double x = g.pl.x, y = g.pl.y, z = g.pl.z;
			double yr = g.pl.yaw * M_PI / 180.0;
			x += sin (yr) * 3, z -= cos (yr) * 3;	// (devant le joueur)
			if (w.size () > 5) {
				double a, b, d;
				if (coord (w[3], g.pl.x, a) && coord (w[4], g.pl.y, b) && coord (w[5], g.pl.z, d)) x = a, y = b, z = d;
			}
			for (int i = 0; i < n; i++) g.mobs.spawn (type, x + (i % 4) * 0.7, y + 0.2, z + (i / 4) * 0.7);
			say (std::to_string (n) + " × " + entityInfo (type).label);
		}
	} else if (is (c, { "kill", "tuer" })) {
		int type = w.size () > 1 ? entityByName (w[1].c_str ()) : -1;
		bool all = w.size () > 1 && (w[1] == "all" || w[1] == "tout");
		bool monsters = w.size () > 1 && (w[1] == "monsters" || w[1] == "hostile");
		int n = 0;
		for (auto &e : g.mobs.list)
			if (e.dead < 0 && (all || e.type == type || (monsters && entityInfo (e.type).hostile))) e.dead = 9, n++;
		if (type < 0 && !all && !monsters) say ("kill : une créature (chicken, pig, wolf, rodeur, spider), monsters ou all");
		else say (std::to_string (n) + " créature(s) enlevée(s)");
	} else if (is (c, { "mobs", "monsters", "monstres" })) {
		std::string a = w.size () > 1 ? w[1] : "";
		if (is (a, { "on", "oui" })) g.mobs.monsters = true;
		else if (is (a, { "off", "non", "peaceful" })) g.mobs.monsters = false;
		char t[120];
		snprintf (t, sizeof t, "créatures de la nuit : %s · %d créature(s) autour", g.mobs.monsters ? "oui" : "non (paisible)", g.mobs.count (-1, 128));
		say (t);
	} else if (is (c, { "heal", "soigner" })) {
		g.health = 20, g.burn = 0, g.air = 15;
		say ("santé : 20");
	} else if (is (c, { "feed", "nourrir" })) {
		g.hunger = 20, g.saturation = 10;
		say ("faim : 20");
	} else if (is (c, { "info" })) {
		char t[200];
		snprintf (t, sizeof t, "%s (graine %u) · %s · %s · %s · x %.1f y %.1f z %.1f · santé %.0f, faim %.0f", g.world.name.c_str (), g.world.seed,
			  modeName (g.mode), fmtHour (g.hour).c_str (), weatherName (g.weather), g.pl.x, g.pl.y, g.pl.z, g.health, g.hunger);
		say (t);
	} else if (is (c, { "say", "dire" })) {
		gamePost (line.size () > c.size () ? line.substr (c.size () + 1) : "");
	} else if (line.find ('(') != std::string::npos) {
		bool reply;
		std::string r = apiExec (line, reply);
		say (r.empty () ? "fait" : r);
	} else if (!slash) {
		gamePost (line);	// (un message)
	} else say ("commande inconnue : " + c + " (help)");
	return out;
}

} // namespace tc
