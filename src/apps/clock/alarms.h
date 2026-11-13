//
// alarms.h -- les alarmes de l'Horloge : le modèle d'alarms.h d'Onyx (user/Apps/clock : struct Alarm, AlarmSet,
// alarm_next, alarms_next, alarm_passed, alarm_once_day, alarm_repeat_kind...), sur le fichier de Korin que clockd
// sonne : SD0:/etc/alarms.txt, au format de src/inc/alarms.inc (et de src/sys/common/alarms.c, sa traduction pour
// clockd) : mêmes clés, même ordre, mêmes bornes, pour que l'Horloge et clockd se relisent l'un l'autre.
//
//	snooze = 10		minutes du report (1..30), avant tout bloc (clé de Korin)
//	[alarm]
//	id     = 1		jamais réutilisé tant que le fichier vit
//	time   = 07:00		HH:MM, heure locale (invalide : jamais sonnée)
//	label  = Ecole		UTF-8, ALARM_LABEL_MAX octets au plus (Korin ; Onyx : 40 caractères)
//	on     = 1
//	days   = mon tue wed thu fri	(vide : une fois, le jour de « date »)
//	date   = 20261105	une fois : le jour, AAAAMMJJ
//	sound  = chimes		chimes | beeps | marimba
//	snooze = 202611050710	AAAAMMJJHHMM : sonne encore à cette minute
//	missed = 202611050700	(seulement si posée) une sonnerie sans réponse
//
// Ce qu'Onyx a en plus et que Korin n'a pas : le bloc [timer] (le minuteur passé à clockd), les clés inconnues
// gardées (fk_kv de FileKit), le Ringer (il est dans clockd). Minutes : heure locale, jours * 1440 + h * 60 + m.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef CLOCK_ALARMS_H
#define CLOCK_ALARMS_H

#define ALARMS_PATH	"SD0:/etc/alarms.txt"
#define ALARMS_TOPIC	"korin.clock.alarms"	// publié après chaque écriture (docs/BUS.md)
#define ALARMS_MAX	20
#define ALARM_LABEL_MAX	87			// octets, sans le 0 (AL_LABEL_MAX de l'asm)
#define ALARM_SNOOZE_DEF 10
#define ALARM_SOUNDS	3			// 0 carillon, 1 bips, 2 marimba
enum { AL_MON = 1, AL_TUE = 2, AL_WED = 4, AL_THU = 8, AL_FRI = 16, AL_SAT = 32, AL_SUN = 64 };
#define AL_WEEKDAYS	(AL_MON | AL_TUE | AL_WED | AL_THU | AL_FRI)
#define AL_WEEKENDS	(AL_SAT | AL_SUN)
#define AL_EVERYDAY	(AL_WEEKDAYS | AL_WEEKENDS)

struct Alarm
{
	int  id;		// le numéro du fichier
	bool valid;		// time = HH:MM lu (sinon « Invalide », jamais sonnée)
	int  hh, mm;
	bool on;
	unsigned days;		// AL_* ; 0 : une fois
	long date;		// une fois : le jour (clk_days), -1 aucun
	char label[ALARM_LABEL_MAX + 1];
	int  sound;		// 0..ALARM_SOUNDS-1 (Onyx : le mot ; Korin : son numéro)
	long snooze;		// la minute du report, -1
	long missed;		// la minute de la dernière sonnerie sans réponse, -1
};
struct AlarmSet
{
	Alarm a[ALARMS_MAX];
	int  n;
	int  next_id;		// le numéro d'une nouvelle
	int  snooze_min;	// snooze = en tête (Korin)
};

// ---- les dates (clocktime.h d'Onyx) ----
long clk_days (long y, long mo, long d);		// les jours depuis le 1/1/1970 (H. Hinnant)
void clk_civil (long days, long *y, long *mo, long *d);
int  clk_wday (long days);				// 0 lundi .. 6 dimanche
long clk_now_minute (void);				// la minute locale (SYS_CLOCK_GET 2 : comme al_now de l'asm)

// ---- le fichier ----
int  alarms_load (AlarmSet &s);				// SD0:/etc/alarms.txt -> le nombre (absent : aucune)
int  alarms_save (const AlarmSet &s);			// écrit -> 0, -1 : échec
void alarms_publish (void);				// korin.clock.alarms (BusKit) : clockd relit

// ---- une alarme ----
void alarm_blank (Alarm &a);				// 07:00, active, une fois, carillon, sans nom ni report (pas valide)
int  alarm_find (const AlarmSet &s, int id);		// son rang, -1
int  alarm_remove (AlarmSet &s, int id);		// -> 1, 0 aucune
void alarm_set_label (Alarm &a, const char *utf8);	// coupé sur un caractère à ALARM_LABEL_MAX octets, tabulation -> espace
enum { REP_ONCE, REP_DAILY, REP_WEEKDAYS, REP_WEEKENDS, REP_CUSTOM };
int  alarm_repeat_kind (unsigned days);

// ---- quand ----
long alarm_once_day (int hh, int mm, long now_min);	// une fois : aujourd'hui si hh:mm est encore à venir, sinon demain
long alarm_next (const Alarm &a, long from_min);	// la première minute >= from_min de son programme, -1 jamais
long alarms_next (const AlarmSet &s, long now_min, int *idx);	// la plus proche sonnerie après now_min, reports compris
bool alarm_passed (const Alarm &a, long now_min);	// une alarme d'une fois dont la minute est passée

// (les sons, sounds.h d'Onyx : dans main.cpp, avec gamesound.h)

#endif
