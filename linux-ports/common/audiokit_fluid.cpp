//
// audiokit_fluid.cpp -- AudioKit (src/kits/audiokit, le kit du son de Korin) sur Linux : les fonctions dont Koton et
// Arabesque se servent, sur FluidSynth.
//
//   ak_sf_*           une SoundFont : son chemin, son nom, ses presets (lus une fois par un synthe d'ancrage, qui garde
//                     les echantillons dans le cache de FluidSynth : les synthes suivants ne relisent pas le disque)
//   ak_synth_*        un synthe = un fluid_synth_t (16 canaux, le canal 9 en batterie, reverberation et chorus a lui) ;
//                     ak_synth_new (0, ...) prend la SoundFont du systeme (kport_soundfont : FluidR3_GM par defaut)
//   ak_open/ak_write  un flux de sortie : un anneau s16 stereo que vide un pilote audio de FluidSynth
//                     (new_fluid_audio_driver2 : PipeWire, PulseAudio ou ALSA, $KORIN_AUDIO_DRIVER pour choisir)
//   ak_enc_*          l'export : WAV ; MP3 par libmp3lame.so.0 si elle est installee (chargee a la demande)
//
// Les poignees commencent a 1 (Koton veut > 0, Arabesque >= 0).
//
#include "korin_port.h"
#include <dlfcn.h>
#include <fluidsynth.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>
#include "audiokit_abi.h"

namespace {

enum { MAX_SF = 32, MAX_SYNTH = 256, MAX_STREAM = 16, MAX_ENC = 16, RATE = 44100 };

pthread_mutex_t g_lk = PTHREAD_MUTEX_INITIALIZER;

struct Preset { int bank, program; char name[24]; };
struct SoundFont
{
	bool used;
	std::string path, name;
	std::vector<Preset> presets;
	fluid_settings_t *anchorSet;
	fluid_synth_t *anchor;		// (garde les echantillons dans le cache de FluidSynth)
};
SoundFont g_sf[MAX_SF + 1];
long g_sysSf;				// la SoundFont du systeme (0 : pas encore chargee)

struct Synth
{
	bool used;
	long sf;			// (0 : celle du systeme, suivie quand elle change)
	fluid_settings_t *set;
	fluid_synth_t *syn;
	int sfid;
	std::vector<float> tl, tr;
};
Synth g_syn[MAX_SYNTH + 1];

float gain ()
{
	const char *e = getenv ("KORIN_FLUID_GAIN");
	float g = e ? (float) atof (e) : 0.5f;
	return g > 0 && g < 10 ? g : 0.5f;
}

void quiet_log (int level, const char *msg, void *)
{
	if (level <= FLUID_WARN) fprintf (stderr, "fluidsynth: %s\n", msg);
}
void init_log ()
{
	static bool done;
	if (done) return;
	done = true;
	fluid_set_log_function (FLUID_WARN, quiet_log, 0);
	fluid_set_log_function (FLUID_INFO, 0, 0);
	fluid_set_log_function (FLUID_DBG, 0, 0);
	if (!getenv ("KORIN_FLUID_VERBOSE")) fluid_set_log_function (FLUID_WARN, 0, 0);
}

fluid_settings_t *synth_settings (bool fx)
{
	fluid_settings_t *s = new_fluid_settings ();
	fluid_settings_setnum (s, "synth.sample-rate", RATE);
	fluid_settings_setnum (s, "synth.gain", gain ());
	fluid_settings_setint (s, "synth.polyphony", 256);
	fluid_settings_setint (s, "synth.reverb.active", fx ? 1 : 0);
	fluid_settings_setint (s, "synth.chorus.active", fx ? 1 : 0);
	fluid_settings_setint (s, "synth.cpu-cores", 1);
	return s;
}

long sf_load_locked (const char *path)
{
	init_log ();
	char tb[PATH_MAX];
	path = kport_path (path, tb, sizeof tb);
	if (!path || !fluid_is_soundfont (path)) return AKE_FORMAT;
	for (long i = 1; i <= MAX_SF; i++) if (g_sf[i].used && g_sf[i].path == path) return i;
	long h = 0;
	for (long i = 1; i <= MAX_SF && !h; i++) if (!g_sf[i].used) h = i;
	if (!h) return AKE_NOMEM;
	SoundFont &f = g_sf[h];
	f.anchorSet = synth_settings (false);
	f.anchor = new_fluid_synth (f.anchorSet);
	int id = f.anchor ? fluid_synth_sfload (f.anchor, path, 0) : FLUID_FAILED;
	if (id == FLUID_FAILED)
	{
		if (f.anchor) delete_fluid_synth (f.anchor);
		delete_fluid_settings (f.anchorSet);
		f.anchor = 0; f.anchorSet = 0;
		return AKE_CORRUPT;
	}
	f.used = true;
	f.path = path;
	const char *b = strrchr (path, '/');
	f.name = b ? b + 1 : path;
	if (f.name.size () > 4 && !strcasecmp (f.name.c_str () + f.name.size () - 4, ".sf2")) f.name.resize (f.name.size () - 4);
	f.presets.clear ();
	fluid_sfont_t *sf = fluid_synth_get_sfont_by_id (f.anchor, id);
	if (sf)
	{
		fluid_sfont_iteration_start (sf);
		for (fluid_preset_t *p; (p = fluid_sfont_iteration_next (sf)); )
		{
			Preset q;
			q.bank = fluid_preset_get_banknum (p);
			q.program = fluid_preset_get_num (p);
			snprintf (q.name, sizeof q.name, "%s", fluid_preset_get_name (p) ? fluid_preset_get_name (p) : "");
			f.presets.push_back (q);
		}
	}
	return h;
}

long system_sf_locked ()
{
	if (g_sysSf > 0 && g_sf[g_sysSf].used) return g_sysSf;
	const char *p = kport_soundfont ();
	if (!p || !*p) return 0;
	long h = sf_load_locked (p);
	g_sysSf = h > 0 ? h : 0;
	return g_sysSf;
}

Synth *synth (long h) { return h >= 1 && h <= MAX_SYNTH && g_syn[h].used ? &g_syn[h] : 0; }

// ---- les flux de sortie ----------------------------------------------------------------------------------------------
struct Stream
{
	bool used;
	fluid_settings_t *set;
	fluid_audio_driver_t *drv;
	pthread_mutex_t m;
	pthread_cond_t space;
	int16_t *ring;			// trames stereo
	long cap, rd, wr;		// (en trames ; wr - rd = en attente)
	long limit;			// l'avance acceptee (trames)
	long played;
	bool closing;
};
Stream g_st[MAX_STREAM + 1];

int stream_cb (void *data, int len, int nfx, float **fx, int nout, float **out)
{
	(void) nfx; (void) fx;
	Stream *s = (Stream *) data;
	pthread_mutex_lock (&s->m);
	int i = 0;
	for (; i < len && s->rd < s->wr; i++, s->rd++)
	{
		const int16_t *f = s->ring + (s->rd % s->cap) * 2;
		if (nout > 0) out[0][i] = f[0] / 32768.0f;
		if (nout > 1) out[1][i] = f[1] / 32768.0f;
	}
	for (; i < len; i++) { if (nout > 0) out[0][i] = 0; if (nout > 1) out[1][i] = 0; }
	long before = s->played;
	s->played += len;
	static int verbose = getenv ("KORIN_FLUID_VERBOSE") ? 1 : 0;
	if (verbose && s->played / (RATE * 2) != before / (RATE * 2))
	{
		float pk = 0;
		for (int k = 0; k < len && nout > 0; k++) pk = fabsf (out[0][k]) > pk ? fabsf (out[0][k]) : pk;
		fprintf (stderr, "audiokit: %ld trames jouees, en attente %ld, crete %.3f\n", s->played, s->wr - s->rd, pk);
	}
	pthread_cond_broadcast (&s->space);
	pthread_mutex_unlock (&s->m);
	return FLUID_OK;
}

Stream *stream (long h) { return h >= 1 && h <= MAX_STREAM && g_st[h].used ? &g_st[h] : 0; }

long stream_write (Stream *s, const char *buf, long bytes, bool all)
{
	long frames = bytes / 4, done = 0;
	const int16_t *src = (const int16_t *) buf;
	pthread_mutex_lock (&s->m);
	while (done < frames && !s->closing)
	{
		long room = s->limit - (s->wr - s->rd);
		if (room <= 0)
		{
			if (!all) break;
			struct timespec t;
			clock_gettime (CLOCK_REALTIME, &t);
			t.tv_nsec += 50 * 1000000L;
			if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
			pthread_cond_timedwait (&s->space, &s->m, &t);
			continue;
		}
		long n = frames - done < room ? frames - done : room;
		for (long k = 0; k < n; k++)
		{
			int16_t *d = s->ring + ((s->wr + k) % s->cap) * 2;
			d[0] = src[(done + k) * 2];
			d[1] = src[(done + k) * 2 + 1];
		}
		s->wr += n;
		done += n;
	}
	pthread_mutex_unlock (&s->m);
	return done * 4;
}

// ---- l'export ------------------------------------------------------------------------------------------------------------
struct LameApi
{
	void *h;
	void *(*init) ();
	int (*set_in_samplerate) (void *, int), (*set_num_channels) (void *, int), (*set_brate) (void *, int), (*set_quality) (void *, int);
	int (*init_params) (void *);
	int (*encode_interleaved) (void *, short *, int, unsigned char *, int);
	int (*flush) (void *, unsigned char *, int);
	int (*close) (void *);
};
LameApi g_lame;
bool lame_ok ()
{
	if (g_lame.h) return true;
	void *h = dlopen ("libmp3lame.so.0", RTLD_NOW);
	if (!h) return false;
	#define SYM(f, n) *(void **) &g_lame.f = dlsym (h, n)
	SYM (init, "lame_init"); SYM (set_in_samplerate, "lame_set_in_samplerate"); SYM (set_num_channels, "lame_set_num_channels");
	SYM (set_brate, "lame_set_brate"); SYM (set_quality, "lame_set_quality"); SYM (init_params, "lame_init_params");
	SYM (encode_interleaved, "lame_encode_buffer_interleaved"); SYM (flush, "lame_encode_flush"); SYM (close, "lame_close");
	#undef SYM
	if (!g_lame.init || !g_lame.init_params || !g_lame.encode_interleaved || !g_lame.flush || !g_lame.close) { dlclose (h); return false; }
	g_lame.h = h;
	return true;
}

struct Encoder
{
	bool used;
	int format, channels, rate;
	FILE *f;
	long long frames;
	void *lame;
	std::vector<unsigned char> out;
};
Encoder g_enc[MAX_ENC + 1];

void put32 (unsigned char *p, uint32_t v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); p[2] = (unsigned char) (v >> 16); p[3] = (unsigned char) (v >> 24); }
void put16 (unsigned char *p, unsigned v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); }

} // namespace

extern "C" {

int audiokit_kit_open (void) { init_log (); return 1; }

long ak_wav_header (long buf, long rate, long channels, long bits, long bytes)
{
	unsigned char *h = (unsigned char *) buf;
	memcpy (h, "RIFF", 4); put32 (h + 4, (uint32_t) (36 + bytes)); memcpy (h + 8, "WAVEfmt ", 8);
	put32 (h + 16, 16); put16 (h + 20, 1); put16 (h + 22, (unsigned) channels); put32 (h + 24, (uint32_t) rate);
	put32 (h + 28, (uint32_t) (rate * channels * bits / 8)); put16 (h + 32, (unsigned) (channels * bits / 8)); put16 (h + 34, (unsigned) bits);
	memcpy (h + 36, "data", 4); put32 (h + 40, (uint32_t) bytes);
	return 44;
}

// ---- les SoundFonts ----
long ak_sf_load (long path)
{
	pthread_mutex_lock (&g_lk);
	long r = sf_load_locked ((const char *) path);
	pthread_mutex_unlock (&g_lk);
	return r;
}
long ak_sf_free (long h)
{
	pthread_mutex_lock (&g_lk);
	if (h >= 1 && h <= MAX_SF && g_sf[h].used && h != g_sysSf)
	{
		bool inUse = false;
		for (int i = 1; i <= MAX_SYNTH; i++) if (g_syn[i].used && g_syn[i].sf == h) inUse = true;
		if (!inUse)
		{
			delete_fluid_synth (g_sf[h].anchor); delete_fluid_settings (g_sf[h].anchorSet);
			g_sf[h].anchor = 0; g_sf[h].anchorSet = 0; g_sf[h].used = false; g_sf[h].presets.clear ();
		}
	}
	pthread_mutex_unlock (&g_lk);
	return 0;
}
long ak_sf_name (long h) { return h >= 1 && h <= MAX_SF && g_sf[h].used ? (long) g_sf[h].name.c_str () : 0; }
long ak_sf_presets (long h) { return h >= 1 && h <= MAX_SF && g_sf[h].used ? (long) g_sf[h].presets.size () : 0; }
long ak_sf_preset (long h, long i, long buf)
{
	if (!(h >= 1 && h <= MAX_SF && g_sf[h].used) || i < 0 || i >= (long) g_sf[h].presets.size ()) return AKE_INVAL;
	unsigned char *p = (unsigned char *) buf;
	memset (p, 0, AKSP_SIZE);
	const Preset &q = g_sf[h].presets[(size_t) i];
	*(int *) (p + AKSP_BANK) = q.bank;
	*(int *) (p + AKSP_PROGRAM) = q.program;
	snprintf ((char *) p + AKSP_NAME, AKSP_SIZE - AKSP_NAME, "%s", q.name);
	return 0;
}

// ---- les synthes ----
long ak_synth_new (long sf, long flags)
{
	pthread_mutex_lock (&g_lk);
	long h = 0;
	for (long i = 1; i <= MAX_SYNTH && !h; i++) if (!g_syn[i].used) h = i;
	long use = sf > 0 && sf <= MAX_SF && g_sf[sf].used ? sf : system_sf_locked ();
	if (!h || use <= 0) { pthread_mutex_unlock (&g_lk); return h ? AKE_IO : AKE_NOMEM; }
	Synth &y = g_syn[h];
	y.set = synth_settings (!(flags & AKS_NOFX));
	y.syn = new_fluid_synth (y.set);
	y.sfid = y.syn ? fluid_synth_sfload (y.syn, g_sf[use].path.c_str (), 1) : FLUID_FAILED;
	if (y.sfid == FLUID_FAILED)
	{
		if (y.syn) delete_fluid_synth (y.syn);
		delete_fluid_settings (y.set);
		pthread_mutex_unlock (&g_lk);
		return AKE_CORRUPT;
	}
	y.sf = sf > 0 ? use : 0;
	y.used = true;
	pthread_mutex_unlock (&g_lk);
	return h;
}
long ak_synth_free (long h)
{
	pthread_mutex_lock (&g_lk);
	Synth *y = synth (h);
	if (y)
	{
		delete_fluid_synth (y->syn); delete_fluid_settings (y->set);
		y->syn = 0; y->set = 0; y->used = false;
	}
	pthread_mutex_unlock (&g_lk);
	return 0;
}
long ak_synth_midi (long h, long ch, long cmd, long a, long b)
{
	Synth *y = synth (h);
	if (!y) return AKE_INVAL;
	int c = (int) (ch & 15), d1 = (int) (a & 127), d2 = (int) (b & 127);
	switch (cmd & 0xF0)
	{
	case 0x80: fluid_synth_noteoff (y->syn, c, d1); break;
	case 0x90: if (d2) fluid_synth_noteon (y->syn, c, d1, d2); else fluid_synth_noteoff (y->syn, c, d1); break;
	case 0xA0: fluid_synth_key_pressure (y->syn, c, d1, d2); break;
	case 0xB0: fluid_synth_cc (y->syn, c, d1, d2); break;
	case 0xC0: fluid_synth_program_change (y->syn, c, d1); break;
	case 0xD0: fluid_synth_channel_pressure (y->syn, c, d1); break;
	case 0xE0: fluid_synth_pitch_bend (y->syn, c, d1 | d2 << 7); break;
	}
	return 0;
}
long ak_synth_render_f32 (long h, long l, long r, long frames)
{
	Synth *y = synth (h);
	if (!y) { memset ((void *) l, 0, (size_t) frames * 4); memset ((void *) r, 0, (size_t) frames * 4); return AKE_INVAL; }
	fluid_synth_write_float (y->syn, (int) frames, (float *) l, 0, 1, (float *) r, 0, 1);
	return 0;
}
long ak_synth_render (long h, long out, long frames)
{
	int16_t *o = (int16_t *) out;
	Synth *y = synth (h);
	if (!y) { memset (o, 0, (size_t) frames * 4); return AKE_INVAL; }
	if ((long) y->tl.size () < frames) { y->tl.resize ((size_t) frames); y->tr.resize ((size_t) frames); }
	fluid_synth_write_float (y->syn, (int) frames, y->tl.data (), 0, 1, y->tr.data (), 0, 1);
	for (long i = 0; i < frames; i++)
	{
		float a = y->tl[(size_t) i] * 32767.0f, b = y->tr[(size_t) i] * 32767.0f;
		o[2 * i] = (int16_t) (a > 32767 ? 32767 : a < -32768 ? -32768 : a);
		o[2 * i + 1] = (int16_t) (b > 32767 ? 32767 : b < -32768 ? -32768 : b);
	}
	return 0;
}
long ak_synth_voices (long h) { Synth *y = synth (h); return y ? fluid_synth_get_active_voice_count (y->syn) : 0; }

// ---- les flux ----
long ak_open (long rate, long channels, long bits, long out)
{
	(void) out;
	init_log ();
	if (rate != RATE || channels != 2 || bits != 16) return AKE_INVAL;
	pthread_mutex_lock (&g_lk);
	long h = 0;
	for (long i = 1; i <= MAX_STREAM && !h; i++) if (!g_st[i].used) h = i;
	if (!h) { pthread_mutex_unlock (&g_lk); return AKE_NOMEM; }
	Stream &s = g_st[h];
	s.cap = 65536 / 4 * 2;
	s.ring = new int16_t[s.cap * 2];
	s.rd = s.wr = s.played = 0;
	s.limit = 8192 / 4;
	s.closing = false;
	pthread_mutex_init (&s.m, 0);
	pthread_cond_init (&s.space, 0);
	s.set = new_fluid_settings ();
	fluid_settings_setnum (s.set, "synth.sample-rate", RATE);
	fluid_settings_setint (s.set, "audio.period-size", 256);
	fluid_settings_setint (s.set, "audio.periods", 4);
	const char *drv = getenv ("KORIN_AUDIO_DRIVER");
	const char *order[] = { drv && *drv ? drv : "pipewire", "pulseaudio", "alsa" };
	s.drv = 0;
	for (int i = 0; i < 3 && !s.drv; i++)
	{
		if (fluid_settings_setstr (s.set, "audio.driver", order[i]) != FLUID_OK) continue;
		if (!strcmp (order[i], "pipewire")) fluid_settings_setstr (s.set, "audio.pipewire.media-role", "Music");
		s.drv = new_fluid_audio_driver2 (s.set, stream_cb, &s);
		if (s.drv && getenv ("KORIN_FLUID_VERBOSE")) fprintf (stderr, "audiokit: sortie %s\n", order[i]);
	}
	if (!s.drv)
	{
		delete_fluid_settings (s.set); delete[] s.ring; s.ring = 0;
		pthread_mutex_unlock (&g_lk);
		fprintf (stderr, "audiokit: aucune sortie audio (PipeWire, PulseAudio, ALSA)\n");
		return AKE_IO;
	}
	s.used = true;
	pthread_mutex_unlock (&g_lk);
	return h;
}
long ak_close (long h)
{
	Stream *s = stream (h);
	if (!s) return 0;
	pthread_mutex_lock (&s->m); s->closing = true; pthread_cond_broadcast (&s->space); pthread_mutex_unlock (&s->m);
	delete_fluid_audio_driver (s->drv);
	delete_fluid_settings (s->set);
	pthread_mutex_lock (&g_lk);
	delete[] s->ring; s->ring = 0; s->used = false;
	pthread_mutex_unlock (&g_lk);
	return 0;
}
long ak_write (long h, long buf, long bytes) { Stream *s = stream (h); return s ? stream_write (s, (const char *) buf, bytes, false) : AKE_INVAL; }
long ak_write_all (long h, long buf, long bytes) { Stream *s = stream (h); return s ? stream_write (s, (const char *) buf, bytes, true) : AKE_INVAL; }
long ak_pending (long h)
{
	Stream *s = stream (h);
	if (!s) return 0;
	pthread_mutex_lock (&s->m); long n = (s->wr - s->rd) * 4; pthread_mutex_unlock (&s->m);
	return n;
}
long ak_space (long h)
{
	Stream *s = stream (h);
	if (!s) return 0;
	pthread_mutex_lock (&s->m); long n = (s->limit - (s->wr - s->rd)) * 4; pthread_mutex_unlock (&s->m);
	return n > 0 ? n : 0;
}
long ak_position (long h) { Stream *s = stream (h); return s ? s->played : 0; }
long ak_latency (long h, long bytes)
{
	Stream *s = stream (h);
	if (!s) return AKE_INVAL;
	if (bytes < 4096) bytes = 4096;
	if (bytes > 65536) bytes = 65536;
	pthread_mutex_lock (&s->m); s->limit = bytes / 4; pthread_mutex_unlock (&s->m);
	return 0;
}

// ---- les encodeurs ----
long ak_enc_open (long path, long format, long rate, long channels, long bitrate)
{
	if (format != AKF_WAV && format != AKF_MP3) return AKE_FORMAT;
	if (format == AKF_MP3 && !lame_ok ()) { fprintf (stderr, "audiokit: MP3 : libmp3lame.so.0 introuvable (dnf install lame-libs)\n"); return AKE_FORMAT; }
	long h = 0;
	pthread_mutex_lock (&g_lk);
	for (long i = 1; i <= MAX_ENC && !h; i++) if (!g_enc[i].used) h = i;
	if (h) g_enc[h].used = true;
	pthread_mutex_unlock (&g_lk);
	if (!h) return AKE_NOMEM;
	Encoder &e = g_enc[h];
	char tb[PATH_MAX];
	e.f = fopen (kport_path ((const char *) path, tb, sizeof tb), "wb");
	if (!e.f) { e.used = false; return AKE_IO; }
	e.format = (int) format; e.channels = (int) channels; e.rate = (int) rate; e.frames = 0; e.lame = 0;
	if (format == AKF_WAV)
	{
		unsigned char hd[44];
		ak_wav_header ((long) hd, rate, channels, 16, 0);
		fwrite (hd, 1, 44, e.f);
	}
	else
	{
		e.lame = g_lame.init ();
		g_lame.set_in_samplerate (e.lame, (int) rate);
		g_lame.set_num_channels (e.lame, (int) channels);
		g_lame.set_brate (e.lame, (int) (bitrate > 0 ? bitrate / 1000 : 192));
		if (g_lame.set_quality) g_lame.set_quality (e.lame, 2);
		if (g_lame.init_params (e.lame) < 0) { g_lame.close (e.lame); fclose (e.f); e.used = false; return AKE_INVAL; }
	}
	return h;
}
long ak_enc_write (long h, long pcm, long frames)
{
	if (h < 1 || h > MAX_ENC || !g_enc[h].used) return AKE_INVAL;
	Encoder &e = g_enc[h];
	if (e.format == AKF_WAV)
	{
		if (fwrite ((const void *) pcm, (size_t) e.channels * 2, (size_t) frames, e.f) != (size_t) frames) return AKE_IO;
	}
	else
	{
		e.out.resize ((size_t) (frames * 5 / 4 + 7200));
		int n = g_lame.encode_interleaved (e.lame, (short *) pcm, (int) frames, e.out.data (), (int) e.out.size ());
		if (n < 0 || (n > 0 && fwrite (e.out.data (), 1, (size_t) n, e.f) != (size_t) n)) return AKE_IO;
	}
	e.frames += frames;
	return 0;
}
long ak_enc_close (long h)
{
	if (h < 1 || h > MAX_ENC || !g_enc[h].used) return AKE_INVAL;
	Encoder &e = g_enc[h];
	long r = 0;
	if (e.format == AKF_WAV)
	{
		unsigned char hd[44];
		ak_wav_header ((long) hd, e.rate, e.channels, 16, e.frames * e.channels * 2);
		if (fseek (e.f, 0, SEEK_SET) != 0 || fwrite (hd, 1, 44, e.f) != 44) r = AKE_IO;
	}
	else
	{
		e.out.resize (7200);
		int n = g_lame.flush (e.lame, e.out.data (), (int) e.out.size ());
		if (n > 0) fwrite (e.out.data (), 1, (size_t) n, e.f);
		g_lame.close (e.lame);
	}
	if (fclose (e.f) != 0) r = AKE_IO;
	e.used = false;
	return r;
}

// la SoundFont du systeme changee : chaque synthe qui la suit la recharge (ses programmes repartent de zero : l'app
// les redonne a la lecture suivante)
int kport_audio_reload_soundfont (const char *path)
{
	pthread_mutex_lock (&g_lk);
	long old = g_sysSf;
	g_sysSf = 0;
	long h = sf_load_locked (path);
	if (h <= 0) { g_sysSf = old; pthread_mutex_unlock (&g_lk); return 0; }
	g_sysSf = h;
	for (int i = 1; i <= MAX_SYNTH; i++)
	{
		Synth &y = g_syn[i];
		if (!y.used || y.sf != 0) continue;
		int id = fluid_synth_sfload (y.syn, g_sf[h].path.c_str (), 1);
		if (id == FLUID_FAILED) continue;
		fluid_synth_sfunload (y.syn, y.sfid, 1);
		y.sfid = id;
	}
	if (old > 0 && old != h)
	{
		delete_fluid_synth (g_sf[old].anchor); delete_fluid_settings (g_sf[old].anchorSet);
		g_sf[old].anchor = 0; g_sf[old].anchorSet = 0; g_sf[old].used = false;
	}
	pthread_mutex_unlock (&g_lk);
	return 1;
}

} // extern "C"
