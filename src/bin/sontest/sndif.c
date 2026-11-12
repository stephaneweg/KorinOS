/* sndif.c : src/svc/inc/sndif.inc traduit (sans l'attente d'interruption DMA), pour sontest.
 * Anneau partagé : un producteur, un consommateur, compteurs d'octets monotones ; la tête est lue
 * avec acquisition (ldar) et la queue écrite avec libération (stlr), comme l'asm. */
#include "korinrt.h"
#include "sndif.h"

#define WM_OP 0
#define WM_A 8
#define WM_B 16
#define WM_C 24
#define WM_LEN 40
#define WM_DATA 48

struct sif sif_out = {.dma = -1}, sif_in = {.dma = -1};
static long sif_chan;
static u64 sif_next;
static u64 sif_msg[512 / 8];

/* bloc, attente (ms) -> numéro, < 0 si échec */
static long sif_register(struct sif *b, long wait)
{
	b->ring = 0;
	long r = korin_sys2(SYS_SVC_OPEN, "sound", wait);
	if (r < 0)
		return r;
	sif_chan = r;
	u8 *m = (u8 *)sif_msg;
	sif_msg[WM_OP / 8] = b == &sif_in ? SND_OP_DRV_INPUT : SND_OP_DRV_OUTPUT;
	sif_msg[WM_A / 8] = (u64)b->type;
	sif_msg[WM_B / 8] = (u64)b->rate;
	long req = b->dmareq;
	if (b->dma >= 0 && req) /* réinscription : le même canal */
		req = b->dma + 0x100;
	sif_msg[WM_C / 8] = (u64)req;
	u64 i; /* nom (31 caractères au plus) */
	for (i = 0; i < 31; i++) {
		m[WM_DATA + i] = (u8)b->name[i];
		if (!b->name[i])
			break;
	}
	if (i == 31)
		m[WM_DATA + 31] = 0;
	*(u32 *)(m + WM_LEN) = 32;
	r = korin_sys2(SYS_SVC_CALL, sif_chan, sif_msg);
	if (r < 0)
		return r;
	b->id = r;
	b->dma = (long)sif_msg[WM_C / 8];
	b->buf = (long)sif_msg[WM_B / 8]; /* l'anneau */
	r = korin_sys1(SYS_BUF_MAP, b->buf);
	if (r < 0)
		return r;
	b->ring = (u8 *)r;
	return b->id;
}

long sndif_output(long type, long rate, long dma, const char *name)
{
	sif_out.type = type, sif_out.rate = rate, sif_out.dmareq = dma, sif_out.name = name;
	return sif_register(&sif_out, 60000); /* (sound peut démarrer après le pilote) */
}

long sndif_input(long type, long rate, long dma, const char *name)
{
	sif_in.type = type, sif_in.rate = rate, sif_in.dmareq = dma, sif_in.name = name;
	return sif_register(&sif_in, 60000);
}

long sndif_read(i16 *dst, long frames)
{
	u32 *d = (u32 *)dst;
	u64 n = (u64)frames, k = 0, took = 0;
	u8 *ring = sif_out.ring;
	if (ring) {
		u64 head = __atomic_load_n((u64 *)(ring + SR_HEAD), __ATOMIC_ACQUIRE);
		u64 tail = *(u64 *)(ring + SR_TAIL);
		took = (head - tail) >> 2;
		if (took >= n)
			took = n; /* trames prises */
		for (; k < took; k++, tail += 4)
			d[k] = *(u32 *)(ring + SR_DATA + (tail & (SR_OUT_BYTES - 1)));
		__atomic_store_n((u64 *)(ring + SR_TAIL), tail, __ATOMIC_RELEASE);
		*(u64 *)(ring + SR_PLAYED) += took;
		if (n != took)
			*(u64 *)(ring + SR_LOST) += n - took;
	}
	for (; k < n; k++)
		d[k] = 0;
	return (long)took;
}

long sndif_write(const i16 *src, long frames)
{
	const u32 *s = (const u32 *)src;
	u8 *ring = sif_in.ring;
	if (!ring)
		return 0;
	u64 n = (u64)frames;
	u64 head = *(u64 *)(ring + SR_HEAD);
	u64 tail = __atomic_load_n((u64 *)(ring + SR_TAIL), __ATOMIC_ACQUIRE);
	u64 put = (SR_IN_BYTES - (head - tail)) >> 2;
	if (put >= n)
		put = n;
	for (u64 k = 0; k < put; k++, head += 4)
		*(u32 *)(ring + SR_DATA + (head & (SR_IN_BYTES - 1))) = s[k];
	__atomic_store_n((u64 *)(ring + SR_HEAD), head, __ATOMIC_RELEASE);
	if (n != put)
		*(u64 *)(ring + SR_LOST) += n - put;
	return (long)put;
}

long sndif_wanted(void)
{
	if (!sif_in.ring)
		return 0;
	return *(long *)(sif_in.ring + SR_WANTED);
}

/* DRV_ALIVE ; réinscription si sound ne connaît plus ce bloc (relance) */
static void sif_alive(struct sif *b)
{
	if (b->ring) {
		sif_msg[WM_OP / 8] = SND_OP_DRV_ALIVE;
		long id = b->id;
		if (b == &sif_in)
			id |= SND_ID_INPUT;
		sif_msg[WM_A / 8] = (u64)id;
		*(u32 *)((u8 *)sif_msg + WM_LEN) = 0;
		if (korin_sys2(SYS_SVC_CALL, sif_chan, sif_msg) >= 0)
			return;
		korin_sys1(SYS_BUF_FREE, b->buf); /* l'ancien anneau n'est plus servi */
	} else if (!b->name) {
		return; /* jamais inscrit (ou échec) : rien */
	}
	sif_register(b, 0);
}

void sndif_tick(void)
{
	u64 t = (u64)sys_time_ms();
	if (t < sif_next)
		return;
	sif_next = t + 2000;
	sif_alive(&sif_out);
	sif_alive(&sif_in);
}
