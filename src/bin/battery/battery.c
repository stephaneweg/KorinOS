/* battery - la batterie et le profil d'énergie (Korin PC : service acpid, docs/etude/BATTERIE-PC.md) :
 *    battery                    niveau, temps restant, secteur, puissance, capacité, usure, profil
 *    battery -r                 les valeurs brutes du service (clé=valeur)
 *    battery profile MODE       auto, performance, balanced ou saver (gardé dans /etc/power.ini)
 *    battery reload             relire /etc/power.ini
 * Sans acpid (le Pi, un service arrêté) : code 1. */
#include "korinrt.h"

#define ACPID_OP_GET 1
#define ACPID_OP_PROFILE 2
#define ACPID_OP_RELOAD 3

static u64 m[MSG_SIZE / 8] __attribute__((aligned(16)));

static long call(long op, long a)
{
	long ch = korin_sys2(SYS_SVC_OPEN, "acpid", 500);
	if (ch < 0) {
		printf("battery : le service acpid ne répond pas (%ld)\n", ch);
		_exit(1);
	}
	memset(m, 0, sizeof m);
	m[0] = (u64)op;
	m[1] = (u64)a;
	long r = korin_sys2(SYS_SVC_CALL, ch, m);
	if (r < 0) {
		printf("battery : appel refusé (%ld)\n", r);
		_exit(1);
	}
	return (long)m[0];
}

/* la valeur de « clé= » dans la réponse (copie), "" si absente */
static const char *val(const char *key)
{
	static char out[8][48];
	static int k;
	char *o = out[k++ & 7];
	o[0] = 0;
	const char *p = (const char *)m + MSG_DATA;
	u64 n = strlen(key);
	while (*p) {
		if (!strncmp(p, key, n) && p[n] == '=') {
			p += n + 1;
			int i = 0;
			while (*p && *p != '\n' && i < 47)
				o[i++] = *p++;
			o[i] = 0;
			return o;
		}
		while (*p && *p != '\n')
			p++;
		if (*p)
			p++;
	}
	return o;
}

static long num(const char *key)
{
	const char *v = val(key);
	return *v ? strtol(v, 0, 10) : -1;
}

/* mW ou mWh en « 12,3 » */
static void w(char *b, long mw)
{
	snprintf(b, 24, "%ld,%ld", mw / 1000, (mw % 1000) / 100);
}

int main(int argc, char **argv)
{
	static const char *const pnames[4] = {"auto", "performance", "balanced", "saver"};
	static const char *const pfr[3] = {"performance", "équilibré", "économie"};
	if (argc >= 3 && str_eq(argv[1], "profile")) {
		for (int i = 0; i < 4; i++)
			if (str_eq(argv[2], pnames[i])) {
				long r = call(ACPID_OP_PROFILE, i - 1);
				if (r < 0) {
					printf("battery : refusé (%ld)\n", r);
					return 1;
				}
				call(ACPID_OP_GET, 0);
				long p = num("profile");
				printf("profil d'énergie : %s%s\n", p >= 0 && p <= 2 ? pfr[p] : "aucun",
				       i == 0 ? " (automatique)" : "");
				return 0;
			}
		printf("usage : battery profile auto|performance|balanced|saver\n");
		return 1;
	}
	if (argc >= 2 && str_eq(argv[1], "reload"))
		return call(ACPID_OP_RELOAD, 0) < 0;
	if (argc >= 2 && !str_eq(argv[1], "-r")) {
		printf("usage : battery [-r] | battery profile auto|performance|balanced|saver | battery reload\n");
		return 1;
	}
	call(ACPID_OP_GET, 0);
	if (argc >= 2) {
		out_puts((const char *)m + MSG_DATA);
		return 0;
	}
	long pct = num("pct"), min = num("min"), ac = num("ac"), prof = num("profile");
	const char *st = val("state");
	char a[24], b[24], c[24];
	if (st[0] == 'n' || num("batteries") <= 0) {
		printf("Pas de batterie%s ; secteur %s\n", num("adapters") > 0 ? " présente" : "",
		       ac ? "branché" : "débranché");
	} else {
		const char *what = st[0] == 'c' ? "en charge" : st[0] == 'd' ? "décharge" : st[0] == 'f' ? "pleine"
							  : "branchée, ne charge pas";
		printf("Batterie : %ld %% (%s)", pct, what);
		if (min >= 0)
			printf(", %s %ld h %02ld", st[0] == 'c' ? "pleine dans" : "reste environ", min / 60, min % 60);
		printf("\nSecteur : %s\n", ac ? "branché" : "débranché");
		long rate = num("rate_mw"), rem = num("remain_mwh"), full = num("full_mwh"), des = num("design_mwh");
		if (rate >= 0) {
			w(a, rate);
			printf("Puissance : %s W\n", a);
		}
		if (rem >= 0 && full > 0) {
			w(a, rem);
			w(b, full);
			printf("Capacité : %s / %s Wh", a, b);
			if (des > 0) {
				w(c, des);
				printf(" (neuve : %s Wh, usure %ld %%)", c, full < des ? (des - full) * 100 / des : 0);
			}
			printf("\n");
		}
		long cyc = num("cycles");
		const char *model = val("model"), *oem = val("oem"), *tech = val("tech");
		if (*model || cyc >= 0)
			printf("Modèle : %s %s %s%s", oem, model, tech, cyc >= 0 ? "" : "\n");
		if (cyc >= 0)
			printf(", %ld cycles\n", cyc);
		printf("Alertes : %ld %%, %ld %%, extinction à %ld %%\n", num("warn"), num("critical"), num("shutdown"));
	}
	printf("Profil d'énergie : %s%s\n", prof >= 0 && prof <= 2 ? pfr[prof] : "aucun (pstate=off ?)",
	       num("auto") == 1 ? " (automatique)" : "");
	return 0;
}
