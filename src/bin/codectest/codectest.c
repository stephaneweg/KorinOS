/* codectest - essai des formats audio d'AudioKit (docs/SON.md 9), comme codectest.asm :
 *	codectest <fichier> [reference.raw]
 *	       décode tout le fichier (ak_dec_*) : format, fréquence, canaux, étiquettes, trames,
 *	       temps de décodage et vitesse ; avec une référence (s16 entrelacé, même format) : écart
 *	       maximal et rapport signal / bruit
 *	codectest -o <sortie.raw> <fichier>     écrit le PCM décodé (s16)
 *	codectest -s <ms> <fichier> [ref.raw]   recherche puis décode la suite
 *	codectest -p <fichier> [secondes]       ak_play : joue (5 s), position chaque seconde, pause
 *	       d'une seconde à 2 s, reprise, arrêt
 * Comme l'asm : la ligne d'arguments brute du noyau (krt_line), coupée aux espaces (8 mots au
 * plus) ; une option n'est vue qu'à sa deuxième lettre (« -ox » vaut « -o ») ; un premier mot
 * sans « - » est le fichier, le deuxième la référence, le reste ignoré ; une étiquette est
 * affichée si ak_dec_tag ne rend pas 0 ; toute erreur (ouverture, référence, sortie, recherche,
 * ak_play) : « codectest : erreur N », code 1 ; sortie sans tampon (puts, print_dec). */
#include "korinrt.h"
#include "audiokit_import.h"
#include "../../kits/audiokit/ak_fp.h"

#pragma GCC optimize("fp-contract=off")

typedef int16_t i16;

#define FRAMES 4096

static char *words[8];
static long nw;
static const char *f_in, *f_ref, *f_out;
static u64 seek_ms;
static u64 info[AKI_WORDS];
static char tagbuf[256];

static const char *const fmt_names[] = {"?", "WAV", "FLAC", "MP3", "Vorbis", "Opus", "MIDI", "FM Song"};
static const char *const tag_names[] = {"titre   : ", "artiste : ", "album   : "};

static NORETURN void usage(void)
{
	ak_puts("codectest <fichier> [ref.raw] | -o <sortie.raw> <fichier> | -s <ms> <fichier> [ref.raw] | -p <fichier> [s]\n");
	_exit(1);
}

static NORETURN void fail(long e)
{
	ak_puts("codectest : erreur ");
	print_dec(e);
	ak_puts("\n");
	_exit(1);
}

/* x > 0 -> log2(x) (log2d de l'asm : exposant, puis 30 bits de la mantisse par carrés).
 * Comme l'asm : une mantisse >= 2 est multipliée par le poids du bit courant (0,5, 0,25...) et
 * non par 0,5 : les bits après le premier sont faux (le RSB affiché aussi). */
static double log2d(double x)
{
	u64 b = ak_d2u(x);
	i64 e = (i64)((b >> 52) & 0x7FF) - 1023;
	double m = ak_u2d((b & 0xFFFFFFFFFFFFFull) | 0x3FF0000000000000ull); /* dans [1, 2[ */
	double r = (double)e, bit = 0.5;
	for (int k = 30; k; k--) {
		m = m * m;
		if (!(m < 2.0)) { /* (b.lt : vrai sur NaN) */
			m = m * bit;
			r = r + bit;
		}
		bit = bit * 0.5;
	}
	return r;
}

/* nombre en dixièmes (signé) */
static void print_tenth(long v)
{
	u64 u = (u64)v;
	if (v < 0) {
		ak_puts("-");
		u = (u64)-v;
	}
	print_dec((long)(u / 10));
	ak_puts(",");
	print_dec((long)(u % 10));
}

static NORETURN void decode(void)
{
	i16 *buf = ak_malloc(FRAMES * 16); /* tampons : décodage, référence */
	if (!buf)
		fail(0);
	i16 *ref = ak_malloc(FRAMES * 16);
	if (!ref)
		fail(0);
	long t0 = sys_time_ms();
	long d = ak_dec_open((long)f_in);
	if (d < 0)
		fail(d);
	long t_open = sys_time_ms() - t0;
	ak_dec_info(d, (long)info);
	ak_puts("format : ");
	ak_puts(fmt_names[info[AKI_FORMAT] & 7]); /* (l'asm ne borne pas : AKF_* va de 0 à 7) */
	ak_puts(", ");
	print_dec((long)info[AKI_RATE]);
	ak_puts(" Hz, ");
	print_dec((long)info[AKI_CHANNELS]);
	ak_puts(" canaux, ");
	print_dec((long)info[AKI_DURATION]);
	ak_puts(" ms, ");
	print_dec((long)(info[AKI_BITRATE] / 1000));
	ak_puts(" kbit/s\n");
	for (long t = AKT_TITLE; t <= AKT_ALBUM; t++) { /* étiquettes */
		if (!ak_dec_tag(d, t, (long)tagbuf, 256))
			continue;
		ak_puts(tag_names[t]);
		ak_puts(tagbuf);
		ak_puts("\n");
	}
	u64 fb = info[AKI_CHANNELS] << 1; /* octets par trame */
	long rf = -1, of = -1;            /* référence, sortie */
	if (f_ref) {
		rf = f_open(f_ref, O_READ);
		if (rf < 0)
			fail(rf);
	}
	if (f_out) {
		of = f_open(f_out, O_WRITE | O_CREATE | O_TRUNC);
		if (of < 0)
			fail(of);
	}
	if (seek_ms) { /* recherche */
		long r = ak_dec_seek(d, (long)seek_ms);
		if (r < 0)
			fail(r);
		u64 pos = (u64)ak_dec_tell(d);
		ak_puts("apres la recherche : trame ");
		print_dec((long)pos);
		ak_puts("\n");
		if (rf >= 0) /* la référence au même endroit */
			f_seek(rf, (long)(pos * fb), 0);
	}
	u64 frames = 0, maxd = 0;
	double sig = 0.0, noise = 0.0;
	u64 t_dec = 0; /* temps de décodage (ms) */
	long r;
	for (;;) {
		long t1 = sys_time_ms();
		r = ak_dec_read(d, (long)buf, FRAMES);
		t_dec += (u64)(sys_time_ms() - t1);
		if (r <= 0)
			break;
		frames += (u64)r;
		u64 bytes = (u64)r * fb;
		if (of >= 0)
			f_write(of, buf, bytes);
		if (rf < 0)
			continue;
		long got = f_read(rf, ref, bytes); /* comparaison */
		if (got <= 0)
			continue;
		u64 ns = (u64)got >> 1; /* échantillons */
		for (u64 k = 0; k < ns; k++) {
			i32 a = buf[k], b = ref[k];
			i32 e = a - b;
			u32 ae = (u32)(e < 0 ? -e : e);
			if (ae > maxd)
				maxd = ae;
			double x = (double)b;
			sig = ak_dmadd(x, x, sig);
			double y = (double)(i64)(i32)ae;
			noise = ak_dmadd(y, y, noise);
		}
	}
	ak_puts("trames : ");
	print_dec((long)frames);
	if (r < 0) {
		ak_puts(", erreur ");
		print_dec(r);
	}
	ak_puts("\n");
	ak_puts("ouverture : ");
	print_dec(t_open);
	ak_puts(" ms, decodage : ");
	print_dec((long)t_dec);
	ak_puts(" ms, vitesse : ");
	/* vitesse : durée / temps, en dixièmes */
	u64 rate = info[AKI_RATE];
	u64 dur = rate ? frames * 10000 / rate : 0; /* (udiv par 0 : 0) */
	print_tenth((long)(dur / (t_dec >= 1 ? t_dec : 1)));
	ak_puts(" x temps reel\n");
	if (rf >= 0) {
		ak_puts("reference : ecart max ");
		print_dec((long)maxd);
		ak_puts(", RSB ");
		if (noise == 0.0) {
			ak_puts("identique\n");
		} else {
			double v = log2d(sig / noise);
			v = v * 3.0102999566398120; /* 10 log10 = log2 * 3,0103 */
			v = v * 10.0;
			print_tenth(ak_dcvtzs_x(v));
			ak_puts(" dB\n");
		}
	}
	ak_dec_close(d);
	if (of >= 0)
		f_close(of);
	_exit(0);
}

/* -p : lecteur */
static NORETURN void play_mode(void)
{
	if (nw < 2)
		usage();
	u64 secs = 5;
	if (nw >= 3)
		secs = parse_dec(words[2]);
	long pl = ak_play((long)words[1], 0);
	if (pl < 0)
		fail(pl);
	for (u64 s = 0;;) {
		sys_sleep(1000);
		s++;
		ak_puts("position (ms) ");
		print_dec(ak_play_pos(pl));
		ak_puts(", etat ");
		print_dec(ak_play_state(pl));
		ak_puts("\n");
		if (s == 2) { /* pause d'une seconde à 2 s */
			ak_play_pause(pl, 1);
			sys_sleep(1000);
			ak_puts("en pause, position ");
			print_dec(ak_play_pos(pl));
			ak_puts("\n");
			ak_play_pause(pl, 0);
		}
		if ((u64)ak_play_state(pl) >= AKP_ENDED)
			break;
		if (s >= secs)
			break;
	}
	ak_play_stop(pl);
	ak_puts("arret\n");
	_exit(0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!krt_line)
		usage();
	nw = krt_split_spaces((char *)krt_line, words, 8); /* (coupée sur place, comme l'asm) */
	if (!nw)
		usage();
	const char *a = words[0];
	if (a[0] != '-') { /* codectest f [ref] */
		f_in = a;
		f_ref = words[1];
		decode();
	}
	if (a[1] == 'o') {
		if (nw < 3)
			usage();
		f_out = words[1];
		f_in = words[2];
		decode();
	}
	if (a[1] == 's') {
		if (nw < 3)
			usage();
		seek_ms = parse_dec(words[1]);
		f_in = words[2];
		f_ref = words[3];
		decode();
	}
	if (a[1] == 'p')
		play_mode();
	usage();
}
