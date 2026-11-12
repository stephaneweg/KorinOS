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

#endif
