/* hwmanager.h : entre le cœur commun (hwmanager.c) et la couche de chaque
 * machine (hwmanager_aarch64.c : bus I2C 1, périphériques du SoC, son HDMI du
 * Pi 4 ; hwmanager_x86_64.c : le PC). */
#ifndef KORIN_HWMANAGER_H
#define KORIN_HWMANAGER_H

#include "../svclib/svclib.h"

#define SOC_MAX 4 /* lignes « soc:... » (genet, wifi) */

struct soc_entry {
	char key[16];     /* ce qui suit « soc: » */
	char driver[112]; /* « chemin [arguments] » */
	u64 pid;          /* pilote lancé (0 : aucun) */
	u64 tries;        /* relances */
};

extern struct soc_entry soct[SOC_MAX];
extern long soc_n;
extern char hdmi_line[128]; /* « hdmi0:audio = ... » de drivers.txt */

/* du cœur, pour la couche */
long launch(const char *line);           /* « chemin [arguments] » -> pid (journal : chemin et pid) */
void soc_launch(struct soc_entry *e);    /* « hwmanager : soc:genet -> » puis le pilote */
int option_word(const char *word);       /* 1 s'il commence une ligne de SD0:/etc/options.txt */
void print_hex(u64 v);                   /* en hexadécimal (majuscules) sur la console */

/* de la couche */
long plat_i2c_open(void);                /* 0, ou < 0 : pas de bus I2C 1 */
int plat_i2c_present(u64 adr);           /* 1 si un périphérique acquitte cette adresse */
void plat_soc_start(void);               /* pilotes du SoC d'après la carte, son HDMI */
int plat_hdmi_watched(void);             /* 1 : l'état « hot plug » est surveillé (toutes les 2 s) */
void plat_hdmi_poll(void);               /* après chaque SVC_NEXT */
int plat_hdmi_end(long pid);             /* 1 si pid était le pilote du son HDMI (oublié) */
long plat_spawn(const char *path, const char *args); /* SYS_SPAWN -> pid */
int plat_drivers_missing(ini_fn fn);     /* drivers.txt absent : une autre source (1 : lue) */

#endif
