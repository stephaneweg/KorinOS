/* ---- les essais (audtest.c : en-tête, sortie, appels ; ce fichier est inclus) */

#define AKF_WAV 1
#define AKF_FLAC 2
#define AKF_MP3 3
#define AKF_VORBIS 4
#define AKF_OPUS 5
#define AKS_FM 1
#define AKS_NOFX 2
#define AKP_PLAYING 1
#define AKP_PAUSED 2
#define AKP_ENDED 3
#define AKP_ERROR 4

/* fichiers d'essai embarqués (chemins depuis la racine du dépôt) */
#define EMBED(sym, path)                                       \
	__asm__(".section .rodata\n.balign 16\n.global " #sym "\n" #sym ":\n.incbin \"" path "\"\n" \
		".global " #sym "_end\n" #sym "_end:\n.byte 0\n.balign 16\n.text\n"); \
	extern const unsigned char sym[], sym##_end[];
EMBED(e_wav, "skel/sd0/res/audiotest/w8m.wav")
EMBED(e_flac, "skel/sd0/res/audiotest/f16s.flac")
EMBED(e_jfk, "skel/sd0/res/audiotest/jfk.mp3")
EMBED(e_enc, "skel/sd0/res/audiotest/enc128.mp3")
EMBED(e_kc, "skel/sd0/res/audiotest/kenney_confirm.ogg")
EMBED(e_ks, "skel/sd0/res/audiotest/kenney_select.ogg")
EMBED(e_silk, "skel/sd0/res/audiotest/silk16.opus")
EMBED(e_celt, "skel/sd0/res/audiotest/celt64.opus")
EMBED(e_fms, "src/apps/fmtracker/songs/GUITAR.FMS")
EMBED(e_fms2, "src/apps/fmtracker/songs/MKMORT.FMS")
EMBED(e_sf2, "src/bin/synthtest/test.sf2")

struct emb {
	const char *name;
	const unsigned char *p, *e;
};
static const struct emb *embs(int i)
{
	static struct emb t[11];
	if (!t[0].name) {
		struct emb v[11] = {{"w8m.wav", e_wav, e_wav_end},	    {"f16s.flac", e_flac, e_flac_end},
				    {"jfk.mp3", e_jfk, e_jfk_end},	    {"enc128.mp3", e_enc, e_enc_end},
				    {"kenney_confirm.ogg", e_kc, e_kc_end}, {"kenney_select.ogg", e_ks, e_ks_end},
				    {"silk16.opus", e_silk, e_silk_end},    {"celt64.opus", e_celt, e_celt_end},
				    {"GUITAR.FMS", e_fms, e_fms_end},	    {"MKMORT.FMS", e_fms2, e_fms2_end},
				    {"test.sf2", e_sf2, e_sf2_end}};
		for (int k = 0; k < 11; k++)
			t[k] = v[k];
	}
	return &t[i];
}
#define NEMB_DEC 10

static unsigned char work[1 << 21];
static unsigned char work2[1 << 21];
static short pcm[8 * 4096];

/* ---- FMA de l'émulateur
 *
 * QEMU 11.1 (raspi4b, hôte Windows) calcule FMADD / FMLA (simple et double) de deux façons :
 * tant que le drapeau « inexact » interne de son état flottant est baissé, en logiciel (exact,
 * fusionné) ; une fois levé (après la première opération inexacte), par la FMA de l'hôte, qui
 * n'est pas exacte (non fusionnée ou mal arrondie : environ 30 % des triplets au hasard
 * diffèrent d'un ulp). Chaque écriture de FPSR (msr fpsr : le noyau la fait à chaque
 * changement de contexte, sched.asm, en rechargeant l'état FP du thread) baisse ce drapeau
 * interne sans changer le FPSR vu par le programme. Donc la première FMA après chaque reprise
 * du thread est exacte, les suivantes non : le résultat dépend du moment des préemptions.
 * C'est le cas du kit en asm comme du kit en C (mêmes FMA aux mêmes endroits), d'un passage
 * à l'autre : sorties F32 des décodeurs (MP3, Vorbis, Opus) qui changent de 1 à 4 ulp sur
 * quelques échantillons, octets des encodeurs MP3 et Opus (un ulp change une décision).
 * Le S16 n'en voit rien en pratique (l'écart est loin d'un demi-pas de 2^-15).
 * Pas d'effet sur le vrai Pi (FMA exacte) ni sur le banc Linux (qemu-aarch64 d'un hôte Linux).
 *
 * L'essai sonde l'émulateur (fma_probe) : si la FMA dépend de l'état, il n'écrit pas les sommes
 * qui en dépendent (F32 comparé au S16 avec une tolérance, encodages lossy vérifiés par la
 * relecture sans sommes) ; sinon (vrai Pi), il écrit tout. */
static int fma_ok = 1;

/* fmadd (double si dbl) de a, b, c ; ixc : après une division inexacte, sinon juste après
 * msr fpsr (drapeau interne de QEMU baissé) */
static uint64_t fma_one(uint64_t a, uint64_t b, uint64_t c, int ixc, int dbl)
{
#if defined(__aarch64__)
	uint64_t r = 0;
	if (dbl)
		__asm__ volatile("msr fpsr, xzr\n\tcbz %w4, 1f\n\tfmov s4, #1.0\n\tfmov s5, #3.0\n\tfdiv s4, s4, s5\n"
				 "1:\tfmov d0, %1\n\tfmov d1, %2\n\tfmov d2, %3\n\tfmadd d0, d0, d1, d2\n\tfmov %0, d0\n"
				 "\tmsr fpsr, xzr"
				 : "=&r"(r)
				 : "r"(a), "r"(b), "r"(c), "r"(ixc)
				 : "v0", "v1", "v2", "v4", "v5");
	else
		__asm__ volatile("msr fpsr, xzr\n\tcbz %w4, 1f\n\tfmov s4, #1.0\n\tfmov s5, #3.0\n\tfdiv s4, s4, s5\n"
				 "1:\tfmov s0, %w1\n\tfmov s1, %w2\n\tfmov s2, %w3\n\tfmadd s0, s0, s1, s2\n\tfmov %w0, s0\n"
				 "\tmsr fpsr, xzr"
				 : "=&r"(r)
				 : "r"(a), "r"(b), "r"(c), "r"(ixc)
				 : "v0", "v1", "v2", "v4", "v5");
	return r;
#else
	(void)a, (void)b, (void)c, (void)ixc, (void)dbl;
	return 0;
#endif
}

/* -> 1 si la FMA ne dépend pas de l'état (32 essais : une préemption entre la division et la
 * FMA ne peut pas les fausser tous) ; écrit « fma exacte=... » */
static void fma_probe(void)
{
	/* triplets où la FMA de QEMU diffère du résultat exact (relevés le 12/11) */
	static const uint64_t t[2][4] = {{0x3eeedfa7, 0x40adf043, 0x40af437e, 0x41003514},
					 {0x4037324f429f561fULL, 0x4007fd911bc71edeULL, 0x3fec880f492e9438ULL,
					  0x40519d0809358ecfULL}};
	int ok = 1, exact = 1;
	for (int i = 0; i < 32; i++)
		for (int d = 0; d < 2; d++) {
			uint64_t s = fma_one(t[d][0], t[d][1], t[d][2], 0, d);
			uint64_t h = fma_one(t[d][0], t[d][1], t[d][2], 1, d);
			if (s != h)
				ok = 0;
			if (s != t[d][3])
				exact = 0;
		}
#if !defined(__aarch64__)
	exact = 1;
#endif
	fma_ok = ok;
	kv("fma exacte", ok && exact);
	kv("fma selon l'etat", !ok);
}

/* float (bits) x 2^31, entier (|f| >= 2 : saturé) ; NaN, infini -> *bad */
static int64_t f_fix31(uint32_t v, long *bad)
{
	int e = (int)((v >> 23) & 255);
	int64_t m = (int64_t)((v & 0x7FFFFF) | 0x800000), x;
	if (e == 255) {
		(*bad)++;
		return 0;
	}
	if (!e)
		return 0;
	int sh = e - 119; /* m x 2^(e - 127 - 23) x 2^31 */
	if (sh >= 8)
		x = (int64_t)1 << 32;
	else if (sh >= 0)
		x = m << sh;
	else if (sh > -40)
		x = m >> -sh;
	else
		x = 0;
	return v >> 31 ? -x : x;
}

/* sortie S16 d'un passage (pour comparer le F32 au S16 quand la FMA dépend de l'état) */
static short ref16[1 << 20];
static long ref16_n;

/* échantillons F32 (o) loin du S16 de référence (ref16 depuis k) de plus d'un demi-pas
 * (+ 2^-10 pas) : 0 attendu ; NaN, infini comptés */
static long f32_vs_s16(const float *o, long n, long k)
{
	long bad = 0;
	for (long i = 0; i < n; i++) {
		uint32_t v;
		memcpy(&v, &o[i], 4);
		int64_t x = f_fix31(v, &bad); /* f x 32768 x 65536 */
		if (x > (int64_t)32767 << 16)
			x = (int64_t)32767 << 16;
		if (x < -((int64_t)32768 << 16))
			x = -((int64_t)32768 << 16);
		if (k + i >= ref16_n)
			continue;
		int64_t d = x - ((int64_t)ref16[k + i] << 16);
		if (d < 0)
			d = -d;
		if (d > 32768 + 64)
			bad++;
	}
	return bad;
}

/* ---- décodeur complet : sommes du PCM, informations, étiquettes, recherches
 * dec_noh : pas de sommes du PCM (relecture d'un encodage lossy quand la FMA dépend de l'état :
 * voir fma_probe) ; à la place, la puissance du PCM relu comparée à dec_rms2 (carré moyen de
 * l'entrée de l'encodeur) : « son=1 » entre le quart et quatre fois */
static int dec_noh;
static uint64_t dec_rms2;
static void dec_all(const char *what, long d, long maxframes)
{
	long info[8];
	C2(X_DEC_INFO, d, info);
	ps(what);
	kv("fmt", info[0]);
	kv("rate", info[1]);
	kv("ch", info[2]);
	kv("frames", info[3]);
	kv("ms", info[4]);
	kv("bps", info[5]);
	kv("bits", info[7]);
	nl();
	char tag[80];
	for (int t = 0; t < 3; t++) {
		long k = C4(X_DEC_TAG, d, t, tag, t == 2 ? 5 : sizeof tag);
		ps("  tag");
		pd(t);
		pc(' ');
		pq(tag, k);
		nl();
	}
	long ch = info[2] > 0 && info[2] <= 8 ? info[2] : 1;
	uint32_t h = 2166136261u;
	long tot = 0, chunk = 1, r = 0;
	uint64_t pw = 0;
	while (tot < maxframes) {
		r = C3(X_DEC_READ, d, pcm, chunk);
		if (r <= 0)
			break;
		h = fnv_mix(h, fnv(pcm, r * 2 * ch));
		for (long i = 0; i < r * ch; i++)
			pw += (uint64_t)((long)pcm[i] * pcm[i]);
		tot += r;
		chunk = chunk * 3 + 1;
		if (chunk > 4096)
			chunk = 7;
	}
	ps("  pcm");
	kv("frames", tot);
	kv("end", r);
	if (dec_noh) {
		uint64_t m = tot ? pw / (uint64_t)(tot * ch) : 0;
		kv("son", m >= dec_rms2 / 4 && m <= dec_rms2 * 4);
	} else {
		kh("h", h);
	}
	kv("tell", C1(X_DEC_TELL, d));
	nl();
	static const long ms[] = {0, 40, 333, -7, 999999, 1500};
	for (unsigned i = 0; i < sizeof ms / sizeof ms[0]; i++) {
		long s = C2(X_DEC_SEEK, d, ms[i]);
		long t1 = C1(X_DEC_TELL, d);
		long k = C3(X_DEC_READ, d, pcm, 500);
		ps("  seek");
		kv("ms", ms[i]);
		kv("r", s);
		kv("tell", t1);
		kv("n", k);
		if (k > 0 && !dec_noh)
			kh("h", fnv(pcm, k * 2 * ch));
		nl();
	}
	kv("close", C1(X_DEC_CLOSE, d));
	nl();
}

static void test_decode(void)
{
	ps("=== décodage en mémoire\n");
	for (int i = 0; i < NEMB_DEC; i++) {
		const struct emb *e = embs(i);
		long n = e->e - e->p;
		for (int v = 0; v < 3; v++) {
			const unsigned char *src = e->p;
			long len = n;
			if (v == 1)
				len = n * 3 / 5; /* coupé */
			if (v == 2) {	 /* abîmé : quelques octets changés (pas l'en-tête) */
				memcpy(work, e->p, n);
				uint64_t s = lcg;
				lcg = 1000 + i;
				for (int k = 0; k < 6; k++)
					work[200 + rnd() % (n - 200)] = (unsigned char)rnd();
				lcg = s;
				src = work;
			}
			long d = C2(X_DEC_OPEN_MEM, src, len);
			ps(e->name);
			ps(v == 0 ? " " : v == 1 ? " (coupé) " : " (abîmé) ");
			kv("open", d < 0 ? d : 1);
			nl();
			if (d >= 0)
				dec_all("  ", d, i >= 8 ? 120000 : 2000000);
		}
	}
	/* WAV fabriqués : 8, 24, 32 bits, flottant */
	static const int fm[][3] = {{1, 8, 1}, {1, 24, 2}, {1, 32, 3}, {3, 32, 2}};
	for (int k = 0; k < 4; k++) {
		unsigned char *b = work;
		int ch = fm[k][2], bits = fm[k][1], frames = 3000;
		long data = (long)frames * ch * bits / 8;
		C5(X_WAV_HEADER, b, 22050, ch, bits, data);
		b[20] = (unsigned char)fm[k][0];
		for (long i = 0; i < data; i++)
			b[44 + i] = (unsigned char)rnd();
		if (fm[k][0] == 3)
			for (long i = 0; i < frames * ch; i++) {
				uint32_t f = 0x3E000000u + (rnd() & 0x00FFFFFF);
				if (i & 1)
					f |= 0x80000000u;
				memcpy(b + 44 + 4 * i, &f, 4);
			}
		long d = C2(X_DEC_OPEN_MEM, b, 44 + data);
		ps("wav");
		pd(k);
		kv("open", d < 0 ? d : 1);
		nl();
		if (d >= 0)
			dec_all("  ", d, 100000);
	}
	/* MIDI fabriqué (format 1, deux pistes, tempo, statut courant) */
	static const unsigned char mid[] = {
		'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0, 11, 0, 0xFF, 0x51, 3,
		0x07, 0xA1, 0x20, 0, 0xFF, 0x2F, 0, 'M', 'T', 'r', 'k', 0, 0, 0, 34, 0, 0xC0, 19, 0, 0x90, 60, 100,
		48, 64, 90, 48, 67, 80, 96, 60, 0, 0, 64, 0, 0, 67, 0, 0, 0xE0, 0, 0x50, 0x20, 0x80, 72, 0, 0, 0xFF,
		0x2F, 0};
	long d = C2(X_DEC_OPEN_MEM, mid, sizeof mid);
	ps("mid");
	kv("open", d < 0 ? d : 1);
	nl();
	if (d >= 0)
		dec_all("  ", d, 400000);
	long junk = C2(X_DEC_OPEN_MEM, "junk junk junk junk junk", 24);
	ps("inconnu");
	kv("open", junk);
	nl();
}

/* ---- API par paquets */

static long ogg_packets(const unsigned char *b, long n, long *offs, long *lens, unsigned char *out, long max)
{
	long p = 0, np = 0, o = 0, cur = -1;
	while (p + 27 <= n && np < max) {
		if (memcmp(b + p, "OggS", 4))
			break;
		int ns = b[p + 26];
		const unsigned char *lace = b + p + 27;
		const unsigned char *body = lace + ns;
		long bo = 0;
		for (int s = 0; s < ns; s++) {
			if (cur < 0) {
				cur = o;
			}
			memcpy(out + o, body + bo, lace[s]);
			o += lace[s];
			bo += lace[s];
			if (lace[s] < 255) {
				offs[np] = cur;
				lens[np] = o - cur;
				np++;
				cur = -1;
				if (np >= max)
					break;
			}
		}
		p += 27 + ns + bo;
	}
	return np;
}

static long offs[4096], lens[4096];

/* f32 = 0 : garde le S16 dans ref16 ; f32 = 1 juste après, sur les mêmes paquets : si la FMA
 * dépend de l'état (fma_probe), le F32 est comparé à ce S16 (« loin » : échantillons à plus
 * d'un demi-pas, 0 attendu) au lieu d'être haché */
static void codec_run(const char *what, long h, long np, const unsigned char *pk, int first, int f32)
{
	uint32_t hs = 2166136261u;
	long tot = 0, errs = 0, far = 0, k = 0;
	if (!f32)
		ref16_n = 0;
	for (long i = first; i < np; i++) {
		long r = C6(X_CODEC_DECODE, h, pk + offs[i], lens[i], pcm, 4096 / 2, f32);
		if (r < 0) {
			errs++;
			continue;
		}
		long info[8];
		C2(X_CODEC_INFO, h, info);
		long n = r * info[2];
		hs = fnv_mix(hs, fnv(pcm, n * (f32 ? 4 : 2)));
		if (!f32) {
			for (long j = 0; j < n && ref16_n < (long)(sizeof ref16 / sizeof ref16[0]); j++)
				ref16[ref16_n++] = pcm[j];
		} else {
			far += f32_vs_s16((const float *)pcm, n, k);
			k += n;
		}
		tot += r;
		if (i == np / 2)
			C1(X_CODEC_RESET, h);
	}
	ps(what);
	kv("f32", f32);
	kv("frames", tot);
	kv("err", errs);
	if (f32 && !fma_ok)
		kv("loin", far);
	else
		kh("h", hs);
	nl();
}

static long mp3_frame_len(const unsigned char *p)
{
	static const short b1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
	static const short b2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
	static const int sr[3] = {44100, 48000, 32000};
	if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0)
		return 0;
	int ver = (p[1] >> 3) & 3; /* 3 : MPEG-1, 2 : MPEG-2, 0 : 2.5 */
	int bi = p[2] >> 4, si = (p[2] >> 2) & 3, pad = (p[2] >> 1) & 1;
	if (ver == 1 || si == 3 || !bi || bi == 15 || ((p[1] >> 1) & 3) != 1)
		return 0;
	long rate = sr[si] >> (ver == 3 ? 0 : ver == 2 ? 1 : 2);
	if (ver == 3)
		return 144000L * b1[bi] / rate + pad;
	return 72000L * b2[bi] / rate + pad;
}

static void test_codec(void)
{
	ps("=== API par paquets\n");
	for (int f = 2; f <= 3; f++) { /* MP3 : trame par trame */
		const struct emb *e = embs(f);
		const unsigned char *p = e->p;
		long n = e->e - e->p, o = 0, np = 0;
		if (!memcmp(p, "ID3", 3))
			o = 10 + ((long)p[6] << 21 | (long)p[7] << 14 | (long)p[8] << 7 | p[9]);
		while (o + 4 <= n && np < 4096) {
			long l = mp3_frame_len(p + o);
			if (!l) {
				o++;
				continue;
			}
			offs[np] = o;
			lens[np] = o + l <= n ? l : n - o;
			np++;
			o += l;
		}
		for (int f32 = 0; f32 < 2; f32++) {
			long h = C5(X_CODEC_OPEN, AKF_MP3, 44100, 2, 0, 0);
			ps(e->name);
			kv("frames", np);
			kv("open", h < 0 ? h : 1);
			nl();
			if (h >= 0) {
				codec_run("  mp3", h, np, p, 0, f32);
				kv("close", C1(X_CODEC_CLOSE, h));
				nl();
			}
		}
	}
	for (int f = 4; f <= 7; f++) { /* Vorbis, Opus : paquets de l'Ogg */
		const struct emb *e = embs(f);
		long np = ogg_packets(e->p, e->e - e->p, offs, lens, work2, 4096);
		int opus = f >= 6;
		for (int f32 = 0; f32 < 2; f32++) {
			long h, first;
			if (opus) {
				h = C5(X_CODEC_OPEN, AKF_OPUS, 48000, 2, work2 + offs[0], lens[0]);
				first = 2;
			} else { /* en-têtes Xiph : 02, deux tailles en laçage, trois en-têtes */
				unsigned char *x = work;
				long k = 0;
				x[k++] = 2;
				for (int j = 0; j < 2; j++) {
					long l = lens[j];
					while (l >= 255) {
						x[k++] = 255;
						l -= 255;
					}
					x[k++] = (unsigned char)l;
				}
				for (int j = 0; j < 3; j++) {
					memcpy(x + k, work2 + offs[j], lens[j]);
					k += lens[j];
				}
				h = C5(X_CODEC_OPEN, AKF_VORBIS, 44100, 2, x, k);
				first = 3;
			}
			ps(e->name);
			kv("packets", np);
			kv("open", h < 0 ? h : 1);
			nl();
			if (h >= 0) {
				codec_run(opus ? "  opus" : "  vorbis", h, np, work2, first, f32);
				kv("close", C1(X_CODEC_CLOSE, h));
				nl();
			}
		}
	}
	ps("format refusé");
	kv("open", C5(X_CODEC_OPEN, AKF_WAV, 44100, 2, 0, 0));
	long fh = C5(X_CODEC_OPEN, AKF_FLAC, 44100, 2, 0, 0);
	kv("flac sans données", fh < 0 ? fh : 1);
	if (fh >= 0)
		kv("close", C1(X_CODEC_CLOSE, fh));
	nl();
}

/* ---- encodage en mémoire */

static long sink_pos, sink_end, sink_writes, sink_seeks;
static long sink_write(long ctx, const void *p, long n)
{
	(void)ctx;
	sink_writes++;
	if (sink_pos + n > (long)sizeof work2)
		n = (long)sizeof work2 - sink_pos;
	memcpy(work2 + sink_pos, p, n);
	sink_pos += n;
	if (sink_pos > sink_end)
		sink_end = sink_pos;
	return n;
}
static long sink_seek(long ctx, long pos)
{
	(void)ctx;
	sink_seeks++;
	sink_pos = pos;
	return 0;
}

static void gen_pcm(short *o, long frames, int ch, int kind)
{
	long ph = 0;
	for (long i = 0; i < frames; i++)
		for (int c = 0; c < ch; c++) {
			long v;
			ph += 1000 + 300 * c + (i >> 4);
			long t = (ph >> 4) & 1023; /* triangle */
			v = t < 512 ? t * 40 - 10240 : (1023 - t) * 40 - 10240;
			if (kind == 1)
				v = (long)(rnd() & 0x3FFF) - 8192;
			if (kind == 2 && (i % 2000) < 30)
				v = 32767;
			o[i * ch + c] = (short)v;
		}
}

static void test_encode(void)
{
	ps("=== encodage en mémoire\n");
	static const long cs[][5] = {
		{AKF_WAV, 44100, 2, 0, 0}, {AKF_WAV, 8000, 1, 0, 1},	 {AKF_MP3, 44100, 2, 0, 0},
		{AKF_MP3, 48000, 1, 64000, 1}, {AKF_MP3, 22050, 2, 32000, 2}, {AKF_MP3, 8000, 1, 0, 0},
		{AKF_MP3, 32000, 2, 320000, 1}, {AKF_OPUS, 48000, 2, 0, 0}, {AKF_OPUS, 44100, 1, 24000, 1},
		{AKF_OPUS, 16000, 2, 96000, 2}, {AKF_OPUS, 8000, 1, 0, 0},  {AKF_MP3, 44100, 3, 0, 0},
		{AKF_MP3, 7000, 1, 0, 0},	 {9, 44100, 2, 0, 0},
	};
	for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; i++) {
		sink_pos = sink_end = sink_writes = sink_seeks = 0;
		long e = call(X_ENC_OPEN_IO, L(sink_write), L(i & 1 ? 0 : (long)sink_seek), 0, cs[i][0], cs[i][1],
			      cs[i][2], cs[i][3]);
		ps("enc");
		pd(i);
		kv("fmt", cs[i][0]);
		kv("rate", cs[i][1]);
		kv("ch", cs[i][2]);
		kv("open", e < 0 ? e : 1);
		if (e >= 0) {
			long total = 0, w = 0;
			uint64_t pw = 0;
			int ch = (int)cs[i][2];
			for (int k = 0; k < 9; k++) {
				long fr = (k * 977) % 3000;
				gen_pcm(pcm, fr, ch, (int)cs[i][4]);
				for (long j = 0; j < fr * ch; j++)
					pw += (uint64_t)((long)pcm[j] * pcm[j]);
				w |= C3(X_ENC_WRITE, e, pcm, fr);
				total += fr;
			}
			/* MP3, Opus : octets selon les FMA (voir fma_probe) */
			int noh = !fma_ok && cs[i][0] != AKF_WAV;
			kv("write", w);
			kv("size", C1(X_ENC_SIZE, e));
			kv("close", C1(X_ENC_CLOSE, e));
			kv("bytes", sink_end);
			kv("writes", sink_writes);
			kv("seeks", sink_seeks);
			if (!noh)
				kh("h", fnv(work2, sink_end));
			nl();
			/* relecture */
			memcpy(work, work2, sink_end);
			long d = C2(X_DEC_OPEN_MEM, work, sink_end);
			ck(d >= 0, "relecture de l'encode");
			if (d >= 0) {
				dec_noh = noh;
				dec_rms2 = total ? pw / (uint64_t)(total * ch) : 0;
				dec_all("  relu", d, 100000);
				dec_noh = 0;
			} else {
				kv("  relu", d);
				nl();
			}
		} else {
			nl();
		}
	}
}

/* ---- synthèse FM */

static void test_fm(void)
{
	ps("=== FM\n");
	for (int k = -2; k < 140; k += 7)
		kv("mhz", C1(X_NOTE_MHZ, k));
	nl();
	for (int o = -1; o < 10; o += 2)
		for (int n = -1; n < 13; n += 5)
			kv("oct", C2(X_NOTE_OCTAVE_MHZ, n, o));
	nl();
	long bad = C1(X_FM_NEW, 65);
	kv("new65", bad < 0 ? bad : 1);
	long fm = C1(X_FM_NEW, 0);
	kv("new0", fm < 0 ? fm : 1);
	nl();
	unsigned char inst[24], fms[26];
	for (int v = 0; v < 16; v++) {
		for (int j = 0; j < 24; j++)
			inst[j] = (unsigned char)(rnd() & (j == 7 || j == 17 ? 3 : j == 8 || j == 18 ? 15 : 15));
		inst[4] = (unsigned char)(rnd() % 64);
		inst[14] = (unsigned char)(rnd() % 64);
		inst[20] = (unsigned char)(rnd() & 7);
		inst[21] = (unsigned char)(rnd() & 1);
		if (v >= 8) { /* instruments FM Song */
			const unsigned char *s = embs(8)->p;
			memcpy(fms, s + 40 + (v * 26) % 300, 26);
			kv("fms", C2(X_FM_FROM_FMS, inst, fms));
		}
		kv("inst", C3(X_FM_INSTRUMENT, fm, v, inst));
	}
	kv("inst-bad", C3(X_FM_INSTRUMENT, fm, 99, inst));
	nl();
	uint32_t h = 2166136261u;
	for (int step = 0; step < 60; step++) {
		int v = step % 16;
		if (step % 3 == 0)
			C5(X_FM_START, fm, v, C1(X_NOTE_MHZ, 36 + (step * 5) % 50), step % 6, 100 + step);
		if (step % 5 == 1)
			C3(X_FM_PAN, fm, v, (step * 37) % 256);
		if (step % 7 == 2)
			C3(X_FM_PITCH, fm, v, C1(X_NOTE_MHZ, 40 + step % 30));
		if (step % 4 == 3)
			C3(X_FM_GAIN, fm, v, (step * 11) % 256);
		if (step % 9 == 4)
			C2(X_FM_STOP, fm, v);
		if (step == 40)
			C1(X_FM_SILENCE, fm);
		if (step == 50)
			C2(X_FM_STOP, fm, -1);
		long fr = 1 + (step * 331) % 1500;
		C3(X_FM_RENDER, fm, pcm, fr);
		h = fnv_mix(h, fnv(pcm, fr * 4));
		h = fnv_mix(h, C1(X_FM_ACTIVE, fm));
	}
	kh("render", h);
	kv("free", C1(X_FM_FREE, fm));
	nl();
}

/* ---- synthé MIDI */

static void synth_seq(long sy, const char *what)
{
	uint32_t h = 2166136261u, hf = 2166136261u;
	static float fl[2048], fr[2048];
	for (int step = 0; step < 80; step++) {
		int c = step % 16;
		C5(X_SYNTH_MIDI, sy, c, 0xC0, (step * 7) % 128, 0);
		C5(X_SYNTH_MIDI, sy, c, 0x90, 40 + (step * 13) % 50, 60 + step % 60);
		if (step % 3 == 0)
			C5(X_SYNTH_MIDI, sy, c, 0xB0, step % 2 ? 7 : 10, (step * 9) % 128);
		if (step % 4 == 1)
			C5(X_SYNTH_MIDI, sy, c, 0xE0, step % 128, (step * 3) % 128);
		if (step % 5 == 2)
			C5(X_SYNTH_MIDI, sy, (c + 3) % 16, 0x80, 40 + (step * 13) % 50, 0);
		if (step == 60)
			C5(X_SYNTH_MIDI, sy, 0, 0xB0, 64, 127);
		long n = 1 + (step * 211) % 1024;
		C3(X_SYNTH_RENDER, sy, pcm, n);
		h = fnv_mix(h, fnv(pcm, n * 4));
		if (step % 10 == 9) {
			C4(X_SYNTH_RENDER_F32, sy, fl, fr, 700);
			hf = fnv_mix(hf, fnv(fl, 2800));
			hf = fnv_mix(hf, fnv(fr, 2800));
		}
		h = fnv_mix(h, C1(X_SYNTH_VOICES, sy));
	}
	ps(what);
	kh("s16", h);
	kh("f32", hf);
	kv("reset", C1(X_SYNTH_RESET, sy));
	kv("voices", C1(X_SYNTH_VOICES, sy));
	nl();
}

static void test_synth(void)
{
	ps("=== synthé\n");
	long sy = C2(X_SYNTH_NEW, 0, AKS_FM);
	kv("new", sy < 0 ? sy : 1);
	nl();
	if (sy >= 0) {
		synth_seq(sy, "fm");
		kv("free", C1(X_SYNTH_FREE, sy));
		nl();
	}
	sy = C2(X_SYNTH_NEW, 0, AKS_FM | AKS_NOFX);
	if (sy >= 0) {
		synth_seq(sy, "fm-nofx");
		C1(X_SYNTH_FREE, sy);
	}
	const struct emb *e = embs(10);
	long sf = C2(X_SF_OPEN_MEM, e->p, e->e - e->p);
	kv("sf", sf < 0 ? sf : 1);
	if (sf >= 0) {
		const char *nm = (const char *)C1(X_SF_NAME, sf);
		pc(' ');
		pq(nm, slen(nm));
		long np = C1(X_SF_PRESETS, sf);
		kv("presets", np);
		unsigned char pr[32];
		for (long i = -1; i <= np; i++) {
			memset(pr, 0, 32);
			kv("preset", C3(X_SF_PRESET, sf, i, pr));
			kh("p", fnv(pr, 32));
		}
		nl();
		for (int fx = 0; fx < 2; fx++) {
			sy = C2(X_SYNTH_NEW, sf, fx ? AKS_NOFX : 0);
			if (sy >= 0) {
				synth_seq(sy, fx ? "sf-nofx" : "sf");
				C1(X_SYNTH_FREE, sy);
			}
		}
		kv("sf_free", C1(X_SF_FREE, sf));
	}
	memcpy(work, e->p, 64);
	kv("sf-court", C2(X_SF_OPEN_MEM, work, 40));
	nl();
}

/* ---- outils */

static void test_tools(void)
{
	ps("=== outils\n");
	fma_probe();
	nl();
	for (int v = 0; v <= 120; v += 15)
		kv("curve", C1(X_VOLUME_CURVE, v));
	nl();
	unsigned char hd[44];
	kv("wavhdr", C5(X_WAV_HEADER, hd, 48000, 6, 24, 123456));
	kh("h", fnv(hd, 44));
	nl();
	short a[2000], b[2000];
	gen_pcm(a, 1000, 2, 1);
	gen_pcm(b, 1000, 2, 2);
	C4(X_MIX_S16, a, b, 1000, 40000);
	kh("mix", fnv(a, 4000));
	C3(X_GAIN_S16, a, 1000, 70000);
	kh("gain", fnv(a, 4000));
	nl();
	uint64_t st[3] = {0, 0, (22050ULL << 16) / 44100};
	short o[4100];
	uint32_t h = 2166136261u;
	for (int k = 0; k < 6; k++) {
		long n = C4(X_RESAMPLE, a, 300 + k * 50, o, st);
		h = fnv_mix(h, fnv(o, n * 4));
		h = fnv_mix(h, n);
	}
	st[2] = (48000ULL << 16) / 44100;
	for (int k = 0; k < 4; k++) {
		long n = C4(X_RESAMPLE, b, 500, o, st);
		h = fnv_mix(h, fnv(o, n * 4));
	}
	kh("resample", h);
	kv("zero", C4(X_RESAMPLE, a, 0, o, st));
	nl();
}

/* ---- fichiers et son (-f) */

static long sleepms(long ms)
{
	return sys2(SYS_SLEEP, ms, 0);
}

static void test_files(void)
{
	ps("=== fichiers et son\n");
	ak_call(AK_F_MKDIR, L("RAM0:/audtest"), 0, 0);
	static const char *const paths[] = {"RAM0:/audtest/a.wav", "RAM0:/audtest/a.mp3", "RAM0:/audtest/a.opus"};
	static const long fmts[] = {AKF_WAV, AKF_MP3, AKF_OPUS};
	for (int i = 0; i < 3; i++) {
		long e = C5(X_ENC_OPEN, paths[i], fmts[i], 44100, 2, 0);
		ps(paths[i]);
		kv("open", e < 0 ? e : 1);
		uint64_t pw = 0;
		if (e >= 0) {
			gen_pcm(pcm, 4000, 2, 0);
			for (long j = 0; j < 8000; j++)
				pw += (uint64_t)((long)pcm[j] * pcm[j]);
			kv("w", C3(X_ENC_WRITE, e, pcm, 4000));
			kv("size", C1(X_ENC_SIZE, e));
			kv("close", C1(X_ENC_CLOSE, e));
		}
		nl();
		long d = C1(X_DEC_OPEN, paths[i]);
		if (d >= 0) {
			dec_noh = !fma_ok && fmts[i] != AKF_WAV; /* voir fma_probe */
			dec_rms2 = pw / 8000;
			dec_all("  ", d, 100000);
			dec_noh = 0;
		}
	}
	kv("enc-mauvais", C5(X_ENC_OPEN, "RAM0:/audtest/x.bin", 99, 44100, 2, 0));
	kv("dec-absent", C1(X_DEC_OPEN, "RAM0:/audtest/absent.wav"));
	nl();
	static const char *const cards[] = {"SD0:/res/audiotest/w8m.wav", "SD0:/res/audiotest/f16s.flac",
					    "SD0:/res/audiotest/jfk.mp3", "SD0:/res/audiotest/kenney_select.ogg",
					    "SD0:/res/audiotest/silk16.opus", "SD0:/res/audiotest/celt64.opus"};
	for (int i = 0; i < 6; i++) {
		long d = C1(X_DEC_OPEN, cards[i]);
		ps(cards[i]);
		kv("open", d < 0 ? d : 1);
		nl();
		if (d >= 0)
			dec_all("  ", d, 2000000);
	}
	/* SoundFont sur la carte */
	long fd = ak_call(AK_F_OUVRIR, L("RAM0:/audtest/t.sf2"), 2 | 4 | 8, 0);
	const struct emb *e = embs(10);
	ak_call(AK_F_ECRIRE, fd, L(e->p), e->e - e->p);
	ak_call(AK_F_FERMER, fd, 0, 0);
	long sf = C1(X_SF_LOAD, "RAM0:/audtest/t.sf2");
	kv("sf_load", sf < 0 ? sf : 1);
	if (sf >= 0)
		kv("presets", C1(X_SF_PRESETS, sf));
	kv("sf_free", C1(X_SF_FREE, sf));
	kv("sf_absent", C1(X_SF_LOAD, "RAM0:/audtest/absent.sf2"));
	long def = C1(X_SF_DEFAULT, 0);
	kv("sf_default", def > 0);
	kv("sf_default2", C1(X_SF_DEFAULT, 0) == def);
	nl();
	/* le service de son */
	long be = C1(X_BACKEND, 0);
	kv("backend", be);
	kv("state", C1(X_STATE, 0) != 0);
	kv("client_volume", C3(X_CLIENT_VOLUME, 0, 100, 0));
	kv("route", C2(X_ROUTE, 0, 0));
	kv("main", C1(X_MAIN, 0));
	kv("out_volume-mauvais", C3(X_OUT_VOLUME, 9999, 50, 0) < 0);
	nl();
	static const long fo[][3] = {{44100, 2, 16}, {22050, 1, 8}, {48000, 2, 24}, {8000, 1, 32},
				     {3000, 2, 16}, {44100, 9, 16}, {44100, 2, 12}};
	for (int i = 0; i < 7; i++) {
		long s = C4(X_OPEN, fo[i][0], fo[i][1], fo[i][2], 0);
		ps("open");
		kv("rate", fo[i][0]);
		kv("r", s < 0 ? s : 1);
		if (s >= 0) {
			kv("latency", C2(X_LATENCY, s, 8000));
			kv("volume", C2(X_VOLUME, s, 300));
			kv("space>0", C1(X_SPACE, s) > 0);
			long fb = fo[i][1] * fo[i][2] / 8;
			gen_pcm(pcm, 2000, 2, 0);
			kv("write>0", C3(X_WRITE, s, pcm, 1000 * fb) > 0);
			kv("write_all", C3(X_WRITE_ALL, s, pcm, 1500 * fb));
			kv("pending>=0", C1(X_PENDING, s) >= 0);
			kv("position>=0", C1(X_POSITION, s) >= 0);
			uint64_t stt[3];
			kv("status", C2(X_STATUS, s, stt));
			kv("drain", C1(X_DRAIN, s));
			kv("close", C1(X_CLOSE, s));
			kv("close2", C1(X_CLOSE, s));
		}
		nl();
	}
	kv("write-mauvais", C3(X_WRITE, 99, pcm, 4));
	kv("pending-mauvais", C1(X_PENDING, -1));
	long r = C1(X_REC_OPEN, 0);
	kv("rec_open", r < 0 ? r : 1);
	if (r >= 0) {
		sleepms(50);
		kv("rec_read>=0", C3(X_REC_READ, r, pcm, 4000) >= 0);
		uint64_t stt[3];
		kv("rec_status", C2(X_REC_STATUS, r, stt));
		kv("rec_close", C1(X_REC_CLOSE, r));
	} else { /* (pas d'entrée : les poignées invalides) */
		uint64_t stt[3];
		kv("rec_read-mauvais", C3(X_REC_READ, 99, pcm, 4000));
		kv("rec_status-mauvais", C2(X_REC_STATUS, -1, stt));
		kv("rec_close-mauvais", C1(X_REC_CLOSE, 5));
	}
	nl();
	/* lecteur en arrière-plan */
	long p = C2(X_PLAY, "SD0:/res/audiotest/kenney_select.ogg", 0);
	kv("play", p < 0 ? p : 1);
	if (p >= 0) {
		kv("volume", C2(X_PLAY_VOLUME, p, 128));
		long dd = C1(X_PLAY_DECODER, p);
		long info[8];
		C2(X_DEC_INFO, dd, info);
		kv("dec.rate", info[1]);
		kv("pause", C2(X_PLAY_PAUSE, p, 1));
		sleepms(100);
		kv("state", C1(X_PLAY_STATE, p));
		kv("resume", C2(X_PLAY_PAUSE, p, 0));
		kv("seek", C2(X_PLAY_SEEK, p, 10));
		kv("pos>=0", C1(X_PLAY_POS, p) >= 0);
		long st = 0;
		for (int i = 0; i < 300; i++) {
			st = C1(X_PLAY_STATE, p);
			if (i)
				calls--; /* (le nombre de tours dépend du temps : un seul appel compté) */
			if (st == AKP_ENDED || st == AKP_ERROR)
				break;
			sleepms(50);
		}
		kv("fin", st);
		kv("stop", C1(X_PLAY_STOP, p));
	}
	kv("play-absent", C2(X_PLAY, "RAM0:/audtest/absent.wav", 0));
	nl();
	/* jeu en direct de la FM */
	long fm = C1(X_FM_NEW, 4);
	kv("live", C2(X_FM_LIVE, fm, 0));
	kv("live2", C2(X_FM_LIVE, fm, 0));
	C5(X_FM_START, fm, 0, 440000, 1, 200);
	sleepms(200);
	C2(X_FM_STOP, fm, -1);
	sleepms(100);
	kv("live-stop", C2(X_FM_LIVE, fm, -1));
	C2(X_FM_LIVE, fm, 0);
	kv("free(live)", C1(X_FM_FREE, fm));
	nl();
	ak_call(AK_F_UNLINK, L("RAM0:/audtest/a.wav"), 0, 0);
	ak_call(AK_F_UNLINK, L("RAM0:/audtest/a.mp3"), 0, 0);
	ak_call(AK_F_UNLINK, L("RAM0:/audtest/a.opus"), 0, 0);
	ak_call(AK_F_UNLINK, L("RAM0:/audtest/t.sf2"), 0, 0);
}

