//
// ui/audio.h -- Koton's sound on ARMPI: the SoundFont (found on the card, else AudioKit's FM bank), the engine
// (engine/engine.h) and where it runs: a thread of the process (pthread, high priority) renders 256-frame blocks and
// writes them to an AudioKit stream (ak_write, a shared ring of the service "sound") as long as less than the target
// is queued, then sleeps a little. No kernel ring, no app core: those are Onyx's.
//
// The song is compiled on the UI thread when the document changed (compileSong is fast: a few ms
// for a song of a few minutes) and handed to the engine, which swaps it at a block boundary and
// hands the old one back (retired) to be freed here.
//
#ifndef _koton_audio_h
#define _koton_audio_h
#include "audiokit_abi.h"
#include "../engine/audioutil.h"
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include "engine/engine.h"
#include "ui/doc.h"

namespace kui {

enum { AUDIO_BLOCK = 256, AUDIO_TARGET = 1536 };	// frames kept queued ahead of the output (35 ms)

struct AudioHost
{
	Engine engine;
	volatile long sf;			// ak_sf_*, SF_FM: the kit's FM bank, 0: not there yet
	char sfPath[256], sfName[64], status[160];
	long stream;				// the AudioKit stream, < 0 none
	pthread_t thread;
	bool haveThread, running;
	volatile int stop, stopped;
	unsigned compiledRev;			// the document revision the engine plays
	float bufL[AUDIO_BLOCK], bufR[AUDIO_BLOCK];
	short pcm[AUDIO_BLOCK * 2];
	volatile unsigned long long busyUs, spanUs;	// the engine's load (core loop)
	volatile unsigned underruns;

	AudioHost () : sf (0), haveLoader (false), stream (-1), haveThread (false), running (false), stop (0), stopped (1), compiledRev (0), busyUs (0), spanUs (1), underruns (0)
	{ sfPath[0] = sfName[0] = 0; snprintf (status, sizeof status, "no sound yet"); }

	// ---- the SoundFont --------------------------------------------------------------------------------------------
	bool findSoundFont (const char *preferred)			{ return soundfont_find (preferred, sfPath, sizeof sfPath) != 0; }
	long loadSoundFont ()
	{
		long f = ak_sf_load ((long) sfPath);
		if (f <= 0) { snprintf (status, sizeof status, TR ("cannot read %s"), sfPath); return 0; }
		const char *name = (const char *) ak_sf_name (f);
		snprintf (sfName, sizeof sfName, "%s", name ? name : "");
		// its names: the GM programs (bank 0) and the drum kits (bank 128), by patch
		g_kitPrograms.clear (); g_names.nKits = 0;
		int cnt = (int) ak_sf_presets (f);
		for (int patch = 0; patch < 128; patch++)
			for (int i = 0; i < cnt; i++)
			{
				unsigned char p[AKSP_SIZE];
				if (ak_sf_preset (f, i, (long) p) < 0) continue;
				int b = *(int *) (p + AKSP_BANK), pt = *(int *) (p + AKSP_PROGRAM);
				const char *name = (const char *) p + AKSP_NAME;
				if (b == 0 && pt == patch) snprintf (g_names.gm[patch], sizeof g_names.gm[patch], "%s", name);
				if (b == 128 && pt == patch && g_names.nKits < 64)
				{
					snprintf (g_names.kit[g_names.nKits], sizeof g_names.kit[0], "%s", name);
					g_names.nKits++;
					g_kitPrograms.push (patch);
				}
			}
		return f;
	}

	// ---- starting ---------------------------------------------------------------------------------------------------------
	// The SoundFont (31 MB) is read by a thread: the window is up meanwhile, the sound comes when it is ready (sf
	// stays 0 until then: every call below asks it). The status bar says so.
	char prefer[256];
	pthread_t loader;
	bool haveLoader;
	void start (const char *preferredSf)
	{
		snprintf (prefer, sizeof prefer, "%s", preferredSf ? preferredSf : "");
		snprintf (status, sizeof status, "loading the SoundFont...");
		audiokit_kit_open ();
		haveLoader = pthread_create (&loader, 0, loadLoop, this) == 0;
		if (!haveLoader) loadLoop (this);
	}
	static void *loadLoop (void *arg) { ((AudioHost *) arg)->load (); return 0; }
	void load ()
	{
		unsigned t0 = ticks ();
		long found = findSoundFont (prefer) ? loadSoundFont () : 0;
		if (!found) { found = SF_FM; snprintf (sfName, sizeof sfName, "FM bank (no SoundFont)"); }	// (no .sf2: AudioKit's General MIDI in FM)
		fprintf (stderr, "koton: %s in %u ms\n", sfName, (ticks () - t0) * 10);
		engine.init (found, SOUND_RATE);
		engine.ensureTracks (8);
		stream = ak_open (SOUND_RATE, 2, 16, 0);
		if (stream < 0) { snprintf (status, sizeof status, "no sound output"); sf = found; return; }
		ak_latency (stream, AUDIO_TARGET * 4);
		stop = 0; stopped = 0;
		pthread_attr_t at; struct sched_param sp;
		pthread_attr_init (&at);
		pthread_attr_setstacksize (&at, 256 * 1024);
		pthread_attr_setinheritsched (&at, PTHREAD_EXPLICIT_SCHED);
		pthread_attr_setschedpolicy (&at, SCHED_FIFO);			// (libposix: ARMPI's high priority)
		sp.sched_priority = 1;
		pthread_attr_setschedparam (&at, &sp);
		haveThread = pthread_create (&thread, &at, threadLoop, this) == 0;
		pthread_attr_destroy (&at);
		if (!haveThread) { ak_close (stream); stream = -1; snprintf (status, sizeof status, "no audio thread"); sf = found; return; }
		snprintf (status, sizeof status, TR ("%s - engine on a thread"), sfName[0] ? sfName : "SoundFont");
		__sync_synchronize ();
		sf = found;
		running = true;
	}
	void shutdown ()
	{
		if (haveLoader) { pthread_join (loader, 0); haveLoader = false; }	// (a quick close: while it reads)
		if (!running) { freeSoundFont (); return; }
		stop = 1;
		if (haveThread) { pthread_join (thread, 0); haveThread = false; }
		if (stream >= 0) { ak_close (stream); stream = -1; }
		running = false;
		for (CompiledSong *s; (s = engine.retired ()); ) delete s;
		// (the engine is destroyed with the host; the SoundFont after it: see ~AudioHost)
	}
	~AudioHost () { }
	void freeSoundFont () { if (sf > 0) ak_sf_free (sf); sf = 0; }

	// ---- the render loop -----------------------------------------------------------------------------------------------
	static inline unsigned long long nowUs ()
	{
		unsigned long long c, f;
		__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (c));
		__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
		return f ? c * 1000000ull / f : 0;
	}
	void renderOne ()
	{
		unsigned long long t0 = nowUs ();
		engine.render (bufL, bufR, AUDIO_BLOCK);
		toS16 (bufL, bufR, pcm, AUDIO_BLOCK, 1.0f);
		unsigned long long t1 = nowUs ();
		if (engine.playing && ak_pending (stream) < AUDIO_BLOCK * 4) underruns = underruns + 1;	// (the block came late)
		const char *p = (const char *) pcm; long left = AUDIO_BLOCK * 4;
		while (left > 0 && !stop)
		{
			long n = ak_write (stream, (long) p, left);
			if (n > 0) { p += n; left -= n; } else if (n < 0) break; else usleep (1000);
		}
		busyUs = busyUs + (t1 - t0);
		engine.renderUs = (unsigned) (t1 - t0);
	}
	static void *threadLoop (void *arg)
	{
		AudioHost *h = (AudioHost *) arg;
		unsigned long long w0 = nowUs ();
		while (!h->stop)
		{
			if (ak_pending (h->stream) < AUDIO_TARGET * 4) h->renderOne ();
			else usleep (2000);
			unsigned long long now = nowUs ();
			if (now - w0 > 500000) { h->spanUs = now - w0; w0 = now; h->busyUs = 0; }
		}
		h->stopped = 1;
		return 0;
	}
	// the UI's tick: the retired songs freed
	void tick ()
	{
		for (CompiledSong *s; (s = engine.retired ()); ) delete s;
	}
	int loadPercent () const { return spanUs ? (int) (busyUs * 100 / spanUs) : 0; }
	int latencyMs () const { return AUDIO_TARGET * 1000 / SOUND_RATE; }

	// ---- the song ----------------------------------------------------------------------------------------------------
	// compile the document if it changed since, give it to the engine (the mixer follows the tracks)
	void sync (Doc &d)
	{
		if (!sf) return;
		for (int t = 0; t < d.p.tracks.size () && t < ENGINE_MAX_TRACKS; t++)
		{
			const Track &tr = d.p.tracks[t];
			engine.mix[t].volume = (float) tr.volume; engine.mix[t].pan = (float) tr.pan;
			engine.mix[t].mute = tr.mute; engine.mix[t].solo = tr.solo; engine.mix[t].reverb = tr.reverbSend ();
		}
		if (compiledRev == d.revision) return;
		CompiledSong *cs = compileSong (d.p, SOUND_RATE);
		engine.ensureTracks (cs->tracks.size ());
		engine.post (CMD_SONG, 0, 0, 0, 0, cs);
		compiledRev = d.revision;
	}
	void play (Doc &d, double beat)
	{
		if (!sf) return;
		sync (d);
		CompiledSong tmp; (void) tmp;
		long long s = engine.song () ? engine.song ()->sampleAtBeat (beat) : 0;
		// (the song just posted is not the engine's yet: compute from the document's tempo)
		if (!engine.song () || compiledRev != d.revision) s = (long long) (beatToSeconds (d.p, beat) * SOUND_RATE);
		engine.post (CMD_PLAY, s);
	}
	void stopPlay () { if (sf) engine.post (CMD_STOP); }
	void seek (Doc &d, double beat) { if (sf) engine.post (CMD_SEEK, (long long) (beatToSeconds (d.p, beat) * SOUND_RATE)); }
	void loop (Doc &d, bool on, double a, double b)
	{
		if (sf) engine.post (CMD_LOOP, (long long) (beatToSeconds (d.p, a) * SOUND_RATE), (long long) (beatToSeconds (d.p, b) * SOUND_RATE), on ? 1 : 0);
	}
	static double beatToSeconds (const Project &p, double beat)
	{
		// the tempo map, as the compiler builds it (slice by slice)
		double s = 0; int slices = (int) (beat * CSPB);
		for (int i = 0; i < slices; i++) s += 60.0 / p.bpmAt (i / (double) CSPB) / CSPB;
		s += (beat * CSPB - slices) * 60.0 / p.bpmAt (beat) / CSPB;
		return s;
	}
	double playheadBeat () const { const CompiledSong *s = engine.song (); return s ? s->beatAtSample (engine.position) : 0; }
	bool isPlaying () const { return engine.playing != 0; }

	// a preview (an editor's Listen): a module or a riff looping on the preview synth
	void preview (CompiledSong *cs) { if (sf) engine.post (CMD_PREVIEW, 0, 0, 0, 0, cs); else delete cs; }
	void stopPreview () { if (sf) engine.post (CMD_PREVIEW, 0, 0, 0, 0, 0); }
	void noteOn (int note, int vel, int program, bool drum) { if (sf) engine.post (CMD_NOTE_ON, 0, 0, note, vel, 0, program, drum ? 1 : 0); }
	void noteOff (int note) { if (sf) engine.post (CMD_NOTE_OFF, 0, 0, note); }
	void metronome (bool on) { if (sf) engine.post (CMD_METRONOME, 0, 0, on ? 1 : 0); }

	// ---- the WAV export: the song rendered off line by a second engine (the playing one untouched)
	bool exportWav (Doc &d, const char *path, void (*progress) (int pct))
	{
		if (!sf) return false;
		Engine *e = new Engine;
		e->init (sf, SOUND_RATE);
		CompiledSong *cs = compileSong (d.p, SOUND_RATE);
		e->ensureTracks (cs->tracks.size ());
		for (int t = 0; t < d.p.tracks.size () && t < ENGINE_MAX_TRACKS; t++)
		{
			e->mix[t].volume = (float) d.p.tracks[t].volume; e->mix[t].pan = (float) d.p.tracks[t].pan;
			e->mix[t].mute = d.p.tracks[t].mute; e->mix[t].solo = d.p.tracks[t].solo; e->mix[t].reverb = d.p.tracks[t].reverbSend ();
		}
		long long frames = cs->totalSamples () + 2 * SOUND_RATE;
		e->post (CMD_SONG, 0, 0, 0, 0, cs);
		e->post (CMD_PLAY, 0);
		wav_file *out = wav_begin (path, SOUND_RATE, 2, frames);
		if (!out) { delete e; return false; }
		enum { N = 4096 };
		float *L = new float[N], *R = new float[N]; short *s = new short[N * 2];
		int lastPct = -1;
		for (long long done = 0; done < frames; )
		{
			int n = frames - done > N ? N : (int) (frames - done);
			e->render (L, R, n);
			toS16 (L, R, s, n, 1.0f);
			wav_write (out, s, n);
			done += n;
			int pct = (int) (done * 100 / frames);
			if (progress && pct != lastPct) { lastPct = pct; progress (pct); }
		}
		wav_end (out);
		delete [] L; delete [] R; delete [] s;
		delete e;
		return true;
	}
};

extern AudioHost g_audio;

} // namespace kui

#endif
