/* AppKit : ce qui fait tourner un programme (console, nombres, chaînes, tas,
 * fichiers, tubes, lancement des programmes, fork, exec, langue, verrous), en
 * C commun au Pi et au PC ; remplace appkit.asm et ses .inc (appsA_util.inc,
 * fileclient.inc, spawnenv.inc, exec.inc, lang.inc, sync.inc, et les fonctions
 * de src/inc/lib.inc), même table (appkit.abi), mêmes résultats, mêmes codes
 * d'erreur, bizarreries comprises (signalées « (comme l'asm) »).
 *
 *  * Client de fichiers (docs/FICHIERS.md) : descripteurs 64..127 dans les
 *    variables du kit (une copie par process, partagée par ses threads) ;
 *    chemins au service vfs (ops 30-47), lectures et écritures directement au
 *    service FS (ops 10-17, 26), tubes à vfs, entrées / sorties standard
 *    (V_STDIO) au premier usage, pseudo-terminaux de vterm.
 *  * file_sys : appelée par la macro sys de tout programme en assembleur à
 *    la place d'un svc 0. Sur le Pi, son relais (kit.conf : keep_all, x8_arg)
 *    garde x1-x18 et NZCV et lui passe x8 (le numéro) en 7e argument ; sur PC,
 *    le numéro est le 7e argument (System V).
 *  * Toutes les fonctions ont, sur le Pi, un relais qui garde x1-x18 et NZCV
 *    (les appelants en assembleur comptent sur des registres que l'asm ne
 *    touchait pas : ftpd après fmt_dec, threadtest après mtx_unlock) ;
 *    f_exec_resume garde aussi x0. f_fork revient deux fois (le fils a une
 *    copie de la pile), f_exec ne revient pas s'il réussit.
 *  * Les chargeurs de kits ne font pas de relocations : aucune adresse dans
 *    les variables initialisées (chaînes constantes lues par le code).
 * Compilé sans registres flottants sur AArch64 (-mgeneral-regs-only). */
#include "../common/korin_kit.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define EXPORT __attribute__((noinline, used))
#define NOINL __attribute__((noinline))

/* ---- constantes (src/inc/armpi.inc, fs.inc, vterm.inc, syscalls.inc) */

#define E_PERM -1
#define E_NOENT -2
#define E_2BIG -7
#define E_NOEXEC -8
#define E_BADF -9
#define E_AGAIN -11
#define E_FAULT -14
#define E_BUSY -16
#define E_NOTDIR -20
#define E_ISDIR -21
#define E_INVAL -22
#define E_MFILE -24
#define E_NOSPC -28
#define E_SPIPE -29
#define E_NOSYS -38
#define E_TIMEDOUT -110

#define O_READ 1
#define O_WRITE 2
#define O_APPEND 16
#define O_NONBLOCK 32

#define ATTR_DIR 0x10
#define ATTR_SYS 0x100
#define ATTR_LIB 0x200
#define ATTR_SCRIPT 0x800
#define DIRENT_BYTES 272

#define PROC_RIGHTS 0x100
#define PROC_EXEC 0x200
#define INIT_OP_LAUNCH 1
#define INIT_OP_KIT_LOAD 2
#define BUF_VA_BASE 0x6800000000UL

#define FS_READ 10
#define FS_WRITE 11
#define FS_CLOSE 12
#define FS_READDIR 13
#define FS_BUFFER 14
#define FS_READ_BUF 15
#define FS_WRITE_BUF 16
#define FS_READ_DIRECT 17
#define FS_SEEK 26
#define FS_POS_CUR (-1L)
#define FS_DIRECT_MIN 65536UL
#define FS_DIRECT_MAX 0x100000UL
#define FS_BUFFER_SIZE 65536UL
#define FS_DATA_MAX 464UL

#define V_OPEN 30
#define V_STAT 31
#define V_MKDIR 32
#define V_UNLINK 33
#define V_CHDIR 34
#define V_GETCWD 35
#define V_EXEC 36
#define V_PIPE 37
#define V_PIPE_READ 38
#define V_PIPE_WRITE 39
#define V_PIPE_CLOSE 40
#define V_STDIO 41
#define V_FILE_END 44
#define V_END_FILE 45

#define VT_TAG 0x40000000L
#define VT_READ 2
#define VT_WRITE 3
#define VT_CLOSE 4

#define IMG_PIN 1
#define IMS_ABORT 1
#define EXP_NAME 0
#define EXP_ARGS 1
#define EXP_OPTIONS 2
#define EXP_STDIN 3
#define EXP_IMAGE 6
#define EXP_MARKS 7
#define EXP_NMARKS 8
#define EXP_N 9 /* EXP_SIZE = 72 octets */
#define EXEC_FC_TAG 0x40000000U
#define IPC_INHERIT_MAX 255
#define PROT_READ 1
#define PROT_WRITE 2

/* fileclient.inc */
#define FC_BASE 64
#define FC_MAX 64
#define FC_PARTAGE 0x100
#define FC_CANAUX 32
#define FC_CHEMIN 255
#define FC_IMAGE_MAX 0x4000000UL
#define FC_KIT_NOM 23
/* spawnenv.inc */
#define BLOB_TAG 0x80000000U
#define BLOB_MAGIC 0x4E505341U
#define BLOB_CHAINES 16
#define LIGNE_MAX 1024
#define SA_MAX 256
/* exec.inc */
#define FCX_MAGIC 0x58454346U
#define FCX_ENT_SIZE (8 + 48)
/* lang.inc */
#define LANG_SEAUX 1024

/* ---- message des services (512 octets) */

typedef long msg_t[SVC_MSG / 8];
#define M_OP(m) ((m)[0])
#define M_A(m) ((m)[1])
#define M_B(m) ((m)[2])
#define M_C(m) ((m)[3])
#define M_D(m) ((m)[4])
#define M_LEN(m) (*(u32 *)((char *)(m) + MSG_LEN))
#define M_DATA(m) ((char *)(m) + MSG_DATA)

/* ---- variables (une copie par process) */

struct fcd {   /* FC_SIZE = 48 */
	long type; /* 0 libre, 1 fichier, 2 répertoire, 3 en ouverture, 4 bout de tube */
	long canal;
	long num;
	long pos;
	long taille;
	long drap;
};

static u32 fc_verrou;
static struct fcd fc_desc[FC_MAX];
static long fc_tampons[FC_CANAUX]; /* adresse mappée (0 : pas encore, -1 : refus) */
static long fc_tnum[FC_CANAUX];
static u32 fc_tverrous[FC_CANAUX];
static long fc_stdio[3]; /* entrée, sortie, erreur : descripteur (0 : console) */
static u32 fc_stdio_ok;
static u32 fc_sverrou;
static u32 fc_exec_etat; /* état d'avant un exec : 0 à voir, 1 en cours, 2 fait */

static long lang_ok;
static char *lang_seaux_p; /* LANG_SEAUX listes { source, traduction, suivante } */
static char lang_cur[8];
static char lang_chemin[64];

/* ---- prototypes des fonctions exportées (ordre de appkit.abi) */

long puts(const char *s);
char *fmt_dec(u64 v, char *buf);
long print_dec(long v);
char *str_cat(char *d, const char *s);
void *malloc(u64 n);
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
static int fc_spawn_uid = -1; /* SYS_SPAWN_AS : uid du prochain fils (-1 : celui du père) */
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

static long f_preload(const char *path);
static long f_unload(const char *path);

/* =========================================================================
 *  src/inc/lib.inc, appsA_util.inc : console, nombres, chaînes, tas
 * ========================================================================= */

/* x0 = chaîne -> sortie standard (f_print) */
EXPORT long puts(const char *s)
{
	return f_print(s, strlen(s));
}

/* valeur non signée, tampon -> fin (sur le 0 final) */
EXPORT char *fmt_dec(u64 v, char *buf)
{
	char t[32];
	int n = 0;
	do {
		t[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n)
		*buf++ = t[--n];
	*buf = 0;
	return buf;
}

/* valeur signée -> console */
EXPORT long print_dec(long v)
{
	char b[32];
	char *p = b;
	u64 u = (u64)v;
	if (v < 0) {
		*p++ = '-';
		u = -u;
	}
	fmt_dec(u, p);
	return puts(b);
}

/* destination, source -> fin (sur le 0 final) */
EXPORT char *str_cat(char *d, const char *s)
{
	while ((*d = *s++))
		d++;
	return d;
}

/* taille -> bloc du tas (jamais rendu, 16 octets), 0 si échec */
EXPORT void *malloc(u64 n)
{
	long r = korin_sys1(SYS_SBRK, (n + 15) & ~(u64)15);
	return r < 0 ? 0 : (void *)r;
}

EXPORT long str_len(const char *s)
{
	return (long)strlen(s);
}

EXPORT long str_eq(const char *a, const char *b)
{
	for (;;) {
		u8 c = (u8)*a++;
		if (c != (u8)*b++)
			return 0;
		if (!c)
			return 1;
	}
}

/* recouvrement permis ; -> fin de la destination (copie vers l'avant), son
 * début (vers l'arrière) (comme l'asm) */
EXPORT void *mem_move(void *d, const void *s, u64 n)
{
	u8 *a = d;
	const u8 *b = s;
	if (!n)
		return d;
	if (a > b) {
		while (n--)
			a[n] = b[n];
		return d;
	}
	while (n--)
		*a++ = *b++;
	return a;
}

/* point de code, destination -> octets écrits (1..3 ; au-delà de 0xFFFF :
 * trois octets tronqués, comme l'asm) */
EXPORT long utf8_put(u32 cp, char *d)
{
	u8 *o = (u8 *)d;
	if (cp < 0x80) {
		o[0] = (u8)cp;
		return 1;
	}
	if (cp < 0x800) {
		o[0] = (u8)((cp >> 6) | 0xC0);
		o[1] = (u8)((cp & 0x3F) | 0x80);
		return 2;
	}
	o[0] = (u8)((cp >> 12) | 0xE0);
	o[1] = (u8)(((cp >> 6) & 0x3F) | 0x80);
	o[2] = (u8)((cp & 0x3F) | 0x80);
	return 3;
}

/* « rep/nom » ; -> fin. (Un répertoire vide : l'octet d'avant la destination
 * est lu, comme l'asm.) */
EXPORT char *path_join(char *d, const char *dir, const char *name)
{
	char *e = str_cat(d, dir);
	if (((volatile u8 *)e)[-1] != '/')
		*e++ = '/';
	return str_cat(e, name);
}

/* retire le dernier composant (« /a/b » -> « /a », « /a » -> « / », « HOME:/a » -> « HOME:/ ») ;
 * « / » ne change pas ; -> le chemin */
EXPORT char *path_parent(char *p)
{
	char *last = p;
	for (char *q = p; *q; q++)
		if (*q == '/')
			last = q;
	if (last == p || last[-1] == ':')
		last++;
	if (*last)
		*last = 0;
	return p;
}

/* chiffres décimaux au début -> valeur (non signée, en boucle au-delà de 64 bits) */
EXPORT u64 parse_dec(const char *s)
{
	u64 v = 0;
	for (;;) {
		u32 c = (u32)(u8)*s++ - '0';
		if (c > 9)
			return v;
		v = v * 10 + c;
	}
}

/* la chaîne finit-elle par le suffixe (casse ASCII : bit 0x20 forcé des deux
 * côtés, comme l'asm) -> 1 / 0 */
EXPORT long str_ends(const char *s, const char *suf)
{
	u64 ls = strlen(s), lf = strlen(suf);
	if (lf > ls)
		return 0;
	const u8 *a = (const u8 *)s + (ls - lf);
	const u8 *b = (const u8 *)suf;
	for (;;) {
		u32 c1 = *a++, c2 = *b++;
		if (!c2)
			return 1;
		if ((c1 | 0x20) != (c2 | 0x20))
			return 0;
	}
}

/* =========================================================================
 *  fileclient.inc : client de fichiers
 * ========================================================================= */

/* verrous du kit (attente : yield) */
static void fc_take(u32 *l)
{
	for (;;) {
		u32 z = 0;
		if (__atomic_compare_exchange_n(l, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			return;
		korin_sys0(SYS_YIELD);
	}
}

static void fc_release(u32 *l)
{
	__atomic_store_n(l, 0, __ATOMIC_RELEASE);
}

static void fc_copy(void *d, const void *s, u64 n)
{
	u8 *a = d;
	const u8 *b = s;
	while (n--)
		*a++ = *b++;
}

static void fc_exec_resume(void);

/* état d'avant un exec, repris au premier usage d'un descripteur */
static inline void fc_exec_check(void)
{
	if (__atomic_load_n(&fc_exec_etat, __ATOMIC_ACQUIRE) != 2)
		fc_exec_resume();
}

/* -> entrée réservée (type 3), *rang ; 0 : table pleine */
static struct fcd *fc_reserve(long *rang)
{
	fc_exec_check();
	fc_take(&fc_verrou);
	struct fcd *d = 0;
	long i;
	for (i = 0; i < FC_MAX; i++)
		if (!fc_desc[i].type) {
			d = &fc_desc[i];
			d->type = 3;
			break;
		}
	fc_release(&fc_verrou);
	*rang = i;
	return d;
}

/* descripteur -> son entrée (fichier, répertoire, bout de tube), 0 sinon */
static struct fcd *fc_desc_of(long fd)
{
	fc_exec_check();
	u64 i = (u64)fd - FC_BASE;
	if (i >= FC_MAX)
		return 0;
	struct fcd *d = &fc_desc[i];
	if (d->type == 4 || (u64)(d->type - 1) <= 1)
		return d;
	return 0;
}

/* message de chemin -> 0, E_INVAL si trop long (254 caractères au plus) */
static long fc_msg_path(long *m, long op, long a, const char *path)
{
	M_OP(m) = op;
	M_A(m) = a;
	M_D(m) = 0;
	char *o = M_DATA(m);
	for (u32 i = 0; i < FC_CHEMIN; i++) {
		char c = path[i];
		o[i] = c;
		if (!c) {
			M_LEN(m) = i + 1;
			return 0;
		}
	}
	return E_INVAL;
}

static long fc_svc_call(long canal, long *m)
{
	return korin_sys2(SYS_SVC_CALL, canal, m);
}

/* message rempli : au service vfs -> résultat (E_NOENT sans vfs) */
static long fc_vfs(long *m)
{
	long ch = korin_sys2(SYS_SVC_OPEN, "vfs", 0);
	if (ch < 0)
		return E_NOENT;
	return fc_svc_call(ch, m);
}

/* op V_*, chemin : requête sans réponse à garder */
static NOINL long fc_path_only(long op, const char *path)
{
	msg_t m;
	long r = fc_msg_path(m, op, 0, path);
	if (r)
		return r;
	return fc_vfs(m);
}

/* canal d'un service FS -> son tampon partagé mappé (0 : aucun), *lock son
 * verrou ; demandé au premier besoin (FS_BUFFER, buf_map), un refus gardé */
static NOINL long fc_buffer(long canal, u32 **lock)
{
	if ((u64)canal >= FC_CANAUX)
		return 0;
	long t = fc_tampons[canal];
	if (t == -1)
		return 0;
	if (!t) {
		msg_t m;
		fc_take(&fc_verrou);
		if (!fc_tampons[canal]) {
			M_OP(m) = FS_BUFFER;
			M_LEN(m) = 0;
			long r = fc_svc_call(canal, m);
			long a = 0;
			if (r >= 0) {
				long num = M_A(m);
				a = korin_sys1(SYS_BUF_MAP, num);
				if (a > 0) {
					fc_tnum[canal] = num;
					fc_tampons[canal] = a;
				}
			}
			if (r < 0 || a <= 0)
				fc_tampons[canal] = -1;
		}
		fc_release(&fc_verrou);
		t = fc_tampons[canal];
		if (t == -1)
			return 0;
	}
	*lock = &fc_tverrous[canal];
	return t;
}

/* canal : son tampon n'est plus reconnu (service relancé) : démappé */
static NOINL void fc_buffer_forget(long canal)
{
	if ((u64)canal >= FC_CANAUX)
		return;
	fc_take(&fc_verrou);
	long t = fc_tampons[canal];
	if (t && t != -1) {
		fc_tampons[canal] = 0;
		korin_sys1(SYS_BUF_FREE, fc_tnum[canal]);
	}
	fc_release(&fc_verrou);
}


/* --- open(chemin, drapeaux) -> descripteur */
EXPORT long f_open(const char *path, long flags)
{
	msg_t m;
	long rang;
	struct fcd *d = fc_reserve(&rang);
	if (!d)
		return E_MFILE;
	long r = fc_msg_path(m, V_OPEN, flags, path);
	if (!r)
		r = fc_vfs(m);
	if (r < 0) {
		d->type = 0;
		return r;
	}
	d->canal = M_A(m);
	d->num = M_B(m);
	d->taille = M_D(m);
	d->drap = flags;
	d->pos = (flags & O_APPEND) ? M_D(m) : 0;
	__atomic_store_n(&d->type, (M_C(m) & ATTR_DIR) ? 2 : 1, __ATOMIC_RELEASE); /* (prêt) */
	return rang + FC_BASE;
}

/* --- read / write / pread / pwrite. Comme le relais du noyau : une lecture
 * s'arrête au premier morceau court (fin du fichier), une écriture va
 * jusqu'au bout ; une erreur après des octets rend ces octets. pos < 0 : la
 * position du descripteur. */

/* b d'un transfert : la position explicite, FS_POS_CUR (partagé) ou FCD_POS */
static inline long fc_pos_send(const struct fcd *d, long pos)
{
	if (pos >= 0)
		return pos;
	if (d->drap & FC_PARTAGE)
		return FS_POS_CUR;
	return d->pos;
}

/* got octets transférés, rd = d de la réponse (position après) */
static inline void fc_pos_advance(struct fcd *d, long *pos, u64 got, long rd)
{
	if (*pos >= 0)
		*pos += (long)got;
	else if (d->drap & FC_PARTAGE)
		d->pos = rd;
	else
		d->pos += (long)got;
}

static NOINL long fc_rw(long fd, u8 *buf, u64 n, int wr, long pos)
{
	msg_t m;
	struct fcd *d = fc_desc_of(fd);
	if (!d)
		return E_BADF;
	if (d->type == 2)
		return E_ISDIR;
	if (d->type == 4 && pos >= 0) /* un tube : pas de position explicite */
		return E_SPIPE;
	if (!(d->drap & (wr ? O_WRITE : O_READ)))
		return E_BADF;
	u64 done = 0, rest, got;
	long r;
	if (d->type == 4)
		goto pipe;
	if (n <= FS_DATA_MAX)
		goto msgs;
	if (wr || n < FS_DIRECT_MIN || ((u64)buf & 3))
		goto tampon;
	/* grosse lecture : directement ici (le service mappe cette mémoire) ; sur
	 * un refus, le tampon partagé */
	for (;;) {
		rest = n - done;
		if (!rest)
			return (long)done;
		if (rest > FS_DIRECT_MAX)
			rest = FS_DIRECT_MAX;
		M_OP(m) = FS_READ_DIRECT;
		M_A(m) = d->num;
		M_B(m) = fc_pos_send(d, pos);
		M_C(m) = (long)rest;
		M_D(m) = (long)(buf + done);
		M_LEN(m) = 0;
		r = fc_svc_call(d->canal, m);
		if (r < 0)
			goto tampon;
		if (!r)
			return (long)done; /* fin du fichier */
		got = (u64)r > rest ? rest : (u64)r;
		fc_pos_advance(d, &pos, got, M_D(m));
		done += got;
		if (got < rest)
			return (long)done;
	}
tampon:; /* gros transfert : le tampon partagé */
	u32 *tl;
	u8 *tb = (u8 *)fc_buffer(d->canal, &tl);
	if (!tb)
		goto msgs;
	fc_take(tl);
	for (;;) {
		rest = n - done;
		if (!rest)
			break;
		if (rest > FS_BUFFER_SIZE)
			rest = FS_BUFFER_SIZE;
		if (wr)
			fc_copy(tb, buf + done, rest);
		M_OP(m) = wr ? FS_WRITE_BUF : FS_READ_BUF;
		M_A(m) = d->num;
		M_B(m) = fc_pos_send(d, pos);
		M_C(m) = (long)rest;
		M_LEN(m) = 0;
		r = fc_svc_call(d->canal, m);
		if (r < 0) {
			fc_release(tl);
			if (done)
				return (long)done;
			if (r != E_INVAL) /* E_INVAL : tampon inconnu du service (relancé) */
				return r;
			fc_buffer_forget(d->canal);
			goto msgs;
		}
		if (!r)
			break; /* fin du fichier */
		got = (u64)r > rest ? rest : (u64)r;
		if (!wr)
			fc_copy(buf + done, tb, got);
		else
			d->taille = M_B(m); /* nouvelle taille */
		fc_pos_advance(d, &pos, got, M_D(m));
		done += got;
		if (got < rest && !wr)
			break;
	}
	fc_release(tl);
	return (long)done;
msgs: /* par messages de 464 o */
	for (;;) {
		rest = n - done;
		if (!rest)
			return (long)done;
		if (rest > FS_DATA_MAX)
			rest = FS_DATA_MAX;
		if (wr)
			fc_copy(M_DATA(m), buf + done, rest);
		M_OP(m) = wr ? FS_WRITE : FS_READ;
		M_A(m) = d->num;
		M_B(m) = fc_pos_send(d, pos);
		M_C(m) = (long)rest;
		M_LEN(m) = wr ? (u32)rest : 0;
		r = fc_svc_call(d->canal, m);
		if (r < 0)
			return done ? (long)done : r;
		if (!r)
			return (long)done;
		got = (u64)r > rest ? rest : (u64)r;
		if (!wr)
			fc_copy(buf + done, M_DATA(m), got);
		else
			d->taille = M_B(m);
		fc_pos_advance(d, &pos, got, M_D(m));
		done += got;
		if (got < FS_DATA_MAX && !wr) /* (comparé à 464, pas au reste : comme l'asm) */
			return (long)done;
	}
pipe:
	if (!wr) { /* tube : lecture, un message */
		if (!n)
			return 0;
		for (;;) {
			M_OP(m) = V_PIPE_READ;
			M_A(m) = d->num;
			M_B(m) = (long)(n < FS_DATA_MAX ? n : FS_DATA_MAX);
			M_C(m) = (d->drap & O_NONBLOCK) != 0;
			M_LEN(m) = 0;
			r = fc_svc_call(d->canal, m);
			if (r == E_AGAIN) { /* vide (ou attente échue) */
				if (d->drap & O_NONBLOCK)
					return r;
				continue; /* bloquant : on redemande */
			}
			if (r <= 0)
				return r;
			got = (u64)r > n ? n : (u64)r;
			fc_copy(buf, M_DATA(m), got);
			return (long)got;
		}
	}
	for (;;) { /* écriture : tout */
		rest = n - done;
		if (!rest)
			return (long)done;
		if (rest > FS_DATA_MAX)
			rest = FS_DATA_MAX;
		fc_copy(M_DATA(m), buf + done, rest);
		M_OP(m) = V_PIPE_WRITE;
		M_A(m) = d->num;
		M_LEN(m) = (u32)rest;
		r = fc_svc_call(d->canal, m);
		if (r < 0)
			return done ? (long)done : r;
		if (r) {
			done += (u64)r;
			continue;
		}
		if (d->drap & O_NONBLOCK) /* plein */
			return done ? (long)done : E_AGAIN;
		korin_sys1(SYS_SLEEP, 2); /* laisser le lecteur vider */
	}
}

EXPORT long f_read(long fd, void *buf, u64 n)
{
	return fc_rw(fd, buf, n, 0, -1);
}

EXPORT long f_write(long fd, const void *buf, u64 n)
{
	return fc_rw(fd, (u8 *)buf, n, 1, -1);
}

EXPORT long f_pread(long fd, void *buf, u64 n, long pos)
{
	if (pos < 0)
		return E_INVAL;
	return fc_rw(fd, buf, n, 0, pos);
}

EXPORT long f_pwrite(long fd, const void *buf, u64 n, long pos)
{
	if (pos < 0)
		return E_INVAL;
	return fc_rw(fd, (u8 *)buf, n, 1, pos);
}

/* --- seek(fd, position, origine 0/1/2) -> nouvelle position (descripteur
 * partagé : au service, FS_SEEK ; FCD_POS en garde la copie) */
EXPORT long f_seek(long fd, long off, u64 whence)
{
	struct fcd *d = fc_desc_of(fd);
	if (!d)
		return E_BADF;
	if (d->type == 4 || whence > 2)
		return E_INVAL;
	if (d->drap & FC_PARTAGE) {
		msg_t m;
		M_OP(m) = FS_SEEK;
		M_A(m) = d->num;
		M_B(m) = off;
		M_C(m) = (long)whence;
		M_LEN(m) = 0;
		long r = fc_svc_call(d->canal, m);
		if (r >= 0)
			d->pos = r;
		return r;
	}
	long base = whence == 1 ? d->pos : whence == 2 ? d->taille : 0;
	long np = (long)((u64)base + (u64)off);
	if (np < 0)
		return E_INVAL;
	d->pos = np;
	return np;
}

/* --- fork() -> pid du fils (père), 0 (fils), < 0 erreur : les positions des
 * fichiers ouverts passent aux services (FS_SEEK), descripteurs partagés ;
 * verrous du kit tenus pendant l'appel ; dans le fils, f_fork_child. */
EXPORT long f_fork(void)
{
	msg_t m;
	fc_exec_check();
	fc_take(&fc_sverrou); /* (dans l'ordre de fc_stdio_init) */
	fc_take(&fc_verrou);
	for (int i = 0; i < FC_MAX; i++) {
		struct fcd *d = &fc_desc[i];
		if (d->type != 1 || (d->drap & FC_PARTAGE))
			continue;
		M_OP(m) = FS_SEEK;
		M_A(m) = d->num;
		M_B(m) = d->pos;
		M_C(m) = 0;
		M_LEN(m) = 0;
		if (fc_svc_call(d->canal, m) < 0)
			continue; /* (service sans FS_SEEK : à lui seul) */
		d->drap |= FC_PARTAGE;
	}
	long pid = korin_sys0(SYS_FORK);
	if (!pid) {
		f_fork_child(); /* (rend aussi les verrous) */
		return 0;
	}
	fc_release(&fc_verrou);
	fc_release(&fc_sverrou);
	return pid;
}

/* --- dans le fils d'un fork, juste après : verrous rendus, descripteurs en
 * ouverture oubliés, tampons partagés du père démappés et oubliés -> 0 */
EXPORT long f_fork_child(void)
{
	fc_verrou = 0;
	fc_sverrou = 0;
	for (int i = 0; i < FC_MAX; i++)
		if (fc_desc[i].type == 3)
			fc_desc[i].type = 0;
	for (int i = 0; i < FC_CANAUX; i++) {
		fc_tverrous[i] = 0;
		long t = fc_tampons[i];
		fc_tampons[i] = 0;
		if (t && t != -1)
			korin_sys1(SYS_BUF_FREE, fc_tnum[i]); /* (sa projection chez le fils) */
	}
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	return 0;
}

/* --- close(fd) */
EXPORT long f_close(long fd)
{
	msg_t m;
	for (int i = 0; i < 3; i++) /* plus une entrée standard */
		if (fc_stdio[i] == fd)
			fc_stdio[i] = 0;
	struct fcd *d = fc_desc_of(fd);
	if (!d)
		return E_BADF;
	M_OP(m) = d->type == 4 ? V_PIPE_CLOSE : FS_CLOSE;
	M_A(m) = d->num;
	M_LEN(m) = 0;
	fc_svc_call(d->canal, m);
	d->type = 0;
	return 0;
}

/* --- stat(chemin, {taille, attributs}) */
EXPORT long f_stat(const char *path, long *out)
{
	msg_t m;
	if (!out)
		return E_FAULT;
	long r = fc_msg_path(m, V_STAT, 0, path);
	if (r)
		return r;
	r = fc_vfs(m);
	if (r < 0)
		return r;
	out[0] = M_B(m);
	out[1] = M_C(m);
	return r;
}

/* --- readdir(fd, {nom[256], taille, attributs}) -> 1, 0 en fin */
EXPORT long f_readdir(long fd, void *ent)
{
	msg_t m;
	struct fcd *d = fc_desc_of(fd);
	if (!d)
		return E_BADF;
	if (d->type != 2)
		return E_NOTDIR;
	M_OP(m) = FS_READDIR;
	M_A(m) = d->num;
	M_B(m) = d->pos;
	M_LEN(m) = 0;
	long r = fc_svc_call(d->canal, m);
	if (r != 1)
		return r;
	fc_copy(ent, M_DATA(m), DIRENT_BYTES);
	d->pos++;
	return 1;
}

EXPORT long f_mkdir(const char *path)
{
	return fc_path_only(V_MKDIR, path);
}

EXPORT long f_unlink(const char *path)
{
	return fc_path_only(V_UNLINK, path);
}

/* vfs seul (le noyau n'a plus de dossier courant) */
EXPORT long f_chdir(const char *path)
{
	return fc_path_only(V_CHDIR, path);
}

/* --- getcwd(tampon, taille >= 256) */
EXPORT long f_getcwd(char *buf, u64 size)
{
	msg_t m;
	if (size < 256)
		return E_INVAL;
	if (!buf)
		return E_FAULT;
	M_OP(m) = V_GETCWD;
	M_LEN(m) = 0;
	long r = fc_vfs(m);
	if (r < 0)
		return r;
	u64 n = M_LEN(m);
	if (n > 256)
		n = 256;
	if (!n) {
		buf[0] = 0;
		return 0;
	}
	fc_copy(buf, M_DATA(m), n);
	buf[255] = 0;
	return 0;
}

/* -------------------------------------------------------------------------
 *  Entrées / sorties standard, tubes de vfs, lancement des programmes
 * ------------------------------------------------------------------------- */

static long fc_pipe_desc(long h, long fl);

/* Entrées / sorties standard, au premier besoin : proc_flags dit lesquelles
 * sont des tubes ; leurs poignées viennent de vfs (V_STDIO) et deviennent des
 * descripteurs de type 4 (fc_stdio : 0 = console, -1 = /dev/null, VT_TAG : un
 * bout de vterm tel quel, sinon un descripteur). */
static NOINL void fc_stdio_init(void)
{
	msg_t m;
	long h[4];
	if (__atomic_load_n(&fc_stdio_ok, __ATOMIC_ACQUIRE))
		return;
	fc_take(&fc_sverrou);
	if (fc_stdio_ok)
		goto rend;
	long r = korin_sys0(SYS_PROC_FLAGS);
	if (r < 0 || !(r & 7)) /* (tout à la console) */
		goto fait;
	M_OP(m) = V_STDIO;
	M_LEN(m) = 0;
	if (fc_vfs(m) < 0)
		goto fait;
	h[0] = M_A(m);
	h[1] = M_B(m);
	h[2] = M_C(m);
	h[3] = M_D(m);
	for (int i = 0; i < 3; i++) {
		long x = h[i];
		if (!x)
			continue;
		if (x & VT_TAG) /* un bout de vterm : la poignée telle quelle */
			goto pose;
		if ((h[3] >> i) & 1) { /* un fichier (redirection) : ouvert ici */
			M_OP(m) = V_END_FILE;
			M_A(m) = x;
			M_LEN(m) = 0;
			if (fc_vfs(m) != 1)
				continue;
			if (str_ends(M_DATA(m), "/dev/null")) {
				x = -1; /* (ni lecture ni écriture) */
			} else {
				x = f_open(M_DATA(m), M_B(m) == 1 ? O_READ : (O_WRITE | O_APPEND));
				if (x == -1)
					continue;
			}
		} else {
			x = fc_pipe_desc(x, i == 0 ? O_READ : O_WRITE); /* entrée : lecture */
		}
		if (x == -1)
			goto pose;
		if (x < 0)
			continue;
pose:
		fc_stdio[i] = x;
	}
fait:
	__atomic_store_n(&fc_stdio_ok, 1, __ATOMIC_RELEASE);
rend:
	fc_release(&fc_sverrou);
}

/* --- f_redir(chemin, O_READ ou O_WRITE) -> descripteur à donner à
 * f_spawn_stdio : le fils ouvrira ce fichier lui-même (ajout à l'écriture) */
EXPORT long f_redir(const char *path, long mode)
{
	msg_t m;
	long r = fc_msg_path(m, V_FILE_END, (mode & O_WRITE) ? 2 : 1, path);
	if (r)
		return r;
	r = fc_vfs(m);
	if (r < 0)
		return r;
	return fc_pipe_desc(M_A(m), mode);
}

/* poignée d'un bout de tube (vfs), drapeaux -> descripteur (type 4) ou
 * erreur (le bout reste ouvert) */
static NOINL long fc_pipe_desc(long h, long fl)
{
	long ch = korin_sys2(SYS_SVC_OPEN, "vfs", 0);
	if (ch < 0)
		return ch;
	long rang;
	struct fcd *d = fc_reserve(&rang);
	if (!d)
		return E_MFILE;
	d->canal = ch;
	d->num = h;
	d->pos = 0;
	d->taille = 0;
	d->drap = fl;
	__atomic_store_n(&d->type, 4, __ATOMIC_RELEASE); /* (prêt) */
	return rang + FC_BASE;
}

/* poignée d'un bout de tube : fermée chez vfs (sans descripteur) */
static NOINL void fc_end_close(long h)
{
	msg_t m;
	M_OP(m) = V_PIPE_CLOSE;
	M_A(m) = h;
	M_LEN(m) = 0;
	fc_vfs(m);
}

/* canal d'un service FS, numéro : fichier fermé (sans descripteur) */
static NOINL void fc_fs_close(long canal, long num)
{
	msg_t m;
	M_OP(m) = FS_CLOSE;
	M_A(m) = num;
	M_LEN(m) = 0;
	fc_svc_call(canal, m);
}

/* tampon, octets : à la console du noyau (print, morceaux de 4096) -> 0 ou erreur */
static long fc_console(const char *buf, u64 n)
{
	while (n) {
		u64 k = n < 4096 ? n : 4096;
		long r = korin_sys2(SYS_PRINT, buf, k);
		buf += k;
		n -= k;
		if (r < 0)
			return r;
	}
	return 0;
}

/* message (op VT_*) : au service vterm -> résultat */
static long fc_vt_call(long *m)
{
	long ch = korin_sys2(SYS_SVC_OPEN, "vterm", 0);
	if (ch < 0)
		return E_BADF;
	return fc_svc_call(ch, m);
}

/* poignée de vterm, tampon, octets, appel (SYS_READ, SYS_WRITE, SYS_CLOSE),
 * descripteur standard (0..2) -> résultat. Lecture : attend des données
 * (E_AGAIN : on redemande) ; écriture : jusqu'au bout (anneau plein : 2 ms) ;
 * fermer : le descripteur redevient la console, vterm prévenu si aucun autre
 * descripteur standard ne tient ce bout. */
static NOINL long fc_vt_std(long h, u8 *buf, u64 n, long nr, long i)
{
	msg_t m;
	long r;
	if (nr == SYS_READ) {
		if (!n)
			return 0;
		do {
			M_OP(m) = VT_READ;
			M_A(m) = h;
			M_B(m) = (long)(n < FS_DATA_MAX ? n : FS_DATA_MAX);
			M_C(m) = 0;
			M_LEN(m) = 0;
			r = fc_vt_call(m);
		} while (r == E_AGAIN);
		if (r <= 0)
			return r;
		fc_copy(buf, M_DATA(m), (u64)r); /* (sans borne : vterm rend <= b, comme l'asm) */
		return r;
	}
	if (nr == SYS_WRITE) {
		u64 done = 0;
		while (done < n) {
			u64 k = n - done;
			if (k >= FS_DATA_MAX)
				k = FS_DATA_MAX;
			fc_copy(M_DATA(m), buf + done, k);
			M_OP(m) = VT_WRITE;
			M_A(m) = h;
			M_LEN(m) = (u32)k;
			r = fc_vt_call(m);
			if (r < 0)
				return done ? (long)done : r;
			done += (u64)r;
			if (!r)
				korin_sys1(SYS_SLEEP, 2); /* plein : le maître lira */
		}
		return (long)done;
	}
	if (nr == SYS_CLOSE) {
		fc_stdio[i] = 0;
		for (int j = 0; j < 3; j++)
			if (fc_stdio[j] == h)
				return 0;
		M_OP(m) = VT_CLOSE;
		M_A(m) = h;
		M_LEN(m) = 0;
		fc_vt_call(m);
		return 0;
	}
	return E_INVAL;
}

/* (file_sys) fd 0, 1, 2 : read / write / close sur l'entrée, la sortie ou
 * l'erreur standard */
static NOINL long fc_std(long i, u8 *buf, u64 n, long nr)
{
	fc_stdio_init();
	long x = fc_stdio[i];
	if (!x) { /* la console */
		if (nr == SYS_WRITE) {
			if (!i)
				return E_BADF; /* (l'entrée ne s'écrit pas) */
			long r = fc_console((const char *)buf, n);
			return r < 0 ? r : (long)n;
		}
		if (nr == SYS_READ || nr == SYS_CLOSE)
			return 0; /* lire la console : fin ; fermer : 0 */
		return E_INVAL;
	}
	if (x == -1) /* /dev/null : tout est accepté, rien à lire */
		return nr == SYS_WRITE ? (long)n : 0;
	if (x & VT_TAG)
		return fc_vt_std(x, buf, n, nr, i);
	if (nr == SYS_READ)
		return f_read(x, buf, n);
	if (nr == SYS_WRITE)
		return f_write(x, buf, n);
	if (nr == SYS_CLOSE)
		return f_close(x);
	return E_INVAL;
}

/* --- print(tampon, octets) : sur la sortie standard -> 0 (console : le
 * résultat du noyau, 4096 octets au plus) */
EXPORT long f_print(const char *buf, u64 n)
{
	fc_stdio_init();
	long x = fc_stdio[1];
	if (!x)
		return korin_sys2(SYS_PRINT, buf, n);
	if (x == -1)
		return 0;
	if (x & VT_TAG)
		fc_vt_std(x, (u8 *)buf, n, SYS_WRITE, 1);
	else
		f_write(x, buf, n); /* (lecteur parti : sortie perdue) */
	return 0;
}

/* --- f_pipe(&fds[2]) -> 0 : un tube neuf, fds[0] lecture, fds[1] écriture */
EXPORT long f_pipe(long *fds)
{
	msg_t m;
	M_OP(m) = V_PIPE;
	M_LEN(m) = 0;
	long r = fc_vfs(m);
	if (r < 0)
		return r;
	long rh = M_A(m), wh = M_B(m);
	long fr = fc_pipe_desc(rh, O_READ);
	if (fr < 0) {
		fc_end_close(rh);
		fc_end_close(wh);
		return fr;
	}
	long fw = fc_pipe_desc(wh, O_WRITE);
	if (fw < 0) {
		f_close(fr); /* (ferme aussi le bout de lecture) */
		fc_end_close(wh);
		return fw;
	}
	fds[0] = fr;
	fds[1] = fw;
	return 0;
}

/* chemin résolu d'une réponse de V_EXEC : après le nom */
static char *fc_after_name(long *resp)
{
	char *p = M_DATA(resp);
	while (*p++)
		;
	return p;
}

static long fc_script_line(long *resp, const char *line, char *dst);
static NOINL long fc_spawn_dotnet(long *resp, const char *args, const long *stdio);

/* chemin résolu, taille du fichier, descripteur ouvert en lecture (position
 * 0), drapeaux IMG_* -> 0 : l'image construite (IMAGE_CREATE, lecture,
 * IMAGE_SEAL) ; E_NOSYS : pas d'images ; sinon l'erreur */
static NOINL long fc_image_load(const char *path, u64 size, long fd, long flags)
{
	long w = korin_sys3(SYS_IMAGE_CREATE, path, size, flags);
	if (w < 0)
		return w;
	long r = f_read(fd, (void *)w, size);
	if ((u64)r == size)
		return korin_sys2(SYS_IMAGE_SEAL, w, 0);
	korin_sys2(SYS_IMAGE_SEAL, w, IMS_ABORT); /* lecture courte ou échouée : rendue */
	return r < 0 ? r : E_INVAL;
}

/* un descripteur temporaire de lecture sur un fichier ouvert par vfs */
static void fc_temp_fill(struct fcd *d, long canal, long num, u64 size)
{
	d->canal = canal;
	d->num = num;
	d->taille = (long)size;
	d->pos = 0;
	d->drap = O_READ;
	__atomic_store_n(&d->type, 1, __ATOMIC_RELEASE); /* (prêt) */
}

/* --- f_spawn_file(réponse de V_EXEC, arguments, stdio[3] ou 0) -> pid.
 * L'image de ce chemin et de cette taille (spawn_preloaded), sinon le fichier
 * lu dans les pages d'une image neuve, sinon lu dans un tampon partagé
 * (spawn_image). MZ : une DLL .NET (fc_spawn_dotnet). Un script : l'argument
 * de #! et le script passent devant les arguments. */
EXPORT long f_spawn_file(long *resp, const char *args, const long *stdio)
{
	long P[8];
	char line[LIGNE_MAX];
	if (M_C(resp) & ATTR_SCRIPT) {
		if (!fc_script_line(resp, args, line)) {
			fc_fs_close(M_A(resp), M_B(resp));
			return E_2BIG;
		}
		args = line;
	}
	long rang;
	struct fcd *d = fc_reserve(&rang); /* descripteur temporaire */
	if (!d) {
		fc_fs_close(M_A(resp), M_B(resp));
		return E_MFILE;
	}
	long fd = rang + FC_BASE;
	u64 size = (u64)M_D(resp);
	fc_temp_fill(d, M_A(resp), M_B(resp), size);
	long res = E_INVAL;
	if (!size)
		goto ferme;
	P[0] = (long)M_DATA(resp); /* params : nom, arguments, options */
	P[1] = (long)args;
	P[2] = (M_C(resp) & ATTR_SYS) != 0; /* droits demandés */
	if (fc_spawn_uid >= 0) /* SYS_SPAWN_AS : l'uid du fils (SPAWN_UID, bits 32-63), pour ce seul lancement */
		P[2] |= 2 | ((long)fc_spawn_uid << 32);
	fc_spawn_uid = -1;
	P[3] = P[4] = P[5] = 0;
	if (stdio) {
		P[3] = stdio[0];
		P[4] = stdio[1];
		P[5] = stdio[2];
	}
	char *path = fc_after_name(resp);
	res = korin_sys3(SYS_SPAWN_PRELOADED, path, size, P);
	if (res != E_NOENT && res != E_NOSYS)
		goto ferme;
	u16 *mz = (u16 *)&P[7]; /* ses deux premiers octets */
	*mz = 0;
	f_read(fd, mz, 2);
	d->pos = 0;
	if (*mz == ('M' | ('Z' << 8))) {
		res = fc_spawn_dotnet(resp, args, stdio);
		goto ferme;
	}
	if (res != E_NOSYS) {
		res = fc_image_load(path, size, fd, 0);
		if (res != E_NOSYS && res != E_NOSPC) {
			if (!res)
				res = korin_sys3(SYS_SPAWN_PRELOADED, path, size, P);
			goto ferme;
		}
	}
	res = E_INVAL; /* sans images : par un tampon partagé, rendu après */
	if (size > FC_IMAGE_MAX)
		goto ferme;
	long b = korin_sys1(SYS_BUF_CREATE, size);
	res = b;
	if (b < 0)
		goto ferme;
	P[6] = (long)(BUF_VA_BASE + ((u64)b << 26)); /* (BUF_SLOT = 64 Mo) */
	res = f_read(fd, (void *)P[6], size);
	f_close(fd);
	if ((u64)res == size)
		res = korin_sys3(SYS_SPAWN_IMAGE, P[6], size, P);
	else if (res >= 0)
		res = E_INVAL; /* (fichier plus court) */
	korin_sys1(SYS_BUF_FREE, b);
	return res;
ferme:
	f_close(fd);
	return res;
}

/* réponse de V_EXEC d'une DLL .NET, arguments, stdio -> pid : /devtools/dotnet/dotnet
 * sous le nom du programme ; sa ligne : 1, le chemin de la DLL, 1, puis les
 * arguments. <dll>.skia à côté : /devtools/dotnet/dotnet-skia. */
static NOINL long fc_spawn_dotnet(long *resp, const char *args, const long *stdio)
{
	msg_t m;
	char line[LIGNE_MAX];
	const char *p = fc_after_name(resp);
	char *o = line, *lim = line + LIGNE_MAX - 2;
	char c;
	*o++ = 1;
	while ((c = *p++)) {
		if (o >= lim)
			return E_INVAL;
		*o++ = c;
	}
	*o++ = 1;
	if (args) {
		*o++ = ' ';
		while ((c = *args++)) {
			if (o >= lim)
				return E_INVAL;
			*o++ = c;
		}
	}
	*o = 0;
	const char *host = "/devtools/dotnet/dotnet"; /* l'hôte : <dll>.skia existe-t-il ? */
	if (!fc_msg_path(m, V_STAT, 0, fc_after_name(resp))) {
		u32 len = M_LEN(m);
		if (len < FC_CHEMIN - 6) {
			str_cat(M_DATA(m) + len - 1, ".skia");
			M_LEN(m) = len + 5;
			if (fc_vfs(m) >= 0)
				host = "/devtools/dotnet/dotnet-skia";
		}
	}
	long r = fc_msg_path(m, V_EXEC, 0, host);
	if (r)
		return r;
	r = fc_vfs(m);
	if (r < 0)
		return r;
	o = str_cat(M_DATA(m), M_DATA(resp)) + 1; /* le nom du programme, puis son propre chemin */
	str_cat(o, host);
	return f_spawn_file(m, line, stdio);
}

/* réponse de V_EXEC, arguments -> pid : init2 lance le programme
 * (INIT_OP_LAUNCH ; données : chemin, 0, arguments, 0) */
static long fc_launch_init2(long *m, const char *args)
{
	char *src = fc_after_name(m), *o = M_DATA(m);
	char c;
	do { /* (vers l'avant : recouvrement sans danger) */
		c = *src++;
		*o++ = c;
	} while (c);
	const char *a = args ? args : "";
	char *end = (char *)m + SVC_MSG;
	do {
		c = *a++;
		if (o >= end)
			return E_INVAL;
		*o++ = c;
	} while (c);
	M_LEN(m) = (u32)(o - M_DATA(m));
	M_OP(m) = INIT_OP_LAUNCH;
	long ch = korin_sys2(SYS_SVC_OPEN, "init2", 0);
	if (ch < 0)
		return E_PERM; /* (pas d'init2) */
	return fc_svc_call(ch, m);
}

/* chemin, arguments, stdio -> pid, *det = 1 s'il est détaché : V_EXEC, puis
 * f_spawn_file ; un programme de /sys demandé sans droits : par init2 */
static NOINL long fc_spawn(const char *path, const char *args, const long *stdio, long *det)
{
	msg_t m;
	*det = 0;
	long r = fc_msg_path(m, V_EXEC, 0, path);
	if (r)
		return r;
	r = fc_vfs(m);
	if (r < 0)
		return r;
	if (M_C(m) & ATTR_SYS) {
		long f = korin_sys0(SYS_PROC_FLAGS);
		if (f >= 0 && !(f & PROC_RIGHTS)) {
			fc_fs_close(M_A(m), M_B(m));
			*det = 1;
			return fc_launch_init2(m, args);
		}
	}
	return f_spawn_file(m, args, stdio);
}

/* --- preload(chemin) -> 0 : le fichier lu dans une image épinglée ;
 * chemin = 0 : 0 si le noyau a les images */
static NOINL long f_preload(const char *path)
{
	msg_t m;
	if (!path)
		return korin_sys3(SYS_IMAGE_PRELOAD, 0, 0, 0);
	long r = fc_msg_path(m, V_EXEC, 0, path);
	if (r)
		return r;
	r = fc_vfs(m);
	if (r < 0)
		return r;
	u64 size = (u64)M_D(m);
	long res = E_INVAL;
	if (size < 64)
		goto ferme;
	long rang;
	struct fcd *d = fc_reserve(&rang); /* descripteur temporaire */
	res = E_MFILE;
	if (!d)
		goto ferme;
	fc_temp_fill(d, M_A(m), M_B(m), size);
	res = fc_image_load(fc_after_name(m), size, rang + FC_BASE, IMG_PIN);
	f_close(rang + FC_BASE); /* (ferme aussi le fichier) */
	return res;
ferme:
	fc_fs_close(M_A(m), M_B(m));
	return res;
}

/* --- unload(chemin) -> nombre : ses images oubliées (IMAGE_FORGET) */
static NOINL long f_unload(const char *path)
{
	msg_t m;
	long r = fc_msg_path(m, V_EXEC, 0, path);
	if (r)
		return r;
	r = fc_vfs(m);
	if (r < 0)
		return r;
	fc_fs_close(M_A(m), M_B(m));
	return korin_sys1(SYS_IMAGE_FORGET, fc_after_name(m));
}

/* --- spawn(chemin, arguments) -> pid : entrées / sorties du père */
EXPORT long f_spawn(const char *path, const char *args)
{
	long det;
	return fc_spawn(path, args, 0, &det);
}

/* --- spawn_as(chemin, arguments, uid) -> pid : comme spawn, le fils avec cet uid (docs/etude/
 * MULTI-UTILISATEUR.md §2.2 ; le noyau refuse si l'appelant n'est pas d'uid 0). Un programme de /sys
 * demandé sans droits passe par init2, qui prend lui-même l'uid du demandeur. Chemin 0 : l'uid est
 * gardé pour le prochain lancement, quel qu'il soit (spawn_stdio de telnetd et de la console) ; uid -1
 * l'oublie. */
static NOINL long f_spawn_as(const char *path, const char *args, long uid)
{
	if (!path) {
		fc_spawn_uid = uid >= 0 && uid <= 0x7FFFFFFF ? (int)uid : -1;
		return 0;
	}
	if (uid < 0 || uid > 0x7FFFFFFF)
		return E_INVAL;
	fc_spawn_uid = (int)uid;
	long r = f_spawn(path, args);
	fc_spawn_uid = -1;
	return r;
}

/* descripteurs (fds : tube, 0-2 standard du père, < 0 console, VT_TAG) ->
 * poignées de vfs pour le fils, dans h ; E_BADF */
static long fc_stdio_handles(const long *fds, long *h)
{
	for (int i = 0; i < 3; i++) {
		long x = fds[i], v = -1;
		if (x < 0)
			goto mis; /* console */
		if (x & VT_TAG) { /* un bout de vterm : tel quel */
			v = x;
			goto mis;
		}
		if ((u64)x < 3) { /* une entrée standard du père */
			x = fc_stdio[x];
			if (!x)
				goto mis;
			v = 0; /* (/dev/null : 0, celle du père) */
			if (x == -1)
				goto mis;
			if (x & VT_TAG) {
				v = x;
				goto mis;
			}
		}
		struct fcd *d = fc_desc_of(x);
		if (!d)
			return E_BADF;
		if (d->type == 4)
			v = d->num;
		else if (d->type == 1)
			v = 0; /* (redirigée vers un fichier : celle du père, que vfs connaît) */
		else
			return E_BADF;
mis:
		h[i] = v;
	}
	return 0;
}

/* --- f_spawn_stdio(chemin, arguments, &fds[3]) -> pid ; fds = 0 : spawn */
EXPORT long f_spawn_stdio(const char *path, const char *args, const long *fds)
{
	long h[4], det;
	if (!fds)
		return f_spawn(path, args);
	fc_stdio_init();
	if (fc_stdio_handles(fds, h))
		return E_BADF;
	return fc_spawn(path, args, h, &det);
}

/* --- spawn_pipe(chemin, arguments, &fd) -> pid : sortie et erreur du fils
 * dans un tube neuf ; *fd = son bout de lecture (O_NONBLOCK). Détaché (init2) :
 * *fd = -1, pas de tube. */
EXPORT long f_spawn_pipe(const char *path, const char *args, long *pfd)
{
	msg_t m;
	if (!pfd)
		return E_FAULT;
	M_OP(m) = V_PIPE;
	M_LEN(m) = 0;
	long r = fc_vfs(m);
	if (r < 0)
		return r;
	long rh = M_A(m), wh = M_B(m);
	long fr = fc_pipe_desc(rh, O_READ | O_NONBLOCK);
	if (fr < 0) {
		fc_end_close(rh);
		fc_end_close(wh);
		return fr;
	}
	long h[3] = {0, wh, wh}, det; /* entrée : la sienne ; sortie, erreur : le tube */
	long pid = fc_spawn(path, args, h, &det);
	fc_end_close(wh); /* le père n'écrit pas : fin du tube quand le fils s'arrête */
	if (pid < 0) {
		f_close(fr);
		return pid;
	}
	if (det) {
		*pfd = -1; /* détaché : pas de tube */
		f_close(fr);
		return pid;
	}
	*pfd = fr;
	return pid;
}

/* fin d'une chaîne, chaîne à ajouter -> nouvelle fin (0 écrit) */
static char *fc_cat(char *d, const char *s)
{
	return str_cat(d, s);
}

/* --- f_kit_load(nom) -> 0 ou erreur : /lib/<nom>.so lu par vfs (ATTR_LIB)
 * et inscrit par le noyau (droits de /sys) ; sans droits : init2 */
EXPORT long f_kit_load(const char *name)
{
	msg_t m;
	char path[64];
	long i = 0;
	for (;;) { /* nom : 1-23 caractères a-z 0-9 _ */
		u32 c = (u8)name[i];
		if (!c)
			break;
		if (i >= FC_KIT_NOM)
			return E_INVAL;
		if (c != '_' && c - 'a' > 25 && c - '0' > 9)
			return E_INVAL;
		i++;
	}
	if (!i)
		return E_INVAL;
	long f = korin_sys0(SYS_PROC_FLAGS);
	if (f < 0 || !(f & PROC_RIGHTS)) { /* sans droits : par init2 */
		M_OP(m) = INIT_OP_KIT_LOAD; /* données : le nom */
		char *e = fc_cat(M_DATA(m), name);
		M_LEN(m) = (u32)(e - M_DATA(m) + 1);
		long ch = korin_sys2(SYS_SVC_OPEN, "init2", 0);
		if (ch < 0)
			return E_PERM; /* (pas d'init2) */
		return fc_svc_call(ch, m);
	}
	fc_cat(fc_cat(fc_cat(path, "/lib/"), name), ".so");
	long r = fc_msg_path(m, V_OPEN, O_READ, path);
	if (r)
		return r;
	r = fc_vfs(m);
	if (r < 0)
		return r;
	long canal = M_A(m), num = M_B(m), attr = M_C(m);
	u64 size = (u64)M_D(m);
	long res = E_PERM; /* pas un kit du système */
	if ((attr & ATTR_DIR) || !(attr & ATTR_LIB))
		goto ferme;
	res = E_INVAL;
	if (size < 64 || size > FC_IMAGE_MAX)
		goto ferme;
	long rang;
	struct fcd *d = fc_reserve(&rang); /* descripteur temporaire */
	res = E_MFILE;
	if (!d)
		goto ferme;
	long fd = rang + FC_BASE;
	fc_temp_fill(d, canal, num, size);
	long b = korin_sys1(SYS_BUF_CREATE, size); /* l'image : un tampon partagé, rendu après */
	if (b < 0) {
		f_close(fd);
		return b;
	}
	u64 addr = BUF_VA_BASE + ((u64)b << 26);
	res = f_read(fd, (void *)addr, size);
	f_close(fd);
	if ((u64)res == size) {
		r = korin_sys3(SYS_KIT_REGISTER, name, addr, size);
		res = r < 0 ? r : 0;
	} else if (res >= 0) {
		res = E_INVAL; /* (fichier plus court) */
	}
	korin_sys1(SYS_BUF_FREE, b); /* tampon rendu (démappé) */
	return res;
ferme:
	fc_fs_close(canal, num);
	return res;
}

/* =========================================================================
 *  file_sys : x8 = numéro (7e argument), x0-x5 -> x0
 * ========================================================================= */

EXPORT long file_sys(long a0, long a1, long a2, long a3, long a4, long a5, long nr)
{
	(void)a3;
	(void)a4;
	(void)a5;
	switch (nr) {
	case SYS_PRINT:
		return f_print((const char *)a0, (u64)a1);
	case SYS_FORK:
		return f_fork();
	case SYS_SPAWN:
		return f_spawn((const char *)a0, (const char *)a1);
	case SYS_SPAWN_AS:
		return f_spawn_as((const char *)a0, (const char *)a1, a2);
	case SYS_SPAWN_PIPE:
		return f_spawn_pipe((const char *)a0, (const char *)a1, (long *)a2);
	case SYS_PRELOAD:
		return f_preload((const char *)a0);
	case SYS_UNLOAD:
		return f_unload((const char *)a0);
	case SYS_OPEN:
		return f_open((const char *)a0, a1);
	case SYS_STAT:
		return f_stat((const char *)a0, (long *)a1);
	case SYS_MKDIR:
		return f_mkdir((const char *)a0);
	case SYS_UNLINK:
		return f_unlink((const char *)a0);
	case SYS_CHDIR:
		return f_chdir((const char *)a0);
	case SYS_GETCWD:
		return f_getcwd((char *)a0, (u64)a1);
	}
	if ((u64)a0 < 3) /* entrée, sortie, erreur standard */
		return fc_std(a0, (u8 *)a1, (u64)a2, nr);
	if ((u64)a0 - FC_BASE >= FC_MAX) /* (aucun autre descripteur) */
		return E_BADF;
	switch (nr) {
	case SYS_READ:
		return f_read(a0, (void *)a1, (u64)a2);
	case SYS_WRITE:
		return f_write(a0, (const void *)a1, (u64)a2);
	case SYS_SEEK:
		return f_seek(a0, a1, (u64)a2);
	case SYS_CLOSE:
		return f_close(a0);
	case SYS_READDIR:
		return f_readdir(a0, (void *)a1);
	}
	return E_BADF; /* (ceux du noyau : partis à l'étape 5) */
}

/* =========================================================================
 *  spawnenv.inc : argv et environnement d'un fils (protocole de libposix)
 *  Un objet partagé donné par HANDLE_INHERIT, marqué 0x80000000 | pages ;
 *  +0 magique, +4 argc, +8 envc, +12 taille, +16 les chaînes à la suite.
 * ========================================================================= */

/* tableau de chaînes terminé par 0 -> nombre, *bytes = octets (avec les 0) */
static u64 env_count(char *const *v, u64 *bytes)
{
	u64 n = 0, b = 0;
	if (v)
		for (; v[n]; n++)
			b += strlen(v[n]) + 1;
	*bytes = b;
	return n;
}

/* tableau, n, destination -> fin */
static char *env_copy(char *const *v, u64 n, char *o)
{
	for (u64 i = 0; i < n; i++) {
		const char *s = v[i];
		char c;
		do {
			c = *s++;
			*o++ = c;
		} while (c);
	}
	return o;
}

/* argv, destination (LIGNE_MAX) -> 1, 0 si elle ne tient pas : argv[1...] en
 * une ligne, un argument vide, avec espaces ou guillemets entre guillemets */
static long env_line(char *const *argv, char *dst)
{
	if (!argv || !argv[0]) {
		dst[0] = 0;
		return 1;
	}
	argv++; /* (pas argv[0]) */
	char *o = dst, *lim = dst + LIGNE_MAX - 8;
	const char *a;
	while ((a = *argv++)) {
		if (o != dst)
			*o++ = ' ';
		int q = !a[0];
		for (const char *p = a; *p; p++)
			if (*p == ' ' || *p == 9 || *p == '"' || *p == '\\')
				q = 1;
		if (q)
			*o++ = '"';
		char c;
		while ((c = *a++)) {
			if (o >= lim)
				return 0;
			if (q && (c == '"' || c == '\\'))
				*o++ = '\\';
			*o++ = c;
		}
		if (q)
			*o++ = '"';
	}
	*o = 0;
	return 1;
}

/* réponse de V_EXEC d'un script (données : nom, interprète, argument de #!,
 * script), argv de l'appelant, destination (max entrées) -> destination :
 * interprète, [argument,] script, argv[1..], 0 ; 0 s'il ne tient pas */
static char **fc_script_argv(long *resp, char *const *argv, char **dst, u64 max)
{
	char *p = fc_after_name(resp);
	char *interp = p;
	while (*p++)
		;
	char *arg = p;
	while (*p++)
		;
	char *script = p;
	max--; /* (place du 0 final) */
	u64 k = 0;
	dst[k++] = interp;
	if (*arg)
		dst[k++] = arg;
	dst[k++] = script;
	if (argv && argv[0])
		for (argv++; *argv; argv++) {
			if (k >= max)
				return 0;
			dst[k++] = *argv;
		}
	dst[k] = 0;
	return dst;
}

/* réponse de V_EXEC d'un script, ligne d'arguments (0 : aucune), destination
 * (LIGNE_MAX) -> 1 : « [argument ]script[ ligne] », 0 si elle ne tient pas */
static long fc_script_line(long *resp, const char *line, char *dst)
{
	const char *p = M_DATA(resp);
	for (int k = 0; k < 2; k++) /* sauter le nom et l'interprète */
		while (*p++)
			;
	char *o = dst, *lim = dst + LIGNE_MAX - 1;
	char c;
	if (*p) { /* l'argument de #!, puis une espace */
		while ((c = *p++)) {
			if (o >= lim)
				return 0;
			*o++ = c;
		}
		*o++ = ' ';
	} else {
		p++;
	}
	while ((c = *p++)) { /* le script */
		if (o >= lim)
			return 0;
		*o++ = c;
	}
	if (line && *line) { /* puis la ligne */
		if (o >= lim)
			return 0;
		*o++ = ' ';
		while ((c = *line++)) {
			if (o >= lim)
				return 0;
			*o++ = c;
		}
	}
	*o = 0;
	return 1;
}

/* l'objet de argv et de l'environnement -> poignée (< 0 : erreur), rempli ;
 * *tag = sa marque */
static long fc_blob(char *const *argv, char *const *envp, u32 *tag, int *map_err, long *err)
{
	u64 ab, eb;
	u64 argc = env_count(argv, &ab);
	u64 envc = env_count(envp, &eb);
	u64 bytes = (ab + eb + BLOB_CHAINES + 0xFFF) & ~(u64)0xFFF;
	*map_err = 0;
	long h = korin_sys2(SYS_SHM_CREATE, bytes, 0);
	if (h < 0) {
		*err = h;
		return h;
	}
	long mp = korin_sys6(SYS_SHM_MAP, h, 0, (long)bytes, PROT_READ | PROT_WRITE, 0, 0);
	if ((u64)mp >= (u64)-4096) {
		*map_err = 1;
		*err = mp;
		korin_sys1(SYS_HANDLE_CLOSE, h);
		return -1;
	}
	u32 *b = (u32 *)mp;
	b[0] = BLOB_MAGIC;
	b[1] = (u32)argc;
	b[2] = (u32)envc;
	b[3] = (u32)bytes;
	char *e = env_copy(argv, argc, (char *)mp + BLOB_CHAINES);
	env_copy(envp, envc, e);
	korin_sys2(SYS_VM_UNMAP, mp, bytes);
	*tag = (u32)(bytes >> 12) | BLOB_TAG;
	return h;
}

/* --- f_spawn_env(chemin, argv, envp, &fds[3]) -> pid : comme f_spawn_stdio,
 * avec argv (argv[0] compris) et l'environnement pour un programme C ; un
 * programme en assembleur lit la ligne d'arguments (vide si elle dépasse
 * 1 024 octets). Un script : son interprète, argv = interprète, [argument,]
 * script, argv[1..]. */
EXPORT long f_spawn_env(const char *path, char *const *argv, char *const *envp, const long *fds)
{
	char line[LIGNE_MAX];
	msg_t m;
	char *sargv[SA_MAX];
	if (!fc_msg_path(m, V_EXEC, 0, path) && fc_vfs(m) >= 0) { /* un script ? (fichier refermé) */
		fc_fs_close(M_A(m), M_B(m));
		if (M_C(m) & ATTR_SCRIPT) {
			char **a = fc_script_argv(m, argv, sargv, SA_MAX);
			if (!a)
				return E_2BIG;
			argv = a;
			path = fc_after_name(m); /* l'interprète */
		}
	}
	u32 tag[2];
	int me;
	long err;
	long h = fc_blob(argv, envp, &tag[1], &me, &err);
	if (h >= 0) {
		tag[0] = (u32)h; /* { poignée, marque } */
		korin_sys2(SYS_HANDLE_INHERIT, tag, 1);
	}
	const char *a = 0; /* (trop longue : vide) */
	if (env_line(argv, line) && line[0])
		a = line;
	long pid = f_spawn_stdio(path, a, fds);
	if (h >= 0) {
		if (pid < 0) /* pas de fils : l'objet est rendu */
			korin_sys2(SYS_HANDLE_INHERIT, 0, 0);
		korin_sys1(SYS_HANDLE_CLOSE, h);
	}
	return pid;
}

/* --- f_start_blob() -> l'objet du père, projeté (+4 argc, +8 envc, +16
 * chaînes), 0 sans objet */
EXPORT void *f_start_blob(void)
{
	u32 list[256 * 4];
	long n = korin_sys3(SYS_HANDLE_LIST, list, 256, 1);
	if (n <= 0)
		return 0;
	for (long i = 0; i < n && i < 256; i++) {
		u32 t = list[i * 4 + 1];
		if (!(t & 0x80000000U))
			continue;
		long h = list[i * 4];
		long mp = korin_sys6(SYS_SHM_MAP, h, 0, (long)((u64)(t & 0x7FFFFFFF) << 12), PROT_READ | PROT_WRITE, 0, 0);
		void *res = 0;
		if ((u64)mp < (u64)-4096 && *(u32 *)mp == BLOB_MAGIC)
			res = (void *)mp; /* (autre magique : projection gardée, comme l'asm) */
		korin_sys1(SYS_HANDLE_CLOSE, h);
		return res;
	}
	return 0;
}

/* =========================================================================
 *  exec.inc : exec (SYS_EXEC) ; l'état d'AppKit (descripteurs ouverts) passe
 *  au nouveau programme dans un objet marqué EXEC_FC_TAG | 1
 * ========================================================================= */

/* reprise : une fois par process (0 : à voir, 1 : en cours, 2 : fait) */
static NOINL void fc_exec_resume(void)
{
	u32 list[256 * 4];
	for (;;) {
		u32 e = 0;
		if (__atomic_compare_exchange_n(&fc_exec_etat, &e, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
			break;
		if (e == 2)
			return;
		korin_sys0(SYS_YIELD); /* un autre thread la fait */
	}
	long f = korin_sys0(SYS_PROC_FLAGS); /* lancé par exec ? */
	if (f < 0 || !(f & PROC_EXEC))
		goto fait;
	long n = korin_sys3(SYS_HANDLE_LIST, list, 256, 0); /* (marques gardées : libposix les lit) */
	u32 h = 0;
	for (long i = 0; i < n && i < 256; i++)
		if ((list[i * 4 + 1] >> 30) == (EXEC_FC_TAG >> 30)) {
			h = list[i * 4];
			break;
		}
	if (!h)
		goto fait;
	long mp = korin_sys6(SYS_SHM_MAP, h, 0, 4096, PROT_READ, 0, 0);
	if ((u64)mp < (u64)-4096) {
		const u32 *b = (const u32 *)mp;
		u32 cnt = b[1];
		if (b[0] == FCX_MAGIC && cnt <= FC_MAX) {
			const u8 *ent = (const u8 *)mp + 8;
			for (; cnt; cnt--, ent += FCX_ENT_SIZE) {
				u64 rang = *(const u64 *)ent;
				if (rang < FC_MAX)
					fc_copy(&fc_desc[rang], ent + 8, sizeof(struct fcd));
			}
		}
		korin_sys2(SYS_VM_UNMAP, mp, 4096);
	}
	korin_sys1(SYS_HANDLE_CLOSE, h);
fait:
	__atomic_store_n(&fc_exec_etat, 2, __ATOMIC_RELEASE);
}

/* (exportée : libposix, au démarrage) */
EXPORT void f_exec_resume(void)
{
	fc_exec_resume();
}

/* -> poignée de l'objet de l'état d'AppKit (rempli), < 0 erreur */
static NOINL long fc_exec_state_make(void)
{
	long h = korin_sys2(SYS_SHM_CREATE, 4096, 0);
	if (h < 0)
		return h;
	long mp = korin_sys6(SYS_SHM_MAP, h, 0, 4096, PROT_READ | PROT_WRITE, 0, 0);
	if ((u64)mp >= (u64)-4096) {
		korin_sys1(SYS_HANDLE_CLOSE, h);
		return mp;
	}
	u32 *b = (u32 *)mp;
	b[0] = FCX_MAGIC;
	fc_take(&fc_verrou);
	u8 *ent = (u8 *)mp + 8;
	for (long rang = 0; rang < FC_MAX; rang++) {
		long t = fc_desc[rang].type;
		if (t == 3 || !t) /* ouverts : 1, 2, 4 */
			continue;
		long fd = rang + FC_BASE; /* pas une entrée standard */
		if (fc_stdio[0] == fd || fc_stdio[1] == fd || fc_stdio[2] == fd)
			continue;
		*(u64 *)ent = (u64)rang;
		fc_copy(ent + 8, &fc_desc[rang], sizeof(struct fcd));
		ent += FCX_ENT_SIZE;
		b[1]++;
	}
	fc_release(&fc_verrou);
	korin_sys2(SYS_VM_UNMAP, mp, 4096);
	return h;
}

/* canal, numéro du fichier (ouvert par V_EXEC ; fermé ici), taille, chemin
 * résolu -> 0 : image construite ; n + 1 : l'ELF dans le tampon partagé n ;
 * < 0 : erreur (E_NOEXEC pour une DLL .NET ou ce qui n'est pas un ELF) */
static NOINL long fc_exec_read(long canal, long num, u64 size, const char *rpath)
{
	long rang;
	struct fcd *d = fc_reserve(&rang); /* descripteur temporaire */
	if (!d) {
		fc_fs_close(canal, num);
		return E_MFILE;
	}
	long fd = rang + FC_BASE;
	fc_temp_fill(d, canal, num, size);
	u16 mz = 0; /* ses deux premiers octets */
	f_read(fd, &mz, 2);
	d->pos = 0;
	long res = E_NOEXEC;
	if (mz == ('M' | ('Z' << 8)))
		goto ferme;
	res = fc_image_load(rpath, size, fd, 0); /* le fichier dans les pages de l'image */
	if (res == E_INVAL) { /* (IMAGE_SEAL) : pas un ELF */
		res = E_NOEXEC;
		goto ferme;
	}
	if (res != E_NOSYS && res != E_NOSPC)
		goto ferme;
	res = E_INVAL;
	if (size > FC_IMAGE_MAX)
		goto ferme;
	long b = korin_sys1(SYS_BUF_CREATE, size);
	res = b;
	if (b < 0)
		goto ferme;
	long r = f_read(fd, (void *)(BUF_VA_BASE + ((u64)b << 26)), size);
	if ((u64)r == size) {
		res = b + 1;
		goto ferme;
	}
	res = r < 0 ? r : E_INVAL; /* (fichier plus court) */
	korin_sys1(SYS_BUF_FREE, b);
ferme:
	f_close(fd);
	return res;
}

/* --- f_exec_file(réponse de V_EXEC, params EXP_* 72 o) -> erreur (ne revient
 * pas sinon). Le fichier est fermé ici ; EXP_NAME = 0 : le nom de la réponse ;
 * params rendus intacts en cas d'erreur. /sys : E_PERM. */
EXPORT long f_exec_file(long *resp, long *P)
{
	u32 marks[(IPC_INHERIT_MAX + 1) * 2]; /* celles de l'appelant + la sienne */
	msg_t m2;                            /* réponse d'un second V_EXEC */
	char name[32];
	long save[4];
	fc_exec_check(); /* (son état repris s'il vient d'un exec) */
	save[0] = P[EXP_NAME];
	save[1] = P[EXP_MARKS];
	save[2] = P[EXP_NMARKS];
	save[3] = P[EXP_IMAGE];
	long canal = M_A(resp), num = M_B(resp);
	u64 size = (u64)M_D(resp);
	long res = E_PERM; /* exec n'accorde pas les droits */
	long hs;
	if (M_C(resp) & ATTR_SYS)
		goto ferme_f;
	res = E_INVAL;
	if (size < 64)
		goto ferme_f;
	{ /* le nom (31 caractères au plus) */
		const char *s = M_DATA(resp);
		char *o = name;
		for (int k = 31;;) {
			char c = *s++;
			*o++ = c;
			if (!c)
				break;
			if (!--k) {
				*o = 0;
				break;
			}
		}
	}
	char *rpath = M_DATA(m2); /* chemin résolu : dans le message de travail */
	str_cat(rpath, fc_after_name(resp));
	if (!P[EXP_NAME])
		P[EXP_NAME] = (long)name;
	res = fc_exec_state_make(); /* son état */
	if (res < 0)
		goto ferme_f;
	hs = res;
	u64 nm = (u64)P[EXP_NMARKS];
	res = E_INVAL;
	if (nm >= IPC_INHERIT_MAX)
		goto rend_e; /* (le fichier reste ouvert, comme l'asm) */
	if (nm)
		fc_copy(marks, (const void *)P[EXP_MARKS], nm * 8);
	marks[nm * 2] = (u32)hs;
	marks[nm * 2 + 1] = EXEC_FC_TAG | 1;
	P[EXP_NMARKS] = (long)(nm + 1);
	P[EXP_MARKS] = (long)marks;
	P[EXP_IMAGE] = 0;
	fc_fs_close(canal, num); /* le fichier : fermé (l'image suffit) */
	res = korin_sys3(SYS_EXEC, rpath, size, P); /* l'image de ce programme ? */
	if (res != E_NOENT)
		goto rend_e;
	M_OP(m2) = V_EXEC; /* pas d'image : le fichier, rouvert */
	M_A(m2) = 0;
	M_D(m2) = 0;
	M_LEN(m2) = (u32)(strlen(rpath) + 1);
	res = fc_vfs(m2);
	if (res < 0)
		goto rend_e;
	rpath = fc_after_name(m2);
	canal = M_A(m2);
	num = M_B(m2);
	size = (u64)M_D(m2);
	res = fc_exec_read(canal, num, size, rpath); /* image construite, ou ELF en tampon */
	if (res < 0)
		goto rend_e;
	long tb = res;
	if (tb >= 1) /* un tampon : l'ELF de lui */
		P[EXP_IMAGE] = (long)(BUF_VA_BASE + ((u64)(tb - 1) << 26));
	res = korin_sys3(SYS_EXEC, rpath, size, P);
	if (tb)
		korin_sys1(SYS_BUF_FREE, tb - 1); /* (échec : tampon rendu) */
	hs = marks[(P[EXP_NMARKS] - 1) * 2]; /* l'état : sa poignée est la dernière */
rend_e:
	korin_sys1(SYS_HANDLE_CLOSE, hs); /* échec : l'objet de l'état rendu */
	goto restaure;
ferme_f:
	fc_fs_close(canal, num);
restaure:
	P[EXP_NAME] = save[0];
	P[EXP_MARKS] = save[1];
	P[EXP_NMARKS] = save[2];
	P[EXP_IMAGE] = save[3];
	return res;
}

/* --- f_exec(chemin, argv, envp, &fds[3]) -> erreur (ne revient pas sinon) ;
 * argv[0] compris ; envp = 0 : aucun environnement ; fds : comme
 * f_spawn_stdio (0 : entrées / sorties inchangées). Ne ferme rien. */
EXPORT long f_exec(const char *path, char *const *argv, char *const *envp, const long *fds)
{
	long P[EXP_N + 1];
	u32 tag[2];
	msg_t m;
	char line[LIGNE_MAX];
	char *sargv[SA_MAX];
	for (int i = 0; i < EXP_N; i++) /* params à zéro */
		P[i] = 0;
	if (fds) { /* entrées / sorties : poignées */
		fc_stdio_init();
		if (fc_stdio_handles(fds, &P[EXP_STDIN]))
			return E_BADF;
	}
	long res = fc_msg_path(m, V_EXEC, 0, path); /* le chemin (vfs : V_EXEC) */
	if (res)
		return res;
	res = fc_vfs(m);
	if (res < 0)
		return res;
	if (M_C(m) & ATTR_SCRIPT) { /* argv = interprète, [argument,] script, argv[1..] */
		M_C(m) &= ~(long)ATTR_SCRIPT;
		char **a = fc_script_argv(m, argv, sargv, SA_MAX);
		res = E_2BIG;
		if (!a)
			goto ferme_e;
		argv = a;
	}
	int me;
	long h = fc_blob(argv, envp, &tag[1], &me, &res); /* argv et environnement : un objet */
	if (h < 0)
		goto ferme_e;
	tag[0] = (u32)h;
	P[EXP_MARKS] = (long)tag;
	P[EXP_NMARKS] = 1;
	if (env_line(argv, line) && line[0]) /* argv[1..] : la ligne (trop longue : vide) */
		P[EXP_ARGS] = (long)line;
	res = f_exec_file(m, P); /* (ferme le fichier) */
	korin_sys1(SYS_HANDLE_CLOSE, h); /* échec : l'objet rendu */
	return res;
ferme_e:
	fc_fs_close(M_A(m), M_B(m));
	return res;
}

/* =========================================================================
 *  sync.inc : verrous (mutex) et conditions, sur les futex du noyau
 *  Mutex : mot de 32 bits, 0 libre, 1 pris, 2 pris et attendu. Condition : mot
 *  de 32 bits (compteur de signaux) ; réveils parasites possibles.
 *  (Valeurs rendues sans contrat : celles de l'asm, x0 laissé tel quel.)
 * ========================================================================= */

EXPORT long mtx_lock(u32 *m)
{
	long r = (long)m;
	u32 z = 0;
	if (__atomic_compare_exchange_n(m, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return r; /* libre : 0 -> 1, sans appel */
	/* pris : on le marque 2 (attente) ; trouvé libre, il est à nous */
	while (__atomic_exchange_n(m, 2, __ATOMIC_ACQUIRE))
		r = korin_sys3(SYS_FUTEX_WAIT, m, 2, 0);
	return r;
}

EXPORT long mtx_trylock(u32 *m)
{
	u32 z = 0;
	if (__atomic_compare_exchange_n(m, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return 0;
	return E_BUSY;
}

EXPORT long mtx_unlock(u32 *m)
{
	if (__atomic_exchange_n(m, 0, __ATOMIC_RELEASE) != 2)
		return (long)m;
	return korin_sys2(SYS_FUTEX_WAKE, m, 1); /* quelqu'un attend */
}

/* condition, mutex pris, attente ms (0 : sans fin) -> 0 (signal, ou réveil
 * parasite), E_TIMEDOUT ; le mutex est repris */
EXPORT long cnd_timedwait(u32 *c, u32 *m, long ms)
{
	u32 v = __atomic_load_n(c, __ATOMIC_ACQUIRE); /* compteur avant de rendre le mutex */
	mtx_unlock(m);
	long r = korin_sys3(SYS_FUTEX_WAIT, c, v, ms);
	mtx_lock(m);
	return r == E_TIMEDOUT ? E_TIMEDOUT : 0;
}

EXPORT long cnd_wait(u32 *c, u32 *m)
{
	return cnd_timedwait(c, m, 0);
}

static long cnd_wake(u32 *c, long n)
{
	__atomic_add_fetch(c, 1, __ATOMIC_ACQ_REL);
	return korin_sys2(SYS_FUTEX_WAKE, c, n);
}

EXPORT long cnd_signal(u32 *c)
{
	return cnd_wake(c, 1);
}

EXPORT long cnd_broadcast(u32 *c)
{
	return cnd_wake(c, 0x7FFFFFFF);
}

/* =========================================================================
 *  lang.inc : la langue et les traductions
 *  Langue : « lang = xx » de /etc/system.ini (sans clé : fr) ;
 *  dictionnaires /res/lang/<xx>.txt puis ceux du programme (lang_load),
 *  le dernier lu gagne. Ligne « source = traduction », « # » : commentaire ;
 *  \n, \= et \\ dans le texte.
 * ========================================================================= */

/* texte -> son seau (FNV-1a) */
static u64 lang_hash(const char *s)
{
	u32 h = 0x811C9DC5U;
	u8 c;
	while ((c = (u8)*s++)) {
		h ^= c;
		h *= 0x01000193U;
	}
	h ^= h >> 15;
	return h & (LANG_SEAUX - 1);
}

/* chemin -> son contenu terminé par NUL (0 : absent) */
static NOINL char *lang_read(const char *path)
{
	long st[2];
	if (f_stat(path, st))
		return 0;
	u64 n = (u64)st[0];
	char *buf = malloc(n + 1);
	if (!buf)
		return 0;
	long fd = f_open(path, O_READ);
	if (fd < 0)
		return 0;
	long r = f_read(fd, buf, n);
	f_close(fd);
	if (r < 0)
		return 0;
	buf[r] = 0;
	return buf;
}

/* texte : \n, \= et \\ décodés sur place */
static void lang_decode(char *s)
{
	char *o = s, *i = s;
	for (;;) {
		char c = *i++;
		if (c == '\\') {
			c = *i++;
			if (c == 'n') {
				c = 10;
			} else if (!c) {
				*o = 0;
				return;
			}
		}
		*o++ = c;
		if (!c)
			return;
	}
}

/* chemin : lit le dictionnaire et retient ses lignes -> 1 si lu (le texte
 * reste en mémoire : les entrées y pointent) */
static NOINL long lang_add(const char *path)
{
	char *p = lang_read(path);
	if (!p)
		return 0;
	if (!lang_seaux_p) {
		char **t = malloc(LANG_SEAUX * 8);
		if (!t)
			return 0;
		for (int i = LANG_SEAUX - 1; i >= 0; i--)
			t[i] = 0;
		lang_seaux_p = (char *)t;
	}
	char **seaux = (char **)lang_seaux_p;
	while (*p) { /* une ligne */
		char *deb = p;
		for (;;) { /* fin de ligne -> NUL */
			char c = *p;
			if (!c)
				break;
			p++;
			if (c == 10) {
				char *e = p - 1;
				*e = 0;
				e--; /* (retour chariot) */
				if (e >= deb && *e == 13)
					*e = 0;
				break;
			}
		}
		if (*deb == '#')
			continue;
		char *q = deb; /* le premier « = » : source avant, traduction après */
		for (;;) {
			char c = *q;
			if (!c)
				goto ligne;
			if (c == '=')
				break;
			if (c == '\\' && !*++q)
				goto ligne;
			q++;
		}
		*q = 0;
		char *tr = q + 1;
		while (*tr == ' ') /* espaces après « = » */
			tr++;
		for (;;) { /* espaces avant « = » */
			q--;
			if (q < deb)
				goto ligne;
			if (*q != ' ')
				break;
			*q = 0;
		}
		lang_decode(deb);
		lang_decode(tr);
		char **e = malloc(24);
		if (!e)
			return 1;
		e[0] = deb;
		e[1] = tr;
		u64 k = lang_hash(deb);
		e[2] = seaux[k];
		seaux[k] = (char *)e;
ligne:;
	}
	return 1;
}

/* la langue et le dictionnaire du système, au premier besoin */
static NOINL void lang_init(void)
{
	if (lang_ok)
		return;
	lang_ok = 1;
	const char *s = lang_read("/etc/system.ini"); /* « lang = xx » */
	if (s) {
		for (;;) {
			if (!*s)
				break;
			if (s[0] == 'l' && s[1] == 'a' && s[2] == 'n' && s[3] == 'g') {
				s += 4; /* (« language » aussi : comme l'asm) */
				while (*s == ' ' || *s == '=')
					s++;
				int k = 0; /* la langue (2 à 7 lettres) */
				for (; k < 7; k++) {
					u8 c = (u8)s[k];
					if ((u32)c - 'a' >= 26)
						break;
					lang_cur[k] = (char)c;
				}
				lang_cur[k] = 0;
				break;
			}
			char c; /* ligne suivante */
			do
				c = *s++;
			while (c && c != 10);
			if (!c)
				break;
		}
	}
	if (!lang_cur[0]) { /* sans clé : fr */
		lang_cur[0] = 'f';
		lang_cur[1] = 'r';
		lang_cur[2] = 0;
	}
	str_cat(str_cat(str_cat(lang_chemin, "/res/lang/"), lang_cur), ".txt");
	lang_add(lang_chemin);
}

/* texte -> sa traduction dans la langue choisie (ou le texte) */
EXPORT const char *lang_tr(const char *s)
{
	if (!s)
		return s;
	lang_init();
	char **seaux = (char **)lang_seaux_p;
	if (!seaux)
		return s;
	for (char **e = (char **)seaux[lang_hash(s)]; e; e = (char **)e[2])
		if (str_eq(e[0], s))
			return e[1];
	return s;
}

EXPORT long lang_puts(const char *s)
{
	return puts(lang_tr(s));
}

EXPORT const char *lang_get(void)
{
	lang_init();
	return lang_cur;
}

/* chemin d'un dictionnaire du programme, déjà dans la langue choisie -> 1 si lu */
EXPORT long lang_load(const char *path)
{
	lang_init();
	return lang_add(path);
}

/* ---- le coffre à secrets (service keyvault, docs/KEYVAULT.md) : le client de src/lib/keyvault/keyvault.h ;
 * l'identité du programme est prise par le coffre lui-même */
#define KV_SYSCALL(n, a, b) korin_sys2((n), (a), (b))
#include "../../lib/keyvault/keyvault.h"

EXPORT long vault_set(const char *name, const void *value, long len)
{
	return kv_set(name, value, len);
}

EXPORT long vault_get(const char *name, void *out, long max)
{
	return kv_get(name, out, max);
}

EXPORT long vault_delete(const char *name)
{
	return kv_delete(name);
}

EXPORT long vault_list(char *out, long max)
{
	return kv_list(out, max);
}

EXPORT long vault_status(void)
{
	return kv_status();
}

EXPORT long vault_error(char *out, long max)
{
	return kv_error(out, max);
}
