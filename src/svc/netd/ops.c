/* ops.c - requêtes des programmes (NetKit) : sockets, DNS, ping, état ;
 * traduction de ops.inc (reprise des appels 40-52 de src/kernel/net.asm) :
 * message (données du message, ou tampon du process : data_ptr) -> résultat.
 * Une attente se fait sans le verrou, 25 s au plus (E_AGAIN : NetKit redemande).
 *
 * Un socket rendu à un process a un ou plusieurs propriétaires (table
 * sk_owners : couples descripteur + 1, pid ; sous le verrou de la pile).
 * Plusieurs : après un fork (avis SVC_OP_FORK), le fils tient les sockets du
 * père aux mêmes descripteurs. close et la fin d'un process retirent le pid ; le
 * socket se ferme avec son dernier propriétaire. owner garde l'un d'eux
 * (netstat ; 0 : orphelin). */
#include "netd.h"

/* descripteur, pid -> son couple (0 : aucun) */
static u32 *sko_find(u64 sd, u64 pid)
{
	u32 k = (u32)sd + 1;
	for (int i = 0; i < SKO_MAX; i++)
		if (sk_owners[i][0] == k && sk_owners[i][1] == (u32)pid)
			return sk_owners[i];
	return 0;
}

/* (verrou pris) un propriétaire de plus -> 1, 0 si la table est pleine */
static int sko_add(u64 sd, u64 pid)
{
	if (sko_find(sd, pid))
		return 1;
	for (int i = 0; i < SKO_MAX; i++)
		if (!sk_owners[i][0]) {
			sk_owners[i][0] = (u32)sd + 1;
			sk_owners[i][1] = (u32)pid;
			return 1;
		}
	return 0;
}

/* (verrou pris) plus aucun propriétaire */
static void sko_clear(u64 sd)
{
	u32 k = (u32)sd + 1;
	for (int i = 0; i < SKO_MAX; i++)
		if (sk_owners[i][0] == k) {
			sk_owners[i][0] = 0;
			sk_owners[i][1] = 0;
		}
}

/* (verrou pris) pid qui le quitte -> 1 si plus personne ne le tient (à fermer) ;
 * sinon owner = un propriétaire restant */
static int sko_drop(u64 sd, u64 pid)
{
	u32 *o = sko_find(sd, pid);
	if (o) {
		o[0] = 0;
		o[1] = 0;
	}
	u32 k = (u32)sd + 1;
	for (int i = 0; i < SKO_MAX; i++)
		if (sk_owners[i][0] == k) {
			sock_table[sd].owner = sk_owners[i][1];
			return 0;
		}
	return 1;
}

/* descripteur, pid -> socket de ce process (0 si invalide). Un socket tenu par un
 * autre pid : l'avis de fork qui le donne à ce pid peut être servi par un autre
 * thread juste après cette requête ; on le laisse passer (quelques ms) avant de
 * refuser. */
static struct sock *sk_get(u64 sd, u64 pid)
{
	if (sd >= MAX_SOCK || !pid)
		return 0;
	for (int tries = 5;;) {
		struct sock *s = &sock_table[sd];
		if (!VOL(s->used) || !VOL(s->owner)) /* (orphelin) */
			return 0;
		if (sko_find(sd, pid))
			return s;
		if (!--tries)
			return 0;
		thread_sleep(2);
	}
}

/* avis de fork : le fils tient les sockets du père (mêmes descripteurs ; son
 * tampon : le sien, op 1) */
void sko_fork(u64 child, u64 parent)
{
	if (!child || !parent)
		return;
	nl_take();
	for (int i = 0; i < SKO_MAX; i++)
		if (sk_owners[i][0] && sk_owners[i][1] == (u32)parent)
			sko_add(sk_owners[i][0] - 1, child);
	nl_drop();
}

/* délai demandé -> min(délai, WAIT_MAX) */
static u64 wait_cap(u64 d)
{
	return d < WAIT_MAX ? d : WAIT_MAX;
}

/* 2 socket(a type) -> descripteur */
long op_socket(u8 *m)
{
	u64 pid = W32(m, MSG_PID), type = W64(m, MSG_A);
	if (type != SOCK_TCP && type != SOCK_UDP)
		return E_INVAL;
	nl_take();
	struct sock *s = sock_alloc();
	if (!s)
		goto full;
	s->type = type;
	s->owner = pid;
	long sd = sk_index(s); /* son propriétaire */
	sko_clear(sd);
	if (!sko_add(sd, pid))
		goto none;
	s->rto = 1000;
	s->mss = 536;
	if (sk_alloc_rings(s, type == SOCK_TCP)) {
		nl_drop();
		return sd;
	}
	sko_clear(sd);
none:
	sock_free(s);
full:
	nl_drop();
	return E_NOMEM;
}

/* 3 connect(a sd, b ip, c port) */
long op_connect(u8 *m)
{
	u32 ip = W32(m, MSG_B), port = W32(m, MSG_C);
	struct sock *s = sk_get(W64(m, MSG_A), W32(m, MSG_PID));
	if (!s)
		return E_BADF;
	if (s->type != SOCK_TCP) { /* UDP : destination par défaut */
		s->rip = ip;
		s->rport = port;
		return 0;
	}
	if (s->state)
		return E_INVAL;
	if (!arp_resolve_wait(ip))
		return E_HOSTUNREACH;
	nl_take();
	s->rip = ip;
	s->rport = port;
	if (!s->lport)
		s->lport = sock_ephemeral();
	u32 iss = tcp_iss(port);
	s->iss = iss;
	s->snd_una = iss;
	s->snd_nxt = iss + 1;
	s->state = TCP_SYN_SENT;
	tcp_send(s, TH_SYN, s->iss, 0);
	tcp_arm_rtx(s);
	nl_drop();
	u64 end = time_ms() + 20000;
	u64 sd0 = W64(m, MSG_A), pid = W32(m, MSG_PID);
	for (;;) {
		u32 gen = net_gen_now(); /* (avant de regarder : net_wait) */
		/* (correction, comme dans op_accept : fermé pendant l'attente et repris
		 * par un autre process : ce n'est plus notre connexion) */
		nl_take();
		int mine = s->used && sko_find(sd0, pid);
		nl_drop();
		if (!mine)
			return E_BADF;
		if (VOL(s->state) >= TCP_ESTAB)
			return 0;
		u64 f = VOL(s->flags);
		if (f & F(SKF_REFUSED))
			return E_CONNREFUSED;
		if (f & F(SKF_TIMEOUT))
			break;
		if (f & F(SKF_RESET))
			return E_CONNREFUSED;
		if (time_ms() >= end)
			break;
		net_wait(gen, end);
	}
	nl_take(); /* abandon : la socket redevient fermée */
	s->state = 0;
	s->rtx_at = 0;
	nl_drop();
	return E_TIMEDOUT;
}

/* 4 bind(a sd, b port) */
long op_bind(u8 *m)
{
	u64 port = W64(m, MSG_B);
	struct sock *s = sk_get(W64(m, MSG_A), W32(m, MSG_PID));
	if (!s)
		return E_BADF;
	if (!port || port > 0xFFFF)
		return E_INVAL;
	nl_take();
	for (int i = 0; i < MAX_SOCK; i++) { /* port déjà pris pour ce type ? */
		struct sock *c = &sock_table[i];
		/* (connexion acceptée : même port ; le socket lui-même compte, comme l'asm) */
		if (c->used && c->type == s->type && c->lport == port && !c->parent && !c->rip) {
			nl_drop();
			return E_ADDRINUSE;
		}
	}
	s->lport = port;
	nl_drop();
	return 0;
}

/* 5 listen(a sd, b file d'attente) */
long op_listen(u8 *m)
{
	u64 q = W64(m, MSG_B);
	struct sock *s = sk_get(W64(m, MSG_A), W32(m, MSG_PID));
	if (!s)
		return E_BADF;
	if (s->type != SOCK_TCP || !s->lport)
		return E_INVAL;
	if (q < 1)
		q = 1;
	if (q > 16)
		q = 16;
	s->backlog = q;
	s->state = TCP_LISTEN;
	return 0;
}

/* 6 accept(a sd, b délai ms) -> descripteur de la connexion */
long op_accept(u8 *m)
{
	u64 pid = W32(m, MSG_PID);
	u64 d = wait_cap(W64(m, MSG_B));
	struct sock *s = sk_get(W64(m, MSG_A), pid);
	if (!s)
		return E_BADF;
	if (s->state != TCP_LISTEN)
		return E_INVAL;
	u64 sd0 = W64(m, MSG_A);
	u64 end = time_ms() + d;
	for (;;) {
		u32 gen = net_gen_now(); /* (avant de regarder : net_wait) */
		nl_take();
		/* Correction (l'asm ne le fait pas) : le socket est-il toujours à
		 * l'appelant ? Fermé pendant l'attente, ou l'appelant mort (net_close_all),
		 * sa place peut déjà servir à l'écoute d'un autre process : sans ce
		 * contrôle, ce thread lui volait sa connexion et la donnait au pid mort
		 * (connexion perdue pour le vrai serveur, socket jamais rendu) : la cause
		 * de la boucle locale « une fois sur deux ». */
		if (!s->used || !sko_find(sd0, pid)) {
			nl_drop();
			return E_BADF;
		}
		if (s->state != TCP_LISTEN) { /* (fermée pendant l'attente) */
			nl_drop();
			return E_INVAL;
		}
		for (int i = 0; i < MAX_SOCK; i++) {
			struct sock *c = &sock_table[i];
			if (!c->used || c->parent != s || c->state < TCP_ESTAB)
				continue;
			long sd = sk_index(c); /* à l'appelant */
			sko_clear(sd);
			if (!sko_add(sd, pid)) {
				nl_drop(); /* (table pleine : il reste en attente) */
				return E_NOMEM;
			}
			c->parent = 0;
			c->owner = pid;
			nl_drop();
			return sd;
		}
		nl_drop();
		if (time_ms() >= end)
			return E_AGAIN;
		net_wait(gen, end);
	}
}

/* 7 send(a sd, b décalage, c octets) -> octets mis en file */
long op_send(u8 *m)
{
	u64 n = W64(m, MSG_C);
	u8 *data = data_ptr(m, W64(m, MSG_B), n);
	if (!data)
		return E_FAULT;
	struct sock *s = sk_get(W64(m, MSG_A), W32(m, MSG_PID));
	if (!s)
		return E_BADF;
	if (s->type == SOCK_UDP) { /* UDP connecté */
		if (!s->rip)
			return E_NOTCONN;
		return udp_sendto_user(data, n, s->rip, (u32)s->rport, s);
	}
	u64 done = 0; /* déjà mis en file */
	u64 end = time_ms() + 20000;
	u64 sd0 = W64(m, MSG_A), pid = W32(m, MSG_PID);
	while (done < n) {
		u32 gen = net_gen_now(); /* (avant de regarder : net_wait) */
		nl_take();
		/* (fermée pendant l'attente ; correction : ou rendue et reprise par un
		 * autre process, comme dans op_accept) */
		if (!s->used || !sko_find(sd0, pid) || (s->state != TCP_ESTAB && s->state != TCP_CLOSE_WAIT)) {
			nl_drop();
			if (done)
				return (long)done;
			return (s->flags & (F(SKF_RESET) | F(SKF_TIMEOUT))) ? E_CONNRESET : E_NOTCONN;
		}
		u64 h = s->txhead;
		u64 k = SK_RING - (h - s->txtail); /* place libre */
		if (k > n - done)
			k = n - done;
		if (!k) {
			nl_drop();
			if (time_ms() >= end)
				break;
			net_wait(gen, end);
			continue;
		}
		for (u64 i = 0; i < k; i++)
			s->txbuf[(h + i) & (SK_RING - 1)] = data[done + i];
		s->txhead = h + k;
		done += k;
		tcp_output(s);
		nl_drop();
	}
	return (long)done;
}

/* 8 recv(a sd, b décalage, c octets, d délai) -> octets (0 : fermée) ;
 * 11 recvfrom : idem -> c ip, d port de la source */
long op_recv(u8 *m)
{
	u32 src[2] = {0, 0}; /* source {ip, port} */
	u64 want = W64(m, MSG_C);
	long r;
	struct sock *s;
	u8 *buf = data_ptr(m, W64(m, MSG_B), want);
	if (!buf) {
		r = E_FAULT;
		goto out;
	}
	u64 d = wait_cap(W64(m, MSG_D));
	s = sk_get(W64(m, MSG_A), W32(m, MSG_PID));
	if (!s) {
		r = E_BADF;
		goto out;
	}
	u64 end = time_ms() + d; /* échéance */
	int waited = 0, gathered = 0;
	for (;;) {
		u32 gen = net_gen_now(); /* (avant de regarder : net_wait) */
		nl_take();
		/* (fermée pendant l'attente, par un autre thread du process) */
		if (!s->used || !sko_find(W64(m, MSG_A), W32(m, MSG_PID))) {
			nl_drop();
			r = E_BADF;
			goto out;
		}
		/* (un anneau partagé : ce qui y attend passe d'abord, lu ici par netd) */
		if (s->ring_cli && s->type == SOCK_TCP) {
			u64 k = ring_read(s, buf, want);
			if (k) {
				r = (long)k;
				nl_drop();
				goto out;
			}
		}
		u64 h = s->rxhead, t = s->rxtail;
		/* réveillé par les premières données d'un flux TCP (il a fallu attendre) et moins de 16 Ko (ou de ce qui est
		 * demandé) : 2 ms de plus, une fois, pour que la suite arrive ; une trame par réponse, le client refaisait
		 * un aller et retour pour 1460 octets (le débit d'un téléchargement : moitié moins). (L'ancien sommeil de
		 * 2 ms entre deux regards faisait ce regroupement sans le dire.) */
		if (h != t && waited && !gathered && s->type == SOCK_TCP && h - t < (want < 16384 ? want : 16384) &&
		    !(s->flags & F(SKF_RX_FIN)) && time_ms() + 2 < end) {
			gathered = 1;
			nl_drop();
			thread_sleep(2);
			continue;
		}
		if (h != t) {
			if (s->type == SOCK_UDP) { /* enregistrement UDP */
				u8 hd[8];
				sk_rx_read(s, hd, 8);
				u64 dl = *(u16 *)hd; /* longueur du datagramme */
				src[0] = *(u32 *)(hd + 4);
				src[1] = *(u16 *)(hd + 2);
				u64 k = dl <= want ? dl : want;
				sk_rx_read(s, buf, k);
				s->rxtail += dl - k; /* reste à jeter */
				r = (long)k;
			} else {
				u64 k = h - t; /* disponible */
				if (k > want)
					k = want;
				u64 room = sk_rx_free(s); /* fenêtre était-elle presque fermée ? */
				sk_rx_read(s, buf, k);
				if (room < s->mss << 1)
					tcp_send_ack(s); /* mise à jour de fenêtre */
				r = (long)k;
			}
			nl_drop();
			goto out;
		}
		if (s->type == SOCK_TCP) {
			u64 f = s->flags;
			if (f & F(SKF_RX_FIN)) {
				nl_drop();
				r = 0;
				goto out;
			}
			if (f & (F(SKF_RESET) | F(SKF_TIMEOUT))) {
				nl_drop();
				r = E_CONNRESET;
				goto out;
			}
			if (s->state < TCP_SYN_SENT) {
				nl_drop();
				r = E_NOTCONN;
				goto out;
			}
		}
		nl_drop();
		if (time_ms() >= end) {
			r = E_AGAIN;
			goto out;
		}
		net_wait(gen, end);
		waited = 1;
	}
out:
	if (r > 0 && W64(m, MSG_B) == (u64)-1) /* données du message : leur longueur */
		W32(m, MSG_LEN) = (u32)r;
	W64(m, MSG_C) = src[0]; /* source (recvfrom) */
	W64(m, MSG_D) = src[1];
	return r;
}

/* 18 poll(a nombre, b délai ms ; données : nombre x {u32 sd, u32 événements}) -> sockets prêts ; données : les
 * événements rendus à la place des demandés. Événements : 1 lecture (données, connexion à accepter, fin), 4 écriture
 * (place dans l'anneau d'émission), 8 erreur, 16 fin (FIN, RST), 32 descripteur inconnu. L'attente dort sur net_gen
 * (net_wait), comme recv : poll et select de libposix y attendent au lieu de sonder chaque socket toutes les
 * millisecondes (21/11/2026 : git-remote-https à 73 % de SYS_YIELD, smbd à 24 000 appels/s au repos, et leurs sondes
 * disputaient net_lock au thread de la pile). */
#define PL_IN 1
#define PL_OUT 4
#define PL_ERR 8
#define PL_HUP 16
#define PL_NVAL 32
#define PL_MAX (NET_INLINE / 8)

static u32 sk_ready(struct sock *s, u32 ev)
{
	u32 r = 0;
	if (s->type == SOCK_UDP) {
		if (s->rxhead != s->rxtail)
			r |= PL_IN;
		r |= PL_OUT;
	} else if (s->state == TCP_LISTEN) {
		for (int i = 0; i < MAX_SOCK; i++) {
			struct sock *c = &sock_table[i];
			if (c->used && c->parent == s && c->state >= TCP_ESTAB) {
				r |= PL_IN;
				break;
			}
		}
	} else {
		u64 f = s->flags;
		if (s->rxhead != s->rxtail || ring_avail(s))
			r |= PL_IN;
		if (f & F(SKF_RX_FIN))
			r |= PL_IN | PL_HUP;
		if (f & (F(SKF_RESET) | F(SKF_TIMEOUT) | F(SKF_REFUSED)))
			r |= PL_IN | PL_ERR | PL_HUP;
		if ((s->state == TCP_ESTAB || s->state == TCP_CLOSE_WAIT) && SK_RING - (s->txhead - s->txtail) > 0)
			r |= PL_OUT;
	}
	return r & (ev | PL_ERR | PL_HUP);
}

long op_poll(u8 *m)
{
	u64 n = W64(m, MSG_A);
	if (n > PL_MAX) /* (pas MSG_LEN : dispatch le remet à 0 avant l'opération) */
		return E_INVAL;
	u8 *d = m + MSG_DATA;
	u32 sd[PL_MAX], ev[PL_MAX];
	for (u64 i = 0; i < n; i++) {
		sd[i] = W32(d, i * 8);
		ev[i] = W32(d, i * 8 + 4);
	}
	u64 pid = W32(m, MSG_PID);
	u64 end = time_ms() + wait_cap(W64(m, MSG_B));
	long r;
	int waited = 0, gathered = 0;
	for (;;) {
		u32 gen = net_gen_now(); /* (avant de regarder : net_wait) */
		r = 0;
		int small = 0; /* (un flux TCP prêt avec moins de 16 Ko, sans fin) */
		nl_take();
		for (u64 i = 0; i < n; i++) {
			struct sock *s = sko_find(sd[i], pid) ? sk_get(sd[i], pid) : 0;
			u32 got = s && s->used ? sk_ready(s, ev[i]) : PL_NVAL;
			W32(d, i * 8 + 4) = got;
			if (got)
				r++;
			if ((got & PL_IN) && s->type == SOCK_TCP && s->state != TCP_LISTEN && !(got & PL_HUP) &&
			    s->rxhead - s->rxtail < 16384)
				small = 1;
		}
		nl_drop();
		/* réveillé par les premières trames d'un flux : 2 ms de plus, une fois, comme recv (sinon le client lit
		 * une trame par tour de poll : un aller et retour pour 1460 octets) */
		if (r && small && waited && !gathered && time_ms() + 2 < end) {
			gathered = 1;
			thread_sleep(2);
			continue;
		}
		if (r || time_ms() >= end)
			break;
		net_wait(gen, end);
		waited = 1;
	}
	for (u64 i = 0; i < n; i++)
		W32(d, i * 8) = sd[i];
	W32(m, MSG_LEN) = (u32)(n * 8);
	return r;
}

/* 9 close(a sd) : l'appelant le quitte ; fermé avec son dernier propriétaire */
long op_close(u8 *m)
{
	u64 sd = W64(m, MSG_A), pid = W32(m, MSG_PID);
	struct sock *s = sk_get(sd, pid);
	if (!s)
		return E_BADF;
	nl_take();
	if (s->ring_cli && s->ring_cli->pid == pid)
		ring_release(s, 1);
	if (s->used && sko_drop(sd, pid))
		sock_close_locked(s);
	nl_drop();
	net_wake(); /* (une attente sur ce socket, dans un autre thread du process) */
	return 0;
}

/* 24 shutdown(a sd) : FIN après ce qui reste en file ; le socket reste au
 * process et reçoit toujours (ni orphelin, ni délai en FIN-WAIT-2) */
long op_shutdown(u8 *m)
{
	struct sock *s = sk_get(W64(m, MSG_A), W32(m, MSG_PID));
	if (!s)
		return E_BADF;
	nl_take();
	/* (déjà en fermeture : rien de plus) */
	if (s->used && s->type == SOCK_TCP && (s->state == TCP_ESTAB || s->state == TCP_CLOSE_WAIT)) {
		s->flags |= F(SKF_FIN_PENDING);
		tcp_output(s);
	}
	nl_drop();
	net_wake();
	return 0;
}

/* un process terminé quitte ses sockets ; ceux dont il était le dernier
 * propriétaire sont fermés */
void net_close_all(u64 pid)
{
	if (!pid)
		return;
	nl_take();
	for (int i = 0; i < SKO_MAX; i++) {
		if (!sk_owners[i][0] || sk_owners[i][1] != (u32)pid)
			continue;
		u64 sd = sk_owners[i][0] - 1;
		if (sock_table[sd].ring_cli && sock_table[sd].ring_cli->pid == pid)
			ring_release(&sock_table[sd], 1);
		if (sko_drop(sd, pid) && sock_table[sd].used)
			sock_close_locked(&sock_table[sd]);
	}
	nl_drop();
	net_wake();
}

/* 10 sendto(a sd, b décalage, c octets, d ip | port << 32) (UDP) */
long op_sendto(u8 *m)
{
	u64 n = W64(m, MSG_C);
	u8 *data = data_ptr(m, W64(m, MSG_B), n);
	if (!data)
		return E_FAULT;
	u64 d = W64(m, MSG_D);
	struct sock *s = sk_get(W64(m, MSG_A), W32(m, MSG_PID));
	if (!s)
		return E_BADF;
	if (s->type != SOCK_UDP)
		return E_INVAL;
	return udp_sendto_user(data, n, (u32)d, (d >> 32) & 0xFFFF, s);
}

/* 12 resolve(données : nom) -> adresse IPv4 */
long op_resolve(u8 *m)
{
	char name[256];
	u64 i;
	for (i = 0; i < 255; i++)
		if (!(name[i] = (char)m[MSG_DATA + i]))
			break;
	name[i] = 0;
	return dns_resolve(name);
}

/* 13 ping(a ip, b délai ms) -> temps aller-retour en ms */
long op_ping(u8 *m)
{
	u64 d = wait_cap(W64(m, MSG_B));
	u32 ip = W32(m, MSG_A);
	if (!ip_is_local(ip) && !net_ip) /* (vers nous : pas besoin d'adresse) */
		return E_HOSTUNREACH;
	if (!arp_resolve_wait(ip))
		return E_HOSTUNREACH;
	mutex_lock(&ping_mutex); /* un ping à la fois */
	nl_take();
	ping_seq = (ping_seq + 1) & 0xFFFF;
	ping_id = 0x4152; /* « AR » */
	ping_reply_ms = 0;
	u8 *p = net_txbuf + PAYLOAD;
	p[0] = 8; /* demande d'écho, 32 octets de données */
	p[1] = 0;
	p[2] = p[3] = 0;
	st16be(p + 4, ping_id);
	st16be(p + 6, ping_seq);
	for (int i = 8; i < 40; i++)
		p[i] = (u8)(i + 0x20);
	st16be(p + 2, csum_fold(csum_add(p, 40, 0)));
	u64 t0 = time_ms(); /* instant d'envoi */
	ip_output(ip, PROTO_ICMP, 40);
	nl_drop();
	u64 end = t0 + d;
	long r;
	for (;;) {
		u64 rep = ping_reply_ms;
		if (rep) {
			r = (long)(rep - t0);
			break;
		}
		if (time_ms() >= end) {
			r = E_TIMEDOUT;
			break;
		}
		thread_sleep(1);
	}
	mutex_unlock(&ping_mutex);
	return r;
}

/* 14 info -> données : netinfo (32 o : ip, masque, passerelle, dns, mac, lien,
 * état DHCP), nom de l'interface active (+32, 8 o), son type (+40) */
long op_info(u8 *m)
{
	u8 *d = m + MSG_DATA;
	W32(m, MSG_LEN) = 48;
	nl_take();
	W32(d, 0) = net_ip;
	W32(d, 4) = net_mask;
	W32(d, 8) = net_gw;
	W32(d, 12) = net_dns;
	W32(d, 16) = (u32)netdev_mac;
	*(u16 *)(d + 20) = (u16)(netdev_mac >> 32);
	*(u16 *)(d + 22) = 0;
	W32(d, 24) = netdev_up;
	W32(d, 28) = dhcp_state;
	W64(d, 32) = 0;
	W64(d, 40) = 0;
	long a = if_active;
	if (a >= 0) {
		memcpy(d + 32, ifs[a].name, 8);
		W64(d, 40) = ifs[a].type;
	}
	nl_drop();
	return 0;
}

/* 15 ifaces -> a nombre ; données : NIF_ENT o par interface */
long op_ifaces(u8 *m)
{
	u8 *e = m + MSG_DATA;
	u64 n = 0;
	nl_take();
	for (int i = 0; i < IF_MAX; i++) {
		struct iface *f = &ifs[i];
		if (!f->pid)
			continue;
		memset(e, 0, NIF_ENT); /* entrée à zéro */
		memcpy(e + NIF_E_NAME, f->name, 8);
		W32(e, NIF_E_TYPE) = (u32)f->type;
		W32(e, NIF_E_LINK) = (u32)f->link;
		W32(e, NIF_E_PRIO) = (u32)f->prio;
		W32(e, NIF_E_SPEED) = (u32)f->speed;
		if (if_active == i) {
			W32(e, NIF_E_ACTIVE) = 1;
			W32(e, NIF_E_IP) = net_ip;
		}
		for (int k = 0; k < 31; k++) /* SSID (31 o au plus) */
			if (!(e[NIF_E_SSID + k] = (u8)f->ssid[k]))
				break;
		e += NIF_ENT;
		n++;
	}
	nl_drop();
	W64(m, MSG_A) = n;
	W32(m, MSG_LEN) = (u32)(n * NIF_ENT);
	return (long)n;
}

/* ---- anneaux de réception partagés (docs/etude/ANNEAUX.md, 22/11/2026)
 * Une socket TCP d'un process peut recevoir dans un anneau de son tampon (op 19) : netd y verse ce qui arrive
 * (ring_pump, thread de la pile), le process y lit sans message (NetKit : net_recv, net_poll) et dort sur le futex
 * de l'en-tête ; les pages du tampon sont partagées (VM_SHARED) : un futex y a la même clé chez netd et chez le
 * process. Les données attendent dans l'anneau interne de la socket quand celui du process est plein (NRF_STALL :
 * le process relance netd après lecture) ; la fenêtre TCP suit donc toujours la place réelle. */
#if NET_RINGS
static struct ring_hdr *ring_hdr_of(struct client *c, u64 slot)
{
	return (struct ring_hdr *)(c->adr + NET_RING_HDR + 64 * (slot + 1));
}

static u8 *ring_data_of(struct client *c, u64 slot)
{
	return c->adr + NET_RING_DATA + slot * NET_RING_SZ;
}

/* un changement : le futex de l'anneau et la sonnette du process (poll de plusieurs sockets) */
static void ring_signal(struct client *c, struct ring_hdr *h)
{
	volatile u32 *bell = (volatile u32 *)(c->adr + NET_RING_HDR);
	__atomic_fetch_add(&h->seq, 1, __ATOMIC_SEQ_CST);
	__atomic_fetch_add(bell, 1, __ATOMIC_SEQ_CST);
	/* (un appel seulement s'il y a un dormeur : NetKit compte avant de dormir et relit seq après) */
	if (__atomic_load_n(&h->waiters, __ATOMIC_SEQ_CST))
		korin_sys2(SYS_FUTEX_WAKE, (long)&h->seq, 0x7fffffff);
	if (__atomic_load_n(bell + 1, __ATOMIC_SEQ_CST))
		korin_sys2(SYS_FUTEX_WAKE, (long)bell, 0x7fffffff);
}

/* (net_lock) octets de l'anneau pas encore lus par le process */
u64 ring_avail(struct sock *s)
{
	if (!s->ring_cli)
		return 0;
	struct ring_hdr *h = ring_hdr_of(s->ring_cli, s->ring_slot);
	return __atomic_load_n(&h->head, __ATOMIC_ACQUIRE) - __atomic_load_n(&h->tail, __ATOMIC_ACQUIRE);
}

/* (net_lock) l'anneau interne -> celui du process, autant qu'il tient ; drapeaux ; réveil */
void ring_pump(struct sock *s)
{
	struct client *c = s->ring_cli;
	if (!c)
		return;
	struct ring_hdr *h = ring_hdr_of(c, s->ring_slot);
	u8 *d = ring_data_of(c, s->ring_slot);
	u64 head = h->head, tail = __atomic_load_n(&h->tail, __ATOMIC_ACQUIRE);
	u64 room = NET_RING_SZ - (head - tail);
	u64 have = s->rxhead - s->rxtail;
	u64 k = have < room ? have : room;
	u64 free_before = sk_rx_free(s);
	int changed = 0;
	for (u64 done = 0; done < k;) {
		u64 ri = (s->rxtail + done) & (SK_RING - 1), wi = (head + done) & (NET_RING_SZ - 1), n = k - done;
		if (n > SK_RING - ri)
			n = SK_RING - ri;
		if (n > NET_RING_SZ - wi)
			n = NET_RING_SZ - wi;
		memcpy(d + wi, s->rxbuf + ri, n);
		done += n;
	}
	if (k) {
		s->rxtail += k;
		__atomic_store_n(&h->head, head + k, __ATOMIC_RELEASE);
		changed = 1;
	}
	u32 fl = NRF_LIVE;
	if (s->rxhead != s->rxtail)
		fl |= NRF_STALL;
	else if (s->flags & F(SKF_RX_FIN))
		fl |= NRF_FIN;
	if (s->flags & (F(SKF_RESET) | F(SKF_TIMEOUT)))
		fl |= NRF_ERR;
	if (fl != h->flags) {
		__atomic_store_n(&h->flags, fl, __ATOMIC_RELEASE);
		changed = 1;
	}
	if (k && free_before < (s->mss << 1))
		tcp_send_ack(s); /* (la fenêtre était presque fermée : mise à jour) */
	if (changed)
		ring_signal(c, h);
}

/* (net_lock, thread de la pile) toutes les sockets à anneau */
void rings_pump_all(void)
{
	for (int i = 0; i < MAX_SOCK; i++)
		if (sock_table[i].used && sock_table[i].ring_cli)
			ring_pump(&sock_table[i]);
}

/* (net_lock) netd lit lui-même l'anneau (un recv par message : un fils après fork, un NetKit sans anneau) */
u64 ring_read(struct sock *s, u8 *dst, u64 max)
{
	struct client *c = s->ring_cli;
	if (!c)
		return 0;
	struct ring_hdr *h = ring_hdr_of(c, s->ring_slot);
	u8 *d = ring_data_of(c, s->ring_slot);
	u64 tail = __atomic_load_n(&h->tail, __ATOMIC_ACQUIRE), head = __atomic_load_n(&h->head, __ATOMIC_ACQUIRE);
	u64 k = head - tail < max ? head - tail : max;
	for (u64 done = 0; done < k;) {
		u64 ri = (tail + done) & (NET_RING_SZ - 1), n = k - done;
		if (n > NET_RING_SZ - ri)
			n = NET_RING_SZ - ri;
		memcpy(dst + done, d + ri, n);
		done += n;
	}
	if (k) {
		__atomic_store_n(&h->tail, tail + k, __ATOMIC_RELEASE);
		ring_pump(s);
	}
	return k;
}

/* (net_lock) la socket quitte son anneau ; keep : les octets pas lus repassent devant l'anneau interne (un autre
 * process tient encore la socket : un fils après fork) */
void ring_release(struct sock *s, int keep)
{
	struct client *c = s->ring_cli;
	if (!c)
		return;
	struct ring_hdr *h = ring_hdr_of(c, s->ring_slot);
	u8 *d = ring_data_of(c, s->ring_slot);
	u64 tail = h->tail, k = h->head - tail;
	if (keep && k && s->rxbuf) {
		if (sk_rx_free(s) >= k) {
			s->rxtail -= k;
			for (u64 done = 0; done < k;) {
				u64 ri = (tail + done) & (NET_RING_SZ - 1), wi = (s->rxtail + done) & (SK_RING - 1), n = k - done;
				if (n > NET_RING_SZ - ri)
					n = NET_RING_SZ - ri;
				if (n > SK_RING - wi)
					n = SK_RING - wi;
				memcpy(s->rxbuf + wi, d + ri, n);
				done += n;
			}
		} else
			ak_puts("netd : anneau rendu sans place dans la socket : données perdues\n");
	}
	__atomic_store_n(&h->flags, 0, __ATOMIC_RELEASE);
	c->ring_sk[s->ring_slot] = 0;
	s->ring_cli = 0;
	s->ring_slot = 0;
	ring_signal(c, h);
}

/* 19 ring(a sd, b 0 : un anneau pour cette socket -> son numéro ; 1 : relancer après lecture (NRF_STALL)) */
long op_ring(u8 *m)
{
	u64 sd = W64(m, MSG_A), pid = W32(m, MSG_PID);
	struct sock *s = sk_get(sd, pid);
	if (!s)
		return E_BADF;
	nl_take();
	long r = E_INVAL;
	struct client *c = 0;
	for (int i = 0; i < CLI_MAX; i++)
		if (clis[i].pid == pid)
			c = &clis[i];
	if (W64(m, MSG_B) == 1) {
		if (s->ring_cli && s->ring_cli == c)
			ring_pump(s);
		nl_drop();
		return 0;
	}
	int owners = 0;
	for (int i = 0; i < SKO_MAX; i++)
		owners += sk_owners[i][0] == (u32)sd + 1;
	if (s->ring_cli)
		r = s->ring_cli == c ? (long)s->ring_slot : E_INVAL;
	else if (c && c->rings && s->used && s->type == SOCK_TCP && s->state < TCP_ESTAB && s->state != TCP_LISTEN)
		r = E_AGAIN; /* (pas encore connectée : redemander) */
	else if (c && c->rings && s->used && s->type == SOCK_TCP && s->state >= TCP_ESTAB && owners == 1) {
		for (u64 j = 0; j < c->rings; j++) {
			if (c->ring_sk[j])
				continue;
			struct ring_hdr *h = ring_hdr_of(c, j);
			h->head = h->tail = 0;
			h->sd = (u32)sd;
			c->ring_sk[j] = s;
			s->ring_cli = c;
			s->ring_slot = j;
			ring_pump(s); /* (ce qui attendait déjà : dans l'anneau ; drapeaux) */
			r = (long)j;
			break;
		}
		if (r < 0)
			r = E_NOSPC;
	}
	nl_drop();
	return r;
}
#else
u64 ring_avail(struct sock *s) { (void)s; return 0; }
void ring_pump(struct sock *s) { (void)s; }
void rings_pump_all(void) {}
u64 ring_read(struct sock *s, u8 *dst, u64 max) { (void)s; (void)dst; (void)max; return 0; }
void ring_release(struct sock *s, int keep) { (void)s; (void)keep; }
long op_ring(u8 *m) { (void)m; return E_INVAL; }
#endif
