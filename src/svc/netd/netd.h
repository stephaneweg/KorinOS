/* netd.h : ce que partagent les morceaux de netd en C (netd.c, stack.c, ops.c,
 * bg.c), traduction de netd.asm, stack.inc, ops.inc, background.inc,
 * stackdata.inc. Protocole du service « net » : src/inc/netd.inc (inchangé). */
#ifndef NETD_H
#define NETD_H

#include "korinrt.h"

/* ---- src/inc/netd.inc */
#define NET_OP_OPEN 1
#define NET_OP_SOCKETS 25
#define NIF_ETHERNET 1
#define NIF_WIFI 2
#define NIF_USB 3
#define NIF_E_NAME 0
#define NIF_E_TYPE 8
#define NIF_E_LINK 12
#define NIF_E_IP 16
#define NIF_E_PRIO 20
#define NIF_E_SPEED 24
#define NIF_E_ACTIVE 28
#define NIF_E_SSID 32
#define NIF_ENT 64
#define NET_CLI_BUF 131072
#define NET_INLINE 448
#define NR_RX_HEAD 0
#define NR_RX_TAIL 8
#define NR_TX_HEAD 16
#define NR_TX_TAIL 24
#define NR_MTU 40
#define NR_RX_DROPS 48
#define NR_TX_DROPS 56
#define NR_STATS 64
#define NR_SLEEP 2048
#define NR_WAKE 2056
#define NR_SIG_RX 1
#define NIF_SIG_TX 2
#define NR_SLOTS 64
#define NR_FRAME_MAX 1544
#define NR_RX 4096
#define NR_TX (4096 + NR_SLOTS * 2048)
#define NR_SIZE (4096 + 2 * NR_SLOTS * 2048)
#define NSK_TYPE 0
#define NSK_STATE 4
#define NSK_LPORT 8
#define NSK_RPORT 12
#define NSK_RIP 16
#define NSK_OWNER 20
#define NSK_RXQ 24
#define NSK_TXQ 28
#define NSK_ENT 32
#define NSK_PER_MSG 14
#define NST_RX_DROPS 96
#define NST_TX_DROPS 104
#define NST_STACK 64
#define NST_OOO 80
#define NST_FREQ 112
#define NST_FRTX 120
#define NST_RXB 136
#define NST_SIZE 152

/* ---- src/inc/armpi.inc, syscalls.inc */
#define SVF_ENDS 1
#define SVF_BIRTHS 2
#define SVC_OP_END 0xFFFF
#define SVC_OP_BIRTH 0xFFFE
#define SVC_OP_FORK 0xFFFC

/* ---- netd.asm */
#define WORKERS 24      /* threads de requêtes (dont le principal) */
#define WSTACK 32768    /* pile d'un thread */
#define WAIT_MAX 25000  /* attente d'une requête (svc_call : 30 s) */
#define IF_MAX 4
#define NET_BUSY_MS 5   /* ms sans trame avant de revenir au repos (50 : jamais au repos sur un Wi-Fi bavard) */
#define NET_TICK_MS 10  /* sommeil : une minuterie TCP en cours */
#define NET_DHCP_MS 100 /*   adresse pas encore obtenue */
#define NET_LONG_MS 1000 /*  rien à surveiller */
#define LO_SLOTS 256    /* boucle locale (lo_put), puissance de 2 */
#define LO_SHIFT 11     /* 2 Ko par trame : longueur (8 o), trame */
#define CLI_MAX 48      /* process clients ayant un tampon */
#define SKO_MAX 256     /* couples (socket, pid) : sk_owners */

#define E_FAULT -14
#define E_BUSY -16

/* ---- stack.inc */
#define ETH_HDR 14
#define IP_HDR 20
#define PAYLOAD (ETH_HDR + IP_HDR)
#define ETH_ARP 0x0806
#define ETH_IP 0x0800
#define PROTO_ICMP 1
#define PROTO_TCP 6
#define PROTO_UDP 17
#define ARP_ENTRIES 16
#define MAX_SOCK 64
/* tampons des sockets (puissance de 2) et décalage de la fenêtre annoncée (RFC 7323 : 65535 << décalage doit
 * couvrir l'anneau). Le PC : 1 Mo, la fenêtre de réception d'une connexion TCP limite son débit à anneau / aller-
 * retour (256 Ko et 60 ms en Wi-Fi jusqu'à Google : 4 Mo/s pour gdrivefs, pkg, Jet) ; alloués à l'ouverture d'une
 * socket et gardés pour resservir (64 sockets au plus). Le Pi garde 256 Ko. */
#ifdef __x86_64__
#define SK_RING 1048576
#define TCP_WS_SHIFT 5
#else
#define SK_RING 262144
#define TCP_WS_SHIFT 3
#endif
#define SK_RING_PAGES (SK_RING / 4096)
#define TCP_MSS 1460
#define TCP_RTO_MIN 1000
#define TCP_IW 10
#define TCP_SSTHRESH 0x100000
#define TCP_ACK_EVERY 8
#define TCP_ACK_MS 20
#define TCP_ACK_FULL 1000
#define TCP_DUPACK_MAX 3
#define TCP_PROBE_MS 60
#define TCP_PROBE_MAX 4
#define EPHEMERAL 49152
#define OOO_MAX 4

#define SKF_FIN_PENDING 0 /* l'application a fermé : FIN à envoyer */
#define SKF_FIN_SENT 1
#define SKF_RX_FIN 2      /* le pair a fermé */
#define SKF_RESET 3
#define SKF_TIMEOUT 4
#define SKF_REFUSED 5
#define SKF_ACCEPTED 6
#define SKF_ORPHAN 7
#define F(b) (1UL << (b))

#define TCP_CLOSED 0
#define TCP_LISTEN 1
#define TCP_SYN_SENT 2
#define TCP_SYN_RCVD 3
#define TCP_ESTAB 4
#define TCP_FIN_WAIT1 5
#define TCP_FIN_WAIT2 6
#define TCP_CLOSE_WAIT 7
#define TCP_CLOSING 8
#define TCP_LAST_ACK 9
#define TCP_TIME_WAIT 10

#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PSH 0x08
#define TH_ACK 0x10

#define E_TIMEDOUT -110
#define E_CONNREFUSED -111
#define E_CONNRESET -104
#define E_ADDRINUSE -98
#define E_NOTCONN -107
#define E_HOSTUNREACH -113

#define DHCP_IDLE 0
#define DHCP_INIT 1
#define DHCP_SELECTING 2
#define DHCP_REQUESTING 3
#define DHCP_BOUND 4

/* un socket (SK_SIZE = 512 octets dans l'asm ; ici le compilateur range) ;
 * numéros de séquence sur 32 bits, comme les registres w de l'asm */
struct sock {
	u64 used;
	u64 type;     /* 1 TCP, 2 UDP */
	u64 state;
	u64 owner;    /* pid propriétaire (0 : interne ou orphelin) */
	u64 lport;
	u32 rip;
	u64 rport;
	u8 *rxbuf;
	u64 rxhead, rxtail; /* compteurs monotones (index = compteur & masque) */
	u8 *txbuf;
	u64 txhead, txtail; /* txtail correspond à SND_UNA */
	u32 snd_una, snd_nxt;
	u64 snd_wnd;
	u32 rcv_nxt, iss;
	u64 mss, rto;
	u64 rtx_at;   /* échéance de retransmission (ms), 0 = désarmée */
	u64 retries;
	u64 srtt;
	u32 rtt_seq;  /* séquence chronométrée (0 : aucune) */
	u64 rtt_start;
	u64 flags;
	u32 fin_seq;
	struct sock *parent; /* socket en écoute (fils pas encore accepté) */
	u64 backlog;
	u64 deadline; /* fin de TIME_WAIT / délai d'un orphelin */
	u64 acknow;
	u64 ooo_n;    /* plages reçues hors ordre (déjà dans l'anneau) */
	u32 ooo[OOO_MAX][2];
	u64 cwnd, ssthresh, dupacks;
	u32 recover;  /* récupération : fin (SND_NXT au moment de la perte) */
	u64 inrec;
	u64 ack_cnt, ack_at;
	u64 rx_len;   /* (tcp_input) octets du segment en ordre */
	u64 ws_ok, snd_shift, rcv_shift;
	u64 probe_at, probe_n;
};

struct iface {
	u64 pid;   /* pilote (0 : place libre) */
	u64 type;
	u64 prio;
	u64 mac;
	char name[8];
	u64 buf;   /* numéro du tampon de l'anneau */
	u8 *ring;  /* son adresse chez netd (0 : aucun) */
	u64 link;
	u64 speed;
	char ssid[40];
	u64 wake;  /* canal + 1 du pilote (nif_wake, 0 : il sonde) */
};

struct client {
	u64 pid, num;
	u8 *adr;
};

/* lectures hors du verrou (attentes) : relues à chaque tour */
#define VOL(x) (*(volatile __typeof__(x) *)&(x))
#define RING64(r, off) (*(volatile u64 *)((r) + (off)))
#define W64(m, off) (*(u64 *)((u8 *)(m) + (off)))
#define W32(m, off) (*(u32 *)((u8 *)(m) + (off)))

/* ---- netd.c */
extern long canal, canal_rx;
extern volatile u64 net_long, net_asleep;
extern struct iface ifs[IF_MAX];
extern volatile long if_active;
extern u64 if_changes;
extern struct client clis[CLI_MAX];
extern u32 sk_owners[SKO_MAX][2];
void nl_take(void);
void nl_drop(void);
void mutex_lock(u32 *m);
void mutex_unlock(u32 *m);
u64 time_ms(void);
void thread_sleep(u64 ms);
void net_wake(void);
u32 net_gen_now(void);
void net_wait(u32 gen, u64 end);
u8 *pmm_alloc_n(u64 pages);
void pmm_free_n(u8 *p, u64 pages);
int lo_put(const u8 *frame, u64 len);
int netif_send(const u8 *frame, u64 len);
void netif_choose(void);
u8 *data_ptr(u8 *msg, u64 off, u64 n);
u64 arch_counter(void);
u64 arch_counter_freq(void);

/* ---- stack.c (stackdata.inc) */
extern u64 netdev_mac;
extern u64 net_next_timer;
extern u64 net_st[8]; /* trames, expirations, hors ordre, sans place, rapides, sondes, octets reçus, émis */
extern u64 net_busy_t;
extern u8 *net_txbuf;
extern struct sock sock_table[MAX_SOCK];
extern volatile u64 ping_reply_ms;
extern u64 dhcp_next_ms, dhcp_tries;
extern volatile u32 net_ip;
extern u32 net_mask, net_gw, net_dns, net_ip_id, net_next_port;
extern volatile u32 dhcp_state;
extern u32 dhcp_xid, dhcp_offer_ip, dhcp_server, ping_id, ping_seq, arp_next;
extern volatile u32 netdev_up;
extern u32 ping_mutex;
#define net_stat(n) (net_st[n]++)

void net_reset_ip(void);
void net_input(u8 *frame, u64 len);
int arp_resolve_wait(u32 dst);
int ip_output(u32 dst, u32 proto, u64 plen);
int udp_output(u32 sport, u32 dst, u32 dport, const u8 *data, u64 n);
int ip_is_local(u32 a);
u64 net_wait_ms(void);
void dhcp_timer(void);
int tcp_timers(void); /* -> 1 : un socket abandonné ou rendu */
struct sock *sock_alloc(void);
int sk_alloc_rings(struct sock *s, int tx);
void sock_free(struct sock *s);
u32 sock_ephemeral(void);
void sk_rx_read(struct sock *s, u8 *dst, u64 n);
u64 sk_rx_free(struct sock *s);
int tcp_send(struct sock *s, u32 flags, u32 seq, u64 n);
void tcp_send_ack(struct sock *s);
u32 tcp_iss(u32 seed);
void tcp_arm_rtx(struct sock *s);
void tcp_output(struct sock *s);
void sock_close_locked(struct sock *s);
long udp_sendto_user(const u8 *data, u64 n, u32 ip, u32 port, struct sock *s);
long dns_resolve(const char *name);
u16 csum_fold(u64 acc);
u64 csum_add(const u8 *p, u64 n, u64 acc);
static inline void st16be(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}
static inline void st32be(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}
static inline u32 ld16be(const u8 *p)
{
	return (u32)p[0] << 8 | p[1];
}
static inline u32 ld32be(const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static inline long sk_index(struct sock *s)
{
	return s - sock_table;
}

/* ---- ops.c */
long op_socket(u8 *m);
long op_connect(u8 *m);
long op_bind(u8 *m);
long op_listen(u8 *m);
long op_accept(u8 *m);
long op_send(u8 *m);
long op_recv(u8 *m);
long op_close(u8 *m);
long op_sendto(u8 *m);
long op_resolve(u8 *m);
long op_ping(u8 *m);
long op_info(u8 *m);
long op_ifaces(u8 *m);
long op_shutdown(u8 *m);
long op_poll(u8 *m);
void sko_fork(u64 child, u64 parent);
void net_close_all(u64 pid);

/* ---- bg.c */
void bg_thread(void);
long op_time(u8 *m);
long op_ntp(u8 *m);

#endif
