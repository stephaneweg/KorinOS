/*
 * applets/mouse.cpp -- le volet Souris du panneau de configuration (demande de l'utilisateur, 13/11) :
 *   * la vitesse des souris (USB, PS/2, pointeur du clavier) et celle du touchpad (i2chid), en %, par deux curseurs
 *     (ui_slider) : appliquées aussitôt ;
 *   * une petite accélération des souris (jusqu'à x2 pour un geste vif ; le touchpad a toujours la sienne).
 * Les réglages vont à hidmanager (service « hid », HID_OP_POINTER avec les valeurs), qui les applique à tous les
 * pilotes à chaud, et dans le fichier du compte, HOME:/etc/pointer.ini (mouse, touchpad, accel ; E72 : sans
 * élévation), que l'écran de connexion applique à l'ouverture de sa session. /etc/pointer.ini reste celui de la
 * machine (l'écran de connexion, un compte qui n'a rien réglé). Défauts : souris 100 %, sans
 * accélération (comme avant), touchpad 200 % (docs/etude/TOUCHPAD-PC.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <armpi/userconf.h>
#include "../control.h"

enum
{
	HID_OP_POINTER = 5,			/* src/svc/hidmanager/hidmanager.c */
	MS_ACCEL = 1,				/* valeur de la case */
	MS_MIN = 25,
	MS_MAX = 400,
};


static long ms_mouse = 100, ms_touchpad = 200, ms_accel;
static long ms_sl_mouse, ms_sl_touchpad;	/* les curseurs */
static const char ms_s_done[] = "Enregistr\xC3\xA9.";
static const char ms_s_err[] = "R\xC3\xA9glages de la souris non enregistr\xC3\xA9s";

static void ms_line (char *key, char *val, void *)
{
	long v = atol (val);
	if (strcmp (key, "mouse") == 0)
		ms_mouse = v;
	else if (strcmp (key, "touchpad") == 0)
		ms_touchpad = v;
	else if (strcmp (key, "accel") == 0)
		ms_accel = v != 0;
}

static long clamp (long v)
{
	return v < MS_MIN ? MS_MIN : v > MS_MAX ? MS_MAX : v;
}

/* les vitesses à hidmanager (tout de suite), puis le fichier du compte (HOME:/etc/pointer.ini : E72, sans
 * élévation ; l'écran de connexion l'applique à l'ouverture de la session ; /etc/pointer.ini reste celui de la
 * machine) -> 0, < 0 : fichier non écrit */
static long ms_save (void)
{
	long ch = svc_open ("hid", 1000);
	if (ch >= 0)
	{
		long msg[64] = { 0 };
		msg[0] = HID_OP_POINTER;
		msg[1] = 1;			/* les valeurs suivent */
		msg[2] = ms_mouse;
		msg[3] = ms_touchpad;
		msg[4] = ms_accel;
		svc_call (ch, msg);
	}
	char v[16], cp[160];
	const char *f = korin_conf_path ("pointer.ini", cp, sizeof cp, 1);
	if (access (f, F_OK) != 0
	    && file_write (f, "# Vitesse du pointeur (R\xC3\xA9glages, Souris) : hidmanager, en %" CRLF) < 0)
		return -1;
	snprintf (v, sizeof v, "%ld", ms_mouse);
	long r = ini_set (f, "mouse", v);
	snprintf (v, sizeof v, "%ld", ms_touchpad);
	r |= ini_set (f, "touchpad", v);
	r |= ini_set (f, "accel", ms_accel ? "1" : "0");
	return r < 0 ? -1 : 0;
}

/* (ui_w_on) un curseur a bougé : la vitesse, tout de suite */
static void ms_slider_change (long w, long code)
{
	if (code != UN_CHANGE)
		return;
	if (w == ms_sl_mouse)
		ms_mouse = ui_w_get_value (w);
	else if (w == ms_sl_touchpad)
		ms_touchpad = ui_w_get_value (w);
	ms_save ();
	ap.msg = 0;
	ap.redraw = 1;				/* (la valeur à droite) */
}

static void ms_close (void)
{
	if (ms_sl_mouse)
		ui_w_destroy (ms_sl_mouse);
	if (ms_sl_touchpad)
		ui_w_destroy (ms_sl_touchpad);
	ms_sl_mouse = ms_sl_touchpad = 0;
}

static void ms_open (void)
{
	ms_mouse = 100;
	ms_touchpad = 200;
	ms_accel = 0;
	char cp[160];
	ini_read (korin_conf_path ("pointer.ini", cp, sizeof cp, 0), ms_line);	/* (celui du compte, sinon /etc) */
	ms_mouse = clamp (ms_mouse);
	ms_touchpad = clamp (ms_touchpad);
	ap.colw = AP_COLS * COL_W;		/* (une seule colonne : des curseurs plus longs) */
	applet_row ("Vitesse de la souris", R_TITLE);
	ms_sl_mouse = ui_slider (0, 0, 10, ROW_H - 2, MS_MIN, MS_MAX, ms_mouse);
	ui_w_on (ms_sl_mouse, (long) ms_slider_change, 0);
	applet_row (" %", R_SLIDER, 0, 0, ms_sl_mouse);
	applet_row ("Acc\xC3\xA9l\xC3\xA9ration de la souris", R_CHECK, 1, (int) ms_accel, MS_ACCEL);
	applet_row ("Vitesse du touchpad", R_TITLE);
	ms_sl_touchpad = ui_slider (0, 0, 10, ROW_H - 2, MS_MIN, MS_MAX, ms_touchpad);
	ui_w_on (ms_sl_touchpad, (long) ms_slider_change, 0);
	applet_row (" %", R_SLIDER, 0, 0, ms_sl_touchpad);
	applet_row ("Les vitesses changent aussit\xC3\xB4t.", R_NOTE);
	applet_row ("L'acc\xC3\xA9l\xC3\xA9ration : en appliquant.", R_NOTE);
	ap.widgets = 2;				/* (les événements aux curseurs d'abord ; le bas reste au cadre) */
	ap.close = ms_close;
}

static void ms_apply (void)
{
	for (long i = 0; i < ap.nrows; i++)
		if (ap.rows[i].kind == R_CHECK && ap.rows[i].value == MS_ACCEL)
			ms_accel = ap.rows[i].checked;
	if (ms_sl_mouse)
		ms_mouse = ui_w_get_value (ms_sl_mouse);
	if (ms_sl_touchpad)
		ms_touchpad = ui_w_get_value (ms_sl_touchpad);
	ap.msg = ms_save () < 0 ? ms_s_err : ms_s_done;
}

const Applet mouse_applet = { "mouse", "Souris", "Souris et touchpad", "control", ms_open, ms_apply };
