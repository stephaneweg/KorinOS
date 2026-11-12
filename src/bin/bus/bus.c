/* bus - le bus d'événements du système (busd, docs/BUS.md) en ligne de commande, comme bus.asm :
 *	bus list			les sujets et leur valeur
 *	bus get <nom>			la valeur d'un sujet
 *	bus pub [-r] <nom> <valeur>	publie (-r : valeur gardée, BUS_RETAIN)
 *	bus del <nom>			retire un sujet
 *	bus watch [-c] <motif>		affiche les événements jusqu'à Ctrl+C
 *					(-c : d'abord les valeurs gardées)
 * Motif : un nom, ou un préfixe suivi de "*" ("korin.usb.*", "*" : tout).
 *
 * Comme l'asm : la ligne d'arguments est lue telle quelle (mots coupés aux espaces seulement, pas
 * de guillemets) ; busd est interrogé avant de reconnaître la commande (busd absent : « ne répond
 * pas » même pour une commande inconnue) ; les mots en trop sont ignorés ; la valeur de pub est
 * le reste de la ligne ; une valeur s'écrit sur une ligne, contrôles (et octets nuls) en « . »,
 * 256 octets au plus. Usage et erreurs : code 1. Sortie sans tampon (puts d'AppKit). */
#include "korinrt.h"
#include "buskit_import.h"

#define WEV_BUS 13
#define BEV_TOPIC 8
#define BEV_LEN 12
#define BEV_DATA 16
#define BUS_INLINE 12
#define BUS_DELETED 0xFFFFFFFFu
#define BUS_NAME_MAX 64
#define BUS_VALUE_MAX 256
#define BUS_RETAIN 1
#define BUS_DELETE 2
#define BUS_CURRENT 1
#define BUS_ENOENT -2

static u8 ev[32];
static char t_name[BUS_NAME_MAX];
static char t_value[BUS_VALUE_MAX];
static char t_line[BUS_VALUE_MAX + 8];

static NORETURN void usage(void)
{
	lang_puts("usage : bus list | get <nom> | pub [-r] <nom> <valeur> | del <nom> | watch [-c] <motif>\n");
	_exit(1);
}

/* r = BUS_E* */
static NORETURN void fail(long r)
{
	if (r == BUS_ENOENT)
		lang_puts("bus : sujet inconnu\n");
	else {
		lang_puts("bus : erreur ");
		print_dec(r);
		ak_puts("\n");
	}
	_exit(1);
}

/* texte -> premier mot (espaces sautés, coupé par un 0) ; *rest : la suite */
static char *next_word(char *s, char **rest)
{
	while (*s == ' ')
		s++;
	char *e = s;
	while (*e && *e != ' ')
		e++;
	if (*e)
		*e++ = 0;
	*rest = e;
	return s;
}

/* valeur, octets : sur une ligne (contrôles en « . ») */
static void print_value(const char *v, u64 n)
{
	if (n >= BUS_VALUE_MAX)
		n = BUS_VALUE_MAX;
	u64 i;
	for (i = 0; i < n; i++) {
		u8 c = (u8)v[i];
		t_line[i] = c < ' ' ? '.' : (char)c;
	}
	t_line[i++] = '\n';
	t_line[i] = 0;
	ak_puts(t_line);
}

static void cmd_list(void)
{
	long id = 0;
	for (;;) {
		id = bus_list(id, (long)t_name);
		if (!id)
			return;
		ak_puts(t_name);
		ak_puts(" = ");
		long r = bus_get_id(id, (long)t_value, BUS_VALUE_MAX);
		if (r < 0)
			r = 0; /* (retiré entre-temps) */
		print_value(t_value, (u64)r);
	}
}

static void cmd_get(char *rest)
{
	char *w = next_word(rest, &rest);
	if (!*w)
		usage();
	long r = bus_get((long)w, (long)t_value, BUS_VALUE_MAX);
	if (r < 0)
		fail(r);
	print_value(t_value, (u64)r);
}

static void cmd_pub(char *rest)
{
	long flags = 0;
	char *w = next_word(rest, &rest);
	if (str_eq(w, "-r")) {
		flags = BUS_RETAIN;
		w = next_word(rest, &rest);
	}
	if (!*w)
		usage();
	while (*rest == ' ') /* la valeur : le reste de la ligne */
		rest++;
	long r = bus_publish((long)w, (long)rest, -1, flags);
	if (r < 0)
		fail(r);
	lang_puts("bus : ");
	print_dec(r);
	lang_puts(" abonnement(s) prévenu(s)\n");
}

static void cmd_del(char *rest)
{
	char *w = next_word(rest, &rest);
	if (!*w)
		usage();
	long r = bus_publish((long)w, (long)t_value, 0, BUS_DELETE);
	if (r < 0)
		fail(r);
}

static void cmd_watch(char *rest)
{
	long flags = 0;
	char *w = next_word(rest, &rest);
	if (str_eq(w, "-c")) {
		flags = BUS_CURRENT;
		w = next_word(rest, &rest);
	}
	if (!*w)
		usage();
	long r = bus_subscribe((long)w, flags);
	if (r < 0)
		fail(r);
	for (;;) {
		if (bus_wait((long)ev, 60000) != WEV_BUS)
			continue;
		u32 len = *(u32 *)(ev + BEV_LEN);
		u32 topic = *(u32 *)(ev + BEV_TOPIC);
		if (len == BUS_DELETED) {
			lang_puts("sujet ");
			print_dec(topic);
			lang_puts(" : retiré\n");
			continue;
		}
		bus_name(topic, (long)t_name);
		ak_puts(t_name);
		ak_puts(" = ");
		if (len <= BUS_INLINE) { /* courte : celle de l'événement */
			print_value((char *)ev + BEV_DATA, len);
			continue;
		}
		r = bus_get_id(topic, (long)t_value, BUS_VALUE_MAX); /* longue : la valeur du moment */
		if (r < 0)
			r = 0;
		print_value(t_value, (u64)r);
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!krt_line)
		usage();
	u64 n = (u64)str_len(krt_line) + 1;
	char *line = malloc(n);
	memcpy(line, krt_line, n);
	char *rest;
	char *cmd = next_word(line, &rest);
	if (!*cmd)
		usage();
	if (!bus_available()) {
		lang_puts("bus : busd ne répond pas\n");
		_exit(1);
	}
	if (str_eq(cmd, "list"))
		cmd_list();
	else if (str_eq(cmd, "get"))
		cmd_get(rest);
	else if (str_eq(cmd, "pub"))
		cmd_pub(rest);
	else if (str_eq(cmd, "del"))
		cmd_del(rest);
	else if (str_eq(cmd, "watch"))
		cmd_watch(rest);
	else
		usage();
	_exit(0);
}
