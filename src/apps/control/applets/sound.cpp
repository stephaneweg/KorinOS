/*
 * applets/sound.cpp -- le volet Son du panneau de configuration (docs/SON.md 4.4), par AudioKit (service sound) :
 *   * la sortie principale préférée (ou « Automatique » : USB, HDMI, prise jack), retenue par sound dans
 *     /etc/sound.txt (ak_main) ;
 *   * le volume de la sortie principale, par un curseur (ui_slider) : changé aussitôt, comme le volet du son de la
 *     barre de menus (retenu de même, ak_out_volume) ; il suit aussi les changements faits ailleurs ;
 *   * le micro I2S (INMP441) au démarrage : la ligne /sys/svc/i2smic de /etc/services.txt, mise ou retirée
 *     du commentaire ;
 *   * un son d'essai (un la 440 Hz en triangle, 0,6 s) en appliquant.
 *
 * Port en C++ de applets/sound.inc (Korin). Le soundconf d'Onyx (user/Apps/soundconf) avait les mêmes idées (sortie
 * automatique ou choisie, volume, entrée I2S, son d'essai) sur des widgets et kapi_sound_* ; c'est l'asm de Korin,
 * des lignes du cadre sur le service sound, qui fait foi ici, et que ce volet reprend tel quel, sauf le volume : un
 * curseur à la place des quatre choix 25, 50, 75 et 100 % (demande de l'utilisateur, 13/11).
 */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <audiokit_abi.h>
#include "../control.h"

/* src/inc/sound.inc : le tampon d'état de sound (ak_state), champs de 32 bits */
enum
{
	SS_GEN = 0,				/* génération (64 bits) : +1 à chaque changement */
	SS_MAIN = 8,				/* sortie principale (64 bits) */
	SS_OUTS = 64,				/* SS_OUT_MAX entrées de 64 octets */
	SS_OUT_MAX = 8,
	SO_ID = 0,
	SO_VOLUME = 12,
	SO_MUTE = 16,
	SO_FLAGS = 20,				/* bit 0 principale, bit 1 préférée */
	SO_NAME = 32,				/* 32 octets (UTF-8) */
	SO_SIZE = 64,
	SN_I2S = 1,				/* valeurs des cases */
	SN_TEST = 2,
};

static long sn_slider;				/* le curseur du volume (0 : pas de sortie principale) */
static long sn_gen = -1;			/* génération de l'état montrée */
static const char sn_s_done[] = "Enregistr\xC3\xA9.";
static const char sn_s_err[] = "/etc/services.txt non modifi\xC3\xA9";
#define SN_SERVICES	"/etc/services.txt"
#define SN_I2S_PATH	"/sys/svc/i2smic"

static char sn_names[SS_OUT_MAX][32];		/* noms des sorties (les lignes pointent dessus) */

static inline uint32_t rd32 (const uint8_t *p, long off) { return *(const uint32_t *) (p + off); }
static inline const uint8_t *sn_out (const uint8_t *st, long i) { return st + SS_OUTS + i * SO_SIZE; }

/* ---- /etc/services.txt ---- */

/* le fichier dans ap_file (terminé par 0) -> octets (< 0 : erreur) */
static long sn_read_services (void)
{
	int fd = open (SN_SERVICES, O_RDONLY);
	if (fd < 0)
		return fd;
	long n = read (fd, ap_file, AP_FILE_MAX - 1);
	close (fd);
	ap_file[n > 0 ? n : 0] = 0;
	return n;
}

/* -> 1 si services.txt lance i2smic (une ligne qui commence, après ses blancs, par le chemin : comme l'asm, sans
 * regarder ce qui le suit) */
static int sn_i2s_on (void)
{
	if (sn_read_services () <= 0)
		return 0;
	const char *line = ap_file;
	for (;;)
	{
		const char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (!strncmp (p, SN_I2S_PATH, sizeof SN_I2S_PATH - 1))
			return 1;
		while (*line && *line != '\n')
			line++;
		if (!*line)
			return 0;
		line++;
	}
}

/* on = 1 : la ligne i2smic active (sinon en commentaire, « # ») ; ajoutée à la fin si elle manque et qu'on la veut
 * -> 0, < 0 si erreur */
static long sn_i2s_set (int on)
{
	long n = sn_read_services ();
	if (n < 0)
		return n;
	const char *in = ap_file;
	char *out = ap_out;
	int found = 0;
	while (*in)
	{
		const char *p = in;		/* « [#] /sys/svc/i2smic » ? (blancs et « # » sautés) */
		while (*p == ' ' || *p == '#' || *p == '\t')
			p++;
		if (!strncmp (p, SN_I2S_PATH, sizeof SN_I2S_PATH - 1))
		{
			found = 1;		/* la ligne : refaite */
			in = p;
			if (!on)
			{
				*out++ = '#';
				*out++ = ' ';
			}
		}
		while (*in)			/* le reste de la ligne */
		{
			char c = *in++;
			*out++ = c;
			if (c == '\n')
				break;
		}
	}
	if (!found && on)			/* absente et voulue : ajoutée */
	{
		if (out != ap_out && out[-1] != '\n')	/* (fichier sans fin de ligne au bout) */
			*out++ = '\n';
		memcpy (out, SN_I2S_PATH CRLF, sizeof SN_I2S_PATH CRLF - 1);
		out += sizeof SN_I2S_PATH CRLF - 1;
	}
	*out = 0;
	return file_write (SN_SERVICES, ap_out);
}

/* valeur d'une case -> 1 si cochée */
static int sn_check (long value)
{
	for (long i = 0; i < ap.nrows; i++)
		if (ap.rows[i].kind == R_CHECK && ap.rows[i].value == value)
			return ap.rows[i].checked;
	return 0;
}

/* ---- le son d'essai : le la 440 Hz (triangle), 0,6 s, mono 44,1 kHz ---- */
static void sn_beep (void)
{
	static int16_t buf[1024];
	long h = ak_open (44100, 1, 16, 0);
	if (h < 0)
		return;
	uint32_t phase = 0;
	const uint32_t step = 42852281;		/* 440 x 2^32 / 44100 */
	for (int blk = 0; blk < 26; blk++)	/* 26 morceaux de 1024 trames */
	{
		for (int i = 0; i < 1024; i++)
		{
			int32_t v = (int32_t) (phase >> 16);	/* triangle de -8192 à 8191 */
			if (v >= 32768)
				v = 65535 - v;
			buf[i] = (int16_t) ((v - 16384) >> 1);
			phase += step;
		}
		ak_write_all (h, (long) buf, sizeof buf);
	}
	ak_drain (h);
	ak_close (h);
}

/* ---- le volume : le curseur ---- */

/* -> l'entrée de la sortie principale dans l'état (0 : aucune) */
static const uint8_t *sn_main (const uint8_t *st)
{
	if (!st)
		return 0;
	uint64_t main_id = *(const uint64_t *) (st + SS_MAIN);
	for (long i = 0; i < SS_OUT_MAX; i++)
	{
		const uint8_t *o = sn_out (st, i);
		if (rd32 (o, SO_ID) && rd32 (o, SO_ID) == main_id)
			return o;
	}
	return 0;
}

/* (ui_w_on) le curseur a bougé : le volume de la principale, tout de suite */
static void sn_slider_change (long w, long code)
{
	if (code != UN_CHANGE)
		return;
	const uint8_t *st = (const uint8_t *) ak_state ();
	const uint8_t *o = sn_main (st);
	if (o)
	{
		ak_out_volume (rd32 (o, SO_ID), ui_w_get_value (w), rd32 (o, SO_MUTE));
		sn_gen = *(const long *) (st + SS_GEN);
	}
	ap.redraw = 1;				/* (la valeur à droite) */
}

/* (ap.tick) le volume changé ailleurs (barre de menus, soundctl) : le curseur le suit -> 1 : redessiner */
static int sn_tick (void)
{
	const uint8_t *st = (const uint8_t *) ak_state ();
	if (!st || !sn_slider || *(const long *) (st + SS_GEN) == sn_gen)
		return 0;
	sn_gen = *(const long *) (st + SS_GEN);
	const uint8_t *o = sn_main (st);
	if (!o || (long) rd32 (o, SO_VOLUME) == ui_w_get_value (sn_slider))
		return 0;
	ui_w_set_value (sn_slider, rd32 (o, SO_VOLUME));
	return 1;
}

/* (ap.close) le curseur s'en va */
static void sn_close (void)
{
	if (sn_slider)
		ui_w_destroy (sn_slider);
	sn_slider = 0;
}

/* ---- le volet ---- */

static void sn_open (void)
{
	ap.colw = AP_COLS * COL_W;		/* (une seule colonne : un curseur plus long) */
	const uint8_t *st = (const uint8_t *) ak_state ();
	applet_row ("Sortie principale pr\xC3\xA9" "f\xC3\xA9r\xC3\xA9" "e", R_TITLE);
	int automatic = 1;			/* « Automatique » cochée si aucune préférée */
	if (st)
		for (long i = 0; i < SS_OUT_MAX; i++)
			if (rd32 (sn_out (st, i), SO_FLAGS) & 2)
				automatic = 0;
	applet_row ("Automatique", R_CHOICE, 0, automatic, 0);
	if (st)
		for (long i = 0; i < SS_OUT_MAX; i++)
		{
			const uint8_t *o = sn_out (st, i);
			if (!rd32 (o, SO_ID))
				continue;
			memcpy (sn_names[i], o + SO_NAME, 31);	/* nom gardé (la ligne pointe dessus) */
			sn_names[i][31] = 0;
			applet_row (sn_names[i], R_CHOICE, 0, (rd32 (o, SO_FLAGS) >> 1) & 1, rd32 (o, SO_ID));
		}
	applet_row ("Volume principal", R_TITLE);
	const uint8_t *o = sn_main (st);	/* le curseur : le volume actuel de la principale */
	if (o)
	{
		sn_gen = *(const long *) (st + SS_GEN);
		sn_slider = ui_slider (0, 0, 10, ROW_H - 2, 0, 100, rd32 (o, SO_VOLUME));
		ui_w_on (sn_slider, (long) sn_slider_change, 0);
		applet_row (" %", R_SLIDER, 0, 0, sn_slider);
		ap.widgets = 2;			/* (les événements au curseur d'abord ; le bas reste au cadre) */
		ap.close = sn_close;
		ap.tick = sn_tick;
	}
	else
		applet_row ("Aucune sortie.", R_NOTE);
	applet_row ("Micro et essai", R_TITLE);
	applet_row ("Micro I2S (INMP441)", R_CHECK, 2, sn_i2s_on (), SN_I2S);
	applet_row ("Son d'essai en appliquant", R_CHECK, 3, 1, SN_TEST);
	applet_row ("Micro : au red\xC3\xA9marrage.", R_NOTE);
}

static void sn_apply (void)
{
	long pref = applet_choice (0);		/* préférée (0 : automatique) */
	if (pref >= 0)
		ak_main (pref);
	/* (le volume : déjà changé par le curseur) */
	ap.msg = sn_s_done;
	int want = sn_check (SN_I2S);
	if (want != sn_i2s_on ())		/* micro I2S : services.txt, élevé (E72), seulement s'il change */
	{
		unsigned me;
		if (ap_elevate (TR ("R\xC3\xA9glages veut changer les services du d\xC3\xA9marrage."), &me))
			ap.msg = TR ("Autorisation refus\xC3\xA9" "e.");
		else
		{
			if (sn_i2s_set (want) < 0)
				ap.msg = sn_s_err;
			ap_unelevate (me);
		}
	}
	if (sn_check (SN_TEST))
		sn_beep ();
}

const Applet sound_applet = { "sound", "Son", "Son", "sound", sn_open, sn_apply };
