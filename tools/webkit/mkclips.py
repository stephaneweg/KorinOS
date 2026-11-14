# mkclips.py -- les clips d'essai du son et de la video de Jet (docs/WEBKIT.md, « Son et video ») : un motif
# synthetique qui bouge et un ton de 440 Hz, rien de protege ; dans WSL, avec PyAV (ses roues ont libvpx,
# SVT-AV1, Opus, LAME) :
#     python3 -m venv ~/jetav-venv && ~/jetav-venv/bin/pip install av numpy
#     ~/jetav-venv/bin/python -I tools/webkit/mkclips.py ~/korin-webkit-x86_64/clips
# -> WebM VP9 + Opus 480p, VP9 720p (sans son), VP8 + Opus 360p, WebM AV1 + Opus 480p, MP4 AV1 + Opus 360p, VP9 et AV1
# + Opus 240p (4 s, 30 images/s), Ogg Opus, MP3, FLAC, WAV (4 s). tools/pc/sysroot_webkit.sh (TESTS=1) les met dans SD0:/webkit/clips, que
# lit SD0:/webkit/video.html.
import os, sys, math, fractions
import numpy as np, av
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
def frame_img(i, w, h):
    y, x = np.mgrid[0:h, 0:w]
    r = ((x + i * 4) % 256).astype(np.uint8)
    g = ((y + i * 2) % 256).astype(np.uint8)
    b = (((x // 32 + y // 32 + i // 6) % 2) * 200).astype(np.uint8)
    img = np.dstack([r, g, b])
    # un carre blanc qui bouge (horloge visible)
    cx = int((w - 60) * (0.5 + 0.5 * math.sin(i / 12)))
    img[h // 2 - 30:h // 2 + 30, cx:cx + 60] = 255
    return img
def make(name, fmt, vcodec, acodec, w, h, secs=4, fps=30, opts=None, arate=48000):
    c = av.open(f"{out}/{name}", "w", format=fmt)
    vs = None
    if vcodec:
        vs = c.add_stream(vcodec, rate=fps, options=opts or {})
        vs.width, vs.height, vs.pix_fmt = w, h, "yuv420p"
    a = None
    if acodec:
        a = c.add_stream(acodec, rate=arate)
        a.layout = "stereo"
    n = int(secs * fps) if vs else 0
    for i in range(n):
        f = av.VideoFrame.from_ndarray(frame_img(i, w, h), format="rgb24")
        for p in vs.encode(f): c.mux(p)
    if vs:
        for p in vs.encode(): c.mux(p)
    if a:
        fs = a.codec_context.frame_size or 1024
        total = int(secs * arate); t0 = 0
        while t0 < total:
            t = (np.arange(fs) + t0) / arate
            s = (0.2 * np.sin(2 * math.pi * 440 * t)).astype(np.float32)
            fr = av.AudioFrame.from_ndarray(np.vstack([s, s]), format="fltp", layout="stereo")
            fr.sample_rate = arate; fr.pts = t0; fr.time_base = fractions.Fraction(1, arate)
            for p in a.encode(fr): c.mux(p)
            t0 += fs
        for p in a.encode(): c.mux(p)
    c.close()
    print("ok", name)
make("jet-vp9-opus-480p.webm", "webm", "libvpx-vp9", "libopus", 854, 480, opts={"deadline": "realtime", "cpu-used": "8", "b": "800k"})
make("jet-vp9-720p.webm", "webm", "libvpx-vp9", None, 1280, 720, opts={"deadline": "realtime", "cpu-used": "8", "b": "1500k"})
make("jet-vp8-opus-360p.webm", "webm", "libvpx", "libopus", 640, 360, opts={"deadline": "realtime", "cpu-used": "8", "b": "600k"})
make("jet-av1-opus-480p.webm", "webm", "libsvtav1", "libopus", 854, 480, opts={"preset": "12"})
make("jet-av1-opus-360p.mp4", "mp4", "libsvtav1", "libopus", 640, 360, opts={"preset": "12"})
# (petits, une image cle toutes les 15 : sous QEMU, le decodeur suit et le lecteur ne saute pas jusqu'a la fin)
make("jet-vp9-opus-240p.webm", "webm", "libvpx-vp9", "libopus", 426, 240, opts={"deadline": "realtime", "cpu-used": "8", "b": "300k", "g": "15"})
make("jet-av1-opus-240p.webm", "webm", "libsvtav1", "libopus", 426, 240, opts={"preset": "12", "g": "15"})
# (1080p a 30 et 720p a 60 images/s, en colonnes de tuiles comme YouTube : le banc des decodeurs sur plusieurs fils)
make("jet-vp9-1080p.webm", "webm", "libvpx-vp9", None, 1920, 1080, secs=2, opts={"deadline": "realtime", "cpu-used": "8", "b": "3000k", "tile-columns": "2"})
make("jet-vp9-720p60.webm", "webm", "libvpx-vp9", None, 1280, 720, secs=2, fps=60, opts={"deadline": "realtime", "cpu-used": "8", "b": "2500k", "tile-columns": "2"})
make("jet-av1-1080p.webm", "webm", "libsvtav1", None, 1920, 1080, secs=2, opts={"preset": "12"})
make("jet-tone.opus", "ogg", None, "libopus", 0, 0)
make("jet-tone.flac", "flac", None, "flac", 0, 0, arate=44100)
make("jet-tone.wav", "wav", None, "pcm_s16le", 0, 0, arate=44100)
if "libmp3lame" in av.codecs_available: make("jet-tone.mp3", "mp3", None, "libmp3lame", 0, 0, arate=44100)
