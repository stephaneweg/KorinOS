/* fs.c : les commandes de fichiers de smbd (docs/SMB.md) : CREATE, CLOSE, READ, WRITE, QUERY_DIRECTORY,
 * QUERY_INFO, SET_INFO... sur les fichiers de Korin (libposix) ou de Linux (essais).
 *
 * Les connexions sont en passthrough (main.c) : les listes de dossier, les infos et les IOCTL sont
 * encodées ici, octet par octet, selon MS-FSCC ; libsmb2 ne fait que les envelopper. Un gestionnaire
 * rend 0 (réponse normale), ou -1 avec un code NT (reply_error).
 *
 * Un fichier ouvert : une entrée de « files » (son numéro et une génération dans l'identifiant de
 * 16 octets) ; un dossier ouvert garde la liste lue à la première QUERY_DIRECTORY. Un identifiant
 * 0xFF..FF (requêtes composées : CREATE + QUERY_INFO + CLOSE) désigne le dernier fichier ouvert de
 * la connexion. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>

#include "smbd.h"

/* ---- MS-FSCC, MS-SMB2 */
#define FA_READONLY 0x01
#define FA_HIDDEN 0x02
#define FA_DIRECTORY 0x10
#define FA_ARCHIVE 0x20

#define DISP_SUPERSEDE 0
#define DISP_OPEN 1
#define DISP_CREATE 2
#define DISP_OPEN_IF 3
#define DISP_OVERWRITE 4
#define DISP_OVERWRITE_IF 5

#define OPT_DIRECTORY 0x01
#define OPT_NON_DIRECTORY 0x40
#define OPT_DELETE_ON_CLOSE 0x1000

#define ACC_WRITE_DATA 0x2
#define ACC_APPEND_DATA 0x4
#define ACC_DELETE 0x10000
#define ACC_MAXIMUM 0x02000000
#define ACC_GENERIC_ALL 0x10000000
#define ACC_GENERIC_WRITE 0x40000000

#define INFO_FILE 1
#define INFO_FS 2
#define INFO_SECURITY 3

#define FILE_DIRECTORY_INFO 1
#define FILE_FULL_DIRECTORY_INFO 2
#define FILE_BOTH_DIRECTORY_INFO 3
#define FILE_BASIC_INFO 4
#define FILE_STANDARD_INFO 5
#define FILE_INTERNAL_INFO 6
#define FILE_EA_INFO 7
#define FILE_ACCESS_INFO 8
#define FILE_NAME_INFO 9
#define FILE_RENAME_INFO 10
#define FILE_NAMES_INFO 12
#define FILE_DISPOSITION_INFO 13
#define FILE_POSITION_INFO 14
#define FILE_MODE_INFO 16
#define FILE_ALIGNMENT_INFO 17
#define FILE_ALL_INFO 18
#define FILE_ALLOCATION_INFO 19
#define FILE_END_OF_FILE_INFO 20
#define FILE_ALTERNATE_NAME_INFO 21
#define FILE_STREAM_INFO 22
#define FILE_COMPRESSION_INFO 28
#define FILE_NETWORK_OPEN_INFO 34
#define FILE_ATTRIBUTE_TAG_INFO 35
#define FILE_ID_BOTH_DIRECTORY_INFO 37
#define FILE_ID_FULL_DIRECTORY_INFO 38
#define FILE_ID_INFO 59
#define FILE_DISPOSITION_INFO_EX 64

#define FS_VOLUME_INFO 1
#define FS_SIZE_INFO 3
#define FS_DEVICE_INFO 4
#define FS_ATTRIBUTE_INFO 5
#define FS_FULL_SIZE_INFO 7
#define FS_SECTOR_SIZE_INFO 11

#define QD_RESTART 0x01
#define QD_SINGLE 0x02
#define QD_INDEX 0x04
#define QD_REOPEN 0x10

#define FSCTL_DFS_GET_REFERRALS 0x00060194
#define FSCTL_DFS_GET_REFERRALS_EX 0x000601B0

/* ---- fichiers ouverts */

struct dent {
	char *name;
	struct stat st;
};

struct fh {
	struct smb2_context *ctx; /* 0 : libre */
	uint32_t tid, gen;
	struct share *share;
	char rel[SMBD_PATH];  /* chemin dans le partage (« a/b », « » : la racine) */
	char full[SMBD_PATH]; /* chemin réel */
	int fd;               /* fichier : son descripteur ; dossier : -1 */
	int isdir, writable, delete_on_close;
	uint32_t access;
	uint64_t pos;        /* FilePositionInformation */
	uint64_t eof_later;  /* taille demandée plus grande (EndOfFile) : faite à la fermeture */
	struct dent *list;   /* dossier : la liste (QUERY_DIRECTORY), 0 : pas lue */
	int nlist, next;
	char pattern[256];
};

static struct fh files[SMBD_MAX_FILES];
static uint32_t generation = 1;

/* le dernier fichier ouvert par connexion (requêtes composées) */
static struct {
	struct smb2_context *ctx;
	int last;
} conns[64];

static void set_last(struct smb2_context *smb2, int i)
{
	int free_slot = -1;
	for (int k = 0; k < 64; k++) {
		if (conns[k].ctx == smb2) {
			conns[k].last = i;
			return;
		}
		if (!conns[k].ctx && free_slot < 0)
			free_slot = k;
	}
	if (free_slot >= 0) {
		conns[free_slot].ctx = smb2;
		conns[free_slot].last = i;
	}
}

static int get_last(struct smb2_context *smb2)
{
	for (int k = 0; k < 64; k++)
		if (conns[k].ctx == smb2)
			return conns[k].last;
	return -1;
}

static void put_id(smb2_file_id id, int i, uint32_t gen)
{
	memset(id, 0, SMB2_FD_SIZE);
	memcpy(id, &i, 4);
	memcpy(id + 4, &gen, 4);
	memcpy(id + 8, &i, 4);
	memcpy(id + 12, &gen, 4);
}

static struct fh *fh_of(struct smb2_context *smb2, const smb2_file_id id)
{
	static const uint8_t compound[SMB2_FD_SIZE] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
						       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	int i;
	uint32_t gen;
	if (!memcmp(id, compound, SMB2_FD_SIZE)) {
		i = get_last(smb2);
		log_dbg("identifiant de requête composée -> %d", i);
		if (i < 0 || i >= SMBD_MAX_FILES || files[i].ctx != smb2)
			return 0;
		struct share *csh = tree_share(smb2, files[i].tid);
		if (!csh)
			return 0;
		files[i].share = csh;
		return &files[i];
	}
	memcpy(&i, id, 4);
	memcpy(&gen, id + 4, 4);
	if (i < 0 || i >= SMBD_MAX_FILES) {
		log_dbg("identifiant inconnu (%d)", i);
		return 0;
	}
	struct fh *f = &files[i];
	if (f->ctx != smb2 || f->gen != gen) {
		log_dbg("identifiant périmé (%d, génération %u, ouverte %u)", i, gen, f->ctx ? f->gen : 0);
		return 0;
	}
	struct share *sh = tree_share(smb2, f->tid); /* (partage retiré par le volet : plus d'accès) */
	if (!sh)
		return 0;
	f->share = sh;
	return f;
}

static void list_free(struct fh *f)
{
	for (int k = 0; k < f->nlist; k++)
		free(f->list[k].name);
	free(f->list);
	f->list = 0;
	f->nlist = f->next = 0;
}

static void fh_release(struct fh *f)
{
	struct stat st;
	if (f->fd >= 0 && f->eof_later && !f->delete_on_close && fstat(f->fd, &st) == 0 &&
	    (uint64_t)st.st_size < f->eof_later)
		if (ftruncate(f->fd, (off_t)f->eof_later) < 0)
			log_dbg("taille de %s : %s", f->full, strerror(errno));
	if (f->fd >= 0)
		close(f->fd);
	f->fd = -1;
	list_free(f);
	if (f->delete_on_close) {
		int r = f->isdir ? rmdir(f->full) : unlink(f->full);
		log_dbg("effacé à la fermeture : %s (%d)", f->full, r);
	}
	f->ctx = 0;
}

void fs_close_tree(struct smb2_context *smb2, uint32_t tid)
{
	for (int i = 0; i < SMBD_MAX_FILES; i++)
		if (files[i].ctx == smb2 && files[i].tid == tid)
			fh_release(&files[i]);
}

void fs_close_all(struct smb2_context *smb2)
{
	for (int i = 0; i < SMBD_MAX_FILES; i++)
		if (files[i].ctx == smb2)
			fh_release(&files[i]);
	for (int k = 0; k < 64; k++)
		if (conns[k].ctx == smb2)
			conns[k].ctx = 0;
}

/* ---- petits outils */

static uint32_t status_of(int e)
{
	switch (e) {
	case ENOENT:
		return ST_OBJECT_NAME_NOT_FOUND;
	case ENOTDIR:
		return ST_OBJECT_PATH_NOT_FOUND;
	case EEXIST:
		return ST_OBJECT_NAME_COLLISION;
	case EACCES:
	case EPERM:
		return ST_ACCESS_DENIED;
	case EROFS:
		return ST_MEDIA_WRITE_PROTECTED;
	case ENOSPC:
		return ST_DISK_FULL;
	case EISDIR:
		return ST_FILE_IS_A_DIRECTORY;
	case ENOTEMPTY:
		return ST_DIRECTORY_NOT_EMPTY;
	case EBUSY:
		return ST_SHARING_VIOLATION;
	case ENAMETOOLONG:
	case EINVAL:
		return ST_OBJECT_NAME_INVALID;
	default:
		return ST_ACCESS_DENIED;
	}
}

static void w16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void w32(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> (8 * i));
}

static void w64(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t r32(const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t r64(const uint8_t *p)
{
	return r32(p) | (uint64_t)r32(p + 4) << 32;
}

/* UTF-8 -> UTF-16LE dans dst (place : max octets) -> octets écrits */
static size_t utf16(uint8_t *dst, size_t max, const char *s)
{
	size_t n = 0;
	const uint8_t *p = (const uint8_t *)s;
	while (*p) {
		uint32_t c = *p++;
		if (c >= 0xF0 && p[0] && p[1] && p[2]) {
			c = (c & 7) << 18 | (p[0] & 0x3F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F);
			p += 3;
		} else if (c >= 0xE0 && p[0] && p[1]) {
			c = (c & 15) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F);
			p += 2;
		} else if (c >= 0xC0 && p[0]) {
			c = (c & 31) << 6 | (p[0] & 0x3F);
			p += 1;
		}
		if (c >= 0x10000) {
			c -= 0x10000;
			if (n + 4 > max)
				break;
			w16(dst + n, (uint16_t)(0xD800 | c >> 10));
			w16(dst + n + 2, (uint16_t)(0xDC00 | (c & 0x3FF)));
			n += 4;
		} else {
			if (n + 2 > max)
				break;
			w16(dst + n, (uint16_t)c);
			n += 2;
		}
	}
	return n;
}

static size_t utf16_len(const char *s)
{
	static uint8_t tmp[4096];
	return utf16(tmp, sizeof tmp, s);
}

/* stat, et la racine d'un volume (« SD0:/ ») : fatfs n'a pas d'entrée de dossier pour elle, stat échoue ;
 * si opendir l'ouvre, c'est un dossier (daté du démarrage de smbd) */
static int sstat(const char *p, struct stat *st)
{
	static time_t started;
	if (!started)
		started = time(NULL);
	if (stat(p, st) == 0)
		return 0;
	int e = errno;
	DIR *d = opendir(p);
	if (!d) {
		errno = e;
		return -1;
	}
	closedir(d);
	memset(st, 0, sizeof *st);
	st->st_mode = S_IFDIR | 0755;
	st->st_nlink = 1;
	st->st_mtime = st->st_atime = st->st_ctime = started;
	return 0;
}

static uint64_t filetime(time_t t, long ns)
{
	if (t <= 0)
		return 0;
	return ((uint64_t)t + 11644473600ULL) * 10000000ULL + (uint64_t)ns / 100;
}

static int filetime_to_unix(uint64_t ft, struct timeval *tv)
{
	if (ft == 0 || ft == (uint64_t)-1 || ft == (uint64_t)-2 || ft < 116444736000000000ULL)
		return 0;
	uint64_t u = ft - 116444736000000000ULL;
	tv->tv_sec = (time_t)(u / 10000000ULL);
	tv->tv_usec = (long)((u % 10000000ULL) / 10);
	return 1;
}

#if defined(__linux__)
#define ST_MTIME_NS(st) ((st)->st_mtim.tv_nsec)
#define ST_ATIME_NS(st) ((st)->st_atim.tv_nsec)
#else
#define ST_MTIME_NS(st) 0L
#define ST_ATIME_NS(st) 0L
#endif

struct times {
	uint64_t create, access, write, change;
};

static struct times times_of(const struct stat *st)
{
	struct times t;
	t.write = filetime(st->st_mtime, ST_MTIME_NS(st));
	t.change = t.write;
	t.access = st->st_atime > 0 ? filetime(st->st_atime, ST_ATIME_NS(st)) : t.write;
	/* (pas de date de création sous POSIX : la plus ancienne connue) */
	t.create = st->st_ctime > 0 && st->st_ctime < st->st_mtime ? filetime(st->st_ctime, 0) : t.write;
	return t;
}

static uint32_t attrs_of(const struct stat *st, const char *name, const struct share *sh)
{
	uint32_t a = S_ISDIR(st->st_mode) ? FA_DIRECTORY : FA_ARCHIVE;
	if (name && name[0] == '.' && strcmp(name, ".") && strcmp(name, ".."))
		a |= FA_HIDDEN;
	if (sh && sh->readonly && !S_ISDIR(st->st_mode))
		a |= FA_READONLY;
	return a;
}

static uint64_t alloc_of(const struct stat *st)
{
	if (S_ISDIR(st->st_mode))
		return 0;
	return ((uint64_t)st->st_size + 4095) & ~(uint64_t)4095;
}

static uint64_t index_of(const struct stat *st, const char *full)
{
	if (st->st_ino)
		return (uint64_t)st->st_ino;
	uint64_t h = 1469598103934665603ULL; /* (Korin : pas de numéro de nœud) */
	for (const char *p = full; *p; p++)
		h = (h ^ (uint8_t)(*p | (*p >= 'A' && *p <= 'Z' ? 0x20 : 0))) * 1099511628211ULL;
	return h ? h : 1;
}

/* nom reçu (« \a\b », UTF-8) -> chemin dans le partage (« a/b ») ; -1 : nom refusé */
static int rel_path(const char *name, char *rel, size_t max)
{
	size_t n = 0;
	const char *p = name ? name : "";
	while (*p == '\\' || *p == '/')
		p++;
	while (*p) {
		const char *s = p;
		while (*p && *p != '\\' && *p != '/')
			p++;
		size_t len = (size_t)(p - s);
		while (*p == '\\' || *p == '/')
			p++;
		if (!len)
			continue;
		if ((len == 1 && s[0] == '.') || (len == 2 && s[0] == '.' && s[1] == '.'))
			return -1;
		if (n + len + 2 > max)
			return -1;
		if (n)
			rel[n++] = '/';
		memcpy(rel + n, s, len);
		n += len;
	}
	rel[n] = 0;
	/* le flux par défaut (« a.txt::$DATA ») : le fichier lui-même ; les autres flux : refusés */
	char *c = strchr(rel, ':');
	if (c) {
		if (!strcasecmp(c, "::$DATA"))
			*c = 0;
		else
			return -2;
	}
	return 0;
}

static void full_path(const struct share *sh, const char *rel, char *full, size_t max)
{
	if (!*rel)
		snprintf(full, max, "%s", sh->path);
	else if (sh->path[0] && sh->path[strlen(sh->path) - 1] == '/')
		snprintf(full, max, "%s%s", sh->path, rel);
	else
		snprintf(full, max, "%s/%s", sh->path, rel);
}

static int parent_exists(const char *full)
{
	char p[SMBD_PATH];
	snprintf(p, sizeof p, "%s", full);
	char *s = strrchr(p, '/');
	if (!s)
		return 1;
	if (s == p || s[-1] == ':')
		s[1] = 0;
	else
		*s = 0;
	struct stat st;
	return sstat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ---- CREATE, CLOSE */

static int create(struct smb2_context *smb2, struct smb2_create_request *req, struct smb2_create_reply *rep);

/* (un CREATE refusé : les requêtes composées qui le suivent n'ont pas de fichier, pas l'ancien) */
int fs_create(struct smb2_server *s, struct smb2_context *smb2, struct smb2_create_request *req,
	      struct smb2_create_reply *rep)
{
	(void)s;
	set_last(smb2, -1);
	int r = create(smb2, req, rep);
	if (r < 0)
		log_dbg("CREATE « %s » disp %u opts %#x acc %#x : refusé", req->name ? req->name : "",
			req->create_disposition, req->create_options, req->desired_access);
	return r;
}

static int create(struct smb2_context *smb2, struct smb2_create_request *req, struct smb2_create_reply *rep)
{
	struct tree *t = tree_of(smb2);
	if (!t)
		return reply_error(smb2, SMB2_CREATE, ST_INVALID_HANDLE);
	if (!t->share) /* IPC$ : pas de tubes nommés (srvsvc...) */
		return reply_error(smb2, SMB2_CREATE, ST_OBJECT_NAME_NOT_FOUND);
	struct share *sh = t->share;
	char rel[SMBD_PATH], full[SMBD_PATH];
	int r = rel_path(req->name, rel, sizeof rel);
	if (r == -2)
		return reply_error(smb2, SMB2_CREATE, ST_OBJECT_NAME_NOT_FOUND);
	if (r < 0)
		return reply_error(smb2, SMB2_CREATE, ST_OBJECT_NAME_INVALID);
	full_path(sh, rel, full, sizeof full);

	uint32_t disp = req->create_disposition, opts = req->create_options, acc = req->desired_access;
	int want_dir = (opts & OPT_DIRECTORY) != 0, want_file = (opts & OPT_NON_DIRECTORY) != 0;
	int want_write = (acc & (ACC_WRITE_DATA | ACC_APPEND_DATA | ACC_GENERIC_ALL | ACC_GENERIC_WRITE)) != 0;
	int creating = disp != DISP_OPEN;
	if (sh->readonly && (want_write || (acc & ACC_DELETE) || (opts & OPT_DELETE_ON_CLOSE) ||
			     disp == DISP_SUPERSEDE || disp == DISP_OVERWRITE || disp == DISP_OVERWRITE_IF ||
			     disp == DISP_CREATE)) {
		log_dbg("CREATE %s : partage en lecture seule", full);
		return reply_error(smb2, SMB2_CREATE, ST_ACCESS_DENIED);
	}

	struct stat st;
	int exists = sstat(full, &st) == 0;
	uint32_t action = 1; /* FILE_OPENED */
	int fd = -1, isdir = 0;
	if (exists) {
		isdir = S_ISDIR(st.st_mode);
		if (disp == DISP_CREATE)
			return reply_error(smb2, SMB2_CREATE, ST_OBJECT_NAME_COLLISION);
		if (isdir && want_file)
			return reply_error(smb2, SMB2_CREATE, ST_FILE_IS_A_DIRECTORY);
		if (!isdir && want_dir)
			return reply_error(smb2, SMB2_CREATE, ST_NOT_A_DIRECTORY);
		if (!isdir) {
			int trunc = disp == DISP_SUPERSEDE || disp == DISP_OVERWRITE || disp == DISP_OVERWRITE_IF;
			int rw = !sh->readonly && (want_write || trunc || (acc & ACC_MAXIMUM));
			fd = open(full, (rw ? O_RDWR : O_RDONLY) | (trunc ? O_TRUNC : 0));
			if (fd < 0 && rw && !want_write && !trunc) { /* (MAXIMUM_ALLOWED : la lecture suffit) */
				rw = 0;
				fd = open(full, O_RDONLY);
			}
			if (fd < 0)
				return reply_error(smb2, SMB2_CREATE, status_of(errno));
			if (trunc)
				action = disp == DISP_SUPERSEDE ? 0 : 3; /* FILE_SUPERSEDED, FILE_OVERWRITTEN */
			want_write = rw;
		}
	} else {
		if (!creating || disp == DISP_OVERWRITE)
			return reply_error(smb2, SMB2_CREATE,
					   parent_exists(full) ? ST_OBJECT_NAME_NOT_FOUND : ST_OBJECT_PATH_NOT_FOUND);
		if (!parent_exists(full))
			return reply_error(smb2, SMB2_CREATE, ST_OBJECT_PATH_NOT_FOUND);
		if (want_dir) {
			if (mkdir(full, 0777) < 0)
				return reply_error(smb2, SMB2_CREATE, status_of(errno));
			isdir = 1;
		} else {
			fd = open(full, O_RDWR | O_CREAT | O_EXCL, 0666);
			if (fd < 0)
				return reply_error(smb2, SMB2_CREATE, status_of(errno));
			want_write = 1;
		}
		action = 2; /* FILE_CREATED */
		log_dbg("créé : %s", full);
	}
	if (fd >= 0 ? fstat(fd, &st) < 0 : sstat(full, &st) < 0) {
		int e = errno;
		if (fd >= 0)
			close(fd);
		return reply_error(smb2, SMB2_CREATE, status_of(e));
	}

	int i = -1;
	for (int k = 0; k < SMBD_MAX_FILES && i < 0; k++)
		if (!files[k].ctx)
			i = k;
	if (i < 0) {
		if (fd >= 0)
			close(fd);
		log_msg("trop de fichiers ouverts (%d)", SMBD_MAX_FILES);
		return reply_error(smb2, SMB2_CREATE, ST_ACCESS_DENIED);
	}
	struct fh *f = &files[i];
	memset(f, 0, sizeof *f);
	f->ctx = smb2;
	f->tid = t->tid;
	f->gen = generation++;
	f->share = sh;
	snprintf(f->rel, sizeof f->rel, "%s", rel);
	snprintf(f->full, sizeof f->full, "%s", full);
	f->fd = fd;
	f->isdir = isdir;
	f->writable = want_write;
	f->access = acc;
	f->delete_on_close = (opts & OPT_DELETE_ON_CLOSE) != 0;
	set_last(smb2, i);

	struct times tm = times_of(&st);
	memset(rep, 0, sizeof *rep);
	rep->oplock_level = 0;
	rep->create_action = action;
	rep->creation_time = tm.create;
	rep->last_access_time = tm.access;
	rep->last_write_time = tm.write;
	rep->change_time = tm.change;
	rep->allocation_size = alloc_of(&st);
	rep->end_of_file = isdir ? 0 : (uint64_t)st.st_size;
	rep->file_attributes = attrs_of(&st, strrchr(full, '/') ? strrchr(full, '/') + 1 : full, sh);
	put_id(rep->file_id, i, f->gen);
	log_dbg("CREATE « %s » disp %u opts %#x acc %#x -> %d (%s)", rel, disp, opts, acc, i,
		isdir ? "dossier" : "fichier");
	return 0;
}

int fs_close(struct smb2_server *s, struct smb2_context *smb2, struct smb2_close_request *req,
	     struct smb2_close_reply *rep)
{
	(void)s;
	struct fh *f = fh_of(smb2, req->file_id);
	if (!f)
		return reply_error(smb2, SMB2_CLOSE, ST_INVALID_HANDLE);
	memset(rep, 0, sizeof *rep);
	struct stat st;
	if ((req->flags & 1) && (f->fd >= 0 ? fstat(f->fd, &st) : sstat(f->full, &st)) == 0) {
		struct times tm = times_of(&st); /* SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB */
		rep->flags = 1;
		rep->creation_time = tm.create;
		rep->last_access_time = tm.access;
		rep->last_write_time = tm.write;
		rep->change_time = tm.change;
		rep->allocation_size = alloc_of(&st);
		rep->end_of_file = f->isdir ? 0 : (uint64_t)st.st_size;
		rep->file_attributes = attrs_of(&st, 0, f->share);
	}
	log_dbg("CLOSE %s", f->rel);
	fh_release(f);
	return 0;
}

int fs_flush(struct smb2_server *s, struct smb2_context *smb2, struct smb2_flush_request *req)
{
	(void)s;
	struct fh *f = fh_of(smb2, req->file_id);
	if (!f)
		return reply_error(smb2, SMB2_FLUSH, ST_INVALID_HANDLE);
	if (f->fd >= 0)
		fsync(f->fd);
	return 0;
}

/* ---- READ, WRITE, LOCK */

int fs_read(struct smb2_server *s, struct smb2_context *smb2, struct smb2_read_request *req,
	    struct smb2_read_reply *rep)
{
	(void)s;
	struct fh *f = fh_of(smb2, req->file_id);
	if (!f)
		return reply_error(smb2, SMB2_READ, ST_INVALID_HANDLE);
	if (f->isdir)
		return reply_error(smb2, SMB2_READ, ST_INVALID_DEVICE_REQUEST);
	uint32_t len = req->length;
	if (len > 0x100000)
		len = 0x100000;
	uint8_t *buf = malloc(len ? len : 1); /* (rendu par libsmb2 avec la réponse) */
	if (!buf)
		return reply_error(smb2, SMB2_READ, ST_DISK_FULL);
	ssize_t n = pread(f->fd, buf, len, (off_t)req->offset);
	if (n < 0) {
		int e = errno;
		free(buf);
		return reply_error(smb2, SMB2_READ, status_of(e));
	}
	if (n == 0 || (uint32_t)n < req->minimum_count) {
		free(buf);
		return reply_error(smb2, SMB2_READ, ST_END_OF_FILE);
	}
	memset(rep, 0, sizeof *rep);
	rep->data = buf;
	rep->data_length = (uint32_t)n;
	f->pos = req->offset + (uint64_t)n;
	return 0;
}

int fs_write(struct smb2_server *s, struct smb2_context *smb2, struct smb2_write_request *req,
	     struct smb2_write_reply *rep)
{
	(void)s;
	struct fh *f = fh_of(smb2, req->file_id);
	if (!f)
		return reply_error(smb2, SMB2_WRITE, ST_INVALID_HANDLE);
	if (f->isdir)
		return reply_error(smb2, SMB2_WRITE, ST_INVALID_DEVICE_REQUEST);
	if (!f->writable || f->share->readonly)
		return reply_error(smb2, SMB2_WRITE, ST_ACCESS_DENIED);
	off_t off = (off_t)req->offset;
	if (req->offset == 0xFFFFFFFFFFFFFFFFULL) { /* (à la fin du fichier) */
		struct stat st;
		off = fstat(f->fd, &st) == 0 ? st.st_size : 0;
	}
	size_t done = 0;
	while (done < req->length) {
		ssize_t n = pwrite(f->fd, req->buf + done, req->length - done, off + (off_t)done);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			if (!done)
				return reply_error(smb2, SMB2_WRITE, n < 0 ? status_of(errno) : ST_DISK_FULL);
			break;
		}
		done += (size_t)n;
	}
	memset(rep, 0, sizeof *rep);
	rep->count = (uint32_t)done;
	f->pos = (uint64_t)off + done;
	return 0;
}

int fs_lock(struct smb2_server *s, struct smb2_context *smb2, struct smb2_lock_request *req)
{
	(void)s;
	/* (pas de verrous d'octets : accordés, un seul utilisateur) */
	if (!fh_of(smb2, req->file_id))
		return reply_error(smb2, SMB2_LOCK, ST_INVALID_HANDLE);
	return 0;
}

int fs_ioctl(struct smb2_server *s, struct smb2_context *smb2, struct smb2_ioctl_request *req,
	     struct smb2_ioctl_reply *rep)
{
	(void)s;
	(void)rep;
	switch (req->ctl_code) {
	case FSCTL_DFS_GET_REFERRALS:
	case FSCTL_DFS_GET_REFERRALS_EX:
		return reply_error(smb2, SMB2_IOCTL, ST_NOT_FOUND);
	default:
		log_dbg("IOCTL %#x : pas pris en charge", req->ctl_code);
		return reply_error(smb2, SMB2_IOCTL, ST_INVALID_DEVICE_REQUEST);
	}
}

int fs_change_notify(struct smb2_server *s, struct smb2_context *smb2, struct smb2_change_notify_request *req,
		     struct smb2_change_notify_reply *rep)
{
	(void)s;
	(void)req;
	(void)rep;
	return reply_error(smb2, SMB2_CHANGE_NOTIFY, ST_NOT_SUPPORTED);
}

/* ---- QUERY_DIRECTORY */

/* motif de Windows (« * », « *.txt », « a?c », jokers DOS < > ") contre un nom, sans la casse */
static int match(const char *p, const char *n)
{
	for (;;) {
		char c = *p;
		if (c == '<')
			c = '*';
		else if (c == '>')
			c = '?';
		else if (c == '"')
			c = '.';
		if (!c)
			return !*n;
		if (c == '*') {
			p++;
			if (!*p)
				return 1;
			for (; *n; n++)
				if (match(p, n))
					return 1;
			return match(p, n);
		}
		if (!*n)
			return c == '.' && !p[1]; /* (« a" » : l'extension vide) */
		char a = c, b = *n;
		if (a >= 'A' && a <= 'Z')
			a += 32;
		if (b >= 'A' && b <= 'Z')
			b += 32;
		if (c != '?' && a != b)
			return 0;
		p++;
		n++;
	}
}

static int list_read(struct fh *f)
{
	list_free(f);
	DIR *d = opendir(f->full);
	if (!d)
		return -1;
	int cap = 64;
	f->list = calloc((size_t)cap, sizeof *f->list);
	if (!f->list) {
		closedir(d);
		return -1;
	}
	/* « . » et « .. » d'abord, comme Windows et Samba */
	const char *dots[2] = {".", ".."};
	for (int k = 0; k < 2; k++) {
		f->list[f->nlist].name = strdup(dots[k]);
		if (sstat(f->full, &f->list[f->nlist].st) < 0)
			memset(&f->list[f->nlist].st, 0, sizeof(struct stat));
		f->list[f->nlist].st.st_mode = S_IFDIR | 0755;
		f->nlist++;
	}
	struct dirent *e;
	char p[SMBD_PATH * 2];
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		if (f->nlist == cap) {
			struct dent *n = realloc(f->list, (size_t)cap * 2 * sizeof *n);
			if (!n)
				break;
			f->list = n;
			cap *= 2;
		}
		snprintf(p, sizeof p, "%s/%s", f->full, e->d_name);
		struct dent *de = &f->list[f->nlist];
		if (sstat(p, &de->st) < 0)
			continue;
		de->name = strdup(e->d_name);
		if (de->name)
			f->nlist++;
	}
	closedir(d);
	f->next = 0;
	return 0;
}

/* une entrée de dossier de la classe cls dans buf (place : room) -> octets (sans l'alignement), 0 :
 * pas la place, -1 : classe inconnue */
static int dir_entry(uint8_t cls, const struct fh *f, const struct dent *de, uint8_t *buf, size_t room)
{
	size_t base;
	switch (cls) {
	case FILE_DIRECTORY_INFO:
		base = 64;
		break;
	case FILE_FULL_DIRECTORY_INFO:
		base = 68;
		break;
	case FILE_ID_FULL_DIRECTORY_INFO:
		base = 80;
		break;
	case FILE_BOTH_DIRECTORY_INFO:
		base = 94;
		break;
	case FILE_ID_BOTH_DIRECTORY_INFO:
		base = 104;
		break;
	case FILE_NAMES_INFO:
		base = 12;
		break;
	default:
		return -1;
	}
	size_t nl = utf16_len(de->name);
	if (base + nl > room)
		return 0;
	memset(buf, 0, base);
	utf16(buf + base, nl, de->name);
	if (cls == FILE_NAMES_INFO) {
		w32(buf + 8, (uint32_t)nl);
		return (int)(base + nl);
	}
	struct times tm = times_of(&de->st);
	w64(buf + 8, tm.create);
	w64(buf + 16, tm.access);
	w64(buf + 24, tm.write);
	w64(buf + 32, tm.change);
	w64(buf + 40, S_ISDIR(de->st.st_mode) ? 0 : (uint64_t)de->st.st_size);
	w64(buf + 48, alloc_of(&de->st));
	w32(buf + 56, attrs_of(&de->st, de->name, f->share));
	w32(buf + 60, (uint32_t)nl);
	char full[SMBD_PATH * 2];
	snprintf(full, sizeof full, "%s/%s", f->full, de->name);
	uint64_t id = index_of(&de->st, full);
	if (cls == FILE_ID_FULL_DIRECTORY_INFO)
		w64(buf + 72, id);
	if (cls == FILE_ID_BOTH_DIRECTORY_INFO)
		w64(buf + 96, id);
	/* (EaSize 0 ; pas de nom court 8.3) */
	return (int)(base + nl);
}

int fs_query_directory(struct smb2_server *s, struct smb2_context *smb2, struct smb2_query_directory_request *req,
		       struct smb2_query_directory_reply *rep)
{
	(void)s;
	struct fh *f = fh_of(smb2, req->file_id);
	if (!f)
		return reply_error(smb2, SMB2_QUERY_DIRECTORY, ST_INVALID_HANDLE);
	if (!f->isdir)
		return reply_error(smb2, SMB2_QUERY_DIRECTORY, ST_INVALID_PARAMETER);
	const char *pat = req->name && req->name[0] ? req->name : "*";
	int restart = (req->flags & (QD_RESTART | QD_REOPEN)) || !f->list || strcmp(pat, f->pattern);
	if (restart) {
		if (list_read(f) < 0)
			return reply_error(smb2, SMB2_QUERY_DIRECTORY, status_of(errno));
		snprintf(f->pattern, sizeof f->pattern, "%s", pat);
	}
	size_t room = req->output_buffer_length;
	if (room > 0x100000)
		room = 0x100000;
	uint8_t *buf = calloc(1, room + 8);
	if (!buf)
		return reply_error(smb2, SMB2_QUERY_DIRECTORY, ST_DISK_FULL);
	size_t used = 0, prev = (size_t)-1;
	int count = 0, any_match = 0;
	while (f->next < f->nlist) {
		struct dent *de = &f->list[f->next];
		if (!match(pat, de->name)) {
			f->next++;
			continue;
		}
		any_match = 1;
		size_t at = (used + 7) & ~(size_t)7;
		if (at >= room)
			break;
		int n = dir_entry(req->file_information_class, f, de, buf + at, room - at);
		if (n < 0) {
			free(buf);
			return reply_error(smb2, SMB2_QUERY_DIRECTORY, ST_INVALID_INFO_CLASS);
		}
		if (n == 0)
			break;
		if (prev != (size_t)-1)
			w32(buf + prev, (uint32_t)(at - prev)); /* NextEntryOffset de la précédente */
		prev = at;
		used = at + (size_t)n;
		count++;
		f->next++;
		if (req->flags & QD_SINGLE)
			break;
	}
	if (!count) {
		free(buf);
		if (f->next < f->nlist) /* (la première entrée ne tient pas) */
			return reply_error(smb2, SMB2_QUERY_DIRECTORY, ST_INFO_LENGTH_MISMATCH);
		return reply_error(smb2, SMB2_QUERY_DIRECTORY,
				   restart && !any_match ? ST_NO_SUCH_FILE : ST_NO_MORE_FILES);
	}
	memset(rep, 0, sizeof *rep);
	rep->output_buffer = buf; /* (copié par libsmb2 : rendu ici après) */
	rep->output_buffer_length = (uint32_t)used;
	log_dbg("QUERY_DIRECTORY %s classe %u « %s » : %d entrées, %zu octets", f->rel,
		req->file_information_class, pat, count, used);
	/* libsmb2 copie le tampon dans la réponse pendant smb2_cmd_query_directory_reply_async, après notre
	 * retour : on le garde jusqu'à la requête suivante */
	static uint8_t *pending;
	free(pending);
	pending = buf;
	return 0;
}

/* ---- QUERY_INFO */

static uint8_t *qi_buf;

static int fs_space(const struct fh *f, uint64_t *total, uint64_t *avail, uint32_t *bsize)
{
	struct statvfs v;
	if (statvfs(f->share->path, &v) == 0 && v.f_blocks) {
		uint64_t bs = v.f_frsize ? v.f_frsize : v.f_bsize;
		*bsize = (uint32_t)(bs ? bs : 4096);
		*total = (uint64_t)v.f_blocks;
		*avail = (uint64_t)v.f_bavail;
		return 0;
	}
	*bsize = 4096;
	*total = 1ULL << 24;
	*avail = 1ULL << 23;
	return 0;
}

/* le descripteur de sécurité : Tout le monde (S-1-1-0), contrôle total (lecture seule : lecture) */
static size_t security_desc(uint8_t *b, uint32_t which, int readonly, int isdir)
{
	static const uint8_t everyone[12] = {1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
	size_t n = 20;
	memset(b, 0, 20);
	b[0] = 1;
	uint16_t control = 0x8000; /* SE_SELF_RELATIVE */
	if (which & 1) {
		w32(b + 4, (uint32_t)n);
		memcpy(b + n, everyone, 12);
		n += 12;
	}
	if (which & 2) {
		w32(b + 8, (uint32_t)n);
		memcpy(b + n, everyone, 12);
		n += 12;
	}
	if (which & 4) {
		control |= 0x0004; /* SE_DACL_PRESENT */
		w32(b + 16, (uint32_t)n);
		uint8_t *acl = b + n;
		acl[0] = 2;
		acl[1] = 0;
		w16(acl + 2, 8 + 20);
		w16(acl + 4, 1);
		w16(acl + 6, 0);
		uint8_t *ace = acl + 8;
		ace[0] = 0;                 /* ACCESS_ALLOWED_ACE_TYPE */
		ace[1] = isdir ? 0x03 : 0;  /* OBJECT_INHERIT | CONTAINER_INHERIT */
		w16(ace + 2, 20);
		w32(ace + 4, readonly ? 0x001200A9 : 0x001F01FF);
		memcpy(ace + 8, everyone, 12);
		n += 28;
	}
	w16(b + 2, control);
	return n;
}

int fs_query_info(struct smb2_server *s, struct smb2_context *smb2, struct smb2_query_info_request *req,
		  struct smb2_query_info_reply *rep)
{
	(void)s;
	struct fh *f = fh_of(smb2, req->file_id);
	if (!f)
		return reply_error(smb2, SMB2_QUERY_INFO, ST_INVALID_HANDLE);
	struct stat st;
	if ((f->fd >= 0 ? fstat(f->fd, &st) : sstat(f->full, &st)) < 0)
		return reply_error(smb2, SMB2_QUERY_INFO, status_of(errno));
	free(qi_buf);
	qi_buf = calloc(1, 4096);
	if (!qi_buf)
		return reply_error(smb2, SMB2_QUERY_INFO, ST_DISK_FULL);
	uint8_t *b = qi_buf;
	size_t n = 0;
	struct times tm = times_of(&st);
	const char *base = strrchr(f->full, '/') ? strrchr(f->full, '/') + 1 : f->full;
	uint32_t attrs = attrs_of(&st, f->rel[0] ? base : 0, f->share);
	uint64_t eof = f->isdir ? 0 : (uint64_t)st.st_size, alloc = alloc_of(&st);
	if (eof < f->eof_later) /* (taille demandée, pas encore écrite) */
		eof = f->eof_later;
	char winname[SMBD_PATH + 2]; /* « \a\b » */
	snprintf(winname, sizeof winname, "\\%s", f->rel);
	for (char *p = winname; *p; p++)
		if (*p == '/')
			*p = '\\';

	if (req->info_type == INFO_FILE) {
		switch (req->file_info_class) {
		case FILE_BASIC_INFO:
			w64(b, tm.create);
			w64(b + 8, tm.access);
			w64(b + 16, tm.write);
			w64(b + 24, tm.change);
			w32(b + 32, attrs);
			n = 40;
			break;
		case FILE_STANDARD_INFO:
			w64(b, alloc);
			w64(b + 8, eof);
			w32(b + 16, 1);
			b[20] = (uint8_t)f->delete_on_close;
			b[21] = (uint8_t)f->isdir;
			n = 24;
			break;
		case FILE_INTERNAL_INFO:
			w64(b, index_of(&st, f->full));
			n = 8;
			break;
		case FILE_EA_INFO:
			n = 4;
			break;
		case FILE_ACCESS_INFO:
			w32(b, f->share->readonly ? 0x001200A9 : 0x001F01FF);
			n = 4;
			break;
		case FILE_POSITION_INFO:
			w64(b, f->pos);
			n = 8;
			break;
		case FILE_MODE_INFO:
		case FILE_ALIGNMENT_INFO:
			n = 4;
			break;
		case FILE_NAME_INFO: {
			size_t nl = utf16(b + 4, 4096 - 4, winname);
			w32(b, (uint32_t)nl);
			n = 4 + nl;
			break;
		}
		case FILE_ALL_INFO: {
			w64(b, tm.create);
			w64(b + 8, tm.access);
			w64(b + 16, tm.write);
			w64(b + 24, tm.change);
			w32(b + 32, attrs);
			w64(b + 40, alloc);
			w64(b + 48, eof);
			w32(b + 56, 1);
			b[60] = (uint8_t)f->delete_on_close;
			b[61] = (uint8_t)f->isdir;
			w64(b + 64, index_of(&st, f->full));
			/* EaSize 72, AccessFlags 76, Position 80, Mode 88, Alignment 92 */
			w32(b + 76, f->share->readonly ? 0x001200A9 : 0x001F01FF);
			w64(b + 80, f->pos);
			size_t nl = utf16(b + 100, 4096 - 100, winname);
			w32(b + 96, (uint32_t)nl);
			n = 100 + nl;
			break;
		}
		case FILE_NETWORK_OPEN_INFO:
			w64(b, tm.create);
			w64(b + 8, tm.access);
			w64(b + 16, tm.write);
			w64(b + 24, tm.change);
			w64(b + 32, alloc);
			w64(b + 40, eof);
			w32(b + 48, attrs);
			n = 56;
			break;
		case FILE_ATTRIBUTE_TAG_INFO:
			w32(b, attrs);
			n = 8;
			break;
		case FILE_STREAM_INFO:
			if (f->isdir)
				return reply_error(smb2, SMB2_QUERY_INFO, ST_INVALID_PARAMETER);
			w32(b, 0);
			w32(b + 4, 14);
			w64(b + 8, eof);
			w64(b + 16, alloc);
			utf16(b + 24, 14, "::$DATA");
			n = 38;
			break;
		case FILE_COMPRESSION_INFO:
			w64(b, eof);
			n = 16;
			break;
		case FILE_ID_INFO:
			w64(b, 0x4B6F72696EULL); /* (numéro du volume) */
			w64(b + 8, index_of(&st, f->full));
			n = 24;
			break;
		case FILE_ALTERNATE_NAME_INFO:
			return reply_error(smb2, SMB2_QUERY_INFO, ST_OBJECT_NAME_NOT_FOUND);
		default:
			log_dbg("QUERY_INFO fichier classe %u : pas pris en charge", req->file_info_class);
			return reply_error(smb2, SMB2_QUERY_INFO, ST_INVALID_INFO_CLASS);
		}
	} else if (req->info_type == INFO_FS) {
		uint64_t total, avail;
		uint32_t bsize;
		switch (req->file_info_class) {
		case FS_VOLUME_INFO: {
			w64(b, 0);
			w32(b + 8, 0x4B524E31); /* numéro de série */
			size_t nl = utf16(b + 18, 4096 - 18, conf.label);
			w32(b + 12, (uint32_t)nl);
			n = 18 + nl;
			break;
		}
		case FS_SIZE_INFO:
			fs_space(f, &total, &avail, &bsize);
			w64(b, total);
			w64(b + 8, avail);
			w32(b + 16, bsize / 512 ? bsize / 512 : 1);
			w32(b + 20, 512);
			n = 24;
			break;
		case FS_FULL_SIZE_INFO:
			fs_space(f, &total, &avail, &bsize);
			w64(b, total);
			w64(b + 8, avail);
			w64(b + 16, avail);
			w32(b + 24, bsize / 512 ? bsize / 512 : 1);
			w32(b + 28, 512);
			n = 32;
			break;
		case FS_DEVICE_INFO:
			w32(b, 7); /* FILE_DEVICE_DISK */
			w32(b + 4, f->share->readonly ? 0x2 : 0); /* FILE_READ_ONLY_DEVICE */
			n = 8;
			break;
		case FS_ATTRIBUTE_INFO: {
			/* FILE_CASE_PRESERVED_NAMES | FILE_UNICODE_ON_DISK (pas sensible à la casse, comme FAT) */
			w32(b, 0x2 | 0x4 | (f->share->readonly ? 0x00080000 : 0));
			w32(b + 4, 255);
			size_t nl = utf16(b + 12, 64, "NTFS");
			w32(b + 8, (uint32_t)nl);
			n = 12 + nl;
			break;
		}
		case FS_SECTOR_SIZE_INFO:
			w32(b, 512);
			w32(b + 4, 512);
			w32(b + 8, 512);
			w32(b + 12, 512);
			n = 28;
			break;
		default:
			log_dbg("QUERY_INFO volume classe %u : pas pris en charge", req->file_info_class);
			return reply_error(smb2, SMB2_QUERY_INFO, ST_INVALID_INFO_CLASS);
		}
	} else if (req->info_type == INFO_SECURITY) {
		n = security_desc(b, req->additional_information ? req->additional_information : 7,
				  f->share->readonly, f->isdir);
		if (n > req->output_buffer_length) {
			/* STATUS_BUFFER_TOO_SMALL et la taille voulue (MS-SMB2 3.3.5.20.3) */
			struct smb2_error_reply err;
			uint32_t need = (uint32_t)n;
			memset(&err, 0, sizeof err);
			err.byte_count = 4;
			err.error_data = (uint8_t *)&need;
			struct smb2_pdu *pdu = smb2_cmd_error_reply_async(smb2, &err, SMB2_QUERY_INFO,
									  ST_BUFFER_TOO_SMALL, NULL, NULL);
			if (pdu)
				smb2_queue_pdu(smb2, pdu);
			return 1;
		}
	} else {
		return reply_error(smb2, SMB2_QUERY_INFO, ST_NOT_SUPPORTED);
	}
	if (n > req->output_buffer_length)
		return reply_error(smb2, SMB2_QUERY_INFO, ST_INFO_LENGTH_MISMATCH);
	if (!n) { /* (libsmb2 : une réponse vide serait NOT_SUPPORTED) */
		n = 0;
	}
	memset(rep, 0, sizeof *rep);
	rep->output_buffer = b; /* (copié par libsmb2 ; qi_buf rendu à la requête suivante) */
	rep->output_buffer_length = (uint32_t)n;
	log_dbg("QUERY_INFO %s type %u classe %u : %zu octets", f->rel, req->info_type, req->file_info_class, n);
	return 0;
}

/* ---- SET_INFO */

static int do_rename(struct smb2_context *smb2, struct fh *f, const uint8_t *in, uint32_t len)
{
	if (len < 20)
		return reply_error(smb2, SMB2_SET_INFO, ST_INVALID_PARAMETER);
	int replace = in[0] != 0;
	uint32_t nl = r32(in + 16);
	if (20 + (size_t)nl > len)
		return reply_error(smb2, SMB2_SET_INFO, ST_INVALID_PARAMETER);
	const char *name = smb2_utf16_to_utf8((const uint16_t *)(in + 20), nl / 2);
	if (!name)
		return reply_error(smb2, SMB2_SET_INFO, ST_OBJECT_NAME_INVALID);
	char rel[SMBD_PATH], full[SMBD_PATH];
	int r = rel_path(name, rel, sizeof rel);
	free((void *)name);
	if (r < 0 || !rel[0])
		return reply_error(smb2, SMB2_SET_INFO, ST_OBJECT_NAME_INVALID);
	full_path(f->share, rel, full, sizeof full);
	struct stat st;
	if (strcasecmp(full, f->full) && sstat(full, &st) == 0) {
		if (!replace)
			return reply_error(smb2, SMB2_SET_INFO, ST_OBJECT_NAME_COLLISION);
		if (S_ISDIR(st.st_mode))
			return reply_error(smb2, SMB2_SET_INFO, ST_ACCESS_DENIED);
		unlink(full);
	}
	if (rename(f->full, full) < 0)
		return reply_error(smb2, SMB2_SET_INFO, status_of(errno));
	log_dbg("renommé : %s -> %s", f->full, full);
	snprintf(f->rel, sizeof f->rel, "%s", rel);
	snprintf(f->full, sizeof f->full, "%s", full);
	return 0;
}

int fs_set_info(struct smb2_server *s, struct smb2_context *smb2, struct smb2_set_info_request *req)
{
	(void)s;
	struct fh *f = fh_of(smb2, req->file_id);
	if (!f)
		return reply_error(smb2, SMB2_SET_INFO, ST_INVALID_HANDLE);
	const uint8_t *in = req->input_data;
	uint32_t len = req->buffer_length;
	log_dbg("SET_INFO %s type %u classe %u (%u octets)", f->rel, req->info_type, req->file_info_class, len);
	if (req->info_type == INFO_SECURITY) /* (les droits ne changent pas : accepté) */
		return 0;
	if (req->info_type != INFO_FILE)
		return reply_error(smb2, SMB2_SET_INFO, ST_NOT_SUPPORTED);
	if (f->share->readonly)
		return reply_error(smb2, SMB2_SET_INFO, ST_ACCESS_DENIED);
	switch (req->file_info_class) {
	case FILE_BASIC_INFO: {
		if (len < 36)
			return reply_error(smb2, SMB2_SET_INFO, ST_INVALID_PARAMETER);
		struct timeval tv[2];
		struct stat st;
		int a = filetime_to_unix(r64(in + 8), &tv[0]);
		int w = filetime_to_unix(r64(in + 16), &tv[1]);
		if ((a || w) && sstat(f->full, &st) == 0) {
			if (!a) {
				tv[0].tv_sec = st.st_atime;
				tv[0].tv_usec = 0;
			}
			if (!w) {
				tv[1].tv_sec = st.st_mtime;
				tv[1].tv_usec = 0;
			}
			if (utimes(f->full, tv) < 0)
				log_dbg("utimes %s : %s", f->full, strerror(errno));
		}
		return 0; /* (attributs : pas gardés) */
	}
	case FILE_RENAME_INFO:
		return do_rename(smb2, f, in, len);
	case FILE_DISPOSITION_INFO:
		if (len < 1)
			return reply_error(smb2, SMB2_SET_INFO, ST_INVALID_PARAMETER);
		f->delete_on_close = in[0] != 0;
		break;
	case FILE_DISPOSITION_INFO_EX:
		if (len < 4)
			return reply_error(smb2, SMB2_SET_INFO, ST_INVALID_PARAMETER);
		f->delete_on_close = (r32(in) & 1) != 0;
		break;
	case FILE_END_OF_FILE_INFO:
	case FILE_ALLOCATION_INFO: {
		if (len < 8 || f->isdir || f->fd < 0)
			return reply_error(smb2, SMB2_SET_INFO, ST_INVALID_PARAMETER);
		uint64_t size = r64(in);
		struct stat st;
		if (fstat(f->fd, &st) < 0)
			return reply_error(smb2, SMB2_SET_INFO, status_of(errno));
		if (size >= (uint64_t)st.st_size) {
			/* plus grand : Windows fixe la taille finale avant d'écrire une copie ; l'agrandir ici
			 * remplirait de zéros tout le fichier (lent sur FAT) avant les données : fait à la
			 * fermeture si les écritures ne l'ont pas atteinte (réserver : rien) */
			if (req->file_info_class == FILE_END_OF_FILE_INFO)
				f->eof_later = size;
			return 0;
		}
		f->eof_later = 0;
		if (ftruncate(f->fd, (off_t)size) < 0)
			return reply_error(smb2, SMB2_SET_INFO, status_of(errno));
		return 0;
	}
	case FILE_POSITION_INFO:
		if (len >= 8)
			f->pos = r64(in);
		return 0;
	case FILE_MODE_INFO:
		return 0;
	default:
		log_dbg("SET_INFO classe %u : pas pris en charge", req->file_info_class);
		return reply_error(smb2, SMB2_SET_INFO, ST_NOT_SUPPORTED);
	}
	/* effacement demandé : un dossier doit être vide */
	if (f->delete_on_close && f->isdir) {
		DIR *d = opendir(f->full);
		struct dirent *e;
		int empty = 1;
		while (d && (e = readdir(d)))
			if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
				empty = 0;
		if (d)
			closedir(d);
		if (!empty) {
			f->delete_on_close = 0;
			return reply_error(smb2, SMB2_SET_INFO, ST_DIRECTORY_NOT_EMPTY);
		}
	}
	return 0;
}
