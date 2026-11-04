# EmuKit: the base of the emulators

**EmuKit** (`SD0:/lib/emukit.so`) does everything in an emulator that is not the console: picking the ROM (argument or **Open** dialog), opening the window and its menus, scaling the picture, running the machine in a high-priority thread at the right pace, sending the sound to [AudioKit](audiokit_en.md), reading the gamepad and the keyboard, writing the `<rom>.sav` save, handling pause, reset, the three views and the speed.

The application only gives a **descriptor**: the picture size, the pace, the sound rate, and a few functions (load, make a picture, return the sound...). Korin OS's NES, GBC, SNES, GBA and N64 emulators (in assembly), DS, GameCube and 3DS (in C++) and Quake (in C) are made this way: a core and a small `main` function.

## Opening the kit

```c
#include <emukit.h>        // includes emukit_abi.h

// in main, once the descriptor is filled in:
if (!emukit_kit_open ()) return 1;
return (int) emu_run ((long) &desc, emu_args (argc, argv));
```

`emukit.h` adds to the generated header the `struct emu_desc` structure and `emu_args`, which joins the program's arguments back into one string for `emu_run`. Nothing to add when linking: `pibuild` takes every library of `/gcc/lib`. EmuKit opens UIKit and AudioKit by itself.

## The descriptor

`struct emu_desc`, field by field with `EMD_*` of `emukit.inc`:

| Field | Meaning |
|---|---|
| `name` | console name: title of the window and of the messages |
| `ext` | ROM patterns for the **Open** dialog, "`*.nes`" or "`*.iso;*.gcm`"; 0: all files |
| `w`, `h` | picture size |
| `maxw`, `maxh` | maximum size (0: `w`, `h`); `emu_frame_size` varies the picture below it |
| `fps_milli` | pace in millihertz (60,099 for 60.099 pictures per second) |
| `rate` | sound rate (s16 stereo) |
| `ctx` | first argument of every function |
| `load` | `long load (ctx, rom, bytes)`: 1 accepts the ROM, 0 refuses it; the ROM stays in memory |
| `frame` | `void frame (ctx, pixels, pitch, buttons, flags)`: one picture |
| `audio` | `long audio (ctx, lr, maximum frames)`: the sound produced, in frames; 0: nothing |
| `reset` | `void reset (ctx)`: reset (menu) |
| `save` | `long save (ctx, &pointer)`: bytes to write to `<rom>.sav`, 0: nothing changed |
| `restore` | `void restore (ctx, data, bytes)`: the `<rom>.sav` read on loading |
| `menu` | extra menus, in `ui_menu`'s format, numbers from 100 up |
| `cmd` | `void cmd (ctx, number)`: a choice from those menus |
| `mouse` | `void mouse (ctx, kind, x, y, buttons)`: the mouse in the picture |
| `bufs` | three pointers to the pictures (0: the kit allocates them) |
| `skip` | 1: `frame` accepts `EMF_SKIP` (the machine catches up without drawing) |
| `error` | text of the message when `load` refuses (0: a common text) |

Any function can stay 0 if the emulator does not need it, except `load` and `frame`.

### Who calls what, and where

| Function | Thread | When |
|---|---|---|
| `load`, `restore` | the one of `emu_run` | when the ROM is loaded, before the first `frame` |
| `frame`, `audio` | the machine | every picture; `audio` is called again as long as it fills the whole buffer (4096 frames) |
| `save` | the machine | every 300 pictures; the kit copies the data at once, then the window writes the file |
| `reset`, `cmd` | the machine | between two pictures |
| `mouse`, the key function of `emu_game` | the window | on each event |

`frame` receives the pixels of the picture to fill (`0x00RRGGBB`, `maxw` × `maxh` at most), the **pitch in bytes**, the **buttons** of all gamepads together (`PAD_UP`, `PAD_DOWN`, `PAD_LEFT`, `PAD_RIGHT`, `PAD_A`, `PAD_B`, `PAD_X`, `PAD_Y`, `PAD_L`, `PAD_R`, `PAD_L2`, `PAD_R2`, `PAD_SELECT`, `PAD_START`, `PAD_L3`, `PAD_R3`, `PAD_HOME`) and `EMF_SKIP` when the picture will not be shown. Whatever both the machine and the window touch (mouse, keys) must go through `volatile` or atomic variables, or through `emu_command`.

Gamepad 0 is the keyboard, according to `SD0:/etc/gamepad.ini` (by default: the arrows, A, B, X, Y, L, R; Enter and Pause for Start, Backspace and Print Screen for Select); gamepads 1 and 2 are the USB gamepads.

## The functions

| Function | Arguments | Result |
|---|---|---|
| `emu_run` | descriptor, string of options and ROM (0: the **Open** dialog) | exit code, on closing; `EME_CANCEL` (1): no ROM picked |
| `emu_frame_size` | width, height | from `frame`: the size of the current picture (at most `maxw` × `maxh`) |
| `emu_command` | number | from the window: `cmd` (or the reset) will be called on the machine's thread |
| `emu_rom_path` | – | the ROM's path |
| `emu_thread` | function (argument), argument, stack (bytes), name | thread number; it shares the main thread's TLS (Mesa finds its context there) |
| `emu_pad_axes` | gamepad (0 to 3), buffer of 4 `short` | the axes lx, ly, rx, ry (-1000..1000) |
| `emu_frame_same` | – | from `frame`: nothing new, the last published picture stays on screen |
| `emu_game` | key function, `EMG_*` flags | before `emu_run`: a game that wants the whole keyboard |
| `emu_pointer` | 1 / 0 | relative mouse: `mouse` receives kind 3 and movements (if the window has the keyboard) |
| `emu_rom_by_path` | – | before `emu_run`: the ROM is not read, `load` receives its path instead of the bytes (large disc images) |
| `emu_gl_hook` | function, ctx | before `emu_run`: `f (ctx, window, x, y, w, h)` returns the number of pictures shown; the application renders by itself (through Mesa) |

### A game rather than a console

`emu_game (keys, EMG_NOROM)` turns EmuKit into the base of a game: no ROM (`load` receives 0, 0), no **Open** dialog nor reset, and the keyboard is no longer gamepad 0: each key goes to the function given, on the window's thread:

```c
void keys (void *ctx, long code, long down, long ch, long mods);
// code: HID code of the key; down: 1 pressed, 0 released; ch: the character; mods: the modifiers
// code EMG_KEY_WHEEL: the wheel (down carries the value); EMG_KEY_NONE: the window lost the keyboard, everything is released
```

F9 (full screen) and F12 (speed) stay with the kit. A game lives in real time: its pace no longer follows the sound.

### What the user finds

EmuKit adds its **Game**, **View** and **Sound** menus: open, pause (P), reset, quit; zoom 1x, 2x, 3x, maximize, full screen (F9, Esc to leave), speed shown in the title (F12); sound off. The descriptor's menus come after. Command line options: `--full rom` (full screen at once), `--thumb output rom` (makes a card picture for Game Library, without a window).

## Example: a game without a ROM

A paddle and a ball, with the keyboard (arrows) or a gamepad (d-pad or left stick), with a beep on each bounce. The game touches neither the window nor the sound: EmuKit handles them.

```c
#include <string.h>
#include <emukit.h>

enum { W = 256, H = 192, RATE = 44100, FPS = 60000, PER_FRAME = RATE * 1000 / FPS };
enum { HID_RIGHT = 0x4F, HID_LEFT = 0x50, HID_SPACE = 0x2C };

static volatile int keys;			// written by the window's thread, read by the machine
static int pad_x = W / 2 - 24, ball_x = W / 2, ball_y = 40, dx = 2, dy = 2, misses, beep;
static short pcm[PER_FRAME * 2];
static int npcm;

static long load (void *ctx, const unsigned char *rom, long size) { return 1; }	// EMG_NOROM: rom = 0

static void key (void *ctx, long code, long down, long ch, long mods)
{
	int bit = code == HID_LEFT ? 1 : code == HID_RIGHT ? 2 : code == HID_SPACE ? 4 : 0;
	if (code == EMG_KEY_NONE) keys = 0;
	else if (bit) keys = down ? keys | bit : keys & ~bit;
}

static void box (unsigned *px, long pitch, int x, int y, int w, int h, unsigned c)
{
	for (int j = y; j < y + h; j++)
	{
		unsigned *row = (unsigned *) ((char *) px + j * pitch);	// the pitch is in bytes
		for (int i = x; i < x + w; i++) row[i] = c;
	}
}

static void frame (void *ctx, unsigned *px, long pitch, long buttons, long flags)
{
	short ax[4];
	emu_pad_axes (1, (long) ax);				// the stick of the first USB gamepad
	int k = keys, speed = ax[0] / 200;
	if ((k & 1) || (buttons & PAD_LEFT)) speed = -4;
	if ((k & 2) || (buttons & PAD_RIGHT)) speed = 4;
	pad_x += speed;
	if (pad_x < 0) pad_x = 0;
	if (pad_x > W - 48) pad_x = W - 48;
	if ((k & 4) || (buttons & PAD_START)) misses = 0;

	ball_x += dx; ball_y += dy;
	if (ball_x <= 0 || ball_x >= W - 6) { dx = -dx; beep = 3; }
	if (ball_y <= 0) { dy = -dy; beep = 3; }
	if (dy > 0 && ball_y >= H - 18 && ball_y <= H - 12 && ball_x + 6 > pad_x && ball_x < pad_x + 48) { dy = -dy; beep = 6; }
	if (ball_y >= H) { misses++; ball_x = W / 2; ball_y = 40; dy = 2; }

	// this picture's sound: a square wave while the beep lasts
	for (int i = 0; i < PER_FRAME; i++)
	{
		short s = beep > 0 ? ((i / 25) & 1 ? 2500 : -2500) : 0;
		pcm[2 * i] = pcm[2 * i + 1] = s;
	}
	npcm = PER_FRAME;
	if (beep > 0) beep--;

	box (px, pitch, 0, 0, W, H, 0x101828);
	for (int i = 0; i < misses && i < 20; i++) box (px, pitch, 4 + i * 8, 4, 5, 5, 0xE04040);
	box (px, pitch, pad_x, H - 12, 48, 6, 0xF0F0F0);
	box (px, pitch, ball_x, ball_y < H - 6 ? ball_y : H - 6, 6, 6, 0xF0C040);
}

static long audio (void *ctx, short *lr, long max)
{
	long n = npcm < max ? npcm : max;
	memcpy (lr, pcm, n * 4);
	npcm = 0;
	return n;
}

static struct emu_desc d;

int main (int argc, char **argv)
{
	if (!emukit_kit_open ()) return 1;
	d.name = "Paddle";
	d.w = W; d.h = H;
	d.fps_milli = FPS;
	d.rate = RATE;
	d.load = load;
	d.frame = frame;
	d.audio = audio;
	emu_game ((long) key, EMG_NOROM);
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
```

## Example: a "console" that reads files

This program treats any file as a ROM and shows its bytes in colors, one row of pixels per line of bytes. It uses the rest of the descriptor: the d-pad scrolls, A jumps a page; a **Viewer** menu changes the width (and so the picture size) and inverts the colors; a click marks a byte; the position is kept in `<file>.sav`, next to the file; **Reset** goes back to the start.

```c
#include <stdio.h>
#include <string.h>
#include <emukit.h>

enum { MAXW = 256, H = 192 };
enum { ID_W256 = 100, ID_W128, ID_INVERT, ID_CLICK };

struct viewer
{
	const unsigned char *rom;
	long size, offset, saved;		// first byte shown; the one in the .sav
	int width, invert, prev;
	long cursor;				// marked byte (-1: none)
	volatile int click_x, click_y;		// the last click (window's thread)
};
static struct viewer v = { 0, 0, 0, -1, 256, 0, 0, -1, 0, 0 };

static long load (void *ctx, const unsigned char *rom, long size)
{
	struct viewer *s = ctx;
	if (size <= 0) return 0;			// refused: the message of d.error
	s->rom = rom;
	s->size = size;
	printf ("%s: %ld bytes\n", (const char *) emu_rom_path (), size);
	return 1;
}

static void frame (void *ctx, unsigned *px, long pitch, long buttons, long flags)
{
	struct viewer *s = ctx;
	int pressed = (int) buttons & ~s->prev;	// the buttons just pressed
	s->prev = (int) buttons;
	if (buttons & PAD_DOWN) s->offset += s->width * 2;
	if (buttons & PAD_UP) s->offset -= s->width * 2;
	if (pressed & PAD_A) s->offset += (long) s->width * H;
	if (pressed & PAD_B) s->offset -= (long) s->width * H;
	if (s->offset > s->size - s->width) s->offset = s->size - s->width;
	if (s->offset < 0) s->offset = 0;

	emu_frame_size (s->width, H);		// every picture: the size of this one
	for (int y = 0; y < H; y++)
	{
		unsigned *row = (unsigned *) ((char *) px + y * pitch);
		for (int x = 0; x < s->width; x++)
		{
			long i = s->offset + (long) y * s->width + x;
			unsigned b = i < s->size ? s->rom[i] : 0, c = b << 16 | (b * 3 & 255) << 8 | (255 - b);
			if (i >= s->size) c = 0;
			else if (s->invert) c ^= 0xFFFFFF;
			if (i == s->cursor) c = 0xFFFFFF;
			row[x] = c;
		}
	}
}

static void reset (void *ctx) { struct viewer *s = ctx; s->offset = 0; s->cursor = -1; }

static void cmd (void *ctx, long id)
{
	struct viewer *s = ctx;
	switch (id)
	{
	case ID_W256: s->width = 256; break;
	case ID_W128: s->width = 128; break;
	case ID_INVERT: s->invert = !s->invert; break;
	case ID_CLICK:					// (here, on the machine's thread)
		s->cursor = s->offset + (long) s->click_y * s->width + s->click_x;
		if (s->cursor >= s->size) s->cursor = -1;
		else printf ("byte %ld = 0x%02x\n", s->cursor, s->rom[s->cursor]);
		break;
	}
}

// kind: 0 move, 1 press, 2 release; x, y in the picture
static void mouse (void *ctx, long kind, long x, long y, long buttons)
{
	struct viewer *s = ctx;
	if (kind != 1 || x < 0 || y < 0 || x >= s->width || y >= H) return;
	s->click_x = (int) x;
	s->click_y = (int) y;
	emu_command (ID_CLICK);			// the machine does the rest, between two pictures
}

static long save (void *ctx, const void **data)
{
	struct viewer *s = ctx;
	if (s->offset == s->saved) return 0;	// nothing changed: no writing
	s->saved = s->offset;
	*data = &s->saved;
	return sizeof s->saved;
}

static void restore (void *ctx, const void *data, long size)
{
	struct viewer *s = ctx;
	if (size == sizeof s->offset) memcpy (&s->offset, data, sizeof s->offset);
	s->saved = s->offset;
}

static struct emu_desc d;

int main (int argc, char **argv)
{
	if (!emukit_kit_open ()) return 1;
	d.name = "Byte Viewer";
	d.ext = 0;					// the Open dialog shows every file
	d.w = MAXW; d.h = H;
	d.maxw = MAXW; d.maxh = H;
	d.fps_milli = 30000;
	d.rate = 44100;					// (no sound: d.audio stays 0)
	d.ctx = &v;
	d.load = load;
	d.frame = frame;
	d.reset = reset;
	d.save = save;
	d.restore = restore;
	d.menu = "MViewer\nI100\tWidth 256\nI101\tWidth 128\n-\nI102\tInvert Colors\n";
	d.cmd = cmd;
	d.mouse = mouse;
	d.error = "This file is empty.";
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
```

The menu labels and the error message are in English, like EmuKit's own: the application's dictionary (`lang/fr.txt`, "`Invert Colors = Inverser les couleurs`") translates them for French.

## Pitfalls

- **`emu_frame_size` on every picture**: the size is noted for the buffer `frame` fills; there are three of them. Calling it only once leaves the two other pictures at the old size.
- **The pitch is in bytes**, the picture is at most `maxw` × `maxh` pixels: do not write beyond the announced width.
- **Two threads**: `frame` runs on the machine, `mouse` and the key function on the window. Share through `volatile` or atomic variables, or go through `emu_command`. Do not touch UIKit from the machine.
- **`audio` returns frames** (s16 stereo, 4 bytes each): return exactly what the machine produced. A console emulator follows this sound: returning too much slows the game down, too little speeds it up.
- **`save`**: return 0 as long as nothing changed, otherwise the SD card is rewritten every 300 pictures. The pointer must stay valid until the return (the kit copies at once).
- **`EMG_NOROM`**: the keyboard is no longer gamepad 0; read it through the key function. The USB gamepads stay in `buttons`.
- **F9, F12** always belong to the kit; F10 and F11 belong to Elegant.
- **`emu_run` only returns on closing**: prepare everything (descriptor, `emu_game`, `emu_rom_by_path`, `emu_gl_hook`) before.
- **Under QEMU**, no USB gamepad: the keyboard only. The sound goes to the virtual output.

## See also

- [AudioKit](audiokit_en.md): sound, which the kit sends for you.
- [GpuKit](gpukit_en.md): 3D rendering by the GPU (N64, Cosmo Zoom!).
- [UIKit](uikit_en.md): windows, if the game wants one of its own.
- The help of the emulators, for instance **Game Library**.
