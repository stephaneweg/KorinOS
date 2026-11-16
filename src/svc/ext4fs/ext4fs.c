/* ext4fs - service de fichiers ext4 (et ext2, ext3), en lecture seule pour l'instant (docs/etude/SYSTEME-FICHIERS.md,
 * docs/etude/INSTALLATEUR.md) : un volume monté par vfs (« mount /media/data blk.nvme0p3 ext4fs »), lu par
 * src/lib/ext4/ext4r.c. Il s'enregistre sous « fs.<volume> » et répond au protocole de src/inc/fs.inc, comme
 * fatfs et ramfs (propriétaires et tampons : fsowner.c).
 *  * Les blocs du volume passent par un cache (tampon partagé avec le service bloc, CACHE octets, une place
 *    par bloc : bloc modulo le nombre de places).
 *  * Lecture seule : ouvrir pour écrire, mkdir, unlink, rename, utime -> E_ROFS. L'écriture (et le journal)
 *    viendra ensuite, jugée par e2fsck.
 *  * Les liens symboliques sont suivis (Korin n'en a pas) : un lien vers un dossier se voit comme un dossier.
 *  * Les noms : la casse exacte, sinon sans la casse ASCII (les habitudes du FAT). */
#include "fsowner.h"
#include "../../lib/ext4/ext4r.h"

#define OUV_MAX 64
#define CACHE (1 << 20) /* octets du cache des blocs */
#define E_ROFS -30
#define BLK_INFO 1
#define BLK_ATTACH 2
#define BLK_READ 3
#define PATH_OUV 256

struct ouvert {
	u64 owner; /* 0 : entrée libre */
	i64 pos;
	u64 dpos, drang; /* (dossier) la position de l'entrée de rang drang */
	struct ext4r_inode in;
	char path[PATH_OUV]; /* (dossier) son chemin : les liens de ses entrées */
};

static char nom_svc[32], nom_blk[32];
static struct msg msg __attribute__((aligned(16)));
static struct msg msg_blk __attribute__((aligned(16)));
static long canal_blk, req_cour;
static struct ouvert ouverts[OUV_MAX];
static struct ext4r fs;
static u8 *cache;
static u64 *tags; /* bloc + 1 par place (0 : vide) */
static u64 places, cache_bs;
static u64 secteurs; /* du périphérique */

/* ---- périphérique bloc et cache */

static long blk_request(long op, u64 sector, u64 count, void *addr)
{
	msg_blk.op = op;
	msg_blk.a = sector;
	msg_blk.b = count;
	msg_blk.c = (i64)addr;
	msg_blk.len = 0;
	return korin_sys2(SYS_SVC_CALL, canal_blk, &msg_blk);
}

static const uint8_t *rd(void *ctx, uint64_t block)
{
	(void)ctx;
	u64 bs = fs.bs;
	if (bs != cache_bs) { /* (le superbloc lu en blocs de 1 Kio, puis la vraie taille) */
		places = CACHE / bs;
		memset(tags, 0, (CACHE / 1024) * sizeof *tags);
		cache_bs = bs;
	}
	u64 spb = bs / 512, sector = block * spb;
	if (sector + spb > secteurs)
		return 0;
	u64 slot = block % places;
	u8 *p = cache + slot * bs;
	if (tags[slot] == block + 1)
		return p;
	tags[slot] = 0;
	if (blk_request(BLK_READ, sector, spb, p))
		return 0;
	tags[slot] = block + 1;
	return p;
}

/* ---- fichiers ouverts */

static long kerr(long r)
{
	if (r == -2)
		return E_NOENT;
	if (r == -20)
		return E_NOTDIR;
	if (r == -22)
		return E_INVAL;
	return r < 0 ? E_IO : r;
}

static u64 attr_de(const struct ext4r_inode *in)
{
	return EXT4R_ISDIR(in->mode) ? ATTR_DIR : ATTR_FILE;
}

static long ouvert_de(struct msg *m)
{
	u64 num = m->a;
	if (num >= OUV_MAX || !ouverts[num].owner)
		return E_INVAL;
	long r = fo_access(m, num);
	return r ? r : (long)num;
}

static long fichier_de(struct msg *m)
{
	long e = ouvert_de(m);
	if (e >= 0 && EXT4R_ISDIR(ouverts[e].in.mode))
		return E_INVAL;
	return e;
}

static void ouvert_liberer(long num)
{
	ouverts[num].owner = 0;
}

static void fin_pid(long pid)
{
	fo_own_end(pid, ouvert_liberer);
	tampons_fin(pid);
}

/* ---- opérations */

/* 1 ouvrir(a drapeaux, chemin) -> b numéro, c attributs, d taille */
static long op_ouvrir(struct msg *m)
{
	open_extras_take(m);
	const char *path = (const char *)m->data;
	struct ext4r_inode in;
	long r = ext4r_lookup(&fs, path, &in);
	if (m->a & (O_WRITE | O_CREATE | O_TRUNC | O_APPEND))
		return r == 0 && EXT4R_ISDIR(in.mode) ? E_ISDIR : E_ROFS;
	if (r)
		return kerr(r);
	long e = -1;
	for (long i = 0; i < OUV_MAX && e < 0; i++)
		if (!ouverts[i].owner)
			e = i;
	if (e < 0)
		return E_MFILE;
	long owner = proprietaire(m);
	fo_own_clear(e);
	if (!fo_own_add(e, owner))
		return E_MFILE;
	struct ouvert *o = &ouverts[e];
	o->owner = owner;
	o->pos = 0;
	o->dpos = o->drang = 0;
	o->in = in;
	u64 n = strlen(path);
	if (n >= PATH_OUV)
		n = PATH_OUV - 1;
	memcpy(o->path, path, n);
	o->path[n] = 0;
	m->b = e;
	m->c = attr_de(&in);
	m->d = EXT4R_ISDIR(in.mode) ? 0 : in.size;
	open_extras_put(m);
	return 0;
}

/* 2 stat(chemin) -> b taille, c attributs, d date */
static long op_stat(struct msg *m)
{
	struct ext4r_inode in;
	long r = ext4r_lookup(&fs, (const char *)m->data, &in);
	if (r)
		return kerr(r);
	m->b = EXT4R_ISDIR(in.mode) ? 0 : in.size;
	m->c = attr_de(&in);
	m->d = in.mtime;
	return 0;
}

static long op_lecture_seule(struct msg *m)
{
	(void)m;
	return E_ROFS;
}

static i64 position_lire(struct msg *m, long e)
{
	return m->b == FS_POS_CUR ? ouverts[e].pos : m->b;
}

static void position_apres(struct msg *m, long e, long done, i64 pos)
{
	if (done < 0)
		return;
	pos += done;
	m->d = pos;
	if (m->b == FS_POS_CUR)
		ouverts[e].pos = pos;
}

static long lire(long e, i64 pos, void *dst, u64 n)
{
	if (pos < 0)
		return E_INVAL;
	long r = ext4r_read(&fs, &ouverts[e].in, (u64)pos, dst, n);
	return r < 0 ? E_IO : r;
}

/* 10 lire(a numéro, b position, c octets) -> octets (données), d position après */
static long op_lire(struct msg *m)
{
	long e = fichier_de(m);
	if (e < 0)
		return e;
	i64 pos = position_lire(m, e);
	u64 n = (u64)m->c > FS_DATA_MAX ? FS_DATA_MAX : (u64)m->c;
	long r = lire(e, pos, m->data, n);
	m->len = r > 0 ? (u32)r : 0;
	position_apres(m, e, r, pos);
	return r;
}

/* 15 lire_t(a numéro, b position, c octets) : au début du tampon de l'appelant */
static long op_lire_t(struct msg *m)
{
	long e = fichier_de(m);
	if (e < 0)
		return e;
	u8 *buf = tampon_de(m);
	if (!buf || (u64)m->c > FS_BUFFER_SIZE)
		return E_INVAL;
	i64 pos = position_lire(m, e);
	long r = lire(e, pos, buf, m->c);
	position_apres(m, e, r, pos);
	return r;
}

/* 17 lire_d(a numéro, b position, c octets, d adresse) : dans la mémoire de l'appelant */
static long op_lire_d(struct msg *m)
{
	long e = fichier_de(m);
	if (e < 0)
		return e;
	if ((u64)m->c > FS_DIRECT_MAX)
		return E_INVAL;
	long va = korin_sys4(SYS_SVC_MAP_CALLER, req_cour, m->d, m->c, MC_WRITE);
	if (va < 0)
		return va;
	i64 pos = position_lire(m, e);
	long r = lire(e, pos, (void *)va, m->c); /* (démappé par SVC_REPLY) */
	position_apres(m, e, r, pos);
	return r;
}

/* 26 position(a numéro, b décalage, c origine) -> position */
static long op_position(struct msg *m)
{
	long e = fichier_de(m);
	if (e < 0)
		return e;
	u64 org = m->c;
	if (org > 2)
		return E_INVAL;
	i64 base = org == 1 ? ouverts[e].pos : org == 2 ? (i64)ouverts[e].in.size : 0;
	i64 p = (i64)((u64)base + (u64)m->b);
	if (p < 0)
		return E_INVAL;
	ouverts[e].pos = p;
	m->b = p;
	return p;
}

/* 13 lire_rep(a numéro, b rang) -> 1 (données : nom[256], taille, attributs), 0 en fin */
static long op_lire_rep(struct msg *m)
{
	long e = ouvert_de(m);
	if (e < 0)
		return e;
	struct ouvert *o = &ouverts[e];
	if (!EXT4R_ISDIR(o->in.mode))
		return E_INVAL;
	u64 rang = m->b;
	if (rang < o->drang) { /* (en arrière : depuis le début) */
		o->dpos = o->drang = 0;
	}
	static char name[256];
	uint32_t ino;
	uint8_t type;
	for (;;) {
		long r = ext4r_readdir(&fs, &o->in, &o->dpos, name, &ino, &type);
		if (r <= 0)
			return r < 0 ? E_IO : 0;
		if (!o->path[0] && !strcmp(name, "lost+found"))
			continue; /* (celui de mke2fs, à la racine : pas montré) */
		if (o->drang++ == rang)
			break;
	}
	struct ext4r_inode c;
	long r = ext4r_inode(&fs, ino, &c);
	if (r == 0 && EXT4R_ISLNK(c.mode)) { /* un lien : ce qu'il désigne */
		static char p[PATH_OUV + 260];
		u64 n = strlen(o->path);
		memcpy(p, o->path, n);
		p[n] = '/';
		memcpy(p + n + 1, name, strlen(name) + 1);
		if (ext4r_lookup(&fs, p, &c))
			c.mode = 0x8000, c.size = 0; /* (brisé : un fichier vide) */
	} else if (r) {
		c.mode = 0x8000, c.size = 0;
	}
	memset(m->data, 0, 272);
	memcpy(m->data, name, strlen(name) + 1);
	u64 *t = (u64 *)(m->data + 256);
	t[0] = EXT4R_ISDIR(c.mode) ? 0 : c.size;
	t[1] = attr_de(&c);
	m->len = 272;
	return 1;
}

/* 12 fermer(a numéro) */
static long op_fermer(struct msg *m)
{
	u64 num = m->a;
	if (num >= OUV_MAX || !ouverts[num].owner)
		return E_INVAL;
	long r = fo_access(m, num);
	if (r)
		return r;
	if (fo_own_close(m, num))
		ouverts[num].owner = 0;
	return 0;
}

/* 19 info -> a octets du volume, b octets libres, c octets par bloc, d FS_T_EXT4 ; données : étiquette */
static long op_info(struct msg *m)
{
	m->a = fs.blocks * fs.bs;
	m->b = fs.free_blocks * fs.bs;
	m->c = fs.bs;
	m->d = FS_T_EXT4;
	u64 n = strlen(fs.label);
	memcpy(m->data, fs.label, n + 1);
	m->len = (u32)(n + 1);
	return 0;
}

static long op_sync(struct msg *m)
{
	(void)m;
	return 0;
}

static const struct {
	long op;
	long (*fn)(struct msg *);
} ops[] = {
	{FS_OPEN, op_ouvrir},          {FS_STAT, op_stat},          {FS_MKDIR, op_lecture_seule},
	{FS_UNLINK, op_lecture_seule}, {FS_READ, op_lire},          {FS_WRITE, op_lecture_seule},
	{FS_READDIR, op_lire_rep},     {FS_BUFFER, op_tampon},      {FS_READ_BUF, op_lire_t},
	{FS_WRITE_BUF, op_lecture_seule}, {FS_UTIME, op_lecture_seule}, {FS_RENAME, op_lecture_seule},
	{FS_INFO, op_info},            {FS_SEEK, op_position},      {FS_READ_DIRECT, op_lire_d},
	{FS_SYNC, op_sync},            {0, 0},
};

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *p = krt_line; /* « <volume> <périphérique> [ro] » */
	if (!p)
		SVC_QUIT();
	char *o = str_cat(nom_svc, "fs.");
	while (*p && *p != ' ')
		*o++ = *p++;
	*o = 0;
	if (*p++ != ' ')
		SVC_QUIT();
	o = nom_blk;
	while (*p && *p != ' ' && o < nom_blk + sizeof nom_blk - 1)
		*o++ = *p++;
	*o = 0;
	canal_blk = korin_sys2(SYS_SVC_OPEN, nom_blk, 3000);
	if (canal_blk < 0)
		SVC_QUIT();
	if (blk_request(BLK_INFO, 0, 0, 0) < 0)
		SVC_QUIT();
	secteurs = msg_blk.b;
	long blk_pid = msg_blk.d;
	long num = korin_sys1(SYS_BUF_CREATE, CACHE);
	if (num < 0)
		SVC_QUIT();
	cache = (u8 *)(BUF_VA_BASE + (u64)num * BUF_SLOT);
	korin_sys2(SYS_BUF_GRANT, num, blk_pid);
	msg_blk.op = BLK_ATTACH;
	msg_blk.a = num;
	msg_blk.b = CACHE;
	if (korin_sys2(SYS_SVC_CALL, canal_blk, &msg_blk) < 0)
		SVC_QUIT();
	tags = ak_malloc((CACHE / 1024) * sizeof *tags);
	if (!tags)
		SVC_QUIT();
	long r = ext4r_mount(&fs, rd, 0);
	if (r) {
		ak_puts(nom_svc);
		ak_puts(r == -22 ? " : pas un volume ext4 (ou un format refusé)\n" : " : lecture impossible\n");
		SVC_QUIT();
	}
	if (fs.flags & EXT4R_DIRTY) {
		ak_puts(nom_svc);
		ak_puts(" : le journal n'a pas été rejoué (volume mal démonté) : lu tel quel\n");
	}
	fs_canal = korin_sys2(SYS_SVC_REGISTER, nom_svc, SVF_ENDS | SVF_BIRTHS);
	if (fs_canal < 0)
		SVC_QUIT();
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, fs_canal, &msg, 60000);
		if (req < 0)
			SVC_QUIT();
		if (!req)
			continue;
		req_cour = req;
		u64 op = msg.op;
		if (op == SVC_OP_END) {
			fin_pid(msg.a);
			continue;
		}
		if (op == SVC_OP_FORK) {
			fo_own_fork(msg.a, msg.b);
			continue;
		}
		if (op == SVC_OP_BIRTH)
			continue;
		long res;
		if (op == FS_CLOSE) {
			res = op_fermer(&msg);
			if (!msg.pid) /* avis du noyau : sans réponse */
				continue;
		} else {
			res = E_INVAL;
			for (int i = 0; ops[i].op; i++)
				if ((u64)ops[i].op == op) {
					res = ops[i].fn(&msg);
					break;
				}
		}
		msg.op = res;
		korin_sys2(SYS_SVC_REPLY, req, &msg);
	}
}
