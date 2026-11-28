/* taskfile.h : une tâche planifiée (docs/TACHES.md), commune au service taskd, à la commande tasks et au volet
 * « Tâches planifiées » de Réglages. Du C sans aucune bibliothèque (ni korinrt, ni libc) : le même source se compile
 * dans le runtime du système et dans le C++ de Réglages.
 *
 * Une tâche est un fichier « clé = valeur » (<racine>/etc/tasks/<id>.task, racine : « » pour le système, le dossier
 * du compte pour l'utilisateur) :
 *
 *	name    = Sauvegarde des notes
 *	enabled = 1
 *	kind    = program                  program (chemin [arguments]) | app (nom d'une app) | shell (ligne de bash)
 *	run     = /home/steph/bin/backup.sh
 *	when    = daily 03:00              une ou plusieurs lignes (la tâche part à la première qui arrive)
 *	catchup = 1                        un passage manqué (machine éteinte) est rattrapé au démarrage
 *	delay   = 0                        secondes entre le déclencheur et le lancement
 *	timeout = 0                        secondes, puis le programme est arrêté (0 : sans limite)
 *	ac_only = 0                        seulement sur secteur
 *	net_only = 0                       seulement avec le réseau
 *	notify  = fail                     never | fail | always : une notification à la fin
 *
 * Les déclencheurs (when) :
 *	every <n> m|h|d                    toutes les n minutes, heures, jours (depuis le dernier passage)
 *	daily HH:MM                        chaque jour
 *	weekly mon,tue,wed,thu,fri,sat,sun HH:MM
 *	monthly <jour> HH:MM               le jour du mois (31 : le dernier, dans un mois plus court)
 *	once AAAA-MM-JJ HH:MM              une seule fois
 *	startup                            système : au démarrage ; utilisateur : à l'ouverture de sa session
 *	shutdown                           système : à l'extinction ; utilisateur : à la fermeture de sa session
 *	event <sujet> [valeur]             un sujet du bus (docs/BUS.md ; « préfixe* » permis) ; valeur : motif avec * et ?
 *	removed <sujet>                    le sujet retiré du bus (une clé débranchée, un volume démonté)
 *	power ac | power battery           passage sur secteur, sur batterie (korin.power.battery)
 *	battery <n>                        la batterie descend sous n %, sans secteur
 * Un déclencheur d'événement part quand la condition devient vraie (pas à chaque publication qui la confirme). */
#ifndef KORIN_TASKFILE_H
#define KORIN_TASKFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#define TK_ID 48
#define TK_NAME 80
#define TK_RUN 480
#define TK_TOPIC 64
#define TK_VALUE 96
#define TK_WHEN_MAX 4
#define TK_FILE 2048 /* un fichier .task au plus */

enum tk_kind { TK_PROGRAM = 0, TK_APP = 1, TK_SHELL = 2 };

enum tk_type {
	TW_NONE = 0,
	TW_EVERY,
	TW_DAILY,
	TW_WEEKLY,
	TW_MONTHLY,
	TW_ONCE,
	TW_STARTUP,
	TW_SHUTDOWN,
	TW_EVENT,
	TW_REMOVED,
	TW_POWER,   /* n : 1 secteur, 0 batterie */
	TW_BATTERY, /* n : le seuil en % */
};

enum tk_notify { TN_NEVER = 0, TN_FAIL = 1, TN_ALWAYS = 2 };

struct tk_when {
	int type;
	long n;     /* every : le nombre ; power, battery */
	long unit;  /* every : secondes par unité (60, 3600, 86400) */
	int days;   /* weekly : bit 0 dimanche ... bit 6 samedi */
	int hour, min;
	int mday;   /* monthly */
	int year, month, day; /* once */
	char topic[TK_TOPIC]; /* event, removed */
	char value[TK_VALUE]; /* event : le motif de la valeur (vide : toute valeur) */
};

struct task {
	char id[TK_ID]; /* le nom du fichier, sans .task */
	char name[TK_NAME];
	int enabled;
	int kind;
	char run[TK_RUN];
	int nwhen;
	struct tk_when when[TK_WHEN_MAX];
	int catchup;
	long delay, timeout; /* secondes */
	int ac_only, net_only;
	int notify;
};

/* une date locale (secondes depuis 1970, l'heure du fuseau comprise) */
struct tk_tm {
	int year, month, day, hour, min, sec;
	int wday; /* 0 dimanche */
};
void tk_civil(long secs, struct tk_tm *t);
long tk_secs(int year, int month, int day, int hour, int min, int sec);
int tk_mdays(int year, int month); /* jours du mois */

/* le texte d'un fichier .task (modifié : coupé en lignes) -> 0, ou -1 s'il n'a rien de valable ; t remis aux valeurs
 * par défaut d'abord (enabled 1, notify fail, catchup 1) ; l'id n'est pas touché */
int tk_parse(struct task *t, char *text);
void tk_defaults(struct task *t);
/* le fichier d'une tâche -> octets écrits dans out (cap au moins TK_FILE) */
long tk_format(const struct task *t, char *out, long cap);

/* un déclencheur : « daily 03:00 »... -> 1 si compris */
int tk_when_parse(struct tk_when *w, const char *s);
long tk_when_format(const struct tk_when *w, char *out, long cap);

/* déclencheur de calendrier (daily, weekly, monthly, once) : la prochaine heure locale strictement après after
 * -> secondes locales, -1 : jamais (once passé, autre type) */
long tk_next(const struct tk_when *w, long after);
int tk_is_calendar(int type);
int tk_is_event(int type); /* event, removed, power, battery : un sujet du bus */
/* le sujet du bus d'un déclencheur d'événement (power, battery : korin.power.battery) */
const char *tk_when_topic(const struct tk_when *w);
/* le déclencheur est-il vrai pour cette valeur du sujet (len < 0 : sujet retiré) ? */
int tk_when_holds(const struct tk_when *w, const char *value, long len);

/* motif du bus (nom exact, ou préfixe suivi de *) contre un sujet -> 1 */
int tk_topic_match(const char *pattern, const char *topic);
/* motif de valeur (* : n'importe quoi, ? : un caractère) -> 1 ; motif vide : 1 */
int tk_glob(const char *pat, const char *s);
/* un id convenable (lettres minuscules, chiffres, - et _, 1 à TK_ID - 1) -> 1 */
int tk_id_ok(const char *id);
/* un id tiré du nom (« Sauvegarde des notes » -> « sauvegarde-des-notes ») dans out (TK_ID) */
void tk_id_from_name(const char *name, char *out);

/* chaînes, sans bibliothèque */
long tk_len(const char *s);
void tk_copy(char *d, const char *s, long cap);
int tk_eq(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif
