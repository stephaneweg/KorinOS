/*
 * files -- le navigateur de fichiers de Korin OS. Port en C de files.asm (memes fenetre, panneau,
 * menus, raccourcis, textes ; un seul source pour le Pi et le PC) ; l'asm reste dans le dossier
 * jusqu'a sa validation sur le Pi.
 *
 * A gauche, un panneau : les volumes montes (service vfs, relu a chaque montage ou demontage publie
 * sur le bus, « korin.storage.volume.* », sinon chaque seconde ; bouton d'ejection des cles USB) et
 * les acces rapides (SD0:/etc/places.ini). A droite : en-tete (precedent, suivant, parent, fil
 * d'Ariane, choix de la vue ; FRAME_CAP_W libres a droite), contenu du dossier courant (dossiers
 * d'abord, tri par nom ; paquets .app avec leur icone et leur nom), ligne d'etat. Barre de menus
 * globale. Ouvrir : dossier -> on y entre, .app -> son main, autre -> son application (UIKit :
 * ui_assoc_open). Clavier : fleches, Entree, Retour, Suppr, Ctrl+N, Ctrl+L, F5.
 */
#include <armpi/syscall.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <spawn.h>
#include <sys/stat.h>
#include <uikit.h>
#include <buskit_abi.h>

extern char **environ;

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

/* ---- appels systeme (src/inc/syscalls.inc) et service vfs (src/inc/fs.inc) ---- */
enum { SYS_SLEEP_ = 0x17, SYS_TIME_MS_ = 0x19, SYS_SVC_OPEN_ = 0x31, SYS_SVC_CALL_ = 0x32 };
enum { VFS_UNMOUNT_ = 21, VFS_VOLUMES_ = 22 };
static long sys3 (long n, long a, long b) { return armpi_syscall (n, a, b, 0, 0, 0, 0); }
static long time_ms (void) { return sys3 (SYS_TIME_MS_, 0, 0); }

struct svc_msg					/* message de 512 octets (MSG_*) */
{
	long op, a, b, c, d;
	unsigned len, pid;
	unsigned char data[464];
};

#define BUS_TOPIC_VOLUME "korin.storage.volume."	/* (src/inc/bus.inc) */

#define WIN_W		760
#define WIN_H		480
#define PAN_W		190			/* panneau de gauche */
#define PAN_Y0		30			/* premiere ligne de volume */
#define VROW_H		36			/* ligne de volume (nom, genre) */
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
#define CR_R		(SEG_X_R + 12)		/* sa fin, depuis la droite */
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
#define SB_H		24			/* ligne d'etat */
#define ROW_H		26
#define MAXE		4096
#define K_FILE		0
#define K_DIR		1
#define K_APP		2
#define VMAX		8
#define VE_SZ		56			/* entree de VFS_VOLUMES : nom 16, source 24, service 16 */
#define PL_MAX		10
#define PL_BUF		2048
#define DCLICK_MS	600
#define POLL_MS		1000

/* codes HID */
#define KEY_N_		0x11
#define KEY_L_		0x0F
#define KEY_W_		0x1A
#define KEY_F5_		0x3E
#define KEY_KPENT_	0x58

/* entrees des menus */
#define ID_OPEN		1
#define ID_NEWDIR	2
#define ID_DELETE	3
#define ID_REFRESH	4
#define ID_CLOSE	5
#define ID_OPENWITH	6
#define ID_PARENT	10
#define ID_SD0		11
#define ID_PLACE	20			/* 20 + rang de l'acces rapide */
#define ID_EJECT	30
#define ID_ICONS	40
#define ID_LIST		41

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
#define COL_FOLDER	0x00E8B64C
#define COL_FOLDER_D	0x00C99A35

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
	char disp[16];				/* « USB0: » */
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
static long hover;				/* separateur survole */
static long hhot;				/* element de l'en-tete survole */
static long cr_n;				/* segments du fil d'Ariane */
static long h_pos = -1, h_n;			/* historique */
static long last_idx = -1, last_time, last_poll;
static long vol_sub;				/* abonnement aux volumes (0 : pas de bus) */
static const char *msg;				/* message d'etat (0 : aucun) */
static long vfs_ch = -1;
static long vraw_n = -1, vol_n, pl_n;
static long scr[4];
static char errbuf[32];
static struct svc_msg vmsg;
static unsigned char vraw[VMAX * VE_SZ];
static struct vol vols[VMAX];
static struct place places[PL_MAX];
static char pl_buf[PL_BUF];
static char menu_buf[WS_MENU_MAX + 16];
static char path[272], fullpath[600], tmpname[272], crbuf[272], stbuf[400], popbuf[400], cellbuf[272], cfgbuf[128];
static struct crumb crumbs[CR_MAX];
static char hist[HIST_MAX][HIST_SZ];
static long order[MAXE];
static struct ent *ents;			/* MAXE entrees */
static const unsigned *tk;			/* jetons du theme (pendant draw) */
static long W_, H_;				/* taille de la fenetre (pendant draw) */

#define TOK(t)	(tk[(t) / 4])

static void quit (void) { exit (0); }

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
	if (last == p)
		return;
	if (last[-1] == ':')
		last++;
	*last = 0;
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

static const char *ent_shown (struct ent *e) { return e->kind == K_APP ? e->label : e->name; }

/* rang affiche -> chemin complet dans fullpath -> sa fin */
static char *make_full (long i) { return path_join (fullpath, path, ent_ptr (i)->name); }

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
static void app_info (struct ent *e)
{
	long n = (long) strlen (e->name) - 4;
	if (n > 31)
		n = 31;
	memcpy (e->icon, e->name, (size_t) n);
	e->icon[n] = 0;
	str_copy_n (e->label, e->icon, 64);
	str_cat (path_join (fullpath, path, e->name), "/app.txt");
	cur_ent = e;
	ini_read (fullpath, man_line);
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

static void load_dir (void)
{
	hist_note ();
	nents = 0;
	scroll_ = 0;
	cursel = -1;
	last_idx = -1;
	msg = 0;
	DIR *d = opendir (path);
	if (!d)
	{
		set_err (kerr (errno));
		return;
	}
	struct dirent *de;
	long n = 0;
	while (n < MAXE && (de = readdir (d)))
	{
		if (!de->d_name[0] || de->d_name[0] == '.')
			continue;			/* caches : « .xxx » */
		struct ent *e = &ents[n];
		str_copy_n (e->name, de->d_name, 256);
		e->size = 0;
		e->kind = K_FILE;
		if (de->d_type == DT_DIR)
		{
			e->kind = K_DIR;
			if (str_ends (e->name, ".app"))
			{
				app_info (e);
				e->kind = K_APP;
			}
		}
		else
		{
			struct stat st;			/* (la taille : readdir de la libc ne la donne pas) */
			path_join (fullpath, path, e->name);
			if (stat (fullpath, &st) == 0)
				e->size = (unsigned long) st.st_size;
		}
		set_type (e);
		order[n] = n;
		n++;
	}
	closedir (d);
	nents = n;
	sort_ents ();
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
	*tx = ui.w - w_type - 6;
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
	if (w_size < COL_MIN || w_type < COL_MIN || ui.w - w_size - w_type - (6 + PAN_W + NAME_MIN) < 0)
	{
		w_size = COL_SIZE_W;
		w_type = COL_TYPE_W;
	}
}

/* separateur sous la souris (1 avant Taille, 2 avant Type, 0 aucun ; vue liste, en-tete) */
static long sep_at (long x, long y)
{
	long sx, tx;
	if (!view || y < LIST_Y || y >= LIST_Y + HDR_H)
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
		x = clamp (x, PAN_W + NAME_MIN, tx - COL_MIN);
		long old = w_size;
		w_size = tx - x;
		return old != w_size;
	}
	x = clamp (x, sx + COL_MIN, ui.w - 6 - COL_MIN);	/* avant Type : Taille ne bouge pas a gauche */
	long old = w_type;
	w_size = x - sx;
	w_type = ui.w - 6 - x;
	return old != w_type;
}

/* SD0:/etc/files.ini : « vue = ... », « taille = ... », « type = ... » */
static void save_cfg (void)
{
	char *e = str_cat (cfgbuf, view ? "vue = liste\n" : "vue = icones\n");
	e = fmt_dec ((unsigned long) w_size, str_cat (e, "taille = "));
	e = fmt_dec ((unsigned long) w_type, str_cat (e, "\ntype = "));
	*e++ = 10;
	*e = 0;
	int fd = open ("SD0:/etc/files.ini", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	write (fd, cfgbuf, (size_t) (e - cfgbuf));
	close (fd);
}

/* x de la premiere colonne de la grille (centree) */
static long grid_x0 (void) { return ((ui.w - PAN_W - cols * CELL_W) >> 1) + PAN_W; }

/* disposition de la vue : cols, body_y, line_h, rows */
static void layout (void)
{
	if (!view)
	{
		cols = (long) ((unsigned long) (ui.w - (PAN_W + 8)) / CELL_W);
		if (cols < 1)
			cols = 1;
		body_y = LIST_Y + 4;
		line_h = CELL_H;
	}
	else
	{
		cols = 1;
		body_y = LIST_Y + HDR_H;
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

/* (ini_read de files.ini) « vue = liste », « taille = <px> », « type = <px> » */
static void cfg_line (char *k, char *v)
{
	if (!strcmp (k, "vue"))
		view = !strcmp (v, "liste");
	else if (!strcmp (k, "taille"))
		w_size = (long) parse_dec (v);
	else if (!strcmp (k, "type"))
		w_type = (long) parse_dec (v);
}

/* nom : le selectionne s'il est dans la liste */
static void select_name (const char *name)
{
	for (long i = 0; i < nents; i++)
		if (!strcmp (ent_ptr (i)->name, name))
		{
			cursel = i;
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

static void go_sd0 (void) { go_dir ("SD0:/"); }

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
 *  Acces rapides (SD0:/etc/places.ini, « libelle = dossier », dans l'ordre ; seulement les
 *  dossiers presents)
 * --------------------------------------------------------------------------- */
static void places_load (void)
{
	pl_n = 0;
	int fd = open ("SD0:/etc/places.ini", O_RDONLY);
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
			if (*lab && *dir && stat (dir, &st) == 0 && S_ISDIR (st.st_mode))
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
		"I3\tSupprimer\tSuppr\n"
		"-\n"
		"I4\tActualiser\tCtrl+L\n"
		"-\n"
		"I5\tFermer\tCtrl+W\n"
		"MAller\n"
		"I10\tDossier parent\tRetour\n"
		"-\n";
	static const char tail[] = "-\n"
		"I11\tSD0:\n"
		"MAffichage\n"
		"I40\tIcônes\n"
		"I41\tListe\n"
		"MVolume\n"
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
	str_cat (e, pl_n ? tail : tail + 2);	/* (pas d'acces rapide : pas de separateur) */
}

/* ---------------------------------------------------------------------------
 *  Volumes (service vfs : VFS_VOLUMES, VFS_UNMOUNT)
 * --------------------------------------------------------------------------- */

/* nom en minuscules, source : ajoute un volume a vols */
static void add_vol (const char *low, const char *src)
{
	if (vol_n >= VMAX)
		return;
	struct vol *v = &vols[vol_n++];
	long i;
	for (i = 0; i < 13 && low[i]; i++)
	{
		v->low[i] = low[i];
		v->disp[i] = (char) (low[i] >= 'a' && low[i] <= 'z' ? low[i] - 32 : low[i]);
	}
	v->low[i] = 0;
	v->disp[i++] = ':';
	v->disp[i] = 0;
	for (i = 0; i < 23 && src[i]; i++)
		v->src[i] = src[i];
	v->src[i] = 0;
	v->rem = str_pre (v->src, "blk.usb");	/* amovible : « blk.usb... » */
}

/* vraw -> vols : SD0: d'abord (toujours present), puis les autres */
static void vol_build (void)
{
	vol_n = 0;
	long i;
	for (i = 0; i < vraw_n; i++)
		if (!strcmp ((char *) vraw + i * VE_SZ, "sd0"))
			break;
	if (i < vraw_n)
		add_vol ((char *) vraw + i * VE_SZ, (char *) vraw + i * VE_SZ + 16);
	else
		add_vol ("sd0", "blk.sd0p1");
	for (i = 0; i < vraw_n; i++)
		if (strcmp ((char *) vraw + i * VE_SZ, "sd0"))
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
	return TR (s);
}

/* -> volume du dossier courant (-1 : aucun) */
static long cur_vol (void)
{
	char low[16];
	long i;
	for (i = 0; path[i] != ':'; i++)
	{
		if (!path[i] || i >= 14)
			return -1;
		low[i] = (char) (path[i] >= 'A' && path[i] <= 'Z' ? path[i] + 32 : path[i]);
	}
	low[i] = 0;
	for (i = 0; i < vol_n; i++)
		if (!strcmp (vols[i].low, low))
			return i;
	return -1;
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
	vol_build ();
	if (vfs_ch >= 0 && cur_vol () < 0)
	{					/* le volume du dossier courant a disparu */
		go_sd0 ();
		set_msg ("Volume retiré : retour à SD0:");
	}
	return 1;
}

/* volume : y aller (sa racine) */
static void go_vol (long i)
{
	char *e = str_cat (path, vols[i].disp);
	*e++ = '/';
	*e = 0;
	load_dir ();
}

/* volume : le demonter (son service s'arrete) */
static void eject (long i)
{
	if (vfs_ch < 0)
		return;
	vmsg.op = VFS_UNMOUNT_;
	memcpy (vmsg.data, vols[i].low, 16);
	vmsg.len = 16;
	long r = vfs_call ();
	if (cur_vol () == i)			/* on y etait : retour a SD0:/ */
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
			select_name (tmpname);
		return;
	}
}

static void draw (void);

/* supprimer la selection, apres confirmation (menu surgissant) */
static void delete_sel (void)
{
	if (cursel < 0)
		return;
	struct ent *en = ent_ptr (cursel);
	char *e = tr_cat (popbuf, "Supprimer « ");	/* « Supprimer « nom » », « Annuler » */
	e = str_cat (e, ent_shown (en));
	e = tr_cat (e, " »") + 1;
	e = tr_cat (e, "Annuler");
	e[1] = 0;
	long x = scr[0], y = scr[1];		/* au milieu de l'ecran */
	if (!x)
	{
		x = 640;
		y = 480;
	}
	if (ui_popup ((x - 320) >> 1, (y >> 1) - 40, (long) popbuf, 320) != 0)
		return;				/* Annuler, Echap, clic ailleurs */
	make_full (cursel);
	struct stat st;
	long r = 0;
	if (stat (fullpath, &st) == 0 && S_ISDIR (st.st_mode) ? rmdir (fullpath) : unlink (fullpath))
		r = kerr (errno);
	load_dir ();
	if (r)
		set_err (r);
	else
		set_msg ("Supprimé");
}

/* point de la fenetre : menu de l'entree selectionnee (Ouvrir, Ouvrir avec... pour un fichier,
 * Supprimer) */
static void ctx_menu (long x, long y)
{
	if (cursel < 0)
		return;
	int file = ent_ptr (cursel)->kind == K_FILE;
	char *e = tr_cat (popbuf, "Ouvrir") + 1;
	if (file)
		e = tr_cat (e, "Ouvrir avec…") + 1;
	e = tr_cat (e, "Supprimer");
	e[1] = 0;
	long r = ui_popup_local (x, y, (long) popbuf, 220);
	if (r < 0)
		return;				/* Echap, clic ailleurs */
	if (r == 0)
		open_sel ();
	else if (r == 1 && file)
		open_with_sel ();
	else
		delete_sel ();
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

/* petit dossier (20 x 17) */
static void draw_folder (long x, long y, unsigned tab, unsigned body)
{
	ui_fill (x, y + 1, 8, 3, tab);
	ui_fill (x, y + 3, 20, 14, body);
}

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
	ui_rbox (x, y, w, h, 8, TOK (TJ_ACCENT_LIGHT), TOK (TJ_ACCENT_LIGHT));
}

/* -> element du panneau du dossier courant : 0x200 + n (acces rapide exact), n (volume qui le
 * contient), -1 */
static long cur_hl (void)
{
	for (long i = 0; i < pl_n; i++)
		if (!str_icmp (path, places[i].path))
			return 0x200 + i;
	return cur_vol ();
}

/* haut de la premiere ligne de volume (sous les acces rapides) */
static long vol_y0 (void) { return pl_n ? PAN_Y0 + pl_n * PROW_H + PL_GAP : PAN_Y0; }

static void draw_panel (void)
{
	ui_fill (0, 0, PAN_W - 1, H_, TOK (TJ_SUNKEN));
	ui_fill (PAN_W - 1, 0, 1, H_, TOK (TJ_LINE));
	long hl = cur_hl (), y0 = PAN_Y0;
	if (pl_n)
	{					/* acces rapides */
		text (TR ("Accès rapides"), 14, y0 - 10, TOK (TJ_TEXT2), 12, ui.font_b, 0);
		for (long i = 0; i < pl_n; i++)
		{
			long y = i * PROW_H + y0;
			if (hl == 0x200 + i)
				draw_sel (6, y + 1, PAN_W - 13, PROW_H - 2);
			draw_folder (14, y + 5, TOK (TJ_TEXT2), TOK (TJ_TEXT2));
			text (places[i].lab, 42, y + 19, TOK (TJ_TEXT1), 13, ui.font, PAN_W - 50);
		}
		y0 = vol_y0 ();
	}
	text (TR ("Volumes"), 14, y0 - 10, TOK (TJ_TEXT2), 12, ui.font_b, 0);
	for (long i = 0; i < vol_n; i++)
	{
		long y = i * VROW_H + y0;	/* haut de la ligne */
		if (y + VROW_H > H_)
			break;
		struct vol *v = &vols[i];
		if (hl == i)
			draw_sel (6, y + 1, PAN_W - 13, VROW_H - 2);
		ui_rbox (14, y + 10, 20, 15, 3, TOK (TJ_TEXT2), TOK (TJ_TEXT2));	/* disque */
		ui_fill (28, y + 19, 3, 3, TOK (TJ_SUNKEN));
		text (v->disp, 42, y + 16, TOK (TJ_TEXT1), 13, ui.font_b, PAN_W - 42 - 36);	/* « USB0: » */
		text (vol_kind (v), 42, y + 30, TOK (TJ_TEXT2), 12, ui.font, PAN_W - 42 - 36);	/* genre */
		if (v->rem)
			draw_eject (PAN_W - 22, y + 13);
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

/* path -> segments du fil d'Ariane (crumbs, cr_n ; noms dans crbuf). Trop profond : le dernier
 * segment reste le vrai */
static void crumbs_make (void)
{
	str_copy_n (crbuf, path, 272);
	char *start = crbuf;
	long n = 0;
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
static void draw_top (void)
{
	ui_fill (PAN_W, 0, W_ - PAN_W, TOP_H, TOK (TJ_SURFACE));
	ui_fill (PAN_W, TOP_H, W_ - PAN_W, 1, TOK (TJ_LINE));
	ui_fill (PAN_W, TOP_H + 1, W_ - PAN_W, LIST_Y - TOP_H - 1, TOK (TJ_SURFACE));
	crumbs_make ();
	for (long h = H_BACK; h <= H_UP; h++)
		draw_nav (h);
	draw_seg ();
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
	if (n && fullpath[n - 1] == ':')	/* « SD0: » : sa racine */
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
	text (s, x, LIST_Y + 16, TOK (TJ_TEXT2), 12, ui.font_b, max);
	if (sort_key != col)
		return;
	text (sort_desc ? "▼" : "▲", x + text_w (s, 12, ui.font_b) + 5, LIST_Y + 15, TOK (TJ_TEXT2), 9, ui.font, 16);
}

/* vue liste : en-tete (Nom, Taille, Type) et lignes */
static void draw_rows (void)
{
	char buf[32];
	long sx, tx;
	ui_fill (PAN_W, LIST_Y, W_ - PAN_W, H_ - (LIST_Y + SB_H), TOK (TJ_SURFACE));
	col_x (&sx, &tx);
	ui_fill (PAN_W, LIST_Y + HDR_H - 1, W_ - PAN_W, 1, TOK (TJ_LINE));
	long sw = tx - sx;			/* largeur de Taille */
	ui_fill (sx, LIST_Y + 4, 1, HDR_H - 8, TOK (TJ_LINE));	/* separateurs */
	ui_fill (tx, LIST_Y + 4, 1, HDR_H - 8, TOK (TJ_LINE));
	draw_hdr (TR ("Nom"), PAN_W + 42, S_NAME, sx - (PAN_W + 50));
	draw_hdr (TR ("Taille"), sx + 8, S_SIZE, sw - 12);
	draw_hdr (TR ("Type"), tx + 8, S_TYPE, W_ - tx - 18);
	for (long r = 0; r < rows; r++)
	{
		long i = scroll_ + r;		/* rang */
		if (i >= nents)
			break;
		long y = r * ROW_H + LIST_Y + HDR_H;	/* haut de la ligne */
		if (cursel == i)
			draw_sel (PAN_W + 4, y + 1, W_ - (PAN_W + 8), ROW_H - 2);
		struct ent *e = ent_ptr (i);
		if (e->kind == K_DIR)
			draw_folder (PAN_W + 12, y + 4, COL_FOLDER_D, COL_FOLDER);
		else if (e->kind == K_APP)
			ui_icon ((long) e->icon, PAN_W + 12, y + 3, 20);
		else
			draw_doc (PAN_W + 15, y + 4);
		if (e->kind == K_FILE)
		{				/* taille, alignee a droite */
			fmt_size (e->size, buf);
			long x = tx - 10 - text_w (buf, 12, ui.font);
			if (x < sx)
				x = sx;
			text (buf, x, y + 18, TOK (TJ_TEXT2), 12, ui.font, sw - 4);
		}
		text (e->type, tx + 8, y + 18, TOK (TJ_TEXT2), 12, ui.font, W_ - tx - 18);
		long m = sx - (PAN_W + 50);	/* nom */
		text (ent_shown (e), PAN_W + 42, y + 18, TOK (TJ_TEXT1), 13, ui.font, m < 8 ? 8 : m);
	}
	long k = drag, h = H_ - (LIST_Y + SB_H);	/* separateur saisi (toute la hauteur) */
	if (!k)
	{
		k = hover;			/* ou survole (l'en-tete) */
		h = HDR_H;
	}
	if (k)
		ui_fill ((k == 1 ? sx : tx) - 1, LIST_Y, 2, h, TOK (TJ_ACCENT));
}

/* grand dossier (48 x 44) */
static void draw_folder_big (long x, long y)
{
	ui_rbox (x + 2, y + 4, 20, 10, 3, COL_FOLDER_D, COL_FOLDER_D);
	ui_rbox (x, y + 9, ICON_S, 35, 4, COL_FOLDER, COL_FOLDER);
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
	ui_fill (PAN_W, LIST_Y, W_ - PAN_W, H_ - (LIST_Y + SB_H), TOK (TJ_SURFACE));
	long gx = grid_x0 ();
	for (long c = 0; c < rows * cols; c++)
	{
		long i = scroll_ * cols + c;	/* rang */
		if (i >= nents)
			break;
		long x = (c % cols) * CELL_W + gx, y = (c / cols) * CELL_H + body_y;
		if (cursel == i)
			draw_sel (x + 3, y + 1, CELL_W - 6, CELL_H - 2);
		struct ent *e = ent_ptr (i);
		long ix = x + (CELL_W - ICON_S) / 2, iy = y + 5;
		if (e->kind == K_DIR)
			draw_folder_big (ix, iy);
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
	long y = H_ - SB_H;
	ui_fill (PAN_W, y, W_ - PAN_W, SB_H, TOK (TJ_SURFACE));
	ui_fill (PAN_W, y, W_ - PAN_W, 1, TOK (TJ_LINE));
	const char *s = msg;
	if (!s)
	{
		char *e = tr_cat (fmt_dec ((unsigned long) nents, stbuf), nents == 1 ? " élément" : " éléments");
		if (cursel >= 0)
		{
			struct ent *en = ent_ptr (cursel);
			e = str_cat (str_cat (e, "   —   "), ent_shown (en));
			if (en->kind == K_FILE)
				fmt_size (en->size, str_cat (e, ", "));
		}
		s = stbuf;
	}
	text (s, PAN_W + 12, H_ - 8, TOK (TJ_TEXT2), 12, ui.font, W_ - (PAN_W + 24));
}

static void draw (void)
{
	tk = (const unsigned *) ui_tokens ();
	W_ = ui.w;
	H_ = ui.h;
	draw_panel ();
	draw_top ();
	if (view)
		draw_rows ();
	else
		draw_grid ();
	draw_status ();
	ui_update_all ();
}

/* fonction de ui_on_resize : colonnes, rangees et dessin suivent la fenetre */
static long on_resize (long w, long h)
{
	(void) w;
	(void) h;
	cols_fix ();
	layout ();
	keep_visible ();
	draw ();
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Entrees
 * --------------------------------------------------------------------------- */

/* entree de menu choisie */
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
	case ID_PARENT: go_parent (); break;
	case ID_SD0: go_sd0 (); break;
	case ID_EJECT: eject_cur (); break;
	case ID_ICONS: set_view (0); break;
	case ID_LIST: set_view (1); break;
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
		return;
	long s = sep_at (x, y), ch = s != hover;
	hover = s;
	long h = head_at (x, y);
	if (h != hhot)
	{
		ch = 1;
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
	if (x < PAN_W)
	{
		if ((unsigned long) y < TOP_H - 8)
		{				/* le haut du panneau deplace la fenetre */
			ui_window_drag ();
			return;
		}
		if (y < PAN_Y0)			/* panneau : acces rapides puis volumes */
			return;
		long r = (y - PAN_Y0) / PROW_H;
		if (r < pl_n)
			go_dir (places[r].path);
		else
		{
			long y0 = vol_y0 ();
			if (y < y0)
				return;
			r = (y - y0) / VROW_H;
			if (r >= vol_n)
				return;
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
		if (!h)
			return;
		head_click (h);
		draw ();
		return;
	}
	if (view)
	{					/* en-tete des colonnes */
		if (y < LIST_Y)
			return;
		if (y < LIST_Y + HDR_H)
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
	if (empty || i >= nents)
	{					/* dans le vide : plus de selection */
		cursel = -1;
		last_idx = -1;
		draw ();
		return;
	}
	if (ui_ev_field (&ui, EV_CODE) & 2)
	{					/* bouton droit : le menu de l'entree */
		cursel = i;
		draw ();
		ctx_menu (x, y);
		draw ();
		return;
	}
	long now = time_ms ();
	if (last_idx == i && (unsigned long) (now - last_time) < DCLICK_MS)
	{					/* double-clic */
		last_idx = -1;
		cursel = i;
		open_sel ();
	}
	else
	{
		last_idx = i;
		last_time = now;
		cursel = i;
		msg = 0;			/* le message laisse place a la selection */
	}
	draw ();
}

static void on_key (void)
{
	if (!ui_ev_field (&ui, EV_KIND))
		return;				/* relache */
	long k = ui_ev_field (&ui, EV_CODE);
	if (ui_ev_field (&ui, EV_MODS) & UI_MOD_CTRL)
	{
		if (k == KEY_N_)
			new_folder ();
		else if (k == KEY_L_)
			refresh ();
		else if (k == KEY_W_)
			quit ();
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
	case KEY_F5_:
		refresh ();
		break;
	default:
		return;
	}
	if (set)
	{
		cursel = cs;
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
	ents = (struct ent *) calloc (MAXE, sizeof *ents);
	if (!ents)
		return 1;
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
	places_load ();
	menu_build ();
	ui_menu ((long) menu_buf);		/* barre de menus globale */
	ini_read ("SD0:/etc/files.ini", cfg_line);	/* vue choisie */
	cols_fix ();
	layout ();
	str_copy_n (path, argc > 1 && argv[1][0] ? argv[1] : "SD0:/", 256);	/* dossier initial : argument ou SD0:/ */
	vfs_ch = sys3 (SYS_SVC_OPEN_, (long) "vfs", 500);
	load_dir ();
	vol_sub = bus_subscribe ((long) BUS_TOPIC_VOLUME "*", 0);	/* montages et demontages : le bus */
	if (vol_sub < 0)
		vol_sub = 0;
	last_poll = time_ms ();
	vol_poll ();
	draw ();
	for (;;)
	{
		long t = ui_event (500);
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_RESIZE)
		{
			long code, taken;	/* (le kit prend la taille, puis on_resize) */
			uikit_w_event (&code, &taken);
		}
		else if (t == WEV_MOUSE)
			on_mouse ();
		else if (t == WEV_KEY)
			on_key ();
		else if (t == WEV_MENU)
			on_menu (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_BUS)
		{				/* un volume monte ou demonte */
			if (ui_ev_field (&ui, BEV_SUB) == vol_sub && vol_poll ())
				draw ();
		}
		else if (t == WEV_WHEEL)
		{				/* 3 lignes (liste), 1 rangee (icones) */
			set_scroll (scroll_ - ui_ev_field (&ui, EV_Y) * (view ? 3 : 1));
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
