/* BusKit : le bus d'événements du système (docs/BUS.md, docs/KITS.md), en C
 * commun au Pi et au PC ; remplace buskit.asm, même table (buskit.abi), mêmes
 * résultats.
 *
 * Chaque appel est une requête au service « bus » (busd, src/inc/bus.inc).
 * Les événements WEV_BUS arrivent dans la file d'événements du programme :
 * ui_event les rend avec ceux des fenêtres ; sans UIKit, bus_wait.
 * busd relancé (surveillé par init2) : le canal est rouvert une fois, mais les
 * abonnements sont à refaire. Variables par process (données du kit). */
#include "../common/korin_kit.h"

/* src/inc/bus.inc */
#define BUS_OP_PUBLISH 1
#define BUS_OP_SUBSCRIBE 2
#define BUS_OP_UNSUBSCRIBE 3
#define BUS_OP_GET 4
#define BUS_OP_LIST 5
#define BUS_DATA 48

/* buskit.inc */
#define BUS_NAME_MAX 64
#define BUS_VALUE_MAX 256
#define BUS_ENOENT -2
#define BUS_ENODEV -19
#define BUS_EINVAL -22

static long b_chan;                 /* canal + 1 (0 : pas ouvert) */
static uint8_t b_msg[SVC_MSG] __attribute__((aligned(8)));

/* canal du service, -1 : busd absent */
static long chan(void)
{
	if (b_chan)
		return b_chan - 1;
	long c = korin_sys2(SYS_SVC_OPEN, "bus", 0);
	if (c < 0)
		return -1;
	b_chan = c + 1;
	return c;
}

/* requête (données déjà dans b_msg) -> résultat de busd ; BUS_ENODEV : busd
 * absent. SVC_CALL rend le résultat de busd, qui peut être négatif : c'est une
 * erreur du canal seulement si le message n'a pas reçu de réponse (+0 : l'op,
 * toujours > 0). Un canal mort est rouvert une fois. */
static long svc_request(long op, long a, long b)
{
	for (int retried = 0;; retried = 1) {
		uint64_t *m = (uint64_t *)b_msg;
		m[0] = (uint64_t)op;
		m[1] = (uint64_t)a;
		m[2] = (uint64_t)b;
		long c = chan();
		if (c < 0)
			return BUS_ENODEV;
		long r = korin_sys2(SYS_SVC_CALL, c, b_msg);
		if (r >= 0)
			return (long)m[0];
		if (r == (long)m[0])
			return r; /* réponse de busd */
		b_chan = 0;
		if (retried)
			return BUS_ENODEV;
	}
}

/* nom ou motif dans les données de b_msg (0 compris) -> longueur, -1 si faux
 * (busd vérifie le reste) */
static long put_name(const char *name)
{
	char *d = (char *)b_msg + BUS_DATA;
	for (long i = 0; i < BUS_NAME_MAX; i++) {
		d[i] = name[i];
		if (!name[i])
			return i ? i : -1;
	}
	return -1;
}

long bus_available(void)
{
	return chan() >= 0;
}

long bus_publish(const char *name, const void *value, long len, long flags)
{
	if (len == -1)
		len = (long)strlen(value);
	if ((unsigned long)len > BUS_VALUE_MAX)
		return BUS_EINVAL;
	long n = put_name(name);
	if (n < 0)
		return BUS_EINVAL;
	memcpy(b_msg + BUS_DATA + n + 1, value, (size_t)len); /* la valeur après le 0 du nom */
	return svc_request(BUS_OP_PUBLISH, flags, len);
}

long bus_subscribe(const char *pattern, long flags)
{
	if (put_name(pattern) < 0)
		return BUS_EINVAL;
	return svc_request(BUS_OP_SUBSCRIBE, flags, 0);
}

long bus_unsubscribe(long sub)
{
	long r = svc_request(BUS_OP_UNSUBSCRIBE, sub, 0);
	return r < 0 ? r : 0;
}

static long get_common(long topic, void *buf, unsigned long size)
{
	long r = svc_request(BUS_OP_GET, topic, 0);
	if (r < 0)
		return r;
	memcpy(buf, b_msg + BUS_DATA, (unsigned long)r < size ? (size_t)r : size);
	return r;
}

long bus_get(const char *name, void *buf, long size)
{
	if (put_name(name) < 0)
		return BUS_EINVAL;
	return get_common(0, buf, (unsigned long)size);
}

long bus_get_id(long topic, void *buf, long size)
{
	if (!topic)
		return BUS_ENOENT; /* (0 : le nom, pas ici) */
	return get_common(topic, buf, (unsigned long)size);
}

long bus_list(long from, char *buf)
{
	long r = svc_request(BUS_OP_LIST, from, 0);
	if (r <= 0) {
		buf[0] = 0;
		return 0;
	}
	memcpy(buf, b_msg + BUS_DATA, BUS_NAME_MAX);
	return r;
}

long bus_name(long topic, char *buf)
{
	if (topic && bus_list(topic - 1, buf) == topic)
		return 0;
	buf[0] = 0;
	return BUS_ENOENT;
}

/* la file d'événements du programme (comme ui_event, sans UIKit) */
long bus_wait(void *event, long ms)
{
	return korin_sys2(SYS_EV_WAIT, event, ms);
}
