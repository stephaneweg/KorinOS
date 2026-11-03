/*
 * SDL_armpiaudio.c -- le son de SDL 3 sur AudioKit (docs/SON.md, docs/SDL.md) : un flux s16 stereo 44,1 kHz,
 * le format natif de sound. SDL convertit le reste. AudioKit ne reveille personne : on attend la place par
 * petits sommeils, comme lui (ak_write_all).
 */
#include "SDL_internal.h"

#include "../src/audio/SDL_sysaudio.h"

#include <armpi/kit.h>

enum { AK_OPEN = 0, AK_WRITE = 1, AK_SPACE = 3, AK_CLOSE = 7, AK_LATENCY = 10, AK_MIN = 11 };

static long (*ak_open) (long rate, long channels, long bits, long output);
static long (*ak_write) (long stream, const void *buf, long bytes);
static long (*ak_space) (long stream);
static long (*ak_close) (long stream);
static long (*ak_latency) (long stream, long bytes);

struct SDL_PrivateAudioData
{
	long stream;
	Uint8 *mix;
};

static bool ARMPI_OpenDevice (SDL_AudioDevice *device)
{
	struct SDL_PrivateAudioData *h = SDL_calloc (1, sizeof *h);

	if (!h)
		return false;
	device->hidden = h;
	h->stream = -1;
	device->spec.format = SDL_AUDIO_S16LE;
	device->spec.channels = 2;
	device->spec.freq = 44100;
	SDL_UpdatedAudioDeviceFormat (device);
	h->mix = SDL_malloc (device->buffer_size);
	if (!h->mix)
		return false;
	h->stream = ak_open (44100, 2, 16, 0);
	if (h->stream < 0)
		return SDL_SetError ("ARMPI : pas de flux son");
	ak_latency (h->stream, device->buffer_size * 2L);
	return true;
}

static bool ARMPI_WaitDevice (SDL_AudioDevice *device)
{
	struct SDL_PrivateAudioData *h = device->hidden;

	while (!SDL_GetAtomicInt (&device->shutdown) && ak_space (h->stream) < device->buffer_size)
		SDL_Delay (2);
	return true;
}

static bool ARMPI_PlayDevice (SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
	return ak_write (device->hidden->stream, buffer, buflen) >= 0;
}

static Uint8 *ARMPI_GetDeviceBuf (SDL_AudioDevice *device, int *buffer_size)
{
	return device->hidden->mix;
}

static void ARMPI_CloseDevice (SDL_AudioDevice *device)
{
	struct SDL_PrivateAudioData *h = device->hidden;

	if (h) {
		if (h->stream >= 0)
			ak_close (h->stream);
		SDL_free (h->mix);
		SDL_free (h);
		device->hidden = NULL;
	}
}

static bool ARMPI_Init (SDL_AudioDriverImpl *impl)
{
	void *k = armpi_kit_open ("audiokit", AK_MIN);

	if (!k)
		return false;
	ak_open = (long (*) (long, long, long, long)) armpi_kit_fn (k, AK_OPEN);
	ak_write = (long (*) (long, const void *, long)) armpi_kit_fn (k, AK_WRITE);
	ak_space = (long (*) (long)) armpi_kit_fn (k, AK_SPACE);
	ak_close = (long (*) (long)) armpi_kit_fn (k, AK_CLOSE);
	ak_latency = (long (*) (long, long)) armpi_kit_fn (k, AK_LATENCY);
	impl->OpenDevice = ARMPI_OpenDevice;
	impl->WaitDevice = ARMPI_WaitDevice;
	impl->PlayDevice = ARMPI_PlayDevice;
	impl->GetDeviceBuf = ARMPI_GetDeviceBuf;
	impl->CloseDevice = ARMPI_CloseDevice;
	impl->OnlyHasDefaultPlaybackDevice = true;
	return true;
}

AudioBootStrap PRIVATEAUDIO_bootstrap = {
	"armpi", "ARMPI : AudioKit", ARMPI_Init, false, false
};
