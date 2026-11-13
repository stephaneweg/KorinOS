/*
 * music -- lecteur de musique : les fichiers audio d'un dossier (WAV, FLAC, MP3, Ogg), joues par le
 * lecteur d'AudioKit (ak_play, un thread du process). Dossier par defaut SD0:/home/music (vide :
 * SD0:/res/audiotest) ; « Dossier… » : ui_folder_choose. Double-clic ou Lecture : joue ; Pause /
 * Reprise, Arret, Suivant ; glissiere de position (recherche au relache), glissiere de volume ; morceau
 * suivant a la fin.
 * Argument : un dossier (facultatif).
 * Le bus (docs/MODE-POCHE.md) : korin.media.now (garde) dit ce qui joue, a chaque changement ;
 * korin.media.control « music toggle|play|pause|next|stop » le pilote (le bouton de la carte Musique de
 * l'accueil).
 *
 * Port en C de music.asm (memes widgets, textes, sujets du bus), un seul source pour le Pi et le PC ;
 * l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <armpi/syscall.h>
#include <uikit.h>
#include <audiokit_abi.h>
#include <buskit_abi.h>

#define MUS_W		600
#define MUS_H		410
#define MUS_M		16
#define MUS_LIST_Y	46
#define MUS_LIST_H	180
#define MUS_POS_Y	330
#define MUS_BTN_Y	366
#define MUS_BTN_W	100
#define MUS_TICK	250			/* rafraichissement de la position (ms) */
#define MUS_PATH	260

#define SYS_TIME_MS_	0x19

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static long now_ms (void) { return armpi_syscall (SYS_TIME_MS_, 0, 0, 0, 0, 0, 0); }

static const char *const fmt_names[] = { "?", "WAV", "FLAC", "MP3", "Ogg Vorbis", "Ogg Opus", "MIDI", "FM Song" };
static const char s_title[] = "Musique";
static const char p_music[] = "SD0:/home/music";
static const char p_test[] = "SD0:/res/audiotest";
static const char *const exts[] = { ".wav", ".flac", ".mp3", ".ogg", ".oga", ".opus", ".mid", ".midi", ".fms", 0 };
static const char s_hint[] = "Double-cliquez un morceau pour l'\xC3\xA9" "couter";
static const char s_nofiles[] = "Aucun fichier audio dans ce dossier (WAV, FLAC, MP3, Ogg, MIDI, FM Song)";
static const char s_e_format[] = "Format non reconnu ou non pris en charge (Ogg Opus : pas encore)";
static const char s_e_corrupt[] = "Fichier invalide ou ab\xC3\xAEm\xC3\xA9";
static const char s_e_other[] = "Lecture impossible";
static const char s_e_play[] = "Erreur pendant la lecture";
static const char s_time0[] = "00:00 / 00:00";
static const char b_pause[] = "Pause";
static const char b_resume[] = "Reprise";
static const char t_now[] = "korin.media.now";
static const char t_control[] = "korin.media.control";
static const char s_me[] = "music";

static const unsigned *jetons;
static long w_dirlbl, w_dir, w_list, w_ltitle, w_lartist, w_lalbum, w_linfo, w_pos, w_time;
static long w_play, w_pause, w_stop, w_next, w_vol;
static long player, cur, paused, dragging, volume = 80, last_sec = -1, last_tick;
static long ev_type;				/* type du dernier evenement (la glissiere de position) */
static long info[AKI_SIZE / 8];
static char dir[MUS_PATH], path[MUS_PATH * 2], tmp[MUS_PATH * 2], name[MUS_PATH];
static char tbuf[64], tagbuf[256], now_title[256], tbuf_pub[BUS_VALUE_MAX + 8];

static void on_widget (long w, long code);

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

/* texte -> apres le mot et les espaces */
static char *skip_word (char *s)
{
	while (*s && *s != ' ')
		s++;
	while (*s == ' ')
		s++;
	return s;
}

/* 1 si le texte commence par ce mot (puis fin ou espace) */
static int starts_with (const char *s, const char *w)
{
	while (*w)
		if (*s++ != *w++)
			return 0;
	return *s == ' ' || *s == 0;
}

/* d = tampon, duree (ms, < 0 : inconnue) -> fin (« mm:ss », NUL) */
static char *fmt_time (char *d, long ms)
{
	if (ms < 0)
		return stpcpy (d, "--:--");
	unsigned long s = (unsigned long) ms / 1000;
	return d + sprintf (d, "%02lu:%c%c", s / 60, (char) ('0' + s % 60 / 10), (char) ('0' + s % 10));
}

/* ---- le bus ---------------------------------------------------------------------------------------- */

/* korin.media.now <- « <etat> <position ms> <duree ms> <instant ms> music <titre> » (etat : playing,
 * paused, stopped) */
static void media_pub (void)
{
	const char *st = !player ? "stopped" : paused ? "paused" : "playing";
	long pos = 0, dur = 0;
	if (player)
	{
		pos = ak_play_pos (player);
		if (pos < 0)
			pos = 0;
		dur = info[AKI_DURATION / 8];
		if (dur < 0)
			dur = 0;
	}
	char *e = tbuf_pub + sprintf (tbuf_pub, "%s %lu %lu %lu %s ", st, (unsigned long) pos, (unsigned long) dur,
				      (unsigned long) now_ms (), s_me);
	char *end = tbuf_pub + BUS_VALUE_MAX - 1;	/* le titre, borne */
	for (const char *t = now_title; *t && e < end;)
		*e++ = *t++;
	*e = 0;
	bus_publish ((long) t_now, (long) tbuf_pub, -1, BUS_RETAIN);
}

/* ---- le lecteur ------------------------------------------------------------------------------------ */

static void set_title (const char *s) { ui_w_set_text (w_ltitle, (long) s); }

/* volume de la glissiere (0..100, courbe a l'oreille) -> lecteur (0..256) */
static void apply_volume (void)
{
	if (player)
		ak_play_volume (player, (long) ((unsigned long) ak_volume_curve (volume) >> 8));
}

static void stop_player (void)
{
	if (!player)
		return;
	long p = player;
	player = 0;
	ak_play_stop (p);
	dragging = 0;
	ui_w_set_value (w_pos, 0);
	ui_w_set_text (w_time, (long) s_time0);
	ui_w_set_text (w_pause, (long) TR (b_pause));
	media_pub ();
}

static void toggle_pause (void)
{
	if (!player)
		return;
	paused ^= 1;
	ak_play_pause (player, paused);
	ui_w_set_text (w_pause, (long) TR (paused ? b_resume : b_pause));
	media_pub ();
}

/* decodeur, AKT_*, etiquette : l'etiquette (ou rien) */
static void show_tag (long dec, long tag, long w)
{
	long n = ak_dec_tag (dec, tag, (long) tagbuf, 256);
	ui_w_set_text (w, (long) (n > 0 ? tagbuf : ""));
}

/* « MP3, 44100 Hz, 2 canaux, 128 kbit/s, 03:25 » d'apres info */
static void show_info (void)
{
	long f = info[AKI_FORMAT / 8];
	if ((unsigned long) f > AKF_FMS)
		f = 0;
	char *e = stpcpy (tmp, fmt_names[f]);
	e = stpcpy (e, ", ");
	e += sprintf (e, "%lu", (unsigned long) info[AKI_RATE / 8]);
	e = stpcpy (e, " Hz, ");
	e += sprintf (e, "%lu", (unsigned long) info[AKI_CHANNELS / 8]);
	e = tr_cat (e, " canaux, ");
	if (info[AKI_BITRATE / 8])
	{
		e += sprintf (e, "%lu", (unsigned long) info[AKI_BITRATE / 8] / 1000);
		e = stpcpy (e, " kbit/s, ");
	}
	fmt_time (e, info[AKI_DURATION / 8]);
	ui_w_set_text (w_linfo, (long) tmp);
}

/* position entendue -> glissiere et « mm:ss / mm:ss » (une fois par seconde) */
static void update_pos (void)
{
	if (dragging || !player)		/* pendant un glisser : rien */
		return;
	long pos = ak_play_pos (player);
	long sec = (long) ((unsigned long) pos / 1000);
	if (sec == last_sec)
		return;
	last_sec = sec;
	ui_w_set_value (w_pos, sec);
	char *e = fmt_time (tbuf, pos);
	e = stpcpy (e, " / ");
	fmt_time (e, info[AKI_DURATION / 8]);
	ui_w_set_text (w_time, (long) tbuf);
}

/* numero dans la liste : le joue (hors liste : arret) */
static void play_row (long row)
{
	stop_player ();
	if (row < 0 || row >= ui_list_count (w_list))
		return;
	const char *t = (const char *) ui_list_get (w_list, row);	/* nom du fichier (garde) */
	strcpy (name, t ? t : "");
	char *e = stpcpy (path, dir);
	if (e[-1] != '/')
		*e++ = '/';
	strcpy (e, name);
	ui_w_set_value (w_list, row);
	long p = ak_play ((long) path, 0);
	if (p < 0)
	{					/* erreur d'ak_play */
		const char *m = p == AKE_FORMAT ? s_e_format : p == AKE_CORRUPT ? s_e_corrupt : s_e_other;
		set_title (TR (m));
		ui_w_set_text (w_lartist, (long) name);
		return;
	}
	player = p;
	cur = row;
	paused = 0;
	last_sec = -1;
	apply_volume ();
	ui_w_set_text (w_pause, (long) TR (b_pause));
	long dec = ak_play_decoder (p);		/* format, etiquettes */
	ak_dec_info (dec, (long) info);
	const char *title = ak_dec_tag (dec, AKT_TITLE, (long) tagbuf, 256) > 0 ? tagbuf : name;
	int n = 0;				/* (pour le bus) */
	for (; title[n] && n < 255; n++)
		now_title[n] = title[n];
	now_title[n] = 0;
	set_title (title);			/* titre (sinon le nom du fichier) */
	show_tag (dec, AKT_ARTIST, w_lartist);
	show_tag (dec, AKT_ALBUM, w_lalbum);
	show_info ();
	long d = info[AKI_DURATION / 8];	/* glissiere : 0..duree (s) */
	if (d < 0)
		d = 0;
	ui_w_set_range (w_pos, 0, d / 1000, 0, 1);
	ui_w_set_value (w_pos, 0);
	update_pos ();
	media_pub ();
}

/* position de la glissiere -> ak_play_seek */
static void seek_slider (void)
{
	if (!player)
		return;
	ak_play_seek (player, ui_w_get_value (w_pos) * 1000);
	last_sec = -1;
	media_pub ();
}

/* toutes les MUS_TICK ms : fin du morceau, position */
static void maybe_tick (void)
{
	long t = now_ms ();
	if ((unsigned long) (t - last_tick) < MUS_TICK)
		return;
	last_tick = t;
	if (!player)
		return;
	long st = ak_play_state (player);
	if (st == AKP_ENDED)
		play_row (cur + 1);		/* le suivant (apres le dernier : arret) */
	else if (st == AKP_ERROR)
	{
		stop_player ();
		set_title (TR (s_e_play));
	}
	else
		update_pos ();
}

/* korin.media.control : « music <commande> » */
static void on_control (void)
{
	long n = bus_get ((long) t_control, (long) tbuf_pub, BUS_VALUE_MAX);
	if (n <= 0)
		return;
	if (n > BUS_VALUE_MAX)
		n = BUS_VALUE_MAX;
	tbuf_pub[n] = 0;
	if (!starts_with (tbuf_pub, s_me))
		return;
	char *c = skip_word (tbuf_pub);
	if (starts_with (c, "toggle"))
		goto toggle;
	if (starts_with (c, "pause"))
	{
		if (paused)			/* (deja en pause : rien) */
			return;
		goto toggle;
	}
	if (starts_with (c, "play"))
	{
		if (!player)
			goto start;
		if (!paused)
			return;
		goto toggle;
	}
	if (starts_with (c, "next"))
	{
		on_widget (w_next, UN_ACTIVATE);
		return;
	}
	if (starts_with (c, "stop"))
		stop_player ();
	return;
toggle:
	if (player)
	{
		toggle_pause ();
		return;
	}
start:						/* rien ne joue : comme Lecture */
	on_widget (w_play, UN_ACTIVATE);
}

/* ---- les widgets ----------------------------------------------------------------------------------- */

static void build (void)
{
	w_dirlbl = ui_label (MUS_M, 14, MUS_W - 2 * MUS_M - 140 - FRAME_CAP_W, 22, (long) "", ULB_DIM);	/* dossier */
	/* (a gauche des boutons de la fenetre) */
	w_dir = ui_pushbutton (MUS_W - MUS_M - 120 - FRAME_CAP_W, 10, 120, 0, (long) "Dossier\xE2\x80\xA6", 0);
	w_list = ui_list (MUS_M, MUS_LIST_Y, MUS_W - 2 * MUS_M, MUS_LIST_H, 0);	/* les fichiers */
	/* titre, artiste, album, format */
	w_ltitle = ui_label (MUS_M, MUS_LIST_Y + MUS_LIST_H + 10, MUS_W - 2 * MUS_M, 22, (long) s_hint, ULB_BOLD);
	w_lartist = ui_label (MUS_M, MUS_LIST_Y + MUS_LIST_H + 34, MUS_W - 2 * MUS_M, 20, (long) "", 0);
	w_lalbum = ui_label (MUS_M, MUS_LIST_Y + MUS_LIST_H + 56, MUS_W - 2 * MUS_M, 20, (long) "", 0);
	w_linfo = ui_label (MUS_M, MUS_LIST_Y + MUS_LIST_H + 78, MUS_W - 2 * MUS_M, 20, (long) "", ULB_DIM);
	w_pos = ui_slider (MUS_M, MUS_POS_Y, MUS_W - 2 * MUS_M - 130, 28, 0, 0, 0);	/* position (secondes) */
	w_time = ui_label (MUS_W - MUS_M - 120, MUS_POS_Y, 120, 28, (long) s_time0, ULB_RIGHT);
	w_play = ui_pushbutton (MUS_M, MUS_BTN_Y, MUS_BTN_W, 0, (long) "Lecture", UB_DEFAULT);	/* boutons */
	w_pause = ui_pushbutton (MUS_M + (MUS_BTN_W + 8), MUS_BTN_Y, MUS_BTN_W, 0, (long) b_pause, 0);
	w_stop = ui_pushbutton (MUS_M + 2 * (MUS_BTN_W + 8), MUS_BTN_Y, MUS_BTN_W, 0, (long) "Arr\xC3\xAAt", 0);
	w_next = ui_pushbutton (MUS_M + 3 * (MUS_BTN_W + 8), MUS_BTN_Y, MUS_BTN_W, 0, (long) "Suivant", 0);
	w_vol = ui_slider (MUS_W - MUS_M - 130, MUS_BTN_Y, 130, 28, 0, 100, volume);	/* volume (0..100) */
	ui_w_tip (w_vol, (long) "Volume");
}

static void paint (void)
{
	ui_fill (0, 0, MUS_W, MUS_H, jetons[TJ_FACE / 4]);
	ui_w_draw (0);
	ui_update_all ();
}

/* ---- le dossier ------------------------------------------------------------------------------------ */

/* remplit la liste avec les fichiers audio de « dir » -> nombre */
static long load_dir (void)
{
	ui_list_clear (w_list);
	strcpy (tr_cat (tmp, "Dossier : "), dir);	/* « Dossier : ... » */
	ui_w_set_text (w_dirlbl, (long) tmp);
	DIR *d = opendir (dir);
	if (d)
	{
		struct dirent *e;
		while ((e = readdir (d)))
		{
			if (e->d_type == DT_DIR)
				continue;
			for (int i = 0; exts[i]; i++)	/* extension connue ? (casse ASCII) */
			{
				size_t ls = strlen (e->d_name), lf = strlen (exts[i]);
				if (ls >= lf && !strcasecmp (e->d_name + ls - lf, exts[i]))
				{
					ui_list_add (w_list, (long) e->d_name, 0);
					break;
				}
			}
		}
		closedir (d);
		ui_list_sort (w_list, 0, 0);	/* par nom */
	}
	long n = ui_list_count (w_list);
	set_title (TR (n ? s_hint : s_nofiles));
	if (n)
		ui_w_set_value (w_list, 0);
	return n;
}

/* widget, code (rendus par ui_w_event) */
static void on_widget (long w, long code)
{
	if (w == w_list)			/* liste : double-clic, Entree */
	{
		if (code == UN_ACTIVATE)
			play_row (ui_w_get_value (w));
		return;
	}
	if (w == w_pos)				/* position : recherche */
	{
		if (code != UN_CHANGE)
			return;
		if (ev_type == WEV_MOUSE)
			dragging = 1;		/* glisser : au relache (boucle principale) */
		else
			seek_slider ();		/* clavier, molette : tout de suite */
		return;
	}
	if (w == w_vol)				/* volume */
	{
		if (code != UN_CHANGE)
			return;
		volume = ui_w_get_value (w);
		apply_volume ();
		return;
	}
	if (code != UN_ACTIVATE)		/* les boutons */
		return;
	if (w == w_play)
	{
		if (player && paused)		/* en pause : reprise */
			toggle_pause ();
		else
		{				/* sinon l'element choisi (ou le premier) */
			long r = ui_w_get_value (w_list);
			play_row (r < 0 ? 0 : r);
		}
	}
	else if (w == w_pause)
		toggle_pause ();
	else if (w == w_stop)
		stop_player ();
	else if (w == w_next)
	{					/* suivant (apres le dernier : le premier) */
		long n = ui_list_count (w_list);
		if (!n)
			return;
		long r = player ? cur : ui_w_get_value (w_list);
		r++;
		play_row (r >= n ? 0 : r);
	}
	else if (w == w_dir)
	{					/* choix d'un dossier */
		if (ui_folder_choose ((long) dir, (long) tmp, MUS_PATH))
		{
			stop_player ();
			strcpy (dir, tmp);
			load_dir ();
		}
		paint ();
	}
}

static void quit (void)
{
	stop_player ();
	/* korin.media.now retire, s'il est a nous (etat, position, duree, instant : l'app) */
	if (bus_get ((long) t_now, (long) tmp, BUS_VALUE_MAX) > 0)
	{
		char *s = skip_word (skip_word (skip_word (skip_word (tmp))));
		if (starts_with (s, s_me))
			bus_publish ((long) t_now, 0, 0, BUS_DELETE);
	}
	exit (0);
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui) || !audiokit_kit_open () || !buskit_kit_open ())
	{
		fputs ("music : UIKit, AudioKit ou BusKit absent\n", stderr);
		return 1;
	}
	/* argument : un dossier (les mots de la ligne, comme la ligne d'arguments de l'asm) */
	static char arg[MUS_PATH];
	for (int i = 1; i < argc; i++)
	{
		if (i > 1)
			strcat (arg, " ");
		strncat (arg, argv[i], MUS_PATH - 2 - strlen (arg));
	}
	if (ui_window (MUS_W, MUS_H, (long) TR (s_title)) < 0)
		return 0;
	jetons = (const unsigned *) ui_tokens ();
	build ();
	bus_subscribe ((long) t_control, 0);	/* (sans busd : rien) */
	strcpy (dir, arg[0] ? arg : p_music);	/* dossier : l'argument, ou celui par defaut */
	if (!load_dir () && !arg[0])
	{
		strcpy (dir, p_test);		/* vide : les fichiers d'essai */
		load_dir ();
	}
	paint ();
	for (;;)
	{
		maybe_tick ();
		long t = ui_event (100);
		if (!t)
		{
			ui_w_idle ();		/* (infobulles) */
			continue;
		}
		if (t == WEV_CLOSE)
			quit ();
		if (t == WEV_BUS)
		{
			on_control ();
			continue;
		}
		ev_type = t;
		long code = 0, taken = 0;
		long w = uikit_w_event (&code, &taken);
		if (w && code)
			on_widget (w, code);
		/* relache apres un glisser de la position */
		if (t == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 2 && dragging)
		{
			dragging = 0;
			seek_slider ();
		}
	}
}
