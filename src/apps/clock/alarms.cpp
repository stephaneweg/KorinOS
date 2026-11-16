//
// alarms.cpp -- les alarmes de l'Horloge (alarms.h) : le modèle d'Onyx (user/Apps/clock/alarms.cpp, clocktime.cpp :
// alarm_next, alarms_next, alarm_passed, alarm_once_day, alarm_repeat_kind, les dates de H. Hinnant), le fichier
// de Korin (src/inc/alarms.inc : al_load, al_save ; src/sys/common/alarms.c en est la traduction pour clockd) :
// on lit et on écrit /etc/alarms.txt octet pour octet comme l'asm, pour que clockd et l'Horloge se relisent.
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
#include <armpi/userconf.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <armpi/syscall.h>
#include <buskit_abi.h>
#include "alarms.h"

#define SYS_TIME_MS	0x19
#define SYS_CLOCK_GET	0x1B
#define FBUF		8192			// (AL_FBUF de l'asm)

namespace {
const char S_HEAD[] =
	"# Horloge : les alarmes (ecrites par l'Horloge et clockd, sonnees par clockd).\n"
	"# snooze : minutes du report. Un bloc [alarm] par alarme.\n";
// les clés d'un bloc, dans l'ordre de l'écriture
const char *const KEYS[] = { "id", "time", "label", "on", "days", "date", "sound", "snooze", "missed" };
enum { K_ID, K_TIME, K_LABEL, K_ON, K_DAYS, K_DATE, K_SOUND, K_SNOOZE, K_MISSED, K_N };
const char DAY[7][4] = { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };
const char *const SOUND[ALARM_SOUNDS] = { "chimes", "beeps", "marimba" };
char g_buf[FBUF];

bool seq (const char *a, const char *b) { return !strcmp (a, b); }
// les chiffres du début -> valeur (parse_dec d'AppKit : 0 si rien)
unsigned long parse_dec (const char *s)
{
	unsigned long v = 0;
	for (; (unsigned) (*s - '0') <= 9; s++) v = v * 10 + (unsigned) (*s - '0');
	return v;
}
// n chiffres, puis la fin -> valeur, -1
long digits (const char *s, int n)
{
	long v = 0;
	for (; n; n--, s++)
	{
		unsigned c = (unsigned char) *s - (unsigned) '0';
		if (c > 9) return -1;
		v = v * 10 + c;
	}
	return *s ? -1 : v;
}
char *put (char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
char *put_dec (char *d, unsigned long v)
{
	char t[24]; int k = 0;
	do t[k++] = (char) ('0' + v % 10); while (v /= 10);
	while (k) *d++ = t[--k];
	*d = 0;
	return d;
}
char *put_2d (char *d, long v) { *d++ = (char) ('0' + v / 10); *d++ = (char) ('0' + v % 10); *d = 0; return d; }
}

// ---- les dates (clocktime.cpp d'Onyx ; non signées comme l'asm : rien avant 1970) ----
long clk_days (long y, long m, long d)
{
	unsigned long Y = (unsigned long) y - (m <= 2);
	unsigned long era = Y / 400, yoe = Y - era * 400;
	unsigned long doy = (153 * (unsigned long) (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned long) d - 1;
	unsigned long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (long) (era * 146097 + doe - 719468);
}
void clk_civil (long days, long *py, long *pm, long *pd)
{
	unsigned long z = (unsigned long) days + 719468, era = z / 146097, doe = z - era * 146097;
	unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	unsigned long y = era * 400 + yoe, doy = doe - (yoe * 365 + yoe / 4 - yoe / 100);
	unsigned long mp = (doy * 5 + 2) / 153, d = doy - (mp * 153 + 2) / 5 + 1;
	unsigned long m = mp < 10 ? mp + 3 : mp - 9;
	if (m <= 2) y++;
	*py = (long) y; *pm = (long) m; *pd = (long) d;
}
int clk_wday (long days) { return (int) (((unsigned long) days + 3) % 7); }	// (le 1/1/1970 : un jeudi)
long clk_now_minute (void) { return (long) ((unsigned long) armpi_syscall (SYS_CLOCK_GET, 2, 0, 0, 0, 0, 0) / 60); }

// ---- lecture ----
namespace {
// « HH:MM » ou « H:MM » -> 1 (al_parse_hm de l'asm : « HH:MMx » refusé)
bool parse_hm (const char *s, int *hh, int *mm)
{
	int h = 0, nd = 0;
	*hh = *mm = 0;
	for (;;)
	{
		unsigned char c = (unsigned char) *s++;
		if (c == ':') break;
		unsigned dg = c - (unsigned) '0';
		if (dg > 9 || ++nd > 2) return false;
		h = h * 10 + (int) dg;
	}
	if (!nd || h > 23) return false;
	if (s[0] && s[1] && s[2]) return false;
	unsigned d1 = (unsigned char) s[0] - (unsigned) '0';
	if (d1 > 5) return false;
	unsigned d2 = (unsigned char) s[1] - (unsigned) '0';
	if (d2 > 9) return false;
	*hh = h; *mm = (int) (d1 * 10 + d2);
	return true;
}
// « AAAAMMJJ » -> jours, -1
long parse_day (const char *s)
{
	long v = digits (s, 8);
	if (v < 0) return -1;
	unsigned long y = (unsigned long) v / 10000, r = (unsigned long) v % 10000, m = r / 100, d = r % 100;
	if (y < 1970 || m - 1 > 11 || d - 1 > 30) return -1;
	return clk_days ((long) y, (long) m, (long) d);
}
// « AAAAMMJJHHMM » -> minute, -1
long parse_stamp (const char *s)
{
	if (strlen (s) != 12) return -1;
	long v = digits (s + 8, 4);
	if (v < 0) return -1;
	long hh = v / 100, mm = v % 100;
	if (hh > 23 || mm > 59) return -1;
	char day[9];
	memcpy (day, s, 8); day[8] = 0;
	long d = parse_day (day);
	return d < 0 ? d : d * 1440 + hh * 60 + mm;
}
// « mon tue ... » -> AL_* (comme l'asm : les trois premières lettres d'un mot, sans la casse)
unsigned parse_days (const char *s)
{
	unsigned bits = 0;
	while (*s)
	{
		unsigned c0 = (unsigned char) s[0] | 0x20, c1 = (unsigned char) s[1] | 0x20;
		unsigned c2 = (s[1] ? (unsigned char) s[2] : 0) | 0x20;
		for (int i = 0; i < 7; i++)
			if ((unsigned char) DAY[i][0] == c0 && (unsigned char) DAY[i][1] == c1 && (unsigned char) DAY[i][2] == c2)
			{
				bits |= 1u << i;
				break;
			}
		for (;; s++)				// la fin du mot
		{
			if (!*s) return bits;
			if (*s == ' ' || *s == ',' || *s == '\t') break;
		}
		while (*s == ' ' || *s == ',' || *s == '\t') s++;
	}
	return bits;
}
// une ligne « clé = valeur » ; a : l'alarme du bloc (0 : aucune), block : un bloc vu
void on_key (AlarmSet &s, const char *key, const char *val, Alarm *a, bool block)
{
	if (!a)
	{
		if (block || !seq (key, "snooze")) return;	// (avant tout bloc : le report seul)
		unsigned long v = parse_dec (val);
		if (v - 1 <= 29) s.snooze_min = (int) v;
		return;
	}
	int k = 0;
	while (k < K_N && !seq (key, KEYS[k])) k++;
	switch (k)
	{
	case K_ID: a->id = (int) (unsigned) parse_dec (val); break;
	case K_TIME: a->valid = parse_hm (val, &a->hh, &a->mm); break;
	case K_LABEL: alarm_set_label (*a, val); break;
	case K_ON: a->on = !seq (val, "0"); break;
	case K_DAYS: a->days = parse_days (val); break;
	case K_DATE: a->date = parse_day (val); break;
	case K_SOUND:
		a->sound = 0;
		for (int i = 0; i < ALARM_SOUNDS; i++) if (seq (val, SOUND[i])) a->sound = i;
		break;
	case K_SNOOZE: a->snooze = parse_stamp (val); break;
	case K_MISSED: a->missed = parse_stamp (val); break;
	}
}
}

int alarms_load (AlarmSet &s)
{
	long n = 0;
	char cp_[160];
	int fd = open (korin_conf_path ("alarms.txt", cp_, sizeof cp_, 0), O_RDONLY);
	if (fd >= 0)
	{
		n = read (fd, g_buf, FBUF - 1);
		if (n < 0) n = 0;
		close (fd);
	}
	s.n = 0;
	s.snooze_min = ALARM_SNOOZE_DEF;
	char *p = g_buf, *end = g_buf + n;
	*end = 0;
	Alarm *a = 0;
	bool block = false;
	while (p < end)
	{
		if (*p == ' ' || *p == '\t') { p++; continue; }
		char *le = p;				// la fin de la ligne
		while (le < end && *le != '\n') le++;
		char *t = le;
		while (t > p && (t[-1] == '\r' || t[-1] == ' ' || t[-1] == '\t')) t--;
		*t = 0;
		if (*p && *p != '#' && *p != ';')
		{
			if (*p == '[')			// un bloc : [alarm] ou un autre (ignoré)
			{
				block = true;
				a = 0;
				if (seq (p, "[alarm]") && s.n < ALARMS_MAX)
				{
					a = &s.a[s.n++];
					alarm_blank (*a);
				}
			}
			else
			{
				char *q = strchr (p, '=');
				if (q)
				{
					*q = 0;
					for (char *k = q; k > p && (k[-1] == ' ' || k[-1] == '\t');) *--k = 0;
					char *v = q + 1;
					while (*v == ' ' || *v == '\t') v++;
					on_key (s, p, v, a, block);
				}
			}
		}
		p = le + 1;
	}
	// les numéros : uniques, positifs, le suivant au-dessus de tous (from_doc d'Onyx)
	s.next_id = 1;
	for (int i = 0; i < s.n; i++) if (s.a[i].id >= s.next_id) s.next_id = s.a[i].id + 1;
	for (int i = 0; i < s.n; i++)
	{
		bool dup = s.a[i].id <= 0;
		for (int j = 0; j < i && !dup; j++) dup = s.a[j].id == s.a[i].id;
		if (dup) s.a[i].id = s.next_id++;
	}
	return s.n;
}

// ---- écriture ----
namespace {
char *put_key (char *d, int k) { return put (put (d, KEYS[k]), " = "); }
char *put_day (char *d, long day)
{
	if (day < 0) return d;
	long y, m, dd;
	clk_civil (day, &y, &m, &dd);
	return put_2d (put_2d (put_dec (d, (unsigned long) y), m), dd);
}
char *put_stamp (char *d, long min)
{
	if (min < 0) return d;
	d = put_day (d, min / 1440);
	long r = min % 1440;
	return put_2d (put_2d (d, r / 60), r % 60);
}
}

int alarms_save (const AlarmSet &s)
{
	char *d = put (g_buf, S_HEAD);
	d = put_dec (put (d, "snooze = "), (unsigned long) s.snooze_min);
	*d++ = '\n';
	for (int i = 0; i < s.n; i++)
	{
		const Alarm &a = s.a[i];
		d = put (d, "\n[alarm]\n");
		d = put_dec (put_key (d, K_ID), (unsigned) a.id);
		*d++ = '\n';
		d = put_key (d, K_TIME);
		if (a.valid) { d = put_2d (d, a.hh); *d++ = ':'; d = put_2d (d, a.mm); }
		*d++ = '\n';
		d = put (put (put_key (d, K_LABEL), a.label), "\n");
		d = put_key (d, K_ON);
		*d++ = a.on ? '1' : '0';
		*d++ = '\n';
		d = put_key (d, K_DAYS);
		for (int k = 0, w = 0; k < 7; k++)
			if (a.days & (1u << k)) { if (w++) *d++ = ' '; d = put (d, DAY[k]); }
		*d++ = '\n';
		d = put_key (d, K_DATE);
		if (!(a.days & AL_EVERYDAY)) d = put_day (d, a.date);
		*d++ = '\n';
		d = put (put (put_key (d, K_SOUND), SOUND[a.sound >= 0 && a.sound < ALARM_SOUNDS ? a.sound : 0]), "\n");
		d = put_stamp (put_key (d, K_SNOOZE), a.snooze);
		*d++ = '\n';
		if (a.missed >= 0)
		{
			d = put_stamp (put_key (d, K_MISSED), a.missed);
			*d++ = '\n';
		}
	}
	long n = d - g_buf;
	char cp_[160];
	int fd = open (korin_conf_path ("alarms.txt", cp_, sizeof cp_, 1), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return -1;
	long w = write (fd, g_buf, (size_t) n);
	close (fd);
	return w == n ? 0 : -1;
}

void alarms_publish (void)
{
	static char v[24];
	put_dec (v, (unsigned long) armpi_syscall (SYS_TIME_MS, 0, 0, 0, 0, 0, 0));
	bus_publish ((long) ALARMS_TOPIC, (long) v, -1, 0);
}

// ---- une alarme ----
void alarm_blank (Alarm &a)
{
	a.id = 0; a.valid = false; a.hh = 7; a.mm = 0; a.on = true; a.days = 0; a.date = -1;
	a.label[0] = 0; a.sound = 0; a.snooze = -1; a.missed = -1;
}
int alarm_find (const AlarmSet &s, int id)
{
	for (int i = 0; i < s.n; i++) if (s.a[i].id == id) return i;
	return -1;
}
int alarm_remove (AlarmSet &s, int id)
{
	int i = alarm_find (s, id);
	if (i < 0) return 0;
	for (; i + 1 < s.n; i++) s.a[i] = s.a[i + 1];
	s.n--;
	return 1;
}
void alarm_set_label (Alarm &a, const char *u)
{
	int i = 0;
	for (;; i++)
	{
		unsigned char c = (unsigned char) u[i];
		if (!c) break;
		if (c == '\t') c = ' ';
		if ((c & 0xC0) != 0x80)			// un caractère commence : tient-il ?
		{
			int len = c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
			if (i + len > ALARM_LABEL_MAX) break;
		}
		else if (i >= ALARM_LABEL_MAX)
			break;
		a.label[i] = (char) c;
	}
	a.label[i] = 0;
}
int alarm_repeat_kind (unsigned days)
{
	days &= AL_EVERYDAY;
	return !days ? REP_ONCE : days == AL_EVERYDAY ? REP_DAILY : days == AL_WEEKDAYS ? REP_WEEKDAYS
	     : days == AL_WEEKENDS ? REP_WEEKENDS : REP_CUSTOM;
}

// ---- quand (Onyx) ----
long alarm_once_day (int hh, int mm, long now_min)
{
	long day = now_min / 1440;
	return hh * 60 + mm > now_min - day * 1440 ? day : day + 1;
}
long alarm_next (const Alarm &a, long from_min)
{
	if (!a.valid) return -1;
	int hm = a.hh * 60 + a.mm;
	if (!(a.days & AL_EVERYDAY))
	{
		if (a.date < 0) return -1;
		long m = a.date * 1440 + hm;
		return m >= from_min ? m : -1;
	}
	long day = (from_min > 0 ? from_min : 0) / 1440;
	for (int k = 0; k <= 7; k++)
		if (a.days & (1u << clk_wday (day + k)))
		{
			long m = (day + k) * 1440 + hm;
			if (m >= from_min) return m;
		}
	return -1;
}
long alarms_next (const AlarmSet &s, long now_min, int *idx)
{
	long best = -1;
	if (idx) *idx = -1;
	for (int i = 0; i < s.n; i++)
	{
		const Alarm &a = s.a[i];
		if (!a.on || !a.valid) continue;
		long m = alarm_next (a, now_min + 1);
		if (a.snooze > now_min && (m < 0 || a.snooze < m)) m = a.snooze;
		if (m >= 0 && (best < 0 || m < best)) { best = m; if (idx) *idx = i; }
	}
	return best;
}
bool alarm_passed (const Alarm &a, long now_min)
{
	if ((a.days & AL_EVERYDAY) || a.date < 0 || !a.valid) return false;
	if (a.snooze >= now_min) return false;		// (reportée : pas encore finie)
	return a.date * 1440 + a.hh * 60 + a.mm < now_min;
}
