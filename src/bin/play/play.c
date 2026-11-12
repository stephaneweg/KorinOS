/* play - joue un fichier audio par le lecteur d'AudioKit (docs/SON.md 3 et 9), comme play.asm :
 *	play <fichier> [--volume 0..100] [--seek ms] [--output n]
 * Affiche le format, la durée, titre / artiste / album, puis la position (mm:ss / mm:ss) une fois
 * par seconde jusqu'à la fin. Code 0 si tout a été joué, 1 sinon (usage compris).
 * Comme l'asm : la ligne d'arguments brute du noyau (krt_line), coupée aux espaces seulement
 * (16 mots au plus, sans guillemets) ; tout mot qui commence par « - » est une option et prend le
 * mot suivant comme valeur (même « --volume -x », lu par parse_dec) ; une option inconnue,
 * absente ou un volume > 100 : l'usage ; un seul fichier ; sortie sans tampon (puts, lang_puts,
 * print_dec). */
#include "korinrt.h"
#include "audiokit_import.h"

#define PLY_ARGS 16 /* mots au plus sur la ligne de commande */

static char *words[PLY_ARGS];
static u64 info[AKI_WORDS];
static char tbuf[64];
static char tagbuf[256];

static const char *const fmt_names[] = {"?", "WAV", "FLAC", "MP3", "Ogg Vorbis", "Ogg Opus", "MIDI", "FM Song"};
static const char *const tag_names[] = {"titre   : ", "artiste : ", "album   : "};

static NORETURN void usage(void)
{
	lang_puts("usage : play <fichier> [--volume 0..100] [--seek ms] [--output n]\n"
		  "        (WAV, FLAC, MP3, Ogg Vorbis)\n");
	_exit(1);
}

/* durée (ms, < 0 : inconnue), tampon -> fin (« mm:ss », NUL) */
static char *fmt_time(long ms, char *p)
{
	if (ms < 0) {
		p[0] = '-', p[1] = '-', p[2] = ':', p[3] = '-', p[4] = '-';
		p += 5;
	} else {
		u64 s = (u64)ms / 1000;
		u64 m = s / 60;
		u64 r = s - m * 60;
		if (m < 10)
			*p++ = '0';
		p = fmt_dec(m, p);
		*p++ = ':';
		*p++ = (char)('0' + r / 10);
		*p++ = (char)('0' + r % 10);
	}
	*p = 0;
	return p;
}

static NORETURN void open_fail(long e)
{
	lang_puts("play : ");
	const char *m;
	if (e == AKE_FORMAT)
		m = "format non reconnu ou non pris en charge (WAV, FLAC, MP3, Ogg Vorbis ; Ogg Opus pas encore)";
	else if (e == AKE_CORRUPT)
		m = "fichier invalide ou abime";
	else if (e == AKE_IO)
		m = "lecture impossible";
	else if (e == AKE_NOMEM)
		m = "memoire insuffisante";
	else
		m = "lecture impossible (fichier absent, ou pas de son ?)";
	lang_puts(m);
	lang_puts(" [erreur ");
	print_dec(e);
	ak_puts("]\n");
	_exit(1);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!krt_line)
		usage();
	u64 n = (u64)str_len(krt_line) + 1;
	char *line = malloc(n);
	memcpy(line, krt_line, n);
	long nw = krt_split_spaces(line, words, PLY_ARGS);
	const char *file = 0;
	u64 vol = 100, seek = 0, out = 0;
	for (long i = 0; i < nw;) {
		char *w = words[i++];
		if (w[0] != '-') {
			if (file) /* un seul fichier */
				usage();
			file = w;
			continue;
		}
		if (i >= nw) /* toute option prend une valeur */
			usage();
		char *v = words[i++];
		if (str_eq(w, "--volume")) {
			vol = parse_dec(v);
			if (vol > 100)
				usage();
		} else if (str_eq(w, "--seek")) {
			seek = parse_dec(v);
		} else if (str_eq(w, "--output")) {
			out = parse_dec(v);
		} else {
			usage();
		}
	}
	if (!file)
		usage();

	/* lecture */
	long pl = ak_play((long)file, (long)out);
	if (pl < 0)
		open_fail(pl);
	ak_play_volume(pl, (long)((u64)ak_volume_curve((long)vol) >> 8)); /* courbe à l'oreille, 0..256 */
	if (seek)
		ak_play_seek(pl, (long)seek);
	long dec = ak_play_decoder(pl); /* format, durée, étiquettes */
	ak_dec_info(dec, (long)info);
	lang_puts("format : ");
	u64 f = info[AKI_FORMAT];
	if (f > AKF_FMS)
		f = 0;
	ak_puts(fmt_names[f]);
	ak_puts(", ");
	print_dec((long)info[AKI_RATE]);
	ak_puts(" Hz, ");
	print_dec((long)info[AKI_CHANNELS]);
	lang_puts(" canaux");
	if (info[AKI_BITRATE]) {
		ak_puts(", ");
		print_dec((long)(info[AKI_BITRATE] / 1000));
		ak_puts(" kbit/s");
	}
	ak_puts("\n");
	lang_puts("duree  : ");
	fmt_time((long)info[AKI_DURATION], tbuf);
	ak_puts(tbuf);
	ak_puts("\n");
	for (long t = AKT_TITLE; t <= AKT_ALBUM; t++) { /* étiquettes présentes */
		if (ak_dec_tag(dec, t, (long)tagbuf, 256) <= 0)
			continue;
		lang_puts(tag_names[t]);
		ak_puts(tagbuf);
		ak_puts("\n");
	}

	/* position chaque seconde jusqu'à la fin */
	long dur = (long)info[AKI_DURATION]; /* (ms, -1 : inconnue) */
	for (;;) {
		sys_sleep(1000);
		long st = ak_play_state(pl);
		if (st == AKP_ENDED) {
			ak_play_stop(pl);
			lang_puts("fin\n");
			_exit(0);
		}
		if (st == AKP_ERROR) {
			ak_play_stop(pl);
			lang_puts("play : erreur pendant la lecture\n");
			_exit(1);
		}
		char *e = fmt_time(ak_play_pos(pl), tbuf);
		e = str_cat(e, " / ");
		e = fmt_time(dur, e);
		str_cat(e, "\n");
		ak_puts(tbuf);
	}
}
