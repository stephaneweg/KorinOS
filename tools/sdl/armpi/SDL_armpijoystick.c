/*
 * SDL_armpijoystick.c -- les manettes de SDL 3 sur ARMPI (docs/SDL.md, src/inc/pad.inc). HIDManager applique
 * le mapping de SD0:/etc/gamepad.ini et Elegant envoie WEV_PAD a la fenetre qui a le clavier : la boucle de la
 * video le donne ici. Boutons par leur place sur une manette de type Xbox, donc le mapping de SDL est fixe.
 * Une manette apparait a son premier evenement : Elegant n'annonce pas les branchements.
 */
#include "SDL_internal.h"

#include "../src/joystick/SDL_sysjoystick.h"
#include "../src/joystick/SDL_joystick_c.h"
#include "../src/joystick/SDL_gamepad_c.h"

#include "SDL_armpi.h"

#define NBUTTONS 17			/* PAD_NBUTTONS */
#define NAXES 4

enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_X, B_Y, B_L, B_R, B_L2, B_R2, B_SELECT, B_START, B_L3, B_R3, B_HOME };

struct pad
{
	bool seen, added;
	SDL_JoystickID id;
	unsigned buttons;
	short axes[NAXES];
	unsigned model;
	SDL_Joystick *open;
};

static struct pad pads[ARMPI_PADS];
static int npads;

void ARMPI_PadEvent (int pad, unsigned buttons, const short axes[4], unsigned model)
{
	if (pad < 0 || pad >= ARMPI_PADS)
		return;
	pads[pad].seen = true;
	pads[pad].buttons = buttons;
	SDL_memcpy (pads[pad].axes, axes, sizeof pads[pad].axes);
	pads[pad].model = model;
}

static struct pad *pad_at (int index)
{
	for (int i = 0; i < ARMPI_PADS; i++)
		if (pads[i].added && index-- == 0)
			return &pads[i];
	return NULL;
}

static bool ARMPI_JoystickInit (void)
{
	return true;
}

static int ARMPI_JoystickGetCount (void)
{
	return npads;
}

static void ARMPI_JoystickDetect (void)
{
	for (int i = 0; i < ARMPI_PADS; i++)
		if (pads[i].seen && !pads[i].added) {
			pads[i].added = true;
			pads[i].id = SDL_GetNextObjectID ();
			npads++;
			SDL_PrivateJoystickAdded (pads[i].id);
		}
}

static bool ARMPI_JoystickIsDevicePresent (Uint16 vendor_id, Uint16 product_id, Uint16 version, const char *name)
{
	return false;
}

static const char *ARMPI_JoystickGetDeviceName (int index)
{
	return "ARMPI gamepad";
}

static const char *ARMPI_JoystickGetDevicePath (int index)
{
	return NULL;
}

static int ARMPI_JoystickGetDeviceSteamVirtualGamepadSlot (int index)
{
	return -1;
}

static int ARMPI_JoystickGetDevicePlayerIndex (int index)
{
	return -1;
}

static void ARMPI_JoystickSetDevicePlayerIndex (int index, int player)
{
}

static SDL_GUID ARMPI_JoystickGetDeviceGUID (int index)
{
	struct pad *p = pad_at (index);
	unsigned m = p ? p->model : 0;

	return SDL_CreateJoystickGUID (SDL_HARDWARE_BUS_USB, (Uint16) (m >> 16), (Uint16) m, 0, NULL, "ARMPI gamepad", 'a', 0);
}

static SDL_JoystickID ARMPI_JoystickGetDeviceInstanceID (int index)
{
	struct pad *p = pad_at (index);

	return p ? p->id : 0;
}

static bool ARMPI_JoystickOpen (SDL_Joystick *joystick, int index)
{
	struct pad *p = pad_at (index);

	if (!p)
		return SDL_SetError ("ARMPI : pas de manette %d", index);
	joystick->hwdata = (struct joystick_hwdata *) p;
	joystick->nbuttons = NBUTTONS;
	joystick->naxes = NAXES;
	joystick->nhats = 0;
	p->open = joystick;
	return true;
}

static bool ARMPI_JoystickRumble (SDL_Joystick *joystick, Uint16 low, Uint16 high)
{
	return SDL_Unsupported ();
}

static bool ARMPI_JoystickRumbleTriggers (SDL_Joystick *joystick, Uint16 left, Uint16 right)
{
	return SDL_Unsupported ();
}

static bool ARMPI_JoystickSetLED (SDL_Joystick *joystick, Uint8 r, Uint8 g, Uint8 b)
{
	return SDL_Unsupported ();
}

static bool ARMPI_JoystickSendEffect (SDL_Joystick *joystick, const void *data, int size)
{
	return SDL_Unsupported ();
}

static bool ARMPI_JoystickSetSensorsEnabled (SDL_Joystick *joystick, bool enabled)
{
	return SDL_Unsupported ();
}

static void ARMPI_JoystickUpdate (SDL_Joystick *joystick)
{
	struct pad *p = (struct pad *) joystick->hwdata;
	Uint64 now = SDL_GetTicksNS ();

	for (int i = 0; i < NBUTTONS; i++)
		SDL_SendJoystickButton (now, joystick, (Uint8) i, (p->buttons >> i) & 1);
	for (int i = 0; i < NAXES; i++)
		SDL_SendJoystickAxis (now, joystick, (Uint8) i, (Sint16) (p->axes[i] * 32));	/* -1000..1000 */
}

static void ARMPI_JoystickClose (SDL_Joystick *joystick)
{
	struct pad *p = (struct pad *) joystick->hwdata;

	if (p)
		p->open = NULL;
	joystick->hwdata = NULL;
}

static void ARMPI_JoystickQuit (void)
{
	SDL_zeroa (pads);
	npads = 0;
}

static bool ARMPI_JoystickGetGamepadMapping (int index, SDL_GamepadMapping *out)
{
#define MAP_B(field, b) out->field.kind = EMappingKind_Button, out->field.target = b
#define MAP_A(field, a) out->field.kind = EMappingKind_Axis, out->field.target = a
	MAP_B (a, B_A);
	MAP_B (b, B_B);
	MAP_B (x, B_X);
	MAP_B (y, B_Y);
	MAP_B (back, B_SELECT);
	MAP_B (start, B_START);
	MAP_B (guide, B_HOME);
	MAP_B (leftstick, B_L3);
	MAP_B (rightstick, B_R3);
	MAP_B (leftshoulder, B_L);
	MAP_B (rightshoulder, B_R);
	MAP_B (dpup, B_UP);
	MAP_B (dpdown, B_DOWN);
	MAP_B (dpleft, B_LEFT);
	MAP_B (dpright, B_RIGHT);
	MAP_B (lefttrigger, B_L2);
	MAP_B (righttrigger, B_R2);
	MAP_A (leftx, 0);
	MAP_A (lefty, 1);
	MAP_A (rightx, 2);
	MAP_A (righty, 3);
	return true;
}

SDL_JoystickDriver SDL_PRIVATE_JoystickDriver = {
	ARMPI_JoystickInit,
	ARMPI_JoystickGetCount,
	ARMPI_JoystickDetect,
	ARMPI_JoystickIsDevicePresent,
	ARMPI_JoystickGetDeviceName,
	ARMPI_JoystickGetDevicePath,
	ARMPI_JoystickGetDeviceSteamVirtualGamepadSlot,
	ARMPI_JoystickGetDevicePlayerIndex,
	ARMPI_JoystickSetDevicePlayerIndex,
	ARMPI_JoystickGetDeviceGUID,
	ARMPI_JoystickGetDeviceInstanceID,
	ARMPI_JoystickOpen,
	ARMPI_JoystickRumble,
	ARMPI_JoystickRumbleTriggers,
	ARMPI_JoystickSetLED,
	ARMPI_JoystickSendEffect,
	ARMPI_JoystickSetSensorsEnabled,
	ARMPI_JoystickUpdate,
	ARMPI_JoystickClose,
	ARMPI_JoystickQuit,
	ARMPI_JoystickGetGamepadMapping
};
