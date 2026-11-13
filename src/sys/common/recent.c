/* recent.c : les applications récentes (src/inc/recent.inc), voir recent.h. */
#include "recent.h"

long rc_n;
u8 rc_app[RC_MAX];
static char rc_buf[RC_BUF];

/* dans l'ordre du fichier, sans doublon, connues du catalogue seulement */
void rc_load(void)
{
	rc_n = 0;
	long fd = sys_open(RC_PATH, O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, rc_buf, RC_BUF - 1);
	sys_close(fd);
	if (n < 0)
		n = 0;
	rc_buf[n] = 0;
	char *p = rc_buf;
	while (*p) {
		char *e = p; /* fin de ligne -> 0 (CR, espaces : aussi, par ini_trim) */
		while (*e && *e != '\n')
			e++;
		char end = *e;
		*e = 0;
		char *s = ini_trim(p);
		if (*s) {
			long a = app_find(s);
			if (a >= 0) {
				if (rc_n >= RC_MAX) /* plus de place */
					return;
				long i;
				for (i = 0; i < rc_n; i++) /* déjà là ? */
					if (rc_app[i] == a)
						break;
				if (i == rc_n)
					rc_app[rc_n++] = (u8)a;
			}
		}
		if (!end)
			return;
		p = e + 1;
	}
}

void rc_touch(long app)
{
	long i;
	for (i = 0; i < rc_n; i++) /* sa place (sinon la dernière, qui sort) */
		if (rc_app[i] == app)
			break;
	if (i == rc_n) {
		if (rc_n < RC_MAX)
			rc_n++;
		i = rc_n - 1;
	}
	for (; i > 0; i--) /* décale ce qui est devant elle */
		rc_app[i] = rc_app[i - 1];
	rc_app[0] = (u8)app;
	rc_save();
}

void rc_save(void)
{
	char *e = rc_buf;
	*e = 0;
	for (long i = 0; i < rc_n; i++) {
		e = str_cat(e, ap_nom[rc_app[i]]);
		*e++ = '\n';
		*e = 0;
	}
	long fd = sys_open(RC_PATH, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return;
	sys_write(fd, rc_buf, (u64)(e - rc_buf));
	sys_close(fd);
}
