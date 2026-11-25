/* korinrt.h : le runtime des commandes de /bin écrites en C (docs/BIN-C.md).
 *
 * Pas de newlib ni de libposix : un petit runtime au-dessus d'AppKit, comme
 * tool.inc pour les commandes en assembleur. Les mêmes sources donnent la
 * commande du Pi (AArch64, liée à 0x4000000000) et celle du PC (x86-64,
 * liée à 0x400000) : tools/bin/build_c.sh.
 *
 * Une commande :
 *
 *	#include "korinrt.h"
 *	int main(int argc, char **argv)
 *	{
 *		out_puts("bonjour");
 *		out_nl();
 *		return 0;	// sortie vidée, puis SYS_EXIT (comme t_fin)
 *	}
 *
 * L'entrée (crt0.c) fait ce que fait t_debut de tool.inc : ouvre AppKit,
 * prend argv et l'environnement donnés par le père (f_start_blob), sinon
 * coupe la ligne d'arguments du noyau comme t_ligne_coupee (argv[0] vaut
 * alors « commande », comme l'asm).
 *
 * Fichiers : les sys_* passent par file_sys d'AppKit pour les numéros
 * 0x80-0x8E, SYS_PRINT et SYS_FORK, exactement comme la macro sys d'un
 * programme en assembleur (descripteurs 0, 1, 2 compris) ; les autres
 * appels vont au noyau (korin_sys*). */
#ifndef KORINRT_H
#define KORINRT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "korin_kit.h" /* korin_sys0..6, MSG_*, numéros SYS_* (korin_sys_nr.h) */

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t i32;
typedef int64_t i64;

#define NORETURN __attribute__((noreturn))

/* ---- constantes (src/inc/armpi.inc, fs.inc, syscalls.inc) */

#define O_READ 1
#define O_WRITE 2
#define O_CREATE 4
#define O_TRUNC 8
#define O_APPEND 16
#define O_NONBLOCK 32

#define ATTR_DIR 0x10
#define DIRENT_NAME 0
#define DIRENT_SIZE 256
#define DIRENT_ATTR 264
#define DIRENT_BYTES 272

#define PROT_READ 1
#define PROT_WRITE 2

#define E_PERM -1
#define E_NOENT -2
#define E_IO -5
#define E_BADF -9
#define E_AGAIN -11
#define E_NOMEM -12
#define E_EXIST -17
#define E_XDEV -18
#define E_NOTDIR -20
#define E_ISDIR -21
#define E_INVAL -22
#define E_NOSPC -28
#define E_PIPE -32
#define E_NOSYS -38
#define E_NOTEMPTY -39

#define MSG_SIZE 512
#define V_STAT 31
#define V_UTIME 42
#define V_RENAME 43

/* tailles de tool.inc */
#define T_OUT 4096    /* tampon de la sortie standard */
#define T_LINE 65536  /* une ligne lue (T_LINE - 2 octets au plus, le reste perdu) */
#define T_READ 4096   /* une lecture */

/* une entrée de répertoire (SYS_READDIR) */
struct dirent {
	char name[256];
	u64 size;
	u64 attr;
};

/* ---- AppKit (src/kits/appkit/appkit.abi), par les relais de
 * tools/kits/abi2import.py ; puts et malloc renommés ak_puts, ak_malloc */

long ak_puts(const char *s);
char *fmt_dec(u64 v, char *buf);
long print_dec(long v);
char *str_cat(char *d, const char *s);
void *ak_malloc(u64 n);
long str_len(const char *s);
long str_eq(const char *a, const char *b);
void *mem_move(void *d, const void *s, u64 n);
long utf8_put(u32 cp, char *d);
char *path_join(char *d, const char *dir, const char *name);
char *path_parent(char *p);
u64 parse_dec(const char *s);
long str_ends(const char *s, const char *suf);
long file_sys(long a0, long a1, long a2, long a3, long a4, long a5, long nr);
long f_open(const char *path, long flags);
long f_read(long fd, void *buf, u64 n);
long f_write(long fd, const void *buf, u64 n);
long f_seek(long fd, long off, u64 whence);
long f_close(long fd);
long f_stat(const char *path, long *out);
long f_readdir(long fd, void *ent);
long f_mkdir(const char *path);
long f_unlink(const char *path);
long f_chdir(const char *path);
long f_getcwd(char *buf, u64 size);
long f_print(const char *buf, u64 n);
long f_spawn(const char *path, const char *args);
long f_spawn_pipe(const char *path, const char *args, long *pfd);
long f_spawn_stdio(const char *path, const char *args, const long *fds);
long f_spawn_file(long *resp, const char *args, const long *stdio);
long f_pipe(long *fds);
long f_kit_load(const char *name);
long mtx_lock(u32 *m);
long mtx_trylock(u32 *m);
long mtx_unlock(u32 *m);
long cnd_wait(u32 *c, u32 *m);
long cnd_timedwait(u32 *c, u32 *m, long ms);
long cnd_signal(u32 *c);
long cnd_broadcast(u32 *c);
long f_redir(const char *path, long mode);
long f_spawn_env(const char *path, char *const *argv, char *const *envp, const long *fds);
void *f_start_blob(void);
const char *lang_tr(const char *s);
long lang_puts(const char *s);
const char *lang_get(void);
long lang_load(const char *path);
long f_fork(void);
long f_fork_child(void);
long f_pread(long fd, void *buf, u64 n, long pos);
long f_pwrite(long fd, const void *buf, u64 n, long pos);
long f_exec(const char *path, char *const *argv, char *const *envp, const long *fds);
long f_exec_file(long *resp, long *params);
void f_exec_resume(void);

/* ---- démarrage et fin (crt0.c) */

extern int krt_argc;
extern char **krt_argv;
extern long krt_envc;      /* chaînes « NOM=valeur » données par le père (t_envc) */
extern char *krt_env;      /* la première, les autres à la suite (t_env ; 0 : aucune) */
extern const char *krt_line; /* la ligne d'arguments donnée par le noyau (x0, rdi ; 0 : aucune) */
int main(int argc, char **argv);
NORETURN void exit(int code); /* sortie standard vidée, puis SYS_EXIT (t_fin) */
NORETURN void _exit(int code); /* SYS_EXIT sans vider */
const char *getenv(const char *name); /* dans l'environnement du père, 0 sinon */

/* ---- appels système (sys.c) : file_sys pour les fichiers, comme la macro sys */

long ksys(long nr, long a0, long a1, long a2, long a3, long a4, long a5);
long sys_open(const char *path, long flags);
long sys_read(long fd, void *buf, u64 n);
long sys_write(long fd, const void *buf, u64 n);
long sys_seek(long fd, long pos, long whence);
long sys_close(long fd);
long sys_stat(const char *path, long st[2]); /* st[0] taille, st[1] attributs */
long sys_readdir(long fd, struct dirent *e); /* 1, 0 en fin */
long sys_mkdir(const char *path);
long sys_unlink(const char *path);
long sys_chdir(const char *path);
long sys_getcwd(char *buf, u64 size);
long sys_print(const char *s, u64 n);
long sys_sleep(long ms);
long sys_clock(long which); /* SYS_CLOCK_GET : 0 secondes UTC, 1 décalage local, 2 secondes locales */
long sys_time_ms(void);
long sys_getpid(void);
void *sys_vm_map(u64 size); /* région à zéro, peuplée à la demande (lecture, écriture) */

/* ---- sortie tamponnée (io.c), comme t_putc, t_puts... de tool.inc */

void out_putc(int c);
void out_puts(const char *s);
void out_write(const void *buf, u64 n);
void out_nl(void);
void out_putn(u64 v);          /* décimal non signé (t_putn) */
void out_putw(u64 v, int w);   /* aligné à droite sur w colonnes */
void out_flush(void);
void out_say(const char *s);   /* traduit (lang_tr), puis out_puts (t_dire) */

/* erreurs : la sortie standard est vidée d'abord, puis l'erreur standard (fd 2) */
void err_puts(const char *s);  /* t_err */
void err_say(const char *s);   /* traduit (t_derr) */
void err_msg(const char *s);   /* « nom: texte traduit\n » (t_err_msg) */
void err_file(const char *path); /* « nom: chemin : inaccessible\n » (t_err_fic) */
void err_file_e(const char *path, long e); /* E_NOSPC : « nom: chemin : disque plein\n », sinon err_file */
extern long krt_out_err; /* première erreur d'écriture de la sortie standard (out_flush) */

/* ---- formatage (format.c) : %d %i %u %x %X %o %c %s %p %%, longueurs l ll z,
 * largeur, précision (%.Ns), drapeaux - 0 + espace, * */

int printf(const char *fmt, ...);
int eprintf(const char *fmt, ...); /* vers l'erreur standard (sortie vidée d'abord) */
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int vformat(void (*put)(void *ctx, const char *s, size_t n), void *ctx, const char *fmt, va_list ap);

/* ---- lecture tamponnée (io.c), comme t_ouvrir, t_getc, t_lire_ligne */

struct reader {
	long fd;
	u64 n, i;
	u8 buf[T_READ];
};
extern struct reader krt_in;   /* le lecteur commun (t_fd, t_lu) */
extern char krt_line_buf[T_LINE]; /* la ligne de rd_line (t_ligne) */
long in_open(const char *path); /* 0 ou « - » : entrée standard (0) ; sinon sys_open(O_READ) */
void in_close(long fd);         /* rien pour 0 */
void rd_init(struct reader *r, long fd);
int rd_getc(struct reader *r);  /* octet, -1 à la fin (une fin n'est pas retenue : relit) */
char *rd_line(struct reader *r, u64 *len); /* ligne sans fin de ligne, dans krt_line_buf ; 0 à la fin */

/* ---- fichiers (sys.c), comme tool.inc */

int path_type(const char *path); /* 1 dossier, 0 fichier, -1 absent (t_type) */
void path_join2(char *d, const char *dir, const char *name); /* t_join (« / » sauf après « / » ou « : ») */
char *path_base(char *path); /* son dernier composant, dans le chemin (t_base) */
int is_opt(const char *s);   /* « -x... » (pas « - ») : t_est_opt */
long vfs_call(void *msg);    /* message de 512 o au service vfs (t_vfs) */
void vfs_msg(void *msg, long op, long a, long b, const char *p1, const char *p2); /* t_msg */
long vfs_rename(const char *from, const char *to); /* E_XDEV : deux volumes */
long vfs_utime(const char *path, u64 secs);
long copy_file(const char *src, const char *dst); /* 0 ou erreur (t_copier) */

/* ---- mémoire et chaînes (string.c) */

void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
size_t strlcpy(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
char *strdup(const char *s);
int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int isprint(int c);
int tolower(int c);
int toupper(int c);

/* ---- nombres (string.c) */

long strtol(const char *s, char **end, int base);
u64 strtoul(const char *s, char **end, int base);
int atoi(const char *s);
char *u64_to_str(u64 v, char *buf); /* décimal, rend la fin (fmt_dec sans AppKit) */

/* ---- tas (heap.c) : blocs rendus et réutilisés ; au-dessus de SYS_SBRK */

void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void *realloc(void *p, size_t n);
void free(void *p);

/* ---- tri (sort.c) : tri fusion stable (l'ordre des égaux est gardé) */

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
void sort_ptrs(void **v, size_t n, int (*cmp)(const void *, const void *, void *), void *ctx);

/* ---- dates (time.c), comme civil de date.asm et dostime.inc */

struct civil {
	u64 year, month, day, hour, min, sec; /* mois 1..12, jour 1..31 */
	u64 wday; /* 0 dimanche */
	u64 yday; /* 0..365 */
};
void civil_from_secs(u64 secs, struct civil *c);
u64 secs_from_civil(u64 year, u64 month, u64 day, u64 hour, u64 min, u64 sec);

/* ---- expressions régulières (regex.c) : le moteur de src/inc/regex.inc
 * (grep, sed, ed), traduit pas à pas : le motif devient une petite machine
 * exécutée avec retour arrière. Syntaxe de base : . [abc] [^a-z] * ^ $
 * \( \) \| \+ \? ; RE_E (étendue) : ( ) | + ? ; \t, \n ; pas de {n,m} ni
 * de références arrière ; RE_I : sans la casse (ASCII). En base, ^ n'est une ancre qu'en tête
 * de branche et $ qu'en fin de branche (sinon des lettres). */

#define RE_E 1
#define RE_I 2
#define RE_CAPS 20 /* captures : [0] [1] le tout, [2n] [2n+1] le groupe n (1..9) */
struct regex;
struct regex *re_compile(const char *pat, int flags); /* 0 : mémoire, ou motif trop long */
/* line : la ligne entière (pour ^), start : où commencer -> 1 si trouvé ;
 * caps : pointeurs dans line, 0 pour un groupe absent */
int re_search(struct regex *re, const char *line, const char *start, const char *caps[RE_CAPS]);

/* ---- lot « net » : les commandes réseau (net.c), comme src/inc/nettool.inc ;
 * pour les commandes qui importent netkit (bin.conf « import = netkit » :
 * leurs fonctions viennent de netkit_import.h) */

#define SOCK_TCP 1
#define SOCK_UDP 2
#define NI_IP 0 /* netinfo (32 o), src/kits/netkit/netkit.inc */
#define NI_MASK 4
#define NI_GW 8
#define NI_DNS 12
#define NI_MAC 16
#define NI_UP 24
#define NI_DHCP 28
#define NT_LINE 1024 /* une ligne de réponse (FTP, HTTP, WHOIS) */
#define NT_RX 4096   /* tampon de réception d'une connexion */
#define E_NT_CONNREFUSED -111
#define E_NT_HOSTUNREACH -113
#define E_NT_TIMEDOUT -110
#define E_NT_CONNRESET -104

struct nt_reader {
	long sd;
	u64 n, i;
	u8 rx[NT_RX];
};
extern struct nt_reader nt_in; /* la connexion lue par lignes (nt_sd, nt_rx_n, nt_rx_i, nt_rx) */
extern char nt_line_buf[NT_LINE]; /* la ligne de nt_line (nt_l) */
long nt_network(void);            /* 1 si le réseau a une adresse, après 20 s de DHCP au plus */
long nt_tcp(const char *host, long port, u32 *ip); /* socket connecté, < 0 ; *ip (0 : sans) */
long nt_tcp_ip(u32 ip, long port);
long nt_send_str(long sd, const char *s);
void nt_read_start(long sd);
int nt_getc(void);                /* -1 : fin, erreur ou 30 s sans rien */
char *nt_line(void);              /* sans CR ni LF ; 0 à la fin */
void out_col(const char *s, u64 w); /* t_col : texte, espaces jusqu'à w (une au moins) */
void nt_put_ip(u32 ip);           /* sur la sortie tamponnée */
void nt_err_text(long e);         /* texte traduit d'une erreur réseau, sur 2, sans fin de ligne */

/* ---- lot « files » : fichiers et volumes (volutil.c), comme src/inc/volutil.inc et fs.inc ;
 * pour ls, chmod, df, mount, eject, mkfs, fscache */

#define ATTR_EXEC 0x400 /* (vfs, V_STATX) fichier exécutable */
#define V_STDIO 41      /* -> a entrée, b sortie, c erreur standard (VT_TAG : bout de vterm) */
#define V_STATX 46      /* comme V_STAT, c + ATTR_EXEC pour un exécutable */
#define V_CHMOD 47      /* a = mode ; chemin */
#define VT_TAG 0x40000000
#define VT_GWINSZ 7     /* (a bout) -> a lignes, b colonnes */
#define FS_CACHE 18
#define FS_INFO 19
#define FS_SYNC 25
#define FS_T_FAT32 1
#define FS_T_EXFAT 2
#define FS_T_RAM 3
#define FS_T_NET 4  /* smbfs, gdrivefs */
#define FS_T_EXT4 5 /* ext4fs */
#define FS_CC_ON 1
#define FS_CC_OFF 2
#define FS_CC_DROP 3
#define VFS_MOUNT 20
#define VFS_UNMOUNT 21
#define VFS_VOLUMES 22
#define VFS_MOUNTS 53 /* -> b = nombre ; données : par volume, nom interne, 0, point de montage, 0 */
#define VFS_BOOT_BEGIN 50 /* -> données « /boot » (a = 1 : à rendre par VFS_BOOT_END) ou « » (la racine) */
#define VFS_BOOT_END 51
#define VFS_VOLINFO 49 /* données : volume -> données : volume du service (« fs.<vol> »), 0, rootpath, 0 */
#define VFS_WATCH 58 /* a = rang ; données : dossier (vide : retiré) -> avis korin.fs.watch.<pid> (vfs/watch.c) */
#define FS_DATA_MAX 464
#define VU_MAX 8      /* volumes d'une réponse de vfs (464 / 56) */
#define VU_ENT 56     /* entrée de VFS_VOLUMES : nom 16, source 24, service 16 */
#define VU_SRC 16
#define VU_FS 40
#define VU_MOUNTS 4096 /* mounts.txt lu au plus */
#define E_BUSY_VU -16

extern u8 vu_msg[MSG_SIZE];  /* réponse de vfs (la liste : vu_msg + MSG_DATA) */
extern u8 vu_rep[MSG_SIZE];  /* réponse du service de fichiers (FS_INFO, FS_SYNC) */
extern char vu_vol[16];      /* « usb0 » */
extern char vu_aff[64];      /* « /media/usb0 » : le point de montage */
extern char vu_src[256];
extern char vu_srv[256];
extern char vu_root[64];     /* rootpath (vu_source, mount) ; vide : la racine */
long vu_name(const char *arg);    /* -> vu_vol, vu_aff ; longueur (0 : aucun) (vu_nom) */
long vu_list(void);               /* volumes montés, < 0 sans vfs (vu_liste) */
char *vu_find(const char *name);  /* entrée de VFS_VOLUMES, 0 (vu_trouver) */
long vu_fs_op(const char *name, long op);
long vu_sync(const char *name);   /* FS_SYNC -> fichiers ouverts, < 0 */
long vu_info(const char *name);   /* FS_INFO -> 0, < 0 ; vu_rep */
const char *vu_type_name(u64 t);  /* FS_T_* -> « FAT32 »... (vu_type_nom) */
void vu_size(u64 bytes);          /* « 37,5 Mo » sur la sortie tamponnée (vu_taille) */
int vu_source(const char *name);  /* -> vu_src, vu_srv ; 1 si trouvés */
long vu_mount(const char *name, const char *src, const char *srv); /* VFS_MOUNT (vu_monter) */
long vu_mount_root(const char *name, const char *src, const char *srv, const char *root);
long vu_volinfo(const char *name, char *svc, char *root); /* VFS_VOLINFO : svc 16 o, root 64 o -> 0, < 0 */
long vu_unmount(const char *name); /* VFS_UNMOUNT, puis 5 s au plus (vu_demonter) */
const char *vu_boot_begin(void); /* -> « /boot » (monté à la demande) ou « » (la racine) */
void vu_boot_end(void);           /* rendu (le dernier : /boot démonté) */
void vu_err(long e);              /* texte traduit sur 2, sans fin de ligne */

/* ---- réglages personnels (conf.c, docs/etude/MULTI-UTILISATEUR.md §2.5) : lus dans
 * HOME:/etc/<nom>, sinon HOME:/.config/korin/<nom> (15/11), sinon /etc/<nom> ; écrits dans HOME:/etc/<nom> */
const char *conf_path(const char *name, int write); /* tampon commun */
/* ce qu'on ne lance pas au démarrage (conf.c, docs/DEMARRAGE.md) : /etc/services.off, session.off */
long list_load(const char *path, char *buf, long size); /* -> octets lus (0 : absent, buf vide) */
int list_has(const char *list, const char *line);       /* 1 si la ligne (blancs autour ignorés) y est */

/* ---- comptes (users.c, docs/etude/MULTI-UTILISATEUR.md §2.1) : /etc/users.ini */
long user_uid(const char *name);  /* uid du compte (name = 0 : le premier d'uid >= 1000), -1 : aucun */
long user_session_uid(void);      /* le compte de la session (autologin de system.ini, sinon le premier) */
long spawn_session(const char *path, const char *args); /* spawn avec l'uid de la session (sinon spawn) */
void spawn_session_next(void);    /* le prochain lancement (spawn_stdio...) avec l'uid de la session */

/* ---- lot « sys » : process et système (notify.c), comme src/inc/notify.inc et
 * src/inc/preload_notify.inc ; pour notify et preload */

long notify_send(const char *title, const char *text, const char *app); /* notifier_app : < 0 sans notifyd */
char *preload_name(const char *path, char *out); /* « /apps/jet.app/main » -> « jet » (64 o) ; sa fin */
void preload_notify(const char *path);           /* « Jet est préchargé » */

/* ---- lot « ui » : essais du serveur de fenêtres et d'UIKit (ui.c) ; pour les
 * commandes qui importent uikit (bin.conf « import = uikit » : uikit_import.h) */

/* src/inc/armpi.inc, ws_ext.inc : messages du serveur graphique (SYS_WS_CALL) */
#define WS_MSG 512
#define WM_OP 0
#define WM_A 8
#define WM_B 16
#define WM_C 24
#define WM_D 32
#define WM_LEN 40 /* 32 bits */
#define WM_PID 44 /* 32 bits */
#define WM_DATA 48
#define WS_OP_CREATE 1
#define WS_OP_UPDATE 2
#define WS_OP_SNAPSHOT 13
#define WS_OP_EXIT 0xFFFF
#define WS_SF_XRGB8888 0
#define WS_SF_RGB565 1
#define WF_RESIZABLE 512
/* événements (ui_event, SYS_EV_WAIT : 32 o) */
#define WEV_KEY 1
#define WEV_MOUSE 2
#define WEV_CLOSE 3
#define WEV_MENU 7
#define WEV_FRAME 10
#define EV_TYPE 0
#define EV_CODE 16
#define EV_WIN 28
/* src/kits/uikit/uikit.inc */
#define UI_FONT_SIZE 320
#define UN_ACTIVATE 2
#define UM_OK 0
#define ULB_BOLD 1
#define ULB_DIM 2
#define TJ_FACE 0
#define TJ_ACCENT 40

/* les variables de l'interface : la macro ui_data de uikit.inc, champ pour
 * champ (ui_win, ui_surf, ui_w, ui_h, ui_ev, ui_font, ui_font_b, ui_font_m) */
struct krt_ui {
	long win;
	u32 *surf;
	long w, h;
	u8 ev[32];
	u8 font[UI_FONT_SIZE], font_b[UI_FONT_SIZE], font_m[UI_FONT_SIZE];
};

/* une fonction d'un kit qui rend plusieurs registres, comme l'asm (x0..x3 ;
 * kit.conf « ret = » : ui_w_event, ui_surface_pixels, ui_clip_get_files...) :
 * les relais générés n'en rendent qu'un. Sur x86-64, la fonction du kit rend
 * une structure de quatre long (tools/kits/abi2table.py, --ret). */
struct krt_r4 {
	long r0, r1, r2, r3;
};
struct krt_r4 krt_call_r4(long (*fn)(), long a0, long a1, long a2, long a3);

/* ---- lot « sound » : son et médias (sound.c) ; pour play, soundctl, volume, sontest,
 * synthtest, codectest, mediatest (bin.conf « import = audiokit » : audiokit_import.h) */

/* src/kits/audiokit/audiokit.inc */
#define AKF_WAV 1
#define AKF_FLAC 2
#define AKF_MP3 3
#define AKF_VORBIS 4
#define AKF_OPUS 5
#define AKF_MIDI 6
#define AKF_FMS 7
#define AKI_FORMAT 0 /* ak_dec_info : 8 mots de 64 bits */
#define AKI_RATE 1
#define AKI_CHANNELS 2
#define AKI_FRAMES 3
#define AKI_DURATION 4
#define AKI_BITRATE 5
#define AKI_POSITION 6
#define AKI_BITS 7
#define AKI_WORDS 8
#define AKT_TITLE 0
#define AKT_ARTIST 1
#define AKT_ALBUM 2
#define AKE_NOMEM -12
#define AKE_INVAL -22
#define AKE_FORMAT -95
#define AKE_CORRUPT -74
#define AKE_IO -5
#define AKP_PLAYING 1
#define AKP_PAUSED 2
#define AKP_ENDED 3
#define AKP_ERROR 4
#define AKW_SQUARE 0
#define AKW_SINE 1
#define AKW_TRIANGLE 2
#define AKW_SAW 3
#define AKW_NOISE 4
#define AKW_FM 5
#define AKFF_SUSTAINED 1
#define AKFF_TREMOLO 2
#define AKFF_VIBRATO 4
#define AKS_FM 1
#define AKS_NOFX 2

/* src/inc/sound.inc : le tampon d'état du service sound (ak_state), champs de 32 bits */
#define SND_RATE 44100 /* (armpi.inc) */
#define SND_OUT_USB 3
#define SND_IN_USB 3
#define SND_OP_DRV_OUTPUT 20
#define SND_OP_DRV_INPUT 21
#define SND_OP_DRV_ALIVE 22
#define SND_ID_INPUT 0x1000
#define SR_HEAD 0 /* anneaux partagés */
#define SR_TAIL 8
#define SR_PLAYED 40
#define SR_LOST 48
#define SR_WANTED 64
#define SR_DATA 4096
#define SR_OUT_BYTES 32768
#define SR_IN_BYTES 65536
#define SS_GEN 0     /* 64 bits */
#define SS_MAIN 8    /* 64 bits */
#define SS_OUTS 64
#define SS_OUT_MAX 8
#define SO_ID 0
#define SO_TYPE 4
#define SO_RATE 8
#define SO_VOLUME 12
#define SO_MUTE 16
#define SO_FLAGS 20
#define SO_PEAK 24
#define SO_NAME 32
#define SS_CLIS (64 + SS_OUT_MAX * 64)
#define SS_CLI_MAX 32
#define SC_PID 0
#define SC_VOLUME 4
#define SC_MUTE 8
#define SC_ROUTE 12
#define SC_OUTPUT 16
#define SC_PEAK 20
#define SC_STREAMS 24
#define SC_NAME 32
#define SS_INS (SS_CLIS + SS_CLI_MAX * 64)
#define SS_IN_MAX 4
#define SS_U32(p, off) (*(const u32 *)((const u8 *)(p) + (off)))

/* coupe s sur place aux espaces (seulement), au plus max mots -> nombre de mots (la boucle de
 * play.asm et codectest.asm ; s = 0 : aucun) */
long krt_split_spaces(char *s, char **words, long max);
/* proc_find de procfind.inc : nom -> pid (0 : aucun), après le pid after (0 : depuis le début) */
long proc_find(const char *name, long after);

#endif
