/* sudo - lancer une commande avec l'uid 0 (docs/etude/MULTI-UTILISATEUR.md §2.6, étape 4)
 *
 *   sudo commande [arguments]    la commande en uid 0, sur le même terminal
 *   sudo -s                      un shell system (bash, sinon sh)
 *
 * Le mot de passe demandé est celui de l'utilisateur (décision du 16/11), vérifié par authd (service
 * « auth »), qui passe ce process en uid 0 ; la commande, lancée ensuite, hérite de cet uid. Les comptes qui
 * peuvent s'élever sont ceux de /etc/elevate.ini. Un compte sans mot de passe s'élève sans rien taper. Aucun
 * droit sur le matériel n'est donné : seulement l'uid 0, donc l'écriture dans /. La commande est cherchée
 * dans /sbin, /bin puis /devtools/gcc/bin (le PATH de system). */
#include "korinrt.h"

#define AUTH_CAN 1
#define AUTH_SUDO 2
#define E_ACCES -13
#define VT_TCGETS 5
#define VT_TCSETS 6
#define VTL_ECHO 8

static long m[MSG_SIZE / 8];

static void out(const char *s)
{
	sys_write(2, s, (u64)str_len(s));
}

/* l'entrée standard, si c'est un bout de vterm (0 sinon) */
static long tty_in(void)
{
	memset(m, 0, sizeof m);
	m[0] = V_STDIO;
	if (vfs_call(m) >= 0 && (m[MSG_A / 8] & VT_TAG))
		return m[MSG_A / 8];
	return 0;
}

/* écho du terminal coupé (on = 0) ou rendu (on = 1) */
static void echo(long vt, int on)
{
	static u8 saved[48];
	static int have;
	if (!vt)
		return;
	long ch = korin_sys2(SYS_SVC_OPEN, "vterm", 3000);
	if (ch < 0)
		return;
	if (!on) {
		memset(m, 0, sizeof m);
		m[0] = VT_TCGETS;
		m[MSG_A / 8] = vt;
		if (korin_sys2(SYS_SVC_CALL, ch, m) < 0)
			return;
		memcpy(saved, (u8 *)m + MSG_DATA, 48);
		have = 1;
		u8 t[48];
		memcpy(t, saved, 48);
		*(u32 *)(t + 12) &= ~(u32)VTL_ECHO; /* VTT_LFLAG */
		memset(m, 0, sizeof m);
		m[0] = VT_TCSETS;
		m[MSG_A / 8] = vt;
		memcpy((u8 *)m + MSG_DATA, t, 48);
		((u32 *)m)[MSG_LEN / 4] = 48;
		korin_sys2(SYS_SVC_CALL, ch, m);
	} else if (have) {
		memset(m, 0, sizeof m);
		m[0] = VT_TCSETS;
		m[MSG_A / 8] = vt;
		memcpy((u8 *)m + MSG_DATA, saved, 48);
		((u32 *)m)[MSG_LEN / 4] = 48;
		korin_sys2(SYS_SVC_CALL, ch, m);
	}
}

/* une ligne de l'entrée standard (sans la fin de ligne) */
static void read_line(char *buf, u64 cap)
{
	u64 n = 0;
	char c;
	while (n + 1 < cap && sys_read(0, &c, 1) == 1) {
		if (c == '\n' || c == '\r')
			break;
		buf[n++] = c;
	}
	buf[n] = 0;
}

static long auth_call(long ch, u64 op, const char *pw)
{
	memset(m, 0, sizeof m);
	m[0] = (long)op;
	u64 n = pw ? (u64)str_len(pw) : 0;
	if (n > 400)
		n = 400;
	if (pw)
		memcpy((u8 *)m + MSG_DATA, pw, n);
	((u32 *)m)[MSG_LEN / 4] = (u32)(n + 1);
	long r = korin_sys2(SYS_SVC_CALL, ch, m);
	return r < 0 ? r : m[0];
}

/* nom -> chemin (« /sbin/x », « /bin/x »...) dans out ; 0 : introuvable */
static int find(const char *name, char *o)
{
	static const char *const dirs[] = {"/sbin/", "/bin/", "/devtools/gcc/bin/", 0};
	long st[2];
	for (const char *s = name; *s; s++)
		if (*s == '/') {
			str_cat(o, name);
			return sys_stat(o, st) >= 0;
		}
	for (int i = 0; dirs[i]; i++) {
		str_cat(str_cat(o, dirs[i]), name);
		if (sys_stat(o, st) >= 0)
			return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		out("usage : sudo commande [arguments] | sudo -s\n");
		return 1;
	}
	if (korin_sys0(SYS_GETUID) != 0) {
		long ch = korin_sys2(SYS_SVC_OPEN, "auth", 3000);
		if (ch < 0) {
			out("sudo : le service auth (authd) ne repond pas\n");
			return 1;
		}
		long can = auth_call(ch, AUTH_CAN, 0);
		if (can <= 0) {
			out("sudo : ce compte ne peut pas s'elever (/etc/elevate.ini)\n");
			return 1;
		}
		long r = E_ACCES;
		if (can == 1)
			r = auth_call(ch, AUTH_SUDO, "");
		long vt = tty_in();
		for (int tries = 0; r == E_ACCES && tries < 3; tries++) {
			char pw[128];
			out("[sudo] mot de passe : ");
			echo(vt, 0);
			read_line(pw, sizeof pw);
			echo(vt, 1);
			out("\n");
			r = auth_call(ch, AUTH_SUDO, pw);
			memset(pw, 0, sizeof pw);
			if (r == E_ACCES)
				out("Mot de passe incorrect.\n");
		}
		if (r != 0) {
			if (r != E_ACCES)
				out("sudo : elevation refusee\n");
			return 1;
		}
	}
	/* uid 0 : la commande, sur le même terminal */
	char path[256], args[1024];
	args[0] = 0;
	int first = 1;
	if (str_eq(argv[1], "-s")) {
		long st[2];
		str_cat(path, sys_stat("/bin/bash", st) >= 0 ? "/bin/bash" : "/bin/sh");
		first = 2;
	} else {
		if (!find(argv[1], path)) {
			out("sudo : commande introuvable : ");
			out(argv[1]);
			out("\n");
			return 127;
		}
		first = 2;
	}
	char *o = args;
	for (int i = first; i < argc; i++) {
		if ((u64)(o - args) + (u64)str_len(argv[i]) + 2 >= sizeof args)
			break;
		if (o != args)
			*o++ = ' ';
		o = str_cat(o, argv[i]);
	}
	long pid = ksys(SYS_SPAWN, (long)path, args[0] ? (long)args : 0, 0, 0, 0, 0);
	if (pid < 0) {
		out("sudo : lancement impossible : ");
		out(path);
		out("\n");
		return 126;
	}
	return (int)korin_sys1(SYS_WAIT, pid);
}
