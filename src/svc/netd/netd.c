/* netd - la pile réseau en service (docs/RESEAU.md), lancé par init2
 * (SD0:/etc/services.txt) ; traduction de netd.asm et de ses .inc (même
 * protocole : src/inc/netd.inc ; même anneau de trames avec les pilotes, qui
 * restent en asm sur le Pi : src/svc/inc/netif.inc ; mêmes délais, mêmes
 * messages, mêmes bizarreries : « comme l'asm »). Service « net » :
 *  * les pilotes d'interface (usbnet, genet, wifi) s'enregistrent (op 20) ; netd
 *    leur crée un anneau de trames partagé ; l'interface active est celle de plus
 *    haute priorité qui a un lien (GENET 30 > Wi-Fi 20 > USB 10) : elle porte
 *    l'adresse (DHCP) et la route ;
 *  * les programmes y viennent par NetKit (ops 1-25) ; les données de plus de
 *    448 octets passent par un tampon partagé de 128 Ko par process (op 1) ;
 *  * un socket a un ou plusieurs propriétaires (fork : ops.c) ;
 *  * NTP, fuseau, notifications : bg.c.
 * Threads : un thread « pile » (anneaux, minuteries), un thread auxiliaire
 * (bg.c) et WORKERS threads qui servent les requêtes ; une requête qui attend
 * le fait dans son thread, sans le verrou de la pile, 25 s au plus (E_AGAIN :
 * NetKit redemande).
 * Sur PC (x86-64), sans pilote réseau : la boucle locale seule (127.0.0.0/8). */
#include "netd.h"

long canal, canal_rx;
volatile u64 net_long;   /* 1 : la pile dort sans minuterie à suivre */
volatile u64 net_asleep; /* 1 : la pile dort (lo_put la réveille) */
static u8 lo_ring[LO_SLOTS << LO_SHIFT] __attribute__((aligned(4096)));
static volatile u64 lo_head, lo_tail;
volatile long if_active = -1; /* interface active (-1 : aucune) */
u64 if_changes;               /* compteur de changements */
static u8 *ring_free;         /* anneaux de sockets libres */
struct iface ifs[IF_MAX];
struct client clis[CLI_MAX];
u32 sk_owners[SKO_MAX][2]; /* descripteur + 1 (0 : libre), pid */
static u32 net_lock;

/* ---- machine */

u64 arch_counter(void)
{
#if defined(__aarch64__)
	u64 v;
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
	return v;
#else
	u32 lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return (u64)hi << 32 | lo;
#endif
}

/* fréquence du compteur des temps des pilotes (NST_FREQ) ; PC : pas de pilote, 0 */
u64 arch_counter_freq(void)
{
#if defined(__aarch64__)
	u64 v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
#else
	return 0;
#endif
}

/* ---- équivalents des services du noyau utilisés par la pile */

u64 time_ms(void)
{
	return (u64)korin_sys0(SYS_TIME_MS);
}

void thread_sleep(u64 ms)
{
	korin_sys1(SYS_SLEEP, ms);
}

/* pages -> adresse (alignée sur 4 Ko), 0 si échec. Les anneaux des sockets
 * (SK_RING_PAGES) libérés sont gardés pour resservir. (net_lock) */
u8 *pmm_alloc_n(u64 pages)
{
	if (pages == SK_RING_PAGES && ring_free) {
		u8 *p = ring_free;
		ring_free = *(u8 **)p;
		return p;
	}
	long r = korin_sys1(SYS_SBRK, (pages << 12) + 4095);
	if (r < 0)
		return 0;
	return (u8 *)(((u64)r + 4095) & ~4095UL);
}

void pmm_free_n(u8 *p, u64 pages)
{
	if (pages != SK_RING_PAGES)
		return;
	*(u8 **)p = ring_free;
	ring_free = p;
}

/* verrou de la pile : tourne, cède le cœur s'il est pris */
void nl_take(void)
{
	while (__atomic_exchange_n(&net_lock, 1, __ATOMIC_ACQUIRE))
		korin_sys0(SYS_YIELD);
}

void nl_drop(void)
{
	__atomic_store_n(&net_lock, 0, __ATOMIC_RELEASE);
}

/* verrou qui dort 1 ms tant qu'il est pris */
void mutex_lock(u32 *m)
{
	while (__atomic_exchange_n(m, 1, __ATOMIC_ACQUIRE))
		korin_sys1(SYS_SLEEP, 1);
}

void mutex_unlock(u32 *m)
{
	__atomic_store_n(m, 0, __ATOMIC_RELEASE);
}

/* ---- boucle locale */

/* trame vers soi-même, comme une trame reçue (sous net_lock) -> 1 ; 0 : file
 * pleine, trame perdue (TCP retransmet) */
int lo_put(const u8 *frame, u64 len)
{
	u64 h = lo_head;
	if (h - lo_tail >= LO_SLOTS)
		return 0;
	u8 *slot = lo_ring + ((h & (LO_SLOTS - 1)) << LO_SHIFT);
	*(u64 *)slot = len;
	memcpy(slot + 8, frame, (len + 15) & ~15UL); /* (par 16 octets, comme l'asm) */
	__atomic_store_n(&lo_head, h + 1, __ATOMIC_RELEASE);
	__atomic_thread_fence(__ATOMIC_SEQ_CST); /* (net_sleep : drapeau, puis file relue) */
	if (net_asleep) {
		net_asleep = 0;
		korin_sys2(SYS_SVC_SIGNAL, canal_rx, NR_SIG_RX);
	}
	return 1;
}

/* (sous net_lock) la file de la boucle locale -> trames traitées */
static u64 lo_drain(void)
{
	u64 n = 0;
	for (;;) {
		u64 t = lo_tail;
		if (lo_head == t || n >= 32)
			break;
		u8 *slot = lo_ring + ((t & (LO_SLOTS - 1)) << LO_SHIFT);
		net_input(slot + 8, *(u64 *)slot);
		lo_tail = t + 1;
		n++;
		net_stat(0);
		nl_drop(); /* (les requêtes passent) */
		nl_take();
	}
	return n;
}

/* -> 1 si un anneau a une trame */
static int net_rx_ready(void)
{
	if (lo_tail != __atomic_load_n(&lo_head, __ATOMIC_ACQUIRE))
		return 1;
	for (int i = 0; i < IF_MAX; i++) {
		u8 *r = ifs[i].ring;
		if (r && RING64(r, NR_RX_TAIL) != __atomic_load_n((u64 *)(r + NR_RX_HEAD), __ATOMIC_ACQUIRE))
			return 1;
	}
	return 0;
}

/* NR_SLEEP de chaque anneau */
static void net_flags(u64 v)
{
	for (int i = 0; i < IF_MAX; i++)
		if (ifs[i].ring)
			RING64(ifs[i].ring, NR_SLEEP) = v;
}

/* Dort jusqu'à une trame (pilote : netif_rx_put), une requête qui a pu armer une
 * minuterie (worker), ou la prochaine minuterie (net_wait_ms). Les drapeaux sont
 * posés avant de relire les anneaux et les sockets : ce qui arrive après
 * réveille forcément. */
static void net_sleep(void)
{
	u8 buf[MSG_SIZE] __attribute__((aligned(16)));
	net_flags(1);
	net_asleep = 1;
	net_long = 1;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	if (!net_rx_ready()) {
		nl_take();
		u64 w = net_wait_ms();
		nl_drop();
		if (w <= NET_TICK_MS) /* (une minuterie : pas besoin d'être réveillé par les requêtes) */
			net_long = 0;
		korin_sys3(SYS_SVC_NEXT, canal_rx, buf, w);
	}
	net_long = 0;
	net_asleep = 0;
	net_flags(0);
}

/* ---- thread de la pile : anneaux de réception, minuteries */

NORETURN static void stack_thread(void)
{
	for (;;) {
		nl_take();
		u64 n = lo_drain(); /* trames traitées dans ce tour */
		for (int i = 0; i < IF_MAX; i++) {
			struct iface *f = &ifs[i];
			u8 *r = f->ring;
			if (!r)
				continue;
			for (int k = 0; k < 32; k++) { /* au plus 32 trames par interface */
				u64 t = RING64(r, NR_RX_TAIL);
				if (__atomic_load_n((u64 *)(r + NR_RX_HEAD), __ATOMIC_ACQUIRE) == t)
					break;
				u8 *slot = r + NR_RX + ((t & (NR_SLOTS - 1)) << 11); /* emplacement (2 Ko) */
				u64 len = *(volatile u64 *)slot;
				if (if_active == i && len <= NR_FRAME_MAX) { /* interface inactive : trame jetée */
					net_st[6] += len; /* (octets reçus) */
					net_input(slot + 8, len);
					nl_drop(); /* (relâché un instant entre deux trames : les requêtes passent) */
					nl_take();
					if (f->ring != r) /* interface partie entre-temps ? */
						break;
				}
				net_stat(0); /* (trames traitées) */
				__atomic_store_n((u64 *)(r + NR_RX_TAIL), RING64(r, NR_RX_TAIL) + 1, __ATOMIC_RELEASE);
				n++;
			}
		}
		nl_drop();
		u64 now = time_ms(); /* minuteries toutes les 10 ms */
		if (now >= net_next_timer) {
			net_next_timer = now + 10;
			nl_take();
			dhcp_timer();
			tcp_timers();
			nl_drop();
		}
		now = time_ms(); /* sans pause pendant le trafic */
		if (n) {
			net_busy_t = now;
			continue;
		}
		if (now - net_busy_t < NET_BUSY_MS) {
			korin_sys0(SYS_YIELD);
			continue;
		}
		net_sleep();
	}
}

/* ---- interfaces */

/* trame Ethernet -> 1 si mise dans l'anneau d'émission de l'interface active (sous net_lock) */
int netif_send(const u8 *frame, u64 len)
{
	long a = if_active;
	if (a < 0)
		return 0;
	u8 *r = ifs[a].ring;
	if (!r || len > NR_FRAME_MAX)
		return 0;
	u64 h = RING64(r, NR_TX_HEAD);
	if (h - __atomic_load_n((u64 *)(r + NR_TX_TAIL), __ATOMIC_ACQUIRE) >= NR_SLOTS) {
		RING64(r, NR_TX_DROPS)++;
		return 0;
	}
	u8 *slot = r + NR_TX + ((h & (NR_SLOTS - 1)) << 11);
	net_st[7] += len; /* (octets émis) */
	*(u64 *)slot = len;
	/* copie par 16 octets (la trame de travail fait 4 Ko : on peut lire un peu au-delà) */
	memcpy(slot + 8, frame, (len + 15) & ~15UL);
	__atomic_store_n((u64 *)(r + NR_TX_HEAD), h + 1, __ATOMIC_RELEASE);
	if (ifs[a].wake) /* le pilote dort ? */
		korin_sys2(SYS_SVC_SIGNAL, ifs[a].wake - 1, NIF_SIG_TX);
	return 1;
}

/* (sous net_lock) choisit l'interface active : lien monté, plus haute priorité ;
 * si elle change, l'adresse est oubliée et le DHCP relancé */
void netif_choose(void)
{
	long best = -1, prio = -1;
	for (int i = 0; i < IF_MAX; i++) {
		struct iface *f = &ifs[i];
		if (!f->pid || !f->ring || !f->link)
			continue;
		if ((long)f->prio > prio) {
			prio = (long)f->prio;
			best = i;
		}
	}
	if (if_active == best)
		return;
	if_active = best;
	if (best < 0) {
		netdev_up = 0;
	} else {
		netdev_up = 1;
		netdev_mac = ifs[best].mac;
	}
	net_reset_ip();
	if_changes++; /* (le thread auxiliaire le voit) */
}

/* NET_OP_IF_REGISTER (a type, b MAC, c priorité, d canal + 1 du pilote ; données : nom)
 * -> interface ; b = numéro du tampon de l'anneau. (Tout pid est accepté : le noyau
 * ne donne pas les droits dans le message, comme l'asm.) */
static long op_if_register(u8 *m)
{
	u64 pid = W32(m, MSG_PID);
	long buf = korin_sys1(SYS_BUF_CREATE, NR_SIZE); /* l'anneau */
	if (buf < 0)
		return buf;
	long r = korin_sys1(SYS_BUF_MAP, buf);
	if (r < 0)
		goto free;
	u8 *ring = (u8 *)r;
	memset(ring, 0, 4096); /* en-tête à zéro */
	RING64(ring, NR_MTU) = 1500;
	RING64(ring, NR_WAKE) = (u64)canal_rx + 1;
	r = korin_sys2(SYS_BUF_GRANT, buf, pid);
	if (r < 0)
		goto free;
	nl_take();
	int k = -1; /* une place libre */
	for (int i = 0; i < IF_MAX; i++)
		if (!ifs[i].pid && k < 0)
			k = i;
	if (k < 0) {
		nl_drop();
		korin_sys1(SYS_BUF_FREE, buf);
		return E_NOMEM;
	}
	struct iface *f = &ifs[k];
	f->pid = pid;
	f->type = W64(m, MSG_A);
	f->mac = W64(m, MSG_B) & 0xFFFFFFFFFFFFUL;
	f->prio = W64(m, MSG_C);
	u64 name = W64(m, MSG_DATA) & 0x00FFFFFFFFFFFFFFUL; /* nom (7 caractères) */
	memcpy(f->name, &name, 8);
	f->buf = (u64)buf;
	f->wake = W64(m, MSG_D);
	f->ring = ring;
	f->link = 0;
	f->speed = 0;
	f->ssid[0] = 0;
	nl_drop();
	W64(m, MSG_B) = (u64)buf;
	ak_puts("netd : interface "); /* « netd : interface usb0 (pilote 23) » */
	ak_puts(f->name);
	ak_puts(" (pilote ");
	print_dec((long)pid);
	ak_puts(")\n");
	return k;
free:
	korin_sys1(SYS_BUF_FREE, buf);
	return r;
}

/* NET_OP_IF_STATE (a interface, b lien, c débit ; données : SSID) */
static long op_if_state(u8 *m)
{
	u64 i = W64(m, MSG_A);
	if (i >= IF_MAX)
		return E_INVAL;
	struct iface *f = &ifs[i];
	nl_take();
	if (f->pid != W32(m, MSG_PID)) { /* l'interface de ce pilote ? */
		nl_drop();
		return E_INVAL;
	}
	f->link = W64(m, MSG_B) != 0;
	f->speed = W64(m, MSG_C);
	u64 n = W32(m, MSG_LEN); /* SSID (32 o au plus) */
	if (n > 32)
		n = 32;
	char *d = f->ssid;
	for (const u8 *s = m + MSG_DATA; n && *s; n--)
		*d++ = (char)*s++;
	*d = 0;
	netif_choose();
	nl_drop();
	return 0;
}

/* pid terminé : ses interfaces disparaissent */
static void if_gone(u64 pid)
{
	for (int i = 0; i < IF_MAX; i++) {
		struct iface *f = &ifs[i];
		if (f->pid != pid)
			continue;
		nl_take();
		f->pid = 0;
		f->ring = 0;
		f->link = 0;
		netif_choose();
		nl_drop();
		ak_puts("netd : interface ");
		ak_puts(f->name);
		ak_puts(" partie\n");
		korin_sys1(SYS_BUF_FREE, f->buf); /* (le thread de la pile ne le lit plus) */
	}
}

/* ---- tampons des process (128 Ko : NetKit y met les données de plus de 448 o) */

static struct client *cli_find(u64 pid)
{
	for (int i = 0; i < CLI_MAX; i++)
		if (clis[i].pid == pid)
			return pid ? &clis[i] : 0;
	return 0;
}

/* NET_OP_OPEN -> 0 ; b = numéro du tampon (accordé au pid) */
static long op_open(u8 *m)
{
	u64 pid = W32(m, MSG_PID);
	struct client *c = cli_find(pid);
	if (c) {
		W64(m, MSG_B) = c->num;
		return 0;
	}
	long buf = korin_sys1(SYS_BUF_CREATE, NET_CLI_BUF);
	if (buf < 0)
		return buf;
	long r = korin_sys1(SYS_BUF_MAP, buf);
	if (r < 0)
		goto free;
	u8 *adr = (u8 *)r;
	r = korin_sys2(SYS_BUF_GRANT, buf, pid);
	if (r < 0)
		goto free;
	nl_take();
	/* deux threads du process à la fois : un seul tampon, sinon netd écrit dans
	 * l'un et le client lit l'autre */
	if ((c = cli_find(pid))) {
		u64 num = c->num;
		nl_drop();
		korin_sys1(SYS_BUF_FREE, buf);
		W64(m, MSG_B) = num;
		return 0;
	}
	for (int i = 0; i < CLI_MAX; i++) {
		if (clis[i].pid)
			continue;
		clis[i].pid = pid;
		clis[i].num = (u64)buf;
		clis[i].adr = adr;
		nl_drop();
		W64(m, MSG_B) = (u64)buf;
		return 0;
	}
	nl_drop();
	r = E_NOMEM;
free:
	korin_sys1(SYS_BUF_FREE, buf);
	return r;
}

/* pid terminé : son tampon est rendu */
static void cli_gone(u64 pid)
{
	if (!pid)
		return;
	nl_take();
	struct client *c = cli_find(pid);
	if (!c) {
		nl_drop();
		return;
	}
	c->pid = 0;
	u64 num = c->num;
	nl_drop();
	korin_sys1(SYS_BUF_FREE, num);
}

/* message, décalage (-1 : données du message), octets -> adresse des données
 * chez netd (0 si invalide) */
u8 *data_ptr(u8 *m, u64 off, u64 n)
{
	if (off == (u64)-1)
		return n > NET_INLINE ? 0 : m + MSG_DATA;
	if (off >= NET_CLI_BUF || n > NET_CLI_BUF || off + n > NET_CLI_BUF)
		return 0;
	u64 pid = W32(m, MSG_PID);
	for (int i = 0; i < CLI_MAX; i++)
		if (clis[i].pid == pid)
			return clis[i].adr + off;
	return 0;
}

/* ---- requêtes */

static long op_doorbell(u8 *m)
{
	(void)m;
	return 0;
}

/* 25 sockets(a premier emplacement) -> nombre d'entrées NSK_ENT (14 au plus),
 * b emplacement suivant (MAX_SOCK : fin) : la table des sockets (netstat) */
static long op_sockets(u8 *m)
{
	u8 *e = m + MSG_DATA;
	u64 n = 0, i = W64(m, MSG_A);
	nl_take();
	while (i < MAX_SOCK && n < NSK_PER_MSG) {
		struct sock *s = &sock_table[i++];
		if (!s->used)
			continue;
		W32(e, NSK_TYPE) = (u32)s->type;
		W32(e, NSK_STATE) = s->type == SOCK_TCP ? (u32)s->state : 0;
		W32(e, NSK_LPORT) = (u32)s->lport;
		W32(e, NSK_RPORT) = (u32)s->rport;
		W32(e, NSK_RIP) = s->rip;
		W32(e, NSK_OWNER) = (u32)s->owner;
		W32(e, NSK_RXQ) = (u32)(s->rxhead - s->rxtail);
		W32(e, NSK_TXQ) = (u32)(s->txhead - s->txtail);
		e += NSK_ENT;
		n++;
	}
	nl_drop();
	W64(m, MSG_B) = i;
	W32(m, MSG_LEN) = (u32)(n * NSK_ENT);
	return (long)n;
}

/* 23 stats -> données NST_SIZE : compteurs du pilote actif, puis de la pile
 * (sans interface : les 64 premiers octets restent ceux du message, comme l'asm) */
static long op_stats(u8 *m)
{
	u8 *d = m + MSG_DATA;
	W32(m, MSG_LEN) = NST_SIZE;
	long a = if_active;
	if (a >= 0 && ifs[a].ring) {
		u8 *r = ifs[a].ring;
		for (int i = 0; i < 8; i++)
			W64(d, i * 8) = RING64(r, NR_STATS + i * 8);
		W64(d, NST_RX_DROPS) = RING64(r, NR_RX_DROPS);
		W64(d, NST_TX_DROPS) = RING64(r, NR_TX_DROPS);
	}
	W64(d, NST_STACK) = net_st[0];
	W64(d, NST_STACK + 8) = net_st[1];
	W64(d, NST_OOO) = net_st[2];
	W64(d, NST_OOO + 8) = net_st[3];
	W64(d, NST_FREQ) = arch_counter_freq();
	W64(d, NST_FRTX) = net_st[4];
	W64(d, NST_FRTX + 8) = net_st[5];
	W64(d, NST_RXB) = net_st[6];
	W64(d, NST_RXB + 8) = net_st[7];
	return 0;
}

typedef long (*op_fn)(u8 *);
static const op_fn op_table[NET_OP_SOCKETS + 1] = {
	0, op_open, op_socket, op_connect, op_bind, op_listen, /* 0-5 */
	op_accept, op_send, op_recv, op_close, op_sendto, /* 6-10 */
	op_recv, /* 11 recvfrom */
	op_resolve, op_ping, op_info, op_ifaces, op_time, op_ntp, 0, 0, /* 12-19 */
	op_if_register, op_if_state, op_doorbell, op_stats, op_shutdown, op_sockets, /* 20-25 */
};

/* message -> 1 : répondre (résultat en +0) */
static int dispatch(u8 *m)
{
	W32(m, MSG_LEN) = 0;
	u64 op = W64(m, 0);
	if (op == SVC_OP_END) { /* un process s'arrête */
		if_gone(W64(m, MSG_A));
		net_close_all(W64(m, MSG_A));
		cli_gone(W64(m, MSG_A));
		return 0;
	}
	if (op == SVC_OP_BIRTH)
		return 0;
	if (op == SVC_OP_FORK) { /* fork : le fils tient les sockets du père */
		sko_fork(W64(m, MSG_A), W64(m, MSG_B));
		return 0;
	}
	long r = E_INVAL;
	if (op <= NET_OP_SOCKETS && op_table[op])
		r = op_table[op](m);
	W64(m, 0) = (u64)r;
	return 1;
}

NORETURN static void worker(void)
{
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, canal, m, 60000);
		if (req < 0) {
			korin_sys1(SYS_SLEEP, 100);
			continue;
		}
		if (!req)
			continue;
		int rep = dispatch(m);
		/* la pile dort longtemps : cette requête a pu armer une minuterie (net_sleep) */
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		if (net_long)
			korin_sys2(SYS_SVC_SIGNAL, canal_rx, NR_SIG_RX);
		if (rep) /* (avis : pas de réponse) */
			korin_sys2(SYS_SVC_REPLY, req, m);
	}
}

/* un thread avec sa pile (WSTACK) -> tid, < 0 si échec */
static long spawn_thread(void (*entry)(void))
{
	long r = korin_sys1(SYS_SBRK, WSTACK + 16);
	if (r < 0)
		return r;
	u64 top = ((u64)r + WSTACK) & ~15UL;
	return korin_sys3(SYS_THREAD_CREATE, entry, top, 0);
}

/* pile : trames de travail -> 1 si prêt */
static int stack_init(void)
{
	u8 *p = pmm_alloc_n(2); /* trame de travail RX + TX (seule TX sert, comme l'asm) */
	if (!p)
		return 0;
	net_txbuf = p + 4096;
	dhcp_state = DHCP_INIT;
	if_active = -1;
	return 1;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!stack_init())
		return 1;
	canal = korin_sys2(SYS_SVC_REGISTER, "net", SVF_ENDS | SVF_BIRTHS); /* (clients et pilotes qui s'arrêtent, fork) */
	if (canal < 0)
		return 1;
	canal_rx = korin_sys2(SYS_SVC_REGISTER, "net.rx", 0); /* le réveil de la pile (net_sleep) */
	if (canal_rx < 0)
		return 1;
	ak_puts("netd : en service\n");
	if (spawn_thread(stack_thread) < 0)
		return 1; /* (init2 relance) */
	spawn_thread(bg_thread);
	for (int i = 0; i < WORKERS - 1; i++)
		spawn_thread(worker);
	worker(); /* le thread principal sert aussi */
}
