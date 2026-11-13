/*
 * mediaplayer -- lecteur multimedia (video et son) : MediaKit (SD0:/lib/mediakit.so : demultiplexage,
 * VP8 / VP9 / AV1, son Opus en secours) pour les images et le son, la surface attachee d'Elegant pour
 * l'image (aucune recopie : MediaKit convertit et met a l'echelle directement dans le tampon partage
 * qu'Elegant compose), AudioKit pour le son (flux PCM s16 stereo 44,1 kHz), synchronisation sur
 * l'horloge du son (celle du systeme sans son). Interface UIKit : fenetre redimensionnable, barre de
 * lecture (lecture / pause, ouvrir, position, temps, volume), menus, raccourcis.
 *
 *	mediaplayer [fichier] [loop] [autoexit]
 *
 * loop : recommence a la fin ; autoexit : quitte a la fin (essais) ; a la fin de chaque lecture, le
 * bilan (images montrees, sautees, images/s) est ecrit sur la sortie standard. MediaKit absent :
 * message, le reste marche. Raccourcis : Espace lecture / pause, gauche / droite -5 / +5 s, Ctrl+O,
 * Ctrl+W.
 *
 * Port en C de mediaplayer.asm (memes fenetre, menus, raccourcis, textes, messages), un seul source
 * pour le Pi et le PC ; l'asm reste dans le dossier jusqu'a sa validation sur le Pi. Bizarreries de
 * l'asm gardees : Lecture apres la fin repart du debut sans rouvrir le son (horloge du systeme).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <armpi/syscall.h>
#include <uikit.h>
#include <audiokit_abi.h>
#include <mediakit.h>
#include <mediakit_abi.h>

#define MP_WIN_W	680
#define MP_WIN_H	440
#define MP_BAR_H	48			/* barre de lecture */
#define MP_RATE		44100			/* son : le format des flux de sound */
#define MP_CHUNK	1024			/* trames par lecture de son */
#define MP_UI_MS	250			/* temps et position : mis a jour */
#define MP_LATE_US	150000			/* image en retard de plus : sautee */
#define MP_MAX_DROP	4			/* sautees de suite au plus */
#define MP_STALL_MS	1500			/* son qui n'avance plus : horloge du systeme */
#define MP_TAIL_BYTES	16384			/* fin : ce qui reste dans le flux (~90 ms) */
#define MP_ID_OPEN	1
#define MP_ID_CLOSE	2
#define MP_ID_PLAY	3
#define MP_ID_START	4

#define SYS_TIME_MS_	0x19

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static long now_ms (void) { return armpi_syscall (SYS_TIME_MS_, 0, 0, 0, 0, 0, 0); }

/* les textes */
static const char s_app[] = "Lecteur multim\xC3\xA9" "dia";
static const char s_title[] = "Lecteur multim\xC3\xA9" "dia - ";
static const char s_dir[] = "SD0:/home/video";
static const char s_playg[] = "\xE2\x96\xB6";			/* ▶ */
static const char s_pauseg[] = "\xE2\x9D\x9A\xE2\x9D\x9A";		/* ❚❚ */
static const char m_nofile[] = "Fichier > Ouvrir (Ctrl+O) : WebM, Matroska, MP4 (VP8, VP9, AV1)";
static const char m_audioonly[] = "Son seul";
static const char m_nosurf[] = "Surface d'Elegant indisponible (Elegant trop ancien ?)";
static const char m_nomk[] = "MediaKit est absent : SD0:/lib/mediakit.so (prebuilt/mediakit.so, construit par "
	"tools/mediakit/build.sh). Le lecteur ne peut pas lire de vid\xC3\xA9o sans lui.";
static const char m_openko[] = "Lecture impossible : ";
static const char menus[] = "MFichier\n"
	"I1\tOuvrir\xE2\x80\xA6\tCtrl+O\n"
	"-\n"
	"I2\tFermer\tCtrl+W\n"
	"MLecture\n"
	"I3\tLecture / pause\tEspace\n"
	"I4\tAu d\xC3\xA9" "but\n";

/* l'etat */
static int mk_ok;				/* MediaKit present */
static long mh;					/* poignee de MediaKit */
static const unsigned *jetons;
static long win_w = MP_WIN_W, win_h = MP_WIN_H;
static long zone_x, zone_y, zone_w, zone_h;	/* zone de l'image */
static long surf;				/* surface attachee (UIKit) */
static long w_play, w_open, w_pos, w_time, w_vol;
static const char *msg = m_nofile;		/* texte de la zone de l'image (0 : aucun) */
static long volume = 80;
static int opt_loop, opt_autoexit;
static int playing, ended, has_video, pending, video_eof, audio_eof, audio_on;
static long stream;				/* flux d'AudioKit (ouvert si audio_on) */
static int a_first;				/* 1 : la base de l'horloge pas encore lue */
static int64_t a_pts;
static long a_base;				/* temps du premier paquet ecrit (us) */
static long a_lastpos, a_posms;
static long clk_base, clk_t0;			/* horloge du systeme : temps (us) a clk_t0 */
static long last_ui, user_ms;			/* user_ms : dernier geste sur la glissiere */
static long shown, dropped, drop_run, play_t0;
static struct mk_info info;
static struct mk_frame frame;
static struct mk_stats stats;
static char path[260], titlebuf[300], errbuf[200], timebuf[48];
static int16_t abuf[MP_CHUNK * 2];

static void quit (void);
static void seek_to (long ms);

/* d = destination, s = morceau de texte -> fin : copie traduite. Les espaces autour du morceau restent
 * ici (un dictionnaire ne garde pas ceux d'une cle). */
static char *tr_cat (char *d, const char *s)
{
	char key[240];
	while (*s == ' ')
		*d++ = *s++;
	int n = 0, k = 0;
	while (s[n] && n < 239)
	{
		key[n] = s[n];
		n++;
		if (s[n - 1] != ' ')
			k = n;
	}
	key[k] = 0;
	d = stpcpy (d, TR (key));
	return stpcpy (d, s + k);
}

/* la sortie standard (puts d'AppKit : sans saut de ligne ajoute) */
static void out_s (const char *s) { fputs (s, stdout); fflush (stdout); }
static void out_d (long v) { printf ("%ld", v); fflush (stdout); }

/* ---- arguments, MediaKit ---------------------------------------------------------------------------- */

/* les mots : « loop », « autoexit », sinon le fichier */
static void parse_args (int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "loop"))
			opt_loop = 1;
		else if (!strcmp (argv[i], "autoexit"))
			opt_autoexit = 1;
		else
		{
			int n = 0;
			for (; argv[i][n] && n < 250; n++)
				path[n] = argv[i][n];
			path[n] = 0;
		}
	}
}

/* ---- interface ------------------------------------------------------------------------------------- */

/* zone de l'image : la video ajustee a la zone au-dessus de la barre, centree ; la surface suit (creee
 * au premier besoin) */
static void fit_surface (void)
{
	if (!has_video)
		return;
	long zw = win_w, zh = win_h - MP_BAR_H;
	long vw = info.width, vh = info.height, sw, sh;
	if (vw < 1 || vh < 1)
		return;
	if ((unsigned long) (vw * zh) > (unsigned long) (vh * zw))
	{
		sw = zw;				/* la largeur limite */
		sh = (long) ((unsigned long) (vh * zw) / (unsigned long) vw);
	}
	else
	{
		sh = zh;
		sw = (long) ((unsigned long) (vw * zh) / (unsigned long) vh);
	}
	sw &= ~1L;
	sh &= ~1L;
	if (sw < 2)
		sw = 2;
	if (sh < 2)
		sh = 2;
	zone_x = (long) ((unsigned long) (zw - sw) >> 1);	/* centree */
	zone_y = (long) ((unsigned long) (zh - sh) >> 1);
	zone_w = sw;
	zone_h = sh;
	if (surf)
		ui_surface_move (surf, zone_x, zone_y, sw, sh);	/* (deplacee, tampon neuf s'il le faut) */
	else
		surf = ui_surface (zone_x, zone_y, sw, sh, WS_SF_XRGB8888);
}

/* places des widgets et de la surface d'apres win_w, win_h */
static void layout (void)
{
	long y = win_h - (MP_BAR_H - 8);		/* y de la barre (+ 8) */
	ui_w_move (w_play, 8, y, 44, 32);
	ui_w_move (w_open, 56, y, 80, 32);
	long pw = win_w - (144 + 8 + 120 + 8 + 100 + 8);	/* glissiere de position */
	if (pw < 40)
		pw = 40;
	ui_w_move (w_pos, 144, y + 2, pw, 28);
	long x = pw + 144 + 8;				/* temps */
	ui_w_move (w_time, x, y + 2, 120, 28);
	ui_w_move (w_vol, x + 128, y + 2, 100, 28);	/* volume */
	fit_surface ();
}

static void make_widgets (void)
{
	w_play = ui_pushbutton (0, 0, 44, 32, (long) s_playg, 0);
	w_open = ui_pushbutton (0, 0, 80, 32, (long) "Ouvrir", UB_GHOST);
	w_pos = ui_slider (0, 0, 100, 28, 0, 1000, 0);
	w_time = ui_label (0, 0, 100, 28, (long) "0:00 / 0:00", ULB_RIGHT);
	w_vol = ui_slider (0, 0, 90, 28, 0, 100, volume);
	layout ();
}

/* fond (zone de l'image noire, barre), message, widgets */
static void paint (void)
{
	/* (la zone de l'image est noire comme une video, quel que soit le theme ; le message y est
	 * gris clair : comme l'asm) */
	ui_fill (0, 0, win_w, win_h - MP_BAR_H, 0);
	ui_fill (0, win_h - MP_BAR_H, win_w, MP_BAR_H, jetons[TJ_FACE / 4]);	/* barre */
	if (msg)
	{
		long w = ui_text_width ((long) TR (msg), 15, (long) ui.font);
		long x = (win_w - w) >> 1;
		if (x < 8)
			x = 8;
		long y = (long) ((unsigned long) (win_h - MP_BAR_H) >> 1);
		ui_text_fit ((long) TR (msg), x, y, 0x00C8CDD5, 15, (long) ui.font, win_w - 16);
	}
	ui_w_draw (0);
	ui_update_all ();
}

/* texte du bouton de lecture d'apres playing */
static void play_label (void)
{
	ui_w_set_text (w_play, (long) (playing ? s_pauseg : s_playg));
}

/* d = tampon, ms -> fin (« m:ss ») */
static char *fmt_time (char *d, unsigned long ms)
{
	unsigned long s = ms / 1000;
	return d + sprintf (d, "%lu:%s%lu", s / 60, s % 60 < 10 ? "0" : "", s % 60);
}

/* ---- son ------------------------------------------------------------------------------------------- */

static void apply_volume (void)
{
	if (audio_on)
		ak_volume (stream, (long) ((unsigned long) (volume << 8) / 100));	/* 0..100 -> 0..256 */
}

static void audio_stop (void)
{
	if (!audio_on)				/* (audio_on : le flux est ouvert) */
		return;
	audio_on = 0;
	ak_close (stream);
}

/* MediaKit en s16 stereo 44,1 kHz, flux d'AudioKit au meme format (audio_on = 1) ; sinon horloge du
 * systeme */
static void audio_start (void)
{
	audio_stop ();
	if (!mh || info.atrack < 0 || !info.aok)
		return;
	if (mk_audio_open (mh, MP_RATE) < 0)
		return;
	long s = ak_open (MP_RATE, 2, 16, 0);	/* (poignee >= 0 : 0 est valable) */
	if (s < 0)
	{
		out_s ("mediaplayer : pas de sortie son (AudioKit) : horloge du syst\xC3\xA8me\n");
		return;
	}
	stream = s;
	audio_on = 1;
	a_first = 1;				/* l'horloge part du premier paquet */
	a_lastpos = a_posms = 0;
	audio_eof = 0;
	apply_volume ();
}

/* le flux d'AudioKit garde plein (sans attendre) */
static void feed_audio (void)
{
	while (audio_on && !audio_eof && ak_space (stream) >= MP_CHUNK * 4)
	{
		long n = mk_audio_read (mh, (long) abuf, MP_CHUNK, (long) &a_pts);
		if (n <= 0)
		{
			audio_eof = 1;
			return;
		}
		if (a_first)			/* premier paquet : base de l'horloge */
		{
			a_first = 0;
			a_base = (long) a_pts;
		}
		ak_write (stream, (long) abuf, n * 4);
	}
}

/* trames jouees -> temps (us) */
static long aclk_val (long frames)
{
	return (long) ((unsigned long) (frames * 1000000) / MP_RATE) + a_base;
}

/* -> temps de la lecture (us) : horloge du son (premier paquet + trames jouees), sinon du systeme ; un
 * son qui n'avance plus (pas de sortie) rend la main a l'horloge du systeme */
static long clock_now (void)
{
	if (!playing)
		return clk_base;
	if (audio_on && !a_first)
	{
		long pos = ak_position (stream);
		long t = now_ms ();
		if (pos != a_lastpos)
		{
			a_lastpos = pos;
			a_posms = t;
			return aclk_val (pos);
		}
		if (audio_eof)			/* (son fini : il s'arrete, normal) */
			return aclk_val (pos);
		if (!a_posms)
		{
			a_posms = t;
			return aclk_val (pos);
		}
		if ((unsigned long) (t - a_posms) < MP_STALL_MS)
			return aclk_val (pos);
		out_s ("mediaplayer : le son n'avance plus : horloge du syst\xC3\xA8me\n");
		clk_base = aclk_val (pos);
		clk_t0 = t;
		audio_stop ();
	}
	return (now_ms () - clk_t0) * 1000 + clk_base;
}

/* temps (« m:ss / m:ss ») et position, toutes les MP_UI_MS (pas pendant qu'on tire la glissiere) */
static void ui_progress_update (void)
{
	long t = now_ms ();
	if ((unsigned long) (t - last_ui) < MP_UI_MS)
		return;
	last_ui = t;
	long ms = clock_now () / 1000;		/* ms, de 0 a la duree */
	if (ms < 0)
		ms = 0;
	unsigned long dur = (unsigned long) info.duration / 1000;
	if (ms > (long) dur)
		ms = (long) dur;
	char *e = fmt_time (timebuf, (unsigned long) ms);
	e = stpcpy (e, " / ");
	fmt_time (e, dur);
	ui_w_set_text (w_time, (long) timebuf);
	if ((unsigned long) (t - user_ms) >= 700)
		ui_w_set_value (w_pos, ms);
}

/* ---- le fichier ------------------------------------------------------------------------------------ */

static void reset_counters (void)
{
	shown = dropped = drop_run = 0;
	pending = video_eof = audio_eof = 0;
}

/* ferme le fichier ouvert (son, surface gardee) */
static void close_media (void)
{
	audio_stop ();
	if (mh)
	{
		long h = mh;
		mh = 0;
		mk_close (h);
	}
	playing = has_video = pending = video_eof = audio_eof = 0;
}

/* ouvre « path » (l'ancien ferme) et commence la lecture */
static void open_media (void)
{
	close_media ();
	long h = mk_open ((long) path);
	if (h < 0)
	{
		const char *err = (const char *) mk_error_text (h);	/* message : l'erreur */
		char *e = tr_cat (errbuf, m_openko);
		strcpy (e, err ? err : "");
		msg = errbuf;
		ui_title ((long) TR (s_app));
		return;
	}
	mh = h;
	mk_info (mh, (long) &info);
	has_video = (int) info.vok;
	msg = info.vok ? 0 : m_audioonly;		/* message : son seul, ou rien */
	long d = (long) ((unsigned long) info.duration / 1000);	/* glissiere : 0 .. duree (ms), pas 100 ms */
	if (d < 1)
		d = 1;
	ui_w_set_range (w_pos, 0, d, 0, 100);
	char *e = tr_cat (titlebuf, s_title);	/* « Lecteur multimédia - nom » */
	const char *base = path;
	for (const char *p = path; *p; p++)
		if (*p == '/')
			base = p + 1;
	strcpy (e, base);
	ui_title ((long) titlebuf);
	fit_surface ();
	if (has_video && !surf)			/* pas de surface : son seul */
	{
		has_video = 0;
		msg = m_nosurf;
	}
	audio_start ();				/* son (s'il est decode par MediaKit) */
	if (info.atrack >= 0 && !info.aok)
		out_s ("mediaplayer : son pour AudioKit (pas encore de d\xC3\xA9" "codeur par paquets) : muet\n");
	reset_counters ();
	play_t0 = now_ms ();
	playing = 1;
	ended = 0;
	clk_t0 = now_ms ();
	clk_base = 0;
	play_label ();
}

/* « mediaplayer : N images montrees, M sautees en T ms : F images/s ; decodage moyen D us » */
static void report (void)
{
	out_s ("mediaplayer : ");
	out_d (shown);
	out_s (" images montr\xC3\xA9" "es, ");
	out_d (dropped);
	out_s (" saut\xC3\xA9" "es en ");
	long t = now_ms () - play_t0;
	out_d (t);
	out_s (" ms : ");
	if (t < 1)
		t = 1;
	unsigned long f = (unsigned long) (shown * 10000) / (unsigned long) t;	/* images/s (une decimale) */
	out_d ((long) (f / 10));
	out_s (",");
	out_d ((long) (f % 10));
	out_s (" images/s ; d\xC3\xA9" "codage moyen ");
	mk_stats (mh, (long) &stats);
	long n = stats.vframes < 1 ? 1 : (long) stats.vframes;
	out_d ((long) ((unsigned long) stats.vdecode_us / (unsigned long) n));
	out_s (" us");
	out_s (audio_on ? " ; son par AudioKit\n" : " ; sans son\n");
	play_t0 = now_ms ();			/* (lecture suivante : loop) */
	shown = dropped = 0;
}

/* ---- lecture --------------------------------------------------------------------------------------- */

/* l'image gardee par MediaKit dans la surface (a sa taille), signalee */
static void show_held (void)
{
	if (!has_video || !surf)
		return;
	long pitch = 0;
	long px = uikit_call3 ((long (*) ()) ui_surface_pixels, surf, 0, 0, 0, &pitch, 0);
	if (!mh)
		return;
	if (mk_video_convert (mh, px, pitch, zone_w, zone_h) < 0)
		return;
	ui_surface_damage (surf, 0, 0, 0, 0);
}

/* fin : bilan sur la sortie standard ; loop : recommence ; autoexit : quitte */
static void end_of_media (void)
{
	report ();
	if (opt_loop)
	{
		seek_to (0);
		return;
	}
	if (opt_autoexit)
		quit ();
	clk_base = clock_now ();		/* a l'arret : sur la fin */
	playing = 0;
	ended = 1;
	audio_stop ();
	play_label ();
	last_ui = 0;
	ui_progress_update ();
}

/* fin de la lecture : images finies (ou pas d'image) et son fini */
static void check_end (void)
{
	if (has_video && !video_eof)
		return;
	if (audio_on)
	{
		if (!audio_eof)
			return;
		/* ce qui est ecrit, joue (le dernier bloc partiel de sound peut rester : a peu pres), ou la
		 * duree passee */
		if (ak_pending (stream) > MP_TAIL_BYTES && clock_now () < info.duration)
			return;
	}
	else if (!has_video && info.atrack >= 0 && clock_now () < info.duration)
		return;				/* son muet seul : jusqu'a la duree */
	end_of_media ();
}

/* le tour de la lecture : son, image due, fin, temps affiche */
static void tick (void)
{
	if (!playing)
		return;
	feed_audio ();
	if (has_video)
	{
		for (int n = 8; !video_eof;)	/* au plus 8 images par tour */
		{
			if (!pending)
			{
				if (mk_video_next (mh, (long) &frame) != 1)	/* l'image suivante, decodee */
				{
					video_eof = 1;
					break;
				}
				pending = 1;
			}
			long late = clock_now () - (long) frame.pts;	/* avance de l'horloge sur l'image */
			if (late < 0)
				break;			/* pas encore l'heure */
			if (late >= MP_LATE_US && drop_run < MP_MAX_DROP)
			{				/* tres en retard : sautee (pas plus de MP_MAX_DROP de suite) */
				drop_run++;
				dropped++;
				pending = 0;
				if (--n == 0)
					break;
				continue;
			}
			show_held ();
			drop_run = 0;
			shown++;
			pending = 0;
			break;
		}
	}
	check_end ();
	ui_progress_update ();
}

/* son repris sans recherche : flux neuf, horloge du premier paquet lu */
static void audio_restart (void)
{
	if (!info.aok || audio_eof)
		return;
	long s = ak_open (MP_RATE, 2, 16, 0);
	if (s < 0)
		return;
	stream = s;
	audio_on = 1;
	a_first = 1;
	a_lastpos = a_posms = 0;
	apply_volume ();
}

/* lecture / pause (a la fin : depuis le debut) */
static void toggle_play (void)
{
	if (!mh)
		return;
	if (ended)
	{
		ended = 0;
		seek_to (0);			/* finie : depuis le debut */
		playing = 1;
	}
	else if (playing)
	{
		clk_base = clock_now ();	/* pause : l'horloge arretee la, son coupe */
		playing = 0;
		audio_stop ();
	}
	else
	{
		/* reprise : horloge du systeme depuis la pause, puis celle du son (le son repart ou il en
		 * etait) */
		clk_t0 = now_ms ();
		playing = 1;
		audio_restart ();
	}
	play_label ();
}

/* ms : recherche (au point d'acces precedent), son repris la */
static void seek_to (long ms)
{
	if (!mh)
		return;
	long r = mk_seek (mh, ms * 1000);
	if (r < 0)
		return;				/* (pas d'index : rien) */
	clk_base = r;				/* temps atteint */
	clk_t0 = now_ms ();
	pending = video_eof = audio_eof = 0;
	drop_run = 0;
	ended = 0;
	if (playing)
		audio_start ();
	else if (has_video && mk_video_next (mh, (long) &frame) == 1)
		show_held ();			/* en pause : l'image du point atteint */
	last_ui = 0;
	user_ms = 0;
	ui_progress_update ();
}

/* fonction de ui_on_resize */
static void on_resize (long w, long h)
{
	win_w = w;
	win_h = h;
	layout ();
	paint ();
	show_held ();				/* l'image, a la nouvelle taille */
}

static void quit (void)
{
	close_media ();
	exit (0);
}

static void command (long id)
{
	if (id == MP_ID_CLOSE)
		quit ();
	else if (id == MP_ID_OPEN)
	{
		if (!mk_ok)
		{
			ui_message ((long) s_app, (long) TR (m_nomk), UM_OK);
			return;
		}
		if (!ui_file_open ((long) s_dir, (long) path, 256))
			return;
		open_media ();
		paint ();
	}
	else if (id == MP_ID_PLAY)
		toggle_play ();
	else if (id == MP_ID_START)
		seek_to (0);
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui) || !audiokit_kit_open ())
	{
		fputs ("mediaplayer : UIKit ou AudioKit absent\n", stderr);
		return 1;
	}
	parse_args (argc, argv);
	if (ui_window_ex (-1, -1, MP_WIN_W, MP_WIN_H, (long) TR (s_app), WF_RESIZABLE | WF_GLYPH_LIGHT | WF_BODY_DARK) < 0)
		return 0;
	ui_menu ((long) menus);
	jetons = (const unsigned *) ui_tokens ();
	ui_min_size (360, 240);
	ui_on_resize ((long) on_resize, 0);
	make_widgets ();
	mk_ok = mediakit_kit_open ();		/* MediaKit (message s'il manque) */
	if (path[0] && mk_ok)
		open_media ();
	paint ();
	for (;;)
	{
		long t = ui_event (playing ? 4 : 200);	/* attente : courte en lecture */
		if (t == WEV_CLOSE)
			quit ();
		if (t == WEV_MENU)
			command (ui_ev_field (&ui, EV_CODE));
		else if (t)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (w)
			{
				if (code == UN_CHANGE)
				{
					if (w == w_pos)
					{
						long v = ui_w_get_value (w);	/* position choisie (ms) */
						user_ms = now_ms ();
						seek_to (v);
					}
					else if (w == w_vol)
					{
						volume = ui_w_get_value (w);
						apply_volume ();
					}
				}
				else if (code == UN_ACTIVATE)
				{
					if (w == w_play)
						toggle_play ();
					else if (w == w_open)
						command (MP_ID_OPEN);
				}
			}
			else if (!taken && ui_ev_field (&ui, EV_TYPE) == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0)
			{
				long id = ui_menu_key (0);	/* Ctrl+O, Ctrl+W, Espace */
				if (id != -1)
					command (id);
				else
				{
					int k = ui_ev_field (&ui, EV_CODE);
					if (k == 0x2C)		/* Espace */
						toggle_play ();
					else if (k == 0x50 || k == 0x4F)	/* gauche / droite : -5 / +5 s */
					{
						long ms = clock_now () / 1000 + (k == 0x50 ? -5000 : 5000);
						seek_to (ms < 0 ? 0 : ms);
					}
				}
			}
		}
		tick ();
	}
}
