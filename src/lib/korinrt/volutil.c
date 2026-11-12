/* volutil.c : ce que partagent mount, eject et mkfs (docs/BIN-C.md), traduction de
 * src/inc/volutil.inc : le nom d'un volume, la liste de vfs (VFS_VOLUMES), les infos et la
 * synchronisation de son service (FS_INFO, FS_SYNC), sa source quand il n'est pas monté
 * (SD0:/etc/mounts.txt, ou blk.usbNp1), monter et démonter (avec un rootpath facultatif,
 * docs/FICHIERS.md), une taille en texte.
 *
 * Les tampons sont communs, comme volutil_data : vu_msg (la réponse de vfs : la liste reste
 * dans vu_msg + MSG_DATA jusqu'à la requête suivante), vu_rep (la réponse du service de
 * fichiers), vu_vol (« usb0 »), vu_aff (« USB0: »), vu_src, vu_srv. */
#include "korinrt.h"

u8 vu_msg[MSG_SIZE] __attribute__((aligned(8)));
u8 vu_rep[MSG_SIZE] __attribute__((aligned(8)));
char vu_vol[16];
char vu_aff[20];
char vu_src[256]; /* (32 dans l'asm : une source donnée plus longue débordait) */
char vu_srv[256]; /* (16 dans l'asm) */
char vu_root[64]; /* rootpath (vide : la racine de la source) */
static char vu_tmp[640]; /* (128 dans l'asm) */
static char vu_mounts[VU_MOUNTS];

/* argument (« usb0 », « USB0: », « usb0:/x ») -> vu_vol (« usb0 ») et vu_aff (« USB0: ») ;
 * longueur du nom (0 : aucun ; 14 au plus) (vu_nom) */
long vu_name(const char *arg)
{
	long n = 0;
	for (;;) {
		u8 c = (u8)arg[n];
		if (!c || c == ':' || c == '/' || n >= 14)
			break;
		if ((u8)(c - 'A') <= 25)
			c += 32;
		vu_vol[n] = (char)c;
		if ((u8)(c - 'a') <= 25)
			c -= 32;
		vu_aff[n] = (char)c;
		n++;
	}
	vu_vol[n] = 0;
	vu_aff[n] = ':';
	vu_aff[n + 1] = 0;
	return n;
}

/* -> nombre de volumes montés (entrées VU_ENT à vu_msg + MSG_DATA, VU_MAX au plus), < 0 si vfs
 * ne répond pas (vu_liste) */
long vu_list(void)
{
	vfs_msg(vu_msg, VFS_VOLUMES, 0, 0, "", 0);
	long r = vfs_call(vu_msg);
	if (r < 0)
		return r;
	u64 n = *(u64 *)(vu_msg + MSG_B);
	return n <= VU_MAX ? (long)n : VU_MAX;
}

/* nom en minuscules -> son entrée de VFS_VOLUMES (dans vu_msg), 0 s'il n'est pas monté
 * (vu_trouver) */
char *vu_find(const char *name)
{
	long n = vu_list();
	if (n <= 0)
		return 0;
	char *e = (char *)vu_msg + MSG_DATA;
	for (; n; n--, e += VU_ENT)
		if (str_eq(e, name))
			return e;
	return 0;
}

/* nom en minuscules, svc (16 o), root (64 o) -> 0 : svc le volume dont le service porte le nom
 * (« fs.<svc> »), root son rootpath (« / », « /korinARM ») ; < 0 : pas monté, ou vfs sans
 * VFS_VOLINFO */
long vu_volinfo(const char *name, char *svc, char *root)
{
	u8 m[MSG_SIZE] __attribute__((aligned(8)));
	vfs_msg(m, VFS_VOLINFO, 0, 0, name, 0);
	long r = vfs_call(m);
	if (r < 0)
		return r;
	m[MSG_SIZE - 1] = 0;
	const char *s = (const char *)m + MSG_DATA;
	long k = 0;
	for (; s[k] && k < 15; k++)
		svc[k] = s[k];
	svc[k] = 0;
	s += str_len(s) + 1;
	for (k = 0; s[k] && k < 63; k++)
		root[k] = s[k];
	root[k] = 0;
	return 0;
}

/* nom en minuscules, op (FS_INFO, FS_SYNC) : requête au service « fs.<nom> » -> résultat
 * (réponse dans vu_rep ; seul l'en-tête est mis à zéro, comme l'asm) (vu_fs_op). Un volume
 * qui partage le service d'un autre (BOOT: et SD0:) : celui de l'autre (VFS_VOLINFO) */
long vu_fs_op(const char *name, long op)
{
	str_cat(str_cat(vu_tmp, "fs."), name);
	long ch = korin_sys2(SYS_SVC_OPEN, vu_tmp, 500);
	char sv[16], rt[64];
	if (ch < 0 && vu_volinfo(name, sv, rt) == 0 && !str_eq(sv, name)) {
		str_cat(str_cat(vu_tmp, "fs."), sv);
		ch = korin_sys2(SYS_SVC_OPEN, vu_tmp, 500);
	}
	if (ch < 0)
		return ch;
	for (int i = 0; i < MSG_DATA; i += 8)
		*(u64 *)(vu_rep + i) = 0;
	*(u64 *)vu_rep = (u64)op;
	return korin_sys2(SYS_SVC_CALL, ch, vu_rep);
}

/* FS_SYNC -> fichiers ouverts, < 0 (E_INVAL : service sans sync) */
long vu_sync(const char *name)
{
	return vu_fs_op(name, FS_SYNC);
}

/* FS_INFO -> 0 ; vu_rep : a octets, b libres (-1 : inconnu), c octets par cluster, d FS_T_* ;
 * données : étiquette */
long vu_info(const char *name)
{
	return vu_fs_op(name, FS_INFO);
}

/* FS_T_* -> son nom (vu_type_nom) */
const char *vu_type_name(u64 t)
{
	if (t == FS_T_FAT32)
		return "FAT32";
	if (t == FS_T_EXFAT)
		return "exFAT";
	if (t == FS_T_RAM)
		return "RAM";
	return "?";
}

/* octets : « 512 Ko », « 37,5 Mo », « 29,7 Go » sur la sortie (virgule en français, point en
 * anglais : lang_get) (vu_taille) */
void vu_size(u64 v)
{
	const char *u = " Ko";
	int sh = 10;
	if (v >> 40) {
		u = " To";
		sh = 40;
	} else if (v >> 30) {
		u = " Go";
		sh = 30;
	} else if (v >> 20) {
		u = " Mo";
		sh = 20;
	}
	u64 n = v >> sh;
	out_putn(n);
	if (sh != 10 && n < 100) {
		out_putc(*lang_get() == 'e' ? '.' : ',');
		out_putn(((v & ((1ull << sh) - 1)) * 10) >> sh);
	}
	out_say(u);
}

static const char *blanks(const char *p)
{
	while (*p == ' ' || *p == 9)
		p++;
	return p;
}

/* nom en minuscules, non monté : sa source et son service -> vu_src, vu_srv ; 1 si trouvés
 * (ligne de SD0:/etc/mounts.txt ; sinon usbN : blk.usbNp1, ou blk.usbN sans partition)
 * (vu_source) */
int vu_source(const char *name)
{
	str_cat(vu_srv, "fatfs");
	vu_root[0] = 0;
	long fd = sys_open("SD0:/etc/mounts.txt", O_READ);
	if (fd >= 0) {
		long n = sys_read(fd, vu_mounts, VU_MOUNTS - 1);
		sys_close(fd);
		if (n > 0) {
			vu_mounts[n] = 0;
			const char *l = vu_mounts;
			while (*l) {
				if (*l == '#')
					goto next;
				long i = 0;
				for (;;) { /* le nom, sans la casse */
					u8 a = (u8)name[i], b = (u8)l[i];
					if ((u8)(b - 'A') <= 25)
						b += 32;
					if (!a)
						break;
					if (a != b)
						goto next;
					i++;
				}
				const char *p = blanks(l + i); /* puis blancs, « = », blancs */
				if (*p != '=')
					goto next;
				p = blanks(p + 1);
				long k = 0; /* la source, jusqu'à « , » */
				while (*p != ',' && (u8)*p > ' ' && k < 23)
					vu_src[k++] = *p++;
				vu_src[k] = 0;
				if (!k)
					goto next;
				while (*p == ',' || *p == ' ') /* le service */
					p++;
				k = 0;
				while ((u8)p[k] > ' ' && p[k] != ',' && k < 15) {
					vu_srv[k] = p[k];
					k++;
				}
				if (k) /* (pas de service : fatfs) */
					vu_srv[k] = 0;
				p += k; /* puis le rootpath, facultatif */
				while (*p == ',' || *p == ' ' || *p == 9)
					p++;
				k = 0;
				while ((u8)p[k] > ' ' && p[k] != ',' && p[k] != '#' && k < 63) {
					vu_root[k] = p[k];
					k++;
				}
				vu_root[k] = 0;
				return 1;
			next:
				for (;;) {
					char c = *l++;
					if (!c)
						goto usb;
					if (c == 10)
						break;
				}
			}
		}
	}
usb:
	if (name[0] != 'u' || name[1] != 's' || name[2] != 'b')
		return 0;
	char *e = str_cat(str_cat(vu_src, "blk."), name);
	str_cat(e, "p1");
	if (korin_sys2(SYS_SVC_OPEN, vu_src, 0) >= 0)
		return 1;
	*e = 0; /* sans partition : le disque entier */
	if (korin_sys2(SYS_SVC_OPEN, vu_src, 0) >= 0)
		return 1;
	return 0;
}

/* nom (vu_aff, le « : » final retiré), source, service : VFS_MOUNT -> 0, erreur (vu_monter) */
long vu_mount(const char *name, const char *src, const char *srv)
{
	return vu_mount_root(name, src, srv, 0);
}

/* comme vu_mount, avec un rootpath (0 ou vide : la racine de la source) */
long vu_mount_root(const char *name, const char *src, const char *srv, const char *root)
{
	vu_tmp[0] = 0;
	char *e = str_cat(vu_tmp, name);
	if (e[-1] == ':')
		*--e = 0;
	e = str_cat(e, " ");
	e = str_cat(e, src);
	e = str_cat(e, " ");
	e = str_cat(e, srv);
	if (root && *root) {
		e = str_cat(e, " ");
		str_cat(e, root);
	}
	vfs_msg(vu_msg, VFS_MOUNT, 0, 0, vu_tmp, 0);
	return vfs_call(vu_msg);
}

/* nom en minuscules : VFS_UNMOUNT (son service s'arrête), puis attend que le volume ait quitté
 * la liste (5 s au plus) -> 0, erreur (vu_demonter) */
long vu_unmount(const char *name)
{
	vfs_msg(vu_msg, VFS_UNMOUNT, 0, 0, name, 0);
	long r = vfs_call(vu_msg);
	if (r < 0)
		return r;
	for (int i = 50; i; i--) {
		if (!vu_find(name))
			return 0;
		sys_sleep(100);
	}
	return E_BUSY_VU;
}

/* ---- BOOT: à la demande (docs/FICHIERS.md, « Le volume BOOT: ») : pour écrire un fichier de
 * démarrage (un module, config.txt), vu_boot_begin, puis vu_boot_end. vfs monte BOOT: d'après
 * sa ligne « BOOT = source, service, rootpath, ondemand » de SD0:/etc/mounts.txt et le démonte
 * à la dernière fin (ou à la fin du programme) ; sans cette ligne, ou avec un vfs plus ancien :
 * « SD0: », la racine du volume système (comme avant). */
static int vu_boot_taken;

/* -> « BOOT: » ou « SD0: » : le volume où écrire les fichiers de démarrage */
const char *vu_boot_begin(void)
{
	static char vol[8];
	u8 m[MSG_SIZE] __attribute__((aligned(8)));
	vfs_msg(m, VFS_BOOT_BEGIN, 0, 0, "", 0);
	if (vfs_call(m) < 0) /* (BOOT: décrit mais pas montable, ou vfs plus ancien) */
		return "SD0:";
	if (*(u64 *)(m + MSG_A))
		vu_boot_taken++;
	m[MSG_DATA + 7] = 0;
	str_cat(vol, (const char *)m + MSG_DATA);
	return vol[0] ? vol : "SD0:";
}

/* la demande de vu_boot_begin rendue (la dernière : BOOT: démonté) */
void vu_boot_end(void)
{
	if (!vu_boot_taken)
		return;
	vu_boot_taken--;
	u8 m[MSG_SIZE] __attribute__((aligned(8)));
	vfs_msg(m, VFS_BOOT_END, 0, 0, "", 0);
	vfs_call(m);
}

/* code d'erreur : son texte, traduit, sur l'erreur standard (sans fin de ligne) (vu_err) */
void vu_err(long e)
{
	const char *s;
	switch (e) {
	case -16: s = "occupé (des fichiers y sont ouverts)"; break;
	case -1: s = "non permis sur ce volume"; break;
	case -22: s = "invalide"; break;
	case -2: s = "pas de tel volume, ou pas monté"; break;
	case -17: s = "déjà monté"; break;
	case -5: s = "erreur d'entrée / sortie"; break;
	case -12: s = "plus de mémoire"; break;
	case -110: s = "le service ne répond pas (pas de système de fichiers reconnu ?)"; break;
	case -20: s = "pas un dossier"; break;
	default: {
		char t[32];
		err_say("erreur ");
		fmt_dec((u64)-e, t);
		err_puts("-");
		err_puts(t);
		return;
	}
	}
	err_say(s);
}
