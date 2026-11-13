/*
 * applets/mouse.cpp -- le volet Souris du panneau de configuration (demande de l'utilisateur, 13/11) :
 *   * la vitesse des souris (USB, PS/2, pointeur du clavier) et celle du touchpad (i2chid), en %, par deux curseurs
 *     (ui_slider) : appliquées aussitôt ;
 *   * une petite accélération des souris (jusqu'à x2 pour un geste vif ; le touchpad a toujours la sienne).
 * Les réglages vont dans SD0:/etc/pointer.ini (mouse, touchpad, accel), que hidmanager applique à tous les pilotes
 * et relit à la demande (service « hid », HID_OP_POINTER) : à chaud, sans redémarrer. Défauts : souris 100 %, sans
 * accélération (comme avant), touchpad 200 % (docs/etude/TOUCHPAD-PC.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../control.h"

enum
{
	HID_OP_POINTER = 5,			/* src/svc/hidmanager/hidmanager.c */
	MS_ACCEL = 1,				/* valeur de la case */
	MS_MIN = 25,
	MS_MAX = 400,
};

#define MS_FILE		"SD0:/etc/pointer.ini"

static long ms_mouse = 100, ms_touchpad = 200, ms_accel;
static long ms_sl_mouse, ms_sl_touchpad;	/* les curseurs */
static const char ms_s_done[] = "Enregistr\xC3\xA9.";
static const char ms_s_err[] = "SD0:/etc/pointer.ini non modifi\xC3\xA9";

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

/* le fichier, puis hidmanager le relit -> 0, < 0 : fichier non écrit */
static long ms_save (void)
{
	char v[16];
	if (access (MS_FILE, F_OK) != 0
	    && file_write (MS_FILE, "# Vitesse du pointeur (R\xC3\xA9glages, Souris) : hidmanager, en %" CRLF) < 0)
		return -1;
	snprintf (v, sizeof v, "%ld", ms_mouse);
	long r = ini_set (MS_FILE, "mouse", v);
	snprintf (v, sizeof v, "%ld", ms_touchpad);
	r |= ini_set (MS_FILE, "touchpad", v);
	r |= ini_set (MS_FILE, "accel", ms_accel ? "1" : "0");
	long ch = svc_open ("hid", 1000);
	if (ch >= 0)
	{
		long msg[64] = { 0 };
		msg[0] = HID_OP_POINTER;
		svc_call (ch, msg);
	}
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
	ini_read (MS_FILE, ms_line);
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
