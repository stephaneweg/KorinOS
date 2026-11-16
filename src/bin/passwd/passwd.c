/* passwd - changer son mot de passe (docs/etude/MULTI-UTILISATEUR.md §2.6, étape 4) : l'ancien (s'il y en a
 * un), puis le nouveau deux fois ; authd (service « auth ») vérifie l'ancien et écrit l'empreinte dans
 * /etc/shadow.ini (PBKDF2-SHA-256). Un nouveau mot de passe vide retire le mot de passe du compte. */
#include "korinrt.h"

#define AUTH_CHECK 3
#define AUTH_PASSWD 4
#define E_ACCES -13
#define VT_TCGETS 5
#define VT_TCSETS 6
#define VTL_ECHO 8

static long m[MSG_SIZE / 8];

static void out(const char *s)
{
	sys_write(2, s, (u64)str_len(s));
}

static long tty_in(void)
{
	memset(m, 0, sizeof m);
	m[0] = V_STDIO;
	if (vfs_call(m) >= 0 && (m[MSG_A / 8] & VT_TAG))
		return m[MSG_A / 8];
	return 0;
}

static void echo(long vt, int on)
{
	static u8 saved[48];
	static int have;
	if (!vt)
		return;
	long ch = korin_sys2(SYS_SVC_OPEN, "vterm", 3000);
	if (ch < 0)
		return;
	u8 t[48];
	if (!on) {
		memset(m, 0, sizeof m);
		m[0] = VT_TCGETS;
		m[MSG_A / 8] = vt;
		if (korin_sys2(SYS_SVC_CALL, ch, m) < 0)
			return;
		memcpy(saved, (u8 *)m + MSG_DATA, 48);
		have = 1;
		memcpy(t, saved, 48);
		*(u32 *)(t + 12) &= ~(u32)VTL_ECHO;
	} else if (have)
		memcpy(t, saved, 48);
	else
		return;
	memset(m, 0, sizeof m);
	m[0] = VT_TCSETS;
	m[MSG_A / 8] = vt;
	memcpy((u8 *)m + MSG_DATA, t, 48);
	((u32 *)m)[MSG_LEN / 4] = 48;
	korin_sys2(SYS_SVC_CALL, ch, m);
}

static void ask(const char *prompt, char *buf, u64 cap, long vt)
{
	out(prompt);
	echo(vt, 0);
	u64 n = 0;
	char c;
	while (n + 1 < cap && sys_read(0, &c, 1) == 1 && c != '\n' && c != '\r')
		buf[n++] = c;
	buf[n] = 0;
	echo(vt, 1);
	out("\n");
}

static long call(long ch, u64 op, const char *a, const char *b)
{
	memset(m, 0, sizeof m);
	m[0] = (long)op;
	char *d = (char *)m + MSG_DATA;
	u64 na = (u64)str_len(a), nb = b ? (u64)str_len(b) : 0;
	if (na + nb + 2 > 460)
		return E_INVAL;
	memcpy(d, a, na + 1);
	if (b)
		memcpy(d + na + 1, b, nb + 1);
	((u32 *)m)[MSG_LEN / 4] = (u32)(na + nb + 2);
	long r = korin_sys2(SYS_SVC_CALL, ch, m);
	return r < 0 ? r : m[0];
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long ch = korin_sys2(SYS_SVC_OPEN, "auth", 3000);
	if (ch < 0) {
		out("passwd : le service auth (authd) ne repond pas\n");
		return 1;
	}
	long vt = tty_in();
	char old[128] = "", n1[128], n2[128];
	if (call(ch, AUTH_CHECK, "", 0) != 0) /* le compte a un mot de passe : l'ancien d'abord */
		ask("Mot de passe actuel : ", old, sizeof old, vt);
	ask("Nouveau mot de passe (vide : aucun) : ", n1, sizeof n1, vt);
	ask("Encore une fois : ", n2, sizeof n2, vt);
	if (!str_eq(n1, n2)) {
		out("passwd : les deux ne sont pas les memes, rien n'est change\n");
		return 1;
	}
	long r = call(ch, AUTH_PASSWD, old, n1);
	memset(old, 0, sizeof old);
	memset(n1, 0, sizeof n1);
	memset(n2, 0, sizeof n2);
	if (r == E_ACCES) {
		out("passwd : mot de passe actuel incorrect\n");
		return 1;
	}
	if (r) {
		out("passwd : echec (/etc/shadow.ini)\n");
		return 1;
	}
	out("Mot de passe change.\n");
	return 0;
}
