#!/bin/sh
# fetch.sh -- les sources de SDL, de ses dependances et des deux jeux (docs/SDL.md), dans ~/armpi-sdl/src.
#     sh tools/sdl/fetch.sh
set -eu
. "$(dirname "$0")/env.sh"
cd "$SRC"
get () {	# depot, dossier, etiquette
	[ -d "$2" ] || git clone -q --depth 1 -b "$3" "https://github.com/$1" "$2"
}
get libsdl-org/SDL SDL3 "$SDL_TAG"
get libsdl-org/sdl2-compat sdl2-compat "$COMPAT_TAG"
get libsdl-org/SDL_image SDL2_image release-2.8.8
get kcat/openal-soft openal-soft 1.25.2
get SuperTux/supertux supertux v0.7.0-rev.1
get supertuxkart/stk-code stk-code 1.5
get xiph/ogg ogg v1.3.6
get xiph/vorbis vorbis v1.3.7
get icculus/physfs physfs release-3.2.0
get fmtlib/fmt fmt 12.2.0
get g-truc/glm glm 1.0.3
get libsndfile/libsamplerate libsamplerate 0.2.2
git -C supertux submodule update --init --depth 1 external/tinygettext external/sexp-cpp external/SDL_ttf
git -C supertux submodule update --init --recursive --depth 1 external/simplesquirrel
