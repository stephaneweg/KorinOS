/* netif/netif.c - côté pilote d'une interface réseau (voir netif.h), repris pas à
 * pas de src/svc/inc/netif.inc. */
#include "netif.h"

#define NET_OP_IF_REGISTER 20
#define NET_OP_IF_STATE 21
/* l'anneau de trames (créé par netd, src/inc/netd.inc) */
#define NR_RX_HEAD 0
#define NR_RX_TAIL 8
#define NR_TX_HEAD 16
#define NR_TX_TAIL 24
#define NR_RX_DROPS 48
#define NR_SLEEP 2048
#define NR_WAKE 2056
#define NR_SIG_RX 1
#define NR_SLOTS 64
#define NR_SLOT 2048
#define NR_RX 4096
#define NR_TX (4096 + NR_SLOTS * NR_SLOT)

long nif_wake;
static u64 nif_type, nif_mac, nif_prio;
static const char *nif_name;
static long nif_chan, nif_index;
static u8 *nif_ring; /* mappé (0 : pas inscrit) */
static u64 nif_next; /* prochain rappel de l'état (ms) */
static u64 nif_link_up, nif_speed;
static char nif_ssid[40];
static u64 nif_msg[64] __attribute__((aligned(16)));

#define RING64(off) (*(volatile u64 *)(nif_ring + (off)))

/* attente (ms) : (ré)inscription : canal de netd, anneau -> 0 */
static long nif_register_wait(long wait)
{
	nif_ring = 0;
	long ch = korin_sys2(SYS_SVC_OPEN, "net", wait);
	if (ch < 0)
		return ch;
	nif_chan = ch;
	u8 *m = (u8 *)nif_msg;
	nif_msg[0] = NET_OP_IF_REGISTER;
	nif_msg[1] = nif_type;
	nif_msg[2] = nif_mac;
	nif_msg[3] = nif_prio;
	nif_msg[4] = (u64)nif_wake;
	memset(m + MSG_DATA, 0, 8); /* nom (7 caractères au plus) */
	for (int i = 0; i < 7 && nif_name[i]; i++)
		m[MSG_DATA + i] = (u8)nif_name[i];
	*(u32 *)(m + MSG_LEN) = 8;
	long r = korin_sys2(SYS_SVC_CALL, nif_chan, nif_msg);
	if (r < 0)
		return r;
	nif_index = r;
	r = korin_sys1(SYS_BUF_MAP, nif_msg[2]); /* l'anneau */
	if (r < 0)
		return r;
	nif_ring = (u8 *)r;
	nif_next = 0; /* état à redire tout de suite */
	return 0;
}

long netif_register(u64 type, u64 mac, u64 prio, const char *name)
{
	nif_type = type;
	nif_mac = mac;
	nif_prio = prio;
	nif_name = name;
	return nif_register_wait(60000); /* (netd peut démarrer après le pilote) */
}

int netif_rx_put(const u8 *frame, u64 len)
{
	if (!nif_ring || len > NR_FRAME_MAX || len < 14)
		return 0;
	u64 head = RING64(NR_RX_HEAD);
	u64 tail = __atomic_load_n((u64 *)(nif_ring + NR_RX_TAIL), __ATOMIC_ACQUIRE);
	if (head - tail >= NR_SLOTS) {
		RING64(NR_RX_DROPS) = RING64(NR_RX_DROPS) + 1;
		return 0;
	}
	u8 *slot = nif_ring + NR_RX + (head & (NR_SLOTS - 1)) * NR_SLOT;
	*(u64 *)slot = len;
	memcpy(slot + 8, frame, len);
	__atomic_store_n((u64 *)(nif_ring + NR_RX_HEAD), head + 1, __ATOMIC_RELEASE);
	__atomic_thread_fence(__ATOMIC_SEQ_CST); /* (netd : drapeau, puis anneau relu) */
	if (RING64(NR_SLEEP)) {
		RING64(NR_SLEEP) = 0;
		u64 wake = RING64(NR_WAKE);
		if (wake)
			korin_sys2(SYS_SVC_SIGNAL, wake - 1, NR_SIG_RX);
	}
	return 1;
}

u64 netif_tx_pending(void)
{
	if (!nif_ring)
		return 0;
	return __atomic_load_n((u64 *)(nif_ring + NR_TX_HEAD), __ATOMIC_ACQUIRE) - RING64(NR_TX_TAIL);
}

u64 netif_tx_get(u8 *out)
{
	if (!nif_ring)
		return 0;
	u64 tail = RING64(NR_TX_TAIL);
	if (tail == __atomic_load_n((u64 *)(nif_ring + NR_TX_HEAD), __ATOMIC_ACQUIRE))
		return 0;
	const u8 *slot = nif_ring + NR_TX + (tail & (NR_SLOTS - 1)) * NR_SLOT;
	u64 len = *(const u64 *)slot;
	if (len > NR_FRAME_MAX || !len)
		len = 0; /* (trame impossible : sautée) */
	else
		memcpy(out, slot + 8, len);
	__atomic_store_n((u64 *)(nif_ring + NR_TX_TAIL), tail + 1, __ATOMIC_RELEASE);
	return len;
}

/* état à netd -> résultat (< 0 : netd parti ou interface inconnue) */
static long nif_state(void)
{
	if (!nif_ring)
		return -1;
	u8 *m = (u8 *)nif_msg;
	nif_msg[0] = NET_OP_IF_STATE;
	nif_msg[1] = (u64)nif_index;
	nif_msg[2] = nif_link_up;
	nif_msg[3] = nif_speed;
	u32 n = 0;
	for (;;) { /* le SSID et son 0 (33 octets au plus) */
		u8 c = (u8)nif_ssid[n];
		m[MSG_DATA + n] = c;
		n++;
		if (!c || n >= 33)
			break;
	}
	*(u32 *)(m + MSG_LEN) = n;
	return korin_sys2(SYS_SVC_CALL, nif_chan, nif_msg);
}

void netif_link(u64 link, u64 speed, const char *ssid)
{
	nif_link_up = link;
	nif_speed = speed;
	nif_ssid[0] = 0;
	if (ssid) {
		int i = 0;
		for (; i < 32; i++)
			if (!(nif_ssid[i] = ssid[i]))
				break;
		if (i == 32)
			nif_ssid[32] = 0;
	}
	nif_state();
}

void netif_tick(void)
{
	u64 now = (u64)korin_sys0(SYS_TIME_MS);
	if (now < nif_next)
		return;
	nif_next = now + 2000;
	if (nif_state() >= 0)
		return;
	if (nif_register_wait(0) < 0) /* netd relancé : l'anneau a changé */
		return;
	nif_state();
}
