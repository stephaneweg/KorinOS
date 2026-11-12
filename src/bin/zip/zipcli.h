/* zipcli.h - ce que zip et unzip partagent, comme zipcli.inc (et bin/arccli.h d'Onyx) : motifs (* ?
 * sans la casse, ou un dossier : tout ce qu'il contient), noms d'archive faits d'un chemin, chaînes,
 * tailles, dates, erreurs. Inclus par src/bin/zip/zip.c et src/bin/unzip/unzip.c (« ../zip/zipcli.h »),
 * comme l'asm inclut zipcli.inc. */
#ifndef ZIPCLI_H
#define ZIPCLI_H

#include "korinrt.h"

#define ZC static __attribute__((unused))

/* les entrées d'ArchiveKit (archivekit.inc) */
#define AR_FMT_ZIP 1
#define ARE_NAME 0
#define ARE_SIZE 1
#define ARE_PACKED 2
#define ARE_TIME 3
#define ARE_DIR 4
#define ARE_METHOD 5
#define ARE_CRC 6

ZC int zc_lower(int c)
{
	return (unsigned)(c - 'A') <= 25 ? c + 32 : c;
}

/* motif (* ?), nom -> 1 s'il y correspond (sans la casse ASCII) ; comme l'asm, après « * » la
 * suite du motif n'est jamais essayée sur un nom vide */
ZC int zc_glob(const char *p, const char *s)
{
	for (;;) {
		u8 c = (u8)*p;
		if (!c)
			return !*s;
		if (c == '*') {
			while (p[1] == '*')
				p++;
			if (!p[1])
				return 1;
			for (;; s++) {
				if (!*s)
					return 0;
				if (zc_glob(p + 1, s))
					return 1;
			}
		}
		u8 d = (u8)*s;
		if (!d)
			return 0;
		if (c != '?' && zc_lower(c) != zc_lower(d))
			return 0;
		p++;
		s++;
	}
}

/* nom, dossier -> 1 si le nom est sous ce dossier (« dossier/... », sans la casse) */
ZC int zc_under(const char *name, const char *dir)
{
	if (!*dir)
		return 0;
	for (;;) {
		u8 c = (u8)*dir++;
		if (!c)
			return *name == '/';
		if (zc_lower(c) != zc_lower((u8)*name++))
			return 0;
	}
}

/* nom, motifs, nombre -> 1 si un motif le choisit (aucun motif : tous) */
ZC int zc_chosen(const char *name, char **pats, long n)
{
	char buf[512];
	if (!n)
		return 1;
	for (; n; n--) {
		const char *p = *pats++;
		long k = 0;
		for (;;) { /* le motif sans ses / de la fin (500 octets) */
			buf[k] = p[k];
			if (!buf[k])
				break;
			if (++k >= 500) {
				buf[k] = 0;
				break;
			}
		}
		while (k && buf[k - 1] == '/')
			buf[--k] = 0;
		if (zc_glob(buf, name) || zc_under(name, buf))
			return 1;
	}
	return 0;
}

/* -> 1 si égales sans la casse ASCII */
ZC int zc_streqi(const char *a, const char *b)
{
	for (;;) {
		int c = zc_lower((u8)*a++), d = zc_lower((u8)*b++);
		if (c != d)
			return 0;
		if (!d)
			return 1;
	}
}

ZC int zc_has_dot(const char *s)
{
	return strchr(s, '.') != 0;
}

/* nom -> 1 s'il a une partie « .. » (refusé à l'extraction) */
ZC int zc_dotdot(const char *s)
{
	const char *part = s;
	for (;;) {
		char c = *s;
		if (c && c != '/' && c != '\\') {
			s++;
			continue;
		}
		if (s - part == 2 && part[0] == '.' && part[1] == '.')
			return 1;
		if (!c)
			return 0;
		part = ++s;
	}
}

/* dossier, nom -> « dossier/nom » (malloc ; dossier vide : le nom seul) */
ZC char *zc_join(const char *dir, const char *name)
{
	char *d = malloc((u64)strlen(dir) + strlen(name) + 2), *o = d;
	while (*dir)
		*o++ = *dir++;
	if (o != d && o[-1] != '/' && o[-1] != ':')
		*o++ = '/';
	while ((*o++ = *name++))
		;
	return d;
}

/* chemin -> son nom dans une archive (malloc) : sans volume (« SD0: » dans les 6 premiers octets),
 * sans / en tête, sans . ni .. (« SD0:/a/./b/../c.txt » -> « a/c.txt ») */
ZC char *zc_norm(const char *s)
{
	for (int i = 0; i < 6 && s[i]; i++)
		if (s[i] == ':') {
			s += i + 1;
			break;
		}
	char *d = malloc((u64)strlen(s) + 2), *o = d;
	while (*s) {
		if (*s == '/' || *s == '\\') {
			s++;
			continue;
		}
		const char *e = s;
		while (*e && *e != '/' && *e != '\\')
			e++;
		long k = e - s;
		if (k == 1 && s[0] == '.') {
		} else if (k == 2 && s[0] == '.' && s[1] == '.') {
			while (o != d && *--o != '/') /* la partie d'avant retirée */
				;
		} else {
			if (o != d)
				*o++ = '/';
			while (s < e)
				*o++ = *s++;
		}
		s = e;
	}
	*o = 0;
	return d;
}

/* valeur, largeur : alignée à droite */
ZC void zc_put_w(u64 v, u64 w)
{
	char t[32];
	u64 k = (u64)(fmt_dec(v, t) - t);
	for (; k < w; k++)
		out_putc(' ');
	out_puts(t);
}

/* chaîne, largeur : alignée à gauche (des blancs après) */
ZC void zc_put_pad(const char *s, long w)
{
	w -= str_len(s);
	out_puts(s);
	for (; w > 0; w--)
		out_putc(' ');
}

ZC void zc_spaces(long n)
{
	while (n--)
		out_putc(' ');
}

/* 0..99 : deux chiffres */
ZC void zc_two(u64 v)
{
	out_putc((int)('0' + (v / 10) % 10));
	out_putc((int)('0' + v % 10));
}

ZC void zc_hex8(u64 v)
{
	for (int s = 28; s >= 0; s -= 4)
		out_putc("0123456789abcdef"[(v >> s) & 15]);
}

/* secondes UTC (0 : aucune date) : « AAAA-MM-JJ HH:MM » en heure locale */
ZC void zc_put_date(u64 t)
{
	if (!t) {
		out_puts("---------- -----");
		return;
	}
	t += (u64)sys_clock(1); /* décalage de l'heure locale */
	u64 days = t / 86400, sec = t % 86400;
	days += 719468;
	u64 era = days / 146097, doe = days % 146097;
	u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	u64 y = era * 400 + yoe;
	u64 doy = doe - (365 * yoe + (yoe >> 2) - yoe / 100);
	u64 mp = (5 * doy + 2) / 153;
	u64 d = doy - (153 * mp + 2) / 5 + 1;
	u64 m = mp < 10 ? mp + 3 : mp - 9;
	if (m <= 2)
		y++;
	out_putn(y);
	out_putc('-');
	zc_two(m);
	out_putc('-');
	zc_two(d);
	out_putc(' ');
	zc_two(sec / 3600);
	out_putc(':');
	zc_two((sec / 60) % 60);
}

/* octets : « 512 o », « 37,5 Mo », « 29,7 Go » */
ZC void zc_put_size(u64 v)
{
	const char *unit = " o";
	u64 div = 1;
	if (v >= 1024) {
		unit = " Ko";
		div = 1024;
		if (v >> 20) {
			unit = " Mo";
			div = 0x100000;
			if (v >> 30) {
				unit = " Go";
				div = 0x40000000;
			}
		}
	}
	u64 q = v / div;
	out_putn(q);
	if (div != 1 && q < 100) {
		out_say(",");
		out_putn((v - q * div) * 10 / div);
	}
	out_say(unit);
}

/* taille, taille compressée -> pourcentage gagné */
ZC u64 zc_pct(u64 size, u64 packed)
{
	if (!size || packed >= size)
		return 0;
	return (size - packed) * 100 / size;
}

/* donnée (0 : aucune), texte : « nom : donnée : texte » sur l'erreur standard */
ZC void zc_err(const char *data, const char *text)
{
	err_puts(krt_argv[0]);
	err_say(" : ");
	if (data) {
		err_puts(data);
		err_say(" : ");
	}
	err_say(text);
	err_puts("\n");
}

#endif
