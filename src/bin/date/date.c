/* date - la date et l'heure (l'horloge du noyau, heure locale) : date [-u] [+FORMAT]
 * (docs/OUTILS.md, d'après date.c d'Onyx), comme date.asm. Sans format : 2026-10-04 21:47:03.
 * Dans FORMAT : %Y année, %m mois, %d jour, %H heure, %M minute, %S seconde, %y les deux derniers
 * chiffres de l'année, %F = %Y-%m-%d, %T = %H:%M:%S, %s secondes depuis 1970 (UTC), %n fin de
 * ligne, %% un % ; %X inconnu reste « %X », un % final reste « % ». -u : en temps universel.
 * Tout autre argument : usage et code 2. */
#include "korinrt.h"

/* deux chiffres : dizaines (modulo 10) et unités */
static void two(u64 v)
{
	out_putc((int)('0' + v / 10 % 10));
	out_putc((int)('0' + v % 10));
}

int main(int argc, char **argv)
{
	long which = 2; /* SYS_CLOCK_GET : 2 heure locale, 0 UTC */
	const char *f = "%F %T";
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] == '+')
			f = a + 1;
		else if (str_eq(a, "-u"))
			which = 0;
		else {
			err_msg("usage : date [-u] [+FORMAT]");
			return 2;
		}
	}
	u64 utc = (u64)sys_clock(0);
	struct civil t;
	civil_from_secs((u64)sys_clock(which), &t);
	for (char c; (c = *f++);) {
		if (c != '%' || !*f) {
			out_putc(c);
			continue;
		}
		c = *f++;
		switch (c) {
		case 'Y':
			out_putn(t.year);
			break;
		case 'y':
			two(t.year % 100);
			break;
		case 'm':
			two(t.month);
			break;
		case 'd':
			two(t.day);
			break;
		case 'H':
			two(t.hour);
			break;
		case 'M':
			two(t.min);
			break;
		case 'S':
			two(t.sec);
			break;
		case 'F':
			out_putn(t.year);
			out_putc('-');
			two(t.month);
			out_putc('-');
			two(t.day);
			break;
		case 'T':
			two(t.hour);
			out_putc(':');
			two(t.min);
			out_putc(':');
			two(t.sec);
			break;
		case 's':
			out_putn(utc);
			break;
		case 'n':
			out_nl();
			break;
		case '%':
			out_putc('%');
			break;
		default:
			out_putc('%');
			out_putc(c);
			break;
		}
	}
	out_nl();
	return 0;
}
