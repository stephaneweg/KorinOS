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
 *                           K système home version comptes                 (un Korin déjà installé : les numéros de
 *                                                                          KORIN OS et de KORIN HOME, 0 sans ; la
 *                                                                          version de pc et les comptes, « ? » s'ils
 *                                                                          ne se lisent pas)
 *
 * PLAN (Setup l'écrit dans /tmp/install/plan.ini) :
 *   [disk]   device = blk.nvme0 ; mode = beside (à côté de l'existant) | wipe (le disque entier) ;
 *            delete = 3, 4 (beside : les entrées à supprimer d'abord, numéros de la GPT) ;
 *            place = PREMIER-DERNIER (secteurs : la place de Korin, libre après les suppressions ; défaut : le plus
 *            grand espace libre) ; root_fs = ext4 (ext4fs sert / dès init.txt) ; home = no | yes ; root_gb = 64
 *            (avec home) ; home_fs = ext4 ; boot_mb = 512 (une EFI neuve) ; min_gb = 10 (place exigée). / et /home
 *            sont toujours en ext4 (décision du 25/11/2026 : FAT32 et exFAT sont réservés aux disques amovibles) ;
 *            la réinstallation garde le système de fichiers de KORIN OS et de KORIN HOME
 *            Des partitions choisies (beside, décision du 17/11/2026 soir) : root_part = 3 (une partition existante,
 *            formatée en root_fs : /) ou root_place = PREMIER-DERNIER (un espace libre, une partition neuve) ;
 *            home_part = 4 ou home_place = PREMIER-DERNIER (/home, formatée en home_fs) ; sans l'un ni l'autre, /home
 *            reste dans /. Elles sont renommées « KORIN OS » et « KORIN HOME » ; une partition EFI doit exister.
 *            mode = reinstall ; root = 3 ; home_part = 4 (0 : pas de KORIN HOME) : la réinstallation (décision du
 *            17/11/2026) : avec KORIN HOME, KORIN OS est reformatée (même système de fichiers) et KORIN HOME n'est pas
 *            touchée ; sans, KORIN OS n'est pas formatée : tout y est effacé sauf /home (et les points de montage).
 *            Repris de l'ancien système : etc/users.ini, shadow.ini, elevate.ini, wifi.txt, options.txt, et de
 *            system.ini le nom, la langue, l'ouverture automatique ; [user] est ignoré, les logiciels ne sont pas repris
 *   [system] hostname, lang (fr, en), keyb (BE, FR, US...), timeout (secondes du menu ; défaut 5 si Windows, 0)
 *   [user]   name (uid 1000), fullname, autologin = yes|no, admin = yes|no, passwords = stdin|none ;
 *            hash, root_hash : les empreintes déjà faites (Setup : « pbkdf2-sha256$... », jamais le mot de passe) ;
 *            osinstall les retire du fichier dès qu'il l'a lu (plan_scrub : /tmp n'a pas de droits par fichier)
 *   [packages] online = yes : l'installateur a l'index en ligne vérifié (/var/pkg/index.txt de la clé, « pkg check ») :
 *            un paquet choisi vient de la clé si elle l'a dans la même version ou plus récente, sinon du dépôt en ligne
 *            (« pkg --root /media/tgt add NOM » : la racine du disque installé, 18/11/2026) ;
 *            list = jet courier ... (les paquets choisis : ceux de la copie du système y sont déjà ; ceux dont
 *            l'archive est dans le dépôt de la clé, /packages/pc, sont installés depuis elle (/bin/unzip, puis leur
 *            fiche var/pkg/db/<nom>.ini avec le SHA-256 de chaque fichier, comme mkrepo.py) ; les autres sont notés
 *            dans /var/pkg/wanted.txt de la cible, que Logiciels installera)
 * Entrée standard (passwords = stdin) : le mot de passe de l'utilisateur, puis celui de root (deux lignes ; vides : aucun ; root vide
 * et admin : celui de l'utilisateur). Jamais écrits en clair : seules leurs empreintes (authd/pwhash.h).
 *
 * Le disque (décision du 16/11/2026) : toujours une partition EFI, celle qui existe (gardée, jamais formatée : le
 * chargeur va dans \EFI\Korin, le noyau, l'initrd, local.tar et boot.cfg dans \korin ; rien d'autre n'y est
 * touché), sinon une neuve de boot_mb au début de la place (avec aussi \EFI\BOOT\BOOTX64.EFI) ; puis KORIN OS
 * (/, ext4) et KORIN HOME (/home, ext4) si demandé. L'EFI est /boot (blk.boot : le volume du chargeur). Étapes (sur la sortie, pour Setup) :
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

/* l'aléa des sels et des GUID : un appel qui échoue laissait un sel nul ou un GUID prévisible (B10) ; on
 * arrête plutôt l'installation */
static void rnd(void *b, u64 n)
{
	for (int i = 0; i < 3; i++)
		if (korin_sys2(SYS_GETRANDOM, (long)b, (long)n) == (long)n)
			return;
	static const char m[] = "osinstall : pas d'alea (SYS_GETRANDOM), installation arretee\n";
	sys_write(2, m, sizeof m - 1);
	_exit(1);
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

/* le plan relu sans ses lignes « hash = », « root_hash = » (les empreintes des mots de passe : seul osinstall les lit,
 * gardées dans plan ; /tmp n'a pas de droits par fichier), s'il est sous /tmp (un plan d'essai reste tel quel) */
static void plan_scrub(const char *path)
{
	static char t[sizeof plan];
	if (strncmp(path, "/tmp/", 5))
		return;
	char *o = t;
	int found = 0;
	for (const char *p = plan; *p;) {
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		if (*e)
			e++;
		const char *s = p;
		while (s < e && (*s == ' ' || *s == '\t'))
			s++;
		int drop = 0;
		static const char *const k[] = {"hash", "root_hash", 0};
		for (int i = 0; k[i] && !drop; i++) {
			u64 kl = (u64)str_len(k[i]);
			const char *v = s + kl;
			if ((u64)(e - s) > kl && !memcmp(s, k[i], kl)) {
				while (v < e && (*v == ' ' || *v == '\t'))
					v++;
				drop = v < e && *v == '=';
			}
		}
		if (drop)
			found = 1;
		else {
			memcpy(o, p, (u64)(e - p));
			o += e - p;
		}
		p = e;
	}
	if (found)
		wr_file(path, t, (u64)(o - t));
	memset(t, 0, sizeof t);
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
	str_cat(str_cat(sect + 1, name), "]"); /* (str_cat copie à l'adresse donnée et rend la fin) */
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

/* la valeur de « clé = ... » dans la section [name] d'un index -> out (vide s'il n'y en a pas) */
static void idx_val(const char *idx, const char *name, const char *key, char *out, u64 cap)
{
	out[0] = 0;
	char sect[64] = "[";
	str_cat(str_cat(sect + 1, name), "]");
	u64 sl = (u64)str_len(sect), kl = (u64)str_len(key);
	for (const char *p = idx; *p;) {
		if (!memcmp(p, sect, sl) && (p[sl] == '\n' || p[sl] == '\r')) {
			for (const char *l = p + sl; *l && *l != '[';) {
				while (*l == '\n' || *l == '\r')
					l++;
				if (!memcmp(l, key, kl) && (l[kl] == ' ' || l[kl] == '=')) {
					const char *v = l + kl;
					while (*v == ' ' || *v == '=')
						v++;
					u64 n = 0;
					while (v[n] && v[n] != '\n' && v[n] != '\r' && n + 1 < cap) {
						out[n] = v[n];
						n++;
					}
					out[n] = 0;
					return;
				}
				while (*l && *l != '\n')
					l++;
			}
			return;
		}
		while (*p && *p != '\n')
			p++;
		if (*p)
			p++;
	}
}

/* deux versions « 2026.11.47 », « 1.3.2 » : < 0, 0, > 0 (nombre par nombre) */
static int vcmp(const char *a, const char *b)
{
	while (*a || *b) {
		u64 x = 0, y = 0;
		while (*a >= '0' && *a <= '9')
			x = x * 10 + (u64)(*a++ - '0');
		while (*b >= '0' && *b <= '9')
			y = y * 10 + (u64)(*b++ - '0');
		if (x != y)
			return x < y ? -1 : 1;
		while (*a && (*a < '0' || *a > '9'))
			a++;
		while (*b && (*b < '0' || *b > '9'))
			b++;
	}
	return 0;
}

static char onidx[262144]; /* l'index en ligne vérifié (/var/pkg/index.txt de la clé) */

/* installe un paquet de la clé dans la cible -> 0, ou < 0. Par pkg (E52, B8) : le dépôt hors ligne de la clé
 * (/packages/pc : index.txt, index.sig, pkgs/), sa signature (la clé compilée dans pkg) et le SHA-256 de l'archive
 * vérifiés, la fiche de /media/tgt/var/pkg/db écrite par pkg ; avant, l'archive était décompressée sans contrôle */
static long key_install(const char *name, const char *opk)
{
	(void)opk; /* (pkg la retrouve par l'index) */
	static char args[300], db[160];
	args[0] = 0;
	str_cat(str_cat(args, "--root /media/tgt -r /packages/pc add "), name);
	long r = run("/bin/pkg", args);
	join(db, "/media/tgt/var/pkg/db", name);
	str_cat(db + str_len(db), ".ini");
	long f = sys_open(db, O_READ); /* (installé : sa fiche est là) */
	if (f < 0)
		return r < 0 ? r : E_IO;
	sys_close(f);
	return 0;
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

static void list_korin(const char *dev);

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
		list_korin(dev);
	}
	return 0;
}

/* le service de fichiers de / (fatfs pour FAT32 et exFAT, ext4fs pour ext4) : le montage de la cible, init.txt */
static char rootsvc[8] = "fatfs";
static const char *svc_of(const char *fs) { return str_eq(fs, "ext4") || str_eq(fs, "ext") ? "ext4fs" : "fatfs"; }

/* ---- un Korin déjà installé (--list : la ligne K ; la réinstallation) */

static int name_is(const struct gpt_entry *e, const char *n)
{
	int k = 0;
	for (; n[k]; k++)
		if (e->name[k] != (u16)(u8)n[k])
			return 0;
	return k < 36 ? e->name[k] == 0 : 1;
}

/* les lignes « clé=... » d'un fichier de réglages qui commencent par l'un des mots, mises bout à bout dans o */
static void pick_lines(const char *text, const char *const *keys, char *o, u64 cap)
{
	u64 n = (u64)str_len(o);
	for (const char *l = text; *l;) {
		const char *e = l;
		while (*e && *e != '\n')
			e++;
		for (int i = 0; keys[i]; i++) {
			u64 kl = (u64)str_len(keys[i]);
			if (!memcmp(l, keys[i], kl) && n + (u64)(e - l) + 2 < cap) {
				memcpy(o + n, l, (u64)(e - l));
				n += (u64)(e - l);
				o[n++] = '\n';
				o[n] = 0;
				break;
			}
		}
		l = *e ? e + 1 : e;
	}
}

/* les comptes de users.ini (uid 1000 et plus), séparés par des virgules */
static void users_of(const char *ini_text, char *o, u64 cap)
{
	char cur[40] = "";
	o[0] = 0;
	for (const char *l = ini_text; *l;) {
		const char *e = l;
		while (*e && *e != '\n')
			e++;
		while (*l == ' ')
			l++;
		if (*l == '[') {
			u64 k = 0;
			for (const char *c = l + 1; c < e && *c != ']' && k < sizeof cur - 1; c++)
				cur[k++] = *c;
			cur[k] = 0;
		} else if (!memcmp(l, "uid", 3) && cur[0]) {
			const char *v = l + 3;
			while (*v == ' ' || *v == '=')
				v++;
			if (parse_dec(v) >= 1000 && (u64)str_len(o) + (u64)str_len(cur) + 2 < cap) {
				if (o[0])
					str_cat(o + str_len(o), ",");
				str_cat(o + str_len(o), cur);
			}
		}
		l = *e ? e + 1 : e;
	}
}

/* --list : « K système home version comptes » pour un disque qui porte KORIN OS (monté en lecture le temps de lire) */
static void list_korin(const char *dev)
{
	int nr = 0, nh = 0;
	for (int i = 0; i < GPT_ENTRIES; i++)
		if (gpt_used(&g.e[i])) {
			if (!nr && name_is(&g.e[i], "KORIN OS"))
				nr = i + 1;
			else if (!nh && name_is(&g.e[i], "KORIN HOME"))
				nh = i + 1;
		}
	if (!nr)
		return;
	char ver[40] = "?", us[200] = "?", pr[40];
	part_name(pr, dev, nr);
	if (vu_mount("oldkorin", pr, svc_of(content_word(g.e[nr - 1].first))) >= 0) {
		if (rd_file("/media/oldkorin/var/pkg/db/pc.ini", tmp, sizeof tmp) >= 0) {
			static const char *const k[] = {"version", 0};
			char l[128] = "";
			pick_lines(tmp, k, l, sizeof l);
			const char *v = l + 7;
			while (*v == ' ' || *v == '=')
				v++;
			u64 j = 0;
			while (v[j] && v[j] != '\n' && v[j] != '\r' && j < sizeof ver - 1)
				j++;
			if (j) {
				memcpy(ver, v, j);
				ver[j] = 0;
			}
		}
		if (rd_file("/media/oldkorin/etc/users.ini", tmp, sizeof tmp) >= 0) {
			users_of(tmp, us, sizeof us);
			if (!us[0])
				str_cat(us, "?");
		}
		vu_unmount("oldkorin");
	}
	out("K ");
	out_num((u64)nr);
	out(" ");
	out_num((u64)nh);
	out(" ");
	out(ver);
	out(" ");
	out(us);
	out("\n");
}

/* la raison d'un refus du plan : la dernière ligne « plan refusé : ... » du pilote, dans le journal du noyau */
static int plan_refused(long r)
{
	static char why[200];
	why[0] = 0;
	str_cat(why, "le pilote du disque refuse le plan");
	long n = korin_sys2(SYS_KLOG, cbuf, sizeof cbuf - 1);
	if (n > 0) {
		cbuf[n] = 0;
		const char *last = 0;
		for (const char *c = (const char *)cbuf; *c; c++)
			if (!memcmp(c, "plan refus", 10))
				last = c;
		if (last) {
			u64 k = 0;
			while (last[k] && last[k] != '\n' && k < 150)
				k++;
			char *w = why + str_len(why);
			*w++ = ' ';
			*w++ = '(';
			memcpy(w, last, k);
			w[k] = ')';
			w[k + 1] = 0;
		}
	}
	return fail(r, why);
}

/* ce que la réinstallation reprend de l'ancien système */
static char old_users[8192], old_shadow[8192], old_elevate[2048], old_wifi[8192], old_options[1024], old_sys[512];

static void rd_or_empty(const char *path, char *buf, u64 cap)
{
	if (rd_file(path, buf, cap) < 0)
		buf[0] = 0;
}

/* tout ce que contient path, puis path (s'il n'est pas la racine du volume) ; plusieurs passes : effacer en lisant
 * le dossier peut sauter des entrées */
static void rm_tree(const char *path, int self)
{
	for (int pass = 0; pass < 8; pass++) {
		long fd = sys_open(path, O_READ);
		if (fd < 0)
			break;
		struct dirent e;
		int gone = 0;
		while (sys_readdir(fd, &e) > 0) {
			if (str_eq(e.name, ".") || str_eq(e.name, ".."))
				continue;
			char c[512];
			join(c, path, e.name);
			if (e.attr & ATTR_DIR)
				rm_tree(c, 1);
			else if (sys_unlink(c) >= 0)
				gone++;
			else
				err_files++;
		}
		sys_close(fd);
		if (!gone)
			break;
	}
	if (self && sys_unlink(path) < 0)
		err_files++;
}

/* sans KORIN HOME : la racine de l'ancien système vidée, sauf /home et les points de montage */
static void wipe_root(const char *root)
{
	static const char *const keep[] = {"home", "media", "net", "tmp", "boot", "System Volume Information", 0};
	for (int pass = 0; pass < 4; pass++) {
		long fd = sys_open(root, O_READ);
		if (fd < 0)
			return;
		struct dirent e;
		int gone = 0;
		while (sys_readdir(fd, &e) > 0) {
			int k = str_eq(e.name, ".") || str_eq(e.name, "..");
			for (int i = 0; keep[i] && !k; i++)
				k = str_eq(e.name, keep[i]);
			if (k)
				continue;
			char c[512];
			join(c, root, e.name);
			if (e.attr & ATTR_DIR)
				rm_tree(c, 1);
			else if (sys_unlink(c) < 0)
				err_files++;
			gone++;
		}
		sys_close(fd);
		if (!gone)
			return;
	}
}

/* « PREMIER-DERNIER » -> 1 (a, b alignés au Mio : début vers le haut, fin vers le bas) */
static int place_of(const char *v, u64 *a, u64 *b)
{
	if (!*v)
		return 0;
	*a = parse_dec(v);
	while (*v && *v != '-')
		v++;
	*b = *v ? parse_dec(v + 1) : 0;
	*a = (*a + MIB - 1) / MIB * MIB;
	if ((*b + 1) / MIB * MIB - 1 > *a)
		*b = (*b + 1) / MIB * MIB - 1;
	return *b > *a;
}

/* 1. le disque, avec des partitions choisies (root_part / root_place, home_part / home_place) : les suppressions, le
 * plan, la table (noms, types, partitions neuves), les formatages ; KORIN OS sur /media/tgt, l'EFI sur /media/tgtboot,
 * /home sur /media/tgthome -> 0, ou fail() */
static int parts_disk(const char *dev, u32 crc, const char *rootfs, const char *homefs, u64 min_gb, int *home, int *nboot,
		      int *nroot, int *nhome, int *esp, u8 *gboot, u8 *groot, u8 *ghome)
{
	char v[64];
	const char *d = ini("disk", "delete", "", v, sizeof v);
	while (*d) {
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
	for (int i = 0; i < GPT_ENTRIES && *esp < 0; i++)
		if (gpt_used(&g.e[i]) && !memcmp(g.e[i].type, GPT_T_EFI, 16))
			*esp = i;
	if (*esp < 0)
		return fail(E_INVAL, "partitions choisies : il faut une partition EFI (gardée)");
	int rp = (int)parse_dec(ini("disk", "root_part", "0", v, sizeof v));
	int hp = (int)parse_dec(ini("disk", "home_part", "0", v, sizeof v));
	u64 ra = 0, rb = 0, ha = 0, hb = 0;
	int rpl = !rp && place_of(ini("disk", "root_place", "", v, sizeof v), &ra, &rb);
	int hpl = !hp && place_of(ini("disk", "home_place", "", v, sizeof v), &ha, &hb);
	if (rp && (rp > GPT_ENTRIES || !gpt_used(&g.e[rp - 1])))
		return fail(E_INVAL, "la partition choisie pour / n'existe pas");
	if (hp && (hp > GPT_ENTRIES || hp == rp || !gpt_used(&g.e[hp - 1])))
		return fail(E_INVAL, "la partition choisie pour /home n'existe pas");
	if (!rp && !rpl)
		return fail(E_INVAL, "partitions choisies : pas de place pour /");
	for (int i = 0; i < GPT_ENTRIES; i++) { /* (ni EFI, ni MSR, ni récupération ; une place libre l'est vraiment) */
		const struct gpt_entry *e = &g.e[i];
		if (!gpt_used(e))
			continue;
		if ((i + 1 == rp || i + 1 == hp) && (!memcmp(e->type, GPT_T_EFI, 16) || !memcmp(e->type, GPT_T_MSR, 16) ||
						     !memcmp(e->type, GPT_T_RECOVERY, 16)))
			return fail(E_INVAL, "une partition EFI, MSR ou de récupération ne peut recevoir ni / ni /home");
		if ((rpl && e->first <= rb && e->last >= ra) || (hpl && e->first <= hb && e->last >= ha))
			return fail(E_INVAL, "la place choisie n'est pas libre");
	}
	if (rp) {
		ra = g.e[rp - 1].first;
		rb = g.e[rp - 1].last;
	}
	if (hp) {
		ha = g.e[hp - 1].first;
		hb = g.e[hp - 1].last;
	}
	if (rb - ra + 1 < min_gb * 1024 * MIB)
		return fail(E_NOSPC, "la place choisie pour / est trop petite");
	*home = hp || hpl;
	logl(*home ? "plan : / et /home sur les partitions choisies, la partition EFI gardée"
		   : "plan : / sur la partition choisie, la partition EFI gardée");
	u64 ranges[6] = {ra, rb, g.e[*esp].first, g.e[*esp].last, ha, hb};
	long r = plan_send(PLAN_ESP, crc, ranges, *home ? 3 : 2);
	if (r < 0)
		return plan_refused(r);
	/* la table : les partitions choisies prennent leur nom (et leur type), les places libres deviennent des partitions */
	*nboot = *esp + 1;
	memcpy(gboot, g.e[*esp].guid, 16);
	guid_rnd(groot);
	guid_rnd(ghome);
	if (rp) {
		*nroot = rp;
		gpt_name_set(&g.e[rp - 1], "KORIN OS");
		memcpy(g.e[rp - 1].type, str_eq(rootfs, "ext4") ? GPT_T_LINUX : GPT_T_BASIC, 16);
		memcpy(groot, g.e[rp - 1].guid, 16);
	} else {
		*nroot = gpt_add(&g, str_eq(rootfs, "ext4") ? GPT_T_LINUX : GPT_T_BASIC, ra, rb, "KORIN OS", groot);
	}
	const u8 *ht = str_eq(homefs, "ext4") ? GPT_T_LINUX : GPT_T_BASIC;
	if (hp) {
		*nhome = hp;
		gpt_name_set(&g.e[hp - 1], "KORIN HOME");
		memcpy(g.e[hp - 1].type, ht, 16);
		memcpy(ghome, g.e[hp - 1].guid, 16);
	} else if (hpl) {
		*nhome = gpt_add(&g, ht, ha, hb, "KORIN HOME", ghome);
	}
	if (*nroot <= 0 || (*home && *nhome <= 0))
		return fail(E_NOSPC, "la table est pleine");
	if ((r = gpt_write(&g, disk_wr, 0)) < 0)
		return fail(r, "écriture de la table");
	if ((r = blk(dchan, BLK_RESCAN, 0, 0, 0)) < 0)
		return fail(r, "relecture de la table");
	prog(3, "Table des partitions écrite");
	char pboot[40], proot[40], phome[40], args[128];
	part_name(pboot, dev, *nboot);
	part_name(proot, dev, *nroot);
	part_name(phome, dev, *nhome);
	args[0] = 0;
	str_cat(str_cat(str_cat(str_cat(args, "-t "), rootfs), " -y -L KORINOS "), proot);
	if ((r = run("/sbin/mkfs", args)) != 0)
		return fail(r, "formatage de KORIN OS");
	prog(6, "KORIN OS formatée");
	if (*home) {
		args[0] = 0;
		str_cat(str_cat(str_cat(str_cat(args, "-t "), homefs), " -y -L KORINHOME "), phome);
		if ((r = run("/sbin/mkfs", args)) != 0)
			return fail(r, "formatage de KORIN HOME");
		prog(8, "KORIN HOME formatée");
	}
	if ((r = vu_mount("tgt", proot, rootsvc)) < 0)
		return fail(r, "montage de KORIN OS");
	if ((r = vu_mount("tgtboot", pboot, "fatfs")) < 0)
		return fail(r, "montage de la partition EFI");
	if (vu_info("tgtboot") >= 0 && ((u64 *)vu_rep)[MSG_B / 8] < (16ull << 20))
		return fail(E_NOSPC, "la partition EFI a moins de 16 Mo libres (le noyau et l'initrd)");
	if (*home && (r = vu_mount("tgthome", phome, str_eq(homefs, "ext4") ? "ext4fs" : "fatfs")) < 0)
		return fail(r, "montage de KORIN HOME");
	return 0;
}

/* 1. le disque, pour une réinstallation : le plan (KORIN OS et l'EFI), l'ancien système lu, puis KORIN OS reformatée
 * (avec KORIN HOME) ou vidée sauf /home ; KORIN OS sur /media/tgt, l'EFI sur /media/tgtboot -> 0, ou fail() */
static int reinstall_disk(const char *dev, u32 crc, int *nboot, int *nroot, int *nhome, int *esp, u8 *gboot, u8 *groot,
			  u8 *ghome, const char **fsvc_home)
{
	char v[64];
	int nr = (int)parse_dec(ini("disk", "root", "0", v, sizeof v));
	int nh = (int)parse_dec(ini("disk", "home_part", "0", v, sizeof v));
	if (nr < 1 || nr > GPT_ENTRIES || !gpt_used(&g.e[nr - 1]))
		return fail(E_INVAL, "réinstallation : la partition KORIN OS n'existe pas");
	if (nh && (nh > GPT_ENTRIES || nh == nr || !gpt_used(&g.e[nh - 1])))
		return fail(E_INVAL, "réinstallation : la partition KORIN HOME n'existe pas");
	for (int i = 0; i < GPT_ENTRIES && *esp < 0; i++)
		if (gpt_used(&g.e[i]) && !memcmp(g.e[i].type, GPT_T_EFI, 16))
			*esp = i;
	if (*esp < 0)
		return fail(E_INVAL, "réinstallation : pas de partition EFI");
	const struct gpt_entry *er = &g.e[nr - 1];
	char rootfs[16] = "";
	str_cat(rootfs, content_word(er->first));
	if (str_eq(rootfs, "ext"))
		rootfs[3] = '4', rootfs[4] = 0; /* (« ext » : reformatée en ext4) */
	if (!str_eq(rootfs, "fat32") && !str_eq(rootfs, "exfat") && !str_eq(rootfs, "ext4"))
		return fail(E_INVAL, "réinstallation : KORIN OS n'est ni en FAT32, ni en exFAT, ni en ext4");
	rootsvc[0] = 0;
	str_cat(rootsvc, svc_of(rootfs));
	if (nh)
		*fsvc_home = str_eq(content_word(g.e[nh - 1].first), "ext") ? "ext4fs" : "fatfs";
	logl(nh ? "réinstallation : KORIN OS reformatée, KORIN HOME gardée"
		: "réinstallation sans formater : tout est remplacé sauf /home");
	u64 ranges[4] = {er->first, er->last, g.e[*esp].first, g.e[*esp].last};
	long r = plan_send(PLAN_ESP, crc, ranges, 2);
	if (r < 0)
		return plan_refused(r);
	if ((r = blk(dchan, BLK_RESCAN, 0, 0, 0)) < 0)
		return fail(r, "relecture de la table");
	*nboot = *esp + 1;
	*nroot = nr;
	*nhome = nh;
	memcpy(gboot, g.e[*esp].guid, 16);
	memcpy(groot, er->guid, 16);
	if (nh)
		memcpy(ghome, g.e[nh - 1].guid, 16);
	char pboot[40], proot[40], args[128];
	part_name(pboot, dev, *nboot);
	part_name(proot, dev, nr);
	if ((r = vu_mount("tgt", proot, rootsvc)) < 0)
		return fail(r, "montage de l'ancien KORIN OS");
	rd_or_empty("/media/tgt/etc/users.ini", old_users, sizeof old_users);
	rd_or_empty("/media/tgt/etc/shadow.ini", old_shadow, sizeof old_shadow);
	rd_or_empty("/media/tgt/etc/elevate.ini", old_elevate, sizeof old_elevate);
	/* (avec le coffre à secrets du PC, wifi.txt n'a plus les clés des réseaux : elles sont dans le coffre de l'ancien
	   système, /var/keyvault, qui ne se reprend pas ici ; le nouveau système redemandera les mots de passe du Wi-Fi.
	   De même pour l'accès à distance, smbfs, smbd et les clés de webd, qui ne sont pas repris) */
	rd_or_empty("/media/tgt/etc/wifi.txt", old_wifi, sizeof old_wifi);
	rd_or_empty("/media/tgt/etc/options.txt", old_options, sizeof old_options);
	old_sys[0] = 0;
	if (rd_file("/media/tgt/etc/system.ini", tmp, sizeof tmp) >= 0) {
		static const char *const k[] = {"hostname=", "lang=", "autologin=", 0};
		pick_lines(tmp, k, old_sys, sizeof old_sys);
	}
	if (!old_users[0])
		return fail(E_NOENT, "réinstallation : pas de comptes dans l'ancien système (etc/users.ini)");
	prog(3, "Réglages de l'ancien système lus");
	if (nh) {
		vu_sync("tgt");
		vu_unmount("tgt");
		args[0] = 0;
		str_cat(str_cat(str_cat(str_cat(args, "-t "), rootfs), " -y -L KORINOS "), proot);
		if ((r = run("/sbin/mkfs", args)) != 0)
			return fail(r, "formatage de KORIN OS");
		if ((r = vu_mount("tgt", proot, rootsvc)) < 0)
			return fail(r, "montage de KORIN OS");
		prog(8, "KORIN OS formatée");
	} else {
		wipe_root("/media/tgt");
		vu_sync("tgt");
		prog(8, "Ancien système effacé, /home gardé");
	}
	if ((r = vu_mount("tgtboot", pboot, "fatfs")) < 0)
		return fail(r, "montage de la partition EFI");
	if (vu_info("tgtboot") >= 0 && ((u64 *)vu_rep)[MSG_B / 8] < (16ull << 20))
		return fail(E_NOSPC, "la partition EFI a moins de 16 Mo libres (le noyau et l'initrd)");
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
	plan_scrub(argv[1]); /* (les empreintes : plus dans le fichier) */
	char dev[32], mode[16], rootfs[16], homefs[16], v[64];
	ini("disk", "device", "blk.nvme0", dev, sizeof dev);
	ini("disk", "mode", "beside", mode, sizeof mode);
	ini("disk", "root_fs", "ext4", rootfs, sizeof rootfs);
	ini("disk", "home_fs", "ext4", homefs, sizeof homefs);
	int wipe = str_eq(mode, "wipe"), reinst = str_eq(mode, "reinstall");
	int home = yes(ini("disk", "home", "no", v, sizeof v));
	u64 root_gb = parse_dec(ini("disk", "root_gb", "64", v, sizeof v));
	u64 boot_mb = parse_dec(ini("disk", "boot_mb", "512", v, sizeof v));
	u64 min_gb = parse_dec(ini("disk", "min_gb", "10", v, sizeof v));
	if (!reinst && !str_eq(rootfs, "ext4")) /* (la réinstallation lit celui de KORIN OS : reinstall_disk) */
		return fail(E_INVAL, "root_fs : ext4 (FAT32 et exFAT : disques amovibles seulement)");
	rootsvc[0] = 0;
	str_cat(rootsvc, svc_of(rootfs));
	if (home && !reinst && !str_eq(homefs, "ext4"))
		return fail(E_INVAL, "home_fs : ext4 (FAT32 et exFAT : disques amovibles seulement)");
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
	{ /* un compte qui administre a un mot de passe (décision du 25/11/2026) ; une réinstallation garde les comptes */
		char hv[8];
		if (admin && !upw[0] && !ini("user", "hash", "", hv, sizeof hv)[0] && !reinst)
			return fail(E_INVAL, "un compte administrateur doit avoir un mot de passe (passwords = stdin, ou hash)");
	}

	/* 1. le disque */
	step(1, "Préparer le disque");
	u64 sectors;
	long r = disk_open(dev, &sectors);
	if (r < 0)
		return fail(r, "le disque ne répond pas");
	int had_table = gpt_read(&g, disk_rd, 0, sectors) == 0 && !g.from_backup;
	u32 crc = had_table ? g.header_crc : 0;
	int esp = -1; /* l'entrée EFI gardée (rang), -1 : une neuve */
	int nboot = 0, nroot = 0, nhome = 0;
	u8 gboot[16], groot[16], ghome[16];
	const char *fsvc_home = str_eq(homefs, "ext4") ? "ext4fs" : "fatfs";
	if (reinst) {
		if (!had_table)
			return fail(E_INVAL, "réinstallation : pas de table GPT lisible");
		int rr = reinstall_disk(dev, crc, &nboot, &nroot, &nhome, &esp, gboot, groot, ghome, &fsvc_home);
		if (rr)
			return rr;
		home = nhome > 0;
	} else if (!wipe && (str_eq(ini("disk", "root_part", "", v, sizeof v), "") == 0 ||
			     str_eq(ini("disk", "root_place", "", v, sizeof v), "") == 0)) {
		if (!had_table)
			return fail(E_INVAL, "partitions choisies : pas de table GPT lisible");
		int rr = parts_disk(dev, crc, rootfs, homefs, min_gb, &home, &nboot, &nroot, &nhome, &esp, gboot, groot, ghome);
		if (rr)
			return rr;
	} else {
	u64 a, b;
	if (!wipe) {
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
		/* tout supprimé (EFI, MSR et récupération comprises) : c'est le disque entier, que le pilote n'accepte qu'ainsi
		 * (une plage « à côté » qui touche une EFI, une MSR ou une récupération est refusée) ; table et EFI neuves */
		int reste = 0;
		for (int i = 0; i < GPT_ENTRIES; i++)
			reste |= gpt_used(&g.e[i]);
		if (!reste) {
			wipe = 1;
			logl("toutes les partitions supprimées : le disque entier");
		}
	}
	if (wipe) {
		u8 dg[16];
		guid_rnd(dg);
		gpt_init(&g, sectors, dg);
		if (!gpt_free_space(&g, &a, &b))
			return fail(E_NOSPC, "disque trop petit");
	} else {
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
		return plan_refused(r);
	guid_rnd(gboot);
	guid_rnd(groot);
	guid_rnd(ghome);
	if (esp >= 0) {
		nboot = esp + 1;
		memcpy(gboot, g.e[esp].guid, 16);
	} else {
		nboot = gpt_add(&g, GPT_T_EFI, a, a + bsz - 1, "EFI system partition", gboot);
	}
	nroot = gpt_add(&g, str_eq(rootfs, "ext4") ? GPT_T_LINUX : GPT_T_BASIC, rfirst, rlast, "KORIN OS", groot);
	nhome = home ? gpt_add(&g, str_eq(homefs, "ext4") ? GPT_T_LINUX : GPT_T_BASIC, hfirst, b, "KORIN HOME", ghome)
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
	const char *fsvc_root = rootsvc;
	if ((r = vu_mount("tgt", proot, fsvc_root)) < 0)
		return fail(r, "montage de KORIN OS");
	if ((r = vu_mount("tgtboot", pboot, "fatfs")) < 0)
		return fail(r, "montage de la partition EFI");
	if (vu_info("tgtboot") >= 0 && ((u64 *)vu_rep)[MSG_B / 8] < (16ull << 20))
		return fail(E_NOSPC, "la partition EFI a moins de 16 Mo libres (le noyau et l'initrd)");
	if (home && (r = vu_mount("tgthome", phome, fsvc_home)) < 0)
		return fail(r, "montage de KORIN HOME");
	} /* (fin de l'installation neuve) */

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
		int online = yes(ini("packages", "online", "no", v, sizeof v)) && rd_file("/var/pkg/index.txt", onidx, sizeof onidx) > 0;
		if (online)
			logl("index en ligne : les paquets plus récents ou absents de la clé viennent du dépôt");
		u64 nnet = 0;
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
			str_cat(db + str_len(db), ".ini");
			long f = sys_open(db, O_READ);
			char kv[40] = "", ov[40] = "";
			int onkey = f < 0 && key_opk(s, opk, sizeof opk);
			if (onkey)
				idx_val(kidx, s, "version", kv, sizeof kv);
			if (online && f < 0)
				idx_val(onidx, s, "version", ov, sizeof ov);
			if (f >= 0) {
				sys_close(f);
			} else if (ov[0] && (!onkey || vcmp(kv, ov) < 0)) { /* en ligne : absent de la clé, ou plus récent */
				char m[96] = "depuis Internet : ", a[160] = "--root /media/tgt add ";
				str_cat(m + str_len(m), s);
				str_cat(a + str_len(a), s);
				prog(83, m);
				long rr = run("/bin/pkg", a);
				f = sys_open(db, O_READ); /* (installé : sa fiche est là) */
				if (f >= 0) {
					sys_close(f);
					nnet++;
				} else if (onkey && key_install(s, opk) >= 0) { /* (le dépôt ne répond pas : la version de la clé,
										     vérifiée de même) */
					logl(m);
				} else {
					char e[96] = "échec : ";
					str_cat(m + str_len(m), "");
					str_cat(e + str_len(e), s);
					logl(e);
					(void)rr;
					str_cat(str_cat(txt + str_len(txt), s), "\n");
					nw++;
				}
			} else if (onkey) {
				char m[96] = "depuis la clé : ";
				str_cat(m + str_len(m), s);
				prog(82, m);
				if (key_install(s, opk) < 0) { /* (pkg : index.sig de la clé et SHA-256 vérifiés, E52) */
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
		if (nnet) {
			char m[64] = "paquets installés depuis Internet : ";
			num(nnet, m + str_len(m));
			logl(m);
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
	if (reinst) { /* les comptes, les mots de passe, l'élévation, le Wi-Fi et le clavier de l'ancien système */
		if ((r = wr_file("/media/tgt/etc/users.ini", old_users, (u64)str_len(old_users))) < 0)
			return fail(r, "users.ini");
		if (old_shadow[0])
			wr_file("/media/tgt/etc/shadow.ini", old_shadow, (u64)str_len(old_shadow));
		if (old_elevate[0])
			wr_file("/media/tgt/etc/elevate.ini", old_elevate, (u64)str_len(old_elevate));
		if (old_wifi[0])
			wr_file("/media/tgt/etc/wifi.txt", old_wifi, (u64)str_len(old_wifi));
		if (old_options[0])
			wr_file("/media/tgt/etc/options.txt", old_options, (u64)str_len(old_options));
		if (rd_file("/etc/system.ini", tmp, sizeof tmp) < 0)
			tmp[0] = 0;
		txt[0] = 0;
		str_cat(str_cat(str_cat(txt, tmp), "\n# osinstall (réinstallation : repris de l'ancien système)\n"), old_sys);
		wr_file("/media/tgt/etc/system.ini", txt, (u64)str_len(txt));
		memset(upw, 0, sizeof upw);
		memset(rpw, 0, sizeof rpw);
		logl("comptes, mots de passe, Wi-Fi et clavier repris");
	} else {
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
	} /* (fin des réglages d'une installation neuve) */
	if (rd_file("/etc/mounts.txt", tmp, sizeof tmp) < 0)
		tmp[0] = 0;
	txt[0] = 0;
	str_cat(str_cat(txt, tmp), "\n# osinstall : /boot (la partition du chargeur), à la demande\nBOOT = blk.boot, fatfs, /, ondemand\n");
	if (home)
		str_cat(str_cat(str_cat(txt + str_len(txt), "/home = blk.home, "), fsvc_home), "\n");
	wr_file("/media/tgt/etc/mounts.txt", txt, (u64)str_len(txt));
	/* le dossier de l'utilisateur (une réinstallation garde ceux qui existent) */
	if (!reinst) {
		char hd[96];
		join(hd, home ? "/media/tgthome" : "/media/tgt/home", user);
		sys_mkdir(hd);
		static const char *const sub[] = {"Documents", "Images", "Musique", "Vidéos", "Téléchargements", 0};
		for (int i = 0; sub[i]; i++) {
			char s[160];
			sys_mkdir(join(s, hd, sub[i]));
		}
	}
	prog(88, "Comptes et réglages écrits");

	/* 5. démarrable : local.tar (init.txt, nvme.txt), boot.cfg (firmware = install) */
	step(5, "Rendre Korin démarrable");
	txt[0] = 0;
	str_cat(txt, "# init.txt de cette machine (osinstall, dans \\korin\\local.tar) : / par son GUID (nvme.txt)\n"
		     "pcid -n\nnvme\nwait blk.root 30000\nvfs / blk.root ");
	str_cat(str_cat(txt + str_len(txt), rootsvc), "\n");
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
	str_cat(txt, "# menu de démarrage (osinstall) ; la clé firmware (install) : ce chargeur crée son entrée du firmware, puis la passe à done\n"
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
	if (home && !reinst) {
		vu_sync("tgthome");
		vu_unmount("tgthome");
	}
	prog(100, "Terminé");
	out("OK\n");
	return 0;
}
