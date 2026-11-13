#!/bin/bash
# avbench.sh -- les codecs de Jet sans WebKit (docs/WEBKIT.md, « Son et video ») : ce que repondent canPlayType et
# MediaSource.isTypeSupported, puis chaque clip de SD0:/webkit/clips decode au plus vite (avplay --decode : images/s,
# conversion en BGRA comprise) et un clip joue (son par AudioKit, horloge du son).
#   bash SD0:/webkit/avbench.sh [fichier]     (defaut : SD0:/webkit/avbench.txt ; aussi a la sortie)
OUT=${1:-SD0:/webkit/avbench.txt}
cd SD0:/webkit
{
	echo "=== avplay --types"
	SD0:/bin/avplay --types
	for f in clips/*.webm clips/*.mp4; do
		echo "=== avplay --decode $f"
		SD0:/bin/avplay --decode "$f"
	done
	echo "=== avplay clips/jet-vp9-opus-480p.webm (lecture)"
	SD0:/bin/avplay clips/jet-vp9-opus-480p.webm 8
	for f in clips/jet-tone.opus clips/jet-tone.mp3 clips/jet-tone.flac clips/jet-tone.wav; do
		echo "=== avplay $f (lecture)"
		SD0:/bin/avplay "$f" 6
	done
	echo "=== fin"
} > "$OUT" 2>&1
cat "$OUT"
