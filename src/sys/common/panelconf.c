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

long pan_shell(long app)
{
	long c = ap_cat[app];
	/* (0xFF, sans catégorie : l'asm lit ca_nom hors du tableau ; ici, pas Shell) */
	if (c >= CA_MAX)
		return 0;
	return str_ci_cmp(ca_nom[c], "Shell") == 0;
}
