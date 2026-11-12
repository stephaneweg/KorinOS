/* sontest - essai de la sortie son par AudioKit, comme sontest.asm : gamme do4..do5 (300 ms par
 * note) puis le la 440 Hz pendant 2 s, sinus généré par rotation de vecteur (flottant).
 *	sontest            flux s16 stéréo 44,1 kHz (format natif)
 *	sontest <freq>     flux s16 MONO à <freq> Hz (conversion et rééchantillonnage d'AudioKit)
 * À la fin : la cadence mesurée, la sortie et le chemin.
 *	sontest driver [freq] [s]   faux pilote (sndif.c) : sortie USB à <freq> Hz (48000) avec un
 *	                   canal DMA, consomme son anneau au rythme de l'horloge pendant <s> s (6)
 *	sontest mic [freq] [s]      faux micro : entrée USB, triangle à 440 Hz (crête 8191)
 * Comme l'asm : la ligne d'arguments brute du noyau (krt_line) ; un premier mot qui commence par
 * « d » est « driver », par « m » « mic » (les 6, ou 3, premiers octets sautés quel que soit le
 * mot : la ligne est lue telle quelle, au-delà de sa fin si elle est plus courte) ; un nombre
 * < 4000 (ou un autre mot) : le mode stéréo ; le faux micro refusé dit « sontest driver :
 * inscription refusée » ; sortie sans tampon (puts, print_dec). */
#include "korinrt.h"
#include "audiokit_import.h"
#include "../../kits/audiokit/ak_fp.h"
#include "sndif.h"

#pragma GCC optimize("fp-contract=off") /* fmul puis fadd : deux arrondis, comme l'asm */

#define BLOCK 512 /* trames par écriture */

static u64 rate = SND_RATE;
static u64 chans = 2;
static long stream;
static long t_start;
static u64 status[3];
static const double freqs[8] = {261.6256, 293.6648, 329.6276, 349.2282, 391.9954, 440.0, 493.8833, 523.2511};
static double two_pi_over_rate;
static double osc[4] = {1.0, 0.0, 1.0, 0.0};
static i16 pcm[BLOCK * 2];

/* fréquence (Hz, 0 = silence), trames */
static void tone(double f, u64 frames)
{
	double th = f * two_pi_over_rate; /* thêta par échantillon */
	double t2 = th * th;              /* cos / sin de thêta (Taylor) */
	double c = 1.0 - t2 * 0.5;
	c = c + t2 * t2 * 0.0416666667;
	double s = th - t2 * 0.1666666667 * th;
	osc[0] = 1.0, osc[1] = 0.0, osc[2] = c, osc[3] = s;
	while (frames) {
		u64 n = frames < BLOCK ? frames : BLOCK;
		frames -= n;
		double re = osc[0], im = osc[1], cr = osc[2], ci = osc[3];
		i16 *p = pcm;
		for (u64 k = n; k; k--) {
			double r2 = re * cr - im * ci;
			im = re * ci + im * cr;
			re = r2;
			i16 v = (i16)ak_dcvtzs_w(im * 10000.0);
			*p++ = v;
			if (chans != 1)
				*p++ = v;
		}
		double m = ak_dsqrt(re * re + im * im); /* renormaliser */
		osc[0] = re / m;
		osc[1] = im / m;
		if (!t_start) /* début de la lecture */
			t_start = sys_time_ms();
		ak_write_all(stream, (long)pcm, (long)(chans * 2 * n)); /* (en attendant la place) */
	}
}

/* texte -> nombre décimal qui suit les espaces (0 : aucun), *rest = suite */
static u64 arg_num(const char *s, const char **rest)
{
	u64 v = 0;
	while (*s == ' ')
		s++;
	for (;;) {
		u32 d = (u32)(u8)*s - '0';
		if (d > 9)
			break;
		v = v * 10 + d;
		s++;
	}
	*rest = s;
	return v;
}

static NORETURN void drv_fail(long e)
{
	ak_puts("sontest driver : inscription refusée : ");
	print_dec(e);
	ak_puts("\n");
	_exit(1);
}

/* faux pilote : « driver [freq] [s] » */
static NORETURN void drv_mode(const char *a)
{
	const char *r;
	u64 hz = arg_num(a + 6, &r);
	if (!hz)
		hz = 48000;
	u64 secs = arg_num(r, &r);
	if (!secs)
		secs = 6;
	long id = sndif_output(SND_OUT_USB, (long)hz, 1, "Essai sontest"); /* un canal DMA (pour l'essai) */
	if (id < 0)
		drv_fail(id);
	ak_puts("sontest driver : sortie ");
	print_dec(id);
	ak_puts(", canal DMA ");
	print_dec(sif_out.dma);
	ak_puts(", ");
	print_dec((long)hz);
	ak_puts(" Hz\n");
	u64 t0 = (u64)sys_time_ms();
	u64 due = 0, got = 0, peak = 0;
	u64 ms = secs * 1000;
	for (;;) {
		u64 t = (u64)sys_time_ms() - t0;
		if (t >= ms)
			break;
		u64 n = t * hz / 1000 - due; /* à consommer */
		if (n >= BLOCK)
			n = BLOCK;
		if (n) {
			due += n;
			u64 r = (u64)sndif_read(pcm, (long)n);
			got += r;
			for (u64 k = 0; k < r * 2; k++) { /* crête */
				i32 v = pcm[k];
				if (v < 0)
					v = -v;
				if ((u64)v > peak)
					peak = (u64)v;
			}
		}
		sndif_tick();
		sys_sleep(5);
	}
	ak_puts("sontest driver : ");
	print_dec((long)got);
	ak_puts(" trames lues, ");
	print_dec((long)(due - got));
	ak_puts(" en silence (manques), cadence ");
	print_dec((long)(got * 1000 / ms));
	ak_puts(" trames/s, crête ");
	print_dec((long)peak);
	ak_puts("\n");
	_exit(0);
}

/* faux micro : « mic [freq] [s] » */
static NORETURN void mic_mode(const char *a)
{
	const char *r;
	u64 hz = arg_num(a + 3, &r);
	if (!hz)
		hz = 48000;
	u64 secs = arg_num(r, &r);
	if (!secs)
		secs = 6;
	long id = sndif_input(SND_IN_USB, (long)hz, 0, "Essai micro");
	if (id < 0)
		drv_fail(id); /* (comme l'asm : le message du faux pilote) */
	ak_puts("sontest mic : entrée ");
	print_dec(id);
	ak_puts(", ");
	print_dec((long)hz);
	ak_puts(" Hz\n");
	u32 step = (u32)((440ULL << 32) / hz); /* pas de phase : 440 x 2^32 / freq */
	u32 phase = 0;
	u64 t0 = (u64)sys_time_ms();
	u64 due = 0, put = 0, heard = 0;
	u64 ms = secs * 1000;
	for (;;) {
		u64 t = (u64)sys_time_ms() - t0;
		if (t >= ms)
			break;
		u64 n = t * hz / 1000 - due;
		if (n >= BLOCK)
			n = BLOCK;
		if (n) {
			due += n;
			i16 *p = pcm; /* triangle de -8192 à 8191 */
			for (u64 k = n; k; k--) {
				u32 h = phase >> 16;
				u32 w = h < 32768 ? h : 65535 - h;
				i16 v = (i16)((i32)(w - 16384) >> 1);
				*p++ = v;
				*p++ = v;
				phase += step;
			}
			put += (u64)sndif_write(pcm, (long)n);
		}
		if (sndif_wanted())
			heard += 5;
		sndif_tick();
		sys_sleep(5);
	}
	ak_puts("sontest mic : ");
	print_dec((long)put);
	ak_puts(" trames écrites, écoutée environ ");
	print_dec((long)heard);
	ak_puts(" ms\n");
	_exit(0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *a = krt_line;
	if (a) {
		if (a[0] == 'd')
			drv_mode(a);
		if (a[0] == 'm')
			mic_mode(a);
		if ((u8)(a[0] - '0') <= 9) { /* sontest <fréquence> : mono */
			u64 v = parse_dec(a);
			if (v >= 4000) {
				rate = v;
				chans = 1;
			}
		}
	}
	two_pi_over_rate = 6.283185307179586 / (double)(i64)rate; /* thêta par Hz */
	ak_puts("sontest : gamme do4-do5 puis la 440 Hz (2 s), ");
	print_dec((long)rate);
	ak_puts(" Hz ");
	ak_puts(chans == 1 ? "mono\n" : "stéréo\n");
	long s = ak_open((long)rate, (long)chans, 16, 0);
	if (s < 0) {
		ak_puts("sontest : pas de flux son : ");
		print_dec(s);
		ak_puts("\n");
		_exit(1);
	}
	stream = s;
	for (int i = 0; i < 8; i++)
		tone(freqs[i], rate * 3 / 10);
	ak_puts("sontest : la 440 Hz\n");
	tone(440.0, rate << 1);  /* 2 s */
	tone(0.0, rate / 5);     /* un peu de silence à la fin (200 ms) */
	ak_drain(stream);        /* attendre la fin de la lecture */
	u64 dt = (u64)(sys_time_ms() - t_start);
	u64 played = (u64)ak_position(stream);
	ak_status(stream, (long)status);
	ak_puts("sontest : terminé, ");
	print_dec((long)played);
	ak_puts(" trames jouées en ");
	print_dec((long)dt);
	ak_puts(" ms, cadence ");
	print_dec((long)(played * 1000 / (dt ? dt : 1)));
	ak_puts(" trames/s (attendu 44100)\n");
	ak_puts("sontest : sortie matérielle = ");
	print_dec((long)status[2]);
	ak_puts(" (0 virtuelle, 1 PWM, 2 HDMI, 3 USB), chemin = ");
	print_dec(ak_backend());
	ak_puts(" (1 service sound, 2 son du noyau)\n");
	ak_close(stream);
	_exit(0);
}
