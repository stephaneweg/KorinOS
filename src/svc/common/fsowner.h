/* fsowner.h : propriétaires des fichiers ouverts et tampons partagés des
 * services de fichiers (fatfs, ramfs), repris de src/svc/inc/fsowner.inc
 * (docs/FICHIERS.md, 4.2). Mêmes règles, mêmes tables, mêmes erreurs. */
#ifndef KORIN_FSOWNER_H
#define KORIN_FSOWNER_H

#include "fssvc.h"

#define FOW_MAX 1024 /* couples (fichier, pid) */
#define TP_MAX 48    /* tampons (process clients à la fois) */

extern long fs_canal; /* le numéro du service (open_extras_put le rend en a) */

long proprietaire(struct msg *m);        /* propriétaire d'une ouverture */
void open_extras_take(struct msg *m);    /* attributs demandés par vfs (c) */
void open_extras_put(struct msg *m);     /* c |= ces attributs, a = canal */
long fo_access(struct msg *m, long num); /* 0, ou E_BADF */
u32 *fo_own_find(long num, long pid);
long fo_own_add(long num, long pid);     /* 1, 0 si la table est pleine */
long fo_own_count(long num);
void fo_own_clear(long num);
long fo_own_close(struct msg *m, long num); /* 1 : à fermer */
void fo_own_end(long pid, void (*close_num)(long num));
long fo_own_fork(long child, long parent);
void *tampon_de(struct msg *m);          /* tampon de l'appelant, 0 : aucun */
long op_tampon(struct msg *m);           /* 14 FS_BUFFER */
void tampons_fin(long pid);

#endif
