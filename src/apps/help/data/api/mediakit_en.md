# MediaKit: video

**MediaKit** (`SD0:/lib/mediakit.so`) opens video files, splits their tracks and decodes the pictures. It is a prebuilt C kit (Onyx's *av* library, libvpx and dav1d), shared by every application: the **Media** library and the `mediaplayer` player use it.

| | |
|---|---|
| Containers | WebM / Matroska (`.webm`, `.mkv`, `.mka`), MP4 / ISO BMFF (fragmented included), RIFF |
| Video | VP8, VP9 (libvpx), 8-bit AV1 (dav1d), uncompressed I420 |
| Picture returned | XRGB8888 (`0x00RRGGBB`), at the picture's size or scaled (bilinear) |
| Sound | **no codec in MediaKit**: Opus, Vorbis, MP3 and FLAC are decoded by [AudioKit](audiokit_en.md); MediaKit returns s16 stereo at the rate you ask for |
| Not supported | H.264, H.265, AAC |

The decoders run on a single thread, the caller's. The card's test videos are in `SD0:/home/video`.

## Opening the kit

Two headers, **in this order**: `mediakit.h` (the structures) then `mediakit_abi.h` (the functions and their relays).

```c
#include <mediakit.h>
#include <mediakit_abi.h>

if (!mediakit_kit_open ()) return 1;   // SD0:/lib/mediakit.so missing
```

Nothing to add when linking: `pibuild` takes every library of `/gcc/lib`. For the sound of a video, also open AudioKit (`audiokit_kit_open`), which plays the PCM returned by MediaKit.

`mediakit.h` also has `mk_kit_open (&api, kit_open)`, another way to open the kit, through a table of function pointers: it is meant for programs that do not link with `libmediakit.a`. With `pibuild`, the relays of `mediakit_abi.h` are enough.

## The functions

A handle (> 0) stands for an open file. It serves **one thread at a time**; several handles on several threads are allowed. Errors are negative.

| Function | Arguments | Result |
|---|---|---|
| `mk_version` | – | `0x00010000` |
| `mk_open` | path | handle, or `MK_E_*`; reads until the tracks are known, picks the first decodable video and the first audio track |
| `mk_close` | handle | 0 |
| `mk_info` | handle, `struct mk_info *` | 0 |
| `mk_track` | handle, track number, `struct mk_track *` | 0 |
| `mk_select` | handle, video track, audio track (-1: none) | 0; the decoders are reopened |
| `mk_video_next` | handle, `struct mk_frame *` (or 0) | 1: a picture decoded and **kept**; 0: end |
| `mk_video_convert` | handle, pixels, pitch (bytes), width, height | 0: the kept picture in XRGB8888 at that size |
| `mk_video_read` | handle, pixels, pitch, width, height, `struct mk_frame *` | 1: both at once; 0: end |
| `mk_audio_open` | handle, rate | 0: `mk_audio_read` will return s16 stereo at that rate; `MK_E_UNSUP`: codec not supported (silent track); `MK_E_NODATA`: no sound |
| `mk_audio_read` | handle, s16 stereo buffer, maximum frames, `int64_t *pts` (or 0) | frames written (0: end); pts (µs) of the first |
| `mk_audio_packet` | handle, `struct mk_packet *` | 1: the next audio packet, raw; 0: end |
| `mk_seek` | handle, time (µs) | time reached (µs, the key frame before); `MK_E_SPIPE`: no index |
| `mk_stats` | handle, `struct mk_stats *` | 0 |
| `mk_error_text` | error | UTF-8 text (owned by the kit) |

## The structures

All numeric fields are 64-bit integers.

| `struct mk_info` | Content |
|---|---|
| `duration` | duration (µs, 0: unknown) |
| `ntracks` | number of tracks |
| `vtrack`, `atrack` | tracks picked (-1: none) |
| `width`, `height` | display size |
| `fps1000` | frames per second × 1000 |
| `rate`, `channels` | sound of the picked track |
| `vcodec`, `acodec` | `MK_C_*` |
| `vok`, `aok` | 1: decodable by MediaKit (the sound: by AudioKit through MediaKit) |
| `container`, `vname`, `aname` | names (16 bytes each): "webm", "VP9", "Opus"... |

| `struct mk_track` | Content |
|---|---|
| `kind` | `MK_VIDEO` 1, `MK_AUDIO` 2 |
| `codec` | `MK_C_VP8`, `MK_C_VP9`, `MK_C_AV1`, `MK_C_H264`... `MK_C_OPUS`, `MK_C_VORBIS`, `MK_C_AAC`, `MK_C_MP3`, `MK_C_FLAC`, `MK_C_PCM_S16LE` |
| `width`, `height`, `rate`, `channels`, `duration` | depending on the kind |
| `extra`, `extra_len` | codec initialization data (for `ak_codec_open`) |
| `supported` | 1: decodable |
| `name`, `codec_id` | track name, codec name in the container ("V_VP9", "A_OPUS") |
| `delay` | µs (Opus pre-skip) |

| Structure | Fields |
|---|---|
| `struct mk_frame` | `pts`, `dur` (µs), `width`, `height` (decoded size), `decode_us` |
| `struct mk_packet` | `data`, `size`, `pts`, `dur`, `key` (1: key packet), `codec` |
| `struct mk_stats` | `vframes`, `vdecode_us`, `aframes`, `adecode_us`, `dropped` (packets thrown away), `heap_big` |

Errors: `MK_E_NOENT` (-2), `MK_E_IO` (-5), `MK_E_BADF` (-9), `MK_E_NOMEM` (-12), `MK_E_INVAL` (-22), `MK_E_SPIPE` (-29), `MK_E_NODATA` (-61), `MK_E_BADMSG` (-74), `MK_E_UNSUP` (-95).

## Reading pictures

`mk_video_next` decodes the next picture and keeps it; `mk_video_convert` converts it as many times as you like, at the size you want, for instance after a resize. `mk_video_read` does both at once. The pitch is in **bytes**; for the UIKit window it is `ui.w * 4`, and you can convert **straight into the window's pixels**.

Packets are queued per track (at most 4096 packets or 48 MB per track): reading the video and the sound in the same thread, alternately, is the intended case. A picked track nobody reads loses its oldest packets; if you do not want the sound, `mk_select (h, vtrack, -1)`.

## Example: what a video contains

A text-mode program: the information, the tracks, then a picture from the middle, scaled down to 160 × 90, and the decoding time.

```c
#include <stdio.h>
#include <stdlib.h>
#include <mediakit.h>
#include <mediakit_abi.h>

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/home/video/sintel-vp9-opus.webm";
	if (!mediakit_kit_open ()) { printf ("MediaKit missing\n"); return 1; }

	long h = mk_open ((long) path);
	if (h < 0) { printf ("%s: %s\n", path, (const char *) mk_error_text (h)); return 1; }
	struct mk_info in;
	mk_info (h, (long) &in);
	printf ("%s: %s, %lld ms, %lld tracks\n", path, in.container, (long long) (in.duration / 1000), (long long) in.ntracks);
	for (long i = 0; i < in.ntracks; i++)
	{
		struct mk_track t;
		if (mk_track (h, i, (long) &t) != 0) continue;
		if (t.kind == MK_VIDEO)
			printf ("  %ld: video %s %lld x %lld%s\n", i, t.codec_id, (long long) t.width, (long long) t.height, t.supported ? "" : " (not supported)");
		else
			printf ("  %ld: sound %s %lld Hz, %lld channels%s\n", i, t.codec_id, (long long) t.rate, (long long) t.channels, t.supported ? "" : " (not supported)");
	}
	if (in.vtrack < 0 || !in.vok) { printf ("no decodable video\n"); mk_close (h); return 0; }

	mk_select (h, in.vtrack, -1);			// no sound: its packets do not pile up
	long at = mk_seek (h, in.duration / 2);		// the key frame before the middle
	if (at < 0) at = 0;					// (MK_E_SPIPE: no index, we stay at the start)
	enum { W = 160, H = 90 };
	unsigned *px = malloc (W * H * 4);
	struct mk_frame f;
	if (px && mk_video_read (h, (long) px, W * 4, W, H, (long) &f) == 1)
	{
		unsigned long r = 0, g = 0, b = 0;
		for (int i = 0; i < W * H; i++) { r += px[i] >> 16 & 255; g += px[i] >> 8 & 255; b += px[i] & 255; }
		printf ("picture at %lld ms (%lld x %lld, decoded in %lld µs): average color #%02lx%02lx%02lx\n",
			(long long) (f.pts / 1000), (long long) f.width, (long long) f.height, (long long) f.decode_us,
			r / (W * H), g / (W * H), b / (W * H));
	}
	struct mk_stats s;
	mk_stats (h, (long) &s);
	printf ("%lld pictures decoded, %lld µs\n", (long long) s.vframes, (long long) s.vdecode_us);
	free (px);
	mk_close (h);
	return 0;
}
```

## Example: a small player

A resizable window in the theme's colors: the picture is converted straight into the window's pixels, fitted under the top band; the sound goes through an AudioKit stream, kept full without waiting. The clock is the system's: simple, but it does not follow an output that falls behind (the `mediaplayer` player locks onto the stream position, `ak_position`).

```c
#include <stdio.h>
#include <time.h>
#include <uikit.h>
#include <mediakit.h>
#include <mediakit_abi.h>
#include <audiokit_abi.h>

enum { RATE = 44100, CHUNK = 1024, TOP = 40 };

static struct ui_vars ui;
static struct mk_info in;
static const char *title;

// a theme color (TJ_*: offsets in the token table)
static unsigned tok (int off) { return *(const unsigned *) ((const char *) ui_jetons () + off) & 0xFFFFFF; }
static long now_us (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000L + t.tv_nsec / 1000; }

// the picture's rectangle: under the top band, proportions kept
static int video_rect (long *x, long *y, long *w, long *h)
{
	long aw = ui.w - 16, ah = ui.h - TOP - 8;
	if (aw < 16 || ah < 16 || in.width <= 0 || in.height <= 0) return 0;
	*w = aw; *h = aw * in.height / in.width;
	if (*h > ah) { *h = ah; *w = ah * in.width / in.height; }
	*x = 8 + (aw - *w) / 2; *y = TOP + (ah - *h) / 2;
	return *w > 0 && *h > 0;
}

static long paint (long w, long h)
{
	long x, y, vw, vh;
	ui_fill (0, 0, ui.w, ui.h, tok (TJ_FACE));
	ui_text ((long) title, 12, 26, tok (TJ_TEXTE1), 14, (long) ui.font_b, 0);
	if (video_rect (&x, &y, &vw, &vh)) ui_fill (x, y, vw, vh, tok (TJ_SOMBRE));
	ui_update_all ();
	return 0;
}

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/home/video/sintel-vp8-opus.webm";
	title = path;
	if (!uikit_open (&ui)) return 1;
	if (!mediakit_kit_open ())
	{
		ui_message ((long) "MediaKit", (long) ui_tr ((long) "SD0:/lib/mediakit.so est absent."), UM_OK);
		return 1;
	}
	long h = mk_open ((long) path);
	if (h < 0) { ui_message ((long) path, mk_error_text (h), UM_OK); return 1; }
	mk_info (h, (long) &in);
	if (in.vtrack < 0 || !in.vok) { ui_message ((long) path, mk_error_text (MK_E_UNSUP), UM_OK); return 1; }

	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	if (ui_window_ex ((scr[0] - 680) / 2, scr[3] + 40, 680, 440, (long) ui_tr ((long) "Vidéo"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (320, 240);
	ui_on_resize ((long) paint, 0);
	paint (ui.w, ui.h);

	// the sound: s16 stereo 44.1 kHz, the format of sound's streams (no conversion)
	long snd = -1;
	if (in.atrack >= 0 && in.aok && audiokit_kit_open () && mk_audio_open (h, RATE) == 0) snd = ak_open (RATE, 2, 16, 0);
	if (snd < 0) mk_select (h, in.vtrack, -1);
	static short pcm[CHUNK * 2];
	int audio_end = snd < 0;

	struct mk_frame fr;
	int have = mk_video_next (h, (long) &fr) == 1;
	long t0 = now_us ();
	for (;;)
	{
		long t = ui_event (have ? 5 : 100);
		if (t == WEV_CLOSE) break;
		while (!audio_end && ak_space (snd) >= CHUNK * 4)
		{
			long n = mk_audio_read (h, (long) pcm, CHUNK, 0);
			if (n <= 0) { audio_end = 1; break; }
			ak_write (snd, (long) pcm, n * 4);
		}
		if (have && fr.pts <= now_us () - t0)		// this picture's time has come
		{
			long x, y, w, vh;
			if (video_rect (&x, &y, &w, &vh) && mk_video_convert (h, (long) (ui.surf + y * ui.w + x), ui.w * 4, w, vh) == 0)
				ui_update (x, y, w, vh);
			have = mk_video_next (h, (long) &fr) == 1;
		}
	}
	if (snd >= 0) ak_close (snd);
	mk_close (h);
	return 0;
}
```

The texts given to `ui_tr` are the French keys of the system dictionary (an app's `lang/en.txt` gives their English version), as in the [skeleton](skeleton_en.md).

## Example: extracting the sound to WAV

MediaKit splits off the audio track and has AudioKit decode it; AudioKit's encoder writes the file. A second argument gives the starting point in seconds.

```c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <mediakit.h>
#include <mediakit_abi.h>
#include <audiokit_abi.h>

enum { RATE = 44100, CHUNK = 4096 };

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/home/video/sintel-vp8-opus.webm";
	const char *out = "SD0:/home/soundtrack.wav";
	if (!mediakit_kit_open () || !audiokit_kit_open ()) return 1;

	long h = mk_open ((long) path);
	if (h < 0) { printf ("%s: %s\n", path, (const char *) mk_error_text (h)); return 1; }
	struct mk_info in;
	mk_info (h, (long) &in);
	if (in.atrack < 0) { printf ("no sound\n"); mk_close (h); return 1; }
	mk_select (h, -1, in.atrack);			// no video: nothing decoded for nothing
	if (argc > 2)
	{
		long at = mk_seek (h, (int64_t) atol (argv[2]) * 1000000);
		printf ("start: %ld ms\n", at < 0 ? 0 : at / 1000);
	}
	long r = mk_audio_open (h, RATE);
	if (r != 0) { printf ("sound %s: %s\n", in.aname, (const char *) mk_error_text (r)); mk_close (h); return 1; }

	long e = ak_enc_open ((long) out, AKF_WAV, RATE, 2, 0);
	if (e < 0) { mk_close (h); return 1; }
	short *pcm = malloc (CHUNK * 4);
	long n, frames = 0;
	int64_t first = -1, pts;
	while (pcm && (n = mk_audio_read (h, (long) pcm, CHUNK, (long) &pts)) > 0)
	{
		if (first < 0) first = pts;
		if (ak_enc_write (e, (long) pcm, n) < 0) break;
		frames += n;
	}
	ak_enc_close (e);
	free (pcm);
	mk_close (h);
	printf ("%s: %ld frames (%ld ms), from %lld ms\n", out, frames, frames * 1000 / RATE, (long long) (first / 1000));
	return 0;
}
```

## Example: raw packets

`mk_audio_packet` returns the audio track's packets as they are in the file: the program decodes them itself, here through AudioKit's packet API, with the track's initialization data. Do not mix it with `mk_audio_read` on the same handle.

```c
#include <stdio.h>
#include <mediakit.h>
#include <mediakit_abi.h>
#include <audiokit_abi.h>

// a container's codec -> AudioKit's format (0: not for AudioKit)
static long ak_format (long mk_codec)
{
	switch (mk_codec)
	{
	case MK_C_OPUS: return AKF_OPUS;
	case MK_C_VORBIS: return AKF_VORBIS;
	case MK_C_MP3: return AKF_MP3;
	case MK_C_FLAC: return AKF_FLAC;
	}
	return 0;
}

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/home/video/sintel-vp8-opus.webm";
	if (!mediakit_kit_open () || !audiokit_kit_open ()) return 1;
	long h = mk_open ((long) path);
	if (h < 0) return 1;
	struct mk_info in;
	struct mk_track t;
	mk_info (h, (long) &in);
	if (in.atrack < 0 || mk_track (h, in.atrack, (long) &t) != 0) { printf ("no sound\n"); mk_close (h); return 1; }
	long fmt = ak_format (t.codec);
	if (!fmt) { printf ("%s: not an AudioKit codec\n", t.codec_id); mk_close (h); return 1; }
	mk_select (h, -1, in.atrack);

	// MP3 has no initialization data
	long c = ak_codec_open (fmt, t.rate, t.channels, fmt == AKF_MP3 ? 0 : (long) t.extra, fmt == AKF_MP3 ? 0 : t.extra_len);
	if (c < 0) { printf ("ak_codec_open: %ld\n", c); mk_close (h); return 1; }
	static short pcm[5760 * 8];			// the largest packet: 5760 Opus frames, 8 channels at most
	struct mk_packet p;
	long packets = 0, frames = 0, bad = 0;
	while (mk_audio_packet (h, (long) &p) == 1)
	{
		long n = ak_codec_decode (c, (long) p.data, p.size, (long) pcm, 5760, AKC_S16);
		packets++;
		if (n < 0) bad++; else frames += n;
	}
	long info[AKI_SIZE / 8];
	ak_codec_info (c, (long) info);
	printf ("%s: %ld packets (%ld refused), %ld frames at %ld Hz, %ld channels, pre-skip %lld µs\n", t.codec_id, packets, bad,
		frames, info[AKI_RATE / 8], info[AKI_CHANNELS / 8], (long long) t.delay);
	ak_codec_close (c);
	mk_close (h);
	return 0;
}
```

## Pitfalls

- **Header order**: `mediakit.h` before `mediakit_abi.h`. The other way round, the `#define MK_C_*` of `mediakit_abi.h` break the `enum` of `mediakit.h`.
- **The pitch is in bytes** for `mk_video_convert` and `mk_video_read` (width × 4 for a full picture), unlike [GpuKit](gpukit_en.md), which counts in pixels.
- **The kept picture**: `mk_video_convert` converts the last picture returned by `mk_video_next`; after the next call, the previous one is gone.
- **A track nobody reads**: its packets pile up to the limit, then the oldest are thrown away (`dropped` in `mk_stats`). Deselect the track you do not use with `mk_select`.
- **`mk_audio_read` and `mk_audio_packet`** exclude each other on the same handle.
- **`mk_seek`** goes back to the key frame before: you must decode (and not show) the pictures up to the time you want. Without an index (WebM without Cues), only 0 works (`MK_E_SPIPE`).
- **Unsupported sound**: `mk_audio_open` returns `MK_E_UNSUP` for AAC; the video stays playable, silent.
- **One thread per handle**: to decode in a separate thread (as Media does), open the handle in that thread or use it only there. The decoders have their own 2 MB stack: nothing to plan for.
- **Under QEMU**, everything works, but slowly: 480p VP9 does not keep up with real time there.

## See also

- [AudioKit](audiokit_en.md): sound, streams, the packet API.
- [UIKit](uikit_en.md): windows and surfaces.
- [GpuKit](gpukit_en.md): drawing with the GPU.
- The help of **Media**, the media library built on MediaKit.
