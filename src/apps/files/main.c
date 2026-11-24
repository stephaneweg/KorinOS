/*
 * files -- le navigateur de fichiers de Korin OS. Port en C de files.asm (memes fenetre, panneau,
 * menus, raccourcis, textes ; un seul source pour le Pi et le PC) ; l'asm reste dans le dossier
 * jusqu'a sa validation sur le Pi.
 *
 * A gauche, un panneau : les volumes montes (service vfs, relu a chaque montage ou demontage publie
 * sur le bus, « korin.storage.volume.* », sinon chaque seconde ; bouton d'ejection des cles USB) et
 * les acces rapides (/etc/places.ini). A droite : en-tete (precedent, suivant, parent, fil
 * d'Ariane, choix de la vue ; FRAME_CAP_W libres a droite), contenu du dossier courant (dossiers
 * d'abord, tri par nom ; paquets .app avec leur icone et leur nom), ligne d'etat. Barre de menus
 * globale. Ouvrir : dossier -> on y entre, .app -> son main, autre -> son application (UIKit :
 * ui_assoc_open). Clavier : fleches, Entree, Retour, Suppr, F2 (renommer), Ctrl+C, Ctrl+X, Ctrl+V
 * (copier, couper, coller : dialogue de progression, Annuler), Ctrl+N, Ctrl+L, F5. Glisser un
 * element hors de la fenetre, y deposer des fichiers.
 */
#include <armpi/userconf.h>
#include <armpi/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <uikit.h>
#include <buskit_abi.h>
#include <netkit_abi.h>

extern char **environ;

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

/* ---- appels systeme (src/inc/syscalls.inc) et service vfs (src/inc/fs.inc) ---- */
enum { SYS_SLEEP_ = 0x17, SYS_TIME_MS_ = 0x19, SYS_SVC_OPEN_ = 0x31, SYS_SVC_CALL_ = 0x32 };
enum { VFS_MOUNT_ = 20, VFS_UNMOUNT_ = 21, VFS_VOLUMES_ = 22, VFS_MOUNTS_ = 53 };
static long sys3 (long n, long a, long b) { return armpi_syscall (n, a, b, 0, 0, 0, 0); }
static long time_ms (void) { return sys3 (SYS_TIME_MS_, 0, 0); }

struct svc_msg					/* message de 512 octets (MSG_*) */
{
	long op, a, b, c, d;
	unsigned len, pid;
	unsigned char data[464];
};

#define BUS_TOPIC_VOLUME "korin.storage.volume."	/* (src/inc/bus.inc) */
#define BUS_TOPIC_CHANGED "korin.files.changed"	/* dossiers changes par une fenetre de Fichiers (docs/BUS.md) */
#define GDRIVEFS	"/sys/svc/gdrivefs"		/* (« Se connecter à Google Drive… » s'il est là) */

#define WIN_W		760
#define WIN_H		480
#define PAN_W		190			/* panneau de gauche */
#define PAN_Y0		30			/* premiere ligne de volume */
#define VROW_H		48			/* ligne de volume (nom, place ou genre, jauge) */
#define PROW_H		28			/* ligne d'acces rapide */
#define PL_GAP		30			/* en-tete des acces rapides */
#define TOP_H		44			/* en-tete */
#define LIST_Y		(TOP_H + 3)
#define NAV_X		(PAN_W + 8)		/* precedent, suivant, parent */
#define NAV_Y		6
#define NAV_W		32
#define NAV_H		32
#define NAV_PX		16
#define SEG_W		28			/* un segment icones / liste */
#define SEG_RAIL	(2 * SEG_W + 4)
#define SEG_Y		8
#define SEG_H		28
#define SEG_X_R		(FRAME_CAP_W + 8 + SEG_RAIL)	/* x du rail, depuis la droite */
#define CR_X0		(NAV_X + 3 * NAV_W + 10)	/* fil d'Ariane */
#define CR_R		(SEG_X_R + 52)		/* sa fin, depuis la droite (avant le bouton Deux vues) */
#define CR_BASE		27			/* ligne de base */
#define CR_PX		14
#define CR_GAP		6			/* de part et d'autre du separateur */
#define CR_MAX		32
#define HIST_MAX	16			/* dossiers de l'historique */
#define HIST_SZ		272
#define H_BACK		1			/* elements de l'en-tete (head_at) */
#define H_NEXT		2
#define H_UP		3
#define H_ICONS		4
#define H_LIST		5
#define H_CRUMB		8			/* 8 + rang du segment */
#define SB_H0		24			/* ligne d'etat */
#define BAND_H		64			/* elle devient la bande de progression d'une copie */
#define SB_H		(band_on ? BAND_H : SB_H0)
#define ROW_H		26
#define MAXE		4096
#define K_FILE		0
#define K_DIR		1
#define K_APP		2
#define VMAX		8
#define VE_SZ		56			/* entree de VFS_VOLUMES : nom 16, source 24, service 16 */
#define PL_MAX		16
#define PL_BUF		2048
#define DCLICK_MS	600
#define DRAG_PX		6			/* glisser un element : a partir de */
#define POLL_MS		1000

/* codes HID */
#define KEY_A_		0x04
#define KEY_C_		0x06
#define KEY_V_		0x19
#define KEY_X_		0x1B
#define KEY_N_		0x11
#define KEY_K_		0x0E
#define KEY_L_		0x0F
#define KEY_R_		0x15
#define KEY_H_		0x0B
#define KEY_ESC_	0x29
#define KEY_TAB_	0x2B
#define KEY_W_		0x1A
#define KEY_F2_		0x3B
#define KEY_F5_		0x3E
#define KEY_KPENT_	0x58

/* entrees des menus */
#define ID_OPEN		1
#define ID_NEWDIR	2
#define ID_DELETE	3
#define ID_REFRESH	4
#define ID_CLOSE	5
#define ID_OPENWITH	6
#define ID_RENAME	7
#define ID_COPY		8
#define ID_CUT		9
#define ID_PASTE	12
#define ID_PARENT	10
#define ID_SD0		11
#define ID_PLACE	100			/* 100 + rang de l'acces rapide (PL_MAX : jusqu'a 115) */
#define ID_EJECT	30
#define ID_CONNECT	31
#define ID_GDRIVE	32
#define ID_ICONS	40
#define ID_LIST		41
#define ID_DUAL		42			/* Affichage > Deux vues */
#define ID_HIDDEN	43			/* Affichage > Fichiers caches (Ctrl+H) */
#define ID_ALLVOL	44			/* Affichage > Tous les volumes */
#define ID_COPYTO	13			/* Edition > Copier vers l'autre vue */
#define ID_MOVETO	14
#define ID_LOCATION	15			/* Aller > Aller a l'emplacement (Ctrl+L) */

#define HDR_H		24			/* en-tete des colonnes (vue liste) */
#define CELL_W		96			/* case de la vue icones */
#define CELL_H		88
#define ICON_S		48
#define COL_SIZE_W	90
#define COL_TYPE_W	90
#define COL_MIN		50			/* Taille, Type : largeur minimale */
#define NAME_MIN	120			/* Nom : largeur minimale */
#define SEP_HIT		6			/* prise d'un separateur (+-) */
#define S_NAME		0			/* tri : colonne */
#define S_SIZE		1
#define S_TYPE		2

/* les dossiers dessines ont leur couleur propre (comme l'asm : une icone, pas un element de l'interface) */

struct ent
{
	char name[256];
	char icon[32];				/* paquet : nom sans .app */
	char label[64];				/* paquet : nom affiche */
	unsigned long size;
	int kind;
	char type[16];				/* « Dossier », « TXT »... */
};

struct vol
{
	char disp[64];				/* le point de montage : « /media/usb0 », « / » */
	char low[16];				/* « usb0 » */
	char src[24];				/* « blk.usb0p1 » */
	int rem;				/* 1 : amovible */
};

struct place
{
	char lab[48];
	char path[80];
};

struct crumb
{
	const char *txt;			/* nom (dans crbuf) */
	long end;				/* sa fin dans path */
	long x;					/* x affiche (-1 : cache) */
	long w;					/* largeur affichee */
};

static long nents, cursel = -1, scroll_, rows = 1, cols = 1, body_y = LIST_Y + HDR_H, line_h = ROW_H;
static long view;				/* 0 icones, 1 liste */
static long sort_key = S_NAME, sort_desc;
static long w_size = COL_SIZE_W, w_type = COL_TYPE_W;	/* largeurs des colonnes (vue liste) */
static long drag;				/* separateur saisi (1, 2) */
static int band_on;				/* bande de progression montree (copie en arriere-plan) */
static long press_i = -1, press_x, press_y;	/* element appuye (pour le glisser) */
static int press_keep;				/* appui dans une selection multiple : relache sans glisser = elle seule */
static long hover;				/* separateur survole */
static long hhot;				/* element de l'en-tete survole */
static long cr_n;				/* segments du fil d'Ariane */
static long h_pos = -1, h_n;			/* historique */
static long last_idx = -1, last_time, last_poll;
static long vol_sub;				/* abonnement aux volumes (0 : pas de bus) */
static const char *msg;				/* message d'etat (0 : aucun) */
static long vfs_ch = -1;
static long vraw_n = -1, vol_n, pl_n;
static int show_hidden;			/* Affichage > Fichiers caches (« . » au debut, attribut cache de FAT) */
static int show_allvol;			/* Affichage > Tous les volumes (sinon : amovibles et distants) */
static long scr[4];
static char errbuf[32];
static struct svc_msg vmsg;
static unsigned char vraw[VMAX * VE_SZ];
static char vpts[464];				/* VFS_MOUNTS : nom, 0, point, 0... */
static long vpts_n;
static struct vol vols[VMAX];
static struct place places[PL_MAX];
static char pl_buf[PL_BUF];
static char menu_buf[WS_MENU_MAX + 16];
static char path[272], fullpath[600], tmpname[272], crbuf[272], stbuf[400], popbuf[400], cellbuf[272];

/* Recents (24/11/2026, comme GNOME) : un dossier virtuel, « recent: », les fichiers ouverts (HOME:/etc/recent-files.txt,
 * tenu par UIKit : ui_assoc_open, ui_file_open, ui_file_save), le plus recent d'abord ; le nom d'un element y est
 * son chemin complet (ent_shown n'en montre que le nom, make_full le rend tel quel) */
#define RECENT		"recent:"
#define RECENT_FILE	"recent-files.txt"
static int is_recent_p (const char *p) { return !strcmp (p, RECENT); }
static int is_recent (void) { return is_recent_p (path); }
static void recent_forget (void);
static struct crumb crumbs[CR_MAX];
static char (*hist)[HIST_SZ];			/* HIST_MAX dossiers (ceux de la vue en cours) */
static long *order;				/* MAXE rangs -> entrees */
static struct ent *ents;			/* MAXE entrees */
static unsigned char *selm;			/* MAXE : 1 si l'entree (par numero d'entree) est choisie */
static long nsel;				/* entrees choisies */
static long anchor = -1;			/* rang de depart d'une plage (Maj+clic) */

/* ---- deux vues (docs/maquettes/fichiers-deux-vues.html, variante B) ----
 * Tout ce qui tient a un dossier (chemin, entrees, selection, defilement, presentation, tri, colonnes,
 * historique, lecture en cours) est dans les variables globales pour la vue en cours (curp) ; l'autre vue
 * garde les siens dans panes[]. pane_use echange. L'en-tete, le panneau de gauche et la ligne d'etat
 * suivent la vue active (act). Une seule vue : la vue active seule, sur toute la largeur. */
#define NPANE		2
#define BAN_H		26			/* bandeau d'une vue (deux vues) */
#define SPL_W		5			/* separation des vues (saisie a la souris) */
#define PANE_MIN	240			/* largeur minimale d'une vue */
#define H_DUAL		6			/* bouton Deux vues de l'en-tete */
#define DUAL_X_R	(SEG_X_R + 40)		/* son x, depuis la droite */

struct pane
{
	char path[272];
	struct ent *ents;
	long *order;
	unsigned char *selm;
	char (*hist)[HIST_SZ];
	long nents, cursel, scroll_, rows, cols, body_y, line_h, view, sort_key, sort_desc, w_size, w_type;
	long h_pos, h_n, last_idx, nsel, anchor;
	const char *msg;
	int ls_loading;
	char ls_sel[256];
};
static struct pane panes[NPANE];
static int dual;				/* deux vues montrees */
static int act;					/* vue active */
static int curp;				/* vue dans les variables globales */
static long split_pm = 500;			/* place de la separation (pour mille de la largeur des vues) */
static long vx0 = PAN_W, vx1 = WIN_W;		/* la vue en cours : de vx0 a vx1 */
static long vtop = LIST_Y;			/* haut de son contenu (sous son bandeau en deux vues) */
static int pane_dim;				/* 1 : la vue dessinee n'est pas l'active (selection « en veille ») */
static int other_stale;				/* l'autre vue est a relire */
static long split_drag;				/* separation saisie */
static int act_pane (void) { return act; }	/* (ou un nom local « act » le cache) */
static const unsigned *tk;			/* jetons du theme (pendant draw) */
static long W_, H_;				/* taille de la fenetre (pendant draw) */

#define TOK(t)	(tk[(t) / 4])

static int cp_stop_for_quit (void);
static void quit (void)
{
	if (cp_stop_for_quit ())
		exit (0);
}

/* ---------------------------------------------------------------------------
 *  Outils
 * --------------------------------------------------------------------------- */

static void str_copy_n (char *d, const char *s, long size)
{
	size--;
	while (size && *s)
	{
		*d++ = *s++;
		size--;
	}
	*d = 0;
}

/* comme str_cat d'AppKit : copie, -> la fin */
static char *str_cat (char *d, const char *s)
{
	while ((*d = *s++))
		d++;
	return d;
}

static char *path_join (char *d, const char *dir, const char *name)
{
	char *e = str_cat (d, dir);
	if (e[-1] != '/')
		*e++ = '/';
	return str_cat (e, name);
}

static void path_parent (char *p)
{
	char *last = p;
	for (char *q = p; *q; q++)
		if (*q == '/')
			last = q;
	if (last == p && *p != '/')
		return;				/* « HOME: » : une racine, rien au-dessus */
	if (last == p)				/* « /a » -> « / » ; « / » reste */
		last++;
	*last = 0;
	if (last > p && last[-1] == ':')
	{				/* « HOME:/a » -> « HOME:/ » (la racine du volume, avec sa barre) */
		last[0] = '/';
		last[1] = 0;
	}
}

/* p est-il la racine d'un volume nomme (« HOME: », « HOME:/ ») ? */
static int vol_root (const char *p)
{
	const char *c = strchr (p, ':'), *s = strchr (p, '/');
	return c && (!s || s > c) && (!c[1] || (c[1] == '/' && !c[2]));
}

static char *fmt_dec (unsigned long v, char *d)
{
	char t[24];
	int n = 0;
	do
		t[n++] = (char) ('0' + v % 10);
	while ((v /= 10));
	while (n)
		*d++ = t[--n];
	*d = 0;
	return d;
}

static unsigned long parse_dec (const char *s)
{
	unsigned long v = 0;
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (unsigned long) (*s++ - '0');
	return v;
}

static int str_ends (const char *s, const char *suf)
{
	size_t a = strlen (s), b = strlen (suf);
	if (b > a)
		return 0;
	for (size_t i = 0; i < b; i++)
	{
		char x = s[a - b + i], y = suf[i];
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y)
			return 0;
	}
	return 1;
}

/* casse ASCII ignoree */
static int str_icmp (const char *a, const char *b)
{
	for (;;)
	{
		int x = (unsigned char) *a++, y = (unsigned char) *b++;
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y)
			return x - y;
		if (!x)
			return 0;
	}
}

static int str_pre (const char *s, const char *p)
{
	return strncmp (s, p, strlen (p)) == 0;
}

/* sans espaces ni tabulations devant ; derriere, sans rien qui soit <= ' ' */
static char *ini_trim (char *s)
{
	while (*s == ' ' || *s == 9)
		s++;
	char *e = s + strlen (s);
	while (e > s && (unsigned char) e[-1] <= ' ')
		*--e = 0;
	return s;
}

/* comme ini_read d'applist.inc : « cle = valeur » (cle en minuscules), « # » commente */
static void ini_read (const char *p, void (*fn) (char *, char *))
{
	static char buf[4096];
	int fd = open (p, O_RDONLY);
	if (fd < 0)
		return;
	long n = read (fd, buf, sizeof buf - 1);
	close (fd);
	if (n < 0)
		n = 0;
	buf[n] = 0;
	char *line = buf;
	while (*line)
	{
		char *e = line;
		while (*e && *e != 10)
		{
			if (*e == '#')
				*e = 0;
			e++;
		}
		char end = *e;
		*e = 0;
		char *q = line;
		while (*q && *q != '=')
			q++;
		if (*q == '=')
		{
			*q = 0;
			char *k = ini_trim (line);
			for (char *c = k; *c; c++)
				if ((unsigned char) (*c - 'A') <= 25)
					*c += 32;
			fn (k, ini_trim (q + 1));
		}
		if (!end)
			break;
		line = e + 1;
	}
}

/* str_cat traduit : les espaces autour du morceau restent ici (un dictionnaire ne garde pas ceux
 * d'une cle) -> la fin */
static char *tr_cat (char *d, const char *s)
{
	char t[240];
	while (*s == ' ')
		*d++ = *s++;
	long n = 0, k = 0;
	while (s[k] && k < 239)
	{
		t[k] = s[k];
		k++;
		if (s[k - 1] != ' ')
			n = k;
	}
	t[n] = 0;
	d = str_cat (d, TR (t));
	return str_cat (d, s + n);
}

/* une erreur de la libc (errno) -> le code de Korin (numeros de Linux) */
static long kerr (int e)
{
	if (e == ENOTEMPTY)
		return -39;
	return -(long) (e > 0 ? e : EIO);
}

/* code d'erreur -> message d'etat */
static void set_err (long code)
{
	const char *s;
	if (code == -39)
		s = "Le dossier n'est pas vide";
	else if (code == -2)
		s = "Introuvable";
	else
	{
		fmt_dec ((unsigned long) -code, tr_cat (errbuf, "Erreur -"));
		s = errbuf;
	}
	msg = TR (s);
}

static void set_msg (const char *s) { msg = TR (s); }

/* octets : « 123 o », « 12 Ko », « 3 Mo » -> la fin */
static char *fmt_size (unsigned long v, char *d)
{
	const char *u = " o";
	if (v >= 10240)
	{
		v >>= 10;
		u = " Ko";
		if (v >= 10240)
		{
			v >>= 10;
			u = " Mo";
		}
	}
	return tr_cat (fmt_dec (v, d), u);
}

static struct ent *ent_ptr (long i) { return &ents[order[i]]; }

static const char *ent_shown (struct ent *e)
{
	if (e->kind == K_APP)
		return e->label;
	if (is_recent ())
	{					/* (Recents : le nom seul) */
		const char *b = strrchr (e->name, '/');
		return b && b[1] ? b + 1 : e->name;
	}
	return e->name;
}

/* rang affiche -> chemin complet dans fullpath -> sa fin */
static char *make_full (long i)
{
	if (is_recent ())
		return str_cat (fullpath, ent_ptr (i)->name);	/* (Recents : deja complet) */
	return path_join (fullpath, path, ent_ptr (i)->name);
}

static void set_scroll (long v)
{
	long m = (nents + cols - 1) / cols - rows;
	if (m < 0)
		m = 0;
	if (v > m)
		v = m;
	if (v < 0)
		v = 0;
	scroll_ = v;
}

/* la selection reste visible */
static void keep_visible (void)
{
	if (cursel < 0)
		return;
	long l = cursel / cols;
	if (l < scroll_)
		scroll_ = l;
	else if (l >= scroll_ + rows)
		scroll_ = l - rows + 1;
}

/* ---------------------------------------------------------------------------
 *  Lecture du dossier (dossiers d'abord, puis par nom)
 * --------------------------------------------------------------------------- */

static void hist_note (void);

static struct ent *cur_ent;

/* (ini_read) « name » -> nom affiche du paquet */
static void man_line (char *k, char *v)
{
	if (!strcmp (k, "name"))
		str_copy_n (cur_ent->label, v, 64);
}

/* entree d'un paquet : icone (nom sans .app) et nom affiche (« name = » de son app.txt) */
static void app_info (struct ent *e, const char *dir)
{
	char p[600];
	long n = (long) strlen (e->name) - 4;
	if (n > 31)
		n = 31;
	memcpy (e->icon, e->name, (size_t) n);
	e->icon[n] = 0;
	str_copy_n (e->label, e->icon, 64);
	if (strlen (dir) + strlen (e->name) + 10 > sizeof p)
		return;
	str_cat (path_join (p, dir, e->name), "/app.txt");
	cur_ent = e;
	ini_read (p, man_line);			/* (tampon et cur_ent communs : sous ls_mx) */
}

/* son type affiche */
static void set_type (struct ent *e)
{
	const char *s = "Dossier";
	if (e->kind == K_APP)
		s = "Application";
	else if (e->kind == K_FILE)
	{
		const char *x = 0;			/* apres le dernier « . » (pas en tete) */
		for (const char *p = e->name + 1; *p; p++)
			if (*p == '.')
				x = p + 1;
		s = "Fichier";
		long n = x ? (long) strlen (x) : 0;
		if (n >= 1 && n <= 8)
		{
			long i;
			for (i = 0; x[i]; i++)
				e->type[i] = (char) (x[i] >= 'a' && x[i] <= 'z' ? x[i] - 32 : x[i]);
			e->type[i] = 0;
			return;
		}
	}
	str_copy_n (e->type, TR (s), 16);
}

/* numeros d'entrees -> < 0 si a passe avant (sort_key, sort_desc ; dossiers d'abord, sauf tri par type) */
static long ent_cmp (long ia, long ib)
{
	struct ent *a = &ents[ia], *b = &ents[ib];
	long r;
	if (sort_key == S_TYPE)
	{
		r = str_icmp (a->type, b->type);
		if (!r)
			r = str_icmp (a->name, b->name);
	}
	else
	{
		int da = a->kind != K_FILE, db = b->kind != K_FILE;
		if (da != db)
			return da ? -1 : 1;
		if (sort_key == S_SIZE && a->size != b->size)
			r = a->size < b->size ? -1 : 1;
		else
			r = str_icmp (a->name, b->name);
	}
	return sort_desc ? -r : r;
}

/* tri par insertion de order[nents] */
static void sort_ents (void)
{
	if (is_recent ())
	{					/* (Recents : le plus recent d'abord, toujours) */
		for (long i = 0; i < nents; i++)
			order[i] = i;
		return;
	}
	for (long i = 1; i < nents; i++)
	{
		long v = order[i], j = i;
		while (j > 0 && ent_cmp (order[j - 1], v) > 0)
		{
			order[j] = order[j - 1];
			j--;
		}
		order[j] = v;
	}
}

/* ---- lire un dossier : dans un thread ----
 *
 * Un dossier se lit par une requete par element (readdir) et un stat par fichier (sa taille) : sur un
 * volume lent (Google Drive, qui sert une requete a la fois : pendant une copie, chacune attend le
 * bloc en cours), la fenetre attendrait. load_dir lance donc la lecture dans un thread (libc seule)
 * et l'attend 150 ms au plus ; au-dela, la fenetre reste utilisable (« Chargement… ») et la boucle
 * prend le resultat quand il arrive (ls_poll). Une lecture depassee (on est alle ailleurs) s'arrete
 * et son resultat est jete. select_name pendant la lecture : le nom est choisi a son arrivee. */

#define LS_WAIT_MS	150

struct listing
{
	long gen;				/* la lecture (ls_gen[pane] quand elle a ete demandee) */
	int pane;				/* la vue qui l'attend */
	char dir[272];
	struct ent *e;				/* MAXE entrees */
	long n;
	long err;				/* code de Korin, 0 : lu */
};

static pthread_mutex_t ls_mx = PTHREAD_MUTEX_INITIALIZER;	/* ls_ready ; app_info (tampon d'ini_read, cur_ent) */
static volatile long ls_gen[NPANE];		/* la lecture voulue, par vue */
static struct listing *ls_ready[NPANE];		/* finie, a prendre (ls_mx) */
static int ls_loading;				/* une lecture est attendue (vue en cours) */
static char ls_sel[256];			/* le nom a choisir a son arrivee */
static void select_name (const char *name);

/* ---- deux vues : echange des variables globales ---- */

static long split_x (void)
{
	long w = ui.w - PAN_W, x = PAN_W + w * split_pm / 1000;
	if (w < 2 * PANE_MIN)
		return PAN_W + w / 2;
	if (x < PAN_W + PANE_MIN)
		x = PAN_W + PANE_MIN;
	if (x > ui.w - PANE_MIN)
		x = ui.w - PANE_MIN;
	return x;
}

/* vx0, vx1, vtop de la vue en cours */
static void pane_rect (void)
{
	vtop = dual ? LIST_Y + BAN_H : LIST_Y;
	if (!dual)
	{
		vx0 = PAN_W;
		vx1 = ui.w;
		return;
	}
	long sx = split_x ();
	if (curp == 0)
	{
		vx0 = PAN_W;
		vx1 = sx - SPL_W / 2;
	}
	else
	{
		vx0 = sx + SPL_W - SPL_W / 2;
		vx1 = ui.w;
	}
}

/* les variables globales -> panes[p] */
static void pane_save (int p)
{
	struct pane *P = &panes[p];
	str_copy_n (P->path, path, sizeof P->path);
	P->ents = ents;
	P->order = order;
	P->selm = selm;
	P->hist = hist;
	P->nents = nents;
	P->cursel = cursel;
	P->scroll_ = scroll_;
	P->rows = rows;
	P->cols = cols;
	P->body_y = body_y;
	P->line_h = line_h;
	P->view = view;
	P->sort_key = sort_key;
	P->sort_desc = sort_desc;
	P->w_size = w_size;
	P->w_type = w_type;
	P->h_pos = h_pos;
	P->h_n = h_n;
	P->last_idx = last_idx;
	P->nsel = nsel;
	P->anchor = anchor;
	P->msg = msg;
	P->ls_loading = ls_loading;
	memcpy (P->ls_sel, ls_sel, sizeof ls_sel);
}

/* panes[p] -> les variables globales */
static void pane_load (int p)
{
	struct pane *P = &panes[p];
	str_copy_n (path, P->path, sizeof path);
	ents = P->ents;
	order = P->order;
	selm = P->selm;
	hist = P->hist;
	nents = P->nents;
	cursel = P->cursel;
	scroll_ = P->scroll_;
	rows = P->rows;
	cols = P->cols;
	body_y = P->body_y;
	line_h = P->line_h;
	view = P->view;
	sort_key = P->sort_key;
	sort_desc = P->sort_desc;
	w_size = P->w_size;
	w_type = P->w_type;
	h_pos = P->h_pos;
	h_n = P->h_n;
	last_idx = P->last_idx;
	nsel = P->nsel;
	anchor = P->anchor;
	msg = P->msg;
	ls_loading = P->ls_loading;
	memcpy (ls_sel, P->ls_sel, sizeof ls_sel);
	curp = p;
	pane_rect ();
}

/* la vue p dans les variables globales */
static void pane_use (int p)
{
	if (p == curp)
	{
		pane_rect ();
		return;
	}
	pane_save (curp);
	pane_load (p);
}

/* ---- la selection (par numero d'entree : elle suit un nouveau tri) ---- */

static int is_sel (long i) { return i >= 0 && i < nents && selm[order[i]]; }

static void sel_clear (void)
{
	memset (selm, 0, MAXE);
	nsel = 0;
}

static void sel_put (long i, int on)
{
	if (i < 0 || i >= nents || !selm[order[i]] == !on)
		return;
	selm[order[i]] = (unsigned char) (on != 0);
	nsel += on ? 1 : -1;
}

/* rang i seul choisi (-1 : rien) */
static void sel_one (long i)
{
	sel_clear ();
	cursel = i;
	anchor = i;
	sel_put (i, 1);
}

/* de anchor a i (Maj) */
static void sel_range (long i)
{
	long a = anchor >= 0 && anchor < nents ? anchor : i, lo = a < i ? a : i, hi = a < i ? i : a;
	sel_clear ();
	for (long k = lo; k <= hi; k++)
		sel_put (k, 1);
	cursel = i;
}

static void *ls_thread (void *a)
{
	struct listing *L = (struct listing *) a;
	L->e = (struct ent *) calloc (MAXE, sizeof *L->e);
	DIR *d = 0;
	if (!L->e)
		L->err = -ENOMEM;
	else if (is_recent_p (L->dir))
	{					/* Recents : la liste de UIKit, les fichiers encore la */
		char cp_[160], *buf = (char *) malloc (16384);
		int fd = buf ? open (korin_conf_path (RECENT_FILE, cp_, sizeof cp_, 0), O_RDONLY) : -1;
		long n = fd >= 0 ? read (fd, buf, 16383) : 0;
		if (fd >= 0)
			close (fd);
		if (n < 0)
			n = 0;
		if (buf)
			buf[n] = 0;
		for (char *l = buf; l && *l && L->n < MAXE;)
		{
			char *e = strchr (l, 10);
			if (e)
				*e = 0;
			struct stat st;
			if (*l && strlen (l) < 256 && stat (l, &st) == 0 && !S_ISDIR (st.st_mode))
			{
				struct ent *en = &L->e[L->n++];
				str_copy_n (en->name, l, 256);
				en->kind = K_FILE;
				en->size = (unsigned long) st.st_size;
			}
			if (!e)
				break;
			l = e + 1;
		}
		free (buf);
	}
	else if (!(d = opendir (L->dir)))
		L->err = kerr (errno);
	if (d)
	{
		char fp[600];
		struct dirent *de;
		while (L->n < MAXE && L->gen == ls_gen[L->pane] && (de = readdir (d)))
		{
			if (!de->d_name[0])
				continue;
			if (!show_hidden && (de->d_name[0] == '.' || (korin_readdir_attr (d) & KORIN_ATTR_HIDDEN)))
				continue;		/* caches : « .xxx », attribut cache de FAT (Affichage > Fichiers caches) */
			struct ent *e = &L->e[L->n];
			str_copy_n (e->name, de->d_name, 256);
			e->size = 0;
			e->kind = K_FILE;
			if (de->d_type == DT_DIR)
			{
				e->kind = K_DIR;
				if (str_ends (e->name, ".app"))
				{
					pthread_mutex_lock (&ls_mx);
					app_info (e, L->dir);
					pthread_mutex_unlock (&ls_mx);
					e->kind = K_APP;
				}
			}
			else if (strlen (L->dir) + strlen (e->name) + 2 <= sizeof fp)
			{
				struct stat st;		/* (la taille : readdir de la libc ne la donne pas) */
				path_join (fp, L->dir, e->name);
				if (stat (fp, &st) == 0)
					e->size = (unsigned long) st.st_size;
			}
			L->n++;
		}
		closedir (d);
	}
	pthread_mutex_lock (&ls_mx);
	if (L->gen == ls_gen[L->pane])
	{					/* (sinon : depassee, jetee) */
		struct listing *o = ls_ready[L->pane];
		ls_ready[L->pane] = L;
		L = o;
	}
	pthread_mutex_unlock (&ls_mx);
	if (L)
	{
		free (L->e);
		free (L);
	}
	return 0;
}

/* le resultat arrive : il remplace la liste -> 1 s'il y en avait un */
static int ls_take (void)
{
	pthread_mutex_lock (&ls_mx);
	struct listing *L = ls_ready[curp];
	ls_ready[curp] = 0;
	pthread_mutex_unlock (&ls_mx);
	if (!L)
		return 0;
	if (L->gen != ls_gen[curp] || strcmp (L->dir, path))
	{					/* (depassee entre-temps) */
		free (L->e);
		free (L);
		return 1;
	}
	ls_loading = 0;
	if (L->err)
	{
		free (L->e);
		set_err (L->err);
	}
	else
	{
		free (ents);
		ents = L->e;
		nents = L->n;
		sel_clear ();
		for (long i = 0; i < nents; i++)
		{
			set_type (&ents[i]);
			order[i] = i;
		}
		sort_ents ();
		if (is_recent () && !nents)
			set_msg ("Aucun fichier récent : ceux que vous ouvrez viendront ici");
	}
	free (L);
	if (ls_sel[0])
	{
		select_name (ls_sel);
		ls_sel[0] = 0;
	}
	return 1;
}

static void load_dir (void)
{
	hist_note ();
	nents = 0;
	scroll_ = 0;
	cursel = -1;
	anchor = -1;
	sel_clear ();
	last_idx = -1;
	msg = 0;
	ls_sel[0] = 0;
	struct listing *L = (struct listing *) calloc (1, sizeof *L);
	if (!L)
	{
		set_err (-ENOMEM);
		return;
	}
	L->gen = ++ls_gen[curp];
	L->pane = curp;
	str_copy_n (L->dir, path, sizeof L->dir);
	ls_loading = 1;
	pthread_t th;
	pthread_attr_t at;
	pthread_attr_init (&at);
	pthread_attr_setstacksize (&at, 128 * 1024);
	pthread_attr_setdetachstate (&at, PTHREAD_CREATE_DETACHED);
	int bad = pthread_create (&th, &at, ls_thread, L);
	pthread_attr_destroy (&at);
	if (bad)
	{					/* (sans thread : ici) */
		ls_thread (L);
		ls_take ();
		return;
	}
	long t0 = time_ms ();			/* un dossier rapide : comme avant, sans attente visible */
	while (time_ms () - t0 < LS_WAIT_MS)
	{
		ls_take ();
		if (!ls_loading)
			break;
		sys3 (SYS_SLEEP_, 5, 0);
	}
}

/* la boucle : une lecture arrivee -> 1 (a redessiner) */
static int ls_poll (void)
{
	int r = ls_loading && ls_take ();
	int me = curp, o = !curp;
	if (dual && panes[o].ls_loading)
	{					/* (l'autre vue attend aussi le sien) */
		pane_use (o);
		if (ls_take ())
		{
			keep_visible ();
			r = 1;
		}
		pane_use (me);
	}
	return r;
}

/* retrier en gardant la selection */
static void resort (void)
{
	long keep = cursel >= 0 ? order[cursel] : -1;
	sort_ents ();
	if (keep < 0)
		return;
	for (long i = 0; i < nents; i++)
		if (order[i] == keep)
		{
			cursel = i;
			keep_visible ();
			return;
		}
}

/* x de la colonne Taille (*sx), de la colonne Type (*tx) */
static void col_x (long *sx, long *tx)
{
	*tx = vx1 - w_type - 6;
	*sx = *tx - w_size;
}

/* x du clic dans l'en-tete : tri par cette colonne (deuxieme clic : ordre inverse) */
static void header_click (long x)
{
	long sx, tx, k;
	col_x (&sx, &tx);
	k = x < sx ? S_NAME : x < tx ? S_SIZE : S_TYPE;
	if (k == sort_key)
		sort_desc ^= 1;
	else
	{
		sort_key = k;
		sort_desc = 0;
	}
	resort ();
}

/* largeurs lues : bornees (sinon celles par defaut) */
static void cols_fix (void)
{
	if (w_size < COL_MIN || w_type < COL_MIN || vx1 - w_size - w_type - (6 + vx0 + NAME_MIN) < 0)
	{
		w_size = COL_SIZE_W;
		w_type = COL_TYPE_W;
	}
}

/* separateur sous la souris (1 avant Taille, 2 avant Type, 0 aucun ; vue liste, en-tete) */
static long sep_at (long x, long y)
{
	long sx, tx;
	if (!view || x < vx0 || x >= vx1 || y < vtop || y >= vtop + HDR_H)
		return 0;
	col_x (&sx, &tx);
	if (labs (x - sx) <= SEP_HIT)
		return 1;
	if (labs (x - tx) <= SEP_HIT)
		return 2;
	return 0;
}

static long clamp (long v, long lo, long hi)
{
	if (v > hi)
		v = hi;
	if (v < lo)
		v = lo;
	return v;
}

/* x de la souris : deplace le separateur saisi -> 1 si les largeurs ont change */
static long drag_to (long x)
{
	long sx, tx;
	col_x (&sx, &tx);
	if (drag == 1)
	{					/* avant Taille : Type ne bouge pas */
		x = clamp (x, vx0 + NAME_MIN, tx - COL_MIN);
		long old = w_size;
		w_size = tx - x;
		return old != w_size;
	}
	x = clamp (x, sx + COL_MIN, vx1 - 6 - COL_MIN);	/* avant Type : Taille ne bouge pas a gauche */
	long old = w_type;
	w_size = x - sx;
	w_type = vx1 - 6 - x;
	return old != w_type;
}

/* files.ini (HOME:/etc, sinon /etc) : la presentation et les colonnes de chaque vue (« vue », « taille »,
 * « type » ; « vue2 »... pour la seconde), les deux vues (« deux_vues », « active », « partage », les dossiers
 * « dossier » et « dossier2 ») */
static void save_cfg (void)
{
	char b[1400];
	pane_save (curp);
	const struct pane *a = &panes[0], *o = &panes[1];
	int n = snprintf (b, sizeof b, "vue = %s\ntaille = %ld\ntype = %ld\nvue2 = %s\ntaille2 = %ld\ntype2 = %ld\n"
		"deux_vues = %d\nactive = %d\npartage = %ld\ndossier = %s\ndossier2 = %s\ncaches = %d\nvolumes = %d\n",
		a->view ? "liste" : "icones", a->w_size, a->w_type, o->view ? "liste" : "icones", o->w_size, o->w_type,
		dual, act, split_pm, a->path, o->path, show_hidden, show_allvol);
	if (n <= 0 || n >= (int) sizeof b)
		return;
	char cp_[160];
	int fd = open (korin_conf_path ("files.ini", cp_, sizeof cp_, 1), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	write (fd, b, (size_t) n);
	close (fd);
}

/* x de la premiere colonne de la grille (centree) */
static long grid_x0 (void) { return ((vx1 - vx0 - cols * CELL_W) >> 1) + vx0; }

/* disposition de la vue : cols, body_y, line_h, rows */
static void layout (void)
{
	if (!view)
	{
		cols = (long) ((unsigned long) (vx1 - (vx0 + 8)) / CELL_W);
		if (cols < 1)
			cols = 1;
		body_y = vtop + 4;
		line_h = CELL_H;
	}
	else
	{
		cols = 1;
		body_y = vtop + HDR_H;
		line_h = ROW_H;
	}
	rows = (long) ((unsigned long) (ui.h - SB_H - body_y) / (unsigned long) line_h);
	if (rows < 1)
		rows = 1;
}

/* vue (0 icones, 1 liste) : la prend et l'enregistre */
static void set_view (long v)
{
	view = v;
	layout ();
	scroll_ = 0;
	keep_visible ();
	save_cfg ();
}

/* (ini_read de files.ini) ce que save_cfg ecrit ; la vue 0 est dans les variables globales */
static char cfg_dir[2][272];
static void cfg_line (char *k, char *v)
{
	struct pane *o = &panes[1];
	if (!strcmp (k, "vue"))
		view = !strcmp (v, "liste");
	else if (!strcmp (k, "taille"))
		w_size = (long) parse_dec (v);
	else if (!strcmp (k, "type"))
		w_type = (long) parse_dec (v);
	else if (!strcmp (k, "vue2"))
		o->view = !strcmp (v, "liste");
	else if (!strcmp (k, "taille2"))
		o->w_size = (long) parse_dec (v);
	else if (!strcmp (k, "type2"))
		o->w_type = (long) parse_dec (v);
	else if (!strcmp (k, "deux_vues"))
		dual = *v == '1';
	else if (!strcmp (k, "active"))
		act = *v == '1';
	else if (!strcmp (k, "partage"))
		split_pm = clamp ((long) parse_dec (v), 100, 900);
	else if (!strcmp (k, "dossier"))
		str_copy_n (cfg_dir[0], v, sizeof cfg_dir[0]);
	else if (!strcmp (k, "dossier2"))
		str_copy_n (cfg_dir[1], v, sizeof cfg_dir[1]);
	else if (!strcmp (k, "caches"))
		show_hidden = *v == '1';
	else if (!strcmp (k, "volumes"))
		show_allvol = *v == '1';
}

/* nom : le selectionne s'il est dans la liste */
static void select_name (const char *name)
{
	if (ls_loading)
	{					/* (la lecture du dossier n'est pas arrivee : a son arrivee) */
		if (name != ls_sel)
			str_copy_n (ls_sel, name, sizeof ls_sel);
		return;
	}
	for (long i = 0; i < nents; i++)
		if (!strcmp (ent_ptr (i)->name, name))
		{
			sel_one (i);
			keep_visible ();
			return;
		}
}

static void go_dir (const char *p)
{
	char t[272];
	str_copy_n (t, p, 256);			/* (p peut etre un historique, ou fullpath) */
	str_copy_n (path, t, 256);
	load_dir ();
}

static void go_sd0 (void) { go_dir ("/"); }

/* ---- deux vues : disposition, vue active, l'autre relue ---- */

static void refresh (void);

/* chaque vue montree : colonnes, rangees (apres un changement de taille, de mode, de separation) */
static void relayout_all (void)
{
	int me = curp;
	for (int p = 0; p < NPANE; p++)
	{
		if (!dual && p != act)
			continue;
		pane_use (p);
		cols_fix ();
		layout ();
		keep_visible ();
	}
	pane_use (me);
}

/* la vue p devient la vue active (l'en-tete, le panneau et la ligne d'etat la suivent) */
static void pane_activate (int p)
{
	if (!dual || p == act)
		return;
	act = p;
	pane_use (p);
	press_i = -1;
	hhot = hover = 0;
}

/* l'autre vue : la relire (meme dossier change, copie arrivee) */
static void other_refresh (void)
{
	other_stale = 0;
	if (!dual)
		return;
	int me = curp;
	pane_use (!act);
	refresh ();
	pane_use (me);
}

/* une vue <-> deux vues ; en revenant a une vue, la vue active reste */
static void dual_set (int on)
{
	on = on != 0;
	if (on == dual)
		return;
	pane_use (act);
	dual = on;
	if (dual)
	{					/* la seconde vue : son dossier d'avant, sinon celui-ci */
		struct pane *o = &panes[!act];
		if (!o->path[0])
			str_copy_n (o->path, path, sizeof o->path);
		int me = curp;
		pane_use (!act);
		if (!h_n || !nents)
			load_dir ();
		pane_use (me);
	}
	relayout_all ();
	save_cfg ();
}

/* ---------------------------------------------------------------------------
 *  Historique (hist[0..h_n[, h_pos = dossier courant) : load_dir y note tout changement de
 *  dossier, hist_go ne fait que deplacer h_pos
 * --------------------------------------------------------------------------- */

/* path a change ? il devient la derniere entree (la suite est perdue ; pleine, la plus ancienne sort) */
static void hist_note (void)
{
	if (h_pos >= 0 && !strcmp (hist[h_pos], path))
		return;
	long p = h_pos + 1;
	if (p >= HIST_MAX)
	{
		p = HIST_MAX - 1;
		memmove (hist[0], hist[1], (HIST_MAX - 1) * HIST_SZ);
	}
	h_pos = p;
	h_n = p + 1;
	str_copy_n (hist[p], path, HIST_SZ);
}

/* -1 precedent, +1 suivant */
static void hist_go (long d)
{
	long p = h_pos + d;
	if (p < 0 || p >= h_n)
		return;
	h_pos = p;
	str_copy_n (path, hist[p], 256);
	load_dir ();
}

/* ---------------------------------------------------------------------------
 *  Acces rapides (/etc/places.ini, « libelle = dossier », dans l'ordre ; seulement les
 *  dossiers presents)
 * --------------------------------------------------------------------------- */

/* les dossiers types du dossier personnel (HOME:/<nom>) et leur glyphe (tools/icons/art.py, glyph-place-*) ; ceux
 * qui manquent sont crees au demarrage, comme xdg-user-dirs de GNOME (24/11/2026) */
static const char *const std_dirs[][2] = {
	{ "music", "glyph-place-music" }, { "pictures", "glyph-place-pictures" }, { "downloads", "glyph-place-downloads" },
	{ "projects", "glyph-place-projects" }, { "documents", "glyph-place-documents" }, { "video", "glyph-place-videos" },
	{ "videos", "glyph-place-videos" }, { "public", "glyph-place-public" } };
#define STD_MAKE	6			/* (les 6 premiers sont crees ; « videos » n'est qu'un autre nom ; « public » : /home/public,
					 * le dossier partage entre les comptes, il a son embleme quand HOME: est /home) */

static void std_dirs_make (void)
{
	struct stat st;
	char d[64];
	if (stat ("HOME:/", &st) != 0 || !S_ISDIR (st.st_mode))
		return;
	for (int i = 0; i < STD_MAKE; i++)
	{
		snprintf (d, sizeof d, "HOME:/%s", std_dirs[i][0]);
		if (stat (d, &st) != 0)
			mkdir (d, 0755);
	}
}

/* « HOME: » ou « HOME:/ » (sans casse) -> la longueur du prefixe avec sa barre (5 ou 6), sinon 0 */
static long home_prefix (const char *p)
{
	if (strncasecmp (p, "HOME:", 5))
		return 0;
	return p[5] == '/' ? 6 : 5;
}

/* dossier d'un acces rapide -> le nom de son glyphe : le dossier personnel, /apps, un dossier type, sinon un dossier */
static int same_dir (const char *a, const char *b);

static const char *place_glyph (const char *dir)
{
	long h = home_prefix (dir);
	if (h)
	{
		const char *r = dir + h;
		size_t n = strlen (r);
		if (n && r[n - 1] == '/')
			n--;
		if (!n)
			return "glyph-place-home";
		for (size_t i = 0; i < sizeof std_dirs / sizeof std_dirs[0]; i++)
			if (strlen (std_dirs[i][0]) == n && !strncasecmp (r, std_dirs[i][0], n))
				return std_dirs[i][1];
	}
	if (same_dir (dir, "/apps") || same_dir (dir, "APPS:/"))
		return "glyph-place-apps";
	if (is_recent_p (dir))
		return "glyph-place-recent";
	if (same_dir (dir, "/home/public") || same_dir (dir, "PUBLIC:/"))
		return "glyph-place-public";	/* (le dossier partage entre les comptes) */
	return "glyph-place-folder";
}

/* deux dossiers egaux, sans casse et sans la barre de fin (« HOME: » = « HOME:/ », « /apps/ » = « /apps ») */
static int same_dir (const char *a, const char *b)
{
	size_t la = strlen (a), lb = strlen (b);
	if (la > 1 && a[la - 1] == '/')
		la--;
	if (lb > 1 && b[lb - 1] == '/')
		lb--;
	return la == lb && !strncasecmp (a, b, la);
}

/* nom d'un dossier type du dossier personnel -> son glyphe, sinon 0 */
static const char *std_glyph (const char *name)
{
	for (size_t i = 0; i < sizeof std_dirs / sizeof std_dirs[0]; i++)
		if (!strcasecmp (name, std_dirs[i][0]))
			return std_dirs[i][1];
	return 0;
}

/* le dossier courant est-il le dossier personnel (« HOME:/ », ou son vrai chemin : $HOME) ? */
static int path_is_home (void)
{
	const char *h = getenv ("HOME");
	return same_dir (path, "HOME:/") || same_dir (path, "HOME:") || (h && *h && same_dir (path, h));
}

static void places_load (void)
{
	pl_n = 0;
	char cp_[160];
	int fd = open (korin_conf_path ("places.ini", cp_, sizeof cp_, 0), O_RDONLY);
	if (fd < 0)
		return;
	long n = read (fd, pl_buf, PL_BUF - 1);
	close (fd);
	if (n < 0)
		n = 0;
	pl_buf[n] = 0;
	char *line = pl_buf;
	while (*line)
	{
		char *e = line;			/* fin de ligne ; « # » : commentaire */
		while (*e && *e != 10)
		{
			if (*e == '#')
				*e = 0;
			e++;
		}
		char end = *e;
		*e = 0;
		char *q = line;
		while (*q && *q != '=')
			q++;
		if (*q == '=')
		{
			*q = 0;
			char *lab = ini_trim (line);	/* (casse gardee) */
			char *dir = ini_trim (q + 1);
			struct stat st;
			if (*lab && *dir && (is_recent_p (dir) || (stat (dir, &st) == 0 && S_ISDIR (st.st_mode))))
			{
				if (pl_n >= PL_MAX)
					return;
				str_copy_n (places[pl_n].lab, lab, sizeof places[0].lab);
				str_copy_n (places[pl_n].path, dir, sizeof places[0].path);
				pl_n++;
			}
		}
		if (!end)
			return;
		line = e + 1;
	}
}

/* texte des menus : Fichier, Aller (avec les acces rapides), Affichage, Volume */
static void menu_build (void)
{
	static const char head[] = "MFichier\n"
		"I1\tOuvrir\tEntrée\n"
		"I6\tOuvrir avec…\n"
		"I2\tNouveau dossier\tCtrl+N\n"
		"I7\tRenommer…\tF2\n"
		"I3\tSupprimer\tSuppr\n"
		"-\n"
		"I4\tActualiser\tF5\n"
		"-\n"
		"I5\tFermer\tCtrl+W\n"
		"MÉdition\n"
		"I9\tCouper\tCtrl+X\n"
		"I8\tCopier\tCtrl+C\n"
		"I12\tColler\tCtrl+V\n"
		"-\n"
		"I13\tCopier vers l'autre vue\n"
		"I14\tDéplacer vers l'autre vue\n"
		"MAller\n"
		"I10\tDossier parent\tRetour\n"
		"I15\tAller à l'emplacement…\tCtrl+L\n"
		"-\n";
	static const char tail[] = "-\n"
		"I11\tSD0:\n"
		"MAffichage\n"
		"I40\tIcônes\n"
		"I41\tListe\n"
		"-\n"
		"I42\tDeux vues\n"
		"-\n"
		"I43\tFichiers cachés\tCtrl+H\n"
		"I44\tTous les volumes\n"
		"MVolume\n"
		"I31\tSe connecter à un serveur…\tCtrl+K\n";
	static const char tail2[] = "-\n"
		"I30\tDémonter le volume courant\n";
	char *e = str_cat (menu_buf, head);
	for (long i = 0; i < pl_n; i++)
	{
		e = fmt_dec ((unsigned long) (i + ID_PLACE), str_cat (e, "I"));	/* « I<id>\t<libelle>\n » */
		*e++ = 9;
		e = str_cat (e, places[i].lab);
		*e++ = 10;
		*e = 0;
	}
	e = str_cat (e, pl_n ? tail : tail + 2);	/* (pas d'acces rapide : pas de separateur) */
	if (access (GDRIVEFS, X_OK) == 0)		/* (gdrivefs : construit pour le PC seulement, pour l'instant) */
		e = str_cat (e, "I32\tSe connecter à Google Drive…\n");
	str_cat (e, tail2);
}

/* Deux vues coche ; Copier, Deplacer vers l'autre vue grises en une seule vue */
static void menu_states (void)
{
	ui_menu_state (ID_DUAL, dual ? UMS_CHECKED : 0);
	ui_menu_state (ID_HIDDEN, show_hidden ? UMS_CHECKED : 0);
	ui_menu_state (ID_ALLVOL, show_allvol ? UMS_CHECKED : 0);
	ui_menu_state (ID_COPYTO, dual ? 0 : UMS_DISABLED);
	ui_menu_state (ID_MOVETO, dual ? 0 : UMS_DISABLED);
}

/* ---------------------------------------------------------------------------
 *  Volumes (service vfs : VFS_VOLUMES, VFS_UNMOUNT)
 * --------------------------------------------------------------------------- */

/* nom interne -> son point de montage (VFS_MOUNTS), « /media/<nom> » s'il n'en a pas */
static void vol_point (const char *low, char *out, size_t cap)
{
	const char *e = vpts;
	for (long k = 0; k < vpts_n; k++)
	{
		const char *pt = e + strlen (e) + 1;
		if (!strcmp (e, low) && *pt)
		{
			str_copy_n (out, pt, cap);
			return;
		}
		e = pt + strlen (pt) + 1;
	}
	snprintf (out, cap, "%s%s", strcmp (low, "root") ? "/media/" : "/", strcmp (low, "root") ? low : "");
}

static int is_net_src (const char *s);

/* nom en minuscules, source : ajoute un volume a vols */
static void add_vol (const char *low, const char *src)
{
	if (vol_n >= VMAX)
		return;
	struct vol *v = &vols[vol_n++];
	long i;
	for (i = 0; i < 13 && low[i]; i++)
		v->low[i] = low[i];
	v->low[i] = 0;
	vol_point (v->low, v->disp, sizeof v->disp);
	for (i = 0; i < 23 && src[i]; i++)
		v->src[i] = src[i];
	v->src[i] = 0;
	/* amovible : « blk.usb... », ou un volume du réseau (smbfs : « //hôte/partage », « smb:nom » ; gdrivefs :
	 * « gdrive:nom ») */
	v->rem = str_pre (v->src, "blk.usb") || is_net_src (v->src);
}

/* vraw -> vols : « / » d'abord (toujours present), puis les autres */
static void vol_build (void)
{
	vol_n = 0;
	long i;
	for (i = 0; i < vraw_n; i++)
		if (!strcmp ((char *) vraw + i * VE_SZ, "root"))
			break;
	if (i < vraw_n)
		add_vol ((char *) vraw + i * VE_SZ, (char *) vraw + i * VE_SZ + 16);
	else
		add_vol ("root", "blk.sd0p1");
	for (i = 0; i < vraw_n; i++)
		if (strcmp ((char *) vraw + i * VE_SZ, "root"))
			add_vol ((char *) vraw + i * VE_SZ, (char *) vraw + i * VE_SZ + 16);
}

/* volume -> libelle de son genre */
static const char *vol_kind (struct vol *v)
{
	const char *s = "Volume";
	if (str_pre (v->src, "blk.usb"))
		s = "Clé USB";
	else if (str_pre (v->src, "blk.sd"))
		s = "Carte SD";
	else if (str_pre (v->src, "ram:"))
		s = "RAM";
	else if (str_pre (v->src, "blk.img"))
		s = "Image";
	else if (str_pre (v->src, "//") || str_pre (v->src, "smb:"))
		s = "Serveur";
	else if (str_pre (v->src, "gdrive:"))
		s = "Google Drive";
	return TR (s);
}

/* le nom d'un volume dans le panneau (24/11/2026, comme GNOME) : « Google Drive » (« Google Drive (compte) » s'il y en a
 * plusieurs), « partage sur hote » pour un partage « //hote/partage », sinon son point de montage -> buf */
static int vol_is_root (const struct vol *v);
static const char *vol_title (struct vol *v, char *buf, size_t cap)
{
	if (vol_is_root (v))
	{				/* « / » : le Systeme */
		snprintf (buf, cap, "%s", TR ("Système"));
		return buf;
	}
	if (str_pre (v->src, "gdrive:"))
	{
		long n = 0;
		for (long i = 0; i < vol_n; i++)
			n += str_pre (vols[i].src, "gdrive:");
		if (n > 1)
			snprintf (buf, cap, "Google Drive (%s)", v->src + 7);
		else
			snprintf (buf, cap, "Google Drive");
		return buf;
	}
	if (str_pre (v->src, "//"))
	{				/* « //hote/partage[/dossier] » */
		char host[24], share[24];
		const char *h = v->src + 2, *sl = strchr (h, '/');
		if (sl && sl > h)
		{
			snprintf (host, sizeof host, "%.*s", (int) (sl - h), h);
			const char *e = strchr (sl + 1, '/');
			snprintf (share, sizeof share, "%.*s", (int) (e ? e - sl - 1 : (long) strlen (sl + 1)), sl + 1);
			if (share[0])
			{
				snprintf (buf, cap, TR ("%s sur\u00A0%s"), share, host);	/* (espace insecable : une autre cle que « %s sur %s ») */
				return buf;
			}
		}
	}
	return v->disp;
}

/* -> volume du dossier courant : le point de montage au plus long prefixe (-1 : aucun) */
static long vol_of (const char *p);
static long cur_vol (void) { return vol_of (path); }

/* le volume qui contient le chemin p (le point de montage le plus long), -1 */
static long vol_of (const char *p)
{
	char hp[600];
	long h = home_prefix (p);
	if (h)
	{					/* « HOME:/x » : dans le volume du vrai dossier personnel ($HOME) ; sans cela, un dossier de
						 * HOME: passait pour un volume retire a chaque changement des volumes (retour a /) */
		const char *hm = getenv ("HOME");
		snprintf (hp, sizeof hp, "%s/%s", hm && *hm ? hm : "/home", p + h);
		p = hp;
	}
	else if (!strncasecmp (p, "PUBLIC:", 7) || !strncasecmp (p, "APPS:", 5))
	{					/* les alias fixes de vfs : leur dossier */
		int pub = (p[0] | 0x20) == 'p';
		snprintf (hp, sizeof hp, "%s/%s", pub ? "/home/public" : "/apps", p + (pub ? 7 : 5));
		p = hp;
	}
	long best = -1;
	size_t bl = 0;
	for (long i = 0; i < vol_n; i++)
	{
		const char *d = vols[i].disp;
		size_t l = strlen (d);
		int in = l == 1 && d[0] == '/' ? p[0] == '/'
			: !strncmp (p, d, l) && (p[l] == 0 || p[l] == '/');
		if (in && (best < 0 || l > bl))
		{
			best = i;
			bl = l;
		}
	}
	return best;
}

/* ---------------------------------------------------------------------------
 *  Dossiers changes : apres un deplacement, une copie, une suppression, un renommage, chaque fenetre de
 *  Fichiers publie sur le bus (BUS_TOPIC_CHANGED) « <pid>\n<dossier>\n<dossier>... » ; les autres
 *  fenetres de Fichiers ouvertes sur l'un de ces dossiers le relisent (deposer d'une fenetre dans
 *  l'autre : la premiere voit l'element partir). La derniere valeur seule est lisible : deux
 *  publications tres rapprochees, la premiere peut etre manquee (Actualiser la rattrape).
 * --------------------------------------------------------------------------- */

static char chg_buf[256];			/* la valeur en preparation */
static long chg_len, chg_n, chg_sub;

/* dossier d -> dans la valeur (une fois) */
static void chg_add (const char *d)
{
	if (!d || !*d)
		return;
	if (!chg_len)
		chg_len = snprintf (chg_buf, sizeof chg_buf, "%ld\n", (long) getpid ());
	size_t l = strlen (d);
	for (char *q = strchr (chg_buf, '\n') + 1; *q;)
	{
		char *e = strchr (q, '\n');
		if (!e)
			break;
		if ((size_t) (e - q) == l && !strncasecmp (q, d, l))
			return;			/* deja la */
		q = e + 1;
	}
	if (chg_len + (long) l + 2 > (long) sizeof chg_buf)
		return;				/* (trop : les premiers suffisent) */
	memcpy (chg_buf + chg_len, d, l);
	chg_len += (long) l;
	chg_buf[chg_len++] = '\n';
	chg_buf[chg_len] = 0;
	chg_n++;
}

/* le dossier qui contient le chemin p */
static void chg_add_parent (const char *p)
{
	char d[272];
	str_copy_n (d, p, sizeof d);
	path_parent (d);
	chg_add (d);
}

/* la valeur preparee -> le bus */
static void chg_publish (void)
{
	if (chg_n)
		bus_publish ((long) BUS_TOPIC_CHANGED, (long) chg_buf, chg_len, BUS_RETAIN);
	if (chg_n && dual)
		other_stale = 1;		/* (l'autre vue de cette fenetre : relue par la boucle) */
	chg_len = chg_n = 0;
	chg_buf[0] = 0;
}

/* WEV_BUS de BUS_TOPIC_CHANGED : une autre fenetre a change le dossier ouvert ici -> 1 (relu) */
static void refresh (void);
static int chg_seen (long topic)
{
	char v[260];
	long n = bus_get_id (topic, (long) v, sizeof v - 1);
	if (n <= 0)
		return 0;
	v[n < (long) sizeof v - 1 ? n : (long) sizeof v - 1] = 0;
	char *q = strchr (v, '\n');
	if (!q || strtol (v, 0, 10) == (long) getpid ())
		return 0;			/* (la notre : deja relu) */
	size_t l = strlen (path);
	for (q++; *q;)
	{
		char *e = strchr (q, '\n');
		if (!e)
			break;
		if ((size_t) (e - q) == l && !strncasecmp (q, path, l))
		{
			refresh ();
			return 1;
		}
		const char *op = panes[!curp].path;	/* (l'autre vue) */
		if (dual && (size_t) (e - q) == strlen (op) && !strncasecmp (q, op, strlen (op)))
			other_stale = 1;
		q = e + 1;
	}
	return other_stale;
}

/* ---------------------------------------------------------------------------
 *  Place des volumes : statvfs de libposix (FS_INFO du service du volume), mesuree dans un thread
 *  (un partage ou Google Drive peut mettre du temps a repondre). Mesuree quand la liste change,
 *  apres une copie ou un effacement, a Actualiser ; les volumes locaux aussi toutes les SP_EVERY_MS.
 * --------------------------------------------------------------------------- */

#define SP_EVERY_MS	10000

struct vspace
{
	char disp[64];				/* point de montage */
	unsigned long long total, avail;	/* octets */
	int known;				/* 0 : pas encore mesure, ou place inconnue */
	int net;				/* volume du reseau (pas de mesure periodique) */
};

static pthread_mutex_t sp_mx = PTHREAD_MUTEX_INITIALIZER;
static struct vspace sp_req[VMAX], sp_res[VMAX];	/* (sp_mx) la liste a mesurer, les mesures */
static long sp_req_n, sp_res_n;
static int sp_want;				/* (sp_mx) 1 : tout mesurer */
static long sp_gen, sp_seen;			/* (sp_mx) mesures faites, vues par draw */
static struct vspace sp_ui[VMAX];		/* copie pour le dessin */
static long sp_ui_n;
static int sp_started;

static void *sp_thread (void *a)
{
	(void) a;
	struct vspace l[VMAX];
	long n = 0;
	for (;;)
	{
		int all = 0;
		for (long w = 0; w < SP_EVERY_MS; w += 200)
		{
			pthread_mutex_lock (&sp_mx);
			all = sp_want;
			sp_want = 0;
			if (all)
			{
				n = sp_req_n;
				memcpy (l, sp_req, sizeof l);
			}
			pthread_mutex_unlock (&sp_mx);
			if (all)
				break;
			usleep (200000);
		}
		for (long i = 0; i < n; i++)
		{
			if (!all && l[i].net)
				continue;	/* (le reseau : seulement sur demande) */
			struct statvfs s;
			l[i].known = 0;
			if (statvfs (l[i].disp, &s) == 0 && s.f_blocks)
			{
				l[i].total = (unsigned long long) s.f_blocks * s.f_frsize;
				l[i].avail = (unsigned long long) s.f_bavail * s.f_frsize;
				l[i].known = 1;
			}
		}
		pthread_mutex_lock (&sp_mx);
		for (long i = 0; i < n; i++)	/* (la liste a pu changer pendant la mesure) */
			for (long j = 0; j < sp_req_n; j++)
				if (!strcmp (sp_req[j].disp, l[i].disp))
				{
					sp_req[j].total = l[i].total;
					sp_req[j].avail = l[i].avail;
					sp_req[j].known = l[i].known;
				}
		memcpy (sp_res, sp_req, sizeof sp_res);
		sp_res_n = sp_req_n;
		memcpy (l, sp_req, sizeof l);
		n = sp_req_n;
		sp_gen++;
		pthread_mutex_unlock (&sp_mx);
	}
	return 0;
}

static int is_net_src (const char *s);

/* demander une mesure de tous les volumes (vols) */
static void space_ask (void)
{
	pthread_mutex_lock (&sp_mx);
	struct vspace old[VMAX];
	long on = sp_req_n;
	memcpy (old, sp_req, sizeof old);
	sp_req_n = vol_n;
	for (long i = 0; i < vol_n; i++)
	{
		struct vspace *r = &sp_req[i];
		memset (r, 0, sizeof *r);
		str_copy_n (r->disp, vols[i].disp, sizeof r->disp);
		r->net = is_net_src (vols[i].src);
		for (long j = 0; j < on; j++)	/* (la mesure d'avant, en attendant) */
			if (!strcmp (old[j].disp, r->disp))
			{
				r->total = old[j].total;
				r->avail = old[j].avail;
				r->known = old[j].known;
			}
	}
	sp_want = 1;
	pthread_mutex_unlock (&sp_mx);
	if (!sp_started)
	{
		pthread_t t;
		pthread_attr_t at;
		pthread_attr_init (&at);
		pthread_attr_setdetachstate (&at, PTHREAD_CREATE_DETACHED);
		sp_started = pthread_create (&t, &at, sp_thread, 0) == 0;
		pthread_attr_destroy (&at);
	}
}

/* -> 1 si de nouvelles mesures sont la (a redessiner) */
static int space_poll (void)
{
	pthread_mutex_lock (&sp_mx);
	int nw = sp_gen != sp_seen;
	if (nw)
	{
		sp_seen = sp_gen;
		memcpy (sp_ui, sp_res, sizeof sp_ui);
		sp_ui_n = sp_res_n;
	}
	pthread_mutex_unlock (&sp_mx);
	return nw;
}

/* volume -> sa mesure (0 : inconnue) */
static const struct vspace *space_of (const struct vol *v)
{
	for (long i = 0; i < sp_ui_n; i++)
		if (sp_ui[i].known && !strcmp (sp_ui[i].disp, v->disp))
			return &sp_ui[i];
	return 0;
}

static long vfs_call (void) { return sys3 (SYS_SVC_CALL_, vfs_ch, (long) &vmsg); }

/* relit la liste -> 1 si elle a change (table vols refaite) */
static long vol_poll (void)
{
	long n = 0;
	if (vfs_ch < 0)
		vfs_ch = sys3 (SYS_SVC_OPEN_, (long) "vfs", 0);	/* service pas encore ouvert */
	if (vfs_ch >= 0)
	{
		vmsg.op = VFS_VOLUMES_;
		vmsg.b = 0;
		vmsg.len = 0;
		if (vfs_call () < 0)
			vfs_ch = -1;
		else
		{
			n = vmsg.b;
			if ((unsigned long) n > VMAX)
				n = VMAX;
		}
	}
	if (vraw_n == n && !memcmp (vmsg.data, vraw, (size_t) (VE_SZ * n)))
		return 0;			/* meme liste */
	vraw_n = n;
	memcpy (vraw, vmsg.data, (size_t) (VE_SZ * n));
	vpts_n = 0;				/* leurs points de montage */
	if (vfs_ch >= 0)
	{
		vmsg.op = VFS_MOUNTS_;
		vmsg.b = 0;
		vmsg.len = 0;
		if (vfs_call () >= 0)
		{
			memcpy (vpts, vmsg.data, sizeof vpts);
			vpts[sizeof vpts - 1] = 0;
			vpts_n = vmsg.b;
		}
	}
	vol_build ();
	space_ask ();				/* (leur place, dans son thread) */
	if (vfs_ch >= 0 && cur_vol () < 0 && !is_recent ())
	{					/* le volume du dossier courant a disparu */
		go_sd0 ();
		set_msg ("Volume retiré : retour à /");
	}
	if (vfs_ch >= 0 && dual && vol_of (panes[!curp].path) < 0 && !is_recent_p (panes[!curp].path))
	{					/* celui de l'autre vue */
		str_copy_n (panes[!curp].path, "/", sizeof panes[0].path);
		panes[!curp].msg = TR ("Volume retiré : retour à /");
		other_stale = 1;
	}
	return 1;
}

/* volume : y aller (sa racine) */
static void go_vol (long i)
{
	str_copy_n (path, vols[i].disp, sizeof path);
	load_dir ();
}

static int mounts_set (const char *vol, const char *line);
static int is_net_src (const char *s);
static void drag_ghost (long i);
static void draw (void);

/* volume : le demonter (son service s'arrete) */
static void eject (long i)
{
	if (vfs_ch < 0)
		return;
	vmsg.op = VFS_UNMOUNT_;
	memcpy (vmsg.data, vols[i].low, 16);
	vmsg.len = 16;
	long r = vfs_call ();
	if (r >= 0 && is_net_src (vols[i].src))
	{					/* un partage : plus remonté au démarrage (le compte reste) */
		mounts_set (vols[i].low, 0);	/* (« net0 = ... ») */
	}
	if (cur_vol () == i)			/* on y etait : retour a / */
		go_sd0 ();
	sys3 (SYS_SLEEP_, 200, 0);		/* le volume part a la fin du service */
	vol_poll ();
	if (r < 0)
		set_err (r);
	else
		set_msg ("Volume démonté");
}

/* demonter le volume courant s'il est amovible */
static void eject_cur (void)
{
	long v = cur_vol ();
	if (v >= 0 && vols[v].rem)
		eject (v);
}

/* ---------------------------------------------------------------------------
 *  Partages du réseau (docs/SMB.md) : « Se connecter à un serveur… » monte un partage SMB en
 *  /net/netn (service smbfs). Les comptes vont dans /etc/smbfs.ini (gardés, même après un
 *  démontage), le montage dans /etc/mounts.txt (refait par vfs à chaque démarrage, retiré
 *  quand on démonte le volume).
 * --------------------------------------------------------------------------- */

#define SMB_INI		"/etc/smbfs.ini"
#define MOUNTS_TXT	"/etc/mounts.txt"
#define SRC_MAX		23			/* source d'un montage (table de vfs) */

static int is_net_src (const char *s) { return str_pre (s, "//") || str_pre (s, "smb:") || str_pre (s, "gdrive:"); }

/* fichier entier -> tampon alloue (0 : absent ; vide : "") */
static char *file_load (const char *p)
{
	FILE *f = fopen (p, "rb");
	if (!f)
		return 0;
	size_t cap = 4096, n = 0;
	char *b = malloc (cap + 1);
	size_t r;
	while (b && (r = fread (b + n, 1, cap - n, f)) > 0)
	{
		n += r;
		if (n == cap)
		{
			char *nb = realloc (b, cap * 2 + 1);
			if (!nb)
				break;
			b = nb;
			cap *= 2;
		}
	}
	fclose (f);
	if (b)
		b[n] = 0;
	return b;
}

static int file_save (const char *p, const char *s)
{
	FILE *f = fopen (p, "wb");
	if (!f)
		return -1;
	size_t n = strlen (s);
	int ok = fwrite (s, 1, n, f) == n;
	return fclose (f) == 0 && ok ? 0 : -1;
}

/* une ligne (sans fin) : « [nom] » -> 1, le nom dans out */
static int line_section (const char *l, const char *end, char *out, long max)
{
	while (l < end && (*l == ' ' || *l == 9))
		l++;
	if (l >= end || *l != '[')
		return 0;
	l++;
	long n = 0;
	while (l < end && *l != ']' && n < max - 1)
		out[n++] = *l++;
	out[n] = 0;
	return 1;
}

/* smbfs.ini : la section « sec » remplacee par « body » (lignes « cle = valeur ») */
static int ini_put_section (const char *sec, const char *body)
{
	char *old = file_load (SMB_INI);
	size_t cap = (old ? strlen (old) : 0) + strlen (sec) + strlen (body) + 16;
	char *out = malloc (cap), *o = out, name[160];
	if (!out)
	{
		free (old);
		return -1;
	}
	*o = 0;
	int skip = 0;
	for (const char *l = old ? old : ""; *l;)
	{
		const char *e = strchr (l, '\n');
		e = e ? e + 1 : l + strlen (l);
		if (line_section (l, e, name, sizeof name))
			skip = !str_icmp (name, sec);
		if (!skip)
		{
			memcpy (o, l, (size_t) (e - l));
			o += e - l;
		}
		l = e;
	}
	if (o > out && o[-1] != '\n')
		*o++ = '\n';
	o += sprintf (o, "[%s]\n%s", sec, body);
	int r = file_save (SMB_INI, out);
	free (old);
	free (out);
	return r;
}

/* smbfs.ini : la section « sec » existe-t-elle avec un compte ? */
static int ini_has_user (const char *sec)
{
	char *s = file_load (SMB_INI), name[160];
	int in = 0, found = 0;
	for (const char *l = s ? s : ""; *l && !found;)
	{
		const char *e = strchr (l, '\n');
		e = e ? e + 1 : l + strlen (l);
		if (line_section (l, e, name, sizeof name))
			in = !str_icmp (name, sec);
		else if (in)
		{
			const char *p = l;
			while (*p == ' ' || *p == 9)
				p++;
			found = str_pre (p, "user") || str_pre (p, "password");
		}
		l = e;
	}
	free (s);
	return found;
}

/* mounts.txt : la ligne du volume « vol » (« NET0 ») retiree, puis « line » ajoutee (0 : aucune) */
static int mounts_set (const char *vol, const char *line)
{
	char *old = file_load (MOUNTS_TXT);
	size_t cap = (old ? strlen (old) : 0) + (line ? strlen (line) : 0) + 64;
	char *out = malloc (cap), *o = out;
	if (!out)
	{
		free (old);
		return -1;
	}
	size_t vl = strlen (vol);
	for (const char *l = old ? old : ""; *l;)
	{
		const char *e = strchr (l, '\n');
		e = e ? e + 1 : l + strlen (l);
		const char *p = l;
		while (*p == ' ' || *p == 9)
			p++;
		int mine = 0;			/* « NET0 = ... » (sans la casse) */
		if ((size_t) (e - p) > vl && !strncasecmp (p, vol, vl))
		{
			const char *q = p + vl;
			while (*q == ' ' || *q == 9)
				q++;
			mine = *q == '=';
		}
		if (!mine)
		{
			memcpy (o, l, (size_t) (e - l));
			o += e - l;
		}
		l = e;
	}
	if (o > out && o[-1] != '\n')
		*o++ = '\n';
	*o = 0;
	if (line)
		sprintf (o, "%s\n", line);
	int r = file_save (MOUNTS_TXT, out);
	free (old);
	free (out);
	return r;
}

/* « smb://hote/partage/dossier », « \\hote\partage », « //hote/partage », « hote/partage » -> hote,
 * partage, dossier (« a/b », peut etre vide) ; 0 si l'adresse est incomplete */
static int smb_parse (const char *in, char *host, char *share, char *dir)
{
	char t[300];
	long n = 0;
	while (*in == ' ')
		in++;
	if (!strncasecmp (in, "smb://", 6))
		in += 6;
	for (; *in && n < (long) sizeof t - 1; in++)
		t[n++] = *in == '\\' ? '/' : *in;
	while (n && (t[n - 1] == '/' || t[n - 1] == ' '))
		n--;
	t[n] = 0;
	char *p = t;
	while (*p == '/')
		p++;
	char *s = strchr (p, '/');
	if (!s || s == p)
		return 0;
	*s++ = 0;
	char *d = strchr (s, '/');
	if (d)
		*d++ = 0;
	if (!*s)
		return 0;
	str_copy_n (host, p, 128);
	str_copy_n (share, s, 128);
	str_copy_n (dir, d ? d : "", 160);
	return 1;
}

/* les serveurs deja connus (sections « hote/partage » de smbfs.ini) -> « smb://a/b|smb://c/d » */
static void known_servers (char *out, long max)
{
	char *s = file_load (SMB_INI), name[160];
	long n = 0;
	out[0] = 0;
	for (const char *l = s ? s : ""; *l;)
	{
		const char *e = strchr (l, '\n');
		e = e ? e + 1 : l + strlen (l);
		if (line_section (l, e, name, sizeof name) && strchr (name, '/') && n + (long) strlen (name) + 8 < max)
			n += sprintf (out + n, "%ssmb://%s", n ? "|" : "", name);
		l = e;
	}
	free (s);
}

/* le premier /net/netn libre -> n (-1 : aucun) */
static long free_net (void)
{
	for (long n = 0; n < 10; n++)
	{
		char low[8] = "net0";
		low[3] = (char) ('0' + n);
		long used = 0;
		for (long i = 0; i < vol_n; i++)
			used |= !strcmp (vols[i].low, low);
		if (!used)
			return n;
	}
	return -1;
}

/* Entree dans un champ du dialogue : Se connecter */
static long cd_fields[3];
static long connect_fn (long d, long w, long code)
{
	if (code == UN_ACTIVATE && (w == cd_fields[0] || w == cd_fields[1] || w == cd_fields[2]))
		ui_dialog_close (d, 1);
	return 0;
}

/* le dialogue : adresse, compte, mot de passe (prerempli par les champs) -> 1 Se connecter */
static int connect_dialog (char *addr, char *user, char *pw)
{
	enum { DW = 480, DH = 300, M = 16, LW = 120, CH = 30 };
	char items[1024];
	known_servers (items, sizeof items);
	long d = ui_dialog_begin (DW, DH, (long) TR ("Se connecter à un serveur"));
	long y = 20;
	ui_label (M, y + 6, LW, 20, (long) TR ("Adresse"), 0);
	long wa = items[0] ? ui_combobox (M + LW, y, DW - 2 * M - LW, CH, (long) items)
			   : ui_textbox (M + LW, y, DW - 2 * M - LW, CH, 0);
	ui_w_set_text (wa, (long) addr);
	if (!items[0])
		ui_textbox_hint (wa, (long) "smb://192.168.0.9/Partage");
	y += 42;
	ui_label (M, y + 6, LW, 20, (long) TR ("Utilisateur"), 0);
	long wu = ui_textbox (M + LW, y, DW - 2 * M - LW, CH, 0);
	ui_w_set_text (wu, (long) user);
	ui_textbox_hint (wu, (long) TR ("Compte du serveur (ou DOMAINE\\nom)"));
	y += 42;
	ui_label (M, y + 6, LW, 20, (long) TR ("Mot de passe"), 0);
	long wp = ui_textbox (M + LW, y, DW - 2 * M - LW, CH, UTF_PASSWORD);
	ui_w_set_text (wp, (long) pw);
	y += 44;
	ui_label (M, y, DW - 2 * M, 60, (long) TR ("Le partage est remonté à chaque démarrage, jusqu'à ce que vous le "
		"démontiez. Le compte est gardé dans /etc/smbfs.ini (vides : ceux déjà enregistrés pour ce partage)."),
		ULB_DIM | ULB_WRAP);
	long e = ui_dialog_button (d, 0, (long) TR ("Se connecter"), 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, e);
	ui_dialog_cancel (d, 0);
	ui_w_focus (wa);
	cd_fields[0] = wa;
	cd_fields[1] = wu;
	cd_fields[2] = wp;
	long r = ui_dialog_run (d, (long) connect_fn, 0);
	str_copy_n (addr, (const char *) ui_w_get_text (wa), 300);	/* (avant ui_dialog_end) */
	str_copy_n (user, (const char *) ui_w_get_text (wu), 128);
	str_copy_n (pw, (const char *) ui_w_get_text (wp), 128);
	ui_dialog_end (d);
	return r == 1;
}

/* « Se connecter a un serveur… » */
static void connect_server (void)
{
	static char addr[300], user[128], pw[128];
	char host[128], share[128], dir[160], sec[300], src[200], line[300], body[600], err[400];
	if (vfs_ch < 0)
		vol_poll ();
	for (;;)
	{
		if (!connect_dialog (addr, user, pw))
			return;
		if (!smb_parse (addr, host, share, dir))
		{
			ui_message ((long) TR ("Se connecter à un serveur"),
				    (long) TR ("Adresse incomplète : il faut le serveur et le partage, par exemple smb://192.168.0.9/Partage."),
				    UM_OK);
			continue;
		}
		long n = free_net ();
		if (n < 0 || vfs_ch < 0)
		{
			ui_message ((long) TR ("Se connecter à un serveur"), (long) TR ("Plus de volume NET libre (/net/net0 à /net/net9)."), UM_OK);
			return;
		}
		/* la source : « //hote/partage[/dossier] » si elle tient dans la table de vfs, sinon un nom
		 * (« smb:netN ») et l'adresse dans sa section */
		snprintf (src, sizeof src, "//%s/%s%s%s", host, share, dir[0] ? "/" : "", dir);
		int alias = strlen (src) > SRC_MAX;
		snprintf (sec, sizeof sec, "%s/%s", host, share);
		if (alias)
		{
			snprintf (sec, sizeof sec, "net%ld", n);
			snprintf (src, sizeof src, "smb:net%ld", n);
		}
		/* le compte : « DOMAINE\nom » ou « nom » ; vides : celui deja enregistre */
		char *dom = strchr (user, '\\');
		if (user[0] || pw[0] || alias || !ini_has_user (sec))
		{
			char *b = body;
			if (alias)
				b += sprintf (b, "host = %s\nshare = %s\n%s%s%s", host, share, dir[0] ? "dir = " : "", dir, dir[0] ? "\n" : "");
			if (dom)
				b += sprintf (b, "domain = %.*s\nuser = %s\n", (int) (dom - user), user, dom + 1);
			else if (user[0])
				b += sprintf (b, "user = %s\n", user);
			if (pw[0])
				sprintf (b, "password = %s\n", pw);
			if (ini_put_section (sec, body) < 0)
			{
				set_err (kerr (errno));
				return;
			}
		}
		/* le montage, puis un premier acces (smbfs se connecte a la premiere requete) */
		snprintf (line, sizeof line, "net%ld %s smbfs", n, src);
		vmsg.op = VFS_MOUNT_;
		str_copy_n ((char *) vmsg.data, line, sizeof vmsg.data);
		vmsg.len = (unsigned) strlen (line) + 1;
		set_msg ("Connexion…");
		draw ();
		long r = vfs_call ();
		char root[24];
		snprintf (root, sizeof root, "/net/net%ld", n);
		DIR *dd = r < 0 ? 0 : opendir (root);
		if (!dd)
		{
			if (r >= 0)
			{			/* (monté mais inaccessible : demonte) */
				vmsg.op = VFS_UNMOUNT_;
				memset (vmsg.data, 0, 16);
				snprintf ((char *) vmsg.data, 16, "net%ld", n);
				vmsg.len = 16;
				vfs_call ();
			}
			snprintf (err, sizeof err, "%s //%s/%s.\n%s", TR ("Connexion impossible à"), host, share,
				  TR ("Vérifiez l'adresse, le compte et le mot de passe, et que le serveur partage ce dossier."));
			ui_message ((long) TR ("Se connecter à un serveur"), (long) err, UM_OK);
			msg = 0;
			continue;
		}
		closedir (dd);
		/* remonte a chaque demarrage (« net0 = //hote/partage, smbfs » : monte sur /net/net0) */
		snprintf (line, sizeof line, "net%ld = %s, smbfs", n, src);
		snprintf (root, sizeof root, "net%ld", n);
		mounts_set (root, line);
		pw[0] = 0;			/* (ne reste pas dans le dialogue) */
		sys3 (SYS_SLEEP_, 100, 0);
		vol_poll ();
		snprintf (path, sizeof path, "/net/net%ld", n);
		load_dir ();
		set_msg ("Connecté");
		return;
	}
}

/* ---------------------------------------------------------------------------
 *  Google Drive (docs/GDRIVE.md) : « Se connecter à Google Drive… » connecte un compte Google
 *  (oauthd, par NetKit : le navigateur s'ouvre sur la page de Google, comme dans Mail), puis monte
 *  le Drive en /net/netn (service gdrivefs : source « gdrive:NOM », compte OAuth « gdrive/NOM »).
 *  Le montage va dans /etc/mounts.txt, comme un partage ; le jeton, chiffré, dans HOME:/oauth.
 * --------------------------------------------------------------------------- */

#define WEB_PROGRAM	"/apps/jet.app/main"
#define E_NOENT_	-2
#define E_AGAIN_	-11
#define E_ACCES_	-13
#define GD_NAME_MAX	16			/* « gdrive:NOM » dans la table de vfs (SRC_MAX) */

/* le nom du Drive : lettres, chiffres, « . _ - » */
static int gd_name_ok (const char *s)
{
	size_t n = strlen (s);
	if (!n || n > GD_NAME_MAX)
		return 0;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
		      *s == '.' || *s == '_' || *s == '-'))
			return 0;
	return 1;
}

static long gd_field;
static long gd_fn (long d, long w, long code)
{
	if (code == UN_ACTIVATE && w == gd_field)
		ui_dialog_close (d, 1);
	return 0;
}

/* le dialogue : le nom du Drive -> 1 Se connecter */
static int gdrive_dialog (char *name)
{
	enum { DW = 480, DH = 250, M = 16, LW = 130, CH = 30 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("Se connecter à Google Drive"));
	long y = 20;
	ui_label (M, y + 6, LW, 20, (long) TR ("Nom du Drive"), 0);
	long wn = ui_textbox (M + LW, y, DW - 2 * M - LW, CH, 0);
	ui_w_set_text (wn, (long) name);
	ui_textbox_hint (wn, (long) "perso");
	y += 46;
	ui_label (M, y, DW - 2 * M, 110, (long) TR ("Le navigateur s'ouvre sur la page de Google : connectez-vous et "
		"autorisez Korin. Le Drive est ensuite monté dans /net, lu et écrit, et remonté à chaque démarrage jusqu'à "
		"ce que vous le démontiez. Aucun mot de passe n'est gardé, seulement un jeton chiffré (HOME:/oauth)."),
		ULB_DIM | ULB_WRAP);
	long e = ui_dialog_button (d, 0, (long) TR ("Se connecter"), 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, e);
	ui_dialog_cancel (d, 0);
	ui_w_focus (wn);
	gd_field = wn;
	long r = ui_dialog_run (d, (long) gd_fn, 0);
	str_copy_n (name, (const char *) ui_w_get_text (wn), GD_NAME_MAX + 8);
	ui_dialog_end (d);
	return r == 1;
}

/* l'attente du retour de Google (oauth_browser_poll, une fois par seconde) */
static const char *gd_acct;
static long gd_err, gd_last;
static long gd_tick (long d)
{
	long t = time_ms ();
	if (t - gd_last < 900)
		return 0;
	gd_last = t;
	long r = oauth_browser_poll ((long) gd_acct);
	if (r == 0)
		ui_dialog_close (d, 1);
	else if (r != E_AGAIN_)
	{
		gd_err = r;
		ui_dialog_close (d, 2);
	}
	return 0;
}

/* -> 1 connecté, 0 Annuler, 2 erreur (gd_err) */
static long gdrive_wait (const char *acct)
{
	enum { DW = 440, DH = 160, M = 16 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("Se connecter à Google Drive"));
	ui_label (M, 20, DW - 2 * M, 80, (long) TR ("Connectez-vous à votre compte Google dans la fenêtre du navigateur, "
		"puis autorisez Korin. Cette fenêtre se fermera toute seule."), ULB_WRAP);
	ui_dialog_button (d, 0, (long) TR ("Annuler"), 0, 0, DW - 12);
	ui_dialog_cancel (d, 0);
	gd_acct = acct;
	gd_err = 0;
	gd_last = 0;
	ui_dialog_timer (d, (long) gd_tick, 250);
	long r = ui_dialog_run (d, 0, 0);
	ui_dialog_end (d);
	return r;
}

/* un message d'erreur d'oauthd (« texte : détail ») */
static void gd_fail (const char *what, long r)
{
	char e[300] = "", m[600];
	if (oauth_error ((long) e, sizeof e) < 0)
		e[0] = 0;
	snprintf (m, sizeof m, "%s (%ld)%s%s", TR (what), r, e[0] ? "\n" : "", e);
	ui_message ((long) TR ("Se connecter à Google Drive"), (long) m, UM_OK);
}

/* La connexion se fait en arrière-plan (un thread, libc et NetKit seulement) : Fichiers reste utilisable.
 * Étape 1 (thread) : le compte gdrive/NOM (créé au besoin) et son jeton ; s'il faut se connecter, la
 * fenêtre ouvre Jet et attend Google (dialogue), puis relance le thread. Étape 2 (thread) : le montage
 * (VFS_MOUNT, sur son propre canal de vfs), la ligne de /etc/mounts.txt, puis un premier accès qui prépare
 * le service (jeton, connexion TLS, liste de la racine : quelques secondes) ; une erreur à ce premier accès
 * ne démonte pas (un Actualiser suffit ensuite). La boucle principale voit la fin (gdj_poll) et ouvre le Drive. */

enum { GDJ_IDLE, GDJ_RUN, GDJ_LOGIN, GDJ_DONE, GDJ_FAIL };
static struct
{
	volatile int state;
	int skip_auth;
	long n;					/* /net/netn */
	long err;
	int listed;				/* premier accès : 1 réussi */
	const char *what;			/* message d'échec (à traduire) */
	char acct[48], src[32];
	char detail[300];
} gdj;

static void gdj_fail (const char *what, long r)
{
	gdj.what = what;
	gdj.err = r;
	if (oauth_error ((long) gdj.detail, sizeof gdj.detail) < 0)
		gdj.detail[0] = 0;
	gdj.state = GDJ_FAIL;
}

static void *gdj_thread (void *arg)
{
	(void) arg;
	if (!gdj.skip_auth)
	{
		long st = oauth_status ((long) gdj.acct);
		if (st == E_NOENT_)
		{
			long r = oauth_setup ((long) gdj.acct, (long) "provider = google\n");
			if (r < 0)
			{
				gdj_fail ("Compte Google impossible à créer", r);
				return 0;
			}
			st = 0;
		}
		if (st < 0)
		{
			gdj_fail ("Service des comptes en ligne (oauthd) injoignable", st);
			return 0;
		}
		if (st == 0)
		{
			gdj.state = GDJ_LOGIN;
			return 0;
		}
		static char tok[4096];		/* (connecté : le jeton se rafraîchit-il encore ?) */
		long r = oauth_token ((long) gdj.acct, (long) tok, sizeof tok);
		memset (tok, 0, sizeof tok);
		if (r == E_ACCES_)
		{
			gdj.state = GDJ_LOGIN;
			return 0;
		}
		if (r < 0)
		{
			gdj_fail ("Google ne répond pas", r);
			return 0;
		}
	}
	/* le montage, sur un canal et un message à lui (la fenêtre garde les siens) */
	static struct svc_msg m;
	long ch = sys3 (SYS_SVC_OPEN_, (long) "vfs", 0);
	if (ch < 0)
	{
		gdj_fail ("Le service des fichiers (vfs) ne répond pas", ch);
		return 0;
	}
	char line[96], root[24], low[8];
	snprintf (line, sizeof line, "net%ld %s gdrivefs", gdj.n, gdj.src);
	memset (&m, 0, sizeof m);
	m.op = VFS_MOUNT_;
	str_copy_n ((char *) m.data, line, sizeof m.data);
	m.len = (unsigned) strlen (line) + 1;
	long r = sys3 (SYS_SVC_CALL_, ch, (long) &m);
	if (r < 0)
	{
		gdj.detail[0] = 0;
		gdj.what = "Le Drive n'a pas pu être monté ; le journal du système dit pourquoi (gdrivefs, vfs).";
		gdj.err = r;
		gdj.state = GDJ_FAIL;
		return 0;
	}
	snprintf (low, sizeof low, "net%ld", gdj.n);
	snprintf (line, sizeof line, "net%ld = %s, gdrivefs", gdj.n, gdj.src);
	mounts_set (low, line);		/* (remonté à chaque démarrage) */
	/* premier accès : le service se prépare ; son erreur ne démonte pas */
	snprintf (root, sizeof root, "/net/net%ld", gdj.n);
	DIR *dd = opendir (root);
	gdj.listed = dd != 0;
	if (dd)
	{
		while (readdir (dd))
			;
		closedir (dd);
	}
	else
		gdj.err = kerr (errno);
	gdj.state = GDJ_DONE;
	return 0;
}

static int gdj_start (int skip_auth)
{
	pthread_t th;
	pthread_attr_t at;
	pthread_attr_init (&at);
	pthread_attr_setstacksize (&at, 256 * 1024);
	gdj.skip_auth = skip_auth;
	gdj.state = GDJ_RUN;
	int ok = pthread_create (&th, &at, gdj_thread, 0) == 0;
	pthread_attr_destroy (&at);
	if (!ok)
	{
		gdj.state = GDJ_IDLE;
		return 0;
	}
	pthread_detach (th);
	set_msg ("Connexion à Google Drive…");
	return 1;
}

/* la connexion par le navigateur (fenêtre) -> 1 connecté, 0 abandon */
static int gdrive_browser (const char *acct)
{
	static char url[4096];
	long r = oauth_browser_begin ((long) acct, 0, (long) url, sizeof url - 1);
	if (r < 0)
	{
		gd_fail ("Connexion à Google impossible", r);
		return 0;
	}
	url[r < (long) sizeof url ? r : (long) sizeof url - 1] = 0;
	url[strcspn (url, "\r\n")] = 0;
	pid_t pid;
	char *argv[] = { (char *) WEB_PROGRAM, url, 0 };
	if (posix_spawn (&pid, WEB_PROGRAM, 0, 0, argv, environ) != 0)
	{
		ui_message ((long) TR ("Se connecter à Google Drive"),
			    (long) TR ("Le navigateur (Jet) est introuvable. Dans le Terminal : oauth browser gdrive/NOM, puis "
				       "ouvrez l'adresse donnée."), UM_OK);
		return 0;
	}
	r = gdrive_wait (acct);
	if (r == 2)
		gd_fail ("Connexion à Google refusée", gd_err);
	return r == 1;
}

/* (boucle principale, à chaque événement ou toutes les 500 ms) la connexion en arrière-plan a-t-elle avancé ? */
static void gdj_poll (void)
{
	int st = gdj.state;
	if (st == GDJ_IDLE || st == GDJ_RUN)
		return;
	gdj.state = GDJ_IDLE;
	if (st == GDJ_LOGIN)
	{					/* il faut se connecter : Jet, puis l'étape 2 */
		msg = 0;
		draw ();
		if (gdrive_browser (gdj.acct))
			gdj_start (1);
	}
	else if (st == GDJ_FAIL)
	{
		char m[600];
		snprintf (m, sizeof m, "%s (%ld)%s%s", TR (gdj.what), gdj.err, gdj.detail[0] ? "\n" : "", gdj.detail);
		msg = 0;
		draw ();
		ui_message ((long) TR ("Se connecter à Google Drive"), (long) m, UM_OK);
	}
	else
	{					/* monté : on y va */
		vol_poll ();
		snprintf (path, sizeof path, "/net/net%ld", gdj.n);
		load_dir ();
		if (gdj.listed)
			set_msg ("Connecté");
		else
			set_msg ("Google Drive monté, mais pas encore lu : Actualiser (F5) dans un instant");
	}
	draw ();
}

/* « Se connecter à Google Drive… » */
static void connect_gdrive (void)
{
	static char name[GD_NAME_MAX + 8] = "perso";
	if (gdj.state != GDJ_IDLE)
	{
		ui_message ((long) TR ("Se connecter à Google Drive"), (long) TR ("Une connexion à Google Drive est déjà en cours."), UM_OK);
		return;
	}
	if (vfs_ch < 0)
		vol_poll ();
	for (;;)
	{
		if (!gdrive_dialog (name))
			return;
		if (!gd_name_ok (name))
		{
			ui_message ((long) TR ("Se connecter à Google Drive"),
				    (long) TR ("Le nom : 16 lettres, chiffres, « . », « _ » ou « - » au plus, par exemple perso."), UM_OK);
			continue;
		}
		snprintf (gdj.src, sizeof gdj.src, "gdrive:%s", name);
		for (long i = 0; i < vol_n; i++)	/* (déjà monté : on y va) */
			if (!strcmp (vols[i].src, gdj.src))
			{
				go_vol (i);
				return;
			}
		gdj.n = free_net ();
		if (gdj.n < 0 || vfs_ch < 0)
		{
			ui_message ((long) TR ("Se connecter à Google Drive"), (long) TR ("Plus de volume NET libre (/net/net0 à /net/net9)."), UM_OK);
			return;
		}
		if (!netkit_kit_open ())
		{
			ui_message ((long) TR ("Se connecter à Google Drive"), (long) TR ("NetKit est introuvable (lib/netkit.so)."), UM_OK);
			return;
		}
		snprintf (gdj.acct, sizeof gdj.acct, "gdrive/%s", name);
		gdj.detail[0] = 0;
		gdj.listed = 0;
		gdj_start (0);
		return;
	}
}

/* ---------------------------------------------------------------------------
 *  Actions
 * --------------------------------------------------------------------------- */

/* ouvrir l'entree selectionnee */
static void open_sel (void)
{
	if (cursel < 0)
		return;
	char *e = make_full (cursel);
	struct ent *en = ent_ptr (cursel);
	long r;
	if (en->kind == K_DIR)
	{
		go_dir (fullpath);
		return;
	}
	if (en->kind == K_FILE)
		r = ui_assoc_open ((long) fullpath);	/* son application (UIKit : app.txt, fileassoc.ini) */
	else
	{					/* paquet : son main */
		str_cat (e, "/main");
		pid_t pid;
		char *argv[] = { fullpath, 0 };
		int x = posix_spawn (&pid, fullpath, 0, 0, argv, environ);
		r = x ? kerr (x) : pid;
	}
	if (r >= 0 && en->kind == K_FILE && is_recent ())
	{					/* (Recents : il passe en tete) */
		char keep[600];
		str_copy_n (keep, fullpath, sizeof keep);
		load_dir ();
		select_name (keep);
	}
	if (r < 0)
		set_err (r);
	else
		set_msg ("Programme lancé");
}

/* « Ouvrir avec » : l'application choisie dans le dialogue de UIKit */
static void open_with_sel (void)
{
	if (cursel < 0 || ent_ptr (cursel)->kind != K_FILE)
		return;
	make_full (cursel);
	long r = ui_open_with ((long) fullpath);
	if (!r)
		return;				/* Annuler */
	if (r < 0)
		set_err (r);
	else
		set_msg ("Programme lancé");
}

/* dossier parent (le dossier quitte reste selectionne) */
static void go_parent (void)
{
	if (is_recent () || vol_root (path))
		return;				/* (Recents, « HOME:/ » : rien au-dessus) */
	const char *last = path;
	for (const char *p = path; *p; p++)
		if (*p == '/')
			last = p + 1;
	str_copy_n (tmpname, last, 256);
	size_t n = strlen (path);
	path_parent (path);
	if (strlen (path) == n)
		return;				/* deja a la racine */
	load_dir ();
	select_name (tmpname);
}

/* « Nouveau dossier », « Nouveau dossier 2 »... (le premier libre) */
static void new_folder (void)
{
	if (is_recent ())
	{
		set_msg ("Impossible dans Récents");
		return;
	}
	struct stat st;
	for (long i = 1; i < 100; i++)
	{
		char *e = tr_cat (tmpname, "Nouveau dossier");
		if (i != 1)
		{
			*e++ = ' ';
			fmt_dec ((unsigned long) i, e);
		}
		path_join (fullpath, path, tmpname);
		if (stat (fullpath, &st) == 0)
			continue;
		long r = mkdir (fullpath, 0777) ? kerr (errno) : 0;
		load_dir ();
		if (r)
			set_err (r);
		else
		{
			select_name (tmpname);
			chg_add (path);
			chg_publish ();
		}
		return;
	}
}

static void draw (void);

/* supprimer la selection, apres confirmation (menu surgissant) */
static int del_enqueue (const char *p);

static void delete_many (void);

static void delete_sel (void)
{
	if (is_recent ())
	{					/* (Recents : retirer de la liste, le fichier reste) */
		recent_forget ();
		return;
	}
	if (nsel > 1)
	{
		delete_many ();
		return;
	}
	if (cursel < 0 || !is_sel (cursel))
		return;
	struct ent *en = ent_ptr (cursel);
	int dir = en->kind != K_FILE;
	char *e = tr_cat (popbuf, dir ? "Supprimer le dossier « " : "Supprimer « ");	/* « Supprimer « nom » », « Annuler » */
	e = str_cat (e, ent_shown (en));
	e = tr_cat (e, dir ? " » et son contenu" : " »") + 1;
	e = tr_cat (e, "Annuler");
	e[1] = 0;
	long x = scr[0], y = scr[1];		/* au milieu de l'ecran */
	if (!x)
	{
		x = 640;
		y = 480;
	}
	if (ui_popup ((x - 440) >> 1, (y >> 1) - 40, (long) popbuf, 440) != 0)
		return;				/* Annuler, Echap, clic ailleurs */
	make_full (cursel);
	if (dir)
	{					/* un dossier : avec son contenu, en arriere-plan (la bande, Annuler) */
		if (!del_enqueue (fullpath))
			set_err (-ENOMEM);
		return;
	}
	long r = 0;
	if (unlink (fullpath))
		r = kerr (errno);
	load_dir ();
	if (r)
		set_err (r);
	else
	{
		set_msg ("Supprimé");
		chg_add (path);
		chg_publish ();
	}
}

/* supprimer plusieurs elements choisis, apres une seule confirmation : les fichiers tout de suite, les dossiers
 * (avec leur contenu) en arriere-plan, chacun un lot */
static void delete_many (void)
{
	char t[64];
	snprintf (t, sizeof t, TR ("Supprimer ces %ld éléments"), nsel);
	char *e = str_cat (popbuf, t) + 1;
	e = tr_cat (e, "Annuler");
	e[1] = 0;
	long x = scr[0], y = scr[1];
	if (!x)
	{
		x = 640;
		y = 480;
	}
	if (ui_popup ((x - 440) >> 1, (y >> 1) - 40, (long) popbuf, 440) != 0)
		return;
	long err = 0, files = 0;
	for (long i = 0; i < nents; i++)
	{
		if (!is_sel (i))
			continue;
		make_full (i);
		if (ent_ptr (i)->kind != K_FILE)
		{
			if (!del_enqueue (fullpath))
				err = -ENOMEM;
		}
		else if (unlink (fullpath))
			err = kerr (errno);
		else
			files++;
	}
	if (files)
	{
		load_dir ();
		chg_add (path);
		chg_publish ();
	}
	if (err)
		set_err (err);
	else if (files)
		set_msg ("Supprimé");
}

/* nouveau nom (rogne de ses espaces autour, sur place) -> 0 s'il est permis, sinon le message
 * (FAT32 et exFAT refusent / \ : * ? " < > | et les caracteres de controle ; ni vide, ni . ou ..) */
static const char *name_check (char *s)
{
	char *b = s, *e;
	while (*b == ' ')
		b++;
	for (e = b + strlen (b); e > b && e[-1] == ' '; e--)
		;
	*e = 0;
	if (b != s)
		memmove (s, b, (size_t) (e - b) + 1);
	if (!*s)
		return "Le nom ne peut pas être vide.";
	if (!strcmp (s, ".") || !strcmp (s, ".."))
		return "Ce nom est réservé.";
	for (const unsigned char *p = (const unsigned char *) s; *p; p++)
		if (*p < 0x20 || strchr ("/\\:*?\"<>|", *p))
			return "Un nom ne peut contenir aucun de ces caractères : / \\ : * ? \" < > |";
	return 0;
}

/* deux noms egaux aux majuscules pres (ASCII : FAT32 et exFAT ne distinguent pas la casse) */
static int same_nocase (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		unsigned char x = (unsigned char) *a, y = (unsigned char) *b;
		if (x - 'A' < 26u)
			x += 32;
		if (y - 'A' < 26u)
			y += 32;
		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

/* renommer la selection (fichier, dossier, paquet) : le nouveau nom par le dialogue de UIKit (le
 * nom actuel prerempli ; redemande tant qu'il est refuse), puis rename() (meme dossier, donc meme
 * volume : sans copie) ; la liste relue, l'entree renommee choisie */
static void rename_sel (void)
{
	if (is_recent ())
	{
		set_msg ("Impossible dans Récents");
		return;
	}
	if (cursel < 0)
		return;
	char old[256], nn[256], to[520];
	str_copy_n (old, ent_ptr (cursel)->name, 256);
	str_copy_n (nn, old, 256);
	for (;;)
	{
		if (!ui_ask ((long) "Renommer", (long) "Nouveau nom :", (long) nn, 256))
			return;			/* Annuler, Echap */
		const char *bad = name_check (nn);
		if (!bad && !strcmp (nn, old))
			return;			/* inchange */
		path_join (to, path, nn);
		struct stat st;
		if (!bad && !same_nocase (nn, old) && stat (to, &st) == 0)
			bad = "Un élément porte déjà ce nom dans ce dossier.";
		if (!bad)
			break;
		ui_message ((long) "Renommer", (long) bad, UM_OK);	/* (traduits par UIKit) */
	}
	path_join (fullpath, path, old);
	long r = rename (fullpath, to) ? kerr (errno) : 0;
	load_dir ();
	if (r)
	{
		select_name (old);
		set_err (r);
		return;
	}
	select_name (nn);
	set_msg ("Renommé");
	chg_add (path);
	chg_publish ();
}

/* ---------------------------------------------------------------------------
 *  Copier, couper, coller, deposer
 *
 * Copier et Couper mettent le chemin de la selection dans le presse-papiers de fichiers d'UIKit
 * (ui_clip_set_files) ; Coller les y prend (ui_clip_get_files, aussi ceux d'une autre app). Un
 * fichier depose sur la fenetre (UDT_FILES) va dans le dossier pointe, ou le dossier courant.
 * Deplacer (couper, deposer) essaie d'abord rename() : instantane sur un meme volume ; entre deux
 * volumes, c'est une copie (puis, pour couper, la source effacee).
 *
 * La copie se fait en arriere-plan : chaque collage devient un lot, mis en file ; un thread qui ne
 * se sert que de la libc (UIKit reste au thread de la fenetre) prend les lots l'un apres l'autre,
 * compte (taille totale), puis copie par morceaux de 1 Mo. La fenetre reste utilisable : la ligne
 * d'etat devient une bande de progression (element en cours, barre, octets sur le total, debit,
 * temps restant, lots en attente, bouton Annuler), relue par la boucle principale (cp_poll, 4 fois
 * par seconde). Annuler arrete le lot en cours entre deux morceaux (le fichier partiel supprime, les
 * elements deja copies restent) et retire ceux en attente. Une lecture ou une ecriture lente
 * (volume reseau ; gdrivefs envoie un fichier a Google a sa fermeture, d'ou « Finalisation ») ne
 * fige rien. Un lot fini est rapporte par la boucle (erreur, dossier relu). Une copie de moins de
 * 0,4 s ne montre pas la bande. Fermer Fichiers pendant une copie demande s'il faut l'arreter.
 * --------------------------------------------------------------------------- */

#define CP_MAX		64			/* elements d'un collage */
#define CP_BUF		(1 << 20)		/* morceau copie */
#define CP_PATH		600
#define CP_QUIET_MS	400			/* sans bande jusque-la */
#define M_COPY		0			/* modes de paste_list */
#define M_CUT		1
#define M_DROP		2
#define M_DEL		3			/* supprimer un dossier et son contenu (delete_sel) */

static void refresh (void);

struct cp_item
{
	char src[CP_PATH];
	char dst[CP_PATH];
	int move;				/* la source effacee apres la copie */
};

struct batch					/* un collage (un lot) */
{
	struct batch *next;
	long n;
	int mode;				/* M_COPY, M_CUT, M_DROP */
	int all;				/* rien d'ignore au collage */
	int move;				/* des elements a deplacer (titre de la bande) */
	int err, erread, cancelled;		/* le resultat (thread) */
	char errname[256];
	char dir[CP_PATH];			/* le dossier de destination */
	struct cp_item it[CP_MAX];
};

/* l'avancement du lot en cours : ecrit par le thread, lu par la boucle */
static struct
{
	char *buf;
	volatile int cancel;
	volatile int phase;			/* 0 compte, 1 copie */
	volatile int closing;			/* fermeture du fichier ecrit en cours */
	volatile unsigned long long total, copied;
	volatile long nfiles, files;		/* fichiers a copier, commences */
	volatile long serial;			/* + 1 a chaque lot pris */
	volatile int del;			/* le lot supprime (rm_tree compte et s'arrete sur Annuler) */
	char cur[256];				/* nom de l'element en cours (job_mx) */
	int err;				/* premiere erreur (errno), 0 : aucune */
	int erread;				/* 1 : en lisant (sinon en ecrivant) */
	char errname[256];
} job;
static pthread_mutex_t job_mx = PTHREAD_MUTEX_INITIALIZER;	/* job.cur, la file */
static struct batch *q_head, *q_tail;	/* en attente */
static struct batch *q_cur;		/* en cours */
static struct batch *q_done;		/* finis, a rapporter */
static int th_run;			/* le thread tourne */

static const char *base_name (const char *p)
{
	const char *b = p;
	for (; *p; p++)
		if (*p == '/' && p[1])
			b = p + 1;
	return b;
}

static void cp_set_cur (const char *p)
{
	pthread_mutex_lock (&job_mx);
	str_copy_n (job.cur, base_name (p), sizeof job.cur);
	pthread_mutex_unlock (&job_mx);
}

/* -> 1 (la copie s'arrete) ; la premiere erreur est gardee */
static int cp_fail (int e, const char *p, int rd)
{
	if (!job.err)
	{
		job.err = e > 0 ? e : EIO;
		job.erread = rd;
		str_copy_n (job.errname, base_name (p), sizeof job.errname);
	}
	return 1;
}

/* chemin d'un element du dossier d (0 : trop long) */
static char *cp_join (char *out, const char *d, const char *name)
{
	if (strlen (d) + strlen (name) + 2 > CP_PATH)
		return 0;
	return path_join (out, d, name), out;
}

static int dot_name (const char *s) { return s[0] == '.' && (!s[1] || (s[1] == '.' && !s[2])); }

/* taille et nombre de fichiers sous p */
static void cp_count (const char *p)
{
	struct stat st;
	if (job.cancel || stat (p, &st))
		return;
	if (!S_ISDIR (st.st_mode))
	{
		job.total += (unsigned long long) st.st_size;
		job.nfiles++;
		return;
	}
	DIR *d = opendir (p);
	if (!d)
		return;
	char q[CP_PATH];
	struct dirent *de;
	while (!job.cancel && (de = readdir (d)))
		if (!dot_name (de->d_name) && cp_join (q, p, de->d_name))
			cp_count (q);
	closedir (d);
}

/* un fichier -> 0, 1 arret (erreur ou Annuler : le fichier ecrit supprime) */
static int cp_file (const char *src, const char *dst)
{
	cp_set_cur (src);
	job.files++;
	int in = open (src, O_RDONLY);
	if (in < 0)
		return cp_fail (errno, src, 1);
	int out = open (dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (out < 0)
	{
		int e = errno;
		close (in);
		return cp_fail (e, dst, 0);
	}
	int r = 0;
	while (!r)
	{
		if (job.cancel)
		{
			r = 1;
			break;
		}
		ssize_t g = read (in, job.buf, CP_BUF);
		if (g < 0)
			r = cp_fail (errno, src, 1);
		if (g <= 0)
			break;
		for (ssize_t o = 0; o < g;)
		{
			ssize_t w = write (out, job.buf + o, (size_t) (g - o));
			if (w <= 0)
			{
				r = cp_fail (w < 0 ? errno : ENOSPC, dst, 0);
				break;
			}
			o += w;
		}
		job.copied += (unsigned long long) g;
	}
	close (in);
	job.closing = 1;			/* (gdrivefs : l'envoi a Google se fait ici) */
	if (close (out) && !r)
		r = cp_fail (errno, dst, 0);
	job.closing = 0;
	if (r)
		unlink (dst);			/* le fichier partiel */
	return r;
}

/* un element (dossier : tout ce qu'il contient) -> 0, 1 arret */
static int cp_tree (const char *src, const char *dst)
{
	struct stat st;
	if (stat (src, &st))
		return cp_fail (errno, src, 1);
	if (!S_ISDIR (st.st_mode))
		return cp_file (src, dst);
	cp_set_cur (src);
	if (mkdir (dst, 0777) && (errno != EEXIST || stat (dst, &st) || !S_ISDIR (st.st_mode)))
		return cp_fail (errno ? errno : EEXIST, dst, 0);
	DIR *d = opendir (src);
	if (!d)
		return cp_fail (errno, src, 1);
	char a[CP_PATH], b[CP_PATH];
	struct dirent *de;
	int r = 0;
	while (!r && (de = readdir (d)))
	{
		if (dot_name (de->d_name))
			continue;
		if (!cp_join (a, src, de->d_name) || !cp_join (b, dst, de->d_name))
			r = cp_fail (ENAMETOOLONG, de->d_name, 1);
		else
			r = cp_tree (a, b);
	}
	closedir (d);
	return r;
}

/* effacer p (dossier : tout ce qu'il contient) -> 0, 1 erreur */
static int rm_tree (const char *p)
{
	int how = job.del ? 2 : 0;		/* (cp_fail : « Impossible de supprimer ») */
	struct stat st;
	if (stat (p, &st))
		return cp_fail (errno, p, how);
	if (S_ISDIR (st.st_mode))
	{
		DIR *d = opendir (p);
		if (!d)
			return cp_fail (errno, p, how);
		char q[CP_PATH];
		struct dirent *de;
		int r = 0;
		while (!r && (de = readdir (d)))
		{
			if (job.del && job.cancel)
				r = 1;		/* Annuler : ce qui est supprime l'est */
			else if (!dot_name (de->d_name) && cp_join (q, p, de->d_name))
				r = rm_tree (q);
		}
		closedir (d);
		if (r)
			return r;
		if (rmdir (p))
			return cp_fail (errno, p, how);
		return 0;
	}
	if (job.del)
	{
		cp_set_cur (p);
		job.files++;
	}
	if (unlink (p))
		return cp_fail (errno, p, how);
	return 0;
}

/* le thread : les lots de la file, l'un apres l'autre ; s'arrete quand elle est vide */
static void *cp_thread (void *a)
{
	(void) a;
	for (;;)
	{
		pthread_mutex_lock (&job_mx);
		struct batch *b = q_head;
		if (!b)
		{
			q_cur = 0;
			th_run = 0;
			pthread_mutex_unlock (&job_mx);
			return 0;
		}
		q_head = b->next;
		if (!q_head)
			q_tail = 0;
		q_cur = b;
		job.cancel = job.phase = job.closing = 0;
		job.total = job.copied = 0;
		job.nfiles = job.files = 0;
		job.err = 0;
		job.cur[0] = 0;
		job.serial++;
		job.del = b->mode == M_DEL;
		pthread_mutex_unlock (&job_mx);
		for (long i = 0; i < b->n; i++)
			cp_count (b->it[i].src);
		job.phase = 1;
		for (long i = 0; i < b->n && !job.cancel; i++)
		{
			if (job.del)
			{
				if (rm_tree (b->it[i].src))
					break;
				continue;
			}
			if (cp_tree (b->it[i].src, b->it[i].dst))
				break;
			if (b->it[i].move && !job.cancel && rm_tree (b->it[i].src))
				break;
		}
		pthread_mutex_lock (&job_mx);
		b->err = job.err;
		b->erread = job.erread;
		str_copy_n (b->errname, job.errname, sizeof b->errname);
		b->cancelled = job.cancel;
		b->next = q_done;
		q_done = b;
		q_cur = 0;
		pthread_mutex_unlock (&job_mx);
	}
}

/* octets avec une decimale : « 12 o », « 3,4 Ko », « 1,2 Go » -> la fin */
static char *fmt_bytes (unsigned long long v, char *d)
{
	static const char *const u[] = { " o", " Ko", " Mo", " Go", " To" };
	int k = 0;
	unsigned long long r = 0;
	while (v >= 1024 && k < 4)
	{
		r = v & 1023;
		v >>= 10;
		k++;
	}
	d = fmt_dec ((unsigned long) v, d);
	if (k && v < 100)
	{
		const char *l = (const char *) ui_lang ();
		*d++ = l && !strcmp (l, "fr") ? ',' : '.';
		d = fmt_dec ((unsigned long) (r * 10 / 1024), d);
	}
	return tr_cat (d, u[k]);
}

/* errno -> texte (traduit) */
static const char *err_text (int e, char *buf)
{
	switch (e)
	{
	case ENOSPC: return TR ("plus de place sur le volume");
	case EACCES:
	case EPERM: return TR ("accès refusé");
	case EROFS: return TR ("volume en lecture seule");
	case ENOENT: return TR ("introuvable");
	case EFBIG: return TR ("fichier trop gros pour ce volume");
	case ENAMETOOLONG: return TR ("nom trop long");
	case EEXIST: return TR ("un élément porte déjà ce nom");
	case EIO: return TR ("erreur de lecture ou d'écriture");
	}
	fmt_dec ((unsigned long) e, tr_cat (buf, "erreur "));
	return buf;
}

/* « nom (copie).ext », « nom (copie 2).ext »... libre dans dir -> 1 */
static int copy_name (char *dst, const char *dir, const char *name, int is_dir)
{
	const char *dot = is_dir ? 0 : strrchr (name, '.');
	if (dot == name)
		dot = 0;
	size_t bl = dot ? (size_t) (dot - name) : strlen (name);
	struct stat st;
	for (long i = 1; i < 100; i++)
	{
		char n[300], *e;
		if (bl > 200)
			return 0;
		memcpy (n, name, bl);
		e = str_cat (n + bl, " (");
		e = str_cat (e, TR ("copie"));
		if (i > 1)
		{
			*e++ = ' ';
			e = fmt_dec ((unsigned long) i, e);
		}
		e = str_cat (e, ")");
		if (dot)
			str_cat (e, dot);
		if (!cp_join (dst, dir, n))
			return 0;
		if (stat (dst, &st))
			return 1;
	}
	return 0;
}

/* ---- la bande de progression (a la place de la ligne d'etat ; band_on, BAND_H : en haut) ---- */
static void text (const char *s, long x, long base, unsigned c, long px, const unsigned char *font, long max);
static long text_w (const char *s, long px, const unsigned char *font);
static long busy_since;				/* debut de l'occupation (ms), 0 : libre */
static long bd_last;				/* derniere mise a jour (ms) */
static long bd_ts, bd_rate, bd_serial = -1, bd_phase;	/* echantillon du debit : ms, o/s, lot, phase */
static unsigned long long bd_cs;		/* octets a l'echantillon */
static char bd_l1[400], bd_l2[400];		/* ses deux lignes */
static long bd_bar;				/* 0..1000 */
static long bd_x, bd_y, bd_w, bd_h;		/* le bouton Annuler */

static void band_text (long npend, int kind, long now)
{
	char nm[256], a[32], z[32], r[48];
	pthread_mutex_lock (&job_mx);
	str_copy_n (nm, job.cur, sizeof nm);
	pthread_mutex_unlock (&job_mx);
	int k;
	if (!job.phase)
		k = snprintf (bd_l1, sizeof bd_l1, TR ("Préparation… %ld fichiers"), (long) job.nfiles);
	else if (job.closing)
		k = snprintf (bd_l1, sizeof bd_l1, TR ("Finalisation de « %s »…"), nm);
	else if (job.nfiles > 1)
		k = snprintf (bd_l1, sizeof bd_l1, TR (kind == 2 ? "Suppression de « %s » (fichier %ld sur %ld)" :
			kind ? "Déplacement de « %s » (fichier %ld sur %ld)" : "Copie de « %s » (fichier %ld sur %ld)"),
			nm, (long) job.files, (long) job.nfiles);
	else
		k = snprintf (bd_l1, sizeof bd_l1, TR (kind == 2 ? "Suppression de « %s »" : kind ? "Déplacement de « %s »" :
			"Copie de « %s »"), nm);
	if (npend > 0 && k > 0 && (size_t) k < sizeof bd_l1 - 8)
	{
		char p[64];
		snprintf (p, sizeof p, TR ("%ld en attente"), npend);
		snprintf (bd_l1 + k, sizeof bd_l1 - (size_t) k, " — %s", p);
	}
	if (kind == 2)
	{					/* supprimer : en fichiers */
		long f = job.files, nf = job.nfiles;
		bd_bar = nf ? f * 1000 / nf : 0;
		if (bd_bar > 1000)
			bd_bar = 1000;
		snprintf (bd_l2, sizeof bd_l2, TR ("%ld fichiers supprimés sur %ld"), f, nf);
		return;
	}
	unsigned long long c = job.copied, tot = job.total;
	bd_bar = tot ? (long) (c * 1000 / tot) : 0;
	if (bd_bar > 1000)
		bd_bar = 1000;
	if (job.serial != bd_serial || job.phase != bd_phase)
	{					/* un autre lot, ou le compte fini : le debit repart */
		bd_serial = job.serial;
		bd_phase = job.phase;
		bd_ts = now;
		bd_cs = c;
		bd_rate = 0;
	}
	else if (now - bd_ts >= 1000)
	{					/* debit : moyenne glissante, un echantillon par seconde */
		long inst = (long) ((c - bd_cs) * 1000 / (unsigned long long) (now - bd_ts));
		bd_rate = bd_rate ? (bd_rate * 2 + inst) / 3 : inst;
		bd_ts = now;
		bd_cs = c;
	}
	fmt_bytes (c, a);
	fmt_bytes (tot, z);
	char *e = bd_l2;
	e += snprintf (e, 120, TR ("%s sur %s"), a, z);
	if (job.phase && bd_rate > 0)
	{
		fmt_bytes ((unsigned long long) bd_rate, r);
		e += snprintf (e, 80, " — %s/s — ", r);
		long s = (long) ((tot > c ? tot - c : 0) / (unsigned long long) bd_rate);
		if (s < 60)
			snprintf (e, 120, "%s", TR ("moins d'une minute restante"));
		else if (s < 3600)
			snprintf (e, 120, TR ("environ %ld min restantes"), (s + 30) / 60);
		else
			snprintf (e, 120, TR ("environ %ld h %02ld restantes"), s / 3600, s / 60 % 60);
	}
}

/* la bande (draw_status, quand band_on) */
static void draw_band (void)
{
	long y = H_ - BAND_H;
	ui_fill (PAN_W, y, W_ - PAN_W, BAND_H, TOK (TJ_SURFACE));
	ui_fill (PAN_W, y, W_ - PAN_W, 1, TOK (TJ_LINE));
	const char *cn = TR ("Annuler");
	bd_w = text_w (cn, 13, ui.font) + 28;
	bd_h = 28;
	bd_x = W_ - 12 - bd_w;
	bd_y = y + (BAND_H - bd_h) / 2;
	long x0 = PAN_W + 12, tw = bd_x - 16 - x0;
	if (tw < 40)
		tw = 40;
	text (bd_l1, x0, y + 20, TOK (TJ_TEXT1), 13, ui.font, tw);
	ui_rbox (x0, y + 29, tw, 6, 3, TOK (TJ_LINE), TOK (TJ_LINE));
	long bw = tw * bd_bar / 1000;
	if (bw > 0)
		ui_rbox (x0, y + 29, bw < 6 ? 6 : bw, 6, 3, TOK (TJ_ACCENT), TOK (TJ_ACCENT));
	text (bd_l2, x0, y + 53, TOK (TJ_TEXT2), 12, ui.font, tw);
	ui_rbox (bd_x, bd_y, bd_w, bd_h, 6, TOK (TJ_RAISED), TOK (TJ_LINE_STRONG));
	text (cn, bd_x + 14, bd_y + 19, TOK (TJ_TEXT1), 13, ui.font, bd_w);
}

/* Annuler : le lot en cours s'arrete, ceux en attente sont retires */
static void cp_cancel (void)
{
	pthread_mutex_lock (&job_mx);
	if (q_cur)
		job.cancel = 1;
	while (q_head)
	{
		struct batch *b = q_head;
		q_head = b->next;
		b->cancelled = 1;
		b->next = q_done;
		q_done = b;
	}
	q_tail = 0;
	pthread_mutex_unlock (&job_mx);
}

/* un point sur le bouton Annuler de la bande ? */
static int band_hit (long x, long y)
{
	return band_on && x >= bd_x && x < bd_x + bd_w && y >= bd_y && y < bd_y + bd_h;
}

static int cp_busy (void)
{
	pthread_mutex_lock (&job_mx);
	int b = q_cur || q_head || th_run;
	pthread_mutex_unlock (&job_mx);
	return b;
}

/* fermer Fichiers : une copie en cours est arretee, apres confirmation -> 1 on peut fermer */
static int cp_stop_for_quit (void)
{
	if (!cp_busy ())
		return 1;
	if (ui_message ((long) "Fichiers", (long) "Une copie est en cours. L'arrêter et fermer Fichiers ?", UM_OKCANCEL) != 1)
		return 0;
	cp_cancel ();
	while (cp_busy ())			/* (le morceau, ou la fermeture, en cours) */
		sys3 (SYS_SLEEP_, 50, 0);
	return 1;
}

/* un lot fini : son erreur, le presse-papiers (deplaces), le dossier relu */
static void cp_report (struct batch *b)
{
	const char *title = b->mode == M_COPY ? "Copier" : b->mode == M_CUT ? "Déplacer" : b->mode == M_DEL ? "Supprimer" : "Déposer";
	char m[700], eb[48];
	if (b->err)
	{
		snprintf (m, sizeof m, TR (b->erread == 2 ? "Impossible de supprimer « %s » : %s." : b->erread ?
			"Impossible de lire « %s » : %s." : "Impossible d'écrire « %s » : %s."),
			b->errname, err_text (b->err, eb));
		ui_message ((long) title, (long) m, UM_OK);
	}
	if (b->mode == M_CUT && b->all && !b->err && !b->cancelled)
		ui_clip_done_cut ();		/* deplaces : le presse-papiers ne les garde plus */
	refresh ();				/* (la selection est gardee) */
	for (long i = 0; i < b->n; i++)		/* les autres fenetres de Fichiers */
		if (b->mode == M_DEL || b->it[i].move)
			chg_add_parent (b->it[i].src);
	chg_add (b->dir);
	chg_publish ();
	if (b->mode == M_DEL)
		set_msg (b->cancelled ? "Suppression annulée" : b->err ? "Suppression interrompue" : "Supprimé");
	else
		set_msg (b->cancelled ? "Copie annulée" : b->err ? "Copie interrompue" : b->move ? "Déplacé" : "Copié");
	free (b);
}

/* la boucle principale, a chaque tour : la bande suit le thread, les lots finis sont rapportes */
static void cp_poll (void)
{
	pthread_mutex_lock (&job_mx);
	struct batch *d = q_done;
	q_done = 0;
	int busy = q_cur || q_head;
	long np = 0;
	for (struct batch *b = q_head; b; b = b->next)
		np++;
	struct batch *kb = q_cur ? q_cur : q_head;
	int move = !kb ? 0 : kb->mode == M_DEL ? 2 : kb->move;
	static long shown = -1;			/* lot dont l'element ecrit est deja dans la liste */
	int here = q_cur && job.serial != shown && job.files > 0 && (job.copied || job.closing)
		&& !strcmp (q_cur->dir, path);
	if (here)
		shown = job.serial;
	pthread_mutex_unlock (&job_mx);
	long now = time_ms ();
	if (!busy)
		busy_since = 0;
	else if (!busy_since)
		busy_since = now;
	int want = busy && now - busy_since >= CP_QUIET_MS, full = 0, part = 0;
	if (here)
	{					/* le premier fichier ecrit ici : la liste le montre */
		refresh ();
		full = 1;
	}
	if (want != band_on)
	{
		band_on = want;
		relayout_all ();
		full = 1;
	}
	if (band_on && (full || now - bd_last >= 250))
	{
		bd_last = now;
		band_text (np, move, now);
		part = 1;
	}
	struct batch *r = 0;			/* (dans l'ordre ou ils ont fini) */
	while (d)
	{
		struct batch *n = d->next;
		d->next = r;
		r = d;
		d = n;
	}
	int fin = r != 0;
	while (r)
	{
		struct batch *n = r->next;
		cp_report (r);
		r = n;
		full = 1;
	}
	if (fin)
		space_ask ();			/* (la place a change) */
	if (full)
		draw ();
	else if (part)
	{
		tk = (const unsigned *) ui_tokens ();
		draw_band ();
		ui_update_all ();
	}
}

/* un lot dans la file ; le thread demarre s'il ne tourne pas -> 0 echec */
static int cp_enqueue (struct batch *b)
{
	b->next = 0;
	pthread_mutex_lock (&job_mx);
	if (q_tail)
		q_tail->next = b;
	else
		q_head = b;
	q_tail = b;
	int start = !th_run;
	if (start)
		th_run = 1;
	pthread_mutex_unlock (&job_mx);
	if (!busy_since)
		busy_since = time_ms ();
	if (!start)
		return 1;
	if (!job.buf)
		job.buf = (char *) malloc (CP_BUF);	/* (garde ensuite) */
	pthread_t th;
	pthread_attr_t at;
	pthread_attr_init (&at);
	pthread_attr_setstacksize (&at, 256 * 1024);
	pthread_attr_setdetachstate (&at, PTHREAD_CREATE_DETACHED);
	int bad = !job.buf || pthread_create (&th, &at, cp_thread, 0);
	pthread_attr_destroy (&at);
	if (!bad)
		return 1;
	pthread_mutex_lock (&job_mx);		/* (seul dans la file : le thread ne tournait pas) */
	q_head = q_tail = 0;
	th_run = 0;
	pthread_mutex_unlock (&job_mx);
	return 0;
}

/* supprimer le dossier p et tout ce qu'il contient : un lot (delete_sel) -> 0 echec */
static int del_enqueue (const char *p)
{
	struct batch *b = (struct batch *) calloc (1, sizeof *b);
	if (!b)
		return 0;
	b->mode = M_DEL;
	b->all = 1;
	b->n = 1;
	str_copy_n (b->it[0].src, p, CP_PATH);
	str_copy_n (b->dir, path, CP_PATH);
	if (cp_enqueue (b))
		return 1;
	free (b);
	return 0;
}

/* les chemins de list (un par ligne) dans dir : M_COPY, M_CUT, M_DROP (rename() si c'est possible,
 * sinon une copie, en arriere-plan : un lot dans la file) -> 1 si tout est deja fait (sans lot,
 * rien d'ignore) */
/* un depot : une copie, un deplacement si Maj etait tenue (decision du 18/11 : plus de « meme volume :
 * deplacer ») */
static int drop_move;

static int paste_list (char *list, int mode, const char *dir)
{
	const char *title = mode == M_COPY ? "Copier" : mode == M_CUT ? "Déplacer" : "Déposer";
	char msgb[700], first[256];
	long moved = 0;
	int all = 1;
	first[0] = 0;
	struct batch *b = (struct batch *) calloc (1, sizeof *b);
	if (!b)
	{
		set_err (-ENOMEM);
		return 0;
	}
	b->mode = mode;
	str_copy_n (b->dir, dir, CP_PATH);
	for (char *s = list; *s;)
	{
		char *e = s;
		while (*e && *e != '\n' && *e != '\r')
			e++;
		char c = *e;
		*e = 0;
		char *src = s;
		s = c ? e + 1 : e;
		if (!*src)
			continue;
		size_t sl = strlen (src);
		while (sl > 1 && src[sl - 1] == '/')
			src[--sl] = 0;
		struct stat ss, ds;
		if (sl >= CP_PATH || stat (src, &ss))
		{
			snprintf (msgb, sizeof msgb, TR ("« %s » est introuvable."), base_name (src));
			ui_message ((long) title, (long) msgb, UM_OK);
			all = 0;
			continue;
		}
		const char *name = base_name (src);
		char dst[CP_PATH];
		if (!cp_join (dst, dir, name))
		{
			all = 0;
			continue;
		}
		int isd = S_ISDIR (ss.st_mode);
		if (!strcmp (dst, src))
		{				/* deja dans ce dossier */
			if (mode != M_COPY)
				continue;
			if (!copy_name (dst, dir, name, isd))
			{
				all = 0;
				continue;
			}
		}
		else if (isd && !strncmp (dst, src, sl) && dst[sl] == '/')
		{
			all = 0;
			if (mode == M_DROP)
				continue;	/* (depose sur lui-meme : rien) */
			snprintf (msgb, sizeof msgb, TR ("« %s » ne peut pas aller dans lui-même."), name);
			ui_message ((long) title, (long) msgb, UM_OK);
			continue;
		}
		else if (stat (dst, &ds) == 0)
		{
			if (S_ISDIR (ds.st_mode) != isd)
			{
				snprintf (msgb, sizeof msgb, TR ("Un élément nommé « %s » existe déjà dans ce dossier."), name);
				ui_message ((long) title, (long) msgb, UM_OK);
				all = 0;
				continue;
			}
			snprintf (msgb, sizeof msgb, TR (isd ? "Le dossier « %s » existe déjà ici. Y mettre ce qu'il contient ?"
				: "« %s » existe déjà dans ce dossier. Le remplacer ?"), name);
			long a = ui_message ((long) title, (long) msgb, UM_YESNOCANCEL);
			if (a == 0)
			{			/* Annuler : rien de plus */
				b->n = 0;
				all = 0;
				break;
			}
			if (a != 1)
			{
				all = 0;	/* Non : celui-ci ignore */
				continue;
			}
		}
		if (!first[0])
			str_copy_n (first, base_name (dst), sizeof first);
		int mv = mode == M_CUT || (mode == M_DROP && drop_move);
		if (mv && rename (src, dst) == 0)
		{
			moved++;		/* meme volume : sans copie */
			chg_add_parent (src);
			continue;
		}
		if (b->n >= CP_MAX)
		{
			all = 0;
			break;
		}
		struct cp_item *it = &b->it[b->n++];
		str_copy_n (it->src, src, CP_PATH);
		str_copy_n (it->dst, dst, CP_PATH);
		it->move = mv;
		b->move = b->move || mv;
	}
	long queued = b->n;
	b->all = all;
	if (queued && !cp_enqueue (b))
	{
		free (b);
		set_err (-ENOMEM);
		return 0;
	}
	if (!queued)
		free (b);
	if (strcmp (dir, path))
		refresh ();			/* (dans un sous-dossier : la source a pu partir d'ici) */
	else
	{
		load_dir ();
		select_name (first);
	}
	if (moved)
	{				/* (les lots : cp_report) */
		chg_add (dir);
		chg_publish ();
	}
	if (queued)
		msg = 0;			/* (la bande de progression le dit) */
	else if (moved)
		set_msg ("Déplacé");
	return all && !queued;			/* (un lot : cp_report le dira) */
}

/* les chemins complets de la selection, un par ligne, dans l'ordre affiche -> leur nombre (0 : rien de choisi,
 * ou trop long) */
static long sel_list (char *b, long cap)
{
	long n = 0, k = 0;
	for (long i = 0; i < nents; i++)
	{
		if (!is_sel (i))
			continue;
		make_full (i);
		long l = (long) strlen (fullpath);
		if (k + l + 2 > cap)
			return 0;
		memcpy (b + k, fullpath, (size_t) l);
		k += l;
		b[k++] = '\n';
		n++;
	}
	b[k] = 0;
	return n;
}

/* Copier, Couper : les chemins de la selection dans le presse-papiers */
static void clip_sel (int cut)
{
	static char b[16384];
	long n = sel_list (b, sizeof b);
	if (!n)
		return;
	if (!ui_clip_set_files ((long) b, cut))
		set_msg ("Presse-papiers indisponible");
	else
		set_msg (cut ? "Coupé : collez-le dans un autre dossier" : "Copié dans le presse-papiers");
}

/* Copier, Deplacer vers l'autre vue : la selection dans le dossier de l'autre vue, sans question (sauf un nom
 * deja pris), comme Coller */
static void to_other (int move)
{
	static char b[16384];
	if (is_recent_p (panes[!act].path))
	{
		set_msg ("Impossible dans Récents");
		return;
	}
	if (!dual || !sel_list (b, sizeof b))
		return;
	char dir[CP_PATH];
	str_copy_n (dir, panes[!act].path, sizeof dir);
	paste_list (b, move ? M_CUT : M_COPY, dir);
	other_stale = 1;
}

/* Coller : dans le dossier courant */
static void paste (void)
{
	if (is_recent ())
	{
		set_msg ("Impossible dans Récents");
		return;
	}
	static char b[8192];
	long cut = 0, n = uikit_call3 ((long (*) ()) ui_clip_get_files, (long) b, sizeof b, 0, 0, &cut, 0);
	if (n <= 0)
	{
		set_msg ("Le presse-papiers ne contient aucun fichier");
		return;
	}
	b[n < (long) sizeof b ? n : (long) sizeof b - 1] = 0;
	if (paste_list (b, cut ? M_CUT : M_COPY, path) && cut)
		ui_clip_done_cut ();		/* deplaces : le presse-papiers ne les garde plus */
}

/* le dossier dir parmi les acces rapides -> son rang, sinon -1 */
static long place_find (const char *dir)
{
	for (long i = 0; i < pl_n; i++)
		if (!strcmp (places[i].path, dir))
			return i;
	return -1;
}

/* epingler (pin = 1) ou retirer (0) le dossier dir des acces rapides : places.ini (personnel, sinon celui de /etc
 * comme depart) relu tel quel, une ligne « nom = dossier » ajoutee a la fin ou celles de ce dossier retirees
 * (commentaires et autres lignes gardes), ecrit dans le places.ini personnel ; puis menu Aller et panneau refaits */
static void place_pin (const char *dir, int pin)
{
	static char raw[PL_BUF], out[PL_BUF + 200];
	char cp_[160];
	if (pin && (pl_n >= PL_MAX || strlen (dir) >= sizeof places[0].path))
	{
		ui_message ((long) TR ("Accès rapides"), (long) TR (pl_n >= PL_MAX ? "Les accès rapides sont pleins : retirez-en un d'abord."
				: "Ce chemin est trop long pour les accès rapides."), UM_OK);
		return;
	}
	long n = 0;
	int fd = open (korin_conf_path ("places.ini", cp_, sizeof cp_, 0), O_RDONLY);
	if (fd >= 0)
	{
		n = read (fd, raw, PL_BUF - 1);
		close (fd);
		if (n < 0)
			n = 0;
	}
	raw[n] = 0;
	char *o = out;
	for (char *line = raw; *line;)
	{
		char *e = line;
		while (*e && *e != 10)
			e++;
		long len = e - line;
		int keep = 1;
		if (!pin)
		{				/* « nom = dossier » de ce dossier : retiree */
			char tmp[300];
			long l = len < (long) sizeof tmp - 1 ? len : (long) sizeof tmp - 1;
			memcpy (tmp, line, (size_t) l);
			tmp[l] = 0;
			char *q = strchr (tmp, '=');
			if (tmp[0] != '#' && q && !strcmp (ini_trim (q + 1), dir))
				keep = 0;
		}
		if (keep)
		{
			memcpy (o, line, (size_t) len);
			o += len;
			*o++ = 10;
		}
		line = *e ? e + 1 : e;
	}
	if (pin)
	{
		const char *nm = base_name (dir);
		o += snprintf (o, 200, "%s = %s\n", *nm ? nm : dir, dir);
	}
	fd = open (korin_conf_path ("places.ini", cp_, sizeof cp_, 1), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		ui_message ((long) TR ("Accès rapides"), (long) TR ("Les accès rapides ne peuvent pas être enregistrés."), UM_OK);
		return;
	}
	write (fd, out, (size_t) (o - out));
	close (fd);
	places_load ();
	menu_build ();
	ui_menu ((long) menu_buf);
	menu_states ();
	draw ();
}

/* point de la fenetre : menu de l'entree selectionnee (Ouvrir, Ouvrir avec... pour un fichier,
 * Couper, Copier, Renommer..., Supprimer) ; dans le vide : Coller, Nouveau dossier */
static void ctx_menu (long x, long y)
{
	enum { A_OPEN, A_WITH, A_CUT, A_COPY, A_RENAME, A_DELETE, A_PASTE, A_NEWDIR, A_COPYTO, A_MOVETO, A_PIN, A_LOCATE,
		A_FORGET };
	static const char *const lab[] = { "Ouvrir", "Ouvrir avec…", "Couper", "Copier", "Renommer…", "Supprimer",
		"Coller", "Nouveau dossier", "Copier vers « %s »", "Déplacer vers « %s »", "Épingler aux accès rapides",
		"Ouvrir l'emplacement", "Retirer des récents" };
	int act[14], n = 0;
	if (is_recent ())
	{					/* Recents : ouvrir, copier, son dossier, retirer de la liste */
		if (cursel < 0 || !nsel)
			return;
		act[n++] = A_OPEN;
		if (nsel == 1)
			act[n++] = A_WITH;
		act[n++] = A_COPY;
		if (nsel == 1)
			act[n++] = A_LOCATE;
		act[n++] = A_FORGET;
	}
	else if (cursel < 0 || !nsel)
	{
		act[n++] = A_PASTE;
		act[n++] = A_NEWDIR;
	}
	else
	{
		act[n++] = A_OPEN;
		if (nsel == 1 && ent_ptr (cursel)->kind == K_FILE)
			act[n++] = A_WITH;
		act[n++] = A_CUT;
		act[n++] = A_COPY;
		if (dual)
		{				/* (deux vues : vers le dossier de l'autre) */
			act[n++] = A_COPYTO;
			act[n++] = A_MOVETO;
		}
		if (nsel == 1)
			act[n++] = A_RENAME;
		act[n++] = A_DELETE;
		if (nsel == 1 && ent_ptr (cursel)->kind == K_DIR && place_find ((make_full (cursel), fullpath)) < 0)
			act[n++] = A_PIN;	/* (un dossier pas encore epingle ; Retirer : clic droit dans le panneau) */
	}
	const char *other = base_name (panes[!act_pane ()].path);
	char *e = popbuf;
	for (int i = 0; i < n; i++)
	{
		if (act[i] == A_COPYTO || act[i] == A_MOVETO)
		{
			e += snprintf (e, 120, TR (lab[act[i]]), *other ? other : "/");
			e++;
		}
		else
			e = tr_cat (e, lab[act[i]]) + 1;
	}
	*e = 0;
	long r = ui_popup_local (x, y, (long) popbuf, 260);
	if (r < 0 || r >= n)
		return;				/* Echap, clic ailleurs */
	switch (act[r])
	{
	case A_COPYTO: to_other (0); break;
	case A_MOVETO: to_other (1); break;
	case A_OPEN: open_sel (); break;
	case A_WITH: open_with_sel (); break;
	case A_CUT: clip_sel (1); break;
	case A_COPY: clip_sel (0); break;
	case A_RENAME: rename_sel (); break;
	case A_DELETE: delete_sel (); break;
	case A_PASTE: paste (); break;
	case A_NEWDIR: new_folder (); break;
	case A_PIN: place_pin ((make_full (cursel), fullpath), 1); break;
	case A_LOCATE:
	{					/* son dossier, lui choisi */
		char f[600];
		str_copy_n (f, (make_full (cursel), fullpath), sizeof f);
		str_copy_n (fullpath, f, sizeof fullpath);
		path_parent (fullpath);
		go_dir (fullpath);
		select_name (base_name (f));
		break;
	}
	case A_FORGET: recent_forget (); break;
	}
}

/* Recents : la selection retiree de la liste (les fichiers restent) */
static void recent_forget (void)
{
	if (!is_recent () || !nsel)
		return;
	char cp_[160];
	static char raw[16384], out[16384];
	int fd = open (korin_conf_path (RECENT_FILE, cp_, sizeof cp_, 0), O_RDONLY);
	long n = fd >= 0 ? read (fd, raw, sizeof raw - 1) : 0;
	if (fd >= 0)
		close (fd);
	if (n < 0)
		n = 0;
	raw[n] = 0;
	long o = 0;
	for (char *l = raw; *l;)
	{
		char *e = strchr (l, 10);
		if (e)
			*e = 0;
		int drop = 0;
		for (long i = 0; i < nents && !drop; i++)
			if (is_sel (i) && !strcmp (ent_ptr (i)->name, l))
				drop = 1;
		if (*l && !drop && o + (long) strlen (l) + 2 < (long) sizeof out)
			o += snprintf (out + o, sizeof out - (size_t) o, "%s\n", l);
		if (!e)
			break;
		l = e + 1;
	}
	fd = open (korin_conf_path (RECENT_FILE, cp_, sizeof cp_, 1), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0)
	{
		write (fd, out, (size_t) o);
		close (fd);
	}
	load_dir ();
	set_msg ("Retiré des récents");
}

/* actualiser (dossier et volumes ; la selection est gardee) */
static void refresh (void)
{
	tmpname[0] = 0;
	if (cursel >= 0)
		str_copy_n (tmpname, ent_ptr (cursel)->name, 256);
	load_dir ();
	select_name (tmpname);
	vraw_n = -1;
	vol_poll ();
}

/* ---------------------------------------------------------------------------
 *  Affichage
 * --------------------------------------------------------------------------- */

static void text (const char *s, long x, long base, unsigned c, long px, const unsigned char *font, long max)
{
	ui_text ((long) s, x, base, c, px, (long) font, max);
}

static long text_w (const char *s, long px, const unsigned char *font) { return ui_text_width ((long) s, px, (long) font); }

/* les dossiers a la maniere d'Adwaita (24/11/2026) : deux masques de tools/icons (glyph-folder-back : le fond et son
 * onglet ; glyph-folder-front : la face, qui redescend a gauche sous l'onglet), colories d'apres l'accent du theme ;
 * un dossier type du dossier personnel porte en plus son glyphe (place_glyph) en embleme */

/* c eclairci vers le blanc de t/256 */
static unsigned tint (unsigned c, unsigned t)
{
	unsigned r = 0;
	for (int sh = 0; sh < 24; sh += 8)
	{
		unsigned v = (c >> sh) & 255;
		r |= (v + (255 - v) * t / 256) << sh;
	}
	return r;
}

static unsigned folder_back (void) { return tint (TOK (TJ_ACCENT), 31); }	/* le fond, l'embleme */

/* dossier de cote side (20, 48) ; edge : son epaisseur sous la face (un trait plus fonce) */
static void draw_folder_s (long x, long y, long side, int edge)
{
	unsigned a = TOK (TJ_ACCENT);
	ui_glyph ((long) "glyph-folder-back", x, y, side, folder_back ());
	if (edge)
		ui_glyph ((long) "glyph-folder-front", x, y + 1, side, tint (a, 77));
	ui_glyph ((long) "glyph-folder-front", x, y, side, tint (a, 148));
}

/* petit dossier (20 x 20) */
static void draw_folder (long x, long y) { draw_folder_s (x, y, 20, 0); }

/* page (14 x 18) */
static void draw_doc (long x, long y)
{
	ui_fill (x, y, 14, 18, TOK (TJ_RAISED));
	ui_frame (x, y, 14, 18, TOK (TJ_LINE_STRONG));
	for (long i = 3; i < 6; i++)		/* lignes de texte */
		ui_fill (x + 3, y + i * 3, 8, 1, TOK (TJ_LINE_STRONG));
}

/* x du milieu, haut : symbole d'ejection */
static void draw_eject (long x, long y)
{
	for (long i = 0; i < 5; i++)
		ui_fill (x - i, y + i, 2 * i + 1, 1, TOK (TJ_TEXT2));
	ui_fill (x - 4, y + 7, 9, 2, TOK (TJ_TEXT2));
}

/* fond de selection */
static void draw_sel (long x, long y, long w, long h)
{
	unsigned c = pane_dim ? TOK (TJ_SUNKEN) : TOK (TJ_ACCENT_LIGHT);	/* (vue inactive : « en veille ») */
	ui_rbox (x, y, w, h, 8, c, c);
}

/* -> element du panneau du dossier courant : 0x200 + n (acces rapide exact), n (volume qui le
 * contient), -1 */
static long cur_hl (void)
{
	for (long i = 0; i < pl_n; i++)
		if (same_dir (path, places[i].path))
			return 0x200 + i;
	return cur_vol ();
}

static char *fmt_bytes (unsigned long long v, char *d);

/* jauge de la place prise : rail LIGNE, remplie d'ACCENT (DANGER au-dela de 90 %) */
static void draw_gauge (long x, long y, long w, long h, const struct vspace *sp)
{
	ui_rbox (x, y, w, h, h >> 1, TOK (TJ_LINE), TOK (TJ_LINE));
	unsigned long long t = sp->total, u = t > sp->avail ? t - sp->avail : 0;
	long f = t ? (long) (u * (unsigned long long) w / t) : 0;
	if (u && f < h)
		f = h;				/* (un rien pris se voit) */
	unsigned c = u * 10 >= t * 9 ? TOK (TJ_DANGER) : TOK (TJ_ACCENT);
	if (f > 0)
		ui_rbox (x, y, f, h, h >> 1, c, c);
}

/* « 12,3 Go libres sur 29,7 Go » (« 12,3 Go libres » si max est depasse) -> buf */
static const char *space_text (const struct vspace *sp, char *buf, long px, long max)
{
	char *e = tr_cat (fmt_bytes (sp->avail, buf), " libres sur ");
	fmt_bytes (sp->total, e);
	if (max <= 0 || text_w (buf, px, ui.font) <= max)
		return buf;
	tr_cat (fmt_bytes (sp->avail, buf), " libres");
	return buf;
}

/* haut de la premiere ligne de volume (sous les acces rapides) */
static long vol_y0 (void) { return pl_n ? PAN_Y0 + pl_n * PROW_H + PL_GAP : PAN_Y0; }

/* le panneau de gauche defile quand ses lignes ne tiennent pas (23/11/2026) : pan_scroll pixels, la molette sur lui */
/* les volumes montres dans le panneau -> leur nombre, leurs rangs dans ix : les amovibles et les distants (24/11/2026,
 * comme GNOME) ; Affichage > Tous les volumes ajoute « / » seul, nomme « Systeme » (les autres, /tmp, /home..., sont
 * dans son arborescence) */
static int is_net_src (const char *s);
static int vol_is_root (const struct vol *v) { return !strcmp (v->low, "root") || !strcmp (v->disp, "/"); }
static long vol_shown (long *ix)
{
	long n = 0;
	for (long i = 0; i < vol_n; i++)
		if (vols[i].rem || is_net_src (vols[i].src) || (show_allvol && vol_is_root (&vols[i])))
			ix[n++] = i;
	return n;
}

static long pan_scroll;
static long ptr_x;				/* x du pointeur (le dernier evenement de la souris) : la molette va au panneau */
#define PAN_CLIP_Y	8			/* (en haut, le contenu defile sous rien : le panneau commence la) */

/* -> le plus grand defilement du panneau (0 : tout tient) */
static long pan_max (void)
{
	long ix[VMAX];
	long h = vol_y0 () + vol_shown (ix) * VROW_H + 8 - H_;
	return h > 0 ? h : 0;
}

static void pan_clamp (void)
{
	long m = pan_max ();
	if (pan_scroll > m)
		pan_scroll = m;
	if (pan_scroll < 0)
		pan_scroll = 0;
}

static void draw_panel (void)
{
	ui_fill (0, 0, PAN_W - 1, H_, TOK (TJ_SUNKEN));
	ui_fill (PAN_W - 1, 0, 1, H_, TOK (TJ_LINE));
	pan_clamp ();
	ui_clip (0, PAN_CLIP_Y, PAN_W - 1, H_ - PAN_CLIP_Y);	/* (les lignes defilent dans le panneau) */
	long hl = cur_hl (), y0 = PAN_Y0 - pan_scroll;
	if (pl_n)
	{					/* acces rapides */
		text (TR ("Accès rapides"), 14, y0 - 10, TOK (TJ_TEXT2), 12, ui.font_b, 0);
		for (long i = 0; i < pl_n; i++)
		{
			long y = i * PROW_H + y0;
			if (hl == 0x200 + i)
				draw_sel (6, y + 1, PAN_W - 13, PROW_H - 2);
			ui_glyph ((long) place_glyph (places[i].path), 14, y + 4, 20, TOK (TJ_TEXT2));
			text (places[i].lab, 42, y + 19, TOK (TJ_TEXT1), 13, ui.font, PAN_W - 50);
		}
		y0 = vol_y0 () - pan_scroll;
	}
	long vix[VMAX], vn = vol_shown (vix);
	if (vn)
		text (TR ("Volumes"), 14, y0 - 10, TOK (TJ_TEXT2), 12, ui.font_b, 0);
	for (long k = 0; k < vn; k++)
	{
		long i = vix[k];
		long y = k * VROW_H + y0;	/* haut de la ligne */
		if (y + VROW_H <= PAN_CLIP_Y)
			continue;
		if (y >= H_)
			break;
		struct vol *v = &vols[i];
		if (hl == i)
			draw_sel (6, y + 1, PAN_W - 13, VROW_H - 2);
		if (is_net_src (v->src))
			ui_glyph ((long) "glyph-tb-network", 14, y + 8, 20, TOK (TJ_TEXT2));	/* un volume du reseau */
		else if (vol_is_root (v))
			ui_glyph ((long) "glyph-place-drive", 14, y + 8, 20, TOK (TJ_TEXT2));	/* le Systeme : un disque */
		else
		{
			ui_rbox (14, y + 10, 20, 15, 3, TOK (TJ_TEXT2), TOK (TJ_TEXT2));	/* disque */
			ui_fill (28, y + 19, 3, 3, TOK (TJ_SUNKEN));
		}
		char tb[96];
		const char *title = vol_title (v, tb, sizeof tb);
		text (title, 42, y + 16, TOK (TJ_TEXT1), 13, ui.font_b, PAN_W - 42 - 36);	/* « /media/usb0 », « Google Drive » */
		const struct vspace *sp = space_of (v);
		if (sp)
		{				/* sa place, et la jauge */
			char sb[96];
			long tw = PAN_W - 42 - 14;
			text (space_text (sp, sb, 11, tw), 42, y + 31, TOK (TJ_TEXT2), 11, ui.font, tw);
			draw_gauge (42, y + 36, tw, 5, sp);
		}
		else				/* le genre ; un volume nomme : son chemin (« Google Drive » dit deja son genre) */
			text (title != v->disp ? v->disp : vol_kind (v), 42, y + 31, TOK (TJ_TEXT2), 12, ui.font, PAN_W - 42 - 36);
		if (v->rem)
			draw_eject (PAN_W - 22, y + 13);
	}
	ui_clip (0, 0, 0, 0);
	long m = pan_max ();
	if (m > 0)
	{					/* la place dans le panneau : une barre fine, a droite */
		long track = H_ - PAN_CLIP_Y - 8, th = track * H_ / (H_ + m);
		if (th < 24)
			th = 24;
		long ty = PAN_CLIP_Y + 4 + (track - th) * pan_scroll / m;
		ui_rbox (PAN_W - 6, ty, 3, th, 1, TOK (TJ_LINE_STRONG), TOK (TJ_LINE_STRONG));
	}
}

/* H_BACK, H_NEXT, H_UP -> 1 si le bouton sert (0 : grise) */
static long nav_on (long h)
{
	if (h == H_BACK)
		return h_pos > 0;
	if (h == H_NEXT)
		return h_pos + 1 < h_n;
	return (unsigned long) cr_n > 1;
}

/* H_BACK, H_NEXT, H_UP : bouton fantome */
static void draw_nav (long h)
{
	long x = (h - H_BACK) * NAV_W + NAV_X;
	unsigned ink = TOK (TJ_TEXT3);
	if (nav_on (h))
	{
		ink = TOK (TJ_TEXT1);
		if (hhot == h)
			ui_rbox (x, NAV_Y, NAV_W, NAV_H, 8, TOK (TJ_SUNKEN), TOK (TJ_SUNKEN));
	}
	const char *s = h == H_BACK ? "←" : h == H_NEXT ? "→" : "↑";
	text (s, x + ((NAV_W - text_w (s, NAV_PX, ui.font)) >> 1), NAV_Y + 22, ink, NAV_PX, ui.font, 0);
}

/* segment (0 icones, 1 liste) -> encre : TEXTE1 choisi ou survole */
static unsigned seg_ink (long s)
{
	return view == s || hhot - H_ICONS == s ? TOK (TJ_TEXT1) : TOK (TJ_TEXT2);
}

/* controle segmente icones / liste (rail CREUX, comme ui_segmented ; segment choisi ELEVE sur une
 * ombre LIGNE) */
static void draw_seg (void)
{
	long x0 = W_ - SEG_X_R;
	ui_rbox (x0, SEG_Y, SEG_RAIL, SEG_H, 8, TOK (TJ_SUNKEN), TOK (TJ_SUNKEN));
	long x = view * SEG_W + x0 + 2;
	ui_rbox (x, SEG_Y + 2, SEG_W, SEG_H - 3, 6, TOK (TJ_LINE), TOK (TJ_LINE));
	ui_rbox (x, SEG_Y + 2, SEG_W, SEG_H - 4, 6, TOK (TJ_RAISED), TOK (TJ_RAISED));
	unsigned ink = seg_ink (0);		/* icones : quatre carres */
	for (long i = 0; i < 4; i++)
		ui_rbox ((i & 1) * 8 + x0 + 2 + (SEG_W - 14) / 2, (i >> 1) * 8 + SEG_Y + 7, 6, 6, 1, ink, ink);
	ink = seg_ink (1);			/* liste : trois lignes pointees */
	x = x0 + 2 + SEG_W + (SEG_W - 14) / 2;
	for (long i = 0, y = SEG_Y + 8; i < 3; i++, y += 5)
	{
		ui_fill (x, y, 2, 2, ink);
		ui_fill (x + 4, y, 10, 2, ink);
	}
}

/* bouton Deux vues : fantome comme les fleches ; enfonce en deux vues (fond ACCENT_CLAIR, glyphe ACCENT) ;
 * glyphe : deux colonnes */
static void draw_dual (void)
{
	long x = W_ - DUAL_X_R;
	if (dual)
		ui_rbox (x, NAV_Y, NAV_W, NAV_H, 8, TOK (TJ_ACCENT_LIGHT), TOK (TJ_ACCENT_LIGHT));
	else if (hhot == H_DUAL)
		ui_rbox (x, NAV_Y, NAV_W, NAV_H, 8, TOK (TJ_SUNKEN), TOK (TJ_SUNKEN));
	unsigned ink = dual ? TOK (TJ_ACCENT) : hhot == H_DUAL ? TOK (TJ_TEXT1) : TOK (TJ_TEXT2);
	long gx = x + 8, gy = NAV_Y + 9;
	for (long c = 0; c < 2; c++)
	{
		ui_frame (gx + c * 9, gy, 7, 14, ink);
		ui_fill (gx + c * 9, gy, 7, 3, ink);
	}
}

/* path -> segments du fil d'Ariane (crumbs, cr_n ; noms dans crbuf). Trop profond : le dernier
 * segment reste le vrai */
static void crumbs_make (void)
{
	if (is_recent ())
	{					/* (Recents : un seul segment, son nom) */
		crumbs[0].txt = TR ("Récents");
		crumbs[0].end = 0;
		cr_n = 1;
		return;
	}
	str_copy_n (crbuf, path, 272);
	char *start = crbuf;
	long n = 0;
	if (crbuf[0] == '/')			/* la racine : un segment « / » */
	{
		static char root[] = "/";
		crumbs[0].txt = root;
		crumbs[0].end = 0;
		n = 1;
	}
	for (char *p = crbuf;; p++)
	{
		char c = *p;
		if (c && c != '/')
			continue;
		*p = 0;
		if (p != start)
		{
			if (n >= CR_MAX)
				n = CR_MAX - 1;
			crumbs[n].txt = start;
			crumbs[n].end = p - crbuf;
			n++;
		}
		start = p + 1;
		if (!c)
			break;
	}
	cr_n = n;
}

/* rang du segment -> encre : TEXTE2, TEXTE1 au survol */
static unsigned crumb_ink (long i) { return hhot - H_CRUMB == i ? TOK (TJ_TEXT1) : TOK (TJ_TEXT2); }

/* fil d'Ariane : dernier segment gras ; trop long, on coupe a gauche, le « … » mene au segment
 * qu'il remplace (x, w pour head_at) */
static void draw_crumbs (void)
{
	if (!cr_n)
		return;
	long last = cr_n - 1, x = CR_X0, xr = W_ - CR_R;
	long ell_w = text_w ("…", CR_PX, ui.font);
	long step = text_w ("›", CR_PX, ui.font) + 2 * CR_GAP;
	for (long i = 0; i <= last; i++)
	{
		crumbs[i].w = text_w (crumbs[i].txt, CR_PX, i == last ? ui.font_b : ui.font);
		crumbs[i].x = -1;
	}
	if (xr <= x)
		return;
	long acc = crumbs[last].w, first = last;	/* depuis la fin, ce qui tient */
	for (long i = last; i > 0;)
	{
		i--;
		long t = crumbs[i].w + acc + step, need = t;
		if (i)
			need += ell_w + step;	/* (reste a gauche : la place du « … ») */
		if (need + x > xr)
			break;
		acc = t;
		first = i;
	}
	if (first)
	{
		struct crumb *c = &crumbs[first - 1];
		c->x = x;
		c->w = ell_w;
		text ("…", x, CR_BASE, crumb_ink (first - 1), CR_PX, ui.font, 0);
		x += ell_w;
		text ("›", x + CR_GAP, CR_BASE, TOK (TJ_TEXT3), CR_PX, ui.font, 0);
		x += step;
	}
	for (long i = first;; i++)
	{
		struct crumb *c = &crumbs[i];
		c->x = x;
		if (i == last)
		{
			if (xr - x >= 8)
				ui_text_fit ((long) c->txt, x, CR_BASE, TOK (TJ_TEXT1), CR_PX, (long) ui.font_b, xr - x);
			return;
		}
		text (c->txt, x, CR_BASE, crumb_ink (i), CR_PX, ui.font, 0);
		x += c->w;
		text ("›", x + CR_GAP, CR_BASE, TOK (TJ_TEXT3), CR_PX, ui.font, 0);
		x += step;
	}
}

/* en-tete : precedent, suivant, parent, fil d'Ariane, choix de la vue ; rien dans les FRAME_CAP_W
 * de droite (boutons de la fenetre) */
/* le champ d'emplacement (24/11/2026) : un ui_textbox par-dessus le fil d'Ariane, ouvert par Ctrl+L, un clic dans le
 * fil d'Ariane, ou « / » et « ~ » tapes dans la vue ; Entree y va, Echap le ferme, Tab complete le dernier nom */
static long loc_w;				/* le champ (0 : pas encore cree) */
static int loc_on;				/* montre */
static void loc_open (const char *init);

static void loc_place (void)
{
	ui_w_move (loc_w, CR_X0 - 8, NAV_Y, W_ - CR_R - CR_X0 + 16, NAV_H);
}

static void draw_top (void)
{
	ui_fill (PAN_W, 0, W_ - PAN_W, TOP_H, TOK (TJ_SURFACE));
	ui_fill (PAN_W, TOP_H, W_ - PAN_W, 1, TOK (TJ_LINE));
	ui_fill (PAN_W, TOP_H + 1, W_ - PAN_W, LIST_Y - TOP_H - 1, TOK (TJ_SURFACE));
	crumbs_make ();
	for (long h = H_BACK; h <= H_UP; h++)
		draw_nav (h);
	draw_dual ();
	draw_seg ();
	if (loc_on)
		loc_place ();			/* (le champ est dessine a la fin de draw, par-dessus) */
	else
		draw_crumbs ();
}

/* x, y -> element de l'en-tete sous ce point (H_*, 0 : aucun ; le dernier segment n'en est pas un,
 * on y est deja) */
static long head_at (long x, long y)
{
	if (y < NAV_Y || y >= NAV_Y + NAV_H || x < NAV_X)
		return 0;
	long i = (x - NAV_X) / NAV_W;
	if (i < 3)
		return i + H_BACK;
	long dx = x - (ui.w - DUAL_X_R);
	if (dx >= 0 && dx < NAV_W)
		return H_DUAL;
	long s = x - (ui.w - SEG_X_R);
	if (s >= 0)
	{
		if (s >= SEG_RAIL)
			return 0;
		return s >= 2 + SEG_W ? H_LIST : H_ICONS;
	}
	for (long c = 0; c + 1 < cr_n; c++)
	{
		if (crumbs[c].x < 0)
			continue;
		long d = x - crumbs[c].x + CR_GAP;
		if (d >= 0 && d < crumbs[c].w + 2 * CR_GAP)
			return c + H_CRUMB;
	}
	return 0;
}

/* rang du segment : aller a ce dossier */
static void crumb_go (long i)
{
	long n = crumbs[i].end;
	memcpy (fullpath, path, (size_t) n);
	if (!n)					/* « / » : la racine */
		fullpath[n++] = '/';
	fullpath[n] = 0;
	go_dir (fullpath);
}

/* element de l'en-tete : son action */
static void head_click (long h)
{
	if (h == H_BACK)
		hist_go (-1);
	else if (h == H_NEXT)
		hist_go (1);
	else if (h == H_UP)
		go_parent ();
	else if (h == H_DUAL)
	{
		dual_set (!dual);
		menu_states ();
	}
	else if (h <= H_LIST)
	{
		if (h - H_ICONS != view)
			set_view (h - H_ICONS);
	}
	else
		crumb_go (h - H_CRUMB);
}

/* titre de colonne, avec la fleche du tri */
static void draw_hdr (const char *s, long x, long col, long max)
{
	text (s, x, vtop + 16, TOK (TJ_TEXT2), 12, ui.font_b, max);
	if (sort_key != col)
		return;
	text (sort_desc ? "▼" : "▲", x + text_w (s, 12, ui.font_b) + 5, vtop + 15, TOK (TJ_TEXT2), 9, ui.font, 16);
}

/* vue liste : en-tete (Nom, Taille, Type) et lignes */
static void draw_rows (void)
{
	char buf[32];
	long sx, tx;
	ui_fill (vx0, vtop, vx1 - vx0, H_ - (vtop + SB_H), TOK (TJ_SURFACE));
	col_x (&sx, &tx);
	ui_fill (vx0, vtop + HDR_H - 1, vx1 - vx0, 1, TOK (TJ_LINE));
	long sw = tx - sx;			/* largeur de Taille */
	ui_fill (sx, vtop + 4, 1, HDR_H - 8, TOK (TJ_LINE));	/* separateurs */
	ui_fill (tx, vtop + 4, 1, HDR_H - 8, TOK (TJ_LINE));
	draw_hdr (TR ("Nom"), vx0 + 42, S_NAME, sx - (vx0 + 50));
	draw_hdr (TR ("Taille"), sx + 8, S_SIZE, sw - 12);
	draw_hdr (TR ("Type"), tx + 8, S_TYPE, vx1 - tx - 18);
	for (long r = 0; r < rows; r++)
	{
		long i = scroll_ + r;		/* rang */
		if (i >= nents)
			break;
		long y = r * ROW_H + vtop + HDR_H;	/* haut de la ligne */
		if (is_sel (i))
			draw_sel (vx0 + 4, y + 1, vx1 - (vx0 + 8), ROW_H - 2);
		struct ent *e = ent_ptr (i);
		if (e->kind == K_DIR)
			draw_folder (vx0 + 12, y + 3);
		else if (e->kind == K_APP)
			ui_icon ((long) e->icon, vx0 + 12, y + 3, 20);
		else
			draw_doc (vx0 + 15, y + 4);
		if (e->kind == K_FILE)
		{				/* taille, alignee a droite */
			fmt_size (e->size, buf);
			long x = tx - 10 - text_w (buf, 12, ui.font);
			if (x < sx)
				x = sx;
			text (buf, x, y + 18, TOK (TJ_TEXT2), 12, ui.font, sw - 4);
		}
		text (e->type, tx + 8, y + 18, TOK (TJ_TEXT2), 12, ui.font, vx1 - tx - 18);
		long m = sx - (vx0 + 50);	/* nom */
		text (ent_shown (e), vx0 + 42, y + 18, TOK (TJ_TEXT1), 13, ui.font, m < 8 ? 8 : m);
	}
	long k = drag, h = H_ - (vtop + SB_H);	/* separateur saisi (toute la hauteur) */
	if (!k)
	{
		k = hover < 3 ? hover : 0;	/* ou survole (l'en-tete ; 3 : la separation des vues) */
		h = HDR_H;
	}
	if (k && !pane_dim)
		ui_fill ((k == 1 ? sx : tx) - 1, vtop, 2, h, TOK (TJ_ACCENT));
}

/* grand dossier (48 x 44) */
/* grand dossier (ICON_S) ; emb : son embleme (glyph-place-*, 0 : aucun), au milieu de la face */
static void draw_folder_big (long x, long y, const char *emb)
{
	draw_folder_s (x, y, ICON_S, 1);
	if (emb)
		ui_glyph ((long) emb, x + ICON_S / 2 - 8, y + ICON_S * 5 / 8 - 8, 16, folder_back ());
}

/* grande page (32 x 44, centree dans 48) */
static void draw_doc_big (long x, long y)
{
	x += 8;
	y += 2;
	ui_fill (x, y, 32, 44, TOK (TJ_RAISED));
	ui_frame (x, y, 32, 44, TOK (TJ_LINE_STRONG));
	for (long i = 0; i < 6; i++)		/* lignes de texte */
		ui_fill (x + 6, y + i * 5 + 10, 20, 1, TOK (TJ_LINE_STRONG));
}

/* texte, x de la case, ligne de base : centre dans la case (trop large : a gauche, coupe) */
static void text_center (const char *s, long x, long base)
{
	long o = (CELL_W - text_w (s, 13, ui.font)) >> 1;
	if (o < 4)
		o = 4;
	text (s, x + o, base, TOK (TJ_TEXT1), 13, ui.font, CELL_W - 8);
}

/* nom, x, y de la case : nom centre sur deux lignes au plus */
static void cell_name (const char *s, long x, long y)
{
	if (text_w (s, 13, ui.font) <= CELL_W - 8)
	{
		text_center (s, x, y + ICON_S + 18);
		return;
	}
	long fit = 0, sp = 0, n = 0;		/* longueur qui tient, coupure apres une espace */
	while (s[n])
	{
		n++;				/* un caractere UTF-8 de plus */
		while (((unsigned char) s[n] & 0xC0) == 0x80)
			n++;
		memcpy (cellbuf, s, (size_t) n);
		cellbuf[n] = 0;
		if (text_w (cellbuf, 13, ui.font) > CELL_W - 8)
			break;
		fit = n;
		if (s[n - 1] == ' ')
			sp = n;
	}
	if (!fit)
		fit = n;			/* (au moins un caractere) */
	if (sp)
		fit = sp;
	memcpy (cellbuf, s, (size_t) fit);	/* premiere ligne */
	cellbuf[fit] = 0;
	text_center (cellbuf, x, y + ICON_S + 18);
	text_center (s + fit, x, y + ICON_S + 32);	/* la suite */
}

/* vue icones : grille de cases */
static void draw_grid (void)
{
	ui_fill (vx0, vtop, vx1 - vx0, H_ - (vtop + SB_H), TOK (TJ_SURFACE));
	long gx = grid_x0 ();
	int home = path_is_home ();		/* (ses dossiers types ont leur embleme) */
	for (long c = 0; c < rows * cols; c++)
	{
		long i = scroll_ * cols + c;	/* rang */
		if (i >= nents)
			break;
		long x = (c % cols) * CELL_W + gx, y = (c / cols) * CELL_H + body_y;
		if (is_sel (i))
			draw_sel (x + 3, y + 1, CELL_W - 6, CELL_H - 2);
		struct ent *e = ent_ptr (i);
		long ix = x + (CELL_W - ICON_S) / 2, iy = y + 5;
		if (e->kind == K_DIR)
			draw_folder_big (ix, iy, home ? std_glyph (e->name) : 0);
		else if (e->kind == K_APP)
			ui_icon ((long) e->icon, ix, iy, ICON_S);
		else
			draw_doc_big (ix, iy);
		cell_name (ent_shown (e), x, y);
	}
}

/* ligne d'etat : « N elements », la selection (nom, taille) ou un message */
static void draw_status (void)
{
	if (band_on)
	{
		draw_band ();
		return;
	}
	long y = H_ - SB_H;
	ui_fill (PAN_W, y, W_ - PAN_W, SB_H, TOK (TJ_SURFACE));
	ui_fill (PAN_W, y, W_ - PAN_W, 1, TOK (TJ_LINE));
	const char *s = msg;
	if (!s && ls_loading)
		s = TR ("Chargement…");
	if (!s)
	{
		char *e = tr_cat (fmt_dec ((unsigned long) nents, stbuf), nents == 1 ? " élément" : " éléments");
		if (nsel > 1)
			tr_cat (fmt_dec ((unsigned long) nsel, str_cat (e, "   —   ")), " choisis");
		else if (cursel >= 0 && is_sel (cursel))
		{
			struct ent *en = ent_ptr (cursel);
			e = str_cat (str_cat (e, "   —   "), ent_shown (en));
			if (en->kind == K_FILE)
				fmt_size (en->size, str_cat (e, ", "));
		}
		s = stbuf;
	}
	long right = W_ - 12;			/* a droite : la place du volume courant */
	long cv = cur_vol ();
	const struct vspace *sp = cv >= 0 ? space_of (&vols[cv]) : 0;
	if (sp && W_ - PAN_W > 420)
	{
		char sb[96];
		const char *t = space_text (sp, sb, 12, 0);
		long gw = 64, tw = text_w (t, 12, ui.font);
		draw_gauge (right - gw, H_ - 15, gw, 6, sp);
		right -= gw + 8 + tw;
		text (t, right, H_ - 8, TOK (TJ_TEXT2), 12, ui.font, 0);
		right -= 16;
	}
	text (s, PAN_W + 12, H_ - 8, TOK (TJ_TEXT2), 12, ui.font, right - (PAN_W + 12));
}

/* deux vues : le bandeau de la vue en cours (26 px) : icone, nom du dossier, chemin en TEXTE3, a droite la
 * place libre de son volume ; vue active : fond ACCENT_CLAIR, trait ACCENT de 2 px en haut */
static void draw_banner (void)
{
	long y = LIST_Y, w = vx1 - vx0;
	int on = curp == act;
	ui_fill (vx0, y, w, BAN_H, on ? TOK (TJ_ACCENT_LIGHT) : TOK (TJ_FACE));
	if (on)
		ui_fill (vx0, y, w, 2, TOK (TJ_ACCENT));
	ui_fill (vx0, y + BAN_H - 1, w, 1, TOK (TJ_LINE));
	draw_folder (vx0 + 8, y + 3);
	const char *nm = is_recent () ? TR ("Récents") : path[0] == '/' && !path[1] ? "/" : base_name (path);
	long x = vx0 + 34, right = vx1 - 10;
	long v = vol_of (path);
	const struct vspace *sp = v >= 0 ? space_of (&vols[v]) : 0;
	if (sp)
	{
		char sb[96];
		tr_cat (fmt_bytes (sp->avail, sb), " libres");
		long tw = text_w (sb, 12, ui.font);
		if (right - tw > x + 60)
		{
			text (sb, right - tw, y + 17, TOK (TJ_TEXT2), 12, ui.font, 0);
			right -= tw + 12;
		}
	}
	if (right - x < 8)
		return;
	text (nm, x, y + 18, on ? TOK (TJ_TEXT1) : TOK (TJ_TEXT2), 13, ui.font_b, right - x);
	x += text_w (nm, 13, ui.font_b) + 10;
	if (right - x > 30 && strcmp (nm, "/"))
		text (path, x, y + 17, TOK (TJ_TEXT3), 12, ui.font, right - x);
}

/* la separation des deux vues (saisie a la souris : ACCENT) */
static void draw_split (void)
{
	long sx = split_x (), h = H_ - LIST_Y - SB_H;
	ui_fill (sx - SPL_W / 2, LIST_Y, SPL_W, h, TOK (TJ_SURFACE));
	unsigned c = split_drag || hover == 3 ? TOK (TJ_ACCENT) : TOK (TJ_LINE);
	ui_fill (sx - (split_drag || hover == 3 ? 1 : 0), LIST_Y, split_drag || hover == 3 ? 2 : 1, h, c);
}

static void draw (void)
{
	tk = (const unsigned *) ui_tokens ();
	W_ = ui.w;
	H_ = ui.h;
	pane_use (act);
	draw_panel ();
	draw_top ();
	for (int p = 0; p < NPANE; p++)
	{					/* chaque vue montree dans son rectangle */
		if (!dual && p != act)
			continue;
		pane_use (p);
		pane_dim = dual && p != act;
		if (dual)
			draw_banner ();
		if (view)
			draw_rows ();
		else
			draw_grid ();
	}
	pane_dim = 0;
	pane_use (act);
	if (dual)
		draw_split ();
	draw_status ();
	if (loc_on)
		ui_w_draw (loc_w);
	ui_update_all ();
}

/* fonction de ui_on_resize : colonnes, rangees et dessin suivent la fenetre */
static long on_resize (long w, long h)
{
	(void) w;
	(void) h;
	relayout_all ();
	draw ();
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Entrees
 * --------------------------------------------------------------------------- */

/* entree de menu choisie */
/* Affichage > Fichiers caches (Ctrl+H) : le dossier relu (et celui de l'autre vue), la selection gardee */
static void toggle_hidden (void)
{
	show_hidden = !show_hidden;
	refresh ();
	if (dual)
		other_stale = 1;
	menu_states ();
	save_cfg ();
	set_msg (show_hidden ? "Fichiers cachés montrés" : "Fichiers cachés masqués");
}

static void on_menu (long id)
{
	switch (id)
	{
	case ID_OPEN: open_sel (); break;
	case ID_NEWDIR: new_folder (); break;
	case ID_DELETE: delete_sel (); break;
	case ID_REFRESH: refresh (); break;
	case ID_CLOSE: quit (); break;
	case ID_OPENWITH: open_with_sel (); break;
	case ID_RENAME: rename_sel (); break;
	case ID_COPY: clip_sel (0); break;
	case ID_CUT: clip_sel (1); break;
	case ID_PASTE: paste (); break;
	case ID_PARENT: go_parent (); break;
	case ID_SD0: go_sd0 (); break;
	case ID_EJECT: eject_cur (); break;
	case ID_CONNECT: connect_server (); break;
	case ID_GDRIVE: connect_gdrive (); break;
	case ID_ICONS: set_view (0); break;
	case ID_LIST: set_view (1); break;
	case ID_DUAL: dual_set (!dual); menu_states (); break;
	case ID_COPYTO: to_other (0); break;
	case ID_MOVETO: to_other (1); break;
	case ID_LOCATION: loc_open (0); return;
	case ID_HIDDEN: toggle_hidden (); break;
	case ID_ALLVOL:
		show_allvol = !show_allvol;
		pan_clamp ();
		menu_states ();
		save_cfg ();
		break;
	default:
		if ((unsigned long) (id - ID_PLACE) >= (unsigned long) pl_n)
			return;
		go_dir (places[id - ID_PLACE].path);
	}
	draw ();
}

/* souris hors appui : survol (separateurs, en-tete), glisser, relache */
static void mouse_aux (void)
{
	long kind = ui_ev_field (&ui, EV_KIND), x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y);
	if (split_drag)
	{					/* la separation des deux vues suit la souris */
		if (kind == 2 || !ui_ev_field (&ui, EV_CODE))
		{
			split_drag = 0;
			save_cfg ();
		}
		else if (ui.w > PAN_W)
		{
			split_pm = clamp ((x - PAN_W) * 1000 / (ui.w - PAN_W), 100, 900);
			relayout_all ();
		}
		draw ();
		return;
	}
	if (drag)
	{
		if (kind == 2 || !ui_ev_field (&ui, EV_CODE))
		{
			drag = 0;
			save_cfg ();
			draw ();
		}
		else if (drag_to (x))
			draw ();
		return;
	}
	if (kind)
	{					/* relache */
		if (press_keep && press_i >= 0 && press_i < nents)
		{				/* un simple clic dans la selection : elle seule */
			sel_one (press_i);
			draw ();
		}
		press_keep = 0;
		press_i = -1;
		return;
	}
	if (press_i >= 0)
	{					/* bouton tenu sur un element : assez loin, on le glisse (toute la selection) */
		static char b[16384];
		if (!(ui_ev_field (&ui, EV_CODE) & 1))
			press_i = -1;
		else if (labs (x - press_x) + labs (y - press_y) >= DRAG_PX && press_i < nents)
		{
			long gi = press_i;
			if (!is_sel (gi) || !sel_list (b, sizeof b))
				str_copy_n (b, (make_full (gi), fullpath), sizeof b);
			press_i = -1;
			press_keep = 0;
			last_idx = -1;
			ui_drag_start ((long) b, -1, UDT_FILES);
			drag_ghost (gi);
			return;
		}
	}
	long s = sep_at (x, y);
	if (!s && dual && y >= LIST_Y && y < ui.h - SB_H && labs (x - split_x ()) <= SPL_W / 2 + 1)
		s = 3;				/* (la separation des vues) */
	long ch = s != hover;
	hover = s;
	long h = head_at (x, y);
	if (h != hhot)
	{
		ch = 1;
		if (h == H_DUAL)		/* l'infobulle du bouton Deux vues */
			ui_tip_at (ui.w - DUAL_X_R, NAV_Y + NAV_H + 4, (long) TR (dual ? "Une seule vue" : "Deux vues"));
		else if (hhot == H_DUAL)
			ui_tip_at (0, 0, 0);
		hhot = h;
	}
	if (ch)
		draw ();
}

static void on_mouse (void)
{
	if (ui_ev_field (&ui, EV_KIND) != 1)
	{					/* deplacement, relache */
		mouse_aux ();
		return;
	}
	long x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y);
	if (band_hit (x, y))
	{					/* Annuler de la bande de progression */
		cp_cancel ();
		return;
	}
	if (x < PAN_W)
	{
		if ((unsigned long) y < TOP_H - 8)
		{				/* le haut du panneau deplace la fenetre */
			ui_window_drag ();
			return;
		}
		long py = y + pan_scroll;	/* (le panneau defile : la ligne sous le pointeur) */
		if (py < PAN_Y0)			/* panneau : acces rapides puis volumes */
			return;
		long r = (py - PAN_Y0) / PROW_H;
		if (r < pl_n && (ui_ev_field (&ui, EV_CODE) & 2))
		{				/* bouton droit sur un acces rapide : Retirer */
			char *e = tr_cat (popbuf, "Retirer") + 1;
			*e = 0;
			char dir[sizeof places[0].path];
			str_copy_n (dir, places[r].path, sizeof dir);
			if (ui_popup_local (x, y, (long) popbuf, 200) == 0)
				place_pin (dir, 0);
			draw ();
			return;
		}
		if (r < pl_n)
			go_dir (places[r].path);
		else
		{
			long y0 = vol_y0 ();
			if (py < y0)
				return;
			r = (py - y0) / VROW_H;
			long vix[VMAX], vn = vol_shown (vix);
			if (r >= vn)
				return;
			r = vix[r];			/* (le rang dans vols) */
			if (vols[r].rem && x >= PAN_W - 36)
				eject (r);		/* bouton d'ejection */
			else
				go_vol (r);
		}
		draw ();
		return;
	}
	if (y < TOP_H)
	{					/* en-tete */
		long h = head_at (x, y);
		if (!h && x >= CR_X0 - 8 && x < W_ - CR_R + 8 && y >= NAV_Y && y < NAV_Y + NAV_H)
		{				/* le fil d'Ariane hors d'un segment : le chemin s'edite */
			loc_open (0);
			return;
		}
		if (!h)
			return;
		head_click (h);
		draw ();
		return;
	}
	if (dual && y >= LIST_Y && y < ui.h - SB_H)
	{					/* deux vues : la separation se saisit ; un clic dans une vue l'active */
		long sx = split_x ();
		if (labs (x - sx) <= SPL_W / 2 + 1)
		{
			split_drag = 1;
			draw ();
			return;
		}
		if ((x > sx) != act)
		{
			pane_activate (x > sx);
			draw ();
		}
		if (y < LIST_Y + BAN_H)
			return;			/* (le bandeau : l'activer, rien de plus) */
	}
	if (view)
	{					/* en-tete des colonnes */
		if (y < vtop)
			return;
		if (y < vtop + HDR_H)
		{
			long s = sep_at (x, y);	/* sur un separateur : on le saisit */
			if (s)
				drag = s;
			else
				header_click (x);
			draw ();
			return;
		}
	}
	if (y < body_y)
		return;
	long r = (y - body_y) / line_h;		/* ligne visible */
	if (r >= rows)
		return;
	long i = (r + scroll_) * cols;		/* premier rang de la ligne */
	int empty = 0;
	if (!view)
	{
		long gx = grid_x0 ();		/* colonne de la grille */
		if (x < gx || (x - gx) / CELL_W >= cols)
			empty = 1;
		else
			i += (x - gx) / CELL_W;
	}
	long mods = ui_ev_field (&ui, EV_MODS);
	if (empty || i >= nents)
	{					/* dans le vide : plus de selection */
		if (!(mods & (UI_MOD_CTRL | UI_MOD_SHIFT)))
			sel_one (-1);
		last_idx = -1;
		draw ();
		if (ui_ev_field (&ui, EV_CODE) & 2)
		{				/* bouton droit : Coller, Nouveau dossier */
			ctx_menu (x, y);
			draw ();
		}
		return;
	}
	if (ui_ev_field (&ui, EV_CODE) & 2)
	{					/* bouton droit : le menu de l'entree (de toute la selection, s'il en est) */
		if (!is_sel (i))
			sel_one (i);
		cursel = i;
		draw ();
		ctx_menu (x, y);
		draw ();
		return;
	}
	long now = time_ms ();
	press_keep = 0;
	if (last_idx == i && (unsigned long) (now - last_time) < DCLICK_MS && !(mods & (UI_MOD_CTRL | UI_MOD_SHIFT)))
	{					/* double-clic */
		last_idx = -1;
		sel_one (i);
		open_sel ();
	}
	else
	{
		last_idx = i;
		last_time = now;
		if (mods & UI_MOD_CTRL)
		{				/* Ctrl : ajoute ou retire, comme Windows */
			sel_put (i, !is_sel (i));
			cursel = i;
			anchor = i;
		}
		else if (mods & UI_MOD_SHIFT)
			sel_range (i);		/* Maj : la plage depuis le dernier choisi */
		else if (is_sel (i) && nsel > 1)
		{				/* dans la selection : la garder pour la glisser (un simple clic : elle seule) */
			cursel = i;
			press_keep = 1;
		}
		else
			sel_one (i);
		msg = 0;			/* le message laisse place a la selection */
		press_i = i;			/* (glisser l'element : mouse_aux) */
		press_x = x;
		press_y = y;
	}
	draw ();
}

/* l'element au point (x, y) de la fenetre, -1 : aucun */
static long ent_at (long x, long y)
{
	if (x < vx0 || x >= vx1 || y < body_y)
		return -1;
	long r = (y - body_y) / line_h;
	if (r >= rows)
		return -1;
	long i = (r + scroll_) * cols;
	if (!view)
	{
		long gx = grid_x0 ();
		if (x < gx || (x - gx) / CELL_W >= cols)
			return -1;
		i += (x - gx) / CELL_W;
	}
	return i < nents ? i : -1;
}

/* element glisse : son image sous le curseur (Elegant copie sa case de la fenetre : icone et nom,
 * fond de la selection compris), le point appuye restant sous le curseur */
static void drag_ghost (long i)
{
	long x, y, w, h;
	if (view)
	{				/* liste : l'icone et le nom de la ligne */
		x = vx0 + 6;
		y = (i - scroll_) * line_h + body_y;
		w = 42 + text_w (ent_shown (ent_ptr (i)), 13, ui.font) + 10;
		if (w > 260)
			w = 260;
		h = line_h;
	}
	else
	{
		long c = i - scroll_ * cols;
		x = (c % cols) * CELL_W + grid_x0 () + 3;
		y = (c / cols) * CELL_H + body_y + 1;
		w = CELL_W - 6;
		h = CELL_H - 2;
	}
	if (y < body_y || y + h > ui.h - SB_H)
		return;				/* (a moitie cache : le curseur seul) */
	ui_drag_image (x, y, w, h, press_x - x, press_y - y);
}

/* des fichiers deposes (UDT_FILES) : dans le dossier pointe, sinon le dossier courant ; deplaces
 * sur un meme volume, copies d'un volume a l'autre */
static void on_drop (void)
{
	if (is_recent ())
		return;				/* (rien ne se depose dans Recents) */
	static char b[8192];
	long kind = 0, n = uikit_drop_get (b, sizeof b - 1, &kind);
	if (n <= 0 || kind != UDT_FILES)
		return;
	b[n] = 0;
	char dir[CP_PATH];
	long dx = ui_ev_field (&ui, EV_X);
	if (dual && dx >= PAN_W)
		pane_activate (dx > split_x ());	/* (la vue ou l'on depose devient l'active) */
	drop_move = (ui_ev_field (&ui, EV_MODS) & UI_MOD_SHIFT) != 0;	/* copie ; Maj : deplacer */
	long i = ent_at (dx, ui_ev_field (&ui, EV_Y));
	if (i >= 0 && ent_ptr (i)->kind == K_DIR)
		make_full (i);
	else
		str_copy_n (fullpath, path, sizeof fullpath);
	str_copy_n (dir, fullpath, sizeof dir);
	paste_list (b, M_DROP, dir);
	drop_move = 0;
	if (dual)
		other_stale = 1;		/* (la source est peut-etre dans l'autre vue) */
	draw ();
}

/* ---- le champ d'emplacement ---- */

/* init : le texte de depart, curseur a la fin (« / », « ~/ ») ; 0 : le dossier courant, tout choisi */
static void loc_open (const char *init)
{
	if (!loc_w)
		loc_w = ui_textbox (CR_X0, NAV_Y, 100, NAV_H, 0);
	if (!loc_w)
		return;
	ui_w_set_text (loc_w, (long) (init ? init : path));
	long n = ui_w_text_len (loc_w);
	ui_w_show (loc_w, 1);
	loc_on = 1;
	loc_place ();
	ui_w_focus (loc_w);
	ui_textbox_select (loc_w, init ? n : 0, n);
	draw ();
}

static void loc_close (void)
{
	if (!loc_on)
		return;
	loc_on = 0;
	ui_w_show (loc_w, 0);
	draw ();
}

/* texte du champ -> chemin complet dans d (« ~ » : HOME:, relatif : depuis le dossier courant ; « . » et « .. »
 * resolus, « / » doubles et de fin retires) */
static void loc_resolve (const char *in, char *d, size_t cap)
{
	char t[600];
	while (*in == ' ')
		in++;
	if (in[0] == '~' && (!in[1] || in[1] == '/'))
		snprintf (t, sizeof t, "HOME:/%s", in[1] ? in + 2 : "");
	else if (in[0] == '/' || home_prefix (in) || strchr (in, ':'))
		snprintf (t, sizeof t, "%s", in);
	else
		path_join (t, path, in);
	size_t n = strlen (t);
	while (n && t[n - 1] == ' ')
		t[--n] = 0;
	/* la racine (« / » ou « VOLUME: ») puis les noms */
	char *r = t, out[600];
	size_t o = 0;
	char *colon = strchr (t, ':'), *slash = strchr (t, '/');
	if (colon && (!slash || colon < slash))
	{
		o = (size_t) (colon - t) + 1;
		memcpy (out, t, o);
		r = colon + 1;
	}
	out[o++] = '/';
	size_t root = o;
	for (char *seg = r; *seg;)
	{
		while (*seg == '/')
			seg++;
		char *e = seg;
		while (*e && *e != '/')
			e++;
		size_t l = (size_t) (e - seg);
		if (l == 1 && seg[0] == '.')
			;
		else if (l == 2 && seg[0] == '.' && seg[1] == '.')
		{				/* le nom d'avant s'en va (jamais la racine) */
			if (o > root)
			{
				o--;
				while (o > root && out[o - 1] != '/')
					o--;
			}
		}
		else if (l && o + l + 2 < sizeof out)
		{
			memcpy (out + o, seg, l);
			o += l;
			out[o++] = '/';
		}
		seg = e;
	}
	if (o > root)
		o--;				/* (pas de « / » a la fin, sauf la racine) */
	out[o] = 0;
	snprintf (d, cap, "%s", out);
}

/* Entree : un dossier -> on y va ; un fichier -> son dossier, lui choisi ; sinon le message, le champ reste */
static void loc_go (void)
{
	char p[600];
	struct stat st;
	const char *txt = (const char *) ui_w_get_text (loc_w);
	while (*txt == ' ')
		txt++;
	if (!strncasecmp (txt, RECENT, 7) && (!txt[7] || txt[7] == '/' || txt[7] == ' '))
	{				/* « recent: » : la vue Recents */
		loc_on = 0;
		ui_w_show (loc_w, 0);
		go_dir (RECENT);
		draw ();
		return;
	}
	loc_resolve (txt, p, sizeof p);
	if (strlen (p) >= 256 || stat (p, &st) != 0)
	{
		static char m[300];
		snprintf (m, sizeof m, TR ("Introuvable : %s"), p);
		msg = m;
		draw ();
		return;
	}
	loc_on = 0;
	ui_w_show (loc_w, 0);
	if (S_ISDIR (st.st_mode))
		go_dir (p);
	else
	{				/* un fichier : son dossier, et lui choisi */
		char dir[600];
		snprintf (dir, sizeof dir, "%s", p);
		path_parent (dir);
		go_dir (dir);
		const char *b = base_name (p);
		for (long i = 0; i < nents; i++)
			if (!strcmp (ent_ptr (i)->name, b))
			{
				sel_one (i);
				keep_visible ();
				break;
			}
	}
	draw ();
}

/* Tab : le dernier nom complete d'apres son dossier (le plus long debut commun ; un seul dossier : « / » apres) */
static void loc_complete (void)
{
	char txt[600], dir[600], best[256];
	snprintf (txt, sizeof txt, "%s", (const char *) ui_w_get_text (loc_w));
	char *last = strrchr (txt, '/');
	const char *pre = last ? last + 1 : txt;
	size_t pl = strlen (pre);
	if (last)
	{				/* le dossier : le texte jusqu'au dernier « / » */
		char c = last[1];
		last[1] = 0;
		loc_resolve (txt, dir, sizeof dir);
		last[1] = c;
	}
	else
		snprintf (dir, sizeof dir, "%s", path);
	DIR *dp = opendir (dir);
	if (!dp)
		return;
	long found = 0;
	int isdir = 0;
	size_t bl = 0;
	struct dirent *de;
	while ((de = readdir (dp)))
	{
		const char *nm = de->d_name;
		if (nm[0] == '.' && (!pl || pre[0] != '.'))
			continue;			/* (les caches seulement si on les demande) */
		if (strncasecmp (nm, pre, pl))
			continue;
		if (!found++)
		{
			snprintf (best, sizeof best, "%s", nm);
			bl = strlen (best);
		}
		else
		{				/* le debut commun (sans casse) */
			size_t k = 0;
			while (k < bl && nm[k] && tolower ((unsigned char) nm[k]) == tolower ((unsigned char) best[k]))
				k++;
			bl = k;
		}
		if (found == 1)
		{
			char fp[600];
			struct stat st;
			path_join (fp, dir, nm);
			isdir = stat (fp, &st) == 0 && S_ISDIR (st.st_mode);
		}
	}
	closedir (dp);
	if (!found || (bl <= pl && found > 1))
		return;				/* (rien, ou plusieurs sans rien de plus en commun) */
	best[bl] = 0;
	size_t head = (size_t) (pre - txt);
	snprintf (txt + head, sizeof txt - head, "%s%s", best, found == 1 && isdir ? "/" : "");
	ui_w_set_text (loc_w, (long) txt);
	long n = ui_w_text_len (loc_w);
	ui_textbox_select (loc_w, n, n);
	ui_w_draw (loc_w);
}

/* un evenement pendant l'edition -> 1 s'il est pris (le champ, Entree, Echap, Tab, un clic ailleurs le ferme) */
static int loc_event (long t)
{
	if (t == WEV_KEY && ui_ev_field (&ui, EV_KIND))
	{
		long k = ui_ev_field (&ui, EV_CODE);
		if (k == KEY_ESC_)
		{
			loc_close ();
			return 1;
		}
		if (k == KEY_TAB_ && !(ui_ev_field (&ui, EV_MODS) & UI_MOD_CTRL))
		{
			loc_complete ();
			return 1;
		}
	}
	if (t == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1)
	{				/* un clic hors du champ : ferme, puis le clic sert */
		long x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y);
		if (y < NAV_Y || y >= NAV_Y + NAV_H || x < CR_X0 - 8 || x >= W_ - CR_R + 8)
		{
			loc_close ();
			return 0;
		}
	}
	long code = 0, taken = 0;
	long wid = uikit_w_event (&code, &taken);
	if (wid == loc_w && code == UN_ACTIVATE)
	{
		loc_go ();
		return 1;
	}
	if (t == WEV_KEY)
		return 1;			/* (les touches restent au champ : Suppr n'efface pas un fichier) */
	return taken != 0;
}

/* ---- aller a un element en tapant son nom (24/11/2026) : les lettres tapees a moins d'une seconde
 * d'intervalle s'ajoutent ; le premier element dont le nom commence ainsi est choisi ; la meme lettre
 * repetee passe au suivant qui commence par elle ---- */
static char ta_buf[64];
static long ta_len, ta_time;
static char ta_msg[120];

/* point de code -> UTF-8 dans d -> octets */
static int utf8_put (long c, char *d)
{
	if (c < 0x80) { d[0] = (char) c; return 1; }
	if (c < 0x800) { d[0] = (char) (0xC0 | (c >> 6)); d[1] = (char) (0x80 | (c & 63)); return 2; }
	if (c < 0x10000) { d[0] = (char) (0xE0 | (c >> 12)); d[1] = (char) (0x80 | ((c >> 6) & 63)); d[2] = (char) (0x80 | (c & 63)); return 3; }
	d[0] = (char) (0xF0 | (c >> 18)); d[1] = (char) (0x80 | ((c >> 12) & 63)); d[2] = (char) (0x80 | ((c >> 6) & 63));
	d[3] = (char) (0x80 | (c & 63));
	return 4;
}

static void type_ahead (long ch)
{
	long now = time_ms ();
	if (now - ta_time > 1000)
		ta_len = 0;
	ta_time = now;
	char u[4];
	int n = utf8_put (ch, u);
	if (ta_len + n >= (long) sizeof ta_buf)
		return;
	memcpy (ta_buf + ta_len, u, (size_t) n);
	ta_len += n;
	ta_buf[ta_len] = 0;
	if (!nents)
		return;
	/* la meme lettre repetee (« aaa ») : le suivant qui commence par elle, en boucle */
	int same = ta_len > n;
	for (long k = n; same && k < ta_len; k++)
		if (ta_buf[k] != ta_buf[k % n])
			same = 0;
	long start = 0, found = -1;
	const char *want = ta_buf;
	size_t wl = (size_t) ta_len;
	if (same)
	{
		wl = (size_t) n;
		start = cursel >= 0 ? cursel + 1 : 0;
	}
	for (long j = 0; j < nents && found < 0; j++)
	{
		long i = (start + j) % nents;
		if (!strncasecmp (ent_shown (ent_ptr (i)), want, wl))
			found = i;
	}
	if (same && found < 0)
		return;
	if (found < 0)
	{				/* rien ne commence ainsi : le message, la selection ne bouge pas */
		snprintf (ta_msg, sizeof ta_msg, "%s « %s »", TR ("Aucun élément ne commence par"), ta_buf);
		msg = ta_msg;
		draw ();
		return;
	}
	sel_one (found);
	keep_visible ();
	snprintf (ta_msg, sizeof ta_msg, "%s « %.*s »", TR ("Aller à"), (int) wl, ta_buf);
	msg = ta_msg;
	draw ();
}

static void on_key (void)
{
	if (!ui_ev_field (&ui, EV_KIND))
		return;				/* relache */
	long k = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR), mods = ui_ev_field (&ui, EV_MODS);
	if (ch >= 0x20 && ch != 0x7F && ch < 0x110000 && (!(mods & UI_MOD_CTRL) || (mods & 0x40)))
	{					/* un caractere (AltGr compris) : « / » et « ~ » ouvrent l'emplacement, le reste cherche */
		if (ta_len && time_ms () - ta_time <= 1000)
			;			/* (en cours de frappe : « / » ou « ~ » font partie du nom) */
		else if (ch == '/' || ch == '~')
		{
			loc_open (ch == '/' ? "/" : "~/");
			return;
		}
		if (ch != ' ' || (ta_len && time_ms () - ta_time <= 1000))
		{
			type_ahead (ch);
			return;
		}
	}
	if (mods & UI_MOD_CTRL)
	{
		if (k == KEY_N_)
			new_folder ();
		else if (k == KEY_L_)
		{
			loc_open (0);		/* (comme GNOME : Ctrl+L, l'emplacement ; Actualiser : F5, Ctrl+R) */
			return;
		}
		else if (k == KEY_R_)
			refresh ();
		else if (k == KEY_H_)
			toggle_hidden ();
		else if (k == KEY_W_)
			quit ();
		else if (k == KEY_K_)
			connect_server ();
		else if (k == KEY_C_)
			clip_sel (0);
		else if (k == KEY_X_)
			clip_sel (1);
		else if (k == KEY_V_)
			paste ();
		else if (k == KEY_A_)
		{				/* tout choisir */
			for (long i = 0; i < nents; i++)
				sel_put (i, 1);
			if (cursel < 0 && nents)
				cursel = 0;
		}
		else
			return;
		draw ();
		return;
	}
	long cs = cursel, step = cols, set = 0;
	switch (k)
	{
	case KEY_RIGHT:				/* gauche / droite : vue icones */
		if (view)
			return;
		step = 1;
		/* fallthrough */
	case KEY_DOWN:
		if (cs < 0)
			goto first;
		cs += step;
		if (cs > nents - 1)
			cs = nents - 1;
		if (cs < 0)
			return;
		set = 1;
		break;
	case KEY_LEFT:
		if (view)
			return;
		step = 1;
		/* fallthrough */
	case KEY_UP:
		if (cs <= 0)
			goto first;
		cs -= step;
		if (cs < 0)
			cs = 0;
		set = 1;
		break;
	case KEY_HOME:
	first:
		if (!nents)
			return;
		cs = 0;
		set = 1;
		break;
	case KEY_END:
		if (!nents)
			return;
		cs = nents - 1;
		set = 1;
		break;
	case KEY_ENTER:
	case KEY_KPENT_:
		open_sel ();
		break;
	case KEY_BACKSPACE:
		go_parent ();
		break;
	case KEY_DELETE:
		delete_sel ();
		break;
	case KEY_F2_:
		rename_sel ();
		break;
	case KEY_F5_:
		refresh ();
		break;
	default:
		return;
	}
	if (set)
	{					/* Maj : la plage s'etend ; sinon un seul element */
		if (ui_ev_field (&ui, EV_MODS) & UI_MOD_SHIFT)
		{
			if (anchor < 0)
				anchor = cursel >= 0 ? cursel : cs;
			sel_range (cs);
		}
		else
			sel_one (cs);
		msg = 0;
		keep_visible ();
	}
	draw ();
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui))
		return 1;
	buskit_kit_open ();
	for (int p = 0; p < NPANE; p++)
	{					/* les deux vues ; la 0 dans les variables globales */
		struct pane *P = &panes[p];
		P->ents = (struct ent *) calloc (MAXE, sizeof *P->ents);
		P->order = (long *) calloc (MAXE, sizeof *P->order);
		P->selm = (unsigned char *) calloc (MAXE, 1);
		P->hist = (char (*)[HIST_SZ]) calloc (HIST_MAX, HIST_SZ);
		if (!P->ents || !P->order || !P->selm || !P->hist)
			return 1;
		P->cursel = P->anchor = P->last_idx = P->h_pos = -1;
		P->rows = P->cols = 1;
		P->line_h = ROW_H;
		P->body_y = LIST_Y + HDR_H;
		P->sort_key = S_NAME;
		P->w_size = COL_SIZE_W;
		P->w_type = COL_TYPE_W;
	}
	pane_load (0);
	long ww = WIN_W, wh = WIN_H;		/* taille : bornee par l'ecran */
	if (ui_screen ((long) scr) >= 0)
	{
		if (ww > scr[0] - 40)
			ww = scr[0] - 40;
		long m = scr[1] - scr[2] - scr[3] - (FRAME_TITLE_H + FRAME_BORDER + 24);
		if (wh > m)
			wh = m;
		if (ww < 480)
			ww = 480;
		if (wh < 300)
			wh = 300;
	}
	if (ui_window_ex (-1, -1, ww, wh, (long) TR ("Fichiers"), WF_RESIZABLE) < 0)
		return 0;
	ui_min_size (480, 300);
	ui_on_resize ((long) on_resize, 0);
	std_dirs_make ();
	places_load ();
	menu_build ();
	ui_menu ((long) menu_buf);		/* barre de menus globale */
	menu_states ();
	char cq_[160];
	ini_read (korin_conf_path ("files.ini", cq_, sizeof cq_, 0), cfg_line);	/* vues choisies, deux vues */
	/* dossiers : une vue, / (ou l'argument) ; deux vues, ceux d'avant (l'argument dans la vue active) */
	struct stat st0;
	for (int p = 0; p < NPANE; p++)
		if (!dual || !cfg_dir[p][0] || stat (cfg_dir[p], &st0) || !S_ISDIR (st0.st_mode))
			str_copy_n (cfg_dir[p], "/", sizeof cfg_dir[p]);
	if (!dual)
		act = 0;
	if (argc > 1 && argv[1][0])
		str_copy_n (cfg_dir[act], argv[1], sizeof cfg_dir[act]);
	str_copy_n (panes[1].path, cfg_dir[1], sizeof panes[1].path);
	str_copy_n (path, cfg_dir[0], 256);
	vfs_ch = sys3 (SYS_SVC_OPEN_, (long) "vfs", 500);
	if (dual)
	{
		pane_use (1);
		load_dir ();
		pane_use (0);
	}
	load_dir ();
	pane_use (act);
	relayout_all ();
	menu_states ();
	vol_sub = bus_subscribe ((long) BUS_TOPIC_VOLUME "*", 0);	/* montages et demontages : le bus */
	if (vol_sub < 0)
		vol_sub = 0;
	chg_sub = bus_subscribe ((long) BUS_TOPIC_CHANGED, 0);	/* les autres fenetres de Fichiers */
	if (chg_sub < 0)
		chg_sub = 0;
	last_poll = time_ms ();
	vol_poll ();
	draw ();
	for (;;)
	{
		long t = ui_event (band_on || busy_since || ls_loading ? 100 : 500);
		if (t == WEV_CLOSE)
		{
			if (cp_stop_for_quit ())
				break;
			continue;
		}
		if (t == WEV_RESIZE)
		{
			long code, taken;	/* (le kit prend la taille, puis on_resize) */
			uikit_w_event (&code, &taken);
		}
		else if (loc_on && (t == WEV_KEY || t == WEV_MOUSE) && loc_event (t))
			;				/* (le champ d'emplacement l'a pris) */
		else if (t == WEV_MOUSE)
		{
			ptr_x = ui_ev_field (&ui, EV_X);
			on_mouse ();
		}
		else if (t == WEV_KEY)
			on_key ();
		else if (t == WEV_MENU)
			on_menu (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_THEME)
			draw ();			/* (theme ou accent change : les dossiers en prennent la couleur) */
		else if (t == WEV_DROP)
			on_drop ();
		else if (t == WEV_BUS)
		{				/* un volume monte ou demonte */
			long sub = ui_ev_field (&ui, BEV_SUB);
			if (sub == vol_sub && vol_poll ())
				draw ();
			else if (chg_sub && sub == chg_sub && chg_seen (ui_ev_field (&ui, BEV_TOPIC)))
				draw ();
		}
		else if (t == WEV_WHEEL && ptr_x < PAN_W)
		{				/* sur le panneau : ses lignes (40 px par cran) */
			pan_scroll -= ui_ev_field (&ui, EV_Y) * 40;
			pan_clamp ();
			draw ();
		}
		else if (t == WEV_WHEEL)
		{				/* 3 lignes (liste), 1 rangee (icones) */
			set_scroll (scroll_ - ui_ev_field (&ui, EV_Y) * (view ? 3 : 1));
			draw ();
		}
		gdj_poll ();			/* (la connexion à Google Drive en arrière-plan) */
		cp_poll ();			/* (la copie en arriere-plan : bande, lots finis) */
		if (space_poll ())		/* (la place des volumes, mesuree dans son thread) */
			draw ();
		if (ls_poll ())			/* (un dossier lu dans son thread) */
		{
			keep_visible ();
			draw ();
		}
		if (other_stale)		/* (deux vues : l'autre a change) */
		{
			other_refresh ();
			draw ();
		}
		if (vol_sub)			/* sans le bus : les volumes environ chaque seconde */
			continue;
		long now = time_ms ();
		if ((unsigned long) (now - last_poll) < POLL_MS)
			continue;
		last_poll = now;
		if (vol_poll ())
			draw ();
	}
	return 0;
}
