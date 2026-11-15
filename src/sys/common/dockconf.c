/* dockconf.c : les réglages du dock (src/inc/dockconf.inc), voir dockconf.h. */
#include "dockconf.h"

long dl_ncats, dl_nlaunchers, dl_ndesks;
long dl_trash = 1;
long dl_size = -1;
u8 dl_cat[CA_MAX];
u8 dl_hidden[CA_MAX];
u8 dl_napps[CA_MAX];
u8 ap_order[AP_MAX];
char dl_app[CA_MAX][AP_NOM];
char dl_launcher[DOCK_MAXLAUNCHERS][AP_NOM];
char dl_desk[DOCK_MAXDESKS][DL_DESK];

/* catégories sans tiroir (suite de chaînes, une vide à la fin) */
static const char no_docked[] = "Shell\0Settings\0Emulators\0Réglages\0Paramètres\0Émulateurs\0";
static const char *const sizes[3] = {"grande", "moyenne", "petite"};

long str_ci_cmp(const char *a, const char *b)
{
	for (;;) {
		u64 c = (u8)*a++, d = (u8)*b++;
		if (c - 'A' <= 25)
			c += 32;
		if (d - 'A' <= 25)
			d += 32;
		if (c != d || !c)
			return (long)(c - d);
	}
}

long dock_category_docked(const char *cat)
{
	if (!*cat)
		return 0;
	for (const char *n = no_docked; *n; n += str_len(n) + 1)
		if (!str_ci_cmp(cat, n))
			return 0;
	return 1;
}

long dl_find(long cat)
{
	for (long i = 0; i < dl_ncats; i++)
		if (dl_cat[i] == cat)
			return i;
	return -1;
}

long dl_add(long cat)
{
	long r = dl_find(cat);
	if (r >= 0)
		return r;
	r = dl_ncats;
	if (r >= CA_MAX)
		return -1;
	dl_ncats = r + 1;
	dl_cat[r] = (u8)cat;
	dl_hidden[r] = 0;
	dl_napps[r] = 0;
	dl_app[r][0] = 0;
	return r;
}

void dl_launcher_add(const char *name)
{
	if (!*name || dl_nlaunchers >= DOCK_MAXLAUNCHERS)
		return;
	str_copy_n(dl_launcher[dl_nlaunchers++], name, AP_NOM);
}

void dl_line(char *key, char *val)
{
	if (str_eq(key, "drawer") || str_eq(key, "tiroir")) {
		char *rest;
		char *cat = ini_field(val, &rest); /* « catégorie, application » */
		if (!dock_category_docked(cat))
			return;
		long c = cat_find(cat);
		if (c < 0)
			return;
		long r = dl_add(c);
		if (r < 0 || !rest)
			return;
		char *app = ini_field(rest, &rest);
		if (!dl_app[r][0]) /* (la première ligne garde sa place) */
			str_copy_n(dl_app[r], app, AP_NOM);
		return;
	}
	if (str_eq(key, DL_KEY_LAUNCHER) || str_eq(key, DL_KEY_LANCEUR)) {
		dl_launcher_add(val);
		return;
	}
	if (str_eq(key, "hidden")) {
		if (!*val)
			return;
		long c = cat_find(val);
		if (c < 0)
			return;
		long r = dl_add(c);
		if (r >= 0)
			dl_hidden[r] = 1;
		return;
	}
	if (str_eq(key, "desk")) {
		if (dl_ndesks >= DOCK_MAXDESKS)
			return;
		str_copy_n(dl_desk[dl_ndesks++], val, DL_DESK);
		return;
	}
	if (str_eq(key, "corbeille")) {
		dl_trash = str_eq(val, "non") ? 0 : 1;
		return;
	}
	if (str_eq(key, "taille")) {
		/* grande, moyenne, petite ; autre chose (« auto ») : rien ne change, comme l'asm
		 * (la valeur reste celle d'avant, -1 sauf deux lignes taille) */
		for (long i = 0; i < 3; i++)
			if (str_eq(val, sizes[i])) {
				dl_size = i;
				return;
			}
	}
}

/* aucun espace nommé : 4, de 1 à 4 (aussi pan_load) */
void dl_desks_default(void)
{
	if (dl_ndesks)
		return;
	dl_ndesks = 4;
	for (long i = 0; i < 4; i++) {
		dl_desk[i][0] = (char)('1' + i);
		dl_desk[i][1] = 0;
	}
}

long dock_layout_load(void)
{
	dl_ncats = 0;
	dl_nlaunchers = 0;
	dl_ndesks = 0;
	dl_trash = 1;
	dl_size = -1;
	long found = ini_read(conf_path("dock.ini", 0), dl_line);
	if (!found) { /* sans fichier : terminal, fichiers */
		dl_launcher_add(DL_DEFAULT_TERMINAL);
		dl_launcher_add(DL_DEFAULT_FILES);
	}
	dl_desks_default();
	long first = dl_ncats; /* les catégories des applications : comptées, les nouvelles ajoutées */
	for (long i = 0; i < ap_n; i++) {
		long c = ap_cat[i];
		/* (0xFF, sans catégorie : ca_nom lu hors du tableau dans l'asm ; ici non docké) */
		if (c >= CA_MAX || !dock_category_docked(ca_nom[c]))
			continue;
		long r = dl_add(c);
		if (r >= 0)
			dl_napps[r]++;
	}
	for (long i = first + 1; i < dl_ncats; i++) /* (les nouvelles par nom) */
		for (long j = i; j > first; j--) {
			if (str_ci_cmp(dl_cat_name(j - 1), dl_cat_name(j)) <= 0)
				break;
			dl_swap(j - 1, j);
		}
	for (long r = 0; r < dl_ncats; r++) /* une application principale absente : aucune */
		if (dl_app[r][0] && app_find(dl_app[r]) < 0)
			dl_app[r][0] = 0;
	return found;
}

const char *dl_cat_name(long row)
{
	return ca_nom[dl_cat[row]];
}

void dl_swap(long a, long b)
{
	u8 t = dl_cat[a];
	dl_cat[a] = dl_cat[b];
	dl_cat[b] = t;
	t = dl_hidden[a];
	dl_hidden[a] = dl_hidden[b];
	dl_hidden[b] = t;
	t = dl_napps[a];
	dl_napps[a] = dl_napps[b];
	dl_napps[b] = t;
	char tmp[AP_NOM];
	memcpy(tmp, dl_app[a], AP_NOM);
	memcpy(dl_app[a], dl_app[b], AP_NOM);
	memcpy(dl_app[b], tmp, AP_NOM);
}

char *app_label(long app)
{
	return ap_lib[app];
}

void apps_sort(void)
{
	for (long i = 0; i < ap_n; i++) {
		ap_order[i] = (u8)i;
		for (long j = i; j > 0; j--) {
			if (str_ci_cmp(app_label(ap_order[j - 1]), app_label(ap_order[j])) <= 0)
				break;
			u8 t = ap_order[j - 1];
			ap_order[j - 1] = ap_order[j];
			ap_order[j] = t;
		}
	}
}

long dl_main(long row)
{
	if (dl_app[row][0]) {
		long a = app_find(dl_app[row]);
		if (a >= 0)
			return a;
	}
	for (long i = 0; i < ap_n; i++) {
		long a = ap_order[i];
		if (ap_cat[a] == dl_cat[row])
			return a;
	}
	return -1;
}
