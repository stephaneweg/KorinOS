/* vfs.h : structures communes de vfs (vfs.c : volumes et boucle ; path.c :
 * chemins et dossiers courants ; exec.c : bit d'exécution ; pipes.c : tubes et
 * entrées / sorties standard ; pids.c : fiches des process). Traduction de
 * vfs.asm, path.inc, exec.inc, pipes.inc, pids.inc : mêmes tables, mêmes règles. */
#ifndef KORIN_VFS_H
#define KORIN_VFS_H

#include "fssvc.h"

typedef struct msg vmsg; /* (msg est une macro : le message du thread, vctx) */

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
	u32 idxbusy;  /*   1 : un thread le lit (verrou relâché) */
	u64 idxfail;  /*   heure (ms) du dernier échec de lecture : pas d'autre essai avant IDX_RETRY_MS */
};

/* ---- threads (vfs.c) : plusieurs threads servent vfs (PC), sous un verrou global relâché
 * seulement pendant les appels aux services de fichiers (vol_appel_libre) : un service lent
 * (gdrivefs, smbfs) ne bloque plus les autres requêtes. L'état d'une requête est dans le
 * contexte du thread, au pied de sa pile (VFS_STACK octets, alignée sur VFS_STACK) : vctx. */
#define TETE 128      /* octets lus en tête d'un programme (exec.c) */
#define IARG_MAX 128
#define VFS_STACK 65536
struct ctx {
	vmsg msg;  /* la requête en cours (et sa réponse) */
	vmsg msg2; /* requêtes de vfs à un volume (exec.c) */
	vmsg msg_save;
	long current_req;
	long answered; /* 1 : requête transmise (SVC_FORWARD) */
	int holds;     /* 1 : ce thread tient le verrou global */
	char resolu[PATH_MAX]; /* chemin résolu de la requête */
	char nom_exec[PATH_MAX];
	char nom_proc[32];
	char interp[PATH_MAX]; /* interprète d'un script (#!) */
	char iarg[IARG_MAX];   /* son argument */
	char script_res[PATH_MAX];
	u8 tete[TETE];
} __attribute__((aligned(16)));
#define vctx ((struct ctx *)((u64)__builtin_frame_address(0) & ~(u64)(VFS_STACK - 1)))
#define msg (vctx->msg)
#define msg2 (vctx->msg2)
#define msg_save (vctx->msg_save)
#define current_req (vctx->current_req)
#define answered (vctx->answered)
#define resolu (vctx->resolu)
#define nom_exec (vctx->nom_exec)
#define nom_proc (vctx->nom_proc)
#define interp (vctx->interp)
#define iarg (vctx->iarg)
#define script_res (vctx->script_res)
#define tete (vctx->tete)
long vol_appel_libre(struct volume *v, vmsg *m); /* SVC_CALL, verrou relâché pendant l'attente */

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
struct volume *volume_de(const char *p, const char **rest);
u64 vol_chemin(const struct volume *v, const char *rest, u8 *out, u64 cap);
struct volume *vol_index(struct volume *v);
long vol_lire(struct volume *v, const char *rel, void *buf, u64 max);
void vol_publier(struct volume *v, int remove);

/* ---- ncache.c : le cache des noms (présents et absents) */
#define NC_KEY_MAX 464 /* (FS_DATA_MAX) */
long nc_lookup(const struct volume *v, const char *svcpath);
u64 nc_seq(void);
void nc_learn(const struct volume *v, const char *svcpath, long r, u64 seq);
void nc_forget(const struct volume *v, const char *svcpath);
void nc_flush(void);
void nc_stats(u64 *hits, u64 *neg, u64 *learned);

/* ---- path.c */
extern const char dossier_defaut[];
extern const char chemin_sys[], sp_bin[], sp_sbin[], sp_apps[], sp_main[];
long acces_sbin(long pid, const char *interne); /* access.c : /sbin pour l'uid 0 seulement */
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
const char *home_externe(long uid);
const char *home_compte(long uid);
long acces_exec_bac(long pid, const char *interne); /* (access.c) V_EXEC d'un bac à sable */
int nom_egal_n(const char *a, const char *b, u64 n);  /* homes.c : son dossier personnel (users.ini) */
int home_autre(const char *p, long uid); /* dans le dossier d'un autre compte ? */
#define ACCESS_LOG 0                     /* « access = » de system.ini (étape 3 : log par défaut) */
#define ACCESS_ENFORCE 1
#define ACCESS_OFF 2
int acces_mode(void);
long home_proprio(const char *p);        /* le compte dont le dossier contient p, 0 : aucun */
long session_uid_de(void);               /* le compte de la session, 0 : aucun */
u64 acces_bits(const char *interne);     /* propriétaire et genre pour V_STATX (bits 32-57) */
long acces_verifier(long pid, const char *interne, int ecrire); /* access.c : 0 ou E_ACCES */
long acces_renommer(long pid, const char *src, const char *dst); /* access.c : un dossier qui contient un chemin protégé */
/* ---- watch.c : le guet d'un dossier (VFS_WATCH) */
long watch_op(void);
void watch_touch(const char *interne);  /* un nom a changé : le guet de son dossier */
void watch_pid_gone(long pid);
long watch_flush(void);                 /* les avis dus -> ms jusqu'au prochain (-1 : aucun) */
void bus_publier(const char *topic, const char *val); /* vfs.c : un avis (pas gardé), sans attendre busd */
long pid_uid(long pid);                /* uid du process (0 : system, pid inconnu, noyau sans uid) */
void pid_setuid(long pid, long uid);   /* avis SVC_OP_SETUID */
void pid_stdio_drop(u64 handle);

#endif
