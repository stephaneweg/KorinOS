#!/bin/sh
# render.sh -- une sequence MIDI de Koton -> WAV (FluidSynth, SoundFont General MIDI) -> MP3 (ffmpeg).
#     sh tools/koton/render.sh morceau.mid morceau.mp3 [soundfont.sf2]
# Hors de l'OS : ce n'est pas le synthe d'AudioKit de Koton ; la SoundFont est celle de la machine.
set -eu
SF=${3:-/usr/share/sounds/sf2/FluidR3_GM.sf2}
W=$(mktemp -t koton-render-XXXXXX.wav)
fluidsynth -ni -g 0.8 -r 44100 -R 1 -C 1 -F "$W" "$SF" "$1" >/dev/null
ffmpeg -hide_banner -loglevel error -y -i "$W" -af "loudnorm=I=-16:TP=-1.5" -codec:a libmp3lame -q:a 2 "$2"
rm -f "$W"
