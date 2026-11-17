/* osinstall - installer Korin OS sur un disque, depuis la clé d'installation (docs/etude/INSTALLATEUR.md,
 * docs/DESIGN-INSTALLATEUR.md) : le moteur, que Setup --install lance ; aussi seul, pour les essais.
 *
 *   osinstall PLAN        (uid 0, sur la clé : le pilote du disque en mode installation)
 *   osinstall --list      les disques internes (blk.nvme0 à 3) et leurs partitions, pour Setup, une ligne chacun :
 *                           D service secteurs gpt|none
 *                           P numéro premier dernier type contenu nom      (type : efi, msr, recovery, basic, linux,
 *                                                                          other ; contenu : ntfs, bitlocker, fat32,
 *                                                                          exfat, ext, empty, unknown ; nom : le reste)
 *                           F premier dernier                              (un espace libre d'1 Mio au moins)
 *
 * PLAN (Setup l'écrit dans /tmp/install/plan.ini) :
 *   [disk]   device = blk.nvme0 ; mode = beside (à côté de l'existant) | wipe (le disque entier) ;
 *            delete = 3, 4 (beside : les entrées à supprimer d'abord, numéros de la GPT) ;
 *            place = PREMIER-DERNIER (secteurs : la place de Korin, libre après les suppressions ; défaut : le plus
 *            grand espace libre) ; root_fs = fat32 | exfat ; home = no | yes ; root_gb = 64 (avec home) ;
 *            home_fs = exfat | ext4 ; boot_mb = 512 (une EFI neuve) ; min_gb = 16 (place exigée)
 *   [system] hostname, lang (fr, en), keyb (BE, FR, US...), timeout (secondes du menu ; défaut 5 si Windows, 0)
 *   [user]   name (uid 1000), fullname, autologin = yes|no, admin = yes|no, passwords = stdin|none ;
 *            hash, root_hash : les empreintes déjà faites (Setup : « pbkdf2-sha256$... », jamais le mot de passe)
 *   [packages] list = jet courier ... (les paquets choisis : ceux de la copie du système y sont déjà ; ceux dont
 *            l'archive est dans le dépôt de la clé, /packages/pc, sont installés depuis elle (/bin/unzip, puis leur
 *            fiche var/pkg/db/<nom>.ini avec le SHA-256 de chaque fichier, comme mkrepo.py) ; les autres sont notés
 *            dans /var/pkg/wanted.txt de la cible, que Logiciels installera)
 * Entrée standard (passwords = stdin) : le mot de passe de l'utilisateur, puis celui de root (deux lignes ; vides : aucun ; root vide
 * et admin : celui de l'utilisateur). Jamais écrits en clair : seules leurs empreintes (authd/pwhash.h).
 *
 * Le disque (décision du 16/11/2026) : toujours une partition EFI, celle qui existe (gardée, jamais formatée : le
 * chargeur va dans \EFI\Korin, le noyau, l'initrd, local.tar et boot.cfg dans \korin ; rien d'autre n'y est
 * touché), sinon une neuve de boot_mb au début de la place (avec aussi \EFI\BOOT\BOOTX64.EFI) ; puis KORIN OS
 * (/, FAT32 ou exFAT) et KORIN HOME (/home) si demandé. L'EFI est /boot (blk.boot : le volume du chargeur). Étapes (sur la sortie, pour Setup) :
 *   « S n texte » une étape (1 à 5), « P pourcent texte » la progression, « L texte » le journal,
 *   « X code texte » l'échec (code de sortie 1), « OK » la fin (code 0).
 * Le système installé : / monté par l'alias blk.root (nvme.txt de local.tar, par GUID), /boot à la demande
 * (blk.boot : la partition du chargeur), /home ; la première fois, le chargeur crée l'entrée « Korin OS » du
 * firmware (boot.cfg : firmware = install ; la clé lance ce chargeur au redémarrage, src/kernel/arch/pc/boot). */
#include "korinrt.h"
#include "../../lib/gpt/gpt.h"
#include "../../svc/authd/pwhash.h"

#define BLK_INFO 1
#define BLK_ATTACH 2
#define BLK_READ 3
#define BLK_WRITE 4
#define BLK_PLAN 9
#define BLK_RESCAN 10
#define PLAN_WIPE 1
#define BUF_VA_BASE 0x6800000000ull
#define BUF_SLOT 0x4000000ull
#define DBUF 65536
#define MIB 2048ull /* secteurs */

/* ---- sortie pour Setup */

static void out(const char *s)
{
	sys_write(1, s, (u64)str_len(s));
}

static char *num(u64 v, char *o)
{
	char t[24];
	int k = 0;
	do
		t[k++] = (char)('0' + v % 10);
	while ((v /= 10));
	while (k)
		*o++ = t[--k];
	*o = 0;
	return o;
}

static void line2(const char *a, const char *b)
{
	out(a);
	out(b);
	out("\n");
}

static void step(int n, const char *t)
{
	char b[8] = "S ";
	b[2] = (char)('0' + n);
	b[3] = ' ';
	b[4] = 0;
	line2(b, t);
}

static void logl(const char *t) { line2("L ", t); }

static u64 prog_last = 101;
static void prog(u64 pct, const char *t)
{
	if (pct == prog_last)
		return;
	prog_last = pct;
	char b[16] = "P ";
	char *e = num(pct, b + 2);
	*e++ = ' ';
	*e = 0;
	line2(b, t);
}

static int fail(long code, const char *t)
{
	char b[32] = "X ";
	char *e = b + 2;
	if (code < 0) {
		*e++ = '-';
		code = -code;
	}
	e = num((u64)code, e);
	*e++ = ' ';
	*e = 0;
	line2(b, t);
	return 1;
}

/* ---- le plan */

static char plan[8192];

/* [section] key = valeur -> valeur (copiée dans out), ou def */
static const char *ini(const char *sect, const char *key, const char *def, char *o, u64 cap)
{
	const char *p = plan;
	int in = 0;
	u64 kl = (u64)str_len(key), sl = (u64)str_len(sect);
	while (*p) {
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		const char *s = p;
		while (s < e && (*s == ' ' || *s == '\t'))
			s++;
		if (*s == '[') {
			in = (u64)(e - s) >= sl + 2 && !memcmp(s + 1, sect, sl) && s[sl + 1] == ']';
		} else if (in && (u64)(e - s) > kl && !memcmp(s, key, kl)) {
			const char *v = s + kl;
			while (v < e && (*v == ' ' || *v == '\t'))
				v++;
			if (v < e && *v == '=') {
				v++;
				while (v < e && (*v == ' ' || *v == '\t'))
					v++;
				const char *ve = e;
				while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r'))
					ve--;
				u64 n = (u64)(ve - v) < cap - 1 ? (u64)(ve - v) : cap - 1;
				memcpy(o, v, n);
				o[n] = 0;
				return o;
			}
		}
		p = *e ? e + 1 : e;
	}
	u64 n = (u64)str_len(def);
	memcpy(o, def, n + 1);
	return o;
}

static int yes(const char *v) { return str_eq(v, "yes") || str_eq(v, "1") || str_eq(v, "oui"); }

/* ---- le disque, par le protocole blk (comme src/bin/gpt) */

static long dchan = -1;
static u8 *dbuf;
static u64 dm[MSG_SIZE / 8];

static long blk(long ch, long op, long a, long b, long c)
{
	for (int i = 0; i < MSG_SIZE / 8; i++)
		dm[i] = 0;
	dm[0] = (u64)op;
	dm[1] = (u64)a;
	dm[2] = (u64)b;
	dm[3] = (u64)c;
	long r = korin_sys2(SYS_SVC_CALL, ch, dm);
	return r < 0 ? r : (long)dm[0];
}

static long disk_open(const char *svc, u64 *sectors)
{
	dchan = korin_sys2(SYS_SVC_OPEN, svc, 3000);
	if (dchan < 0)
		return dchan;
	long r = blk(dchan, BLK_INFO, 0, 0, 0);
	if (r < 0)
		return r;
	*sectors = dm[2];
	long pid = (long)dm[4];
	long id = korin_sys1(SYS_BUF_CREATE, DBUF);
	if (id < 0)
		return id;
	dbuf = (u8 *)(BUF_VA_BASE + (u64)id * BUF_SLOT);
	korin_sys2(SYS_BUF_GRANT, id, pid);
	return blk(dchan, BLK_ATTACH, id, DBUF, 0);
}

static long disk_rw(void *ctx, u64 sector, u64 count, void *buf, int write)
{
	(void)ctx;
	while (count) {
		u64 k = count > DBUF / GPT_SECTOR ? DBUF / GPT_SECTOR : count;
		if (write)
			memcpy(dbuf, buf, k * GPT_SECTOR);
		long r = blk(dchan, write ? BLK_WRITE : BLK_READ, (long)sector, (long)k, (long)dbuf);
		if (r < 0)
			return r;
		if (!write)
			memcpy(buf, dbuf, k * GPT_SECTOR);
		buf = (u8 *)buf + k * GPT_SECTOR;
		sector += k;
		count -= k;
	}
	return 0;
}

static long disk_rd(void *ctx, uint64_t s, uint64_t n, void *b) { return disk_rw(ctx, s, n, b, 0); }
static long disk_wr(void *ctx, uint64_t s, uint64_t n, void *b) { return disk_rw(ctx, s, n, b, 1); }

#define PLAN_ESP 2

/* le plan : n plages (premier, dernier) */
static long plan_send(u64 flags, u32 crc, const u64 *ranges, int n)
{
	for (int i = 0; i < MSG_SIZE / 8; i++)
		dm[i] = 0;
	dm[0] = BLK_PLAN;
	dm[1] = flags;
	dm[2] = crc;
	dm[3] = (u64)n;
	for (int i = 0; i < 2 * n; i++)
		dm[6 + i] = ranges[i]; /* (données : MSG_DATA = 48) */
	((u32 *)dm)[10] = (u32)(16 * n);
	long r = korin_sys2(SYS_SVC_CALL, dchan, dm);
	return r < 0 ? r : (long)dm[0];
}

static void rnd(void *b, u64 n)
{
	korin_sys2(SYS_GETRANDOM, (long)b, (long)n);
}

static void guid_rnd(u8 g[16])
{
	rnd(g, 16);
	g[7] = (u8)((g[7] & 0x0F) | 0x40); /* (version 4, dans l'ordre de la GPT) */
	g[8] = (u8)((g[8] & 0x3F) | 0x80);
}

/* le GUID d'une entrée, comme blkserv et nvme l'écrivent (minuscules) */
static void guid_text(const u8 *g, char *o)
{
	static const char hx[] = "0123456789abcdef";
	static const int order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
	for (int i = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			*o++ = '-';
		u8 v = g[order[i]];
		*o++ = hx[v >> 4];
		*o++ = hx[v & 15];
	}
	*o = 0;
}

/* ---- programmes : mkfs */

static long run(const char *prog_path, const char *args)
{
	long pid = ksys(SYS_SPAWN, (long)prog_path, (long)args, 0, 0, 0, 0);
	if (pid < 0)
		return pid;
	return korin_sys1(SYS_WAIT, pid);
}

/* ---- fichiers */

static u8 cbuf[262144];

static long wr_file(const char *path, const void *data, u64 n)
{
	long f = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
	if (f < 0)
		return f;
	long r = n ? sys_write(f, data, n) : 0;
	sys_close(f);
	return r == (long)n ? 0 : E_IO;
}

/* lit n octets (moins à la fin du fichier) : un sys_read rend au plus ce que le service passe en une fois */
static long rd_full(long f, void *buf, u64 n)
{
	u64 got = 0;
	long r = 0;
	while (got < n && (r = sys_read(f, (char *)buf + got, n - got)) > 0)
		got += (u64)r;
	return got ? (long)got : r;
}

static long rd_file(const char *path, char *buf, u64 cap)
{
	long f = sys_open(path, O_READ);
	if (f < 0)
		return f;
	long n = rd_full(f, buf, cap - 1); /* (l'index de la clé : 44 Ko, en plusieurs lectures) */
	sys_close(f);
	if (n < 0)
		return n;
	buf[n] = 0;
	return n;
}

static long cp_file(const char *from, const char *to)
{
	long a = sys_open(from, O_READ);
	if (a < 0)
		return a;
	long b = sys_open(to, O_WRITE | O_CREATE | O_TRUNC);
	if (b < 0) {
		sys_close(a);
		return b;
	}
	long n, r = 0;
	while ((n = sys_read(a, cbuf, sizeof cbuf)) > 0)
		if (sys_write(b, cbuf, (u64)n) != n) {
			r = E_IO;
			break;
		}
	if (n < 0)
		r = n;
	sys_close(a);
	sys_close(b);
	return r;
}

static char *join(char *o, const char *a, const char *b)
{
	o[0] = 0;
	str_cat(o, a);
	u64 n = (u64)str_len(o);
	if (n && o[n - 1] != '/')
		str_cat(o + n, "/");
	str_cat(o + str_len(o), b);
	return o;
}

/* ce qui ne se copie pas depuis la racine de la clé : les points de montage, ce qui va sur KORIN BOOT, la clé */
static int skip_top(const char *n)
{
	static const char *const s[] = {"home", "korin", "EFI", "media", "net", "tmp", "boot", "System Volume Information",
					0};
	for (int i = 0; s[i]; i++)
		if (str_eq(n, s[i]))
			return 1;
	return 0;
}

static int skip_file(const char *rel)
{
	return str_eq(rel, "etc/install.ini") || str_eq(rel, "etc/e4test.sh") || str_eq(rel, "etc/plan-test.ini");
}

/* ce qui n'appartient qu'à la clé : l'installateur, le dépôt hors ligne */
static int skip_dir(const char *rel)
{
	return str_eq(rel, "apps/installer.app") || str_eq(rel, "packages");
}

/* les lignes que installkey.py ajoute (l'installateur dans session, les essais dans services.txt) quittent la copie :
 * une ligne qui contient l'un des mots, et le commentaire « (installkey.py ...) » qui la précède */
static void strip_key_lines(const char *path)
{
	static char in[16384], out[16384];
	if (rd_file(path, in, sizeof in) < 0)
		return;
	static const char *const mots[] = {"installer.app", "osinstall", "gpt disktest", "installkey.py", 0};
	u64 o = 0;
	for (char *l = in; *l;) {
		char *e = l;
		while (*e && *e != '\n')
			e++;
		char c = *e;
		*e = 0;
		int jeter = 0;
		for (int i = 0; mots[i] && !jeter; i++)
			for (char *s = l; *s && !jeter; s++)
				if (!memcmp(s, mots[i], (u64)str_len(mots[i])))
					jeter = 1;
		if (!jeter) {
			u64 n = (u64)(e - l);
			if (o + n + 2 < sizeof out) {
				memcpy(out + o, l, n);
				o += n;
				out[o++] = '\n';
			}
		}
		l = c ? e + 1 : e;
	}
	wr_file(path, out, o);
}

static u64 tot_files, done_files, err_files;
static int counting;

static void tree(const char *src, const char *dst, const char *rel, int top)
{
	long fd = sys_open(src, O_READ);
	if (fd < 0) {
		err_files++;
		return;
	}
	struct dirent e;
	while (sys_readdir(fd, &e) > 0) {
		if (top && skip_top(e.name))
			continue;
		char s[512], d[512], r[512];
		join(s, src, e.name);
		join(d, dst, e.name);
		if (rel[0])
			join(r, rel, e.name);
		else {
			r[0] = 0;
			str_cat(r, e.name);
		}
		if (e.attr & ATTR_DIR) {
			if (skip_dir(r))
				continue;
			if (!counting)
				sys_mkdir(d);
			tree(s, d, r, 0);
			continue;
		}
		if (skip_file(r))
			continue;
		if (counting) {
			tot_files++;
			continue;
		}
		if (cp_file(s, d) < 0) {
			err_files++;
			logl(r);
		}
		done_files++;
		if (tot_files)
			prog(10 + done_files * 70 / tot_files, "Copie du système");
	}
	sys_close(fd);
}

/* ---- local.tar (ustar) : init.txt et nvme.txt de cette machine */

static u8 tarb[16384];
static u64 tarn;

static void octal(char *o, u64 v, int w)
{
	o[w - 1] = 0;
	for (int i = w - 2; i >= 0; i--) {
		o[i] = (char)('0' + (v & 7));
		v >>= 3;
	}
}

static void tar_add(const char *name, const char *text)
{
	u64 n = (u64)str_len(text);
	char *h = (char *)tarb + tarn;
	memset(h, 0, 512);
	memcpy(h, name, (u64)str_len(name));
	octal(h + 100, 0644, 8);
	octal(h + 108, 0, 8);
	octal(h + 116, 0, 8);
	octal(h + 124, n, 12);
	octal(h + 136, 0, 12);
	h[156] = '0';
	memcpy(h + 257, "ustar", 6);
	memcpy(h + 263, "00", 2);
	memset(h + 148, ' ', 8);
	u32 sum = 0;
	for (int i = 0; i < 512; i++)
		sum += (u8)h[i];
	octal(h + 148, sum, 7);
	h[155] = ' ';
	tarn += 512;
	memcpy(tarb + tarn, text, n);
	tarn += (n + 511) / 512 * 512;
}

/* ---- empreintes */

static void hexs(const u8 *b, u64 n, char *o)
{
	static const char hx[] = "0123456789abcdef";
	for (u64 i = 0; i < n; i++) {
		*o++ = hx[b[i] >> 4];
		*o++ = hx[b[i] & 15];
	}
	*o = 0;
}

/* « pbkdf2-sha256$20000$<sel>$<empreinte> » (comme authd) */
static void pw_hash(const char *pw, char *o)
{
	u8 salt[16], h[32];
	rnd(salt, 16);
	pbkdf2(pw, salt, 16, 20000, h);
	o[0] = 0;
	str_cat(o, "pbkdf2-sha256$20000$");
	hexs(salt, 16, o + str_len(o));
	str_cat(o + str_len(o), "$");
	hexs(h, 32, o + str_len(o));
}

static void read_line(char *o, u64 cap)
{
	u64 n = 0;
	char c;
	while (n < cap - 1 && sys_read(0, &c, 1) == 1 && c != '\n')
		if (c != '\r')
			o[n++] = c;
	o[n] = 0;
}

/* ---- les paquets de la clé (/packages/pc : index.txt et les archives) */

static char kidx[262144];

/* le fichier de l'archive du paquet sur la clé (« pkgs/x.opk », ou le nom d'une archive de la Release) -> 1 */
static int key_opk(const char *name, char *path, u64 cap)
{
	if (!kidx[0] && rd_file("/packages/pc/index.txt", kidx, sizeof kidx) < 0)
		return 0;
	char sect[64] = "[";
	str_cat(str_cat(sect, name), "]");
	const char *p = kidx;
	u64 sl = (u64)str_len(sect);
	while (*p) {
		if (!memcmp(p, sect, sl) && (p[sl] == '\n' || p[sl] == '\r')) {
			for (const char *l = p + sl; *l && *l != '[';) {
				while (*l == '\n' || *l == '\r')
					l++;
				if (!memcmp(l, "file", 4)) {
					const char *v = l + 4;
					while (*v == ' ' || *v == '=')
						v++;
					const char *e = v, *base = v;
					while (*e && *e != '\n' && *e != '\r') {
						if (*e == '/')
							base = e + 1;
						e++;
					}
					int url = 0;
					for (const char *c = v; c + 2 < e; c++)
						if (c[0] == ':' && c[1] == '/' && c[2] == '/')
							url = 1;
					char rel[160];
					u64 n = url ? (u64)(e - base) : (u64)(e - v);
					if (n > 150)
						return 0;
					memcpy(rel, url ? "pkgs/" : "", url ? 5 : 0);
					memcpy(rel + (url ? 5 : 0), url ? base : v, n);
					rel[n + (url ? 5 : 0)] = 0;
					if ((u64)str_len(rel) + 14 > cap)
						return 0;
					join(path, "/packages/pc", rel);
					long f = sys_open(path, O_READ);
					if (f < 0)
						return 0;
					sys_close(f);
					return 1;
				}
				while (*l && *l != '\n')
					l++;
			}
			return 0;
		}
		while (*p && *p != '\n')
			p++;
		if (*p)
			p++;
	}
	return 0;
}

static void sha256_file(const char *path, char *hex)
{
	struct sha s;
	u8 h[32];
	sha_init(&s);
	long f = sys_open(path, O_READ), n;
	if (f >= 0) {
		while ((n = sys_read(f, cbuf, sizeof cbuf)) > 0)
			sha_add(&s, cbuf, (u64)n);
		sys_close(f);
	}
	sha_end(&s, h);
	hexs(h, 32, hex);
}

/* installe un paquet de la clé dans la cible -> 0, ou < 0 */
static long key_install(const char *name, const char *opk)
{
	static char args[400], db[96000], man[4096];
	args[0] = 0;
	str_cat(str_cat(str_cat(args, "-o -q -d /media/tgt "), opk), "");
	long r = run("/bin/unzip", args);
	if (r != 0)
		return r < 0 ? r : E_IO;
	/* la fiche : le manifeste, puis chaque fichier de l'archive (le répertoire central du zip) et son SHA-256 */
	if (rd_file("/media/tgt/PKG/manifest.ini", man, sizeof man) < 0)
		man[0] = 0;
	sys_unlink("/media/tgt/PKG/manifest.ini");
	sys_unlink("/media/tgt/PKG");
	db[0] = 0;
	str_cat(db, "# installe par osinstall (cle d'installation)\n[package]\n");
	for (char *l = man; *l;) {
		char *e = l;
		while (*e && *e != '\n')
			e++;
		char c = *e;
		*e = 0;
		if (*l && *l != '#' && (u64)str_len(db) + (u64)(e - l) + 2 < sizeof db)
			str_cat(str_cat(db + str_len(db), l), "\n");
		*e = c;
		l = c ? e + 1 : e;
	}
	str_cat(db + str_len(db), "mode = manual\n\n[files]\n");
	long f = sys_open(opk, O_READ);
	if (f < 0)
		return f;
	long size = sys_seek(f, 0, 2);
	u64 tail = size > 65557 ? 65557 : (u64)size; /* la fin du répertoire central : dans les 64 Kio de la fin */
	sys_seek(f, size - (long)tail, 0);
	long got = rd_full(f, cbuf, tail);
	long eocd = -1;
	for (long i = got - 22; i >= 0; i--)
		if (cbuf[i] == 'P' && cbuf[i + 1] == 'K' && cbuf[i + 2] == 5 && cbuf[i + 3] == 6) {
			eocd = i;
			break;
		}
	if (eocd < 0) {
		sys_close(f);
		return E_IO;
	}
	u32 cd_size = cbuf[eocd + 12] | cbuf[eocd + 13] << 8 | cbuf[eocd + 14] << 16 | (u32)cbuf[eocd + 15] << 24;
	u32 cd_off = cbuf[eocd + 16] | cbuf[eocd + 17] << 8 | cbuf[eocd + 18] << 16 | (u32)cbuf[eocd + 19] << 24;
	static u8 cd[131072];
	if (cd_size > sizeof cd) {
		sys_close(f);
		return E_IO;
	}
	sys_seek(f, cd_off, 0);
	if (rd_full(f, cd, cd_size) != (long)cd_size) {
		sys_close(f);
		return E_IO;
	}
	sys_close(f);
	for (u32 o = 0; o + 46 <= cd_size;) {
		if (cd[o] != 'P' || cd[o + 1] != 'K' || cd[o + 2] != 1 || cd[o + 3] != 2)
			break;
		u32 nl = cd[o + 28] | cd[o + 29] << 8, xl = cd[o + 30] | cd[o + 31] << 8, cl = cd[o + 32] | cd[o + 33] << 8;
		char nm[300];
		u32 k = nl < 299 ? nl : 299;
		memcpy(nm, cd + o + 46, k);
		nm[k] = 0;
		o += 46 + nl + xl + cl;
		if (!k || nm[k - 1] == '/' || !memcmp(nm, "PKG/", 4))
			continue;
		char full[400], hex[65];
		join(full, "/media/tgt", nm);
		sha256_file(full, hex);
		if ((u64)str_len(db) + k + 70 < sizeof db)
			str_cat(str_cat(str_cat(str_cat(db + str_len(db), nm), " = "), hex), "\n");
	}
	char dbp[160];
	join(dbp, "/media/tgt/var/pkg/db", name);
	str_cat(dbp, ".ini");
	return wr_file(dbp, db, (u64)str_len(db));
}

/* ---- le programme */

static struct gpt g;
static char txt[16384], tmp[16384];

static void part_name(char *o, const char *dev, int n)
{
	o[0] = 0;
	str_cat(o, dev);
	char *e = o + str_len(o);
	*e++ = 'p';
	num((u64)n, e);
}

/* ---- --list : les disques pour Setup */

static const char *type_word(const u8 *t)
{
	if (!memcmp(t, GPT_T_EFI, 16))
		return "efi";
	if (!memcmp(t, GPT_T_MSR, 16))
		return "msr";
	if (!memcmp(t, GPT_T_RECOVERY, 16))
		return "recovery";
	if (!memcmp(t, GPT_T_BASIC, 16))
		return "basic";
	if (!memcmp(t, GPT_T_LINUX, 16))
		return "linux";
	return "other";
}

static const char *content_word(u64 first)
{
	static u8 s[1024];
	if (disk_rw(0, first, 2, s, 0) < 0)
		return "unknown";
	int blank = 1;
	for (int i = 0; i < 512; i++)
		blank &= s[i] == 0;
	if (!memcmp(s + 3, "NTFS    ", 8))
		return "ntfs";
	if (!memcmp(s + 3, "-FVE-FS-", 8))
		return "bitlocker";
	if (!memcmp(s + 3, "EXFAT   ", 8))
		return "exfat";
	if (!memcmp(s + 82, "FAT32", 5) || !memcmp(s + 54, "FAT1", 4))
		return "fat32";
	if (disk_rw(0, first + 2, 1, s, 0) == 0 && s[0x38] == 0x53 && s[0x39] == 0xEF)
		return "ext";
	return blank ? "empty" : "unknown";
}

static void out_num(u64 v)
{
	char b[24];
	num(v, b);
	out(b);
}

static int list_disks(void)
{
	for (int d = 0; d < 4; d++) {
		char dev[16] = "blk.nvme0";
		dev[8] = (char)('0' + d);
		long ch = korin_sys2(SYS_SVC_OPEN, (long)dev, 200);
		if (ch < 0)
			continue;
		dchan = ch;
		if (blk(dchan, BLK_INFO, 0, 0, 0) < 0)
			continue;
		u64 sectors = dm[2];
		long pid = (long)dm[4];
		static long id = -1;
		if (id < 0) {
			id = korin_sys1(SYS_BUF_CREATE, DBUF);
			if (id < 0)
				return 1;
			dbuf = (u8 *)(BUF_VA_BASE + (u64)id * BUF_SLOT);
		}
		korin_sys2(SYS_BUF_GRANT, id, pid);
		if (blk(dchan, BLK_ATTACH, id, DBUF, 0) < 0)
			continue;
		int has = gpt_read(&g, disk_rd, 0, sectors) == 0;
		out("D ");
		out(dev);
		out(" ");
		out_num(sectors);
		out(has ? " gpt\n" : " none\n");
		if (!has)
			continue;
		int ord[GPT_ENTRIES], n = 0; /* par début croissant */
		for (int i = 0; i < GPT_ENTRIES; i++)
			if (gpt_used(&g.e[i])) {
				int k = n++;
				while (k > 0 && g.e[ord[k - 1]].first > g.e[i].first) {
					ord[k] = ord[k - 1];
					k--;
				}
				ord[k] = i;
			}
		u64 at = (g.first_usable + MIB - 1) / MIB * MIB;
		for (int j = 0; j <= n; j++) {
			u64 next = j < n ? g.e[ord[j]].first : g.last_usable + 1;
			if (next > at && next - at >= MIB) { /* l'espace libre avant */
				u64 end = j < n ? next - 1 : (g.last_usable + 1) / MIB * MIB - 1;
				if (end > at) {
					out("F ");
					out_num(at);
					out(" ");
					out_num(end);
					out("\n");
				}
			}
			if (j == n)
				break;
			const struct gpt_entry *e = &g.e[ord[j]];
			out("P ");
			out_num((u64)ord[j] + 1);
			out(" ");
			out_num(e->first);
			out(" ");
			out_num(e->last);
			out(" ");
			out(type_word(e->type));
			out(" ");
			out(content_word(e->first));
			out(" ");
			char nm[80];
			int k = 0;
			for (int c = 0; c < 36 && e->name[c] && k < 76; c++) { /* UTF-16 -> UTF-8 (plan de base) */
				u32 u = e->name[c];
				if (u < 0x80)
					nm[k++] = (char)(u > ' ' || u == ' ' ? u : '?');
				else if (u < 0x800) {
					nm[k++] = (char)(0xC0 | (u >> 6));
					nm[k++] = (char)(0x80 | (u & 63));
				} else {
					nm[k++] = (char)(0xE0 | (u >> 12));
					nm[k++] = (char)(0x80 | ((u >> 6) & 63));
					nm[k++] = (char)(0x80 | (u & 63));
				}
			}
			nm[k] = 0;
			out(nm);
			out("\n");
			at = (e->last + 1 + MIB - 1) / MIB * MIB;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && str_eq(argv[1], "--list"))
		return list_disks();
	if (argc < 2) {
		out("usage : osinstall PLAN (le plan de Setup --install, /tmp/install/plan.ini)\n");
		return 2;
	}
	if (rd_file(argv[1], plan, sizeof plan) < 0)
		return fail(E_NOENT, "plan illisible");
	char dev[32], mode[16], rootfs[16], homefs[16], v[64];
	ini("disk", "device", "blk.nvme0", dev, sizeof dev);
	ini("disk", "mode", "beside", mode, sizeof mode);
	ini("disk", "root_fs", "fat32", rootfs, sizeof rootfs);
	ini("disk", "home_fs", "exfat", homefs, sizeof homefs);
	int wipe = str_eq(mode, "wipe");
	int home = yes(ini("disk", "home", "no", v, sizeof v));
	u64 root_gb = parse_dec(ini("disk", "root_gb", "64", v, sizeof v));
	u64 boot_mb = parse_dec(ini("disk", "boot_mb", "512", v, sizeof v));
	u64 min_gb = parse_dec(ini("disk", "min_gb", "16", v, sizeof v));
	if (!str_eq(rootfs, "fat32") && !str_eq(rootfs, "exfat"))
		return fail(E_INVAL, "root_fs : fat32 ou exfat");
	if (home && !str_eq(homefs, "exfat") && !str_eq(homefs, "fat32") && !str_eq(homefs, "ext4"))
		return fail(E_INVAL, "home_fs : exfat, fat32 ou ext4");
	char user[32], full[64], host[64], lang[8], keyb[16];
	ini("user", "name", "user", user, sizeof user);
	ini("user", "fullname", user, full, sizeof full);
	ini("system", "hostname", "korin", host, sizeof host);
	ini("system", "lang", "fr", lang, sizeof lang);
	ini("system", "keyb", "", keyb, sizeof keyb);
	int autologin = yes(ini("user", "autologin", "yes", v, sizeof v));
	int admin = yes(ini("user", "admin", "yes", v, sizeof v));
	static char upw[256], rpw[256];
	if (str_eq(ini("user", "passwords", "none", v, sizeof v), "stdin")) { /* (Setup ; un essai : aucun) */
		read_line(upw, sizeof upw);
		read_line(rpw, sizeof rpw);
	}
	if (!rpw[0] && admin)
		memcpy(rpw, upw, sizeof rpw);

	/* 1. le disque */
	step(1, "Préparer le disque");
	u64 sectors;
	long r = disk_open(dev, &sectors);
	if (r < 0)
		return fail(r, "le disque ne répond pas");
	int had_table = gpt_read(&g, disk_rd, 0, sectors) == 0 && !g.from_backup;
	u32 crc = had_table ? g.header_crc : 0;
	u64 a, b;
	int esp = -1; /* l'entrée EFI gardée (rang), -1 : une neuve */
	if (wipe) {
		u8 dg[16];
		guid_rnd(dg);
		gpt_init(&g, sectors, dg);
		if (!gpt_free_space(&g, &a, &b))
			return fail(E_NOSPC, "disque trop petit");
	} else {
		if (!had_table)
			return fail(E_INVAL, "pas de table GPT lisible : seul « disque entier » est possible");
		const char *d = ini("disk", "delete", "", v, sizeof v);
		while (*d) { /* les suppressions demandées (Setup) */
			while (*d == ' ' || *d == ',')
				d++;
			if (!*d)
				break;
			u64 nd = parse_dec(d);
			while (*d >= '0' && *d <= '9')
				d++;
			if (nd >= 1 && nd <= GPT_ENTRIES)
				gpt_del(&g, (int)nd);
		}
		for (int i = 0; i < GPT_ENTRIES && esp < 0; i++)
			if (gpt_used(&g.e[i]) && !memcmp(g.e[i].type, GPT_T_EFI, 16))
				esp = i;
		const char *pl = ini("disk", "place", "", v, sizeof v);
		if (*pl) {
			a = parse_dec(pl);
			while (*pl && *pl != '-')
				pl++;
			b = *pl ? parse_dec(pl + 1) : 0;
			for (int i = 0; i < GPT_ENTRIES; i++)
				if (gpt_used(&g.e[i]) && g.e[i].first <= b && g.e[i].last >= a)
					return fail(E_INVAL, "la place choisie n'est pas libre");
			a = (a + MIB - 1) / MIB * MIB;
		} else if (!gpt_free_space(&g, &a, &b)) {
			return fail(E_NOSPC, "pas d'espace libre");
		}
	}
	if (b <= a || (b + 1) / MIB * MIB - 1 > a)
		b = (b + 1) / MIB * MIB - 1;
	if (b <= a)
		return fail(E_NOSPC, "pas assez de place");
	u64 avail = b - a + 1;
	if (avail < min_gb * 1024 * MIB)
		return fail(E_NOSPC, "pas assez de place libre");
	u64 bsz = esp >= 0 ? 0 : boot_mb * MIB; /* (une EFI neuve au début de la place) */
	if (bsz + 64 * MIB > avail)
		return fail(E_NOSPC, "pas assez de place pour la partition EFI");
	u64 rfirst = a + bsz, rlast = b, hfirst = 0;
	if (home) {
		u64 rs = root_gb * 1024 * MIB;
		if (rs + 256 * MIB > b - rfirst + 1)
			return fail(E_NOSPC, "pas assez de place pour / et /home");
		rlast = rfirst + rs - 1;
		hfirst = rlast + 1;
	}
	logl(wipe ? "plan : le disque entier" : esp >= 0 ? "plan : la place de Korin, et la partition EFI gardée"
						  : "plan : la place de Korin (une partition EFI neuve)");
	u64 ranges[4] = {a, b, 0, 0};
	int nr = 1;
	if (esp >= 0) {
		ranges[2] = g.e[esp].first;
		ranges[3] = g.e[esp].last;
		nr = 2;
	}
	r = plan_send((wipe ? PLAN_WIPE : 0) | (esp >= 0 ? PLAN_ESP : 0), crc, ranges, nr);
	if (r < 0)
		return fail(r, "le pilote du disque refuse le plan");
	u8 gboot[16], groot[16], ghome[16];
	guid_rnd(gboot);
	guid_rnd(groot);
	guid_rnd(ghome);
	int nboot;
	if (esp >= 0) {
		nboot = esp + 1;
		memcpy(gboot, g.e[esp].guid, 16);
	} else {
		nboot = gpt_add(&g, GPT_T_EFI, a, a + bsz - 1, "EFI system partition", gboot);
	}
	int nroot = gpt_add(&g, GPT_T_BASIC, rfirst, rlast, "KORIN OS", groot);
	int nhome = home ? gpt_add(&g, str_eq(homefs, "ext4") ? GPT_T_LINUX : GPT_T_BASIC, hfirst, b, "KORIN HOME", ghome)
			 : 0;
	if (nboot <= 0 || nroot <= 0 || (home && nhome <= 0))
		return fail(E_NOSPC, "la table est pleine");
	if ((r = gpt_write(&g, disk_wr, 0)) < 0)
		return fail(r, "écriture de la table");
	if ((r = blk(dchan, BLK_RESCAN, 0, 0, 0)) < 0)
		return fail(r, "relecture de la table");
	prog(3, "Table des partitions écrite");
	char pboot[40], proot[40], phome[40], args[128];
	part_name(pboot, dev, nboot);
	part_name(proot, dev, nroot);
	part_name(phome, dev, nhome);
	if (esp < 0) { /* (une EFI gardée n'est jamais formatée) */
		args[0] = 0;
		str_cat(str_cat(args, "-t fat32 -y -L EFI "), pboot);
		if ((r = run("/sbin/mkfs", args)) != 0)
			return fail(r, "formatage de la partition EFI");
	}
	prog(5, "Partition EFI prête");
	args[0] = 0;
	str_cat(str_cat(str_cat(str_cat(args, "-t "), rootfs), " -y -L KORINOS "), proot);
	if ((r = run("/sbin/mkfs", args)) != 0)
		return fail(r, "formatage de KORIN OS");
	prog(8, "KORIN OS formatée");
	if (home) {
		args[0] = 0;
		str_cat(str_cat(str_cat(str_cat(args, "-t "), homefs), " -y -L KORINHOME "), phome);
		if ((r = run("/sbin/mkfs", args)) != 0)
			return fail(r, "formatage de KORIN HOME");
	}
	const char *fsvc_root = "fatfs", *fsvc_home = str_eq(homefs, "ext4") ? "ext4fs" : "fatfs";
	if ((r = vu_mount("tgt", proot, fsvc_root)) < 0)
		return fail(r, "montage de KORIN OS");
	if ((r = vu_mount("tgtboot", pboot, "fatfs")) < 0)
		return fail(r, "montage de la partition EFI");
	if (vu_info("tgtboot") >= 0 && ((u64 *)vu_rep)[MSG_B / 8] < (16ull << 20))
		return fail(E_NOSPC, "la partition EFI a moins de 16 Mo libres (le noyau et l'initrd)");
	if (home && (r = vu_mount("tgthome", phome, fsvc_home)) < 0)
		return fail(r, "montage de KORIN HOME");

	/* 2. le système */
	step(2, "Copier le système");
	counting = 1;
	tree("/", "/media/tgt", "", 1);
	counting = 0;
	tree("/", "/media/tgt", "", 1);
	strip_key_lines("/media/tgt/etc/session");
	strip_key_lines("/media/tgt/etc/services.txt");
	sys_mkdir("/media/tgt/home");
	sys_mkdir("/media/tgt/boot");
	sys_mkdir("/media/tgtboot/EFI");
	sys_mkdir("/media/tgtboot/EFI/Korin");
	sys_mkdir("/media/tgtboot/korin");
	if (cp_file("/EFI/BOOT/BOOTX64.EFI", "/media/tgtboot/EFI/Korin/korin.efi") < 0 ||
	    cp_file("/korin/kernel.elf", "/media/tgtboot/korin/kernel.elf") < 0 ||
	    cp_file("/korin/initrd.tar", "/media/tgtboot/korin/initrd.tar") < 0)
		return fail(E_IO, "copie du chargeur et du noyau");
	if (esp < 0) { /* une EFI à nous : le chemin de secours, que le firmware démarre même sans entrée */
		sys_mkdir("/media/tgtboot/EFI/BOOT");
		cp_file("/EFI/BOOT/BOOTX64.EFI", "/media/tgtboot/EFI/BOOT/BOOTX64.EFI");
	}
	if (err_files) {
		char m[64] = "fichiers non copiés : ";
		num(err_files, m + str_len(m));
		logl(m);
	}

	/* 3. les logiciels : ceux de la clé sont dans la copie (var/pkg/db compris) ; les autres, notés pour Logiciels */
	step(3, "Installer les logiciels");
	{
		static char pl[4096];
		ini("packages", "list", "", pl, sizeof pl);
		txt[0] = 0;
		str_cat(txt, "# paquets choisis à l'installation, pas encore installés (osinstall) : Logiciels les installe\n");
		u64 nw = 0;
		for (char *p = pl; *p;) {
			while (*p == ' ' || *p == ',')
				p++;
			char *s = p;
			while (*p && *p != ' ' && *p != ',')
				p++;
			if (p == s)
				break;
			char c = *p;
			*p = 0;
			char db[160], opk[200];
			join(db, "/media/tgt/var/pkg/db", s);
			str_cat(db, ".ini");
			long f = sys_open(db, O_READ);
			if (f >= 0) {
				sys_close(f);
			} else if (key_opk(s, opk, sizeof opk)) {
				char m[96] = "depuis la clé : ";
				str_cat(m + str_len(m), s);
				prog(82, m);
				if (key_install(s, opk) < 0) {
					logl(m);
					str_cat(str_cat(txt + str_len(txt), s), "\n");
					nw++;
				}
			} else {
				str_cat(str_cat(txt + str_len(txt), s), "\n");
				nw++;
			}
			*p = c;
		}
		if (nw) {
			wr_file("/media/tgt/var/pkg/wanted.txt", txt, (u64)str_len(txt));
			char m[64] = "paquets notés pour Logiciels : ";
			num(nw, m + str_len(m));
			logl(m);
		}
	}
	prog(82, "Logiciels de la clé");

	/* 4. les réglages */
	step(4, "Régler l'ordinateur");
	char gr[40], gb[40], gh[40], ghead[32];
	guid_text(groot, gr);
	guid_text(gboot, gb);
	guid_text(ghome, gh);
	txt[0] = 0;
	str_cat(txt, "# comptes (osinstall)\n[system]\nuid = 0\nname = Systeme\nhome = /\ngroups = admin\n[");
	str_cat(str_cat(str_cat(str_cat(txt + str_len(txt), user), "]\nuid = 1000\nname = "), full), "\nhome = /home/");
	str_cat(str_cat(str_cat(txt + str_len(txt), user), "\nshell = /bin/bash\ngroups = "), admin ? "admin" : "users");
	str_cat(txt + str_len(txt), "\n");
	if ((r = wr_file("/media/tgt/etc/users.ini", txt, (u64)str_len(txt))) < 0)
		return fail(r, "users.ini");
	txt[0] = 0;
	str_cat(str_cat(str_cat(txt, "# qui peut élever ses droits (osinstall)\n[elevate]\nusers = "), admin ? user : ""),
		"\ngroups = admin\n");
	wr_file("/media/tgt/etc/elevate.ini", txt, (u64)str_len(txt));
	txt[0] = 0;
	str_cat(txt, "# empreintes des mots de passe (osinstall) : lisible par l'uid 0 seulement\n[shadow]\n");
	static char h[160], hv[160];
	if (upw[0])
		pw_hash(upw, h);
	else
		ini("user", "hash", "", h, sizeof h); /* (Setup : l'empreinte déjà faite) */
	if (h[0])
		str_cat(str_cat(str_cat(str_cat(txt + str_len(txt), user), " = "), h), "\n");
	if (rpw[0])
		pw_hash(rpw, hv);
	else
		ini("user", "root_hash", h, hv, sizeof hv); /* (sans empreinte de root : celle de l'utilisateur) */
	if (hv[0] && (admin || rpw[0] || !str_eq(hv, h)))
		str_cat(str_cat(str_cat(txt + str_len(txt), "system = "), hv), "\n");
	memset(upw, 0, sizeof upw);
	memset(rpw, 0, sizeof rpw);
	wr_file("/media/tgt/etc/shadow.ini", txt, (u64)str_len(txt));
	/* system.ini : la clé, puis le compte, le nom, la langue */
	if (rd_file("/etc/system.ini", tmp, sizeof tmp) < 0)
		tmp[0] = 0;
	txt[0] = 0;
	str_cat(str_cat(txt, tmp), "\n# osinstall\n");
	if (autologin)
		str_cat(str_cat(str_cat(txt + str_len(txt), "autologin="), user), "\n");
	str_cat(str_cat(str_cat(txt + str_len(txt), "hostname="), host), "\n");
	str_cat(str_cat(str_cat(txt + str_len(txt), "lang="), lang), "\n");
	wr_file("/media/tgt/etc/system.ini", txt, (u64)str_len(txt));
	if (keyb[0]) {
		txt[0] = 0;
		str_cat(str_cat(str_cat(txt, "keyb="), keyb), "\n");
		wr_file("/media/tgt/etc/options.txt", txt, (u64)str_len(txt));
	}
	if (rd_file("/etc/mounts.txt", tmp, sizeof tmp) < 0)
		tmp[0] = 0;
	txt[0] = 0;
	str_cat(str_cat(txt, tmp), "\n# osinstall : /boot (la partition du chargeur), à la demande\nBOOT = blk.boot, fatfs, /, ondemand\n");
	if (home)
		str_cat(str_cat(str_cat(txt + str_len(txt), "/home = blk.home, "), fsvc_home), "\n");
	wr_file("/media/tgt/etc/mounts.txt", txt, (u64)str_len(txt));
	/* le dossier de l'utilisateur */
	char hd[96];
	join(hd, home ? "/media/tgthome" : "/media/tgt/home", user);
	sys_mkdir(hd);
	static const char *const sub[] = {"Documents", "Images", "Musique", "Vidéos", "Téléchargements", 0};
	for (int i = 0; sub[i]; i++) {
		char s[160];
		sys_mkdir(join(s, hd, sub[i]));
	}
	prog(88, "Comptes et réglages écrits");

	/* 5. démarrable : local.tar (init.txt, nvme.txt), boot.cfg (firmware = install) */
	step(5, "Rendre Korin démarrable");
	txt[0] = 0;
	str_cat(txt, "# init.txt de cette machine (osinstall, dans \\korin\\local.tar) : / par son GUID (nvme.txt)\n"
		     "pcid -n\nnvme\nwait blk.root 30000\nvfs / blk.root fatfs\n");
	tar_add("init.txt", txt);
	txt[0] = 0;
	str_cat(txt, "# nvme.txt de cette machine (osinstall) : ses partitions par leur GUID ; le reste en lecture seule\n");
	str_cat(str_cat(str_cat(txt + str_len(txt), "name = root, guid="), gr), "\n");
	str_cat(str_cat(str_cat(txt + str_len(txt), "write = guid="), gr), "\n");
	str_cat(str_cat(str_cat(txt + str_len(txt), "write = guid="), gb), ", type=efi\n"); /* (l'EFI : /boot) */
	if (home) {
		str_cat(str_cat(str_cat(txt + str_len(txt), "name = home, guid="), gh), "\n");
		str_cat(str_cat(str_cat(txt + str_len(txt), "write = guid="), gh), "\n");
	}
	tar_add("nvme.txt", txt);
	memset(tarb + tarn, 0, 1024); /* deux blocs nuls : la fin */
	tarn += 1024;
	if ((r = wr_file("/media/tgtboot/korin/local.tar", tarb, tarn)) < 0)
		return fail(r, "local.tar");
	int windows = 0;
	for (int i = 0; i < GPT_ENTRIES; i++)
		if (!wipe && gpt_used(&g.e[i]) && !memcmp(g.e[i].type, GPT_T_BASIC, 16) && i + 1 != nroot && i + 1 != nboot &&
		    i + 1 != nhome)
			windows = 1;
	char to[16];
	ini("system", "timeout", windows ? "5" : "0", to, sizeof to);
	txt[0] = 0;
	str_cat(txt, "# menu de démarrage (osinstall) ; firmware = install : ce chargeur crée son entrée du firmware\n"
		     "default = korin\ntimeout = ");
	str_cat(str_cat(txt + str_len(txt), to), "\nfirmware = install\n");
	if ((r = wr_file("/media/tgtboot/korin/boot.cfg", txt, (u64)str_len(txt))) < 0)
		return fail(r, "boot.cfg");
	(void)ghead;
	prog(95, "Chargeur prêt");
	vu_sync("tgt");
	vu_sync("tgtboot");
	vu_unmount("tgtboot");
	vu_unmount("tgt");
	if (home) {
		vu_sync("tgthome");
		vu_unmount("tgthome");
	}
	prog(100, "Terminé");
	out("OK\n");
	return 0;
}
