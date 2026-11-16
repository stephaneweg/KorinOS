/* wifisvc.c : le service « wifi » du PC, même protocole et même comportement que le pilote Wi-Fi du Pi
 * (src/svc/wifi/wifi.asm, docs/RESEAU.md 4 : opérations 1 à 8, réseaux connus de /etc/wifi.txt,
 * connexion automatique, état publié sur le bus korin.net.wifi, notifications), traduits en C ; la
 * puce est derrière les fonctions be_* (iwl.c). NetKit (wifi_*), le volet Wi-Fi de Réglages et
 * wifimenu servent tels quels.
 *
 * Connexion automatique (docs/RESEAU.md 4.2) : au démarrage, le réseau connu préféré ; puis balayage,
 * réseaux connus présents triés par priorité puis signal, essayés l'un après l'autre ; lien perdu : le
 * même réseau après 2 s, puis le cycle ; sans réseau connu joint, un balayage toutes les 5 s, 10 s…
 * jusqu'à 60 s, sans fin ; QUITTER : plus rien jusqu'au prochain REJOINDRE. Jamais de clé au journal. */
#include "iwl.h"

#define OP_STATUS 1
#define OP_SCAN 2
#define OP_JOIN 3
#define OP_LEAVE 4
#define OP_FORGET 5
#define OP_KNOWN 6
#define OP_PRIORITY 7
#define OP_COUNTRY 8
#define PER_MSG 9
#define WK_ENT 48
#define NET_MAX 16
#define CFG_MAX 8192
#define WS_ABSENT 0
#define WS_STARTING 1
#define WS_READY 2
#define WS_JOINING 3
#define WS_CONNECTED 4

struct known {
	char ssid[33];
	char key[65];
	int prio;
	int seen; /* (balayage) RSSI + 1000 du plus fort de ses points d'accès, 0 : pas vu */
};

static struct known known[NET_MAX];
static int known_n;
static char cc[3];
static volatile int auto_off, user_join, kick; /* kick : un balayage demandé a vu un réseau connu */
static char last_ssid[33];
static u32 cfg_lock;
static long canal = -1;
static struct scan_entry scan_tab[SCAN_ENT_MAX];
static int scan_n;
static u32 scan_lock;
static char cfgbuf[CFG_MAX];

/* ---- fichiers */
static long read_file(const char *path, char *buf, long max)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return fd;
	long n = 0;
	while (n < max) {
		long r = sys_read(fd, buf + n, (u64)(max - n));
		if (r <= 0)
			break;
		n += r;
	}
	sys_close(fd);
	return n;
}

static long write_file(const char *path, const char *buf, long n)
{
	long fd = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long r = sys_write(fd, buf, (u64)n);
	sys_close(fd);
	return r;
}

static void copy_z(char *d, const char *s, int size)
{
	int i = 0;
	for (; s[i] && i < size - 1; i++)
		d[i] = s[i];
	d[i] = 0;
}

static struct known *known_find(const char *ssid)
{
	for (int i = 0; i < known_n; i++)
		if (!strcmp(known[i].ssid, ssid))
			return &known[i];
	return 0;
}

static void known_set(const char *ssid, const char *key, int prio)
{
	struct known *k = known_find(ssid);
	if (!k) {
		if (known_n >= NET_MAX)
			return;
		k = &known[known_n++];
		memset(k, 0, sizeof *k);
	}
	copy_z(k->ssid, ssid, 33);
	copy_z(k->key, key, 65);
	k->prio = prio;
}

/* valeur brute -> valeur (sans espaces autour ; « "..." » : entre le premier et le dernier guillemet) ;
 * *quoted = 1 si entre guillemets */
static char *cfg_value(char *v, int *quoted)
{
	while (*v == ' ' || *v == '\t')
		v++;
	if (*v == '"') {
		v++;
		char *last = 0;
		for (char *p = v; *p; p++)
			if (*p == '"')
				last = p;
		if (last)
			*last = 0;
		*quoted = 1;
		return v;
	}
	char *end = v;
	for (char *p = v; *p; p++)
		if (*p != ' ' && *p != '\t')
			end = p + 1;
	*end = 0;
	*quoted = 0;
	return v;
}

static void cut_comment(char *v)
{
	for (char *p = v; *p; p++)
		if (*p == '#' && p > v && (p[-1] == ' ' || p[-1] == '\t')) {
			char *q = p - 1;
			*q = 0;
			while (q > v && (q[-1] == ' ' || q[-1] == '\t'))
				*--q = 0;
			return;
		}
}

static void set_cc(const char *v)
{
	if (v[0] && v[1]) {
		cc[0] = (char)(v[0] & ~0x20);
		cc[1] = (char)(v[1] & ~0x20);
		cc[2] = 0;
	}
}

/* lignes « clé=valeur » : ssid= (ou reseau=), key= (cle=, psk=), priority=, country= (pays=) */
static void cfg_read(const char *path)
{
	long n = read_file(path, cfgbuf, CFG_MAX - 1);
	if (n <= 0)
		return;
	cfgbuf[n] = 0;
	struct known *cur = 0;
	char *l = cfgbuf;
	while (*l) {
		char *e = l;
		while (*e && *e != '\n' && *e != '\r')
			e++;
		char save = *e;
		*e = 0;
		while (*l == ' ' || *l == '\t')
			l++;
		char *eq = l;
		if (*l != '#') {
			while (*eq && *eq != '=') {
				if (*eq >= 'A' && *eq <= 'Z')
					*eq = (char)(*eq | 0x20); /* (clé en minuscules) */
				eq++;
			}
		}
		if (*l != '#' && *eq == '=') {
			*eq = 0;
			int q;
			char *v = cfg_value(eq + 1, &q);
			if (!strcmp(l, "ssid") || !strcmp(l, "reseau")) {
				if (known_n < NET_MAX) {
					cur = &known[known_n++];
					memset(cur, 0, sizeof *cur);
					copy_z(cur->ssid, v, 33);
				} else
					cur = 0;
			} else if (!strcmp(l, "key") || !strcmp(l, "cle") || !strcmp(l, "psk")) {
				if (cur)
					copy_z(cur->key, v, 65);
			} else {
				if (!q)
					cut_comment(v);
				if (!strcmp(l, "priority")) {
					if (cur)
						cur->prio = (int)parse_dec(v);
				} else if (!strcmp(l, "country") || !strcmp(l, "pays"))
					set_cc(v);
			}
		}
		if (!save)
			break;
		l = e + 1;
	}
}

static void cfg_load(void)
{
	known_n = 0;
	cfg_read("/etc/wifi.txt");
	if (!known_n)
		cfg_read("/etc/wifi.bak");
	long n = read_file("/etc/options.txt", cfgbuf, CFG_MAX - 1); /* pays= (prioritaire) */
	if (n > 0) {
		cfgbuf[n] = 0;
		for (char *l = cfgbuf; *l;) {
			if (!strncmp(l, "pays=", 5) && l[5] >= 'A' && l[6] >= 'A')
				set_cc(l + 5);
			while (*l && *l != '\n')
				l++;
			if (*l)
				l++;
		}
	}
	ilog("wifi : %d réseau(x) connu(s), pays %s\n", known_n, cc);
}

/* /etc/wifi.bak puis /etc/wifi.txt */
static void cfg_save(void)
{
	char *p = cfgbuf;
	p = str_cat(p, "# Réseaux Wi-Fi connus (écrit par le service wifi ; docs/RESEAU.md 4.1)\n");
	if (cc[0]) {
		p = str_cat(p, "country=");
		p = str_cat(p, cc);
		p = str_cat(p, "\n");
	}
	for (int i = 0; i < known_n; i++) {
		p = str_cat(p, "ssid=\"");
		p = str_cat(p, known[i].ssid);
		p = str_cat(p, "\"\n");
		if (known[i].key[0]) {
			p = str_cat(p, "key=\"");
			p = str_cat(p, known[i].key);
			p = str_cat(p, "\"\n");
		}
		if (known[i].prio) {
			p = str_cat(p, "priority=");
			p = fmt_dec((u64)known[i].prio, p);
			p = str_cat(p, "\n");
		}
	}
	write_file("/etc/wifi.bak", cfgbuf, p - cfgbuf);
	write_file("/etc/wifi.txt", cfgbuf, p - cfgbuf);
}

/* ---- balayage */
static int do_scan(void)
{
	static struct scan_entry tmp[SCAN_ENT_MAX];
	int n = be_scan(tmp, SCAN_ENT_MAX);
	if (n < 0)
		return n;
	/* un réseau par nom : la box annonce le même SSID sur 2,4 et 5 GHz (et parfois plusieurs points d'accès) ;
	 * on garde l'entrée au meilleur signal (la connexion, be_join, balaye ce SSID et prend aussi le plus fort).
	 * Les réseaux cachés (SSID vide) restent séparés. */
	int m = 0;
	for (int i = 0; i < n; i++) {
		int j = 0;
		if (tmp[i].ssid_len)
			for (; j < m; j++)
				if (tmp[j].ssid_len == tmp[i].ssid_len && !memcmp(tmp[j].ssid, tmp[i].ssid, tmp[i].ssid_len))
					break;
		if (!tmp[i].ssid_len || j == m)
			tmp[m++] = tmp[i];
		else if (tmp[i].rssi > tmp[j].rssi)
			tmp[j] = tmp[i];
	}
	n = m;
	mtx_lock(&scan_lock);
	memcpy(scan_tab, tmp, sizeof tmp);
	scan_n = n;
	mtx_unlock(&scan_lock);
	return n;
}

/* -> nombre de réseaux connus vus au dernier balayage (seen = RSSI + 1000 du plus fort) */
static int mark_seen(void)
{
	int vus = 0;
	mtx_lock(&scan_lock);
	for (int i = 0; i < known_n; i++) {
		known[i].seen = 0;
		for (int k = 0; k < scan_n; k++) {
			const struct scan_entry *e = &scan_tab[k];
			if (e->ssid_len != strlen(known[i].ssid) || memcmp(e->ssid, known[i].ssid, e->ssid_len))
				continue;
			int s = e->rssi + 1000;
			if (!known[i].seen)
				vus++;
			if (s > known[i].seen)
				known[i].seen = s;
		}
	}
	mtx_unlock(&scan_lock);
	return vus;
}

static void mark_all(void)
{
	for (int i = 0; i < known_n; i++)
		known[i].seen = 1;
}

static struct known *best_seen(void)
{
	struct known *b = 0;
	for (int i = 0; i < known_n; i++) {
		struct known *k = &known[i];
		if (!k->seen)
			continue;
		if (!b || k->prio > b->prio || (k->prio == b->prio && k->seen > b->seen))
			b = k;
	}
	return b;
}

static void log_result(long r, const char *ssid)
{
	if (!r)
		return;
	ilog("wifi : connexion impossible, erreur %ld\n", -r);
	if (r == E_ACCES) {
		char t[96];
		char *p = str_cat(t, "Clé refusée pour « ");
		p = str_cat(p, ssid);
		str_cat(p, " »");
		notify_send("Wi-Fi", t, 0);
	}
}

static long try_known(struct known *k)
{
	char ssid[33], key[65];
	mtx_lock(&cfg_lock);
	copy_z(ssid, k->ssid, 33);
	copy_z(key, k->key, 65);
	mtx_unlock(&cfg_lock);
	ilog("wifi : connexion automatique à %s\n", ssid);
	long r = be_join(ssid, key);
	memset(key, 0, sizeof key);
	log_result(r, ssid);
	if (!r)
		copy_z(last_ssid, ssid, 33);
	return r;
}

/* le réseau connu préféré, sans balayage complet (be_join balaye lui-même ce SSID) */
static long quick_round(void)
{
	mark_all();
	struct known *k = best_seen();
	return k ? try_known(k) : 2;
}

/* -> 0 connecté, 1 aucun réseau connu visible, 2 essais sans succès */
static long round_once(int fresh)
{
	int n = fresh ? scan_n : do_scan();
	int vus = mark_seen();
	if (!vus) {
		if (n > 0)
			return 1;
		mark_all(); /* (balayage vide ou raté : on essaie tous les connus, réseaux cachés compris) */
	}
	for (;;) {
		struct known *k = best_seen();
		if (!k)
			return 2;
		k->seen = 0;
		if (user_join || auto_off)
			return 2;
		if (!try_known(k))
			return 0;
	}
}

static void conn_thread(void)
{
	long wait = 5000, next = now_ms() + 500;
	int first = 1, was_on = 0, said_none = 0;
	for (;;) {
		msleep(250);
		if (be_connected()) {
			was_on = 1;
			wait = 5000;
			continue;
		}
		if (was_on) {
			was_on = 0;
			ilog("wifi : lien perdu\n");
			msleep(2000);
			if (auto_off)
				continue;
			struct known *k = last_ssid[0] ? known_find(last_ssid) : 0;
			if (k && !try_known(k))
				continue;
		}
		int fresh = kick;
		kick = 0;
		if (fresh) { /* (BALAYER a vu un réseau connu : tout de suite, l'attente remise à 5 s) */
			next = 0;
			wait = 5000;
		}
		if (auto_off || user_join || !known_n || now_ms() < next)
			continue;
		long r = 2;
		if (first && !fresh) {
			first = 0;
			r = quick_round();
		}
		if (r)
			r = round_once(fresh);
		if (!r) {
			wait = 5000;
			continue;
		}
		if (r == 1 && !said_none) {
			said_none = 1;
			notify_send("Wi-Fi", "Aucun réseau connu", 0);
			ilog("wifi : aucun réseau connu visible\n");
		}
		next = now_ms() + wait;
		wait = wait * 2 > 60000 ? 60000 : wait * 2;
	}
}

/* ---- l'état sur le bus : korin.net.wifi « <état> <barres> <ssid> » (gardé), quand il change */
static long bp_canal;

static long bp_publish(const char *topic, const char *val)
{
	if (!bp_canal) {
		long c = korin_sys2(SYS_SVC_OPEN, "bus", 0);
		if (c < 0)
			return c;
		bp_canal = c + 1;
	}
	static u8 m[MSG_SIZE] __attribute__((aligned(16)));
	memset(m, 0, 48);
	*(u64 *)m = 1;                /* BUS_OP_PUBLISH */
	*(u64 *)(m + MSG_A) = 1;      /* BUS_RETAIN */
	u8 *d = m + MSG_DATA;
	while ((*d++ = (u8)*topic++))
		;
	u8 *v = d;
	while (*val && d - m < MSG_SIZE - 2)
		*d++ = (u8)*val++;
	*d = 0;
	*(u64 *)(m + MSG_A + 8) = (u64)(d - v);
	long r = korin_sys2(SYS_SVC_CALL, bp_canal - 1, m);
	if (r < 0 && r != (long)*(u64 *)m)
		bp_canal = 0;
	return r;
}

static int state(void)
{
	return be_state();
}

static void pub_thread(void)
{
	static char last[80];
	for (;;) {
		msleep(250);
		int st = state();
		int bars = 0;
		if (st == WS_CONNECTED) {
			int r = be_rssi();
			bars = r >= -55 ? 4 : r >= -65 ? 3 : r >= -75 ? 2 : 1;
		}
		static const char *const names[] = {"absent", "starting", "ready", "joining", "connected"};
		char val[80];
		char *p = str_cat(val, names[st >= 0 && st <= 4 ? st : 0]);
		*p++ = ' ';
		*p++ = (char)('0' + bars);
		*p = 0;
		if (st == WS_CONNECTED) {
			*p++ = ' ';
			copy_z(p, be_ssid(), 33);
		}
		if (!strcmp(val, last))
			continue;
		if (bp_publish("korin.net.wifi", val) >= 0)
			copy_z(last, val, sizeof last);
	}
}

/* ---- opérations */
static long op_status(u8 *m)
{
	u8 *d = m + MSG_DATA;
	memset(d, 0, 64);
	*(u32 *)(m + MSG_LEN) = 64;
	int st = state();
	put32(d, (u32)st);
	if (st < WS_READY)
		return 0;
	memcpy(d + 8, W.mac, 6);
	if (st == WS_CONNECTED) {
		put32(d + 4, (u32)be_rssi());
		const char *s = be_ssid();
		int n = 0;
		for (; s[n] && n < 32; n++)
			d[16 + n] = (u8)s[n];
		put32(d + 48, (u32)n);
	}
	return 0;
}

static long op_scan(u8 *m)
{
	if (state() < WS_READY)
		return E_NODEV;
	if (!*(u64 *)(m + MSG_A + 8)) { /* b = 0 : un vrai balayage */
		int r = do_scan();
		if (r < 0)
			return r;
		if (!be_connected() && !auto_off && !user_join) {
			mtx_lock(&cfg_lock);
			int vus = mark_seen();
			mtx_unlock(&cfg_lock);
			if (vus)
				kick = 1;
		}
	}
	mtx_lock(&scan_lock);
	u64 a = *(u64 *)(m + MSG_A);
	long n = scan_n;
	if (a < (u64)n) {
		long k = n - (long)a > PER_MSG ? PER_MSG : n - (long)a;
		memcpy(m + MSG_DATA, &scan_tab[a], (u64)k * sizeof scan_tab[0]);
		*(u32 *)(m + MSG_LEN) = (u32)(k * sizeof scan_tab[0]);
	}
	mtx_unlock(&scan_lock);
	*(u64 *)(m + MSG_A) = (u64)n;
	return n;
}

static long op_join(u8 *m)
{
	if (state() < WS_READY)
		return E_NODEV;
	char ssid[33], key[65];
	const char *s = (const char *)m + MSG_DATA;
	copy_z(ssid, s, 33);
	const char *k = s + strlen(s) + 1;
	if (k - (const char *)m > MSG_SIZE - 2)
		k = "";
	copy_z(key, k, 65);
	mtx_lock(&cfg_lock);
	struct known *kn = known_find(ssid);
	if (!key[0] && kn) { /* sans clé, un réseau connu : sa clé, et sa priorité gardée */
		copy_z(key, kn->key, 65);
		*(u64 *)(m + MSG_A + 8) = (u64)kn->prio;
	}
	mtx_unlock(&cfg_lock);
	user_join = 1;
	auto_off = 0;
	ilog("wifi : connexion à %s\n", ssid);
	long r = be_join(ssid, key);
	user_join = 0;
	log_result(r, ssid);
	if (!r) {
		copy_z(last_ssid, ssid, 33);
		if (*(u64 *)(m + MSG_A)) { /* mémoriser */
			mtx_lock(&cfg_lock);
			known_set(ssid, key, (int)*(u64 *)(m + MSG_A + 8));
			cfg_save();
			mtx_unlock(&cfg_lock);
		}
	}
	memset(key, 0, sizeof key);
	return r;
}

static long op_leave(u8 *m)
{
	(void)m;
	if (state() < WS_READY)
		return E_NODEV;
	auto_off = 1;
	be_leave();
	return 0;
}

static long op_forget(u8 *m)
{
	char ssid[33];
	copy_z(ssid, (const char *)m + MSG_DATA, 33);
	mtx_lock(&cfg_lock);
	struct known *k = known_find(ssid);
	long r = E_NOENT;
	if (k) {
		int i = (int)(k - known);
		memmove(&known[i], &known[i + 1], (u64)(known_n - i - 1) * sizeof known[0]);
		known_n--;
		cfg_save();
		r = 0;
	}
	mtx_unlock(&cfg_lock);
	return r;
}

static long op_known(u8 *m)
{
	u64 a = *(u64 *)(m + MSG_A);
	u8 *d = m + MSG_DATA;
	int sent = 0;
	mtx_lock(&cfg_lock);
	for (u64 i = a; i < (u64)known_n && sent < PER_MSG; i++, sent++) {
		u8 *e = d + sent * WK_ENT;
		memset(e, 0, WK_ENT);
		copy_z((char *)e, known[i].ssid, 33); /* jamais la clé */
		put32(e + 36, known[i].key[0] ? 3 : 0);
		put32(e + 40, (u32)known[i].prio);
	}
	long n = known_n;
	mtx_unlock(&cfg_lock);
	*(u32 *)(m + MSG_LEN) = (u32)(sent * WK_ENT);
	*(u64 *)(m + MSG_A) = (u64)n;
	return n;
}

static long op_priority(u8 *m)
{
	char ssid[33];
	copy_z(ssid, (const char *)m + MSG_DATA, 33);
	mtx_lock(&cfg_lock);
	struct known *k = known_find(ssid);
	long r = E_NOENT;
	if (k) {
		k->prio = (int)*(u64 *)(m + MSG_A);
		cfg_save();
		r = 0;
	}
	mtx_unlock(&cfg_lock);
	return r;
}

static long op_country(u8 *m)
{
	const char *v = (const char *)m + MSG_DATA;
	if (!v[0] || !v[1])
		return E_INVAL;
	set_cc(v);
	if (state() >= WS_READY)
		be_country(cc);
	return 0;
}

static void srv_thread(void)
{
	static u8 bufs[3][MSG_SIZE] __attribute__((aligned(16)));
	static int next_buf;
	u8 *m = bufs[__atomic_fetch_add(&next_buf, 1, __ATOMIC_SEQ_CST) % 3];
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, canal, m, 60000);
		if (req < 0) {
			msleep(100);
			continue;
		}
		if (!req)
			continue;
		u64 op = *(u64 *)m;
		if (op == SVC_OP_SIGNAL) { /* (pas de réponse) : arrêt demandé par init2 (extinction) */
			if (*(u64 *)(m + MSG_A) & 1)
				be_stop();
			continue;
		}
		*(u32 *)(m + MSG_LEN) = 0;
		long r;
		switch (op) {
		case OP_STATUS: r = op_status(m); break;
		case OP_SCAN: r = op_scan(m); break;
		case OP_JOIN: r = op_join(m); break;
		case OP_LEAVE: r = op_leave(m); break;
		case OP_FORGET: r = op_forget(m); break;
		case OP_KNOWN: r = op_known(m); break;
		case OP_PRIORITY: r = op_priority(m); break;
		case OP_COUNTRY: r = op_country(m); break;
		default: r = E_INVAL;
		}
		*(long *)m = r;
		korin_sys2(SYS_SVC_REPLY, req, m);
	}
}

static long spawn(void (*f)(void))
{
	long r = korin_sys1(SYS_SBRK, 65536 + 16);
	if (r < 0)
		return r;
	return korin_sys3(SYS_THREAD_CREATE, f, ((u64)r + 65536) & ~15UL, 0);
}

const char *wifi_cc(void)
{
	return cc;
}

/* le service : réseaux connus, canal « wifi », threads (3 de service, connexion, bus) */
int wifisvc_start(void)
{
	cfg_load();
	canal = korin_sys2(SYS_SVC_REGISTER, "wifi", 0);
	if (canal < 0) {
		ilog("wifi : service « wifi » déjà là (%ld) : un autre pilote tourne\n", canal);
		return -1;
	}
	for (int i = 0; i < 3; i++)
		spawn(srv_thread);
	spawn(pub_thread);
	return 0;
}

void wifisvc_auto(void)
{
	spawn(conn_thread);
}
