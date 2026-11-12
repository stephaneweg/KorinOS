/* Banc différentiel : Vorbis (ak_vorbis.c).
 * 1) fichiers Ogg Vorbis réels (skel/sd0/res/audiotest/kenney_*.ogg, et les sons
 *    de SuperTux / SuperTuxKart de build/mesa/testroot s'ils sont là) : l'essai
 *    démultiplexe l'Ogg lui-même et passe par l'API par paquets (ak_codec_*),
 *    en-têtes en laçage Xiph ; paquets abîmés (octets changés, coupés), en-têtes
 *    abîmés ;
 * 2) flux fabriqués : configurations tirées au hasard (1 à 8 canaux, livres
 *    ordonnés / creux / incomplets, quantification 1 et 2, résidus 0, 1, 2,
 *    sous-plans, couplage, plusieurs modes) et paquets de bits au hasard ;
 * 3) quand ak_ogg.c existe : ak_dec_open_mem, ak_dec_read, ak_dec_seek, ak_dec_tag.
 * Une configuration où l'asm lirait hors de ses blocs (écarts voulus de
 * ak_vorbis.c : livre >= nombre de livres, type 2 sur plus de 2 canaux) est
 * sautée dans les deux passes (vérifiée sur l'état du C). */
#include <unistd.h>

#include "cmp.h"

long ak_dec_open_mem(), ak_dec_read(), ak_dec_info(), ak_dec_tell(), ak_dec_seek(), ak_dec_tag(), ak_dec_close();
long ak_codec_open(), ak_codec_decode(), ak_codec_reset(), ak_codec_info(), ak_codec_close();

#define AKF_VORBIS 4
#define MAXPK 4096

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
	rng = rng * 1103515245u + 12345u;
	return rng >> 8;
}

static uint8_t *padded(const uint8_t *p, size_t n)
{
	uint8_t *b = calloc(1, n + 64);
	if (n)
		memcpy(b, p, n);
	return b;
}

/* ---- démultiplexage Ogg (premier flux logique) */
struct pk {
	uint8_t *p;
	size_t n;
};
struct stream {
	struct pk pk[MAXPK];
	int npk;
};

static void demux(const uint8_t *b, size_t n, struct stream *s, int maxpk)
{
	size_t off = 0;
	long serial = -1;
	uint8_t *cur = malloc(1 << 20);
	size_t cn = 0;
	s->npk = 0;
	while (off + 27 <= n && !memcmp(b + off, "OggS", 4) && s->npk < maxpk) {
		int ns = b[off + 26];
		if (off + 27 + ns > n)
			break;
		const uint8_t *lace = b + off + 27;
		size_t body = 0;
		for (int i = 0; i < ns; i++)
			body += lace[i];
		long ser;
		memcpy(&ser, b + off + 14, 4);
		ser &= 0xFFFFFFFF;
		if (serial < 0)
			serial = ser;
		const uint8_t *p = lace + ns;
		if (off + 27 + ns + body > n)
			break;
		if (ser == serial)
			for (int i = 0; i < ns && s->npk < maxpk; i++) {
				if (cn + lace[i] <= (1 << 20)) {
					memcpy(cur + cn, p, lace[i]);
					cn += lace[i];
				}
				p += lace[i];
				if (lace[i] < 255) {
					s->pk[s->npk].p = padded(cur, cn);
					s->pk[s->npk].n = cn;
					s->npk++;
					cn = 0;
				}
			}
		off += 27 + ns + body;
	}
	free(cur);
}

static void free_stream(struct stream *s)
{
	for (int i = 0; i < s->npk; i++)
		free(s->pk[i].p);
	s->npk = 0;
}

/* en-têtes -> données de l'API par paquets : 02, deux tailles en laçage, h1 h2 h3 */
static size_t mkextra(uint8_t *o, const struct pk *h)
{
	size_t k = 0;
	o[k++] = 2;
	for (int j = 0; j < 2; j++) {
		size_t v = h[j].n;
		while (v >= 255) {
			o[k++] = 255;
			v -= 255;
		}
		o[k++] = (uint8_t)v;
	}
	for (int j = 0; j < 3; j++) {
		memcpy(o + k, h[j].p, h[j].n);
		k += h[j].n;
	}
	return k;
}

/* ---- état du C : l'asm sortirait-il de ses blocs ? (écarts voulus) */
#define R64(p) (*(const uint64_t *)(p))
static int c_safe(const uint8_t *extra, size_t n)
{
	long d = ak_codec_open(AKF_VORBIS, 44100, 2, extra, n);
	if (d < 0)
		return 1; /* refusé : l'asm aussi */
	const uint8_t *s = *(const uint8_t **)((const uint8_t *)d + 136);
	uint64_t ch = R64(s), ncb = R64(s + 32);
	int ok = 1;
	const uint8_t *fl = *(const uint8_t **)(s + 56);
	for (uint64_t i = 0; i < R64(s + 48); i++) {
		const uint8_t *f = fl + i * 768;
		int maxc = -1;
		for (uint64_t p = 0; p < R64(f); p++)
			if (f[64 + p] > maxc)
				maxc = f[64 + p];
		for (int c = 0; c <= maxc; c++) {
			int sb = f[112 + c];
			if (sb && f[128 + c] >= ncb)
				ok = 0;
			for (int k = 0; k < (1 << sb); k++) {
				uint16_t b = *(const uint16_t *)(f + 144 + c * 16 + k * 2);
				if (b != 0xFFFF && b >= ncb)
					ok = 0;
			}
		}
	}
	const uint8_t *rs = *(const uint8_t **)(s + 72);
	for (uint64_t i = 0; i < R64(s + 64); i++) {
		const uint8_t *r = rs + i * 1088;
		if (R64(r + 40) >= ncb)
			ok = 0;
		for (uint64_t c = 0; c < R64(r + 32); c++)
			for (int p = 0; p < 8; p++) {
				uint16_t b = *(const uint16_t *)(r + 64 + c * 16 + p * 2);
				if (b != 0xFFFF && b >= ncb)
					ok = 0;
			}
	}
	const uint8_t *mp = *(const uint8_t **)(s + 88);
	for (uint64_t i = 0; i < R64(s + 80); i++) {
		const uint8_t *m = mp + i * 832;
		for (uint64_t k = 0; k < R64(m); k++) {
			int nch = 0;
			for (uint64_t c = 0; c < ch; c++)
				nch += m[528 + c] == k;
			if (nch > 2 && R64(rs + m[800 + k] * 1088) == 2)
				ok = 0;
		}
	}
	ak_codec_close(d);
	return ok;
}

/* ---- décodage par paquets */
static int skipped;
static long nans;

static void decode_pkts(int ref, const char *name, const uint8_t *extra, size_t en, struct pk *pk, int npk, int seed)
{
	char nm[128];
	uint8_t *ex = padded(extra, en);
	if (!c_safe(ex, en)) {
		if (!ref)
			skipped++;
		free(ex);
		return;
	}
	long d = AKCALL(ref, ak_codec_open, AKF_VORBIS, 44100, 2, ex, en);
	snprintf(nm, sizeof nm, "%s.open", name);
	OUTV(nm, d < 0 ? d : 1);
	if (d < 0) {
		free(ex);
		return;
	}
	long info[8];
	AKCALL(ref, ak_codec_info, d, info);
	snprintf(nm, sizeof nm, "%s.info", name);
	OUT(nm, info, sizeof info);
	float *out = malloc(8192 * 8 * 4 + 64);
	rng = seed;
	uint32_t crc = 0;
	long tot = 0, nan = 0;
	for (int i = 0; i < npk; i++) {
		int f32 = rnd() & 1;
		long max = rnd() % 8 ? 8192 : rnd() % 300;
		memset(out, 0x5A, 8192 * 8 * 4);
		long r = AKCALL(ref, ak_codec_decode, d, pk[i].p, pk[i].n, out, max, f32);
		snprintf(nm, sizeof nm, "%s.p%d", name, i);
		OUTV(nm, r);
		if (r > 0) {
			size_t bytes = (size_t)r * info[2] * (f32 ? 4 : 2);
			if (f32) /* NaN : signe et charge non reproduits (voir ak_vorbis.c) -> forme canonique */
				for (size_t j = 0; j < bytes / 4; j++) {
					uint32_t u;
					memcpy(&u, (uint8_t *)out + 4 * j, 4);
					if ((u & 0x7FFFFFFF) > 0x7F800000) {
						u = 0x7FC00000;
						memcpy((uint8_t *)out + 4 * j, &u, 4);
						nan++;
					}
				}
			OUT(nm, out, bytes);
			crc ^= crc32(out, bytes);
			tot += r;
		}
		if (i == npk / 2 && (seed & 1)) { /* recherche simulée */
			AKCALL(ref, ak_codec_reset, d);
			OUTV("reset", 0);
		}
	}
	snprintf(nm, sizeof nm, "%s.tot", name);
	OUTV(nm, tot);
	OUTV(nm, crc);
	OUTV(nm, nan);
	if (!ref)
		nans += nan;
	AKCALL(ref, ak_codec_info, d, info);
	OUT(nm, info, sizeof info);
	free(out);
	AKCALL(ref, ak_codec_close, d);
	free(ex);
}

static const char *files[] = {
	"skel/sd0/res/audiotest/kenney_confirm.ogg",
	"skel/sd0/res/audiotest/kenney_select.ogg",
	"build/mesa/testroot/apps/supertux.app/data/sounds/pop.ogg",
	"build/mesa/testroot/apps/supertuxkart.app/data/sfx/wee.ogg",
	"build/mesa/testroot/apps/supertuxkart.app/data/sfx/parachute.ogg",
	"build/mesa/testroot/apps/supertux.app/data/sounds/metal_hit.ogg",
	"build/mesa/testroot/apps/supertuxkart.app/data/sfx/bowling_shoot.ogg",
	"build/mesa/testroot/apps/supertux.app/data/sounds/crystallo-pop.ogg",
	"build/mesa/testroot/apps/supertux.app/data/sounds/crystallo-shardhit.ogg",
	"build/mesa/testroot/apps/supertux.app/data/sounds/tree_hit.ogg",
	"build/mesa/testroot/apps/supertuxkart.app/data/music/landing_in_gran_paradiso_fast.ogg",
};
#define NFILES (int)(sizeof files / sizeof files[0])

static struct stream st;
static uint8_t extra[1 << 20];

static void t_files(int ref)
{
	for (int f = 0; f < NFILES; f++) {
		size_t n;
		uint8_t *b = load(files[f], &n);
		if (!b)
			continue;
		demux(b, n, &st, f == NFILES - 1 ? 200 : MAXPK);
		free(b);
		if (st.npk < 3) {
			free_stream(&st);
			continue;
		}
		size_t en = mkextra(extra, st.pk);
		char nm[64];
		snprintf(nm, sizeof nm, "f%d", f);
		decode_pkts(ref, nm, extra, en, st.pk + 3, st.npk - 3, 100 + f);
		/* paquets abîmés : octets changés, coupés, vides, en-tête au milieu */
		rng = 777 + f;
		int np = st.npk - 3;
		struct pk *bad = malloc(np * sizeof *bad);
		for (int i = 0; i < np; i++) {
			struct pk *o = &st.pk[3 + i];
			size_t k = o->n;
			uint32_t m = rnd() % 6;
			if (m == 1 && k)
				k = rnd() % k;
			bad[i].p = padded(o->p, k);
			bad[i].n = k;
			if (m == 2 || m == 3)
				for (int j = 0; j < 1 + (int)(rnd() % 4); j++)
					if (k)
						bad[i].p[rnd() % k] ^= 1 << (rnd() % 8);
			if (m == 4 && k > 1)
				bad[i].p[1 + rnd() % (k - 1)] = rnd();
			if (m == 5 && !(rnd() % 4))
				bad[i].n = 0;
		}
		bad[np / 3].p[0] |= 1; /* paquet d'en-tête au milieu */
		snprintf(nm, sizeof nm, "f%d.bad", f);
		decode_pkts(ref, nm, extra, en, bad, np, 200 + f);
		/* en-têtes abîmés : coupés, octets changés (sans les paquets audio, sauf quelques-uns) */
		int nk = np < 12 ? np : 12;
		for (int c = 0; c < 24; c++) {
			rng = 5000 + f * 100 + c;
			static uint8_t e2[1 << 20];
			memcpy(e2, extra, en);
			size_t n2 = en;
			if (c < 6)
				n2 = rnd() % en;
			else
				for (int j = 0; j < 1 + (int)(rnd() % 3); j++)
					e2[rnd() % en] ^= 1 << (rnd() % 8);
			snprintf(nm, sizeof nm, "f%d.hdr%d", f, c);
			decode_pkts(ref, nm, e2, n2, st.pk + 3, nk, 300 + c);
		}
		for (int i = 0; i < np; i++)
			free(bad[i].p);
		free(bad);
		free_stream(&st);
	}
}

/* ---- flux fabriqués */
struct bw {
	uint8_t *b;
	size_t bit;
};
static void put(struct bw *w, uint64_t v, int k)
{
	for (int i = 0; i < k; i++) {
		if ((v >> i) & 1)
			w->b[w->bit >> 3] |= 1 << (w->bit & 7);
		w->bit++;
	}
}
static int ilog(uint64_t v)
{
	return v ? 64 - __builtin_clzll(v) : 0;
}
static uint64_t lookup1(uint64_t e, uint64_t dim)
{
	uint64_t r = 0;
	for (;;) {
		uint64_t p = 1;
		int over = 0;
		for (uint64_t k = 0; k < dim; k++) {
			p *= r + 1;
			if (p > e) {
				over = 1;
				break;
			}
		}
		if (over)
			return r;
		r++;
	}
}

static void gen_book(struct bw *w, int dim, int ent, int lookup)
{
	put(w, 0x564342, 24);
	put(w, dim, 16);
	put(w, ent, 24);
	int ordered = !(rnd() % 4);
	int sparse = !ordered && !(rnd() % 3);
	int used = sparse ? 1 + (int)(rnd() % ent) : ent;
	int lens[256], nl = 0;
	if (used == 1)
		lens[nl++] = 1;
	else {
		lens[nl++] = 1;
		lens[nl++] = 1;
		while (nl < used) {
			int i = rnd() % nl;
			if (lens[i] >= 20)
				continue;
			lens[i]++;
			lens[nl++] = lens[i];
		}
		if (!(rnd() % 6) && !ordered) /* incomplet */
			lens[rnd() % nl] += 1;
	}
	if (ordered) {
		for (int i = 1; i < nl; i++)
			for (int j = i; j > 0 && lens[j - 1] > lens[j]; j--) {
				int t = lens[j];
				lens[j] = lens[j - 1];
				lens[j - 1] = t;
			}
		put(w, 1, 1);
		put(w, lens[0] - 1, 5);
		int i = 0, L = lens[0];
		while (i < ent) {
			int c = 0;
			while (i + c < ent && lens[i + c] == L)
				c++;
			put(w, c, ilog(ent - i));
			i += c;
			L++;
		}
	} else {
		for (int i = nl - 1; i > 0; i--) {
			int j = rnd() % (i + 1), t = lens[i];
			lens[i] = lens[j];
			lens[j] = t;
		}
		put(w, 0, 1);
		put(w, sparse, 1);
		int left = used;
		for (int e = 0; e < ent; e++) {
			int u = 1;
			if (sparse) {
				u = left && (ent - e <= left || rnd() % 2);
				put(w, u, 1);
			}
			if (u)
				put(w, lens[used - left--] - 1, 5);
		}
	}
	put(w, lookup, 4);
	if (lookup) {
		for (int j = 0; j < 2; j++)
			put(w, (rnd() & 0x1FFFFF) | ((uint64_t)(762 + rnd() % 8) << 21) | ((uint64_t)(rnd() & 1) << 31), 32);
		int vb = 1 + rnd() % 8;
		put(w, vb - 1, 4);
		put(w, rnd() & 1, 1);
		uint64_t nv = lookup == 1 ? lookup1(ent, dim) : (uint64_t)ent * dim;
		for (uint64_t i = 0; i < nv; i++)
			put(w, rnd() & ((1 << vb) - 1), vb);
	}
}

static size_t gen_setup(uint8_t *o, int ch)
{
	memset(o, 0, 1 << 16);
	struct bw w = {o, 0};
	put(&w, 5, 8);
	for (const char *v = "vorbis"; *v; v++)
		put(&w, *v, 8);
	int ncb = 3 + rnd() % 6, vq[16];
	put(&w, ncb - 1, 8);
	for (int i = 0; i < ncb; i++) {
		int lk = i < 3 ? i : (int)(rnd() % 3);
		vq[i] = lk;
		int dim = 1 + rnd() % 4;
		int ent = lk == 2 ? 2 + rnd() % 30 : 2 + rnd() % 60;
		if (i == 0 && !(rnd() % 3))
			ent = 1 + rnd() % 3;
		gen_book(&w, dim, ent, lk);
	}
	put(&w, 0, 6); /* transformées */
	put(&w, 0, 16);
	int nfl = 1 + rnd() % 2;
	put(&w, nfl - 1, 6);
	for (int i = 0; i < nfl; i++) {
		put(&w, 1, 16);
		int parts = rnd() % 7, ncl = 1 + rnd() % 3, cdim[4];
		put(&w, parts, 5);
		int pc[32];
		for (int p = 0; p < parts; p++) {
			pc[p] = rnd() % ncl;
			put(&w, pc[p], 4);
		}
		int maxc = -1;
		for (int p = 0; p < parts; p++)
			if (pc[p] > maxc)
				maxc = pc[p];
		for (int c = 0; c <= maxc; c++) {
			cdim[c] = 1 + rnd() % 3;
			put(&w, cdim[c] - 1, 3);
			int sb = rnd() % 3;
			put(&w, sb, 2);
			if (sb)
				put(&w, rnd() % ncb, 8);
			for (int k = 0; k < (1 << sb); k++)
				put(&w, rnd() % 4 ? 1 + rnd() % ncb : 0, 8);
		}
		put(&w, rnd() % 4, 2);
		int rb = 4 + rnd() % 7;
		put(&w, rb, 4);
		for (int p = 0; p < parts; p++)
			for (int j = 0; j < cdim[pc[p]]; j++)
				put(&w, 1 + rnd() % ((1 << rb) - 1), rb);
	}
	int nres = 1 + rnd() % 3, rtype[4];
	put(&w, nres - 1, 6);
	for (int i = 0; i < nres; i++) {
		rtype[i] = rnd() % 3;
		if (i == 0 && ch > 2)
			rtype[i] = rnd() % 2;
		put(&w, rtype[i], 16);
		put(&w, rnd() % 4 ? 0 : rnd() % 40, 24);
		put(&w, rnd() % 4 ? 100000 : rnd() % 3000, 24);
		put(&w, rnd() % 32, 24);
		int nc = 1 + rnd() % 4;
		put(&w, nc - 1, 6);
		put(&w, rnd() % ncb, 8);
		int casc[64];
		for (int c = 0; c < nc; c++) {
			casc[c] = rnd() & 0xFF;
			put(&w, casc[c] & 7, 3);
			put(&w, casc[c] > 7, 1);
			if (casc[c] > 7)
				put(&w, casc[c] >> 3, 5);
		}
		for (int c = 0; c < nc; c++)
			for (int p = 0; p < 8; p++)
				if ((casc[c] >> p) & 1) {
					int b = rnd() % ncb;
					if (rnd() % 8 && !vq[b])
						b = 1 + rnd() % 2;
					put(&w, b, 8);
				}
	}
	int nmap = 1 + rnd() % 2;
	put(&w, nmap - 1, 6);
	for (int i = 0; i < nmap; i++) {
		put(&w, 0, 16);
		int sm = ch > 1 && rnd() % 2 ? 1 + rnd() % (ch < 4 ? ch : 4) : 1;
		put(&w, sm > 1, 1);
		if (sm > 1)
			put(&w, sm - 1, 4);
		int cpl = ch > 1 && rnd() % 3;
		put(&w, cpl, 1);
		if (cpl) {
			int steps = 1 + rnd() % ch;
			put(&w, steps - 1, 8);
			for (int k = 0; k < steps; k++) {
				int a = rnd() % ch, b = (a + 1 + rnd() % (ch - 1)) % ch;
				put(&w, a, ilog(ch - 1));
				put(&w, b, ilog(ch - 1));
			}
		}
		put(&w, 0, 2);
		int mux[8], cnt[16] = {0};
		if (sm > 1)
			for (int c = 0; c < ch; c++) {
				mux[c] = rnd() % sm;
				cnt[mux[c]]++;
				put(&w, mux[c], 4);
			}
		else
			cnt[0] = ch;
		for (int k = 0; k < sm; k++) {
			put(&w, 0, 8);
			put(&w, rnd() % nfl, 8);
			int r = rnd() % nres;
			if (cnt[k] > 2 && rtype[r] == 2)
				r = 0;
			put(&w, r, 8);
		}
	}
	int nmode = 1 + rnd() % 4;
	put(&w, nmode - 1, 6);
	for (int i = 0; i < nmode; i++) {
		put(&w, rnd() & 1, 1);
		put(&w, 0, 16);
		put(&w, 0, 16);
		put(&w, rnd() % nmap, 8);
	}
	put(&w, 1, 1);
	return (w.bit + 7) >> 3;
}

static void t_synth(int ref)
{
	static uint8_t hid[30], hcm[64], hsu[1 << 16];
	static struct pk pk[64];
	for (int k = 0; k < 120; k++) {
		rng = 9000 + k * 31;
		int ch = 1 + rnd() % 8;
		int b0 = 6 + rnd() % 5, b1 = b0 + rnd() % 3;
		if (k % 17 == 5)
			b1 = 13;
		memset(hid, 0, sizeof hid);
		hid[0] = 1;
		memcpy(hid + 1, "vorbis", 6);
		hid[11] = ch;
		uint32_t rate = 8000 + rnd() % 90000;
		memcpy(hid + 12, &rate, 4);
		uint32_t br = rnd();
		memcpy(hid + 20, &br, 4);
		hid[28] = b0 | b1 << 4;
		hid[29] = 1;
		static const uint8_t cm[] = "\x03vorbis\x04\0\0\0test\x01\0\0\0\x0b\0\0\0TITLE=synth\x01";
		memcpy(hcm, cm, sizeof cm - 1);
		struct pk h[3] = {{hid, 30}, {hcm, sizeof cm - 1}, {hsu, 0}};
		h[2].n = gen_setup(hsu, ch);
		size_t en = mkextra(extra, h);
		int np = 12 + rnd() % 24;
		for (int i = 0; i < np; i++) {
			size_t n = 1 + rnd() % (rnd() % 4 ? 120 : 1500);
			pk[i].p = malloc(n + 64);
			for (size_t j = 0; j < n; j++)
				pk[i].p[j] = rnd();
			pk[i].p[0] &= 0xFE;
			pk[i].n = n;
		}
		char nm[32];
		snprintf(nm, sizeof nm, "s%d", k);
		decode_pkts(ref, nm, extra, en, pk, np, 400 + k);
		/* même flux, configuration abîmée (pas 5 canaux : l'écart du couplage) */
		if (ch != 5)
			for (int c = 0; c < 3; c++) {
				rng = 70000 + k * 7 + c;
				size_t p = 1 + 30 + 1 + h[1].n + 7 + rnd() % (h[2].n - 7);
				if (p < en)
					extra[p] ^= 1 << (rnd() % 8);
				snprintf(nm, sizeof nm, "s%d.h%d", k, c);
				decode_pkts(ref, nm, extra, en, pk, np < 8 ? np : 8, 500 + k);
			}
		for (int i = 0; i < np; i++)
			free(pk[i].p);
	}
}

/* ---- fichiers par l'Ogg (ak_ogg.c) */
static void t_ogg(int ref)
{
	if (access("src/kits/audiokit/ak_ogg.c", F_OK)) {
		OUTV("ogg absent", 0);
		return;
	}
	for (int f = 0; f < NFILES; f++) {
		size_t n;
		uint8_t *b = load(files[f], &n);
		if (!b)
			continue;
		if (f == NFILES - 1 && n > 200000)
			n = 200000; /* (coupé) */
		char nm[96];
		for (int v = 0; v < 2; v++) {
			size_t k = v ? n - n / 3 : n;
			long d = AKCALL(ref, ak_dec_open_mem, b, k);
			snprintf(nm, sizeof nm, "o%d.%d.open", f, v);
			OUTV(nm, d < 0 ? d : 1);
			if (d < 0)
				continue;
			long info[8];
			AKCALL(ref, ak_dec_info, d, info);
			OUT(nm, info, sizeof info);
			char tag[64];
			for (int t = 0; t < 3; t++) {
				memset(tag, 0x55, sizeof tag);
				OUTV(nm, AKCALL(ref, ak_dec_tag, d, t, tag, sizeof tag));
				OUT(nm, tag, sizeof tag);
			}
			int16_t *pcm = malloc(1 << 23);
			size_t tot = 0;
			int chunk = 1;
			for (;;) {
				long r = AKCALL(ref, ak_dec_read, d, (uint8_t *)pcm + tot, chunk);
				if (r <= 0) {
					OUTV(nm, r);
					break;
				}
				tot += r * 2 * info[2];
				chunk = chunk * 3 + 1;
				if (chunk > 5000)
					chunk = 7;
				if (tot > (1 << 23) - 200000)
					break;
			}
			snprintf(nm, sizeof nm, "o%d.%d.pcm", f, v);
			OUT(nm, pcm, tot);
			long ms[] = {0, 30, 250, -5, 100000, 17, 1000};
			for (unsigned i = 0; i < sizeof ms / sizeof ms[0]; i++) {
				snprintf(nm, sizeof nm, "o%d.%d.seek%ld", f, v, ms[i]);
				OUTV(nm, AKCALL(ref, ak_dec_seek, d, ms[i]));
				OUTV(nm, AKCALL(ref, ak_dec_tell, d));
				long r = AKCALL(ref, ak_dec_read, d, pcm, 700);
				OUTV(nm, r);
				if (r > 0)
					OUT(nm, pcm, r * 2 * info[2]);
			}
			free(pcm);
			AKCALL(ref, ak_dec_close, d);
		}
		free(b);
	}
}

static void t_end(int ref)
{
	(void)ref;
	OUTV("sautées (écarts voulus)", skipped);
	OUTV("NaN canonisés (F32)", nans);
}

int main(void)
{
	RUN(t_files);
	RUN(t_synth);
	RUN(t_ogg);
	RUN(t_end);
	return cmp_end();
}
