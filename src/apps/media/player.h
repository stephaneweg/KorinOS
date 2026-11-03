//
// player.h -- Media Player's music: AudioKit plays it (ak_play: its decoders -- MP3, Ogg Vorbis, Opus, FLAC, WAV, MIDI, FM
// Songs --, its thread, its stream). The window asks (play, pause, seek, stop) and, at each of its turns, reads the state
// (poll). Opening may be long (a MIDI file's SoundFont: 31 MB), and stopping waits for AudioKit's thread: a small thread
// of ours does both, the window never waits.
//
#ifndef _media_player_h
#define _media_player_h

#include "sys.h"

namespace media {

enum { PS_STOPPED, PS_LOADING, PS_PLAYING, PS_PAUSED };

class Player
{
public:
	int state;				// PS_*
	i64 posMs, lenMs;			// what is heard now, the song's length
	int endedGen;				// bumped when a song ends by itself (the window plays the next)
	int errGen; char err[160];		// bumped with an error (err: why)
	char fmt[8]; int rate, bits, kbps, channels;	// the song playing (its description)
	char sfName[64];			// a MIDI file: what plays it (AudioKit's, once it has loaded it)
	int volume;				// 0..100

	Player () : state (PS_STOPPED), posMs (0), lenMs (0), endedGen (0), errGen (0), rate (0), bits (0), kbps (0), channels (0), volume (80),
		m_cur (0), m_gen (0), m_wantGen (0), m_opened (0), m_openedGen (0), m_openErr (0), m_stopH (0), m_quit (false), m_have (false)
	{ err[0] = fmt[0] = sfName[0] = 0; m_path[0] = 0; m_seen = -1; pthread_mutex_init (&m_lk, 0); }

	void start () { m_have = pthread_create (&m_tid, 0, thread_main, this) == 0; }
	void quit () { stop (); m_quit = true; if (m_have) pthread_join (m_tid, 0); m_have = false; }

	void play (const char *path)
	{
		drop ();
		pthread_mutex_lock (&m_lk); scopy (m_path, path, sizeof m_path); m_wantGen++; pthread_mutex_unlock (&m_lk);
		state = PS_LOADING; posMs = 0; lenMs = 0;
	}
	void pause () { if (state == PS_PLAYING && m_cur) { ak_play_pause (m_cur, 1); state = PS_PAUSED; } }
	void resume () { if (state == PS_PAUSED && m_cur) { ak_play_pause (m_cur, 0); state = PS_PLAYING; } }
	void stop () { drop (); pthread_mutex_lock (&m_lk); m_wantGen++; m_path[0] = 0; pthread_mutex_unlock (&m_lk); state = PS_STOPPED; posMs = 0; }
	void seek (i64 ms) { if (m_cur) { ak_play_seek (m_cur, ms < 0 ? 0 : ms); posMs = ms < 0 ? 0 : ms; m_seekT = now_ms (); } }
	void set_volume (int v) { volume = v; if (m_cur) ak_play_volume (m_cur, v * v * 256 / 10000); }	// (a square law: the ear's)

	// the window's turn
	void poll ()
	{
		pthread_mutex_lock (&m_lk);
		long h = 0; bool bad = false;
		if (m_openedGen == m_wantGen && m_opened) { h = m_opened; m_opened = 0; }
		else if (m_opened) { if (!m_stopH) { m_stopH = m_opened; m_opened = 0; } }	// (asked for something else meanwhile)
		if (m_openedGen == m_wantGen && m_openErr && m_seen != m_openedGen) { bad = true; m_seen = m_openedGen; }
		pthread_mutex_unlock (&m_lk);
		if (h)
		{
			m_cur = h; state = PS_PLAYING; m_seekT = 0;
			lenMs = m_len; rate = m_rate; bits = m_bits; kbps = m_kbps; channels = m_chans; memcpy (fmt, m_fmt, sizeof fmt);
			set_volume (volume);
		}
		if (bad)
		{
			snprintf (err, sizeof err, "%s", (const char *) ui_tr ((long) (m_openErr == -2 ? "The file cannot be opened." : "This file cannot be played (no sound output, or a file that is damaged).")));
			errGen++; state = PS_STOPPED;
		}
		if (!m_cur) return;
		long st = ak_play_state (m_cur);
		if (st == AKP_ENDED) { drop (); posMs = lenMs; state = PS_STOPPED; endedGen++; return; }
		if (st == AKP_ERROR) { drop (); snprintf (err, sizeof err, "%s", (const char *) ui_tr ((long) "The song stopped: its data could not be read.")); errGen++; state = PS_STOPPED; return; }
		if (now_ms () - m_seekT > 300) posMs = ak_play_pos (m_cur);		// (just after a seek: where it was asked)
	}

private:
	long m_cur;				// the window's: the player of ak_play
	int m_gen, m_wantGen, m_seen;
	long m_opened; int m_openedGen; long m_openErr; long m_stopH;
	i64 m_seekT, m_len; int m_rate, m_bits, m_kbps, m_chans; char m_fmt[8];
	char m_path[300];
	pthread_mutex_t m_lk; pthread_t m_tid; volatile bool m_quit; bool m_have;

	// the song playing, given to the thread to stop
	void drop ()
	{
		if (!m_cur) return;
		ak_play_pause (m_cur, 1);
		for (;;)
		{
			pthread_mutex_lock (&m_lk);
			bool free_ = !m_stopH; if (free_) m_stopH = m_cur;
			pthread_mutex_unlock (&m_lk);
			if (free_) break;
			sleep_ms (2);
		}
		m_cur = 0;
	}
	static void *thread_main (void *p) { ((Player *) p)->run (); return 0; }
	void run ()
	{
		while (!m_quit || m_stopH)
		{
			pthread_mutex_lock (&m_lk);
			long stopH = m_stopH; m_stopH = 0;
			int want = m_wantGen; char path[300]; memcpy (path, m_path, sizeof path);
			pthread_mutex_unlock (&m_lk);
			if (stopH) { ak_play_stop (stopH); continue; }
			if (want == m_gen) { sleep_ms (10); continue; }
			m_gen = want;
			if (!path[0]) continue;
			long h = ak_play ((long) path, 0);
			if (h > 0)
			{
				long in[AKI_SIZE / 8];
				static const char *const NAMES[] = { "", "WAV", "FLAC", "MP3", "OGG", "OPUS", "MIDI", "FMS" };
				if (ak_dec_info (ak_play_decoder (h), (long) in) == 0)
				{
					int f = (int) in[AKI_FORMAT / 8];
					scopy (m_fmt, f >= 1 && f <= 7 ? NAMES[f] : "", sizeof m_fmt);
					m_rate = (int) in[AKI_RATE / 8]; m_chans = (int) in[AKI_CHANNELS / 8]; m_bits = (int) in[AKI_BITS / 8];
					m_kbps = (int) (in[AKI_BITRATE / 8] / 1000); m_len = in[AKI_DURATION / 8];
					if (f == AKF_MIDI && !sfName[0])
					{
						long sf = ak_sf_default ();		// (ak_play has loaded it: at once)
						const char *n = sf ? (const char *) ak_sf_name (sf) : 0;
						scopy (sfName, n && n[0] ? n : sf ? "SoundFont" : "FM", sizeof sfName);
					}
				}
			}
			pthread_mutex_lock (&m_lk);
			m_openedGen = want;
			if (h > 0) { m_opened = h; m_openErr = 0; } else m_openErr = h ? h : -1;
			pthread_mutex_unlock (&m_lk);
		}
	}
};

} // namespace media

#endif
