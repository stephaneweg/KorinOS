/* alarms.h : les alarmes de l'Horloge (le modèle d'alarms.h d'Onyx), comme src/inc/alarms.inc,
 * partagées par l'Horloge (src/apps/clock : elle les édite) et clockd (src/sys/clockd : il les
 * sonne). Pas d'interface ici : le fichier, les dates, la prochaine sonnerie, les sons.
 *
 * SD0:/etc/alarms.txt (le format d'Onyx : des lignes « clé = valeur », un bloc [alarm] par
 * alarme, '#' et ';' commencent un commentaire) :
 *	snooze = 10		minutes du report (1..30), avant tout bloc
 *	[alarm]
 *	id     = 1		jamais réutilisé tant que le fichier vit
 *	time   = 07:00		HH:MM, heure locale (invalide : jamais sonnée)
 *	label  = Ecole		UTF-8, AL_LABEL_MAX octets au plus
 *	on     = 1
 *	days   = mon tue wed thu fri	(vide : une fois, le jour de « date »)
 *	date   = 20261105	une fois : le jour, AAAAMMJJ
 *	sound  = chimes		chimes | beeps | marimba
 *	snooze = 202611050710	AAAAMMJJHHMM : sonne encore à cette minute
 *	missed = 202611050700	(seulement si posée) une sonnerie sans réponse
 * L'Horloge et clockd l'écrivent tous deux, puis publient le sujet korin.clock.alarms
 * (docs/BUS.md) : l'autre le relit.
 *
 *	sys.conf : import = audiokit, buskit ; sources = ../common/alarms.c, ../common/gamesnd.c
 *
 * Minutes : heure locale, jours * 1440 + h * 60 + m. */
#ifndef KORIN_ALARMS_H
#define KORIN_ALARMS_H

#include "sys_consts.h"
#include "gamesnd.h"

#define AL_MAX 20
#define AL_LABEL_MAX 87 /* octets (sans le 0) */
#define AL_FBUF 8192
#define AL_DAYS_ALL 0x7F
#define AL_WEEKDAYS 0x1F
#define AL_WEEKEND 0x60
#define AL_SNOOZE_DEF 10
#define AL_SOUNDS 3

/* une alarme (AL_SIZE = 128 octets, champs AL_* de l'asm) */
struct alarm {
	u32 id;     /* AL_ID */
	u8 hh, mm;  /* AL_HH, AL_MM */
	u8 on;      /* AL_ON */
	u8 valid;   /* AL_VALID */
	u8 days;    /* AL_DAYS : bit 0 lundi .. bit 6 dimanche ; 0 : une fois */
	u8 sound;   /* AL_SOUND : 0 carillon, 1 bips, 2 marimba */
	u8 pad[6];
	i64 date;   /* AL_DATE : une fois, jours depuis 1970 (-1 : aucun) */
	i64 snooze; /* AL_SNOOZE : minute du report, -1 */
	i64 missed; /* AL_MISSED : minute de la dernière sonnerie sans réponse, -1 */
	char label[AL_LABEL_MAX + 1]; /* AL_LABEL */
};

extern u64 al_n;
extern u64 al_next_id;
extern u64 al_snz; /* minutes du report */
extern struct alarm al_tab[AL_MAX];
extern const char al_path[];  /* SD0:/etc/alarms.txt */
extern const char al_topic[]; /* korin.clock.alarms */

/* dates */
u64 al_days(u64 y, u64 m, u64 d);                /* -> jours depuis 1970 (H. Hinnant) */
void al_civil(u64 days, u64 *y, u64 *m, u64 *d); /* l'inverse */
u64 al_wday(u64 days);                           /* 0 lundi .. 6 dimanche */
i64 al_now(u64 *secs);                           /* minute locale ; *secs : secondes locales (0 : sans) */

/* la première minute >= from où elle sonne d'après son programme (valide ; « on » et le report
 * non regardés), -1 : jamais */
i64 al_next(const struct alarm *a, i64 from);
/* la plus proche sonnerie après now des alarmes actives, reports compris (-1 : aucune) ;
 * *idx : son numéro (-1) */
i64 al_next_all(i64 now, i64 *idx);
/* 1 : une alarme d'une fois dont la minute est passée (et pas de report à venir) */
int al_passed(const struct alarm *a, i64 now);
/* le jour d'une alarme d'une fois : aujourd'hui si hh:mm est encore à venir, sinon demain */
u64 al_once_day(u64 hh, u64 mm, u64 now);
struct alarm *al_find(u32 id); /* 0 : aucune */
void al_blank(struct alarm *a); /* 07:00, active, une fois, carillon, sans nom ni report */
int al_remove(u32 id);          /* retirée de la table -> 1, 0 : aucune */

/* lecture */
int al_parse_hm(const char *s, u64 *hh, u64 *mm); /* « HH:MM » ou « H:MM » -> 1 valide */
i64 al_parse_day(const char *s);   /* « AAAAMMJJ » -> jours, -1 */
i64 al_parse_stamp(char *s);       /* « AAAAMMJJHHMM » -> minute, -1 (s modifié puis rendu) */
u64 al_parse_days(const char *s);  /* « mon tue ... » -> bits */
void al_label_copy(char *d, const char *s); /* d : AL_LABEL_MAX + 1 */
u64 al_load(void);                 /* SD0:/etc/alarms.txt -> table ; nombre */

/* écriture : chaque fonction rend la fin (sur le 0) */
char *al_2d(char *d, u64 v);
char *al_fmt_hm(char *d, const struct alarm *a);   /* « HH:MM » */
char *al_fmt_min_hm(char *d, u64 min);             /* « HH:MM » de la minute */
char *al_fmt_day(char *d, i64 days);               /* « AAAAMMJJ » (-1 : rien) */
char *al_fmt_stamp(char *d, i64 min);              /* « AAAAMMJJHHMM » (-1 : rien) */
char *al_fmt_days(char *d, u64 bits);              /* « mon tue ... » */
long al_save(void);                                /* -> 0, -1 : échec */
void al_publish(void);                             /* korin.clock.alarms (BusKit) */

/* son 0..2 -> sa table (gs_seq) ; *round : la durée d'un tour (ms) */
const struct gs_ent *al_sound(u64 n, u64 *round);

#endif
