/**************************************************************************/
/*  audio_driver_korin.cpp                                                */
/**************************************************************************/
/* Korin OS : AudioKit (audio_driver_korin.h). */

#include "audio_driver_korin.h"

#include "core/os/os.h"

extern "C" {
#include <armpi/kit.h>
}

/* la table d'AudioKit (src/kits/audiokit/audiokit.abi) */
enum { AK_OPEN = 0,
	AK_WRITE = 1,
	AK_SPACE = 3,
	AK_CLOSE = 7,
	AK_LATENCY = 10,
	AK_MIN = 11 };

static long (*ak_open)(long rate, long channels, long bits, long output);
static long (*ak_write)(long stream, const void *buf, long bytes);
static long (*ak_space)(long stream);
static long (*ak_close)(long stream);
static long (*ak_latency)(long stream, long bytes);

Error AudioDriverKorin::init() {
	void *k = armpi_kit_open("audiokit", AK_MIN);
	ERR_FAIL_NULL_V_MSG(k, ERR_CANT_OPEN, "Korin : AudioKit absent.");
	ak_open = (long (*)(long, long, long, long))armpi_kit_fn(k, AK_OPEN);
	ak_write = (long (*)(long, const void *, long))armpi_kit_fn(k, AK_WRITE);
	ak_space = (long (*)(long))armpi_kit_fn(k, AK_SPACE);
	ak_close = (long (*)(long))armpi_kit_fn(k, AK_CLOSE);
	ak_latency = (long (*)(long, long))armpi_kit_fn(k, AK_LATENCY);

	stream = ak_open(44100, 2, 16, 0);
	ERR_FAIL_COND_V_MSG(stream < 0, ERR_CANT_OPEN, "Korin : pas de flux son (service sound).");
	ak_latency(stream, buffer_frames * 4L * 2);

	samples_in = memnew_arr(int32_t, size_t(buffer_frames) * 2);
	samples_out = memnew_arr(int16_t, size_t(buffer_frames) * 2);
	active.clear();
	exit_thread.clear();
	thread.start(AudioDriverKorin::thread_func, this);
	return OK;
}

void AudioDriverKorin::thread_func(void *p_udata) {
	AudioDriverKorin *ad = static_cast<AudioDriverKorin *>(p_udata);
	const long bytes = long(ad->buffer_frames) * 4;

	while (!ad->exit_thread.is_set()) {
		if (!ad->active.is_set()) {
			OS::get_singleton()->delay_usec(10000);
			continue;
		}
		ad->lock();
		ad->start_counting_ticks();
		ad->audio_server_process(ad->buffer_frames, ad->samples_in);
		ad->stop_counting_ticks();
		ad->unlock();

		for (uint32_t i = 0; i < ad->buffer_frames * 2; i++) {
			ad->samples_out[i] = int16_t(ad->samples_in[i] >> 16);
		}
		/* AudioKit ne reveille personne : la place, par petits sommeils */
		while (!ad->exit_thread.is_set() && ak_space(ad->stream) < bytes) {
			OS::get_singleton()->delay_usec(2000);
		}
		ak_write(ad->stream, ad->samples_out, bytes);
	}
}

void AudioDriverKorin::start() {
	active.set();
}

float AudioDriverKorin::get_latency() {
	return float(buffer_frames * 2) / 44100.0f;
}

void AudioDriverKorin::lock() {
	mutex.lock();
}

void AudioDriverKorin::unlock() {
	mutex.unlock();
}

void AudioDriverKorin::finish() {
	exit_thread.set();
	if (thread.is_started()) {
		thread.wait_to_finish();
	}
	if (stream >= 0) {
		ak_close(stream);
		stream = -1;
	}
	if (samples_in) {
		memdelete_arr(samples_in);
		samples_in = nullptr;
	}
	if (samples_out) {
		memdelete_arr(samples_out);
		samples_out = nullptr;
	}
}
