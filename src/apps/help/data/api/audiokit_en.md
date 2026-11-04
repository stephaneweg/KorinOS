# AudioKit: sound

**AudioKit** (`SD0:/lib/audiokit.so`) is the only way programs reach the sound. It opens playback and capture streams with the `sound` service, and does everything else **inside the application's process**: decoders (WAV, FLAC, MP3, Ogg Vorbis, Ogg Opus, MIDI, FM Song), a background player, encoders (WAV, MP3, Opus), FM synthesis and a General MIDI synthesizer on SoundFonts, conversion and mixing.

The kit is written in assembly; its functions follow AAPCS64 and take only integers, pointers and opaque handles. From C, every argument is a `long`.

| Layer | Role |
|---|---|
| your program | calls `ak_*` |
| AudioKit | decodes, synthesizes, converts to the stream format (s16 stereo 44.1 kHz) |
| `sound` (service) | mixes the streams, applies the volumes, picks the output |
| drivers | headphone jack, HDMI, USB sound card, I2S microphone |

Under QEMU, `sound` has a **virtual output** that consumes sound at the pace of the clock: a program that plays runs normally, you just hear nothing (or the sound goes to QEMU's file when it emulates a USB card).

## Opening the kit

```c
#include <audiokit_abi.h>

if (!audiokit_kit_open ()) return 1;   // SD0:/lib/audiokit.so missing
```

Nothing to add when linking: `pibuild` takes every library of `/gcc/lib`, including the relays of `libaudiokit.a`. The kit is mapped on the first open; calling it again costs nothing.

Errors are negative values: `AKE_NOMEM` (-12), `AKE_INVAL` (-22), `AKE_IO` (-5), `AKE_CORRUPT` (-74), `AKE_FORMAT` (-95), or an AppKit error (missing file...).

## Playback streams

A stream takes interleaved PCM in the format chosen when opening it; AudioKit converts it to s16 stereo 44.1 kHz (mono to stereo, 8, 24, 32 bits, other rates through its resampler). The stream volume combines with the program's (set in the Mixer) and the output's.

| Function | Arguments | Result |
|---|---|---|
| `ak_open` | rate, channels, bits (8, 16, 24, 32), output (0: the program's) | stream, < 0 on error |
| `ak_write` | stream, buffer, bytes | bytes taken (does not wait) |
| `ak_write_all` | stream, buffer, bytes | bytes; waits for room (see the pitfalls) |
| `ak_space` | stream | bytes accepted right now, in the stream's format |
| `ak_pending` | stream | bytes waiting (s16 stereo 44.1 kHz) |
| `ak_position` | stream | frames played |
| `ak_volume` | stream, volume 0..256 | 0 |
| `ak_status` | stream, buffer of 3 `long` | 0: waiting, free, hardware side |
| `ak_drain` | stream | waits until everything is played (10 s at most) |
| `ak_latency` | stream, maximum lead (s16 stereo bytes, 4096..65536) | 0 |
| `ak_close` | stream | 0 |

The default lead is about two blocks of 1024 frames: `ak_write_all` does not let a program get seconds ahead. A game that wants less latency lowers `ak_latency`; a player that wants more margin raises it.

## Capture

A capture returns s16 stereo 44.1 kHz. The sources (the `REC_SRC_*` constants of `src/inc/sound.inc`, to copy into your program):

| Source | Value |
|---|---|
| microphone (the USB input if there is one, otherwise the first) | 0 |
| loopback: what the main output plays | 1 |
| loopback of output n | 0x100 + n |
| input n | 0x200 + n |

| Function | Arguments | Result |
|---|---|---|
| `ak_rec_open` | source | capture, < 0 on error (no microphone: error) |
| `ak_rec_read` | capture, buffer, bytes | bytes read (does not wait) |
| `ak_rec_status` | capture, buffer of 3 `long` | 0: available, source, lost |
| `ak_rec_close` | capture | 0 |

## Mixer and outputs

What the Mixer and the sound panel of the menu bar do; an ordinary program does not need it.

| Function | Arguments | Result |
|---|---|---|
| `ak_backend` | – | 1: `sound` service present, 0: none |
| `ak_state` | – | address of `sound`'s state buffer (read only, 4096 bytes), 0 without the service |
| `ak_out_volume` | output, volume 0..100, mute | 0 |
| `ak_client_volume` | pid (0: itself), volume 0..100, mute | 0 |
| `ak_route` | pid (0: itself), output (0: the main one) | 0: all the program's streams go there |
| `ak_main` | preferred output (0: automatic) | 0; the preference is saved |

The state buffer (the `SS_*`, `SO_*`, `SC_*` fields of `src/inc/sound.inc`) starts with a generation number that changes with every modification: reading it from time to time is enough. The [Recording](#example-recording-what-plays) example reads the outputs from it.

## Tools

| Function | Arguments | Result |
|---|---|---|
| `ak_resample` | s16 stereo input, frames, output, state of 3 `long` | frames produced |
| `ak_mix_s16` | destination, source, frames, gain 0..65536 | destination += source × gain (s16 stereo, saturated) |
| `ak_gain_s16` | buffer, frames, gain 0..65536 | 0 |
| `ak_volume_curve` | volume 0..100 | gain 0..65536 "by ear" (the square of the volume) |
| `ak_wav_header` | 44-byte buffer, rate, channels, bits, data bytes | 44 |

The state of `ak_resample`: the previous frame (left in the low 16 bits, right in the next 16), the position (16.16) and the step (16.16): step = (input rate << 16) / output rate. Set all three to zero except the step before the first block, then keep the state from one block to the next.

## Decoding a file

The format is recognized from the first bytes, not from the extension.

| Format | Decoding | Encoding |
|---|---|---|
| WAV | PCM 8, 16, 24, 32 bits, 32-bit float | 16-bit PCM |
| FLAC | 4 to 24 bits, 1 to 8 channels | – |
| MP3 | MPEG-1, 2, 2.5 layer III, ID3v1 and v2 | CBR 32 to 320 kbit/s |
| Ogg Vorbis | Vorbis I | – |
| Ogg Opus | CELT, SILK, hybrid; always 48 kHz | CELT, 16 to 256 kbit/s |
| MIDI (`.mid`) | formats 0, 1, 2, through the kit's synthesizer, s16 stereo 44.1 kHz | – |
| FM Song (`.fms`) | FM Tracker songs, s16 stereo 44.1 kHz | – |

| Function | Arguments | Result |
|---|---|---|
| `ak_dec_open` | path | decoder, < 0 on error |
| `ak_dec_open_mem` | buffer, bytes | decoder; the buffer must live until `ak_dec_close` |
| `ak_dec_info` | decoder, buffer of `AKI_SIZE` (64) bytes | 0 |
| `ak_dec_read` | decoder, buffer, frames | frames returned, **s16 interleaved in the source's channels**; 0: end; < 0: error |
| `ak_dec_seek` | decoder, position (ms) | 0, < 0 |
| `ak_dec_tell` | decoder | position (frames) |
| `ak_dec_tag` | decoder, `AKT_TITLE` / `AKT_ARTIST` / `AKT_ALBUM`, buffer, size | bytes (UTF-8, final 0 written); 0: missing |
| `ak_dec_close` | decoder | 0 |

The information buffer reads as an array of `long` (index = offset / 8):

| Offset | Content |
|---|---|
| `AKI_FORMAT` | `AKF_WAV` 1, `AKF_FLAC` 2, `AKF_MP3` 3, `AKF_VORBIS` 4, `AKF_OPUS` 5, `AKF_MIDI` 6, `AKF_FMS` 7 |
| `AKI_RATE`, `AKI_CHANNELS` | rate, channels |
| `AKI_FRAMES` | total frames (-1: unknown) |
| `AKI_DURATION` | duration (ms) |
| `AKI_BITRATE` | bit rate (bits/s) |
| `AKI_POSITION` | position (frames) |
| `AKI_BITS` | bits per sample of the source |

## The player

`ak_play` opens the file and a stream in its format, then starts **a thread** of the program that decodes and writes in blocks of 1024 frames. The program stays free: it asks for the state whenever it wants.

| Function | Arguments | Result |
|---|---|---|
| `ak_play` | path, output (0: the program's) | player, < 0 on error; playback starts |
| `ak_play_pause` | player, 1 pause / 0 resume | 0 |
| `ak_play_seek` | player, position (ms) | 0 |
| `ak_play_pos` | player | position **heard** (ms) |
| `ak_play_state` | player | `AKP_PLAYING` 1, `AKP_PAUSED` 2, `AKP_ENDED` 3, `AKP_ERROR` 4 |
| `ak_play_volume` | player, volume 0..256 | 0 |
| `ak_play_decoder` | player | its decoder, for `ak_dec_info` and `ak_dec_tag` (not `ak_dec_read`) |
| `ak_play_stop` | player | 0; stops the thread, closes stream and file |

## Decoding packets

For a demultiplexer (the one in [MediaKit](mediakit_en.md), or your own): AudioKit reads no file, it receives the packets one by one.

| Function | Arguments | Result |
|---|---|---|
| `ak_codec_open` | `AKF_*`, rate, channels, initialization data, bytes | handle, < 0 on error |
| `ak_codec_decode` | handle, packet, bytes, output, maximum frames, `AKC_S16` or `AKC_F32` | frames (0: packet without sound; < 0: packet skipped) |
| `ak_codec_reset` | handle | 0; call it after a seek |
| `ak_codec_info` | handle, `AKI_SIZE` buffer | 0; actual rate and channels |
| `ak_codec_close` | handle | 0 |

Initialization data: Vorbis, the three headers in Xiph lacing (Matroska's `CodecPrivate`); Opus, the `OpusHead`; FLAC, `fLaC` and its blocks, or STREAMINFO; MP3, none (0, 0). Plan an output of 1152 frames for MP3, 4096 for Vorbis, 5760 for Opus. Opus always returns 48 kHz; the pre-skip and the end trimming are up to the caller. A complete example is in the [MediaKit](mediakit_en.md) page.

## Encoding

| Function | Arguments | Result |
|---|---|---|
| `ak_enc_open` | path, `AKF_WAV` / `AKF_MP3` / `AKF_OPUS`, rate, channels (1, 2), bit rate (bits/s; 0: 128,000 for MP3, 96,000 for Opus) | encoder; the file is created |
| `ak_enc_write` | encoder, interleaved s16 PCM, frames | 0, < 0 |
| `ak_enc_close` | encoder | 0, < 0; ends the stream and closes the file |
| `ak_enc_size` | encoder | bytes written |
| `ak_enc_open_io` | write `(ctx, bytes, n) -> n`, seek `(ctx, position)` or 0, ctx, format, rate, channels, bit rate | encoder without a file (see the pitfalls) |

Opus resamples any rate to 48 kHz. An open encoder must always be closed by `ak_enc_close`: that is where WAV writes its sizes and MP3 its last frame.

## FM synthesis

An FM synth has 1 to 64 voices (0: 16). Each voice plays a simple wave or a two-operator instrument in the manner of the OPL2. The functions take a lock: one thread can render while another plays.

| Function | Arguments | Result |
|---|---|---|
| `ak_fm_new` | voices (0: 16) | synth, < 0 on error |
| `ak_fm_free` | synth | 0 |
| `ak_fm_instrument` | synth, voice, instrument (`AKFI_SIZE` = 24 bytes) | 0, < 0 |
| `ak_fm_from_fms` | instrument (24 bytes, output), 26 bytes of an FM Song instrument | 0 |
| `ak_fm_start` | synth, voice, frequency (milli-Hz), wave `AKW_*`, volume 0..255 | 0 |
| `ak_fm_stop` | synth, voice (-1: all) | 0; the release is heard |
| `ak_fm_silence` | synth | 0; everything stops at once |
| `ak_fm_active` | synth | 1 while a voice sounds |
| `ak_fm_pan` | synth, voice, 0 left .. 128 center .. 255 right | 0 |
| `ak_fm_pitch` | synth, voice, milli-Hz | 0; the note glides |
| `ak_fm_gain` | synth, voice, volume 0..255 | 0 |
| `ak_fm_render` | synth, s16 stereo output, frames | 0; the frames are **all written** (44.1 kHz) |
| `ak_fm_live` | synth, output (0: the program's; -1: stop) | 0; a kit thread renders the voices into a stream |
| `ak_note_mhz` | MIDI key | milli-Hz (69: A4 = 440,000) |
| `ak_note_octave_mhz` | note 0..11 (C..B), octave (C4 = key 60) | milli-Hz |

Waves: `AKW_SQUARE` 0, `AKW_SINE` 1, `AKW_TRIANGLE` 2, `AKW_SAW` 3, `AKW_NOISE` 4, `AKW_FM` 5 (the voice's instrument). An instrument is made of two operators (`AKFI_OP0`, `AKFI_OP1`, 10 bytes each: `AKFO_ATTACK`, `AKFO_DECAY`, `AKFO_SUSTAIN`, `AKFO_RELEASE`, `AKFO_LEVEL`, `AKFO_KSL`, `AKFO_MULT`, `AKFO_WAVE`, `AKFO_FLAGS` on 2 bytes with `AKFF_*`), followed by `AKFI_FEEDBACK` and `AKFI_CONN`.

## MIDI synthesizer and SoundFonts

`ak_synth_new` gives 16 General MIDI channels and 64 voices: notes, programs, pitch bend, the usual controllers (volume 7, pan 10, expression 11, sustain 64, reverb 91, chorus 93, RPN). Two engines: a **SoundFont 2** (the system's, `SD0:/res/soundfonts`, or another), otherwise a built-in **FM bank**.

| Function | Arguments | Result |
|---|---|---|
| `ak_sf_load` | path of an `.sf2` | SoundFont, < 0 on error |
| `ak_sf_open_mem` | `.sf2` file in memory, bytes | SoundFont; the buffer stays the caller's |
| `ak_sf_free` | SoundFont | 0; after freeing its synths |
| `ak_sf_default` | – | the system SoundFont, loaded once per program; 0: none |
| `ak_sf_name` | SoundFont | its name (UTF-8) |
| `ak_sf_presets` | SoundFont | number of presets |
| `ak_sf_preset` | SoundFont, number, `AKSP_SIZE` (32) buffer | 0: bank (`AKSP_BANK`), program (`AKSP_PROGRAM`), name (`AKSP_NAME`) |
| `ak_synth_new` | SoundFont (0: the system's), `AKS_FM` (FM bank) and/or `AKS_NOFX` (no effects) | synth |
| `ak_synth_free` | synth | 0 |
| `ak_synth_midi` | synth, channel 0..15, command (0x80, 0x90, 0xB0, 0xC0, 0xE0), data 1, data 2 | 0 |
| `ak_synth_render` | synth, s16 stereo output, frames | 0; frames **all written** (44.1 kHz) |
| `ak_synth_render_f32` | synth, left, right (`float`), frames | 0 |
| `ak_synth_reset` | synth | 0; everything stops, channels reset |
| `ak_synth_voices` | synth | voices sounding |

Several synths can share a SoundFont (read only). A MIDI synth has no lock: if one thread renders while another sends notes, protect the calls with a mutex.

## Example: playing a file

The player does everything; the program shows the title, the duration and the position.

```c
#include <stdio.h>
#include <unistd.h>
#include <audiokit_abi.h>

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/res/audiotest/jfk.mp3";
	if (!audiokit_kit_open ()) { printf ("AudioKit missing\n"); return 1; }

	long p = ak_play ((long) path, 0);
	if (p < 0) { printf ("%s: error %ld\n", path, p); return 1; }

	// the player's decoder: information and tags
	long dec = ak_play_decoder (p), info[AKI_SIZE / 8];
	char title[128];
	ak_dec_info (dec, (long) info);
	if (ak_dec_tag (dec, AKT_TITLE, (long) title, sizeof title) <= 0) snprintf (title, sizeof title, "%s", path);
	printf ("%s: %ld Hz, %ld channels, %ld ms\n", title, info[AKI_RATE / 8], info[AKI_CHANNELS / 8], info[AKI_DURATION / 8]);

	ak_play_volume (p, 200);
	for (;;)
	{
		long st = ak_play_state (p);
		if (st == AKP_ENDED || st == AKP_ERROR) break;
		printf ("\r%ld ms", ak_play_pos (p));
		fflush (stdout);
		usleep (250000);
	}
	printf ("\nend\n");
	ak_play_stop (p);	// always: stops the thread and closes the file
	return 0;
}
```

## Example: a computed sound

A 16-bit mono stream at 22,050 Hz: AudioKit converts it to `sound`'s format. `ak_write_all` waits for room, then `ak_drain` waits until everything is played.

```c
#include <math.h>
#include <stdio.h>
#include <audiokit_abi.h>

// ak_write_all is declared without arguments in the header: call it through this pointer
typedef long (*write_all_fn) (long stream, long buf, long bytes);

enum { RATE = 22050, BLOCK = 1024 };

int main (void)
{
	if (!audiokit_kit_open ()) return 1;
	write_all_fn write_all = (write_all_fn) ak_write_all;
	long s = ak_open (RATE, 1, 16, 0);
	if (s < 0) { printf ("no stream: %ld\n", s); return 1; }
	ak_volume (s, 128);

	static short buf[BLOCK];
	double phase = 0;
	long total = RATE * 2;					// two seconds
	for (long done = 0; done < total; done += BLOCK)
	{
		// an A at 440 Hz rising one octave
		double f = 440.0 * pow (2.0, (double) done / total);
		for (int i = 0; i < BLOCK; i++)
		{
			buf[i] = (short) (sin (phase) * 9000);
			phase += 2 * M_PI * f / RATE;
		}
		write_all (s, (long) buf, sizeof buf);
	}
	ak_drain (s);
	printf ("%ld frames played\n", ak_position (s));
	ak_close (s);
	return 0;
}
```

## Example: an FM scale

`ak_fm_live` hands the rendering to a kit thread: the program only starts and stops notes.

```c
#include <unistd.h>
#include <audiokit_abi.h>

int main (void)
{
	if (!audiokit_kit_open ()) return 1;
	long fm = ak_fm_new (4);
	if (fm < 0 || ak_fm_live (fm, 0) < 0) return 1;

	static const int scale[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };	// C major
	for (int i = 0; i < 8; i++)
	{
		int v = i & 1;						// two alternating voices
		long mhz = ak_note_octave_mhz (scale[i] % 12, 4 + scale[i] / 12);
		ak_fm_pan (fm, v, v ? 192 : 64);
		ak_fm_start (fm, v, mhz, i < 4 ? AKW_TRIANGLE : AKW_SQUARE, 160);
		usleep (250000);
		ak_fm_stop (fm, v);
	}
	// a final chord, then let the release finish
	ak_fm_start (fm, 0, ak_note_mhz (60), AKW_SINE, 140);
	ak_fm_start (fm, 1, ak_note_mhz (64), AKW_SINE, 140);
	ak_fm_start (fm, 2, ak_note_mhz (67), AKW_SINE, 140);
	usleep (800000);
	ak_fm_stop (fm, -1);
	while (ak_fm_active (fm)) usleep (20000);

	ak_fm_live (fm, -1);	// stops the thread
	ak_fm_free (fm);
	return 0;
}
```

## Example: MIDI to an MP3

The synthesizer renders offline, as fast as it can, and the encoder writes the file: nothing goes through `sound`.

```c
#include <stdio.h>
#include <audiokit_abi.h>

enum { RATE = 44100, BLOCK = 512 };

static long synth, enc;
static short pcm[BLOCK * 2];

// renders ms milliseconds and hands them to the encoder
static int render (int ms)
{
	for (long n = (long) RATE * ms / 1000; n > 0; n -= BLOCK)
	{
		ak_synth_render (synth, (long) pcm, BLOCK);
		if (ak_enc_write (enc, (long) pcm, BLOCK) < 0) return 0;
	}
	return 1;
}

int main (void)
{
	const char *out = "SD0:/home/arpeggio.mp3";
	if (!audiokit_kit_open ()) return 1;
	synth = ak_synth_new (0, 0);		// the system SoundFont, otherwise the FM bank
	if (synth < 0) return 1;
	enc = ak_enc_open ((long) out, AKF_MP3, RATE, 2, 160000);
	if (enc < 0) { printf ("%s: error %ld\n", out, enc); return 1; }

	ak_synth_midi (synth, 0, 0xC0, 0, 0);		// program 0: piano
	ak_synth_midi (synth, 0, 0xB0, 91, 60);		// a little reverb
	static const int keys[] = { 60, 64, 67, 72, 67, 64 };
	int ok = 1;
	for (int bar = 0; bar < 4 && ok; bar++)
		for (int i = 0; i < 6 && ok; i++)
		{
			int k = keys[i] + (bar & 1 ? 5 : 0);
			ak_synth_midi (synth, 0, 0x90, k, 100);
			ok = render (180);
			ak_synth_midi (synth, 0, 0x80, k, 0);
		}
	if (ok) ok = render (1500);			// the end of the reverb

	long bytes = ak_enc_size (enc);		// before ak_enc_close: the handle is gone after it
	if (ak_enc_close (enc) < 0) ok = 0;
	ak_synth_free (synth);
	printf ("%s: %s, %ld bytes\n", out, ok ? "written" : "error", bytes);
	return ok ? 0 : 1;
}
```

## Example: converting to WAV

Any file the kit decodes becomes a 16-bit WAV, with the source's channels and rate. `ak_dec_seek` keeps only an excerpt.

```c
#include <stdio.h>
#include <stdlib.h>
#include <audiokit_abi.h>

enum { BLOCK = 2048 };

int main (int argc, char **argv)
{
	const char *in = argc > 1 ? argv[1] : "SD0:/res/audiotest/kenney_select.ogg";
	const char *out = argc > 2 ? argv[2] : "SD0:/home/extract.wav";
	long from_ms = argc > 3 ? atol (argv[3]) : 0;
	if (!audiokit_kit_open ()) return 1;

	long d = ak_dec_open ((long) in);
	if (d < 0) { printf ("%s: error %ld\n", in, d); return 1; }
	long info[AKI_SIZE / 8];
	ak_dec_info (d, (long) info);
	long rate = info[AKI_RATE / 8], ch = info[AKI_CHANNELS / 8];
	if (ch > 2) { printf ("%ld channels: the encoder only takes 1 or 2\n", ch); ak_dec_close (d); return 1; }
	if (from_ms > 0 && ak_dec_seek (d, from_ms) < 0) printf ("seek refused: from the start\n");

	long e = ak_enc_open ((long) out, AKF_WAV, rate, ch, 0);
	if (e < 0) { ak_dec_close (d); return 1; }
	short *pcm = malloc (BLOCK * 2 * sizeof (short));
	long n, frames = 0;
	while (pcm && (n = ak_dec_read (d, (long) pcm, BLOCK)) > 0)
	{
		if (ak_enc_write (e, (long) pcm, n) < 0) break;
		frames += n;
	}
	long bytes = ak_enc_size (e);
	ak_enc_close (e);
	ak_dec_close (d);
	free (pcm);
	printf ("%s: %ld frames, %ld Hz, %ld channels, %ld bytes\n", out, frames, rate, ch, bytes);
	return 0;
}
```

## Example: recording what plays

The program lists the outputs from `sound`'s state, then captures three seconds of the main output's loopback (what you hear) into a WAV. With `REC_SRC_MIC`, it would take the microphone, if there is one.

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <audiokit_abi.h>

// from src/inc/sound.inc
enum { REC_SRC_MIC = 0, REC_SRC_LOOP = 1 };
enum { SS_GEN = 0, SS_MAIN = 8, SS_NOUT = 16, SS_OUTS = 64, SO_SIZE = 64,
       SO_ID = 0, SO_VOLUME = 12, SO_MUTE = 16, SO_NAME = 32 };

static void list_outputs (void)
{
	const unsigned char *st = (const unsigned char *) ak_state ();
	if (!st) { printf ("no sound service\n"); return; }
	long n = *(const long *) (st + SS_NOUT), main_out = *(const long *) (st + SS_MAIN);
	for (long i = 0; i < n && i < 8; i++)
	{
		const unsigned char *o = st + SS_OUTS + i * SO_SIZE;
		int id = *(const int *) (o + SO_ID);
		printf ("%c output %d: %.32s, volume %d%s\n", id == main_out ? '*' : ' ', id, (const char *) (o + SO_NAME),
			*(const int *) (o + SO_VOLUME), *(const int *) (o + SO_MUTE) ? " (muted)" : "");
	}
}

int main (void)
{
	if (!audiokit_kit_open ()) return 1;
	list_outputs ();

	long r = ak_rec_open (REC_SRC_LOOP);
	if (r < 0) { printf ("no capture: %ld\n", r); return 1; }
	long e = ak_enc_open ((long) "SD0:/home/loop.wav", AKF_WAV, 44100, 2, 0);
	if (e < 0) { ak_rec_close (r); return 1; }

	static short buf[4096 * 2];
	long frames = 0;
	for (int idle = 0; frames < 44100 * 3 && idle < 500; )	// 5 s with nothing: give up
	{
		long n = ak_rec_read (r, (long) buf, sizeof buf);	// does not wait
		if (n <= 0) { idle++; usleep (10000); continue; }
		idle = 0;
		ak_enc_write (e, (long) buf, n / 4);			// 4 bytes per frame
		frames += n / 4;
	}
	long st[3];
	ak_rec_status (r, (long) st);
	printf ("%ld frames, %ld lost\n", frames, st[2]);
	ak_rec_close (r);
	ak_enc_close (e);
	return 0;
}
```

## Pitfalls

- **`ak_write_all` and `ak_enc_open_io`**: the generated header declares them with the wrong number of arguments (`ak_write_all (void)`, `ak_enc_open_io (long)`). Call them through a function pointer variable of the right type, as in the computed sound example: `typedef long (*write_all_fn) (long, long, long);` then `write_all_fn write_all = (write_all_fn) ak_write_all;`; for `ak_enc_open_io`, a pointer taking 7 `long`. (Calling the cast directly, `((write_all_fn) ak_write_all) (...)`, compiles but gives a warning in C.)
- **Frames and bytes**: `ak_write`, `ak_space`, `ak_rec_read` count **bytes**; `ak_dec_read`, `ak_enc_write`, `ak_*_render` count **frames** (one frame = one sample per channel).
- **The source's channels**: `ak_dec_read` returns the file's channels (1, 2, up to 8 for FLAC or Opus); open the stream with the same channel count and rate. Opus is always 48 kHz.
- **`ak_write` does not wait**: it returns what it could take. In a user interface loop, write what `ak_space` accepts and come back later; in a thread of your own, use `ak_write_all`.
- **The player has its thread**: always call `ak_play_stop`, even after `AKP_ENDED`, otherwise the stream and the file stay open. Do not call `ak_dec_read` on `ak_play_decoder`.
- **No microphone**: `ak_rec_open (REC_SRC_MIC)` fails when there is none, which is the usual case under QEMU and on a Pi without a USB sound card.
- **MIDI synth and threads**: `ak_synth_*` has no lock (`ak_fm_*` has one).
- **Ending an encoder**: without `ak_enc_close`, a WAV has wrong sizes and an MP3 loses its last frame.

## See also

- [MediaKit](mediakit_en.md): video, and the sound of videos through the packet API.
- [EmuKit](emukit_en.md): emulators send their sound through AudioKit.
- [UIKit](uikit_en.md): windows, for a player with a user interface.
- [Kits](kit_en.md): how a kit is loaded and called from C.
