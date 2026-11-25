/* stack.c - pile TCP/IP de netd (docs/RESEAU.md) : traduction de stack.inc et
 * stackdata.inc (reprise de src/kernel/net.asm). Mêmes algorithmes, mêmes
 * constantes, mêmes bizarreries (« comme l'asm »).
 *
 * D'après les RFC : 826 (ARP), 791 (IPv4), 792 (ICMP), 768 (UDP), 2131/2132
 * (DHCP), 1035 (DNS), 793/9293 (TCP), 6298 (RTO, simplifié).
 *  * Thread « pile » de netd : vide les anneaux des interfaces, traite les trames,
 *    fait tourner les minuteries (DHCP, retransmissions TCP) toutes les 10 ms.
 *  * Tout l'état est protégé par net_lock. Les requêtes bloquantes attendent
 *    SANS le verrou.
 *  * Adresses IPv4 et ports en ordre « hôte » dans les variables et l'API ;
 *    ordre réseau seulement dans les trames.
 * Limites : pas de fragmentation IP. TCP : NewReno (RFC 5681, 6582), accusés
 * retardés, options MSS et window scale, sondes de fin de rafale. */
#include "netd.h"

/* ---- données de la pile (stackdata.inc) */
u64 netdev_mac; /* MAC de la carte active */
u64 net_next_timer;
u64 net_st[8];
u64 net_busy_t;
u8 *net_txbuf;
struct sock sock_table[MAX_SOCK];
volatile u64 ping_reply_ms;
u64 dhcp_next_ms, dhcp_tries; /* DISCOVER envoyés depuis le lien */
volatile u32 net_ip;
u32 net_mask, net_gw, net_dns, net_ip_id, net_next_port;
volatile u32 dhcp_state;
u32 dhcp_xid, dhcp_offer_ip, dhcp_server, ping_id, ping_seq, arp_next;
volatile u32 netdev_up; /* 1 : une carte est prête */
u32 ping_mutex;
static u8 arp_table[ARP_ENTRIES][16]; /* ip (4), mac (6), valide (2) */
static u8 net_arpbuf[64];
static u8 net_mcmac[8];

/* ---- sommes de contrôle */

u64 csum_add(const u8 *p, u64 n, u64 acc)
{
	while (n >= 2) {
		acc += (u64)p[0] << 8 | p[1];
		p += 2;
		n -= 2;
	}
	if (n)
		acc += (u64)p[0] << 8;
	return acc;
}

/* accumulateur -> complément à un replié sur 16 bits */
u16 csum_fold(u64 a)
{
	while (a >> 16)
		a = (a & 0xFFFF) + (a >> 16);
	return (u16)~a;
}

/* accumulateur du pseudo-en-tête TCP/UDP */
static u64 csum_pseudo(u32 src, u32 dst, u32 proto, u32 len)
{
	return (u64)(src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + proto + len;
}

/* adresse IPv4 -> console « a.b.c.d » */
static void kput_ip(u32 a)
{
	for (int sh = 24;; sh -= 8) {
		print_dec((a >> sh) & 0xFF);
		if (!sh)
			break;
		ak_puts(".");
	}
}

void net_reset_ip(void)
{
	net_ip = 0;
	net_gw = 0;
	dhcp_state = DHCP_INIT;
	dhcp_next_ms = 0;
	dhcp_tries = 0;
	memset(arp_table, 0, sizeof arp_table); /* les voisins ont pu changer */
}

/* ---- ARP */

/* IP, MAC (6 octets) : ajoute / met à jour le cache ARP */
static void arp_update(u32 ip, const u8 *mac)
{
	if (!ip)
		return;
	u32 i;
	for (i = 0; i < ARP_ENTRIES; i++)
		if (*(u32 *)arp_table[i] == ip)
			break;
	if (i == ARP_ENTRIES) { /* nouvelle entrée (remplacement circulaire) */
		i = arp_next;
		arp_next = (i + 1) & (ARP_ENTRIES - 1);
	}
	u8 *e = arp_table[i];
	*(u32 *)e = ip;
	memcpy(e + 4, mac, 6);
	*(u16 *)(e + 10) = 1;
}

/* IP -> MAC (dans le cache), 0 si inconnue */
static const u8 *arp_lookup(u32 ip)
{
	if ((ip >> 28) == 0xE) { /* multidiffusion (mDNS) : la MAC vient de l'adresse */
		net_mcmac[0] = 0x01;
		net_mcmac[1] = 0;
		net_mcmac[2] = 0x5E;
		net_mcmac[3] = (ip >> 16) & 0x7F;
		net_mcmac[4] = (u8)(ip >> 8);
		net_mcmac[5] = (u8)ip;
		return net_mcmac;
	}
	for (int i = 0; i < ARP_ENTRIES; i++)
		if (*(u32 *)arp_table[i] == ip && *(u16 *)(arp_table[i] + 10))
			return arp_table[i] + 4;
	return 0;
}

static void arp_header(u8 *b, u32 op)
{
	st16be(b + 12, ETH_ARP);
	st16be(b + 14, 1);
	st16be(b + 16, ETH_IP);
	st16be(b + 18, 0x0604); /* hlen 6, plen 4 */
	st16be(b + 20, op);
	memcpy(b + 22, &netdev_mac, 6);
	st32be(b + 28, net_ip);
}

/* les demandes ARP récentes : une réponse n'est prise que pour une adresse demandée (R4, E67) */
#define ARP_PENDING 8
static u32 arp_pend[ARP_PENDING];
static u32 arp_pend_next;

static int arp_known(u32 ip)
{
	for (int i = 0; i < ARP_ENTRIES; i++)
		if (*(u32 *)arp_table[i] == ip && *(u16 *)(arp_table[i] + 10))
			return 1;
	return 0;
}

/* IP -> envoie une demande ARP */
static void arp_request(u32 ip)
{
	arp_pend[arp_pend_next++ & (ARP_PENDING - 1)] = ip;
	u8 *b = net_arpbuf;
	memset(b, 0, 64);
	memset(b, 0xFF, 6); /* diffusion */
	memcpy(b + 6, &netdev_mac, 6);
	arp_header(b, 1); /* demande */
	st32be(b + 38, ip);
	netif_send(b, 60);
}

static void arp_input(u8 *f, u64 len)
{
	if (len < ETH_HDR + 28)
		return;
	u8 *a = f + ETH_HDR;
	if (ld16be(a) != 1 || ld16be(a + 2) != ETH_IP) /* matériel Ethernet, protocole IPv4 */
		return;
	/* ce qu'on apprend (R4, E67) : la réponse à une de nos demandes ; la demande d'un voisin qui nous vise
	 * (il va nous parler : sa première trame trouve sa MAC) ; une annonce (ARP gratuit) seulement pour une
	 * entrée connue ; jamais un changement de la passerelle tant que le bail tient */
	u32 sip = ld32be(a + 14), tip = ld32be(a + 24), op = ld16be(a + 6);
	int pending = 0;
	for (int i = 0; i < ARP_PENDING; i++)
		if (arp_pend[i] == sip && sip) {
			pending = 1;
			arp_pend[i] = 0;
		}
	int learn = op == 2 ? pending || arp_known(sip) : (net_ip && tip == net_ip) || arp_known(sip);
	if (learn && sip == net_gw && arp_known(sip) && !pending)
		learn = 0; /* (la passerelle : figée, sauf réponse à notre demande) */
	if (sip == net_ip || (sip >> 24) == 127)
		learn = 0;
	if (learn)
		arp_update(sip, a + 8); /* IP émetteur -> cache */
	if (op != 1) /* réponse : déjà prise en compte */
		return;
	if (!net_ip || tip != net_ip) /* demande pour nous ? */
		return;
	/* réponse : émetteur = nous, cible = le demandeur (les octets 42-59 restent
	 * ceux de la trame précédente, comme l'asm) */
	u8 *b = net_arpbuf;
	memcpy(b, a + 8, 6);
	memcpy(b + 6, &netdev_mac, 6);
	arp_header(b, 2);
	memcpy(b + 32, a + 8, 10); /* MAC + IP du demandeur */
	netif_send(b, 60);
}

/* destination -> prochain saut (destination ou passerelle) */
static u32 ip_next_hop(u32 dst)
{
	if ((dst >> 28) == 0xE) /* (multidiffusion : directement sur le lien) */
		return dst;
	if (!((dst ^ net_ip) & net_mask))
		return dst;
	return net_gw;
}

/* 1 si c'est nous : 127.0.0.0/8, ou notre adresse */
int ip_is_local(u32 a)
{
	if ((a >> 24) == 127)
		return 1;
	u32 ip = net_ip;
	return ip && a == ip;
}

/* destination -> notre adresse source pour elle : en boucle locale, sans
 * interface, net_ip peut être nulle */
static u32 ip_src(u32 dst)
{
	return (dst >> 24) == 127 ? dst : net_ip;
}

/* résout l'adresse MAC du prochain saut, en attendant jusqu'à 1 s (SANS
 * net_lock) -> 1 si connue */
int arp_resolve_wait(u32 dst)
{
	if (dst == 0xFFFFFFFF) /* diffusion */
		return 1;
	if (ip_is_local(dst)) /* nous : la boucle locale */
		return 1;
	u32 hop = ip_next_hop(dst);
	u64 end = time_ms() + 1000, next = 0; /* prochaine demande */
	for (;;) {
		nl_take();
		const u8 *mac = arp_lookup(hop);
		if (!mac && time_ms() >= next) {
			next = time_ms() + 200;
			arp_request(hop);
		}
		nl_drop();
		if (mac)
			return 1;
		if (time_ms() >= end)
			return 0;
		thread_sleep(5);
	}
}

/* ---- IPv4 */

static void icmp_input(u8 *p, u64 len, u32 src);
static void udp_input(u8 *p, u64 len, u32 src, u32 dst);
static void tcp_input(u8 *p, u64 len, u32 src, u32 dst);

static void ip_input(u8 *f, u64 len)
{
	u8 *ip = f + ETH_HDR;
	u64 l = len - ETH_HDR;
	if (l < IP_HDR || (ip[0] >> 4) != 4)
		return;
	u64 hl = (u64)(ip[0] & 0xF) << 2; /* longueur de l'en-tête */
	if (hl < IP_HDR || hl > l)
		return;
	u64 tot = ld16be(ip + 2); /* longueur totale */
	if (tot > l || tot < hl)
		return;
	if (csum_fold(csum_add(ip, hl, 0))) /* somme de contrôle de l'en-tête */
		return;
	if (ld16be(ip + 6) & 0x3FFF) /* fragments : non gérés */
		return;
	u32 src = ld32be(ip + 12);
	u32 dst = ld32be(ip + 16); /* destination : nous, diffusion, boucle */
	u32 me = net_ip;
	if (dst != me && dst != 0xFFFFFFFF && (dst >> 24) != 127)
		return;
	/* adresses martiennes : 127.x (ou notre adresse comme source) seulement par la boucle locale ; sinon un
	 * voisin du réseau parlait aux services qui n'écoutent que localhost (R9, E67) */
	if (!net_from_lo && ((src >> 24) == 127 || (dst >> 24) == 127 || (me && src == me) || src == 0xFFFFFFFF))
		return;
	if (dst == 0xFFFFFFFF && ip[9] == PROTO_ICMP)
		return; /* (pas d'écho vers la diffusion) */
	/* (plus d'apprentissage de l'adresse MAC depuis les paquets IP : n'importe qui détournait ainsi le trafic
	 * d'un voisin ; seulement l'ARP, ci-dessus, R4) */
	u8 *pl = ip + hl; /* charge utile */
	u64 n = tot - hl;
	switch (ip[9]) { /* protocole */
	case PROTO_TCP:
		tcp_input(pl, n, src, dst);
		break;
	case PROTO_UDP:
		udp_input(pl, n, src, dst);
		break;
	case PROTO_ICMP:
		icmp_input(pl, n, src);
		break;
	}
}

void net_input(u8 *f, u64 len)
{
	if (len < ETH_HDR)
		return;
	u32 t = ld16be(f + 12);
	if (t == ETH_ARP)
		arp_input(f, len);
	else if (t == ETH_IP)
		ip_input(f, len);
}

/* destination, protocole, longueur de la charge utile (déjà écrite à net_txbuf +
 * PAYLOAD) -> 1 si la trame est partie (0 : adresse MAC inconnue, demande ARP
 * envoyée, trame perdue) */
int ip_output(u32 dst, u32 proto, u64 plen)
{
	u8 *b = net_txbuf, *ip = b + ETH_HDR;
	ip[0] = 0x45;
	ip[1] = 0;
	st16be(ip + 2, (u32)plen + IP_HDR);
	st16be(ip + 4, net_ip_id++);
	st16be(ip + 6, 0x4000); /* ne pas fragmenter */
	ip[8] = 64;
	ip[9] = (u8)proto;
	ip[10] = ip[11] = 0;
	st32be(ip + 12, ip_src(dst));
	st32be(ip + 16, dst);
	st16be(ip + 10, csum_fold(csum_add(ip, IP_HDR, 0)));
	if (ip_is_local(dst)) { /* vers nous : pas de réseau */
		st16be(b + 12, ETH_IP);
		return lo_put(b, plen + PAYLOAD);
	}
	if (dst == 0xFFFFFFFF) { /* Ethernet : destination */
		memset(b, 0xFF, 6);
	} else {
		u32 hop = ip_next_hop(dst);
		const u8 *mac = arp_lookup(hop);
		if (!mac) {
			arp_request(hop);
			return 0;
		}
		memcpy(b, mac, 6);
	}
	memcpy(b + 6, &netdev_mac, 6);
	st16be(b + 12, ETH_IP);
	u64 n = plen + PAYLOAD; /* longueur de trame (minimum 60) */
	while (n < 60)
		b[n++] = 0; /* bourrage à zéro */
	return netif_send(b, n);
}

/* ---- ICMP */

static void icmp_input(u8 *p, u64 len, u32 src)
{
	if (len < 8 || len > 1400)
		return;
	if (csum_fold(csum_add(p, len, 0)))
		return;
	if (p[0] == 8) { /* demande d'écho -> réponse */
		u8 *r = net_txbuf + PAYLOAD;
		memmove(r, p, len);
		r[0] = 0; /* type 0 : réponse */
		r[2] = r[3] = 0;
		st16be(r + 2, csum_fold(csum_add(r, len, 0)));
		ip_output(src, PROTO_ICMP, len);
		return;
	}
	if (p[0]) /* 0 : réponse d'écho */
		return;
	if (ld16be(p + 4) != ping_id || ld16be(p + 6) != ping_seq) /* identifiant / séquence attendus ? */
		return;
	ping_reply_ms = time_ms();
}

/* ---- UDP */

void sk_rx_write(struct sock *s, const u8 *src, u64 n);
struct sock *sock_find_port(u64 type, u64 port);
static void dhcp_input(u8 *m, u64 len);

static void udp_input(u8 *p, u64 len, u32 src, u32 dst)
{
	if (len < 8)
		return;
	u64 ul = ld16be(p + 4); /* longueur UDP */
	if (ul > len || ul < 8)
		return;
	if (*(u16 *)(p + 6)) /* somme de contrôle (0 : absente) */
		if (csum_fold(csum_add(p, ul, csum_pseudo(src, dst, PROTO_UDP, (u32)ul))))
			return;
	u32 dport = ld16be(p + 2);
	if (dport == 68) { /* client DHCP */
		dhcp_input(p + 8, ul - 8);
		return;
	}
	struct sock *s = sock_find_port(SOCK_UDP, dport);
	if (!s)
		return;
	/* enregistrement : longueur(2) port(2) ip(4) données, si la place suffit */
	u64 room = SK_RING - (s->rxhead - s->rxtail);
	u64 n = ul - 8;
	if (n + 8 > room)
		return;
	u8 h[8];
	*(u16 *)h = (u16)n;
	*(u16 *)(h + 2) = (u16)ld16be(p);
	*(u32 *)(h + 4) = src;
	sk_rx_write(s, h, 8);
	sk_rx_write(s, p + 8, n);
}

/* port source, destination, port destination, données (0 : déjà à net_txbuf +
 * PAYLOAD + 8), longueur -> 1 si envoyé */
int udp_output(u32 sport, u32 dst, u32 dport, const u8 *data, u64 n)
{
	u8 *u = net_txbuf + PAYLOAD;
	if (data)
		memmove(u + 8, data, n);
	u32 ul = (u32)n + 8;
	st16be(u, sport);
	st16be(u + 2, dport);
	st16be(u + 4, ul);
	u[6] = u[7] = 0;
	u16 c = csum_fold(csum_add(u, ul, csum_pseudo(ip_src(dst), dst, PROTO_UDP, ul)));
	st16be(u + 6, c ? c : 0xFFFF);
	return ip_output(dst, PROTO_UDP, ul);
}

/* ---- DHCP */

static void dhcp_send(u32 type);

/* minuterie (sous net_lock) */
void dhcp_timer(void)
{
	if (!netdev_up || dhcp_state < DHCP_INIT || dhcp_state >= DHCP_BOUND)
		return;
	u64 now = time_ms();
	if (now < dhcp_next_ms)
		return;
	/* nouvel essai dans 0,5 s, 1 s, puis toutes les 2 s : la première demande part
	 * souvent avant que le lien Wi-Fi passe les trames (docs/BOOT.md) */
	u64 t = dhcp_tries++;
	dhcp_next_ms = now + (t < 2 ? 500UL << t : 2000);
	if (!t) /* (le même pour les renvois : une offre lente reste bonne) */
		dhcp_xid = net_rand32(); /* (avant : l'heure, devinable) */
	dhcp_send(1); /* DISCOVER */
	dhcp_state = DHCP_SELECTING;
}

/* type de message (1 DISCOVER, 3 REQUEST) */
static void dhcp_send(u32 type)
{
	u8 *d = net_txbuf + PAYLOAD + 8;
	memset(d, 0, 304);
	d[0] = 1; /* op BOOTREQUEST, htype 1, hlen 6, hops 0 */
	d[1] = 1;
	d[2] = 6;
	st32be(d + 4, dhcp_xid);
	st16be(d + 10, 0x8000); /* réponse en diffusion */
	memcpy(d + 28, &netdev_mac, 6); /* chaddr */
	u8 *o = d + 236; /* options */
	*o++ = 0x63; /* cookie magique */
	*o++ = 0x82;
	*o++ = 0x53;
	*o++ = 0x63;
	*o++ = 53; /* type de message */
	*o++ = 1;
	*o++ = (u8)type;
	if (type == 3) {
		*o++ = 50; /* adresse demandée */
		*o++ = 4;
		st32be(o, dhcp_offer_ip);
		o += 4;
		*o++ = 54; /* serveur */
		*o++ = 4;
		st32be(o, dhcp_server);
		o += 4;
	}
	*o++ = 55; /* paramètres voulus : masque, routeur, DNS */
	*o++ = 3;
	*o++ = 1;
	*o++ = 3;
	*o++ = 6;
	*o++ = 255;
	udp_output(68, 0xFFFFFFFF, 67, 0, 300);
}

static void dhcp_input(u8 *m, u64 len)
{
	if (len < 240 || m[0] != 2) /* BOOTREPLY */
		return;
	if (ld32be(m + 4) != dhcp_xid)
		return;
	u32 yi = ld32be(m + 16);
	u32 type = 0, mask = 0, router = 0, dns = 0, server = 0;
	u8 *p = m + 240, *end = m + len;
	while (p < end) { /* options (bornées par le paquet) */
		u8 k = *p;
		if (k == 255)
			break;
		if (!k) {
			p++;
			continue;
		}
		if (p + 2 > end)
			break;
		u8 l = p[1], *v = p + 2;
		if (v + l > end)
			break;
		if (k == 53 && l >= 1)
			type = v[0];
		else if (k == 1 && l >= 4)
			mask = ld32be(v);
		else if (k == 3 && l >= 4)
			router = ld32be(v);
		else if (k == 6 && l >= 4)
			dns = ld32be(v);
		else if (k == 54 && l >= 4)
			server = ld32be(v);
		p = v + l;
	}
	u32 st = dhcp_state;
	/* l'accusé (ou le refus) : du serveur de l'offre seulement (option 54), pas d'un autre du réseau */
	if ((type == 5 || type == 6) && dhcp_server && server != dhcp_server)
		return;
	if (type == 2) { /* OFFER */
		if (st != DHCP_SELECTING)
			return;
		dhcp_offer_ip = yi;
		dhcp_server = server;
		dhcp_state = DHCP_REQUESTING;
		dhcp_next_ms = time_ms() + 2000; /* l'accusé a 2 s pour venir avant qu'un renvoi ne reparte de zéro */
		dhcp_send(3);
	} else if (type == 5) { /* ACK */
		if (st != DHCP_REQUESTING)
			return;
		dhcp_state = DHCP_BOUND;
		net_ip = yi;
		net_mask = mask;
		net_gw = router;
		net_dns = dns;
		ak_puts("NET : DHCP ok, adresse ");
		kput_ip(yi);
		ak_puts("  passerelle ");
		kput_ip(net_gw);
		ak_puts("  DNS ");
		kput_ip(net_dns);
		ak_puts("\n");
	} else if (type == 6) { /* NAK : on recommence */
		dhcp_state = DHCP_INIT;
	}
}

/* ---- sockets : outils */

/* -> socket libre (mis à zéro, used = 1), 0 si table pleine */
struct sock *sock_alloc(void)
{
	for (int i = 0; i < MAX_SOCK; i++) {
		struct sock *s = &sock_table[i];
		if (s->used)
			continue;
		memset(s, 0, sizeof *s);
		s->used = 1;
		return s;
	}
	return 0;
}

/* tx = 1 : aussi un tampon d'émission -> 1 si ok */
int sk_alloc_rings(struct sock *s, int tx)
{
	u8 *p = pmm_alloc_n(SK_RING_PAGES);
	if (!p)
		return 0;
	s->rxbuf = p;
	if (!tx)
		return 1;
	p = pmm_alloc_n(SK_RING_PAGES);
	if (!p)
		return 0;
	s->txbuf = p;
	return 1;
}

/* libère ses tampons et son emplacement (les autres champs restent, comme l'asm) */
void sock_free(struct sock *s)
{
	ring_release(s, 0);
	if (s->rxbuf)
		pmm_free_n(s->rxbuf, SK_RING_PAGES);
	if (s->txbuf)
		pmm_free_n(s->txbuf, SK_RING_PAGES);
	s->rxbuf = 0;
	s->txbuf = 0;
	s->used = 0;
}

/* type, port local -> socket (UDP, ou TCP en écoute), 0 sinon */
struct sock *sock_find_port(u64 type, u64 port)
{
	for (int i = 0; i < MAX_SOCK; i++) {
		struct sock *s = &sock_table[i];
		if (!s->used || (u32)s->type != (u32)type || (u32)s->lport != (u32)port)
			continue;
		if (type != SOCK_TCP || s->state == TCP_LISTEN)
			return s;
	}
	return 0;
}

/* aléa du noyau ; avant : l'heure ou un compteur, que devinait un tiers (empoisonnement DNS, DHCP,
 * injection TCP : R2, R3, R5, R7, E66) */
u32 net_rand32(void)
{
	u32 v = 0;
	if (korin_sys2(SYS_GETRANDOM, &v, 4) != 4)
		v = (u32)arch_counter() * 0x9E3779B1u; /* (pas d'aléa : comme avant) */
	return v;
}

/* -> port local éphémère : tiré au hasard parmi les libres (sous nl_take) */
u32 sock_ephemeral(void)
{
	for (int k = 0; k < 32; k++) {
		u32 p = EPHEMERAL + (net_rand32() & 0x3FFF);
		if (!sock_find_port(SOCK_UDP, p) && !sock_find_port(SOCK_TCP, p))
			return p;
	}
	u32 p = net_next_port;
	net_next_port = (p + 1) & 0x3FFF;
	return p + EPHEMERAL;
}

void sk_rx_write(struct sock *s, const u8 *src, u64 n)
{
	u64 h = s->rxhead;
	while (n--)
		s->rxbuf[h++ & (SK_RING - 1)] = *src++;
	s->rxhead = h;
}

/* lit et consomme l'anneau de réception */
void sk_rx_read(struct sock *s, u8 *dst, u64 n)
{
	u64 t = s->rxtail;
	while (n--)
		*dst++ = s->rxbuf[t++ & (SK_RING - 1)];
	s->rxtail = t;
}

u64 sk_rx_free(struct sock *s)
{
	return SK_RING - (s->rxhead - s->rxtail);
}

/* ---- TCP */

static void tcp_arm_rtx_force(struct sock *s);

/* drapeaux, séquence, octets de données à prendre dans l'anneau d'émission à
 * partir de txtail + (séquence - SND_UNA) -> 1 si envoyé */
int tcp_send(struct sock *s, u32 fl, u32 seq, u64 n)
{
	if (fl & TH_ACK) { /* tout segment porte l'accusé */
		s->ack_cnt = 0;
		s->ack_at = 0;
	}
	u8 *t = net_txbuf + PAYLOAD; /* en-tête TCP */
	u64 hl = 20;
	if (fl & TH_SYN) {
		hl = 24; /* option MSS : kind 2, len 4, 1460 */
		t[20] = 2;
		t[21] = 4;
		st16be(t + 22, TCP_MSS);
		if (!(fl & TH_ACK) || s->ws_ok) { /* window scale : toujours dans notre SYN, dans le SYN-ACK si le pair l'a */
			hl = 28; /* NOP, kind 3, len 3, décalage */
			t[24] = 1;
			t[25] = 3;
			t[26] = 3;
			t[27] = TCP_WS_SHIFT;
		}
	}
	st16be(t, (u32)s->lport);
	st16be(t + 2, (u32)s->rport);
	st32be(t + 4, seq);
	st32be(t + 8, (fl & TH_ACK) ? s->rcv_nxt : 0); /* acquittement */
	t[12] = (u8)(hl << 2); /* (longueur/4) << 4 */
	t[13] = (u8)fl;
	u64 w = 0; /* fenêtre = place libre en réception */
	if (s->rxbuf) {
		w = sk_rx_free(s);
		if (!(fl & TH_SYN)) /* (jamais décalée dans un SYN) */
			w >>= s->rcv_shift;
		if (w > 0xFFFF)
			w = 0xFFFF;
	}
	st16be(t + 14, (u32)w);
	t[16] = t[17] = t[18] = t[19] = 0; /* somme de contrôle, urgence */
	if (n) { /* données depuis l'anneau d'émission */
		u64 i = s->txtail + (u32)(seq - s->snd_una);
		u8 *d = t + hl;
		for (u64 k = 0; k < n; k++)
			d[k] = s->txbuf[(i + k) & (SK_RING - 1)];
	}
	u32 tl = (u32)(hl + n);
	st16be(t + 16, csum_fold(csum_add(t, tl, csum_pseudo(ip_src(s->rip), s->rip, PROTO_TCP, tl))));
	return ip_output(s->rip, PROTO_TCP, tl);
}

/* ACK seul */
void tcp_send_ack(struct sock *s)
{
	tcp_send(s, TH_ACK, s->snd_nxt, 0);
}

/* réponse RST à un segment sans socket */
static void tcp_send_rst(u32 rip, u32 rport, u32 lport, u32 seq, u32 ack, u32 fl)
{
	struct sock t; /* socket temporaire sur la pile */
	memset(&t, 0, sizeof t);
	t.rip = rip;
	t.rport = rport;
	t.lport = lport;
	t.rcv_nxt = ack;
	tcp_send(&t, fl, seq, 0);
}

/* graine -> numéro de séquence initial */
u32 tcp_iss(u32 seed)
{
	(void)seed;
	return net_rand32(); /* (aléatoire : un tiers ne devine plus la séquence pour injecter, R7) */
}

/* lit les options du segment (MSS, défaut 536 ; window scale) */
static void tcp_parse_mss(struct sock *s, const u8 *seg)
{
	s->ws_ok = 0;
	s->snd_shift = 0;
	s->rcv_shift = 0;
	u64 mss = 536;
	const u8 *p = seg + 20, *end = seg + ((seg[12] >> 4) << 2);
	while (p < end) {
		u8 k = *p;
		if (!k)
			break;
		if (k == 1) {
			p++;
			continue;
		}
		u8 l = p[1];
		if (l < 2)
			break;
		if (k == 3) { /* window scale du pair (14 au plus) */
			s->snd_shift = p[2] > 14 ? 14 : p[2];
			s->ws_ok = 1;
			s->rcv_shift = TCP_WS_SHIFT;
		} else if (k == 2) {
			mss = (u64)p[2] << 8 | p[3];
		}
		p += l;
	}
	if (mss > TCP_MSS)
		mss = TCP_MSS;
	if (mss < 64)
		mss = 64;
	s->mss = mss;
	s->cwnd = mss * TCP_IW; /* (contrôle de congestion : départ) */
	s->ssthresh = TCP_SSTHRESH;
	s->dupacks = 0;
	s->inrec = 0;
}

/* socket en écoute -> fils en attente d'accept */
static u64 tcp_pending_count(struct sock *l)
{
	u64 n = 0;
	for (int i = 0; i < MAX_SOCK; i++)
		if (sock_table[i].used && sock_table[i].parent == l)
			n++;
	return n;
}

static void tcp_rtx_head(struct sock *s);

/* une perte : seuil à la moitié du vol, récupération jusqu'à SND_NXT -> seuil */
static u64 tcp_cc_loss(struct sock *s)
{
	u64 th = (u32)(s->snd_nxt - s->snd_una) >> 1;
	if (th < s->mss * 2)
		th = s->mss * 2;
	s->ssthresh = th;
	s->recover = s->snd_nxt;
	s->inrec = 1;
	s->rtt_seq = 0; /* Karn */
	return th;
}

/* octets acquittés, acquittement (SND_UNA déjà avancé) : fin de récupération,
 * nouveau trou (accusé partiel), ou fenêtre qui grandit */
static void tcp_cc_ack(struct sock *s, u64 acked, u32 ack)
{
	s->dupacks = 0;
	if (s->inrec) {
		if ((i32)(ack - s->recover) >= 0) { /* tout est récupéré */
			s->inrec = 0;
			s->cwnd = s->ssthresh;
			return;
		}
		u64 c = s->cwnd - acked + s->mss; /* accusé partiel : le trou suivant */
		if (c < s->mss)
			c = s->mss;
		s->cwnd = c;
		tcp_rtx_head(s);
		return;
	}
	u64 c = s->cwnd;
	if (c < s->ssthresh) { /* démarrage lent */
		c += acked < s->mss ? acked : s->mss;
	} else { /* évitement de congestion */
		u64 inc = s->mss * s->mss / c;
		c += inc ? inc : 1;
	}
	if (c > 0x1000000)
		c = 0x1000000;
	s->cwnd = c;
}

/* un accusé dupliqué ; au troisième, retransmission rapide */
static void tcp_cc_dup(struct sock *s)
{
	s->dupacks++;
	if (s->inrec) { /* en récupération : une trame est sortie */
		s->cwnd += s->mss;
		return;
	}
	if (s->dupacks != TCP_DUPACK_MAX)
		return;
	net_stat(4);
	u64 th = tcp_cc_loss(s);
	s->cwnd = s->mss * TCP_DUPACK_MAX + th;
	tcp_rtx_head(s);
}

/* renvoie le plus ancien segment non acquitté */
static void tcp_rtx_head(struct sock *s)
{
	u32 fl = (u32)(s->snd_nxt - s->snd_una); /* en vol */
	if (s->flags & F(SKF_FIN_SENT))
		fl--; /* le FIN n'est pas une donnée */
	if (!fl)
		return;
	u64 n = fl < s->mss ? fl : s->mss;
	tcp_send(s, TH_ACK | TH_PSH, s->snd_una, n);
	tcp_arm_rtx_force(s);
}

/* prochaine sonde à max(2 x SRTT, TCP_PROBE_MS), doublée à chaque sonde */
static void tcp_probe_arm(struct sock *s, u64 now)
{
	u64 d = s->srtt << 1;
	if (d < TCP_PROBE_MS)
		d = TCP_PROBE_MS;
	s->probe_at = now + (d << s->probe_n);
}

/* (re)arme la retransmission à maintenant + RTO */
static void tcp_arm_rtx_force(struct sock *s)
{
	u64 now = time_ms();
	s->rtx_at = now + s->rto;
	s->probe_n = 0;
	tcp_probe_arm(s, now);
}

/* arme la retransmission si elle ne l'est pas */
void tcp_arm_rtx(struct sock *s)
{
	if (!s->rtx_at)
		tcp_arm_rtx_force(s);
}

/* TIME-WAIT pendant 2 s */
static void tcp_enter_time_wait(struct sock *s)
{
	s->state = TCP_TIME_WAIT;
	s->rtx_at = 0;
	s->deadline = time_ms() + 2000;
}

/* socket fermé : libéré si l'application ne le tient plus */
static void tcp_maybe_free(struct sock *s)
{
	if (s->flags & F(SKF_ORPHAN))
		sock_free(s);
}

/* range les données à leur place dans l'anneau (sans avancer rxhead) et
 * mémorise la plage ; off = séquence - RCV_NXT > 0 */
static void tcp_ooo_store(struct sock *s, u64 off, const u8 *d, u64 n, u32 seq)
{
	if (!n)
		return;
	if (off + n > sk_rx_free(s)) /* doit tenir dans la place libre */
		return;
	u64 h = s->rxhead + off;
	for (u64 k = 0; k < n; k++)
		s->rxbuf[(h + k) & (SK_RING - 1)] = d[k];
	u32 e = seq + (u32)n; /* plage [seq, e[ */
	for (u64 i = 0; i < s->ooo_n; i++) { /* fusion avec une plage qui touche */
		u32 *r = s->ooo[i];
		if ((i32)(e - r[0]) < 0 || (i32)(r[1] - seq) < 0)
			continue;
		if ((i32)(seq - r[0]) < 0) /* union */
			r[0] = seq;
		if ((i32)(e - r[1]) >= 0)
			r[1] = e;
		return;
	}
	net_stat(2);
	if (s->ooo_n >= OOO_MAX) {
		net_stat(3); /* plus de place : données non suivies */
		return;
	}
	s->ooo[s->ooo_n][0] = seq;
	s->ooo[s->ooo_n][1] = e;
	s->ooo_n++;
}

/* intègre les plages hors ordre que RCV_NXT a rejointes */
static void tcp_ooo_merge(struct sock *s)
{
again:
	for (u64 i = 0; i < s->ooo_n; i++) {
		u32 *r = s->ooo[i];
		u32 nxt = s->rcv_nxt;
		if ((i32)(r[0] - nxt) > 0) /* début <= RCV_NXT ? */
			continue;
		i32 d = (i32)(r[1] - nxt);
		if (d > 0) { /* fin > RCV_NXT : avancer */
			s->rxhead += (u32)d;
			s->rcv_nxt = r[1];
		}
		s->ooo_n--; /* retirer la plage (la dernière prend sa place) */
		r[0] = s->ooo[s->ooo_n][0];
		r[1] = s->ooo[s->ooo_n][1];
		goto again;
	}
}

/* segment, longueur, IP source, IP destination (net_lock) */
static void tcp_input(u8 *seg, u64 len, u32 sip, u32 dip)
{
	if (len < 20)
		return;
	if (csum_fold(csum_add(seg, len, csum_pseudo(sip, dip, PROTO_TCP, (u32)len)))) /* somme de contrôle */
		return;
	u64 hl = (u64)(seg[12] >> 4) << 2; /* longueur de l'en-tête */
	if (hl < 20 || hl > len)
		return;
	u32 fl = seg[13];
	u32 seq = ld32be(seg + 4);
	u32 ack = ld32be(seg + 8);
	u8 *data = seg + hl;
	u64 dlen = len - hl;
	u32 rport = ld16be(seg), lport = ld16be(seg + 2);
	u32 wnd = ld16be(seg + 14);
	struct sock *s = 0;

	/* recherche de la connexion */
	for (int i = 0; i < MAX_SOCK; i++) {
		struct sock *c = &sock_table[i];
		if (!c->used || c->type != SOCK_TCP || c->state < TCP_SYN_SENT)
			continue;
		if ((u32)c->lport == lport && (u32)c->rport == rport && c->rip == sip) {
			s = c;
			break;
		}
	}
	if (s)
		goto found;
	s = sock_find_port(SOCK_TCP, lport); /* socket en écoute ? */
	if (s)
		goto listen;

no_socket: /* RST (sauf à un RST) */
	if (fl & TH_RST)
		return;
	if (fl & TH_ACK)
		tcp_send_rst(sip, rport, lport, ack, 0, TH_RST); /* seq = leur ACK */
	else /* ack = seq + longueur (+1 si SYN, FIN) */
		tcp_send_rst(sip, rport, lport, 0, seq + (u32)dlen + !!(fl & TH_SYN) + !!(fl & TH_FIN),
			     TH_RST | TH_ACK);
	return;

listen: /* nouvelle connexion entrante */
	if (fl & TH_RST)
		return;
	if (fl & TH_ACK)
		goto no_socket;
	if (!(fl & TH_SYN))
		return;
	if (tcp_pending_count(s) >= s->backlog) /* place dans la file d'attente ? */
		return;
	{
		struct sock *c = sock_alloc();
		if (!c)
			return;
		if (!sk_alloc_rings(c, 1)) {
			sock_free(c);
			return;
		}
		c->type = SOCK_TCP;
		c->state = TCP_SYN_RCVD;
		c->lport = s->lport;
		c->rport = rport;
		c->rip = sip;
		c->parent = s;
		c->rcv_nxt = seq + 1;
		u32 iss = tcp_iss(seq);
		c->iss = iss;
		c->snd_una = iss;
		c->snd_nxt = iss + 1;
		tcp_parse_mss(c, seg);
		c->snd_wnd = wnd;
		c->rto = 1000;
		tcp_send(c, TH_SYN | TH_ACK, c->iss, 0); /* SYN-ACK */
		tcp_arm_rtx(c);
	}
	return;

found: /* connexion existante */
	if (s->state == TCP_SYN_SENT) {
		if (fl & TH_ACK) {
			if (ack != s->snd_nxt) {
				if (fl & TH_RST)
					return;
				goto no_socket; /* ACK inacceptable : RST */
			}
			if (fl & TH_RST) { /* connexion refusée */
				s->flags |= F(SKF_REFUSED);
				s->state = 0;
				s->rtx_at = 0;
				return;
			}
		} else if (fl & TH_RST) {
			return;
		}
		if (!(fl & TH_SYN))
			return;
		s->rcv_nxt = seq + 1;
		tcp_parse_mss(s, seg);
		s->snd_wnd = wnd;
		if (!(fl & TH_ACK))
			return; /* ouverture simultanée : non gérée */
		s->snd_una = ack;
		s->state = TCP_ESTAB;
		s->rtx_at = 0;
		s->retries = 0;
		tcp_send_ack(s);
		return;
	}
	if (fl & TH_RST) { /* RST : connexion réinitialisée */
		if ((u32)(seq - s->rcv_nxt) >= 0x10000) /* numéro dans la fenêtre (approximation) */
			return;
		if (s->state == TCP_SYN_RCVD) { /* fils pas encore accepté : supprimé */
			sock_free(s);
			return;
		}
		s->flags |= F(SKF_RESET);
		s->state = 0;
		s->rtx_at = 0;
		tcp_maybe_free(s);
		return;
	}
	if (fl & TH_SYN) /* SYN dans une connexion établie : ignoré */
		goto ack_only;
	if (!(fl & TH_ACK))
		return;

	/* traitement de l'acquittement */
	if (s->state == TCP_SYN_RCVD) {
		if (ack != s->snd_nxt)
			goto no_socket;
		s->snd_una = ack;
		s->state = TCP_ESTAB;
		s->rtx_at = 0;
		s->retries = 0;
		s->snd_wnd = (u64)wnd << s->snd_shift;
		goto data;
	}
	{
		u32 acked = ack - s->snd_una;
		u32 flight = s->snd_nxt - s->snd_una; /* en vol */
		if (!acked) {
			/* accusé dupliqué (RFC 5681) : sans données, ni SYN, ni FIN, même
			 * fenêtre, des données en vol */
			if (!dlen && !(fl & (TH_SYN | TH_FIN)) && flight &&
			    ((u64)wnd << s->snd_shift) == s->snd_wnd)
				tcp_cc_dup(s);
			goto win;
		}
		if (acked > flight)
			goto ack_only; /* acquitte des données jamais envoyées */
		if ((i32)acked < 0)
			goto win; /* ancien ACK */
		u64 n = acked; /* octets de données acquittés */
		int fin_acked = 0;
		if ((s->flags & F(SKF_FIN_SENT)) && ack == s->fin_seq + 1) {
			n--; /* notre FIN est acquitté */
			fin_acked = 1;
		}
		s->txtail += n;
		s->snd_una = ack;
		s->retries = 0;
		tcp_cc_ack(s, n, ack);
		/* mesure du RTT (Karn : seulement si pas de retransmission) */
		if (s->rtt_seq) {
			i32 d = (i32)(ack - s->rtt_seq);
			if (d > 0) {
				s->rtt_seq = 0;
				u64 rtt = time_ms() - s->rtt_start;
				if (s->srtt)
					rtt = (s->srtt * 7 + rtt) >> 3;
				s->srtt = rtt;
				/* RTO = 2 x SRTT + 50, borné 1000..16000 (200 ms : fausses
				 * expirations avec les accusés retardés de Windows) */
				u64 rto = (rtt << 1) + 50;
				if (rto < TCP_RTO_MIN)
					rto = TCP_RTO_MIN;
				if (rto > 16000)
					rto = 16000;
				s->rto = rto;
			}
		}
		if (s->snd_una == s->snd_nxt)
			s->rtx_at = 0;
		else
			tcp_arm_rtx_force(s);
		if (fin_acked) { /* transitions sur l'acquittement du FIN */
			if (s->state == TCP_FIN_WAIT1) {
				s->state = TCP_FIN_WAIT2;
				s->deadline = time_ms() + 10000; /* orphelin : 10 s au plus en FIN-WAIT-2 */
			} else if (s->state == TCP_CLOSING) {
				tcp_enter_time_wait(s);
			} else if (s->state == TCP_LAST_ACK) {
				s->state = 0;
				tcp_maybe_free(s);
				return;
			}
		}
	}
win:
	s->snd_wnd = (u64)wnd << s->snd_shift;

data: /* données */
	if (s->state < TCP_ESTAB)
		goto output;
	if (s->state > TCP_FIN_WAIT2 || !dlen) /* plus de données acceptées après leur FIN */
		goto fin;
	{
		u32 d = s->rcv_nxt - seq; /* déjà reçu en tête du segment ? */
		if ((i32)d < 0) { /* segment en avance : hors ordre ; rangé à sa place si la
				   * fenêtre le permet, puis ACK dupliqué */
			tcp_ooo_store(s, (u32)-d, data, dlen, seq);
			tcp_send_ack(s);
			goto output;
		}
		if (d >= dlen) {
			dlen = 0;
			seq += d;
			goto ack_now;
		}
		data += d;
		dlen -= d;
		seq += d;
		u64 room = sk_rx_free(s);
		u64 n = dlen <= room ? dlen : room; /* ce qui tient dans l'anneau */
		if (!n)
			goto ack_now;
		sk_rx_write(s, data, n);
		s->rx_len = n;
		s->rcv_nxt += (u32)n;
		seq += (u32)n;
		dlen -= n;
		tcp_ooo_merge(s); /* plages hors ordre devenues contiguës */
		if (dlen) /* (le FIN éventuel suit la dernière plage) */
			goto ack_now;
		seq = s->rcv_nxt;
		/* accusé retardé : segment plein, en ordre, sans PSH ni FIN, aucun trou */
		if (s->rx_len < TCP_ACK_FULL || (fl & (TH_PSH | TH_FIN)) || s->ooo_n)
			goto ack_now;
		u64 c = s->ack_cnt + 1;
		if (c >= TCP_ACK_EVERY)
			goto ack_now;
		s->ack_cnt = c;
		if (!s->ack_at)
			s->ack_at = time_ms() + TCP_ACK_MS;
		goto fin;
	}
ack_now:
	s->acknow = 1;

fin: /* FIN du pair */
	if ((fl & TH_FIN) && !dlen && seq == s->rcv_nxt) { /* (données non acceptées : FIN plus tard) */
		if (!(s->flags & F(SKF_RX_FIN))) {
			s->flags |= F(SKF_RX_FIN);
			s->rcv_nxt++;
			if (s->state == TCP_ESTAB)
				s->state = TCP_CLOSE_WAIT;
			else if (s->state == TCP_FIN_WAIT1)
				s->state = TCP_CLOSING;
			else if (s->state == TCP_FIN_WAIT2)
				tcp_enter_time_wait(s);
		}
		s->acknow = 1;
	}
	if (s->acknow) {
		s->acknow = 0;
		tcp_send_ack(s);
	}
output:
	tcp_output(s);
	return;
ack_only:
	tcp_send_ack(s);
}

/* envoie les données en attente que la fenêtre permet, puis FIN */
void tcp_output(struct sock *s)
{
	if (s->state != TCP_ESTAB && s->state != TCP_CLOSE_WAIT)
		return;
	if (s->flags & F(SKF_FIN_SENT))
		return;
	for (;;) {
		u64 flight = (u32)(s->snd_nxt - s->snd_una); /* en vol */
		u64 unsent = (s->txhead - s->txtail) - flight; /* pas encore envoyé */
		if (!unsent)
			break;
		u64 w = s->snd_wnd;
		if (!w) {
			if (flight) /* fenêtre nulle : sonde d'un octet si rien en vol */
				return;
			w = 1;
		}
		if (w > s->cwnd && s->cwnd) /* fenêtre du pair, de congestion */
			w = s->cwnd;
		if (w <= flight)
			return;
		u64 n = w - flight; /* utilisable */
		if (unsent < n)
			n = unsent;
		if (s->mss < n)
			n = s->mss;
		if (!s->rtt_seq) { /* chronométrer ce segment */
			s->rtt_seq = s->snd_nxt + (u32)n;
			s->rtt_start = time_ms();
		}
		if (!tcp_send(s, TH_ACK | TH_PSH, s->snd_nxt, n))
			return; /* adresse MAC perdue : la minuterie réessaiera */
		s->snd_nxt += (u32)n;
		tcp_arm_rtx(s);
	}
	if (!(s->flags & F(SKF_FIN_PENDING))) /* tout est envoyé : FIN demandé ? */
		return;
	s->flags |= F(SKF_FIN_SENT);
	u32 nxt = s->snd_nxt;
	s->fin_seq = nxt;
	s->snd_nxt = nxt + 1;
	s->state = s->state == TCP_ESTAB ? TCP_FIN_WAIT1 : TCP_LAST_ACK;
	tcp_send(s, TH_FIN | TH_ACK, nxt, 0);
	tcp_arm_rtx(s);
}

/* (sous net_lock) -> ms avant la prochaine vérification (net_sleep) */
u64 net_wait_ms(void)
{
	u64 w = (netdev_up && dhcp_state < DHCP_BOUND) ? NET_DHCP_MS : NET_LONG_MS;
	for (int i = 0; i < MAX_SOCK; i++) {
		struct sock *s = &sock_table[i];
		if (!s->used || s->type != SOCK_TCP)
			continue;
		if (s->ack_at || s->rtx_at || s->probe_at || s->state == TCP_TIME_WAIT)
			return NET_TICK_MS;
		/* (orphelin : son délai ; après un shutdown, le pair décide : rien à voir) */
		if (s->state == TCP_FIN_WAIT2 && (s->flags & F(SKF_ORPHAN)))
			return NET_TICK_MS;
	}
	return w;
}

/* minuteries TCP (sous net_lock, toutes les 10 ms) */
int tcp_timers(void)
{
	int changed = 0; /* -> 1 : un socket abandonné ou rendu (ce que regarde une requête qui attend : net_wait) */
	u64 now = time_ms();
	for (int i = 0; i < MAX_SOCK; i++) {
		struct sock *s = &sock_table[i];
		if (!s->used || s->type != SOCK_TCP)
			continue;
		if (s->ack_at && now >= s->ack_at) /* accusé retardé arrivé à échéance */
			tcp_send_ack(s);
		if (s->state == TCP_TIME_WAIT || /* fin de TIME-WAIT */
		    (s->state == TCP_FIN_WAIT2 && (s->flags & F(SKF_ORPHAN)))) { /* orphelin bloqué en FIN-WAIT-2 */
			if (now < s->deadline)
				continue;
			s->state = 0;
			tcp_maybe_free(s);
			changed = 1;
			continue;
		}
		if (!s->rtx_at)
			continue;
		if (now >= s->rtx_at) {
			if (++s->retries > 8) { /* trop d'essais : abandon */
				s->flags |= F(SKF_TIMEOUT);
				changed = 1;
				tcp_send(s, TH_RST | TH_ACK, s->snd_nxt, 0);
				s->state = 0;
				s->rtx_at = 0;
				if (s->parent) /* fils non accepté */
					sock_free(s);
				else
					tcp_maybe_free(s);
				continue;
			}
			net_stat(1);
			s->rtt_seq = 0; /* Karn : pas de mesure après retransmission */
			u64 rto = s->rto << 1; /* RTO doublé, 16 s au plus */
			s->rto = rto > 16000 ? 16000 : rto;
			tcp_arm_rtx_force(s);
			if (s->state == TCP_SYN_SENT) {
				tcp_send(s, TH_SYN, s->iss, 0);
				continue;
			}
			if (s->state == TCP_SYN_RCVD) {
				tcp_send(s, TH_SYN | TH_ACK, s->iss, 0);
				continue;
			}
			/* expiration : on repart d'un segment, les accusés partiels rendent les trous */
			tcp_cc_loss(s);
			s->cwnd = s->mss;
			s->dupacks = 0;
			u32 fl = s->snd_nxt - s->snd_una; /* plus ancien segment non acquitté */
			if (!fl)
				continue;
			if (s->flags & F(SKF_FIN_SENT)) {
				fl--; /* le FIN n'est pas une donnée */
				if (!fl) { /* seul le FIN reste : le renvoyer */
					tcp_send(s, TH_FIN | TH_ACK, s->fin_seq, 0);
					continue;
				}
			}
			tcp_send(s, TH_ACK | TH_PSH, s->snd_una, fl < s->mss ? fl : s->mss);
			continue;
		}
		/* plus d'accusé depuis un moment : le dernier segment envoyé, renvoyé seul,
		 * en réclame un tout de suite. Si les accusés se sont perdus (le pair a tout
		 * reçu : plus rien ne les ferait repartir), le débit reprend sans RTO ni
		 * baisse de la fenêtre de congestion */
		if (now < s->probe_at || s->probe_n >= TCP_PROBE_MAX || s->inrec || (s->flags & F(SKF_FIN_SENT)))
			continue;
		if (s->state != TCP_ESTAB && s->state != TCP_CLOSE_WAIT)
			continue;
		s->probe_n++;
		tcp_probe_arm(s, now);
		s->rtt_seq = 0; /* Karn */
		net_stat(5);
		u32 fl = s->snd_nxt - s->snd_una; /* en vol */
		if (!fl)
			continue;
		u64 n = fl < s->mss ? fl : s->mss;
		tcp_send(s, TH_ACK | TH_PSH, s->snd_nxt - (u32)n, n);
	}
	return changed;
}

/* fermeture par l'application (orphelin) */
void sock_close_locked(struct sock *s)
{
	ring_release(s, 0);
	s->owner = 0;
	s->flags |= F(SKF_ORPHAN);
	if (s->type != SOCK_TCP)
		goto free;
	if (s->state == TCP_LISTEN) { /* fils non acceptés : RST + libération */
		for (int i = 0; i < MAX_SOCK; i++) {
			struct sock *c = &sock_table[i];
			if (!c->used || c->parent != s)
				continue;
			tcp_send(c, TH_RST | TH_ACK, c->snd_nxt, 0);
			sock_free(c);
		}
		goto free;
	}
	if (s->state < TCP_SYN_RCVD) /* fermé ou SYN-SENT : libération immédiate */
		goto free;
	if (s->state == TCP_ESTAB || s->state == TCP_CLOSE_WAIT || s->state == TCP_SYN_RCVD) {
		s->flags |= F(SKF_FIN_PENDING);
		tcp_output(s);
	}
	return; /* (déjà en fermeture : le protocole finira) */
free:
	sock_free(s);
}

/* envoi UDP d'un programme -> octets, < 0 si erreur */
long udp_sendto_user(const u8 *data, u64 n, u32 ip, u32 port, struct sock *s)
{
	if (n > 1472)
		return E_INVAL;
	if (!arp_resolve_wait(ip))
		return E_HOSTUNREACH;
	nl_take();
	if (!s->lport)
		s->lport = sock_ephemeral();
	int r = udp_output((u32)s->lport, ip, port, data, n);
	nl_drop();
	return r ? (long)n : E_HOSTUNREACH;
}

/* ---- DNS */

/* « a.b.c.d » -> adresse, -1 (négatif) si ce n'en est pas une */
static long parse_dotted(const char *p)
{
	u64 r = 0;
	for (int k = 0;; p++) {
		u64 v = 0, nd = 0;
		while ((u8)(*p - '0') <= 9) {
			v = v * 10 + (u64)(*p - '0');
			p++;
			nd++;
		}
		if (!nd || v > 255)
			return -1;
		r = r << 8 | v;
		if (++k == 4)
			return *p ? -1 : (long)r;
		if (*p != '.')
			return -1;
	}
}

/* réponse DNS, longueur -> première adresse A, ou erreur */
static long dns_parse_a(const u8 *m, u64 len)
{
	const u8 *end = m + len, *p;
	if (len < 12 || (m[3] & 0xF)) /* RCODE */
		return E_NOENT;
	u32 an = (u32)m[6] << 8 | m[7]; /* ANCOUNT */
	if (!an)
		return E_NOENT;
#define SKIP_NAME()                                   \
	for (;;) {                                    \
		if (p >= end)                         \
			break;                        \
		u8 c = *p;                            \
		if (!c) {                             \
			p++;                          \
			break;                        \
		}                                     \
		if ((c & 0xC0) == 0xC0) {             \
			p += 2;                       \
			break;                        \
		}                                     \
		p += c + 1;                           \
	}
	p = m + 12; /* sauter la question */
	SKIP_NAME();
	p += 4;
	while (an--) {
		SKIP_NAME();
		if (p + 10 > end)
			return E_NOENT;
		u32 type = (u32)p[0] << 8 | p[1], cls = (u32)p[2] << 8 | p[3];
		u32 dl = (u32)p[8] << 8 | p[9]; /* longueur des données */
		p += 10;
		if (type == 1 && cls == 1 && dl == 4 && p + 4 <= end)
			return (long)ld32be(p);
		p += dl;
	}
	return E_NOENT;
#undef SKIP_NAME
}

/* nom -> adresse IPv4 (ordre hôte), ou erreur */
long dns_resolve(const char *name)
{
	u8 q[512];
	long r = parse_dotted(name);
	if (r >= 0) /* déjà une adresse numérique */
		return r;
	{ /* localhost : la boucle locale (sans la casse) */
		const char *a = name, *b = "localhost";
		while (*b && (*a | 0x20) == *b) {
			a++;
			b++;
		}
		if (!*b && !*a)
			return 0x7F000001;
	}
	u32 dns = net_dns;
	if (!dns || !arp_resolve_wait(dns))
		return E_HOSTUNREACH;
	if (strlen(name) > 253) /* (un nom plus long débordait la requête) */
		return E_INVAL;
	/* requête : en-tête 12 octets + nom en étiquettes + type A + classe IN */
	memset(q, 0, sizeof q);
	st16be(q, net_rand32() & 0xFFFF); /* identifiant (aléatoire, R2) */
	st16be(q + 2, 0x0100); /* récursion demandée */
	st16be(q + 4, 1);
	u8 *p = q + 12; /* étiquettes */
	const char *s = name;
	u64 n;
	for (;;) {
		u8 *lp = p++; /* octet de longueur */
		n = 0;
		for (;;) {
			char c = *s;
			if (!c)
				break;
			s++;
			if (c == '.')
				break;
			*p++ = (u8)c;
			if (++n > 63)
				return E_INVAL;
		}
		*lp = (u8)n;
		if (!n || s[-1] != '.')
			break;
	}
	if (n)
		*p++ = 0; /* étiquette vide finale */
	st16be(p, 1); /* type A */
	st16be(p + 2, 1); /* classe IN */
	p += 4;
	u64 qlen = (u64)(p - q);
	nl_take(); /* socket UDP interne */
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
	u8 *rep = q + 256; /* réponse dans la 2e moitié du tampon */
	for (int tries = 3;;) {
		nl_take();
		udp_output((u32)sk->lport, dns, 53, q, qlen);
		nl_drop();
		u64 end = time_ms() + 2000; /* (identifiant écrasé : relu dans la requête) */
	wait:
		nl_take();
		if (sk->rxhead != sk->rxtail) {
			sk_rx_read(sk, rep, 8);
			u64 l = *(u16 *)rep; /* longueur */
			u32 sport = *(u16 *)(rep + 2), sip = *(u32 *)(rep + 4);
			u64 take = l <= 248 ? l : 248;
			sk_rx_read(sk, rep, take);
			sk->rxtail += l - take; /* reste à jeter */
			nl_drop();
			/* du serveur DNS (adresse et port 53), une réponse (QR), même identifiant, même question
			 * (sans la casse) : sinon un tiers du réseau répondait à sa place (R2, E66) */
			if (sip != dns || sport != 53 || take < qlen || !(rep[2] & 0x80) || *(u16 *)q != *(u16 *)rep)
				goto wait;
			int same = 1;
			for (u64 i = 12; i < qlen && same; i++) {
				u8 a = rep[i], b = q[i];
				if (a >= 'A' && a <= 'Z')
					a += 32;
				if (b >= 'A' && b <= 'Z')
					b += 32;
				same = a == b;
			}
			if (!same)
				goto wait;
			res = dns_parse_a(rep, take);
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
