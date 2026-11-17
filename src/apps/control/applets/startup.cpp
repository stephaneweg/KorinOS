/*
 * applets/startup.cpp -- le volet Démarrage du panneau de configuration (docs/DEMARRAGE.md) : ce que Korin lance au
 * démarrage, en trois parties, chaque entrée avec un nom et une description lisibles, son état (le process tourne ou
 * pas, relu chaque seconde), un bouton Arrêter / Démarrer quand c'est sûr et un interrupteur :
 *   * Système : ce qu'init et init2 lancent toujours (vfs, busd, authd, hwmanager, hidmanager, Elegant), sans
 *     interrupteur ni bouton ;
 *   * Services : les lignes de /etc/services.txt (init2, en uid 0). Couper un service l'écrit dans /etc/services.off
 *     (init2 saute ces lignes), sans toucher à services.txt : une mise à jour du paquet peut ainsi toujours le
 *     remplacer (les fichiers de « config = » ne sont remplacés que s'ils n'ont pas été modifiés). Une ligne en
 *     commentaire (« #/sys/svc/telnetd », écrite par les volets Accès distant ou Serveur web) est montrée coupée ; la
 *     rallumer retire le « # ». Écrire dans /etc demande l'élévation (ap_elevate) ; Arrêter et Démarrer passent par
 *     init2 (INIT_OP_SVC_STOP, INIT_OP_SVC_START), qui cesse alors de surveiller le service ou le surveille ;
 *   * Session : les programmes du bureau (/etc/session, ou /etc/session.poche en mode poche ; Elegant les lance) et
 *     ceux de startup.txt (init2, une fois la session ouverte). Les couper les écrit dans session.off, personnel
 *     (HOME:/etc/session.off) : Elegant et init2 sautent ces lignes ; pas d'élévation.
 * Les indispensables (vterm, ps2kbd, menubar, panel, l'accueil du mode poche…) n'ont pas d'interrupteur ; d'autres
 * (netd, sound, pkgd, v3d, notifyd, clipd) demandent confirmation avant d'être coupés ou arrêtés.
 * Appliquer écrit les listes : effet au prochain démarrage (ou à la prochaine session).
 */
#include <fcntl.h>
#include <spawn.h>
#include <string.h>
#include <unistd.h>
#include <armpi/syscall.h>
#include <armpi/userconf.h>
#include "../control.h"

extern char **environ;

enum
{
	SYS_KILL = 0x14,
	SYS_PROCLIST_FROM = 0x1F,
	INIT_OP_SVC_STOP = 4,			/* (src/inc/armpi.inc) */
	INIT_OP_SVC_START = 5,
	E_NOENT = -2,
	E_EXIST = -17,
	ST_MAX = 64,				/* entrées */
	ST_LINE = 128,
	ST_NAME = 32,
	ST_PROCS = 256,				/* process lus */
	ST_Y = AP_HEAD + 22,			/* haut de la liste (sous le sous-titre) */
	ST_HEAD_H = 30,				/* un titre de partie */
	ST_ROW_H = 46,				/* une entrée */
	ST_SW_W = 40,				/* l'interrupteur */
	ST_SW_H = 22,
	ST_STATE_W = 92,
	ST_OFF_MAX = 2048,
};

/* les parties */
enum { S_SYSTEM, S_SERVICES, S_SESSION, S_COUNT };

/* protection : 0 libre, 1 confirmation, 2 indispensable (ni interrupteur ni Arrêter) */
enum { P_FREE, P_WARN, P_LOCK };

struct Info
{
	const char *path;			/* le chemin (sans arguments) */
	const char *label;
	const char *desc;
	int prot;
};

/* les programmes connus ; un autre : son nom de fichier, et la ligne pour description */
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
	{ "/sys/svc/ps2kbd", "Clavier du portable", "Le clavier interne et la souris PS/2.", P_LOCK },
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
enum { INFO_N = sizeof st_info / sizeof st_info[0] };

/* une entrée de la liste */
struct Entry
{
	char line[ST_LINE];			/* « chemin [arguments] », telle que dans son fichier */
	char name[ST_NAME];			/* le nom du process */
	const Info *info;			/* (0 : inconnu) */
	unsigned char sect;
	unsigned char on;			/* l'interrupteur */
	unsigned char was_on;			/* ce que disent les fichiers */
	unsigned char commented;		/* ligne en commentaire dans son fichier */
	unsigned char file;			/* 0 services.txt, 1 session, 2 startup.txt */
	long pid;				/* > 0 : tourne */
};

static const char st_s_sub[] = "Ce que Korin lance au démarrage. Coupé : il ne démarre plus au prochain démarrage.";
static const char *const st_s_sect[S_COUNT] = {
	"Système (toujours lancé)", "Services du système", "Programmes de la session",
};
static const char st_s_running[] = "En cours";
static const char st_s_stopped[] = "Arrêté";
static const char st_s_stop[] = "Arrêter";
static const char st_s_start[] = "Démarrer";
static const char st_s_locked[] = "Indispensable";
static const char st_s_saved[] = "Enregistré : effet au prochain démarrage.";
static const char st_s_nochange[] = "Rien n'a changé.";
static const char st_s_denied[] = "Autorisation refusée.";
static const char st_s_write[] = "Enregistrement impossible.";
static const char st_s_failed[] = "Impossible : le programme ne répond pas.";
static const char st_s_empty[] = "Aucun programme.";
#define ST_SERVICES	"/etc/services.txt"
#define ST_SERVICES_OFF	"/etc/services.off"

static Entry st[ST_MAX];
static long st_n;
static long st_first;				/* première ligne montrée (titres compris) */
static char st_session[64];			/* /etc/session ou /etc/session.poche */
static char st_startup[160];			/* startup.txt lu (personnel, sinon /etc) */
static char st_names[ST_PROCS][ST_NAME];	/* les process qui tournent */
static long st_pids[ST_PROCS];
static long st_nprocs;

/* ---- les listes .off (aussi pour le volet Accès distant) ---- */

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
	static char in[ST_OFF_MAX], out[ST_OFF_MAX + ST_LINE + 4];
	long n = st_read (path, in, sizeof in);
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
	(void) n;
	if (off)
	{
		if (o == out)				/* un nouveau fichier : son en-tête */
			o = stpcpy (o, "# Ce qui ne demarre plus (Reglages > Demarrage) : une ligne du fichier de demarrage par entree\n");
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

static const Info *st_find_info (const char *line)
{
	for (long i = 0; i < INFO_N; i++)
	{
		size_t l = strlen (st_info[i].path);
		if (!strncmp (line, st_info[i].path, l) && (!line[l] || line[l] == ' '))
			return &st_info[i];
	}
	return 0;
}

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
	e.info = st_find_info (e.line);
	st_proc_name (e.line, e.name);
	st_n++;
	return &e;
}

/* fichier de démarrage path : ses lignes (et celles en commentaire « #/… ») -> entrées de la partie sect ; off = la
 * liste des coupées */
static void st_load_file (const char *path, int sect, int file, const char *off)
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
		Entry *en = st_add (b, f - b, sect, file);
		if (!en)
			continue;
		en->commented = (unsigned char) commented;
		en->on = en->was_on = (unsigned char) (!commented && !off_has (off, en->line));
	}
}

/* (ini_read de system.ini) */
static void st_ini_sys (char *key, char *val, void *data)
{
	if (!strcmp (key, "shell"))
		*(int *) data = !strcmp (val, "poche");
}

/* -> 1 si p est un chemin qui existe */
static int st_exists (const char *p) { return access (p, F_OK) == 0; }

static void st_load (void)
{
	static char off[ST_OFF_MAX];
	st_n = 0;
	/* Système : ce qu'init et init2 lancent eux-mêmes */
	for (long i = 0; i < 6; i++)
	{
		const Info &in = st_info[i];
		if (i == 0 || st_exists (in.path))	/* (vfs : dans l'initramfs ou l'initrd) */
		{
			Entry *e = st_add (in.path, (long) strlen (in.path), S_SYSTEM, 0);
			if (e)
				e->on = e->was_on = 1;
		}
	}
	/* services.txt, moins services.off */
	st_read (ST_SERVICES_OFF, off, sizeof off);
	st_load_file (ST_SERVICES, S_SERVICES, 0, off);
	/* la session du mode, puis startup.txt, moins session.off */
	int poche = 0;
	ini_read (P_SYSTEM, st_ini_sys, &poche);
	strcpy (st_session, poche ? "/etc/session.poche" : "/etc/session");
	char cp[160];
	st_read (korin_conf_path ("session.off", cp, sizeof cp, 0), off, sizeof off);
	st_load_file (st_session, S_SESSION, 1, off);
	korin_conf_path ("startup.txt", st_startup, sizeof st_startup, 0);
	st_load_file (st_startup, S_SESSION, 2, off);
}

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

/* ---- la liste à l'écran : titres de partie et entrées, à la suite ---- */

/* k = rang à l'écran -> entrée (>= 0), ou -1 - partie pour un titre ; -> 0 : au-delà */
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
		if (!any && r++ == k)			/* « Aucun programme. » */
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

static long st_bottom (void) { return ui.h - AP_BOTTOM - 4; }
static long st_right (void) { return ui.w - AP_MARGIN; }
static long st_sw_x (void) { return st_right () - ST_SW_W; }
static long st_btn_x (void) { return st_sw_x () - 12 - BTN_W; }
static long st_state_x (void) { return st_btn_x () - 8 - ST_STATE_W; }

/* l'entrée peut-elle être arrêtée ou démarrée d'ici ? */
static int st_can_act (const Entry &e)
{
	if (e.sect == S_SYSTEM)
		return 0;
	if (e.pid > 0)
		return !e.info || e.info->prot != P_LOCK;
	return 1;
}

static void st_switch (long x, long y, int on, int enabled)
{
	uint32_t track = on ? tok (TJ_ACCENT) : tok (TJ_LINE_STRONG);
	if (!enabled)
		track = tok (TJ_LINE);
	ui_rbox (x, y, ST_SW_W, ST_SW_H, ST_SW_H / 2, track, track);
	long kx = on ? x + ST_SW_W - ST_SW_H + 2 : x + 2;
	uint32_t knob = on ? tok (TJ_ACCENT_INK) : tok (TJ_SURFACE);
	ui_rbox (kx, y + 2, ST_SW_H - 4, ST_SW_H - 4, (ST_SW_H - 4) / 2, knob, knob);
}

static void st_draw (void)
{
	long x0 = SIDE_W + AP_MARGIN, y = ST_Y, bottom = st_bottom ();
	long tw = st_state_x () - 12 - x0;		/* le texte d'une entrée */
	long n = st_items ();
	for (long k = st_first; k < n; k++)
	{
		long it;
		st_item (k, &it);
		long h = it >= 0 ? ST_ROW_H : ST_HEAD_H;
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
			const char *label = e.info ? TR (e.info->label) : e.name;
			uint32_t c1 = tok (e.on ? TJ_TEXT1 : TJ_TEXT2);
			ui_text ((long) label, x0, y + 19, c1, 14, FB (), tw);
			const char *desc = e.info ? TR (e.info->desc) : e.line;
			ui_text ((long) desc, x0, y + 37, tok (TJ_TEXT3), 12, F (), tw);
			long sx = st_state_x ();		/* l'état : un voyant, puis le mot */
			uint32_t dot = e.pid > 0 ? tok (TJ_SUCCESS) : tok (TJ_LINE_STRONG);
			ui_rbox (sx, y + 17, 8, 8, 4, dot, dot);
			ui_text ((long) TR (e.pid > 0 ? st_s_running : st_s_stopped), sx + 14, y + 26, tok (TJ_TEXT2), 12, F (),
				 ST_STATE_W - 14);
			if (st_can_act (e))
				ap_button (st_btn_x (), y + ((long) ST_ROW_H - (long) BTN_H) / 2, e.pid > 0 ? st_s_stop : st_s_start, 0);
			if (e.sect == S_SYSTEM || (e.info && e.info->prot == P_LOCK))
				ui_text ((long) TR (st_s_locked), st_sw_x () - 36, y + 27, tok (TJ_TEXT3), 11, F (), 0);
			else
				st_switch (st_sw_x (), y + (ST_ROW_H - ST_SW_H) / 2, e.on, 1);
			ui_fill (x0, y + h - 1, st_right () - x0, 1, tok (TJ_LINE));
		}
		y += h;
	}
	if (st_first > 0 || y + ST_ROW_H > bottom)	/* trop long : le rappel de la molette, en bande */
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

/* ---- Arrêter, Démarrer ---- */

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

static void st_act (Entry &e)
{
	int stop = e.pid > 0;
	const char *label = e.info ? TR (e.info->label) : e.name;
	if (stop && e.info && e.info->prot == P_WARN)
	{
		char q[256];
		strcpy (stpcpy (stpcpy (q, TR ("Arrêter maintenant :")), " "), label);
		strcat (q, " ?");
		if (ui_message ((long) TR ("Démarrage"), (long) q, UM_YESNO) != 1)
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
		r = st_init2 (stop ? INIT_OP_SVC_STOP : INIT_OP_SVC_START, e.line);
		if (stop && r == E_NOENT)	/* pas lancé par init2 (ou plus surveillé) : arrêté tout court */
		{
			st_kill_all (e);
			r = 0;
		}
		if (!stop && r == E_EXIST)
			r = 0;
		ap_unelevate (me);
	}
	else if (stop)				/* la session : ses programmes sont à l'utilisateur */
		st_kill_all (e);
	else
		r = st_spawn_line (e.line);
	ap.msg = r < 0 ? st_s_failed : 0;
	sys_sleep (stop ? 300 : 500);		/* (le temps qu'il parte ou arrive) */
	st_status ();
}

/* ---- l'interrupteur ---- */

static void st_toggle (Entry &e)
{
	if (e.sect == S_SYSTEM || (e.info && e.info->prot == P_LOCK))
		return;
	if (e.on && e.info && e.info->prot == P_WARN)
	{
		char q[300];
		strcpy (stpcpy (stpcpy (q, TR ("Ne plus lancer au démarrage :")), " "), TR (e.info->label));
		strcat (strcat (q, " ?\n"), TR ("Une partie du système ne marchera plus sans lui."));
		if (ui_message ((long) TR ("Démarrage"), (long) q, UM_YESNO) != 1)
			return;
	}
	e.on ^= 1;
	ap.msg = 0;
}

static int st_click (long x, long y)
{
	if (y < ST_Y || y >= st_bottom ())
		return 0;
	long yy = ST_Y, n = st_items ();
	for (long k = st_first; k < n; k++)
	{
		long it;
		st_item (k, &it);
		long h = it >= 0 ? ST_ROW_H : ST_HEAD_H;
		if (y < yy + h)
		{
			if (it < 0)
				return 0;
			Entry &e = st[it];
			if (x >= st_sw_x () - 8 && x < st_right ())
			{
				st_toggle (e);
				return 1;
			}
			long by = yy + ((long) ST_ROW_H - (long) BTN_H) / 2;
			if (x >= st_btn_x () && x < st_btn_x () + BTN_W && y >= by && y < by + BTN_H && st_can_act (e))
			{
				st_act (e);
				return 1;
			}
			return 0;
		}
		yy += h;
	}
	return 0;
}

/* ---- Appliquer ---- */

/* fichier de démarrage path : les lignes en commentaire des entrées rallumées, sans leur « # » -> 0, 1 (rien) ou
 * une erreur */
static long st_uncomment (const char *path, int file)
{
	static char in[AP_FILE_MAX];
	int any = 0;
	for (long i = 0; i < st_n; i++)
		if (st[i].file == file && st[i].sect != S_SYSTEM && st[i].commented && st[i].on)
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
				if (en.file == file && en.commented && en.on && !strncmp (q, en.line, l)
				    && (q + l == e || st_blank (q[l])))
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

/* sect (S_SERVICES, S_SESSION), path de sa liste .off -> 0 (écrite), 1 (rien à faire), erreur */
static long st_write_off (int sect, const char *path)
{
	long r = 1;
	for (long i = 0; i < st_n; i++)
	{
		const Entry &e = st[i];
		if (e.sect != sect || e.commented)
			continue;
		long w = off_set (path, e.line, !e.on);
		if (w < 0)
			return w;
		if (!w)
			r = 0;
	}
	return r;
}

/* -> 1 si une entrée de la partie sect a changé */
static int st_changed (int sect)
{
	for (long i = 0; i < st_n; i++)
		if (st[i].sect == sect && st[i].on != st[i].was_on)
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
	if (ses)				/* la session : personnel, sans élévation */
	{
		char cp[160];
		long r = st_write_off (S_SESSION, korin_conf_path ("session.off", cp, sizeof cp, 1));
		if (r < 0)
			err = r;
		if (!strncmp (st_startup, "HOME:", 5) && st_uncomment (st_startup, 2) < 0)
			err = -1;
	}
	int need_root = svc;			/* /etc : services.off, services.txt, session (lignes en commentaire) */
	for (long i = 0; i < st_n; i++)
		if (st[i].sect == S_SESSION && st[i].commented && st[i].on && (st[i].file == 1 || strncmp (st_startup, "HOME:", 5)))
			need_root = 1;
	if (need_root)
	{
		unsigned me;
		if (ap_elevate (TR ("Réglages veut modifier ce qui est lancé au démarrage."), &me))
		{
			ap.msg = st_s_denied;
			return;
		}
		if (svc)
		{
			long r = st_write_off (S_SERVICES, ST_SERVICES_OFF);
			if (r < 0 || st_uncomment (ST_SERVICES, 0) < 0)
				err = -1;
		}
		if (st_uncomment (st_session, 1) < 0)
			err = -1;
		if (strncmp (st_startup, "HOME:", 5) && st_uncomment (st_startup, 2) < 0)
			err = -1;
		ap_unelevate (me);
	}
	ap.msg = err ? st_s_write : st_s_saved;
	const char *msg = ap.msg;
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
	st_first = 0;
	st_load ();
	st_status ();
}

const Applet startup_applet = { "startup", "D\xC3\xA9marrage", "D\xC3\xA9marrage", "taskman", st_open, st_apply };
