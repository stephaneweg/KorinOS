#!/bin/sh
# stage.sh -- depose les programmes WebKit construits ($WK_OUT/out : jet.app, koton.app) dans build/webkit/sdroot du
# depot ; build.ps1 les recopie sur la carte (SD0:/apps/jet.app...). Les binaires (des dizaines de Mo) ne vont jamais
# dans git. Les programmes et pages d'essai (jsc, wctest, wk2test, avplay, uitest, kptest, curltest, SD0:/webkit/) ne
# vont sur la carte qu'avec TESTS=1, quand on en a besoin.
#
#   sh tools/webkit/stage.sh            (TESTS=1 sh tools/webkit/stage.sh : avec les essais)
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
D=$ARMPI/build/webkit/sdroot
rm -rf "$D"
mkdir -p "$D"
for a in jet koton; do
	if [ -d "$WK_OUT/out/$a.app" ]; then
		mkdir -p "$D/apps"
		cp -r "$WK_OUT/out/$a.app" "$D/apps/"
	fi
done
# la chanson d'essai de Koton, la ou son dialogue d'ouverture commence
if [ -f "$WK_OUT/out/koton.app/demo.kson" ]; then
	mkdir -p "$D/home/koton/songs"
	mv "$D/apps/koton.app/demo.kson" "$D/home/koton/songs/"
fi
if [ "${TESTS:-0}" = 1 ]; then
	mkdir -p "$D/bin" "$D/webkit"
	for f in jsc wctest wk2test avplay uitest kptest; do
		if [ -f "$WK_OUT/out/$f" ]; then cp "$WK_OUT/out/$f" "$D/bin/$f"; fi
	done
	# curl sur OpenSSL, l'essai de TLS (docs/OPENSSL.md)
	if [ -f "$WK_OUT/ports/bin/curl.elf" ]; then cp "$WK_OUT/ports/bin/curl.elf" "$D/bin/curltest"; fi
	cp "$ARMPI"/tools/webkit/tests/* "$D/webkit/"
	# la page d'essai de WebGL (docs/MESA.md)
	cp "$ARMPI/tools/mesa/webkit/webgl.html" "$D/webkit/"
	# un son d'essai pour avplay : 3 s de la4 (440 Hz) en WAV s16 stereo 44,1 kHz
	python3 - "$D/webkit/tone.wav" <<'PY'
import math, struct, sys, wave
w = wave.open(sys.argv[1], "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
w.writeframes(b"".join(struct.pack("<hh", v, v) for v in (int(8000 * math.sin(2 * math.pi * 440 * i / 44100)) for i in range(3 * 44100))))
w.close()
PY
fi
ls -lR "$D" | grep -v '^total'
