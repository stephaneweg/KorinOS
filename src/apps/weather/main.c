// main.c -- Météo : le temps qu'il fait et les jours qui viennent, pour une ville (Korin OS, UIKit, Open-Meteo)
//
// Un seul programme, deux rôles :
//   main            une petite fenêtre : la ville, le pays (facultatif), et la carte météo ;
//   main --widget   la carte sur le bureau (sans cadre, sous toutes les fenêtres, hors de la liste des fenêtres) ;
//                   un clic ouvre la fenêtre pour changer de ville. C'est un choix de l'utilisateur (Fichier,
//                   Afficher sur le bureau : une ligne de HOME:/etc/startup.txt), pas du système.
// Le lieu choisi est gardé dans HOME:/etc/weather.ini ; le widget le relit toutes les 5 s et suit le changement.
// Écrit par Claude sur Korin (le 18/11/2026), intégré à l'arbre ensuite.
// Le réseau passe par un thread (weather.c attend les réponses) ; la boucle d'événements regarde s'il a fini.
// Au démarrage de la session, le widget est souvent lancé avant que le Wi-Fi ait son adresse : il s'abonne à
// korin.net.state (BusKit) et refait la carte dès que le réseau vient, sans attendre le prochain essai.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <armpi/userconf.h>
#include <uikit.h>
#include <netkit_abi.h>
#include <buskit_abi.h>
#include "weather.h"

extern char **environ;

#define TR(s)	((const char *) ui_tr ((long) (s)))
#define D(v)	((long) ((v) * 16))			// pixels -> 1/16 de pixel (ui_vp_*)
static const char *APP = "weather";
static char conf_buf[160];
/* weather.ini : lu dans HOME:/etc (sinon /etc), ecrit dans HOME:/etc (armpi/userconf.h) */
static const char *conf_file (int write) { return korin_conf_path ("weather.ini", conf_buf, sizeof conf_buf, write); }

enum { REFRESH_S = 15 * 60, RETRY_S = 60, RELOAD_S = 5 };	// prévisions, nouvel essai sans données, relecture du lieu
enum { CARD_W = 344, CARD_H = 186 };			// la carte (la largeur de l'agenda du bureau)
enum { WIDGET_X = 16, WIDGET_Y = 44 + 190 + 12 };	// sous l'agenda (inc/applet.inc : APPLET_X, APPLET_Y)
#define CARD_ALPHA	0x28000000u			// le fond du widget laisse voir un peu le bureau (comme l'agenda)

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

static int g_widget;					// 1 : le rôle « widget du bureau »
static char g_lang[8] = "fr";

// ---- l'état ----------------------------------------------------------------------------------------------------------

static struct wx_place g_place;				// le lieu montré
static struct wx_data g_data;				// son temps (g_data.valid : 0 tant que rien n'est venu)
static char g_status[256];				// le dernier message (recherche, erreur)
static int g_status_err;				// 1 : le message est une erreur
static time_t g_last;					// heure du dernier essai, réussi ou non
static int g_wx = -1, g_wy = -1;			// place du widget (weather.ini : widget_x, widget_y)

// le travail du thread du réseau
static pthread_t g_thread;
static int g_busy;					// un thread tourne
static volatile int g_done;				// il a fini : g_job_* sont à lire
static struct wx_place g_job_place;
static struct wx_data g_job_data;
static int g_job_rc;
static int g_last_rc = WX_ERR_NONET;			// le résultat du dernier essai (rien encore : comme sans réseau)
static long g_netsub;					// l'abonnement à korin.net.state (0 : pas de bus)
static int g_net_retry;					// le réseau est venu pendant un essai : le refaire s'il a échoué

static void *job_run (void *arg)
{
	(void) arg;
	int rc = WX_OK;
	if (!g_job_place.found) rc = wx_geocode (&g_job_place, g_lang);
	if (rc == WX_OK) rc = wx_forecast (&g_job_place, &g_job_data);
	g_job_rc = rc;
	__atomic_store_n (&g_done, 1, __ATOMIC_RELEASE);
	return 0;
}

// ---- les textes ------------------------------------------------------------------------------------------------------

static int iround (double v) { return (int) (v < 0 ? v - 0.5 : v + 0.5); }

static const char *describe (int code)
{
	switch (code)
	{
	case 0: return TR ("Ciel dégagé");
	case 1: return TR ("Plutôt dégagé");
	case 2: return TR ("Partiellement nuageux");
	case 3: return TR ("Couvert");
	case 45: return TR ("Brouillard");
	case 48: return TR ("Brouillard givrant");
	case 51: return TR ("Bruine légère");
	case 53: return TR ("Bruine");
	case 55: return TR ("Bruine dense");
	case 56: return TR ("Bruine verglaçante");
	case 57: return TR ("Bruine verglaçante dense");
	case 61: return TR ("Pluie faible");
	case 63: return TR ("Pluie");
	case 65: return TR ("Pluie forte");
	case 66: return TR ("Pluie verglaçante");
	case 67: return TR ("Pluie verglaçante forte");
	case 71: return TR ("Neige faible");
	case 73: return TR ("Neige");
	case 75: return TR ("Neige forte");
	case 77: return TR ("Grains de neige");
	case 80: return TR ("Averses faibles");
	case 81: return TR ("Averses");
	case 82: return TR ("Averses violentes");
	case 85: return TR ("Averses de neige");
	case 86: return TR ("Fortes averses de neige");
	case 95: return TR ("Orage");
	case 96: return TR ("Orage avec grêle");
	case 99: return TR ("Orage avec forte grêle");
	}
	return TR ("Temps inconnu");
}

static const char *weekday_name (int wd)
{
	switch (wd)
	{
	case 0: return TR ("Lundi");
	case 1: return TR ("Mardi");
	case 2: return TR ("Mercredi");
	case 3: return TR ("Jeudi");
	case 4: return TR ("Vendredi");
	case 5: return TR ("Samedi");
	case 6: return TR ("Dimanche");
	}
	return "";
}

static const char *error_text (int rc)
{
	switch (rc)
	{
	case WX_ERR_NONET: return TR ("Pas de réseau.");
	case WX_ERR_DNS: return TR ("Serveur météo introuvable (DNS).");
	case WX_ERR_CONNECT: return TR ("Le serveur météo ne répond pas.");
	case WX_ERR_HTTP: return TR ("Réponse illisible du serveur météo.");
	}
	return TR ("Erreur inconnue.");
}

// ---- le lieu gardé ---------------------------------------------------------------------------------------------------

static void place_save (void)
{
	FILE *f = fopen (conf_file (1), "w");
	if (!f) return;
	fprintf (f, "# Météo : le dernier lieu choisi\ncity = %s\ncountry = %s\nname = %s\ncountry_name = %s\ncode = %s\n"
		"lat = %.5f\nlon = %.5f\n", g_place.city, g_place.country, g_place.name, g_place.country_name, g_place.code,
		g_place.lat, g_place.lon);
	if (g_wx >= 0 && g_wy >= 0) fprintf (f, "# place du widget du bureau\nwidget_x = %d\nwidget_y = %d\n", g_wx, g_wy);
	fclose (f);
}

static int place_load (struct wx_place *pl)
{
	char line[256];
	FILE *f = fopen (conf_file (0), "r");
	memset (pl, 0, sizeof *pl);
	if (!f) return 0;
	int got = 0;
	while (fgets (line, sizeof line, f))
	{
		line[strcspn (line, "\r\n")] = 0;
		char *eq = strchr (line, '=');
		if (line[0] == '#' || !eq) continue;
		char *k = line, *v = eq + 1, *ke = eq;
		while (ke > k && ke[-1] == ' ') ke--;
		*ke = 0;
		while (*v == ' ') v++;
		if (!strcmp (k, "city")) snprintf (pl->city, sizeof pl->city, "%s", v);
		else if (!strcmp (k, "country")) snprintf (pl->country, sizeof pl->country, "%s", v);
		else if (!strcmp (k, "name")) snprintf (pl->name, sizeof pl->name, "%s", v);
		else if (!strcmp (k, "country_name")) snprintf (pl->country_name, sizeof pl->country_name, "%s", v);
		else if (!strcmp (k, "code")) snprintf (pl->code, sizeof pl->code, "%s", v);
		else if (!strcmp (k, "lat")) { pl->lat = strtod (v, 0); got |= 1; }
		else if (!strcmp (k, "lon")) { pl->lon = strtod (v, 0); got |= 2; }
		else if (!strcmp (k, "widget_x")) g_wx = atoi (v);
		else if (!strcmp (k, "widget_y")) g_wy = atoi (v);
	}
	fclose (f);
	pl->found = got == 3 && pl->name[0];
	return pl->found;
}

static int same_place (const struct wx_place *a, const struct wx_place *b)
{
	return a->found == b->found && a->lat == b->lat && a->lon == b->lon && !strcmp (a->name, b->name);
}

// ---- les dessins du temps (formes vectorielles aux couleurs du thème) ----------------------------------------------------

static void sun (double cx, double cy, double r)
{
	unsigned c = jet (TJ_WARNING);
	static const double dir[8][2] = { { 1, 0 }, { .7071, .7071 }, { 0, 1 }, { -.7071, .7071 }, { -1, 0 },
		{ -.7071, -.7071 }, { 0, -1 }, { .7071, -.7071 } };
	for (int i = 0; i < 8; i++)
		ui_vp_line (D (cx + dir[i][0] * r * 1.35), D (cy + dir[i][1] * r * 1.35), D (cx + dir[i][0] * r * 1.75),
			D (cy + dir[i][1] * r * 1.75), D (r * .2 > 1.2 ? r * .2 : 1.2), c);
	ui_vp_disc (D (cx), D (cy), D (r), c);
}

static void moon (double cx, double cy, double r, unsigned bg)
{
	ui_vp_disc (D (cx), D (cy), D (r), jet (TJ_WARNING));
	ui_vp_disc (D (cx + r * .45), D (cy - r * .35), D (r * .85), bg);	// le croissant
}

static void cloud (double cx, double cy, double r, unsigned c)
{
	ui_vp_disc (D (cx - r * .55), D (cy + r * .1), D (r * .42), c);
	ui_vp_disc (D (cx + r * .5), D (cy + r * .12), D (r * .38), c);
	ui_vp_disc (D (cx - r * .05), D (cy - r * .2), D (r * .58), c);
	ui_vp_line (D (cx - r * .55), D (cy + r * .3), D (cx + r * .5), D (cy + r * .3), D (r * .44), c);
}

static void sky (int day, double cx, double cy, double r, unsigned bg)
{
	if (day) sun (cx, cy, r);
	else moon (cx, cy, r * 1.25, bg);
}

// l'icône du code WMO dans un carré de côté s centré en (cx, cy)
static void draw_icon (int code, int day, double cx, double cy, double s, unsigned bg)
{
	double R = s / 2;
	unsigned light = ui_mix (jet (TJ_TEXT3), bg, 40), dark = jet (TJ_TEXT2), water = jet (TJ_ACCENT);
	if (code == 0) { sky (day, cx, cy, R * .42, bg); return; }
	if (code == 1 || code == 2)
	{
		sky (day, cx - R * .22, cy - R * .22, R * .34, bg);
		cloud (cx + R * .12, cy + R * .22, code == 1 ? R * .48 : R * .62, light);
		return;
	}
	if (code == 3)
	{
		cloud (cx - R * .22, cy - R * .18, R * .5, light);
		cloud (cx + R * .1, cy + R * .12, R * .64, dark);
		return;
	}
	if (code == 45 || code == 48)
	{
		cloud (cx, cy - R * .25, R * .58, light);
		for (int i = 0; i < 3; i++)
		{
			double y = cy + R * (.42 + .2 * i), dx = (i & 1) ? R * .12 : 0;
			ui_vp_line (D (cx - R * .7 + dx), D (y), D (cx + R * .6 + dx), D (y), D (R * .09), light);
		}
		return;
	}
	// précipitations : un nuage, puis ce qui tombe
	int showers = code >= 80 && code <= 86;
	int snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
	if (showers && day) sun (cx - R * .38, cy - R * .42, R * .26);
	cloud (cx, cy - R * .22, R * .62, snow ? light : dark);
	if (code >= 95)
	{
		int32_t bolt[14] = { (int32_t) D (cx + R * .1), (int32_t) D (cy + R * .2), (int32_t) D (cx - R * .2), (int32_t) D (cy + R * .62),
			(int32_t) D (cx - R * .02), (int32_t) D (cy + R * .62), (int32_t) D (cx - R * .16), (int32_t) D (cy + R * .98),
			(int32_t) D (cx + R * .24), (int32_t) D (cy + R * .5), (int32_t) D (cx + R * .05), (int32_t) D (cy + R * .5),
			(int32_t) D (cx + R * .22), (int32_t) D (cy + R * .2) };
		ui_vp_poly ((long) bolt, 7, jet (TJ_WARNING));
		if (code > 95)
			for (int i = 0; i < 2; i++) ui_vp_disc (D (cx + R * (i ? .45 : -.45)), D (cy + R * .7), D (R * .08), water);
		return;
	}
	int heavy = code == 55 || code == 57 || code == 65 || code == 67 || code == 75 || code == 82 || code == 86;
	int drizzle = code >= 51 && code <= 57;
	int n = heavy ? 4 : 3;
	for (int i = 0; i < n; i++)
	{
		double x = cx + R * (-.45 + .9 * i / (n - 1)), y = cy + R * ((i & 1) ? .62 : .5);
		if (snow)
		{
			static const double a[3][2] = { { 1, 0 }, { .5, .866 }, { -.5, .866 } };
			double f = R * .11;
			for (int k = 0; k < 3; k++)
				ui_vp_line (D (x - a[k][0] * f), D (y + R * .1 - a[k][1] * f), D (x + a[k][0] * f), D (y + R * .1 + a[k][1] * f),
					D (R * .045 > 1 ? R * .045 : 1), water);
		}
		else if (drizzle) ui_vp_disc (D (x), D (y + R * .12), D (R * .06 > 1.2 ? R * .06 : 1.2), water);
		else ui_vp_line (D (x + R * .06), D (y), D (x - R * .06), D (y + R * .26), D (R * .08 > 1.2 ? R * .08 : 1.2), water);
	}
}

// ---- la carte (la même dans la fenêtre et sur le bureau) ---------------------------------------------------------------

static void text_center (const char *s, int cx, int base, unsigned c, int size, const void *font, int maxw)
{
	int tw = (int) ui_text_width ((long) s, size, (long) font);
	if (tw > maxw) tw = maxw;
	ui_text_fit ((long) s, cx - tw / 2, base, c, size, (long) font, maxw);
}

static void text_right (const char *s, int right, int base, unsigned c, int size, const void *font)
{
	int tw = (int) ui_text_width ((long) s, size, (long) font);
	ui_text ((long) s, right - tw, base, c, size, (long) font, 0);
}

// la carte dans [0, w) x [0, h) de la fenêtre ou de la vue courante ; alpha : transparence du fond (widget)
static void paint_card (int w, int h, unsigned alpha)
{
	double k = (double) w / CARD_W < (double) h / CARD_H ? (double) w / CARD_W : (double) h / CARD_H;
	if (k < .8) k = .8;
#define S(v)	((int) ((v) * k + .5))
	int oy = (h - S (CARD_H)) / 2;
	if (oy < 0) oy = 0;
	unsigned bg = jet (TJ_SURFACE);
	if (alpha) ui_fill (0, 0, w, h, 0xFF000000);		// (tout transparent autour des coins)
	ui_rbox (0, 0, w, h, S (10), bg | alpha, bg | alpha);
	char t[160];

	if (!g_data.valid)
	{
		const char *msg = g_status[0] ? g_status : g_widget ? TR ("Cliquez pour choisir une ville.")
			: TR ("Écrivez une ville en haut, puis Entrée.");
		draw_icon (g_busy ? 3 : 2, 1, w / 2.0, oy + S (70), S (64), bg);
		text_center (msg, w / 2, oy + S (140), g_status_err ? jet (TJ_DANGER) : jet (TJ_TEXT2), S (13), ui.font, w - S (24));
		return;
	}

	// le lieu, le jour et l'heure du lieu
	int hh = 0, mm = 0;
	const char *tp = strchr (g_data.time, 'T');
	if (tp) sscanf (tp + 1, "%d:%d", &hh, &mm);
	snprintf (t, sizeof t, "%s %02d:%02d", weekday_name (wx_weekday (g_data.time)), hh, mm);
	int tw = (int) ui_text_width ((long) t, S (12), (long) ui.font);
	text_right (t, w - S (12), oy + S (23), jet (TJ_TEXT3), S (12), ui.font);
	ui_text_fit ((long) g_place.name, S (12), oy + S (23), jet (TJ_TEXT1), S (14), (long) ui.font_b, w - S (32) - tw);

	// le temps qu'il fait
	draw_icon (g_data.code, g_data.is_day, S (12) + S (26), oy + S (64), S (52), bg);
	snprintf (t, sizeof t, "%d°", iround (g_data.temp));
	ui_text ((long) t, S (76), oy + S (76), jet (TJ_TEXT1), S (32), (long) ui.font_b, 0);
	char feels[48], hum[48], wind[48];
	snprintf (feels, sizeof feels, TR ("Ressenti %d°"), iround (g_data.feels));
	snprintf (hum, sizeof hum, TR ("Humidité %d %%"), g_data.humidity);
	snprintf (wind, sizeof wind, TR ("Vent %d km/h"), iround (g_data.wind));
	text_right (feels, w - S (12), oy + S (54), jet (TJ_TEXT2), S (12), ui.font);
	text_right (hum, w - S (12), oy + S (72), jet (TJ_TEXT2), S (12), ui.font);
	text_right (wind, w - S (12), oy + S (90), jet (TJ_TEXT2), S (12), ui.font);
	int dw = (int) ui_text_width ((long) hum, S (12), (long) ui.font);
	int ww = (int) ui_text_width ((long) wind, S (12), (long) ui.font);
	if (ww > dw) dw = ww;
	ui_text_fit ((long) describe (g_data.code), S (76), oy + S (96), jet (TJ_TEXT2), S (13), (long) ui.font,
		w - S (12) - dw - S (8) - S (76));

	// les jours qui viennent
	ui_fill (S (12), oy + S (108), w - S (24), 1, jet (TJ_LINE));
	int n = g_data.ndays, top = oy + S (114);
	int cw = n ? (w - S (24)) / n : 0;
	for (int i = 0; i < n; i++)
	{
		int cx = S (12) + i * cw + cw / 2;
		const char *name = i == 0 ? TR ("Aujourd'hui") : i == 1 ? TR ("Demain") : weekday_name (wx_weekday (g_data.day[i]));
		text_center (name, cx, top + S (13), jet (TJ_TEXT1), S (12), ui.font_b, cw - S (4));
		draw_icon (g_data.dcode[i], 1, cx, top + S (36), S (30), bg);
		char hi[16], lo[16];
		snprintf (hi, sizeof hi, "%d°", iround (g_data.tmax[i]));
		snprintf (lo, sizeof lo, " %d°", iround (g_data.tmin[i]));
		int w1 = (int) ui_text_width ((long) hi, S (12), (long) ui.font_b), w2 = (int) ui_text_width ((long) lo, S (12), (long) ui.font);
		int x = cx - (w1 + w2) / 2;
		ui_text ((long) hi, x, top + S (66), jet (TJ_TEXT1), S (12), (long) ui.font_b, 0);
		ui_text ((long) lo, x + w1, top + S (66), jet (TJ_TEXT3), S (12), (long) ui.font, 0);
	}
#undef S
}

// ---- le travail ------------------------------------------------------------------------------------------------------

static long g_city, g_country, g_search, g_view, g_info;	// les widgets de la fenêtre (pas du rôle widget)

static void repaint (void);

static void set_status (int err, const char *fmt, const char *arg)
{
	snprintf (g_status, sizeof g_status, fmt, arg ? arg : "");
	g_status_err = err;
	if (!g_widget) ui_w_set_text (g_info, (long) g_status);
	repaint ();
}

// lance le thread : pl->found à 0 -> géocodage d'abord
static void fetch (const struct wx_place *pl)
{
	if (g_busy) return;
	g_job_place = *pl;
	g_done = 0;
	g_net_retry = 0;
	g_last = time (0);
	if (pthread_create (&g_thread, 0, job_run, 0) != 0) { set_status (1, "%s", TR ("Impossible de lancer la recherche.")); return; }
	g_busy = 1;
	if (g_widget) return;					// (le widget garde l'ancienne carte pendant ce temps)
	ui_w_enable (g_search, 0);
	set_status (0, pl->found ? TR ("Mise à jour de %s…") : TR ("Recherche de %s…"), pl->found ? pl->name : pl->city);
}

// l'essai a échoué faute de réseau (adresse, DNS, connexion) : la venue du réseau doit le refaire
static int net_failed (int rc) { return rc == WX_ERR_NONET || rc == WX_ERR_DNS || rc == WX_ERR_CONNECT; }

// le thread a fini : on reprend son résultat
static void fetch_done (void)
{
	pthread_join (g_thread, 0);
	g_busy = 0;
	g_last_rc = g_job_rc;
	if (!g_widget) ui_w_enable (g_search, 1);
	if (g_net_retry && net_failed (g_job_rc))	// le réseau est venu pendant l'essai : tout de suite un autre
	{
		struct wx_place pl = g_job_place;
		fetch (&pl);
		return;
	}
	if (g_job_rc == WX_ERR_NOTFOUND)
		set_status (1, g_job_place.country[0] ? TR ("« %s » est introuvable dans ce pays.") : TR ("« %s » est introuvable."),
			g_job_place.city);
	else if (g_job_rc != WX_OK) set_status (1, "%s", error_text (g_job_rc));
	else
	{
		g_place = g_job_place;
		g_data = g_job_data;
		if (!g_widget) place_save ();			// (le widget ne fait que lire le lieu)
		set_status (0, g_widget ? "" : TR ("Données : Open-Meteo.com"), 0);
	}
}

// ---- le réseau qui vient : korin.net.state (docs/BUS.md) -----------------------------------------------------------

static void net_watch (void)
{
	long s = buskit_kit_open () ? bus_subscribe ((long) "korin.net.state", 0) : 0;
	g_netsub = s > 0 ? s : 0;			// (sans bus : les essais toutes les RETRY_S secondes restent)
}

// WEV_BUS : « up <genre> <adresse> <interface> » ou « down » ; à « up », refaire ce qui a échoué faute de réseau
static void net_changed (void)
{
	if (!g_netsub || ui_ev_field (&ui, BEV_SUB) != g_netsub) return;
	char v[BUS_VALUE_MAX + 1];
	long n = bus_get ((long) "korin.net.state", (long) v, BUS_VALUE_MAX);
	if (n < 3) return;
	v[n < BUS_VALUE_MAX ? n : BUS_VALUE_MAX] = 0;
	if (strncmp (v, "up ", 3)) return;
	if (g_busy) { g_net_retry = 1; return; }	// (fetch_done le refera s'il échoue)
	if (g_place.found && net_failed (g_last_rc)) fetch (&g_place);
}

// ---- le rôle « widget du bureau » ------------------------------------------------------------------------------------

static void widget_paint (void)
{
	paint_card (ui.w, ui.h, CARD_ALPHA);
	ui_update_all ();
}

/* ---- le widget au demarrage de la session : une ligne de la liste personnelle HOME:/etc/startup.txt
 * (docs/DEMARRAGE.md ; Reglages, Demarrage la montre). Le widget est un choix de l'utilisateur : Fichier,
 * Afficher sur le bureau ; decoche, la ligne part et le widget qui tourne s'arrete de lui-meme. */
#define WIDGET_LINE	"/apps/weather.app/main --widget"

/* la ligne s'y trouve (hors commentaire) -> 1 */
static int widget_on (void)
{
	char p[160], l[256];
	FILE *f = fopen (korin_conf_path ("startup.txt", p, sizeof p, 0), "r");
	int on = 0;
	if (!f) return 0;
	while (!on && fgets (l, sizeof l, f))
	{
		l[strcspn (l, "\r\n")] = 0;
		char *b = l + strspn (l, " \t");
		size_t n = strlen (b);
		while (n && (b[n - 1] == ' ' || b[n - 1] == '\t')) b[--n] = 0;
		on = !strcmp (b, WIDGET_LINE);
	}
	fclose (f);
	return on;
}

/* ajoute (on) ou retire la ligne -> 0 si la liste n'a pas pu s'ecrire */
static int widget_set (int on)
{
	char p[160], l[256];
	static char buf[8192];
	size_t n = 0;
	FILE *f = fopen (korin_conf_path ("startup.txt", p, sizeof p, 0), "r");	/* (la sienne, sinon celle de /etc) */
	if (f)
	{
		while (fgets (l, sizeof l, f))
		{
			char t[256];
			snprintf (t, sizeof t, "%s", l);
			t[strcspn (t, "\r\n")] = 0;
			char *b = t + strspn (t, " \t");
			if (!strncmp (b, WIDGET_LINE, strlen (WIDGET_LINE)))
				continue;
			size_t k = strlen (l);
			if (n + k + 1 >= sizeof buf) break;
			memcpy (buf + n, l, k);
			n += k;
		}
		fclose (f);
	}
	if (n && buf[n - 1] != '\n') buf[n++] = '\n';
	if (on)
		n += (size_t) snprintf (buf + n, sizeof buf - n, "# Météo : la carte sur le bureau\n%s\n", WIDGET_LINE);
	f = fopen (korin_conf_path ("startup.txt", p, sizeof p, 1), "w");
	if (!f) return 0;
	int ok = fwrite (buf, 1, n, f) == n;
	return fclose (f) == 0 && ok;
}

static void open_app (void)
{
	char path[64];
	snprintf (path, sizeof path, "/apps/%s.app/main", APP);
	char *argv[] = { path, 0 };
	pid_t pid;
	posix_spawn (&pid, path, 0, 0, argv, environ);
}

static int widget_main (void)
{
	struct wx_place pl;
	place_load (&pl);
	g_place = pl;
	int x = g_wx >= 0 ? g_wx : WIDGET_X, y = g_wy >= 0 ? g_wy : WIDGET_Y;
	if (ui_window_ex (x, y, CARD_W, CARD_H, (long) TR ("Météo"),
		WF_FRAMELESS | WF_BACK | WF_NOFOCUS | WF_NOLIST | WF_ALPHA) <= 0) return 1;
	widget_paint ();
	net_watch ();					// (avant le premier essai : le réseau peut venir pendant)
	if (g_place.found) fetch (&g_place);
	time_t reload = time (0);

	for (;;)
	{
		long t = ui_event (1000);
		if (t == WEV_CLOSE) break;
		if (t == WEV_THEME) { s_jet = (const unsigned *) ui_tokens (); widget_paint (); }
		else if (t == WEV_BUS) net_changed ();
		else if (t == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1) open_app ();

		time_t now = time (0);
		if (g_busy && __atomic_load_n (&g_done, __ATOMIC_ACQUIRE)) { fetch_done (); widget_paint (); }
		if (!g_busy && now - reload >= RELOAD_S)		// la fenêtre a choisi un autre lieu ?
		{
			reload = now;
			if (!widget_on ()) break;			// (retiré du bureau : Fichier, Afficher sur le bureau)
			place_load (&pl);
			if (pl.found && !same_place (&pl, &g_place))
			{
				g_place = pl;
				g_data.valid = 0;
				g_status[0] = 0;
				fetch (&g_place);
				widget_paint ();
			}
		}
		if (!g_busy && g_place.found && now - g_last >= (g_data.valid ? REFRESH_S : RETRY_S)) fetch (&g_place);
	}
	if (g_busy) pthread_join (g_thread, 0);
	return 0;
}

// ---- le rôle « fenêtre » ---------------------------------------------------------------------------------------------

enum { M_REFRESH = 1, M_PLACE, M_DESK, M_CLOSE };
static int g_quit;

static void repaint (void)
{
	if (g_widget) widget_paint ();
	else ui_view_dirty (g_view);
}

static void view_paint (long v, long w, long h)
{
	(void) v;
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	paint_card ((int) w, (int) h, 0);
	ui_frame (0, 0, w, h, jet (TJ_LINE));
}

static void search (void)
{
	struct wx_place pl;
	memset (&pl, 0, sizeof pl);
	snprintf (pl.city, sizeof pl.city, "%s", (const char *) ui_w_get_text (g_city));
	snprintf (pl.country, sizeof pl.country, "%s", (const char *) ui_w_get_text (g_country));
	if (!pl.city[strspn (pl.city, " \t")]) { ui_w_focus (g_city); set_status (1, "%s", TR ("Écrivez le nom d'une ville.")); return; }
	fetch (&pl);
}

static int ctl_h (void) { return (int) ui_metric (UMT_CTL); }

static long on_resize (long w, long h)
{
	int ctl = ctl_h (), m = 12, bw = 96;
	int y1 = 10, y2 = y1 + ctl + 8, yv = y2 + ctl + 12;
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_w_move (g_city, m, y1, w - FRAME_CAP_W - 2 * m, ctl);	// (les boutons de la fenêtre sont en haut à droite)
	ui_w_move (g_country, m, y2, w - 2 * m - bw - 8, ctl);
	ui_w_move (g_search, w - m - bw, y2, bw, ctl);
	ui_w_move (g_view, m, yv, w - 2 * m, h - yv - 30);
	ui_w_move (g_info, m, h - 26, w - 2 * m, 20);
	ui_view_dirty (g_view);
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();
	return 0;
}

static void command (long id)
{
	switch (id)
	{
	case M_REFRESH: if (g_place.found) fetch (&g_place); else search (); break;
	case M_PLACE: ui_w_focus (g_city); ui_textbox_select (g_city, 0, ui_w_text_len (g_city)); break;
	case M_DESK:
	{						/* la carte sur le bureau, ou plus */
		int on = !widget_on ();
		if (!widget_set (on))
		{
			set_status (1, "%s", TR ("La liste de démarrage n'a pas pu s'écrire."));
			break;
		}
		ui_menu_state (M_DESK, on ? UMS_CHECKED : 0);
		if (on)
		{					/* (tout de suite, sans attendre la prochaine session) */
			char *argv[] = { "/apps/weather.app/main", "--widget", 0 };
			pid_t pid;
			posix_spawn (&pid, argv[0], 0, 0, argv, environ);
		}
		set_status (0, "%s", on ? TR ("La carte est sur le bureau, et y reviendra à chaque session.")
			: TR ("La carte quitte le bureau."));
		break;
	}
	case M_CLOSE: g_quit = 1; break;
	}
}

static int window_main (void)
{
	int ctl = ctl_h ();
	int w = CARD_W + 24, h = 10 + 2 * ctl + 8 + 12 + CARD_H + 30;
	if (ui_window_ex (-1, -1, w, h, (long) TR ("Météo"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (w, h);
	ui_on_resize ((long) on_resize, 0);

	g_city = ui_textbox (0, 0, 10, 30, 0);
	ui_textbox_hint (g_city, (long) TR ("Ville"));
	g_country = ui_textbox (0, 0, 10, 30, 0);
	ui_textbox_hint (g_country, (long) TR ("Pays (facultatif)"));
	g_search = ui_pushbutton (0, 0, 10, 30, (long) TR ("Chercher"), UB_DEFAULT);
	g_view = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_view, (long) view_paint, 0, 0, 0);
	g_info = ui_label (0, 0, 10, 20, (long) "", ULB_DIM);
	ui_menu ((long) "MFichier\nI1\tActualiser\tCtrl+R\nI2\tChanger de ville\tCtrl+L\n-\nI3\tAfficher sur le bureau\n-\n"
		"I4\tFermer\tCtrl+W\n");
	ui_menu_state (M_DESK, widget_on () ? UMS_CHECKED : 0);

	struct wx_place saved;
	if (place_load (&saved))
	{
		g_place = saved;
		ui_w_set_text (g_city, (long) saved.city);
		ui_w_set_text (g_country, (long) saved.country);
	}
	on_resize (ui.w, ui.h);
	net_watch ();
	if (g_place.found) fetch (&g_place);
	ui_w_focus (g_city);
	ui_textbox_select (g_city, 0, ui_w_text_len (g_city));

	for (;;)
	{
		long t = ui_event (100);
		if (t == WEV_CLOSE || g_quit) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_BUS) net_changed ();
		else if (t > 0)
		{
			long code = 0, taken = 0;
			long wid = uikit_w_event (&code, &taken);
			if (t == WEV_THEME) { s_jet = (const unsigned *) ui_tokens (); on_resize (ui.w, ui.h); }	// le thème a changé
			if (code == UN_ACTIVATE && (wid == g_search || wid == g_city || wid == g_country)) search ();
			if (t == WEV_KEY && ui_ev_field (&ui, EV_KIND) && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command (id);
			}
		}
		else ui_w_idle ();

		if (g_busy && __atomic_load_n (&g_done, __ATOMIC_ACQUIRE)) fetch_done ();
		if (!g_busy && g_place.found && time (0) - g_last >= REFRESH_S) fetch (&g_place);
		ui_view_flush ();
	}
	if (g_busy) pthread_join (g_thread, 0);
	return 0;
}

int main (int argc, char **argv)
{
	g_widget = argc > 1 && !strcmp (argv[1], "--widget");
	if (!uikit_open (&ui)) return 1;
	if (!netkit_kit_open ()) return 1;
	snprintf (g_lang, sizeof g_lang, "%s", (const char *) ui_lang ());	/* (la langue des noms de lieux ; les
										   textes : lang/en.txt dans le dictionnaire du systeme) */
	s_jet = (const unsigned *) ui_tokens ();
	return g_widget ? widget_main () : window_main ();
}
