/* svclib.c : ini_read, ini_trim, str_copy_n de src/inc/applist.inc, pour les
 * services du système écrits en C (svclib.h). */
#include "svclib.h"

#define INI_MAX 16384

static char ini_buf[INI_MAX];

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
	long n = sys_read(fd, ini_buf, INI_MAX - 1);
	sys_close(fd);
	if (n < 0)
		n = 0;
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
