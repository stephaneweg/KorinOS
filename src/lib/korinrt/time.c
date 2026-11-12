/* time.c : dates (docs/BIN-C.md), le calcul de civil dans date.asm, uname.asm
 * et src/inc/dostime.inc (jours depuis le 1er mars de l'an 0, en non signé). */
#include "korinrt.h"

void civil_from_secs(u64 secs, struct civil *c)
{
	u64 days = secs / 86400, s = secs % 86400;
	c->hour = s / 3600;
	s %= 3600;
	c->min = s / 60;
	c->sec = s % 60;
	c->wday = (days + 4) % 7; /* le 1er janvier 1970 : un jeudi */
	u64 z = days + 719468;
	u64 era = z / 146097;
	u64 doe = z - era * 146097;
	u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	u64 y = yoe + era * 400;
	u64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	u64 mp = (5 * doy + 2) / 153;
	c->day = doy - (153 * mp + 2) / 5 + 1;
	c->month = mp < 10 ? mp + 3 : mp - 9;
	if (c->month <= 2)
		y++;
	c->year = y;
	static const u16 before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
	c->yday = before[c->month - 1] + c->day - 1 + (leap && c->month > 2);
}

u64 secs_from_civil(u64 year, u64 month, u64 day, u64 hour, u64 min, u64 sec)
{
	u64 y = month <= 2 ? year - 1 : year;
	u64 era = y / 400;
	u64 yoe = y - era * 400;
	u64 mp = month > 2 ? month - 3 : month + 9;
	u64 doy = (153 * mp + 2) / 5 + day - 1;
	u64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	u64 days = era * 146097 + doe - 719468;
	return days * 86400 + hour * 3600 + min * 60 + sec;
}
