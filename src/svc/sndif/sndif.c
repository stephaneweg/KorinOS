/* sndif/sndif.c - côté pilote audio (voir sndif.h), repris pas à pas de
 * src/svc/inc/sndif.inc. */
#include "sndif.h"

struct sif {
	u64 type, rate, dmareq;
	const char *name;
	long id;
	u8 *ring; /* mappé (0 : pas inscrit) */
	long dma; /* canal donné (-1 : aucun) */
	long buf; /* numéro du tampon de l'anneau */
};

static long sif_chan;
static u64 sif_next;
static struct sif sif_out = {.dma = -1}, sif_in = {.dma = -1};
static u64 sif_msg[64] __attribute__((aligned(16)));

#define R64(r, off) (*(volatile u64 *)((r) + (off)))

/* bloc, attente (ms) -> numéro, < 0 si échec */
static long sif_register(struct sif *b, long wait)
{
	b->ring = 0;
	long ch = korin_sys2(SYS_SVC_OPEN, "sound", wait);
	if (ch < 0)
		return ch;
	sif_chan = ch;
	u8 *m = (u8 *)sif_msg;
	sif_msg[0] = b == &sif_in ? SND_OP_DRV_INPUT : SND_OP_DRV_OUTPUT;
	sif_msg[1] = b->type;
	sif_msg[2] = b->rate;
	sif_msg[3] = b->dma >= 0 && b->dmareq ? (u64)b->dma + 0x100 : b->dmareq; /* réinscription : le même canal */
	int i = 0; /* nom (31 caractères au plus) */
	for (; i < 31; i++)
		if (!(m[MSG_DATA + i] = (u8)b->name[i]))
			break;
	if (i == 31)
		m[MSG_DATA + 31] = 0;
	*(u32 *)(m + MSG_LEN) = 32;
	long r = korin_sys2(SYS_SVC_CALL, sif_chan, sif_msg);
	if (r < 0)
		return r;
	b->id = r;
	b->dma = (long)sif_msg[3];
	b->buf = (long)sif_msg[2]; /* l'anneau */
	r = korin_sys1(SYS_BUF_MAP, b->buf);
	if (r < 0)
		return r;
	b->ring = (u8 *)r;
	return b->id;
}

static long sif_setup(struct sif *b, u64 type, u64 rate, u64 dma, const char *name)
{
	b->type = type;
	b->rate = rate;
	b->dmareq = dma;
	b->name = name;
	return sif_register(b, 60000); /* (sound peut démarrer après le pilote) */
}

long sndif_output(u64 type, u64 rate, u64 dma, const char *name)
{
	return sif_setup(&sif_out, type, rate, dma, name);
}

long sndif_input(u64 type, u64 rate, u64 dma, const char *name)
{
	return sif_setup(&sif_in, type, rate, dma, name);
}

u64 sndif_read(u32 *dst, u64 n)
{
	u8 *r = sif_out.ring;
	u64 k = 0;
	if (r) {
		u64 head = __atomic_load_n((u64 *)(r + SR_HEAD), __ATOMIC_ACQUIRE), tail = R64(r, SR_TAIL);
		u64 avail = (head - tail) >> 2;
		k = avail < n ? avail : n; /* trames prises */
		for (u64 i = 0; i < k; i++, tail += 4)
			dst[i] = *(volatile u32 *)(r + SR_DATA + (tail & (SR_OUT_BYTES - 1)));
		__atomic_store_n((u64 *)(r + SR_TAIL), tail, __ATOMIC_RELEASE);
		R64(r, SR_PLAYED) = R64(r, SR_PLAYED) + k;
		if (n - k)
			R64(r, SR_LOST) = R64(r, SR_LOST) + (n - k);
	}
	for (u64 i = k; i < n; i++) /* (sans anneau : du silence, rien compté) */
		dst[i] = 0;
	return k;
}

u64 sndif_write(const u32 *src, u64 n)
{
	u8 *r = sif_in.ring;
	if (!r)
		return 0;
	u64 head = R64(r, SR_HEAD), tail = __atomic_load_n((u64 *)(r + SR_TAIL), __ATOMIC_ACQUIRE);
	u64 room = (SR_IN_BYTES - (head - tail)) >> 2;
	u64 k = room < n ? room : n;
	for (u64 i = 0; i < k; i++, head += 4)
		*(volatile u32 *)(r + SR_DATA + (head & (SR_IN_BYTES - 1))) = src[i];
	__atomic_store_n((u64 *)(r + SR_HEAD), head, __ATOMIC_RELEASE);
	if (n - k)
		R64(r, SR_LOST) = R64(r, SR_LOST) + (n - k);
	return k;
}

u64 sndif_wanted(void)
{
	return sif_in.ring ? R64(sif_in.ring, SR_WANTED) : 0;
}

/* DRV_ALIVE ; réinscription si sound ne connaît plus ce bloc */
static void sif_alive(struct sif *b)
{
	if (b->ring) {
		sif_msg[0] = SND_OP_DRV_ALIVE;
		sif_msg[1] = (u64)b->id | (b == &sif_in ? SND_ID_INPUT : 0);
		*(u32 *)((u8 *)sif_msg + MSG_LEN) = 0;
		if (korin_sys2(SYS_SVC_CALL, sif_chan, sif_msg) >= 0)
			return;
		korin_sys1(SYS_BUF_FREE, b->buf); /* l'ancien anneau n'est plus servi */
	} else if (!b->name)
		return; /* jamais inscrit : rien */
	sif_register(b, 0);
}

void sndif_tick(void)
{
	u64 now = (u64)korin_sys0(SYS_TIME_MS);
	if (now < sif_next)
		return;
	sif_next = now + 2000;
	sif_alive(&sif_out);
	sif_alive(&sif_in);
}
