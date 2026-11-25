/* applist.c : le catalogue des applications (src/inc/applist.inc), pour panel et menubar. */
#include "applist.h"

long ap_n, ca_n;
char ap_nom[AP_MAX][AP_NOM];
char ap_lib[AP_MAX][AP_LIB];
u8 ap_cat[AP_MAX];
u8 ap_home[AP_MAX]; /* 1 : une app de l'utilisateur, dans HOME:/apps (qbasic, qbstudio ; décision du 25/11) */
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

/* les apps d'un dossier (« /apps », « HOME:/apps ») ; un nom déjà vu (une app du système) est sauté */
static void applis_dir(const char *dir, int home)
{
	char path[96];
	long fd = sys_open(dir, O_READ);
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
		int twin = home && app_find(ap_dirent.name) >= 0; /* (même nom qu'une app du système : les deux, « (u) »
								     * devant celle de l'utilisateur ; décision du 25/11) */
		long i = ap_n;
		ap_home[i] = (u8)home;
		str_copy_n(ap_nom[i], ap_dirent.name, AP_NOM);
		ap_cat[i] = 0xFF; /* (pas encore de catégorie) */
		str_copy_n(ap_lib[i], ap_dirent.name, AP_LIB); /* libellé : le nom, sauf manifeste */
		ap_cur = i;
		char *e = str_cat(str_cat(path, dir), "/");
		e = str_cat(e, ap_dirent.name);
		str_cat(e, ".app/app.txt");
		ini_read(path, manifest_line);
		if (twin) {
			char t[AP_LIB];
			str_copy_n(t, ap_lib[i], AP_LIB);
			ap_lib[i][0] = '(';
			ap_lib[i][1] = 'u';
			ap_lib[i][2] = ')';
			ap_lib[i][3] = ' ';
			str_copy_n(ap_lib[i] + 4, t, AP_LIB - 4);
		}
		ap_n = i + 1;
	}
	sys_close(fd);
}

void applis_load(void)
{
	ap_n = 0;
	ca_n = 0;
	applis_dir("/apps", 0);
	applis_dir("HOME:/apps", 1);
	for (long i = 0; i < ap_n; i++) /* sans catégorie : « Autres » */
		if (ap_cat[i] == 0xFF)
			ap_cat[i] = (u8)cat_find("Autres"); /* (-1 : 0xFF, comme l'asm) */
}

long cat_hidden(long c)
{
	if (c < 0 || c >= ca_n) /* (0xFF : sans catégorie) */
		return 0;
	/* « Émulateurs » ; « Craft » : la suite Craft, ouverte par son lanceur Craft (docs/CRAFT.md) */
	return str_eq(ca_nom[c], "Émulateurs") != 0 || str_eq(ca_nom[c], "Craft") != 0;
}

long app_launch(long app)
{
	char path[80];
	char *e = str_cat(path, ap_home[app] ? "HOME:/apps/" : "/apps/");
	e = str_cat(e, ap_nom[app]);
	str_cat(e, ".app/main");
	return ksys(SYS_SPAWN, (long)path, 0, 0, 0, 0, 0);
}
