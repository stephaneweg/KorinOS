/* bg.c - thread auxiliaire de netd, traduction de background.inc : ce qui peut
 * bloquer (fichiers, notifyd) ; notifications « Réseau connecté / perdu », heure
 * par NTP (SNTP, RFC 4330 : au premier bail puis toutes les 6 h ; serveur ntp=
 * de SD0:/etc/options.txt, pool.ntp.org par défaut, ntp=non : jamais), heure
 * locale par fuseau= (Europe/Paris par défaut, règles de SD0:/res/tz.txt)
 * écrite dans SD0:/etc/clock.cfg ; l'état du réseau publié sur le bus :
 * korin.net.state, le décalage de l'heure après chaque synchronisation :
 * korin.clock.offset (docs/BUS.md ; sans BusKit, comme buspub.inc). */
#include "netd.h"

#define NTP_PERIOD (6UL * 3600 * 1000) /* ms entre deux synchronisations */
#define NTP_RETRY 60000                /* après un échec */
#define NTP_1970 2208988800UL          /* secondes entre 1900 et 1970 */
#define TZBUF 16384                    /* SD0:/res/tz.txt (un peu plus de 12 Ko) */
#define INI_MAX 16384
#define BUS_RETAIN 1
#define BUS_NAME_MAX 64
#define BUS_VALUE_MAX 256
#define BUS_ENODEV -19
#define BUS_EINVAL -22

static u64 tz_r[8];          /* début et fin de l'heure d'été (tz_rule_parse) */
static u64 ntp_next;         /* prochaine synchronisation (ms) */
static volatile u64 ntp_force; /* demandée (op 17) */
static volatile u64 ntp_gen; /* synchronisations faites */
static volatile long ntp_last; /* résultat de la dernière */
static volatile u64 ntp_unix, ntp_ms; /* heure UTC à la synchro, instant (ms) */
static volatile u64 ntp_ok;
static u64 ntp_off;          /* ntp=non */
static char tz_name[32];
static char tz_rule[64];     /* règle POSIX du fuseau (tz_lookup) */
static char ntp_name[64];
static char nbuf[160];
static char cfgbuf[32];
static char npub[96];        /* korin.net.state : à publier, publié */
static char nlast[96];
static long bp_canal;        /* canal de busd + 1 (buspub.inc) */
static char tzbuf[TZBUF];
static char ini_buf[INI_MAX];

static const char tz_paris[] = "CET-1CEST,M3.5.0,M10.5.0/3";

/* ---- bus, sans BusKit (src/svc/inc/buspub.inc) */

/* attente en ms -> 1 si le canal de busd est ouvert */
static int bp_open(long ms)
{
	if (bp_canal)
		return 1;
	long c = korin_sys2(SYS_SVC_OPEN, "bus", ms);
	if (c < 0)
		return 0;
	bp_canal = c + 1;
	return 1;
}

/* sujet, valeur, drapeaux -> réponse de busd, < 0 : erreur ou pas de canal
 * (busd relancé : le canal est oublié, à rouvrir) */
static long bp_publish(const char *topic, const char *val, u64 flags)
{
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	W64(m, 0) = 1; /* BUS_OP_PUBLISH */
	W64(m, MSG_A) = flags;
	u8 *d = m + MSG_DATA;
	for (int k = BUS_NAME_MAX;;) { /* le nom */
		u8 c = (u8)*topic++;
		*d++ = c;
		if (!c)
			break;
		if (!--k)
			return BUS_EINVAL;
	}
	u8 *v = d; /* la valeur */
	if (val)
		for (int k = BUS_VALUE_MAX - 1; k && *val; k--)
			*d++ = (u8)*val++;
	*d = 0;
	W64(m, MSG_B) = (u64)(d - v);
	long c = bp_canal;
	if (!c)
		return BUS_ENODEV;
	long r = korin_sys2(SYS_SVC_CALL, c - 1, m);
	if (r >= 0 || r == (long)W64(m, 0)) /* réponse de busd ? */
		return r;
	bp_canal = 0; /* non : le canal est mort */
	return BUS_ENODEV;
}

/* ---- petits outils */

/* -> fin (copiée sans son 0 final) (bp_cat) */
static char *cat(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	return d;
}

/* str_copy_n d'applist.inc : au plus n - 1 octets, puis 0 */
static void str_copy_n(char *d, const char *s, u64 n)
{
	for (n--; n && *s; n--)
		*d++ = *s++;
	*d = 0;
}

/* adresse, tampon -> fin (« a.b.c.d ») */
static char *fmt_ip4(u32 a, char *p)
{
	for (int sh = 24;; sh -= 8) {
		p = fmt_dec((a >> sh) & 0xFF, p);
		if (!sh)
			return p;
		*p++ = '.';
		*p = 0;
	}
}

/* chaîne -> sans espaces (ni tabulations, ni CR) devant ni derrière (ini_trim) */
static char *ini_trim(char *s)
{
	while (*s == ' ' || *s == 9)
		s++;
	char *e = s + strlen(s);
	while (e > s && (u8)e[-1] <= ' ')
		*--e = 0;
	return s;
}

/* chemin, fonction (clé en minuscules, valeur ; sans espaces autour) : appelée
 * pour chaque ligne « clé = valeur » -> 0 si le fichier manque (ini_read) */
static int ini_read(const char *path, void (*fn)(const char *k, const char *v))
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, ini_buf, INI_MAX - 1);
	sys_close(fd);
	if (n < 0)
		n = 0;
	ini_buf[n] = 0;
	char *p = ini_buf;
	while (*p) {
		char *e = p; /* fin de ligne -> 0 ; « # » : commentaire */
		while (*e && *e != 10) {
			if (*e == '#')
				*e = 0;
			e++;
		}
		char c = *e;
		*e = 0;
		char *q = p; /* clé = valeur ? */
		while (*q && *q != '=')
			q++;
		if (*q == '=') {
			*q = 0;
			char *k = ini_trim(p);
			for (char *x = k; *x; x++) /* clé en minuscules (ASCII) */
				if ((u8)(*x - 'A') <= 25)
					*x += 32;
			fn(k, ini_trim(q + 1));
		}
		if (!c)
			break;
		p = e + 1;
	}
	return 1;
}

/* ---- options.txt : fuseau=, ntp= */

static void opt_line(const char *k, const char *v)
{
	if (str_eq(k, "fuseau")) {
		str_copy_n(tz_name, v, 32);
	} else if (str_eq(k, "ntp")) {
		ntp_off = (u64)str_eq(v, "non");
		if (!ntp_off)
			str_copy_n(ntp_name, v, 64);
	}
}

static void opts_load(void)
{
	tz_name[0] = 0;
	ntp_name[0] = 0;
	ntp_off = 0;
	ini_read("SD0:/etc/options.txt", opt_line);
}

/* ---- notifications, état sur le bus */

/* 1 connecté, 0 perdu : notification (« Ethernet — 10.0.0.5 »...) */
static void notify_net(int up)
{
	nbuf[0] = 0;
	long a = if_active; /* le genre de l'interface active */
	if (a >= 0) {
		char *p;
		if (ifs[a].type == NIF_WIFI) {
			p = str_cat(nbuf, "Wi-Fi « ");
			p = str_cat(p, ifs[a].ssid);
			p = str_cat(p, " »");
		} else {
			p = str_cat(nbuf, ifs[a].type == NIF_ETHERNET ? "Ethernet" : "USB");
		}
		if (up) {
			p = str_cat(p, " — ");
			fmt_ip4(net_ip, p);
		}
	}
	notify_send(up ? "Réseau connecté" : "Réseau perdu", nbuf, 0);
	ak_puts("netd : "); /* (et au journal) */
	ak_puts(nbuf);
	ak_puts("\n");
}

/* korin.net.state : « up <genre> <adresse> <interface> » ou « down », publié
 * (gardé) quand il change ; rien tant que busd n'est pas là */
static void net_publish(void)
{
	if (!bp_open(0))
		return;
	char *p = npub;
	long a = if_active;
	if (dhcp_state == DHCP_BOUND && net_ip && a >= 0) {
		struct iface *f = &ifs[a];
		p = cat(p, "up ");
		p = cat(p, f->type == NIF_WIFI ? "wifi" : f->type == NIF_USB ? "usb" : "ethernet");
		*p++ = ' ';
		p = fmt_ip4(net_ip, p);
		*p++ = ' ';
		p = cat(p, f->name);
	} else {
		p = cat(p, "down");
	}
	*p = 0;
	if (str_eq(npub, nlast)) /* déjà publié ? */
		return;
	str_copy_n(nlast, npub, 96);
	if (bp_publish("korin.net.state", npub, BUS_RETAIN) < 0)
		nlast[0] = 0; /* (raté : au prochain tour) */
}

/* ---- fuseau */

/* chiffres -> nombre ; *pp : la suite */
static u64 tz_num(const char **pp)
{
	const char *p = *pp;
	u64 v = 0;
	while ((u8)(*p - '0') <= 9)
		v = v * 10 + (u64)(*p++ - '0');
	*pp = p;
	return v;
}

/* règle -> après son nom (« CET », « <+0530> ») */
static const char *tz_skip_name(const char *p)
{
	if (*p == '<') {
		for (;;) {
			char c = *p++;
			if (!c)
				return p - 1;
			if (c == '>')
				return p;
		}
	}
	while ((u8)((*p | 0x20) - 'a') <= 25)
		p++;
	return p;
}

/* « [+-]h[:mm[:ss]] » -> secondes ; *pp : la suite */
static long tz_off(const char **pp)
{
	const char *p = *pp;
	int neg = 0;
	if (*p == '+') {
		p++;
	} else if (*p == '-') {
		neg = 1;
		p++;
	}
	long s = (long)tz_num(&p) * 3600;
	if (*p == ':') {
		p++;
		s += (long)tz_num(&p) * 60;
		if (*p == ':') {
			p++;
			s += (long)tz_num(&p);
		}
	}
	*pp = p;
	return neg ? -s : s;
}

/* « Mmois.semaine.jour[/heure] » -> 4 mots (mois, semaine 1..5, jour 0
 * dimanche, heure locale en s, 2 h sans) ; la suite (0 : autre forme) */
static const char *tz_rule_parse(const char *p, u64 *r)
{
	if (*p != 'M')
		return 0;
	p++;
	r[0] = tz_num(&p);
	p++;
	r[1] = tz_num(&p);
	p++;
	r[2] = tz_num(&p);
	r[3] = 7200;
	if (*p == '/') {
		p++;
		r[3] = (u64)tz_off(&p);
	}
	return p;
}

/* année, mois, jour -> jours depuis 1970 (H. Hinnant) */
static u64 tz_dfc(u64 y, u64 m, u64 d)
{
	if (m <= 2) {
		y--;
		m += 9;
	} else {
		m -= 3;
	}
	u64 era = y / 400, yoe = y - era * 400;
	u64 doy = (153 * m + 2) / 5 + d - 1; /* jour de l'année (depuis mars) */
	u64 doe = yoe * 365 + (yoe >> 2) - yoe / 100 + doy; /* jour de l'ère */
	return era * 146097 + doe - 719468;
}

/* jours depuis 1970 -> année */
static u64 year_of(u64 z)
{
	z += 719468;
	u64 era = z / 146097, doe = z - era * 146097;
	u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* année de l'ère */
	u64 y = yoe + era * 400;
	u64 doy = doe - (yoe * 365 + (yoe >> 2) - yoe / 100); /* jour de l'année (depuis mars) */
	u64 mp = (doy * 5 + 2) / 153; /* mois depuis mars */
	return mp >= 10 ? y + 1 : y; /* janvier, février : année suivante */
}

/* année, règle -> son jour (depuis 1970) */
static u64 tz_day(u64 y, const u64 *r)
{
	u64 day = tz_dfc(y, r[0], 1); /* le 1er du mois */
	u64 wd = (day + 4) % 7; /* son jour de la semaine (0 : dimanche) */
	day += (r[2] - wd + 7) % 7; /* le premier jour voulu */
	day += (r[1] - 1) * 7; /* la semaine voulue */
	if (r[1] < 5)
		return day;
	u64 m = r[0] + 1; /* 5 : la dernière du mois */
	if (m > 12) {
		m = 1;
		y++;
	}
	if (day >= tz_dfc(y, m, 1))
		day -= 7;
	return day;
}

/* heure UTC, règle POSIX (« CET-1CEST,M3.5.0,M10.5.0/3 ») -> décalage de
 * l'heure locale (s) */
static long tz_posix(u64 utc, const char *rule)
{
	const char *p = tz_skip_name(rule);
	long std = -tz_off(&p); /* heure d'hiver */
	if (!*p)
		return std;
	p = tz_skip_name(p);
	long dst = std + 3600; /* heure d'été (une heure de plus) */
	if (*p != ',') {
		if (!*p)
			return std;
		dst = -tz_off(&p);
		if (*p != ',')
			return std;
	}
	p = tz_rule_parse(p + 1, tz_r);
	if (!p || *p != ',')
		return std;
	p = tz_rule_parse(p + 1, tz_r + 4);
	if (!p)
		return std;
	u64 y = year_of((utc + (u64)std) / 86400); /* l'année locale */
	long start = (long)(tz_day(y, tz_r) * 86400 + tz_r[3]) - std; /* début (UTC), d'après l'heure d'hiver */
	long end = (long)(tz_day(y, tz_r + 4) * 86400 + tz_r[7]) - dst; /* fin, d'après l'heure d'été */
	long t = (long)utc;
	if (start >= end) /* (hémisphère sud : l'été passe l'année) */
		return (t < end || t >= start) ? dst : std;
	return (t < start || t >= end) ? std : dst;
}

/* tz_name cherché dans SD0:/res/tz.txt (lignes « nom règle ») : sa règle dans
 * tz_rule -> 1 si trouvé */
static int tz_lookup(void)
{
	long fd = sys_open("SD0:/res/tz.txt", O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, tzbuf, TZBUF - 1);
	sys_close(fd);
	if (n <= 0)
		return 0;
	tzbuf[n] = 0;
	const char *l = tzbuf;
	while (*l) {
		const char *a = tz_name, *b = l;
		while (*a && *a == *b) {
			a++;
			b++;
		}
		if (!*a && *b == ' ') {
			b++;
			char *d = tz_rule;
			for (int k = 63; k && (u8)*b > ' '; k--) /* (fin de ligne, CR) */
				*d++ = *b++;
			*d = 0;
			return 1;
		}
		for (;;) { /* ligne suivante */
			char c = *l;
			if (!c)
				return 0;
			l++;
			if (c == 10)
				break;
		}
	}
	return 0;
}

/* heure UTC -> décalage de l'heure locale (s) d'après tz_name : UTC/GMT[+-h], ou
 * un fuseau de SD0:/res/tz.txt ; inconnu ou absent : Europe/Paris */
static long tz_offset(u64 utc)
{
	if (tz_name[0] == 'U' || tz_name[0] == 'G') { /* « UTC+2 », « GMT-5 » */
		char c = tz_name[3];
		if (!c)
			return 0;
		if (c == '+' || c == '-') {
			long h = (long)parse_dec(tz_name + 4) * 3600;
			return c == '-' ? -h : h;
		}
	}
	return tz_posix(utc, tz_lookup() ? tz_rule : tz_paris);
}

/* ---- NTP */

/* serveur -> secondes depuis 1970 (< 0 : erreur), *at = instant de la réponse
 * (ms du démarrage). Socket UDP interne, 3 essais de 2 s. */
static long ntp_query(u32 ip, u64 *at)
{
	u8 q[128];
	if (!arp_resolve_wait(ip))
		return E_HOSTUNREACH;
	memset(q, 0, sizeof q); /* requête : 48 octets, LI 0, VN 4, mode 3 */
	q[0] = 0x23;
	nl_take();
	struct sock *sk = sock_alloc();
	if (!sk || (sk->type = SOCK_UDP, !sk_alloc_rings(sk, 0))) {
		if (sk)
			sock_free(sk);
		nl_drop();
		return E_NOMEM;
	}
	sk->lport = sock_ephemeral();
	nl_drop();
	long res;
	u8 *rep = q + 64; /* en-tête de l'enregistrement, puis la réponse */
	for (int tries = 3;;) {
		nl_take();
		udp_output((u32)sk->lport, ip, 123, q, 48);
		nl_drop();
		u64 end = time_ms() + 2000;
	wait:
		nl_take();
		if (sk->rxhead != sk->rxtail) {
			sk_rx_read(sk, rep, 8);
			u64 l = *(u16 *)rep; /* longueur */
			u64 take = l <= 48 ? l : 48;
			sk_rx_read(sk, rep, take);
			sk->rxtail += l - take; /* reste à jeter */
			nl_drop();
			/* mode 4 (serveur), strate non nulle */
			if (l < 48 || (rep[0] & 7) != 4 || !rep[1])
				goto wait;
			res = (long)((u64)ld32be(rep + 40) - NTP_1970); /* horodatage d'émission (secondes) */
			*at = time_ms();
			break;
		}
		nl_drop();
		if (time_ms() < end) {
			thread_sleep(5);
			goto wait;
		}
		if (!--tries) {
			res = E_TIMEDOUT;
			break;
		}
	}
	nl_take();
	sock_free(sk);
	nl_drop();
	return res;
}

/* une synchronisation -> 0, ou < 0 */
static long ntp_sync(void)
{
	if (ntp_off)
		return E_NOENT;
	long ip = dns_resolve(ntp_name[0] ? ntp_name : "pool.ntp.org"); /* le serveur */
	if (ip < 0)
		return ip;
	u64 at;
	long secs = ntp_query((u32)ip, &at);
	if (secs < 0)
		return secs;
	ntp_unix = (u64)secs;
	ntp_ms = at;
	ntp_ok = 1;
	long off = tz_offset((u64)secs); /* heure locale */
	u64 local = (u64)(secs + off);
	/* l'horloge du système ; CLOCK_SET_EXACT (src/kernel/core/sys.c) : l'heure NTP peut la faire reculer (une
	 * horloge CMOS à l'heure locale de Windows, lue comme UTC, mettait Korin en avance pour de bon) */
	korin_sys3(SYS_CLOCK_SET, secs, off, 0x4E545021);
	local -= at / 1000; /* - secondes depuis le démarrage */
	local %= 86400;
	char *e = fmt_dec(local, cfgbuf); /* SD0:/etc/clock.cfg */
	*e++ = 10;
	u64 n = (u64)(e - cfgbuf);
	long fd = sys_open("SD0:/etc/clock.cfg", O_WRITE | O_CREATE | O_TRUNC);
	if (fd >= 0) {
		sys_write(fd, cfgbuf, n);
		sys_close(fd);
	}
	/* sur le bus, sans la fin de ligne : korin.clock.offset (la barre de menus,
	 * l'Horloge et clockd ne relisent pas le fichier) */
	cfgbuf[n - 1] = 0;
	if (bp_open(0))
		bp_publish("korin.clock.offset", cfgbuf, BUS_RETAIN);
	ak_puts("netd : NTP "); /* « netd : NTP 1730000000 (UTC) » */
	print_dec(secs);
	ak_puts(" (UTC)\n");
	return 0;
}

NORETURN void bg_thread(void)
{
	opts_load();
	int seen = 0; /* bail vu au dernier tour */
	for (;;) {
		korin_sys1(SYS_SLEEP, 500);
		int up = dhcp_state == DHCP_BOUND && net_ip != 0; /* adresse obtenue ? */
		net_publish();
		if (up != seen) {
			seen = up;
			notify_net(up);
			if (up) /* nouveau bail : l'heure tout de suite */
				ntp_next = 0;
		}
		if (!ntp_force) {
			if (!up || time_ms() < ntp_next)
				continue;
		}
		opts_load();
		long r = ntp_sync();
		ntp_force = 0;
		ntp_gen++;
		ntp_last = r;
		u64 d = r == 0 ? NTP_PERIOD : NTP_RETRY;
		if (ntp_off) /* (ntp=non : rien, pas de nouvel essai) */
			d = 0x7FFFFFFF;
		ntp_next = time_ms() + d;
	}
}

/* 16 time -> a heure UTC (0 : inconnue), b 1 si NTP, c instant de la synchro */
long op_time(u8 *m)
{
	u64 ok = ntp_ok;
	W64(m, MSG_B) = ok;
	if (!ok) {
		W64(m, MSG_A) = 0;
		W64(m, MSG_C) = 0;
		return 0;
	}
	u64 u = ntp_unix, at = ntp_ms;
	W64(m, MSG_C) = at;
	W64(m, MSG_A) = (time_ms() - at) / 1000 + u;
	return 0;
}

/* 17 ntp -> 0 après une synchronisation réussie (20 s au plus) ; a = 0 : demande seulement */
long op_ntp(u8 *m)
{
	u64 wait = W64(m, MSG_A);
	u64 gen = ntp_gen;
	ntp_force = 1;
	if (!wait)
		return 0;
	u64 end = time_ms() + 20000;
	while (ntp_gen == gen) {
		if (time_ms() >= end)
			return E_TIMEDOUT;
		thread_sleep(50);
	}
	return ntp_last;
}
