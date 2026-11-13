/*
 * applets/keyboard.cpp -- le volet Région et langue du panneau de configuration, en lignes de réglages (une carte
 * de listes déroulantes) :
 *   * la DISPOSITION du clavier : celles de SD0:/res/keyb (<NOM>.txt, montrées sous leur nom lisible : BE Belge,
 *     FR Français, US) ; Appliquer la change tout de suite (HIDManager, service « hid ») et l'écrit dans
 *     SD0:/etc/options.txt (« keyb=NOM », lu au démarrage) ;
 *   * la LANGUE (« lang=fr | en » dans SD0:/etc/system.ini) : AppKit la lit et traduit tout programme à son
 *     démarrage ;
 *   * le PAYS (« pays=FR » dans options.txt, et les canaux du Wi-Fi : wifi_country de NetKit) ;
 *   * le FUSEAU : une région (liste déroulante), puis sa ville dans une liste sous la carte ; les fuseaux de
 *     SD0:/res/tz.txt (« nom règle », tools/tz/mktz.py) ; « fuseau=Europe/Paris » dans options.txt, puis NTP.
 *
 * Port en C++ de applets/keyboard.inc (Korin). Onyx séparait keyconf (les cartes de SD:/etc/keymaps, la molette) et
 * langconf (la langue, un fuseau en décalage) : Korin les réunit en un volet aux formats de Korin (keyb=, lang=,
 * fuseau= nommé), et c'est ce que l'utilisateur a validé.
 */
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <netkit_abi.h>
#include "../control.h"

enum
{
	CL_N = 16,				/* dispositions */
	CL_TZBUF = 16384,			/* SD0:/res/tz.txt (un peu plus de 12 Ko) */
	CL_TZ_MAX = 512,
	HID_OP_KEYMAP = 2,			/* (service « hid ») */
};

static const char cl_s_sub[] = "Clavier, langue et heure de cet appareil.";
static const char cl_s_note[] = "La traduction est progressive : certaines applications restent en anglais.";
static const char cl_s_layout[] = "Disposition du clavier";
static const char cl_s_lang[] = "Langue";
static const char cl_s_country[] = "Pays (Wi-Fi)";
static const char cl_s_zone[] = "Fuseau horaire (NTP)";
static const char cl_s_done[] = "Disposition appliqu\xC3\xA9" "e.";
static const char cl_s_error[] = "Disposition illisible.";

/* les pays : code, libellé */
static const char *const cl_countries[][2] = {
	{ "FR", "France" }, { "BE", "Belgique" }, { "CH", "Suisse" }, { "LU", "Luxembourg" }, { "CA", "Canada" },
	{ "DE", "Allemagne" }, { "GB", "Royaume-Uni" }, { "US", "\xC3\x89tats-Unis" },
};
enum { CL_NCOUNTRY = sizeof cl_countries / sizeof cl_countries[0] };
/* les régions des fuseaux : le nom avant « / », son libellé */
static const char *const cl_regions[][2] = {
	{ "Africa", "Afrique" }, { "America", "Am\xC3\xA9rique" }, { "Antarctica", "Antarctique" }, { "Arctic", "Arctique" },
	{ "Asia", "Asie" }, { "Atlantic", "Atlantique" }, { "Australia", "Australie" }, { "Europe", "Europe" },
	{ "Indian", "Oc\xC3\xA9" "an Indien" }, { "Pacific", "Pacifique" }, { "UTC", "UTC" },
};
enum { CL_NREGION = sizeof cl_regions / sizeof cl_regions[0] };
/* les dispositions : nom du fichier, libellé (les autres : leur nom) */
static const char *const cl_kb_names[][2] = { { "BE", "Belge (point)" }, { "BE-COMMA", "Belge (virgule)" },
	{ "FR", "Fran\xC3\xA7" "ais" }, { "US", "US" } };	/* (BE / BE-COMMA : la touche décimale du pavé numérique) */

static char cl_current[16];			/* keyb= (sans : FR, intégrée) */
static char cl_country_cur[4];			/* pays= (sans : FR) */
static char cl_lang_cur[4];			/* lang= (sans : fr) */
static char cl_zone_cur[32];			/* fuseau= (sans : Europe/Paris) */
static char cl_layouts[CL_N][16];
static long cl_nlayouts;
static long cl_list;				/* les villes de la région */
static long cl_ntz;
static char *cl_tz[CL_TZ_MAX];			/* noms des fuseaux, dans cl_tzbuf */
static char cl_labels[CL_TZ_MAX][64];		/* leurs libellés (un par fuseau ; l'asm n'en avait qu'un) */
static char cl_tzbuf[CL_TZBUF];

/* dst, src, n : copie de n - 1 octets au plus (str_copy_n de l'asm) */
static void copy_n (char *dst, const char *src, size_t n)
{
	size_t i = 0;
	for (; i + 1 < n && src[i]; i++)
		dst[i] = src[i];
	dst[i] = 0;
}

/* nom d'une disposition (BE) -> son libellé (Belge), le nom lui-même s'il n'est pas dans cl_kb_names */
static const char *cl_label (const char *name)
{
	for (const auto &k : cl_kb_names)
		if (!strcmp (k[0], name))
			return k[1];
	return name;
}

/* (ini_read) « keyb », « pays », « fuseau », « lang » */
static void cl_line (char *key, char *val, void *)
{
	if (!strcmp (key, "keyb"))
		copy_n (cl_current, val, sizeof cl_current);
	else if (!strcmp (key, "pays"))
	{
		copy_n (cl_country_cur, val, sizeof cl_country_cur);
		cl_country_cur[0] &= ~0x20;	/* (majuscules, comme l'asm : « bic 0x20 » des deux premiers) */
		cl_country_cur[1] &= ~0x20;
	}
	else if (!strcmp (key, "fuseau"))
		copy_n (cl_zone_cur, val, sizeof cl_zone_cur);
	else if (!strcmp (key, "lang"))
		copy_n (cl_lang_cur, val, sizeof cl_lang_cur);
}

/* SD0:/res/tz.txt -> cl_tz (le nom de chaque ligne « nom règle » ; « # » : commentaire ; sans espace : ignorée) */
static void cl_tz_read (void)
{
	cl_ntz = 0;
	int fd = open ("SD0:/res/tz.txt", O_RDONLY);
	if (fd < 0)
		return;
	long n = read (fd, cl_tzbuf, CL_TZBUF - 1);
	close (fd);
	if (n <= 0)
		return;
	cl_tzbuf[n] = 0;
	char *p = cl_tzbuf;
	while (*p)
	{
		char *line = p, *space = 0;
		int comment = *p == '#';
		for (; *p && *p != '\n'; p++)
			if (*p == ' ' && !space)
				space = p;
		if (*p)
			*p++ = 0;
		if (comment || !space)
			continue;
		*space = 0;
		if (cl_ntz < CL_TZ_MAX)
			cl_tz[cl_ntz++] = line;
	}
}

/* fuseau -> sa région (cl_regions : le nom avant « / »), -1 si aucune */
static long cl_region_of (const char *zone)
{
	for (long r = 0; r < CL_NREGION; r++)
	{
		size_t l = strlen (cl_regions[r][0]);
		if (!strncmp (zone, cl_regions[r][0], l) && (zone[l] == 0 || zone[l] == '/'))
			return r;
	}
	return -1;
}

/* region : la liste montre ses villes (« Argentina / Buenos Aires »), le fuseau actuel choisi s'il en est */
static void cl_fill (long region)
{
	ui_list_clear (cl_list);
	long sel = -1;
	for (long z = 0; region >= 0 && z < cl_ntz; z++)
	{
		const char *name = cl_tz[z];
		if (cl_region_of (name) != region)
			continue;
		const char *s = strchr (name, '/');	/* le libellé : après la région (UTC : tout) */
		s = s ? s + 1 : name;
		char *o = cl_labels[z];
		long room = 60;
		for (; *s && room >= 4; s++)
		{
			char c = *s;
			if (c == '_')
				c = ' ';
			else if (c == '/')
			{
				*o++ = ' ';
				*o++ = '/';
				room -= 2;
				c = ' ';
			}
			*o++ = c;
			room--;
		}
		*o = 0;
		long i = ui_list_add (cl_list, (long) cl_labels[z], (long) name);
		if (!strcmp (name, cl_zone_cur))
			sel = i;
	}
	ui_w_set_value (cl_list, sel);
}

/* (ui_w_on) une autre région : ses villes */
static void cl_region_on (long w, long code)
{
	fm_change (w, code);
	if (code != UN_CHANGE)
		return;
	cl_fill (ui_w_get_value (w));
	ui_w_draw (cl_list);
}

/* (ap.draw) la liste des villes sous la carte, jusqu'à Appliquer */
static void cl_draw (void)
{
	long y = fm_top () + fm_n * FM_H + 36;	/* (sous la note) */
	long h = ui.h - (BTN_H + 24) - y;
	if (h < 60)
		h = 60;
	ui_w_move (cl_list, SIDE_W + AP_MARGIN, y, ui.w - (SIDE_W + 2 * AP_MARGIN), h);
	fm_draw ();
}

/* (ap.close) la liste, puis les lignes */
static void cl_close (void)
{
	ui_w_destroy (cl_list);
	cl_list = 0;
	fm_close ();
}

/* disposition (« FR », « US »…) : à HIDManager (service « hid »), qui la charge depuis SD0:/res/keyb
 * -> 0 ou erreur */
static long cl_keymap (const char *name)
{
	long ch = svc_open ("hid", 1000);
	if (ch < 0)
		return ch;
	long msg[64] = { 0 };
	msg[0] = HID_OP_KEYMAP;
	copy_n ((char *) (msg + 6), name, 16);	/* données (+48) : le nom, 15 octets au plus */
	return svc_call (ch, msg);
}

static void cl_open (void)
{
	copy_n (cl_current, "FR", sizeof cl_current);
	copy_n (cl_country_cur, "FR", sizeof cl_country_cur);
	copy_n (cl_zone_cur, "Europe/Paris", sizeof cl_zone_cur);
	copy_n (cl_lang_cur, "fr", sizeof cl_lang_cur);
	ini_read (P_OPTIONS, cl_line);		/* disposition, pays, fuseau */
	ini_read (P_SYSTEM, cl_line);		/* la langue : celle de tout le système (AppKit) */
	fm_begin (cl_s_sub, cl_s_note);
	fm_line (cl_s_layout);
	cl_nlayouts = 0;
	DIR *d = opendir ("SD0:/res/keyb");	/* les dispositions de SD0:/res/keyb */
	if (d)
	{
		struct dirent *e;
		while (cl_nlayouts < CL_N && (e = readdir (d)) != 0)
		{
			size_t l = strlen (e->d_name);	/* « .txt », aux capitales près */
			if (l < 4 || strcasecmp (e->d_name + l - 4, ".txt"))
				continue;
			l -= 4;
			if (l > 15)
				l = 15;
			char *name = cl_layouts[cl_nlayouts];	/* nom sans .txt */
			memcpy (name, e->d_name, l);
			name[l] = 0;
			fm_item (0, cl_label (name), !strcmp (name, cl_current));
			cl_nlayouts++;
		}
		closedir (d);
	}
	fm_line (cl_s_lang);			/* la langue */
	fm_item (1, "Fran\xC3\xA7" "ais", !strcmp (cl_lang_cur, "fr"));
	fm_item (1, "English", !strcmp (cl_lang_cur, "en"));
	fm_line (cl_s_country);			/* le pays */
	for (long i = 0; i < CL_NCOUNTRY; i++)
		fm_item (2, cl_countries[i][1], !strcmp (cl_countries[i][0], cl_country_cur));
	cl_tz_read ();				/* le fuseau : sa région */
	fm_line (cl_s_zone);
	long region = cl_region_of (cl_zone_cur);
	for (long r = 0; r < CL_NREGION; r++)
		fm_item (3, cl_regions[r][1], r == region);
	ui_w_on (fm_w[3], (long) cl_region_on, 0);	/* puis sa ville, dans la liste */
	cl_list = ui_list (0, 0, 100, 100, 0);	/* (placée par cl_draw) */
	ui_w_on (cl_list, (long) fm_change, 0);
	cl_fill (region);
	ap.draw = cl_draw;
	ap.close = cl_close;
}

/* Appliquer : la langue, le pays, le fuseau, puis la disposition tout de suite */
static void cl_apply (void)
{
	long v = fm_value (1);			/* la langue : system.ini (AppKit) */
	if (v >= 0)
		ini_set (P_SYSTEM, "lang", v == 0 ? "fr" : "en");
	v = fm_value (2);			/* le pays : options.txt et le Wi-Fi */
	if (v >= 0)
	{
		const char *code = cl_countries[v][0];
		ini_set (P_OPTIONS, "pays", code);
		wifi_country ((long) code);	/* (sans Wi-Fi : E_NODEV, sans effet) */
	}
	v = ui_w_get_value (cl_list);		/* le fuseau (la ville choisie) : options.txt, puis NTP */
	if (v >= 0)
	{
		long data = 0;
		if (uikit_list_get (cl_list, v, &data))	/* (sa donnée : le nom du fuseau) */
		{
			ini_set (P_OPTIONS, "fuseau", (const char *) data);
			net_ntp_sync (0);	/* (netd reprend l'heure : demande seule) */
		}
	}
	v = fm_value (0);
	if (v < 0)
		return;
	const char *name = cl_layouts[v];
	if (cl_keymap (name) < 0)
	{
		ap.msg = cl_s_error;
		return;
	}
	ini_set (P_OPTIONS, "keyb", name);
	ap.msg = cl_s_done;
}

const Applet keyboard_applet = { "clavier", "R\xC3\xA9gion", "R\xC3\xA9gion et langue", "clavier", cl_open, cl_apply };
