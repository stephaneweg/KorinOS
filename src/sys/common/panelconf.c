/* panelconf.c : les réglages du panel (src/inc/panelconf.inc), voir panelconf.h. */
#include "panelconf.h"

long pan_npin;
char pan_pin[PANEL_MAXPINS][AP_NOM];

void pan_add(const char *name)
{
	if (!*name || pan_npin >= PANEL_MAXPINS)
		return;
	str_copy_n(pan_pin[pan_npin++], name, AP_NOM);
}

void pan_line(char *key, char *val)
{
	if (str_eq(key, "pin") || str_eq(key, DL_KEY_LAUNCHER) || str_eq(key, DL_KEY_LANCEUR))
		pan_add(val);
	else
		dl_line(key, val);
}

void pan_load(void)
{
	dl_ncats = 0;
	dl_nlaunchers = 0;
	dl_ndesks = 0;
	pan_npin = 0;
	dl_trash = 1;
	dl_size = -1;
	if (!ini_read(conf_path("panel.ini", 0), pan_line) && !ini_read(conf_path("dock.ini", 0), pan_line)) {
		pan_add(DL_DEFAULT_TERMINAL);
		pan_add(DL_DEFAULT_FILES);
	}
	dl_desks_default();
}

/* chemin, tampon de PAN_FILE octets -> le fichier lu, terminé par 0 (0 : absent ou vide) */
static long pan_read_file(const char *path, char *buf)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, buf, PAN_FILE - 1);
	sys_close(fd);
	if (n < 0)
		n = 0;
	buf[n] = 0;
	return n;
}

/* ligne -> 1 si c'est un raccourci (pin, launcher ou lanceur =) */
static long pan_is_pin_line(const char *l)
{
	while (*l == ' ' || *l == '\t')
		l++;
	char key[16];
	long k = 0;
	while (*l && *l != '=' && *l != ' ' && *l != '\t' && *l != '\r' && *l != '\n' && k < 15)
		key[k++] = *l++;
	key[k] = 0;
	while (*l == ' ' || *l == '\t')
		l++;
	if (*l != '=')
		return 0;
	return str_eq(key, "pin") || str_eq(key, DL_KEY_LAUNCHER) || str_eq(key, DL_KEY_LANCEUR);
}

/* les raccourcis, à la place du premier (à la fin s'il n'y en avait pas) */
static char *pan_put_pins(char *o)
{
	for (long i = 0; i < pan_npin; i++)
		o = str_cat(str_cat(str_cat(o, "pin       = "), pan_pin[i]), "\r\n");
	return o;
}

long pan_save(void)
{
	static char src[PAN_FILE];
	static char dst[PAN_FILE + PANEL_MAXPINS * (AP_NOM + 16)];
	if (!pan_read_file(conf_path("panel.ini", 0), src)) /* (sans panel.ini : ce que lisait pan_load) */
		pan_read_file(conf_path("dock.ini", 0), src);
	char *o = dst;
	long done = 0;
	for (char *l = src; *l;) {
		char *e = l; /* la ligne, sa fin comprise */
		while (*e && *e != '\n')
			e++;
		if (*e)
			e++;
		if (pan_is_pin_line(l)) {
			if (!done)
				o = pan_put_pins(o);
			done = 1;
		} else {
			mem_move(o, l, (u64)(e - l));
			o += e - l;
			if (e[-1] != '\n') /* (dernière ligne sans fin) */
				o = str_cat(o, "\r\n");
		}
		l = e;
	}
	if (!done)
		o = pan_put_pins(o);
	*o = 0;
	long fd = sys_open(conf_path("panel.ini", 1), O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long n = sys_write(fd, dst, (u64)(o - dst));
	sys_close(fd);
	return n < 0 ? n : 0;
}

long pan_shell(long app)
{
	long c = ap_cat[app];
	/* (0xFF, sans catégorie : l'asm lit ca_nom hors du tableau ; ici, pas Shell) */
	if (c >= CA_MAX)
		return 0;
	return str_ci_cmp(ca_nom[c], "Shell") == 0;
}
