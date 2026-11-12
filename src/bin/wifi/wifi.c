/* wifi - état, scan et connexion du Wi-Fi (service « wifi », par NetKit),
 * comme wifi.asm :
 *   wifi                        état de la connexion
 *   wifi scan                   réseaux visibles
 *   wifi join SSID [clé]        connexion, mémorisée (sans clé : réseau ouvert)
 *   wifi leave                  déconnexion (plus de reconnexion automatique)
 *   wifi known                  réseaux connus (SD0:/etc/wifi.txt, sans les clés)
 *   wifi forget SSID            oublie un réseau
 *   wifi priority SSID N        priorité d'un réseau (le plus grand d'abord)
 *   wifi country XX             pays (réglementation radio), tout de suite
 * (anciens mots : connecter, quitter)
 * Comme l'asm : les mots viennent de la ligne d'arguments du noyau (krt_line),
 * coupée aux seules espaces (pas de guillemets) ; les mots en trop sont
 * ignorés ; sortie sans tampon ; usage : code 2 ; pas de Wi-Fi ou erreur : 1. */
#include "korinrt.h"
#include "netkit_import.h"

static u8 wst[64];
static u32 ni[8];
static u8 tab[64 * 48];
static char line[4096 + 2];

static const char *const sec_names[] = {"ouvert | ", "WEP    | ", "WPA    | ", "WPA2   | "};

/* chaîne : les espaces deviennent des 0, double 0 final -> nombre de mots */
static long split(char *s)
{
	long n = 0;
	int in = 0;
	for (; *s; s++) {
		if (*s == ' ') {
			*s = 0;
			in = 0;
		} else if (!in) {
			in = 1;
			n++;
		}
	}
	s[1] = 0; /* (fin de liste) */
	return n;
}

/* mot -> mot suivant (après le(s) 0) */
static char *next_word(char *s)
{
	while (*s++)
		;
	while (!*s)
		s++;
	return s;
}

/* 6 octets -> aa:bb:cc:dd:ee:ff */
static void print_mac(const u8 *m)
{
	static const char hex[] = "0123456789abcdef";
	char b[32], *p = b;
	for (int i = 0;;) {
		*p++ = hex[m[i] >> 4];
		*p++ = hex[m[i] & 15];
		if (++i >= 6)
			break;
		*p++ = ':';
	}
	*p = 0;
	ak_puts(b);
}

/* adresse IPv4 -> sortie standard (le print_ip de NetKit écrit sur la console
 * du noyau, donc dans le journal, pas dans le tube du terminal) */
static void put_ip(u32 ip)
{
	char b[32];
	fmt_ip(ip, (long)b);
	ak_puts(b);
}

static NORETURN void usage(void)
{
	lang_puts("usage : wifi [scan | join SSID [cle] | leave | known | forget SSID |"
		  " priority SSID N | country XX]\n");
	_exit(2);
}

static NORETURN void absent(void)
{
	lang_puts("wifi : pas de WiFi (puce absente, firmware manquant ou QEMU)\n");
	_exit(1);
}

/* code d'erreur : son message, code 1 */
static NORETURN void fail(long e)
{
	static const struct {
		long code;
		const char *msg;
	} errs[] = {
		{-19, "wifi : pas de WiFi\n"},
		{-2, "wifi : réseau introuvable\n"},
		{-13, "wifi : clé refusée\n"},
		{-110, "wifi : délai d'attente dépassé\n"},
		{-22, "wifi : SSID (1-32) ou clé (8-63 caractères) invalide\n"},
		{0, 0},
	};
	int i = 0;
	while (errs[i].code && errs[i].code != e)
		i++;
	if (errs[i].code)
		lang_puts(errs[i].msg);
	else {
		lang_puts("wifi : erreur ");
		print_dec(e);
		ak_puts("\n");
	}
	_exit(1);
}

static NORETURN void ok(void)
{
	_exit(0);
}

static NORETURN void do_status(void)
{
	if (wifi_status((long)wst) < 0 || !*(u32 *)wst)
		absent();
	u32 st = *(u32 *)wst;
	if (st == 1) {
		lang_puts("wifi : démarrage en cours\n");
		ok();
	}
	lang_puts("wifi : MAC ");
	print_mac(wst + 8);
	ak_puts("\n");
	if (st != 4) {
		lang_puts(st == 3 ? "wifi : connexion en cours\n" : "wifi : non connecté\n");
		ok();
	}
	lang_puts("wifi : connecté à ");
	ak_puts((const char *)wst + 16);
	ak_puts(", signal ");
	print_dec(*(i32 *)(wst + 4));
	ak_puts(" dBm\n");
	net_info((long)ni); /* adresse IP (DHCP) */
	if (ni[0]) {
		lang_puts("wifi : adresse ");
		put_ip(ni[0]);
		ak_puts("\n");
	}
	ok();
}

static NORETURN void do_scan(void)
{
	lang_puts("wifi : recherche des réseaux...\n");
	long n = wifi_scan((long)tab, 64);
	if (n < 0)
		fail(n);
	if (!n) {
		lang_puts("wifi : aucun réseau visible\n");
		ok();
	}
	lang_puts("dBm | canal | sécurité | réseau\n");
	for (u8 *e = tab; n; n--, e += 48) {
		print_dec(*(int16_t *)(e + 36)); /* RSSI */
		ak_puts(" | ");
		print_dec(e[33]); /* canal */
		ak_puts(" | ");
		lang_puts(sec_names[e[34]]); /* sécurité (comme l'asm : sans contrôle de la valeur) */
		e[e[32]] = 0; /* SSID (pas forcément terminé) */
		ak_puts((const char *)e);
		ak_puts("\n");
	}
	ok();
}

static NORETURN void do_known(void)
{
	long n = wifi_known((long)tab, 16);
	if (n < 0)
		fail(n);
	if (!n) {
		lang_puts("wifi : aucun réseau connu\n");
		ok();
	}
	for (u8 *e = tab; n; n--, e += 48) {
		print_dec(*(u32 *)(e + 40)); /* priorité */
		ak_puts(" | ");
		lang_puts(sec_names[*(u32 *)(e + 36)]); /* sécurité */
		ak_puts((const char *)e);
		ak_puts("\n");
	}
	ok();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!krt_line)
		do_status();
	u64 len = (u64)str_len(krt_line);
	if (len > sizeof line - 2)
		len = sizeof line - 2;
	memcpy(line, krt_line, len);
	line[len] = 0;
	line[len + 1] = 0;
	long words = split(line);
	if (!words)
		do_status();
	char *w = line;
	if (!*w) /* espaces en tête */
		w = next_word(w);
	if (str_eq(w, "scan"))
		do_scan();
	int join = str_eq(w, "connecter") || str_eq(w, "join");
	int leave = !join && (str_eq(w, "quitter") || str_eq(w, "leave"));
	if (join) {
		if (words < 2)
			usage();
		char *ssid = next_word(w); /* 2e mot : SSID */
		char *key = 0; /* 3e mot : clé (facultative) */
		if (words >= 3)
			key = next_word(ssid);
		lang_puts("wifi : connexion à ");
		ak_puts(ssid);
		ak_puts("...\n");
		long r = wifi_join((long)ssid, (long)key);
		if (r < 0)
			fail(r);
		lang_puts("wifi : connecté (adresse IP par DHCP)\n");
		ok();
	}
	if (leave) {
		long r = wifi_leave();
		if (r < 0)
			fail(r);
		lang_puts("wifi : déconnecté\n");
		ok();
	}
	if (str_eq(w, "known"))
		do_known();
	long r;
	if (str_eq(w, "forget")) {
		if (words < 2)
			usage();
		r = wifi_forget((long)next_word(w));
	} else if (str_eq(w, "priority")) {
		if (words < 3)
			usage();
		char *ssid = next_word(w);
		r = wifi_priority((long)ssid, (long)parse_dec(next_word(ssid)));
	} else if (str_eq(w, "country")) {
		if (words < 2)
			usage();
		r = wifi_country((long)next_word(w));
	} else
		usage();
	if (r < 0)
		fail(r);
	ok();
}
