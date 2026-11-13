/* iso.c : transferts isochrones d'usbd en C (cartes son et casques USB, docs/SON.md 5.1),
 * repris de la partie xHCI d'iso.inc (le DWC2 du Pi reste dans usbd.asm).
 *
 * Un tuyau isochrone (USB_OP_ISO_OPEN) a un anneau partagé avec son pilote (UI_* de
 * src/inc/usb.inc) : OUT, le pilote y écrit le PCM et usbd en fait un paquet par
 * milliseconde (octets par ms en 16.16, arrondis à la trame, au plus la taille max.) ;
 * IN, usbd y écrit ce qu'il reçoit. Chaque tuyau garde ISO_XQ TRB Isoch en file
 * (« au plus tôt » : SIA), un par ms, dans ses cases de 1 Ko de la zone du xHCI
 * (xh_iso_*) ; chaque fin rend sa case (dans l'ordre : une fin qui en saute
 * d'autres, Missed Service, les compte toutes) et la boucle d'usbd en remet.
 * usbd est d'un seul fil : pas de verrou (l'asm en avait un pour le thread DWC2).
 * Limites, comme l'asm : pas de retour de synchronisation (sound corrige), un
 * paquet par ms même en haute vitesse. */
#include "usbd.h"

#define UI_HEAD 0
#define UI_TAIL 8
#define UI_LOST 16    /* octets manquants (OUT) ou perdus (IN) */
#define UI_PACKETS 24 /* paquets faits */
#define UI_STATE 32   /* 1 : périphérique retiré (tuyau fermé) */
#define UI_DATA 4096
#define UI_BYTES 32768

#define ISO_XQ 12    /* TRB en file : 12 ms */
#define ISO_SLOTS 16 /* cases de 1 Ko par tuyau (xhci.c) */
#define CC_SUCCESS 1
#define CC_SHORT 13

struct iso {
	int active, pid;
	long dev, handle; /* numéro usbd, poignée du contrôleur */
	unsigned ep, mps, frame;
	uint64_t bpms, acc; /* octets par ms (16.16), cumul */
	long buf;           /* anneau partagé : numéro, adresse */
	uint8_t *ring;
	unsigned outst, push, done;
	uint64_t trb[ISO_SLOTS]; /* adresse du TRB de chaque case en file */
};

static struct iso isos[XH_ISO_MAX];

#define U64(r, off) (*(volatile uint64_t *)((r) + (off)))

/* octets du prochain paquet OUT (cumul arrondi à la trame, au plus la taille max.) */
static unsigned packet_bytes(struct iso *p)
{
	p->acc += p->bpms;
	uint64_t n = (p->acc >> 16) / p->frame * p->frame;
	if (n > p->mps)
		n = p->mps / p->frame * p->frame;
	p->acc -= n << 16;
	return (unsigned)n;
}

/* depuis l'anneau OUT (silence s'il manque, compté dans UI_LOST) */
static void take(struct iso *p, uint8_t *dst, unsigned n)
{
	uint8_t *r = p->ring;
	uint64_t head = __atomic_load_n((uint64_t *)(r + UI_HEAD), __ATOMIC_ACQUIRE), tail = U64(r, UI_TAIL);
	uint64_t avail = head - tail, k = avail < n ? avail : n;
	for (uint64_t i = 0; i < k; i++, tail++)
		dst[i] = r[UI_DATA + (tail & (UI_BYTES - 1))];
	if (k < n) {
		memset(dst + k, 0, n - k);
		U64(r, UI_LOST) += n - k;
	}
	__atomic_store_n((uint64_t *)(r + UI_TAIL), tail, __ATOMIC_RELEASE);
}

/* dans l'anneau IN (le reste perdu) */
static void put(struct iso *p, const uint8_t *src, unsigned n)
{
	uint8_t *r = p->ring;
	uint64_t head = U64(r, UI_HEAD), tail = __atomic_load_n((uint64_t *)(r + UI_TAIL), __ATOMIC_ACQUIRE);
	uint64_t room = UI_BYTES - (head - tail), k = room < n ? room : n;
	for (uint64_t i = 0; i < k; i++, head++)
		r[UI_DATA + (head & (UI_BYTES - 1))] = src[i];
	if (k < n)
		U64(r, UI_LOST) += n - k;
	__atomic_store_n((uint64_t *)(r + UI_HEAD), head, __ATOMIC_RELEASE);
}

/* rate : octets par ms (16.16) | octets par trame << 32 -> numéro du tuyau, *buf =
 * l'anneau (pour le pilote) */
long iso_open(int pid, long dev, long handle, unsigned ep, unsigned mps, uint64_t rate, long *buf)
{
	mps &= 0x7FF;
	if (!mps || mps > 1023)
		return E_INVAL;
	int i;
	for (i = 0; i < XH_ISO_MAX && isos[i].active; i++)
		;
	if (i == XH_ISO_MAX)
		return E_NOMEM;
	struct iso *p = &isos[i];
	long num = korin_sys1(SYS_BUF_CREATE, UI_DATA + UI_BYTES);
	if (num < 0)
		return num;
	long va = korin_sys1(SYS_BUF_MAP, num);
	long r = va;
	if (va >= 0) {
		memset((void *)va, 0, UI_DATA);
		r = korin_sys2(SYS_BUF_GRANT, num, pid);
	}
	if (r >= 0)
		r = xh_iso_open(handle, (ep & 0x80) | (ep & 15), mps, i);
	if (r < 0) {
		korin_sys1(SYS_BUF_FREE, num);
		return r;
	}
	memset(p, 0, sizeof *p);
	p->pid = pid;
	p->dev = dev;
	p->handle = handle;
	p->ep = (ep & 0x80) | (ep & 15);
	p->mps = mps;
	p->bpms = (uint32_t)rate;
	p->frame = (unsigned)(rate >> 32);
	if (p->frame < 1)
		p->frame = 4;
	p->buf = num;
	p->ring = (uint8_t *)va;
	p->active = 1;
	*buf = num;
	say("usbd : isochrone %d : périphérique %ld, extrémité %02x %s, %u o max., %u,%02u o/ms\n", i, dev, p->ep,
	    (p->ep & 0x80) ? "IN" : "OUT", mps, (unsigned)(p->bpms >> 16),
	    (unsigned)(((p->bpms & 0xFFFF) * 100) >> 16));
	return i;
}

static void close_entry(struct iso *p, int i)
{
	if (!p->active)
		return;
	p->active = 0;
	xh_iso_close(p->handle, i);
	say("usbd : isochrone %d fermé (%lu paquets, %lu octets manquants)\n", i,
	    (unsigned long)U64(p->ring, UI_PACKETS), (unsigned long)U64(p->ring, UI_LOST));
	korin_sys1(SYS_BUF_FREE, p->buf);
}

long iso_close(int pid, long pipe)
{
	if (pipe < 0 || pipe >= XH_ISO_MAX || !isos[pipe].active || isos[pipe].pid != pid)
		return E_INVAL;
	close_entry(&isos[pipe], (int)pipe);
	return 0;
}

void iso_close_pid(int pid)
{
	for (int i = 0; i < XH_ISO_MAX; i++)
		if (isos[i].active && isos[i].pid == pid)
			close_entry(&isos[i], i);
}

/* périphérique retiré : UI_STATE = 1 (le pilote voit), tuyaux fermés */
void iso_close_dev(long dev)
{
	for (int i = 0; i < XH_ISO_MAX; i++)
		if (isos[i].active && isos[i].dev == dev) {
			U64(isos[i].ring, UI_STATE) = 1;
			close_entry(&isos[i], i);
		}
}

void iso_service(void)
{
	for (int i = 0; i < XH_ISO_MAX; i++) {
		struct iso *p = &isos[i];
		if (!p->active)
			continue;
		unsigned added = 0;
		while (p->outst < ISO_XQ) {
			unsigned k = p->push;
			unsigned len = p->mps;
			if (!(p->ep & 0x80)) {
				len = packet_bytes(p);
				take(p, xh_iso_buf(p->handle, i, k), len);
			}
			uint64_t a = xh_iso_push(p->handle, i, k, len);
			if (!a)
				break;
			p->trb[k] = a;
			p->push = (k + 1) % ISO_SLOTS;
			p->outst++;
			added++;
		}
		if (added)
			xh_iso_bell(p->handle, i);
	}
}

void usbd_iso_event(int i, uint64_t trb, unsigned code, unsigned left)
{
	if (i < 0 || i >= XH_ISO_MAX || !isos[i].active || !code)
		return;
	struct iso *p = &isos[i];
	unsigned n, k = p->done;
	for (n = 0; n < p->outst && p->trb[k] != trb; n++)
		k = (k + 1) % ISO_SLOTS;
	if (n == p->outst)
		return; /* (pas un des nôtres) */
	U64(p->ring, UI_PACKETS) += n + 1; /* (celles sautées comptent) */
	if ((p->ep & 0x80) && (code == CC_SUCCESS || code == CC_SHORT) && left < p->mps)
		put(p, xh_iso_buf(p->handle, i, k), p->mps - left);
	p->done = (k + 1) % ISO_SLOTS;
	p->outst -= n + 1;
}
