//
// watch.h -- a video playing: a thread of its own (the loop of the assembly player, src/apps/mediaplayer, docs/MEDIAKIT.md)
// reads MediaKit, keeps AudioKit's stream full, and when the clock reaches a frame converts it straight into the window's
// attached surface (no copy). The clock is the sound's (first packet's pts + frames played), the system's without
// sound. A frame more than 150 ms late is skipped (4 in a row at most). Pause: the stream closed, the clock stopped;
// going on: a new stream, the clock from the first packet read. The window's thread alone calls UIKit: it moves the
// surface (under `lk`) and signals the frames (frame_shown).
//
#ifndef _media_watch_h
#define _media_watch_h

#include "videos.h"

namespace media {

class VideoPlay
{
public:
	struct mk_info in;
	volatile int paused, ended, firstShown;		// the status
	volatile i64 posUs;				// the clock

	VideoPlay () : paused (0), ended (0), firstShown (0), posUs (0), m_h (0), m_on (false), m_surf (0), m_px (0), m_pitch (0), m_sw (0), m_sh (0)
	{ memset (&in, 0, sizeof in); pthread_mutex_init (&m_lk, 0); }
	~VideoPlay () { close (); }

	bool active () const { return m_on; }
	long long pos_ms () const { return posUs > 0 ? posUs / 1000 : 0; }
	long long len_ms () const { return in.duration > 0 ? in.duration / 1000 : 0; }

	// -> 0, or MK_E_* (the file cannot be read, not a container MediaKit knows, MediaKit absent)
	int open (const char *path, int startMs, int volume, bool muted)
	{
		close ();
		if (!mediakit_kit_open ()) return MK_E_NOENT;
		m_h = mk_open ((long) path);
		if (m_h <= 0) { int r = (int) m_h; m_h = 0; return r ? r : MK_E_BADMSG; }
		if (mk_info (m_h, (long) &in) != 0 || !in.vok) { mk_close (m_h); m_h = 0; return MK_E_UNSUP; }
		m_quit = 0; m_cmdPause = 0; m_seekUs = startMs > 0 ? (i64) startMs * 1000 : -1; m_redraw = 0; m_frame = 0;
		paused = 0; ended = 0; firstShown = 0; posUs = (i64) startMs * 1000;
		set_volume (volume, muted);
		m_on = pthread_create (&m_tid, 0, thread_main, this) == 0;
		if (!m_on) { mk_close (m_h); m_h = 0; return MK_E_NOMEM; }
		return 0;
	}
	void close ()
	{
		if (m_on) { m_quit = 1; pthread_join (m_tid, 0); m_on = false; }
		if (m_h) mk_close (m_h);
		m_h = 0; memset (&in, 0, sizeof in); paused = 0; ended = 0; posUs = 0;
	}
	void set_volume (int v, bool muted) { m_vol = muted ? 0 : v * v * 256 / 10000; }
	void play () { m_cmdPause = 0; }
	void pause () { m_cmdPause = 1; }
	void seek (long long ms) { if (in.duration > 0 && ms * 1000 > in.duration) ms = in.duration / 1000; m_seekUs = (i64) (ms < 0 ? 0 : ms) * 1000; posUs = m_seekUs; }

	// ---- the surface (the window's thread) ---------------------------------------------------------------
	// the picture fitted into (x, y, w, h) of the current window, centred; x, y, w, h: its place. The first call
	// makes the surface, the next ones move it.
	bool place (int zx, int zy, int zw, int zh)
	{
		if (in.width <= 0 || in.height <= 0 || zw < 2 || zh < 2) return false;
		int sw = zw, sh = (int) ((long long) zw * in.height / in.width);
		if (sh > zh) { sh = zh; sw = (int) ((long long) zh * in.width / in.height); }
		sw &= ~1; sh &= ~1; if (sw < 2) sw = 2; if (sh < 2) sh = 2;
		int x = zx + (zw - sw) / 2, y = zy + (zh - sh) / 2;
		pthread_mutex_lock (&m_lk);
		if (m_surf) ui_surface_move (m_surf, x, y, sw, sh);
		else { m_surf = ui_surface (x, y, sw, sh, WS_SF_XRGB8888); }
		if (m_surf)
		{
			long pitch = 0;
			long *p = (long *) uikit_call3 ((long (*) ()) ui_surface_pixels, m_surf, 0, 0, 0, &pitch, 0);
			m_px = (uint32_t *) p; m_pitch = pitch; m_sw = sw; m_sh = sh;
		}
		pthread_mutex_unlock (&m_lk);
		m_redraw = 1;
		return m_surf != 0;
	}
	void free_surface ()
	{
		pthread_mutex_lock (&m_lk);
		if (m_surf) ui_surface_free (m_surf);
		m_surf = 0; m_px = 0;
		pthread_mutex_unlock (&m_lk);
	}
	// a frame was put in the surface: Elegant told -> true if there was one
	bool frame_shown ()
	{
		if (!m_frame) return false;
		m_frame = 0;
		pthread_mutex_lock (&m_lk);
		if (m_surf) ui_surface_damage (m_surf, 0, 0, 0, 0);
		pthread_mutex_unlock (&m_lk);
		return true;
	}

private:
	enum { CHUNK = 1024, LATE_US = 150000, MAX_DROP = 4, STALL_MS = 1500, TAIL_BYTES = 16384 };
	long m_h; bool m_on; pthread_t m_tid;
	volatile int m_quit, m_cmdPause, m_redraw, m_frame, m_vol;
	volatile i64 m_seekUs;
	pthread_mutex_t m_lk;
	long m_surf; uint32_t *m_px; long m_pitch; int m_sw, m_sh;
	// (the thread's)
	int m_shown, m_dropped;
	long m_stream; bool m_aFirst, m_aEof; i64 m_aBase, m_lastPos, m_lastMs, m_clkBase, m_clkT0; bool m_soundOk; int m_volSet;

	static void *thread_main (void *p) { ((VideoPlay *) p)->run (); return 0; }
	void stream_open ()
	{
		m_stream = -1; m_aFirst = true; m_lastPos = 0; m_lastMs = 0;
		if (!m_soundOk) return;
		long s = ak_open (SOUND_RATE, 2, 16, 0);
		if (s >= 0) { m_stream = s; m_volSet = -1; }
	}
	void stream_close () { if (m_stream >= 0) ak_close (m_stream); m_stream = -1; }
	i64 sound_time (i64 pos) { return m_aBase + pos * 1000000 / SOUND_RATE; }
	i64 clock_now (bool running)
	{
		if (!running) return m_clkBase;
		if (m_stream >= 0 && !m_aFirst)
		{
			i64 pos = ak_position (m_stream), now = now_ms ();
			if (pos != m_lastPos) { m_lastPos = pos; m_lastMs = now; return sound_time (pos); }
			if (m_aEof) return sound_time (pos);
			if (!m_lastMs) { m_lastMs = now; return sound_time (pos); }
			if (now - m_lastMs < STALL_MS) return sound_time (pos);
			m_clkBase = sound_time (pos); m_clkT0 = now;		// (the sound does not move: the system's clock)
			stream_close ();
		}
		return m_clkBase + (now_ms () - m_clkT0) * 1000;
	}
	void convert ()
	{
		pthread_mutex_lock (&m_lk);
		if (m_px && mk_video_convert (m_h, (long) m_px, m_pitch, m_sw, m_sh) == 0) { m_frame = 1; firstShown = 1; }
		pthread_mutex_unlock (&m_lk);
	}
	void run ()
	{
		static short abuf[2 * CHUNK];
		struct mk_frame fr;
		i64 t0 = now_ms (); m_shown = m_dropped = 0;
		bool pending = false, vEof = false, running = true, showOne = true; int dropRun = 0; i64 aPts = 0;
		m_soundOk = in.atrack >= 0 && in.aok && mk_audio_open (m_h, SOUND_RATE) == 0;
		m_aEof = !m_soundOk; m_clkBase = 0; m_clkT0 = now_ms (); m_stream = -1; m_aFirst = true; m_aBase = 0;
		stream_open ();
		while (!m_quit)
		{
			bool busy = false;
			i64 sk = m_seekUs;
			if (sk >= 0)
			{
				m_seekUs = -1;
				long r = mk_seek (m_h, sk);
				stream_close ();
				pending = false; vEof = false; m_aEof = !m_soundOk; showOne = true; dropRun = 0; ended = 0;
				m_clkBase = r >= 0 ? r : 0; m_clkT0 = now_ms ();
				posUs = m_clkBase;
				if (running) stream_open ();
			}
			if (m_cmdPause && running && !ended) { m_clkBase = clock_now (true); running = false; stream_close (); paused = 1; }
			if (!m_cmdPause && !running) { running = true; m_clkT0 = now_ms (); paused = 0; stream_open (); }
			if (m_redraw) { m_redraw = 0; if (firstShown) convert (); }
			if (ended || (!running && !showOne)) { sleep_ms (10); continue; }
			// the sound
			if (running && m_stream >= 0)
			{
				if (m_volSet != m_vol) { m_volSet = m_vol; ak_volume (m_stream, m_volSet); }
				while (!m_aEof && m_stream >= 0 && ak_space (m_stream) >= CHUNK * 4)
				{
					long n = mk_audio_read (m_h, (long) abuf, CHUNK, (long) &aPts);
					if (n <= 0) { m_aEof = true; break; }
					if (m_aFirst) { m_aFirst = false; m_aBase = aPts; }
					ak_write (m_stream, (long) abuf, n * 4);
					busy = true;
				}
			}
			// the frame due
			for (int k = 0; k < 8 && !vEof; k++)
			{
				if (!pending)
				{
					if (mk_video_next (m_h, (long) &fr) != 1) { vEof = true; break; }
					pending = true; busy = true;
				}
				i64 d = clock_now (running) - fr.pts;
				if (!showOne && d < 0) break;
				if (!showOne && d >= LATE_US && dropRun < MAX_DROP) { dropRun++; pending = false; m_dropped++; continue; }
				convert (); dropRun = 0; pending = false; showOne = false; m_shown++;
				if (!running) break;
			}
			if (running) posUs = clock_now (true);
			// the end: the frames gone and the sound played
			if (running && vEof)
			{
				bool done = m_stream < 0 || (m_aEof && (ak_pending (m_stream) <= TAIL_BYTES || (in.duration > 0 && posUs >= in.duration)));
				if (m_stream < 0 && in.duration > 0 && posUs < in.duration && m_soundOk == false) done = true;
				if (done) { stream_close (); m_clkBase = posUs; ended = 1; }
			}
			if (!busy) sleep_ms (3);
		}
		stream_close ();
		// (what the last video cost: how a film is judged on the Pi, which has no console)
		struct mk_stats ms; int ms_t = (int) (now_ms () - t0);
		if (mk_stats (m_h, (long) &ms) == 0)
			if (FILE *rf = fopen ("SD0:/home/media/lastvideo.txt", "w"))
			{
				fprintf (rf, "%d images shown, %d skipped in %d ms: %d.%d images/s; decoding %d us an image; %s\n", m_shown, m_dropped, ms_t, m_shown * 1000 / (ms_t ? ms_t : 1),
					 m_shown * 10000 / (ms_t ? ms_t : 1) % 10, ms.vframes ? (int) (ms.vdecode_us / ms.vframes) : 0, m_soundOk ? "sound" : "no sound");
				fclose (rf);
			}
	}
};

} // namespace media

#endif
