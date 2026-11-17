/* svclib.c : ini_read, ini_trim, str_copy_n de src/inc/applist.inc, pour les
 * services du système écrits en C (svclib.h). */
#include "svclib.h"

#define INI_MAX 16384

static char ini_small[INI_MAX];
static char *ini_buf = ini_small;
static u64 ini_cap = INI_MAX;

/* un tampon d'au moins n octets (agrandi par sys_vm_map, jamais rendu : rare) -> 0 si refusé */
static char *ini_room(u64 n)
{
	if (n <= ini_cap)
		return ini_buf;
	u64 c = ini_cap;
	while (c < n)
		c *= 2;
	char *b = sys_vm_map(c);
	if (!b)
		return 0;
	ini_buf = b;
	ini_cap = c;
	return b;
}

/* sans espaces ni tabulations devant ; derrière, sans rien qui soit <= ' '
 * (CR compris) : ini_trim */
char *ini_trim(char *s)
{
	while (*s == ' ' || *s == 9)
		s++;
	char *e = s;
	while (*e)
		e++;
	while (e > s && (u8)e[-1] <= ' ')
		*--e = 0;
	return s;
}

int ini_read(const char *path, ini_fn fn)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	/* tout le fichier, quelle que soit sa taille (à 4 Kio puis 16 Kio, la fin de drivers.txt se perdait) */
	long st[2] = {0, 0};
	sys_stat(path, st);
	u64 n = 0;
	if (!ini_room((u64)st[0] + 1)) {
		sys_close(fd);
		return 0;
	}
	for (;;) {
		if (n + 1 >= ini_cap && !ini_room(ini_cap * 2)) /* (le fichier a grandi depuis le stat) */
			break;
		long r = sys_read(fd, ini_buf + n, ini_cap - 1 - n);
		if (r <= 0)
			break;
		n += (u64)r;
	}
	sys_close(fd);
	ini_buf[n] = 0;
	ini_parse(ini_buf, fn);
	return 1;
}

void ini_parse(char *text, ini_fn fn)
{
	char *line = text;
	while (*line) {
		char *e = line; /* fin de ligne -> 0 ; « # » : le reste de la ligne (comme l'asm,
				   y compris un « # » dans une valeur) */
		while (*e && *e != 10) {
			if (*e == '#')
				*e = 0;
			e++;
		}
		char end = *e;
		*e = 0;
		char *q = line;
		while (*q && *q != '=')
			q++;
		if (*q == '=') {
			*q = 0;
			char *k = ini_trim(line);
			for (char *c = k; *c; c++)
				if ((u8)(*c - 'A') <= 25)
					*c += 32;
			fn(k, ini_trim(q + 1));
		}
		if (!end)
			break;
		line = e + 1;
	}
}

void str_copy_n(char *d, const char *s, long size)
{
	size--;
	while (size && *s) {
		*d++ = *s++;
		size--;
	}
	*d = 0;
}
