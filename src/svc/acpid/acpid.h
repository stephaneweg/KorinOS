/* acpid.h - le service ACPI du PC (docs/etude/BATTERIE-PC.md) : ACPICA (osl.c, acpi.c) et le reste du
 * service (acpid.c, sur korinrt). Les deux moitiés ne partagent que ce fichier : acpi.c et osl.c
 * incluent acpi.h d'ACPICA (qui renomme la bibliothèque C, tools/acpica/ackorin.h), acpid.c
 * korinrt.h ; aucun type d'ACPICA ici. */
#ifndef ACPID_H
#define ACPID_H

/* ce que les méthodes de l'AML disent de l'énergie (une lecture) ; -1 : inconnu */
struct power_raw {
	int batteries;      /* batteries présentes (_STA bit 4) */
	int ac;             /* secteur : 1 branché, 0 débranché, -1 pas d'adaptateur décrit (ACPI0003) */
	unsigned state;     /* OU des _BST : bit 0 décharge, bit 1 charge, bit 2 critique */
	long rate_mw;       /* puissance (somme), mW */
	long remain_mwh;    /* capacité restante (somme), mWh */
	long full_mwh;      /* capacité à la dernière charge complète (somme), mWh */
	long design_mwh;    /* capacité neuve (somme), mWh */
	long voltage_mv;    /* tension de la première batterie */
	long cycles;        /* cycles de la première batterie (_BIX) */
	long warn_mwh;      /* niveaux d'alerte du micrologiciel (somme) */
	long low_mwh;
	char model[24];     /* modèle et fabricant de la première batterie */
	char oem[24];
	char tech[8];       /* type : « LION »… */
};

/* acpid.c */
void acpid_log(const char *line); /* une ligne au journal (sans '\n') */

/* acpi.c : ACPICA chargé, l'EC servi, les méthodes d'initialisation lancées (init : 0 le minimum, 1
 * tout comme Linux) -> 0, < 0 sinon (le journal dit pourquoi) */
int acpi_start(int init_all);
/* une lecture de la batterie et du secteur -> 0 */
int acpi_read(struct power_raw *p);
/* nombre de batteries et d'adaptateurs décrits dans le DSDT (même absents) */
void acpi_devices(int *batteries, int *adapters);
/* travaux différés d'ACPICA (Notify…), depuis la boucle du service */
void acpi_run_deferred(void);

/* osl.c */
void osl_init(void);

#endif
