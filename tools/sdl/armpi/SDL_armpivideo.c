/*
 * SDL_armpivideo.c -- la video de SDL 3 sur ARMPI (docs/SDL.md) : couche d'adaptation pour des ports tiers.
 *
 * Une fenetre SDL est une fenetre d'UIKit : son cadre et ses couleurs sont ceux du theme, comme pour toute
 * fenetre. Le contenu est une surface d'Elegant : pixels du processeur (ui_surface), ou image du GPU
 * (armgl_window, rendu sans recopie). Trois vues, comme les emulateurs : fenetre, agrandie, plein ecran (F9).
 *
 * Le GPU est facultatif : armgl et Mesa pesent 16 Mo, ils ne sont lies que si le programme le demande
 * (-Wl,-u,armgl_init), d'ou les references faibles. Sans GPU, ou sans armgl, le GL refuse proprement et
 * SDL_Renderer reste sur le processeur.
 */
#include "SDL_internal.h"

#include "../src/video/SDL_sysvideo.h"
#include "../src/video/SDL_pixels_c.h"
#include "../src/events/SDL_events_c.h"
#include "../src/events/SDL_keyboard_c.h"
#include "../src/events/SDL_mouse_c.h"
#include "../src/events/SDL_windowevents_c.h"

#include <uikit.h>
#include "armgl.h"
#include "SDL_armpi.h"

#pragma weak armgl_init
#pragma weak armgl_context
#pragma weak armgl_window
#pragma weak armgl_window_zone
#pragma weak armgl_make_current
#pragma weak armgl_swap
#pragma weak armgl_proc

#define KEY_F9		0x42		/* code HID : la bascule du plein ecran, comme les emulateurs */
#define ROLE_DARK	(WF_GLYPH_LIGHT | WF_BODY_DARK)		/* un jeu : contenu fonce sous les boutons du cadre */

typedef struct SDL_WindowData
{
	long win;			/* la fenetre montree */
	long framed;			/* celle du cadre, tenue pendant le plein ecran (0 : pas en plein ecran) */
	long surf;			/* surface des pixels du processeur */
	Uint32 *back, *front;		/* SDL dessine dans back ; Elegant ne voit que des images finies */
	long pitch;
	void *gl;			/* surface du GPU, de la taille de l'ecran : redimensionner ne change que la zone */
	int w, h;
} SDL_WindowData;

static struct ui_vars ui;
static void *gl_current;
static int gl_interval, gl_mode;
static SDL_Keycode key_sym[256];	/* le code de la touche a l'appui, pour le relachement */

static void screen_size (long scr[4])
{
	ui_screen ((long) scr);
}

static void use_window (SDL_WindowData *d)
{
	ui_window_use (d->win);
}

/* --- pixels du processeur ------------------------------------------------------------------------ */

static bool ARMPI_CreateWindowFramebuffer (SDL_VideoDevice *_this, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
	SDL_WindowData *d = window->internal;
	long p = 0, sw, sh;

	use_window (d);
	if (d->surf)
		ui_surface_free (d->surf);
	d->surf = ui_surface (0, 0, d->w, d->h, WS_SF_XRGB8888);
	if (!d->surf)
		return SDL_SetError ("ARMPI : pas de surface");
	d->front = (Uint32 *) uikit_call3 ((long (*) ()) ui_surface_pixels, d->surf, 0, 0, 0, &p, &sw);
	d->pitch = p;
	SDL_free (d->back);
	d->back = SDL_calloc (d->h, (size_t) p);
	if (!d->front || !d->back)
		return false;
	*pixels = d->back;
	*pitch = (int) p;
	*format = SDL_PIXELFORMAT_XRGB8888;
	return true;
}

static bool ARMPI_UpdateWindowFramebuffer (SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
	SDL_WindowData *d = window->internal;

	use_window (d);
	for (int i = 0; i < numrects; i++) {
		const SDL_Rect *r = &rects[i];
		long off = r->y * d->pitch + r->x * 4L;
		for (int y = 0; y < r->h; y++)
			SDL_memcpy ((Uint8 *) d->front + off + y * d->pitch, (Uint8 *) d->back + off + y * d->pitch, r->w * 4);
		ui_surface_damage (d->surf, r->x, r->y, r->w, r->h);
	}
	return true;
}

static void ARMPI_DestroyWindowFramebuffer (SDL_VideoDevice *_this, SDL_Window *window)
{
	SDL_WindowData *d = window->internal;

	if (d->surf) {
		use_window (d);
		ui_surface_free (d->surf);
		d->surf = 0;
	}
	SDL_free (d->back);
	d->back = NULL;
}

/* --- OpenGL ES, par armgl : pas d'EGL ------------------------------------------------------------- */

static bool ARMPI_GL_LoadLibrary (SDL_VideoDevice *_this, const char *path)
{
	const char *mode = SDL_getenv ("SDL_ARMPI_GL");

	if (!armgl_init)
		return SDL_SetError ("ARMPI : programme lie sans armgl");
	if (mode && !SDL_strcmp (mode, "off"))
		return SDL_SetError ("ARMPI : GL coupe par SDL_ARMPI_GL");
	gl_mode = armgl_init (mode && !SDL_strcmp (mode, "dry") ? ARMGL_F_DRY : 0);
	if (gl_mode == ARMGL_NONE)
		return SDL_SetError ("ARMPI : pas de GPU");
	return true;
}

static SDL_FunctionPointer ARMPI_GL_GetProcAddress (SDL_VideoDevice *_this, const char *proc)
{
	return (SDL_FunctionPointer) armgl_proc (proc);
}

static void ARMPI_GL_UnloadLibrary (SDL_VideoDevice *_this)
{
}

static void show_gl_zone (SDL_WindowData *d)
{
	if (d->gl)
		armgl_window_zone (d->gl, d->win, 0, 0, d->w, d->h);
}

static SDL_GLContext ARMPI_GL_CreateContext (SDL_VideoDevice *_this, SDL_Window *window)
{
	SDL_WindowData *d = window->internal;
	int major = _this->gl_config.major_version, minor = _this->gl_config.minor_version;
	void *ctx;

	if (_this->gl_config.profile_mask != SDL_GL_CONTEXT_PROFILE_ES) {
		SDL_SetError ("ARMPI : OpenGL ES seulement");
		return NULL;
	}
	if (major > 3 || (major == 3 && minor > 0)) {
		SDL_SetError ("ARMPI : OpenGL ES 3.0 au plus");
		return NULL;
	}
	if (!d->gl) {
		long scr[4];
		screen_size (scr);
		d->gl = armgl_window (d->win, 0, 0, (int) scr[0], (int) scr[1]);
		if (!d->gl) {
			SDL_SetError ("ARMPI : pas de memoire basse pour la surface du GPU");
			return NULL;
		}
		show_gl_zone (d);
	}
	/* (ES 2.0 demande : le contexte 3.0 le contient) */
	ctx = armgl_context (3, 0, _this->gl_config.share_with_current_context ? gl_current : NULL);
	if (!ctx) {
		SDL_SetError ("ARMPI : pas de contexte GL");
		return NULL;
	}
	if (!armgl_make_current (ctx, d->gl)) {
		SDL_SetError ("ARMPI : contexte GL refuse");
		return NULL;
	}
	gl_current = ctx;
	return (SDL_GLContext) ctx;
}

static bool ARMPI_GL_MakeCurrent (SDL_VideoDevice *_this, SDL_Window *window, SDL_GLContext context)
{
	SDL_WindowData *d = window ? window->internal : NULL;

	if (!armgl_make_current (context, d ? d->gl : NULL))
		return SDL_SetError ("ARMPI : contexte GL refuse");
	gl_current = context;
	return true;
}

static bool ARMPI_GL_SwapWindow (SDL_VideoDevice *_this, SDL_Window *window)
{
	SDL_WindowData *d = window->internal;

	if (armgl_swap (d->gl))
		return SDL_SetError ("ARMPI : image refusee");
	return true;
}

/* (le rythme vient d'Elegant : armgl_swap attend la composition en cours) */
static bool ARMPI_GL_SetSwapInterval (SDL_VideoDevice *_this, int interval)
{
	gl_interval = interval;
	return true;
}

static bool ARMPI_GL_GetSwapInterval (SDL_VideoDevice *_this, int *interval)
{
	*interval = gl_interval;
	return true;
}

/* armgl ne detruit pas encore un contexte : il reste jusqu'a la fin du programme */
static bool ARMPI_GL_DestroyContext (SDL_VideoDevice *_this, SDL_GLContext context)
{
	if (gl_current == context)
		gl_current = NULL;
	return true;
}

/* --- fenetres ------------------------------------------------------------------------------------- */

static bool ARMPI_CreateSDLWindow (SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
	SDL_WindowData *d = SDL_calloc (1, sizeof *d);
	long scr[4], role = ROLE_DARK;
	int x = window->x, y = window->y;

	if (!d)
		return false;
	screen_size (scr);
	if (window->flags & SDL_WINDOW_RESIZABLE)
		role |= WF_RESIZABLE;
	if (window->w > scr[0])
		window->w = (int) scr[0];
	if (window->h > scr[1] - scr[2] - scr[3])
		window->h = (int) (scr[1] - scr[2] - scr[3]);
	if (SDL_WINDOWPOS_ISUNDEFINED (window->undefined_x ? SDL_WINDOWPOS_UNDEFINED : x) || SDL_WINDOWPOS_ISCENTERED (x))
		x = (int) (scr[0] - window->w) / 2;
	if (SDL_WINDOWPOS_ISUNDEFINED (window->undefined_y ? SDL_WINDOWPOS_UNDEFINED : y) || SDL_WINDOWPOS_ISCENTERED (y))
		y = (int) (scr[3] + (scr[1] - scr[2] - scr[3] - window->h) / 2);
	d->win = ui_window_ex (x, y, window->w, window->h, (long) (window->title ? window->title : "SDL"), role);
	if (d->win <= 0) {
		SDL_free (d);
		return SDL_SetError ("ARMPI : fenetre refusee");
	}
	d->w = window->w;
	d->h = window->h;
	window->internal = d;
	SDL_SetMouseFocus (window);
	SDL_SetKeyboardFocus (window);
	return true;
}

static void ARMPI_DestroyWindow (SDL_VideoDevice *_this, SDL_Window *window)
{
	SDL_WindowData *d = window->internal;

	if (!d)
		return;
	if (d->framed)
		ui_window_close (d->framed);
	ui_window_close (d->win);
	SDL_free (d);
	window->internal = NULL;
}

static void ARMPI_SetWindowTitle (SDL_VideoDevice *_this, SDL_Window *window)
{
	SDL_WindowData *d = window->internal;

	use_window (d);
	ui_title ((long) (window->title ? window->title : ""));
}

static void set_size (SDL_Window *window, SDL_WindowData *d, int w, int h)
{
	d->w = w;
	d->h = h;
	show_gl_zone (d);
	SDL_SendWindowEvent (window, SDL_EVENT_WINDOW_RESIZED, w, h);
}

static void ARMPI_SetWindowSize (SDL_VideoDevice *_this, SDL_Window *window)
{
	SDL_WindowData *d = window->internal;

	use_window (d);
	ui_resize (window->pending.w, window->pending.h);
	set_size (window, d, window->pending.w, window->pending.h);
}

static void ARMPI_MaximizeWindow (SDL_VideoDevice *_this, SDL_Window *window)
{
	use_window (window->internal);
	ui_maximize ();			/* (Elegant repond par WEV_RESIZE) */
}

/* Plein ecran : une fenetre sans cadre, au-dessus de tout, de la taille de l'ecran, comme em_fullscreen
 * d'EmuKit ; celle du cadre attend dessous. La surface du GPU passe de l'une a l'autre. */
static SDL_FullscreenResult ARMPI_SetWindowFullscreen (SDL_VideoDevice *_this, SDL_Window *window, SDL_VideoDisplay *display, SDL_FullscreenOp fullscreen)
{
	SDL_WindowData *d = window->internal;
	long scr[4];

	if (!fullscreen == !d->framed)
		return SDL_FULLSCREEN_SUCCEEDED;
	if (d->surf) {			/* (SDL refait la surface a la nouvelle taille) */
		use_window (d);
		ui_surface_free (d->surf);
		d->surf = 0;
	}
	if (fullscreen) {
		long full;
		screen_size (scr);
		full = ui_window_ex (0, 0, scr[0], scr[1], (long) (window->title ? window->title : "SDL"), WF_FRAMELESS | WF_TOP);
		if (full <= 0)
			return SDL_FULLSCREEN_FAILED;
		d->framed = d->win;
		d->win = full;
		set_size (window, d, (int) scr[0], (int) scr[1]);
	} else {
		long full = d->win;
		long w, h;
		d->win = d->framed;
		d->framed = 0;
		ui_window_close (full);
		ui_window_use (d->win);
		w = ui.w;
		h = ui.h;
		set_size (window, d, (int) w, (int) h);
	}
	SDL_SetKeyboardFocus (window);
	return SDL_FULLSCREEN_SUCCEEDED;
}

/* --- presse-papiers ------------------------------------------------------------------------------- */

static bool ARMPI_SetClipboardText (SDL_VideoDevice *_this, const char *text)
{
	ui_clip_set ((long) text, -1);
	return true;
}

static char *ARMPI_GetClipboardText (SDL_VideoDevice *_this)
{
	size_t cap = 65536;
	char *s = SDL_malloc (cap);

	if (s)
		s[ui_clip_get ((long) s, (long) cap) > 0 ? cap - 1 : 0] = 0;
	return s;
}

static bool ARMPI_HasClipboardText (SDL_VideoDevice *_this)
{
	char s[2];

	return ui_clip_get ((long) s, sizeof s) > 0;
}

/* --- evenements ----------------------------------------------------------------------------------- */

static SDL_Window *window_of (SDL_VideoDevice *_this, long id)
{
	for (SDL_Window *w = _this->windows; w; w = w->next) {
		SDL_WindowData *d = w->internal;
		if (d && (d->win == id || d->framed == id))
			return w;
	}
	return _this->windows;
}

static void on_key (SDL_Window *window)
{
	int code = ui_ev_field (&ui, EV_CODE) & 255, ch = ui_ev_field (&ui, EV_CHAR);
	bool down = ui_ev_field (&ui, EV_KIND) != 0;

	if (code == KEY_F9) {
		if (down && window)
			SDL_SetWindowFullscreen (window, !(window->flags & SDL_WINDOW_FULLSCREEN));
		return;
	}
	if (down) {
		/* HIDManager donne le caractere de la disposition (AZERTY...) : le code de touche le suit */
		key_sym[code] = 0;
		if (ch > 32 && ch < 0x10000 && ch != 127) {
			key_sym[code] = ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
		}
	}
	if (key_sym[code])
		SDL_SendKeyboardKeyAndKeycode (0, SDL_DEFAULT_KEYBOARD_ID, code, (SDL_Scancode) code, key_sym[code], down);
	else
		SDL_SendKeyboardKey (0, SDL_DEFAULT_KEYBOARD_ID, code, (SDL_Scancode) code, down);
	if (down && ch >= 32 && ch != 127) {
		char text[8];
		*SDL_UCS4ToUTF8 ((Uint32) ch, text) = 0;
		SDL_SendKeyboardText (text);
	}
}

static void on_mouse (SDL_Window *window)
{
	static const Uint8 sdl_button[3] = { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE };
	static int held;
	int b = ui_ev_field (&ui, EV_CODE) & 7;

	SDL_SendMouseMotion (0, window, SDL_DEFAULT_MOUSE_ID, false, (float) ui_ev_field (&ui, EV_X), (float) ui_ev_field (&ui, EV_Y));
	for (int i = 0; i < 3; i++)
		if ((b ^ held) & (1 << i))
			SDL_SendMouseButton (0, window, SDL_DEFAULT_MOUSE_ID, sdl_button[i], (b >> i) & 1);
	held = b;
}

static void on_pad (void)
{
	short axes[4];

	/* (+16 : quatre axes de 16 bits ; manette 0 : le clavier, deja rendu en touches) */
	SDL_memcpy (axes, ui.ev + WEV_PAD_AXES, sizeof axes);
	if (ui_ev_field (&ui, 4) > 0)
		ARMPI_PadEvent (ui_ev_field (&ui, 4), (unsigned) ui_ev_field (&ui, 8), axes, (unsigned) ui_ev_field (&ui, 24));
}

static void ARMPI_PumpEvents (SDL_VideoDevice *_this)
{
	long t;

	while ((t = ui_event (0)) > 0) {
		SDL_Window *window = window_of (_this, ui_ev_field (&ui, EV_WIN));
		SDL_WindowData *d = window ? window->internal : NULL;
		switch (t) {
		case WEV_KEY:
			on_key (window);
			break;
		case WEV_MOUSE:
			on_mouse (window);
			break;
		case WEV_WHEEL:
			SDL_SendMouseWheel (0, window, SDL_DEFAULT_MOUSE_ID, 0.0f, (float) ui_ev_field (&ui, EV_Y), SDL_MOUSEWHEEL_NORMAL);
			break;
		case WEV_CLOSE:
			if (window)
				SDL_SendWindowEvent (window, SDL_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
			break;
		case WEV_FOCUS:
			/* (en plein ecran, la fenetre a cadre, cachee dessous, perd le focus : ce n'est pas le jeu) */
			if (d && ui_ev_field (&ui, EV_WIN) == d->win)
				SDL_SetKeyboardFocus (ui_ev_field (&ui, EV_KIND) ? window : NULL);
			break;
		case WEV_RESIZE:
			if (d) {
				int w = ui_ev_field (&ui, 8), h = ui_ev_field (&ui, 12);
				use_window (d);
				ui_resize (w, h);
				set_size (window, d, w, h);
			}
			break;
		case WEV_PAD:
			on_pad ();
			break;
		case WEV_SCREEN:
			/* resolution changee a chaud : le plein ecran suit, l'agrandie a recu WEV_RESIZE */
			if (d) {
				int w = ui_ev_field (&ui, 8), h = ui_ev_field (&ui, 12);
				SDL_DisplayMode mode;
				SDL_zero (mode);
				mode.format = SDL_PIXELFORMAT_XRGB8888;
				mode.w = w;
				mode.h = h;
				SDL_SetDesktopDisplayMode (SDL_GetVideoDisplayForWindow (window), &mode);
				if (d->framed && d->win == ui_ev_field (&ui, EV_WIN)) {
					use_window (d);
					ui_resize (w, h);
					set_size (window, d, w, h);
				}
			}
			break;
		default:
			break;
		}
	}
}

/* --- le pilote ------------------------------------------------------------------------------------ */

static bool ARMPI_VideoInit (SDL_VideoDevice *_this)
{
	SDL_DisplayMode mode;
	long scr[4];

	screen_size (scr);
	SDL_zero (mode);
	mode.format = SDL_PIXELFORMAT_XRGB8888;
	mode.w = (int) scr[0];
	mode.h = (int) scr[1];
	/* (latin_letters remplacerait notre table, partielle, par celle du QWERTY) */
	SDL_SetHintWithPriority (SDL_HINT_KEYCODE_OPTIONS, "french_numbers", SDL_HINT_DEFAULT);
	return SDL_AddBasicVideoDisplay (&mode) != 0;
}

static void ARMPI_VideoQuit (SDL_VideoDevice *_this)
{
}

static bool ARMPI_GetDisplayUsableBounds (SDL_VideoDevice *_this, SDL_VideoDisplay *display, SDL_Rect *rect)
{
	long scr[4];

	screen_size (scr);
	rect->x = 0;
	rect->y = (int) scr[3];
	rect->w = (int) scr[0];
	rect->h = (int) (scr[1] - scr[2] - scr[3]);
	return true;
}

static void ARMPI_DeleteDevice (SDL_VideoDevice *device)
{
	SDL_free (device);
}

static SDL_VideoDevice *ARMPI_CreateDevice (void)
{
	SDL_VideoDevice *device;

	if (!uikit_open (&ui)) {
		SDL_SetError ("ARMPI : pas d'UIKit, ou pas de polices");
		return NULL;
	}
	device = SDL_calloc (1, sizeof *device);
	if (!device)
		return NULL;
	device->VideoInit = ARMPI_VideoInit;
	device->VideoQuit = ARMPI_VideoQuit;
	device->GetDisplayUsableBounds = ARMPI_GetDisplayUsableBounds;
	device->PumpEvents = ARMPI_PumpEvents;
	device->CreateSDLWindow = ARMPI_CreateSDLWindow;
	device->DestroyWindow = ARMPI_DestroyWindow;
	device->SetWindowTitle = ARMPI_SetWindowTitle;
	device->SetWindowSize = ARMPI_SetWindowSize;
	device->MaximizeWindow = ARMPI_MaximizeWindow;
	device->SetWindowFullscreen = ARMPI_SetWindowFullscreen;
	device->CreateWindowFramebuffer = ARMPI_CreateWindowFramebuffer;
	device->UpdateWindowFramebuffer = ARMPI_UpdateWindowFramebuffer;
	device->DestroyWindowFramebuffer = ARMPI_DestroyWindowFramebuffer;
	device->GL_LoadLibrary = ARMPI_GL_LoadLibrary;
	device->GL_GetProcAddress = ARMPI_GL_GetProcAddress;
	device->GL_UnloadLibrary = ARMPI_GL_UnloadLibrary;
	device->GL_CreateContext = ARMPI_GL_CreateContext;
	device->GL_MakeCurrent = ARMPI_GL_MakeCurrent;
	device->GL_SwapWindow = ARMPI_GL_SwapWindow;
	device->GL_SetSwapInterval = ARMPI_GL_SetSwapInterval;
	device->GL_GetSwapInterval = ARMPI_GL_GetSwapInterval;
	device->GL_DestroyContext = ARMPI_GL_DestroyContext;
	device->SetClipboardText = ARMPI_SetClipboardText;
	device->GetClipboardText = ARMPI_GetClipboardText;
	device->HasClipboardText = ARMPI_HasClipboardText;
	device->free = ARMPI_DeleteDevice;
	return device;
}

VideoBootStrap PRIVATE_bootstrap = {
	"armpi", "ARMPI : Elegant, UIKit, armgl",
	ARMPI_CreateDevice,
	NULL,
	false
};
