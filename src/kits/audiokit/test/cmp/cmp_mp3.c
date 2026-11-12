/* Banc différentiel : décodeur MP3 (ak_mp3.c, ak_mp3tab.c).
 *
 * Fichiers : skel/sd0/res/audiotest/{jfk,enc128}.mp3 ; build/audiokit_cmp/mp3/fz_*.mp3
 * (mkmp3.py : trames aléatoires valides de tools/audiotest/mp3fuzz.py : MPEG-1, 2, 2.5,
 * blocs longs, courts, mixtes, M/S, intensité, scfsi, pretab, quadruplets A/B,
 * réservoir) ; build/audiokit_cmp/mp3/enc_*.mp3 (fabriqués ici, sur aarch64, par
 * l'encodeur MP3 en asm : 8 à 48 kHz, mono et stéréo ; relus tels quels sur x86-64).
 * Chaque flux : mémoire et fonctions (lectures de tailles variées), lectures de tailles
 * variées, recherches, informations, étiquettes ; API par paquets (s16, f32, remise à
 * zéro) ; variantes : Xing/Info fabriqués, ID3v2/ID3v1, ordures devant, coupés,
 * octets changés. */
#include "cmp.h"

long ak_dec_open_mem(), ak_dec_open_io(), ak_dec_read(), ak_dec_info(), ak_dec_tell(), ak_dec_seek();
long ak_dec_tag(), ak_dec_close();
long ak_codec_open(), ak_codec_decode(), ak_codec_reset(), ak_codec_info(), ak_codec_close();
long ak_enc_open_io(), ak_enc_write(), ak_enc_close();

#define AKF_MP3 3
#define DIR "build/audiokit_cmp/mp3/"

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
	rng = rng * 1103515245u + 12345u;
	return rng >> 8;
}

/* source par fonctions (mode 1) sur un tampon : lectures de tailles variées */
struct src {
	const uint8_t *p;
	long n, pos, reads, seeks, k;
};
static long src_read(long ctx, void *buf, long n)
{
	struct src *s = (struct src *)ctx;
	s->reads++;
	static const long sz[] = {7000, 1, 333, 4096, 65536, 17, 9000};
	long k = s->n - s->pos;
	if (k > n)
		k = n;
	long lim = sz[s->k++ % 7];
	if (k > lim)
		k = lim;
	if (k < 0)
		k = 0;
	memcpy(buf, s->p + s->pos, k);
	s->pos += k;
	return k;
}
static long src_seek(long ctx, long pos)
{
	struct src *s = (struct src *)ctx;
	s->seeks++;
	s->pos = pos;
	return 0;
}

#define PCMMAX (16 << 20)
static int16_t *pcm;

static void decode_all(int ref, const char *name, const uint8_t *buf, size_t n, int io, int full)
{
	char nm[128];
	struct src s = {buf, (long)n, 0, 0, 0, 0};
	long d;
	if (io)
		d = AKCALL(ref, ak_dec_open_io, refcb(ref, 0, (void *)src_read), refcb(ref, 1, (void *)src_seek), &s, n);
	else
		d = AKCALL(ref, ak_dec_open_mem, buf, n);
	snprintf(nm, sizeof nm, "%s.open", name);
	OUTV(nm, d < 0 ? d : 1);
	if (d < 0)
		return;
	long info[8];
	AKCALL(ref, ak_dec_info, d, info);
	snprintf(nm, sizeof nm, "%s.info", name);
	OUT(nm, info, sizeof info);
	char tag[300];
	for (int t = 0; t < 4; t++) {
		memset(tag, 0x55, sizeof tag);
		long k = AKCALL(ref, ak_dec_tag, d, t, tag, t == 1 ? 5 : sizeof tag);
		snprintf(nm, sizeof nm, "%s.tag%d", name, t);
		OUTV(nm, k);
		OUT(nm, tag, sizeof tag);
	}
	size_t tot = 0;
	int chunk = 1;
	for (;;) {
		long r = AKCALL(ref, ak_dec_read, d, (uint8_t *)pcm + tot, chunk);
		if (r <= 0) {
			snprintf(nm, sizeof nm, "%s.end", name);
			OUTV(nm, r);
			break;
		}
		tot += r * 2 * info[2];
		chunk = chunk * 3 + 1;
		if (chunk > 5000)
			chunk = 7;
		if (tot > PCMMAX - 200000 || (!full && tot > 400000))
			break;
	}
	snprintf(nm, sizeof nm, "%s.pcm", name);
	OUT(nm, pcm, tot);
	snprintf(nm, sizeof nm, "%s.tell", name);
	OUTV(nm, AKCALL(ref, ak_dec_tell, d));
	/* recherches */
	long ms[] = {0, 3, 50, 500, -5, 100000, 17, 1234, 2000, 260};
	for (unsigned i = 0; i < sizeof ms / sizeof ms[0]; i++) {
		long r = AKCALL(ref, ak_dec_seek, d, ms[i]);
		snprintf(nm, sizeof nm, "%s.seek%ld", name, ms[i]);
		OUTV(nm, r);
		OUTV(nm, AKCALL(ref, ak_dec_tell, d));
		r = AKCALL(ref, ak_dec_read, d, pcm, 3000);
		OUTV(nm, r);
		if (r > 0)
			OUT(nm, pcm, r * 2 * info[2]);
		AKCALL(ref, ak_dec_info, d, info);
		OUT(nm, info, sizeof info);
		if (!full && i >= 3)
			break;
	}
	OUTV("close", AKCALL(ref, ak_dec_close, d));
	if (io) {
		snprintf(nm, sizeof nm, "%s.io", name);
		OUTV(nm, s.reads);
		OUTV(nm, s.seeks);
	}
}

/* octets de la trame MP3 commençant en p (0 : pas un en-tête de couche III) */
static long frame_len(const uint8_t *p)
{
	static const int rates[9] = {44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000};
	static const int br[2][16] = {{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},
				      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}};
	uint32_t h = (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
	if ((h >> 21) != 0x7FF)
		return 0;
	int v = (h >> 19) & 3, l = (h >> 17) & 3, b = (h >> 12) & 15, f = (h >> 10) & 3;
	if (v == 1 || l != 1 || !b || b == 15 || f == 3)
		return 0;
	int sri = (v == 3 ? 0 : v == 2 ? 3 : 6) + f, lsf = v != 3;
	return (lsf ? 72000 : 144000) * br[lsf][b] / rates[sri] + ((h >> 9) & 1);
}

/* API par paquets : chaque trame (puis des paquets abîmés), s16 puis f32 */
static void packets(int ref, const char *name, const uint8_t *buf, size_t n)
{
	char nm[128];
	size_t o = 0;
	while (o + 4 <= n && !frame_len(buf + o)) /* première trame */
		o++;
	if (o + 4 > n)
		return;
	static float out[4 * 1152];
	for (int f32 = 0; f32 < 2; f32++) {
		long d = AKCALL(ref, ak_codec_open, AKF_MP3, 44100, 2, 0, 0);
		snprintf(nm, sizeof nm, "%s.pk%d.open", name, f32);
		OUTV(nm, d < 0 ? d : 1);
		if (d < 0)
			return;
		size_t p = o;
		int k = 0;
		uint32_t crc = 0;
		while (p + 4 <= n && k < 400) {
			long fl = frame_len(buf + p);
			if (!fl || p + fl > n)
				break;
			memset(out, 0x77, sizeof out);
			long max = k % 5 == 3 ? 100 : 1152;
			long r = AKCALL(ref, ak_codec_decode, d, buf + p, k % 7 == 6 ? fl + 1000 : fl, out, max, f32);
			snprintf(nm, sizeof nm, "%s.pk%d.%d", name, f32, k);
			if (r <= 0 || k % 9 == 0) {
				OUTV(nm, r);
				if (r > 0)
					OUT(nm, out, sizeof out);
			} else { /* (sortie résumée : crc de toutes, une ligne par trame serait trop) */
				crc = crc32(out, sizeof out) ^ (crc * 31) ^ (uint32_t)r;
			}
			if (k == 20) {
				OUTV(nm, AKCALL(ref, ak_codec_reset, d));
				long info[8];
				AKCALL(ref, ak_codec_info, d, info);
				OUT(nm, info, sizeof info);
			}
			p += fl;
			k++;
		}
		snprintf(nm, sizeof nm, "%s.pk%d.crc", name, f32);
		OUTV(nm, crc);
		/* paquets abîmés : court, en-tête faux, coupé, octets changés */
		uint8_t bad[2048];
		long fl = frame_len(buf + o);
		for (int t = 0; t < 8; t++) {
			size_t m = fl < (long)sizeof bad ? (size_t)fl : sizeof bad;
			if (o + m > n)
				m = n - o;
			memcpy(bad, buf + o, m);
			if (t == 0)
				m = 3;
			else if (t == 1)
				bad[1] = 0;
			else if (t == 2)
				m = m / 2;
			else if (t == 3)
				bad[2] |= 0xF0; /* débit 15 */
			else
				for (int j = 0; j < 4 * t; j++)
					bad[4 + rnd() % (m - 4)] = rnd();
			memset(out, 0x77, sizeof out);
			long r = AKCALL(ref, ak_codec_decode, d, bad, m, out, 1152, f32);
			snprintf(nm, sizeof nm, "%s.pk%d.bad%d", name, f32, t);
			OUTV(nm, r);
			if (r > 0)
				OUT(nm, out, sizeof out);
		}
		OUTV(nm, AKCALL(ref, ak_codec_close, d));
	}
}

static uint8_t *files[64];
static size_t fsize[64];
static char fname[64][48];
static int nfiles;

static void addfile(const char *path, const char *name)
{
	size_t n;
	uint8_t *f = load(path, &n);
	if (!f) {
		printf("(absent : %s)\n", path);
		return;
	}
	files[nfiles] = f;
	fsize[nfiles] = n;
	snprintf(fname[nfiles], sizeof fname[0], "%s", name);
	nfiles++;
}

static void t_files(int ref)
{
	char nm[96];
	for (int i = 0; i < nfiles; i++) {
		int full = i < 2 || i % 3 == 0;
		decode_all(ref, fname[i], files[i], fsize[i], 0, 1);
		snprintf(nm, sizeof nm, "%s.io", fname[i]);
		decode_all(ref, nm, files[i], fsize[i], 1, full);
	}
}

static void t_packets(int ref)
{
	for (int i = 0; i < nfiles; i++) {
		rng = 500 + i;
		packets(ref, fname[i], files[i], fsize[i]);
	}
}

/* en-tête Xing / Info fabriqué devant le flux f (même genre de trame) */
static size_t mkxing(uint8_t *b, const uint8_t *f, size_t n, const char *id, uint32_t flags, uint32_t frames,
		     int crc)
{
	size_t o = 0; /* première trame (le modèle de l'en-tête) */
	while (o + 4 <= n && !frame_len(f + o))
		o++;
	if (o + 4 > n)
		return 0;
	long fl;
	memcpy(b, f + o, 4);
	b[2] &= ~2; /* sans bourrage */
	if (crc)
		b[1] &= ~1;
	fl = frame_len(b);
	memset(b + 4, 0, fl - 4);
	int lsf = ((b[1] >> 3) & 3) != 3, mono = (b[3] >> 6) == 3;
	int side = lsf ? (mono ? 9 : 17) : (mono ? 17 : 32);
	uint8_t *x = b + 4 + side + (crc ? 2 : 0);
	memcpy(x, id, 4);
	x[4] = flags >> 24;
	x[5] = flags >> 16;
	x[6] = flags >> 8;
	x[7] = flags;
	x += 8;
	if (flags & 1) {
		x[0] = frames >> 24;
		x[1] = frames >> 16;
		x[2] = frames >> 8;
		x[3] = frames;
		x += 4;
	}
	if (flags & 2)
		x += 4;
	if (flags & 4)
		for (int i = 0; i < 100; i++)
			x[i] = (uint8_t)(i * 255 / 100 + (i & 3));
	memcpy(b + fl, f, n);
	return fl + n;
}

static void t_variants(int ref)
{
	static uint8_t b[1 << 21];
	char nm[96];
	for (int i = 0; i < nfiles; i++) {
		const uint8_t *f = files[i];
		size_t n = fsize[i];
		if (n + 70000 > sizeof b)
			continue;
		/* Xing (VBR, table), Info, Xing sans trames, Xing avec CRC */
		static const struct {
			const char *id;
			uint32_t flags, frames;
			int crc;
		} xs[] = {{"Xing", 15, 40, 0}, {"Info", 7, 33, 0}, {"Xing", 4, 0, 0}, {"Xing", 5, 1000, 1}, {"Xing", 2, 9, 0}};
		for (unsigned k = 0; k < sizeof xs / sizeof xs[0]; k++) {
			if ((unsigned)i % 4 != k % 4 && i > 2)
				continue;
			size_t m = mkxing(b, f, n, xs[k].id, xs[k].flags, xs[k].frames, xs[k].crc);
			if (!m)
				continue;
			snprintf(nm, sizeof nm, "%s.x%u", fname[i], k);
			decode_all(ref, nm, b, m, k & 1, 1);
		}
		/* ID3v2 devant, ID3v1 derrière */
		static const uint8_t id3[] = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 60, 'T', 'I', 'T', '2', 0, 0, 0, 7, 0, 0,
					      0, 'T', 'i', 't', 'r', 'e', ' ', 'T', 'P', 'E', '1', 0, 0, 0, 5, 0, 0, 1,
					      0xFF, 0xFE, 'A', 0, 'B', 0, 'T', 'A', 'L', 'B', 0, 0, 0, 4, 0, 0, 3, 'A',
					      'l', 'b'};
		memset(b, 0, 70);
		memcpy(b, id3, sizeof id3);
		memcpy(b + 70, f, n);
		size_t m = 70 + n;
		if (i & 1) {
			memset(b + m, ' ', 128);
			memcpy(b + m, "TAGTitre v1", 11);
			memcpy(b + m + 33, "Artiste v1", 10);
			memcpy(b + m + 63, "Album", 5);
			m += 128;
		}
		snprintf(nm, sizeof nm, "%s.id3", fname[i]);
		decode_all(ref, nm, b, m, i & 2, i < 3);
		/* ID3v1 seul */
		memcpy(b, f, n);
		memset(b + n, 0, 128);
		memcpy(b + n, "TAG\xe9t\xe9", 6);
		snprintf(nm, sizeof nm, "%s.v1", fname[i]);
		decode_all(ref, nm, b, n + 128, 0, 0);
		/* ordures devant (avec de fausses synchros) */
		rng = 33 + i;
		size_t g = 1 + rnd() % 3000;
		for (size_t j = 0; j < g; j++) {
			b[j] = rnd();
			if (j % 97 == 5)
				b[j] = 0xFF;
		}
		memcpy(b + g, f, n);
		snprintf(nm, sizeof nm, "%s.junk", fname[i]);
		decode_all(ref, nm, b, g + n, i & 1, 0);
		/* coupé */
		snprintf(nm, sizeof nm, "%s.cut", fname[i]);
		decode_all(ref, nm, f, n * 2 / 3 + 13, i & 1, 0);
		decode_all(ref, nm, f, 300, 0, 0);
		/* octets changés */
		for (int k = 0; k < 4; k++) {
			rng = 1000 * i + k;
			memcpy(b, f, n);
			int cnt = k == 0 ? 3 : k == 1 ? 40 : k == 2 ? 400 : 4000;
			for (int j = 0; j < cnt; j++) {
				size_t at = rnd() % n;
				if (k == 3 || at > 64)
					b[at] = rnd();
			}
			snprintf(nm, sizeof nm, "%s.bad%d", fname[i], k);
			decode_all(ref, nm, b, n, (k + i) & 1, k < 2);
		}
	}
	/* rien de bon */
	static const uint8_t junk[] = {0xFF, 0xFB, 0xF0, 0x00, 0xFF, 0xE2, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0};
	decode_all(ref, "junk", junk, sizeof junk, 0, 1);
	memset(b, 0, 5000);
	memcpy(b, "ID3\3\0\0\0\0\0\20", 10);
	decode_all(ref, "id3only", b, 5000, 0, 1);
	if (nfiles) { /* une seule trame */
		long fl = frame_len(files[0]);
		decode_all(ref, "one", files[0], fl, 0, 1);
		decode_all(ref, "onep3", files[0], fl + 3, 1, 1);
	}
}

/* flux fabriqués par l'encodeur MP3 en asm (aarch64 seulement), écrits sur disque */
struct wbuf {
	uint8_t *p;
	long n;
};
static long w_write(long ctx, const void *b, long n)
{
	struct wbuf *w = (struct wbuf *)ctx;
	if (w->n + n > (8 << 20))
		return -5;
	memcpy(w->p + w->n, b, n);
	w->n += n;
	return n;
}

/* sinus approché (pas de libm dans le banc) */
static double xsin(double x)
{
	x -= 6.283185307179586 * (double)(long)(x / 6.283185307179586);
	if (x > 3.141592653589793)
		x -= 6.283185307179586;
	double x2 = x * x;
	return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}

static void mkenc(void)
{
	static const int rates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
	static const int brs[] = {0, 8000, 160000, 64000};
	struct wbuf w = {malloc(8 << 20), 0};
	int16_t *s = malloc(48000 * 2 * 2 * 2);
	for (int ri = 0; ri < 9; ri++)
		for (int ch = 1; ch <= 2; ch++) {
			int rate = rates[ri], br = brs[(ri + ch) % 4];
			if (br == 8000 && rate > 24000) /* (MPEG-1 : pas de 8 kbit/s) */
				br = 32000;
			int frames = rate * 3 / 2;
			rng = rate + ch;
			for (int i = 0; i < frames; i++) /* deux sinus, un balayage, du bruit, des silences */
				for (int c = 0; c < ch; c++) {
					double t = (double)i / rate;
					double v = 0.3 * xsin(6.2831853 * (220 + 110 * c) * t) +
						   0.2 * xsin(6.2831853 * (200 + 3000 * t) * t) +
						   (int)(rnd() % 2001 - 1000) / 8000.0;
					if ((i / 4000) % 3 == 2)
						v *= (c ? 0.0 : 0.05);
					s[i * ch + c] = (int16_t)(v * 30000);
				}
			w.n = 0;
			long e = AKCALL(1, ak_enc_open_io, refcb(1, 2, (void *)w_write), 0, &w, AKF_MP3, rate, ch, br);
			if (e < 0) /* (débit trop bas : pas de fichier, « absent » des deux côtés) */
				continue;
			for (int i = 0; i < frames;) {
				int k = 1000 + i % 777;
				if (k > frames - i)
					k = frames - i;
				AKCALL(1, ak_enc_write, e, s + i * ch, k);
				i += k;
			}
			AKCALL(1, ak_enc_close, e);
			char path[128];
			snprintf(path, sizeof path, DIR "enc_%d_%d.mp3", rate, ch);
			FILE *f = fopen(path, "wb");
			if (f) {
				fwrite(w.p, 1, w.n, f);
				fclose(f);
			}
		}
	free(s);
	free(w.p);
}

int main(void)
{
	static const char *fz[] = {"m1",	 "m1long", "m1short", "m1mixed", "m1ms",       "m1is",	   "m1isshort",
				   "m1ismixed", "m1nost", "m1noresv", "lsf",	"lsfis",      "lsfisshort", "lsfismixed",
				   "lsfms",	 "l25",	   "l25is",   "l25mixed", "l25short"};
	static const int rates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
	char path[128], name[48];
	pcm = malloc(PCMMAX);
	if (cmp_has_ref)
		mkenc();
	addfile("skel/sd0/res/audiotest/jfk.mp3", "jfk");
	addfile("skel/sd0/res/audiotest/enc128.mp3", "enc128");
	for (unsigned i = 0; i < sizeof fz / sizeof fz[0]; i++) {
		snprintf(path, sizeof path, DIR "fz_%s.mp3", fz[i]);
		snprintf(name, sizeof name, "fz_%s", fz[i]);
		addfile(path, name);
	}
	for (int ri = 0; ri < 9; ri++)
		for (int ch = 1; ch <= 2; ch++) {
			snprintf(path, sizeof path, DIR "enc_%d_%d.mp3", rates[ri], ch);
			snprintf(name, sizeof name, "enc_%d_%d", rates[ri], ch);
			addfile(path, name);
		}
	RUN(t_files);
	RUN(t_packets);
	RUN(t_variants);
	return cmp_end();
}
