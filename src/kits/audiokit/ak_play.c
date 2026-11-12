/* AudioKit : lecteur en arrière-plan, ak_play(chemin) (ak_play.inc).
 *
 * Un thread du process de l'application décode le fichier (ak_dec_*) et
 * écrit dans un flux de sound (ak_open au format du fichier : AudioKit
 * convertit et rééchantillonne). Commandes par des mots partagés, lus par
 * le thread à chaque bloc (~23 ms) : pause, arrêt, recherche. */
#include "ak.h"

#define PL_FRAMES 1024
#define PL_STACK_SIZE 65536

long ak_dec_open(const char *path);
long ak_open(u64 rate, u64 ch, u64 bits, long out);
long ak_write(long h, const void *buf, u64 n);
long ak_pending(long h);
long ak_volume(long h, u64 v);
long ak_close(long h);

/* PL_* */
struct pl {
	struct akd *dec;     /* 0 décodeur */
	long stream;	     /* 8 flux de sound */
	volatile long state; /* 16 AKP_* */
	volatile long pause; /* 24 1 : en pause (demande) */
	volatile long stop;  /* 32 1 : le thread doit finir */
	volatile long seek;  /* 40 recherche demandée (ms), -1 : aucune */
	long tid;	     /* 48 */
	void *stack;	     /* 56 */
	u64 rate;	     /* 64 */
	u64 ch;		     /* 72 */
	u64 pad[6];
	s16 buf[PL_FRAMES * 8]; /* 128 trames s16 (8 canaux au plus) */
};
_Static_assert(sizeof(struct pl) == 128 + PL_FRAMES * 16, "pl");

static void sleep_ms(long ms)
{
	korin_sys1(SYS_SLEEP, ms);
}

/* thread du lecteur */
static void __attribute__((noreturn)) ak_play_thread(struct pl *p)
{
	for (;;) {
		if (p->stop)
			break;
		long sk = p->seek;
		if (sk >= 0) {
			p->seek = -1;
			ak_dec_seek(p->dec, sk);
			p->state = AKP_PLAYING;
		}
		if (p->pause) {
			p->state = AKP_PAUSED;
			sleep_ms(20);
			continue;
		}
		if (p->state != AKP_PLAYING) {
			if (p->state != AKP_PAUSED) { /* fini ou en erreur : attendre */
				sleep_ms(20);
				continue;
			}
			p->state = AKP_PLAYING;
		}
		long n = ak_dec_read(p->dec, p->buf, PL_FRAMES);
		if (n <= 0) { /* fin : attendre que tout soit joué */
			long st = n < 0 ? AKP_ERROR : AKP_ENDED;
			for (;;) {
				if (p->stop)
					goto exit;
				if (p->seek >= 0)
					break;
				if (ak_pending(p->stream) <= 0) {
					p->state = st;
					break;
				}
				sleep_ms(10);
			}
			continue;
		}
		u64 left = (u64)n * (p->ch << 1); /* octets */
		const u8 *src = (const u8 *)p->buf;
		while (left) {
			long w = ak_write(p->stream, src, left);
			if (w < 0) {
				p->state = AKP_ERROR;
				sleep_ms(20);
				break;
			}
			src += w;
			left -= (u64)w;
			if (!left)
				break;
			if (p->stop)
				goto exit;
			if (p->seek >= 0) /* (le reste du bloc est perdu) */
				break;
			sleep_ms(5);
		}
	}
exit:
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, 0);
}

/* chemin, sortie (0 : celle du process) -> lecteur, < 0 : erreur ; la
 * lecture commence tout de suite */
long ak_play(const char *path, long out)
{
	s64 info[AKI_SIZE / 8];
	long d = ak_dec_open(path);
	if (d < 0)
		return d;
	struct akd *dec = (struct akd *)d;
	ak_dec_info(dec, info);
	long s = ak_open((u64)info[AKI_RATE / 8], (u64)info[AKI_CHANNELS / 8], 16, out);
	long err;
	if (s < 0) {
		err = s;
		goto fd;
	}
	err = AKE_NOMEM;
	struct pl *p = ak_alloc(sizeof(struct pl));
	if (!p)
		goto fst;
	p->dec = dec;
	p->stream = s;
	p->rate = (u64)info[AKI_RATE / 8];
	p->ch = (u64)info[AKI_CHANNELS / 8];
	p->seek = -1;
	p->state = AKP_PLAYING;
	p->stack = ak_alloc(PL_STACK_SIZE);
	if (!p->stack)
		goto fp;
	long t = korin_sys3(SYS_THREAD_CREATE, ak_play_thread, (u8 *)p->stack + PL_STACK_SIZE, p);
	if (t < 0) {
		err = t;
		ak_free(p->stack);
		goto fp;
	}
	p->tid = t;
	return (long)p;
fp:
	ak_free(p);
fst:
	ak_close(s);
fd:
	ak_dec_close(dec);
	return err;
}

long ak_play_pause(struct pl *p, long on)
{
	p->pause = on != 0;
	return 0;
}

long ak_play_seek(struct pl *p, long ms)
{
	p->seek = ms < 0 ? 0 : ms;
	return 0;
}

long ak_play_state(struct pl *p)
{
	return p->state;
}

long ak_play_volume(struct pl *p, u64 v)
{
	return ak_volume(p->stream, v);
}

long ak_play_decoder(struct pl *p)
{
	return (long)p->dec;
}

/* -> position entendue (ms) : décodée moins l'avance du flux */
long ak_play_pos(struct pl *p)
{
	long q = ak_pending(p->stream); /* octets s16 stéréo 44,1 kHz */
	if (q < 0)
		q = 0;
	u64 wait = (((u64)q >> 2) * 1000) / SND_RATE_STD; /* ms en attente */
	u64 pos = ak_udiv((u64)p->dec->pos * 1000, p->rate);
	s64 r = (s64)(pos - wait);
	return r < 0 ? 0 : r;
}

/* arrête le thread, ferme le flux et le fichier -> 0 */
long ak_play_stop(struct pl *p)
{
	p->stop = 1;
	korin_sys1(SYS_THREAD_JOIN, p->tid);
	ak_close(p->stream);
	ak_dec_close(p->dec);
	ak_free(p->stack);
	ak_free(p);
	return 0;
}
