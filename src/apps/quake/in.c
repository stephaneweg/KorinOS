/*
 * in.c -- clavier, souris, manette. Les touches et la souris arrivent sur le thread de la fenetre : un anneau, vide
 * sur le thread de la machine (Sys_SendKeyEvents). En jeu, une touche vaut sa place (Z sur un AZERTY est le W du
 * clavier americain, comme le veut WASD) ; dans la console et les menus, son caractere. La souris devient relative
 * quand on joue vraiment : ni menu, ni console, ni demo. IN_Move suit celui de vid_x.c d'id (GPL).
 */
#include "quakedef.h"
#include <emukit.h>
#include "armpi.h"

#define QN 256
#define QM (QN - 1)
#define MAXHID 256

typedef struct { short code, ch; } kev;

static kev q[QN];
static volatile unsigned qw, qr;
static volatile int mdx, mdy;
static int oldbuttons, pointer_on, padold;
static byte held[MAXHID];		/* la touche Quake envoyee a l'appui */
static int stickx, sticky;

static cvar_t m_filter = { "m_filter", "0" };

/* code HID -> touche Quake, par la place (clavier americain) */
static const byte hidmap[MAXHID] =
{
	[0x04] = 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
	[0x1E] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
	[0x28] = K_ENTER, K_ESCAPE, K_BACKSPACE, K_TAB, K_SPACE, '-', '=', '[', ']', '\\',
	[0x33] = ';', '\'', '`', ',', '.', '/',
	[0x3A] = K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12,
	[0x48] = K_PAUSE, K_INS, K_HOME, K_PGUP, K_DEL, K_END, K_PGDN, K_RIGHTARROW, K_LEFTARROW, K_DOWNARROW, K_UPARROW,
	[0xE0] = K_CTRL, K_SHIFT, K_ALT, 0, K_CTRL, K_SHIFT, K_ALT
};

static void put (int code, int ch)
{
	unsigned w = qw;
	if (w - qr >= QN) return;
	q[w & QM].code = code;
	q[w & QM].ch = ch;
	__atomic_store_n (&qw, w + 1, __ATOMIC_RELEASE);
}

#define EV_DOWN 0x4000		/* dans ch : appui */
#define EV_DIRECT 0x2000	/* dans ch : touche Quake, pas un code HID */

void armpi_key (void *ctx, long code, long down, long ch, long mods)
{
	if (code == EMG_KEY_WHEEL)
	{
		put (down > 0 ? K_MWHEELUP : K_MWHEELDOWN, EV_DIRECT | EV_DOWN);
		put (down > 0 ? K_MWHEELUP : K_MWHEELDOWN, EV_DIRECT);
		return;
	}
	if (code == EMG_KEY_NONE) { put (-1, 0); return; }
	if (code < MAXHID) put (code, (ch > 31 && ch < 127 ? ch : 0) | (down ? EV_DOWN : 0));
}

void armpi_mouse (void *ctx, long kind, long x, long y, long buttons)
{
	int i, diff;
	if (kind == 3)
	{
		__atomic_add_fetch (&mdx, x, __ATOMIC_RELAXED);
		__atomic_add_fetch (&mdy, y, __ATOMIC_RELAXED);
	}
	diff = (buttons ^ oldbuttons) & 7;
	oldbuttons = buttons;
	if (!pointer_on) return;		/* (le clic dans la fenetre n'est pas un tir) */
	for (i = 0; i < 3; i++) if (diff >> i & 1)
		put (K_MOUSE1 + i, EV_DIRECT | (buttons >> i & 1 ? EV_DOWN : 0));
}

static void all_up (void)
{
	int i;
	for (i = 0; i < MAXHID; i++) if (held[i]) { Key_Event (held[i], false); held[i] = 0; }
}

void Sys_SendKeyEvents (void)
{
	int want;
	while (qr != __atomic_load_n (&qw, __ATOMIC_ACQUIRE))
	{
		kev e = q[qr & QM];
		qr = qr + 1;
		if (e.code < 0) { all_up (); continue; }
		if (e.ch & EV_DIRECT) { Key_Event (e.code, (e.ch & EV_DOWN) != 0); continue; }
		if (e.ch & EV_DOWN)
		{
			int c = e.ch & 0xFF, k = hidmap[e.code];
			if (key_dest != key_game && c && k < 128 && k > 32) k = c;
			if (!k) continue;
			if (k >= 'A' && k <= 'Z') k += 'a' - 'A';
			held[e.code] = k;
			Key_Event (k, true);
		}
		else if (held[e.code])
		{
			Key_Event (held[e.code], false);
			held[e.code] = 0;
		}
	}
	want = key_dest == key_game && cls.state == ca_connected && !cls.demoplayback;
	if (want != pointer_on) { pointer_on = want; emu_pointer (want); }
}

/* la manette : en jeu, les touches du jeu ; ailleurs, celles des menus */
void armpi_pad (long b)
{
	static const struct { long bit; int game, menu; } map[] =
	{
		{ PAD_UP, 'w', K_UPARROW }, { PAD_DOWN, 's', K_DOWNARROW }, { PAD_LEFT, 'a', K_LEFTARROW }, { PAD_RIGHT, 'd', K_RIGHTARROW },
		{ PAD_A, K_SPACE, K_ENTER }, { PAD_B, K_MOUSE1, K_ESCAPE }, { PAD_X, 'q', 'q' }, { PAD_Y, K_MWHEELDOWN, K_MWHEELDOWN },
		{ PAD_L, 'c', 'c' }, { PAD_R, K_SHIFT, K_SHIFT }, { PAD_START, K_ESCAPE, K_ESCAPE }, { PAD_SELECT, K_TAB, K_TAB }
	};
	int i, diff = (int) b ^ padold;
	short ax[4];
	padold = b;
	if (diff)
		for (i = 0; i < (int) (sizeof map / sizeof map[0]); i++)
			if (diff & map[i].bit)
				Key_Event (key_dest == key_game ? map[i].game : map[i].menu, (b & map[i].bit) != 0);
	emu_pad_axes (1, (long) ax);
	stickx = ax[2];
	sticky = ax[3];
}

void IN_Init (void) { Cvar_RegisterVariable (&m_filter); }
void IN_Shutdown (void) {}
void IN_Commands (void) {}

void IN_Move (usercmd_t *cmd)
{
	float mx = __atomic_exchange_n (&mdx, 0, __ATOMIC_RELAXED);
	float my = __atomic_exchange_n (&mdy, 0, __ATOMIC_RELAXED);

	mx *= sensitivity.value;
	my *= sensitivity.value;
	if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
		cmd->sidemove += m_side.value * mx;
	else
		cl.viewangles[YAW] -= m_yaw.value * mx;
	if (in_mlook.state & 1) V_StopPitchDrift ();
	if ((in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * my;
		if (cl.viewangles[PITCH] > 80) cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70) cl.viewangles[PITCH] = -70;
	}
	else if ((in_strafe.state & 1) && noclip_anglehack)
		cmd->upmove -= m_forward.value * my;
	else
		cmd->forwardmove -= m_forward.value * my;
	/* le stick droit tourne la vue */
	cl.viewangles[YAW] -= stickx * host_frametime * 0.18;
	cl.viewangles[PITCH] -= sticky * host_frametime * 0.12;
	if (cl.viewangles[PITCH] > 80) cl.viewangles[PITCH] = 80;
	if (cl.viewangles[PITCH] < -70) cl.viewangles[PITCH] = -70;
}
