/* NetKit : le réseau (docs/KITS.md, docs/RESEAU.md 3), en C commun au Pi et au
 * PC ; remplace netkit.asm, même table (netkit.abi), mêmes résultats.
 *
 * Sockets, DNS, ping, état : requêtes au service « net » (netd) ; Wi-Fi :
 * service « wifi » (le pilote) ; OAuth 2.0 (oauth_*) : service « oauth »
 * (oauthd, docs/OAUTH.md), lancé au premier appel. Données de plus de 448 octets : par le tampon
 * du process (128 Ko, 16 zones de 8 Ko, donné par netd à la première
 * utilisation ; une zone par appel, plusieurs threads à la fois). Une attente
 * de plus de 25 s est découpée (netd rend E_AGAIN, on redemande tant que le
 * délai n'est pas écoulé).
 *
 * Les fonctions net_* et wifi_* gardent tous les registres sauf x0 sur le Pi,
 * comme les anciens appels système qu'elles remplacent : relais générés
 * (kit.conf : keep). */
#include "../common/korin_kit.h"

#include "appkit_import.h" /* (f_spawn : oauthd lancé au premier appel) */

/* src/inc/netd.inc */
#define NET_OP_OPEN 1
#define NET_OP_SOCKET 2
#define NET_OP_CONNECT 3
#define NET_OP_BIND 4
#define NET_OP_LISTEN 5
#define NET_OP_ACCEPT 6
#define NET_OP_SEND 7
#define NET_OP_RECV 8
#define NET_OP_CLOSE 9
#define NET_OP_SENDTO 10
#define NET_OP_RECVFROM 11
#define NET_OP_RESOLVE 12
#define NET_OP_PING 13
#define NET_OP_INFO 14
#define NET_OP_IFACES 15
#define NET_OP_TIME 16
#define NET_OP_NTP 17
#define NET_OP_STATS 23
#define NET_OP_SHUTDOWN 24
#define NET_OP_SOCKETS 25
#define NET_OP_BIND_ADDR 26
#define NET_OP_PEERNAME 27
#define NIF_ENT 64
#define NET_ZONE 8192
#define NET_ZONES 16
#define NET_INLINE 448
#define NSK_ENT 32
#define NSK_PER_MSG 14
#define NST_SIZE 152
#define WIFI_OP_STATUS 1
#define WIFI_OP_SCAN 2
#define WIFI_OP_JOIN 3
#define WIFI_OP_LEAVE 4
#define WIFI_OP_FORGET 5
#define WIFI_OP_KNOWN 6
#define WIFI_OP_PRIORITY 7
#define WIFI_OP_COUNTRY 8
#define WIFI_SCAN_ENT 48
#define WIFI_PER_MSG 9
/* src/inc/armpi.inc, netkit.inc */
#define BUF_VA_BASE 0x6800000000ULL
#define NI_DHCP 28

#define E_AGAIN -11
#define E_NOMEM -12
#define E_NODEV -19
#define E_PIPE -32
#define WAIT_SLICE 25000

static long nk_canal;  /* canal de netd + 1 (0 : à ouvrir) */
static long nk_wifi;   /* canal du service wifi + 1 */
static uint8_t *nk_buf; /* tampon du process (0 : pas encore ; oublié dans le fils d'un fork) */
static uint64_t nk_zones; /* zones prises (bits) */

/* ---- anneaux de réception partagés (docs/etude/ANNEAUX.md ; netd en C sur le PC, op 19) : netd verse les données
 * d'une socket TCP dans un anneau de notre tampon, recv y lit sans message et dort sur le futex de son en-tête */
#define NET_OP_RING 19
#define NK_SOCKS 64
#define NET_CLI_BUF 131072
#define NET_RING_SZ 65536
#define NET_RING_HDR NET_CLI_BUF
#define NET_RING_DATA (NET_CLI_BUF + 4096)
#define NRF_FIN 1
#define NRF_ERR 2
#define NRF_STALL 4
#define NRF_LIVE 8
#define E_CONNRESET -104
struct ring_hdr {
	volatile uint64_t head, tail;
	volatile uint32_t seq, flags;
	uint32_t sd;
	volatile uint32_t waiters; /* (compté avant de dormir : netd ne réveille que s'il y en a) */
	uint32_t pad[8];
};
static uint64_t nk_rings;        /* anneaux du tampon (0 : netd sans anneaux) */
static signed char nk_ring[NK_SOCKS]; /* anneau + 1 de chaque socket ; -1 : pas d'anneau (refusé, rendu) */
static unsigned char nk_tcp[NK_SOCKS];

typedef uint64_t msg_t[SVC_MSG / 8];

static long time_ms(void)
{
	return korin_sys0(SYS_TIME_MS);
}

/* ---- adresses */

/* valeur non signée en décimal -> fin (sur le 0 final), comme fmt_dec de lib.inc */
static char *nk_fmt_dec(unsigned long v, char *p)
{
	char tmp[24];
	int n = 0;
	do
		tmp[n++] = (char)('0' + v % 10);
	while (v /= 10);
	while (n)
		*p++ = tmp[--n];
	*p = 0;
	return p;
}

/* IPv4 (ordre hôte) -> « a.b.c.d », rend la fin (sur le 0) */
char *fmt_ip(unsigned long ip, char *buf)
{
	char *p = buf;
	for (int shift = 24; shift >= 0; shift -= 8) {
		p = nk_fmt_dec((ip >> shift) & 0xFF, p);
		if (shift) {
			*p++ = '.';
			*p = 0;
		}
	}
	return p;
}

long print_ip(unsigned long ip)
{
	char buf[32];
	char *end = fmt_ip(ip, buf);
	return korin_sys2(SYS_PRINT, buf, end - buf);
}

/* ---- liaison avec netd */

/* message -> résultat (le service « net » ouvert au premier appel, repris s'il a
 * été relancé) */
static long ncall(void *m)
{
	if (!nk_canal) {
		long c = korin_sys2(SYS_SVC_OPEN, "net", 0); /* sans attendre : pas de netd, pas de réseau */
		if (c < 0)
			return c;
		nk_canal = c + 1;
	}
	long r = korin_sys2(SYS_SVC_CALL, nk_canal - 1, m);
	if (r == E_PIPE) { /* netd s'est arrêté */
		nk_canal = 0;
		nk_buf = 0;
	}
	return r;
}

/* adresse du tampon du process, 0 si impossible */
static uint8_t *nbuf(void)
{
	if (nk_buf)
		return nk_buf;
	msg_t m = {NET_OP_OPEN};
	if (ncall(m) < 0)
		return 0;
	long a = korin_sys1(SYS_BUF_MAP, m[2]);
	if (a < 0)
		return 0;
	nk_rings = m[3] <= 16 ? m[3] : 0; /* (netd sans anneaux : 0, c n'est pas touché) */
	nk_buf = (uint8_t *)a;
	return nk_buf;
}

/* une zone libre du tampon (0..15) ; attend s'il n'y en a pas */
static int zone_take(void)
{
	for (;;) {
		uint64_t z = __atomic_load_n(&nk_zones, __ATOMIC_ACQUIRE);
		uint64_t free_bits = ~z & ((1ULL << NET_ZONES) - 1);
		if (!free_bits) {
			korin_sys1(SYS_SLEEP, 1);
			continue;
		}
		int n = __builtin_ctzll(free_bits);
		if (__atomic_compare_exchange_n(&nk_zones, &z, z | (1ULL << n), 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return n;
	}
}

static void zone_give(int n)
{
	__atomic_fetch_and(&nk_zones, ~(1ULL << n), __ATOMIC_RELEASE);
}

/* délai (ms) -> échéance (saturée) */
static unsigned long deadline_of(unsigned long ms)
{
	unsigned long now = (unsigned long)time_ms(), d = now + ms;
	return d < now ? ~0UL : d;
}

/* échéance -> attente pour une requête (25 s au plus, 0 si passée) */
static unsigned long slice_of(unsigned long deadline)
{
	unsigned long now = (unsigned long)time_ms();
	unsigned long left = deadline > now ? deadline - now : 0;
	return left < WAIT_SLICE ? left : WAIT_SLICE;
}

/* requête simple -> résultat */
static long simple(long op, long a, long b, long c, long d)
{
	msg_t m = {(uint64_t)op, (uint64_t)a, (uint64_t)b, (uint64_t)c, (uint64_t)d};
	return ncall(m);
}

/* ---- sockets */

long net_socket(long type)
{
	long sd = simple(NET_OP_SOCKET, type, 0, 0, 0);
	if (sd >= 0 && sd < NK_SOCKS) {
		nk_tcp[sd] = type == 1;
		nk_ring[sd] = 0;
	}
	return sd;
}

long net_connect(long sd, long ip, long port)
{
	return simple(NET_OP_CONNECT, sd, ip, port, 0);
}

long net_bind(long sd, long port)
{
	return simple(NET_OP_BIND, sd, port, 0, 0);
}

/* liée à une adresse (E68) : 127.x n'écoute que la boucle locale ; un vieux netd (E_INVAL) : le port seul */
long net_bind_addr(long sd, long port, long ip)
{
	return simple(NET_OP_BIND_ADDR, sd, port, ip, 0);
}

/* l'autre bout d'un socket connecté -> ip | port << 32 */
long net_peername(long sd)
{
	msg_t m = {NET_OP_PEERNAME, (uint64_t)sd};
	long r = ncall(m);
	return r < 0 ? r : (long)(m[2] | (m[3] << 32));
}

long net_listen(long sd, long backlog)
{
	if (sd >= 0 && sd < NK_SOCKS)
		nk_tcp[sd] = 0; /* (en écoute : pas d'anneau ; ses connexions acceptées en auront) */
	return simple(NET_OP_LISTEN, sd, backlog, 0, 0);
}

long net_close(long sd)
{
	if (sd >= 0 && sd < NK_SOCKS) {
		nk_ring[sd] = 0;
		nk_tcp[sd] = 0;
	}
	return simple(NET_OP_CLOSE, sd, 0, 0, 0);
}

long net_shutdown(long sd)
{
	return simple(NET_OP_SHUTDOWN, sd, 0, 0, 0);
}

long net_accept(long sd, unsigned long ms)
{
	unsigned long deadline = deadline_of(ms);
	for (;;) {
		long r = simple(NET_OP_ACCEPT, sd, (long)slice_of(deadline), 0, 0);
		if (r >= 0 && r < NK_SOCKS) {
			nk_tcp[r] = 1;
			nk_ring[r] = 0;
		}
		if (r != E_AGAIN || (unsigned long)time_ms() >= deadline)
			return r;
	}
}

long net_ping(long ip, long ms)
{
	return simple(NET_OP_PING, ip, ms, 0, 0);
}

/* envoi : dans le message, ou par une zone du tampon du process, en morceaux */
static long send_common(long sd, const uint8_t *buf, unsigned long len, uint64_t d, long op)
{
	msg_t m;
	if (len <= NET_INLINE) {
		m[0] = (uint64_t)op;
		m[1] = (uint64_t)sd;
		m[2] = (uint64_t)-1;
		m[3] = len;
		m[4] = d;
		*(uint32_t *)((uint8_t *)m + MSG_LEN) = (uint32_t)len;
		memcpy((uint8_t *)m + MSG_DATA, buf, len);
		return ncall(m);
	}
	uint8_t *b = nbuf();
	if (!b)
		return E_NOMEM;
	int zone = zone_take();
	uint8_t *z = b + ((unsigned long)zone << 13);
	unsigned long sent = 0;
	long r = 0;
	while (sent < len) {
		unsigned long chunk = len - sent < NET_ZONE ? len - sent : NET_ZONE;
		memcpy(z, buf + sent, chunk);
		m[0] = (uint64_t)op;
		m[1] = (uint64_t)sd;
		m[2] = (uint64_t)zone << 13; /* b = décalage */
		m[3] = chunk;                /* c = octets */
		m[4] = d;
		*(uint32_t *)((uint8_t *)m + MSG_LEN) = 0;
		r = ncall(m);
		if (r < 0)
			break;
		sent += (unsigned long)r;
		if ((unsigned long)r < chunk) /* envoi incomplet : délai */
			break;
	}
	zone_give(zone);
	if (r < 0 && !sent)
		return r;
	return (long)sent;
}

long net_send(long sd, const void *buf, long len)
{
	return send_common(sd, buf, (unsigned long)len, 0, NET_OP_SEND);
}

long net_sendto(long sd, const void *buf, long len, long ip, long port)
{
	uint64_t d = ((uint64_t)ip & 0xFFFFFFFF) | (((uint64_t)port & 0xFFFF) << 32);
	return send_common(sd, buf, (unsigned long)len, d, NET_OP_SENDTO);
}

static struct ring_hdr *ring_hdr(int slot)
{
	return (struct ring_hdr *)(nk_buf + NET_RING_HDR + 64 * (slot + 1));
}

/* l'anneau de cette socket (en le demandant à netd la première fois) -> numéro, -1 : pas d'anneau */
static int ring_of(long sd)
{
	if (sd < 0 || sd >= NK_SOCKS || !nk_tcp[sd] || nk_ring[sd] < 0)
		return -1;
	if (nk_ring[sd] > 0)
		return nk_ring[sd] - 1;
	if (!nbuf() || !nk_rings) {
		nk_ring[sd] = -1;
		return -1;
	}
	long r = simple(NET_OP_RING, sd, 0, 0, 0);
	if (r < 0 || (uint64_t)r >= nk_rings) {
		/* (pas encore connectée : on redemandera ; sinon : messages) */
		nk_ring[sd] = r == E_AGAIN ? 0 : -1;
		return -1;
	}
	nk_ring[sd] = (signed char)(r + 1);
	return (int)r;
}

#define RING_GONE 1L << 40
/* lecture dans l'anneau -> octets, 0 fin, E_AGAIN, E_CONNRESET ; RING_GONE : l'anneau a été rendu (messages) */
static long ring_recv(long sd, int slot, uint8_t *buf, unsigned long max, unsigned long ms)
{
	struct ring_hdr *h = ring_hdr(slot);
	uint8_t *d = nk_buf + NET_RING_DATA + (uint64_t)slot * NET_RING_SZ;
	unsigned long deadline = deadline_of(ms);
	for (;;) {
		uint32_t seq = __atomic_load_n(&h->seq, __ATOMIC_ACQUIRE);
		uint32_t fl = __atomic_load_n(&h->flags, __ATOMIC_ACQUIRE);
		if (!(fl & NRF_LIVE)) {
			nk_ring[sd] = -1;
			return RING_GONE;
		}
		uint64_t tail = h->tail, head = __atomic_load_n(&h->head, __ATOMIC_ACQUIRE);
		if (head != tail) {
			uint64_t k = head - tail < max ? head - tail : max;
			for (uint64_t done = 0; done < k;) {
				uint64_t ri = (tail + done) & (NET_RING_SZ - 1), n = k - done;
				if (n > NET_RING_SZ - ri)
					n = NET_RING_SZ - ri;
				memcpy(buf + done, d + ri, n);
				done += n;
			}
			__atomic_store_n(&h->tail, tail + k, __ATOMIC_RELEASE);
			if (__atomic_load_n(&h->flags, __ATOMIC_ACQUIRE) & NRF_STALL)
				simple(NET_OP_RING, sd, 1, 0, 0); /* (netd attendait de la place) */
			return (long)k;
		}
		if (fl & NRF_STALL) { /* (vide, mais netd en garde : il verse) */
			simple(NET_OP_RING, sd, 1, 0, 0);
			continue;
		}
		if (fl & NRF_ERR)
			return E_CONNRESET;
		if (fl & NRF_FIN)
			return 0;
		unsigned long now = (unsigned long)time_ms();
		if (now >= deadline)
			return E_AGAIN;
		unsigned long left = deadline - now;
		__atomic_fetch_add(&h->waiters, 1, __ATOMIC_SEQ_CST);
		if (__atomic_load_n(&h->seq, __ATOMIC_SEQ_CST) == seq) /* (rien de neuf depuis la lecture) */
			korin_sys3(SYS_FUTEX_WAIT, (long)&h->seq, (long)seq, (long)(left < 1000 ? left : 1000));
		__atomic_fetch_sub(&h->waiters, 1, __ATOMIC_SEQ_CST);
	}
}

/* réception : dans le message (448 octets au plus) ou par une zone (8 Ko au plus) */
static long recv_common(long sd, uint8_t *buf, unsigned long max, unsigned long ms, uint32_t *from, long op)
{
	unsigned long deadline = deadline_of(ms);
	int zone = -1;
	uint8_t *b = 0;
	if (max > NET_INLINE) {
		if (max > NET_ZONE)
			max = NET_ZONE;
		b = nbuf();
		if (!b)
			return E_NOMEM;
		zone = zone_take();
	}
	msg_t m;
	long r;
	for (;;) {
		m[0] = (uint64_t)op;
		m[1] = (uint64_t)sd;
		m[2] = zone < 0 ? (uint64_t)-1 : (uint64_t)zone << 13;
		m[3] = max;
		m[4] = slice_of(deadline);
		*(uint32_t *)((uint8_t *)m + MSG_LEN) = 0;
		r = ncall(m);
		if (r != E_AGAIN || (unsigned long)time_ms() >= deadline)
			break;
	}
	if (r > 0)
		memcpy(buf, zone < 0 ? (uint8_t *)m + MSG_DATA : b + ((unsigned long)zone << 13), (size_t)r);
	if (from) { /* source (recvfrom) : c = ip, d = port */
		from[0] = (uint32_t)m[3];
		from[1] = (uint32_t)m[4];
	}
	if (zone >= 0)
		zone_give(zone);
	return r;
}

long net_recv(long sd, void *buf, long max, long ms)
{
	int slot = max > 0 ? ring_of(sd) : -1;
	if (slot >= 0) {
		long r = ring_recv(sd, slot, buf, (unsigned long)max, (unsigned long)ms);
		if (r != RING_GONE)
			return r;
	}
	return recv_common(sd, buf, (unsigned long)max, (unsigned long)ms, 0, NET_OP_RECV);
}

long net_recvfrom(long sd, void *buf, long max, long ms, uint32_t *from)
{
	return recv_common(sd, buf, (unsigned long)max, (unsigned long)ms, from, NET_OP_RECVFROM);
}

/* attente sur plusieurs sockets (poll et select de libposix) : ents = n x {u32 sd, u32 événements}, rendus à la
 * place des demandés ; netd dort sur son futex jusqu'à un changement -> sockets prêts, 0 à l'échéance */
#define NET_OP_POLL 18
/* (net_poll) ce que les anneaux disent sur place : prêts (événements rendus), -1 : une entrée sans anneau */
static long ring_poll_local(uint32_t *e, long n, uint32_t *bell)
{
	long ready = 0;
	*bell = nk_buf ? __atomic_load_n((volatile uint32_t *)(nk_buf + NET_RING_HDR), __ATOMIC_ACQUIRE) : 0;
	for (long i = 0; i < n; i++) {
		long sd = e[i * 2];
		int slot = ring_of(sd);
		if (slot < 0)
			return -1;
		struct ring_hdr *h = ring_hdr(slot);
		uint32_t fl = __atomic_load_n(&h->flags, __ATOMIC_ACQUIRE);
		if (!(fl & NRF_LIVE))
			return -1;
		uint32_t got = 0;
		if (__atomic_load_n(&h->head, __ATOMIC_ACQUIRE) != h->tail || (fl & NRF_STALL))
			got |= 1;
		if (fl & NRF_FIN)
			got |= 1 | 16;
		if (fl & NRF_ERR)
			got |= 1 | 8 | 16;
		e[i * 2 + 1] = got & (e[i * 2 + 1] | 8 | 16);
		if (e[i * 2 + 1])
			ready++;
	}
	return ready;
}

long net_poll(void *ents, long n, long ms)
{
	if (n < 0 || n > NET_INLINE / 8)
		return -22;
	unsigned long deadline = ms < 0 ? ~0UL : deadline_of((unsigned long)ms);
	/* toutes les sockets ont un anneau : ici, sans message ; l'attente sur la sonnette du process */
	if (n > 0 && nk_rings) {
		uint32_t save[NET_INLINE / 4], bell;
		memcpy(save, ents, (size_t)n * 8);
		for (;;) {
			long r = ring_poll_local((uint32_t *)ents, n, &bell);
			if (r < 0) {
				memcpy(ents, save, (size_t)n * 8);
				break; /* (une socket sans anneau : netd, comme avant) */
			}
			if (r > 0)
				return r;
			unsigned long now = (unsigned long)time_ms();
			if (now >= deadline)
				return 0;
			memcpy(ents, save, (size_t)n * 8);
			unsigned long left = deadline - now;
			volatile uint32_t *b = (volatile uint32_t *)(nk_buf + NET_RING_HDR);
			__atomic_fetch_add(b + 1, 1, __ATOMIC_SEQ_CST);
			if (__atomic_load_n(b, __ATOMIC_SEQ_CST) == bell)
				korin_sys3(SYS_FUTEX_WAIT, (long)b, (long)bell, (long)(left < 1000 ? left : 1000));
			__atomic_fetch_sub(b + 1, 1, __ATOMIC_SEQ_CST);
		}
	}
	msg_t m;
	long r;
	for (;;) {
		m[0] = NET_OP_POLL;
		m[1] = (uint64_t)n;
		m[2] = slice_of(deadline);
		m[3] = m[4] = 0;
		*(uint32_t *)((uint8_t *)m + MSG_LEN) = (uint32_t)(n * 8);
		memcpy((uint8_t *)m + MSG_DATA, ents, (size_t)n * 8);
		r = ncall(m);
		if (r != 0 || (unsigned long)time_ms() >= deadline)
			break;
	}
	if (r >= 0)
		memcpy(ents, (uint8_t *)m + MSG_DATA, (size_t)n * 8);
	return r;
}

long net_resolve(const char *name)
{
	msg_t m = {NET_OP_RESOLVE};
	char *d = (char *)m + MSG_DATA;
	unsigned long n = 0;
	while (n < 255 && name[n]) {
		d[n] = name[n];
		n++;
	}
	d[n] = 0;
	*(uint32_t *)((uint8_t *)m + MSG_LEN) = (uint32_t)(n + 1);
	return ncall(m);
}

/* réponse de NET_OP_INFO : octets [off, off + len[ des données ; sans netd : zéros */
static long info_common(void *out, unsigned long off, unsigned long len)
{
	msg_t m = {NET_OP_INFO};
	*(uint32_t *)((uint8_t *)m + MSG_LEN) = 0;
	long r = ncall(m);
	if (r < 0) { /* pas de netd : rien, et l'erreur */
		memset(out, 0, len);
		return r;
	}
	memcpy(out, (uint8_t *)m + MSG_DATA + off, len);
	return 0;
}

long net_info(void *out)
{
	return info_common(out, 0, 32);
}

long net_active(void *out)
{
	return info_common(out, 32, 16);
}

/* attend une adresse DHCP (20 s au plus) -> 1 si obtenue */
long wait_dhcp(void *ni)
{
	for (int i = 0; i < 100; i++) {
		info_common(ni, 0, 32);
		if (*(uint32_t *)((uint8_t *)ni + NI_DHCP) == 4)
			return 1;
		korin_sys1(SYS_SLEEP, 200);
	}
	return 0;
}

long net_ifaces(void *out, unsigned long max)
{
	msg_t m = {NET_OP_IFACES};
	*(uint32_t *)((uint8_t *)m + MSG_LEN) = 0;
	long r = ncall(m);
	if (r < 0)
		return r;
	unsigned long n = (unsigned long)r < max ? (unsigned long)r : max;
	memcpy(out, (uint8_t *)m + MSG_DATA, n * NIF_ENT);
	return (long)n;
}

long net_stats(void *out)
{
	msg_t m = {NET_OP_STATS};
	*(uint32_t *)((uint8_t *)m + MSG_LEN) = 0;
	long r = ncall(m);
	if (r < 0)
		return r;
	memcpy(out, (uint8_t *)m + MSG_DATA, NST_SIZE);
	return 0;
}

/* les sockets ouverts de tout le système (netstat), une requête par 14 entrées */
long net_sockets(void *out, unsigned long max)
{
	unsigned long copied = 0;
	uint64_t slot = 0;
	while (copied < max) {
		msg_t m = {NET_OP_SOCKETS, slot};
		*(uint32_t *)((uint8_t *)m + MSG_LEN) = 0;
		long r = ncall(m);
		if (r < 0)
			return (long)copied ? (long)copied : r;
		slot = m[2];
		unsigned long n = (unsigned long)r < max - copied ? (unsigned long)r : max - copied;
		memcpy((uint8_t *)out + copied * NSK_ENT, (uint8_t *)m + MSG_DATA, n * NSK_ENT);
		copied += n;
		if (r != NSK_PER_MSG) /* moins qu'un plein message : la fin */
			break;
	}
	return (long)copied;
}

long net_time(uint64_t *out)
{
	msg_t m = {NET_OP_TIME, 0, 0, 0};
	long r = ncall(m);
	if (r < 0) {
		if (out)
			out[0] = out[1] = out[2] = 0;
		return 0;
	}
	if (out) {
		out[0] = m[1];
		out[1] = m[2];
		out[2] = m[3];
	}
	return (long)m[1];
}

long net_ntp_sync(long wait)
{
	return simple(NET_OP_NTP, wait, 0, 0, 0);
}

/* Dans le fils d'un fork, juste après : le tampon du process est celui du père
 * (netd l'a accordé à son pid) : démappé ici, un nouveau sera demandé au premier
 * besoin ; zones libérées. Les sockets restent (netd les donne aussi au fils). */
long net_fork_child(void)
{
	nk_zones = 0;
	nk_rings = 0; /* (les anneaux restent au père ; le fils passe par les messages, netd lit l'anneau pour lui) */
	for (int i = 0; i < NK_SOCKS; i++)
		if (nk_ring[i] > 0)
			nk_ring[i] = -1;
	uint8_t *b = nk_buf;
	nk_buf = 0;
	if (b)
		korin_sys1(SYS_BUF_FREE, ((uint64_t)b - BUF_VA_BASE) >> 26); /* (BUF_SLOT = 64 Mo) */
	return 0;
}

/* ---- Wi-Fi : service « wifi » (pilote). Sans lui : E_NODEV */

static long wcall(void *m)
{
	if (!nk_wifi) {
		long c = korin_sys2(SYS_SVC_OPEN, "wifi", 0);
		if (c < 0)
			return E_NODEV;
		nk_wifi = c + 1;
	}
	long r = korin_sys2(SYS_SVC_CALL, nk_wifi - 1, m);
	if (r == E_PIPE) { /* le pilote s'est arrêté */
		nk_wifi = 0;
		return E_NODEV;
	}
	return r;
}

/* op, a, b, chaîne 1 et chaîne 2 (0 : aucune) dans les données, séparées par leur 0 */
static long wsimple(uint64_t *m, long op, long a, long b, const char *s1, const char *s2)
{
	m[0] = (uint64_t)op;
	m[1] = (uint64_t)a;
	m[2] = (uint64_t)b;
	char *d = (char *)m + MSG_DATA, *end = d + 460;
	char *p = d;
	const char *ss[2] = {s1, s2};
	for (int k = 0; k < 2; k++) {
		const char *s = ss[k];
		if (!s)
			continue;
		for (;;) {
			char c = *s++;
			*p++ = c;
			if (!c)
				break;
			if (p >= end) {
				*p++ = 0;
				break;
			}
		}
	}
	*(uint32_t *)((uint8_t *)m + MSG_LEN) = (uint32_t)(p - d);
	return wcall(m);
}

long wifi_status(void *out)
{
	msg_t m;
	long r = wsimple(m, WIFI_OP_STATUS, 0, 0, 0, 0);
	if (r < 0) {
		memset(out, 0, 64); /* état 0 : absent */
		return r;
	}
	memcpy(out, (uint8_t *)m + MSG_DATA, 64);
	return 0;
}

/* pages de 9 entrées de 48 octets */
static long list_common(void *out, unsigned long max, long op)
{
	msg_t m;
	unsigned long copied = 0, total;
	do {
		long r = wsimple(m, op, (long)copied, copied != 0, 0, 0);
		if (r < 0)
			return copied ? (long)copied : r;
		total = (unsigned long)r < max ? (unsigned long)r : max;
		unsigned long n = total - copied;
		if (n > WIFI_PER_MSG)
			n = WIFI_PER_MSG;
		if (!n || total <= copied)
			break;
		memcpy((uint8_t *)out + copied * WIFI_SCAN_ENT, (uint8_t *)m + MSG_DATA, n * WIFI_SCAN_ENT);
		copied += n;
	} while (copied < total);
	return (long)copied;
}

long wifi_scan(void *out, long max)
{
	return list_common(out, (unsigned long)max, WIFI_OP_SCAN);
}

long wifi_known(void *out, long max)
{
	return list_common(out, (unsigned long)max, WIFI_OP_KNOWN);
}

long wifi_join_ex(const char *ssid, const char *key, long remember, long prio)
{
	msg_t m;
	return wsimple(m, WIFI_OP_JOIN, remember, prio, ssid, key ? key : "");
}

long wifi_join(const char *ssid, const char *key)
{
	return wifi_join_ex(ssid, key, 1, 0);
}

long wifi_leave(void)
{
	msg_t m;
	return wsimple(m, WIFI_OP_LEAVE, 0, 0, 0, 0);
}

long wifi_forget(const char *ssid)
{
	msg_t m;
	return wsimple(m, WIFI_OP_FORGET, 0, 0, ssid, 0);
}

long wifi_priority(const char *ssid, long prio)
{
	msg_t m;
	return wsimple(m, WIFI_OP_PRIORITY, prio, 0, ssid, 0);
}

long wifi_country(const char *country)
{
	msg_t m;
	return wsimple(m, WIFI_OP_COUNTRY, 0, 0, country, 0);
}

/* ---- OAuth 2.0 : service « oauth » (oauthd, docs/OAUTH.md), lancé au premier appel. Le compte (nom)
 * dans les données du message ; c, d : un tampon du programme (entrée ou sortie), que le service lit ou
 * écrit directement (SVC_MAP_CALLER). Résultats : >= 0, ou une erreur (oauth_error donne le texte). */

#define OA_SETUP 1
#define OA_STATUS 2
#define OA_DEVICE_BEGIN 3
#define OA_DEVICE_POLL 4
#define OA_BROWSER_BEGIN 5
#define OA_BROWSER_POLL 6
#define OA_BROWSER_FINISH 7
#define OA_TOKEN 8
#define OA_FORGET 9
#define OA_ERROR 10
#define OA_LIST 11
#define OA_REMOVE 12
#define OA_INFO 13
#define OA_NAME_MAX 64
#define E_INVAL -22

static long nk_oauth; /* canal du service oauth + 1 */
static const char oa_bin[] = "/sys/svc/oauthd";

/* le service d'oauthd d'un compte : « oauth.<uid> » (un oauthd par compte, ses propres jetons : décision du
 * 25/11/2026) ; l'uid 0 (et le Pi, sans uid) : « oauth » */
static void oa_name(char *o, long uid)
{
	const char *b = "oauth";
	while (*b)
		*o++ = *b++;
	if (uid > 0) {
		char t[16];
		int k = 0;
		*o++ = '.';
		do
			t[k++] = (char)('0' + uid % 10);
		while ((uid /= 10) && k < 15);
		while (k)
			*o++ = t[--k];
	}
	*o = 0;
}

static long ocall(void *m)
{
	for (int tries = 0; tries < 2; tries++) {
		if (!nk_oauth) {
			long me = korin_sys0(SYS_GETUID);
			if (me < 0)
				me = 0; /* (le Pi) */
			char name[32];
			oa_name(name, me);
			long c = korin_sys2(SYS_SVC_OPEN, name, 0);
			if (c < 0) { /* pas encore lancé : celui de son compte */
				f_spawn((long)oa_bin, 0);
				c = korin_sys2(SYS_SVC_OPEN, name, 4000);
				if (c < 0)
					return c;
			}
			nk_oauth = c + 1;
		}
		long r = korin_sys2(SYS_SVC_CALL, nk_oauth - 1, m);
		if (r != E_PIPE) /* (arrêté : relancé une fois) */
			return r < 0 ? r : (long)((uint64_t *)m)[0];
		nk_oauth = 0;
	}
	return E_PIPE;
}

static long oreq(long op, const char *name, long a, const void *buf, long len)
{
	msg_t m;
	memset(m, 0, sizeof m);
	m[0] = (uint64_t)op;
	m[1] = (uint64_t)a;
	m[3] = (uint64_t)buf;
	m[4] = (uint64_t)len;
	if (name) {
		size_t n = strlen(name);
		if (n >= OA_NAME_MAX)
			return E_INVAL;
		memcpy((uint8_t *)m + MSG_DATA, name, n);
		*(uint32_t *)((uint8_t *)m + MSG_LEN) = (uint32_t)(n + 1);
	}
	return ocall(m);
}

long oauth_setup(const char *name, const char *conf)
{
	return conf ? oreq(OA_SETUP, name, 0, conf, (long)strlen(conf)) : E_INVAL;
}

long oauth_status(const char *name)
{
	return oreq(OA_STATUS, name, 0, 0, 0);
}

long oauth_device_begin(const char *name, char *out, long max)
{
	return oreq(OA_DEVICE_BEGIN, name, 0, out, max);
}

long oauth_device_poll(const char *name)
{
	return oreq(OA_DEVICE_POLL, name, 0, 0, 0);
}

long oauth_browser_begin(const char *name, long port, char *out, long max)
{
	return oreq(OA_BROWSER_BEGIN, name, port, out, max);
}

long oauth_browser_poll(const char *name)
{
	return oreq(OA_BROWSER_POLL, name, 0, 0, 0);
}

long oauth_browser_finish(const char *name, const char *reply)
{
	return reply ? oreq(OA_BROWSER_FINISH, name, 0, reply, (long)strlen(reply)) : E_INVAL;
}

long oauth_token(const char *name, char *out, long max)
{
	return oreq(OA_TOKEN, name, 0, out, max);
}

long oauth_forget(const char *name)
{
	return oreq(OA_FORGET, name, 0, 0, 0);
}

long oauth_error(char *out, long max)
{
	return oreq(OA_ERROR, 0, 0, out, max);
}

long oauth_list(char *out, long max)
{
	return oreq(OA_LIST, 0, 0, out, max);
}

long oauth_remove(const char *name)
{
	return oreq(OA_REMOVE, name, 0, 0, 0);
}

long oauth_info(const char *name, char *out, long max)
{
	return oreq(OA_INFO, name, 0, out, max);
}
