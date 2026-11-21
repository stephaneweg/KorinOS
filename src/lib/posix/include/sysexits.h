/* sysexits.h : les codes de sortie de BSD (valeurs de 4.3BSD, les mêmes partout), que des programmes portés
   rendent ou attendent (LLVM : EX_IOERR pour un tube cassé). Aucun programme de Korin ne les donne de lui-même. */
#ifndef _SYSEXITS_H
#define _SYSEXITS_H

#define EX_OK		0	/* fin normale */
#define EX__BASE	64	/* premier code d'erreur */
#define EX_USAGE	64	/* mauvais usage de la commande */
#define EX_DATAERR	65	/* données d'entrée fausses */
#define EX_NOINPUT	66	/* entrée absente ou illisible */
#define EX_NOUSER	67	/* utilisateur inconnu */
#define EX_NOHOST	68	/* hôte inconnu */
#define EX_UNAVAILABLE	69	/* service indisponible */
#define EX_SOFTWARE	70	/* erreur interne */
#define EX_OSERR	71	/* erreur du système (fork, pipe...) */
#define EX_OSFILE	72	/* fichier du système absent ou faux */
#define EX_CANTCREAT	73	/* fichier de sortie impossible à créer */
#define EX_IOERR	74	/* erreur d'entrée-sortie */
#define EX_TEMPFAIL	75	/* échec passager, à réessayer */
#define EX_PROTOCOL	76	/* erreur de protocole */
#define EX_NOPERM	77	/* permission refusée */
#define EX_CONFIG	78	/* erreur de configuration */
#define EX__MAX		78	/* dernier code d'erreur */

#endif
