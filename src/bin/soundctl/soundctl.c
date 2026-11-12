/* soundctl - le mixer en ligne de commande (service sound, par AudioKit ; docs/SON.md 4), comme
 * soundctl.asm :
 *	soundctl                         état : sorties, principale, clients, entrées
 *	soundctl volume <sortie> <0..100> [muet]
 *	soundctl app <pid|nom> <0..100> [muet]
 *	soundctl route <pid|nom> <sortie>       (0 : la principale)
 *	soundctl main <sortie>                  (0 : automatique)
 * Comme l'asm : la ligne d'arguments brute du noyau (krt_line), mots coupés aux espaces ; sans
 * service sound, le message (code 1) avant tout, même avant l'usage ; commande inconnue : l'usage
 * (code 2) ; un nombre absent ou illisible vaut 0 (parse_dec), ce qui suit est ignoré ; pid :
 * chiffres, sinon un client du mixer de ce nom, sinon un process de ce nom (pid 0 : « pas de
 * process de ce nom », code 1) ; résultat « soundctl : N », code 1 s'il est négatif ; sortie sans
 * tampon (puts, lang_puts, print_dec). */
#include "korinrt.h"
#include "audiokit_import.h"

static const u8 *st; /* le tampon d'état de sound */

/* noms des types de sortie : décalages depuis types, puis les noms (comme l'asm : le type n'est
 * pas vérifié ; ici, hors de la table, le dernier nom) */
static const char types[] = "\x04\x0e\x13\x18"
			    "virtuelle\0jack\0HDMI\0USB";

static NORETURN void done(long r)
{
	lang_puts("soundctl : ");
	print_dec(r);
	ak_puts("\n");
	_exit((int)((u64)r >> 63));
}

static NORETURN void noproc(void)
{
	lang_puts("soundctl : pas de process de ce nom\n");
	_exit(1);
}

/* texte -> premier mot (terminé par 0), *rest = suite (0 : fin) */
static char *next_word(char *s, char **rest)
{
	while (*s == ' ')
		s++;
	char *e = s;
	while (*e && *e != ' ')
		e++;
	if (!*e) {
		*rest = 0;
		return s;
	}
	*e = 0;
	*rest = e + 1;
	return s;
}

/* suite (0 : aucune) -> nombre (0 si absent), *rest = suite */
static u64 num_arg(char *s, char **rest)
{
	if (!s) {
		*rest = 0;
		return 0;
	}
	return parse_dec(next_word(s, rest));
}

/* suite -> pid (chiffres, ou nom d'un client du mixer, ou d'un process), *rest = suite */
static u64 pid_arg(char *s, char **rest)
{
	if (!s) {
		*rest = 0;
		return 0;
	}
	char *w = next_word(s, rest);
	if ((u8)(w[0] - '0') <= 9)
		return parse_dec(w);
	const u8 *c = st + SS_CLIS; /* un client de ce nom */
	for (int i = 0; i < SS_CLI_MAX; i++, c += 64)
		if (SS_U32(c, SC_PID) && str_eq((const char *)c + SC_NAME, w))
			return SS_U32(c, SC_PID);
	return (u64)proc_find(w, 0); /* sinon un process de ce nom */
}

static void type_name(u32 t)
{
	if (t > 3)
		t = 3;
	lang_puts(types + (u8)types[t]);
}

/* état du mixer */
static NORETURN void show(void)
{
	lang_puts("sortie principale : ");
	print_dec(*(const long *)(st + SS_MAIN));
	lang_puts(" (génération ");
	print_dec(*(const long *)(st + SS_GEN));
	ak_puts("\n");
	const u8 *o = st + SS_OUTS; /* sorties */
	for (int i = 0; i < SS_OUT_MAX; i++, o += 64) {
		if (!SS_U32(o, SO_ID))
			continue;
		lang_puts("sortie ");
		print_dec(SS_U32(o, SO_ID));
		ak_puts(" « ");
		ak_puts((const char *)o + SO_NAME);
		ak_puts(" » ");
		type_name(SS_U32(o, SO_TYPE));
		ak_puts(" ");
		print_dec(SS_U32(o, SO_RATE));
		ak_puts(" Hz, volume ");
		print_dec(SS_U32(o, SO_VOLUME));
		if (SS_U32(o, SO_MUTE))
			lang_puts(" (muet)");
		if (SS_U32(o, SO_FLAGS) & 1)
			lang_puts(", principale");
		if (SS_U32(o, SO_FLAGS) & 2)
			lang_puts(", préférée");
		lang_puts(", crête ");
		print_dec(SS_U32(o, SO_PEAK));
		ak_puts("\n");
	}
	const u8 *c = st + SS_CLIS; /* clients */
	for (int i = 0; i < SS_CLI_MAX; i++, c += 64) {
		if (!SS_U32(c, SC_PID))
			continue;
		ak_puts("client ");
		print_dec(SS_U32(c, SC_PID));
		ak_puts(" « ");
		ak_puts((const char *)c + SC_NAME);
		ak_puts(" » volume ");
		print_dec(SS_U32(c, SC_VOLUME));
		if (SS_U32(c, SC_MUTE))
			lang_puts(" (muet)");
		lang_puts(", sur la sortie ");
		print_dec(SS_U32(c, SC_OUTPUT));
		lang_puts(" (choisie : ");
		print_dec(SS_U32(c, SC_ROUTE));
		lang_puts("), flux ");
		print_dec(SS_U32(c, SC_STREAMS));
		lang_puts(", crête ");
		print_dec(SS_U32(c, SC_PEAK));
		ak_puts("\n");
	}
	const u8 *in = st + SS_INS; /* entrées */
	for (int i = 0; i < SS_IN_MAX; i++, in += 64) {
		if (!SS_U32(in, SO_ID))
			continue;
		lang_puts("entrée ");
		print_dec(SS_U32(in, SO_ID));
		ak_puts(" « ");
		ak_puts((const char *)in + SO_NAME);
		ak_puts(" » ");
		print_dec(SS_U32(in, SO_RATE));
		ak_puts(" Hz\n");
	}
	_exit(0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	st = (const u8 *)ak_state();
	if (!st) {
		lang_puts("soundctl : pas de service sound\n");
		_exit(1);
	}
	if (!krt_line)
		show();
	u64 n = (u64)str_len(krt_line) + 1;
	char *line = malloc(n);
	memcpy(line, krt_line, n);
	char *rest;
	char *cmd = next_word(line, &rest);
	if (str_eq(cmd, "volume")) {
		u64 out = num_arg(rest, &rest);
		u64 vol = num_arg(rest, &rest);
		u64 mute = num_arg(rest, &rest);
		done(ak_out_volume((long)out, (long)vol, (long)mute));
	}
	if (str_eq(cmd, "app")) {
		u64 pid = pid_arg(rest, &rest);
		if (!pid)
			noproc();
		u64 vol = num_arg(rest, &rest);
		u64 mute = num_arg(rest, &rest);
		done(ak_client_volume((long)pid, (long)vol, (long)mute));
	}
	if (str_eq(cmd, "route")) {
		u64 pid = pid_arg(rest, &rest);
		if (!pid)
			noproc();
		u64 out = num_arg(rest, &rest);
		done(ak_route((long)pid, (long)out));
	}
	if (str_eq(cmd, "main"))
		done(ak_main((long)num_arg(rest, &rest)));
	lang_puts("usage : soundctl [volume <sortie> <0..100> [muet] | app <pid|nom> <0..100> [muet] | "
		  "route <pid|nom> <sortie> | main <sortie>]\n");
	_exit(2);
}
