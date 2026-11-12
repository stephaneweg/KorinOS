/* Banc différentiel : FFT et IMDCT (ak_mdct.c). */
#include "ak_mdct.h"
#include "cmp.h"

static uint32_t rng = 1;
static float rndf(void)
{
	rng = rng * 1103515245u + 12345u;
	return ((int)(rng >> 8 & 0xFFFF) - 32768) / 4096.0f;
}

static void t_mdct(int ref)
{
	static const int ns[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192, 240, 480, 960, 1920, 120, 36, 7, 100};
	char nm[64];
	for (unsigned i = 0; i < sizeof ns / sizeof ns[0]; i++) {
		int n = ns[i];
		void *d = ak_alloc(AKD_BYTES);
		struct akmdct *m = ak_alloc(MD_SIZE);
		long r = AKCALL(ref, ak_imdct_init, d, m, n);
		snprintf(nm, sizeof nm, "imdct%d.init", n);
		OUTV(nm, r);
		if (r == 0) {
			OUT(nm, m->tw, m->n4 * 8);
			OUT(nm, m->fft.f, sizeof m->fft.f);
			OUTV(nm, m->fft.nf);
			OUT(nm, m->fft.pos, m->n4 * 4);
			OUT(nm, m->fft.w, m->n4 * 8);
			float *x = malloc(n * 2), *y = malloc(n * 4 + 64);
			for (int t = 0; t < 3; t++) {
				rng = n * 7 + t;
				for (int k = 0; k < n / 2; k++)
					x[k] = rndf() * (t == 2 ? 1e-30f : 1.0f);
				memset(y, 0, n * 4 + 64);
				AKCALL(ref, ak_imdct, m, x, y);
				snprintf(nm, sizeof nm, "imdct%d.%d", n, t);
				OUT(nm, y, n * 4 + 64);
				/* FFT seule */
				float *o = malloc(m->n4 * 8);
				AKCALL(ref, ak_fft, &m->fft, x, o);
				snprintf(nm, sizeof nm, "fft%d.%d", (int)m->n4, t);
				OUT(nm, o, m->n4 * 8);
				free(o);
			}
			free(x);
			free(y);
		}
		ak_free(m);
		ak_free(d);
	}
}

int main(void)
{
	RUN(t_mdct);
	return cmp_end();
}
