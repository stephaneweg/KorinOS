/* busd - le bus d'événements du système (docs/BUS.md), repris de busd.asm ;
 * lancé par init2 avant hwmanager. Service « bus » (src/inc/bus.inc), client :
 * BusKit. Commun au Pi et au PC.
 *  * des sujets nommés (« korin.theme », « korin.usb.device »...) : un programme
 *    publie une valeur (BUS_VALUE_MAX octets au plus) sous un nom ;
 *  * des abonnements : un nom exact, ou un préfixe suivi de « * »
 *    (« korin.usb.* », « * » : tout). À chaque publication, chaque abonnement
 *    qui correspond reçoit WEV_BUS dans la file d'événements de son programme
 *    (celle d'ui_event : SVF_POST permet à busd le WS_POST du serveur
 *    graphique), avec les BUS_INLINE premiers octets de la valeur ;
 *  * la dernière valeur d'un sujet reste lisible (BUS_OP_GET) ; une valeur
 *    BUS_RETAIN est en plus rendue à un abonné BUS_CURRENT dès son arrivée ;
 *  * fin d'un programme (SVF_ENDS) : ses abonnements sont retirés.
 * File d'événements pleine (64) : l'événement est perdu pour ce programme ;
 * la valeur, elle, reste lisible. */
#include "../svclib/svclib.h"

/* src/inc/bus.inc, src/kits/buskit/buskit.inc */
#define BUS_OP_PUBLISH 1
#define BUS_OP_SUBSCRIBE 2
#define BUS_OP_UNSUBSCRIBE 3
#define BUS_OP_GET 4
#define BUS_OP_LIST 5
#define BUS_DATA 48
#define BUS_DATA_MAX 464
#define WEV_BUS 13
#define BEV_SUB 4
#define BEV_TOPIC 8
#define BEV_LEN 12
#define BEV_DATA 16
#define BUS_INLINE 12
#define BUS_NAME_MAX 64
#define BUS_VALUE_MAX 256
#define BUS_RETAIN 1
#define BUS_DELETE 2
#define BUS_CURRENT 1
#define BUS_ENOENT -2
#define BUS_ENOMEM -12
#define BUS_EINVAL -22

#define TOPIC_MAX 128
#define SUB_MAX 256

struct topic {
	u64 used;  /* 1 : place prise */
	u64 flags; /* BUS_RETAIN */
	i64 len;   /* octets de la valeur (-1 : en train d'être retiré) */
	u64 pid;   /* dernier éditeur */
	char name[BUS_NAME_MAX];
	u8 value[BUS_VALUE_MAX];
};

struct sub {
	u64 pid;  /* abonné (0 : place libre) */
	u64 id;   /* numéro rendu au programme */
	i64 plen; /* préfixe : octets avant « * » (-1 : nom exact) */
	char pat[BUS_NAME_MAX];
};

static u8 msg[512] __attribute__((aligned(16)));
static u8 ev[32] __attribute__((aligned(8)));
static u64 next_id = 1;
static struct topic topics[TOPIC_MAX];
static struct sub subs[SUB_MAX];

/* nom (0 final), pattern = 1 si un « * » final est permis -> longueur
 * (1..BUS_NAME_MAX-1), -1 si faux (vide, trop long, espace ou caractère de
 * contrôle, « * » ailleurs qu'à la fin d'un motif) */
static long name_check(const char *s, int pattern)
{
	long n = 0;
	for (;;) {
		u8 c = (u8)s[n];
		if (!c)
			break;
		if (c <= ' ')
			return -1;
		if (c == '*' && (!pattern || s[n + 1]))
			return -1;
		n++;
		if (n >= BUS_NAME_MAX)
			return -1;
	}
	return n ? n : -1;
}

static long topic_find(const char *name)
{
	for (long i = 0; i < TOPIC_MAX; i++)
		if (topics[i].used && str_eq(topics[i].name, name))
			return i;
	return -1;
}

static long topic_alloc(void)
{
	for (long i = 0; i < TOPIC_MAX; i++)
		if (!topics[i].used)
			return i;
	return -1;
}

static int sub_match(const struct sub *s, const char *name)
{
	const char *p = s->pat;
	i64 n = s->plen;
	if (n >= 0) { /* préfixe */
		for (; n; n--)
			if (*p++ != *name++)
				return 0;
		return 1;
	}
	for (;;) { /* nom exact */
		char a = *p++, b = *name++;
		if (a != b)
			return 0;
		if (!a)
			return 1;
	}
}

/* t = place du sujet, one = place de l'abonnement + 1 (0 : tous ceux qui
 * correspondent) : WEV_BUS à leurs programmes -> événements déposés */
static long notify(long t, long one)
{
	struct topic *tp = &topics[t];
	*(u32 *)ev = WEV_BUS; /* l'événement, le même pour tous */
	*(u32 *)(ev + BEV_TOPIC) = (u32)(t + 1);
	*(u32 *)(ev + BEV_LEN) = (u32)tp->len; /* (-1 : BUS_DELETED) */
	memset(ev + 16, 0, 16);
	if (tp->len >= 0)
		mem_move(ev + BEV_DATA, tp->value, (u64)tp->len < BUS_INLINE ? (u64)tp->len : BUS_INLINE);
	long n = 0;
	for (long i = one ? one - 1 : 0; i < SUB_MAX; i++) {
		struct sub *s = &subs[i];
		if (s->pid && sub_match(s, tp->name)) {
			*(u32 *)(ev + BEV_SUB) = (u32)s->id;
			if (korin_sys2(SYS_WS_POST, s->pid, ev) >= 0) /* (file pleine, programme parti) */
				n++;
		}
		if (one)
			break;
	}
	return n;
}

/* pid, id (0 : tous ceux du pid) : abonnements retirés -> combien */
static long unsubscribe(u64 pid, u64 id)
{
	long n = 0;
	for (long i = 0; i < SUB_MAX; i++)
		if (subs[i].pid == pid && (!id || subs[i].id == id)) {
			subs[i].pid = 0;
			n++;
		}
	return n;
}

static long op_publish(void)
{
	char *name = (char *)msg + BUS_DATA;
	long len = name_check(name, 0);
	if (len < 0)
		return BUS_EINVAL;
	u64 vlen = M64(msg, 16);
	if (vlen > BUS_VALUE_MAX || (u64)len + 1 + vlen > BUS_DATA_MAX)
		return BUS_EINVAL;
	long t = topic_find(name);
	if (M64(msg, 8) & BUS_DELETE) {
		if (t < 0)
			return BUS_ENOENT;
		topics[t].len = -1;
		long n = notify(t, 0);
		topics[t].used = 0;
		return n;
	}
	if (t < 0) {
		t = topic_alloc();
		if (t < 0)
			return BUS_ENOMEM;
		topics[t].used = 1;
		mem_move(topics[t].name, name, (u64)len + 1);
	}
	struct topic *tp = &topics[t];
	tp->flags = M64(msg, 8) & BUS_RETAIN;
	tp->len = (i64)vlen;
	tp->pid = M32(msg, 44);
	mem_move(tp->value, name + len + 1, vlen); /* la valeur suit le 0 du nom */
	return notify(t, 0);
}

static long op_subscribe(void)
{
	char *pat = (char *)msg + BUS_DATA;
	long len = name_check(pat, 1);
	if (len < 0)
		return BUS_EINVAL;
	long i;
	for (i = 0; i < SUB_MAX; i++)
		if (!subs[i].pid)
			break;
	if (i == SUB_MAX)
		return BUS_ENOMEM;
	struct sub *s = &subs[i];
	s->pid = M32(msg, 44);
	s->id = next_id; /* numéros : 1, 2... (31 bits) */
	next_id = (next_id + 1) & 0x7FFFFFFF;
	if (!next_id)
		next_id = 1;
	s->plen = pat[len - 1] == '*' ? len - 1 : -1;
	mem_move(s->pat, pat, (u64)len + 1);
	if (M64(msg, 8) & BUS_CURRENT) /* les valeurs gardées */
		for (long t = 0; t < TOPIC_MAX; t++)
			if (topics[t].used && (topics[t].flags & BUS_RETAIN))
				notify(t, i + 1);
	return (long)s->id;
}

static long op_get(void)
{
	u64 a = M64(msg, 8);
	long t;
	if (a) {
		if (a - 1 >= TOPIC_MAX)
			return BUS_ENOENT;
		t = (long)(a - 1);
	} else {
		msg[BUS_DATA + BUS_NAME_MAX - 1] = 0;
		t = topic_find((char *)msg + BUS_DATA);
		if (t < 0)
			return BUS_ENOENT;
	}
	struct topic *tp = &topics[t];
	if (!tp->used)
		return BUS_ENOENT;
	M64(msg, 8) = (u64)t + 1;
	M64(msg, 16) = tp->flags;
	mem_move(msg + BUS_DATA, tp->value, (u64)tp->len);
	return tp->len;
}

static long op_list(void)
{
	for (u64 t = M64(msg, 8); t < TOPIC_MAX; t++) { /* (sujet a = place a - 1 : on part de a) */
		struct topic *tp = &topics[t];
		if (!tp->used)
			continue;
		M64(msg, 16) = (u64)tp->len;
		M64(msg, 24) = tp->flags;
		M64(msg, 32) = tp->pid;
		mem_move(msg + BUS_DATA, tp->name, BUS_NAME_MAX);
		return (long)t + 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long chan = korin_sys2(SYS_SVC_REGISTER, "bus", SVF_ENDS | SVF_POST);
	if (chan < 0)
		return 1;
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, chan, msg, 60000);
		if (req < 0)
			return 1;
		if (!req)
			continue;
		long r;
		switch (M64(msg, 0)) {
		case SVC_OP_END: /* (avis, sans réponse) a = pid */
			if (M64(msg, 8))
				unsubscribe(M64(msg, 8), 0);
			continue;
		case SVC_OP_SIGNAL:
			continue; /* (sans réponse) */
		case BUS_OP_PUBLISH:
			r = op_publish();
			break;
		case BUS_OP_SUBSCRIBE:
			r = op_subscribe();
			break;
		case BUS_OP_UNSUBSCRIBE: {
			u64 id = M64(msg, 8);
			r = unsubscribe(M32(msg, 44), id);
			if (id) /* (tous : combien, même 0) */
				r = r ? 0 : BUS_ENOENT;
			break;
		}
		case BUS_OP_GET:
			r = op_get();
			break;
		case BUS_OP_LIST:
			r = op_list();
			break;
		default:
			r = BUS_EINVAL;
		}
		M64(msg, 0) = (u64)r;
		korin_sys2(SYS_SVC_REPLY, req, msg);
	}
}
