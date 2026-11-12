/* sys.c : appels système et fichiers (docs/BIN-C.md), comme la macro sys
 * de src/inc/armpi.inc et les fonctions de fichiers de src/inc/tool.inc. */
#include "korinrt.h"

/* file_sys d'AppKit : sur AArch64, le numéro d'appel va dans x8 (comme un
 * svc 0 : le relais de file_sys le passe à la fonction en 7e argument), et le
 * relais garde x1-x18 et NZCV ; sur x86-64, le numéro est le 7e argument */
static inline long fsys(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
#if defined(__aarch64__)
	register long x0 __asm__("x0") = a0;
	register long x1 __asm__("x1") = a1;
	register long x2 __asm__("x2") = a2;
	register long x3 __asm__("x3") = a3;
	register long x4 __asm__("x4") = a4;
	register long x5 __asm__("x5") = a5;
	register long x8 __asm__("x8") = nr;
	__asm__ volatile("bl file_sys"
			 : "+r"(x0)
			 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
			 : "x16", "x17", "x30", "memory", "cc");
	return x0;
#else
	return file_sys(a0, a1, a2, a3, a4, a5, nr);
#endif
}

/* numéro, arguments -> résultat : les appels de fichiers (0x80-0x8E),
 * SYS_PRINT et SYS_FORK vont à file_sys d'AppKit, les autres au noyau */
long ksys(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
	if ((nr >= SYS_OPEN && nr <= SYS_UNLOAD) || nr == SYS_PRINT || nr == SYS_FORK)
		return fsys(nr, a0, a1, a2, a3, a4, a5);
	return korin_sys6(nr, a0, a1, a2, a3, a4, a5);
}

#define FS(nr, a, b, c) fsys((nr), (long)(a), (long)(b), (long)(c), 0, 0, 0)

long sys_open(const char *path, long flags) { return FS(SYS_OPEN, path, flags, 0); }
long sys_read(long fd, void *buf, u64 n) { return FS(SYS_READ, fd, buf, n); }
long sys_write(long fd, const void *buf, u64 n) { return FS(SYS_WRITE, fd, buf, n); }
long sys_seek(long fd, long pos, long whence) { return FS(SYS_SEEK, fd, pos, whence); }
long sys_close(long fd) { return FS(SYS_CLOSE, fd, 0, 0); }
long sys_stat(const char *path, long st[2]) { return FS(SYS_STAT, path, st, 0); }
long sys_readdir(long fd, struct dirent *e) { return FS(SYS_READDIR, fd, e, 0); }
long sys_mkdir(const char *path) { return FS(SYS_MKDIR, path, 0, 0); }
long sys_unlink(const char *path) { return FS(SYS_UNLINK, path, 0, 0); }
long sys_chdir(const char *path) { return FS(SYS_CHDIR, path, 0, 0); }
long sys_getcwd(char *buf, u64 size) { return FS(SYS_GETCWD, buf, size, 0); }
long sys_print(const char *s, u64 n) { return FS(SYS_PRINT, s, n, 0); }
long sys_sleep(long ms) { return korin_sys1(SYS_SLEEP, ms); }
long sys_clock(long which) { return korin_sys1(SYS_CLOCK_GET, which); }
long sys_time_ms(void) { return korin_sys0(SYS_TIME_MS); }
long sys_getpid(void) { return korin_sys0(SYS_GETPID); }

void *sys_vm_map(u64 size)
{
	return (void *)korin_sys4(SYS_VM_MAP, 0, size, PROT_READ | PROT_WRITE, 0);
}

/* chemin -> 1 dossier, 0 fichier, -1 absent (t_type) */
int path_type(const char *path)
{
	long st[2];
	if (sys_stat(path, st) < 0)
		return -1;
	return (st[1] & ATTR_DIR) != 0;
}

/* d (300 o au moins) = dir + « / » + name ; pas de « / » après « / », « : »
 * ou un dossier vide (t_join) */
void path_join2(char *d, const char *dir, const char *name)
{
	char *o = d;
	while (*dir)
		*o++ = *dir++;
	if (o != d && o[-1] != '/' && o[-1] != ':')
		*o++ = '/';
	while ((*o++ = *name++))
		;
}

/* chemin -> son dernier composant non vide, dans le chemin (t_base : « a/ »
 * rend « a/ ») */
char *path_base(char *path)
{
	char *r = path;
	for (char *p = path; *p;) {
		char c = *p++;
		if ((c == '/' || c == ':') && *p)
			r = p;
	}
	return r;
}

int is_opt(const char *s)
{
	return s[0] == '-' && s[1];
}

static long vfs_chan;

long vfs_call(void *msg)
{
	if (!vfs_chan) {
		long c = korin_sys2(SYS_SVC_OPEN, "vfs", 5000);
		if (c < 0)
			return c;
		vfs_chan = c;
	}
	return korin_sys2(SYS_SVC_CALL, vfs_chan, msg);
}

/* message : op, a, b, c = d = 0, données = p1 (et p2), longueur (t_msg) */
void vfs_msg(void *msg, long op, long a, long b, const char *p1, const char *p2)
{
	long *m = msg;
	m[0] = op;
	m[1] = a;
	m[2] = b;
	m[3] = 0;
	m[4] = 0;
	char *o = (char *)msg + MSG_DATA;
	while ((*o++ = *p1++))
		;
	if (p2)
		while ((*o++ = *p2++))
			;
	*(u32 *)((char *)msg + MSG_LEN) = (u32)(o - ((char *)msg + MSG_DATA));
}

long vfs_rename(const char *from, const char *to)
{
	long m[MSG_SIZE / 8];
	vfs_msg(m, V_RENAME, 0, 0, from, to);
	return vfs_call(m);
}

long vfs_utime(const char *path, u64 secs)
{
	long m[MSG_SIZE / 8];
	vfs_msg(m, V_UTIME, 0, (long)secs, path, 0);
	return vfs_call(m);
}

/* source, destination -> 0, ou l'erreur (t_copier : la destination est
 * refaite ; une erreur d'écriture arrête) */
long copy_file(const char *src, const char *dst)
{
	static u8 buf[T_READ];
	long in = sys_open(src, O_READ);
	if (in < 0)
		return in;
	long out = sys_open(dst, O_WRITE | O_CREATE | O_TRUNC);
	if (out < 0) {
		sys_close(in);
		return out;
	}
	long r;
	for (;;) {
		r = sys_read(in, buf, T_READ);
		if (r <= 0)
			break;
		r = sys_write(out, buf, (u64)r);
		if (r < 0)
			break;
	}
	sys_close(out);
	sys_close(in);
	return r < 0 ? r : 0;
}
