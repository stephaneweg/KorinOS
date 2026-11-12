/* synthtest - essai de la synthèse d'AudioKit (docs/SON.md 9), comme synthtest.asm :
 *	synthtest              essais complets, sans fichier : FM (CRC du PCM égal à fmsynth.h
 *	                       d'Onyx, tools/audiotest/fmref.cpp), notes, synthé MIDI en banque FM et
 *	                       en SoundFont (test.sf2, embarquée), lecture d'un .mid et d'un .fms en
 *	                       mémoire -> PASS (code 0) / ECHECS : N (code 1)
 *	synthtest -p <fichier> [secondes]   joue un .mid ou un .fms par ak_play (5 s par défaut)
 * Comme l'asm : la ligne d'arguments brute du noyau (krt_line) ; toute ligne non vide joue un
 * fichier (« - » en tête : les 3 premiers octets sautés, quels qu'ils soient) ; le chemin va
 * jusqu'à la première espace, le reste est lu par parse_dec (0 : jusqu'à la fin du morceau) ; la
 * position (ms) chaque seconde ; une erreur d'ak_play est seulement affichée ; code 0. Sortie sans
 * tampon (puts, print_dec). */
#include "korinrt.h"
#include "audiokit_import.h"

typedef int16_t i16;

#define FM_REF_CRC 0xE7C31181u /* tools/audiotest/fmref.cpp */
#define BUF_FRAMES 65536

static i16 *pcm;
static u32 crc;
static u64 fails;

/* la SoundFont d'essai, embarquée comme le « file 'test.sf2' » de l'asm */
__asm__(".section .rodata.synthtest_sf2,\"a\"\n"
	".balign 8\n"
	"synthtest_sf2:\n"
	".incbin \"src/bin/synthtest/test.sf2\"\n"
	"synthtest_sf2_end:\n"
	".previous\n");
extern const u8 synthtest_sf2[], synthtest_sf2_end[];

/* une note d'une seconde à 120 noires/min, deux pistes (format 1) */
static const u8 mid_data[] __attribute__((aligned(8))) = {
	'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 0, 96,
	'M', 'T', 'r', 'k', 0, 0, 0, 15, 0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20, 0, 0xFF, 0x2F, 0, 0, 0, 0, 0,
	'M', 'T', 'r', 'k', 0, 0, 0, 24, 0, 0xC0, 73, 0, 0x90, 60, 100, 96, 0x90, 64, 100, 96, 0x80, 60, 0,
	0, 0x80, 64, 0, 96, 0xFF, 0x2F, 0};

static const char fms_magic[15] = {'f', 'm', '-', 's', 'o', 'n', 'g', '-', 'p', 'r', 'o', 'j', 'e', 'c', 't'};
static const u8 fms_ins[34] = {'T', 'E', 'S', 'T', ' ', ' ', ' ', ' ', 15, 15, 1, 2, 5, 7, 3, 4, 20, 0, 0,
			       0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 3, 0, 1, 0};
static const u8 fms_notes[8] = {33, 34, 35, 36, 37, 38, 39, 128}; /* do4 ... si4, silence */

/* instruments du scénario (fmref.cpp) : AKFI_* */
static const u8 ins_a[24] = {15, 2, 1, 2, 20, 0, 1, 0, AKFF_SUSTAINED, 0, 15, 1, 2, 3, 0, 0, 1, 0,
			     AKFF_SUSTAINED, 0, 3, 0, 0, 0};
static const u8 ins_b[24] = {10, 4, 3, 5, 10, 0, 3, 1, AKFF_TREMOLO, 0, 12, 3, 4, 4, 3, 0, 2, 2,
			     AKFF_VIBRATO, 0, 5, 1, 0, 0};
static const u8 ins_c[24] = {8, 5, 15, 6, 30, 0, 0, 3, AKFF_VIBRATO | AKFF_TREMOLO | AKFF_SUSTAINED, 0,
			     15, 0, 0, 7, 5, 0, 7, 0, AKFF_SUSTAINED, 0, 7, 0, 0, 0};
static const u32 starts[][4] = {{1, 261626, AKW_FM, 180}, {2, 659255, AKW_FM, 200}, {3, 220000, AKW_SINE, 120},
				{4, 330000, AKW_TRIANGLE, 120}, {5, 110000, AKW_SAW, 90}, {6, 8000000, AKW_NOISE, 60},
				{7, 55000, AKW_SQUARE, 60}, {99, 0, 0, 0}};

/* libellé, valeur montrée, 1 réussi */
static void check(const char *label, long v, int ok)
{
	ak_puts(ok ? "  ok    " : "  ECHEC ");
	ak_puts(label);
	ak_puts(" = ");
	print_dec(v);
	ak_puts("\n");
	if (!ok)
		fails++;
}

static void check_in(const char *label, long v, long lo, long hi)
{
	check(label, v, v >= lo && v <= hi);
}

/* pcm stéréo, trames -> moyenne de |gauche| + |droite| */
static u64 level(const i16 *p, u64 n)
{
	u64 s = 0;
	if (!n)
		return 0;
	for (u64 k = n; k; k--) {
		i32 a = *p++, b = *p++;
		s += (u64)(u32)(a < 0 ? -a : a);
		s += (u64)(u32)(b < 0 ? -b : b);
	}
	return s / n;
}

/* pcm stéréo (canal gauche), trames, décalage bas, haut -> la période (autocorrélation) */
static long period(const i16 *p, u64 n, u64 lo, u64 hi)
{
	long best = 0, lag = 0;
	u64 cmp = n - hi; /* trames comparées */
	for (u64 d = lo; d <= hi; d++) {
		const i16 *a = p, *b = p + d * 2;
		long s = 0;
		for (u64 k = cmp; k; k--, a += 2, b += 2)
			s += (i32)((u32)(i32)*a * (u32)(i32)*b);
		if (s > best) {
			best = s;
			lag = (long)d;
		}
	}
	return lag;
}

/* CRC-32 continu (crc) */
static void crc_add(const u8 *p, u64 n)
{
	u32 c = crc;
	for (; n; n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = ((0u - (c & 1)) & 0xEDB88320u) ^ (c >> 1);
	}
	crc = c;
}

/* synthé, trames : rendu ajouté au CRC */
static void fm_run(long fm, u64 n)
{
	while (n) {
		u64 k = n < 8192 ? n : 8192;
		ak_fm_render(fm, (long)pcm, (long)k);
		crc_add((const u8 *)pcm, k * 4);
		n -= k;
	}
}

static void test_notes(void)
{
	ak_puts("--- notes\n");
	check_in("note 69 (mHz)", ak_note_mhz(69), 440000, 440000);
	check_in("do4 (mHz)", ak_note_mhz(60), 261625, 261627);
	check_in("do4 (mHz)", ak_note_octave_mhz(0, 4), 261625, 261627);
	check_in("note 127 (mHz)", ak_note_mhz(127), 12543800, 12543900);
}

/* le scénario de tools/audiotest/fmref.cpp */
static void test_fm(void)
{
	ak_puts("--- synthese FM\n");
	long fm = ak_fm_new(16);
	crc = 0xFFFFFFFFu;
	ak_fm_instrument(fm, 0, (long)ins_a);
	ak_fm_instrument(fm, 1, (long)ins_b);
	ak_fm_instrument(fm, 2, (long)ins_c);
	ak_fm_start(fm, 0, 440000, AKW_FM, 220);
	fm_run(fm, 4096);
	for (int i = 0; starts[i][0] != 99; i++) /* voix, milli-Hz, onde, volume */
		ak_fm_start(fm, starts[i][0], starts[i][1], starts[i][2], starts[i][3]);
	fm_run(fm, 22050);
	check_in("voix actives", ak_fm_active(fm), 1, 1);
	ak_fm_stop(fm, 0);
	fm_run(fm, 11025);
	ak_fm_stop(fm, -1);
	fm_run(fm, 44100);
	u32 c = ~crc;
	check("CRC egal a fmsynth.h", (long)c, c == FM_REF_CRC);
	/* panoramique à droite : rien à gauche */
	ak_fm_silence(fm);
	ak_fm_pan(fm, 0, 255);
	ak_fm_start(fm, 0, 440000, AKW_SINE, 200);
	ak_fm_render(fm, (long)pcm, 4096);
	u64 s = 0; /* somme de |gauche| */
	const i16 *p = pcm;
	for (int k = 0; k < 4096; k++, p += 2) {
		i32 v = *p;
		s += (u64)(u32)(v < 0 ? -v : v);
	}
	check_in("panoramique : rien a gauche", (long)s, 0, 0);
	ak_fm_free(fm);
}

/* synthé : 8192 trames dans pcm -> niveau */
static long render_level(long sy)
{
	ak_synth_render(sy, (long)pcm, 8192);
	return (long)level(pcm, 8192);
}

static void midi(long sy, long ch, long cmd, long a, long b)
{
	ak_synth_midi(sy, ch, cmd, a, b);
}

static void test_bank(void)
{
	ak_puts("--- MIDI, banque FM\n");
	long sy = ak_synth_new(0, AKS_FM);
	midi(sy, 0, 0xC0, 73, 0); /* flûte : hauteur claire */
	midi(sy, 0, 0x90, 69, 110);
	render_level(sy);
	check_in("niveau note enfoncee", render_level(sy), 100, 40000);
	check_in("periode de la4 (trames)", period(pcm, 4096, 60, 160), 98, 102);
	midi(sy, 0, 0xE0, 127, 127); /* molette + 2 demi-tons */
	render_level(sy);
	check_in("periode avec molette", period(pcm, 4096, 60, 160), 87, 91);
	midi(sy, 0, 0xB0, 64, 127); /* pédale : la note reste */
	midi(sy, 0, 0x80, 69, 0);
	check_in("niveau sous la pedale", render_level(sy), 100, 40000);
	midi(sy, 0, 0xB0, 64, 0);
	long lv = 0;
	for (int i = 0; i < 30; i++)
		lv = render_level(sy);
	check_in("niveau apres le relachement", lv, 0, 50);
	/* percussions (canal 10) : un son */
	midi(sy, 9, 0x90, 38, 120);
	check_in("niveau percussion", render_level(sy), 50, 40000);
	/* toute note relâchée finit, même d'un instrument de relâchement 0 */
	midi(sy, 9, 0x80, 38, 0);
	midi(sy, 9, 0x90, 39, 120); /* BRUSH */
	midi(sy, 0, 0xC0, 118, 0);  /* SYNDRUM */
	midi(sy, 0, 0x90, 60, 120);
	render_level(sy);
	midi(sy, 9, 0x80, 39, 0);
	midi(sy, 0, 0x80, 60, 0);
	for (int i = 0; i < 60; i++)
		render_level(sy);
	check_in("voix restantes apres relachement", ak_synth_voices(sy), 0, 0);
	ak_synth_free(sy);
}

/* SoundFont, CC 93 -> synthé, la4 enfoncé */
static long cho_synth(long sf, long cc)
{
	long sy = ak_synth_new(sf, 0);
	midi(sy, 0, 0xB0, 93, cc);
	midi(sy, 0, 0x90, 69, 127);
	return sy;
}

/* SoundFont, CC 93 du premier synthé, du second -> moyenne des écarts entre leurs rendus de la
 * même note (trames 8192..16384, canal gauche) */
static u64 cho_diff(long sf, long cc1, long cc2)
{
	long a = cho_synth(sf, cc1);
	long b = cho_synth(sf, cc2);
	i16 *pa = pcm, *pb = pcm + 16384 * 2; /* deuxième moitié du tampon */
	ak_synth_render(a, (long)pa, 8192);
	ak_synth_render(b, (long)pb, 8192);
	ak_synth_render(a, (long)pa, 8192);
	ak_synth_render(b, (long)pb, 8192);
	u64 s = 0;
	for (int k = 0; k < 8192; k++, pa += 2, pb += 2) {
		i32 d = (i32)*pa - (i32)*pb;
		s += (u64)(u32)(d < 0 ? -d : d);
	}
	ak_synth_free(a);
	ak_synth_free(b);
	return s >> 13;
}

static void test_sf2(void)
{
	ak_puts("--- MIDI, SoundFont\n");
	long sf = ak_sf_open_mem((long)synthtest_sf2, (long)(synthtest_sf2_end - synthtest_sf2));
	check("SoundFont lue", sf, sf >= 0);
	if (sf < 0)
		return;
	check_in("presets", ak_sf_presets(sf), 1, 1);
	long sy = ak_synth_new(sf, AKS_NOFX);
	midi(sy, 0, 0x90, 69, 127);
	check_in("niveau note enfoncee", render_level(sy), 3000, 40000);
	check_in("voix", ak_synth_voices(sy), 1, 1);
	check_in("periode de la4 (trames)", period(pcm + 1024 * 2, 4096, 60, 160), 100, 100); /* (après l'attaque) */
	midi(sy, 0, 0x80, 69, 0); /* relâchement */
	render_level(sy);
	long lv = 0;
	for (int i = 0; i < 8; i++)
		lv = render_level(sy);
	check_in("niveau apres le relachement", lv, 0, 50);
	midi(sy, 0, 0x90, 81, 100); /* octave au-dessus : période 50 */
	render_level(sy);
	check_in("periode de la5 (trames)", period(pcm + 1024 * 2, 4096, 30, 70), 50, 50);
	midi(sy, 0, 0xE0, 127, 127); /* molette + 2 demi-tons sur la5 : 44,5 */
	render_level(sy);
	check_in("periode avec molette", period(pcm + 1024 * 2, 4096, 30, 70), 44, 45);
	ak_synth_free(sy);
	sy = ak_synth_new(sf, 0); /* avec réverbération : la traîne reste */
	midi(sy, 0, 0x90, 69, 127);
	render_level(sy);
	midi(sy, 0, 0x80, 69, 0);
	lv = 0;
	for (int i = 0; i < 2; i++) /* 370 ms : la voix est finie */
		lv = render_level(sy);
	check_in("traine de reverberation", lv, 1, 30000);
	ak_synth_free(sy);
	check_in("ecart sans chorus", (long)cho_diff(sf, 0, 0), 0, 0); /* deux synthés égaux */
	check_in("ecart avec chorus", (long)cho_diff(sf, 0, 127), 100, 30000); /* CC 93 sur l'un */
	ak_sf_free(sf);
}

/* tampon, octets, format attendu, durée min (ms), max */
static void test_file(const void *buf, u64 n, long fmt, long dmin, long dmax)
{
	u64 info[AKI_WORDS];
	long d = ak_dec_open_mem((long)buf, (long)n);
	check("ouverture", d, d >= 0);
	if (d < 0)
		return;
	ak_dec_info(d, (long)info);
	check_in("format", (long)info[AKI_FORMAT], fmt, fmt);
	check_in("duree (ms)", (long)info[AKI_DURATION], dmin, dmax);
	u64 frames = 0, peak = 0;
	for (;;) {
		long r = ak_dec_read(d, (long)pcm, 4096);
		if (r <= 0)
			break;
		frames += (u64)r;
		u64 lv = level(pcm, (u64)r);
		if (lv > peak)
			peak = lv;
	}
	check_in("trames", (long)frames, 1000, 10000000);
	check_in("niveau maximal", (long)peak, 200, 50000);
	check_in("recherche", ak_dec_seek(d, 100), 0, 0); /* recherche puis lecture : encore du son */
	check_in("lecture apres recherche", ak_dec_read(d, (long)pcm, 4096), 1, 4096);
	ak_dec_close(d);
}

static void test_mid(void)
{
	ak_puts("--- fichier .mid\n");
	test_file(mid_data, sizeof mid_data, AKF_MIDI, 3500, 3510); /* 1,5 s de notes + 2 s de fin */
}

static void test_fms(void)
{
	ak_puts("--- fichier .fms\n");
	/* une chanson : 8 rangs de vitesse 2 (0,1 s), une note par rang */
	u64 n = 107 + 8 * 34 + 12 + 8 * 8;
	u8 *s = ak_malloc(n);
	for (u64 i = n; i;)
		s[--i] = 0;
	for (int i = 0; i < 15; i++)
		s[i] = (u8)fms_magic[i];
	s[105] = 1, s[106] = 0; /* un motif */
	for (int i = 0; i < 34; i++) /* instrument du canal 0 */
		s[107 + i] = fms_ins[i];
	u8 *p = s + 107 + 8 * 34;
	p[0] = 8, p[1] = 0;
	p[2] = 2, p[3] = 0;
	for (int i = 0; i < 8; i++)
		p[12 + i] = fms_notes[i];
	test_file(s, n, AKF_FMS, 2790, 2810); /* 0,8 s + 2 s de fin */
}

/* ligne de commande : -p <fichier> [secondes] */
static void play_file(const char *l)
{
	char *path = (char *)l; /* (coupée sur place, comme l'asm) */
	if (path[0] == '-')
		path += 3;
	u64 secs = 5;
	for (char *q = path; *q; q++)
		if (*q == ' ') {
			*q = 0;
			secs = parse_dec(q + 1);
			break;
		}
	long pl = ak_play((long)path, 0);
	if (pl < 0) {
		print_dec(pl);
		ak_puts("\n");
		return;
	}
	for (;;) {
		sys_sleep(1000);
		print_dec(ak_play_pos(pl));
		ak_puts("\n");
		if ((u64)ak_play_state(pl) >= AKP_ENDED)
			break;
		if (!--secs) /* (0 : -1, jusqu'à la fin, comme l'asm) */
			break;
	}
	ak_play_stop(pl);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (krt_line && krt_line[0]) {
		play_file(krt_line);
		_exit(0);
	}
	pcm = ak_malloc(BUF_FRAMES * 4);
	test_notes();
	test_fm();
	test_bank();
	test_sf2();
	test_mid();
	test_fms();
	ak_puts("synthtest : ");
	if (!fails) {
		ak_puts("PASS\n");
		_exit(0);
	}
	ak_puts("ECHECS : ");
	print_dec((long)fails);
	ak_puts("\n");
	_exit(1);
}
