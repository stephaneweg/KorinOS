/* sudo - lancer une commande avec l'uid 0 (docs/etude/MULTI-UTILISATEUR.md §2.6, étape 4)
 *
 *   sudo commande [arguments]    la commande en uid 0, sur le même terminal
 *   sudo -s                      un shell system (bash, sinon sh)
 *   sudo -k                      oublier les élévations retenues de ce compte
 *
 * Le mot de passe demandé est celui de l'utilisateur (décision du 16/11), vérifié par authd (service
 * « auth »), qui passe ce process en uid 0 ; la commande, lancée ensuite, hérite de cet uid. Les comptes qui
 * peuvent s'élever sont ceux de /etc/elevate.ini. Un compte sans mot de passe s'élève sans rien taper. Aucun
 * droit sur le matériel n'est donné : seulement l'uid 0, donc l'écriture dans /. La commande est cherchée
 * dans /sbin, /bin, /devtools/gcc/bin puis /devtools/rust/bin (le PATH de system).
 *
 * Après le mot de passe, « Retenir pendant 5 minutes ? [o/N] » : oui, authd ne redemande plus le mot de passe
 * pendant 5 minutes à ce compte depuis ce terminal (le shell qui lance sudo, son père) ; la carte de la
 * session graphique a sa propre case (docs/maquettes/elevation-retenir.html). */
#include "korinrt.h"

#define AUTH_CAN 1
#define AUTH_SUDO 2
#define AUTH_FORGET 11
#define AUTH_ELEVATE 5
#define AUTH_WAIT 7
#ifndef E_AGAIN
#define E_AGAIN -11
#endif
#ifndef E_PERM
#define E_PERM -1
#endif
#ifndef E_BUSY
#define E_BUSY -16
#endif
#ifndef E_CANCELED
#define E_CANCELED -125
#endif
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

/* la carte d'élévation d'authd (AUTH_ELEVATE, puis AUTH_WAIT tant qu'elle est ouverte), comme ui_elevate : un
 * compte sans mot de passe accepte à l'écran -> 0 (ce process est passé en uid 0), < 0 : refus */
static long card(long ch, const char *what)
{
	char why[160] = "sudo ";
	u64 k = 5;
	for (; what && *what && k < sizeof why - 1; what++)
		why[k++] = *what;
	why[k] = 0;
	u64 op = AUTH_ELEVATE;
	for (;;) {
		memset(m, 0, sizeof m);
		m[0] = (long)op;
		u64 n = (u64)str_len(why);
		memcpy((u8 *)m + MSG_DATA, why, n);
		((u32 *)m)[MSG_LEN / 4] = (u32)(n + 1);
		long e = korin_sys2(SYS_SVC_CALL, ch, m);
		long r = e < 0 ? e : m[0];
		if (r != E_AGAIN)
			return r;
		op = AUTH_WAIT;
	}
}

static long auth_call(long ch, u64 op, const char *pw, int keep)
{
	memset(m, 0, sizeof m);
	m[0] = (long)op;
	m[1] = korin_sys0(SYS_GETPPID); /* (l'ancienne portée : authd la calcule lui-même, le père réel, E38) */
	m[2] = keep ? 1 : 0;
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
	static const char *const dirs[] = {"/sbin/", "/bin/", "/devtools/gcc/bin/", "/devtools/rust/bin/", 0};
	long st[2];
	if (str_len(name) > 200) /* (o : 256 octets, le plus long dossier compris) */
		return 0;
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
		out("usage : sudo commande [arguments] | sudo -s | sudo -k\n");
		return 1;
	}
	if (str_eq(argv[1], "-k")) {
		long ch = korin_sys2(SYS_SVC_OPEN, "auth", 3000);
		if (ch < 0 || auth_call(ch, AUTH_FORGET, 0, 0) != 0) {
			out("sudo : le service auth (authd) ne repond pas\n");
			return 1;
		}
		return 0;
	}
	if (korin_sys0(SYS_GETUID) != 0) {
		long ch = korin_sys2(SYS_SVC_OPEN, "auth", 3000);
		if (ch < 0) {
			out("sudo : le service auth (authd) ne repond pas\n");
			return 1;
		}
		long can = auth_call(ch, AUTH_CAN, 0, 0);
		if (can <= 0) {
			out("sudo : ce compte ne peut pas s'elever (/etc/elevate.ini)\n");
			return 1;
		}
		long r = E_ACCES;
		long vt = tty_in();
		if (can == 1) { /* retenu, ou un compte sans mot de passe : celui-ci passe par la carte d'élévation, que
				 * l'utilisateur accepte à l'écran (authd refuse AUTH_SUDO sans mot de passe : A3) */
			r = auth_call(ch, AUTH_SUDO, "", 0);
			if (r == E_PERM) { /* (sans mot de passe : la carte ; E_ACCES : retenu expiré, le mot de passe) */
				r = card(ch, argv[1]);
				if (r != 0) {
					out(r == E_CANCELED ? "sudo : elevation refusee a l'ecran\n"
					    : r == E_BUSY ? "sudo : une autre demande d'elevation est ouverte a l'ecran\n"
							  : "sudo : elevation impossible (pas de carte d'elevation : session "
							    "graphique absente ?)\n");
					return 1;
				}
			}
		}
		int keep = -1; /* (demandé une fois, après le premier mot de passe) */
		for (int tries = 0; r == E_ACCES && tries < 3; tries++) {
			char pw[128];
			out("[sudo] mot de passe : ");
			echo(vt, 0);
			read_line(pw, sizeof pw);
			echo(vt, 1);
			out("\n");
			if (keep < 0) {
				char a[16];
				out("Retenir pendant 5 minutes ? [o/N] ");
				read_line(a, sizeof a);
				keep = a[0] == 'o' || a[0] == 'O' || a[0] == 'y' || a[0] == 'Y';
			}
			r = auth_call(ch, AUTH_SUDO, pw, keep);
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
	/* la ligne du programme lancé : un argument avec espace, guillemet ou « \ » (ou vide) entre guillemets,
	 * avec \" et \\ (la règle de crt0 et de libposix) ; avant, « sudo rm 'mon fichier' » effaçait « mon » et
	 * « fichier » en uid 0 (A7, E40) */
	char *o = args;
	for (int i = first; i < argc; i++) {
		const char *a = argv[i];
		int q = !*a;
		for (const char *c = a; *c; c++)
			q |= *c == ' ' || *c == '"' || *c == '\\' || *c == '\t';
		u64 need = (u64)str_len(a) * 2 + 4;
		if ((u64)(o - args) + need >= sizeof args) {
			out("sudo : ligne de commande trop longue\n");
			return 126;
		}
		if (o != args)
			*o++ = ' ';
		if (!q) {
			o = str_cat(o, a);
			continue;
		}
		*o++ = '"';
		for (const char *c = a; *c; c++) {
			if (*c == '"' || *c == '\\')
				*o++ = '\\';
			*o++ = *c;
		}
		*o++ = '"';
		*o = 0;
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
