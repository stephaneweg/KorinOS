/*
 * recorder -- magnetophone : enregistrer (micro ou sortie du mixeur), ecouter, sauver
 * (/enregistrement.wav, .mp3 ou .opus : format choisi dans la liste deroulante, encodeurs
 * d'AudioKit ak_enc_*), vumetre.
 * Argument « --test [source] [wav|mp3|opus] » : enregistre 2,5 s de la sortie principale (boucle, ou
 * la source donnee : 256 + n boucle de la sortie n, 512 + n entree n ; docs/SON.md 4.2), sauve (WAV par
 * defaut) et quitte (test automatique). Opus n'est propose que si l'encodeur d'AudioKit existe.
 * Fenetre de taille fixe ; agrandie (mode poche, WEV_RESIZE : docs/MODE-POCHE.md), le vumetre s'etire,
 * la liste du format suit le bord droit (ui_anchor).
 *
 * Port en C de recorder.asm (memes boutons, textes, fichiers et messages du mode test), un seul
 * source pour le Pi et le PC ; l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <armpi/syscall.h>
#include <uikit.h>
#include <audiokit_abi.h>

#define SYS_SLEEP_	0x17
#define SYS_TIME_MS_	0x19
#define REC_SRC_MIC	0			/* src/inc/sound.inc */
#define REC_SRC_LOOP	1
#define E_NOSYS_	(-38)

#define WIN_W	540
#define WIN_H	250
#define MAX_SEC	30
#define BUF_MAX	(SND_RATE * 4 * MAX_SEC)
#define BTN_Y	150
#define BTN_H	34
#define METER_Y	104

#define RCD_MP3_BPS	128000			/* debit du MP3 (bits/s) */
#define RCD_CHUNK	4096			/* trames par ak_enc_write */
enum { RCD_FMT_WAV, RCD_FMT_MP3, RCD_FMT_OPUS };	/* formats : element de la liste deroulante */
enum { ST_IDLE, ST_REC, ST_PLAY };

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static unsigned tok (int off) { return ((const unsigned *) ui_tokens ())[off / 4]; }

static const char s_ready[] = "Pr\xC3\xAAt.";
static const char s_recording[] = "Enregistrement en cours...";
static const char s_stopped[] = "Arr\xC3\xAAt\xC3\xA9.";
static const char s_playing[] = "Lecture...";
static const char s_encoding[] = "Encodage et \xC3\xA9" "criture...";
static const char s_savefail[] = "Erreur d'\xC3\xA9" "criture du fichier";
static const char s_nomic[] = "Aucun micro d\xC3\xA9tect\xC3\xA9 (essayez la source \xC2\xAB boucle \xC2\xBB)";
static const char s_busy[] = "Enregistreur d\xC3\xA9j\xC3\xA0 utilis\xC3\xA9";
static const char *const btn_labels[5] = { "Enreg.", "Source", "Arr\xC3\xAAt", "\xC3\x89" "couter", "Sauver" };	/* (« Enregistrer » : Save dans res/lang) */
static const long fmt_akf[3] = { AKF_WAV, AKF_MP3, AKF_OPUS };
static const long fmt_bps[3] = { 0, RCD_MP3_BPS, 0 };
static const char *const fmt_exts[3] = { ".wav", ".mp3", ".opus" };
static const char *const fmt_names[3] = { "WAV", "MP3", "Opus" };

static long fmt = RCD_FMT_WAV;
static long opus_ok;
static long w_fmt;
static const char *msg = s_ready;
static uint8_t *buf;
static unsigned long len, ppos;
static long state, gui, source, stream, rec;
static unsigned long peak, peak_max;
static char path[64];
static char msgbuf[128];

static long sys_ms (void) { return armpi_syscall (SYS_TIME_MS_, 0, 0, 0, 0, 0, 0); }
static void sys_sleep (long ms) { armpi_syscall (SYS_SLEEP_, ms, 0, 0, 0, 0, 0); }

/* ---- le dessin ---- */

/* vumetre + duree */
static void draw_meter (void)
{
	char t[32];
	if (!gui)
		return;
	ui_fill (20, METER_Y, ui.w - 40, 16, tok (TJ_SURFACE));
	ui_frame (20, METER_Y, ui.w - 40, 16, tok (TJ_LINE_STRONG));
	ui_fill (22, METER_Y + 2, (long) ((peak * (unsigned long) (ui.w - 44)) >> 15), 12, 0x0040B060);	/* crete * (W-44) / 32768 */
	ui_fill (0, BTN_Y + BTN_H + 8, ui.w, 30, tok (TJ_FACE));			/* duree enregistree */
	unsigned long d = len / (SND_RATE * 4 / 10);					/* dixiemes de seconde */
	snprintf (t, sizeof t, "%lu,%lu s", d / 10, d % 10);
	ui_text ((long) t, 20, BTN_Y + BTN_H + 28, tok (TJ_TEXT2), 14, (long) ui.font, 0);
	ui_update (0, METER_Y, ui.w, ui.h - METER_Y);
}

static void draw (void)
{
	if (!gui)					/* pas de fenetre (mode test) */
		return;
	ui_fill (0, 0, ui.w, ui.h, tok (TJ_FACE));
	ui_text ((long) TR ("Magn\xC3\xA9tophone"), 20, 34, tok (TJ_TEXT1), 20, (long) ui.font_b, 0);
	ui_text ((long) TR (source ? "Source : sortie du mixeur (boucle)" : "Source : micro (USB ou I2S)"), 20, 62, tok (TJ_TEXT2),
		 14, (long) ui.font, 0);
	ui_text ((long) TR (msg), 20, 86, tok (TJ_TEXT1), 14, (long) ui.font, 0);
	/* « Format » devant la liste deroulante */
	ui_text ((long) TR ("Format"), ui.w - (20 + 170 + 62 + FRAME_CAP_W), 36, tok (TJ_TEXT2), 14, (long) ui.font, 0);
	for (int i = 0; i < 5; i++)			/* boutons */
		ui_button (20 + 104 * i, BTN_Y, 96, BTN_H, (long) TR (btn_labels[i]), i == 0);
	ui_w_draw (0);					/* widgets (format) */
	draw_meter ();
	ui_update_all ();
}

static void on_resize (void) { draw (); }		/* autre taille : tout redessine */

/* ---- enregistrer, ecouter ---- */

static void stop_all (void)
{
	long s = state;
	state = ST_IDLE;
	if (s == ST_REC)
	{
		ak_rec_close (rec);
		msg = s_stopped;
	}
	if (s == ST_PLAY)
		ak_close (stream);
}

/* demarre l'enregistrement depuis la source choisie */
static void start_rec (void)
{
	stop_all ();
	long r = ak_rec_open (source);
	if (r < 0)
	{
		msg = r == E_NOSYS_ ? s_nomic : s_busy;	/* E_NOSYS : pas de micro */
		return;
	}
	rec = r;
	msg = s_recording;
	len = 0;
	peak_max = 0;
	state = ST_REC;
}

static void start_play (void)
{
	stop_all ();
	if (!len)
		return;
	long s = ak_open (SND_RATE, 2, 16, 0);
	if (s < 0)
		return;
	stream = s;
	ppos = 0;
	state = ST_PLAY;
	msg = s_playing;
}

/* echantillons s16, octets -> crete absolue */
static unsigned long peak_of (const uint8_t *p, unsigned long n)
{
	unsigned long m = 0;
	const int16_t *s = (const int16_t *) p;
	for (unsigned long i = 0; i < n / 2; i++)
	{
		int v = s[i];
		unsigned long a = (unsigned long) (v < 0 ? -v : v);
		if (a > m)
			m = a;
	}
	return m;
}

/* travail periodique : lire la capture / alimenter la lecture */
static void tick (void)
{
	if (state == ST_REC)
	{
		unsigned long room = BUF_MAX - len;
		if (!room)
		{
			stop_all ();
			draw ();
			return;
		}
		uint8_t *p = buf + len;
		long n = ak_rec_read (rec, (long) p, (long) room);
		if (n > 0)
		{
			len += (unsigned long) n;
			peak = peak_of (p, (unsigned long) n);	/* crete des echantillons lus */
			if (peak > peak_max)
				peak_max = peak;
		}
		draw_meter ();
	}
	else if (state == ST_PLAY)
	{
		unsigned long left = len - ppos;
		if (!left)
		{
			long st[4];			/* tout est ecrit : fin quand vide */
			ak_status (stream, (long) st);
			if (st[0])
				return;
			stop_all ();
			msg = s_stopped;
			draw ();
			return;
		}
		uint8_t *p = buf + ppos;
		long n = ak_write (stream, (long) p, (long) left);
		if (n <= 0)
			return;
		ppos += (unsigned long) n;
		peak = peak_of (p, (unsigned long) n);
		draw_meter ();
	}
}

/* encode l'enregistrement (PCM s16 stereo 44,1 kHz) au format choisi : /enregistrement.wav, .mp3
 * (128 kbit/s) ou .opus (ak_enc_*) ; msg */
static void save_rec (void)
{
	snprintf (path, sizeof path, "/enregistrement%s", fmt_exts[fmt]);
	long e = ak_enc_open ((long) path, fmt_akf[fmt], SND_RATE, 2, fmt_bps[fmt]);
	if (e < 0)
	{
		msg = s_savefail;
		return;
	}
	unsigned long frames = len >> 2;
	const uint8_t *p = buf;
	while (frames)
	{
		unsigned long k = frames < RCD_CHUNK ? frames : RCD_CHUNK;
		if (ak_enc_write (e, (long) p, (long) k) < 0)
		{
			ak_enc_close (e);
			msg = s_savefail;
			return;
		}
		frames -= k;
		p += k << 2;
	}
	if (ak_enc_close (e) < 0)
	{
		msg = s_savefail;
		return;
	}
	snprintf (msgbuf, sizeof msgbuf, "%s%s", TR ("Sauv\xC3\xA9 dans "), path);	/* « Sauve dans <chemin> » */
	msg = msgbuf;
}

/* ---- le format ---- */

/* ecriture de l'essai : (ctx, tampon, n) -> n, rien n'est garde */
static long null_write (long ctx, long b, long n) { (void) ctx; (void) b; return n; }

/* l'encodeur Opus d'AudioKit existe-t-il ? (essai sans fichier) -> opus_ok */
static void probe_opus (void)
{
	long e = ak_enc_open_io ((long) null_write, 0, 0, AKF_OPUS, SND_RATE, 2, 0);
	if (e < 0)					/* AKE_FORMAT : pas encore */
		return;
	ak_enc_close (e);
	opus_ok = 1;
}

/* la liste deroulante du format (en haut a droite, a gauche des boutons de la fenetre) */
static void build_fmt (void)
{
	w_fmt = ui_dropdown (WIN_W - 20 - 170 - FRAME_CAP_W, 14, 170, 0,
			     (long) (opus_ok ? "WAV|MP3 128 kbit/s|Opus" : "WAV|MP3 128 kbit/s"));
	if (!w_fmt)
		return;
	ui_anchor (w_fmt, UA_RIGHT | UA_TOP);		/* (en haut a droite, a toute taille) */
	ui_w_set_value (w_fmt, fmt);
}

/* ---- mode test : boucle 2,5 s -> /enregistrement.wav (.mp3, .opus) ---- */

/* mot (fini par NUL ou un blanc) == attendu ? */
static int word_is (const char *w, const char *x)
{
	while (*x)
		if (*w++ != *x++)
			return 0;
	return (unsigned char) *w <= ' ';
}

static int auto_test (const char *args)
{
	const char *last = args;			/* format : dernier mot (wav, mp3, opus) */
	unsigned char prev = ' ';
	for (const char *s = args; *s; prev = (unsigned char) *s++)
		if (prev <= ' ' && (unsigned char) *s > ' ')
			last = s;			/* un mot commence */
	fmt = word_is (last, "mp3") ? RCD_FMT_MP3 : word_is (last, "opus") ? RCD_FMT_OPUS : RCD_FMT_WAV;
	printf ("magneto : format %s, encodeur Opus %ld\n", fmt_names[fmt], opus_ok);
	if (fmt == RCD_FMT_OPUS && !opus_ok)
	{
		fputs ("magneto : encodeur Opus absent (AKE_FORMAT), rien enregistre\n", stdout);	/* on s'arrete */
		return 0;
	}
	long r = ak_rec_open (REC_SRC_MIC);		/* sans micro : E_NOSYS attendu sous QEMU */
	printf ("magneto : ak_rec_open(micro) = %ld\n", r);
	fflush (stdout);
	if (r >= 0)
		ak_rec_close (r);
	sys_sleep (1500);				/* laisser sontest jouer */
	long src = REC_SRC_LOOP;			/* source donnee apres --test ? */
	const char *s = args + 6;
	while (*s == ' ')
		s++;
	if ((unsigned) (*s - '0') <= 9)
		src = strtol (s, 0, 10);
	source = src;
	start_rec ();
	if (state != ST_REC)
		return 0;
	long end = sys_ms () + 2500;
	do
	{
		tick ();
		sys_sleep (20);
	}
	while ((unsigned long) sys_ms () < (unsigned long) end);
	stop_all ();
	save_rec ();
	printf ("magneto : enregistrement sauve, octets %ld, crete %ld\n", (long) len, (long) peak_max);
	printf ("magneto : %s", msg);			/* « Sauve dans ... » ou l'erreur */
	struct stat st;					/* taille du fichier */
	long size = stat (path, &st) == 0 ? (long) st.st_size : -(long) errno;
	printf (", fichier (octets) %ld\n", size);
	return 0;
}

int main (int argc, char **argv)
{
	char args[256] = "";				/* la ligne d'arguments (comme l'asm) */
	for (int i = 1; i < argc; i++)
	{
		if (i > 1)
			strncat (args, " ", sizeof args - strlen (args) - 1);
		strncat (args, argv[i], sizeof args - strlen (args) - 1);
	}
	if (!uikit_kit_open () || !audiokit_kit_open ())
		return 1;
	buf = malloc (BUF_MAX);
	if (!buf)
		return 0;
	probe_opus ();
	if (args[0] == '-')				/* "--test" ? (sans fenetre : pas d'ui_init, comme l'asm) */
		return auto_test (args);
	gui = 1;
	if (!ui_init ((long) &ui))
		return 1;
	if (ui_window (WIN_W, WIN_H, (long) TR ("Magn\xC3\xA9tophone")) < 0)
		return 0;
	build_fmt ();
	ui_on_resize ((long) on_resize, 0);
	draw ();
	for (;;)
	{
		tick ();
		long t = ui_event (20);
		if (t == 0)
			continue;
		if (t == WEV_CLOSE)
			break;
		long code, taken;
		long w = uikit_w_event (&code, &taken);	/* le widget du format d'abord */
		if (taken)
		{
			if (w == w_fmt && code == UN_CHANGE)
			{
				long v = ui_w_get_value (w);
				if (v >= 0)
					fmt = v;
			}
			continue;
		}
		if (t != WEV_MOUSE || ui_ev_field (&ui, EV_KIND) != 1)
			continue;
		int b = 0;					/* quel bouton ? */
		while (b < 5 && !ui_hit (20 + 104 * b, BTN_Y, 96, BTN_H))
			b++;
		switch (b)
		{
		case 0:
			start_rec ();
			break;
		case 1:
			source ^= 1;
			break;
		case 2:
			stop_all ();
			break;
		case 3:
			start_play ();
			break;
		case 4:
			msg = s_encoding;		/* l'encodage peut prendre un moment */
			draw ();
			save_rec ();
			break;
		default:
			continue;
		}
		draw ();
	}
	return 0;
}
