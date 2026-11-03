//
// basic -- /bin/basic, the runtime of Onyx BASIC on ARMPI (docs/BASIC.md):  basic <program.bas> [args]
//
// Compiles the program (src/lib/basic/bascomp.cpp) and runs it on the VM (basvm.cpp) with a host made of
// ARMPI's kits. Started from a terminal, PRINT and INPUT use it until the program opens its window (SCREEN,
// graphics, WINDOW, a control); else the first PRINT opens the window: a QBasic screen (basscreen.h: 80 x 25
// text cells = 640 x 400, SCREEN 12 = 640 x 480, SCREEN 13 = 320 x 200) in a ui_view, the program's controls its
// children. A windowed app (WINDOW) takes the theme's colours, unless COLOR chose others; the QBasic
// screens keep their own. The program's folder becomes the current directory.
//
// The options: -d <dir> (current directory), -i <fd> (a syntax or run-time error sent to an editor on this
// descriptor: "line\0message\0"), -c (compile only: prog.bas -> prog.bax), -m (managed: the program on the VM,
// without it in machine code, basjit.h), -t (the text on the output even if it is no terminal), -p (where the time goes: SD0:/basprof.txt), -u (the card's standalone
// apps given this runtime).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <armpi/kit.h>
#include <uikit.h>
#include <audiokit_abi.h>
#include <gpukit_abi.h>
#include <gpiokit_abi.h>

// (src/inc/pad.inc)
enum { WEV_PAD_BUTTONS = 8, WEV_PAD_AXES = 16 };

#include "../../lib/basic/basscreen.h"
#include "../../lib/basic/bas3dclip.h"
#include "../../lib/basic/hostutil.h"

extern "C" const char *__posix_exe_path (void);
extern char **environ;

static struct ui_vars ui;
class ArmHost;
static ArmHost *g_host = 0;

#include "baskits.h"

static const unsigned *s_jet;
static unsigned jet (int off) { return s_jet[off / 4] & 0xFFFFFF; }

// ---- the host: the shared BASIC screen (basscreen.h) in a window of UIKit ----------------------------------
class ArmHost : public bas::ScreenHost
{
public:
	bool console = false, uiOpen = false, resizable = false, closed = false;
	long view = 0;
	unsigned lastPresent = 0;
	char args[256];

	ArmHost () { args[0] = 0; }

	bool uiReady ()
	{
		if (uiOpen) return true;
		if (!uikit_open (&ui)) return false;
		s_jet = (const unsigned *) ui_jetons ();
		uiOpen = true;
		return true;
	}

	// ---- the platform (bas::ScreenHost) ------------------------------------------------------------------
	static long paint (long, long w, long h)
	{
		ArmHost *o = g_host;
		long pw, ph;
		uint32_t *d = uikit_canvas_pixels (o->view, &pw, &ph);
		const unsigned *v = o->visible ();
		int ow = o->W, oh = o->H, sx = o->sx, sy = o->sy;
		for (int y = 0; y < h && y / sy < oh; y++)
		{
			uint32_t *q = d + (long) y * pw;
			const unsigned *s = v + (long) (y / sy) * ow;
			int n = w < ow * sx ? (int) w : ow * sx;
			if (sx == 1) memcpy (q, s, n * 4);
			else for (int x = 0; x < n; x++) q[x] = s[x / sx];
		}
		return 0;
	}
	static long on_resized (long w, long h)
	{
		ArmHost *o = g_host;
		ui_w_move (o->view, 0, 0, w, h);
		o->userResized ((int) w, (int) h);
		return 0;
	}
	bool openWindow (int w, int h) override
	{
		if (bk_win || !uiReady ()) return false;		// (the program made its own window: UIKit.window)
		if (resizable)
		{
			long scr[4] = { 1024, 768, 0, 0 };
			ui_screen ((long) scr);
			bas_utf8 t (title[0] ? title : "BASIC");
			if (ui_window_ex (0, scr[3], w, h, (long) t.s, WF_RESIZABLE) <= 0) return false;
			ui_on_resize ((long) on_resized, 0);
		}
		else
		{
			bas_utf8 t (title[0] ? title : "BASIC");
			if (ui_window (w, h, (long) t.s) <= 0) return false;
		}
		view = ui_view (0, 0, w, h, 0);
		ui_view_on (view, (long) paint, 0, 0, 0);
		return view != 0;
	}
	void resizeWindow (int w, int h) override
	{
		// SCREEN n: the window takes the mode's size
		ui_resize (w, h);
		ui_w_move (view, 0, 0, w, h);
	}
	void markDirty () override { if (view) ui_view_dirty (view); }
	void present (bool force) override
	{
		if (!view && !fsDlg) return;
		unsigned now = nowMs ();
		if (!force && now - lastPresent < 20) return;		// ~50 Hz at most
		lastPresent = now;
		if (fsDlg) { blitFull (); return; }
		ui_view_flush ();
	}
	void pumpEvents () override;
	bool stopRequested () override { return closed || bk_closed; }
	unsigned nowMs () override { return bas_ms (); }
	void sleepRaw (int ms) override { usleep ((ms > 0 ? ms : 1) * 1000); }
	unsigned clockUs () override { return bas_us (); }
	void *codeAlloc (unsigned size) override
	{
		void *p = mmap (0, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		return p == MAP_FAILED ? 0 : p;
	}
	bas::Profile profile;
	void profReport () override
	{
		char t[320]; int n = profile.text (t, sizeof t - 2);
		t[n++] = '\n'; t[n] = 0;
		save ("SD0:/basprof.txt", t, n);
	}

	// ---- keys: Latin-1 characters, K_* for the others; what is held, by the character each key gave --------
	unsigned char heldKey[256]; int heldCode[256];
	bool keyHeld (int key) override
	{
		for (int i = 0; i < 256; i++) if (heldKey[i] && heldCode[i] == key) return true;
		return false;
	}
	static int hid_key (int hid)
	{
		switch (hid)
		{
		case 0x28: case 0x58: return bas::K_ENTER;
		case 0x2A: return bas::K_BACKSPACE;
		case 0x2B: return bas::K_TAB;
		case 0x29: return 27;
		case 0x4F: return bas::K_RIGHT;
		case 0x50: return bas::K_LEFT;
		case 0x51: return bas::K_DOWN;
		case 0x52: return bas::K_UP;
		case 0x4A: return bas::K_HOME;
		case 0x4D: return bas::K_END;
		case 0x4B: return bas::K_PGUP;
		case 0x4E: return bas::K_PGDN;
		case 0x4C: return bas::K_DEL;
		}
		if (hid >= 0x3A && hid <= 0x45) return bas::K_F1 + hid - 0x3A;
		return 0;
	}

	// ---- the gamepads: WEV_PAD (inc/pad.inc) gives what changes, to the window that has the keyboard. BASIC's pads 0..2 are
	// the drivers' 1..3, as Onyx's USB pads; the keyboard mapped by SD0:/etc/gamepad.ini is pad 3, for who asks for it by its
	// number: in Onyx too it is no pad unless a program wants it (a game has its keys already). No call says what is
	// plugged in: a pad is known from its first event.
	enum { BPADS = 4, KEYBOARD_PAD = 3 };
	unsigned padB[BPADS] = { 0, 0, 0, 0 }; short padA[BPADS][4]; bool padSeen[BPADS] = { false, false, false, false };
	void padEvent ()
	{
		int n = ui_ev_field (&ui, EV_KIND);
		if (n < 0 || n >= BPADS) return;
		int p = n == 0 ? KEYBOARD_PAD : n - 1;
		padB[p] = (unsigned) ui_ev_field (&ui, WEV_PAD_BUTTONS);
		memcpy (padA[p], ui.ev + WEV_PAD_AXES, sizeof padA[p]);
		padSeen[p] = true;
	}
	void padsOff () { for (int i = 0; i < BPADS; i++) { padB[i] = 0; memset (padA[i], 0, sizeof padA[i]); } }	// (no keyboard: nothing pressed)
	unsigned padButtons (int pad) override
	{
		if (pad >= 0) return pad < BPADS ? padB[pad] : 0;
		return padB[0] | padB[1] | padB[2];
	}
	int padAxis (int pad, int axis) override
	{
		if (pad < 0) pad = 0;
		return pad < BPADS && axis >= 0 && axis < 4 ? padA[pad][axis] : 0;
	}
	bool padConnected (int pad) override { return pad >= 0 && pad < KEYBOARD_PAD && padSeen[pad]; }

	// ---- full screen: a frameless window over everything, the visible page scaled into it, proportions kept --
	long fsDlg = 0; int fsOx = 0, fsOy = 0, fsW = 1, fsH = 1;
	void blitFull ()
	{
		const unsigned *v = visible ();
		int sw = (int) ui.w, sh = (int) ui.h, dw = W * sx, dh = H * sy, ow, oh;
		if ((long) sw * dh <= (long) sh * dw) { ow = sw; oh = (int) ((long) sw * dh / dw); }
		else { oh = sh; ow = (int) ((long) sh * dw / dh); }
		int k = sx == sy ? ow / W : 0;				// a whole-number zoom when it fills nearly as much
		if (k >= 1 && k * W * 100 >= ow * 85) { ow = k * W; oh = k * H; }
		int ox = (sw - ow) / 2, oy = (sh - oh) / 2;
		fsOx = ox; fsOy = oy; fsW = ow; fsH = oh;
		static int xmap[8192];
		for (int x = 0; x < ow && x < 8192; x++) xmap[x] = x * W / ow;
		for (int y = 0; y < oh; y++)
		{
			const unsigned *s = v + (long) (y * H / oh) * W;
			uint32_t *d = ui.surf + (long) (oy + y) * sw + ox;
			for (int x = 0; x < ow && x < 8192; x++) d[x] = s[xmap[x]];
		}
		ui_update_all ();
	}
	void fullscreen (bool on) override
	{
		if (!ensureWindow ()) return;
		if (on && !fsDlg)
		{
			long scr[4] = { 1024, 768, 0, 0 };
			ui_screen ((long) scr);
			ui_dialog_at (0, 0);
			fsDlg = ui_dialog_begin (scr[0], scr[1], (long) "BASIC");
			if (!fsDlg) return;
			ui_fill (0, 0, ui.w, ui.h, 0);
			ui_update_all ();
			present (true);
		}
		else if (!on && fsDlg)
		{
			ui_dialog_end (fsDlg); fsDlg = 0;
			ui_view_dirty (view); present (true);
		}
	}
	void leaveFullscreen () override { if (fsDlg) fullscreen (false); }
	bool windowColours (unsigned *face, unsigned *text) override
	{
		if (!uiReady ()) return false;
		*face = jet (TJ_FACE); *text = jet (TJ_TEXTE1);
		return true;
	}
	void windowFlags (int flags) override { if (flags & 1) resizable = true; }

	// ---- 3D by GpuKit: the frame drawn right into the active page; without a GPU, basscreen.h's renderer -----
	int gpuState = -1;					// -1 not asked, 0 no GPU, 1 yes
	bool gpuDry = false;					// basic -g: the GPU's lists made, nothing sent to it (GK_DBG_DRY: a test, also on QEMU)
	int dryOk = 0, dryBad = 0; long dryLast = 0;
	bool gpuReady ()
	{
		if (gpuState < 0)
		{
			if (gpuDry && gpukit_kit_open ()) { gk_debug (GK_DBG_DRY, 1, 0); gpuState = 1; }
			else gpuState = gpukit_kit_open () && gk_info (0) == 1 ? 1 : 0;
		}
		return gpuState == 1;
	}
	bool gpu3d () override { return gpuReady (); }
	int gpuTexture (const unsigned *px, int w, int h) override
	{
		if (!gpuReady ()) return -1;
		long r = gk_texture (-1, (long) px, w, h, w);
		if (gpuDry) fprintf (stderr, "basic -g: a texture %d x %d -> %ld\n", w, h, r);
		return r < 0 ? -1 : (int) r;
	}
	int gpuRender (unsigned *dst, int w, int h, int stride, const bas::G3Vertex *v, int nv, const bas::G3Batch *b, int nb, unsigned clear, bool keep) override
	{
		if (!gpuReady ()) return -1;
		struct { unsigned *px; int w, h, stride; unsigned clear, flags; unsigned pad; } f = { dst, w, h, stride, clear, keep ? (unsigned) GK_F_KEEP : 0u, 0 };
		static_assert (sizeof f == GKF_SIZE, "gpukit.inc");
		static bas::G3GpuFrame g3c;			// (GpuKit does not clip: bas3dclip.h)
		int cn = 0;
		if (!bas::g3ClipFrame (g3c, v, nv, b, nb, &cn)) return -1;
		long r = gk_render ((long) &f, (long) g3c.v, cn, (long) g3c.b, nb);
		if (gpuDry)
		{
			if (r == 0) dryOk++; else { dryBad++; dryLast = r; }
			if ((dryOk + dryBad) % 50 == 1) fprintf (stderr, "basic -g: %d frames taken by GpuKit, %d refused (last %ld), %d vertices\n", dryOk, dryBad, dryLast, nv);
			return 0;
		}
		if (r == GK_E_NOGPU || r == GK_E_LOST) gpuState = 0;	// (software from now on)
		return (int) r;
	}

	// ---- the console: started from a terminal, before the program opens its window ------------------------
	bool windowText () { return win || !console; }
	void out (const char *s, int n) override
	{
		if (windowText ()) { ScreenHost::out (s, n); return; }
		for (int i = 0; i < n; i++)
		{
			unsigned c = bas437ToLatin1[(unsigned char) s[i]];		// UTF-8 for the terminal
			char t[2]; int m = 0;
			if (c < 0x80) t[m++] = (char) c;
			else { t[m++] = (char) (0xC0 | c >> 6); t[m++] = (char) (0x80 | (c & 63)); }
			fwrite (t, 1, m, stdout);
			ccol = s[i] == '\n' ? 0 : ccol + 1;
		}
		fflush (stdout);
	}
	int inputLine (char *buf, int cap) override
	{
		if (windowText ()) return ScreenHost::inputLine (buf, cap);
		int n = 0;
		for (;;)
		{
			int c = getchar ();
			if (c == EOF || c == 4) return n ? n : -1;
			if (c == '\r') continue;
			if (c == '\n') break;
			if (c >= 0xC0 && c < 0xE0)
			{
				int d = getchar ();
				c = ((c & 31) << 6) | (d & 63);
			}
			if (n < cap - 1 && c < 256) buf[n++] = (char) basLatin1To437[c];
		}
		buf[n] = 0; ccol = 0;
		return n;
	}
	int inkey (char *o) override
	{
		if (win || !console) return ScreenHost::inkey (o);
		struct pollfd p = { 0, POLLIN, 0 };
		if (::poll (&p, 1, 0) <= 0) return 0;
		int c = getchar ();
		if (c == EOF) return 0;
		o[0] = (char) basLatin1To437[c & 255];
		return 1;
	}
	void cls (int m) override
	{
		if (!windowText ()) { fputc ('\n', stdout); fflush (stdout); return; }
		ScreenHost::cls (m);
	}

	// ---- time ----------------------------------------------------------------------------------------------
	double timer () override
	{
		struct timespec t; clock_gettime (CLOCK_REALTIME, &t);
		struct tm m; time_t s = t.tv_sec; localtime_r (&s, &m);
		return m.tm_hour * 3600.0 + m.tm_min * 60 + m.tm_sec + t.tv_nsec / 1e9;
	}
	void date (char *o) override
	{
		time_t s = ::time (0); struct tm m; localtime_r (&s, &m);
		snprintf (o, 11, "%02d-%02d-%04d", m.tm_mon + 1, m.tm_mday, m.tm_year + 1900);
	}
	void time (char *o) override
	{
		time_t s = ::time (0); struct tm m; localtime_r (&s, &m);
		snprintf (o, 9, "%02d:%02d:%02d", m.tm_hour, m.tm_min, m.tm_sec);
	}
	unsigned seed () override { return bas_us () * 2654435761u; }

	// ---- sound: SOUND and PLAY's voices are AudioKit's FM synthesizer; files and MIDI, AudioKit too ----------
	long fm = 0, midi = 0, midiStream = 0, player = 0; int akOpen = -1, playLoop = 0, playVol = 100;
	char playPath[256], akErr[80]; volatile int midiRun = 0; pthread_t midiTid;
	bool audioKit () { if (akOpen < 0) akOpen = audiokit_kit_open (); return akOpen == 1; }
	int note (int voice, double freq, int wave, int vol) override
	{
		if (!audioKit ()) return -1;
		if (freq <= 0) { if (fm) ak_fm_stop (fm, voice); return 0; }
		if (!fm)
		{
			fm = ak_fm_new (0);
			if (fm <= 0) { fm = 0; return -1; }
			ak_fm_live (fm, 0);
		}
		return ak_fm_start (fm, voice, (long) (freq * 1000), wave, vol) == 0 ? 0 : -1;
	}
	void endSound () override
	{
		if (fm) { ak_fm_silence (fm); ak_fm_live (fm, -1); ak_fm_free (fm); fm = 0; }
		if (player) { ak_play_stop (player); player = 0; }
		if (midiRun) { midiRun = 0; pthread_join (midiTid, 0); ak_close (midiStream); ak_synth_free (midi); midi = 0; }
	}
	void keepPlaying ()					// PLAYFILE ..., 1: again at its end
	{
		if (player && playLoop && ak_play_state (player) == AKP_ENDED)
		{
			ak_play_stop (player);
			player = ak_play ((long) playPath, 0);
			if (player < 0) player = 0;
			else ak_play_volume (player, playVol * 256 / 100);
		}
	}
	int akPlay (const char *path, int loop) override
	{
		if (!audioKit ()) { bas_cpy (akErr, "no AudioKit", sizeof akErr); return -1; }
		if (player) { ak_play_stop (player); player = 0; }
		bas_path (playPath, sizeof playPath, path);
		long r = ak_play ((long) playPath, 0);
		if (r < 0) { bas_cpy (akErr, "this file cannot be played", sizeof akErr); return -1; }
		player = r; playLoop = loop;
		ak_play_volume (player, playVol * 256 / 100);
		return 0;
	}
	void akCommand (int what, int v) override
	{
		if (!audioKit ()) return;
		if (what == 0 && player) { ak_play_stop (player); player = 0; }
		else if (what == 1 && player) ak_play_pause (player, v);
		else if (what == 2) { playVol = v < 0 ? 0 : v; if (player) ak_play_volume (player, playVol * 256 / 100); }
		else if (what == 3 && midi) for (int c = 0; c < 16; c++) ak_synth_midi (midi, c, 0xB0, 123, 0);
	}
	double akQuery (int what) override
	{
		if (!player) return 0;
		if (what == 1) return ak_play_pos (player) / 1000.0;
		long st = ak_play_state (player);
		if (what == 0) return st == AKP_PLAYING ? 1 : st == AKP_PAUSED ? 2 : 0;
		unsigned char info[AKI_SIZE];
		if (ak_dec_info (ak_play_decoder (player), (long) info) != 0) return 0;
		long ms; memcpy (&ms, info + AKI_DURATION, sizeof ms);
		return ms / 1000.0;
	}
	static void *midi_loop (void *)
	{
		ArmHost *o = g_host;
		static short pcm[2 * 512];
		while (o->midiRun)
		{
			ak_synth_render (o->midi, (long) pcm, 512);
			((long (*) (long, long, long)) ak_write_all) (o->midiStream, (long) pcm, sizeof pcm);
		}
		return 0;
	}
	int akMidi (int cmd, int ch, int d1, int d2) override
	{
		if (!audioKit ()) return -1;
		if (!midi)
		{
			midi = ak_synth_new (0, 0);
			if (midi <= 0) { midi = 0; return -1; }
			midiStream = ak_open (44100, 2, 16, 0);
			if (midiStream < 0) { ak_synth_free (midi); midi = 0; return -1; }
			midiRun = 1;
			pthread_create (&midiTid, 0, midi_loop, 0);
		}
		return ak_synth_midi (midi, ch, cmd, d1, d2) == 0 ? 0 : -1;
	}
	const char *akError () override { return akErr; }
	double akNoteHz (int key) override { return audioKit () ? ak_note_mhz (key) / 1000.0 : 0; }
	int akNoteKey (const char *name) override { return bas_note_key (name); }

	// ---- GpioKit (SD0:/lib/gpiokit.so): PINMODE, PIN, PWM, ON PIN, I2C, SPI ---------------------------------
	int gpioOpen = -1; bool i2cOpen = false, spiOpen = false;
	int gpio (int op, int a, int b, int c, const char *in, int inLen, char *out, int outCap) override
	{
		if (gpioOpen < 0) gpioOpen = gpiokit_kit_open ();
		if (gpioOpen != 1) return GP_NODEV;
		int r;
		switch (op)
		{
		case GP_MODE:	return (int) gp_mode (a, b);
		case GP_WRITE:	return (int) gp_write (a, b);
		case GP_READ:	return (int) gp_read (a);
		case GP_PWM:	return (int) gp_pwm (a, b, c);
		case GP_SERVO:	return (int) gp_servo (a, b);
		case GP_EDGES:	return (int) gp_edges (a, b);
		case GP_EVENTS:
		{
			static unsigned char ev[32 * GPE_SIZE]; int max = outCap / 2 < 32 ? outCap / 2 : 32;
			r = max > 0 ? (int) gp_events ((long) ev, max, 0) : 0;
			for (int k = 0; k < r; k++) { out[2 * k] = (char) ev[k * GPE_SIZE + GPE_PIN]; out[2 * k + 1] = (char) ev[k * GPE_SIZE + GPE_EDGE]; }
			return r;
		}
		case GP_FREE:
			if (a < 0) { i2cOpen = spiOpen = false; return (int) gp_release (); }
			return (int) gp_mode (a, GP_M_FREE);
		case GP_SIM:	i2cOpen = spiOpen = false; gp_release (); return (int) gp_sim (a);
		case GP_I2C_OPEN: r = (int) gp_i2c_open (a); i2cOpen = r >= 0; return r;
		case GP_SPI_OPEN: r = (int) gp_spi_open (a, b); spiOpen = r >= 0; return r;
		default: break;
		}
		// (the buses are opened by their first use when the program did not: 100 kHz, 1 MHz mode 0)
		if (op == GP_SPI_XFER) { if (!spiOpen && (r = (int) gp_spi_open (0, 0)) < 0) return r; spiOpen = true; }
		else if (!i2cOpen) { if ((r = (int) gp_i2c_open (0)) < 0) return r; i2cOpen = true; }
		switch (op)
		{
		case GP_I2C_REG_READ:	return (int) gp_i2c_reg_read (a, b);
		case GP_I2C_REG_WRITE:	return (int) gp_i2c_reg_write (a, b, c);
		case GP_I2C_XFER:	r = (int) gp_i2c_write_read (a, (long) in, inLen, (long) out, b); return r < 0 ? r : b > 0 ? r : 0;
		case GP_I2C_SCAN:	return outCap >= 16 ? (int) gp_i2c_scan ((long) out) : GP_NODEV;
		case GP_SPI_XFER:	return (int) gp_spi_transfer (a, (long) in, (long) out, inLen);
		default:		return GP_NODEV;
		}
	}
	const char *gpioError (int code) override
	{
		if (gpioOpen < 0) gpioOpen = gpiokit_kit_open ();
		return gpioOpen == 1 ? (const char *) gp_error (code) : "GpioKit (SD0:/lib/gpiokit.so) is not on this system";
	}

	// ---- GUI controls: a program's controls are UIKit's, children of the view; text is Latin-1 here ---------
	enum { MAXCTL = 128 };
	long ctl[MAXCTL]; int ctlKind[MAXCTL]; int nctl = 0;
	int control (int kind, int x, int y, int w, int h, const char *text, int val) override
	{
		if (!ensureWindow () || nctl >= MAXCTL - 1) return 0;
		int id = nctl + 1;
		long wd = 0;
		bas_utf8 t (text);
		switch (kind)
		{
		case bas::CTL_BUTTON:   wd = ui_pushbutton (x, y, w, h, (long) t.s, 0); break;
		case bas::CTL_LABEL:    wd = ui_label (x, y, w, h, (long) t.s, 0); break;
		case bas::CTL_TEXTBOX:  wd = ui_textbox (x, y, w, h, 0); if (wd) ui_w_set_text (wd, (long) t.s); break;
		case bas::CTL_CHECKBOX: wd = ui_checkbox (x, y, w, h, (long) t.s, val != 0); break;
		case bas::CTL_PROGRESS: wd = ui_progress (x, y, w, h, 100, val); break;
		case bas::CTL_SLIDER:   wd = ui_slider (x, y, w, h, 0, val > 0 ? val : 100, 0); break;
		case bas::CTL_DROPDOWN: wd = ui_dropdown (x, y, w, h, (long) t.s); break;
		case bas::CTL_LISTBOX:
			wd = ui_list (x, y, w, h, 0);
			if (wd)
				for (char *p = t.s; *p; )
				{
					char *e = strchr (p, '|');
					if (e) *e = 0;
					ui_list_add (wd, (long) p, 0);
					if (!e) break;
					p = e + 1;
				}
			break;
		default: return 0;
		}
		if (!wd) return 0;
		ui_w_set_parent (wd, view);
		ctl[id] = wd; ctlKind[id] = kind; nctl++;
		ui_w_draw (wd);
		dirty ();
		return id;
	}
	long get (int id) { return id > 0 && id <= nctl ? ctl[id] : 0; }
	void moveControl (int id, int x, int y, int w, int h) override { long wd = get (id); if (wd) { ui_w_move (wd, x, y, w > 1 ? w : 1, h > 1 ? h : 1); dirty (); } }
	void showControl (int id, bool on) override { long wd = get (id); if (wd) { ui_w_show (wd, on); dirty (); } }
	void enableControl (int id, bool on) override { long wd = get (id); if (wd) { ui_w_enable (wd, on); dirty (); } }
	void focusControl (int id) override { long wd = get (id); if (wd) ui_w_focus (wd); }
	void setText (int id, const char *s) override
	{
		long w = get (id); if (!w) return;
		bas_utf8 t (s);
		if (ctlKind[id] == bas::CTL_LISTBOX) ui_list_add (w, (long) t.s, 0);	// SETTEXT on a list = add an item
		else ui_w_set_text (w, (long) t.s);
		ui_w_draw (w);
		dirty ();
	}
	int getText (int id, char *buf, int cap) override
	{
		long w = get (id); buf[0] = 0; if (!w) return 0;
		const char *s = 0;
		if (ctlKind[id] == bas::CTL_LISTBOX) { long i = ui_w_get_value (w); if (i >= 0) s = (const char *) uikit_list_get (w, i, 0); }
		else s = (const char *) ui_w_get_text (w);
		if (s) bas_latin1 (buf, cap, s);
		return bas_len (buf);
	}
	int getValue (int id) override
	{
		long w = get (id); if (!w) return 0;
		long v = ui_w_get_value (w);
		return ctlKind[id] == bas::CTL_CHECKBOX ? (v ? -1 : 0) : (int) v;
	}
	void setValue (int id, int v) override { long w = get (id); if (w) { ui_w_set_value (w, v); ui_w_draw (w); dirty (); } }

	// ---- the menus: MENUITEM (title$, item$[, key$]), the bar written again at each new item ------------------
	enum { MAXMENU = 48 };
	struct MenuItem { char title[32], item[64], key[16]; int id; };
	MenuItem menus[MAXMENU]; int nmenu = 0;
	int menuItem (const char *t, const char *item, const char *key) override
	{
		if (!ensureWindow () || nmenu >= MAXMENU || nctl >= MAXCTL - 1) return 0;
		int id = nctl + 1; nctl++;
		ctl[id] = 0; ctlKind[id] = 0;
		MenuItem &m = menus[nmenu++];
		bas_cpy (m.title, t, sizeof m.title); bas_cpy (m.item, item, sizeof m.item); bas_cpy (m.key, key ? key : "", sizeof m.key); m.id = id;
		static char text[MAXMENU * 128]; int n = 0;
		bool done[MAXMENU] = { false };
		for (int i = 0; i < nmenu; i++)
		{
			if (done[i]) continue;
			bas_utf8 h (menus[i].title);
			n += snprintf (text + n, sizeof text - n, "M%s\n", h.s);
			for (int j = i; j < nmenu; j++)
			{
				if (done[j] || strcasecmp (menus[j].title, menus[i].title)) continue;
				done[j] = true;
				if (menus[j].item[0] == '-' && !menus[j].item[1]) { n += snprintf (text + n, sizeof text - n, "-\n"); continue; }
				bas_utf8 s (menus[j].item);
				n += snprintf (text + n, sizeof text - n, "I%d\t%s%s%s\n", menus[j].id, s.s, menus[j].key[0] ? "\t" : "", menus[j].key);
			}
		}
		ui_menu ((long) text);
		return id;
	}

	// ---- system --------------------------------------------------------------------------------------------
	void notify (const char *t, const char *m) override { bas_notify (t, m); }
	int msgbox (const char *t, const char *m, int b) override
	{
		if (!view && !bk_win && !ensureWindow ()) return 0;
		bas_utf8 a (t), c (m);
		int r = (int) ui_message ((long) a.s, (long) c.s, b);
		dirty (); present (true);
		return r;
	}
	int clipboard (char *buf, int cap) override
	{
		if (!uiReady ()) { buf[0] = 0; return 0; }
		static char t[8192]; long n = ui_clip_get ((long) t, sizeof t);
		if (n <= 0) { buf[0] = 0; return 0; }
		bas_latin1 (buf, cap, t);
		return bas_len (buf);
	}
	void setClipboard (const char *s) override { if (uiReady ()) { bas_utf8 t (s); ui_clip_set ((long) t.s, -1); } }
	bool fileDialog (bool sv, const char *dir, char *o, int cap) override
	{
		if (!view && !ensureWindow ()) return false;
		char d[256], name[128]; bas_path (d, sizeof d, dir[0] ? dir : "SD0:/home/documents");
		bas_cpy (name, o, sizeof name);
		long ok = sv ? ui_file_save ((long) d, (long) (name[0] ? name : "untitled.txt"), (long) o, cap) : ui_file_open ((long) d, (long) o, cap);
		dirty (); present (true);
		return ok != 0;
	}
	bool exec (const char *p, const char *a) override { char q[256]; bas_path (q, sizeof q, p); return bas_spawn (q, a, -1, 0) > 0; }
	bool launch (const char *app) override
	{
		char q[256]; snprintf (q, sizeof q, "SD0:/apps/%s.app/main", app);
		return bas_spawn (q, "", -1, 0) > 0;
	}
	bool chdir (const char *p) override { char q[256]; bas_path (q, sizeof q, p); return ::chdir (q) == 0; }
	// SHELL "prog args": a /bin tool (or a path), its output on the screen; SHELL alone opens a terminal.
	int shell (const char *cmd) override
	{
		int i = 0; while (cmd[i] == ' ') i++;
		if (!cmd[i]) return launch ("terminal") ? 0 : -1;
		char prog[200], path[220]; int n = 0;
		while (cmd[i] && cmd[i] != ' ' && n < 199) prog[n++] = cmd[i++];
		prog[n] = 0;
		while (cmd[i] == ' ') i++;
		if (strchr (prog, '/') || strchr (prog, ':')) bas_path (path, sizeof path, prog);
		else snprintf (path, sizeof path, "SD0:/bin/%s", prog);
		if (!windowText ())				// the console: the tool writes there itself
		{
			pid_t p = bas_spawn (path, cmd + i, -1, 0);
			if (p <= 0) return -1;
			int st = 0; waitpid (p, &st, 0);
			return WEXITSTATUS (st);
		}
		int fd[2];
		if (pipe (fd)) return -1;
		pid_t p = bas_spawn (path, cmd + i, fd[1], 1);
		close (fd[1]);
		if (p <= 0) { close (fd[0]); return -1; }
		fcntl (fd[0], F_SETFL, O_NONBLOCK);
		char b[256]; int st = 0;
		for (;;)
		{
			int r = (int) read (fd[0], b, sizeof b);
			if (r > 0) { out (b, r); continue; }
			if (r == 0) break;
			pump (); usleep (5000);
		}
		close (fd[0]);
		waitpid (p, &st, 0);
		return WEXITSTATUS (st);
	}

	// ---- files ---------------------------------------------------------------------------------------------
	char *load (const char *path, int *len) override { char q[256]; bas_path (q, sizeof q, path); return bas_load (q, len); }
	bool save (const char *path, const char *d, int n) override { char q[256]; bas_path (q, sizeof q, path); return bas_save (q, d, n); }
	bool remove (const char *p) override { char q[256]; bas_path (q, sizeof q, p); return unlink (q) == 0 || rmdir (q) == 0; }
	bool rename (const char *a, const char *b) override { char q[256], r[256]; bas_path (q, sizeof q, a); bas_path (r, sizeof r, b); return ::rename (q, r) == 0; }
	bool makeDir (const char *p) override { char q[256]; bas_path (q, sizeof q, p); return mkdir (q, 0755) == 0; }
	bool exists (const char *p) override { char q[256]; bas_path (q, sizeof q, p); struct stat s; return stat (q, &s) == 0; }
	int listDir (const char *dir, int index, char *o, int cap) override
	{
		char q[256]; bas_path (q, sizeof q, dir[0] ? dir : ".");
		return bas_list_dir (q, index, o, cap);
	}
	void *const *kitOpen (const char *name, int minVersion, char *why, int cap) override { return bk_open (name, minVersion, why, cap); }
	const char *command () override { return args; }
};

static bool bk_openUi () { return g_host->uiReady (); }
static void bk_pad_event (long t)
{
	if (t == WEV_PAD) g_host->padEvent ();
	else if (t == WEV_FOCUS && !ui_ev_field (&ui, EV_KIND)) g_host->padsOff ();
}
static void sk_notify (const char *t, const char *m) { g_host->notify (t, m); }

void ArmHost::pumpEvents ()
{
	if (!uiOpen || !view) return;
	keepPlaying ();
	for (;;)
	{
		long t = ui_event (0);
		if (t <= 0) { ui_w_idle (); break; }
		if (t == WEV_CLOSE) { closed = true; continue; }
		if (t == WEV_MENU) { pushEvent (ui_ev_field (&ui, EV_CODE)); continue; }
		if (t == WEV_PAD) { padEvent (); continue; }
		if (t == WEV_FOCUS && !ui_ev_field (&ui, EV_KIND)) padsOff ();
		int kind = ui_ev_field (&ui, EV_KIND), code = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR), mods = ui_ev_field (&ui, EV_MODS);
		if (t == WEV_KEY && code < 256 && !kind) heldKey[code] = 0;
		long wcode = 0, taken = 0, wd = uikit_w_event (&wcode, &taken);
		if (wd && wcode)
			for (int i = 1; i <= nctl; i++) if (ctl[i] == wd) { pushEvent (i); break; }
		if (t == WEV_KEY && kind && !taken)
		{
			long id = ui_menu_key (0);
			if (id > 0) { pushEvent ((int) id); continue; }	// (no menu at all: 0)
			int k = hid_key (code);
			if (!k && ch > 0 && ch < 256) k = (mods & UI_MOD_CTRL) && (ch | 32) >= 'a' && (ch | 32) <= 'z' ? (ch & 31) : ch;
			if (k)
			{
				if (code < 256) { heldKey[code] = 1; heldCode[code] = k >= 'A' && k <= 'Z' ? k + 32 : k; }
				pushKey (k);
			}
		}
		else if (t == WEV_MOUSE)
		{
			int x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y), b = code & 7;
			if (fsDlg) setMouse ((x - fsOx) * W / fsW, (y - fsOy) * H / fsH, b);
			else setMouse (x / sx, y / sy, b);
		}
	}
}

// A standalone app (Make App): this executable is the runtime with a compiled program after it
// (bas::attachBax). Its bytes (new[]), or 0: the plain runtime.
static char *attached_program (const char *self, int *len)
{
	int f = open (self, O_RDONLY);
	if (f < 0) return 0;
	unsigned size = (unsigned) lseek (f, 0, SEEK_END), off = 0, l = 0;
	char tail[bas::BAX_TRAILER]; char *b = 0;
	if (size > bas::BAX_TRAILER && lseek (f, size - bas::BAX_TRAILER, SEEK_SET) >= 0 && read (f, tail, sizeof tail) == (int) sizeof tail
	    && bas::attachedBax (tail, size, &off, &l) && lseek (f, off, SEEK_SET) >= 0)
	{
		b = new char[l + 1];
		if (read (f, b, l) != (int) l) { delete [] b; b = 0; }
	}
	close (f);
	if (b) *len = (int) l;
	return b;
}

// An app started by the system has no argv[0]: its process has the name of its folder (SYS_PROCLIST) -> its main.
static bool app_main_path (char *o, int cap)
{
	static char list[64 * 256];
	long n = bas_svc (0x15, (long) list, 256);
	for (long i = 0; i < n; i++)
	{
		const char *e = list + i * 64;
		if (*(const long *) e != (long) getpid ()) continue;
		snprintf (o, cap, "SD0:/apps/%.32s.app/main", e + 24);
		return true;
	}
	return false;
}

// basic -u: every standalone app of the card (SD0:/apps/<name>.app/main with a program attached) given THIS
// runtime in the place of the one it was made with: a standalone app keeps the runtime of the day it was made.
static int update_standalone (void)
{
	int rlen = 0;
	char *rt = bas_load (__posix_exe_path (), &rlen);
	if (!rt) { fputs ("basic -u: cannot read the runtime\n", stderr); return 1; }
	DIR *d = opendir ("SD0:/apps");
	struct dirent *e;
	int done = 0, failed = 0;
	while (d && (e = readdir (d)))
	{
		char path[320];
		snprintf (path, sizeof path, "SD0:/apps/%s/main", e->d_name);
		int len = 0;
		if (strcmp (path + strlen (path) - 9, ".app/main")) continue;
		int f = open (path, O_RDONLY);
		if (f < 0) continue;
		unsigned size = (unsigned) lseek (f, 0, SEEK_END), off = 0, bl = 0; char tail[bas::BAX_TRAILER];
		bool is = size > bas::BAX_TRAILER && lseek (f, size - bas::BAX_TRAILER, SEEK_SET) >= 0 && read (f, tail, sizeof tail) == (int) sizeof tail && bas::attachedBax (tail, size, &off, &bl);
		close (f);
		if (!is) continue;
		char *old = bas_load (path, &len);
		if (!old) continue;
		char *out = 0;
		int n = bas::attachBax (rt, rlen, old + off, (int) bl, &out);
		bool ok = bas_save (path, out, n);
		printf ("%s: %s\n", path, ok ? "updated" : "CANNOT BE WRITTEN");
		if (ok) done++; else failed++;
		delete [] out; delete [] old;
	}
	if (d) closedir (d);
	delete [] rt;
	if (!done && !failed) puts ("basic -u: no standalone app on the card");
	return failed ? 1 : 0;
}

int main (int argc, char **argv)
{
	int a = 1, ide = -1;
	bool compileOnly = false, prof = false, managed = false, text = false, gpuDry = false;
	const char *cwd = 0;
	static char path[256];
	int alen = 0;
	char *attached = attached_program (__posix_exe_path (), &alen);	// (then: no options, every argument is the program's)
	if (attached) bas_cpy (path, __posix_exe_path (), sizeof path);
	else if (argc < 2 && app_main_path (path, sizeof path) && !(attached = attached_program (path, &alen))) path[0] = 0;
	else
	{
		for (; a < argc && argv[a][0] == '-'; a++)
		{
			const char *o = argv[a];
			if (!strcmp (o, "-u")) return update_standalone ();
			else if (!strcmp (o, "-c")) compileOnly = true;
			else if (!strcmp (o, "-p")) prof = true;
			else if (!strcmp (o, "-m")) managed = true;
			else if (!strcmp (o, "-t")) text = true;
			else if (!strcmp (o, "-g")) gpuDry = true;
			else if (!strcmp (o, "-i") && a + 1 < argc) ide = atoi (argv[++a]);
			else if (!strcmp (o, "-d") && a + 1 < argc) cwd = argv[++a];
			else break;
		}
		if (a < argc) bas_path (path, sizeof path, argv[a++]);
	}

	static ArmHost host;
	g_host = &host;
	host.gpuDry = gpuDry;
	host.console = text || (ide < 0 && ((bas_svc (0x16, 0, 0) & 2) || (!isatty (1) && fcntl (1, F_GETFD) >= 0)));	// (SYS_PROC_FLAGS: a terminal's programs write in a pipe; an app has the system's console)
	{ int n = 0; for (; a < argc; a++) n += snprintf (host.args + n, sizeof host.args - n, "%s%s", n ? " " : "", argv[a]); }
	if (!path[0])
	{
		puts ("usage: basic <program.bas | program.bax> [arguments]   basic -c <program.bas>");
		return 1;
	}

	// Window title: the app's name (apps/<name>.app/main) or the file's
	{
		int e = bas_len (path), s = e; while (s > 0 && path[s - 1] != '/' && path[s - 1] != ':') s--;
		const char *base = path + s;
		if (!strncmp (base, "main", 4) && s >= 5 && path[s - 5] == '.')
		{
			int ps = s - 1; while (ps > 0 && path[ps - 1] != '/' && path[ps - 1] != ':') ps--;
			int k = 0; for (int j = ps; j < s - 5 && k < 63; j++) host.title[k++] = path[j];
			host.title[k] = 0;
		}
		else bas_cpy (host.title, base, sizeof host.title);
	}

	int len = alen;
	char *src = attached ? attached : bas_load (path, &len);
	if (!src)
	{
		fprintf (stderr, "basic: cannot read %s\n", path);
		return 1;
	}
	// The program's folder becomes the current directory (relative file names)
	if (compileOnly) {}
	else if (cwd) chdir (cwd);
	else
	{
		char dir[256]; bas_cpy (dir, path, sizeof dir);
		int e = bas_len (dir); while (e > 0 && dir[e - 1] != '/') e--;
		if (e > 0) { dir[e - 1 > 0 && dir[e - 2] != ':' ? e - 1 : e] = 0; chdir (dir); }
	}

	bas::Error err;
	bas::setKitSource (bas_kit_source);
	bas::Program *prog = bas::load (src, len, &err);	// a .bax runs as it is
	delete [] src;
	if (compileOnly)					// basic -c prog.bas: prog.bax
	{
		if (!prog) { printf ("Syntax error in line %d: %s\n", err.line, err.msg); return 2; }
		char out[260]; bas_cpy (out, path, sizeof out);
		int e = bas_len (out), d = e; while (d > 0 && out[d - 1] != '.' && out[d - 1] != '/') d--;
		if (d > 0 && out[d - 1] == '.') e = d - 1;
		bas_cpy (out + e, ".bax", sizeof out - e);
		char *bytes; int n = bas::saveBax (prog, &bytes);
		bool ok = bas_save (out, bytes, n);
		delete [] bytes; bas::destroy (prog);
		printf ("%s%s\n", ok ? "compiled: " : "cannot write ", out);
		return ok ? 0 : 1;
	}
	auto report = [&] (const char *kind)
	{
		char msg[200];
		snprintf (msg, sizeof msg, "%s%d: %s", kind, err.line, err.msg);
		if (ide >= 0)						// tell the editor: line \0 message
		{
			char m[180]; int q = bas::formatNum (err.line, m); m[q++] = 0;
			for (int j = 0; err.msg[j] && q < 178; j++) m[q++] = err.msg[j];
			m[q++] = 0;
			write (ide, m, q);
		}
		if (host.fsDlg) host.fullscreen (false);
		if (host.view) { bas_utf8 t (msg); ui_message ((long) "BASIC", (long) t.s, UM_OK); }
		else if (host.console || ide < 0) fprintf (stderr, "%s\n", msg);
	};
	if (!prog) { report ("Syntax error in line "); return 2; }
	if (prof) host.prof = &host.profile;
	if (managed) host.managed = true;
	int r = bas::run (prog, host, &err);
	if (prof && host.console) { char t[320]; host.profile.text (t, sizeof t); puts (t); }
	if (r) report ("Error in line ");
	bas::destroy (prog);
	return r ? 3 : 0;
}
