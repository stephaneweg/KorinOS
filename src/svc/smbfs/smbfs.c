/* smbfs - un partage SMB (Windows, Samba, macOS, smbd de Korin) vu comme un volume de Korin
 * (docs/SMB.md). Un service de fichiers comme ramfs et fatfs (protocole de src/inc/fs.inc), lancé
 * par vfs au montage :
 *
 *   mount NET0: //192.168.0.9/Korin smbfs        (source : //hôte/partage[/dossier])
 *   mount NET0: smb:bureau smbfs                 (source : une entrée [bureau] de /etc/smbfs.ini)
 *
 * Les comptes sont dans /etc/smbfs.ini (SD0:/etc/smbfs.ini), une section par hôte (ou par nom) :
 *
 *   [192.168.0.9]
 *   user = stephane
 *   password = ...
 *   domain =                 (facultatif : le domaine ou le nom du PC Windows)
 *   [bureau]
 *   host = 192.168.0.9
 *   share = Korin
 *   user = ...
 *
 * Sur libposix et l'API synchrone de libsmb2 (NTLMv2, signature, SMB 2 et 3) ; un seul thread : une
 * requête à la fois. La connexion se fait à la première requête d'un programme (jamais pendant le
 * montage : vfs attend alors ce service, et lire /etc/smbfs.ini passe par vfs) ; elle est refaite
 * si le serveur l'a coupée. Pas de rootpath au montage (un dossier dans la source à la place). */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <armpi/syscall.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

typedef int64_t i64;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

/* ---- Korin : appels système, message, protocole (src/inc/syscalls.inc, fs.inc, armpi.inc) */
#define SYS_BUF_CREATE 0x01
#define SYS_BUF_GRANT 0x02
#define SYS_BUF_FREE 0x04
#define SYS_PROC_INFO 0x1E
#define SYS_SVC_REGISTER 0x30
#define SYS_SVC_NEXT 0x33
#define SYS_SVC_REPLY 0x34
#define SYS_SVC_MAP_CALLER 0x36
#define SVF_ENDS 1
#define SVF_BIRTHS 2
#define SVC_OP_END 0xFFFF
#define SVC_OP_BIRTH 0xFFFE
#define SVC_OP_FORK 0xFFFC
#define SVC_PID_KERNEL 0x7FFFFFFF
#define MC_WRITE 1
#define BUF_VA_BASE 0x6800000000ULL
#define BUF_SLOT 0x4000000ULL

#define FS_OPEN 1
#define FS_STAT 2
#define FS_MKDIR 3
#define FS_UNLINK 4
#define FS_UTIME 5
#define FS_RENAME 6
#define FS_READ 10
#define FS_WRITE 11
#define FS_CLOSE 12
#define FS_READDIR 13
#define FS_BUFFER 14
#define FS_READ_BUF 15
#define FS_WRITE_BUF 16
#define FS_READ_DIRECT 17
#define FS_INFO 19
#define FS_SYNC 25
#define FS_SEEK 26
#define FS_POS_CUR -1
#define FS_DATA_MAX 464
#define FS_BUFFER_SIZE 65536
#define FS_DIRECT_MAX 0x100000
#define FS_T_SMB 4

#define O_K_READ 1
#define O_K_WRITE 2
#define O_K_CREATE 4
#define O_K_TRUNC 8
#define O_K_APPEND 16
#define ATTR_DIR 0x10
#define ATTR_FILE 0x20
#define ATTR_SYS 0x100
#define ATTR_LIB 0x200

/* erreurs de Korin : numéros de Linux (newlib a les siens) */
#define E_NOENT -2
#define E_IO -5
#define E_BADF -9
#define E_AGAIN -11
#define E_NOMEM -12
#define E_ACCES -13
#define E_BUSY -16
#define E_EXIST -17
#define E_XDEV -18
#define E_NOTDIR -20
#define E_ISDIR -21
#define E_INVAL -22
#define E_MFILE -24
#define E_NOSPC -28
#define E_NAMETOOLONG -36
#define E_NOTEMPTY -39
#define E_TIMEDOUT -110

struct msg {
	i64 op;
	i64 a, b, c, d;
	u32 len;
	u32 pid;
	u8 data[464];
};
_Static_assert(sizeof(struct msg) == 512, "message de 512 octets");

static long ksys(long n, long a, long b, long c, long d)
{
	return armpi_syscall(n, a, b, c, d, 0, 0);
}

/* ---- état */

#define OUV_MAX 128
#define OWN_MAX 8
#define TP_MAX 48

struct ent {
	char *name;
	u64 size;
	u64 attr;
};

struct ouvert {
	int used, isdir, append;
	struct smb2fh *fh;
	u32 conn; /* génération de la connexion (refaite : le fichier est perdu) */
	struct ent *ents;
	int nents;
	i64 pos;
	u64 size;
	u32 owners[OWN_MAX];
	char path[600];
};

static struct ouvert ouverts[OUV_MAX];
static struct {
	i64 pid, num;
} tampons[TP_MAX];
static struct msg msg __attribute__((aligned(16)));
static long canal, req_cour, pid_vfs;
static char volume[32], source[256];

/* la connexion */
static struct smb2_context *smb2;
static u32 conn_gen;
static char host[128], share[128], subdir[256], user[128], password[128], domain[128];
static int creds_read;
static char last_error[256];

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "smbfs %s: ", volume);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/* errno de newlib (ou -errno de libsmb2) -> erreur de Korin */
static long kerr(int e)
{
	if (e < 0)
		e = -e;
	switch (e) {
	case ENOENT:
		return E_NOENT;
	case EEXIST:
		return E_EXIST;
	case ENOTDIR:
		return E_NOTDIR;
	case EISDIR:
		return E_ISDIR;
	case EINVAL:
		return E_INVAL;
	case ENOSPC:
		return E_NOSPC;
	case ENOTEMPTY:
		return E_NOTEMPTY;
	case EACCES:
	case EPERM:
		return E_ACCES;
	case EBUSY:
		return E_BUSY;
	case ENOMEM:
		return E_NOMEM;
	case ENAMETOOLONG:
		return E_NAMETOOLONG;
	case ETIMEDOUT:
		return E_TIMEDOUT;
	case EAGAIN:
		return E_AGAIN;
	case EXDEV:
		return E_XDEV;
	default:
		return E_IO;
	}
}

static int conn_lost(int r)
{
	if (r >= 0)
		return 0;
	int e = -r;
	return e == ENOTCONN || e == ECONNRESET || e == EPIPE || e == ETIMEDOUT || e == ECONNABORTED ||
	       e == ESHUTDOWN || !smb2 || smb2_get_fd(smb2) < 0;
}

/* ---- configuration (/etc/smbfs.ini) */

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
	return s;
}

/* les clés de la section « name » (sans la casse) ; 1 : trouvée */
static int ini_section(const char *name)
{
	FILE *f = fopen("/etc/smbfs.ini", "r");
	if (!f)
		return 0;
	char line[512], sec[160] = "";
	int found = 0;
	while (fgets(line, sizeof line, f)) {
		char *s = trim(line);
		if (!*s || *s == '#' || *s == ';')
			continue;
		if (*s == '[') {
			char *e = strchr(s, ']');
			if (e)
				*e = 0;
			snprintf(sec, sizeof sec, "%s", trim(s + 1));
			continue;
		}
		if (strcasecmp(sec, name))
			continue;
		char *eq = strchr(s, '=');
		if (!eq)
			continue;
		*eq = 0;
		char *k = trim(s), *v = trim(eq + 1);
		found = 1;
		if (!strcasecmp(k, "user"))
			snprintf(user, sizeof user, "%s", v);
		else if (!strcasecmp(k, "password"))
			snprintf(password, sizeof password, "%s", v);
		else if (!strcasecmp(k, "domain"))
			snprintf(domain, sizeof domain, "%s", v);
		else if (!strcasecmp(k, "host") && !host[0])
			snprintf(host, sizeof host, "%s", v);
		else if (!strcasecmp(k, "share") && !share[0])
			snprintf(share, sizeof share, "%s", v);
	}
	fclose(f);
	return found;
}

/* la source -> host, share, subdir ; les comptes */
static int read_creds(void)
{
	if (creds_read)
		return 0;
	const char *s = source;
	if (!strncasecmp(s, "smb://", 6))
		s += 4;
	if (s[0] == '/' && s[1] == '/') { /* //hôte/partage[/dossier] */
		s += 2;
		const char *p = strchr(s, '/');
		if (!p)
			return -1;
		snprintf(host, sizeof host, "%.*s", (int)(p - s), s);
		s = p + 1;
		p = strchr(s, '/');
		if (p) {
			snprintf(share, sizeof share, "%.*s", (int)(p - s), s);
			snprintf(subdir, sizeof subdir, "%s", p + 1);
		} else {
			snprintf(share, sizeof share, "%s", s);
		}
		char hs[300];
		snprintf(hs, sizeof hs, "%s/%s", host, share);
		if (!ini_section(hs) && !ini_section(host))
			ini_section("default");
	} else if (!strncasecmp(s, "smb:", 4)) { /* smb:nom : une entrée de smbfs.ini */
		if (!ini_section(s + 4))
			return -1;
		if (!user[0])
			ini_section("default");
	} else {
		return -1;
	}
	size_t n = strlen(subdir); /* (dossier sans « / » autour, en « \ ») */
	while (n && subdir[n - 1] == '/')
		subdir[--n] = 0;
	for (char *p = subdir; *p; p++)
		if (*p == '/')
			*p = '\\';
	if (!host[0] || !share[0])
		return -1;
	creds_read = 1;
	return 0;
}

static void disconnect(void)
{
	if (smb2) {
		smb2_disconnect_share(smb2);
		smb2_destroy_context(smb2);
	}
	smb2 = 0;
	conn_gen++;
}

static long connect_share(void)
{
	if (smb2)
		return 0;
	if (read_creds() < 0) {
		say("source « %s » : il faut //hôte/partage, ou smb:nom et une section [nom] de /etc/smbfs.ini",
		    source);
		return E_INVAL;
	}
	smb2 = smb2_init_context();
	if (!smb2)
		return E_NOMEM;
	smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
	smb2_set_version(smb2, SMB2_VERSION_ANY);
	smb2_set_timeout(smb2, 30);
	if (user[0])
		smb2_set_user(smb2, user);
	if (password[0])
		smb2_set_password(smb2, password);
	if (domain[0])
		smb2_set_domain(smb2, domain);
	char ws[64] = "KORIN";
	gethostname(ws, sizeof ws);
	smb2_set_workstation(smb2, ws);
	int r = smb2_connect_share(smb2, host, share, user[0] ? user : NULL);
	if (r < 0) {
		const char *e = smb2_get_error(smb2);
		if (strcmp(e, last_error)) { /* (une fois par erreur) */
			say("connexion à //%s/%s impossible : %s", host, share, e);
			snprintf(last_error, sizeof last_error, "%s", e);
		}
		smb2_destroy_context(smb2);
		smb2 = 0;
		return r == -EACCES || r == -EPERM ? E_ACCES : E_IO;
	}
	last_error[0] = 0;
	conn_gen++;
	say("connecté à //%s/%s (SMB %x)", host, share, smb2_get_dialect(smb2));
	return 0;
}

/* chemin de Korin (« a/b », relatif à la racine du volume) -> chemin SMB (« dossier\a\b ») */
static void smb_path(const char *p, char *out, size_t max)
{
	size_t n = 0;
	if (subdir[0])
		n = (size_t)snprintf(out, max, "%s", subdir);
	else
		out[0] = 0;
	while (*p == '/')
		p++;
	if (*p && n && n < max - 1)
		out[n++] = '\\';
	for (; *p && n < max - 1; p++)
		out[n++] = *p == '/' ? '\\' : *p;
	while (n && out[n - 1] == '\\')
		n--;
	out[n] = 0;
}

static u64 attr_of(const struct smb2_stat_64 *st)
{
	return st->smb2_type == SMB2_TYPE_DIRECTORY ? ATTR_DIR : ATTR_FILE;
}

/* ---- propriétaires et tampons (comme src/svc/common/fsowner.c) */

static long proprietaire(struct msg *m)
{
	long pid = m->pid, d = m->d;
	if (!d)
		return pid;
	if (pid == pid_vfs)
		return d;
	if (pid == SVC_PID_KERNEL)
		return pid;
	static u8 pl[64] __attribute__((aligned(8)));
	if (ksys(SYS_PROC_INFO, pid, (long)pl, 0, 0) != 0)
		return pid;
	if (memcmp(pl + 24, "vfs", 4))
		return pid;
	pid_vfs = pid;
	return d;
}

static int own_has(struct ouvert *o, u32 pid)
{
	for (int i = 0; i < OWN_MAX; i++)
		if (o->owners[i] == pid)
			return 1;
	return 0;
}

static int own_add(struct ouvert *o, u32 pid)
{
	if (own_has(o, pid))
		return 1;
	for (int i = 0; i < OWN_MAX; i++)
		if (!o->owners[i]) {
			o->owners[i] = pid;
			return 1;
		}
	return 0;
}

static int own_count(struct ouvert *o)
{
	int c = 0;
	for (int i = 0; i < OWN_MAX; i++)
		c += o->owners[i] != 0;
	return c;
}

static void ouvert_free(struct ouvert *o)
{
	if (o->fh && smb2 && o->conn == conn_gen)
		smb2_close(smb2, o->fh);
	for (int i = 0; i < o->nents; i++)
		free(o->ents[i].name);
	free(o->ents);
	memset(o, 0, sizeof *o);
}

/* le fichier ouvert « a » si l'appelant y a droit, sinon 0 (*err) */
static struct ouvert *ouvert_de(struct msg *m, long *err)
{
	u64 num = (u64)m->a;
	if (num >= OUV_MAX || !ouverts[num].used) {
		*err = E_INVAL;
		return 0;
	}
	struct ouvert *o = &ouverts[num];
	u32 pid = m->pid;
	if (pid && pid != SVC_PID_KERNEL && !own_has(o, pid)) {
		*err = E_BADF;
		return 0;
	}
	if (!o->isdir && (o->conn != conn_gen || !smb2)) { /* (connexion refaite : perdu) */
		*err = E_IO;
		return 0;
	}
	return o;
}

static int tampon_entree(long pid)
{
	for (int i = 0; i < TP_MAX; i++)
		if (tampons[i].pid == pid)
			return i;
	return -1;
}

static u8 *tampon_de(struct msg *m)
{
	int i = m->pid ? tampon_entree(m->pid) : -1;
	return i < 0 ? 0 : (u8 *)(uintptr_t)(BUF_VA_BASE + (u64)tampons[i].num * BUF_SLOT);
}

static long op_tampon(struct msg *m)
{
	long pid = m->pid;
	if (!pid || pid == SVC_PID_KERNEL)
		return E_INVAL;
	int i = tampon_entree(pid);
	if (i < 0) {
		i = tampon_entree(0);
		if (i < 0)
			return E_NOMEM;
		long num = ksys(SYS_BUF_CREATE, FS_BUFFER_SIZE, 0, 0, 0);
		if (num < 0)
			return num;
		tampons[i].pid = pid;
		tampons[i].num = num;
		ksys(SYS_BUF_GRANT, num, pid, 0, 0);
	}
	m->a = tampons[i].num;
	return 0;
}

static void fin_pid(long pid)
{
	if (pid == pid_vfs)
		pid_vfs = 0;
	if (!pid)
		return;
	for (int k = 0; k < OUV_MAX; k++) {
		struct ouvert *o = &ouverts[k];
		if (!o->used)
			continue;
		for (int i = 0; i < OWN_MAX; i++)
			if (o->owners[i] == (u32)pid)
				o->owners[i] = 0;
		if (!own_count(o))
			ouvert_free(o);
	}
	int i = tampon_entree(pid);
	if (i >= 0) {
		ksys(SYS_BUF_FREE, tampons[i].num, 0, 0, 0);
		tampons[i].pid = tampons[i].num = 0;
	}
}

static void fork_pid(long child, long parent)
{
	if (!child || !parent)
		return;
	for (int k = 0; k < OUV_MAX; k++)
		if (ouverts[k].used && own_has(&ouverts[k], (u32)parent))
			own_add(&ouverts[k], (u32)child);
}

/* ---- opérations */

/* stat avec une reconnexion si le serveur a coupé */
static int do_stat(const char *sp, struct smb2_stat_64 *st)
{
	int r = smb2_stat(smb2, sp, st);
	if (conn_lost(r)) {
		disconnect();
		if (connect_share() < 0)
			return -EIO;
		r = smb2_stat(smb2, sp, st);
	}
	return r;
}

static int dir_load(struct ouvert *o, const char *sp)
{
	struct smb2dir *d = smb2_opendir(smb2, sp);
	if (!d)
		return -1;
	int cap = 32;
	o->ents = calloc((size_t)cap, sizeof *o->ents);
	struct smb2dirent *e;
	while (o->ents && (e = smb2_readdir(smb2, d))) {
		if (!strcmp(e->name, ".") || !strcmp(e->name, ".."))
			continue;
		if (o->nents == cap) {
			struct ent *n = realloc(o->ents, (size_t)cap * 2 * sizeof *n);
			if (!n)
				break;
			o->ents = n;
			cap *= 2;
		}
		struct ent *x = &o->ents[o->nents];
		x->name = strdup(e->name);
		x->size = e->st.smb2_type == SMB2_TYPE_DIRECTORY ? 0 : e->st.smb2_size;
		x->attr = attr_of(&e->st);
		if (x->name)
			o->nents++;
	}
	smb2_closedir(smb2, d);
	return 0;
}

/* 1 ouvrir(a drapeaux ; chemin) -> a canal, b numéro, c attributs, d taille */
static long op_open(struct msg *m)
{
	long extras = m->c & (ATTR_SYS | ATTR_LIB);
	long flags = m->a;
	char sp[600];
	smb_path((char *)m->data, sp, sizeof sp);
	int k = -1;
	for (int i = 0; i < OUV_MAX && k < 0; i++)
		if (!ouverts[i].used)
			k = i;
	if (k < 0)
		return E_MFILE;
	struct smb2_stat_64 st;
	int r = do_stat(sp, &st);
	int exists = r >= 0;
	if (!exists && r != -ENOENT && r != -ENOTDIR)
		return kerr(r);
	if (!exists && !(flags & O_K_CREATE))
		return E_NOENT;
	struct ouvert *o = &ouverts[k];
	memset(o, 0, sizeof *o);
	snprintf(o->path, sizeof o->path, "%s", sp);
	o->conn = conn_gen;
	if (exists && st.smb2_type == SMB2_TYPE_DIRECTORY) {
		if (flags & O_K_WRITE)
			return E_ISDIR;
		if (dir_load(o, sp) < 0)
			return kerr(errno ? errno : EIO);
		o->isdir = 1;
	} else {
		int pf = (flags & O_K_WRITE) ? ((flags & O_K_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
		if (flags & O_K_CREATE)
			pf |= O_CREAT;
		if (flags & O_K_TRUNC)
			pf |= O_TRUNC;
		o->fh = smb2_open(smb2, sp, pf);
		if (!o->fh) {
			say("ouvrir « %s » : %s", sp, smb2_get_error(smb2));
			return exists ? E_ACCES : E_NOENT;
		}
		struct smb2_stat_64 fs;
		o->size = smb2_fstat(smb2, o->fh, &fs) >= 0 ? fs.smb2_size : (exists ? st.smb2_size : 0);
		o->append = (flags & O_K_APPEND) != 0;
		o->pos = o->append ? (i64)o->size : 0;
	}
	o->used = 1;
	own_add(o, (u32)proprietaire(m));
	m->a = (u32)canal;
	m->b = k;
	m->c = (o->isdir ? ATTR_DIR : ATTR_FILE) | extras;
	m->d = o->isdir ? 0 : (i64)o->size;
	return 0;
}

/* 2 stat(chemin) -> b taille, c attributs, d date */
static long op_stat(struct msg *m)
{
	char sp[600];
	smb_path((char *)m->data, sp, sizeof sp);
	struct smb2_stat_64 st;
	int r = do_stat(sp, &st);
	if (r < 0)
		return kerr(r);
	m->b = st.smb2_type == SMB2_TYPE_DIRECTORY ? 0 : (i64)st.smb2_size;
	m->c = (i64)attr_of(&st);
	m->d = (i64)st.smb2_mtime;
	return 0;
}

static long op_mkdir(struct msg *m)
{
	char sp[600];
	smb_path((char *)m->data, sp, sizeof sp);
	int r = smb2_mkdir(smb2, sp);
	return r < 0 ? kerr(r) : 0;
}

static long op_unlink(struct msg *m)
{
	char sp[600];
	smb_path((char *)m->data, sp, sizeof sp);
	struct smb2_stat_64 st;
	int r = do_stat(sp, &st);
	if (r < 0)
		return kerr(r);
	r = st.smb2_type == SMB2_TYPE_DIRECTORY ? smb2_rmdir(smb2, sp) : smb2_unlink(smb2, sp);
	return r < 0 ? kerr(r) : 0;
}

static long op_rename(struct msg *m)
{
	const char *a = (char *)m->data;
	const char *b = a + strlen(a) + 1;
	if ((u8 *)b >= m->data + sizeof m->data)
		return E_INVAL;
	char sa[600], sb[600];
	smb_path(a, sa, sizeof sa);
	smb_path(b, sb, sizeof sb);
	int r = smb2_rename(smb2, sa, sb);
	return r < 0 ? kerr(r) : 0;
}

static i64 pos_of(struct msg *m, struct ouvert *o)
{
	return m->b == FS_POS_CUR ? o->pos : m->b;
}

static void pos_after(struct msg *m, struct ouvert *o, long done, i64 pos)
{
	if (done < 0)
		return;
	pos += done;
	m->d = pos;
	if (m->b == FS_POS_CUR)
		o->pos = pos;
}

/* lit jusqu'à n octets à pos (morceaux de la taille du serveur) -> octets, < 0 : erreur */
static long read_at(struct ouvert *o, u8 *dst, u64 n, i64 pos)
{
	u64 done = 0;
	u32 max = smb2_get_max_read_size(smb2);
	if (!max || max > 0x100000)
		max = 0x100000;
	while (done < n) {
		u32 c = (u32)(n - done > max ? max : n - done);
		int r = smb2_pread(smb2, o->fh, dst + done, c, (u64)pos + done);
		if (r < 0)
			return done ? (long)done : kerr(r);
		if (r == 0)
			break;
		done += (u64)r;
		if ((u32)r < c)
			break;
	}
	return (long)done;
}

static long write_at(struct ouvert *o, const u8 *src, u64 n, i64 pos)
{
	u64 done = 0;
	u32 max = smb2_get_max_write_size(smb2);
	if (!max || max > 0x100000)
		max = 0x100000;
	while (done < n) {
		u32 c = (u32)(n - done > max ? max : n - done);
		int r = smb2_pwrite(smb2, o->fh, src + done, c, (u64)pos + done);
		if (r <= 0)
			return done ? (long)done : (r < 0 ? kerr(r) : E_IO);
		done += (u64)r;
	}
	if ((u64)pos + done > o->size)
		o->size = (u64)pos + done;
	return (long)done;
}

static struct ouvert *fichier_de(struct msg *m, long *err)
{
	struct ouvert *o = ouvert_de(m, err);
	if (o && o->isdir) {
		*err = E_INVAL;
		return 0;
	}
	return o;
}

/* 10 lire(a, b position, c octets) -> octets (données), d position après */
static long op_read(struct msg *m)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	i64 pos = pos_of(m, o);
	u64 n = (u64)m->c > FS_DATA_MAX ? FS_DATA_MAX : (u64)m->c;
	long r = read_at(o, m->data, n, pos);
	m->len = r > 0 ? (u32)r : 0;
	pos_after(m, o, r, pos);
	return r;
}

/* 11 écrire(a, b position, c octets, données) -> octets, b taille, d position après */
static long op_write(struct msg *m)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	if ((u64)m->c > FS_DATA_MAX)
		return E_INVAL;
	i64 pos = o->append ? (i64)o->size : pos_of(m, o);
	long r = write_at(o, m->data, (u64)m->c, pos);
	if (r < 0)
		return r;
	pos_after(m, o, r, pos);
	m->b = (i64)o->size;
	return r;
}

/* 15, 16 : au début du tampon partagé de l'appelant */
static long op_rw_buf(struct msg *m, int wr)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	u8 *buf = tampon_de(m);
	if (!buf || (u64)m->c > FS_BUFFER_SIZE)
		return E_INVAL;
	i64 pos = pos_of(m, o);
	long r;
	if (wr) {
		if (o->append)
			pos = (i64)o->size;
		r = write_at(o, buf, (u64)m->c, pos);
		if (r >= 0)
			m->b = (i64)o->size;
	} else {
		r = read_at(o, buf, (u64)m->c, pos);
	}
	pos_after(m, o, r, pos);
	return r;
}

/* 17 lire_d(a, b position, c octets, d adresse) : dans la mémoire de l'appelant */
static long op_read_direct(struct msg *m)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	if ((u64)m->c > FS_DIRECT_MAX)
		return E_INVAL;
	long va = ksys(SYS_SVC_MAP_CALLER, req_cour, m->d, m->c, MC_WRITE);
	if (va < 0)
		return va;
	i64 pos = pos_of(m, o);
	long r = read_at(o, (u8 *)va, (u64)m->c, pos); /* (démappé par SVC_REPLY) */
	pos_after(m, o, r, pos);
	return r;
}

/* 26 position(a, b décalage, c origine) -> position */
static long op_seek(struct msg *m)
{
	long err;
	struct ouvert *o = fichier_de(m, &err);
	if (!o)
		return err;
	if ((u64)m->c > 2)
		return E_INVAL;
	i64 base = m->c == 1 ? o->pos : m->c == 2 ? (i64)o->size : 0;
	i64 p = base + m->b;
	if (p < 0)
		return E_INVAL;
	o->pos = p;
	m->b = p;
	return p;
}

/* 13 lire_rep(a, b rang) -> 1 (nom[256], taille, attributs) ou 0 */
static long op_readdir(struct msg *m)
{
	long err;
	struct ouvert *o = ouvert_de(m, &err);
	if (!o)
		return err;
	if (!o->isdir)
		return E_INVAL;
	u64 rang = (u64)m->b;
	if (rang >= (u64)o->nents)
		return 0;
	struct ent *e = &o->ents[rang];
	memset(m->data, 0, 272);
	snprintf((char *)m->data, 256, "%s", e->name);
	u64 *t = (u64 *)(m->data + 256);
	t[0] = e->size;
	t[1] = e->attr;
	m->len = 272;
	return 1;
}

/* 12 fermer(a) */
static long op_close(struct msg *m)
{
	u64 num = (u64)m->a;
	if (num >= OUV_MAX || !ouverts[num].used)
		return E_INVAL;
	struct ouvert *o = &ouverts[num];
	u32 pid = m->pid;
	if (!pid || pid == SVC_PID_KERNEL) {
		ouvert_free(o);
		return 0;
	}
	if (!own_has(o, pid))
		return E_BADF;
	for (int i = 0; i < OWN_MAX; i++)
		if (o->owners[i] == pid)
			o->owners[i] = 0;
	if (!own_count(o))
		ouvert_free(o);
	return 0;
}

/* 19 info -> a octets, b libres, c octets par bloc, d FS_T_SMB ; données : étiquette */
static long op_info(struct msg *m)
{
	struct smb2_statvfs v;
	char sp[600];
	smb_path("", sp, sizeof sp);
	if (smb2_statvfs(smb2, sp, &v) == 0) {
		u64 bs = v.f_frsize ? v.f_frsize : v.f_bsize;
		m->a = (i64)(v.f_blocks * bs);
		m->b = (i64)(v.f_bavail * bs);
		m->c = (i64)bs;
	} else {
		m->a = 0;
		m->b = -1;
		m->c = 0;
	}
	m->d = FS_T_SMB;
	int n = snprintf((char *)m->data, 64, "//%s/%s", host, share);
	m->len = (u32)(n + 1);
	return 0;
}

int main(int argc, char **argv)
{
	/* « <volume> <source> » (argv[0] est peut-être le programme) */
	int s = -1;
	for (int i = 0; i < argc; i++)
		if (!strncmp(argv[i], "//", 2) || !strncasecmp(argv[i], "smb:", 4))
			s = i;
	if (s < 1) {
		fprintf(stderr, "smbfs : lancé par vfs (mount VOL: //hôte/partage smbfs), docs/SMB.md\n");
		return 1;
	}
	snprintf(volume, sizeof volume, "%s", argv[s - 1]);
	snprintf(source, sizeof source, "%s", argv[s]);
	char name[48];
	snprintf(name, sizeof name, "fs.%s", volume);
	for (char *p = name; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p += 32;
	canal = ksys(SYS_SVC_REGISTER, (long)name, SVF_ENDS | SVF_BIRTHS, 0, 0);
	if (canal < 0) {
		say("enregistrement de %s impossible (%ld)", name, canal);
		return 1;
	}
	for (;;) {
		long req = ksys(SYS_SVC_NEXT, canal, (long)&msg, 60000, 0);
		if (req < 0)
			return 1;
		if (!req) {
			if (smb2) /* (garde la session en vie) */
				smb2_echo(smb2);
			continue;
		}
		req_cour = req;
		u64 op = (u64)msg.op;
		if (op == SVC_OP_END) {
			fin_pid(msg.a);
			continue;
		}
		if (op == SVC_OP_FORK) {
			fork_pid(msg.a, msg.b);
			continue;
		}
		if (op == SVC_OP_BIRTH)
			continue;
		long r;
		if (op == FS_CLOSE) {
			r = op_close(&msg);
			if (!msg.pid) /* (avis du noyau : sans réponse) */
				continue;
		} else if (op == FS_BUFFER) {
			r = op_tampon(&msg);
		} else if (op == FS_SYNC) {
			r = 0;
		} else if ((r = connect_share()) == 0) {
			switch (op) {
			case FS_OPEN: r = op_open(&msg); break;
			case FS_STAT: r = op_stat(&msg); break;
			case FS_MKDIR: r = op_mkdir(&msg); break;
			case FS_UNLINK: r = op_unlink(&msg); break;
			case FS_UTIME: r = 0; break; /* (dates : gardées par le serveur) */
			case FS_RENAME: r = op_rename(&msg); break;
			case FS_READ: r = op_read(&msg); break;
			case FS_WRITE: r = op_write(&msg); break;
			case FS_READDIR: r = op_readdir(&msg); break;
			case FS_READ_BUF: r = op_rw_buf(&msg, 0); break;
			case FS_WRITE_BUF: r = op_rw_buf(&msg, 1); break;
			case FS_READ_DIRECT: r = op_read_direct(&msg); break;
			case FS_INFO: r = op_info(&msg); break;
			case FS_SEEK: r = op_seek(&msg); break;
			default: r = E_INVAL; break;
			}
			if (smb2 && smb2_get_fd(smb2) < 0) /* (coupé : refait à la requête suivante) */
				disconnect();
		}
		msg.op = r;
		ksys(SYS_SVC_REPLY, req, (long)&msg, 0, 0);
	}
}
