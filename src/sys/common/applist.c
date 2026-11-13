/* applist.c : le catalogue des applications (src/inc/applist.inc), pour panel et menubar. */
#include "applist.h"

long ap_n, ca_n;
char ap_nom[AP_MAX][AP_NOM];
char ap_lib[AP_MAX][AP_LIB];
u8 ap_cat[AP_MAX];
char ca_nom[CA_MAX][CA_NOM];
static long ap_cur; /* application dont on lit le manifeste */
static struct dirent ap_dirent;

char *ini_field(char *s, char **rest)
{
	char *c = s;
	while (*c && *c != ',')
		c++;
	if (*c == ',') {
		*c = 0;
		*rest = c + 1;
	} else
		*rest = 0;
	return ini_trim(s);
}

long app_find(const char *name)
{
	for (long i = 0; i < ap_n; i++)
		if (str_eq(name, ap_nom[i]))
			return i;
	return -1;
}

long cat_find(const char *name)
{
	long i;
	for (i = 0; i < ca_n; i++)
		if (str_eq(name, ca_nom[i]))
			return i;
	if (i >= CA_MAX)
		return -1;
	str_copy_n(ca_nom[i], name, CA_NOM);
	ca_n = i + 1;
	return i;
}

/* (ini_read d'un manifeste, application ap_cur) */
static void manifest_line(char *key, char *val)
{
	if (str_eq(key, "name")) {
		/* le libellé traduit ; la catégorie reste la clé : lang_tr à l'affichage */
		str_copy_n(ap_lib[ap_cur], lang_tr(val), AP_LIB);
		return;
	}
	if (!str_eq(key, "category"))
		return;
	long c = cat_find(val);
	if (c >= 0)
		ap_cat[ap_cur] = (u8)c;
}

void applis_load(void)
{
	char path[96];
	ap_n = 0;
	ca_n = 0;
	long fd = sys_open("SD0:/apps", O_READ);
	if (fd < 0)
		return;
	while (ap_n < AP_MAX) {
		if (sys_readdir(fd, &ap_dirent) != 1)
			break;
		if (!str_ends(ap_dirent.name, ".app"))
			continue;
		long l = str_len(ap_dirent.name) - 4;
		if (l > AP_NOM - 1)
			l = AP_NOM - 1;
		ap_dirent.name[l] = 0; /* nom sans .app */
		long i = ap_n;
		str_copy_n(ap_nom[i], ap_dirent.name, AP_NOM);
		ap_cat[i] = 0xFF; /* (pas encore de catégorie) */
		str_copy_n(ap_lib[i], ap_dirent.name, AP_LIB); /* libellé : le nom, sauf manifeste */
		ap_cur = i;
		char *e = str_cat(path, "SD0:/apps/");
		e = str_cat(e, ap_dirent.name);
		str_cat(e, ".app/app.txt");
		ini_read(path, manifest_line);
		ap_n = i + 1;
	}
	sys_close(fd);
	for (long i = 0; i < ap_n; i++) /* sans catégorie : « Autres » */
		if (ap_cat[i] == 0xFF)
			ap_cat[i] = (u8)cat_find("Autres"); /* (-1 : 0xFF, comme l'asm) */
}

long app_launch(long app)
{
	char path[64];
	char *e = str_cat(path, "SD0:/apps/");
	e = str_cat(e, ap_nom[app]);
	str_cat(e, ".app/main");
	return ksys(SYS_SPAWN, (long)path, 0, 0, 0, 0, 0);
}
