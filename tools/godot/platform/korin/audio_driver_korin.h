/**************************************************************************/
/*  audio_driver_korin.h                                                  */
/**************************************************************************/
/* Korin OS : le son de Godot sur AudioKit (docs/SON.md) : un flux s16 stereo
 * 44,1 kHz, le format natif du service sound, comme le pilote de SDL
 * (tools/sdl/armpi/SDL_armpiaudio.c). Godot mixe en 32 bits ; ce pilote
 * convertit et attend la place dans le flux par petits sommeils. */

#pragma once

#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "servers/audio/audio_server.h"

class AudioDriverKorin : public AudioDriver {
	Thread thread;
	Mutex mutex;

	int32_t *samples_in = nullptr;
	int16_t *samples_out = nullptr;
	uint32_t buffer_frames = 1024;
	long stream = -1;

	SafeFlag active;
	SafeFlag exit_thread;

	static void thread_func(void *p_udata);

public:
	virtual const char *get_name() const override { return "Korin"; }

	virtual Error init() override;
	virtual void start() override;
	virtual int get_mix_rate() const override { return 44100; }
	virtual SpeakerMode get_speaker_mode() const override { return SPEAKER_MODE_STEREO; }
	virtual float get_latency() override;

	virtual void lock() override;
	virtual void unlock() override;
	virtual void finish() override;
};
