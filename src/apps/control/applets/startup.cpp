/*
 * applets/startup.cpp -- le volet Démarrage du panneau de configuration (docs/DEMARRAGE.md), à la manière de la
 * console Services de Windows : la liste de ce que Korin lance, chaque entrée avec un nom et une description
 * lisibles, son état (le process tourne ou pas, relu chaque seconde) et son type de démarrage ; pour l'entrée
 * choisie, une barre : Démarrer, Arrêter, Redémarrer (tout de suite), le type (Automatique, Manuel, Désactivé), et
 * ses dépendances (« Nécessite », « Requis par », de /etc/services.deps). Trois parties :
 *   * Système : ce qu'init et init2 lancent toujours (vfs, busd, authd, hwmanager, hidmanager, Elegant), sans action ;
 *   * Services : les lignes de /etc/services.txt (init2, en uid 0). Désactivé : la ligne dans /etc/services.off,
 *     jamais lancée ; Manuel : dans /etc/services.manual, pas lancée au démarrage mais quand un service qui en a
 *     besoin démarre, ou par Démarrer. services.txt ne change pas : une mise à jour peut toujours le remplacer (les
 *     fichiers de « config = » ne sont remplacés que s'ils n'ont pas été modifiés). Une ligne en commentaire
 *     (« #/sys/svc/telnetd », des volets Accès distant ou Serveur web) est montrée désactivée ; un autre type retire
 *     le « # ». Écrire dans /etc demande l'élévation (ap_elevate) ; Démarrer, Arrêter passent par init2
 *     (INIT_OP_SVC_START, qui lance d'abord les dépendances, INIT_OP_SVC_STOP, qui cesse de surveiller) ;
 *   * Session : les programmes du bureau (/etc/session, ou /etc/session.poche en mode poche ; Elegant les lance) et
 *     ceux de startup.txt (init2, une fois la session ouverte) : session.off et session.manual, personnels
 *     (HOME:/etc/), sans élévation ; Manuel n'y veut dire que « par Démarrer ».
 * Les indispensables (vterm, ps2kbd, menubar, panel, l'accueil du mode poche…) restent automatiques et ne s'arrêtent
 * pas (menubar et panel peuvent être redémarrés) ; d'autres (netd, sound, pkgd, v3d, notifyd, clipd) demandent
 * confirmation. Appliquer écrit les listes : effet au prochain démarrage (ou à la prochaine session).
 */
#include <fcntl.h>
#include <spawn.h>
#include <string.h>
#include <unistd.h>
#include <armpi/syscall.h>
#include <armpi/userconf.h>
#include "../control.h"

extern char **environ;

#ifndef INIT_OP_SVC_STOP			/* (src/inc/armpi.inc ; uikit_abi.h les donne aussi) */
#define INIT_OP_SVC_STOP 4
#define INIT_OP_SVC_START 5
#endif

enum
{
	SYS_KILL = 0x14,
	SYS_PROCLIST_FROM = 0x1F,
	ST_E_NOENT = -2,
	ST_E_EXIST = -17,
	ST_MAX = 64,				/* entrées */
	ST_LINE = 128,
	ST_NAME = 32,
	ST_PROCS = 256,				/* process lus */
	ST_BAR_Y = AP_HEAD + 22,		/* la barre des actions (sous le sous-titre) */
	ST_INFO_Y = ST_BAR_Y + BTN_H + 18,	/* ligne de base de « Nécessite… » */
	ST_Y = ST_INFO_Y + 14,			/* haut de la liste */
	ST_HEAD_H = 30,				/* un titre de partie */
	ST_ROW_H = 46,				/* une entrée */
	ST_TYPE_W = 104,			/* la colonne du type */
	ST_STATE_W = 88,
	ST_SEG_W = 92,				/* un bouton du type */
	ST_OFF_MAX = 2048,
	ST_DEPS_MAX = 2048,
};

/* les parties */
enum { S_SYSTEM, S_SERVICES, S_SESSION, S_COUNT };

/* protection : 0 libre, 1 confirmation, 2 indispensable (toujours automatique, ne s'arrête pas) */
enum { P_FREE, P_WARN, P_LOCK };

/* le type de démarrage */
enum { M_AUTO, M_MANUAL, M_OFF, M_COUNT };

struct Info
{
	const char *path;			/* le chemin (sans arguments) */
	const char *label;
	const char *desc;
	int prot;
};

/* les programmes connus ; un autre : le nom de son app, ou de son fichier, et sa ligne pour description */
static const Info st_info[] = {
	/* Système */
	{ "/sys/svc/vfs", "Fichiers et volumes", "Le service des fichiers : disques, clés USB, partages.", P_LOCK },
	{ "/sys/svc/busd", "Bus d'événements", "Les nouvelles du système (thème, réseau, son, volumes) pour les programmes.", P_LOCK },
	{ "/sys/svc/authd", "Autorisations", "Comptes, mots de passe et élévation.", P_LOCK },
	{ "/sys/svc/hwmanager", "Gestionnaire du matériel", "Découvre le matériel et lance ses pilotes (drivers.txt).", P_LOCK },
	{ "/sys/svc/hidmanager", "Entrées", "Claviers, souris, touchpad et manettes.", P_LOCK },
	{ "/sys/elegant", "Bureau Elegant", "Le serveur graphique : écran, fenêtres, session du bureau.", P_LOCK },
	/* services.txt */
	{ "/sys/svc/vterm", "Pseudo-terminaux", "Le Terminal, la console texte et telnet s'en servent.", P_LOCK },
	{ "/sys/svc/netd", "Réseau", "Wi-Fi et Ethernet, adresses (DHCP), noms (DNS) et heure (NTP).", P_WARN },
	{ "/sys/svc/ps2kbd", "Clavier & Souris PS/2", "Le clavier interne du portable et la souris PS/2.", P_LOCK },
	{ "/sys/svc/sound", "Son", "Mélange, volumes et sorties ; les pilotes du son s'y inscrivent.", P_WARN },
	{ "/sys/pkgd", "Mises à jour", "Cherche les mises à jour des logiciels une fois par jour.", P_WARN },
	{ "/sys/svc/imgblk", "Image disque d'essai", "Montre une image (test.img) comme un disque.", P_FREE },
	{ "/sys/svc/pwmaudio", "Prise jack", "La sortie son analogique du Pi 4.", P_FREE },
	{ "/sys/svc/i2smic", "Micro I2S", "Un micro INMP441 sur les broches 18 à 20.", P_FREE },
	{ "/sys/svc/v3d", "Processeur graphique", "Le GPU V3D du Pi : la 3D et l'accélération des apps.", P_WARN },
	{ "/sys/svc/gpio", "Broches GPIO", "Le connecteur à 40 broches (GpioKit, BASIC).", P_FREE },
	{ "/sys/svc/telnetd", "Terminal à distance (telnet)", "Un shell depuis un autre ordinateur (port 23).", P_FREE },
	{ "/sys/svc/ftpd", "Partage de fichiers (FTP)", "Les fichiers depuis un autre ordinateur (port 21).", P_FREE },
	{ "/sys/svc/vncd", "Bureau à distance (VNC)", "L'écran depuis un autre ordinateur (port 5900).", P_FREE },
	{ "/sys/svc/rdpd", "Fenêtres à distance (RDP)", "Les fenêtres dans le client de Korin (port 3390).", P_FREE },
	{ "/sys/webd", "Serveur web", "Les sites de /etc/web/sites.ini (Réglages > Serveur web).", P_FREE },
	{ "/sys/smbd", "Partage Windows (SMB)", "Les dossiers partagés vus depuis Windows.", P_FREE },
	/* la session */
	{ "/sys/voronoy", "Fond d'écran", "Le fond du bureau.", P_FREE },
	{ "/sys/menubar", "Barre de menus", "La barre du haut : menus, heure, son, réseau.", P_LOCK },
	{ "/sys/panel", "Panel", "La barre du bas : lanceur et fenêtres ouvertes.", P_LOCK },
	{ "/sys/dock", "Dock", "L'ancien dock du bas de l'écran.", P_FREE },
	{ "/sys/notifyd", "Notifications", "Les avis des programmes et leur historique.", P_WARN },
	{ "/sys/clipd", "Presse-papiers", "Garde les dernières copies, même l'app fermée.", P_WARN },
	{ "/sys/agenda", "Agenda du bureau", "Les prochains rendez-vous sur le bureau et leurs rappels.", P_FREE },
	{ "/sys/clockd", "Alarmes", "Les alarmes et minuteurs de l'Horloge.", P_FREE },
	{ "/sys/stickies", "Notes épinglées", "Les notes épinglées sur le bureau.", P_FREE },
	{ "/sys/printd", "Impression", "La file d'impression et les imprimantes.", P_FREE },
	{ "/sys/home", "Accueil", "L'écran d'accueil du mode poche.", P_LOCK },
	{ "/sys/quick", "Réglages rapides", "Le volet des réglages rapides du mode poche.", P_FREE },
	{ "/sys/switcher", "Apps ouvertes", "Le sélecteur d'apps du mode poche.", P_LOCK },
};
enum { INFO_N = sizeof st_info / sizeof st_info[0], INFO_SYSTEM = 6 };

/* une entrée de la liste */
struct Entry
{
	char line[ST_LINE];			/* « chemin [arguments] », telle que dans son fichier */
	char name[ST_NAME];			/* le nom du process */
	char label[40];				/* (sans Info : le nom de l'app, ou du fichier) */
	const Info *info;			/* (0 : inconnu) */
	unsigned char sect;
	unsigned char mode;			/* le type choisi (M_*) */
	unsigned char was_mode;			/* celui des fichiers */
	unsigned char commented;		/* ligne en commentaire dans son fichier */
	unsigned char file;			/* 0 services.txt, 1 session, 2 startup.txt */
	long pid;				/* > 0 : tourne */
};

static const char st_s_sub[] = "Type de démarrage et état de ce que Korin lance. Appliquer : effet au prochain démarrage.";
static const char *const st_s_sect[S_COUNT] = {
	"Système (toujours lancé)", "Services du système", "Programmes de la session",
};
static const char *const st_s_mode[M_COUNT] = { "Automatique", "Manuel", "Désactivé" };
static const char st_s_running[] = "En cours";
static const char st_s_stopped[] = "Arrêté";
static const char st_s_stop[] = "Arrêter";
static const char st_s_start[] = "Démarrer";
static const char st_s_restart[] = "Redémarrer";
static const char st_s_locked[] = "Indispensable";
static const char st_s_needs[] = "Nécessite :";
static const char st_s_needed[] = "Requis par :";
static const char st_s_nodeps[] = "Aucune dépendance.";
static const char st_s_always[] = "Indispensable : toujours lancé au démarrage.";
static const char st_s_choose[] = "Choisissez une ligne.";
static const char st_s_saved[] = "Enregistré : effet au prochain démarrage.";
static const char st_s_nochange[] = "Rien n'a changé.";
static const char st_s_denied[] = "Autorisation refusée.";
static const char st_s_write[] = "Enregistrement impossible.";
static const char st_s_failed[] = "Impossible : le programme ne répond pas.";
static const char st_s_missing[] = "Programme introuvable (pas installé ?).";
static const char st_s_disabled[] = "Désactivé : choisissez d'abord Manuel ou Automatique, puis Appliquer.";
static const char st_s_empty[] = "Aucun programme.";
#define ST_SERVICES		"/etc/services.txt"
#define ST_SERVICES_OFF		"/etc/services.off"
#define ST_SERVICES_MANUAL	"/etc/services.manual"
#define ST_SERVICES_DEPS	"/etc/services.deps"

static Entry st[ST_MAX];
static long st_n;
static long st_sel = -1;			/* l'entrée choisie */
static long st_first;				/* première ligne montrée (titres compris) */
static char st_session[64];			/* /etc/session ou /etc/session.poche */
static char st_startup[160];			/* startup.txt lu (personnel, sinon /etc) */
static char st_deps[ST_DEPS_MAX];		/* /etc/services.deps */
static char st_names[ST_PROCS][ST_NAME];	/* les process qui tournent */
static long st_pids[ST_PROCS];
static long st_nprocs;

/* ---- les listes .off et .manual (aussi pour le volet Accès distant) ---- */

static int st_blank (char c) { return c == ' ' || c == '\t' || c == '\r'; }

/* [*b, *e) sans blancs autour */
static void st_trim (const char **b, const char **e)
{
	while (*b < *e && st_blank (**b))
		(*b)++;
	while (*e > *b && (st_blank ((*e)[-1]) || (*e)[-1] == '\n'))
		(*e)--;
}

/* list (terminée par 0), line -> 1 si la ligne (blancs autour ignorés) y est ; comme list_has de korinrt */
int off_has (const char *list, const char *line)
{
	const char *lb = line, *le = line + strlen (line);
	st_trim (&lb, &le);
	if (lb >= le)
		return 0;
	for (const char *p = list; *p;)
	{
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		const char *b = p, *f = e;
		st_trim (&b, &f);
		if (*b != '#' && f - b == le - lb && !memcmp (b, lb, (size_t) (le - lb)))
			return 1;
		p = *e ? e + 1 : e;
	}
	return 0;
}

/* path -> son texte dans buf (size octets, terminé par 0) ; -> octets (0 : absent) */
static long st_read (const char *path, char *buf, long size)
{
	long n = 0;
	int fd = open (path, O_RDONLY);
	if (fd >= 0)
	{
		n = read (fd, buf, (size_t) (size - 1));
		if (n < 0)
			n = 0;
		close (fd);
	}
	buf[n] = 0;
	return n;
}

/* la liste path : line ajoutée (off = 1) ou retirée (0), les autres lignes gardées -> 0, 1 (rien à faire) ou une
 * erreur ; l'appelant s'est élevé pour /etc */
long off_set (const char *path, const char *line, int off)
{
	static char in[ST_OFF_MAX], out[ST_OFF_MAX + ST_LINE + 128];
	st_read (path, in, sizeof in);
	if (off_has (in, line) == off)
		return 1;
	char *o = out;
	for (const char *p = in; *p;)
	{
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		const char *b = p, *f = e;
		st_trim (&b, &f);
		const char *next = *e ? e + 1 : e;
		char one[ST_LINE];
		long l = f - b < ST_LINE - 1 ? f - b : ST_LINE - 1;
		memcpy (one, b, (size_t) l);
		one[l] = 0;
		if (!(l && !off && off_has (line, one)))	/* (la ligne retirée) */
		{
			memcpy (o, p, (size_t) (next - p));
			o += next - p;
		}
		p = next;
	}
	if (off)
	{
		if (o == out)				/* un nouveau fichier : son en-tête */
			o = stpcpy (o, "# Type de demarrage (Reglages > Demarrage, docs/DEMARRAGE.md) : une ligne du fichier de demarrage par entree\n");
		else if (o[-1] != '\n')
			*o++ = '\n';
		o = stpcpy (stpcpy (o, line), "\n");
	}
	*o = 0;
	return file_write (path, out) < 0 ? -1 : 0;
}

/* ---- les entrées ---- */

/* path (« /apps/jet.app/main », « /sys/menubar ») -> le nom de son process (« jet », « menubar ») */
static void st_proc_name (const char *line, char *out)
{
	const char *end = line;
	while (*end && *end != ' ')
		end++;
	const char *b = end;
	while (b > line && b[-1] != '/')
		b--;
	if (end - b == 4 && !memcmp (b, "main", 4) && b > line)	/* « main » : le paquet */
	{
		const char *e2 = b - 1, *b2 = e2;
		while (b2 > line && b2[-1] != '/')
			b2--;
		if (e2 - b2 > 4 && !memcmp (e2 - 4, ".app", 4))
			e2 -= 4;
		b = b2;
		end = e2;
	}
	long n = 0;
	while (b < end && n < ST_NAME - 1)
		out[n++] = *b++;
	out[n] = 0;
}

/* path, son nombre d'octets -> l'Info (0 : inconnu) */
static const Info *st_info_of (const char *path, size_t n)
{
	for (long i = 0; i < INFO_N; i++)
		if (strlen (st_info[i].path) == n && !memcmp (path, st_info[i].path, n))
			return &st_info[i];
	return 0;
}

static size_t st_path_len (const char *line)
{
	size_t n = 0;
	while (line[n] && line[n] != ' ' && line[n] != '\t')
		n++;
	return n;
}

static const char *st_label (const Entry &e) { return e.info ? TR (e.info->label) : e.label; }
static int st_prot (const Entry &e) { return e.sect == S_SYSTEM ? P_LOCK : e.info ? e.info->prot : P_FREE; }

/* une entrée de plus (sans doublon dans sa partie) */
static Entry *st_add (const char *line, long len, int sect, int file)
{
	if (st_n >= ST_MAX || len <= 0)
		return 0;
	if (len > ST_LINE - 1)
		len = ST_LINE - 1;
	Entry &e = st[st_n];
	memset (&e, 0, sizeof e);
	memcpy (e.line, line, (size_t) len);
	e.line[len] = 0;
	for (long i = 0; i < st_n; i++)
		if (st[i].sect == sect && !strcmp (st[i].line, e.line))
			return 0;
	e.sect = (unsigned char) sect;
	e.file = (unsigned char) file;
	e.info = st_info_of (e.line, st_path_len (e.line));
	st_proc_name (e.line, e.name);
	long a = app_find (e.name);		/* une app : son nom lisible (app.txt) */
	strncpy (e.label, a >= 0 && !strncmp (e.line, "/apps/", 6) ? apps[a].label : e.name, sizeof e.label - 1);
	st_n++;
	return &e;
}

/* [b, f) commence par un chemin de programme (« /sys/svc/i2smic ») : lettres, chiffres, « /._- », deux « / » au
 * moins, puis la fin ou un blanc -> 1 */
static int st_path_like (const char *b, const char *f)
{
	int slashes = 0;
	for (; b < f && *b != ' ' && *b != '\t'; b++)
	{
		char c = *b;
		if (c == '/')
			slashes++;
		else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_'
			   || c == '-'))
			return 0;
	}
	return slashes >= 2;
}

/* fichier de démarrage path : ses lignes (et celles en commentaire « #/… ») -> entrées de la partie sect ; off,
 * man = les listes des désactivées, des manuelles */
static void st_load_file (const char *path, int sect, int file, const char *off, const char *man)
{
	static char buf[AP_FILE_MAX];
	st_read (path, buf, sizeof buf);
	for (const char *p = buf; *p;)
	{
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		const char *b = p, *f = e;
		p = *e ? e + 1 : e;
		st_trim (&b, &f);
		int commented = 0;
		while (b < f && (*b == '#' || *b == ' ' || *b == '\t'))	/* « # » et blancs de tête */
			if (*b++ == '#')
				commented = 1;
		if (b >= f || *b != '/')		/* vide, ou un commentaire en mots */
			continue;
		if (commented && !st_path_like (b, f))	/* (« # /bin, /apps, /sys : ... » : des mots) */
			continue;
		Entry *en = st_add (b, f - b, sect, file);
		if (!en)
			continue;
		en->commented = (unsigned char) commented;
		int m = commented || off_has (off, en->line) ? M_OFF : off_has (man, en->line) ? M_MANUAL : M_AUTO;
		en->mode = en->was_mode = (unsigned char) m;
	}
}

/* (ini_read de system.ini) */
static void st_ini_sys (char *key, char *val, void *data)
{
	if (!strcmp (key, "shell"))
		*(int *) data = !strcmp (val, "poche");
}

static void st_load (void)
{
	static char off[ST_OFF_MAX], man[ST_OFF_MAX];
	st_n = 0;
	/* Système : ce qu'init et init2 lancent eux-mêmes */
	for (long i = 0; i < INFO_SYSTEM; i++)
	{
		const Info &in = st_info[i];
		if (i == 0 || access (in.path, F_OK) == 0)	/* (vfs : dans l'initramfs ou l'initrd) */
			st_add (in.path, (long) strlen (in.path), S_SYSTEM, 0);
	}
	/* services.txt, ses listes, ses dépendances */
	st_read (ST_SERVICES_OFF, off, sizeof off);
	st_read (ST_SERVICES_MANUAL, man, sizeof man);
	st_read (ST_SERVICES_DEPS, st_deps, sizeof st_deps);
	st_load_file (ST_SERVICES, S_SERVICES, 0, off, man);
	/* la session du mode, puis startup.txt, et leurs listes personnelles */
	int poche = 0;
	ini_read (P_SYSTEM, st_ini_sys, &poche);
	strcpy (st_session, poche ? "/etc/session.poche" : "/etc/session");
	char cp[160];
	st_read (korin_conf_path ("session.off", cp, sizeof cp, 0), off, sizeof off);
	st_read (korin_conf_path ("session.manual", cp, sizeof cp, 0), man, sizeof man);
	st_load_file (st_session, S_SESSION, 1, off, man);
	korin_conf_path ("startup.txt", st_startup, sizeof st_startup, 0);
	st_load_file (st_startup, S_SESSION, 2, off, man);
	if (st_sel >= st_n)
		st_sel = -1;
	for (long i = 0; st_sel < 0 && i < st_n; i++)	/* le premier service, d'office */
		if (st[i].sect == S_SERVICES)
			st_sel = i;
}

/* ---- les dépendances (/etc/services.deps : « chemin = ce qu'il lui faut ») ---- */

/* e, i = une autre entrée des services -> 1 si e a besoin de i */
static int st_needs (const Entry &e, const Entry &i)
{
	if (e.sect != S_SERVICES || i.sect != S_SERVICES)
		return 0;
	size_t en = st_path_len (e.line), in = st_path_len (i.line);
	for (const char *p = st_deps; *p;)
	{
		while (st_blank (*p))
			p++;
		const char *end = p;
		while (*end && *end != '\n')
			end++;
		if (*p != '#' && !memcmp (p, e.line, en) && (st_blank (p[en]) || p[en] == '='))
		{
			for (const char *q = p + en; q < end;)	/* chaque dépendance */
			{
				while (q < end && (st_blank (*q) || *q == '='))
					q++;
				const char *w = q;
				while (q < end && !st_blank (*q))
					q++;
				if ((size_t) (q - w) == in && !memcmp (w, i.line, in))
					return 1;
			}
			return 0;
		}
		p = *end ? end + 1 : end;
	}
	return 0;
}

/* e, needed_by : la liste des noms (« Réseau, Pseudo-terminaux ») dans out ; running = seulement ceux qui tournent,
 * auto = seulement les automatiques -> leur nombre */
static long st_dep_list (long e, int needed_by, int running, int autom, char *out, size_t cap)
{
	long n = 0;
	*out = 0;
	for (long i = 0; i < st_n; i++)
	{
		if (i == e || !(needed_by ? st_needs (st[i], st[e]) : st_needs (st[e], st[i])))
			continue;
		if ((running && st[i].pid <= 0) || (autom && st[i].mode != M_AUTO))
			continue;
		if (n && strlen (out) + 2 < cap)
			strcat (out, ", ");
		strncat (out, st_label (st[i]), cap - strlen (out) - 1);
		n++;
	}
	return n;
}

/* ---- l'état ---- */

/* les process qui tournent (SYS_PROCLIST_FROM), puis le pid de chaque entrée -> 1 si un état a changé */
static int st_status (void)
{
	enum { PF_N = 16 };
	long list[PF_N * 8];				/* 64 octets par process : pid, …, nom (+24, 32 octets) */
	long from = 0;
	st_nprocs = 0;
	for (;;)
	{
		long n = armpi_syscall (SYS_PROCLIST_FROM, (long) list, PF_N, from, 0, 0, 0);
		if (n <= 0)
			break;
		for (long i = 0; i < n && st_nprocs < ST_PROCS; i++)
		{
			const long *e = list + i * 8;
			from = e[0] & 0xFFFF;
			st_pids[st_nprocs] = e[0];
			memcpy (st_names[st_nprocs], (const char *) (e + 3), ST_NAME - 1);
			st_names[st_nprocs++][ST_NAME - 1] = 0;
		}
		if (n < PF_N)
			break;
	}
	int changed = 0;
	for (long i = 0; i < st_n; i++)
	{
		long pid = 0;
		for (long k = 0; k < st_nprocs; k++)
			if (!strcmp (st_names[k], st[i].name))
			{
				pid = st_pids[k];
				break;
			}
		if ((pid > 0) != (st[i].pid > 0))
			changed = 1;
		st[i].pid = pid;
	}
	return changed;
}

/* ce que l'entrée permet */
static int st_can_start (const Entry &e) { return e.sect != S_SYSTEM && e.pid <= 0 && e.was_mode != M_OFF && e.mode != M_OFF; }
static int st_can_stop (const Entry &e) { return e.sect != S_SYSTEM && e.pid > 0 && st_prot (e) != P_LOCK; }
static int st_can_restart (const Entry &e)
{
	return e.sect != S_SYSTEM && e.pid > 0 && (st_prot (e) != P_LOCK || e.sect == S_SESSION);
}
static int st_can_type (const Entry &e) { return st_prot (e) != P_LOCK; }

/* ---- la liste à l'écran : titres de partie et entrées, à la suite ---- */

/* k = rang à l'écran -> entrée (>= 0), ou -1 - partie pour un titre, -100 pour « Aucun programme » ; -> 0 : au-delà */
static int st_item (long k, long *out)
{
	long r = 0;
	for (int s = 0; s < S_COUNT; s++)
	{
		if (r++ == k)
		{
			*out = -1 - s;
			return 1;
		}
		int any = 0;
		for (long i = 0; i < st_n; i++)
			if (st[i].sect == s)
			{
				any = 1;
				if (r++ == k)
				{
					*out = i;
					return 1;
				}
			}
		if (!any && r++ == k)
		{
			*out = -100;
			return 1;
		}
	}
	return 0;
}

static long st_items (void)
{
	long n = 0, x;
	while (st_item (n, &x))
		n++;
	return n;
}

static long st_item_h (long it) { return it >= 0 ? ST_ROW_H : ST_HEAD_H; }
static long st_bottom (void) { return ui.h - AP_BOTTOM - 4; }
static long st_right (void) { return ui.w - AP_MARGIN; }
static long st_type_x (void) { return st_right () - ST_TYPE_W; }
static long st_state_x (void) { return st_type_x () - 8 - ST_STATE_W; }
static long st_seg_x (void) { return st_right () - (long) M_COUNT * ST_SEG_W; }

/* l'entrée choisie dans la partie visible de la liste */
static void st_show_sel (void)
{
	long n = st_items (), k = -1, it;
	for (long j = 0; j < n; j++)
		if (st_item (j, &it) && it == st_sel)
			k = j;
	if (k < 0)
		return;
	if (k <= st_first)
	{
		st_first = k > 0 ? k - 1 : 0;	/* (son titre de partie avec elle, s'il la précède) */
		long prev;
		if (st_first < k && st_item (st_first, &prev) && prev >= 0)
			st_first = k;
		return;
	}
	for (;;)				/* jusqu'à ce qu'elle tienne en bas */
	{
		long y = ST_Y;
		for (long j = st_first; j <= k; j++)
		{
			st_item (j, &it);
			y += st_item_h (it);
		}
		if (y <= st_bottom () || st_first >= k)
			return;
		st_first++;
	}
}

/* un bouton, grisé s'il ne sert pas */
static void st_button (long x, long y, long w, const char *label, int enabled)
{
	if (enabled && w == BTN_W)
	{
		ap_button (x, y, label, 0);
		return;
	}
	ui_rbox (x, y, w, BTN_H, 6, tok (TJ_LINE), tok (enabled ? TJ_RAISED : TJ_FACE));
	const char *t = TR (label);
	long tw = ui_text_width ((long) t, 14, F ());
	ui_text ((long) t, x + (w - tw) / 2, y + 19, tok (enabled ? TJ_TEXT1 : TJ_TEXT3), 14, F (), 0);
}

/* la barre des actions de l'entrée choisie, puis ses dépendances */
static void st_draw_bar (void)
{
	long x0 = SIDE_W + AP_MARGIN, y = ST_BAR_Y;
	const Entry *e = st_sel >= 0 ? &st[st_sel] : 0;
	st_button (x0, y, BTN_W, st_s_start, e && st_can_start (*e));
	st_button (x0 + BTN_W + 8, y, BTN_W, st_s_stop, e && st_can_stop (*e));
	st_button (x0 + 2 * (BTN_W + 8), y, BTN_W, st_s_restart, e && st_can_restart (*e));
	long sx = st_seg_x ();			/* le type : trois boutons accolés */
	int en = e && st_can_type (*e);
	ui_rbox (sx, y, (long) M_COUNT * ST_SEG_W, BTN_H, 6, tok (TJ_LINE_STRONG), tok (en ? TJ_RAISED : TJ_FACE));
	for (int m = 0; m < M_COUNT; m++)
	{
		long bx = sx + m * ST_SEG_W;
		int on = e && e->mode == m;
		if (on)
		{
			uint32_t c = tok (en ? TJ_ACCENT : TJ_LINE_STRONG);
			ui_rbox (bx, y, ST_SEG_W, BTN_H, 6, c, c);
		}
		else if (m)
			ui_fill (bx, y + 5, 1, BTN_H - 10, tok (TJ_LINE));
		const char *t = TR (st_s_mode[m]);
		long tw = ui_text_width ((long) t, 13, F ());
		uint32_t ink = on ? tok (en ? TJ_ACCENT_INK : TJ_TEXT1) : tok (en ? TJ_TEXT1 : TJ_TEXT3);
		ui_text ((long) t, bx + (ST_SEG_W - tw) / 2, y + 19, ink, 13, on ? FB () : F (), 0);
	}
	char info[300];				/* Nécessite, Requis par */
	if (!e)
		strcpy (info, TR (st_s_choose));
	else if (st_prot (*e) == P_LOCK && e->sect == S_SYSTEM)
		strcpy (info, TR (st_s_always));
	else
	{
		char a[120], b[120];
		long na = st_dep_list (st_sel, 0, 0, 0, a, sizeof a), nb = st_dep_list (st_sel, 1, 0, 0, b, sizeof b);
		info[0] = 0;
		if (na)
			strcat (strcat (strcat (info, TR (st_s_needs)), " "), a);
		if (nb)
		{
			if (na)
				strcat (info, "   ");
			strcat (strcat (strcat (info, TR (st_s_needed)), " "), b);
		}
		if (!na && !nb)
			strcpy (info, TR (st_prot (*e) == P_LOCK ? st_s_always : st_s_nodeps));
	}
	ui_text ((long) info, x0, ST_INFO_Y, tok (TJ_TEXT3), 12, F (), st_right () - x0);
}

static void st_draw (void)
{
	st_draw_bar ();
	long x0 = SIDE_W + AP_MARGIN, y = ST_Y, bottom = st_bottom ();
	long tw = st_state_x () - 12 - x0;		/* le texte d'une entrée */
	ui_fill (x0, y - 1, st_right () - x0, 1, tok (TJ_LINE_STRONG));
	long n = st_items ();
	for (long k = st_first; k < n; k++)
	{
		long it;
		st_item (k, &it);
		long h = st_item_h (it);
		if (y + h > bottom)
			break;
		if (it == -100)
			ui_text ((long) TR (st_s_empty), x0, y + 18, tok (TJ_TEXT3), 12, F (), tw);
		else if (it < 0)			/* titre de partie */
		{
			ui_text ((long) TR (st_s_sect[-1 - it]), x0, y + 22, tok (TJ_TEXT2), 13, FB (), 0);
			ui_fill (x0, y + h - 2, st_right () - x0, 1, tok (TJ_LINE));
		}
		else
		{
			const Entry &e = st[it];
			if (it == st_sel)		/* choisie */
				ui_rbox (x0 - 6, y + 1, st_right () - x0 + 6, h - 2, 6, tok (TJ_ACCENT_LIGHT), tok (TJ_ACCENT_LIGHT));
			const char *label = st_label (e);
			uint32_t c1 = tok (e.mode == M_OFF ? TJ_TEXT2 : TJ_TEXT1);
			ui_text ((long) label, x0, y + 19, c1, 14, FB (), tw);
			const char *args = strchr (e.line, ' ');	/* ses arguments (deux lignes du même programme) */
			long lw = ui_text_width ((long) label, 14, FB ());
			if (e.info && args && lw + 16 < tw)
				ui_text ((long) (args + 1), x0 + lw + 10, y + 19, tok (TJ_TEXT3), 12, F (), tw - lw - 10);
			const char *desc = e.info ? TR (e.info->desc) : e.line;
			ui_text ((long) desc, x0, y + 37, tok (TJ_TEXT3), 12, F (), tw);
			long sx = st_state_x ();		/* l'état : un voyant, puis le mot */
			uint32_t dot = e.pid > 0 ? tok (TJ_SUCCESS) : tok (TJ_LINE_STRONG);
			ui_rbox (sx, y + 17, 8, 8, 4, dot, dot);
			ui_text ((long) TR (e.pid > 0 ? st_s_running : st_s_stopped), sx + 14, y + 26, tok (TJ_TEXT2), 12, F (),
				 ST_STATE_W - 14);
			if (st_prot (e) == P_LOCK)	/* le type (accentué : changé, pas encore appliqué) */
				ui_text ((long) TR (st_s_locked), st_type_x (), y + 26, tok (TJ_TEXT3), 12, F (), ST_TYPE_W);
			else
			{
				int changed = e.mode != e.was_mode;
				ui_text ((long) TR (st_s_mode[e.mode]), st_type_x (), y + 26,
					 tok (changed ? TJ_ACCENT : e.mode == M_OFF ? TJ_TEXT3 : TJ_TEXT1), 12, changed ? FB () : F (),
					 ST_TYPE_W);
			}
			ui_fill (x0, y + h - 1, st_right () - x0, 1, tok (TJ_LINE));
		}
		y += h;
	}
	if (st_first > 0 || y + ST_ROW_H > bottom)	/* trop long : la bande de défilement (cliquable) */
	{
		long top = ST_Y, hh = bottom - top;
		ui_rbox (st_right () + 4, top, 4, hh, 2, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
		long ch = n ? hh * 6 / (n + 6) : hh;
		long cy = n ? top + (hh - ch) * st_first / (n > 1 ? n - 1 : 1) : top;
		ui_rbox (st_right () + 4, cy, 4, ch, 2, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));
	}
}

static int st_wheel (long n)
{
	long o = st_first - n, m = st_items () - 3;
	if (o > m)
		o = m;
	if (o < 0)
		o = 0;
	st_first = o;
	return 1;
}

/* ---- Démarrer, Arrêter, Redémarrer ---- */

/* les process du nom de e, tués (déjà élevé si c'est un service) */
static void st_kill_all (const Entry &e)
{
	long pid = 0;
	while ((pid = proc_find (e.name, pid)) != 0)
		armpi_syscall (SYS_KILL, pid, 0, 0, 0, 0, 0);
}

/* op, la ligne -> la réponse d'init2 */
static long st_init2 (long op, const char *line)
{
	static unsigned char m[512] __attribute__ ((aligned (16)));
	long ch = svc_open ("init2", 0);
	if (ch < 0)
		return ch;
	memset (m, 0, sizeof m);
	*(long *) m = op;
	size_t l = strlen (line);
	if (l > 400)
		l = 400;
	memcpy (m + 48, line, l);
	*(unsigned *) (m + 40) = (unsigned) (l + 1);
	long r = svc_call (ch, m);
	return r < 0 ? r : *(long *) m;
}

/* une ligne de la session, lancée comme Elegant le fait (le chemin, ses arguments séparés par des espaces) */
static long st_spawn_line (const char *line)
{
	char buf[ST_LINE];
	strcpy (buf, line);
	char *argv[10];
	int argc = 0;
	for (char *p = buf; *p && argc < 9;)
	{
		while (*p == ' ')
			*p++ = 0;
		if (!*p)
			break;
		argv[argc++] = p;
		while (*p && *p != ' ')
			p++;
	}
	argv[argc] = 0;
	if (!argc)
		return -1;
	pid_t pid;
	int r = posix_spawn (&pid, argv[0], 0, 0, argv, environ);
	return r ? -r : pid;
}

/* une question (« Arrêter : Réseau ? ») avec ses raisons -> 1 : oui */
static int st_ask (const char *what, const Entry &e, const char *why1, const char *why2)
{
	char q[512];
	strcpy (stpcpy (stpcpy (stpcpy (q, TR (what)), " "), st_label (e)), " ?");
	if (why1 && *why1)
		strcat (strcat (q, "\n"), why1);
	if (why2 && *why2)
		strcat (strcat (q, "\n"), why2);
	return ui_message ((long) TR ("Démarrage"), (long) q, UM_YESNO) == 1;
}

/* act : 0 démarrer, 1 arrêter, 2 redémarrer */
static void st_act (long i, int act)
{
	Entry &e = st[i];
	if (act)				/* arrêter : ce qui s'en sert, les indispensables à confirmer */
	{
		char who[160], why[220] = "";
		if (st_dep_list (i, 1, 1, 0, who, sizeof who))
			strcat (strcat (strcpy (why, TR ("Ceux-ci s'en servent :")), " "), who);
		if ((st_prot (e) == P_WARN || *why)
		    && !st_ask (act == 1 ? "Arrêter maintenant :" : "Redémarrer maintenant :", e, why, 0))
			return;
	}
	long r = 0;
	if (e.sect == S_SERVICES)		/* en uid 0, par init2 (qui cesse de le surveiller ou le surveille) */
	{
		unsigned me;
		if (ap_elevate (TR ("Réglages veut arrêter ou démarrer un service du système."), &me))
		{
			ap.msg = st_s_denied;
			return;
		}
		if (act)
		{
			r = st_init2 (INIT_OP_SVC_STOP, e.line);
			if (r == ST_E_NOENT)	/* pas lancé par init2 (ou plus surveillé) : arrêté tout court */
			{
				st_kill_all (e);
				r = 0;
			}
			if (act == 2)
				sys_sleep (300);
		}
		if (act != 1)
		{
			r = st_init2 (INIT_OP_SVC_START, e.line);	/* (ses dépendances d'abord) */
			if (r == ST_E_EXIST)
				r = 0;
		}
		ap_unelevate (me);
	}
	else					/* la session : ses programmes sont à l'utilisateur */
	{
		if (act)
		{
			st_kill_all (e);
			if (act == 2)
				sys_sleep (300);
		}
		if (act != 1)
			r = st_spawn_line (e.line);
	}
	ap.msg = r == ST_E_NOENT ? st_s_missing : r < 0 ? st_s_failed : 0;
	sys_sleep (act == 1 ? 300 : 500);	/* (le temps qu'il parte ou arrive) */
	st_status ();
}

/* ---- le type ---- */

static void st_set_mode (long i, int m)
{
	Entry &e = st[i];
	if (!st_can_type (e) || e.mode == m)
		return;
	if (m != M_AUTO)
	{
		char who[160], why[220] = "";
		if (m == M_OFF && st_dep_list (i, 1, 0, 1, who, sizeof who))	/* des automatiques qui en ont besoin */
			strcat (strcat (strcpy (why, TR ("Requis par :")), " "), who);
		const char *warn = st_prot (e) == P_WARN ? TR ("Une partie du système ne marchera plus sans lui.") : 0;
		if ((warn || *why) && !st_ask (m == M_OFF ? "Désactiver :" : "Démarrage manuel :", e, why, warn))
			return;
	}
	e.mode = (unsigned char) m;
	ap.msg = 0;
}

/* ---- clics, clavier ---- */

static int st_click (long x, long y)
{
	if (y >= ST_BAR_Y && y < (long) ST_BAR_Y + BTN_H)	/* la barre */
	{
		if (st_sel < 0)
			return 0;
		long x0 = SIDE_W + AP_MARGIN;
		for (int a = 0; a < 3; a++)
		{
			long bx = x0 + a * (BTN_W + 8);
			if (x >= bx && x < bx + BTN_W)
			{
				const Entry &e = st[st_sel];
				int ok = a == 0 ? st_can_start (e) : a == 1 ? st_can_stop (e) : st_can_restart (e);
				if (!ok)
				{
					if (a == 0 && e.mode == M_OFF)
						ap.msg = st_s_disabled;
					return 1;
				}
				st_act (st_sel, a == 0 ? 0 : a == 1 ? 1 : 2);
				return 1;
			}
		}
		long sx = st_seg_x ();
		if (x >= sx && x < sx + (long) M_COUNT * ST_SEG_W)
		{
			st_set_mode (st_sel, (int) ((x - sx) / ST_SEG_W));
			return 1;
		}
		return 0;
	}
	if (y < ST_Y || y >= st_bottom ())
		return 0;
	if (x >= st_right () + 1)		/* la bande (tactile) : la moitié du haut remonte, celle du bas descend */
		return st_wheel (y < (ST_Y + st_bottom ()) / 2 ? 5 : -5);
	long yy = ST_Y, n = st_items ();
	for (long k = st_first; k < n; k++)
	{
		long it;
		st_item (k, &it);
		long h = st_item_h (it);
		if (y < yy + h)
		{
			if (it < 0)
				return 0;
			st_sel = it;
			ap.msg = 0;
			return 1;
		}
		yy += h;
	}
	return 0;
}

/* (ap.key) Haut, Bas : l'entrée d'avant, d'après -> 1 si prise */
static int st_key (void)
{
	long code = ui_ev_field (&ui, EV_CODE);
	if (code != KEY_UP && code != KEY_DOWN)
		return 0;
	long i = st_sel < 0 ? 0 : st_sel + (code == KEY_UP ? -1 : 1);
	if (i < 0 || i >= st_n)
		return 1;
	st_sel = i;
	ap.msg = 0;
	st_show_sel ();
	return 1;
}

/* ---- Appliquer ---- */

/* fichier de démarrage path : les lignes en commentaire des entrées remises en service, sans leur « # » -> 0, 1
 * (rien) ou une erreur */
static long st_uncomment (const char *path, int file)
{
	static char in[AP_FILE_MAX];
	int any = 0;
	for (long i = 0; i < st_n; i++)
		if (st[i].file == file && st[i].sect != S_SYSTEM && st[i].commented && st[i].mode != M_OFF)
			any = 1;
	if (!any)
		return 1;
	st_read (path, in, sizeof in);
	char *o = ap_out;
	for (const char *p = in; *p && o - ap_out < AP_FILE_MAX + 64;)
	{
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		const char *next = *e ? e + 1 : e;
		const char *q = p;
		while (*q == '#' || *q == ' ' || *q == '\t')
			q++;
		if (q != p && *q == '/')
			for (long i = 0; i < st_n; i++)
			{
				const Entry &en = st[i];
				size_t l = strlen (en.line);
				if (en.file == file && en.sect != S_SYSTEM && en.commented && en.mode != M_OFF
				    && !strncmp (q, en.line, l) && (q + l == e || st_blank (q[l])))
				{
					p = q;			/* la ligne sans ses « # » */
					break;
				}
			}
		memcpy (o, p, (size_t) (next - p));
		o += next - p;
		p = next;
	}
	*o = 0;
	return file_write (path, ap_out) < 0 ? -1 : 0;
}

/* sect (S_SERVICES, S_SESSION), les chemins de ses listes -> 0 (écrites), 1 (rien à faire), erreur */
static long st_write_lists (int sect, const char *off, const char *man)
{
	long r = 1;
	for (long i = 0; i < st_n; i++)
	{
		const Entry &e = st[i];
		if (e.sect != sect)
			continue;
		long w = off_set (off, e.line, e.mode == M_OFF && !e.commented);	/* (en commentaire : le « # » suffit) */
		long v = off_set (man, e.line, e.mode == M_MANUAL);
		if (w < 0 || v < 0)
			return -1;
		if (!w || !v)
			r = 0;
	}
	return r;
}

/* -> 1 si une entrée de la partie sect a changé */
static int st_changed (int sect)
{
	for (long i = 0; i < st_n; i++)
		if (st[i].sect == sect && st[i].mode != st[i].was_mode)
			return 1;
	return 0;
}

static void st_apply (void)
{
	int svc = st_changed (S_SERVICES), ses = st_changed (S_SESSION);
	if (!svc && !ses)
	{
		ap.msg = st_s_nochange;
		return;
	}
	long err = 0;
	int home = !strncmp (st_startup, "HOME:", 5);
	if (ses)				/* la session : personnel, sans élévation */
	{
		char co[160], cm[160];
		if (st_write_lists (S_SESSION, korin_conf_path ("session.off", co, sizeof co, 1),
				    korin_conf_path ("session.manual", cm, sizeof cm, 1)) < 0)
			err = -1;
		if (home && st_uncomment (st_startup, 2) < 0)
			err = -1;
	}
	int need_root = svc;			/* /etc : les listes des services, services.txt, session (en commentaire) */
	for (long i = 0; i < st_n; i++)
		if (st[i].sect == S_SESSION && st[i].commented && st[i].mode != M_OFF && (st[i].file == 1 || !home))
			need_root = 1;
	if (need_root)
	{
		unsigned me;
		if (ap_elevate (TR ("Réglages veut modifier ce qui est lancé au démarrage."), &me))
		{
			ap.msg = st_s_denied;
			return;
		}
		if (svc && (st_write_lists (S_SERVICES, ST_SERVICES_OFF, ST_SERVICES_MANUAL) < 0
			    || st_uncomment (ST_SERVICES, 0) < 0))
			err = -1;
		if (st_uncomment (st_session, 1) < 0)
			err = -1;
		if (!home && st_uncomment (st_startup, 2) < 0)
			err = -1;
		ap_unelevate (me);
	}
	const char *msg = err ? st_s_write : st_s_saved;
	long first = st_first;
	st_load ();				/* relu : l'état des fichiers */
	st_status ();
	st_first = first;
	ap.msg = msg;
}

/* ---- le volet ---- */

static void st_open (void)
{
	ap.sub = st_s_sub;
	ap.draw = st_draw;
	ap.click = st_click;
	ap.wheel = st_wheel;
	ap.tick = st_status;
	ap.key = st_key;
	st_first = 0;
	st_sel = -1;
	st_load ();
	st_status ();
}

const Applet startup_applet = { "startup", "D\xC3\xA9marrage", "D\xC3\xA9marrage", "taskman", st_open, st_apply };
