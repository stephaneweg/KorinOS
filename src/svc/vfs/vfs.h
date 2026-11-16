/* vfs.h : structures communes de vfs (vfs.c : volumes et boucle ; path.c :
 * chemins et dossiers courants ; exec.c : bit d'exécution ; pipes.c : tubes et
 * entrées / sorties standard ; pids.c : fiches des process). Traduction de
 * vfs.asm, path.inc, exec.inc, pipes.inc, pids.inc : mêmes tables, mêmes règles. */
#ifndef KORIN_VFS_H
#define KORIN_VFS_H

#include "fssvc.h"

#define PATH_MAX 256
#define VOL_MAX 8
#define VT_TAG 0x40000000 /* bit des poignées de vterm (src/inc/vterm.inc) */
#define ROOT_MAX 64       /* dossier racine d'un volume (rootpath), sans « / » au bord */

/* un volume monté */
struct volume {
	char name[16]; /* en minuscules (vide : place libre) */
	long chan;
	long pid;
	char src[24]; /* « blk.usb0p1 », « ram:16M » */
	char fs[16];  /* « fatfs » */
	char root[ROOT_MAX]; /* rootpath : dossier de la source qui sert de racine (« korinARM » ;
				vide : la racine de la source). Les chemins rendus aux programmes ne
				le montrent jamais : il n'est ajouté qu'en envoyant au service */
	char svcvol[16];     /* le volume qui a lancé le service (« fs.<svcvol> ») ; un second
				volume sur la même partition (/boot) partage chan, pid et cache */
	char *idx;    /* index des exécutables (exec.c) : tampon */
	u64 idxn;     /*   octets */
	u64 idxok;    /*   1 : lu (ou vide) */
};

/* fiche d'un process (pids.c) */
struct pr {
	u64 pid; /* 0 : fiche libre */
	u64 stdio[3];
	u32 uid;       /* à qui sont ses fichiers (docs/etude/MULTI-UTILISATEUR.md §2.2) */
	u32 uid_known; /* 0 : à demander au noyau (SYS_PROC_UID) */
	char dir[PATH_MAX]; /* dossier courant interne (vide : « /root ») */
};

/* ---- vfs.c */
extern struct volume volumes[VOL_MAX];
extern struct msg msg;
extern long current_req;
extern long answered; /* 1 : requête transmise (SVC_FORWARD) */
struct volume *volume_de(const char *p, const char **rest);
u64 vol_chemin(const struct volume *v, const char *rest, u8 *out, u64 cap);
struct volume *vol_index(struct volume *v);
long vol_lire(struct volume *v, const char *rel, void *buf, u64 max);
void vol_publier(struct volume *v, int remove);

/* ---- path.c */
extern char resolu[PATH_MAX];
extern char nom_exec[PATH_MAX];
extern const char dossier_defaut[];
extern const char chemin_sys[], sp_bin[], sp_apps[], sp_main[];
long path_normalize(const char *cwd, const char *path, char *out);
long point_ajouter(const char *ext, const char *in);
void points_retirer(const char *vol);
const char *point_de(const char *vol);
int point_pris(const char *ext);
char *chemin_externe(const char *path, char *out);
void naissance(long child, long parent);
void oublier_dossier(long pid);
long resoudre(long pid, const char *path, char *out);
long chemin_ajouter(void);
long chemins_lister(void);
const char *chemin_virtuel(const char *path, const char **rest);
long ops_chemins(void);

/* ---- exec.c */
extern char exd_ligne[PATH_MAX];
long est_executable(const char *path);
long exec_regle(const char *path);
long idx_charger(struct volume *v);
long idx_cherche(struct volume *v, const char *rel, int sub, u64 *len);
long idx_renommer(struct volume *vo, const char *old, struct volume *vn, const char *new);
long idx_ecrire(struct volume *v);
void idx_oublier(void);
long idx_chmod(u64 mode);
long script_voir(void);
extern char interp[PATH_MAX], iarg[], script_res[PATH_MAX];

/* ---- pipes.c */
long ops_pipes(int *deferred);
long ops_bouts(void);
void ends_pid_gone(long pid);
long ends_fork(long child, long parent);
void pipes_dump(void);
void stdio_birth(long child, long parent, long in, long out, long err);
void stdio_exec(long pid, long in, long out, long err);
void waits_tick(void);
long waits_any(void);

/* ---- pids.c */
struct pr *pid_find(long pid);
struct pr *pid_get(long pid);
void pid_forget(long pid);
long pid_uid(long pid);                /* uid du process (0 : system, pid inconnu, noyau sans uid) */
void pid_setuid(long pid, long uid);   /* avis SVC_OP_SETUID */
void pid_stdio_drop(u64 handle);

#endif
