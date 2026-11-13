/*
 * control.h -- le cadre du panneau de configuration (Réglages) et l'interface d'un volet.
 *
 * Port en C++ de control.asm et applet.inc (Korin). Le control d'Onyx (user/Apps/control/main.cpp) lançait chaque
 * volet comme une app à part qui dessinait dans une surface partagée ; Korin, lui, réunit tout dans un seul
 * programme : c'est ce que garde ce port (l'asm de Korin fait foi pour ce qui se voit).
 *
 * La fenêtre : la liste des volets à gauche (SIDE_W), le volet choisi à droite. Un volet (applets/<nom>.cpp) est
 * un Applet : son nom (l'argument de « control <nom> »), son libellé dans la liste, son titre, son icône, et deux
 * fonctions, open et apply. open remplit l'état du volet (ap, remis à zéro avant chaque ouverture) :
 *   * des lignes (applet_row : titres, choix uniques par groupe, cases, notes, curseurs) en deux colonnes, que le cadre
 *     dessine, fait défiler et coche ; apply les relit (applet_choice, ap.rows) ;
 *   * ou des lignes de réglages (fm_begin, fm_line, fm_item : une carte de listes déroulantes) ;
 *   * ou son propre dessin (ap.draw, ap.click…) et ses widgets (ap.widgets, ap.close pour les retirer).
 * Le bas du volet (trait, message ap.msg, bouton « Appliquer » ou ap.button) est au cadre, sauf ap.widgets == 1.
 * Tous les textes passent par ui_tr (la clé est le texte français, lang/en.txt).
 */
#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>
#include <uikit.h>

/* ---- les mesures du cadre (applet.inc) ---- */
enum
{
	R_TITLE = 0,				/* genres de ligne : titre */
	R_CHOICE = 1,				/* choix unique dans son groupe */
	R_CHECK = 2,				/* case à cocher */
	R_NOTE = 3,
	R_SLIDER = 4,				/* un curseur (widget ui_slider, dans value), sa valeur et text (l'unité,
						 * « % ») à droite : le cadre le place et le cache avec sa ligne */
	ROWS_MAX = 208,
	ROW_H = 26,
	COL_W = 210,
	AP_COLS = 2,
	AP_MARGIN = 16,
	AP_HEAD = 48,				/* titre du volet */
	AP_TITLE_Y = 32,			/* sa ligne de base (20 px) */
	AP_SUB_Y = 50,				/* celle du sous-titre (12 px) */
	AP_BOTTOM = 56,				/* message + bouton */
	SIDE_W = 160,				/* la liste des volets */
	SIDE_H = 40,				/* une ligne de la liste */
	SIDE_IC = 24,
	BTN_W = 96,
	BTN_H = 28,
	AP_FILE_MAX = 4096,
	AP_WIN_W = SIDE_W + 700,		/* la fenêtre : le volet d'Onyx, 700 x 470 */
	AP_WIN_H = AP_HEAD + 470,		/* sous le titre, si l'écran le permet */
	FM_N = 4,				/* lignes de réglages */
	FM_H = 48,				/* une ligne */
	FM_LW = 194,				/* sa liste déroulante */
	FM_LH = 30,
	FM_PAD = 10,				/* marge dans la carte */
	FM_RADIUS = 10,
	FM_Y_SUB = 66,				/* haut de la carte, sous un sous-titre */
};

/* une ligne du volet (ROW_SIZE de l'asm) */
struct Row
{
	const char *text;			/* texte français (traduit au dessin) */
	unsigned char kind, group, checked;
	long value;				/* libre */
};

/* l'état du volet ouvert (applet_data) : remis à zéro par le cadre avant chaque open, sauf percol */
struct Pane
{
	long nrows;
	Row rows[ROWS_MAX];
	long percol;				/* lignes par colonne (d'après la hauteur de la fenêtre) */
	long colw;				/* largeur d'une colonne de lignes (COL_W ; plus : une seule colonne) */
	const char *title;			/* titre du volet */
	const char *sub;			/* sous-titre (0 : aucun) */
	int surface;				/* 1 : volet sur SURFACE, pas FACE */
	void (*extra) (long x, long y, long w);	/* dessin dans la 2e colonne (aperçu) */
	int (*xclick) (long x, long y);		/* clic sur ce dessin -> 1 si pris */
	void (*draw) (void);			/* dessin libre du volet, à la place des lignes */
	int (*click) (long x, long y);		/* (avec draw) clic -> 1 : changé */
	int (*wheel) (long n);			/* crans de molette -> 1 : changé */
	int (*tick) (void);			/* chaque seconde (250 ms avec des widgets) -> 1 : redessiner */
	int (*key) (void);			/* touche (ui.ev) -> 1 : prise (et redessiner) */
	int (*pad) (void);			/* WEV_PAD (ui.ev) -> 1 : redessiner */
	const char *button;			/* libellé du bouton du bas (0 : Appliquer) */
	int widgets;				/* 1 : volet en widgets, à lui tout le volet ; 2 : le bas reste au cadre */
	void (*close) (void);			/* à sa fermeture (retirer ses widgets) */
	int redraw;				/* 1 : tout redessiner (posé par un widget) */
	long scroll;				/* première ligne montrée */
	const char *msg;			/* message sous les lignes (0 : aucun) */
};

/* un volet de la liste (une entrée de la table « applets » de control.asm) */
struct Applet
{
	const char *name;			/* l'argument de « control <nom> » */
	const char *label;			/* son libellé dans la liste */
	const char *title;			/* son titre dans le volet */
	const char *icon;			/* l'icône (ui_icon : le nom d'une app) */
	void (*open) (void);
	void (*apply) (void);			/* Appliquer (le bouton, Entrée) */
	const char *needs = 0;			/* un fichier sans lequel le volet n'est pas montré (0 : aucun) */
};

extern struct ui_vars ui;
extern Pane ap;
extern long ap_w, ap_h;			/* la taille de la fenêtre */
extern long ap_net_sub;			/* abonnement à korin.net.* (0 : pas de bus) */
extern int ap_net_update;		/* le réseau a été publié : état à relire (applet Wi-Fi) */
extern char ap_file[AP_FILE_MAX];	/* tampons de travail des volets (ceux de l'asm) */
extern char ap_out[AP_FILE_MAX + 128];

#define TR(s)		((const char *) ui_tr ((long) (s)))
#define P_OPTIONS	"SD0:/etc/options.txt"
#define P_SYSTEM	"SD0:/etc/system.ini"
#define P_THEME		"SD0:/etc/theme.txt"
#define CRLF		"\r\n"

/* un jeton du thème (TJ_* : décalage en octets), tel quel (ui_rbox et ui_fill ne gardent que 24 bits) */
static inline uint32_t tok (int off) { return ((const uint32_t *) ui_tokens ())[off / 4]; }
static inline long F (void) { return (long) ui.font; }
static inline long FB (void) { return (long) ui.font_b; }

/* ---- main.cpp (control.asm) ---- */
void draw (void);			/* toute la fenêtre : la liste, puis le volet */
long ap_fit (void);			/* l'écran a changé : la fenêtre à sa taille -> 1 si elle a changé */

/* ---- applet.cpp (applet.inc) ---- */
long applet_row (const char *text, int kind, int group = 0, int checked = 0, long value = 0);
Row *row_ptr (long i);
long applet_choice (int group);		/* -> valeur de la ligne cochée du groupe (-1 : aucune) */
void ap_border (void);			/* ap.scroll ramené dans les lignes */
int pane_wheel (long n);
int pane_click (long x, long y);	/* -> 0 rien, 1 Appliquer, 2 ligne changée */
void pane_draw (void);
void ap_button (long x, long y, const char *label, int primary);

/* lignes de réglages : une carte « libellé à gauche, liste déroulante à droite », une note dessous */
extern long fm_n;
extern long fm_w[FM_N];
void fm_begin (const char *sub, const char *note);
long fm_top (void);			/* -> y du haut de la carte */
void fm_line (const char *label);
void fm_item (long line, const char *text, int current);
void fm_choose (long line, long item);
long fm_value (long line);
void fm_change (long w, long code);	/* (ui_w_on) un changement efface le message */
void fm_place (void);
void fm_draw (void);
void fm_close (void);

/* aides */
char *tr_cat (char *dst, const char *piece);	/* ajoute la traduction du morceau (espaces autour gardés) -> fin */
char *ini_trim (char *s);
char *ini_field (char *s, char **rest);
/* chaque ligne « clé = valeur » (clé en minuscules, les deux sans blancs autour ; « # » commente) -> 0 si le
 * fichier manque */
int ini_read (const char *path, void (*fn) (char *key, char *val, void *data), void *data = 0);
long ini_set (const char *path, const char *key, const char *val);	/* val 0 : retirer la clé */
long file_write (const char *path, const char *text);
long proc_find (const char *name, long after);
long spawn (const char *path, const char *arg);	/* SYS_SPAWN d'AppKit -> pid ou erreur */
void relaunch (const char *name, const char *path);	/* arrête puis relance un programme de la session */
long sys_ms (void);			/* SYS_TIME_MS */
void sys_sleep (long ms);		/* SYS_SLEEP */
long svc_open (const char *name, long wait_ms);
long svc_call (long ch, void *msg);	/* message de 512 octets */

/* ---- applist.cpp (src/inc/applist.inc) : le catalogue des applications ---- */
enum { APP_MAX = 128, APP_NAME = 32, APP_LABEL = 40, CAT_MAX = 24, CAT_NAME = 24 };
struct AppInfo
{
	char name[APP_NAME];			/* paquet sans .app */
	char label[APP_LABEL];			/* name= du manifeste (sinon le nom) */
	unsigned char cat;			/* catégorie */
};
extern long apps_n, cats_n;
extern AppInfo apps[APP_MAX];
extern char cats[CAT_MAX][CAT_NAME];
void applis_load (void);
long app_find (const char *name);
long cat_find (const char *name);
long app_launch (long app);

/* ---- partagés entre volets ---- */
long display_density_file (void);	/* display.cpp : 1 si theme.txt dit « density = touch » */
extern const char *const theme_topic;	/* theme.cpp : korin.theme */
extern char theme_bus[64];		/* sa valeur */

/* ---- les volets (applets/<nom>.cpp) ---- */
extern const Applet theme_applet, keyboard_applet, display_applet, dock_applet, panel_applet, wifi_applet,
	remote_applet, sound_applet, mouse_applet, padconf_applet, preload_applet, dotnet_applet, pkgman_applet, printers_applet;

#endif
