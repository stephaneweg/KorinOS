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
 * tout comme Linux), events : mode ACPI, SCI, GPE, boutons -> 0, < 0 sinon (le journal dit pourquoi) */
int acpi_start(int init_all, int events);
/* événements arrivés depuis le dernier appel (ACPI_EV_*, remis à 0) */
#define ACPI_EV_POWER 1   /* bouton d'alimentation */
#define ACPI_EV_SLEEP 2   /* bouton de mise en veille */
#define ACPI_EV_LID 4     /* couvercle */
#define ACPI_EV_BATTERY 8 /* batterie ou secteur (Notify) */
unsigned acpi_take_events(void);
/* ce que les tables décrivent (après acpi_start) : découvert, rien de propre à une machine */
struct acpi_features {
	int events;        /* SCI et GPE en service */
	int lids;          /* couvercles (PNP0C0D) */
	int power_button;  /* bouton d'alimentation (fixe ou PNP0C0C) */
	int sleep_button;  /* bouton de mise en veille (fixe ou PNP0C0E) */
	int ec;            /* contrôleur embarqué (PNP0C09) */
	int ec_gpe;        /* sa GPE servie */
};
void acpi_features(struct acpi_features *f);
/* couvercle : 1 ouvert, 0 fermé, -1 pas de couvercle décrit */
int acpi_lid(void);
/* trop d'interruptions : GPE coupées sauf celle de l'EC */
void acpi_quiet_gpes(void);
/* une lecture de la batterie et du secteur -> 0 */
int acpi_read(struct power_raw *p);
/* nombre de batteries et d'adaptateurs décrits dans le DSDT (même absents) */
void acpi_devices(int *batteries, int *adapters);
/* travaux différés d'ACPICA (Notify…), depuis la boucle du service */
void acpi_run_deferred(void);

/* osl.c */
void osl_init(void);
/* la ligne (GSI de l'IOAPIC) du SCI, après acpi_start ; -1 : aucun gestionnaire installé */
int osl_sci_line(void);
/* le SCI s'est levé : le gestionnaire d'ACPICA, puis les travaux différés */
void osl_sci_fire(void);

#endif
