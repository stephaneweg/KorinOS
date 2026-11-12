/* df - la place des volumes : df [volume-ou-chemin ...] (docs/OUTILS.md), comme df.asm. Sans
 * argument : chaque volume monté (VFS_VOLUMES), ceux qui ne répondent pas sont tus. Une ligne
 * chacun : le volume, son système de fichiers, sa taille, la place prise et libre, son étiquette
 * (FS_INFO de « fs.<volume> »). Un chemin : le volume qui le porte (relatif ou « /... » : le
 * dossier courant ; un « : » avant le premier « / » : le nom devant, 14 lettres au plus, même un
 * volume qui n'existe pas). Les tailles : virgule même en anglais (comme l'asm, à la
 * différence de mount). */
#include "korinrt.h"

static char vname[16];

/* chemin -> vname : le nom de son volume, en minuscules (« sd0 ») */
static void vol_of(const char *p)
{
	static char cwd[256];
	const char *q = p;
	for (;;) {
		char c = *q++;
		if (!c || c == '/') {
			sys_getcwd(cwd, 256);
			p = cwd;
			break;
		}
		if (c == ':')
			break;
	}
	long n = 0;
	for (;;) {
		u8 c = (u8)p[n];
		if (!c || c == ':' || c == '/')
			break;
		if ((u8)(c - 'A') <= 25)
			c += 32;
		vname[n++] = (char)c;
		if (n >= 14)
			break;
	}
	vname[n] = 0;
}

/* n blancs (au moins un) */
static void pad(long n)
{
	if (n < 1)
		n = 1;
	while (n--)
		out_putc(' ');
}

/* octets : « 512 Ko », « 37,5 Mo », « 29,7 Go », « 1,8 To » */
static void put_size(u64 v)
{
	const char *u = " Ko";
	u64 d = 1024;
	if (v >= 1ull << 40) {
		u = " To";
		d = 1ull << 40;
	} else if (v >= 1ull << 30) {
		u = " Go";
		d = 1ull << 30;
	} else if (v >= 1ull << 20) {
		u = " Mo";
		d = 1ull << 20;
	}
	u64 n = v / d;
	out_putn(n);
	if (d > 1024 && n < 100) {
		out_say(",");
		out_putn((v - n * d) * 10 / d);
	}
	out_say(u);
}

/* nom du volume (minuscules), quiet : se taire s'il ne répond pas -> 0, 1 (absent) */
static int show(const char *name, int quiet)
{
	static u64 m[MSG_SIZE / 8];
	char svc[40], up[24];
	str_cat(str_cat(svc, "fs."), name);
	long ch = korin_sys2(SYS_SVC_OPEN, svc, 200);
	if (ch < 0)
		goto absent;
	memset(m, 0, sizeof m);
	m[0] = FS_INFO;
	if (korin_sys2(SYS_SVC_CALL, ch, m) < 0)
		goto absent;
	char *o = up; /* « SD0: » */
	for (const char *s = name; *s; s++) {
		u8 c = (u8)*s;
		if ((u8)(c - 'a') <= 25)
			c -= 32;
		*o++ = (char)c;
	}
	*o++ = ':';
	*o = 0;
	out_puts(up);
	pad(8 - str_len(up));
	static const char *const types[] = { "?", "FAT32", "exFAT", "RAM" };
	u64 t = m[MSG_D / 8];
	const char *ty = types[t <= 3 ? t : 0];
	out_puts(ty);
	pad(7 - str_len(ty));
	out_say("taille ");
	put_size(m[MSG_A / 8]);
	if ((i64)m[MSG_B / 8] < 0) {
		out_say(", place libre inconnue");
	} else {
		out_say(", utilisé ");
		u64 a = m[MSG_A / 8], b = m[MSG_B / 8];
		put_size(a >= b ? a - b : 0);
		out_say(", libre ");
		put_size(m[MSG_B / 8]);
	}
	char *lab = (char *)m + MSG_DATA;
	if (*lab) {
		out_puts("  « ");
		lab[63] = 0;
		out_puts(lab);
		out_puts(" »");
	}
	if (m[MSG_D / 8] == FS_T_RAM)
		out_say(" (en mémoire : perdu au redémarrage)");
	out_nl();
	return 0;
absent:
	if (quiet)
		return 1;
	err_puts(krt_argv[0]);
	err_puts(": ");
	err_say("pas de volume ");
	err_puts(name);
	err_puts("\n");
	return 1;
}

int main(int argc, char **argv)
{
	int code = 0;
	if (argc < 2) { /* tous les volumes montés */
		static u8 vm[MSG_SIZE];
		vfs_msg(vm, VFS_VOLUMES, 0, 0, "", 0);
		if (vfs_call(vm) < 0) {
			err_msg("pas de service vfs");
			return 1;
		}
		u64 n = *(u64 *)(vm + MSG_B);
		if (n > FS_DATA_MAX / VU_ENT)
			n = FS_DATA_MAX / VU_ENT;
		for (const char *e = (char *)vm + MSG_DATA; n; n--, e += VU_ENT)
			show(e, 1);
		return 0;
	}
	for (int i = 1; i < argc; i++) {
		vol_of(argv[i]);
		code |= show(vname, 0);
	}
	return code;
}
