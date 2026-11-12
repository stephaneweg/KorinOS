/* volume - le volume du son, simplement (docs/OUTILS.md, d'après volume.c d'Onyx), comme
 * volume.asm : la sortie principale du mixer (service sound, par AudioKit).
 *	volume                          le volume de la sortie principale
 *	volume 0..100                   le règle (et remet le son)
 *	volume mute | unmute | toggle   coupe, remet, bascule
 *	volume output                   les sorties, laquelle joue
 *	volume output auto|jack|hdmi|usb|<numéro>|<nom>   la sortie préférée
 *	volume apps                     les programmes qui jouent, le volume de chacun
 *	volume app <nom|pid> 0..100|mute|unmute           le volume d'un programme
 * Comme l'asm : sans service sound, l'erreur avant tout (code 1) ; « volume N » seulement avec un
 * argument (sinon l'usage, code 2), mais mute, unmute, toggle, output et apps ignorent ce qui
 * suit ; « volume output » : un mot qui n'est pas un nombre de 0 à 100 cherche le type (« » pour
 * la sortie virtuelle : « volume output "" » la choisit) puis le nom ; « volume app » : exactement
 * deux arguments, un pid fait seulement de chiffres (0 : cherché comme un nom), tous les clients
 * qui correspondent sont réglés ; après un réglage, 50 ms d'attente puis l'état. Sortie
 * tamponnée (t_dire, t_putn), erreurs « volume: … » (t_err_msg). */
#include "korinrt.h"
#include "audiokit_import.h"

static const u8 *st; /* état (lecture seule, tenu à jour par sound) */

/* décalages : les mots de la commande (type 0 : le mot vide), puis les noms affichés */
static const char c_types[] = "\x04\x05\x0a\x0f"
			      "\0jack\0hdmi\0usb";
static const char types[] = "\x04\x0e\x13\x18"
			    "virtuelle\0jack\0HDMI\0USB";

static NORETURN void fail_msg(const char *m, int code)
{
	err_msg(m);
	exit(code);
}

static NORETURN void usage(void)
{
	fail_msg("usage : volume [0..100 | mute | unmute | toggle | output [auto|jack|hdmi|usb|<sortie>] | apps | "
		 "app <nom|pid> <0..100|mute|unmute>]",
		 2);
}

static NORETURN void noout(void)
{
	fail_msg("aucune sortie de son", 1);
}

static NORETURN void failed(void)
{
	fail_msg("refusé par le service sound", 1);
}

/* texte -> 0..100, -1 si ce n'en est pas un */
static long num100(const char *s)
{
	u64 v = 0;
	if (!*s)
		return -1;
	for (; *s; s++) {
		u32 d = (u32)(u8)*s - '0';
		if (d > 9)
			return -1;
		v = v * 10 + d;
		if (v > 100)
			return -1;
	}
	return (long)v;
}

/* texte -> pid s'il n'a que des chiffres, 0 sinon */
static u64 num_pid(const char *s)
{
	u64 v = 0;
	if (!*s)
		return 0;
	for (; *s; s++) {
		u32 d = (u32)(u8)*s - '0';
		if (d > 9)
			return 0;
		v = v * 10 + d;
	}
	return v;
}

/* -> l'entrée SO_* de la sortie principale (0 : aucune) */
static const u8 *main_out(void)
{
	u64 m = *(const u64 *)(st + SS_MAIN);
	if (!m)
		return 0;
	const u8 *o = st + SS_OUTS;
	for (int i = 0; i < SS_OUT_MAX; i++, o += 64)
		if (SS_U32(o, SO_ID) == m)
			return o;
	return 0;
}

/* « 1 « jack » (jack) » */
static void out_desc(const u8 *o)
{
	out_putn(SS_U32(o, SO_ID));
	out_puts(" « ");
	out_puts((const char *)o + SO_NAME);
	out_puts(" » (");
	u32 t = SS_U32(o, SO_TYPE);
	if (t > 3)
		t = 0;
	out_say(types + (u8)types[t]);
	out_puts(")");
}

/* « volume 70 % (muet), sortie 1 « jack » (jack) » */
static NORETURN void show(void)
{
	const u8 *o = main_out();
	if (!o)
		noout();
	out_say("volume ");
	out_putn(SS_U32(o, SO_VOLUME));
	out_puts(" %");
	if (SS_U32(o, SO_MUTE))
		out_say(" (muet)");
	out_say(", sortie ");
	out_desc(o);
	out_nl();
	exit(0);
}

/* nombre, largeur : aligné à droite (put_w) */
static void put_w(u64 v, long w)
{
	char b[32];
	long n = fmt_dec(v, b) - b;
	for (w -= n; w > 0; w--)
		out_putc(' ');
	out_puts(b);
}

static void list_apps(void)
{
	const u8 *c = st + SS_CLIS;
	u64 seen = 0;
	for (int i = 0; i < SS_CLI_MAX; i++, c += 64) {
		if (!SS_U32(c, SC_PID))
			continue;
		if (!seen)
			out_say("  PID  VOLUME  CRÊTE  NOM\n");
		seen++;
		put_w(SS_U32(c, SC_PID), 5);
		put_w(SS_U32(c, SC_VOLUME), 8);
		put_w((u64)SS_U32(c, SC_PEAK) * 100 / 32767, 6);
		out_puts(" %  ");
		out_puts((const char *)c + SC_NAME);
		if (SS_U32(c, SC_MUTE))
			out_say("  (muet)");
		out_nl();
	}
	if (!seen)
		out_say("aucun programme ne joue\n");
}

static NORETURN void set_mute(u64 mute)
{
	const u8 *o = main_out();
	if (!o)
		noout();
	if (ak_out_volume(SS_U32(o, SO_ID), SS_U32(o, SO_VOLUME), (long)mute) < 0)
		failed();
	sys_sleep(50);
	show();
}

/* volume output [choix] */
static NORETURN void cmd_output(int argc, char **argv)
{
	if (argc >= 3) {
		const char *a = argv[2];
		u64 id = 0;
		if (!str_eq(a, "auto")) {
			long want = num100(a); /* un numéro, sinon (-1) un type ou un nom */
			const u8 *o = st + SS_OUTS;
			int i;
			for (i = 0; i < SS_OUT_MAX; i++, o += 64) {
				u32 oid = SS_U32(o, SO_ID);
				if (!oid)
					continue;
				if (want >= 0) {
					if (oid == (u64)want)
						break;
					continue;
				}
				u32 t = SS_U32(o, SO_TYPE);
				if (t <= 3 && str_eq(c_types + (u8)c_types[t], a))
					break;
				if (str_eq((const char *)o + SO_NAME, a))
					break;
			}
			if (i == SS_OUT_MAX)
				fail_msg("pas de sortie de ce nom (volume output : les sorties)", 1);
			id = SS_U32(o, SO_ID);
		}
		if (ak_main((long)id) < 0)
			failed();
		sys_sleep(50); /* le temps que sound bascule */
	}
	out_say("sorties (* : celle qui joue) :\n");
	const u8 *o = st + SS_OUTS;
	for (int i = 0; i < SS_OUT_MAX; i++, o += 64) {
		if (!SS_U32(o, SO_ID))
			continue;
		out_putc(SS_U32(o, SO_FLAGS) & 1 ? '*' : ' ');
		out_putc(' ');
		out_desc(o);
		out_say(", volume ");
		out_putn(SS_U32(o, SO_VOLUME));
		out_puts(" %");
		if (SS_U32(o, SO_MUTE))
			out_say(" (muet)");
		if (SS_U32(o, SO_FLAGS) & 2)
			out_say(", préférée");
		out_nl();
	}
	out_say("volume output auto|jack|hdmi|usb|<numéro> : la sortie préférée\n");
	exit(0);
}

/* volume app <nom|pid> <0..100|mute|unmute> */
static NORETURN void cmd_app(int argc, char **argv)
{
	if (argc != 4)
		fail_msg("usage : volume app <nom|pid> <0..100|mute|unmute>", 2);
	const char *who = argv[2], *what = argv[3];
	long vol = -1; /* (-1 : le sien) */
	u64 mute = 0;
	if (str_eq(what, "mute")) {
		mute = 1;
	} else if (!str_eq(what, "unmute")) {
		vol = num100(what);
		if (vol < 0)
			fail_msg("usage : volume app <nom|pid> <0..100|mute|unmute>", 2);
	}
	u64 pid = num_pid(who);
	const u8 *c = st + SS_CLIS;
	u64 done = 0;
	for (int i = 0; i < SS_CLI_MAX; i++, c += 64) {
		u32 p = SS_U32(c, SC_PID);
		if (!p)
			continue;
		if (pid ? p != pid : !str_eq((const char *)c + SC_NAME, who))
			continue;
		if (ak_client_volume(p, vol < 0 ? (long)SS_U32(c, SC_VOLUME) : vol, (long)mute) < 0)
			failed();
		done++;
	}
	if (!done) {
		err_puts(argv[0]);
		err_puts(": ");
		err_puts(who);
		err_say(" ne joue pas (volume apps : les programmes qui jouent)\n");
		exit(1);
	}
	sys_sleep(50);
	list_apps();
	exit(0);
}

int main(int argc, char **argv)
{
	st = (const u8 *)ak_state();
	if (!st)
		fail_msg("pas de service sound", 1);
	if (argc < 2)
		show();
	const char *a = argv[1];
	if (str_eq(a, "mute"))
		set_mute(1);
	if (str_eq(a, "unmute"))
		set_mute(0);
	if (str_eq(a, "toggle")) {
		const u8 *o = main_out();
		if (!o)
			noout();
		set_mute(SS_U32(o, SO_MUTE) == 0);
	}
	if (str_eq(a, "output"))
		cmd_output(argc, argv);
	if (str_eq(a, "apps")) {
		list_apps();
		exit(0);
	}
	if (str_eq(a, "app"))
		cmd_app(argc, argv);
	if (argc != 2)
		usage();
	long v = num100(a);
	if (v < 0)
		usage();
	const u8 *o = main_out();
	if (!o)
		noout();
	if (ak_out_volume(SS_U32(o, SO_ID), v, 0) < 0)
		failed();
	sys_sleep(50); /* le temps que sound publie son état */
	show();
}
